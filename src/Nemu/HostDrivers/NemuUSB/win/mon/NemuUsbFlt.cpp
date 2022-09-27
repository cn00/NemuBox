/* $Id: NemuUsbFlt.cpp $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "NemuUsbMon.h"
#include "../cmn/NemuUsbTool.h"

#include <Nemu/cdefs.h>
#include <Nemu/types.h>
#include <iprt/process.h>
#include <iprt/assert.h>
#include <Nemu/err.h>
//#include <Nemu/sup.h>

#include <iprt/assert.h>
#include <stdio.h>

#pragma warning(disable : 4200)
#include "usbdi.h"
#pragma warning(default : 4200)
#include "usbdlib.h"
#include "NemuUSBFilterMgr.h"
#include <Nemu/usblib.h>
#include <devguid.h>

/*
 * Note: Must match the VID & PID in the USB driver .inf file!!
 */
/*
  BusQueryDeviceID USB\Vid_80EE&Pid_CAFE
  BusQueryInstanceID 2
  BusQueryHardwareIDs USB\Vid_80EE&Pid_CAFE&Rev_0100
  BusQueryHardwareIDs USB\Vid_80EE&Pid_CAFE
  BusQueryCompatibleIDs USB\Class_ff&SubClass_00&Prot_00
  BusQueryCompatibleIDs USB\Class_ff&SubClass_00
  BusQueryCompatibleIDs USB\Class_ff
*/

#define szBusQueryDeviceId                  L"USB\\Vid_80EE&Pid_CAFE"
#define szBusQueryHardwareIDs               L"USB\\Vid_80EE&Pid_CAFE&Rev_0100\0USB\\Vid_80EE&Pid_CAFE\0\0"
#define szBusQueryCompatibleIDs             L"USB\\Class_ff&SubClass_00&Prot_00\0USB\\Class_ff&SubClass_00\0USB\\Class_ff\0\0"

#define szDeviceTextDescription             L"VirtualBox USB"

/* Possible USB bus driver names. */
static LPWSTR lpszStandardControllerName[1] =
{
    L"\\Driver\\usbhub",
};

/*
 * state transitions:
 *
 *           (we are not filtering this device )
 * ADDED --> UNCAPTURED ------------------------------->-
 *       |                                              |
 *       |   (we are filtering this device,             | (the device is being
 *       |    waiting for our device driver             |  re-plugged to perform
 *       |    to pick it up)                            |  capture-uncapture transition)
 *       |-> CAPTURING -------------------------------->|---> REPLUGGING -----
 *            ^  |    (device driver picked             |                    |
 *            |  |     up the device)                   | (remove cased      |  (device is removed
 *            |  ->---> CAPTURED ---------------------->|  by "real" removal |   the device info is removed form the list)
 *            |            |                            |------------------->->--> REMOVED
 *            |            |                            |
 *            |-----------<->---> USED_BY_GUEST ------->|
 *            |                         |
 *            |------------------------<-
 *
 * NOTE: the order of enums DOES MATTER!!
 * Do not blindly modify!! as the code assumes the state is ordered this way.
 */
typedef enum
{
    NEMUUSBFLT_DEVSTATE_UNKNOWN = 0,
    NEMUUSBFLT_DEVSTATE_REMOVED,
    NEMUUSBFLT_DEVSTATE_REPLUGGING,
    NEMUUSBFLT_DEVSTATE_ADDED,
    NEMUUSBFLT_DEVSTATE_UNCAPTURED,
    NEMUUSBFLT_DEVSTATE_CAPTURING,
    NEMUUSBFLT_DEVSTATE_CAPTURED,
    NEMUUSBFLT_DEVSTATE_USED_BY_GUEST,
    NEMUUSBFLT_DEVSTATE_32BIT_HACK = 0x7fffffff
} NEMUUSBFLT_DEVSTATE;

typedef struct NEMUUSBFLT_DEVICE
{
    LIST_ENTRY      GlobalLe;
    /* auxiliary list to be used for gathering devices to be re-plugged
     * only thread that puts the device to the REPLUGGING state can use this list */
    LIST_ENTRY      RepluggingLe;
    /* Owning session. Each matched device has an owning session. */
    struct NEMUUSBFLTCTX *pOwner;
    /* filter id - if NULL AND device has an owner - the filter is destroyed */
    uintptr_t uFltId;
    /* true iff device is filtered with a one-shot filter */
    bool fIsFilterOneShot;
    /* The device state. If the non-owner session is requesting the state while the device is grabbed,
     * the USBDEVICESTATE_USED_BY_HOST is returned. */
    NEMUUSBFLT_DEVSTATE  enmState;
    volatile uint32_t cRefs;
    PDEVICE_OBJECT  Pdo;
    uint16_t        idVendor;
    uint16_t        idProduct;
    uint16_t        bcdDevice;
    uint8_t         bClass;
    uint8_t         bSubClass;
    uint8_t         bProtocol;
    char            szSerial[MAX_USB_SERIAL_STRING];
    char            szMfgName[MAX_USB_SERIAL_STRING];
    char            szProduct[MAX_USB_SERIAL_STRING];
#if 0
    char            szDrvKeyName[512];
    BOOLEAN         fHighSpeed;
#endif
} NEMUUSBFLT_DEVICE, *PNEMUUSBFLT_DEVICE;

#define PNEMUUSBFLT_DEVICE_FROM_LE(_pLe) ( (PNEMUUSBFLT_DEVICE)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(NEMUUSBFLT_DEVICE, GlobalLe) ) )
#define PNEMUUSBFLT_DEVICE_FROM_REPLUGGINGLE(_pLe)  ( (PNEMUUSBFLT_DEVICE)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(NEMUUSBFLT_DEVICE, RepluggingLe) ) )
#define PNEMUUSBFLTCTX_FROM_LE(_pLe) ( (PNEMUUSBFLTCTX)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(NEMUUSBFLTCTX, ListEntry) ) )

typedef struct NEMUUSBFLT_LOCK
{
    KSPIN_LOCK Lock;
    KIRQL OldIrql;
} NEMUUSBFLT_LOCK, *PNEMUUSBFLT_LOCK;

#define NEMUUSBFLT_LOCK_INIT() \
    KeInitializeSpinLock(&g_NemuUsbFltGlobals.Lock.Lock)
#define NEMUUSBFLT_LOCK_TERM() do { } while (0)
#define NEMUUSBFLT_LOCK_ACQUIRE() \
    KeAcquireSpinLock(&g_NemuUsbFltGlobals.Lock.Lock, &g_NemuUsbFltGlobals.Lock.OldIrql);
#define NEMUUSBFLT_LOCK_RELEASE() \
    KeReleaseSpinLock(&g_NemuUsbFltGlobals.Lock.Lock, g_NemuUsbFltGlobals.Lock.OldIrql);


typedef struct NEMUUSBFLT_BLDEV
{
    LIST_ENTRY ListEntry;
    uint16_t   idVendor;
    uint16_t   idProduct;
    uint16_t   bcdDevice;
} NEMUUSBFLT_BLDEV, *PNEMUUSBFLT_BLDEV;

#define PNEMUUSBFLT_BLDEV_FROM_LE(_pLe) ( (PNEMUUSBFLT_BLDEV)( ((uint8_t*)(_pLe)) - RT_OFFSETOF(NEMUUSBFLT_BLDEV, ListEntry) ) )

typedef struct NEMUUSBFLTGLOBALS
{
    LIST_ENTRY DeviceList;
    LIST_ENTRY ContextList;
    /* devices known to misbehave */
    LIST_ENTRY BlackDeviceList;
    NEMUUSBFLT_LOCK Lock;
} NEMUUSBFLTGLOBALS, *PNEMUUSBFLTGLOBALS;
static NEMUUSBFLTGLOBALS g_NemuUsbFltGlobals;

static bool nemuUsbFltBlDevMatchLocked(uint16_t idVendor, uint16_t idProduct, uint16_t bcdDevice)
{
    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.BlackDeviceList.Flink;
            pEntry != &g_NemuUsbFltGlobals.BlackDeviceList;
            pEntry = pEntry->Flink)
    {
        PNEMUUSBFLT_BLDEV pDev = PNEMUUSBFLT_BLDEV_FROM_LE(pEntry);
        if (pDev->idVendor != idVendor)
            continue;
        if (pDev->idProduct != idProduct)
            continue;
        if (pDev->bcdDevice != bcdDevice)
            continue;

        return true;
    }
    return false;
}

