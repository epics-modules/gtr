/*drvVtr812.c */

/* Author:   Marty Kraimer */
/* Date:     22JUL2002     */

/*************************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of California, as
* Operator of Los Alamos National Laboratory. EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <epicsInterrupt.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <epicsExport.h>

/*Following needed for block transfer requests*/
#include <epicsDma.h>

#include "ellLib.h"
#include "errlog.h"
#include "devLib.h"
#include "drvSup.h"

#include "drvGtr.h"
#include "drvVtr812.h"

int vtr812Debug=0;
int vtr812UseDma = 0;
typedef unsigned int uint32;
typedef unsigned char uint8;

#define STATIC static
#define BUFLEN 2048
#define GROUPSIZE 0x100000
#define GROUPMEMSIZE 0x400000
#define nMemorySize 7
#define Ksamples 1024
#define Msamples 1024*1024

static int memorySize[nMemorySize] = {
    128*Ksamples, 256*Ksamples, 512*Ksamples,
    Msamples, 2*Msamples, 4*Msamples, 8*Msamples
};

typedef enum {vtrType812_10,vtrType812_40} vtrType;
#define vtr812NTypes vtrType812_40 + 1
static const char *vtrname[vtr812NTypes] = {
    "VTR812/10","VTR812/40"
};
static int16 dataMask[vtr812NTypes] = {0x0fff,0x0fff};

#define nclockChoices812_10 16
static char *clockChoices812_10[nclockChoices812_10] = {
    "10 MHz","5 MHz","2.5 MHz","1 MHz",
    "500 KHz","250 KHz","125 KHz","62.5 KHz",
    "Ext","Ext/2","Ext/4","Ext/10",
    "Ext/20","Ext/40","Ext/80","Ext/160"
};
#define nclockChoices812_40 16
static char *clockChoices812_40[nclockChoices812_40] = {
    "40 MHz","20 MHz","10 MHz","4 MHz",
    "2 MHz","1 MHz","0.5 MHz","0.25 MHz",
    "Ext","Ext/2","Ext/4","Ext/10",
    "Ext/20","Ext/40","Ext/80","Ext/160"
};

static int nclockChoices[vtr812NTypes] = {
    nclockChoices812_10,nclockChoices812_40
};
static char **clockChoices[vtr812NTypes] = {
    clockChoices812_10, clockChoices812_40
};

typedef enum {triggerSoft,triggerExt,triggerGate} triggerType;
#define ntriggerChoices 3
static char *triggerChoices[ntriggerChoices] =
{
    "soft","extTrigger","extGate"
};

#define nmultiEventChoices 5
static char *multiEventChoices[nmultiEventChoices] = {
    "1","2","4","8","16"
};
static char *noMultiPrePostChoice[1] = {"no option"};

static int numberEvents[nmultiEventChoices] = {1,2,4,8,16};

typedef enum { armDisarm, armPostTrigger, armPrePostTrigger } armType;
#define narmChoices 3
static char *armChoices[narmChoices] = {
    "disarm","postTrigger","prePostTrigger"
};

/* memory map register offsets */
#define IntStatusID 0x09
#define IRQLevel    0x0B
#define CSR3        0x0D
#define ID          0x0F
#define CSR1        0x21
#define CSR2        0x23
#define Disarm      0x25
#define LBGDR       0x27
#define MBGDR       0x29
#define HBGDR       0x2B
#define SoftTrigger 0x2D
#define ResetMLC    0x2F
#define LBMLC       0x31
#define MBMLC       0x33
#define HBMLC       0x35
#define LBPMemS     0x37
#define MBPMemS     0x39
#define HBPMemS     0x3B
#define PmemCounter 0x3D
#define MultiPrePost 0x3F

