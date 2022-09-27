/* $Id: NemuMPWddm.cpp $ */
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
#include "common/NemuMPCommon.h"
#include "common/NemuMPHGSMI.h"
#include "NemuMPVhwa.h"
#include "NemuMPVidPn.h"

#include <iprt/asm.h>
//#include <iprt/initterm.h>

#include <Nemu/NemuGuestLib.h>
#include <Nemu/NemuVideo.h>
#include <wingdi.h> /* needed for RGNDATA definition */
#include <NemuDisplay.h> /* this is from Additions/WINNT/include/ to include escape codes */
#include <Nemu/Hardware/NemuVideoVBE.h>
#include <Nemu/Version.h>

#include <stdio.h>

/* Uncomment this in order to enable dumping regions guest wants to display on DxgkDdiPresentNew(). */
//#define NEMU_WDDM_DUMP_REGIONS_ON_PRESENT

#define NEMUWDDM_DUMMY_DMABUFFER_SIZE (sizeof (NEMUCMDVBVA_HDR) / 2)

DWORD g_NemuLogUm = 0;
#ifdef NEMU_WDDM_WIN8
DWORD g_NemuDisplayOnly = 0;
#endif

#define NEMUWDDM_MEMTAG 'MDBV'
PVOID nemuWddmMemAlloc(IN SIZE_T cbSize)
{
    return ExAllocatePoolWithTag(NonPagedPool, cbSize, NEMUWDDM_MEMTAG);
}

PVOID nemuWddmMemAllocZero(IN SIZE_T cbSize)
{
    PVOID pvMem = nemuWddmMemAlloc(cbSize);
    if (pvMem)
        memset(pvMem, 0, cbSize);
    return pvMem;
}


VOID nemuWddmMemFree(PVOID pvMem)
{
    ExFreePool(pvMem);
}

DECLINLINE(void) NemuWddmOaHostIDReleaseLocked(PNEMUWDDM_OPENALLOCATION pOa)
{
    Assert(pOa->cHostIDRefs);
    PNEMUWDDM_ALLOCATION pAllocation = pOa->pAllocation;
    Assert(pAllocation->AllocData.cHostIDRefs >= pOa->cHostIDRefs);
    Assert(pAllocation->AllocData.hostID);
    --pOa->cHostIDRefs;
    --pAllocation->AllocData.cHostIDRefs;
    if (!pAllocation->AllocData.cHostIDRefs)
        pAllocation->AllocData.hostID = 0;
}

DECLINLINE(void) NemuWddmOaHostIDCheckReleaseLocked(PNEMUWDDM_OPENALLOCATION pOa)
{
    if (pOa->cHostIDRefs)
        NemuWddmOaHostIDReleaseLocked(pOa);
}

DECLINLINE(void) NemuWddmOaRelease(PNEMUWDDM_OPENALLOCATION pOa)
{
    PNEMUWDDM_ALLOCATION pAllocation = pOa->pAllocation;
    KIRQL OldIrql;
    KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
    Assert(pAllocation->cOpens);
    NemuWddmOaHostIDCheckReleaseLocked(pOa);
    --pAllocation->cOpens;
    uint32_t cOpens = --pOa->cOpens;
    Assert(cOpens < UINT32_MAX/2);
    if (!cOpens)
    {
        RemoveEntryList(&pOa->ListEntry);
        KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
        nemuWddmMemFree(pOa);
    }
    else
        KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
}

DECLINLINE(PNEMUWDDM_OPENALLOCATION) NemuWddmOaSearchLocked(PNEMUWDDM_DEVICE pDevice, PNEMUWDDM_ALLOCATION pAllocation)
{
    for (PLIST_ENTRY pCur = pAllocation->OpenList.Flink; pCur != &pAllocation->OpenList; pCur = pCur->Flink)
    {
        PNEMUWDDM_OPENALLOCATION pCurOa = CONTAINING_RECORD(pCur, NEMUWDDM_OPENALLOCATION, ListEntry);
        if (pCurOa->pDevice == pDevice)
        {
            return pCurOa;
        }
    }
    return NULL;
}

DECLINLINE(PNEMUWDDM_OPENALLOCATION) NemuWddmOaSearch(PNEMUWDDM_DEVICE pDevice, PNEMUWDDM_ALLOCATION pAllocation)
{
    PNEMUWDDM_OPENALLOCATION pOa;
    KIRQL OldIrql;
    KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
    pOa = NemuWddmOaSearchLocked(pDevice, pAllocation);
    KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
    return pOa;
}

DECLINLINE(int) NemuWddmOaSetHostID(PNEMUWDDM_DEVICE pDevice, PNEMUWDDM_ALLOCATION pAllocation, uint32_t hostID, uint32_t *pHostID)
{
    PNEMUWDDM_OPENALLOCATION pOa;
    KIRQL OldIrql;
    int rc = VINF_SUCCESS;
    KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
    pOa = NemuWddmOaSearchLocked(pDevice, pAllocation);
    if (!pOa)
    {
        KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);;
        WARN(("no open allocation!"));
        return VERR_INVALID_STATE;
    }

    if (hostID)
    {
        if (pAllocation->AllocData.hostID == 0)
        {
            pAllocation->AllocData.hostID = hostID;
        }
        else if (pAllocation->AllocData.hostID != hostID)
        {
            WARN(("hostID differ: alloc(%d), trying to assign(%d)", pAllocation->AllocData.hostID, hostID));
            hostID = pAllocation->AllocData.hostID;
            rc = VERR_NOT_EQUAL;
        }

        ++pAllocation->AllocData.cHostIDRefs;
        ++pOa->cHostIDRefs;
    }
    else
        NemuWddmOaHostIDCheckReleaseLocked(pOa);

    KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);

    if (pHostID)
        *pHostID = hostID;

    return rc;
}

DECLINLINE(PNEMUWDDM_ALLOCATION) nemuWddmGetAllocationFromHandle(PNEMUMP_DEVEXT pDevExt, D3DKMT_HANDLE hAllocation)
{
    DXGKARGCB_GETHANDLEDATA GhData;
    GhData.hObject = hAllocation;
    GhData.Type = DXGK_HANDLE_ALLOCATION;
    GhData.Flags.Value = 0;
    return (PNEMUWDDM_ALLOCATION)pDevExt->u.primary.DxgkInterface.DxgkCbGetHandleData(&GhData);
}

DECLINLINE(PNEMUWDDM_ALLOCATION) nemuWddmGetAllocationFromAllocList(PNEMUMP_DEVEXT pDevExt, DXGK_ALLOCATIONLIST *pAllocList)
{
    PNEMUWDDM_OPENALLOCATION pOa = (PNEMUWDDM_OPENALLOCATION)pAllocList->hDeviceSpecificAllocation;
    return pOa->pAllocation;
}

static void nemuWddmPopulateDmaAllocInfo(PNEMUWDDM_DMA_ALLOCINFO pInfo, PNEMUWDDM_ALLOCATION pAlloc, DXGK_ALLOCATIONLIST *pDmaAlloc)
{
    pInfo->pAlloc = pAlloc;
    if (pDmaAlloc->SegmentId)
    {
        pInfo->offAlloc = (NEMUVIDEOOFFSET)pDmaAlloc->PhysicalAddress.QuadPart;
        pInfo->segmentIdAlloc = pDmaAlloc->SegmentId;
    }
    else
        pInfo->segmentIdAlloc = 0;
    pInfo->srcId = pAlloc->AllocData.SurfDesc.VidPnSourceId;
}

static void nemuWddmPopulateDmaAllocInfoWithOffset(PNEMUWDDM_DMA_ALLOCINFO pInfo, PNEMUWDDM_ALLOCATION pAlloc, DXGK_ALLOCATIONLIST *pDmaAlloc, uint32_t offStart)
{
    pInfo->pAlloc = pAlloc;
    if (pDmaAlloc->SegmentId)
    {
        pInfo->offAlloc = (NEMUVIDEOOFFSET)pDmaAlloc->PhysicalAddress.QuadPart + offStart;
        pInfo->segmentIdAlloc = pDmaAlloc->SegmentId;
    }
    else
        pInfo->segmentIdAlloc = 0;
    pInfo->srcId = pAlloc->AllocData.SurfDesc.VidPnSourceId;
}

int nemuWddmGhDisplayPostInfoScreen(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData, const POINT * pVScreenPos, uint16_t fFlags)
{
    void *p = NemuHGSMIBufferAlloc (&NemuCommonFromDeviceExt(pDevExt)->guestCtx,
                                      sizeof (VBVAINFOSCREEN),
                                      HGSMI_CH_VBVA,
                                      VBVA_INFO_SCREEN);
    if (!p)
    {
        WARN(("NemuHGSMIBufferAlloc failed"));

        return VERR_OUT_OF_RESOURCES;
    }

    VBVAINFOSCREEN *pScreen = (VBVAINFOSCREEN *)p;

    int rc = nemuWddmScreenInfoInit(pScreen, pAllocData, pVScreenPos, fFlags);
    if (RT_SUCCESS(rc))
    {
        pScreen->u32StartOffset  = 0; //(uint32_t)offVram; /* we pretend the view is located at the start of each framebuffer */

        rc = NemuHGSMIBufferSubmit (&NemuCommonFromDeviceExt(pDevExt)->guestCtx, p);
        if (RT_FAILURE(rc))
            WARN(("NemuHGSMIBufferSubmit failed %d", rc));
    }
    else
        WARN(("NemuHGSMIBufferSubmit failed %d", rc));

    NemuHGSMIBufferFree (&NemuCommonFromDeviceExt(pDevExt)->guestCtx, p);

    return rc;
}

int nemuWddmGhDisplayPostInfoView(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData)
{
    NEMUVIDEOOFFSET offVram = nemuWddmAddrFramOffset(&pAllocData->Addr);
    if (offVram == NEMUVIDEOOFFSET_VOID)
    {
        WARN(("offVram == NEMUVIDEOOFFSET_VOID"));
        return VERR_INVALID_PARAMETER;
    }

    /* Issue the screen info command. */
    void *p = NemuHGSMIBufferAlloc (&NemuCommonFromDeviceExt(pDevExt)->guestCtx,
                                      sizeof (VBVAINFOVIEW),
                                      HGSMI_CH_VBVA,
                                      VBVA_INFO_VIEW);
    if (!p)
    {
        WARN(("NemuHGSMIBufferAlloc failed"));
        return VERR_OUT_OF_RESOURCES;
    }

    VBVAINFOVIEW *pView = (VBVAINFOVIEW *)p;

    pView->u32ViewIndex     = pAllocData->SurfDesc.VidPnSourceId;
    pView->u32ViewOffset    = (uint32_t)offVram; /* we pretend the view is located at the start of each framebuffer */
    pView->u32ViewSize      = nemuWddmVramCpuVisibleSegmentSize(pDevExt)/NemuCommonFromDeviceExt(pDevExt)->cDisplays;

    pView->u32MaxScreenSize = pView->u32ViewSize;

    int rc = NemuHGSMIBufferSubmit (&NemuCommonFromDeviceExt(pDevExt)->guestCtx, p);
    if (RT_FAILURE(rc))
        WARN(("NemuHGSMIBufferSubmit failed %d", rc));

    NemuHGSMIBufferFree (&NemuCommonFromDeviceExt(pDevExt)->guestCtx, p);

    return rc;
}

NTSTATUS nemuWddmGhDisplayPostResizeLegacy(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData, const POINT * pVScreenPos, uint16_t fFlags)
{
    int rc;

    if (!(fFlags & VBVA_SCREEN_F_DISABLED))
    {
        rc = nemuWddmGhDisplayPostInfoView(pDevExt, pAllocData);
        if (RT_FAILURE(rc))
        {
            WARN(("nemuWddmGhDisplayPostInfoView failed %d", rc));
            return STATUS_UNSUCCESSFUL;
        }
    }

    rc = nemuWddmGhDisplayPostInfoScreen(pDevExt, pAllocData, pVScreenPos, fFlags);
    if (RT_FAILURE(rc))
    {
        WARN(("nemuWddmGhDisplayPostInfoScreen failed %d", rc));
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS nemuWddmGhDisplayPostResizeNew(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData, const uint32_t *pTargetMap, const POINT * pVScreenPos, uint16_t fFlags)
{
    int rc = NemuCmdVbvaConCmdResize(pDevExt, pAllocData, pTargetMap, pVScreenPos, fFlags);
    if (RT_SUCCESS(rc))
        return STATUS_SUCCESS;

    WARN(("NemuCmdVbvaConCmdResize failed %d", rc));
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS nemuWddmGhDisplaySetMode(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData)
{
    NEMUVIDEOOFFSET offVram = nemuWddmAddrFramOffset(&pAllocData->Addr);;
    if (offVram == NEMUVIDEOOFFSET_VOID)
    {
        WARN(("offVram == NEMUVIDEOOFFSET_VOID"));
        return STATUS_UNSUCCESSFUL;
    }

    USHORT width  = pAllocData->SurfDesc.width;
    USHORT height = pAllocData->SurfDesc.height;
    USHORT bpp    = pAllocData->SurfDesc.bpp;
    ULONG cbLine  = NEMUWDDM_ROUNDBOUND(((width * bpp) + 7) / 8, 4);
    ULONG yOffset = (ULONG)offVram / cbLine;
    ULONG xOffset = (ULONG)offVram % cbLine;

    if (bpp == 4)
    {
        xOffset <<= 1;
    }
    else
    {
        Assert(!(xOffset%((bpp + 7) >> 3)));
        xOffset /= ((bpp + 7) >> 3);
    }
    Assert(xOffset <= 0xffff);
    Assert(yOffset <= 0xffff);

    NemuVideoSetModeRegisters(width, height, width, bpp, 0, (uint16_t)xOffset, (uint16_t)yOffset);
    /*@todo read back from port to check if mode switch was successful */

    return STATUS_SUCCESS;
}

NTSTATUS nemuWddmGhDisplaySetInfoLegacy(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData, const POINT * pVScreenPos, uint8_t u8CurCyncState)
{
    NTSTATUS Status = STATUS_SUCCESS;
    bool fEnabled = !!pAllocData->SurfDesc.width;
    uint16_t fu16Flags = fEnabled ? VBVA_SCREEN_F_ACTIVE : VBVA_SCREEN_F_DISABLED;
    if (fEnabled)
    {
#ifdef NEMU_WITH_CROGL
        if ((u8CurCyncState & NEMUWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY) == NEMUWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY
                && pAllocData->hostID)
        {
            Status = nemuVdmaTexPresentSetAlloc(pDevExt, pAllocData);
            if (!NT_SUCCESS(Status))
                WARN(("nemuVdmaTexPresentSetAlloc failed, Status 0x%x", Status));
            return Status;
        }
#endif

        if (pAllocData->SurfDesc.VidPnSourceId == 0)
            Status = nemuWddmGhDisplaySetMode(pDevExt, pAllocData);
    }

    if (NT_SUCCESS(Status))
    {
        Status = nemuWddmGhDisplayPostResizeLegacy(pDevExt, pAllocData, pVScreenPos,
                fu16Flags);
        if (NT_SUCCESS(Status))
        {
#ifdef NEMU_WITH_CROGL
            if (fEnabled && pDevExt->f3DEnabled)
            {
                Status = nemuVdmaTexPresentSetAlloc(pDevExt, pAllocData);
                if (NT_SUCCESS(Status))
                    return STATUS_SUCCESS;
                else
                    WARN(("nemuVdmaTexPresentSetAlloc failed, Status 0x%x", Status));
            }
#else
            return STATUS_SUCCESS;
#endif
        }
        else
            WARN(("nemuWddmGhDisplayPostResize failed, Status 0x%x", Status));
    }
    else
        WARN(("nemuWddmGhDisplaySetMode failed, Status 0x%x", Status));

    return Status;
}

NTSTATUS nemuWddmGhDisplaySetInfoNew(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData, const uint32_t *pTargetMap, const POINT * pVScreenPos, uint8_t u8CurCyncState)
{
    NTSTATUS Status = STATUS_SUCCESS;
    bool fEnabled = !!pAllocData->SurfDesc.width;
    uint16_t fu16Flags = fEnabled ? VBVA_SCREEN_F_ACTIVE : VBVA_SCREEN_F_DISABLED;
    if (fEnabled)
    {
#ifdef NEMU_WITH_CROGL
        if ((u8CurCyncState & NEMUWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY) == NEMUWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY
                && pAllocData->hostID)
        {
            Status = nemuVdmaTexPresentSetAlloc(pDevExt, pAllocData);
            if (!NT_SUCCESS(Status))
                WARN(("nemuVdmaTexPresentSetAlloc failed, Status 0x%x", Status));
            return Status;
        }
#endif

        if (ASMBitTest(pTargetMap, 0))
            Status = nemuWddmGhDisplaySetMode(pDevExt, pAllocData);
    }

    if (NT_SUCCESS(Status))
    {
        Status = nemuWddmGhDisplayPostResizeNew(pDevExt, pAllocData, pTargetMap, pVScreenPos, fu16Flags);
        if (NT_SUCCESS(Status))
        {
#ifdef NEMU_WITH_CROGL
            if (fEnabled && pDevExt->f3DEnabled)
            {
                Status = nemuVdmaTexPresentSetAlloc(pDevExt, pAllocData);
                if (NT_SUCCESS(Status))
                    return STATUS_SUCCESS;
                else
                    WARN(("nemuVdmaTexPresentSetAlloc failed, Status 0x%x", Status));
            }
#else
            return STATUS_SUCCESS;
#endif
        }
        else
            WARN(("nemuWddmGhDisplayPostResizeNew failed, Status 0x%x", Status));
    }
    else
        WARN(("nemuWddmGhDisplaySetMode failed, Status 0x%x", Status));

    return Status;
}

bool nemuWddmGhDisplayCheckSetInfoFromSourceNew(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SOURCE pSource, bool fReportTargets)
{
    if (pSource->u8SyncState == NEMUWDDM_HGSYNC_F_SYNCED_ALL)
    {
        if (!pSource->fTargetsReported && fReportTargets)
            pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_TOPOLOGY;
        else
            return false;
    }

    if (!pSource->AllocData.Addr.SegmentId && pSource->AllocData.SurfDesc.width)
        return false;

    NEMUCMDVBVA_SCREENMAP_DECL(uint32_t, aTargetMap);
    uint32_t *pTargetMap;
    if (fReportTargets)
        pTargetMap = pSource->aTargetMap;
    else
    {
        memset(aTargetMap, 0, sizeof (aTargetMap));
        pTargetMap = aTargetMap;
    }

    NTSTATUS Status = nemuWddmGhDisplaySetInfoNew(pDevExt, &pSource->AllocData, pTargetMap, &pSource->VScreenPos, pSource->u8SyncState);
    if (NT_SUCCESS(Status))
    {
        if (fReportTargets && (pSource->u8SyncState & NEMUWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY) != NEMUWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY)
        {
            NEMUWDDM_TARGET_ITER Iter;
            NemuVidPnStTIterInit(pSource, pDevExt->aTargets, NemuCommonFromDeviceExt(pDevExt)->cDisplays, &Iter);

            for (PNEMUWDDM_TARGET pTarget = NemuVidPnStTIterNext(&Iter);
                    pTarget;
                    pTarget = NemuVidPnStTIterNext(&Iter))
            {
                pTarget->u8SyncState = NEMUWDDM_HGSYNC_F_SYNCED_ALL;
            }
        }

        pSource->u8SyncState = NEMUWDDM_HGSYNC_F_SYNCED_ALL;
        pSource->fTargetsReported = !!fReportTargets;
        return true;
    }

    WARN(("nemuWddmGhDisplaySetInfoNew failed, Status (0x%x)", Status));
    return false;
}

bool nemuWddmGhDisplayCheckSetInfoFromSourceLegacy(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SOURCE pSource, bool fReportTargets)
{
    if (!fReportTargets)
        return false;

    if (pSource->u8SyncState == NEMUWDDM_HGSYNC_F_SYNCED_ALL)
        return false;

    if (!pSource->AllocData.Addr.SegmentId)
        return false;

    NEMUWDDM_TARGET_ITER Iter;
    NemuVidPnStTIterInit(pSource, pDevExt->aTargets, NemuCommonFromDeviceExt(pDevExt)->cDisplays, &Iter);
    uint8_t u8SyncState = NEMUWDDM_HGSYNC_F_SYNCED_ALL;
    NEMUWDDM_ALLOC_DATA AllocData = pSource->AllocData;

    for (PNEMUWDDM_TARGET pTarget = NemuVidPnStTIterNext(&Iter);
            pTarget;
            pTarget = NemuVidPnStTIterNext(&Iter))
    {
        AllocData.SurfDesc.VidPnSourceId = pTarget->u32Id;
        NTSTATUS Status = nemuWddmGhDisplaySetInfoLegacy(pDevExt, &AllocData, &pSource->VScreenPos, pSource->u8SyncState | pTarget->u8SyncState);
        if (NT_SUCCESS(Status))
            pTarget->u8SyncState = NEMUWDDM_HGSYNC_F_SYNCED_ALL;
        else
        {
            WARN(("nemuWddmGhDisplaySetInfoLegacy failed, Status (0x%x)", Status));
            u8SyncState = 0;
        }
    }

    pSource->u8SyncState |= u8SyncState;

    return true;
}

bool nemuWddmGhDisplayCheckSetInfoFromSourceEx(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SOURCE pSource, bool fReportTargets)
{
    if (pDevExt->fCmdVbvaEnabled)
        return nemuWddmGhDisplayCheckSetInfoFromSourceNew(pDevExt, pSource, fReportTargets);
    return nemuWddmGhDisplayCheckSetInfoFromSourceLegacy(pDevExt, pSource, fReportTargets);
}

bool nemuWddmGhDisplayCheckSetInfoFromSource(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SOURCE pSource)
{
    bool fReportTargets = !pDevExt->fDisableTargetUpdate;
    return nemuWddmGhDisplayCheckSetInfoFromSourceEx(pDevExt, pSource, fReportTargets);
}

bool nemuWddmGhDisplayCheckSetInfoForDisabledTargetsNew(PNEMUMP_DEVEXT pDevExt)
{
    NEMUCMDVBVA_SCREENMAP_DECL(uint32_t, aTargetMap);

    memset(aTargetMap, 0, sizeof (aTargetMap));

    bool fFound = false;
    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        NEMUWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
        Assert(pTarget->u32Id == i);
        if (pTarget->VidPnSourceId != D3DDDI_ID_UNINITIALIZED)
        {
            Assert(pTarget->VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
            continue;
        }

        if (pTarget->u8SyncState != NEMUWDDM_HGSYNC_F_SYNCED_ALL)
            fFound = true;

        ASMBitSet(aTargetMap, i);
    }

    if (!fFound)
        return false;

    POINT VScreenPos = {0};
    NEMUWDDM_ALLOC_DATA AllocData;
    NemuVidPnAllocDataInit(&AllocData, D3DDDI_ID_UNINITIALIZED);
    NTSTATUS Status = nemuWddmGhDisplaySetInfoNew(pDevExt, &AllocData, aTargetMap, &VScreenPos, 0);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuWddmGhDisplaySetInfoNew failed %#x", Status));
        return false;
    }

    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        NEMUWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
        if (pTarget->VidPnSourceId != D3DDDI_ID_UNINITIALIZED)
        {
            Assert(pTarget->VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
            continue;
        }

        pTarget->u8SyncState = NEMUWDDM_HGSYNC_F_SYNCED_ALL;
    }

    return true;
}

bool nemuWddmGhDisplayCheckSetInfoForDisabledTargetsLegacy(PNEMUMP_DEVEXT pDevExt)
{
    POINT VScreenPos = {0};
    bool fFound = false;
    NEMUWDDM_ALLOC_DATA AllocData;
    NemuVidPnAllocDataInit(&AllocData, D3DDDI_ID_UNINITIALIZED);

    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        NEMUWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
        Assert(pTarget->u32Id == i);
        if (pTarget->VidPnSourceId != D3DDDI_ID_UNINITIALIZED)
        {
            Assert(pTarget->VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
            continue;
        }

        if (pTarget->u8SyncState == NEMUWDDM_HGSYNC_F_SYNCED_ALL)
            continue;

        fFound = true;
        AllocData.SurfDesc.VidPnSourceId = i;
        NTSTATUS Status = nemuWddmGhDisplaySetInfoLegacy(pDevExt, &AllocData, &VScreenPos, 0);
        if (NT_SUCCESS(Status))
            pTarget->u8SyncState = NEMUWDDM_HGSYNC_F_SYNCED_ALL;
        else
            WARN(("nemuWddmGhDisplaySetInfoLegacy failed, Status (0x%x)", Status));
    }

    return fFound;
}

void nemuWddmGhDisplayCheckSetInfoForDisabledTargets(PNEMUMP_DEVEXT pDevExt)
{
    if (pDevExt->fCmdVbvaEnabled)
        nemuWddmGhDisplayCheckSetInfoForDisabledTargetsNew(pDevExt);
    else
        nemuWddmGhDisplayCheckSetInfoForDisabledTargetsLegacy(pDevExt);
}

void nemuWddmGhDisplayCheckSetInfoForDisabledTargetsCheck(PNEMUMP_DEVEXT pDevExt)
{
    bool fReportTargets = !pDevExt->fDisableTargetUpdate;

    if (fReportTargets)
        nemuWddmGhDisplayCheckSetInfoForDisabledTargets(pDevExt);
}

void nemuWddmGhDisplayCheckSetInfoEx(PNEMUMP_DEVEXT pDevExt, bool fReportTargets)
{
    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        NEMUWDDM_SOURCE *pSource = &pDevExt->aSources[i];
        nemuWddmGhDisplayCheckSetInfoFromSourceEx(pDevExt, pSource, fReportTargets);
    }

    if (fReportTargets)
        nemuWddmGhDisplayCheckSetInfoForDisabledTargets(pDevExt);
}

void nemuWddmGhDisplayCheckSetInfo(PNEMUMP_DEVEXT pDevExt)
{
    bool fReportTargets = !pDevExt->fDisableTargetUpdate;
    nemuWddmGhDisplayCheckSetInfoEx(pDevExt, fReportTargets);
}

PNEMUSHGSMI nemuWddmHgsmiGetHeapFromCmdOffset(PNEMUMP_DEVEXT pDevExt, HGSMIOFFSET offCmd)
{
#ifdef NEMU_WITH_VDMA
    if(HGSMIAreaContainsOffset(&pDevExt->u.primary.Vdma.CmdHeap.Heap.area, offCmd))
        return &pDevExt->u.primary.Vdma.CmdHeap;
#endif
    if (HGSMIAreaContainsOffset(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx.Heap.area, offCmd))
        return &NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx;
    return NULL;
}

typedef enum
{
    NEMUWDDM_HGSMICMD_TYPE_UNDEFINED = 0,
    NEMUWDDM_HGSMICMD_TYPE_CTL       = 1,
#ifdef NEMU_WITH_VDMA
    NEMUWDDM_HGSMICMD_TYPE_DMACMD    = 2
#endif
} NEMUWDDM_HGSMICMD_TYPE;

NEMUWDDM_HGSMICMD_TYPE nemuWddmHgsmiGetCmdTypeFromOffset(PNEMUMP_DEVEXT pDevExt, HGSMIOFFSET offCmd)
{
#ifdef NEMU_WITH_VDMA
    if(HGSMIAreaContainsOffset(&pDevExt->u.primary.Vdma.CmdHeap.Heap.area, offCmd))
        return NEMUWDDM_HGSMICMD_TYPE_DMACMD;
#endif
    if (HGSMIAreaContainsOffset(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx.Heap.area, offCmd))
        return NEMUWDDM_HGSMICMD_TYPE_CTL;
    return NEMUWDDM_HGSMICMD_TYPE_UNDEFINED;
}

typedef struct NEMUWDDM_HWRESOURCES
{
    PHYSICAL_ADDRESS phVRAM;
    ULONG cbVRAM;
    ULONG ulApertureSize;
} NEMUWDDM_HWRESOURCES, *PNEMUWDDM_HWRESOURCES;

NTSTATUS nemuWddmPickResources(PNEMUMP_DEVEXT pDevExt, PDXGK_DEVICE_INFO pDeviceInfo, PNEMUWDDM_HWRESOURCES pHwResources)
{
    NTSTATUS Status = STATUS_SUCCESS;
    USHORT DispiId;
    memset(pHwResources, 0, sizeof (*pHwResources));
    pHwResources->cbVRAM = VBE_DISPI_TOTAL_VIDEO_MEMORY_BYTES;

    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID2);
    DispiId = NemuVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);
    if (DispiId == VBE_DISPI_ID2)
    {
       LOGREL(("found the VBE card"));
       /*
        * Write some hardware information to registry, so that
        * it's visible in Windows property dialog.
        */

       /*
        * Query the adapter's memory size. It's a bit of a hack, we just read
        * an ULONG from the data port without setting an index before.
        */
       pHwResources->cbVRAM = NemuVideoCmnPortReadUlong(VBE_DISPI_IOPORT_DATA);
       if (NemuHGSMIIsSupported ())
       {
           PCM_RESOURCE_LIST pRcList = pDeviceInfo->TranslatedResourceList;
           /* @todo: verify resources */
           for (ULONG i = 0; i < pRcList->Count; ++i)
           {
               PCM_FULL_RESOURCE_DESCRIPTOR pFRc = &pRcList->List[i];
               for (ULONG j = 0; j < pFRc->PartialResourceList.Count; ++j)
               {
                   PCM_PARTIAL_RESOURCE_DESCRIPTOR pPRc = &pFRc->PartialResourceList.PartialDescriptors[j];
                   switch (pPRc->Type)
                   {
                       case CmResourceTypePort:
                           break;
                       case CmResourceTypeInterrupt:
                           break;
                       case CmResourceTypeMemory:
                           /* we assume there is one memory segment */
                           Assert(pHwResources->phVRAM.QuadPart == 0);
                           pHwResources->phVRAM = pPRc->u.Memory.Start;
                           Assert(pHwResources->phVRAM.QuadPart != 0);
                           pHwResources->ulApertureSize = pPRc->u.Memory.Length;
                           Assert(pHwResources->cbVRAM <= pHwResources->ulApertureSize);
                           break;
                       case CmResourceTypeDma:
                           break;
                       case CmResourceTypeDeviceSpecific:
                           break;
                       case CmResourceTypeBusNumber:
                           break;
                       default:
                           break;
                   }
               }
           }
       }
       else
       {
           LOGREL(("HGSMI unsupported, returning err"));
           /* @todo: report a better status */
           Status = STATUS_UNSUCCESSFUL;
       }
    }
    else
    {
        LOGREL(("VBE card not found, returning err"));
        Status = STATUS_UNSUCCESSFUL;
    }


    return Status;
}

static void nemuWddmDevExtZeroinit(PNEMUMP_DEVEXT pDevExt, CONST PDEVICE_OBJECT pPDO)
{
    memset(pDevExt, 0, sizeof (NEMUMP_DEVEXT));
    pDevExt->pPDO = pPDO;
    PWCHAR pName = (PWCHAR)(((uint8_t*)pDevExt) + NEMUWDDM_ROUNDBOUND(sizeof(NEMUMP_DEVEXT), 8));
    RtlInitUnicodeString(&pDevExt->RegKeyName, pName);

    NemuVidPnSourcesInit(pDevExt->aSources, RT_ELEMENTS(pDevExt->aSources), 0);

    NemuVidPnTargetsInit(pDevExt->aTargets, RT_ELEMENTS(pDevExt->aTargets), 0);
}

static void nemuWddmSetupDisplaysLegacy(PNEMUMP_DEVEXT pDevExt)
{
    /* For WDDM, we simply store the number of monitors as we will deal with
     * VidPN stuff later */
    int rc = STATUS_SUCCESS;

    if (NemuCommonFromDeviceExt(pDevExt)->bHGSMI)
    {
        ULONG ulAvailable = NemuCommonFromDeviceExt(pDevExt)->cbVRAM
                            - NemuCommonFromDeviceExt(pDevExt)->cbMiniportHeap
                            - VBVA_ADAPTER_INFORMATION_SIZE;

        ULONG ulSize;
        ULONG offset;
#ifdef NEMU_WITH_VDMA
        ulSize = ulAvailable / 2;
        if (ulSize > NEMUWDDM_C_VDMA_BUFFER_SIZE)
            ulSize = NEMUWDDM_C_VDMA_BUFFER_SIZE;

        /* Align down to 4096 bytes. */
        ulSize &= ~0xFFF;
        offset = ulAvailable - ulSize;

        Assert(!(offset & 0xFFF));
#else
        offset = ulAvailable;
#endif
        rc = nemuVdmaCreate (pDevExt, &pDevExt->u.primary.Vdma
#ifdef NEMU_WITH_VDMA
                , offset, ulSize
#endif
                );
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
#ifdef NEMU_VDMA_WITH_WATCHDOG
            nemuWddmWdInit(pDevExt);
#endif
            /* can enable it right away since the host does not need any screen/FB info
             * for basic DMA functionality */
            rc = nemuVdmaEnable(pDevExt, &pDevExt->u.primary.Vdma);
            AssertRC(rc);
            if (RT_FAILURE(rc))
                nemuVdmaDestroy(pDevExt, &pDevExt->u.primary.Vdma);
        }

        ulAvailable = offset;
        ulSize = ulAvailable/2;
        offset = ulAvailable - ulSize;

        NTSTATUS Status = nemuVideoAMgrCreate(pDevExt, &pDevExt->AllocMgr, offset, ulSize);
        Assert(Status == STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
        {
            offset = ulAvailable;
        }

#ifdef NEMUWDDM_RENDER_FROM_SHADOW
        if (RT_SUCCESS(rc))
        {
            ulAvailable = offset;
            ulSize = ulAvailable / 2;
            ulSize /= NemuCommonFromDeviceExt(pDevExt)->cDisplays;
            Assert(ulSize > VBVA_MIN_BUFFER_SIZE);
            if (ulSize > VBVA_MIN_BUFFER_SIZE)
            {
                ULONG ulRatio = ulSize/VBVA_MIN_BUFFER_SIZE;
                ulRatio >>= 4; /* /= 16; */
                if (ulRatio)
                    ulSize = VBVA_MIN_BUFFER_SIZE * ulRatio;
                else
                    ulSize = VBVA_MIN_BUFFER_SIZE;
            }
            else
            {
                /* todo: ?? */
            }

            ulSize &= ~0xFFF;
            Assert(ulSize);

            Assert(ulSize * NemuCommonFromDeviceExt(pDevExt)->cDisplays < ulAvailable);

            for (int i = NemuCommonFromDeviceExt(pDevExt)->cDisplays-1; i >= 0; --i)
            {
                offset -= ulSize;
                rc = nemuVbvaCreate(pDevExt, &pDevExt->aSources[i].Vbva, offset, ulSize, i);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    rc = nemuVbvaEnable(pDevExt, &pDevExt->aSources[i].Vbva);
                    AssertRC(rc);
                    if (RT_FAILURE(rc))
                    {
                        /* @todo: de-initialize */
                    }
                }
            }
        }
#endif

        rc = NemuMPCmnMapAdapterMemory(NemuCommonFromDeviceExt(pDevExt), (void**)&pDevExt->pvVisibleVram,
                                       0, nemuWddmVramCpuVisibleSize(pDevExt));
        Assert(rc == VINF_SUCCESS);
        if (rc != VINF_SUCCESS)
            pDevExt->pvVisibleVram = NULL;

        if (RT_FAILURE(rc))
            NemuCommonFromDeviceExt(pDevExt)->bHGSMI = FALSE;
    }
}
#ifdef NEMU_WITH_CROGL
static NTSTATUS nemuWddmSetupDisplaysNew(PNEMUMP_DEVEXT pDevExt)
{
    if (!NemuCommonFromDeviceExt(pDevExt)->bHGSMI)
        return STATUS_UNSUCCESSFUL;

    ULONG cbAvailable = NemuCommonFromDeviceExt(pDevExt)->cbVRAM
                            - NemuCommonFromDeviceExt(pDevExt)->cbMiniportHeap
                            - VBVA_ADAPTER_INFORMATION_SIZE;

    /* Size of the VBVA buffer which is used to pass NEMUCMDVBVA_* commands to the host.
     * Estimate max 4KB per command.
     */
    ULONG cbCmdVbva = NEMUCMDVBVA_BUFFERSIZE(4096);

    if (cbCmdVbva >= cbAvailable)
    {
        WARN(("too few VRAM memory fatal, %d, requested for CmdVbva %d", cbAvailable, cbCmdVbva));
        return STATUS_UNSUCCESSFUL;
    }


    ULONG offCmdVbva = cbAvailable - cbCmdVbva;

    int rc = NemuCmdVbvaCreate(pDevExt, &pDevExt->CmdVbva, offCmdVbva, cbCmdVbva);
    if (RT_SUCCESS(rc))
    {
        rc = NemuCmdVbvaEnable(pDevExt, &pDevExt->CmdVbva);
        if (RT_SUCCESS(rc))
        {
            rc = NemuMPCmnMapAdapterMemory(NemuCommonFromDeviceExt(pDevExt), (void**)&pDevExt->pvVisibleVram,
                                           0, nemuWddmVramCpuVisibleSize(pDevExt));
            if (RT_SUCCESS(rc))
                return STATUS_SUCCESS;
            else
                WARN(("NemuMPCmnMapAdapterMemory failed, rc %d", rc));

            NemuCmdVbvaDisable(pDevExt, &pDevExt->CmdVbva);
        }
        else
            WARN(("NemuCmdVbvaEnable failed, rc %d", rc));

        NemuCmdVbvaDestroy(pDevExt, &pDevExt->CmdVbva);
    }
    else
        WARN(("NemuCmdVbvaCreate failed, rc %d", rc));

    return STATUS_UNSUCCESSFUL;
}
#endif
static NTSTATUS nemuWddmSetupDisplays(PNEMUMP_DEVEXT pDevExt)
{
#ifdef NEMU_WITH_CROGL
    if (pDevExt->fCmdVbvaEnabled)
    {
        NTSTATUS Status = nemuWddmSetupDisplaysNew(pDevExt);
        if (!NT_SUCCESS(Status))
            NemuCommonFromDeviceExt(pDevExt)->bHGSMI = FALSE;
        return Status;
    }
#endif

    nemuWddmSetupDisplaysLegacy(pDevExt);
    return NemuCommonFromDeviceExt(pDevExt)->bHGSMI ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        return STATUS_UNSUCCESSFUL;
}

