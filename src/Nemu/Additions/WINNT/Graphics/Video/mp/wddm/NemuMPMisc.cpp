/* $Id: NemuMPMisc.cpp $ */

/** @file
 * Nemu WDDM Miniport driver
 */

/*
 * Copyright (C) 2011-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "NemuMPWddm.h"
#include <Nemu/Hardware/NemuVideoVBE.h>
#include <stdio.h>

/* simple handle -> value table API */
NTSTATUS nemuWddmHTableCreate(PNEMUWDDM_HTABLE pTbl, uint32_t cSize)
{
    memset(pTbl, 0, sizeof (*pTbl));
    pTbl->paData = (PVOID*)nemuWddmMemAllocZero(sizeof (pTbl->paData[0]) * cSize);
    if (pTbl->paData)
    {
        pTbl->cSize = cSize;
        return STATUS_SUCCESS;
    }
    return STATUS_NO_MEMORY;
}

VOID nemuWddmHTableDestroy(PNEMUWDDM_HTABLE pTbl)
{
    if (!pTbl->paData)
        return;

    nemuWddmMemFree(pTbl->paData);
}

DECLINLINE(NEMUWDDM_HANDLE) nemuWddmHTableIndex2Handle(uint32_t iIndex)
{
    return iIndex+1;
}

DECLINLINE(uint32_t) nemuWddmHTableHandle2Index(NEMUWDDM_HANDLE hHandle)
{
    return hHandle-1;
}

