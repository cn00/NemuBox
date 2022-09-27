/** @file
 * USBLib - Library for wrapping up the NemuUSB functionality, Solaris flavor.
 * (DEV,HDrv,Main)
 */

/*
 * Copyright (C) 2008-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___Nemu_usblib_solaris_h
#define ___Nemu_usblib_solaris_h

#include <Nemu/cdefs.h>
#include <Nemu/usbfilter.h>
#include <Nemu/vusb.h>
#include <sys/types.h>
#include <sys/ioccom.h>
#include <sys/param.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_usblib_solaris    Solaris USB Specifics
 * @ingroup grp_usblib
 * @{
 */

/** @name NemuUSB specific IOCtls.
 * NemuUSB uses them for resetting USB devices requests from userland.
 * USBProxyService/Device makes use of them to communicate with NemuUSB.
 * @{ */

/** Ring-3 request wrapper for big requests.
 *
 * This is necessary because the ioctl number scheme on many Unixy OSes (esp. Solaris)
 * only allows a relatively small size to be encoded into the request. So, for big
 * request this generic form is used instead. */
typedef struct NEMUUSBREQ
{
    /** Magic value (NEMUUSB(MON)_MAGIC). */
    uint32_t    u32Magic;
    /** The size of the data buffer (In & Out). */
    uint32_t    cbData;
    /** Result code of the request filled by driver. */
    int32_t     rc;
    /** The user address of the data buffer. */
    RTR3PTR     pvDataR3;
} NEMUUSBREQ;
/** Pointer to a request wrapper for solaris. */
typedef NEMUUSBREQ *PNEMUUSBREQ;
/** Pointer to a const request wrapper for solaris. */
typedef const NEMUUSBREQ *PCNEMUUSBREQ;

#pragma pack(1)
typedef struct
{
    /* Pointer to the Filter. */
    USBFILTER      Filter;
    /* Where to store the added Filter (Id). */
    uintptr_t      uId;
} NEMUUSBREQ_ADD_FILTER;

typedef struct
{
    /* Pointer to Filter (Id) to be removed. */
    uintptr_t      uId;
} NEMUUSBREQ_REMOVE_FILTER;

typedef struct
{
    /** Whether to re-attach the driver. */
    bool           fReattach;
    /* Physical path of the USB device. */
    char           szDevicePath[1];
} NEMUUSBREQ_RESET_DEVICE;

typedef struct
{
    /* Where to store the instance. */
    int           *pInstance;
    /* Physical path of the USB device. */
    char           szDevicePath[1];
} NEMUUSBREQ_DEVICE_INSTANCE;

typedef struct
{
    /** Where to store the instance. */
    int            Instance;
    /* Where to store the client path. */
    char           szClientPath[MAXPATHLEN];
    /** Device identifier (VendorId:ProductId:Release:StaticPath) */
    char           szDeviceIdent[MAXPATHLEN+48];
    /** Callback from monitor specifying client consumer (VM) credentials */
    DECLR0CALLBACKMEMBER(int, pfnSetConsumerCredentials,(RTPROCESS Process, int Instance, void *pvReserved));
} NEMUUSBREQ_CLIENT_INFO, *PNEMUUSBREQ_CLIENT_INFO;
typedef NEMUUSBREQ_CLIENT_INFO NEMUUSB_CLIENT_INFO;
typedef PNEMUUSBREQ_CLIENT_INFO PNEMUUSB_CLIENT_INFO;

/** Isoc packet descriptor (Must mirror exactly Solaris USBA's usb_isoc_pkt_descr_t) */
typedef struct
{
    ushort_t                cbPkt;              /* Size of the packet */
    ushort_t                cbActPkt;           /* Size of the packet actually transferred */
    VUSBSTATUS              enmStatus;          /* Per frame transfer status */
} VUSBISOC_PKT_DESC;

/** NemuUSB IOCtls */
typedef struct
{
    void                   *pvUrbR3;            /* Pointer to userland URB (untouched by kernel driver) */
    uint8_t                 bEndpoint;          /* Endpoint address */
    VUSBXFERTYPE            enmType;            /* Xfer type */
    VUSBDIRECTION           enmDir;             /* Xfer direction */
    VUSBSTATUS              enmStatus;          /* URB status */
    bool                    fShortOk;           /* Whether receiving less data than requested is acceptable. */
    size_t                  cbData;             /* Size of the data */
    void                   *pvData;             /* Pointer to the data */
    uint32_t                cIsocPkts;          /* Number of Isoc packets */
    VUSBISOC_PKT_DESC       aIsocPkts[8];       /* Array of Isoc packet descriptors */
} NEMUUSBREQ_URB, *PNEMUUSBREQ_URB;

