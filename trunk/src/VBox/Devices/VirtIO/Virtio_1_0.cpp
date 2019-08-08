/* $Id$ */
/** @file
 * Virtio_1_0 - Virtio Common Functions (VirtQ, VQueue, Virtio PCI)
 */

/*
 * Copyright (C) 2009-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_VIRTIO

#include <VBox/log.h>
#include <iprt/param.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/mem.h>
#include <VBox/vmm/pdmdev.h>
#include "Virtio_1_0_impl.h"
#include "Virtio_1_0.h"

#define INSTANCE(pState) pState->szInstance
#define IFACE_TO_STATE(pIface, ifaceName) ((VIRTIOSTATE *)((char*)(pIface) - RT_UOFFSETOF(VIRTIOSTATE, ifaceName)))

#define H2P(hVirtio) ((PVIRTIOSTATE)(hVirtio))

#ifdef LOG_ENABLED
# define QUEUENAME(s, q) (q->pcszName)
#endif

/**
 * Formats the logging of a memory-mapped I/O input or output value
 *
 * @param   pszFunc     - To avoid displaying this function's name via __FUNCTION__ or LogFunc()
 * @param   pszMember   - Name of struct member
 * @param   pv          - pointer to value
 * @param   cb          - size of value
 * @param   uOffset     - offset into member where value starts
 * @param   fWrite      - True if write I/O
 * @param   fHasIndex   - True if the member is indexed
 * @param   idx         - The index if fHasIndex
 */
void virtioLogMappedIoValue(const char *pszFunc, const char *pszMember, const void *pv, uint32_t cb,
                        uint32_t uOffset, bool fWrite, bool fHasIndex, uint32_t idx)
{

#define FMTHEX(fmtout, val, cNybs) \
    fmtout[cNybs] = '\0'; \
    for (uint8_t i = 0; i < cNybs; i++) \
        fmtout[(cNybs - i) -1] = "0123456789abcdef"[(val >> (i * 4)) & 0xf];

#define MAX_STRING   64
    char pszIdx[MAX_STRING] = { 0 };
    char pszDepiction[MAX_STRING] = { 0 };
    char pszFormattedVal[MAX_STRING] = { 0 };
    if (fHasIndex)
        RTStrPrintf(pszIdx, sizeof(pszIdx), "[%d]", idx);
    if (cb == 1 || cb == 2 || cb == 4 || cb == 8)
    {
        /* manually padding with 0's instead of \b due to different impl of %x precision than printf() */
        uint64_t val = 0;
        memcpy((char *)&val, pv, cb);
        FMTHEX(pszFormattedVal, val, cb * 2);
        if (uOffset != 0) /* display bounds if partial member access */
            RTStrPrintf(pszDepiction, sizeof(pszDepiction), "%s%s[%d:%d]",
                        pszMember, pszIdx, uOffset, uOffset + cb - 1);
        else
            RTStrPrintf(pszDepiction, sizeof(pszDepiction), "%s%s", pszMember, pszIdx);
        RTStrPrintf(pszDepiction, sizeof(pszDepiction), "%-30s", pszDepiction);
        uint32_t first = 0;
        for (uint8_t i = 0; i < sizeof(pszDepiction); i++)
            if (pszDepiction[i] == ' ' && first++ != 0)
                pszDepiction[i] = '.';
        Log(("%s: Guest %s %s 0x%s\n", \
                  pszFunc, fWrite ? "wrote" : "read ", pszDepiction, pszFormattedVal));
    }
    else /* odd number or oversized access, ... log inline hex-dump style */
    {
        Log(("%s: Guest %s %s%s[%d:%d]: %.*Rhxs\n", \
              pszFunc, fWrite ? "wrote" : "read ", pszMember,
              pszIdx, uOffset, uOffset + cb, cb, pv));
    }
}


void virtQueueReadDesc(PVIRTIOSTATE pState, PVIRTQ pVirtQ, uint32_t idx, PVIRTQ_DESC_T pDesc)
{
    //Log(("%s virtQueueReadDesc: ring=%p idx=%u\n", INSTANCE(pState), pVirtQ, idx));
    PDMDevHlpPhysRead(pState->CTX_SUFF(pDevIns),
                      pVirtQ->pGcPhysVirtqDescriptors + sizeof(VIRTQ_DESC_T) * (idx % pVirtQ->cbQueue),
                      pDesc, sizeof(VIRTQ_DESC_T));
}

uint16_t virtQueueReadAvail(PVIRTIOSTATE pState, PVIRTQ pVirtQ, uint32_t idx)
{
    uint16_t tmp;
    PDMDevHlpPhysRead(pState->CTX_SUFF(pDevIns),
                      pVirtQ->pGcPhysVirtqAvail + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[idx % pVirtQ->cbQueue]),
                      &tmp, sizeof(tmp));
    return tmp;
}

uint16_t virtQueueReadAvailFlags(PVIRTIOSTATE pState, PVIRTQ pVirtQ)
{
    uint16_t tmp;
    PDMDevHlpPhysRead(pState->CTX_SUFF(pDevIns),
                      pVirtQ->pGcPhysVirtqAvail + RT_UOFFSETOF(VIRTQ_AVAIL_T, fFlags),
                      &tmp, sizeof(tmp));
    return tmp;
}

uint16_t virtQueueReadUsedIndex(PVIRTIOSTATE pState, PVIRTQ pVirtQ)
{
    uint16_t tmp;
    PDMDevHlpPhysRead(pState->CTX_SUFF(pDevIns),
                      pVirtQ->pGcPhysVirtqUsed + RT_UOFFSETOF(VIRTQ_USED_T, uIdx),
                      &tmp, sizeof(tmp));
    return tmp;
}

void virtQueueWriteUsedIndex(PVIRTIOSTATE pState, PVIRTQ pVirtQ, uint16_t u16Value)
{
    PDMDevHlpPCIPhysWrite(pState->CTX_SUFF(pDevIns),
                          pVirtQ->pGcPhysVirtqAvail + RT_UOFFSETOF(VIRTQ_USED_T, uIdx),
                          &u16Value, sizeof(u16Value));
}

void virtQueueWriteUsedElem(PVIRTIOSTATE pState, PVIRTQ pVirtQ, uint32_t idx, uint32_t id, uint32_t uLen)
{

    RT_NOREF5(pState, pVirtQ, idx, id, uLen);
    /* PK TODO: Adapt to VirtIO 1.0
    VIRTQ_USED_ELEM_T elem;

    elem.id = id;
    elem.uLen = uLen;
    PDMDevHlpPCIPhysWrite(pState->CTX_SUFF(pDevIns),
                          pVirtQ->pGcPhysVirtqUsed + RT_UOFFSETOF_DYN(VIRTQ_USED_T, ring[idx % pVirtQ->cbQueue]),
                          &elem, sizeof(elem));
    */
}

