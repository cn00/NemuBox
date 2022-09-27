/* $Id: NemuMPVidPn.cpp $ */

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
#include "NemuMPVidPn.h"
#include "common/NemuMPCommon.h"

static D3DDDIFORMAT nemuWddmCalcPixelFormat(const VIDEO_MODE_INFORMATION *pInfo)
{
    switch (pInfo->BitsPerPlane)
    {
        case 32:
            if(!(pInfo->AttributeFlags & VIDEO_MODE_PALETTE_DRIVEN) && !(pInfo->AttributeFlags & VIDEO_MODE_MANAGED_PALETTE))
            {
                if (pInfo->RedMask == 0xFF0000 && pInfo->GreenMask == 0xFF00 && pInfo->BlueMask == 0xFF)
                    return D3DDDIFMT_A8R8G8B8;
                WARN(("unsupported format: bpp(%d), rmask(%d), gmask(%d), bmask(%d)",
                      pInfo->BitsPerPlane, pInfo->RedMask, pInfo->GreenMask, pInfo->BlueMask));
                AssertBreakpoint();
            }
            else
            {
                WARN(("unsupported AttributeFlags(0x%x)", pInfo->AttributeFlags));
                AssertBreakpoint();
            }
            break;
        case 24:
            if(!(pInfo->AttributeFlags & VIDEO_MODE_PALETTE_DRIVEN) && !(pInfo->AttributeFlags & VIDEO_MODE_MANAGED_PALETTE))
            {
                if (pInfo->RedMask == 0xFF0000 && pInfo->GreenMask == 0xFF00 && pInfo->BlueMask == 0xFF)
                    return D3DDDIFMT_R8G8B8;
                WARN(("unsupported format: bpp(%d), rmask(%d), gmask(%d), bmask(%d)",
                     pInfo->BitsPerPlane, pInfo->RedMask, pInfo->GreenMask, pInfo->BlueMask));
                AssertBreakpoint();
            }
            else
            {
                WARN(("unsupported AttributeFlags(0x%x)", pInfo->AttributeFlags));
                AssertBreakpoint();
            }
            break;
        case 16:
            if(!(pInfo->AttributeFlags & VIDEO_MODE_PALETTE_DRIVEN) && !(pInfo->AttributeFlags & VIDEO_MODE_MANAGED_PALETTE))
            {
                if (pInfo->RedMask == 0xF800 && pInfo->GreenMask == 0x7E0 && pInfo->BlueMask == 0x1F)
                    return D3DDDIFMT_R5G6B5;
                WARN(("unsupported format: bpp(%d), rmask(%d), gmask(%d), bmask(%d)",
                      pInfo->BitsPerPlane, pInfo->RedMask, pInfo->GreenMask, pInfo->BlueMask));
                AssertBreakpoint();
            }
            else
            {
                WARN(("unsupported AttributeFlags(0x%x)", pInfo->AttributeFlags));
                AssertBreakpoint();
            }
            break;
        case 8:
            if((pInfo->AttributeFlags & VIDEO_MODE_PALETTE_DRIVEN) && (pInfo->AttributeFlags & VIDEO_MODE_MANAGED_PALETTE))
            {
                return D3DDDIFMT_P8;
            }
            else
            {
                WARN(("unsupported AttributeFlags(0x%x)", pInfo->AttributeFlags));
                AssertBreakpoint();
            }
            break;
        default:
            WARN(("unsupported bpp(%d)", pInfo->BitsPerPlane));
            AssertBreakpoint();
            break;
    }

    return D3DDDIFMT_UNKNOWN;
}

static int nemuWddmResolutionFind(const D3DKMDT_2DREGION *pResolutions, int cResolutions, const D3DKMDT_2DREGION *pRes)
{
    for (int i = 0; i < cResolutions; ++i)
    {
        const D3DKMDT_2DREGION *pResolution = &pResolutions[i];
        if (pResolution->cx == pRes->cx && pResolution->cy == pRes->cy)
            return i;
    }
    return -1;
}

static bool nemuWddmVideoModesMatch(const VIDEO_MODE_INFORMATION *pMode1, const VIDEO_MODE_INFORMATION *pMode2)
{
    return pMode1->VisScreenHeight == pMode2->VisScreenHeight
            && pMode1->VisScreenWidth == pMode2->VisScreenWidth
            && pMode1->BitsPerPlane == pMode2->BitsPerPlane;
}

static int nemuWddmVideoModeFind(const VIDEO_MODE_INFORMATION *pModes, int cModes, const VIDEO_MODE_INFORMATION *pM)
{
    for (int i = 0; i < cModes; ++i)
    {
        const VIDEO_MODE_INFORMATION *pMode = &pModes[i];
        if (nemuWddmVideoModesMatch(pMode, pM))
            return i;
    }
    return -1;
}

static NTSTATUS nemuVidPnPopulateVideoSignalInfo(D3DKMDT_VIDEO_SIGNAL_INFO *pVsi,
        const RTRECTSIZE *pResolution,
        ULONG VSync)
{
    NTSTATUS Status = STATUS_SUCCESS;

    pVsi->VideoStandard  = D3DKMDT_VSS_OTHER;
    pVsi->ActiveSize.cx = pResolution->cx;
    pVsi->ActiveSize.cy = pResolution->cy;
    pVsi->VSyncFreq.Numerator = VSync * 1000;
    pVsi->VSyncFreq.Denominator = 1000;
    pVsi->TotalSize.cx = pVsi->ActiveSize.cx;// + NEMUVDPN_C_DISPLAY_HBLANK_SIZE;
    pVsi->TotalSize.cy = pVsi->ActiveSize.cy;// + NEMUVDPN_C_DISPLAY_VBLANK_SIZE;
    pVsi->PixelRate = pVsi->TotalSize.cx * pVsi->TotalSize.cy * VSync;
    pVsi->HSyncFreq.Numerator = (UINT)((pVsi->PixelRate / pVsi->TotalSize.cy) * 1000);
    pVsi->HSyncFreq.Denominator = 1000;
    pVsi->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

    return Status;
}

BOOLEAN nemuVidPnMatchVideoSignal(const D3DKMDT_VIDEO_SIGNAL_INFO *pVsi1, const D3DKMDT_VIDEO_SIGNAL_INFO *pVsi2)
{
    if (pVsi1->VideoStandard != pVsi2->VideoStandard)
        return FALSE;
    if (pVsi1->TotalSize.cx != pVsi2->TotalSize.cx)
        return FALSE;
    if (pVsi1->TotalSize.cy != pVsi2->TotalSize.cy)
        return FALSE;
    if (pVsi1->ActiveSize.cx != pVsi2->ActiveSize.cx)
        return FALSE;
    if (pVsi1->ActiveSize.cy != pVsi2->ActiveSize.cy)
        return FALSE;
    if (pVsi1->VSyncFreq.Numerator != pVsi2->VSyncFreq.Numerator)
        return FALSE;
    if (pVsi1->VSyncFreq.Denominator != pVsi2->VSyncFreq.Denominator)
        return FALSE;
    if (pVsi1->HSyncFreq.Numerator != pVsi2->HSyncFreq.Numerator)
        return FALSE;
    if (pVsi1->HSyncFreq.Denominator != pVsi2->HSyncFreq.Denominator)
        return FALSE;
    if (pVsi1->PixelRate != pVsi2->PixelRate)
        return FALSE;
    if (pVsi1->ScanLineOrdering != pVsi2->ScanLineOrdering)
        return FALSE;

    return TRUE;
}

static void nemuVidPnPopulateSourceModeInfo(D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo,
        const RTRECTSIZE *pSize)
{
    NTSTATUS Status = STATUS_SUCCESS;
    /* this is a graphics mode */
    pNewVidPnSourceModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
    pNewVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx = pSize->cx;
    pNewVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy = pSize->cy;
    pNewVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize = pNewVidPnSourceModeInfo->Format.Graphics.PrimSurfSize;
    pNewVidPnSourceModeInfo->Format.Graphics.Stride = pSize->cx * 4;
    pNewVidPnSourceModeInfo->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
    Assert(pNewVidPnSourceModeInfo->Format.Graphics.PixelFormat != D3DDDIFMT_UNKNOWN);
    pNewVidPnSourceModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SRGB;
    if (pNewVidPnSourceModeInfo->Format.Graphics.PixelFormat == D3DDDIFMT_P8)
        pNewVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_SETTABLEPALETTE;
    else
        pNewVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
}

static void nemuVidPnPopulateMonitorModeInfo(D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSourceMode,
        const RTRECTSIZE *pResolution)
{
    nemuVidPnPopulateVideoSignalInfo(&pMonitorSourceMode->VideoSignalInfo, pResolution, 60 /* ULONG VSync */);
    pMonitorSourceMode->ColorBasis = D3DKMDT_CB_SRGB;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FirstChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.SecondChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
    pMonitorSourceMode->ColorCoeffDynamicRanges.FourthChannel = 0;
    pMonitorSourceMode->Origin = D3DKMDT_MCO_DRIVER;
    pMonitorSourceMode->Preference = D3DKMDT_MP_NOTPREFERRED;
}

static NTSTATUS nemuVidPnPopulateTargetModeInfo(D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, const RTRECTSIZE *pResolution)
{
    pNewVidPnTargetModeInfo->Preference = D3DKMDT_MP_NOTPREFERRED;
    return nemuVidPnPopulateVideoSignalInfo(&pNewVidPnTargetModeInfo->VideoSignalInfo, pResolution, 60 /* ULONG VSync */);
}

void NemuVidPnStTargetCleanup(PNEMUWDDM_SOURCE paSources, uint32_t cScreens, PNEMUWDDM_TARGET pTarget)
{
    if (pTarget->VidPnSourceId == D3DDDI_ID_UNINITIALIZED)
        return;

    Assert(pTarget->VidPnSourceId < cScreens);

    PNEMUWDDM_SOURCE pSource = &paSources[pTarget->VidPnSourceId];
    if (!pSource)
        return;
    Assert(pSource->cTargets);
    Assert(ASMBitTest(pSource->aTargetMap, pTarget->u32Id));
    ASMBitClear(pSource->aTargetMap, pTarget->u32Id);
    pSource->cTargets--;
    pTarget->VidPnSourceId = D3DDDI_ID_UNINITIALIZED;

    pTarget->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
    pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
}

void NemuVidPnStSourceTargetAdd(PNEMUWDDM_SOURCE paSources, uint32_t cScreens, PNEMUWDDM_SOURCE pSource, PNEMUWDDM_TARGET pTarget)
{
    if (pTarget->VidPnSourceId == pSource->AllocData.SurfDesc.VidPnSourceId)
        return;

    NemuVidPnStTargetCleanup(paSources, cScreens, pTarget);

    ASMBitSet(pSource->aTargetMap, pTarget->u32Id);
    pSource->cTargets++;
    pTarget->VidPnSourceId = pSource->AllocData.SurfDesc.VidPnSourceId;

    pTarget->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
    pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
}

void NemuVidPnStTIterInit(PNEMUWDDM_SOURCE pSource, PNEMUWDDM_TARGET paTargets, uint32_t cTargets, NEMUWDDM_TARGET_ITER *pIter)
{
    pIter->pSource = pSource;
    pIter->paTargets = paTargets;
    pIter->cTargets = cTargets;
    pIter->i = 0;
    pIter->c = 0;
}

PNEMUWDDM_TARGET NemuVidPnStTIterNext(NEMUWDDM_TARGET_ITER *pIter)
{
    PNEMUWDDM_SOURCE pSource = pIter->pSource;
    if (pSource->cTargets <= pIter->c)
        return NULL;

    int i =  (!pIter->c) ? ASMBitFirstSet(pSource->aTargetMap, pIter->cTargets)
            : ASMBitNextSet(pSource->aTargetMap, pIter->cTargets, pIter->i);
    if (i < 0)
        STOP_FATAL();

    pIter->i = (uint32_t)i;
    pIter->c++;
    return &pIter->paTargets[i];
}

void NemuVidPnStSourceCleanup(PNEMUWDDM_SOURCE paSources, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, PNEMUWDDM_TARGET paTargets, uint32_t cTargets)
{
    PNEMUWDDM_SOURCE pSource = &paSources[VidPnSourceId];
    NEMUWDDM_TARGET_ITER Iter;
    NemuVidPnStTIterInit(pSource, paTargets, cTargets, &Iter);
    for (PNEMUWDDM_TARGET pTarget = NemuVidPnStTIterNext(&Iter);
            pTarget;
            pTarget = NemuVidPnStTIterNext(&Iter))
    {
        Assert(pTarget->VidPnSourceId == pSource->AllocData.SurfDesc.VidPnSourceId);
        NemuVidPnStTargetCleanup(paSources, cTargets, pTarget);
        /* iterator is not safe wrt target removal, reinit it */
        NemuVidPnStTIterInit(pSource, paTargets, cTargets, &Iter);
    }
}

void NemuVidPnStCleanup(PNEMUWDDM_SOURCE paSources, PNEMUWDDM_TARGET paTargets, uint32_t cScreens)
{
    for (UINT i = 0; i < cScreens; ++i)
    {
        PNEMUWDDM_TARGET pTarget = &paTargets[i];
        NemuVidPnStTargetCleanup(paSources, cScreens, pTarget);
    }
}

void NemuVidPnAllocDataInit(NEMUWDDM_ALLOC_DATA *pData, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    memset(pData, 0, sizeof (*pData));
    pData->SurfDesc.VidPnSourceId = VidPnSourceId;
    pData->Addr.offVram = NEMUVIDEOOFFSET_VOID;
}

void NemuVidPnSourceInit(PNEMUWDDM_SOURCE pSource, const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, uint8_t u8SyncState)
{
    memset(pSource, 0, sizeof (*pSource));
    NemuVidPnAllocDataInit(&pSource->AllocData, VidPnSourceId);
    pSource->u8SyncState = (u8SyncState & NEMUWDDM_HGSYNC_F_SYNCED_ALL);
}

void NemuVidPnTargetInit(PNEMUWDDM_TARGET pTarget, const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, uint8_t u8SyncState)
{
    memset(pTarget, 0, sizeof (*pTarget));
    pTarget->u32Id = VidPnTargetId;
    pTarget->VidPnSourceId = D3DDDI_ID_UNINITIALIZED;
    pTarget->u8SyncState = (u8SyncState & NEMUWDDM_HGSYNC_F_SYNCED_ALL);
}

