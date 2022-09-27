/* $Id: NemuMPVModes.cpp $ */

/** @file
 * Nemu WDDM Miniport driver
 */

/*
 * Copyright (C) 2014 Oracle Corporation
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

#include <stdio.h>


int NemuVModesInit(NEMU_VMODES *pModes, uint32_t cTargets)
{
    if (cTargets >= NEMU_VIDEO_MAX_SCREENS)
    {
        WARN(("invalid target"));
        return VERR_INVALID_PARAMETER;
    }

    pModes->cTargets = cTargets;
    for (uint32_t i = 0; i < cTargets; ++i)
    {
        int rc = CrSaInit(&pModes->aTargets[i], 16);
        if (RT_FAILURE(rc))
        {
            WARN(("CrSaInit failed"));

            for (uint32_t j = 0; j < i; ++j)
            {
                CrSaCleanup(&pModes->aTargets[j]);
            }
            return rc;
        }
    }

    return VINF_SUCCESS;
}

void NemuVModesCleanup(NEMU_VMODES *pModes)
{
    for (uint32_t i = 0; i < pModes->cTargets; ++i)
    {
        CrSaCleanup(&pModes->aTargets[i]);
    }
}

int NemuVModesAdd(NEMU_VMODES *pModes, uint32_t u32Target, uint64_t u64)
{
    if (u32Target >= pModes->cTargets)
    {
        WARN(("invalid target id"));
        return VERR_INVALID_PARAMETER;
    }

    return CrSaAdd(&pModes->aTargets[u32Target], u64);
}

int NemuVModesRemove(NEMU_VMODES *pModes, uint32_t u32Target, uint64_t u64)
{
    if (u32Target >= pModes->cTargets)
    {
        WARN(("invalid target id"));
        return VERR_INVALID_PARAMETER;
    }

    return CrSaRemove(&pModes->aTargets[u32Target], u64);
}

static void nemuWddmVModesInit(NEMUWDDM_VMODES *pModes, uint32_t cTargets)
{
    NemuVModesInit(&pModes->Modes, cTargets);
    memset(pModes->aTransientResolutions, 0, cTargets * sizeof (pModes->aTransientResolutions[0]));
    memset(pModes->aPendingRemoveCurResolutions, 0, cTargets * sizeof (pModes->aPendingRemoveCurResolutions[0]));
}

static void nemuWddmVModesCleanup(NEMUWDDM_VMODES *pModes)
{
    NemuVModesCleanup(&pModes->Modes);
    memset(pModes->aTransientResolutions, 0, sizeof (pModes->aTransientResolutions));
    memset(pModes->aPendingRemoveCurResolutions, 0, sizeof (pModes->aPendingRemoveCurResolutions));
}

/*
static void nemuWddmVModesClone(const NEMUWDDM_VMODES *pModes, NEMUWDDM_VMODES *pDst)
{
    NemuVModesClone(&pModes->Modes, pDst->Modes);
    memcpy(pDst->aTransientResolutions, pModes->aTransientResolutions, pModes->Modes.cTargets * sizeof (pModes->aTransientResolutions[0]));
    memcpy(pDst->aPendingRemoveCurResolutions, pModes->aPendingRemoveCurResolutions, pModes->Modes.cTargets * sizeof (pModes->aPendingRemoveCurResolutions[0]));
}
*/

static const RTRECTSIZE g_NemuBuiltinResolutions[] =
{
    /* standard modes */
    { 640,   480 },
    { 800,   600 },
    { 1024,  768 },
    { 1152,  864 },
    { 1280,  960 },
    { 1280, 1024 },
    { 1400, 1050 },
    { 1600, 1200 },
    { 1920, 1440 },
};

DECLINLINE(bool) nemuVModesRMatch(const RTRECTSIZE *pResolution1, const RTRECTSIZE *pResolution2)
{
    return !memcmp(pResolution1, pResolution2, sizeof (*pResolution1));
}