void virtQueueSetNotification(PVIRTIOSTATE pState, PVIRTQ pVirtQ, bool fEnabled)
{
    RT_NOREF3(pState, pVirtQ, fEnabled);

/* PK TODO: Adapt to VirtIO 1.0
    uint16_t tmp;

    PDMDevHlpPhysRead(pState->CTX_SUFF(pDevIns),
                      pVirtQ->pGcPhysVirtqAvail + RT_UOFFSETOF(VIRTQ_USED_T, uFlags),
                      &tmp, sizeof(tmp));

    if (fEnabled)
        tmp &= ~ VIRTQ_USED_T_F_NO_NOTIFY;
    else
        tmp |= VIRTQ_USED_T_F_NO_NOTIFY;

    PDMDevHlpPCIPhysWrite(pState->CTX_SUFF(pDevIns),
                          pVirtQ->pGcPhysVirtqAvail + RT_UOFFSETOF(VIRTQ_USED_T, uFlags),
                          &tmp, sizeof(tmp));
*/
}

bool virtQueueSkip(PVIRTIOSTATE pState, PVQUEUE pQueue)
{

    RT_NOREF2(pState, pQueue);
/* PK TODO Adapt to VirtIO 1.0
    if (virtQueueIsEmpty(pState, pQueue))
        return false;

    Log2(("%s virtQueueSkip: %s avail_idx=%u\n", INSTANCE(pState),
          QUEUENAME(pState, pQueue), pQueue->uNextAvailIndex));
    pQueue->uNextAvailIndex++;
*/
    return true;
}

bool virtQueueGet(PVIRTIOSTATE pState, PVQUEUE pQueue, PVQUEUEELEM pElem, bool fRemove)
{

    RT_NOREF4(pState, pQueue, pElem, fRemove);

/* PK TODO: Adapt to VirtIO 1.0
    if (virtQueueIsEmpty(pState, pQueue))
        return false;

    pElem->nIn = pElem->nOut = 0;

    Log2(("%s virtQueueGet: %s avail_idx=%u\n", INSTANCE(pState),
          QUEUENAME(pState, pQueue), pQueue->uNextAvailIndex));

    VIRTQ_DESC_T desc;
    uint16_t  idx = virtQueueReadAvail(pState, &pQueue->VirtQ, pQueue->uNextAvailIndex);
    if (fRemove)
        pQueue->uNextAvailIndex++;
    pElem->idx = idx;
    do
    {
        VQUEUESEG *pSeg;

        //
        // Malicious guests may try to trick us into writing beyond aSegsIn or
        // aSegsOut boundaries by linking several descriptors into a loop. We
        // cannot possibly get a sequence of linked descriptors exceeding the
        // total number of descriptors in the ring (see @bugref{8620}).
        ///
        if (pElem->nIn + pElem->nOut >= VIRTQ_MAX_SIZE)
        {
            static volatile uint32_t s_cMessages  = 0;
            static volatile uint32_t s_cThreshold = 1;
            if (ASMAtomicIncU32(&s_cMessages) == ASMAtomicReadU32(&s_cThreshold))
            {
                LogRel(("%s: too many linked descriptors; check if the guest arranges descriptors in a loop.\n",
                        INSTANCE(pState)));
                if (ASMAtomicReadU32(&s_cMessages) != 1)
                    LogRel(("%s: (the above error has occured %u times so far)\n",
                            INSTANCE(pState), ASMAtomicReadU32(&s_cMessages)));
                ASMAtomicWriteU32(&s_cThreshold, ASMAtomicReadU32(&s_cThreshold) * 10);
            }
            break;
        }
        RT_UNTRUSTED_VALIDATED_FENCE();

        virtQueueReadDesc(pState, &pQueue->VirtQ, idx, &desc);
        if (desc.u16Flags & VIRTQ_DESC_T_F_WRITE)
        {
            Log2(("%s virtQueueGet: %s IN  seg=%u desc_idx=%u addr=%p cb=%u\n", INSTANCE(pState),
                  QUEUENAME(pState, pQueue), pElem->nIn, idx, desc.addr, desc.uLen));
            pSeg = &pElem->aSegsIn[pElem->nIn++];
        }
        else
        {
            Log2(("%s virtQueueGet: %s OUT seg=%u desc_idx=%u addr=%p cb=%u\n", INSTANCE(pState),
                  QUEUENAME(pState, pQueue), pElem->nOut, idx, desc.addr, desc.uLen));
            pSeg = &pElem->aSegsOut[pElem->nOut++];
        }

        pSeg->addr = desc.addr;
        pSeg->cb   = desc.uLen;
        pSeg->pv   = NULL;

        idx = desc.next;
    } while (desc.u16Flags & VIRTQ_DESC_T_F_NEXT);

    Log2(("%s virtQueueGet: %s head_desc_idx=%u nIn=%u nOut=%u\n", INSTANCE(pState),
          QUEUENAME(pState, pQueue), pElem->idx, pElem->nIn, pElem->nOut));
*/
    return true;
}



void virtQueuePut(PVIRTIOSTATE pState, PVQUEUE pQueue,
               PVQUEUEELEM pElem, uint32_t uTotalLen, uint32_t uReserved)
{

    RT_NOREF5(pState, pQueue, pElem, uTotalLen, uReserved);

/* PK TODO Re-work this for VirtIO 1.0
    Log2(("%s virtQueuePut: %s"
          " desc_idx=%u acb=%u (%u)\n",
          INSTANCE(pState), QUEUENAME(pState, pQueue),
          pElem->idx, uTotalLen, uReserved));

    Assert(uReserved < uTotalLen);

    uint32_t cbLen = uTotalLen - uReserved;
    uint32_t cbSkip = uReserved;

    for (unsigned i = 0; i < pElem->nIn && cbLen > 0; ++i)
    {
        if (cbSkip >= pElem->aSegsIn[i].cb) // segment completely skipped?
        {
            cbSkip -= pElem->aSegsIn[i].cb;
            continue;
        }

        uint32_t cbSegLen = pElem->aSegsIn[i].cb - cbSkip;
        if (cbSegLen > cbLen)   // last segment only partially used?
            cbSegLen = cbLen;

        //
        // XXX: We should assert pv != NULL, but we need to check and
        // fix all callers first.
        //
        if (pElem->aSegsIn[i].pv != NULL)
        {
            Log2(("%s virtQueuePut: %s"
                  " used_idx=%u seg=%u addr=%p pv=%p cb=%u acb=%u\n",
                  INSTANCE(pState), QUEUENAME(pState, pQueue),
                  pQueue->uNextUsedIndex, i,
                  (void *)pElem->aSegsIn[i].addr, pElem->aSegsIn[i].pv,
                  pElem->aSegsIn[i].cb, cbSegLen));

            PDMDevHlpPCIPhysWrite(pState->CTX_SUFF(pDevIns),
                                  pElem->aSegsIn[i].addr + cbSkip,
                                  pElem->aSegsIn[i].pv,
                                  cbSegLen);
        }

        cbSkip = 0;
        cbLen -= cbSegLen;
    }

    Log2(("%s virtQueuePut: %s"
          " used_idx=%u guest_used_idx=%u id=%u len=%u\n",
          INSTANCE(pState), QUEUENAME(pState, pQueue),
          pQueue->uNextUsedIndex, virtQueueReadUsedIndex(pState, &pQueue->VirtQ),
          pElem->idx, uTotalLen));

    virtQueueWriteUsedElem(pState, &pQueue->VirtQ,
                       pQueue->uNextUsedIndex++,
                       pElem->idx, uTotalLen);
*/

}


