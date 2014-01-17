/* $Id$ */

/** @file
 * Compositor impl
 */

/*
 * Copyright (C) 2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#include <cr_compositor.h>

#define VBOXVR_SCR_COMPOSITOR_RECTS_UNDEFINED UINT32_MAX


static int crVrScrCompositorRectsAssignBuffer(PVBOXVR_SCR_COMPOSITOR pCompositor, uint32_t cRects)
{
    Assert(cRects);

    if (pCompositor->cRectsBuffer >= cRects)
    {
        pCompositor->cRects = cRects;
        return VINF_SUCCESS;
    }

    if (pCompositor->cRectsBuffer)
    {
        Assert(pCompositor->paSrcRects);
        RTMemFree(pCompositor->paSrcRects);
        pCompositor->paSrcRects = NULL;
        Assert(pCompositor->paDstRects);
        RTMemFree(pCompositor->paDstRects);
        pCompositor->paDstRects = NULL;
        Assert(pCompositor->paDstUnstretchedRects);
        RTMemFree(pCompositor->paDstUnstretchedRects);
        pCompositor->paDstUnstretchedRects = NULL;
    }
    else
    {
        Assert(!pCompositor->paSrcRects);
        Assert(!pCompositor->paDstRects);
        Assert(!pCompositor->paDstUnstretchedRects);
    }

    pCompositor->paSrcRects = (PRTRECT)RTMemAlloc(sizeof (*pCompositor->paSrcRects) * cRects);
    if (pCompositor->paSrcRects)
    {
        pCompositor->paDstRects = (PRTRECT)RTMemAlloc(sizeof (*pCompositor->paDstRects) * cRects);
        if (pCompositor->paDstRects)
        {
            pCompositor->paDstUnstretchedRects = (PRTRECT)RTMemAlloc(sizeof (*pCompositor->paDstUnstretchedRects) * cRects);
            if (pCompositor->paDstUnstretchedRects)
            {
                pCompositor->cRects = cRects;
                pCompositor->cRectsBuffer = cRects;
                return VINF_SUCCESS;
            }

            RTMemFree(pCompositor->paDstRects);
            pCompositor->paDstRects = NULL;
        }
        else
        {
            WARN(("RTMemAlloc failed!"));
        }
        RTMemFree(pCompositor->paSrcRects);
        pCompositor->paSrcRects = NULL;
    }
    else
    {
        WARN(("RTMemAlloc failed!"));
    }

    pCompositor->cRects = VBOXVR_SCR_COMPOSITOR_RECTS_UNDEFINED;
    pCompositor->cRectsBuffer = 0;

    return VERR_NO_MEMORY;
}

static void crVrScrCompositorRectsInvalidate(PVBOXVR_SCR_COMPOSITOR pCompositor)
{
    pCompositor->cRects = VBOXVR_SCR_COMPOSITOR_RECTS_UNDEFINED;
}

static DECLCALLBACK(bool) crVrScrCompositorRectsCounterCb(PVBOXVR_COMPOSITOR pCompositor, PVBOXVR_COMPOSITOR_ENTRY pEntry, void *pvVisitor)
{
    uint32_t* pCounter = (uint32_t*)pvVisitor;
    Assert(VBoxVrListRectsCount(&pEntry->Vr));
    *pCounter += VBoxVrListRectsCount(&pEntry->Vr);
    return true;
}

typedef struct VBOXVR_SCR_COMPOSITOR_RECTS_ASSIGNER
{
    PRTRECT paSrcRects;
    PRTRECT paDstRects;
    PRTRECT paDstUnstretchedRects;
    uint32_t cRects;
} VBOXVR_SCR_COMPOSITOR_RECTS_ASSIGNER, *PVBOXVR_SCR_COMPOSITOR_RECTS_ASSIGNER;

static DECLCALLBACK(bool) crVrScrCompositorRectsAssignerCb(PVBOXVR_COMPOSITOR pCCompositor, PVBOXVR_COMPOSITOR_ENTRY pCEntry, void *pvVisitor)
{
    PVBOXVR_SCR_COMPOSITOR_RECTS_ASSIGNER pData = (PVBOXVR_SCR_COMPOSITOR_RECTS_ASSIGNER)pvVisitor;
    PVBOXVR_SCR_COMPOSITOR pCompositor = VBOXVR_SCR_COMPOSITOR_FROM_COMPOSITOR(pCCompositor);
    PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry = VBOXVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pCEntry);
    pEntry->paSrcRects = pData->paSrcRects;
    pEntry->paDstRects = pData->paDstRects;
    pEntry->paDstUnstretchedRects = pData->paDstUnstretchedRects;
    uint32_t cRects = VBoxVrListRectsCount(&pCEntry->Vr);
    Assert(cRects);
    Assert(cRects <= pData->cRects);
    int rc = VBoxVrListRectsGet(&pCEntry->Vr, cRects, pEntry->paDstUnstretchedRects);
    AssertRC(rc);

    if (!pEntry->Rect.xLeft && !pEntry->Rect.yTop)
    {
        memcpy(pEntry->paSrcRects, pEntry->paDstUnstretchedRects, cRects * sizeof (*pEntry->paSrcRects));
    }
    else
    {
        for (uint32_t i = 0; i < cRects; ++i)
        {
            pEntry->paSrcRects[i].xLeft = (int32_t)((pEntry->paDstUnstretchedRects[i].xLeft - pEntry->Rect.xLeft));
            pEntry->paSrcRects[i].yTop = (int32_t)((pEntry->paDstUnstretchedRects[i].yTop - pEntry->Rect.yTop));
            pEntry->paSrcRects[i].xRight = (int32_t)((pEntry->paDstUnstretchedRects[i].xRight - pEntry->Rect.xLeft));
            pEntry->paSrcRects[i].yBottom = (int32_t)((pEntry->paDstUnstretchedRects[i].yBottom - pEntry->Rect.yTop));
        }
    }

#ifndef IN_RING0
    if (pCompositor->StretchX != 1. || pCompositor->StretchY != 1.)
    {
        for (uint32_t i = 0; i < cRects; ++i)
        {
            if (pCompositor->StretchX != 1.)
            {
                pEntry->paDstRects[i].xLeft = (int32_t)(pEntry->paDstUnstretchedRects[i].xLeft * pCompositor->StretchX);
                pEntry->paDstRects[i].xRight = (int32_t)(pEntry->paDstUnstretchedRects[i].xRight * pCompositor->StretchX);
            }
            if (pCompositor->StretchY != 1.)
            {
                pEntry->paDstRects[i].yTop = (int32_t)(pEntry->paDstUnstretchedRects[i].yTop * pCompositor->StretchY);
                pEntry->paDstRects[i].yBottom = (int32_t)(pEntry->paDstUnstretchedRects[i].yBottom * pCompositor->StretchY);
            }
        }
    }
    else
#endif
    {
        memcpy(pEntry->paDstRects, pEntry->paDstUnstretchedRects, cRects * sizeof (*pEntry->paDstUnstretchedRects));
    }

#if 0//ndef IN_RING0
    bool canZeroX = (pCompositor->StretchX < 1.);
    bool canZeroY = (pCompositor->StretchY < 1.);
    if (canZeroX && canZeroY)
    {
        /* filter out zero rectangles*/
        uint32_t iOrig, iNew;
        for (iOrig = 0, iNew = 0; iOrig < cRects; ++iOrig)
        {
            PRTRECT pOrigRect = &pEntry->paDstRects[iOrig];
            if (pOrigRect->xLeft != pOrigRect->xRight
                    && pOrigRect->yTop != pOrigRect->yBottom)
                continue;

            if (iNew != iOrig)
            {
                PRTRECT pNewRect = &pEntry->paSrcRects[iNew];
                *pNewRect = *pOrigRect;
            }

            ++iNew;
        }

        Assert(iNew <= iOrig);

        uint32_t cDiff = iOrig - iNew;

        if (cDiff)
        {
            pCompositor->cRects -= cDiff;
            cRects -= cDiff;
        }
    }
