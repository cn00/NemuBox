/* $Id: NemuDisp.h $ */

/** @file
 * Nemu XPDM Display driver
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

#ifndef NEMUDISP_H
#define NEMUDISP_H

#include "NemuDispInternal.h"
#include "NemuDispVrdpBmp.h"

/* VirtualBox display driver version, could be seen in Control Panel */
#define NEMUDISPDRIVERVERSION 0x01UL

#if (NEMUDISPDRIVERVERSION & (~0xFFUL))
#error NEMUDISPDRIVERVERSION can't be more than 0xFF
#endif

#define NEMUDISP_DEVICE_NAME L"NemuDisp"

/* Current mode info */
typedef struct _NEMUDISPCURRENTMODE
{
    ULONG ulIndex;                      /* miniport's video mode index */
    ULONG ulWidth, ulHeight;            /* visible screen width and height */
    ULONG ulBitsPerPel;                 /* number of bits per pel */
    LONG  lScanlineStride;              /* distance between scanlines */
    FLONG flMaskR, flMaskG, flMaskB;    /* RGB mask */
    ULONG ulPaletteShift;               /* number of bits we have to shift 888 palette to match device palette */
} NEMUDISPCURRENTMODE, *PNEMUDISPCURRENTMODE;

/* Pointer related info */
typedef struct _NEMUDISPPOINTERINFO
{
    VIDEO_POINTER_CAPABILITIES caps;    /* Pointer capabilities */
    PVIDEO_POINTER_ATTRIBUTES pAttrs;   /* Preallocated buffer to pass pointer shape to miniport driver */
    DWORD  cbAttrs;                     /* Size of pAttrs buffer */
    POINTL orgHotSpot;                  /* Hot spot origin */
} NEMUDISPPOINTERINFO, *PNEMUDISPPOINTERINFO;

/* Surface info */
typedef struct _NEMUDISPSURF
{
    HBITMAP  hBitmap;        /* GDI's handle to framebuffer bitmap */
    SURFOBJ* psoBitmap;      /* lock pointer to framebuffer bitmap */
    HSURF    hSurface;       /* GDI's handle to framebuffer device-managed surface */
    ULONG    ulFormat;       /* Bitmap format, one of BMF_XXBPP */
} NEMUDISPSURF, *PNEMUDISPSURF;

/* VRAM Layout */
typedef struct _NEMUDISPVRAMLAYOUT
{
    ULONG cbVRAM;

    ULONG offFramebuffer, cbFramebuffer;
    ULONG offDDrawHeap, cbDDrawHeap;
    ULONG offVBVABuffer, cbVBVABuffer;
    ULONG offDisplayInfo, cbDisplayInfo;
} NEMUDISPVRAMLAYOUT;

/* HGSMI info */
typedef struct _NEMUDISPHGSMIINFO
{
    BOOL bSupported;               /* HGSMI is supported and enabled */

    HGSMIQUERYCALLBACKS mp;        /* HGSMI miniport's callbacks and context */
    HGSMIGUESTCOMMANDCONTEXT ctx;  /* HGSMI guest context */
} NEMUDISPHGSMIINFO;

/* Saved screen bits information. */
typedef struct _SSB
{
    ULONG ident;   /* 1 based index in the stack = the handle returned by NemuDispDrvSaveScreenBits (SS_SAVE) */
    BYTE *pBuffer; /* Buffer where screen bits are saved. */
} SSB;

#ifdef NEMU_WITH_DDRAW
/* DirectDraw surface lock information */
typedef struct _NEMUDDLOCKINFO
{
    BOOL bLocked;
    RECTL rect;
} NEMUDDLOCKINFO;
#endif

