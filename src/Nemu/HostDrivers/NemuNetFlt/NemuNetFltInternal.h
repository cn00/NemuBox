/* $Id: NemuNetFltInternal.h $ */
/** @file
 * NemuNetFlt - Network Filter Driver (Host), Internal Header.
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

#ifndef ___NemuNetFltInternal_h___
#define ___NemuNetFltInternal_h___

#include <Nemu/sup.h>
#include <Nemu/intnet.h>
#include <iprt/semaphore.h>
#include <iprt/assert.h>


RT_C_DECLS_BEGIN

/** Pointer to the globals. */
typedef struct NEMUNETFLTGLOBALS *PNEMUNETFLTGLOBALS;


/**
 * The state of a filter driver instance.
 *
 * The state machine differs a bit between the platforms because of
 * the way we hook into the stack. On some hosts we can dynamically
 * attach when required (on CreateInstance) and on others we will
 * have to connect when the network stack is bound up. These modes
 * are called static and dynamic config and governed at compile time
 * by the NEMUNETFLT_STATIC_CONFIG define.
 *
 * See sec_netflt_msc for more details on locking and synchronization.
 */
typedef enum NEMUNETFTLINSSTATE
{
    /** The usual invalid state. */
    kNemuNetFltInsState_Invalid = 0,
    /** Initialization.
     * We've reserved the interface name but need to attach to the actual
     * network interface outside the lock to avoid deadlocks.
     * In the dynamic case this happens during a Create(Instance) call.
     * In the static case it happens during driver initialization. */
    kNemuNetFltInsState_Initializing,
#ifdef NEMUNETFLT_STATIC_CONFIG
    /** Unconnected, not hooked up to a switch (static only).
     * The filter driver instance has been instantiated and hooked up,
     * waiting to be connected to an internal network. */
    kNemuNetFltInsState_Unconnected,
#endif
    /** Connected to an internal network. */
    kNemuNetFltInsState_Connected,
    /** Disconnecting from the internal network and possibly the host network interface.
     * Partly for reasons of deadlock avoidance again. */
    kNemuNetFltInsState_Disconnecting,
    /** The instance has been disconnected from both the host and the internal network. */
    kNemuNetFltInsState_Destroyed,

    /** The habitual 32-bit enum hack.  */
    kNemuNetFltInsState_32BitHack = 0x7fffffff
} NEMUNETFTLINSSTATE;


/**
 * The per-instance data of the Nemu filter driver.
 *
 * This is data associated with a network interface / NIC / wossname which
 * the filter driver has been or may be attached to. When possible it is
 * attached dynamically, but this may not be possible on all OSes so we have
 * to be flexible about things.
 *
 * A network interface / NIC / wossname can only have one filter driver
 * instance attached to it. So, attempts at connecting an internal network
 * to an interface that's already in use (connected to another internal network)
 * will result in a VERR_SHARING_VIOLATION.
 *
 * Only one internal network can connect to a filter driver instance.
 */
