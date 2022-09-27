/* $Id: NemuDrvTool.cpp $ */
/** @file
 * Windows Driver R0 Tooling.
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

#include "NemuDrvTool.h"

#include <iprt/assert.h>
#include <Nemu/log.h>

#include "../../../win/NemuDbgLog.h"

#define NEMUDRVTOOL_MEMTAG 'TDBV'

static PVOID nemuDrvToolMemAlloc(SIZE_T cbBytes)
{
    PVOID pvMem = ExAllocatePoolWithTag(NonPagedPool, cbBytes, NEMUDRVTOOL_MEMTAG);
    Assert(pvMem);
    return pvMem;
}

static PVOID nemuDrvToolMemAllocZ(SIZE_T cbBytes)
{
    PVOID pvMem = nemuDrvToolMemAlloc(cbBytes);
    if (pvMem)
    {
        RtlZeroMemory(pvMem, cbBytes);
    }
    return pvMem;
}

static VOID nemuDrvToolMemFree(PVOID pvMem)
{
    ExFreePoolWithTag(pvMem, NEMUDRVTOOL_MEMTAG);
}

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegOpenKeyU(OUT PHANDLE phKey, IN PUNICODE_STRING pName, IN ACCESS_MASK fAccess)
{
    OBJECT_ATTRIBUTES ObjAttr;

    InitializeObjectAttributes(&ObjAttr, pName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    return ZwOpenKey(phKey, fAccess, &ObjAttr);
}

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegOpenKey(OUT PHANDLE phKey, IN PWCHAR pName, IN ACCESS_MASK fAccess)
{
    UNICODE_STRING RtlStr;
    RtlInitUnicodeString(&RtlStr, pName);

    return NemuDrvToolRegOpenKeyU(phKey, &RtlStr, fAccess);
}

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegCloseKey(IN HANDLE hKey)
{
    return ZwClose(hKey);
}

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegQueryValueDword(IN HANDLE hKey, IN PWCHAR pName, OUT PULONG pDword)
{
    struct
    {
        KEY_VALUE_PARTIAL_INFORMATION Info;
        UCHAR Buf[32]; /* should be enough */
    } Buf;
    ULONG cbBuf;
    UNICODE_STRING RtlStr;
    RtlInitUnicodeString(&RtlStr, pName);
    NTSTATUS Status = ZwQueryValueKey(hKey,
                &RtlStr,
                KeyValuePartialInformation,
                &Buf.Info,
                sizeof(Buf),
                &cbBuf);
    if (Status == STATUS_SUCCESS)
    {
        if (Buf.Info.Type == REG_DWORD)
        {
            Assert(Buf.Info.DataLength == 4);
            *pDword = *((PULONG)Buf.Info.Data);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INVALID_PARAMETER;
}

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegSetValueDword(IN HANDLE hKey, IN PWCHAR pName, OUT ULONG val)
{
    UNICODE_STRING RtlStr;
    RtlInitUnicodeString(&RtlStr, pName);
    return ZwSetValueKey(hKey, &RtlStr,
            NULL, /* IN ULONG  TitleIndex  OPTIONAL, reserved */
            REG_DWORD,
            &val,
            sizeof(val));
}

static NTSTATUS nemuDrvToolIoCompletionSetEvent(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp, IN PVOID pvContext)
{
    PKEVENT pEvent = (PKEVENT)pvContext;
    KeSetEvent(pEvent, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolIoPostAsync(PDEVICE_OBJECT pDevObj, PIRP pIrp, PKEVENT pEvent)
{
    IoSetCompletionRoutine(pIrp, nemuDrvToolIoCompletionSetEvent, pEvent, TRUE, TRUE, TRUE);
    return IoCallDriver(pDevObj, pIrp);
}

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolIoPostSync(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    KEVENT Event;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    NTSTATUS Status = NemuDrvToolIoPostAsync(pDevObj, pIrp, &Event);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = pIrp->IoStatus.Status;
    }
    return Status;
}

/* !!!NOTE: the caller MUST be the IRP owner!!! *
 * !! one can not post threaded IRPs this way!! */
NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolIoPostSyncWithTimeout(PDEVICE_OBJECT pDevObj, PIRP pIrp, ULONG dwTimeoutMs)
{
    KEVENT Event;
    LOG(("post irp (0x%p) to DevObj(0x%p) with timeout (%u)", pIrp, pDevObj, dwTimeoutMs));

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    NTSTATUS Status = NemuDrvToolIoPostAsync(pDevObj, pIrp, &Event);
    if (Status == STATUS_PENDING)
    {
        LARGE_INTEGER Interval;
        PLARGE_INTEGER pInterval = NULL;
        if (dwTimeoutMs != RT_INDEFINITE_WAIT)
        {
            Interval.QuadPart = -(int64_t) dwTimeoutMs /* ms */ * 10000;
            pInterval = &Interval;
        }

        Status = KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, pInterval);
        if (Status == STATUS_TIMEOUT)
        {
            WARN(("irp (0x%p) to DevObj(0x%p) was not completed within timeout (%u), cancelling", pIrp, pDevObj, dwTimeoutMs));
            if (!IoCancelIrp(pIrp))
            {
                /* this may happen, but this is something the caller with timeout is not expecting */
                WARN(("IoCancelIrp failed"));
            }

            /* wait for the IRP to complete */
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        }
        else
        {
            ASSERT_WARN(Status == STATUS_SUCCESS, ("uunexpected Status (0x%x)", Status));
        }

        /* by this time the IRP is completed */
        Status = pIrp->IoStatus.Status;
        LOG(("Pending IRP(0x%p) completed with status(0x%x)", pIrp, Status));
    }
    else
    {
        LOG(("IRP(0x%p) completed with status(0x%x)", pIrp, Status));
    }
    return Status;
}

NEMUDRVTOOL_DECL(VOID) NemuDrvToolRefWaitEqual(PNEMUDRVTOOL_REF pRef, uint32_t u32Val)
{
    LARGE_INTEGER Interval;
    Interval.QuadPart = -(int64_t) 2 /* ms */ * 10000;
    uint32_t cRefs;
    size_t loops = 0;
    KTIMER kTimer;
    NTSTATUS status = STATUS_SUCCESS;

    KeInitializeTimer(&kTimer);

    while ((cRefs = ASMAtomicReadU32(&pRef->cRefs)) > u32Val && loops < 256)
    {
        Assert(cRefs >= u32Val);
        Assert(cRefs < UINT32_MAX/2);

        KeSetTimer(&kTimer, Interval, NULL);
        status = KeWaitForSingleObject(&kTimer, Executive, KernelMode, false, NULL);
        Assert(NT_SUCCESS(status));
        loops++;
    }
}

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolStrCopy(PUNICODE_STRING pDst, CONST PUNICODE_STRING pSrc)
{
    USHORT cbLength = pSrc->Length + sizeof (pDst->Buffer[0]);
    pDst->Buffer = (PWCHAR)nemuDrvToolMemAlloc(cbLength);
    Assert(pDst->Buffer);
    if (pDst->Buffer)
    {
        RtlMoveMemory(pDst->Buffer, pSrc->Buffer, pSrc->Length);
        pDst->Buffer[pSrc->Length / sizeof (pDst->Buffer[0])] = L'\0';
        pDst->Length = pSrc->Length;
        pDst->MaximumLength = cbLength;
        return STATUS_SUCCESS;
    }
    return STATUS_NO_MEMORY;
}

NEMUDRVTOOL_DECL(VOID) NemuDrvToolStrFree(PUNICODE_STRING pStr)
{
    nemuDrvToolMemFree(pStr->Buffer);
}
