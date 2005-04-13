/* Author:   Marty Kraimer */
/* Date:     17SEP2001     */

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
#include <limits.h>

#include <menuFtype.h>
#include <epicsDma.h>
#include <epicsInterrupt.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <epicsExport.h>

#include "ellLib.h"
#include "errlog.h"
#include "devLib.h"
#include "drvSup.h"

#include "drvGtr.h"
#include "drvSisfadc.h"

/*
 * Size of local cache
 */
#define DMA_BUFFER_CAPACITY   2048

/*
 * Uncomment to produce timing signals on user output
#define EMIT_TIMING_MARKERS 1
 */

typedef unsigned int uint32;

#define STATIC static
/* memory map register offset 1s  and sizes*/
#define CSR         0x00000000
#define MODID       0x00000004
#define INTCONFIG   0x00000008
#define INTCONTROL  0x0000000c
#define ACQCSR      0x00000010
#define STOPDELAY   0x00000018
#define RESET       0x00000020
#define START       0x00000030
#define STOP        0x00000034
#define EVENTCONFIG 0x00100000
#define READEVENTCONFIG 0x00200000
#define MAXEVENTS   0x0010002C
#define TRIGGEREVENTDIRECTORY 0x00101000
#define READMAXEVENTS 0x0020002C
#define BANK1ADDRESS 0x00200008
#define BANK2ADDRESS 0x0020000C
#define EVENTCOUNTER 0x00200010

#define EVENTSTART  0x00200000
#define EVENTBYTES  0x00080000

#define MEMORYSTART 0x00400000
#define ARRAYBYTES  0x00080000
#define ARRAYSIZE   ARRAYBYTES/4

static char *clockChoices3300[] = {
    "100 MHz","50 MHz","25 MHz","12.5 MHz","6.25 MHz","3.125 MHz",
    "extClock","P2-Clock","Random"
};
static int clockSource3300[] = { 0x0000,0x1000,0x2000,0x3000,0x4000,0x5000,0x6000,0x7000,0x0800};

static char *clockChoices3301_65[] = {
    "50 MHz","25 MHz",
    "extClock","P2-Clock","Random"
};
static int clockSource3301_65[] = { 0x1000,0x2000,0x6000,0x7000,0x1800};

static char *clockChoices3301_80[] = {
    "80 MHz","40 MHz","20 MHz",
    "extClock","P2-Clock","Random"
};
static int clockSource3301_80[] = { 0x0000,0x1000,0x2000,0x6000,0x7000,0x0800};

static char *clockChoices3301_105[] = {
    "100 MHz","50 MHz","25 MHz",
    "extClock","P2-Clock","Random"
};
static int clockSource3301_105[] = { 0x0000,0x1000,0x2000,0x6000,0x7000,0x0800};

typedef struct sisTypeInfo {
    const char *name;
    char **papclockChoices;
    int *paClockSource;
    int nclockChoices;
    int16 dataMask;
}sisTypeInfo;

static sisTypeInfo pasisTypeInfo[] = {
    {"sisType3300",clockChoices3300,clockSource3300,
        sizeof(clockChoices3300)/sizeof(char *),0x0fff},
    {"sisType3301_65",clockChoices3301_65,clockSource3301_65,
        sizeof(clockChoices3301_65)/sizeof(char *),0x3fff},
    {"sisType3301_80",clockChoices3301_80,clockSource3301_80,
        sizeof(clockChoices3301_80)/sizeof(char *),0x3fff},
    {"sisType3301_105",clockChoices3301_105,clockSource3301_105,
        sizeof(clockChoices3301_105)/sizeof(char *),0x3fff}
};

typedef enum {
    sisType3300,
    sisType3301_65,
    sisType3301_80,
    sisType3301_105
}sisType;

typedef enum {triggerSoft,triggerFPSS,triggerP2SS,triggerFPGate} triggerType;
#define ntriggerChoices 4
static char *triggerChoices[ntriggerChoices] =
{
    "soft","FP start/stop","P2 start/stop","FP gate"
};
static int acrTriggerMask[ntriggerChoices] = {
    0x000,0x100,0x200,0x500
};

