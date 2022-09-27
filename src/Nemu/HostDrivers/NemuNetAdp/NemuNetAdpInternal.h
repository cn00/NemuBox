/* $Id: NemuNetAdpInternal.h $ */
/** @file
 * NemuNetAdp - Network Filter Driver (Host), Internal Header.
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
 */

#ifndef ___NemuNetAdpInternal_h___
#define ___NemuNetAdpInternal_h___

#include <Nemu/sup.h>
#include <Nemu/intnet.h>
#include <iprt/semaphore.h>
#include <iprt/assert.h>


RT_C_DECLS_BEGIN

/** Pointer to the globals. */
typedef struct NEMUNETADPGLOBALS *PNEMUNETADPGLOBALS;

#define NEMUNETADP_MAX_INSTANCES   128
#define NEMUNETADP_MAX_UNITS       128
#define NEMUNETADP_NAME            "nemunet"
#define NEMUNETADP_MAX_NAME_LEN    32
#define NEMUNETADP_MTU             1500
#if defined(RT_OS_DARWIN)
# define NEMUNETADP_MAX_FAMILIES   4
# define NEMUNETADP_DETACH_TIMEOUT 500
#endif

#define NEMUNETADP_CTL_DEV_NAME    "nemunetctl"
#define NEMUNETADP_CTL_ADD   _IOWR('v', 1, NEMUNETADPREQ)
#define NEMUNETADP_CTL_REMOVE _IOW('v', 2, NEMUNETADPREQ)

typedef struct NemuNetAdpReq
{
    char szName[NEMUNETADP_MAX_NAME_LEN];
} NEMUNETADPREQ;
typedef NEMUNETADPREQ *PNEMUNETADPREQ;

/**
 * Void entries mark vacant slots in adapter array. Valid entries are busy slots.
 * As soon as slot is being modified its state changes to transitional.
 * An entry in transitional state must only be accessed by the thread that
 * put it to this state.
 */
/**
 * To avoid races on adapter fields we stick to the following rules:
 * - rewrite: Int net port calls are serialized
 * - No modifications are allowed on busy adapters (deactivate first)
 *     Refuse to destroy adapter until it gets to available state
 * - No transfers (thus getting busy) on inactive adapters
 * - Init sequence: void->available->connected->active
     1) Create
     2) Connect
     3) Activate
 * - Destruction sequence: active->connected->available->void
     1) Deactivate
     2) Disconnect
     3) Destroy
*/

enum NemuNetAdpState
{
    kNemuNetAdpState_Invalid,
    kNemuNetAdpState_Transitional,
    kNemuNetAdpState_Active,
    kNemuNetAdpState_U32Hack = 0xFFFFFFFF
};
typedef enum NemuNetAdpState NEMUNETADPSTATE;

struct NemuNetAdapter
{
    /** Denotes availability of this slot in adapter array. */
    NEMUNETADPSTATE   enmState;
    /** Corresponds to the digit at the end of device name. */
    int               iUnit;

    union
    {
#ifdef NEMUNETADP_OS_SPECFIC
        struct
        {
# if defined(RT_OS_DARWIN)
            /** @name Darwin instance data.
             * @{ */
            /** Event to signal detachment of interface. */
            RTSEMEVENT        hEvtDetached;
            /** Pointer to Darwin interface structure. */
            ifnet_t           pIface;
            /** MAC address. */
            RTMAC             Mac;
            /** @} */
# elif defined(RT_OS_LINUX)
            /** @name Darwin instance data.
             * @{ */
            /** Pointer to Linux network device structure. */
            struct net_device *pNetDev;
            /** @} */
# elif defined(RT_OS_FREEBSD)
            /** @name FreeBSD instance data.
             * @{ */
            struct ifnet *ifp;
            /** @} */
# else
# error PORTME
# endif
        } s;
#endif
        /** Union alignment to a pointer. */
        void *pvAlign;
        /** Padding. */
        uint8_t abPadding[64];
    } u;
    /** The interface name. */
    char szName[NEMUNETADP_MAX_NAME_LEN];
};
typedef struct NemuNetAdapter NEMUNETADP;
typedef NEMUNETADP *PNEMUNETADP;
/* Paranoia checks for alignment and padding. */
AssertCompileMemberAlignment(NEMUNETADP, u, ARCH_BITS/8);
AssertCompileMemberAlignment(NEMUNETADP, szName, ARCH_BITS/8);
AssertCompileMembersSameSize(NEMUNETADP, u, NEMUNETADP, u.abPadding);

DECLHIDDEN(int) nemuNetAdpInit(void);
DECLHIDDEN(void) nemuNetAdpShutdown(void);
DECLHIDDEN(int) nemuNetAdpCreate(PNEMUNETADP *ppNew, const char *pcszName);
DECLHIDDEN(int) nemuNetAdpDestroy(PNEMUNETADP pThis);
DECLHIDDEN(PNEMUNETADP) nemuNetAdpFindByName(const char *pszName);
DECLHIDDEN(void) nemuNetAdpComposeMACAddress(PNEMUNETADP pThis, PRTMAC pMac);


/**
 * This is called to perform OS-specific structure initializations.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks Owns no locks.
 */
DECLHIDDEN(int) nemuNetAdpOsInit(PNEMUNETADP pThis);

/**
 * Counter part to nemuNetAdpOsCreate().
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks May own the semaphores for the global list, the network lock and the out-bound trunk port.
 */
DECLHIDDEN(void) nemuNetAdpOsDestroy(PNEMUNETADP pThis);

/**
 * This is called to attach to the actual host interface
 * after linking the instance into the list.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 * @param   pMac            The MAC address to use for this instance.
 *
 * @remarks Owns no locks.
 */
DECLHIDDEN(int) nemuNetAdpOsCreate(PNEMUNETADP pThis, PCRTMAC pMac);



RT_C_DECLS_END

#endif

