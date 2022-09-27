/* $Id: NemuMPVdma.cpp $ */

/** @file
 * Nemu WDDM Miniport driver
 */

/*
 * Copyright (C) 2011-2012 Oracle Corporation
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
#include "common/NemuMPCommon.h"
#include "NemuMPVdma.h"
#include "NemuMPVhwa.h"
#include <iprt/asm.h>
#include "NemuMPCr.h"
#include <iprt/mem.h>

#ifdef NEMU_WITH_CROGL
# include <packer.h>
#endif

static NTSTATUS nemuVdmaCrCtlGetDefaultClientId(PNEMUMP_DEVEXT pDevExt, uint32_t *pu32ClienID);

NTSTATUS nemuVdmaPipeConstruct(PNEMUVDMAPIPE pPipe)
{
    KeInitializeSpinLock(&pPipe->SinchLock);
    KeInitializeEvent(&pPipe->Event, SynchronizationEvent, FALSE);
    InitializeListHead(&pPipe->CmdListHead);
    pPipe->enmState = NEMUVDMAPIPE_STATE_CREATED;
    pPipe->bNeedNotify = true;
    return STATUS_SUCCESS;
}

NTSTATUS nemuVdmaPipeSvrOpen(PNEMUVDMAPIPE pPipe)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;
    KeAcquireSpinLock(&pPipe->SinchLock, &OldIrql);
    Assert(pPipe->enmState == NEMUVDMAPIPE_STATE_CREATED);
    switch (pPipe->enmState)
    {
        case NEMUVDMAPIPE_STATE_CREATED:
            pPipe->enmState = NEMUVDMAPIPE_STATE_OPENNED;
            pPipe->bNeedNotify = false;
            break;
        case NEMUVDMAPIPE_STATE_OPENNED:
            pPipe->bNeedNotify = false;
            break;
        default:
            AssertBreakpoint();
            Status = STATUS_INVALID_PIPE_STATE;
            break;
    }

    KeReleaseSpinLock(&pPipe->SinchLock, OldIrql);
    return Status;
}

NTSTATUS nemuVdmaPipeSvrClose(PNEMUVDMAPIPE pPipe)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;
    KeAcquireSpinLock(&pPipe->SinchLock, &OldIrql);
    Assert(pPipe->enmState == NEMUVDMAPIPE_STATE_CLOSED
            || pPipe->enmState == NEMUVDMAPIPE_STATE_CLOSING);
    switch (pPipe->enmState)
    {
        case NEMUVDMAPIPE_STATE_CLOSING:
            pPipe->enmState = NEMUVDMAPIPE_STATE_CLOSED;
            break;
        case NEMUVDMAPIPE_STATE_CLOSED:
            break;
        default:
            AssertBreakpoint();
            Status = STATUS_INVALID_PIPE_STATE;
            break;
    }

    KeReleaseSpinLock(&pPipe->SinchLock, OldIrql);
    return Status;
}

NTSTATUS nemuVdmaPipeCltClose(PNEMUVDMAPIPE pPipe)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;
    KeAcquireSpinLock(&pPipe->SinchLock, &OldIrql);
    bool bNeedNotify = false;
    Assert(pPipe->enmState == NEMUVDMAPIPE_STATE_OPENNED
                || pPipe->enmState == NEMUVDMAPIPE_STATE_CREATED
                ||  pPipe->enmState == NEMUVDMAPIPE_STATE_CLOSED);
    switch (pPipe->enmState)
    {
        case NEMUVDMAPIPE_STATE_OPENNED:
            pPipe->enmState = NEMUVDMAPIPE_STATE_CLOSING;
            bNeedNotify = pPipe->bNeedNotify;
            pPipe->bNeedNotify = false;
            break;
        case NEMUVDMAPIPE_STATE_CREATED:
            pPipe->enmState = NEMUVDMAPIPE_STATE_CLOSED;
            pPipe->bNeedNotify = false;
            break;
        case NEMUVDMAPIPE_STATE_CLOSED:
            break;
        default:
            AssertBreakpoint();
            Status = STATUS_INVALID_PIPE_STATE;
            break;
    }

    KeReleaseSpinLock(&pPipe->SinchLock, OldIrql);

    if (bNeedNotify)
    {
        KeSetEvent(&pPipe->Event, 0, FALSE);
    }
    return Status;
}

NTSTATUS nemuVdmaPipeDestruct(PNEMUVDMAPIPE pPipe)
{
    Assert(pPipe->enmState == NEMUVDMAPIPE_STATE_CLOSED
            || pPipe->enmState == NEMUVDMAPIPE_STATE_CREATED);
    /* ensure the pipe is closed */
    NTSTATUS Status = nemuVdmaPipeCltClose(pPipe);
    Assert(Status == STATUS_SUCCESS);

    Assert(pPipe->enmState == NEMUVDMAPIPE_STATE_CLOSED);

    return Status;
}

NTSTATUS nemuVdmaPipeSvrCmdGetList(PNEMUVDMAPIPE pPipe, PLIST_ENTRY pDetachHead)
{
    PLIST_ENTRY pEntry = NULL;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;
    NEMUVDMAPIPE_STATE enmState = NEMUVDMAPIPE_STATE_CLOSED;
    do
    {
        bool bListEmpty = true;
        KeAcquireSpinLock(&pPipe->SinchLock, &OldIrql);
        Assert(pPipe->enmState == NEMUVDMAPIPE_STATE_OPENNED
                || pPipe->enmState == NEMUVDMAPIPE_STATE_CLOSING);
        Assert(pPipe->enmState >= NEMUVDMAPIPE_STATE_OPENNED);
        enmState = pPipe->enmState;
        if (enmState >= NEMUVDMAPIPE_STATE_OPENNED)
        {
            nemuVideoLeDetach(&pPipe->CmdListHead, pDetachHead);
            bListEmpty = !!(IsListEmpty(pDetachHead));
            pPipe->bNeedNotify = bListEmpty;
        }
        else
        {
            KeReleaseSpinLock(&pPipe->SinchLock, OldIrql);
            Status = STATUS_INVALID_PIPE_STATE;
            break;
        }

        KeReleaseSpinLock(&pPipe->SinchLock, OldIrql);

        if (!bListEmpty)
        {
            Assert(Status == STATUS_SUCCESS);
            break;
        }

        if (enmState == NEMUVDMAPIPE_STATE_OPENNED)
        {
            Status = KeWaitForSingleObject(&pPipe->Event, Executive, KernelMode, FALSE, NULL /* PLARGE_INTEGER Timeout */);
            Assert(Status == STATUS_SUCCESS);
            if (Status != STATUS_SUCCESS)
                break;
        }
        else
        {
            Assert(enmState == NEMUVDMAPIPE_STATE_CLOSING);
            Status = STATUS_PIPE_CLOSING;
            break;
        }
    } while (1);

    return Status;
}

NTSTATUS nemuVdmaPipeCltCmdPut(PNEMUVDMAPIPE pPipe, PNEMUVDMAPIPE_CMD_HDR pCmd)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;
    bool bNeedNotify = false;

    KeAcquireSpinLock(&pPipe->SinchLock, &OldIrql);

    Assert(pPipe->enmState == NEMUVDMAPIPE_STATE_OPENNED);
    if (pPipe->enmState == NEMUVDMAPIPE_STATE_OPENNED)
    {
        bNeedNotify = pPipe->bNeedNotify;
        InsertHeadList(&pPipe->CmdListHead, &pCmd->ListEntry);
        pPipe->bNeedNotify = false;
    }
    else
        Status = STATUS_INVALID_PIPE_STATE;

    KeReleaseSpinLock(&pPipe->SinchLock, OldIrql);

    if (bNeedNotify)
    {
        KeSetEvent(&pPipe->Event, 0, FALSE);
    }

    return Status;
}

DECLINLINE(void) nemuVdmaDirtyRectsCalcIntersection(const RECT *pArea, const NEMUWDDM_RECTS_INFO *pRects, PNEMUWDDM_RECTS_INFO pResult)
{
    uint32_t cRects = 0;
    for (uint32_t i = 0; i < pRects->cRects; ++i)
    {
        if (nemuWddmRectIntersection(pArea, &pRects->aRects[i], &pResult->aRects[cRects]))
        {
            ++cRects;
        }
    }

    pResult->cRects = cRects;
}

DECLINLINE(bool) nemuVdmaDirtyRectsHasIntersections(const RECT *paRects1, uint32_t cRects1, const RECT *paRects2, uint32_t cRects2)
{
    RECT tmpRect;
    for (uint32_t i = 0; i < cRects1; ++i)
    {
        const RECT * pRect1 = &paRects1[i];
        for (uint32_t j = 0; j < cRects2; ++j)
        {
            const RECT * pRect2 = &paRects2[j];
            if (nemuWddmRectIntersection(pRect1, pRect2, &tmpRect))
                return true;
        }
    }
    return false;
}

DECLINLINE(bool) nemuVdmaDirtyRectsIsCover(const RECT *paRects, uint32_t cRects, const RECT *paRectsCovered, uint32_t cRectsCovered)
{
    for (uint32_t i = 0; i < cRectsCovered; ++i)
    {
        const RECT * pRectCovered = &paRectsCovered[i];
        uint32_t j = 0;
        for (; j < cRects; ++j)
        {
            const RECT * pRect = &paRects[j];
            if (nemuWddmRectIsCoveres(pRect, pRectCovered))
                break;
        }
        if (j == cRects)
            return false;
    }
    return true;
}
#ifdef NEMU_WITH_CROGL
NTSTATUS nemuVdmaPostHideSwapchain(PNEMUWDDM_SWAPCHAIN pSwapchain)
{
    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);
    uint32_t cbCmdInternal = NEMUVIDEOCM_CMD_RECTS_INTERNAL_SIZE4CRECTS(0);
    PNEMUVIDEOCM_CMD_RECTS_INTERNAL pCmdInternal =
            (PNEMUVIDEOCM_CMD_RECTS_INTERNAL)nemuVideoCmCmdCreate(&pSwapchain->pContext->CmContext, cbCmdInternal);
    Assert(pCmdInternal);
    if (pCmdInternal)
    {
        pCmdInternal->hSwapchainUm = pSwapchain->hSwapchainUm;
        pCmdInternal->Cmd.fFlags.Value = 0;
        pCmdInternal->Cmd.fFlags.bAddHiddenRects = 1;
        pCmdInternal->Cmd.fFlags.bHide = 1;
        pCmdInternal->Cmd.RectsInfo.cRects = 0;
        nemuVideoCmCmdSubmit(pCmdInternal, NEMUVIDEOCM_SUBMITSIZE_DEFAULT);
        return STATUS_SUCCESS;
    }
    return STATUS_NO_MEMORY;
}
#endif
static VOID nemuWddmBltPipeRectsTranslate(NEMUVDMAPIPE_RECTS *pRects, int x, int y)
{
    nemuWddmRectTranslate(&pRects->ContextRect, x, y);

    for (UINT i = 0; i < pRects->UpdateRects.cRects; ++i)
    {
        nemuWddmRectTranslate(&pRects->UpdateRects.aRects[i], x, y);
    }
}

