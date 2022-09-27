/* $Id: NemuX11Helper.cpp $ */
/** @file
 * Nemu Qt GUI - X11 helpers..
 */

/*
 * Copyright (C) 2008-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QX11Info>
#include <QString>

/* GUI includes: */
#include "NemuX11Helper.h"

/* Other Nemu includes: */
#include <iprt/cdefs.h>

/* rhel3 build hack */
RT_C_DECLS_BEGIN
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
RT_C_DECLS_END

static int  gX11ScreenSaverTimeout;
static BOOL gX11ScreenSaverDpmsAvailable;
static BOOL gX11DpmsState;

X11WMType X11WindowManagerType()
{
    /* Get display: */
    Display *pDisplay = QX11Info::display();
    /* Prepare variables to be reused: */
    Atom atom_property_name;
    Atom atom_returned_type;
    int iReturnedFormat;
    unsigned long ulReturnedItemCount;
    unsigned long ulDummy;
    unsigned char *pcData = 0;
    X11WMType wmType = X11WMType_Unknown;

    /* Ask if root-window supports check for WM name: */
    atom_property_name = XInternAtom(pDisplay, "_NET_SUPPORTING_WM_CHECK", True);
    if (XGetWindowProperty(pDisplay, QX11Info::appRootWindow(), atom_property_name,
                           0, 512, False, XA_WINDOW, &atom_returned_type,
                           &iReturnedFormat, &ulReturnedItemCount, &ulDummy, &pcData) == Success)
    {
        Window WMWindow = None;
        if (atom_returned_type == XA_WINDOW && iReturnedFormat == 32)
            WMWindow = *((Window*)pcData);
        if (pcData)
            XFree(pcData);
        if (WMWindow != None)
        {
            /* Ask root-window for WM name: */
            atom_property_name = XInternAtom(pDisplay, "_NET_WM_NAME", True);
            Atom utf8Atom = XInternAtom(pDisplay, "UTF8_STRING", True);
            if (XGetWindowProperty(pDisplay, WMWindow, atom_property_name,
                                   0, 512, False, utf8Atom, &atom_returned_type,
                                   &iReturnedFormat, &ulReturnedItemCount, &ulDummy, &pcData) == Success)
            {
                if (QString((const char*)pcData).contains("Compiz", Qt::CaseInsensitive))
                    wmType = X11WMType_Compiz;
                else
                if (QString((const char*)pcData).contains("GNOME Shell", Qt::CaseInsensitive))
                    wmType = X11WMType_GNOMEShell;
                else
                if (QString((const char*)pcData).contains("KWin", Qt::CaseInsensitive))
                    wmType = X11WMType_KWin;
                else
                if (QString((const char*)pcData).contains("Mutter", Qt::CaseInsensitive))
                    wmType = X11WMType_Mutter;
                else
                if (QString((const char*)pcData).contains("Xfwm4", Qt::CaseInsensitive))
                    wmType = X11WMType_Xfwm4;
                if (pcData)
                    XFree(pcData);
            }
        }
    }
    return wmType;
}

void X11ScreenSaverSettingsInit()
{
    int     dummy;
    Display *display = QX11Info::display();
    gX11ScreenSaverDpmsAvailable = DPMSQueryExtension(display, &dummy, &dummy);
}

void X11ScreenSaverSettingsSave()
{
    /* Actually this is a big mess. By default the libSDL disables the screen saver
     * during the SDL_InitSubSystem() call and restores the saved settings during
     * the SDL_QuitSubSystem() call. This mechanism can be disabled by setting the
     * environment variable SDL_VIDEO_ALLOW_SCREENSAVER to 1. However, there is a
     * known bug in the Debian libSDL: If this environment variable is set, the
     * screen saver is still disabled but the old state is not restored during
     * SDL_QuitSubSystem()! So the only solution to overcome this problem is to
     * save and restore the state prior and after each of these function calls. */

    int     dummy;
    CARD16  dummy2;
    Display *display = QX11Info::display();

    XGetScreenSaver(display, &gX11ScreenSaverTimeout, &dummy, &dummy, &dummy);
    if (gX11ScreenSaverDpmsAvailable)
        DPMSInfo(display, &dummy2, &gX11DpmsState);
}

void X11ScreenSaverSettingsRestore()
{
    int     timeout, interval, preferBlank, allowExp;
    Display *display = QX11Info::display();

    XGetScreenSaver(display, &timeout, &interval, &preferBlank, &allowExp);
    timeout = gX11ScreenSaverTimeout;
    XSetScreenSaver(display, timeout, interval, preferBlank, allowExp);

    if (gX11DpmsState && gX11ScreenSaverDpmsAvailable)
        DPMSEnable(display);
}

