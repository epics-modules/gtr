#ifndef SYS_DMA_HELPER_H
#define SYS_DMA_HELPER_H
/* DMA Routines */

/*************************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of California, as
* Operator of Los Alamos National Laboratory. EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*************************************************************************/
#ifndef __vxworks
typedef void (*VOIDFUNCPTR)();
typedef unsigned long	UINT32;
typedef unsigned short	UINT16;
typedef long			STATUS;
#endif

typedef struct dmaRequest *DMA_ID;
typedef DMA_ID(*sysDmaCreate)(VOIDFUNCPTR callback, void *context);
typedef STATUS (*sysDmaStatus)(DMA_ID dmaId);
typedef STATUS (*sysDmaToVme)(DMA_ID dmaId, UINT32 vmeAddr, int adrsSpace,
    void *pLocal, int length, int dataWidth);
typedef STATUS (*sysDmaFromVme)(DMA_ID dmaId, void *pLocal, UINT32 vmeAddr,
    int adrsSpace, int length, int dataWidth);

#endif
