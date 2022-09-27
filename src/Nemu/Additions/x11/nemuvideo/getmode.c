/* $Id: getmode.c $ */
/** @file
 * VirtualBox X11 Additions graphics driver dynamic video mode functions.
 */

/*
 * Copyright (C) 2006-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "nemuvideo.h"
#include <Nemu/VMMDev.h>

#define NEED_XF86_TYPES
#include <iprt/string.h>

#include "xf86.h"

#ifdef XORG_7X
# include <stdio.h>
# include <stdlib.h>
#endif

#ifdef NEMUVIDEO_13
# ifdef RT_OS_LINUX
#  include <linux/input.h>
#  ifndef EVIOCGRAB
#   define EVIOCGRAB _IOW('E', 0x90, int)
#  endif
#  ifndef KEY_SWITCHVIDEOMODE
#   define KEY_SWITCHVIDEOMODE 227
#  endif
#  include <dirent.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <unistd.h>
# endif /* RT_OS_LINUX */
#endif /* NEMUVIDEO_13 */

/**************************************************************************
* Main functions                                                          *
**************************************************************************/

/**
 * Fills a display mode M with a built-in mode of name pszName and dimensions
 * cx and cy.
 */
static void nemuFillDisplayMode(ScrnInfoPtr pScrn, DisplayModePtr m,
                                const char *pszName, unsigned cx, unsigned cy)
{
    NEMUPtr pNemu = pScrn->driverPrivate;
    char szName[256];
    DisplayModePtr pPrev = m->prev;
    DisplayModePtr pNext = m->next;

    if (!pszName)
    {
        sprintf(szName, "%ux%u", cx, cy);
        pszName = szName;
    }
    TRACE_LOG("pszName=%s, cx=%u, cy=%u\n", pszName, cx, cy);
    if (m->name)
        free((void*)m->name);
    memset(m, '\0', sizeof(*m));
    m->prev          = pPrev;
    m->next          = pNext;
    m->status        = MODE_OK;
    m->type          = M_T_BUILTIN;
    /* Older versions of Nemu only support screen widths which are a multiple
     * of 8 */
    if (pNemu->fAnyX)
        m->HDisplay  = cx;
    else
        m->HDisplay  = cx & ~7;
    m->HSyncStart    = m->HDisplay + 2;
    m->HSyncEnd      = m->HDisplay + 4;
    m->HTotal        = m->HDisplay + 6;
    m->VDisplay      = cy;
    m->VSyncStart    = m->VDisplay + 2;
    m->VSyncEnd      = m->VDisplay + 4;
    m->VTotal        = m->VDisplay + 6;
    m->Clock         = m->HTotal * m->VTotal * 60 / 1000; /* kHz */
    m->name      = xnfstrdup(pszName);
}

/**
 * Allocates an empty display mode and links it into the doubly linked list of
 * modes pointed to by pScrn->modes.  Returns a pointer to the newly allocated
 * memory.
 */
static DisplayModePtr nemuAddEmptyScreenMode(ScrnInfoPtr pScrn)
{
    DisplayModePtr pMode = xnfcalloc(sizeof(DisplayModeRec), 1);

    TRACE_ENTRY();
    if (!pScrn->modes)
    {
        pScrn->modes = pMode;
        pMode->next = pMode;
        pMode->prev = pMode;
    }
    else
    {
        pMode->next = pScrn->modes;
        pMode->prev = pScrn->modes->prev;
        pMode->next->prev = pMode;
        pMode->prev->next = pMode;
    }
    return pMode;
}

/**
 * Create display mode entries in the screen information structure for each
 * of the graphics modes that we wish to support, that is:
 *  - A dynamic mode in first place which will be updated by the RandR code.
 *  - Any modes that the user requested in xorg.conf/XFree86Config.
 */
