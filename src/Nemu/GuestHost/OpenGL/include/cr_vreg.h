/* $Id: cr_vreg.h $ */
/** @file
 * Visible Regions processing API
 */

/*
 * Copyright (C) 2012-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___cr_vreg_h_
#define ___cr_vreg_h_

#include <iprt/list.h>
#include <iprt/types.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/asm.h>

#ifndef IN_RING0
# define NEMUVREGDECL(_type) DECLEXPORT(_type)
#else
/** @todo r=bird: Using RTDECL is just SOO wrong!   */
# define NEMUVREGDECL(_type) RTDECL(_type)
#endif



RT_C_DECLS_BEGIN

typedef struct NEMUVR_LIST
{
    RTLISTANCHOR ListHead;
    uint32_t cEntries;
} NEMUVR_LIST;
typedef NEMUVR_LIST *PNEMUVR_LIST;
typedef NEMUVR_LIST const *PCNEMUVR_LIST;

DECLINLINE(int) NemuRectCmp(PCRTRECT pRect1, PCRTRECT pRect2)
{
    return memcmp(pRect1, pRect2, sizeof(*pRect1));
}

#ifndef IN_RING0
# define CR_FLOAT_RCAST(_t, _v) ((_t)((float)(_v) + 0.5))

DECLINLINE(void) NemuRectScale(PRTRECT pRect, float xScale, float yScale)
{
    pRect->xLeft   = CR_FLOAT_RCAST(int32_t, pRect->xLeft   * xScale);
    pRect->yTop    = CR_FLOAT_RCAST(int32_t, pRect->yTop    * yScale);
    pRect->xRight  = CR_FLOAT_RCAST(int32_t, pRect->xRight  * xScale);
    pRect->yBottom = CR_FLOAT_RCAST(int32_t, pRect->yBottom * yScale);
}

DECLINLINE(void) NemuRectScaled(PCRTRECT pRect, float xScale, float yScale, PRTRECT pResult)
{
    *pResult = *pRect;
    NemuRectScale(pResult, xScale, yScale);
}

DECLINLINE(void) NemuRectUnscale(PRTRECT pRect, float xScale, float yScale)
{
    pRect->xLeft   = CR_FLOAT_RCAST(int32_t, pRect->xLeft   / xScale);
    pRect->yTop    = CR_FLOAT_RCAST(int32_t, pRect->yTop    / yScale);
    pRect->xRight  = CR_FLOAT_RCAST(int32_t, pRect->xRight  / xScale);
    pRect->yBottom = CR_FLOAT_RCAST(int32_t, pRect->yBottom / yScale);
}

DECLINLINE(void) NemuRectUnscaled(PCRTRECT pRect, float xScale, float yScale, PRTRECT pResult)
{
    *pResult = *pRect;
    NemuRectUnscale(pResult, xScale, yScale);
}

#endif /* IN_RING0 */

DECLINLINE(void) NemuRectIntersect(PRTRECT pRect1, PCRTRECT pRect2)
{
    Assert(pRect1);
    Assert(pRect2);
    pRect1->xLeft   = RT_MAX(pRect1->xLeft,   pRect2->xLeft);
    pRect1->yTop    = RT_MAX(pRect1->yTop,    pRect2->yTop);
    pRect1->xRight  = RT_MIN(pRect1->xRight,  pRect2->xRight);
    pRect1->yBottom = RT_MIN(pRect1->yBottom, pRect2->yBottom);
    /* ensure the rect is valid */
    pRect1->xRight  = RT_MAX(pRect1->xRight,  pRect1->xLeft);
    pRect1->yBottom = RT_MAX(pRect1->yBottom, pRect1->yTop);
}

DECLINLINE(void) NemuRectIntersected(PCRTRECT pRect1, PCRTRECT pRect2, PRTRECT pResult)
{
    *pResult = *pRect1;
    NemuRectIntersect(pResult, pRect2);
}


DECLINLINE(void) NemuRectTranslate(PRTRECT pRect, int32_t x, int32_t y)
{
    pRect->xLeft   += x;
    pRect->yTop    += y;
    pRect->xRight  += x;
    pRect->yBottom += y;
}

