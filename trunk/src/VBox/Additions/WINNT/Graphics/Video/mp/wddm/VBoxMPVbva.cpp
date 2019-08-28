/* $Id$ */
/** @file
 * VBox WDDM Miniport driver
 */

/*
 * Copyright (C) 2011-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "VBoxMPWddm.h"
#include "common/VBoxMPCommon.h"

/*
 * Public hardware buffer methods.
 */
int vboxVbvaEnable (PVBOXMP_DEVEXT pDevExt, VBOXVBVAINFO *pVbva)
{
    if (VBoxVBVAEnable(&pVbva->Vbva, &VBoxCommonFromDeviceExt(pDevExt)->guestCtx,
            pVbva->Vbva.pVBVA, pVbva->srcId))
        return VINF_SUCCESS;

    WARN(("VBoxVBVAEnable failed!"));
    return VERR_GENERAL_FAILURE;
}

int vboxVbvaDisable (PVBOXMP_DEVEXT pDevExt, VBOXVBVAINFO *pVbva)
{
    VBoxVBVADisable(&pVbva->Vbva, &VBoxCommonFromDeviceExt(pDevExt)->guestCtx, pVbva->srcId);
    return VINF_SUCCESS;
}

int vboxVbvaCreate(PVBOXMP_DEVEXT pDevExt, VBOXVBVAINFO *pVbva, ULONG offBuffer, ULONG cbBuffer, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    memset(pVbva, 0, sizeof(VBOXVBVAINFO));

    KeInitializeSpinLock(&pVbva->Lock);

    int rc = VBoxMPCmnMapAdapterMemory(VBoxCommonFromDeviceExt(pDevExt),
                                       (void**)&pVbva->Vbva.pVBVA,
                                       offBuffer,
                                       cbBuffer);
    if (RT_SUCCESS(rc))
    {
        Assert(pVbva->Vbva.pVBVA);
        VBoxVBVASetupBufferContext(&pVbva->Vbva, offBuffer, cbBuffer);
        pVbva->srcId = srcId;
    }
    else
    {
        WARN(("VBoxMPCmnMapAdapterMemory failed rc %d", rc));
    }


    return rc;
}

int vboxVbvaDestroy(PVBOXMP_DEVEXT pDevExt, VBOXVBVAINFO *pVbva)
{
    int rc = VINF_SUCCESS;
    VBoxMPCmnUnmapAdapterMemory(VBoxCommonFromDeviceExt(pDevExt), (void**)&pVbva->Vbva.pVBVA);
    memset(pVbva, 0, sizeof (VBOXVBVAINFO));
    return rc;
}

int vboxVbvaReportDirtyRect (PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSrc, RECT *pRectOrig)
{
    VBVACMDHDR hdr;

    RECT rect = *pRectOrig;

//        if (rect.left < 0) rect.left = 0;
//        if (rect.top < 0) rect.top = 0;
//        if (rect.right > (int)ppdev->cxScreen) rect.right = ppdev->cxScreen;
//        if (rect.bottom > (int)ppdev->cyScreen) rect.bottom = ppdev->cyScreen;

    hdr.x = (int16_t)rect.left;
    hdr.y = (int16_t)rect.top;
    hdr.w = (uint16_t)(rect.right - rect.left);
    hdr.h = (uint16_t)(rect.bottom - rect.top);

    hdr.x += (int16_t)pSrc->VScreenPos.x;
    hdr.y += (int16_t)pSrc->VScreenPos.y;

    if (VBoxVBVAWrite(&pSrc->Vbva.Vbva, &VBoxCommonFromDeviceExt(pDevExt)->guestCtx, &hdr, sizeof(hdr)))
        return VINF_SUCCESS;

    WARN(("VBoxVBVAWrite failed"));
    return VERR_GENERAL_FAILURE;
}
