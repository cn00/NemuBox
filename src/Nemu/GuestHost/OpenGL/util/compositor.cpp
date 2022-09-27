/* $Id: compositor.cpp $ */
/** @file
 * Compositor implementation.
 */

/*
 * Copyright (C) 2013-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "../include/cr_compositor.h"


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define NEMUVR_SCR_COMPOSITOR_RECTS_UNDEFINED UINT32_MAX
#ifdef IN_VMSVGA3D
# define WARN AssertMsgFailed
#endif



static int crVrScrCompositorRectsAssignBuffer(PNEMUVR_SCR_COMPOSITOR pCompositor, uint32_t cRects)
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

    pCompositor->paSrcRects = (PRTRECT)RTMemAlloc(sizeof(*pCompositor->paSrcRects) * cRects);
    if (pCompositor->paSrcRects)
    {
        pCompositor->paDstRects = (PRTRECT)RTMemAlloc(sizeof(*pCompositor->paDstRects) * cRects);
        if (pCompositor->paDstRects)
        {
            pCompositor->paDstUnstretchedRects = (PRTRECT)RTMemAlloc(sizeof(*pCompositor->paDstUnstretchedRects) * cRects);
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

    pCompositor->cRects = NEMUVR_SCR_COMPOSITOR_RECTS_UNDEFINED;
    pCompositor->cRectsBuffer = 0;

    return VERR_NO_MEMORY;
}

static void crVrScrCompositorRectsInvalidate(PNEMUVR_SCR_COMPOSITOR pCompositor)
{
    pCompositor->cRects = NEMUVR_SCR_COMPOSITOR_RECTS_UNDEFINED;
}

static DECLCALLBACK(bool) crVrScrCompositorRectsCounterCb(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                          void *pvVisitor)
{
    uint32_t* pCounter = (uint32_t*)pvVisitor;
    Assert(NemuVrListRectsCount(&pEntry->Vr));
    *pCounter += NemuVrListRectsCount(&pEntry->Vr);
    return true;
}

typedef struct NEMUVR_SCR_COMPOSITOR_RECTS_ASSIGNER
{
    PRTRECT paSrcRects;
    PRTRECT paDstRects;
    PRTRECT paDstUnstretchedRects;
    uint32_t cRects;
} NEMUVR_SCR_COMPOSITOR_RECTS_ASSIGNER, *PNEMUVR_SCR_COMPOSITOR_RECTS_ASSIGNER;

static DECLCALLBACK(bool) crVrScrCompositorRectsAssignerCb(PNEMUVR_COMPOSITOR pCCompositor, PNEMUVR_COMPOSITOR_ENTRY pCEntry,
                                                           void *pvVisitor)
{
    PNEMUVR_SCR_COMPOSITOR_RECTS_ASSIGNER pData = (PNEMUVR_SCR_COMPOSITOR_RECTS_ASSIGNER)pvVisitor;
    PNEMUVR_SCR_COMPOSITOR pCompositor = NEMUVR_SCR_COMPOSITOR_FROM_COMPOSITOR(pCCompositor);
    PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry = NEMUVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pCEntry);
    pEntry->paSrcRects = pData->paSrcRects;
    pEntry->paDstRects = pData->paDstRects;
    pEntry->paDstUnstretchedRects = pData->paDstUnstretchedRects;
    uint32_t cRects = NemuVrListRectsCount(&pCEntry->Vr);
    Assert(cRects);
    Assert(cRects <= pData->cRects);
    int rc = NemuVrListRectsGet(&pCEntry->Vr, cRects, pEntry->paDstUnstretchedRects);
    AssertRC(rc);

    if (!pEntry->Rect.xLeft && !pEntry->Rect.yTop)
    {
        memcpy(pEntry->paSrcRects, pEntry->paDstUnstretchedRects, cRects * sizeof(*pEntry->paSrcRects));
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
        memcpy(pEntry->paDstRects, pEntry->paDstUnstretchedRects, cRects * sizeof(*pEntry->paDstUnstretchedRects));
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

static int crVrScrCompositorRectsCheckInit(PCNEMUVR_SCR_COMPOSITOR pcCompositor)
{
    PNEMUVR_SCR_COMPOSITOR pCompositor = const_cast<PNEMUVR_SCR_COMPOSITOR>(pcCompositor);

    if (pCompositor->cRects != NEMUVR_SCR_COMPOSITOR_RECTS_UNDEFINED)
        return VINF_SUCCESS;

    uint32_t cRects = 0;
    NemuVrCompositorVisit(&pCompositor->Compositor, crVrScrCompositorRectsCounterCb, &cRects);

    if (!cRects)
    {
        pCompositor->cRects = 0;
        return VINF_SUCCESS;
    }

    int rc = crVrScrCompositorRectsAssignBuffer(pCompositor, cRects);
    if (RT_FAILURE(rc))
        return rc;

    NEMUVR_SCR_COMPOSITOR_RECTS_ASSIGNER AssignerData;
    AssignerData.paSrcRects = pCompositor->paSrcRects;
    AssignerData.paDstRects = pCompositor->paDstRects;
    AssignerData.paDstUnstretchedRects = pCompositor->paDstUnstretchedRects;
    AssignerData.cRects = pCompositor->cRects;
    NemuVrCompositorVisit(&pCompositor->Compositor, crVrScrCompositorRectsAssignerCb, &AssignerData);
    Assert(!AssignerData.cRects);
    return VINF_SUCCESS;
}


static int crVrScrCompositorEntryRegionsAdd(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                            uint32_t cRegions, PCRTRECT paRegions,
                                            NEMUVR_SCR_COMPOSITOR_ENTRY **ppReplacedScrEntry, uint32_t *pfChangedFlags)
{
    uint32_t fChangedFlags = 0;
    PNEMUVR_COMPOSITOR_ENTRY pReplacedEntry;
    int rc = NemuVrCompositorEntryRegionsAdd(&pCompositor->Compositor, pEntry ? &pEntry->Ce : NULL, cRegions,
                                             paRegions, &pReplacedEntry, &fChangedFlags);
    if (RT_FAILURE(rc))
    {
        WARN(("NemuVrCompositorEntryRegionsAdd failed, rc %d", rc));
        return rc;
    }

    NEMUVR_SCR_COMPOSITOR_ENTRY *pReplacedScrEntry = NEMUVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pReplacedEntry);

    if (fChangedFlags & NEMUVR_COMPOSITOR_CF_REGIONS_CHANGED)
        crVrScrCompositorRectsInvalidate(pCompositor);
    else if (fChangedFlags & NEMUVR_COMPOSITOR_CF_ENTRY_REPLACED)
        Assert(pReplacedScrEntry);

    if (fChangedFlags & NEMUVR_COMPOSITOR_CF_OTHER_ENTRIES_REGIONS_CHANGED)
        CrVrScrCompositorEntrySetAllChanged(pCompositor, true);
    else if ((fChangedFlags & NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED) && pEntry)
        CrVrScrCompositorEntrySetChanged(pEntry, true);

    if (pfChangedFlags)
        *pfChangedFlags = fChangedFlags;

    if (ppReplacedScrEntry)
        *ppReplacedScrEntry = pReplacedScrEntry;

    return VINF_SUCCESS;
}

static int crVrScrCompositorEntryRegionsSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                            uint32_t cRegions, PCRTRECT paRegions, bool *pfChanged)
{
    bool fChanged;
    int rc = NemuVrCompositorEntryRegionsSet(&pCompositor->Compositor, &pEntry->Ce, cRegions, paRegions, &fChanged);
    if (RT_FAILURE(rc))
    {
        WARN(("NemuVrCompositorEntryRegionsSet failed, rc %d", rc));
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

static int crVrScrCompositorEntryPositionSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                             PCRTPOINT pPos, bool *pfChanged)
{
    if (pfChanged)
        *pfChanged = false;
    if (pEntry && (pEntry->Rect.xLeft != pPos->x || pEntry->Rect.yTop != pPos->y))
    {
        if (NemuVrCompositorEntryIsInList(&pEntry->Ce))
        {
            int rc = NemuVrCompositorEntryRegionsTranslate(&pCompositor->Compositor, &pEntry->Ce, pPos->x - pEntry->Rect.xLeft,
                                                           pPos->y - pEntry->Rect.yTop, pfChanged);
            if (RT_FAILURE(rc))
            {
                WARN(("NemuVrCompositorEntryRegionsTranslate failed rc %d", rc));
                return rc;
            }

            crVrScrCompositorRectsInvalidate(pCompositor);
        }

        NemuRectMove(&pEntry->Rect, pPos->x, pPos->y);
        CrVrScrCompositorEntrySetChanged(pEntry, true);

        if (pfChanged)
            *pfChanged = true;
    }
    return VINF_SUCCESS;
}

static int crVrScrCompositorEntryEnsureRegionsBounds(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                     bool *pfChanged)
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
    if (RT_FAILURE(rc))
        WARN(("CrVrScrCompositorEntryRegionsIntersect failed, rc %d", rc));

    if (pfChanged)
        *pfChanged = fChanged;
    return rc;
}

NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsAdd(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                   PCRTPOINT pPos, uint32_t cRegions, PCRTRECT paRegions,
                                                   bool fPosRelated, NEMUVR_SCR_COMPOSITOR_ENTRY **ppReplacedScrEntry,
                                                   uint32_t *pfChangeFlags)
{
    int rc;
    uint32_t fChangeFlags = 0;
    bool fPosChanged = false;
    RTRECT *paTranslatedRects = NULL;
    if (pPos)
    {
        rc = crVrScrCompositorEntryPositionSet(pCompositor, pEntry, pPos, &fPosChanged);
        if (RT_FAILURE(rc))
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
            paTranslatedRects = (RTRECT*)RTMemAlloc(sizeof(RTRECT) * cRegions);
            if (!paTranslatedRects)
            {
                WARN(("RTMemAlloc failed"));
                return VERR_NO_MEMORY;
            }
            memcpy (paTranslatedRects, paRegions, sizeof(RTRECT) * cRegions);
            for (uint32_t i = 0; i < cRegions; ++i)
            {
                NemuRectTranslate(&paTranslatedRects[i], pEntry->Rect.xLeft, pEntry->Rect.yTop);
                paRegions = paTranslatedRects;
            }
        }
    }

    rc = crVrScrCompositorEntryRegionsAdd(pCompositor, pEntry, cRegions, paRegions, ppReplacedScrEntry, &fChangeFlags);
    if (RT_FAILURE(rc))
    {
        WARN(("crVrScrCompositorEntryRegionsAdd failed, rc %d", rc));
        goto done;
    }

    if ((fPosChanged || (fChangeFlags & NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED)) && pEntry)
    {
        bool fAdjusted = false;
        rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, &fAdjusted);
        if (RT_FAILURE(rc))
        {
            WARN(("crVrScrCompositorEntryEnsureRegionsBounds failed, rc %d", rc));
            goto done;
        }

        if (fAdjusted)
        {
            if (CrVrScrCompositorEntryIsUsed(pEntry))
            {
                fChangeFlags &= ~NEMUVR_COMPOSITOR_CF_ENTRY_REPLACED;
                fChangeFlags |= NEMUVR_COMPOSITOR_CF_REGIONS_CHANGED | NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED;
            }
            else
            {
                fChangeFlags = 0;
            }
        }
    }

    if (fChangeFlags & NEMUVR_COMPOSITOR_CF_ENTRY_REPLACED)
        fPosChanged = false;
    else if (ppReplacedScrEntry)
        *ppReplacedScrEntry = NULL;

    if (pfChangeFlags)
    {
        if (fPosChanged)
        {
            /* means entry was in list and was moved, so regions changed */
            *pfChangeFlags = NEMUVR_COMPOSITOR_CF_REGIONS_CHANGED | NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED
                           | NEMUVR_COMPOSITOR_CF_OTHER_ENTRIES_REGIONS_CHANGED;
        }
        else
            *pfChangeFlags = fChangeFlags;
    }