static NEMUVDMAPIPE_RECTS * nemuWddmBltPipeRectsDup(const NEMUVDMAPIPE_RECTS *pRects)
{
    const size_t cbDup = RT_OFFSETOF(NEMUVDMAPIPE_RECTS, UpdateRects.aRects[pRects->UpdateRects.cRects]);
    NEMUVDMAPIPE_RECTS *pDup = (NEMUVDMAPIPE_RECTS*)nemuWddmMemAllocZero(cbDup);
    if (!pDup)
    {
        WARN(("nemuWddmMemAllocZero failed"));
        return NULL;
    }
    memcpy(pDup, pRects, cbDup);
    return pDup;
}
#ifdef NEMU_WITH_CROGL
typedef struct NEMUMP_VDMACR_WRITECOMPLETION
{
    void *pvBufferToFree;
} NEMUMP_VDMACR_WRITECOMPLETION, *PNEMUMP_VDMACR_WRITECOMPLETION;

static DECLCALLBACK(void) nemuVdmaCrWriteCompletion(PNEMUMP_CRSHGSMITRANSPORT pCon, int rc, void *pvCtx)
{
    PNEMUMP_VDMACR_WRITECOMPLETION pData = (PNEMUMP_VDMACR_WRITECOMPLETION)pvCtx;
    void* pvBufferToFree = pData->pvBufferToFree;
    if (pvBufferToFree)
        NemuMpCrShgsmiTransportBufFree(pCon, pvBufferToFree);

    NemuMpCrShgsmiTransportCmdTermWriteAsync(pCon, pvCtx);
}

typedef struct NEMUMP_VDMACR_WRITEREADCOMPLETION
{
    void *pvBufferToFree;
    void *pvContext;
} NEMUMP_VDMACR_WRITEREADCOMPLETION, *PNEMUMP_VDMACR_WRITEREADCOMPLETION;

void nemuVdmaCrSubmitWriteReadAsyncGenericCompletion(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvCtx)
{
    PNEMUMP_VDMACR_WRITEREADCOMPLETION pData = (PNEMUMP_VDMACR_WRITEREADCOMPLETION)pvCtx;
    void* pvBufferToFree = pData->pvBufferToFree;
    if (pvBufferToFree)
        NemuMpCrShgsmiTransportBufFree(pCon, pvBufferToFree);

    NemuMpCrShgsmiTransportCmdTermWriteReadAsync(pCon, pvCtx);
}