typedef struct NEMUNETFLTINS
{
    /** Pointer to the next interface in the list. (NEMUNETFLTGLOBAL::pInstanceHead) */
    struct NEMUNETFLTINS *pNext;
    /** Our RJ-45 port.
     * This is what the internal network plugs into. */
    INTNETTRUNKIFPORT MyPort;
    /** The RJ-45 port on the INTNET "switch".
     * This is what we're connected to. */
    PINTNETTRUNKSWPORT pSwitchPort;
    /** Pointer to the globals. */
    PNEMUNETFLTGLOBALS pGlobals;

    /** The spinlock protecting the state variables and host interface handle. */
    RTSPINLOCK hSpinlock;
    /** The current interface state. */
    NEMUNETFTLINSSTATE volatile enmState;
    /** The trunk state. */
    INTNETTRUNKIFSTATE volatile enmTrunkState;
    bool volatile fActive;
    /** Disconnected from the host network interface. */
    bool volatile fDisconnectedFromHost;
    /** Rediscovery is pending.
     * cBusy will never reach zero during rediscovery, so which
     * takes care of serializing rediscovery and disconnecting. */
    bool volatile fRediscoveryPending;
    /** Whether we should not attempt to set promiscuous mode at all. */
    bool fDisablePromiscuous;
#if (ARCH_BITS == 32) && defined(__GNUC__)
#if 0
    uint32_t u32Padding;    /**< Alignment padding, will assert in ASMAtomicUoWriteU64 otherwise. */
#endif
#endif
    /** The timestamp of the last rediscovery. */
    uint64_t volatile NanoTSLastRediscovery;
    /** Reference count. */
    uint32_t volatile cRefs;
    /** The busy count.
     * This counts the number of current callers and pending packet. */
    uint32_t volatile cBusy;
    /** The event that is signaled when we go idle and that pfnWaitForIdle blocks on. */
    RTSEMEVENT hEventIdle;

    /** @todo move MacAddr out of this structure!  */
    union
    {
#ifdef NEMUNETFLT_OS_SPECFIC
        struct
        {
# if defined(RT_OS_DARWIN)
            /** @name Darwin instance data.
             * @{ */
            /** Pointer to the darwin network interface we're attached to.
             * This is treated as highly volatile and should only be read and retained
             * while owning hSpinlock. Releasing references to this should not be done
             * while owning it though as we might end up destroying it in some paths. */
            ifnet_t volatile pIfNet;
            /** The interface filter handle.
             * Same access rules as with pIfNet. */
            interface_filter_t volatile pIfFilter;
            /** Whether we've need to set promiscuous mode when the interface comes up. */
            bool volatile fNeedSetPromiscuous;
            /** Whether we've successfully put the interface into to promiscuous mode.
             * This is for dealing with the ENETDOWN case. */
            bool volatile fSetPromiscuous;
            /** The MAC address of the interface. */
            RTMAC MacAddr;
            /** PF_SYSTEM socket to listen for events (XXX: globals?) */
            socket_t pSysSock;
            /** @} */
# elif defined(RT_OS_LINUX)
            /** @name Linux instance data
             * @{ */
            /** Pointer to the device. */
            struct net_device * volatile pDev;
            /** MTU of host's interface. */
            uint32_t cbMtu;
            /** Whether we've successfully put the interface into to promiscuous mode.
             * This is for dealing with the ENETDOWN case. */
            bool volatile fPromiscuousSet;
            /** Whether device exists and physically attached. */
            bool volatile fRegistered;
            /** Whether our packet handler is installed. */
            bool volatile fPacketHandler;
            /** The MAC address of the interface. */
            RTMAC MacAddr;
            struct notifier_block Notifier; /* netdevice */
            struct notifier_block NotifierIPv4;
            struct notifier_block NotifierIPv6;
            struct packet_type    PacketType;
#  ifndef NEMUNETFLT_LINUX_NO_XMIT_QUEUE
            struct sk_buff_head   XmitQueue;
            struct work_struct    XmitTask;
#  endif
            /** @} */
# elif defined(RT_OS_SOLARIS)
            /** @name Solaris instance data.
             * @{ */
#  ifdef NEMU_WITH_NETFLT_CROSSBOW
            /** Whether the underlying interface is a VNIC or not. */
            bool fIsVNIC;
            /** Whether the underlying interface is a VNIC template or not. */
            bool fIsVNICTemplate;
            /** Handle to list of created VNICs. */
            list_t hVNICs;
            /** The MAC address of the host interface. */
            RTMAC MacAddr;
            /** Handle of this interface (lower MAC). */
            mac_handle_t hInterface;
            /** Handle to link state notifier. */
            mac_notify_handle_t hNotify;
#  else
            /** Pointer to the bound IPv4 stream. */
            struct nemunetflt_stream_t * volatile pIp4Stream;
            /** Pointer to the bound IPv6 stream. */
            struct nemunetflt_stream_t * volatile pIp6Stream;
            /** Pointer to the bound ARP stream. */
            struct nemunetflt_stream_t * volatile pArpStream;
            /** Pointer to the unbound promiscuous stream. */
            struct nemunetflt_promisc_stream_t * volatile pPromiscStream;
            /** Whether we are attaching to IPv6 stream dynamically now. */
            bool volatile fAttaching;
            /** Whether this is a VLAN interface or not. */
            bool volatile fVLAN;
            /** Layered device handle to the interface. */
            ldi_handle_t hIface;
            /** The MAC address of the interface. */
            RTMAC MacAddr;
            /** Mutex protection used for loopback. */
            kmutex_t hMtx;
            /** Mutex protection used for dynamic IPv6 attaches. */
            RTSEMFASTMUTEX hPollMtx;
#  endif
            /** @} */
# elif defined(RT_OS_FREEBSD)
            /** @name FreeBSD instance data.
             * @{ */
            /** Interface handle */
            struct ifnet *ifp;
            /** Netgraph node handle */
            node_p node;
            /** Input hook */
            hook_p input;
            /** Output hook */
            hook_p output;
            /** Original interface flags */
            unsigned int flags;
            /** Input queue */
            struct ifqueue inq;
            /** Output queue */
            struct ifqueue outq;
            /** Input task */
            struct task tskin;
            /** Output task */
            struct task tskout;
            /** The MAC address of the interface. */
            RTMAC MacAddr;
            /** @} */
# elif defined(RT_OS_WINDOWS)
            /** @name Windows instance data.
             * @{ */
            /** Filter driver device context. */
            NEMUNETFLTWIN WinIf;

            volatile uint32_t cModeNetFltRefs;
            volatile uint32_t cModePassThruRefs;
#ifndef NEMUNETFLT_NO_PACKET_QUEUE
            /** Packet worker thread info */
            PACKET_QUEUE_WORKER PacketQueueWorker;
#endif
            /** The MAC address of the interface. Caching MAC for performance reasons. */
            RTMAC MacAddr;
            /** mutex used to synchronize WinIf init/deinit */
            RTSEMMUTEX hWinIfMutex;
            /** @}  */
# else
#  error "PORTME"
# endif
        } s;
#endif
        /** Padding. */
#if defined(RT_OS_WINDOWS)
# if defined(NEMU_NETFLT_ONDEMAND_BIND)
        uint8_t abPadding[192];
# elif defined(NEMUNETADP)
        uint8_t abPadding[256];
# else
        uint8_t abPadding[1024];
# endif
#elif defined(RT_OS_LINUX)
        uint8_t abPadding[320];
#elif defined(RT_OS_FREEBSD)
        uint8_t abPadding[320];
#else
        uint8_t abPadding[128];
#endif
    } u;

    /** The interface name. */
    char szName[1];
} NEMUNETFLTINS;
/** Pointer to the instance data of a host network filter driver. */
typedef struct NEMUNETFLTINS *PNEMUNETFLTINS;