int nemuWddmVModesRemove(PNEMUMP_DEVEXT pExt, NEMUWDDM_VMODES *pModes, uint32_t u32Target, const RTRECTSIZE *pResolution)
{
    if (!pResolution->cx || !pResolution->cy)
    {
        WARN(("invalid resolution data"));
        return VERR_INVALID_PARAMETER;
    }

    if (u32Target >= pModes->Modes.cTargets)
    {
        WARN(("invalid target id"));
        return VERR_INVALID_PARAMETER;
    }

    if (CR_RSIZE2U64(*pResolution) == pModes->aTransientResolutions[u32Target])
        pModes->aTransientResolutions[u32Target] = 0;

    if (nemuVModesRMatch(pResolution, &pExt->aTargets[u32Target].Size))
    {
        if (CR_RSIZE2U64(*pResolution) == pModes->aPendingRemoveCurResolutions[u32Target])
            return VINF_ALREADY_INITIALIZED;

        if (pModes->aPendingRemoveCurResolutions[u32Target])
        {
            NemuVModesRemove(&pModes->Modes, u32Target, pModes->aPendingRemoveCurResolutions[u32Target]);
            pModes->aPendingRemoveCurResolutions[u32Target] = 0;
        }

        pModes->aPendingRemoveCurResolutions[u32Target] = CR_RSIZE2U64(*pResolution);
        return VINF_ALREADY_INITIALIZED;
    }
    else if (CR_RSIZE2U64(*pResolution) == pModes->aPendingRemoveCurResolutions[u32Target])
        pModes->aPendingRemoveCurResolutions[u32Target] = 0;

    int rc = NemuVModesRemove(&pModes->Modes, u32Target, CR_RSIZE2U64(*pResolution));
    if (RT_FAILURE(rc))
    {
        WARN(("NemuVModesRemove failed %d, can never happen", rc));
        return rc;
    }

    if (rc == VINF_ALREADY_INITIALIZED)
        return rc;

    return VINF_SUCCESS;
}

static void nemuWddmVModesSaveTransient(PNEMUMP_DEVEXT pExt, uint32_t u32Target, const RTRECTSIZE *pResolution)
{
    NEMUMPCMNREGISTRY Registry;
    VP_STATUS rc;

    rc = NemuMPCmnRegInit(pExt, &Registry);
    NEMUMP_WARN_VPS(rc);

    if (u32Target==0)
    {
        /*First name without a suffix*/
        rc = NemuMPCmnRegSetDword(Registry, L"CustomXRes", pResolution->cx);
        NEMUMP_WARN_VPS(rc);
        rc = NemuMPCmnRegSetDword(Registry, L"CustomYRes", pResolution->cy);
        NEMUMP_WARN_VPS(rc);
        rc = NemuMPCmnRegSetDword(Registry, L"CustomBPP", 32); /* <- just in case for older driver usage */
        NEMUMP_WARN_VPS(rc);
    }
    else
    {
        wchar_t keyname[32];
        swprintf(keyname, L"CustomXRes%d", u32Target);
        rc = NemuMPCmnRegSetDword(Registry, keyname, pResolution->cx);
        NEMUMP_WARN_VPS(rc);
        swprintf(keyname, L"CustomYRes%d", u32Target);
        rc = NemuMPCmnRegSetDword(Registry, keyname, pResolution->cy);
        NEMUMP_WARN_VPS(rc);
        swprintf(keyname, L"CustomBPP%d", u32Target);
        rc = NemuMPCmnRegSetDword(Registry, keyname, 32); /* <- just in case for older driver usage */
        NEMUMP_WARN_VPS(rc);
    }

    rc = NemuMPCmnRegFini(Registry);
    NEMUMP_WARN_VPS(rc);
}