void virtQueueNotify(PVIRTIOSTATE pState, PVQUEUE pQueue)
{

    RT_NOREF2(pState, pQueue);
/* PK TODO Adapt to VirtIO 1.0
    LogFlow(("%s virtQueueNotify: %s availFlags=%x guestFeatures=%x virtQueue is %sempty\n",
             INSTANCE(pState), QUEUENAME(pState, pQueue),
             virtQueueReadAvailFlags(pState, &pQueue->VirtQ),
             pState->uGuestFeatures, virtQueueIsEmpty(pState, pQueue)?"":"not "));
    if (!(virtQueueReadAvailFlags(pState, &pQueue->VirtQ) & VIRTQ_AVAIL_T_F_NO_INTERRUPT)
        || ((pState->uGuestFeatures & VIRTIO_F_NOTIFY_ON_EMPTY) && virtQueueIsEmpty(pState, pQueue)))
    {
        int rc = virtioRaiseInterrupt(pState, VERR_INTERNAL_ERROR, VIRTIO_ISR_QUEUE);
        if (RT_FAILURE(rc))
            Log(("%s virtQueueNotify: Failed to raise an interrupt (%Rrc).\n", INSTANCE(pState), rc));
    }
*/
}

void virtQueueSync(PVIRTIOSTATE pState, PVQUEUE pQueue)
{
    RT_NOREF(pState, pQueue);
/* PK TODO Adapt to VirtIO 1.0
    Log2(("%s virtQueueSync: %s old_used_idx=%u new_used_idx=%u\n", INSTANCE(pState),
          QUEUENAME(pState, pQueue), virtQueueReadUsedIndex(pState, &pQueue->VirtQ), pQueue->uNextUsedIndex));
    virtQueueWriteUsedIndex(pState, &pQueue->VirtQ, pQueue->uNextUsedIndex);
    virtQueueNotify(pState, pQueue);
*/
}



/**
 * Raise interrupt.
 *
 * @param   pState      The device state structure.
 * @param   rcBusy      Status code to return when the critical section is busy.
 * @param   u8IntCause  Interrupt cause bit mask to set in PCI ISR port.
 */
__attribute__((unused))
int virtioRaiseInterrupt(VIRTIOSTATE *pState, int rcBusy, uint8_t u8IntCause)
{
    RT_NOREF2(pState, u8IntCause);
    RT_NOREF_PV(rcBusy);
    LogFlow(("%s virtioRaiseInterrupt: u8IntCause=%x\n",
             INSTANCE(pState), u8IntCause));

    pState->uISR |= u8IntCause;
    PDMDevHlpPCISetIrq(pState->CTX_SUFF(pDevIns), 0, 1);
    return VINF_SUCCESS;
}

/**
 * Lower interrupt.
 *
 * @param   pState      The device state structure.
 */
__attribute__((unused))
static void virtioLowerInterrupt(VIRTIOSTATE *pState)
{
    LogFlow(("%s virtioLowerInterrupt\n", INSTANCE(pState)));
    PDMDevHlpPCISetIrq(pState->CTX_SUFF(pDevIns), 0, 0);
}


#ifdef IN_RING3
/**
 * Saves the state of device.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pSSM        The handle to the saved state.
 */
int virtioSaveExec(PVIRTIOSTATE pVirtio, PSSMHANDLE pSSM)
{
    int rc = VINF_SUCCESS;
    virtioDumpState(pVirtio, "virtioSaveExec");
    RT_NOREF(pSSM);
    /*
     * PK TODO save guest features, queue selector, sttus ISR,
     *              and per queue info (size, address, indicies)...
     * using calls like SSMR3PutU8(), SSMR3PutU16(), SSMR3PutU16()...
     * and AssertRCReturn(rc, rc)
     */

    return rc;
}

/**
 * Loads a saved device state.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pSSM        The handle to the saved state.
 * @param   uVersion    The data unit version number.
 * @param   uPass       The data pass.
 */
int virtioLoadExec(PVIRTIOSTATE pVirtio, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass, uint32_t uNumQueues)
{
    RT_NOREF5(pVirtio, pSSM, uVersion, uPass, uNumQueues);
    int rc = VINF_SUCCESS;
    virtioDumpState(pVirtio, "virtioLoadExec");

    /*
     * PK TODO, restore everything saved in virtioSaveExect, using
     * using calls like SSMR3PutU8(), SSMR3PutU16(), SSMR3PutU16()...
     * and AssertRCReturn(rc, rc)
     */
    if (uPass == SSM_PASS_FINAL)
    {
    }
    return rc;
}

/**
 * Device relocation callback.
 *
 * When this callback is called the device instance data, and if the
 * device have a GC component, is being relocated, or/and the selectors
 * have been changed. The device must use the chance to perform the
 * necessary pointer relocations and data updates.
 *
 * Before the GC code is executed the first time, this function will be
 * called with a 0 delta so GC pointer calculations can be one in one place.
 *
 * @param   pDevIns     Pointer to the device instance.
 * @param   offDelta    The relocation delta relative to the old location.
 *
 * @remark  A relocation CANNOT fail.
 */
void virtioRelocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    RT_NOREF(offDelta);
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);

    pVirtio->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    // TBD
}

PVQUEUE virtioAddQueue(VIRTIOSTATE* pState, unsigned cbQueue, const char *pcszName)
{

    RT_NOREF3(pState, cbQueue, pcszName);
/* PK TODO Adapt to VirtIO 1.0

    PVQUEUE pQueue = NULL;
    // Find an empty queue slot
    for (unsigned i = 0; i < pState->uNumQueues; i++)
    {
        if (pState->Queues[i].VirtQ.cbQueue == 0)
        {
            pQueue = &pState->Queues[i];
            break;
        }
    }

    if (!pQueue)
    {
        Log(("%s Too many queues being added, no empty slots available!\n", INSTANCE(pState)));
    }
    else
    {
        pQueue->VirtQ.cbQueue = cbQueue;
        pQueue->VirtQ.addrDescriptors = 0;
        pQueue->uPageNumber = 0;
        pQueue->pfnCallback = pfnCallback;
        pQueue->pcszName    = pcszName;
    }
    return pQueue;
*/
    return NULL;// Temporary
}



__attribute__((unused))
static void virtQueueInit(PVQUEUE pQueue, uint32_t uPageNumber)
{
    RT_NOREF2(pQueue, uPageNumber);

/* PK TODO, re-work this for VirtIO 1.0
    pQueue->VirtQ.addrDescriptors = (uint64_t)uPageNumber << PAGE_SHIFT;

    pQueue->VirtQ.addrAvail = pQueue->VirtQ.addrDescriptors
                                + sizeof(VIRTQ_DESC_T) * pQueue->VirtQ.cbQueue;

    pQueue->VirtQ.addrUsed  = RT_ALIGN(pQueue->VirtQ.addrAvail
            + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, ring[pQueue->VirtQ.cbQueue])
            + sizeof(uint16_t), // virtio 1.0 adds a 16-bit field following ring data
              PAGE_SIZE); // The used ring must start from the next page.

    pQueue->uNextAvailIndex       = 0;
    pQueue->uNextUsedIndex        = 0;
*/

}


