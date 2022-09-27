/* $Id: nemuvideo.c $ */
/** @file
 * Linux Additions X11 graphics driver
 */

/*
 * Copyright (C) 2006-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 * --------------------------------------------------------------------
 *
 * This code is based on the X.Org VESA driver with the following copyrights:
 *
 * Copyright (c) 2000 by Conectiva S.A. (http://www.conectiva.com)
 * Copyright 2008 Red Hat, Inc.
 * Copyright 2012 Red Hat, Inc.
 *
 * and the following permission notice (not all original sourse files include
 * the last paragraph):
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * CONECTIVA LINUX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of Conectiva Linux shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from
 * Conectiva Linux.
 *
 * Authors: Paulo César Pereira de Andrade <pcpa@conectiva.com.br>
 *          David Dawes <dawes@xfree86.org>
 *          Adam Jackson <ajax@redhat.com>
 *          Dave Airlie <airlied@redhat.com>
 */

#include "nemuvideo.h"
#include <Nemu/NemuGuest.h>
#include <Nemu/NemuGuestLib.h>
#include <Nemu/Hardware/NemuVideoVBE.h>
#include "version-generated.h"
#include "product-generated.h"
#include "revision-generated.h"

/* Basic definitions and functions needed by all drivers. */
#include "xf86.h"
/* For video memory mapping. */
#include "xf86_OSproc.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
/* PCI resources. */
# include "xf86Resources.h"
#endif
/* Generic server linear frame-buffer APIs. */
#include "fb.h"
/* Colormap and visual handling. */
#include "micmap.h"
#include "xf86cmap.h"
/* ShadowFB support */
#include "shadowfb.h"
/* VGA hardware functions for setting and restoring text mode */
#include "vgaHW.h"
#ifdef NEMU_DRI
# include "xf86drm.h"
# include "xf86drmMode.h"
#endif
#ifdef NEMUVIDEO_13
/* X.org 1.3+ mode setting */
# define _HAVE_STRING_ARCH_strsep /* bits/string2.h, __strsep_1c. */
# include "xf86Crtc.h"
# include "xf86Modes.h"
/* For xf86RandR12GetOriginalVirtualSize(). */
# include "xf86RandR12.h"
#endif
/* For setting the root window property. */
#include "property.h"
#include "X11/Xatom.h"

#ifdef XORG_7X
# include <stdlib.h>
# include <string.h>
#endif

/* Mandatory functions */

static const OptionInfoRec * NEMUAvailableOptions(int chipid, int busid);
static void NEMUIdentify(int flags);
#ifndef PCIACCESS
static Bool NEMUProbe(DriverPtr drv, int flags);
#else
static Bool NEMUPciProbe(DriverPtr drv, int entity_num,
     struct pci_device *dev, intptr_t match_data);
#endif
static Bool NEMUPreInit(ScrnInfoPtr pScrn, int flags);
static Bool NEMUScreenInit(ScreenPtr pScreen, int argc, char **argv);
static Bool NEMUEnterVT(ScrnInfoPtr pScrn);
static void NEMULeaveVT(ScrnInfoPtr pScrn);
static Bool NEMUCloseScreen(ScreenPtr pScreen);
static Bool NEMUSaveScreen(ScreenPtr pScreen, int mode);
static Bool NEMUSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr pMode);
static void NEMUAdjustFrame(ScrnInfoPtr pScrn, int x, int y);
static void NEMUFreeScreen(ScrnInfoPtr pScrn);
static void NEMUDisplayPowerManagementSet(ScrnInfoPtr pScrn, int mode,
                                          int flags);

/* locally used functions */
static Bool NEMUMapVidMem(ScrnInfoPtr pScrn);
static void NEMUUnmapVidMem(ScrnInfoPtr pScrn);
static void NEMUSaveMode(ScrnInfoPtr pScrn);
static void NEMURestoreMode(ScrnInfoPtr pScrn);
static void setSizesAndCursorIntegration(ScrnInfoPtr pScrn, bool fScreenInitTime);

#ifndef XF86_SCRN_INTERFACE
# define xf86ScreenToScrn(pScreen) xf86Screens[(pScreen)->myNum]
# define xf86ScrnToScreen(pScrn) screenInfo.screens[(pScrn)->scrnIndex]
#endif

static inline void NEMUSetRec(ScrnInfoPtr pScrn)
{
    if (!pScrn->driverPrivate)
    {
        NEMUPtr pNemu = (NEMUPtr)xnfcalloc(sizeof(NEMURec), 1);
        pScrn->driverPrivate = pNemu;
#if defined(NEMUVIDEO_13) && defined(RT_OS_LINUX)
        pNemu->fdACPIDevices = -1;
#endif
    }
}

enum GenericTypes
{
    CHIP_NEMU_GENERIC
};

#ifdef PCIACCESS
static const struct pci_id_match nemu_device_match[] = {
    {
        NEMU_VENDORID, NEMU_DEVICEID, PCI_MATCH_ANY, PCI_MATCH_ANY,
        0, 0, 0
    },

    { 0, 0, 0 },
};
#endif

/* Supported chipsets */
static SymTabRec NEMUChipsets[] =
{
    {NEMU_DEVICEID, "nemu"},
    {-1,            NULL}
};

static PciChipsets NEMUPCIchipsets[] = {
  { NEMU_DEVICEID, NEMU_DEVICEID, RES_SHARED_VGA },
  { -1,            -1,            RES_UNDEFINED },
};

/*
 * This contains the functions needed by the server after loading the
 * driver module.  It must be supplied, and gets added the driver list by
 * the Module Setup function in the dynamic case.  In the static case a
 * reference to this is compiled in, and this requires that the name of
 * this DriverRec be an upper-case version of the driver name.
 */

#ifdef XORG_7X
_X_EXPORT
#endif
DriverRec NEMUVIDEO = {
    NEMU_VERSION,
    NEMU_DRIVER_NAME,
    NEMUIdentify,
#ifdef PCIACCESS
    NULL,
#else
    NEMUProbe,
#endif
    NEMUAvailableOptions,
    NULL,
    0,
#ifdef XORG_7X
    NULL,
#endif
#ifdef PCIACCESS
    nemu_device_match,
    NEMUPciProbe
#endif
};

/* No options for now */
static const OptionInfoRec NEMUOptions[] = {
    { -1, NULL, OPTV_NONE, {0}, FALSE }
};

#ifndef XORG_7X
/*
 * List of symbols from other modules that this module references.  This
 * list is used to tell the loader that it is OK for symbols here to be
 * unresolved providing that it hasn't been told that they haven't been
 * told that they are essential via a call to xf86LoaderReqSymbols() or
 * xf86LoaderReqSymLists().  The purpose is this is to avoid warnings about
 * unresolved symbols that are not required.
 */
static const char *fbSymbols[] = {
    "fbPictureInit",
    "fbScreenInit",
    NULL
};

static const char *shadowfbSymbols[] = {
    "ShadowFBInit2",
    NULL
};

static const char *ramdacSymbols[] = {
    "xf86DestroyCursorInfoRec",
    "xf86InitCursor",
    "xf86CreateCursorInfoRec",
    NULL
};

static const char *vgahwSymbols[] = {
    "vgaHWFreeHWRec",
    "vgaHWGetHWRec",
    "vgaHWGetIOBase",
    "vgaHWGetIndex",
    "vgaHWRestore",
    "vgaHWSave",
    "vgaHWSetStdFuncs",
    NULL
};
#endif /* !XORG_7X */

