/* $Id: vreg.cpp $ */
/** @file
 * Visible Regions processing API implementation
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef IN_VMSVGA3D
# include "../include/cr_vreg.h"
# define WARN AssertMsgFailed
#else
# include <cr_vreg.h>
# include <cr_error.h>
#endif

#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/asm.h>

#ifdef DEBUG_misha
# define NEMUVDBG_VR_LAL_DISABLE
#endif

#ifndef IN_RING0
# include <iprt/memcache.h>
#  ifndef NEMUVDBG_VR_LAL_DISABLE
static RTMEMCACHE g_NemuVrLookasideList;
#   define nemuVrRegLaAlloc(_c) RTMemCacheAlloc((_c))
#   define nemuVrRegLaFree(_c, _e) RTMemCacheFree((_c), (_e))

DECLINLINE(int) nemuVrLaCreate(PRTMEMCACHE phCache, size_t cbElement)
{
    int rc = RTMemCacheCreate(phCache,
                              cbElement,
                              0 /* cbAlignment */,
                              UINT32_MAX /* cMaxObjects */,
                              NULL /* pfnCtor*/,
                              NULL /* pfnDtor*/,
                              NULL /* pvUser*/,
                              0 /* fFlags*/);
    if (!RT_SUCCESS(rc))
    {
        WARN(("RTMemCacheCreate failed rc %d", rc));
        return rc;
    }
    return VINF_SUCCESS;
}
#  define nemuVrLaDestroy(_c) RTMemCacheDestroy((_c))
# endif /* !NEMUVDBG_VR_LAL_DISABLE */

#else /* IN_RING0 */
# ifdef RT_OS_WINDOWS
#  undef PAGE_SIZE
#  undef PAGE_SHIFT
#  define NEMU_WITH_WORKAROUND_MISSING_PACK
#  if (_MSC_VER >= 1400) && !defined(NEMU_WITH_PATCHED_DDK)
#    define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
#    define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
#    define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
#    define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
#    define _interlockedbittestandset      _interlockedbittestandset_StupidDDKVsCompilerCrap
#    define _interlockedbittestandreset    _interlockedbittestandreset_StupidDDKVsCompilerCrap
#    define _interlockedbittestandset64    _interlockedbittestandset64_StupidDDKVsCompilerCrap
#    define _interlockedbittestandreset64  _interlockedbittestandreset64_StupidDDKVsCompilerCrap
#    pragma warning(disable : 4163)
#    ifdef NEMU_WITH_WORKAROUND_MISSING_PACK
#     pragma warning(disable : 4103)
#    endif
#    include <ntddk.h>
#    pragma warning(default : 4163)
#    ifdef NEMU_WITH_WORKAROUND_MISSING_PACK
#     pragma pack()
#     pragma warning(default : 4103)
#    endif
#    undef  _InterlockedExchange
#    undef  _InterlockedExchangeAdd
#    undef  _InterlockedCompareExchange
#    undef  _InterlockedAddLargeStatistic
#    undef  _interlockedbittestandset
#    undef  _interlockedbittestandreset
#    undef  _interlockedbittestandset64
#    undef  _interlockedbittestandreset64
#  else
#    include <ntddk.h>
#  endif
#  ifndef NEMUVDBG_VR_LAL_DISABLE
static LOOKASIDE_LIST_EX g_NemuVrLookasideList;
#   define nemuVrRegLaAlloc(_c) ExAllocateFromLookasideListEx(&(_c))
#   define nemuVrRegLaFree(_c, _e) ExFreeToLookasideListEx(&(_c), (_e))
#   define NEMUWDDMVR_MEMTAG 'vDBV'
DECLINLINE(int) nemuVrLaCreate(LOOKASIDE_LIST_EX *pCache, size_t cbElement)
{
    NTSTATUS Status = ExInitializeLookasideListEx(pCache,
                                                  NULL, /* PALLOCATE_FUNCTION_EX Allocate */
                                                  NULL, /* PFREE_FUNCTION_EX Free */
                                                  NonPagedPool,
                                                  0, /* ULONG Flags */
                                                  cbElement,
                                                  NEMUWDDMVR_MEMTAG,
                                                  0 /* USHORT Depth - reserved, must be null */
                                                  );
    if (!NT_SUCCESS(Status))
    {
        WARN(("ExInitializeLookasideListEx failed, Status (0x%x)", Status));
        return VERR_GENERAL_FAILURE;
    }

    return VINF_SUCCESS;
}
#   define nemuVrLaDestroy(_c) ExDeleteLookasideListEx(&(_c))
#  endif
# else  /* !RT_OS_WINDOWS */
#  error "port me!"
# endif /* !RT_OS_WINDOWS */
#endif /* IN_RING0 */


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define NEMUVR_INVALID_COORD    (~0U)


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
static volatile int32_t g_cNemuVrInits = 0;


static PNEMUVR_REG nemuVrRegCreate(void)
{
#ifndef NEMUVDBG_VR_LAL_DISABLE
    PNEMUVR_REG pReg = (PNEMUVR_REG)nemuVrRegLaAlloc(g_NemuVrLookasideList);
    if (!pReg)
    {
        WARN(("ExAllocateFromLookasideListEx failed!"));
    }
    return pReg;
#else
    return (PNEMUVR_REG)RTMemAlloc(sizeof(NEMUVR_REG));
#endif
}

static void nemuVrRegTerm(PNEMUVR_REG pReg)
{
#ifndef NEMUVDBG_VR_LAL_DISABLE
    nemuVrRegLaFree(g_NemuVrLookasideList, pReg);
#else
    RTMemFree(pReg);
#endif
}

NEMUVREGDECL(void) NemuVrListClear(PNEMUVR_LIST pList)
{
    PNEMUVR_REG pReg, pRegNext;

    RTListForEachSafe(&pList->ListHead, pReg, pRegNext, NEMUVR_REG, ListEntry)
    {
        nemuVrRegTerm(pReg);
    }
    NemuVrListInit(pList);
}

/* moves list data to pDstList and empties the pList */
NEMUVREGDECL(void) NemuVrListMoveTo(PNEMUVR_LIST pList, PNEMUVR_LIST pDstList)
{
    *pDstList = *pList;
    pDstList->ListHead.pNext->pPrev = &pDstList->ListHead;
    pDstList->ListHead.pPrev->pNext = &pDstList->ListHead;
    NemuVrListInit(pList);
}

NEMUVREGDECL(int) NemuVrInit(void)
{
    int32_t cNewRefs = ASMAtomicIncS32(&g_cNemuVrInits);
    Assert(cNewRefs >= 1);
    Assert(cNewRefs == 1); /* <- debugging */
    if (cNewRefs > 1)
        return VINF_SUCCESS;

#ifndef NEMUVDBG_VR_LAL_DISABLE
    int rc = nemuVrLaCreate(&g_NemuVrLookasideList, sizeof(NEMUVR_REG));
    if (!RT_SUCCESS(rc))
    {
        WARN(("ExInitializeLookasideListEx failed, rc (%d)", rc));
        return rc;
    }
#endif

    return VINF_SUCCESS;
}

NEMUVREGDECL(void) NemuVrTerm(void)
{
    int32_t cNewRefs = ASMAtomicDecS32(&g_cNemuVrInits);
    Assert(cNewRefs >= 0);
    if (cNewRefs > 0)
        return;

#ifndef NEMUVDBG_VR_LAL_DISABLE
    nemuVrLaDestroy(g_NemuVrLookasideList);
#endif
}

typedef DECLCALLBACK(int) FNNEMUVR_CB_COMPARATOR(PCNEMUVR_REG pReg1, PCNEMUVR_REG pReg2);
typedef FNNEMUVR_CB_COMPARATOR *PFNNEMUVR_CB_COMPARATOR;