NTSTATUS nemuVdmaCrSubmitWriteReadAsync(PNEMUMP_DEVEXT pDevExt, NEMUMP_CRPACKER *pCrPacker, uint32_t u32CrConClientID, PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION pfnCompletion, void *pvCompletion)
{
    Assert(u32CrConClientID);
    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t cbBuffer;
    void * pvPackBuffer;
    void * pvBuffer = NemuMpCrPackerTxBufferComplete(pCrPacker, &cbBuffer, &pvPackBuffer);
    if (pvBuffer)
    {
        PNEMUMP_VDMACR_WRITEREADCOMPLETION pvCompletionData = (PNEMUMP_VDMACR_WRITEREADCOMPLETION)NemuMpCrShgsmiTransportCmdCreateWriteReadAsync(&pDevExt->CrHgsmiTransport, u32CrConClientID, pvBuffer, cbBuffer,
                pfnCompletion, sizeof (*pvCompletionData));
        if (pvCompletionData)
        {
            pvCompletionData->pvBufferToFree = pvPackBuffer;
            pvCompletionData->pvContext = pvCompletion;
            int rc = NemuMpCrShgsmiTransportCmdSubmitWriteReadAsync(&pDevExt->CrHgsmiTransport, pvCompletionData);
            if (RT_SUCCESS(rc))
            {
                return STATUS_SUCCESS;
            }
            WARN(("NemuMpCrShgsmiTransportCmdSubmitWriteReadAsync failed, rc %d", rc));
            Status = STATUS_UNSUCCESSFUL;
            NemuMpCrShgsmiTransportCmdTermWriteReadAsync(&pDevExt->CrHgsmiTransport, pvCompletionData);
        }
        else
        {
            WARN(("NemuMpCrShgsmiTransportCmdCreateWriteReadAsync failed"));
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    return Status;
}

NTSTATUS nemuVdmaCrSubmitWriteAsync(PNEMUMP_DEVEXT pDevExt, NEMUMP_CRPACKER *pCrPacker, uint32_t u32CrConClientID)
{
    Assert(u32CrConClientID);
    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t cbBuffer;
    void * pvPackBuffer;
    void * pvBuffer = NemuMpCrPackerTxBufferComplete(pCrPacker, &cbBuffer, &pvPackBuffer);
    if (pvBuffer)
    {
        PNEMUMP_VDMACR_WRITECOMPLETION pvCompletionData = (PNEMUMP_VDMACR_WRITECOMPLETION)NemuMpCrShgsmiTransportCmdCreateWriteAsync(&pDevExt->CrHgsmiTransport, u32CrConClientID, pvBuffer, cbBuffer,
                nemuVdmaCrWriteCompletion, sizeof (*pvCompletionData));
        if (pvCompletionData)
        {
            pvCompletionData->pvBufferToFree = pvPackBuffer;
            int rc = NemuMpCrShgsmiTransportCmdSubmitWriteAsync(&pDevExt->CrHgsmiTransport, pvCompletionData);
            if (RT_SUCCESS(rc))
            {
                return STATUS_SUCCESS;
            }
            WARN(("NemuMpCrShgsmiTransportCmdSubmitWriteAsync failed, rc %d", rc));
            Status = STATUS_UNSUCCESSFUL;
            NemuMpCrShgsmiTransportCmdTermWriteAsync(&pDevExt->CrHgsmiTransport, pvCompletionData);
        }
        else
        {
            WARN(("NemuMpCrShgsmiTransportCmdCreateWriteAsync failed"));
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    return Status;
}

static NTSTATUS nemuVdmaVRegGet(PNEMUWDDM_SWAPCHAIN pSwapchain, const RTRECT *pCtxRect, uint32_t *pcVRects, RTRECT **ppVRectsBuff, uint32_t *pcVRectsBuff)
{
    RTRECT *pVRectsBuff = *ppVRectsBuff;
    uint32_t cVRectsBuff = *pcVRectsBuff;
    uint32_t cVRects = NemuVrListRectsCount(&pSwapchain->VisibleRegions);
    if (cVRectsBuff < cVRects)
    {
        if (pVRectsBuff)
            nemuWddmMemFree(pVRectsBuff);
        pVRectsBuff = (RTRECT*)nemuWddmMemAlloc(cVRects * sizeof (pVRectsBuff[0]));
        if (!pVRectsBuff)
        {
            WARN(("nemuWddmMemAlloc failed"));
            *pcVRectsBuff = 0;
            *ppVRectsBuff = NULL;
            *pcVRects = NULL;
            return STATUS_NO_MEMORY;
        }

        cVRectsBuff = cVRects;
        *pcVRectsBuff = cVRectsBuff;
        *ppVRectsBuff = pVRectsBuff;
    }

    int rc = NemuVrListRectsGet(&pSwapchain->VisibleRegions, cVRects, pVRectsBuff);
    AssertRC(rc);
    if (pCtxRect->xLeft || pCtxRect->yTop)
    {
        for (UINT i = 0; i < cVRects; ++i)
        {
            NemuRectTranslate(&pVRectsBuff[i], -pCtxRect->xLeft, -pCtxRect->yTop);
        }
    }

    *pcVRects = cVRects;
    return STATUS_SUCCESS;
}


/**
 * @param pDevExt
 */
static NTSTATUS nemuVdmaProcessVRegCmdLegacy(PNEMUMP_DEVEXT pDevExt,
        NEMUMP_CRPACKER *pCrPacker,
        uint32_t u32CrConClientID,
        PNEMUWDDM_SOURCE pSource,
        PNEMUWDDM_SWAPCHAIN pSwapchain,
        const RECT *pSrcRect,
        const NEMUVDMAPIPE_RECTS *pContextRects)
{
    NEMUVDMAPIPE_RECTS *pRectsToFree = NULL;
    POINT pos = pSource->VScreenPos;
    if (pos.x || pos.y)
    {
        pRectsToFree = nemuWddmBltPipeRectsDup(pContextRects);
        /* note: do NOT translate the src rect since it is used for screen pos calculation */
        nemuWddmBltPipeRectsTranslate(pRectsToFree, pos.x, pos.y);
        pContextRects = pRectsToFree;
    }
    const NEMUWDDM_RECTS_INFO *pRects = &pContextRects->UpdateRects;
    NTSTATUS Status = STATUS_SUCCESS;
    int rc;
    bool fCurChanged = FALSE, fCurRectChanged = FALSE;
    POINT CurPos;
    RTRECT *pVRectsBuff = NULL;
    uint32_t cVRectsBuff = 0;
    NEMUWDDM_CTXLOCK_DATA

    NEMUWDDM_CTXLOCK_LOCK(pDevExt);

    if (pSwapchain)
    {
        CurPos.x = pContextRects->ContextRect.left - pSrcRect->left;
        CurPos.y = pContextRects->ContextRect.top - pSrcRect->top;

        if (CurPos.x != pSwapchain->Pos.x || CurPos.y != pSwapchain->Pos.y)
        {
#if 0
            if (pSwapchain->Pos.x != NEMUWDDM_INVALID_COORD)
                NemuVrListTranslate(&pSwapchain->VisibleRegions, pSwapchain->Pos.x - CurPos.x, pSwapchain->Pos.y - CurPos.y);
            else
#endif
                NemuVrListClear(&pSwapchain->VisibleRegions);
            fCurRectChanged = TRUE;
            pSwapchain->Pos = CurPos;
        }

        rc = NemuVrListRectsAdd(&pSwapchain->VisibleRegions, pRects->cRects, (const RTRECT*)pRects->aRects, &fCurChanged);
        if (!RT_SUCCESS(rc))
        {
            WARN(("NemuWddmVrListRectsAdd failed, rc %d!", rc));
            Status = STATUS_UNSUCCESSFUL;
            goto done;
        }


        /* visible rects of different windows do not intersect,
         * so if the given window visible rects did not increase, others have not changed either */
        if (!fCurChanged && !fCurRectChanged)
            goto done;
    }

    /* before posting the add visible rects diff, we need to first hide rects for other windows */

    for (PLIST_ENTRY pCur = pDevExt->SwapchainList3D.Flink; pCur != &pDevExt->SwapchainList3D; pCur = pCur->Flink)
    {
        if (pCur != &pSwapchain->DevExtListEntry)
        {
            PNEMUWDDM_SWAPCHAIN pCurSwapchain = NEMUWDDMENTRY_2_SWAPCHAIN(pCur);
            PNEMUWDDM_CONTEXT pCurContext = pCurSwapchain->pContext;
            PNEMUMP_CRPACKER pCurPacker = &pCurContext->CrPacker;
            bool fChanged = FALSE;

            rc = NemuVrListRectsSubst(&pCurSwapchain->VisibleRegions, pRects->cRects, (const RTRECT*)pRects->aRects, &fChanged);
            if (!RT_SUCCESS(rc))
            {
                WARN(("NemuWddmVrListRectsAdd failed, rc %d!", rc));
                Status = STATUS_UNSUCCESSFUL;
                goto done;
            }

            if (!fChanged)
                continue;

            uint32_t cVRects;
            RTRECT CurCtxRect;
            CurCtxRect.xLeft = pCurSwapchain->Pos.x;
            CurCtxRect.yTop = pCurSwapchain->Pos.y;
            CurCtxRect.xRight = CurCtxRect.xLeft + pCurSwapchain->width;
            CurCtxRect.yBottom = CurCtxRect.yTop + pCurSwapchain->height;
            Status = nemuVdmaVRegGet(pCurSwapchain, &CurCtxRect, &cVRects, &pVRectsBuff, &cVRectsBuff);
            if (!NT_SUCCESS(Status))
            {
                WARN(("nemuVdmaVRegGet Status 0x%x", Status));
                goto done;
            }

            void *pvCommandBuffer = NULL;
            uint32_t cbCommandBuffer = NEMUMP_CRCMD_HEADER_SIZE;
            cbCommandBuffer += NEMUMP_CRCMD_SIZE_WINDOWVISIBLEREGIONS(cVRects);

            pvCommandBuffer = NemuMpCrShgsmiTransportBufAlloc(&pDevExt->CrHgsmiTransport, cbCommandBuffer);
            if (!pvCommandBuffer)
            {
                WARN(("NemuMpCrShgsmiTransportBufAlloc failed!"));
                Status = VERR_OUT_OF_RESOURCES;
                goto done;
            }

            NemuMpCrPackerTxBufferInit(pCurPacker, pvCommandBuffer, cbCommandBuffer, 1);

            Assert(pCurSwapchain->winHostID);
            crPackWindowVisibleRegion(&pCurPacker->CrPacker, pCurSwapchain->winHostID, cVRects, (GLint*)pVRectsBuff);

            Status = nemuVdmaCrSubmitWriteAsync(pDevExt, pCurPacker, pCurContext->u32CrConClientID);
            if (!NT_SUCCESS(Status))
            {
                WARN(("nemuVdmaCrSubmitWriteAsync failed Status 0x%x", Status));
                NemuMpCrShgsmiTransportBufFree(&pDevExt->CrHgsmiTransport, pvCommandBuffer);
            }
        }
    }

    if (!pSwapchain)
        goto done;

    uint32_t cbCommandBuffer = NEMUMP_CRCMD_HEADER_SIZE, cCommands = 0;

    uint32_t cVRects;
    Status = nemuVdmaVRegGet(pSwapchain, (const RTRECT *)&pContextRects->ContextRect, &cVRects, &pVRectsBuff, &cVRectsBuff);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaVRegGet Status 0x%x", Status));
        goto done;
    }

    ++cCommands;
    cbCommandBuffer += NEMUMP_CRCMD_SIZE_WINDOWVISIBLEREGIONS(cVRects);

    if (fCurRectChanged && fCurChanged)
    {
        ++cCommands;
        cbCommandBuffer += NEMUMP_CRCMD_SIZE_WINDOWPOSITION;
    }

    if (!pSwapchain->fExposed)
    {
        ++cCommands;
        cbCommandBuffer += NEMUMP_CRCMD_SIZE_WINDOWSHOW;
        ++cCommands;
        cbCommandBuffer += NEMUMP_CRCMD_SIZE_WINDOWSIZE;
    }

    void *pvCommandBuffer = NemuMpCrShgsmiTransportBufAlloc(&pDevExt->CrHgsmiTransport, cbCommandBuffer);
    if (!pvCommandBuffer)
    {
        WARN(("NemuMpCrShgsmiTransportBufAlloc failed!"));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    NemuMpCrPackerTxBufferInit(pCrPacker, pvCommandBuffer, cbCommandBuffer, cCommands);

    Assert(pSwapchain->winHostID);

    if (fCurRectChanged && fCurChanged)
        crPackWindowPosition(&pCrPacker->CrPacker, pSwapchain->winHostID, CurPos.x, CurPos.y);

    if (!pSwapchain->fExposed)
    {
        crPackWindowSize(&pCrPacker->CrPacker, pSwapchain->winHostID, pSwapchain->width, pSwapchain->height);
        crPackWindowShow(&pCrPacker->CrPacker, pSwapchain->winHostID, TRUE);
        pSwapchain->fExposed = TRUE;
    }

    crPackWindowVisibleRegion(&pCrPacker->CrPacker, pSwapchain->winHostID, cVRects, (GLint*)pVRectsBuff);

    Status = nemuVdmaCrSubmitWriteAsync(pDevExt, pCrPacker, u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrSubmitWriteAsync failed Status 0x%x", Status));
        NemuMpCrShgsmiTransportBufFree(&pDevExt->CrHgsmiTransport, pvCommandBuffer);
    }

done:
    NEMUWDDM_CTXLOCK_UNLOCK(pDevExt);

    if (pRectsToFree)
        nemuWddmMemFree(pRectsToFree);

    if (pVRectsBuff)
        nemuWddmMemFree(pVRectsBuff);

    return Status;
}
#endif

static NTSTATUS nemuVdmaGgDmaColorFill(PNEMUMP_DEVEXT pDevExt, NEMUVDMA_CLRFILL *pCF)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    Assert (pDevExt->pvVisibleVram);
    if (pDevExt->pvVisibleVram)
    {
        PNEMUWDDM_ALLOCATION pAlloc = pCF->Alloc.pAlloc;
        if (pAlloc->AllocData.Addr.SegmentId && pAlloc->AllocData.Addr.SegmentId != 1)
        {
            WARN(("request to collor fill invalid allocation"));
            return STATUS_INVALID_PARAMETER;
        }

        NEMUVIDEOOFFSET offVram = nemuWddmAddrFramOffset(&pAlloc->AllocData.Addr);
        if (offVram != NEMUVIDEOOFFSET_VOID)
        {
            RECT UnionRect = {0};
            uint8_t *pvMem = pDevExt->pvVisibleVram + offVram;
            UINT bpp = pAlloc->AllocData.SurfDesc.bpp;
            Assert(bpp);
            Assert(((bpp * pAlloc->AllocData.SurfDesc.width) >> 3) == pAlloc->AllocData.SurfDesc.pitch);
            switch (bpp)
            {
                case 32:
                {
                    uint8_t bytestPP = bpp >> 3;
                    for (UINT i = 0; i < pCF->Rects.cRects; ++i)
                    {
                        RECT *pRect = &pCF->Rects.aRects[i];
                        for (LONG ir = pRect->top; ir < pRect->bottom; ++ir)
                        {
                            uint32_t * pvU32Mem = (uint32_t*)(pvMem + (ir * pAlloc->AllocData.SurfDesc.pitch) + (pRect->left * bytestPP));
                            uint32_t cRaw = pRect->right - pRect->left;
                            Assert(pRect->left >= 0);
                            Assert(pRect->right <= (LONG)pAlloc->AllocData.SurfDesc.width);
                            Assert(pRect->top >= 0);
                            Assert(pRect->bottom <= (LONG)pAlloc->AllocData.SurfDesc.height);
                            for (UINT j = 0; j < cRaw; ++j)
                            {
                                *pvU32Mem = pCF->Color;
                                ++pvU32Mem;
                            }
                        }
                        nemuWddmRectUnited(&UnionRect, &UnionRect, pRect);
                    }
                    Status = STATUS_SUCCESS;
                    break;
                }
                case 16:
                case 8:
                default:
                    AssertBreakpoint();
                    break;
            }

            if (Status == STATUS_SUCCESS)
            {
                if (pAlloc->AllocData.SurfDesc.VidPnSourceId != D3DDDI_ID_UNINITIALIZED
                        && NEMUWDDM_IS_FB_ALLOCATION(pDevExt, pAlloc)
                        && pAlloc->bVisible
                        )
                {
                    if (!nemuWddmRectIsEmpty(&UnionRect))
                    {
                        PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pCF->Alloc.pAlloc->AllocData.SurfDesc.VidPnSourceId];
                        uint32_t cUnlockedVBVADisabled = ASMAtomicReadU32(&pDevExt->cUnlockedVBVADisabled);
                        if (!cUnlockedVBVADisabled)
                        {
                            NEMUVBVA_OP(ReportDirtyRect, pDevExt, pSource, &UnionRect);
                        }
                        else
                        {
                            NEMUVBVA_OP_WITHLOCK(ReportDirtyRect, pDevExt, pSource, &UnionRect);
                        }
                    }
                }
                else
                {
                    AssertBreakpoint();
                }
            }
        }
        else
            WARN(("invalid offVram"));
    }

    return Status;
}

NTSTATUS nemuVdmaGgDmaBltPerform(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOC_DATA pSrcAlloc, RECT* pSrcRect,
        PNEMUWDDM_ALLOC_DATA pDstAlloc, RECT* pDstRect)
{
    uint8_t* pvVramBase = pDevExt->pvVisibleVram;
    /* we do not support stretching */
    uint32_t srcWidth = pSrcRect->right - pSrcRect->left;
    uint32_t srcHeight = pSrcRect->bottom - pSrcRect->top;
    uint32_t dstWidth = pDstRect->right - pDstRect->left;
    uint32_t dstHeight = pDstRect->bottom - pDstRect->top;
    Assert(srcHeight == dstHeight);
    Assert(dstWidth == srcWidth);
    Assert(pDstAlloc->Addr.offVram != NEMUVIDEOOFFSET_VOID);
    Assert(pSrcAlloc->Addr.offVram != NEMUVIDEOOFFSET_VOID);
    D3DDDIFORMAT enmSrcFormat, enmDstFormat;

    enmSrcFormat = pSrcAlloc->SurfDesc.format;
    enmDstFormat = pDstAlloc->SurfDesc.format;

    if (pDstAlloc->Addr.SegmentId && pDstAlloc->Addr.SegmentId != 1)
    {
        WARN(("request to collor blit invalid allocation"));
        return STATUS_INVALID_PARAMETER;
    }

    if (pSrcAlloc->Addr.SegmentId && pSrcAlloc->Addr.SegmentId != 1)
    {
        WARN(("request to collor blit invalid allocation"));
        return STATUS_INVALID_PARAMETER;
    }

    if (enmSrcFormat != enmDstFormat)
    {
        /* just ignore the alpha component
         * this is ok since our software-based stuff can not handle alpha channel in any way */
        enmSrcFormat = nemuWddmFmtNoAlphaFormat(enmSrcFormat);
        enmDstFormat = nemuWddmFmtNoAlphaFormat(enmDstFormat);
        if (enmSrcFormat != enmDstFormat)
        {
            WARN(("color conversion src(%d), dst(%d) not supported!", pSrcAlloc->SurfDesc.format, pDstAlloc->SurfDesc.format));
            return STATUS_INVALID_PARAMETER;
        }
    }
    if (srcHeight != dstHeight)
            return STATUS_INVALID_PARAMETER;
    if (srcWidth != dstWidth)
            return STATUS_INVALID_PARAMETER;
    if (pDstAlloc->Addr.offVram == NEMUVIDEOOFFSET_VOID)
        return STATUS_INVALID_PARAMETER;
    if (pSrcAlloc->Addr.offVram == NEMUVIDEOOFFSET_VOID)
        return STATUS_INVALID_PARAMETER;

    uint8_t *pvDstSurf = pDstAlloc->Addr.SegmentId ? pvVramBase + pDstAlloc->Addr.offVram : (uint8_t*)pDstAlloc->Addr.pvMem;
    uint8_t *pvSrcSurf = pSrcAlloc->Addr.SegmentId ? pvVramBase + pSrcAlloc->Addr.offVram : (uint8_t*)pSrcAlloc->Addr.pvMem;

    if (pDstAlloc->SurfDesc.width == dstWidth
            && pSrcAlloc->SurfDesc.width == srcWidth
            && pSrcAlloc->SurfDesc.width == pDstAlloc->SurfDesc.width)
    {
        Assert(!pDstRect->left);
        Assert(!pSrcRect->left);
        uint32_t cbDstOff = nemuWddmCalcOffXYrd(0 /* x */, pDstRect->top, pDstAlloc->SurfDesc.pitch, pDstAlloc->SurfDesc.format);
        uint32_t cbSrcOff = nemuWddmCalcOffXYrd(0 /* x */, pSrcRect->top, pSrcAlloc->SurfDesc.pitch, pSrcAlloc->SurfDesc.format);
        uint32_t cbSize = nemuWddmCalcSize(pDstAlloc->SurfDesc.pitch, dstHeight, pDstAlloc->SurfDesc.format);
        memcpy(pvDstSurf + cbDstOff, pvSrcSurf + cbSrcOff, cbSize);
    }
    else
    {
        uint32_t cbDstLine =  nemuWddmCalcRowSize(pDstRect->left, pDstRect->right, pDstAlloc->SurfDesc.format);
        uint32_t offDstStart = nemuWddmCalcOffXYrd(pDstRect->left, pDstRect->top, pDstAlloc->SurfDesc.pitch, pDstAlloc->SurfDesc.format);
        Assert(cbDstLine <= pDstAlloc->SurfDesc.pitch);
        uint32_t cbDstSkip = pDstAlloc->SurfDesc.pitch;
        uint8_t * pvDstStart = pvDstSurf + offDstStart;

        uint32_t cbSrcLine = nemuWddmCalcRowSize(pSrcRect->left, pSrcRect->right, pSrcAlloc->SurfDesc.format);
        uint32_t offSrcStart = nemuWddmCalcOffXYrd(pSrcRect->left, pSrcRect->top, pSrcAlloc->SurfDesc.pitch, pSrcAlloc->SurfDesc.format);
        Assert(cbSrcLine <= pSrcAlloc->SurfDesc.pitch);
        uint32_t cbSrcSkip = pSrcAlloc->SurfDesc.pitch;
        const uint8_t * pvSrcStart = pvSrcSurf + offSrcStart;

        uint32_t cRows = nemuWddmCalcNumRows(pDstRect->top, pDstRect->bottom, pDstAlloc->SurfDesc.format);

        Assert(cbDstLine == cbSrcLine);

        for (uint32_t i = 0; i < cRows; ++i)
        {
            memcpy(pvDstStart, pvSrcStart, cbDstLine);
            pvDstStart += cbDstSkip;
            pvSrcStart += cbSrcSkip;
        }
    }
    return STATUS_SUCCESS;
}

/*
 * @return on success the number of bytes the command contained, otherwise - VERR_xxx error code
 */
static NTSTATUS nemuVdmaGgDmaBlt(PNEMUMP_DEVEXT pDevExt, PNEMUVDMA_BLT pBlt)
{
    /* we do not support stretching for now */
    Assert(pBlt->SrcRect.right - pBlt->SrcRect.left == pBlt->DstRects.ContextRect.right - pBlt->DstRects.ContextRect.left);
    Assert(pBlt->SrcRect.bottom - pBlt->SrcRect.top == pBlt->DstRects.ContextRect.bottom - pBlt->DstRects.ContextRect.top);
    if (pBlt->SrcRect.right - pBlt->SrcRect.left != pBlt->DstRects.ContextRect.right - pBlt->DstRects.ContextRect.left)
        return STATUS_INVALID_PARAMETER;
    if (pBlt->SrcRect.bottom - pBlt->SrcRect.top != pBlt->DstRects.ContextRect.bottom - pBlt->DstRects.ContextRect.top)
        return STATUS_INVALID_PARAMETER;
    Assert(pBlt->DstRects.UpdateRects.cRects);

    NTSTATUS Status = STATUS_SUCCESS;

    if (pBlt->DstRects.UpdateRects.cRects)
    {
        for (uint32_t i = 0; i < pBlt->DstRects.UpdateRects.cRects; ++i)
        {
            RECT SrcRect;
            nemuWddmRectTranslated(&SrcRect, &pBlt->DstRects.UpdateRects.aRects[i], -pBlt->DstRects.ContextRect.left, -pBlt->DstRects.ContextRect.top);

            Status = nemuVdmaGgDmaBltPerform(pDevExt, &pBlt->SrcAlloc.pAlloc->AllocData, &SrcRect,
                    &pBlt->DstAlloc.pAlloc->AllocData, &pBlt->DstRects.UpdateRects.aRects[i]);
            Assert(Status == STATUS_SUCCESS);
            if (Status != STATUS_SUCCESS)
                return Status;
        }
    }
    else
    {
        Status = nemuVdmaGgDmaBltPerform(pDevExt, &pBlt->SrcAlloc.pAlloc->AllocData, &pBlt->SrcRect,
                &pBlt->DstAlloc.pAlloc->AllocData, &pBlt->DstRects.ContextRect);
        Assert(Status == STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
            return Status;
    }

    return Status;
}

#ifdef NEMU_WITH_CROGL
typedef struct NEMUVDMA_CRRXGENERICSYNC
{
    int rc;
    KEVENT Event;
} NEMUVDMA_CRRXGENERICSYNC, *PNEMUVDMA_CRRXGENERICSYNC;

static DECLCALLBACK(void) nemuVdmaCrRxGenericSyncCompletion(PNEMUMP_CRSHGSMITRANSPORT pCon, int rc, void *pvRx, uint32_t cbRx, void *pvCtx)
{
    PNEMUMP_VDMACR_WRITEREADCOMPLETION pvCompletionData = (PNEMUMP_VDMACR_WRITEREADCOMPLETION)pvCtx;
    PNEMUVDMA_CRRXGENERICSYNC pData = (PNEMUVDMA_CRRXGENERICSYNC)pvCompletionData->pvContext;
    if (RT_SUCCESS(rc))
    {
        rc = NemuMpCrCmdRxHandler((CRMessageHeader*)pvRx, cbRx);
        if (!RT_SUCCESS(rc))
        {
            WARN(("NemuMpCrCmdRxHandler failed %d", rc));
        }
    }
    else
    {
        WARN(("rx failure %d", rc));
    }

    pData->rc = rc;

    KeSetEvent(&pData->Event, 0, FALSE);

    nemuVdmaCrSubmitWriteReadAsyncGenericCompletion(pCon, pvCtx);
}

NTSTATUS nemuVdmaCrRxGenericSync(PNEMUMP_DEVEXT pDevExt, NEMUMP_CRPACKER *pCrPacker, uint32_t u32CrConClientID)
{
    NEMUVDMA_CRRXGENERICSYNC Data;
    Data.rc = VERR_NOT_SUPPORTED;
    KeInitializeEvent(&Data.Event, SynchronizationEvent, FALSE);
    NTSTATUS Status = nemuVdmaCrSubmitWriteReadAsync(pDevExt, pCrPacker, u32CrConClientID, nemuVdmaCrRxGenericSyncCompletion, &Data);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrSubmitWriteAsync failed Status 0x%x", Status));
        return Status;
    }

     Status = KeWaitForSingleObject(&Data.Event, Executive, KernelMode, FALSE, NULL /* PLARGE_INTEGER Timeout */);
     if (!NT_SUCCESS(Status))
     {
         WARN(("KeWaitForSingleObject failed Status 0x%x", Status));
         return Status;
     }

     return STATUS_SUCCESS;
}

typedef struct NEMUMP_CRHGSMIMGR
{
    NEMUMP_CRPACKER CrPacker;
    void *pvCommandBuffer;
} NEMUMP_CRHGSMIMGR;

DECLINLINE(CRPackContext*) nemuVdmaCrHmGetPackContext(NEMUMP_CRHGSMIMGR *pMgr)
{
    return &pMgr->CrPacker.CrPacker;
}

NTSTATUS nemuVdmaCrHmCreate(PNEMUMP_DEVEXT pDevExt, NEMUMP_CRHGSMIMGR *pMgr, uint32_t cbCommandBuffer, uint32_t cCommands)
{
    pMgr->pvCommandBuffer = NemuMpCrShgsmiTransportBufAlloc(&pDevExt->CrHgsmiTransport, cbCommandBuffer);
    if (!pMgr->pvCommandBuffer)
    {
        WARN(("NemuMpCrShgsmiTransportBufAlloc failed!"));
        return VERR_OUT_OF_RESOURCES;
    }

    NemuMpCrPackerInit(&pMgr->CrPacker);

    NemuMpCrPackerTxBufferInit(&pMgr->CrPacker, pMgr->pvCommandBuffer, cbCommandBuffer, cCommands);

    return STATUS_SUCCESS;
}

NTSTATUS nemuVdmaCrHmSubmitWrSync(PNEMUMP_DEVEXT pDevExt, NEMUMP_CRHGSMIMGR *pMgr, uint32_t u32CrConClientID)
{
    NTSTATUS Status = nemuVdmaCrRxGenericSync(pDevExt, &pMgr->CrPacker, u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrRxGenericSync failed Status 0x%x", Status));
        NemuMpCrShgsmiTransportBufFree(&pDevExt->CrHgsmiTransport, pMgr->pvCommandBuffer);
        return Status;
    }

    return STATUS_SUCCESS;
}
#if 0
NTSTATUS nemuVdmaCrCmdGetChromiumParametervCR(PNEMUMP_DEVEXT pDevExt, uint32_t u32CrConClientID, GLenum target, GLuint index, GLenum type, GLsizei count, GLvoid * values)
{
    uint32_t cbCommandBuffer = NEMUMP_CRCMD_HEADER_SIZE + NEMUMP_CRCMD_SIZE_GETCHROMIUMPARAMETERVCR;
    uint32_t cCommands = 1;

    NEMUMP_CRHGSMIMGR Mgr;
    NTSTATUS Status = nemuVdmaCrHmCreate(pDevExt, &Mgr, cbCommandBuffer, cCommands);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrHmCreate failed Status 0x%x", Status));
        return Status;
    }

    int dummy = 1;

    crPackGetChromiumParametervCR(nemuVdmaCrHmGetPackContext(&Mgr), target, index, type, count, values, &dummy);

    Status = nemuVdmaCrHmSubmitWrSync(pDevExt, &Mgr, u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrHmSubmitWrSync failed Status 0x%x", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS nemuVdmaCrCmdCreateContext(PNEMUMP_DEVEXT pDevExt, int32_t visualBits, int32_t *pi32CtxID)
{
    uint32_t cbCommandBuffer = NEMUMP_CRCMD_HEADER_SIZE + NEMUMP_CRCMD_SIZE_CREATECONTEXT;
    uint32_t cCommands = 1;

    NEMUMP_CRHGSMIMGR Mgr;
    NTSTATUS Status = nemuVdmaCrHmCreate(pDevExt, &Mgr, cbCommandBuffer, cCommands);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrHmCreate failed Status 0x%x", Status));
        return Status;
    }

    int dummy = 1;

    crPackCreateContext(&CrPacker.CrPacker, "", visualBits, 0, &pi32CtxID, &dummy);

    Status = nemuVdmaCrHmSubmitWrSync(pDevExt, &Mgr, u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrHmSubmitWrSync failed Status 0x%x", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS nemuVdmaCrCmdWindowCreate(PNEMUMP_DEVEXT pDevExt, int32_t visualBits, int32_t *pi32WinID)
{
    uint32_t cbCommandBuffer = NEMUMP_CRCMD_HEADER_SIZE + NEMUMP_CRCMD_SIZE_WINDOWCREATE;
    uint32_t cCommands = 1;

    NEMUMP_CRHGSMIMGR Mgr;
    NTSTATUS Status = nemuVdmaCrHmCreate(pDevExt, &Mgr, cbCommandBuffer, cCommands);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrHmCreate failed Status 0x%x", Status));
        return Status;
    }

    int dummy = 1;

    crPackWindowCreate(&CrPacker.CrPacker, "", visualBits, 0, &pi32CtxID, &dummy);

    Status = nemuVdmaCrHmSubmitWrSync(pDevExt, &Mgr, u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrHmSubmitWrSync failed Status 0x%x", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS nemuVdmaCrCtlGetDefaultCtxId(PNEMUMP_DEVEXT pDevExt, int32_t *pi32CtxID)
{
    if (!pDevExt->i32CrConDefaultCtxID)
    {
        if (!pDevExt->f3DEnabled)
        {
            WARN(("3D disabled, should not be here!"));
            return STATUS_UNSUCCESSFUL;
        }

        uint32_t u32ClienID;
        NTSTATUS Status = nemuVdmaCrCtlGetDefaultClientId(pDevExt, &u32ClienID);
        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVdmaCrCtlGetDefaultClientId failed, Status %#x", Status));
            return Status;
        }

        Status = nemuVdmaCrCmdWindowCreate(PNEMUMP_DEVEXT pDevExt, int32_t visualBits, int32_t *pi32WinID)

        NEMUMP_CRPACKER CrPacker;
        NemuMpCrPackerInit(&CrPacker);

        int rc = NemuMpCrCtlConConnect(pDevExt, &pDevExt->CrCtlCon, CR_PROTOCOL_VERSION_MAJOR, CR_PROTOCOL_VERSION_MINOR, &pDevExt->u32CrConDefaultClientID);
        if (!RT_SUCCESS(rc))
        {
            WARN(("NemuMpCrCtlConConnect failed, rc %d", rc));
            return STATUS_UNSUCCESSFUL;
        }
    }

    *pi32CtxID = pDevExt->i32CrConDefaultCtxID;
    return STATUS_SUCCESS;
}
#endif

static NTSTATUS nemuVdmaTexPresentSubmit(PNEMUMP_DEVEXT pDevExt,
        NEMUMP_CRPACKER *pCrPacker,
        uint32_t u32CrConClientID,
        uint32_t hostID,
        uint32_t cfg,
        int32_t posX,
        int32_t posY,
        uint32_t cRects,
        const RTRECT*paRects)
{
    Assert(pDevExt->fTexPresentEnabled);

    uint32_t cbCommandBuffer = NEMUMP_CRCMD_HEADER_SIZE + NEMUMP_CRCMD_SIZE_NEMUTEXPRESENT(cRects);
    uint32_t cCommands = 1;
    void *pvCommandBuffer = NemuMpCrShgsmiTransportBufAlloc(&pDevExt->CrHgsmiTransport, cbCommandBuffer);
    if (!pvCommandBuffer)
    {
        WARN(("NemuMpCrShgsmiTransportBufAlloc failed!"));
        return VERR_OUT_OF_RESOURCES;
    }

    NemuMpCrPackerTxBufferInit(pCrPacker, pvCommandBuffer, cbCommandBuffer, cCommands);

    crPackNemuTexPresent(&pCrPacker->CrPacker, hostID, cfg, posX, posY, cRects, (int32_t*)paRects);

    NTSTATUS Status = nemuVdmaCrSubmitWriteAsync(pDevExt, pCrPacker, u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrSubmitWriteAsync failed Status 0x%x", Status));
        NemuMpCrShgsmiTransportBufFree(&pDevExt->CrHgsmiTransport, pvCommandBuffer);
    }

    return Status;
}

static NTSTATUS nemuVdmaChromiumParameteriCRSubmit(PNEMUMP_DEVEXT pDevExt,
        NEMUMP_CRPACKER *pCrPacker,
        uint32_t u32CrConClientID,
        uint32_t target, uint32_t value)
{
    Assert(pDevExt->fTexPresentEnabled);

    uint32_t cbCommandBuffer = NEMUMP_CRCMD_HEADER_SIZE + NEMUMP_CRCMD_SIZE_CHROMIUMPARAMETERICR;
    uint32_t cCommands = 1;
    void *pvCommandBuffer = NemuMpCrShgsmiTransportBufAlloc(&pDevExt->CrHgsmiTransport, cbCommandBuffer);
    if (!pvCommandBuffer)
    {
        WARN(("NemuMpCrShgsmiTransportBufAlloc failed!"));
        return VERR_OUT_OF_RESOURCES;
    }

    NemuMpCrPackerTxBufferInit(pCrPacker, pvCommandBuffer, cbCommandBuffer, cCommands);

    crPackChromiumParameteriCR(&pCrPacker->CrPacker, target, value);

    NTSTATUS Status = nemuVdmaCrSubmitWriteAsync(pDevExt, pCrPacker, u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrSubmitWriteAsync failed Status 0x%x", Status));
        NemuMpCrShgsmiTransportBufFree(&pDevExt->CrHgsmiTransport, pvCommandBuffer);
    }

    return Status;
}

NTSTATUS NemuVdmaChromiumParameteriCRSubmit(PNEMUMP_DEVEXT pDevExt, uint32_t target, uint32_t value)
{
    uint32_t u32CrConClientID;
    NTSTATUS Status = nemuVdmaCrCtlGetDefaultClientId(pDevExt, &u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrCtlGetDefaultClientId failed Status 0x%x", Status));
        return Status;
    }

    NEMUMP_CRPACKER *pCrPacker = (NEMUMP_CRPACKER *)RTMemTmpAlloc(sizeof(*pCrPacker));
    if (!pCrPacker)
        return STATUS_NO_MEMORY;
    NemuMpCrPackerInit(pCrPacker);

    Status = nemuVdmaChromiumParameteriCRSubmit(pDevExt, pCrPacker, u32CrConClientID, target, value);
    if (!NT_SUCCESS(Status))
        WARN(("nemuVdmaChromiumParameteriCRSubmit failed Status 0x%x", Status));

    RTMemTmpFree(pCrPacker);
    return Status;
}

static NTSTATUS nemuVdmaCrCtlGetDefaultClientId(PNEMUMP_DEVEXT pDevExt, uint32_t *pu32ClienID)
{
    if (!pDevExt->u32CrConDefaultClientID)
    {
        if (!pDevExt->f3DEnabled)
        {
            WARN(("3D disabled, should not be here!"));
            return STATUS_UNSUCCESSFUL;
        }

        int rc = NemuMpCrCtlConConnect(pDevExt, &pDevExt->CrCtlCon, CR_PROTOCOL_VERSION_MAJOR, CR_PROTOCOL_VERSION_MINOR, &pDevExt->u32CrConDefaultClientID);
        if (!RT_SUCCESS(rc))
        {
            WARN(("NemuMpCrCtlConConnect failed, rc %d", rc));
            return STATUS_UNSUCCESSFUL;
        }
    }

    *pu32ClienID = pDevExt->u32CrConDefaultClientID;
    return STATUS_SUCCESS;
}


static NTSTATUS nemuVdmaProcessVRegTexPresent(PNEMUMP_DEVEXT pDevExt,
        NEMUMP_CRPACKER *pCrPacker,
        uint32_t u32CrConClientID,
        const NEMUWDDM_ALLOC_DATA *pSrcAllocData,
        const NEMUWDDM_ALLOC_DATA *pDstAllocData,
        const RECT *pSrcRect, const NEMUVDMAPIPE_RECTS *pDstRects)
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId = pDstAllocData->SurfDesc.VidPnSourceId;
    if (srcId >= (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays)
    {
        WARN(("invalid srcId"));
        return STATUS_NOT_SUPPORTED;
    }

    NEMUWDDM_SOURCE *pSource = &pDevExt->aSources[srcId];

    bool fPrimary = nemuWddmAddrVramEqual(&pDstAllocData->Addr, &pSource->AllocData.Addr);
    /* we care only about screen regions */
    if (!fPrimary)
    {
        WARN(("non-primary allocation passed to nemuWddmSubmitBltCmd!"));
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t hostID = pSrcAllocData->hostID;
    int rc;
    if (hostID)
    {
//            Assert(pContext->enmType == NEMUWDDM_CONTEXT_TYPE_CUSTOM_3D);
        int32_t posX = pDstRects->ContextRect.left - pSrcRect->left;
        int32_t posY = pDstRects->ContextRect.top - pSrcRect->top;

        Status = nemuVdmaTexPresentSubmit(pDevExt, pCrPacker, u32CrConClientID, hostID, srcId, posX, posY, pDstRects->UpdateRects.cRects, (const RTRECT*)pDstRects->UpdateRects.aRects);
        if (NT_SUCCESS(Status))
        {
            rc = NemuVrListRectsSubst(&pSource->VrList, pDstRects->UpdateRects.cRects, (const RTRECT*)pDstRects->UpdateRects.aRects, NULL);
            if (RT_SUCCESS(rc))
                pSource->fHas3DVrs = TRUE;
            else
                WARN(("NemuVrListRectsSubst failed rc %d, ignoring..", rc));
        }
        else
            WARN(("nemuVdmaTexPresentSubmit failed Status 0x%x", Status));
    }
    else
    {
        Assert(fPrimary);

        bool fChanged = false;
        Assert(pSource->pPrimaryAllocation->bVisible);
        rc = NemuVrListRectsAdd(&pSource->VrList, pDstRects->UpdateRects.cRects, (const RTRECT*)pDstRects->UpdateRects.aRects, &fChanged);
        if (RT_SUCCESS(rc))
        {
            if (fChanged)
            {
                Status = nemuVdmaTexPresentSubmit(pDevExt, pCrPacker, u32CrConClientID, hostID, srcId, 0, 0, pDstRects->UpdateRects.cRects, (const RTRECT*)pDstRects->UpdateRects.aRects);
                if (NT_SUCCESS(Status))
                {
                    if (pSource->fHas3DVrs)
                    {
                        if (NemuVrListRectsCount(&pSource->VrList) == 1)
                        {
                            RTRECT Rect;
                            NemuVrListRectsGet(&pSource->VrList, 1, &Rect);
                            if (Rect.xLeft == 0
                                    && Rect.yTop == 0
                                    && Rect.xRight == pDstAllocData->SurfDesc.width
                                    && Rect.yBottom == pDstAllocData->SurfDesc.height)
                            {
                                pSource->fHas3DVrs = FALSE;
                            }
                        }
                    }
                }
                else
                    WARN(("nemuVdmaTexPresentSubmit failed Status 0x%x", Status));
            }
        }
        else
            WARN(("NemuVrListRectsAdd failed rc %d, ignoring..", rc));
    }

    return Status;
}

static NTSTATUS nemuVdmaProcessVReg(PNEMUMP_DEVEXT pDevExt,
        NEMUMP_CRPACKER *pCrPacker,
        uint32_t u32CrConClientID,
        const NEMUWDDM_ALLOCATION *pSrcAlloc,
        const NEMUWDDM_ALLOCATION *pDstAlloc,
        const RECT *pSrcRect, const NEMUVDMAPIPE_RECTS *pDstRects)
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId = pDstAlloc->AllocData.SurfDesc.VidPnSourceId;
    NEMUWDDM_SOURCE *pSource = &pDevExt->aSources[srcId];
    NTSTATUS Status = STATUS_SUCCESS;

    if (pDevExt->fTexPresentEnabled)
    {
        Status = nemuVdmaProcessVRegTexPresent(pDevExt, pCrPacker, u32CrConClientID,
                &pSrcAlloc->AllocData, &pDstAlloc->AllocData,
                pSrcRect, pDstRects);
    }
    else
    {
        PNEMUWDDM_SWAPCHAIN pSwapchain = nemuWddmSwapchainRetainByAlloc(pDevExt, pSrcAlloc);

        if (pSwapchain)
        {
            Assert(pSrcAlloc->AllocData.SurfDesc.width == pSwapchain->width);
            Assert(pSrcAlloc->AllocData.SurfDesc.height == pSwapchain->height);
        }

        Status = nemuVdmaProcessVRegCmdLegacy(pDevExt, pCrPacker, u32CrConClientID, pSource, pSwapchain, pSrcRect, pDstRects);
        if (!NT_SUCCESS(Status))
            WARN(("nemuVdmaProcessVRegCmdLegacy failed Status 0x%x", Status));

        if (pSwapchain)
            nemuWddmSwapchainRelease(pSwapchain);
    }

    return Status;
}

NTSTATUS nemuVdmaTexPresentSetAlloc(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData)
{
    uint32_t u32CrConClientID;
    NTSTATUS Status = nemuVdmaCrCtlGetDefaultClientId(pDevExt, &u32CrConClientID);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVdmaCrCtlGetDefaultClientId failed Status 0x%x", Status));
        return Status;
    }

    NEMUMP_CRPACKER *pCrPacker = (NEMUMP_CRPACKER *)RTMemTmpAlloc(sizeof(*pCrPacker));
    if (!pCrPacker)
        return STATUS_NO_MEMORY;
    NemuMpCrPackerInit(pCrPacker);

    RECT Rect;
    Rect.left = 0;
    Rect.top = 0;
    Rect.right = pAllocData->SurfDesc.width;
    Rect.bottom = pAllocData->SurfDesc.height;

    if (pDevExt->fCmdVbvaEnabled)
        Status = nemuVdmaTexPresentSubmit(pDevExt, pCrPacker, u32CrConClientID, pAllocData->hostID, pAllocData->SurfDesc.VidPnSourceId, 0, 0, 1, (RTRECT*)&Rect);
    else if (pDevExt->fTexPresentEnabled)
    {
        NEMUVDMAPIPE_RECTS RectInfo;
        RectInfo.ContextRect = Rect;
        RectInfo.UpdateRects.cRects = 1;
        RectInfo.UpdateRects.aRects[0] = Rect;

        Status = nemuVdmaProcessVRegTexPresent(pDevExt, pCrPacker, u32CrConClientID,
                pAllocData, pAllocData,
                &Rect, &RectInfo);
    }
    else
        Status = STATUS_NOT_IMPLEMENTED;

    RTMemTmpFree(pCrPacker);
    return Status;
}

static NTSTATUS nemuVdmaProcessVRegCmd(PNEMUMP_DEVEXT pDevExt, NEMUWDDM_CONTEXT *pContext,
        const NEMUWDDM_DMA_ALLOCINFO *pSrcAllocInfo,
        const NEMUWDDM_DMA_ALLOCINFO *pDstAllocInfo,
        const RECT *pSrcRect, const NEMUVDMAPIPE_RECTS *pDstRects)
{
    return nemuVdmaProcessVReg(pDevExt, &pContext->CrPacker, pContext->u32CrConClientID,
            pSrcAllocInfo->pAlloc, pDstAllocInfo->pAlloc,
            pSrcRect, pDstRects);
}

static void nemuVdmaBltDirtyRectsUpdate(PNEMUMP_DEVEXT pDevExt, NEMUWDDM_SOURCE *pSource, uint32_t cRects, const RECT *paRects)
{
    if (!cRects)
    {
        WARN(("nemuVdmaBltDirtyRectsUpdate: no rects specified"));
        return;
    }

    RECT rect;
    rect = paRects[0];
    for (UINT i = 1; i < cRects; ++i)
    {
        nemuWddmRectUnited(&rect, &rect, &paRects[i]);
    }

    uint32_t cUnlockedVBVADisabled = ASMAtomicReadU32(&pDevExt->cUnlockedVBVADisabled);
    if (!cUnlockedVBVADisabled)
    {
        NEMUVBVA_OP(ReportDirtyRect, pDevExt, pSource, &rect);
    }
    else
    {
        NEMUVBVA_OP_WITHLOCK_ATDPC(ReportDirtyRect, pDevExt, pSource, &rect);
    }
}
#endif

NTSTATUS nemuVdmaProcessBltCmd(PNEMUMP_DEVEXT pDevExt, NEMUWDDM_CONTEXT *pContext, NEMUWDDM_DMA_PRIVATEDATA_BLT *pBlt)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_ALLOCATION pDstAlloc = pBlt->Blt.DstAlloc.pAlloc;
    PNEMUWDDM_ALLOCATION pSrcAlloc = pBlt->Blt.SrcAlloc.pAlloc;
    BOOLEAN fVRAMUpdated = FALSE;
#ifdef NEMU_WITH_CROGL
    if (!pDstAlloc->AllocData.hostID && !pSrcAlloc->AllocData.hostID)
#endif
    {
        /* the allocations contain a real data in VRAM, do blitting */
        nemuVdmaGgDmaBlt(pDevExt, &pBlt->Blt);
        fVRAMUpdated = TRUE;
    }

#ifdef NEMU_WITH_CROGL
    if (NEMUWDDM_IS_FB_ALLOCATION(pDevExt, pDstAlloc)
            && pDstAlloc->bVisible)
    {
        NEMUWDDM_SOURCE *pSource = &pDevExt->aSources[pDstAlloc->AllocData.SurfDesc.VidPnSourceId];
        Assert(pDstAlloc->AllocData.SurfDesc.VidPnSourceId < NEMU_VIDEO_MAX_SCREENS);
        Assert(pSource->pPrimaryAllocation == pDstAlloc);

        if (fVRAMUpdated)
            nemuVdmaBltDirtyRectsUpdate(pDevExt, pSource, pBlt->Blt.DstRects.UpdateRects.cRects, pBlt->Blt.DstRects.UpdateRects.aRects);

        if (pSrcAlloc->AllocData.hostID || (pDevExt->fTexPresentEnabled ? pSource->fHas3DVrs : !!pDevExt->cContexts3D))
        {
            Status = nemuVdmaProcessVRegCmd(pDevExt, pContext, &pBlt->Blt.SrcAlloc, &pBlt->Blt.DstAlloc, &pBlt->Blt.SrcRect, &pBlt->Blt.DstRects);
            if (!NT_SUCCESS(Status))
                WARN(("nemuVdmaProcessVRegCmd failed Status 0x%x", Status));
        }
    }
#endif
    return Status;
}

NTSTATUS nemuVdmaProcessFlipCmd(PNEMUMP_DEVEXT pDevExt, NEMUWDDM_CONTEXT *pContext, NEMUWDDM_DMA_PRIVATEDATA_FLIP *pFlip)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_ALLOCATION pAlloc = pFlip->Flip.Alloc.pAlloc;
    NEMUWDDM_SOURCE *pSource = &pDevExt->aSources[pAlloc->AllocData.SurfDesc.VidPnSourceId];
    nemuWddmAssignPrimary(pSource, pAlloc, pAlloc->AllocData.SurfDesc.VidPnSourceId);
#ifdef NEMU_WITH_CROGL
    if (pAlloc->AllocData.hostID)
    {
        RECT SrcRect;
        NEMUVDMAPIPE_RECTS Rects;
        SrcRect.left = 0;
        SrcRect.top = 0;
        SrcRect.right = pAlloc->AllocData.SurfDesc.width;
        SrcRect.bottom = pAlloc->AllocData.SurfDesc.height;
        Rects.ContextRect.left = 0;
        Rects.ContextRect.top = 0;
        Rects.ContextRect.right = pAlloc->AllocData.SurfDesc.width;
        Rects.ContextRect.bottom = pAlloc->AllocData.SurfDesc.height;
        Rects.UpdateRects.cRects = 1;
        Rects.UpdateRects.aRects[0] = Rects.ContextRect;

        Status = nemuVdmaProcessVRegCmd(pDevExt, pContext, &pFlip->Flip.Alloc, &pFlip->Flip.Alloc, &SrcRect, &Rects);
        if (!NT_SUCCESS(Status))
            WARN(("nemuVdmaProcessVRegCmd failed Status 0x%x", Status));
    }
    else
#endif
    {
        WARN(("unexpected flip request"));
    }

    return Status;
}

NTSTATUS nemuVdmaProcessClrFillCmd(PNEMUMP_DEVEXT pDevExt, NEMUWDDM_CONTEXT *pContext, NEMUWDDM_DMA_PRIVATEDATA_CLRFILL *pCF)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_ALLOCATION pAlloc = pCF->ClrFill.Alloc.pAlloc;
#ifdef NEMU_WITH_CROGL
    if (!pAlloc->AllocData.hostID)
#endif
    {
        Status = nemuVdmaGgDmaColorFill(pDevExt, &pCF->ClrFill);
        if (!NT_SUCCESS(Status))
            WARN(("nemuVdmaGgDmaColorFill failed Status 0x%x", Status));
    }
#ifdef NEMU_WITH_CROGL
    else
        WARN(("unexpected clrfill request"));
#endif

    return Status;
}


