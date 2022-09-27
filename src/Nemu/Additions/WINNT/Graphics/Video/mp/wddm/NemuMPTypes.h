/* $Id: NemuMPTypes.h $ */

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

#ifndef ___NemuMPTypes_h___
#define ___NemuMPTypes_h___

typedef struct _NEMUMP_DEVEXT *PNEMUMP_DEVEXT;
#ifdef NEMU_WITH_CROGL
typedef struct NEMUWDDM_SWAPCHAIN *PNEMUWDDM_SWAPCHAIN;
#endif
typedef struct NEMUWDDM_CONTEXT *PNEMUWDDM_CONTEXT;
typedef struct NEMUWDDM_ALLOCATION *PNEMUWDDM_ALLOCATION;

#include "common/wddm/NemuMPIf.h"
#include "NemuMPMisc.h"
#include "NemuMPCm.h"
#include "NemuMPVdma.h"
#include "NemuMPShgsmi.h"
#include "NemuMPVbva.h"
#include "NemuMPCr.h"
#include "NemuMPVModes.h"

#ifdef NEMU_WITH_CROGL
#include <cr_vreg.h>
#endif

#include <cr_sortarray.h>

#if 0
#include <iprt/avl.h>
#endif

/* one page size */
#define NEMUWDDM_C_DMA_BUFFER_SIZE         0x1000
#define NEMUWDDM_C_DMA_PRIVATEDATA_SIZE    0x4000
#define NEMUWDDM_C_ALLOC_LIST_SIZE         0xc00
#define NEMUWDDM_C_PATH_LOCATION_LIST_SIZE 0xc00

#define NEMUWDDM_C_POINTER_MAX_WIDTH  64
#define NEMUWDDM_C_POINTER_MAX_HEIGHT 64

#ifdef NEMU_WITH_VDMA
#define NEMUWDDM_C_VDMA_BUFFER_SIZE   (64*_1K)
#endif

#ifndef NEMUWDDM_RENDER_FROM_SHADOW
# ifndef NEMU_WITH_VDMA
#  error "NEMU_WITH_VDMA must be defined!!!"
# endif
#endif

#define NEMUWDDM_POINTER_ATTRIBUTES_SIZE NEMUWDDM_ROUNDBOUND( \
         NEMUWDDM_ROUNDBOUND( sizeof (VIDEO_POINTER_ATTRIBUTES), 4 ) + \
         NEMUWDDM_ROUNDBOUND(NEMUWDDM_C_POINTER_MAX_WIDTH * NEMUWDDM_C_POINTER_MAX_HEIGHT * 4, 4) + \
         NEMUWDDM_ROUNDBOUND((NEMUWDDM_C_POINTER_MAX_WIDTH * NEMUWDDM_C_POINTER_MAX_HEIGHT + 7) >> 3, 4) \
          , 8)

typedef struct _NEMUWDDM_POINTER_INFO
{
    uint32_t xPos;
    uint32_t yPos;
    union
    {
        VIDEO_POINTER_ATTRIBUTES data;
        char buffer[NEMUWDDM_POINTER_ATTRIBUTES_SIZE];
    } Attributes;
} NEMUWDDM_POINTER_INFO, *PNEMUWDDM_POINTER_INFO;

typedef struct _NEMUWDDM_GLOBAL_POINTER_INFO
{
    uint32_t iLastReportedScreen;
    uint32_t cVisible;
} NEMUWDDM_GLOBAL_POINTER_INFO, *PNEMUWDDM_GLOBAL_POINTER_INFO;

#ifdef NEMU_WITH_VIDEOHWACCEL
typedef struct NEMUWDDM_VHWA
{
    NEMUVHWA_INFO Settings;
    volatile uint32_t cOverlaysCreated;
} NEMUWDDM_VHWA;
#endif

typedef struct NEMUWDDM_ADDR
{
    /* if SegmentId == NULL - the sysmem data is presented with pvMem */
    UINT SegmentId;
    union {
        NEMUVIDEOOFFSET offVram;
        void * pvMem;
    };
} NEMUWDDM_ADDR, *PNEMUWDDM_ADDR;