static NTSTATUS nemuUsbFltBlDevAddLocked(uint16_t idVendor, uint16_t idProduct, uint16_t bcdDevice)
{
    if (nemuUsbFltBlDevMatchLocked(idVendor, idProduct, bcdDevice))
        return STATUS_SUCCESS;
    PNEMUUSBFLT_BLDEV pDev = (PNEMUUSBFLT_BLDEV)NemuUsbMonMemAllocZ(sizeof (*pDev));
    if (!pDev)
    {
        AssertFailed();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pDev->idVendor = idVendor;
    pDev->idProduct = idProduct;
    pDev->bcdDevice = bcdDevice;
    InsertHeadList(&g_NemuUsbFltGlobals.BlackDeviceList, &pDev->ListEntry);
    return STATUS_SUCCESS;
}

static void nemuUsbFltBlDevClearLocked()
{
    PLIST_ENTRY pNext;
    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.BlackDeviceList.Flink;
            pEntry != &g_NemuUsbFltGlobals.BlackDeviceList;
            pEntry = pNext)
    {
        pNext = pEntry->Flink;
        NemuUsbMonMemFree(pEntry);
    }
}

static void nemuUsbFltBlDevPopulateWithKnownLocked()
{
    /* this one halts when trying to get string descriptors from it */
    nemuUsbFltBlDevAddLocked(0x5ac, 0x921c, 0x115);
}


DECLINLINE(void) nemuUsbFltDevRetain(PNEMUUSBFLT_DEVICE pDevice)
{
    Assert(pDevice->cRefs);
    ASMAtomicIncU32(&pDevice->cRefs);
}

static void nemuUsbFltDevDestroy(PNEMUUSBFLT_DEVICE pDevice)
{
    Assert(!pDevice->cRefs);
    Assert(pDevice->enmState == NEMUUSBFLT_DEVSTATE_REMOVED);
    NemuUsbMonMemFree(pDevice);
}

DECLINLINE(void) nemuUsbFltDevRelease(PNEMUUSBFLT_DEVICE pDevice)
{
    uint32_t cRefs = ASMAtomicDecU32(&pDevice->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    if (!cRefs)
    {
        nemuUsbFltDevDestroy(pDevice);
    }
}

static void nemuUsbFltDevOwnerSetLocked(PNEMUUSBFLT_DEVICE pDevice, PNEMUUSBFLTCTX pContext, uintptr_t uFltId, bool fIsOneShot)
{
    ASSERT_WARN(!pDevice->pOwner, ("device 0x%p has an owner(0x%p)", pDevice, pDevice->pOwner));
    ++pContext->cActiveFilters;
    pDevice->pOwner = pContext;
    pDevice->uFltId = uFltId;
    pDevice->fIsFilterOneShot = fIsOneShot;
}

static void nemuUsbFltDevOwnerClearLocked(PNEMUUSBFLT_DEVICE pDevice)
{
    ASSERT_WARN(pDevice->pOwner, ("no owner for device 0x%p", pDevice));
    --pDevice->pOwner->cActiveFilters;
    ASSERT_WARN(pDevice->pOwner->cActiveFilters < UINT32_MAX/2, ("cActiveFilters (%d)", pDevice->pOwner->cActiveFilters));
    pDevice->pOwner = NULL;
    pDevice->uFltId = 0;
}

static void nemuUsbFltDevOwnerUpdateLocked(PNEMUUSBFLT_DEVICE pDevice, PNEMUUSBFLTCTX pContext, uintptr_t uFltId, bool fIsOneShot)
{
    if (pDevice->pOwner != pContext)
    {
        if (pDevice->pOwner)
            nemuUsbFltDevOwnerClearLocked(pDevice);
        if (pContext)
            nemuUsbFltDevOwnerSetLocked(pDevice, pContext, uFltId, fIsOneShot);
    }
    else if (pContext)
    {
        pDevice->uFltId = uFltId;
        pDevice->fIsFilterOneShot = fIsOneShot;
    }
}

static PNEMUUSBFLT_DEVICE nemuUsbFltDevGetLocked(PDEVICE_OBJECT pPdo)
{
#ifdef NEMU_USB_WITH_VERBOSE_LOGGING
    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_NemuUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PNEMUUSBFLT_DEVICE pDevice = PNEMUUSBFLT_DEVICE_FROM_LE(pEntry);
        for (PLIST_ENTRY pEntry2 = pEntry->Flink;
                pEntry2 != &g_NemuUsbFltGlobals.DeviceList;
                pEntry2 = pEntry2->Flink)
        {
            PNEMUUSBFLT_DEVICE pDevice2 = PNEMUUSBFLT_DEVICE_FROM_LE(pEntry2);
            ASSERT_WARN(    pDevice->idVendor  != pDevice2->idVendor
                    || pDevice->idProduct != pDevice2->idProduct
                    || pDevice->bcdDevice != pDevice2->bcdDevice, ("duplicate devices in a list!!"));
        }
    }
#endif
    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_NemuUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PNEMUUSBFLT_DEVICE pDevice = PNEMUUSBFLT_DEVICE_FROM_LE(pEntry);
        ASSERT_WARN(    pDevice->enmState == NEMUUSBFLT_DEVSTATE_REPLUGGING
                || pDevice->enmState == NEMUUSBFLT_DEVSTATE_UNCAPTURED
                || pDevice->enmState == NEMUUSBFLT_DEVSTATE_CAPTURING
                || pDevice->enmState == NEMUUSBFLT_DEVSTATE_CAPTURED
                || pDevice->enmState == NEMUUSBFLT_DEVSTATE_USED_BY_GUEST,
                ("Invalid device state(%d) for device(0x%p) PDO(0x%p)", pDevice->enmState, pDevice, pDevice->Pdo));
        if (pDevice->Pdo == pPdo)
            return pDevice;
    }
    return NULL;
}

PNEMUUSBFLT_DEVICE nemuUsbFltDevGet(PDEVICE_OBJECT pPdo)
{
    PNEMUUSBFLT_DEVICE pDevice;

    NEMUUSBFLT_LOCK_ACQUIRE();
    pDevice = nemuUsbFltDevGetLocked(pPdo);
    /*
     * Prevent a host crash when nemuUsbFltDevGetLocked fails to locate the matching PDO
     * in g_NemuUsbFltGlobals.DeviceList (see @bugref{6509}).
     */
    if (pDevice == NULL)
    {
        WARN(("failed to get device for PDO(0x%p)", pPdo));
    }
    else if (pDevice->enmState > NEMUUSBFLT_DEVSTATE_ADDED)
    {
        nemuUsbFltDevRetain(pDevice);
        LOG(("found device (0x%p), state(%d) for PDO(0x%p)", pDevice, pDevice->enmState, pPdo));
    }
    else
    {
        LOG(("found replugging device (0x%p), state(%d) for PDO(0x%p)", pDevice, pDevice->enmState, pPdo));
        pDevice = NULL;
    }
    NEMUUSBFLT_LOCK_RELEASE();

    return pDevice;
}

static NTSTATUS nemuUsbFltPdoReplug(PDEVICE_OBJECT pDo)
{
    LOG(("Replugging PDO(0x%p)", pDo));
    NTSTATUS Status = NemuUsbToolIoInternalCtlSendSync(pDo, IOCTL_INTERNAL_USB_CYCLE_PORT, NULL, NULL);
    ASSERT_WARN(Status == STATUS_SUCCESS, ("replugging PDO(0x%p) failed Status(0x%x)", pDo, Status));
    LOG(("Replugging PDO(0x%p) done with Status(0x%x)", pDo, Status));
    return Status;
}

static bool nemuUsbFltDevCanBeCaptured(PNEMUUSBFLT_DEVICE pDevice)
{
    if (pDevice->bClass == USB_DEVICE_CLASS_HUB)
    {
        LOG(("device (0x%p), pdo (0x%p) is a hub, can not be captured", pDevice, pDevice->Pdo));
        return false;
    }
    return true;
}