DECLINLINE(void) NemuRectTranslated(PCRTRECT pRect, int32_t x, int32_t y, PRTRECT pResult)
{
    *pResult = *pRect;
    NemuRectTranslate(pResult, x, y);
}

DECLINLINE(void) NemuRectInvertY(PRTRECT pRect)
{
    int32_t y = pRect->yTop;
    pRect->yTop = pRect->yBottom;
    pRect->yBottom = y;
}

DECLINLINE(void) NemuRectInvertedY(PCRTRECT pRect, PRTRECT pResult)
{
    *pResult = *pRect;
    NemuRectInvertY(pResult);
}

DECLINLINE(void) NemuRectMove(PRTRECT pRect, int32_t x, int32_t y)
{
    int32_t cx = pRect->xRight  - pRect->xLeft;
    int32_t cy = pRect->yBottom - pRect->yTop;
    pRect->xLeft   = x;
    pRect->yTop    = y;
    pRect->xRight  = cx + x;
    pRect->yBottom = cy + y;
}

DECLINLINE(void) NemuRectMoved(PCRTRECT pRect, int32_t x, int32_t y, PRTRECT pResult)
{
    *pResult = *pRect;
    NemuRectMove(pResult, x, y);
}

DECLINLINE(bool) NemuRectCovers(PCRTRECT pRect, PCRTRECT pCovered)
{
    AssertPtr(pRect);
    AssertPtr(pCovered);
    if (pRect->xLeft > pCovered->xLeft)
        return false;
    if (pRect->yTop > pCovered->yTop)
        return false;
    if (pRect->xRight < pCovered->xRight)
        return false;
    if (pRect->yBottom < pCovered->yBottom)
        return false;
    return true;
}

DECLINLINE(bool) NemuRectIsZero(PCRTRECT pRect)
{
    return pRect->xLeft == pRect->xRight || pRect->yTop == pRect->yBottom;
}

DECLINLINE(bool) NemuRectIsIntersect(PCRTRECT pRect1, PCRTRECT pRect2)
{
    return !(   (pRect1->xLeft < pRect2->xLeft && pRect1->xRight  <= pRect2->xLeft)
             || (pRect2->xLeft < pRect1->xLeft && pRect2->xRight  <= pRect1->xLeft)
             || (pRect1->yTop  < pRect2->yTop  && pRect1->yBottom <= pRect2->yTop)
             || (pRect2->yTop  < pRect1->yTop  && pRect2->yBottom <= pRect1->yTop) );
}

DECLINLINE(uint32_t) NemuVrListRectsCount(PCNEMUVR_LIST pList)
{
    return pList->cEntries;
}

DECLINLINE(bool) NemuVrListIsEmpty(PCNEMUVR_LIST pList)
{
    return !NemuVrListRectsCount(pList);
}

DECLINLINE(void) NemuVrListInit(PNEMUVR_LIST pList)
{
    RTListInit(&pList->ListHead);
    pList->cEntries = 0;
}

NEMUVREGDECL(void) NemuVrListClear(PNEMUVR_LIST pList);

/* moves list data to pDstList and empties the pList */
NEMUVREGDECL(void) NemuVrListMoveTo(PNEMUVR_LIST pList, PNEMUVR_LIST pDstList);

NEMUVREGDECL(void) NemuVrListTranslate(PNEMUVR_LIST pList, int32_t x, int32_t y);

NEMUVREGDECL(int) NemuVrListCmp(PCNEMUVR_LIST pList1, PCNEMUVR_LIST pList2);

NEMUVREGDECL(int) NemuVrListRectsSet(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT paRects, bool *pfChanged);
NEMUVREGDECL(int) NemuVrListRectsAdd(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT paRects, bool *pfChanged);
NEMUVREGDECL(int) NemuVrListRectsSubst(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT paRects, bool *pfChanged);
NEMUVREGDECL(int) NemuVrListRectsGet(PNEMUVR_LIST pList, uint32_t cRects, PRTRECT paRects);

NEMUVREGDECL(int) NemuVrListClone(PCNEMUVR_LIST pList, NEMUVR_LIST *pDstList);

