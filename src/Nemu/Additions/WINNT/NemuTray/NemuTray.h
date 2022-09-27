/* $Id: NemuTray.h $ */
/** @file
 * NemuTray - Guest Additions Tray, Internal Header.
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

#ifndef ___NEMUTRAY_H
#define ___NEMUTRAY_H

#       define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
#       define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
#       define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
#       define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
#       define _interlockedbittestandset      _interlockedbittestandset_StupidDDKVsCompilerCrap
#       define _interlockedbittestandreset    _interlockedbittestandreset_StupidDDKVsCompilerCrap
#       define _interlockedbittestandset64    _interlockedbittestandset64_StupidDDKVsCompilerCrap
#       define _interlockedbittestandreset64  _interlockedbittestandreset64_StupidDDKVsCompilerCrap
#       pragma warning(disable : 4163)
#include <windows.h>
#       pragma warning(default : 4163)
#       undef  _InterlockedExchange
#       undef  _InterlockedExchangeAdd
#       undef  _InterlockedCompareExchange
#       undef  _InterlockedAddLargeStatistic
#       undef  _interlockedbittestandset
#       undef  _interlockedbittestandreset
#       undef  _interlockedbittestandset64
#       undef  _interlockedbittestandreset64

#include <tchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <process.h>

#include <iprt/initterm.h>
#include <iprt/string.h>

#include <Nemu/version.h>
#include <Nemu/NemuGuest.h> /** @todo use the VbglR3 interface! */
#include <Nemu/NemuGuestLib.h>
#include <NemuDisplay.h>

#include "NemuDispIf.h"

/*
 * Windows messsages.
 */

/**
 * General NemuTray messages.
 */
#define WM_NEMUTRAY_TRAY_ICON                   WM_APP + 40


/* The tray icon's ID. */
#define ID_TRAYICON                             2000


/*
 * Timer IDs.
 */
#define TIMERID_NEMUTRAY_CHECK_HOSTVERSION      1000
#define TIMERID_NEMUTRAY_CAPS_TIMER             1001
#define TIMERID_NEMUTRAY_DT_TIMER               1002
#define TIMERID_NEMUTRAY_ST_DELAYED_INIT_TIMER  1003

/* The environment information for services. */
typedef struct _NEMUSERVICEENV
{
    HINSTANCE hInstance;
    HANDLE    hDriver;
    HANDLE    hStopEvent;
    /* display driver interface, XPDM - WDDM abstraction see NEMUDISPIF** definitions above */
    NEMUDISPIF dispIf;
} NEMUSERVICEENV;

/* The service initialization info and runtime variables. */
typedef struct _NEMUSERVICEINFO
{
    char     *pszName;
    int      (* pfnInit)             (const NEMUSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread);
    unsigned (__stdcall * pfnThread) (void *pInstance);
    void     (* pfnStop)             (const NEMUSERVICEENV *pEnv, void *pInstance);
    void     (* pfnDestroy)          (const NEMUSERVICEENV *pEnv, void *pInstance);

    /* Variables. */
    HANDLE   hThread;
    void    *pInstance;
    bool     fStarted;
} NEMUSERVICEINFO;

/* Globally unique (system wide) message registration. */
typedef struct _NEMUGLOBALMESSAGE
{
    /** Message name. */
    char    *pszName;
    /** Function pointer for handling the message. */
    int      (* pfnHandler)          (WPARAM wParam, LPARAM lParam);

    /* Variables. */

    /** Message ID;
     *  to be filled in when registering the actual message. */
    UINT     uMsgID;
} NEMUGLOBALMESSAGE, *PNEMUGLOBALMESSAGE;

extern HWND         ghwndToolWindow;
extern HINSTANCE    ghInstance;

#endif /* !___NEMUTRAY_H */

