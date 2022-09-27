/* $Id: NemuDispD3D.h $ */

/** @file
 * NemuVideo Display D3D User mode dll
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

#ifndef ___NemuDispD3D_h___
#define ___NemuDispD3D_h___

#include "NemuDispD3DIf.h"
#include "../../common/wddm/NemuMPIf.h"
#ifdef NEMU_WITH_CRHGSMI
#include "NemuUhgsmiDisp.h"
#endif

#ifdef NEMU_WDDMDISP_WITH_PROFILE
#include <iprt/asm.h>
extern volatile uint32_t g_u32NemuDispProfileFunctionLoggerIndex;
# define NEMUDISPPROFILE_FUNCTION_LOGGER_INDEX_GEN() ASMAtomicIncU32(&g_u32NemuDispProfileFunctionLoggerIndex);
# include "NemuDispProfile.h"
#endif

#include <iprt/cdefs.h>
#include <iprt/list.h>

#define NEMUWDDMDISP_MAX_VERTEX_STREAMS 16
#define NEMUWDDMDISP_MAX_SWAPCHAIN_SIZE 16
#define NEMUWDDMDISP_MAX_TEX_SAMPLERS 16
#define NEMUWDDMDISP_TOTAL_SAMPLERS NEMUWDDMDISP_MAX_TEX_SAMPLERS + 5
#define NEMUWDDMDISP_SAMPLER_IDX_IS_SPECIAL(_i) ((_i) >= D3DDMAPSAMPLER && (_i) <= D3DVERTEXTEXTURESAMPLER3)
#define NEMUWDDMDISP_SAMPLER_IDX_SPECIAL(_i) (NEMUWDDMDISP_SAMPLER_IDX_IS_SPECIAL(_i) ? (int)((_i) - D3DDMAPSAMPLER + NEMUWDDMDISP_MAX_TEX_SAMPLERS) : (int)-1)
#define NEMUWDDMDISP_SAMPLER_IDX(_i) (((_i) < NEMUWDDMDISP_MAX_TEX_SAMPLERS) ? (int)(_i) : NEMUWDDMDISP_SAMPLER_IDX_SPECIAL(_i))


/* maximum number of direct render targets to be used before
 * switching to offscreen rendering */
#ifdef NEMUWDDMDISP_DEBUG
# define NEMUWDDMDISP_MAX_DIRECT_RTS      g_NemuVDbgCfgMaxDirectRts
#else
# define NEMUWDDMDISP_MAX_DIRECT_RTS      3
#endif

#define NEMUWDDMDISP_IS_TEXTURE(_f) ((_f).Texture || (_f).Value == 0)

#ifdef NEMU_WITH_VIDEOHWACCEL
typedef struct NEMUDISPVHWA_INFO
{
    NEMUVHWA_INFO Settings;
}NEMUDISPVHWA_INFO;

/* represents settings secific to
 * display device (head) on the multiple-head graphics card
 * currently used for 2D (overlay) only since in theory its settings
 * can differ per each frontend's framebuffer. */
typedef struct NEMUWDDMDISP_HEAD
{
    NEMUDISPVHWA_INFO Vhwa;
} NEMUWDDMDISP_HEAD;
#endif

typedef struct NEMUWDDMDISP_ADAPTER
{
    HANDLE hAdapter;
    UINT uIfVersion;
    UINT uRtVersion;
    D3DDDI_ADAPTERCALLBACKS RtCallbacks;
    NEMUWDDMDISP_D3D D3D;
    NEMUWDDMDISP_FORMATS Formats;
    uint32_t u32Nemu3DCaps;
#ifdef NEMU_WDDMDISP_WITH_PROFILE
    NemuDispProfileFpsCounter ProfileDdiFps;
    NemuDispProfileSet ProfileDdiFunc;
#endif
#ifdef NEMU_WITH_VIDEOHWACCEL
    uint32_t cHeads;
    NEMUWDDMDISP_HEAD aHeads[1];
#endif
} NEMUWDDMDISP_ADAPTER, *PNEMUWDDMDISP_ADAPTER;

typedef struct NEMUWDDMDISP_CONTEXT
{
    RTLISTNODE ListNode;
    struct NEMUWDDMDISP_DEVICE *pDevice;
    D3DDDICB_CREATECONTEXT ContextInfo;
} NEMUWDDMDISP_CONTEXT, *PNEMUWDDMDISP_CONTEXT;

