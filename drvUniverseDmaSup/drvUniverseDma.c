/* DMA Routines using the universe driver */

#include <stdlib.h>
#include <errno.h>

#include <epicsMutex.h>
#include <epicsInterrupt.h>
#include <errlog.h>
#include <devLib.h>

#include <basicIoOps.h>

#ifdef __vxworks

#include <vxWorks.h>
#include <vme.h>
#include <sysLib.h>
#include <vmeUniverse.h>


/* Here comes ugly stuff - there's no common API for mapping a local
 * memory address on the PCI bus.
 * PROBLEM: this is highly BSP specific. However, EPICS is only built
 *          generically, the build system doesn't know about vxWorks
 *          BSPs.
 *          Therefore, we use the sysLocalToBusAdrs() to find our
 *          memory's VME address. Then, we map back through the universe
 *          and retrieve its PCI address :-(
 * NOTE:    this hack doesn't work if our memory is not published on VME
 *          (although DMA could still work in this case)
 */

static __inline__ unsigned long
LOCAL2PCI(adrs)
{
unsigned long	pciaddr,vmeaddr;
int		i;
unsigned ams[]={
	VME_AM_EXT_SUP_DATA,
	VME_AM_EXT_USR_DATA,
};

	/* try a couple of address modifiers */
	for (i=0; i < sizeof(ams)/sizeof(ams[0]); i++) {
		/* find our VME address and reverse-map through the universe VME slave */
		if (OK==sysLocalToBusAdrs(ams[i],(void*)adrs,(void*)&vmeaddr) &&
		    OK==vmeUniverseXlateAddr(0  /* look in slave wins */,
					     1, /* look in reverse direction */
					     ams[i],
					     vmeaddr,
					     &pciaddr))
			return pciaddr;
	}
	return 0xdeadbeef;
}

#ifndef UNIV_DMA_INT_VEC
#error "You must find some way to determine your BSP's interrupt vector the universe bridge uses for DMA IRQS"
#endif

#elif defined(__rtems__)
#include <bsp.h>
#include <bsp/vmeUniverse.h>
#define LOCAL2PCI(adrs) ((unsigned long)(adrs)+(PCI_DRAM_OFFSET))
#endif

#define _INSIDE_DRV_UNIVERSE_
#include <drvUniverseDma.h>

#undef DEBUG

static epicsMutexId lock=0;
static DMA_ID inProgress=0;

typedef struct dmaRequest {
		VOIDFUNCPTR				callback;
		void					*closure;
		STATUS					status;
		unsigned long			dgcs;
} DmaRequest;

#define ERR_STAT_MASK (UNIV_DGCS_STATUS_CLEAR & ~UNIV_DGCS_DONE)

#ifdef DEBUG
unsigned long universeDmaLastDGCS=0;
#endif

static void
universeDMAisr(void *p)
{
unsigned long s=vmeUniverseReadReg(UNIV_REGOFF_DGCS);

#ifdef DEBUG
	universeDmaLastDGCS=s;
#endif

	if (inProgress) {
		inProgress->dgcs   = s;
		inProgress->status = s & ERR_STAT_MASK ? EIO : 0;
		if (inProgress->callback)
				inProgress->callback(inProgress->closure);
		inProgress = 0;
	}
	/* clear status by writing actual settings back */
	vmeUniverseWriteReg(s,  UNIV_REGOFF_DGCS);
	iobarrier_w();
	/* yield the driver */
	epicsMutexUnlock(lock);
}

static void
universeDmaInit(void)
{
	lock=epicsMutexCreate();
	/* clear possible pending IRQ */
	vmeUniverseWriteReg(
		UNIV_LINT_STAT_DMA,
		UNIV_REGOFF_LINT_STAT
		);

	/* setup global status register */
	vmeUniverseWriteReg(
		UNIV_DGCS_STATUS_CLEAR | UNIV_DGCS_INT_MSK,
		UNIV_REGOFF_DGCS);

	/* connect and enable DMA interrupt */
	assert( 0==devConnectInterruptVME(UNIV_DMA_INT_VEC,universeDMAisr,0) );

	vmeUniverseWriteReg(
		vmeUniverseReadReg(UNIV_REGOFF_LINT_EN) | UNIV_LINT_EN_DMA,
		UNIV_REGOFF_LINT_EN
		);
}

DMA_ID
universeDmaCreate(VOIDFUNCPTR callback, void *context)
{
DMA_ID	rval;
	/* lazy init */
	if (!lock) {
		universeDmaInit();
	}

	rval = malloc(sizeof(*rval));
	rval->callback = callback;
	rval->closure = context;

	return rval;
}

STATUS
universeDmaStatus(DMA_ID dmaId)
{
	return dmaId->status;
}

unsigned long
universeDmaDGCS(DMA_ID dmaId)
{
	return dmaId->dgcs;
}

STATUS
universeDmaFromVme(DMA_ID dmaId, void *pLocal, UINT32 vmeAddr,
	int adrsSpace, int length, int dataWidth)
{
	return universeDmaStart(
			dmaId,
			pLocal,
			vmeAddr, adrsSpace,
			length, dataWidth,
			UNIV_DCTL_VCT | UNIV_DCTL_LD64EN);
}

STATUS
universeDmaToVme(DMA_ID dmaId, UINT32 vmeAddr, int adrsSpace,
				void *pLocal, int length, int dataWidth)
{
	return universeDmaStart(
			dmaId,
			pLocal,
			vmeAddr, adrsSpace,
			length, dataWidth,
			UNIV_DCTL_L2V | UNIV_DCTL_VCT | UNIV_DCTL_LD64EN);
}