/** Resize the virtual framebuffer. */
static Bool adjustScreenPixmap(ScrnInfoPtr pScrn, int width, int height)
{
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    int adjustedWidth = pScrn->bitsPerPixel == 16 ? (width + 1) & ~1 : width;
    int cbLine = adjustedWidth * pScrn->bitsPerPixel / 8;
    PixmapPtr pPixmap;

    TRACE_LOG("width=%d, height=%d\n", width, height);
    VBVXASSERT(width >= 0 && height >= 0, ("Invalid negative width (%d) or height (%d)\n", width, height));
    if (pScreen == NULL)  /* Not yet initialised. */
        return TRUE;
    pPixmap = pScreen->GetScreenPixmap(pScreen);
    VBVXASSERT(pPixmap != NULL, ("Failed to get the screen pixmap.\n"));
    TRACE_LOG("pPixmap=%p adjustedWidth=%d height=%d pScrn->depth=%d pScrn->bitsPerPixel=%d cbLine=%d pNemu->base=%p pPixmap->drawable.width=%d pPixmap->drawable.height=%d\n",
              pPixmap, adjustedWidth, height, pScrn->depth, pScrn->bitsPerPixel, cbLine, pNemu->base, pPixmap->drawable.width,
              pPixmap->drawable.height);
    if (   adjustedWidth != pPixmap->drawable.width
        || height != pPixmap->drawable.height)
    {
        if (   adjustedWidth > NEMU_VIDEO_MAX_VIRTUAL || height > NEMU_VIDEO_MAX_VIRTUAL
            || (unsigned)cbLine * (unsigned)height >= pNemu->cbFBMax)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Virtual framebuffer %dx%d too large.  For information, video memory: %u Kb.\n",
                       adjustedWidth, height, (unsigned) pNemu->cbFBMax / 1024);
            return FALSE;
        }
        if (pScrn->vtSema)
            vbvxClearVRAM(pScrn, ((size_t)pScrn->virtualX) * pScrn->virtualY * (pScrn->bitsPerPixel / 8),
                          ((size_t)adjustedWidth) * height * (pScrn->bitsPerPixel / 8));
        pScreen->ModifyPixmapHeader(pPixmap, adjustedWidth, height, pScrn->depth, pScrn->bitsPerPixel, cbLine, pNemu->base);
    }
    pScrn->displayWidth = pScrn->virtualX = adjustedWidth;
    pScrn->virtualY = height;
#ifdef NEMU_DRI_OLD
    if (pNemu->useDRI)
        NEMUDRIUpdateStride(pScrn, pNemu);
#endif
    return TRUE;
}

/** Set a video mode to the hardware, RandR 1.1 version.  Since we no longer do
 * virtual frame buffers, adjust the screen pixmap dimensions to match.  The
 * "override" parameters are for when we received a mode hint while switched to
 * a virtual terminal.  In this case NemuClient will have told us about the
 * mode, but not yet been able to do a mode switch using RandR.  We solve this
 * by setting the requested mode to the host but keeping the virtual frame-
 * buffer matching what the X server expects. */
static void setModeRandR11(ScrnInfoPtr pScrn, DisplayModePtr pMode, bool fScreenInitTime, bool fEnterVTTime,
                           int cXOverRide, int cYOverRide)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    struct vbvxFrameBuffer frameBuffer = { 0, 0, pMode->HDisplay, pMode->VDisplay, pScrn->bitsPerPixel};
    int cXPhysical = cXOverRide > 0 ? min(cXOverRide, pMode->HDisplay) : pMode->HDisplay;
    int cYPhysical = cYOverRide > 0 ? min(cYOverRide, pMode->VDisplay) : pMode->VDisplay;

    pNemu->pScreens[0].aScreenLocation.cx = pMode->HDisplay;
    pNemu->pScreens[0].aScreenLocation.cy = pMode->VDisplay;
    if (fScreenInitTime)
    {
        /* The screen structure is not fully set up yet, so do not touch it. */
        pScrn->displayWidth = pScrn->virtualX = pMode->HDisplay;
        pScrn->virtualY = pMode->VDisplay;
    }
    else
    {
        xf86ScrnToScreen(pScrn)->width = pMode->HDisplay;
        xf86ScrnToScreen(pScrn)->height = pMode->VDisplay;
        /* This prevents a crash in CentOS 3.  I was unable to debug it to
         * satisfaction, partly due to the lack of symbols.  My guess is that
         * pScrn->ModifyPixmapHeader() expects certain things to be set up when
         * it sees pScrn->vtSema set to true which are not quite done at this
         * point of the VT switch. */
        if (fEnterVTTime)
            pScrn->vtSema = FALSE;
        adjustScreenPixmap(pScrn, pMode->HDisplay, pMode->VDisplay);
        if (fEnterVTTime)
            pScrn->vtSema = TRUE;
    }
    if (pMode->HDisplay != 0 && pMode->VDisplay != 0 && pScrn->vtSema)
        vbvxSetMode(pScrn, 0, cXPhysical, cYPhysical, 0, 0, true, true, &frameBuffer);
    pScrn->currentMode = pMode;
}

#ifdef NEMUVIDEO_13
/* X.org 1.3+ mode-setting support ******************************************/

/** Set a video mode to the hardware, RandR 1.2 version.  If this is the first
 * screen, re-set the current mode for all others (the offset for the first
 * screen is always treated as zero by the hardware, so all other screens need
 * to be changed to compensate for any changes!).  The mode to set is taken
 * from the X.Org Crtc structure. */
static void setModeRandR12(ScrnInfoPtr pScrn, unsigned cScreen)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    unsigned i;
    struct vbvxFrameBuffer frameBuffer = { pNemu->pScreens[0].paCrtcs->x, pNemu->pScreens[0].paCrtcs->y, pScrn->virtualX,
                                           pScrn->virtualY, pScrn->bitsPerPixel };
    unsigned cFirst = cScreen;
    unsigned cLast = cScreen != 0 ? cScreen + 1 : pNemu->cScreens;
    int originalX, originalY;

    /* Check that this code cannot trigger the resizing bug in X.Org Server 1.3.
     * See the work-around in ScreenInit. */
    xf86RandR12GetOriginalVirtualSize(pScrn, &originalX, &originalY);
    VBVXASSERT(originalX == NEMU_VIDEO_MAX_VIRTUAL && originalY == NEMU_VIDEO_MAX_VIRTUAL, ("OriginalSize=%dx%d",
               originalX, originalY));
    for (i = cFirst; i < cLast; ++i)
        if (pNemu->pScreens[i].paCrtcs->mode.HDisplay != 0 && pNemu->pScreens[i].paCrtcs->mode.VDisplay != 0 && pScrn->vtSema)
            vbvxSetMode(pScrn, i, pNemu->pScreens[i].paCrtcs->mode.HDisplay, pNemu->pScreens[i].paCrtcs->mode.VDisplay,
                        pNemu->pScreens[i].paCrtcs->x, pNemu->pScreens[i].paCrtcs->y, pNemu->pScreens[i].fPowerOn,
                        pNemu->pScreens[i].paOutputs->status == XF86OutputStatusConnected, &frameBuffer);
}

/** Wrapper around setModeRandR12() to avoid exposing non-obvious semantics.
 */
static void setAllModesRandR12(ScrnInfoPtr pScrn)
{
    setModeRandR12(pScrn, 0);
}

/* For descriptions of these functions and structures, see
   hw/xfree86/modes/xf86Crtc.h and hw/xfree86/modes/xf86Modes.h in the
   X.Org source tree. */

static Bool nemu_config_resize(ScrnInfoPtr pScrn, int cw, int ch)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    Bool rc;
    unsigned i;

    TRACE_LOG("width=%d, height=%d\n", cw, ch);
    rc = adjustScreenPixmap(pScrn, cw, ch);
    /* Power-on all screens (the server expects this) and set the new pitch to them. */
    for (i = 0; i < pNemu->cScreens; ++i)
        pNemu->pScreens[i].fPowerOn = true;
    setAllModesRandR12(pScrn);
    vbvxSetSolarisMouseRange(cw, ch);
    return rc;
}

static const xf86CrtcConfigFuncsRec NEMUCrtcConfigFuncs = {
    nemu_config_resize
};

static void
nemu_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    unsigned cDisplay = (uintptr_t)crtc->driver_private;

    TRACE_LOG("mode=%d\n", mode);
    pNemu->pScreens[cDisplay].fPowerOn = (mode != DPMSModeOff);
    setModeRandR12(pScrn, cDisplay);
}