#ifdef NEMU_WITH_VDMA
/*
 * This is currently used by VDMA. It is invisible for Vdma API clients since
 * Vdma transport may change if we choose to use another (e.g. more light-weight)
 * transport for DMA commands submission
 */

#ifdef NEMUVDMA_WITH_VBVA
static int nemuWddmVdmaSubmitVbva(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo, HGSMIOFFSET offDr)
{
    int rc;
    if (nemuVbvaBufferBeginUpdate (pDevExt, &pDevExt->u.primary.Vbva))
    {
        rc = nemuVbvaReportCmdOffset(pDevExt, &pDevExt->u.primary.Vbva, offDr);
        nemuVbvaBufferEndUpdate (pDevExt, &pDevExt->u.primary.Vbva);
    }
    else
    {
        AssertBreakpoint();
        rc = VERR_INVALID_STATE;
    }
    return rc;
}
#define nemuWddmVdmaSubmit nemuWddmVdmaSubmitVbva
#else
static int nemuWddmVdmaSubmitHgsmi(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo, HGSMIOFFSET offDr)
{
    NemuVideoCmnPortWriteUlong(NemuCommonFromDeviceExt(pDevExt)->guestCtx.port, offDr);
    /* Make the compiler aware that the host has changed memory. */
    ASMCompilerBarrier();
    return VINF_SUCCESS;
}
#define nemuWddmVdmaSubmit nemuWddmVdmaSubmitHgsmi
#endif