#endif

    pEntry->cRects = cRects;
    pData->paDstRects += cRects;
    pData->paSrcRects += cRects;
    pData->paDstUnstretchedRects += cRects;
    pData->cRects -= cRects;
    return true;
}

static int crVrScrCompositorRectsCheckInit(const VBOXVR_SCR_COMPOSITOR *pcCompositor)
{
    VBOXVR_SCR_COMPOSITOR *pCompositor = const_cast<VBOXVR_SCR_COMPOSITOR*>(pcCompositor);

    if (pCompositor->cRects != VBOXVR_SCR_COMPOSITOR_RECTS_UNDEFINED)
        return VINF_SUCCESS;

    uint32_t cRects = 0;
    VBoxVrCompositorVisit(&pCompositor->Compositor, crVrScrCompositorRectsCounterCb, &cRects);

    if (!cRects)
    {
        pCompositor->cRects = 0;
        return VINF_SUCCESS;
    }

    int rc = crVrScrCompositorRectsAssignBuffer(pCompositor, cRects);
    if (!RT_SUCCESS(rc))
        return rc;

    VBOXVR_SCR_COMPOSITOR_RECTS_ASSIGNER AssignerData;
    AssignerData.paSrcRects = pCompositor->paSrcRects;
    AssignerData.paDstRects = pCompositor->paDstRects;
    AssignerData.paDstUnstretchedRects = pCompositor->paDstUnstretchedRects;
    AssignerData.cRects = pCompositor->cRects;
    VBoxVrCompositorVisit(&pCompositor->Compositor, crVrScrCompositorRectsAssignerCb, &AssignerData);
    Assert(!AssignerData.cRects);
    return VINF_SUCCESS;
}


