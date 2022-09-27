/* $Id: NemuMPWddm.h $ */
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

#ifndef ___NemuMPWddm_h___
#define ___NemuMPWddm_h___

#ifdef NEMU_WDDM_WIN8
# define NEMU_WDDM_DRIVERNAME L"NemuVideoW8"
#else
# define NEMU_WDDM_DRIVERNAME L"NemuVideoWddm"
#endif

#ifndef DEBUG_misha
# ifdef Assert
#  error "NemuMPWddm.h must be included first."
# endif
# define RT_NO_STRICT
#endif
#include "common/NemuMPUtils.h"
#include "common/NemuMPDevExt.h"
#include "../../common/NemuVideoTools.h"

//#define NEMUWDDM_DEBUG_VIDPN

#define NEMUWDDM_CFG_DRV_DEFAULT                        0
#define NEMUWDDM_CFG_DRV_SECONDARY_TARGETS_CONNECTED    1

#define NEMUWDDM_CFG_DRVTARGET_CONNECTED                1

#define NEMUWDDM_CFG_LOG_UM_BACKDOOR 0x00000001
#define NEMUWDDM_CFG_LOG_UM_DBGPRINT 0x00000002
#define NEMUWDDM_CFG_STR_LOG_UM L"NemuLogUm"

#define NEMUWDDM_REG_DRV_FLAGS_NAME L"NemuFlags"
#define NEMUWDDM_REG_DRV_DISPFLAGS_PREFIX L"NemuDispFlags"

#define NEMUWDDM_REG_DRVKEY_PREFIX L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\"

#define NEMUWDDM_REG_DISPLAYSETTINGSVIDEOKEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\"
#define NEMUWDDM_REG_DISPLAYSETTINGSVIDEOKEY_SUBKEY L"\\Video"


#define NEMUWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_VISTA L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\Current\\System\\CurrentControlSet\\Control\\VIDEO\\"
#define NEMUWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_WIN7 L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\UnitedVideo\\CONTROL\\VIDEO\\"

#define NEMUWDDM_REG_DISPLAYSETTINGS_ATTACH_RELX L"Attach.RelativeX"
#define NEMUWDDM_REG_DISPLAYSETTINGS_ATTACH_RELY L"Attach.RelativeY"
#define NEMUWDDM_REG_DISPLAYSETTINGS_ATTACH_DESKTOP L"Attach.ToDesktop"

extern DWORD g_NemuLogUm;

RT_C_DECLS_BEGIN
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath);
RT_C_DECLS_END

PVOID nemuWddmMemAlloc(IN SIZE_T cbSize);
PVOID nemuWddmMemAllocZero(IN SIZE_T cbSize);
VOID nemuWddmMemFree(PVOID pvMem);

NTSTATUS nemuWddmCallIsr(PNEMUMP_DEVEXT pDevExt);

DECLINLINE(PNEMUWDDM_RESOURCE) nemuWddmResourceForAlloc(PNEMUWDDM_ALLOCATION pAlloc)
{
#if 0
    if(pAlloc->iIndex == NEMUWDDM_ALLOCATIONINDEX_VOID)
        return NULL;
    PNEMUWDDM_RESOURCE pRc = (PNEMUWDDM_RESOURCE)(((uint8_t*)pAlloc) - RT_OFFSETOF(NEMUWDDM_RESOURCE, aAllocations[pAlloc->iIndex]));
    return pRc;
#else
    return pAlloc->pResource;
#endif
}

VOID nemuWddmAllocationDestroy(PNEMUWDDM_ALLOCATION pAllocation);

DECLINLINE(BOOLEAN) nemuWddmAddrSetVram(PNEMUWDDM_ADDR pAddr, UINT SegmentId, NEMUVIDEOOFFSET offVram)
{
    if (pAddr->SegmentId == SegmentId && pAddr->offVram == offVram)
        return FALSE;

    pAddr->SegmentId = SegmentId;
    pAddr->offVram = offVram;
    return TRUE;
}

DECLINLINE(bool) nemuWddmAddrVramEqual(const NEMUWDDM_ADDR *pAddr1, const NEMUWDDM_ADDR *pAddr2)
{
    return pAddr1->SegmentId == pAddr2->SegmentId && pAddr1->offVram == pAddr2->offVram;
}

DECLINLINE(NEMUVIDEOOFFSET) nemuWddmVramAddrToOffset(PNEMUMP_DEVEXT pDevExt, PHYSICAL_ADDRESS Addr)
{
    PNEMUMP_COMMON pCommon = NemuCommonFromDeviceExt(pDevExt);
    AssertRelease(pCommon->phVRAM.QuadPart <= Addr.QuadPart);
    return (NEMUVIDEOOFFSET)Addr.QuadPart - pCommon->phVRAM.QuadPart;
}

