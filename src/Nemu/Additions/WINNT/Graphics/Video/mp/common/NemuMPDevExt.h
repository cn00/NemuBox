/* $Id: NemuMPDevExt.h $ */

/** @file
 * Nemu Miniport device extension header
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

#ifndef NEMUMPDEVEXT_H
#define NEMUMPDEVEXT_H

#include "NemuMPUtils.h"
#include <Nemu/NemuVideoGuest.h>

#ifdef NEMU_XPDM_MINIPORT
# include <miniport.h>
# include <ntddvdeo.h>
# include <video.h>
# include "common/xpdm/NemuVideoPortAPI.h"
#endif

#ifdef NEMU_WDDM_MINIPORT
# ifdef NEMU_WDDM_WIN8
extern DWORD g_NemuDisplayOnly;
# endif
# include "wddm/NemuMPTypes.h"
#endif

#define NEMUMP_MAX_VIDEO_MODES 128
typedef struct NEMUMP_COMMON
{
    int cDisplays;                      /* Number of displays. */

    uint32_t cbVRAM;                    /* The VRAM size. */

    PHYSICAL_ADDRESS phVRAM;            /* Physical VRAM base. */

    ULONG ulApertureSize;               /* Size of the LFB aperture (>= VRAM size). */

    uint32_t cbMiniportHeap;            /* The size of reserved VRAM for miniport driver heap.
                                         * It is at offset:
                                         *   cbAdapterMemorySize - NEMU_VIDEO_ADAPTER_INFORMATION_SIZE - cbMiniportHeap
                                         */
    void *pvMiniportHeap;               /* The pointer to the miniport heap VRAM.
                                         * This is mapped by miniport separately.
                                         */
    void *pvAdapterInformation;         /* The pointer to the last 4K of VRAM.
                                         * This is mapped by miniport separately.
                                         */

    /** Whether HGSMI is enabled. */
    bool bHGSMI;
    /** Context information needed to receive commands from the host. */
    HGSMIHOSTCOMMANDCONTEXT hostCtx;
    /** Context information needed to submit commands to the host. */
    HGSMIGUESTCOMMANDCONTEXT guestCtx;

    BOOLEAN fAnyX;                      /* Unrestricted horizontal resolution flag. */
} NEMUMP_COMMON, *PNEMUMP_COMMON;

typedef struct _NEMUMP_DEVEXT
{
   struct _NEMUMP_DEVEXT *pNext;               /* Next extension in the DualView extension list.
                                                * The primary extension is the first one.
                                                */
#ifdef NEMU_XPDM_MINIPORT
   struct _NEMUMP_DEVEXT *pPrimary;            /* Pointer to the primary device extension. */

   ULONG iDevice;                              /* Device index: 0 for primary, otherwise a secondary device. */
   /* Standart video modes list.
    * Additional space is reserved for a custom video mode for this guest monitor.
    * The custom video mode index is alternating for each mode set and 2 indexes are needed for the custom mode.
    */
   VIDEO_MODE_INFORMATION aVideoModes[NEMUMP_MAX_VIDEO_MODES + 2];
   /* Number of available video modes, set by NemuMPCmnBuildVideoModesTable. */
   uint32_t cVideoModes;
   ULONG CurrentMode;                          /* Saved information about video modes */
   ULONG CurrentModeWidth;
   ULONG CurrentModeHeight;
   ULONG CurrentModeBPP;

   ULONG ulFrameBufferOffset;                  /* The framebuffer position in the VRAM. */
   ULONG ulFrameBufferSize;                    /* The size of the current framebuffer. */

   uint8_t  iInvocationCounter;
   uint32_t Prev_xres;
   uint32_t Prev_yres;
   uint32_t Prev_bpp;
#endif /*NEMU_XPDM_MINIPORT*/

#ifdef NEMU_WDDM_MINIPORT
   PDEVICE_OBJECT pPDO;
   UNICODE_STRING RegKeyName;
   UNICODE_STRING VideoGuid;

   uint8_t * pvVisibleVram;

   NEMUVIDEOCM_MGR CmMgr;
   NEMUVIDEOCM_MGR SeamlessCtxMgr;
   /* hgsmi allocation manager */
   NEMUVIDEOCM_ALLOC_MGR AllocMgr;
   NEMUVDMADDI_NODE aNodes[NEMUWDDM_NUM_NODES];
   LIST_ENTRY DpcCmdQueue;
   LIST_ENTRY SwapchainList3D;
   /* mutex for context list operations */
   KSPIN_LOCK ContextLock;
   KSPIN_LOCK SynchLock;
   volatile uint32_t cContexts3D;
   volatile uint32_t cContexts2D;
   volatile uint32_t cContextsDispIfResize;
   volatile uint32_t cUnlockedVBVADisabled;

   volatile uint32_t fCompletingCommands;

   DWORD dwDrvCfgFlags;
#ifdef NEMU_WITH_CROGL
   BOOLEAN f3DEnabled;
   BOOLEAN fTexPresentEnabled;
   BOOLEAN fCmdVbvaEnabled;
   BOOLEAN fComplexTopologiesEnabled;

   uint32_t u32CrConDefaultClientID;

   NEMUCMDVBVA CmdVbva;

   NEMUMP_CRCTLCON CrCtlCon;
   NEMUMP_CRSHGSMITRANSPORT CrHgsmiTransport;
#endif
   NEMUWDDM_GLOBAL_POINTER_INFO PointerInfo;

   NEMUVTLIST CtlList;
   NEMUVTLIST DmaCmdList;
#ifdef NEMU_WITH_VIDEOHWACCEL
   NEMUVTLIST VhwaCmdList;
#endif
   BOOLEAN bNotifyDxDpc;

   BOOLEAN fDisableTargetUpdate;



#ifdef NEMU_VDMA_WITH_WATCHDOG
   PKTHREAD pWdThread;
   KEVENT WdEvent;
#endif
   BOOL bVSyncTimerEnabled;
   volatile uint32_t fVSyncInVBlank;
   volatile LARGE_INTEGER VSyncTime;
   KTIMER VSyncTimer;
   KDPC VSyncDpc;

#if 0
   FAST_MUTEX ShRcTreeMutex;
   AVLPVTREE ShRcTree;
#endif

   NEMUWDDM_SOURCE aSources[NEMU_VIDEO_MAX_SCREENS];
   NEMUWDDM_TARGET aTargets[NEMU_VIDEO_MAX_SCREENS];
#endif /*NEMU_WDDM_MINIPORT*/

   union {
       /* Information that is only relevant to the primary device or is the same for all devices. */
       struct {

           void *pvReqFlush;                   /* Pointer to preallocated generic request structure for
                                                * VMMDevReq_VideoAccelFlush. Allocated when VBVA status
                                                * is changed. Deallocated on HwReset.
                                                */
           ULONG ulVbvaEnabled;                /* Indicates that VBVA mode is enabled. */
           ULONG ulMaxFrameBufferSize;         /* The size of the VRAM allocated for the a single framebuffer. */
           BOOLEAN fMouseHidden;               /* Has the mouse cursor been hidden by the guest? */
           NEMUMP_COMMON commonInfo;
#ifdef NEMU_XPDM_MINIPORT
           /* Video Port API dynamically picked up at runtime for binary backwards compatibility with older NT versions */
           NEMUVIDEOPORTPROCS VideoPortProcs;
#endif

#ifdef NEMU_WDDM_MINIPORT
           NEMUVDMAINFO Vdma;
# ifdef NEMUVDMA_WITH_VBVA
           NEMUVBVAINFO Vbva;
# endif
           D3DKMDT_HVIDPN hCommittedVidPn;      /* committed VidPn handle */
           DXGKRNL_INTERFACE DxgkInterface;     /* Display Port handle and callbacks */
#endif
       } primary;

       /* Secondary device information. */
       struct {
           BOOLEAN bEnabled;                   /* Device enabled flag */
       } secondary;
   } u;

   HGSMIAREA areaDisplay;                      /* Entire VRAM chunk for this display device. */
} NEMUMP_DEVEXT, *PNEMUMP_DEVEXT;

