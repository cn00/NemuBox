/* $Id: NemuMFDriver.cpp $ */
/** @file
 * Nemu Mouse Filter Driver - Interface functions.
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

#include "NemuMF.h"
#include <Nemu/NemuGuestLib.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>

#ifdef ALLOC_PRAGMA
# pragma alloc_text(INIT, DriverEntry)
#endif

/* Driver entry point */
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
    NOREF(RegistryPath);
    PAGED_CODE();

    int irc = RTR0Init(0);
    if (RT_FAILURE(irc))
    {
        LOGREL(("failed to init IPRT (rc=%#x)", irc));
        return STATUS_INTERNAL_ERROR;
    }
    LOGF_ENTER();

    DriverObject->DriverUnload = NemuDrvUnload;
    DriverObject->DriverExtension->AddDevice = NemuDrvAddDevice;

    for (int i=0; i<=IRP_MJ_MAXIMUM_FUNCTION; ++i)
    {
        DriverObject->MajorFunction[i] = NemuIrpPassthrough;
    }

    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = NemuIrpInternalIOCTL;
    DriverObject->MajorFunction[IRP_MJ_PNP] = NemuIrpPnP;
    DriverObject->MajorFunction[IRP_MJ_POWER] = NemuIrpPower;

    NTSTATUS tmpStatus = NemuNewProtInit();
    if (!NT_SUCCESS(tmpStatus))
    {
        WARN(("NemuNewProtInit failed Status (0x%x)", tmpStatus));
    }

    LOGF_LEAVE();
    return STATUS_SUCCESS;
}

VOID NemuDrvUnload(IN PDRIVER_OBJECT Driver)
{
    NOREF(Driver);
    PAGED_CODE();
    LOGF_ENTER();

    NTSTATUS tmpStatus = NemuNewProtTerm();
    if (!NT_SUCCESS(tmpStatus))
    {
        WARN(("NemuNewProtTerm failed Status (0x%x)", tmpStatus));
    }


    RTR0Term();
}

#define NEMUUSB_RLTAG 'LRBV'

NTSTATUS NemuDrvAddDevice(IN PDRIVER_OBJECT Driver, IN PDEVICE_OBJECT PDO)
{
    NTSTATUS rc;
    PDEVICE_OBJECT pDO, pDOParent;
    PNEMUMOUSE_DEVEXT pDevExt;

    PAGED_CODE();
    LOGF_ENTER();

    rc = IoCreateDevice(Driver, sizeof(NEMUMOUSE_DEVEXT), NULL, FILE_DEVICE_MOUSE, 0, FALSE, &pDO);
    if (!NT_SUCCESS(rc))
    {
        WARN(("IoCreateDevice failed with %#x", rc));
        return rc;
    }

    pDevExt = (PNEMUMOUSE_DEVEXT) pDO->DeviceExtension;
    RtlZeroMemory(pDevExt, sizeof(NEMUMOUSE_DEVEXT));

    IoInitializeRemoveLock(&pDevExt->RemoveLock, NEMUUSB_RLTAG, 1, 100);

    rc = IoAcquireRemoveLock(&pDevExt->RemoveLock, pDevExt);
    if (!NT_SUCCESS(rc))
    {
        WARN(("IoAcquireRemoveLock failed with %#x", rc));
        IoDeleteDevice(pDO);
        return rc;
    }

    pDOParent = IoAttachDeviceToDeviceStack(pDO, PDO);
    if (!pDOParent)
    {
        IoReleaseRemoveLockAndWait(&pDevExt->RemoveLock, pDevExt);

        WARN(("IoAttachDeviceToDeviceStack failed"));
        IoDeleteDevice(pDO);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    pDevExt->pdoMain   = PDO;
    pDevExt->pdoSelf   = pDO;
    pDevExt->pdoParent = pDOParent;

    NemuDeviceAdded(pDevExt);

    pDO->Flags |= (DO_BUFFERED_IO | DO_POWER_PAGABLE);
    pDO->Flags &= ~DO_DEVICE_INITIALIZING;

    LOGF_LEAVE();
    return rc;
}

NTSTATUS NemuIrpPassthrough(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PNEMUMOUSE_DEVEXT pDevExt;
    LOGF_ENTER();

    pDevExt = (PNEMUMOUSE_DEVEXT) DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);

    LOGF_LEAVE();
    return IoCallDriver(pDevExt->pdoParent, Irp);
}

