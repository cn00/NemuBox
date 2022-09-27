/** $Id: NemuUSBInterface.h $ */
/** @file
 * VirtualBox USB Driver User<->Kernel Interface.
 */

/*
 * Copyright (C) 2007-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuUSBInterface_h
#define ___NemuUSBInterface_h

#include <Nemu/usbfilter.h>

/**
 * org_virtualbox_NemuUSBClient method indexes.
 */
typedef enum NEMUUSBMETHOD
{
    /** org_virtualbox_NemuUSBClient::addFilter */
    NEMUUSBMETHOD_ADD_FILTER = 0,
    /** org_virtualbox_NemuUSBClient::removeFilter */
    NEMUUSBMETHOD_REMOVE_FILTER,
    /** End/max. */
    NEMUUSBMETHOD_END
} NEMUUSBMETHOD;

/**
 * Output from a NEMUUSBMETHOD_ADD_FILTER call.
 */
typedef struct NEMUUSBADDFILTEROUT
{
    /** The ID. */
    uintptr_t       uId;
    /** The return code. */
    int             rc;
} NEMUUSBADDFILTEROUT;
/** Pointer to a NEMUUSBADDFILTEROUT. */
typedef NEMUUSBADDFILTEROUT *PNEMUUSBADDFILTEROUT;

/** Cookie used to fend off some unwanted clients to the IOService.  */
#define NEMUUSB_DARWIN_IOSERVICE_COOKIE     UINT32_C(0x62735556) /* 'VUsb' */

#endif

