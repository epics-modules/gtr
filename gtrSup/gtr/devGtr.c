/*devGtr.c */

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
#include <limits.h>

#include <epicsExport.h>
#include <errlog.h>
#include <dbStaticLib.h>
#include <callback.h>
#include <alarm.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <recGbl.h>
#include <recSup.h>
#include <devSup.h>
#include <dbCommon.h>
#include <boRecord.h>
#include <longoutRecord.h>
#include <mbboRecord.h>
#include <stringinRecord.h>
#include <waveformRecord.h>
#include <menuFtype.h>
#include <devLib.h>

#include "drvGtr.h"

typedef struct devGtrChannels {
    int nchannels;
    gtrchannel *pachannel;
    gtrchannel **papgtrchannel;
    int hasWaveforms;
} devGtrChannels;

typedef struct devGtr {
    CALLBACK callback;
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    IOSCANPVT   ioscanpvt;
    int arm;
    devGtrChannels channels;
    devGtrChannels rawChannels;
} devGtr;

typedef struct dpvt{
    int      parm;
    devGtr *pdevGtr;
    /* The following are only used by waveform record */
    int      signal; /*only used by waveform*/
    int      isPdataBptr;
}dpvt;

#define NBOPARM 2
typedef enum {
    autoRestart,softTrigger
}boParm;
static char *boParmString[NBOPARM] =
{
    "autoRestart","softTrigger"
};

#define NLOPARM 3
typedef enum {
    numberPTS,numberPPS,numberPTE
}longoutParm;
static char *longoutParmString[NLOPARM] =
{
    "numberPTS","numberPPS","numberPTE"
};

#define NMBBOPARM 5
typedef enum {
    arm,clockp,trigger,multiEvent,preAverage
}mbboParm;
static char *mbboParmString[NMBBOPARM] =
{
    "arm","clock","trigger","multiEvent","preAverage"
};

#define NSIPARM 3
typedef enum {
    name
}stringinParm;
static char *stringinParmString[NSIPARM] =
{
    "name"
};

#define NWFPARM 2
typedef enum {
    readData,readRawData
}waveformParm;
static char *waveformParmString[NWFPARM] =
{
    "readData","readRawData"
};

static long get_ioint_info(int cmd, dbCommon *precord, IOSCANPVT *pvt);
typedef struct bodset {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN write;
} bodset;
static long bo_init_record(dbCommon *precord);
static long bo_write(dbCommon *precord);
bodset devGtrBO = {5,0,0,bo_init_record,get_ioint_info,bo_write};
epicsExportAddress(dset,devGtrBO);

typedef struct longoutdset {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN write;
} longoutdset;
static long longout_init_record(dbCommon *precord);
static long longout_write(dbCommon *precord);
longoutdset devGtrLO = {5,0,0,longout_init_record,get_ioint_info,longout_write};
epicsExportAddress(dset,devGtrLO);

typedef struct mbbodset {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN write;
} mbbodset;
static long mbbo_init_record(dbCommon *precord);
static long mbbo_write(dbCommon *precord);
mbbodset devGtrMBBO   = {5,0,0,mbbo_init_record,get_ioint_info,mbbo_write};
epicsExportAddress(dset,devGtrMBBO);

typedef struct stringindset {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read;
} stringindset;
static long stringin_init_record(dbCommon *precord);
static long stringin_read(dbCommon *precord);
stringindset devGtrSI
    = {5,0,0,stringin_init_record,get_ioint_info,stringin_read};
epicsExportAddress(dset,devGtrSI);

typedef struct waveformdset {
    long      number;
    DEVSUPFUN report;
    DEVSUPFUN init;
    DEVSUPFUN init_record;
    DEVSUPFUN get_ioint_info;
    DEVSUPFUN read;
} waveformdset;
static long waveform_init_record(dbCommon *precord);
static long waveform_read(dbCommon *precord);
waveformdset devGtrWF =
    {5,0,0,waveform_init_record,get_ioint_info,waveform_read};
epicsExportAddress(dset,devGtrWF);

static void myCallback(CALLBACK *pcallback)
{
    devGtr *pdevGtr = 0;
    gtrops *pgtrops;
    gtrStatus status;

    callbackGetUser(pdevGtr,pcallback);
    pgtrops = pdevGtr->pgtrops;
    if(pdevGtr->channels.hasWaveforms) {
        status = (*pgtrops->readMemory)(pdevGtr->gtrpvt,pdevGtr->channels.papgtrchannel);
        if(status!=gtrStatusOK)
            printf("devGtr: myCallback read failed\n");
    }
    if(pdevGtr->rawChannels.hasWaveforms) {
        status = (*pgtrops->readRawMemory)(pdevGtr->gtrpvt,pdevGtr->rawChannels.papgtrchannel);
        if(status!=gtrStatusOK)
            printf("devGtr: myCallback raw read failed\n");
    }
    scanIoRequest(pdevGtr->ioscanpvt);
}