static DECLCALLBACK(int) nemuVrRegNonintersectedComparator(PCRTRECT pRect1, PCRTRECT pRect2)
{
    Assert(!NemuRectIsIntersect(pRect1, pRect2));
    if (pRect1->yTop != pRect2->yTop)
        return pRect1->yTop - pRect2->yTop;
    return pRect1->xLeft - pRect2->xLeft;
}

#ifdef DEBUG_misha
static void nemuVrDbgListDoVerify(PNEMUVR_LIST pList)
{
    PNEMUVR_REG pReg1, pReg2;
    RTListForEach(&pList->ListHead, pReg1, NEMUVR_REG, ListEntry)
    {
        Assert(!NemuRectIsZero(&pReg1->Rect));
        for (RTLISTNODE *pEntry2 = pReg1->ListEntry.pNext; pEntry2 != &pList->ListHead; pEntry2 = pEntry2->pNext)
        {
            pReg2 = PNEMUVR_REG_FROM_ENTRY(pEntry2);
            Assert(nemuVrRegNonintersectedComparator(&pReg1->Rect, &pReg2->Rect) < 0);
        }
    }
}
# define nemuVrDbgListVerify(_p) nemuVrDbgListDoVerify(_p)
#else
# define nemuVrDbgListVerify(_p) do {} while (0)
#endif


DECLINLINE(void) nemuVrListRegAdd(PNEMUVR_LIST pList, PNEMUVR_REG pReg, PRTLISTNODE pPlace, bool fAfter)
{
    if (fAfter)
        RTListPrepend(pPlace, &pReg->ListEntry);
    else
        RTListAppend(pPlace, &pReg->ListEntry);
    ++pList->cEntries;
    nemuVrDbgListVerify(pList);
}

DECLINLINE(void) nemuVrListRegRemove(PNEMUVR_LIST pList, PNEMUVR_REG pReg)
{
    RTListNodeRemove(&pReg->ListEntry);
    --pList->cEntries;
    nemuVrDbgListVerify(pList);
}

static void nemuVrListRegAddOrder(PNEMUVR_LIST pList, PRTLISTNODE pMemberEntry, PNEMUVR_REG pReg)
{
    for (;;)
    {
        if (pMemberEntry != &pList->ListHead)
        {
            PNEMUVR_REG pMemberReg = PNEMUVR_REG_FROM_ENTRY(pMemberEntry);
            if (nemuVrRegNonintersectedComparator(&pMemberReg->Rect, &pReg->Rect) < 0)
            {
                pMemberEntry = pMemberEntry->pNext;
                continue;
            }
        }
        nemuVrListRegAdd(pList, pReg, pMemberEntry, false);
        break;
    }
}

static void nemuVrListAddNonintersected(PNEMUVR_LIST pList1, PNEMUVR_LIST pList2)
{
    PRTLISTNODE pEntry1 = pList1->ListHead.pNext;

    for (PRTLISTNODE pEntry2 = pList2->ListHead.pNext; pEntry2 != &pList2->ListHead; pEntry2 = pList2->ListHead.pNext)
    {
        PNEMUVR_REG pReg2 = PNEMUVR_REG_FROM_ENTRY(pEntry2);
        for (;;)
        {
            if (pEntry1 != &pList1->ListHead)
            {
                PNEMUVR_REG pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);
                if (nemuVrRegNonintersectedComparator(&pReg1->Rect, &pReg2->Rect) < 0)
                {
                    pEntry1 = pEntry1->pNext;
                    continue;
                }
            }
            nemuVrListRegRemove(pList2, pReg2);
            nemuVrListRegAdd(pList1, pReg2, pEntry1, false);
            break;
        }
    }

    Assert(NemuVrListIsEmpty(pList2));
}

static int nemuVrListRegIntersectSubstNoJoin(PNEMUVR_LIST pList1, PNEMUVR_REG pReg1, PCRTRECT  pRect2)
{
    uint32_t topLim = NEMUVR_INVALID_COORD;
    uint32_t bottomLim = NEMUVR_INVALID_COORD;
    RTLISTNODE List;
    PNEMUVR_REG pBottomReg = NULL;
#ifdef DEBUG_misha
    RTRECT tmpRect = pReg1->Rect;
    nemuVrDbgListVerify(pList1);
#endif
    Assert(!NemuRectIsZero(pRect2));

    RTListInit(&List);

    Assert(NemuRectIsIntersect(&pReg1->Rect, pRect2));

    if (pReg1->Rect.yTop < pRect2->yTop)
    {
        Assert(pRect2->yTop < pReg1->Rect.yBottom);
        PNEMUVR_REG pRegResult = nemuVrRegCreate();
        pRegResult->Rect.yTop = pReg1->Rect.yTop;
        pRegResult->Rect.xLeft = pReg1->Rect.xLeft;
        pRegResult->Rect.yBottom = pRect2->yTop;
        pRegResult->Rect.xRight = pReg1->Rect.xRight;
        topLim = pRect2->yTop;
        RTListAppend(&List, &pRegResult->ListEntry);
    }

    if (pReg1->Rect.yBottom > pRect2->yBottom)
    {
        Assert(pRect2->yBottom > pReg1->Rect.yTop);
        PNEMUVR_REG pRegResult = nemuVrRegCreate();
        pRegResult->Rect.yTop = pRect2->yBottom;
        pRegResult->Rect.xLeft = pReg1->Rect.xLeft;
        pRegResult->Rect.yBottom = pReg1->Rect.yBottom;
        pRegResult->Rect.xRight = pReg1->Rect.xRight;
        bottomLim = pRect2->yBottom;
        pBottomReg = pRegResult;
    }

    if (pReg1->Rect.xLeft < pRect2->xLeft)
    {
        Assert(pRect2->xLeft < pReg1->Rect.xRight);
        PNEMUVR_REG pRegResult = nemuVrRegCreate();
        pRegResult->Rect.yTop = topLim == NEMUVR_INVALID_COORD ? pReg1->Rect.yTop : topLim;
        pRegResult->Rect.xLeft = pReg1->Rect.xLeft;
        pRegResult->Rect.yBottom = bottomLim == NEMUVR_INVALID_COORD ? pReg1->Rect.yBottom : bottomLim;
        pRegResult->Rect.xRight = pRect2->xLeft;
        RTListAppend(&List, &pRegResult->ListEntry);
    }

    if (pReg1->Rect.xRight > pRect2->xRight)
    {
        Assert(pRect2->xRight > pReg1->Rect.xLeft);
        PNEMUVR_REG pRegResult = nemuVrRegCreate();
        pRegResult->Rect.yTop = topLim == NEMUVR_INVALID_COORD ? pReg1->Rect.yTop : topLim;
        pRegResult->Rect.xLeft = pRect2->xRight;
        pRegResult->Rect.yBottom = bottomLim == NEMUVR_INVALID_COORD ? pReg1->Rect.yBottom : bottomLim;
        pRegResult->Rect.xRight = pReg1->Rect.xRight;
        RTListAppend(&List, &pRegResult->ListEntry);
    }

    if (pBottomReg)
        RTListAppend(&List, &pBottomReg->ListEntry);

    PRTLISTNODE pMemberEntry = pReg1->ListEntry.pNext;
    nemuVrListRegRemove(pList1, pReg1);
    nemuVrRegTerm(pReg1);

    if (RTListIsEmpty(&List))
        return VINF_SUCCESS; /* the region is covered by the pRect2 */

    PRTLISTNODE pNext;
    PRTLISTNODE pEntry = List.pNext;
    for (; pEntry != &List; pEntry = pNext)
    {
        pNext = pEntry->pNext;
        PNEMUVR_REG pReg = PNEMUVR_REG_FROM_ENTRY(pEntry);

        nemuVrListRegAddOrder(pList1, pMemberEntry, pReg);
        pMemberEntry = pEntry->pNext; /* the following elements should go after the given pEntry since they are ordered already */
    }
    return VINF_SUCCESS;
}