static int nemuVdmaInformHost(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo, NEMUVDMA_CTL_TYPE enmCtl)
{
    int rc = VINF_SUCCESS;

    PNEMUVDMA_CTL pCmd = (PNEMUVDMA_CTL)NemuSHGSMICommandAlloc(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, sizeof (NEMUVDMA_CTL), HGSMI_CH_VBVA, VBVA_VDMA_CTL);
    if (pCmd)
    {
        pCmd->enmCtl = enmCtl;
        pCmd->u32Offset = pInfo->CmdHeap.Heap.area.offBase;
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
                    rc = nemuWddmVdmaSubmit(pDevExt, pInfo, offCmd);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        rc = NemuSHGSMICommandDoneSynch(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, pHdr);
                        AssertRC(rc);
                        if (RT_SUCCESS(rc))
                        {
                            rc = pCmd->i32Result;
                            AssertRC(rc);
                        }
                        break;
                    }
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
#endif

static DECLCALLBACK(void *) hgsmiEnvAlloc(void *pvEnv, HGSMISIZE cb)
{
    NOREF(pvEnv);
    return RTMemAlloc(cb);
}

static DECLCALLBACK(void) hgsmiEnvFree(void *pvEnv, void *pv)
{
    NOREF(pvEnv);
    RTMemFree(pv);
}

static HGSMIENV g_hgsmiEnvVdma =
{
    NULL,
    hgsmiEnvAlloc,
    hgsmiEnvFree
};

/* create a DMACommand buffer */
int nemuVdmaCreate(PNEMUMP_DEVEXT pDevExt, NEMUVDMAINFO *pInfo
#ifdef NEMU_WITH_VDMA
        , ULONG offBuffer, ULONG cbBuffer
#endif
        )
{
    pInfo->fEnabled           = FALSE;

#ifdef NEMU_WITH_VDMA
    int rc;
    Assert((offBuffer & 0xfff) == 0);
    Assert((cbBuffer & 0xfff) == 0);
    Assert(offBuffer);
    Assert(cbBuffer);

    if((offBuffer & 0xfff)
            || (cbBuffer & 0xfff)
            || !offBuffer
            || !cbBuffer)
    {
        LOGREL(("invalid parameters: offBuffer(0x%x), cbBuffer(0x%x)", offBuffer, cbBuffer));
        return VERR_INVALID_PARAMETER;
    }
    PVOID pvBuffer;

    rc = NemuMPCmnMapAdapterMemory(NemuCommonFromDeviceExt(pDevExt),
                                   &pvBuffer,
                                   offBuffer,
                                   cbBuffer);
    Assert(RT_SUCCESS(rc));
    if (RT_SUCCESS(rc))
    {
        /* Setup a HGSMI heap within the adapter information area. */
        rc = NemuSHGSMIInit(&pInfo->CmdHeap,
                             pvBuffer,
                             cbBuffer,
                             offBuffer,
                             &g_hgsmiEnvVdma);
        Assert(RT_SUCCESS(rc));
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;
        else
            LOGREL(("HGSMIHeapSetup failed rc = 0x%x", rc));

        NemuMPCmnUnmapAdapterMemory(NemuCommonFromDeviceExt(pDevExt), &pvBuffer);
    }
    else
        LOGREL(("NemuMapAdapterMemory failed rc = 0x%x\n", rc));
    return rc;
#else
    return VINF_SUCCESS;
#endif
}

int nemuVdmaDisable (PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo)
{
    Assert(pInfo->fEnabled);
    if (!pInfo->fEnabled)
        return VINF_ALREADY_INITIALIZED;

    /* ensure nothing else is submitted */
    pInfo->fEnabled        = FALSE;
#ifdef NEMU_WITH_VDMA
    int rc = nemuVdmaInformHost (pDevExt, pInfo, NEMUVDMA_CTL_TYPE_DISABLE);
    AssertRC(rc);
    return rc;
#else
    return VINF_SUCCESS;
#endif
}

int nemuVdmaEnable (PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo)
{
    Assert(!pInfo->fEnabled);
    if (pInfo->fEnabled)
        return VINF_ALREADY_INITIALIZED;
#ifdef NEMU_WITH_VDMA
    int rc = nemuVdmaInformHost (pDevExt, pInfo, NEMUVDMA_CTL_TYPE_ENABLE);
    Assert(RT_SUCCESS(rc));
    if (RT_SUCCESS(rc))
        pInfo->fEnabled        = TRUE;

    return rc;
#else
    return VINF_SUCCESS;
#endif
}

#ifdef NEMU_WITH_VDMA
int nemuVdmaFlush (PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo)
{
    Assert(pInfo->fEnabled);
    if (!pInfo->fEnabled)
        return VINF_ALREADY_INITIALIZED;

    int rc = nemuVdmaInformHost (pDevExt, pInfo, NEMUVDMA_CTL_TYPE_FLUSH);
    Assert(RT_SUCCESS(rc));

    return rc;
}
#endif

int nemuVdmaDestroy (PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo)
{
    int rc = VINF_SUCCESS;
    Assert(!pInfo->fEnabled);
    if (pInfo->fEnabled)
        rc = nemuVdmaDisable (pDevExt, pInfo);
#ifdef NEMU_WITH_VDMA
    NemuSHGSMITerm(&pInfo->CmdHeap);
    NemuMPCmnUnmapAdapterMemory(NemuCommonFromDeviceExt(pDevExt), (void**)&pInfo->CmdHeap.Heap.area.pu8Base);
#endif
    return rc;
}

#ifdef NEMU_WITH_VDMA
void nemuVdmaCBufDrFree (PNEMUVDMAINFO pInfo, PNEMUVDMACBUF_DR pDr)
{
    NemuSHGSMICommandFree (&pInfo->CmdHeap, pDr);
}

PNEMUVDMACBUF_DR nemuVdmaCBufDrCreate (PNEMUVDMAINFO pInfo, uint32_t cbTrailingData)
{
    uint32_t cbDr = NEMUVDMACBUF_DR_SIZE(cbTrailingData);
    PNEMUVDMACBUF_DR pDr = (PNEMUVDMACBUF_DR)NemuSHGSMICommandAlloc (&pInfo->CmdHeap, cbDr, HGSMI_CH_VBVA, VBVA_VDMA_CMD);
    Assert(pDr);
    if (pDr)
        memset (pDr, 0, cbDr);
    else
        LOGREL(("NemuSHGSMICommandAlloc returned NULL"));

    return pDr;
}

static DECLCALLBACK(void) nemuVdmaCBufDrCompletion(PNEMUSHGSMI pHeap, void *pvCmd, void *pvContext)
{
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)pvContext;
    PNEMUVDMAINFO pInfo = &pDevExt->u.primary.Vdma;

    nemuVdmaCBufDrFree (pInfo, (PNEMUVDMACBUF_DR)pvCmd);
}