static int crVrScrCompositorEntryRegionsAdd(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, uint32_t cRegions, const RTRECT *paRegions, VBOXVR_SCR_COMPOSITOR_ENTRY **ppReplacedScrEntry, uint32_t *pfChangedFlags)
{
    uint32_t fChangedFlags = 0;
    PVBOXVR_COMPOSITOR_ENTRY pReplacedEntry;
    int rc = VBoxVrCompositorEntryRegionsAdd(&pCompositor->Compositor, pEntry ? &pEntry->Ce : NULL, cRegions, paRegions, &pReplacedEntry, &fChangedFlags);
    if (!RT_SUCCESS(rc))
    {
        WARN(("VBoxVrCompositorEntryRegionsAdd failed, rc %d", rc));
        return rc;
    }

    VBOXVR_SCR_COMPOSITOR_ENTRY *pReplacedScrEntry = VBOXVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pReplacedEntry);

    if (fChangedFlags & VBOXVR_COMPOSITOR_CF_REGIONS_CHANGED)
    {
        crVrScrCompositorRectsInvalidate(pCompositor);
    }
    else if (fChangedFlags & VBOXVR_COMPOSITOR_CF_ENTRY_REPLACED)
    {
        Assert(pReplacedScrEntry);
    }

    if (fChangedFlags & VBOXVR_COMPOSITOR_CF_OTHER_ENTRIES_REGIONS_CHANGED)
    {
        CrVrScrCompositorEntrySetAllChanged(pCompositor, true);
    }
    else if ((fChangedFlags & VBOXVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED) && pEntry)
    {
        CrVrScrCompositorEntrySetChanged(pEntry, true);
    }

    if (pfChangedFlags)
        *pfChangedFlags = fChangedFlags;

    if (ppReplacedScrEntry)
        *ppReplacedScrEntry = pReplacedScrEntry;

    return VINF_SUCCESS;
}

static int crVrScrCompositorEntryRegionsSet(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, uint32_t cRegions, const RTRECT *paRegions, bool *pfChanged)
{
    bool fChanged;
    int rc = VBoxVrCompositorEntryRegionsSet(&pCompositor->Compositor, &pEntry->Ce, cRegions, paRegions, &fChanged);
    if (!RT_SUCCESS(rc))
    {
        WARN(("VBoxVrCompositorEntryRegionsSet failed, rc %d", rc));
        return rc;
    }

    if (fChanged)
    {
        CrVrScrCompositorEntrySetAllChanged(pCompositor, true);
        if (!CrVrScrCompositorEntryIsInList(pEntry))
        {
            pEntry->cRects = 0;
            pEntry->paSrcRects = NULL;
            pEntry->paDstRects = NULL;
            pEntry->paDstUnstretchedRects = NULL;
        }
        crVrScrCompositorRectsInvalidate(pCompositor);
    }


    if (pfChanged)
        *pfChanged = fChanged;
    return VINF_SUCCESS;
}

static int crVrScrCompositorEntryPositionSet(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, const RTPOINT *pPos, bool *pfChanged)
{
    if (pfChanged)
        *pfChanged = false;
    if (pEntry && (pEntry->Rect.xLeft != pPos->x || pEntry->Rect.yTop != pPos->y))
    {
        if (VBoxVrCompositorEntryIsInList(&pEntry->Ce))
        {
            int rc = VBoxVrCompositorEntryRegionsTranslate(&pCompositor->Compositor, &pEntry->Ce, pPos->x - pEntry->Rect.xLeft, pPos->y - pEntry->Rect.yTop, pfChanged);
            if (!RT_SUCCESS(rc))
            {
                WARN(("VBoxVrCompositorEntryRegionsTranslate failed rc %d", rc));
                return rc;
            }

            crVrScrCompositorRectsInvalidate(pCompositor);
        }

        VBoxRectMove(&pEntry->Rect, pPos->x, pPos->y);
        CrVrScrCompositorEntrySetChanged(pEntry, true);

        if (pfChanged)
            *pfChanged = true;
    }
    return VINF_SUCCESS;
}

