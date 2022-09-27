/* $Id: NemuDispMini.h $ */

/** @file
 * Nemu XPDM Display driver, helper functions which interacts with our miniport driver
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

#ifndef NEMUDISPMINI_H
#define NEMUDISPMINI_H

#include "NemuDisp.h"

int NemuDispMPGetVideoModes(HANDLE hDriver, PVIDEO_MODE_INFORMATION *ppModesTable, ULONG *cModes);
int NemuDispMPGetPointerCaps(HANDLE hDriver, PVIDEO_POINTER_CAPABILITIES pCaps);
int NemuDispMPSetCurrentMode(HANDLE hDriver, ULONG ulMode);
int NemuDispMPMapMemory(PNEMUDISPDEV pDev, PVIDEO_MEMORY_INFORMATION pMemInfo);
int NemuDispMPUnmapMemory(PNEMUDISPDEV pDev);
int NemuDispMPQueryHGSMIInfo(HANDLE hDriver, QUERYHGSMIRESULT *pInfo);
int NemuDispMPQueryHGSMICallbacks(HANDLE hDriver, HGSMIQUERYCALLBACKS *pCallbacks);
int NemuDispMPHGSMIQueryPortProcs(HANDLE hDriver, HGSMIQUERYCPORTPROCS *pPortProcs);
int NemuDispMPVHWAQueryInfo(HANDLE hDriver, VHWAQUERYINFO *pInfo);
int NemuDispMPSetColorRegisters(HANDLE hDriver, PVIDEO_CLUT pClut, DWORD cbClut);
int NemuDispMPDisablePointer(HANDLE hDriver);
int NemuDispMPSetPointerPosition(HANDLE hDriver, PVIDEO_POINTER_POSITION pPos);
int NemuDispMPSetPointerAttrs(PNEMUDISPDEV pDev);
int NemuDispMPSetVisibleRegion(HANDLE hDriver, PRTRECT pRects, DWORD cRects);
int NemuDispMPResetDevice(HANDLE hDriver);
int NemuDispMPShareVideoMemory(HANDLE hDriver, PVIDEO_SHARE_MEMORY pSMem, PVIDEO_SHARE_MEMORY_INFORMATION pSMemInfo);
int NemuDispMPUnshareVideoMemory(HANDLE hDriver, PVIDEO_SHARE_MEMORY pSMem);
int NemuDispMPQueryRegistryFlags(HANDLE hDriver, ULONG *pulFlags);

#endif /*NEMUDISPMINI_H*/