__attribute__((unused))
static void virtQueueReset(PVQUEUE pQueue)
{
    RT_NOREF(pQueue);
/* PK TODO Adapt to VirtIO 1.0
    pQueue->VirtQ.addrDescriptors = 0;
    pQueue->VirtQ.addrAvail       = 0;
    pQueue->VirtQ.addrUsed        = 0;
    pQueue->uNextAvailIndex           = 0;
    pQueue->uNextUsedIndex            = 0;
    pQueue->uPageNumber               = 0;
*/
}

/**
 * Notify driver of a configuration or queue event
 *
 * @param   pVirtio             - Pointer to instance state
 * @param   fConfigChange       - True if cfg change notification else, queue notification
 */
static void virtioNotifyDriver(VIRTIOHANDLE hVirtio, bool fConfigChange)
{
   RT_NOREF(hVirtio);
   LogFunc(("fConfigChange = %d\n", fConfigChange));
}


int virtioReset(VIRTIOHANDLE hVirtio)  /* Part of our "custom API" */
{
    PVIRTIOSTATE pVirtio = H2P(hVirtio);

    RT_NOREF(pVirtio);
/* PK TODO Adapt to VirtIO 1.09
    pState->uGuestFeatures = 0;
    pState->uQueueSelector = 0;
    pState->uStatus        = 0;
    pState->uISR           = 0;

    for (unsigned i = 0; i < pState->uNumQueues; i++)
        virtQueueReset(&pState->Queues[i]);
    virtioNotify(pVirtio);
*/
    return VINF_SUCCESS;
}

__attribute__((unused))
static void virtioSetNeedsReset(PVIRTIOSTATE pVirtio)
{
    pVirtio->uDeviceStatus |= VIRTIO_STATUS_DEVICE_NEEDS_RESET;
    if (pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK)
    {
        pVirtio->fGenUpdatePending = true;
        virtioNotifyDriver(pVirtio, true);
    }
}

static void virtioResetDevice(PVIRTIOSTATE pVirtio)
{

    LogFunc(("\n"));
    pVirtio->uDeviceStatus          = 0;
    pVirtio->uDeviceFeaturesSelect  = 0;
    pVirtio->uDriverFeaturesSelect  = 0;
    pVirtio->uConfigGeneration      = 0;
    pVirtio->uNumQueues             = VIRTIO_MAX_QUEUES;

    for (uint32_t i = 0; i < pVirtio->uNumQueues; i++)
    {
        pVirtio->uQueueSize[i]      = VIRTQ_MAX_SIZE;
        pVirtio->uQueueNotifyOff[i] = i;
//      virtqNotify();
    }
}

/**
 * Handle accesses to Common Configuration capability
 *
 * @returns VBox status code
 *
 * @param   pVirtio     Virtio instance state
 * @param   fWrite      If write access (otherwise read access)
 * @param   pv          Pointer to location to write to or read from
 * @param   cb          Number of bytes to read or write
 */
int virtioCommonCfgAccessed(PVIRTIOSTATE pVirtio, int fWrite, off_t uOffset, unsigned cb, void const *pv)
{
    int rc = VINF_SUCCESS;
    uint64_t val;
    if (COMMON_CFG(uDeviceFeatures))
    {
        if (fWrite) /* Guest WRITE pCommonCfg>uDeviceFeatures */
            Log(("Guest attempted to write readonly virtio_pci_common_cfg.device_feature\n"));
        else /* Guest READ pCommonCfg->uDeviceFeatures */
        {
            uint32_t uIntraOff = uOffset - RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDeviceFeatures);
            switch(pVirtio->uDeviceFeaturesSelect)
            {
                case 0:
                    val = pVirtio->uDeviceFeatures & 0xffffffff;
                    memcpy((void *)pv, (const void *)&val, cb);
                    LOG_ACCESSOR(uDeviceFeatures);
                    break;
                case 1:
                    val = (pVirtio->uDeviceFeatures >> 32) & 0xffffffff;
                    uIntraOff += 4;
                    memcpy((void *)pv, (const void *)&val, cb);
                    LOG_ACCESSOR(uDeviceFeatures);
                    break;
                default:
                    LogFunc(("Guest read uDeviceFeatures with out of range selector (%d), returning 0\n",
                          pVirtio->uDeviceFeaturesSelect));
                    return VERR_ACCESS_DENIED;
            }
        }
    }
    else if (COMMON_CFG(uDriverFeatures))
    {
        if (fWrite) /* Guest WRITE pCommonCfg->udriverFeatures */
        {
            uint32_t uIntraOff = uOffset - RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDriverFeatures);
            switch(pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    memcpy(&pVirtio->uDriverFeatures, pv, cb);
                    LOG_ACCESSOR(uDriverFeatures);
                    break;
                case 1:
                    memcpy(((char *)&pVirtio->uDriverFeatures) + sizeof(uint32_t), pv, cb);
                    uIntraOff += 4;
                    LOG_ACCESSOR(uDriverFeatures);
                    break;
                default:
                    LogFunc(("Guest wrote uDriverFeatures with out of range selector (%d), returning 0\n",
                         pVirtio->uDriverFeaturesSelect));
                    return VERR_ACCESS_DENIED;
            }
        }
        else /* Guest READ pCommonCfg->udriverFeatures */
        {
            uint32_t uIntraOff = uOffset - RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDriverFeatures);
            switch(pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    val = pVirtio->uDriverFeatures & 0xffffffff;
                    memcpy((void *)pv, (const void *)&val, cb);
                    LOG_ACCESSOR(uDriverFeatures);
                    break;
                case 1:
                    val = (pVirtio->uDriverFeatures >> 32) & 0xffffffff;
                    uIntraOff += 4;
                    memcpy((void *)pv, (const void *)&val, cb);
                    LOG_ACCESSOR(uDriverFeatures);
                    break;
                default:
                    LogFunc(("Guest read uDriverFeatures with out of range selector (%d), returning 0\n",
                         pVirtio->uDriverFeaturesSelect));
                    return VERR_ACCESS_DENIED;
            }
        }
    }
    else if (COMMON_CFG(uNumQueues))
    {
        if (fWrite)
        {
            LogFunc(("Guest attempted to write readonly virtio_pci_common_cfg.num_queues\n"));
            return VERR_ACCESS_DENIED;
        }
        else
        {
            uint32_t uIntraOff = 0;
            *(uint16_t *)pv = VIRTIO_MAX_QUEUES;
            LOG_ACCESSOR(uNumQueues);
        }
    }
    else if (COMMON_CFG(uDeviceStatus))
    {
        if (fWrite) /* Guest WRITE pCommonCfg->uDeviceStatus */
        {
            pVirtio->uDeviceStatus = *(uint8_t *)pv;
            LogFunc(("Guest wrote uDeviceStatus ................ ("));
            virtioLogDeviceStatus(pVirtio->uDeviceStatus);
            Log((")\n"));
            if (pVirtio->uDeviceStatus == 0)
                virtioResetDevice(pVirtio);
        }
        else /* Guest READ pCommonCfg->uDeviceStatus */
        {
            LogFunc(("Guest read  uDeviceStatus ................ ("));
            *(uint32_t *)pv = pVirtio->uDeviceStatus;
            virtioLogDeviceStatus(pVirtio->uDeviceStatus);
            Log((")\n"));
        }
    }
    else if (COMMON_CFG(uMsixConfig))
    {
        ACCESSOR(uMsixConfig);
    }
    else if (COMMON_CFG(uDeviceFeaturesSelect))
    {
        ACCESSOR(uDeviceFeaturesSelect);
    }
    else if (COMMON_CFG(uDriverFeaturesSelect))
    {
        ACCESSOR(uDriverFeaturesSelect);
    }
    else if (COMMON_CFG(uConfigGeneration))
    {
        ACCESSOR_READONLY(uConfigGeneration);
    }
    else if (COMMON_CFG(uQueueSelect))
    {
        ACCESSOR(uQueueSelect);
    }
    else if (COMMON_CFG(uQueueSize))
    {
        ACCESSOR_WITH_IDX(uQueueSize, pVirtio->uQueueSelect);
    }
    else if (COMMON_CFG(uQueueMsixVector))
    {
        ACCESSOR_WITH_IDX(uQueueMsixVector, pVirtio->uQueueSelect);
    }
    else if (COMMON_CFG(uQueueEnable))
    {
        ACCESSOR_WITH_IDX(uQueueEnable, pVirtio->uQueueSelect);
    }
    else if (COMMON_CFG(uQueueNotifyOff))
    {
        ACCESSOR_READONLY_WITH_IDX(uQueueNotifyOff, pVirtio->uQueueSelect);
    }
    else if (COMMON_CFG(uQueueDesc))
    {
        ACCESSOR_WITH_IDX(uQueueDesc, pVirtio->uQueueSelect);
    }
    else if (COMMON_CFG(uQueueAvail))
    {
        ACCESSOR_WITH_IDX(uQueueAvail, pVirtio->uQueueSelect);
    }
    else if (COMMON_CFG(uQueueUsed))
    {
        ACCESSOR_WITH_IDX(uQueueUsed, pVirtio->uQueueSelect);
    }
    else
    {
        LogFunc(("Bad guest %s access to virtio_pci_common_cfg: uOffset=%d, cb=%d\n",
            fWrite ? "write" : "read ", uOffset, cb));
        rc = VERR_ACCESS_DENIED;
    }
    return rc;
}