/**
 * @returns Entry to be used for continuing the rectangles iterations being made currently on the callback call.
 *          ListHead is returned to break the current iteration
 * @param   ppNext      specifies next reg entry to be used for iteration. the default is pReg1->ListEntry.pNext */
typedef DECLCALLBACK(PRTLISTNODE) FNNEMUVR_CB_INTERSECTED_VISITOR(PNEMUVR_LIST pList1, PNEMUVR_REG pReg1,
                                                                  PCRTRECT pRect2, void *pvContext, PRTLISTNODE *ppNext);
typedef FNNEMUVR_CB_INTERSECTED_VISITOR *PFNNEMUVR_CB_INTERSECTED_VISITOR;

static void nemuVrListVisitIntersected(PNEMUVR_LIST pList1, uint32_t cRects, PCRTRECT aRects,
                                       PFNNEMUVR_CB_INTERSECTED_VISITOR pfnVisitor, void* pvVisitor)
{
    PRTLISTNODE pEntry1 = pList1->ListHead.pNext;
    PRTLISTNODE pNext1;
    uint32_t iFirst2 = 0;

    for (; pEntry1 != &pList1->ListHead; pEntry1 = pNext1)
    {
        pNext1 = pEntry1->pNext;
        PNEMUVR_REG pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);
        for (uint32_t i = iFirst2; i < cRects; ++i)
        {
            PCRTRECT pRect2 = &aRects[i];
            if (NemuRectIsZero(pRect2))
                continue;

            if (!NemuRectIsIntersect(&pReg1->Rect, pRect2))
                continue;

            /* the visitor can modify the list 1, apply necessary adjustments after it */
            pEntry1 = pfnVisitor (pList1, pReg1, pRect2, pvVisitor, &pNext1);
            if (pEntry1 == &pList1->ListHead)
                break;
            pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);
        }
    }
}

/**
 * @returns Entry to be iterated next. ListHead is returned to break the
 *          iteration
 */
typedef DECLCALLBACK(PRTLISTNODE) FNNEMUVR_CB_NONINTERSECTED_VISITOR(PNEMUVR_LIST pList1, PNEMUVR_REG pReg1, void *pvContext);
typedef FNNEMUVR_CB_NONINTERSECTED_VISITOR *PFNNEMUVR_CB_NONINTERSECTED_VISITOR;

static void nemuVrListVisitNonintersected(PNEMUVR_LIST pList1, uint32_t cRects, PCRTRECT aRects,
                                          PFNNEMUVR_CB_NONINTERSECTED_VISITOR pfnVisitor, void* pvVisitor)
{
    PRTLISTNODE pEntry1 = pList1->ListHead.pNext;
    PRTLISTNODE pNext1;
    uint32_t iFirst2 = 0;

    for (; pEntry1 != &pList1->ListHead; pEntry1 = pNext1)
    {
        PNEMUVR_REG pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);
        uint32_t i = iFirst2;
        for (; i < cRects; ++i)
        {
            PCRTRECT pRect2 = &aRects[i];
            if (NemuRectIsZero(pRect2))
                continue;

            if (NemuRectIsIntersect(&pReg1->Rect, pRect2))
                break;
        }

        if (i == cRects)
            pNext1 = pfnVisitor(pList1, pReg1, pvVisitor);
        else
            pNext1 = pEntry1->pNext;
    }
}

static void nemuVrListJoinRectsHV(PNEMUVR_LIST pList, bool fHorizontal)
{
    PRTLISTNODE pNext1, pNext2;

    for (PRTLISTNODE pEntry1 = pList->ListHead.pNext; pEntry1 != &pList->ListHead; pEntry1 = pNext1)
    {
        PNEMUVR_REG pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);
        pNext1 = pEntry1->pNext;
        for (PRTLISTNODE pEntry2 = pEntry1->pNext; pEntry2 != &pList->ListHead; pEntry2 = pNext2)
        {
            PNEMUVR_REG pReg2 = PNEMUVR_REG_FROM_ENTRY(pEntry2);
            pNext2 = pEntry2->pNext;
            if (fHorizontal)
            {
                if (pReg1->Rect.yTop == pReg2->Rect.yTop)
                {
                    if (pReg1->Rect.xRight == pReg2->Rect.xLeft)
                    {
                        /* join rectangles */
                        nemuVrListRegRemove(pList, pReg2);
                        if (pReg1->Rect.yBottom > pReg2->Rect.yBottom)
                        {
                            int32_t oldRight1 = pReg1->Rect.xRight;
                            int32_t oldBottom1 = pReg1->Rect.yBottom;
                            pReg1->Rect.xRight = pReg2->Rect.xRight;
                            pReg1->Rect.yBottom = pReg2->Rect.yBottom;

                            nemuVrDbgListVerify(pList);

                            pReg2->Rect.xLeft = pReg1->Rect.xLeft;
                            pReg2->Rect.yTop = pReg1->Rect.yBottom;
                            pReg2->Rect.xRight = oldRight1;
                            pReg2->Rect.yBottom = oldBottom1;
                            nemuVrListRegAddOrder(pList, pReg1->ListEntry.pNext, pReg2);
                            /* restart the pNext1 & pNext2 since regs are splitted into smaller ones in y dimension
                             * and thus can match one of the previous rects */
                            pNext1 = pList->ListHead.pNext;
                            break;
                        }

                        if (pReg1->Rect.yBottom < pReg2->Rect.yBottom)
                        {
                            pReg1->Rect.xRight = pReg2->Rect.xRight;
                            nemuVrDbgListVerify(pList);
                            pReg2->Rect.yTop = pReg1->Rect.yBottom;
                            nemuVrListRegAddOrder(pList, pReg1->ListEntry.pNext, pReg2);
                            /* restart the pNext1 & pNext2 since regs are splitted into smaller ones in y dimension
                             * and thus can match one of the previous rects */
                            pNext1 = pList->ListHead.pNext;
                            break;
                        }

                        pReg1->Rect.xRight = pReg2->Rect.xRight;
                        nemuVrDbgListVerify(pList);
                        /* reset the pNext1 since it could be the pReg2 being destroyed */
                        pNext1 = pEntry1->pNext;
                        /* pNext2 stays the same since it is pReg2->ListEntry.pNext, which is kept intact */
                        nemuVrRegTerm(pReg2);
                    }
                    continue;
                }
                else if (pReg1->Rect.yBottom == pReg2->Rect.yBottom)
                {
                    Assert(pReg1->Rect.yTop < pReg2->Rect.yTop); /* <- since pReg1 > pReg2 && pReg1->Rect.yTop != pReg2->Rect.yTop*/
                    if (pReg1->Rect.xRight == pReg2->Rect.xLeft)
                    {
                        /* join rectangles */
                        nemuVrListRegRemove(pList, pReg2);

                        pReg1->Rect.yBottom = pReg2->Rect.yTop;
                        nemuVrDbgListVerify(pList);
                        pReg2->Rect.xLeft = pReg1->Rect.xLeft;

                        nemuVrListRegAddOrder(pList, pNext2, pReg2);

                        /* restart the pNext1 & pNext2 since regs are splitted into smaller ones in y dimension
                         * and thus can match one of the previous rects */
                        pNext1 = pList->ListHead.pNext;
                        break;
                    }

                    if (pReg1->Rect.xLeft == pReg2->Rect.xRight)
                    {
                        /* join rectangles */
                        nemuVrListRegRemove(pList, pReg2);

                        pReg1->Rect.yBottom = pReg2->Rect.yTop;
                        nemuVrDbgListVerify(pList);
                        pReg2->Rect.xRight = pReg1->Rect.xRight;

                        nemuVrListRegAddOrder(pList, pNext2, pReg2);

                        /* restart the pNext1 & pNext2 since regs are splitted into smaller ones in y dimension
                         * and thus can match one of the previous rects */
                        pNext1 = pList->ListHead.pNext;
                        break;
                    }
                    continue;
                }
            }
            else
            {
                if (pReg1->Rect.yBottom == pReg2->Rect.yTop)
                {
                    if (pReg1->Rect.xLeft == pReg2->Rect.xLeft)
                    {
                        if (pReg1->Rect.xRight == pReg2->Rect.xRight)
                        {
                            /* join rects */
                            nemuVrListRegRemove(pList, pReg2);

                            pReg1->Rect.yBottom = pReg2->Rect.yBottom;
                            nemuVrDbgListVerify(pList);

                            /* reset the pNext1 since it could be the pReg2 being destroyed */
                            pNext1 = pEntry1->pNext;
                            /* pNext2 stays the same since it is pReg2->ListEntry.pNext, which is kept intact */
                            nemuVrRegTerm(pReg2);
                            continue;
                        }
                        /* no more to be done for for pReg1 */
                        break;
                    }

                    if (pReg1->Rect.xRight > pReg2->Rect.xLeft)
                    {
                        /* no more to be done for for pReg1 */
                        break;
                    }

                    continue;
                }

                if (pReg1->Rect.yBottom < pReg2->Rect.yTop)
                {
                    /* no more to be done for for pReg1 */
                    break;
                }
            }
        }
    }
}