int nemuWddmVModesAdd(PNEMUMP_DEVEXT pExt, NEMUWDDM_VMODES *pModes, uint32_t u32Target, const RTRECTSIZE *pResolution, BOOLEAN fTransient)
{
    if (!pResolution->cx || !pResolution->cy)
    {
        WARN(("invalid resolution data"));
        return VERR_INVALID_PARAMETER;
    }

    if (u32Target >= pModes->Modes.cTargets)
    {
        WARN(("invalid target id"));
        return VERR_INVALID_PARAMETER;
    }

    ULONG vramSize = nemuWddmVramCpuVisibleSegmentSize(pExt);
    vramSize /= pExt->u.primary.commonInfo.cDisplays;
# ifdef NEMU_WDDM_WIN8
    if (!g_NemuDisplayOnly)
# endif
    {
        /* at least two surfaces will be needed: primary & shadow */
        vramSize /= 2;
    }
    vramSize &= ~PAGE_OFFSET_MASK;

    /* prevent potensial overflow */
    if (pResolution->cx > 0x7fff
            || pResolution->cy > 0x7fff)
    {
        WARN(("too big resolution"));
        return VERR_INVALID_PARAMETER;
    }
    uint32_t cbSurfMem = pResolution->cx * pResolution->cy * 4;
    if (cbSurfMem > vramSize)
        return VERR_NOT_SUPPORTED;

    if (!NemuLikesVideoMode(u32Target, pResolution->cx, pResolution->cy, 32))
        return VERR_NOT_SUPPORTED;

    if (pModes->aTransientResolutions[u32Target] == CR_RSIZE2U64(*pResolution))
    {
        if (!fTransient) /* if the mode is not transient anymore, remove it from transient */
            pModes->aTransientResolutions[u32Target] = 0;
        return VINF_ALREADY_INITIALIZED;
    }

    int rc;
    bool fTransientIfExists = false;
    if (pModes->aPendingRemoveCurResolutions[u32Target] == CR_RSIZE2U64(*pResolution))
    {
        /* no need to remove it anymore */
        pModes->aPendingRemoveCurResolutions[u32Target] = 0;
        rc = VINF_ALREADY_INITIALIZED;
        fTransientIfExists = true;
    }
    else
    {
        rc = NemuVModesAdd(&pModes->Modes, u32Target, CR_RSIZE2U64(*pResolution));
        if (RT_FAILURE(rc))
        {
            WARN(("NemuVModesAdd failed %d", rc));
            return rc;
        }
    }

    if (rc == VINF_ALREADY_INITIALIZED && !fTransientIfExists)
        return rc;

    if (fTransient)
    {
        if (pModes->aTransientResolutions[u32Target])
        {
            /* note that we can not overwrite rc here, because it holds the "existed" status, which we need to return */
            RTRECTSIZE size = CR_U642RSIZE(pModes->aTransientResolutions[u32Target]);
            int tmpRc = nemuWddmVModesRemove(pExt, pModes, u32Target, &size);
            if (RT_FAILURE(tmpRc))
            {
                WARN(("nemuWddmVModesRemove failed %d, can never happen", tmpRc));
                return tmpRc;
            }
        }
        Assert(!pModes->aTransientResolutions[u32Target]);

        pModes->aTransientResolutions[u32Target] = CR_RSIZE2U64(*pResolution);
        nemuWddmVModesSaveTransient(pExt, u32Target, pResolution);
    }

    return rc;
}

int voxWddmVModesInitForTarget(PNEMUMP_DEVEXT pExt, NEMUWDDM_VMODES *pModes, uint32_t u32Target)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_NemuBuiltinResolutions); ++i)
    {
        nemuWddmVModesAdd(pExt, pModes, u32Target, &g_NemuBuiltinResolutions[i], FALSE);
    }

    if (pExt->aTargets[u32Target].Size.cx)
    {
        nemuWddmVModesAdd(pExt, pModes, u32Target, &pExt->aTargets[u32Target].Size, TRUE);
    }

    /* Check registry for manually added modes, up to 128 entries is supported
     * Give up on the first error encountered.
     */
    NEMUMPCMNREGISTRY Registry;
    int fPrefSet=0;
    VP_STATUS vpRc;

    vpRc = NemuMPCmnRegInit(pExt, &Registry);
    if (vpRc != NO_ERROR)
    {
        WARN(("NemuMPCmnRegInit failed %d, ignore", vpRc));
        return VINF_SUCCESS;
    }

    uint32_t CustomXRes = 0, CustomYRes = 0;

    if (u32Target == 0)
    {
        /*First name without a suffix*/
        vpRc = NemuMPCmnRegQueryDword(Registry, L"CustomXRes", &CustomXRes);
        NEMUMP_WARN_VPS_NOBP(vpRc);
        vpRc = NemuMPCmnRegQueryDword(Registry, L"CustomYRes", &CustomYRes);
        NEMUMP_WARN_VPS_NOBP(vpRc);
    }
    else
    {
        wchar_t keyname[32];
        swprintf(keyname, L"CustomXRes%d", u32Target);
        vpRc = NemuMPCmnRegQueryDword(Registry, keyname, &CustomXRes);
        NEMUMP_WARN_VPS_NOBP(vpRc);
        swprintf(keyname, L"CustomYRes%d", u32Target);
        vpRc = NemuMPCmnRegQueryDword(Registry, keyname, &CustomYRes);
        NEMUMP_WARN_VPS_NOBP(vpRc);
    }

    LOG(("got stored custom resolution[%d] %dx%dx", u32Target, CustomXRes, CustomYRes));

    if (CustomXRes || CustomYRes)
    {
        if (CustomXRes == 0)
            CustomXRes = pExt->aTargets[u32Target].Size.cx ? pExt->aTargets[u32Target].Size.cx : 800;
        if (CustomYRes == 0)
            CustomYRes = pExt->aTargets[u32Target].Size.cy ? pExt->aTargets[u32Target].Size.cy : 600;

        RTRECTSIZE Resolution = {CustomXRes, CustomYRes};
        nemuWddmVModesAdd(pExt, pModes, u32Target, &Resolution, TRUE);
    }


    for (int curKey=0; curKey<128; curKey++)
    {
        wchar_t keyname[24];

        swprintf(keyname, L"CustomMode%dWidth", curKey);
        vpRc = NemuMPCmnRegQueryDword(Registry, keyname, &CustomXRes);
        NEMUMP_CHECK_VPS_BREAK(vpRc);

        swprintf(keyname, L"CustomMode%dHeight", curKey);
        vpRc = NemuMPCmnRegQueryDword(Registry, keyname, &CustomYRes);
        NEMUMP_CHECK_VPS_BREAK(vpRc);

        LOG(("got custom mode[%u]=%ux%u", curKey, CustomXRes, CustomYRes));

        /* round down width to be a multiple of 8 if necessary */
        if (!NemuCommonFromDeviceExt(pExt)->fAnyX)
        {
            CustomXRes &= 0xFFF8;
        }

        LOG(("adding video mode from registry."));

        RTRECTSIZE Resolution = {CustomXRes, CustomYRes};

        nemuWddmVModesAdd(pExt, pModes, u32Target, &Resolution, FALSE);
    }

    vpRc = NemuMPCmnRegFini(Registry);
    NEMUMP_WARN_VPS(vpRc);

    return VINF_SUCCESS;
}

