/*drvSisfadc.h */

/* Author:   Marty Kraimer */
/* Date:     213NOV2001     */

/*************************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of California, as
* Operator of Los Alamos National Laboratory. EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*************************************************************************/

#ifndef drvSisfadcH
#define drvSisfadcH

#ifdef __cplusplus
extern "C" {
#endif

int sisfadcConfig(int card,int clockSpeed,
    unsigned int a32offset,int intVec,int intLev,int useDma);

int idromGetID(char *a32addr, unsigned short *modId,
    unsigned short *clockSpeed,unsigned long *serialNumber);

#ifdef __cplusplus
}
#endif

#endif /*drvSisfadcH*/