static DECLCALLBACK(PFNNEMUSHGSMICMDCOMPLETION) nemuVdmaCBufDrCompletionIrq(PNEMUSHGSMI pHeap, void *pvCmd, void *pvContext, void **ppvCompletion)
{
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)pvContext;
    PNEMUVDMAINFO pVdma = &pDevExt->u.primary.Vdma;
    PNEMUVDMACBUF_DR pDr = (PNEMUVDMACBUF_DR)pvCmd;

    DXGK_INTERRUPT_TYPE enmComplType;

    if (RT_SUCCESS(pDr->rc))
    {
        enmComplType = DXGK_INTERRUPT_DMA_COMPLETED;
    }
    else if (pDr->rc == VERR_INTERRUPTED)
    {
        Assert(0);
        enmComplType = DXGK_INTERRUPT_DMA_PREEMPTED;
    }
    else
    {
        Assert(0);
        enmComplType = DXGK_INTERRUPT_DMA_FAULTED;
    }

    if (nemuVdmaDdiCmdCompletedIrq(pDevExt, NEMUVDMADDI_CMD_FROM_BUF_DR(pDr), enmComplType))
    {
        pDevExt->bNotifyDxDpc = TRUE;
    }

    /* inform SHGSMI we DO NOT want to be called at DPC later */
    return NULL;