#define nmultiEventChoices 8
static char *multiEventChoices[nmultiEventChoices] = {
    "1","8","32","64","128","256","512","1024"
};
static int multiEventNumber[nmultiEventChoices] = {
    1,8,32,64,128,256,512,1024
};

#define npreAverageChoices 8
static char *preAverageChoices[npreAverageChoices] = {
    "1","2","4","8","16","32","64","128"
};

typedef enum { armDisarm, armPostTrigger, armPrePostTrigger } armType;
#define narmChoices 3
static char *armChoices[narmChoices] = {
    "disarm","postTrigger","prePostTrigger"
};

typedef struct sisInfo {
    ELLNODE     node;
    int         card;
    char        *name;
    sisTypeInfo *psisTypeInfo;
    char        *a32;
    int         intVec;
    int         intLev;
    int         indMultiEventNumber;
    int         preAverageChoice;
    armType     arm;
    triggerType trigger;
    int         numberPTS;
    int         numberPPS;
    int         numberPTE;
    gtrhandler usrIH;
    void        *handlerPvt;
    void        *userPvt;
    epicsDmaId  dmaId;
    long        *dmaBuffer;
} sisInfo;

static ELLLIST sisList;
static int sisIsInited = 0;
static int isRebooting;
static int sisFadcDebug = 0;

static void writeRegister(sisInfo *psisInfo, int offset,uint32 value)
{
    char *a32 = psisInfo->a32;
    uint32 *reg;

    if(sisFadcDebug >= 2) {
        volatile static struct {
            uint32 offset;
            uint32 value;
        } irqBuf[10];
        volatile static int irqIn;
        static int irqOut;

        if(epicsInterruptIsInterruptContext()) {
            irqBuf[irqIn].offset = offset;
            irqBuf[irqIn].value = value;
            if(irqIn == (((sizeof irqBuf)/sizeof irqBuf[0]) - 1))
                irqIn = 0;
            else
                irqIn++;
        }
        else {
            while(irqOut != irqIn) {
                printf("sisfadcirq: 0x%.8x -> %#x\n", irqBuf[irqOut].value, irqBuf[irqOut].offset);
                if(irqOut == (((sizeof irqBuf)/sizeof irqBuf[0]) - 1))
                    irqOut = 0;
                else
                    irqOut++;
            }
        }
        printf("sisfadc: 0x%.8x -> %#x\n", value, offset);
    }
    reg = (uint32 *)(a32+offset);
    *reg = value;
}

static uint32 readRegister(sisInfo *psisInfo, int offset)
{
    char *a32 = psisInfo->a32;
    uint32 *reg;
    uint32 value;

    reg = (uint32 *)(a32+offset);
    value = *reg;
    return(value);
}

static void sisReboot(void *arg)
{
    sisInfo  *psisInfo;

    isRebooting = 1;
    psisInfo = (sisInfo *)ellFirst(&sisList);
    while(psisInfo) {
        writeRegister(psisInfo,RESET,1);
        psisInfo = (sisInfo *)ellNext(&psisInfo->node);
    }
}
    
static void initialize()
{
    if(sisIsInited) return;
    sisIsInited=1;
    isRebooting = 0;
    ellInit(&sisList);
   epicsAtExit(sisReboot,NULL);
}

void sisIH(void *arg)
{
    sisInfo *psisInfo = (sisInfo *)arg;

    writeRegister(psisInfo,ACQCSR,0x000f0000);
    writeRegister(psisInfo,INTCONTROL,0x00ff0000);
    if(isRebooting) return;
    switch(psisInfo->arm) {
    case armDisarm:
        break;
    case armPostTrigger:
    case armPrePostTrigger:
        if(psisInfo->usrIH) (*psisInfo->usrIH)(psisInfo->handlerPvt);
        break;
    default:
        epicsInterruptContextMessage("drvSisfadc::sisIH Illegal armType\n");
        break;
    }
}