/* NOTE: with the current implementation the NemuVrListIntersect is faster than NemuVrListRectsIntersect,
 * i.e. NemuVrListRectsIntersect is actually a convenience function that create a temporary list and calls
 * NemuVrListIntersect internally. */
NEMUVREGDECL(int) NemuVrListRectsIntersect(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT paRects, bool *pfChanged);
NEMUVREGDECL(int) NemuVrListIntersect(PNEMUVR_LIST pList, PCNEMUVR_LIST pList2, bool *pfChanged);

NEMUVREGDECL(int) NemuVrInit(void);
NEMUVREGDECL(void) NemuVrTerm(void);

typedef struct NEMUVR_LIST_ITERATOR
{
    PNEMUVR_LIST pList;
    PRTLISTNODE pNextEntry;
} NEMUVR_LIST_ITERATOR;
typedef NEMUVR_LIST_ITERATOR *PNEMUVR_LIST_ITERATOR;
typedef NEMUVR_LIST_ITERATOR const *PCNEMUVR_LIST_ITERATOR;

DECLINLINE(void) NemuVrListIterInit(PNEMUVR_LIST pList, PNEMUVR_LIST_ITERATOR pIter)
{
    pIter->pList = pList;
    pIter->pNextEntry = pList->ListHead.pNext;
}

typedef struct NEMUVR_REG
{
    RTLISTNODE ListEntry;
    RTRECT Rect;
} NEMUVR_REG;
typedef NEMUVR_REG *PNEMUVR_REG;
typedef NEMUVR_REG const *PCNEMUVR_REG;

#define PNEMUVR_REG_FROM_ENTRY(_pEntry)     RT_FROM_MEMBER(_pEntry, NEMUVR_REG, ListEntry)

DECLINLINE(PCRTRECT) NemuVrListIterNext(PNEMUVR_LIST_ITERATOR pIter)
{
    PRTLISTNODE pNextEntry = pIter->pNextEntry;
    if (pNextEntry != &pIter->pList->ListHead)
    {
        PCRTRECT pRect = &PNEMUVR_REG_FROM_ENTRY(pNextEntry)->Rect;
        pIter->pNextEntry = pNextEntry->pNext;
        return pRect;
    }
    return NULL;
}

typedef struct NEMUVR_COMPOSITOR_ENTRY
{
    RTLISTNODE Node;
    NEMUVR_LIST Vr;
    uint32_t cRefs;
} NEMUVR_COMPOSITOR_ENTRY;
typedef NEMUVR_COMPOSITOR_ENTRY *PNEMUVR_COMPOSITOR_ENTRY;
typedef NEMUVR_COMPOSITOR_ENTRY const *PCNEMUVR_COMPOSITOR_ENTRY;

struct NEMUVR_COMPOSITOR;

typedef DECLCALLBACK(void) FNNEMUVRCOMPOSITOR_ENTRY_RELEASED(const struct NEMUVR_COMPOSITOR *pCompositor,
                                                             PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                             PNEMUVR_COMPOSITOR_ENTRY pReplacingEntry);
typedef FNNEMUVRCOMPOSITOR_ENTRY_RELEASED *PFNNEMUVRCOMPOSITOR_ENTRY_RELEASED;

typedef struct NEMUVR_COMPOSITOR
{
    RTLISTANCHOR List;
    PFNNEMUVRCOMPOSITOR_ENTRY_RELEASED pfnEntryReleased;
} NEMUVR_COMPOSITOR;
typedef NEMUVR_COMPOSITOR *PNEMUVR_COMPOSITOR;
typedef NEMUVR_COMPOSITOR const *PCNEMUVR_COMPOSITOR;

typedef DECLCALLBACK(bool) FNNEMUVRCOMPOSITOR_VISITOR(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                      void *pvVisitor);
typedef FNNEMUVRCOMPOSITOR_VISITOR *PFNNEMUVRCOMPOSITOR_VISITOR;