done:

    if (paTranslatedRects)
        RTMemFree(paTranslatedRects);

    return rc;
}

NEMUVREGDECL(int) CrVrScrCompositorEntryRectSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                PCRTRECT pRect)
{
    if (!memcmp(&pEntry->Rect, pRect, sizeof(*pRect)))
    {
        return VINF_SUCCESS;
    }
    RTPOINT Point = {pRect->xLeft, pRect->yTop};
    bool fChanged = false;
    int rc = crVrScrCompositorEntryPositionSet(pCompositor, pEntry, &Point, &fChanged);
    if (RT_FAILURE(rc))
    {
        WARN(("crVrScrCompositorEntryPositionSet failed %d", rc));
        return rc;
    }

    pEntry->Rect = *pRect;

    if (!CrVrScrCompositorEntryIsUsed(pEntry))
        return VINF_SUCCESS;

    rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, NULL);
    if (RT_FAILURE(rc))
    {
        WARN(("crVrScrCompositorEntryEnsureRegionsBounds failed, rc %d", rc));
        return rc;
    }

    return VINF_SUCCESS;
}

NEMUVREGDECL(int) CrVrScrCompositorEntryTexAssign(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                  CR_TEXDATA *pTex)
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

NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                   PCRTPOINT pPos, uint32_t cRegions, PCRTRECT paRegions,
                                                   bool fPosRelated, bool *pfChanged)
{
    /* @todo: the fChanged sate calculation is really rough now, this is enough for now though */
    bool fChanged = false, fPosChanged = false;
    bool fWasInList = CrVrScrCompositorEntryIsInList(pEntry);
    RTRECT *paTranslatedRects = NULL;
    int rc = CrVrScrCompositorEntryRemove(pCompositor, pEntry);
    if (RT_FAILURE(rc))
    {
        WARN(("RegionsSet: CrVrScrCompositorEntryRemove failed rc %d", rc));
        return rc;
    }

    if (pPos)
    {
        rc = crVrScrCompositorEntryPositionSet(pCompositor, pEntry, pPos, &fPosChanged);
        if (RT_FAILURE(rc))
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
            paTranslatedRects = (RTRECT*)RTMemAlloc(sizeof(RTRECT) * cRegions);
            if (!paTranslatedRects)
            {
                WARN(("RTMemAlloc failed"));
                return VERR_NO_MEMORY;
            }
            memcpy (paTranslatedRects, paRegions, sizeof(RTRECT) * cRegions);
            for (uint32_t i = 0; i < cRegions; ++i)
            {
                NemuRectTranslate(&paTranslatedRects[i], pEntry->Rect.xLeft, pEntry->Rect.yTop);
                paRegions = paTranslatedRects;
            }
        }
    }

    rc = crVrScrCompositorEntryRegionsSet(pCompositor, pEntry, cRegions, paRegions, &fChanged);
    if (RT_SUCCESS(rc))
    {
        if (fChanged && CrVrScrCompositorEntryIsUsed(pEntry))
        {
            rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, NULL);
            if (RT_SUCCESS(rc))
            {
                if (pfChanged)
                    *pfChanged = fPosChanged || fChanged || fWasInList;
            }
            else
                WARN(("crVrScrCompositorEntryEnsureRegionsBounds failed, rc %d", rc));
        }

    }
    else
        WARN(("crVrScrCompositorEntryRegionsSet failed, rc %d", rc));

    if (paTranslatedRects)
        RTMemFree(paTranslatedRects);

    return rc;
}

