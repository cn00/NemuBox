/* $Id: NemuMPInternal.h $ */

/** @file
 * Nemu XPDM Miniport internal header
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

#ifndef NEMUMPINTERNAL_H
#define NEMUMPINTERNAL_H

#include "common/NemuMPUtils.h"
#include "common/NemuMPDevExt.h"
#include "common/xpdm/NemuVideoIOCTL.h"

RT_C_DECLS_BEGIN
ULONG DriverEntry(IN PVOID Context1, IN PVOID Context2);
RT_C_DECLS_END

/* ==================== Misc ==================== */
void NemuSetupVideoPortAPI(PNEMUMP_DEVEXT pExt, PVIDEO_PORT_CONFIG_INFO pConfigInfo);
void NemuCreateDisplays(PNEMUMP_DEVEXT pExt, PVIDEO_PORT_CONFIG_INFO pConfigInfo);
int NemuVbvaEnable(PNEMUMP_DEVEXT pExt, BOOLEAN bEnable, VBVAENABLERESULT *pResult);
DECLCALLBACK(void) NemuMPHGSMIHostCmdCompleteCB(HNEMUVIDEOHGSMI hHGSMI, struct VBVAHOSTCMD *pCmd);
DECLCALLBACK(int) NemuMPHGSMIHostCmdRequestCB(HNEMUVIDEOHGSMI hHGSMI, uint8_t u8Channel, uint32_t iDisplay, struct VBVAHOSTCMD **ppCmd);
int NemuVbvaChannelDisplayEnable(PNEMUMP_COMMON pCommon, int iDisplay, uint8_t u8Channel);

/* ==================== System VRP's handlers ==================== */
BOOLEAN NemuMPSetCurrentMode(PNEMUMP_DEVEXT pExt, PVIDEO_MODE pMode, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPResetDevice(PNEMUMP_DEVEXT pExt, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPMapVideoMemory(PNEMUMP_DEVEXT pExt, PVIDEO_MEMORY pRequestedAddress,
                             PVIDEO_MEMORY_INFORMATION pMapInfo, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPUnmapVideoMemory(PNEMUMP_DEVEXT pExt, PVIDEO_MEMORY VideoMemory, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPShareVideoMemory(PNEMUMP_DEVEXT pExt, PVIDEO_SHARE_MEMORY pShareMem,
                               PVIDEO_SHARE_MEMORY_INFORMATION pShareMemInfo, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPUnshareVideoMemory(PNEMUMP_DEVEXT pExt, PVIDEO_SHARE_MEMORY pMem, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPQueryCurrentMode(PNEMUMP_DEVEXT pExt, PVIDEO_MODE_INFORMATION pModeInfo, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPQueryNumAvailModes(PNEMUMP_DEVEXT pExt, PVIDEO_NUM_MODES pNumModes, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPQueryAvailModes(PNEMUMP_DEVEXT pExt, PVIDEO_MODE_INFORMATION pModes, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPSetColorRegisters(PNEMUMP_DEVEXT pExt, PVIDEO_CLUT pClut, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPSetPointerAttr(PNEMUMP_DEVEXT pExt, PVIDEO_POINTER_ATTRIBUTES pPointerAttrs, uint32_t cbLen, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPEnablePointer(PNEMUMP_DEVEXT pExt, BOOLEAN bEnable, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPQueryPointerPosition(PNEMUMP_DEVEXT pExt, PVIDEO_POINTER_POSITION pPos, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPQueryPointerCapabilities(PNEMUMP_DEVEXT pExt, PVIDEO_POINTER_CAPABILITIES pCaps, PSTATUS_BLOCK pStatus);

/* ==================== Virtual Box VRP's handlers ==================== */
BOOLEAN NemuMPVBVAEnable(PNEMUMP_DEVEXT pExt, BOOLEAN bEnable, VBVAENABLERESULT *pResult, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPSetVisibleRegion(uint32_t cRects, RTRECT *pRects, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPHGSMIQueryPortProcs(PNEMUMP_DEVEXT pExt, HGSMIQUERYCPORTPROCS *pProcs, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPHGSMIQueryCallbacks(PNEMUMP_DEVEXT pExt, HGSMIQUERYCALLBACKS *pCallbacks, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPQueryHgsmiInfo(PNEMUMP_DEVEXT pExt, QUERYHGSMIRESULT *pResult, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPHgsmiHandlerEnable(PNEMUMP_DEVEXT pExt, HGSMIHANDLERENABLE *pChannel, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPVhwaQueryInfo(PNEMUMP_DEVEXT pExt, VHWAQUERYINFO *pInfo, PSTATUS_BLOCK pStatus);
BOOLEAN NemuMPQueryRegistryFlags(PNEMUMP_DEVEXT pExt, ULONG *pulFlags, PSTATUS_BLOCK pStatus);

#endif /*NEMUMPINTERNAL_H*/