typedef struct
{
    uint8_t                 bEndpoint;          /* Endpoint address */
} NEMUUSBREQ_CLEAR_EP, *PNEMUUSBREQ_CLEAR_EP;


typedef struct
{
    uint8_t                 bConfigValue;       /* Configuration value */
} NEMUUSBREQ_SET_CONFIG, *PNEMUUSBREQ_SET_CONFIG;
typedef NEMUUSBREQ_SET_CONFIG  NEMUUSBREQ_GET_CONFIG;
typedef PNEMUUSBREQ_SET_CONFIG PNEMUUSBREQ_GET_CONFIG;

typedef struct
{
    uint8_t                 bInterface;         /* Interface number */
    uint8_t                 bAlternate;         /* Alternate setting */
} NEMUUSBREQ_SET_INTERFACE, *PNEMUUSBREQ_SET_INTERFACE;

typedef enum
{
    /** Close device not a reset. */
    NEMUUSB_RESET_LEVEL_CLOSE     = 0,
    /** Hard reset resulting in device replug behaviour. */
    NEMUUSB_RESET_LEVEL_REATTACH  = 2,
    /** Device-level reset. */
    NEMUUSB_RESET_LEVEL_SOFT      = 4
} NEMUUSB_RESET_LEVEL;

typedef struct
{
    NEMUUSB_RESET_LEVEL     ResetLevel;         /* Reset level after closing */
} NEMUUSBREQ_CLOSE_DEVICE, *PNEMUUSBREQ_CLOSE_DEVICE;

typedef struct
{
    uint8_t                 bEndpoint;          /* Endpoint address */
} NEMUUSBREQ_ABORT_PIPE, *PNEMUUSBREQ_ABORT_PIPE;

typedef struct
{
    uint32_t                u32Major;           /* Driver major number */
    uint32_t                u32Minor;           /* Driver minor number */
} NEMUUSBREQ_GET_VERSION, *PNEMUUSBREQ_GET_VERSION;

#pragma pack()

/** The NEMUUSBREQ::u32Magic value for NemuUSBMon. */
#define NEMUUSBMON_MAGIC           0xba5eba11
/** The NEMUUSBREQ::u32Magic value for NemuUSB.*/
#define NEMUUSB_MAGIC              0x601fba11
/** The USBLib entry point for userland. */
#define NEMUUSB_DEVICE_NAME        "/dev/nemuusbmon"

/** The USBMonitor Major version. */
#define NEMUUSBMON_VERSION_MAJOR   2
/** The USBMonitor Minor version. */
#define NEMUUSBMON_VERSION_MINOR   1

/** The USB Major version. */
#define NEMUUSB_VERSION_MAJOR      1
/** The USB Minor version. */
#define NEMUUSB_VERSION_MINOR      1

#ifdef RT_ARCH_AMD64
# define NEMUUSB_IOCTL_FLAG     128
#elif defined(RT_ARCH_X86)
# define NEMUUSB_IOCTL_FLAG     0
#else
# error "dunno which arch this is!"
#endif

/** USB driver name*/
#define NEMUUSB_DRIVER_NAME     "nemuusb"

/* No automatic buffering, size limited to 255 bytes => use NEMUUSBREQ for everything. */
#define NEMUUSB_IOCTL_CODE(Function, Size)  _IOWRN('V', (Function) | NEMUUSB_IOCTL_FLAG, sizeof(NEMUUSBREQ))
#define NEMUUSB_IOCTL_CODE_FAST(Function)   _IO(   'V', (Function) | NEMUUSB_IOCTL_FLAG)
#define NEMUUSB_IOCTL_STRIP_SIZE(Code)      (Code)

