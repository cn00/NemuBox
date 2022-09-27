/* $Id: NemuMPIf.h $ */

/** @file
 * Nemu WDDM Miniport driver
 *
 * Contains base definitions of constants & structures used
 * to control & perform rendering,
 * such as DMA commands types, allocation types, escape codes, etc.
 * used by both miniport & display drivers.
 *
 * The latter uses these and only these defs to communicate with the former
 * by posting appropriate requests via D3D RT Krnl Svc accessing callbacks.
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

#ifndef ___NemuMPIf_h___
#define ___NemuMPIf_h___

#include <Nemu/NemuVideo.h>
#include "../../../../include/NemuDisplay.h"
#include "../NemuVideoTools.h"
#include <Nemu/NemuUhgsmi.h>
#include <Nemu/NemuGuest2.h>

/* One would increase this whenever definitions in this file are changed */
#define NEMUVIDEOIF_VERSION 20

#define NEMUWDDM_NODE_ID_SYSTEM           0
#define NEMUWDDM_NODE_ID_3D               (NEMUWDDM_NODE_ID_SYSTEM)
#define NEMUWDDM_NODE_ID_3D_KMT           (NEMUWDDM_NODE_ID_3D)
#define NEMUWDDM_NODE_ID_2D_VIDEO         (NEMUWDDM_NODE_ID_3D_KMT+1)
#define NEMUWDDM_NUM_NODES                (NEMUWDDM_NODE_ID_2D_VIDEO+1)

#define NEMUWDDM_ENGINE_ID_SYSTEM         0
#if (NEMUWDDM_NODE_ID_3D == NEMUWDDM_NODE_ID_SYSTEM)
# define NEMUWDDM_ENGINE_ID_3D            (NEMUWDDM_ENGINE_ID_SYSTEM+1)
#else
# define NEMUWDDM_ENGINE_ID_3D            0
#endif
#if (NEMUWDDM_NODE_ID_3D_KMT == NEMUWDDM_NODE_ID_3D)
# define NEMUWDDM_ENGINE_ID_3D_KMT     NEMUWDDM_ENGINE_ID_3D
#else
# define NEMUWDDM_ENGINE_ID_3D_KMT     0
#endif
#if (NEMUWDDM_NODE_ID_2D_VIDEO == NEMUWDDM_NODE_ID_3D)
# define NEMUWDDM_ENGINE_ID_2D_VIDEO       NEMUWDDM_ENGINE_ID_3D
#else
# define NEMUWDDM_ENGINE_ID_2D_VIDEO       0
#endif


/* create allocation func */
typedef enum
{
    NEMUWDDM_ALLOC_TYPE_UNEFINED = 0,
    NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE,
    NEMUWDDM_ALLOC_TYPE_STD_SHADOWSURFACE,
    NEMUWDDM_ALLOC_TYPE_STD_STAGINGSURFACE,
    /* this one is win 7-specific and hence unused for now */
    NEMUWDDM_ALLOC_TYPE_STD_GDISURFACE
    /* custom allocation types requested from user-mode d3d module will go here */
    , NEMUWDDM_ALLOC_TYPE_UMD_RC_GENERIC
    , NEMUWDDM_ALLOC_TYPE_UMD_HGSMI_BUFFER
} NEMUWDDM_ALLOC_TYPE;

/* usage */
typedef enum
{
    NEMUWDDM_ALLOCUSAGE_TYPE_UNEFINED = 0,
    /* set for the allocation being primary */
    NEMUWDDM_ALLOCUSAGE_TYPE_PRIMARY,
} NEMUWDDM_ALLOCUSAGE_TYPE;

typedef struct NEMUWDDM_SURFACE_DESC
{
    UINT width;
    UINT height;
    D3DDDIFORMAT format;
    UINT bpp;
    UINT pitch;
    UINT depth;
    UINT slicePitch;
    UINT d3dWidth;
    UINT cbSize;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DDDI_RATIONAL RefreshRate;
} NEMUWDDM_SURFACE_DESC, *PNEMUWDDM_SURFACE_DESC;

typedef struct NEMUWDDM_ALLOCINFO
{
    NEMUWDDM_ALLOC_TYPE enmType;
    union
    {
        struct
        {
            D3DDDI_RESOURCEFLAGS fFlags;
            /* id used to identify the allocation on the host */
            uint32_t hostID;
            uint64_t hSharedHandle;
            NEMUWDDM_SURFACE_DESC SurfDesc;
        };

        struct
        {
            uint32_t cbBuffer;
            NEMUUHGSMI_BUFFER_TYPE_FLAGS fUhgsmiType;
        };
    };
} NEMUWDDM_ALLOCINFO, *PNEMUWDDM_ALLOCINFO;

