/* $Id: NemuUsbRt.cpp $ */
/** @file
 * Nemu USB R0 runtime
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
#include "../cmn/NemuUsbIdc.h"
#include "../cmn/NemuUsbTool.h"

#include <Nemu/usblib-win.h>
#include <iprt/assert.h>
#include <Nemu/log.h>
#define _USBD_

#define USBD_DEFAULT_PIPE_TRANSFER 0x00000008

#define NEMUUSB_MAGIC  0xABCF1423

typedef struct NEMUUSB_URB_CONTEXT
{
    PURB pUrb;
    PMDL pMdlBuf;
    PNEMUUSBDEV_EXT pDevExt;
    PVOID pOut;
    ULONG ulTransferType;
    ULONG ulMagic;
} NEMUUSB_URB_CONTEXT, * PNEMUUSB_URB_CONTEXT;

typedef struct NEMUUSB_SETUP
{
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} NEMUUSB_SETUP, *PNEMUUSB_SETUP;

static bool nemuUsbRtCtxSetOwner(PNEMUUSBDEV_EXT pDevExt, PFILE_OBJECT pFObj)
{
    bool bRc = ASMAtomicCmpXchgPtr(&pDevExt->Rt.pOwner, pFObj, NULL);
    if (bRc)
    {
        Log((__FUNCTION__": pDevExt (0x%x) Owner(0x%x) acquired\n", pFObj));
    }
    else
    {
        Log((__FUNCTION__": pDevExt (0x%x) Owner(0x%x) FAILED!!\n", pFObj));
    }
    return bRc;
}

static bool nemuUsbRtCtxReleaseOwner(PNEMUUSBDEV_EXT pDevExt, PFILE_OBJECT pFObj)
{
    bool bRc = ASMAtomicCmpXchgPtr(&pDevExt->Rt.pOwner, NULL, pFObj);
    if (bRc)
    {
        Log((__FUNCTION__": pDevExt (0x%x) Owner(0x%x) released\n", pFObj));
    }
    else
    {
        Log((__FUNCTION__": pDevExt (0x%x) Owner(0x%x) release: is NOT an owner\n", pFObj));
    }
    return bRc;
}

static bool nemuUsbRtCtxIsOwner(PNEMUUSBDEV_EXT pDevExt, PFILE_OBJECT pFObj)
{
    PFILE_OBJECT pOwner = (PFILE_OBJECT)ASMAtomicReadPtr((void *volatile *)(&pDevExt->Rt.pOwner));
    return pOwner == pFObj;
}

static NTSTATUS nemuUsbRtIdcSubmit(ULONG uCtl, void *pvBuffer)
{
    /* we just reuse the standard usb tooling for simplicity here */
    NTSTATUS Status = NemuUsbToolIoInternalCtlSendSync(g_NemuUsbGlobals.RtIdc.pDevice, uCtl, pvBuffer, NULL);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

static NTSTATUS nemuUsbRtIdcInit()
{
    UNICODE_STRING UniName;
    RtlInitUnicodeString(&UniName, USBMON_DEVICE_NAME_NT);
    NTSTATUS Status = IoGetDeviceObjectPointer(&UniName, FILE_ALL_ACCESS, &g_NemuUsbGlobals.RtIdc.pFile, &g_NemuUsbGlobals.RtIdc.pDevice);
    if (NT_SUCCESS(Status))
    {
        NEMUUSBIDC_VERSION Version;
        nemuUsbRtIdcSubmit(NEMUUSBIDC_INTERNAL_IOCTL_GET_VERSION, &Version);
        if (NT_SUCCESS(Status))
        {
            if (Version.u32Major == NEMUUSBIDC_VERSION_MAJOR
                    && Version.u32Minor >= NEMUUSBIDC_VERSION_MINOR)
                return STATUS_SUCCESS;
            AssertFailed();
        }
        else
        {
            AssertFailed();
        }

        /* this will as well dereference the dev obj */
        ObDereferenceObject(g_NemuUsbGlobals.RtIdc.pFile);
    }
    else
    {
        AssertFailed();
    }

    memset(&g_NemuUsbGlobals.RtIdc, 0, sizeof (g_NemuUsbGlobals.RtIdc));
    return Status;
}

static VOID nemuUsbRtIdcTerm()
{
    Assert(g_NemuUsbGlobals.RtIdc.pFile);
    Assert(g_NemuUsbGlobals.RtIdc.pDevice);
    ObDereferenceObject(g_NemuUsbGlobals.RtIdc.pFile);
    memset(&g_NemuUsbGlobals.RtIdc, 0, sizeof (g_NemuUsbGlobals.RtIdc));
}

static NTSTATUS nemuUsbRtIdcReportDevStart(PDEVICE_OBJECT pPDO, HNEMUUSBIDCDEV *phDev)
{
    NEMUUSBIDC_PROXY_STARTUP Start;
    Start.u.pPDO = pPDO;

    *phDev = NULL;

    NTSTATUS Status = nemuUsbRtIdcSubmit(NEMUUSBIDC_INTERNAL_IOCTL_PROXY_STARTUP, &Start);
    Assert(Status == STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return Status;

    *phDev = Start.u.hDev;
    return STATUS_SUCCESS;
}

static NTSTATUS nemuUsbRtIdcReportDevStop(HNEMUUSBIDCDEV hDev)
{
    NEMUUSBIDC_PROXY_TEARDOWN Stop;
    Stop.hDev = hDev;

    NTSTATUS Status = nemuUsbRtIdcSubmit(NEMUUSBIDC_INTERNAL_IOCTL_PROXY_TEARDOWN, &Stop);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}


DECLHIDDEN(NTSTATUS) nemuUsbRtGlobalsInit()
{
    return nemuUsbRtIdcInit();
}

DECLHIDDEN(VOID) nemuUsbRtGlobalsTerm()
{
    nemuUsbRtIdcTerm();
}


DECLHIDDEN(NTSTATUS) nemuUsbRtInit(PNEMUUSBDEV_EXT pDevExt)
{
    RtlZeroMemory(&pDevExt->Rt, sizeof (pDevExt->Rt));
    NTSTATUS Status = IoRegisterDeviceInterface(pDevExt->pPDO, &GUID_CLASS_NEMUUSB,
                                NULL, /* IN PUNICODE_STRING ReferenceString OPTIONAL */
                                &pDevExt->Rt.IfName);
    Assert(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = nemuUsbRtIdcReportDevStart(pDevExt->pPDO, &pDevExt->Rt.hMonDev);
        Assert(Status == STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Assert(pDevExt->Rt.hMonDev);
            return STATUS_SUCCESS;
        }

        NTSTATUS tmpStatus = IoSetDeviceInterfaceState(&pDevExt->Rt.IfName, FALSE);
        Assert(tmpStatus == STATUS_SUCCESS);
        if (NT_SUCCESS(tmpStatus))
        {
            RtlFreeUnicodeString(&pDevExt->Rt.IfName);
        }
    }
    return Status;
}

/**
 * Free cached USB device/configuration descriptors
 *
 * @param   pDevExt             USB DevExt pointer
 */
static void nemuUsbRtFreeCachedDescriptors(PNEMUUSBDEV_EXT pDevExt)
{
    if (pDevExt->Rt.devdescr)
    {
        nemuUsbMemFree(pDevExt->Rt.devdescr);
        pDevExt->Rt.devdescr = NULL;
    }
    for (ULONG i = 0; i < NEMUUSBRT_MAX_CFGS; ++i)
    {
        if (pDevExt->Rt.cfgdescr[i])
        {
            nemuUsbMemFree(pDevExt->Rt.cfgdescr[i]);
            pDevExt->Rt.cfgdescr[i] = NULL;
        }
    }
}

/**
 * Free per-device interface info
 *
 * @param   pDevExt             USB DevExt pointer
 * @param   fAbortPipes         If true, also abort any open pipes
 */
static void nemuUsbRtFreeInterfaces(PNEMUUSBDEV_EXT pDevExt, BOOLEAN fAbortPipes)
{
    unsigned i;
    unsigned j;

    /*
     * Free old interface info
     */
    if (pDevExt->Rt.pVBIfaceInfo)
    {
        for (i=0;i<pDevExt->Rt.uNumInterfaces;i++)
        {
            if (pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo)
            {
                if (fAbortPipes)
                {
                    for(j=0; j<pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->NumberOfPipes; j++)
                    {
                        Log(("Aborting Pipe %d handle %x address %x\n", j,
                                 pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].PipeHandle,
                                 pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].EndpointAddress));
                        NemuUsbToolPipeClear(pDevExt->pLowerDO, pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].PipeHandle, FALSE);
                    }
                }
                nemuUsbMemFree(pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo);
            }
            pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo = NULL;
            if (pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo)
                nemuUsbMemFree(pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo);
            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo = NULL;
        }
        nemuUsbMemFree(pDevExt->Rt.pVBIfaceInfo);
        pDevExt->Rt.pVBIfaceInfo = NULL;
    }
}

