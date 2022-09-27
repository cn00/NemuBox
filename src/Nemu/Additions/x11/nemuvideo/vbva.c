/* $Id: vbva.c $ */
/** @file
 * VirtualBox X11 Additions graphics driver 2D acceleration functions
 */

/*
 * Copyright (C) 2006-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <Nemu/VMMDev.h>
#include <Nemu/NemuGuestLib.h>

#include <iprt/string.h>
#include "compiler.h"

#include "nemuvideo.h"

#ifdef XORG_7X
# include <stdlib.h>
#endif

/**************************************************************************
* Main functions                                                          *
**************************************************************************/

/**
 * Callback function called by the X server to tell us about dirty
 * rectangles in the video buffer.
 *
 * @param pScreen pointer to the information structure for the current
 *                screen
 * @param iRects  Number of dirty rectangles to update
 * @param aRects  Array of structures containing the coordinates of the
 *                rectangles
 */
void vbvxHandleDirtyRect(ScrnInfoPtr pScrn, int iRects, BoxPtr aRects)
{
    VBVACMDHDR cmdHdr;
    NEMUPtr pNemu;
    int i;
    unsigned j;

    pNemu = pScrn->driverPrivate;
    if (!pScrn->vtSema)
        return;

    for (j = 0; j < pNemu->cScreens; ++j)
    {
        /* Just continue quietly if VBVA is not currently active. */
        struct VBVABUFFER *pVBVA = pNemu->pScreens[j].aVbvaCtx.pVBVA;
        if (   !pVBVA
            || !(pVBVA->hostFlags.u32HostEvents & VBVA_F_MODE_ENABLED))
            continue;
        for (i = 0; i < iRects; ++i)
        {
            if (   aRects[i].x1 >   pNemu->pScreens[j].aScreenLocation.x
                                  + pNemu->pScreens[j].aScreenLocation.cx
                || aRects[i].y1 >   pNemu->pScreens[j].aScreenLocation.y
                                  + pNemu->pScreens[j].aScreenLocation.cy
                || aRects[i].x2 <   pNemu->pScreens[j].aScreenLocation.x
                || aRects[i].y2 <   pNemu->pScreens[j].aScreenLocation.y)
                continue;
            cmdHdr.x = (int16_t)aRects[i].x1 - pNemu->pScreens[0].aScreenLocation.x;
            cmdHdr.y = (int16_t)aRects[i].y1 - pNemu->pScreens[0].aScreenLocation.y;
            cmdHdr.w = (uint16_t)(aRects[i].x2 - aRects[i].x1);
            cmdHdr.h = (uint16_t)(aRects[i].y2 - aRects[i].y1);

#if 0
            TRACE_LOG("display=%u, x=%d, y=%d, w=%d, h=%d\n",
                      j, cmdHdr.x, cmdHdr.y, cmdHdr.w, cmdHdr.h);
#endif

            if (NemuVBVABufferBeginUpdate(&pNemu->pScreens[j].aVbvaCtx,
                                          &pNemu->guestCtx))
            {
                NemuVBVAWrite(&pNemu->pScreens[j].aVbvaCtx, &pNemu->guestCtx, &cmdHdr,
                              sizeof(cmdHdr));
                NemuVBVABufferEndUpdate(&pNemu->pScreens[j].aVbvaCtx);
            }
        }
    }
}

static DECLCALLBACK(void *) hgsmiEnvAlloc(void *pvEnv, HGSMISIZE cb)
{
    NOREF(pvEnv);
    return calloc(1, cb);
}

static DECLCALLBACK(void) hgsmiEnvFree(void *pvEnv, void *pv)
{
    NOREF(pvEnv);
    free(pv);
}

static HGSMIENV g_hgsmiEnv =
{
    NULL,
    hgsmiEnvAlloc,
    hgsmiEnvFree
};

/**
 * Calculate the location in video RAM of and initialise the heap for guest to
 * host messages.  In the VirtualBox 4.3 and earlier Guest Additions this
 * function creates the heap structures directly in guest video RAM, so it
 * needs to be called whenever video RAM is (re-)set-up.
 */
void vbvxSetUpHGSMIHeapInGuest(NEMUPtr pNemu, uint32_t cbVRAM)
{
    int rc;
    uint32_t offVRAMBaseMapping, offGuestHeapMemory, cbGuestHeapMemory;
    void *pvGuestHeapMemory;

    NemuHGSMIGetBaseMappingInfo(cbVRAM, &offVRAMBaseMapping, NULL, &offGuestHeapMemory, &cbGuestHeapMemory, NULL);
    pvGuestHeapMemory = ((uint8_t *)pNemu->base) + offVRAMBaseMapping + offGuestHeapMemory;
    rc = NemuHGSMISetupGuestContext(&pNemu->guestCtx, pvGuestHeapMemory, cbGuestHeapMemory,
                                    offVRAMBaseMapping + offGuestHeapMemory, &g_hgsmiEnv);
    VBVXASSERT(RT_SUCCESS(rc), ("Failed to set up the guest-to-host message buffer heap, rc=%d\n", rc));
    pNemu->cbView = offVRAMBaseMapping;
}