STATIC void readContiguous(sisInfo *psisInfo,
    gtrchannel *phigh,gtrchannel *plow,uint32 *pmemory,
    int nmax,int *nskipHigh, int *nskipLow)
{
    int16 high,low,himask,lomask;
    int ind;

    himask = lomask = psisInfo->psisTypeInfo->dataMask;
    if(psisInfo->trigger == triggerFPGate)
        lomask |= 0x8000;  /* Let G bit through */
    for(ind=0; ind<nmax; ind++) {
        uint32 word;
        if(psisInfo->dmaId) {
            if((psisInfo->dmaBuffer == NULL)
             && ((psisInfo->dmaBuffer = malloc(DMA_BUFFER_CAPACITY*sizeof(epicsUInt32))) == NULL)) {
                printf("No memory for SIS3301 DMA buffer.  Falling back to non-DMA opertaion\n");
                psisInfo->dmaId = NULL;
                word = pmemory[ind];
            }
            else {
                int dmaInd = ind % DMA_BUFFER_CAPACITY;
                if(dmaInd == 0) {
                    unsigned int nnow = nmax - ind;
                    if(nnow > DMA_BUFFER_CAPACITY)
                        nnow = DMA_BUFFER_CAPACITY;
#ifdef EMIT_TIMING_MARKERS
                    writeRegister(psisInfo,CSR,0x00000002);
#endif
                    if(epicsDmaFromVmeAndWait(psisInfo->dmaId,
                                   psisInfo->dmaBuffer,
                                   (unsigned long)(pmemory + ind),
                                   VME_AM_EXT_SUP_ASCENDING,
                                   nnow*sizeof(long),
                                   sizeof(long)) != 0) {
                        printf("Can't perform DMA: %s\n", strerror(errno));
                        psisInfo->dmaId = NULL;
                        psisInfo->dmaBuffer[dmaInd] = pmemory[ind];
                    }
#ifdef EMIT_TIMING_MARKERS
                    writeRegister(psisInfo,CSR,0x00020000);
#endif
                }
                word = psisInfo->dmaBuffer[dmaInd];
            }
        }
        else {
            word = pmemory[ind];
        }
        if(*nskipHigh>0) {
            --*nskipHigh;
        } else if(phigh->ndata<phigh->len) {
            high = (word>>16)&himask;
            (phigh->pdata)[phigh->ndata++] = high;
        }
        if(*nskipLow>0) {
            --*nskipLow;
        } else if(plow->ndata<plow->len) {
            low = word&lomask;
            (plow->pdata)[plow->ndata++] = low;
        }
        if((phigh->ndata>=phigh->len) && (plow->ndata>=plow->len)) break;
    }
}

STATIC void sisinit(gtrPvt pvt)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    long status;
    
    status = devConnectInterruptVME(psisInfo->intVec,
        sisIH,(void *)psisInfo);
    if(status) {
        errMessage(status,"init devConnectInterrupt failed\n");
        return;
    }
    status = devEnableInterruptLevelVME(psisInfo->intLev);
    if(status) {
        errMessage(status,"init devEnableInterruptLevel failed\n");
    }
    writeRegister(psisInfo,RESET,1);
    writeRegister(psisInfo,INTCONFIG,
        (0x00001800 | (psisInfo->intLev <<8) | psisInfo->intVec));
    writeRegister(psisInfo,CSR,0x00000001); /* turn on user LED */
    return;
}