static void
NemuServiceCB(PDEVICE_OBJECT DeviceObject, PMOUSE_INPUT_DATA InputDataStart,
              PMOUSE_INPUT_DATA InputDataEnd, PULONG InputDataConsumed)
{
    PNEMUMOUSE_DEVEXT pDevExt;
    LOGF_ENTER();

    pDevExt = (PNEMUMOUSE_DEVEXT) DeviceObject->DeviceExtension;

    NemuDrvNotifyServiceCB(pDevExt, InputDataStart, InputDataEnd, InputDataConsumed);

    LOGF_LEAVE();
}

NTSTATUS NemuIrpInternalIOCTL(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PIO_STACK_LOCATION pStack;
    PNEMUMOUSE_DEVEXT pDevExt;
    LOGF_ENTER();

    pStack = IoGetCurrentIrpStackLocation(Irp);
    pDevExt = (PNEMUMOUSE_DEVEXT) DeviceObject->DeviceExtension;

    LOGF(("IOCTL %08X, fn = %#04X", pStack->Parameters.DeviceIoControl.IoControlCode,
          (pStack->Parameters.DeviceIoControl.IoControlCode>>2)&0xFFF));

    /* Hook into connection between mouse class device and port drivers */
    if (pStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_MOUSE_CONNECT)
    {
        Irp->IoStatus.Information = 0;

        if (pDevExt->OriginalConnectData.pfnServiceCB)
        {
            WARN(("STATUS_SHARING_VIOLATION"));
            Irp->IoStatus.Status = STATUS_SHARING_VIOLATION;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Irp->IoStatus.Status;
        }

        if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(INTERNAL_MOUSE_CONNECT_DATA))
        {
            WARN(("STATUS_INVALID_PARAMETER"));
            Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Irp->IoStatus.Status;
        }

        PINTERNAL_MOUSE_CONNECT_DATA pData = (PINTERNAL_MOUSE_CONNECT_DATA) pStack->Parameters.DeviceIoControl.Type3InputBuffer;

        pDevExt->OriginalConnectData = *pData;
        pData->pDO = pDevExt->pdoSelf;
        pData->pfnServiceCB = NemuServiceCB;
    }

    NemuInformHost(pDevExt);

    LOGF_LEAVE();
    return NemuIrpPassthrough(DeviceObject, Irp);
}

NTSTATUS NemuIrpPnP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PIO_STACK_LOCATION pStack;
    PNEMUMOUSE_DEVEXT pDevExt;
    NTSTATUS rc;
    LOGF_ENTER();

    pStack = IoGetCurrentIrpStackLocation(Irp);
    pDevExt = (PNEMUMOUSE_DEVEXT) DeviceObject->DeviceExtension;

    switch (pStack->MinorFunction)
    {
        case IRP_MN_REMOVE_DEVICE:
        {
            LOGF(("IRP_MN_REMOVE_DEVICE"));

            IoReleaseRemoveLockAndWait(&pDevExt->RemoveLock, pDevExt);

            NemuDeviceRemoved(pDevExt);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            rc = NemuIrpPassthrough(DeviceObject, Irp);

            IoDetachDevice(pDevExt->pdoParent);
            IoDeleteDevice(DeviceObject);
            break;
        }
        default:
        {
            rc = NemuIrpPassthrough(DeviceObject, Irp);
            break;
        }
    }

    if (!NT_SUCCESS(rc) && rc != STATUS_NOT_SUPPORTED)
    {
        WARN(("rc=%#x", rc));
    }

    LOGF_LEAVE();
    return rc;
}

NTSTATUS NemuIrpPower(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PNEMUMOUSE_DEVEXT pDevExt;
    PAGED_CODE();
    LOGF_ENTER();
    pDevExt = (PNEMUMOUSE_DEVEXT) DeviceObject->DeviceExtension;
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    LOGF_LEAVE();
    return PoCallDriver(pDevExt->pdoParent, Irp);
}