void nemuAddModes(ScrnInfoPtr pScrn)
{
    unsigned cx = 0, cy = 0, cIndex = 0;
    unsigned i;
    DisplayModePtr pMode;

    /* Add two dynamic mode entries.  When we receive a new size hint we will
     * update whichever of these is not current. */
    pMode = nemuAddEmptyScreenMode(pScrn);
    nemuFillDisplayMode(pScrn, pMode, NULL, 800, 600);
    pMode = nemuAddEmptyScreenMode(pScrn);
    nemuFillDisplayMode(pScrn, pMode, NULL, 800, 600);
    /* Add any modes specified by the user.  We assume here that the mode names
     * reflect the mode sizes. */
    for (i = 0; pScrn->display->modes && pScrn->display->modes[i]; i++)
    {
        if (sscanf(pScrn->display->modes[i], "%ux%u", &cx, &cy) == 2)
        {
            pMode = nemuAddEmptyScreenMode(pScrn);
            nemuFillDisplayMode(pScrn, pMode, pScrn->display->modes[i], cx, cy);
        }
    }
}

/** Set the initial values for the guest screen size hints to standard values
 * in case nothing else is available. */
void NemuInitialiseSizeHints(ScrnInfoPtr pScrn)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    unsigned i;

    for (i = 0; i < pNemu->cScreens; ++i)
    {
        pNemu->pScreens[i].aPreferredSize.cx = 800;
        pNemu->pScreens[i].aPreferredSize.cy = 600;
        pNemu->pScreens[i].afConnected       = true;
    }
    /* Set up the first mode correctly to match the requested initial mode. */
    pScrn->modes->HDisplay = pNemu->pScreens[0].aPreferredSize.cx;
    pScrn->modes->VDisplay = pNemu->pScreens[0].aPreferredSize.cy;
}

static bool useHardwareCursor(uint32_t fCursorCapabilities)
{
    if (   !(fCursorCapabilities & VMMDEV_MOUSE_HOST_CANNOT_HWPOINTER)
        && (fCursorCapabilities & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE))
        return true;
    return false;
}

static void compareAndMaybeSetUseHardwareCursor(NEMUPtr pNemu, uint32_t fCursorCapabilities, bool *pfChanged, bool fSet)
{
    if (pNemu->fUseHardwareCursor != useHardwareCursor(fCursorCapabilities))
        *pfChanged = true;
    if (fSet)
        pNemu->fUseHardwareCursor = useHardwareCursor(fCursorCapabilities);
}

#define SIZE_HINTS_PROPERTY          "NEMU_SIZE_HINTS"
#define SIZE_HINTS_MISMATCH_PROPERTY "NEMU_SIZE_HINTS_MISMATCH"
#define MOUSE_CAPABILITIES_PROPERTY  "NEMU_MOUSE_CAPABILITIES"

#define COMPARE_AND_MAYBE_SET(pDest, src, pfChanged, fSet) \
do { \
    if (*(pDest) != (src)) \
    { \
        if (fSet) \
            *(pDest) = (src); \
        *(pfChanged) = true; \
    } \
} while(0)

/** Read in information about the most recent size hints and cursor
 * capabilities requested for the guest screens from a root window property set
 * by an X11 client.  Information obtained via HGSMI takes priority. */
