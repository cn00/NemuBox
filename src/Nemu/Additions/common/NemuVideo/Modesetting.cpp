/* $Id: Modesetting.cpp $ */
/** @file
 * VirtualBox Video driver, common code - HGSMI initialisation and helper
 * functions.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <Nemu/NemuVideoGuest.h>
#include <Nemu/NemuVideo.h>
#include <Nemu/NemuGuest.h>
#include <Nemu/Hardware/NemuVideoVBE.h>
#include <Nemu/VMMDev.h>

#include <iprt/asm.h>
#include <iprt/log.h>

#ifndef NEMU_GUESTR3XF86MOD
# include <iprt/string.h>
#endif

/**
 * Gets the count of virtual monitors attached to the guest via an HGSMI
 * command
 *
 * @returns the right count on success or 1 on failure.
 * @param  pCtx  the context containing the heap to use
 */
RTDECL(uint32_t) NemuHGSMIGetMonitorCount(PHGSMIGUESTCOMMANDCONTEXT pCtx)
{
    /* Query the configured number of displays. */
    uint32_t cDisplays = 0;
    NemuQueryConfHGSMI(pCtx, NEMU_VBVA_CONF32_MONITOR_COUNT, &cDisplays);
    LogFunc(("cDisplays = %d\n", cDisplays));
    if (cDisplays == 0 || cDisplays > NEMU_VIDEO_MAX_SCREENS)
        /* Host reported some bad value. Continue in the 1 screen mode. */
        cDisplays = 1;
    return cDisplays;
}


/**
 * Returns the size of the video RAM in bytes.
 *
 * @returns the size
 */
RTDECL(uint32_t) NemuVideoGetVRAMSize(void)
{
    /** @note A 32bit read on this port returns the VRAM size. */
    return NemuVideoCmnPortReadUlong(VBE_DISPI_IOPORT_DATA);
}


/**
 * Check whether this hardware allows the display width to have non-multiple-
 * of-eight values.
 *
 * @returns true if any width is allowed, false otherwise.
 */
RTDECL(bool) NemuVideoAnyWidthAllowed(void)
{
    unsigned DispiId;
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID_ANYX);
    DispiId = NemuVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);
    return (DispiId == VBE_DISPI_ID_ANYX);
}


/**
 * Tell the host about how VRAM is divided up between each screen via an HGSMI
 * command.  It is acceptable to specifiy identical data for each screen if
 * they share a single framebuffer.
 *
 * @returns iprt status code, either VERR_NO_MEMORY or the status returned by
 *          @a pfnFill
 * @todo  What was I thinking of with that callback function?  It
 *        would be much simpler to just pass in a structure in normal
 *        memory and copy it.
 * @param  pCtx      the context containing the heap to use
 * @param  u32Count  the number of screens we are activating
 * @param  pfnFill   a callback which initialises the VBVAINFOVIEW structures
 *                   for all screens
 * @param  pvData    context data for @a pfnFill
 */
RTDECL(int) NemuHGSMISendViewInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                  uint32_t u32Count,
                                  PFNHGSMIFILLVIEWINFO pfnFill,
                                  void *pvData)
{
    int rc;
    /* Issue the screen info command. */
    void *p = NemuHGSMIBufferAlloc(pCtx, sizeof(VBVAINFOVIEW) * u32Count,
                                   HGSMI_CH_VBVA, VBVA_INFO_VIEW);
    if (p)
    {
        VBVAINFOVIEW *pInfo = (VBVAINFOVIEW *)p;
        rc = pfnFill(pvData, pInfo, u32Count);
        if (RT_SUCCESS(rc))
            NemuHGSMIBufferSubmit (pCtx, p);
        NemuHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Set a video mode using port registers.  This must be done for the first
 * screen before every HGSMI modeset and also works when HGSM is not enabled.
 * @param  cWidth      the mode width
 * @param  cHeight     the mode height
 * @param  cVirtWidth  the mode pitch
 * @param  cBPP        the colour depth of the mode
 * @param  fFlags      flags for the mode.  These will be or-ed with the
 *                     default _ENABLED flag, so unless you are restoring
 *                     a saved mode or have special requirements you can pass
 *                     zero here.
 * @param  cx          the horizontal panning offset
 * @param  cy          the vertical panning offset
 */
RTDECL(void) NemuVideoSetModeRegisters(uint16_t cWidth, uint16_t cHeight,
                                       uint16_t cVirtWidth, uint16_t cBPP,
                                       uint16_t fFlags, uint16_t cx,
                                       uint16_t cy)
{
    /* set the mode characteristics */
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, cWidth);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, cHeight);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                VBE_DISPI_INDEX_VIRT_WIDTH);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, cVirtWidth);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, cBPP);
    /* enable the mode */
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                VBE_DISPI_INDEX_ENABLE);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA,
                                fFlags | VBE_DISPI_ENABLED);
    /* Panning registers */
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                VBE_DISPI_INDEX_X_OFFSET);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, cx);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                VBE_DISPI_INDEX_Y_OFFSET);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, cy);
    /** @todo read from the port to see if the mode switch was successful */
}