typedef struct NEMUWDDM_RC_DESC
{
    D3DDDI_RESOURCEFLAGS fFlags;
    D3DDDIFORMAT enmFormat;
    D3DDDI_POOL enmPool;
    D3DDDIMULTISAMPLE_TYPE enmMultisampleType;
    UINT MultisampleQuality;
    UINT MipLevels;
    UINT Fvf;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DDDI_RATIONAL RefreshRate;
    D3DDDI_ROTATION enmRotation;
} NEMUWDDM_RC_DESC, *PNEMUWDDM_RC_DESC;

typedef struct NEMUWDDMDISP_RESOURCE_FLAGS
{
    union
    {
        struct
        {
            UINT Opened     : 1; /* this resource is OpenResource'd rather than CreateResource'd */
            UINT Generic    : 1; /* identifies this is a resource created with CreateResource, the NEMUWDDMDISP_RESOURCE::fRcFlags is valid */
            UINT KmResource : 1; /* this resource has underlying km resource */
            UINT Reserved   : 29; /* reserved */
        };
        UINT        Value;
    };
} NEMUWDDMDISP_RESOURCE_FLAGS, *PNEMUWDDMDISP_RESOURCE_FLAGS;

typedef struct NEMUWDDM_RCINFO
{
    NEMUWDDMDISP_RESOURCE_FLAGS fFlags;
    NEMUWDDM_RC_DESC RcDesc;
    uint32_t cAllocInfos;
//    NEMUWDDM_ALLOCINFO aAllocInfos[1];
} NEMUWDDM_RCINFO, *PNEMUWDDM_RCINFO;

typedef struct NEMUWDDM_DMA_PRIVATEDATA_FLAFS
{
    union
    {
        struct
        {
            UINT bCmdInDmaBuffer : 1;
            UINT bReserved : 31;
        };
        uint32_t Value;
    };
} NEMUWDDM_DMA_PRIVATEDATA_FLAFS, *PNEMUWDDM_DMA_PRIVATEDATA_FLAFS;

typedef struct NEMUWDDM_DMA_PRIVATEDATA_BASEHDR
{
    NEMUVDMACMD_TYPE enmCmd;
    union
    {
        NEMUWDDM_DMA_PRIVATEDATA_FLAFS fFlags;
        uint32_t u32CmdReserved;
    };
} NEMUWDDM_DMA_PRIVATEDATA_BASEHDR, *PNEMUWDDM_DMA_PRIVATEDATA_BASEHDR;

typedef struct NEMUWDDM_UHGSMI_BUFFER_UI_SUBMIT_INFO
{
    uint32_t offData;
    uint32_t cbData;
} NEMUWDDM_UHGSMI_BUFFER_UI_SUBMIT_INFO, *PNEMUWDDM_UHGSMI_BUFFER_UI_SUBMIT_INFO;

typedef struct NEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD
{
    NEMUWDDM_DMA_PRIVATEDATA_BASEHDR Base;
    NEMUWDDM_UHGSMI_BUFFER_UI_SUBMIT_INFO aBufInfos[1];
} NEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD, *PNEMUWDDM_DMA_PRIVATEDATA_UM_CHROMIUM_CMD;


#define NEMUVHWA_F_ENABLED  0x00000001
#define NEMUVHWA_F_CKEY_DST 0x00000002
#define NEMUVHWA_F_CKEY_SRC 0x00000004

#define NEMUVHWA_MAX_FORMATS 8

typedef struct NEMUVHWA_INFO
{
    uint32_t fFlags;
    uint32_t cOverlaysSupported;
    uint32_t cFormats;
    D3DDDIFORMAT aFormats[NEMUVHWA_MAX_FORMATS];
} NEMUVHWA_INFO;

#define NEMUWDDM_OVERLAY_F_CKEY_DST      0x00000001
#define NEMUWDDM_OVERLAY_F_CKEY_DSTRANGE 0x00000002
#define NEMUWDDM_OVERLAY_F_CKEY_SRC      0x00000004
#define NEMUWDDM_OVERLAY_F_CKEY_SRCRANGE 0x00000008
#define NEMUWDDM_OVERLAY_F_BOB           0x00000010
#define NEMUWDDM_OVERLAY_F_INTERLEAVED   0x00000020
#define NEMUWDDM_OVERLAY_F_MIRROR_LR     0x00000040
#define NEMUWDDM_OVERLAY_F_MIRROR_UD     0x00000080
#define NEMUWDDM_OVERLAY_F_DEINTERLACED  0x00000100

