/* $Id: NemuUsbPwr.cpp $ */
/** @file
 * USB Power state Handling
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

#include <iprt/assert.h>

DECLHIDDEN(VOID) nemuUsbPwrStateInit(PNEMUUSBDEV_EXT pDevExt)
{
    POWER_STATE PowerState;
    PowerState.SystemState = PowerSystemWorking;
    PowerState.DeviceState = PowerDeviceD0;
    PoSetPowerState(pDevExt->pFDO, DevicePowerState, PowerState);
    pDevExt->DdiState.PwrState.PowerState = PowerState;
    pDevExt->DdiState.PwrState.PowerDownLevel = PowerDeviceUnspecified;
}

static NTSTATUS nemuUsbPwrMnDefault(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    NTSTATUS Status;
    PoStartNextPowerIrp(pIrp);
    IoSkipCurrentIrpStackLocation(pIrp);
    Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbPwrMnPowerSequence(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    Assert(0);
    return nemuUsbPwrMnDefault(pDevExt, pIrp);
}

typedef struct NEMUUSB_PWRDEV_CTX
{
    PNEMUUSBDEV_EXT pDevExt;
    PIRP pIrp;
} NEMUUSB_PWRDEV_CTX, *PNEMUUSB_PWRDEV_CTX;

static VOID nemuUsbPwrIoDeviceCompletion(IN PDEVICE_OBJECT pDeviceObject,
                    IN UCHAR MinorFunction,
                    IN POWER_STATE PowerState,
                    IN PVOID pvContext,
                    IN PIO_STATUS_BLOCK pIoStatus)
{
    PNEMUUSB_PWRDEV_CTX pDevCtx = (PNEMUUSB_PWRDEV_CTX)pvContext;
    PNEMUUSBDEV_EXT pDevExt = pDevCtx->pDevExt;
    PIRP pIrp = pDevCtx->pIrp;
    pIrp->IoStatus.Status = pIoStatus->Status;
    pIrp->IoStatus.Information = 0;

    PoStartNextPowerIrp(pIrp);
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    nemuUsbDdiStateRelease(pDevExt);

    nemuUsbMemFree(pDevCtx);
}

static NTSTATUS nemuUsbPwrIoRequestDev(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    POWER_STATE PwrState;
    PwrState.SystemState = pSl->Parameters.Power.State.SystemState;
    PwrState.DeviceState = pDevExt->DdiState.DevCaps.DeviceState[PwrState.SystemState];

    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    PNEMUUSB_PWRDEV_CTX pDevCtx = (PNEMUUSB_PWRDEV_CTX)nemuUsbMemAlloc(sizeof (*pDevCtx));
    Assert(pDevCtx);
    if (pDevCtx)
    {
        pDevCtx->pDevExt = pDevExt;
        pDevCtx->pIrp = pIrp;

        Status = PoRequestPowerIrp(pDevExt->pPDO, pSl->MinorFunction, PwrState,
                        nemuUsbPwrIoDeviceCompletion, pDevCtx, NULL);
        Assert(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
        {
            return STATUS_MORE_PROCESSING_REQUIRED;
        }

        nemuUsbMemFree(pDevCtx);
    }

    PoStartNextPowerIrp(pIrp);
    pIrp->IoStatus.Status = Status;
    pIrp->IoStatus.Information = 0;
    nemuUsbDdiStateRelease(pDevExt);

    /* the "real" Status is stored in pIrp->IoStatus.Status,
     * return success here to complete the Io */
    return STATUS_SUCCESS;
}

static NTSTATUS nemuUsbPwrIoPostSysCompletion(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp, IN PVOID pvContext)
{
    PNEMUUSBDEV_EXT pDevExt = (PNEMUUSBDEV_EXT)pvContext;
    NTSTATUS Status = pIrp->IoStatus.Status;
    Assert(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
        switch (pSl->MinorFunction)
        {
            case IRP_MN_SET_POWER:
            {
                pDevExt->DdiState.PwrState.PowerState.SystemState = pSl->Parameters.Power.State.SystemState;
                break;
            }
            default:
            {
                break;
            }
        }

        return nemuUsbPwrIoRequestDev(pDevExt, pIrp);
    }

    PoStartNextPowerIrp(pIrp);
    nemuUsbDdiStateRelease(pDevExt);
    return STATUS_SUCCESS;
}

static NTSTATUS nemuUsbPwrIoPostSys(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    IoMarkIrpPending(pIrp);
    IoCopyCurrentIrpStackLocationToNext(pIrp);
    IoSetCompletionRoutine(pIrp, nemuUsbPwrIoPostSysCompletion, pDevExt, TRUE, TRUE, TRUE);
    NTSTATUS Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status));
    return STATUS_PENDING;
}