static int nemuWddmFreeDisplays(PNEMUMP_DEVEXT pDevExt)
{
    int rc = VINF_SUCCESS;

    Assert(pDevExt->pvVisibleVram);
    if (pDevExt->pvVisibleVram)
        NemuMPCmnUnmapAdapterMemory(NemuCommonFromDeviceExt(pDevExt), (void**)&pDevExt->pvVisibleVram);

    if (pDevExt->fCmdVbvaEnabled)
    {
        rc = NemuCmdVbvaDisable(pDevExt, &pDevExt->CmdVbva);
        if (RT_SUCCESS(rc))
        {
            rc = NemuCmdVbvaDestroy(pDevExt, &pDevExt->CmdVbva);
            if (RT_FAILURE(rc))
                WARN(("NemuCmdVbvaDestroy failed %d", rc));
        }
        else
            WARN(("NemuCmdVbvaDestroy failed %d", rc));

    }
    else
    {
        for (int i = NemuCommonFromDeviceExt(pDevExt)->cDisplays-1; i >= 0; --i)
        {
            rc = nemuVbvaDisable(pDevExt, &pDevExt->aSources[i].Vbva);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                rc = nemuVbvaDestroy(pDevExt, &pDevExt->aSources[i].Vbva);
                AssertRC(rc);
                if (RT_FAILURE(rc))
                {
                    /* @todo: */
                }
            }
        }

        nemuVideoAMgrDestroy(pDevExt, &pDevExt->AllocMgr);

        rc = nemuVdmaDisable(pDevExt, &pDevExt->u.primary.Vdma);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
#ifdef NEMU_VDMA_WITH_WATCHDOG
            nemuWddmWdTerm(pDevExt);
#endif
            rc = nemuVdmaDestroy(pDevExt, &pDevExt->u.primary.Vdma);
            AssertRC(rc);
        }
    }

    return rc;
}


/* driver callbacks */
NTSTATUS DxgkDdiAddDevice(
    IN CONST PDEVICE_OBJECT PhysicalDeviceObject,
    OUT PVOID *MiniportDeviceContext
    )
{
    /* The DxgkDdiAddDevice function should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, pdo(0x%x)", PhysicalDeviceObject));

    nemuVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUMP_DEVEXT pDevExt;

    WCHAR RegKeyBuf[512];
    ULONG cbRegKeyBuf = sizeof (RegKeyBuf);

    Status = IoGetDeviceProperty (PhysicalDeviceObject,
                                  DevicePropertyDriverKeyName,
                                  cbRegKeyBuf,
                                  RegKeyBuf,
                                  &cbRegKeyBuf);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        pDevExt = (PNEMUMP_DEVEXT)nemuWddmMemAllocZero(NEMUWDDM_ROUNDBOUND(sizeof(NEMUMP_DEVEXT), 8) + cbRegKeyBuf);
        if (pDevExt)
        {
            PWCHAR pName = (PWCHAR)(((uint8_t*)pDevExt) + NEMUWDDM_ROUNDBOUND(sizeof(NEMUMP_DEVEXT), 8));
            memcpy(pName, RegKeyBuf, cbRegKeyBuf);
            nemuWddmDevExtZeroinit(pDevExt, PhysicalDeviceObject);
            *MiniportDeviceContext = pDevExt;
        }
        else
        {
            Status  = STATUS_NO_MEMORY;
            LOGREL(("ERROR, failed to create context"));
        }
    }

    LOGF(("LEAVE, Status(0x%x), pDevExt(0x%x)", Status, pDevExt));

    return Status;
}

NTSTATUS DxgkDdiStartDevice(
    IN CONST PVOID  MiniportDeviceContext,
    IN PDXGK_START_INFO  DxgkStartInfo,
    IN PDXGKRNL_INTERFACE  DxgkInterface,
    OUT PULONG  NumberOfVideoPresentSources,
    OUT PULONG  NumberOfChildren
    )
{
    /* The DxgkDdiStartDevice function should be made pageable. */
    PAGED_CODE();

    NTSTATUS Status;

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));

    nemuVDbgBreakFv();

    if ( ARGUMENT_PRESENT(MiniportDeviceContext) &&
        ARGUMENT_PRESENT(DxgkInterface) &&
        ARGUMENT_PRESENT(DxgkStartInfo) &&
        ARGUMENT_PRESENT(NumberOfVideoPresentSources),
        ARGUMENT_PRESENT(NumberOfChildren)
        )
    {
        PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;

        nemuWddmVGuidGet(pDevExt);

        /* Save DeviceHandle and function pointers supplied by the DXGKRNL_INTERFACE structure passed to DxgkInterface. */
        memcpy(&pDevExt->u.primary.DxgkInterface, DxgkInterface, sizeof (DXGKRNL_INTERFACE));

        /* Allocate a DXGK_DEVICE_INFO structure, and call DxgkCbGetDeviceInformation to fill in the members of that structure, which include the registry path, the PDO, and a list of translated resources for the display adapter represented by MiniportDeviceContext. Save selected members (ones that the display miniport driver will need later)
         * of the DXGK_DEVICE_INFO structure in the context block represented by MiniportDeviceContext. */
        DXGK_DEVICE_INFO DeviceInfo;
        Status = pDevExt->u.primary.DxgkInterface.DxgkCbGetDeviceInformation (pDevExt->u.primary.DxgkInterface.DeviceHandle, &DeviceInfo);
        if (Status == STATUS_SUCCESS)
        {
            NEMUWDDM_HWRESOURCES HwRc;
            Status = nemuWddmPickResources(pDevExt, &DeviceInfo, &HwRc);
            if (Status == STATUS_SUCCESS)
            {
#ifdef NEMU_WITH_CROGL
                pDevExt->f3DEnabled = NemuMpCrCtlConIs3DSupported();

                if (pDevExt->f3DEnabled)
                {
                    pDevExt->fTexPresentEnabled = !!(NemuMpCrGetHostCaps() & CR_NEMU_CAP_TEX_PRESENT);
                    pDevExt->fCmdVbvaEnabled = !!(NemuMpCrGetHostCaps() & CR_NEMU_CAP_CMDVBVA);
# if 1
                    pDevExt->fComplexTopologiesEnabled = pDevExt->fCmdVbvaEnabled;
# else
                    pDevExt->fComplexTopologiesEnabled = FALSE;
# endif
                }
                else
                {
                    pDevExt->fTexPresentEnabled = FALSE;
                    pDevExt->fCmdVbvaEnabled = FALSE;
                    pDevExt->fComplexTopologiesEnabled = FALSE;
                }
#endif

                /* Guest supports only HGSMI, the old VBVA via VMMDev is not supported.
                 * The host will however support both old and new interface to keep compatibility
                 * with old guest additions.
                 */
                NemuSetupDisplaysHGSMI(NemuCommonFromDeviceExt(pDevExt),
                                       HwRc.phVRAM, HwRc.ulApertureSize, HwRc.cbVRAM,
                                       VBVACAPS_COMPLETEGCMD_BY_IOREAD | VBVACAPS_IRQ);
                if (NemuCommonFromDeviceExt(pDevExt)->bHGSMI)
                {
                    nemuWddmSetupDisplays(pDevExt);
                    if (!NemuCommonFromDeviceExt(pDevExt)->bHGSMI)
                        NemuFreeDisplaysHGSMI(NemuCommonFromDeviceExt(pDevExt));
                }
                if (NemuCommonFromDeviceExt(pDevExt)->bHGSMI)
                {
                    LOGREL(("using HGSMI"));
                    *NumberOfVideoPresentSources = NemuCommonFromDeviceExt(pDevExt)->cDisplays;
                    *NumberOfChildren = NemuCommonFromDeviceExt(pDevExt)->cDisplays;
                    LOG(("sources(%d), children(%d)", *NumberOfVideoPresentSources, *NumberOfChildren));

                    nemuVdmaDdiNodesInit(pDevExt);
                    nemuVideoCmInit(&pDevExt->CmMgr);
                    nemuVideoCmInit(&pDevExt->SeamlessCtxMgr);
                    InitializeListHead(&pDevExt->SwapchainList3D);
                    pDevExt->cContexts3D = 0;
                    pDevExt->cContexts2D = 0;
                    pDevExt->cContextsDispIfResize = 0;
                    pDevExt->cUnlockedVBVADisabled = 0;
                    pDevExt->fDisableTargetUpdate = 0;
                    NEMUWDDM_CTXLOCK_INIT(pDevExt);
                    KeInitializeSpinLock(&pDevExt->SynchLock);

                    NemuCommonFromDeviceExt(pDevExt)->fAnyX = NemuVideoAnyWidthAllowed();
#if 0
                    nemuShRcTreeInit(pDevExt);
#endif

#ifdef NEMU_WITH_VIDEOHWACCEL
                    nemuVhwaInit(pDevExt);
#endif
                    NemuWddmSlInit(pDevExt);

#ifdef NEMU_WITH_CROGL
                    NemuMpCrShgsmiTransportCreate(&pDevExt->CrHgsmiTransport, pDevExt);
#endif

                    for (UINT i = 0; i < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                    {
                        PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[i];
                        KeInitializeSpinLock(&pSource->AllocationLock);
#ifdef NEMU_WITH_CROGL
                        NemuVrListInit(&pSource->VrList);
#endif
                    }

                    DWORD dwVal = NEMUWDDM_CFG_DRV_DEFAULT;
                    HANDLE hKey = NULL;
                    WCHAR aNameBuf[100];

                    Status = IoOpenDeviceRegistryKey(pDevExt->pPDO, PLUGPLAY_REGKEY_DRIVER, GENERIC_READ, &hKey);
                    if (!NT_SUCCESS(Status))
                    {
                        WARN(("IoOpenDeviceRegistryKey failed, Status = 0x%x", Status));
                        hKey = NULL;
                    }


                    if (hKey)
                    {
                        Status = nemuWddmRegQueryValueDword(hKey, NEMUWDDM_REG_DRV_FLAGS_NAME, &dwVal);
                        if (!NT_SUCCESS(Status))
                        {
                            LOG(("nemuWddmRegQueryValueDword failed, Status = 0x%x", Status));
                            dwVal = NEMUWDDM_CFG_DRV_DEFAULT;
                        }
                    }

                    pDevExt->dwDrvCfgFlags = dwVal;

                    for (UINT i = 0; i < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                    {
                        PNEMUWDDM_TARGET pTarget = &pDevExt->aTargets[i];
                        if (i == 0 || (pDevExt->dwDrvCfgFlags & NEMUWDDM_CFG_DRV_SECONDARY_TARGETS_CONNECTED) || !hKey)
                        {
                            pTarget->fConnected = true;
                            pTarget->fConfigured = true;
                        }
                        else if (hKey)
                        {
                            swprintf(aNameBuf, L"%s%d", NEMUWDDM_REG_DRV_DISPFLAGS_PREFIX, i);
                            Status = nemuWddmRegQueryValueDword(hKey, aNameBuf, &dwVal);
                            if (NT_SUCCESS(Status))
                            {
                                pTarget->fConnected = !!(dwVal & NEMUWDDM_CFG_DRVTARGET_CONNECTED);
                                pTarget->fConfigured = true;
                            }
                            else
                            {
                                WARN(("nemuWddmRegQueryValueDword failed, Status = 0x%x", Status));
                                pTarget->fConnected = false;
                                pTarget->fConfigured = false;
                            }
                        }
                    }

                    if (hKey)
                    {
                        NTSTATUS tmpStatus = ZwClose(hKey);
                        Assert(tmpStatus == STATUS_SUCCESS);
                    }

                    Status = STATUS_SUCCESS;
#ifdef NEMU_WDDM_WIN8
                    DXGK_DISPLAY_INFORMATION DisplayInfo;
                    Status = pDevExt->u.primary.DxgkInterface.DxgkCbAcquirePostDisplayOwnership(pDevExt->u.primary.DxgkInterface.DeviceHandle,
                            &DisplayInfo);
                    if (NT_SUCCESS(Status))
                    {
                        PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[0];
                        PHYSICAL_ADDRESS PhAddr;
                        /* display info may sometimes not be valid, e.g. on from-full-graphics wddm driver update
                         * ensure we have something meaningful here */
                        if (!DisplayInfo.Width)
                        {
                            PhAddr = NemuCommonFromDeviceExt(pDevExt)->phVRAM;
                            nemuWddmDiInitDefault(&DisplayInfo, PhAddr, 0);
                        }
                        else
                        {
                            PhAddr = DisplayInfo.PhysicAddress;
                            DisplayInfo.TargetId = 0;
                        }

                        nemuWddmDiToAllocData(pDevExt, &DisplayInfo, &pSource->AllocData);

                        /* init the rest source infos with some default values */
                        for (UINT i = 1; i < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                        {
                            PhAddr.QuadPart += pSource->AllocData.SurfDesc.cbSize;
                            PhAddr.QuadPart = ROUND_TO_PAGES(PhAddr.QuadPart);
                            nemuWddmDiInitDefault(&DisplayInfo, PhAddr, i);
                            pSource = &pDevExt->aSources[i];
                            nemuWddmDiToAllocData(pDevExt, &DisplayInfo, &pSource->AllocData);
                        }
                    }
                    else
                    {
                        WARN(("DxgkCbAcquirePostDisplayOwnership failed, Status 0x%x", Status));
                    }
#endif

                    NemuWddmVModesInit(pDevExt);
                }
                else
                {
                    LOGREL(("HGSMI failed to initialize, returning err"));

                    /* @todo: report a better status */
                    Status = STATUS_UNSUCCESSFUL;
                }
            }
            else
            {
                LOGREL(("nemuWddmPickResources failed Status(0x%x), returning err", Status));
                Status = STATUS_UNSUCCESSFUL;
            }
        }
        else
        {
            LOGREL(("DxgkCbGetDeviceInformation failed Status(0x%x), returning err", Status));
        }
    }
    else
    {
        LOGREL(("invalid parameter, returning err"));
        Status = STATUS_INVALID_PARAMETER;
    }

    LOGF(("LEAVE, status(0x%x)", Status));

    return Status;
}

NTSTATUS DxgkDdiStopDevice(
    IN CONST PVOID MiniportDeviceContext
    )
{
    /* The DxgkDdiStopDevice function should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%p)", MiniportDeviceContext));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;
    NTSTATUS Status = STATUS_SUCCESS;

#ifdef NEMU_WITH_CROGL
    if (pDevExt->u32CrConDefaultClientID)
        NemuMpCrCtlConDisconnect(pDevExt, &pDevExt->CrCtlCon, pDevExt->u32CrConDefaultClientID);

    NemuMpCrShgsmiTransportTerm(&pDevExt->CrHgsmiTransport);
#endif

    NemuWddmSlTerm(pDevExt);

    nemuVideoCmTerm(&pDevExt->CmMgr);

    nemuVideoCmTerm(&pDevExt->SeamlessCtxMgr);

    /* do everything we did on DxgkDdiStartDevice in the reverse order */
#ifdef NEMU_WITH_VIDEOHWACCEL
    nemuVhwaFree(pDevExt);
#endif
#if 0
    nemuShRcTreeTerm(pDevExt);
#endif

    int rc = nemuWddmFreeDisplays(pDevExt);
    if (RT_SUCCESS(rc))
        NemuFreeDisplaysHGSMI(NemuCommonFromDeviceExt(pDevExt));
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        nemuWddmVGuidFree(pDevExt);

        NemuWddmVModesCleanup();
        /* revert back to the state we were right after the DxgkDdiAddDevice */
        nemuWddmDevExtZeroinit(pDevExt, pDevExt->pPDO);
    }
    else
        Status = STATUS_UNSUCCESSFUL;

    return Status;
}

NTSTATUS DxgkDdiRemoveDevice(
    IN CONST PVOID MiniportDeviceContext
    )
{
    /* DxgkDdiRemoveDevice should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%p)", MiniportDeviceContext));

    nemuVDbgBreakFv();

    nemuWddmMemFree(MiniportDeviceContext);

    LOGF(("LEAVE, context(0x%p)", MiniportDeviceContext));

    return STATUS_SUCCESS;
}

NTSTATUS DxgkDdiDispatchIoRequest(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG VidPnSourceId,
    IN PVIDEO_REQUEST_PACKET VideoRequestPacket
    )
{
    LOGF(("ENTER, context(0x%p), ctl(0x%x)", MiniportDeviceContext, VideoRequestPacket->IoControlCode));

    AssertBreakpoint();
#if 0
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;

    switch (VideoRequestPacket->IoControlCode)
    {
        case IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES:
        {
            if (VideoRequestPacket->OutputBufferLength < sizeof(VIDEO_COLOR_CAPABILITIES))
            {
                AssertBreakpoint();
                VideoRequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return TRUE;
            }
            VIDEO_COLOR_CAPABILITIES *pCaps = (VIDEO_COLOR_CAPABILITIES*)VideoRequestPacket->OutputBuffer;

            pCaps->Length = sizeof (VIDEO_COLOR_CAPABILITIES);
            pCaps->AttributeFlags = VIDEO_DEVICE_COLOR;
            pCaps->RedPhosphoreDecay = 0;
            pCaps->GreenPhosphoreDecay = 0;
            pCaps->BluePhosphoreDecay = 0;
            pCaps->WhiteChromaticity_x = 3127;
            pCaps->WhiteChromaticity_y = 3290;
            pCaps->WhiteChromaticity_Y = 0;
            pCaps->RedChromaticity_x = 6700;
            pCaps->RedChromaticity_y = 3300;
            pCaps->GreenChromaticity_x = 2100;
            pCaps->GreenChromaticity_y = 7100;
            pCaps->BlueChromaticity_x = 1400;
            pCaps->BlueChromaticity_y = 800;
            pCaps->WhiteGamma = 0;
            pCaps->RedGamma = 20000;
            pCaps->GreenGamma = 20000;
            pCaps->BlueGamma = 20000;

            VideoRequestPacket->StatusBlock->Status = NO_ERROR;
            VideoRequestPacket->StatusBlock->Information = sizeof (VIDEO_COLOR_CAPABILITIES);
            break;
        }
#if 0
        case IOCTL_VIDEO_HANDLE_VIDEOPARAMETERS:
        {
            if (VideoRequestPacket->OutputBufferLength < sizeof(VIDEOPARAMETERS)
                    || VideoRequestPacket->InputBufferLength < sizeof(VIDEOPARAMETERS))
            {
                AssertBreakpoint();
                VideoRequestPacket->StatusBlock->Status = ERROR_INSUFFICIENT_BUFFER;
                return TRUE;
            }

            Result = NemuVideoResetDevice((PNEMUMP_DEVEXT)HwDeviceExtension,
                                          RequestPacket->StatusBlock);
            break;
        }
#endif
        default:
            AssertBreakpoint();
            VideoRequestPacket->StatusBlock->Status = ERROR_INVALID_FUNCTION;
            VideoRequestPacket->StatusBlock->Information = 0;
    }
#endif
    LOGF(("LEAVE, context(0x%p), ctl(0x%x)", MiniportDeviceContext, VideoRequestPacket->IoControlCode));

    return STATUS_SUCCESS;
}

#ifdef NEMU_WITH_CROGL
BOOLEAN DxgkDdiInterruptRoutineNew(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG MessageNumber
    )
{
//    LOGF(("ENTER, context(0x%p), msg(0x%x)", MiniportDeviceContext, MessageNumber));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;
    BOOLEAN bOur = FALSE;
    bool bNeedDpc = FALSE;
    if (!NemuCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags) /* If HGSMI is enabled at all. */
    {
        WARN(("ISR called with hgsmi disabled!"));
        return FALSE;
    }

    NEMUVTLIST CtlList;
    nemuVtListInit(&CtlList);
#ifdef NEMU_WITH_VIDEOHWACCEL
    NEMUVTLIST VhwaCmdList;
    nemuVtListInit(&VhwaCmdList);
#endif

    uint32_t flags = NemuCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags->u32HostFlags;
    bOur = (flags & HGSMIHOSTFLAGS_IRQ);

    if (bOur)
        NemuHGSMIClearIrq(&NemuCommonFromDeviceExt(pDevExt)->hostCtx);

    bNeedDpc |= NemuCmdVbvaCheckCompletedIrq(pDevExt, &pDevExt->CmdVbva);

    do {
        /* re-read flags right here to avoid host-guest racing,
         * i.e. the situation:
         * 1. guest reads flags ant it is HGSMIHOSTFLAGS_IRQ, i.e. HGSMIHOSTFLAGS_GCOMMAND_COMPLETED no set
         * 2. host completes guest command, sets the HGSMIHOSTFLAGS_GCOMMAND_COMPLETED and raises IRQ
         * 3. guest clleans IRQ and exits  */
        flags = NemuCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags->u32HostFlags;

        if (flags & HGSMIHOSTFLAGS_GCOMMAND_COMPLETED)
        {
            /* read the command offset */
            HGSMIOFFSET offCmd = NemuVideoCmnPortReadUlong(NemuCommonFromDeviceExt(pDevExt)->guestCtx.port);
            if (offCmd == HGSMIOFFSET_VOID)
            {
                WARN(("void command offset!"));
                continue;
            }

            uint16_t chInfo;
            uint8_t *pvCmd = HGSMIBufferDataAndChInfoFromOffset (&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx.Heap.area, offCmd, &chInfo);
            if (!pvCmd)
            {
                WARN(("zero cmd"));
                continue;
            }

            switch (chInfo)
            {
                case VBVA_CMDVBVA_CTL:
                {
                    int rc = NemuSHGSMICommandProcessCompletion (&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, (NEMUSHGSMIHEADER*)pvCmd, TRUE /*bool bIrq*/ , &CtlList);
                    AssertRC(rc);
                    break;
                }
#ifdef NEMU_WITH_VIDEOHWACCEL
                case VBVA_VHWA_CMD:
                {
                    nemuVhwaPutList(&VhwaCmdList, (NEMUVHWACMD*)pvCmd);
                    break;
                }
#endif /* # ifdef NEMU_WITH_VIDEOHWACCEL */
                default:
                    AssertBreakpoint();
            }
        }
        else if (flags & HGSMIHOSTFLAGS_COMMANDS_PENDING)
        {
            AssertBreakpoint();
            /* @todo: FIXME: implement !!! */
        }
        else
            break;
    } while (1);

    if (!nemuVtListIsEmpty(&CtlList))
    {
        nemuVtListCat(&pDevExt->CtlList, &CtlList);
        bNeedDpc = TRUE;
        ASMAtomicWriteU32(&pDevExt->fCompletingCommands, 1);
    }

    if (!nemuVtListIsEmpty(&VhwaCmdList))
    {
        nemuVtListCat(&pDevExt->VhwaCmdList, &VhwaCmdList);
        bNeedDpc = TRUE;
        ASMAtomicWriteU32(&pDevExt->fCompletingCommands, 1);
    }

    bNeedDpc |= !nemuVdmaDdiCmdIsCompletedListEmptyIsr(pDevExt);

    if (bOur)
    {
#ifdef NEMU_VDMA_WITH_WATCHDOG
        if (flags & HGSMIHOSTFLAGS_WATCHDOG)
        {
            Assert(0);
        }
#endif
        if (flags & HGSMIHOSTFLAGS_VSYNC)
        {
            Assert(0);
            DXGKARGCB_NOTIFY_INTERRUPT_DATA notify;
            for (UINT i = 0; i < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
            {
                PNEMUWDDM_TARGET pTarget = &pDevExt->aTargets[i];
                if (pTarget->fConnected)
                {
                    memset(&notify, 0, sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA));
                    notify.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
                    notify.CrtcVsync.VidPnTargetId = i;
                    pDevExt->u.primary.DxgkInterface.DxgkCbNotifyInterrupt(pDevExt->u.primary.DxgkInterface.DeviceHandle, &notify);
                    bNeedDpc = TRUE;
                }
            }
        }
    }

    if (pDevExt->bNotifyDxDpc)
        bNeedDpc = TRUE;

    if (bNeedDpc)
        pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);

    return bOur;
}
#endif

static BOOLEAN DxgkDdiInterruptRoutineLegacy(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG MessageNumber
    )
{
//    LOGF(("ENTER, context(0x%p), msg(0x%x)", MiniportDeviceContext, MessageNumber));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;
    BOOLEAN bOur = FALSE;
    BOOLEAN bNeedDpc = FALSE;
    if (NemuCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags) /* If HGSMI is enabled at all. */
    {
        NEMUVTLIST CtlList;
#ifdef NEMU_WITH_VDMA
        NEMUVTLIST DmaCmdList;
#endif
        nemuVtListInit(&CtlList);
#ifdef NEMU_WITH_VDMA
        nemuVtListInit(&DmaCmdList);
#endif

#ifdef NEMU_WITH_VIDEOHWACCEL
        NEMUVTLIST VhwaCmdList;
        nemuVtListInit(&VhwaCmdList);
#endif

        uint32_t flags = NemuCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags->u32HostFlags;
        bOur = (flags & HGSMIHOSTFLAGS_IRQ);

        if (bOur)
            NemuHGSMIClearIrq(&NemuCommonFromDeviceExt(pDevExt)->hostCtx);

        do
        {
            if (flags & HGSMIHOSTFLAGS_GCOMMAND_COMPLETED)
            {
                /* read the command offset */
                HGSMIOFFSET offCmd = NemuVideoCmnPortReadUlong(NemuCommonFromDeviceExt(pDevExt)->guestCtx.port);
                Assert(offCmd != HGSMIOFFSET_VOID);
                if (offCmd != HGSMIOFFSET_VOID)
                {
                    NEMUWDDM_HGSMICMD_TYPE enmType = nemuWddmHgsmiGetCmdTypeFromOffset(pDevExt, offCmd);
                    PNEMUVTLIST pList;
                    PNEMUSHGSMI pHeap = NULL;
                    switch (enmType)
                    {
#ifdef NEMU_WITH_VDMA
                        case NEMUWDDM_HGSMICMD_TYPE_DMACMD:
                            pList = &DmaCmdList;
                            pHeap = &pDevExt->u.primary.Vdma.CmdHeap;
                            break;
#endif
                        case NEMUWDDM_HGSMICMD_TYPE_CTL:
                            pList = &CtlList;
                            pHeap = &NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx;
                            break;
                        default:
                            AssertBreakpoint();
                    }

                    if (pHeap)
                    {
                        uint16_t chInfo;
                        uint8_t *pvCmd = HGSMIBufferDataAndChInfoFromOffset (&pHeap->Heap.area, offCmd, &chInfo);
                        Assert(pvCmd);
                        if (pvCmd)
                        {
                            switch (chInfo)
                            {
#ifdef NEMU_WITH_VDMA
                                case VBVA_VDMA_CMD:
                                case VBVA_VDMA_CTL:
                                {
                                    int rc = NemuSHGSMICommandProcessCompletion (pHeap, (NEMUSHGSMIHEADER*)pvCmd, TRUE /*bool bIrq*/ , pList);
                                    AssertRC(rc);
                                    break;
                                }
#endif
#ifdef NEMU_WITH_VIDEOHWACCEL
                                case VBVA_VHWA_CMD:
                                {
                                    nemuVhwaPutList(&VhwaCmdList, (NEMUVHWACMD*)pvCmd);
                                    break;
                                }
#endif /* # ifdef NEMU_WITH_VIDEOHWACCEL */
                                default:
                                    AssertBreakpoint();
                            }
                        }
                    }
                }
            }
            else if (flags & HGSMIHOSTFLAGS_COMMANDS_PENDING)
            {
                AssertBreakpoint();
                /* @todo: FIXME: implement !!! */
            }
            else
                break;

            flags = NemuCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags->u32HostFlags;
        } while (1);

        if (!nemuVtListIsEmpty(&CtlList))
        {
            nemuVtListCat(&pDevExt->CtlList, &CtlList);
            bNeedDpc = TRUE;
        }
#ifdef NEMU_WITH_VDMA
        if (!nemuVtListIsEmpty(&DmaCmdList))
        {
            nemuVtListCat(&pDevExt->DmaCmdList, &DmaCmdList);
            bNeedDpc = TRUE;
        }
#endif
        if (!nemuVtListIsEmpty(&VhwaCmdList))
        {
            nemuVtListCat(&pDevExt->VhwaCmdList, &VhwaCmdList);
            bNeedDpc = TRUE;
        }

        bNeedDpc |= !nemuVdmaDdiCmdIsCompletedListEmptyIsr(pDevExt);

        if (pDevExt->bNotifyDxDpc)
        {
            bNeedDpc = TRUE;
        }

        if (bOur)
        {
#ifdef NEMU_VDMA_WITH_WATCHDOG
            if (flags & HGSMIHOSTFLAGS_WATCHDOG)
            {
                Assert(0);
            }
#endif
            if (flags & HGSMIHOSTFLAGS_VSYNC)
            {
                Assert(0);
                DXGKARGCB_NOTIFY_INTERRUPT_DATA notify;
                for (UINT i = 0; i < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                {
                    PNEMUWDDM_TARGET pTarget = &pDevExt->aTargets[i];
                    if (pTarget->fConnected)
                    {
                        memset(&notify, 0, sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA));
                        notify.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
                        notify.CrtcVsync.VidPnTargetId = i;
                        pDevExt->u.primary.DxgkInterface.DxgkCbNotifyInterrupt(pDevExt->u.primary.DxgkInterface.DeviceHandle, &notify);
                        bNeedDpc = TRUE;
                    }
                }
            }

            if (pDevExt->bNotifyDxDpc)
            {
                bNeedDpc = TRUE;
            }

#if 0 //def DEBUG_misha
            /* this is not entirely correct since host may concurrently complete some commands and raise a new IRQ while we are here,
             * still this allows to check that the host flags are correctly cleared after the ISR */
            Assert(NemuCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags);
            uint32_t flags = NemuCommonFromDeviceExt(pDevExt)->hostCtx.pfHostFlags->u32HostFlags;
            Assert(flags == 0);
#endif
        }

        if (bNeedDpc)
        {
            pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);
        }
    }

//    LOGF(("LEAVE, context(0x%p), bOur(0x%x)", MiniportDeviceContext, (ULONG)bOur));

    return bOur;
}


typedef struct NEMUWDDM_DPCDATA
{
    NEMUVTLIST CtlList;
#ifdef NEMU_WITH_VDMA
    NEMUVTLIST DmaCmdList;
#endif
#ifdef NEMU_WITH_VIDEOHWACCEL
    NEMUVTLIST VhwaCmdList;
#endif
    LIST_ENTRY CompletedDdiCmdQueue;
    BOOL bNotifyDpc;
} NEMUWDDM_DPCDATA, *PNEMUWDDM_DPCDATA;

typedef struct NEMUWDDM_GETDPCDATA_CONTEXT
{
    PNEMUMP_DEVEXT pDevExt;
    NEMUWDDM_DPCDATA data;
} NEMUWDDM_GETDPCDATA_CONTEXT, *PNEMUWDDM_GETDPCDATA_CONTEXT;

BOOLEAN nemuWddmGetDPCDataCallback(PVOID Context)
{
    PNEMUWDDM_GETDPCDATA_CONTEXT pdc = (PNEMUWDDM_GETDPCDATA_CONTEXT)Context;
    PNEMUMP_DEVEXT pDevExt = pdc->pDevExt;
    nemuVtListDetach2List(&pDevExt->CtlList, &pdc->data.CtlList);
#ifdef NEMU_WITH_VDMA
    nemuVtListDetach2List(&pDevExt->DmaCmdList, &pdc->data.DmaCmdList);
#endif
#ifdef NEMU_WITH_VIDEOHWACCEL
    nemuVtListDetach2List(&pDevExt->VhwaCmdList, &pdc->data.VhwaCmdList);
#endif
#ifdef NEMU_WITH_CROGL
    if (!pDevExt->fCmdVbvaEnabled)
#endif
    {
        nemuVdmaDdiCmdGetCompletedListIsr(pDevExt, &pdc->data.CompletedDdiCmdQueue);
    }

    pdc->data.bNotifyDpc = pDevExt->bNotifyDxDpc;
    pDevExt->bNotifyDxDpc = FALSE;

    ASMAtomicWriteU32(&pDevExt->fCompletingCommands, 0);

    return TRUE;
}

#ifdef NEMU_WITH_CROGL
static VOID DxgkDdiDpcRoutineNew(
    IN CONST PVOID  MiniportDeviceContext
    )
{
//    LOGF(("ENTER, context(0x%p)", MiniportDeviceContext));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;

    pDevExt->u.primary.DxgkInterface.DxgkCbNotifyDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);

    if (ASMAtomicReadU32(&pDevExt->fCompletingCommands))
    {
        NEMUWDDM_GETDPCDATA_CONTEXT context = {0};
        BOOLEAN bRet;

        context.pDevExt = pDevExt;

        /* get DPC data at IRQL */
        NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
                pDevExt->u.primary.DxgkInterface.DeviceHandle,
                nemuWddmGetDPCDataCallback,
                &context,
                0, /* IN ULONG MessageNumber */
                &bRet);
        Assert(Status == STATUS_SUCCESS);

    //    if (context.data.bNotifyDpc)
        pDevExt->u.primary.DxgkInterface.DxgkCbNotifyDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);

        if (!nemuVtListIsEmpty(&context.data.CtlList))
        {
            int rc = NemuSHGSMICommandPostprocessCompletion (&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, &context.data.CtlList);
            AssertRC(rc);
        }
#ifdef NEMU_WITH_VIDEOHWACCEL
        if (!nemuVtListIsEmpty(&context.data.VhwaCmdList))
        {
            nemuVhwaCompletionListProcess(pDevExt, &context.data.VhwaCmdList);
        }
#endif
    }
//    LOGF(("LEAVE, context(0x%p)", MiniportDeviceContext));
}
#endif

static VOID DxgkDdiDpcRoutineLegacy(
    IN CONST PVOID  MiniportDeviceContext
    )
{
//    LOGF(("ENTER, context(0x%p)", MiniportDeviceContext));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;

    NEMUWDDM_GETDPCDATA_CONTEXT context = {0};
    BOOLEAN bRet;

    context.pDevExt = pDevExt;

    /* get DPC data at IRQL */
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            nemuWddmGetDPCDataCallback,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRet);
    Assert(Status == STATUS_SUCCESS);

//    if (context.data.bNotifyDpc)
    pDevExt->u.primary.DxgkInterface.DxgkCbNotifyDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);

    if (!nemuVtListIsEmpty(&context.data.CtlList))
    {
        int rc = NemuSHGSMICommandPostprocessCompletion (&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, &context.data.CtlList);
        AssertRC(rc);
    }
#ifdef NEMU_WITH_VDMA
    if (!nemuVtListIsEmpty(&context.data.DmaCmdList))
    {
        int rc = NemuSHGSMICommandPostprocessCompletion (&pDevExt->u.primary.Vdma.CmdHeap, &context.data.DmaCmdList);
        AssertRC(rc);
    }
#endif
#ifdef NEMU_WITH_VIDEOHWACCEL
    if (!nemuVtListIsEmpty(&context.data.VhwaCmdList))
    {
        nemuVhwaCompletionListProcess(pDevExt, &context.data.VhwaCmdList);
    }
#endif

    nemuVdmaDdiCmdHandleCompletedList(pDevExt, &context.data.CompletedDdiCmdQueue);

//    LOGF(("LEAVE, context(0x%p)", MiniportDeviceContext));
}

NTSTATUS DxgkDdiQueryChildRelations(
    IN CONST PVOID MiniportDeviceContext,
    IN OUT PDXGK_CHILD_DESCRIPTOR ChildRelations,
    IN ULONG ChildRelationsSize
    )
{
    /* The DxgkDdiQueryChildRelations function should be made pageable. */
    PAGED_CODE();

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));
    Assert(ChildRelationsSize == (NemuCommonFromDeviceExt(pDevExt)->cDisplays + 1)*sizeof(DXGK_CHILD_DESCRIPTOR));
    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        ChildRelations[i].ChildDeviceType = TypeVideoOutput;
        ChildRelations[i].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_HD15; /* VGA */
        ChildRelations[i].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE; //D3DKMDT_MOA_INTERRUPTIBLE; /* ?? D3DKMDT_MOA_NONE*/
        ChildRelations[i].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        ChildRelations[i].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible; /* ?? HpdAwarenessAlwaysConnected; */
        ChildRelations[i].AcpiUid =  0; /* */
        ChildRelations[i].ChildUid = i; /* should be == target id */
    }
    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));
    return STATUS_SUCCESS;
}

NTSTATUS DxgkDdiQueryChildStatus(
    IN CONST PVOID  MiniportDeviceContext,
    IN PDXGK_CHILD_STATUS  ChildStatus,
    IN BOOLEAN  NonDestructiveOnly
    )
{
    /* The DxgkDdiQueryChildStatus should be made pageable. */
    PAGED_CODE();

    nemuVDbgBreakFv();

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)MiniportDeviceContext;

    if (ChildStatus->ChildUid >= (uint32_t)NemuCommonFromDeviceExt(pDevExt)->cDisplays)
    {
        WARN(("Invalid child id %d", ChildStatus->ChildUid));
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    switch (ChildStatus->Type)
    {
        case StatusConnection:
        {
            LOGF(("StatusConnection"));
            NEMUWDDM_TARGET *pTarget = &pDevExt->aTargets[ChildStatus->ChildUid];
            BOOLEAN Connected = !!pTarget->fConnected;
            if (!Connected)
                LOGREL(("Tgt[%d] DISCONNECTED!!", ChildStatus->ChildUid));
            ChildStatus->HotPlug.Connected = !!pTarget->fConnected;
            break;
        }
        case StatusRotation:
            LOGF(("StatusRotation"));
            ChildStatus->Rotation.Angle = 0;
            break;
        default:
            WARN(("ERROR: status type: %d", ChildStatus->Type));
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));

    return Status;
}

NTSTATUS DxgkDdiQueryDeviceDescriptor(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG ChildUid,
    IN OUT PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor
    )
{
    /* The DxgkDdiQueryDeviceDescriptor should be made pageable. */
    PAGED_CODE();

    nemuVDbgBreakFv();

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));

    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));

    /* we do not support EDID */
    return STATUS_MONITOR_NO_DESCRIPTOR;
}