/**
 * Memory mapped I/O Handler for PCI Capabilities read operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   GCPhysAddr  Physical address (in GC) where the read starts.
 * @param   pv          Where to store the result.
 * @param   cb          Number of bytes read.
 */
PDMBOTHCBDECL(int) virtioR3MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void *pv, unsigned cb)
{
    RT_NOREF(pvUser);
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    int rc = VINF_SUCCESS;

//#ifdef LOG_ENABLED
//    LogFunc(("pVirtio=%#p GCPhysAddr=%RGp pv=%#p{%.*Rhxs} cb=%u\n", pVirtio, GCPhysAddr, pv, cb, pv, cb));
//#endif

    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysDeviceCap, pVirtio->pDeviceCap,     fDevSpecific);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysCommonCfg, pVirtio->pCommonCfgCap,  fCommonCfg);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysIsrCap,    pVirtio->pIsrCap,        fIsrCap);

    if (fDevSpecific)
    {
        uint32_t uDevSpecificDataOffset = GCPhysAddr - pVirtio->pGcPhysDeviceCap;
        /**
         * Callback to client to manage device-specific configuration and changes to it.
         */
        rc = pVirtio->virtioCallbacks.pfnVirtioDevCapRead(pDevIns, uDevSpecificDataOffset, pv, cb);
        /**
         * Anytime any part of the device-specific configuration (which our client maintains) is read
         * it needs to be checked to see if it changed since the last time any part was read, in
         * order to maintain the config generation (see VirtIO 1.0 spec, section 4.1.4.3.1)
         */
        uint32_t fDevSpecificFieldChanged = false;

        if (memcmp((char *)pv + uDevSpecificDataOffset, (char *)pVirtio->pPrevDevSpecificCap + uDevSpecificDataOffset, cb))
            fDevSpecificFieldChanged = true;

        memcpy(pVirtio->pPrevDevSpecificCap, pv, pVirtio->cbDevSpecificCap);
        if (pVirtio->fGenUpdatePending || fDevSpecificFieldChanged)
        {
            if (fDevSpecificFieldChanged)
                LogFunc(("Dev specific config field changed since last read, gen++ = %d\n",
                     pVirtio->uConfigGeneration));
            else
                LogFunc(("Config generation pending flag set, gen++ = %d\n",
                     pVirtio->uConfigGeneration));
            ++pVirtio->uConfigGeneration;
            pVirtio->fGenUpdatePending = false;
        }
    }
    else
    if (fCommonCfg)
    {
        uint32_t uCommonCfgDataOffset = GCPhysAddr - pVirtio->pGcPhysCommonCfg;
        virtioCommonCfgAccessed(pVirtio, 0 /* fWrite */, uCommonCfgDataOffset, cb, pv);
    }
    else
    if (fIsrCap)
    {
        *(uint8_t *)pv = pVirtio->fQueueInterrupt | pVirtio->fDeviceConfigInterrupt << 1;
        LogFunc(("Read 0x%s from pIsrCap\n", *(uint8_t *)pv));
    }
    else {

        AssertMsgFailed(("virtio: Read outside of capabilities region: GCPhysAddr=%RGp cb=%RGp\n", GCPhysAddr, cb));
    }
    return rc;
}

/**
 * Memory mapped I/O Handler for PCI Capabilities write operations.
 *
 * @returns VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   GCPhysAddr  Physical address (in GC) where the write starts.
 * @param   pv          Where to fetch the result.
 * @param   cb          Number of bytes to write.
 */