typedef struct NEMUWDDM_OVERLAY_DESC
{
    uint32_t fFlags;
    UINT DstColorKeyLow;
    UINT DstColorKeyHigh;
    UINT SrcColorKeyLow;
    UINT SrcColorKeyHigh;
} NEMUWDDM_OVERLAY_DESC, *PNEMUWDDM_OVERLAY_DESC;

typedef struct NEMUWDDM_OVERLAY_INFO
{
    NEMUWDDM_OVERLAY_DESC OverlayDesc;
    NEMUWDDM_DIRTYREGION DirtyRegion; /* <- the dirty region of the overlay surface */
} NEMUWDDM_OVERLAY_INFO, *PNEMUWDDM_OVERLAY_INFO;

typedef struct NEMUWDDM_OVERLAYFLIP_INFO
{
    NEMUWDDM_DIRTYREGION DirtyRegion; /* <- the dirty region of the overlay surface */
} NEMUWDDM_OVERLAYFLIP_INFO, *PNEMUWDDM_OVERLAYFLIP_INFO;


typedef enum
{
    NEMUWDDM_CONTEXT_TYPE_UNDEFINED = 0,
    /* system-created context (for GDI rendering) */
    NEMUWDDM_CONTEXT_TYPE_SYSTEM,
    /* context created by the D3D User-mode driver when crogl IS available */
    NEMUWDDM_CONTEXT_TYPE_CUSTOM_3D,
    /* context created by the D3D User-mode driver when crogl is NOT available or for ddraw overlay acceleration */
    NEMUWDDM_CONTEXT_TYPE_CUSTOM_2D,
    /* contexts created by the cromium HGSMI transport for HGSMI commands submission */
    NEMUWDDM_CONTEXT_TYPE_CUSTOM_UHGSMI_3D,
    NEMUWDDM_CONTEXT_TYPE_CUSTOM_UHGSMI_GL,
    /* context created by the kernel->user communication mechanism for visible rects reporting, etc.  */
    NEMUWDDM_CONTEXT_TYPE_CUSTOM_SESSION,
    /* context created by NemuTray to handle resize operations */
    NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE,
    /* context created by NemuTray to handle seamless operations */
    NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS
} NEMUWDDM_CONTEXT_TYPE;

typedef struct NEMUWDDM_CREATECONTEXT_INFO
{
    /* interface version, i.e. 9 for d3d9, 8 for d3d8, etc. */
    uint32_t u32IfVersion;
    /* true if d3d false if ddraw */
    NEMUWDDM_CONTEXT_TYPE enmType;
    uint32_t crVersionMajor;
    uint32_t crVersionMinor;
    /* we use uint64_t instead of HANDLE to ensure structure def is the same for both 32-bit and 64-bit
     * since x64 kernel driver can be called by 32-bit UMD */
    uint64_t hUmEvent;
    /* info to be passed to UMD notification to identify the context */
    uint64_t u64UmInfo;
} NEMUWDDM_CREATECONTEXT_INFO, *PNEMUWDDM_CREATECONTEXT_INFO;

typedef uint64_t NEMUDISP_UMHANDLE;
typedef uint32_t NEMUDISP_KMHANDLE;

typedef struct NEMUWDDM_RECTS_FLAFS
{
    union
    {
        struct
        {
            /* used only in conjunction with bSetVisibleRects.
             * if set - NEMUWDDM_RECTS_INFO::aRects[0] contains view rectangle */
            UINT bSetViewRect : 1;
            /* adds visible regions */
            UINT bAddVisibleRects : 1;
            /* adds hidden regions */
            UINT bAddHiddenRects : 1;
            /* hide entire window */
            UINT bHide : 1;
            /* reserved */
            UINT Reserved : 28;
        };
        uint32_t Value;
    };
} NEMUWDDM_RECTS_FLAFS, *PNEMUWDDM_RECTS_FLAFS;

typedef struct NEMUWDDM_RECTS_INFO
{
    uint32_t cRects;
    RECT aRects[1];
} NEMUWDDM_RECTS_INFO, *PNEMUWDDM_RECTS_INFO;