STATIC void sisreport(gtrPvt pvt,int level)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    uint32 value;

    printf("%s card %d a32 %p intVec %2.2x intLev %d\n",
        psisInfo->name,psisInfo->card,psisInfo->a32,
        psisInfo->intVec,psisInfo->intLev);
    if(level<1) return;
    value = readRegister(psisInfo,CSR);
    printf("    CSR %8.8x",value);
    value = readRegister(psisInfo,MODID);
    printf(" MODID %8.8x",value);
    value = readRegister(psisInfo,INTCONFIG);
    printf(" INTCONFIG %8.8x",value);
    value = readRegister(psisInfo,INTCONTROL);
    printf(" INTCONTROL %8.8x",value);
    value = readRegister(psisInfo,ACQCSR);
    printf(" ACQCSR %8.8x",value);
    printf("\n");
    value = readRegister(psisInfo,READEVENTCONFIG);
    printf("   EVENTCONFIG %8.8x",value);
    value = readRegister(psisInfo,STOPDELAY);
    printf("  STOPDELAY %u",value);
    value = readRegister(psisInfo,READMAXEVENTS);
    printf(" MAXEVENTS %u",value);
    printf("\n");
}

STATIC gtrStatus sisclock(gtrPvt pvt, int value)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    int clockChoice;
    
    if(value<0 || value>=psisInfo->psisTypeInfo->nclockChoices)
        return(gtrStatusError);
    clockChoice = psisInfo->psisTypeInfo->paClockSource[value];
    writeRegister(psisInfo,ACQCSR,0x78000000);
    writeRegister(psisInfo,ACQCSR,clockChoice);
    writeRegister(psisInfo,EVENTCONFIG,
        (readRegister(psisInfo,READEVENTCONFIG) & ~0x800) |
        (clockChoice & 0x800));
    return(gtrStatusOK);
}

STATIC gtrStatus sistrigger(gtrPvt pvt, int value)
{
    sisInfo *psisInfo = (sisInfo *)pvt;

    if(value<0 || value>=ntriggerChoices) return(gtrStatusError);
    psisInfo->trigger = value;
    return(gtrStatusOK);
}

STATIC gtrStatus sismultiEvent(gtrPvt pvt, int value)
{
    sisInfo *psisInfo = (sisInfo *)pvt;

    if(value<0 || value>=nmultiEventChoices) return(gtrStatusError);
    if(isRebooting) epicsThreadSuspendSelf();
    psisInfo->indMultiEventNumber = value;
    return(gtrStatusOK);
}

STATIC gtrStatus sispreAverage(gtrPvt pvt, int value)
{
    sisInfo *psisInfo = (sisInfo *)pvt;

    if(value<0 || value>=npreAverageChoices) return(gtrStatusError);
    if(isRebooting) epicsThreadSuspendSelf();
    psisInfo->preAverageChoice = value;
    return(gtrStatusOK);
}

STATIC gtrStatus sisnumberPTS(gtrPvt pvt, int value)
{
    sisInfo *psisInfo = (sisInfo *)pvt;

    psisInfo->numberPTS = value;
    return(gtrStatusOK);
}

STATIC gtrStatus sisnumberPPS(gtrPvt pvt, int value)
{
    sisInfo *psisInfo = (sisInfo *)pvt;

    psisInfo->numberPPS = value;
    return(gtrStatusOK);
}
STATIC gtrStatus sisnumberPTE(gtrPvt pvt, int value)
{
    sisInfo *psisInfo = (sisInfo *)pvt;

    psisInfo->numberPTE = value;
    return(gtrStatusOK);
}

