/* $Id: NemuHlp.h $ */
/** @file
 * Nemu Qt GUI - Declaration of OS/2-specific helpers that require to reside in a DLL.
 */

/*
 * Copyright (C) 2008-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuHlp_h
#define ___NemuHlp_h

#include <iprt/cdefs.h>

#ifdef IN_NEMUHLP
# define NEMUHLPDECL(type) DECLEXPORT(type) RTCALL
#else
# define NEMUHLPDECL(type) DECLIMPORT(type) RTCALL
#endif

NEMUHLPDECL(bool) NemuHlpInstallKbdHook (HAB aHab, HWND aHwnd,
                                           unsigned long aMsg);

NEMUHLPDECL(bool) NemuHlpUninstallKbdHook (HAB aHab, HWND aHwnd,
                                           unsigned long aMsg);

#endif /* !___NemuHlp_h */