NTSTATUS nemuWddmHTableRealloc(PNEMUWDDM_HTABLE pTbl, uint32_t cNewSize)
{
    Assert(cNewSize > pTbl->cSize);
    if (cNewSize > pTbl->cSize)
    {
        PVOID *pvNewData = (PVOID*)nemuWddmMemAllocZero(sizeof (pTbl->paData[0]) * cNewSize);
        if (!pvNewData)
        {
            WARN(("nemuWddmMemAllocZero failed for size (%d)", sizeof (pTbl->paData[0]) * cNewSize));
            return STATUS_NO_MEMORY;
        }
        memcpy(pvNewData, pTbl->paData, sizeof (pTbl->paData[0]) * pTbl->cSize);
        nemuWddmMemFree(pTbl->paData);
        pTbl->iNext2Search = pTbl->cSize;
        pTbl->cSize = cNewSize;
        pTbl->paData = pvNewData;
        return STATUS_SUCCESS;
    }
    else if (cNewSize >= pTbl->cData)
    {
        AssertFailed();
        return STATUS_NOT_IMPLEMENTED;
    }
    return STATUS_INVALID_PARAMETER;

}
NEMUWDDM_HANDLE nemuWddmHTablePut(PNEMUWDDM_HTABLE pTbl, PVOID pvData)
{
    if (pTbl->cSize == pTbl->cData)
    {
        NTSTATUS Status = nemuWddmHTableRealloc(pTbl, pTbl->cSize + RT_MAX(10, pTbl->cSize/4));
        Assert(Status == STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
            return NEMUWDDM_HANDLE_INVALID;
    }
    for (UINT i = pTbl->iNext2Search; ; ++i, i %= pTbl->cSize)
    {
        Assert(i < pTbl->cSize);
        if (!pTbl->paData[i])
        {
            pTbl->paData[i] = pvData;
            ++pTbl->cData;
            Assert(pTbl->cData <= pTbl->cSize);
            ++pTbl->iNext2Search;
            pTbl->iNext2Search %= pTbl->cSize;
            return nemuWddmHTableIndex2Handle(i);
        }
    }
    Assert(0);
    return NEMUWDDM_HANDLE_INVALID;
}

PVOID nemuWddmHTableRemove(PNEMUWDDM_HTABLE pTbl, NEMUWDDM_HANDLE hHandle)
{
    uint32_t iIndex = nemuWddmHTableHandle2Index(hHandle);
    Assert(iIndex < pTbl->cSize);
    if (iIndex < pTbl->cSize)
    {
        PVOID pvData = pTbl->paData[iIndex];
        pTbl->paData[iIndex] = NULL;
        --pTbl->cData;
        Assert(pTbl->cData <= pTbl->cSize);
        pTbl->iNext2Search = iIndex;
        return pvData;
    }
    return NULL;
}

PVOID nemuWddmHTableGet(PNEMUWDDM_HTABLE pTbl, NEMUWDDM_HANDLE hHandle)
{
    uint32_t iIndex = nemuWddmHTableHandle2Index(hHandle);
    Assert(iIndex < pTbl->cSize);
    if (iIndex < pTbl->cSize)
        return pTbl->paData[iIndex];
    return NULL;
}

VOID nemuWddmHTableIterInit(PNEMUWDDM_HTABLE pTbl, PNEMUWDDM_HTABLE_ITERATOR pIter)
{
    pIter->pTbl = pTbl;
    pIter->iCur = ~0UL;
    pIter->cLeft = pTbl->cData;
}

BOOL nemuWddmHTableIterHasNext(PNEMUWDDM_HTABLE_ITERATOR pIter)
{
    return pIter->cLeft;
}


PVOID nemuWddmHTableIterNext(PNEMUWDDM_HTABLE_ITERATOR pIter, NEMUWDDM_HANDLE *phHandle)
{
    if (nemuWddmHTableIterHasNext(pIter))
    {
        for (uint32_t i = pIter->iCur+1; i < pIter->pTbl->cSize ; ++i)
        {
            if (pIter->pTbl->paData[i])
            {
                pIter->iCur = i;
                --pIter->cLeft;
                NEMUWDDM_HANDLE hHandle = nemuWddmHTableIndex2Handle(i);
                Assert(hHandle);
                if (phHandle)
                    *phHandle = hHandle;
                return pIter->pTbl->paData[i];
            }
        }
    }

    Assert(!nemuWddmHTableIterHasNext(pIter));
    if (phHandle)
        *phHandle = NEMUWDDM_HANDLE_INVALID;
    return NULL;
}


PVOID nemuWddmHTableIterRemoveCur(PNEMUWDDM_HTABLE_ITERATOR pIter)
{
    NEMUWDDM_HANDLE hHandle = nemuWddmHTableIndex2Handle(pIter->iCur);
    Assert(hHandle);
    if (hHandle)
    {
        PVOID pRet = nemuWddmHTableRemove(pIter->pTbl, hHandle);
        Assert(pRet);
        return pRet;
    }
    return NULL;
}
#ifdef NEMU_WITH_CROGL
PNEMUWDDM_SWAPCHAIN nemuWddmSwapchainCreate(UINT w, UINT h)
{
    PNEMUWDDM_SWAPCHAIN pSwapchain = (PNEMUWDDM_SWAPCHAIN)nemuWddmMemAllocZero(sizeof (NEMUWDDM_SWAPCHAIN));
    Assert(pSwapchain);
    if (pSwapchain)
    {
        InitializeListHead(&pSwapchain->AllocList);
        pSwapchain->enmState = NEMUWDDM_OBJSTATE_TYPE_INITIALIZED;
        pSwapchain->cRefs = 1;
        /* init to some invalid value so that the pos get submitted */
        pSwapchain->Pos.x = pSwapchain->Pos.y = NEMUWDDM_INVALID_COORD;
        pSwapchain->width = w;
        pSwapchain->height = h;
        NemuVrListInit(&pSwapchain->VisibleRegions);
    }
    return pSwapchain;
}

DECLINLINE(BOOLEAN) nemuWddmSwapchainRetainLocked(PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    if (pSwapchain->enmState == NEMUWDDM_OBJSTATE_TYPE_INITIALIZED)
    {
        ASMAtomicIncU32(&pSwapchain->cRefs);
        return TRUE;
    }
    return FALSE;
}

BOOLEAN nemuWddmSwapchainRetain(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    KIRQL OldIrql;
    BOOLEAN bRc;
    KeAcquireSpinLock(&pDevExt->SynchLock, &OldIrql);
    bRc = nemuWddmSwapchainRetainLocked(pSwapchain);
    KeReleaseSpinLock(&pDevExt->SynchLock, OldIrql);
    return bRc;
}

VOID nemuWddmSwapchainRelease(PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    const uint32_t cRefs = ASMAtomicDecU32(&pSwapchain->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    if (!cRefs)
    {
        NemuVrListClear(&pSwapchain->VisibleRegions);
        nemuWddmMemFree(pSwapchain);
    }
}

PNEMUWDDM_SWAPCHAIN nemuWddmSwapchainRetainByAllocData(PNEMUMP_DEVEXT pDevExt, const struct NEMUWDDM_ALLOC_DATA *pAllocData)
{
    KIRQL OldIrql;
    PNEMUWDDM_SWAPCHAIN pSwapchain;
    KeAcquireSpinLock(&pDevExt->SynchLock, &OldIrql);
    pSwapchain = pAllocData->pSwapchain;
    if (pSwapchain && !nemuWddmSwapchainRetainLocked(pSwapchain))
        pSwapchain = NULL;
    KeReleaseSpinLock(&pDevExt->SynchLock, OldIrql);
    return pSwapchain;
}

PNEMUWDDM_SWAPCHAIN nemuWddmSwapchainRetainByAlloc(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOCATION *pAlloc)
{
    return nemuWddmSwapchainRetainByAllocData(pDevExt, &pAlloc->AllocData);
}

VOID nemuWddmSwapchainAllocRemove(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain, PNEMUWDDM_ALLOCATION pAlloc)
{
    KIRQL OldIrql;
    KeAcquireSpinLock(&pDevExt->SynchLock, &OldIrql);
    Assert(pAlloc->AllocData.pSwapchain == pSwapchain);
    pAlloc->AllocData.pSwapchain = NULL;
    RemoveEntryList(&pAlloc->SwapchainEntry);
    KeReleaseSpinLock(&pDevExt->SynchLock, OldIrql);
    nemuWddmSwapchainRelease(pSwapchain);
}

BOOLEAN nemuWddmSwapchainAllocAdd(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain, PNEMUWDDM_ALLOCATION pAlloc)
{
    KIRQL OldIrql;
    BOOLEAN bRc;
    Assert(!pAlloc->AllocData.pSwapchain);
    KeAcquireSpinLock(&pDevExt->SynchLock, &OldIrql);
    bRc = nemuWddmSwapchainRetainLocked(pSwapchain);
    if (bRc)
    {
        if (pAlloc->AllocData.pSwapchain)
        {
            RemoveEntryList(&pAlloc->SwapchainEntry);
        }
        InsertTailList(&pSwapchain->AllocList, &pAlloc->SwapchainEntry);
        pAlloc->AllocData.pSwapchain = pSwapchain;
    }
    KeReleaseSpinLock(&pDevExt->SynchLock, OldIrql);
    return bRc;
}

#define NEMUSCENTRY_2_ALLOC(_pE) ((PNEMUWDDM_ALLOCATION)((uint8_t*)(_pE) - RT_OFFSETOF(NEMUWDDM_ALLOCATION, SwapchainEntry)))

static VOID nemuWddmSwapchainAllocRemoveAllInternal(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain, BOOLEAN bOnDestroy)
{
    KIRQL OldIrql;
    UINT cRemoved = 0;
    KeAcquireSpinLock(&pDevExt->SynchLock, &OldIrql);
    PLIST_ENTRY pEntry = pSwapchain->AllocList.Flink;
    do
    {
        if (pEntry != &pSwapchain->AllocList)
        {
            PNEMUWDDM_ALLOCATION pAlloc = NEMUSCENTRY_2_ALLOC(pEntry);
            pEntry = pEntry->Flink;
            Assert(pAlloc->AllocData.pSwapchain == pSwapchain);
            pAlloc->AllocData.pSwapchain = NULL;
            RemoveEntryList(&pAlloc->SwapchainEntry);
            ++cRemoved;
        }
        else
            break;
    } while (1);

    if (bOnDestroy)
        pSwapchain->enmState = NEMUWDDM_OBJSTATE_TYPE_TERMINATED;
    KeReleaseSpinLock(&pDevExt->SynchLock, OldIrql);

    for (UINT i = 0; i < cRemoved; ++i)
        nemuWddmSwapchainRelease(pSwapchain);
}

VOID nemuWddmSwapchainAllocRemoveAll(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    nemuWddmSwapchainAllocRemoveAllInternal(pDevExt, pSwapchain, FALSE);
}

VOID nemuWddmSwapchainDestroy(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    nemuWddmSwapchainAllocRemoveAllInternal(pDevExt, pSwapchain, TRUE);

    nemuWddmSwapchainRelease(pSwapchain);
}

static BOOLEAN nemuWddmSwapchainCtxAddLocked(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext, PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    if (nemuWddmSwapchainRetain(pDevExt, pSwapchain))
    {
        Assert(!pSwapchain->hSwapchainKm);
        Assert(!pSwapchain->pContext);
        pSwapchain->pContext = pContext;
        pSwapchain->hSwapchainKm = nemuWddmHTablePut(&pContext->Swapchains, pSwapchain);
        InsertHeadList(&pDevExt->SwapchainList3D, &pSwapchain->DevExtListEntry);
        return TRUE;
    }
    return FALSE;
}

static VOID nemuWddmSwapchainCtxRemoveLocked(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext, PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    Assert(pSwapchain->hSwapchainKm);
    Assert(pSwapchain->pContext);
    void * pTst = nemuWddmHTableRemove(&pContext->Swapchains, pSwapchain->hSwapchainKm);
    Assert(pTst == pSwapchain);
    RemoveEntryList(&pSwapchain->DevExtListEntry);
    pSwapchain->hSwapchainKm = NULL;
    NemuVrListClear(&pSwapchain->VisibleRegions);
    nemuWddmSwapchainRelease(pSwapchain);
}

/* adds the given swapchain to the context's swapchain list
 * @return true on success */
BOOLEAN nemuWddmSwapchainCtxAdd(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext, PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    BOOLEAN bRc;
    NEMUWDDM_CTXLOCK_DATA
    NEMUWDDM_CTXLOCK_LOCK(pDevExt);
    bRc = nemuWddmSwapchainCtxAddLocked(pDevExt, pContext, pSwapchain);
    NEMUWDDM_CTXLOCK_UNLOCK(pDevExt);
    return bRc;
}

/* removes the given swapchain from the context's swapchain list
 * */
VOID nemuWddmSwapchainCtxRemove(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext, PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    NEMUWDDM_CTXLOCK_DATA
    NEMUWDDM_CTXLOCK_LOCK(pDevExt);
    nemuWddmSwapchainCtxRemoveLocked(pDevExt, pContext, pSwapchain);
    NEMUWDDM_CTXLOCK_UNLOCK(pDevExt);
}

/* destroys all swapchains for the given context
 * */
VOID nemuWddmSwapchainCtxDestroyAll(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext)
{
    NEMUWDDM_HTABLE_ITERATOR Iter;
    NEMUWDDM_CTXLOCK_DATA
    do
    {
        NEMUWDDM_CTXLOCK_LOCK(pDevExt);
        nemuWddmHTableIterInit(&pContext->Swapchains, &Iter);
        PNEMUWDDM_SWAPCHAIN pSwapchain = (PNEMUWDDM_SWAPCHAIN)nemuWddmHTableIterNext(&Iter, NULL);
        if (!pSwapchain)
            break;

        /* yes, we can call remove locked even when using iterator */
        nemuWddmSwapchainCtxRemoveLocked(pDevExt, pContext, pSwapchain);

        NEMUWDDM_CTXLOCK_UNLOCK(pDevExt);
        /* we must not do nemuWddmSwapchainDestroy inside a context mutex */
        nemuWddmSwapchainDestroy(pDevExt, pSwapchain);
        /* start from the very beginning, we will quit the loop when no swapchains left */
    } while (1);

    /* no swapchains left, we exiteed the while loop via the "break", and we still owning the mutex */
    NEMUWDDM_CTXLOCK_UNLOCK(pDevExt);
}

/* process the swapchain info passed from user-mode display driver & synchronizes the driver state with it */
NTSTATUS nemuWddmSwapchainCtxEscape(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext, PNEMUDISPIFESCAPE_SWAPCHAININFO pSwapchainInfo, UINT cbSize)
{
    if (cbSize < RT_OFFSETOF(NEMUDISPIFESCAPE_SWAPCHAININFO, SwapchainInfo.ahAllocs[0]))
    {
        WARN(("invalid cbSize1 %d", cbSize));
        return STATUS_INVALID_PARAMETER;
    }

    if (cbSize < RT_OFFSETOF(NEMUDISPIFESCAPE_SWAPCHAININFO, SwapchainInfo.ahAllocs[pSwapchainInfo->SwapchainInfo.cAllocs]))
    {
        WARN(("invalid cbSize2 %d", cbSize));
        return STATUS_INVALID_PARAMETER;
    }

    if (!pSwapchainInfo->SwapchainInfo.winHostID)
    {
        WARN(("Zero winHostID specified!"));
        return STATUS_INVALID_PARAMETER;
    }

    if (!pContext)
    {
        WARN(("nemuWddmSwapchainCtxEscape: no context specified"));
        return STATUS_INVALID_PARAMETER;
    }

    PNEMUWDDM_SWAPCHAIN pSwapchain = NULL;
    PNEMUWDDM_ALLOCATION *apAlloc = NULL;
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
    NTSTATUS Status = STATUS_SUCCESS;
    NEMUWDDM_CTXLOCK_DATA

    do {
        if (pSwapchainInfo->SwapchainInfo.cAllocs)
        {
            /* ensure we do not overflow the 32bit buffer size value */
            if (NEMUWDDM_ARRAY_MAXELEMENTSU32(NEMUWDDM_ALLOCATION) < pSwapchainInfo->SwapchainInfo.cAllocs)
            {
                WARN(("number of allocations passed in too big (%d), max is (%d)", pSwapchainInfo->SwapchainInfo.cAllocs, NEMUWDDM_ARRAY_MAXELEMENTSU32(NEMUWDDM_ALLOCATION)));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            apAlloc = (PNEMUWDDM_ALLOCATION *)nemuWddmMemAlloc(sizeof (PNEMUWDDM_ALLOCATION) * pSwapchainInfo->SwapchainInfo.cAllocs);
            Assert(apAlloc);
            if (!apAlloc)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }
            for (UINT i = 0; i < pSwapchainInfo->SwapchainInfo.cAllocs; ++i)
            {
                DXGKARGCB_GETHANDLEDATA GhData;
                GhData.hObject = pSwapchainInfo->SwapchainInfo.ahAllocs[i];
                GhData.Type = DXGK_HANDLE_ALLOCATION;
                GhData.Flags.Value = 0;
                PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pDevExt->u.primary.DxgkInterface.DxgkCbGetHandleData(&GhData);
                Assert(pAlloc);
                if (!pAlloc)
                {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
                apAlloc[i] = pAlloc;
            }

            if (!NT_SUCCESS(Status))
                break;
        }

        if (pSwapchainInfo->SwapchainInfo.hSwapchainKm)
        {
            NEMUWDDM_CTXLOCK_LOCK(pDevExt);
            pSwapchain = (PNEMUWDDM_SWAPCHAIN)nemuWddmHTableGet(&pContext->Swapchains, (NEMUWDDM_HANDLE)pSwapchainInfo->SwapchainInfo.hSwapchainKm);
            Assert(pSwapchain);
            if (!pSwapchain)
            {
                NEMUWDDM_CTXLOCK_UNLOCK(pDevExt);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Assert(pSwapchain->hSwapchainKm == pSwapchainInfo->SwapchainInfo.hSwapchainKm);
            Assert(pSwapchain->pContext == pContext);
            if (pSwapchain->pContext != pContext)
            {
                NEMUWDDM_CTXLOCK_UNLOCK(pDevExt);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }
        else if (pSwapchainInfo->SwapchainInfo.cAllocs)
        {
            pSwapchain = nemuWddmSwapchainCreate(apAlloc[0]->AllocData.SurfDesc.width, apAlloc[0]->AllocData.SurfDesc.height);
            if (!pSwapchain)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }

            NEMUWDDM_CTXLOCK_LOCK(pDevExt);
            BOOLEAN bRc = nemuWddmSwapchainCtxAddLocked(pDevExt, pContext, pSwapchain);
            Assert(bRc);
        }
        else
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        /* do not zero up the view rect since it may still be valid */
//        memset(&pSwapchain->ViewRect, 0, sizeof (pSwapchain->ViewRect));
        /* @todo: do we really need to zero this up here ? */
        NemuVrListClear(&pSwapchain->VisibleRegions);

        nemuWddmSwapchainAllocRemoveAll(pDevExt, pSwapchain);

        if (pSwapchainInfo->SwapchainInfo.cAllocs)
        {
            for (UINT i = 0; i < pSwapchainInfo->SwapchainInfo.cAllocs; ++i)
            {
                nemuWddmSwapchainAllocAdd(pDevExt, pSwapchain, apAlloc[i]);
            }
            pSwapchain->hSwapchainUm = pSwapchainInfo->SwapchainInfo.hSwapchainUm;
            if (pSwapchain->winHostID != pSwapchainInfo->SwapchainInfo.winHostID)
            {
                pSwapchain->fExposed = FALSE;
                pSwapchain->winHostID = pSwapchainInfo->SwapchainInfo.winHostID;
            }
        }
        else
        {
            nemuWddmSwapchainCtxRemoveLocked(pDevExt, pContext, pSwapchain);
        }

        NEMUWDDM_CTXLOCK_UNLOCK(pDevExt);

        if (pSwapchainInfo->SwapchainInfo.cAllocs)
        {
            Assert(pSwapchain->pContext);
            Assert(pSwapchain->hSwapchainKm);
            pSwapchainInfo->SwapchainInfo.hSwapchainKm = pSwapchain->hSwapchainKm;
        }
        else
        {
            nemuWddmSwapchainDestroy(pDevExt, pSwapchain);
            pSwapchainInfo->SwapchainInfo.hSwapchainKm = 0;
        }

        Assert(Status == STATUS_SUCCESS);
    } while (0);

    /* cleanup */
    if (apAlloc)
        nemuWddmMemFree(apAlloc);

    return Status;
}

NTSTATUS nemuWddmSwapchainCtxInit(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext)
{
    NTSTATUS Status = nemuWddmHTableCreate(&pContext->Swapchains, 4);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuWddmHTableCreate failes, Status (x%x)", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

VOID nemuWddmSwapchainCtxTerm(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext)
{
    nemuWddmSwapchainCtxDestroyAll(pDevExt, pContext);
    nemuWddmHTableDestroy(&pContext->Swapchains);
}
#endif
NTSTATUS nemuWddmRegQueryDrvKeyName(PNEMUMP_DEVEXT pDevExt, ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult)
{
    WCHAR fallBackBuf[2];
    PWCHAR pSuffix;
    bool bFallback = false;

    if (cbBuf > sizeof(NEMUWDDM_REG_DRVKEY_PREFIX))
    {
        memcpy(pBuf, NEMUWDDM_REG_DRVKEY_PREFIX, sizeof (NEMUWDDM_REG_DRVKEY_PREFIX));
        pSuffix = pBuf + (sizeof (NEMUWDDM_REG_DRVKEY_PREFIX)-2)/2;
        cbBuf -= sizeof (NEMUWDDM_REG_DRVKEY_PREFIX)-2;
    }
    else
    {
        pSuffix = fallBackBuf;
        cbBuf = sizeof (fallBackBuf);
        bFallback = true;
    }

    NTSTATUS Status = IoGetDeviceProperty (pDevExt->pPDO,
                                  DevicePropertyDriverKeyName,
                                  cbBuf,
                                  pSuffix,
                                  &cbBuf);
    if (Status == STATUS_SUCCESS && bFallback)
        Status = STATUS_BUFFER_TOO_SMALL;
    if (Status == STATUS_BUFFER_TOO_SMALL)
        *pcbResult = cbBuf + sizeof (NEMUWDDM_REG_DRVKEY_PREFIX)-2;

    return Status;
}

NTSTATUS nemuWddmRegQueryDisplaySettingsKeyName(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PWCHAR pSuffix;
    bool bFallback = false;
    const WCHAR* pKeyPrefix;
    UINT cbKeyPrefix;
    UNICODE_STRING* pVGuid = nemuWddmVGuidGet(pDevExt);
    Assert(pVGuid);
    if (!pVGuid)
        return STATUS_UNSUCCESSFUL;

    nemuWinVersion_t ver = NemuQueryWinVersion();
    if (ver == WINVERSION_VISTA)
    {
        pKeyPrefix = NEMUWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_VISTA;
        cbKeyPrefix = sizeof (NEMUWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_VISTA);
    }
    else
    {
        Assert(ver > WINVERSION_VISTA);
        pKeyPrefix = NEMUWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_WIN7;
        cbKeyPrefix = sizeof (NEMUWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_WIN7);
    }

    ULONG cbResult = cbKeyPrefix + pVGuid->Length + 2 + 8; // L"\\" + "XXXX"
    if (cbBuf >= cbResult)
    {
        wcscpy(pBuf, pKeyPrefix);
        pSuffix = pBuf + (cbKeyPrefix-2)/2;
        memcpy(pSuffix, pVGuid->Buffer, pVGuid->Length);
        pSuffix += pVGuid->Length/2;
        pSuffix[0] = L'\\';
        pSuffix += 1;
        swprintf(pSuffix, L"%04d", VidPnSourceId);
    }
    else
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }

    *pcbResult = cbResult;

    return Status;
}

NTSTATUS nemuWddmRegQueryVideoGuidString(PNEMUMP_DEVEXT pDevExt, ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult)
{
    BOOLEAN fNewMethodSucceeded = FALSE;
    HANDLE hKey = NULL;
    NTSTATUS Status = IoOpenDeviceRegistryKey(pDevExt->pPDO, PLUGPLAY_REGKEY_DEVICE, GENERIC_READ, &hKey);
    if (NT_SUCCESS(Status))
    {
        struct
        {
            KEY_VALUE_PARTIAL_INFORMATION Info;
            UCHAR Buf[1024]; /* should be enough */
        } KeyData;
        ULONG cbResult;
        UNICODE_STRING RtlStr;
        RtlInitUnicodeString(&RtlStr, L"VideoID");
        Status = ZwQueryValueKey(hKey,
                    &RtlStr,
                    KeyValuePartialInformation,
                    &KeyData.Info,
                    sizeof(KeyData),
                    &cbResult);
        if (NT_SUCCESS(Status))
        {
            if (KeyData.Info.Type == REG_SZ)
            {
                fNewMethodSucceeded = TRUE;
                *pcbResult = KeyData.Info.DataLength + 2;
                if (cbBuf >= KeyData.Info.DataLength)
                {
                    memcpy(pBuf, KeyData.Info.Data, KeyData.Info.DataLength + 2);
                    Status = STATUS_SUCCESS;
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
            }
        }
        else
        {
            WARN(("ZwQueryValueKey failed, Status 0x%x", Status));
        }

        NTSTATUS tmpStatus = ZwClose(hKey);
        Assert(tmpStatus == STATUS_SUCCESS);
    }
    else
    {
        WARN(("IoOpenDeviceRegistryKey failed Status 0x%x", Status));
    }

    if (fNewMethodSucceeded)
        return Status;
    else
        WARN(("failed to acquire the VideoID, falling back to the old impl"));

    Status = nemuWddmRegOpenKey(&hKey, NEMUWDDM_REG_DISPLAYSETTINGSVIDEOKEY, GENERIC_READ);
    //Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        struct
        {
            KEY_BASIC_INFORMATION Name;
            WCHAR Buf[256];
        } Buf;
        WCHAR KeyBuf[sizeof (NEMUWDDM_REG_DISPLAYSETTINGSVIDEOKEY)/2 + 256 + 64];
        wcscpy(KeyBuf, NEMUWDDM_REG_DISPLAYSETTINGSVIDEOKEY);
        ULONG ResultLength;
        BOOL bFound = FALSE;
        for (ULONG i = 0; !bFound; ++i)
        {
            RtlZeroMemory(&Buf, sizeof (Buf));
            Status = ZwEnumerateKey(hKey, i, KeyBasicInformation, &Buf, sizeof (Buf), &ResultLength);
            Assert(Status == STATUS_SUCCESS);
            /* we should not encounter STATUS_NO_MORE_ENTRIES here since this would mean we did not find our entry */
            if (Status != STATUS_SUCCESS)
                break;

            HANDLE hSubKey;
            PWCHAR pSubBuf = KeyBuf + (sizeof (NEMUWDDM_REG_DISPLAYSETTINGSVIDEOKEY) - 2)/2;
            memcpy(pSubBuf, Buf.Name.Name, Buf.Name.NameLength);
            pSubBuf += Buf.Name.NameLength/2;
            memcpy(pSubBuf, NEMUWDDM_REG_DISPLAYSETTINGSVIDEOKEY_SUBKEY, sizeof (NEMUWDDM_REG_DISPLAYSETTINGSVIDEOKEY_SUBKEY));
            Status = nemuWddmRegOpenKey(&hSubKey, KeyBuf, GENERIC_READ);
            //Assert(Status == STATUS_SUCCESS);
            if (Status == STATUS_SUCCESS)
            {
                struct
                {
                    KEY_VALUE_PARTIAL_INFORMATION Info;
                    UCHAR Buf[sizeof (NEMU_WDDM_DRIVERNAME)]; /* should be enough */
                } KeyData;
                ULONG cbResult;
                UNICODE_STRING RtlStr;
                RtlInitUnicodeString(&RtlStr, L"Service");
                Status = ZwQueryValueKey(hSubKey,
                            &RtlStr,
                            KeyValuePartialInformation,
                            &KeyData.Info,
                            sizeof(KeyData),
                            &cbResult);
                Assert(Status == STATUS_SUCCESS || STATUS_BUFFER_TOO_SMALL || STATUS_BUFFER_OVERFLOW);
                if (Status == STATUS_SUCCESS)
                {
                    if (KeyData.Info.Type == REG_SZ)
                    {
                        if (KeyData.Info.DataLength == sizeof (NEMU_WDDM_DRIVERNAME))
                        {
                            if (!wcscmp(NEMU_WDDM_DRIVERNAME, (PWCHAR)KeyData.Info.Data))
                            {
                                bFound = TRUE;
                                *pcbResult = Buf.Name.NameLength + 2;
                                if (cbBuf >= Buf.Name.NameLength + 2)
                                {
                                    memcpy(pBuf, Buf.Name.Name, Buf.Name.NameLength + 2);
                                }
                                else
                                {
                                    Status = STATUS_BUFFER_TOO_SMALL;
                                }
                            }
                        }
                    }
                }

                NTSTATUS tmpStatus = ZwClose(hSubKey);
                Assert(tmpStatus == STATUS_SUCCESS);
            }
            else
                break;
        }
        NTSTATUS tmpStatus = ZwClose(hKey);
        Assert(tmpStatus == STATUS_SUCCESS);
    }

    return Status;
}

NTSTATUS nemuWddmRegOpenKeyEx(OUT PHANDLE phKey, IN HANDLE hRootKey, IN PWCHAR pName, IN ACCESS_MASK fAccess)
{
    OBJECT_ATTRIBUTES ObjAttr;
    UNICODE_STRING RtlStr;

    RtlInitUnicodeString(&RtlStr, pName);
    InitializeObjectAttributes(&ObjAttr, &RtlStr, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, hRootKey, NULL);

    return ZwOpenKey(phKey, fAccess, &ObjAttr);
}

NTSTATUS nemuWddmRegOpenKey(OUT PHANDLE phKey, IN PWCHAR pName, IN ACCESS_MASK fAccess)
{
    return nemuWddmRegOpenKeyEx(phKey, NULL, pName, fAccess);
}

NTSTATUS nemuWddmRegOpenDisplaySettingsKey(IN PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, OUT PHANDLE phKey)
{
    WCHAR Buf[512];
    ULONG cbBuf = sizeof(Buf);
    NTSTATUS Status = nemuWddmRegQueryDisplaySettingsKeyName(pDevExt, VidPnSourceId, cbBuf, Buf, &cbBuf);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Status = nemuWddmRegOpenKey(phKey, Buf, GENERIC_READ);
        Assert(Status == STATUS_SUCCESS);
        if(Status == STATUS_SUCCESS)
            return STATUS_SUCCESS;
    }

    /* fall-back to make the subsequent NemuVideoCmnRegXxx calls treat the fail accordingly
     * basically needed to make as less modifications to the current XPDM code as possible */
    *phKey = NULL;

    return Status;
}

NTSTATUS nemuWddmRegDisplaySettingsQueryRelX(HANDLE hKey, int * pResult)
{
    DWORD dwVal;
    NTSTATUS Status = nemuWddmRegQueryValueDword(hKey, NEMUWDDM_REG_DISPLAYSETTINGS_ATTACH_RELX, &dwVal);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        *pResult = (int)dwVal;
    }

    return Status;
}

NTSTATUS nemuWddmRegDisplaySettingsQueryRelY(HANDLE hKey, int * pResult)
{
    DWORD dwVal;
    NTSTATUS Status = nemuWddmRegQueryValueDword(hKey, NEMUWDDM_REG_DISPLAYSETTINGS_ATTACH_RELY, &dwVal);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        *pResult = (int)dwVal;
    }

    return Status;
}

NTSTATUS nemuWddmDisplaySettingsQueryPos(IN PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, POINT * pPos)
{
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
    HANDLE hKey;
    NTSTATUS Status = nemuWddmRegOpenDisplaySettingsKey(pDevExt, VidPnSourceId, &hKey);
    //Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        int x, y;
        Status = nemuWddmRegDisplaySettingsQueryRelX(hKey, &x);
        Assert(Status == STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
        {
            Status = nemuWddmRegDisplaySettingsQueryRelY(hKey, &y);
            Assert(Status == STATUS_SUCCESS);
            if (Status == STATUS_SUCCESS)
            {
                pPos->x = x;
                pPos->y = y;
            }
        }
        NTSTATUS tmpStatus = ZwClose(hKey);
        Assert(tmpStatus == STATUS_SUCCESS);
    }

    return Status;
}

void nemuWddmDisplaySettingsCheckPos(IN PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    POINT Pos = {0};
    NTSTATUS Status = nemuWddmDisplaySettingsQueryPos(pDevExt, VidPnSourceId, &Pos);
    if (!NT_SUCCESS(Status))
    {
        Log(("nemuWddmDisplaySettingsQueryPos failed %#x", Status));
        return;
    }

    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];

    if (!memcmp(&pSource->VScreenPos, &Pos, sizeof (Pos)))
        return;

    pSource->VScreenPos = Pos;
    pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;

    nemuWddmGhDisplayCheckSetInfoFromSource(pDevExt, pSource);
}