typedef struct NEMUWDDMDISP_STREAMSOURCEUM
{
    CONST VOID* pvBuffer;
    UINT cbStride;
} NEMUWDDMDISP_STREAMSOURCEUM, *PNEMUWDDMDISP_STREAMSOURCEUM;

typedef struct NEMUWDDMDISP_INDICIESUM
{
    CONST VOID* pvBuffer;
    UINT cbSize;
} NEMUWDDMDISP_INDICIESUM, *PNEMUWDDMDISP_INDICIESUM;

struct NEMUWDDMDISP_ALLOCATION;

typedef struct NEMUWDDMDISP_STREAM_SOURCE_INFO
{
  UINT   uiOffset;
  UINT   uiStride;
} NEMUWDDMDISP_STREAM_SOURCE_INFO;

typedef struct NEMUWDDMDISP_INDICES_INFO
{
    struct NEMUWDDMDISP_ALLOCATION *pIndicesAlloc;
    const void *pvIndicesUm;
    UINT uiStride;
} NEMUWDDMDISP_INDICES_INFO;

typedef struct NEMUWDDMDISP_RENDERTGT_FLAGS
{
    union
    {
        struct
        {
            UINT bAdded : 1;
            UINT bRemoved : 1;
            UINT Reserved : 30;
        };
        uint32_t Value;
    };
}NEMUWDDMDISP_RENDERTGT_FLAGS;

typedef struct NEMUWDDMDISP_RENDERTGT
{
    struct NEMUWDDMDISP_ALLOCATION *pAlloc;
    UINT cNumFlips;
    NEMUWDDMDISP_RENDERTGT_FLAGS fFlags;
} NEMUWDDMDISP_RENDERTGT, *PNEMUWDDMDISP_RENDERTGT;

#define NEMUWDDMDISP_INDEX_UNDEFINED (~0)
typedef struct NEMUWDDMDISP_SWAPCHAIN_FLAGS
{
    union
    {
        struct
        {
            UINT bChanged                : 1;
            UINT bRtReportingPresent     : 1; /* use Nemu extension method for performing present */
            UINT bSwitchReportingPresent : 1; /* switch to use Nemu extension method for performing present on next present */
            UINT Reserved                : 29;
        };
        uint32_t Value;
    };
}NEMUWDDMDISP_SWAPCHAIN_FLAGS;

typedef struct NEMUWDDMDISP_SWAPCHAIN
{
    RTLISTNODE ListEntry;
    UINT iBB; /* Backbuffer index */
    UINT cRTs; /* Number of render targets in the swapchain */
    NEMUWDDMDISP_SWAPCHAIN_FLAGS fFlags;
#ifndef NEMUWDDM_WITH_VISIBLE_FB
    IDirect3DSurface9 *pRenderTargetFbCopy;
    BOOL bRTFbCopyUpToDate;
#endif
    IDirect3DSwapChain9 *pSwapChainIf;
    /* a read-only hWnd we receive from wine
     * we use it for visible region notifications only,
     * it MUST NOT be destroyed on swapchain destruction,
     * wine will handle that for us */
    HWND hWnd;
    NEMUDISP_KMHANDLE hSwapchainKm;
    NEMUWDDMDISP_RENDERTGT aRTs[NEMUWDDMDISP_MAX_SWAPCHAIN_SIZE];
} NEMUWDDMDISP_SWAPCHAIN, *PNEMUWDDMDISP_SWAPCHAIN;

