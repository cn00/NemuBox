/** @file
 * Shared Clipboard - Common header for host service and guest clients.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
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

#ifndef ___Nemu_HostService_NemuClipboardSvc_h
#define ___Nemu_HostService_NemuClipboardSvc_h

#include <Nemu/types.h>
#include <Nemu/VMMDev.h>
#include <Nemu/NemuGuest2.h>
#include <Nemu/hgcmsvc.h>

/*
 * The mode of operations.
 */
#define NEMU_SHARED_CLIPBOARD_MODE_OFF           0
#define NEMU_SHARED_CLIPBOARD_MODE_HOST_TO_GUEST 1
#define NEMU_SHARED_CLIPBOARD_MODE_GUEST_TO_HOST 2
#define NEMU_SHARED_CLIPBOARD_MODE_BIDIRECTIONAL 3

/*
 * Supported data formats. Bit mask.
 */
#define NEMU_SHARED_CLIPBOARD_FMT_UNICODETEXT   UINT32_C(0x01)
#define NEMU_SHARED_CLIPBOARD_FMT_BITMAP        UINT32_C(0x02)
#define NEMU_SHARED_CLIPBOARD_FMT_HTML          UINT32_C(0x04)

/*
 * The service functions which are callable by host.
 */
#define NEMU_SHARED_CLIPBOARD_HOST_FN_SET_MODE      1
/** Run headless on the host, i.e. do not touch the host clipboard. */
#define NEMU_SHARED_CLIPBOARD_HOST_FN_SET_HEADLESS  2

/*
 * The service functions which are called by guest.
 */
/* Call host and wait blocking for an host event NEMU_SHARED_CLIPBOARD_HOST_MSG_* */
#define NEMU_SHARED_CLIPBOARD_FN_GET_HOST_MSG      1
/* Send list of available formats to host. */
#define NEMU_SHARED_CLIPBOARD_FN_FORMATS           2
/* Obtain data in specified format from host. */
#define NEMU_SHARED_CLIPBOARD_FN_READ_DATA         3
/* Send data in requested format to host. */
#define NEMU_SHARED_CLIPBOARD_FN_WRITE_DATA        4

/*
 * The host messages for the guest.
 */
#define NEMU_SHARED_CLIPBOARD_HOST_MSG_QUIT        1
#define NEMU_SHARED_CLIPBOARD_HOST_MSG_READ_DATA   2
#define NEMU_SHARED_CLIPBOARD_HOST_MSG_FORMATS     3

/*
 * HGCM parameter structures.
 */
#pragma pack (1)
typedef struct _NemuClipboardGetHostMsg
{
    NemuGuestHGCMCallInfo hdr;

    /* NEMU_SHARED_CLIPBOARD_HOST_MSG_* */
    HGCMFunctionParameter msg;     /* OUT uint32_t */

    /* NEMU_SHARED_CLIPBOARD_FMT_*, depends on the 'msg'. */
    HGCMFunctionParameter formats; /* OUT uint32_t */
} NemuClipboardGetHostMsg;

#define NEMU_SHARED_CLIPBOARD_CPARMS_GET_HOST_MSG 2

typedef struct _NemuClipboardFormats
{
    NemuGuestHGCMCallInfo hdr;

    /* NEMU_SHARED_CLIPBOARD_FMT_* */
    HGCMFunctionParameter formats; /* OUT uint32_t */
} NemuClipboardFormats;

#define NEMU_SHARED_CLIPBOARD_CPARMS_FORMATS 1

typedef struct _NemuClipboardReadData
{
    NemuGuestHGCMCallInfo hdr;

    /* Requested format. */
    HGCMFunctionParameter format; /* IN uint32_t */

    /* The data buffer. */
    HGCMFunctionParameter ptr;    /* IN linear pointer. */

    /* Size of returned data, if > ptr->cb, then no data was
     * actually transferred and the guest must repeat the call.
     */
    HGCMFunctionParameter size;   /* OUT uint32_t */

} NemuClipboardReadData;

#define NEMU_SHARED_CLIPBOARD_CPARMS_READ_DATA 3

typedef struct _NemuClipboardWriteData
{
    NemuGuestHGCMCallInfo hdr;

    /* Returned format as requested in the NEMU_SHARED_CLIPBOARD_HOST_MSG_READ_DATA message. */
    HGCMFunctionParameter format; /* IN uint32_t */

    /* Data.  */
    HGCMFunctionParameter ptr;    /* IN linear pointer. */
} NemuClipboardWriteData;

#define NEMU_SHARED_CLIPBOARD_CPARMS_WRITE_DATA 2

#pragma pack ()

#endif