static void nemuVrListJoinRects(PNEMUVR_LIST pList)
{
    nemuVrListJoinRectsHV(pList, true);
    nemuVrListJoinRectsHV(pList, false);
}

typedef struct NEMUVR_CBDATA_SUBST
{
    int rc;
    bool fChanged;
} NEMUVR_CBDATA_SUBST;
typedef NEMUVR_CBDATA_SUBST *PNEMUVR_CBDATA_SUBST;

static DECLCALLBACK(PRTLISTNODE) nemuVrListSubstNoJoinCb(PNEMUVR_LIST pList, PNEMUVR_REG pReg1, PCRTRECT pRect2,
                                                         void *pvContext, PRTLISTNODE *ppNext)
{
    PNEMUVR_CBDATA_SUBST pData = (PNEMUVR_CBDATA_SUBST)pvContext;
    /* store the prev to get the new pNext out of it*/
    PRTLISTNODE pPrev = pReg1->ListEntry.pPrev;
    pData->fChanged = true;

    Assert(NemuRectIsIntersect(&pReg1->Rect, pRect2));

    /* NOTE: the pReg1 will be invalid after the nemuVrListRegIntersectSubstNoJoin call!!! */
    int rc = nemuVrListRegIntersectSubstNoJoin(pList, pReg1, pRect2);
    if (RT_SUCCESS(rc))
    {
        *ppNext = pPrev->pNext;
        return &pList->ListHead;
    }
    WARN(("nemuVrListRegIntersectSubstNoJoin failed!"));
    Assert(!RT_SUCCESS(rc));
    pData->rc = rc;
    *ppNext = &pList->ListHead;
    return &pList->ListHead;
}

static int nemuVrListSubstNoJoin(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT aRects, bool *pfChanged)
{
    if (pfChanged)
        *pfChanged = false;

    if (NemuVrListIsEmpty(pList))
        return VINF_SUCCESS;

    NEMUVR_CBDATA_SUBST Data;
    Data.rc = VINF_SUCCESS;
    Data.fChanged = false;

    nemuVrListVisitIntersected(pList, cRects, aRects, nemuVrListSubstNoJoinCb, &Data);
    if (!RT_SUCCESS(Data.rc))
    {
        WARN(("nemuVrListVisitIntersected failed!"));
        return Data.rc;
    }

    if (pfChanged)
        *pfChanged = Data.fChanged;

    return VINF_SUCCESS;
}

#if 0
static PCRTRECT nemuVrRectsOrder(uint32_t cRects, PCRTRECT  aRects)
{
#ifdef NEMU_STRICT
    for (uint32_t i = 0; i < cRects; ++i)
    {
        PRTRECT pRectI = &aRects[i];
        for (uint32_t j = i + 1; j < cRects; ++j)
        {
            PRTRECT pRectJ = &aRects[j];
            Assert(!NemuRectIsIntersect(pRectI, pRectJ));
        }
    }
#endif

    PRTRECT pRects = (PRTRECT)aRects;
    /* check if rects are ordered already */
    for (uint32_t i = 0; i < cRects - 1; ++i)
    {
        PRTRECT pRect1 = &pRects[i];
        PRTRECT pRect2 = &pRects[i+1];
        if (nemuVrRegNonintersectedComparator(pRect1, pRect2) < 0)
            continue;

        WARN(("rects are unoreded!"));

        if (pRects == aRects)
        {
            pRects = (PRTRECT)RTMemAlloc(sizeof(RTRECT) * cRects);
            if (!pRects)
            {
                WARN(("RTMemAlloc failed!"));
                return NULL;
            }

            memcpy(pRects, aRects, sizeof(RTRECT) * cRects);
        }

        Assert(pRects != aRects);

        int j = (int)i - 1;
        for (;;)
        {
            RTRECT Tmp = *pRect1;
            *pRect1 = *pRect2;
            *pRect2 = Tmp;

            if (j < 0)
                break;

            if (nemuVrRegNonintersectedComparator(pRect1, pRect1-1) > 0)
                break;

            pRect2 = pRect1--;
            --j;
        }
    }

    return pRects;
}
#endif

NEMUVREGDECL(void) NemuVrListTranslate(PNEMUVR_LIST pList, int32_t x, int32_t y)
{
    for (PRTLISTNODE pEntry1 = pList->ListHead.pNext; pEntry1 != &pList->ListHead; pEntry1 = pEntry1->pNext)
    {
        PNEMUVR_REG pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);
        NemuRectTranslate(&pReg1->Rect, x, y);
    }
}

static DECLCALLBACK(PRTLISTNODE) nemuVrListIntersectNoJoinNonintersectedCb(PNEMUVR_LIST pList1, PNEMUVR_REG pReg1, void *pvContext)
{
    NEMUVR_CBDATA_SUBST *pData = (NEMUVR_CBDATA_SUBST*)pvContext;

    PRTLISTNODE pNext = pReg1->ListEntry.pNext;

    nemuVrDbgListVerify(pList1);

    nemuVrListRegRemove(pList1, pReg1);
    nemuVrRegTerm(pReg1);

    nemuVrDbgListVerify(pList1);

    pData->fChanged = true;

    return pNext;
}

static DECLCALLBACK(PRTLISTNODE) nemuVrListIntersectNoJoinIntersectedCb(PNEMUVR_LIST pList1, PNEMUVR_REG pReg1, PCRTRECT pRect2,
                                                                        void *pvContext, PPRTLISTNODE ppNext)
{
    PNEMUVR_CBDATA_SUBST pData = (PNEMUVR_CBDATA_SUBST)pvContext;
    pData->fChanged = true;

    nemuVrDbgListVerify(pList1);

    PRTLISTNODE pMemberEntry = pReg1->ListEntry.pNext;

    Assert(NemuRectIsIntersect(&pReg1->Rect, pRect2));
    Assert(!NemuRectIsZero(pRect2));

    nemuVrListRegRemove(pList1, pReg1);
    NemuRectIntersect(&pReg1->Rect, pRect2);
    Assert(!NemuRectIsZero(&pReg1->Rect));

    nemuVrListRegAddOrder(pList1, pMemberEntry, pReg1);

    nemuVrDbgListVerify(pList1);

    return &pReg1->ListEntry;
}

