/* $Id: NemuUsbMon.h $ */
/** @file
 * Nemu USB Monitor
 */
/*
 * Copyright (C) 2011-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___NemuUsbMon_h___
#define ___NemuUsbMon_h___

#include <Nemu/cdefs.h>
#include <Nemu/types.h>
#include <iprt/assert.h>
#include <Nemu/sup.h>
#include <iprt/asm.h>
#include <Nemu/log.h>

#ifdef DEBUG
/* disables filters */
//#define NEMUUSBMON_DBG_NO_FILTERS
/* disables pnp hooking */
//#define NEMUUSBMON_DBG_NO_PNPHOOK
#endif

#include "../../../win/NemuDbgLog.h"
#include "../cmn/NemuDrvTool.h"
#include "../cmn/NemuUsbTool.h"

#include "NemuUsbHook.h"
#include "NemuUsbFlt.h"

PVOID NemuUsbMonMemAlloc(SIZE_T cbBytes);
PVOID NemuUsbMonMemAllocZ(SIZE_T cbBytes);
VOID NemuUsbMonMemFree(PVOID pvMem);

NTSTATUS NemuUsbMonGetDescriptor(PDEVICE_OBJECT pDevObj, void *buffer, int size, int type, int index, int language_id);
NTSTATUS NemuUsbMonQueryBusRelations(PDEVICE_OBJECT pDevObj, PFILE_OBJECT pFileObj, PDEVICE_RELATIONS *pDevRelations);

void nemuUsbDbgPrintUnicodeString(PUNICODE_STRING pUnicodeString);

/* visit usbhub-originated device PDOs */
#define NEMUUSBMONHUBWALK_F_PDO 0x00000001
/* visit usbhub device FDOs */
#define NEMUUSBMONHUBWALK_F_FDO 0x00000002
/* visit all usbhub-originated device objects */
#define NEMUUSBMONHUBWALK_F_ALL (NEMUUSBMONHUBWALK_F_FDO | NEMUUSBMONHUBWALK_F_PDO)

typedef DECLCALLBACK(BOOLEAN) FNNEMUUSBMONDEVWALKER(PFILE_OBJECT pFile, PDEVICE_OBJECT pTopDo, PDEVICE_OBJECT pHubDo, PVOID pvContext);
typedef FNNEMUUSBMONDEVWALKER *PFNNEMUUSBMONDEVWALKER;

VOID nemuUsbMonHubDevWalk(PFNNEMUUSBMONDEVWALKER pfnWalker, PVOID pvWalker, ULONG fFlags);

#endif /* #ifndef ___NemuUsbMon_h___ */