static PNEMUUSBFLTCTX nemuUsbFltDevMatchLocked(PNEMUUSBFLT_DEVICE pDevice, uintptr_t *puId, bool fRemoveFltIfOneShot, bool *pfFilter, bool *pfIsOneShot)
{
    *puId = 0;
    *pfFilter = false;
    *pfIsOneShot = false;
    if (!nemuUsbFltDevCanBeCaptured(pDevice))
    {
        LOG(("nemuUsbFltDevCanBeCaptured returned false"));
        return NULL;
    }

    USBFILTER DevFlt;
    USBFilterInit(&DevFlt, USBFILTERTYPE_CAPTURE);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_VENDOR_ID, pDevice->idVendor, true);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_PRODUCT_ID, pDevice->idProduct, true);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_DEVICE_REV, pDevice->bcdDevice, true);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_DEVICE_CLASS, pDevice->bClass, true);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_DEVICE_SUB_CLASS, pDevice->bSubClass, true);
    USBFilterSetNumExact(&DevFlt, USBFILTERIDX_DEVICE_PROTOCOL, pDevice->bProtocol, true);
    USBFilterSetStringExact(&DevFlt, USBFILTERIDX_MANUFACTURER_STR, pDevice->szMfgName, true);
    USBFilterSetStringExact(&DevFlt, USBFILTERIDX_PRODUCT_STR, pDevice->szProduct, true);
    USBFilterSetStringExact(&DevFlt, USBFILTERIDX_SERIAL_NUMBER_STR, pDevice->szSerial, true);

    /* Run filters on the thing. */
    PNEMUUSBFLTCTX pOwner = NemuUSBFilterMatchEx(&DevFlt, puId, fRemoveFltIfOneShot, pfFilter, pfIsOneShot);
    USBFilterDelete(&DevFlt);
    return pOwner;
}

static void nemuUsbFltDevStateMarkReplugLocked(PNEMUUSBFLT_DEVICE pDevice)
{
    nemuUsbFltDevOwnerUpdateLocked(pDevice, NULL, 0, false);
    pDevice->enmState = NEMUUSBFLT_DEVSTATE_REPLUGGING;
}

static bool nemuUsbFltDevStateIsNotFiltered(PNEMUUSBFLT_DEVICE pDevice)
{
    return pDevice->enmState == NEMUUSBFLT_DEVSTATE_UNCAPTURED;
}

static bool nemuUsbFltDevStateIsFiltered(PNEMUUSBFLT_DEVICE pDevice)
{
    return pDevice->enmState >= NEMUUSBFLT_DEVSTATE_CAPTURING;
}

#define NEMUUSBMON_POPULATE_REQUEST_TIMEOUT_MS 10000