typedef struct NEMUWDDMDISP_DEVICE
{
    HANDLE hDevice;
    PNEMUWDDMDISP_ADAPTER pAdapter;
    IDirect3DDevice9 *pDevice9If;
    RTLISTANCHOR SwapchainList;
    UINT u32IfVersion;
    UINT uRtVersion;
    D3DDDI_DEVICECALLBACKS RtCallbacks;
    VOID *pvCmdBuffer;
    UINT cbCmdBuffer;
    D3DDDI_CREATEDEVICEFLAGS fFlags;
    /* number of StreamSources set */
    UINT cStreamSources;
    UINT cStreamSourcesUm;
    NEMUWDDMDISP_STREAMSOURCEUM aStreamSourceUm[NEMUWDDMDISP_MAX_VERTEX_STREAMS];
    struct NEMUWDDMDISP_ALLOCATION *aStreamSource[NEMUWDDMDISP_MAX_VERTEX_STREAMS];
    NEMUWDDMDISP_STREAM_SOURCE_INFO StreamSourceInfo[NEMUWDDMDISP_MAX_VERTEX_STREAMS];
    NEMUWDDMDISP_INDICES_INFO IndiciesInfo;
    /* need to cache the ViewPort data because IDirect3DDevice9::SetViewport
     * is split into two calls : SetViewport & SetZRange */
    D3DVIEWPORT9 ViewPort;
    NEMUWDDMDISP_CONTEXT DefaultContext;
#ifdef NEMU_WITH_CRHGSMI
    NEMUUHGSMI_PRIVATE_D3D Uhgsmi;
#endif

    /* no lock is needed for this since we're guaranteed the per-device calls are not reentrant */
    RTLISTANCHOR DirtyAllocList;

    UINT cSamplerTextures;
    struct NEMUWDDMDISP_RESOURCE *aSamplerTextures[NEMUWDDMDISP_TOTAL_SAMPLERS];

    struct NEMUWDDMDISP_RESOURCE *pDepthStencilRc;

    HMODULE hHgsmiTransportModule;

#ifdef NEMU_WDDMDISP_WITH_PROFILE
    NemuDispProfileFpsCounter ProfileDdiFps;
    NemuDispProfileSet ProfileDdiFunc;

    NemuDispProfileSet ProfileDdiPresentCb;
#endif

#ifdef NEMUWDDMDISP_DEBUG_TIMER
    HANDLE hTimerQueue;
#endif

    UINT cRTs;
    struct NEMUWDDMDISP_ALLOCATION * apRTs[1];
} NEMUWDDMDISP_DEVICE, *PNEMUWDDMDISP_DEVICE;

typedef struct NEMUWDDMDISP_LOCKINFO
{
    uint32_t cLocks;
    union {
        D3DDDIRANGE  Range;
        RECT  Area;
        D3DDDIBOX  Box;
    };
    D3DDDI_LOCKFLAGS fFlags;
    union {
        D3DLOCKED_RECT LockedRect;
        D3DLOCKED_BOX LockedBox;
    };
#ifdef NEMUWDDMDISP_DEBUG
    PVOID pvData;
#endif
} NEMUWDDMDISP_LOCKINFO;

typedef enum
{
    NEMUDISP_D3DIFTYPE_UNDEFINED = 0,
    NEMUDISP_D3DIFTYPE_SURFACE,
    NEMUDISP_D3DIFTYPE_TEXTURE,
    NEMUDISP_D3DIFTYPE_CUBE_TEXTURE,
    NEMUDISP_D3DIFTYPE_VOLUME_TEXTURE,
    NEMUDISP_D3DIFTYPE_VERTEXBUFFER,
    NEMUDISP_D3DIFTYPE_INDEXBUFFER
} NEMUDISP_D3DIFTYPE;

typedef struct NEMUWDDMDISP_ALLOCATION
{
    D3DKMT_HANDLE hAllocation;
    NEMUWDDM_ALLOC_TYPE enmType;
    UINT iAlloc;
    struct NEMUWDDMDISP_RESOURCE *pRc;
    void* pvMem;
    /* object type is defined by enmD3DIfType enum */
    IUnknown *pD3DIf;
    NEMUDISP_D3DIFTYPE enmD3DIfType;
    /* list entry used to add allocation to the dirty alloc list */
    RTLISTNODE DirtyAllocListEntry;
    BOOLEAN fEverWritten;
    BOOLEAN fDirtyWrite;
    BOOLEAN fAllocLocked;
    HANDLE hSharedHandle;
    NEMUWDDMDISP_LOCKINFO LockInfo;
    NEMUWDDM_DIRTYREGION DirtyRegion; /* <- dirty region to notify host about */
    NEMUWDDM_SURFACE_DESC SurfDesc;
    PNEMUWDDMDISP_SWAPCHAIN pSwapchain;
} NEMUWDDMDISP_ALLOCATION, *PNEMUWDDMDISP_ALLOCATION;

