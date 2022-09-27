/* $Id: cr_compositor.h $ */
/** @file
 * Compositor API.
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

#ifndef ___cr_compositor_h
#define ___cr_compositor_h

#include "cr_vreg.h"
#include "cr_blitter.h"


/* Compositor with Stretching & Cached Rectangles info */

RT_C_DECLS_BEGIN

struct NEMUVR_SCR_COMPOSITOR_ENTRY;
struct NEMUVR_SCR_COMPOSITOR;

typedef DECLCALLBACK(void) FNNEMUVRSCRCOMPOSITOR_ENTRY_RELEASED(const struct NEMUVR_SCR_COMPOSITOR *pCompositor,
                                                                struct NEMUVR_SCR_COMPOSITOR_ENTRY *pEntry,
                                                                struct NEMUVR_SCR_COMPOSITOR_ENTRY *pReplacingEntry);
typedef FNNEMUVRSCRCOMPOSITOR_ENTRY_RELEASED *PFNNEMUVRSCRCOMPOSITOR_ENTRY_RELEASED;


typedef struct NEMUVR_SCR_COMPOSITOR_ENTRY
{
    NEMUVR_COMPOSITOR_ENTRY Ce;
    RTRECT Rect;
    uint32_t fChanged;
    uint32_t fFlags;
    uint32_t cRects;
    PRTRECT paSrcRects;
    PRTRECT paDstRects;
    PRTRECT paDstUnstretchedRects;
    PFNNEMUVRSCRCOMPOSITOR_ENTRY_RELEASED pfnEntryReleased;
    PCR_TEXDATA pTex;
} NEMUVR_SCR_COMPOSITOR_ENTRY;
typedef NEMUVR_SCR_COMPOSITOR_ENTRY *PNEMUVR_SCR_COMPOSITOR_ENTRY;
typedef NEMUVR_SCR_COMPOSITOR_ENTRY const *PCNEMUVR_SCR_COMPOSITOR_ENTRY;

typedef struct NEMUVR_SCR_COMPOSITOR
{
    NEMUVR_COMPOSITOR Compositor;
    RTRECT Rect;
#ifndef IN_RING0
    float StretchX;
    float StretchY;
#endif
    uint32_t fFlags;
    uint32_t cRects;
    uint32_t cRectsBuffer;
    PRTRECT paSrcRects;
    PRTRECT paDstRects;
    PRTRECT paDstUnstretchedRects;
} NEMUVR_SCR_COMPOSITOR;
typedef NEMUVR_SCR_COMPOSITOR *PNEMUVR_SCR_COMPOSITOR;
typedef NEMUVR_SCR_COMPOSITOR const *PCNEMUVR_SCR_COMPOSITOR;


typedef DECLCALLBACK(bool) FNNEMUVRSCRCOMPOSITOR_VISITOR(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                         void *pvVisitor);
typedef FNNEMUVRSCRCOMPOSITOR_VISITOR *PFNNEMUVRSCRCOMPOSITOR_VISITOR;

DECLINLINE(void) CrVrScrCompositorEntryInit(PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry, PCRTRECT pRect, CR_TEXDATA *pTex,
                                            PFNNEMUVRSCRCOMPOSITOR_ENTRY_RELEASED pfnEntryReleased)
{
    memset(pEntry, 0, sizeof (*pEntry));
    NemuVrCompositorEntryInit(&pEntry->Ce);
    pEntry->Rect = *pRect;
    pEntry->pfnEntryReleased = pfnEntryReleased;
    if (pTex)
    {
        CrTdAddRef(pTex);
        pEntry->pTex = pTex;
    }
}

DECLINLINE(void) CrVrScrCompositorEntryCleanup(PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    if (pEntry->pTex)
    {
        CrTdRelease(pEntry->pTex);
        pEntry->pTex = NULL;
    }
}

DECLINLINE(bool) CrVrScrCompositorEntryIsUsed(PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    return NemuVrCompositorEntryIsInList(&pEntry->Ce);
}

DECLINLINE(void) CrVrScrCompositorEntrySetChanged(PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry, bool fChanged)
{
    pEntry->fChanged = !!fChanged;
}

DECLINLINE(void) CrVrScrCompositorEntryTexSet(PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry, CR_TEXDATA *pTex)
{
    if (pEntry->pTex)
        CrTdRelease(pEntry->pTex);

    if (pTex)
        CrTdAddRef(pTex);

    pEntry->pTex = pTex;
}

DECLINLINE(CR_TEXDATA *) CrVrScrCompositorEntryTexGet(PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    return pEntry->pTex;
}

DECLINLINE(bool) CrVrScrCompositorEntryIsChanged(PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    return !!pEntry->fChanged;
}

DECLINLINE(bool) CrVrScrCompositorIsEmpty(PCNEMUVR_SCR_COMPOSITOR pCompositor)
{
    return NemuVrCompositorIsEmpty(&pCompositor->Compositor);
}