void NemuVidPnSourcesInit(PNEMUWDDM_SOURCE pSources, uint32_t cScreens, uint8_t u8SyncState)
{
    for (uint32_t i = 0; i < cScreens; ++i)
        NemuVidPnSourceInit(&pSources[i], i, u8SyncState);
}

void NemuVidPnTargetsInit(PNEMUWDDM_TARGET pTargets, uint32_t cScreens, uint8_t u8SyncState)
{
    for (uint32_t i = 0; i < cScreens; ++i)
        NemuVidPnTargetInit(&pTargets[i], i, u8SyncState);
}

void NemuVidPnSourceCopy(NEMUWDDM_SOURCE *pDst, const NEMUWDDM_SOURCE *pSrc)
{
    uint8_t u8SyncState = pDst->u8SyncState;
    *pDst = *pSrc;
    pDst->u8SyncState &= u8SyncState;
}

void NemuVidPnTargetCopy(NEMUWDDM_TARGET *pDst, const NEMUWDDM_TARGET *pSrc)
{
    uint8_t u8SyncState = pDst->u8SyncState;
    *pDst = *pSrc;
    pDst->u8SyncState &= u8SyncState;
}

void NemuVidPnSourcesCopy(NEMUWDDM_SOURCE *pDst, const NEMUWDDM_SOURCE *pSrc, uint32_t cScreens)
{
    for (uint32_t i = 0; i < cScreens; ++i)
        NemuVidPnSourceCopy(&pDst[i], &pSrc[i]);
}

void NemuVidPnTargetsCopy(NEMUWDDM_TARGET *pDst, const NEMUWDDM_TARGET *pSrc, uint32_t cScreens)
{
    for (uint32_t i = 0; i < cScreens; ++i)
        NemuVidPnTargetCopy(&pDst[i], &pSrc[i]);
}


static D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE nemuVidPnCofuncModalityCurrentPathPivot(D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmPivot,
                    const DXGK_ENUM_PIVOT *pPivot,
                    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    switch (enmPivot)
    {
        case D3DKMDT_EPT_VIDPNSOURCE:
            if (pPivot->VidPnSourceId == VidPnSourceId)
                return D3DKMDT_EPT_VIDPNSOURCE;
            if (pPivot->VidPnSourceId == D3DDDI_ID_ALL)
            {
#ifdef DEBUG_misha
                AssertFailed();
#endif
                return D3DKMDT_EPT_VIDPNSOURCE;
            }
            return D3DKMDT_EPT_NOPIVOT;
        case D3DKMDT_EPT_VIDPNTARGET:
            if (pPivot->VidPnTargetId == VidPnTargetId)
                return D3DKMDT_EPT_VIDPNTARGET;
            if (pPivot->VidPnTargetId == D3DDDI_ID_ALL)
            {
#ifdef DEBUG_misha
                AssertFailed();
#endif
                return D3DKMDT_EPT_VIDPNTARGET;
            }
            return D3DKMDT_EPT_NOPIVOT;
        case D3DKMDT_EPT_SCALING:
        case D3DKMDT_EPT_ROTATION:
        case D3DKMDT_EPT_NOPIVOT:
            return D3DKMDT_EPT_NOPIVOT;
        default:
            WARN(("unexpected pivot"));
            return D3DKMDT_EPT_NOPIVOT;
    }
}

NTSTATUS nemuVidPnQueryPinnedTargetMode(D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, RTRECTSIZE *pSize)
{
    D3DKMDT_HVIDPNTARGETMODESET hCurVidPnTargetModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pCurVidPnTargetModeSetInterface;
    pSize->cx = 0;
    pSize->cy = 0;
    NTSTATUS Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                        VidPnTargetId,
                        &hCurVidPnTargetModeSet,
                        &pCurVidPnTargetModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnAcquireTargetModeSet failed Status(0x%x)", Status));
        return Status;
    }

    CONST D3DKMDT_VIDPN_TARGET_MODE* pPinnedVidPnTargetModeInfo;
    Status = pCurVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnTargetModeSet, &pPinnedVidPnTargetModeInfo);
    if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
    {
        pPinnedVidPnTargetModeInfo = NULL;
        Status = STATUS_SUCCESS;
    }
    else if (!NT_SUCCESS(Status))
    {
        WARN(("pfnAcquirePinnedModeInfo failed Status(0x%x)", Status));
    }
    else
    {
        Assert(pPinnedVidPnTargetModeInfo);
        pSize->cx = pPinnedVidPnTargetModeInfo->VideoSignalInfo.ActiveSize.cx;
        pSize->cy = pPinnedVidPnTargetModeInfo->VideoSignalInfo.ActiveSize.cy;
        NTSTATUS tmpStatus = pCurVidPnTargetModeSetInterface->pfnReleaseModeInfo(hCurVidPnTargetModeSet, pPinnedVidPnTargetModeInfo);
        Assert(NT_SUCCESS(tmpStatus));
    }

    NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hCurVidPnTargetModeSet);
    Assert(tmpStatus == STATUS_SUCCESS);

    return Status;
}

NTSTATUS nemuVidPnQueryPinnedSourceMode(D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, RTRECTSIZE *pSize)
{
    D3DKMDT_HVIDPNSOURCEMODESET hCurVidPnSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pCurVidPnSourceModeSetInterface;
    pSize->cx = 0;
    pSize->cy = 0;
    NTSTATUS Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                        VidPnSourceId,
                        &hCurVidPnSourceModeSet,
                        &pCurVidPnSourceModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnAcquireSourceModeSet failed Status(0x%x)", Status));
        return Status;
    }

    CONST D3DKMDT_VIDPN_SOURCE_MODE* pPinnedVidPnSourceModeInfo;
    Status = pCurVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
    if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
    {
        pPinnedVidPnSourceModeInfo = NULL;
        Status = STATUS_SUCCESS;
    }
    else if (!NT_SUCCESS(Status))
    {
        WARN(("pfnAcquirePinnedModeInfo failed Status(0x%x)", Status));
    }
    else
    {
        Assert(pPinnedVidPnSourceModeInfo);
        pSize->cx = pPinnedVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize.cx;
        pSize->cy = pPinnedVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize.cy;
        NTSTATUS tmpStatus = pCurVidPnSourceModeSetInterface->pfnReleaseModeInfo(hCurVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        Assert(NT_SUCCESS(tmpStatus));
    }

    NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hCurVidPnSourceModeSet);
    Assert(tmpStatus == STATUS_SUCCESS);

    return Status;
}

static NTSTATUS nemuVidPnSourceModeSetToArray(D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet,
                    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface,
                    CR_SORTARRAY *pArray)
{
    NEMUVIDPN_SOURCEMODE_ITER Iter;
    const D3DKMDT_VIDPN_SOURCE_MODE *pVidPnModeInfo;

    NemuVidPnSourceModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = NemuVidPnSourceModeIterNext(&Iter)) != NULL)
    {
        RTRECTSIZE size;
        size.cx = pVidPnModeInfo->Format.Graphics.VisibleRegionSize.cx;
        size.cy = pVidPnModeInfo->Format.Graphics.VisibleRegionSize.cy;
        int rc = CrSaAdd(pArray, CR_RSIZE2U64(size));
        if (RT_FAILURE(rc))
        {
            WARN(("CrSaAdd failed %d", rc));
            NemuVidPnSourceModeIterTerm(&Iter);
            return STATUS_UNSUCCESSFUL;
        }
    }

    NemuVidPnSourceModeIterTerm(&Iter);

    return NemuVidPnSourceModeIterStatus(&Iter);
}

static NTSTATUS nemuVidPnSourceModeSetFromArray(D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet,
        const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface,
        const CR_SORTARRAY *pArray)
{
    for (uint32_t i = 0; i < CrSaGetSize(pArray); ++i)
    {
        RTRECTSIZE size = CR_U642RSIZE(CrSaGetVal(pArray, i));

        D3DKMDT_VIDPN_SOURCE_MODE *pVidPnModeInfo;
        NTSTATUS Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnCreateNewModeInfo failed, Status 0x%x", Status));
            return Status;
        }

        nemuVidPnPopulateSourceModeInfo(pVidPnModeInfo, &size);

        Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAddMode failed, Status 0x%x", Status));
            NemuVidPnDumpSourceMode("SourceMode: ", pVidPnModeInfo, "\n");
            NTSTATUS tmpStatus = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
            Assert(tmpStatus == STATUS_SUCCESS);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS nemuVidPnTargetModeSetToArray(D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet,
                    const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface,
                    CR_SORTARRAY *pArray)
{
    NEMUVIDPN_TARGETMODE_ITER Iter;
    const D3DKMDT_VIDPN_TARGET_MODE *pVidPnModeInfo;

    NemuVidPnTargetModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = NemuVidPnTargetModeIterNext(&Iter)) != NULL)
    {
        RTRECTSIZE size;
        size.cx = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cx;
        size.cy = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cy;
        int rc = CrSaAdd(pArray, CR_RSIZE2U64(size));
        if (RT_FAILURE(rc))
        {
            WARN(("CrSaAdd failed %d", rc));
            NemuVidPnTargetModeIterTerm(&Iter);
            return STATUS_UNSUCCESSFUL;
        }
    }

    NemuVidPnTargetModeIterTerm(&Iter);

    return NemuVidPnTargetModeIterStatus(&Iter);
}

static NTSTATUS nemuVidPnTargetModeSetFromArray(D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet,
        const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface,
        const CR_SORTARRAY *pArray)
{
    for (uint32_t i = 0; i < CrSaGetSize(pArray); ++i)
    {
        RTRECTSIZE size = CR_U642RSIZE(CrSaGetVal(pArray, i));

        D3DKMDT_VIDPN_TARGET_MODE *pVidPnModeInfo;
        NTSTATUS Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnCreateNewModeInfo failed, Status 0x%x", Status));
            return Status;
        }

        nemuVidPnPopulateTargetModeInfo(pVidPnModeInfo, &size);

        Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAddMode failed, Status 0x%x", Status));
            NemuVidPnDumpTargetMode("TargetMode: ", pVidPnModeInfo, "\n");
            NTSTATUS tmpStatus = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
            Assert(tmpStatus == STATUS_SUCCESS);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS nemuVidPnMonitorModeSetToArray(D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet,
                    const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface,
                    CR_SORTARRAY *pArray)
{
    NEMUVIDPN_MONITORMODE_ITER Iter;
    const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo;

    NemuVidPnMonitorModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = NemuVidPnMonitorModeIterNext(&Iter)) != NULL)
    {
        RTRECTSIZE size;
        size.cx = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cx;
        size.cy = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cy;
        int rc = CrSaAdd(pArray, CR_RSIZE2U64(size));
        if (RT_FAILURE(rc))
        {
            WARN(("CrSaAdd failed %d", rc));
            NemuVidPnMonitorModeIterTerm(&Iter);
            return STATUS_UNSUCCESSFUL;
        }
    }

    NemuVidPnMonitorModeIterTerm(&Iter);

    return NemuVidPnMonitorModeIterStatus(&Iter);
}

static NTSTATUS nemuVidPnMonitorModeSetFromArray(D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet,
        const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface,
        const CR_SORTARRAY *pArray)
{
    for (uint32_t i = 0; i < CrSaGetSize(pArray); ++i)
    {
        RTRECTSIZE size = CR_U642RSIZE(CrSaGetVal(pArray, i));

        D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo;
        NTSTATUS Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnCreateNewModeInfo failed, Status 0x%x", Status));
            return Status;
        }

        nemuVidPnPopulateMonitorModeInfo(pVidPnModeInfo, &size);

        Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAddMode failed, Status 0x%x", Status));
            NTSTATUS tmpStatus = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
            Assert(tmpStatus == STATUS_SUCCESS);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}


static NTSTATUS nemuVidPnCollectInfoForPathTarget(PNEMUMP_DEVEXT pDevExt,
        D3DKMDT_HVIDPN hVidPn,
        const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot,
        uint32_t *aAdjustedModeMap,
        CR_SORTARRAY *aModes,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    const CR_SORTARRAY* pSupportedModes = NemuWddmVModesGet(pDevExt, VidPnTargetId);
    NTSTATUS Status;
    if (enmCurPivot == D3DKMDT_EPT_VIDPNTARGET)
    {
        D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet;
        const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface;
        Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                    VidPnTargetId,
                    &hVidPnModeSet,
                    &pVidPnModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAcquireTargetModeSet failed %#x", Status));
            return Status;
        }

        /* intersect modes from target */
        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Status = nemuVidPnTargetModeSetToArray(hVidPnModeSet, pVidPnModeSetInterface, &aModes[VidPnTargetId]);
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CR_SORTARRAY Arr;
            CrSaInit(&Arr, 0);
            Status = nemuVidPnTargetModeSetToArray(hVidPnModeSet, pVidPnModeSetInterface, &aModes[VidPnTargetId]);
            CrSaIntersect(&aModes[VidPnTargetId], &Arr);
            CrSaCleanup(&Arr);
        }

        NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnModeSet);
        Assert(tmpStatus == STATUS_SUCCESS);

        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVidPnTargetModeSetToArray failed %#x", Status));
            return Status;
        }

        return STATUS_SUCCESS;
    }

    RTRECTSIZE pinnedSize = {0};
    Status = nemuVidPnQueryPinnedTargetMode(hVidPn, pVidPnInterface, VidPnTargetId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnQueryPinnedTargetMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
    {
        Assert(CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)));

        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
            int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
            if (!RT_SUCCESS(rc))
            {
                WARN(("CrSaAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CrSaClear(&aModes[VidPnTargetId]);
            int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
            if (!RT_SUCCESS(rc))
            {
                WARN(("CrSaAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }
        }

        return STATUS_SUCCESS;
    }


    Status = nemuVidPnQueryPinnedSourceMode(hVidPn, pVidPnInterface, VidPnSourceId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnQueryPinnedSourceMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
    {
        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
            if (CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)))
            {
                int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
                if (!RT_SUCCESS(rc))
                {
                    WARN(("CrSaAdd failed %d", rc));
                    return STATUS_UNSUCCESSFUL;
                }
            }
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CrSaClear(&aModes[VidPnTargetId]);
            if (CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)))
            {
                int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
                if (!RT_SUCCESS(rc))
                {
                    WARN(("CrSaAdd failed %d", rc));
                    return STATUS_UNSUCCESSFUL;
                }
            }
        }

        return STATUS_SUCCESS;
    }

    /* now we are here because no pinned info is specified, we need to populate it based on the supported info
     * and modes already configured,
     * this is pretty simple actually */

    if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
    {
        Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
        int rc = CrSaClone(pSupportedModes, &aModes[VidPnTargetId]);
        if (!RT_SUCCESS(rc))
        {
            WARN(("CrSaClone failed %d", rc));
            return STATUS_UNSUCCESSFUL;
        }
        ASMBitSet(aAdjustedModeMap, VidPnTargetId);
    }
    else
    {
        CrSaIntersect(&aModes[VidPnTargetId], pSupportedModes);
    }

    /* we are done */
    return STATUS_SUCCESS;
}

