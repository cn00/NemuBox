/* $Id: NemuUsbTool.cpp $ */
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
#define INITGUID
#include "NemuUsbTool.h"
#include <usbbusif.h>

#include <iprt/assert.h>
#include <Nemu/log.h>

#include "../../../win/NemuDbgLog.h"

#define NEMUUSBTOOL_MEMTAG 'TUBV'

static PVOID nemuUsbToolMemAlloc(SIZE_T cbBytes)
{
    PVOID pvMem = ExAllocatePoolWithTag(NonPagedPool, cbBytes, NEMUUSBTOOL_MEMTAG);
    Assert(pvMem);
    return pvMem;
}

static PVOID nemuUsbToolMemAllocZ(SIZE_T cbBytes)
{
    PVOID pvMem = nemuUsbToolMemAlloc(cbBytes);
    if (pvMem)
    {
        RtlZeroMemory(pvMem, cbBytes);
    }
    return pvMem;
}

static VOID nemuUsbToolMemFree(PVOID pvMem)
{
    ExFreePoolWithTag(pvMem, NEMUUSBTOOL_MEMTAG);
}

NEMUUSBTOOL_DECL(PURB) NemuUsbToolUrbAlloc(USHORT u16Function, USHORT cbSize)
{
    PURB pUrb = (PURB)nemuUsbToolMemAlloc(cbSize);
    Assert(pUrb);
    if (!pUrb)
        return NULL;

    pUrb->UrbHeader.Length = cbSize;
    pUrb->UrbHeader.Function = u16Function;
    return pUrb;
}

NEMUUSBTOOL_DECL(PURB) NemuUsbToolUrbAllocZ(USHORT u16Function, USHORT cbSize)
{
    PURB pUrb = (PURB)nemuUsbToolMemAllocZ(cbSize);
    Assert(pUrb);
    if (!pUrb)
        return NULL;

    pUrb->UrbHeader.Length = cbSize;
    pUrb->UrbHeader.Function = u16Function;
    return pUrb;
}

NEMUUSBTOOL_DECL(PURB) NemuUsbToolUrbReinit(PURB pUrb, USHORT cbSize, USHORT u16Function)
{
    Assert(pUrb->UrbHeader.Length == cbSize);
    if (pUrb->UrbHeader.Length < cbSize)
        return NULL;
    pUrb->UrbHeader.Length = cbSize;
    pUrb->UrbHeader.Function = u16Function;
    return pUrb;
}

