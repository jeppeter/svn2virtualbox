/* $Revision$ */
/** @file
 * VBoxGuestLib - Host-Guest Communication Manager internal functions, implemented by VBoxGuest
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

/* Entire file is ifdef'ed with VBGL_VBOXGUEST */
#ifdef VBGL_VBOXGUEST

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "VBGLInternal.h"
#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The max parameter buffer size for a user request. */
#define VBGLR0_MAX_HGCM_USER_PARM       _1M
/** The max parameter buffer size for a kernel request. */
#define VBGLR0_MAX_HGCM_KERNEL_PARM     (16*_1M)
#ifdef RT_OS_LINUX
/** Linux needs to use bounce buffers since RTR0MemObjLockUser has unwanted
 *  side effects. */
# define USE_BOUNCH_BUFFERS
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Lock info structure used by VbglR0HGCMInternalCall and its helpers.
 */
struct VbglR0ParmInfo
{
    uint32_t cLockBufs;
    struct
    {
        uint32_t    iParm;
        RTR0MEMOBJ  hObj;
#ifdef USE_BOUNCH_BUFFERS
        void       *pvSmallBuf;
#endif
    } aLockBufs[10];
};



/* These functions can be only used by VBoxGuest. */

DECLVBGL(int) VbglR0HGCMInternalConnect (VBoxGuestHGCMConnectInfo *pConnectInfo,
                                         PFNVBGLHGCMCALLBACK pfnAsyncCallback, void *pvAsyncData, uint32_t u32AsyncData)
{
    VMMDevHGCMConnect *pHGCMConnect;
    int rc;

    if (!pConnectInfo || !pfnAsyncCallback)
        return VERR_INVALID_PARAMETER;

    pHGCMConnect = NULL;

    /* Allocate request */
    rc = VbglGRAlloc ((VMMDevRequestHeader **)&pHGCMConnect, sizeof (VMMDevHGCMConnect), VMMDevReq_HGCMConnect);

    if (RT_SUCCESS(rc))
    {
        /* Initialize request memory */
        pHGCMConnect->header.fu32Flags = 0;

        memcpy (&pHGCMConnect->loc, &pConnectInfo->Loc, sizeof (HGCMServiceLocation));
        pHGCMConnect->u32ClientID = 0;

        /* Issue request */
        rc = VbglGRPerform (&pHGCMConnect->header.header);

        if (RT_SUCCESS(rc))
        {
            /* Check if host decides to process the request asynchronously. */
            if (rc == VINF_HGCM_ASYNC_EXECUTE)
            {
                /* Wait for request completion interrupt notification from host */
                pfnAsyncCallback (&pHGCMConnect->header, pvAsyncData, u32AsyncData);
            }

            pConnectInfo->result = pHGCMConnect->header.result;

            if (RT_SUCCESS (pConnectInfo->result))
                pConnectInfo->u32ClientID = pHGCMConnect->u32ClientID;
        }

        VbglGRFree (&pHGCMConnect->header.header);
    }

    return rc;
}


DECLR0VBGL(int) VbglR0HGCMInternalDisconnect (VBoxGuestHGCMDisconnectInfo *pDisconnectInfo,
                                              PFNVBGLHGCMCALLBACK pfnAsyncCallback, void *pvAsyncData, uint32_t u32AsyncData)
{
    VMMDevHGCMDisconnect *pHGCMDisconnect;
    int rc;

    if (!pDisconnectInfo || !pfnAsyncCallback)
        return VERR_INVALID_PARAMETER;

    pHGCMDisconnect = NULL;

    /* Allocate request */
    rc = VbglGRAlloc ((VMMDevRequestHeader **)&pHGCMDisconnect, sizeof (VMMDevHGCMDisconnect), VMMDevReq_HGCMDisconnect);

    if (RT_SUCCESS(rc))
    {
        /* Initialize request memory */
        pHGCMDisconnect->header.fu32Flags = 0;

        pHGCMDisconnect->u32ClientID = pDisconnectInfo->u32ClientID;

        /* Issue request */
        rc = VbglGRPerform (&pHGCMDisconnect->header.header);

        if (RT_SUCCESS(rc))
        {
            /* Check if host decides to process the request asynchronously. */
            if (rc == VINF_HGCM_ASYNC_EXECUTE)
            {
                /* Wait for request completion interrupt notification from host */
                pfnAsyncCallback (&pHGCMDisconnect->header, pvAsyncData, u32AsyncData);
            }

            pDisconnectInfo->result = pHGCMDisconnect->header.result;
        }

        VbglGRFree (&pHGCMDisconnect->header.header);
    }

    return rc;
}


/**
 * Preprocesses the HGCM call, validating and locking/buffering parameters.
 *
 * @returns VBox status code.
 *
 * @param   pCallInfo       The call info.
 * @param   cbCallInfo      The size of the call info structure.
 * @param   fIsUser         Is it a user request or kernel request.
 * @param   pcbExtra        Where to return the extra request space needed for
 *                          physical page lists.
 */