static NTSTATUS nemuVidPnApplyInfoForPathTarget(PNEMUMP_DEVEXT pDevExt,
        D3DKMDT_HVIDPN hVidPn,
        const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot,
        uint32_t *aAdjustedModeMap,
        const CR_SORTARRAY *aModes,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    Assert(ASMBitTest(aAdjustedModeMap, VidPnTargetId));

    if (enmCurPivot == D3DKMDT_EPT_VIDPNTARGET)
        return STATUS_SUCCESS;

    RTRECTSIZE pinnedSize = {0};
    NTSTATUS Status = nemuVidPnQueryPinnedTargetMode(hVidPn, pVidPnInterface, VidPnTargetId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnQueryPinnedTargetMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
        return STATUS_SUCCESS;

    /* now just create the new source mode set and apply it */
    D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface;
    Status = pVidPnInterface->pfnCreateNewTargetModeSet(hVidPn,
                        VidPnTargetId,
                        &hVidPnModeSet,
                        &pVidPnModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnCreateNewTargetModeSet failed Status(0x%x)", Status));
        return Status;
    }

    Status = nemuVidPnTargetModeSetFromArray(hVidPnModeSet,
            pVidPnModeSetInterface,
            &aModes[VidPnTargetId]);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnTargetModeSetFromArray failed Status(0x%x)", Status));
        nemuVidPnDumpVidPn("\nVidPn: ---------\n", pDevExt, hVidPn, pVidPnInterface, "\n------\n");
        NemuVidPnDumpMonitorModeSet("MonModeSet: --------\n", pDevExt, VidPnTargetId, "\n------\n");
        NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnModeSet);
        Assert(tmpStatus == STATUS_SUCCESS);
        return Status;
    }

    Status = pVidPnInterface->pfnAssignTargetModeSet(hVidPn, VidPnTargetId, hVidPnModeSet);
    if (!NT_SUCCESS(Status))
    {
        WARN(("\n\n!!!!!!!\n\n pfnAssignTargetModeSet failed, Status(0x%x)", Status));
        nemuVidPnDumpVidPn("\nVidPn: ---------\n", pDevExt, hVidPn, pVidPnInterface, "\n------\n");
        NemuVidPnDumpMonitorModeSet("MonModeSet: --------\n", pDevExt, VidPnTargetId, "\n------\n");
        NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnModeSet);
        Assert(tmpStatus == STATUS_SUCCESS);
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS nemuVidPnApplyInfoForPathSource(PNEMUMP_DEVEXT pDevExt,
        D3DKMDT_HVIDPN hVidPn,
        const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot,
        uint32_t *aAdjustedModeMap,
        const CR_SORTARRAY *aModes,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    Assert(ASMBitTest(aAdjustedModeMap, VidPnTargetId));

    if (enmCurPivot == D3DKMDT_EPT_VIDPNSOURCE)
        return STATUS_SUCCESS;

    RTRECTSIZE pinnedSize = {0};
    NTSTATUS Status = nemuVidPnQueryPinnedSourceMode(hVidPn, pVidPnInterface, VidPnSourceId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnQueryPinnedSourceMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
        return STATUS_SUCCESS;

    /* now just create the new source mode set and apply it */
    D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;
    Status = pVidPnInterface->pfnCreateNewSourceModeSet(hVidPn,
                        VidPnSourceId,
                        &hVidPnModeSet,
                        &pVidPnModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnCreateNewSourceModeSet failed Status(0x%x)", Status));
        return Status;
    }

    Status = nemuVidPnSourceModeSetFromArray(hVidPnModeSet,
            pVidPnModeSetInterface,
            &aModes[VidPnTargetId]); /* <- target modes always! */
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnSourceModeSetFromArray failed Status(0x%x)", Status));
        nemuVidPnDumpVidPn("\nVidPn: ---------\n", pDevExt, hVidPn, pVidPnInterface, "\n------\n");
        NemuVidPnDumpMonitorModeSet("MonModeSet: --------\n", pDevExt, VidPnTargetId, "\n------\n");
        NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnModeSet);
        Assert(tmpStatus == STATUS_SUCCESS);
        return Status;
    }

    Status = pVidPnInterface->pfnAssignSourceModeSet(hVidPn, VidPnSourceId, hVidPnModeSet);
    if (!NT_SUCCESS(Status))
    {
        WARN(("\n\n!!!!!!!\n\n pfnAssignSourceModeSet failed, Status(0x%x)", Status));
        nemuVidPnDumpVidPn("\nVidPn: ---------\n", pDevExt, hVidPn, pVidPnInterface, "\n------\n");
        NemuVidPnDumpMonitorModeSet("MonModeSet: --------\n", pDevExt, VidPnTargetId, "\n------\n");
        NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnModeSet);
        Assert(tmpStatus == STATUS_SUCCESS);
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS nemuVidPnCollectInfoForPathSource(PNEMUMP_DEVEXT pDevExt,
        D3DKMDT_HVIDPN hVidPn,
        const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot,
        uint32_t *aAdjustedModeMap,
        CR_SORTARRAY *aModes,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    const CR_SORTARRAY* pSupportedModes = NemuWddmVModesGet(pDevExt, VidPnTargetId); /* <- yes, modes are target-determined always */
    NTSTATUS Status;

    if (enmCurPivot == D3DKMDT_EPT_VIDPNSOURCE)
    {
        D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet;
        const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;
        Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                    VidPnSourceId,
                    &hVidPnModeSet,
                    &pVidPnModeSetInterface);
        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnAcquireSourceModeSet failed %#x", Status));
            return Status;
        }

        /* intersect modes from target */
        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Status = nemuVidPnSourceModeSetToArray(hVidPnModeSet, pVidPnModeSetInterface, &aModes[VidPnTargetId]);
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CR_SORTARRAY Arr;
            CrSaInit(&Arr, 0);
            Status = nemuVidPnSourceModeSetToArray(hVidPnModeSet, pVidPnModeSetInterface, &aModes[VidPnTargetId]);
            CrSaIntersect(&aModes[VidPnTargetId], &Arr);
            CrSaCleanup(&Arr);
        }

        NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnModeSet);
        Assert(tmpStatus == STATUS_SUCCESS);

        if (!NT_SUCCESS(Status))
        {
            WARN(("pfnReleaseSourceModeSet failed %#x", Status));
            return Status;
        }

        /* intersect it with supported target modes, just in case */
        CrSaIntersect(&aModes[VidPnTargetId], pSupportedModes);

        return STATUS_SUCCESS;
    }

    RTRECTSIZE pinnedSize = {0};
    Status = nemuVidPnQueryPinnedSourceMode(hVidPn, pVidPnInterface, VidPnSourceId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnQueryPinnedSourceMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
    {
        Assert(CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)));

        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);

            if (CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)))
            {
                int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
                if (!RT_SUCCESS(rc))
                {
                    WARN(("CrSaAdd failed %d", rc));
                    return STATUS_UNSUCCESSFUL;
                }
            }
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CrSaClear(&aModes[VidPnTargetId]);
            if (CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)))
            {
                int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
                if (!RT_SUCCESS(rc))
                {
                    WARN(("CrSaAdd failed %d", rc));
                    return STATUS_UNSUCCESSFUL;
                }
            }
        }

        return STATUS_SUCCESS;
    }


    Status = nemuVidPnQueryPinnedTargetMode(hVidPn, pVidPnInterface, VidPnTargetId, &pinnedSize);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnQueryPinnedTargetMode failed %#x", Status));
        return Status;
    }

    if (pinnedSize.cx)
    {
        Assert(CrSaContains(pSupportedModes, CR_RSIZE2U64(pinnedSize)));

        if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
        {
            Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
            int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
            if (!RT_SUCCESS(rc))
            {
                WARN(("CrSaAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }
            ASMBitSet(aAdjustedModeMap, VidPnTargetId);
        }
        else
        {
            CrSaClear(&aModes[VidPnTargetId]);
            int rc = CrSaAdd(&aModes[VidPnTargetId], CR_RSIZE2U64(pinnedSize));
            if (!RT_SUCCESS(rc))
            {
                WARN(("CrSaAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }
        }

        return STATUS_SUCCESS;
    }

    /* now we are here because no pinned info is specified, we need to populate it based on the supported info
     * and modes already configured,
     * this is pretty simple actually */

    if (!ASMBitTest(aAdjustedModeMap, VidPnTargetId))
    {
        Assert(CrSaGetSize(&aModes[VidPnTargetId]) == 0);
        int rc = CrSaClone(pSupportedModes, &aModes[VidPnTargetId]);
        if (!RT_SUCCESS(rc))
        {
            WARN(("CrSaClone failed %d", rc));
            return STATUS_UNSUCCESSFUL;
        }
        ASMBitSet(aAdjustedModeMap, VidPnTargetId);
    }
    else
    {
        CrSaIntersect(&aModes[VidPnTargetId], pSupportedModes);
    }

    /* we are done */
    return STATUS_SUCCESS;
}

static NTSTATUS nemuVidPnCheckMonitorModes(PNEMUMP_DEVEXT pDevExt, uint32_t u32Target)
{
    NTSTATUS Status;
    CONST DXGK_MONITOR_INTERFACE *pMonitorInterface;
    Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryMonitorInterface(pDevExt->u.primary.DxgkInterface.DeviceHandle, DXGK_MONITOR_INTERFACE_VERSION_V1, &pMonitorInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryMonitorInterface failed, Status()0x%x", Status));
        return Status;
    }

    D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet;
    CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;

    const CR_SORTARRAY *pSupportedModes = NemuWddmVModesGet(pDevExt, u32Target);
    CR_SORTARRAY DiffModes;
    int rc = CrSaInit(&DiffModes, CrSaGetSize(pSupportedModes));
    if (!RT_SUCCESS(rc))
    {
        WARN(("CrSaInit failed"));
        return STATUS_NO_MEMORY;
    }


    Status = pMonitorInterface->pfnAcquireMonitorSourceModeSet(pDevExt->u.primary.DxgkInterface.DeviceHandle,
                                        u32Target,
                                        &hVidPnModeSet,
                                        &pVidPnModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryMonitorInterface failed, Status()0x%x", Status));
//        if (Status == STATUS_GRAPHICS_MONITOR_NOT_CONNECTED)
        CrSaCleanup(&DiffModes);
        return Status;
    }

    NEMUVIDPN_MONITORMODE_ITER Iter;
    const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo;

    rc = CrSaClone(pSupportedModes, &DiffModes);
    if (!RT_SUCCESS(rc))
    {
        WARN(("CrSaClone failed"));
        Status = STATUS_NO_MEMORY;
        goto done;
    }

    NemuVidPnMonitorModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = NemuVidPnMonitorModeIterNext(&Iter)) != NULL)
    {
        RTRECTSIZE size;
        size.cx = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cx;
        size.cy = pVidPnModeInfo->VideoSignalInfo.ActiveSize.cy;
        CrSaRemove(&DiffModes, CR_RSIZE2U64(size));
    }

    NemuVidPnMonitorModeIterTerm(&Iter);

    Status = NemuVidPnMonitorModeIterStatus(&Iter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("iter status failed %#x", Status));
        goto done;
    }

    Status = nemuVidPnMonitorModeSetFromArray(hVidPnModeSet, pVidPnModeSetInterface, &DiffModes);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnMonitorModeSetFromArray failed %#x", Status));
        goto done;
    }

done:
    NTSTATUS tmpStatus = pMonitorInterface->pfnReleaseMonitorSourceModeSet(pDevExt->u.primary.DxgkInterface.DeviceHandle, hVidPnModeSet);
    if (!NT_SUCCESS(tmpStatus))
        WARN(("pfnReleaseMonitorSourceModeSet failed tmpStatus(0x%x)", tmpStatus));

    CrSaCleanup(&DiffModes);

    return Status;
}

static NTSTATUS nemuVidPnPathAdd(D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
        D3DKMDT_VIDPN_PRESENT_PATH_IMPORTANCE enmImportance)
{
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    NTSTATUS Status = pVidPnInterface->pfnGetTopology(hVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        AssertFailed();
        return Status;
    }

    D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo;
    Status = pVidPnTopologyInterface->pfnCreateNewPathInfo(hVidPnTopology, &pNewVidPnPresentPathInfo);
    if (!NT_SUCCESS(Status))
    {
        AssertFailed();
        return Status;
    }

    pNewVidPnPresentPathInfo->VidPnSourceId = VidPnSourceId;
    pNewVidPnPresentPathInfo->VidPnTargetId = VidPnTargetId;
    pNewVidPnPresentPathInfo->ImportanceOrdinal = enmImportance;
    pNewVidPnPresentPathInfo->ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
    memset(&pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport,
            0, sizeof (pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport));
    pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Identity = 1;
    pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Centered = 0;
    pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Stretched = 0;
    pNewVidPnPresentPathInfo->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
    pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Identity = 1;
    pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate180 = 0;
    pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate270 = 0;
    pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate90 = 0;
    pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cx = 0;
    pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cy = 0;
    pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cx = 0;
    pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cy = 0;
    pNewVidPnPresentPathInfo->VidPnTargetColorBasis = D3DKMDT_CB_SRGB; /* @todo: how does it matters? */
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FirstChannel =  8;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.SecondChannel =  8;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.ThirdChannel =  8;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FourthChannel =  0;
    pNewVidPnPresentPathInfo->Content = D3DKMDT_VPPC_GRAPHICS;
    pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_UNINITIALIZED;
//                    pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_NOPROTECTION;
    pNewVidPnPresentPathInfo->CopyProtection.APSTriggerBits = 0;
    memset(&pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport, 0, sizeof (pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport));
//            pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport.NoProtection  = 1;
    memset (&pNewVidPnPresentPathInfo->GammaRamp, 0, sizeof (pNewVidPnPresentPathInfo->GammaRamp));
//            pNewVidPnPresentPathInfo->GammaRamp.Type = D3DDDI_GAMMARAMP_DEFAULT;
//            pNewVidPnPresentPathInfo->GammaRamp.DataSize = 0;
    Status = pVidPnTopologyInterface->pfnAddPath(hVidPnTopology, pNewVidPnPresentPathInfo);
    if (!NT_SUCCESS(Status))
    {
        AssertFailed();
        NTSTATUS tmpStatus = pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pNewVidPnPresentPathInfo);
        Assert(NT_SUCCESS(tmpStatus));
    }

    LOG(("Recommended Path (%d->%d)", VidPnSourceId, VidPnTargetId));

    return Status;
}