NEMUVREGDECL(int) CrVrScrCompositorEntryRectSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                PCRTRECT pRect);
NEMUVREGDECL(int) CrVrScrCompositorEntryTexAssign(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                  CR_TEXDATA *pTex);
NEMUVREGDECL(void) CrVrScrCompositorVisit(PNEMUVR_SCR_COMPOSITOR pCompositor, PFNNEMUVRSCRCOMPOSITOR_VISITOR pfnVisitor,
                                          void *pvVisitor);
NEMUVREGDECL(void) CrVrScrCompositorEntrySetAllChanged(PNEMUVR_SCR_COMPOSITOR pCompositor, bool fChanged);
DECLINLINE(bool) CrVrScrCompositorEntryIsInList(PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    return NemuVrCompositorEntryIsInList(&pEntry->Ce);
}
NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsAdd(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                   PCRTPOINT pPos, uint32_t cRegions, PCRTRECT paRegions, bool fPosRelated,
                                                   NEMUVR_SCR_COMPOSITOR_ENTRY **ppReplacedScrEntry, uint32_t *pfChangeFlags);
NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                   PCRTPOINT pPos, uint32_t cRegions, PCRTRECT paRegions, bool fPosRelated,
                                                   bool *pfChanged);
NEMUVREGDECL(int) CrVrScrCompositorEntryListIntersect(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                      PCNEMUVR_LIST pList2, bool *pfChanged);
NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsIntersect(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                         uint32_t cRegions, PCRTRECT paRegions, bool *pfChanged);
NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsIntersectAll(PNEMUVR_SCR_COMPOSITOR pCompositor, uint32_t cRegions,
                                                            PCRTRECT paRegions, bool *pfChanged);
NEMUVREGDECL(int) CrVrScrCompositorEntryListIntersectAll(PNEMUVR_SCR_COMPOSITOR pCompositor, PCNEMUVR_LIST pList2,
                                                         bool *pfChanged);
NEMUVREGDECL(int) CrVrScrCompositorEntryPosSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                               PCRTPOINT pPos);
DECLINLINE(PCRTRECT) CrVrScrCompositorEntryRectGet(PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    return &pEntry->Rect;
}

/* regions are valid until the next CrVrScrCompositor call */
NEMUVREGDECL(int) CrVrScrCompositorEntryRegionsGet(PCNEMUVR_SCR_COMPOSITOR pCompositor,
                                                   PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry, uint32_t *pcRegions,
                                                   PCRTRECT *ppaSrcRegions, PCRTRECT *ppaDstRegions,
                                                   PCRTRECT *ppaDstUnstretchedRects);
NEMUVREGDECL(int) CrVrScrCompositorEntryRemove(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry);
NEMUVREGDECL(bool) CrVrScrCompositorEntryReplace(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                 PNEMUVR_SCR_COMPOSITOR_ENTRY pNewEntry);
NEMUVREGDECL(void) CrVrScrCompositorEntryFlagsSet(PNEMUVR_SCR_COMPOSITOR_ENTRY pEntry, uint32_t fFlags);
NEMUVREGDECL(uint32_t) CrVrScrCompositorEntryFlagsCombinedGet(PCNEMUVR_SCR_COMPOSITOR pCompositor,
                                                              PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry);
DECLINLINE(uint32_t) CrVrScrCompositorEntryFlagsGet(PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry)
{
    return pEntry->fFlags;
}

NEMUVREGDECL(void) CrVrScrCompositorInit(PNEMUVR_SCR_COMPOSITOR pCompositor, PCRTRECT pRect);
NEMUVREGDECL(int) CrVrScrCompositorRectSet(PNEMUVR_SCR_COMPOSITOR pCompositor, PCRTRECT pRect, bool *pfChanged);
DECLINLINE(PCRTRECT) CrVrScrCompositorRectGet(PCNEMUVR_SCR_COMPOSITOR pCompositor)
{
    return &pCompositor->Rect;
}

NEMUVREGDECL(void) CrVrScrCompositorClear(PNEMUVR_SCR_COMPOSITOR pCompositor);
NEMUVREGDECL(void) CrVrScrCompositorRegionsClear(PNEMUVR_SCR_COMPOSITOR pCompositor, bool *pfChanged);

typedef DECLCALLBACK(NEMUVR_SCR_COMPOSITOR_ENTRY*) FNNEMUVR_SCR_COMPOSITOR_ENTRY_FOR(PCNEMUVR_SCR_COMPOSITOR_ENTRY pEntry,
                                                                                     void *pvContext);
typedef FNNEMUVR_SCR_COMPOSITOR_ENTRY_FOR *PFNNEMUVR_SCR_COMPOSITOR_ENTRY_FOR;

