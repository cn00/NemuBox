/* $Id: NemuHook.h $ */
/** @file
 * NemuHook -- Global windows hook dll.
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
 */
#ifndef ___winnt_include_NemuHook_h
#define ___winnt_include_NemuHook_h

/* custom messages as we must install the hook from the main thread */
/** @todo r=andy Use WM_APP + n offsets here! */
#define WM_NEMU_SEAMLESS_ENABLE                     0x2001
#define WM_NEMU_SEAMLESS_DISABLE                    0x2002
#define WM_NEMU_SEAMLESS_UPDATE                     0x2003
#define WM_NEMU_GRAPHICS_SUPPORTED                  0x2004
#define WM_NEMU_GRAPHICS_UNSUPPORTED                0x2005


#define NEMUHOOK_DLL_NAME              "NemuHook.dll"
#define NEMUHOOK_GLOBAL_DT_EVENT_NAME  "Local\\NemuHookDtNotifyEvent"
#define NEMUHOOK_GLOBAL_WT_EVENT_NAME  "Local\\NemuHookWtNotifyEvent"

BOOL NemuHookInstallActiveDesktopTracker(HMODULE hDll);
BOOL NemuHookRemoveActiveDesktopTracker();

BOOL NemuHookInstallWindowTracker(HMODULE hDll);
BOOL NemuHookRemoveWindowTracker();

#endif

