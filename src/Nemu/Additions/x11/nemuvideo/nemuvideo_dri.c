/*  $Id: nemuvideo_dri.c $ */
/** @file
 * VirtualBox X11 Additions graphics driver, DRI support
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
 * This code is based on:
 *
 * X11 TDFX driver, src/tdfx_dri.c
 *
 * Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Daryll Strauss <daryll@precisioninsight.com>
 */

#include "xf86.h"
#include "nemuvideo.h"
#ifndef PCIACCESS
# include "xf86Pci.h"
#endif
#include <dri.h>
#include <GL/glxtokens.h>
#include <GL/glxint.h>
#include <drm.h>

static Bool
NEMUCreateContext(ScreenPtr pScreen, VisualPtr visual,
                  drm_context_t hwContext, void *pVisualConfigPriv,
                  DRIContextType contextStore);
static void
NEMUDestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
                   DRIContextType contextStore);
static void
NEMUDRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
                   DRIContextType oldContextType, void *oldContext,
                   DRIContextType newContextType, void *newContext);
static void
NEMUDRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 index);
static void
NEMUDRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
                   RegionPtr prgnSrc, CARD32 index);
static Bool
NEMUDRIOpenFullScreen(ScreenPtr pScreen);
static Bool
NEMUDRICloseFullScreen(ScreenPtr pScreen);
static void
NEMUDRITransitionTo2d(ScreenPtr pScreen);
static void
NEMUDRITransitionTo3d(ScreenPtr pScreen);

static Bool
NEMUInitVisualConfigs(ScrnInfoPtr pScrn, NEMUPtr pNemu)
{
    Bool rc = TRUE;
    TRACE_ENTRY();
    int cConfigs = 2;  /* With and without double buffering */
    __GLXvisualConfig *pConfigs = NULL;
    pConfigs = (__GLXvisualConfig*) calloc(sizeof(__GLXvisualConfig),
                                           cConfigs);
    if (!pConfigs)
    {
        rc = FALSE;
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Disabling DRI: out of memory.\n");
    }
    for (int i = 0; rc && i < cConfigs; ++i)
    {
        pConfigs[i].vid = -1;
        pConfigs[i].class = -1;
        pConfigs[i].rgba = TRUE;
        if (pScrn->bitsPerPixel == 16)
        {
            pConfigs[i].redSize = 5;
            pConfigs[i].greenSize = 6;
            pConfigs[i].blueSize = 5;
            pConfigs[i].redMask = 0x0000F800;
            pConfigs[i].greenMask = 0x000007E0;
            pConfigs[i].blueMask = 0x0000001F;
        }
        else if (pScrn->bitsPerPixel == 32)
        {
            pConfigs[i].redSize = 8;
            pConfigs[i].greenSize = 8;
            pConfigs[i].blueSize = 8;
            pConfigs[i].alphaSize = 8;
            pConfigs[i].redMask   = 0x00ff0000;
            pConfigs[i].greenMask = 0x0000ff00;
            pConfigs[i].blueMask  = 0x000000ff;
            pConfigs[i].alphaMask = 0xff000000;
        }
        else
            rc = FALSE;
        pConfigs[i].bufferSize = pScrn->bitsPerPixel;
        pConfigs[i].visualRating = GLX_NONE;
        pConfigs[i].transparentPixel = GLX_NONE;
    }
    if (rc)
    {
        pConfigs[0].doubleBuffer = FALSE;
        pConfigs[1].doubleBuffer = TRUE;
        pNemu->cVisualConfigs = cConfigs;
        pNemu->pVisualConfigs = pConfigs;
        TRACE_LOG("Calling GlxSetVisualConfigs\n");
        GlxSetVisualConfigs(cConfigs, pConfigs, NULL);
    }
    if (!rc && pConfigs)
        free(pConfigs);
    TRACE_LOG("returning %s\n", BOOL_STR(rc));
    return rc;
}

#if 0
static void
NEMUDoWakeupHandler(int screenNum, pointer wakeupData, unsigned long result,
                    pointer pReadmask)
{

}
#endif

#if 0
static void
NEMUDoBlockHandler(int screenNum, pointer blockData, pointer pTimeout,
                   pointer pReadmask)
{

}
#endif