NEMUVREGDECL(int) CrVrScrCompositorEntryListIntersect(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                      PCNEMUVR_LIST pList2, bool *pfChanged)
{
    bool fChanged = false;
    int rc = NemuVrCompositorEntryListIntersect(&pCompositor->Compositor, &pEntry->Ce, pList2, &fChanged);
    if (RT_FAILURE(rc))
    {
        WARN(("RegionsIntersect: NemuVrCompositorEntryRegionsIntersect failed rc %d", rc));
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

NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsIntersect(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                         uint32_t cRegions, PCRTRECT paRegions, bool *pfChanged)
{
    bool fChanged = false;
    int rc = NemuVrCompositorEntryRegionsIntersect(&pCompositor->Compositor, &pEntry->Ce, cRegions, paRegions, &fChanged);
    if (RT_FAILURE(rc))
    {
        WARN(("RegionsIntersect: NemuVrCompositorEntryRegionsIntersect failed rc %d", rc));
        return rc;
    }

    if (fChanged)
        crVrScrCompositorRectsInvalidate(pCompositor);

    if (pfChanged)
        *pfChanged = fChanged;

    return VINF_SUCCESS;
}

NEMUVREGDECL(int) CrVrScrCompositorEntryListIntersectAll(PNEMUVR_SCR_COMPOSITOR pCompositor, PCNEMUVR_LIST pList2, bool *pfChanged)
{
    NEMUVR_SCR_COMPOSITOR_ITERATOR Iter;
    CrVrScrCompositorIterInit(pCompositor, &Iter);
    PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry;
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

NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsIntersectAll(PNEMUVR_SCR_COMPOSITOR pCompositor, uint32_t cRegions,
                                                            PCRTRECT paRegions, bool *pfChanged)
{
    NEMUVR_SCR_COMPOSITOR_ITERATOR Iter;
    CrVrScrCompositorIterInit(pCompositor, &Iter);
    PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry;
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

NEMUVREGDECL(int) CrVrScrCompositorEntryPosSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                               PCRTPOINT pPos)
{
    int rc = crVrScrCompositorEntryPositionSet(pCompositor, pEntry, pPos, NULL);
    if (RT_FAILURE(rc))
    {
        WARN(("RegionsSet: crVrScrCompositorEntryPositionSet failed rc %d", rc));
        return rc;
    }

    rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, NULL);
    if (RT_FAILURE(rc))
    {
        WARN(("RegionsSet: crVrScrCompositorEntryEnsureRegionsBounds failed rc %d", rc));
        return rc;
    }

    return VINF_SUCCESS;
}

/* regions are valid until the next CrVrScrCompositor call */
NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsGet(PCNEMUVR_SCR_COMPOSITOR pCompositor,
                                                   PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry, uint32_t *pcRegions,
                                                   PCRTRECT *ppaSrcRegions, PCRTRECT *ppaDstRegions,
                                                   PCRTRECT *ppaDstUnstretchedRects)
{
    if (CrVrScrCompositorEntryIsUsed(pEntry))
    {
        int rc = crVrScrCompositorRectsCheckInit(pCompositor);
        if (RT_FAILURE(rc))
        {
            WARN(("crVrScrCompositorRectsCheckInit failed, rc %d", rc));
            return rc;
        }
    }

    Assert(pCompositor->cRects != NEMUVR_SCR_COMPOSITOR_RECTS_UNDEFINED);

    *pcRegions = pEntry->cRects;
    if (ppaSrcRegions)
        *ppaSrcRegions = pEntry->paSrcRects;
    if (ppaDstRegions)
        *ppaDstRegions = pEntry->paDstRects;
    if (ppaDstUnstretchedRects)
        *ppaDstUnstretchedRects = pEntry->paDstUnstretchedRects;

    return VINF_SUCCESS;
}

NEMUVREGDECL(uint32_t) CrVrScrCompositorEntryFlagsCombinedGet(PCNEMUVR_SCR_COMPOSITOR pCompositor,
                                                              PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    return CRBLT_FOP_COMBINE(pCompositor->fFlags, pEntry->fFlags);
}

NEMUVREGDECL(void) CrVrScrCompositorEntryFlagsSet(PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry, uint32_t fFlags)
{
    if (pEntry->fFlags == fFlags)
        return;

    pEntry->fFlags = fFlags;
    CrVrScrCompositorEntrySetChanged(pEntry, true);
}

static void crVrScrCompositorEntryDataCleanup(PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    pEntry->cRects = 0;
    pEntry->paSrcRects = NULL;
    pEntry->paDstRects = NULL;
    pEntry->paDstUnstretchedRects = NULL;
}

static void crVrScrCompositorEntryDataCopy(PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry, PNEMUVR_SCR_COMPOSITOR_ENTRY pToEntry)
{
    pToEntry->cRects = pEntry->cRects;
    pToEntry->paSrcRects = pEntry->paSrcRects;
    pToEntry->paDstRects = pEntry->paDstRects;
    pToEntry->paDstUnstretchedRects = pEntry->paDstUnstretchedRects;
    crVrScrCompositorEntryDataCleanup(pEntry);
}

NEMUVREGDECL(int) CrVrScrCompositorEntryRemove(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    if (!NemuVrCompositorEntryRemove(&pCompositor->Compositor, &pEntry->Ce))
        return VINF_SUCCESS;

    CrVrScrCompositorEntrySetChanged(pEntry, true);
    crVrScrCompositorEntryDataCleanup(pEntry);

    crVrScrCompositorRectsInvalidate(pCompositor);
    return VINF_SUCCESS;
}

NEMUVREGDECL(bool) CrVrScrCompositorEntryReplace(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                 PNEMUVR_SCR_COMPOSITOR_ENTRY pNewEntry)
{
    Assert(!CrVrScrCompositorEntryIsUsed(pNewEntry));

    if (!NemuVrCompositorEntryReplace(&pCompositor->Compositor, &pEntry->Ce, &pNewEntry->Ce))
        return false;

    CrVrScrCompositorEntrySetChanged(pEntry, true);
    crVrScrCompositorEntryDataCopy(pEntry, pNewEntry);
    CrVrScrCompositorEntrySetChanged(pNewEntry, true);

    return true;
}

static DECLCALLBACK(void) crVrScrCompositorEntryReleasedCB(PCNEMUVR_COMPOSITOR pCompositor,
                                                           PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                           PNEMUVR_COMPOSITOR_ENTRY pReplacingEntry)
{
    PNEMUVR_SCR_COMPOSITOR_ENTRY pCEntry = NEMUVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pEntry);

    CrVrScrCompositorEntrySetChanged(pCEntry, true);

    Assert(!CrVrScrCompositorEntryIsInList(pCEntry));

    if (pReplacingEntry)
    {
        PNEMUVR_SCR_COMPOSITOR_ENTRY pCReplacingEntry = NEMUVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pReplacingEntry);
        Assert(CrVrScrCompositorEntryIsInList(pCReplacingEntry));
        pCReplacingEntry->cRects = pCEntry->cRects;
        pCReplacingEntry->paSrcRects = pCEntry->paSrcRects;
        pCReplacingEntry->paDstRects = pCEntry->paDstRects;
        pCReplacingEntry->paDstUnstretchedRects = pCEntry->paDstUnstretchedRects;
    }

    if (pCEntry->pfnEntryReleased)
    {
        PNEMUVR_SCR_COMPOSITOR_ENTRY pCReplacingEntry = pReplacingEntry
                                                      ? NEMUVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pReplacingEntry) : NULL;
        PNEMUVR_SCR_COMPOSITOR pCConpositor = NEMUVR_SCR_COMPOSITOR_FROM_COMPOSITOR(pCompositor);
        pCEntry->pfnEntryReleased(pCConpositor, pCEntry, pCReplacingEntry);
    }
}

