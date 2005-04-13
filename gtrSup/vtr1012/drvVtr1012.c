/*drvVtr1012.c */

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

#include <epicsInterrupt.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <epicsExport.h>

#include "ellLib.h"
#include "errlog.h"
#include "devLib.h"
#include "drvSup.h"

#include "drvGtr.h"
#include "drvVtr1012.h"

#define STATIC static
/* memory map register offsets */
#define CSR1BYTE0 0x00
#define CSR1BYTE1 0x01
/* MLR - Word Location */
#define MBMLR     0x02
#define LBMLR     0x03
#define HBMLR     0x09
/* GDR - The number of conversions following a trigger*/
#define MBGDR     0x04
#define LBGDR     0x05
#define HBGDR     0x0B
#define IACKBYTE  0x0D
#define IACKLEV   0x0F
#define CSR2      0x11
#define IDREG     0x13

typedef unsigned char uint8;

static const char *vtrname = "vtr1012";

static int vtrMemorySize[8] =
{0x20000,0x40000,0x80000,0x100000,0x200000,0x400000,0x800000,0};

typedef enum {armDisable,armPostTrigger,armPrePostTrigger} armType;
typedef enum {triggerSoft,triggerExt,triggerGate} triggerType;

#define nChannels1012 4
typedef struct vtrInfo {
    ELLNODE node;
    int     card;
    char    *a16;
    int     a32offset;
    char    *a32;
    int     intVec;
    int     intLev;
    uint8   csr1byte0;  /*keep write state*/
    uint8   csr1byte1;  /*keep write state*/
    gtrhandler usrIH;
    armType arm;
    triggerType trigger;
    int     prePost;
    int     numberPTS;
    int     numberPPS;
    int     numberPTE;
    int     indPTE;
    void    *handlerPvt;
    void    *userPvt;
    int     arraySize;
    int16   *channel[nChannels1012];
} vtrInfo;

#define nclockChoices 16
static char *clockChoices[nclockChoices] = {
    "10 Mhz","5 Mhz","2.5 Mhz","1.25 Mhz",
    ".625 Mhz","312.5 Khz","156.25 Khz","78.125 Khz",
    "Ext","Ext/2","Ext/4","Ext/8",
    "Ext/16","Ext/32","Ext/64","Ext/128"
};

#define ntriggerChoices 3
static char *triggerChoices[ntriggerChoices] =
{ "soft","extTrigger","extGate"
};

#define narmChoices 3
static char *armChoices[narmChoices] = {
    "disarm","postTrigger","prePostTrigger"
};

static ELLLIST vtrList;
static int vtrIsInited = 0;
static int isRebooting;

#define isArmed(pvtrInfo) ((readRegister((pvtrInfo),CSR1BYTE0)&0x40) ? 1 : 0)

static void writeRegister(vtrInfo *pvtrInfo, int offset,uint8 value)
{
    char *a16 = pvtrInfo->a16;
    uint8 *reg;

    reg = (uint8 *)(a16+offset);
    *reg = value;
}

static uint8 readRegister(vtrInfo *pvtrInfo, int offset)
{
    char *a16 = pvtrInfo->a16;
    uint8 *reg;
    uint8 value;

    reg = (uint8 *)(a16+offset);
    value = *reg;
    return(value);
}

static void writeLocationRegister(vtrInfo *pvtrInfo,int value)
{
    writeRegister(pvtrInfo,MBMLR,(value>>8)&0xff);
    writeRegister(pvtrInfo,LBMLR,value&0xff);
    writeRegister(pvtrInfo,HBMLR,(value>>16)&0xff);
}

static int readLocationRegister(vtrInfo *pvtrInfo)
{
    int value;
    uint8 low,middle,high;
    middle = readRegister(pvtrInfo,MBMLR);
    low = readRegister(pvtrInfo,LBMLR);
    high = readRegister(pvtrInfo,HBMLR);
    value = low | (middle<<8) | (high<<16);
    return(value);
}

static void writeGateRegister(vtrInfo *pvtrInfo,int value)
{
    writeRegister(pvtrInfo,MBGDR,(value>>8)&0xff);
    writeRegister(pvtrInfo,LBGDR,value&0xff);
    writeRegister(pvtrInfo,HBGDR,(value>>16)&0xff);
}

static int readGateRegister(vtrInfo *pvtrInfo)
{
    int value;
    uint8 low,middle,high;
    middle = readRegister(pvtrInfo,MBGDR);
    low = readRegister(pvtrInfo,LBGDR);
    high = readRegister(pvtrInfo,HBGDR);
    value = low | (middle<<8) | (high<<16);
    return(value);
}