NTSTATUS NemuVidPnRecommendMonitorModes(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
                        D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet, const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface)
{
    const CR_SORTARRAY *pSupportedModes = NemuWddmVModesGet(pDevExt, VideoPresentTargetId);

    NTSTATUS Status = nemuVidPnMonitorModeSetFromArray(hVidPnModeSet, pVidPnModeSetInterface, pSupportedModes);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuVidPnMonitorModeSetFromArray failed %d", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NemuVidPnUpdateModes(PNEMUMP_DEVEXT pDevExt, uint32_t u32TargetId, const RTRECTSIZE *pSize)
{
    if (u32TargetId >= (uint32_t)NemuCommonFromDeviceExt(pDevExt)->cDisplays)
    {
        WARN(("invalid target id"));
        return STATUS_INVALID_PARAMETER;
    }

    int rc = NemuWddmVModesAdd(pDevExt, u32TargetId, pSize, TRUE);
    if (RT_FAILURE(rc))
    {
        WARN(("NemuWddmVModesAdd failed %d", rc));
        return STATUS_UNSUCCESSFUL;
    }

    if (rc == VINF_ALREADY_INITIALIZED)
    {
        /* mode was already in list, just return */
        Assert(CrSaContains(NemuWddmVModesGet(pDevExt, u32TargetId), CR_RSIZE2U64(*pSize)));
        return STATUS_SUCCESS;
    }

    /* modes have changed, need to replug */
    NTSTATUS Status = NemuWddmChildStatusReportReconnected(pDevExt, u32TargetId);
    if (!NT_SUCCESS(Status))
    {
        WARN(("NemuWddmChildStatusReportReconnected failed Status(%#x)", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NemuVidPnRecommendFunctional(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, const NEMUWDDM_RECOMMENDVIDPN *pData)
{
    const DXGK_VIDPN_INTERFACE* pVidPnInterface = NULL;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryVidPnInterface(hVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryVidPnInterface failed Status(%#x)", Status));
        return Status;
    }

    NEMUCMDVBVA_SCREENMAP_DECL(uint32_t, aVisitedSourceMap);

    memset(aVisitedSourceMap, 0, sizeof (aVisitedSourceMap));

    uint32_t Importance = (uint32_t)D3DKMDT_VPPI_PRIMARY;

    for (uint32_t i = 0; i < (uint32_t)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        int32_t iSource = pData->aTargets[i].iSource;
        if (iSource < 0)
            continue;

        if (iSource >= NemuCommonFromDeviceExt(pDevExt)->cDisplays)
        {
            WARN(("invalid iSource"));
            return STATUS_INVALID_PARAMETER;
        }

        if (!pDevExt->fComplexTopologiesEnabled && iSource != i)
        {
            WARN(("complex topologies not supported!"));
            return STATUS_INVALID_PARAMETER;
        }

        bool fNewSource = false;

        if (!ASMBitTest(aVisitedSourceMap, iSource))
        {
            int rc = NemuWddmVModesAdd(pDevExt, i, &pData->aSources[iSource].Size, TRUE);
            if (RT_FAILURE(rc))
            {
                WARN(("NemuWddmVModesAdd failed %d", rc));
                return STATUS_UNSUCCESSFUL;
            }

            Assert(CrSaContains(NemuWddmVModesGet(pDevExt, i), CR_RSIZE2U64(pData->aSources[iSource].Size)));

            Status = nemuVidPnCheckMonitorModes(pDevExt, i);
            if (!NT_SUCCESS(Status))
            {
                WARN(("nemuVidPnCheckMonitorModes failed %#x", Status));
                return Status;
            }

            ASMBitSet(aVisitedSourceMap, iSource);
            fNewSource = true;
        }

        Status = nemuVidPnPathAdd(hVidPn, pVidPnInterface,
                (const D3DDDI_VIDEO_PRESENT_SOURCE_ID)iSource, (const D3DDDI_VIDEO_PRESENT_TARGET_ID)i,
                (D3DKMDT_VIDPN_PRESENT_PATH_IMPORTANCE)Importance);
        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVidPnPathAdd failed Status()0x%x\n", Status));
            return Status;
        }

        Importance++;

        do {
            D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet;
            const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface;

            Status = pVidPnInterface->pfnCreateNewTargetModeSet(hVidPn,
                                i,
                                &hVidPnModeSet,
                                &pVidPnModeSetInterface);
            if (NT_SUCCESS(Status))
            {
                D3DKMDT_VIDPN_TARGET_MODE *pVidPnModeInfo;
                Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
                if (NT_SUCCESS(Status))
                {
                    nemuVidPnPopulateTargetModeInfo(pVidPnModeInfo, &pData->aSources[iSource].Size);

                    IN_CONST_D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID idMode = pVidPnModeInfo->Id;

                    Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
                    if (NT_SUCCESS(Status))
                    {
                        pVidPnModeInfo = NULL;

                        Status = pVidPnModeSetInterface->pfnPinMode(hVidPnModeSet, idMode);
                        if (NT_SUCCESS(Status))
                        {
                            Status = pVidPnInterface->pfnAssignTargetModeSet(hVidPn, i, hVidPnModeSet);
                            if (NT_SUCCESS(Status))
                            {
                                LOG(("Recommended Target[%d] (%dx%d)", i, pData->aSources[iSource].Size.cx, pData->aSources[iSource].Size.cy));
                                break;
                            }
                            else
                                WARN(("pfnAssignTargetModeSet failed %#x", Status));
                        }
                        else
                            WARN(("pfnPinMode failed %#x", Status));

                    }
                    else
                        WARN(("pfnAddMode failed %#x", Status));

                    if (pVidPnModeInfo)
                    {
                        NTSTATUS tmpStatus = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
                        Assert(tmpStatus == STATUS_SUCCESS);
                    }
                }
                else
                    WARN(("pfnCreateNewTargetModeSet failed %#x", Status));

                NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hVidPnModeSet);
                Assert(tmpStatus == STATUS_SUCCESS);
            }
            else
                WARN(("pfnCreateNewTargetModeSet failed %#x", Status));

            Assert(!NT_SUCCESS(Status));

            return Status;
        } while (0);

        if (fNewSource)
        {
            do {
                D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet;
                const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;

                Status = pVidPnInterface->pfnCreateNewSourceModeSet(hVidPn,
                                    iSource,
                                    &hVidPnModeSet,
                                    &pVidPnModeSetInterface);
                if (NT_SUCCESS(Status))
                {
                    D3DKMDT_VIDPN_SOURCE_MODE *pVidPnModeInfo;
                    Status = pVidPnModeSetInterface->pfnCreateNewModeInfo(hVidPnModeSet, &pVidPnModeInfo);
                    if (NT_SUCCESS(Status))
                    {
                        nemuVidPnPopulateSourceModeInfo(pVidPnModeInfo, &pData->aSources[iSource].Size);

                        IN_CONST_D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID idMode = pVidPnModeInfo->Id;

                        Status = pVidPnModeSetInterface->pfnAddMode(hVidPnModeSet, pVidPnModeInfo);
                        if (NT_SUCCESS(Status))
                        {
                            pVidPnModeInfo = NULL;

                            Status = pVidPnModeSetInterface->pfnPinMode(hVidPnModeSet, idMode);
                            if (NT_SUCCESS(Status))
                            {
                                Status = pVidPnInterface->pfnAssignSourceModeSet(hVidPn, iSource, hVidPnModeSet);
                                if (NT_SUCCESS(Status))
                                {
                                    LOG(("Recommended Source[%d] (%dx%d)", iSource, pData->aSources[iSource].Size.cx, pData->aSources[iSource].Size.cy));
                                    break;
                                }
                                else
                                    WARN(("pfnAssignSourceModeSet failed %#x", Status));
                            }
                            else
                                WARN(("pfnPinMode failed %#x", Status));

                        }
                        else
                            WARN(("pfnAddMode failed %#x", Status));

                        if (pVidPnModeInfo)
                        {
                            NTSTATUS tmpStatus = pVidPnModeSetInterface->pfnReleaseModeInfo(hVidPnModeSet, pVidPnModeInfo);
                            Assert(tmpStatus == STATUS_SUCCESS);
                        }
                    }
                    else
                        WARN(("pfnCreateNewSourceModeSet failed %#x", Status));

                    NTSTATUS tmpStatus = pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hVidPnModeSet);
                    Assert(tmpStatus == STATUS_SUCCESS);
                }
                else
                    WARN(("pfnCreateNewSourceModeSet failed %#x", Status));

                Assert(!NT_SUCCESS(Status));

                return Status;
            } while (0);
        }
    }

    Assert(NT_SUCCESS(Status));
    return STATUS_SUCCESS;
}

static BOOLEAN nemuVidPnIsPathSupported(PNEMUMP_DEVEXT pDevExt, const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo)
{
    if (!pDevExt->fComplexTopologiesEnabled && pNewVidPnPresentPathInfo->VidPnSourceId != pNewVidPnPresentPathInfo->VidPnTargetId)
    {
        LOG(("unsupported source(%d)->target(%d) pair", pNewVidPnPresentPathInfo->VidPnSourceId, pNewVidPnPresentPathInfo->VidPnTargetId));
        return FALSE;
    }

    /*
    ImportanceOrdinal does not matter for now
    pNewVidPnPresentPathInfo->ImportanceOrdinal
    */

    if (pNewVidPnPresentPathInfo->ContentTransformation.Scaling != D3DKMDT_VPPS_UNPINNED
            && pNewVidPnPresentPathInfo->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY
            && pNewVidPnPresentPathInfo->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED)
    {
        WARN(("unsupported Scaling (%d)", pNewVidPnPresentPathInfo->ContentTransformation.Scaling));
        return FALSE;
    }

    if (    !pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Identity
         || pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Centered
         || pNewVidPnPresentPathInfo->ContentTransformation.ScalingSupport.Stretched)
    {
        WARN(("unsupported Scaling support"));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->ContentTransformation.Rotation != D3DKMDT_VPPR_UNPINNED
            && pNewVidPnPresentPathInfo->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY
            && pNewVidPnPresentPathInfo->ContentTransformation.Rotation != D3DKMDT_VPPR_NOTSPECIFIED)
    {
        WARN(("unsupported rotation (%d)", pNewVidPnPresentPathInfo->ContentTransformation.Rotation));
        return FALSE;
    }

    if (    !pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Identity
         || pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate90
         || pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate180
         || pNewVidPnPresentPathInfo->ContentTransformation.RotationSupport.Rotate270)
    {
        WARN(("unsupported RotationSupport"));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cx
            || pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cy)
    {
        WARN(("Non-zero TLOffset: cx(%d), cy(%d)",
                pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cx,
                pNewVidPnPresentPathInfo->VisibleFromActiveTLOffset.cy));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cx
            || pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cy)
    {
        WARN(("Non-zero TLOffset: cx(%d), cy(%d)",
                pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cx,
                pNewVidPnPresentPathInfo->VisibleFromActiveBROffset.cy));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->VidPnTargetColorBasis != D3DKMDT_CB_SRGB
            && pNewVidPnPresentPathInfo->VidPnTargetColorBasis != D3DKMDT_CB_UNINITIALIZED)
    {
        WARN(("unsupported VidPnTargetColorBasis (%d)", pNewVidPnPresentPathInfo->VidPnTargetColorBasis));
        return FALSE;
    }

    /* channels?
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FirstChannel;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.SecondChannel;
    pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.ThirdChannel;
    we definitely not support fourth channel
    */
    if (pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FourthChannel)
    {
        WARN(("Non-zero FourthChannel (%d)", pNewVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges.FourthChannel));
        return FALSE;
    }

    /* Content (D3DKMDT_VPPC_GRAPHICS, _NOTSPECIFIED, _VIDEO), does not matter for now
    pNewVidPnPresentPathInfo->Content
    */
    /* not support copy protection for now */
    if (pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType != D3DKMDT_VPPMT_NOPROTECTION
            && pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType != D3DKMDT_VPPMT_UNINITIALIZED)
    {
        WARN(("Copy protection not supported CopyProtectionType(%d)", pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionType));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->CopyProtection.APSTriggerBits)
    {
        WARN(("Copy protection not supported APSTriggerBits(%d)", pNewVidPnPresentPathInfo->CopyProtection.APSTriggerBits));
        return FALSE;
    }

    D3DKMDT_VIDPN_PRESENT_PATH_COPYPROTECTION_SUPPORT tstCPSupport = {0};
    tstCPSupport.NoProtection = 1;
    if (memcmp(&tstCPSupport, &pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport, sizeof(tstCPSupport)))
    {
        WARN(("Copy protection support (0x%x)", *((UINT*)&pNewVidPnPresentPathInfo->CopyProtection.CopyProtectionSupport)));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->GammaRamp.Type != D3DDDI_GAMMARAMP_DEFAULT
            && pNewVidPnPresentPathInfo->GammaRamp.Type != D3DDDI_GAMMARAMP_UNINITIALIZED)
    {
        WARN(("Unsupported GammaRamp.Type (%d)", pNewVidPnPresentPathInfo->GammaRamp.Type));
        return FALSE;
    }

    if (pNewVidPnPresentPathInfo->GammaRamp.DataSize != 0)
    {
        WARN(("Warning: non-zero GammaRamp.DataSize (%d), treating as supported", pNewVidPnPresentPathInfo->GammaRamp.DataSize));
    }

    return TRUE;
}

NTSTATUS NemuVidPnIsSupported(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, BOOLEAN *pfSupported)
{
    *pfSupported = FALSE;

    const DXGK_VIDPN_INTERFACE* pVidPnInterface = NULL;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryVidPnInterface(hVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryVidPnInterface failed Status()0x%x\n", Status));
        return Status;
    }

#ifdef NEMUWDDM_DEBUG_VIDPN
    nemuVidPnDumpVidPn(">>>>IsSupported VidPN (IN) : >>>>\n", pDevExt, hVidPn, pVidPnInterface, "<<<<<<<<<<<<<<<<<<<<\n");
#endif

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    Status = pVidPnInterface->pfnGetTopology(hVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnGetTopology failed Status()0x%x\n", Status));
        return Status;
    }

    NEMUVIDPN_PATH_ITER PathIter;
    const D3DKMDT_VIDPN_PRESENT_PATH * pPath;
    NEMUCMDVBVA_SCREENMAP_DECL(uint32_t, aVisitedTargetMap);

    memset(aVisitedTargetMap, 0, sizeof (aVisitedTargetMap));

    BOOLEAN fSupported = TRUE;
    /* collect info first */
    NemuVidPnPathIterInit(&PathIter, hVidPnTopology, pVidPnTopologyInterface);
    while ((pPath = NemuVidPnPathIterNext(&PathIter)) != NULL)
    {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId = pPath->VidPnSourceId;
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId = pPath->VidPnTargetId;
        /* actually vidpn topology should contain only one target info, right? */
        Assert(!ASMBitTest(aVisitedTargetMap, VidPnTargetId));
        ASMBitSet(aVisitedTargetMap, VidPnTargetId);

        if (!nemuVidPnIsPathSupported(pDevExt, pPath))
        {
            fSupported = FALSE;
            break;
        }

        RTRECTSIZE TargetSize;
        RTRECTSIZE SourceSize;
        Status = nemuVidPnQueryPinnedTargetMode(hVidPn, pVidPnInterface, VidPnTargetId, &TargetSize);
        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVidPnQueryPinnedTargetMode failed %#x", Status));
            break;
        }

        Status = nemuVidPnQueryPinnedSourceMode(hVidPn, pVidPnInterface, VidPnSourceId, &SourceSize);
        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVidPnQueryPinnedSourceMode failed %#x", Status));
            break;
        }

        if (memcmp(&TargetSize, &SourceSize, sizeof (TargetSize)) && TargetSize.cx)
        {
            if (!SourceSize.cx)
                WARN(("not expected?"));

            fSupported = FALSE;
            break;
        }
    }

    NemuVidPnPathIterTerm(&PathIter);

    if (!NT_SUCCESS(Status))
        goto done;

    Status = NemuVidPnPathIterStatus(&PathIter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("PathIter failed Status()0x%x\n", Status));
        goto done;
    }

    *pfSupported = fSupported;
done:

    return Status;
}