NEMUVREGDECL(int) CrVrScrCompositorRectSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PCRTRECT pRect, bool *pfChanged)
{
    if (!memcmp(&pCompositor->Rect, pRect, sizeof(pCompositor->Rect)))
    {
        if (pfChanged)
            *pfChanged = false;
        return VINF_SUCCESS;
    }

    pCompositor->Rect = *pRect;

    NEMUVR_SCR_COMPOSITOR_ITERATOR Iter;
    CrVrScrCompositorIterInit(pCompositor, &Iter);
    PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry;
    while ((pEntry = CrVrScrCompositorIterNext(&Iter)) != NULL)
    {
        int rc = crVrScrCompositorEntryEnsureRegionsBounds(pCompositor, pEntry, NULL);
        if (RT_FAILURE(rc))
        {
            WARN(("crVrScrCompositorEntryEnsureRegionsBounds failed, rc %d", rc));
            return rc;
        }
    }

    return VINF_SUCCESS;
}

NEMUVREGDECL(void) CrVrScrCompositorInit(PNEMUVR_SCR_COMPOSITOR pCompositor, PCRTRECT pRect)
{
    memset(pCompositor, 0, sizeof(*pCompositor));
    NemuVrCompositorInit(&pCompositor->Compositor, crVrScrCompositorEntryReleasedCB);
    pCompositor->fFlags = CRBLT_F_LINEAR | CRBLT_F_INVERT_YCOORDS;
    if (pRect)
        pCompositor->Rect = *pRect;
#ifndef IN_RING0
    pCompositor->StretchX = 1.0;
    pCompositor->StretchY = 1.0;
#endif
}