static int crVrScrCompositorEntryEnsureRegionsBounds(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, bool *pfChanged)
{
    RTRECT Rect;
    Rect.xLeft = RT_MAX(pCompositor->Rect.xLeft, pEntry->Rect.xLeft);
    Rect.yTop = RT_MAX(pCompositor->Rect.yTop, pEntry->Rect.yTop);
    Rect.xRight = RT_MIN(pCompositor->Rect.xRight, pEntry->Rect.xRight);
    Rect.yBottom = RT_MIN(pCompositor->Rect.yBottom, pEntry->Rect.yBottom);
    bool fChanged = false;

    if (pfChanged)
        *pfChanged = false;

    int rc = CrVrScrCompositorEntryRegionsIntersect(pCompositor, pEntry, 1, &Rect, &fChanged);
    if (!RT_SUCCESS(rc))
        WARN(("CrVrScrCompositorEntryRegionsIntersect failed, rc %d", rc));

    if (pfChanged)
        *pfChanged = fChanged;
    return rc;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryRegionsAdd(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, const RTPOINT *pPos, uint32_t cRegions, const RTRECT *paRegions, bool fPosRelated, VBOXVR_SCR_COMPOSITOR_ENTRY **ppReplacedScrEntry, uint32_t *pfChangeFlags)
{
    int rc;
    uint32_t fChangeFlags = 0;
    bool fPosChanged = false;
    RTRECT *paTranslatedRects = NULL;
    if (pPos)
    {
        rc = crVrScrCompositorEntryPositionSet(pCompositor, pEntry, pPos, &fPosChanged);
        if (!RT_SUCCESS(rc))
        {
            WARN(("RegionsAdd: crVrScrCompositorEntryPositionSet failed rc %d", rc));
            return rc;
        }
    }

    if (fPosRelated)
    {
        if (!pEntry)
        {
            WARN(("Entry is expected to be specified for pos-related regions"));
            return VERR_INVALID_PARAMETER;
        }

        if (cRegions && (pEntry->Rect.xLeft || pEntry->Rect.yTop))
        {
            paTranslatedRects = (RTRECT*)RTMemAlloc(sizeof (RTRECT) * cRegions);
            if (!paTranslatedRects)
            {
                WARN(("RTMemAlloc failed"));
                return VERR_NO_MEMORY;
            }
            memcpy (paTranslatedRects, paRegions, sizeof (RTRECT) * cRegions);
            for (uint32_t i = 0; i < cRegions; ++i)
            {
                VBoxRectTranslate(&paTranslatedRects[i], pEntry->Rect.xLeft, pEntry->Rect.yTop);
                paRegions = paTranslatedRects;
            }
        }
    }

    rc = crVrScrCompositorEntryRegionsAdd(pCompositor, pEntry, cRegions, paRegions, ppReplacedScrEntry, &fChangeFlags);
    if (!RT_SUCCESS(rc))
    {
        WARN(("crVrScrCompositorEntryRegionsAdd failed, rc %d", rc));
        goto done;
    }

    if ((fPosChanged || (fChangeFlags & VBOXVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED)) && pEntry)
    {
        bool fAdjusted = false;
        rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, &fAdjusted);
        if (!RT_SUCCESS(rc))
        {
            WARN(("crVrScrCompositorEntryEnsureRegionsBounds failed, rc %d", rc));
            goto done;
        }

        if (fAdjusted)
        {
            fChangeFlags &= ~VBOXVR_COMPOSITOR_CF_ENTRY_REPLACED;
            fChangeFlags |= VBOXVR_COMPOSITOR_CF_REGIONS_CHANGED | VBOXVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED;
        }
    }

    if (fChangeFlags & VBOXVR_COMPOSITOR_CF_ENTRY_REPLACED)
        fPosChanged = false;
    else if (ppReplacedScrEntry)
        *ppReplacedScrEntry = NULL;

    if (pfChangeFlags)
    {
        if (fPosChanged)
        {
            /* means entry was in list and was moved, so regions changed */
            *pfChangeFlags = VBOXVR_COMPOSITOR_CF_REGIONS_CHANGED | VBOXVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED | VBOXVR_COMPOSITOR_CF_OTHER_ENTRIES_REGIONS_CHANGED;
        }
        else
            *pfChangeFlags = fChangeFlags;
    }

done:

    if (paTranslatedRects)
        RTMemFree(paTranslatedRects);

    return rc;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryRectSet(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, const RTRECT *pRect)
{
    if (!memcmp(&pEntry->Rect, pRect, sizeof (*pRect)))
    {
        return VINF_SUCCESS;
    }
    RTPOINT Point = {pRect->xLeft, pRect->yTop};
    bool fChanged = false;
    int rc = crVrScrCompositorEntryPositionSet(pCompositor, pEntry, &Point, &fChanged);
    if (!RT_SUCCESS(rc))
    {
        WARN(("crVrScrCompositorEntryPositionSet failed %d", rc));
        return rc;
    }

    pEntry->Rect = *pRect;

    if (!CrVrScrCompositorEntryIsUsed(pEntry))
        return VINF_SUCCESS;

    rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, NULL);
    if (!RT_SUCCESS(rc))
    {
        WARN(("crVrScrCompositorEntryEnsureRegionsBounds failed, rc %d", rc));
        return rc;
    }

    return VINF_SUCCESS;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryTexAssign(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, CR_TEXDATA *pTex)
{
    if (pEntry->pTex == pTex)
        return VINF_SUCCESS;

    if (pEntry->pTex)
        CrTdRelease(pEntry->pTex);
    if (pTex)
        CrTdAddRef(pTex);
    pEntry->pTex = pTex;
    return VINF_SUCCESS;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryRegionsSet(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, const RTPOINT *pPos, uint32_t cRegions, const RTRECT *paRegions, bool fPosRelated, bool *pfChanged)
{
    /* @todo: the fChanged sate calculation is really rough now, this is enough for now though */
    bool fChanged = false, fPosChanged = false;
    bool fWasInList = CrVrScrCompositorEntryIsInList(pEntry);
    RTRECT *paTranslatedRects = NULL;
    int rc = CrVrScrCompositorEntryRemove(pCompositor, pEntry);
    if (!RT_SUCCESS(rc))
    {
        WARN(("RegionsSet: CrVrScrCompositorEntryRemove failed rc %d", rc));
        return rc;
    }

    if (pPos)
    {
        rc = crVrScrCompositorEntryPositionSet(pCompositor, pEntry, pPos, &fPosChanged);
        if (!RT_SUCCESS(rc))
        {
            WARN(("RegionsSet: crVrScrCompositorEntryPositionSet failed rc %d", rc));
            return rc;
        }
    }

    if (fPosRelated)
    {
        if (!pEntry)
        {
            WARN(("Entry is expected to be specified for pos-related regions"));
            return VERR_INVALID_PARAMETER;
        }

        if (cRegions && (pEntry->Rect.xLeft || pEntry->Rect.yTop))
        {
            paTranslatedRects = (RTRECT*)RTMemAlloc(sizeof (RTRECT) * cRegions);
            if (!paTranslatedRects)
            {
                WARN(("RTMemAlloc failed"));
                return VERR_NO_MEMORY;
            }
            memcpy (paTranslatedRects, paRegions, sizeof (RTRECT) * cRegions);
            for (uint32_t i = 0; i < cRegions; ++i)
            {
                VBoxRectTranslate(&paTranslatedRects[i], pEntry->Rect.xLeft, pEntry->Rect.yTop);
                paRegions = paTranslatedRects;
            }
        }
    }

    rc = crVrScrCompositorEntryRegionsSet(pCompositor, pEntry, cRegions, paRegions, &fChanged);
    if (!RT_SUCCESS(rc))
    {
        WARN(("crVrScrCompositorEntryRegionsSet failed, rc %d", rc));
        return rc;
    }

    if (fChanged && CrVrScrCompositorEntryIsUsed(pEntry))
    {
        rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, NULL);
        if (!RT_SUCCESS(rc))
        {
            WARN(("crVrScrCompositorEntryEnsureRegionsBounds failed, rc %d", rc));
            return rc;
        }
    }

    if (pfChanged)
        *pfChanged = fPosChanged || fChanged || fWasInList;

    return VINF_SUCCESS;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryListIntersect(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, const VBOXVR_LIST *pList2, bool *pfChanged)
{
    bool fChanged = false;
    int rc = VBoxVrCompositorEntryListIntersect(&pCompositor->Compositor, &pEntry->Ce, pList2, &fChanged);
    if (!RT_SUCCESS(rc))
    {
        WARN(("RegionsIntersect: VBoxVrCompositorEntryRegionsIntersect failed rc %d", rc));
        return rc;
    }

    if (fChanged)
    {
        CrVrScrCompositorEntrySetChanged(pEntry, true);
        crVrScrCompositorRectsInvalidate(pCompositor);
    }

    if (pfChanged)
        *pfChanged = fChanged;

    return VINF_SUCCESS;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryRegionsIntersect(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, uint32_t cRegions, const RTRECT *paRegions, bool *pfChanged)
{
    bool fChanged = false;
    int rc = VBoxVrCompositorEntryRegionsIntersect(&pCompositor->Compositor, &pEntry->Ce, cRegions, paRegions, &fChanged);
    if (!RT_SUCCESS(rc))
    {
        WARN(("RegionsIntersect: VBoxVrCompositorEntryRegionsIntersect failed rc %d", rc));
        return rc;
    }

    if (fChanged)
        crVrScrCompositorRectsInvalidate(pCompositor);

    if (pfChanged)
        *pfChanged = fChanged;

    return VINF_SUCCESS;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryListIntersectAll(PVBOXVR_SCR_COMPOSITOR pCompositor, const VBOXVR_LIST *pList2, bool *pfChanged)
{
    VBOXVR_SCR_COMPOSITOR_ITERATOR Iter;
    CrVrScrCompositorIterInit(pCompositor, &Iter);
    PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry;
    int rc = VINF_SUCCESS;
    bool fChanged = false;

    while ((pEntry = CrVrScrCompositorIterNext(&Iter)) != NULL)
    {
        bool fTmpChanged = false;
        int tmpRc = CrVrScrCompositorEntryListIntersect(pCompositor, pEntry, pList2, &fTmpChanged);
        if (RT_SUCCESS(tmpRc))
        {
            fChanged |= fTmpChanged;
        }
        else
        {
            WARN(("CrVrScrCompositorEntryRegionsIntersect failed, rc %d", tmpRc));
            rc = tmpRc;
        }
    }

    if (pfChanged)
        *pfChanged = fChanged;

    return rc;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryRegionsIntersectAll(PVBOXVR_SCR_COMPOSITOR pCompositor, uint32_t cRegions, const RTRECT *paRegions, bool *pfChanged)
{
    VBOXVR_SCR_COMPOSITOR_ITERATOR Iter;
    CrVrScrCompositorIterInit(pCompositor, &Iter);
    PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry;
    int rc = VINF_SUCCESS;
    bool fChanged = false;

    while ((pEntry = CrVrScrCompositorIterNext(&Iter)) != NULL)
    {
        bool fTmpChanged = false;
        int tmpRc = CrVrScrCompositorEntryRegionsIntersect(pCompositor, pEntry, cRegions, paRegions, &fTmpChanged);
        if (RT_SUCCESS(tmpRc))
        {
            fChanged |= fTmpChanged;
        }
        else
        {
            WARN(("CrVrScrCompositorEntryRegionsIntersect failed, rc %d", tmpRc));
            rc = tmpRc;
        }
    }

    if (pfChanged)
        *pfChanged = fChanged;

    return rc;
}

VBOXVREGDECL(int) CrVrScrCompositorEntryPosSet(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, const RTPOINT *pPos)
{
    int rc = crVrScrCompositorEntryPositionSet(pCompositor, pEntry, pPos, NULL);
    if (!RT_SUCCESS(rc))
    {
        WARN(("RegionsSet: crVrScrCompositorEntryPositionSet failed rc %d", rc));
        return rc;
    }

    rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, NULL);
    if (!RT_SUCCESS(rc))
    {
        WARN(("RegionsSet: crVrScrCompositorEntryEnsureRegionsBounds failed rc %d", rc));
        return rc;
    }

    return VINF_SUCCESS;
}

/* regions are valid until the next CrVrScrCompositor call */
VBOXVREGDECL(int) CrVrScrCompositorEntryRegionsGet(const VBOXVR_SCR_COMPOSITOR *pCompositor, const VBOXVR_SCR_COMPOSITOR_ENTRY *pEntry, uint32_t *pcRegions, const RTRECT **ppaSrcRegions, const RTRECT **ppaDstRegions, const RTRECT **ppaDstUnstretchedRects)
{
	if (CrVrScrCompositorEntryIsUsed(pEntry))
	{
		int rc = crVrScrCompositorRectsCheckInit(pCompositor);
		if (!RT_SUCCESS(rc))
		{
			WARN(("crVrScrCompositorRectsCheckInit failed, rc %d", rc));
			return rc;
		}
	}

    Assert(pCompositor->cRects != VBOXVR_SCR_COMPOSITOR_RECTS_UNDEFINED);

    *pcRegions = pEntry->cRects;
    if (ppaSrcRegions)
        *ppaSrcRegions = pEntry->paSrcRects;
    if (ppaDstRegions)
        *ppaDstRegions = pEntry->paDstRects;
    if (ppaDstUnstretchedRects)
        *ppaDstUnstretchedRects = pEntry->paDstUnstretchedRects;

    return VINF_SUCCESS;
}

VBOXVREGDECL(uint32_t) CrVrScrCompositorEntryFlagsCombinedGet(const VBOXVR_SCR_COMPOSITOR *pCompositor, const VBOXVR_SCR_COMPOSITOR_ENTRY *pEntry)
{
    return CRBLT_FOP_COMBINE(pCompositor->fFlags, pEntry->fFlags);
}

VBOXVREGDECL(void) CrVrScrCompositorEntryFlagsSet(PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, uint32_t fFlags)
{
    if (pEntry->fFlags == fFlags)
        return;

    pEntry->fFlags = fFlags;
    CrVrScrCompositorEntrySetChanged(pEntry, true);
}

static void crVrScrCompositorEntryDataCleanup(PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    pEntry->cRects = 0;
    pEntry->paSrcRects = NULL;
    pEntry->paDstRects = NULL;
    pEntry->paDstUnstretchedRects = NULL;
}

static void crVrScrCompositorEntryDataCopy(PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, PVBOXVR_SCR_COMPOSITOR_ENTRY pToEntry)
{
    pToEntry->cRects = pEntry->cRects;
    pToEntry->paSrcRects = pEntry->paSrcRects;
    pToEntry->paDstRects = pEntry->paDstRects;
    pToEntry->paDstUnstretchedRects = pEntry->paDstUnstretchedRects;
    crVrScrCompositorEntryDataCleanup(pEntry);
}

VBOXVREGDECL(int) CrVrScrCompositorEntryRemove(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    if (!VBoxVrCompositorEntryRemove(&pCompositor->Compositor, &pEntry->Ce))
        return VINF_SUCCESS;

    CrVrScrCompositorEntrySetChanged(pEntry, true);
    crVrScrCompositorEntryDataCleanup(pEntry);

    crVrScrCompositorRectsInvalidate(pCompositor);
    return VINF_SUCCESS;
}

VBOXVREGDECL(bool) CrVrScrCompositorEntryReplace(PVBOXVR_SCR_COMPOSITOR pCompositor, PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry, PVBOXVR_SCR_COMPOSITOR_ENTRY pNewEntry)
{
    Assert(!CrVrScrCompositorEntryIsUsed(pNewEntry));

    if (!VBoxVrCompositorEntryReplace(&pCompositor->Compositor, &pEntry->Ce, &pNewEntry->Ce))
        return false;

    CrVrScrCompositorEntrySetChanged(pEntry, true);
    crVrScrCompositorEntryDataCopy(pEntry, pNewEntry);
    CrVrScrCompositorEntrySetChanged(pNewEntry, true);

    return true;
}

static DECLCALLBACK(void) crVrScrCompositorEntryReleasedCB(const struct VBOXVR_COMPOSITOR *pCompositor, PVBOXVR_COMPOSITOR_ENTRY pEntry, PVBOXVR_COMPOSITOR_ENTRY pReplacingEntry)
{
    PVBOXVR_SCR_COMPOSITOR_ENTRY pCEntry = VBOXVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pEntry);

    CrVrScrCompositorEntrySetChanged(pCEntry, true);

    Assert(!CrVrScrCompositorEntryIsInList(pCEntry));

    if (pReplacingEntry)
    {
        PVBOXVR_SCR_COMPOSITOR_ENTRY pCReplacingEntry = VBOXVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pReplacingEntry);
        Assert(CrVrScrCompositorEntryIsInList(pCReplacingEntry));
        pCReplacingEntry->cRects = pCEntry->cRects;
        pCReplacingEntry->paSrcRects = pCEntry->paSrcRects;
        pCReplacingEntry->paDstRects = pCEntry->paDstRects;
        pCReplacingEntry->paDstUnstretchedRects = pCEntry->paDstUnstretchedRects;
    }

    if (pCEntry->pfnEntryReleased)
    {
        PVBOXVR_SCR_COMPOSITOR_ENTRY pCReplacingEntry = pReplacingEntry ? VBOXVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pReplacingEntry) : NULL;
        PVBOXVR_SCR_COMPOSITOR pCConpositor = VBOXVR_SCR_COMPOSITOR_FROM_COMPOSITOR(pCompositor);
        pCEntry->pfnEntryReleased(pCConpositor, pCEntry, pCReplacingEntry);
    }
}

VBOXVREGDECL(int) CrVrScrCompositorRectSet(PVBOXVR_SCR_COMPOSITOR pCompositor, const RTRECT *pRect, bool *pfChanged)
{
    if (!memcmp(&pCompositor->Rect, pRect, sizeof (pCompositor->Rect)))
    {
        if (pfChanged)
            *pfChanged = false;
        return VINF_SUCCESS;
    }

    pCompositor->Rect = *pRect;

    VBOXVR_SCR_COMPOSITOR_ITERATOR Iter;
    CrVrScrCompositorIterInit(pCompositor, &Iter);
    PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry;
    while ((pEntry = CrVrScrCompositorIterNext(&Iter)) != NULL)
    {
        int rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, NULL);
        if (!RT_SUCCESS(rc))
        {
            WARN(("crVrScrCompositorEntryEnsureRegionsBounds failed, rc %d", rc));
            return rc;
        }
    }

    return VINF_SUCCESS;
}