NTSTATUS NemuVidPnCofuncModality(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmPivot, const DXGK_ENUM_PIVOT *pPivot)
{
    const DXGK_VIDPN_INTERFACE* pVidPnInterface = NULL;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryVidPnInterface(hVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryVidPnInterface failed Status()0x%x\n", Status));
        return Status;
    }

#ifdef NEMUWDDM_DEBUG_VIDPN
    nemuVidPnDumpCofuncModalityArg(">>>>MODALITY Args: ", pEnumCofuncModalityArg, "\n");
    nemuVidPnDumpVidPn(">>>>MODALITY VidPN (IN) : >>>>\n", pDevExt, pEnumCofuncModalityArg->hConstrainingVidPn, pVidPnInterface, "<<<<<<<<<<<<<<<<<<<<\n");
#endif

    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    Status = pVidPnInterface->pfnGetTopology(hVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnGetTopology failed Status()0x%x\n", Status));
        return Status;
    }

    NEMUVIDPN_PATH_ITER PathIter;
    const D3DKMDT_VIDPN_PRESENT_PATH * pPath;
    NEMUCMDVBVA_SCREENMAP_DECL(uint32_t, aVisitedTargetMap);
    NEMUCMDVBVA_SCREENMAP_DECL(uint32_t, aAdjustedModeMap);
    CR_SORTARRAY aModes[NEMU_VIDEO_MAX_SCREENS];

    memset(aVisitedTargetMap, 0, sizeof (aVisitedTargetMap));
    memset(aAdjustedModeMap, 0, sizeof (aAdjustedModeMap));
    memset(aModes, 0, sizeof (aModes));

    /* collect info first */
    NemuVidPnPathIterInit(&PathIter, hVidPnTopology, pVidPnTopologyInterface);
    while ((pPath = NemuVidPnPathIterNext(&PathIter)) != NULL)
    {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId = pPath->VidPnSourceId;
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId = pPath->VidPnTargetId;
        /* actually vidpn topology should contain only one target info, right? */
        Assert(!ASMBitTest(aVisitedTargetMap, VidPnTargetId));
        ASMBitSet(aVisitedTargetMap, VidPnTargetId);

        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot = nemuVidPnCofuncModalityCurrentPathPivot(enmPivot, pPivot, VidPnSourceId, VidPnTargetId);

        Status = nemuVidPnCollectInfoForPathTarget(pDevExt,
                hVidPn,
                pVidPnInterface,
                enmCurPivot,
                aAdjustedModeMap,
                aModes,
                VidPnSourceId, VidPnTargetId);
        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVidPnCollectInfoForPathTarget failed Status(0x%x\n", Status));
            NemuVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
            break;
        }

        Assert(CrSaCovers(NemuWddmVModesGet(pDevExt, VidPnTargetId), &aModes[VidPnTargetId]));

        Status = nemuVidPnCollectInfoForPathSource(pDevExt,
                hVidPn,
                pVidPnInterface,
                enmCurPivot,
                aAdjustedModeMap,
                aModes,
                VidPnSourceId, VidPnTargetId);
        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVidPnCollectInfoForPathSource failed Status(0x%x\n", Status));
            NemuVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
            break;
        }

        Assert(CrSaCovers(NemuWddmVModesGet(pDevExt, VidPnTargetId), &aModes[VidPnTargetId]));
    }

    NemuVidPnPathIterTerm(&PathIter);

    if (!NT_SUCCESS(Status))
        goto done;

    Status = NemuVidPnPathIterStatus(&PathIter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("PathIter failed Status()0x%x\n", Status));
        NemuVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
        goto done;
    }

    /* now we have collected all the necessary info,
     * go ahead and apply it */
    memset(aVisitedTargetMap, 0, sizeof (aVisitedTargetMap));
    NemuVidPnPathIterInit(&PathIter, hVidPnTopology, pVidPnTopologyInterface);
    while ((pPath = NemuVidPnPathIterNext(&PathIter)) != NULL)
    {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId = pPath->VidPnSourceId;
        D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId = pPath->VidPnTargetId;
        /* actually vidpn topology should contain only one target info, right? */
        Assert(!ASMBitTest(aVisitedTargetMap, VidPnTargetId));
        ASMBitSet(aVisitedTargetMap, VidPnTargetId);

        D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmCurPivot = nemuVidPnCofuncModalityCurrentPathPivot(enmPivot, pPivot, VidPnSourceId, VidPnTargetId);

        bool bUpdatePath = false;
        D3DKMDT_VIDPN_PRESENT_PATH AdjustedPath = {0};
        AdjustedPath.VidPnSourceId = pPath->VidPnSourceId;
        AdjustedPath.VidPnTargetId = pPath->VidPnTargetId;
        AdjustedPath.ContentTransformation = pPath->ContentTransformation;
        AdjustedPath.CopyProtection = pPath->CopyProtection;

        if (pPath->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED)
        {
            AdjustedPath.ContentTransformation.ScalingSupport.Identity = TRUE;
            bUpdatePath = true;
        }

        if (pPath->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED)
        {
            AdjustedPath.ContentTransformation.RotationSupport.Identity = TRUE;
            bUpdatePath = true;
        }

        if (bUpdatePath)
        {
            Status = pVidPnTopologyInterface->pfnUpdatePathSupportInfo(hVidPnTopology, &AdjustedPath);
            if (!NT_SUCCESS(Status))
            {
                WARN(("pfnUpdatePathSupportInfo failed Status()0x%x\n", Status));
                NemuVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
                goto done;
            }
        }

        Assert(CrSaCovers(NemuWddmVModesGet(pDevExt, VidPnTargetId), &aModes[VidPnTargetId]));

        Status = nemuVidPnApplyInfoForPathTarget(pDevExt,
                hVidPn,
                pVidPnInterface,
                enmCurPivot,
                aAdjustedModeMap,
                aModes,
                VidPnSourceId, VidPnTargetId);
        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVidPnApplyInfoForPathTarget failed Status(0x%x\n", Status));
            NemuVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
            break;
        }

        Status = nemuVidPnApplyInfoForPathSource(pDevExt,
                hVidPn,
                pVidPnInterface,
                enmCurPivot,
                aAdjustedModeMap,
                aModes,
                VidPnSourceId, VidPnTargetId);
        if (!NT_SUCCESS(Status))
        {
            WARN(("nemuVidPnApplyInfoForPathSource failed Status(0x%x\n", Status));
            NemuVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
            break;
        }
    }

    NemuVidPnPathIterTerm(&PathIter);

    if (!NT_SUCCESS(Status))
        goto done;

    Status = NemuVidPnPathIterStatus(&PathIter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("PathIter failed Status()0x%x\n", Status));
        NemuVidPnDumpCofuncModalityInfo("Modality Info: ", enmPivot, pPivot, "\n");
        goto done;
    }

done:

    for (uint32_t i = 0; i < (uint32_t)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        CrSaCleanup(&aModes[i]);
    }

    return Status;
}