AssertCompileMemberAlignment(NEMUNETFLTINS, NanoTSLastRediscovery, 8);
#ifdef NEMUNETFLT_OS_SPECFIC
AssertCompile(RT_SIZEOFMEMB(NEMUNETFLTINS, u.s) <= RT_SIZEOFMEMB(NEMUNETFLTINS, u.abPadding));
#endif


/**
 * The global data of the Nemu filter driver.
 *
 * This contains the bit required for communicating with support driver, NemuDrv
 * (start out as SupDrv).
 */
typedef struct NEMUNETFLTGLOBALS
{
    /** Mutex protecting the list of instances and state changes. */
    RTSEMFASTMUTEX hFastMtx;
    /** Pointer to a list of instance data. */
    PNEMUNETFLTINS pInstanceHead;

    /** The INTNET trunk network interface factory. */
    INTNETTRUNKFACTORY TrunkFactory;
    /** The SUPDRV component factory registration. */
    SUPDRVFACTORY SupDrvFactory;
    /** The number of current factory references. */
    int32_t volatile cFactoryRefs;
    /** Whether the IDC connection is open or not.
     * This is only for cleaning up correctly after the separate IDC init on Windows. */
    bool fIDCOpen;
    /** The SUPDRV IDC handle (opaque struct). */
    SUPDRVIDCHANDLE SupDrvIDC;
} NEMUNETFLTGLOBALS;


DECLHIDDEN(int) nemuNetFltInitGlobalsAndIdc(PNEMUNETFLTGLOBALS pGlobals);
DECLHIDDEN(int) nemuNetFltInitGlobals(PNEMUNETFLTGLOBALS pGlobals);
DECLHIDDEN(int) nemuNetFltInitIdc(PNEMUNETFLTGLOBALS pGlobals);
DECLHIDDEN(int) nemuNetFltTryDeleteIdcAndGlobals(PNEMUNETFLTGLOBALS pGlobals);
DECLHIDDEN(void) nemuNetFltDeleteGlobals(PNEMUNETFLTGLOBALS pGlobals);
DECLHIDDEN(int) nemuNetFltTryDeleteIdc(PNEMUNETFLTGLOBALS pGlobals);

DECLHIDDEN(bool) nemuNetFltCanUnload(PNEMUNETFLTGLOBALS pGlobals);
DECLHIDDEN(PNEMUNETFLTINS) nemuNetFltFindInstance(PNEMUNETFLTGLOBALS pGlobals, const char *pszName);

DECLHIDDEN(DECLCALLBACK(void)) nemuNetFltPortReleaseBusy(PINTNETTRUNKIFPORT pIfPort);
DECLHIDDEN(void) nemuNetFltRetain(PNEMUNETFLTINS pThis, bool fBusy);
DECLHIDDEN(bool) nemuNetFltTryRetainBusyActive(PNEMUNETFLTINS pThis);
DECLHIDDEN(bool) nemuNetFltTryRetainBusyNotDisconnected(PNEMUNETFLTINS pThis);
DECLHIDDEN(void) nemuNetFltRelease(PNEMUNETFLTINS pThis, bool fBusy);