NEMUVREGDECL(void) CrVrScrCompositorRegionsClear(PNEMUVR_SCR_COMPOSITOR pCompositor, bool *pfChanged)
{
    /* set changed flag first, while entries are in the list and we have them */
    CrVrScrCompositorEntrySetAllChanged(pCompositor, true);
    NemuVrCompositorRegionsClear(&pCompositor->Compositor, pfChanged);
    crVrScrCompositorRectsInvalidate(pCompositor);
}

NEMUVREGDECL(void) CrVrScrCompositorClear(PNEMUVR_SCR_COMPOSITOR pCompositor)
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

NEMUVREGDECL(void) CrVrScrCompositorEntrySetAllChanged(PNEMUVR_SCR_COMPOSITOR pCompositor, bool fChanged)
{
    NEMUVR_SCR_COMPOSITOR_ITERATOR CIter;
    PNEMUVR_SCR_COMPOSITOR_ENTRY pCurEntry;
    CrVrScrCompositorIterInit(pCompositor, &CIter);

    while ((pCurEntry = CrVrScrCompositorIterNext(&CIter)) != NULL)
    {
        CrVrScrCompositorEntrySetChanged(pCurEntry, fChanged);
    }
}

#ifndef IN_RING0
NEMUVREGDECL(void) CrVrScrCompositorSetStretching(PNEMUVR_SCR_COMPOSITOR pCompositor, float StretchX, float StretchY)
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
NEMUVREGDECL(int) CrVrScrCompositorRegionsGet(PCNEMUVR_SCR_COMPOSITOR pCompositor, uint32_t *pcRegions,
                                              PCRTRECT *ppaSrcRegions, PCRTRECT *ppaDstRegions,
                                              PCRTRECT *ppaDstUnstretchedRects)
{
    int rc = crVrScrCompositorRectsCheckInit(pCompositor);
    if (RT_FAILURE(rc))
    {
        WARN(("crVrScrCompositorRectsCheckInit failed, rc %d", rc));
        return rc;
    }

    Assert(pCompositor->cRects != NEMUVR_SCR_COMPOSITOR_RECTS_UNDEFINED);

    *pcRegions = pCompositor->cRects;
    if (ppaSrcRegions)
        *ppaSrcRegions = pCompositor->paSrcRects;
    if (ppaDstRegions)
        *ppaDstRegions = pCompositor->paDstRects;
    if (ppaDstUnstretchedRects)
        *ppaDstUnstretchedRects = pCompositor->paDstUnstretchedRects;

    return VINF_SUCCESS;
}