static void interruptHandler(void *pvt)
{
    devGtr *pdevGtr = (devGtr *)pvt;

    callbackRequest(&pdevGtr->callback);
}

static long get_ioint_info(int cmd, dbCommon *precord, IOSCANPVT *pvt)
{
    dpvt *pdpvt;
    devGtr *pdevGtr;

    pdpvt = precord->dpvt;
    if(!pdpvt) return(-1);
    pdevGtr = pdpvt->pdevGtr;
    *pvt = pdevGtr->ioscanpvt;
    return(0);
}

static void
allocateChannels(devGtrChannels *pdevgtrchannels, int nchannels)
{
    int ind;

    pdevgtrchannels->nchannels = nchannels;
    if(pdevgtrchannels->nchannels != 0) {
        pdevgtrchannels->pachannel = calloc(pdevgtrchannels->nchannels,sizeof(gtrchannel));
        pdevgtrchannels->papgtrchannel = calloc(pdevgtrchannels->nchannels,sizeof(gtrchannel *));
        for(ind=0;ind<pdevgtrchannels->nchannels; ind++)
            pdevgtrchannels->papgtrchannel[ind] = &pdevgtrchannels->pachannel[ind];
    }
}

static dpvt *common_init_record(dbCommon *precord,DBLINK *plink,
    char **parmString,int nparmStrings)
{
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    struct vmeio *pvmeio;
    devGtr *pdevGtr;
    dpvt   *pdpvt;
    int ind;
    char *parm = 0;

    if(plink->type!=VME_IO) {
        recGblRecordError(S_db_badField,(void *)precord, "devGtr NOT VME_IO");
        goto bad;
    }
    pvmeio = &(plink->value.vmeio);
    gtrpvt = gtrFind(pvmeio->card,&pgtrops);
    if(!gtrpvt) {
        recGblRecordError(S_db_badField,(void *)precord, "devGtr no card");
        goto bad;
    }
    (*pgtrops->lock)(gtrpvt);
    pdevGtr = (*pgtrops->getUser)(gtrpvt);
    if(!pdevGtr) {
        pdevGtr = dbCalloc(1,sizeof(devGtr));
        pdevGtr->gtrpvt = gtrpvt;
        pdevGtr->pgtrops = pgtrops;
        allocateChannels(&pdevGtr->channels, (*pgtrops->numberChannels)(gtrpvt));
        allocateChannels(&pdevGtr->rawChannels, (*pgtrops->numberRawChannels)(gtrpvt));
        (*pgtrops->setUser)(gtrpvt,pdevGtr);
        callbackSetCallback(myCallback,&pdevGtr->callback);
        callbackSetUser(pdevGtr,&pdevGtr->callback);
        callbackSetPriority(priorityLow,&pdevGtr->callback);
        (*pgtrops->registerHandler)(gtrpvt,interruptHandler,pdevGtr);
        scanIoInit(&pdevGtr->ioscanpvt);
        (*pgtrops->setUser)(gtrpvt,pdevGtr);
    }
    (*pgtrops->unlock)(gtrpvt);
    if(pvmeio->parm) parm = pvmeio->parm;
    if(!pvmeio->parm || !parm) {
        recGblRecordError(S_db_badField,(void *)precord, "devGtr no parm");
        goto bad;
    }
    if(strcmp(parm,"setDefault")==0) {
        ind = 0;
    }
    else {
        for(ind=0; ind<nparmStrings; ind++) {
            if(strcmp(parm,parmString[ind])==0) break;
        }
    }
    if(ind>=nparmStrings) {
        recGblRecordError(S_db_badField,(void *)precord, "devGtr bad parm");
        goto bad;
    }
    pdpvt = dbCalloc(1,sizeof(dpvt));
    pdpvt->pdevGtr = pdevGtr;
    pdpvt->parm = ind;
    precord->dpvt = pdpvt;
    return(pdpvt);
bad:
    precord->pact = 1;
    return(0);
}

static long bo_init_record(dbCommon *precord)
{
    boRecord *pboRecord = (boRecord *)precord;

    common_init_record(precord,&pboRecord->out,boParmString,NBOPARM);
    return(2);
}