typedef struct vtrInfo {
    ELLNODE node;
    epicsDmaId dmaId;
    int     card;
    vtrType type;
    char    *a16;
    int     memsize;
    int     memoffset;
    char    *memory;
    uint32  *buffer;
    int     intVec;
    int     intLev;
    int     hasMultiPrePost;
    int     indMultiEventNumber;
    armType arm;
    triggerType trigger;
    int     numberPTS;
    int     numberPPS;
    int     numberPTE;
    int     numberTriggersSoFar;
    gtrhandler usrIH;
    void    *handlerPvt;
    void    *userPvt;
    int     numberEvents;
} vtrInfo;

static ELLLIST vtrList;
static int vtrIsInited = 0;
static int isRebooting;
#define isArmed(pvtrInfo) ((readRegister((pvtrInfo),CSR2)&0x40) ? 1 : 0)

static int dmaRead(epicsDmaId dmaId,uint32 vmeaddr,uint32 *buffer,int len)
{
    int status;

    if(vtr812Debug)
        printf("dmaRead(%p,%x,%p,%d)\n",dmaId,vmeaddr,buffer,len);

    status = epicsDmaFromVmeAndWait(dmaId,(void *)buffer,
                                        vmeaddr,VME_AM_EXT_SUP_ASCENDING,len,4);
    if(status) {
        printf("vtr812: dmaRead error %s\n",strerror(errno));
        return(-1);
    }
    if(vtr812Debug) {
        printf("dmaRead OK, vmeaddr %8.8x len %d\n",vmeaddr,len);
    }
    return(0);
}

static void writeRegister(vtrInfo *pvtrInfo, int offset,uint8 value)
{
    char *a16 = pvtrInfo->a16;
    uint8 *reg;

    reg = (uint8 *)(a16+offset);
    *reg = value;
    if(vtr812Debug) printf("writeRegister reg %2.2x = %2.2x\n",offset,value);
}

static uint8 readRegister(vtrInfo *pvtrInfo, int offset)
{
    char *a16 = pvtrInfo->a16;
    uint8 *reg;
    uint8 value;

    reg = (uint8 *)(a16+offset);
    value = *reg;
    if(vtr812Debug) printf("readRegister reg %2.2x = %2.2x\n",offset,value);
    return(value);
}

static void writeLocation(vtrInfo *pvtrInfo,uint32 value)
{
    if(vtr812Debug) printf("writeLocation %x\n",value);
    writeRegister(pvtrInfo,HBMLC,(value>>16)&0xff);
    writeRegister(pvtrInfo,MBMLC,(value>>8)&0xff);
    writeRegister(pvtrInfo,LBMLC,value&0xff);
}

static uint32 readLocation(vtrInfo *pvtrInfo)
{
    uint8 low,middle,high;
    uint32 value;

    high = readRegister(pvtrInfo,HBMLC);
    middle = readRegister(pvtrInfo,MBMLC);
    low = readRegister(pvtrInfo,LBMLC);
    value = high<<16 | middle<<8 | low;
    if(vtr812Debug) printf("readLocation %x\n",value);
    return(value);
}

static void writeGate(vtrInfo *pvtrInfo,int value)
{
    if(vtr812Debug) printf("writeGate %x\n",value);
    writeRegister(pvtrInfo,HBGDR,(value>>16)&0xff);
    writeRegister(pvtrInfo,MBGDR,(value>>8)&0xff);
    writeRegister(pvtrInfo,LBGDR,value&0xff);
}

static uint32 readPmemAddress(vtrInfo *pvtrInfo)
{
    uint8 low,middle,high;
    uint32 value;

    high = readRegister(pvtrInfo,HBPMemS);
    middle = readRegister(pvtrInfo,MBPMemS);
    low = readRegister(pvtrInfo,LBPMemS);
    value = high<<16 | middle<<8 | low;
    if(vtr812Debug) printf("readPmemAddress %x\n",value);
    return(value);
}

static void vtrReboot(void *arg)
{
    vtrInfo  *pvtrInfo;

    isRebooting = 1;
    pvtrInfo = (vtrInfo *)ellFirst(&vtrList);
    while(pvtrInfo) {
        writeRegister(pvtrInfo,CSR3,0x05);
        pvtrInfo = (vtrInfo *)ellNext(&pvtrInfo->node);
    }
    vtrIsInited = 0;
}
    