/**
 * Get the video mode for the first screen using the port registers.  All
 * parameters are optional
 * @returns  true if the VBE mode returned is active, false if we are in VGA
 *           mode
 * @note  If anyone else needs additional register values just extend the
 *        function with additional parameters and fix any existing callers.
 * @param  pcWidth      where to store the mode width
 * @param  pcHeight     where to store the mode height
 * @param  pcVirtWidth  where to store the mode pitch
 * @param  pcBPP        where to store the colour depth of the mode
 * @param  pfFlags      where to store the flags for the mode
 */
RTDECL(bool) NemuVideoGetModeRegisters(uint16_t *pcWidth, uint16_t *pcHeight,
                                       uint16_t *pcVirtWidth, uint16_t *pcBPP,
                                       uint16_t *pfFlags)
{
    uint16_t fFlags;

    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                VBE_DISPI_INDEX_ENABLE);
    fFlags = NemuVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);
    if (pcWidth)
    {
        NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                    VBE_DISPI_INDEX_XRES);
        *pcWidth = NemuVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);
    }
    if (pcHeight)
    {
        NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                    VBE_DISPI_INDEX_YRES);
        *pcHeight = NemuVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);
    }
    if (pcVirtWidth)
    {
        NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                    VBE_DISPI_INDEX_VIRT_WIDTH);
        *pcVirtWidth = NemuVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);
    }
    if (pcBPP)
    {
        NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                    VBE_DISPI_INDEX_BPP);
        *pcBPP = NemuVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);
    }
    if (pfFlags)
        *pfFlags = fFlags;
    return RT_BOOL(fFlags & VBE_DISPI_ENABLED);
}


/**
 * Disable our extended graphics mode and go back to VGA mode.
 */
RTDECL(void) NemuVideoDisableVBE(void)
{
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX,
                                VBE_DISPI_INDEX_ENABLE);
    NemuVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, 0);
}


/**
 * Set a video mode via an HGSMI request.  The views must have been
 * initialised first using @a NemuHGSMISendViewInfo and if the mode is being
 * set on the first display then it must be set first using registers.
 * @param  cDisplay  the screen number
 * @param  cOriginX  the horizontal displacement relative to the first screen
 * @param  cOriginY  the vertical displacement relative to the first screen
 * @param  offStart  the offset of the visible area of the framebuffer
 *                   relative to the framebuffer start
 * @param  cbPitch   the offset in bytes between the starts of two adjecent
 *                   scan lines in video RAM
 * @param  cWidth    the mode width
 * @param  cHeight   the mode height
 * @param  cBPP      the colour depth of the mode
 */
RTDECL(void) NemuHGSMIProcessDisplayInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                         uint32_t cDisplay,
                                         int32_t  cOriginX,
                                         int32_t  cOriginY,
                                         uint32_t offStart,
                                         uint32_t cbPitch,
                                         uint32_t cWidth,
                                         uint32_t cHeight,
                                         uint16_t cBPP,
                                         uint16_t fFlags)
{
    /* Issue the screen info command. */
    void *p = NemuHGSMIBufferAlloc(pCtx,
                                   sizeof (VBVAINFOSCREEN),
                                   HGSMI_CH_VBVA,
                                   VBVA_INFO_SCREEN);
    if (!p)
    {
        LogFunc(("HGSMIHeapAlloc failed\n"));
    }
    else
    {
        VBVAINFOSCREEN *pScreen = (VBVAINFOSCREEN *)p;

        pScreen->u32ViewIndex    = cDisplay;
        pScreen->i32OriginX      = cOriginX;
        pScreen->i32OriginY      = cOriginY;
        pScreen->u32StartOffset  = offStart;
        pScreen->u32LineSize     = cbPitch;
        pScreen->u32Width        = cWidth;
        pScreen->u32Height       = cHeight;
        pScreen->u16BitsPerPixel = cBPP;
        pScreen->u16Flags        = fFlags;

        NemuHGSMIBufferSubmit(pCtx, p);

        NemuHGSMIBufferFree(pCtx, p);
    }
}