NTSTATUS DxgkDdiSetPowerState(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG DeviceUid,
    IN DEVICE_POWER_STATE DevicePowerState,
    IN POWER_ACTION ActionType
    )
{
    /* The DxgkDdiSetPowerState function should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));

    nemuVDbgBreakFv();

    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));

    return STATUS_SUCCESS;
}

NTSTATUS DxgkDdiNotifyAcpiEvent(
    IN CONST PVOID  MiniportDeviceContext,
    IN DXGK_EVENT_TYPE  EventType,
    IN ULONG  Event,
    IN PVOID  Argument,
    OUT PULONG  AcpiFlags
    )
{
    LOGF(("ENTER, MiniportDeviceContext(0x%x)", MiniportDeviceContext));

    nemuVDbgBreakF();

    LOGF(("LEAVE, MiniportDeviceContext(0x%x)", MiniportDeviceContext));

    return STATUS_SUCCESS;
}

VOID DxgkDdiResetDevice(
    IN CONST PVOID MiniportDeviceContext
    )
{
    /* DxgkDdiResetDevice can be called at any IRQL, so it must be in nonpageable memory.  */
    nemuVDbgBreakF();



    LOGF(("ENTER, context(0x%x)", MiniportDeviceContext));
    LOGF(("LEAVE, context(0x%x)", MiniportDeviceContext));
}

VOID DxgkDdiUnload(
    VOID
    )
{
    /* DxgkDdiUnload should be made pageable. */
    PAGED_CODE();
    LOGF((": unloading"));

    nemuVDbgBreakFv();

    VbglTerminate();

#ifdef NEMU_WITH_CROGL
    NemuVrTerm();
#endif

    PRTLOGGER pLogger = RTLogRelSetDefaultInstance(NULL);
    if (pLogger)
    {
        RTLogDestroy(pLogger);
    }
    pLogger = RTLogSetDefaultInstance(NULL);
    if (pLogger)
    {
        RTLogDestroy(pLogger);
    }
}

NTSTATUS DxgkDdiQueryInterface(
    IN CONST PVOID MiniportDeviceContext,
    IN PQUERY_INTERFACE QueryInterface
    )
{
    LOGF(("ENTER, MiniportDeviceContext(0x%x)", MiniportDeviceContext));

    nemuVDbgBreakFv();

    LOGF(("LEAVE, MiniportDeviceContext(0x%x)", MiniportDeviceContext));

    return STATUS_NOT_SUPPORTED;
}

VOID DxgkDdiControlEtwLogging(
    IN BOOLEAN  Enable,
    IN ULONG  Flags,
    IN UCHAR  Level
    )
{
    LOGF(("ENTER"));

    nemuVDbgBreakF();

    LOGF(("LEAVE"));
}

/**
 * DxgkDdiQueryAdapterInfo
 */
NTSTATUS APIENTRY DxgkDdiQueryAdapterInfo(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_QUERYADAPTERINFO*  pQueryAdapterInfo)
{
    /* The DxgkDdiQueryAdapterInfo should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x), Query type (%d)", hAdapter, pQueryAdapterInfo->Type));
    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    nemuVDbgBreakFv();

    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
        {
            DXGK_DRIVERCAPS *pCaps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;

#ifdef NEMU_WDDM_WIN8
            memset(pCaps, 0, sizeof (*pCaps));
#endif
            pCaps->HighestAcceptableAddress.LowPart = ~0UL;
#ifdef RT_ARCH_AMD64
            /* driver talks to host in terms of page numbers when reffering to RAM
             * we use uint32_t field to pass page index to host, so max would be (~0UL) << PAGE_OFFSET,
             * which seems quite enough */
            pCaps->HighestAcceptableAddress.HighPart = PAGE_OFFSET_MASK;
#endif
            pCaps->MaxPointerWidth  = NEMUWDDM_C_POINTER_MAX_WIDTH;
            pCaps->MaxPointerHeight = NEMUWDDM_C_POINTER_MAX_HEIGHT;
            pCaps->PointerCaps.Value = 3; /* Monochrome , Color*/ /* MaskedColor == Value | 4, disable for now */
#ifdef NEMU_WDDM_WIN8
            if (!g_NemuDisplayOnly)
#endif
            {
            pCaps->MaxAllocationListSlotId = 16;
            pCaps->ApertureSegmentCommitLimit = 0;
            pCaps->InterruptMessageNumber = 0;
            pCaps->NumberOfSwizzlingRanges = 0;
            pCaps->MaxOverlays = 0;
#ifdef NEMU_WITH_VIDEOHWACCEL
            for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
            {
                if ( pDevExt->aSources[i].Vhwa.Settings.fFlags & NEMUVHWA_F_ENABLED)
                    pCaps->MaxOverlays += pDevExt->aSources[i].Vhwa.Settings.cOverlaysSupported;
            }
#endif
            pCaps->GammaRampCaps.Value = 0;
            pCaps->PresentationCaps.Value = 0;
            pCaps->PresentationCaps.NoScreenToScreenBlt = 1;
            pCaps->PresentationCaps.NoOverlapScreenBlt = 1;
            pCaps->MaxQueuedFlipOnVSync = 0; /* do we need it? */
            pCaps->FlipCaps.Value = 0;
            /* ? pCaps->FlipCaps.FlipOnVSyncWithNoWait = 1; */
            pCaps->SchedulingCaps.Value = 0;
            /* we might need it for Aero.
             * Setting this flag means we support DeviceContext, i.e.
             *  DxgkDdiCreateContext and DxgkDdiDestroyContext
             */
            pCaps->SchedulingCaps.MultiEngineAware = 1;
            pCaps->MemoryManagementCaps.Value = 0;
            /* @todo: this correlates with pCaps->SchedulingCaps.MultiEngineAware */
            pCaps->MemoryManagementCaps.PagingNode = 0;
            /* @todo: this correlates with pCaps->SchedulingCaps.MultiEngineAware */
            pCaps->GpuEngineTopology.NbAsymetricProcessingNodes = NEMUWDDM_NUM_NODES;
#ifdef NEMU_WDDM_WIN8
            pCaps->WDDMVersion = DXGKDDI_WDDMv1;
#endif
            }
#ifdef NEMU_WDDM_WIN8
            else
            {
                pCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
            }
#endif
            break;
        }
        case DXGKQAITYPE_QUERYSEGMENT:
        {
#ifdef NEMU_WDDM_WIN8
            if (!g_NemuDisplayOnly)
#endif
            {
            /* no need for DXGK_QUERYSEGMENTIN as it contains AGP aperture info, which (AGP aperture) we do not support
             * DXGK_QUERYSEGMENTIN *pQsIn = (DXGK_QUERYSEGMENTIN*)pQueryAdapterInfo->pInputData; */
            DXGK_QUERYSEGMENTOUT *pQsOut = (DXGK_QUERYSEGMENTOUT*)pQueryAdapterInfo->pOutputData;
# define NEMUWDDM_SEGMENTS_COUNT 2
            if (!pQsOut->pSegmentDescriptor)
            {
                /* we are requested to provide the number of segments we support */
                pQsOut->NbSegment = NEMUWDDM_SEGMENTS_COUNT;
            }
            else if (pQsOut->NbSegment != NEMUWDDM_SEGMENTS_COUNT)
            {
                WARN(("NbSegment (%d) != 1", pQsOut->NbSegment));
                Status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                DXGK_SEGMENTDESCRIPTOR* pDr = pQsOut->pSegmentDescriptor;
                /* we are requested to provide segment information */
                pDr->BaseAddress.QuadPart = 0;
                pDr->CpuTranslatedAddress = NemuCommonFromDeviceExt(pDevExt)->phVRAM;
                /* make sure the size is page aligned */
                /* @todo: need to setup VBVA buffers and adjust the mem size here */
                pDr->Size = nemuWddmVramCpuVisibleSegmentSize(pDevExt);
                pDr->NbOfBanks = 0;
                pDr->pBankRangeTable = 0;
                pDr->CommitLimit = pDr->Size;
                pDr->Flags.Value = 0;
                pDr->Flags.CpuVisible = 1;

                ++pDr;
                /* create cpu-invisible segment of the same size */
                pDr->BaseAddress.QuadPart = 0;
                pDr->CpuTranslatedAddress.QuadPart = 0;
                /* make sure the size is page aligned */
                /* @todo: need to setup VBVA buffers and adjust the mem size here */
                pDr->Size = nemuWddmVramCpuInvisibleSegmentSize(pDevExt);
                pDr->NbOfBanks = 0;
                pDr->pBankRangeTable = 0;
                pDr->CommitLimit = pDr->Size;
                pDr->Flags.Value = 0;

                pQsOut->PagingBufferSegmentId = 0;
                pQsOut->PagingBufferSize = PAGE_SIZE;
                pQsOut->PagingBufferPrivateDataSize = PAGE_SIZE;
            }
            }
#ifdef NEMU_WDDM_WIN8
            else
            {
                WARN(("unsupported Type (%d)", pQueryAdapterInfo->Type));
                Status = STATUS_NOT_SUPPORTED;
            }
#endif

            break;
        }
        case DXGKQAITYPE_UMDRIVERPRIVATE:
#ifdef NEMU_WDDM_WIN8
            if (!g_NemuDisplayOnly)
#endif
            {
                if (pQueryAdapterInfo->OutputDataSize == sizeof (NEMUWDDM_QI))
                {
                    NEMUWDDM_QI * pQi = (NEMUWDDM_QI*)pQueryAdapterInfo->pOutputData;
                    memset (pQi, 0, sizeof (NEMUWDDM_QI));
                    pQi->u32Version = NEMUVIDEOIF_VERSION;
#ifdef NEMU_WITH_CROGL
                    pQi->u32Nemu3DCaps = NemuMpCrGetHostCaps();
#endif
                    pQi->cInfos = NemuCommonFromDeviceExt(pDevExt)->cDisplays;
#ifdef NEMU_WITH_VIDEOHWACCEL
                    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                    {
                        pQi->aInfos[i] = pDevExt->aSources[i].Vhwa.Settings;
                    }
#endif
                }
                else
                {
                    WARN(("incorrect buffer size %d, expected %d", pQueryAdapterInfo->OutputDataSize, sizeof (NEMUWDDM_QI)));
                    Status = STATUS_BUFFER_TOO_SMALL;
                }
            }
#ifdef NEMU_WDDM_WIN8
            else
            {
                WARN(("unsupported Type (%d)", pQueryAdapterInfo->Type));
                Status = STATUS_NOT_SUPPORTED;
            }
#endif
            break;
#ifdef NEMU_WDDM_WIN8
        case DXGKQAITYPE_QUERYSEGMENT3:
            LOGREL(("DXGKQAITYPE_QUERYSEGMENT3 treating as unsupported!"));
            Status = STATUS_NOT_SUPPORTED;
            break;
#endif
        default:
            WARN(("unsupported Type (%d)", pQueryAdapterInfo->Type));
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    LOGF(("LEAVE, context(0x%x), Status(0x%x)", hAdapter, Status));
    return Status;
}

/**
 * DxgkDdiCreateDevice
 */
NTSTATUS APIENTRY DxgkDdiCreateDevice(
    CONST HANDLE  hAdapter,
    DXGKARG_CREATEDEVICE*  pCreateDevice)
{
    /* DxgkDdiCreateDevice should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));
    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    nemuVDbgBreakFv();

    PNEMUWDDM_DEVICE pDevice = (PNEMUWDDM_DEVICE)nemuWddmMemAllocZero(sizeof (NEMUWDDM_DEVICE));
    if (!pDevice)
    {
        WARN(("nemuWddmMemAllocZero failed for WDDM device structure"));
        return STATUS_NO_MEMORY;
    }
    pCreateDevice->hDevice = pDevice;
    if (pCreateDevice->Flags.SystemDevice)
        pDevice->enmType = NEMUWDDM_DEVICE_TYPE_SYSTEM;

    pDevice->pAdapter = pDevExt;
    pDevice->hDevice = pCreateDevice->hDevice;

    pCreateDevice->hDevice = pDevice;
    pCreateDevice->pInfo = NULL;

    LOGF(("LEAVE, context(0x%x), Status(0x%x)", hAdapter, Status));

    return Status;
}

PNEMUWDDM_RESOURCE nemuWddmResourceCreate(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_RCINFO pRcInfo)
{
    PNEMUWDDM_RESOURCE pResource = (PNEMUWDDM_RESOURCE)nemuWddmMemAllocZero(RT_OFFSETOF(NEMUWDDM_RESOURCE, aAllocations[pRcInfo->cAllocInfos]));
    if (!pResource)
    {
        AssertFailed();
        return NULL;
    }
    pResource->cRefs = 1;
    pResource->cAllocations = pRcInfo->cAllocInfos;
    pResource->fFlags = pRcInfo->fFlags;
    pResource->RcDesc = pRcInfo->RcDesc;
    return pResource;
}

VOID nemuWddmResourceRetain(PNEMUWDDM_RESOURCE pResource)
{
    ASMAtomicIncU32(&pResource->cRefs);
}

static VOID nemuWddmResourceDestroy(PNEMUWDDM_RESOURCE pResource)
{
    nemuWddmMemFree(pResource);
}

VOID nemuWddmResourceWaitDereference(PNEMUWDDM_RESOURCE pResource)
{
    nemuWddmCounterU32Wait(&pResource->cRefs, 1);
}

VOID nemuWddmResourceRelease(PNEMUWDDM_RESOURCE pResource)
{
    uint32_t cRefs = ASMAtomicDecU32(&pResource->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    if (!cRefs)
    {
        nemuWddmResourceDestroy(pResource);
    }
}

void nemuWddmAllocationDeleteFromResource(PNEMUWDDM_RESOURCE pResource, PNEMUWDDM_ALLOCATION pAllocation)
{
    Assert(pAllocation->pResource == pResource);
    if (pResource)
    {
        Assert(&pResource->aAllocations[pAllocation->iIndex] == pAllocation);
        nemuWddmResourceRelease(pResource);
    }
    else
    {
        nemuWddmMemFree(pAllocation);
    }
}

VOID nemuWddmAllocationCleanupAssignment(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pAllocation)
{
    switch (pAllocation->enmType)
    {
        case NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE:
        case NEMUWDDM_ALLOC_TYPE_UMD_RC_GENERIC:
        {
            if (pAllocation->bAssigned)
            {
                /* @todo: do we need to notify host? */
                nemuWddmAssignPrimary(&pDevExt->aSources[pAllocation->AllocData.SurfDesc.VidPnSourceId], NULL, pAllocation->AllocData.SurfDesc.VidPnSourceId);
            }
            break;
        }
        default:
            break;
    }
}

VOID nemuWddmAllocationCleanup(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pAllocation)
{
    switch (pAllocation->enmType)
    {
        case NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE:
        case NEMUWDDM_ALLOC_TYPE_UMD_RC_GENERIC:
        {
#if 0
            if (pAllocation->enmType == NEMUWDDM_ALLOC_TYPE_UMD_RC_GENERIC)
            {
                if (pAllocation->hSharedHandle)
                {
                    nemuShRcTreeRemove(pDevExt, pAllocation);
                }
            }
#endif
            break;
        }
        case NEMUWDDM_ALLOC_TYPE_UMD_HGSMI_BUFFER:
        {
            break;
        }
        default:
            break;
    }
#ifdef NEMU_WITH_CROGL
    PNEMUWDDM_SWAPCHAIN pSwapchain = nemuWddmSwapchainRetainByAlloc(pDevExt, pAllocation);
    if (pSwapchain)
    {
        nemuWddmSwapchainAllocRemove(pDevExt, pSwapchain, pAllocation);
        nemuWddmSwapchainRelease(pSwapchain);
    }
#endif
}

VOID nemuWddmAllocationDestroy(PNEMUWDDM_ALLOCATION pAllocation)
{
    PAGED_CODE();

    nemuWddmAllocationDeleteFromResource(pAllocation->pResource, pAllocation);
}

PNEMUWDDM_ALLOCATION nemuWddmAllocationCreateFromResource(PNEMUWDDM_RESOURCE pResource, uint32_t iIndex)
{
    PNEMUWDDM_ALLOCATION pAllocation = NULL;
    if (pResource)
    {
        Assert(iIndex < pResource->cAllocations);
        if (iIndex < pResource->cAllocations)
        {
            pAllocation = &pResource->aAllocations[iIndex];
            memset(pAllocation, 0, sizeof (NEMUWDDM_ALLOCATION));
        }
        nemuWddmResourceRetain(pResource);
    }
    else
        pAllocation = (PNEMUWDDM_ALLOCATION)nemuWddmMemAllocZero(sizeof (NEMUWDDM_ALLOCATION));

    if (pAllocation)
    {
        if (pResource)
        {
            pAllocation->pResource = pResource;
            pAllocation->iIndex = iIndex;
        }
    }

    return pAllocation;
}

NTSTATUS nemuWddmAllocationCreate(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_RESOURCE pResource, uint32_t iIndex, DXGK_ALLOCATIONINFO* pAllocationInfo)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    Assert(pAllocationInfo->PrivateDriverDataSize == sizeof (NEMUWDDM_ALLOCINFO));
    if (pAllocationInfo->PrivateDriverDataSize >= sizeof (NEMUWDDM_ALLOCINFO))
    {
        PNEMUWDDM_ALLOCINFO pAllocInfo = (PNEMUWDDM_ALLOCINFO)pAllocationInfo->pPrivateDriverData;
        PNEMUWDDM_ALLOCATION pAllocation = nemuWddmAllocationCreateFromResource(pResource, iIndex);
        Assert(pAllocation);
        if (pAllocation)
        {
            pAllocationInfo->pPrivateDriverData = NULL;
            pAllocationInfo->PrivateDriverDataSize = 0;
            pAllocationInfo->Alignment = 0;
            pAllocationInfo->PitchAlignedSize = 0;
            pAllocationInfo->HintedBank.Value = 0;
            pAllocationInfo->PreferredSegment.Value = 0;
            pAllocationInfo->SupportedReadSegmentSet = 1;
            pAllocationInfo->SupportedWriteSegmentSet = 1;
            pAllocationInfo->EvictionSegmentSet = 0;
            pAllocationInfo->MaximumRenamingListLength = 0;
            pAllocationInfo->hAllocation = pAllocation;
            pAllocationInfo->Flags.Value = 0;
            pAllocationInfo->pAllocationUsageHint = NULL;
            pAllocationInfo->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;

            pAllocation->enmType = pAllocInfo->enmType;
            pAllocation->AllocData.Addr.SegmentId = 0;
            pAllocation->AllocData.Addr.offVram = NEMUVIDEOOFFSET_VOID;
            pAllocation->bVisible = FALSE;
            pAllocation->bAssigned = FALSE;
            KeInitializeSpinLock(&pAllocation->OpenLock);
            InitializeListHead(&pAllocation->OpenList);

            switch (pAllocInfo->enmType)
            {
                case NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE:
                case NEMUWDDM_ALLOC_TYPE_UMD_RC_GENERIC:
                case NEMUWDDM_ALLOC_TYPE_STD_SHADOWSURFACE:
                case NEMUWDDM_ALLOC_TYPE_STD_STAGINGSURFACE:
                {
                    pAllocation->fRcFlags = pAllocInfo->fFlags;
                    pAllocation->AllocData.SurfDesc = pAllocInfo->SurfDesc;
                    pAllocation->AllocData.hostID = pAllocInfo->hostID;

                    pAllocationInfo->Size = pAllocInfo->SurfDesc.cbSize;

                    switch (pAllocInfo->enmType)
                    {
                        case NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE:
                            break;
                        case NEMUWDDM_ALLOC_TYPE_UMD_RC_GENERIC:
#ifdef NEMU_WITH_VIDEOHWACCEL
                            if (pAllocInfo->fFlags.Overlay)
                            {
                                /* actually we can not "properly" issue create overlay commands to the host here
                                 * because we do not know source VidPn id here, i.e.
                                 * the primary which is supposed to be overlayed,
                                 * however we need to get some info like pitch & size from the host here */
                                int rc = nemuVhwaHlpGetSurfInfo(pDevExt, pAllocation);
                                AssertRC(rc);
                                if (RT_SUCCESS(rc))
                                {
                                    pAllocationInfo->Flags.Overlay = 1;
                                    pAllocationInfo->Flags.CpuVisible = 1;
                                    pAllocationInfo->Size = pAllocation->AllocData.SurfDesc.cbSize;

                                    pAllocationInfo->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_HIGH;
                                }
                                else
                                    Status = STATUS_UNSUCCESSFUL;
                            }
                            else
#endif
                            {
                                Assert(pAllocation->AllocData.SurfDesc.bpp);
                                Assert(pAllocation->AllocData.SurfDesc.pitch);
                                Assert(pAllocation->AllocData.SurfDesc.cbSize);

                                /*
                                 * Mark the allocation as visible to the CPU so we can
                                 * lock it in the user mode driver for SYSTEM pool allocations.
                                 * See @bugref{8040} for further information.
                                 */
                                if (!pAllocInfo->fFlags.SharedResource && !pAllocInfo->hostID)
                                    pAllocationInfo->Flags.CpuVisible = 1;

                                if (pAllocInfo->fFlags.SharedResource)
                                {
                                    pAllocation->hSharedHandle = (HANDLE)pAllocInfo->hSharedHandle;
#if 0
                                    if (pAllocation->hSharedHandle)
                                    {
                                        nemuShRcTreePut(pDevExt, pAllocation);
                                    }
#endif
                                }

#if 0
                                /* Allocation from the CPU invisible second segment does not
                                 * work apparently and actually fails on Vista.
                                 *
                                 * @todo Find out what exactly is wrong.
                                 */
//                                if (pAllocInfo->hostID)
                                {
                                    pAllocationInfo->SupportedReadSegmentSet = 2;
                                    pAllocationInfo->SupportedWriteSegmentSet = 2;
                                }
#endif
                            }
                            break;
                        case NEMUWDDM_ALLOC_TYPE_STD_SHADOWSURFACE:
                        case NEMUWDDM_ALLOC_TYPE_STD_STAGINGSURFACE:
                            pAllocationInfo->Flags.CpuVisible = 1;
                            break;
                    }

                    if (Status == STATUS_SUCCESS)
                    {
                        pAllocation->UsageHint.Version = 0;
                        pAllocation->UsageHint.v1.Flags.Value = 0;
                        pAllocation->UsageHint.v1.Format = pAllocInfo->SurfDesc.format;
                        pAllocation->UsageHint.v1.SwizzledFormat = 0;
                        pAllocation->UsageHint.v1.ByteOffset = 0;
                        pAllocation->UsageHint.v1.Width = pAllocation->AllocData.SurfDesc.width;
                        pAllocation->UsageHint.v1.Height = pAllocation->AllocData.SurfDesc.height;
                        pAllocation->UsageHint.v1.Pitch = pAllocation->AllocData.SurfDesc.pitch;
                        pAllocation->UsageHint.v1.Depth = 0;
                        pAllocation->UsageHint.v1.SlicePitch = 0;

                        Assert(!pAllocationInfo->pAllocationUsageHint);
                        pAllocationInfo->pAllocationUsageHint = &pAllocation->UsageHint;
                    }

                    break;
                }
                case NEMUWDDM_ALLOC_TYPE_UMD_HGSMI_BUFFER:
                {
                    pAllocationInfo->Size = pAllocInfo->cbBuffer;
                    pAllocation->fUhgsmiType = pAllocInfo->fUhgsmiType;
                    pAllocation->AllocData.SurfDesc.cbSize = pAllocInfo->cbBuffer;
                    pAllocationInfo->Flags.CpuVisible = 1;
//                    pAllocationInfo->Flags.SynchronousPaging = 1;
                    pAllocationInfo->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_MAXIMUM;
                    break;
                }

                default:
                    LOGREL(("ERROR: invalid alloc info type(%d)", pAllocInfo->enmType));
                    AssertBreakpoint();
                    Status = STATUS_INVALID_PARAMETER;
                    break;

            }

            if (Status != STATUS_SUCCESS)
                nemuWddmAllocationDeleteFromResource(pResource, pAllocation);
        }
        else
        {
            LOGREL(("ERROR: failed to create allocation description"));
            Status = STATUS_NO_MEMORY;
        }

    }
    else
    {
        LOGREL(("ERROR: PrivateDriverDataSize(%d) less than header size(%d)", pAllocationInfo->PrivateDriverDataSize, sizeof (NEMUWDDM_ALLOCINFO)));
        Status = STATUS_INVALID_PARAMETER;
    }

    return Status;
}