#define NEMUWDDM_RECTS_INFO_SIZE4CRECTS(_cRects) (RT_OFFSETOF(NEMUWDDM_RECTS_INFO, aRects[(_cRects)]))
#define NEMUWDDM_RECTS_INFO_SIZE(_pRects) (NEMUVIDEOCM_CMD_RECTS_SIZE4CRECTS((_pRects)->cRects))

typedef enum
{
    /* command to be post to user mode */
    NEMUVIDEOCM_CMD_TYPE_UM = 0,
    /* control command processed in kernel mode */
    NEMUVIDEOCM_CMD_TYPE_CTL_KM,
    NEMUVIDEOCM_CMD_DUMMY_32BIT = 0x7fffffff
} NEMUVIDEOCM_CMD_TYPE;

typedef struct NEMUVIDEOCM_CMD_HDR
{
    uint64_t u64UmData;
    uint32_t cbCmd;
    NEMUVIDEOCM_CMD_TYPE enmType;
}NEMUVIDEOCM_CMD_HDR, *PNEMUVIDEOCM_CMD_HDR;

AssertCompile((sizeof (NEMUVIDEOCM_CMD_HDR) & 7) == 0);

typedef struct NEMUVIDEOCM_CMD_RECTS
{
    NEMUWDDM_RECTS_FLAFS fFlags;
    NEMUWDDM_RECTS_INFO RectsInfo;
} NEMUVIDEOCM_CMD_RECTS, *PNEMUVIDEOCM_CMD_RECTS;

typedef struct NEMUVIDEOCM_CMD_RECTS_INTERNAL
{
    union
    {
        NEMUDISP_UMHANDLE hSwapchainUm;
        uint64_t hWnd;
        uint64_t u64Value;
    };
    NEMUVIDEOCM_CMD_RECTS Cmd;
} NEMUVIDEOCM_CMD_RECTS_INTERNAL, *PNEMUVIDEOCM_CMD_RECTS_INTERNAL;

typedef struct NEMUVIDEOCM_CMD_RECTS_HDR
{
    NEMUVIDEOCM_CMD_HDR Hdr;
    NEMUVIDEOCM_CMD_RECTS_INTERNAL Data;
} NEMUVIDEOCM_CMD_RECTS_HDR, *PNEMUVIDEOCM_CMD_RECTS_HDR;

#define NEMUVIDEOCM_CMD_RECTS_INTERNAL_SIZE4CRECTS(_cRects) (RT_OFFSETOF(NEMUVIDEOCM_CMD_RECTS_INTERNAL, Cmd.RectsInfo.aRects[(_cRects)]))
#define NEMUVIDEOCM_CMD_RECTS_INTERNAL_SIZE(_pCmd) (NEMUVIDEOCM_CMD_RECTS_INTERNAL_SIZE4CRECTS((_pCmd)->cRects))

typedef struct NEMUWDDM_GETNEMUVIDEOCMCMD_HDR
{
    uint32_t cbCmdsReturned;
    uint32_t cbRemainingCmds;
    uint32_t cbRemainingFirstCmd;
    uint32_t u32Reserved;
} NEMUWDDM_GETNEMUVIDEOCMCMD_HDR, *PNEMUWDDM_GETNEMUVIDEOCMCMD_HDR;

typedef struct NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD
{
    NEMUDISPIFESCAPE EscapeHdr;
    NEMUWDDM_GETNEMUVIDEOCMCMD_HDR Hdr;
} NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD, *PNEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD;

AssertCompile((sizeof (NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD) & 7) == 0);
AssertCompile(RT_OFFSETOF(NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD, EscapeHdr) == 0);

typedef struct NEMUDISPIFESCAPE_DBGPRINT
{
    NEMUDISPIFESCAPE EscapeHdr;
    /* null-terminated string to DbgPrint including \0 */
    char aStringBuf[1];
} NEMUDISPIFESCAPE_DBGPRINT, *PNEMUDISPIFESCAPE_DBGPRINT;
AssertCompile(RT_OFFSETOF(NEMUDISPIFESCAPE_DBGPRINT, EscapeHdr) == 0);

typedef enum
{
    NEMUDISPIFESCAPE_DBGDUMPBUF_TYPE_UNDEFINED = 0,
    NEMUDISPIFESCAPE_DBGDUMPBUF_TYPE_D3DCAPS9 = 1,
    NEMUDISPIFESCAPE_DBGDUMPBUF_TYPE_DUMMY32BIT = 0x7fffffff
} NEMUDISPIFESCAPE_DBGDUMPBUF_TYPE;