static int nemuVrListIntersectNoJoin(PNEMUVR_LIST pList, PCNEMUVR_LIST pList2, bool *pfChanged)
{
    bool fChanged = false;
    *pfChanged = false;

    if (NemuVrListIsEmpty(pList))
        return VINF_SUCCESS;

    if (NemuVrListIsEmpty(pList2))
    {
        if (pfChanged)
            *pfChanged = true;

        NemuVrListClear(pList);
        return VINF_SUCCESS;
    }

    PRTLISTNODE pNext1;

    for (PRTLISTNODE pEntry1 = pList->ListHead.pNext; pEntry1 != &pList->ListHead; pEntry1 = pNext1)
    {
        pNext1 = pEntry1->pNext;
        PNEMUVR_REG pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);
        RTRECT RegRect1 = pReg1->Rect;
        PRTLISTNODE pMemberEntry = pReg1->ListEntry.pNext;

        for (const RTLISTNODE *pEntry2 = pList2->ListHead.pNext; pEntry2 != &pList2->ListHead; pEntry2 = pEntry2->pNext)
        {
            PCNEMUVR_REG pReg2 = PNEMUVR_REG_FROM_ENTRY(pEntry2);
            PCRTRECT pRect2 = &pReg2->Rect;

            if (!NemuRectIsIntersect(&RegRect1, pRect2))
                continue;

            if (pReg1)
            {
                if (NemuRectCovers(pRect2, &RegRect1))
                {
                    /* no change */

                    /* zero up the pReg1 to mark it as intersected (see the code after this inner loop) */
                    pReg1 = NULL;

                    if (!NemuRectCmp(pRect2, &RegRect1))
                        break; /* and we can break the iteration here */
                }
                else
                {
                    /*just to ensure the NemuRectCovers is true for equal rects */
                    Assert(NemuRectCmp(pRect2, &RegRect1));

                    /* @todo: this can have false-alarming sometimes if the separated rects will then be joind into the original rect,
                     * so far this should not be a problem for VReg clients, so keep it this way for now  */
                    fChanged = true;

                    /* re-use the reg entry */
                    nemuVrListRegRemove(pList, pReg1);
                    NemuRectIntersect(&pReg1->Rect, pRect2);
                    Assert(!NemuRectIsZero(&pReg1->Rect));

                    nemuVrListRegAddOrder(pList, pMemberEntry, pReg1);
                    pReg1 = NULL;
                }
            }
            else
            {
                Assert(fChanged); /* <- should be set by the if branch above */
                PNEMUVR_REG pReg = nemuVrRegCreate();
                if (!pReg)
                {
                    WARN(("nemuVrRegCreate failed!"));
                    return VERR_NO_MEMORY;
                }
                NemuRectIntersected(&RegRect1, pRect2, &pReg->Rect);
                Assert(!NemuRectIsZero(&pReg->Rect));
                nemuVrListRegAddOrder(pList, pList->ListHead.pNext, pReg);
            }
        }

        if (pReg1)
        {
            /* the region has no intersections, remove it */
            nemuVrListRegRemove(pList, pReg1);
            nemuVrRegTerm(pReg1);
            fChanged = true;
        }
    }

    *pfChanged = fChanged;
    return VINF_SUCCESS;
}

NEMUVREGDECL(int) NemuVrListIntersect(PNEMUVR_LIST pList, PCNEMUVR_LIST pList2, bool *pfChanged)
{
    if (pfChanged)
        *pfChanged = false;

    int rc = nemuVrListIntersectNoJoin(pList, pList2, pfChanged);
    if (!RT_SUCCESS(rc))
    {
        WARN(("nemuVrListSubstNoJoin failed!"));
        return rc;
    }

    if (*pfChanged)
    {
        nemuVrListJoinRects(pList);
    }

    return rc;
}

NEMUVREGDECL(int) NemuVrListRectsIntersect(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT aRects, bool *pfChanged)
{
    if (pfChanged)
        *pfChanged = false;

    if (NemuVrListIsEmpty(pList))
        return VINF_SUCCESS;

    if (!cRects)
    {
        if (pfChanged)
            *pfChanged = true;

        NemuVrListClear(pList);
        return VINF_SUCCESS;
    }

    /* we perform intersection using lists because the algorythm axpects the rects to be non-intersected,
     * which list guaranties to us */

    NEMUVR_LIST TmpList;
    NemuVrListInit(&TmpList);

    int rc = NemuVrListRectsAdd(&TmpList, cRects, aRects, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = NemuVrListIntersect(pList, &TmpList, pfChanged);
        if (!RT_SUCCESS(rc))
        {
            WARN(("NemuVrListIntersect failed! rc %d", rc));
        }
    }
    else
    {
        WARN(("NemuVrListRectsAdd failed, rc %d", rc));
    }
    NemuVrListClear(&TmpList);

    return rc;
}

NEMUVREGDECL(int) NemuVrListRectsSubst(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT aRects, bool *pfChanged)
{
#if 0
    PCRTRECT  pRects = nemuVrRectsOrder(cRects, aRects);
    if (!pRects)
    {
        WARN(("nemuVrRectsOrder failed!"));
        return VERR_NO_MEMORY;
    }
#endif

    bool fChanged = false;

    int rc = nemuVrListSubstNoJoin(pList, cRects, aRects, &fChanged);
    if (!RT_SUCCESS(rc))
    {
        WARN(("nemuVrListSubstNoJoin failed!"));
        goto done;
    }

    if (fChanged)
        goto done;

    nemuVrListJoinRects(pList);

done:
#if 0
    if (pRects != aRects)
        RTMemFree(pRects);
#endif

    if (pfChanged)
        *pfChanged = fChanged;

    return rc;
}

NEMUVREGDECL(int) NemuVrListRectsSet(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT aRects, bool *pfChanged)
{
    if (pfChanged)
        *pfChanged = false;

    if (!cRects && NemuVrListIsEmpty(pList))
        return VINF_SUCCESS;

    /* @todo: fChanged will have false alarming here, fix if needed */
    NemuVrListClear(pList);

    int rc = NemuVrListRectsAdd(pList, cRects, aRects, NULL);
    if (!RT_SUCCESS(rc))
    {
        WARN(("NemuVrListRectsSet failed rc %d", rc));
        return rc;
    }

    if (pfChanged)
        *pfChanged = true;

    return VINF_SUCCESS;
}

