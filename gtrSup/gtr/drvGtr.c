/*drvGtr.c */

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

#include <epicsMutex.h>
#include <epicsAssert.h>
#include <epicsExport.h>
#include <ellLib.h>
#include <errlog.h>
#include <drvSup.h>

#include "drvGtr.h"

#define STATIC static

typedef struct gtrInfo {
    ELLNODE node;
    epicsMutexId  lock;
    int     card;
    const char *name;
    gtrops  *pgtrdrvops;
    gtrPvt  drvPvt;
    void    *userPvt;
} gtrInfo;

static ELLLIST gtrList;
static int gtrIsInited = 0;
    
static void gtrinitialize()
{
    if(gtrIsInited) return;
    gtrIsInited=1;
    ellInit(&gtrList);
}

STATIC void gtrinit(gtrPvt pvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;

    if(pgtrInfo->pgtrdrvops->init)
        (*pgtrInfo->pgtrdrvops->init)(pgtrInfo->drvPvt);
}

STATIC void gtrreport(gtrPvt pvt,int level)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;

    if(pgtrInfo->pgtrdrvops->report) {
        (*pgtrInfo->pgtrdrvops->report)(pgtrInfo->drvPvt,level);
    } else {
        printf("gtr card %d name %s\n", pgtrInfo->card,pgtrInfo->name);
    }
}

STATIC gtrStatus gtrclock(gtrPvt pvt, int value)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->clock) {
        return (*pgtrInfo->pgtrdrvops->clock)(pgtrInfo->drvPvt,value);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrtrigger(gtrPvt pvt, int value)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->trigger) {
        return (*pgtrInfo->pgtrdrvops->trigger)(pgtrInfo->drvPvt,value);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrmultiEvent(gtrPvt pvt, int value)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->multiEvent) {
        return (*pgtrInfo->pgtrdrvops->multiEvent)(pgtrInfo->drvPvt,value);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrpreAverage(gtrPvt pvt, int value)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->preAverage) {
        return (*pgtrInfo->pgtrdrvops->preAverage)(pgtrInfo->drvPvt,value);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrnumberPTS(gtrPvt pvt, int value)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->numberPTS) {
        return (*pgtrInfo->pgtrdrvops->numberPTS)(pgtrInfo->drvPvt,value);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrnumberPPS(gtrPvt pvt, int value)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->numberPPS) {
        return (*pgtrInfo->pgtrdrvops->numberPPS)(pgtrInfo->drvPvt,value);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrnumberPTE(gtrPvt pvt, int value)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->numberPTE) {
        return (*pgtrInfo->pgtrdrvops->numberPTE)(pgtrInfo->drvPvt,value);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrarm(gtrPvt pvt, int type)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->arm) {
        return (*pgtrInfo->pgtrdrvops->arm)(pgtrInfo->drvPvt,type);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrsoftTrigger(gtrPvt pvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->softTrigger) {
        return (*pgtrInfo->pgtrdrvops->softTrigger)(pgtrInfo->drvPvt);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrreadMemory(gtrPvt pvt, gtrchannel **papgtrchannel)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->readMemory) {
        return (*pgtrInfo->pgtrdrvops->readMemory)(pgtrInfo->drvPvt,papgtrchannel);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrreadRawMemory(gtrPvt pvt, gtrchannel **papgtrchannel)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->readRawMemory) {
        return (*pgtrInfo->pgtrdrvops->readRawMemory)(pgtrInfo->drvPvt,papgtrchannel);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrgetLimits(gtrPvt pvt, int16 *rawLow,int16 *rawHigh)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->getLimits) {
        return (*pgtrInfo->pgtrdrvops->getLimits)(
            pgtrInfo->drvPvt,rawLow,rawHigh);
    } else {
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrregisterHandler(gtrPvt pvt,gtrhandler usrIH,void *handlerPvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->registerHandler) {
        return (*pgtrInfo->pgtrdrvops->registerHandler)(
            pgtrInfo->drvPvt,usrIH,handlerPvt);
    } else {
        return(gtrStatusError);
    }
}

STATIC int gtrnumberChannels(gtrPvt pvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->numberChannels) {
        return (*pgtrInfo->pgtrdrvops->numberChannels)(pgtrInfo->drvPvt);
    } else {
        return(0);
    }
}

STATIC int gtrnumberRawChannels(gtrPvt pvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->numberRawChannels) {
        return (*pgtrInfo->pgtrdrvops->numberRawChannels)(pgtrInfo->drvPvt);
    } else {
        return(0);
    }
}

STATIC gtrStatus gtrclockChoices(gtrPvt pvt, int *number,char ***choice)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->clockChoices) {
        return (*pgtrInfo->pgtrdrvops->clockChoices)(
            pgtrInfo->drvPvt,number,choice);
    } else {
        *number = 0; *choice = 0;
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrarmChoices(gtrPvt pvt, int *number,char ***choice)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->armChoices) {
        return (*pgtrInfo->pgtrdrvops->armChoices)(
            pgtrInfo->drvPvt,number,choice);
    } else {
        *number = 0; *choice = 0;
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrtriggerChoices(gtrPvt pvt, int *number,char ***choice)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->triggerChoices) {
        return (*pgtrInfo->pgtrdrvops->triggerChoices)(
            pgtrInfo->drvPvt,number,choice);
    } else {
        *number = 0; *choice = 0;
        return(gtrStatusError);
    }
}

