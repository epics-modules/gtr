/* $Id$ */

/* EPICS glue for the vmeUniverse driver */

#ifndef DRV_UNIVERSE_DMA_H
#define DRV_UNIVERSE_DMA_H

/* DMA Routines using the universe driver */
#ifdef _INSIDE_DRV_UNIVERSE_

#include <sysDma.h>

#else

#if !defined(__vxworks) && !defined(vxWorks) && !defined(VXWORKS) && !defined(vxworks)
typedef void 		(*VOIDFUNCPTR)();
typedef unsigned long	UINT32;
typedef unsigned short	UINT16;
typedef long		STATUS;
#endif

typedef struct dmaRequest *DMA_ID;
typedef DMA_ID(*sysDmaCreate)(VOIDFUNCPTR callback, void *context);
typedef STATUS (*sysDmaStatus)(DMA_ID dmaId);
typedef STATUS (*sysDmaToVme)(DMA_ID dmaId, UINT32 vmeAddr, int adrsSpace,
    void *pLocal, int length, int dataWidth);
typedef STATUS (*sysDmaFromVme)(DMA_ID dmaId, void *pLocal, UINT32 vmeAddr,
    int adrsSpace, int length, int dataWidth);

#endif

DMA_ID
universeDmaCreate(VOIDFUNCPTR callback, void *context);

STATUS
universeDmaStatus(DMA_ID dmaId);

/* retrieve the DGCS value of a terminated DMA */
unsigned long
universeDmaDGCS(DMA_ID dmaId);

STATUS
universeDmaFromVme(DMA_ID dmaId, void *pLocal, UINT32 vmeAddr,
	int adrsSpace, int length, int dataWidth);

STATUS
universeDmaToVme(DMA_ID dmaId, UINT32 vmeAddr, int adrsSpace,
    void *pLocal, int length, int dataWidth);

STATUS
universeDmaToVme(DMA_ID dmaId, UINT32 vmeAddr, int adrsSpace,
    void *pLocal, int length, int dataWidth);

STATUS
universeDmaStart(DMA_ID dmaId, void *pLocal, UINT32 vmeAddr,
	int adrsSpace, int length, int dataWidth, unsigned long dctl);


/* scatter/gather DMA
 *
 * This currently only supports scattering/gathering
 * from/to a contiguous address range (on one side
 * of the universe).
 */

/* An array of UniverseDmaBlockRecs describes
 * scattered blocks of data. Len is in bytes.
 */
typedef struct UniverseDmaBlockRec_ {
	char	*address;
	long	len;		/* terminate an array of BlockRecs with a -1 len */
} UniverseDmaBlockRec, *UniverseDmaBlock;

/* opaque handle for an initialized DMA descriptor list
 * (which is built from a UniverseDmaBlock array)
 */
typedef void *UniverseDmaList;

/* set to DMA from PCI->VME, clear for VME->PCI */
#define UNIVERSE_DMA_FLG_TO_VME		(1<<0)
/* set to scatter/gather from/to contigous VME area (non-contig PCI)
 * clr to scatter/gather from/to contigous PCI area (non-contig VME)
 * NOTE: two scattered lists currently not supported
 */
#define	UNIVERSE_DMA_FLG_CONTIG_VME	(1<<1)

/* Allocate and initialize a DMA descriptor list suitable
 * for doing linked-list DMA with the universe.
 *
 *  - the returned list may be released calling ordinary 'free()'
 *  - valid flags are described above
 *  - address space / dataWidth are as in universeDmaStart
 *  - DMA block array 'blk' must end with a len==-1 entry.
 *  - blocks are scattered/gathered from/to a contiguous
 *    address area starting at 'ctg'.
 *    NOTE every blk->address must be 8-byte aligned with
 *         its associated source/target address in the contiguous
 *         area.
 *
 * RETURNS: opaque handle to the initialized descriptor list
 *          or NULL in case of invalid parameters:
 *            o alignment violation
 *            o invalid adrsSpace and/or dataWidth
 *			  o invalid block list
 *
 * NOTE: no DMA transfer is initiated by this routine.
 */
UniverseDmaList *
universeDmaListSetup(
	UniverseDmaBlock blk,		/* block list to scatter/gather */
	char			 *ctg,		/* contiguous     source/target */
	int				 adrsSpace,
	int				 dataWidth,
	int				 flags
	);

/* start transferring a list-DMA
 *
 * RETURNS: 0
 */
STATUS
universeDmaListStart(DMA_ID dmaId, UniverseDmaList l);

#endif