NEMUUSBTOOL_DECL(VOID) NemuUsbToolUrbFree(PURB pUrb)
{
    nemuUsbToolMemFree(pUrb);
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolUrbPost(PDEVICE_OBJECT pDevObj, PURB pUrb, ULONG dwTimeoutMs)
{
    if (dwTimeoutMs == RT_INDEFINITE_WAIT)
        return NemuUsbToolIoInternalCtlSendSync(pDevObj, IOCTL_INTERNAL_USB_SUBMIT_URB, pUrb, NULL);
    return NemuUsbToolIoInternalCtlSendSyncWithTimeout(pDevObj, IOCTL_INTERNAL_USB_SUBMIT_URB, pUrb, NULL, dwTimeoutMs);
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolGetDescriptor(PDEVICE_OBJECT pDevObj, void *pvBuffer, int cbBuffer, int Type, int iIndex, int LangId, ULONG dwTimeoutMs)
{
    NTSTATUS Status;
    USHORT cbUrb = sizeof (struct _URB_CONTROL_DESCRIPTOR_REQUEST);
    PURB pUrb = NemuUsbToolUrbAllocZ(URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE, cbUrb);
    if(!pUrb)
    {
        WARN(("allocating URB failed"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PUSB_COMMON_DESCRIPTOR pCmn = (PUSB_COMMON_DESCRIPTOR)pvBuffer;
    pCmn->bLength = cbBuffer;
    pCmn->bDescriptorType = Type;

    pUrb->UrbHeader.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);
    pUrb->UrbControlDescriptorRequest.TransferBufferLength = cbBuffer;
    pUrb->UrbControlDescriptorRequest.TransferBuffer       = pvBuffer;
    pUrb->UrbControlDescriptorRequest.Index                = (UCHAR)iIndex;
    pUrb->UrbControlDescriptorRequest.DescriptorType       = (UCHAR)Type;
    pUrb->UrbControlDescriptorRequest.LanguageId           = (USHORT)LangId;

    Status = NemuUsbToolUrbPost(pDevObj, pUrb, dwTimeoutMs);
    ASSERT_WARN(Status == STATUS_SUCCESS, ("NemuUsbToolUrbPost failed Status (0x%x)", Status));

    NemuUsbToolUrbFree(pUrb);

    return Status;
}

NEMUUSBTOOL_DECL(VOID) NemuUsbToolStringDescriptorToUnicodeString(PUSB_STRING_DESCRIPTOR pDr, PUNICODE_STRING pUnicode)
{
    /* for some reason the string dr sometimes contains a non-null terminated string
     * although we zeroed up the complete descriptor buffer
     * this is why RtlInitUnicodeString won't work
     * we need to init the scting length based on dr length */
    pUnicode->Buffer = pDr->bString;
    pUnicode->Length = pUnicode->MaximumLength = pDr->bLength - RT_OFFSETOF(USB_STRING_DESCRIPTOR, bString);
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolGetStringDescriptorA(PDEVICE_OBJECT pDevObj, char *pResult, ULONG cbResult, int iIndex, int LangId, ULONG dwTimeoutMs)
{
    char aBuf[MAXIMUM_USB_STRING_LENGTH];
    AssertCompile(sizeof (aBuf) <= UINT8_MAX);
    UCHAR cbBuf = (UCHAR)sizeof (aBuf);
    PUSB_STRING_DESCRIPTOR pDr = (PUSB_STRING_DESCRIPTOR)&aBuf;

    Assert(pResult);
    *pResult = 0;

    memset(pDr, 0, cbBuf);
    pDr->bLength = cbBuf;
    pDr->bDescriptorType = USB_STRING_DESCRIPTOR_TYPE;

    NTSTATUS Status = NemuUsbToolGetDescriptor(pDevObj, pDr, cbBuf, USB_STRING_DESCRIPTOR_TYPE, iIndex, LangId, dwTimeoutMs);
    if (NT_SUCCESS(Status))
    {
        if (pDr->bLength >= sizeof (USB_STRING_DESCRIPTOR))
        {
            UNICODE_STRING Unicode;
            ANSI_STRING Ansi;
            /* for some reason the string dr sometimes contains a non-null terminated string
             * although we zeroed up the complete descriptor buffer
             * this is why RtlInitUnicodeString won't work*/
            NemuUsbToolStringDescriptorToUnicodeString(pDr, &Unicode);
            Ansi.Buffer = pResult;
            Ansi.Length = 0;
            Ansi.MaximumLength = (USHORT)cbResult - 1;
            memset(pResult, 0, cbResult);
            Status = RtlUnicodeStringToAnsiString(&Ansi, &Unicode, FALSE);
            Assert(Status == STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                /* just to make sure the string is null-terminated */
                Assert(pResult[cbResult-1] == 0);
                Status = STATUS_SUCCESS;
            }
        }
        else
        {
            Status = STATUS_INVALID_PARAMETER;
        }
    }
    return Status;
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolGetLangID(PDEVICE_OBJECT pDevObj, int *pLangId, ULONG dwTimeoutMs)
{
    char aBuf[MAXIMUM_USB_STRING_LENGTH];
    AssertCompile(sizeof (aBuf) <= UINT8_MAX);
    UCHAR cbBuf = (UCHAR)sizeof (aBuf);
    PUSB_STRING_DESCRIPTOR pDr = (PUSB_STRING_DESCRIPTOR)&aBuf;

    Assert(pLangId);
    *pLangId = 0;

    memset(pDr, 0, cbBuf);
    pDr->bLength = cbBuf;
    pDr->bDescriptorType = USB_STRING_DESCRIPTOR_TYPE;

    NTSTATUS Status = NemuUsbToolGetDescriptor(pDevObj, pDr, cbBuf, USB_STRING_DESCRIPTOR_TYPE, 0, 0, dwTimeoutMs);
    if (NT_SUCCESS(Status))
    {
        /* Just grab the first lang ID if available. In 99% cases, it will be US English (0x0409).*/
        if (pDr->bLength >= sizeof (USB_STRING_DESCRIPTOR))
        {
            AssertCompile(sizeof (pDr->bString[0]) == sizeof (uint16_t));
            *pLangId = pDr->bString[0];
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_INVALID_PARAMETER;
        }
    }
    return Status;
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolGetDeviceSpeed(PDEVICE_OBJECT pDevObj, BOOLEAN *pbIsHigh)
{
    Assert(pbIsHigh);
    *pbIsHigh = FALSE;

    PIRP pIrp = IoAllocateIrp(pDevObj->StackSize, FALSE);
    Assert(pIrp);
    if (!pIrp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    USB_BUS_INTERFACE_USBDI_V1 BusIf;
    PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->MajorFunction = IRP_MJ_PNP;
    pSl->MinorFunction = IRP_MN_QUERY_INTERFACE;
    pSl->Parameters.QueryInterface.InterfaceType = &USB_BUS_INTERFACE_USBDI_GUID;
    pSl->Parameters.QueryInterface.Size = sizeof (BusIf);
    pSl->Parameters.QueryInterface.Version = USB_BUSIF_USBDI_VERSION_1;
    pSl->Parameters.QueryInterface.Interface = (PINTERFACE)&BusIf;
    pSl->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    NTSTATUS Status = NemuDrvToolIoPostSync(pDevObj, pIrp);
    Assert(NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(Status))
    {
        *pbIsHigh = BusIf.IsDeviceHighSpeed(BusIf.BusContext);
        BusIf.InterfaceDereference(BusIf.BusContext);
    }
    IoFreeIrp(pIrp);

    return Status;
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolPipeClear(PDEVICE_OBJECT pDevObj, HANDLE hPipe, bool fReset)
{
    if (!hPipe)
    {
        Log(("Resetting the control pipe??\n"));
        return STATUS_SUCCESS;
    }
    USHORT u16Function = fReset ? URB_FUNCTION_RESET_PIPE : URB_FUNCTION_ABORT_PIPE;
    PURB pUrb = NemuUsbToolUrbAlloc(u16Function, sizeof (struct _URB_PIPE_REQUEST));
    if (!pUrb)
    {
        AssertMsgFailed((__FUNCTION__": NemuUsbToolUrbAlloc failed!\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    pUrb->UrbPipeRequest.PipeHandle = hPipe;
    pUrb->UrbPipeRequest.Reserved = 0;

    NTSTATUS Status = NemuUsbToolUrbPost(pDevObj, pUrb, RT_INDEFINITE_WAIT);
    if (!NT_SUCCESS(Status) || !USBD_SUCCESS(pUrb->UrbHeader.Status))
    {
        AssertMsgFailed((__FUNCTION__": nemuUsbToolRequest failed with %x (%x)\n", Status, pUrb->UrbHeader.Status));
    }

    NemuUsbToolUrbFree(pUrb);

    return Status;
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolCurrentFrame(PDEVICE_OBJECT pDevObj, PIRP pIrp, PULONG piFrame)
{
    struct _URB_GET_CURRENT_FRAME_NUMBER Urb;
    Urb.Hdr.Function = URB_FUNCTION_GET_CURRENT_FRAME_NUMBER;
    Urb.Hdr.Length = sizeof(Urb);
    Urb.FrameNumber = (ULONG)-1;

    Assert(piFrame);
    *piFrame = (ULONG)-1;

    PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    pSl->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    pSl->Parameters.Others.Argument1 = (PVOID)&Urb;
    pSl->Parameters.Others.Argument2 = NULL;

    NTSTATUS Status = NemuUsbToolUrbPost(pDevObj, (PURB)&Urb, RT_INDEFINITE_WAIT);
    Assert(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        *piFrame = Urb.FrameNumber;
    }

    return Status;
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolDevUnconfigure(PDEVICE_OBJECT pDevObj)
{
    USHORT cbUrb = sizeof (struct _URB_SELECT_CONFIGURATION);
    PURB pUrb = NemuUsbToolUrbAlloc(URB_FUNCTION_SELECT_CONFIGURATION, cbUrb);
    Assert(pUrb);
    if (!pUrb)
        return STATUS_INSUFFICIENT_RESOURCES;

    UsbBuildSelectConfigurationRequest(pUrb, (USHORT)cbUrb, NULL);

    NTSTATUS Status = NemuUsbToolUrbPost(pDevObj, pUrb, RT_INDEFINITE_WAIT);
    Assert(NT_SUCCESS(Status));

    NemuUsbToolUrbFree(pUrb);

    return Status;
}

NEMUUSBTOOL_DECL(PIRP) NemuUsbToolIoBuildAsyncInternalCtl(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2)
{
    PIRP pIrp = IoAllocateIrp(pDevObj->StackSize, FALSE);
    Assert(pIrp);
    if (!pIrp)
    {
        return NULL;
    }

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = NULL;

    PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    pSl->MinorFunction = 0;
    pSl->Parameters.DeviceIoControl.IoControlCode = uCtl;
    pSl->Parameters.Others.Argument1 = pvArg1;
    pSl->Parameters.Others.Argument2 = pvArg2;
    return pIrp;
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolIoInternalCtlSendSyncWithTimeout(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2, ULONG dwTimeoutMs)
{
    /* since we're going to cancel the irp on timeout, we should allocate our own IRP rather than using the threaded one
     * */
    PIRP pIrp = NemuUsbToolIoBuildAsyncInternalCtl(pDevObj, uCtl, pvArg1, pvArg2);
    if (!pIrp)
    {
        WARN(("NemuUsbToolIoBuildAsyncInternalCtl failed"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NTSTATUS Status = NemuDrvToolIoPostSyncWithTimeout(pDevObj, pIrp, dwTimeoutMs);

    IoFreeIrp(pIrp);

    return Status;
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolIoInternalCtlSendAsync(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2,
        PKEVENT pEvent, PIO_STATUS_BLOCK pIoStatus)
{
    NTSTATUS Status;
    PIRP pIrp;
    PIO_STACK_LOCATION pSl;
    KIRQL Irql = KeGetCurrentIrql();
    Assert(Irql == PASSIVE_LEVEL);

    pIrp = IoBuildDeviceIoControlRequest(uCtl, pDevObj, NULL, 0, NULL, 0, TRUE, pEvent, pIoStatus);
    if (!pIrp)
    {
        WARN(("IoBuildDeviceIoControlRequest failed!!\n"));
        pIoStatus->Status = STATUS_INSUFFICIENT_RESOURCES;
        pIoStatus->Information = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Get the next stack location as that is used for the new irp */
    pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->Parameters.Others.Argument1 = pvArg1;
    pSl->Parameters.Others.Argument2 = pvArg2;

    Status = IoCallDriver(pDevObj, pIrp);

    return Status;
}

NEMUUSBTOOL_DECL(NTSTATUS) NemuUsbToolIoInternalCtlSendSync(PDEVICE_OBJECT pDevObj, ULONG uCtl, void *pvArg1, void *pvArg2)
{
    IO_STATUS_BLOCK IoStatus = {0};
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    LOG(("Sending sync Ctl pDevObj(0x%p), uCtl(0x%x), pvArg1(0x%p), pvArg2(0x%p)", pDevObj, uCtl, pvArg1, pvArg2));

    Status = NemuUsbToolIoInternalCtlSendAsync(pDevObj, uCtl, pvArg1, pvArg2, &Event, &IoStatus);

    if (Status == STATUS_PENDING)
    {
        LOG(("NemuUsbToolIoInternalCtlSendAsync returned pending for pDevObj(0x%x)", pDevObj));
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
        LOG(("Pending NemuUsbToolIoInternalCtlSendAsync completed with Status (0x%x) for pDevObj(0x%x)", Status, pDevObj));
    }
    else
    {
        LOG(("NemuUsbToolIoInternalCtlSendAsync completed with Status (0x%x) for pDevObj(0x%x)", Status, pDevObj));
    }

    return Status;
}
