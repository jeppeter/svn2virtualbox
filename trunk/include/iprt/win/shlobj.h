/** @file
 * Safe way to include shlobj.h.
 */

/*
 * Copyright (C) 2016-2020 Oracle Corporation
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
 */

#ifndef IPRT_INCLUDED_win_shlobj_h
#define IPRT_INCLUDED_win_shlobj_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable:4668) /* warning C4668: 'USE_SP_ALTPLATFORM_INFO_V1' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif' */
# if _MSC_VER >= 1800 /*RT_MSC_VER_VC120*/
#  pragma warning(disable:4005) /* sdk/v7.1/include/sal_supp.h(57) : warning C4005: '__useHeader' : macro redefinition */
#  pragma warning(disable:4255) /* windef.h(227) : warning C4255: 'NEARPROC' : no function prototype given: converting '()' to '(void)' */
# endif
# if _MSC_VER >= 1900 /*RT_MSC_VER_VC140*/
#  pragma warning(disable:4091) /* sdk/v7.1/include/shlobj.h(1151): warning C4091: 'typedef ': ignored on left of 'tagGPFIDL_FLAGS' when no variable is declared */
#  ifdef __cplusplus
#   pragma warning(disable:5039) /* commctrl.h(8323): warning C5039: 'DSA_DestroyCallback': pointer or reference to potentially throwing function passed to 'extern "C"' function under -EHc. Undefined behavior may occur if this function throws an exception. */
#  endif
# endif
# if _MSC_VER >= 1910 /*RT_MSC_VER_VC141*/
#  pragma warning(disable:4768) /* sdk/v7.1/include/shlobj.h(1065): warning C4768: __declspec attributes before linkage specification are ignored */
# endif
#endif

#include <shlobj.h>

#ifdef _MSC_VER
# pragma warning(pop)
#endif

#endif /* !IPRT_INCLUDED_win_shlobj_h */