#ifdef NEMUNETFLT_STATIC_CONFIG
DECLHIDDEN(int) nemuNetFltSearchCreateInstance(PNEMUNETFLTGLOBALS pGlobals, const char *pszName, PNEMUNETFLTINS *ppInstance, void * pContext);
#endif



/** @name The OS specific interface.
 * @{ */
/**
 * Try rediscover the host interface.
 *
 * This is called periodically from the transmit path if we're marked as
 * disconnected from the host. There is no chance of a race here.
 *
 * @returns true if the interface was successfully rediscovered and reattach,
 *          otherwise false.
 * @param   pThis           The new instance.
 */
DECLHIDDEN(bool) nemuNetFltOsMaybeRediscovered(PNEMUNETFLTINS pThis);

/**
 * Transmits a frame.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 * @param   pvIfData        Pointer to the host-private interface data.
 * @param   pSG             The (scatter/)gather list.
 * @param   fDst            The destination mask. At least one bit will be set.
 *
 * @remarks Owns the out-bound trunk port semaphore.
 */
DECLHIDDEN(int) nemuNetFltPortOsXmit(PNEMUNETFLTINS pThis, void *pvIfData, PINTNETSG pSG, uint32_t fDst);

/**
 * This is called when activating or suspending the instance.
 *
 * Use this method to enable and disable promiscuous mode on
 * the interface to prevent unnecessary interrupt load.
 *
 * It is only called when the state changes.
 *
 * @param   pThis           The instance.
 * @param   fActive         Whether to active (@c true) or deactive.
 *
 * @remarks Owns the lock for the out-bound trunk port.
 */
DECLHIDDEN(void) nemuNetFltPortOsSetActive(PNEMUNETFLTINS pThis, bool fActive);

/**
 * This is called when a network interface has obtained a new MAC address.
 *
 * @param   pThis           The instance.
 * @param   pvIfData        Pointer to the private interface data.
 * @param   pMac            Pointer to the new MAC address.
 */
DECLHIDDEN(void) nemuNetFltPortOsNotifyMacAddress(PNEMUNETFLTINS pThis, void *pvIfData, PCRTMAC pMac);

/**
 * This is called when an interface is connected to the network.
 *
 * @return IPRT status code.
 * @param   pThis           The instance.
 * @param   pvIf            Pointer to the interface.
 * @param   ppvIfData       Where to store the private interface data.
 */
DECLHIDDEN(int) nemuNetFltPortOsConnectInterface(PNEMUNETFLTINS pThis, void *pvIf, void **ppvIfData);

/**
 * This is called when a VM host disconnects from the network.
 *
 * @param   pThis           The instance.
 * @param   pvIfData        Pointer to the private interface data.
 */
DECLHIDDEN(int) nemuNetFltPortOsDisconnectInterface(PNEMUNETFLTINS pThis, void *pvIfData);

/**
 * This is called to when disconnecting from a network.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks May own the semaphores for the global list, the network lock and the out-bound trunk port.
 */
DECLHIDDEN(int) nemuNetFltOsDisconnectIt(PNEMUNETFLTINS pThis);

/**
 * This is called to when connecting to a network.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks Owns the semaphores for the global list, the network lock and the out-bound trunk port.
 */
DECLHIDDEN(int) nemuNetFltOsConnectIt(PNEMUNETFLTINS pThis);

/**
 * Counter part to nemuNetFltOsInitInstance().
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks May own the semaphores for the global list, the network lock and the out-bound trunk port.
 */
DECLHIDDEN(void) nemuNetFltOsDeleteInstance(PNEMUNETFLTINS pThis);

/**
 * This is called to attach to the actual host interface
 * after linking the instance into the list.
 *
 * The MAC address as well promiscuousness and GSO capabilities should be
 * reported by this function.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 * @param   pvContext       The user supplied context in the static config only.
 *                          NULL in the dynamic config.
 *
 * @remarks Owns no locks.
 */
DECLHIDDEN(int) nemuNetFltOsInitInstance(PNEMUNETFLTINS pThis, void *pvContext);

/**
 * This is called to perform structure initializations.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks Owns no locks.
 */
DECLHIDDEN(int) nemuNetFltOsPreInitInstance(PNEMUNETFLTINS pThis);
/** @} */


RT_C_DECLS_END

#endif