static Bool
nemu_crtc_lock (xf86CrtcPtr crtc)
{ (void) crtc; return FALSE; }


/* We use this function to check whether the X server owns the active virtual
 * terminal before attempting a mode switch, since the RandR extension isn't
 * very dilligent here, which can mean crashes if we are unlucky.  This is
 * not the way it the function is intended - it is meant for reporting modes
 * which the hardware can't handle.  I hope that this won't confuse any clients
 * connecting to us. */
static Bool
nemu_crtc_mode_fixup (xf86CrtcPtr crtc, DisplayModePtr mode,
                      DisplayModePtr adjusted_mode)
{ (void) crtc; (void) mode; (void) adjusted_mode; return TRUE; }

static void
nemu_crtc_stub (xf86CrtcPtr crtc)
{ (void) crtc; }

static void
nemu_crtc_mode_set (xf86CrtcPtr crtc, DisplayModePtr mode,
                    DisplayModePtr adjusted_mode, int x, int y)
{
    (void) mode;
    NEMUPtr pNemu = NEMUGetRec(crtc->scrn);
    unsigned cDisplay = (uintptr_t)crtc->driver_private;

    TRACE_LOG("name=%s, HDisplay=%d, VDisplay=%d, x=%d, y=%d\n", adjusted_mode->name,
           adjusted_mode->HDisplay, adjusted_mode->VDisplay, x, y);
    pNemu->pScreens[cDisplay].fPowerOn = true;
    pNemu->pScreens[cDisplay].aScreenLocation.cx = adjusted_mode->HDisplay;
    pNemu->pScreens[cDisplay].aScreenLocation.cy = adjusted_mode->VDisplay;
    pNemu->pScreens[cDisplay].aScreenLocation.x = x;
    pNemu->pScreens[cDisplay].aScreenLocation.y = y;
    setModeRandR12(crtc->scrn, cDisplay);
}

static void
nemu_crtc_gamma_set (xf86CrtcPtr crtc, CARD16 *red,
                     CARD16 *green, CARD16 *blue, int size)
{ (void) crtc; (void) red; (void) green; (void) blue; (void) size; }

static void *
nemu_crtc_shadow_allocate (xf86CrtcPtr crtc, int width, int height)
{ (void) crtc; (void) width; (void) height; return NULL; }

static const xf86CrtcFuncsRec NEMUCrtcFuncs = {
    .dpms = nemu_crtc_dpms,
    .save = NULL, /* These two are never called by the server. */
    .restore = NULL,
    .lock = nemu_crtc_lock,
    .unlock = NULL, /* This will not be invoked if lock returns FALSE. */
    .mode_fixup = nemu_crtc_mode_fixup,
    .prepare = nemu_crtc_stub,
    .mode_set = nemu_crtc_mode_set,
    .commit = nemu_crtc_stub,
    .gamma_set = nemu_crtc_gamma_set,
    .shadow_allocate = nemu_crtc_shadow_allocate,
    .shadow_create = NULL, /* These two should not be invoked if allocate
                              returns NULL. */
    .shadow_destroy = NULL,
    .set_cursor_colors = NULL, /* We are still using the old cursor API. */
    .set_cursor_position = NULL,
    .show_cursor = NULL,
    .hide_cursor = NULL,
    .load_cursor_argb = NULL,
    .destroy = nemu_crtc_stub
};

static void
nemu_output_stub (xf86OutputPtr output)
{ (void) output; }

static void
nemu_output_dpms (xf86OutputPtr output, int mode)
{
    (void)output; (void)mode;
}

static int
nemu_output_mode_valid (xf86OutputPtr output, DisplayModePtr mode)
{
    return MODE_OK;
}

static Bool
nemu_output_mode_fixup (xf86OutputPtr output, DisplayModePtr mode,
                        DisplayModePtr adjusted_mode)
{ (void) output; (void) mode; (void) adjusted_mode; return TRUE; }

static void
nemu_output_mode_set (xf86OutputPtr output, DisplayModePtr mode,
                        DisplayModePtr adjusted_mode)
{ (void) output; (void) mode; (void) adjusted_mode; }

/* A virtual monitor is always connected. */
static xf86OutputStatus
nemu_output_detect (xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    uint32_t iScreen = (uintptr_t)output->driver_private;
    return   pNemu->pScreens[iScreen].afConnected
           ? XF86OutputStatusConnected : XF86OutputStatusDisconnected;
}

static DisplayModePtr nemu_output_add_mode(NEMUPtr pNemu, DisplayModePtr *pModes, const char *pszName, int x, int y,
                                           Bool isPreferred, Bool isUserDef)
{
    TRACE_LOG("pszName=%s, x=%d, y=%d\n", pszName ? pszName : "(null)", x, y);
    DisplayModePtr pMode = xnfcalloc(1, sizeof(DisplayModeRec));
    int cRefresh = 60;

    pMode->status        = MODE_OK;
    /* We don't ask the host whether it likes user defined modes,
     * as we assume that the user really wanted that mode. */
    pMode->type          = isUserDef ? M_T_USERDEF : M_T_BUILTIN;
    if (isPreferred)
        pMode->type     |= M_T_PREFERRED;
    /* Older versions of Nemu only support screen widths which are a multiple
     * of 8 */
    if (pNemu->fAnyX)
        pMode->HDisplay  = x;
    else
        pMode->HDisplay  = x & ~7;
    pMode->HSyncStart    = pMode->HDisplay + 2;
    pMode->HSyncEnd      = pMode->HDisplay + 4;
    pMode->HTotal        = pMode->HDisplay + 6;
    pMode->VDisplay      = y;
    pMode->VSyncStart    = pMode->VDisplay + 2;
    pMode->VSyncEnd      = pMode->VDisplay + 4;
    pMode->VTotal        = pMode->VDisplay + 6;
    pMode->Clock         = pMode->HTotal * pMode->VTotal * cRefresh / 1000; /* kHz */
    if (NULL == pszName) {
        xf86SetModeDefaultName(pMode);
    } else {
        pMode->name          = xnfstrdup(pszName);
    }
    *pModes = xf86ModesAdd(*pModes, pMode);
    return pMode;
}

static DisplayModePtr
nemu_output_get_modes (xf86OutputPtr output)
{
    unsigned cIndex = 0;
    DisplayModePtr pModes = NULL;
    ScrnInfoPtr pScrn = output->scrn;
    NEMUPtr pNemu = NEMUGetRec(pScrn);

    TRACE_ENTRY();
    uint32_t iScreen = (uintptr_t)output->driver_private;
    nemu_output_add_mode(pNemu, &pModes, NULL,
                         RT_CLAMP(pNemu->pScreens[iScreen].aPreferredSize.cx, NEMU_VIDEO_MIN_SIZE, NEMU_VIDEO_MAX_VIRTUAL),
                         RT_CLAMP(pNemu->pScreens[iScreen].aPreferredSize.cy, NEMU_VIDEO_MIN_SIZE, NEMU_VIDEO_MAX_VIRTUAL),
                         TRUE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 2560, 1600, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 2560, 1440, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 2048, 1536, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 1920, 1600, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 1920, 1080, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 1680, 1050, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 1600, 1200, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 1400, 1050, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 1280, 1024, FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 1024, 768,  FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 800,  600,  FALSE, FALSE);
    nemu_output_add_mode(pNemu, &pModes, NULL, 640,  480,  FALSE, FALSE);
    TRACE_EXIT();
    return pModes;
}

static const xf86OutputFuncsRec NEMUOutputFuncs = {
    .create_resources = nemu_output_stub,
    .dpms = nemu_output_dpms,
    .save = NULL, /* These two are never called by the server. */
    .restore = NULL,
    .mode_valid = nemu_output_mode_valid,
    .mode_fixup = nemu_output_mode_fixup,
    .prepare = nemu_output_stub,
    .commit = nemu_output_stub,
    .mode_set = nemu_output_mode_set,
    .detect = nemu_output_detect,
    .get_modes = nemu_output_get_modes,
#ifdef RANDR_12_INTERFACE
     .set_property = NULL,
#endif
    .destroy = nemu_output_stub
};
#endif /* NEMUVIDEO_13 */

