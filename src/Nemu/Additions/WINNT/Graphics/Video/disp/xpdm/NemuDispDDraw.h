/* $Id: NemuDispDDraw.h $ */

/** @file
 * Nemu XPDM Display driver, direct draw callbacks
 */

/*
 * Copyright (C) 2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef NEMUDISPDDRAW_H
#define NEMUDISPDDRAW_H

#include <winddi.h>

DWORD APIENTRY NemuDispDDCanCreateSurface(PDD_CANCREATESURFACEDATA lpCanCreateSurface);
DWORD APIENTRY NemuDispDDCreateSurface(PDD_CREATESURFACEDATA lpCreateSurface);
DWORD APIENTRY NemuDispDDDestroySurface(PDD_DESTROYSURFACEDATA lpDestroySurface);
DWORD APIENTRY NemuDispDDLock(PDD_LOCKDATA lpLock);
DWORD APIENTRY NemuDispDDUnlock(PDD_UNLOCKDATA lpUnlock);
DWORD APIENTRY NemuDispDDMapMemory(PDD_MAPMEMORYDATA lpMapMemory);

#ifdef NEMU_WITH_VIDEOHWACCEL
int NemuDispVHWAUpdateDDHalInfo(PNEMUDISPDEV pDev, DD_HALINFO *pHalInfo);

DWORD APIENTRY NemuDispDDGetDriverInfo(DD_GETDRIVERINFODATA *lpData);
DWORD APIENTRY NemuDispDDSetColorKey(PDD_SETCOLORKEYDATA lpSetColorKey);
DWORD APIENTRY NemuDispDDAddAttachedSurface(PDD_ADDATTACHEDSURFACEDATA lpAddAttachedSurface);
DWORD APIENTRY NemuDispDDBlt(PDD_BLTDATA lpBlt);
DWORD APIENTRY NemuDispDDFlip(PDD_FLIPDATA lpFlip);
DWORD APIENTRY NemuDispDDGetBltStatus(PDD_GETBLTSTATUSDATA lpGetBltStatus);
DWORD APIENTRY NemuDispDDGetFlipStatus(PDD_GETFLIPSTATUSDATA lpGetFlipStatus);
DWORD APIENTRY NemuDispDDSetOverlayPosition(PDD_SETOVERLAYPOSITIONDATA lpSetOverlayPosition);
DWORD APIENTRY NemuDispDDUpdateOverlay(PDD_UPDATEOVERLAYDATA lpUpdateOverlay);
#endif

#endif /*NEMUDISPDDRAW_H*/