NEMUVREGDECL(int) NemuVrListRectsAdd(PNEMUVR_LIST pList, uint32_t cRects, PCRTRECT aRects, bool *pfChanged)
{
    uint32_t cCovered = 0;

    if (pfChanged)
        *pfChanged = false;

#if 0
#ifdef NEMU_STRICT
        for (uint32_t i = 0; i < cRects; ++i)
        {
            PRTRECT pRectI = &aRects[i];
            for (uint32_t j = i + 1; j < cRects; ++j)
            {
                PRTRECT pRectJ = &aRects[j];
                Assert(!NemuRectIsIntersect(pRectI, pRectJ));
            }
        }
#endif
#endif

    /* early sort out the case when there are no new rects */
    for (uint32_t i = 0; i < cRects; ++i)
    {
        if (NemuRectIsZero(&aRects[i]))
        {
            cCovered++;
            continue;
        }

        for (PRTLISTNODE pEntry1 = pList->ListHead.pNext; pEntry1 != &pList->ListHead; pEntry1 = pEntry1->pNext)
        {
            PNEMUVR_REG pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);

            if (NemuRectCovers(&pReg1->Rect, &aRects[i]))
            {
                cCovered++;
                break;
            }
        }
    }

    if (cCovered == cRects)
        return VINF_SUCCESS;

    /* rects are not covered, need to go the slow way */

    NEMUVR_LIST DiffList;
    NemuVrListInit(&DiffList);
    PRTRECT pListRects = NULL;
    uint32_t cAllocatedRects = 0;
    bool fNeedRectreate = true;
    bool fChanged = false;
    int rc = VINF_SUCCESS;

    for (uint32_t i = 0; i < cRects; ++i)
    {
        if (NemuRectIsZero(&aRects[i]))
            continue;

        PNEMUVR_REG pReg = nemuVrRegCreate();
        if (!pReg)
        {
            WARN(("nemuVrRegCreate failed!"));
            rc = VERR_NO_MEMORY;
            break;
        }
        pReg->Rect = aRects[i];

        uint32_t cListRects = NemuVrListRectsCount(pList);
        if (!cListRects)
        {
            nemuVrListRegAdd(pList, pReg, &pList->ListHead, false);
            fChanged = true;
            continue;
        }
        Assert(NemuVrListIsEmpty(&DiffList));
        nemuVrListRegAdd(&DiffList, pReg, &DiffList.ListHead, false);

        if (cAllocatedRects < cListRects)
        {
            cAllocatedRects = cListRects + cRects;
            Assert(fNeedRectreate);
            if (pListRects)
                RTMemFree(pListRects);
            pListRects = (RTRECT *)RTMemAlloc(sizeof(RTRECT) * cAllocatedRects);
            if (!pListRects)
            {
                WARN(("RTMemAlloc failed!"));
                rc = VERR_NO_MEMORY;
                break;
            }
        }


        if (fNeedRectreate)
        {
            rc = NemuVrListRectsGet(pList, cListRects, pListRects);
            Assert(rc == VINF_SUCCESS);
            fNeedRectreate = false;
        }

        bool fDummyChanged = false;
        rc = nemuVrListSubstNoJoin(&DiffList, cListRects, pListRects, &fDummyChanged);
        if (!RT_SUCCESS(rc))
        {
            WARN(("nemuVrListSubstNoJoin failed!"));
            rc = VERR_NO_MEMORY;
            break;
        }

        if (!NemuVrListIsEmpty(&DiffList))
        {
            nemuVrListAddNonintersected(pList, &DiffList);
            fNeedRectreate = true;
            fChanged = true;
        }

        Assert(NemuVrListIsEmpty(&DiffList));
    }

    if (pListRects)
        RTMemFree(pListRects);

    Assert(NemuVrListIsEmpty(&DiffList) || rc != VINF_SUCCESS);
    NemuVrListClear(&DiffList);

    if (fChanged)
        nemuVrListJoinRects(pList);

    if (pfChanged)
        *pfChanged = fChanged;

    return VINF_SUCCESS;
}

NEMUVREGDECL(int) NemuVrListRectsGet(PNEMUVR_LIST pList, uint32_t cRects, RTRECT * aRects)
{
    if (cRects < NemuVrListRectsCount(pList))
        return VERR_BUFFER_OVERFLOW;

    uint32_t i = 0;
    for (PRTLISTNODE pEntry1 = pList->ListHead.pNext; pEntry1 != &pList->ListHead; pEntry1 = pEntry1->pNext, ++i)
    {
        PNEMUVR_REG pReg1 = PNEMUVR_REG_FROM_ENTRY(pEntry1);
        aRects[i] = pReg1->Rect;
    }
    return VINF_SUCCESS;
}

NEMUVREGDECL(int) NemuVrListCmp(const NEMUVR_LIST *pList1, const NEMUVR_LIST *pList2)
{
    int cTmp = pList1->cEntries - pList2->cEntries;
    if (cTmp)
        return cTmp;

    PNEMUVR_REG pReg1, pReg2;

    for (pReg1 = RTListNodeGetNext(&pList1->ListHead, NEMUVR_REG, ListEntry),
         pReg2 = RTListNodeGetNext(&pList2->ListHead, NEMUVR_REG, ListEntry);

         !RTListNodeIsDummy(&pList1->ListHead, pReg1, NEMUVR_REG, ListEntry);

         pReg1 = RT_FROM_MEMBER(pReg1->ListEntry.pNext, NEMUVR_REG, ListEntry),
         pReg2 = RT_FROM_MEMBER(pReg2->ListEntry.pNext, NEMUVR_REG, ListEntry) )
    {
        Assert(!RTListNodeIsDummy(&pList2->ListHead, pReg2, NEMUVR_REG, ListEntry));
        cTmp = NemuRectCmp(&pReg1->Rect, &pReg2->Rect);
        if (cTmp)
            return cTmp;
    }
    Assert(RTListNodeIsDummy(&pList2->ListHead, pReg2, NEMUVR_REG, ListEntry));
    return 0;
}

NEMUVREGDECL(int) NemuVrListClone(PCNEMUVR_LIST pList, PNEMUVR_LIST pDstList)
{
    NemuVrListInit(pDstList);
    PCNEMUVR_REG pReg;
    RTListForEach(&pList->ListHead, pReg, const NEMUVR_REG, ListEntry)
    {
        PNEMUVR_REG pDstReg = nemuVrRegCreate();
        if (!pDstReg)
        {
            WARN(("nemuVrRegLaAlloc failed"));
            NemuVrListClear(pDstList);
            return VERR_NO_MEMORY;
        }
        pDstReg->Rect = pReg->Rect;
        nemuVrListRegAdd(pDstList, pDstReg, &pDstList->ListHead, true /*bool fAfter*/);
    }

    Assert(pDstList->cEntries == pList->cEntries);

    return VINF_SUCCESS;
}

NEMUVREGDECL(void) NemuVrCompositorInit(PNEMUVR_COMPOSITOR pCompositor, PFNNEMUVRCOMPOSITOR_ENTRY_RELEASED pfnEntryReleased)
{
    RTListInit(&pCompositor->List);
    pCompositor->pfnEntryReleased = pfnEntryReleased;
}

NEMUVREGDECL(void) NemuVrCompositorRegionsClear(PNEMUVR_COMPOSITOR pCompositor, bool *pfChanged)
{
    bool fChanged = false;
    PNEMUVR_COMPOSITOR_ENTRY pEntry, pEntryNext;
    RTListForEachSafe(&pCompositor->List, pEntry, pEntryNext, NEMUVR_COMPOSITOR_ENTRY, Node)
    {
        NemuVrCompositorEntryRemove(pCompositor, pEntry);
        fChanged = true;
    }

    if (pfChanged)
        *pfChanged = fChanged;
}

NEMUVREGDECL(void) NemuVrCompositorClear(PNEMUVR_COMPOSITOR pCompositor)
{
    NemuVrCompositorRegionsClear(pCompositor, NULL);
}

DECLINLINE(void) nemuVrCompositorEntryRelease(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                              PNEMUVR_COMPOSITOR_ENTRY pReplacingEntry)
{
    if (--pEntry->cRefs)
    {
        Assert(pEntry->cRefs < UINT32_MAX/2);
        return;
    }

    Assert(!NemuVrCompositorEntryIsInList(pEntry));

    if (pCompositor->pfnEntryReleased)
        pCompositor->pfnEntryReleased(pCompositor, pEntry, pReplacingEntry);
}

DECLINLINE(void) nemuVrCompositorEntryAddRef(PNEMUVR_COMPOSITOR_ENTRY pEntry)
{
    ++pEntry->cRefs;
}

DECLINLINE(void) nemuVrCompositorEntryAdd(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry)
{
    RTListPrepend(&pCompositor->List, &pEntry->Node);
    nemuVrCompositorEntryAddRef(pEntry);
}

DECLINLINE(void) nemuVrCompositorEntryRemove(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                             PNEMUVR_COMPOSITOR_ENTRY pReplacingEntry)
{
    RTListNodeRemove(&pEntry->Node);
    nemuVrCompositorEntryRelease(pCompositor, pEntry, pReplacingEntry);
}