/* Module loader interface */
static MODULESETUPPROTO(nemuSetup);

static XF86ModuleVersionInfo nemuVersionRec =
{
    NEMU_DRIVER_NAME,
    NEMU_VENDOR,
    MODINFOSTRING1,
    MODINFOSTRING2,
#ifdef XORG_7X
    XORG_VERSION_CURRENT,
#else
    XF86_VERSION_CURRENT,
#endif
    1,                          /* Module major version. Xorg-specific */
    0,                          /* Module minor version. Xorg-specific */
    1,                          /* Module patchlevel. Xorg-specific */
    ABI_CLASS_VIDEODRV,         /* This is a video driver */
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0}
};

/*
 * This data is accessed by the loader.  The name must be the module name
 * followed by "ModuleData".
 */
#ifdef XORG_7X
_X_EXPORT
#endif
XF86ModuleData nemuvideoModuleData = { &nemuVersionRec, nemuSetup, NULL };

static pointer
nemuSetup(pointer Module, pointer Options, int *ErrorMajor, int *ErrorMinor)
{
    static Bool Initialised = FALSE;

    if (!Initialised)
    {
        Initialised = TRUE;
#ifdef PCIACCESS
        xf86AddDriver(&NEMUVIDEO, Module, HaveDriverFuncs);
#else
        xf86AddDriver(&NEMUVIDEO, Module, 0);
#endif
#ifndef XORG_7X
        LoaderRefSymLists(fbSymbols,
                          shadowfbSymbols,
                          ramdacSymbols,
                          vgahwSymbols,
                          NULL);
#endif
        xf86Msg(X_CONFIG, "Load address of symbol \"NEMUVIDEO\" is %p\n",
                (void *)&NEMUVIDEO);
        return (pointer)TRUE;
    }

    if (ErrorMajor)
        *ErrorMajor = LDR_ONCEONLY;
    return (NULL);
}


static const OptionInfoRec *
NEMUAvailableOptions(int chipid, int busid)
{
    return (NEMUOptions);
}

static void
NEMUIdentify(int flags)
{
    xf86PrintChipsets(NEMU_NAME, "guest driver for VirtualBox", NEMUChipsets);
}

#ifndef XF86_SCRN_INTERFACE
# define SCRNINDEXAPI(pfn) pfn ## Index
static Bool NEMUScreenInitIndex(int scrnIndex, ScreenPtr pScreen, int argc,
                                char **argv)
{ return NEMUScreenInit(pScreen, argc, argv); }

static Bool NEMUEnterVTIndex(int scrnIndex, int flags)
{ (void) flags; return NEMUEnterVT(xf86Screens[scrnIndex]); }

static void NEMULeaveVTIndex(int scrnIndex, int flags)
{ (void) flags; NEMULeaveVT(xf86Screens[scrnIndex]); }

static Bool NEMUCloseScreenIndex(int scrnIndex, ScreenPtr pScreen)
{ (void) scrnIndex; return NEMUCloseScreen(pScreen); }

static Bool NEMUSwitchModeIndex(int scrnIndex, DisplayModePtr pMode, int flags)
{ (void) flags; return NEMUSwitchMode(xf86Screens[scrnIndex], pMode); }

static void NEMUAdjustFrameIndex(int scrnIndex, int x, int y, int flags)
{ (void) flags; NEMUAdjustFrame(xf86Screens[scrnIndex], x, y); }

static void NEMUFreeScreenIndex(int scrnIndex, int flags)
{ (void) flags; NEMUFreeScreen(xf86Screens[scrnIndex]); }
# else
# define SCRNINDEXAPI(pfn) pfn
#endif /* XF86_SCRN_INTERFACE */

static void setScreenFunctions(ScrnInfoPtr pScrn, xf86ProbeProc pfnProbe)
{
    pScrn->driverVersion = NEMU_VERSION;
    pScrn->driverName    = NEMU_DRIVER_NAME;
    pScrn->name          = NEMU_NAME;
    pScrn->Probe         = pfnProbe;
    pScrn->PreInit       = NEMUPreInit;
    pScrn->ScreenInit    = SCRNINDEXAPI(NEMUScreenInit);
    pScrn->SwitchMode    = SCRNINDEXAPI(NEMUSwitchMode);
    pScrn->AdjustFrame   = SCRNINDEXAPI(NEMUAdjustFrame);
    pScrn->EnterVT       = SCRNINDEXAPI(NEMUEnterVT);
    pScrn->LeaveVT       = SCRNINDEXAPI(NEMULeaveVT);
    pScrn->FreeScreen    = SCRNINDEXAPI(NEMUFreeScreen);
}

/*
 * One of these functions is called once, at the start of the first server
 * generation to do a minimal probe for supported hardware.
 */

#ifdef PCIACCESS
static Bool
NEMUPciProbe(DriverPtr drv, int entity_num, struct pci_device *dev,
             intptr_t match_data)
{
    ScrnInfoPtr pScrn;

    TRACE_ENTRY();
    pScrn = xf86ConfigPciEntity(NULL, 0, entity_num, NEMUPCIchipsets,
                                NULL, NULL, NULL, NULL, NULL);
    if (pScrn != NULL) {
        NEMUPtr pNemu;

        NEMUSetRec(pScrn);
        pNemu = NEMUGetRec(pScrn);
        if (!pNemu)
            return FALSE;
        setScreenFunctions(pScrn, NULL);
        pNemu->pciInfo = dev;
    }

    TRACE_LOG("returning %s\n", BOOL_STR(pScrn != NULL));
    return (pScrn != NULL);
}
#endif

#ifndef PCIACCESS
static Bool
NEMUProbe(DriverPtr drv, int flags)
{
    Bool foundScreen = FALSE;
    int numDevSections;
    GDevPtr *devSections;

    /*
     * Find the config file Device sections that match this
     * driver, and return if there are none.
     */
    if ((numDevSections = xf86MatchDevice(NEMU_NAME,
                      &devSections)) <= 0)
    return (FALSE);

    /* PCI BUS */
    if (xf86GetPciVideoInfo())
    {
        int numUsed;
        int *usedChips;
        int i;
        numUsed = xf86MatchPciInstances(NEMU_NAME, NEMU_VENDORID,
                        NEMUChipsets, NEMUPCIchipsets,
                        devSections, numDevSections,
                        drv, &usedChips);
        if (numUsed > 0)
        {
            if (flags & PROBE_DETECT)
                foundScreen = TRUE;
            else
                for (i = 0; i < numUsed; i++)
                {
                    ScrnInfoPtr pScrn = NULL;
                    /* Allocate a ScrnInfoRec  */
                    if ((pScrn = xf86ConfigPciEntity(pScrn,0,usedChips[i],
                                     NEMUPCIchipsets,NULL,
                                     NULL,NULL,NULL,NULL)))
                    {
                        setScreenFunctions(pScrn, NEMUProbe);
                        foundScreen = TRUE;
                    }
                }
            free(usedChips);
        }
    }
    free(devSections);
    return (foundScreen);
}
#endif


/*
 * QUOTE from the XFree86 DESIGN document:
 *
 * The purpose of this function is to find out all the information
 * required to determine if the configuration is usable, and to initialise
 * those parts of the ScrnInfoRec that can be set once at the beginning of
 * the first server generation.
 *
 * (...)
 *
 * This includes probing for video memory, clocks, ramdac, and all other
 * HW info that is needed. It includes determining the depth/bpp/visual
 * and related info. It includes validating and determining the set of
 * video modes that will be used (and anything that is required to
 * determine that).
 *
 * This information should be determined in the least intrusive way
 * possible. The state of the HW must remain unchanged by this function.
 * Although video memory (including MMIO) may be mapped within this
 * function, it must be unmapped before returning.
 *
 * END QUOTE
 */