STATIC gtrStatus sisarm(gtrPvt pvt, int value)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    uint32 acr,ecr;
    

    writeRegister(psisInfo,ACQCSR,0x07ff0000);
    writeRegister(psisInfo,INTCONTROL,0x00ff0000);
    psisInfo->arm = value;
    if(psisInfo->arm==armDisarm) return(gtrStatusOK);
    if((psisInfo->trigger==triggerFPGate) && (psisInfo->arm!=armPostTrigger))
        return(gtrStatusError);
    writeRegister(psisInfo,INTCONTROL,2);
    ecr = psisInfo->preAverageChoice << 16;
    ecr |= readRegister(psisInfo,READEVENTCONFIG) & 0x800;
    ecr |= psisInfo->indMultiEventNumber;
    switch(psisInfo->arm) {
    case armPostTrigger:
#if 0
        acr = acrTriggerMask[psisInfo->trigger] | 0xa1;
#else
        acr = acrTriggerMask[psisInfo->trigger] | 0x01;
#endif
        if(psisInfo->trigger == triggerFPGate) {
            /*
             * Turn off stop delay if number of post-trigger samples is zero.
             * This gets rid of the extra two samples that are taken otherwise.
             */
            switch(psisInfo->numberPTS) {
            case 0:
                acr &= ~0x80;
                break;

            case 1:  /* Have to live with taking an extra sample.... */
                writeRegister(psisInfo,STOPDELAY,0);
                break;

            default:
                writeRegister(psisInfo,STOPDELAY,psisInfo->numberPTS-2);
                break;
            }
            writeRegister(psisInfo,MAXEVENTS,psisInfo->numberPTE);
            ecr |= 0x10;
        }
        else {
            if(psisInfo->numberPTS<65536) {
                writeRegister(psisInfo,STOPDELAY,psisInfo->numberPTS);
                writeRegister(psisInfo,CSR,0x00000040); /* turn on trigger routing */
            }
            else {
                writeRegister(psisInfo,CSR,0x00400000); /* turn off trigger routing */
            }
        }
        writeRegister(psisInfo,EVENTCONFIG,ecr);
        writeRegister(psisInfo,ACQCSR,acr);
        break;
    case armPrePostTrigger:
        writeRegister(psisInfo,EVENTCONFIG,(ecr|0x8));
        acr = acrTriggerMask[psisInfo->trigger] | 0xb1;
        writeRegister(psisInfo,ACQCSR,acr);
        writeRegister(psisInfo,START,1);
        break;
    default:
        errlogPrintf("drvSisfadc::sisIH Illegal armType\n");
        return(gtrStatusError);
    }
    return(gtrStatusOK);
}

STATIC gtrStatus sissoftTrigger(gtrPvt pvt)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    writeRegister(psisInfo,START,1);
    return(gtrStatusOK);
}

STATIC gtrStatus sisreadMemory(gtrPvt pvt,gtrchannel **papgtrchannel)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    char *pbank;
    int indgroup;
    int numberPPS = psisInfo->numberPPS;

    pbank = psisInfo->a32 + MEMORYSTART;
    for(indgroup=0; indgroup<4; indgroup++) {
        gtrchannel *phigh;
        gtrchannel *plow;
        uint32 *pgroup;
        int nevents,eventsize,indevent;

        phigh = papgtrchannel[indgroup*2];
        plow = papgtrchannel[indgroup*2 + 1];
        phigh->ndata=0;
        plow->ndata=0;
        pgroup = (uint32 *)(pbank + indgroup*0x80000);
        nevents = multiEventNumber[psisInfo->indMultiEventNumber];
        if(psisInfo->trigger==triggerFPGate) nevents = 1;
        eventsize = ARRAYSIZE/nevents;
        if(numberPPS>eventsize) numberPPS = eventsize;
        if(numberPPS==0) numberPPS = eventsize;
        for(indevent=0; indevent<nevents; indevent++) {
            int nhigh,nlow,nmax,nskipHigh,nskipLow;
            uint32 *pevent;

            nhigh = phigh->len - phigh->ndata;
            if(nhigh>numberPPS) nhigh = numberPPS;
            nlow = plow->len - plow->ndata;
            if(nlow>numberPPS) nlow = numberPPS;
            nmax = (nhigh>nlow) ? nhigh : nlow;
            if(nmax<=0) break;
            pevent = pgroup + indevent*eventsize;
            switch(psisInfo->arm) {
            case armPostTrigger: {
                  int nnow;
                  if(psisInfo->trigger == triggerFPGate)
                      nnow = readRegister(psisInfo,BANK1ADDRESS);
                  else
                      nnow = readRegister(psisInfo,STOPDELAY);
                  nskipHigh = nskipLow = 0;
                  readContiguous(psisInfo,phigh,plow,
                      pevent,nnow,&nskipHigh,&nskipLow);
                }
                break;
            case armPrePostTrigger: {
                    volatile int *ptriggerInfo;
                    int endAddress;

                    ptriggerInfo = (volatile int *)
                        (psisInfo->a32 + 0x201000 + indevent*4);
                    endAddress =  *ptriggerInfo & 0x0000ffff;
                    nskipHigh = nmax - nhigh;
                    nskipLow = nmax - nlow;
                    if(endAddress < nmax) {
                        int nend,nbeg;
        
                        nend = nmax - endAddress;
                        nbeg = nmax - nend;
                        readContiguous(psisInfo,phigh,plow,
                            (pevent + eventsize - nend),nend,
                            &nskipHigh,&nskipLow);
                        readContiguous(psisInfo,phigh,plow,
                            pevent,nbeg,&nskipHigh,&nskipLow);
                    } else {
                        readContiguous(psisInfo,phigh,plow,
                            (pevent + endAddress - nmax),nmax,
                            &nskipHigh,&nskipLow);
                    }
                }
                break;
            default:
                return(gtrStatusError);
            }
        }
    }
    return(gtrStatusOK);
}

