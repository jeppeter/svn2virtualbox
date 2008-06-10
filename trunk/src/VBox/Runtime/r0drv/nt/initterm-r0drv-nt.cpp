/* $Id$ */
/** @file
 * IPRT - Initialization & Termination, R0 Driver, NT.
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
#include "the-nt-kernel.h"
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/mp.h>
#include "internal/initterm.h"
#include "internal-r0drv-nt.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The Nt CPU set.
 * KeQueryActiveProcssors() cannot be called at all IRQLs and therefore we'll
 * have to cache it. Fortunately, Nt doesn't really support taking CPUs offline
 * or online. It's first with W2K8 that support for adding / onlining cpus at
 * runtime is (officially) supported. Once we start caring about this, we'll
 * simply use the native MP event callback and update this variable as cpus
 * comes online.
 */
RTCPUSET g_rtMpNtCpuSet;


int rtR0InitNative(void)
{
    /*
     * Init the Nt cpu set.
     */
    KAFFINITY ActiveProcessors = KeQueryActiveProcessors();
    RTCpuSetEmpty(&g_rtMpNtCpuSet);
    RTCpuSetFromU64(&g_rtMpNtCpuSet, ActiveProcessors);

#if 0 /* W2K8 support */
    return RTR0MpNotificationInit(NULL);
#else
    return VINF_SUCCESS;
#endif
}


void rtR0TermNative(void)
{
#if 0 /* W2K8 support */
    RTR0MpNotificationTerm(NULL);
#endif
}

