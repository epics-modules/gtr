/*drvVtr10012.c */

/* Author:   Marty Kraimer */
/* Date:     01NOV2001     */

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
#include <menuFtype.h>
#include <epicsDma.h>

#include "ellLib.h"
#include "errlog.h"
#include "devLib.h"
#include "drvSup.h"

#include "drvGtr.h"
#include "drvVtr10012.h"

typedef unsigned int uint32;
typedef unsigned short uint16;

#define NSAM10012_8 0x400
#define STATIC static
/* memory map register offsets */
#define RESET      0x00
#define STATUSR    0x02 /* Should be STATUS but vxWorks already defined STATUS*/
#define CONTROL    0x04
#define INTSTATUS  0x06
#define INTSETUP   0x08
#define CLOCK      0x0A
#define ID         0x0C
#define MULPREPOST 0x0E
#define TRIGGER    0x10
#define ARMR       0x12  /* Should be ARM but vxWorks already defined ARM*/
#define DISARM     0x14
#define RESETIRQ   0x16
#define A32BASE    0x1C
#define HGDR       0x20
#define LGDR       0x22
#define HMLC       0x24
#define LMLC       0x26
#define CPTCCDARM  0x30
#define CPTCC      0x32
#define TCOUNTER   0x34

#define BUFLEN 2048

int vtr10012Debug=2;

typedef enum {vtrType10012,vtrType10012_8,vtrType8014,vtrType10014} vtrType;
#define vtrNTypes vtrType10014 + 1
static const char *vtrname[vtrNTypes] = {
    "VTR10012","VTR10012_8","VTR8014","VTR10014"
};
static int16 dataMask[vtrNTypes] = {0x0fff,0x0fff,0x3fff,0x3fff};

#define nclockChoices10012 14
static char *clockChoices10012[nclockChoices10012] = {
    "100 MHz","50 MHz","25 MHz","10 MHz",
    "5 MHz","2.5 MHz","1 MHz",
    "Ext","Ext/2","Ext/4","Ext/10",
    "Ext/20","Ext/40","Ext/100"
};
static uint16 clockValue10012[nclockChoices10012] = {
    0x0000, 0x0001, 0x0002, 0x0003,
    0x0004, 0x0005, 0x0006, 
    0x0008, 0x0009, 0x000a, 0x000b,
    0x000c, 0x000d, 0x000e
};

#define nclockChoices8014 4
static char *clockChoices8014[nclockChoices8014] = {
    "80 MHz","40 MHz",
    "Ext","Ext/2"
};
static uint16 clockValue8014[nclockChoices8014] = {
    0x0000, 0x0001,
    0x0008, 0x0009
};

#define nclockChoices10014 4
static char *clockChoices10014[nclockChoices10014] = {
    "100 MHz","50 MHz",
    "Ext","Ext/2"
};
static uint16 clockValue10014[nclockChoices10014] = {
    0x0000, 0x0001,
    0x0008, 0x0009
};


static int nclockChoices[vtrNTypes] = {
    nclockChoices10012,nclockChoices10012,nclockChoices8014,nclockChoices10014
};
static char **clockChoices[vtrNTypes] = {
    clockChoices10012, clockChoices10012, clockChoices8014,clockChoices10014
};
static uint16 *clockValue[vtrNTypes] = {
    clockValue10012, clockValue10012, clockValue8014, clockValue10014
};

typedef enum {triggerSoft,triggerExt,triggerGate} triggerType;
#define ntriggerChoices 3
static char *triggerChoices[ntriggerChoices] =
{
    "soft","extTrigger","extGate"
};
static uint16 triggerMask[ntriggerChoices] = {
    0x0001, 0x0002, 0x0020
};

#define nmultiEventChoices 5
static char *multiEventChoices[nmultiEventChoices] = {
    "1","2","4","8","16"
};

static int numberEvents[nmultiEventChoices] = {1,2,4,8,16};