VBOXVREGDECL(void) CrVrScrCompositorInit(PVBOXVR_SCR_COMPOSITOR pCompositor, const RTRECT *pRect)
{
    memset(pCompositor, 0, sizeof (*pCompositor));
    VBoxVrCompositorInit(&pCompositor->Compositor, crVrScrCompositorEntryReleasedCB);
    pCompositor->fFlags = CRBLT_F_LINEAR | CRBLT_F_INVERT_YCOORDS;
    if (pRect)
        pCompositor->Rect = *pRect;
#ifndef IN_RING0
    pCompositor->StretchX = 1.0;
    pCompositor->StretchY = 1.0;
#endif
}

VBOXVREGDECL(void) CrVrScrCompositorRegionsClear(PVBOXVR_SCR_COMPOSITOR pCompositor, bool *pfChanged)
{
    /* set changed flag first, while entries are in the list and we have them */
    CrVrScrCompositorEntrySetAllChanged(pCompositor, true);
    VBoxVrCompositorRegionsClear(&pCompositor->Compositor, pfChanged);
    crVrScrCompositorRectsInvalidate(pCompositor);
}

VBOXVREGDECL(void) CrVrScrCompositorClear(PVBOXVR_SCR_COMPOSITOR pCompositor)
{
    CrVrScrCompositorRegionsClear(pCompositor, NULL);
    if (pCompositor->paDstRects)
    {
        RTMemFree(pCompositor->paDstRects);
        pCompositor->paDstRects = NULL;
    }
    if (pCompositor->paSrcRects)
    {
        RTMemFree(pCompositor->paSrcRects);
        pCompositor->paSrcRects = NULL;
    }
    if (pCompositor->paDstUnstretchedRects)
    {
        RTMemFree(pCompositor->paDstUnstretchedRects);
        pCompositor->paDstUnstretchedRects = NULL;
    }

    pCompositor->cRects = 0;
    pCompositor->cRectsBuffer = 0;
}