NTSTATUS nemuWddmRegDrvFlagsSet(PNEMUMP_DEVEXT pDevExt, DWORD fVal)
{
    HANDLE hKey = NULL;
    NTSTATUS Status = IoOpenDeviceRegistryKey(pDevExt->pPDO, PLUGPLAY_REGKEY_DRIVER, GENERIC_WRITE, &hKey);
    if (!NT_SUCCESS(Status))
    {
        WARN(("IoOpenDeviceRegistryKey failed, Status = 0x%x", Status));
        return Status;
    }

    Status = nemuWddmRegSetValueDword(hKey, NEMUWDDM_REG_DRV_FLAGS_NAME, fVal);
    if (!NT_SUCCESS(Status))
        WARN(("nemuWddmRegSetValueDword failed, Status = 0x%x", Status));

    NTSTATUS tmpStatus = ZwClose(hKey);
    Assert(tmpStatus == STATUS_SUCCESS);

    return Status;
}

DWORD nemuWddmRegDrvFlagsGet(PNEMUMP_DEVEXT pDevExt, DWORD fDefault)
{
    HANDLE hKey = NULL;
    NTSTATUS Status = IoOpenDeviceRegistryKey(pDevExt->pPDO, PLUGPLAY_REGKEY_DRIVER, GENERIC_READ, &hKey);
    if (!NT_SUCCESS(Status))
    {
        WARN(("IoOpenDeviceRegistryKey failed, Status = 0x%x", Status));
        return fDefault;
    }

    DWORD dwVal = 0;
    Status = nemuWddmRegQueryValueDword(hKey, NEMUWDDM_REG_DRV_FLAGS_NAME, &dwVal);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuWddmRegQueryValueDword failed, Status = 0x%x", Status));
        dwVal = fDefault;
    }

    NTSTATUS tmpStatus = ZwClose(hKey);
    Assert(tmpStatus == STATUS_SUCCESS);

    return dwVal;
}