typedef enum { armDisarm, armPostTrigger, armPrePostTrigger } armType;
#define narmChoices 3
static char *armChoices[narmChoices] = {
    "disarm","postTrigger","prePostTrigger"
};

typedef struct vtrInfo {
    ELLNODE node;
    epicsDmaId dmaId;
    int     card;
    vtrType type;
    int     nchannels;
    int     arraySize; /* samples per channel */
    char    *a16;
    int     memoffset;
    char    *memory;
    uint32  *buffer;
    int     intVec;
    int     intLev;
    int     indMultiEventNumber;
    armType arm;
    triggerType trigger;
    int     numberPTS;
    int     numberPPS;
    int     numberPTE;
    gtrhandler usrIH;
    void    *handlerPvt;
    void    *userPvt;
    int     numberEvents;
} vtrInfo;

static ELLLIST vtrList;
static int vtrIsInited = 0;
static int isRebooting;
#define isArmed(pvtrInfo) ((readRegister((pvtrInfo),STATUSR)&0x01) ? 1 : 0)

static int dmaRead(epicsDmaId dmaId,uint32 vmeaddr,uint32 *buffer,int len)
{
    int status;

    if(vtr10012Debug)
        printf("dmaRead(%p,%x,%p,%d)\n",dmaId,vmeaddr,buffer,len);

    /*
     * Can use block transfer only on 256-byte boundary
     */
    status = epicsDmaFromVmeAndWait(dmaId,(void *)buffer,
                                            vmeaddr,
                                            (vmeaddr & 0xFF) ?
                                                    VME_AM_EXT_SUP_DATA :
                                                    VME_AM_EXT_SUP_ASCENDING,
                                            len,
                                            4);
    if(status) {
        printf("vtr10012: dmaRead error %s\n",strerror(errno));
        return(-1);
    }
    if(vtr10012Debug) {
        printf("dmaRead OK, vmeaddr %8.8x len %d\n",vmeaddr,len);
    }
    return(0);
}

static void writeRegister(vtrInfo *pvtrInfo, int offset,uint16 value)
{
    char *a16 = pvtrInfo->a16;
    uint16 *reg;

    if(vtr10012Debug>=2)
        printf("VTR %2.2x <- %4.4X\n", offset, value);
    reg = (uint16 *)(a16+offset);
    *reg = value;
}

static uint16 readRegister(vtrInfo *pvtrInfo, int offset)
{
    char *a16 = pvtrInfo->a16;
    uint16 *reg;
    uint16 value;

    reg = (uint16 *)(a16+offset);
    value = *reg;
    return(value);
}

static void writeLocation(vtrInfo *pvtrInfo,int value)
{
    writeRegister(pvtrInfo,HMLC,(value>>16)&0xffff);
    writeRegister(pvtrInfo,LMLC,value&0xffff);
}

static void writeGate(vtrInfo *pvtrInfo,int value)
{
    writeRegister(pvtrInfo,HGDR,(value>>16)&0xffff);
    writeRegister(pvtrInfo,LGDR,value&0xffff);
}

STATIC uint32 readTriggerCounter(vtrInfo *pvtrInfo)
{
    uint16 low,high;

    if(pvtrInfo->type==vtrType10012_8) return(1024);
    high = readRegister(pvtrInfo,TCOUNTER);
    low = readRegister(pvtrInfo,TCOUNTER);
    return(high<<16 | low);
}