static void initialize()
{
    if(vtrIsInited) return;
    vtrIsInited=1;
    isRebooting = 0;
    ellInit(&vtrList);
    epicsAtExit(vtrReboot,NULL);
}

void vtr812IH(void *arg)
{
    vtrInfo *pvtrInfo = (vtrInfo *)arg;

    /*DONT use readRegister or writeRegister in interrupt handler*/
    if(isRebooting || (pvtrInfo->arm == armDisarm)) {
        *(uint8 *)(pvtrInfo->a16+Disarm) = 1;
        return;
    }
    if(pvtrInfo->arm == armPostTrigger) {
        if(++pvtrInfo->numberTriggersSoFar < pvtrInfo->numberPTE) return;
    } else if (pvtrInfo->arm == armPrePostTrigger) {
        if(pvtrInfo->numberEvents>1) {
            int value = (int)(*(uint8 *)(pvtrInfo->a16+PmemCounter));
            if(value < pvtrInfo->numberEvents)  return;
        }
    }
    *(uint8 *)(pvtrInfo->a16+Disarm) = 1;
    if(pvtrInfo->usrIH) (*pvtrInfo->usrIH)(pvtrInfo->handlerPvt);
}

STATIC void vtrinit(gtrPvt pvt)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    long status;
    
    status = devConnectInterruptVME(pvtrInfo->intVec,
        vtr812IH,(void *)pvtrInfo);
    if(status) {
        errMessage(status,"vtrConfig devConnectInterrupt failed\n");
        return;
    }
    status = devEnableInterruptLevelVME(pvtrInfo->intLev);
    if(status) {
        errMessage(status,"init devEnableInterruptLevel failed\n");
    }
    writeRegister(pvtrInfo,CSR3,0x5);
    writeRegister(pvtrInfo,IntStatusID,pvtrInfo->intVec);
    return;
}

STATIC void vtrreport(gtrPvt pvt,int level)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    printf("%s card %d a16 %p memory %p intVec %2.2x intLev %d multiPrePost %s\n",
        vtrname[pvtrInfo->type],pvtrInfo->card,
        pvtrInfo->a16,pvtrInfo->memory,
        pvtrInfo->intVec,pvtrInfo->intLev,
        (pvtrInfo->hasMultiPrePost ? "yes" : "no"));
}

STATIC gtrStatus vtrclock(gtrPvt pvt, int value)
{ 
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    int nchoices;
    uint8 csr1Value,csr2Value;

    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    if(isRebooting) epicsThreadSuspendSelf();
    nchoices = nclockChoices[pvtrInfo->type];
    if(value<0 || value>=nchoices) return(gtrStatusError);
    csr1Value = readRegister(pvtrInfo,CSR1) & 0xf8;
    csr2Value = readRegister(pvtrInfo,CSR2) & 0xfe;
    if(value >= nchoices/2) { /*Is it external clock?*/
        value -= nchoices/2;
        csr1Value |= value;
        csr2Value |= 0x01;
    } else { /*not external clock*/
        csr1Value |= value;
    }
    writeRegister(pvtrInfo,CSR1,csr1Value);
    writeRegister(pvtrInfo,CSR2,csr2Value);
    return(gtrStatusOK);
}

STATIC gtrStatus vtrtrigger(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    uint8 csr2Value;

    if(isRebooting) epicsThreadSuspendSelf();
    if(value<0 || value>ntriggerChoices) return(gtrStatusError);
    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    csr2Value = readRegister(pvtrInfo,CSR2) & 0xf9;
    switch((triggerType)value) {
        case triggerSoft:
            break;
        case triggerExt:
            csr2Value |= 0x04; break;
        case triggerGate:
            csr2Value |= 0x02; break;
        default:
            printf("%s card %d vtrtrigger illegal value %d\n",
                vtrname[pvtrInfo->type],pvtrInfo->card,value);
            return(gtrStatusError);
    }
    pvtrInfo->trigger = value;
    writeRegister(pvtrInfo,CSR2,csr2Value);
    return(gtrStatusOK);
}