NTSTATUS nemuVidPnEnumMonitorSourceModes(D3DKMDT_HMONITORSOURCEMODESET hMonitorSMS, CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSMSIf,
        PFNNEMUVIDPNENUMMONITORSOURCEMODES pfnCallback, PVOID pContext)
{
    CONST D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSMI;
    NTSTATUS Status = pMonitorSMSIf->pfnAcquireFirstModeInfo(hMonitorSMS, &pMonitorSMI);
    Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY);
    if (Status == STATUS_SUCCESS)
    {
        Assert(pMonitorSMI);
        while (1)
        {
            CONST D3DKMDT_MONITOR_SOURCE_MODE *pNextMonitorSMI;
            Status = pMonitorSMSIf->pfnAcquireNextModeInfo(hMonitorSMS, pMonitorSMI, &pNextMonitorSMI);
            if (!pfnCallback(hMonitorSMS, pMonitorSMSIf, pMonitorSMI, pContext))
            {
                Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET);
                if (Status == STATUS_SUCCESS)
                    pMonitorSMSIf->pfnReleaseModeInfo(hMonitorSMS, pNextMonitorSMI);
                else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
                {
                    Status = STATUS_SUCCESS;
                    break;
                }
                else
                {
                    LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x), ignored since callback returned false", Status));
                    Status = STATUS_SUCCESS;
                }
                break;
            }
            else if (Status == STATUS_SUCCESS)
                pMonitorSMI = pNextMonitorSMI;
            else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            else
            {
                AssertBreakpoint();
                LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x)", Status));
                pNextMonitorSMI = NULL;
                break;
            }
        }
    }
    else if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        Status = STATUS_SUCCESS;
    else
        LOGREL(("pfnAcquireFirstModeInfo failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS nemuVidPnEnumSourceModes(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
                                    PFNNEMUVIDPNENUMSOURCEMODES pfnCallback, PVOID pContext)
{
    const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo;
    NTSTATUS Status = pVidPnSourceModeSetInterface->pfnAcquireFirstModeInfo(hNewVidPnSourceModeSet, &pNewVidPnSourceModeInfo);
    if (Status == STATUS_SUCCESS)
    {
        Assert(pNewVidPnSourceModeInfo);
        while (1)
        {
            const D3DKMDT_VIDPN_SOURCE_MODE *pNextVidPnSourceModeInfo;
            Status = pVidPnSourceModeSetInterface->pfnAcquireNextModeInfo(hNewVidPnSourceModeSet, pNewVidPnSourceModeInfo, &pNextVidPnSourceModeInfo);
            if (!pfnCallback(hNewVidPnSourceModeSet, pVidPnSourceModeSetInterface,
                    pNewVidPnSourceModeInfo, pContext))
            {
                Assert(Status == STATUS_SUCCESS);
                if (Status == STATUS_SUCCESS)
                    pVidPnSourceModeSetInterface->pfnReleaseModeInfo(hNewVidPnSourceModeSet, pNextVidPnSourceModeInfo);
                else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
                {
                    Status = STATUS_SUCCESS;
                    break;
                }
                else
                {
                    LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x), ignored since callback returned false", Status));
                    Status = STATUS_SUCCESS;
                }

                break;
            }
            else if (Status == STATUS_SUCCESS)
                pNewVidPnSourceModeInfo = pNextVidPnSourceModeInfo;
            else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            else
            {
                AssertBreakpoint();
                LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x)", Status));
                pNewVidPnSourceModeInfo = NULL;
                break;
            }
        }
    }
    else if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        Status = STATUS_SUCCESS;
    else
        LOGREL(("pfnAcquireFirstModeInfo failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS nemuVidPnEnumTargetModes(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        PFNNEMUVIDPNENUMTARGETMODES pfnCallback, PVOID pContext)
{
    const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo;
    NTSTATUS Status = pVidPnTargetModeSetInterface->pfnAcquireFirstModeInfo(hNewVidPnTargetModeSet, &pNewVidPnTargetModeInfo);
    if (Status == STATUS_SUCCESS)
    {
        Assert(pNewVidPnTargetModeInfo);
        while (1)
        {
            const D3DKMDT_VIDPN_TARGET_MODE *pNextVidPnTargetModeInfo;
            Status = pVidPnTargetModeSetInterface->pfnAcquireNextModeInfo(hNewVidPnTargetModeSet, pNewVidPnTargetModeInfo, &pNextVidPnTargetModeInfo);
            if (!pfnCallback(hNewVidPnTargetModeSet, pVidPnTargetModeSetInterface,
                    pNewVidPnTargetModeInfo, pContext))
            {
                Assert(Status == STATUS_SUCCESS);
                if (Status == STATUS_SUCCESS)
                    pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hNewVidPnTargetModeSet, pNextVidPnTargetModeInfo);
                else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
                {
                    Status = STATUS_SUCCESS;
                    break;
                }
                else
                {
                    LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x), ignored since callback returned false", Status));
                    Status = STATUS_SUCCESS;
                }

                break;
            }
            else if (Status == STATUS_SUCCESS)
                pNewVidPnTargetModeInfo = pNextVidPnTargetModeInfo;
            else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            else
            {
                AssertBreakpoint();
                LOGREL(("pfnAcquireNextModeInfo Failed Status(0x%x)", Status));
                pNewVidPnTargetModeInfo = NULL;
                break;
            }
        }
    }
    else if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        Status = STATUS_SUCCESS;
    else
        LOGREL(("pfnAcquireFirstModeInfo failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS nemuVidPnEnumTargetsForSource(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        PFNNEMUVIDPNENUMTARGETSFORSOURCE pfnCallback, PVOID pContext)
{
    SIZE_T cTgtPaths;
    NTSTATUS Status = pVidPnTopologyInterface->pfnGetNumPathsFromSource(hVidPnTopology, VidPnSourceId, &cTgtPaths);
    Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY);
    if (Status == STATUS_SUCCESS)
    {
        for (SIZE_T i = 0; i < cTgtPaths; ++i)
        {
            D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
            Status = pVidPnTopologyInterface->pfnEnumPathTargetsFromSource(hVidPnTopology, VidPnSourceId, i, &VidPnTargetId);
            Assert(Status == STATUS_SUCCESS);
            if (Status == STATUS_SUCCESS)
            {
                if (!pfnCallback(pDevExt, hVidPnTopology, pVidPnTopologyInterface, VidPnSourceId, VidPnTargetId, cTgtPaths, pContext))
                    break;
            }
            else
            {
                LOGREL(("pfnEnumPathTargetsFromSource failed Status(0x%x)", Status));
                break;
            }
        }
    }
    else if (Status != STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
        LOGREL(("pfnGetNumPathsFromSource failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS nemuVidPnEnumPaths(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        PFNNEMUVIDPNENUMPATHS pfnCallback, PVOID pContext)
{
    const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo = NULL;
    NTSTATUS Status = pVidPnTopologyInterface->pfnAcquireFirstPathInfo(hVidPnTopology, &pNewVidPnPresentPathInfo);
    if (Status == STATUS_SUCCESS)
    {
        while (1)
        {
            const D3DKMDT_VIDPN_PRESENT_PATH *pNextVidPnPresentPathInfo;
            Status = pVidPnTopologyInterface->pfnAcquireNextPathInfo(hVidPnTopology, pNewVidPnPresentPathInfo, &pNextVidPnPresentPathInfo);

            if (!pfnCallback(hVidPnTopology, pVidPnTopologyInterface, pNewVidPnPresentPathInfo, pContext))
            {
                if (Status == STATUS_SUCCESS)
                    pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pNextVidPnPresentPathInfo);
                else
                {
                    if (Status != STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
                        WARN(("pfnAcquireNextPathInfo Failed Status(0x%x), ignored since callback returned false", Status));
                    Status = STATUS_SUCCESS;
                }

                break;
            }
            else if (Status == STATUS_SUCCESS)
                pNewVidPnPresentPathInfo = pNextVidPnPresentPathInfo;
            else if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
            {
                Status = STATUS_SUCCESS;
                break;
            }
            else
            {
                WARN(("pfnAcquireNextPathInfo Failed Status(0x%x)", Status));
                pNewVidPnPresentPathInfo = NULL;
                break;
            }
        }
    }
    else if (Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        Status = STATUS_SUCCESS;
    else
        WARN(("pfnAcquireFirstModeInfo failed Status(0x%x)", Status));

    return Status;
}

NTSTATUS nemuVidPnSetupSourceInfo(PNEMUMP_DEVEXT pDevExt, CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo, PNEMUWDDM_ALLOCATION pAllocation,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId, NEMUWDDM_SOURCE *paSources)
{
    PNEMUWDDM_SOURCE pSource = &paSources[VidPnSourceId];
    /* pVidPnSourceModeInfo could be null if STATUS_GRAPHICS_MODE_NOT_PINNED,
     * see NemuVidPnCommitSourceModeForSrcId */
    uint8_t fChanges = 0;
    if (pVidPnSourceModeInfo)
    {
        if (pSource->AllocData.SurfDesc.width != pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx)
        {
            fChanges |= NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.width = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cx;
        }
        if (pSource->AllocData.SurfDesc.height != pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy)
        {
            fChanges |= NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.height = pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy;
        }
        if (pSource->AllocData.SurfDesc.format != pVidPnSourceModeInfo->Format.Graphics.PixelFormat)
        {
            fChanges |= NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.format = pVidPnSourceModeInfo->Format.Graphics.PixelFormat;
        }
        if (pSource->AllocData.SurfDesc.bpp != nemuWddmCalcBitsPerPixel(pVidPnSourceModeInfo->Format.Graphics.PixelFormat))
        {
            fChanges |= NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.bpp = nemuWddmCalcBitsPerPixel(pVidPnSourceModeInfo->Format.Graphics.PixelFormat);
        }
        if(pSource->AllocData.SurfDesc.pitch != pVidPnSourceModeInfo->Format.Graphics.Stride)
        {
            fChanges |= NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.pitch = pVidPnSourceModeInfo->Format.Graphics.Stride;
        }
        pSource->AllocData.SurfDesc.depth = 1;
        if (pSource->AllocData.SurfDesc.slicePitch != pVidPnSourceModeInfo->Format.Graphics.Stride)
        {
            fChanges |= NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.slicePitch = pVidPnSourceModeInfo->Format.Graphics.Stride;
        }
        if (pSource->AllocData.SurfDesc.cbSize != pVidPnSourceModeInfo->Format.Graphics.Stride * pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy)
        {
            fChanges |= NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;
            pSource->AllocData.SurfDesc.cbSize = pVidPnSourceModeInfo->Format.Graphics.Stride * pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize.cy;
        }
#ifdef NEMU_WDDM_WIN8
        if (g_NemuDisplayOnly)
        {
            nemuWddmDmSetupDefaultVramLocation(pDevExt, VidPnSourceId, paSources);
        }
#endif
    }
    else
    {
        NemuVidPnAllocDataInit(&pSource->AllocData, VidPnSourceId);
        Assert(!pAllocation);
        fChanges |= NEMUWDDM_HGSYNC_F_SYNCED_ALL;
    }

#ifdef NEMU_WDDM_WIN8
    Assert(!g_NemuDisplayOnly || !pAllocation);
    if (!g_NemuDisplayOnly)
#endif
    {
        nemuWddmAssignPrimary(pSource, pAllocation, VidPnSourceId);
    }

    Assert(pSource->AllocData.SurfDesc.VidPnSourceId == VidPnSourceId);
    pSource->u8SyncState &= ~fChanges;
    return STATUS_SUCCESS;
}

NTSTATUS nemuVidPnCommitSourceMode(PNEMUMP_DEVEXT pDevExt, CONST D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo, PNEMUWDDM_ALLOCATION pAllocation,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId, NEMUWDDM_SOURCE *paSources)
{
    if (VidPnSourceId < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays)
        return nemuVidPnSetupSourceInfo(pDevExt, pVidPnSourceModeInfo, pAllocation, VidPnSourceId, paSources);

    WARN(("invalid srcId (%d), cSources(%d)", VidPnSourceId, NemuCommonFromDeviceExt(pDevExt)->cDisplays));
    return STATUS_INVALID_PARAMETER;
}

typedef struct NEMUVIDPNCOMMITTARGETMODE
{
    NTSTATUS Status;
    D3DKMDT_HVIDPN hVidPn;
    const DXGK_VIDPN_INTERFACE* pVidPnInterface;
    NEMUWDDM_SOURCE *paSources;
    NEMUWDDM_TARGET *paTargets;
} NEMUVIDPNCOMMITTARGETMODE;

DECLCALLBACK(BOOLEAN) nemuVidPnCommitTargetModeEnum(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, SIZE_T cTgtPaths, PVOID pContext)
{
    NEMUVIDPNCOMMITTARGETMODE *pInfo = (NEMUVIDPNCOMMITTARGETMODE*)pContext;
    Assert(cTgtPaths <= (SIZE_T)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
    D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet;
    CONST DXGK_VIDPNTARGETMODESET_INTERFACE* pVidPnTargetModeSetInterface;
    NTSTATUS Status = pInfo->pVidPnInterface->pfnAcquireTargetModeSet(pInfo->hVidPn, VidPnTargetId, &hVidPnTargetModeSet, &pVidPnTargetModeSetInterface);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        CONST D3DKMDT_VIDPN_TARGET_MODE* pPinnedVidPnTargetModeInfo;
        Status = pVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hVidPnTargetModeSet, &pPinnedVidPnTargetModeInfo);
        Assert(Status == STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
        {
            NEMUWDDM_SOURCE *pSource = &pInfo->paSources[VidPnSourceId];
            NEMUWDDM_TARGET *pTarget = &pInfo->paTargets[VidPnTargetId];
            pTarget->Size.cx = pPinnedVidPnTargetModeInfo->VideoSignalInfo.ActiveSize.cx;
            pTarget->Size.cy = pPinnedVidPnTargetModeInfo->VideoSignalInfo.TotalSize.cy;

            NemuVidPnStSourceTargetAdd(pInfo->paSources, NemuCommonFromDeviceExt(pDevExt)->cDisplays, pSource, pTarget);

            pTarget->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS;

            pVidPnTargetModeSetInterface->pfnReleaseModeInfo(hVidPnTargetModeSet, pPinnedVidPnTargetModeInfo);
        }
        else
            WARN(("pfnAcquirePinnedModeInfo failed Status(0x%x)", Status));

        pInfo->pVidPnInterface->pfnReleaseTargetModeSet(pInfo->hVidPn, hVidPnTargetModeSet);
    }
    else
        WARN(("pfnAcquireTargetModeSet failed Status(0x%x)", Status));

    pInfo->Status = Status;
    return Status == STATUS_SUCCESS;
}

NTSTATUS NemuVidPnCommitSourceModeForSrcId(PNEMUMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        PNEMUWDDM_ALLOCATION pAllocation,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId, NEMUWDDM_SOURCE *paSources, NEMUWDDM_TARGET *paTargets)
{
    D3DKMDT_HVIDPNSOURCEMODESET hCurVidPnSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pCurVidPnSourceModeSetInterface;

    PNEMUWDDM_SOURCE pSource = &paSources[VidPnSourceId];
    NEMUWDDM_TARGET_ITER Iter;
    NemuVidPnStTIterInit(pSource, paTargets, (uint32_t)NemuCommonFromDeviceExt(pDevExt)->cDisplays, &Iter);
    for (PNEMUWDDM_TARGET pTarget = NemuVidPnStTIterNext(&Iter);
            pTarget;
            pTarget = NemuVidPnStTIterNext(&Iter))
    {
        Assert(pTarget->VidPnSourceId == pSource->AllocData.SurfDesc.VidPnSourceId);
        pTarget->Size.cx = 0;
        pTarget->Size.cy = 0;
        pTarget->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_ALL;
    }

    NemuVidPnStSourceCleanup(paSources, VidPnSourceId, paTargets, (uint32_t)NemuCommonFromDeviceExt(pDevExt)->cDisplays);

    NTSTATUS Status = pVidPnInterface->pfnAcquireSourceModeSet(hDesiredVidPn,
                VidPnSourceId,
                &hCurVidPnSourceModeSet,
                &pCurVidPnSourceModeSetInterface);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        CONST D3DKMDT_VIDPN_SOURCE_MODE* pPinnedVidPnSourceModeInfo;
        Status = pCurVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
        Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_MODE_NOT_PINNED);
        if (Status == STATUS_SUCCESS)
        {
            Assert(pPinnedVidPnSourceModeInfo);
            Status = nemuVidPnCommitSourceMode(pDevExt, pPinnedVidPnSourceModeInfo, pAllocation, VidPnSourceId, paSources);
            Assert(Status == STATUS_SUCCESS);
            if (Status == STATUS_SUCCESS)
            {
                D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
                CONST DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
                Status = pVidPnInterface->pfnGetTopology(hDesiredVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
                Assert(Status == STATUS_SUCCESS);
                if (Status == STATUS_SUCCESS)
                {
                    NEMUVIDPNCOMMITTARGETMODE TgtModeInfo = {0};
                    TgtModeInfo.Status = STATUS_SUCCESS; /* <- to ensure we're succeeded if no targets are set */
                    TgtModeInfo.hVidPn = hDesiredVidPn;
                    TgtModeInfo.pVidPnInterface = pVidPnInterface;
                    TgtModeInfo.paSources = paSources;
                    TgtModeInfo.paTargets = paTargets;
                    Status = nemuVidPnEnumTargetsForSource(pDevExt, hVidPnTopology, pVidPnTopologyInterface,
                            VidPnSourceId,
                            nemuVidPnCommitTargetModeEnum, &TgtModeInfo);
                    Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY);
                    if (Status == STATUS_SUCCESS)
                    {
                        Status = TgtModeInfo.Status;
                        Assert(Status == STATUS_SUCCESS);
                    }
                    else if (Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY)
                    {
                        Status = STATUS_SUCCESS;
                    }
                    else
                        WARN(("nemuVidPnEnumTargetsForSource failed Status(0x%x)", Status));
                }
                else
                    WARN(("pfnGetTopology failed Status(0x%x)", Status));
            }
            else
                WARN(("nemuVidPnCommitSourceMode failed Status(0x%x)", Status));
            /* release */
            pCurVidPnSourceModeSetInterface->pfnReleaseModeInfo(hCurVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        }
        else if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
        {
            Status = nemuVidPnCommitSourceMode(pDevExt, NULL, pAllocation, VidPnSourceId, paSources);
            Assert(Status == STATUS_SUCCESS);
        }
        else
            WARN(("pfnAcquirePinnedModeInfo failed Status(0x%x)", Status));

        pVidPnInterface->pfnReleaseSourceModeSet(hDesiredVidPn, hCurVidPnSourceModeSet);
    }
    else
    {
        WARN(("pfnAcquireSourceModeSet failed Status(0x%x)", Status));
    }

    return Status;
}

NTSTATUS NemuVidPnCommitAll(PNEMUMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        PNEMUWDDM_ALLOCATION pAllocation,
        NEMUWDDM_SOURCE *paSources, NEMUWDDM_TARGET *paTargets)
{
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    NTSTATUS Status = pVidPnInterface->pfnGetTopology(hDesiredVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnGetTopology failed Status 0x%x", Status));
        return Status;
    }

    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        PNEMUWDDM_TARGET pTarget = &paTargets[i];
        pTarget->Size.cx = 0;
        pTarget->Size.cy = 0;
        pTarget->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_ALL;

        if (pTarget->VidPnSourceId == D3DDDI_ID_UNINITIALIZED)
            continue;

        Assert(pTarget->VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays);

        NEMUWDDM_SOURCE *pSource = &paSources[pTarget->VidPnSourceId];
        NemuVidPnAllocDataInit(&pSource->AllocData, pTarget->VidPnSourceId);
        pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_ALL;
    }

    NemuVidPnStCleanup(paSources, paTargets, NemuCommonFromDeviceExt(pDevExt)->cDisplays);

    NEMUVIDPN_PATH_ITER PathIter;
    const D3DKMDT_VIDPN_PRESENT_PATH *pPath;
    NemuVidPnPathIterInit(&PathIter, hVidPnTopology, pVidPnTopologyInterface);
    while ((pPath = NemuVidPnPathIterNext(&PathIter)) != NULL)
    {
        Status = NemuVidPnCommitSourceModeForSrcId(pDevExt, hDesiredVidPn, pVidPnInterface, pAllocation,
                    pPath->VidPnSourceId, paSources, paTargets);
        if (Status != STATUS_SUCCESS)
        {
            WARN(("NemuVidPnCommitSourceModeForSrcId failed Status(0x%x)", Status));
            break;
        }
    }

    NemuVidPnPathIterTerm(&PathIter);

    if (!NT_SUCCESS(Status))
    {
        WARN((""));
        return Status;
    }

    Status = NemuVidPnPathIterStatus(&PathIter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("NemuVidPnPathIterStatus failed Status 0x%x", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

#define NEMUVIDPNDUMP_STRCASE(_t) \
        case _t: return #_t;
#define NEMUVIDPNDUMP_STRCASE_UNKNOWN() \
        default: Assert(0); return "Unknown";

#define NEMUVIDPNDUMP_STRFLAGS(_v, _t) \
        if ((_v)._t return #_t;

const char* nemuVidPnDumpStrImportance(D3DKMDT_VIDPN_PRESENT_PATH_IMPORTANCE ImportanceOrdinal)
{
    switch (ImportanceOrdinal)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_PRIMARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_SECONDARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_TERTIARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_QUATERNARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_QUINARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_SENARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_SEPTENARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_OCTONARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_NONARY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPI_DENARY);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrScaling(D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling)
{
    switch (Scaling)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPS_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPS_IDENTITY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPS_CENTERED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPS_STRETCHED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPS_UNPINNED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPS_NOTSPECIFIED);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrRotation(D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
{
    switch (Rotation)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPR_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPR_IDENTITY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPR_ROTATE90);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPR_ROTATE180);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPR_ROTATE270);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPR_UNPINNED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPR_NOTSPECIFIED);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrColorBasis(const D3DKMDT_COLOR_BASIS ColorBasis)
{
    switch (ColorBasis)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_CB_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_CB_INTENSITY);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_CB_SRGB);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_CB_SCRGB);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_CB_YCBCR);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_CB_YPBPR);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char * nemuVidPnDumpStrMonCapabilitiesOrigin(D3DKMDT_MONITOR_CAPABILITIES_ORIGIN enmOrigin)
{
    switch (enmOrigin)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MCO_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MCO_DEFAULTMONITORPROFILE);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MCO_MONITORDESCRIPTOR);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MCO_MONITORDESCRIPTOR_REGISTRYOVERRIDE);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MCO_SPECIFICCAP_REGISTRYOVERRIDE);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MCO_DRIVER);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrPvam(D3DKMDT_PIXEL_VALUE_ACCESS_MODE PixelValueAccessMode)
{
    switch (PixelValueAccessMode)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_PVAM_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_PVAM_DIRECT);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_PVAM_PRESETPALETTE);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_PVAM_SETTABLEPALETTE);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}