static void vtrReboot(void *arg)
{
    vtrInfo  *pvtrInfo;

    isRebooting = 1;
    pvtrInfo = (vtrInfo *)ellFirst(&vtrList);
    while(pvtrInfo) {
        writeRegister(pvtrInfo,RESET,0x01);
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

void vtr10012IH(void *arg)
{
    vtrInfo *pvtrInfo = (vtrInfo *)arg;

    if(isRebooting || (pvtrInfo->arm == armDisarm)) {
        writeRegister(pvtrInfo,DISARM,1); 
        return;
    }
    if(pvtrInfo->type!=vtrType10012_8) {
        uint16 regCPTCC = readRegister(pvtrInfo,CPTCC);
        if(pvtrInfo->arm == armPostTrigger) {
            if(regCPTCC < pvtrInfo->numberPTE) return;
        } else if(pvtrInfo->arm == armPrePostTrigger) {
            if(regCPTCC < pvtrInfo->numberEvents)  return;
        }
    }
    writeRegister(pvtrInfo,DISARM,1); 
    if(pvtrInfo->usrIH) (*pvtrInfo->usrIH)(pvtrInfo->handlerPvt);
}

STATIC void vtrinit(gtrPvt pvt)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    long status;
    
    status = devConnectInterruptVME(pvtrInfo->intVec,
        vtr10012IH,(void *)pvtrInfo);
    if(status) {
        errMessage(status,"vtrConfig devConnectInterrupt failed\n");
        return;
    }
    status = devEnableInterruptLevelVME(pvtrInfo->intLev);
    if(status) {
        errMessage(status,"init devEnableInterruptLevel failed\n");
    }
    writeRegister(pvtrInfo,RESET,1);
    writeRegister(pvtrInfo,INTSTATUS,pvtrInfo->intVec);
    writeRegister(pvtrInfo,INTSETUP,pvtrInfo->intLev);
    writeRegister(pvtrInfo,A32BASE,(pvtrInfo->memoffset)>>24);
    return;
}

STATIC void vtrreport(gtrPvt pvt,int level)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    printf("%s card %d a16 %p memory %p intVec %2.2x intLev %d\n",
        vtrname[pvtrInfo->type],pvtrInfo->card,
        pvtrInfo->a16,pvtrInfo->memory,
        pvtrInfo->intVec,pvtrInfo->intLev);
    if(level >= 1) {
        printf("Status:%4.4X       Control:%4.4X   Clock Setup:%4.4X\n",
                                                readRegister(pvtrInfo,STATUSR),
                                                readRegister(pvtrInfo,CONTROL),
                                                readRegister(pvtrInfo,CLOCK));
        printf("Gate Duration:%u\n",
                (readRegister(pvtrInfo,HGDR)<<16)|readRegister(pvtrInfo,LGDR));
    }
}

STATIC gtrStatus vtrclock(gtrPvt pvt, int value)
{ 
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    int nchoices;

    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    if(isRebooting) epicsThreadSuspendSelf();
    nchoices = nclockChoices[pvtrInfo->type];
    if(value<0 || value>=nchoices) return(gtrStatusError);
    writeRegister(pvtrInfo,CLOCK,clockValue[pvtrInfo->type][value]);
    return(gtrStatusOK);
}

STATIC gtrStatus vtrtrigger(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    uint16 reg;

    if(isRebooting) epicsThreadSuspendSelf();
    if(value<0 || value>ntriggerChoices) return(gtrStatusError);
    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->trigger = value;
    reg = readRegister(pvtrInfo,CONTROL);
    reg = (reg & ~(0x0023)) | triggerMask[pvtrInfo->trigger];
    writeRegister(pvtrInfo,CONTROL,reg);
    return(gtrStatusOK);
}

