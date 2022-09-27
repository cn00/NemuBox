/* $Id: NemuUsbPnP.cpp $ */
/** @file
 * USB PnP Handling
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
#include "NemuUsbCmn.h"

static NTSTATUS nemuUsbPnPMnStartDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    IoCopyCurrentIrpStackLocationToNext(pIrp);
    NTSTATUS Status = NemuDrvToolIoPostSync(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(Status))
    {
        Status = nemuUsbRtStart(pDevExt);
        Assert(Status == STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            nemuUsbPnPStateSet(pDevExt, ENMNEMUUSB_PNPSTATE_STARTED);
        }
    }

    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbPnPMnQueryStopDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    nemuUsbPnPStateSet(pDevExt, ENMNEMUUSB_PNPSTATE_STOP_PENDING);

    nemuUsbDdiStateReleaseAndWaitCompleted(pDevExt);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    return IoCallDriver(pDevExt->pLowerDO, pIrp);
}

static NTSTATUS nemuUsbPnPMnStopDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    nemuUsbPnPStateSet(pDevExt, ENMNEMUUSB_PNPSTATE_STOPPED);

    nemuUsbRtClear(pDevExt);

    NTSTATUS Status = NemuUsbToolDevUnconfigure(pDevExt->pLowerDO);
    Assert(NT_SUCCESS(Status));

    pIrp->IoStatus.Status = Status;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = IoCallDriver(pDevExt->pLowerDO, pIrp);

    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbPnPMnCancelStopDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    ENMNEMUUSB_PNPSTATE enmState = nemuUsbPnPStateGet(pDevExt);
    NTSTATUS Status = STATUS_SUCCESS;

    IoCopyCurrentIrpStackLocationToNext(pIrp);
    Status = NemuDrvToolIoPostSync(pDevExt->pLowerDO, pIrp);
    if (NT_SUCCESS(Status) && enmState == ENMNEMUUSB_PNPSTATE_STOP_PENDING)
    {
        nemuUsbPnPStateRestore(pDevExt);
    }

    Status = STATUS_SUCCESS;
    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS nemuUsbPnPMnQueryRemoveDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    nemuUsbPnPStateSet(pDevExt, ENMNEMUUSB_PNPSTATE_REMOVE_PENDING);

    nemuUsbDdiStateReleaseAndWaitCompleted(pDevExt);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    return IoCallDriver(pDevExt->pLowerDO, pIrp);
}

static NTSTATUS nemuUsbPnPRmDev(PNEMUUSBDEV_EXT pDevExt)
{
    NTSTATUS Status = nemuUsbRtRm(pDevExt);
    Assert(Status == STATUS_SUCCESS);

    return Status;
}

static NTSTATUS nemuUsbPnPMnRemoveDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    ENMNEMUUSB_PNPSTATE enmState = nemuUsbPnPStateGet(pDevExt);
    NTSTATUS Status = STATUS_SUCCESS;
    if (enmState != ENMNEMUUSB_PNPSTATE_SURPRISE_REMOVED)
    {
        Status = nemuUsbPnPRmDev(pDevExt);
        Assert(Status == STATUS_SUCCESS);
    }

    nemuUsbPnPStateSet(pDevExt, ENMNEMUUSB_PNPSTATE_REMOVED);

    nemuUsbDdiStateRelease(pDevExt);

    nemuUsbDdiStateReleaseAndWaitRemoved(pDevExt);

    nemuUsbRtClear(pDevExt);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = IoCallDriver(pDevExt->pLowerDO, pIrp);

    IoDetachDevice(pDevExt->pLowerDO);
    IoDeleteDevice(pDevExt->pFDO);

    return Status;
}

static NTSTATUS nemuUsbPnPMnCancelRemoveDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    ENMNEMUUSB_PNPSTATE enmState = nemuUsbPnPStateGet(pDevExt);
    NTSTATUS Status = STATUS_SUCCESS;
    IoCopyCurrentIrpStackLocationToNext(pIrp);

    Status = NemuDrvToolIoPostSync(pDevExt->pLowerDO, pIrp);

    if (NT_SUCCESS(Status) &&
        enmState == ENMNEMUUSB_PNPSTATE_REMOVE_PENDING)
    {
        nemuUsbPnPStateRestore(pDevExt);
    }

    Status = STATUS_SUCCESS;
    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS nemuUsbPnPMnSurpriseRemoval(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    nemuUsbPnPStateSet(pDevExt, ENMNEMUUSB_PNPSTATE_SURPRISE_REMOVED);

    NTSTATUS Status = nemuUsbPnPRmDev(pDevExt);
    Assert(Status == STATUS_SUCCESS);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = IoCallDriver(pDevExt->pLowerDO, pIrp);

    nemuUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS nemuUsbPnPMnQueryCapabilities(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PDEVICE_CAPABILITIES pDevCaps = pSl->Parameters.DeviceCapabilities.Capabilities;

    if (pDevCaps->Version < 1 || pDevCaps->Size < sizeof (*pDevCaps))
    {
        Assert(0);
        /* todo: return more appropriate status ?? */
        return STATUS_UNSUCCESSFUL;
    }

    pDevCaps->SurpriseRemovalOK = TRUE;
    pIrp->IoStatus.Status = STATUS_SUCCESS;

    IoCopyCurrentIrpStackLocationToNext(pIrp);
    NTSTATUS Status = NemuDrvToolIoPostSync(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        pDevCaps->SurpriseRemovalOK = 1;
        pDevExt->DdiState.DevCaps = *pDevCaps;
    }

    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS nemuUsbPnPMnDefault(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    NTSTATUS Status;
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = IoCallDriver(pDevExt->pLowerDO, pIrp);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

DECLHIDDEN(NTSTATUS) nemuUsbDispatchPnP(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
    PNEMUUSBDEV_EXT pDevExt = (PNEMUUSBDEV_EXT)pDeviceObject->DeviceExtension;
    ENMNEMUUSB_PNPSTATE enmState = nemuUsbPnPStateGet(pDevExt);
    if (!nemuUsbDdiStateRetainIfNotRemoved(pDevExt))
    {
        return NemuDrvToolIoComplete(pIrp, STATUS_DELETE_PENDING, 0);
    }

    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);

    switch (pSl->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            return nemuUsbPnPMnStartDevice(pDevExt, pIrp);
        }
        case IRP_MN_QUERY_STOP_DEVICE:
        {
            return nemuUsbPnPMnQueryStopDevice(pDevExt, pIrp);
        }
        case IRP_MN_STOP_DEVICE:
        {
            return nemuUsbPnPMnStopDevice(pDevExt, pIrp);
        }
        case IRP_MN_CANCEL_STOP_DEVICE:
        {
            return nemuUsbPnPMnCancelStopDevice(pDevExt, pIrp);
        }
        case IRP_MN_QUERY_REMOVE_DEVICE:
        {
            return nemuUsbPnPMnQueryRemoveDevice(pDevExt, pIrp);
        }
        case IRP_MN_REMOVE_DEVICE:
        {
            return nemuUsbPnPMnRemoveDevice(pDevExt, pIrp);
        }
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        {
            return nemuUsbPnPMnCancelRemoveDevice(pDevExt, pIrp);
        }
        case IRP_MN_SURPRISE_REMOVAL:
        {
            return nemuUsbPnPMnSurpriseRemoval(pDevExt, pIrp);
        }
        case IRP_MN_QUERY_CAPABILITIES:
        {
            return nemuUsbPnPMnQueryCapabilities(pDevExt, pIrp);
        }
        default:
        {
            return nemuUsbPnPMnDefault(pDevExt, pIrp);
        }
    }
}