const char* nemuVidPnDumpStrContent(D3DKMDT_VIDPN_PRESENT_PATH_CONTENT Content)
{
    switch (Content)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPC_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPC_GRAPHICS);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPC_VIDEO);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPC_NOTSPECIFIED);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrCopyProtectionType(D3DKMDT_VIDPN_PRESENT_PATH_COPYPROTECTION_TYPE CopyProtectionType)
{
    switch (CopyProtectionType)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPMT_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPMT_NOPROTECTION);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPMT_MACROVISION_APSTRIGGER);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VPPMT_MACROVISION_FULLSUPPORT);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrGammaRampType(D3DDDI_GAMMARAMP_TYPE Type)
{
    switch (Type)
    {
        NEMUVIDPNDUMP_STRCASE(D3DDDI_GAMMARAMP_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DDDI_GAMMARAMP_DEFAULT);
        NEMUVIDPNDUMP_STRCASE(D3DDDI_GAMMARAMP_RGB256x3x16);
        NEMUVIDPNDUMP_STRCASE(D3DDDI_GAMMARAMP_DXGI_1);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrSourceModeType(D3DKMDT_VIDPN_SOURCE_MODE_TYPE Type)
{
    switch (Type)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_RMT_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_RMT_GRAPHICS);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_RMT_TEXT);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrScanLineOrdering(D3DDDI_VIDEO_SIGNAL_SCANLINE_ORDERING ScanLineOrdering)
{
    switch (ScanLineOrdering)
    {
        NEMUVIDPNDUMP_STRCASE(D3DDDI_VSSLO_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DDDI_VSSLO_PROGRESSIVE);
        NEMUVIDPNDUMP_STRCASE(D3DDDI_VSSLO_INTERLACED_UPPERFIELDFIRST);
        NEMUVIDPNDUMP_STRCASE(D3DDDI_VSSLO_INTERLACED_LOWERFIELDFIRST);
        NEMUVIDPNDUMP_STRCASE(D3DDDI_VSSLO_OTHER);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrCFMPivotType(D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE EnumPivotType)
{
    switch (EnumPivotType)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_EPT_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_EPT_VIDPNSOURCE);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_EPT_VIDPNTARGET);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_EPT_SCALING);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_EPT_ROTATION);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_EPT_NOPIVOT);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrModePreference(D3DKMDT_MODE_PREFERENCE Preference)
{
    switch (Preference)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MP_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MP_PREFERRED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_MP_NOTPREFERRED);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrSignalStandard(D3DKMDT_VIDEO_SIGNAL_STANDARD VideoStandard)
{
    switch (VideoStandard)
    {
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_UNINITIALIZED);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_VESA_DMT);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_VESA_GTF);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_VESA_CVT);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_IBM);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_APPLE);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_NTSC_M);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_NTSC_J);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_NTSC_443);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_B);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_B1);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_G);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_H);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_I);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_D);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_N);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_NC);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_B);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_D);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_G);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_H);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_K);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_K1);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_L);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_SECAM_L1);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_EIA_861);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_EIA_861A);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_EIA_861B);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_K);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_K1);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_L);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_PAL_M);
        NEMUVIDPNDUMP_STRCASE(D3DKMDT_VSS_OTHER);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

const char* nemuVidPnDumpStrPixFormat(D3DDDIFORMAT PixelFormat)
{
    switch (PixelFormat)
    {
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_UNKNOWN);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_R8G8B8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A8R8G8B8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_X8R8G8B8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_R5G6B5);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_X1R5G5B5);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A1R5G5B5);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A4R4G4B4);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_R3G3B2);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A8R3G3B2);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_X4R4G4B4);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A2B10G10R10);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A8B8G8R8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_X8B8G8R8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_G16R16);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A2R10G10B10);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A16B16G16R16);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A8P8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_R32F);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_G32R32F);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A32B32G32R32F);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_CxV8U8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A1);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_BINARYBUFFER);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_VERTEXDATA);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_INDEX16);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_INDEX32);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_Q16W16V16U16);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_MULTI2_ARGB8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_R16F);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_G16R16F);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A16B16G16R16F);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D32F_LOCKABLE);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D24FS8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D32_LOCKABLE);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_S8_LOCKABLE);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_S1D15);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_S8D24);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_X8D24);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_X4S4D24);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_L16);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_UYVY);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_R8G8_B8G8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_YUY2);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_G8R8_G8B8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_DXT1);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_DXT2);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_DXT3);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_DXT4);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_DXT5);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D16_LOCKABLE);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D32);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D15S1);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D24S8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D24X8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D24X4S4);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_D16);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_P8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_L8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A8L8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A4L4);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_V8U8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_L6V5U5);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_X8L8V8U8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_Q8W8V8U8);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_V16U16);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_W11V11U10);
        NEMUVIDPNDUMP_STRCASE(D3DDDIFMT_A2W10V10U10);
        NEMUVIDPNDUMP_STRCASE_UNKNOWN();
    }
}

void nemuVidPnDumpCopyProtectoin(const char *pPrefix, const D3DKMDT_VIDPN_PRESENT_PATH_COPYPROTECTION *pCopyProtection, const char *pSuffix)
{
    LOGREL_EXACT(("%sType(%s), TODO%s", pPrefix,
            nemuVidPnDumpStrCopyProtectionType(pCopyProtection->CopyProtectionType), pSuffix));
}


void nemuVidPnDumpPathTransformation(const D3DKMDT_VIDPN_PRESENT_PATH_TRANSFORMATION *pContentTransformation)
{
    LOGREL_EXACT(("  --Transformation: Scaling(%s), ScalingSupport(%d), Rotation(%s), RotationSupport(%d)--",
            nemuVidPnDumpStrScaling(pContentTransformation->Scaling), pContentTransformation->ScalingSupport,
            nemuVidPnDumpStrRotation(pContentTransformation->Rotation), pContentTransformation->RotationSupport));
}

void nemuVidPnDumpRegion(const char *pPrefix, const D3DKMDT_2DREGION *pRegion, const char *pSuffix)
{
    LOGREL_EXACT(("%s%dX%d%s", pPrefix, pRegion->cx, pRegion->cy, pSuffix));
}

void nemuVidPnDumpRational(const char *pPrefix, const D3DDDI_RATIONAL *pRational, const char *pSuffix)
{
    LOGREL_EXACT(("%s%d/%d=%d%s", pPrefix, pRational->Numerator, pRational->Denominator, pRational->Numerator/pRational->Denominator, pSuffix));
}

void nemuVidPnDumpRanges(const char *pPrefix, const D3DKMDT_COLOR_COEFF_DYNAMIC_RANGES *pDynamicRanges, const char *pSuffix)
{
    LOGREL_EXACT(("%sFirstChannel(%d), SecondChannel(%d), ThirdChannel(%d), FourthChannel(%d)%s", pPrefix,
            pDynamicRanges->FirstChannel,
            pDynamicRanges->SecondChannel,
            pDynamicRanges->ThirdChannel,
            pDynamicRanges->FourthChannel,
            pSuffix));
}

void nemuVidPnDumpGammaRamp(const char *pPrefix, const D3DKMDT_GAMMA_RAMP *pGammaRamp, const char *pSuffix)
{
    LOGREL_EXACT(("%sType(%s), DataSize(%d), TODO: dump the rest%s", pPrefix,
            nemuVidPnDumpStrGammaRampType(pGammaRamp->Type), pGammaRamp->DataSize,
            pSuffix));
}

void NemuVidPnDumpSourceMode(const char *pPrefix, const D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo, const char *pSuffix)
{
    LOGREL_EXACT(("%sType(%s), ", pPrefix, nemuVidPnDumpStrSourceModeType(pVidPnSourceModeInfo->Type)));
    nemuVidPnDumpRegion("surf(", &pVidPnSourceModeInfo->Format.Graphics.PrimSurfSize, "), ");
    nemuVidPnDumpRegion("vis(", &pVidPnSourceModeInfo->Format.Graphics.VisibleRegionSize, "), ");
    LOGREL_EXACT(("stride(%d), ", pVidPnSourceModeInfo->Format.Graphics.Stride));
    LOGREL_EXACT(("format(%s), ", nemuVidPnDumpStrPixFormat(pVidPnSourceModeInfo->Format.Graphics.PixelFormat)));
    LOGREL_EXACT(("clrBasis(%s), ", nemuVidPnDumpStrColorBasis(pVidPnSourceModeInfo->Format.Graphics.ColorBasis)));
    LOGREL_EXACT(("pvam(%s)%s", nemuVidPnDumpStrPvam(pVidPnSourceModeInfo->Format.Graphics.PixelValueAccessMode), pSuffix));
}

void nemuVidPnDumpSignalInfo(const char *pPrefix, const D3DKMDT_VIDEO_SIGNAL_INFO *pVideoSignalInfo, const char *pSuffix)
{
    LOGREL_EXACT(("%sVStd(%s), ", pPrefix, nemuVidPnDumpStrSignalStandard(pVideoSignalInfo->VideoStandard)));
    nemuVidPnDumpRegion("totSize(", &pVideoSignalInfo->TotalSize, "), ");
    nemuVidPnDumpRegion("activeSize(", &pVideoSignalInfo->ActiveSize, "), ");
    nemuVidPnDumpRational("VSynch(", &pVideoSignalInfo->VSyncFreq, "), ");
    LOGREL_EXACT(("PixelRate(%d), ScanLineOrdering(%s)%s", pVideoSignalInfo->PixelRate, nemuVidPnDumpStrScanLineOrdering(pVideoSignalInfo->ScanLineOrdering), pSuffix));
}

void NemuVidPnDumpTargetMode(const char *pPrefix, const D3DKMDT_VIDPN_TARGET_MODE* CONST  pVidPnTargetModeInfo, const char *pSuffix)
{
    LOGREL_EXACT(("%s", pPrefix));
    LOGREL_EXACT(("ID: %d, ", pVidPnTargetModeInfo->Id));
    nemuVidPnDumpSignalInfo("VSI: ", &pVidPnTargetModeInfo->VideoSignalInfo, ", ");
    LOGREL_EXACT(("Preference(%s)%s", nemuVidPnDumpStrModePreference(pVidPnTargetModeInfo->Preference), pSuffix));
}

