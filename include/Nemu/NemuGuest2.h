/** @file
 * NemuGuest - VirtualBox Guest Additions Driver Interface, Mixed Up Mess.
 * (ADD,DEV)
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

#ifndef ___Nemu_NemuGuest2_h
#define ___Nemu_NemuGuest2_h

#include <iprt/assert.h>

#ifdef NEMU_WITH_HGCM
# include <Nemu/VMMDev2.h>

/** @addtogroup grp_vmmdev
 * @{ */

/**
 * HGCM connect info structure.
 *
 * This is used by NEMUGUEST_IOCTL_HGCM_CONNECT and in VbglR0.
 *
 * @ingroup grp_nemuguest
 */
# pragma pack(1) /* explicit packing for good measure. */
typedef struct NemuGuestHGCMConnectInfo
{
    int32_t result;           /**< OUT */
    HGCMServiceLocation Loc;  /**< IN */
    uint32_t u32ClientID;     /**< OUT */
} NemuGuestHGCMConnectInfo;
AssertCompileSize(NemuGuestHGCMConnectInfo, 4+4+128+4);
# pragma pack()


/**
 * HGCM connect info structure.
 *
 * This is used by NEMUGUEST_IOCTL_HGCM_DISCONNECT and in VbglR0.
 *
 * @ingroup grp_nemuguest
 */
typedef struct NemuGuestHGCMDisconnectInfo
{
    int32_t result;           /**< OUT */
    uint32_t u32ClientID;     /**< IN */
} NemuGuestHGCMDisconnectInfo;
AssertCompileSize(NemuGuestHGCMDisconnectInfo, 8);

/**
 * HGCM call info structure.
 *
 * This is used by NEMUGUEST_IOCTL_HGCM_CALL.
 *
 * @ingroup grp_nemuguest
 */
typedef struct NemuGuestHGCMCallInfo
{
    int32_t result;           /**< OUT Host HGCM return code.*/
    uint32_t u32ClientID;     /**< IN  The id of the caller. */
    uint32_t u32Function;     /**< IN  Function number. */
    uint32_t cParms;          /**< IN  How many parms. */
    /* Parameters follow in form HGCMFunctionParameter aParms[cParms] */
} NemuGuestHGCMCallInfo;
AssertCompileSize(NemuGuestHGCMCallInfo, 16);


/**
 * HGCM call info structure.
 *
 * This is used by NEMUGUEST_IOCTL_HGCM_CALL_TIMED.
 *
 * @ingroup grp_nemuguest
 */
# pragma pack(1) /* explicit packing for good measure. */
typedef struct NemuGuestHGCMCallInfoTimed
{
    uint32_t u32Timeout;         /**< IN  How long to wait for completion before cancelling the call. */
    uint32_t fInterruptible;     /**< IN  Is this request interruptible? */
    NemuGuestHGCMCallInfo info;  /**< IN/OUT The rest of the call information.  Placed after the timeout
                                  * so that the parameters follow as they would for a normal call. */
    /* Parameters follow in form HGCMFunctionParameter aParms[cParms] */
} NemuGuestHGCMCallInfoTimed;
AssertCompileSize(NemuGuestHGCMCallInfoTimed, 8+16);
# pragma pack()

/** @} */

#endif /* NEMU_WITH_HGCM */

#endif

