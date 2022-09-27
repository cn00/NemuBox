/* $Id: NemuUsbRt.h $ */
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
#ifndef ___NemuUsbRt_h___
#define ___NemuUsbRt_h___

#include "NemuUsbCmn.h"
#include "../cmn/NemuUsbIdc.h"

#define NEMUUSBRT_MAX_CFGS 4

typedef struct NEMUUSB_PIPE_INFO {
    UCHAR       EndpointAddress;
    ULONG       NextScheduledFrame;
} NEMUUSB_PIPE_INFO;

typedef struct NEMUUSB_IFACE_INFO {
    USBD_INTERFACE_INFORMATION      *pInterfaceInfo;
    NEMUUSB_PIPE_INFO               *pPipeInfo;
} NEMUUSB_IFACE_INFO;

typedef struct NEMUUSB_RT
{
    UNICODE_STRING                  IfName;

    HANDLE                          hConfiguration;
    uint32_t                        uConfigValue;

    uint32_t                        uNumInterfaces;
    USB_DEVICE_DESCRIPTOR           *devdescr;
    USB_CONFIGURATION_DESCRIPTOR    *cfgdescr[NEMUUSBRT_MAX_CFGS];

    NEMUUSB_IFACE_INFO              *pVBIfaceInfo;

    uint16_t                        idVendor, idProduct, bcdDevice;
    char                            szSerial[MAX_USB_SERIAL_STRING];
    BOOLEAN                         fIsHighSpeed;

    HNEMUUSBIDCDEV                  hMonDev;
    PFILE_OBJECT                    pOwner;
} NEMUUSB_RT, *PNEMUUSB_RT;

typedef struct NEMUUSBRT_IDC
{
    PDEVICE_OBJECT pDevice;
    PFILE_OBJECT pFile;
} NEMUUSBRT_IDC, *PNEMUUSBRT_IDC;

DECLHIDDEN(NTSTATUS) nemuUsbRtGlobalsInit();
DECLHIDDEN(VOID) nemuUsbRtGlobalsTerm();

DECLHIDDEN(NTSTATUS) nemuUsbRtInit(PNEMUUSBDEV_EXT pDevExt);
DECLHIDDEN(VOID) nemuUsbRtClear(PNEMUUSBDEV_EXT pDevExt);
DECLHIDDEN(NTSTATUS) nemuUsbRtRm(PNEMUUSBDEV_EXT pDevExt);
DECLHIDDEN(NTSTATUS) nemuUsbRtStart(PNEMUUSBDEV_EXT pDevExt);

DECLHIDDEN(NTSTATUS) nemuUsbRtDispatch(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp);
DECLHIDDEN(NTSTATUS) nemuUsbRtCreate(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp);
DECLHIDDEN(NTSTATUS) nemuUsbRtClose(PNEMUUSBDEV_EXT pDevExt, PIRP pIrp);

#endif /* #ifndef ___NemuUsbRt_h___ */