typedef struct NEMUDISPIFESCAPE_DBGDUMPBUF_FLAGS
{
    union
    {
        struct
        {
            UINT WoW64      : 1;
            UINT Reserved   : 31; /* reserved */
        };
        UINT  Value;
    };
} NEMUDISPIFESCAPE_DBGDUMPBUF_FLAGS, *PNEMUDISPIFESCAPE_DBGDUMPBUF_FLAGS;

typedef struct NEMUDISPIFESCAPE_DBGDUMPBUF
{
    NEMUDISPIFESCAPE EscapeHdr;
    NEMUDISPIFESCAPE_DBGDUMPBUF_TYPE enmType;
    NEMUDISPIFESCAPE_DBGDUMPBUF_FLAGS Flags;
    char aBuf[1];
} NEMUDISPIFESCAPE_DBGDUMPBUF, *PNEMUDISPIFESCAPE_DBGDUMPBUF;
AssertCompile(RT_OFFSETOF(NEMUDISPIFESCAPE_DBGDUMPBUF, EscapeHdr) == 0);

typedef struct NEMUSCREENLAYOUT_ELEMENT
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    POINT pos;
} NEMUSCREENLAYOUT_ELEMENT, *PNEMUSCREENLAYOUT_ELEMENT;

typedef struct NEMUSCREENLAYOUT
{
    uint32_t cScreens;
    NEMUSCREENLAYOUT_ELEMENT aScreens[1];
} NEMUSCREENLAYOUT, *PNEMUSCREENLAYOUT;

typedef struct NEMUDISPIFESCAPE_SCREENLAYOUT
{
    NEMUDISPIFESCAPE EscapeHdr;
    NEMUSCREENLAYOUT ScreenLayout;
} NEMUDISPIFESCAPE_SCREENLAYOUT, *PNEMUDISPIFESCAPE_SCREENLAYOUT;

typedef struct NEMUSWAPCHAININFO
{
    NEMUDISP_KMHANDLE hSwapchainKm; /* in, NULL if new is being created */
    NEMUDISP_UMHANDLE hSwapchainUm; /* in, UMD private data */
    int32_t winHostID;
    RECT Rect;
    UINT u32Reserved;
    UINT cAllocs;
    D3DKMT_HANDLE ahAllocs[1];
}NEMUSWAPCHAININFO, *PNEMUSWAPCHAININFO;
typedef struct NEMUDISPIFESCAPE_SWAPCHAININFO
{
    NEMUDISPIFESCAPE EscapeHdr;
    NEMUSWAPCHAININFO SwapchainInfo;
} NEMUDISPIFESCAPE_SWAPCHAININFO, *PNEMUDISPIFESCAPE_SWAPCHAININFO;

typedef struct NEMUVIDEOCM_UM_ALLOC
{
    NEMUDISP_KMHANDLE hAlloc;
    uint32_t cbData;
    uint64_t pvData;
    uint64_t hSynch;
    NEMUUHGSMI_BUFFER_TYPE_FLAGS fUhgsmiType;
} NEMUVIDEOCM_UM_ALLOC, *PNEMUVIDEOCM_UM_ALLOC;

typedef struct NEMUDISPIFESCAPE_UHGSMI_ALLOCATE
{
    NEMUDISPIFESCAPE EscapeHdr;
    NEMUVIDEOCM_UM_ALLOC Alloc;
} NEMUDISPIFESCAPE_UHGSMI_ALLOCATE, *PNEMUDISPIFESCAPE_UHGSMI_ALLOCATE;

typedef struct NEMUDISPIFESCAPE_UHGSMI_DEALLOCATE
{
    NEMUDISPIFESCAPE EscapeHdr;
    NEMUDISP_KMHANDLE hAlloc;
} NEMUDISPIFESCAPE_UHGSMI_DEALLOCATE, *PNEMUDISPIFESCAPE_UHGSMI_DEALLOCATE;

typedef struct NEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE
{
    NEMUDISP_KMHANDLE hAlloc;
    NEMUWDDM_UHGSMI_BUFFER_UI_SUBMIT_INFO Info;
} NEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE, *PNEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE;

typedef struct NEMUDISPIFESCAPE_UHGSMI_SUBMIT
{
    NEMUDISPIFESCAPE EscapeHdr;
    NEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE aBuffers[1];
} NEMUDISPIFESCAPE_UHGSMI_SUBMIT, *PNEMUDISPIFESCAPE_UHGSMI_SUBMIT;