NEMUVREGDECL(void) NemuVrCompositorInit(PNEMUVR_COMPOSITOR pCompositor, PFNNEMUVRCOMPOSITOR_ENTRY_RELEASED pfnEntryReleased);
NEMUVREGDECL(void) NemuVrCompositorClear(PNEMUVR_COMPOSITOR pCompositor);
NEMUVREGDECL(void) NemuVrCompositorRegionsClear(PNEMUVR_COMPOSITOR pCompositor, bool *pfChanged);
NEMUVREGDECL(void) NemuVrCompositorEntryInit(PNEMUVR_COMPOSITOR_ENTRY pEntry);

DECLINLINE(bool) NemuVrCompositorEntryIsInList(PCNEMUVR_COMPOSITOR_ENTRY pEntry)
{
    return !NemuVrListIsEmpty(&pEntry->Vr);
}

#define CRBLT_F_LINEAR                  0x00000001
#define CRBLT_F_INVERT_SRC_YCOORDS      0x00000002
#define CRBLT_F_INVERT_DST_YCOORDS      0x00000004
#define CRBLT_F_INVERT_YCOORDS          (CRBLT_F_INVERT_SRC_YCOORDS | CRBLT_F_INVERT_DST_YCOORDS)
/* the blit operation with discard the source alpha channel values and set the destination alpha values to 1.0 */
#define CRBLT_F_NOALPHA                 0x00000010

#define CRBLT_FTYPE_XOR                 CRBLT_F_INVERT_YCOORDS
#define CRBLT_FTYPE_OR                  (CRBLT_F_LINEAR | CRBLT_F_NOALPHA)
#define CRBLT_FOP_COMBINE(_f1, _f2)     ((((_f1) ^ (_f2)) & CRBLT_FTYPE_XOR) | (((_f1) | (_f2)) & CRBLT_FTYPE_OR))

#define CRBLT_FLAGS_FROM_FILTER(_f)     ( ((_f) & GL_LINEAR) ? CRBLT_F_LINEAR : 0)
#define CRBLT_FILTER_FROM_FLAGS(_f)     (((_f) & CRBLT_F_LINEAR) ? GL_LINEAR : GL_NEAREST)

/* compositor regions changed */
#define NEMUVR_COMPOSITOR_CF_REGIONS_CHANGED                           0x00000001
/* other entries changed along while doing current entry modification
 * always comes with NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED */
#define NEMUVR_COMPOSITOR_CF_OTHER_ENTRIES_REGIONS_CHANGED             0x00000002
/* only current entry regions changed
 * can come wither with NEMUVR_COMPOSITOR_CF_REGIONS_CHANGED or with NEMUVR_COMPOSITOR_CF_ENTRY_REPLACED */
#define NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED                     0x00000004
/* the given entry has replaced some other entry, while overal regions did NOT change.
 * always comes with NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED */
#define NEMUVR_COMPOSITOR_CF_ENTRY_REPLACED                            0x00000008


NEMUVREGDECL(bool) NemuVrCompositorEntryRemove(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry);
NEMUVREGDECL(bool) NemuVrCompositorEntryReplace(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                PNEMUVR_COMPOSITOR_ENTRY pNewEntry);
NEMUVREGDECL(int) NemuVrCompositorEntryRegionsAdd(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                  uint32_t cRegions, PCRTRECT paRegions,
                                                  PNEMUVR_COMPOSITOR_ENTRY *ppReplacedEntry, uint32_t *pfChangeFlags);
NEMUVREGDECL(int) NemuVrCompositorEntryRegionsSubst(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                    uint32_t cRegions, PCRTRECT paRegions, bool *pfChanged);
NEMUVREGDECL(int) NemuVrCompositorEntryRegionsSet(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                  uint32_t cRegions, PCRTRECT paRegions, bool *pfChanged);
NEMUVREGDECL(int) NemuVrCompositorEntryRegionsIntersect(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                        uint32_t cRegions, PCRTRECT paRegions, bool *pfChanged);
NEMUVREGDECL(int) NemuVrCompositorEntryListIntersect(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                     PCNEMUVR_LIST pList2, bool *pfChanged);
NEMUVREGDECL(int) NemuVrCompositorEntryRegionsIntersectAll(PNEMUVR_COMPOSITOR pCompositor, uint32_t cRegions, PCRTRECT paRegions,
                                                           bool *pfChanged);