DECLINLINE(VOID) nemuWddmAssignPrimary(PNEMUWDDM_SOURCE pSource, PNEMUWDDM_ALLOCATION pAllocation, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    /* nemuWddmAssignPrimary can not be run in reentrant order, so safely do a direct unlocked check here */
    if (pSource->pPrimaryAllocation == pAllocation)
        return;

    if (pSource->pPrimaryAllocation)
    {
        PNEMUWDDM_ALLOCATION pOldAlloc = pSource->pPrimaryAllocation;
        /* clear the visibility info fo the current primary */
        pOldAlloc->bVisible = FALSE;
        pOldAlloc->bAssigned = FALSE;
        Assert(pOldAlloc->AllocData.SurfDesc.VidPnSourceId == srcId);
    }

    if (pAllocation)
    {
        Assert(pAllocation->AllocData.SurfDesc.VidPnSourceId == srcId);
        pAllocation->bAssigned = TRUE;
        pAllocation->bVisible = pSource->bVisible;

        if (pSource->AllocData.hostID != pAllocation->AllocData.hostID)
        {
            pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */
            pSource->AllocData.hostID = pAllocation->AllocData.hostID;
        }

        if (!nemuWddmAddrVramEqual(&pSource->AllocData.Addr, &pAllocation->AllocData.Addr))
        {
            if (!pAllocation->AllocData.hostID)
                pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */

            pSource->AllocData.Addr = pAllocation->AllocData.Addr;
        }
    }
    else
    {
        pSource->u8SyncState &= ~NEMUWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */
        /*ensure we do not refer to the deleted host id */
        pSource->AllocData.hostID = 0;
    }

    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->AllocationLock, &OldIrql);
    pSource->pPrimaryAllocation = pAllocation;
    KeReleaseSpinLock(&pSource->AllocationLock, OldIrql);
}

DECLINLINE(NEMUVIDEOOFFSET) nemuWddmAddrFramOffset(const NEMUWDDM_ADDR *pAddr)
{
    return (pAddr->offVram != NEMUVIDEOOFFSET_VOID && pAddr->SegmentId) ?
            (pAddr->SegmentId == 1 ? pAddr->offVram : 0)
            : NEMUVIDEOOFFSET_VOID;
}

DECLINLINE(int) nemuWddmScreenInfoInit(VBVAINFOSCREEN *pScreen, const NEMUWDDM_ALLOC_DATA *pAllocData, const POINT * pVScreenPos, uint16_t fFlags)
{
    NEMUVIDEOOFFSET offVram = nemuWddmAddrFramOffset(&pAllocData->Addr);
    if (offVram == NEMUVIDEOOFFSET_VOID && !(fFlags & VBVA_SCREEN_F_DISABLED))
    {
        WARN(("offVram == NEMUVIDEOOFFSET_VOID"));
        return VERR_INVALID_PARAMETER;
    }

    pScreen->u32ViewIndex    = pAllocData->SurfDesc.VidPnSourceId;
    pScreen->i32OriginX      = pVScreenPos->x;
    pScreen->i32OriginY      = pVScreenPos->y;
    pScreen->u32StartOffset  = (uint32_t)offVram;
    pScreen->u32LineSize     = pAllocData->SurfDesc.pitch;
    pScreen->u32Width        = pAllocData->SurfDesc.width;
    pScreen->u32Height       = pAllocData->SurfDesc.height;
    pScreen->u16BitsPerPixel = (uint16_t)pAllocData->SurfDesc.bpp;
    pScreen->u16Flags        = fFlags;

    return VINF_SUCCESS;
}

bool nemuWddmGhDisplayCheckSetInfoFromSource(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SOURCE pSource);

#ifdef NEMU_WITH_CROGL
#define NEMUWDDMENTRY_2_SWAPCHAIN(_pE) ((PNEMUWDDM_SWAPCHAIN)((uint8_t*)(_pE) - RT_OFFSETOF(NEMUWDDM_SWAPCHAIN, DevExtListEntry)))

BOOLEAN DxgkDdiInterruptRoutineNew(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG MessageNumber
    );
#endif

#ifdef NEMU_WDDM_WIN8
# define NEMUWDDM_IS_DISPLAYONLY() (g_NemuDisplayOnly)
#else
# define NEMUWDDM_IS_DISPLAYONLY() (FALSE)
#endif

# define NEMUWDDM_IS_FB_ALLOCATION(_pDevExt, _pAlloc) ((_pAlloc)->bAssigned)

# define NEMUWDDM_FB_ALLOCATION(_pDevExt, _pSrc) ((_pSrc)->pPrimaryAllocation)

#define NEMUWDDM_CTXLOCK_INIT(_p) do { \
        KeInitializeSpinLock(&(_p)->ContextLock); \
    } while (0)
#define NEMUWDDM_CTXLOCK_DATA KIRQL _ctxLockOldIrql;
#define NEMUWDDM_CTXLOCK_LOCK(_p) do { \
        KeAcquireSpinLock(&(_p)->ContextLock, &_ctxLockOldIrql); \
    } while (0)
#define NEMUWDDM_CTXLOCK_UNLOCK(_p) do { \
        KeReleaseSpinLock(&(_p)->ContextLock, _ctxLockOldIrql); \
    } while (0)

#endif /* #ifndef ___NemuMPWddm_h___ */

