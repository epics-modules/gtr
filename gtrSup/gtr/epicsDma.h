#ifndef _EPICSDMA_H_
#define _EPICSDMA_H_

#include <epicsTypes.h>

typedef void (*epicsDmaCallback_t)(void *);
typedef struct epicsDmaInfo *epicsDmaId;

/*
 * EPICS wrappers/additions
 */
epicsDmaId epicsDmaCreate(epicsDmaCallback_t callback, void *context);
int epicsDmaStatus(epicsDmaId dmaId);
int epicsDmaToVme(epicsDmaId dmaId, epicsUInt32 vmeAddr, int adrsSpace,
                          void *pLocal, int length, int dataWidth);
int epicsDmaFromVme(epicsDmaId dmaId, void *pLocal, epicsUInt32 vmeAddr,
                          int adrsSpace, int length, int dataWidth);
int epicsDmaToVmeAndWait(epicsDmaId dmaId, epicsUInt32 vmeAddr, int adrsSpace,
                                 void *pLocal, int length, int dataWidth);
int epicsDmaFromVmeAndWait(epicsDmaId dmaId, void *pLocal, epicsUInt32 vmeAddr,
                                   int adrsSpace, int length, int dataWidth);

#endif /* _EPICSDMA_H_ */