/** Callback to fill in the view structures */
static DECLCALLBACK(int) nemuFillViewInfo(void *pvNemu, struct VBVAINFOVIEW *pViews, uint32_t cViews)
{
    NEMUPtr pNemu = (NEMUPtr)pvNemu;
    unsigned i;
    for (i = 0; i < cViews; ++i)
    {
        pViews[i].u32ViewIndex = i;
        pViews[i].u32ViewOffset = 0;
        pViews[i].u32ViewSize = pNemu->cbView;
        pViews[i].u32MaxScreenSize = pNemu->cbFBMax;
    }
    return VINF_SUCCESS;
}

/**
 * Initialise VirtualBox's accelerated video extensions.
 *
 * @returns TRUE on success, FALSE on failure
 */
static Bool nemuSetupVRAMVbva(NEMUPtr pNemu)
{
    int rc = VINF_SUCCESS;
    unsigned i;

    pNemu->cbFBMax = pNemu->cbView;
    for (i = 0; i < pNemu->cScreens; ++i)
    {
        pNemu->cbFBMax -= VBVA_MIN_BUFFER_SIZE;
        pNemu->pScreens[i].aoffVBVABuffer = pNemu->cbFBMax;
        TRACE_LOG("VBVA buffer offset for screen %u: 0x%lx\n", i,
                  (unsigned long) pNemu->cbFBMax);
        NemuVBVASetupBufferContext(&pNemu->pScreens[i].aVbvaCtx,
                                   pNemu->pScreens[i].aoffVBVABuffer,
                                   VBVA_MIN_BUFFER_SIZE);
    }
    TRACE_LOG("Maximum framebuffer size: %lu (0x%lx)\n",
              (unsigned long) pNemu->cbFBMax,
              (unsigned long) pNemu->cbFBMax);
    rc = NemuHGSMISendViewInfo(&pNemu->guestCtx, pNemu->cScreens,
                               nemuFillViewInfo, (void *)pNemu);
    VBVXASSERT(RT_SUCCESS(rc), ("Failed to send the view information to the host, rc=%d\n", rc));
    return TRUE;
}

static bool haveHGSMIModeHintAndCursorReportingInterface(NEMUPtr pNemu)
{
    uint32_t fModeHintReporting, fCursorReporting;

    return    RT_SUCCESS(NemuQueryConfHGSMI(&pNemu->guestCtx, NEMU_VBVA_CONF32_MODE_HINT_REPORTING, &fModeHintReporting))
           && RT_SUCCESS(NemuQueryConfHGSMI(&pNemu->guestCtx, NEMU_VBVA_CONF32_GUEST_CURSOR_REPORTING, &fCursorReporting))
           && fModeHintReporting == VINF_SUCCESS
           && fCursorReporting == VINF_SUCCESS;
}

static bool hostHasScreenBlankingFlag(NEMUPtr pNemu)
{
    uint32_t fScreenFlags;

    return    RT_SUCCESS(NemuQueryConfHGSMI(&pNemu->guestCtx, NEMU_VBVA_CONF32_SCREEN_FLAGS, &fScreenFlags))
           && fScreenFlags & VBVA_SCREEN_F_BLANK;
}

/**
 * Inform Nemu that we will supply it with dirty rectangle information
 * and install the dirty rectangle handler.
 *
 * @returns TRUE for success, FALSE for failure
 * @param   pScrn   Pointer to a structure describing the X screen in use
 */
Bool
nemuEnableVbva(ScrnInfoPtr pScrn)
{
    bool rc = TRUE;
    unsigned i;
    NEMUPtr pNemu = pScrn->driverPrivate;

    TRACE_ENTRY();
    if (!nemuSetupVRAMVbva(pNemu))
        return FALSE;
    for (i = 0; i < pNemu->cScreens; ++i)
    {
        struct VBVABUFFER *pVBVA;

        pVBVA = (struct VBVABUFFER *) (  ((uint8_t *)pNemu->base)
                                       + pNemu->pScreens[i].aoffVBVABuffer);
        if (!NemuVBVAEnable(&pNemu->pScreens[i].aVbvaCtx, &pNemu->guestCtx,
                            pVBVA, i))
            rc = FALSE;
    }
    VBVXASSERT(rc, ("Failed to enable screen update reporting for at least one virtual monitor.\n"));
#ifdef NEMUVIDEO_13
    NemuHGSMISendCapsInfo(&pNemu->guestCtx, VBVACAPS_VIDEO_MODE_HINTS | VBVACAPS_DISABLE_CURSOR_INTEGRATION);
    pNemu->fHaveHGSMIModeHints = haveHGSMIModeHintAndCursorReportingInterface(pNemu);
    pNemu->fHostHasScreenBlankingFlag = hostHasScreenBlankingFlag(pNemu);
#endif
    return rc;
}

/**
 * Inform Nemu that we will stop supplying it with dirty rectangle
 * information. This function is intended to be called when an X
 * virtual terminal is disabled, or the X server is terminated.
 *
 * @returns TRUE for success, FALSE for failure
 * @param   pScrn   Pointer to a structure describing the X screen in use
 */
void
nemuDisableVbva(ScrnInfoPtr pScrn)
{
    unsigned i;
    NEMUPtr pNemu = pScrn->driverPrivate;

    TRACE_ENTRY();
    for (i = 0; i < pNemu->cScreens; ++i)
        NemuVBVADisable(&pNemu->pScreens[i].aVbvaCtx, &pNemu->guestCtx, i);
}