STATIC gtrStatus vtrmultiEvent(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(value<0 || value>=nmultiEventChoices) return(gtrStatusError);
    if(pvtrInfo->type==vtrType10012_8 && value!=0) return(gtrStatusError);
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
    if(pvtrInfo->type==vtrType10012_8 && value>1024) return(gtrStatusError);
    pvtrInfo->numberPTS = value;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrnumberPPS(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    if(pvtrInfo->type==vtrType10012_8) return(gtrStatusError);
    pvtrInfo->numberPPS = value;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrnumberPTE(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    if(pvtrInfo->type==vtrType10012_8) return(gtrStatusError);
    pvtrInfo->numberPTE = value;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrarm(gtrPvt pvt, int typ)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    uint16 reg,regControl;
    armType arm = (armType)typ;

    if(pvtrInfo->type==vtrType10012_8
    &&(arm!=armDisarm && arm!=armPostTrigger)) return(gtrStatusError);
    pvtrInfo->arm = arm;
    writeRegister(pvtrInfo,DISARM,1);
    writeRegister(pvtrInfo,RESETIRQ,1);
    if(pvtrInfo->arm==armDisarm) return(gtrStatusOK);
    writeLocation(pvtrInfo,0);
    writeGate(pvtrInfo,pvtrInfo->numberPTS);
    reg = readRegister(pvtrInfo,INTSETUP);
    writeRegister(pvtrInfo,INTSETUP,reg|0x0008); /*IRQ Enable*/
    regControl = ~0x0048 & readRegister(pvtrInfo,CONTROL);
    writeRegister(pvtrInfo,CONTROL,regControl);
    writeRegister(pvtrInfo,CPTCC,1);
    writeRegister(pvtrInfo,TCOUNTER,1);
    switch(arm) {
    case armPostTrigger:
        writeRegister(pvtrInfo,MULPREPOST,0);
        writeRegister(pvtrInfo,CONTROL,regControl);
        writeRegister(pvtrInfo,ARMR,1);
        break;
    case armPrePostTrigger: {
        uint16 multi = 0;
        if(pvtrInfo->indMultiEventNumber>0)
            multi = 0x0004|(pvtrInfo->indMultiEventNumber - 1);
        writeRegister(pvtrInfo,MULPREPOST,multi);
        regControl |= 0x0048;
        writeRegister(pvtrInfo,CONTROL,regControl);
        writeRegister(pvtrInfo,ARMR,1);
        break;
    }
    default:
        printf("vtrarm: Illegal value\n");
    }
    return(gtrStatusOK);
}

STATIC gtrStatus vtrsoftTrigger(gtrPvt pvt)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    writeRegister(pvtrInfo,TRIGGER,1);
    return(gtrStatusOK);
}

STATIC void readContiguous(vtrInfo *pvtrInfo,
    gtrchannel *phigh,gtrchannel *plow,uint32 *pmemory,
    int nmax,int *nskipHigh, int *nskipLow)
{
    int16 high,low,mask;
    int ind;
    int bufOffset = BUFLEN;

    if(vtr10012Debug)
        printf("readContiguous pmemory %p nmax %d\n",pmemory,nmax);
    mask = dataMask[pvtrInfo->type];
    for(ind=0; ind<nmax; ind++) {
        uint32 word;

        if(pvtrInfo->dmaId && !pvtrInfo->buffer) {
            pvtrInfo->buffer = calloc(BUFLEN,sizeof(uint32));
            if(!pvtrInfo->buffer) {
                printf("vtrConfig: calloc failed\n");
                pvtrInfo->dmaId = 0;
            }
        }
        if(pvtrInfo->dmaId) {
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
        uint32 *pgroup = (uint32 *)(pvtrInfo->memory + indgroup*0x00400000);
        gtrchannel *phigh,*plow;
        int ndata,nskipHigh,nskipLow;

        ndata = pvtrInfo->numberPTS * pvtrInfo->numberPTE;
        if(ndata>pvtrInfo->arraySize) ndata = pvtrInfo->arraySize;
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
    int indevent,eventsize;

    if(nevents != readRegister(pvtrInfo,CPTCCDARM)) {
        printf("drvVtr10012: numberEvents %d but CPTCCDARM %d\n",
            nevents,readRegister(pvtrInfo,CPTCCDARM));
    }
    writeRegister(pvtrInfo,TCOUNTER,1);
    eventsize = pvtrInfo->arraySize/nevents;
    if(numberPPS>eventsize) numberPPS = eventsize;
    if(numberPPS==0) numberPPS = eventsize;
    for(indevent=0; indevent<nevents; indevent++) {
        int location = readTriggerCounter(pvtrInfo);
        int indgroup;

        location -= indevent*eventsize;
        if(location<0 || location>=eventsize) {
            printf("location %d but eventsize %d\n",location,eventsize);
            continue;
        }
        /*readTriggerCounter returns location of last value saved*/
        location += 1;
        if(location==eventsize) location = 0;
        for(indgroup=0; indgroup<4; indgroup++) {
            uint32 *pgroup = (uint32 *)(pvtrInfo->memory + indgroup*0x00400000);
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
    if(pvtrInfo->type==vtrType10012_8 && pvtrInfo->arm!=armPostTrigger)
        return(gtrStatusError);
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

STATIC gtrStatus vtrreadRawMemory(gtrPvt pvt,gtrchannel **papgtrchannel)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    int indgroup;

    if(pvtrInfo->arm==armDisarm) return(gtrStatusOK);
    if(pvtrInfo->type==vtrType10012_8 && pvtrInfo->arm!=armPostTrigger)
        return(gtrStatusError);
    for(indgroup=0; indgroup<4; indgroup++) {
        uint32 *pgroup = (uint32 *)(pvtrInfo->memory + indgroup*0x00400000);
        gtrchannel *pchan;
        int nnow, nchan;

        pchan = papgtrchannel[indgroup];
        pchan->ndata=0;
        if(pchan->len==0) continue; /* No waveform record */
        if(pchan->ftvl!=menuFtypeLONG) return(gtrStatusError);
        nchan = pchan->len;
        switch(pvtrInfo->arm) {
        case armPostTrigger:
            nnow = (readRegister(pvtrInfo,HMLC) << 16) | readRegister(pvtrInfo,LMLC);
            if(nnow>nchan)
                nnow = nchan;
            if(pvtrInfo->dmaId) {
                if(dmaRead(pvtrInfo->dmaId,
                        pvtrInfo->memoffset+((char *)pgroup-pvtrInfo->memory),
                        (uint32 *)pchan->pdata,
                        nnow*sizeof(uint32)) < 0)
                    return(gtrStatusError);
            }
            else {
                bcopyLongs((char *)pgroup, (char *)pchan->pdata, nnow);
            }
            pchan->ndata = nnow;
            break;
        
        default:
            return(gtrStatusError);
        }
    }
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
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    return(pvtrInfo->nchannels);
}

STATIC int vtrnumberRawChannels(gtrPvt pvt)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    return(pvtrInfo->nchannels/2);
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
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
   
    *number = (pvtrInfo->type==vtrType10012_8) ? 2 : narmChoices;
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
   
    *number = (pvtrInfo->type==vtrType10012_8) ? 1 : nmultiEventChoices;
    *choice = multiEventChoices;
    return(gtrStatusOK);
}

static gtrops vtr10012ops = {
vtrinit, 
vtrreport, 
vtrclock,
vtrtrigger, 
vtrmultiEvent, 
0, /* No preAverage */
vtrnumberPTS,
vtrnumberPPS,
vtrnumberPTE,
vtrarm,
vtrsoftTrigger,
vtrreadMemory,
vtrreadRawMemory,
vtrgetLimits,
vtrregisterHandler,
vtrnumberChannels,
vtrnumberRawChannels,
vtrclockChoices,
vtrarmChoices,
vtrtriggerChoices,
vtrmultiEventChoices,
0, /* No preAverageChoices */
0,0,0,0,0
};


int vtr10012Config(int card,
    int a16offset,unsigned int memoffset,
    int intVec,int intLev, int useDma, int nchannels, int kilosamplesPerChan)
{
    char *a16;
    gtrops *pgtrops;
    uint16 probeValue = 0;
    vtrInfo *pvtrInfo;
    long status;
    vtrType type;
    char *memory;
    uint32 *buffer = 0;
    epicsDmaId dmaId = 0;

    if(!vtrIsInited) initialize();
    if(gtrFind(card,&pgtrops)) {
        printf("card is already configured\n"); return(0);
    }
    if((a16offset&0xff00)!=a16offset) {
        printf("vtrConfig: illegal a16offset. Must be multiple of 0x0100\n");
        return(0);
    }
    status = devRegisterAddress("vtr",atVMEA16,
        a16offset,0x100,(void *)&a16);
    if(status) {
        errMessage(status,"vtrConfig devRegisterAddress failed\n");
        return(0);
    }
    if(devReadProbe(2,a16+ID,(char *)&probeValue)!=0) {
        printf("vtrConfig: no card at %#x\n",a16offset);
        return(0);
    }
    if (useDma) {
        dmaId = epicsDmaCreate(NULL, NULL);
        if(!dmaId)
            printf("vtrConfig: DMA requested, but not available.\n");
    }
    probeValue = (probeValue>>10);
    if(probeValue==7) {
        type = vtrType10012;
    } else if(probeValue==8) {
        type = vtrType10012_8;
        if(!dmaId) {
            printf("dma is not available but vtr10012_8 requires it.\n");
            return(0);
        }
        buffer = calloc(BUFLEN,sizeof(uint32));
        if(!buffer) {
            printf("vtrConfig: calloc failed\n");
            return(0);
        }
    } else if(probeValue==9) {
        type = vtrType8014;
    } else if(probeValue==10) {
        type = vtrType10014;
    } else {
        printf("ID %x not vtrType10012 or vtrType10012_8 or vtrType8014\n",probeValue);
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
    pvtrInfo->dmaId = dmaId;
    pvtrInfo->card = card;
    pvtrInfo->type = type;
    pvtrInfo->nchannels = (nchannels ? nchannels : 8);
    if(kilosamplesPerChan==0) kilosamplesPerChan = 256;
    pvtrInfo->arraySize = kilosamplesPerChan * 1024;
    pvtrInfo->a16 = a16;
    pvtrInfo->memoffset = memoffset;
    pvtrInfo->memory = memory;
    pvtrInfo->intVec = intVec;
    pvtrInfo->intLev = intLev;
    pvtrInfo->numberPTE = 1;
    pvtrInfo->buffer = buffer;
    if(type==vtrType10012_8) {
        pvtrInfo->numberPTS = NSAM10012_8;
        pvtrInfo->numberPTE = 1;
    }
    pvtrInfo->numberEvents = 1;
    ellAdd(&vtrList,&pvtrInfo->node);
    gtrRegisterDriver(card,vtrname[type],&vtr10012ops,pvtrInfo);
    return(0);
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg vtr10012ConfigArg0 = { "card",iocshArgInt};
static const iocshArg vtr10012ConfigArg1 = { "VME A16 offset",iocshArgInt};
static const iocshArg vtr10012ConfigArg2 = { "VME memory offset",iocshArgInt};
static const iocshArg vtr10012ConfigArg3 = { "interrupt vector",iocshArgInt};
static const iocshArg vtr10012ConfigArg4 = { "interrupt level",iocshArgInt};
static const iocshArg vtr10012ConfigArg5 = { "use DMA",iocshArgInt};
static const iocshArg vtr10012ConfigArg6 = { "nchannels",iocshArgInt};
static const iocshArg vtr10012ConfigArg7 = { "kilosamplesPerChan",iocshArgInt};
static const iocshArg *vtr10012ConfigArgs[] = {
    &vtr10012ConfigArg0, &vtr10012ConfigArg1, &vtr10012ConfigArg2,
    &vtr10012ConfigArg3, &vtr10012ConfigArg4, &vtr10012ConfigArg5,
    &vtr10012ConfigArg6,&vtr10012ConfigArg7};
static const iocshFuncDef vtr10012ConfigFuncDef =
                      {"vtr10012Config",8,vtr10012ConfigArgs};
static void vtr10012ConfigCallFunc(const iocshArgBuf *args)
{
    vtr10012Config(args[0].ival, args[1].ival, args[2].ival,
                 args[3].ival, args[4].ival, args[5].ival,
                 args[6].ival, args[7].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvVtr10012RegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&vtr10012ConfigFuncDef,vtr10012ConfigCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvVtr10012RegisterCommands);