static gtrStatus vrtenablebyte0(gtrPvt pvt,uint8 mask,int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    
    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    if(isRebooting) epicsThreadSuspendSelf();
    if(value) value = mask;
    pvtrInfo->csr1byte0 = (pvtrInfo->csr1byte0 & (~mask)) | value;
    writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
    return(gtrStatusOK);
}

static void vtrReboot(void *arg)
{
    vtrInfo  *pvtrInfo;

    isRebooting = 1;
    pvtrInfo = (vtrInfo *)ellFirst(&vtrList);
    while(pvtrInfo) {
        writeRegister(pvtrInfo,CSR1BYTE0,0x00);
        pvtrInfo = (vtrInfo *)ellNext(&pvtrInfo->node);
    }
    vtrIsInited = 0;
}
    
static void vtrinitialize()
{
    if(vtrIsInited) return;
    vtrIsInited=1;
    isRebooting = 0;
    ellInit(&vtrList);
    epicsAtExit(vtrReboot,NULL);
}

void vtr1012IH(void *arg)
{
    vtrInfo *pvtrInfo = (vtrInfo *)arg;

    if(isRebooting) return;
    switch(pvtrInfo->arm) {
    case armDisable:
        break;
    case armPostTrigger:
        if(++(pvtrInfo->indPTE) < pvtrInfo->numberPTE) return;
        /*break left out on purpose*/
    case armPrePostTrigger:
        pvtrInfo->csr1byte0 = (pvtrInfo->csr1byte0 & (~0x70));
        writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
        if(pvtrInfo->usrIH) (*pvtrInfo->usrIH)(pvtrInfo->handlerPvt);
        break;
    default:
        epicsInterruptContextMessage("drvVtr1012::vtrIH Illegal armType\n");
        break;
    }
}

STATIC void vtrinit(gtrPvt pvt)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    long status;
    int arraySize;
    char *a32;
    uint8 id;
    int signal;
    
    pvtrInfo->csr1byte0 = 0x00;
    pvtrInfo->csr1byte1 = 0x00;
    writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
    writeRegister(pvtrInfo,CSR1BYTE1,pvtrInfo->csr1byte1);
    writeRegister(pvtrInfo,IACKBYTE,pvtrInfo->intVec);
    /*Start at beginning of array*/
    writeLocationRegister(pvtrInfo,0);
    /*Initialize to 1024 words. No good reason for this value*/
    writeGateRegister(pvtrInfo,1024);
    status = devEnableInterruptLevelVME(pvtrInfo->intLev);
    if(status) {
        errMessage(status,"vtrinit devEnableInterruptLevel failed\n");
    }
    id = readRegister(pvtrInfo,IDREG);
    id = (id>>3) & 0x07;
    arraySize = vtrMemorySize[id];
    arraySize = pvtrInfo->arraySize;
    status = devRegisterAddress(vtrname,atVMEA32,
        pvtrInfo->a32offset,arraySize*2*nChannels1012,(void *)&a32);
    if(status) {
        errMessage(status,"vtrinit1012 devRegisterAddress failed for a32\n");
        return;
    }
    pvtrInfo->a32 = a32;
    for(signal=0; signal<nChannels1012; signal++) {
        pvtrInfo->channel[signal] =
            (int16 *)(a32 + pvtrInfo->arraySize *signal * 2);
    }
    return;
}

STATIC void vtrreport(gtrPvt pvt,int level)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    uint8 value;
    int ivalue;

    printf("%s card %d a16 %p a32 %p intVec %2.2x intLev %d\n",
        vtrname,pvtrInfo->card,
        pvtrInfo->a16,pvtrInfo->a32,
        pvtrInfo->intVec,pvtrInfo->intLev);
    if(level<1) return;
    value = readRegister(pvtrInfo,CSR1BYTE0);
    printf("    CSR1BYTE0 %2.2x",value);
    value = readRegister(pvtrInfo,CSR1BYTE1);
    printf(" CSR1BYTE1 %hx",value);
    ivalue = readLocationRegister(pvtrInfo);
    printf(" MLR %x",ivalue);
    ivalue = readGateRegister(pvtrInfo);
    printf(" GDR %x",ivalue);
    value = readRegister(pvtrInfo,CSR2);
    printf(" CSR2 %hx",value);
    value = readRegister(pvtrInfo,IDREG);
    printf(" IDREG %hx",value);
    printf("\n");
}

STATIC gtrStatus vtrclock(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->csr1byte1 = (pvtrInfo->csr1byte1 & 0xf0) | (value&0x7);
    writeRegister(pvtrInfo,CSR1BYTE1,pvtrInfo->csr1byte1);
    vrtenablebyte0(pvt,0x01,value&0x8);
    return(gtrStatusOK);
}

