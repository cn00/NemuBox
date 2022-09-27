/* $Id: NemuUsbIdc.h $ */
/** @file
 * Windows USB Proxy - Monitor Driver communication interface.
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
#ifndef ___NemuUsbIdc_h___
#define ___NemuUsbIdc_h___

#define NEMUUSBIDC_VERSION_MAJOR 1
#define NEMUUSBIDC_VERSION_MINOR 0

#define NEMUUSBIDC_INTERNAL_IOCTL_GET_VERSION         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x618, METHOD_NEITHER, FILE_WRITE_ACCESS)
#define NEMUUSBIDC_INTERNAL_IOCTL_PROXY_STARTUP       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x619, METHOD_NEITHER, FILE_WRITE_ACCESS)
#define NEMUUSBIDC_INTERNAL_IOCTL_PROXY_TEARDOWN      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x61A, METHOD_NEITHER, FILE_WRITE_ACCESS)
#define NEMUUSBIDC_INTERNAL_IOCTL_PROXY_STATE_CHANGE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x61B, METHOD_NEITHER, FILE_WRITE_ACCESS)

typedef struct
{
    uint32_t        u32Major;
    uint32_t        u32Minor;
} NEMUUSBIDC_VERSION, *PNEMUUSBIDC_VERSION;

typedef void *HNEMUUSBIDCDEV;

/* the initial device state is USBDEVICESTATE_HELD_BY_PROXY */
typedef struct NEMUUSBIDC_PROXY_STARTUP
{
    union
    {
        /* in: device PDO */
        PDEVICE_OBJECT pPDO;
        /* out: device handle to be used for subsequent USBSUP_PROXY_XXX calls */
        HNEMUUSBIDCDEV hDev;
    } u;
} NEMUUSBIDC_PROXY_STARTUP, *PNEMUUSBIDC_PROXY_STARTUP;

typedef struct NEMUUSBIDC_PROXY_TEARDOWN
{
    HNEMUUSBIDCDEV hDev;
} NEMUUSBIDC_PROXY_TEARDOWN, *PNEMUUSBIDC_PROXY_TEARDOWN;

typedef enum
{
    NEMUUSBIDC_PROXY_STATE_UNKNOWN = 0,
    NEMUUSBIDC_PROXY_STATE_IDLE,
    NEMUUSBIDC_PROXY_STATE_INITIAL = NEMUUSBIDC_PROXY_STATE_IDLE,
    NEMUUSBIDC_PROXY_STATE_USED_BY_GUEST
} NEMUUSBIDC_PROXY_STATE;

typedef struct NEMUUSBIDC_PROXY_STATE_CHANGE
{
    HNEMUUSBIDCDEV hDev;
    NEMUUSBIDC_PROXY_STATE enmState;
} NEMUUSBIDC_PROXY_STATE_CHANGE, *PNEMUUSBIDC_PROXY_STATE_CHANGE;

NTSTATUS NemuUsbIdcInit();
VOID NemuUsbIdcTerm();
NTSTATUS NemuUsbIdcProxyStarted(PDEVICE_OBJECT pPDO, HNEMUUSBIDCDEV *phDev);
NTSTATUS NemuUsbIdcProxyStopped(HNEMUUSBIDCDEV hDev);

#endif /* #ifndef ___NemuUsbIdc_h___ */