static NTSTATUS nemuUsbPwrQueryPowerSys(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    SYSTEM_POWER_STATE enmSysPState = pSl->Parameters.Power.State.SystemState;

    return nemuUsbPwrIoPostSys(pDevExt, pIrp);
}

static NTSTATUS nemuUsbPwrIoPostDevCompletion(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp, IN PVOID pvContext)
{
    PNEMUUSBDEV_EXT pDevExt = (PNEMUUSBDEV_EXT)pvContext;

    if (pIrp->PendingReturned)
    {
        IoMarkIrpPending(pIrp);
    }

    NTSTATUS Status = pIrp->IoStatus.Status;
    Assert(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
        switch (pSl->MinorFunction)
        {
            case IRP_MN_SET_POWER:
            {
                pDevExt->DdiState.PwrState.PowerState.DeviceState = pSl->Parameters.Power.State.DeviceState;
                PoSetPowerState(pDevExt->pFDO, DevicePowerState, pSl->Parameters.Power.State);
                break;
            }
            default:
            {
                break;
            }
        }
    }

    PoStartNextPowerIrp(pIrp);
    nemuUsbDdiStateRelease(pDevExt);
    return STATUS_SUCCESS;
}

static NTSTATUS nemuUsbPwrIoPostDev(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    IoMarkIrpPending(pIrp);
    IoCopyCurrentIrpStackLocationToNext(pIrp);
    IoSetCompletionRoutine(pIrp, nemuUsbPwrIoPostDevCompletion, pDevExt, TRUE, TRUE, TRUE);
    NTSTATUS Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    Assert(NT_SUCCESS(Status));
    return STATUS_PENDING;
}

typedef struct NEMUUSB_IOASYNC_CTX
{
    PIO_WORKITEM pWrkItem;
    PIRP pIrp;
} NEMUUSB_IOASYNC_CTX, *PNEMUUSB_IOASYNC_CTX;

static VOID nemuUsbPwrIoWaitCompletionAndPostAsyncWorker(IN PDEVICE_OBJECT pDeviceObject, IN PVOID pvContext)
{
    PNEMUUSBDEV_EXT pDevExt = (PNEMUUSBDEV_EXT)pDeviceObject->DeviceExtension;
    PNEMUUSB_IOASYNC_CTX pCtx = (PNEMUUSB_IOASYNC_CTX)pvContext;
    PIRP pIrp = pCtx->pIrp;

    nemuUsbPwrIoPostDev(pDevExt, pIrp);

    IoFreeWorkItem(pCtx->pWrkItem);
    nemuUsbMemFree(pCtx);
}

static NTSTATUS nemuUsbPwrIoWaitCompletionAndPostAsync(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    PNEMUUSB_IOASYNC_CTX pCtx = (PNEMUUSB_IOASYNC_CTX)nemuUsbMemAlloc(sizeof (*pCtx));
    Assert(pCtx);
    if (pCtx)
    {
        PIO_WORKITEM pWrkItem = IoAllocateWorkItem(pDevExt->pFDO);
        Assert(pWrkItem);
        if (pWrkItem)
        {
            pCtx->pWrkItem = pWrkItem;
            pCtx->pIrp = pIrp;
            IoMarkIrpPending(pIrp);
            IoQueueWorkItem(pWrkItem, nemuUsbPwrIoWaitCompletionAndPostAsyncWorker, DelayedWorkQueue, pCtx);
            return STATUS_PENDING;
        }
        nemuUsbMemFree(pCtx);
    }
    return Status;
}

static NTSTATUS nemuUsbPwrQueryPowerDev(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    DEVICE_POWER_STATE enmDevPState = pSl->Parameters.Power.State.DeviceState;
    NTSTATUS Status = STATUS_SUCCESS;

    if (enmDevPState >= pDevExt->DdiState.PwrState.PowerState.DeviceState)
    {
        Status = nemuUsbPwrIoWaitCompletionAndPostAsync(pDevExt, pIrp);
        Assert(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
            return Status;
    }

    pIrp->IoStatus.Status = Status;
    pIrp->IoStatus.Information = 0;

    PoStartNextPowerIrp(pIrp);

    if (NT_SUCCESS(Status))
    {
        IoSkipCurrentIrpStackLocation(pIrp);
        Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    }
    else
    {
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    }

    nemuUsbDdiStateRelease(pDevExt);

    return Status;
}

static NTSTATUS nemuUsbPwrMnQueryPower(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);

    switch (pSl->Parameters.Power.Type)
    {
        case SystemPowerState:
        {
            return nemuUsbPwrQueryPowerSys(pDevExt, pIrp);
        }
        case DevicePowerState:
        {
            return nemuUsbPwrQueryPowerDev(pDevExt, pIrp);
        }
        default:
        {
            Assert(0);
            return nemuUsbPwrMnDefault(pDevExt, pIrp);
        }

    }
    return nemuUsbPwrMnDefault(pDevExt, pIrp);
}

