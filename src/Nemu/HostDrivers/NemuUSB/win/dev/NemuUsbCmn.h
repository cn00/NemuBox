/* $Id: NemuUsbCmn.h $ */
/** @file
 * NemuUsmCmn.h - USB device. Common defs
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
#ifndef ___NemuUsbCmn_h___
#define ___NemuUsbCmn_h___

#include "../cmn/NemuDrvTool.h"
#include "../cmn/NemuUsbTool.h"

#include <iprt/cdefs.h>
#include <iprt/asm.h>

#include <Nemu/usblib-win.h>

#define NEMUUSB_CFG_IDLE_TIME_MS 5000

typedef struct NEMUUSBDEV_EXT *PNEMUUSBDEV_EXT;

RT_C_DECLS_BEGIN

#ifdef _WIN64
#define DECLSPEC_USBIMPORT                      DECLSPEC_IMPORT
#else
#define DECLSPEC_USBIMPORT

#define USBD_ParseDescriptors                   _USBD_ParseDescriptors
#define USBD_ParseConfigurationDescriptorEx     _USBD_ParseConfigurationDescriptorEx
#define USBD_CreateConfigurationRequestEx       _USBD_CreateConfigurationRequestEx
#endif

DECLSPEC_USBIMPORT PUSB_COMMON_DESCRIPTOR
USBD_ParseDescriptors(
    IN PVOID DescriptorBuffer,
    IN ULONG TotalLength,
    IN PVOID StartPosition,
    IN LONG DescriptorType
    );

DECLSPEC_USBIMPORT PUSB_INTERFACE_DESCRIPTOR
USBD_ParseConfigurationDescriptorEx(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PVOID StartPosition,
    IN LONG InterfaceNumber,
    IN LONG AlternateSetting,
    IN LONG InterfaceClass,
    IN LONG InterfaceSubClass,
    IN LONG InterfaceProtocol
    );

DECLSPEC_USBIMPORT PURB
USBD_CreateConfigurationRequestEx(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSBD_INTERFACE_LIST_ENTRY InterfaceList
    );

RT_C_DECLS_END

DECLHIDDEN(PVOID) nemuUsbMemAlloc(SIZE_T cbBytes);
DECLHIDDEN(PVOID) nemuUsbMemAllocZ(SIZE_T cbBytes);
DECLHIDDEN(VOID) nemuUsbMemFree(PVOID pvMem);

#include "NemuUsbRt.h"
#include "NemuUsbPnP.h"
#include "NemuUsbPwr.h"
#include "NemuUsbDev.h"


#endif /* #ifndef ___NemuUsbCmn_h___ */