DECLINLINE(PNEMUMP_DEVEXT) NemuCommonToPrimaryExt(PNEMUMP_COMMON pCommon)
{
    return RT_FROM_MEMBER(pCommon, NEMUMP_DEVEXT, u.primary.commonInfo);
}

DECLINLINE(PNEMUMP_COMMON) NemuCommonFromDeviceExt(PNEMUMP_DEVEXT pExt)
{
#ifdef NEMU_XPDM_MINIPORT
    return &pExt->pPrimary->u.primary.commonInfo;
#else
    return &pExt->u.primary.commonInfo;
#endif
}

#ifdef NEMU_WDDM_MINIPORT
DECLINLINE(ULONG) nemuWddmVramCpuVisibleSize(PNEMUMP_DEVEXT pDevExt)
{
#ifdef NEMU_WITH_CROGL
    if (pDevExt->fCmdVbvaEnabled)
    {
        /* all memory layout info should be initialized */
        Assert(pDevExt->CmdVbva.Vbva.offVRAMBuffer);
        /* page aligned */
        Assert(!(pDevExt->CmdVbva.Vbva.offVRAMBuffer & 0xfff));

        return (ULONG)(pDevExt->CmdVbva.Vbva.offVRAMBuffer & ~0xfffULL);
    }
#endif
    /* all memory layout info should be initialized */
    Assert(pDevExt->aSources[0].Vbva.Vbva.offVRAMBuffer);
    /* page aligned */
    Assert(!(pDevExt->aSources[0].Vbva.Vbva.offVRAMBuffer & 0xfff));

    return (ULONG)(pDevExt->aSources[0].Vbva.Vbva.offVRAMBuffer & ~0xfffULL);
}

DECLINLINE(ULONG) nemuWddmVramCpuVisibleSegmentSize(PNEMUMP_DEVEXT pDevExt)
{
    return nemuWddmVramCpuVisibleSize(pDevExt);
}

/* 128 MB */
DECLINLINE(ULONG) nemuWddmVramCpuInvisibleSegmentSize(PNEMUMP_DEVEXT pDevExt)
{
    return 128 * 1024 * 1024;
}

#ifdef NEMUWDDM_RENDER_FROM_SHADOW

DECLINLINE(bool) nemuWddmCmpSurfDescsBase(NEMUWDDM_SURFACE_DESC *pDesc1, NEMUWDDM_SURFACE_DESC *pDesc2)
{
    if (pDesc1->width != pDesc2->width)
        return false;
    if (pDesc1->height != pDesc2->height)
        return false;
    if (pDesc1->format != pDesc2->format)
        return false;
    if (pDesc1->bpp != pDesc2->bpp)
        return false;
    if (pDesc1->pitch != pDesc2->pitch)
        return false;
    return true;
}

#endif
#endif /*NEMU_WDDM_MINIPORT*/

#endif /*NEMUMPDEVEXT_H*/