#define NEMUUSBMON_IOCTL_ADD_FILTER         NEMUUSB_IOCTL_CODE(1, (sizeof(NemuUSBAddFilterReq)))
#define NEMUUSBMON_IOCTL_REMOVE_FILTER      NEMUUSB_IOCTL_CODE(2, (sizeof(NemuUSBRemoveFilterReq)))
#define NEMUUSBMON_IOCTL_RESET_DEVICE       NEMUUSB_IOCTL_CODE(3, (sizeof(NEMUUSBREQ_RESET_DEVICE)))
#define NEMUUSBMON_IOCTL_DEVICE_INSTANCE    NEMUUSB_IOCTL_CODE(4, (sizeof(NEMUUSBREQ_DEVICE_INSTANCE)))
#define NEMUUSBMON_IOCTL_CLIENT_INFO        NEMUUSB_IOCTL_CODE(5, (sizeof(NEMUUSBREQ_CLIENT_PATH)))
#define NEMUUSBMON_IOCTL_GET_VERSION        NEMUUSB_IOCTL_CODE(6, (sizeof(NEMUUSBREQ_GET_VERSION)))

/* NemuUSB ioctls */
#define NEMUUSB_IOCTL_SEND_URB              NEMUUSB_IOCTL_CODE(20, (sizeof(NEMUUSBREQ_URB)))            /* 1072146796 */
#define NEMUUSB_IOCTL_REAP_URB              NEMUUSB_IOCTL_CODE(21, (sizeof(NEMUUSBREQ_URB)))            /* 1072146795 */
#define NEMUUSB_IOCTL_CLEAR_EP              NEMUUSB_IOCTL_CODE(22, (sizeof(NEMUUSBREQ_CLEAR_EP)))       /* 1072146794 */
#define NEMUUSB_IOCTL_SET_CONFIG            NEMUUSB_IOCTL_CODE(23, (sizeof(NEMUUSBREQ_SET_CONFIG)))     /* 1072146793 */
#define NEMUUSB_IOCTL_SET_INTERFACE         NEMUUSB_IOCTL_CODE(24, (sizeof(NEMUUSBREQ_SET_INTERFACE)))  /* 1072146792 */
#define NEMUUSB_IOCTL_CLOSE_DEVICE          NEMUUSB_IOCTL_CODE(25, (sizeof(NEMUUSBREQ_CLOSE_DEVICE)))   /* 1072146791 0xc0185699 */
#define NEMUUSB_IOCTL_ABORT_PIPE            NEMUUSB_IOCTL_CODE(26, (sizeof(NEMUUSBREQ_ABORT_PIPE)))     /* 1072146790 */
#define NEMUUSB_IOCTL_GET_CONFIG            NEMUUSB_IOCTL_CODE(27, (sizeof(NEMUUSBREQ_GET_CONFIG)))     /* 1072146789 */
#define NEMUUSB_IOCTL_GET_VERSION           NEMUUSB_IOCTL_CODE(28, (sizeof(NEMUUSBREQ_GET_VERSION)))    /* 1072146788 */

/** @} */

/* USBLibHelper data for resetting the device. */
typedef struct NEMUUSBHELPERDATA_RESET
{
    /** Path of the USB device. */
    const char  *pszDevicePath;
    /** Re-enumerate or not. */
    bool        fHardReset;
} NEMUUSBHELPERDATA_RESET;
typedef NEMUUSBHELPERDATA_RESET *PNEMUUSBHELPERDATA_RESET;
typedef const NEMUUSBHELPERDATA_RESET *PCNEMUUSBHELPERDATA_RESET;

/* USBLibHelper data for device hijacking. */
typedef struct NEMUUSBHELPERDATA_ALIAS
{
    /** Vendor ID. */
    uint16_t        idVendor;
    /** Product ID. */
    uint16_t        idProduct;
    /** Revision, integer part. */
    uint16_t        bcdDevice;
    /** Path of the USB device. */
    const char      *pszDevicePath;
} NEMUUSBHELPERDATA_ALIAS;
typedef NEMUUSBHELPERDATA_ALIAS *PNEMUUSBHELPERDATA_ALIAS;
typedef const NEMUUSBHELPERDATA_ALIAS *PCNEMUUSBHELPERDATA_ALIAS;

USBLIB_DECL(int) USBLibResetDevice(char *pszDevicePath, bool fReattach);
USBLIB_DECL(int) USBLibDeviceInstance(char *pszDevicePath, int *pInstance);
USBLIB_DECL(int) USBLibGetClientInfo(char *pszDeviceIdent, char **ppszClientPath, int *pInstance);
USBLIB_DECL(int) USBLibAddDeviceAlias(PUSBDEVICE pDevice);
USBLIB_DECL(int) USBLibRemoveDeviceAlias(PUSBDEVICE pDevice);
/*USBLIB_DECL(int) USBLibConfigureDevice(PUSBDEVICE pDevice);*/

/** @} */
RT_C_DECLS_END

#endif

