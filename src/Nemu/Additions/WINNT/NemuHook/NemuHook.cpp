/* $Id: NemuHook.cpp $ */
/** @file
 * NemuHook -- Global windows hook dll
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <Windows.h>
#include <NemuHook.h>
#include <Nemu/NemuGuestLib.h>
#include <stdio.h>

#pragma data_seg("SHARED")
static HWINEVENTHOOK    hWinEventHook[2]    = {0};
static HWINEVENTHOOK    hDesktopEventHook   = NULL;
#pragma data_seg()
#pragma comment(linker, "/section:SHARED,RWS")

static HANDLE   hWinNotifyEvent = 0;
static HANDLE   hDesktopNotifyEvent = 0;

#ifdef DEBUG
static void WriteLog(const char *pszFormat, ...);
# define dprintf(a) do { WriteLog a; } while (0)
#else
# define dprintf(a) do {} while (0)
#endif /* !DEBUG */


static void CALLBACK NemuHandleWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                        LONG idObject, LONG idChild,
                                        DWORD dwEventThread, DWORD dwmsEventTime)
{
    DWORD dwStyle;
    if (    idObject != OBJID_WINDOW
        ||  !hwnd)
        return;

    dwStyle  = GetWindowLong(hwnd, GWL_STYLE);
    if (dwStyle & WS_CHILD)
        return;

    switch(event)
    {
    case EVENT_OBJECT_LOCATIONCHANGE:
        if (!(dwStyle & WS_VISIBLE))
            return;

    case EVENT_OBJECT_CREATE:
    case EVENT_OBJECT_DESTROY:
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_SHOW:
#ifdef DEBUG
        switch(event)
        {
        case EVENT_OBJECT_LOCATIONCHANGE:
            dprintf(("NemuHandleWinEvent EVENT_OBJECT_LOCATIONCHANGE for window %x\n", hwnd));
            break;
        case EVENT_OBJECT_CREATE:
            dprintf(("NemuHandleWinEvent EVENT_OBJECT_CREATE for window %x\n", hwnd));
            break;
        case EVENT_OBJECT_HIDE:
            dprintf(("NemuHandleWinEvent EVENT_OBJECT_HIDE for window %x\n", hwnd));
            break;
        case EVENT_OBJECT_SHOW:
            dprintf(("NemuHandleWinEvent EVENT_OBJECT_SHOW for window %x\n", hwnd));
            break;
        case EVENT_OBJECT_DESTROY:
            dprintf(("NemuHandleWinEvent EVENT_OBJECT_DESTROY for window %x\n", hwnd));
            break;
        }
#endif
        if (!hWinNotifyEvent)
        {
            hWinNotifyEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, NEMUHOOK_GLOBAL_WT_EVENT_NAME);
            dprintf(("OpenEvent returned %x (last err=%x)\n", hWinNotifyEvent, GetLastError()));
        }
        BOOL ret = SetEvent(hWinNotifyEvent);
        dprintf(("SetEvent %x returned %d (last error %x)\n", hWinNotifyEvent, ret, GetLastError()));
        break;
    }
}

static void CALLBACK NemuHandleDesktopEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                            LONG idObject, LONG idChild,
                                            DWORD dwEventThread, DWORD dwmsEventTime)
{
    if (!hDesktopNotifyEvent)
    {
        hDesktopNotifyEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, NEMUHOOK_GLOBAL_DT_EVENT_NAME);
        dprintf(("OpenEvent returned %x (last err=%x)\n", hDesktopNotifyEvent, GetLastError()));
    }
    BOOL ret = SetEvent(hDesktopNotifyEvent);
    dprintf(("SetEvent %x returned %d (last error %x)\n", hDesktopNotifyEvent, ret, GetLastError()));
}

BOOL NemuHookInstallActiveDesktopTracker(HMODULE hDll)
{
    if (hDesktopEventHook)
        return TRUE;

    CoInitialize(NULL);
    hDesktopEventHook = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH,
                                        hDll,
                                        NemuHandleDesktopEvent,
                                        0, 0,
                                        0);

    return !!hDesktopEventHook;

}

BOOL NemuHookRemoveActiveDesktopTracker()
{
    if (hDesktopEventHook)
    {
        UnhookWinEvent(hDesktopEventHook);
        CoUninitialize();
    }
    hDesktopEventHook = 0;
    return TRUE;
}

/** Install the global message hook */
BOOL NemuHookInstallWindowTracker(HMODULE hDll)
{
    if (hWinEventHook[0] || hWinEventHook[1])
        return TRUE;

    CoInitialize(NULL);
    hWinEventHook[0] = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                       hDll,
                                       NemuHandleWinEvent,
                                       0, 0,
                                       WINEVENT_INCONTEXT | WINEVENT_SKIPOWNPROCESS);

    hWinEventHook[1] = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE,
                                       hDll,
                                       NemuHandleWinEvent,
                                       0, 0,
                                       WINEVENT_INCONTEXT | WINEVENT_SKIPOWNPROCESS);
    return !!hWinEventHook[0];
}

/** Remove the global message hook */
BOOL NemuHookRemoveWindowTracker()
{
    if (hWinEventHook[0] && hWinEventHook[1])
    {
        UnhookWinEvent(hWinEventHook[0]);
        UnhookWinEvent(hWinEventHook[1]);
        CoUninitialize();
    }
    hWinEventHook[0]  = hWinEventHook[1] = 0;
    return TRUE;
}


#ifdef DEBUG
# include <Nemu/NemuGuest.h>
# include <Nemu/VMMDev.h>

/**
 * dprintf worker using NemuGuest.sys and VMMDevReq_LogString.
 */
static void WriteLog(const char *pszFormat, ...)
{
    /*
     * Open Nemu guest driver once.
     */
    static HANDLE s_hNemuGuest = INVALID_HANDLE_VALUE;
    HANDLE hNemuGuest = s_hNemuGuest;
    if (hNemuGuest == INVALID_HANDLE_VALUE)
    {
        hNemuGuest = CreateFile(NEMUGUEST_DEVICE_NAME,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                NULL);
        if (hNemuGuest == INVALID_HANDLE_VALUE)
            return;
        s_hNemuGuest = hNemuGuest;
    }

    /*
     * We're apparently afraid of using stack here, so we use a static buffer
     * instead and pray we won't be here at the same time on two threads...
     */
    static union
    {
        VMMDevReqLogString Req;
        uint8_t abBuf[1024];
    } s_uBuf;

    vmmdevInitRequest(&s_uBuf.Req.header, VMMDevReq_LogString);

    va_list va;
    va_start(va, pszFormat);
    size_t cch = vsprintf(s_uBuf.Req.szString, pszFormat, va);
    va_end(va);

    s_uBuf.Req.header.size += (uint32_t)cch;
    if (s_uBuf.Req.header.size > sizeof(s_uBuf))
        __debugbreak();

    DWORD cbReturned;
    DeviceIoControl(hNemuGuest, NEMUGUEST_IOCTL_VMMREQUEST(s_uBuf.Req.size),
                    &s_uBuf.Req, s_uBuf.Req.header.size,
                    &s_uBuf.Req, s_uBuf.Req.header.size,
                    &cbReturned, NULL);
}

#endif /* DEBUG */