Bool NEMUDRIScreenInit(ScrnInfoPtr pScrn, ScreenPtr pScreen, NEMUPtr pNemu)
{
    DRIInfoPtr pDRIInfo = NULL;
    Bool rc = TRUE;

    TRACE_ENTRY();
    pNemu->drmFD = -1;
    if (   pScrn->bitsPerPixel != 16
        && pScrn->bitsPerPixel != 32)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "DRI is only available in 16bpp or 32bpp graphics modes.\n");
        rc = FALSE;
    }
    /* Assertion */
    if (   (pScrn->displayWidth == 0)
        || (pNemu->pciInfo == NULL)
        || (pNemu->base == NULL)
        || (pNemu->cbFBMax == 0))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s: preconditions failed\n",
                   RT_GCC_EXTENSION __PRETTY_FUNCTION__);
        rc = FALSE;
    }
    /* Check that the GLX, DRI, and DRM modules have been loaded by testing for
     * canonical symbols in each module, the way all existing _dri drivers do.
     */
    if (rc)
    {
        TRACE_LOG("Checking symbols\n");
        if (   !xf86LoaderCheckSymbol("GlxSetVisualConfigs")
            || !xf86LoaderCheckSymbol("drmAvailable")
            || !xf86LoaderCheckSymbol("DRIQueryVersion"))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Disabling DRI due to missing server functionality.\n");
            rc = FALSE;
        }
    }
    /* Check the DRI version */
    if (rc)
    {
        int major, minor, patch;
        TRACE_LOG("Checking DRI version\n");
        DRIQueryVersion(&major, &minor, &patch);
        if (major != DRIINFO_MAJOR_VERSION || minor < DRIINFO_MINOR_VERSION)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Disabling DRI due to a version mismatch between server and driver.  Server version: %d.%d.  Driver version: %d.%d\n",
                       major, minor, DRIINFO_MAJOR_VERSION, DRIINFO_MINOR_VERSION);
            rc = FALSE;
        }
    }
    if (rc)
    {
        TRACE_LOG("Creating DRIInfoRec\n");
        pDRIInfo = DRICreateInfoRec();
        if (!pDRIInfo)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Disabling DRI: out of memory.\n");
            rc = FALSE;
        }
        else
            pNemu->pDRIInfo = pDRIInfo;
    }
    if (rc)
    {
        pDRIInfo->CreateContext = NEMUCreateContext;
        pDRIInfo->DestroyContext = NEMUDestroyContext;
        pDRIInfo->SwapContext = NEMUDRISwapContext;
        pDRIInfo->InitBuffers = NEMUDRIInitBuffers;
        pDRIInfo->MoveBuffers = NEMUDRIMoveBuffers;
        pDRIInfo->OpenFullScreen = NEMUDRIOpenFullScreen;
        pDRIInfo->CloseFullScreen = NEMUDRICloseFullScreen;
        pDRIInfo->TransitionTo2d = NEMUDRITransitionTo2d;
        pDRIInfo->TransitionTo3d = NEMUDRITransitionTo3d;

        /* These two are set in DRICreateInfoRec(). */
        pDRIInfo->wrap.ValidateTree = NULL;
        pDRIInfo->wrap.PostValidateTree = NULL;

        pDRIInfo->drmDriverName = NEMU_DRM_DRIVER_NAME;
        pDRIInfo->clientDriverName = NEMU_DRI_DRIVER_NAME;
#ifdef PCIACCESS
        pDRIInfo->busIdString = DRICreatePCIBusID(pNemu->pciInfo);
#else
        pDRIInfo->busIdString = alloc(64);
        sprintf(pDRIInfo->busIdString, "PCI:%d:%d:%d",
            ((pciConfigPtr)pNemu->pciInfo->thisCard)->busnum,
            ((pciConfigPtr)pNemu->pciInfo->thisCard)->devnum,
            ((pciConfigPtr)pNemu->pciInfo->thisCard)->funcnum);
#endif
        pDRIInfo->ddxDriverMajorVersion = NEMU_VIDEO_MAJOR;
        pDRIInfo->ddxDriverMinorVersion = NEMU_VIDEO_MINOR;
        pDRIInfo->ddxDriverPatchVersion = 0;
        pDRIInfo->ddxDrawableTableEntry = NEMU_MAX_DRAWABLES;
        pDRIInfo->maxDrawableTableEntry = NEMU_MAX_DRAWABLES;
        pDRIInfo->frameBufferPhysicalAddress = (pointer)pScrn->memPhysBase;
        pDRIInfo->frameBufferSize = pNemu->cbFBMax;
        pDRIInfo->frameBufferStride =   pScrn->displayWidth
                                      * pScrn->bitsPerPixel / 8;
        pDRIInfo->SAREASize = SAREA_MAX;  /* we have no private bits yet. */
        /* This can't be zero, as the server callocs this size and checks for
         * non-NULL... */
        pDRIInfo->contextSize = 4;
        pDRIInfo->driverSwapMethod = DRI_HIDE_X_CONTEXT;
        pDRIInfo->bufferRequests = DRI_ALL_WINDOWS;
        TRACE_LOG("Calling DRIScreenInit\n");
        if (!DRIScreenInit(pScreen, pDRIInfo, &pNemu->drmFD))
        {
            rc = FALSE;
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "DRIScreenInit failed, disabling DRI.\n");
            if (pNemu->drmFD)
            {
                drmClose(pNemu->drmFD);
                pNemu->drmFD = -1;
            }
        }
    }
    if (rc && !NEMUInitVisualConfigs(pScrn, pNemu))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "NEMUInitVisualConfigs failed, disabling DRI.\n");
        rc = FALSE;
    }
    else
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "visual configurations initialized\n");

    /* Check the DRM version */
    if (rc)
    {
        drmVersionPtr version = drmGetVersion(pNemu->drmFD);
        TRACE_LOG("Checking DRM version\n");
        if (version)
        {
            if (version->version_major != 1 || version->version_minor < 0)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                           "Bad DRM driver version %d.%d, expected version 1.0.  Disabling DRI.\n",
                           version->version_major, version->version_minor);
                rc = FALSE;
            }
            drmFreeVersion(version);
        }
    }

    /* Clean up on failure. */
    if (!rc)
    {
        if (pNemu->drmFD >= 0)
           NEMUDRICloseScreen(pScreen, pNemu);
        pNemu->drmFD = -1;
        if (pNemu->pDRIInfo)
            DRIDestroyInfoRec(pNemu->pDRIInfo);
        pNemu->pDRIInfo = NULL;
    }
    TRACE_LOG("returning %s\n", BOOL_STR(rc));
    return rc;
}