DECLHIDDEN(VOID) nemuUsbRtClear(PNEMUUSBDEV_EXT pDevExt)
{
    nemuUsbRtFreeCachedDescriptors(pDevExt);
    nemuUsbRtFreeInterfaces(pDevExt, FALSE);
}

DECLHIDDEN(NTSTATUS) nemuUsbRtRm(PNEMUUSBDEV_EXT pDevExt)
{
    if (!pDevExt->Rt.IfName.Buffer)
        return STATUS_SUCCESS;

    NTSTATUS Status = nemuUsbRtIdcReportDevStop(pDevExt->Rt.hMonDev);
    Assert(Status == STATUS_SUCCESS);
    Status = IoSetDeviceInterfaceState(&pDevExt->Rt.IfName, FALSE);
    Assert(Status == STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&pDevExt->Rt.IfName);
        pDevExt->Rt.IfName.Buffer = NULL;
    }
    return Status;
}

DECLHIDDEN(NTSTATUS) nemuUsbRtStart(PNEMUUSBDEV_EXT pDevExt)
{
    NTSTATUS Status = IoSetDeviceInterfaceState(&pDevExt->Rt.IfName, TRUE);
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

static NTSTATUS nemuUsbRtCacheDescriptors(PNEMUUSBDEV_EXT pDevExt)
{
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
//    uint32_t uTotalLength;
//    unsigned                        i;

    /* Read device descriptor */
    Assert(!pDevExt->Rt.devdescr);
    pDevExt->Rt.devdescr = (PUSB_DEVICE_DESCRIPTOR)nemuUsbMemAlloc(sizeof (USB_DEVICE_DESCRIPTOR));
    if (pDevExt->Rt.devdescr)
    {
        memset(pDevExt->Rt.devdescr, 0, sizeof (USB_DEVICE_DESCRIPTOR));
        Status = NemuUsbToolGetDescriptor(pDevExt->pLowerDO, pDevExt->Rt.devdescr, sizeof (USB_DEVICE_DESCRIPTOR), USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, RT_INDEFINITE_WAIT);
        if (NT_SUCCESS(Status))
        {
            Assert(pDevExt->Rt.devdescr->bNumConfigurations > 0);
            PUSB_CONFIGURATION_DESCRIPTOR pDr = (PUSB_CONFIGURATION_DESCRIPTOR)nemuUsbMemAlloc(sizeof (USB_CONFIGURATION_DESCRIPTOR));
            Assert(pDr);
            if (pDr)
            {
                UCHAR i = 0;
                for (; i < pDevExt->Rt.devdescr->bNumConfigurations; ++i)
                {
                    Status = NemuUsbToolGetDescriptor(pDevExt->pLowerDO, pDr, sizeof (USB_CONFIGURATION_DESCRIPTOR), USB_CONFIGURATION_DESCRIPTOR_TYPE, i, 0, RT_INDEFINITE_WAIT);
                    if (!NT_SUCCESS(Status))
                    {
                        break;
                    }

                    USHORT uTotalLength = pDr->wTotalLength;
                    pDevExt->Rt.cfgdescr[i] = (PUSB_CONFIGURATION_DESCRIPTOR)nemuUsbMemAlloc(uTotalLength);
                    if (!pDevExt->Rt.cfgdescr[i])
                    {
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                        break;
                    }

                    Status = NemuUsbToolGetDescriptor(pDevExt->pLowerDO, pDevExt->Rt.cfgdescr[i], uTotalLength, USB_CONFIGURATION_DESCRIPTOR_TYPE, i, 0, RT_INDEFINITE_WAIT);
                    if (!NT_SUCCESS(Status))
                    {
                        break;
                    }
                }

                nemuUsbMemFree(pDr);

                if (NT_SUCCESS(Status))
                    return Status;

                /* recources will be freed in nemuUsbRtFreeCachedDescriptors below */
            }
        }

        nemuUsbRtFreeCachedDescriptors(pDevExt);
    }

    /* shoud be only on fail here */
    Assert(!NT_SUCCESS(Status));
    return Status;
}

static NTSTATUS nemuUsbRtDispatchClaimDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_CLAIMDEV pDev  = (PUSBSUP_CLAIMDEV)pIrp->AssociatedIrp.SystemBuffer;
    ULONG cbOut = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (  !pDev
            || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pDev)
            || pSl->Parameters.DeviceIoControl.OutputBufferLength != sizeof (*pDev))
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!nemuUsbRtCtxSetOwner(pDevExt, pFObj))
        {
            AssertFailed();
            pDev->fClaimed = false;
            cbOut = sizeof (*pDev);
            break;
        }

        nemuUsbRtFreeCachedDescriptors(pDevExt);
        Status = nemuUsbRtCacheDescriptors(pDevExt);
        if (NT_SUCCESS(Status))
        {
            pDev->fClaimed = true;
            cbOut = sizeof (*pDev);
        }
    } while (0);

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, cbOut);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbRtDispatchReleaseDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    NTSTATUS Status= STATUS_SUCCESS;

    if (nemuUsbRtCtxIsOwner(pDevExt, pFObj))
    {
        nemuUsbRtFreeCachedDescriptors(pDevExt);
        bool bRc = nemuUsbRtCtxReleaseOwner(pDevExt, pFObj);
        Assert(bRc);
    }
    else
    {
        AssertFailed();
        Status = STATUS_ACCESS_DENIED;
    }

    NemuDrvToolIoComplete(pIrp, STATUS_SUCCESS, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return STATUS_SUCCESS;
}