/* Structure holding driver private device info. */
typedef struct _NEMUDISPDEV
{
    HANDLE hDriver;                          /* Display device handle which was passed to NemuDispDrvEnablePDEV */
    HDEV   hDevGDI;                          /* GDI's handle for PDEV created in NemuDispDrvEnablePDEV */

    NEMUDISPCURRENTMODE mode;                /* Current device mode */
    ULONG iDevice;                           /* Miniport's device index */
    POINTL orgDev;                           /* Device origin for DualView (0,0 is primary) */
    POINTL orgDisp;                          /* Display origin in virtual desktop, NT4 only */

    NEMUDISPPOINTERINFO pointer;             /* Pointer info */

    HPALETTE hDefaultPalette;                /* Default palette handle */
    PALETTEENTRY *pPalette;                  /* Palette entries for device managed palette */

    NEMUDISPSURF surface;                    /* Device surface */
    FLONG flDrawingHooks;                    /* Enabled drawing hooks */

    VIDEO_MEMORY_INFORMATION memInfo;        /* Mapped Framebuffer/vram info */
    NEMUDISPVRAMLAYOUT layout;               /* VRAM layout information */

    NEMUDISPHGSMIINFO hgsmi;                 /* HGSMI Info */
    HGSMIQUERYCPORTPROCS vpAPI;              /* Video Port API callbacks and miniport's context */

    VBVABUFFERCONTEXT vbvaCtx;               /* VBVA context */
    VRDPBC            vrdpCache;             /* VRDP bitmap cache */

    ULONG cSSB;                              /* Number of active saved screen bits records in the following array. */
    SSB aSSB[4];                             /* LIFO type stack for saved screen areas. */

#ifdef NEMU_WITH_DDRAW
    NEMUDDLOCKINFO ddpsLock;                 /* Primary surface DirectDraw lock information */
#endif

#ifdef NEMU_WITH_VIDEOHWACCEL
    NEMUDISPVHWAINFO  vhwa;                  /* VHWA Info */
#endif

    BOOL bBitmapCacheDisabled;
} NEMUDISPDEV, *PNEMUDISPDEV;

/* -------------------- Driver callbacks -------------------- */
RT_C_DECLS_BEGIN
ULONG APIENTRY DriverEntry(IN PVOID Context1, IN PVOID Context2);
RT_C_DECLS_END

DHPDEV APIENTRY NemuDispDrvEnablePDEV(DEVMODEW *pdm, LPWSTR pwszLogAddress,
                                      ULONG cPat, HSURF *phsurfPatterns,
                                      ULONG cjCaps, ULONG *pdevcaps,
                                      ULONG cjDevInfo, DEVINFO  *pdi,
                                      HDEV  hdev, PWSTR pwszDeviceName, HANDLE hDriver);
VOID APIENTRY NemuDispDrvCompletePDEV(DHPDEV dhpdev, HDEV hdev);
VOID APIENTRY NemuDispDrvDisablePDEV(DHPDEV dhpdev);
HSURF APIENTRY NemuDispDrvEnableSurface(DHPDEV dhpdev);
VOID APIENTRY NemuDispDrvDisableSurface(DHPDEV dhpdev);

BOOL APIENTRY NemuDispDrvLineTo(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo,
                                LONG x1, LONG y1, LONG x2, LONG y2, RECTL *prclBounds, MIX mix);
BOOL APIENTRY NemuDispDrvStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, XFORMOBJ *pxo,
                                    BRUSHOBJ  *pbo, POINTL *pptlBrushOrg, LINEATTRS *plineattrs, MIX mix);

BOOL APIENTRY NemuDispDrvFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg,
                                  MIX mix, FLONG flOptions);
BOOL APIENTRY NemuDispDrvPaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix);

BOOL APIENTRY NemuDispDrvRealizeBrush(BRUSHOBJ *pbo, SURFOBJ *psoTarget, SURFOBJ *psoPattern, SURFOBJ *psoMask,
                                      XLATEOBJ *pxlo, ULONG iHatch);
ULONG APIENTRY NemuDispDrvDitherColor(DHPDEV dhpdev, ULONG iMode, ULONG rgb, ULONG *pul);

BOOL APIENTRY NemuDispDrvBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                                RECTL *prclTrg, POINTL *pptlSrc, POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush,
                                ROP4 rop4);