VBOXVREGDECL(void) CrVrScrCompositorEntrySetAllChanged(PVBOXVR_SCR_COMPOSITOR pCompositor, bool fChanged)
{
    VBOXVR_SCR_COMPOSITOR_ITERATOR CIter;
    PVBOXVR_SCR_COMPOSITOR_ENTRY pCurEntry;
    CrVrScrCompositorIterInit(pCompositor, &CIter);

    while ((pCurEntry = CrVrScrCompositorIterNext(&CIter)) != NULL)
    {
        CrVrScrCompositorEntrySetChanged(pCurEntry, fChanged);
    }
}

#ifndef IN_RING0
VBOXVREGDECL(void) CrVrScrCompositorSetStretching(PVBOXVR_SCR_COMPOSITOR pCompositor, float StretchX, float StretchY)
{
    if (pCompositor->StretchX == StretchX && pCompositor->StretchY == StretchY)
        return;

    pCompositor->StretchX = StretchX;
    pCompositor->StretchY = StretchY;
    crVrScrCompositorRectsInvalidate(pCompositor);
    CrVrScrCompositorEntrySetAllChanged(pCompositor, true);
}
#endif

/* regions are valid until the next CrVrScrCompositor call */
VBOXVREGDECL(int) CrVrScrCompositorRegionsGet(const VBOXVR_SCR_COMPOSITOR *pCompositor, uint32_t *pcRegions, const RTRECT **ppaSrcRegions, const RTRECT **ppaDstRegions, const RTRECT **ppaDstUnstretchedRects)
{
    int rc = crVrScrCompositorRectsCheckInit(pCompositor);
    if (!RT_SUCCESS(rc))
    {
        WARN(("crVrScrCompositorRectsCheckInit failed, rc %d", rc));
        return rc;
    }

    Assert(pCompositor->cRects != VBOXVR_SCR_COMPOSITOR_RECTS_UNDEFINED);

    *pcRegions = pCompositor->cRects;
    if (ppaSrcRegions)
        *ppaSrcRegions = pCompositor->paSrcRects;
    if (ppaDstRegions)
        *ppaDstRegions = pCompositor->paDstRects;
    if (ppaDstUnstretchedRects)
        *ppaDstUnstretchedRects = pCompositor->paDstUnstretchedRects;

    return VINF_SUCCESS;
}