STATIC gtrStatus vtrmultiEvent(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(!pvtrInfo->hasMultiPrePost) {
        if(value!=0) return(gtrStatusError);
        return(gtrStatusOK);
    }
    if(value<0 || value>=nmultiEventChoices) return(gtrStatusError);
    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    if(isRebooting) epicsThreadSuspendSelf();
    pvtrInfo->indMultiEventNumber = value;
    pvtrInfo->numberEvents = numberEvents[pvtrInfo->indMultiEventNumber];
    return(gtrStatusOK);
}

STATIC gtrStatus vtrnumberPTS(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->numberPTS = value;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrnumberPPS(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->numberPPS = value;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrnumberPTE(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->numberPTE = value;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrarm(gtrPvt pvt, int typ)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    uint8 csr2Value;
    armType arm = (armType)typ;

    pvtrInfo->arm = armDisarm;
    writeRegister(pvtrInfo,Disarm,1);
    writeRegister(pvtrInfo,CSR3,0x06); /*disable and Reset IRQ*/
    if(arm==armDisarm) return(gtrStatusOK);
    writeLocation(pvtrInfo,0);
    writeGate(pvtrInfo,pvtrInfo->numberPTS);
    csr2Value = readRegister(pvtrInfo,CSR2) & ~0xf0;
    writeRegister(pvtrInfo,CSR2,csr2Value);
    pvtrInfo->arm = arm;
    writeRegister(pvtrInfo,CSR3,0x00); /*Enable IRQ*/
    switch(arm) {
    case armPostTrigger:
        pvtrInfo->numberTriggersSoFar = 0;
        writeRegister(pvtrInfo,MultiPrePost,0);
        csr2Value |= 0x40;
        writeRegister(pvtrInfo,CSR2,csr2Value);
        break;
    case armPrePostTrigger: {
        if(pvtrInfo->indMultiEventNumber>1) {
            uint8 multi = 0x04|(pvtrInfo->indMultiEventNumber - 1);
            writeRegister(pvtrInfo,MultiPrePost,multi);
        }
        /*NOTE: bits 0x30 must be set twice*/
        csr2Value |= 0x30;
        writeRegister(pvtrInfo,CSR2,csr2Value);
        csr2Value |= 0x40; /*Now also arm*/
        writeRegister(pvtrInfo,CSR2,csr2Value);
        writeRegister(pvtrInfo,CSR2,csr2Value);
        break;
    }
    default:
        printf("%s card %d vtrtrigger illegal value %d\n",
            vtrname[pvtrInfo->type],pvtrInfo->card,arm);
        return(gtrStatusError);
    }
    return(gtrStatusOK);
}

STATIC gtrStatus vtrsoftTrigger(gtrPvt pvt)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    writeRegister(pvtrInfo,SoftTrigger,1);
    return(gtrStatusOK);
}

STATIC void readContiguous(vtrInfo *pvtrInfo,
    gtrchannel *phigh,gtrchannel *plow,uint32 *pmemory,
    int nmax,int *nskipHigh, int *nskipLow)
{
    int16 high,low,mask;
    int ind;
    int bufOffset = BUFLEN;

    if(vtr812Debug)
        printf("readContiguous pmemory %p nmax %d\n",pmemory,nmax);
    mask = dataMask[pvtrInfo->type];
    for(ind=0; ind<nmax; ind++) {
        uint32 word;

        if(pvtrInfo->dmaId && vtr812UseDma) {
            if(bufOffset>=BUFLEN) {
                int status;
                uint32 VMEaddr,bytesRemaining,bytesMax,nbytes;
                VMEaddr = pvtrInfo->memoffset
                    + ((char *)(pmemory) - pvtrInfo->memory)
                    + ind * sizeof(uint32);
                bytesRemaining = (nmax-ind)*sizeof(uint32);
                bytesMax = BUFLEN*sizeof(uint32);
                nbytes = (bytesRemaining<bytesMax) ? bytesRemaining : bytesMax;
                status = dmaRead(pvtrInfo->dmaId,VMEaddr,pvtrInfo->buffer,nbytes);
                if(status) break;
                bufOffset = 0;
            }
            word = pvtrInfo->buffer[bufOffset++];
        } else {
            word = pmemory[ind];
        }
        if(*nskipHigh>0) {
            --*nskipHigh;
        } else if(phigh->ndata<phigh->len) {
            high = (word>>16)&mask;
            (phigh->pdata)[phigh->ndata++] = high;
        }
        if(*nskipLow>0) {
            --*nskipLow;
        } else if(plow->ndata<plow->len) {
            low = word&mask;
            (plow->pdata)[plow->ndata++] = low;
        }
        if((phigh->ndata>=phigh->len) && (plow->ndata>=plow->len)) break;
    }
}

STATIC gtrStatus readPostTrigger(vtrInfo *pvtrInfo,gtrchannel **papgtrchannel)
{
    int indgroup;

    for(indgroup=0; indgroup<4; indgroup++) {
        uint32 *pgroup = (uint32 *)(pvtrInfo->memory + indgroup*GROUPMEMSIZE);
        gtrchannel *phigh,*plow;
        int ndata,nskipHigh,nskipLow;

        ndata = pvtrInfo->numberPTS * pvtrInfo->numberPTE;
        if(ndata>pvtrInfo->memsize) ndata = pvtrInfo->memsize;
        phigh = papgtrchannel[indgroup + 4];
        plow = papgtrchannel[indgroup];
        nskipHigh = nskipLow = 0;
        readContiguous(pvtrInfo,phigh,plow,pgroup,ndata,&nskipHigh,&nskipLow);
    }
    return(gtrStatusOK);
}

STATIC gtrStatus readPrePostTrigger(vtrInfo *pvtrInfo,gtrchannel **papgtrchannel)
{
    int numberPPS = pvtrInfo->numberPPS;
    int nevents = pvtrInfo->numberEvents;
    uint32 eventsize;
    int indevent;

    if(nevents>1) {
        if(nevents != readRegister(pvtrInfo,PmemCounter)) {
            printf("drvVtr812: numberEvents %d but PmemCounter %d\n",
                nevents,readRegister(pvtrInfo,PmemCounter));
        }
    }
    eventsize = pvtrInfo->memsize/nevents;
    if(numberPPS>eventsize) numberPPS = eventsize;
    if(numberPPS==0) return(gtrStatusOK);
    for(indevent=0; indevent<nevents; indevent++) {
        uint32 location;
        int indgroup;

        if(nevents==1) {
            location = readLocation(pvtrInfo);
        } else {
            writeRegister(pvtrInfo,PmemCounter,indevent);
            location = readPmemAddress(pvtrInfo);
        }
        location -= indevent*eventsize;
        if(location>=eventsize) {
            printf("location %x but eventsize %x\n",location,eventsize);
            continue;
        }
        location += 1;
        if(location==eventsize) location = 0;
        for(indgroup=0; indgroup<4; indgroup++) {
            uint32 *pgroup = (uint32 *)(pvtrInfo->memory + indgroup*GROUPMEMSIZE);
            uint32 *pmemory = pgroup + indevent*eventsize;
            gtrchannel *phigh,*plow;
            int nskipHigh,nskipLow,nhigh,nlow,nmax;

            phigh = papgtrchannel[indgroup + 4];
            plow = papgtrchannel[indgroup];
            nhigh = phigh->len - phigh->ndata;
            if(nhigh>numberPPS) nhigh = numberPPS;
            nlow = plow->len - plow->ndata;
            if(nlow>numberPPS) nlow = numberPPS;
            nmax = (nhigh>nlow) ? nhigh : nlow;
            if(nmax<=0) continue;
            nskipHigh = nmax - nhigh;
            nskipLow = nmax - nlow;
            if(location < nmax) {
                int nend,nbeg;

                nend = nmax - location;
                nbeg = nmax - nend;
                readContiguous(pvtrInfo,phigh,plow,
                    (pmemory + eventsize - nend),nend,
                    &nskipHigh,&nskipLow);
                readContiguous(pvtrInfo,phigh,plow,
                    pmemory,nbeg,&nskipHigh,&nskipLow);
            } else {
                readContiguous(pvtrInfo,phigh,plow,
                    (pmemory + location - nmax),nmax,
                    &nskipHigh,&nskipLow);
            }
        }
    }
    return(gtrStatusOK);
}

STATIC gtrStatus vtrreadMemory(gtrPvt pvt,gtrchannel **papgtrchannel)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    int indgroup;

    if(pvtrInfo->arm==armDisarm) return(gtrStatusOK);
    for(indgroup=0; indgroup<4; indgroup++) {
        gtrchannel *phigh;
        gtrchannel *plow;

        phigh = papgtrchannel[indgroup + 4];
        plow = papgtrchannel[indgroup];
        phigh->ndata=0;
        plow->ndata=0;
    }
    if(pvtrInfo->arm==armPostTrigger) {
        readPostTrigger(pvtrInfo,papgtrchannel);
    } else if(pvtrInfo->arm==armPrePostTrigger) {
        readPrePostTrigger(pvtrInfo,papgtrchannel);
    }  else { printf("Illegal arm request\n"); }
    return(gtrStatusOK);
}