STATIC gtrStatus sisreadRawMemory(gtrPvt pvt,gtrchannel **papgtrchannel)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    char *pbank;
    int indgroup;
    int numberPPS = psisInfo->numberPPS;

    pbank = psisInfo->a32 + MEMORYSTART;
    for(indgroup=0; indgroup<4; indgroup++) {
        gtrchannel *pchan;
        uint32 *pgroup;
        int nevents,eventsize,indevent;

        pchan = papgtrchannel[indgroup];

        pchan->ndata=0;
        if(pchan->len==0) continue;  /* No waveform record */
        if(pchan->ftvl!=menuFtypeLONG) return(gtrStatusError);
        pgroup = (uint32 *)(pbank + indgroup*0x80000);
        nevents = multiEventNumber[psisInfo->indMultiEventNumber];
        if(psisInfo->trigger==triggerFPGate) {
            nevents = 1;
        }
        else {
            int eventcounter = readRegister(psisInfo,EVENTCOUNTER);
            if(eventcounter < nevents) {
                printf("sis3301ReadRawMemory: nevents:%d eventcounter:%d\n",nevents,eventcounter);
                return(gtrStatusError);
            }
        }
        eventsize = ARRAYSIZE/nevents;
        if(numberPPS>eventsize) numberPPS = eventsize;
        if(numberPPS==0) numberPPS = eventsize;
        for(indevent=0; indevent<nevents; indevent++) {
            int nchan;
            uint32 *pevent;

            nchan = pchan->len - pchan->ndata;
            if(nchan>numberPPS) nchan = numberPPS;
            if(nchan<=0) break;
            pevent = pgroup + indevent*eventsize;
            switch(psisInfo->arm) {
            case armPostTrigger: {
                int nnow;
                if(psisInfo->trigger == triggerFPGate) {
                     nnow = readRegister(psisInfo,BANK1ADDRESS);
                }
                else {
                    int eventInfo = readRegister(psisInfo,TRIGGEREVENTDIRECTORY+(indevent*4));
                    nnow = eventInfo % eventsize;
                    if((nnow == 0) && ((eventInfo & (1 << 19)) != 0))
                        nnow = eventsize;
                }
                if(nnow>nchan)
                    nnow = nchan;
#ifdef EMIT_TIMING_MARKERS
                if(indgroup==0) writeRegister(psisInfo,CSR,0x00000002);
#endif
                if(psisInfo->dmaId) {
                    if(epicsDmaFromVmeAndWait(psisInfo->dmaId,
                                   ((long *)pchan->pdata+pchan->ndata),
                                   (long)pevent,
                                   VME_AM_EXT_SUP_ASCENDING,
                                   nnow*sizeof(long),
                                   sizeof(long)) != 0) {
                        printf("Can't perform DMA: %s\n", strerror(errno));
                        return(gtrStatusError);
                    }
                }
                else {
                    bcopyLongs((char *)pevent,(char *)((long *)pchan->pdata+pchan->ndata),nnow);
                }
#ifdef EMIT_TIMING_MARKERS
                if(indgroup==0) writeRegister(psisInfo,CSR,0x00020000);
#endif
                pchan->ndata += nnow;
                }
                break;
            case armPrePostTrigger: 
            default:
                return(gtrStatusError);
            }
        }
    }
    return(gtrStatusOK);
}