typedef struct VBOXVR_SCR_COMPOSITOR_VISITOR_CB
{
    PFNVBOXVRSCRCOMPOSITOR_VISITOR pfnVisitor;
    void *pvVisitor;
} VBOXVR_SCR_COMPOSITOR_VISITOR_CB, *PVBOXVR_SCR_COMPOSITOR_VISITOR_CB;

static DECLCALLBACK(bool) crVrScrCompositorVisitCb(PVBOXVR_COMPOSITOR pCCompositor, PVBOXVR_COMPOSITOR_ENTRY pCEntry, void *pvVisitor)
{
    PVBOXVR_SCR_COMPOSITOR_VISITOR_CB pData = (PVBOXVR_SCR_COMPOSITOR_VISITOR_CB)pvVisitor;
    PVBOXVR_SCR_COMPOSITOR pCompositor = VBOXVR_SCR_COMPOSITOR_FROM_COMPOSITOR(pCCompositor);
    PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry = VBOXVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pCEntry);
    return pData->pfnVisitor(pCompositor, pEntry, pData->pvVisitor);
}

VBOXVREGDECL(void) CrVrScrCompositorVisit(PVBOXVR_SCR_COMPOSITOR pCompositor, PFNVBOXVRSCRCOMPOSITOR_VISITOR pfnVisitor, void *pvVisitor)
{
    VBOXVR_SCR_COMPOSITOR_VISITOR_CB Data;
    Data.pfnVisitor = pfnVisitor;
    Data.pvVisitor = pvVisitor;
    VBoxVrCompositorVisit(&pCompositor->Compositor, crVrScrCompositorVisitCb, &Data);
}