STATIC gtrStatus vtrgetLimits(gtrPvt pvt,int16 *rawLow,int16 *rawHigh)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    *rawLow = 0;
    *rawHigh = dataMask[pvtrInfo->type] + 1;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrregisterHandler(gtrPvt pvt,
     gtrhandler usrIH,void *handlerPvt)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
   
    pvtrInfo->usrIH = usrIH;
    pvtrInfo->handlerPvt = handlerPvt;
    return(gtrStatusOK);
}

STATIC int vtrnumberChannels(gtrPvt pvt)
{
    return(8);
}

STATIC gtrStatus vtrclockChoices(gtrPvt pvt,int *number,char ***choice)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    *number = nclockChoices[pvtrInfo->type];
    *choice = clockChoices[pvtrInfo->type];
    return(gtrStatusOK);
}

STATIC gtrStatus vtrarmChoices(gtrPvt pvt,int *number,char ***choice)
{
    *number = narmChoices;
    *choice = armChoices;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrtriggerChoices(gtrPvt pvt,int *number,char ***choice)
{
    *number = ntriggerChoices;
    *choice = triggerChoices;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrmultiEventChoices(gtrPvt pvt,int *number,char ***choice)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    if(!pvtrInfo->hasMultiPrePost) {
        *number = 1;
        *choice = noMultiPrePostChoice;
        return(gtrStatusOK);
    } 
    *number = nmultiEventChoices;
    *choice = multiEventChoices;
    return(gtrStatusOK);
}

static gtrops vtr812ops = {
vtrinit, 
vtrreport, 
vtrclock,
vtrtrigger, 
vtrmultiEvent, 
0, /* no preAverage */
vtrnumberPTS,
vtrnumberPPS,
vtrnumberPTE,
vtrarm,
vtrsoftTrigger,
vtrreadMemory,
0, /* readRawMemory */
vtrgetLimits,
vtrregisterHandler,
vtrnumberChannels,
0, /* numberRawChannels */
vtrclockChoices,
vtrarmChoices,
vtrtriggerChoices,
vtrmultiEventChoices,
0, /* no preAverageChoices */
0,0,0,0,0
};


int vtr812Config(int card,
    int a16offset,unsigned int memoffset,
    int intVec)
{
    char *a16;
    gtrops *pgtrops;
    uint8 probeValue = 0;
    vtrInfo *pvtrInfo;
    long status;
    vtrType type;
    char *memory;
    epicsDmaId dmaId = 0;
    uint8 idModType,idMemSize,multiIndex;

    if(!vtrIsInited) initialize();
    if(gtrFind(card,&pgtrops)) {
        printf("card is already configured\n"); return(0);
    }
    if((a16offset&0xff00)!=a16offset) {
        printf("vtrConfig: illegal a16offset. Must be multiple of 0x0100\n");
        return(0);
    }
    status = devRegisterAddress("vtr812",atVMEA16,
        a16offset,0x100,(void *)&a16);
    if(status) {
        errMessage(status,"vtr812Config devRegisterAddress failed\n");
        return(0);
    }
    if(devReadProbe(1,a16+ID,(char *)&probeValue)!=0) {
        printf("vtrConfig: no card at %#x\n",a16offset);
        return(0);
    }
    idModType = probeValue&0x7;
    idMemSize = (probeValue>>3) &0x7;
    dmaId = epicsDmaCreate(NULL, NULL);
    if(idModType==5) {
        type = vtrType812_10;
    } else if(idModType==6) {
        type = vtrType812_40;
    } else {
        printf("ID %x not vtrType812 or vtrType812_8\n",probeValue);
        return(0);
    }
    if(idMemSize>nMemorySize) {
        printf("vtrConfig ID Register Memory Size Invalid\n");
        return(0);
    }
    if((memoffset&0xFF000000)!=memoffset) {
        printf("memoffset must be multiple of 0xFF000000\n");
        return(0);
    }
    status = devRegisterAddress(vtrname[type],atVMEA32,
        memoffset,0x01000000,(void *)&memory);
    if(status) {
        errMessage(status,"vtrConfig devRegisterAddress failed for memory\n");
        return(0);
    }
    pvtrInfo = calloc(1,sizeof(vtrInfo));
    if(!pvtrInfo) {
        printf("vtrConfig: calloc failed\n");
        return(0);
    }
    if(dmaId) {
        pvtrInfo->buffer = calloc(BUFLEN,sizeof(uint32));
        if(!pvtrInfo->buffer) {
            printf("vtrConfig: calloc failed\n");
            return(0);
        }
    }
    pvtrInfo->dmaId = dmaId;
    pvtrInfo->card = card;
    pvtrInfo->type = type;
    pvtrInfo->a16 = a16;
    pvtrInfo->memsize = memorySize[idMemSize];
    pvtrInfo->memoffset = memoffset;
    pvtrInfo->memory = memory;
    pvtrInfo->intVec = intVec;
    pvtrInfo->intLev = readRegister(pvtrInfo,IRQLevel) & 0x07;
    writeRegister(pvtrInfo,IRQLevel,pvtrInfo->intLev);
    pvtrInfo->numberPTE = 1;
    pvtrInfo->numberEvents = 1;
    /*Determine if multiPrePost option is present*/
    /* Be paranoid and try all values*/
    pvtrInfo->hasMultiPrePost = 1;
    for(multiIndex=0; multiIndex<4; multiIndex++) {
        writeRegister(pvtrInfo,MultiPrePost,multiIndex);
        if(multiIndex!=readRegister(pvtrInfo,MultiPrePost)) {
            pvtrInfo->hasMultiPrePost = 0;
            break;
        }
    }
    ellAdd(&vtrList,&pvtrInfo->node);
    gtrRegisterDriver(card,vtrname[type],&vtr812ops,pvtrInfo);
    return(0);
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg vtr812ConfigArg0 = { "card",iocshArgInt};
static const iocshArg vtr812ConfigArg1 = { "VME A16 offset",iocshArgInt};
static const iocshArg vtr812ConfigArg2 = { "VME memory offset",iocshArgInt};
static const iocshArg vtr812ConfigArg3 = { "interrupt vector",iocshArgInt};
static const iocshArg *vtr812ConfigArgs[] = {
    &vtr812ConfigArg0, &vtr812ConfigArg1, &vtr812ConfigArg2, &vtr812ConfigArg3};
static const iocshFuncDef vtr812ConfigFuncDef =
                      {"vtr812Config",4,vtr812ConfigArgs};
static void vtr812ConfigCallFunc(const iocshArgBuf *args)
{
    vtr812Config(args[0].ival, args[1].ival, args[2].ival, args[3].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvVtr812RegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&vtr812ConfigFuncDef,vtr812ConfigCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvVtr812RegisterCommands);