typedef struct NEMUVR_SCR_COMPOSITOR_VISITOR_CB
{
    PFNNEMUVRSCRCOMPOSITOR_VISITOR pfnVisitor;
    void *pvVisitor;
} NEMUVR_SCR_COMPOSITOR_VISITOR_CB, *PNEMUVR_SCR_COMPOSITOR_VISITOR_CB;

static DECLCALLBACK(bool) crVrScrCompositorVisitCb(PNEMUVR_COMPOSITOR pCCompositor, PNEMUVR_COMPOSITOR_ENTRY pCEntry,
                                                   void *pvVisitor)
{
    PNEMUVR_SCR_COMPOSITOR_VISITOR_CB pData = (PNEMUVR_SCR_COMPOSITOR_VISITOR_CB)pvVisitor;
    PNEMUVR_SCR_COMPOSITOR pCompositor = NEMUVR_SCR_COMPOSITOR_FROM_COMPOSITOR(pCCompositor);
    PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry = NEMUVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pCEntry);
    return pData->pfnVisitor(pCompositor, pEntry, pData->pvVisitor);
}

NEMUVREGDECL(void) CrVrScrCompositorVisit(PNEMUVR_SCR_COMPOSITOR pCompositor, PFNNEMUVRSCRCOMPOSITOR_VISITOR pfnVisitor,
                                          void *pvVisitor)
{
    NEMUVR_SCR_COMPOSITOR_VISITOR_CB Data;
    Data.pfnVisitor = pfnVisitor;
    Data.pvVisitor = pvVisitor;
    NemuVrCompositorVisit(&pCompositor->Compositor, crVrScrCompositorVisitCb, &Data);
}

