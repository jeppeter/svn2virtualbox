/* $Id$ */
/** @file
 * VirtualBox Guest Additions: Virtio Driver for Solaris, Ring implementation.
 */

/*
 * Copyright (C) 2010 Oracle Corporation
 *
 * Oracle Corporation confidential
 * All rights reserved
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "Virtio-solaris.h"

#include <iprt/asm.h>
#include <iprt/cdefs.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>

#include <sys/cmn_err.h>

#if defined(DEBUG_ramshankar)
# undef LogFlowFunc
# define LogFlowFunc        LogRel
# undef Log
# define Log                LogRel
# undef LogFlow
# define LogFlow            LogRel
#endif

/**
 * Returns the size of the ring in bytes given the number of elements and
 * alignment requirements.
 *
 * @param cElements         Number of elements.
 * @param Align             Alignment (must be a power of two).
 *
 * @return Size of the Virtio ring.
 */
size_t VirtioRingSize(uint64_t cElements, ulong_t Align)
{
    size_t cb = 0;
    cb  = cElements * sizeof(VIRTIORINGDESC);   /* Ring descriptors. */
    cb += 2 * sizeof(uint16_t);                 /* Available flags and index. */
    cb += cElements * sizeof(uint16_t);         /* Available descriptors. */

    size_t cbAlign = RT_ALIGN_Z(cb, Align);
    cbAlign += 2 * sizeof(uint16_t);                    /* Used flags and index. */
    cbAlign += cElements * sizeof(VIRTIORINGUSEDELEM);  /* Used descriptors. */

    return cbAlign;
}


/**
 * Initializes a ring of a queue. This associates the DMA virtual address
 * with the Ring structure's "pRingDesc".
 *
 * @param pQueue            Pointer to the Virtio Queue.
 * @param cDescs            Number of descriptors.
 * @param virtBuf           Buffer associated with the ring.
 * @param Align             Alignment (must be power of two).
 */
void VirtioRingInit(PVIRTIOQUEUE pQueue, uint_t cDescs, caddr_t virtBuf, ulong_t Align)
{
    PVIRTIORING pRing = &pQueue->Ring;
    pRing->cDesc          = cDescs;
    pRing->pRingDesc      = (void *)virtBuf;
    pRing->pRingAvail     = (PVIRTIORINGAVAIL)(virtBuf + (cDescs * sizeof(pRing->pRingDesc[0])));
    pRing->pRingUsedElem  = RT_ALIGN_PT(pRing->pRingAvail + RT_OFFSETOF(VIRTIORINGAVAIL, aRings[pQueue->Ring.cDesc]), Align,
                                        PVIRTIORINGUSEDELEM);

    for (uint_t i = 0; i < pRing->cDesc - 1; i++)
        pRing->pRingDesc[i].Next = i + 1;

    pQueue->FreeHeadIndex = 0;

    cmn_err(CE_NOTE, "cDesc=%u pRingDesc=%p pRingAvail=%p\n", pRing->cDesc, pRing->pRingDesc, pRing->pRingAvail);
}


/**
 * Push a buffer into the ring.
 *
 * @param pQueue            Pointer to the Virtio queue.
 * @param physBuf           Physical address of the buffer.
 * @param cbBuf             Size of the buffer in bytes.
 * @param fFlags            Buffer flags, see VIRTIO_FLAGS_RING_DESC_*.
 *
 * @return IPRT error code.
 */
int VirtioRingPush(PVIRTIOQUEUE pQueue, paddr_t physBuf, uint32_t cbBuf, uint16_t fFlags)
{
    /*
     * Claim a slot, fill the buffer and move head pointer.
     */
    uint_t FreeIndex          = pQueue->FreeHeadIndex;
    PVIRTIORING pRing         = &pQueue->Ring;

    if (FreeIndex >= pRing->cDesc - 1)
    {
        LogRel((VIRTIOLOGNAME ":VirtioRingPush: failed. No free descriptors. cDesc=%u\n", pRing->cDesc));
        return VERR_BUFFER_OVERFLOW;
    }

    PVIRTIORINGDESC pRingDesc = &pRing->pRingDesc[FreeIndex];

    AssertCompile(sizeof(physBuf) == sizeof(pRingDesc->AddrBuf));

    pQueue->cBufs++;
    uint_t AvailIndex = (pRing->pRingAvail->Index + pQueue->cBufs) % pQueue->Ring.cDesc;
    pRing->pRingAvail->aRings[AvailIndex - 1] = FreeIndex;

    pRingDesc->AddrBuf = physBuf;
    pRingDesc->cbBuf   = cbBuf;
    pRingDesc->fFlags  = fFlags;

    pQueue->FreeHeadIndex = pRingDesc->Next;

    ASMCompilerBarrier();

    cmn_err(CE_NOTE, "VirtioRingPush: cbBuf=%u FreeIndex=%u AvailIndex=%u cDesc=%u Queue->cBufs=%u\n",
            cbBuf, FreeIndex, AvailIndex, pQueue->Ring.cDesc,
            pQueue->cBufs);

    return VINF_SUCCESS;
}