static Bool
NEMUPreInit(ScrnInfoPtr pScrn, int flags)
{
    NEMUPtr pNemu;
    Gamma gzeros = {0.0, 0.0, 0.0};
    rgb rzeros = {0, 0, 0};

    TRACE_ENTRY();
    /* Are we really starting the server, or is this just a dummy run? */
    if (flags & PROBE_DETECT)
        return (FALSE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "VirtualBox guest additions video driver version " NEMU_VERSION_STRING "r%d\n",
               NEMU_SVN_REV);

    /* Get our private data from the ScrnInfoRec structure. */
    NEMUSetRec(pScrn);
    pNemu = NEMUGetRec(pScrn);
    if (!pNemu)
        return FALSE;

    /* Entity information seems to mean bus information. */
    pNemu->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

    /* The ramdac module is needed for the hardware cursor. */
    if (!xf86LoadSubModule(pScrn, "ramdac"))
        return FALSE;

    /* The framebuffer module. */
    if (!xf86LoadSubModule(pScrn, "fb"))
        return (FALSE);

    if (!xf86LoadSubModule(pScrn, "shadowfb"))
        return FALSE;

    if (!xf86LoadSubModule(pScrn, "vgahw"))
        return FALSE;

#ifdef NEMU_DRI_OLD
    /* Load the dri module. */
    if (!xf86LoadSubModule(pScrn, "dri"))
        return FALSE;
#else
# ifdef NEMU_DRI
    /* Load the dri module. */
    if (!xf86LoadSubModule(pScrn, "dri2"))
        return FALSE;
# endif
#endif

#ifndef PCIACCESS
    if (pNemu->pEnt->location.type != BUS_PCI)
        return FALSE;

    pNemu->pciInfo = xf86GetPciInfoForEntity(pNemu->pEnt->index);
    pNemu->pciTag = pciTag(pNemu->pciInfo->bus,
                           pNemu->pciInfo->device,
                           pNemu->pciInfo->func);
#endif

    /* Set up our ScrnInfoRec structure to describe our virtual
       capabilities to X. */

    pScrn->chipset = "nemu";
    /** @note needed during colourmap initialisation */
    pScrn->rgbBits = 8;

    /* Let's create a nice, capable virtual monitor. */
    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->monitor->DDC = NULL;
    pScrn->monitor->nHsync = 1;
    pScrn->monitor->hsync[0].lo = 1;
    pScrn->monitor->hsync[0].hi = 10000;
    pScrn->monitor->nVrefresh = 1;
    pScrn->monitor->vrefresh[0].lo = 1;
    pScrn->monitor->vrefresh[0].hi = 100;

    pScrn->progClock = TRUE;

    /* Using the PCI information caused problems with non-powers-of-two
       sized video RAM configurations */
    pNemu->cbFBMax = NemuVideoGetVRAMSize();
    pScrn->videoRam = pNemu->cbFBMax / 1024;

    /* Check if the chip restricts horizontal resolution or not. */
    pNemu->fAnyX = NemuVideoAnyWidthAllowed();

    /* Set up clock information that will support all modes we need. */
    pScrn->clockRanges = xnfcalloc(sizeof(ClockRange), 1);
    pScrn->clockRanges->minClock = 1000;
    pScrn->clockRanges->maxClock = 1000000000;
    pScrn->clockRanges->clockIndex = -1;
    pScrn->clockRanges->ClockMulFactor = 1;
    pScrn->clockRanges->ClockDivFactor = 1;

    if (!xf86SetDepthBpp(pScrn, 24, 0, 0, Support32bppFb))
        return FALSE;
    /* We only support 16 and 24 bits depth (i.e. 16 and 32bpp) */
    if (pScrn->bitsPerPixel != 32 && pScrn->bitsPerPixel != 16)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "The Nemu additions only support 16 and 32bpp graphics modes\n");
        return FALSE;
    }
    xf86PrintDepthBpp(pScrn);
    nemuAddModes(pScrn);

#ifdef NEMUVIDEO_13
    pScrn->virtualX = NEMU_VIDEO_MAX_VIRTUAL;
    pScrn->virtualY = NEMU_VIDEO_MAX_VIRTUAL;
#else
    /* We don't validate with xf86ValidateModes and xf86PruneModes as we
     * already know what we like and what we don't. */

    pScrn->currentMode = pScrn->modes;

    /* Set the right virtual resolution. */
    pScrn->virtualX = pScrn->bitsPerPixel == 16 ? (pScrn->currentMode->HDisplay + 1) & ~1 : pScrn->currentMode->HDisplay;
    pScrn->virtualY = pScrn->currentMode->VDisplay;

#endif /* !NEMUVIDEO_13 */

    /* Needed before we initialise DRI. */
    pScrn->displayWidth = pScrn->virtualX;

    xf86PrintModes(pScrn);

    /* VGA hardware initialisation */
    if (!vgaHWGetHWRec(pScrn))
        return FALSE;
    /* Must be called before any VGA registers are saved or restored */
    vgaHWSetStdFuncs(VGAHWPTR(pScrn));
    vgaHWGetIOBase(VGAHWPTR(pScrn));

    /* Colour weight - we always call this, since we are always in
       truecolour. */
    if (!xf86SetWeight(pScrn, rzeros, rzeros))
        return (FALSE);

    /* visual init */
    if (!xf86SetDefaultVisual(pScrn, -1))
        return (FALSE);

    xf86SetGamma(pScrn, gzeros);

    /* Set the DPI.  Perhaps we should read this from the host? */
    xf86SetDpi(pScrn, 96, 96);

    if (pScrn->memPhysBase == 0) {
#ifdef PCIACCESS
        pScrn->memPhysBase = pNemu->pciInfo->regions[0].base_addr;
#else
        pScrn->memPhysBase = pNemu->pciInfo->memBase[0];
#endif
        pScrn->fbOffset = 0;
    }

    TRACE_EXIT();
    return (TRUE);
}

/**
 * Dummy function for setting the colour palette, which we actually never
 * touch.  However, the server still requires us to provide this.
 */
static void
nemuLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
          LOCO *colors, VisualPtr pVisual)
{
    (void)pScrn; (void) numColors; (void) indices; (void) colors;
    (void)pVisual;
}

/** Set the graphics and guest cursor support capabilities to the host if
 *  the user-space helper is running. */
static void updateGraphicsCapability(ScrnInfoPtr pScrn, Bool hasVT)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    size_t cData;
    int32_t *paData;
    int rc;

    if (pNemu->fHaveHGSMIModeHints)
        return;
    rc = vbvxGetIntegerPropery(pScrn, "NEMU_HAS_GRAPHICS", &cData, &paData);
    if (rc != VINF_SUCCESS || cData != 1)
        return;
    if (RT_BOOL(*paData) != hasVT)
    {
        uint32_t fFeatures;
        VbglR3SetGuestCaps(hasVT ? VMMDEV_GUEST_SUPPORTS_GRAPHICS : 0, hasVT ? 0 : VMMDEV_GUEST_SUPPORTS_GRAPHICS);
        rc = VbglR3GetMouseStatus(&fFeatures, NULL, NULL);
        fFeatures &= VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE | VMMDEV_MOUSE_NEW_PROTOCOL;
        if (RT_SUCCESS(rc))
            VbglR3SetMouseStatus(hasVT ? fFeatures : fFeatures | VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR);
    }
    *paData = hasVT;
}

#ifdef NEMUVIDEO_13

