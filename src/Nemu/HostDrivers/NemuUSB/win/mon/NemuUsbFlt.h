/* $Id: NemuUsbFlt.h $ */
/** @file
 * Nemu USB Monitor Device Filtering functionality
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
#ifndef ___NemuUsbFlt_h___
#define ___NemuUsbFlt_h___

#include "NemuUsbMon.h"
#include <NemuUSBFilterMgr.h>

#include <Nemu/usblib-win.h>

typedef struct NEMUUSBFLTCTX
{
    LIST_ENTRY ListEntry;
    PKEVENT pChangeEvent;
    RTPROCESS Process;
    uint32_t cActiveFilters;
    BOOLEAN bRemoved;
} NEMUUSBFLTCTX, *PNEMUUSBFLTCTX;

NTSTATUS NemuUsbFltInit();
NTSTATUS NemuUsbFltTerm();
NTSTATUS NemuUsbFltCreate(PNEMUUSBFLTCTX pContext);
NTSTATUS NemuUsbFltClose(PNEMUUSBFLTCTX pContext);
int NemuUsbFltAdd(PNEMUUSBFLTCTX pContext, PUSBFILTER pFilter, uintptr_t *pId);
int NemuUsbFltRemove(PNEMUUSBFLTCTX pContext, uintptr_t uId);
NTSTATUS NemuUsbFltSetNotifyEvent(PNEMUUSBFLTCTX pContext, HANDLE hEvent);
NTSTATUS NemuUsbFltFilterCheck(PNEMUUSBFLTCTX pContext);

NTSTATUS NemuUsbFltGetDevice(PNEMUUSBFLTCTX pContext, HNEMUUSBDEVUSR hDevice, PUSBSUP_GETDEV_MON pInfo);

typedef void* HNEMUUSBFLTDEV;
HNEMUUSBFLTDEV NemuUsbFltProxyStarted(PDEVICE_OBJECT pPdo);
void NemuUsbFltProxyStopped(HNEMUUSBFLTDEV hDev);

NTSTATUS NemuUsbFltPdoAdd(PDEVICE_OBJECT pPdo, BOOLEAN *pbFiltered);
NTSTATUS NemuUsbFltPdoAddCompleted(PDEVICE_OBJECT pPdo);
NTSTATUS NemuUsbFltPdoRemove(PDEVICE_OBJECT pPdo);
BOOLEAN NemuUsbFltPdoIsFiltered(PDEVICE_OBJECT pPdo);

#endif /* #ifndef ___NemuUsbFlt_h___ */