static NEMUWDDM_VMODES g_NemuWddmVModes;

void NemuWddmVModesCleanup()
{
    NEMUWDDM_VMODES *pModes = &g_NemuWddmVModes;
    nemuWddmVModesCleanup(pModes);
}

int NemuWddmVModesInit(PNEMUMP_DEVEXT pExt)
{
    NEMUWDDM_VMODES *pModes = &g_NemuWddmVModes;

    nemuWddmVModesInit(pModes, NemuCommonFromDeviceExt(pExt)->cDisplays);

    int rc;

    for (int i = 0; i < NemuCommonFromDeviceExt(pExt)->cDisplays; ++i)
    {
        rc = voxWddmVModesInitForTarget(pExt, pModes, (uint32_t)i);
        if (RT_FAILURE(rc))
        {
            WARN(("voxWddmVModesInitForTarget failed %d", rc));
            return rc;
        }
    }

    return VINF_SUCCESS;
}

const CR_SORTARRAY* NemuWddmVModesGet(PNEMUMP_DEVEXT pExt, uint32_t u32Target)
{
    if (u32Target >= (uint32_t)NemuCommonFromDeviceExt(pExt)->cDisplays)
    {
        WARN(("invalid target"));
        return NULL;
    }

    return &g_NemuWddmVModes.Modes.aTargets[u32Target];
}

int NemuWddmVModesRemove(PNEMUMP_DEVEXT pExt, uint32_t u32Target, const RTRECTSIZE *pResolution)
{
    return nemuWddmVModesRemove(pExt, &g_NemuWddmVModes, u32Target, pResolution);
}

int NemuWddmVModesAdd(PNEMUMP_DEVEXT pExt, uint32_t u32Target, const RTRECTSIZE *pResolution, BOOLEAN fTrancient)
{
    return nemuWddmVModesAdd(pExt, &g_NemuWddmVModes, u32Target, pResolution, fTrancient);
}