BOOL APIENTRY NemuDispDrvStretchBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                                    COLORADJUSTMENT *pca, POINTL *pptlHTOrg, RECTL *prclDest, RECTL *prclSrc,
                                    POINTL *pptlMask, ULONG iMode);
BOOL APIENTRY NemuDispDrvCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc, CLIPOBJ *pco, XLATEOBJ *pxlo,
                                  RECTL *prclDest, POINTL *pptlSrc);

ULONG APIENTRY NemuDispDrvSetPointerShape(SURFOBJ *pso, SURFOBJ *psoMask, SURFOBJ *psoColor, XLATEOBJ *pxlo,
                                          LONG xHot, LONG yHot, LONG x, LONG y, RECTL *prcl, FLONG fl);
VOID APIENTRY NemuDispDrvMovePointer(SURFOBJ *pso, LONG x, LONG y, RECTL *prcl);

BOOL APIENTRY NemuDispDrvAssertMode(DHPDEV dhpdev, BOOL bEnable);
VOID APIENTRY NemuDispDrvDisableDriver();
BOOL APIENTRY NemuDispDrvTextOut(SURFOBJ *pso, STROBJ *pstro, FONTOBJ *pfo, CLIPOBJ *pco,
                                 RECTL *prclExtra, RECTL *prclOpaque, BRUSHOBJ *pboFore,
                                 BRUSHOBJ *pboOpaque, POINTL *pptlOrg, MIX mix);
BOOL APIENTRY NemuDispDrvSetPalette(DHPDEV dhpdev, PALOBJ *ppalo, FLONG fl, ULONG iStart, ULONG cColors);
ULONG APIENTRY NemuDispDrvEscape(SURFOBJ *pso, ULONG iEsc, ULONG cjIn, PVOID pvIn, ULONG cjOut, PVOID pvOut);
ULONG_PTR APIENTRY NemuDispDrvSaveScreenBits(SURFOBJ *pso, ULONG iMode, ULONG_PTR ident, RECTL *prcl);
ULONG APIENTRY NemuDispDrvGetModes(HANDLE hDriver, ULONG cjSize, DEVMODEW *pdm);
BOOL APIENTRY NemuDispDrvOffset(SURFOBJ* pso, LONG x, LONG y, FLONG flReserved);

VOID APIENTRY NemuDispDrvNotify(SURFOBJ *pso, ULONG iType, PVOID pvData);

#ifdef NEMU_WITH_DDRAW
BOOL APIENTRY NemuDispDrvGetDirectDrawInfo(DHPDEV dhpdev, DD_HALINFO *pHalInfo, DWORD *pdwNumHeaps,
                                           VIDEOMEMORY *pvmList, DWORD *pdwNumFourCCCodes, DWORD *pdwFourCC);
BOOL APIENTRY NemuDispDrvEnableDirectDraw(DHPDEV dhpdev, DD_CALLBACKS *pCallBacks,
                                          DD_SURFACECALLBACKS *pSurfaceCallBacks,
                                          DD_PALETTECALLBACKS *pPaletteCallBacks);
VOID APIENTRY NemuDispDrvDisableDirectDraw(DHPDEV  dhpdev);
HBITMAP APIENTRY NemuDispDrvDeriveSurface(DD_DIRECTDRAW_GLOBAL *pDirectDraw, DD_SURFACE_LOCAL *pSurface);
#endif /*#ifdef NEMU_WITH_DDRAW*/

/* -------------------- Internal helpers -------------------- */
DECLINLINE(SURFOBJ) *getSurfObj(SURFOBJ *pso)
{
    if (pso)
    {
        PNEMUDISPDEV pDev = (PNEMUDISPDEV)pso->dhpdev;

        if (pDev && pDev->surface.psoBitmap && pso->hsurf == pDev->surface.hSurface)
        {
            /* Convert the device PSO to the bitmap PSO which can be passed to Eng*. */
            pso = pDev->surface.psoBitmap;
        }
    }

    return pso;
}

#endif /*NEMUDISP_H*/