typedef struct NEMUDISPIFESCAPE_SHRC_REF
{
    NEMUDISPIFESCAPE EscapeHdr;
    uint64_t hAlloc;
} NEMUDISPIFESCAPE_SHRC_REF, *PNEMUDISPIFESCAPE_SHRC_REF;

typedef struct NEMUDISPIFESCAPE_SETALLOCHOSTID
{
    NEMUDISPIFESCAPE EscapeHdr;
    int32_t rc;
    uint32_t hostID;
    uint64_t hAlloc;

} NEMUDISPIFESCAPE_SETALLOCHOSTID, *PNEMUDISPIFESCAPE_SETALLOCHOSTID;

typedef struct NEMUDISPIFESCAPE_CRHGSMICTLCON_CALL
{
    NEMUDISPIFESCAPE EscapeHdr;
    NemuGuestHGCMCallInfo CallInfo;
} NEMUDISPIFESCAPE_CRHGSMICTLCON_CALL, *PNEMUDISPIFESCAPE_CRHGSMICTLCON_CALL;

/* query info func */
typedef struct NEMUWDDM_QI
{
    uint32_t u32Version;
    uint32_t u32Nemu3DCaps;
    uint32_t cInfos;
    NEMUVHWA_INFO aInfos[NEMU_VIDEO_MAX_SCREENS];
} NEMUWDDM_QI;

/** Convert a given FourCC code to a D3DDDIFORMAT enum. */
#define NEMUWDDM_D3DDDIFORMAT_FROM_FOURCC(_a, _b, _c, _d) \
    ((D3DDDIFORMAT)MAKEFOURCC(_a, _b, _c, _d))

/* submit cmd func */
DECLINLINE(D3DDDIFORMAT) nemuWddmFmtNoAlphaFormat(D3DDDIFORMAT enmFormat)
{
    switch (enmFormat)
    {
        case D3DDDIFMT_A8R8G8B8:
            return D3DDDIFMT_X8R8G8B8;
        case D3DDDIFMT_A1R5G5B5:
            return D3DDDIFMT_X1R5G5B5;
        case D3DDDIFMT_A4R4G4B4:
            return D3DDDIFMT_X4R4G4B4;
        case D3DDDIFMT_A8B8G8R8:
            return D3DDDIFMT_X8B8G8R8;
        default:
            return enmFormat;
    }
}

/* tooling */
DECLINLINE(UINT) nemuWddmCalcBitsPerPixel(D3DDDIFORMAT enmFormat)
{
    switch (enmFormat)
    {
        case D3DDDIFMT_R8G8B8:
            return 24;
        case D3DDDIFMT_A8R8G8B8:
        case D3DDDIFMT_X8R8G8B8:
            return 32;
        case D3DDDIFMT_R5G6B5:
        case D3DDDIFMT_X1R5G5B5:
        case D3DDDIFMT_A1R5G5B5:
        case D3DDDIFMT_A4R4G4B4:
            return 16;
        case D3DDDIFMT_R3G3B2:
        case D3DDDIFMT_A8:
            return 8;
        case D3DDDIFMT_A8R3G3B2:
        case D3DDDIFMT_X4R4G4B4:
            return 16;
        case D3DDDIFMT_A2B10G10R10:
        case D3DDDIFMT_A8B8G8R8:
        case D3DDDIFMT_X8B8G8R8:
        case D3DDDIFMT_G16R16:
        case D3DDDIFMT_A2R10G10B10:
            return 32;
        case D3DDDIFMT_A16B16G16R16:
        case D3DDDIFMT_A16B16G16R16F:
            return 64;
        case D3DDDIFMT_A32B32G32R32F:
            return 128;
        case D3DDDIFMT_A8P8:
            return 16;
        case D3DDDIFMT_P8:
        case D3DDDIFMT_L8:
            return 8;
        case D3DDDIFMT_L16:
        case D3DDDIFMT_A8L8:
            return 16;
        case D3DDDIFMT_A4L4:
            return 8;
        case D3DDDIFMT_V8U8:
        case D3DDDIFMT_L6V5U5:
            return 16;
        case D3DDDIFMT_X8L8V8U8:
        case D3DDDIFMT_Q8W8V8U8:
        case D3DDDIFMT_V16U16:
        case D3DDDIFMT_W11V11U10:
        case D3DDDIFMT_A2W10V10U10:
            return 32;
        case D3DDDIFMT_D16_LOCKABLE:
        case D3DDDIFMT_D16:
        case D3DDDIFMT_D15S1:
            return 16;
        case D3DDDIFMT_D32:
        case D3DDDIFMT_D24S8:
        case D3DDDIFMT_D24X8:
        case D3DDDIFMT_D24X4S4:
        case D3DDDIFMT_D24FS8:
        case D3DDDIFMT_D32_LOCKABLE:
        case D3DDDIFMT_D32F_LOCKABLE:
            return 32;
        case D3DDDIFMT_S8_LOCKABLE:
            return 8;
        case D3DDDIFMT_DXT1:
            return 4;
        case D3DDDIFMT_DXT2:
        case D3DDDIFMT_DXT3:
        case D3DDDIFMT_DXT4:
        case D3DDDIFMT_DXT5:
        case D3DDDIFMT_VERTEXDATA:
        case D3DDDIFMT_INDEX16: /* <- yes, dx runtime treats it as such */
            return 8;
        case D3DDDIFMT_INDEX32:
            return 8;
        case D3DDDIFMT_R32F:
            return 32;
        case D3DDDIFMT_R16F:
            return 16;
        case D3DDDIFMT_YUY2: /* 4 bytes per 2 pixels. */
        case NEMUWDDM_D3DDDIFORMAT_FROM_FOURCC('Y', 'V', '1', '2'):
            return 16;
        default:
            AssertBreakpoint();
            return 0;
    }
}