static void setVirtualSizeRandR12(ScrnInfoPtr pScrn, bool fScreenInitTime)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    unsigned i;
    unsigned cx = 0;
    unsigned cy = 0;

    for (i = 0; i < pNemu->cScreens; ++i)
    {
        if (   pNemu->fHaveHGSMIModeHints && pNemu->pScreens[i].afHaveLocation)
        {
            pNemu->pScreens[i].paCrtcs->x = pNemu->pScreens[i].aPreferredLocation.x;
            pNemu->pScreens[i].paCrtcs->y = pNemu->pScreens[i].aPreferredLocation.y;
        }
        if (   pNemu->pScreens[i].paOutputs->status == XF86OutputStatusConnected
            && pNemu->pScreens[i].paCrtcs->x + pNemu->pScreens[i].aPreferredSize.cx < NEMU_VIDEO_MAX_VIRTUAL
            && pNemu->pScreens[i].paCrtcs->y + pNemu->pScreens[i].aPreferredSize.cy < NEMU_VIDEO_MAX_VIRTUAL)
        {
            cx = max(cx, pNemu->pScreens[i].paCrtcs->x + pNemu->pScreens[i].aPreferredSize.cx);
            cy = max(cy, pNemu->pScreens[i].paCrtcs->y + pNemu->pScreens[i].aPreferredSize.cy);
        }
    }
    if (cx != 0 && cy != 0)
    {
        /* Do not set the virtual resolution in limited context as that can
         * cause problems setting up RandR 1.2 which needs it set to the
         * maximum size at this point. */
        if (!fScreenInitTime)
        {
            TRACE_LOG("cx=%u, cy=%u\n", cx, cy);
            xf86ScrnToScreen(pScrn)->width = cx;
            xf86ScrnToScreen(pScrn)->height = cy;
            xf86ScrnToScreen(pScrn)->mmWidth = cx * 254 / 960;
            xf86ScrnToScreen(pScrn)->mmHeight = cy * 254 / 960;
            adjustScreenPixmap(pScrn, cx, cy);
            vbvxSetSolarisMouseRange(cx, cy);
        }
    }
}

static void setScreenSizesRandR12(ScrnInfoPtr pScrn, bool fScreenInitTime)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    unsigned i;

    for (i = 0; i < pNemu->cScreens; ++i)
    {
        if (!pNemu->pScreens[i].afConnected)
            continue;
        /* The Crtc can get "unset" if the screen was disconnected previously.
         * I couldn't find an API to re-set it which did not have side-effects.
         */
        pNemu->pScreens[i].paOutputs->crtc = pNemu->pScreens[i].paCrtcs;
        xf86CrtcSetMode(pNemu->pScreens[i].paCrtcs, pNemu->pScreens[i].paOutputs->probed_modes, RR_Rotate_0,
                        pNemu->pScreens[i].paCrtcs->x, pNemu->pScreens[i].paCrtcs->y);
        if (!fScreenInitTime)
            RRCrtcNotify(pNemu->pScreens[i].paCrtcs->randr_crtc, pNemu->pScreens[i].paOutputs->randr_output->modes[0],
                         pNemu->pScreens[i].paCrtcs->x, pNemu->pScreens[i].paCrtcs->y, RR_Rotate_0,
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) >= 5
                         NULL,
#endif
                         1, &pNemu->pScreens[i].paOutputs->randr_output);
    }
}

static void setSizesRandR12(ScrnInfoPtr pScrn, bool fScreenInitTime)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);

    if (!fScreenInitTime)
    {
# if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) >= 5
        RRGetInfo(xf86ScrnToScreen(pScrn), TRUE);
# else
        RRGetInfo(xf86ScrnToScreen(pScrn));
# endif
    }
    setVirtualSizeRandR12(pScrn, fScreenInitTime);
    setScreenSizesRandR12(pScrn, fScreenInitTime);
    if (!fScreenInitTime)
    {
        /* We use RRScreenSizeSet() here and not RRScreenSizeNotify() because
         * the first also pushes the virtual screen size to the input driver.
         * We were doing this manually by setting screenInfo.width and height
         * and calling xf86UpdateDesktopDimensions() where appropriate, but this
         * failed on Ubuntu 12.04.0 due to a problematic X server back-port. */
        RRScreenSizeSet(xf86ScrnToScreen(pScrn), xf86ScrnToScreen(pScrn)->width, xf86ScrnToScreen(pScrn)->height,
                        xf86ScrnToScreen(pScrn)->mmWidth, xf86ScrnToScreen(pScrn)->mmHeight);
        RRTellChanged(xf86ScrnToScreen(pScrn));
    }
}

#else

static void setSizesRandR11(ScrnInfoPtr pScrn)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    DisplayModePtr pNewMode;

    pNewMode = pScrn->modes != pScrn->currentMode ? pScrn->modes : pScrn->modes->next;
    pNewMode->HDisplay = RT_CLAMP(pNemu->pScreens[0].aPreferredSize.cx, NEMU_VIDEO_MIN_SIZE, NEMU_VIDEO_MAX_VIRTUAL);
    pNewMode->VDisplay = RT_CLAMP(pNemu->pScreens[0].aPreferredSize.cy, NEMU_VIDEO_MIN_SIZE, NEMU_VIDEO_MAX_VIRTUAL);
}

#endif

static void setSizesAndCursorIntegration(ScrnInfoPtr pScrn, bool fScreenInitTime)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);

    TRACE_LOG("fScreenInitTime=%d\n", (int)fScreenInitTime);
#ifdef NEMUVIDEO_13
    setSizesRandR12(pScrn, fScreenInitTime);
#else
    setSizesRandR11(pScrn);
#endif
    /* This calls EnableDisableFBAccess(), so only use when switched in. */
    if (pScrn->vtSema)
        vbvxReprobeCursor(pScrn);
}

/* We update the size hints from the X11 property set by NemuClient every time
 * that the X server goes to sleep (to catch the property change request).
 * Although this is far more often than necessary it should not have real-life
 * performance consequences and allows us to simplify the code quite a bit. */
static void nemuBlockHandler(pointer pData, OSTimePtr pTimeout, pointer pReadmask)
{
    ScrnInfoPtr pScrn = (ScrnInfoPtr)pData;
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    bool fNeedUpdate = false;

    (void)pTimeout;
    (void)pReadmask;
    updateGraphicsCapability(pScrn, pScrn->vtSema);
    if (pScrn->vtSema)
        vbvxReadSizesAndCursorIntegrationFromHGSMI(pScrn, &fNeedUpdate);
    /* This has to be done even when we are switched out so that NemuClient can
     * set a mode using RandR without having to know the virtual terminal state.
     */
    if (ROOT_WINDOW(pScrn) != NULL)
        vbvxReadSizesAndCursorIntegrationFromProperties(pScrn, &fNeedUpdate);
    if (fNeedUpdate)
        setSizesAndCursorIntegration(pScrn, false);
}

/*
 * QUOTE from the XFree86 DESIGN document:
 *
 * This is called at the start of each server generation.
 *
 * (...)
 *
 * Decide which operations need to be placed under resource access
 * control. (...) Map any video memory or other memory regions. (...)
 * Save the video card state. (...) Initialise the initial video
 * mode.
 *
 * End QUOTE.
 */
static Bool NEMUScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    VisualPtr visual;

    TRACE_ENTRY();

    /* Initialise our guest library if possible: ignore failure. */
    VbglR3Init();
    if (!NEMUMapVidMem(pScrn))
        return (FALSE);

    /* save current video state */
    NEMUSaveMode(pScrn);

    /* mi layer - reset the visual list (?)*/
    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth, TrueColorMask,
                          pScrn->rgbBits, TrueColor))
        return (FALSE);
    if (!miSetPixmapDepths())
        return (FALSE);

#ifdef NEMU_DRI
    pNemu->useDRI = NEMUDRIScreenInit(pScrn, pScreen, pNemu);
# ifndef NEMU_DRI_OLD  /* DRI2 */
    if (pNemu->drmFD >= 0)
        /* Tell the kernel driver, if present, that we are taking over. */
        drmIoctl(pNemu->drmFD, NEMUVIDEO_IOCTL_DISABLE_HGSMI, NULL);