NEMUVREGDECL(int) CrVrScrCompositorClone(PCNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR pDstCompositor,
                                         PFNNEMUVR_SCR_COMPOSITOR_ENTRY_FOR pfnEntryFor, void *pvEntryFor)
{
    /* for simplicity just copy from one to another */
    CrVrScrCompositorInit(pDstCompositor, CrVrScrCompositorRectGet(pCompositor));
    NEMUVR_SCR_COMPOSITOR_CONST_ITERATOR CIter;
    PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry;
    CrVrScrCompositorConstIterInit(pCompositor, &CIter);
    int rc = VINF_SUCCESS;
    uint32_t cRects;
    PCRTRECT paRects;

    while ((pEntry = CrVrScrCompositorConstIterNext(&CIter)) != NULL)
    {
        /* get source rects, that will be non-stretched and entry pos - pased */
        rc = CrVrScrCompositorEntryRegionsGet(pCompositor, pEntry, &cRects, NULL, NULL, &paRects);
        if (RT_FAILURE(rc))
        {
            WARN(("CrVrScrCompositorEntryRegionsGet failed, rc %d", rc));
            return rc;
        }

        PNEMUVR_SCR_COMPOSITOR_ENTRY pDstEntry = pfnEntryFor(pEntry, pvEntryFor);
        if (!pDstEntry)
        {
            WARN(("pfnEntryFor failed"));
            return VERR_INVALID_STATE;
        }

        rc = CrVrScrCompositorEntryRegionsSet(pDstCompositor, pDstEntry, NULL, cRects, paRects, false, NULL);
        if (RT_FAILURE(rc))
        {
            WARN(("CrVrScrCompositorEntryRegionsSet failed, rc %d", rc));
            return rc;
        }
    }

    return rc;
}

