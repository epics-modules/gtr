/*drvGtr.h */

/* Author:   Marty Kraimer */
/* Date:     17SEP2001     */

/*************************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of California, as
* Operator of Los Alamos National Laboratory. EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*************************************************************************/

#ifndef drvGtrH
#define drvGtrH

#ifdef __cplusplus
extern "C" {
#endif

typedef void *gtrPvt;
typedef void (*gtrhandler)(void *pvt);
typedef short int16;
typedef enum {gtrStatusOK=0,gtrStatusBusy=-1,gtrStatusError=-2} gtrStatus;

typedef struct gtrchannel {
    int len; /*size of pdata array*/
    int ndata; /*number of elements readMemory put into array*/
    int16 *pdata;
    int ftvl;
}gtrchannel;

typedef struct gtrops {
    void      (*init)(gtrPvt pvt);
    void      (*report)(gtrPvt pvt,int level);
    gtrStatus (*clock)(gtrPvt pvt, int value);
    gtrStatus (*trigger)(gtrPvt pvt, int value);
    gtrStatus (*multiEvent)(gtrPvt pvt, int value);
    gtrStatus (*preAverage)(gtrPvt pvt, int value);
    /*PTS PostTriggerSamples*/
    gtrStatus (*numberPTS)(gtrPvt pvt, int value);
    /*PPS PrePost Samples*/
    gtrStatus (*numberPPS)(gtrPvt pvt, int value);
    /*PTE PostTrigger Events */
    gtrStatus (*numberPTE)(gtrPvt pvt, int value);
    gtrStatus (*arm)(gtrPvt pvt, int type);
    gtrStatus (*softTrigger)(gtrPvt pvt);
    /*papgtrchannel is nchannels array of pointers to gtrchannel*/
    gtrStatus (*readMemory)(gtrPvt pvt, gtrchannel **papgtrchannel);
    gtrStatus (*readRawMemory)(gtrPvt pvt, gtrchannel **papgtrchannel /* MARTY: Should the waveform record data type be passed here? */);
    gtrStatus (*getLimits)(gtrPvt pvt, int16 *rawLow,int16 *rawHigh);
    gtrStatus (*registerHandler)(gtrPvt pvt,gtrhandler usrIH,void *handlerPvt);
    int       (*numberChannels)(gtrPvt pvt);
    int       (*numberRawChannels)(gtrPvt pvt);
    gtrStatus (*clockChoices)(gtrPvt pvt,int *number,char ***choice);
    gtrStatus (*armChoices)(gtrPvt pvt,int *number,char ***choice);
    gtrStatus (*triggerChoices)(gtrPvt pvt,int *number,char ***choice);
    gtrStatus (*multiEventChoices)(gtrPvt pvt,int *number,char ***choice);
    gtrStatus (*preAverageChoices)(gtrPvt pvt,int *number,char ***choice);
    gtrStatus (*name)(gtrPvt pvt,char *pname,int maxchars);
    void      (*setUser)(gtrPvt pvt,void * userPvt);
    void      *(*getUser)(gtrPvt pvt);
    void      (*lock)(gtrPvt pvt);
    void      (*unlock)(gtrPvt pvt);
}gtrops;

gtrPvt gtrFind(int card,gtrops **ppgtrops);

void gtrRegisterDriver(int card,
    const char *name,gtrops *pgtrdrvops,gtrPvt drvPvt);

#ifdef __cplusplus
}
#endif

#endif /*drvGtrH*/