NTSTATUS nemuWddmRegQueryValueDword(IN HANDLE hKey, IN PWCHAR pName, OUT PDWORD pDword)
{
    struct
    {
        KEY_VALUE_PARTIAL_INFORMATION Info;
        UCHAR Buf[32]; /* should be enough */
    } Buf;
    ULONG cbBuf;
    UNICODE_STRING RtlStr;
    RtlInitUnicodeString(&RtlStr, pName);
    NTSTATUS Status = ZwQueryValueKey(hKey,
                &RtlStr,
                KeyValuePartialInformation,
                &Buf.Info,
                sizeof(Buf),
                &cbBuf);
    if (Status == STATUS_SUCCESS)
    {
        if (Buf.Info.Type == REG_DWORD)
        {
            Assert(Buf.Info.DataLength == 4);
            *pDword = *((PULONG)Buf.Info.Data);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INVALID_PARAMETER;
}

NTSTATUS nemuWddmRegSetValueDword(IN HANDLE hKey, IN PWCHAR pName, IN DWORD val)
{
    UNICODE_STRING RtlStr;
    RtlInitUnicodeString(&RtlStr, pName);
    return ZwSetValueKey(hKey, &RtlStr,
            NULL, /* IN ULONG  TitleIndex  OPTIONAL, reserved */
            REG_DWORD,
            &val,
            sizeof(val));
}

UNICODE_STRING* nemuWddmVGuidGet(PNEMUMP_DEVEXT pDevExt)
{
    if (pDevExt->VideoGuid.Buffer)
        return &pDevExt->VideoGuid;

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
    WCHAR VideoGuidBuf[512];
    ULONG cbVideoGuidBuf = sizeof (VideoGuidBuf);
    NTSTATUS Status = nemuWddmRegQueryVideoGuidString(pDevExt ,cbVideoGuidBuf, VideoGuidBuf, &cbVideoGuidBuf);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        PWCHAR pBuf = (PWCHAR)nemuWddmMemAllocZero(cbVideoGuidBuf);
        Assert(pBuf);
        if (pBuf)
        {
            memcpy(pBuf, VideoGuidBuf, cbVideoGuidBuf);
            RtlInitUnicodeString(&pDevExt->VideoGuid, pBuf);
            return &pDevExt->VideoGuid;
        }
    }

    return NULL;
}

VOID nemuWddmVGuidFree(PNEMUMP_DEVEXT pDevExt)
{
    if (pDevExt->VideoGuid.Buffer)
    {
        nemuWddmMemFree(pDevExt->VideoGuid.Buffer);
        pDevExt->VideoGuid.Buffer = NULL;
    }
}

/* mm */

NTSTATUS nemuMmInit(PNEMUWDDM_MM pMm, UINT cPages)
{
    UINT cbBuffer = NEMUWDDM_ROUNDBOUND(cPages, 8) >> 3;
    cbBuffer = NEMUWDDM_ROUNDBOUND(cbBuffer, 4);
    PULONG pBuf = (PULONG)nemuWddmMemAllocZero(cbBuffer);
    if (!pBuf)
    {
        Assert(0);
        return STATUS_NO_MEMORY;
    }
    RtlInitializeBitMap(&pMm->BitMap, pBuf, cPages);
    pMm->cPages = cPages;
    pMm->cAllocs = 0;
    pMm->pBuffer = pBuf;
    return STATUS_SUCCESS;
}

ULONG nemuMmAlloc(PNEMUWDDM_MM pMm, UINT cPages)
{
    ULONG iPage = RtlFindClearBitsAndSet(&pMm->BitMap, cPages, 0);
    if (iPage == 0xFFFFFFFF)
    {
        Assert(0);
        return NEMUWDDM_MM_VOID;
    }

    ++pMm->cAllocs;
    return iPage;
}

VOID nemuMmFree(PNEMUWDDM_MM pMm, UINT iPage, UINT cPages)
{
    Assert(RtlAreBitsSet(&pMm->BitMap, iPage, cPages));
    RtlClearBits(&pMm->BitMap, iPage, cPages);
    --pMm->cAllocs;
    Assert(pMm->cAllocs < UINT32_MAX);
}

NTSTATUS nemuMmTerm(PNEMUWDDM_MM pMm)
{
    Assert(!pMm->cAllocs);
    nemuWddmMemFree(pMm->pBuffer);
    pMm->pBuffer = NULL;
    return STATUS_SUCCESS;
}



typedef struct NEMUVIDEOCM_ALLOC
{
    NEMUWDDM_HANDLE hGlobalHandle;
    uint32_t offData;
    uint32_t cbData;
} NEMUVIDEOCM_ALLOC, *PNEMUVIDEOCM_ALLOC;

typedef struct NEMUVIDEOCM_ALLOC_REF
{
    PNEMUVIDEOCM_ALLOC_CONTEXT pContext;
    NEMUWDDM_HANDLE hSessionHandle;
    PNEMUVIDEOCM_ALLOC pAlloc;
    PKEVENT pSynchEvent;
    NEMUUHGSMI_BUFFER_TYPE_FLAGS fUhgsmiType;
    volatile uint32_t cRefs;
    PVOID pvUm;
    MDL Mdl;
} NEMUVIDEOCM_ALLOC_REF, *PNEMUVIDEOCM_ALLOC_REF;


NTSTATUS nemuVideoCmAllocAlloc(PNEMUVIDEOCM_ALLOC_MGR pMgr, PNEMUVIDEOCM_ALLOC pAlloc)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    UINT cbSize = pAlloc->cbData;
    UINT cPages = BYTES_TO_PAGES(cbSize);
    ExAcquireFastMutex(&pMgr->Mutex);
    UINT iPage = nemuMmAlloc(&pMgr->Mm, cPages);
    if (iPage != NEMUWDDM_MM_VOID)
    {
        uint32_t offData = pMgr->offData + (iPage << PAGE_SHIFT);
        Assert(offData + cbSize <= pMgr->offData + pMgr->cbData);
        pAlloc->offData = offData;
        pAlloc->hGlobalHandle = nemuWddmHTablePut(&pMgr->AllocTable, pAlloc);
        ExReleaseFastMutex(&pMgr->Mutex);
        if (NEMUWDDM_HANDLE_INVALID != pAlloc->hGlobalHandle)
            return STATUS_SUCCESS;

        Assert(0);
        Status = STATUS_NO_MEMORY;
        nemuMmFree(&pMgr->Mm, iPage, cPages);
    }
    else
    {
        Assert(0);
        ExReleaseFastMutex(&pMgr->Mutex);
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    return Status;
}

VOID nemuVideoCmAllocDealloc(PNEMUVIDEOCM_ALLOC_MGR pMgr, PNEMUVIDEOCM_ALLOC pAlloc)
{
    UINT cbSize = pAlloc->cbData;
    UINT cPages = BYTES_TO_PAGES(cbSize);
    UINT iPage = BYTES_TO_PAGES(pAlloc->offData - pMgr->offData);
    ExAcquireFastMutex(&pMgr->Mutex);
    nemuWddmHTableRemove(&pMgr->AllocTable, pAlloc->hGlobalHandle);
    nemuMmFree(&pMgr->Mm, iPage, cPages);
    ExReleaseFastMutex(&pMgr->Mutex);
}


NTSTATUS nemuVideoAMgrAllocCreate(PNEMUVIDEOCM_ALLOC_MGR pMgr, UINT cbSize, PNEMUVIDEOCM_ALLOC *ppAlloc)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUVIDEOCM_ALLOC pAlloc = (PNEMUVIDEOCM_ALLOC)nemuWddmMemAllocZero(sizeof (*pAlloc));
    if (pAlloc)
    {
        pAlloc->cbData = cbSize;
        Status = nemuVideoCmAllocAlloc(pMgr, pAlloc);
        if (Status == STATUS_SUCCESS)
        {
            *ppAlloc = pAlloc;
            return STATUS_SUCCESS;
        }

        Assert(0);
        nemuWddmMemFree(pAlloc);
    }
    else
    {
        Assert(0);
        Status = STATUS_NO_MEMORY;
    }

    return Status;
}