//    *ppvCompletion = pvContext;
}

int nemuVdmaCBufDrSubmit(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo, PNEMUVDMACBUF_DR pDr)
{
    const NEMUSHGSMIHEADER* pHdr = NemuSHGSMICommandPrepAsynchIrq (&pInfo->CmdHeap, pDr, nemuVdmaCBufDrCompletionIrq, pDevExt, NEMUSHGSMI_FLAG_GH_ASYNCH_FORCE);
    Assert(pHdr);
    int rc = VERR_GENERAL_FAILURE;
    if (pHdr)
    {
        do
        {
            HGSMIOFFSET offCmd = NemuSHGSMICommandOffset(&pInfo->CmdHeap, pHdr);
            Assert(offCmd != HGSMIOFFSET_VOID);
            if (offCmd != HGSMIOFFSET_VOID)
            {
                rc = nemuWddmVdmaSubmit(pDevExt, pInfo, offCmd);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    NemuSHGSMICommandDoneAsynch(&pInfo->CmdHeap, pHdr);
                    AssertRC(rc);
                    break;
                }
            }
            else
                rc = VERR_INVALID_PARAMETER;
            /* fail to submit, cancel it */
            NemuSHGSMICommandCancelAsynch(&pInfo->CmdHeap, pHdr);
        } while (0);
    }
    else
        rc = VERR_INVALID_PARAMETER;
    return rc;
}

int nemuVdmaCBufDrSubmitSynch(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo, PNEMUVDMACBUF_DR pDr)
{
    const NEMUSHGSMIHEADER* pHdr = NemuSHGSMICommandPrepAsynch (&pInfo->CmdHeap, pDr, NULL, NULL, NEMUSHGSMI_FLAG_GH_SYNCH);
    Assert(pHdr);
    int rc = VERR_GENERAL_FAILURE;
    if (pHdr)
    {
        do
        {
            HGSMIOFFSET offCmd = NemuSHGSMICommandOffset(&pInfo->CmdHeap, pHdr);
            Assert(offCmd != HGSMIOFFSET_VOID);
            if (offCmd != HGSMIOFFSET_VOID)
            {
                rc = nemuWddmVdmaSubmit(pDevExt, pInfo, offCmd);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    NemuSHGSMICommandDoneAsynch(&pInfo->CmdHeap, pHdr);
                    AssertRC(rc);
                    break;
                }
            }
            else
                rc = VERR_INVALID_PARAMETER;
            /* fail to submit, cancel it */
            NemuSHGSMICommandCancelAsynch(&pInfo->CmdHeap, pHdr);
        } while (0);
    }
    else
        rc = VERR_INVALID_PARAMETER;
    return rc;
}
#endif


/* ddi dma command queue */

VOID nemuVdmaDdiCmdGetCompletedListIsr(PNEMUMP_DEVEXT pDevExt, LIST_ENTRY *pList)
{
    nemuVideoLeDetach(&pDevExt->DpcCmdQueue, pList);
}

BOOLEAN nemuVdmaDdiCmdIsCompletedListEmptyIsr(PNEMUMP_DEVEXT pDevExt)
{
    return IsListEmpty(&pDevExt->DpcCmdQueue);
}

DECLINLINE(BOOLEAN) nemuVdmaDdiCmdCanComplete(PNEMUMP_DEVEXT pDevExt, UINT u32NodeOrdinal)
{
    PNEMUVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[u32NodeOrdinal].CmdQueue;
    return ASMAtomicUoReadU32(&pQueue->cQueuedCmds) == 0;
}

DECLCALLBACK(VOID) nemuVdmaDdiCmdCompletionCbFree(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, PVOID pvContext)
{
    nemuWddmMemFree(pCmd);
}