static NTSTATUS nemuWddmChildStatusReportPerform(PNEMUMP_DEVEXT pDevExt, PNEMUVDMA_CHILD_STATUS pChildStatus, D3DDDI_VIDEO_PRESENT_TARGET_ID iChild)
{
    DXGK_CHILD_STATUS DdiChildStatus;

    Assert(iChild < UINT32_MAX/2);
    Assert(iChild < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays);

    PNEMUWDDM_TARGET pTarget = &pDevExt->aTargets[iChild];

    if ((pChildStatus->fFlags & NEMUVDMA_CHILD_STATUS_F_DISCONNECTED)
            && pTarget->fConnected)
    {
        /* report disconnected */
        memset(&DdiChildStatus, 0, sizeof (DdiChildStatus));
        DdiChildStatus.Type = StatusConnection;
        DdiChildStatus.ChildUid = iChild;
        DdiChildStatus.HotPlug.Connected = FALSE;

        LOG(("Reporting DISCONNECT to child %d", DdiChildStatus.ChildUid));

        NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbIndicateChildStatus(pDevExt->u.primary.DxgkInterface.DeviceHandle, &DdiChildStatus);
        if (!NT_SUCCESS(Status))
        {
            WARN(("DxgkCbIndicateChildStatus failed with Status (0x%x)", Status));
            return Status;
        }
        pTarget->fConnected = FALSE;
    }

    if ((pChildStatus->fFlags & NEMUVDMA_CHILD_STATUS_F_CONNECTED)
            && !pTarget->fConnected)
    {
        /* report disconnected */
        memset(&DdiChildStatus, 0, sizeof (DdiChildStatus));
        DdiChildStatus.Type = StatusConnection;
        DdiChildStatus.ChildUid = iChild;
        DdiChildStatus.HotPlug.Connected = TRUE;

        LOG(("Reporting CONNECT to child %d", DdiChildStatus.ChildUid));

        NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbIndicateChildStatus(pDevExt->u.primary.DxgkInterface.DeviceHandle, &DdiChildStatus);
        if (!NT_SUCCESS(Status))
        {
            WARN(("DxgkCbIndicateChildStatus failed with Status (0x%x)", Status));
            return Status;
        }
        pTarget->fConnected = TRUE;
    }

    if (pChildStatus->fFlags & NEMUVDMA_CHILD_STATUS_F_ROTATED)
    {
        /* report disconnected */
        memset(&DdiChildStatus, 0, sizeof (DdiChildStatus));
        DdiChildStatus.Type = StatusRotation;
        DdiChildStatus.ChildUid = iChild;
        DdiChildStatus.Rotation.Angle = pChildStatus->u8RotationAngle;

        LOG(("Reporting ROTATED to child %d", DdiChildStatus.ChildUid));

        NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbIndicateChildStatus(pDevExt->u.primary.DxgkInterface.DeviceHandle, &DdiChildStatus);
        if (!NT_SUCCESS(Status))
        {
            WARN(("DxgkCbIndicateChildStatus failed with Status (0x%x)", Status));
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS nemuWddmChildStatusHandleRequest(PNEMUMP_DEVEXT pDevExt, NEMUVDMACMD_CHILD_STATUS_IRQ *pBody)
{
    NTSTATUS Status = STATUS_SUCCESS;

    for (UINT i = 0; i < pBody->cInfos; ++i)
    {
        PNEMUVDMA_CHILD_STATUS pInfo = &pBody->aInfos[i];
        if (pBody->fFlags & NEMUVDMACMD_CHILD_STATUS_IRQ_F_APPLY_TO_ALL)
        {
            for (D3DDDI_VIDEO_PRESENT_TARGET_ID iChild = 0; iChild < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++iChild)
            {
                Status = nemuWddmChildStatusReportPerform(pDevExt, pInfo, iChild);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("nemuWddmChildStatusReportPerform failed with Status (0x%x)", Status));
                    break;
                }
            }
        }
        else
        {
            Status = nemuWddmChildStatusReportPerform(pDevExt, pInfo, pInfo->iChild);
            if (!NT_SUCCESS(Status))
            {
                WARN(("nemuWddmChildStatusReportPerform failed with Status (0x%x)", Status));
                break;
            }
        }
    }

    return Status;
}

#ifdef NEMU_WDDM_MONITOR_REPLUG_IRQ
typedef struct NEMUWDDMCHILDSTATUSCB
{
    PNEMUVDMACBUF_DR pDr;
    PKEVENT pEvent;
} NEMUWDDMCHILDSTATUSCB, *PNEMUWDDMCHILDSTATUSCB;

static DECLCALLBACK(VOID) nemuWddmChildStatusReportCompletion(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, PVOID pvContext)
{
    /* we should be called from our DPC routine */
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PNEMUWDDMCHILDSTATUSCB pCtx = (PNEMUWDDMCHILDSTATUSCB)pvContext;
    PNEMUVDMACBUF_DR pDr = pCtx->pDr;
    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHILD_STATUS_IRQ *pBody = NEMUVDMACMD_BODY(pHdr, NEMUVDMACMD_CHILD_STATUS_IRQ);

    nemuWddmChildStatusHandleRequest(pDevExt, pBody);

    nemuVdmaCBufDrFree(&pDevExt->u.primary.Vdma, pDr);

    if (pCtx->pEvent)
    {
        KeSetEvent(pCtx->pEvent, 0, FALSE);
    }
}
#endif

NTSTATUS NemuWddmChildStatusReportReconnected(PNEMUMP_DEVEXT pDevExt, uint32_t iChild)
{
#ifdef NEMU_WDDM_MONITOR_REPLUG_IRQ
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    UINT cbCmd = NEMUVDMACMD_SIZE_FROMBODYSIZE(sizeof (NEMUVDMACMD_CHILD_STATUS_IRQ));

    PNEMUVDMACBUF_DR pDr = nemuVdmaCBufDrCreate(&pDevExt->u.primary.Vdma, cbCmd);
    if (pDr)
    {
        // nemuVdmaCBufDrCreate zero initializes the pDr
        /* the command data follows the descriptor */
        pDr->fFlags = NEMUVDMACBUF_FLAG_BUF_FOLLOWS_DR;
        pDr->cbBuf = cbCmd;
        pDr->rc = VERR_NOT_IMPLEMENTED;

        PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
        pHdr->enmType = NEMUVDMACMD_TYPE_CHILD_STATUS_IRQ;
        pHdr->u32CmdSpecific = 0;
        PNEMUVDMACMD_CHILD_STATUS_IRQ pBody = NEMUVDMACMD_BODY(pHdr, NEMUVDMACMD_CHILD_STATUS_IRQ);
        pBody->cInfos = 1;
        if (iChild == D3DDDI_ID_ALL)
        {
            pBody->fFlags |= NEMUVDMACMD_CHILD_STATUS_IRQ_F_APPLY_TO_ALL;
        }
        pBody->aInfos[0].iChild = iChild;
        pBody->aInfos[0].fFlags = NEMUVDMA_CHILD_STATUS_F_DISCONNECTED | NEMUVDMA_CHILD_STATUS_F_CONNECTED;
        /* we're going to KeWaitForSingleObject */
        Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);

        PNEMUVDMADDI_CMD pDdiCmd = NEMUVDMADDI_CMD_FROM_BUF_DR(pDr);
        NEMUWDDMCHILDSTATUSCB Ctx;
        KEVENT Event;
        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Ctx.pDr = pDr;
        Ctx.pEvent = &Event;
        nemuVdmaDdiCmdInit(pDdiCmd, 0, 0, nemuWddmChildStatusReportCompletion, &Ctx);
        /* mark command as submitted & invisible for the dx runtime since dx did not originate it */
        nemuVdmaDdiCmdSubmittedNotDx(pDdiCmd);
        int rc = nemuVdmaCBufDrSubmit(pDevExt, &pDevExt->u.primary.Vdma, pDr);
        Assert(rc == VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Assert(Status == STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }

        Status = STATUS_UNSUCCESSFUL;

        nemuVdmaCBufDrFree(&pDevExt->u.primary.Vdma, pDr);
    }
    else
    {
        /* @todo: try flushing.. */
        WARN(("nemuVdmaCBufDrCreate returned NULL"));
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
#else
    NEMUVDMACMD_CHILD_STATUS_IRQ Body = {0};
    Body.cInfos = 1;
    if (iChild == D3DDDI_ID_ALL)
    {
        Body.fFlags |= NEMUVDMACMD_CHILD_STATUS_IRQ_F_APPLY_TO_ALL;
    }
    Body.aInfos[0].iChild = iChild;
    Body.aInfos[0].fFlags = NEMUVDMA_CHILD_STATUS_F_DISCONNECTED | NEMUVDMA_CHILD_STATUS_F_CONNECTED;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    return nemuWddmChildStatusHandleRequest(pDevExt, &Body);
#endif
}

NTSTATUS NemuWddmChildStatusConnect(PNEMUMP_DEVEXT pDevExt, uint32_t iChild, BOOLEAN fConnect)
{
#ifdef NEMU_WDDM_MONITOR_REPLUG_IRQ
# error "port me!"
#else
    Assert(iChild < (uint32_t)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
    NTSTATUS Status = STATUS_SUCCESS;
    NEMUVDMACMD_CHILD_STATUS_IRQ Body = {0};
    Body.cInfos = 1;
    Body.aInfos[0].iChild = iChild;
    Body.aInfos[0].fFlags = fConnect ? NEMUVDMA_CHILD_STATUS_F_CONNECTED : NEMUVDMA_CHILD_STATUS_F_DISCONNECTED;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    Status = nemuWddmChildStatusHandleRequest(pDevExt, &Body);
    if (!NT_SUCCESS(Status))
        WARN(("nemuWddmChildStatusHandleRequest failed Status 0x%x", Status));

    return Status;
#endif
}