PDMBOTHCBDECL(int) virtioR3MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS GCPhysAddr, void const *pv, unsigned cb)
{
    RT_NOREF(pvUser);
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    int rc = VINF_SUCCESS;

//#ifdef LOG_ENABLED
//    LogFunc(("pVirtio=%#p GCPhysAddr=%RGp pv=%#p{%.*Rhxs} cb=%u\n", pVirtio, GCPhysAddr, pv, cb, pv, cb));
//#endif

    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysDeviceCap, pVirtio->pDeviceCap,     fDevSpecific);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysCommonCfg, pVirtio->pCommonCfgCap,  fCommonCfg);
    MATCH_VIRTIO_CAP_STRUCT(pVirtio->pGcPhysIsrCap,    pVirtio->pIsrCap,        fIsrCap);

    if (fDevSpecific)
    {
        uint32_t uDevSpecificDataOffset = GCPhysAddr - pVirtio->pGcPhysDeviceCap;
        rc = pVirtio->virtioCallbacks.pfnVirtioDevCapWrite(pDevIns, uDevSpecificDataOffset, pv, cb);
    }
    else
    if (fCommonCfg)
    {
        uint32_t uCommonCfgDataOffset = GCPhysAddr - pVirtio->pGcPhysCommonCfg;
        virtioCommonCfgAccessed(pVirtio, 1 /* fWrite */, uCommonCfgDataOffset, cb, pv);
    }
    else
    if (fIsrCap)
    {
        pVirtio->fQueueInterrupt = (*(uint8_t *)pv) & 1;
        pVirtio->fDeviceConfigInterrupt = !!(*((uint8_t *)pv) & 2);
        Log(("pIsrCap... setting fQueueInterrupt=%d fDeviceConfigInterrupt=%d\n",
              pVirtio->fQueueInterrupt, pVirtio->fDeviceConfigInterrupt));
    }
    else
    {
       LogFunc(("virtio: Write outside of capabilities region:\nGCPhysAddr=%RGp cb=%RGp,", GCPhysAddr, cb));
    }
    return rc;
}


/**
 * @callback_method_impl{FNPCIIOREGIONMAP}
 */
static DECLCALLBACK(int) virtioR3Map(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                     RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    RT_NOREF3(pPciDev, iRegion, enmType);
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);
    int rc = VINF_SUCCESS;

    Assert(cb >= 32);

    if (iRegion == pVirtio->uVirtioCapBar)
    {
        /* We use the assigned size here, because we currently only support page aligned MMIO ranges. */
        rc = PDMDevHlpMMIORegister(pDevIns, GCPhysAddress, cb, NULL /*pvUser*/,
                           IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                           virtioR3MmioWrite, virtioR3MmioRead,
                           "virtio-scsi MMIO");

        if (RT_FAILURE(rc))
        {
            LogFunc(("virtio: PCI Capabilities failed to map GCPhysAddr=%RGp cb=%RGp, region=%d\n",
                    GCPhysAddress, cb, iRegion));
            return rc;
        }
        LogFunc(("virtio: PCI Capabilities mapped at GCPhysAddr=%RGp cb=%RGp, region=%d\n",
                GCPhysAddress, cb, iRegion));
        pVirtio->GCPhysPciCapBase = GCPhysAddress;
        pVirtio->pGcPhysCommonCfg = GCPhysAddress + pVirtio->pCommonCfgCap->uOffset;
        pVirtio->pGcPhysNotifyCap = GCPhysAddress + pVirtio->pNotifyCap->pciCap.uOffset;
        pVirtio->pGcPhysIsrCap    = GCPhysAddress + pVirtio->pIsrCap->uOffset;
        if (pVirtio->pDevSpecificCap)
            pVirtio->pGcPhysDeviceCap = GCPhysAddress + pVirtio->pDeviceCap->uOffset;
    }
    return rc;
}

/**
  * Callback function for reading from the PCI configuration space.
  *
  * @returns The register value.
  * @param   pDevIns         Pointer to the device instance the PCI device
  *                          belongs to.
  * @param   pPciDev         Pointer to PCI device. Use pPciDev->pDevIns to get the device instance.
  * @param   uAddress        The configuration space register address. [0..4096]
  * @param   cb              The register size. [1,2,4]
  *
  * @remarks Called with the PDM lock held.  The device lock is NOT take because
  *          that is very likely be a lock order violation.
  */
static DECLCALLBACK(uint32_t) virtioPciConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                       uint32_t uAddress, unsigned cb)
{
//    PVIRTIOSTATE pVirtio = PDMINS_2_DATA(pDevIns, PVIRTIOSTATE);
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);

    if (uAddress == (uint64_t)&pVirtio->pPciCfgCap->uPciCfgData)
    {
        /* VirtIO 1.0 spec section 4.1.4.7 describes a required alternative access capability
         * whereby the guest driver can specify a bar, offset, and length via the PCI configuration space
         * (the virtio_pci_cfg_cap capability), and access data items. */
        uint32_t uLength = pVirtio->pPciCfgCap->pciCap.uLength;
        uint32_t uOffset = pVirtio->pPciCfgCap->pciCap.uOffset;
        uint8_t  uBar    = pVirtio->pPciCfgCap->pciCap.uBar;
        uint32_t pv = 0;
        if (uBar == pVirtio->uVirtioCapBar)
            (void)virtioR3MmioRead(pDevIns, NULL, (RTGCPHYS)((uint32_t)pVirtio->GCPhysPciCapBase + uOffset),
                                    &pv, uLength);
        else
        {
            LogFunc(("Guest read virtio_pci_cfg_cap.pci_cfg_data using unconfigured BAR. Ignoring"));
            return 0;
        }
        LogFunc(("virtio: Guest read  virtio_pci_cfg_cap.pci_cfg_data, bar=%d, offset=%d, length=%d, result=%d\n",
                uBar, uOffset, uLength, pv));
        return pv;
    }
    return pVirtio->pfnPciConfigReadOld(pDevIns, pPciDev, uAddress, cb);
}

/**
 * Callback function for writing to the PCI configuration space.
 *
 * @returns VINF_SUCCESS or PDMDevHlpDBGFStop status.
 *
 * @param   pDevIns         Pointer to the device instance the PCI device
 *                          belongs to.
 * @param   pPciDev         Pointer to PCI device. Use pPciDev->pDevIns to get the device instance.
 * @param   uAddress        The configuration space register address. [0..4096]
 * @param   u32Value        The value that's being written. The number of bits actually used from
 *                          this value is determined by the cb parameter.
 * @param   cb              The register size. [1,2,4]
 *
 * @remarks Called with the PDM lock held.  The device lock is NOT take because
 *          that is very likely be a lock order violation.
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioPciConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                        uint32_t uAddress, uint32_t u32Value, unsigned cb)
{
    PVIRTIOSTATE pVirtio = *PDMINS_2_DATA(pDevIns, PVIRTIOSTATE *);

    if (uAddress == pVirtio->uPciCfgDataOff)
    {
        /* VirtIO 1.0 spec section 4.1.4.7 describes a required alternative access capability
         * whereby the guest driver can specify a bar, offset, and length via the PCI configuration space
         * (the virtio_pci_cfg_cap capability), and access data items. */
        uint32_t uLength = pVirtio->pPciCfgCap->pciCap.uLength;
        uint32_t uOffset = pVirtio->pPciCfgCap->pciCap.uOffset;
        uint8_t  uBar    = pVirtio->pPciCfgCap->pciCap.uBar;
        if (uBar == pVirtio->uVirtioCapBar)
            (void)virtioR3MmioWrite(pDevIns, NULL, (RTGCPHYS)((uint32_t)pVirtio->GCPhysPciCapBase + uOffset),
                                    (void *)&u32Value, uLength);
        else
        {
            LogFunc(("Guest wrote virtio_pci_cfg_cap.pci_cfg_data using unconfigured BAR. Ignoring"));
            return VINF_SUCCESS;
        }
        LogFunc(("Guest wrote  virtio_pci_cfg_cap.pci_cfg_data, bar=%d, offset=%x, length=%x, value=%d\n",
                uBar, uOffset, uLength, u32Value));
        return VINF_SUCCESS;
    }
    return pVirtio->pfnPciConfigWriteOld(pDevIns, pPciDev, uAddress, u32Value, cb);
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
void *virtioQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    VIRTIOSTATE *pThis = IFACE_TO_STATE(pInterface, IBase);
    Assert(&pThis->IBase == pInterface);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThis->IBase);
    return NULL;
}