typedef struct NEMUWDDM_ALLOC_DATA
{
    NEMUWDDM_SURFACE_DESC SurfDesc;
    NEMUWDDM_ADDR Addr;
    uint32_t hostID;
    uint32_t cHostIDRefs;
    struct NEMUWDDM_SWAPCHAIN *pSwapchain;
} NEMUWDDM_ALLOC_DATA, *PNEMUWDDM_ALLOC_DATA;

#define NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS 0x01
#define NEMUWDDM_HGSYNC_F_SYNCED_LOCATION   0x02
#define NEMUWDDM_HGSYNC_F_SYNCED_VISIBILITY 0x04
#define NEMUWDDM_HGSYNC_F_SYNCED_TOPOLOGY   0x08
#define NEMUWDDM_HGSYNC_F_SYNCED_ALL        (NEMUWDDM_HGSYNC_F_SYNCED_DIMENSIONS | NEMUWDDM_HGSYNC_F_SYNCED_LOCATION | NEMUWDDM_HGSYNC_F_SYNCED_VISIBILITY | NEMUWDDM_HGSYNC_F_SYNCED_TOPOLOGY)
#define NEMUWDDM_HGSYNC_F_CHANGED_LOCATION_ONLY        (NEMUWDDM_HGSYNC_F_SYNCED_ALL & ~NEMUWDDM_HGSYNC_F_SYNCED_LOCATION)
#define NEMUWDDM_HGSYNC_F_CHANGED_TOPOLOGY_ONLY        (NEMUWDDM_HGSYNC_F_SYNCED_ALL & ~NEMUWDDM_HGSYNC_F_SYNCED_TOPOLOGY)

typedef struct NEMUWDDM_SOURCE
{
    struct NEMUWDDM_ALLOCATION * pPrimaryAllocation;
    NEMUWDDM_ALLOC_DATA AllocData;
    uint8_t u8SyncState;
    BOOLEAN fTargetsReported;
    BOOLEAN bVisible;
#ifdef NEMU_WITH_CROGL
    /* specifies whether the source has 3D overlay data visible */
    BOOLEAN fHas3DVrs;
    NEMUVR_LIST VrList;
#endif
    NEMUVBVAINFO Vbva;
#ifdef NEMU_WITH_VIDEOHWACCEL
    /* @todo: in our case this seems more like a target property,
     * but keep it here for now */
    NEMUWDDM_VHWA Vhwa;
    volatile uint32_t cOverlays;
    LIST_ENTRY OverlayList;
    KSPIN_LOCK OverlayListLock;
#endif
    KSPIN_LOCK AllocationLock;
    POINT VScreenPos;
    NEMUWDDM_POINTER_INFO PointerInfo;
    uint32_t cTargets;
    NEMUCMDVBVA_SCREENMAP_DECL(uint32_t, aTargetMap);
} NEMUWDDM_SOURCE, *PNEMUWDDM_SOURCE;

typedef struct NEMUWDDM_TARGET
{
    RTRECTSIZE Size;
    uint32_t u32Id;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    /* since there coul be multiple state changes on auto-resize,
     * we pend notifying host to avoid flickering */
    uint8_t u8SyncState;
    bool fConnected;
    bool fConfigured;
} NEMUWDDM_TARGET, *PNEMUWDDM_TARGET;

/* allocation */
//#define NEMUWDDM_ALLOCATIONINDEX_VOID (~0U)
typedef struct NEMUWDDM_ALLOCATION
{
    LIST_ENTRY SwapchainEntry;
    NEMUWDDM_ALLOC_TYPE enmType;
    D3DDDI_RESOURCEFLAGS fRcFlags;
#ifdef NEMU_WITH_VIDEOHWACCEL
    NEMUVHWA_SURFHANDLE hHostHandle;
#endif
    BOOLEAN fDeleted;
    BOOLEAN bVisible;
    BOOLEAN bAssigned;
#ifdef DEBUG
    /* current for shared rc handling assumes that once resource has no opens, it can not be openned agaion */
    BOOLEAN fAssumedDeletion;
#endif
    NEMUWDDM_ALLOC_DATA AllocData;
    struct NEMUWDDM_RESOURCE *pResource;
    /* to return to the Runtime on DxgkDdiCreateAllocation */
    DXGK_ALLOCATIONUSAGEHINT UsageHint;
    uint32_t iIndex;
    uint32_t cOpens;
    KSPIN_LOCK OpenLock;
    LIST_ENTRY OpenList;
    /* helps tracking when to release wine shared resource */
    uint32_t cShRcRefs;
    HANDLE hSharedHandle;
#if 0
    AVLPVNODECORE ShRcTreeEntry;
#endif
    NEMUUHGSMI_BUFFER_TYPE_FLAGS fUhgsmiType;
} NEMUWDDM_ALLOCATION, *PNEMUWDDM_ALLOCATION;