NEMUVREGDECL(int) NemuVrCompositorEntryListIntersectAll(PNEMUVR_COMPOSITOR pCompositor, PCNEMUVR_LIST pList2, bool *pfChanged);
NEMUVREGDECL(int) NemuVrCompositorEntryRegionsTranslate(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                        int32_t x, int32_t y, bool *pfChanged);
NEMUVREGDECL(void) NemuVrCompositorVisit(PNEMUVR_COMPOSITOR pCompositor, PFNNEMUVRCOMPOSITOR_VISITOR pfnVisitor, void *pvVisitor);

DECLINLINE(bool) NemuVrCompositorIsEmpty(PCNEMUVR_COMPOSITOR pCompositor)
{
    return RTListIsEmpty(&pCompositor->List);
}

typedef struct NEMUVR_COMPOSITOR_ITERATOR
{
    PNEMUVR_COMPOSITOR pCompositor;
    PRTLISTNODE pNextEntry;
} NEMUVR_COMPOSITOR_ITERATOR;
typedef NEMUVR_COMPOSITOR_ITERATOR *PNEMUVR_COMPOSITOR_ITERATOR;

typedef struct NEMUVR_COMPOSITOR_CONST_ITERATOR
{
    PCNEMUVR_COMPOSITOR pCompositor;
    PCRTLISTNODE pNextEntry;
} NEMUVR_COMPOSITOR_CONST_ITERATOR;
typedef NEMUVR_COMPOSITOR_CONST_ITERATOR *PNEMUVR_COMPOSITOR_CONST_ITERATOR;

DECLINLINE(void) NemuVrCompositorIterInit(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ITERATOR pIter)
{
    pIter->pCompositor = pCompositor;
    pIter->pNextEntry = pCompositor->List.pNext;
}

DECLINLINE(void) NemuVrCompositorConstIterInit(PCNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_CONST_ITERATOR pIter)
{
    pIter->pCompositor = pCompositor;
    pIter->pNextEntry = pCompositor->List.pNext;
}

#define NEMUVR_COMPOSITOR_ENTRY_FROM_NODE(_p)       RT_FROM_MEMBER(_p, NEMUVR_COMPOSITOR_ENTRY, Node)
#define NEMUVR_COMPOSITOR_CONST_ENTRY_FROM_NODE(_p) RT_FROM_MEMBER(_p, const NEMUVR_COMPOSITOR_ENTRY, Node)

DECLINLINE(PNEMUVR_COMPOSITOR_ENTRY) NemuVrCompositorIterNext(PNEMUVR_COMPOSITOR_ITERATOR pIter)
{
    PRTLISTNODE pNextEntry = pIter->pNextEntry;
    if (pNextEntry != &pIter->pCompositor->List)
    {
        PNEMUVR_COMPOSITOR_ENTRY pEntry = NEMUVR_COMPOSITOR_ENTRY_FROM_NODE(pNextEntry);
        pIter->pNextEntry = pNextEntry->pNext;
        return pEntry;
    }
    return NULL;
}

DECLINLINE(PCNEMUVR_COMPOSITOR_ENTRY) NemuVrCompositorConstIterNext(PNEMUVR_COMPOSITOR_CONST_ITERATOR pIter)
{
    PCRTLISTNODE pNextEntry = pIter->pNextEntry;
    if (pNextEntry != &pIter->pCompositor->List)
    {
        PCNEMUVR_COMPOSITOR_ENTRY pEntry = NEMUVR_COMPOSITOR_CONST_ENTRY_FROM_NODE(pNextEntry);
        pIter->pNextEntry = pNextEntry->pNext;
        return pEntry;
    }
    return NULL;
}

typedef struct NEMUVR_TEXTURE
{
    int32_t  width;
    int32_t  height;
    uint32_t target;
    uint32_t hwid;
} NEMUVR_TEXTURE;
typedef NEMUVR_TEXTURE *PNEMUVR_TEXTURE;
typedef NEMUVR_TEXTURE const *PCNEMUVR_TEXTURE;

RT_C_DECLS_END

#endif