VOID nemuVideoAMgrAllocDestroy(PNEMUVIDEOCM_ALLOC_MGR pMgr, PNEMUVIDEOCM_ALLOC pAlloc)
{
    nemuVideoCmAllocDealloc(pMgr, pAlloc);
    nemuWddmMemFree(pAlloc);
}

NTSTATUS nemuVideoAMgrCtxAllocMap(PNEMUVIDEOCM_ALLOC_CONTEXT pContext, PNEMUVIDEOCM_ALLOC pAlloc, PNEMUVIDEOCM_UM_ALLOC pUmAlloc)
{
    PNEMUVIDEOCM_ALLOC_MGR pMgr = pContext->pMgr;
    NTSTATUS Status = STATUS_SUCCESS;
    PKEVENT pSynchEvent = NULL;

    if (pUmAlloc->hSynch)
    {
        Status = ObReferenceObjectByHandle((HANDLE)pUmAlloc->hSynch, EVENT_MODIFY_STATE, *ExEventObjectType, UserMode,
                (PVOID*)&pSynchEvent,
                NULL);
        Assert(Status == STATUS_SUCCESS);
        Assert(pSynchEvent);
    }

    if (Status == STATUS_SUCCESS)
    {
        PVOID BaseVa = pMgr->pvData + pAlloc->offData - pMgr->offData;
        SIZE_T cbLength = pAlloc->cbData;

        PNEMUVIDEOCM_ALLOC_REF pAllocRef = (PNEMUVIDEOCM_ALLOC_REF)nemuWddmMemAllocZero(sizeof (*pAllocRef) + sizeof (PFN_NUMBER) * ADDRESS_AND_SIZE_TO_SPAN_PAGES(BaseVa, cbLength));
        if (pAllocRef)
        {
            pAllocRef->cRefs = 1;
            MmInitializeMdl(&pAllocRef->Mdl, BaseVa, cbLength);
            __try
            {
                MmProbeAndLockPages(&pAllocRef->Mdl, KernelMode, IoWriteAccess);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                Assert(0);
                Status = STATUS_UNSUCCESSFUL;
            }

            if (Status == STATUS_SUCCESS)
            {
                PVOID pvUm = MmMapLockedPagesSpecifyCache(&pAllocRef->Mdl, UserMode, MmNonCached,
                          NULL, /* PVOID BaseAddress */
                          FALSE, /* ULONG BugCheckOnFailure */
                          NormalPagePriority);
                if (pvUm)
                {
                    pAllocRef->pvUm = pvUm;
                    pAllocRef->pContext = pContext;
                    pAllocRef->pAlloc = pAlloc;
                    pAllocRef->fUhgsmiType = pUmAlloc->fUhgsmiType;
                    pAllocRef->pSynchEvent = pSynchEvent;
                    ExAcquireFastMutex(&pContext->Mutex);
                    pAllocRef->hSessionHandle = nemuWddmHTablePut(&pContext->AllocTable, pAllocRef);
                    ExReleaseFastMutex(&pContext->Mutex);
                    if (NEMUWDDM_HANDLE_INVALID != pAllocRef->hSessionHandle)
                    {
                        pUmAlloc->hAlloc = pAllocRef->hSessionHandle;
                        pUmAlloc->cbData = pAlloc->cbData;
                        pUmAlloc->pvData = (uint64_t)pvUm;
                        return STATUS_SUCCESS;
                    }

                    MmUnmapLockedPages(pvUm, &pAllocRef->Mdl);
                }
                else
                {
                    Assert(0);
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }

                MmUnlockPages(&pAllocRef->Mdl);
            }

            nemuWddmMemFree(pAllocRef);
        }
        else
        {
            Assert(0);
            Status = STATUS_NO_MEMORY;
        }

        if (pSynchEvent)
            ObDereferenceObject(pSynchEvent);
    }
    else
    {
        Assert(0);
    }


    return Status;
}

NTSTATUS nemuVideoAMgrCtxAllocUnmap(PNEMUVIDEOCM_ALLOC_CONTEXT pContext, NEMUDISP_KMHANDLE hSesionHandle, PNEMUVIDEOCM_ALLOC *ppAlloc)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ExAcquireFastMutex(&pContext->Mutex);
    PNEMUVIDEOCM_ALLOC_REF pAllocRef = (PNEMUVIDEOCM_ALLOC_REF)nemuWddmHTableRemove(&pContext->AllocTable, hSesionHandle);
    ExReleaseFastMutex(&pContext->Mutex);
    if (pAllocRef)
    {
        /* wait for the dereference, i.e. for all commands involving this allocation to complete */
        nemuWddmCounterU32Wait(&pAllocRef->cRefs, 1);

        MmUnmapLockedPages(pAllocRef->pvUm, &pAllocRef->Mdl);

        MmUnlockPages(&pAllocRef->Mdl);
        *ppAlloc = pAllocRef->pAlloc;
        if (pAllocRef->pSynchEvent)
            ObDereferenceObject(pAllocRef->pSynchEvent);
        nemuWddmMemFree(pAllocRef);
    }
    else
    {
        Assert(0);
        Status = STATUS_INVALID_PARAMETER;
    }

    return Status;
}

static PNEMUVIDEOCM_ALLOC_REF nemuVideoAMgrCtxAllocRefAcquire(PNEMUVIDEOCM_ALLOC_CONTEXT pContext, NEMUDISP_KMHANDLE hSesionHandle)
{
    ExAcquireFastMutex(&pContext->Mutex);
    PNEMUVIDEOCM_ALLOC_REF pAllocRef = (PNEMUVIDEOCM_ALLOC_REF)nemuWddmHTableGet(&pContext->AllocTable, hSesionHandle);
    if (pAllocRef)
        ASMAtomicIncU32(&pAllocRef->cRefs);
    ExReleaseFastMutex(&pContext->Mutex);
    return pAllocRef;
}