static long bo_write(dbCommon *precord)
{
    boRecord *pboRecord = (boRecord *)precord;
    dpvt *pdpvt;
    devGtr *pdevGtr;
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    long status = 0;

    pdpvt = pboRecord->dpvt;
    if(!pdpvt) {
        status = S_dev_NoInit;
        recGblRecordError(status,(void *)pboRecord,
            "devGtr init_record failed");
        pboRecord->pact = 1;
        return(status);
    }
    pdevGtr = pdpvt->pdevGtr;
    gtrpvt = pdevGtr->gtrpvt;
    pgtrops = pdevGtr->pgtrops;
    (*pgtrops->lock)(gtrpvt);
    switch(pdpvt->parm) {
        case autoRestart:
            if(pboRecord->val==0) break;
            status = (*pgtrops->arm)(gtrpvt,pdevGtr->arm);
            break;
        case softTrigger:
            status = (*pgtrops->softTrigger)(gtrpvt);
            break;
        default:
            errlogPrintf("%s logic error\n",precord->name);
    }
    (*pgtrops->unlock)(gtrpvt);
    if(status!=gtrStatusOK) recGblSetSevr(pboRecord,STATE_ALARM,MINOR_ALARM);
    return(0);
}

static long longout_init_record(dbCommon *precord)
{
    longoutRecord *plongoutRecord = (longoutRecord *)precord;

    common_init_record(precord,&plongoutRecord->out,longoutParmString,NLOPARM);
    return(0);
}

static long longout_write(dbCommon *precord)
{
    longoutRecord *plongoutRecord = (longoutRecord *)precord;
    dpvt *pdpvt;
    devGtr *pdevGtr;
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    long status = 0;

    pdpvt = plongoutRecord->dpvt;
    if(!pdpvt) {
        status = S_dev_NoInit;
        recGblRecordError(status,(void *)plongoutRecord,
            "devGtr init_record failed");
        plongoutRecord->pact = 1;
        return(status);
    }
    pdevGtr = pdpvt->pdevGtr;
    gtrpvt = pdevGtr->gtrpvt;
    pgtrops = pdevGtr->pgtrops;
    (*pgtrops->lock)(gtrpvt);
    switch(pdpvt->parm) {
        case numberPTS:
            status = (*pgtrops->numberPTS)(gtrpvt,plongoutRecord->val);
            break;
        case numberPPS:
            status = (*pgtrops->numberPPS)(gtrpvt,plongoutRecord->val);
            break;
        case numberPTE:
            status = (*pgtrops->numberPTE)(gtrpvt,plongoutRecord->val);
            break;
        default:
            errlogPrintf("%s logic error\n",precord->name);
    }
    (*pgtrops->unlock)(gtrpvt);
    if(status!=gtrStatusOK) recGblSetSevr(plongoutRecord,STATE_ALARM,MINOR_ALARM);
    return(0);
}

#define ndefaultChoices 2
static char *defaultChoices[ndefaultChoices] = {
    "notSupported","notSupported"
};

static long mbbo_init_record(dbCommon *precord)
{
    mbboRecord *pmbboRecord = (mbboRecord *)precord;
    dpvt *pdpvt;
    devGtr *pdevGtr;
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    long status;
    char **choice;
    int nchoices,indchoice;
    char *pstate_string;

    common_init_record(precord,&pmbboRecord->out,mbboParmString,NMBBOPARM);
    pdpvt = pmbboRecord->dpvt;
    if(!pdpvt) return(2);
    pdevGtr = pdpvt->pdevGtr;
    gtrpvt = pdevGtr->gtrpvt;
    pgtrops = pdevGtr->pgtrops;
    switch(pdpvt->parm) {
        case clockp:
            status = (*pgtrops->clockChoices)(gtrpvt,&nchoices,&choice);
            break;
        case arm:
            status = (*pgtrops->armChoices)(gtrpvt,&nchoices,&choice);
            break;
        case trigger:
            status = (*pgtrops->triggerChoices)(gtrpvt,&nchoices,&choice);
            break;
        case multiEvent:
            status = (*pgtrops->multiEventChoices)(gtrpvt,&nchoices,&choice);
            break;
        case preAverage:
            status = (*pgtrops->preAverageChoices)(gtrpvt,&nchoices,&choice);
            break;
        default:
            return(2);
    }
    if(status!=gtrStatusOK || nchoices>16) {
        recGblSetSevr(pmbboRecord,STATE_ALARM,MINOR_ALARM);
        pmbboRecord->pact=1;
        return(2);
    }
    pstate_string = pmbboRecord->zrst;
    if(nchoices<=1) {
        nchoices = ndefaultChoices;
        choice = defaultChoices;
    }
    for(indchoice=0; indchoice<nchoices; indchoice++) {
        strncpy(pstate_string,choice[indchoice],sizeof(pmbboRecord->zrst));
        pstate_string += sizeof(pmbboRecord->zrst);
    }
    return(2);
}