static NTSTATUS nemuUsbPwrSetPowerSys(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    SYSTEM_POWER_STATE enmSysPState = pSl->Parameters.Power.State.SystemState;

    return nemuUsbPwrIoPostSys(pDevExt, pIrp);
}

static NTSTATUS nemuUsbPwrSetPowerDev(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    DEVICE_POWER_STATE enmDevPState = pSl->Parameters.Power.State.DeviceState;
    DEVICE_POWER_STATE enmCurDevPState = pDevExt->DdiState.PwrState.PowerState.DeviceState;
    NTSTATUS Status = STATUS_SUCCESS;

    if (enmDevPState > enmCurDevPState && enmCurDevPState == PowerDeviceD0)
    {
        Status = nemuUsbPwrIoWaitCompletionAndPostAsync(pDevExt, pIrp);
        Assert(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
            return Status;
    }

    PoStartNextPowerIrp(pIrp);

    if (NT_SUCCESS(Status))
    {
        IoCopyCurrentIrpStackLocationToNext(pIrp);
        IoSetCompletionRoutine(pIrp, nemuUsbPwrIoPostDevCompletion, pDevExt, TRUE, TRUE, TRUE);
        Status = PoCallDriver(pDevExt->pLowerDO, pIrp);
    }
    else
    {
        pIrp->IoStatus.Status = Status;
        pIrp->IoStatus.Information = 0;

        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        nemuUsbDdiStateRelease(pDevExt);
    }

    return Status;
}


static NTSTATUS nemuUsbPwrMnSetPower(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);

    switch (pSl->Parameters.Power.Type)
    {
        case SystemPowerState:
        {
            return nemuUsbPwrSetPowerSys(pDevExt, pIrp);
        }
        case DevicePowerState:
        {
            return nemuUsbPwrSetPowerDev(pDevExt, pIrp);
        }
        default:
        {
            Assert(0);
            return nemuUsbPwrMnDefault(pDevExt, pIrp);
        }

    }
    return nemuUsbPwrMnDefault(pDevExt, pIrp);
}

static NTSTATUS nemuUsbPwrMnWaitWake(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    AssertFailed();
    return nemuUsbPwrMnDefault(pDevExt, pIrp);
}


static NTSTATUS nemuUsbPwrDispatch(IN PNEMUUSBDEV_EXT pDevExt, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);

    switch (pSl->MinorFunction)
    {
        case IRP_MN_POWER_SEQUENCE:
        {
            return nemuUsbPwrMnPowerSequence(pDevExt, pIrp);
        }
        case IRP_MN_QUERY_POWER:
        {
            return nemuUsbPwrMnQueryPower(pDevExt, pIrp);
        }
        case IRP_MN_SET_POWER:
        {
            return nemuUsbPwrMnSetPower(pDevExt, pIrp);
        }
        case IRP_MN_WAIT_WAKE:
        {
            return nemuUsbPwrMnWaitWake(pDevExt, pIrp);
        }
        default:
        {
//            Assert(0);
            return nemuUsbPwrMnDefault(pDevExt, pIrp);
        }
    }
}

DECLHIDDEN(NTSTATUS) nemuUsbDispatchPower(IN PDEVICE_OBJECT pDeviceObject, IN PIRP pIrp)
{
    PNEMUUSBDEV_EXT pDevExt = (PNEMUUSBDEV_EXT)pDeviceObject->DeviceExtension;
    ENMNEMUUSB_PNPSTATE enmState = nemuUsbDdiStateRetainIfNotRemoved(pDevExt);
    switch (enmState)
    {
        case ENMNEMUUSB_PNPSTATE_REMOVED:
        {
            PoStartNextPowerIrp(pIrp);

            pIrp->IoStatus.Status = STATUS_DELETE_PENDING;
            pIrp->IoStatus.Information = 0;

            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            nemuUsbDdiStateRelease(pDevExt);

            return STATUS_DELETE_PENDING;
        }
        case ENMNEMUUSB_PNPSTATE_START_PENDING:
        {
            PoStartNextPowerIrp(pIrp);
            IoSkipCurrentIrpStackLocation(pIrp);

            nemuUsbDdiStateRelease(pDevExt);

            return PoCallDriver(pDevExt->pLowerDO, pIrp);
        }
        default:
        {
            return nemuUsbPwrDispatch(pDevExt, pIrp);
        }
    }
}
