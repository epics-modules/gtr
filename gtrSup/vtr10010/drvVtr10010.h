/*drvVtr10010.h */

/* Author:   Marty Kraimer */
/* Date:     17SEP2001     */

/*************************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of California, as
* Operator of Los Alamos National Laboratory. EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*************************************************************************/

#ifndef drvVtr10010H
#define drvVtr10010H

#ifdef __cplusplus
extern "C" {
#endif

int vtr10010Config(int card,int a16offset,unsigned int a32offset,int intVec);

#ifdef __cplusplus
}
#endif

#endif /*drvVtr10010H*/