static int vbglR0HGCMInternalPreprocessCall(VBoxGuestHGCMCallInfo const *pCallInfo, uint32_t cbCallInfo,
                                            bool fIsUser, struct VbglR0ParmInfo *pParmInfo,  size_t *pcbExtra)
{
    HGCMFunctionParameter const *pSrcParm = VBOXGUEST_HGCM_CALL_PARMS(pCallInfo);
    uint32_t    cParms = pCallInfo->cParms;
    uint32_t    iParm;
    uint32_t    cb;

    /*
     * Lock down the any linear buffers so we can get their addresses
     * and figure out how much extra storage we need for page lists.
     *
     * Note! With kernel mode users we can be assertive. For user mode users
     *       we should just (debug) log it and fail without any fanfare.
     */
    *pcbExtra = 0;
    pParmInfo->cLockBufs = 0;
    for (iParm = 0; iParm < cParms; iParm++, pSrcParm++)
    {
        switch (pSrcParm->type)
        {
            case VMMDevHGCMParmType_32bit:
                Log4(("GstHGCMCall: parm=%u type=32bit: %#010x\n", iParm, pSrcParm->u.value32));
                break;

            case VMMDevHGCMParmType_64bit:
                Log4(("GstHGCMCall: parm=%u type=64bit: %#018x\n", iParm, pSrcParm->u.value64));
                break;

            case VMMDevHGCMParmType_PageList:
                if (fIsUser)
                    return VERR_INVALID_PARAMETER;
                cb = pSrcParm->u.PageList.size;
                if (cb)
                {
                    uint32_t            off = pSrcParm->u.PageList.offset;
                    HGCMPageListInfo   *pPgLst;
                    uint32_t            cPages;
                    uint32_t            u32;

                    AssertMsgReturn(cb <= VBGLR0_MAX_HGCM_KERNEL_PARM, ("%#x > %#x\n", cb, VBGLR0_MAX_HGCM_KERNEL_PARM),
                                    VERR_OUT_OF_RANGE);
                    AssertMsgReturn(   off >= pCallInfo->cParms * sizeof(HGCMFunctionParameter)
                                    && off <= cbCallInfo - sizeof(HGCMPageListInfo),
                                    ("offset=%#x cParms=%#x cbCallInfo=%#x\n", off, pCallInfo->cParms, cbCallInfo),
                                    VERR_INVALID_PARAMETER);

                    pPgLst = (HGCMPageListInfo *)((uint8_t *)pCallInfo + off);
                    cPages = pPgLst->cPages;
                    u32    = RT_OFFSETOF(HGCMPageListInfo, aPages[cPages]) + off;
                    AssertMsgReturn(u32 <= cbCallInfo,
                                    ("u32=%#x (cPages=%#x offset=%#x) cbCallInfo=%#x\n", u32, cPages, off, cbCallInfo),
                                    VERR_INVALID_PARAMETER);
                    AssertMsgReturn(pPgLst->offFirstPage < PAGE_SIZE, ("#x\n", pPgLst->offFirstPage), VERR_INVALID_PARAMETER);
                    u32 = RT_ALIGN_32(pPgLst->offFirstPage + cb, PAGE_SIZE) >> PAGE_SHIFT;
                    AssertMsgReturn(cPages == u32, ("cPages=%#x u32=%#x\n", cPages, u32), VERR_INVALID_PARAMETER);
                    AssertMsgReturn(VBOX_HGCM_F_PARM_ARE_VALID(pPgLst->flags), ("%#x\n", pPgLst->flags), VERR_INVALID_PARAMETER);
                    Log4(("GstHGCMCall: parm=%u type=pglst: cb=%#010x cPgs=%u offPg0=%#x flags=%#x\n",
                          iParm, cb, cPages, pPgLst->offFirstPage, pPgLst->flags));
                    u32 = cPages;
                    while (u32-- > 0)
                    {
                        Log4(("GstHGCMCall:   pg#%u=%RHp\n", u32, pPgLst->aPages[u32]));
                        AssertMsgReturn(!(pPgLst->aPages[u32] & (PAGE_OFFSET_MASK | UINT64_C(0xfff0000000000000))),
                                        ("pg#%u=%RHp\n", u32, pPgLst->aPages[u32]),
                                        VERR_INVALID_PARAMETER);
                    }

                    *pcbExtra += RT_OFFSETOF(HGCMPageListInfo, aPages[pPgLst->cPages]);
                }
                else
                    Log4(("GstHGCMCall: parm=%u type=pglst: cb=0\n", iParm));
                break;

            case VMMDevHGCMParmType_LinAddr_Locked_In:
            case VMMDevHGCMParmType_LinAddr_Locked_Out:
            case VMMDevHGCMParmType_LinAddr_Locked:
                if (fIsUser)
                    return VERR_INVALID_PARAMETER;
                if (!VBGLR0_CAN_USE_PHYS_PAGE_LIST())
                {
                    cb = pSrcParm->u.Pointer.size;
                    AssertMsgReturn(cb <= VBGLR0_MAX_HGCM_KERNEL_PARM, ("%#x > %#x\n", cb, VBGLR0_MAX_HGCM_KERNEL_PARM),
                                    VERR_OUT_OF_RANGE);
                    if (cb != 0)
                        Log4(("GstHGCMCall: parm=%u type=%#x: cb=%#010x pv=%p\n",
                              iParm, pSrcParm->type, cb, pSrcParm->u.Pointer.u.linearAddr));
                    else
                        Log4(("GstHGCMCall: parm=%u type=%#x: cb=0\n", iParm, pSrcParm->type));
                    break;
                }
                /* fall thru */

            case VMMDevHGCMParmType_LinAddr_In:
            case VMMDevHGCMParmType_LinAddr_Out:
            case VMMDevHGCMParmType_LinAddr:
                cb = pSrcParm->u.Pointer.size;
                if (cb != 0)
                {
#ifdef USE_BOUNCH_BUFFERS
                    void       *pvSmallBuf = NULL;
#endif
                    uint32_t    iLockBuf   = pParmInfo->cLockBufs;
                    RTR0MEMOBJ  hObj;
                    int         rc;

                    AssertReturn(iLockBuf < RT_ELEMENTS(pParmInfo->aLockBufs), VERR_INVALID_PARAMETER);
                    if (!fIsUser)
                    {
                        AssertMsgReturn(cb <= VBGLR0_MAX_HGCM_KERNEL_PARM, ("%#x > %#x\n", cb, VBGLR0_MAX_HGCM_KERNEL_PARM),
                                        VERR_OUT_OF_RANGE);
                        rc = RTR0MemObjLockKernel(&hObj, (void *)pSrcParm->u.Pointer.u.linearAddr, cb);
                        if (RT_FAILURE(rc))
                        {
                            Log(("GstHGCMCall: id=%#x fn=%u parm=%u RTR0MemObjLockKernel(,%p,%#x) -> %Rrc\n",
                                 pCallInfo->u32ClientID, pCallInfo->u32Function, iParm, pSrcParm->u.Pointer.u.linearAddr, cb, rc));
                            return rc;
                        }
                        Log3(("GstHGCMCall: parm=%u type=%#x: cb=%#010x pv=%p locked kernel -> %p\n",
                              iParm, pSrcParm->type, cb, pSrcParm->u.Pointer.u.linearAddr, hObj));
                    }
                    else
                    {
                        if (cb > VBGLR0_MAX_HGCM_USER_PARM)
                        {
                            Log(("GstHGCMCall: id=%#x fn=%u parm=%u pv=%p cb=%#x > %#x -> out of range\n",
                                 pCallInfo->u32ClientID, pCallInfo->u32Function, iParm, pSrcParm->u.Pointer.u.linearAddr,
                                 cb, VBGLR0_MAX_HGCM_USER_PARM));
                            return VERR_OUT_OF_RANGE;
                        }

#ifndef USE_BOUNCH_BUFFERS
                        rc = RTR0MemObjLockUser(&hObj, (RTR3PTR)pSrcParm->u.Pointer.u.linearAddr, cb, NIL_RTR0PROCESS);
                        if (RT_FAILURE(rc))
                        {
                            Log(("GstHGCMCall: id=%#x fn=%u parm=%u RTR0MemObjLockUser(,%p,%#x,nil) -> %Rrc\n",
                                 pCallInfo->u32ClientID, pCallInfo->u32Function, iParm, pSrcParm->u.Pointer.u.linearAddr, cb, rc));
                            return rc;
                        }
                        Log3(("GstHGCMCall: parm=%u type=%#x: cb=%#010x pv=%p locked user -> %p\n",
                              iParm, pSrcParm->type, cb, pSrcParm->u.Pointer.u.linearAddr, hObj));

#else  /* USE_BOUNCH_BUFFERS */
                        /*
                         * This is a bit massive, but we don't want to waste a
                         * whole page for a 3 byte string buffer (guest props).
                         *
                         * The threshold is ASSUMING sizeof(RTMEMHDR) == 16 and
                         * the system is using some power of two allocator.
                         */
                        /** @todo A more efficient strategy would be to combine buffers. However it
                         *        is probably going to be more massive than the current code, so
                         *        it can wait till later.   */
                        bool fCopyIn = pSrcParm->type != VMMDevHGCMParmType_LinAddr_Out
                                    && pSrcParm->type != VMMDevHGCMParmType_LinAddr_Locked_Out;
                        if (cb <= PAGE_SIZE / 2 - 16)
                        {
                            pvSmallBuf = fCopyIn ? RTMemTmpAlloc(cb) : RTMemTmpAllocZ(cb);
                            if (RT_UNLIKELY(!pvSmallBuf))
                                return VERR_NO_MEMORY;
                            if (fCopyIn)
                            {
                                rc = RTR0MemUserCopyFrom(pvSmallBuf, pSrcParm->u.Pointer.u.linearAddr, cb);
                                if (RT_FAILURE(rc))
                                {
                                    RTMemTmpFree(pvSmallBuf);
                                    Log(("GstHGCMCall: id=%#x fn=%u parm=%u RTR0MemUserCopyFrom(,%p,%#x) -> %Rrc\n",
                                         pCallInfo->u32ClientID, pCallInfo->u32Function, iParm,
                                         pSrcParm->u.Pointer.u.linearAddr, cb, rc));
                                    return rc;
                                }
                            }
                            rc = RTR0MemObjLockKernel(&hObj, pvSmallBuf, cb);
                            if (RT_FAILURE(rc))
                            {
                                RTMemTmpFree(pvSmallBuf);
                                Log(("GstHGCMCall: RTR0MemObjLockKernel failed for small buffer: rc=%Rrc pvSmallBuf=%p cb=%#x\n",
                                     rc, pvSmallBuf, cb));
                                return rc;
                            }
                            Log3(("GstHGCMCall: parm=%u type=%#x: cb=%#010x pv=%p small buffer %p -> %p\n",
                                  iParm, pSrcParm->type, cb, pSrcParm->u.Pointer.u.linearAddr, pvSmallBuf, hObj));
                        }
                        else
                        {
                            rc = RTR0MemObjAllocPage(&hObj, cb, false /*fExecutable*/);
                            if (RT_FAILURE(rc))
                                return rc;
                            if (!fCopyIn)
                                memset(RTR0MemObjAddress(hObj), '\0', cb);
                            else
                            {
                                rc = RTR0MemUserCopyFrom(RTR0MemObjAddress(hObj), pSrcParm->u.Pointer.u.linearAddr, cb);
                                if (RT_FAILURE(rc))
                                {
                                    RTR0MemObjFree(hObj, false /*fFreeMappings*/);
                                    Log(("GstHGCMCall: id=%#x fn=%u parm=%u RTR0MemUserCopyFrom(,%p,%#x) -> %Rrc\n",
                                         pCallInfo->u32ClientID, pCallInfo->u32Function, iParm,
                                         pSrcParm->u.Pointer.u.linearAddr, cb, rc));
                                    return rc;
                                }
                            }
                            Log3(("GstHGCMCall: parm=%u type=%#x: cb=%#010x pv=%p big buffer -> %p\n",
                                  iParm, pSrcParm->type, cb, pSrcParm->u.Pointer.u.linearAddr, hObj));
                        }
#endif /* USE_BOUNCH_BUFFERS */
                    }

                    pParmInfo->aLockBufs[iLockBuf].iParm      = iParm;
                    pParmInfo->aLockBufs[iLockBuf].hObj       = hObj;
#ifdef USE_BOUNCH_BUFFERS
                    pParmInfo->aLockBufs[iLockBuf].pvSmallBuf = pvSmallBuf;
#endif
                    pParmInfo->cLockBufs = iLockBuf + 1;

                    if (VBGLR0_CAN_USE_PHYS_PAGE_LIST())
                    {
                        size_t cPages = RTR0MemObjSize(hObj);
                        *pcbExtra += RT_OFFSETOF(HGCMPageListInfo, aPages[cPages]);
                    }
                }
                else
                    Log4(("GstHGCMCall: parm=%u type=%#x: cb=0\n", iParm, pSrcParm->type));
                break;

            default:
                return VERR_INVALID_PARAMETER;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Translates locked linear address to the normal type.
 * The locked types are only for the guest side and not handled by the host.
 *
 * @returns normal linear address type.
 * @param   enmType     The type.
 */
static HGCMFunctionParameterType vbglR0HGCMInternalConvertLinAddrType(HGCMFunctionParameterType enmType)
{
    switch (enmType)
    {
        case VMMDevHGCMParmType_LinAddr_Locked_In:
            return VMMDevHGCMParmType_LinAddr_In;
        case VMMDevHGCMParmType_LinAddr_Locked_Out:
            return VMMDevHGCMParmType_LinAddr_Out;
        case VMMDevHGCMParmType_LinAddr_Locked:
            return VMMDevHGCMParmType_LinAddr;
        default:
            return enmType;
    }
}


/**
 * Translates linear address types to page list direction flags.
 *
 * @returns page list flags.
 * @param   enmType     The type.
 */
static uint32_t vbglR0HGCMInternalLinAddrTypeToPageListFlags(HGCMFunctionParameterType enmType)
{
    switch (enmType)
    {
        case VMMDevHGCMParmType_LinAddr_In:
        case VMMDevHGCMParmType_LinAddr_Locked_In:
            return VBOX_HGCM_F_PARM_DIRECTION_TO_HOST;

        case VMMDevHGCMParmType_LinAddr_Out:
        case VMMDevHGCMParmType_LinAddr_Locked_Out:
            return VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST;

        default: AssertFailed();
        case VMMDevHGCMParmType_LinAddr:
        case VMMDevHGCMParmType_LinAddr_Locked:
            return VBOX_HGCM_F_PARM_DIRECTION_BOTH;
    }
}


/**
 * Initializes the call request that we're sending to the host.
 *
 * @returns VBox status code.
 *
 * @param   pCallInfo       The call info.
 * @param   cbCallInfo      The size of the call info structure.
 * @param   fIsUser         Is it a user request or kernel request.
 * @param   pcbExtra        Where to return the extra request space needed for
 *                          physical page lists.
 */
static void vbglR0HGCMInternalInitCall(VMMDevHGCMCall *pHGCMCall, VBoxGuestHGCMCallInfo const *pCallInfo,
                                       uint32_t cbCallInfo, bool fIsUser, struct VbglR0ParmInfo *pParmInfo)
{
    HGCMFunctionParameter const *pSrcParm = VBOXGUEST_HGCM_CALL_PARMS(pCallInfo);
    HGCMFunctionParameter       *pDstParm = VMMDEV_HGCM_CALL_PARMS(pHGCMCall);
    uint32_t    cParms   = pCallInfo->cParms;
    uint32_t    offExtra = (uintptr_t)(pDstParm + cParms) - (uintptr_t)pHGCMCall;
    uint32_t    iLockBuf = 0;
    uint32_t    iParm;


    /*
     * The call request headers.
     */
    pHGCMCall->header.fu32Flags = 0;
    pHGCMCall->header.result    = VINF_SUCCESS;

    pHGCMCall->u32ClientID = pCallInfo->u32ClientID;
    pHGCMCall->u32Function = pCallInfo->u32Function;
    pHGCMCall->cParms      = cParms;

    /*
     * The parameters.
     */
    for (iParm = 0; iParm < pCallInfo->cParms; iParm++, pSrcParm++, pDstParm++)
    {
        switch (pSrcParm->type)
        {
            case VMMDevHGCMParmType_32bit:
            case VMMDevHGCMParmType_64bit:
                *pDstParm = *pSrcParm;
                break;

            case VMMDevHGCMParmType_PageList:
                pDstParm->type = VMMDevHGCMParmType_PageList;
                pDstParm->u.PageList.size = pSrcParm->u.PageList.size;
                if (pSrcParm->u.PageList.size)
                {
                    HGCMPageListInfo const *pSrcPgLst = (HGCMPageListInfo *)((uint8_t *)pCallInfo + pSrcParm->u.PageList.offset);
                    HGCMPageListInfo       *pDstPgLst = (HGCMPageListInfo *)((uint8_t *)pHGCMCall + offExtra);
                    uint32_t const          cPages    = pSrcPgLst->cPages;
                    uint32_t                iPage;

                    pDstParm->u.PageList.offset = offExtra;
                    pDstPgLst->flags            = pSrcPgLst->flags;
                    pDstPgLst->offFirstPage     = pSrcPgLst->offFirstPage;
                    pDstPgLst->cPages           = cPages;
                    for (iPage = 0; iPage < cPages; iPage++)
                        pDstPgLst->aPages[iPage] = pSrcPgLst->aPages[iPage];

                    offExtra += RT_OFFSETOF(HGCMPageListInfo, aPages[cPages]);
                }
                else
                    pDstParm->u.PageList.offset = 0;
                break;

            case VMMDevHGCMParmType_LinAddr_Locked_In:
            case VMMDevHGCMParmType_LinAddr_Locked_Out:
            case VMMDevHGCMParmType_LinAddr_Locked:
                if (!VBGLR0_CAN_USE_PHYS_PAGE_LIST())
                {
                    *pDstParm = *pSrcParm;
                    pDstParm->type = vbglR0HGCMInternalConvertLinAddrType(pSrcParm->type);
                    break;
                }
                /* fall thru */

            case VMMDevHGCMParmType_LinAddr_In:
            case VMMDevHGCMParmType_LinAddr_Out:
            case VMMDevHGCMParmType_LinAddr:
                if (pSrcParm->u.Pointer.size != 0)
                {
#ifdef USE_BOUNCH_BUFFERS
                    void      *pvSmallBuf = pParmInfo->aLockBufs[iLockBuf].pvSmallBuf;
#endif
                    RTR0MEMOBJ hObj       = pParmInfo->aLockBufs[iLockBuf].hObj;
                    Assert(iParm == pParmInfo->aLockBufs[iLockBuf].iParm);

                    if (VBGLR0_CAN_USE_PHYS_PAGE_LIST())
                    {
                        HGCMPageListInfo   *pDstPgLst = (HGCMPageListInfo *)((uint8_t *)pHGCMCall + offExtra);
                        size_t const        cPages    = RTR0MemObjSize(hObj) >> PAGE_SHIFT;
                        size_t              iPage;

                        pDstParm->type = VMMDevHGCMParmType_PageList;
                        pDstParm->u.PageList.size   = pSrcParm->u.Pointer.size;
                        pDstParm->u.PageList.offset = offExtra;
                        pDstPgLst->flags            = vbglR0HGCMInternalLinAddrTypeToPageListFlags(pSrcParm->type);
#ifdef USE_BOUNCH_BUFFERS
                        if (fIsUser)
                            pDstPgLst->offFirstPage = (uintptr_t)pvSmallBuf & PAGE_OFFSET_MASK;
                        else
#endif
                            pDstPgLst->offFirstPage = pSrcParm->u.Pointer.u.linearAddr & PAGE_OFFSET_MASK;
                        pDstPgLst->cPages           = cPages; Assert(pDstPgLst->cPages == cPages);
                        for (iPage = 0; iPage < cPages; iPage++)
                        {
                            pDstPgLst->aPages[iPage] = RTR0MemObjGetPagePhysAddr(hObj, iPage);
                            Assert(pDstPgLst->aPages[iPage] != NIL_RTHCPHYS);
                        }

                        offExtra += RT_OFFSETOF(HGCMPageListInfo, aPages[cPages]);
                    }
                    else
                    {
                        pDstParm->type = vbglR0HGCMInternalConvertLinAddrType(pSrcParm->type);
                        pDstParm->u.Pointer.size = pSrcParm->u.Pointer.size;
#ifdef USE_BOUNCH_BUFFERS
                        if (fIsUser)
                            pDstParm->u.Pointer.u.linearAddr = pvSmallBuf
                                                             ? (uintptr_t)pvSmallBuf
                                                             : (uintptr_t)RTR0MemObjAddress(hObj);
                        else
#endif
                            pDstParm->u.Pointer.u.linearAddr = pSrcParm->u.Pointer.u.linearAddr;
                    }
                    iLockBuf++;
                }
                else
                {
                    pDstParm->type = vbglR0HGCMInternalConvertLinAddrType(pSrcParm->type);
                    pDstParm->u.Pointer.size = 0;
                    pDstParm->u.Pointer.u.linearAddr = 0;
                }
                break;

            default:
                AssertFailed();
                pDstParm->type = VMMDevHGCMParmType_Invalid;
                break;
        }
    }
}


/**
 * Performs the call and completion wait.
 *
 * @returns VBox status code of this operation, not necessarily the call.
 *
 * @param   pHGCMCall           The HGCM call info.
 * @param   pfnAsyncCallback    The async callback that will wait for the call
 *                              to complete.
 * @param   pvAsyncData         Argument for the callback.
 * @param   u32AsyncData        Argument for the callback.
 * @param   pfLeakIt            Where to return the leak it / free it,
 *                              indicator. Cancellation fun.
 */
static int vbglR0HGCMInternalDoCall(VMMDevHGCMCall *pHGCMCall, PFNVBGLHGCMCALLBACK pfnAsyncCallback,
                                    void *pvAsyncData, uint32_t u32AsyncData, bool *pfLeakIt)
{
    int rc;

    Log(("calling VbglGRPerform\n"));
    rc = VbglGRPerform(&pHGCMCall->header.header);
    Log(("VbglGRPerform rc = %Rrc (header rc=%d)\n", rc, pHGCMCall->header.result));

    /*
     * If the call failed, but as a result of the request itself, then pretend
     * success. Upper layers will interpret the result code in the packet.
     */
    if (    RT_FAILURE(rc)
        &&  rc == pHGCMCall->header.result)
    {
        Assert(pHGCMCall->header.fu32Flags & VBOX_HGCM_REQ_DONE);
        rc = VINF_SUCCESS;
    }

    /*
     * Check if host decides to process the request asynchronously,
     * if so, we wait for it to complete using the caller supplied callback.
     */
    *pfLeakIt = false;
    if (rc == VINF_HGCM_ASYNC_EXECUTE)
    {
        Log(("Processing HGCM call asynchronously\n"));
        rc = pfnAsyncCallback(&pHGCMCall->header, pvAsyncData, u32AsyncData);
        if (pHGCMCall->header.fu32Flags & VBOX_HGCM_REQ_DONE)
        {
            Assert(!(pHGCMCall->header.fu32Flags & VBOX_HGCM_REQ_CANCELLED));
            rc = VINF_SUCCESS;
        }
        else
        {
            /*
             * The request didn't complete in time or the call was interrupted,
             * the RC from the callback indicates which. Try cancel the request.
             *
             * This is a bit messy because we're racing request completion. Sorry.
             */
            /** @todo It would be nice if we could use the waiter callback to do further
             *  waiting in case of a completion race. If it wasn't for WINNT having its own
             *  version of all that stuff, I would've done it already. */
            VMMDevHGCMCancel2 *pCancelReq;
            int rc2 = VbglGRAlloc((VMMDevRequestHeader **)&pCancelReq, sizeof(*pCancelReq), VMMDevReq_HGCMCancel2);
            if (RT_SUCCESS(rc2))
            {
                pCancelReq->physReqToCancel = VbglPhysHeapGetPhysAddr(pHGCMCall);
                rc2 = VbglGRPerform(&pCancelReq->header);
                VbglGRFree(&pCancelReq->header);
            }
#if 1 /** @todo ADDVER: Remove this on next minor version change. */
            if (rc2 == VERR_NOT_IMPLEMENTED)
            {
                /* host is too old, or we're out of heap. */
                pHGCMCall->header.fu32Flags |= VBOX_HGCM_REQ_CANCELLED;
                pHGCMCall->header.header.requestType = VMMDevReq_HGCMCancel;
                rc2 = VbglGRPerform(&pHGCMCall->header.header);
                if (rc2 == VERR_INVALID_PARAMETER)
                    rc2 = VERR_NOT_FOUND;
                else if (RT_SUCCESS(rc))
                    RTThreadSleep(1);
            }
#endif
            if (RT_SUCCESS(rc)) rc = VERR_INTERRUPTED; /** @todo weed this out from the WINNT VBoxGuest code. */
            if (RT_SUCCESS(rc2))
            {
                Log(("vbglR0HGCMInternalDoCall: successfully cancelled\n"));
                pHGCMCall->header.fu32Flags |= VBOX_HGCM_REQ_CANCELLED;
            }
            else
            {
                /*
                 * Wait for a bit while the host (hopefully) completes it.
                 */
                uint64_t u64Start       = RTTimeSystemMilliTS();
                uint32_t cMilliesToWait = rc2 == VERR_NOT_FOUND || rc2 == VERR_SEM_DESTROYED ? 500 : 2000;
                uint64_t cElapsed       = 0;
                if (rc2 != VERR_NOT_FOUND)
                    LogRel(("vbglR0HGCMInternalDoCall: Failed to cancel the HGCM call on %Rrc: rc2=%Rrc\n", rc, rc2));
                else
                    Log(("vbglR0HGCMInternalDoCall: Cancel race rc=%Rrc rc2=%Rrc\n", rc, rc2));

                do
                {
                    ASMCompilerBarrier();       /* paranoia */
                    if (pHGCMCall->header.fu32Flags & VBOX_HGCM_REQ_DONE)
                        break;
                    RTThreadSleep(1);
                    cElapsed = RTTimeSystemMilliTS() - u64Start;
                } while (cElapsed < cMilliesToWait);

                ASMCompilerBarrier();           /* paranoia^2 */
                if (pHGCMCall->header.fu32Flags & VBOX_HGCM_REQ_DONE)
                    rc = VINF_SUCCESS;
                else
                {
                    LogRel(("vbglR0HGCMInternalDoCall: Leaking %u bytes. Pending call to %u with %u parms. (rc2=%Rrc)\n",
                            pHGCMCall->header.header.size, pHGCMCall->u32Function, pHGCMCall->cParms, rc2));
                    *pfLeakIt = true;
                }
                Log(("vbglR0HGCMInternalDoCall: Cancel race ended with rc=%Rrc (rc2=%Rrc) after %llu ms\n", rc, rc2, cElapsed));
            }
        }
    }

    Log(("GstHGCMCall: rc=%Rrc result=%Rrc fu32Flags=%#x fLeakIt=%d\n",
         rc, pHGCMCall->header.result, pHGCMCall->header.fu32Flags, *pfLeakIt));
    return rc;
}


/**
 * Copies the result of the call back to the caller info structure and user
 * buffers (if using bounce buffers).
 *
 * @returns rc, unless RTR0MemUserCopyTo fails.
 * @param   pCallInfo           Call info structure to update.
 * @param   pHGCMCall           HGCM call request.
 * @param   pParmInfo           Paramter locking/buffering info.
 * @param   fIsUser             Is it a user (true) or kernel request.
 * @param   rc                  The current result code. Passed along to
 *                              preserve informational status codes.
 */
static int vbglR0HGCMInternalCopyBackResult(VBoxGuestHGCMCallInfo *pCallInfo, VMMDevHGCMCall const *pHGCMCall,
                                            struct VbglR0ParmInfo *pParmInfo, bool fIsUser, int rc)
{
    HGCMFunctionParameter const *pSrcParm = VMMDEV_HGCM_CALL_PARMS(pHGCMCall);
    HGCMFunctionParameter       *pDstParm = VBOXGUEST_HGCM_CALL_PARMS(pCallInfo);
    uint32_t    cParms   = pCallInfo->cParms;
#ifdef USE_BOUNCH_BUFFERS
    uint32_t    iLockBuf = 0;
#endif
    uint32_t    iParm;

    /*
     * The call result.
     */
    pCallInfo->result = pHGCMCall->header.result;

    /*
     * Copy back parameters.
     */
    for (iParm = 0; iParm < pCallInfo->cParms; iParm++, pSrcParm++, pDstParm++)
    {
        switch (pDstParm->type)
        {
            case VMMDevHGCMParmType_32bit:
            case VMMDevHGCMParmType_64bit:
                *pDstParm = *pSrcParm;
                break;

            case VMMDevHGCMParmType_PageList:
                pDstParm->u.PageList.size = pSrcParm->u.PageList.size;
                break;

            case VMMDevHGCMParmType_LinAddr_Locked_In:
            case VMMDevHGCMParmType_LinAddr_In:
#ifdef USE_BOUNCH_BUFFERS
                if (    fIsUser
                    &&  iLockBuf < pParmInfo->cLockBufs
                    &&  iParm   == pParmInfo->aLockBufs[iLockBuf].iParm)
                    iLockBuf++;
#endif
                pDstParm->u.Pointer.size = pSrcParm->u.Pointer.size;
                break;

            case VMMDevHGCMParmType_LinAddr_Locked_Out:
            case VMMDevHGCMParmType_LinAddr_Locked:
                if (!VBGLR0_CAN_USE_PHYS_PAGE_LIST())
                {
                    pDstParm->u.Pointer.size = pSrcParm->u.Pointer.size;
                    break;
                }
                /* fall thru */

            case VMMDevHGCMParmType_LinAddr_Out:
            case VMMDevHGCMParmType_LinAddr:
            {
#ifdef USE_BOUNCH_BUFFERS
                if (fIsUser)
                {
                    size_t cbOut = RT_MIN(pSrcParm->u.Pointer.size, pDstParm->u.Pointer.size);
                    if (cbOut)
                    {
                        Assert(pParmInfo->aLockBufs[iLockBuf].iParm == iParm);
                        int rc2 = RTR0MemUserCopyTo((RTR3PTR)pDstParm->u.Pointer.u.linearAddr,
                                                    pParmInfo->aLockBufs[iLockBuf].pvSmallBuf
                                                    ? pParmInfo->aLockBufs[iLockBuf].pvSmallBuf
                                                    : RTR0MemObjAddress(pParmInfo->aLockBufs[iLockBuf].hObj),
                                                    cbOut);
                        if (RT_FAILURE(rc2))
                            return rc2;
                        iLockBuf++;
                    }
                    else if (   iLockBuf < pParmInfo->cLockBufs
                             && iParm   == pParmInfo->aLockBufs[iLockBuf].iParm)
                        iLockBuf++;
                }
#endif
                pDstParm->u.Pointer.size = pSrcParm->u.Pointer.size;
                break;
            }

            default:
                AssertFailed();
                rc = VERR_INTERNAL_ERROR_4;
                break;
        }
    }

#ifdef USE_BOUNCH_BUFFERS
    Assert(!fIsUser || pParmInfo->cLockBufs == iLockBuf);
#endif
    return rc;
}


DECLR0VBGL(int) VbglR0HGCMInternalCall(VBoxGuestHGCMCallInfo *pCallInfo, uint32_t cbCallInfo, uint32_t fFlags,
                                       PFNVBGLHGCMCALLBACK pfnAsyncCallback, void *pvAsyncData, uint32_t u32AsyncData)
{
    bool                    fIsUser = (fFlags & VBGLR0_HGCMCALL_F_MODE_MASK) == VBGLR0_HGCMCALL_F_USER;
    struct VbglR0ParmInfo   ParmInfo;
    size_t                  cbExtra;
    int                     rc;

    /*
     * Basic validation.
     */
    AssertMsgReturn(   !pCallInfo
                    || !pfnAsyncCallback
                    || pCallInfo->cParms > VBOX_HGCM_MAX_PARMS
                    || !(fFlags & ~VBGLR0_HGCMCALL_F_MODE_MASK),
                    ("pCallInfo=%p pfnAsyncCallback=%p fFlags=%#x\n", pCallInfo, pfnAsyncCallback, fFlags),
                    VERR_INVALID_PARAMETER);
    AssertReturn(   cbCallInfo >= sizeof(VBoxGuestHGCMCallInfo)
                 || cbCallInfo >= pCallInfo->cParms * sizeof(HGCMFunctionParameter),
                 VERR_INVALID_PARAMETER);

    Log(("GstHGCMCall: u32ClientID=%#x u32Function=%u cParms=%u cbCallInfo=%#x fFlags=%#x\n",
         pCallInfo->u32ClientID, pCallInfo->u32ClientID, pCallInfo->u32Function, pCallInfo->cParms, cbCallInfo, fFlags));

    /*
     * Validate, lock and buffer the parameters for the call.
     * This will calculate the amount of extra space for physical page list.
     */
    rc = vbglR0HGCMInternalPreprocessCall(pCallInfo, cbCallInfo, fIsUser, &ParmInfo, &cbExtra);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate the request buffer and recreate the call request.
         */
        VMMDevHGCMCall *pHGCMCall;
        rc = VbglGRAlloc((VMMDevRequestHeader **)&pHGCMCall,
                         sizeof(VMMDevHGCMCall) + pCallInfo->cParms * sizeof(HGCMFunctionParameter) + cbExtra,
                         VMMDevReq_HGCMCall);
        if (RT_SUCCESS(rc))
        {
            bool fLeakIt;
            vbglR0HGCMInternalInitCall(pHGCMCall, pCallInfo, cbCallInfo, fIsUser, &ParmInfo);

            /*
             * Perform the call.
             */
            rc = vbglR0HGCMInternalDoCall(pHGCMCall, pfnAsyncCallback, pvAsyncData, u32AsyncData, &fLeakIt);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Copy back the result (parameters and buffers that changed).
                 */
                rc = vbglR0HGCMInternalCopyBackResult(pCallInfo, pHGCMCall, &ParmInfo, fIsUser, rc);
            }

            if (!fLeakIt)
                VbglGRFree(&pHGCMCall->header.header);
        }
    }

    /*
     * Release locks and free bounce buffers.
     */
    if (ParmInfo.cLockBufs)
        while (ParmInfo.cLockBufs-- > 0)
        {
            RTR0MemObjFree(ParmInfo.aLockBufs[ParmInfo.cLockBufs].hObj, false /*fFreeMappings*/);
#ifdef USE_BOUNCH_BUFFERS
            RTMemTmpFree(ParmInfo.aLockBufs[ParmInfo.cLockBufs].pvSmallBuf);
#endif
        }

    return rc;
}


#if ARCH_BITS == 64
DECLR0VBGL(int) VbglR0HGCMInternalCall32(VBoxGuestHGCMCallInfo *pCallInfo, uint32_t cbCallInfo, uint32_t fFlags,
                                         PFNVBGLHGCMCALLBACK pfnAsyncCallback, void *pvAsyncData, uint32_t u32AsyncData)
{
    VBoxGuestHGCMCallInfo   *pCallInfo64;
    HGCMFunctionParameter   *pParm64;
    HGCMFunctionParameter32 *pParm32;
    uint32_t                 cParms;
    uint32_t                 iParm;
    int rc;

    /*
     * Input validation.
     */
    AssertMsgReturn(    !pCallInfo
                    ||  !pfnAsyncCallback
                    ||  pCallInfo->cParms > VBOX_HGCM_MAX_PARMS
                    || !(fFlags & ~VBGLR0_HGCMCALL_F_MODE_MASK),
                    ("pCallInfo=%p pfnAsyncCallback=%p fFlags=%#x\n", pCallInfo, pfnAsyncCallback, fFlags),
                    VERR_INVALID_PARAMETER);
    AssertReturn(   cbCallInfo >= sizeof(VBoxGuestHGCMCallInfo)
                 || cbCallInfo >= pCallInfo->cParms * sizeof(HGCMFunctionParameter32),
                 VERR_INVALID_PARAMETER);
    AssertReturn((fFlags & VBGLR0_HGCMCALL_F_MODE_MASK) == VBGLR0_HGCMCALL_F_KERNEL, VERR_INVALID_PARAMETER);

    cParms = pCallInfo->cParms;
    Log(("VbglR0HGCMInternalCall32: cParms=%d, u32Function=%d, fFlags=%#x\n", cParms, pCallInfo->u32Function, fFlags));

    /*
     * The simple approach, allocate a temporary request and convert the parameters.
     */
    pCallInfo64 = (VBoxGuestHGCMCallInfo *)RTMemTmpAllocZ(sizeof(*pCallInfo64) + cParms * sizeof(HGCMFunctionParameter));
    if (!pCallInfo64)
        return VERR_NO_TMP_MEMORY;

    *pCallInfo64 = *pCallInfo;
    pParm32 = VBOXGUEST_HGCM_CALL_PARMS32(pCallInfo);
    pParm64 = VBOXGUEST_HGCM_CALL_PARMS(pCallInfo64);
    for (iParm = 0; iParm < cParms; iParm++, pParm32++, pParm64++)
    {
        switch (pParm32->type)
        {
            case VMMDevHGCMParmType_32bit:
                pParm64->type      = VMMDevHGCMParmType_32bit;
                pParm64->u.value32 = pParm32->u.value32;
                break;

            case VMMDevHGCMParmType_64bit:
                pParm64->type      = VMMDevHGCMParmType_64bit;
                pParm64->u.value64 = pParm32->u.value64;
                break;

            case VMMDevHGCMParmType_LinAddr_Out:
            case VMMDevHGCMParmType_LinAddr:
            case VMMDevHGCMParmType_LinAddr_In:
                pParm64->type                   = pParm32->type;
                pParm64->u.Pointer.size         = pParm32->u.Pointer.size;
                pParm64->u.Pointer.u.linearAddr = pParm32->u.Pointer.u.linearAddr;
                break;

            default:
                rc = VERR_INVALID_PARAMETER;
                break;
        }
        if (RT_FAILURE(rc))
            break;
    }
    if (RT_SUCCESS(rc))
    {
        rc = VbglR0HGCMInternalCall(pCallInfo64, sizeof(*pCallInfo64) + cParms * sizeof(HGCMFunctionParameter), fFlags,
                                    pfnAsyncCallback, pvAsyncData, u32AsyncData);

        /*
         * Copy back.
         */
        for (iParm = 0; iParm < cParms; iParm++, pParm32++, pParm64++)
        {
            switch (pParm32->type)
            {
                case VMMDevHGCMParmType_32bit:
                    pParm32->u.value32 = pParm32->u.value32;
                    break;

                case VMMDevHGCMParmType_64bit:
                    pParm32->u.value64 = pParm64->u.value64;
                    break;

                case VMMDevHGCMParmType_LinAddr_Out:
                case VMMDevHGCMParmType_LinAddr:
                case VMMDevHGCMParmType_LinAddr_In:
                    pParm32->u.Pointer.size = pParm64->u.Pointer.size;
                    break;

                default:
                    rc = VERR_INTERNAL_ERROR_3;
                    break;
            }
        }
        *pCallInfo = *pCallInfo64;
    }

    RTMemTmpFree(pCallInfo64);
    return rc;
}
#endif /* ARCH_BITS == 64 */

#endif /* VBGL_VBOXGUEST */

