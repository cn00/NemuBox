/* $Id: NemuVideoTools.h $ */

/** @file
 * Nemu Video tooling
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___NemuVideoTools_h__
#define ___NemuVideoTools_h__

#include <iprt/cdefs.h>
#include <iprt/assert.h>

typedef struct NEMUVTLIST_ENTRY
{
    struct NEMUVTLIST_ENTRY *pNext;
} NEMUVTLIST_ENTRY, *PNEMUVTLIST_ENTRY;

typedef struct NEMUVTLIST
{
    PNEMUVTLIST_ENTRY pFirst;
    PNEMUVTLIST_ENTRY pLast;
} NEMUVTLIST, *PNEMUVTLIST;

DECLINLINE(bool) nemuVtListIsEmpty(PNEMUVTLIST pList)
{
    return !pList->pFirst;
}

DECLINLINE(void) nemuVtListInit(PNEMUVTLIST pList)
{
    pList->pFirst = pList->pLast = NULL;
}

DECLINLINE(void) nemuVtListPut(PNEMUVTLIST pList, PNEMUVTLIST_ENTRY pFirst, PNEMUVTLIST_ENTRY pLast)
{
    Assert(pFirst);
    Assert(pLast);
    pLast->pNext = NULL;
    if (pList->pLast)
    {
        Assert(pList->pFirst);
        pList->pLast->pNext = pFirst;
        pList->pLast = pLast;
    }
    else
    {
        Assert(!pList->pFirst);
        pList->pFirst = pFirst;
        pList->pLast = pLast;
    }
}

#define nemuVtListPutTail nemuVtListPut

DECLINLINE(void) nemuVtListPutHead(PNEMUVTLIST pList, PNEMUVTLIST_ENTRY pFirst, PNEMUVTLIST_ENTRY pLast)
{
    Assert(pFirst);
    Assert(pLast);
    pLast->pNext = pList->pFirst;
    if (!pList->pLast)
    {
        Assert(!pList->pFirst);
        pList->pLast = pLast;
    }
    else
    {
        Assert(pList->pFirst);
    }
    pList->pFirst = pFirst;
}

DECLINLINE(void) nemuVtListPutEntryHead(PNEMUVTLIST pList, PNEMUVTLIST_ENTRY pEntry)
{
    nemuVtListPutHead(pList, pEntry, pEntry);
}

DECLINLINE(void) nemuVtListPutEntryTail(PNEMUVTLIST pList, PNEMUVTLIST_ENTRY pEntry)
{
    nemuVtListPutTail(pList, pEntry, pEntry);
}

DECLINLINE(void) nemuVtListCat(PNEMUVTLIST pList1, PNEMUVTLIST pList2)
{
    nemuVtListPut(pList1, pList2->pFirst, pList2->pLast);
    pList2->pFirst = pList2->pLast = NULL;
}

DECLINLINE(void) nemuVtListDetach(PNEMUVTLIST pList, PNEMUVTLIST_ENTRY *ppFirst, PNEMUVTLIST_ENTRY *ppLast)
{
    *ppFirst = pList->pFirst;
    if (ppLast)
        *ppLast = pList->pLast;
    pList->pFirst = NULL;
    pList->pLast = NULL;
}

DECLINLINE(void) nemuVtListDetach2List(PNEMUVTLIST pList, PNEMUVTLIST pDstList)
{
    nemuVtListDetach(pList, &pDstList->pFirst, &pDstList->pLast);
}

DECLINLINE(void) nemuVtListDetachEntries(PNEMUVTLIST pList, PNEMUVTLIST_ENTRY pBeforeDetach, PNEMUVTLIST_ENTRY pLast2Detach)
{
    if (pBeforeDetach)
    {
        pBeforeDetach->pNext = pLast2Detach->pNext;
        if (!pBeforeDetach->pNext)
            pList->pLast = pBeforeDetach;
    }
    else
    {
        pList->pFirst = pLast2Detach->pNext;
        if (!pList->pFirst)
            pList->pLast = NULL;
    }
    pLast2Detach->pNext = NULL;
}

DECLINLINE(void) nemuWddmRectUnite(RECT *pR, const RECT *pR2Unite)
{
    pR->left = RT_MIN(pR->left, pR2Unite->left);
    pR->top = RT_MIN(pR->top, pR2Unite->top);
    pR->right = RT_MAX(pR->right, pR2Unite->right);
    pR->bottom = RT_MAX(pR->bottom, pR2Unite->bottom);
}

DECLINLINE(bool) nemuWddmRectIntersection(const RECT *a, const RECT *b, RECT *rect)
{
    Assert(a);
    Assert(b);
    Assert(rect);
    rect->left = RT_MAX(a->left, b->left);
    rect->right = RT_MIN(a->right, b->right);
    rect->top = RT_MAX(a->top, b->top);
    rect->bottom = RT_MIN(a->bottom, b->bottom);
    return (rect->right>rect->left) && (rect->bottom>rect->top);
}

DECLINLINE(bool) nemuWddmRectIsEqual(const RECT *pRect1, const RECT *pRect2)
{
    Assert(pRect1);
    Assert(pRect2);
    if (pRect1->left != pRect2->left)
        return false;
    if (pRect1->top != pRect2->top)
        return false;
    if (pRect1->right != pRect2->right)
        return false;
    if (pRect1->bottom != pRect2->bottom)
        return false;
    return true;
}

DECLINLINE(bool) nemuWddmRectIsCoveres(const RECT *pRect, const RECT *pCovered)
{
    Assert(pRect);
    Assert(pCovered);
    if (pRect->left > pCovered->left)
        return false;
    if (pRect->top > pCovered->top)
        return false;
    if (pRect->right < pCovered->right)
        return false;
    if (pRect->bottom < pCovered->bottom)
        return false;
    return true;
}

DECLINLINE(bool) nemuWddmRectIsEmpty(const RECT * pRect)
{
    return pRect->left == pRect->right-1 && pRect->top == pRect->bottom-1;
}

DECLINLINE(bool) nemuWddmRectIsIntersect(const RECT * pRect1, const RECT * pRect2)
{
    return !((pRect1->left < pRect2->left && pRect1->right <= pRect2->left)
            || (pRect2->left < pRect1->left && pRect2->right <= pRect1->left)
            || (pRect1->top < pRect2->top && pRect1->bottom <= pRect2->top)
            || (pRect2->top < pRect1->top && pRect2->bottom <= pRect1->top));
}

DECLINLINE(void) nemuWddmRectUnited(RECT * pDst, const RECT * pRect1, const RECT * pRect2)
{
    pDst->left = RT_MIN(pRect1->left, pRect2->left);
    pDst->top = RT_MIN(pRect1->top, pRect2->top);
    pDst->right = RT_MAX(pRect1->right, pRect2->right);
    pDst->bottom = RT_MAX(pRect1->bottom, pRect2->bottom);
}

DECLINLINE(void) nemuWddmRectTranslate(RECT * pRect, int x, int y)
{
    pRect->left   += x;
    pRect->top    += y;
    pRect->right  += x;
    pRect->bottom += y;
}

DECLINLINE(void) nemuWddmRectMove(RECT * pRect, int x, int y)
{
    LONG w = pRect->right - pRect->left;
    LONG h = pRect->bottom - pRect->top;
    pRect->left   = x;
    pRect->top    = y;
    pRect->right  = w + x;
    pRect->bottom = h + y;
}

DECLINLINE(void) nemuWddmRectTranslated(RECT *pDst, const RECT * pRect, int x, int y)
{
    *pDst = *pRect;
    nemuWddmRectTranslate(pDst, x, y);
}

DECLINLINE(void) nemuWddmRectMoved(RECT *pDst, const RECT * pRect, int x, int y)
{
    *pDst = *pRect;
    nemuWddmRectMove(pDst, x, y);
}

typedef struct NEMUPOINT3D
{
    UINT x;
    UINT y;
    UINT z;
} NEMUPOINT3D, *PNEMUPOINT3D;

typedef struct NEMUBOX3D
{
    UINT Left;
    UINT Top;
    UINT Right;
    UINT Bottom;
    UINT Front;
    UINT Back;
} NEMUBOX3D, *PNEMUBOX3D;

DECLINLINE(void) nemuWddmBoxTranslate(NEMUBOX3D * pBox, int x, int y, int z)
{
    pBox->Left   += x;
    pBox->Top    += y;
    pBox->Right  += x;
    pBox->Bottom += y;
    pBox->Front  += z;
    pBox->Back   += z;
}

DECLINLINE(void) nemuWddmBoxMove(NEMUBOX3D * pBox, int x, int y, int z)
{
    LONG w = pBox->Right - pBox->Left;
    LONG h = pBox->Bottom - pBox->Top;
    LONG d = pBox->Back - pBox->Front;
    pBox->Left   = x;
    pBox->Top    = y;
    pBox->Right  = w + x;
    pBox->Bottom = h + y;
    pBox->Front  = z;
    pBox->Back   = d + z;
}

#define NEMUWDDM_BOXDIV_U(_v, _d, _nz) do { \
        UINT tmp = (_v) / (_d); \
        if (!tmp && (_v) && (_nz)) \
            (_v) = 1; \
        else \
            (_v) = tmp; \
    } while (0)

DECLINLINE(void) nemuWddmBoxDivide(NEMUBOX3D * pBox, int div, bool fDontReachZero)
{
    NEMUWDDM_BOXDIV_U(pBox->Left, div, fDontReachZero);
    NEMUWDDM_BOXDIV_U(pBox->Top, div, fDontReachZero);
    NEMUWDDM_BOXDIV_U(pBox->Right, div, fDontReachZero);
    NEMUWDDM_BOXDIV_U(pBox->Bottom, div, fDontReachZero);
    NEMUWDDM_BOXDIV_U(pBox->Front, div, fDontReachZero);
    NEMUWDDM_BOXDIV_U(pBox->Back, div, fDontReachZero);
}

DECLINLINE(void) nemuWddmPoint3DDivide(NEMUPOINT3D * pPoint, int div, bool fDontReachZero)
{
    NEMUWDDM_BOXDIV_U(pPoint->x, div, fDontReachZero);
    NEMUWDDM_BOXDIV_U(pPoint->y, div, fDontReachZero);
    NEMUWDDM_BOXDIV_U(pPoint->y, div, fDontReachZero);
}

DECLINLINE(void) nemuWddmBoxTranslated(NEMUBOX3D * pDst, const NEMUBOX3D * pBox, int x, int y, int z)
{
    *pDst = *pBox;
    nemuWddmBoxTranslate(pDst, x, y, z);
}

DECLINLINE(void) nemuWddmBoxMoved(NEMUBOX3D * pDst, const NEMUBOX3D * pBox, int x, int y, int z)
{
    *pDst = *pBox;
    nemuWddmBoxMove(pDst, x, y, z);
}

DECLINLINE(void) nemuWddmBoxDivided(NEMUBOX3D * pDst, const NEMUBOX3D * pBox, int div, bool fDontReachZero)
{
    *pDst = *pBox;
    nemuWddmBoxDivide(pDst, div, fDontReachZero);
}

DECLINLINE(void) nemuWddmPoint3DDivided(NEMUPOINT3D * pDst, const NEMUPOINT3D * pPoint, int div, bool fDontReachZero)
{
    *pDst = *pPoint;
    nemuWddmPoint3DDivide(pDst, div, fDontReachZero);
}

/* the dirty rect info is valid */
#define NEMUWDDM_DIRTYREGION_F_VALID      0x00000001
#define NEMUWDDM_DIRTYREGION_F_RECT_VALID 0x00000002

