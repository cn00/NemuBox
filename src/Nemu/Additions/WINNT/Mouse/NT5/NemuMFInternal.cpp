/* $Id: NemuMFInternal.cpp $ */

/** @file
 * Nemu Mouse filter internal functions
 */

/*
 * Copyright (C) 2011-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define WIN9X_COMPAT_SPINLOCK /* Avoid duplicate _KeInitializeSpinLock@4 error on x86. */
#include <iprt/asm.h>
#include "NemuMF.h"
#include <Nemu/NemuGuestLib.h>
#include <Nemu/NemuGuest.h>
#include <iprt/assert.h>

typedef struct NEMUGDC
{
    PDEVICE_OBJECT pDo;
    PFILE_OBJECT pFo;
} NEMUGDC, *PNEMUGDC;

typedef struct _NemuGlobalContext
{
    volatile LONG cDevicesStarted;
    volatile LONG fVBGLInited;
    volatile LONG fVBGLInitFailed;
    volatile LONG fHostInformed;
    volatile LONG fHostMouseFound;
    NEMUGDC Gdc;
    KSPIN_LOCK SyncLock;
    volatile PNEMUMOUSE_DEVEXT pCurrentDevExt;
    LIST_ENTRY DevExtList;
    BOOLEAN fIsNewProtEnabled;
    MOUSE_INPUT_DATA LastReportedData;
} NemuGlobalContext;

static NemuGlobalContext g_ctx = {};

/* Guest Device Communication API */
NTSTATUS NemuGdcInit()
{
    UNICODE_STRING UniName;
    RtlInitUnicodeString(&UniName, NEMUGUEST_DEVICE_NAME_NT);
    NTSTATUS Status = IoGetDeviceObjectPointer(&UniName, FILE_ALL_ACCESS, &g_ctx.Gdc.pFo, &g_ctx.Gdc.pDo);
    if (!NT_SUCCESS(Status))
    {
        WARN(("IoGetDeviceObjectPointer failed Status(0x%x)", Status));
        memset(&g_ctx.Gdc, 0, sizeof (g_ctx.Gdc));
    }
    return Status;
}

BOOLEAN NemuGdcIsInitialized()
{
    return !!g_ctx.Gdc.pDo;
}

NTSTATUS NemuGdcTerm()
{
    if (!g_ctx.Gdc.pFo)
        return STATUS_SUCCESS;
    /* this will dereference device object as well */
    ObDereferenceObject(g_ctx.Gdc.pFo);
    return STATUS_SUCCESS;
}