static long mbbo_write(dbCommon *precord)
{
    mbboRecord *pmbboRecord = (mbboRecord *)precord;
    dpvt *pdpvt;
    devGtr *pdevGtr;
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    long status = 0;

    pdpvt = pmbboRecord->dpvt;
    if(!pdpvt) {
        status = S_dev_NoInit;
        recGblRecordError(status,(void *)pmbboRecord,
            "devGtr init_record failed");
        pmbboRecord->pact = 1;
        return(status);
    }
    pdevGtr = pdpvt->pdevGtr;
    gtrpvt = pdevGtr->gtrpvt;
    pgtrops = pdevGtr->pgtrops;
    (*pgtrops->lock)(gtrpvt);
    switch(pdpvt->parm) {
        case clockp:
            status = (*pgtrops->clock)(gtrpvt,pmbboRecord->val);
            break;
        case arm:
            pdevGtr->arm = pmbboRecord->val;
            status = (*pgtrops->arm)(gtrpvt,pmbboRecord->val);
            break;
        case trigger:
            status = (*pgtrops->trigger)(gtrpvt,pmbboRecord->val);
            break;
        case multiEvent:
            status = (*pgtrops->multiEvent)(gtrpvt,pmbboRecord->val);
            break;
        case preAverage:
            status = (*pgtrops->preAverage)(gtrpvt,pmbboRecord->val);
            break;
        default:
            recGblSetSevr(pmbboRecord,STATE_ALARM,MAJOR_ALARM);
    }
    (*pgtrops->unlock)(gtrpvt);
    if(status!=gtrStatusOK) recGblSetSevr(pmbboRecord,STATE_ALARM,MINOR_ALARM);
    return(0);
}

static long stringin_init_record(dbCommon *precord)
{
    stringinRecord *pstringinRecord = (stringinRecord *)precord;
    dpvt *pdpvt;
    devGtr *pdevGtr;
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    long status;

    common_init_record(precord,&pstringinRecord->inp,stringinParmString,NSIPARM);
    pdpvt = pstringinRecord->dpvt;
    if(!pdpvt) return(2);
    pdevGtr = pdpvt->pdevGtr;
    gtrpvt = pdevGtr->gtrpvt;
    pgtrops = pdevGtr->pgtrops;
    status = (*pgtrops->name)(gtrpvt,
        pstringinRecord->val,sizeof(pstringinRecord->val));
    if(status==gtrStatusOK) pstringinRecord->udf = 0;
    return(0);
}

static long stringin_read(dbCommon *precord)
{ return(0);}

static long waveform_init_record(dbCommon *precord)
{
    waveformRecord *pwaveformRecord = (waveformRecord *)precord;
    devGtr *pdevGtr;
    dpvt *pdpvt;
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    long status = 0;
    struct vmeio *pvmeio;
    gtrchannel *pgtrchannel;
    devGtrChannels *pdevgtrchannels;
    int signal;
    int ftvl = pwaveformRecord->ftvl;

    pdpvt = common_init_record(precord,&pwaveformRecord->inp,
        waveformParmString,NWFPARM);
    if(!pdpvt) return(0);
    pdevGtr = pdpvt->pdevGtr;
    if(!pdevGtr) return(0);
    gtrpvt = pdevGtr->gtrpvt;
    pgtrops = pdevGtr->pgtrops;
    switch(pdpvt->parm) {
    case readData:     pdevgtrchannels=&pdevGtr->channels;     break;
    case readRawData:  pdevgtrchannels=&pdevGtr->rawChannels;  break;
    default:           return(S_db_badField);
    }
    switch(ftvl) {
    default:
        status = S_db_badField;
        recGblRecordError(status,(void *)precord,
            "FTVL must be SHORT FLOAT or DOUBLE");
        pwaveformRecord->pact = 1;
        return(status);
    
    case menuFtypeLONG: /* Limits must come from database */
            break;

    case menuFtypeSHORT: {
        short rawLow,rawHigh;
        (*pgtrops->getLimits)(gtrpvt,&rawLow,&rawHigh);
        pwaveformRecord->hopr = rawHigh;
        pwaveformRecord->lopr = rawLow;
        }
        break;

    case menuFtypeFLOAT:
    case menuFtypeDOUBLE:
        pwaveformRecord->hopr = 1.0;
        pwaveformRecord->lopr = 0.0;
        break;
    }
    pvmeio = &(pwaveformRecord->inp.value.vmeio);
    signal = pvmeio->signal;
    if(signal<0 || signal>=pdevgtrchannels->nchannels) {
        status = S_db_badField;
        signal = 0;
        recGblRecordError(status,(void *)precord,
            "devGtr Illegal signal");
        pwaveformRecord->pact = 1;
        return(status);
    }
    pdpvt->signal = pvmeio->signal;
    pgtrchannel = &pdevgtrchannels->pachannel[pdpvt->signal];
    if(((ftvl==menuFtypeSHORT)||(ftvl==menuFtypeLONG)) && !pgtrchannel->pdata) {
        pgtrchannel->pdata = pwaveformRecord->bptr;
        pgtrchannel->len = pwaveformRecord->nelm;
        pgtrchannel->ftvl = ftvl;
        pdpvt->isPdataBptr = 1;
    } else if(!pgtrchannel->pdata || pgtrchannel->len<pwaveformRecord->nelm) {
        if(pgtrchannel->pdata && !pdpvt->isPdataBptr) free(pgtrchannel->pdata);
        pdpvt->isPdataBptr = 0;
        pgtrchannel->pdata = dbCalloc(pwaveformRecord->nelm, sizeof(int16));
        pgtrchannel->len = pwaveformRecord->nelm;
        pgtrchannel->ftvl = menuFtypeSHORT;
    }
    precord->dpvt = pdpvt;
    pdevgtrchannels->hasWaveforms=1;
    return(0);
}