NEMUVREGDECL(int) CrVrScrCompositorIntersectList(PNEMUVR_SCR_COMPOSITOR pCompositor, PCNEMUVR_LIST pVr, bool *pfChanged)
{
    NEMUVR_SCR_COMPOSITOR_ITERATOR CIter;
    PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry;
    CrVrScrCompositorIterInit(pCompositor, &CIter);
    int rc = VINF_SUCCESS;
    bool fChanged = false;

    while ((pEntry = CrVrScrCompositorIterNext(&CIter)) != NULL)
    {
        bool fCurChanged = false;

        rc = CrVrScrCompositorEntryListIntersect(pCompositor, pEntry, pVr, &fCurChanged);
        if (RT_FAILURE(rc))
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

NEMUVREGDECL(int) CrVrScrCompositorIntersectedList(PCNEMUVR_SCR_COMPOSITOR pCompositor, PCNEMUVR_LIST pVr,
                                                   PNEMUVR_SCR_COMPOSITOR pDstCompositor,
                                                   PFNNEMUVR_SCR_COMPOSITOR_ENTRY_FOR pfnEntryFor, void *pvEntryFor,
                                                   bool *pfChanged)
{
    int rc = CrVrScrCompositorClone(pCompositor, pDstCompositor, pfnEntryFor, pvEntryFor);
    if (RT_FAILURE(rc))
    {
        WARN(("CrVrScrCompositorClone failed, rc %d", rc));
        return rc;
    }

    rc = CrVrScrCompositorIntersectList(pDstCompositor, pVr, pfChanged);
    if (RT_FAILURE(rc))
    {
        WARN(("CrVrScrCompositorIntersectList failed, rc %d", rc));
        CrVrScrCompositorClear(pDstCompositor);
        return rc;
    }

    return VINF_SUCCESS;
}