static NTSTATUS nemuGdcSubmitAsync(ULONG uCtl, PVOID pvBuffer, SIZE_T cbBuffer, PKEVENT pEvent, PIO_STATUS_BLOCK pIoStatus)
{
    NTSTATUS Status;
    PIRP pIrp;
    KIRQL Irql = KeGetCurrentIrql();
    Assert(Irql == PASSIVE_LEVEL);

    pIrp = IoBuildDeviceIoControlRequest(uCtl, g_ctx.Gdc.pDo, NULL, 0, NULL, 0, TRUE, pEvent, pIoStatus);
    if (!pIrp)
    {
        WARN(("IoBuildDeviceIoControlRequest failed!!\n"));
        pIoStatus->Status = STATUS_INSUFFICIENT_RESOURCES;
        pIoStatus->Information = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
    pSl->Parameters.Others.Argument1 = (PVOID)pvBuffer;
    pSl->Parameters.Others.Argument2 = (PVOID)cbBuffer;
    Status = IoCallDriver(g_ctx.Gdc.pDo, pIrp);

    return Status;
}

static NTSTATUS nemuGdcSubmit(ULONG uCtl, PVOID pvBuffer, SIZE_T cbBuffer)
{
    IO_STATUS_BLOCK IoStatus = {0};
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Status = nemuGdcSubmitAsync(uCtl, pvBuffer, cbBuffer, &Event, &IoStatus);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static DECLCALLBACK(void) nemuNewProtMouseEventCb(void *pvContext)
{
    PNEMUMOUSE_DEVEXT pDevExt = (PNEMUMOUSE_DEVEXT)ASMAtomicUoReadPtr((void * volatile *)&g_ctx.pCurrentDevExt);
    if (pDevExt)
    {
#define NEMUMOUSE_POLLERTAG 'PMBV'
        NTSTATUS Status = IoAcquireRemoveLock(&pDevExt->RemoveLock, pDevExt);
        if (NT_SUCCESS(Status))
        {
            ULONG InputDataConsumed = 0;
            NemuDrvNotifyServiceCB(pDevExt, &g_ctx.LastReportedData, &g_ctx.LastReportedData + 1, &InputDataConsumed);
            IoReleaseRemoveLock(&pDevExt->RemoveLock, pDevExt);
        }
        else
        {
            WARN(("IoAcquireRemoveLock failed, Status (0x%x)", Status));
        }
    }
    else
    {
        WARN(("no current pDevExt specified"));
    }
}

static BOOLEAN nemuNewProtIsEnabled()
{
    return g_ctx.fIsNewProtEnabled;
}

static NTSTATUS nemuNewProtRegisterMouseEventCb(BOOLEAN fRegister)
{
    NemuGuestMouseSetNotifyCallback CbInfo = {};
    CbInfo.pfnNotify = fRegister ? nemuNewProtMouseEventCb : NULL;

    NTSTATUS Status = nemuGdcSubmit(NEMUGUEST_IOCTL_SET_MOUSE_NOTIFY_CALLBACK, &CbInfo, sizeof (CbInfo));
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuGdcSubmit failed Status(0x%x)", Status));
    }
    return Status;
}


NTSTATUS NemuNewProtInit()
{
    NTSTATUS Status = NemuGdcInit();
    if (NT_SUCCESS(Status))
    {
        KeInitializeSpinLock(&g_ctx.SyncLock);
        InitializeListHead(&g_ctx.DevExtList);
        /* we assume the new prot data in g_ctx is zero-initialized (see g_ctx definition) */

        Status = nemuNewProtRegisterMouseEventCb(TRUE);
        if (NT_SUCCESS(Status))
        {
            g_ctx.fIsNewProtEnabled = TRUE;
            return STATUS_SUCCESS;
        }
        NemuGdcTerm();
    }

    return Status;
}

NTSTATUS NemuNewProtTerm()
{
    Assert(IsListEmpty(&g_ctx.DevExtList));
    if (!nemuNewProtIsEnabled())
    {
        WARN(("New Protocol is disabled"));
        return STATUS_SUCCESS;
    }

    g_ctx.fIsNewProtEnabled = FALSE;

    NTSTATUS Status = nemuNewProtRegisterMouseEventCb(FALSE);
    if (!NT_SUCCESS(Status))
    {
        WARN(("KeWaitForSingleObject failed, Status (0x%x)", Status));
        return Status;
    }

    NemuGdcTerm();

    return STATUS_SUCCESS;
}

static NTSTATUS nemuNewProtDeviceAdded(PNEMUMOUSE_DEVEXT pDevExt)
{
    if (!nemuNewProtIsEnabled())
    {
        WARN(("New Protocol is disabled"));
        return STATUS_UNSUCCESSFUL;
    }

    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL Irql;
    KeAcquireSpinLock(&g_ctx.SyncLock, &Irql);
    InsertHeadList(&g_ctx.DevExtList, &pDevExt->ListEntry);
    if (!g_ctx.pCurrentDevExt)
    {
        ASMAtomicWritePtr(&g_ctx.pCurrentDevExt, pDevExt);
        /* ensure the object is not deleted while it is being used by a poller thread */
        ObReferenceObject(pDevExt->pdoSelf);
    }
    KeReleaseSpinLock(&g_ctx.SyncLock, Irql);

    return Status;
}

#define PNEMUMOUSE_DEVEXT_FROM_LE(_pLe) ( (PNEMUMOUSE_DEVEXT)(((uint8_t*)(_pLe)) - RT_OFFSETOF(NEMUMOUSE_DEVEXT, ListEntry)) )

static NTSTATUS nemuNewProtDeviceRemoved(PNEMUMOUSE_DEVEXT pDevExt)
{
    if (!nemuNewProtIsEnabled())
    {
        WARN(("New Protocol is disabled"));
        return STATUS_UNSUCCESSFUL;
    }

    KIRQL Irql;
    NTSTATUS Status = STATUS_SUCCESS;
    KeAcquireSpinLock(&g_ctx.SyncLock, &Irql);
    RemoveEntryList(&pDevExt->ListEntry);
    if (g_ctx.pCurrentDevExt == pDevExt)
    {
        ObDereferenceObject(pDevExt->pdoSelf);
        g_ctx.pCurrentDevExt = NULL;
        for (PLIST_ENTRY pCur = g_ctx.DevExtList.Flink; pCur != &g_ctx.DevExtList; pCur = pCur->Flink)
        {
            PNEMUMOUSE_DEVEXT pNewCurDevExt = PNEMUMOUSE_DEVEXT_FROM_LE(pCur);
            ASMAtomicWritePtr(&g_ctx.pCurrentDevExt, pNewCurDevExt);
            /* ensure the object is not deleted while it is being used by a poller thread */
            ObReferenceObject(pNewCurDevExt->pdoSelf);
            break;
        }
    }

    KeReleaseSpinLock(&g_ctx.SyncLock, Irql);

    return Status;
}

VOID NemuDrvNotifyServiceCB(PNEMUMOUSE_DEVEXT pDevExt, PMOUSE_INPUT_DATA InputDataStart, PMOUSE_INPUT_DATA InputDataEnd, PULONG  InputDataConsumed)
{
    KIRQL Irql;
    /* we need to avoid concurrency between the poller thread and our ServiceCB.
     * this is perhaps not the best way of doing things, but the most easiest to avoid concurrency
     * and to ensure the pfnServiceCB is invoked at DISPATCH_LEVEL */
    KeAcquireSpinLock(&g_ctx.SyncLock, &Irql);
    if (pDevExt->pSCReq)
    {
        int rc = VbglGRPerform(&pDevExt->pSCReq->header);

        if (RT_SUCCESS(rc))
        {
            if (pDevExt->pSCReq->mouseFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE)
            {
                PMOUSE_INPUT_DATA pData = InputDataStart;
                while (pData<InputDataEnd)
                {
                    pData->LastX = pDevExt->pSCReq->pointerXPos;
                    pData->LastY = pDevExt->pSCReq->pointerYPos;
                    pData->Flags = MOUSE_MOVE_ABSOLUTE;
                    if (nemuNewProtIsEnabled())
                        pData->Flags |= MOUSE_VIRTUAL_DESKTOP;
                    pData++;
                }

                /* get the last data & cache it */
                --pData;
                g_ctx.LastReportedData.UnitId = pData->UnitId;
            }
        }
        else
        {
            WARN(("VbglGRPerform failed with rc=%#x", rc));
        }
    }

    /* Call original callback */
    pDevExt->OriginalConnectData.pfnServiceCB(pDevExt->OriginalConnectData.pDO,
                                              InputDataStart, InputDataEnd, InputDataConsumed);
    KeReleaseSpinLock(&g_ctx.SyncLock, Irql);
}

static BOOLEAN nemuIsVBGLInited(void)
{
   return InterlockedCompareExchange(&g_ctx.fVBGLInited, TRUE, TRUE) == TRUE;
}

static BOOLEAN nemuIsVBGLInitFailed (void)
{
   return InterlockedCompareExchange(&g_ctx.fVBGLInitFailed, TRUE, TRUE) == TRUE;
}

static BOOLEAN nemuIsHostInformed(void)
{
   return InterlockedCompareExchange(&g_ctx.fHostInformed, TRUE, TRUE) == TRUE;
}

static BOOLEAN nemuIsHostMouseFound(void)
{
   return InterlockedCompareExchange(&g_ctx.fHostMouseFound, TRUE, TRUE) == TRUE;
}

VOID NemuDeviceAdded(PNEMUMOUSE_DEVEXT pDevExt)
{
    LOGF_ENTER();
    LONG callCnt = InterlockedIncrement(&g_ctx.cDevicesStarted);

    /* One time Vbgl initialization */
    if (callCnt == 1)
    {
        if (!nemuIsVBGLInited() && !nemuIsVBGLInitFailed())
        {
            int rc = VbglInitClient();

            if (RT_SUCCESS(rc))
            {
                InterlockedExchange(&g_ctx.fVBGLInited, TRUE);
                LOG(("VBGL init OK"));
            }
            else
            {
                InterlockedExchange (&g_ctx.fVBGLInitFailed, TRUE);
                WARN(("VBGL init failed with rc=%#x", rc));
            }
        }
    }

    nemuNewProtDeviceAdded(pDevExt);

    if (!nemuIsHostMouseFound())
    {
        NTSTATUS rc;
        UCHAR buffer[512];
        CM_RESOURCE_LIST *pResourceList = (CM_RESOURCE_LIST *)&buffer[0];
        ULONG cbWritten=0;
        BOOLEAN bDetected = FALSE;

        rc = IoGetDeviceProperty(pDevExt->pdoMain, DevicePropertyBootConfiguration,
                                 sizeof(buffer), &buffer[0], &cbWritten);
        if (!NT_SUCCESS(rc))
        {
            WARN(("IoGetDeviceProperty failed with rc=%#x", rc));
            return;
        }

        LOG(("Number of descriptors: %d", pResourceList->Count));

        /* Check if device claims IO port 0x60 or int12 */
        for (ULONG i=0; i<pResourceList->Count; ++i)
        {
            CM_FULL_RESOURCE_DESCRIPTOR *pFullDescriptor = &pResourceList->List[i];

            LOG(("FullDescriptor[%i]: IfType %d, Bus %d, Ver %d, Rev %d, Count %d",
                 i, pFullDescriptor->InterfaceType, pFullDescriptor->BusNumber,
                 pFullDescriptor->PartialResourceList.Version, pFullDescriptor->PartialResourceList.Revision,
                 pFullDescriptor->PartialResourceList.Count));

            for (ULONG j=0; j<pFullDescriptor->PartialResourceList.Count; ++j)
            {
                CM_PARTIAL_RESOURCE_DESCRIPTOR *pPartialDescriptor = &pFullDescriptor->PartialResourceList.PartialDescriptors[j];
                LOG(("PartialDescriptor[%d]: type %d, ShareDisposition %d, Flags 0x%04X, Start 0x%llx, length 0x%x",
                     j, pPartialDescriptor->Type, pPartialDescriptor->ShareDisposition, pPartialDescriptor->Flags,
                     pPartialDescriptor->u.Generic.Start.QuadPart, pPartialDescriptor->u.Generic.Length));

                switch(pPartialDescriptor->Type)
                {
                    case CmResourceTypePort:
                    {
                        LOG(("CmResourceTypePort %#x", pPartialDescriptor->u.Port.Start.QuadPart));
                        if (pPartialDescriptor->u.Port.Start.QuadPart == 0x60)
                        {
                            bDetected = TRUE;
                        }
                        break;
                    }
                    case CmResourceTypeInterrupt:
                    {
                        LOG(("CmResourceTypeInterrupt %ld", pPartialDescriptor->u.Interrupt.Vector));
                        if (pPartialDescriptor->u.Interrupt.Vector == 0xC)
                        {
                            bDetected = TRUE;
                        }
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        }

        if (bDetected)
        {
            /* It's the emulated 8042 PS/2 mouse/kbd device, so mark it as the Host one.
             * For this device the filter will query absolute mouse coords from the host.
             */
            InterlockedExchange(&g_ctx.fHostMouseFound, TRUE);

            pDevExt->bHostMouse = TRUE;
            LOG(("Host mouse found"));
        }
    }
    LOGF_LEAVE();
}

VOID NemuInformHost(PNEMUMOUSE_DEVEXT pDevExt)
{
    LOGF_ENTER();

    if (!nemuIsVBGLInited())
    {
        WARN(("!nemuIsVBGLInited"));
        return;
    }

    /* Inform host we support absolute coordinates */
    if (pDevExt->bHostMouse && !nemuIsHostInformed())
    {
        VMMDevReqMouseStatus *req = NULL;
        int rc = VbglGRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevReqMouseStatus), VMMDevReq_SetMouseStatus);

        if (RT_SUCCESS(rc))
        {
            req->mouseFeatures = VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE;
            if (nemuNewProtIsEnabled())
                req->mouseFeatures |= VMMDEV_MOUSE_NEW_PROTOCOL;

            req->pointerXPos = 0;
            req->pointerYPos = 0;

            rc = VbglGRPerform(&req->header);

            if (RT_SUCCESS(rc))
            {
                InterlockedExchange(&g_ctx.fHostInformed, TRUE);
            }
            else
            {
                WARN(("VbglGRPerform failed with rc=%#x", rc));
            }

            VbglGRFree(&req->header);
        }
        else
        {
            WARN(("VbglGRAlloc failed with rc=%#x", rc));
        }
    }

    /* Preallocate request to be used in NemuServiceCB*/
    if (pDevExt->bHostMouse && !pDevExt->pSCReq)
    {
        VMMDevReqMouseStatus *req = NULL;

        int rc = VbglGRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevReqMouseStatus), VMMDevReq_GetMouseStatus);

        if (RT_SUCCESS(rc))
        {
            InterlockedExchangePointer((PVOID volatile *)&pDevExt->pSCReq, req);
        }
        else
        {
            WARN(("VbglGRAlloc for service callback failed with rc=%#x", rc));
        }
    }

    LOGF_LEAVE();
}