# endif
#endif

    if (!fbScreenInit(pScreen, pNemu->base,
                      pScrn->virtualX, pScrn->virtualY,
                      pScrn->xDpi, pScrn->yDpi,
                      pScrn->displayWidth, pScrn->bitsPerPixel))
        return (FALSE);

    /* Fixup RGB ordering */
    /** @note the X server uses this even in true colour. */
    visual = pScreen->visuals + pScreen->numVisuals;
    while (--visual >= pScreen->visuals) {
        if ((visual->class | DynamicClass) == DirectColor) {
            visual->offsetRed   = pScrn->offset.red;
            visual->offsetGreen = pScrn->offset.green;
            visual->offsetBlue  = pScrn->offset.blue;
            visual->redMask     = pScrn->mask.red;
            visual->greenMask   = pScrn->mask.green;
            visual->blueMask    = pScrn->mask.blue;
        }
    }

    /* must be after RGB ordering fixed */
    fbPictureInit(pScreen, 0, 0);

    xf86SetBlackWhitePixels(pScreen);
    pScrn->vtSema = TRUE;

#if defined(NEMUVIDEO_13) && defined(RT_OS_LINUX)
    vbvxSetUpLinuxACPI(pScreen);
#endif

    if (!NemuHGSMIIsSupported())
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Graphics device too old to support.\n");
        return FALSE;
    }
    vbvxSetUpHGSMIHeapInGuest(pNemu, pScrn->videoRam * 1024);
    pNemu->cScreens = NemuHGSMIGetMonitorCount(&pNemu->guestCtx);
    pNemu->pScreens = xnfcalloc(pNemu->cScreens, sizeof(*pNemu->pScreens));
    pNemu->paVBVAModeHints = xnfcalloc(pNemu->cScreens, sizeof(*pNemu->paVBVAModeHints));
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Requested monitor count: %u\n", pNemu->cScreens);
    nemuEnableVbva(pScrn);
    /* Set up the dirty rectangle handler.  It will be added into a function
     * chain and gets removed when the screen is cleaned up. */
    if (ShadowFBInit2(pScreen, NULL, vbvxHandleDirtyRect) != TRUE)
        return FALSE;
    NemuInitialiseSizeHints(pScrn);
    /* Get any screen size hints from HGSMI.  Do not yet try to access X11
     * properties, as they are not yet set up, and nor are the clients that
     * might have set them. */
    vbvxReadSizesAndCursorIntegrationFromHGSMI(pScrn, NULL);

#ifdef NEMUVIDEO_13
    /* Initialise CRTC and output configuration for use with randr1.2. */
    xf86CrtcConfigInit(pScrn, &NEMUCrtcConfigFuncs);

    {
        uint32_t i;

        for (i = 0; i < pNemu->cScreens; ++i)
        {
            char szOutput[256];

            /* Setup our virtual CRTCs. */
            pNemu->pScreens[i].paCrtcs = xf86CrtcCreate(pScrn, &NEMUCrtcFuncs);
            pNemu->pScreens[i].paCrtcs->driver_private = (void *)(uintptr_t)i;

            /* Set up our virtual outputs. */
            snprintf(szOutput, sizeof(szOutput), "VGA-%u", i);
            pNemu->pScreens[i].paOutputs
                = xf86OutputCreate(pScrn, &NEMUOutputFuncs, szOutput);

            /* We are not interested in the monitor section in the
             * configuration file. */
            xf86OutputUseScreenMonitor(pNemu->pScreens[i].paOutputs, FALSE);
            pNemu->pScreens[i].paOutputs->possible_crtcs = 1 << i;
            pNemu->pScreens[i].paOutputs->possible_clones = 0;
            pNemu->pScreens[i].paOutputs->driver_private = (void *)(uintptr_t)i;
            TRACE_LOG("Created crtc (%p) and output %s (%p)\n",
                      (void *)pNemu->pScreens[i].paCrtcs, szOutput,
                      (void *)pNemu->pScreens[i].paOutputs);
        }
    }

    /* Set a sane minimum and maximum mode size to match what the hardware
     * supports. */
    xf86CrtcSetSizeRange(pScrn, NEMU_VIDEO_MIN_SIZE, NEMU_VIDEO_MIN_SIZE, NEMU_VIDEO_MAX_VIRTUAL, NEMU_VIDEO_MAX_VIRTUAL);

    /* Now create our initial CRTC/output configuration. */
    if (!xf86InitialConfiguration(pScrn, TRUE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Initial CRTC configuration failed!\n");
        return (FALSE);
    }

    /* Work around a bug in the original X server modesetting code, which took
     * the first valid values set to these two as maxima over the server
     * lifetime.  This bug was introduced on Feb 15 2007 and was fixed in commit
     * fa877d7f three months later, so it was present in X.Org Server 1.3. */
    pScrn->virtualX = NEMU_VIDEO_MAX_VIRTUAL;
    pScrn->virtualY = NEMU_VIDEO_MAX_VIRTUAL;

    /* Initialise randr 1.2 mode-setting functions. */
    if (!xf86CrtcScreenInit(pScreen)) {
        return FALSE;
    }

    /* set first video mode */
    setSizesAndCursorIntegration(pScrn, true);
#else
    /* set first video mode */
    setModeRandR11(pScrn, pScrn->currentMode, true, false, 0, 0);
#endif

    /* Register block and wake-up handlers for getting new screen size hints. */
    RegisterBlockAndWakeupHandlers(nemuBlockHandler, (WakeupHandlerProcPtr)NoopDDA, (pointer)pScrn);

    /* software cursor */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* colourmap code */
    if (!miCreateDefColormap(pScreen))
        return (FALSE);

    if(!xf86HandleColormaps(pScreen, 256, 8, nemuLoadPalette, NULL, 0))
        return (FALSE);

    pNemu->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = SCRNINDEXAPI(NEMUCloseScreen);
#ifdef NEMUVIDEO_13
    pScreen->SaveScreen = xf86SaveScreen;
#else
    pScreen->SaveScreen = NEMUSaveScreen;
#endif

#ifdef NEMUVIDEO_13
    xf86DPMSInit(pScreen, xf86DPMSSet, 0);
#else
    /* We probably do want to support power management - even if we just use
       a dummy function. */
    xf86DPMSInit(pScreen, NEMUDisplayPowerManagementSet, 0);
#endif

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1)
        xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

    if (vbvxCursorInit(pScreen) != TRUE)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Unable to start the VirtualBox mouse pointer integration with the host system.\n");

#ifdef NEMU_DRI_OLD
    if (pNemu->useDRI)
        pNemu->useDRI = NEMUDRIFinishScreenInit(pScreen);
#endif

    return (TRUE);
}

#define NO_VT_ATOM_NAME "NEMUVIDEO_NO_VT"

static Bool NEMUEnterVT(ScrnInfoPtr pScrn)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
#ifndef NEMUVIDEO_13
    /* If we got a mode request while we were switched out, temporarily override
     * the physical mode set to the device while keeping things consistent from
     * the server's point of view. */
    int cXOverRide = RT_CLAMP(pNemu->pScreens[0].aPreferredSize.cx, NEMU_VIDEO_MIN_SIZE, NEMU_VIDEO_MAX_VIRTUAL);
    int cYOverRide = RT_CLAMP(pNemu->pScreens[0].aPreferredSize.cy, NEMU_VIDEO_MIN_SIZE, NEMU_VIDEO_MAX_VIRTUAL);
#endif

    TRACE_ENTRY();
    updateGraphicsCapability(pScrn, TRUE);
#ifdef NEMU_DRI_OLD
    if (pNemu->useDRI)
        DRIUnlock(xf86ScrnToScreen(pScrn));
#elif defined(NEMU_DRI)  /* DRI2 */
    if (pNemu->drmFD >= 0)
    {
        /* Tell the kernel driver, if present, that we are taking over. */
        drmSetMaster(pNemu->drmFD);
    }
#endif
    vbvxSetUpHGSMIHeapInGuest(pNemu, pScrn->videoRam * 1024);
    nemuEnableVbva(pScrn);
    /* Re-set video mode */
#ifdef NEMUVIDEO_13
    vbvxReadSizesAndCursorIntegrationFromHGSMI(pScrn, NULL);
    setSizesAndCursorIntegration(pScrn, false);
#else
    setModeRandR11(pScrn, pScrn->currentMode, false, true, cXOverRide, cYOverRide);
    DeleteProperty(ROOT_WINDOW(pScrn), MakeAtom(NO_VT_ATOM_NAME, sizeof(NO_VT_ATOM_NAME) - 1, TRUE));