void NEMUDRIUpdateStride(ScrnInfoPtr pScrn, NEMUPtr pNemu)
{
    DRIInfoPtr pDRIInfo = pNemu->pDRIInfo;
    pDRIInfo->frameBufferStride =   pScrn->displayWidth
                                  * pScrn->bitsPerPixel / 8;
}

void
NEMUDRICloseScreen(ScreenPtr pScreen, NEMUPtr pNemu)
{
    DRICloseScreen(pScreen);
    DRIDestroyInfoRec(pNemu->pDRIInfo);
    pNemu->pDRIInfo=0;
    if (pNemu->pVisualConfigs)
        free(pNemu->pVisualConfigs);
    pNemu->cVisualConfigs = 0;
    pNemu->pVisualConfigs = NULL;
}

static Bool
NEMUCreateContext(ScreenPtr pScreen, VisualPtr visual,
                  drm_context_t hwContext, void *pVisualConfigPriv,
                  DRIContextType contextStore)
{
    return TRUE;
}

static void
NEMUDestroyContext(ScreenPtr pScreen, drm_context_t hwContext,
                   DRIContextType contextStore)
{
}

Bool
NEMUDRIFinishScreenInit(ScreenPtr pScreen)
{
    return DRIFinishScreenInit(pScreen);
}

static void
NEMUDRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
                   DRIContextType oldContextType, void *oldContext,
                   DRIContextType newContextType, void *newContext)
{
}

static void
NEMUDRIInitBuffers(WindowPtr pWin, RegionPtr prgn, CARD32 index)
{
}

static void
NEMUDRIMoveBuffers(WindowPtr pParent, DDXPointRec ptOldOrg,
                   RegionPtr prgnSrc, CARD32 index)
{
}

/* Apparently the next two are just legacy. */
static Bool
NEMUDRIOpenFullScreen(ScreenPtr pScreen)
{
    return TRUE;
}

static Bool
NEMUDRICloseFullScreen(ScreenPtr pScreen)
{
    return TRUE;
}

static void
NEMUDRITransitionTo2d(ScreenPtr pScreen)
{
}

static void
NEMUDRITransitionTo3d(ScreenPtr pScreen)
{
}

