/* $Id$ */
/** @file
 * IPRT - Mutex Semaphores, Windows.
 */

/*
 * Copyright (C) 2006-2009 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP RTLOGGROUP_SEMAPHORE
#include <Windows.h>

#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include "internal/magics.h"
#include "internal/strict.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Posix internal representation of a Mutex semaphore. */
struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t                u32Magic;
    /** Recursion count. */
    uint32_t volatile       cRecursions;
    /** The owner thread. */
    RTNATIVETHREAD volatile hNativeOwner;
    /** The mutex handle. */
    HANDLE                  hMtx;
#ifdef RTSEMMUTEX_STRICT
    /** Lock validator record associated with this mutex. */
    RTLOCKVALRECEXCL        ValidatorRec;
#endif
};



#undef RTSemMutexCreate
RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    return RTSemMutexCreateEx(phMutexSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
}


RTDECL(int) RTSemMutexCreateEx(PRTSEMMUTEX phMutexSem, uint32_t fFlags,
                               RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMMUTEX_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Create the semaphore.
     */
    int rc;
    HANDLE hMtx = CreateMutex(NULL, FALSE, NULL);
    if (hMtx)
    {
        RTSEMMUTEXINTERNAL *pThis = (RTSEMMUTEXINTERNAL *)RTMemAlloc(sizeof(*pThis));
        if (pThis)
        {
            pThis->u32Magic     = RTSEMMUTEX_MAGIC;
            pThis->hMtx         = hMtx;
            pThis->hNativeOwner = NIL_RTNATIVETHREAD;
            pThis->cRecursions  = 0;
#ifdef RTSEMMUTEX_STRICT
            va_list va;
            va_start(va, pszNameFmt);
            RTLockValidatorRecExclInitV(&pThis->ValidatorRec, hClass, uSubClass, pThis,
                                        !(fFlags & RTSEMMUTEX_FLAGS_NO_LOCK_VAL), pszNameFmt, va);
            va_end(va);
#endif
            *phMutexSem = pThis;
            return VINF_SUCCESS;
        }

        rc = VERR_NO_MEMORY;
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    return rc;
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    if (pThis == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Close semaphore handle.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSEMMUTEX_MAGIC_DEAD, RTSEMMUTEX_MAGIC), VERR_INVALID_HANDLE);
    HANDLE hMtx = pThis->hMtx;
    ASMAtomicWritePtr((void * volatile *)&pThis->hMtx, (void *)INVALID_HANDLE_VALUE);

    int rc = VINF_SUCCESS;
    if (!CloseHandle(hMtx))
    {
        rc = RTErrConvertFromWin32(GetLastError());
        AssertMsgFailed(("%p rc=%d lasterr=%d\n", pThis->hMtx, rc, GetLastError()));
    }

#ifdef RTSEMMUTEX_STRICT
    RTLockValidatorRecExclDelete(&pThis->ValidatorRec);
#endif
    RTMemFree(pThis);
    return rc;
}


RTDECL(uint32_t) RTSemMutexSetSubClass(RTSEMMUTEX hMutexSem, uint32_t uSubClass)
{
#ifdef RTSEMMUTEX_STRICT
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);

    return RTLockValidatorRecExclSetSubClass(&pThis->ValidatorRec, uSubClass);
#else
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}


/**
 * Internal worker for RTSemMutexRequestNoResume and it's debug companion.
 *
 * @returns Same as RTSEmMutexRequestNoResume
 * @param   hMutexSem            The mutex handle.
 * @param   cMillies            The number of milliseconds to wait.
 * @param   pSrcPos             The source position of the caller.
 */