/**
 * Get VirtIO accepted host-side features
 *
 * @returns feature bits selected or 0 if selector out of range.
 *
 * @param   pState          Virtio state
 * @param   uSelect         Selects which 32-bit set of feature information to return
 */

__attribute__((unused))
static uint64_t virtioGetHostFeatures(PVIRTIOSTATE pVirtio)
{
    return pVirtio->uDriverFeatures;
}

/**
 *
 * Set VirtIO available host-side features
 *
 * @returns VBox status code
 *
 * @param   pState              Virtio state
 * @param   uDeviceFeatures    Feature bits (0-63) to set
 */


void virtioSetHostFeatures(VIRTIOHANDLE hVirtio, uint64_t uDeviceFeatures)
{
    H2P(hVirtio)->uDeviceFeatures = VIRTIO_F_VERSION_1 | uDeviceFeatures;
}

/**
 * Destruct PCI-related part of device.
 *
 * We need to free non-VM resources only.
 *
 * @returns VBox status code.
 * @param   pState      The device state structure.
 */
int virtioDestruct(VIRTIOSTATE* pState)
{
    Log(("%s Destroying PCI instance\n", INSTANCE(pState)));
    return VINF_SUCCESS;
}

/** PK (temp note to self):
 *
 *  Device needs to negotiate capabilities,
 *  then get queue size address information from driver.
 *
 * Still need consumer to pass in:
 *
 *  num_queues
 *  config_generation
 *  Needs to manage feature negotiation
 *  That means consumer needs to pass in device-specific feature bits/values
 *  Device has to provie at least one notifier capability
 *
 *  ISR config value are set by the device (config interrupt vs. queue interrupt)
 *
 */

/**
 * Setup PCI device controller and Virtio state
 *
 * @param   pDevIns               Device instance data
 * @param   pVirtio               Device State
 * @param   iInstance             Instance number
 * @param   pPciParams            Values to populate industry standard PCI Configuration Space data structure
 * @param   pcszNameFmt           Device instance name (format-specifier)
 * @param   uNumQueues               Number of Virtio Queues created by consumer (driver)
 * @param   uVirtioRegion         Region number to map for PCi Capabilities structs
 * @param   devCapReadCallback    Client function to call back to handle device specific capabilities
 * @param   devCapWriteCallback   Client function to call back to handle device specific capabilities
 * @param   cbDevSpecificCap      Size of device specific struct
 * @param   uNotifyOffMultiplier  See VirtIO 1.0 spec 4.1.4.4 re: virtio_pci_notify_cap
 */

int   virtioConstruct(PPDMDEVINS pDevIns, PVIRTIOHANDLE phVirtio, int iInstance, PVIRTIOPCIPARAMS pPciParams,
                    const char *pcszNameFmt, uint32_t uNumQueues, uint32_t uVirtioCapBar, uint64_t uDeviceFeatures,
                    PFNVIRTIODEVCAPREAD devCapReadCallback, PFNVIRTIODEVCAPWRITE devCapWriteCallback,
                    uint16_t cbDevSpecificCap, void *pDevSpecificCap,  uint32_t uNotifyOffMultiplier)
{

    int rc = VINF_SUCCESS;

    PVIRTIOSTATE pVirtio = (PVIRTIOSTATE)RTMemAlloc(sizeof(VIRTIOSTATE));
    if (!pVirtio)
    {
        PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY, N_("virtio: out of memory"));
        return VERR_NO_MEMORY;
    }


    pVirtio->uNumQueues = uNumQueues;
    pVirtio->uNotifyOffMultiplier = uNotifyOffMultiplier;
    pVirtio->uDeviceFeatures = VIRTIO_F_VERSION_1 | uDeviceFeatures;

    /* Init handles and log related stuff. */
    RTStrPrintf(pVirtio->szInstance, sizeof(pVirtio->szInstance), pcszNameFmt, iInstance);

    pVirtio->pDevInsR3 = pDevIns;
    pVirtio->pDevInsR0 = PDMDEVINS_2_R0PTR(pDevIns);
    pVirtio->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    pVirtio->uDeviceStatus = 0;
    pVirtio->cbDevSpecificCap = cbDevSpecificCap;
    pVirtio->pDevSpecificCap = pDevSpecificCap;
    /**
     * Need to keep a history of this relatively small virtio device-specific
     * configuration buffer, which is opaque to this encapsulation of the generic
     * part virtio operations, to track config changes to fields, in order to
     * update the configuration generation each change. (See VirtIO 1.0 section 4.1.4.3.1)
     */
    pVirtio->pPrevDevSpecificCap = RTMemAlloc(cbDevSpecificCap);
    if (!pVirtio->pPrevDevSpecificCap)
    {
        RTMemFree(pVirtio);
        PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY, N_("virtio: out of memory"));
        return VERR_NO_MEMORY;
    }
    memcpy(pVirtio->pPrevDevSpecificCap, pVirtio->pDevSpecificCap, cbDevSpecificCap);
    pVirtio->uVirtioCapBar = uVirtioCapBar;
    pVirtio->virtioCallbacks.pfnVirtioDevCapRead = devCapReadCallback;
    pVirtio->virtioCallbacks.pfnVirtioDevCapWrite = devCapWriteCallback;

    /* Set PCI config registers (assume 32-bit mode) */
    PCIDevSetRevisionId        (&pVirtio->dev, DEVICE_PCI_REVISION_ID_VIRTIO);
    PCIDevSetVendorId          (&pVirtio->dev, DEVICE_PCI_VENDOR_ID_VIRTIO);
    PCIDevSetSubSystemVendorId (&pVirtio->dev, DEVICE_PCI_VENDOR_ID_VIRTIO);
    PCIDevSetDeviceId          (&pVirtio->dev, pPciParams->uDeviceId);
    PCIDevSetClassBase         (&pVirtio->dev, pPciParams->uClassBase);
    PCIDevSetClassSub          (&pVirtio->dev, pPciParams->uClassSub);
    PCIDevSetClassProg         (&pVirtio->dev, pPciParams->uClassProg);
    PCIDevSetSubSystemId       (&pVirtio->dev, pPciParams->uSubsystemId);
    PCIDevSetInterruptLine     (&pVirtio->dev, pPciParams->uInterruptLine);
    PCIDevSetInterruptPin      (&pVirtio->dev, pPciParams->uInterruptPin);

    /* Register PCI device */
    rc = PDMDevHlpPCIRegister(pDevIns, &pVirtio->dev);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pVirtio);
        return PDMDEV_SET_ERROR(pDevIns, rc,
            N_("virtio: cannot register PCI Device")); /* can we put params in this error? */
    }

    pVirtio->IBase = pDevIns->IBase;

    PDMDevHlpPCISetConfigCallbacks(pDevIns, &pVirtio->dev,
                virtioPciConfigRead,  &pVirtio->pfnPciConfigReadOld,
                virtioPciConfigWrite, &pVirtio->pfnPciConfigWriteOld);

    /** Construct & map PCI vendor-specific capabilities for virtio host negotiation with guest driver */