static void nemuVrCompositorEntryReplace(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                         PNEMUVR_COMPOSITOR_ENTRY pReplacingEntry)
{
    NemuVrListMoveTo(&pEntry->Vr, &pReplacingEntry->Vr);

    pReplacingEntry->Node = pEntry->Node;
    pReplacingEntry->Node.pNext->pPrev = &pReplacingEntry->Node;
    pReplacingEntry->Node.pPrev->pNext = &pReplacingEntry->Node;
    pEntry->Node.pNext = NULL;
    pEntry->Node.pPrev = NULL;

    nemuVrCompositorEntryAddRef(pReplacingEntry);
    nemuVrCompositorEntryRelease(pCompositor, pEntry, pReplacingEntry);
}



NEMUVREGDECL(void) NemuVrCompositorEntryInit(PNEMUVR_COMPOSITOR_ENTRY pEntry)
{
    NemuVrListInit(&pEntry->Vr);
    pEntry->cRefs = 0;
}

NEMUVREGDECL(bool) NemuVrCompositorEntryRemove(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry)
{
    if (!NemuVrCompositorEntryIsInList(pEntry))
        return false;

    nemuVrCompositorEntryAddRef(pEntry);

    NemuVrListClear(&pEntry->Vr);
    nemuVrCompositorEntryRemove(pCompositor, pEntry, NULL);
    nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);
    return true;
}

NEMUVREGDECL(bool) NemuVrCompositorEntryReplace(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                PNEMUVR_COMPOSITOR_ENTRY pNewEntry)
{
    if (!NemuVrCompositorEntryIsInList(pEntry))
        return false;

    nemuVrCompositorEntryReplace(pCompositor, pEntry, pNewEntry);

    return true;
}

static int nemuVrCompositorEntryRegionsSubst(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                             uint32_t cRects, PCRTRECT paRects, bool *pfChanged)
{
    bool fChanged;
    nemuVrCompositorEntryAddRef(pEntry);

    int rc = NemuVrListRectsSubst(&pEntry->Vr, cRects, paRects, &fChanged);
    if (RT_SUCCESS(rc))
    {
        if (NemuVrListIsEmpty(&pEntry->Vr))
        {
            Assert(fChanged);
            nemuVrCompositorEntryRemove(pCompositor, pEntry, NULL);
        }
        if (pfChanged)
            *pfChanged = false;
    }
    else
        WARN(("NemuVrListRectsSubst failed, rc %d", rc));

    nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);
    return rc;
}

NEMUVREGDECL(int) NemuVrCompositorEntryRegionsAdd(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                  uint32_t cRects, PCRTRECT paRects, PNEMUVR_COMPOSITOR_ENTRY *ppReplacedEntry,
                                                  uint32_t *pfChangeFlags)
{
    bool fOthersChanged = false;
    bool fCurChanged = false;
    bool fEntryChanged = false;
    bool fEntryWasInList = false;
    PNEMUVR_COMPOSITOR_ENTRY pCur;
    PNEMUVR_COMPOSITOR_ENTRY pNext;
    PNEMUVR_COMPOSITOR_ENTRY pReplacedEntry = NULL;
    int rc = VINF_SUCCESS;

    if (pEntry)
        nemuVrCompositorEntryAddRef(pEntry);

    if (!cRects)
    {
        if (pfChangeFlags)
            *pfChangeFlags = 0;
        if (pEntry)
            nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);
        return VINF_SUCCESS;
    }

    if (pEntry)
    {
        fEntryWasInList = NemuVrCompositorEntryIsInList(pEntry);
        rc = NemuVrListRectsAdd(&pEntry->Vr, cRects, paRects, &fEntryChanged);
        if (RT_SUCCESS(rc))
        {
            if (NemuVrListIsEmpty(&pEntry->Vr))
            {
//                WARN(("Empty rectangles passed in, is it expected?"));
                if (pfChangeFlags)
                    *pfChangeFlags = 0;
                nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);
                return VINF_SUCCESS;
            }
        }
        else
        {
            WARN(("NemuVrListRectsAdd failed, rc %d", rc));
            nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);
            return rc;
        }

        Assert(!NemuVrListIsEmpty(&pEntry->Vr));
    }
    else
    {
        fEntryChanged = true;
    }

    RTListForEachSafe(&pCompositor->List, pCur, pNext, NEMUVR_COMPOSITOR_ENTRY, Node)
    {
        Assert(!NemuVrListIsEmpty(&pCur->Vr));
        if (pCur != pEntry)
        {
            if (pEntry && !NemuVrListCmp(&pCur->Vr, &pEntry->Vr))
            {
                NemuVrListClear(&pCur->Vr);
                pReplacedEntry = pCur;
                nemuVrCompositorEntryAddRef(pReplacedEntry);
                nemuVrCompositorEntryRemove(pCompositor, pCur, pEntry);
                if (ppReplacedEntry)
                    *ppReplacedEntry = pReplacedEntry;
                break;
            }

            rc = nemuVrCompositorEntryRegionsSubst(pCompositor, pCur, cRects, paRects, &fCurChanged);
            if (RT_SUCCESS(rc))
                fOthersChanged |= fCurChanged;
            else
            {
                WARN(("nemuVrCompositorEntryRegionsSubst failed, rc %d", rc));
                return rc;
            }
        }
    }

    AssertRC(rc);

    if (pEntry)
    {
        if (!fEntryWasInList)
        {
            Assert(!NemuVrListIsEmpty(&pEntry->Vr));
            nemuVrCompositorEntryAdd(pCompositor, pEntry);
        }
        nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);
    }

    uint32_t fFlags = 0;
    if (fOthersChanged)
    {
        Assert(!pReplacedEntry);
        fFlags = NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED | NEMUVR_COMPOSITOR_CF_REGIONS_CHANGED
               | NEMUVR_COMPOSITOR_CF_OTHER_ENTRIES_REGIONS_CHANGED;
    }
    else if (pReplacedEntry)
    {
        nemuVrCompositorEntryRelease(pCompositor, pReplacedEntry, pEntry);
        Assert(fEntryChanged);
        fFlags = NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED | NEMUVR_COMPOSITOR_CF_ENTRY_REPLACED;
    }
    else if (fEntryChanged)
    {
        Assert(!pReplacedEntry);
        fFlags = NEMUVR_COMPOSITOR_CF_ENTRY_REGIONS_CHANGED | NEMUVR_COMPOSITOR_CF_REGIONS_CHANGED;
    }
    else
    {
        Assert(!pReplacedEntry);
    }

    if (!fEntryWasInList)
        Assert(fEntryChanged);

    if (pfChangeFlags)
        *pfChangeFlags = fFlags;

    return VINF_SUCCESS;
}

NEMUVREGDECL(int) NemuVrCompositorEntryRegionsSubst(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                    uint32_t cRects, PCRTRECT paRects, bool *pfChanged)
{
    if (!pEntry)
    {
        WARN(("NemuVrCompositorEntryRegionsSubst called with zero entry, unsupported!"));
        if (pfChanged)
            *pfChanged = false;
        return VERR_INVALID_PARAMETER;
    }

    nemuVrCompositorEntryAddRef(pEntry);

    if (NemuVrListIsEmpty(&pEntry->Vr))
    {
        if (pfChanged)
            *pfChanged = false;
        nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);
        return VINF_SUCCESS;
    }

    int rc = nemuVrCompositorEntryRegionsSubst(pCompositor, pEntry, cRects, paRects, pfChanged);
    if (!RT_SUCCESS(rc))
        WARN(("pfChanged failed, rc %d", rc));

    nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);

    return rc;
}

