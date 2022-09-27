/* $Id: NemuUsbTool.h $ */
/** @file
 * Windows USB R0 Tooling.
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
#ifndef ___NemuUsbTool_h___
#define ___NemuUsbTool_h___

#include "NemuDrvTool.h"

RT_C_DECLS_BEGIN
#pragma warning( disable : 4200 )
#include <usbdi.h>
#pragma warning( default : 4200 )
#include <usbdlib.h>

RT_C_DECLS_END

#if 0
/* enable this in case we include this in a dll*/
# ifdef IN_NEMUUSBTOOL
#  define NEMUUSBTOOL_DECL(a_Type) DECLEXPORT(a_Type)
# else
#  define NEMUUSBTOOL_DECL(a_Type) DECLIMPORT(a_Type)
# endif
#else
/*enable this in case we include this in a static lib*/
# define NEMUUSBTOOL_DECL(a_Type) a_Type NEMUCALL
#endif


RT_C_DECLS_BEGIN

NEMUUSBTOOL_DECL(PURB) NemuUsbToolUrbAlloc(USHORT u16Function, USHORT cbSize);
NEMUUSBTOOL_DECL(PURB) NemuUsbToolUrbAllocZ(USHORT u16Function, USHORT cbSize);
NEMUUSBTOOL_DECL(PURB) NemuUsbToolUrbReinit(PURB pUrb, USHORT cbSize, USHORT u16Function);
NEMUUSBTOOL_DECL(VOID) NemuUsbToolUrbFree(PURB pUrb);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolUrbPost(PDEVICE_OBJECT pDevObj, PURB pUrb, ULONG dwTimeoutMs);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolGetDescriptor(PDEVICE_OBJECT pDevObj, void *pvBuffer, int cbBuffer, int Type, int iIndex, int LangId, ULONG dwTimeoutMs);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolGetStringDescriptorA(PDEVICE_OBJECT pDevObj, char *pResult, ULONG cbResult, int iIndex, int LangId, ULONG dwTimeoutMs);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolGetLangID(PDEVICE_OBJECT pDevObj, int *pLangId, ULONG dwTimeoutMs);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolGetDeviceSpeed(PDEVICE_OBJECT pDevObj, BOOLEAN *pbIsHigh);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolPipeClear(PDEVICE_OBJECT pDevObj, HANDLE hPipe, bool fReset);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolCurrentFrame(PDEVICE_OBJECT pDevObj, PIRP pIrp, PULONG piFrame);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolDevUnconfigure(PDEVICE_OBJECT pDevObj);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolIoInternalCtlSendAsync(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2, PKEVENT pEvent, PIO_STATUS_BLOCK pIoStatus);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolIoInternalCtlSendSync(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2);
NEMUUSBTOOL_DECL(PIRP) NemuUsbToolIoBuildAsyncInternalCtl(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2);
NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolIoInternalCtlSendSyncWithTimeout(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2, ULONG dwTimeoutMs);
NEMUUSBTOOL_DECL(VOID) NemuUsbToolStringDescriptorToUnicodeString(PUSB_STRING_DESCRIPTOR pDr, PUNICODE_STRING pUnicode);


RT_C_DECLS_END

#endif /* #ifndef ___NemuUsbTool_h___ */