#if 0 && defined(VBOX_WITH_MSI_DEVICES)  /* T.B.D. */
    uint8_t fMsiSupport = true;
#else
    uint8_t fMsiSupport = false;
#endif

    /* The following capability mapped via VirtIO 1.0: struct virtio_pci_cfg_cap (VIRTIO_PCI_CFG_CAP_T)
     * as a mandatory but suboptimal alternative interface to host device capabilities, facilitating
     * access the memory of any BAR. If the guest uses it (the VirtIO driver on Linux doesn't),
     * Unlike Common, Notify, ISR and Device capabilities, it is accessed directly via PCI Config region.
     * therefore does not contribute to the capabilities region (BAR) the other capabilities use.
     */
#define CFGADDR2IDX(addr) ((uint64_t)addr - (uint64_t)&pVirtio->dev.abConfig)

    PVIRTIO_PCI_CAP_T pCfg;
    uint32_t cbRegion = 0;

    /* Common capability (VirtIO 1.0 spec, section 4.1.4.3) */
    pCfg = (PVIRTIO_PCI_CAP_T)&pVirtio->dev.abConfig[0x40];
    pCfg->uCfgType = VIRTIO_PCI_CAP_COMMON_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
    pCfg->uCapNext = CFGADDR2IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = uVirtioCapBar;
    pCfg->uOffset  = 0;
    pCfg->uLength  = sizeof(VIRTIO_PCI_COMMON_CFG_T);
    cbRegion += pCfg->uLength;
    pVirtio->pCommonCfgCap = pCfg;

    /* Notify capability (VirtIO 1.0 spec, section 4.1.4.4). Note: uLength is based on assumption
     * that each queue's uQueueNotifyOff is set equal to uQueueSelect's ordinal
     * value of the queue */
    pCfg = (PVIRTIO_PCI_CAP_T)&pVirtio->dev.abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_NOTIFY_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_NOTIFY_CAP_T);
    pCfg->uCapNext = CFGADDR2IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = uVirtioCapBar;
    pCfg->uOffset  = pVirtio->pCommonCfgCap->uOffset + pVirtio->pCommonCfgCap->uLength;
    pCfg->uLength  = VIRTIO_MAX_QUEUES * uNotifyOffMultiplier + 2;  /* will change in VirtIO 1.1 */
    cbRegion += pCfg->uLength;
    pVirtio->pNotifyCap = (PVIRTIO_PCI_NOTIFY_CAP_T)pCfg;
    pVirtio->pNotifyCap->uNotifyOffMultiplier = uNotifyOffMultiplier;

    /* ISR capability (VirtIO 1.0 spec, section 4.1.4.5) */
    pCfg = (PVIRTIO_PCI_CAP_T)&pVirtio->dev.abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_ISR_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
    pCfg->uCapNext = CFGADDR2IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = uVirtioCapBar;
    pCfg->uOffset  = pVirtio->pNotifyCap->pciCap.uOffset + pVirtio->pNotifyCap->pciCap.uLength;
    pCfg->uLength  = sizeof(uint32_t);
    cbRegion += pCfg->uLength;
    pVirtio->pIsrCap = pCfg;

    /* PCI Cfg capability (VirtIO 1.0 spec, section 4.1.4.7) */
    pCfg = (PVIRTIO_PCI_CAP_T)&pVirtio->dev.abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_PCI_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CFG_CAP_T);
    pCfg->uCapNext = (fMsiSupport || pVirtio->pDevSpecificCap) ? CFGADDR2IDX(pCfg) + pCfg->uCapLen : 0;
    pCfg->uBar     = uVirtioCapBar;
    pCfg->uOffset  = pVirtio->pIsrCap->uOffset + pVirtio->pIsrCap->uLength;
    pCfg->uLength  = 4;  /* Initialize a non-zero 4-byte aligned so Linux virtio_pci module recognizes this cap */
    cbRegion += pCfg->uLength;
    pVirtio->pPciCfgCap = (PVIRTIO_PCI_CFG_CAP_T)pCfg;

    if (pVirtio->pDevSpecificCap)
    {
        /* Following capability (via VirtIO 1.0, section 4.1.4.6). Client defines the
         * device specific configuration struct and passes its params to this constructor */
        pCfg = (PVIRTIO_PCI_CAP_T)&pVirtio->dev.abConfig[pCfg->uCapNext];
        pCfg->uCfgType = VIRTIO_PCI_CAP_DEVICE_CFG;
        pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
        pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
        pCfg->uCapNext = fMsiSupport ? CFGADDR2IDX(pCfg) + pCfg->uCapLen : 0;
        pCfg->uBar     = uVirtioCapBar;
        pCfg->uOffset  = pVirtio->pIsrCap->uOffset + pVirtio->pIsrCap->uLength;
        pCfg->uLength  = cbDevSpecificCap;
        cbRegion += pCfg->uLength;
        pVirtio->pDeviceCap = pCfg;
    }

    /* Set offset to first capability and enable PCI dev capabilities */
    PCIDevSetCapabilityList    (&pVirtio->dev, 0x40);
    PCIDevSetStatus            (&pVirtio->dev, VBOX_PCI_STATUS_CAP_LIST);

    if (fMsiSupport)
    {
        PDMMSIREG aMsiReg;
        RT_ZERO(aMsiReg);
        aMsiReg.iMsixCapOffset  = pCfg->uCapNext;
        aMsiReg.iMsixNextOffset = 0;
        aMsiReg.iMsixBar        = 0;
        aMsiReg.cMsixVectors    = 1;
        rc = PDMDevHlpPCIRegisterMsi(pDevIns, &aMsiReg); /* see MsixR3init() */
        if (RT_FAILURE (rc))
            /* PK TODO: The following is moot, we need to flag no MSI-X support */
            PCIDevSetCapabilityList(&pVirtio->dev, 0x40);
    }

    Log(("cbRegion = %d (0x%x)\n", cbRegion, cbRegion));
    rc = PDMDevHlpPCIIORegionRegister(pDevIns, uVirtioCapBar, cbRegion,
                                      PCI_ADDRESS_SPACE_MEM, virtioR3Map);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pVirtio->pPrevDevSpecificCap);
        RTMemFree(pVirtio);
        return PDMDEV_SET_ERROR(pDevIns, rc,
            N_("virtio: cannot register PCI Capabilities address space"));
    }
    *phVirtio = (PVIRTIOHANDLE)pVirtio;
    return rc;
}
#endif /* IN_RING3 */