#endif
    return TRUE;
}

static void NEMULeaveVT(ScrnInfoPtr pScrn)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
#ifdef NEMUVIDEO_13
    unsigned i;
#else
    int32_t propertyValue = 0;
#endif

    TRACE_ENTRY();
    updateGraphicsCapability(pScrn, FALSE);
#ifdef NEMUVIDEO_13
    for (i = 0; i < pNemu->cScreens; ++i)
        nemu_crtc_dpms(pNemu->pScreens[i].paCrtcs, DPMSModeOff);
#else
    ChangeWindowProperty(ROOT_WINDOW(pScrn), MakeAtom(NO_VT_ATOM_NAME, sizeof(NO_VT_ATOM_NAME) - 1, FALSE), XA_INTEGER, 32,
                         PropModeReplace, 1, &propertyValue, TRUE);
#endif
    nemuDisableVbva(pScrn);
    vbvxClearVRAM(pScrn, ((size_t)pScrn->virtualX) * pScrn->virtualY * (pScrn->bitsPerPixel / 8), 0);
#ifdef NEMU_DRI_OLD
    if (pNemu->useDRI)
        DRILock(xf86ScrnToScreen(pScrn), 0);
#elif defined(NEMU_DRI)  /* DRI2 */
    if (pNemu->drmFD >= 0)
        drmDropMaster(pNemu->drmFD);
#endif
    NEMURestoreMode(pScrn);
    TRACE_EXIT();
}

static Bool NEMUCloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    NEMUPtr pNemu = NEMUGetRec(pScrn);
#if defined(NEMU_DRI) && !defined(NEMU_DRI_OLD)  /* DRI2 */
    BOOL fRestore = TRUE;
#endif
    BOOL ret;

    if (pScrn->vtSema)
    {
#ifdef NEMUVIDEO_13
        unsigned i;

        for (i = 0; i < pNemu->cScreens; ++i)
            nemu_crtc_dpms(pNemu->pScreens[i].paCrtcs, DPMSModeOff);
#endif
        nemuDisableVbva(pScrn);
        vbvxClearVRAM(pScrn, ((size_t)pScrn->virtualX) * pScrn->virtualY * (pScrn->bitsPerPixel / 8), 0);
    }
#ifdef NEMU_DRI
# ifndef NEMU_DRI_OLD  /* DRI2 */
    if (   pNemu->drmFD >= 0
        /* Tell the kernel driver, if present, that we are going away. */
        && drmIoctl(pNemu->drmFD, NEMUVIDEO_IOCTL_ENABLE_HGSMI, NULL) >= 0)
        fRestore = false;
# endif
    if (pNemu->useDRI)
        NEMUDRICloseScreen(pScreen, pNemu);
    pNemu->useDRI = false;
#endif
#if defined(NEMU_DRI) && !defined(NEMU_DRI_OLD)  /* DRI2 */
    if (fRestore)
#endif
        if (pScrn->vtSema)
            NEMURestoreMode(pScrn);
    if (pScrn->vtSema)
        NEMUUnmapVidMem(pScrn);
    pScrn->vtSema = FALSE;

    vbvxCursorTerm(pNemu);

    pScreen->CloseScreen = pNemu->CloseScreen;
#if defined(NEMUVIDEO_13) && defined(RT_OS_LINUX)
    vbvxCleanUpLinuxACPI(pScreen);
#endif
#ifndef XF86_SCRN_INTERFACE
    ret = pScreen->CloseScreen(pScreen->myNum, pScreen);
#else
    ret = pScreen->CloseScreen(pScreen);
#endif
    VbglR3Term();
    return ret;
}

static Bool NEMUSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr pMode)
{
    Bool rc = TRUE;

    TRACE_LOG("HDisplay=%d, VDisplay=%d\n", pMode->HDisplay, pMode->VDisplay);
#ifdef NEMUVIDEO_13
    rc = xf86SetSingleMode(pScrn, pMode, RR_Rotate_0);
#else
    setModeRandR11(pScrn, pMode, false, false, 0, 0);
#endif
    TRACE_LOG("returning %s\n", rc ? "TRUE" : "FALSE");
    return rc;
}

static void NEMUAdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{ (void)pScrn; (void)x; (void)y; }

static void NEMUFreeScreen(ScrnInfoPtr pScrn)
{
    /* Destroy the VGA hardware record */
    vgaHWFreeHWRec(pScrn);
    /* And our private record */
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

static Bool
NEMUMapVidMem(ScrnInfoPtr pScrn)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    Bool rc = TRUE;

    TRACE_ENTRY();
    if (!pNemu->base)
    {
#ifdef PCIACCESS
        (void) pci_device_map_range(pNemu->pciInfo,
                                    pScrn->memPhysBase,
                                    pScrn->videoRam * 1024,
                                    PCI_DEV_MAP_FLAG_WRITABLE,
                                    & pNemu->base);
#else
        pNemu->base = xf86MapPciMem(pScrn->scrnIndex,
                                    VIDMEM_FRAMEBUFFER,
                                    pNemu->pciTag, pScrn->memPhysBase,
                                    (unsigned) pScrn->videoRam * 1024);
#endif
        if (!pNemu->base)
            rc = FALSE;
    }
    TRACE_LOG("returning %s\n", rc ? "TRUE" : "FALSE");
    return rc;
}

static void
NEMUUnmapVidMem(ScrnInfoPtr pScrn)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);

    TRACE_ENTRY();
    if (pNemu->base == NULL)
        return;

#ifdef PCIACCESS
    (void) pci_device_unmap_range(pNemu->pciInfo,
                                  pNemu->base,
                                  pScrn->videoRam * 1024);
#else
    xf86UnMapVidMem(pScrn->scrnIndex, pNemu->base,
                    (unsigned) pScrn->videoRam * 1024);
#endif
    pNemu->base = NULL;
    TRACE_EXIT();
}

static Bool
NEMUSaveScreen(ScreenPtr pScreen, int mode)
{
    (void)pScreen; (void)mode;
    return TRUE;
}

void
NEMUSaveMode(ScrnInfoPtr pScrn)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    vgaRegPtr vgaReg;

    TRACE_ENTRY();
    vgaReg = &VGAHWPTR(pScrn)->SavedReg;
    vgaHWSave(pScrn, vgaReg, VGA_SR_ALL);
    pNemu->fSavedVBEMode = NemuVideoGetModeRegisters(&pNemu->cSavedWidth,
                                                     &pNemu->cSavedHeight,
                                                     &pNemu->cSavedPitch,
                                                     &pNemu->cSavedBPP,
                                                     &pNemu->fSavedFlags);
}

void
NEMURestoreMode(ScrnInfoPtr pScrn)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    vgaRegPtr vgaReg;
#ifdef NEMU_DRI
    drmModeResPtr pRes;
#endif

    TRACE_ENTRY();
#ifdef NEMU_DRI
    /* Do not try to re-set the VGA state if a mode-setting driver is loaded. */
    if (   pNemu->drmFD >= 0
        && LoaderSymbol("drmModeGetResources") != NULL
        && (pRes = drmModeGetResources(pNemu->drmFD)) != NULL)
    {
        drmModeFreeResources(pRes);
        return;
    }
#endif
    vgaReg = &VGAHWPTR(pScrn)->SavedReg;
    vgaHWRestore(pScrn, vgaReg, VGA_SR_ALL);
    if (pNemu->fSavedVBEMode)
        NemuVideoSetModeRegisters(pNemu->cSavedWidth, pNemu->cSavedHeight,
                                  pNemu->cSavedPitch, pNemu->cSavedBPP,
                                  pNemu->fSavedFlags, 0, 0);
    else
        NemuVideoDisableVBE();
}

static void
NEMUDisplayPowerManagementSet(ScrnInfoPtr pScrn, int mode,
                int flags)
{
    (void)pScrn; (void)mode; (void) flags;
}