static long waveform_read(dbCommon *precord)
{
    waveformRecord *pwaveformRecord = (waveformRecord *)precord;
    dpvt *pdpvt;
    devGtr *pdevGtr;
    gtrPvt gtrpvt;
    gtrops *pgtrops;
    long status;
    int ndata;
    gtrchannel *pgtrchannel;
    devGtrChannels *pdevgtrchannels;

    pdpvt = pwaveformRecord->dpvt;
    if(!pdpvt) {
        status = S_dev_NoInit;
        recGblRecordError(status,(void *)precord,
            "devGtr init_record failed");
        precord->pact = 1;
        return(status);
    }
    pdevGtr = pdpvt->pdevGtr;
    gtrpvt = pdevGtr->gtrpvt;
    pgtrops = pdevGtr->pgtrops;
    switch(pdpvt->parm) {
    case readData:     pdevgtrchannels=&pdevGtr->channels;     break;
    case readRawData:  pdevgtrchannels=&pdevGtr->rawChannels;  break;
    default:           return(S_db_badField);
    }
    pgtrchannel = &pdevgtrchannels->pachannel[pdpvt->signal];
    ndata = pgtrchannel->ndata;
    if(ndata>pwaveformRecord->nelm) ndata = pwaveformRecord->nelm;
    if(ndata>0 ) {
        pwaveformRecord->nord = ndata;
    } else {
        recGblSetSevr(precord,STATE_ALARM,MINOR_ALARM);
        return(0);
    }
    if(pwaveformRecord->bptr==pgtrchannel->pdata) return(0);
    if(pwaveformRecord->ftvl == menuFtypeSHORT) {
        memcpy(pwaveformRecord->bptr,pgtrchannel->pdata,ndata*sizeof(int16));
    } else if(pwaveformRecord->ftvl == menuFtypeLONG) {
        memcpy(pwaveformRecord->bptr,pgtrchannel->pdata,ndata*sizeof(long));
    } else {
        int16 rawLow,rawHigh;
        int16 *pfrom = pgtrchannel->pdata;
        int ind;
        (*pgtrops->getLimits)(gtrpvt,&rawLow,&rawHigh);
        if(pwaveformRecord->ftvl==menuFtypeFLOAT) {
            float *pto = (float *)pwaveformRecord->bptr;
            float low,high,diff;
            low = (float)rawLow; high = (float)rawHigh; diff = high - low;
            for(ind=0; ind<ndata; ind++)
                *pto++ = ((float)(*pfrom++) -low)/diff;
        } else if(pwaveformRecord->ftvl==menuFtypeDOUBLE) {
            double *pto = (double *)pwaveformRecord->bptr;
                double low,high,diff;
                low = (double)rawLow; high = (double)rawHigh; diff = high - low;
                for(ind=0; ind<ndata; ind++)
                    *pto++ = ((double)(*pfrom++) -low)/diff;
        } else {
            recGblRecordError(S_db_badField,(void *)precord,
                "devGtr FTVL must be SHORT or FLOAT or DOUBLE");
            pwaveformRecord->pact = 1;
        }
    }
    return(0);
}