static unsigned long
dctlSetup(int adrsSpace, int dataWidth)
{
unsigned long dctl = 0;

	switch (dataWidth) {
		case 1: dctl |= UNIV_DCTL_VDW_8 ; break;
		case 2: dctl |= UNIV_DCTL_VDW_16; break;
		case 4: dctl |= UNIV_DCTL_VDW_32; break;
		case 8: dctl |= UNIV_DCTL_VDW_64; break;
		default:
			errlogPrintf("universe DMA: invalid data witdh\n");
			return -1;
	}
	if (adrsSpace & 0x4)
		dctl |= UNIV_DCTL_SUPER;
	if (VME_AM_IS_EXT(adrsSpace)) {
		dctl |= UNIV_DCTL_VAS_A32;
	} else if (VME_AM_IS_STD(adrsSpace)) {
		dctl |= UNIV_DCTL_VAS_A24;
	} else if (VME_AM_IS_SHORT(adrsSpace)) {
		dctl |= UNIV_DCTL_VAS_A16;
	} else {
		errlogPrintf("universe DMA: invalid AM\n");
		return -1;
	}
	return dctl;
}

/* universe DMA packets need 32byte alignment */
#define PACK_ALIGNMENT	32
#define PACK_ALIGN(num)							\
	( (VmeUniverseDMAPacket)					\
	  ( (((UINT32)(num)) + (PACK_ALIGNMENT-1))	\
	   & ~(PACK_ALIGNMENT-1)))

UniverseDmaList *
universeDmaListSetup(
	UniverseDmaBlock blk,		/* block list to scatter/gather */
	char			 *ctg,		/* contiguous     source/target */
	int				 adrsSpace,
	int				 dataWidth,
	int				 flags
	)
{
unsigned long			dctl  = dctlSetup(adrsSpace, dataWidth);
int						nPackets;
VmeUniverseDMAPacket	b,p,n;
void					*rval;

	/* invalid parameters ? */
	if ( (unsigned long)-1 == dctl )
		return 0;

	rval = ctg;
	/* count the number of packets required */
	for ( nPackets=0; blk[nPackets].len > 0; nPackets++ ) {
		/* check alignment requirements */
		if ( ((unsigned long)ctg ^ (unsigned long)blk[nPackets].address) & 7 )
			return 0;
		ctg += blk[nPackets].len;
	}
	/* restore */
	ctg = rval;

	if ( nPackets <= 0 )
		return 0;

	/* alloc DMA packets */
	rval = malloc(sizeof(*b) * nPackets + PACK_ALIGNMENT - 1);

	if ( !rval )
		return 0;

	/* beginning of packet area */
	b = PACK_ALIGN(rval);

	dctl |= UNIV_DCTL_VCT | UNIV_DCTL_LD64EN;

	if (flags & UNIVERSE_DMA_FLG_TO_VME)
		dctl |= UNIV_DCTL_L2V;

	for (n=b; nPackets>0;  nPackets--) {

		p = n;

		if (flags & UNIVERSE_DMA_FLG_CONTIG_VME) {
			/* contiguous area is on the VME side */
			p->dva = (LERegister)ctg;
			p->dla = LOCAL2PCI(blk->address);
		} else {
			/* contiguous PCI area */
			p->dva = (LERegister)blk->address;
			p->dla = LOCAL2PCI(ctg);
		}

		p->dtbc = blk->len; /* byte count of this block */

		p->dctl = dctl;

		/* next packet address */
		n++;
		p->dcpp = LOCAL2PCI(n);

		ctg    += blk->len;
		blk++;
	}
	/* close ring and mark end */
	p->dcpp = LOCAL2PCI( (UINT32)b | UNIV_DCPP_IMG_NULL ); 

	vmeUniverseCvtToLE((UINT32*)b, (UINT32*)n - (UINT32*)b);

	return rval;
}

STATUS
universeDmaListStart(DMA_ID dmaId, UniverseDmaList l)
{
unsigned long dgcs;

	dmaId->status = EINVAL;
	dmaId->dgcs   = 0;

	epicsMutexLock(lock);

	inProgress = dmaId;

	dgcs = vmeUniverseReadReg(UNIV_REGOFF_DGCS);

    /* clear global status register; set CHAIN flag */
	dgcs |= UNIV_DGCS_CHAIN;
	vmeUniverseWriteReg(dgcs, UNIV_REGOFF_DGCS);

    /* make sure count is 0 for linked list DMA */
    vmeUniverseWriteReg( 0x0, UNIV_REGOFF_DTBC);

    /* set the address of the descriptor chain */
    vmeUniverseWriteReg( LOCAL2PCI(PACK_ALIGN(l)), UNIV_REGOFF_DCPP);

	/* and GO */
	dgcs |= UNIV_DGCS_GO;
	vmeUniverseWriteReg(dgcs, UNIV_REGOFF_DGCS);

	return 0;
}


STATUS
universeDmaStart(DMA_ID dmaId, void *pLocal, UINT32 vmeAddr,
	int adrsSpace, int length, int dataWidth, unsigned long dctl)
{
STATUS status;

	dmaId->status = EINVAL;
	dmaId->dgcs   = 0;

	if ( (unsigned long)-1 == (dctl |= dctlSetup(adrsSpace, dataWidth)) )
		return -1;

	epicsMutexLock(lock);

	inProgress = dmaId;

	vmeUniverseWriteReg(dctl, UNIV_REGOFF_DCTL);

#ifdef DEBUG
	errlogPrintf("starting DMA from 0x%08x(VME) to 0x%08x (PCI); %i bytes\n",
					vmeAddr, pLocal, length);
#endif
	if ((status=vmeUniverseStartDMA(LOCAL2PCI(pLocal),vmeAddr,length)))
		epicsMutexUnlock(lock);

	return status;
}