STATIC gtrStatus sisgetLimits(gtrPvt pvt,int16 *rawLow,int16 *rawHigh)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    *rawLow = 0;
    *rawHigh = psisInfo->psisTypeInfo->dataMask;
    return(gtrStatusOK);
}

STATIC gtrStatus sisregisterHandler(gtrPvt pvt,
     gtrhandler usrIH,void *handlerPvt)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    
    psisInfo->usrIH = usrIH;
    psisInfo->handlerPvt = handlerPvt;
    return(gtrStatusOK);
}

STATIC int sisnumberChannels(gtrPvt pvt)
{
    return(8);
}

STATIC int sisnumberRawChannels(gtrPvt pvt)
{
    return(4);
}

STATIC gtrStatus sisclockChoices(gtrPvt pvt,int *number,char ***choice)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    sisTypeInfo *psisTypeInfo = psisInfo->psisTypeInfo;
    
    *number = psisTypeInfo->nclockChoices;
    *choice = psisTypeInfo->papclockChoices;
    return(gtrStatusOK);
}

STATIC gtrStatus sisarmChoices(gtrPvt pvt,int *number,char ***choice)
{
    *number = narmChoices;
    *choice = armChoices;
    return(gtrStatusOK);
}

STATIC gtrStatus sistriggerChoices(gtrPvt pvt,int *number,char ***choice)
{
    *number = ntriggerChoices;
    *choice = triggerChoices;
    return(gtrStatusOK);
}

STATIC gtrStatus sismultiEventChoices(gtrPvt pvt,int *number,char ***choice)
{
    *number = nmultiEventChoices;
    *choice = multiEventChoices;
    return(gtrStatusOK);
}

STATIC gtrStatus sispreAverageChoices(gtrPvt pvt,int *number,char ***choice)
{
    *number = npreAverageChoices;
    *choice = preAverageChoices;
    return(gtrStatusOK);
}

STATIC gtrStatus sisname(gtrPvt pvt,char *pname,int maxchars)
{
    sisInfo *psisInfo = (sisInfo *)pvt;
    strncpy(pname,psisInfo->name,maxchars);
    pname[maxchars-1] = 0;
    return(gtrStatusOK);
}

static gtrops sisfadcops = {
sisinit, 
sisreport, 
sisclock, 
sistrigger,
sismultiEvent,
sispreAverage,
sisnumberPTS,
sisnumberPPS,
sisnumberPTE,  /* For gate-chaining mode */
sisarm, 
sissoftTrigger, 
sisreadMemory,
sisreadRawMemory,
sisgetLimits,
sisregisterHandler,
sisnumberChannels,
sisnumberRawChannels,
sisclockChoices,
sisarmChoices,
sistriggerChoices,
sismultiEventChoices,
sispreAverageChoices,
sisname,
0, /*setUser*/
0, /*getUser*/
0, /*lock*/
0  /*unlock*/
};

