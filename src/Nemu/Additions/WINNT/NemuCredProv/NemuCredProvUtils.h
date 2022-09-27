/** @file
 * NemuCredProvUtils - Misc. utility functions for NemuCredProv.
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NEMU_CREDPROV_UTILS_H___
#define ___NEMU_CREDPROV_UTILS_H___

#include <Nemu/NemuGuestLib.h>

extern DWORD g_dwVerbosity;

void NemuCredProvVerbose(DWORD dwLevel, const char *pszFormat, ...);
int  NemuCredProvReportStatus(NemuGuestFacilityStatus enmStatus);

#endif /* !___NEMU_CREDPROV_UTILS_H___ */