NTSTATUS APIENTRY DxgkDdiCreateAllocation(
    CONST HANDLE  hAdapter,
    DXGKARG_CREATEALLOCATION*  pCreateAllocation)
{
    /* DxgkDdiCreateAllocation should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_RESOURCE pResource = NULL;

    if (pCreateAllocation->PrivateDriverDataSize)
    {
        Assert(pCreateAllocation->PrivateDriverDataSize == sizeof (NEMUWDDM_RCINFO));
        Assert(pCreateAllocation->pPrivateDriverData);
        if (pCreateAllocation->PrivateDriverDataSize < sizeof (NEMUWDDM_RCINFO))
        {
            WARN(("invalid private data size (%d)", pCreateAllocation->PrivateDriverDataSize));
            return STATUS_INVALID_PARAMETER;
        }

        PNEMUWDDM_RCINFO pRcInfo = (PNEMUWDDM_RCINFO)pCreateAllocation->pPrivateDriverData;
//            Assert(pRcInfo->RcDesc.VidPnSourceId < NemuCommonFromDeviceExt(pDevExt)->cDisplays);
        if (pRcInfo->cAllocInfos != pCreateAllocation->NumAllocations)
        {
            WARN(("invalid number of allocations passed in, (%d), expected (%d)", pRcInfo->cAllocInfos, pCreateAllocation->NumAllocations));
            return STATUS_INVALID_PARAMETER;
        }

        /* a check to ensure we do not get the allocation size which is too big to overflow the 32bit value */
        if (NEMUWDDM_TRAILARRAY_MAXELEMENTSU32(NEMUWDDM_RESOURCE, aAllocations) < pRcInfo->cAllocInfos)
        {
            WARN(("number of allocations passed too big (%d), max is (%d)", pRcInfo->cAllocInfos, NEMUWDDM_TRAILARRAY_MAXELEMENTSU32(NEMUWDDM_RESOURCE, aAllocations)));
            return STATUS_INVALID_PARAMETER;
        }

        pResource = (PNEMUWDDM_RESOURCE)nemuWddmMemAllocZero(RT_OFFSETOF(NEMUWDDM_RESOURCE, aAllocations[pRcInfo->cAllocInfos]));
        if (!pResource)
        {
            WARN(("nemuWddmMemAllocZero failed for (%d) allocations", pRcInfo->cAllocInfos));
            return STATUS_NO_MEMORY;
        }

        pResource->cRefs = 1;
        pResource->cAllocations = pRcInfo->cAllocInfos;
        pResource->fFlags = pRcInfo->fFlags;
        pResource->RcDesc = pRcInfo->RcDesc;
    }


    for (UINT i = 0; i < pCreateAllocation->NumAllocations; ++i)
    {
        Status = nemuWddmAllocationCreate(pDevExt, pResource, i, &pCreateAllocation->pAllocationInfo[i]);
        if (Status != STATUS_SUCCESS)
        {
            WARN(("nemuWddmAllocationCreate(%d) failed, Status(0x%x)", i, Status));
            /* note: i-th allocation is expected to be cleared in a fail handling code above */
            for (UINT j = 0; j < i; ++j)
            {
                PNEMUWDDM_ALLOCATION pAllocation = (PNEMUWDDM_ALLOCATION)pCreateAllocation->pAllocationInfo[j].hAllocation;
                nemuWddmAllocationCleanup(pDevExt, pAllocation);
                nemuWddmAllocationDestroy(pAllocation);
            }
            break;
        }
    }

    if (Status == STATUS_SUCCESS)
    {
        pCreateAllocation->hResource = pResource;
    }
    else
    {
        if (pResource)
            nemuWddmResourceRelease(pResource);
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiDestroyAllocation(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_DESTROYALLOCATION*  pDestroyAllocation)
{
    /* DxgkDdiDestroyAllocation should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;

    PNEMUWDDM_RESOURCE pRc = (PNEMUWDDM_RESOURCE)pDestroyAllocation->hResource;
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    if (pRc)
    {
        Assert(pRc->cAllocations == pDestroyAllocation->NumAllocations);
    }

    for (UINT i = 0; i < pDestroyAllocation->NumAllocations; ++i)
    {
        PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pDestroyAllocation->pAllocationList[i];
        Assert(pAlloc->pResource == pRc);
        nemuWddmAllocationCleanupAssignment(pDevExt, pAlloc);
        /* wait for all current allocation-related ops are completed */
        nemuWddmAllocationCleanup(pDevExt, pAlloc);
        if (pAlloc->hSharedHandle && pAlloc->AllocData.hostID)
            NemuVdmaChromiumParameteriCRSubmit(pDevExt, GL_PIN_TEXTURE_CLEAR_CR, pAlloc->AllocData.hostID);
        nemuWddmAllocationDestroy(pAlloc);
    }

    if (pRc)
    {
        /* wait for all current resource-related ops are completed */
        nemuWddmResourceWaitDereference(pRc);
        nemuWddmResourceRelease(pRc);
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

/**
 * DxgkDdiDescribeAllocation
 */
NTSTATUS
APIENTRY
DxgkDdiDescribeAllocation(
    CONST HANDLE  hAdapter,
    DXGKARG_DESCRIBEALLOCATION*  pDescribeAllocation)
{
//    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    PNEMUWDDM_ALLOCATION pAllocation = (PNEMUWDDM_ALLOCATION)pDescribeAllocation->hAllocation;
    pDescribeAllocation->Width = pAllocation->AllocData.SurfDesc.width;
    pDescribeAllocation->Height = pAllocation->AllocData.SurfDesc.height;
    pDescribeAllocation->Format = pAllocation->AllocData.SurfDesc.format;
    memset (&pDescribeAllocation->MultisampleMethod, 0, sizeof (pDescribeAllocation->MultisampleMethod));
    pDescribeAllocation->RefreshRate.Numerator = 60000;
    pDescribeAllocation->RefreshRate.Denominator = 1000;
    pDescribeAllocation->PrivateDriverFormatAttribute = 0;

//    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

/**
 * DxgkDdiGetStandardAllocationDriverData
 */
NTSTATUS
APIENTRY
DxgkDdiGetStandardAllocationDriverData(
    CONST HANDLE  hAdapter,
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA*  pGetStandardAllocationDriverData)
{
    /* DxgkDdiGetStandardAllocationDriverData should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_ALLOCINFO pAllocInfo = NULL;

    switch (pGetStandardAllocationDriverData->StandardAllocationType)
    {
        case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE:
        {
            LOGF(("D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE"));
            if(pGetStandardAllocationDriverData->pAllocationPrivateDriverData)
            {
                pAllocInfo = (PNEMUWDDM_ALLOCINFO)pGetStandardAllocationDriverData->pAllocationPrivateDriverData;
                memset (pAllocInfo, 0, sizeof (NEMUWDDM_ALLOCINFO));
                pAllocInfo->enmType = NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE;
                pAllocInfo->SurfDesc.width = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->Width;
                pAllocInfo->SurfDesc.height = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->Height;
                pAllocInfo->SurfDesc.format = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->Format;
                pAllocInfo->SurfDesc.bpp = nemuWddmCalcBitsPerPixel(pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.pitch = nemuWddmCalcPitch(pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->Width, pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.cbSize = nemuWddmCalcSize(pAllocInfo->SurfDesc.pitch, pAllocInfo->SurfDesc.height, pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.depth = 0;
                pAllocInfo->SurfDesc.slicePitch = 0;
                pAllocInfo->SurfDesc.RefreshRate = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->RefreshRate;
                pAllocInfo->SurfDesc.VidPnSourceId = pGetStandardAllocationDriverData->pCreateSharedPrimarySurfaceData->VidPnSourceId;
            }
            pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = sizeof (NEMUWDDM_ALLOCINFO);

            pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;
            break;
        }
        case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE:
        {
            LOGF(("D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE"));
            UINT bpp = nemuWddmCalcBitsPerPixel(pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Format);
            Assert(bpp);
            if (bpp != 0)
            {
                UINT Pitch = nemuWddmCalcPitch(pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Width, pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Format);
                pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Pitch = Pitch;

                /* @todo: need [d/q]word align?? */

                if (pGetStandardAllocationDriverData->pAllocationPrivateDriverData)
                {
                    pAllocInfo = (PNEMUWDDM_ALLOCINFO)pGetStandardAllocationDriverData->pAllocationPrivateDriverData;
                    pAllocInfo->enmType = NEMUWDDM_ALLOC_TYPE_STD_SHADOWSURFACE;
                    pAllocInfo->SurfDesc.width = pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Width;
                    pAllocInfo->SurfDesc.height = pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Height;
                    pAllocInfo->SurfDesc.format = pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Format;
                    pAllocInfo->SurfDesc.bpp = nemuWddmCalcBitsPerPixel(pAllocInfo->SurfDesc.format);
                    pAllocInfo->SurfDesc.pitch = nemuWddmCalcPitch(pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Width, pAllocInfo->SurfDesc.format);
                    pAllocInfo->SurfDesc.cbSize = nemuWddmCalcSize(pAllocInfo->SurfDesc.pitch, pAllocInfo->SurfDesc.height, pAllocInfo->SurfDesc.format);
                    pAllocInfo->SurfDesc.depth = 0;
                    pAllocInfo->SurfDesc.slicePitch = 0;
                    pAllocInfo->SurfDesc.RefreshRate.Numerator = 0;
                    pAllocInfo->SurfDesc.RefreshRate.Denominator = 1000;
                    pAllocInfo->SurfDesc.VidPnSourceId = D3DDDI_ID_UNINITIALIZED;

                    pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Pitch = pAllocInfo->SurfDesc.pitch;
                }
                pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = sizeof (NEMUWDDM_ALLOCINFO);

                pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;
            }
            else
            {
                LOGREL(("Invalid format (%d)", pGetStandardAllocationDriverData->pCreateShadowSurfaceData->Format));
                Status = STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE:
        {
            LOGF(("D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE"));
            if(pGetStandardAllocationDriverData->pAllocationPrivateDriverData)
            {
                pAllocInfo = (PNEMUWDDM_ALLOCINFO)pGetStandardAllocationDriverData->pAllocationPrivateDriverData;
                pAllocInfo->enmType = NEMUWDDM_ALLOC_TYPE_STD_STAGINGSURFACE;
                pAllocInfo->SurfDesc.width = pGetStandardAllocationDriverData->pCreateStagingSurfaceData->Width;
                pAllocInfo->SurfDesc.height = pGetStandardAllocationDriverData->pCreateStagingSurfaceData->Height;
                pAllocInfo->SurfDesc.format = D3DDDIFMT_X8R8G8B8; /* staging has always always D3DDDIFMT_X8R8G8B8 */
                pAllocInfo->SurfDesc.bpp = nemuWddmCalcBitsPerPixel(pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.pitch = nemuWddmCalcPitch(pGetStandardAllocationDriverData->pCreateStagingSurfaceData->Width, pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.cbSize = nemuWddmCalcSize(pAllocInfo->SurfDesc.pitch, pAllocInfo->SurfDesc.height, pAllocInfo->SurfDesc.format);
                pAllocInfo->SurfDesc.depth = 0;
                pAllocInfo->SurfDesc.slicePitch = 0;
                pAllocInfo->SurfDesc.RefreshRate.Numerator = 0;
                pAllocInfo->SurfDesc.RefreshRate.Denominator = 1000;
                pAllocInfo->SurfDesc.VidPnSourceId = D3DDDI_ID_UNINITIALIZED;

                pGetStandardAllocationDriverData->pCreateStagingSurfaceData->Pitch = pAllocInfo->SurfDesc.pitch;
            }
            pGetStandardAllocationDriverData->AllocationPrivateDriverDataSize = sizeof (NEMUWDDM_ALLOCINFO);

            pGetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;
            break;
        }
//#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
//        case D3DKMDT_STANDARDALLOCATION_GDISURFACE:
//# error port to Win7 DDI
//              break;
//#endif
        default:
            LOGREL(("Invalid allocation type (%d)", pGetStandardAllocationDriverData->StandardAllocationType));
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiAcquireSwizzlingRange(
    CONST HANDLE  hAdapter,
    DXGKARG_ACQUIRESWIZZLINGRANGE*  pAcquireSwizzlingRange)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiReleaseSwizzlingRange(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_RELEASESWIZZLINGRANGE*  pReleaseSwizzlingRange)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

#ifdef NEMU_WITH_CROGL
static NTSTATUS
APIENTRY
DxgkDdiPatchNew(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_PATCH*  pPatch)
{
    /* DxgkDdiPatch should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    uint8_t * pPrivateBuf = (uint8_t*)((uint8_t*)pPatch->pDmaBufferPrivateData + pPatch->DmaBufferPrivateDataSubmissionStartOffset);
    UINT cbPatchBuff = pPatch->DmaBufferPrivateDataSubmissionEndOffset - pPatch->DmaBufferPrivateDataSubmissionStartOffset;

    for (UINT i = pPatch->PatchLocationListSubmissionStart; i < pPatch->PatchLocationListSubmissionLength; ++i)
    {
        const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[i];
        Assert(pPatchList->AllocationIndex < pPatch->AllocationListSize);
        const DXGK_ALLOCATIONLIST *pAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
        if (!pAllocationList->SegmentId)
        {
            WARN(("no segment id specified"));
            continue;
        }

        if (pPatchList->PatchOffset == ~0UL)
        {
            /* this is a dummy patch request, ignore */
            continue;
        }

        if (pPatchList->PatchOffset >= cbPatchBuff)
        {
            WARN(("pPatchList->PatchOffset(%d) >= cbPatchBuff(%d)", pPatchList->PatchOffset, cbPatchBuff));
            return STATUS_INVALID_PARAMETER;
        }

        NEMUCMDVBVAOFFSET *poffVram = (NEMUCMDVBVAOFFSET*)(pPrivateBuf + pPatchList->PatchOffset);
        Assert(pAllocationList->SegmentId);
        Assert(!pAllocationList->PhysicalAddress.HighPart);
        Assert(!(pAllocationList->PhysicalAddress.QuadPart & 0xfffUL)); /* <- just a check to ensure allocation offset does not go here */
        *poffVram = pAllocationList->PhysicalAddress.LowPart + pPatchList->AllocationOffset;
    }

    return STATUS_SUCCESS;
}
#endif

static NTSTATUS
APIENTRY
DxgkDdiPatchLegacy(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_PATCH*  pPatch)
{
    /* DxgkDdiPatch should be made pageable. */
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    /* Value == 2 is Present
     * Value == 4 is RedirectedPresent
     * we do not expect any other flags to be set here */
//    Assert(pPatch->Flags.Value == 2 || pPatch->Flags.Value == 4);
    if (pPatch->DmaBufferPrivateDataSubmissionEndOffset - pPatch->DmaBufferPrivateDataSubmissionStartOffset >= sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        Assert(pPatch->DmaBufferPrivateDataSize >= sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR));
        NEMUWDDM_DMA_PRIVATEDATA_BASEHDR *pPrivateDataBase = (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR*)((uint8_t*)pPatch->pDmaBufferPrivateData + pPatch->DmaBufferPrivateDataSubmissionStartOffset);
        switch (pPrivateDataBase->enmCmd)
        {
            case NEMUVDMACMD_TYPE_DMA_PRESENT_BLT:
            {
                PNEMUWDDM_DMA_PRIVATEDATA_BLT pBlt = (PNEMUWDDM_DMA_PRIVATEDATA_BLT)pPrivateDataBase;
                Assert(pPatch->PatchLocationListSubmissionLength == 2);
                const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart];
                Assert(pPatchList->AllocationIndex == DXGK_PRESENT_SOURCE_INDEX);
                Assert(pPatchList->PatchOffset == 0);
                const DXGK_ALLOCATIONLIST *pSrcAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                Assert(pSrcAllocationList->SegmentId);
                pBlt->Blt.SrcAlloc.segmentIdAlloc = pSrcAllocationList->SegmentId;
                pBlt->Blt.SrcAlloc.offAlloc = (NEMUVIDEOOFFSET)pSrcAllocationList->PhysicalAddress.QuadPart;

                pPatchList = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart + 1];
                Assert(pPatchList->AllocationIndex == DXGK_PRESENT_DESTINATION_INDEX);
                Assert(pPatchList->PatchOffset == 4);
                const DXGK_ALLOCATIONLIST *pDstAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                Assert(pDstAllocationList->SegmentId);
                pBlt->Blt.DstAlloc.segmentIdAlloc = pDstAllocationList->SegmentId;
                pBlt->Blt.DstAlloc.offAlloc = (NEMUVIDEOOFFSET)pDstAllocationList->PhysicalAddress.QuadPart;
                break;
            }
            case NEMUVDMACMD_TYPE_DMA_PRESENT_FLIP:
            {
                PNEMUWDDM_DMA_PRIVATEDATA_FLIP pFlip = (PNEMUWDDM_DMA_PRIVATEDATA_FLIP)pPrivateDataBase;
                Assert(pPatch->PatchLocationListSubmissionLength == 1);
                const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart];
                Assert(pPatchList->AllocationIndex == DXGK_PRESENT_SOURCE_INDEX);
                Assert(pPatchList->PatchOffset == 0);
                const DXGK_ALLOCATIONLIST *pSrcAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                Assert(pSrcAllocationList->SegmentId);
                pFlip->Flip.Alloc.segmentIdAlloc = pSrcAllocationList->SegmentId;
                pFlip->Flip.Alloc.offAlloc = (NEMUVIDEOOFFSET)pSrcAllocationList->PhysicalAddress.QuadPart;
                break;
            }
            case NEMUVDMACMD_TYPE_DMA_PRESENT_CLRFILL:
            {
                PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL pCF = (PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL)pPrivateDataBase;
                Assert(pPatch->PatchLocationListSubmissionLength == 1);
                const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[pPatch->PatchLocationListSubmissionStart];
                Assert(pPatchList->AllocationIndex == DXGK_PRESENT_DESTINATION_INDEX);
                Assert(pPatchList->PatchOffset == 0);
                const DXGK_ALLOCATIONLIST *pDstAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                Assert(pDstAllocationList->SegmentId);
                pCF->ClrFill.Alloc.segmentIdAlloc = pDstAllocationList->SegmentId;
                pCF->ClrFill.Alloc.offAlloc = (NEMUVIDEOOFFSET)pDstAllocationList->PhysicalAddress.QuadPart;
                break;
            }
            case NEMUVDMACMD_TYPE_DMA_NOP:
                break;
            case NEMUVDMACMD_TYPE_CHROMIUM_CMD:
            {
                uint8_t * pPrivateBuf = (uint8_t*)pPrivateDataBase;
                for (UINT i = pPatch->PatchLocationListSubmissionStart; i < pPatch->PatchLocationListSubmissionLength; ++i)
                {
                    const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[i];
                    Assert(pPatchList->AllocationIndex < pPatch->AllocationListSize);
                    const DXGK_ALLOCATIONLIST *pAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                    Assert(pAllocationList->SegmentId);
                    if (pAllocationList->SegmentId)
                    {
                        DXGK_ALLOCATIONLIST *pAllocation2Patch = (DXGK_ALLOCATIONLIST*)(pPrivateBuf + pPatchList->PatchOffset);
                        pAllocation2Patch->SegmentId = pAllocationList->SegmentId;
                        pAllocation2Patch->PhysicalAddress.QuadPart = pAllocationList->PhysicalAddress.QuadPart + pPatchList->AllocationOffset;
                        Assert(!(pAllocationList->PhysicalAddress.QuadPart & 0xfffUL)); /* <- just a check to ensure allocation offset does not go here */
                    }
                }
                break;
            }
            default:
            {
                AssertBreakpoint();
                uint8_t *pBuf = ((uint8_t *)pPatch->pDmaBuffer) + pPatch->DmaBufferSubmissionStartOffset;
                for (UINT i = pPatch->PatchLocationListSubmissionStart; i < pPatch->PatchLocationListSubmissionLength; ++i)
                {
                    const D3DDDI_PATCHLOCATIONLIST* pPatchList = &pPatch->pPatchLocationList[i];
                    Assert(pPatchList->AllocationIndex < pPatch->AllocationListSize);
                    const DXGK_ALLOCATIONLIST *pAllocationList = &pPatch->pAllocationList[pPatchList->AllocationIndex];
                    if (pAllocationList->SegmentId)
                    {
                        Assert(pPatchList->PatchOffset < (pPatch->DmaBufferSubmissionEndOffset - pPatch->DmaBufferSubmissionStartOffset));
                        *((NEMUVIDEOOFFSET*)(pBuf+pPatchList->PatchOffset)) = (NEMUVIDEOOFFSET)pAllocationList->PhysicalAddress.QuadPart;
                    }
                    else
                    {
                        /* sanity */
                        if (pPatch->Flags.Value == 2 || pPatch->Flags.Value == 4)
                            Assert(i == 0);
                    }
                }
                break;
            }
        }
    }
    else if (pPatch->DmaBufferPrivateDataSubmissionEndOffset == pPatch->DmaBufferPrivateDataSubmissionStartOffset)
    {
        /* this is a NOP, just return success */
//        LOG(("null data size, treating as NOP"));
        return STATUS_SUCCESS;
    }
    else
    {
        WARN(("DmaBufferPrivateDataSubmissionEndOffset (%d) - DmaBufferPrivateDataSubmissionStartOffset (%d) < sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR) (%d)",
                pPatch->DmaBufferPrivateDataSubmissionEndOffset,
                pPatch->DmaBufferPrivateDataSubmissionStartOffset,
                sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }

    LOGF(("LEAVE, context(0x%x)", hAdapter));

    return Status;
}

typedef struct NEMUWDDM_CALL_ISR
{
    PNEMUMP_DEVEXT pDevExt;
    ULONG MessageNumber;
} NEMUWDDM_CALL_ISR, *PNEMUWDDM_CALL_ISR;

static BOOLEAN nemuWddmCallIsrCb(PVOID Context)
{
    PNEMUWDDM_CALL_ISR pdc = (PNEMUWDDM_CALL_ISR)Context;
    PNEMUMP_DEVEXT pDevExt = pdc->pDevExt;
#ifdef NEMU_WITH_CROGL
    if (pDevExt->fCmdVbvaEnabled)
        return DxgkDdiInterruptRoutineNew(pDevExt, pdc->MessageNumber);
#endif
    return DxgkDdiInterruptRoutineLegacy(pDevExt, pdc->MessageNumber);
}

NTSTATUS nemuWddmCallIsr(PNEMUMP_DEVEXT pDevExt)
{
    NEMUWDDM_CALL_ISR context;
    context.pDevExt = pDevExt;
    context.MessageNumber = 0;
    BOOLEAN bRet;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            nemuWddmCallIsrCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRet);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

#ifdef NEMU_WITH_CRHGSMI
DECLCALLBACK(VOID) nemuWddmDmaCompleteChromiumCmd(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, PVOID pvContext)
{
    PNEMUVDMACBUF_DR pDr = (PNEMUVDMACBUF_DR)pvContext;
    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUVDMACMD_BODY(pHdr, NEMUVDMACMD_CHROMIUM_CMD);
    UINT cBufs = pBody->cBuffers;
    nemuVdmaCBufDrFree(&pDevExt->u.primary.Vdma, pDr);
}
#endif

#ifdef NEMU_WITH_CROGL
static NTSTATUS
APIENTRY
DxgkDdiSubmitCommandNew(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SUBMITCOMMAND*  pSubmitCommand)
{
    /* DxgkDdiSubmitCommand runs at dispatch, should not be pageable. */

//    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
#ifdef DEBUG
    PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pSubmitCommand->hContext;
    Assert(pContext);
    Assert(pContext->pDevice);
    Assert(pContext->pDevice->pAdapter == pDevExt);
    Assert(!pSubmitCommand->DmaBufferSegmentId);
#endif

    uint32_t cbCmd = pSubmitCommand->DmaBufferPrivateDataSubmissionEndOffset - pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset;
    uint32_t cbDma = pSubmitCommand->DmaBufferSubmissionEndOffset - pSubmitCommand->DmaBufferSubmissionStartOffset;
    NEMUCMDVBVA_HDR *pHdr;
    NEMUCMDVBVA_HDR NopCmd;
    uint32_t cbCurCmd, cbCurDma;
    if (cbCmd < sizeof (NEMUCMDVBVA_HDR))
    {
        if (cbCmd || cbDma)
        {
            WARN(("invalid command data"));
            return STATUS_INVALID_PARAMETER;
        }
        Assert(!cbDma);
        NopCmd.u8OpCode = NEMUCMDVBVA_OPTYPE_NOPCMD;
        NopCmd.u8Flags = 0;
        NopCmd.u8State = NEMUCMDVBVA_STATE_SUBMITTED;
        NopCmd.u2.complexCmdEl.u16CbCmdHost = sizeof (NEMUCMDVBVA_HDR);
        NopCmd.u2.complexCmdEl.u16CbCmdGuest = 0;
        cbCmd = sizeof (NEMUCMDVBVA_HDR);
        pHdr = &NopCmd;
        cbCurCmd = sizeof (NEMUCMDVBVA_HDR);
        cbCurDma = 0;
    }
    else
    {
        pHdr = (NEMUCMDVBVA_HDR*)(((uint8_t*)pSubmitCommand->pDmaBufferPrivateData) + pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset);
        cbCurCmd = pHdr->u2.complexCmdEl.u16CbCmdHost;
        cbCurDma = pHdr->u2.complexCmdEl.u16CbCmdGuest;
    }


    NEMUCMDVBVA_HDR *pDstHdr, *pCurDstCmd;
    if (cbCmd != cbCurCmd || cbCurDma != cbDma)
    {
        if (cbCmd < cbCurCmd || cbDma < cbCurDma)
        {
            WARN(("incorrect buffer size"));
            return STATUS_INVALID_PARAMETER;
        }

        pDstHdr = NemuCmdVbvaSubmitLock(pDevExt, &pDevExt->CmdVbva, cbCmd + sizeof (NEMUCMDVBVA_HDR));
        if (!pDstHdr)
        {
            WARN(("NemuCmdVbvaSubmitLock failed"));
            return STATUS_UNSUCCESSFUL;
        }

        pDstHdr->u8OpCode = NEMUCMDVBVA_OPTYPE_COMPLEXCMD;
        pDstHdr->u8Flags = 0;
        pDstHdr->u.u8PrimaryID = 0;

        pCurDstCmd = pDstHdr + 1;
    }
    else
    {
        pDstHdr = NemuCmdVbvaSubmitLock(pDevExt, &pDevExt->CmdVbva, cbCmd);
        if (!pDstHdr)
        {
            WARN(("NemuCmdVbvaSubmitLock failed"));
            return STATUS_UNSUCCESSFUL;
        }
        pCurDstCmd = pDstHdr;
    }

    PHYSICAL_ADDRESS phAddr;
    phAddr.QuadPart = pSubmitCommand->DmaBufferPhysicalAddress.QuadPart + pSubmitCommand->DmaBufferSubmissionStartOffset;
    NTSTATUS Status = STATUS_SUCCESS;
    for (;;)
    {
        switch (pHdr->u8OpCode)
        {
            case NEMUCMDVBVA_OPTYPE_SYSMEMCMD:
            {
                NEMUCMDVBVA_SYSMEMCMD *pSysMem = (NEMUCMDVBVA_SYSMEMCMD*)pHdr;
                if (pSubmitCommand->DmaBufferPhysicalAddress.QuadPart & PAGE_OFFSET_MASK)
                {
                    WARN(("command should be page aligned for now"));
                    return STATUS_INVALID_PARAMETER;
                }
                pSysMem->phCmd = (NEMUCMDVBVAPHADDR)(pSubmitCommand->DmaBufferPhysicalAddress.QuadPart + pSubmitCommand->DmaBufferSubmissionStartOffset);
#ifdef DEBUG
                {
                    uint32_t cbRealDmaCmd = (pSysMem->Hdr.u8Flags | (pSysMem->Hdr.u.u8PrimaryID << 8));
                    Assert(cbRealDmaCmd >= cbDma);
                    if (cbDma < cbRealDmaCmd)
                        WARN(("parrtial sysmem transfer"));
                }
#endif
                break;
            }
            default:
                break;
        }

        memcpy(pCurDstCmd, pHdr, cbCurCmd);
        pCurDstCmd->u2.complexCmdEl.u16CbCmdGuest = 0;

        phAddr.QuadPart += cbCurDma;
        pHdr = (NEMUCMDVBVA_HDR*)(((uint8_t*)pHdr) + cbCurCmd);
        pCurDstCmd = (NEMUCMDVBVA_HDR*)(((uint8_t*)pCurDstCmd) + cbCurCmd);
        cbCmd -= cbCurCmd;
        cbDma -= cbCurDma;
        if (!cbCmd)
        {
            if (cbDma)
            {
                WARN(("invalid param"));
                Status = STATUS_INVALID_PARAMETER;
            }
            break;
        }

        if (cbCmd < sizeof (NEMUCMDVBVA_HDR))
        {
            WARN(("invalid param"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        cbCurCmd = pHdr->u2.complexCmdEl.u16CbCmdHost;
        cbCurDma = pHdr->u2.complexCmdEl.u16CbCmdGuest;

        if (cbCmd < cbCurCmd)
        {
            WARN(("invalid param"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (cbDma < cbCurDma)
        {
            WARN(("invalid param"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
    }

    uint32_t u32FenceId = pSubmitCommand->SubmissionFenceId;

    if (!NT_SUCCESS(Status))
    {
        /* nop the entire command on failure */
        pDstHdr->u8OpCode = NEMUCMDVBVA_OPTYPE_NOPCMD;
        pDstHdr->u8Flags = 0;
        pDstHdr->u.i8Result = 0;
        u32FenceId = 0;
    }

    NemuCmdVbvaSubmitUnlock(pDevExt, &pDevExt->CmdVbva, pDstHdr, u32FenceId);

    return Status;
}
#endif

static NTSTATUS
APIENTRY
DxgkDdiSubmitCommandLegacy(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SUBMITCOMMAND*  pSubmitCommand)
{
    /* DxgkDdiSubmitCommand runs at dispatch, should not be pageable. */
    NTSTATUS Status = STATUS_SUCCESS;

//    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pSubmitCommand->hContext;
    PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR pPrivateDataBase = NULL;
    NEMUVDMACMD_TYPE enmCmd = NEMUVDMACMD_TYPE_UNDEFINED;
    Assert(pContext);
    Assert(pContext->pDevice);
    Assert(pContext->pDevice->pAdapter == pDevExt);
    Assert(!pSubmitCommand->DmaBufferSegmentId);

    /* the DMA command buffer is located in system RAM, the host will need to pick it from there */
    //BufInfo.fFlags = 0; /* see NEMUVDMACBUF_FLAG_xx */
    if (pSubmitCommand->DmaBufferPrivateDataSubmissionEndOffset - pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset >= sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        pPrivateDataBase = (PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR)((uint8_t*)pSubmitCommand->pDmaBufferPrivateData + pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset);
        Assert(pPrivateDataBase);
        enmCmd = pPrivateDataBase->enmCmd;
    }
    else if (pSubmitCommand->DmaBufferPrivateDataSubmissionEndOffset == pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset)
    {
        enmCmd = NEMUVDMACMD_TYPE_DMA_NOP;
    }
    else
    {
        WARN(("DmaBufferPrivateDataSubmissionEndOffset (%d) - DmaBufferPrivateDataSubmissionStartOffset (%d) < sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR) (%d)",
                pSubmitCommand->DmaBufferPrivateDataSubmissionEndOffset,
                pSubmitCommand->DmaBufferPrivateDataSubmissionStartOffset,
                sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }

    switch (enmCmd)
    {
        case NEMUVDMACMD_TYPE_DMA_PRESENT_BLT:
        {
            NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR *pPrivateData = (NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR*)pPrivateDataBase;
            PNEMUWDDM_DMA_PRIVATEDATA_BLT pBlt = (PNEMUWDDM_DMA_PRIVATEDATA_BLT)pPrivateData;
            PNEMUWDDM_ALLOCATION pDstAlloc = pBlt->Blt.DstAlloc.pAlloc;
            PNEMUWDDM_ALLOCATION pSrcAlloc = pBlt->Blt.SrcAlloc.pAlloc;
            BOOLEAN fSrcChanged;
            BOOLEAN fDstChanged;

            fDstChanged = nemuWddmAddrSetVram(&pDstAlloc->AllocData.Addr, pBlt->Blt.DstAlloc.segmentIdAlloc, pBlt->Blt.DstAlloc.offAlloc);
            fSrcChanged = nemuWddmAddrSetVram(&pSrcAlloc->AllocData.Addr, pBlt->Blt.SrcAlloc.segmentIdAlloc, pBlt->Blt.SrcAlloc.offAlloc);

            if (NEMUWDDM_IS_FB_ALLOCATION(pDevExt, pDstAlloc))
            {
                NEMUWDDM_SOURCE *pSource = &pDevExt->aSources[pDstAlloc->AllocData.SurfDesc.VidPnSourceId];

                Assert(pDstAlloc->AllocData.SurfDesc.VidPnSourceId < NEMU_VIDEO_MAX_SCREENS);
#if 0
                if (NEMUWDDM_IS_FB_ALLOCATION(pDevExt, pDstAlloc) && pDstAlloc->AllocData.hostID)
                {
                    if (pSource->AllocData.hostID != pDstAlloc->AllocData.hostID)
                    {
                        pSource->AllocData.hostID = pDstAlloc->AllocData.hostID;
                        fDstChanged = TRUE;
                    }

                    if (fDstChanged)
                        pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_LOCATION;
                }
#endif
            }

            Status = nemuVdmaProcessBltCmd(pDevExt, pContext, pBlt);
            if (!NT_SUCCESS(Status))
                WARN(("nemuVdmaProcessBltCmd failed, Status 0x%x", Status));

            Status = nemuVdmaDdiCmdFenceComplete(pDevExt, pContext->NodeOrdinal, pSubmitCommand->SubmissionFenceId,
                    NT_SUCCESS(Status) ? DXGK_INTERRUPT_DMA_COMPLETED : DXGK_INTERRUPT_DMA_FAULTED);
            break;
        }
        case NEMUVDMACMD_TYPE_DMA_PRESENT_FLIP:
        {
            NEMUWDDM_DMA_PRIVATEDATA_FLIP *pFlip = (NEMUWDDM_DMA_PRIVATEDATA_FLIP*)pPrivateDataBase;
            PNEMUWDDM_ALLOCATION pAlloc = pFlip->Flip.Alloc.pAlloc;
            NEMUWDDM_SOURCE *pSource = &pDevExt->aSources[pAlloc->AllocData.SurfDesc.VidPnSourceId];
            nemuWddmAddrSetVram(&pAlloc->AllocData.Addr, pFlip->Flip.Alloc.segmentIdAlloc, pFlip->Flip.Alloc.offAlloc);
            nemuWddmAssignPrimary(pSource, pAlloc, pAlloc->AllocData.SurfDesc.VidPnSourceId);
            nemuWddmGhDisplayCheckSetInfoFromSource(pDevExt, pSource);

            Status = nemuVdmaDdiCmdFenceComplete(pDevExt, pContext->NodeOrdinal, pSubmitCommand->SubmissionFenceId,
                    NT_SUCCESS(Status) ? DXGK_INTERRUPT_DMA_COMPLETED : DXGK_INTERRUPT_DMA_FAULTED);
            break;
        }
        case NEMUVDMACMD_TYPE_DMA_PRESENT_CLRFILL:
        {
            PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL pCF = (PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL)pPrivateDataBase;
            nemuWddmAddrSetVram(&pCF->ClrFill.Alloc.pAlloc->AllocData.Addr, pCF->ClrFill.Alloc.segmentIdAlloc, pCF->ClrFill.Alloc.offAlloc);

            Status = nemuVdmaProcessClrFillCmd(pDevExt, pContext, pCF);
            if (!NT_SUCCESS(Status))
                WARN(("nemuVdmaProcessClrFillCmd failed, Status 0x%x", Status));

            Status = nemuVdmaDdiCmdFenceComplete(pDevExt, pContext->NodeOrdinal, pSubmitCommand->SubmissionFenceId,
                    NT_SUCCESS(Status) ? DXGK_INTERRUPT_DMA_COMPLETED : DXGK_INTERRUPT_DMA_FAULTED);
            break;
        }
        case NEMUVDMACMD_TYPE_DMA_NOP:
        {
            Status = nemuVdmaDdiCmdFenceComplete(pDevExt, pContext->NodeOrdinal, pSubmitCommand->SubmissionFenceId, DXGK_INTERRUPT_DMA_COMPLETED);
            Assert(Status == STATUS_SUCCESS);
            break;
        }
        default:
        {
            WARN(("unexpected command %d", enmCmd));
#if 0 //def NEMU_WITH_VDMA
            NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR *pPrivateData = (NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR*)pPrivateDataBase;
            PNEMUVDMACBUF_DR pDr = nemuVdmaCBufDrCreate (&pDevExt->u.primary.Vdma, 0);
            if (!pDr)
            {
                /* @todo: try flushing.. */
                LOGREL(("nemuVdmaCBufDrCreate returned NULL"));
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            // nemuVdmaCBufDrCreate zero initializes the pDr
            //pDr->fFlags = 0;
            pDr->cbBuf = pSubmitCommand->DmaBufferSubmissionEndOffset - pSubmitCommand->DmaBufferSubmissionStartOffset;
            pDr->u32FenceId = pSubmitCommand->SubmissionFenceId;
            pDr->rc = VERR_NOT_IMPLEMENTED;
            if (pPrivateData)
                pDr->u64GuestContext = (uint64_t)pPrivateData->pContext;
        //    else    // nemuVdmaCBufDrCreate zero initializes the pDr
        //        pDr->u64GuestContext = NULL;
            pDr->Location.phBuf = pSubmitCommand->DmaBufferPhysicalAddress.QuadPart + pSubmitCommand->DmaBufferSubmissionStartOffset;

            nemuVdmaCBufDrSubmit(pDevExt, &pDevExt->u.primary.Vdma, pDr);
#endif
            break;
        }
    }
//    LOGF(("LEAVE, context(0x%x)", hAdapter));

    return Status;
}

#ifdef NEMU_WITH_CROGL
static NTSTATUS
APIENTRY
DxgkDdiPreemptCommandNew(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_PREEMPTCOMMAND*  pPreemptCommand)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    nemuVDbgBreakF();

    NemuCmdVbvaPreempt(pDevExt, &pDevExt->CmdVbva, pPreemptCommand->PreemptionFenceId);

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}
#endif

static NTSTATUS
APIENTRY
DxgkDdiPreemptCommandLegacy(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_PREEMPTCOMMAND*  pPreemptCommand)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertFailed();
    /* @todo: fixme: implement */

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

#ifdef NEMU_WITH_CROGL
/*
 * DxgkDdiBuildPagingBuffer
 */
static NTSTATUS
APIENTRY
DxgkDdiBuildPagingBufferNew(
    CONST HANDLE  hAdapter,
    DXGKARG_BUILDPAGINGBUFFER*  pBuildPagingBuffer)
{
    /* DxgkDdiBuildPagingBuffer should be made pageable. */
    PAGED_CODE();

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    uint32_t cbBuffer = 0, cbPrivateData = 0;

    LOGF(("ENTER, context(0x%x)", hAdapter));

    if (pBuildPagingBuffer->DmaBufferPrivateDataSize < sizeof (NEMUCMDVBVA_HDR))
    {
        WARN(("private data too small"));
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }

    NEMUCMDVBVA_HDR *pHdr = (NEMUCMDVBVA_HDR*)pBuildPagingBuffer->pDmaBufferPrivateData;

    switch (pBuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        {
#if 0
            if (!pBuildPagingBuffer->Transfer.Flags.AllocationIsIdle)
            {
                WARN(("allocation is not idle"));
                return STATUS_GRAPHICS_ALLOCATION_BUSY;
            }
#endif

            if (pBuildPagingBuffer->DmaBufferPrivateDataSize < sizeof (NEMUCMDVBVA_SYSMEMCMD))
            {
                WARN(("private data too small"));
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            }

            Assert(!pBuildPagingBuffer->Transfer.MdlOffset);

            if ((!pBuildPagingBuffer->Transfer.Source.SegmentId) == (!pBuildPagingBuffer->Transfer.Destination.SegmentId))
            {
                WARN(("we only support RAM <-> VRAM moves, Src Seg(%d), Dst Seg(%d)", pBuildPagingBuffer->Transfer.Source.SegmentId, pBuildPagingBuffer->Transfer.Destination.SegmentId));
                return STATUS_INVALID_PARAMETER;
            }

            PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pBuildPagingBuffer->Transfer.hAllocation;
            if (!pAlloc)
            {
                WARN(("allocation is null"));
                return STATUS_INVALID_PARAMETER;
            }

            if (pAlloc->AllocData.hostID)
            {
                cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
                cbPrivateData = sizeof (*pHdr);

                pHdr->u8OpCode = NEMUCMDVBVA_OPTYPE_NOPCMD;
                pHdr->u8Flags = 0;
                pHdr->u.u8PrimaryID = 0;
                pHdr->u8State = NEMUCMDVBVA_STATE_SUBMITTED;
                break;
            }

            if (pBuildPagingBuffer->DmaSize < sizeof (NEMUCMDVBVA_PAGING_TRANSFER))
            {
                WARN(("pBuildPagingBuffer->DmaSize(%d) < sizeof NEMUCMDVBVA_PAGING_TRANSFER (%d)", pBuildPagingBuffer->DmaSize , sizeof (NEMUCMDVBVA_PAGING_TRANSFER)));
                /* @todo: can this actually happen? what status to return? */
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            }

            NEMUCMDVBVA_PAGING_TRANSFER *pPaging = (NEMUCMDVBVA_PAGING_TRANSFER*)pBuildPagingBuffer->pDmaBuffer;
            pPaging->Hdr.u8OpCode = NEMUCMDVBVA_OPTYPE_PAGING_TRANSFER;
            /* sanity */
            pPaging->Hdr.u8Flags = 0;
            pPaging->Hdr.u8State = NEMUCMDVBVA_STATE_SUBMITTED;

            PMDL pMdl;
            uint32_t offVRAM;
            BOOLEAN fIn;
            UINT SegmentId;

            if (pBuildPagingBuffer->Transfer.Source.SegmentId)
            {
                SegmentId = pBuildPagingBuffer->Transfer.Source.SegmentId;
                Assert(!pBuildPagingBuffer->Transfer.Destination.SegmentId);
                Assert(!pBuildPagingBuffer->Transfer.Source.SegmentAddress.HighPart);
                offVRAM = pBuildPagingBuffer->Transfer.Source.SegmentAddress.LowPart;
                pMdl = pBuildPagingBuffer->Transfer.Destination.pMdl;
                fIn = FALSE;
            }
            else
            {
                SegmentId = pBuildPagingBuffer->Transfer.Destination.SegmentId;
                Assert(pBuildPagingBuffer->Transfer.Destination.SegmentId);
                Assert(!pBuildPagingBuffer->Transfer.Source.SegmentId);
                Assert(!pBuildPagingBuffer->Transfer.Destination.SegmentAddress.HighPart);
                offVRAM = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.LowPart;
                pMdl = pBuildPagingBuffer->Transfer.Source.pMdl;
                fIn = TRUE;
            }

            if (SegmentId != 1)
            {
                WARN(("SegmentId"));
                cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
                break;
            }

            Assert(!(pBuildPagingBuffer->Transfer.TransferSize & PAGE_OFFSET_MASK));
            Assert(!(offVRAM & PAGE_OFFSET_MASK));
            uint32_t cPages = (uint32_t)(pBuildPagingBuffer->Transfer.TransferSize >> PAGE_SHIFT);
            Assert(cPages > pBuildPagingBuffer->MultipassOffset);
            cPages -= pBuildPagingBuffer->MultipassOffset;
            uint32_t iFirstPage = pBuildPagingBuffer->MultipassOffset;
            uint32_t cPagesWritten;
            offVRAM += pBuildPagingBuffer->Transfer.TransferOffset + (pBuildPagingBuffer->MultipassOffset << PAGE_SHIFT);

            pPaging->Data.Alloc.u.offVRAM = offVRAM;
            if (fIn)
                pPaging->Hdr.u8Flags |= NEMUCMDVBVA_OPF_PAGING_TRANSFER_IN;
            cbBuffer = NemuCVDdiPTransferVRamSysBuildEls(pPaging, pMdl, iFirstPage, cPages, pBuildPagingBuffer->DmaSize, &cPagesWritten);
            if (cPagesWritten != cPages)
                pBuildPagingBuffer->MultipassOffset += cPagesWritten;
            else
                pBuildPagingBuffer->MultipassOffset = 0;

            NEMUCMDVBVA_SYSMEMCMD *pSysMemCmd = (NEMUCMDVBVA_SYSMEMCMD*)pBuildPagingBuffer->pDmaBufferPrivateData;

            cbPrivateData = sizeof (*pSysMemCmd);

            pSysMemCmd->Hdr.u8OpCode = NEMUCMDVBVA_OPTYPE_SYSMEMCMD;
            pSysMemCmd->Hdr.u8Flags = cbBuffer & 0xff;
            pSysMemCmd->Hdr.u.u8PrimaryID = (cbBuffer >> 8) & 0xff;
            pSysMemCmd->Hdr.u8State = NEMUCMDVBVA_STATE_SUBMITTED;
            pSysMemCmd->phCmd = 0;

            break;
        }
        case DXGK_OPERATION_FILL:
        {
            Assert(pBuildPagingBuffer->Fill.FillPattern == 0);
            PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pBuildPagingBuffer->Fill.hAllocation;
            if (!pAlloc)
            {
                WARN(("allocation is null"));
                return STATUS_INVALID_PARAMETER;
            }

            if (pAlloc->AllocData.hostID || pBuildPagingBuffer->Fill.Destination.SegmentId != 1)
            {
                if (!pAlloc->AllocData.hostID)
                {
                    WARN(("unexpected segment id"));
                }

                cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
                cbPrivateData = sizeof (*pHdr);

                pHdr->u8OpCode = NEMUCMDVBVA_OPTYPE_NOPCMD;
                pHdr->u8Flags = 0;
                pHdr->u.u8PrimaryID = 0;
                pHdr->u8State = NEMUCMDVBVA_STATE_SUBMITTED;
                break;
            }

            if (pBuildPagingBuffer->DmaBufferPrivateDataSize < sizeof (NEMUCMDVBVA_PAGING_FILL))
            {
                WARN(("private data too small"));
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            }

            NEMUCMDVBVA_PAGING_FILL *pFill = (NEMUCMDVBVA_PAGING_FILL*)pBuildPagingBuffer->pDmaBufferPrivateData;
            pFill->Hdr.u8OpCode = NEMUCMDVBVA_OPTYPE_PAGING_FILL;
            pFill->Hdr.u8Flags = 0;
            pFill->Hdr.u.u8PrimaryID = 0;
            pFill->Hdr.u8State = NEMUCMDVBVA_STATE_SUBMITTED;
            pFill->u32CbFill = (uint32_t)pBuildPagingBuffer->Fill.FillSize;
            pFill->u32Pattern = pBuildPagingBuffer->Fill.FillPattern;
            Assert(!pBuildPagingBuffer->Fill.Destination.SegmentAddress.HighPart);
            pFill->offVRAM = pBuildPagingBuffer->Fill.Destination.SegmentAddress.LowPart;

            cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
            cbPrivateData = sizeof (*pFill);

            break;
        }
        case DXGK_OPERATION_DISCARD_CONTENT:
        {
            PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pBuildPagingBuffer->DiscardContent.hAllocation;
            if (!pAlloc)
            {
                WARN(("allocation is null"));
                return STATUS_INVALID_PARAMETER;
            }
//            WARN(("Do we need to do anything here?"));
            break;
        }
        default:
        {
            WARN(("unsupported op (%d)", pBuildPagingBuffer->Operation));
            break;
        }
    }

    Assert(cbPrivateData >= sizeof (NEMUCMDVBVA_HDR) || pBuildPagingBuffer->Operation == DXGK_OPERATION_DISCARD_CONTENT);
    Assert(pBuildPagingBuffer->Operation == DXGK_OPERATION_DISCARD_CONTENT || cbBuffer);
    Assert(cbBuffer <= pBuildPagingBuffer->DmaSize);
    Assert(cbBuffer == 0 || cbBuffer >= sizeof (NEMUCMDVBVA_PAGING_TRANSFER) || cbBuffer == NEMUWDDM_DUMMY_DMABUFFER_SIZE);
    AssertCompile(NEMUWDDM_DUMMY_DMABUFFER_SIZE < 8);

    pHdr->u2.complexCmdEl.u16CbCmdHost = cbPrivateData;
    pHdr->u2.complexCmdEl.u16CbCmdGuest = cbBuffer;

    pBuildPagingBuffer->pDmaBuffer = ((uint8_t*)pBuildPagingBuffer->pDmaBuffer) + cbBuffer;
    pBuildPagingBuffer->pDmaBufferPrivateData = ((uint8_t*)pBuildPagingBuffer->pDmaBufferPrivateData) + cbPrivateData;

    LOGF(("LEAVE, context(0x%x)", hAdapter));

    if (pBuildPagingBuffer->MultipassOffset)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    return STATUS_SUCCESS;
}
#endif

static NTSTATUS
APIENTRY
DxgkDdiBuildPagingBufferLegacy(
    CONST HANDLE  hAdapter,
    DXGKARG_BUILDPAGINGBUFFER*  pBuildPagingBuffer)
{
    /* DxgkDdiBuildPagingBuffer should be made pageable. */
    PAGED_CODE();

    nemuVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    LOGF(("ENTER, context(0x%x)", hAdapter));

    uint32_t cbCmdDma = 0;

    /* @todo: */
    switch (pBuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        {
            cbCmdDma = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
#ifdef NEMU_WITH_VDMA
#if 0
            if ((!pBuildPagingBuffer->Transfer.Source.SegmentId) != (!pBuildPagingBuffer->Transfer.Destination.SegmentId))
            {
                PNEMUVDMACMD pCmd = (PNEMUVDMACMD)pBuildPagingBuffer->pDmaBuffer;
                pCmd->enmType = NEMUVDMACMD_TYPE_DMA_BPB_TRANSFER_VRAMSYS;
                pCmd->u32CmdSpecific = 0;
                PNEMUVDMACMD_DMA_BPB_TRANSFER_VRAMSYS pBody = NEMUVDMACMD_BODY(pCmd, NEMUVDMACMD_DMA_BPB_TRANSFER_VRAMSYS);
                PMDL pMdl;
                uint32_t cPages = (pBuildPagingBuffer->Transfer.TransferSize + 0xfff) >> 12;
                cPages -= pBuildPagingBuffer->MultipassOffset;
                uint32_t iFirstPage = pBuildPagingBuffer->Transfer.MdlOffset + pBuildPagingBuffer->MultipassOffset;
                uint32_t cPagesRemaining;
                if (pBuildPagingBuffer->Transfer.Source.SegmentId)
                {
                    uint64_t off = pBuildPagingBuffer->Transfer.Source.SegmentAddress.QuadPart;
                    off += pBuildPagingBuffer->Transfer.TransferOffset + (pBuildPagingBuffer->MultipassOffset << PAGE_SHIFT);
                    pBody->offVramBuf = off;
                    pMdl = pBuildPagingBuffer->Transfer.Source.pMdl;
                    pBody->fFlags = 0;//NEMUVDMACMD_DMA_BPB_TRANSFER_VRAMSYS_SYS2VRAM
                }
                else
                {
                    uint64_t off = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.QuadPart;
                    off += pBuildPagingBuffer->Transfer.TransferOffset + (pBuildPagingBuffer->MultipassOffset << PAGE_SHIFT);
                    pBody->offVramBuf = off;
                    pMdl = pBuildPagingBuffer->Transfer.Destination.pMdl;
                    pBody->fFlags = NEMUVDMACMD_DMA_BPB_TRANSFER_VRAMSYS_SYS2VRAM;
                }

                uint32_t sbBufferUsed = nemuWddmBpbTransferVRamSysBuildEls(pBody, pMdl, iFirstPage, cPages, pBuildPagingBuffer->DmaSize, &cPagesRemaining);
                Assert(sbBufferUsed);
            }

#else
            PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pBuildPagingBuffer->Transfer.hAllocation;
            Assert(pAlloc);
            if (pAlloc
                    && !pAlloc->fRcFlags.Overlay /* overlay surfaces actually contain a valid data */
                    && pAlloc->enmType != NEMUWDDM_ALLOC_TYPE_STD_SHADOWSURFACE  /* shadow primary - also */
                    && pAlloc->enmType != NEMUWDDM_ALLOC_TYPE_UMD_HGSMI_BUFFER /* hgsmi buffer - also */
                    )
            {
                /* we do not care about the others for now */
                Status = STATUS_SUCCESS;
                break;
            }
            UINT cbCmd = NEMUVDMACMD_SIZE(NEMUVDMACMD_DMA_BPB_TRANSFER);
            PNEMUVDMACBUF_DR pDr = nemuVdmaCBufDrCreate (&pDevExt->u.primary.Vdma, cbCmd);
            Assert(pDr);
            if (pDr)
            {
                SIZE_T cbTransfered = 0;
                SIZE_T cbTransferSize = pBuildPagingBuffer->Transfer.TransferSize;
                PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
                do
                {
                    // nemuVdmaCBufDrCreate zero initializes the pDr
                    pDr->fFlags = NEMUVDMACBUF_FLAG_BUF_FOLLOWS_DR;
                    pDr->cbBuf = cbCmd;
                    pDr->rc = VERR_NOT_IMPLEMENTED;

                    pHdr->enmType = NEMUVDMACMD_TYPE_DMA_BPB_TRANSFER;
                    pHdr->u32CmdSpecific = 0;
                    NEMUVDMACMD_DMA_BPB_TRANSFER *pBody = NEMUVDMACMD_BODY(pHdr, NEMUVDMACMD_DMA_BPB_TRANSFER);
//                    pBody->cbTransferSize = (uint32_t)pBuildPagingBuffer->Transfer.TransferSize;
                    pBody->fFlags = 0;
                    SIZE_T cSrcPages = (cbTransferSize + 0xfff ) >> 12;
                    SIZE_T cDstPages = cSrcPages;

                    if (pBuildPagingBuffer->Transfer.Source.SegmentId)
                    {
                        uint64_t off = pBuildPagingBuffer->Transfer.Source.SegmentAddress.QuadPart;
                        off += pBuildPagingBuffer->Transfer.TransferOffset + cbTransfered;
                        pBody->Src.offVramBuf = off;
                        pBody->fFlags |= NEMUVDMACMD_DMA_BPB_TRANSFER_F_SRC_VRAMOFFSET;
                    }
                    else
                    {
                        UINT index = pBuildPagingBuffer->Transfer.MdlOffset + (UINT)(cbTransfered>>12);
                        pBody->Src.phBuf = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Source.pMdl)[index] << PAGE_SHIFT;
                        PFN_NUMBER num = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Source.pMdl)[index];
                        cSrcPages = 1;
                        for (UINT i = 1; i < ((cbTransferSize - cbTransfered + 0xfff) >> 12); ++i)
                        {
                            PFN_NUMBER cur = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Source.pMdl)[index+i];
                            if(cur != ++num)
                            {
                                cSrcPages+= i-1;
                                break;
                            }
                        }
                    }

                    if (pBuildPagingBuffer->Transfer.Destination.SegmentId)
                    {
                        uint64_t off = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.QuadPart;
                        off += pBuildPagingBuffer->Transfer.TransferOffset;
                        pBody->Dst.offVramBuf = off + cbTransfered;
                        pBody->fFlags |= NEMUVDMACMD_DMA_BPB_TRANSFER_F_DST_VRAMOFFSET;
                    }
                    else
                    {
                        UINT index = pBuildPagingBuffer->Transfer.MdlOffset + (UINT)(cbTransfered>>12);
                        pBody->Dst.phBuf = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Destination.pMdl)[index] << PAGE_SHIFT;
                        PFN_NUMBER num = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Destination.pMdl)[index];
                        cDstPages = 1;
                        for (UINT i = 1; i < ((cbTransferSize - cbTransfered + 0xfff) >> 12); ++i)
                        {
                            PFN_NUMBER cur = MmGetMdlPfnArray(pBuildPagingBuffer->Transfer.Destination.pMdl)[index+i];
                            if(cur != ++num)
                            {
                                cDstPages+= i-1;
                                break;
                            }
                        }
                    }

                    SIZE_T cbCurTransfer;
                    cbCurTransfer = RT_MIN(cbTransferSize - cbTransfered, (SIZE_T)cSrcPages << PAGE_SHIFT);
                    cbCurTransfer = RT_MIN(cbCurTransfer, (SIZE_T)cDstPages << PAGE_SHIFT);

                    pBody->cbTransferSize = (UINT)cbCurTransfer;
                    Assert(!(cbCurTransfer & 0xfff));

                    int rc = nemuVdmaCBufDrSubmitSynch(pDevExt, &pDevExt->u.primary.Vdma, pDr);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        Status = STATUS_SUCCESS;
                        cbTransfered += cbCurTransfer;
                    }
                    else
                        Status = STATUS_UNSUCCESSFUL;
                } while (cbTransfered < cbTransferSize);
                Assert(cbTransfered == cbTransferSize);
                nemuVdmaCBufDrFree(&pDevExt->u.primary.Vdma, pDr);
            }
            else
            {
                /* @todo: try flushing.. */
                LOGREL(("nemuVdmaCBufDrCreate returned NULL"));
                Status = STATUS_INSUFFICIENT_RESOURCES;
            }
#endif
#endif /* #ifdef NEMU_WITH_VDMA */
            break;
        }
        case DXGK_OPERATION_FILL:
        {
            cbCmdDma = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
            Assert(pBuildPagingBuffer->Fill.FillPattern == 0);
            PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pBuildPagingBuffer->Fill.hAllocation;
//            pBuildPagingBuffer->pDmaBuffer = (uint8_t*)pBuildPagingBuffer->pDmaBuffer + NEMUVDMACMD_SIZE(NEMUVDMACMD_DMA_BPB_FILL);
            break;
        }
        case DXGK_OPERATION_DISCARD_CONTENT:
        {
//            AssertBreakpoint();
            break;
        }
        default:
        {
            WARN(("unsupported op (%d)", pBuildPagingBuffer->Operation));
            break;
        }
    }

    if (cbCmdDma)
    {
        pBuildPagingBuffer->pDmaBuffer = ((uint8_t*)pBuildPagingBuffer->pDmaBuffer) + cbCmdDma;
    }

    LOGF(("LEAVE, context(0x%x)", hAdapter));

    return Status;

}

NTSTATUS
APIENTRY
DxgkDdiSetPalette(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETPALETTE*  pSetPalette
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();
    /* @todo: fixme: implement */

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

BOOL nemuWddmPointerCopyColorData(CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, PVIDEO_POINTER_ATTRIBUTES pPointerAttributes)
{
    ULONG srcMaskW, srcMaskH;
    ULONG dstBytesPerLine;
    ULONG x, y;
    BYTE *pSrc, *pDst, bit;

    srcMaskW = pSetPointerShape->Width;
    srcMaskH = pSetPointerShape->Height;

    /* truncate masks if we exceed supported size */
    pPointerAttributes->Width = min(srcMaskW, NEMUWDDM_C_POINTER_MAX_WIDTH);
    pPointerAttributes->Height = min(srcMaskH, NEMUWDDM_C_POINTER_MAX_HEIGHT);
    pPointerAttributes->WidthInBytes = pPointerAttributes->Width * 4;

    /* cnstruct and mask from alpha color channel */
    pSrc = (PBYTE)pSetPointerShape->pPixels;
    pDst = pPointerAttributes->Pixels;
    dstBytesPerLine = (pPointerAttributes->Width+7)/8;

    /* sanity check */
    uint32_t cbData = RT_ALIGN_T(dstBytesPerLine*pPointerAttributes->Height, 4, ULONG)+
                      pPointerAttributes->Height*pPointerAttributes->WidthInBytes;
    uint32_t cbPointerAttributes = RT_OFFSETOF(VIDEO_POINTER_ATTRIBUTES, Pixels[cbData]);
    Assert(NEMUWDDM_POINTER_ATTRIBUTES_SIZE >= cbPointerAttributes);
    if (NEMUWDDM_POINTER_ATTRIBUTES_SIZE < cbPointerAttributes)
    {
        LOGREL(("NEMUWDDM_POINTER_ATTRIBUTES_SIZE(%d) < cbPointerAttributes(%d)", NEMUWDDM_POINTER_ATTRIBUTES_SIZE, cbPointerAttributes));
        return FALSE;
    }

    memset(pDst, 0xFF, dstBytesPerLine*pPointerAttributes->Height);
    for (y=0; y<pPointerAttributes->Height; ++y)
    {
        for (x=0, bit=7; x<pPointerAttributes->Width; ++x, --bit)
        {
            if (0xFF==bit) bit=7;

            if (pSrc[y*pSetPointerShape->Pitch + x*4 + 3] > 0x7F)
            {
                pDst[y*dstBytesPerLine + x/8] &= ~RT_BIT(bit);
            }
        }
    }

    /* copy 32bpp to XOR DIB, it start in pPointerAttributes->Pixels should be 4bytes aligned */
    pSrc = (BYTE*)pSetPointerShape->pPixels;
    pDst = pPointerAttributes->Pixels + RT_ALIGN_T(dstBytesPerLine*pPointerAttributes->Height, 4, ULONG);
    dstBytesPerLine = pPointerAttributes->Width * 4;

    for (y=0; y<pPointerAttributes->Height; ++y)
    {
        memcpy(pDst+y*dstBytesPerLine, pSrc+y*pSetPointerShape->Pitch, dstBytesPerLine);
    }

    return TRUE;
}

BOOL nemuWddmPointerCopyMonoData(CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, PVIDEO_POINTER_ATTRIBUTES pPointerAttributes)
{
    ULONG srcMaskW, srcMaskH;
    ULONG dstBytesPerLine;
    ULONG x, y;
    BYTE *pSrc, *pDst, bit;

    srcMaskW = pSetPointerShape->Width;
    srcMaskH = pSetPointerShape->Height;

    /* truncate masks if we exceed supported size */
    pPointerAttributes->Width = min(srcMaskW, NEMUWDDM_C_POINTER_MAX_WIDTH);
    pPointerAttributes->Height = min(srcMaskH, NEMUWDDM_C_POINTER_MAX_HEIGHT);
    pPointerAttributes->WidthInBytes = pPointerAttributes->Width * 4;

    /* copy AND mask */
    pSrc = (PBYTE)pSetPointerShape->pPixels;
    pDst = pPointerAttributes->Pixels;
    dstBytesPerLine = (pPointerAttributes->Width+7)/8;

    for (y=0; y<pPointerAttributes->Height; ++y)
    {
        memcpy(pDst+y*dstBytesPerLine, pSrc+y*pSetPointerShape->Pitch, dstBytesPerLine);
    }

    /* convert XOR mask to RGB0 DIB, it start in pPointerAttributes->Pixels should be 4bytes aligned */
    pSrc = (BYTE*)pSetPointerShape->pPixels + srcMaskH*pSetPointerShape->Pitch;
    pDst = pPointerAttributes->Pixels + RT_ALIGN_T(dstBytesPerLine*pPointerAttributes->Height, 4, ULONG);
    dstBytesPerLine = pPointerAttributes->Width * 4;

    for (y=0; y<pPointerAttributes->Height; ++y)
    {
        for (x=0, bit=7; x<pPointerAttributes->Width; ++x, --bit)
        {
            if (0xFF==bit) bit=7;

            *(ULONG*)&pDst[y*dstBytesPerLine+x*4] = (pSrc[y*pSetPointerShape->Pitch+x/8] & RT_BIT(bit)) ? 0x00FFFFFF : 0;
        }
    }

    return TRUE;
}

static BOOLEAN nemuVddmPointerShapeToAttributes(CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, PNEMUWDDM_POINTER_INFO pPointerInfo)
{
    PVIDEO_POINTER_ATTRIBUTES pPointerAttributes = &pPointerInfo->Attributes.data;
    /* pPointerAttributes maintains the visibility state, clear all except visibility */
    pPointerAttributes->Enable &= NEMU_MOUSE_POINTER_VISIBLE;

    Assert(pSetPointerShape->Flags.Value == 1 || pSetPointerShape->Flags.Value == 2);
    if (pSetPointerShape->Flags.Color)
    {
        if (nemuWddmPointerCopyColorData(pSetPointerShape, pPointerAttributes))
        {
            pPointerAttributes->Flags = VIDEO_MODE_COLOR_POINTER;
            pPointerAttributes->Enable |= NEMU_MOUSE_POINTER_ALPHA;
        }
        else
        {
            LOGREL(("nemuWddmPointerCopyColorData failed"));
            AssertBreakpoint();
            return FALSE;
        }

    }
    else if (pSetPointerShape->Flags.Monochrome)
    {
        if (nemuWddmPointerCopyMonoData(pSetPointerShape, pPointerAttributes))
        {
            pPointerAttributes->Flags = VIDEO_MODE_MONO_POINTER;
        }
        else
        {
            LOGREL(("nemuWddmPointerCopyMonoData failed"));
            AssertBreakpoint();
            return FALSE;
        }
    }
    else
    {
        LOGREL(("unsupported pointer type Flags.Value(0x%x)", pSetPointerShape->Flags.Value));
        AssertBreakpoint();
        return FALSE;
    }

    pPointerAttributes->Enable |= NEMU_MOUSE_POINTER_SHAPE;

    /*
     * The hot spot coordinates and alpha flag will be encoded in the pPointerAttributes::Enable field.
     * High word will contain hot spot info and low word - flags.
     */
    pPointerAttributes->Enable |= (pSetPointerShape->YHot & 0xFF) << 24;
    pPointerAttributes->Enable |= (pSetPointerShape->XHot & 0xFF) << 16;

    return TRUE;
}

static void nemuWddmHostPointerEnable(PNEMUMP_DEVEXT pDevExt, BOOLEAN fEnable)
{
    VIDEO_POINTER_ATTRIBUTES PointerAttributes;
    RT_ZERO(PointerAttributes);
    if (fEnable)
    {
        PointerAttributes.Enable = NEMU_MOUSE_POINTER_VISIBLE;
    }
    NemuMPCmnUpdatePointerShape(NemuCommonFromDeviceExt(pDevExt), &PointerAttributes, sizeof(PointerAttributes));
}

NTSTATUS
APIENTRY
DxgkDdiSetPointerPosition(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETPOINTERPOSITION*  pSetPointerPosition)
{
//    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    /* mouse integration is ON */
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    PNEMUWDDM_POINTER_INFO pPointerInfo = &pDevExt->aSources[pSetPointerPosition->VidPnSourceId].PointerInfo;
    PNEMUWDDM_GLOBAL_POINTER_INFO pGlobalPointerInfo = &pDevExt->PointerInfo;
    PVIDEO_POINTER_ATTRIBUTES pPointerAttributes = &pPointerInfo->Attributes.data;
    BOOLEAN fScreenVisState = !!(pPointerAttributes->Enable & NEMU_MOUSE_POINTER_VISIBLE);
    BOOLEAN fVisStateChanged = FALSE;
    BOOLEAN fScreenChanged = pGlobalPointerInfo->iLastReportedScreen != pSetPointerPosition->VidPnSourceId;

    if (pSetPointerPosition->Flags.Visible)
    {
        pPointerAttributes->Enable |= NEMU_MOUSE_POINTER_VISIBLE;
        if (!fScreenVisState)
        {
            fVisStateChanged = !!pGlobalPointerInfo->cVisible;
            ++pGlobalPointerInfo->cVisible;
        }
    }
    else
    {
        pPointerAttributes->Enable &= ~NEMU_MOUSE_POINTER_VISIBLE;
        if (fScreenVisState)
        {
            --pGlobalPointerInfo->cVisible;
            fVisStateChanged = !!pGlobalPointerInfo->cVisible;
        }
    }

    pGlobalPointerInfo->iLastReportedScreen = pSetPointerPosition->VidPnSourceId;

    if ((fVisStateChanged || fScreenChanged) && NemuQueryHostWantsAbsolute())
    {
        if (fScreenChanged)
        {
            BOOLEAN bResult = NemuMPCmnUpdatePointerShape(NemuCommonFromDeviceExt(pDevExt), &pPointerInfo->Attributes.data, NEMUWDDM_POINTER_ATTRIBUTES_SIZE);
            if (!bResult)
            {
                nemuWddmHostPointerEnable(pDevExt, FALSE);
            }
        }
        else
        {
            // tell the host to use the guest's pointer
            nemuWddmHostPointerEnable(pDevExt, pSetPointerPosition->Flags.Visible);
        }
    }

//    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiSetPointerShape(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETPOINTERSHAPE*  pSetPointerShape)
{
//    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    if (NemuQueryHostWantsAbsolute())
    {
        /* mouse integration is ON */
        PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
        PNEMUWDDM_POINTER_INFO pPointerInfo = &pDevExt->aSources[pSetPointerShape->VidPnSourceId].PointerInfo;
        /* @todo: to avoid extra data copy and extra heap allocation,
         *  need to maintain the pre-allocated HGSMI buffer and convert the data directly to it */
        if (nemuVddmPointerShapeToAttributes(pSetPointerShape, pPointerInfo))
        {
            pDevExt->PointerInfo.iLastReportedScreen = pSetPointerShape->VidPnSourceId;
            if (NemuMPCmnUpdatePointerShape(NemuCommonFromDeviceExt(pDevExt), &pPointerInfo->Attributes.data, NEMUWDDM_POINTER_ATTRIBUTES_SIZE))
                Status = STATUS_SUCCESS;
            else
            {
                // tell the host to use the guest's pointer
                nemuWddmHostPointerEnable(pDevExt, FALSE);
            }
        }
    }

//    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY CALLBACK
DxgkDdiResetFromTimeout(
    CONST HANDLE  hAdapter)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();
    /* @todo: fixme: implement */

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}


/* the lpRgnData->Buffer comes to us as RECT
 * to avoid extra memcpy we cast it to PRTRECT assuming
 * they are identical */
AssertCompile(sizeof(RECT) == sizeof(RTRECT));
AssertCompile(RT_OFFSETOF(RECT, left) == RT_OFFSETOF(RTRECT, xLeft));
AssertCompile(RT_OFFSETOF(RECT, bottom) == RT_OFFSETOF(RTRECT, yBottom));
AssertCompile(RT_OFFSETOF(RECT, right) == RT_OFFSETOF(RTRECT, xRight));
AssertCompile(RT_OFFSETOF(RECT, top) == RT_OFFSETOF(RTRECT, yTop));

NTSTATUS
APIENTRY
DxgkDdiEscape(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_ESCAPE*  pEscape)
{
    PAGED_CODE();

//    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    Assert(pEscape->PrivateDriverDataSize >= sizeof (NEMUDISPIFESCAPE));
    if (pEscape->PrivateDriverDataSize >= sizeof (NEMUDISPIFESCAPE))
    {
        PNEMUDISPIFESCAPE pEscapeHdr = (PNEMUDISPIFESCAPE)pEscape->pPrivateDriverData;
        switch (pEscapeHdr->escapeCode)
        {
#ifdef NEMU_WITH_CRHGSMI
            case NEMUESC_UHGSMI_SUBMIT:
            {
                if (pDevExt->fCmdVbvaEnabled)
                {
                    WARN(("NEMUESC_UHGSMI_SUBMIT not supported for CmdVbva mode"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
                /* submit NEMUUHGSMI command */
                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                PNEMUDISPIFESCAPE_UHGSMI_SUBMIT pSubmit = (PNEMUDISPIFESCAPE_UHGSMI_SUBMIT)pEscapeHdr;
                Assert(pEscape->PrivateDriverDataSize >= sizeof (NEMUDISPIFESCAPE_UHGSMI_SUBMIT)
                        && pEscape->PrivateDriverDataSize == RT_OFFSETOF(NEMUDISPIFESCAPE_UHGSMI_SUBMIT, aBuffers[pEscapeHdr->u32CmdSpecific]));
                if (pEscape->PrivateDriverDataSize >= sizeof (NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD)
                        && pEscape->PrivateDriverDataSize == RT_OFFSETOF(NEMUDISPIFESCAPE_UHGSMI_SUBMIT, aBuffers[pEscapeHdr->u32CmdSpecific]))
                {
                    Status = nemuVideoAMgrCtxAllocSubmit(pDevExt, &pContext->AllocContext, pEscapeHdr->u32CmdSpecific, pSubmit->aBuffers);
                    Assert(Status == STATUS_SUCCESS);
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;

                break;
            }

            case NEMUESC_UHGSMI_ALLOCATE:
            {
                /* allocate NEMUUHGSMI buffer */
                if (pDevExt->fCmdVbvaEnabled)
                {
                    WARN(("NEMUESC_UHGSMI_ALLOCATE not supported for CmdVbva mode"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                PNEMUDISPIFESCAPE_UHGSMI_ALLOCATE pAlocate = (PNEMUDISPIFESCAPE_UHGSMI_ALLOCATE)pEscapeHdr;
                Assert(pEscape->PrivateDriverDataSize == sizeof (NEMUDISPIFESCAPE_UHGSMI_ALLOCATE));
                if (pEscape->PrivateDriverDataSize == sizeof (NEMUDISPIFESCAPE_UHGSMI_ALLOCATE))
                {
                    Status = nemuVideoAMgrCtxAllocCreate(&pContext->AllocContext, &pAlocate->Alloc);
                    Assert(Status == STATUS_SUCCESS);
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;

                break;
            }

            case NEMUESC_UHGSMI_DEALLOCATE:
            {
                if (pDevExt->fCmdVbvaEnabled)
                {
                    WARN(("NEMUESC_UHGSMI_DEALLOCATE not supported for CmdVbva mode"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
                /* deallocate NEMUUHGSMI buffer */
                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                PNEMUDISPIFESCAPE_UHGSMI_DEALLOCATE pDealocate = (PNEMUDISPIFESCAPE_UHGSMI_DEALLOCATE)pEscapeHdr;
                Assert(pEscape->PrivateDriverDataSize == sizeof (NEMUDISPIFESCAPE_UHGSMI_DEALLOCATE));
                if (pEscape->PrivateDriverDataSize == sizeof (NEMUDISPIFESCAPE_UHGSMI_DEALLOCATE))
                {
                    Status = nemuVideoAMgrCtxAllocDestroy(&pContext->AllocContext, pDealocate->hAlloc);
                    Assert(Status == STATUS_SUCCESS);
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;

                break;
            }

            case NEMUESC_CRHGSMICTLCON_CALL:
            {
                if (pDevExt->fCmdVbvaEnabled)
                {
                    WARN(("NEMUESC_CRHGSMICTLCON_CALL not supported for CmdVbva mode"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                PNEMUDISPIFESCAPE_CRHGSMICTLCON_CALL pCall = (PNEMUDISPIFESCAPE_CRHGSMICTLCON_CALL)pEscapeHdr;
                if (pEscape->PrivateDriverDataSize >= sizeof (*pCall))
                {
                    /* this is true due to the above condition */
                    Assert(pEscape->PrivateDriverDataSize > RT_OFFSETOF(NEMUDISPIFESCAPE_CRHGSMICTLCON_CALL, CallInfo));
                    int rc = NemuMpCrCtlConCallUserData(&pDevExt->CrCtlCon, &pCall->CallInfo, pEscape->PrivateDriverDataSize - RT_OFFSETOF(NEMUDISPIFESCAPE_CRHGSMICTLCON_CALL, CallInfo));
                    pEscapeHdr->u32CmdSpecific = (uint32_t)rc;
                    Status = STATUS_SUCCESS; /* <- always return success here, otherwise the private data buffer modifications
                                              * i.e. rc status stored in u32CmdSpecific will not be copied to user mode */
                    if (!RT_SUCCESS(rc))
                        WARN(("NemuMpCrUmCtlConCall failed, rc(%d)", rc));
                }
                else
                {
                    WARN(("buffer too small!"));
                    Status = STATUS_BUFFER_TOO_SMALL;
                }

                break;
            }

            case NEMUESC_CRHGSMICTLCON_GETCLIENTID:
            {
                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                if (!pContext)
                {
                    WARN(("context not specified"));
                    return STATUS_INVALID_PARAMETER;
                }
                if (pEscape->PrivateDriverDataSize == sizeof (*pEscapeHdr))
                {
                    pEscapeHdr->u32CmdSpecific = pContext->u32CrConClientID;
                    Status = STATUS_SUCCESS;
                }
                else
                {
                    WARN(("unexpected buffer size!"));
                    Status = STATUS_INVALID_PARAMETER;
                }

                break;
            }

            case NEMUESC_CRHGSMICTLCON_GETHOSTCAPS:
            {
                if (pEscape->PrivateDriverDataSize == sizeof (*pEscapeHdr))
                {
                    pEscapeHdr->u32CmdSpecific = NemuMpCrGetHostCaps();
                    Status = STATUS_SUCCESS;
                }
                else
                {
                    WARN(("unexpected buffer size!"));
                    Status = STATUS_INVALID_PARAMETER;
                }

                break;
            }
#endif

            case NEMUESC_SETVISIBLEREGION:
            {
                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
#ifdef NEMU_DISPIF_WITH_OPCONTEXT
                if (!pContext)
                {
                    WARN(("NEMUESC_SETVISIBLEREGION no context supplied!"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pContext->enmType != NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS)
                {
                    WARN(("NEMUESC_SETVISIBLEREGION invalid context supplied %d!", pContext->enmType));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
#endif
                /* visible regions for seamless */
                LPRGNDATA lpRgnData = NEMUDISPIFESCAPE_DATA(pEscapeHdr, RGNDATA);
                uint32_t cbData = NEMUDISPIFESCAPE_DATA_SIZE(pEscape->PrivateDriverDataSize);
                uint32_t cbRects = cbData - RT_OFFSETOF(RGNDATA, Buffer);
                /* the lpRgnData->Buffer comes to us as RECT
                 * to avoid extra memcpy we cast it to PRTRECT assuming
                 * they are identical
                 * see AssertCompile's above */

                RTRECT   *pRect = (RTRECT *)&lpRgnData->Buffer;

                uint32_t cRects = cbRects/sizeof(RTRECT);
                int      rc;

                LOG(("IOCTL_VIDEO_NEMU_SETVISIBLEREGION cRects=%d", cRects));
                Assert(cbRects >= sizeof(RTRECT)
                    &&  cbRects == cRects*sizeof(RTRECT)
                    &&  cRects == lpRgnData->rdh.nCount);
                if (    cbRects >= sizeof(RTRECT)
                    &&  cbRects == cRects*sizeof(RTRECT)
                    &&  cRects == lpRgnData->rdh.nCount)
                {
                    /*
                     * Inform the host about the visible region
                     */
                    VMMDevVideoSetVisibleRegion *req = NULL;

                    rc = VbglGRAlloc ((VMMDevRequestHeader **)&req,
                                      sizeof (VMMDevVideoSetVisibleRegion) + (cRects-1)*sizeof(RTRECT),
                                      VMMDevReq_VideoSetVisibleRegion);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        req->cRect = cRects;
                        memcpy(&req->Rect, pRect, cRects*sizeof(RTRECT));

                        rc = VbglGRPerform (&req->header);
                        AssertRC(rc);
                        if (RT_SUCCESS(rc))
                            Status = STATUS_SUCCESS;
                        else
                        {
                            WARN(("VbglGRPerform failed rc (%d)", rc));
                            Status = STATUS_UNSUCCESSFUL;
                        }
                    }
                    else
                    {
                        WARN(("VbglGRAlloc failed rc (%d)", rc));
                        Status = STATUS_UNSUCCESSFUL;
                    }
                }
                else
                {
                    WARN(("NEMUESC_SETVISIBLEREGION: incorrect buffer size (%d), reported count (%d)", cbRects, lpRgnData->rdh.nCount));
                    Status = STATUS_INVALID_PARAMETER;
                }
                break;
            }
            case NEMUESC_ISVRDPACTIVE:
                /* @todo: implement */
                Status = STATUS_SUCCESS;
                break;
#ifdef NEMU_WITH_CROGL
            case NEMUESC_SETCTXHOSTID:
            {
                /* set swapchain information */
                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                if (!pContext)
                {
                    WARN(("NEMUESC_SETCTXHOSTID: no context specified"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pEscape->PrivateDriverDataSize != sizeof (NEMUDISPIFESCAPE))
                {
                    WARN(("NEMUESC_SETCTXHOSTID: invalid data size %d", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                int32_t hostID = (int32_t)pEscapeHdr->u32CmdSpecific;
                if (hostID <= 0)
                {
                    WARN(("NEMUESC_SETCTXHOSTID: invalid hostID %d", hostID));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pContext->hostID)
                {
                    WARN(("NEMUESC_SETCTXHOSTID: context already has hostID specified"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                pContext->hostID = hostID;
                Status = STATUS_SUCCESS;
                break;
            }
            case NEMUESC_SWAPCHAININFO:
            {
                /* set swapchain information */
                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                Status = nemuWddmSwapchainCtxEscape(pDevExt, pContext, (PNEMUDISPIFESCAPE_SWAPCHAININFO)pEscapeHdr, pEscape->PrivateDriverDataSize);
                Assert(Status == STATUS_SUCCESS);
                break;
            }
#endif
            case NEMUESC_CONFIGURETARGETS:
            {
                LOG(("=> NEMUESC_CONFIGURETARGETS"));

                if (!pEscape->Flags.HardwareAccess)
                {
                    WARN(("NEMUESC_CONFIGURETARGETS called without HardwareAccess flag set, failing"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

#ifdef NEMU_DISPIF_WITH_OPCONTEXT
                /* win8.1 does not allow context-based escapes for display-only mode */
                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                if (!pContext)
                {
                    WARN(("NEMUESC_CONFIGURETARGETS no context supplied!"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pContext->enmType != NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE)
                {
                    WARN(("NEMUESC_CONFIGURETARGETS invalid context supplied %d!", pContext->enmType));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
#endif

                if (pEscape->PrivateDriverDataSize != sizeof (*pEscapeHdr))
                {
                    WARN(("NEMUESC_CONFIGURETARGETS invalid private driver size %d", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pEscapeHdr->u32CmdSpecific)
                {
                    WARN(("NEMUESC_CONFIGURETARGETS invalid command %d", pEscapeHdr->u32CmdSpecific));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                HANDLE hKey = NULL;
                WCHAR aNameBuf[100];
                uint32_t cAdjusted = 0;

                for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
                {
                    NEMUWDDM_TARGET *pTarget = &pDevExt->aTargets[i];
                    if (pTarget->fConfigured)
                        continue;

                    pTarget->fConfigured = true;

                    if (!pTarget->fConnected)
                    {
                        Status = NemuWddmChildStatusConnect(pDevExt, (uint32_t)i, TRUE);
                        if (NT_SUCCESS(Status))
                            ++cAdjusted;
                        else
                            WARN(("NEMUESC_CONFIGURETARGETS nemuWddmChildStatusConnectSecondaries failed Status 0x%x\n", Status));
                    }

                    if (!hKey)
                    {
                        Status = IoOpenDeviceRegistryKey(pDevExt->pPDO, PLUGPLAY_REGKEY_DRIVER, GENERIC_WRITE, &hKey);
                        if (!NT_SUCCESS(Status))
                        {
                            WARN(("NEMUESC_CONFIGURETARGETS IoOpenDeviceRegistryKey failed, Status = 0x%x", Status));
                            hKey = NULL;
                            continue;
                        }
                    }

                    Assert(hKey);

                    swprintf(aNameBuf, L"%s%d", NEMUWDDM_REG_DRV_DISPFLAGS_PREFIX, i);
                    Status = nemuWddmRegSetValueDword(hKey, aNameBuf, NEMUWDDM_CFG_DRVTARGET_CONNECTED);
                    if (!NT_SUCCESS(Status))
                        WARN(("NEMUESC_CONFIGURETARGETS nemuWddmRegSetValueDword (%d) failed Status 0x%x\n", aNameBuf, Status));

                }

                if (hKey)
                {
                    NTSTATUS tmpStatus = ZwClose(hKey);
                    Assert(tmpStatus == STATUS_SUCCESS);
                }

                pEscapeHdr->u32CmdSpecific = cAdjusted;

                Status = STATUS_SUCCESS;

                LOG(("<= NEMUESC_CONFIGURETARGETS"));
                break;
            }
            case NEMUESC_SETALLOCHOSTID:
            {
                PNEMUWDDM_DEVICE pDevice = (PNEMUWDDM_DEVICE)pEscape->hDevice;
                if (!pDevice)
                {
                    WARN(("NEMUESC_SETALLOCHOSTID called without no device specified, failing"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pEscape->PrivateDriverDataSize != sizeof (NEMUDISPIFESCAPE_SETALLOCHOSTID))
                {
                    WARN(("invalid buffer size for NEMUDISPIFESCAPE_SHRC_REF, was(%d), but expected (%d)",
                            pEscape->PrivateDriverDataSize, sizeof (NEMUDISPIFESCAPE_SHRC_REF)));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (!pEscape->Flags.HardwareAccess)
                {
                    WARN(("NEMUESC_SETALLOCHOSTID not HardwareAccess"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PNEMUDISPIFESCAPE_SETALLOCHOSTID pSetHostID = (PNEMUDISPIFESCAPE_SETALLOCHOSTID)pEscapeHdr;
                PNEMUWDDM_ALLOCATION pAlloc = nemuWddmGetAllocationFromHandle(pDevExt, (D3DKMT_HANDLE)pSetHostID->hAlloc);
                if (!pAlloc)
                {
                    WARN(("failed to get allocation from handle"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pAlloc->enmType != NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE)
                {
                    WARN(("setHostID: invalid allocation type: %d", pAlloc->enmType));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                pSetHostID->rc = NemuWddmOaSetHostID(pDevice, pAlloc, pSetHostID->hostID, &pSetHostID->EscapeHdr.u32CmdSpecific);

                if (pAlloc->bAssigned)
                {
                    PNEMUMP_DEVEXT pDevExt = pDevice->pAdapter;
                    Assert(pAlloc->AllocData.SurfDesc.VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
                    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pAlloc->AllocData.SurfDesc.VidPnSourceId];
                    if (pSource->AllocData.hostID != pAlloc->AllocData.hostID)
                    {
                        pSource->AllocData.hostID = pAlloc->AllocData.hostID;
                        pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_LOCATION;

                        nemuWddmGhDisplayCheckSetInfo(pDevExt);
                    }
                }

                Status = STATUS_SUCCESS;
                break;
            }
            case NEMUESC_SHRC_ADDREF:
            case NEMUESC_SHRC_RELEASE:
            {
                PNEMUWDDM_DEVICE pDevice = (PNEMUWDDM_DEVICE)pEscape->hDevice;
                if (!pDevice)
                {
                    WARN(("NEMUESC_SHRC_ADDREF|NEMUESC_SHRC_RELEASE called without no device specified, failing"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                /* query whether the allocation represanted by the given [wine-generated] shared resource handle still exists */
                if (pEscape->PrivateDriverDataSize != sizeof (NEMUDISPIFESCAPE_SHRC_REF))
                {
                    WARN(("invalid buffer size for NEMUDISPIFESCAPE_SHRC_REF, was(%d), but expected (%d)",
                            pEscape->PrivateDriverDataSize, sizeof (NEMUDISPIFESCAPE_SHRC_REF)));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PNEMUDISPIFESCAPE_SHRC_REF pShRcRef = (PNEMUDISPIFESCAPE_SHRC_REF)pEscapeHdr;
                PNEMUWDDM_ALLOCATION pAlloc = nemuWddmGetAllocationFromHandle(pDevExt, (D3DKMT_HANDLE)pShRcRef->hAlloc);
                if (!pAlloc)
                {
                    WARN(("failed to get allocation from handle"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PNEMUWDDM_OPENALLOCATION pOa = NemuWddmOaSearch(pDevice, pAlloc);
                if (!pOa)
                {
                    WARN(("failed to get open allocation from alloc"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                Assert(pAlloc->cShRcRefs >= pOa->cShRcRefs);

                if (pEscapeHdr->escapeCode == NEMUESC_SHRC_ADDREF)
                {
#ifdef DEBUG
                    Assert(!pAlloc->fAssumedDeletion);
#endif
                    ++pAlloc->cShRcRefs;
                    ++pOa->cShRcRefs;
                }
                else
                {
                    Assert(pAlloc->cShRcRefs);
                    Assert(pOa->cShRcRefs);
                    --pAlloc->cShRcRefs;
                    --pOa->cShRcRefs;
#ifdef DEBUG
                    Assert(!pAlloc->fAssumedDeletion);
                    if (!pAlloc->cShRcRefs)
                    {
                        pAlloc->fAssumedDeletion = TRUE;
                    }
#endif
                }

                pShRcRef->EscapeHdr.u32CmdSpecific = pAlloc->cShRcRefs;
                Status = STATUS_SUCCESS;
                break;
            }
            case NEMUESC_ISANYX:
            {
                if (pEscape->PrivateDriverDataSize != sizeof (NEMUDISPIFESCAPE_ISANYX))
                {
                    WARN(("invalid private driver size %d", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                PNEMUDISPIFESCAPE_ISANYX pIsAnyX = (PNEMUDISPIFESCAPE_ISANYX)pEscapeHdr;
                pIsAnyX->u32IsAnyX = NemuCommonFromDeviceExt(pDevExt)->fAnyX;
                Status = STATUS_SUCCESS;
                break;
            }
            case NEMUESC_UPDATEMODES:
            {
                LOG(("=> NEMUESC_UPDATEMODES"));

                if (!pEscape->Flags.HardwareAccess)
                {
                    WARN(("NEMUESC_UPDATEMODES called without HardwareAccess flag set, failing"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

#ifdef NEMU_DISPIF_WITH_OPCONTEXT
                /* win8.1 does not allow context-based escapes for display-only mode */
                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)pEscape->hContext;
                if (!pContext)
                {
                    WARN(("NEMUESC_UPDATEMODES no context supplied!"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (pContext->enmType != NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE)
                {
                    WARN(("NEMUESC_UPDATEMODES invalid context supplied %d!", pContext->enmType));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
#endif

                if (pEscape->PrivateDriverDataSize != sizeof (NEMUDISPIFESCAPE_UPDATEMODES))
                {
                    WARN(("NEMUESC_UPDATEMODES invalid private driver size %d", pEscape->PrivateDriverDataSize));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                NEMUDISPIFESCAPE_UPDATEMODES *pData = (NEMUDISPIFESCAPE_UPDATEMODES*)pEscapeHdr;
                Status = NemuVidPnUpdateModes(pDevExt, pData->u32TargetId, &pData->Size);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("NemuVidPnUpdateModes failed Status(%#x)\n", Status));
                    return Status;
                }

                break;
            }
            case NEMUESC_DBGPRINT:
            {
                /* use RT_OFFSETOF instead of sizeof since sizeof will give an aligned size that might
                 * be bigger than the NEMUDISPIFESCAPE_DBGPRINT with a data containing just a few chars */
                Assert(pEscape->PrivateDriverDataSize >= RT_OFFSETOF(NEMUDISPIFESCAPE_DBGPRINT, aStringBuf[1]));
                /* only do DbgPrint when pEscape->PrivateDriverDataSize > RT_OFFSETOF(NEMUDISPIFESCAPE_DBGPRINT, aStringBuf[1])
                 * since == RT_OFFSETOF(NEMUDISPIFESCAPE_DBGPRINT, aStringBuf[1]) means the buffer contains just \0,
                 * i.e. no need to print it */
                if (pEscape->PrivateDriverDataSize > RT_OFFSETOF(NEMUDISPIFESCAPE_DBGPRINT, aStringBuf[1]))
                {
                    PNEMUDISPIFESCAPE_DBGPRINT pDbgPrint = (PNEMUDISPIFESCAPE_DBGPRINT)pEscapeHdr;
                    /* ensure the last char is \0*/
                    *((uint8_t*)pDbgPrint + pEscape->PrivateDriverDataSize - 1) = '\0';
                    if (g_NemuLogUm & NEMUWDDM_CFG_LOG_UM_DBGPRINT)
                        DbgPrint("%s\n", pDbgPrint->aStringBuf);
                    if (g_NemuLogUm & NEMUWDDM_CFG_LOG_UM_BACKDOOR)
                        LOGREL_EXACT(("%s\n", pDbgPrint->aStringBuf));
                }
                Status = STATUS_SUCCESS;
                break;
            }
            case NEMUESC_DBGDUMPBUF:
            {
                Status = nemuUmdDumpBuf((PNEMUDISPIFESCAPE_DBGDUMPBUF)pEscapeHdr, pEscape->PrivateDriverDataSize);
                break;
            }
            default:
                WARN(("unsupported escape code (0x%x)", pEscapeHdr->escapeCode));
                break;
        }
    }
    else
    {
        WARN(("pEscape->PrivateDriverDataSize(%d) < (%d)", pEscape->PrivateDriverDataSize, sizeof (NEMUDISPIFESCAPE)));
        Status = STATUS_BUFFER_TOO_SMALL;
    }

//    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiCollectDbgInfo(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_COLLECTDBGINFO*  pCollectDbgInfo
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

typedef struct NEMUWDDM_QUERYCURFENCE_CB
{
    PNEMUMP_DEVEXT pDevExt;
    ULONG MessageNumber;
    ULONG uLastCompletedCmdFenceId;
} NEMUWDDM_QUERYCURFENCE_CB, *PNEMUWDDM_QUERYCURFENCE_CB;

static BOOLEAN nemuWddmQueryCurrentFenceCb(PVOID Context)
{
    PNEMUWDDM_QUERYCURFENCE_CB pdc = (PNEMUWDDM_QUERYCURFENCE_CB)Context;
    PNEMUMP_DEVEXT pDevExt = pdc->pDevExt;
    BOOL bRc = DxgkDdiInterruptRoutineLegacy(pDevExt, pdc->MessageNumber);
    pdc->uLastCompletedCmdFenceId = pDevExt->u.primary.Vdma.uLastCompletedPagingBufferCmdFenceId;
    return bRc;
}

#ifdef NEMU_WITH_CROGL
static NTSTATUS
APIENTRY
DxgkDdiQueryCurrentFenceNew(
    CONST HANDLE  hAdapter,
    DXGKARG_QUERYCURRENTFENCE*  pCurrentFence)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    nemuVDbgBreakF();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    WARN(("=>DxgkDdiQueryCurrentFenceNew"));

    uint32_t u32FenceSubmitted = 0;
    uint32_t u32FenceCompleted = 0;
    uint32_t u32FenceProcessed = 0;

    LARGE_INTEGER DelayInterval;
    DelayInterval.QuadPart = -10LL * 1000LL * 1000LL;

    for (;;)
    {
        u32FenceCompleted = NemuCmdVbvaCheckCompleted(pDevExt, &pDevExt->CmdVbva, false, &u32FenceSubmitted, &u32FenceProcessed);
        if (!u32FenceCompleted)
        {
            WARN(("NemuCmdVbvaCheckCompleted failed"));
            return STATUS_UNSUCCESSFUL;
        }

        if (u32FenceSubmitted == u32FenceProcessed)
            break;

        WARN(("uncompleted fences, u32FenceSubmitted(%d), u32FenceCompleted(%d) u32FenceProcessed(%d)", u32FenceSubmitted, u32FenceCompleted, u32FenceProcessed));

        NTSTATUS Status = KeDelayExecutionThread(KernelMode, FALSE, &DelayInterval);
        if (Status != STATUS_SUCCESS)
            WARN(("KeDelayExecutionThread failed %#x", Status));
    }

    pCurrentFence->CurrentFence = u32FenceCompleted;

    WARN(("<=DxgkDdiQueryCurrentFenceNew"));

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}
#endif

static NTSTATUS
APIENTRY
DxgkDdiQueryCurrentFenceLegacy(
    CONST HANDLE  hAdapter,
    DXGKARG_QUERYCURRENTFENCE*  pCurrentFence)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    nemuVDbgBreakF();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    NEMUWDDM_QUERYCURFENCE_CB context = {0};
    context.pDevExt = pDevExt;
    BOOLEAN bRet;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            nemuWddmQueryCurrentFenceCb,
            &context,
            0, /* IN ULONG MessageNumber */
            &bRet);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        pCurrentFence->CurrentFence = context.uLastCompletedCmdFenceId;
    }

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiIsSupportedVidPn(
    CONST HANDLE  hAdapter,
    OUT DXGKARG_ISSUPPORTEDVIDPN*  pIsSupportedVidPnArg
    )
{
    /* The DxgkDdiIsSupportedVidPn should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    NTSTATUS Status = NemuVidPnIsSupported(pDevExt, pIsSupportedVidPnArg->hDesiredVidPn, &pIsSupportedVidPnArg->IsVidPnSupported);
    if (!NT_SUCCESS(Status))
    {
        WARN(("NemuVidPnIsSupported failed Status(%#x)\n", Status));
        return Status;
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiRecommendFunctionalVidPn(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST  pRecommendFunctionalVidPnArg
    )
{
    /* The DxgkDdiRecommendFunctionalVidPn should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    if (pRecommendFunctionalVidPnArg->PrivateDriverDataSize != sizeof (NEMUWDDM_RECOMMENDVIDPN))
    {
        WARN(("invalid size"));
        return STATUS_INVALID_PARAMETER;
    }

    NEMUWDDM_RECOMMENDVIDPN *pData = (NEMUWDDM_RECOMMENDVIDPN*)pRecommendFunctionalVidPnArg->pPrivateDriverData;
    Assert(pData);

    NTSTATUS Status = NemuVidPnRecommendFunctional(pDevExt, pRecommendFunctionalVidPnArg->hRecommendedFunctionalVidPn, pData);
    if (!NT_SUCCESS(Status))
    {
        WARN(("NemuVidPnRecommendFunctional failed %#x", Status));
        return Status;
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiEnumVidPnCofuncModality(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModalityArg
    )
{
    /* The DxgkDdiEnumVidPnCofuncModality function should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    NTSTATUS Status = NemuVidPnCofuncModality(pDevExt, pEnumCofuncModalityArg->hConstrainingVidPn, pEnumCofuncModalityArg->EnumPivotType, &pEnumCofuncModalityArg->EnumPivot);
    if (!NT_SUCCESS(Status))
    {
        WARN(("NemuVidPnCofuncModality failed Status(%#x)\n", Status));
        return Status;
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiSetVidPnSourceAddress(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETVIDPNSOURCEADDRESS*  pSetVidPnSourceAddress
    )
{
    /* The DxgkDdiSetVidPnSourceAddress function should be made pageable. */
    PAGED_CODE();

    nemuVDbgBreakFv();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    if ((UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays <= pSetVidPnSourceAddress->VidPnSourceId)
    {
        WARN(("invalid VidPnSourceId (%d), for displays(%d)", pSetVidPnSourceAddress->VidPnSourceId, NemuCommonFromDeviceExt(pDevExt)->cDisplays));
        return STATUS_INVALID_PARAMETER;
    }

    nemuWddmDisplaySettingsCheckPos(pDevExt, pSetVidPnSourceAddress->VidPnSourceId);

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pSetVidPnSourceAddress->VidPnSourceId];
    PNEMUWDDM_ALLOCATION pAllocation;
    Assert(pSetVidPnSourceAddress->hAllocation);
    Assert(pSetVidPnSourceAddress->hAllocation || pSource->pPrimaryAllocation);
    Assert (pSetVidPnSourceAddress->Flags.Value < 2); /* i.e. 0 or 1 (ModeChange) */

    if (pSetVidPnSourceAddress->hAllocation)
    {
        pAllocation = (PNEMUWDDM_ALLOCATION)pSetVidPnSourceAddress->hAllocation;
        nemuWddmAssignPrimary(pSource, pAllocation, pSetVidPnSourceAddress->VidPnSourceId);
    }
    else
        pAllocation = pSource->pPrimaryAllocation;

    if (pAllocation)
    {
        nemuWddmAddrSetVram(&pAllocation->AllocData.Addr, pSetVidPnSourceAddress->PrimarySegment, (NEMUVIDEOOFFSET)pSetVidPnSourceAddress->PrimaryAddress.QuadPart);
    }

#ifdef NEMU_WDDM_WIN8
    if (g_NemuDisplayOnly && !pAllocation)
    {
        /* the VRAM here is an absolute address, nto an offset!
         * convert to offset since all internal Nemu functionality is offset-based */
        nemuWddmAddrSetVram(&pSource->AllocData.Addr, pSetVidPnSourceAddress->PrimarySegment,
                nemuWddmVramAddrToOffset(pDevExt, pSetVidPnSourceAddress->PrimaryAddress));
    }
    else
#endif
    {
#ifdef NEMU_WDDM_WIN8
        Assert(!g_NemuDisplayOnly);
#endif
        nemuWddmAddrSetVram(&pSource->AllocData.Addr, pSetVidPnSourceAddress->PrimarySegment,
                                                    pSetVidPnSourceAddress->PrimaryAddress.QuadPart);
    }

    pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_LOCATION;

    nemuWddmGhDisplayCheckSetInfoFromSource(pDevExt, pSource);

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiSetVidPnSourceVisibility(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility
    )
{
    /* DxgkDdiSetVidPnSourceVisibility should be made pageable. */
    PAGED_CODE();

    nemuVDbgBreakFv();

    LOGF(("ENTER, context(0x%x)", hAdapter));

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    if ((UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays <= pSetVidPnSourceVisibility->VidPnSourceId)
    {
        WARN(("invalid VidPnSourceId (%d), for displays(%d)", pSetVidPnSourceVisibility->VidPnSourceId, NemuCommonFromDeviceExt(pDevExt)->cDisplays));
        return STATUS_INVALID_PARAMETER;
    }

    nemuWddmDisplaySettingsCheckPos(pDevExt, pSetVidPnSourceVisibility->VidPnSourceId);

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pSetVidPnSourceVisibility->VidPnSourceId];
    PNEMUWDDM_ALLOCATION pAllocation = pSource->pPrimaryAllocation;
    if (pAllocation)
    {
        Assert(pAllocation->bVisible == pSource->bVisible);
        pAllocation->bVisible = pSetVidPnSourceVisibility->Visible;
    }

    if (pSource->bVisible != pSetVidPnSourceVisibility->Visible)
    {
        pSource->bVisible = pSetVidPnSourceVisibility->Visible;
//        pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_VISIBILITY;
//        if (pDevExt->fCmdVbvaEnabled || pSource->bVisible)
//        {
//            nemuWddmGhDisplayCheckSetInfoFromSource(pDevExt, pSource);
//        }
    }

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiCommitVidPn(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_COMMITVIDPN* CONST  pCommitVidPnArg
    )
{
    LOGF(("ENTER, context(0x%x)", hAdapter));

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    NTSTATUS Status;

    nemuVDbgBreakFv();

    NEMUWDDM_SOURCE *paSources = (NEMUWDDM_SOURCE*)RTMemAlloc(sizeof (NEMUWDDM_SOURCE) * NemuCommonFromDeviceExt(pDevExt)->cDisplays);
    if (!paSources)
    {
        WARN(("RTMemAlloc failed"));
        return STATUS_NO_MEMORY;
    }

    NEMUWDDM_TARGET *paTargets = (NEMUWDDM_TARGET*)RTMemAlloc(sizeof (NEMUWDDM_TARGET) * NemuCommonFromDeviceExt(pDevExt)->cDisplays);
    if (!paTargets)
    {
        WARN(("RTMemAlloc failed"));
        RTMemFree(paSources);
        return STATUS_NO_MEMORY;
    }

    NemuVidPnSourcesInit(paSources, NemuCommonFromDeviceExt(pDevExt)->cDisplays, NEMUWDDM_HGSYNC_F_SYNCED_ALL);

    NemuVidPnTargetsInit(paTargets, NemuCommonFromDeviceExt(pDevExt)->cDisplays, NEMUWDDM_HGSYNC_F_SYNCED_ALL);

    NemuVidPnSourcesCopy(paSources, pDevExt->aSources, NemuCommonFromDeviceExt(pDevExt)->cDisplays);
    NemuVidPnTargetsCopy(paTargets, pDevExt->aTargets, NemuCommonFromDeviceExt(pDevExt)->cDisplays);

    do {
        const DXGK_VIDPN_INTERFACE* pVidPnInterface = NULL;
        Status = pDevExt->u.primary.DxgkInterface.DxgkCbQueryVidPnInterface(pCommitVidPnArg->hFunctionalVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &pVidPnInterface);
        if (!NT_SUCCESS(Status))
        {
            WARN(("DxgkCbQueryVidPnInterface failed Status 0x%x", Status));
            break;
        }

#ifdef NEMUWDDM_DEBUG_VIDPN
        nemuVidPnDumpVidPn("\n>>>>COMMIT VidPN: >>>>", pDevExt, pCommitVidPnArg->hFunctionalVidPn, pVidPnInterface, "<<<<<<<<<<<<<<<<<<<<\n");
#endif

        if (pCommitVidPnArg->AffectedVidPnSourceId != D3DDDI_ID_ALL)
        {
            Status = NemuVidPnCommitSourceModeForSrcId(
                    pDevExt,
                    pCommitVidPnArg->hFunctionalVidPn, pVidPnInterface,
                    (PNEMUWDDM_ALLOCATION)pCommitVidPnArg->hPrimaryAllocation,
                    pCommitVidPnArg->AffectedVidPnSourceId, paSources, paTargets);
            if (!NT_SUCCESS(Status))
            {
                WARN(("NemuVidPnCommitSourceModeForSrcId for current VidPn failed Status 0x%x", Status));
                break;
            }
        }
        else
        {
            Status = NemuVidPnCommitAll(pDevExt, pCommitVidPnArg->hFunctionalVidPn, pVidPnInterface,
                    (PNEMUWDDM_ALLOCATION)pCommitVidPnArg->hPrimaryAllocation,
                    paSources, paTargets);
            if (!NT_SUCCESS(Status))
            {
                WARN(("NemuVidPnCommitAll for current VidPn failed Status 0x%x", Status));
                break;
            }
        }

        Assert(NT_SUCCESS(Status));
        pDevExt->u.primary.hCommittedVidPn = pCommitVidPnArg->hFunctionalVidPn;
        NemuVidPnSourcesCopy(pDevExt->aSources, paSources, NemuCommonFromDeviceExt(pDevExt)->cDisplays);
        NemuVidPnTargetsCopy(pDevExt->aTargets, paTargets, NemuCommonFromDeviceExt(pDevExt)->cDisplays);

        nemuWddmGhDisplayCheckSetInfo(pDevExt);
    } while (0);

    RTMemFree(paSources);
    RTMemFree(paTargets);

    LOGF(("LEAVE, status(0x%x), context(0x%x)", Status, hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiUpdateActiveVidPnPresentPath(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST  pUpdateActiveVidPnPresentPathArg
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiRecommendMonitorModes(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModesArg
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    NTSTATUS Status = NemuVidPnRecommendMonitorModes(pDevExt, pRecommendMonitorModesArg->VideoPresentTargetId,
            pRecommendMonitorModesArg->hMonitorSourceModeSet, pRecommendMonitorModesArg->pMonitorSourceModeSetInterface);
    if (!NT_SUCCESS(Status))
    {
        WARN(("NemuVidPnRecommendMonitorModes failed %#x", Status));
        return Status;
    }

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiRecommendVidPnTopology(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopologyArg
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    nemuVDbgBreakFv();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY;
}

NTSTATUS
APIENTRY
DxgkDdiGetScanLine(
    CONST HANDLE  hAdapter,
    DXGKARG_GETSCANLINE*  pGetScanLine)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

#ifdef DEBUG_misha
//    RT_BREAKPOINT();
#endif

    NTSTATUS Status = NemuWddmSlGetScanLine(pDevExt, pGetScanLine);

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiStopCapture(
    CONST HANDLE  hAdapter,
    CONST DXGKARG_STOPCAPTURE*  pStopCapture)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    AssertBreakpoint();

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkDdiControlInterrupt(
    CONST HANDLE hAdapter,
    CONST DXGK_INTERRUPT_TYPE InterruptType,
    BOOLEAN Enable
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));

    NTSTATUS Status = STATUS_NOT_IMPLEMENTED;
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;

    switch (InterruptType)
    {
#ifdef NEMU_WDDM_WIN8
        case DXGK_INTERRUPT_DISPLAYONLY_VSYNC:
#endif
        case DXGK_INTERRUPT_CRTC_VSYNC:
        {
            Status = NemuWddmSlEnableVSyncNotification(pDevExt, Enable);
            if (NT_SUCCESS(Status))
                Status = STATUS_SUCCESS; /* <- sanity */
            else
                WARN(("VSYNC Interrupt control failed Enable(%d), Status(0x%x)", Enable, Status));
            break;
        }
        case DXGK_INTERRUPT_DMA_COMPLETED:
        case DXGK_INTERRUPT_DMA_PREEMPTED:
        case DXGK_INTERRUPT_DMA_FAULTED:
            WARN(("Unexpected interrupt type! %d", InterruptType));
            break;
        default:
            WARN(("UNSUPPORTED interrupt type! %d", InterruptType));
            break;
    }

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiCreateOverlay(
    CONST HANDLE  hAdapter,
    DXGKARG_CREATEOVERLAY  *pCreateOverlay)
{
    LOGF(("ENTER, hAdapter(0x%p)", hAdapter));

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    PNEMUWDDM_OVERLAY pOverlay = (PNEMUWDDM_OVERLAY)nemuWddmMemAllocZero(sizeof (NEMUWDDM_OVERLAY));
    Assert(pOverlay);
    if (pOverlay)
    {
        int rc = nemuVhwaHlpOverlayCreate(pDevExt, pCreateOverlay->VidPnSourceId, &pCreateOverlay->OverlayInfo, pOverlay);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            pCreateOverlay->hOverlay = pOverlay;
        }
        else
        {
            nemuWddmMemFree(pOverlay);
            Status = STATUS_UNSUCCESSFUL;
        }
    }
    else
        Status = STATUS_NO_MEMORY;

    LOGF(("LEAVE, hAdapter(0x%p)", hAdapter));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiDestroyDevice(
    CONST HANDLE  hDevice)
{
    /* DxgkDdiDestroyDevice should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, hDevice(0x%x)", hDevice));

    nemuVDbgBreakFv();

    nemuWddmMemFree(hDevice);

    LOGF(("LEAVE, "));

    return STATUS_SUCCESS;
}



/*
 * DxgkDdiOpenAllocation
 */
NTSTATUS
APIENTRY
DxgkDdiOpenAllocation(
    CONST HANDLE  hDevice,
    CONST DXGKARG_OPENALLOCATION  *pOpenAllocation)
{
    /* DxgkDdiOpenAllocation should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, hDevice(0x%x)", hDevice));

    nemuVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_DEVICE pDevice = (PNEMUWDDM_DEVICE)hDevice;
    PNEMUMP_DEVEXT pDevExt = pDevice->pAdapter;
    PNEMUWDDM_RCINFO pRcInfo = NULL;
    if (pOpenAllocation->PrivateDriverSize)
    {
        Assert(pOpenAllocation->pPrivateDriverData);
        if (pOpenAllocation->PrivateDriverSize == sizeof (NEMUWDDM_RCINFO))
        {
            pRcInfo = (PNEMUWDDM_RCINFO)pOpenAllocation->pPrivateDriverData;
            Assert(pRcInfo->cAllocInfos == pOpenAllocation->NumAllocations);
        }
        else
        {
            WARN(("Invalid PrivateDriverSize %d", pOpenAllocation->PrivateDriverSize));
            Status = STATUS_INVALID_PARAMETER;
        }
    }

    if (Status == STATUS_SUCCESS)
    {
        UINT i = 0;
        for (; i < pOpenAllocation->NumAllocations; ++i)
        {
            DXGK_OPENALLOCATIONINFO* pInfo = &pOpenAllocation->pOpenAllocation[i];
            Assert(pInfo->PrivateDriverDataSize == sizeof (NEMUWDDM_ALLOCINFO));
            Assert(pInfo->pPrivateDriverData);
            PNEMUWDDM_ALLOCATION pAllocation = nemuWddmGetAllocationFromHandle(pDevExt, pInfo->hAllocation);
            if (!pAllocation)
            {
                WARN(("invalid handle"));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

#ifdef DEBUG
            Assert(!pAllocation->fAssumedDeletion);
#endif
            if (pRcInfo)
            {
                Assert(pAllocation->enmType == NEMUWDDM_ALLOC_TYPE_UMD_RC_GENERIC);

                if (pInfo->PrivateDriverDataSize != sizeof (NEMUWDDM_ALLOCINFO)
                        || !pInfo->pPrivateDriverData)
                {
                    WARN(("invalid data size"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
                PNEMUWDDM_ALLOCINFO pAllocInfo = (PNEMUWDDM_ALLOCINFO)pInfo->pPrivateDriverData;

#ifdef NEMU_WITH_VIDEOHWACCEL
                if (pRcInfo->RcDesc.fFlags.Overlay)
                {
                    /* we have queried host for some surface info, like pitch & size,
                     * need to return it back to the UMD (User Mode Drive) */
                    pAllocInfo->SurfDesc = pAllocation->AllocData.SurfDesc;
                    /* success, just continue */
                }
#endif
            }

            KIRQL OldIrql;
            PNEMUWDDM_OPENALLOCATION pOa;
            KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
            pOa = NemuWddmOaSearchLocked(pDevice, pAllocation);
            if (pOa)
            {
                ++pOa->cOpens;
                ++pAllocation->cOpens;
                KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
            }
            else
            {
                KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
                pOa = (PNEMUWDDM_OPENALLOCATION)nemuWddmMemAllocZero(sizeof (NEMUWDDM_OPENALLOCATION));
                if (!pOa)
                {
                    WARN(("failed to allocation alloc info"));
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                pOa->hAllocation = pInfo->hAllocation;
                pOa->pAllocation = pAllocation;
                pOa->pDevice = pDevice;
                pOa->cOpens = 1;

                PNEMUWDDM_OPENALLOCATION pConcurrentOa;
                KeAcquireSpinLock(&pAllocation->OpenLock, &OldIrql);
                pConcurrentOa = NemuWddmOaSearchLocked(pDevice, pAllocation);
                if (!pConcurrentOa)
                    InsertHeadList(&pAllocation->OpenList, &pOa->ListEntry);
                else
                    ++pConcurrentOa->cOpens;
                ++pAllocation->cOpens;
                KeReleaseSpinLock(&pAllocation->OpenLock, OldIrql);
                if (pConcurrentOa)
                {
                    nemuWddmMemFree(pOa);
                    pOa = pConcurrentOa;
                }
            }

            pInfo->hDeviceSpecificAllocation = pOa;
        }

        if (Status != STATUS_SUCCESS)
        {
            for (UINT j = 0; j < i; ++j)
            {
                DXGK_OPENALLOCATIONINFO* pInfo2Free = &pOpenAllocation->pOpenAllocation[j];
                PNEMUWDDM_OPENALLOCATION pOa2Free = (PNEMUWDDM_OPENALLOCATION)pInfo2Free->hDeviceSpecificAllocation;
                NemuWddmOaRelease(pOa2Free);
            }
        }
    }
    LOGF(("LEAVE, hDevice(0x%x)", hDevice));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiCloseAllocation(
    CONST HANDLE  hDevice,
    CONST DXGKARG_CLOSEALLOCATION*  pCloseAllocation)
{
    /* DxgkDdiCloseAllocation should be made pageable. */
    PAGED_CODE();

    LOGF(("ENTER, hDevice(0x%x)", hDevice));

    nemuVDbgBreakFv();

    PNEMUWDDM_DEVICE pDevice = (PNEMUWDDM_DEVICE)hDevice;
    PNEMUMP_DEVEXT pDevExt = pDevice->pAdapter;

    for (UINT i = 0; i < pCloseAllocation->NumAllocations; ++i)
    {
        PNEMUWDDM_OPENALLOCATION pOa2Free = (PNEMUWDDM_OPENALLOCATION)pCloseAllocation->pOpenHandleList[i];
        PNEMUWDDM_ALLOCATION pAllocation = pOa2Free->pAllocation;
        Assert(pAllocation->cShRcRefs >= pOa2Free->cShRcRefs);
        pAllocation->cShRcRefs -= pOa2Free->cShRcRefs;
        NemuWddmOaRelease(pOa2Free);
    }

    LOGF(("LEAVE, hDevice(0x%x)", hDevice));

    return STATUS_SUCCESS;
}

#ifdef NEMU_WITH_CROGL
static NTSTATUS
APIENTRY
DxgkDdiRenderNew(
    CONST HANDLE  hContext,
    DXGKARG_RENDER  *pRender)
{
//    LOGF(("ENTER, hContext(0x%x)", hContext));
    nemuVDbgBreakF();

    if (pRender->DmaBufferPrivateDataSize < sizeof (NEMUCMDVBVA_HDR))
    {
        WARN(("pRender->DmaBufferPrivateDataSize(%d) < sizeof NEMUCMDVBVA_HDR (%d)",
                pRender->DmaBufferPrivateDataSize , sizeof (NEMUCMDVBVA_HDR)));
        /* @todo: can this actually happen? what status to return? */
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->CommandLength < sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        WARN(("Present->DmaBufferPrivateDataSize(%d) < sizeof NEMUWDDM_DMA_PRIVATEDATA_BASEHDR (%d)",
                pRender->DmaBufferPrivateDataSize , sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->PatchLocationListOutSize < pRender->AllocationListSize)
    {
        WARN(("pRender->PatchLocationListOutSize(%d) < pRender->AllocationListSize(%d)",
                pRender->PatchLocationListOutSize, pRender->AllocationListSize));
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;

    __try
    {
        PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR pInputHdr = (PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR)pRender->pCommand;

        uint32_t cbBuffer = 0;
        uint32_t cbPrivateData = 0;
        uint32_t cbCmd = 0;
        NEMUCMDVBVA_HDR* pCmd = (NEMUCMDVBVA_HDR*)pRender->pDmaBufferPrivateData;

        switch (pInputHdr->enmCmd)
        {
            case NEMUVDMACMD_TYPE_CHROMIUM_CMD:
            {
                if (pRender->AllocationListSize >= (UINT32_MAX - RT_OFFSETOF(NEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD, aBufInfos))/ RT_SIZEOFMEMB(NEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD, aBufInfos[0]))
                {
                    WARN(("Invalid AllocationListSize %d", pRender->AllocationListSize));
                    return STATUS_INVALID_PARAMETER;
                }

                if (pRender->CommandLength != RT_OFFSETOF(NEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD, aBufInfos[pRender->AllocationListSize]))
                {
                    WARN(("pRender->CommandLength (%d) != RT_OFFSETOF(NEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD, aBufInfos[pRender->AllocationListSize](%d)",
                            pRender->CommandLength, RT_OFFSETOF(NEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD, aBufInfos[pRender->AllocationListSize])));
                    return STATUS_INVALID_PARAMETER;
                }

                if (pRender->AllocationListSize >= (UINT32_MAX - RT_OFFSETOF(NEMUCMDVBVA_CRCMD, Cmd.aBuffers))/ RT_SIZEOFMEMB(NEMUCMDVBVA_CRCMD, Cmd.aBuffers[0]))
                {
                    WARN(("Invalid AllocationListSize %d", pRender->AllocationListSize));
                    return STATUS_INVALID_PARAMETER;
                }

                cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
                cbPrivateData = RT_OFFSETOF(NEMUCMDVBVA_CRCMD, Cmd.aBuffers[pRender->AllocationListSize]);

                if (pRender->DmaBufferPrivateDataSize < cbPrivateData)
                {
                    WARN(("pRender->DmaBufferPrivateDataSize too small %d, requested %d", pRender->DmaBufferPrivateDataSize, cbPrivateData));
                    return STATUS_INVALID_PARAMETER;
                }

                if (pRender->DmaSize < cbBuffer)
                {
                    WARN(("dma buffer %d too small", pRender->DmaSize));
                    return STATUS_INVALID_PARAMETER;
                }

    //            Assert(pRender->PatchLocationListOutSize == pRender->AllocationListSize);

                if (pRender->PatchLocationListOutSize < pRender->AllocationListSize)
                {
                    WARN(("pRender->PatchLocationListOutSize too small %d, requested %d", pRender->PatchLocationListOutSize, pRender->AllocationListSize));
                    return STATUS_INVALID_PARAMETER;
                }

                PNEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD pUmCmd = (PNEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD)pInputHdr;
                NEMUCMDVBVA_CRCMD* pChromiumCmd = (NEMUCMDVBVA_CRCMD*)pRender->pDmaBufferPrivateData;

                PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)hContext;
                PNEMUWDDM_DEVICE pDevice = pContext->pDevice;
                PNEMUMP_DEVEXT pDevExt = pDevice->pAdapter;

                pChromiumCmd->Hdr.u8OpCode = NEMUCMDVBVA_OPTYPE_CRCMD;
                pChromiumCmd->Hdr.u8Flags = 0;
                pChromiumCmd->Cmd.cBuffers = pRender->AllocationListSize;

                DXGK_ALLOCATIONLIST *pAllocationList = pRender->pAllocationList;
                NEMUCMDVBVA_CRCMD_BUFFER *pSubmInfo = pChromiumCmd->Cmd.aBuffers;
                PNEMUWDDM_UHGSMI_BUFFER_UI_SUBMIT_INFO pSubmUmInfo = pUmCmd->aBufInfos;

                for (UINT i = 0; i < pRender->AllocationListSize; ++i, ++pRender->pPatchLocationListOut, ++pAllocationList, ++pSubmInfo, ++pSubmUmInfo)
                {
                    NEMUWDDM_UHGSMI_BUFFER_UI_SUBMIT_INFO SubmUmInfo = *pSubmUmInfo;
                    D3DDDI_PATCHLOCATIONLIST* pPLL = pRender->pPatchLocationListOut;
                    PNEMUWDDM_ALLOCATION pAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pAllocationList);
                    if (SubmUmInfo.offData >= pAlloc->AllocData.SurfDesc.cbSize
                            || SubmUmInfo.cbData > pAlloc->AllocData.SurfDesc.cbSize
                            || SubmUmInfo.offData + SubmUmInfo.cbData > pAlloc->AllocData.SurfDesc.cbSize)
                    {
                        WARN(("invalid data"));
                        return STATUS_INVALID_PARAMETER;
                    }

                    memset(pPLL, 0, sizeof (*pPLL));

                    if (pAllocationList->SegmentId)
                        pSubmInfo->offBuffer = pAllocationList->PhysicalAddress.LowPart + SubmUmInfo.offData;

                    pSubmInfo->cbBuffer = SubmUmInfo.cbData;

                    pPLL->AllocationIndex = i;
                    pPLL->PatchOffset = RT_OFFSETOF(NEMUCMDVBVA_CRCMD, Cmd.aBuffers[i].offBuffer);
                    pPLL->AllocationOffset = SubmUmInfo.offData;
                }

                cbCmd = cbPrivateData;

                break;
            }
            case NEMUVDMACMD_TYPE_DMA_NOP:
            {
                cbPrivateData = sizeof (NEMUCMDVBVA_HDR);
                cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;

                if (pRender->DmaBufferPrivateDataSize < cbPrivateData)
                {
                    WARN(("pRender->DmaBufferPrivateDataSize too small %d, requested %d", pRender->DmaBufferPrivateDataSize, cbPrivateData));
                    return STATUS_INVALID_PARAMETER;
                }

                if (pRender->DmaSize < cbBuffer)
                {
                    WARN(("dma buffer %d too small", pRender->DmaSize));
                    return STATUS_INVALID_PARAMETER;
                }

                pCmd->u8OpCode = NEMUCMDVBVA_OPTYPE_NOPCMD;
                pCmd->u8Flags = 0;

                for (UINT i = 0; i < pRender->AllocationListSize; ++i, ++pRender->pPatchLocationListOut)
                {
                    D3DDDI_PATCHLOCATIONLIST* pPLL = pRender->pPatchLocationListOut;
                    memset(pPLL, 0, sizeof (*pPLL));
                    pPLL->AllocationIndex = i;
                    pPLL->PatchOffset = ~0UL;
                    pPLL->AllocationOffset = 0;
                }

                cbCmd = cbPrivateData;

                break;
            }
            default:
             {
                 WARN(("unsupported render command %d", pInputHdr->enmCmd));
                 return STATUS_INVALID_PARAMETER;
             }
        }

        Assert(cbPrivateData >= sizeof (NEMUCMDVBVA_HDR));
        pRender->pDmaBufferPrivateData = ((uint8_t*)pRender->pDmaBufferPrivateData) + cbPrivateData;
        pRender->pDmaBuffer = ((uint8_t*)pRender->pDmaBuffer) + cbBuffer;

        pCmd->u8State = NEMUCMDVBVA_STATE_SUBMITTED;
        pCmd->u2.complexCmdEl.u16CbCmdHost = cbPrivateData;
        pCmd->u2.complexCmdEl.u16CbCmdGuest = cbBuffer;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_PARAMETER;
        WARN(("invalid parameter"));
    }
//    LOGF(("LEAVE, hContext(0x%x)", hContext));

    return Status;
}
#endif

static void nemuWddmPatchLocationInit(D3DDDI_PATCHLOCATIONLIST *pPatchLocationListOut, UINT idx, UINT offPatch)
{
    memset(pPatchLocationListOut, 0, sizeof (*pPatchLocationListOut));
    pPatchLocationListOut->AllocationIndex = idx;
    pPatchLocationListOut->PatchOffset = offPatch;
}

static NTSTATUS
APIENTRY
DxgkDdiRenderLegacy(
    CONST HANDLE  hContext,
    DXGKARG_RENDER  *pRender)
{
//    LOGF(("ENTER, hContext(0x%x)", hContext));

    if (pRender->DmaBufferPrivateDataSize < sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        WARN(("Present->DmaBufferPrivateDataSize(%d) < sizeof NEMUWDDM_DMA_PRIVATEDATA_BASEHDR (%d)",
                pRender->DmaBufferPrivateDataSize , sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->CommandLength < sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR))
    {
        WARN(("Present->DmaBufferPrivateDataSize(%d) < sizeof NEMUWDDM_DMA_PRIVATEDATA_BASEHDR (%d)",
                pRender->DmaBufferPrivateDataSize , sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR)));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->DmaSize < pRender->CommandLength)
    {
        WARN(("pRender->DmaSize(%d) < pRender->CommandLength(%d)",
                pRender->DmaSize, pRender->CommandLength));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->PatchLocationListOutSize < pRender->PatchLocationListInSize)
    {
        WARN(("pRender->PatchLocationListOutSize(%d) < pRender->PatchLocationListInSize(%d)",
                pRender->PatchLocationListOutSize, pRender->PatchLocationListInSize));
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->AllocationListSize != pRender->PatchLocationListInSize)
    {
        WARN(("pRender->AllocationListSize(%d) != pRender->PatchLocationListInSize(%d)",
                pRender->AllocationListSize, pRender->PatchLocationListInSize));
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;

    __try
    {
        PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR pInputHdr = (PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR)pRender->pCommand;
        switch (pInputHdr->enmCmd)
        {
            case NEMUVDMACMD_TYPE_DMA_NOP:
            {
                PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR pPrivateData = (PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR)pRender->pDmaBufferPrivateData;
                pPrivateData->enmCmd = NEMUVDMACMD_TYPE_DMA_NOP;
                pRender->pDmaBufferPrivateData = (uint8_t*)pRender->pDmaBufferPrivateData + sizeof (NEMUWDDM_DMA_PRIVATEDATA_BASEHDR);
                pRender->pDmaBuffer = ((uint8_t*)pRender->pDmaBuffer) + pRender->CommandLength;
                for (UINT i = 0; i < pRender->PatchLocationListInSize; ++i)
                {
                    UINT offPatch = i * 4;
                    if (offPatch + 4 > pRender->CommandLength)
                    {
                        WARN(("wrong offPatch"));
                        return STATUS_INVALID_PARAMETER;
                    }
                    if (offPatch != pRender->pPatchLocationListIn[i].PatchOffset)
                    {
                        WARN(("wrong PatchOffset"));
                        return STATUS_INVALID_PARAMETER;
                    }
                    if (i != pRender->pPatchLocationListIn[i].AllocationIndex)
                    {
                        WARN(("wrong AllocationIndex"));
                        return STATUS_INVALID_PARAMETER;
                    }
                    nemuWddmPatchLocationInit(&pRender->pPatchLocationListOut[i], i, offPatch);
                }
                break;
            }
            default:
            {
                WARN(("unsupported command %d", pInputHdr->enmCmd));
                return STATUS_INVALID_PARAMETER;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_PARAMETER;
        WARN(("invalid parameter"));
    }
//    LOGF(("LEAVE, hContext(0x%x)", hContext));

    return Status;
}

#define NEMUVDMACMD_DMA_PRESENT_BLT_MINSIZE() (NEMUVDMACMD_SIZE(NEMUVDMACMD_DMA_PRESENT_BLT))
#define NEMUVDMACMD_DMA_PRESENT_BLT_SIZE(_c) (NEMUVDMACMD_BODY_FIELD_OFFSET(UINT, NEMUVDMACMD_DMA_PRESENT_BLT, aDstSubRects[_c]))

#ifdef NEMU_WITH_VDMA
DECLINLINE(VOID) nemuWddmRectlFromRect(const RECT *pRect, PNEMUVDMA_RECTL pRectl)
{
    pRectl->left = (int16_t)pRect->left;
    pRectl->width = (uint16_t)(pRect->right - pRect->left);
    pRectl->top = (int16_t)pRect->top;
    pRectl->height = (uint16_t)(pRect->bottom - pRect->top);
}

DECLINLINE(NEMUVDMA_PIXEL_FORMAT) nemuWddmFromPixFormat(D3DDDIFORMAT format)
{
    return (NEMUVDMA_PIXEL_FORMAT)format;
}

DECLINLINE(VOID) nemuWddmSurfDescFromAllocation(PNEMUWDDM_ALLOCATION pAllocation, PNEMUVDMA_SURF_DESC pDesc)
{
    pDesc->width = pAllocation->AllocData.SurfDesc.width;
    pDesc->height = pAllocation->AllocData.SurfDesc.height;
    pDesc->format = nemuWddmFromPixFormat(pAllocation->AllocData.SurfDesc.format);
    pDesc->bpp = pAllocation->AllocData.SurfDesc.bpp;
    pDesc->pitch = pAllocation->AllocData.SurfDesc.pitch;
    pDesc->fFlags = 0;
}
#endif

DECLINLINE(BOOLEAN) nemuWddmPixFormatConversionSupported(D3DDDIFORMAT From, D3DDDIFORMAT To)
{
    Assert(From != D3DDDIFMT_UNKNOWN);
    Assert(To != D3DDDIFMT_UNKNOWN);
    Assert(From == To);
    return From == To;
}

#ifdef NEMU_WITH_CROGL

DECLINLINE(void) NemuCVDdiFillAllocDescHostID(NEMUCMDVBVA_ALLOCDESC *pDesc, const NEMUWDDM_ALLOCATION *pAlloc)
{
    pDesc->Info.u.id = pAlloc->AllocData.hostID;
    /* we do not care about wdth and height, zero them up though */
    pDesc->u16Width = 0;
    pDesc->u16Height = 0;
}

DECLINLINE(void) NemuCVDdiFillAllocInfoOffVRAM(NEMUCMDVBVA_ALLOCINFO *pInfo, const DXGK_ALLOCATIONLIST *pList)
{
    Assert(!pList->PhysicalAddress.HighPart);
    pInfo->u.offVRAM = pList->PhysicalAddress.LowPart;
}

DECLINLINE(void) NemuCVDdiFillAllocDescOffVRAM(NEMUCMDVBVA_ALLOCDESC *pDesc, const NEMUWDDM_ALLOCATION *pAlloc, const DXGK_ALLOCATIONLIST *pList)
{
    NemuCVDdiFillAllocInfoOffVRAM(&pDesc->Info, pList);
    pDesc->u16Width = (uint16_t)pAlloc->AllocData.SurfDesc.width;
    pDesc->u16Height = (uint16_t)pAlloc->AllocData.SurfDesc.height;
}

static NTSTATUS nemuWddmCmCmdBltIdNotIdFill(NEMUCMDVBVA_BLT_HDR *pBltHdr, const NEMUWDDM_ALLOCATION *pIdAlloc, const NEMUWDDM_ALLOCATION *pAlloc, const DXGK_ALLOCATIONLIST *pList,
                            BOOLEAN fToId, uint32_t *poffPatch, uint32_t *poffRects)
{
    uint8_t fFlags;
    Assert(pIdAlloc->AllocData.hostID);
    Assert(!pAlloc->AllocData.hostID);

    D3DDDIFORMAT enmFormat = nemuWddmFmtNoAlphaFormat(pAlloc->AllocData.SurfDesc.format);
    if (enmFormat != D3DDDIFMT_X8R8G8B8)
    {
        WARN(("unsupported format"));
        return STATUS_INVALID_PARAMETER;
    }

    if (pIdAlloc->AllocData.SurfDesc.width == pAlloc->AllocData.SurfDesc.width
            && pIdAlloc->AllocData.SurfDesc.height == pAlloc->AllocData.SurfDesc.height)
    {
        fFlags = NEMUCMDVBVA_OPF_BLT_TYPE_OFFPRIMSZFMT_OR_ID | NEMUCMDVBVA_OPF_OPERAND2_ISID;
        NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID *pBlt = (NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID*)pBltHdr;
        NemuCVDdiFillAllocInfoOffVRAM(&pBlt->alloc, pList);
        pBlt->id = pIdAlloc->AllocData.hostID;
        *poffPatch = RT_OFFSETOF(NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID, alloc.u.offVRAM);
        *poffRects = RT_OFFSETOF(NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID, aRects);
    }
    else
    {
        fFlags = NEMUCMDVBVA_OPF_BLT_TYPE_SAMEDIM_A8R8G8B8 | NEMUCMDVBVA_OPF_OPERAND2_ISID;
        NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8 *pBlt = (NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8*)pBltHdr;
        NemuCVDdiFillAllocDescOffVRAM(&pBlt->alloc1, pAlloc, pList);
        pBlt->info2.u.id = pIdAlloc->AllocData.hostID;
        *poffPatch = RT_OFFSETOF(NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8, alloc1.Info.u.offVRAM);
        *poffRects = RT_OFFSETOF(NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8, aRects);
    }

    if (fToId)
        fFlags |= NEMUCMDVBVA_OPF_BLT_DIR_IN_2;

    pBltHdr->Hdr.u8Flags |= fFlags;
    return STATUS_SUCCESS;
}

static NTSTATUS nemuWddmCmCmdBltNotIdNotIdFill(NEMUCMDVBVA_BLT_HDR *pBltHdr, const NEMUWDDM_ALLOCATION *pSrcAlloc, const DXGK_ALLOCATIONLIST *pSrcList,
        const NEMUWDDM_ALLOCATION *pDstAlloc, const DXGK_ALLOCATIONLIST *pDstList,
                            uint32_t *poffSrcPatch, uint32_t *poffDstPatch, uint32_t *poffRects)
{
    if (pDstAlloc->AllocData.SurfDesc.width == pSrcAlloc->AllocData.SurfDesc.width
            && pDstAlloc->AllocData.SurfDesc.height == pSrcAlloc->AllocData.SurfDesc.height)
    {
        pBltHdr->Hdr.u8Flags |= NEMUCMDVBVA_OPF_BLT_TYPE_SAMEDIM_A8R8G8B8;
        NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8 *pBlt = (NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8*)pBltHdr;
        NemuCVDdiFillAllocDescOffVRAM(&pBlt->alloc1, pDstAlloc, pDstList);
        NemuCVDdiFillAllocInfoOffVRAM(&pBlt->info2, pSrcList);
        *poffDstPatch = RT_OFFSETOF(NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8, alloc1.Info.u.offVRAM);
        *poffSrcPatch = RT_OFFSETOF(NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8, info2.u.offVRAM);
        *poffRects = RT_OFFSETOF(NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8, aRects);
    }
    else
    {
        pBltHdr->Hdr.u8Flags |= NEMUCMDVBVA_OPF_BLT_TYPE_GENERIC_A8R8G8B8;
        NEMUCMDVBVA_BLT_GENERIC_A8R8G8B8 *pBlt = (NEMUCMDVBVA_BLT_GENERIC_A8R8G8B8*)pBltHdr;
        NemuCVDdiFillAllocDescOffVRAM(&pBlt->alloc1, pDstAlloc, pDstList);
        NemuCVDdiFillAllocDescOffVRAM(&pBlt->alloc2, pSrcAlloc, pSrcList);
        *poffDstPatch = RT_OFFSETOF(NEMUCMDVBVA_BLT_GENERIC_A8R8G8B8, alloc1.Info.u.offVRAM);
        *poffSrcPatch = RT_OFFSETOF(NEMUCMDVBVA_BLT_GENERIC_A8R8G8B8, alloc2.Info.u.offVRAM);
        *poffRects = RT_OFFSETOF(NEMUCMDVBVA_BLT_GENERIC_A8R8G8B8, aRects);
    }
    return STATUS_SUCCESS;
}
/**
 * DxgkDdiPresent
 */
static NTSTATUS
APIENTRY
DxgkDdiPresentNew(
    CONST HANDLE  hContext,
    DXGKARG_PRESENT  *pPresent)
{
    PAGED_CODE();

//    LOGF(("ENTER, hContext(0x%x)", hContext));

    nemuVDbgBreakFv();

    PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)hContext;
    PNEMUWDDM_DEVICE pDevice = pContext->pDevice;
    PNEMUMP_DEVEXT pDevExt = pDevice->pAdapter;
    uint32_t cbBuffer = 0;
    uint32_t cbPrivateData = 0;

    if (pPresent->DmaBufferPrivateDataSize < sizeof (NEMUCMDVBVA_HDR))
    {
        WARN(("Present->DmaBufferPrivateDataSize(%d) < sizeof NEMUCMDVBVA_HDR (%d)", pPresent->DmaBufferPrivateDataSize , sizeof (NEMUCMDVBVA_HDR)));
        /* @todo: can this actually happen? what status to return? */
        return STATUS_INVALID_PARAMETER;
    }

    NEMUCMDVBVA_HDR* pHdr = (NEMUCMDVBVA_HDR*)pPresent->pDmaBufferPrivateData;

    UINT u32SrcPatch = ~0UL;
    UINT u32DstPatch = ~0UL;
    BOOLEAN fPatchSrc = false;
    BOOLEAN fPatchDst = false;
    NEMUCMDVBVA_RECT *paRects = NULL;

    if (pPresent->DmaSize < NEMUWDDM_DUMMY_DMABUFFER_SIZE)
    {
        WARN(("Present->DmaSize(%d) < NEMUWDDM_DUMMY_DMABUFFER_SIZE (%d)", pPresent->DmaSize , NEMUWDDM_DUMMY_DMABUFFER_SIZE));
        /* @todo: can this actually happen? what status to return? */
        return STATUS_INVALID_PARAMETER;
    }

#ifdef NEMU_WDDM_DUMP_REGIONS_ON_PRESENT
    LogRel(("%s: [%ld, %ld, %ld, %ld] -> [%ld, %ld, %ld, %ld] (SubRectCnt=%u)\n",
        pPresent->Flags.Blt ? "Blt" : (pPresent->Flags.Flip ? "Flip" : (pPresent->Flags.ColorFill ? "ColorFill" : "Unknown OP")),
        pPresent->SrcRect.left, pPresent->SrcRect.top, pPresent->SrcRect.right, pPresent->SrcRect.bottom,
        pPresent->DstRect.left, pPresent->DstRect.top, pPresent->DstRect.right, pPresent->DstRect.bottom,
        pPresent->SubRectCnt));
    for (unsigned int i = 0; i < pPresent->SubRectCnt; i++)
        LogRel(("\tsub#%u = [%ld, %ld, %ld, %ld]\n", i, pPresent->pDstSubRects[i].left, pPresent->pDstSubRects[i].top, pPresent->pDstSubRects[i].right, pPresent->pDstSubRects[i].bottom));
#endif

    if (pPresent->Flags.Blt)
    {
        Assert(pPresent->Flags.Value == 1); /* only Blt is set, we do not support anything else for now */
        DXGK_ALLOCATIONLIST *pSrc =  &pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX];
        DXGK_ALLOCATIONLIST *pDst =  &pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX];
        PNEMUWDDM_ALLOCATION pSrcAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pSrc);
        if (!pSrcAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get Src Allocation info for hDeviceSpecificAllocation(0x%x)",pSrc->hDeviceSpecificAllocation));
            return STATUS_INVALID_HANDLE;
        }

        PNEMUWDDM_ALLOCATION pDstAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pDst);
        if (!pDstAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get Dst Allocation info for hDeviceSpecificAllocation(0x%x)",pDst->hDeviceSpecificAllocation));
            return STATUS_INVALID_HANDLE;
        }

        fPatchSrc = TRUE;
        fPatchDst = TRUE;

        BOOLEAN fDstPrimary = (!pDstAlloc->AllocData.hostID
                && pDstAlloc->enmType == NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE
                && pDstAlloc->bAssigned);
        BOOLEAN fSrcPrimary = (!pSrcAlloc->AllocData.hostID
                && pSrcAlloc->enmType == NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE
                && pSrcAlloc->bAssigned);

        pHdr->u8OpCode = NEMUCMDVBVA_OPTYPE_BLT;
        pHdr->u8Flags = 0;

        NEMUCMDVBVA_BLT_HDR *pBltHdr = (NEMUCMDVBVA_BLT_HDR*)pHdr;
        pBltHdr->Pos.x = (int16_t)(pPresent->DstRect.left - pPresent->SrcRect.left);
        pBltHdr->Pos.y = (int16_t)(pPresent->DstRect.top - pPresent->SrcRect.top);

        if (pPresent->DmaBufferPrivateDataSize < NEMUCMDVBVA_SIZEOF_BLTSTRUCT_MAX)
        {
            WARN(("Present->DmaBufferPrivateDataSize(%d) < (%d)", pPresent->DmaBufferPrivateDataSize , NEMUCMDVBVA_SIZEOF_BLTSTRUCT_MAX));
            /* @todo: can this actually happen? what status to return? */
            return STATUS_INVALID_PARAMETER;
        }

        if (pSrcAlloc->AllocData.hostID)
        {
            if (pDstAlloc->AllocData.hostID)
            {
                NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID *pBlt = (NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID*)pBltHdr;
                pHdr->u8Flags |= NEMUCMDVBVA_OPF_BLT_TYPE_OFFPRIMSZFMT_OR_ID | NEMUCMDVBVA_OPF_OPERAND1_ISID | NEMUCMDVBVA_OPF_OPERAND2_ISID;
                pBlt->id = pDstAlloc->AllocData.hostID;
                pBlt->alloc.u.id = pSrcAlloc->AllocData.hostID;
                cbPrivateData = RT_OFFSETOF(NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID, aRects);
            }
            else
            {
                NTSTATUS Status = nemuWddmCmCmdBltIdNotIdFill(pBltHdr, pSrcAlloc, pDstAlloc, pDst, FALSE, &u32DstPatch, &cbPrivateData);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("nemuWddmCmCmdBltIdNotIdFill failed, %#x", Status));
                    return Status;
                }
            }
        }
        else
        {
            if (pDstAlloc->AllocData.hostID)
            {
                NTSTATUS Status = nemuWddmCmCmdBltIdNotIdFill(pBltHdr, pDstAlloc, pSrcAlloc, pSrc, TRUE, &u32SrcPatch, &cbPrivateData);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("nemuWddmCmCmdBltIdNotIdFill failed, %#x", Status));
                    return Status;
                }
            }
            else
            {
                nemuWddmCmCmdBltNotIdNotIdFill(pBltHdr, pSrcAlloc, pSrc, pDstAlloc, pDst, &u32SrcPatch, &u32DstPatch, &cbPrivateData);
            }
        }

        if (fDstPrimary)
            pBltHdr->Hdr.u.u8PrimaryID = (uint8_t)pDstAlloc->AllocData.SurfDesc.VidPnSourceId;
        else if (fSrcPrimary)
        {
            pBltHdr->Hdr.u8Flags |= NEMUCMDVBVA_OPF_PRIMARY_HINT_SRC;
            pBltHdr->Hdr.u.u8PrimaryID = (uint8_t)pSrcAlloc->AllocData.SurfDesc.VidPnSourceId;
        }
        else
            pBltHdr->Hdr.u.u8PrimaryID = 0xff;

        paRects = (NEMUCMDVBVA_RECT*)(((uint8_t*)pPresent->pDmaBufferPrivateData) + cbPrivateData);
        cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
    }
    else if (pPresent->Flags.Flip)
    {
        if (pPresent->DmaBufferPrivateDataSize < sizeof (NEMUCMDVBVA_FLIP))
        {
            WARN(("Present->DmaBufferPrivateDataSize(%d) < sizeof NEMUCMDVBVA_FLIP (%d)", pPresent->DmaBufferPrivateDataSize , sizeof (NEMUCMDVBVA_FLIP)));
            /* @todo: can this actually happen? what status to return? */
            return STATUS_INVALID_PARAMETER;
        }

        fPatchSrc = TRUE;

        Assert(pPresent->Flags.Value == 4); /* only Blt is set, we do not support anything else for now */
        Assert(pContext->enmType == NEMUWDDM_CONTEXT_TYPE_CUSTOM_3D);
        DXGK_ALLOCATIONLIST *pSrc =  &pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX];
        PNEMUWDDM_ALLOCATION pSrcAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pSrc);

        if (!pSrcAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get pSrc Allocation info for hDeviceSpecificAllocation(0x%x)",pSrc->hDeviceSpecificAllocation));
            return STATUS_INVALID_HANDLE;
        }

        Assert(pDevExt->cContexts3D);
        pHdr->u8OpCode = NEMUCMDVBVA_OPTYPE_FLIP;
        Assert((UINT)pSrcAlloc->AllocData.SurfDesc.VidPnSourceId < (UINT)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
        pHdr->u.u8PrimaryID = pSrcAlloc->AllocData.SurfDesc.VidPnSourceId;
        NEMUCMDVBVA_FLIP *pFlip = (NEMUCMDVBVA_FLIP*)pHdr;

        if (pSrcAlloc->AllocData.hostID)
        {
            pHdr->u8Flags = NEMUCMDVBVA_OPF_OPERAND1_ISID;
            pFlip->src.u.id = pSrcAlloc->AllocData.hostID;
        }
        else
        {
            WARN(("NemuCVDdiFillAllocInfo reported no host id for flip!"));
            pHdr->u8Flags = 0;
            NemuCVDdiFillAllocInfoOffVRAM(&pFlip->src, pSrc);
            u32SrcPatch = RT_OFFSETOF(NEMUCMDVBVA_FLIP, src.u.offVRAM);
        }

        cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
        paRects = pFlip->aRects;
        cbPrivateData = NEMUCMDVBVA_SIZEOF_FLIPSTRUCT_MIN;
    }
    else if (pPresent->Flags.ColorFill)
    {
#ifdef DEBUG_misha
        WARN(("test color fill!"));
#endif

        fPatchDst = TRUE;

        Assert(pContext->enmType == NEMUWDDM_CONTEXT_TYPE_CUSTOM_2D);
        Assert(pPresent->Flags.Value == 2); /* only ColorFill is set, we do not support anything else for now */
        DXGK_ALLOCATIONLIST *pDst =  &pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX];
        PNEMUWDDM_ALLOCATION pDstAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pDst);
        if (!pDstAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get pDst Allocation info for hDeviceSpecificAllocation(0x%x)",pDst->hDeviceSpecificAllocation));
            return STATUS_INVALID_HANDLE;
        }

        if (pDstAlloc->AllocData.hostID)
        {
            WARN(("color fill present for texid not supported"));
            pHdr->u8OpCode = NEMUCMDVBVA_OPTYPE_NOPCMD;
            pHdr->u8Flags = 0;
            pHdr->u8State = NEMUCMDVBVA_STATE_SUBMITTED;
            cbPrivateData = sizeof (NEMUCMDVBVA_HDR);
            cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
        }
        else
        {
            BOOLEAN fDstPrimary = (!pDstAlloc->AllocData.hostID
                    && pDstAlloc->enmType == NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE
                    && pDstAlloc->bAssigned);

            if (pPresent->DmaBufferPrivateDataSize < NEMUCMDVBVA_SIZEOF_CLRFILLSTRUCT_MAX)
            {
                WARN(("Present->DmaBufferPrivateDataSize(%d) < NEMUCMDVBVA_SIZEOF_CLRFILLSTRUCT_MAX (%d)", pPresent->DmaBufferPrivateDataSize , NEMUCMDVBVA_SIZEOF_CLRFILLSTRUCT_MAX));
                /* @todo: can this actually happen? what status to return? */
                return STATUS_INVALID_PARAMETER;
            }

            NEMUCMDVBVA_CLRFILL_HDR *pClrFillHdr = (NEMUCMDVBVA_CLRFILL_HDR*)pHdr;

            pClrFillHdr->Hdr.u8OpCode = NEMUCMDVBVA_OPTYPE_CLRFILL;
            pClrFillHdr->u32Color = pPresent->Color;

            pHdr->u8Flags = NEMUCMDVBVA_OPF_CLRFILL_TYPE_GENERIC_A8R8G8B8;
            pHdr->u.u8PrimaryID = 0;

            NEMUCMDVBVA_CLRFILL_GENERIC_A8R8G8B8 *pCFill = (NEMUCMDVBVA_CLRFILL_GENERIC_A8R8G8B8*)pHdr;
            NemuCVDdiFillAllocInfoOffVRAM(&pCFill->dst.Info, pDst);
            pCFill->dst.u16Width = (uint16_t)pDstAlloc->AllocData.SurfDesc.width;
            pCFill->dst.u16Height = (uint16_t)pDstAlloc->AllocData.SurfDesc.height;
            u32DstPatch = RT_OFFSETOF(NEMUCMDVBVA_CLRFILL_GENERIC_A8R8G8B8, dst.Info.u.offVRAM);
            paRects = pCFill->aRects;
            cbPrivateData = RT_OFFSETOF(NEMUCMDVBVA_CLRFILL_GENERIC_A8R8G8B8, aRects);

            if (fDstPrimary)
                pCFill->Hdr.Hdr.u.u8PrimaryID = (uint8_t)pDstAlloc->AllocData.SurfDesc.VidPnSourceId;
            else
                pCFill->Hdr.Hdr.u.u8PrimaryID = 0xff;

            cbBuffer = NEMUWDDM_DUMMY_DMABUFFER_SIZE;
        }
    }
    else
    {
        WARN(("cmd NOT IMPLEMENTED!! Flags(0x%x)", pPresent->Flags.Value));
        return STATUS_NOT_SUPPORTED;
    }

    if (paRects)
    {
        uint32_t cbMaxRects = pPresent->DmaBufferPrivateDataSize - cbPrivateData;
        UINT iStartRect = pPresent->MultipassOffset;
        UINT cMaxRects = cbMaxRects / sizeof (NEMUCMDVBVA_RECT);
        Assert(pPresent->SubRectCnt > iStartRect);
        UINT cRects = pPresent->SubRectCnt - iStartRect;
        if (cRects > cMaxRects)
        {
            pPresent->MultipassOffset += cMaxRects;
            cRects = cMaxRects;
        }
        else
            pPresent->MultipassOffset = 0;

        Assert(cRects);
        const RECT *paDstSubRects = &pPresent->pDstSubRects[iStartRect];
        NemuCVDdiPackRects(paRects, paDstSubRects, cRects);
        cbPrivateData += (cRects * sizeof (NEMUCMDVBVA_RECT));
    }

    if (fPatchSrc)
    {
        memset(pPresent->pPatchLocationListOut, 0, sizeof (D3DDDI_PATCHLOCATIONLIST));
        pPresent->pPatchLocationListOut->PatchOffset = u32SrcPatch;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_SOURCE_INDEX;
        ++pPresent->pPatchLocationListOut;
    }

    if (fPatchDst)
    {
        memset(pPresent->pPatchLocationListOut, 0, sizeof (D3DDDI_PATCHLOCATIONLIST));
        pPresent->pPatchLocationListOut->PatchOffset = u32DstPatch;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
        ++pPresent->pPatchLocationListOut;
    }

    pHdr->u8State = NEMUCMDVBVA_STATE_SUBMITTED;

    pHdr->u2.complexCmdEl.u16CbCmdHost = cbPrivateData;
    pHdr->u2.complexCmdEl.u16CbCmdGuest = cbBuffer;

    Assert(cbBuffer);
    Assert(cbPrivateData);
    Assert(cbPrivateData >= sizeof (NEMUCMDVBVA_HDR));
    pPresent->pDmaBuffer = ((uint8_t*)pPresent->pDmaBuffer) + cbBuffer;
    pPresent->pDmaBufferPrivateData = ((uint8_t*)pPresent->pDmaBufferPrivateData) + cbPrivateData;

    if (pPresent->MultipassOffset)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    return STATUS_SUCCESS;
}
#endif

/**
 * DxgkDdiPresent
 */
static NTSTATUS
APIENTRY
DxgkDdiPresentLegacy(
    CONST HANDLE  hContext,
    DXGKARG_PRESENT  *pPresent)
{
    PAGED_CODE();

//    LOGF(("ENTER, hContext(0x%x)", hContext));

    nemuVDbgBreakFv();

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)hContext;
    PNEMUWDDM_DEVICE pDevice = pContext->pDevice;
    PNEMUMP_DEVEXT pDevExt = pDevice->pAdapter;

    Assert(pPresent->DmaBufferPrivateDataSize >= sizeof (NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR));
    if (pPresent->DmaBufferPrivateDataSize < sizeof (NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR))
    {
        LOGREL(("Present->DmaBufferPrivateDataSize(%d) < sizeof NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR (%d)", pPresent->DmaBufferPrivateDataSize , sizeof (NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR)));
        /* @todo: can this actually happen? what status tu return? */
        return STATUS_INVALID_PARAMETER;
    }

    PNEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR pPrivateData = (PNEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR)pPresent->pDmaBufferPrivateData;
    pPrivateData->BaseHdr.fFlags.Value = 0;
    uint32_t cContexts2D = ASMAtomicReadU32(&pDevExt->cContexts2D);

    if (pPresent->Flags.Blt)
    {
        Assert(pPresent->Flags.Value == 1); /* only Blt is set, we do not support anything else for now */
        DXGK_ALLOCATIONLIST *pSrc =  &pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX];
        DXGK_ALLOCATIONLIST *pDst =  &pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX];
        PNEMUWDDM_ALLOCATION pSrcAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pSrc);
        if (!pSrcAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get Src Allocation info for hDeviceSpecificAllocation(0x%x)",pSrc->hDeviceSpecificAllocation));
            Status = STATUS_INVALID_HANDLE;
            goto done;
        }

        PNEMUWDDM_ALLOCATION pDstAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pDst);
        if (!pDstAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get Dst Allocation info for hDeviceSpecificAllocation(0x%x)",pDst->hDeviceSpecificAllocation));
            Status = STATUS_INVALID_HANDLE;
            goto done;
        }


        UINT cbCmd = pPresent->DmaBufferPrivateDataSize;
        pPrivateData->BaseHdr.enmCmd = NEMUVDMACMD_TYPE_DMA_PRESENT_BLT;

        PNEMUWDDM_DMA_PRIVATEDATA_BLT pBlt = (PNEMUWDDM_DMA_PRIVATEDATA_BLT)pPrivateData;

        nemuWddmPopulateDmaAllocInfo(&pBlt->Blt.SrcAlloc, pSrcAlloc, pSrc);
        nemuWddmPopulateDmaAllocInfo(&pBlt->Blt.DstAlloc, pDstAlloc, pDst);

        ASSERT_WARN(!pSrcAlloc->fRcFlags.SharedResource, ("Shared Allocatoin used in Present!"));

        pBlt->Blt.SrcRect = pPresent->SrcRect;
        pBlt->Blt.DstRects.ContextRect = pPresent->DstRect;
        pBlt->Blt.DstRects.UpdateRects.cRects = 0;
        UINT cbHead = RT_OFFSETOF(NEMUWDDM_DMA_PRIVATEDATA_BLT, Blt.DstRects.UpdateRects.aRects[0]);
        Assert(pPresent->SubRectCnt > pPresent->MultipassOffset);
        UINT cbRects = (pPresent->SubRectCnt - pPresent->MultipassOffset) * sizeof (RECT);
        pPresent->pDmaBufferPrivateData = (uint8_t*)pPresent->pDmaBufferPrivateData + cbHead + cbRects;
        pPresent->pDmaBuffer = ((uint8_t*)pPresent->pDmaBuffer) + NEMUWDDM_DUMMY_DMABUFFER_SIZE;
        Assert(pPresent->DmaSize >= NEMUWDDM_DUMMY_DMABUFFER_SIZE);
        cbCmd -= cbHead;
        Assert(cbCmd < UINT32_MAX/2);
        Assert(cbCmd > sizeof (RECT));
        if (cbCmd >= cbRects)
        {
            cbCmd -= cbRects;
            memcpy(&pBlt->Blt.DstRects.UpdateRects.aRects[pPresent->MultipassOffset], &pPresent->pDstSubRects[pPresent->MultipassOffset], cbRects);
            pBlt->Blt.DstRects.UpdateRects.cRects += cbRects/sizeof (RECT);
        }
        else
        {
            UINT cbFitingRects = (cbCmd/sizeof (RECT)) * sizeof (RECT);
            Assert(cbFitingRects);
            memcpy(&pBlt->Blt.DstRects.UpdateRects.aRects[pPresent->MultipassOffset], &pPresent->pDstSubRects[pPresent->MultipassOffset], cbFitingRects);
            cbCmd -= cbFitingRects;
            pPresent->MultipassOffset += cbFitingRects/sizeof (RECT);
            pBlt->Blt.DstRects.UpdateRects.cRects += cbFitingRects/sizeof (RECT);
            Assert(pPresent->SubRectCnt > pPresent->MultipassOffset);
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        }

        memset(pPresent->pPatchLocationListOut, 0, 2*sizeof (D3DDDI_PATCHLOCATIONLIST));
        pPresent->pPatchLocationListOut->PatchOffset = 0;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_SOURCE_INDEX;
        ++pPresent->pPatchLocationListOut;
        pPresent->pPatchLocationListOut->PatchOffset = 4;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
        ++pPresent->pPatchLocationListOut;
    }
    else if (pPresent->Flags.Flip)
    {
        Assert(pPresent->Flags.Value == 4); /* only Blt is set, we do not support anything else for now */
        Assert(pContext->enmType == NEMUWDDM_CONTEXT_TYPE_CUSTOM_3D);
        DXGK_ALLOCATIONLIST *pSrc =  &pPresent->pAllocationList[DXGK_PRESENT_SOURCE_INDEX];
        PNEMUWDDM_ALLOCATION pSrcAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pSrc);

        if (!pSrcAlloc)
        {
            /* this should not happen actually */
            WARN(("failed to get pSrc Allocation info for hDeviceSpecificAllocation(0x%x)",pSrc->hDeviceSpecificAllocation));
            Status = STATUS_INVALID_HANDLE;
            goto done;
        }

        Assert(pDevExt->cContexts3D);
        pPrivateData->BaseHdr.enmCmd = NEMUVDMACMD_TYPE_DMA_PRESENT_FLIP;
        PNEMUWDDM_DMA_PRIVATEDATA_FLIP pFlip = (PNEMUWDDM_DMA_PRIVATEDATA_FLIP)pPrivateData;

        nemuWddmPopulateDmaAllocInfo(&pFlip->Flip.Alloc, pSrcAlloc, pSrc);

        UINT cbCmd = sizeof (NEMUWDDM_DMA_PRIVATEDATA_FLIP);
        pPresent->pDmaBufferPrivateData = (uint8_t*)pPresent->pDmaBufferPrivateData + cbCmd;
        pPresent->pDmaBuffer = ((uint8_t*)pPresent->pDmaBuffer) + NEMUWDDM_DUMMY_DMABUFFER_SIZE;
        Assert(pPresent->DmaSize >= NEMUWDDM_DUMMY_DMABUFFER_SIZE);

        memset(pPresent->pPatchLocationListOut, 0, sizeof (D3DDDI_PATCHLOCATIONLIST));
        pPresent->pPatchLocationListOut->PatchOffset = 0;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_SOURCE_INDEX;
        ++pPresent->pPatchLocationListOut;
    }
    else if (pPresent->Flags.ColorFill)
    {
        Assert(pContext->enmType == NEMUWDDM_CONTEXT_TYPE_CUSTOM_2D);
        Assert(pPresent->Flags.Value == 2); /* only ColorFill is set, we do not support anything else for now */
        DXGK_ALLOCATIONLIST *pDst =  &pPresent->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX];
        PNEMUWDDM_ALLOCATION pDstAlloc = nemuWddmGetAllocationFromAllocList(pDevExt, pDst);
        if (!pDstAlloc)
        {

            /* this should not happen actually */
            WARN(("failed to get pDst Allocation info for hDeviceSpecificAllocation(0x%x)",pDst->hDeviceSpecificAllocation));
            Status = STATUS_INVALID_HANDLE;
            goto done;
        }

        UINT cbCmd = pPresent->DmaBufferPrivateDataSize;
        pPrivateData->BaseHdr.enmCmd = NEMUVDMACMD_TYPE_DMA_PRESENT_CLRFILL;
        PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL pCF = (PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL)pPrivateData;

        nemuWddmPopulateDmaAllocInfo(&pCF->ClrFill.Alloc, pDstAlloc, pDst);

        pCF->ClrFill.Color = pPresent->Color;
        pCF->ClrFill.Rects.cRects = 0;
        UINT cbHead = RT_OFFSETOF(NEMUWDDM_DMA_PRIVATEDATA_CLRFILL, ClrFill.Rects.aRects[0]);
        Assert(pPresent->SubRectCnt > pPresent->MultipassOffset);
        UINT cbRects = (pPresent->SubRectCnt - pPresent->MultipassOffset) * sizeof (RECT);
        pPresent->pDmaBufferPrivateData = (uint8_t*)pPresent->pDmaBufferPrivateData + cbHead + cbRects;
        pPresent->pDmaBuffer = ((uint8_t*)pPresent->pDmaBuffer) + NEMUWDDM_DUMMY_DMABUFFER_SIZE;
        Assert(pPresent->DmaSize >= NEMUWDDM_DUMMY_DMABUFFER_SIZE);
        cbCmd -= cbHead;
        Assert(cbCmd < UINT32_MAX/2);
        Assert(cbCmd > sizeof (RECT));
        if (cbCmd >= cbRects)
        {
            cbCmd -= cbRects;
            memcpy(&pCF->ClrFill.Rects.aRects[pPresent->MultipassOffset], pPresent->pDstSubRects, cbRects);
            pCF->ClrFill.Rects.cRects += cbRects/sizeof (RECT);
        }
        else
        {
            UINT cbFitingRects = (cbCmd/sizeof (RECT)) * sizeof (RECT);
            Assert(cbFitingRects);
            memcpy(&pCF->ClrFill.Rects.aRects[pPresent->MultipassOffset], pPresent->pDstSubRects, cbFitingRects);
            cbCmd -= cbFitingRects;
            pPresent->MultipassOffset += cbFitingRects/sizeof (RECT);
            pCF->ClrFill.Rects.cRects += cbFitingRects/sizeof (RECT);
            Assert(pPresent->SubRectCnt > pPresent->MultipassOffset);
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        }

        memset(pPresent->pPatchLocationListOut, 0, sizeof (D3DDDI_PATCHLOCATIONLIST));
        pPresent->pPatchLocationListOut->PatchOffset = 0;
        pPresent->pPatchLocationListOut->AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
        ++pPresent->pPatchLocationListOut;
    }
    else
    {
        WARN(("cmd NOT IMPLEMENTED!! Flags(0x%x)", pPresent->Flags.Value));
        Status = STATUS_NOT_SUPPORTED;
    }

done:
//    LOGF(("LEAVE, hContext(0x%x), Status(0x%x)", hContext, Status));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiUpdateOverlay(
    CONST HANDLE  hOverlay,
    CONST DXGKARG_UPDATEOVERLAY  *pUpdateOverlay)
{
    LOGF(("ENTER, hOverlay(0x%p)", hOverlay));

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_OVERLAY pOverlay = (PNEMUWDDM_OVERLAY)hOverlay;
    Assert(pOverlay);
    int rc = nemuVhwaHlpOverlayUpdate(pOverlay, &pUpdateOverlay->OverlayInfo);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        Status = STATUS_UNSUCCESSFUL;

    LOGF(("LEAVE, hOverlay(0x%p)", hOverlay));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiFlipOverlay(
    CONST HANDLE  hOverlay,
    CONST DXGKARG_FLIPOVERLAY  *pFlipOverlay)
{
    LOGF(("ENTER, hOverlay(0x%p)", hOverlay));

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_OVERLAY pOverlay = (PNEMUWDDM_OVERLAY)hOverlay;
    Assert(pOverlay);
    int rc = nemuVhwaHlpOverlayFlip(pOverlay, pFlipOverlay);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        Status = STATUS_UNSUCCESSFUL;

    LOGF(("LEAVE, hOverlay(0x%p)", hOverlay));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiDestroyOverlay(
    CONST HANDLE  hOverlay)
{
    LOGF(("ENTER, hOverlay(0x%p)", hOverlay));

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_OVERLAY pOverlay = (PNEMUWDDM_OVERLAY)hOverlay;
    Assert(pOverlay);
    int rc = nemuVhwaHlpOverlayDestroy(pOverlay);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
        nemuWddmMemFree(pOverlay);
    else
        Status = STATUS_UNSUCCESSFUL;

    LOGF(("LEAVE, hOverlay(0x%p)", hOverlay));

    return Status;
}

/**
 * DxgkDdiCreateContext
 */
NTSTATUS
APIENTRY
DxgkDdiCreateContext(
    CONST HANDLE  hDevice,
    DXGKARG_CREATECONTEXT  *pCreateContext)
{
    /* DxgkDdiCreateContext should be made pageable */
    PAGED_CODE();

    LOGF(("ENTER, hDevice(0x%x)", hDevice));

    nemuVDbgBreakFv();

    if (pCreateContext->NodeOrdinal >= NEMUWDDM_NUM_NODES)
    {
        WARN(("Invalid NodeOrdinal (%d), expected to be less that (%d)\n", pCreateContext->NodeOrdinal, NEMUWDDM_NUM_NODES));
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUWDDM_DEVICE pDevice = (PNEMUWDDM_DEVICE)hDevice;
    PNEMUMP_DEVEXT pDevExt = pDevice->pAdapter;
    PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)nemuWddmMemAllocZero(sizeof (NEMUWDDM_CONTEXT));
    Assert(pContext);
    if (pContext)
    {
        pContext->pDevice = pDevice;
        pContext->hContext = pCreateContext->hContext;
        pContext->EngineAffinity = pCreateContext->EngineAffinity;
        pContext->NodeOrdinal = pCreateContext->NodeOrdinal;
        nemuVideoCmCtxInitEmpty(&pContext->CmContext);
        if (pCreateContext->Flags.SystemContext || pCreateContext->PrivateDriverDataSize == 0)
        {
            Assert(pCreateContext->PrivateDriverDataSize == 0);
            Assert(!pCreateContext->pPrivateDriverData);
            Assert(pCreateContext->Flags.Value <= 2); /* 2 is a GDI context in Win7 */
            pContext->enmType = NEMUWDDM_CONTEXT_TYPE_SYSTEM;
            for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
            {
                nemuWddmDisplaySettingsCheckPos(pDevExt, i);
            }

#ifdef NEMU_WITH_CROGL
            if (!NEMUWDDM_IS_DISPLAYONLY() && pDevExt->f3DEnabled)
            {
                NemuMpCrPackerInit(&pContext->CrPacker);
                int rc = NemuMpCrCtlConConnect(pDevExt, &pDevExt->CrCtlCon, CR_PROTOCOL_VERSION_MAJOR, CR_PROTOCOL_VERSION_MINOR, &pContext->u32CrConClientID);
                if (!RT_SUCCESS(rc))
                    WARN(("NemuMpCrCtlConConnect failed rc (%d), ignoring for system context", rc));
            }
#endif
            Status = STATUS_SUCCESS;
        }
        else
        {
            Assert(pCreateContext->Flags.Value == 0);
            Assert(pCreateContext->PrivateDriverDataSize == sizeof (NEMUWDDM_CREATECONTEXT_INFO));
            Assert(pCreateContext->pPrivateDriverData);
            if (pCreateContext->PrivateDriverDataSize == sizeof (NEMUWDDM_CREATECONTEXT_INFO))
            {
                PNEMUWDDM_CREATECONTEXT_INFO pInfo = (PNEMUWDDM_CREATECONTEXT_INFO)pCreateContext->pPrivateDriverData;
                switch (pInfo->enmType)
                {
#ifdef NEMU_WITH_CROGL
                    case NEMUWDDM_CONTEXT_TYPE_CUSTOM_3D:
                    {
                        if (!pDevExt->fCmdVbvaEnabled)
                        {
                            Status = nemuVideoAMgrCtxCreate(&pDevExt->AllocMgr, &pContext->AllocContext);
                            if (!NT_SUCCESS(Status))
                                WARN(("nemuVideoAMgrCtxCreate failed %#x", Status));
                        }
                        else
                            Status = STATUS_SUCCESS;

                        if (Status == STATUS_SUCCESS)
                        {
                            Status = nemuWddmSwapchainCtxInit(pDevExt, pContext);
                            Assert(Status == STATUS_SUCCESS);
                            if (Status == STATUS_SUCCESS)
                            {
                                pContext->enmType = NEMUWDDM_CONTEXT_TYPE_CUSTOM_3D;
                                Status = nemuVideoCmCtxAdd(&pDevice->pAdapter->CmMgr, &pContext->CmContext, (HANDLE)pInfo->hUmEvent, pInfo->u64UmInfo);
                                Assert(Status == STATUS_SUCCESS);
                                if (Status == STATUS_SUCCESS)
                                {
                                    if (pInfo->crVersionMajor || pInfo->crVersionMinor)
                                    {
                                        if (pDevExt->f3DEnabled)
                                        {
                                            int rc = NemuMpCrCtlConConnect(pDevExt, &pDevExt->CrCtlCon,
                                                pInfo->crVersionMajor, pInfo->crVersionMinor,
                                                &pContext->u32CrConClientID);
                                            if (RT_SUCCESS(rc))
                                            {
                                                NemuMpCrPackerInit(&pContext->CrPacker);
                                            }
                                            else
                                            {
                                                WARN(("NemuMpCrCtlConConnect failed rc (%d)", rc));
                                                Status = STATUS_UNSUCCESSFUL;
                                            }
                                        }
                                        else
                                        {
                                            LOG(("3D Not Enabled, failing 3D context creation"));
                                            Status = STATUS_UNSUCCESSFUL;
                                        }
                                    }

                                    if (NT_SUCCESS(Status))
                                    {
                                        ASMAtomicIncU32(&pDevExt->cContexts3D);
                                        break;
                                    }
                                }

                                nemuWddmSwapchainCtxTerm(pDevExt, pContext);
                            }
                            nemuVideoAMgrCtxDestroy(&pContext->AllocContext);
                        }
                        break;
                    }
                    case NEMUWDDM_CONTEXT_TYPE_CUSTOM_UHGSMI_3D:
                    case NEMUWDDM_CONTEXT_TYPE_CUSTOM_UHGSMI_GL:
                    {
                        pContext->enmType = pInfo->enmType;
                        if (!pDevExt->fCmdVbvaEnabled)
                        {
                            Status = nemuVideoAMgrCtxCreate(&pDevExt->AllocMgr, &pContext->AllocContext);
                            if (!NT_SUCCESS(Status))
                                WARN(("nemuVideoAMgrCtxCreate failed %#x", Status));
                        }
                        else
                            Status = STATUS_SUCCESS;

                        if (Status == STATUS_SUCCESS)
                        {
                            if (pInfo->crVersionMajor || pInfo->crVersionMinor)
                            {
                                if (pDevExt->f3DEnabled)
                                {
                                    int rc = NemuMpCrCtlConConnect(pDevExt, &pDevExt->CrCtlCon,
                                        pInfo->crVersionMajor, pInfo->crVersionMinor,
                                        &pContext->u32CrConClientID);
                                    if (!RT_SUCCESS(rc))
                                    {
                                        WARN(("NemuMpCrCtlConConnect failed rc (%d)", rc));
                                        Status = STATUS_UNSUCCESSFUL;
                                    }
                                }
                                else
                                {
                                    LOG(("3D Not Enabled, failing 3D (hgsmi) context creation"));
                                    Status = STATUS_UNSUCCESSFUL;
                                }
                            }

                            if (NT_SUCCESS(Status))
                            {
                                ASMAtomicIncU32(&pDevExt->cContexts3D);
                                break;
                            }
                            nemuVideoAMgrCtxDestroy(&pContext->AllocContext);
                        }
                        break;
                    }
#endif
                    case NEMUWDDM_CONTEXT_TYPE_CUSTOM_2D:
                    {
                        pContext->enmType = pInfo->enmType;
                        ASMAtomicIncU32(&pDevExt->cContexts2D);
                        break;
                    }
                    case NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE:
                    {
                        pContext->enmType = pInfo->enmType;
                        ASMAtomicIncU32(&pDevExt->cContextsDispIfResize);
                        break;
                    }
                    case NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS:
                    {
                        pContext->enmType = pInfo->enmType;
                        Status = nemuVideoCmCtxAdd(&pDevice->pAdapter->SeamlessCtxMgr, &pContext->CmContext, (HANDLE)pInfo->hUmEvent, pInfo->u64UmInfo);
                        if (!NT_SUCCESS(Status))
                        {
                            WARN(("nemuVideoCmCtxAdd failed, Status 0x%x", Status));
                        }
                        break;
                    }
                    default:
                    {
                        WARN(("unsupported context type %d", pInfo->enmType));
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
            }
        }

        if (Status == STATUS_SUCCESS)
        {
            pCreateContext->hContext = pContext;
            pCreateContext->ContextInfo.DmaBufferSize = NEMUWDDM_C_DMA_BUFFER_SIZE;
            pCreateContext->ContextInfo.DmaBufferSegmentSet = 0;
            pCreateContext->ContextInfo.DmaBufferPrivateDataSize = NEMUWDDM_C_DMA_PRIVATEDATA_SIZE;
            pCreateContext->ContextInfo.AllocationListSize = NEMUWDDM_C_ALLOC_LIST_SIZE;
            pCreateContext->ContextInfo.PatchLocationListSize = NEMUWDDM_C_PATH_LOCATION_LIST_SIZE;
        //#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
        //# error port to Win7 DDI
        //    //pCreateContext->ContextInfo.DmaBufferAllocationGroup = ???;
        //#endif // DXGKDDI_INTERFACE_VERSION
        }
        else
            nemuWddmMemFree(pContext);
    }
    else
        Status = STATUS_NO_MEMORY;

    LOGF(("LEAVE, hDevice(0x%x)", hDevice));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiDestroyContext(
    CONST HANDLE  hContext)
{
    LOGF(("ENTER, hContext(0x%x)", hContext));
    nemuVDbgBreakFv();
    PNEMUWDDM_CONTEXT pContext = (PNEMUWDDM_CONTEXT)hContext;
    PNEMUMP_DEVEXT pDevExt = pContext->pDevice->pAdapter;
    NTSTATUS Status = STATUS_SUCCESS;

    switch(pContext->enmType)
    {
        case NEMUWDDM_CONTEXT_TYPE_CUSTOM_3D:
        case NEMUWDDM_CONTEXT_TYPE_CUSTOM_UHGSMI_3D:
        case NEMUWDDM_CONTEXT_TYPE_CUSTOM_UHGSMI_GL:
        {
            uint32_t cContexts = ASMAtomicDecU32(&pDevExt->cContexts3D);
            Assert(cContexts < UINT32_MAX/2);
            break;
        }
        case NEMUWDDM_CONTEXT_TYPE_CUSTOM_2D:
        {
            uint32_t cContexts = ASMAtomicDecU32(&pDevExt->cContexts2D);
            Assert(cContexts < UINT32_MAX/2);
            break;
        }
        case NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE:
        {
            uint32_t cContexts = ASMAtomicDecU32(&pDevExt->cContextsDispIfResize);
            Assert(cContexts < UINT32_MAX/2);
            if (!cContexts)
            {
                if (pDevExt->fDisableTargetUpdate)
                {
                    pDevExt->fDisableTargetUpdate = FALSE;
                    nemuWddmGhDisplayCheckSetInfoEx(pDevExt, true);
                }
            }
            break;
        }
        case NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS:
        {
            Status = nemuVideoCmCtxRemove(&pContext->pDevice->pAdapter->SeamlessCtxMgr, &pContext->CmContext);
            if (!NT_SUCCESS(Status))
                WARN(("nemuVideoCmCtxRemove failed, Status 0x%x", Status));

            Assert(pContext->CmContext.pSession == NULL);
            break;
        }
        default:
            break;
    }

#ifdef NEMU_WITH_CROGL
    if (pContext->u32CrConClientID)
    {
        NemuMpCrCtlConDisconnect(pDevExt, &pDevExt->CrCtlCon, pContext->u32CrConClientID);
    }
#endif

#ifdef NEMU_WITH_CROGL
    /* first terminate the swapchain, this will also ensure
     * all currently pending driver->user Cm commands
     * (i.e. visible regions commands) are completed */
    nemuWddmSwapchainCtxTerm(pDevExt, pContext);
#endif

    Status = nemuVideoAMgrCtxDestroy(&pContext->AllocContext);
    if (NT_SUCCESS(Status))
    {
        Status = nemuVideoCmCtxRemove(&pContext->pDevice->pAdapter->CmMgr, &pContext->CmContext);
        if (NT_SUCCESS(Status))
            nemuWddmMemFree(pContext);
        else
            WARN(("nemuVideoCmCtxRemove failed, Status 0x%x", Status));
    }
    else
        WARN(("nemuVideoAMgrCtxDestroy failed, Status 0x%x", Status));

    LOGF(("LEAVE, hContext(0x%x)", hContext));

    return Status;
}

NTSTATUS
APIENTRY
DxgkDdiLinkDevice(
    __in CONST PDEVICE_OBJECT  PhysicalDeviceObject,
    __in CONST PVOID  MiniportDeviceContext,
    __inout PLINKED_DEVICE  LinkedDevice
    )
{
    LOGF(("ENTER, MiniportDeviceContext(0x%x)", MiniportDeviceContext));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, MiniportDeviceContext(0x%x)", MiniportDeviceContext));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
DxgkDdiSetDisplayPrivateDriverFormat(
    CONST HANDLE  hAdapter,
    /*CONST*/ DXGKARG_SETDISPLAYPRIVATEDRIVERFORMAT*  pSetDisplayPrivateDriverFormat
    )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY CALLBACK DxgkDdiRestartFromTimeout(IN_CONST_HANDLE hAdapter)
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));
    return STATUS_SUCCESS;
}

#ifdef NEMU_WDDM_WIN8

static NTSTATUS APIENTRY DxgkDdiQueryVidPnHWCapability(
        __in     const HANDLE hAdapter,
        __inout  DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps
      )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));
    nemuVDbgBreakF();
    pVidPnHWCaps->VidPnHWCaps.DriverRotation = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverScaling = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverCloning = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay = 0;
    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY DxgkDdiPresentDisplayOnly(
        _In_  const HANDLE hAdapter,
        _In_  const DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly
      )
{
    LOGF(("ENTER, hAdapter(0x%x)", hAdapter));
    nemuVDbgBreakFv();

    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)hAdapter;
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pPresentDisplayOnly->VidPnSourceId];
    Assert(pSource->AllocData.Addr.SegmentId == 1);
    NEMUWDDM_ALLOC_DATA SrcAllocData;
    SrcAllocData.SurfDesc.width = pPresentDisplayOnly->Pitch * pPresentDisplayOnly->BytesPerPixel;
    SrcAllocData.SurfDesc.height = ~0UL;
    switch (pPresentDisplayOnly->BytesPerPixel)
    {
        case 4:
            SrcAllocData.SurfDesc.format = D3DDDIFMT_A8R8G8B8;
            break;
        case 3:
            SrcAllocData.SurfDesc.format = D3DDDIFMT_R8G8B8;
            break;
        case 2:
            SrcAllocData.SurfDesc.format = D3DDDIFMT_R5G6B5;
            break;
        case 1:
            SrcAllocData.SurfDesc.format = D3DDDIFMT_P8;
            break;
        default:
            WARN(("Unknown format"));
            SrcAllocData.SurfDesc.format = D3DDDIFMT_UNKNOWN;
            break;
    }
    SrcAllocData.SurfDesc.bpp = pPresentDisplayOnly->BytesPerPixel >> 3;
    SrcAllocData.SurfDesc.pitch = pPresentDisplayOnly->Pitch;
    SrcAllocData.SurfDesc.depth = 1;
    SrcAllocData.SurfDesc.slicePitch = pPresentDisplayOnly->Pitch;
    SrcAllocData.SurfDesc.cbSize =  ~0UL;
    SrcAllocData.Addr.SegmentId = 0;
    SrcAllocData.Addr.pvMem = pPresentDisplayOnly->pSource;
    SrcAllocData.hostID = 0;
    SrcAllocData.pSwapchain = NULL;

    RECT UpdateRect;
    BOOLEAN bUpdateRectInited = FALSE;

    for (UINT i = 0; i < pPresentDisplayOnly->NumMoves; ++i)
    {
        if (!bUpdateRectInited)
        {
            UpdateRect = pPresentDisplayOnly->pMoves[i].DestRect;
            bUpdateRectInited = TRUE;
        }
        else
            nemuWddmRectUnite(&UpdateRect, &pPresentDisplayOnly->pMoves[i].DestRect);
        nemuVdmaGgDmaBltPerform(pDevExt, &SrcAllocData, &pPresentDisplayOnly->pMoves[i].DestRect, &pSource->AllocData, &pPresentDisplayOnly->pMoves[i].DestRect);
    }

    for (UINT i = 0; i < pPresentDisplayOnly->NumDirtyRects; ++i)
    {
        nemuVdmaGgDmaBltPerform(pDevExt, &SrcAllocData, &pPresentDisplayOnly->pDirtyRect[i], &pSource->AllocData, &pPresentDisplayOnly->pDirtyRect[i]);
        if (!bUpdateRectInited)
        {
            UpdateRect = pPresentDisplayOnly->pDirtyRect[i];
            bUpdateRectInited = TRUE;
        }
        else
            nemuWddmRectUnite(&UpdateRect, &pPresentDisplayOnly->pDirtyRect[i]);
    }

    if (bUpdateRectInited && pSource->bVisible)
    {
        NEMUVBVA_OP_WITHLOCK(ReportDirtyRect, pDevExt, pSource, &UpdateRect);
    }

    LOGF(("LEAVE, hAdapter(0x%x)", hAdapter));
    return STATUS_SUCCESS;
}

static NTSTATUS DxgkDdiStopDeviceAndReleasePostDisplayOwnership(
  _In_   PVOID MiniportDeviceContext,
  _In_   D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
  _Out_  PDXGK_DISPLAY_INFORMATION DisplayInfo
)
{
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS DxgkDdiSystemDisplayEnable(
        _In_   PVOID MiniportDeviceContext,
        _In_   D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
        _In_   PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
        _Out_  UINT *Width,
        _Out_  UINT *Height,
        _Out_  D3DDDIFORMAT *ColorFormat
      )
{
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
    return STATUS_NOT_SUPPORTED;
}

static VOID DxgkDdiSystemDisplayWrite(
  _In_  PVOID MiniportDeviceContext,
  _In_  PVOID Source,
  _In_  UINT SourceWidth,
  _In_  UINT SourceHeight,
  _In_  UINT SourceStride,
  _In_  UINT PositionX,
  _In_  UINT PositionY
)
{
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
}

static NTSTATUS DxgkDdiGetChildContainerId(
  _In_     PVOID MiniportDeviceContext,
  _In_     ULONG ChildUid,
  _Inout_  PDXGK_CHILD_CONTAINER_ID ContainerId
)
{
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY DxgkDdiSetPowerComponentFState(
  _In_  const HANDLE DriverContext,
  UINT ComponentIndex,
  UINT FState
)
{
    LOGF(("ENTER, DriverContext(0x%x)", DriverContext));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, DriverContext(0x%x)", DriverContext));
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY DxgkDdiPowerRuntimeControlRequest(
  _In_       const HANDLE DriverContext,
  _In_       LPCGUID PowerControlCode,
  _In_opt_   PVOID InBuffer,
  _In_       SIZE_T InBufferSize,
  _Out_opt_  PVOID OutBuffer,
  _In_       SIZE_T OutBufferSize,
  _Out_opt_  PSIZE_T BytesReturned
)
{
    LOGF(("ENTER, DriverContext(0x%x)", DriverContext));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, DriverContext(0x%x)", DriverContext));
    return STATUS_SUCCESS;
}

static NTSTATUS DxgkDdiNotifySurpriseRemoval(
        _In_  PVOID MiniportDeviceContext,
        _In_  DXGK_SURPRISE_REMOVAL_TYPE RemovalType
      )
{
    LOGF(("ENTER, hAdapter(0x%x)", MiniportDeviceContext));
    nemuVDbgBreakFv();
    AssertBreakpoint();
    LOGF(("LEAVE, hAdapter(0x%x)", MiniportDeviceContext));
    return STATUS_SUCCESS;
}

static NTSTATUS nemuWddmInitDisplayOnlyDriver(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath)
{
    KMDDOD_INITIALIZATION_DATA DriverInitializationData = {'\0'};

    DriverInitializationData.Version = DXGKDDI_INTERFACE_VERSION_WIN8;

    DriverInitializationData.DxgkDdiAddDevice = DxgkDdiAddDevice;
    DriverInitializationData.DxgkDdiStartDevice = DxgkDdiStartDevice;
    DriverInitializationData.DxgkDdiStopDevice = DxgkDdiStopDevice;
    DriverInitializationData.DxgkDdiRemoveDevice = DxgkDdiRemoveDevice;
    DriverInitializationData.DxgkDdiDispatchIoRequest = DxgkDdiDispatchIoRequest;
    DriverInitializationData.DxgkDdiInterruptRoutine = DxgkDdiInterruptRoutineLegacy;
    DriverInitializationData.DxgkDdiDpcRoutine = DxgkDdiDpcRoutineLegacy;
    DriverInitializationData.DxgkDdiQueryChildRelations = DxgkDdiQueryChildRelations;
    DriverInitializationData.DxgkDdiQueryChildStatus = DxgkDdiQueryChildStatus;
    DriverInitializationData.DxgkDdiQueryDeviceDescriptor = DxgkDdiQueryDeviceDescriptor;
    DriverInitializationData.DxgkDdiSetPowerState = DxgkDdiSetPowerState;
    DriverInitializationData.DxgkDdiNotifyAcpiEvent = DxgkDdiNotifyAcpiEvent;
    DriverInitializationData.DxgkDdiResetDevice = DxgkDdiResetDevice;
    DriverInitializationData.DxgkDdiUnload = DxgkDdiUnload;
    DriverInitializationData.DxgkDdiQueryInterface = DxgkDdiQueryInterface;
    DriverInitializationData.DxgkDdiControlEtwLogging = DxgkDdiControlEtwLogging;
    DriverInitializationData.DxgkDdiQueryAdapterInfo = DxgkDdiQueryAdapterInfo;
    DriverInitializationData.DxgkDdiSetPalette = DxgkDdiSetPalette;
    DriverInitializationData.DxgkDdiSetPointerPosition = DxgkDdiSetPointerPosition;
    DriverInitializationData.DxgkDdiSetPointerShape = DxgkDdiSetPointerShape;
    DriverInitializationData.DxgkDdiEscape = DxgkDdiEscape;
    DriverInitializationData.DxgkDdiCollectDbgInfo = DxgkDdiCollectDbgInfo;
    DriverInitializationData.DxgkDdiIsSupportedVidPn = DxgkDdiIsSupportedVidPn;
    DriverInitializationData.DxgkDdiRecommendFunctionalVidPn = DxgkDdiRecommendFunctionalVidPn;
    DriverInitializationData.DxgkDdiEnumVidPnCofuncModality = DxgkDdiEnumVidPnCofuncModality;
    DriverInitializationData.DxgkDdiSetVidPnSourceVisibility = DxgkDdiSetVidPnSourceVisibility;
    DriverInitializationData.DxgkDdiCommitVidPn = DxgkDdiCommitVidPn;
    DriverInitializationData.DxgkDdiUpdateActiveVidPnPresentPath = DxgkDdiUpdateActiveVidPnPresentPath;
    DriverInitializationData.DxgkDdiRecommendMonitorModes = DxgkDdiRecommendMonitorModes;
    DriverInitializationData.DxgkDdiGetScanLine = DxgkDdiGetScanLine;
    DriverInitializationData.DxgkDdiQueryVidPnHWCapability = DxgkDdiQueryVidPnHWCapability;
    DriverInitializationData.DxgkDdiPresentDisplayOnly = DxgkDdiPresentDisplayOnly;
    DriverInitializationData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    DriverInitializationData.DxgkDdiSystemDisplayEnable = DxgkDdiSystemDisplayEnable;
    DriverInitializationData.DxgkDdiSystemDisplayWrite = DxgkDdiSystemDisplayWrite;
//    DriverInitializationData.DxgkDdiGetChildContainerId = DxgkDdiGetChildContainerId;
    DriverInitializationData.DxgkDdiControlInterrupt = DxgkDdiControlInterrupt;
//    DriverInitializationData.DxgkDdiSetPowerComponentFState = DxgkDdiSetPowerComponentFState;
//    DriverInitializationData.DxgkDdiPowerRuntimeControlRequest = DxgkDdiPowerRuntimeControlRequest;
//    DriverInitializationData.DxgkDdiNotifySurpriseRemoval = DxgkDdiNotifySurpriseRemoval;

    NTSTATUS Status = DxgkInitializeDisplayOnlyDriver(pDriverObject,
                          pRegistryPath,
                          &DriverInitializationData);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkInitializeDisplayOnlyDriver failed! Status 0x%x", Status));
    }
    return Status;
}
#endif

static NTSTATUS nemuWddmInitFullGraphicsDriver(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath, BOOLEAN fCmdVbva)
{
#ifdef NEMU_WITH_CROGL
#define NEMUWDDM_CALLBACK_NAME(_base, _fCmdVbva) ((_fCmdVbva) ? _base##New : _base##Legacy)
#else
#define NEMUWDDM_CALLBACK_NAME(_base, _fCmdVbva) (_base##Legacy)
#endif

    DRIVER_INITIALIZATION_DATA DriverInitializationData = {'\0'};

    // Fill in the DriverInitializationData structure and call DxgkInitialize()
    DriverInitializationData.Version = DXGKDDI_INTERFACE_VERSION;

    DriverInitializationData.DxgkDdiAddDevice = DxgkDdiAddDevice;
    DriverInitializationData.DxgkDdiStartDevice = DxgkDdiStartDevice;
    DriverInitializationData.DxgkDdiStopDevice = DxgkDdiStopDevice;
    DriverInitializationData.DxgkDdiRemoveDevice = DxgkDdiRemoveDevice;
    DriverInitializationData.DxgkDdiDispatchIoRequest = DxgkDdiDispatchIoRequest;
    DriverInitializationData.DxgkDdiInterruptRoutine = NEMUWDDM_CALLBACK_NAME(DxgkDdiInterruptRoutine, fCmdVbva);
    DriverInitializationData.DxgkDdiDpcRoutine = NEMUWDDM_CALLBACK_NAME(DxgkDdiDpcRoutine, fCmdVbva);
    DriverInitializationData.DxgkDdiQueryChildRelations = DxgkDdiQueryChildRelations;
    DriverInitializationData.DxgkDdiQueryChildStatus = DxgkDdiQueryChildStatus;
    DriverInitializationData.DxgkDdiQueryDeviceDescriptor = DxgkDdiQueryDeviceDescriptor;
    DriverInitializationData.DxgkDdiSetPowerState = DxgkDdiSetPowerState;
    DriverInitializationData.DxgkDdiNotifyAcpiEvent = DxgkDdiNotifyAcpiEvent;
    DriverInitializationData.DxgkDdiResetDevice = DxgkDdiResetDevice;
    DriverInitializationData.DxgkDdiUnload = DxgkDdiUnload;
    DriverInitializationData.DxgkDdiQueryInterface = DxgkDdiQueryInterface;
    DriverInitializationData.DxgkDdiControlEtwLogging = DxgkDdiControlEtwLogging;

    DriverInitializationData.DxgkDdiQueryAdapterInfo = DxgkDdiQueryAdapterInfo;
    DriverInitializationData.DxgkDdiCreateDevice = DxgkDdiCreateDevice;
    DriverInitializationData.DxgkDdiCreateAllocation = DxgkDdiCreateAllocation;
    DriverInitializationData.DxgkDdiDestroyAllocation = DxgkDdiDestroyAllocation;
    DriverInitializationData.DxgkDdiDescribeAllocation = DxgkDdiDescribeAllocation;
    DriverInitializationData.DxgkDdiGetStandardAllocationDriverData = DxgkDdiGetStandardAllocationDriverData;
    DriverInitializationData.DxgkDdiAcquireSwizzlingRange = DxgkDdiAcquireSwizzlingRange;
    DriverInitializationData.DxgkDdiReleaseSwizzlingRange = DxgkDdiReleaseSwizzlingRange;
    DriverInitializationData.DxgkDdiPatch = NEMUWDDM_CALLBACK_NAME(DxgkDdiPatch, fCmdVbva);
    DriverInitializationData.DxgkDdiSubmitCommand = NEMUWDDM_CALLBACK_NAME(DxgkDdiSubmitCommand, fCmdVbva);
    DriverInitializationData.DxgkDdiPreemptCommand = NEMUWDDM_CALLBACK_NAME(DxgkDdiPreemptCommand, fCmdVbva);
    DriverInitializationData.DxgkDdiBuildPagingBuffer = NEMUWDDM_CALLBACK_NAME(DxgkDdiBuildPagingBuffer, fCmdVbva);
    DriverInitializationData.DxgkDdiSetPalette = DxgkDdiSetPalette;
    DriverInitializationData.DxgkDdiSetPointerPosition = DxgkDdiSetPointerPosition;
    DriverInitializationData.DxgkDdiSetPointerShape = DxgkDdiSetPointerShape;
    DriverInitializationData.DxgkDdiResetFromTimeout = DxgkDdiResetFromTimeout;
    DriverInitializationData.DxgkDdiRestartFromTimeout = DxgkDdiRestartFromTimeout;
    DriverInitializationData.DxgkDdiEscape = DxgkDdiEscape;
    DriverInitializationData.DxgkDdiCollectDbgInfo = DxgkDdiCollectDbgInfo;
    DriverInitializationData.DxgkDdiQueryCurrentFence = NEMUWDDM_CALLBACK_NAME(DxgkDdiQueryCurrentFence, fCmdVbva);
    DriverInitializationData.DxgkDdiIsSupportedVidPn = DxgkDdiIsSupportedVidPn;
    DriverInitializationData.DxgkDdiRecommendFunctionalVidPn = DxgkDdiRecommendFunctionalVidPn;
    DriverInitializationData.DxgkDdiEnumVidPnCofuncModality = DxgkDdiEnumVidPnCofuncModality;
    DriverInitializationData.DxgkDdiSetVidPnSourceAddress = DxgkDdiSetVidPnSourceAddress;
    DriverInitializationData.DxgkDdiSetVidPnSourceVisibility = DxgkDdiSetVidPnSourceVisibility;
    DriverInitializationData.DxgkDdiCommitVidPn = DxgkDdiCommitVidPn;
    DriverInitializationData.DxgkDdiUpdateActiveVidPnPresentPath = DxgkDdiUpdateActiveVidPnPresentPath;
    DriverInitializationData.DxgkDdiRecommendMonitorModes = DxgkDdiRecommendMonitorModes;
    DriverInitializationData.DxgkDdiRecommendVidPnTopology = DxgkDdiRecommendVidPnTopology;
    DriverInitializationData.DxgkDdiGetScanLine = DxgkDdiGetScanLine;
    DriverInitializationData.DxgkDdiStopCapture = DxgkDdiStopCapture;
    DriverInitializationData.DxgkDdiControlInterrupt = DxgkDdiControlInterrupt;
    DriverInitializationData.DxgkDdiCreateOverlay = DxgkDdiCreateOverlay;

    DriverInitializationData.DxgkDdiDestroyDevice = DxgkDdiDestroyDevice;
    DriverInitializationData.DxgkDdiOpenAllocation = DxgkDdiOpenAllocation;
    DriverInitializationData.DxgkDdiCloseAllocation = DxgkDdiCloseAllocation;
    DriverInitializationData.DxgkDdiRender = NEMUWDDM_CALLBACK_NAME(DxgkDdiRender, fCmdVbva);
    DriverInitializationData.DxgkDdiPresent = NEMUWDDM_CALLBACK_NAME(DxgkDdiPresent, fCmdVbva);

    DriverInitializationData.DxgkDdiUpdateOverlay = DxgkDdiUpdateOverlay;
    DriverInitializationData.DxgkDdiFlipOverlay = DxgkDdiFlipOverlay;
    DriverInitializationData.DxgkDdiDestroyOverlay = DxgkDdiDestroyOverlay;

    DriverInitializationData.DxgkDdiCreateContext = DxgkDdiCreateContext;
    DriverInitializationData.DxgkDdiDestroyContext = DxgkDdiDestroyContext;

    DriverInitializationData.DxgkDdiLinkDevice = NULL; //DxgkDdiLinkDevice;
    DriverInitializationData.DxgkDdiSetDisplayPrivateDriverFormat = DxgkDdiSetDisplayPrivateDriverFormat;
//#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
//# error port to Win7 DDI
//            DriverInitializationData.DxgkDdiRenderKm  = DxgkDdiRenderKm;
//            DriverInitializationData.DxgkDdiRestartFromTimeout  = DxgkDdiRestartFromTimeout;
//            DriverInitializationData.DxgkDdiSetVidPnSourceVisibility  = DxgkDdiSetVidPnSourceVisibility;
//            DriverInitializationData.DxgkDdiUpdateActiveVidPnPresentPath  = DxgkDdiUpdateActiveVidPnPresentPath;
//            DriverInitializationData.DxgkDdiQueryVidPnHWCapability  = DxgkDdiQueryVidPnHWCapability;
//#endif

    NTSTATUS Status = DxgkInitialize(pDriverObject,
                          pRegistryPath,
                          &DriverInitializationData);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkInitialize failed! Status 0x%x", Status));
    }
    return Status;
}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
{
    PAGED_CODE();

    nemuVDbgBreakFv();

#if 0//def DEBUG_misha
    RTLogGroupSettings(0, "+default.e.l.f.l2.l3");
#endif

#ifdef NEMU_WDDM_WIN8
    LOGREL(("Nemu WDDM Driver for Windows 8+ version %d.%d.%dr%d, %d bit; Built %s %s",
            NEMU_VERSION_MAJOR, NEMU_VERSION_MINOR, NEMU_VERSION_BUILD, NEMU_SVN_REV,
            (sizeof (void*) << 3), __DATE__, __TIME__));
#else
    LOGREL(("Nemu WDDM Driver for Windows Vista and 7 version %d.%d.%dr%d, %d bit; Built %s %s",
            NEMU_VERSION_MAJOR, NEMU_VERSION_MINOR, NEMU_VERSION_BUILD, NEMU_SVN_REV,
            (sizeof (void*) << 3), __DATE__, __TIME__));
#endif

    if (   !ARGUMENT_PRESENT(DriverObject)
        || !ARGUMENT_PRESENT(RegistryPath))
        return STATUS_INVALID_PARAMETER;

    nemuWddmDrvCfgInit(RegistryPath);

    ULONG major, minor, build;
    BOOLEAN checkedBuild = PsGetVersion(&major, &minor, &build, NULL);
    BOOLEAN f3DRequired = FALSE;

    LOGREL(("OsVersion(%d, %d, %d)", major, minor, build));

    NTSTATUS Status = STATUS_SUCCESS;
    /* Initialize NemuGuest library, which is used for requests which go through VMMDev. */
    int rc = VbglInitClient();
    if (RT_SUCCESS(rc))
    {
        if (major > 6)
        {
            /* Windows 10 and newer. */
            f3DRequired = TRUE;
        }
        else if (major == 6)
        {
            if (minor >= 2)
            {
                /* Windows 8, 8.1 and 10 preview. */
                f3DRequired = TRUE;
            }
            else
            {
                f3DRequired = FALSE;
            }
        }
        else
        {
            WARN(("Unsupported OLDER win version, ignore and assume 3D is NOT required"));
            f3DRequired = FALSE;
        }

        LOG(("3D is %srequired!", f3DRequired? "": "NOT "));

        Status = STATUS_SUCCESS;
#ifdef NEMU_WITH_CROGL
        NemuMpCrCtlConInit();

        /* always need to do the check to request host caps */
        LOG(("Doing the 3D check.."));
        if (!NemuMpCrCtlConIs3DSupported())
#endif
        {
#ifdef NEMU_WDDM_WIN8
            Assert(f3DRequired);
            g_NemuDisplayOnly = 1;

            /* Black list some builds. */
            if (major == 6 && minor == 4 && build == 9841)
            {
                /* W10 Technical preview crashes with display-only driver. */
                LOGREL(("3D is NOT supported by the host, fallback to the system video driver."));
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                LOGREL(("3D is NOT supported by the host, falling back to display-only mode.."));
            }
#else
            if (f3DRequired)
            {
                LOGREL(("3D is NOT supported by the host, but is required for the current guest version using this driver.."));
                Status = STATUS_UNSUCCESSFUL;
            }
            else
                LOGREL(("3D is NOT supported by the host, but is NOT required for the current guest version using this driver, continuing with Disabled 3D.."));
#endif
        }

#if 0 //defined(DEBUG_misha) && defined(NEMU_WDDM_WIN8)
        /* force g_NemuDisplayOnly for debugging purposes */
        LOGREL(("Current win8 video driver only supports display-only mode no matter whether or not host 3D is enabled!"));
        g_NemuDisplayOnly = 1;
#endif

        if (NT_SUCCESS(Status))
        {
#ifdef NEMU_WITH_CROGL
            rc = NemuVrInit();
            if (RT_SUCCESS(rc))
#endif
            {
#ifdef NEMU_WDDM_WIN8
                if (g_NemuDisplayOnly)
                {
                    Status = nemuWddmInitDisplayOnlyDriver(DriverObject, RegistryPath);
                }
                else
#endif
                {
                    Status = nemuWddmInitFullGraphicsDriver(DriverObject, RegistryPath,
#ifdef NEMU_WITH_CROGL
                            !!(NemuMpCrGetHostCaps() & CR_NEMU_CAP_CMDVBVA)
#else
                            FALSE
#endif
                            );
                }

                if (NT_SUCCESS(Status))
                    return Status;
#ifdef NEMU_WITH_CROGL
                NemuVrTerm();
#endif
            }
#ifdef NEMU_WITH_CROGL
            else
            {
                WARN(("NemuVrInit failed, rc(%d)", rc));
                Status = STATUS_UNSUCCESSFUL;
            }
#endif
        }
        else
            LOGREL(("Aborting the video driver load due to 3D support missing"));

        VbglTerminate();
    }
    else
    {
        WARN(("VbglInitClient failed, rc(%d)", rc));
        Status = STATUS_UNSUCCESSFUL;
    }

    AssertRelease(!NT_SUCCESS(Status));

    PRTLOGGER pLogger = RTLogRelSetDefaultInstance(NULL);
    if (pLogger)
    {
        RTLogDestroy(pLogger);
    }
    pLogger = RTLogSetDefaultInstance(NULL);
    if (pLogger)
    {
        RTLogDestroy(pLogger);
    }

    return Status;
}

