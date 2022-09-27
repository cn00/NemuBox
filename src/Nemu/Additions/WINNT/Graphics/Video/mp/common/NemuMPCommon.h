/* $Id: NemuMPCommon.h $ */
/** @file
 * Nemu Miniport common functions used by XPDM/WDDM drivers
 */

/*
 * Copyright (C) 2011-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef NEMUMPCOMMON_H
#define NEMUMPCOMMON_H

#include "NemuMPDevExt.h"

RT_C_DECLS_BEGIN

int NemuMPCmnMapAdapterMemory(PNEMUMP_COMMON pCommon, void **ppv, uint32_t ulOffset, uint32_t ulSize);
void NemuMPCmnUnmapAdapterMemory(PNEMUMP_COMMON pCommon, void **ppv);

typedef bool(*PFNVIDEOIRQSYNC)(void *);
bool NemuMPCmnSyncToVideoIRQ(PNEMUMP_COMMON pCommon, PFNVIDEOIRQSYNC pfnSync, void *pvUser);

/* Video modes related */
#ifdef NEMU_XPDM_MINIPORT
void NemuMPCmnInitCustomVideoModes(PNEMUMP_DEVEXT pExt);
VIDEO_MODE_INFORMATION* NemuMPCmnGetCustomVideoModeInfo(ULONG ulIndex);
VIDEO_MODE_INFORMATION* NemuMPCmnGetVideoModeInfo(PNEMUMP_DEVEXT pExt, ULONG ulIndex);
VIDEO_MODE_INFORMATION* NemuMPXpdmCurrentVideoMode(PNEMUMP_DEVEXT pExt);
ULONG NemuMPXpdmGetVideoModesCount(PNEMUMP_DEVEXT pExt);
void NemuMPXpdmBuildVideoModesTable(PNEMUMP_DEVEXT pExt);
#endif

/* Registry access */
#ifdef NEMU_XPDM_MINIPORT
typedef PNEMUMP_DEVEXT NEMUMPCMNREGISTRY;
#else
typedef HANDLE NEMUMPCMNREGISTRY;
#endif

VP_STATUS NemuMPCmnRegInit(IN PNEMUMP_DEVEXT pExt, OUT NEMUMPCMNREGISTRY *pReg);
VP_STATUS NemuMPCmnRegFini(IN NEMUMPCMNREGISTRY Reg);
VP_STATUS NemuMPCmnRegSetDword(IN NEMUMPCMNREGISTRY Reg, PWSTR pName, uint32_t Val);
VP_STATUS NemuMPCmnRegQueryDword(IN NEMUMPCMNREGISTRY Reg, PWSTR pName, uint32_t *pVal);

/* Pointer related */
bool NemuMPCmnUpdatePointerShape(PNEMUMP_COMMON pCommon, PVIDEO_POINTER_ATTRIBUTES pAttrs, uint32_t cbLength);

RT_C_DECLS_END

#endif /*NEMUMPCOMMON_H*/