STATIC gtrStatus vtrtrigger(gtrPvt pvt, int value)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(value<0 || value>triggerGate) return(gtrStatusError);
    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->trigger = value;
    pvtrInfo->csr1byte0 = pvtrInfo->csr1byte0 & (~0x06);
    switch(value) {
    case triggerExt:
        pvtrInfo->csr1byte0 |= 0x04; break;
    case triggerGate:
        pvtrInfo->csr1byte0 |= 0x02; break;
    default:
        break;
    }
    writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
    return(gtrStatusOK);
}

STATIC gtrStatus vtrnumberPTS(gtrPvt pvt, int nsamples)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    
    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->numberPTS = nsamples;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrnumberPPS(gtrPvt pvt, int nsamples)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    
    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->numberPPS = nsamples;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrnumberPTE(gtrPvt pvt, int nsamples)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    
    if(isArmed(pvtrInfo)) return(gtrStatusBusy);
    pvtrInfo->numberPTE = nsamples;
    return(gtrStatusOK);
}

STATIC gtrStatus vtrarm(gtrPvt pvt, int type)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    
    if(isRebooting) return(gtrStatusError);
    pvtrInfo->arm = type;
    switch(pvtrInfo->arm) {
    case armDisable:
        pvtrInfo->csr1byte0 = (pvtrInfo->csr1byte0 & (~0x70));
        writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
        break;
    case armPostTrigger:
        pvtrInfo->prePost = 0;
        pvtrInfo->indPTE = 0;
        writeLocationRegister(pvtrInfo,0);
        writeGateRegister(pvtrInfo,pvtrInfo->numberPTS);
        pvtrInfo->csr1byte0 = (pvtrInfo->csr1byte0&(~0x70)) | 0x40;
        writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
        break;
    case armPrePostTrigger:
        pvtrInfo->prePost = 1;
        writeLocationRegister(pvtrInfo,0);
        writeGateRegister(pvtrInfo,pvtrInfo->numberPTS);
        pvtrInfo->csr1byte0 = pvtrInfo->csr1byte0 & (~0x70);
        writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
        /* Must write to circular buffer enabled twice */
        pvtrInfo->csr1byte0 = pvtrInfo->csr1byte0 | 0x70;
        writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
        pvtrInfo->csr1byte0 = pvtrInfo->csr1byte0 | 0x70;
        writeRegister(pvtrInfo,CSR1BYTE0,pvtrInfo->csr1byte0);
        break;
    default:
        errlogPrintf("drvVtr1012::vtrarm Illegal armType\n");
        return(gtrStatusError);
    }
    return(gtrStatusOK);
}

STATIC gtrStatus vtrsoftTrigger(gtrPvt pvt)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;

    if(isRebooting) return(gtrStatusError);
    writeRegister(pvtrInfo,CSR1BYTE1,pvtrInfo->csr1byte1 | 0x80);
    return(gtrStatusOK);
}

static int getArrayLimits(
    int prePost,
    int n, int location,     /* n to fetch, index of next place to put data*/
    int16 *memLow, int memSize, 
    int16 **lowBeg,int16 **lowStop,int16 **highBeg,int16 **highStop
)
{
    int16 *memStop = memLow + memSize;

    if(n>memSize) n = memSize;
    if(!prePost) {
        if(location<n) n = location;
        *lowBeg = memLow + location - n;
        *lowStop = memLow + location;
        *highBeg = *highStop = 0;
        return(n);
    }
    if(location<n) {
        *lowBeg=memLow;
        *lowStop = memLow + location;
        *highBeg = memStop - n + location - 1;
        *highStop = memStop;
    } else {
        *lowBeg = memLow + location - n;
        *lowStop = memLow + location;
        *highBeg = *highStop = 0;
    }
    return(n);
}

STATIC gtrStatus vtrreadMemory(gtrPvt pvt,gtrchannel **papgtrchannel)
{
    vtrInfo *pvtrInfo = (vtrInfo *)pvt;
    gtrchannel *pgtrchannel;
    int16 *buffer;
    int len,ndata;
    int16 data;
    int16 *lowBeg,*lowStop,*highBeg,*highStop;
    int16 *pdata;
    int signal;
    int location;

    location = readLocationRegister(pvtrInfo);
    for(signal=0; signal<nChannels1012; signal++) {
        pgtrchannel = papgtrchannel[signal];
        len = pgtrchannel->len;
        if(pvtrInfo->prePost && len>pvtrInfo->numberPPS) len = pvtrInfo->numberPPS;
        buffer = pgtrchannel->pdata;
        if(len<=0 || !buffer) continue;
        ndata = getArrayLimits(
            pvtrInfo->prePost,len,location,
            pvtrInfo->channel[signal],pvtrInfo->arraySize,
            &lowBeg,&lowStop,&highBeg,&highStop);
        for(pdata=highBeg; pdata < highStop; pdata++) {
            data = *pdata;
            data = (data&0xfff);
            *buffer++ = data;
        }
        for(pdata=lowBeg; pdata < lowStop; pdata++) {
            data = *pdata;
            data = (data&0xfff);
            *buffer++ = data;
        }
        pgtrchannel->ndata = ndata;
    }
    return(gtrStatusOK);
}