static NTSTATUS nemuUsbFltDevPopulate(PNEMUUSBFLT_DEVICE pDevice, PDEVICE_OBJECT pDo /*, BOOLEAN bPopulateNonFilterProps*/)
{
    NTSTATUS Status;
    PUSB_DEVICE_DESCRIPTOR pDevDr = 0;

    pDevice->Pdo = pDo;

    LOG(("Populating Device(0x%p) for PDO(0x%p)", pDevice, pDo));

    pDevDr = (PUSB_DEVICE_DESCRIPTOR)NemuUsbMonMemAllocZ(sizeof(*pDevDr));
    if (pDevDr == NULL)
    {
        WARN(("Failed to alloc mem for urb"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    do
    {
        Status = NemuUsbToolGetDescriptor(pDo, pDevDr, sizeof(*pDevDr), USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, NEMUUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
        if (!NT_SUCCESS(Status))
        {
            WARN(("getting device descriptor failed, Status (0x%x)", Status));
            break;
        }

        if (nemuUsbFltBlDevMatchLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice))
        {
            WARN(("found a known black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
            Status = STATUS_UNSUCCESSFUL;
            break;
        }

        LOG(("Device pid=%x vid=%x rev=%x", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
        pDevice->idVendor     = pDevDr->idVendor;
        pDevice->idProduct    = pDevDr->idProduct;
        pDevice->bcdDevice    = pDevDr->bcdDevice;
        pDevice->bClass       = pDevDr->bDeviceClass;
        pDevice->bSubClass    = pDevDr->bDeviceSubClass;
        pDevice->bProtocol    = pDevDr->bDeviceProtocol;
        pDevice->szSerial[0]  = 0;
        pDevice->szMfgName[0] = 0;
        pDevice->szProduct[0] = 0;

        /* If there are no strings, don't even try to get any string descriptors. */
        if (pDevDr->iSerialNumber || pDevDr->iManufacturer || pDevDr->iProduct)
        {
            int             langId;

            Status = NemuUsbToolGetLangID(pDo, &langId, NEMUUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
            if (!NT_SUCCESS(Status))
            {
                WARN(("reading language ID failed"));
                if (Status == STATUS_CANCELLED)
                {
                    WARN(("found a new black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
                    nemuUsbFltBlDevAddLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice);
                    Status = STATUS_UNSUCCESSFUL;
                }
                break;
            }

            if (pDevDr->iSerialNumber)
            {
                Status = NemuUsbToolGetStringDescriptorA(pDo, pDevice->szSerial, sizeof (pDevice->szSerial), pDevDr->iSerialNumber, langId, NEMUUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("reading serial number failed"));
                    ASSERT_WARN(pDevice->szSerial[0] == '\0', ("serial is not zero!!"));
                    if (Status == STATUS_CANCELLED)
                    {
                        WARN(("found a new black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
                        nemuUsbFltBlDevAddLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice);
                        Status = STATUS_UNSUCCESSFUL;
                        break;
                    }
                    LOG(("pretending success.."));
                    Status = STATUS_SUCCESS;
                }
            }

            if (pDevDr->iManufacturer)
            {
                Status = NemuUsbToolGetStringDescriptorA(pDo, pDevice->szMfgName, sizeof (pDevice->szMfgName), pDevDr->iManufacturer, langId, NEMUUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("reading manufacturer name failed"));
                    ASSERT_WARN(pDevice->szMfgName[0] == '\0', ("szMfgName is not zero!!"));
                    if (Status == STATUS_CANCELLED)
                    {
                        WARN(("found a new black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
                        nemuUsbFltBlDevAddLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice);
                        Status = STATUS_UNSUCCESSFUL;
                        break;
                    }
                    LOG(("pretending success.."));
                    Status = STATUS_SUCCESS;
                }
            }

            if (pDevDr->iProduct)
            {
                Status = NemuUsbToolGetStringDescriptorA(pDo, pDevice->szProduct, sizeof (pDevice->szProduct), pDevDr->iProduct, langId, NEMUUSBMON_POPULATE_REQUEST_TIMEOUT_MS);
                if (!NT_SUCCESS(Status))
                {
                    WARN(("reading product name failed"));
                    ASSERT_WARN(pDevice->szProduct[0] == '\0', ("szProduct is not zero!!"));
                    if (Status == STATUS_CANCELLED)
                    {
                        WARN(("found a new black list device, vid(0x%x), pid(0x%x), rev(0x%x)", pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice));
                        nemuUsbFltBlDevAddLocked(pDevDr->idVendor, pDevDr->idProduct, pDevDr->bcdDevice);
                        Status = STATUS_UNSUCCESSFUL;
                        break;
                    }
                    LOG(("pretending success.."));
                    Status = STATUS_SUCCESS;
                }
            }

#if 0
            if (bPopulateNonFilterProps)
            {
                WCHAR RegKeyBuf[512];
                ULONG cbRegKeyBuf = sizeof (RegKeyBuf);
                Status = IoGetDeviceProperty(pDo,
                                              DevicePropertyDriverKeyName,
                                              cbRegKeyBuf,
                                              RegKeyBuf,
                                              &cbRegKeyBuf);
                if (!NT_SUCCESS(Status))
                {
                    AssertMsgFailed((__FUNCTION__": IoGetDeviceProperty failed Status (0x%x)", Status));
                    break;
                }

                ANSI_STRING Ansi;
                UNICODE_STRING Unicode;
                Ansi.Buffer = pDevice->szDrvKeyName;
                Ansi.Length = 0;
                Ansi.MaximumLength = sizeof(pDevice->szDrvKeyName);
                RtlInitUnicodeString(&Unicode, RegKeyBuf);

                Status = RtlUnicodeStringToAnsiString(&Ansi, &Unicode, FALSE /* do not allocate */);
                if (!NT_SUCCESS(Status))
                {
                    AssertMsgFailed((__FUNCTION__": RtlUnicodeStringToAnsiString failed Status (0x%x)", Status));
                    break;
                }

                pDevice->fHighSpend = FALSE;
                Status = NemuUsbToolGetDeviceSpeed(pDo, &pDevice->fHighSpend);
                if (!NT_SUCCESS(Status))
                {
                    AssertMsgFailed((__FUNCTION__": NemuUsbToolGetDeviceSpeed failed Status (0x%x)", Status));
                    break;
                }
            }
#endif
            LOG((": strings: '%s':'%s':'%s' (lang ID %x)",
                        pDevice->szMfgName, pDevice->szProduct, pDevice->szSerial, langId));
        }

        LOG(("Populating Device(0x%p) for PDO(0x%p) Succeeded", pDevice, pDo));
        Status = STATUS_SUCCESS;
    } while (0);

    NemuUsbMonMemFree(pDevDr);
    LOG(("Populating Device(0x%p) for PDO(0x%p) Done, Status (0x%x)", pDevice, pDo, Status));
    return Status;
}

static void nemuUsbFltSignalChangeLocked()
{
    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.ContextList.Flink;
            pEntry != &g_NemuUsbFltGlobals.ContextList;
            pEntry = pEntry->Flink)
    {
        PNEMUUSBFLTCTX pCtx = PNEMUUSBFLTCTX_FROM_LE(pEntry);
        /* the removed context can not be in a list */
        Assert(!pCtx->bRemoved);
        if (pCtx->pChangeEvent)
        {
            KeSetEvent(pCtx->pChangeEvent,
                    0, /* increment*/
                    FALSE /* wait */);
        }
    }
}

static bool nemuUsbFltDevCheckReplugLocked(PNEMUUSBFLT_DEVICE pDevice, PNEMUUSBFLTCTX pContext)
{
    ASSERT_WARN(pContext, ("context is NULL!"));

    LOG(("Current context is (0x%p)", pContext));
    LOG(("Current Device owner is (0x%p)", pDevice->pOwner));

    /* check if device is already replugging */
    if (pDevice->enmState <= NEMUUSBFLT_DEVSTATE_ADDED)
    {
        LOG(("Device (0x%p) is already replugging, return..", pDevice));
        /* it is, do nothing */
        ASSERT_WARN(pDevice->enmState == NEMUUSBFLT_DEVSTATE_REPLUGGING,
                ("Device (0x%p) state is NOT REPLUGGING (%d)", pDevice, pDevice->enmState));
        return false;
    }

    if (pDevice->pOwner && pContext != pDevice->pOwner)
    {
        LOG(("Device (0x%p) is owned by another context(0x%p), current is(0x%p)", pDevice, pDevice->pOwner, pContext));
        /* this device is owned by another context, we're not allowed to do anything */
        return false;
    }

    uintptr_t uId = 0;
    bool bNeedReplug = false;
    bool fFilter = false;
    bool fIsOneShot = false;
    PNEMUUSBFLTCTX pNewOwner = nemuUsbFltDevMatchLocked(pDevice, &uId,
            false, /* do not remove a one-shot filter */
            &fFilter, &fIsOneShot);
    LOG(("Matching Info: Filter (0x%p), NewOwner(0x%p), fFilter(%d), fIsOneShot(%d)", uId, pNewOwner, (int)fFilter, (int)fIsOneShot));
    if (pDevice->pOwner && pNewOwner && pDevice->pOwner != pNewOwner)
    {
        LOG(("Matching: Device (0x%p) is requested another owner(0x%p), current is(0x%p)", pDevice, pNewOwner, pDevice->pOwner));
        /* the device is owned by another owner, we can not change the owner here */
        return false;
    }

    if (!fFilter)
    {
        LOG(("Matching: Device (0x%p) should NOT be filtered", pDevice));
        /* the device should NOT be filtered, check the current state  */
        if (nemuUsbFltDevStateIsNotFiltered(pDevice))
        {
            LOG(("Device (0x%p) is NOT filtered", pDevice));
            /* no changes */
            if (fIsOneShot)
            {
                ASSERT_WARN(pNewOwner, ("no new owner"));
                LOG(("Matching: This is a one-shot filter (0x%p), removing..", uId));
                /* remove a one-shot filter and keep the original filter data */
                int tmpRc = NemuUSBFilterRemove(pNewOwner, uId);
                ASSERT_WARN(RT_SUCCESS(tmpRc), ("remove filter failed, rc (%d)", tmpRc));
                if (!pDevice->pOwner)
                {
                    LOG(("Matching: updating the one-shot owner to (0x%p), fltId(0x%p)", pNewOwner, uId));
                    /* update owner for one-shot if the owner is changed (i.e. assigned) */
                    nemuUsbFltDevOwnerUpdateLocked(pDevice, pNewOwner, uId, true);
                }
                else
                {
                    LOG(("Matching: device already has owner (0x%p) assigned", pDevice->pOwner));
                }
            }
            else
            {
                LOG(("Matching: This is NOT a one-shot filter (0x%p), newOwner(0x%p)", uId, pNewOwner));
                if (pNewOwner)
                {
                    nemuUsbFltDevOwnerUpdateLocked(pDevice, pNewOwner, uId, false);
                }
            }
        }
        else
        {
            LOG(("Device (0x%p) IS filtered", pDevice));
            /* the device is currently filtered, we should release it only if
             * 1. device does not have an owner
             * or
             * 2. it should be released bue to a one-shot filter
             * or
             * 3. it is NOT grabbed by a one-shot filter */
            if (!pDevice->pOwner || fIsOneShot || !pDevice->fIsFilterOneShot)
            {
                LOG(("Matching: Need replug"));
                bNeedReplug = true;
            }
        }
    }
    else
    {
        LOG(("Matching: Device (0x%p) SHOULD be filtered", pDevice));
        /* the device should be filtered, check the current state  */
        ASSERT_WARN(uId, ("zero uid"));
        ASSERT_WARN(pNewOwner, ("zero pNewOwner"));
        if (nemuUsbFltDevStateIsFiltered(pDevice))
        {
            LOG(("Device (0x%p) IS filtered", pDevice));
            /* the device is filtered */
            if (pNewOwner == pDevice->pOwner)
            {
                LOG(("Device owner match"));
                /* no changes */
                if (fIsOneShot)
                {
                    LOG(("Matching: This is a one-shot filter (0x%p), removing..", uId));
                    /* remove a one-shot filter and keep the original filter data */
                    int tmpRc = NemuUSBFilterRemove(pNewOwner, uId);
                    ASSERT_WARN(RT_SUCCESS(tmpRc), ("remove filter failed, rc (%d)", tmpRc));
                }
                else
                {
                    LOG(("Matching: This is NOT a one-shot filter (0x%p), Owner(0x%p)", uId, pDevice->pOwner));
                    nemuUsbFltDevOwnerUpdateLocked(pDevice, pDevice->pOwner, uId, false);
                }
            }
            else
            {
                ASSERT_WARN(!pDevice->pOwner, ("device should NOT have owner"));
                LOG(("Matching: Need replug"));
                /* the device needs to be filtered, but the owner changes, replug needed */
                bNeedReplug = true;
            }
        }
        else
        {
            /* the device is currently NOT filtered,
             * we should replug it only if
             * 1. device does not have an owner
             * or
             * 2. it should be captured due to a one-shot filter
             * or
             * 3. it is NOT released by a one-shot filter */
            if (!pDevice->pOwner || fIsOneShot || !pDevice->fIsFilterOneShot)
            {
                bNeedReplug = true;
                LOG(("Matching: Need replug"));
            }
        }
    }

    if (bNeedReplug)
    {
        LOG(("Matching: Device needs replugging, marking as such"));
        nemuUsbFltDevStateMarkReplugLocked(pDevice);
    }
    else
    {
        LOG(("Matching: Device does NOT need replugging"));
    }

    return bNeedReplug;
}

static void nemuUsbFltReplugList(PLIST_ENTRY pList)
{
    PLIST_ENTRY pNext;
    for (PLIST_ENTRY pEntry = pList->Flink;
            pEntry != pList;
            pEntry = pNext)
    {
        pNext = pEntry->Flink;
        PNEMUUSBFLT_DEVICE pDevice = PNEMUUSBFLT_DEVICE_FROM_REPLUGGINGLE(pEntry);
        LOG(("replugging matched PDO(0x%p), pDevice(0x%p)", pDevice->Pdo, pDevice));
        ASSERT_WARN(pDevice->enmState == NEMUUSBFLT_DEVSTATE_REPLUGGING
                || pDevice->enmState == NEMUUSBFLT_DEVSTATE_REMOVED,
                ("invalid state(0x%x) for device(0x%p)", pDevice->enmState, pDevice));

        nemuUsbFltPdoReplug(pDevice->Pdo);
        ObDereferenceObject(pDevice->Pdo);
        nemuUsbFltDevRelease(pDevice);
    }
}

typedef struct NEMUUSBFLTCHECKWALKER
{
    PNEMUUSBFLTCTX pContext;
} NEMUUSBFLTCHECKWALKER, *PNEMUUSBFLTCHECKWALKER;

static DECLCALLBACK(BOOLEAN) nemuUsbFltFilterCheckWalker(PFILE_OBJECT pFile, PDEVICE_OBJECT pTopDo, PDEVICE_OBJECT pHubDo, PVOID pvContext)
{
    PNEMUUSBFLTCHECKWALKER pData = (PNEMUUSBFLTCHECKWALKER)pvContext;
    PNEMUUSBFLTCTX pContext = pData->pContext;

    LOG(("Visiting pFile(0x%p), pTopDo(0x%p), pHubDo(0x%p), oContext(0x%p)", pFile, pTopDo, pHubDo, pContext));
    KIRQL Irql = KeGetCurrentIrql();
    ASSERT_WARN(Irql == PASSIVE_LEVEL, ("unexpected IRQL (%d)", Irql));

    PDEVICE_RELATIONS pDevRelations = NULL;

    NTSTATUS Status = NemuUsbMonQueryBusRelations(pTopDo, pFile, &pDevRelations);
    if (Status == STATUS_SUCCESS && pDevRelations)
    {
        ULONG cReplugPdos = pDevRelations->Count;
        LIST_ENTRY ReplugDevList;
        InitializeListHead(&ReplugDevList);
        for (ULONG k = 0; k < pDevRelations->Count; ++k)
        {
            PDEVICE_OBJECT pDevObj = pDevRelations->Objects[k];

            LOG(("Found existing USB PDO 0x%p", pDevObj));
            NEMUUSBFLT_LOCK_ACQUIRE();
            PNEMUUSBFLT_DEVICE pDevice = nemuUsbFltDevGetLocked(pDevObj);
            if (pDevice)
            {
                LOG(("Found existing device info (0x%p) for PDO 0x%p", pDevice, pDevObj));
                bool bReplug = nemuUsbFltDevCheckReplugLocked(pDevice, pContext);
                if (bReplug)
                {
                    LOG(("Replug needed for device (0x%p)", pDevice));
                    InsertHeadList(&ReplugDevList, &pDevice->RepluggingLe);
                    nemuUsbFltDevRetain(pDevice);
                    /* do not dereference object since we will use it later */
                }
                else
                {
                    LOG(("Replug NOT needed for device (0x%p)", pDevice));
                    ObDereferenceObject(pDevObj);
                }

                NEMUUSBFLT_LOCK_RELEASE();

                pDevRelations->Objects[k] = NULL;
                --cReplugPdos;
                ASSERT_WARN((uint32_t)cReplugPdos < UINT32_MAX/2, ("cReplugPdos(%d) state broken", cReplugPdos));
                continue;
            }
            NEMUUSBFLT_LOCK_RELEASE();

            LOG(("NO device info found for PDO 0x%p", pDevObj));
            NEMUUSBFLT_DEVICE Device;
            Status = nemuUsbFltDevPopulate(&Device, pDevObj /*, FALSE /* only need filter properties */);
            if (NT_SUCCESS(Status))
            {
                uintptr_t uId = 0;
                bool fFilter = false;
                bool fIsOneShot = false;
                NEMUUSBFLT_LOCK_ACQUIRE();
                PNEMUUSBFLTCTX pCtx = nemuUsbFltDevMatchLocked(&Device, &uId,
                        false, /* do not remove a one-shot filter */
                        &fFilter, &fIsOneShot);
                NEMUUSBFLT_LOCK_RELEASE();
                LOG(("Matching Info: Filter (0x%p), pCtx(0x%p), fFilter(%d), fIsOneShot(%d)", uId, pCtx, (int)fFilter, (int)fIsOneShot));
                if (fFilter)
                {
                    LOG(("Matching: This device SHOULD be filtered"));
                    /* this device needs to be filtered, but it's not,
                     * leave the PDO in array to issue a replug request for it
                     * later on */
                    continue;
                }
            }
            else
            {
                WARN(("nemuUsbFltDevPopulate for PDO 0x%p failed with Status 0x%x", pDevObj, Status));
            }

            LOG(("Matching: This device should NOT be filtered"));
            /* this device should not be filtered, and it's not */
            ObDereferenceObject(pDevObj);
            pDevRelations->Objects[k] = NULL;
            --cReplugPdos;
            ASSERT_WARN((uint32_t)cReplugPdos < UINT32_MAX/2, ("cReplugPdos is %d", cReplugPdos));
        }

        LOG(("(%d) non-matched PDOs to be replugged", cReplugPdos));

        if (cReplugPdos)
        {
            for (ULONG k = 0; k < pDevRelations->Count; ++k)
            {
                if (!pDevRelations->Objects[k])
                    continue;

                Status = nemuUsbFltPdoReplug(pDevRelations->Objects[k]);
                ASSERT_WARN(Status == STATUS_SUCCESS, ("nemuUsbFltPdoReplug ailed Status(0x%x)", Status));
                ObDereferenceObject(pDevRelations->Objects[k]);
                if (!--cReplugPdos)
                    break;
            }

            ASSERT_WARN(!cReplugPdos, ("cReplugPdosreached zero!"));
        }

        nemuUsbFltReplugList(&ReplugDevList);

        ExFreePool(pDevRelations);
    }
    else
    {
        WARN(("NemuUsbMonQueryBusRelations failed for DO(0x%p), Status(0x%x), pDevRelations(0x%p)",
                pTopDo, Status, pDevRelations));
    }

    LOG(("Done Visiting pFile(0x%p), pTopDo(0x%p), pHubDo(0x%p), oContext(0x%p)", pFile, pTopDo, pHubDo, pContext));

    return TRUE;
}

NTSTATUS NemuUsbFltFilterCheck(PNEMUUSBFLTCTX pContext)
{
    KIRQL Irql = KeGetCurrentIrql();
    ASSERT_WARN(Irql == PASSIVE_LEVEL, ("unexpected IRQL (%d)", Irql));

    LOG(("Running filters, Context (0x%p)..", pContext));

    NEMUUSBFLTCHECKWALKER Data;
    Data.pContext = pContext;
    nemuUsbMonHubDevWalk(nemuUsbFltFilterCheckWalker, &Data, NEMUUSBMONHUBWALK_F_FDO);

    LOG(("DONE Running filters, Context (0x%p)", pContext));

    return STATUS_SUCCESS;
}

NTSTATUS NemuUsbFltClose(PNEMUUSBFLTCTX pContext)
{
    LOG(("Closing context(0x%p)", pContext));
    LIST_ENTRY ReplugDevList;
    InitializeListHead(&ReplugDevList);

    ASSERT_WARN(pContext, ("null context"));

    KIRQL Irql = KeGetCurrentIrql();
    ASSERT_WARN(Irql == PASSIVE_LEVEL, ("irql==(%d)", Irql));

    NEMUUSBFLT_LOCK_ACQUIRE();
    uint32_t cActiveFilters = pContext->cActiveFilters;
    pContext->bRemoved = TRUE;
    if (pContext->pChangeEvent)
    {
        LOG(("seting & closing change event (0x%p)", pContext->pChangeEvent));
        KeSetEvent(pContext->pChangeEvent,
                0, /* increment*/
                FALSE /* wait */);
        ObDereferenceObject(pContext->pChangeEvent);
        pContext->pChangeEvent = NULL;
    }
    else
    {
        LOG(("no change event"));
    }
    RemoveEntryList(&pContext->ListEntry);

    LOG(("removing owner filters"));
    /* now re-arrange the filters */
    /* 1. remove filters */
    NemuUSBFilterRemoveOwner(pContext);

    LOG(("enumerating devices.."));
    /* 2. check if there are devices owned */
    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_NemuUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PNEMUUSBFLT_DEVICE pDevice = PNEMUUSBFLT_DEVICE_FROM_LE(pEntry);
        if (pDevice->pOwner != pContext)
            continue;

        LOG(("found device(0x%p), pdo(0x%p), state(%d), filter id(0x%p), oneshot(%d)",
                pDevice, pDevice->Pdo, pDevice->enmState, pDevice->uFltId, (int)pDevice->fIsFilterOneShot));
        ASSERT_WARN(pDevice->enmState != NEMUUSBFLT_DEVSTATE_ADDED, ("NEMUUSBFLT_DEVSTATE_ADDED state for device(0x%p)", pDevice));
        ASSERT_WARN(pDevice->enmState != NEMUUSBFLT_DEVSTATE_REMOVED, ("NEMUUSBFLT_DEVSTATE_REMOVED state for device(0x%p)", pDevice));

        nemuUsbFltDevOwnerClearLocked(pDevice);

        if (nemuUsbFltDevCheckReplugLocked(pDevice, pContext))
        {
            LOG(("device needs replug"));
            InsertHeadList(&ReplugDevList, &pDevice->RepluggingLe);
            /* retain to ensure the device is not removed before we issue a replug */
            nemuUsbFltDevRetain(pDevice);
            /* keep the PDO alive */
            ObReferenceObject(pDevice->Pdo);
        }
        else
        {
            LOG(("device does NOT need replug"));
        }
    }
    NEMUUSBFLT_LOCK_RELEASE();

    /* this should replug all devices that were either skipped or grabbed due to the context's */
    nemuUsbFltReplugList(&ReplugDevList);

    LOG(("SUCCESS done context(0x%p)", pContext));
    return STATUS_SUCCESS;
}

NTSTATUS NemuUsbFltCreate(PNEMUUSBFLTCTX pContext)
{
    LOG(("Creating context(0x%p)", pContext));
    memset(pContext, 0, sizeof (*pContext));
    pContext->Process = RTProcSelf();
    NEMUUSBFLT_LOCK_ACQUIRE();
    InsertHeadList(&g_NemuUsbFltGlobals.ContextList, &pContext->ListEntry);
    NEMUUSBFLT_LOCK_RELEASE();
    LOG(("SUCCESS context(0x%p)", pContext));
    return STATUS_SUCCESS;
}

int NemuUsbFltAdd(PNEMUUSBFLTCTX pContext, PUSBFILTER pFilter, uintptr_t *pId)
{
    LOG(("adding filter, Context (0x%p)..", pContext));
    *pId = 0;
    /* LOG the filter details. */
    LOG((__FUNCTION__": %s %s %s",
        USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  ? USBFilterGetString(pFilter, USBFILTERIDX_MANUFACTURER_STR)  : "<null>",
        USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       ? USBFilterGetString(pFilter, USBFILTERIDX_PRODUCT_STR)       : "<null>",
        USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) ? USBFilterGetString(pFilter, USBFILTERIDX_SERIAL_NUMBER_STR) : "<null>"));
#ifdef NEMU_USB_WITH_VERBOSE_LOGGING
    LOG(("NemuUSBClient::addFilter: idVendor=%#x idProduct=%#x bcdDevice=%#x bDeviceClass=%#x bDeviceSubClass=%#x bDeviceProtocol=%#x bBus=%#x bPort=%#x Type%#x",
              USBFilterGetNum(pFilter, USBFILTERIDX_VENDOR_ID),
              USBFilterGetNum(pFilter, USBFILTERIDX_PRODUCT_ID),
              USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_REV),
              USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_CLASS),
              USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_SUB_CLASS),
              USBFilterGetNum(pFilter, USBFILTERIDX_DEVICE_PROTOCOL),
              USBFilterGetNum(pFilter, USBFILTERIDX_BUS),
              USBFilterGetNum(pFilter, USBFILTERIDX_PORT),
              USBFilterGetFilterType(pFilter)));
#endif

    /* We can't get the bus/port numbers. Ignore them while matching. */
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_BUS, false);
    USBFilterSetMustBePresent(pFilter, USBFILTERIDX_PORT, false);

    uintptr_t uId = 0;
    NEMUUSBFLT_LOCK_ACQUIRE();
    /* Add the filter. */
    int rc = NemuUSBFilterAdd(pFilter, pContext, &uId);
    NEMUUSBFLT_LOCK_RELEASE();
    if (RT_SUCCESS(rc))
    {
        LOG(("ADDED filer id 0x%p", uId));
        ASSERT_WARN(uId, ("uid is NULL"));
#ifdef NEMU_USBMON_WITH_FILTER_AUTOAPPLY
        NemuUsbFltFilterCheck();
#endif
    }
    else
    {
        WARN(("NemuUSBFilterAdd failed rc (%d)", rc));
        ASSERT_WARN(!uId, ("uid is not NULL"));
    }

    *pId = uId;
    return rc;
}

int NemuUsbFltRemove(PNEMUUSBFLTCTX pContext, uintptr_t uId)
{
    LOG(("removing filter id(0x%p), Context (0x%p)..", pContext, uId));
    Assert(uId);

    NEMUUSBFLT_LOCK_ACQUIRE();
    int rc = NemuUSBFilterRemove(pContext, uId);
    if (!RT_SUCCESS(rc))
    {
        WARN(("NemuUSBFilterRemove failed rc (%d)", rc));
        NEMUUSBFLT_LOCK_RELEASE();
        return rc;
    }

    LOG(("enumerating devices.."));
    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_NemuUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PNEMUUSBFLT_DEVICE pDevice = PNEMUUSBFLT_DEVICE_FROM_LE(pEntry);
        if (pDevice->fIsFilterOneShot)
        {
            ASSERT_WARN(!pDevice->uFltId, ("oneshot filter on device(0x%p): unexpected uFltId(%d)", pDevice, pDevice->uFltId));
        }

        if (pDevice->uFltId != uId)
            continue;

        ASSERT_WARN(pDevice->pOwner == pContext, ("Device(0x%p) owner(0x%p) not match to (0x%p)", pDevice, pDevice->pOwner, pContext));
        if (pDevice->pOwner != pContext)
            continue;

        LOG(("found device(0x%p), pdo(0x%p), state(%d), filter id(0x%p), oneshot(%d)",
                pDevice, pDevice->Pdo, pDevice->enmState, pDevice->uFltId, (int)pDevice->fIsFilterOneShot));
        ASSERT_WARN(!pDevice->fIsFilterOneShot, ("device(0x%p) is filtered with a oneshot filter", pDevice));
        pDevice->uFltId = 0;
        /* clear the fIsFilterOneShot flag to ensure the device is replugged on the next NemuUsbFltFilterCheck call */
        pDevice->fIsFilterOneShot = false;
    }
    NEMUUSBFLT_LOCK_RELEASE();

    LOG(("done enumerating devices"));

    if (RT_SUCCESS(rc))
    {
#ifdef NEMU_USBMON_WITH_FILTER_AUTOAPPLY
        NemuUsbFltFilterCheck();
#endif
    }
    return rc;
}

NTSTATUS NemuUsbFltSetNotifyEvent(PNEMUUSBFLTCTX pContext, HANDLE hEvent)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PKEVENT pEvent = NULL;
    PKEVENT pOldEvent = NULL;
    if (hEvent)
    {
        Status = ObReferenceObjectByHandle(hEvent,
                    EVENT_MODIFY_STATE,
                    *ExEventObjectType, UserMode,
                    (PVOID*)&pEvent,
                    NULL);
        Assert(Status == STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    NEMUUSBFLT_LOCK_ACQUIRE();
    pOldEvent = pContext->pChangeEvent;
    pContext->pChangeEvent = pEvent;
    NEMUUSBFLT_LOCK_RELEASE();

    if (pOldEvent)
    {
        ObDereferenceObject(pOldEvent);
    }

    return STATUS_SUCCESS;
}

static USBDEVICESTATE nemuUsbDevGetUserState(PNEMUUSBFLTCTX pContext, PNEMUUSBFLT_DEVICE pDevice)
{
    if (nemuUsbFltDevStateIsNotFiltered(pDevice))
        return USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;

    /* the device is filtered, or replugging */
    if (pDevice->enmState == NEMUUSBFLT_DEVSTATE_REPLUGGING)
    {
        ASSERT_WARN(!pDevice->pOwner, ("replugging device(0x%p) still has an owner(0x%p)", pDevice, pDevice->pOwner));
        ASSERT_WARN(!pDevice->uFltId, ("replugging device(0x%p) still has filter(0x%p)", pDevice, pDevice->uFltId));
        /* no user state for this, we should not return it tu the user */
        return USBDEVICESTATE_USED_BY_HOST;
    }

    /* the device is filtered, if owner differs from the context, return as USED_BY_HOST */
    ASSERT_WARN(pDevice->pOwner, ("device(0x%p) has noowner", pDevice));
    /* the id can be null if a filter is removed */
//    Assert(pDevice->uFltId);

    if (pDevice->pOwner != pContext)
    {
        LOG(("Device owner differs from the current context, returning used by host"));
        return USBDEVICESTATE_USED_BY_HOST;
    }

    switch (pDevice->enmState)
    {
        case NEMUUSBFLT_DEVSTATE_UNCAPTURED:
        case NEMUUSBFLT_DEVSTATE_CAPTURING:
            return USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;
        case NEMUUSBFLT_DEVSTATE_CAPTURED:
            return USBDEVICESTATE_HELD_BY_PROXY;
        case NEMUUSBFLT_DEVSTATE_USED_BY_GUEST:
            return USBDEVICESTATE_USED_BY_GUEST;
        default:
            WARN(("unexpected device state(%d) for device(0x%p)", pDevice->enmState, pDevice));
            return USBDEVICESTATE_UNSUPPORTED;
    }
}

static void nemuUsbDevToUserInfo(PNEMUUSBFLTCTX pContext, PNEMUUSBFLT_DEVICE pDevice, PUSBSUP_DEVINFO pDevInfo)
{
#if 0
    pDevInfo->usVendorId = pDevice->idVendor;
    pDevInfo->usProductId = pDevice->idProduct;
    pDevInfo->usRevision = pDevice->bcdDevice;
    pDevInfo->enmState = nemuUsbDevGetUserState(pContext, pDevice);

    strcpy(pDevInfo->szDrvKeyName, pDevice->szDrvKeyName);
    if (pDevInfo->enmState == USBDEVICESTATE_HELD_BY_PROXY
            || pDevInfo->enmState == USBDEVICESTATE_USED_BY_GUEST)
    {
        /* this is the only case where we return the obj name to the client */
        strcpy(pDevInfo->szObjName, pDevice->szObjName);
    }
    pDevInfo->fHighSpeed = pDevice->fHighSpeed;
#endif
}

NTSTATUS NemuUsbFltGetDevice(PNEMUUSBFLTCTX pContext, HNEMUUSBDEVUSR hDevice, PUSBSUP_GETDEV_MON pInfo)
{
    Assert(hDevice);

    memset (pInfo, 0, sizeof (*pInfo));
    NEMUUSBFLT_LOCK_ACQUIRE();
    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_NemuUsbFltGlobals.DeviceList;
            pEntry = pEntry->Flink)
    {
        PNEMUUSBFLT_DEVICE pDevice = PNEMUUSBFLT_DEVICE_FROM_LE(pEntry);
        Assert(pDevice->enmState != NEMUUSBFLT_DEVSTATE_REMOVED);
        Assert(pDevice->enmState != NEMUUSBFLT_DEVSTATE_ADDED);

        if (pDevice != hDevice)
            continue;

        USBDEVICESTATE enmUsrState = nemuUsbDevGetUserState(pContext, pDevice);
        pInfo->enmState = enmUsrState;
        NEMUUSBFLT_LOCK_RELEASE();
        return STATUS_SUCCESS;
    }

    NEMUUSBFLT_LOCK_RELEASE();

    /* this should not occur */
    AssertFailed();

    return STATUS_INVALID_PARAMETER;
}

NTSTATUS NemuUsbFltPdoAdd(PDEVICE_OBJECT pPdo, BOOLEAN *pbFiltered)
{
    *pbFiltered = FALSE;
    PNEMUUSBFLT_DEVICE pDevice;

    /* first check if device is in the a already */
    NEMUUSBFLT_LOCK_ACQUIRE();
    pDevice = nemuUsbFltDevGetLocked(pPdo);
    if (pDevice)
    {
        LOG(("found device (0x%p), state(%d) for PDO(0x%p)", pDevice, pDevice->enmState, pPdo));
        ASSERT_WARN(pDevice->enmState != NEMUUSBFLT_DEVSTATE_ADDED, ("NEMUUSBFLT_DEVSTATE_ADDED state for device(0x%p)", pDevice));
        ASSERT_WARN(pDevice->enmState != NEMUUSBFLT_DEVSTATE_REMOVED, ("NEMUUSBFLT_DEVSTATE_REMOVED state for device(0x%p)", pDevice));
        *pbFiltered = pDevice->enmState >= NEMUUSBFLT_DEVSTATE_CAPTURING;
        NEMUUSBFLT_LOCK_RELEASE();
        return STATUS_SUCCESS;
    }
    NEMUUSBFLT_LOCK_RELEASE();
    pDevice = (PNEMUUSBFLT_DEVICE)NemuUsbMonMemAllocZ(sizeof (*pDevice));
    if (!pDevice)
    {
        WARN(("NemuUsbMonMemAllocZ failed"));
        return STATUS_NO_MEMORY;
    }

    pDevice->enmState = NEMUUSBFLT_DEVSTATE_ADDED;
    pDevice->cRefs = 1;
    NTSTATUS Status = nemuUsbFltDevPopulate(pDevice, pPdo /* , TRUE /* need all props */);
    if (!NT_SUCCESS(Status))
    {
        WARN(("nemuUsbFltDevPopulate failed, Status 0x%x", Status));
        NemuUsbMonMemFree(pDevice);
        return Status;
    }

    uintptr_t uId;
    bool fFilter = false;
    bool fIsOneShot = false;
    PNEMUUSBFLTCTX pCtx;
    PNEMUUSBFLT_DEVICE pTmpDev;
    NEMUUSBFLT_LOCK_ACQUIRE();
    /* (paranoia) re-check the device is still not here */
    pTmpDev = nemuUsbFltDevGetLocked(pPdo);
    if (pTmpDev)
    {
        LOG(("second try: found device (0x%p), state(%d) for PDO(0x%p)", pDevice, pDevice->enmState, pPdo));
        ASSERT_WARN(pDevice->enmState != NEMUUSBFLT_DEVSTATE_ADDED, ("second try: NEMUUSBFLT_DEVSTATE_ADDED state for device(0x%p)", pDevice));
        ASSERT_WARN(pDevice->enmState != NEMUUSBFLT_DEVSTATE_REMOVED, ("second try: NEMUUSBFLT_DEVSTATE_REMOVED state for device(0x%p)", pDevice));
        *pbFiltered = pTmpDev->enmState >= NEMUUSBFLT_DEVSTATE_CAPTURING;
        NEMUUSBFLT_LOCK_RELEASE();
        NemuUsbMonMemFree(pDevice);
        return STATUS_SUCCESS;
    }

    LOG(("Created Device 0x%p for PDO 0x%p", pDevice, pPdo));

    pCtx = nemuUsbFltDevMatchLocked(pDevice, &uId,
            true, /* remove a one-shot filter */
            &fFilter, &fIsOneShot);
    LOG(("Matching Info: Filter (0x%p), pCtx(0x%p), fFilter(%d), fIsOneShot(%d)", uId, pCtx, (int)fFilter, (int)fIsOneShot));
    if (fFilter)
    {
        LOG(("Created Device 0x%p should be filtered", pDevice));
        ASSERT_WARN(pCtx, ("zero ctx"));
        ASSERT_WARN(uId, ("zero uId"));
        pDevice->enmState = NEMUUSBFLT_DEVSTATE_CAPTURING;
    }
    else
    {
        LOG(("Created Device 0x%p should NOT be filtered", pDevice));
        ASSERT_WARN(!uId == !pCtx, ("invalid uid(0x%p) - ctx(0x%p) pair", uId, pCtx)); /* either both zero or both not */
        pDevice->enmState = NEMUUSBFLT_DEVSTATE_UNCAPTURED;
    }

    if (pCtx)
        nemuUsbFltDevOwnerSetLocked(pDevice, pCtx, fIsOneShot ? 0 : uId, fIsOneShot);

    InsertHeadList(&g_NemuUsbFltGlobals.DeviceList, &pDevice->GlobalLe);

    /* do not need to signal anything here -
     * going to do that once the proxy device object starts */
    NEMUUSBFLT_LOCK_RELEASE();

    *pbFiltered = fFilter;

    return STATUS_SUCCESS;
}

NTSTATUS NemuUsbFltPdoAddCompleted(PDEVICE_OBJECT pPdo)
{
    NEMUUSBFLT_LOCK_ACQUIRE();
    nemuUsbFltSignalChangeLocked();
    NEMUUSBFLT_LOCK_RELEASE();
    return STATUS_SUCCESS;
}

BOOLEAN NemuUsbFltPdoIsFiltered(PDEVICE_OBJECT pPdo)
{
    NEMUUSBFLT_DEVSTATE enmState = NEMUUSBFLT_DEVSTATE_REMOVED;
    NEMUUSBFLT_LOCK_ACQUIRE();
    PNEMUUSBFLT_DEVICE pDevice = nemuUsbFltDevGetLocked(pPdo);
    if (pDevice)
    {
        enmState = pDevice->enmState;
    }
    NEMUUSBFLT_LOCK_RELEASE();

    return enmState >= NEMUUSBFLT_DEVSTATE_CAPTURING;
}

NTSTATUS NemuUsbFltPdoRemove(PDEVICE_OBJECT pPdo)
{
    PNEMUUSBFLT_DEVICE pDevice;
    NEMUUSBFLT_DEVSTATE enmOldState;

    NEMUUSBFLT_LOCK_ACQUIRE();
    pDevice = nemuUsbFltDevGetLocked(pPdo);
    if (pDevice)
    {
        RemoveEntryList(&pDevice->GlobalLe);
        enmOldState = pDevice->enmState;
        pDevice->enmState = NEMUUSBFLT_DEVSTATE_REMOVED;
        if (enmOldState != NEMUUSBFLT_DEVSTATE_REPLUGGING)
        {
            nemuUsbFltSignalChangeLocked();
        }
        else
        {
            /* the device *should* reappear, do signlling on re-appear only
             * to avoid extra signaling. still there might be a situation
             * when the device will not re-appear if it gets physically removed
             * before it re-appears
             * @todo: set a timer callback to do a notification from it */
        }
    }
    NEMUUSBFLT_LOCK_RELEASE();
    if (pDevice)
        nemuUsbFltDevRelease(pDevice);
    return STATUS_SUCCESS;
}

HNEMUUSBFLTDEV NemuUsbFltProxyStarted(PDEVICE_OBJECT pPdo)
{
    PNEMUUSBFLT_DEVICE pDevice;
    NEMUUSBFLT_LOCK_ACQUIRE();
    pDevice = nemuUsbFltDevGetLocked(pPdo);
    /*
     * Prevent a host crash when nemuUsbFltDevGetLocked fails to locate the matching PDO
     * in g_NemuUsbFltGlobals.DeviceList (see @bugref{6509}).
     */
    if (pDevice == NULL)
    {
        WARN(("failed to get device for PDO(0x%p)", pPdo));
    }
    else if (pDevice->enmState = NEMUUSBFLT_DEVSTATE_CAPTURING)
    {
        pDevice->enmState = NEMUUSBFLT_DEVSTATE_CAPTURED;
        LOG(("The proxy notified proxy start for the captured device 0x%x", pDevice));
        nemuUsbFltDevRetain(pDevice);
        nemuUsbFltSignalChangeLocked();
    }
    else
    {
        WARN(("invalid state, %d", pDevice->enmState));
        pDevice = NULL;
    }
    NEMUUSBFLT_LOCK_RELEASE();
    return pDevice;
}

void NemuUsbFltProxyStopped(HNEMUUSBFLTDEV hDev)
{
    PNEMUUSBFLT_DEVICE pDevice = (PNEMUUSBFLT_DEVICE)hDev;
    /*
     * Prevent a host crash when NemuUsbFltProxyStarted fails, returning NULL.
     * See @bugref{6509}.
     */
    if (pDevice == NULL)
    {
        WARN(("NemuUsbFltProxyStopped called with NULL device pointer"));
        return;
    }
    NEMUUSBFLT_LOCK_ACQUIRE();
    if (pDevice->enmState == NEMUUSBFLT_DEVSTATE_CAPTURED
            || pDevice->enmState == NEMUUSBFLT_DEVSTATE_USED_BY_GUEST)
    {
        /* this is due to devie was physically removed */
        LOG(("The proxy notified proxy stop for the captured device 0x%x, current state %d", pDevice, pDevice->enmState));
        pDevice->enmState = NEMUUSBFLT_DEVSTATE_CAPTURING;
        nemuUsbFltSignalChangeLocked();
    }
    else
    {
        if(pDevice->enmState != NEMUUSBFLT_DEVSTATE_REPLUGGING)
        {
            WARN(("invalid state, %d", pDevice->enmState));
        }
    }
    NEMUUSBFLT_LOCK_RELEASE();

    nemuUsbFltDevRelease(pDevice);
}

NTSTATUS NemuUsbFltInit()
{
    int rc = NemuUSBFilterInit();
    if (RT_FAILURE(rc))
    {
        WARN(("NemuUSBFilterInit failed, rc (%d)", rc));
        return STATUS_UNSUCCESSFUL;
    }

    memset(&g_NemuUsbFltGlobals, 0, sizeof (g_NemuUsbFltGlobals));
    InitializeListHead(&g_NemuUsbFltGlobals.DeviceList);
    InitializeListHead(&g_NemuUsbFltGlobals.ContextList);
    InitializeListHead(&g_NemuUsbFltGlobals.BlackDeviceList);
    nemuUsbFltBlDevPopulateWithKnownLocked();
    NEMUUSBFLT_LOCK_INIT();
    return STATUS_SUCCESS;
}

NTSTATUS NemuUsbFltTerm()
{
    bool bBusy = false;
    NEMUUSBFLT_LOCK_ACQUIRE();
    do
    {
        if (!IsListEmpty(&g_NemuUsbFltGlobals.ContextList))
        {
            AssertFailed();
            bBusy = true;
            break;
        }

        PLIST_ENTRY pNext = NULL;
        for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.DeviceList.Flink;
                pEntry != &g_NemuUsbFltGlobals.DeviceList;
                pEntry = pNext)
        {
            pNext = pEntry->Flink;
            PNEMUUSBFLT_DEVICE pDevice = PNEMUUSBFLT_DEVICE_FROM_LE(pEntry);
            Assert(!pDevice->uFltId);
            Assert(!pDevice->pOwner);
            if (pDevice->cRefs != 1)
            {
                AssertFailed();
                bBusy = true;
                break;
            }
        }
    } while (0);

    NEMUUSBFLT_LOCK_RELEASE()

    if (bBusy)
    {
        return STATUS_DEVICE_BUSY;
    }

    for (PLIST_ENTRY pEntry = g_NemuUsbFltGlobals.DeviceList.Flink;
            pEntry != &g_NemuUsbFltGlobals.DeviceList;
            pEntry = g_NemuUsbFltGlobals.DeviceList.Flink)
    {
        RemoveEntryList(pEntry);
        PNEMUUSBFLT_DEVICE pDevice = PNEMUUSBFLT_DEVICE_FROM_LE(pEntry);
        pDevice->enmState = NEMUUSBFLT_DEVSTATE_REMOVED;
        nemuUsbFltDevRelease(pDevice);
    }

    nemuUsbFltBlDevClearLocked();

    NEMUUSBFLT_LOCK_TERM();

    NemuUSBFilterTerm();

    return STATUS_SUCCESS;
}