VOID NemuDeviceRemoved(PNEMUMOUSE_DEVEXT pDevExt)
{
    LOGF_ENTER();

    /* Save the allocated request pointer and clear the devExt. */
    VMMDevReqMouseStatus *pSCReq = (VMMDevReqMouseStatus *) InterlockedExchangePointer((PVOID volatile *)&pDevExt->pSCReq, NULL);

    if (pDevExt->bHostMouse && nemuIsHostInformed())
    {
        // tell the VMM that from now on we can't handle absolute coordinates anymore
        VMMDevReqMouseStatus *req = NULL;

        int rc = VbglGRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevReqMouseStatus), VMMDevReq_SetMouseStatus);

        if (RT_SUCCESS(rc))
        {
            req->mouseFeatures = 0;
            req->pointerXPos = 0;
            req->pointerYPos = 0;

            rc = VbglGRPerform(&req->header);

            if (RT_FAILURE(rc))
            {
                WARN(("VbglGRPerform failed with rc=%#x", rc));
            }

            VbglGRFree(&req->header);
        }
        else
        {
            WARN(("VbglGRAlloc failed with rc=%#x", rc));
        }

        InterlockedExchange(&g_ctx.fHostInformed, FALSE);
    }

    if (pSCReq)
    {
        VbglGRFree(&pSCReq->header);
    }

    LONG callCnt = InterlockedDecrement(&g_ctx.cDevicesStarted);

    nemuNewProtDeviceRemoved(pDevExt);

    if (callCnt == 0)
    {
        if (nemuIsVBGLInited())
        {
            /* Set the flag to prevent reinitializing of the VBGL. */
            InterlockedExchange(&g_ctx.fVBGLInitFailed, TRUE);

            VbglTerminate();

            /* The VBGL is now in the not initialized state. */
            InterlockedExchange(&g_ctx.fVBGLInited, FALSE);
            InterlockedExchange(&g_ctx.fVBGLInitFailed, FALSE);
        }
    }

    LOGF_LEAVE();
}