STATIC gtrStatus gtrmultiEventChoices(gtrPvt pvt, int *number,char ***choice)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->multiEventChoices) {
        return (*pgtrInfo->pgtrdrvops->multiEventChoices)(
            pgtrInfo->drvPvt,number,choice);
    } else {
        *number = 0; *choice = 0;
        /*Note devGtr will supply default choices */
        return(gtrStatusOK);
    }
}

STATIC gtrStatus gtrpreAverageChoices(gtrPvt pvt, int *number,char ***choice)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->preAverageChoices) {
        return (*pgtrInfo->pgtrdrvops->preAverageChoices)(
            pgtrInfo->drvPvt,number,choice);
    } else {
        *number = 0; *choice = 0;
        /*Note devGtr will supply default choices */
        return(gtrStatusOK);
    }
}

STATIC gtrStatus gtrname(gtrPvt pvt,char *pname,int maxchars)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    
    if(pgtrInfo->pgtrdrvops->name) {
        return (*pgtrInfo->pgtrdrvops->name)(
            pgtrInfo->drvPvt,pname,maxchars);
    } else {
        strncpy(pname,pgtrInfo->name,maxchars);
        pname[maxchars-1] = 0;
        return(gtrStatusError);
    }
}

STATIC void gtrsetUser(gtrPvt pvt,void * userPvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    pgtrInfo->userPvt = userPvt;
}

STATIC void *gtrgetUser(gtrPvt pvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    return(pgtrInfo->userPvt);
}

STATIC void gtrlock(gtrPvt pvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    epicsMutexLock(pgtrInfo->lock);
}

STATIC void gtrunlock(gtrPvt pvt)
{
    gtrInfo *pgtrInfo = (gtrInfo *)pvt;
    epicsMutexUnlock(pgtrInfo->lock);
}

static gtrops ops = {
gtrinit,
gtrreport,
gtrclock,
gtrtrigger,
gtrmultiEvent,
gtrpreAverage,
gtrnumberPTS,
gtrnumberPPS,
gtrnumberPTE,
gtrarm,
gtrsoftTrigger,
gtrreadMemory,
gtrreadRawMemory,
gtrgetLimits,
gtrregisterHandler,
gtrnumberChannels,
gtrnumberRawChannels,
gtrclockChoices,
gtrarmChoices,
gtrtriggerChoices,
gtrmultiEventChoices,
gtrpreAverageChoices,
gtrname,
gtrsetUser,
gtrgetUser,
gtrlock,
gtrunlock
};

gtrPvt gtrFind(int card,gtrops **ppgtrops)
{
    gtrInfo  *pgtrInfo;

    if(!gtrIsInited) gtrinitialize();
    pgtrInfo = (gtrInfo *)ellFirst(&gtrList);
    while(pgtrInfo) {
        if(pgtrInfo->card == card) break;
        pgtrInfo = (gtrInfo *)ellNext(&pgtrInfo->node);
    }
    *ppgtrops = &ops;
    return(pgtrInfo);
}

void gtrRegisterDriver(int card,
    const char *name,gtrops *pgtrdrvops,gtrPvt drvPvt)
{
    gtrInfo *pgtrInfo;
    gtrops *pgtrops;

    if(!gtrIsInited) gtrinitialize();
    if(gtrFind(card,&pgtrops)) {
        printf("gtrRegisterDriver card is already registered\n");
        return;
    }
    pgtrInfo = calloc(1,sizeof(gtrInfo));
    if(!pgtrInfo) {
        printf("gtrConfig: calloc failed\n");
        return;
    }
    pgtrInfo->lock = epicsMutexCreate();
    if(!pgtrInfo->lock) {
        printf("gtrConfig: semMCreate failed\n");
        return;
    }
    pgtrInfo->card = card;
    pgtrInfo->name = name;
    pgtrInfo->pgtrdrvops = pgtrdrvops;
    pgtrInfo->drvPvt = drvPvt;
    pgtrInfo->userPvt = 0;
    ellAdd(&gtrList,&pgtrInfo->node);
}

STATIC long drvGtrReport(int level)
{
    gtrInfo  *pgtrInfo;
    pgtrInfo = (gtrInfo *)ellFirst(&gtrList);
    while(pgtrInfo) {
        gtrreport(pgtrInfo,level);
        pgtrInfo = (gtrInfo *)ellNext(&pgtrInfo->node);
    }
    return(0);
}

STATIC long drvGtrInit()
{
    gtrInfo  *pgtrInfo;

    if(!gtrIsInited) gtrinitialize();
    pgtrInfo = (gtrInfo *)ellFirst(&gtrList);
    while(pgtrInfo) {
        gtrinit(pgtrInfo);
        pgtrInfo = (gtrInfo *)ellNext(&pgtrInfo->node);
    }
    return(0);
}

struct drvet drvGtr = {
2,
drvGtrReport,
drvGtrInit
};
epicsExportAddress(drvet,drvGtr);