static NTSTATUS nemuUsbRtGetDeviceDescription(PNEMUUSBDEV_EXT pDevExt)
{
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    PUSB_DEVICE_DESCRIPTOR pDr = (PUSB_DEVICE_DESCRIPTOR)nemuUsbMemAllocZ(sizeof (USB_DEVICE_DESCRIPTOR));
    if (pDr)
    {
        Status = NemuUsbToolGetDescriptor(pDevExt->pLowerDO, pDr, sizeof(*pDr), USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, RT_INDEFINITE_WAIT);
        if (NT_SUCCESS(Status))
        {
            pDevExt->Rt.idVendor    = pDr->idVendor;
            pDevExt->Rt.idProduct   = pDr->idProduct;
            pDevExt->Rt.bcdDevice   = pDr->bcdDevice;
            pDevExt->Rt.szSerial[0] = 0;

            if (pDr->iSerialNumber
#ifdef DEBUG
                    || pDr->iProduct || pDr->iManufacturer
#endif
               )
            {
                int langId;
                Status = NemuUsbToolGetLangID(pDevExt->pLowerDO, &langId, RT_INDEFINITE_WAIT);
                if (NT_SUCCESS(Status))
                {
                    Status = NemuUsbToolGetStringDescriptorA(pDevExt->pLowerDO, pDevExt->Rt.szSerial, sizeof (pDevExt->Rt.szSerial), pDr->iSerialNumber, langId, RT_INDEFINITE_WAIT);
                }
                else
                {
                    Status = STATUS_SUCCESS;
                }
            }
        }
        nemuUsbMemFree(pDr);
    }

    return Status;
}

static NTSTATUS nemuUsbRtDispatchGetDevice(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PUSBSUP_GETDEV pDev  = (PUSBSUP_GETDEV)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG cbOut = 0;

    /* don't check for owner since this request is allowed for non-owners as well */

    if (pDev && pSl->Parameters.DeviceIoControl.InputBufferLength == sizeof (*pDev)
             && pSl->Parameters.DeviceIoControl.OutputBufferLength == sizeof (*pDev))
    {
        Status = NemuUsbToolGetDeviceSpeed(pDevExt->pLowerDO, &pDevExt->Rt.fIsHighSpeed);
        if (NT_SUCCESS(Status))
        {
            pDev->hDevice = pDevExt->Rt.hMonDev;
            pDev->fAttached = true;
            pDev->fHiSpeed = pDevExt->Rt.fIsHighSpeed;
            cbOut = sizeof (*pDev);
        }
    }
    else
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, cbOut);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbRtDispatchUsbReset(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_GETDEV pDev  = (PUSBSUP_GETDEV)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!nemuUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (pIrp->AssociatedIrp.SystemBuffer
                || pSl->Parameters.DeviceIoControl.InputBufferLength
                || pSl->Parameters.DeviceIoControl.OutputBufferLength)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = NemuUsbToolIoInternalCtlSendSync(pDevExt->pLowerDO, IOCTL_INTERNAL_USB_RESET_PORT, NULL, NULL);
        Assert(NT_SUCCESS(Status));
    } while (0);

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static PUSB_CONFIGURATION_DESCRIPTOR nemuUsbRtFindConfigDesc(PNEMUUSBDEV_EXT pDevExt, uint8_t uConfiguration)
{
    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = NULL;

    for (ULONG i = 0; i < NEMUUSBRT_MAX_CFGS; ++i)
    {
        if (pDevExt->Rt.cfgdescr[i])
        {
            if (pDevExt->Rt.cfgdescr[i]->bConfigurationValue == uConfiguration)
            {
                pCfgDr = pDevExt->Rt.cfgdescr[i];
                break;
            }
        }
    }

    return pCfgDr;
}