int sisfadcConfig(int card,int clockSpeed,
    unsigned int a32offset,int intVec,int intLev, int useDma)
{
    sisType type;
    char name[80];
    char *a32;
    gtrops *pgtrops;
    uint32 probeValue = 0;
    sisInfo *psisInfo;
    long status;

    if(!sisIsInited) initialize();
    if(gtrFind(card,&pgtrops)) {
        printf("card is already configured\n");
        return(0);
    }
    if((a32offset & 0x00FFFFFF) != 0) {
        printf("sisfadcConfig: illegal a32offset (%#x). "
               "Must be multiple of 0x01000000\n", a32offset);
        return(0);
    }
    status = devRegisterAddress("sis330x",atVMEA32,a32offset,0x01000000,(void *)&a32);
    if(status) {
        errMessage(status,"sisfadcConfig: devRegisterAddress failed\n");
        return(0);
    }
    if(devReadProbe(4,a32+MODID,&probeValue)!=0) {
        printf("sisfadcConfig: no card at %#x (local address %p)\n",a32offset,(void *)a32);
printf("sisType probeValue %8.8x\n",probeValue);
while (devReadProbe(4,a32+MODID,&probeValue)!=0);
        return(0);
    }
    if((probeValue>>16) == 0x3300) {
        type = sisType3300;
        if(clockSpeed!=100)
            printf("WARNING: clockSpeed !=100 for sisType3300\n");
        strcpy(name,"sis3300");
    } else if((probeValue>>16) == 0x3301) {
        unsigned short modId,clockSpeedFromId;
        unsigned long serialNumber;
        int status;

        status = idromGetID(a32,&modId,&clockSpeedFromId,&serialNumber);
        switch(clockSpeed) {
        case 65: type = sisType3301_65; break;
        case 80: type = sisType3301_80; break;
        case 105: type = sisType3301_105; break;
        default:
            printf("Illegal clockSpeed %d for 3301\n",clockSpeed);
            return(0);
        }
        if(status) {
            sprintf(name,"sis3301-%d",clockSpeed);
        } else {
            if(clockSpeed!=clockSpeedFromId){
                printf("clockSpeed %d clockSpeedFromId %d\n",
                    clockSpeed,clockSpeedFromId);
            }
            sprintf(name,"sis3301-%hu SN %lu",clockSpeedFromId,serialNumber);
        }
            
    } else {
        printf("Illegal sisType probeValue %8.8x\n",probeValue); return(0);
    }
    psisInfo = calloc(1,sizeof(sisInfo));
    if(!psisInfo) {
        printf("sisfadcConfig: calloc failed\n");
        return(0);
    }
    psisInfo->card = card;
    psisInfo->psisTypeInfo = &pasisTypeInfo[type];
    psisInfo->name = calloc(1,strlen(name)+1);
    strcpy(psisInfo->name,name);
    psisInfo->a32 = a32;
    psisInfo->intVec = intVec;
    psisInfo->intLev = intLev;
    writeRegister(psisInfo,RESET,1);
    if(useDma) {
        psisInfo->dmaId = epicsDmaCreate(NULL, NULL);
        if(psisInfo->dmaId == NULL)
            printf("sisfadcConfig: DMA requested, but not available.\n");
    }
    else {
        psisInfo->dmaId = NULL;
    }
    ellAdd(&sisList,&psisInfo->node);
    gtrRegisterDriver(card,psisInfo->name,&sisfadcops,psisInfo);
    return(0);
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg sisfadcConfigArg0 = { "card",iocshArgInt};
static const iocshArg sisfadcConfigArg1 = { "clock speed",iocshArgInt};
static const iocshArg sisfadcConfigArg2 = { "base address",iocshArgInt};
static const iocshArg sisfadcConfigArg3 = { "interrupt vector",iocshArgInt};
static const iocshArg sisfadcConfigArg4 = { "interrupt level",iocshArgInt};
static const iocshArg sisfadcConfigArg5 = { "use DMA",iocshArgInt};
static const iocshArg *sisfadcConfigArgs[] = {
    &sisfadcConfigArg0, &sisfadcConfigArg1, &sisfadcConfigArg2,
    &sisfadcConfigArg3, &sisfadcConfigArg4, &sisfadcConfigArg5};
static const iocshFuncDef sisfadcConfigFuncDef =
                      {"sisfadcConfig",6,sisfadcConfigArgs};
static void sisfadcConfigCallFunc(const iocshArgBuf *args)
{
    sisfadcConfig(args[0].ival, args[1].ival, args[2].ival,
                 args[3].ival, args[4].ival, args[5].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvSISfadcRegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&sisfadcConfigFuncDef,sisfadcConfigCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvSISfadcRegisterCommands);
