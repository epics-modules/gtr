/*drvVtr10012.h */

/* Author:   Marty Kraimer */
/* Date:     17SEP2001     */

/*************************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of California, as
* Operator of Los Alamos National Laboratory. EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*************************************************************************/

#ifndef drvVtr10012H
#define drvVtr10012H

#ifdef __cplusplus
extern "C" {
#endif

int vtr10012Config(int card,
    int a16offset,unsigned int memoffset,
    int intVec,int intLev,int useDma,
    int nchannels, int kilosamplesPerChan);

int dma_xfer(
    unsigned long realVMEaddr,unsigned long RAMaddr,unsigned long byteCount);

#ifdef __cplusplus
}
#endif

#endif /*drvVtr10012H*/