void vbvxReadSizesAndCursorIntegrationFromProperties(ScrnInfoPtr pScrn, bool *pfNeedUpdate)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    size_t cPropertyElements, cDummy;
    int32_t *paModeHints,  *pfCursorCapabilities;
    unsigned i;
    bool fChanged;
    bool fNeedUpdate = false;
    int32_t fSizeMismatch = false;

    if (vbvxGetIntegerPropery(pScrn, SIZE_HINTS_PROPERTY, &cPropertyElements, &paModeHints) != VINF_SUCCESS)
        paModeHints = NULL;
    if (paModeHints != NULL)
        for (i = 0; i < cPropertyElements / 2 && i < pNemu->cScreens; ++i)
        {
            VBVAMODEHINT *pVBVAModeHint = &pNemu->paVBVAModeHints[i];
            int32_t iSizeHint = paModeHints[i * 2];
            int32_t iLocation = paModeHints[i * 2 + 1];
            bool fNoHGSMI = !pNemu->fHaveHGSMIModeHints || pVBVAModeHint->magic != VBVAMODEHINT_MAGIC;

            fChanged = false;
            if (iSizeHint != 0)
            {
                if (iSizeHint == -1)
                    COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].afConnected, false, &fChanged, fNoHGSMI);
                else
                {
                    COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].aPreferredSize.cx, (iSizeHint >> 16) & 0x8fff, &fChanged, fNoHGSMI);
                    COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].aPreferredSize.cy, iSizeHint & 0x8fff, &fChanged, fNoHGSMI);
                    COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].afConnected, true, &fChanged, fNoHGSMI);
                }
                if (iLocation == -1)
                    COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].afHaveLocation, false, &fChanged, fNoHGSMI);
                else
                {
                    COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].aPreferredLocation.x, (iLocation >> 16) & 0x8fff, &fChanged,
                                          fNoHGSMI);
                    COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].aPreferredLocation.y, iLocation & 0x8fff, &fChanged, fNoHGSMI);
                    COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].afHaveLocation, true, &fChanged, fNoHGSMI);
                }
                if (fChanged && fNoHGSMI)
                    fNeedUpdate = true;
                if (fChanged && !fNoHGSMI)
                    fSizeMismatch = true;
            }
        }
    fChanged = false;
    if (   vbvxGetIntegerPropery(pScrn, MOUSE_CAPABILITIES_PROPERTY, &cDummy, &pfCursorCapabilities) == VINF_SUCCESS
        && cDummy == 1)
        compareAndMaybeSetUseHardwareCursor(pNemu, *pfCursorCapabilities, &fChanged, !pNemu->fHaveHGSMIModeHints);
    if (fChanged && !pNemu->fHaveHGSMIModeHints)
        fNeedUpdate = true;
    if (fChanged && pNemu->fHaveHGSMIModeHints)
        fSizeMismatch = true;
    vbvxSetIntegerPropery(pScrn, SIZE_HINTS_MISMATCH_PROPERTY, 1, &fSizeMismatch, false);
    if (pfNeedUpdate != NULL && fNeedUpdate)
        *pfNeedUpdate = true;
}

/** Read in information about the most recent size hints and cursor
 * capabilities requested for the guest screens from HGSMI. */
void vbvxReadSizesAndCursorIntegrationFromHGSMI(ScrnInfoPtr pScrn, bool *pfNeedUpdate)
{
    NEMUPtr pNemu = NEMUGetRec(pScrn);
    int rc;
    unsigned i;
    bool fChanged = false;
    uint32_t fCursorCapabilities;

    if (!pNemu->fHaveHGSMIModeHints)
        return;
    rc = NemuHGSMIGetModeHints(&pNemu->guestCtx, pNemu->cScreens, pNemu->paVBVAModeHints);
    VBVXASSERT(rc == VINF_SUCCESS, ("NemuHGSMIGetModeHints failed, rc=%d.\n", rc));
    for (i = 0; i < pNemu->cScreens; ++i)
        if (pNemu->paVBVAModeHints[i].magic == VBVAMODEHINT_MAGIC)
        {
            COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].aPreferredSize.cx, pNemu->paVBVAModeHints[i].cx & 0x8fff, &fChanged, true);
            COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].aPreferredSize.cy, pNemu->paVBVAModeHints[i].cy & 0x8fff, &fChanged, true);
            COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].afConnected, RT_BOOL(pNemu->paVBVAModeHints[i].fEnabled), &fChanged, true);
            COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].aPreferredLocation.x, (int32_t)pNemu->paVBVAModeHints[i].dx & 0x8fff, &fChanged,
                                  true);
            COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].aPreferredLocation.y, (int32_t)pNemu->paVBVAModeHints[i].dy & 0x8fff, &fChanged,
                                  true);
            if (pNemu->paVBVAModeHints[i].dx != ~(uint32_t)0 && pNemu->paVBVAModeHints[i].dy != ~(uint32_t)0)
                COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].afHaveLocation, true, &fChanged, true);
            else
                COMPARE_AND_MAYBE_SET(&pNemu->pScreens[i].afHaveLocation, false, &fChanged, true);
        }
    rc = NemuQueryConfHGSMI(&pNemu->guestCtx, NEMU_VBVA_CONF32_CURSOR_CAPABILITIES, &fCursorCapabilities);
    VBVXASSERT(rc == VINF_SUCCESS, ("Getting NEMU_VBVA_CONF32_CURSOR_CAPABILITIES failed, rc=%d.\n", rc));
    compareAndMaybeSetUseHardwareCursor(pNemu, fCursorCapabilities, &fChanged, true);
    if (pfNeedUpdate != NULL && fChanged)
        *pfNeedUpdate = true;
}