static VOID nemuVideoAMgrCtxAllocRefRelease(PNEMUVIDEOCM_ALLOC_REF pRef)
{
    uint32_t cRefs = ASMAtomicDecU32(&pRef->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    Assert(cRefs >= 1); /* we do not do cleanup-on-zero here, instead we wait for the cRefs to reach 1 in nemuVideoAMgrCtxAllocUnmap before unmapping */
}



NTSTATUS nemuVideoAMgrCtxAllocCreate(PNEMUVIDEOCM_ALLOC_CONTEXT pContext, PNEMUVIDEOCM_UM_ALLOC pUmAlloc)
{
    PNEMUVIDEOCM_ALLOC pAlloc;
    PNEMUVIDEOCM_ALLOC_MGR pMgr = pContext->pMgr;
    NTSTATUS Status = nemuVideoAMgrAllocCreate(pMgr, pUmAlloc->cbData, &pAlloc);
    if (Status == STATUS_SUCCESS)
    {
        Status = nemuVideoAMgrCtxAllocMap(pContext, pAlloc, pUmAlloc);
        if (Status == STATUS_SUCCESS)
            return STATUS_SUCCESS;
        else
        {
            Assert(0);
        }
        nemuVideoAMgrAllocDestroy(pMgr, pAlloc);
    }
    else
    {
        Assert(0);
    }
    return Status;
}

NTSTATUS nemuVideoAMgrCtxAllocDestroy(PNEMUVIDEOCM_ALLOC_CONTEXT pContext, NEMUDISP_KMHANDLE hSesionHandle)
{
    PNEMUVIDEOCM_ALLOC pAlloc;
    PNEMUVIDEOCM_ALLOC_MGR pMgr = pContext->pMgr;
    NTSTATUS Status = nemuVideoAMgrCtxAllocUnmap(pContext, hSesionHandle, &pAlloc);
    if (Status == STATUS_SUCCESS)
    {
        nemuVideoAMgrAllocDestroy(pMgr, pAlloc);
    }
    else
    {
        Assert(0);
    }
    return Status;
}

#ifdef NEMU_WITH_CRHGSMI
static DECLCALLBACK(VOID) nemuVideoAMgrAllocSubmitCompletion(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, PVOID pvContext)
{
    /* we should be called from our DPC routine */
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PNEMUVDMACBUF_DR pDr = (PNEMUVDMACBUF_DR)pvContext;
    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUVDMACMD_BODY(pHdr, NEMUVDMACMD_CHROMIUM_CMD);
    UINT cBufs = pBody->cBuffers;
    for (UINT i = 0; i < cBufs; ++i)
    {
        NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[i];
        PNEMUVIDEOCM_ALLOC_REF pRef = (PNEMUVIDEOCM_ALLOC_REF)pBufCmd->u64GuestData;
        if (!pBufCmd->u32GuestData)
        {
            /* signal completion */
            if (pRef->pSynchEvent)
                KeSetEvent(pRef->pSynchEvent, 3, FALSE);
        }

        nemuVideoAMgrCtxAllocRefRelease(pRef);
    }

    nemuVdmaCBufDrFree(&pDevExt->u.primary.Vdma, pDr);
}

/* submits a set of chromium uhgsmi buffers to host for processing */
NTSTATUS nemuVideoAMgrCtxAllocSubmit(PNEMUMP_DEVEXT pDevExt, PNEMUVIDEOCM_ALLOC_CONTEXT pContext, UINT cBuffers, NEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE *paBuffers)
{
    /* ensure we do not overflow the 32bit buffer size value */
    if (NEMUWDDM_TRAILARRAY_MAXELEMENTSU32(NEMUVDMACMD_CHROMIUM_CMD, aBuffers) < cBuffers)
    {
        WARN(("number of buffers passed too big (%d), max is (%d)", cBuffers, NEMUWDDM_TRAILARRAY_MAXELEMENTSU32(NEMUVDMACMD_CHROMIUM_CMD, aBuffers)));
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    UINT cbCmd = NEMUVDMACMD_SIZE_FROMBODYSIZE(RT_OFFSETOF(NEMUVDMACMD_CHROMIUM_CMD, aBuffers[cBuffers]));

    PNEMUVDMACBUF_DR pDr = nemuVdmaCBufDrCreate(&pDevExt->u.primary.Vdma, cbCmd);
    if (pDr)
    {
        // nemuVdmaCBufDrCreate zero initializes the pDr
        pDr->fFlags = NEMUVDMACBUF_FLAG_BUF_FOLLOWS_DR;
        pDr->cbBuf = cbCmd;
        pDr->rc = VERR_NOT_IMPLEMENTED;

        PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
        pHdr->enmType = NEMUVDMACMD_TYPE_CHROMIUM_CMD;
        pHdr->u32CmdSpecific = 0;
        NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUVDMACMD_BODY(pHdr, NEMUVDMACMD_CHROMIUM_CMD);
        pBody->cBuffers = cBuffers;
        for (UINT i = 0; i < cBuffers; ++i)
        {
            NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[i];
            NEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE *pBufInfo = &paBuffers[i];
            PNEMUVIDEOCM_ALLOC_REF pRef = nemuVideoAMgrCtxAllocRefAcquire(pContext, pBufInfo->hAlloc);
            if (pRef)
            {
#ifdef DEBUG_misha
                Assert(pRef->cRefs == 2);
#endif
                pBufCmd->offBuffer = pRef->pAlloc->offData + pBufInfo->Info.offData;
                pBufCmd->cbBuffer = pBufInfo->Info.cbData;
                pBufCmd->u32GuestData = 0;
                pBufCmd->u64GuestData = (uint64_t)pRef;
            }
            else
            {
                WARN(("nemuVideoAMgrCtxAllocRefAcquire failed for hAlloc(0x%x)\n", pBufInfo->hAlloc));
                /* release all previously acquired aloc references */
                for (UINT j = 0; j < i; ++j)
                {
                    NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmdJ = &pBody->aBuffers[j];
                    PNEMUVIDEOCM_ALLOC_REF pRefJ = (PNEMUVIDEOCM_ALLOC_REF)pBufCmdJ;
                    nemuVideoAMgrCtxAllocRefRelease(pRefJ);
                }
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        if (Status == STATUS_SUCCESS)
        {
            PNEMUVDMADDI_CMD pDdiCmd = NEMUVDMADDI_CMD_FROM_BUF_DR(pDr);
            nemuVdmaDdiCmdInit(pDdiCmd, 0, 0, nemuVideoAMgrAllocSubmitCompletion, pDr);
            /* mark command as submitted & invisible for the dx runtime since dx did not originate it */
            nemuVdmaDdiCmdSubmittedNotDx(pDdiCmd);
            int rc = nemuVdmaCBufDrSubmit(pDevExt, &pDevExt->u.primary.Vdma, pDr);
            if (RT_SUCCESS(rc))
            {
                return STATUS_SUCCESS;
            }

            WARN(("nemuVdmaCBufDrSubmit failed with rc (%d)\n", rc));

            /* failure branch */
            /* release all previously acquired aloc references */
            for (UINT i = 0; i < cBuffers; ++i)
            {
                NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[i];
                PNEMUVIDEOCM_ALLOC_REF pRef = (PNEMUVIDEOCM_ALLOC_REF)pBufCmd;
                nemuVideoAMgrCtxAllocRefRelease(pRef);
            }
        }

        nemuVdmaCBufDrFree(&pDevExt->u.primary.Vdma, pDr);
    }
    else
    {
        Assert(0);
        /* @todo: try flushing.. */
        LOGREL(("nemuVdmaCBufDrCreate returned NULL"));
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}
#endif

NTSTATUS nemuVideoAMgrCreate(PNEMUMP_DEVEXT pDevExt, PNEMUVIDEOCM_ALLOC_MGR pMgr, uint32_t offData, uint32_t cbData)
{
    Assert(!(offData & (PAGE_SIZE -1)));
    Assert(!(cbData & (PAGE_SIZE -1)));
    offData = NEMUWDDM_ROUNDBOUND(offData, PAGE_SIZE);
    cbData &= (~(PAGE_SIZE -1));
    Assert(cbData);
    if (!cbData)
        return STATUS_INVALID_PARAMETER;

    ExInitializeFastMutex(&pMgr->Mutex);
    NTSTATUS Status = nemuWddmHTableCreate(&pMgr->AllocTable, 64);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Status = nemuMmInit(&pMgr->Mm, BYTES_TO_PAGES(cbData));
        Assert(Status == STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
        {
            PHYSICAL_ADDRESS PhysicalAddress = {0};
            PhysicalAddress.QuadPart = NemuCommonFromDeviceExt(pDevExt)->phVRAM.QuadPart + offData;
            pMgr->pvData = (uint8_t*)MmMapIoSpace(PhysicalAddress, cbData, MmNonCached);
            Assert(pMgr->pvData);
            if (pMgr->pvData)
            {
                pMgr->offData = offData;
                pMgr->cbData = cbData;
                return STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_UNSUCCESSFUL;
            }
            nemuMmTerm(&pMgr->Mm);
        }
        nemuWddmHTableDestroy(&pMgr->AllocTable);
    }

    return Status;
}

NTSTATUS nemuVideoAMgrDestroy(PNEMUMP_DEVEXT pDevExt, PNEMUVIDEOCM_ALLOC_MGR pMgr)
{
    MmUnmapIoSpace(pMgr->pvData, pMgr->cbData);
    nemuMmTerm(&pMgr->Mm);
    nemuWddmHTableDestroy(&pMgr->AllocTable);
    return STATUS_SUCCESS;
}

NTSTATUS nemuVideoAMgrCtxCreate(PNEMUVIDEOCM_ALLOC_MGR pMgr, PNEMUVIDEOCM_ALLOC_CONTEXT pCtx)
{
    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    if (pMgr->pvData)
    {
        ExInitializeFastMutex(&pCtx->Mutex);
        Status = nemuWddmHTableCreate(&pCtx->AllocTable, 32);
        Assert(Status == STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
        {
            pCtx->pMgr = pMgr;
            return STATUS_SUCCESS;
        }
    }
    return Status;
}

NTSTATUS nemuVideoAMgrCtxDestroy(PNEMUVIDEOCM_ALLOC_CONTEXT pCtx)
{
    if (!pCtx->pMgr)
        return STATUS_SUCCESS;

    NEMUWDDM_HTABLE_ITERATOR Iter;
    NTSTATUS Status = STATUS_SUCCESS;

    nemuWddmHTableIterInit(&pCtx->AllocTable, &Iter);
    do
    {
        PNEMUVIDEOCM_ALLOC_REF pRef = (PNEMUVIDEOCM_ALLOC_REF)nemuWddmHTableIterNext(&Iter, NULL);
        if (!pRef)
            break;

        Assert(0);

        Status = nemuVideoAMgrCtxAllocDestroy(pCtx, pRef->hSessionHandle);
        Assert(Status == STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
            break;
        //        nemuWddmHTableIterRemoveCur(&Iter);
    } while (1);

    if (Status == STATUS_SUCCESS)
    {
        nemuWddmHTableDestroy(&pCtx->AllocTable);
    }

    return Status;
}


VOID nemuWddmSleep(uint32_t u32Val)
{
    LARGE_INTEGER Interval;
    Interval.QuadPart = -(int64_t) 2 /* ms */ * 10000;

    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

VOID nemuWddmCounterU32Wait(uint32_t volatile * pu32, uint32_t u32Val)
{
    LARGE_INTEGER Interval;
    Interval.QuadPart = -(int64_t) 2 /* ms */ * 10000;
    uint32_t u32CurVal;

    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);

    while ((u32CurVal = ASMAtomicReadU32(pu32)) != u32Val)
    {
        Assert(u32CurVal >= u32Val);
        Assert(u32CurVal < UINT32_MAX/2);

        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }
}

/* dump user-mode driver debug info */
static char    g_aNemuUmdD3DCAPS9[304];
static NEMUDISPIFESCAPE_DBGDUMPBUF_FLAGS g_NemuUmdD3DCAPS9Flags;
static BOOLEAN g_bNemuUmdD3DCAPS9IsInited = FALSE;

static void nemuUmdDumpDword(DWORD *pvData, DWORD cData)
{
    char aBuf[16*4];
    DWORD dw1, dw2, dw3, dw4;
    for (UINT i = 0; i < (cData & (~3)); i+=4)
    {
        dw1 = *pvData++;
        dw2 = *pvData++;
        dw3 = *pvData++;
        dw4 = *pvData++;
        sprintf(aBuf, "0x%08x, 0x%08x, 0x%08x, 0x%08x,\n", dw1, dw2, dw3, dw4);
        LOGREL(("%s", aBuf));
    }

    cData = cData % 4;
    switch (cData)
    {
        case 3:
            dw1 = *pvData++;
            dw2 = *pvData++;
            dw3 = *pvData++;
            sprintf(aBuf, "0x%08x, 0x%08x, 0x%08x\n", dw1, dw2, dw3);
            LOGREL(("%s", aBuf));
            break;
        case 2:
            dw1 = *pvData++;
            dw2 = *pvData++;
            sprintf(aBuf, "0x%08x, 0x%08x\n", dw1, dw2);
            LOGREL(("%s", aBuf));
            break;
        case 1:
            dw1 = *pvData++;
            sprintf(aBuf, "0x%8x\n", dw1);
            LOGREL(("%s", aBuf));
            break;
        default:
            break;
    }
}

static void nemuUmdDumpD3DCAPS9(void *pvData, PNEMUDISPIFESCAPE_DBGDUMPBUF_FLAGS pFlags)
{
    AssertCompile(!(sizeof (g_aNemuUmdD3DCAPS9) % sizeof (DWORD)));
    LOGREL(("*****Start Dumping D3DCAPS9:*******"));
    LOGREL(("WoW64 flag(%d)", (UINT)pFlags->WoW64));
    nemuUmdDumpDword((DWORD*)pvData, sizeof (g_aNemuUmdD3DCAPS9) / sizeof (DWORD));
    LOGREL(("*****End Dumping D3DCAPS9**********"));
}

NTSTATUS nemuUmdDumpBuf(PNEMUDISPIFESCAPE_DBGDUMPBUF pBuf, uint32_t cbBuffer)
{
    if (cbBuffer < RT_OFFSETOF(NEMUDISPIFESCAPE_DBGDUMPBUF, aBuf[0]))
    {
        WARN(("Buffer too small"));
        return STATUS_BUFFER_TOO_SMALL;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t cbString = cbBuffer - RT_OFFSETOF(NEMUDISPIFESCAPE_DBGDUMPBUF, aBuf[0]);
    switch (pBuf->enmType)
    {
        case NEMUDISPIFESCAPE_DBGDUMPBUF_TYPE_D3DCAPS9:
        {
            if (cbString != sizeof (g_aNemuUmdD3DCAPS9))
            {
                WARN(("wrong caps size, expected %d, but was %d", sizeof (g_aNemuUmdD3DCAPS9), cbString));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (g_bNemuUmdD3DCAPS9IsInited)
            {
                if (!memcmp(g_aNemuUmdD3DCAPS9, pBuf->aBuf, sizeof (g_aNemuUmdD3DCAPS9)))
                    break;

                WARN(("caps do not match!"));
                nemuUmdDumpD3DCAPS9(pBuf->aBuf, &pBuf->Flags);
                break;
            }

            memcpy(g_aNemuUmdD3DCAPS9, pBuf->aBuf, sizeof (g_aNemuUmdD3DCAPS9));
            g_NemuUmdD3DCAPS9Flags = pBuf->Flags;
            g_bNemuUmdD3DCAPS9IsInited = TRUE;
            nemuUmdDumpD3DCAPS9(pBuf->aBuf, &pBuf->Flags);
        }
    }

    return Status;
}

#if 0
VOID nemuShRcTreeInit(PNEMUMP_DEVEXT pDevExt)
{
    ExInitializeFastMutex(&pDevExt->ShRcTreeMutex);
    pDevExt->ShRcTree = NULL;
}

VOID nemuShRcTreeTerm(PNEMUMP_DEVEXT pDevExt)
{
    Assert(!pDevExt->ShRcTree);
    pDevExt->ShRcTree = NULL;
}

BOOLEAN nemuShRcTreePut(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pAlloc)
{
    HANDLE hSharedRc = pAlloc->hSharedHandle;
    if (!hSharedRc)
    {
        WARN(("invalid call with zero shared handle!"));
        return FALSE;
    }
    pAlloc->ShRcTreeEntry.Key = (AVLPVKEY)hSharedRc;
    ExAcquireFastMutex(&pDevExt->ShRcTreeMutex);
    bool bRc = RTAvlPVInsert(&pDevExt->ShRcTree, &pAlloc->ShRcTreeEntry);
    ExReleaseFastMutex(&pDevExt->ShRcTreeMutex);
    Assert(bRc);
    return (BOOLEAN)bRc;
}

#define PNEMUWDDM_ALLOCATION_FROM_SHRCTREENODE(_p) ((PNEMUWDDM_ALLOCATION)(((uint8_t*)(_p)) - RT_OFFSETOF(NEMUWDDM_ALLOCATION, ShRcTreeEntry)))
PNEMUWDDM_ALLOCATION nemuShRcTreeGet(PNEMUMP_DEVEXT pDevExt, HANDLE hSharedRc)
{
    ExAcquireFastMutex(&pDevExt->ShRcTreeMutex);
    PAVLPVNODECORE pNode = RTAvlPVGet(&pDevExt->ShRcTree, (AVLPVKEY)hSharedRc);
    ExReleaseFastMutex(&pDevExt->ShRcTreeMutex);
    if (!pNode)
        return NULL;
    PNEMUWDDM_ALLOCATION pAlloc = PNEMUWDDM_ALLOCATION_FROM_SHRCTREENODE(pNode);
    return pAlloc;
}

BOOLEAN nemuShRcTreeRemove(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pAlloc)
{
    HANDLE hSharedRc = pAlloc->hSharedHandle;
    if (!hSharedRc)
    {
        WARN(("invalid call with zero shared handle!"));
        return FALSE;
    }
    ExAcquireFastMutex(&pDevExt->ShRcTreeMutex);
    PAVLPVNODECORE pNode = RTAvlPVRemove(&pDevExt->ShRcTree, (AVLPVKEY)hSharedRc);
    ExReleaseFastMutex(&pDevExt->ShRcTreeMutex);
    if (!pNode)
        return NULL;
    PNEMUWDDM_ALLOCATION pRetAlloc = PNEMUWDDM_ALLOCATION_FROM_SHRCTREENODE(pNode);
    Assert(pRetAlloc == pAlloc);
    return !!pRetAlloc;
}
#endif

NTSTATUS nemuWddmDrvCfgInit(PUNICODE_STRING pRegStr)
{
    HANDLE hKey;
    OBJECT_ATTRIBUTES ObjAttr;

    InitializeObjectAttributes(&ObjAttr, pRegStr, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS Status = ZwOpenKey(&hKey, GENERIC_READ, &ObjAttr);
    if (!NT_SUCCESS(Status))
    {
        WARN(("ZwOpenKey for settings key failed, Status 0x%x", Status));
        return Status;
    }

    DWORD dwValue = 0;
    Status = nemuWddmRegQueryValueDword(hKey, NEMUWDDM_CFG_STR_LOG_UM, &dwValue);
    if (NT_SUCCESS(Status))
        g_NemuLogUm = dwValue;

    ZwClose(hKey);

    return Status;
}

NTSTATUS nemuWddmThreadCreate(PKTHREAD * ppThread, PKSTART_ROUTINE pStartRoutine, PVOID pStartContext)
{
    NTSTATUS fStatus;
    HANDLE hThread;
    OBJECT_ATTRIBUTES fObjectAttributes;

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    InitializeObjectAttributes(&fObjectAttributes, NULL, OBJ_KERNEL_HANDLE,
                        NULL, NULL);

    fStatus = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS,
                        &fObjectAttributes, NULL, NULL,
                        (PKSTART_ROUTINE) pStartRoutine, pStartContext);
    if (!NT_SUCCESS(fStatus))
      return fStatus;

    ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL,
                        KernelMode, (PVOID*) ppThread, NULL);
    ZwClose(hThread);
    return STATUS_SUCCESS;
}

#ifdef NEMU_VDMA_WITH_WATCHDOG
static int nemuWddmWdProgram(PNEMUMP_DEVEXT pDevExt, uint32_t cMillis)
{
    int rc = VINF_SUCCESS;
    PNEMUVDMA_CTL pCmd = (PNEMUVDMA_CTL)NemuSHGSMICommandAlloc(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, sizeof (NEMUVDMA_CTL), HGSMI_CH_VBVA, VBVA_VDMA_CTL);
    if (pCmd)
    {
        pCmd->enmCtl = NEMUVDMA_CTL_TYPE_WATCHDOG;
        pCmd->u32Offset = cMillis;
        pCmd->i32Result = VERR_NOT_SUPPORTED;

        const NEMUSHGSMIHEADER* pHdr = NemuSHGSMICommandPrepSynch(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, pCmd);
        Assert(pHdr);
        if (pHdr)
        {
            do
            {
                HGSMIOFFSET offCmd = NemuSHGSMICommandOffset(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, pHdr);
                Assert(offCmd != HGSMIOFFSET_VOID);
                if (offCmd != HGSMIOFFSET_VOID)
                {
                    NemuVideoCmnPortWriteUlong(NemuCommonFromDeviceExt(pDevExt)->guestCtx.port, offCmd);
                    /* Make the compiler aware that the host has changed memory. */
                    ASMCompilerBarrier();
                    rc = NemuSHGSMICommandDoneSynch(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, pHdr);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        rc = pCmd->i32Result;
                        AssertRC(rc);
                    }
                    break;
                }
                else
                    rc = VERR_INVALID_PARAMETER;
                /* fail to submit, cancel it */
                NemuSHGSMICommandCancelSynch(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, pHdr);
            } while (0);
        }

        NemuSHGSMICommandFree (&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, pCmd);
    }
    else
    {
        LOGREL(("HGSMIHeapAlloc failed"));
        rc = VERR_OUT_OF_RESOURCES;
    }
    return rc;
}

static uint32_t g_NemuWdTimeout = 4000;
/* if null g_NemuWdTimeout / 2 is used */
static uint32_t g_NemuWdTimerPeriod = 0;

static VOID nemuWddmWdThread(PVOID pvUser)
{
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)pvUser;
    BOOLEAN bExit = FALSE;
    int rc;
    while (1)
    {
        if (!bExit)
        {
            rc = nemuWddmWdProgram(pDevExt, g_NemuWdTimeout /* ms */);
            AssertRC(rc);
        }
        else
        {
            rc = nemuWddmWdProgram(pDevExt, 0 /* to disable WatchDog */);
            AssertRC(rc);
            break;
        }
        LARGE_INTEGER Timeout;
        uint32_t timerTimeOut = g_NemuWdTimerPeriod ? g_NemuWdTimerPeriod : g_NemuWdTimeout / 2;
        Timeout.QuadPart = 10000ULL * timerTimeOut /* ms */;
        NTSTATUS Status = KeWaitForSingleObject(&pDevExt->WdEvent, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_TIMEOUT)
            bExit = TRUE;
    }
}

NTSTATUS nemuWddmWdInit(PNEMUMP_DEVEXT pDevExt)
{
    KeInitializeEvent(&pDevExt->WdEvent, NotificationEvent, FALSE);

    NTSTATUS Status = nemuWddmThreadCreate(&pDevExt->pWdThread, nemuWddmWdThread, pDevExt);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuWddmThreadCreate failed, Status 0x%x", Status));
        pDevExt->pWdThread = NULL;
    }
    return Status;
}

NTSTATUS nemuWddmWdTerm(PNEMUMP_DEVEXT pDevExt)
{
    if (!pDevExt->pWdThread)
        return STATUS_SUCCESS;

    KeSetEvent(&pDevExt->WdEvent, 0, FALSE);

    KeWaitForSingleObject(pDevExt->pWdThread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(pDevExt->pWdThread);
    pDevExt->pWdThread = NULL;
    return STATUS_SUCCESS;
}
#endif

static int nemuWddmSlConfigure(PNEMUMP_DEVEXT pDevExt, uint32_t fFlags)
{
    PHGSMIGUESTCOMMANDCONTEXT pCtx = &NemuCommonFromDeviceExt(pDevExt)->guestCtx;
    VBVASCANLINECFG *pCfg;
    int rc = VINF_SUCCESS;

    /* Allocate the IO buffer. */
    pCfg = (VBVASCANLINECFG *)NemuHGSMIBufferAlloc(pCtx,
                                       sizeof (VBVASCANLINECFG), HGSMI_CH_VBVA,
                                       VBVA_SCANLINE_CFG);

    if (pCfg)
    {
        /* Prepare data to be sent to the host. */
        pCfg->rc    = VERR_NOT_IMPLEMENTED;
        pCfg->fFlags = fFlags;
        rc = NemuHGSMIBufferSubmit(pCtx, pCfg);
        if (RT_SUCCESS(rc))
        {
            AssertRC(pCfg->rc);
            rc = pCfg->rc;
        }
        /* Free the IO buffer. */
        NemuHGSMIBufferFree(pCtx, pCfg);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}

NTSTATUS NemuWddmSlEnableVSyncNotification(PNEMUMP_DEVEXT pDevExt, BOOLEAN fEnable)
{
    if (!pDevExt->bVSyncTimerEnabled == !fEnable)
        return STATUS_SUCCESS;

    if (!fEnable)
    {
        KeCancelTimer(&pDevExt->VSyncTimer);
    }
    else
    {
        KeQuerySystemTime((PLARGE_INTEGER)&pDevExt->VSyncTime);

        LARGE_INTEGER DueTime;
        DueTime.QuadPart = -166666LL; /* 60 Hz */
        KeSetTimerEx(&pDevExt->VSyncTimer, DueTime, 16, &pDevExt->VSyncDpc);
    }

    pDevExt->bVSyncTimerEnabled = !!fEnable;

    return STATUS_SUCCESS;
}

NTSTATUS NemuWddmSlGetScanLine(PNEMUMP_DEVEXT pDevExt, DXGKARG_GETSCANLINE *pGetScanLine)
{
    Assert((UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays > pGetScanLine->VidPnTargetId);
    NEMUWDDM_TARGET *pTarget = &pDevExt->aTargets[pGetScanLine->VidPnTargetId];
    Assert(pTarget->Size.cx);
    Assert(pTarget->Size.cy);
    if (pTarget->Size.cy)
    {
        uint32_t curScanLine;
        BOOL bVBlank;
        LARGE_INTEGER DevVSyncTime;
        DevVSyncTime.QuadPart =  ASMAtomicReadU64((volatile uint64_t*)&pDevExt->VSyncTime.QuadPart);
        LARGE_INTEGER VSyncTime;
        KeQuerySystemTime(&VSyncTime);

        if (VSyncTime.QuadPart < DevVSyncTime.QuadPart)
        {
            WARN(("vsync time is less than the one stored in device"));
            curScanLine = 0;
        }
        else
        {
            VSyncTime.QuadPart = VSyncTime.QuadPart - DevVSyncTime.QuadPart;
            /* time is in 100ns, */
            curScanLine = (uint32_t)((pTarget->Size.cy * VSyncTime.QuadPart) / DevVSyncTime.QuadPart);
            if (pDevExt->bVSyncTimerEnabled)
            {
                if (curScanLine >= pTarget->Size.cy)
                    curScanLine = 0;
            }
            else
            {
                curScanLine %= pTarget->Size.cy;
            }
        }

        bVBlank = (!curScanLine || curScanLine > pTarget->Size.cy);
        pGetScanLine->ScanLine = curScanLine;
        pGetScanLine->InVerticalBlank = bVBlank;
    }
    else
    {
        pGetScanLine->InVerticalBlank = TRUE;
        pGetScanLine->ScanLine = 0;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN nemuWddmSlVSyncIrqCb(PVOID pvContext)
{
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)pvContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA notify;
    BOOLEAN bNeedDpc = FALSE;
    for (UINT i = 0; i < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        PNEMUWDDM_TARGET pTarget = &pDevExt->aTargets[i];
        if (pTarget->fConnected)
        {
            memset(&notify, 0, sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA));
#ifdef NEMU_WDDM_WIN8
            notify.InterruptType = g_NemuDisplayOnly?
                                       DXGK_INTERRUPT_DISPLAYONLY_VSYNC:
                                       DXGK_INTERRUPT_CRTC_VSYNC;
#else
            notify.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
#endif
            notify.CrtcVsync.VidPnTargetId = i;
            pDevExt->u.primary.DxgkInterface.DxgkCbNotifyInterrupt(pDevExt->u.primary.DxgkInterface.DeviceHandle, &notify);
            bNeedDpc = TRUE;
        }
    }

    if (bNeedDpc)
    {
        pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);
    }

    return FALSE;
}

static VOID nemuWddmSlVSyncDpc(
  __in      struct _KDPC *Dpc,
  __in_opt  PVOID DeferredContext,
  __in_opt  PVOID SystemArgument1,
  __in_opt  PVOID SystemArgument2
)
{
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)DeferredContext;
    Assert(!pDevExt->fVSyncInVBlank);
    ASMAtomicWriteU32(&pDevExt->fVSyncInVBlank, 1);

    BOOLEAN bDummy;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            nemuWddmSlVSyncIrqCb,
            pDevExt,
            0, /* IN ULONG MessageNumber */
            &bDummy);
    if (!NT_SUCCESS(Status))
        WARN(("DxgkCbSynchronizeExecution failed Status %#x", Status));

    LARGE_INTEGER VSyncTime;
    KeQuerySystemTime(&VSyncTime);
    ASMAtomicWriteU64((volatile uint64_t*)&pDevExt->VSyncTime.QuadPart, VSyncTime.QuadPart);

    ASMAtomicWriteU32(&pDevExt->fVSyncInVBlank, 0);
}

NTSTATUS NemuWddmSlInit(PNEMUMP_DEVEXT pDevExt)
{
    pDevExt->bVSyncTimerEnabled = FALSE;
    pDevExt->fVSyncInVBlank = 0;
    KeQuerySystemTime((PLARGE_INTEGER)&pDevExt->VSyncTime);
    KeInitializeTimer(&pDevExt->VSyncTimer);
    KeInitializeDpc(&pDevExt->VSyncDpc, nemuWddmSlVSyncDpc, pDevExt);
    return STATUS_SUCCESS;
}

NTSTATUS NemuWddmSlTerm(PNEMUMP_DEVEXT pDevExt)
{
    KeCancelTimer(&pDevExt->VSyncTimer);
    return STATUS_SUCCESS;
}

#ifdef NEMU_WDDM_WIN8
void nemuWddmDiInitDefault(DXGK_DISPLAY_INFORMATION *pInfo, PHYSICAL_ADDRESS PhAddr, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    pInfo->Width = 1024;
    pInfo->Height = 768;
    pInfo->Pitch = pInfo->Width * 4;
    pInfo->ColorFormat = D3DDDIFMT_A8R8G8B8;
    pInfo->PhysicAddress = PhAddr;
    pInfo->TargetId = VidPnSourceId;
    pInfo->AcpiId = 0;
}

void nemuWddmDiToAllocData(PNEMUMP_DEVEXT pDevExt, const DXGK_DISPLAY_INFORMATION *pInfo, PNEMUWDDM_ALLOC_DATA pAllocData)
{
    pAllocData->SurfDesc.width = pInfo->Width;
    pAllocData->SurfDesc.height = pInfo->Height;
    pAllocData->SurfDesc.format = pInfo->ColorFormat;
    pAllocData->SurfDesc.bpp = nemuWddmCalcBitsPerPixel(pInfo->ColorFormat);
    pAllocData->SurfDesc.pitch = pInfo->Pitch;
    pAllocData->SurfDesc.depth = 1;
    pAllocData->SurfDesc.slicePitch = pInfo->Pitch;
    pAllocData->SurfDesc.cbSize = pInfo->Pitch * pInfo->Height;
    pAllocData->SurfDesc.VidPnSourceId = pInfo->TargetId;
    pAllocData->SurfDesc.RefreshRate.Numerator = 60000;
    pAllocData->SurfDesc.RefreshRate.Denominator = 1000;

    /* the address here is not a VRAM offset! so convert it to offset */
    nemuWddmAddrSetVram(&pAllocData->Addr, 1,
            nemuWddmVramAddrToOffset(pDevExt, pInfo->PhysicAddress));
}

void nemuWddmDmSetupDefaultVramLocation(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID ModifiedVidPnSourceId, NEMUWDDM_SOURCE *paSources)
{
    PNEMUWDDM_SOURCE pSource = &paSources[ModifiedVidPnSourceId];
    AssertRelease(g_NemuDisplayOnly);
    ULONG offVram = nemuWddmVramCpuVisibleSegmentSize(pDevExt);
    offVram /= NemuCommonFromDeviceExt(pDevExt)->cDisplays;
    offVram &= ~PAGE_OFFSET_MASK;
    offVram *= ModifiedVidPnSourceId;

    if (nemuWddmAddrSetVram(&pSource->AllocData.Addr, 1, offVram))
        pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_LOCATION;
}
#endif