NEMUVREGDECL(int) CrVrScrCompositorClone(PCNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR pDstCompositor,
                                         PFNNEMUVR_SCR_COMPOSITOR_ENTRY_FOR pfnEntryFor, void *pvEntryFor);
NEMUVREGDECL(int) CrVrScrCompositorIntersectList(PNEMUVR_SCR_COMPOSITOR pCompositor, PCNEMUVR_LIST pVr, bool *pfChanged);
NEMUVREGDECL(int) CrVrScrCompositorIntersectedList(PCNEMUVR_SCR_COMPOSITOR pCompositor, PCNEMUVR_LIST pVr,
                                                   PNEMUVR_SCR_COMPOSITOR pDstCompositor,
                                                   PFNNEMUVR_SCR_COMPOSITOR_ENTRY_FOR pfnEntryFor, void *pvEntryFor, bool *pfChanged);
#ifndef IN_RING0
NEMUVREGDECL(void) CrVrScrCompositorSetStretching(PNEMUVR_SCR_COMPOSITOR pCompositor, float StretchX, float StretchY);
DECLINLINE(void) CrVrScrCompositorGetStretching(PCNEMUVR_SCR_COMPOSITOR pCompositor, float *pStretchX, float *pStretchY)
{
    if (pStretchX)
        *pStretchX = pCompositor->StretchX;

    if (pStretchY)
        *pStretchY = pCompositor->StretchY;
}
#endif
/* regions are valid until the next CrVrScrCompositor call */
NEMUVREGDECL(int) CrVrScrCompositorRegionsGet(PCNEMUVR_SCR_COMPOSITOR pCompositor, uint32_t *pcRegions,
                                              PCRTRECT *ppaSrcRegions, PCRTRECT *ppaDstRegions, PCRTRECT *ppaDstUnstretchedRects);

#define NEMUVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(_p)          RT_FROM_MEMBER(_p, NEMUVR_SCR_COMPOSITOR_ENTRY, Ce)
#define NEMUVR_SCR_COMPOSITOR_CONST_ENTRY_FROM_ENTRY(_p)    RT_FROM_MEMBER(_p, const NEMUVR_SCR_COMPOSITOR_ENTRY, Ce)
#define NEMUVR_SCR_COMPOSITOR_FROM_COMPOSITOR(_p)           RT_FROM_MEMBER(_p, NEMUVR_SCR_COMPOSITOR, Compositor)

typedef struct NEMUVR_SCR_COMPOSITOR_ITERATOR
{
    NEMUVR_COMPOSITOR_ITERATOR Base;
} NEMUVR_SCR_COMPOSITOR_ITERATOR;
typedef NEMUVR_SCR_COMPOSITOR_ITERATOR *PNEMUVR_SCR_COMPOSITOR_ITERATOR;

typedef struct NEMUVR_SCR_COMPOSITOR_CONST_ITERATOR
{
    NEMUVR_COMPOSITOR_CONST_ITERATOR Base;
} NEMUVR_SCR_COMPOSITOR_CONST_ITERATOR;
typedef NEMUVR_SCR_COMPOSITOR_CONST_ITERATOR *PNEMUVR_SCR_COMPOSITOR_CONST_ITERATOR;

DECLINLINE(void) CrVrScrCompositorIterInit(PNEMUVR_SCR_COMPOSITOR pCompositor, PNEMUVR_SCR_COMPOSITOR_ITERATOR pIter)
{
    NemuVrCompositorIterInit(&pCompositor->Compositor, &pIter->Base);
}

DECLINLINE(void) CrVrScrCompositorConstIterInit(PCNEMUVR_SCR_COMPOSITOR pCompositor,
                                                PNEMUVR_SCR_COMPOSITOR_CONST_ITERATOR pIter)
{
    NemuVrCompositorConstIterInit(&pCompositor->Compositor, &pIter->Base);
}

DECLINLINE(PNEMUVR_SCR_COMPOSITOR_ENTRY) CrVrScrCompositorIterNext(PNEMUVR_SCR_COMPOSITOR_ITERATOR pIter)
{
    PNEMUVR_COMPOSITOR_ENTRY pCe = NemuVrCompositorIterNext(&pIter->Base);
    if (pCe)
        return NEMUVR_SCR_COMPOSITOR_ENTRY_FROM_ENTRY(pCe);
    return NULL;
}

DECLINLINE(PCNEMUVR_SCR_COMPOSITOR_ENTRY) CrVrScrCompositorConstIterNext(PNEMUVR_SCR_COMPOSITOR_CONST_ITERATOR pIter)
{
    PCNEMUVR_COMPOSITOR_ENTRY pCe = NemuVrCompositorConstIterNext(&pIter->Base);
    if (pCe)
        return NEMUVR_SCR_COMPOSITOR_CONST_ENTRY_FROM_ENTRY(pCe);
    return NULL;
}

RT_C_DECLS_END

#endif