#undef COMPARE_AND_MAYBE_SET

#ifdef NEMUVIDEO_13
# ifdef RT_OS_LINUX
/** We have this for two purposes: one is to ensure that the X server is woken
 * up when we get a video ACPI event.  Two is to grab ACPI video events to
 * prevent gnome-settings-daemon from seeing them, as older versions ignored
 * the time stamp and handled them at the wrong time. */
static void acpiEventHandler(int fd, void *pvData)
{
    ScreenPtr pScreen = (ScreenPtr)pvData;
    NEMUPtr pNemu = NEMUGetRec(xf86Screens[pScreen->myNum]);
    struct input_event event;
    ssize_t rc;

    do
        rc = read(fd, &event, sizeof(event));
    while (rc > 0 || (rc == -1 && errno == EINTR));
    /* Why do they return EAGAIN instead of zero bytes read like everyone else does? */
    VBVXASSERT(rc != -1 || errno == EAGAIN, ("Reading ACPI input event failed.\n"));
}

void vbvxSetUpLinuxACPI(ScreenPtr pScreen)
{
    NEMUPtr pNemu = NEMUGetRec(xf86Screens[pScreen->myNum]);
    struct dirent *pDirent;
    DIR *pDir;
    int fd = -1;

    if (pNemu->fdACPIDevices != -1 || pNemu->hACPIEventHandler != NULL)
        FatalError("ACPI input file descriptor not initialised correctly.\n");
    pDir = opendir("/dev/input");
    if (pDir == NULL)
        return;
    for (pDirent = readdir(pDir); pDirent != NULL; pDirent = readdir(pDir))
    {
        if (strncmp(pDirent->d_name, "event", sizeof("event") - 1) == 0)
        {
#define BITS_PER_BLOCK (sizeof(unsigned long) * 8)
            char szFile[64] = "/dev/input/";
            char szDevice[64] = "";
            unsigned long afKeys[KEY_MAX / BITS_PER_BLOCK];

            strncat(szFile, pDirent->d_name, sizeof(szFile) - sizeof("/dev/input/"));
            if (fd != -1)
                close(fd);
            fd = open(szFile, O_RDONLY | O_NONBLOCK);
            if (   fd == -1
                || ioctl(fd, EVIOCGNAME(sizeof(szDevice)), szDevice) == -1
                || strcmp(szDevice, "Video Bus") != 0)
                continue;
            if (   ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(afKeys)), afKeys) == -1
                || ((   afKeys[KEY_SWITCHVIDEOMODE / BITS_PER_BLOCK]
                     >> KEY_SWITCHVIDEOMODE % BITS_PER_BLOCK) & 1) == 0)
                break;
            if (ioctl(fd, EVIOCGRAB, (void *)1) != 0)
                break;
            pNemu->hACPIEventHandler
                = xf86AddGeneralHandler(fd, acpiEventHandler, pScreen);
            if (pNemu->hACPIEventHandler == NULL)
                break;
            pNemu->fdACPIDevices = fd;
            fd = -1;
            break;
#undef BITS_PER_BLOCK
        }
    }
    if (fd != -1)
        close(fd);
    closedir(pDir);
}

void vbvxCleanUpLinuxACPI(ScreenPtr pScreen)
{
    NEMUPtr pNemu = NEMUGetRec(xf86Screens[pScreen->myNum]);
    if (pNemu->fdACPIDevices != -1)
        close(pNemu->fdACPIDevices);
    pNemu->fdACPIDevices = -1;
    xf86RemoveGeneralHandler(pNemu->hACPIEventHandler);
    pNemu->hACPIEventHandler = NULL;
}
# endif /* RT_OS_LINUX */
#endif /* NEMUVIDEO_13 */
