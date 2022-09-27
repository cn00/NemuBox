/* $Id: NemuUsbHook.h $ */
/** @file
 * Driver Dispatch Table Hooking API impl
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

#ifndef ___NemuUsbHook_h___
#define ___NemuUsbHook_h___

#include "NemuUsbMon.h"

typedef struct NEMUUSBHOOK_ENTRY
{
    LIST_ENTRY RequestList;
    KSPIN_LOCK Lock;
    BOOLEAN fIsInstalled;
    PDRIVER_DISPATCH pfnOldHandler;
    NEMUDRVTOOL_REF HookRef;
    PDRIVER_OBJECT pDrvObj;
    UCHAR iMjFunction;
    PDRIVER_DISPATCH pfnHook;
} NEMUUSBHOOK_ENTRY, *PNEMUUSBHOOK_ENTRY;

typedef struct NEMUUSBHOOK_REQUEST
{
    LIST_ENTRY ListEntry;
    PNEMUUSBHOOK_ENTRY pHook;
    IO_STACK_LOCATION OldLocation;
    PDEVICE_OBJECT pDevObj;
    PIRP pIrp;
    BOOLEAN bCompletionStopped;
} NEMUUSBHOOK_REQUEST, *PNEMUUSBHOOK_REQUEST;

DECLINLINE(BOOLEAN) NemuUsbHookRetain(PNEMUUSBHOOK_ENTRY pHook)
{
    KIRQL Irql;
    KeAcquireSpinLock(&pHook->Lock, &Irql);
    if (!pHook->fIsInstalled)
    {
        KeReleaseSpinLock(&pHook->Lock, Irql);
        return FALSE;
    }

    NemuDrvToolRefRetain(&pHook->HookRef);
    KeReleaseSpinLock(&pHook->Lock, Irql);
    return TRUE;
}

DECLINLINE(VOID) NemuUsbHookRelease(PNEMUUSBHOOK_ENTRY pHook)
{
    NemuDrvToolRefRelease(&pHook->HookRef);
}

VOID NemuUsbHookInit(PNEMUUSBHOOK_ENTRY pHook, PDRIVER_OBJECT pDrvObj, UCHAR iMjFunction, PDRIVER_DISPATCH pfnHook);
NTSTATUS NemuUsbHookInstall(PNEMUUSBHOOK_ENTRY pHook);
NTSTATUS NemuUsbHookUninstall(PNEMUUSBHOOK_ENTRY pHook);
BOOLEAN NemuUsbHookIsInstalled(PNEMUUSBHOOK_ENTRY pHook);
NTSTATUS NemuUsbHookRequestPassDownHookCompletion(PNEMUUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp, PIO_COMPLETION_ROUTINE pfnCompletion, PNEMUUSBHOOK_REQUEST pRequest);
NTSTATUS NemuUsbHookRequestPassDownHookSkip(PNEMUUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS NemuUsbHookRequestMoreProcessingRequired(PNEMUUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp, PNEMUUSBHOOK_REQUEST pRequest);
NTSTATUS NemuUsbHookRequestComplete(PNEMUUSBHOOK_ENTRY pHook, PDEVICE_OBJECT pDevObj, PIRP pIrp, PNEMUUSBHOOK_REQUEST pRequest);
VOID NemuUsbHookVerifyCompletion(PNEMUUSBHOOK_ENTRY pHook, PNEMUUSBHOOK_REQUEST pRequest, PIRP pIrp);

#endif /* #ifndef ___NemuUsbHook_h___ */