typedef struct NEMUWDDM_RESOURCE
{
    NEMUWDDMDISP_RESOURCE_FLAGS fFlags;
    volatile uint32_t cRefs;
    NEMUWDDM_RC_DESC RcDesc;
    BOOLEAN fDeleted;
    uint32_t cAllocations;
    NEMUWDDM_ALLOCATION aAllocations[1];
} NEMUWDDM_RESOURCE, *PNEMUWDDM_RESOURCE;

typedef struct NEMUWDDM_OVERLAY
{
    LIST_ENTRY ListEntry;
    PNEMUMP_DEVEXT pDevExt;
    PNEMUWDDM_RESOURCE pResource;
    PNEMUWDDM_ALLOCATION pCurentAlloc;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    RECT DstRect;
} NEMUWDDM_OVERLAY, *PNEMUWDDM_OVERLAY;

typedef enum
{
    NEMUWDDM_DEVICE_TYPE_UNDEFINED = 0,
    NEMUWDDM_DEVICE_TYPE_SYSTEM
} NEMUWDDM_DEVICE_TYPE;

typedef struct NEMUWDDM_DEVICE
{
    PNEMUMP_DEVEXT pAdapter; /* Adapder info */
    HANDLE hDevice; /* handle passed to CreateDevice */
    NEMUWDDM_DEVICE_TYPE enmType; /* device creation flags passed to DxgkDdiCreateDevice, not sure we need it */
} NEMUWDDM_DEVICE, *PNEMUWDDM_DEVICE;

typedef enum
{
    NEMUWDDM_OBJSTATE_TYPE_UNKNOWN = 0,
    NEMUWDDM_OBJSTATE_TYPE_INITIALIZED,
    NEMUWDDM_OBJSTATE_TYPE_TERMINATED
} NEMUWDDM_OBJSTATE_TYPE;

#define NEMUWDDM_INVALID_COORD ((LONG)((~0UL) >> 1))

#ifdef NEMU_WITH_CROGL
typedef struct NEMUWDDM_SWAPCHAIN
{
    LIST_ENTRY DevExtListEntry;
    LIST_ENTRY AllocList;
    struct NEMUWDDM_CONTEXT *pContext;
    NEMUWDDM_OBJSTATE_TYPE enmState;
    volatile uint32_t cRefs;
    NEMUDISP_UMHANDLE hSwapchainUm;
    NEMUDISP_KMHANDLE hSwapchainKm;
    int32_t winHostID;
    BOOLEAN fExposed;
    POINT Pos;
    UINT width;
    UINT height;
    NEMUVR_LIST VisibleRegions;
}NEMUWDDM_SWAPCHAIN, *PNEMUWDDM_SWAPCHAIN;
#endif

typedef struct NEMUWDDM_CONTEXT
{
    struct NEMUWDDM_DEVICE * pDevice;
    HANDLE hContext;
    NEMUWDDM_CONTEXT_TYPE enmType;
    UINT  NodeOrdinal;
    UINT  EngineAffinity;
    BOOLEAN fRenderFromShadowDisabled;
#ifdef NEMU_WITH_CROGL
    int32_t hostID;
    uint32_t u32CrConClientID;
    NEMUMP_CRPACKER CrPacker;
    NEMUWDDM_HTABLE Swapchains;
#endif
    NEMUVIDEOCM_CTX CmContext;
    NEMUVIDEOCM_ALLOC_CONTEXT AllocContext;
} NEMUWDDM_CONTEXT, *PNEMUWDDM_CONTEXT;