typedef struct NEMUWDDM_DIRTYREGION
{
    uint32_t fFlags; /* <-- see NEMUWDDM_DIRTYREGION_F_xxx flags above */
    RECT Rect;
} NEMUWDDM_DIRTYREGION, *PNEMUWDDM_DIRTYREGION;

DECLINLINE(void) nemuWddmDirtyRegionAddRect(PNEMUWDDM_DIRTYREGION pInfo, const RECT *pRect)
{
    if (!(pInfo->fFlags & NEMUWDDM_DIRTYREGION_F_VALID))
    {
        pInfo->fFlags = NEMUWDDM_DIRTYREGION_F_VALID;
        if (pRect)
        {
            pInfo->fFlags |= NEMUWDDM_DIRTYREGION_F_RECT_VALID;
            pInfo->Rect = *pRect;
        }
    }
    else if (!!(pInfo->fFlags & NEMUWDDM_DIRTYREGION_F_RECT_VALID))
    {
        if (pRect)
            nemuWddmRectUnite(&pInfo->Rect, pRect);
        else
            pInfo->fFlags &= ~NEMUWDDM_DIRTYREGION_F_RECT_VALID;
    }
}

DECLINLINE(void) nemuWddmDirtyRegionUnite(PNEMUWDDM_DIRTYREGION pInfo, const NEMUWDDM_DIRTYREGION *pInfo2)
{
    if (pInfo2->fFlags & NEMUWDDM_DIRTYREGION_F_VALID)
    {
        if (pInfo2->fFlags & NEMUWDDM_DIRTYREGION_F_RECT_VALID)
            nemuWddmDirtyRegionAddRect(pInfo, &pInfo2->Rect);
        else
            nemuWddmDirtyRegionAddRect(pInfo, NULL);
    }
}

DECLINLINE(void) nemuWddmDirtyRegionClear(PNEMUWDDM_DIRTYREGION pInfo)
{
    pInfo->fFlags = 0;
}

#endif /* #ifndef ___NemuVideoTools_h__ */
