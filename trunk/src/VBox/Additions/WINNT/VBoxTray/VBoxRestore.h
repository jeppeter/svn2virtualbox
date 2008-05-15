/** @file
 * VBoxRestore - Restore notification
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

#ifndef __VBOXSERVICERESTORE__H
#define __VBOXSERVICERESTORE__H

/* The restore service prototypes. */
int                VBoxRestoreInit    (const VBOXSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread);
unsigned __stdcall VBoxRestoreThread  (void *pInstance);
void               VBoxRestoreDestroy (const VBOXSERVICEENV *pEnv, void *pInstance);

void VBoxRestoreSession();
void VBoxRestoreCheckVRDP();

#endif /* __VBOXSERVICERESTORE__H */