DECLINLINE(uint32_t) nemuWddmFormatToFourcc(D3DDDIFORMAT enmFormat)
{
    uint32_t uFormat = (uint32_t)enmFormat;
    /* assume that in case both four bytes are non-zero, this is a fourcc */
    if ((uFormat & 0xff000000)
            && (uFormat & 0x00ff0000)
            && (uFormat & 0x0000ff00)
            && (uFormat & 0x000000ff)
            )
        return uFormat;
    return 0;
}

#define NEMUWDDM_ROUNDBOUND(_v, _b) (((_v) + ((_b) - 1)) & ~((_b) - 1))

DECLINLINE(UINT) nemuWddmCalcOffXru(UINT w, D3DDDIFORMAT enmFormat)
{
    switch (enmFormat)
    {
        /* pitch for the DXT* (aka compressed) formats is the size in bytes of blocks that fill in an image width
         * i.e. each block decompressed into 4 x 4 pixels, so we have ((Width + 3) / 4) blocks for Width.
         * then each block has 64 bits (8 bytes) for DXT1 and 64+64 bits (16 bytes) for DXT2-DXT5, so.. : */
        case D3DDDIFMT_DXT1:
        {
            UINT Pitch = (w + 3) / 4; /* <- pitch size in blocks */
            Pitch *= 8;               /* <- pitch size in bytes */
            return Pitch;
        }
        case D3DDDIFMT_DXT2:
        case D3DDDIFMT_DXT3:
        case D3DDDIFMT_DXT4:
        case D3DDDIFMT_DXT5:
        {
            UINT Pitch = (w + 3) / 4; /* <- pitch size in blocks */
            Pitch *= 8;               /* <- pitch size in bytes */
            return Pitch;
        }
        default:
        {
            /* the default is just to calculate the pitch from bpp */
            UINT bpp = nemuWddmCalcBitsPerPixel(enmFormat);
            UINT Pitch = bpp * w;
            /* pitch is now in bits, translate in bytes */
            return NEMUWDDM_ROUNDBOUND(Pitch, 8) >> 3;
        }
    }
}

DECLINLINE(UINT) nemuWddmCalcOffXrd(UINT w, D3DDDIFORMAT enmFormat)
{
    switch (enmFormat)
    {
        /* pitch for the DXT* (aka compressed) formats is the size in bytes of blocks that fill in an image width
         * i.e. each block decompressed into 4 x 4 pixels, so we have ((Width + 3) / 4) blocks for Width.
         * then each block has 64 bits (8 bytes) for DXT1 and 64+64 bits (16 bytes) for DXT2-DXT5, so.. : */
        case D3DDDIFMT_DXT1:
        {
            UINT Pitch = w / 4; /* <- pitch size in blocks */
            Pitch *= 8;         /* <- pitch size in bytes */
            return Pitch;
        }
        case D3DDDIFMT_DXT2:
        case D3DDDIFMT_DXT3:
        case D3DDDIFMT_DXT4:
        case D3DDDIFMT_DXT5:
        {
            UINT Pitch = w / 4; /* <- pitch size in blocks */
            Pitch *= 16;               /* <- pitch size in bytes */
            return Pitch;
        }
        default:
        {
            /* the default is just to calculate the pitch from bpp */
            UINT bpp = nemuWddmCalcBitsPerPixel(enmFormat);
            UINT Pitch = bpp * w;
            /* pitch is now in bits, translate in bytes */
            return Pitch >> 3;
        }
    }
}