DECL_FORCE_INLINE(int) rtSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, unsigned cMillies, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Check for recursive entry.
     */
    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeOwner;
    ASMAtomicReadHandle(&pThis->hNativeOwner, &hNativeOwner);
    if (hNativeOwner == hNativeSelf)
    {
#ifdef RTSEMMUTEX_STRICT
        int rc9 = RTLockValidatorRecExclRecursion(&pThis->ValidatorRec, pSrcPos);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        ASMAtomicIncU32(&pThis->cRecursions);
        return VINF_SUCCESS;
    }

    /*
     * Lock mutex semaphore.
     */
    RTTHREAD        hThreadSelf = NIL_RTTHREAD;
    if (cMillies > 0)
    {
#ifdef RTSEMMUTEX_STRICT
        hThreadSelf = RTThreadSelfAutoAdopt();
        int rc9 = RTLockValidatorRecExclCheckOrderAndBlocking(&pThis->ValidatorRec, hThreadSelf, pSrcPos, true,
                                                              cMillies, RTTHREADSTATE_MUTEX, true);
        if (RT_FAILURE(rc9))
            return rc9;
#else
        hThreadSelf = RTThreadSelf();
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_MUTEX, true);
#endif
    }
    DWORD rc = WaitForSingleObjectEx(pThis->hMtx,
                                     cMillies == RT_INDEFINITE_WAIT ? INFINITE : cMillies,
                                     TRUE /*fAlertable*/);
    RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_MUTEX);
    switch (rc)
    {
        case WAIT_OBJECT_0:
#ifdef RTSEMMUTEX_STRICT
            RTLockValidatorRecExclSetOwner(&pThis->ValidatorRec, hThreadSelf, pSrcPos, true);
#endif
            ASMAtomicWriteHandle(&pThis->hNativeOwner, hNativeSelf);
            ASMAtomicWriteU32(&pThis->cRecursions, 1);
            return VINF_SUCCESS;

        case WAIT_TIMEOUT:          return VERR_TIMEOUT;
        case WAIT_IO_COMPLETION:    return VERR_INTERRUPTED;
        case WAIT_ABANDONED:        return VERR_SEM_OWNER_DIED;
        default:
            AssertMsgFailed(("%u\n",  rc));
        case WAIT_FAILED:
        {
            int rc2 = RTErrConvertFromWin32(GetLastError());
            AssertMsgFailed(("Wait on hMutexSem %p failed, rc=%d lasterr=%d\n", hMutexSem, rc, GetLastError()));
            if (rc2 != VINF_SUCCESS)
                return rc2;

            AssertMsgFailed(("WaitForSingleObject(event) -> rc=%d while converted lasterr=%d\n", rc, rc2));
            return VERR_INTERNAL_ERROR;
        }
    }
}


#undef RTSemMutexRequestNoResume
RTDECL(int) RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, unsigned cMillies)
{
#ifndef RTSEMMUTEX_STRICT
    return rtSemMutexRequestNoResume(hMutexSem, cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemMutexRequestNoResume(hMutexSem, cMillies, &SrcPos);
#endif
}


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, unsigned cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemMutexRequestNoResume(hMutexSem, cMillies, &SrcPos);
}


RTDECL(int) RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Check ownership and recursions.
     */
    RTNATIVETHREAD hNativeSelf = RTThreadNativeSelf();
    RTNATIVETHREAD hNativeOwner;
    ASMAtomicReadHandle(&pThis->hNativeOwner, &hNativeOwner);
    if (RT_UNLIKELY(hNativeOwner != hNativeSelf))
    {
        AssertMsgFailed(("Not owner of mutex %p!! hNativeSelf=%RTntrd Owner=%RTntrd cRecursions=%d\n",
                         pThis, hNativeSelf, hNativeOwner, pThis->cRecursions));
        return VERR_NOT_OWNER;
    }
    if (pThis->cRecursions > 1)
    {
#ifdef RTSEMMUTEX_STRICT
        int rc9 = RTLockValidatorRecExclUnwind(&pThis->ValidatorRec);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        ASMAtomicDecU32(&pThis->cRecursions);
        return VINF_SUCCESS;
    }

    /*
     * Unlock mutex semaphore.
     */
#ifdef RTSEMMUTEX_STRICT
    int rc9 = RTLockValidatorRecExclReleaseOwner(&pThis->ValidatorRec, false);
    if (RT_FAILURE(rc9))
        return rc9;
#endif
    ASMAtomicWriteU32(&pThis->cRecursions, 0);
    ASMAtomicWriteHandle(&pThis->hNativeOwner, NIL_RTNATIVETHREAD);

    if (ReleaseMutex(pThis->hMtx))
        return VINF_SUCCESS;

    int rc = RTErrConvertFromWin32(GetLastError());
    AssertMsgFailed(("%p/%p, rc=%Rrc lasterr=%d\n", pThis, pThis->hMtx, rc, GetLastError()));
    return rc;
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, false);

    RTNATIVETHREAD hNativeOwner;
    ASMAtomicReadHandle(&pThis->hNativeOwner, &hNativeOwner);
    return hNativeOwner != NIL_RTNATIVETHREAD;
}