/** Report the rectangle relative to which absolute pointer events should be
 *  expressed.  This information remains valid until the next VBVA resize event
 *  for any screen, at which time it is reset to the bounding rectangle of all
 *  virtual screens.
 * @param  pCtx      The context containing the heap to use.
 * @param  cOriginX  Upper left X co-ordinate relative to the first screen.
 * @param  cOriginY  Upper left Y co-ordinate relative to the first screen.
 * @param  cWidth    Rectangle width.
 * @param  cHeight   Rectangle height.
 * @returns  iprt status code.
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 */
RTDECL(int)      NemuHGSMIUpdateInputMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx, int32_t  cOriginX, int32_t  cOriginY,
                                             uint32_t cWidth, uint32_t cHeight)
{
    int rc = VINF_SUCCESS;
    VBVAREPORTINPUTMAPPING *p;
    Log(("%s: cOriginX=%u, cOriginY=%u, cWidth=%u, cHeight=%u\n", __PRETTY_FUNCTION__, (unsigned)cOriginX, (unsigned)cOriginX,
         (unsigned)cWidth, (unsigned)cHeight));

    /* Allocate the IO buffer. */
    p = (VBVAREPORTINPUTMAPPING *)NemuHGSMIBufferAlloc(pCtx, sizeof(VBVAREPORTINPUTMAPPING), HGSMI_CH_VBVA,
                                                       VBVA_REPORT_INPUT_MAPPING);
    if (p)
    {
        /* Prepare data to be sent to the host. */
        p->x  = cOriginX;
        p->y  = cOriginY;
        p->cx = cWidth;
        p->cy = cHeight;
        rc = NemuHGSMIBufferSubmit(pCtx, p);
        /* Free the IO buffer. */
        NemuHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    LogFunc(("rc = %d\n", rc));
    return rc;
}


/**
 * Get most recent video mode hints.
 * @param  pCtx      the context containing the heap to use
 * @param  cScreens  the number of screens to query hints for, starting at 0.
 * @param  pHints    array of VBVAMODEHINT structures for receiving the hints.
 * @returns  iprt status code
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 * @returns  VERR_NOT_SUPPORTED  Host does not support this command.
 */
RTDECL(int) NemuHGSMIGetModeHints(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                  unsigned cScreens, VBVAMODEHINT *paHints)
{
    int rc;
    AssertPtrReturn(paHints, VERR_INVALID_POINTER);
    void *p = NemuHGSMIBufferAlloc(pCtx,   sizeof(VBVAQUERYMODEHINTS)
                                         + cScreens * sizeof(VBVAMODEHINT),
                                   HGSMI_CH_VBVA, VBVA_QUERY_MODE_HINTS);
    if (!p)
    {
        LogFunc(("HGSMIHeapAlloc failed\n"));
        return VERR_NO_MEMORY;
    }
    else
    {
        VBVAQUERYMODEHINTS *pQuery   = (VBVAQUERYMODEHINTS *)p;

        pQuery->cHintsQueried        = cScreens;
        pQuery->cbHintStructureGuest = sizeof(VBVAMODEHINT);
        pQuery->rc                   = VERR_NOT_SUPPORTED;

        NemuHGSMIBufferSubmit(pCtx, p);
        rc = pQuery->rc;
        if (RT_SUCCESS(rc))
            memcpy(paHints, ((uint8_t *)p) + sizeof(VBVAQUERYMODEHINTS),
                   cScreens * sizeof(VBVAMODEHINT));

        NemuHGSMIBufferFree(pCtx, p);
    }
    return rc;
}


/**
 * Query the supported flags in VBVAINFOSCREEN::u16Flags.
 *
 * @returns The mask of VBVA_SCREEN_F_* flags or 0 if host does not support the request.
 * @param  pCtx  the context containing the heap to use
 */
RTDECL(uint16_t) NemuHGSMIGetScreenFlags(PHGSMIGUESTCOMMANDCONTEXT pCtx)
{
    uint32_t u32Flags = 0;
    int rc = NemuQueryConfHGSMIDef(pCtx, NEMU_VBVA_CONF32_SCREEN_FLAGS, 0, &u32Flags);
    LogFunc(("u32Flags = 0x%x rc %Rrc\n", u32Flags, rc));
    if (RT_FAILURE(rc))
        u32Flags = 0;
    return (uint16_t)u32Flags;
}