DECLINLINE(UINT) nemuWddmCalcHightPacking(D3DDDIFORMAT enmFormat)
{
    switch (enmFormat)
    {
        /* for the DXT* (aka compressed) formats each block is decompressed into 4 x 4 pixels,
         * so packing is 4
         */
        case D3DDDIFMT_DXT1:
        case D3DDDIFMT_DXT2:
        case D3DDDIFMT_DXT3:
        case D3DDDIFMT_DXT4:
        case D3DDDIFMT_DXT5:
            return 4;
        default:
            return 1;
    }
}

DECLINLINE(UINT) nemuWddmCalcOffYru(UINT height, D3DDDIFORMAT enmFormat)
{
    UINT packing = nemuWddmCalcHightPacking(enmFormat);
    /* round it up */
    return (height + packing - 1) / packing;
}

DECLINLINE(UINT) nemuWddmCalcOffYrd(UINT height, D3DDDIFORMAT enmFormat)
{
    UINT packing = nemuWddmCalcHightPacking(enmFormat);
    /* round it up */
    return height / packing;
}

DECLINLINE(UINT) nemuWddmCalcPitch(UINT w, D3DDDIFORMAT enmFormat)
{
    return nemuWddmCalcOffXru(w, enmFormat);
}

DECLINLINE(UINT) nemuWddmCalcWidthForPitch(UINT Pitch, D3DDDIFORMAT enmFormat)
{
    switch (enmFormat)
    {
        /* pitch for the DXT* (aka compressed) formats is the size in bytes of blocks that fill in an image width
         * i.e. each block decompressed into 4 x 4 pixels, so we have ((Width + 3) / 4) blocks for Width.
         * then each block has 64 bits (8 bytes) for DXT1 and 64+64 bits (16 bytes) for DXT2-DXT5, so.. : */
        case D3DDDIFMT_DXT1:
        {
            return (Pitch / 8) * 4;
        }
        case D3DDDIFMT_DXT2:
        case D3DDDIFMT_DXT3:
        case D3DDDIFMT_DXT4:
        case D3DDDIFMT_DXT5:
        {
            return (Pitch / 16) * 4;;
        }
        default:
        {
            /* the default is just to calculate it from bpp */
            UINT bpp = nemuWddmCalcBitsPerPixel(enmFormat);
            return (Pitch << 3) / bpp;
        }
    }
}

DECLINLINE(UINT) nemuWddmCalcNumRows(UINT top, UINT bottom, D3DDDIFORMAT enmFormat)
{
    Assert(bottom > top);
    top = top ? nemuWddmCalcOffYrd(top, enmFormat) : 0; /* <- just to optimize it a bit */
    bottom = nemuWddmCalcOffYru(bottom, enmFormat);
    return bottom - top;
}

DECLINLINE(UINT) nemuWddmCalcRowSize(UINT left, UINT right, D3DDDIFORMAT enmFormat)
{
    Assert(right > left);
    left = left ? nemuWddmCalcOffXrd(left, enmFormat) : 0; /* <- just to optimize it a bit */
    right = nemuWddmCalcOffXru(right, enmFormat);
    return right - left;
}

DECLINLINE(UINT) nemuWddmCalcSize(UINT pitch, UINT height, D3DDDIFORMAT enmFormat)
{
    UINT cRows = nemuWddmCalcNumRows(0, height, enmFormat);
    return pitch * cRows;
}

DECLINLINE(UINT) nemuWddmCalcOffXYrd(UINT x, UINT y, UINT pitch, D3DDDIFORMAT enmFormat)
{
    UINT offY = 0;
    if (y)
        offY = nemuWddmCalcSize(pitch, y, enmFormat);

    return offY + nemuWddmCalcOffXrd(x, enmFormat);
}

#define NEMUWDDM_ARRAY_MAXELEMENTSU32(_t) ((uint32_t)((UINT32_MAX) / sizeof (_t)))
#define NEMUWDDM_TRAILARRAY_MAXELEMENTSU32(_t, _af) ((uint32_t)(((~(0UL)) - (uint32_t)RT_OFFSETOF(_t, _af[0])) / RT_SIZEOFMEMB(_t, _af[0])))

#endif /* #ifndef ___NemuMPIf_h___ */