static VOID nemuVdmaDdiCmdNotifyCompletedIrq(PNEMUMP_DEVEXT pDevExt, UINT u32NodeOrdinal, UINT u32FenceId, DXGK_INTERRUPT_TYPE enmComplType)
{
    PNEMUVDMADDI_NODE pNode = &pDevExt->aNodes[u32NodeOrdinal];
    DXGKARGCB_NOTIFY_INTERRUPT_DATA notify;
    memset(&notify, 0, sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA));
    switch (enmComplType)
    {
        case DXGK_INTERRUPT_DMA_COMPLETED:
            notify.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
            notify.DmaCompleted.SubmissionFenceId = u32FenceId;
            notify.DmaCompleted.NodeOrdinal = u32NodeOrdinal;
            pNode->uLastCompletedFenceId = u32FenceId;
            break;

        case DXGK_INTERRUPT_DMA_PREEMPTED:
            Assert(0);
            notify.InterruptType = DXGK_INTERRUPT_DMA_PREEMPTED;
            notify.DmaPreempted.PreemptionFenceId = u32FenceId;
            notify.DmaPreempted.NodeOrdinal = u32NodeOrdinal;
            notify.DmaPreempted.LastCompletedFenceId = pNode->uLastCompletedFenceId;
            break;

        case DXGK_INTERRUPT_DMA_FAULTED:
            Assert(0);
            notify.InterruptType = DXGK_INTERRUPT_DMA_FAULTED;
            notify.DmaFaulted.FaultedFenceId = u32FenceId;
            notify.DmaFaulted.Status = STATUS_UNSUCCESSFUL; /* @todo: better status ? */
            notify.DmaFaulted.NodeOrdinal = u32NodeOrdinal;
            break;

        default:
            Assert(0);
            break;
    }

    pDevExt->u.primary.DxgkInterface.DxgkCbNotifyInterrupt(pDevExt->u.primary.DxgkInterface.DeviceHandle, &notify);
}

static VOID nemuVdmaDdiCmdProcessCompletedIrq(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, DXGK_INTERRUPT_TYPE enmComplType)
{
    nemuVdmaDdiCmdNotifyCompletedIrq(pDevExt, pCmd->u32NodeOrdinal, pCmd->u32FenceId, enmComplType);
    switch (enmComplType)
    {
        case DXGK_INTERRUPT_DMA_COMPLETED:
            InsertTailList(&pDevExt->DpcCmdQueue, &pCmd->QueueEntry);
            break;
        default:
            AssertFailed();
            break;
    }
}

DECLINLINE(VOID) nemuVdmaDdiCmdDequeueIrq(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd)
{
    PNEMUVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[pCmd->u32NodeOrdinal].CmdQueue;
    ASMAtomicDecU32(&pQueue->cQueuedCmds);
    RemoveEntryList(&pCmd->QueueEntry);
}

DECLINLINE(VOID) nemuVdmaDdiCmdEnqueueIrq(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd)
{
    PNEMUVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[pCmd->u32NodeOrdinal].CmdQueue;
    ASMAtomicIncU32(&pQueue->cQueuedCmds);
    InsertTailList(&pQueue->CmdQueue, &pCmd->QueueEntry);
}

VOID nemuVdmaDdiNodesInit(PNEMUMP_DEVEXT pDevExt)
{
    for (UINT i = 0; i < RT_ELEMENTS(pDevExt->aNodes); ++i)
    {
        pDevExt->aNodes[i].uLastCompletedFenceId = 0;
        PNEMUVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[i].CmdQueue;
        pQueue->cQueuedCmds = 0;
        InitializeListHead(&pQueue->CmdQueue);
    }
    InitializeListHead(&pDevExt->DpcCmdQueue);
}

BOOLEAN nemuVdmaDdiCmdCompletedIrq(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, DXGK_INTERRUPT_TYPE enmComplType)
{
    if (NEMUVDMADDI_STATE_NOT_DX_CMD == pCmd->enmState)
    {
        InsertTailList(&pDevExt->DpcCmdQueue, &pCmd->QueueEntry);
        return FALSE;
    }

    PNEMUVDMADDI_CMD_QUEUE pQueue = &pDevExt->aNodes[pCmd->u32NodeOrdinal].CmdQueue;
    BOOLEAN bQueued = pCmd->enmState > NEMUVDMADDI_STATE_NOT_QUEUED;
    BOOLEAN bComplete = FALSE;
    Assert(!bQueued || pQueue->cQueuedCmds);
    Assert(!bQueued || !IsListEmpty(&pQueue->CmdQueue));
    pCmd->enmState = NEMUVDMADDI_STATE_COMPLETED;
    if (bQueued)
    {
        if (pQueue->CmdQueue.Flink == &pCmd->QueueEntry)
        {
            nemuVdmaDdiCmdDequeueIrq(pDevExt, pCmd);
            bComplete = TRUE;
        }
    }
    else if (IsListEmpty(&pQueue->CmdQueue))
    {
        bComplete = TRUE;
    }
    else
    {
        nemuVdmaDdiCmdEnqueueIrq(pDevExt, pCmd);
    }

    if (bComplete)
    {
        nemuVdmaDdiCmdProcessCompletedIrq(pDevExt, pCmd, enmComplType);

        while (!IsListEmpty(&pQueue->CmdQueue))
        {
            pCmd = NEMUVDMADDI_CMD_FROM_ENTRY(pQueue->CmdQueue.Flink);
            if (pCmd->enmState == NEMUVDMADDI_STATE_COMPLETED)
            {
                nemuVdmaDdiCmdDequeueIrq(pDevExt, pCmd);
                nemuVdmaDdiCmdProcessCompletedIrq(pDevExt, pCmd, pCmd->enmComplType);
            }
            else
                break;
        }
    }
    else
    {
        pCmd->enmState = NEMUVDMADDI_STATE_COMPLETED;
        pCmd->enmComplType = enmComplType;
    }

    return bComplete;
}

VOID nemuVdmaDdiCmdSubmittedIrq(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd)
{
    BOOLEAN bQueued = pCmd->enmState >= NEMUVDMADDI_STATE_PENDING;
    Assert(pCmd->enmState < NEMUVDMADDI_STATE_SUBMITTED);
    pCmd->enmState = NEMUVDMADDI_STATE_SUBMITTED;
    if (!bQueued)
        nemuVdmaDdiCmdEnqueueIrq(pDevExt, pCmd);
}

typedef struct NEMUVDMADDI_CMD_COMPLETED_CB
{
    PNEMUMP_DEVEXT pDevExt;
    PNEMUVDMADDI_CMD pCmd;
    DXGK_INTERRUPT_TYPE enmComplType;
} NEMUVDMADDI_CMD_COMPLETED_CB, *PNEMUVDMADDI_CMD_COMPLETED_CB;

static BOOLEAN nemuVdmaDdiCmdCompletedCb(PVOID Context)
{
    PNEMUVDMADDI_CMD_COMPLETED_CB pdc = (PNEMUVDMADDI_CMD_COMPLETED_CB)Context;
    PNEMUMP_DEVEXT pDevExt = pdc->pDevExt;
    BOOLEAN bNeedDpc = nemuVdmaDdiCmdCompletedIrq(pDevExt, pdc->pCmd, pdc->enmComplType);
    pDevExt->bNotifyDxDpc |= bNeedDpc;

    if (bNeedDpc)
    {
        pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);
    }

    return bNeedDpc;
}

NTSTATUS nemuVdmaDdiCmdCompleted(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, DXGK_INTERRUPT_TYPE enmComplType)
{
    NEMUVDMADDI_CMD_COMPLETED_CB context;
    context.pDevExt = pDevExt;
    context.pCmd = pCmd;
    context.enmComplType = enmComplType;
    BOOLEAN bNeedDps;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            nemuVdmaDdiCmdCompletedCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bNeedDps);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

typedef struct NEMUVDMADDI_CMD_SUBMITTED_CB
{
    PNEMUMP_DEVEXT pDevExt;
    PNEMUVDMADDI_CMD pCmd;
} NEMUVDMADDI_CMD_SUBMITTED_CB, *PNEMUVDMADDI_CMD_SUBMITTED_CB;

static BOOLEAN nemuVdmaDdiCmdSubmittedCb(PVOID Context)
{
    PNEMUVDMADDI_CMD_SUBMITTED_CB pdc = (PNEMUVDMADDI_CMD_SUBMITTED_CB)Context;
    nemuVdmaDdiCmdSubmittedIrq(pdc->pDevExt, pdc->pCmd);

    return FALSE;
}

NTSTATUS nemuVdmaDdiCmdSubmitted(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd)
{
    NEMUVDMADDI_CMD_SUBMITTED_CB context;
    context.pDevExt = pDevExt;
    context.pCmd = pCmd;
    BOOLEAN bRc;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            nemuVdmaDdiCmdSubmittedCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRc);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

typedef struct NEMUVDMADDI_CMD_COMPLETE_CB
{
    PNEMUMP_DEVEXT pDevExt;
    UINT u32NodeOrdinal;
    uint32_t u32FenceId;
} NEMUVDMADDI_CMD_COMPLETE_CB, *PNEMUVDMADDI_CMD_COMPLETE_CB;

static BOOLEAN nemuVdmaDdiCmdFenceCompleteCb(PVOID Context)
{
    PNEMUVDMADDI_CMD_COMPLETE_CB pdc = (PNEMUVDMADDI_CMD_COMPLETE_CB)Context;
    PNEMUMP_DEVEXT pDevExt = pdc->pDevExt;

    nemuVdmaDdiCmdNotifyCompletedIrq(pDevExt, pdc->u32NodeOrdinal, pdc->u32FenceId, DXGK_INTERRUPT_DMA_COMPLETED);

    pDevExt->bNotifyDxDpc = TRUE;
    pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);

    return TRUE;
}

static NTSTATUS nemuVdmaDdiCmdFenceNotifyComplete(PNEMUMP_DEVEXT pDevExt, uint32_t u32NodeOrdinal, uint32_t u32FenceId)
{
    NEMUVDMADDI_CMD_COMPLETE_CB context;
    context.pDevExt = pDevExt;
    context.u32NodeOrdinal = u32NodeOrdinal;
    context.u32FenceId = u32FenceId;
    BOOLEAN bRet;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            nemuVdmaDdiCmdFenceCompleteCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRet);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

NTSTATUS nemuVdmaDdiCmdFenceComplete(PNEMUMP_DEVEXT pDevExt, uint32_t u32NodeOrdinal, uint32_t u32FenceId, DXGK_INTERRUPT_TYPE enmComplType)
{
    if (nemuVdmaDdiCmdCanComplete(pDevExt, u32NodeOrdinal))
        return nemuVdmaDdiCmdFenceNotifyComplete(pDevExt, u32NodeOrdinal, u32FenceId);

    PNEMUVDMADDI_CMD pCmd = (PNEMUVDMADDI_CMD)nemuWddmMemAlloc(sizeof (NEMUVDMADDI_CMD));
    Assert(pCmd);
    if (pCmd)
    {
        nemuVdmaDdiCmdInit(pCmd, u32NodeOrdinal, u32FenceId, nemuVdmaDdiCmdCompletionCbFree, NULL);
        NTSTATUS Status = nemuVdmaDdiCmdCompleted(pDevExt, pCmd, enmComplType);
        Assert(Status == STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
            return STATUS_SUCCESS;
        nemuWddmMemFree(pCmd);
        return Status;
    }
    return STATUS_NO_MEMORY;
}
