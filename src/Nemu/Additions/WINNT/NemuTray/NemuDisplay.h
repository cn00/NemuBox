/* $Id:  $ */
/** @file
 * NemuSeamless - Display notifications
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

#ifndef __NEMUSERVICEDISPLAY__H
#define __NEMUSERVICEDISPLAY__H

/* The display service prototypes. */
int                NemuDisplayInit    (const NEMUSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread);
unsigned __stdcall NemuDisplayThread  (void *pInstance);
void               NemuDisplayDestroy (const NEMUSERVICEENV *pEnv, void *pInstance);

DWORD NemuGetDisplayConfigCount();
DWORD NemuGetDisplayConfig(const DWORD NumDevices, DWORD *pDevPrimaryNum, DWORD *pNumDevices, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes);

#ifndef NEMU_WITH_WDDM
static bool isNemuDisplayDriverActive (void);
#else
/* @misha: getNemuDisplayDriverType is used instead.
 * it seems bad to put static function declaration to header,
 * so it is moved to NemuDisplay.cpp */
#endif

#endif /* __NEMUSERVICEDISPLAY__H */