typedef struct NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR
{
    NEMUWDDM_DMA_PRIVATEDATA_BASEHDR BaseHdr;
}NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR, *PNEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR;

#ifdef NEMUWDDM_RENDER_FROM_SHADOW

typedef struct NEMUWDDM_DMA_PRIVATEDATA_SHADOW2PRIMARY
{
    NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    NEMUVDMA_SHADOW2PRIMARY Shadow2Primary;
} NEMUWDDM_DMA_PRIVATEDATA_SHADOW2PRIMARY, *PNEMUWDDM_DMA_PRIVATEDATA_SHADOW2PRIMARY;

#endif

typedef struct NEMUWDDM_DMA_PRIVATEDATA_BLT
{
    NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    NEMUVDMA_BLT Blt;
} NEMUWDDM_DMA_PRIVATEDATA_BLT, *PNEMUWDDM_DMA_PRIVATEDATA_BLT;

typedef struct NEMUWDDM_DMA_PRIVATEDATA_FLIP
{
    NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    NEMUVDMA_FLIP Flip;
} NEMUWDDM_DMA_PRIVATEDATA_FLIP, *PNEMUWDDM_DMA_PRIVATEDATA_FLIP;

typedef struct NEMUWDDM_DMA_PRIVATEDATA_CLRFILL
{
    NEMUWDDM_DMA_PRIVATEDATA_PRESENTHDR Hdr;
    NEMUVDMA_CLRFILL ClrFill;
} NEMUWDDM_DMA_PRIVATEDATA_CLRFILL, *PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL;

typedef struct NEMUWDDM_UHGSMI_BUFFER_SUBMIT_INFO
{
    NEMUWDDM_DMA_ALLOCINFO Alloc;
    uint32_t cbData;
    uint32_t bDoNotSignalCompletion;
} NEMUWDDM_UHGSMI_BUFFER_SUBMIT_INFO, *PNEMUWDDM_UHGSMI_BUFFER_SUBMIT_INFO;

typedef struct NEMUWDDM_DMA_PRIVATEDATA_CHROMIUM_CMD
{
    NEMUWDDM_DMA_PRIVATEDATA_BASEHDR Base;
    NEMUWDDM_UHGSMI_BUFFER_SUBMIT_INFO aBufInfos[1];
} NEMUWDDM_DMA_PRIVATEDATA_CHROMIUM_CMD, *PNEMUWDDM_DMA_PRIVATEDATA_CHROMIUM_CMD;

typedef struct NEMUWDDM_DMA_PRIVATEDATA_ALLOCINFO_ON_SUBMIT
{
    NEMUWDDM_DMA_PRIVATEDATA_BASEHDR Base;
    NEMUWDDM_DMA_ALLOCINFO aInfos[1];
} NEMUWDDM_DMA_PRIVATEDATA_ALLOCINFO_ON_SUBMIT, *PNEMUWDDM_DMA_PRIVATEDATA_ALLOCINFO_ON_SUBMIT;

typedef struct NEMUWDDM_OPENALLOCATION
{
    LIST_ENTRY ListEntry;
    D3DKMT_HANDLE  hAllocation;
    PNEMUWDDM_ALLOCATION pAllocation;
    PNEMUWDDM_DEVICE pDevice;
    uint32_t cShRcRefs;
    uint32_t cOpens;
    uint32_t cHostIDRefs;
} NEMUWDDM_OPENALLOCATION, *PNEMUWDDM_OPENALLOCATION;

#define NEMU_VMODES_MAX_COUNT 128

typedef struct NEMU_VMODES
{
    uint32_t cTargets;
    CR_SORTARRAY aTargets[NEMU_VIDEO_MAX_SCREENS];
} NEMU_VMODES;

typedef struct NEMUWDDM_VMODES
{
    NEMU_VMODES Modes;
    /* note that we not use array indices to indentify modes, because indices may change due to element removal */
    uint64_t aTransientResolutions[NEMU_VIDEO_MAX_SCREENS];
    uint64_t aPendingRemoveCurResolutions[NEMU_VIDEO_MAX_SCREENS];
} NEMUWDDM_VMODES;

#endif /* #ifndef ___NemuMPTypes_h___ */