STATIC gtrStatus vtrgetLimits(gtrPvt pvt,int16 *rawLow,int16 *rawHigh)
{
    *rawLow = 0;
    *rawHigh = 4096;
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
    return(4);
}

STATIC gtrStatus vtrclockChoices(gtrPvt pvt,int *number,char ***choice)
{
    *number = nclockChoices;
    *choice = clockChoices;
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
    
static gtrops vtr1012ops = {
vtrinit,
vtrreport,
vtrclock,
vtrtrigger,
0, /*no multiEvent */
0, /*no preAverage */
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
0, /*no multiEventChoices*/
0, /*no preAverageChoices*/
0,0,0,0,0
};

int vtr1012Config(int card,int a16offset,unsigned int a32offset,int intVec,
    int channelArraySize)
{
    char *a16;
    gtrops *pgtrops;
    uint8 probeValue = 0;
    vtrInfo *pvtrInfo;
    long status;

    if(!vtrIsInited) vtrinitialize();
    if(gtrFind(card,&pgtrops)) {
        printf("card is already configured\n"); return(0);
    }
    if((a16offset&0xff00)!=a16offset) {
        printf("vtrConfig: illegal a16offset. Must be multiple of 0x0100\n");
        return(0);
    }
    if((a32offset&0xFFE00000)!=a32offset) {
        printf("vtrConfig: illegal a32offset. Must be multiple of 0x00200000\n");
        return(0);
    }
    status = devRegisterAddress(vtrname,atVMEA16,
        a16offset,0x100,(void *)&a16);
    if(status) {
        errMessage(status,"vtrConfig devRegisterAddress failed\n");
        return(0);
    }
    if(devReadProbe(1,a16+CSR1BYTE0,(char *)&probeValue)!=0) {
        printf("vtrConfig: no card at %#x\n",a16offset);
        return(0);
    }
    pvtrInfo = calloc(1,sizeof(vtrInfo));
    if(!pvtrInfo) {
        printf("vtrConfig: calloc failed\n");
        return(0);
    }
    pvtrInfo->card = card;
    pvtrInfo->a16 = a16;
    pvtrInfo->a32offset = a32offset;
    pvtrInfo->intVec = intVec;
    pvtrInfo->numberPTE = 1;
    status = devConnectInterruptVME(pvtrInfo->intVec,
        vtr1012IH,(void *)pvtrInfo);
    if(status) {
        errMessage(status,"vtrConfig devConnectInterrupt failed\n");
        return(0);
    }
    pvtrInfo->intLev = (int)readRegister(pvtrInfo,IACKLEV);
    pvtrInfo->arraySize = channelArraySize;
    ellAdd(&vtrList,&pvtrInfo->node);
    gtrRegisterDriver(card,vtrname,&vtr1012ops,pvtrInfo);
    return(0);
}

/*
 * IOC shell command registration
 */
#include <iocsh.h>
static const iocshArg vtr1012ConfigArg0 = { "card",iocshArgInt};
static const iocshArg vtr1012ConfigArg1 = { "VME A16 offset",iocshArgInt};
static const iocshArg vtr1012ConfigArg2 = { "VME memory offset",iocshArgInt};
static const iocshArg vtr1012ConfigArg3 = { "interrupt vector",iocshArgInt};
static const iocshArg vtr1012ConfigArg4 = { "channel array size",iocshArgInt};
static const iocshArg *vtr1012ConfigArgs[] = {
    &vtr1012ConfigArg0, &vtr1012ConfigArg1, &vtr1012ConfigArg2,
    &vtr1012ConfigArg3, &vtr1012ConfigArg4};
static const iocshFuncDef vtr1012ConfigFuncDef =
                      {"vtr1012Config",5,vtr1012ConfigArgs};
static void vtr1012ConfigCallFunc(const iocshArgBuf *args)
{
    vtr1012Config(args[0].ival, args[1].ival, args[2].ival,
                 args[3].ival, args[4].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvVtr1012RegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&vtr1012ConfigFuncDef,vtr1012ConfigCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvVtr1012RegisterCommands);