void NemuVidPnDumpMonitorMode(const char *pPrefix, const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo, const char *pSuffix)
{
    LOGREL_EXACT(("%s", pPrefix));

    LOGREL_EXACT(("ID: %d, ", pVidPnModeInfo->Id));

    nemuVidPnDumpSignalInfo("VSI: ", &pVidPnModeInfo->VideoSignalInfo, ", ");

    LOGREL_EXACT(("ColorBasis: %s, ", nemuVidPnDumpStrColorBasis(pVidPnModeInfo->ColorBasis)));

    nemuVidPnDumpRanges("Ranges: ", &pVidPnModeInfo->ColorCoeffDynamicRanges, ", ");

    LOGREL_EXACT(("MonCapOr: %s, ", nemuVidPnDumpStrMonCapabilitiesOrigin(pVidPnModeInfo->Origin)));

    LOGREL_EXACT(("Preference(%s)%s", nemuVidPnDumpStrModePreference(pVidPnModeInfo->Preference), pSuffix));
}

NTSTATUS NemuVidPnDumpMonitorModeSet(const char *pPrefix, PNEMUMP_DEVEXT pDevExt, uint32_t u32Target, const char *pSuffix)
{
    LOGREL_EXACT(("%s Tgt[%d]\n", pPrefix, u32Target));

    NTSTATUS Status;
    CONST DXGK_MONITOR_INTERFACE *pMonitorInterface;
    Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryMonitorInterface(pDevExt->u.primary.DxgkInterface.DeviceHandle, DXGK_MONITOR_INTERFACE_VERSION_V1, &pMonitorInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryMonitorInterface failed, Status()0x%x", Status));
        return Status;
    }

    D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet;
    CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;

    Status = pMonitorInterface->pfnAcquireMonitorSourceModeSet(pDevExt->u.primary.DxgkInterface.DeviceHandle,
                                        u32Target,
                                        &hVidPnModeSet,
                                        &pVidPnModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbQueryMonitorInterface failed, Status()0x%x", Status));
        return Status;
    }

    NEMUVIDPN_MONITORMODE_ITER Iter;
    const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo;

    NemuVidPnMonitorModeIterInit(&Iter, hVidPnModeSet, pVidPnModeSetInterface);

    while ((pVidPnModeInfo = NemuVidPnMonitorModeIterNext(&Iter)) != NULL)
    {
        NemuVidPnDumpMonitorMode("MonitorMode: ",pVidPnModeInfo, "\n");
    }

    NemuVidPnMonitorModeIterTerm(&Iter);

    Status = NemuVidPnMonitorModeIterStatus(&Iter);
    if (!NT_SUCCESS(Status))
    {
        WARN(("iter status failed %#x", Status));
    }

    NTSTATUS tmpStatus = pMonitorInterface->pfnReleaseMonitorSourceModeSet(pDevExt->u.primary.DxgkInterface.DeviceHandle, hVidPnModeSet);
    if (!NT_SUCCESS(tmpStatus))
        WARN(("pfnReleaseMonitorSourceModeSet failed tmpStatus(0x%x)", tmpStatus));

    LOGREL_EXACT(("%s", pSuffix));

    return Status;
}

void nemuVidPnDumpPinnedSourceMode(const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    D3DKMDT_HVIDPNSOURCEMODESET hCurVidPnSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pCurVidPnSourceModeSetInterface;

    NTSTATUS Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                        VidPnSourceId,
                        &hCurVidPnSourceModeSet,
                        &pCurVidPnSourceModeSetInterface);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        CONST D3DKMDT_VIDPN_SOURCE_MODE* pPinnedVidPnSourceModeInfo;

        Status = pCurVidPnSourceModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnSourceModeSet, &pPinnedVidPnSourceModeInfo);
        Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_MODE_NOT_PINNED);
        if (Status == STATUS_SUCCESS)
        {
            NemuVidPnDumpSourceMode("Source Pinned: ", pPinnedVidPnSourceModeInfo, "\n");
            pCurVidPnSourceModeSetInterface->pfnReleaseModeInfo(hCurVidPnSourceModeSet, pPinnedVidPnSourceModeInfo);
        }
        else if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
        {
            LOGREL_EXACT(("Source NOT Pinned\n"));
        }
        else
        {
            LOGREL_EXACT(("ERROR getting piined Source Mode(0x%x)\n", Status));
        }
        pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hCurVidPnSourceModeSet);
    }
    else
    {
        LOGREL_EXACT(("ERROR getting SourceModeSet(0x%x)\n", Status));
    }
}


DECLCALLBACK(BOOLEAN) nemuVidPnDumpSourceModeSetEnum(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo, PVOID pContext)
{
    NemuVidPnDumpSourceMode("SourceMode: ", pNewVidPnSourceModeInfo, "\n");
    return TRUE;
}

void nemuVidPnDumpSourceModeSet(PNEMUMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    LOGREL_EXACT(("  >>>+++SourceMode Set for Source(%d)+++\n", VidPnSourceId));
    D3DKMDT_HVIDPNSOURCEMODESET hCurVidPnSourceModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pCurVidPnSourceModeSetInterface;

    NTSTATUS Status = pVidPnInterface->pfnAcquireSourceModeSet(hVidPn,
                        VidPnSourceId,
                        &hCurVidPnSourceModeSet,
                        &pCurVidPnSourceModeSetInterface);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {

        Status = nemuVidPnEnumSourceModes(hCurVidPnSourceModeSet, pCurVidPnSourceModeSetInterface,
                nemuVidPnDumpSourceModeSetEnum, NULL);
        Assert(Status == STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
        {
            LOGREL_EXACT(("ERROR enumerating Source Modes(0x%x)\n", Status));
        }
        pVidPnInterface->pfnReleaseSourceModeSet(hVidPn, hCurVidPnSourceModeSet);
    }
    else
    {
        LOGREL_EXACT(("ERROR getting SourceModeSet for Source(%d), Status(0x%x)\n", VidPnSourceId, Status));
    }

    LOGREL_EXACT(("  <<<+++End Of SourceMode Set for Source(%d)+++", VidPnSourceId));
}

DECLCALLBACK(BOOLEAN) nemuVidPnDumpTargetModeSetEnum(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, PVOID pContext)
{
    NemuVidPnDumpTargetMode("TargetMode: ", pNewVidPnTargetModeInfo, "\n");
    return TRUE;
}

void nemuVidPnDumpTargetModeSet(PNEMUMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    LOGREL_EXACT(("  >>>---TargetMode Set for Target(%d)---\n", VidPnTargetId));
    D3DKMDT_HVIDPNTARGETMODESET hCurVidPnTargetModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pCurVidPnTargetModeSetInterface;

    NTSTATUS Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                        VidPnTargetId,
                        &hCurVidPnTargetModeSet,
                        &pCurVidPnTargetModeSetInterface);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {

        Status = nemuVidPnEnumTargetModes(hCurVidPnTargetModeSet, pCurVidPnTargetModeSetInterface,
                nemuVidPnDumpTargetModeSetEnum, NULL);
        Assert(Status == STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
        {
            LOGREL_EXACT(("ERROR enumerating Target Modes(0x%x)\n", Status));
        }
        pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hCurVidPnTargetModeSet);
    }
    else
    {
        LOGREL_EXACT(("ERROR getting TargetModeSet for Target(%d), Status(0x%x)\n", VidPnTargetId, Status));
    }

    LOGREL_EXACT(("  <<<---End Of TargetMode Set for Target(%d)---", VidPnTargetId));
}


void nemuVidPnDumpPinnedTargetMode(const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    D3DKMDT_HVIDPNTARGETMODESET hCurVidPnTargetModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pCurVidPnTargetModeSetInterface;

    NTSTATUS Status = pVidPnInterface->pfnAcquireTargetModeSet(hVidPn,
                        VidPnTargetId,
                        &hCurVidPnTargetModeSet,
                        &pCurVidPnTargetModeSetInterface);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        CONST D3DKMDT_VIDPN_TARGET_MODE* pPinnedVidPnTargetModeInfo;

        Status = pCurVidPnTargetModeSetInterface->pfnAcquirePinnedModeInfo(hCurVidPnTargetModeSet, &pPinnedVidPnTargetModeInfo);
        Assert(Status == STATUS_SUCCESS || Status == STATUS_GRAPHICS_MODE_NOT_PINNED);
        if (Status == STATUS_SUCCESS)
        {
            NemuVidPnDumpTargetMode("Target Pinned: ", pPinnedVidPnTargetModeInfo, "\n");
            pCurVidPnTargetModeSetInterface->pfnReleaseModeInfo(hCurVidPnTargetModeSet, pPinnedVidPnTargetModeInfo);
        }
        else if (Status == STATUS_GRAPHICS_MODE_NOT_PINNED)
        {
            LOGREL_EXACT(("Target NOT Pinned\n"));
        }
        else
        {
            LOGREL_EXACT(("ERROR getting piined Target Mode(0x%x)\n", Status));
        }
        pVidPnInterface->pfnReleaseTargetModeSet(hVidPn, hCurVidPnTargetModeSet);
    }
    else
    {
        LOGREL_EXACT(("ERROR getting TargetModeSet(0x%x)\n", Status));
    }
}

void NemuVidPnDumpCofuncModalityInfo(const char *pPrefix, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmEnumPivotType, const DXGK_ENUM_PIVOT *pPivot, const char *pSuffix)
{
    LOGREL_EXACT(("%sPivotType(%s), SourceId(0x%x), TargetId(0x%x),%s", pPrefix, nemuVidPnDumpStrCFMPivotType(enmEnumPivotType),
            pPivot->VidPnSourceId, pPivot->VidPnTargetId, pSuffix));
}

void nemuVidPnDumpCofuncModalityArg(const char *pPrefix, CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModalityArg, const char *pSuffix)
{
    LOGREL_EXACT(("%sPivotType(%s), SourceId(0x%x), TargetId(0x%x),%s", pPrefix, nemuVidPnDumpStrCFMPivotType(pEnumCofuncModalityArg->EnumPivotType),
            pEnumCofuncModalityArg->EnumPivot.VidPnSourceId, pEnumCofuncModalityArg->EnumPivot.VidPnTargetId, pSuffix));
}

void nemuVidPnDumpPath(const D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, const D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPathInfo)
{
    LOGREL_EXACT((" >>**** Start Dump VidPn Path ****>>\n"));
    LOGREL_EXACT(("VidPnSourceId(%d),  VidPnTargetId(%d)\n",
            pVidPnPresentPathInfo->VidPnSourceId, pVidPnPresentPathInfo->VidPnTargetId));

    nemuVidPnDumpPinnedSourceMode(hVidPn, pVidPnInterface, pVidPnPresentPathInfo->VidPnSourceId);
    nemuVidPnDumpPinnedTargetMode(hVidPn, pVidPnInterface, pVidPnPresentPathInfo->VidPnTargetId);

    nemuVidPnDumpPathTransformation(&pVidPnPresentPathInfo->ContentTransformation);

    LOGREL_EXACT(("Importance(%s), TargetColorBasis(%s), Content(%s), ",
            nemuVidPnDumpStrImportance(pVidPnPresentPathInfo->ImportanceOrdinal),
            nemuVidPnDumpStrColorBasis(pVidPnPresentPathInfo->VidPnTargetColorBasis),
            nemuVidPnDumpStrContent(pVidPnPresentPathInfo->Content)));
    nemuVidPnDumpRegion("VFA_TL_O(", &pVidPnPresentPathInfo->VisibleFromActiveTLOffset, "), ");
    nemuVidPnDumpRegion("VFA_BR_O(", &pVidPnPresentPathInfo->VisibleFromActiveBROffset, "), ");
    nemuVidPnDumpRanges("CCDynamicRanges: ", &pVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges, "| ");
    nemuVidPnDumpCopyProtectoin("CProtection: ", &pVidPnPresentPathInfo->CopyProtection, "| ");
    nemuVidPnDumpGammaRamp("GammaRamp: ", &pVidPnPresentPathInfo->GammaRamp, "\n");

    LOGREL_EXACT((" <<**** Stop Dump VidPn Path ****<<"));
}

typedef struct NEMUVIDPNDUMPPATHENUM
{
    D3DKMDT_HVIDPN hVidPn;
    const DXGK_VIDPN_INTERFACE* pVidPnInterface;
} NEMUVIDPNDUMPPATHENUM, *PNEMUVIDPNDUMPPATHENUM;

static DECLCALLBACK(BOOLEAN) nemuVidPnDumpPathEnum(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        const D3DKMDT_VIDPN_PRESENT_PATH *pVidPnPresentPathInfo, PVOID pContext)
{
    PNEMUVIDPNDUMPPATHENUM pData = (PNEMUVIDPNDUMPPATHENUM)pContext;
    nemuVidPnDumpPath(pData->hVidPn, pData->pVidPnInterface, pVidPnPresentPathInfo);

    pVidPnTopologyInterface->pfnReleasePathInfo(hVidPnTopology, pVidPnPresentPathInfo);
    return TRUE;
}

void nemuVidPnDumpVidPn(const char * pPrefix, PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, const char * pSuffix)
{
    LOGREL_EXACT(("%s", pPrefix));

    NEMUVIDPNDUMPPATHENUM CbData;
    CbData.hVidPn = hVidPn;
    CbData.pVidPnInterface = pVidPnInterface;
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    NTSTATUS Status = pVidPnInterface->pfnGetTopology(hVidPn, &hVidPnTopology, &pVidPnTopologyInterface);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Status = nemuVidPnEnumPaths(hVidPnTopology, pVidPnTopologyInterface,
                                        nemuVidPnDumpPathEnum, &CbData);
        Assert(Status == STATUS_SUCCESS);
    }

    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        nemuVidPnDumpSourceModeSet(pDevExt, hVidPn, pVidPnInterface, (D3DDDI_VIDEO_PRESENT_SOURCE_ID)i);
        nemuVidPnDumpTargetModeSet(pDevExt, hVidPn, pVidPnInterface, (D3DDDI_VIDEO_PRESENT_TARGET_ID)i);
    }

    LOGREL_EXACT(("%s", pSuffix));
}