VBOXVREGDECL(int) CrVrScrCompositorClone(const VBOXVR_SCR_COMPOSITOR *pCompositor, PVBOXVR_SCR_COMPOSITOR pDstCompositor, PFNVBOXVR_SCR_COMPOSITOR_ENTRY_FOR pfnEntryFor, void* pvEntryFor)
{
    /* for simplicity just copy from one to another */
    CrVrScrCompositorInit(pDstCompositor, CrVrScrCompositorRectGet(pCompositor));
    VBOXVR_SCR_COMPOSITOR_CONST_ITERATOR CIter;
    const VBOXVR_SCR_COMPOSITOR_ENTRY *pEntry;
    CrVrScrCompositorConstIterInit(pCompositor, &CIter);
    int rc = VINF_SUCCESS;
    uint32_t cRects;
    const RTRECT *pRects;

    while ((pEntry = CrVrScrCompositorConstIterNext(&CIter)) != NULL)
    {
        /* get source rects, that will be non-stretched and entry pos - pased */
        rc = CrVrScrCompositorEntryRegionsGet(pCompositor, pEntry, &cRects, NULL, NULL, &pRects);
        if (!RT_SUCCESS(rc))
        {
            WARN(("CrVrScrCompositorEntryRegionsGet failed, rc %d", rc));
            return rc;
        }

        PVBOXVR_SCR_COMPOSITOR_ENTRY pDstEntry = pfnEntryFor(pEntry, pvEntryFor);
        if (!pDstEntry)
        {
            WARN(("pfnEntryFor failed"));
            return VERR_INVALID_STATE;
        }

        rc = CrVrScrCompositorEntryRegionsSet(pDstCompositor, pDstEntry, NULL, cRects, pRects, false, NULL);
        if (!RT_SUCCESS(rc))
        {
            WARN(("CrVrScrCompositorEntryRegionsSet failed, rc %d", rc));
            return rc;
        }
    }

    return rc;
}

VBOXVREGDECL(int) CrVrScrCompositorIntersectList(PVBOXVR_SCR_COMPOSITOR pCompositor, const VBOXVR_LIST *pVr, bool *pfChanged)
{
    VBOXVR_SCR_COMPOSITOR_ITERATOR CIter;
    PVBOXVR_SCR_COMPOSITOR_ENTRY pEntry;
    CrVrScrCompositorIterInit(pCompositor, &CIter);
    int rc = VINF_SUCCESS;
    bool fChanged = false;

    while ((pEntry = CrVrScrCompositorIterNext(&CIter)) != NULL)
    {
        bool fCurChanged = false;

        rc = CrVrScrCompositorEntryListIntersect(pCompositor, pEntry, pVr, &fCurChanged);
        if (!RT_SUCCESS(rc))
        {
            WARN(("CrVrScrCompositorEntryRegionsSet failed, rc %d", rc));
            break;
        }

        fChanged |= fCurChanged;
    }

    if (pfChanged)
        *pfChanged = fChanged;

    return rc;
}

VBOXVREGDECL(int) CrVrScrCompositorIntersectedList(const VBOXVR_SCR_COMPOSITOR *pCompositor, const VBOXVR_LIST *pVr, PVBOXVR_SCR_COMPOSITOR pDstCompositor, PFNVBOXVR_SCR_COMPOSITOR_ENTRY_FOR pfnEntryFor, void* pvEntryFor, bool *pfChanged)
{
    int rc  = CrVrScrCompositorClone(pCompositor, pDstCompositor, pfnEntryFor, pvEntryFor);
    if (!RT_SUCCESS(rc))
    {
        WARN(("CrVrScrCompositorClone failed, rc %d", rc));
        return rc;
    }

    rc = CrVrScrCompositorIntersectList(pDstCompositor, pVr, pfChanged);
    if (!RT_SUCCESS(rc))
    {
        WARN(("CrVrScrCompositorIntersectList failed, rc %d", rc));
        CrVrScrCompositorClear(pDstCompositor);
        return rc;
    }

    return VINF_SUCCESS;
}