static NTSTATUS nemuUsbRtSetConfig(PNEMUUSBDEV_EXT pDevExt, uint8_t uConfiguration)
{
    PURB pUrb = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    uint32_t i;

    if (!uConfiguration)
    {
        pUrb = NemuUsbToolUrbAllocZ(URB_FUNCTION_SELECT_CONFIGURATION, sizeof (struct _URB_SELECT_CONFIGURATION));
        if(!pUrb)
        {
            AssertMsgFailed((__FUNCTION__": NemuUsbToolUrbAlloc failed\n"));
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        nemuUsbRtFreeInterfaces(pDevExt, TRUE);

        pUrb->UrbSelectConfiguration.ConfigurationDescriptor = NULL;

        Status = NemuUsbToolUrbPost(pDevExt->pLowerDO, pUrb, RT_INDEFINITE_WAIT);
        if(NT_SUCCESS(Status) && USBD_SUCCESS(pUrb->UrbHeader.Status))
        {
            pDevExt->Rt.hConfiguration = pUrb->UrbSelectConfiguration.ConfigurationHandle;
            pDevExt->Rt.uConfigValue = uConfiguration;
        }
        else
        {
            AssertMsgFailed((__FUNCTION__": NemuUsbToolUrbPost failed Status (0x%x), usb Status (0x%x)\n", Status, pUrb->UrbHeader.Status));
        }

        NemuUsbToolUrbFree(pUrb);

        return Status;
    }

    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = nemuUsbRtFindConfigDesc(pDevExt, uConfiguration);
    if (!pCfgDr)
    {
        AssertMsgFailed((__FUNCTION__": NemuUSBFindConfigDesc did not find cfg (%d)\n", uConfiguration));
        return STATUS_INVALID_PARAMETER;
    }

    PUSBD_INTERFACE_LIST_ENTRY pIfLe = (PUSBD_INTERFACE_LIST_ENTRY)nemuUsbMemAllocZ((pCfgDr->bNumInterfaces + 1) * sizeof(USBD_INTERFACE_LIST_ENTRY));
    if (!pIfLe)
    {
        AssertMsgFailed((__FUNCTION__": nemuUsbMemAllocZ for pIfLe failed\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i = 0; i < pCfgDr->bNumInterfaces; i++)
    {
        pIfLe[i].InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(pCfgDr, pCfgDr, i, 0, -1, -1, -1);
        if (!pIfLe[i].InterfaceDescriptor)
        {
            AssertMsgFailed((__FUNCTION__": interface %d not found\n", i));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
    }
    pIfLe[pCfgDr->bNumInterfaces].InterfaceDescriptor = NULL;

    if (NT_SUCCESS(Status))
    {
        pUrb = USBD_CreateConfigurationRequestEx(pCfgDr, pIfLe);
        if (pUrb)
        {
            Status = NemuUsbToolUrbPost(pDevExt->pLowerDO, pUrb, RT_INDEFINITE_WAIT);
            if (NT_SUCCESS(Status) && USBD_SUCCESS(pUrb->UrbHeader.Status))
            {
                nemuUsbRtFreeInterfaces(pDevExt, FALSE);

                pDevExt->Rt.hConfiguration = pUrb->UrbSelectConfiguration.ConfigurationHandle;
                pDevExt->Rt.uConfigValue = uConfiguration;
                pDevExt->Rt.uNumInterfaces = pCfgDr->bNumInterfaces;

                pDevExt->Rt.pVBIfaceInfo = (NEMUUSB_IFACE_INFO*)nemuUsbMemAllocZ(pDevExt->Rt.uNumInterfaces * sizeof (NEMUUSB_IFACE_INFO));
                if (pDevExt->Rt.pVBIfaceInfo)
                {
                    Assert(NT_SUCCESS(Status));
                    for (i = 0; i < pDevExt->Rt.uNumInterfaces; i++)
                    {
                        size_t uTotalIfaceInfoLength = GET_USBD_INTERFACE_SIZE(pIfLe[i].Interface->NumberOfPipes);
                        pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo = (PUSBD_INTERFACE_INFORMATION)nemuUsbMemAlloc(uTotalIfaceInfoLength);
                        if (!pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo)
                        {
                            AssertMsgFailed((__FUNCTION__": nemuUsbMemAlloc failed\n"));
                            Status = STATUS_INSUFFICIENT_RESOURCES;
                            break;
                        }

                        if (pIfLe[i].Interface->NumberOfPipes > 0)
                        {
                            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo = (NEMUUSB_PIPE_INFO *)nemuUsbMemAlloc(pIfLe[i].Interface->NumberOfPipes * sizeof(NEMUUSB_PIPE_INFO));
                            if (!pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo)
                            {
                                AssertMsgFailed((__FUNCTION__": nemuUsbMemAlloc failed\n"));
                                Status = STATUS_NO_MEMORY;
                                break;
                            }
                        }
                        else
                        {
                            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo = NULL;
                        }

                        RtlCopyMemory(pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo, pIfLe[i].Interface, uTotalIfaceInfoLength);

                        for (ULONG j = 0; j < pIfLe[i].Interface->NumberOfPipes; j++)
                        {
                            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo[j].EndpointAddress = pIfLe[i].Interface->Pipes[j].EndpointAddress;
                            pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo[j].NextScheduledFrame = 0;
                        }
                    }

//                    if (NT_SUCCESS(Status))
//                    {
//
//                    }
                }
                else
                {
                    AssertMsgFailed((__FUNCTION__": nemuUsbMemAllocZ failed\n"));
                    Status = STATUS_NO_MEMORY;
                }
            }
            else
            {
                AssertMsgFailed((__FUNCTION__": NemuUsbToolUrbPost failed Status (0x%x), usb Status (0x%x)\n", Status, pUrb->UrbHeader.Status));
            }
            ExFreePool(pUrb);
        }
        else
        {
            AssertMsgFailed((__FUNCTION__": USBD_CreateConfigurationRequestEx failed\n"));
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    nemuUsbMemFree(pIfLe);

    return Status;
}

static NTSTATUS nemuUsbRtDispatchUsbSetConfig(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_SET_CONFIG pCfg  = (PUSBSUP_SET_CONFIG)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!nemuUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (      !pCfg
                || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pCfg)
                || pSl->Parameters.DeviceIoControl.OutputBufferLength != 0)
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = nemuUsbRtSetConfig(pDevExt, pCfg->bConfigurationValue);
    } while (0);

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbRtSetInterface(PNEMUUSBDEV_EXT pDevExt, uint32_t InterfaceNumber, int AlternateSetting)
{
    if (!pDevExt->Rt.uConfigValue)
    {
        AssertMsgFailed((__FUNCTION__": Can't select an interface without an active configuration\n"));
        return STATUS_INVALID_PARAMETER;
    }

    if (InterfaceNumber >= pDevExt->Rt.uNumInterfaces)
    {
        AssertMsgFailed((__FUNCTION__": InterfaceNumber %d too high!!\n", InterfaceNumber));
        return STATUS_INVALID_PARAMETER;
    }

    PUSB_CONFIGURATION_DESCRIPTOR pCfgDr = nemuUsbRtFindConfigDesc(pDevExt, pDevExt->Rt.uConfigValue);
    if (!pCfgDr)
    {
        AssertMsgFailed((__FUNCTION__": configuration %d not found!!\n", pDevExt->Rt.uConfigValue));
        return STATUS_INVALID_PARAMETER;
    }

    PUSB_INTERFACE_DESCRIPTOR pIfDr = USBD_ParseConfigurationDescriptorEx(pCfgDr, pCfgDr, InterfaceNumber, AlternateSetting, -1, -1, -1);
    if (!pIfDr)
    {
        AssertMsgFailed((__FUNCTION__": invalid interface %d or alternate setting %d\n", InterfaceNumber, AlternateSetting));
        return STATUS_UNSUCCESSFUL;
    }

    USHORT uUrbSize = GET_SELECT_INTERFACE_REQUEST_SIZE(pIfDr->bNumEndpoints);
    ULONG uTotalIfaceInfoLength = GET_USBD_INTERFACE_SIZE(pIfDr->bNumEndpoints);
    NTSTATUS Status = STATUS_SUCCESS;
    PURB pUrb = NemuUsbToolUrbAllocZ(0, uUrbSize);
    if (!pUrb)
    {
        AssertMsgFailed((__FUNCTION__": NemuUsbToolUrbAlloc failed\n"));
        return STATUS_NO_MEMORY;
    }

    /*
     * Free old interface and pipe info, allocate new again
     */
    if (pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo)
    {
        /* Clear pipes associated with the interface, else Windows may hang. */
        for(ULONG i = 0; i < pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo->NumberOfPipes; i++)
        {
            NemuUsbToolPipeClear(pDevExt->pLowerDO, pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo->Pipes[i].PipeHandle, FALSE);
        }
        nemuUsbMemFree(pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo);
    }

    if (pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo)
    {
        nemuUsbMemFree(pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo);
    }

    pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo = (PUSBD_INTERFACE_INFORMATION)nemuUsbMemAlloc(uTotalIfaceInfoLength);
    if (pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo)
    {
        if (pIfDr->bNumEndpoints > 0)
        {
            pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo = (NEMUUSB_PIPE_INFO*)nemuUsbMemAlloc(pIfDr->bNumEndpoints * sizeof(NEMUUSB_PIPE_INFO));
            if (!pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo)
            {
                AssertMsgFailed(("NemuUSBSetInterface: ExAllocatePool failed!\n"));
                Status = STATUS_NO_MEMORY;
            }
        }
        else
        {
            pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo = NULL;
        }

        if (NT_SUCCESS(Status))
        {
            UsbBuildSelectInterfaceRequest(pUrb, uUrbSize, pDevExt->Rt.hConfiguration, InterfaceNumber, AlternateSetting);
            pUrb->UrbSelectInterface.Interface.Length = GET_USBD_INTERFACE_SIZE(pIfDr->bNumEndpoints);

            Status = NemuUsbToolUrbPost(pDevExt->pLowerDO, pUrb, RT_INDEFINITE_WAIT);
            if (NT_SUCCESS(Status) && USBD_SUCCESS(pUrb->UrbHeader.Status))
            {
                USBD_INTERFACE_INFORMATION *pIfInfo = &pUrb->UrbSelectInterface.Interface;
                memcpy(pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pInterfaceInfo, pIfInfo, GET_USBD_INTERFACE_SIZE(pIfDr->bNumEndpoints));

                Assert(pIfInfo->NumberOfPipes == pIfDr->bNumEndpoints);
                for(ULONG i = 0; i < pIfInfo->NumberOfPipes; i++)
                {
                    pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo[i].EndpointAddress = pIfInfo->Pipes[i].EndpointAddress;
                    pDevExt->Rt.pVBIfaceInfo[InterfaceNumber].pPipeInfo[i].NextScheduledFrame = 0;
                }
            }
            else
            {
                AssertMsgFailed((__FUNCTION__": NemuUsbToolUrbPost failed Status (0x%x) usb Status (0x%x)\n", Status, pUrb->UrbHeader.Status));
            }
        }

    }
    else
    {
        AssertMsgFailed(("NemuUSBSetInterface: ExAllocatePool failed!\n"));
        Status = STATUS_NO_MEMORY;
    }

    NemuUsbToolUrbFree(pUrb);

    return Status;
}

static NTSTATUS nemuUsbRtDispatchUsbSelectInterface(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_SELECT_INTERFACE pIf = (PUSBSUP_SELECT_INTERFACE)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!nemuUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (  !pIf
            || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pIf)
            || pSl->Parameters.DeviceIoControl.OutputBufferLength != 0)
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = nemuUsbRtSetInterface(pDevExt, pIf->bInterfaceNumber, pIf->bAlternateSetting);
    } while (0);

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static HANDLE nemuUsbRtGetPipeHandle(PNEMUUSBDEV_EXT pDevExt, uint32_t EndPointAddress)
{
    for (ULONG i = 0; i < pDevExt->Rt.uNumInterfaces; i++)
    {
        for (ULONG j = 0; j < pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->NumberOfPipes; j++)
        {
            /* Note that bit 7 determines pipe direction, but is still significant
             * because endpoints may be numbered like 0x01, 0x81, 0x02, 0x82 etc.
             */
            if (pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].EndpointAddress == EndPointAddress)
                return pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->Pipes[j].PipeHandle;
        }
    }
    return 0;
}

static NEMUUSB_PIPE_INFO* nemuUsbRtGetPipeInfo(PNEMUUSBDEV_EXT pDevExt, uint32_t EndPointAddress)
{
    for (ULONG i = 0; i < pDevExt->Rt.uNumInterfaces; i++)
    {
        for (ULONG j = 0; j < pDevExt->Rt.pVBIfaceInfo[i].pInterfaceInfo->NumberOfPipes; j++)
        {
            if (pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo[j].EndpointAddress == EndPointAddress)
                return &pDevExt->Rt.pVBIfaceInfo[i].pPipeInfo[j];
        }
    }
    return NULL;
}



static NTSTATUS nemuUsbRtClearEndpoint(PNEMUUSBDEV_EXT pDevExt, uint32_t EndPointAddress, bool fReset)
{
    NTSTATUS Status = NemuUsbToolPipeClear(pDevExt->pLowerDO, nemuUsbRtGetPipeHandle(pDevExt, EndPointAddress), fReset);
    if (!NT_SUCCESS(Status))
    {
        AssertMsgFailed((__FUNCTION__": NemuUsbToolPipeClear failed Status (0x%x)\n", Status));
    }

    return Status;
}

static NTSTATUS nemuUsbRtDispatchUsbClearEndpoint(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_CLEAR_ENDPOINT pCe = (PUSBSUP_CLEAR_ENDPOINT)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!nemuUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (   !pCe
             || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pCe)
             || pSl->Parameters.DeviceIoControl.OutputBufferLength != 0)
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = nemuUsbRtClearEndpoint(pDevExt, pCe->bEndpoint, TRUE);
    } while (0);

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbRtDispatchUsbAbortEndpoint(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_CLEAR_ENDPOINT pCe = (PUSBSUP_CLEAR_ENDPOINT)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!nemuUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (  !pCe
            || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pCe)
            || pSl->Parameters.DeviceIoControl.OutputBufferLength != 0)
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = nemuUsbRtClearEndpoint(pDevExt, pCe->bEndpoint, FALSE);
    } while (0);

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbRtUrbSendCompletion(PDEVICE_OBJECT pDevObj, IRP *pIrp, void *pvContext)
{
    if (!pvContext)
    {
        AssertMsgFailed((__FUNCTION__":  context is NULL\n"));
        pIrp->IoStatus.Information = 0;
        return STATUS_CONTINUE_COMPLETION;
    }

    PNEMUUSB_URB_CONTEXT pContext = (PNEMUUSB_URB_CONTEXT)pvContext;

    if (pContext->ulMagic != NEMUUSB_MAGIC)
    {
        AssertMsgFailed((__FUNCTION__": Invalid context magic\n"));
        pIrp->IoStatus.Information = 0;
        return STATUS_CONTINUE_COMPLETION;
    }

    PURB pUrb = pContext->pUrb;
    PMDL pMdlBuf = pContext->pMdlBuf;
    PUSBSUP_URB pUrbInfo = (PUSBSUP_URB)pContext->pOut;
    PNEMUUSBDEV_EXT pDevExt = pContext->pDevExt;

    if (!pUrb || !pMdlBuf || !pUrbInfo || !pDevExt)
    {
        AssertMsgFailed((__FUNCTION__": Invalid args\n"));
        if (pDevExt)
            nemuUsbDdiStateRelease(pDevExt);
        pIrp->IoStatus.Information = 0;
        return STATUS_CONTINUE_COMPLETION;
    }

    NTSTATUS Status = pIrp->IoStatus.Status;
    if (Status == STATUS_SUCCESS)
    {
        switch(pUrb->UrbHeader.Status)
        {
            case USBD_STATUS_CRC:
                pUrbInfo->error = USBSUP_XFER_CRC;
                break;
            case USBD_STATUS_SUCCESS:
                pUrbInfo->error = USBSUP_XFER_OK;
                break;
            case USBD_STATUS_STALL_PID:
                pUrbInfo->error = USBSUP_XFER_STALL;
                break;
            case USBD_STATUS_INVALID_URB_FUNCTION:
            case USBD_STATUS_INVALID_PARAMETER:
                AssertMsgFailed((__FUNCTION__": sw error, urb Status (0x%x)\n", pUrb->UrbHeader.Status));
            case USBD_STATUS_DEV_NOT_RESPONDING:
            default:
                pUrbInfo->error = USBSUP_XFER_DNR;
            break;
        }

        switch(pContext->ulTransferType)
        {
            case USBSUP_TRANSFER_TYPE_CTRL:
            case USBSUP_TRANSFER_TYPE_MSG:
                pUrbInfo->len = pUrb->UrbControlTransfer.TransferBufferLength;
                if (pContext->ulTransferType == USBSUP_TRANSFER_TYPE_MSG)
                {
                    /* QUSB_TRANSFER_TYPE_MSG is a control transfer, but it is special
                     * the first 8 bytes of the buffer is the setup packet so the real
                     * data length is therefore urb->len - 8
                     */
                    pUrbInfo->len += sizeof (pUrb->UrbControlTransfer.SetupPacket);
                }
                break;
            case USBSUP_TRANSFER_TYPE_ISOC:
                pUrbInfo->len = pUrb->UrbIsochronousTransfer.TransferBufferLength;
                break;
            case USBSUP_TRANSFER_TYPE_BULK:
            case USBSUP_TRANSFER_TYPE_INTR:
                if (pUrbInfo->dir == USBSUP_DIRECTION_IN && pUrbInfo->error == USBSUP_XFER_OK
                        && !(pUrbInfo->flags & USBSUP_FLAG_SHORT_OK)
                        && pUrbInfo->len > pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength
                        )
                {
                    /* If we don't use the USBD_SHORT_TRANSFER_OK flag, the returned buffer lengths are
                     * wrong for short transfers (always a multiple of max packet size?). So we just figure
                     * out if this was a data underrun on our own.
                     */
                    pUrbInfo->error = USBSUP_XFER_UNDERRUN;
                }
                pUrbInfo->len = pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
                break;
            default:
                break;
        }
    }
    else
    {
        pUrbInfo->len = 0;

        Log((__FUNCTION__": URB failed Status (0x%x) urb Status (0x%x)\n", Status, pUrb->UrbHeader.Status));
#ifdef DEBUG
        switch(pContext->ulTransferType)
        {
            case USBSUP_TRANSFER_TYPE_CTRL:
            case USBSUP_TRANSFER_TYPE_MSG:
                LogRel(("Ctrl/Msg length=%d\n", pUrb->UrbControlTransfer.TransferBufferLength));
                break;
            case USBSUP_TRANSFER_TYPE_ISOC:
                LogRel(("ISOC length=%d\n", pUrb->UrbIsochronousTransfer.TransferBufferLength));
                break;
            case USBSUP_TRANSFER_TYPE_BULK:
            case USBSUP_TRANSFER_TYPE_INTR:
                LogRel(("BULK/INTR length=%d\n", pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength));
                break;
        }
#endif
        switch(pUrb->UrbHeader.Status)
        {
            case USBD_STATUS_CRC:
                pUrbInfo->error = USBSUP_XFER_CRC;
                Status = STATUS_SUCCESS;
                break;
            case USBD_STATUS_STALL_PID:
                pUrbInfo->error = USBSUP_XFER_STALL;
                Status = STATUS_SUCCESS;
                break;
            case USBD_STATUS_DEV_NOT_RESPONDING:
            case USBD_STATUS_DEVICE_GONE:
                pUrbInfo->error = USBSUP_XFER_DNR;
                Status = STATUS_SUCCESS;
                break;
            case ((USBD_STATUS)0xC0010000L): // USBD_STATUS_CANCELED - too bad usbdi.h and usb.h aren't consistent!
                // TODO: What the heck are we really supposed to do here?
                pUrbInfo->error = USBSUP_XFER_STALL;
                Status = STATUS_SUCCESS;
                break;
            case USBD_STATUS_BAD_START_FRAME:   // This one really shouldn't happen
            case USBD_STATUS_ISOCH_REQUEST_FAILED:
                pUrbInfo->error = USBSUP_XFER_NAC;
                Status = STATUS_SUCCESS;
                break;
            default:
                AssertMsgFailed((__FUNCTION__": err Status (0x%x) (0x%x)\n", Status, pUrb->UrbHeader.Status));
                pUrbInfo->error = USBSUP_XFER_DNR;
                Status = STATUS_SUCCESS;
                break;
        }
    }
    // For isochronous transfers, always update the individual packets
    if (pContext->ulTransferType == USBSUP_TRANSFER_TYPE_ISOC)
    {
        Assert(pUrbInfo->numIsoPkts == pUrb->UrbIsochronousTransfer.NumberOfPackets);
        for (ULONG i = 0; i < pUrbInfo->numIsoPkts; ++i)
        {
            Assert(pUrbInfo->aIsoPkts[i].off == pUrb->UrbIsochronousTransfer.IsoPacket[i].Offset);
            pUrbInfo->aIsoPkts[i].cb = (uint16_t)pUrb->UrbIsochronousTransfer.IsoPacket[i].Length;
            switch (pUrb->UrbIsochronousTransfer.IsoPacket[i].Status)
            {
                case USBD_STATUS_SUCCESS:
                    pUrbInfo->aIsoPkts[i].stat = USBSUP_XFER_OK;
                    break;
                case USBD_STATUS_NOT_ACCESSED:
                    pUrbInfo->aIsoPkts[i].stat = USBSUP_XFER_NAC;
                    break;
                default:
                    pUrbInfo->aIsoPkts[i].stat = USBSUP_XFER_STALL;
                    break;
            }
        }
    }

    MmUnlockPages(pMdlBuf);
    IoFreeMdl(pMdlBuf);

    nemuUsbMemFree(pContext);

    nemuUsbDdiStateRelease(pDevExt);

    Assert(pIrp->IoStatus.Status != STATUS_IO_TIMEOUT);
    pIrp->IoStatus.Information = sizeof(*pUrbInfo);
    pIrp->IoStatus.Status = Status;
    return STATUS_CONTINUE_COMPLETION;
}

static NTSTATUS nemuUsbRtUrbSend(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp, PUSBSUP_URB pUrbInfo)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PNEMUUSB_URB_CONTEXT pContext = NULL;
    PMDL pMdlBuf = NULL;
    ULONG cbUrb;

    Assert(pUrbInfo);
    if (pUrbInfo->type == USBSUP_TRANSFER_TYPE_ISOC)
    {
        Assert(pUrbInfo->numIsoPkts <= 8);
        cbUrb = GET_ISO_URB_SIZE(pUrbInfo->numIsoPkts);
    }
    else
        cbUrb = sizeof (URB);

    do
    {
        pContext = (PNEMUUSB_URB_CONTEXT)nemuUsbMemAllocZ(cbUrb + sizeof (NEMUUSB_URB_CONTEXT));
        if (!pContext)
        {
            AssertMsgFailed((__FUNCTION__": nemuUsbMemAlloc failed\n"));
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        PURB pUrb = (PURB)(pContext + 1);
        HANDLE hPipe = NULL;
        if (pUrbInfo->ep)
        {
            hPipe = nemuUsbRtGetPipeHandle(pDevExt, pUrbInfo->ep | ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? 0x80 : 0x00));
            if (!hPipe)
            {
                AssertMsgFailed((__FUNCTION__": nemuUsbRtGetPipeHandle failed for endpoint (0x%x)\n", pUrbInfo->ep));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        pMdlBuf = IoAllocateMdl(pUrbInfo->buf, (ULONG)pUrbInfo->len, FALSE, FALSE, NULL);
        if (!pMdlBuf)
        {
            AssertMsgFailed((__FUNCTION__": IoAllocateMdl failed for buffer (0x%p) length (%d)\n", pUrbInfo->buf, pUrbInfo->len));
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        __try
        {
            MmProbeAndLockPages(pMdlBuf, KernelMode, IoModifyAccess);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = GetExceptionCode();
            IoFreeMdl(pMdlBuf);
            pMdlBuf = NULL;
            AssertMsgFailed((__FUNCTION__": Exception Code (0x%x)\n", Status));
            break;
        }

        /* For some reason, passing a MDL in the URB does not work reliably. Notably
         * the iPhone when used with iTunes fails.
         */
        PVOID pBuffer = MmGetSystemAddressForMdlSafe(pMdlBuf, NormalPagePriority);
        if (!pBuffer)
        {
            AssertMsgFailed((__FUNCTION__": MmGetSystemAddressForMdlSafe failed\n"));
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        switch (pUrbInfo->type)
        {
            case USBSUP_TRANSFER_TYPE_CTRL:
            case USBSUP_TRANSFER_TYPE_MSG:
            {
                pUrb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
                pUrb->UrbHeader.Length = sizeof (struct _URB_CONTROL_TRANSFER);
                pUrb->UrbControlTransfer.PipeHandle = hPipe;
                pUrb->UrbControlTransfer.TransferBufferLength = (ULONG)pUrbInfo->len;
                pUrb->UrbControlTransfer.TransferFlags = ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);
                pUrb->UrbControlTransfer.UrbLink = 0;

                if (!hPipe)
                    pUrb->UrbControlTransfer.TransferFlags |= USBD_DEFAULT_PIPE_TRANSFER;

                if (pUrbInfo->type == USBSUP_TRANSFER_TYPE_MSG)
                {
                   /* QUSB_TRANSFER_TYPE_MSG is a control transfer, but it is special
                    * the first 8 bytes of the buffer is the setup packet so the real
                    * data length is therefore pUrb->len - 8
                    */
                    PNEMUUSB_SETUP pSetup = (PNEMUUSB_SETUP)pUrb->UrbControlTransfer.SetupPacket;
                    memcpy(pUrb->UrbControlTransfer.SetupPacket, pBuffer, min(sizeof (pUrb->UrbControlTransfer.SetupPacket), pUrbInfo->len));

                    if (pUrb->UrbControlTransfer.TransferBufferLength <= sizeof (pUrb->UrbControlTransfer.SetupPacket))
                        pUrb->UrbControlTransfer.TransferBufferLength = 0;
                    else
                        pUrb->UrbControlTransfer.TransferBufferLength -= sizeof (pUrb->UrbControlTransfer.SetupPacket);

                    pUrb->UrbControlTransfer.TransferBuffer = (uint8_t *)pBuffer + sizeof(pUrb->UrbControlTransfer.SetupPacket);
                    pUrb->UrbControlTransfer.TransferBufferMDL = 0;
                    pUrb->UrbControlTransfer.TransferFlags |= USBD_SHORT_TRANSFER_OK;
                }
                else
                {
                    pUrb->UrbControlTransfer.TransferBuffer = 0;
                    pUrb->UrbControlTransfer.TransferBufferMDL = pMdlBuf;
                }
                break;
            }
            case USBSUP_TRANSFER_TYPE_ISOC:
            {
                Assert(hPipe);
                NEMUUSB_PIPE_INFO *pPipeInfo = nemuUsbRtGetPipeInfo(pDevExt, pUrbInfo->ep | ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? 0x80 : 0x00));
                if (pPipeInfo == NULL)
                {
                    /* Can happen if the isoc request comes in too early or late. */
                    AssertMsgFailed((__FUNCTION__": pPipeInfo not found\n"));
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                pUrb->UrbHeader.Function = URB_FUNCTION_ISOCH_TRANSFER;
                pUrb->UrbHeader.Length = (USHORT)cbUrb;
                pUrb->UrbIsochronousTransfer.PipeHandle = hPipe;
                pUrb->UrbIsochronousTransfer.TransferBufferLength = (ULONG)pUrbInfo->len;
                pUrb->UrbIsochronousTransfer.TransferBufferMDL = 0;
                pUrb->UrbIsochronousTransfer.TransferBuffer = pBuffer;
                pUrb->UrbIsochronousTransfer.TransferFlags = ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);
                pUrb->UrbIsochronousTransfer.TransferFlags |= USBD_SHORT_TRANSFER_OK;  // May be implied already
                pUrb->UrbIsochronousTransfer.NumberOfPackets = pUrbInfo->numIsoPkts;
                pUrb->UrbIsochronousTransfer.ErrorCount = 0;
                pUrb->UrbIsochronousTransfer.UrbLink = 0;

                Assert(pUrbInfo->numIsoPkts == pUrb->UrbIsochronousTransfer.NumberOfPackets);
                for (ULONG i = 0; i < pUrbInfo->numIsoPkts; ++i)
                {
                    pUrb->UrbIsochronousTransfer.IsoPacket[i].Offset = pUrbInfo->aIsoPkts[i].off;
                    pUrb->UrbIsochronousTransfer.IsoPacket[i].Length = pUrbInfo->aIsoPkts[i].cb;
                }

                /* We have to schedule the URBs ourselves. There is an ASAP flag but
                 * that can only be reliably used after pipe creation/reset, ie. it's
                 * almost completely useless.
                 */
                ULONG iFrame, iStartFrame;
                NemuUsbToolCurrentFrame(pDevExt->pLowerDO, pIrp, &iFrame);
                iFrame += 2;
                iStartFrame = pPipeInfo->NextScheduledFrame;
                if ((iFrame < iStartFrame) || (iStartFrame > iFrame + 512))
                    iFrame = iStartFrame;
                /* For full-speed devices, there must be one transfer per frame (Windows USB
                 * stack requirement), but URBs can contain multiple packets. For high-speed or
                 * faster transfers, we expect one URB per frame, regardless of the interval.
                 */
                if (pDevExt->Rt.devdescr->bcdUSB < 0x300 && !pDevExt->Rt.fIsHighSpeed)
                    pPipeInfo->NextScheduledFrame = iFrame + pUrbInfo->numIsoPkts;
                else
                    pPipeInfo->NextScheduledFrame = iFrame + 1;
                pUrb->UrbIsochronousTransfer.StartFrame = iFrame;
                break;
            }
            case USBSUP_TRANSFER_TYPE_BULK:
            case USBSUP_TRANSFER_TYPE_INTR:
            {
                Assert(pUrbInfo->dir != USBSUP_DIRECTION_SETUP);
                Assert(pUrbInfo->dir == USBSUP_DIRECTION_IN || pUrbInfo->type == USBSUP_TRANSFER_TYPE_BULK);
                Assert(hPipe);

                pUrb->UrbHeader.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
                pUrb->UrbHeader.Length = sizeof (struct _URB_BULK_OR_INTERRUPT_TRANSFER);
                pUrb->UrbBulkOrInterruptTransfer.PipeHandle = hPipe;
                pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength = (ULONG)pUrbInfo->len;
                pUrb->UrbBulkOrInterruptTransfer.TransferBufferMDL = 0;
                pUrb->UrbBulkOrInterruptTransfer.TransferBuffer = pBuffer;
                pUrb->UrbBulkOrInterruptTransfer.TransferFlags = ((pUrbInfo->dir == USBSUP_DIRECTION_IN) ? USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT);

                if (pUrb->UrbBulkOrInterruptTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)
                    pUrb->UrbBulkOrInterruptTransfer.TransferFlags |= (USBD_SHORT_TRANSFER_OK);

                pUrb->UrbBulkOrInterruptTransfer.UrbLink = 0;
                break;
            }
            default:
            {
                AssertFailed();
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        if (!NT_SUCCESS(Status))
        {
            break;
        }

        pContext->pDevExt = pDevExt;
        pContext->pMdlBuf = pMdlBuf;
        pContext->pUrb = pUrb;
        pContext->pOut = pUrbInfo;
        pContext->ulTransferType = pUrbInfo->type;
        pContext->ulMagic = NEMUUSB_MAGIC;

        PIO_STACK_LOCATION pSl = IoGetNextIrpStackLocation(pIrp);
        pSl->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        pSl->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
        pSl->Parameters.Others.Argument1 = pUrb;
        pSl->Parameters.Others.Argument2 = NULL;

        IoSetCompletionRoutine(pIrp, nemuUsbRtUrbSendCompletion, pContext, TRUE, TRUE, TRUE);
        IoMarkIrpPending(pIrp);
        Status = IoCallDriver(pDevExt->pLowerDO, pIrp);
        AssertMsg(NT_SUCCESS(Status), (__FUNCTION__": IoCallDriver failed Status (0x%x)\n", Status));
        return STATUS_PENDING;
    } while (0);

    Assert(!NT_SUCCESS(Status));

    if (pMdlBuf)
    {
        MmUnlockPages(pMdlBuf);
        IoFreeMdl(pMdlBuf);
    }

    if (pContext)
        nemuUsbMemFree(pContext);

    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbRtDispatchSendUrb(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    PUSBSUP_URB pUrbInfo = (PUSBSUP_URB)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status;

    do
    {
        if (!pFObj)
        {
            AssertFailed();
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (!nemuUsbRtCtxIsOwner(pDevExt, pFObj))
        {
            AssertFailed();
            Status = STATUS_ACCESS_DENIED;
            break;
        }

        if (  !pUrbInfo
            || pSl->Parameters.DeviceIoControl.InputBufferLength != sizeof (*pUrbInfo)
            || pSl->Parameters.DeviceIoControl.OutputBufferLength != sizeof (*pUrbInfo))
        {
            AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        return nemuUsbRtUrbSend(pDevExt, pIrp, pUrbInfo);
    } while (0);

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbRtDispatchIsOperational(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    NemuDrvToolIoComplete(pIrp, STATUS_SUCCESS, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return STATUS_SUCCESS;
}

static NTSTATUS nemuUsbRtDispatchGetVersion(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PUSBSUP_VERSION pVer= (PUSBSUP_VERSION)pIrp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;

    if (pVer && pSl->Parameters.DeviceIoControl.InputBufferLength == 0
             && pSl->Parameters.DeviceIoControl.OutputBufferLength == sizeof (*pVer))
    {
        pVer->u32Major = USBDRV_MAJOR_VERSION;
        pVer->u32Minor = USBDRV_MINOR_VERSION;
    }
    else
    {
        AssertMsgFailed((__FUNCTION__": STATUS_INVALID_PARAMETER\n"));
        Status = STATUS_INVALID_PARAMETER;
    }

    Assert(Status != STATUS_PENDING);
    NemuDrvToolIoComplete(pIrp, Status, sizeof (*pVer));
    nemuUsbDdiStateRelease(pDevExt);
    return Status;
}

static NTSTATUS nemuUsbRtDispatchDefault(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    NemuDrvToolIoComplete(pIrp, STATUS_INVALID_DEVICE_REQUEST, 0);
    nemuUsbDdiStateRelease(pDevExt);
    return STATUS_INVALID_DEVICE_REQUEST;
}

DECLHIDDEN(NTSTATUS) nemuUsbRtCreate(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    if (!pFObj)
    {
        AssertFailed();
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

DECLHIDDEN(NTSTATUS) nemuUsbRtClose(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT pFObj = pSl->FileObject;
    Assert(pFObj);

    nemuUsbRtCtxReleaseOwner(pDevExt, pFObj);

    return STATUS_SUCCESS;
}

DECLHIDDEN(NTSTATUS) nemuUsbRtDispatch(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp)
{
    PIO_STACK_LOCATION pSl = IoGetCurrentIrpStackLocation(pIrp);
    switch (pSl->Parameters.DeviceIoControl.IoControlCode)
    {
        case SUPUSB_IOCTL_USB_CLAIM_DEVICE:
        {
            return nemuUsbRtDispatchClaimDevice(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_USB_RELEASE_DEVICE:
        {
            return nemuUsbRtDispatchReleaseDevice(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_GET_DEVICE:
        {
            return nemuUsbRtDispatchGetDevice(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_USB_RESET:
        {
            return nemuUsbRtDispatchUsbReset(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_USB_SET_CONFIG:
        {
            return nemuUsbRtDispatchUsbSetConfig(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_USB_SELECT_INTERFACE:
        {
            return nemuUsbRtDispatchUsbSelectInterface(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_USB_CLEAR_ENDPOINT:
        {
            return nemuUsbRtDispatchUsbClearEndpoint(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_USB_ABORT_ENDPOINT:
        {
            return nemuUsbRtDispatchUsbAbortEndpoint(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_SEND_URB:
        {
            return nemuUsbRtDispatchSendUrb(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_IS_OPERATIONAL:
        {
            return nemuUsbRtDispatchIsOperational(pDevExt, pIrp);
        }
        case SUPUSB_IOCTL_GET_VERSION:
        {
            return nemuUsbRtDispatchGetVersion(pDevExt, pIrp);
        }
        default:
        {
            return nemuUsbRtDispatchDefault(pDevExt, pIrp);
        }
    }
}