NEMUVREGDECL(int) NemuVrCompositorEntryRegionsSet(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                  uint32_t cRects, PCRTRECT paRects, bool *pfChanged)
{
    if (!pEntry)
    {
        WARN(("NemuVrCompositorEntryRegionsSet called with zero entry, unsupported!"));
        if (pfChanged)
            *pfChanged = false;
        return VERR_INVALID_PARAMETER;
    }

    nemuVrCompositorEntryAddRef(pEntry);

    bool fChanged = false, fCurChanged = false;
    uint32_t fChangeFlags = 0;
    int rc;
    fCurChanged = NemuVrCompositorEntryRemove(pCompositor, pEntry);
    fChanged |= fCurChanged;

    rc = NemuVrCompositorEntryRegionsAdd(pCompositor, pEntry, cRects, paRects, NULL, &fChangeFlags);
    if (RT_SUCCESS(rc))
    {
        fChanged |= !!fChangeFlags;
        if (pfChanged)
            *pfChanged = fChanged;
    }
    else
        WARN(("NemuVrCompositorEntryRegionsAdd failed, rc %d", rc));

    nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);

    return VINF_SUCCESS;
}

NEMUVREGDECL(int) NemuVrCompositorEntryListIntersect(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                     PCNEMUVR_LIST pList2, bool *pfChanged)
{
    int rc = VINF_SUCCESS;
    bool fChanged = false;

    nemuVrCompositorEntryAddRef(pEntry);

    if (NemuVrCompositorEntryIsInList(pEntry))
    {
        rc = NemuVrListIntersect(&pEntry->Vr, pList2, &fChanged);
        if (RT_SUCCESS(rc))
        {
            if (NemuVrListIsEmpty(&pEntry->Vr))
            {
                Assert(fChanged);
                nemuVrCompositorEntryRemove(pCompositor, pEntry, NULL);
            }
        }
        else
        {
            WARN(("NemuVrListRectsIntersect failed, rc %d", rc));
        }
    }

    if (pfChanged)
        *pfChanged = fChanged;

    nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);

    return rc;
}

NEMUVREGDECL(int) NemuVrCompositorEntryRegionsIntersect(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                        uint32_t cRects, PCRTRECT paRects, bool *pfChanged)
{
    int rc = VINF_SUCCESS;
    bool fChanged = false;

    nemuVrCompositorEntryAddRef(pEntry);

    if (NemuVrCompositorEntryIsInList(pEntry))
    {
        rc = NemuVrListRectsIntersect(&pEntry->Vr, cRects, paRects, &fChanged);
        if (RT_SUCCESS(rc))
        {
            if (NemuVrListIsEmpty(&pEntry->Vr))
            {
                Assert(fChanged);
                nemuVrCompositorEntryRemove(pCompositor, pEntry, NULL);
            }
        }
        else
        {
            WARN(("NemuVrListRectsIntersect failed, rc %d", rc));
        }
    }

    if (pfChanged)
        *pfChanged = fChanged;

    nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);

    return rc;
}

NEMUVREGDECL(int) NemuVrCompositorEntryListIntersectAll(PNEMUVR_COMPOSITOR pCompositor, PCNEMUVR_LIST pList2, bool *pfChanged)
{
    NEMUVR_COMPOSITOR_ITERATOR Iter;
    NemuVrCompositorIterInit(pCompositor, &Iter);
    PNEMUVR_COMPOSITOR_ENTRY pEntry;
    int rc = VINF_SUCCESS;
    bool fChanged = false;

    while ((pEntry = NemuVrCompositorIterNext(&Iter)) != NULL)
    {
        bool fTmpChanged = false;
        int tmpRc = NemuVrCompositorEntryListIntersect(pCompositor, pEntry, pList2, &fTmpChanged);
        if (RT_SUCCESS(tmpRc))
            fChanged |= fTmpChanged;
        else
        {
            WARN(("NemuVrCompositorEntryRegionsIntersect failed, rc %d", tmpRc));
            rc = tmpRc;
        }
    }

    if (pfChanged)
        *pfChanged = fChanged;

    return rc;
}

NEMUVREGDECL(int) NemuVrCompositorEntryRegionsIntersectAll(PNEMUVR_COMPOSITOR pCompositor, uint32_t cRegions, PCRTRECT paRegions,
                                                           bool *pfChanged)
{
    NEMUVR_COMPOSITOR_ITERATOR Iter;
    NemuVrCompositorIterInit(pCompositor, &Iter);
    PNEMUVR_COMPOSITOR_ENTRY pEntry;
    int rc = VINF_SUCCESS;
    bool fChanged = false;

    while ((pEntry = NemuVrCompositorIterNext(&Iter)) != NULL)
    {
        bool fTmpChanged = false;
        int tmpRc = NemuVrCompositorEntryRegionsIntersect(pCompositor, pEntry, cRegions, paRegions, &fTmpChanged);
        if (RT_SUCCESS(tmpRc))
            fChanged |= fTmpChanged;
        else
        {
            WARN(("NemuVrCompositorEntryRegionsIntersect failed, rc %d", tmpRc));
            rc = tmpRc;
        }
    }

    if (pfChanged)
        *pfChanged = fChanged;

    return rc;
}

NEMUVREGDECL(int) NemuVrCompositorEntryRegionsTranslate(PNEMUVR_COMPOSITOR pCompositor, PNEMUVR_COMPOSITOR_ENTRY pEntry,
                                                        int32_t x, int32_t y, bool *pfChanged)
{
    if (!pEntry)
    {
        WARN(("NemuVrCompositorEntryRegionsTranslate called with zero entry, unsupported!"));
        if (pfChanged)
            *pfChanged = false;
        return VERR_INVALID_PARAMETER;
    }

    nemuVrCompositorEntryAddRef(pEntry);

    if (   (!x && !y)
        || !NemuVrCompositorEntryIsInList(pEntry))
    {
        if (pfChanged)
            *pfChanged = false;

        nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);
        return VINF_SUCCESS;
    }

    NemuVrListTranslate(&pEntry->Vr, x, y);

    Assert(!NemuVrListIsEmpty(&pEntry->Vr));

    PNEMUVR_COMPOSITOR_ENTRY pCur;
    uint32_t cRects = 0;
    PRTRECT paRects = NULL;
    int rc = VINF_SUCCESS;
    RTListForEach(&pCompositor->List, pCur, NEMUVR_COMPOSITOR_ENTRY, Node)
    {
        Assert(!NemuVrListIsEmpty(&pCur->Vr));

        if (pCur == pEntry)
            continue;

        if (!paRects)
        {
            cRects = NemuVrListRectsCount(&pEntry->Vr);
            Assert(cRects);
            paRects = (RTRECT*)RTMemAlloc(cRects * sizeof(RTRECT));
            if (!paRects)
            {
                WARN(("RTMemAlloc failed!"));
                rc = VERR_NO_MEMORY;
                break;
            }

            rc = NemuVrListRectsGet(&pEntry->Vr, cRects, paRects);
            if (!RT_SUCCESS(rc))
            {
                WARN(("NemuVrListRectsGet failed! rc %d", rc));
                break;
            }
        }

        rc = nemuVrCompositorEntryRegionsSubst(pCompositor, pCur, cRects, paRects, NULL);
        if (!RT_SUCCESS(rc))
        {
            WARN(("nemuVrCompositorEntryRegionsSubst failed! rc %d", rc));
            break;
        }
    }

    if (pfChanged)
        *pfChanged = true;

    if (paRects)
        RTMemFree(paRects);

    nemuVrCompositorEntryRelease(pCompositor, pEntry, NULL);

    return rc;
}

NEMUVREGDECL(void) NemuVrCompositorVisit(PNEMUVR_COMPOSITOR pCompositor, PFNNEMUVRCOMPOSITOR_VISITOR pfnVisitor, void *pvVisitor)
{
    PNEMUVR_COMPOSITOR_ENTRY pEntry, pEntryNext;
    RTListForEachSafe(&pCompositor->List, pEntry, pEntryNext, NEMUVR_COMPOSITOR_ENTRY, Node)
    {
        if (!pfnVisitor(pCompositor, pEntry, pvVisitor))
            return;
    }
}