typedef struct NEMUWDDMDISP_RESOURCE
{
    HANDLE hResource;
    D3DKMT_HANDLE hKMResource;
    PNEMUWDDMDISP_DEVICE pDevice;
    NEMUWDDMDISP_RESOURCE_FLAGS fFlags;
    NEMUWDDM_RC_DESC RcDesc;
    UINT cAllocations;
    NEMUWDDMDISP_ALLOCATION aAllocations[1];
} NEMUWDDMDISP_RESOURCE, *PNEMUWDDMDISP_RESOURCE;

typedef struct NEMUWDDMDISP_QUERY
{
    D3DDDIQUERYTYPE enmType;
    D3DDDI_ISSUEQUERYFLAGS fQueryState;
    IDirect3DQuery9 *pQueryIf;
} NEMUWDDMDISP_QUERY, *PNEMUWDDMDISP_QUERY;

typedef struct NEMUWDDMDISP_TSS_LOOKUP
{
    BOOL  bSamplerState;
    DWORD dType;
} NEMUWDDMDISP_TSS_LOOKUP;

typedef struct NEMUWDDMDISP_OVERLAY
{
    D3DKMT_HANDLE hOverlay;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    PNEMUWDDMDISP_RESOURCE *pResource;
} NEMUWDDMDISP_OVERLAY, *PNEMUWDDMDISP_OVERLAY;

#define NEMUDISP_CUBEMAP_LEVELS_COUNT(pRc) (((pRc)->cAllocations)/6)
#define NEMUDISP_CUBEMAP_INDEX_TO_FACE(pRc, idx) ((D3DCUBEMAP_FACES)(D3DCUBEMAP_FACE_POSITIVE_X+(idx)%NEMUDISP_CUBEMAP_LEVELS_COUNT(pRc)))
#define NEMUDISP_CUBEMAP_INDEX_TO_LEVEL(pRc, idx) ((idx)%NEMUDISP_CUBEMAP_LEVELS_COUNT(pRc))

DECLINLINE(PNEMUWDDMDISP_SWAPCHAIN) nemuWddmSwapchainForAlloc(PNEMUWDDMDISP_ALLOCATION pAlloc)
{
    return pAlloc->pSwapchain;
}

DECLINLINE(UINT) nemuWddmSwapchainIdxFb(PNEMUWDDMDISP_SWAPCHAIN pSwapchain)
{
    return (pSwapchain->iBB + pSwapchain->cRTs - 1) % pSwapchain->cRTs;
}

/* if swapchain contains only one surface returns this surface */
DECLINLINE(PNEMUWDDMDISP_RENDERTGT) nemuWddmSwapchainGetBb(PNEMUWDDMDISP_SWAPCHAIN pSwapchain)
{
    if (pSwapchain->cRTs)
    {
        Assert(pSwapchain->iBB < pSwapchain->cRTs);
        return &pSwapchain->aRTs[pSwapchain->iBB];
    }
    return NULL;
}

DECLINLINE(PNEMUWDDMDISP_RENDERTGT) nemuWddmSwapchainGetFb(PNEMUWDDMDISP_SWAPCHAIN pSwapchain)
{
    if (pSwapchain->cRTs)
    {
        UINT iFb = nemuWddmSwapchainIdxFb(pSwapchain);
        return &pSwapchain->aRTs[iFb];
    }
    return NULL;
}

void nemuWddmResourceInit(PNEMUWDDMDISP_RESOURCE pRc, UINT cAllocs);

#ifndef IN_NEMUCRHGSMI
PNEMUWDDMDISP_SWAPCHAIN nemuWddmSwapchainFindCreate(PNEMUWDDMDISP_DEVICE pDevice, PNEMUWDDMDISP_ALLOCATION pBbAlloc, BOOL *pbNeedPresent);
HRESULT nemuWddmSwapchainChkCreateIf(PNEMUWDDMDISP_DEVICE pDevice, PNEMUWDDMDISP_SWAPCHAIN pSwapchain);
VOID nemuWddmSwapchainDestroy(PNEMUWDDMDISP_DEVICE pDevice, PNEMUWDDMDISP_SWAPCHAIN pSwapchain);

#endif

#endif /* #ifndef ___NemuDispD3D_h___ */
