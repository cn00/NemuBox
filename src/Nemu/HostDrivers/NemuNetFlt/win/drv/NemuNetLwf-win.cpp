/* $Id: NemuNetLwf-win.cpp $ */
/** @file
 * NemuNetLwf-win.cpp - NDIS6 Bridged Networking Driver, Windows-specific code.
 */
/*
 * Copyright (C) 2014-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#define LOG_GROUP LOG_GROUP_NET_FLT_DRV

//#define NEMUNETLWF_SYNC_SEND

#include <Nemu/version.h>
#include <Nemu/err.h>
#include <iprt/initterm.h>
#include <iprt/net.h>
#include <iprt/list.h>
#include <Nemu/intnetinline.h>

/// @todo Not sure why, but can it help with build errors?
RT_C_DECLS_BEGIN
/* ntddk.h has a missing #pragma pack(), work around it
 * see #ifdef NEMU_WITH_WORKAROUND_MISSING_PACK below for detail */
#define NEMU_WITH_WORKAROUND_MISSING_PACK
#if (_MSC_VER >= 1400) && !defined(NEMU_WITH_PATCHED_DDK)
#  define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
#  define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
#  define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
#  define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
#  define _interlockedbittestandset      _interlockedbittestandset_StupidDDKVsCompilerCrap
#  define _interlockedbittestandreset    _interlockedbittestandreset_StupidDDKVsCompilerCrap
#  define _interlockedbittestandset64    _interlockedbittestandset64_StupidDDKVsCompilerCrap
#  define _interlockedbittestandreset64  _interlockedbittestandreset64_StupidDDKVsCompilerCrap
#  pragma warning(disable : 4163)
#  ifdef NEMU_WITH_WORKAROUND_MISSING_PACK
#    pragma warning(disable : 4103)
#  endif
#  include <ntddk.h>
#  pragma warning(default : 4163)
#  ifdef NEMU_WITH_WORKAROUND_MISSING_PACK
#    pragma pack()
#    pragma warning(default : 4103)
#  endif
#  undef  _InterlockedExchange
#  undef  _InterlockedExchangeAdd
#  undef  _InterlockedCompareExchange
#  undef  _InterlockedAddLargeStatistic
#  undef  _interlockedbittestandset
#  undef  _interlockedbittestandreset
#  undef  _interlockedbittestandset64
#  undef  _interlockedbittestandreset64
#  include <ndis.h>
#  include <netioapi.h>
#else
#  include <ntddk.h>
#  include <netioapi.h>
/* can include ndis.h right away */
#  include <ndis.h>
#endif
#include <mstcpip.h>
RT_C_DECLS_END

#if 0
#undef Log
#define Log(x) DbgPrint x
#undef LogFlow
#define LogFlow(x) DbgPrint x
#endif

/** We have an entirely different structure than the one defined in NemuNetFltCmn-win.h */
typedef struct NEMUNETFLTWIN
{
    /** filter module context handle */
    NDIS_HANDLE hModuleCtx;
    /** IP address change notifier handle */
    HANDLE hNotifier; /* Must be here as hModuleCtx may already be NULL when nemuNetFltOsDeleteInstance is called */
} NEMUNETFLTWIN, *PNEMUNETFLTWIN;
#define NEMUNETFLT_NO_PACKET_QUEUE
#define NEMUNETFLT_OS_SPECFIC 1
#include "NemuNetFltInternal.h"

#include "NemuNetLwf-win.h"
#include "Nemu/NemuNetCmn-win.h"

/* Forward declarations */
FILTER_ATTACH nemuNetLwfWinAttach;
FILTER_DETACH nemuNetLwfWinDetach;
FILTER_RESTART nemuNetLwfWinRestart;
FILTER_PAUSE nemuNetLwfWinPause;
FILTER_OID_REQUEST nemuNetLwfWinOidRequest;
FILTER_OID_REQUEST_COMPLETE nemuNetLwfWinOidRequestComplete;
//FILTER_CANCEL_OID_REQUEST nemuNetLwfWinCancelOidRequest;
FILTER_STATUS nemuNetLwfWinStatus;
//FILTER_NET_PNP_EVENT nemuNetLwfWinPnPEvent;
FILTER_SEND_NET_BUFFER_LISTS nemuNetLwfWinSendNetBufferLists;
FILTER_SEND_NET_BUFFER_LISTS_COMPLETE nemuNetLwfWinSendNetBufferListsComplete;
FILTER_RECEIVE_NET_BUFFER_LISTS nemuNetLwfWinReceiveNetBufferLists;
FILTER_RETURN_NET_BUFFER_LISTS nemuNetLwfWinReturnNetBufferLists;
KSTART_ROUTINE nemuNetLwfWinInitIdcWorker;

typedef enum {
    LwfState_Detached = 0,
    LwfState_Attaching,
    LwfState_Paused,
    LwfState_Restarting,
    LwfState_Running,
    LwfState_Pausing,
    LwfState_32BitHack = 0x7fffffff
} NEMUNETLWFSTATE;

/*
 * Valid state transitions are:
 * 1) Disconnected -> Connecting   : start the worker thread, attempting to init IDC;
 * 2) Connecting   -> Disconnected : failed to start IDC init worker thread;
 * 3) Connecting   -> Connected    : IDC init successful, terminate the worker;
 * 4) Connecting   -> Stopping     : IDC init incomplete, but the driver is being unloaded, terminate the worker;
 * 5) Connected    -> Stopping     : IDC init was successful, no worker, the driver is being unloaded;
 *
 * Driver terminates in Stopping state.
 */
typedef enum {
    LwfIdcState_Disconnected = 0, /* Initial state */
    LwfIdcState_Connecting,       /* Attemping to init IDC, worker thread running */
    LwfIdcState_Connected,        /* Successfully connected to IDC, worker thread terminated */
    LwfIdcState_Stopping          /* Terminating the worker thread and disconnecting IDC */
} NEMUNETLWFIDCSTATE;

struct _NEMUNETLWF_MODULE;

typedef struct NEMUNETLWFGLOBALS
{
    /** synch event used for device creation synchronization */
    //KEVENT SynchEvent;
    /** Device reference count */
    //int cDeviceRefs;
    /** ndis device */
    NDIS_HANDLE hDevice;
    /** device object */
    PDEVICE_OBJECT pDevObj;
    /** our filter driver handle */
    NDIS_HANDLE hFilterDriver;
    /** lock protecting the module list */
    NDIS_SPIN_LOCK Lock;
    /** the head of module list */
    RTLISTANCHOR listModules;
    /** IDC initialization state */
    volatile uint32_t enmIdcState;
    /** IDC init thread handle */
    HANDLE hInitIdcThread;
} NEMUNETLWFGLOBALS, *PNEMUNETLWFGLOBALS;

/**
 * The (common) global data.
 */
static NEMUNETFLTGLOBALS g_NemuNetFltGlobals;
/* win-specific global data */
NEMUNETLWFGLOBALS g_NemuNetLwfGlobals;

typedef struct _NEMUNETLWF_MODULE {
    RTLISTNODE node;

    NDIS_HANDLE hFilter;
    NDIS_HANDLE hPool;
    PNEMUNETLWFGLOBALS pGlobals;
    /** Associated instance of NetFlt, one-to-one relationship */
    PNEMUNETFLTINS pNetFlt; /// @todo Consider automic access!
    /** Module state as described in http://msdn.microsoft.com/en-us/library/windows/hardware/ff550017(v=vs.85).aspx */
    volatile uint32_t enmState; /* No lock needed yet, atomic should suffice. */
    /** Mutex to prevent pausing while transmitting on behalf of NetFlt */
    NDIS_MUTEX InTransmit;
#ifdef NEMUNETLWF_SYNC_SEND
    /** Event signalled when sending to the wire is complete */
    KEVENT EventWire;
    /** Event signalled when NDIS returns our receive notification */
    KEVENT EventHost;
#else /* !NEMUNETLWF_SYNC_SEND */
    /** Event signalled when all pending sends (both to wire and host) have completed */
    NDIS_EVENT EventSendComplete;
    /** Counter for pending sends (both to wire and host) */
    int32_t cPendingBuffers;
    /** Work Item to deliver offloading indications at passive IRQL */
    NDIS_HANDLE hWorkItem;
#endif /* !NEMUNETLWF_SYNC_SEND */
    /** MAC address of underlying adapter */
    RTMAC MacAddr;
    /** Saved offload configuration */
    NDIS_OFFLOAD SavedOffloadConfig;
    /** the cloned request we have passed down */
    PNDIS_OID_REQUEST pPendingRequest;
    /** true if the underlying miniport supplied offloading config */
    bool fOffloadConfigValid;
    /** true if the trunk expects data from us */
    bool fActive;
    /** true if the host wants the adapter to be in promisc mode */
    bool fHostPromisc;
    /** Name of underlying adapter */
    char szMiniportName[1];
} NEMUNETLWF_MODULE;
typedef NEMUNETLWF_MODULE *PNEMUNETLWF_MODULE;

/*
 * A structure to wrap OID requests in.
 */
typedef struct _NEMUNETLWF_OIDREQ {
    NDIS_OID_REQUEST Request;
    NDIS_STATUS Status;
    NDIS_EVENT Event;
} NEMUNETLWF_OIDREQ;
typedef NEMUNETLWF_OIDREQ *PNEMUNETLWF_OIDREQ;

/* Forward declarations */
static VOID nemuNetLwfWinUnloadDriver(IN PDRIVER_OBJECT pDriver);
static int nemuNetLwfWinInitBase();
static int nemuNetLwfWinFini();

#ifdef DEBUG
static const char *nemuNetLwfWinStatusToText(NDIS_STATUS code)
{
    switch (code)
    {
        case NDIS_STATUS_MEDIA_CONNECT: return "NDIS_STATUS_MEDIA_CONNECT";
        case NDIS_STATUS_MEDIA_DISCONNECT: return "NDIS_STATUS_MEDIA_DISCONNECT";
        case NDIS_STATUS_RESET_START: return "NDIS_STATUS_RESET_START";
        case NDIS_STATUS_RESET_END: return "NDIS_STATUS_RESET_END";
        case NDIS_STATUS_MEDIA_BUSY: return "NDIS_STATUS_MEDIA_BUSY";
        case NDIS_STATUS_MEDIA_SPECIFIC_INDICATION: return "NDIS_STATUS_MEDIA_SPECIFIC_INDICATION";
        case NDIS_STATUS_LINK_SPEED_CHANGE: return "NDIS_STATUS_LINK_SPEED_CHANGE";
        case NDIS_STATUS_LINK_STATE: return "NDIS_STATUS_LINK_STATE";
        case NDIS_STATUS_PORT_STATE: return "NDIS_STATUS_PORT_STATE";
        case NDIS_STATUS_OPER_STATUS: return "NDIS_STATUS_OPER_STATUS";
        case NDIS_STATUS_NETWORK_CHANGE: return "NDIS_STATUS_NETWORK_CHANGE";
        case NDIS_STATUS_PACKET_FILTER: return "NDIS_STATUS_PACKET_FILTER";
        case NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG: return "NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG";
        case NDIS_STATUS_TASK_OFFLOAD_HARDWARE_CAPABILITIES: return "NDIS_STATUS_TASK_OFFLOAD_HARDWARE_CAPABILITIES";
        case NDIS_STATUS_OFFLOAD_ENCASPULATION_CHANGE: return "NDIS_STATUS_OFFLOAD_ENCASPULATION_CHANGE";
        case NDIS_STATUS_TCP_CONNECTION_OFFLOAD_HARDWARE_CAPABILITIES: return "NDIS_STATUS_TCP_CONNECTION_OFFLOAD_HARDWARE_CAPABILITIES";
    }
    return "unknown";
}

static void nemuNetLwfWinDumpFilterTypes(ULONG uFlags)
{
    if (uFlags & NDIS_PACKET_TYPE_DIRECTED) Log5(("   NDIS_PACKET_TYPE_DIRECTED\n"));
    if (uFlags & NDIS_PACKET_TYPE_MULTICAST) Log5(("   NDIS_PACKET_TYPE_MULTICAST\n"));
    if (uFlags & NDIS_PACKET_TYPE_ALL_MULTICAST) Log5(("   NDIS_PACKET_TYPE_ALL_MULTICAST\n"));
    if (uFlags & NDIS_PACKET_TYPE_BROADCAST) Log5(("   NDIS_PACKET_TYPE_BROADCAST\n"));
    if (uFlags & NDIS_PACKET_TYPE_PROMISCUOUS) Log5(("   NDIS_PACKET_TYPE_PROMISCUOUS\n"));
    if (uFlags & NDIS_PACKET_TYPE_ALL_FUNCTIONAL) Log5(("   NDIS_PACKET_TYPE_ALL_FUNCTIONAL\n"));
    if (uFlags & NDIS_PACKET_TYPE_ALL_LOCAL) Log5(("   NDIS_PACKET_TYPE_ALL_LOCAL\n"));
    if (uFlags & NDIS_PACKET_TYPE_FUNCTIONAL) Log5(("   NDIS_PACKET_TYPE_FUNCTIONAL\n"));
    if (uFlags & NDIS_PACKET_TYPE_GROUP) Log5(("   NDIS_PACKET_TYPE_GROUP\n"));
    if (uFlags & NDIS_PACKET_TYPE_MAC_FRAME) Log5(("   NDIS_PACKET_TYPE_MAC_FRAME\n"));
    if (uFlags & NDIS_PACKET_TYPE_SMT) Log5(("   NDIS_PACKET_TYPE_SMT\n"));
    if (uFlags & NDIS_PACKET_TYPE_SOURCE_ROUTING) Log5(("   NDIS_PACKET_TYPE_SOURCE_ROUTING\n"));
    if (uFlags == 0) Log5(("   NONE\n"));
}

DECLINLINE(void) nemuNetLwfWinDumpEncapsulation(const char *pcszText, ULONG uEncapsulation)
{
    if (uEncapsulation == NDIS_ENCAPSULATION_NOT_SUPPORTED)
        Log5(("%s not supported\n", pcszText));
    else
    {
        Log5(("%s", pcszText));
        if (uEncapsulation & NDIS_ENCAPSULATION_NULL)
            Log5((" null"));
        if (uEncapsulation & NDIS_ENCAPSULATION_IEEE_802_3)
            Log5((" 802.3"));
        if (uEncapsulation & NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q)
            Log5((" 802.3pq"));
        if (uEncapsulation & NDIS_ENCAPSULATION_IEEE_802_3_P_AND_Q_IN_OOB)
            Log5((" 802.3pq(oob)"));
        if (uEncapsulation & NDIS_ENCAPSULATION_IEEE_LLC_SNAP_ROUTED)
            Log5((" LLC"));
        Log5(("\n"));
    }
}

DECLINLINE(const char *) nemuNetLwfWinSetOnOffText(ULONG uOnOff)
{
    switch (uOnOff)
    {
        case NDIS_OFFLOAD_SET_NO_CHANGE: return "no change";
        case NDIS_OFFLOAD_SET_ON: return "on";
        case NDIS_OFFLOAD_SET_OFF: return "off";
    }
    return "unknown";
}

DECLINLINE(const char *) nemuNetLwfWinOnOffText(ULONG uOnOff)
{
    switch (uOnOff)
    {
        case NDIS_OFFLOAD_NOT_SUPPORTED: return "off";
        case NDIS_OFFLOAD_SUPPORTED: return "on";
    }
    return "unknown";
}

DECLINLINE(const char *) nemuNetLwfWinSupportedText(ULONG uSupported)
{
    switch (uSupported)
    {
        case NDIS_OFFLOAD_NOT_SUPPORTED: return "not supported";
        case NDIS_OFFLOAD_SUPPORTED: return "supported";
    }
    return "unknown";
}

static void nemuNetLwfWinDumpSetOffloadSettings(PNDIS_OFFLOAD pOffloadConfig)
{
    nemuNetLwfWinDumpEncapsulation("   Checksum.IPv4Transmit.Encapsulation               =", pOffloadConfig->Checksum.IPv4Transmit.Encapsulation);
    Log5(("   Checksum.IPv4Transmit.IpOptionsSupported          = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Transmit.IpOptionsSupported)));
    Log5(("   Checksum.IPv4Transmit.TcpOptionsSupported         = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Transmit.TcpOptionsSupported)));
    Log5(("   Checksum.IPv4Transmit.TcpChecksum                 = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Transmit.TcpChecksum)));
    Log5(("   Checksum.IPv4Transmit.UdpChecksum                 = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Transmit.UdpChecksum)));
    Log5(("   Checksum.IPv4Transmit.IpChecksum                  = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Transmit.IpChecksum)));
    nemuNetLwfWinDumpEncapsulation("   Checksum.IPv4Receive.Encapsulation                =", pOffloadConfig->Checksum.IPv4Receive.Encapsulation);
    Log5(("   Checksum.IPv4Receive.IpOptionsSupported           = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Receive.IpOptionsSupported)));
    Log5(("   Checksum.IPv4Receive.TcpOptionsSupported          = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Receive.TcpOptionsSupported)));
    Log5(("   Checksum.IPv4Receive.TcpChecksum                  = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Receive.TcpChecksum)));
    Log5(("   Checksum.IPv4Receive.UdpChecksum                  = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Receive.UdpChecksum)));
    Log5(("   Checksum.IPv4Receive.IpChecksum                   = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv4Receive.IpChecksum)));
    nemuNetLwfWinDumpEncapsulation("   Checksum.IPv6Transmit.Encapsulation               =", pOffloadConfig->Checksum.IPv6Transmit.Encapsulation);
    Log5(("   Checksum.IPv6Transmit.IpExtensionHeadersSupported = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv6Transmit.IpExtensionHeadersSupported)));
    Log5(("   Checksum.IPv6Transmit.TcpOptionsSupported         = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv6Transmit.TcpOptionsSupported)));
    Log5(("   Checksum.IPv6Transmit.TcpChecksum                 = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv6Transmit.TcpChecksum)));
    Log5(("   Checksum.IPv6Transmit.UdpChecksum                 = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv6Transmit.UdpChecksum)));
    nemuNetLwfWinDumpEncapsulation("   Checksum.IPv6Receive.Encapsulation                =", pOffloadConfig->Checksum.IPv6Receive.Encapsulation);
    Log5(("   Checksum.IPv6Receive.IpExtensionHeadersSupported  = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv6Receive.IpExtensionHeadersSupported)));
    Log5(("   Checksum.IPv6Receive.TcpOptionsSupported          = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv6Receive.TcpOptionsSupported)));
    Log5(("   Checksum.IPv6Receive.TcpChecksum                  = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv6Receive.TcpChecksum)));
    Log5(("   Checksum.IPv6Receive.UdpChecksum                  = %s\n", nemuNetLwfWinSetOnOffText(pOffloadConfig->Checksum.IPv6Receive.UdpChecksum)));
    nemuNetLwfWinDumpEncapsulation("   LsoV1.IPv4.Encapsulation                          =", pOffloadConfig->LsoV1.IPv4.Encapsulation);
    Log5(("   LsoV1.IPv4.TcpOptions                             = %s\n", nemuNetLwfWinSupportedText(pOffloadConfig->LsoV1.IPv4.TcpOptions)));
    Log5(("   LsoV1.IPv4.IpOptions                              = %s\n", nemuNetLwfWinSupportedText(pOffloadConfig->LsoV1.IPv4.IpOptions)));
    nemuNetLwfWinDumpEncapsulation("   LsoV2.IPv4.Encapsulation                          =", pOffloadConfig->LsoV2.IPv4.Encapsulation);
    nemuNetLwfWinDumpEncapsulation("   LsoV2.IPv6.Encapsulation                          =", pOffloadConfig->LsoV2.IPv6.Encapsulation);
    Log5(("   LsoV2.IPv6.IpExtensionHeadersSupported            = %s\n", nemuNetLwfWinSupportedText(pOffloadConfig->LsoV2.IPv6.IpExtensionHeadersSupported)));
    Log5(("   LsoV2.IPv6.TcpOptionsSupported                    = %s\n", nemuNetLwfWinSupportedText(pOffloadConfig->LsoV2.IPv6.TcpOptionsSupported)));
}

static void nemuNetLwfWinDumpOffloadSettings(PNDIS_OFFLOAD pOffloadConfig)
{
    nemuNetLwfWinDumpEncapsulation("   Checksum.IPv4Transmit.Encapsulation               =", pOffloadConfig->Checksum.IPv4Transmit.Encapsulation);
    Log5(("   Checksum.IPv4Transmit.IpOptionsSupported          = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Transmit.IpOptionsSupported)));
    Log5(("   Checksum.IPv4Transmit.TcpOptionsSupported         = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Transmit.TcpOptionsSupported)));
    Log5(("   Checksum.IPv4Transmit.TcpChecksum                 = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Transmit.TcpChecksum)));
    Log5(("   Checksum.IPv4Transmit.UdpChecksum                 = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Transmit.UdpChecksum)));
    Log5(("   Checksum.IPv4Transmit.IpChecksum                  = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Transmit.IpChecksum)));
    nemuNetLwfWinDumpEncapsulation("   Checksum.IPv4Receive.Encapsulation                =", pOffloadConfig->Checksum.IPv4Receive.Encapsulation);
    Log5(("   Checksum.IPv4Receive.IpOptionsSupported           = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Receive.IpOptionsSupported)));
    Log5(("   Checksum.IPv4Receive.TcpOptionsSupported          = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Receive.TcpOptionsSupported)));
    Log5(("   Checksum.IPv4Receive.TcpChecksum                  = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Receive.TcpChecksum)));
    Log5(("   Checksum.IPv4Receive.UdpChecksum                  = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Receive.UdpChecksum)));
    Log5(("   Checksum.IPv4Receive.IpChecksum                   = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv4Receive.IpChecksum)));
    nemuNetLwfWinDumpEncapsulation("   Checksum.IPv6Transmit.Encapsulation               =", pOffloadConfig->Checksum.IPv6Transmit.Encapsulation);
    Log5(("   Checksum.IPv6Transmit.IpExtensionHeadersSupported = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv6Transmit.IpExtensionHeadersSupported)));
    Log5(("   Checksum.IPv6Transmit.TcpOptionsSupported         = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv6Transmit.TcpOptionsSupported)));
    Log5(("   Checksum.IPv6Transmit.TcpChecksum                 = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv6Transmit.TcpChecksum)));
    Log5(("   Checksum.IPv6Transmit.UdpChecksum                 = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv6Transmit.UdpChecksum)));
    nemuNetLwfWinDumpEncapsulation("   Checksum.IPv6Receive.Encapsulation                =", pOffloadConfig->Checksum.IPv6Receive.Encapsulation);
    Log5(("   Checksum.IPv6Receive.IpExtensionHeadersSupported  = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv6Receive.IpExtensionHeadersSupported)));
    Log5(("   Checksum.IPv6Receive.TcpOptionsSupported          = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv6Receive.TcpOptionsSupported)));
    Log5(("   Checksum.IPv6Receive.TcpChecksum                  = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv6Receive.TcpChecksum)));
    Log5(("   Checksum.IPv6Receive.UdpChecksum                  = %s\n", nemuNetLwfWinOnOffText(pOffloadConfig->Checksum.IPv6Receive.UdpChecksum)));
    nemuNetLwfWinDumpEncapsulation("   LsoV1.IPv4.Encapsulation                          =", pOffloadConfig->LsoV1.IPv4.Encapsulation);
    Log5(("   LsoV1.IPv4.TcpOptions                             = %s\n", nemuNetLwfWinSupportedText(pOffloadConfig->LsoV1.IPv4.TcpOptions)));
    Log5(("   LsoV1.IPv4.IpOptions                              = %s\n", nemuNetLwfWinSupportedText(pOffloadConfig->LsoV1.IPv4.IpOptions)));
    nemuNetLwfWinDumpEncapsulation("   LsoV2.IPv4.Encapsulation                          =", pOffloadConfig->LsoV2.IPv4.Encapsulation);
    nemuNetLwfWinDumpEncapsulation("   LsoV2.IPv6.Encapsulation                          =", pOffloadConfig->LsoV2.IPv6.Encapsulation);
    Log5(("   LsoV2.IPv6.IpExtensionHeadersSupported            = %s\n", nemuNetLwfWinSupportedText(pOffloadConfig->LsoV2.IPv6.IpExtensionHeadersSupported)));
    Log5(("   LsoV2.IPv6.TcpOptionsSupported                    = %s\n", nemuNetLwfWinSupportedText(pOffloadConfig->LsoV2.IPv6.TcpOptionsSupported)));
}

static const char *nemuNetLwfWinStateToText(uint32_t enmState)
{
    switch (enmState)
    {
        case LwfState_Detached: return "Detached";
        case LwfState_Attaching: return "Attaching";
        case LwfState_Paused: return "Paused";
        case LwfState_Restarting: return "Restarting";
        case LwfState_Running: return "Running";
        case LwfState_Pausing: return "Pausing";
    }
    return "invalid";
}

#else /* !DEBUG */
#define nemuNetLwfWinDumpFilterTypes(uFlags)
#define nemuNetLwfWinDumpOffloadSettings(p)
#define nemuNetLwfWinDumpSetOffloadSettings(p)
#endif /* DEBUG */

DECLINLINE(bool) nemuNetLwfWinChangeState(PNEMUNETLWF_MODULE pModuleCtx, uint32_t enmNew, uint32_t enmOld = LwfState_32BitHack)
{
    AssertReturn(pModuleCtx, false);

    bool fSuccess = true;
    if (enmOld != LwfState_32BitHack)
    {
        fSuccess = ASMAtomicCmpXchgU32(&pModuleCtx->enmState, enmNew, enmOld);
        if (fSuccess)
            Log(("nemuNetLwfWinChangeState: state change %s -> %s\n",
                 nemuNetLwfWinStateToText(enmOld),
                 nemuNetLwfWinStateToText(enmNew)));
        else
            Log(("ERROR! nemuNetLwfWinChangeState: failed state change %s (actual=%s) -> %s\n",
                 nemuNetLwfWinStateToText(enmOld),
                 nemuNetLwfWinStateToText(ASMAtomicReadU32(&pModuleCtx->enmState)),
                 nemuNetLwfWinStateToText(enmNew)));
        Assert(fSuccess);
    }
    else
    {
        uint32_t enmPrevState = ASMAtomicXchgU32(&pModuleCtx->enmState, enmNew);
        Log(("nemuNetLwfWinChangeState: state change %s -> %s\n",
             nemuNetLwfWinStateToText(enmPrevState),
             nemuNetLwfWinStateToText(enmNew)));
    }
    return fSuccess;
}

DECLINLINE(void) nemuNetLwfWinInitOidRequest(PNEMUNETLWF_OIDREQ pRequest)
{
    NdisZeroMemory(pRequest, sizeof(NEMUNETLWF_OIDREQ));

    NdisInitializeEvent(&pRequest->Event);

    pRequest->Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    pRequest->Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    pRequest->Request.Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;

    pRequest->Request.RequestId = (PVOID)NEMUNETLWF_REQ_ID;
}

static NDIS_STATUS nemuNetLwfWinSyncOidRequest(PNEMUNETLWF_MODULE pModuleCtx, PNEMUNETLWF_OIDREQ pRequest)
{
    NDIS_STATUS Status = NdisFOidRequest(pModuleCtx->hFilter, &pRequest->Request);
    if (Status == NDIS_STATUS_PENDING)
    {
        NdisWaitEvent(&pRequest->Event, 0);
        Status = pRequest->Status;
    }
    return Status;
}

DECLINLINE(void) nemuNetLwfWinCopyOidRequestResults(PNDIS_OID_REQUEST pFrom, PNDIS_OID_REQUEST pTo)
{
    switch (pFrom->RequestType)
    {
        case NdisRequestSetInformation:
            pTo->DATA.SET_INFORMATION.BytesRead   = pFrom->DATA.SET_INFORMATION.BytesRead;
            pTo->DATA.SET_INFORMATION.BytesNeeded = pFrom->DATA.SET_INFORMATION.BytesNeeded;
            break;
        case NdisRequestMethod:
            pTo->DATA.METHOD_INFORMATION.OutputBufferLength = pFrom->DATA.METHOD_INFORMATION.OutputBufferLength;
            pTo->DATA.METHOD_INFORMATION.BytesWritten       = pFrom->DATA.METHOD_INFORMATION.BytesWritten;
            pTo->DATA.METHOD_INFORMATION.BytesRead          = pFrom->DATA.METHOD_INFORMATION.BytesRead;
            pTo->DATA.METHOD_INFORMATION.BytesNeeded        = pFrom->DATA.METHOD_INFORMATION.BytesNeeded;
            break;
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
        default:
            pTo->DATA.QUERY_INFORMATION.BytesWritten = pFrom->DATA.QUERY_INFORMATION.BytesWritten;
            pTo->DATA.QUERY_INFORMATION.BytesNeeded  = pFrom->DATA.QUERY_INFORMATION.BytesNeeded;
    }
}

void inline nemuNetLwfWinOverridePacketFiltersUp(PNEMUNETLWF_MODULE pModuleCtx, ULONG *pFilters)
{
    if (ASMAtomicReadBool(&pModuleCtx->fActive) && !ASMAtomicReadBool(&pModuleCtx->fHostPromisc))
        *pFilters &= ~NDIS_PACKET_TYPE_PROMISCUOUS;
}

NDIS_STATUS nemuNetLwfWinOidRequest(IN NDIS_HANDLE hModuleCtx,
                                    IN PNDIS_OID_REQUEST pOidRequest)
{
    LogFlow(("==>nemuNetLwfWinOidRequest: module=%p\n", hModuleCtx));
    nemuNetCmnWinDumpOidRequest(__FUNCTION__, pOidRequest);
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)hModuleCtx;
    PNDIS_OID_REQUEST pClone = NULL;
    NDIS_STATUS Status = NdisAllocateCloneOidRequest(pModuleCtx->hFilter,
                                                     pOidRequest,
                                                     NEMUNETLWF_MEM_TAG,
                                                     &pClone);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        /* Save the pointer to the original */
        *((PNDIS_OID_REQUEST*)(pClone->SourceReserved)) = pOidRequest;

        pClone->RequestId = pOidRequest->RequestId;
        /* We are not supposed to get another request until we are through with the one we "postponed" */
        PNDIS_OID_REQUEST pPrev = ASMAtomicXchgPtrT(&pModuleCtx->pPendingRequest, pClone, PNDIS_OID_REQUEST);
        Assert(pPrev == NULL);
        pModuleCtx->pPendingRequest = pClone;
        if (pOidRequest->RequestType == NdisRequestSetInformation
            && pOidRequest->DATA.SET_INFORMATION.Oid == OID_GEN_CURRENT_PACKET_FILTER)
        {
            ASMAtomicWriteBool(&pModuleCtx->fHostPromisc, !!(*(ULONG*)pOidRequest->DATA.SET_INFORMATION.InformationBuffer & NDIS_PACKET_TYPE_PROMISCUOUS));
            Log(("nemuNetLwfWinOidRequest: host wanted to set packet filter value to:\n"));
            nemuNetLwfWinDumpFilterTypes(*(ULONG*)pOidRequest->DATA.SET_INFORMATION.InformationBuffer);
            /* Keep adapter in promisc mode as long as we are active. */
            if (ASMAtomicReadBool(&pModuleCtx->fActive))
                *(ULONG*)pClone->DATA.SET_INFORMATION.InformationBuffer |= NDIS_PACKET_TYPE_PROMISCUOUS;
            Log5(("nemuNetLwfWinOidRequest: pass the following packet filters to miniport:\n"));
            nemuNetLwfWinDumpFilterTypes(*(ULONG*)pOidRequest->DATA.SET_INFORMATION.InformationBuffer);
        }
        if (pOidRequest->RequestType == NdisRequestSetInformation
            && pOidRequest->DATA.SET_INFORMATION.Oid == OID_TCP_OFFLOAD_CURRENT_CONFIG)
        {
            Log5(("nemuNetLwfWinOidRequest: offloading set to:\n"));
            nemuNetLwfWinDumpSetOffloadSettings((PNDIS_OFFLOAD)pOidRequest->DATA.SET_INFORMATION.InformationBuffer);
        }

        /* Forward the clone to underlying filters/miniport */
        Status = NdisFOidRequest(pModuleCtx->hFilter, pClone);
        if (Status != NDIS_STATUS_PENDING)
        {
            /* Synchronous completion */
            pPrev = ASMAtomicXchgPtrT(&pModuleCtx->pPendingRequest, NULL, PNDIS_OID_REQUEST);
            Assert(pPrev == pClone);
            Log5(("nemuNetLwfWinOidRequest: got the following packet filters from miniport:\n"));
            nemuNetLwfWinDumpFilterTypes(*(ULONG*)pOidRequest->DATA.QUERY_INFORMATION.InformationBuffer);
            /*
             * The host does not expect the adapter to be in promisc mode,
             * unless it enabled the mode. Let's not disillusion it.
             */
            if (   pOidRequest->RequestType == NdisRequestQueryInformation
                && pOidRequest->DATA.QUERY_INFORMATION.Oid == OID_GEN_CURRENT_PACKET_FILTER)
                nemuNetLwfWinOverridePacketFiltersUp(pModuleCtx, (ULONG*)pOidRequest->DATA.QUERY_INFORMATION.InformationBuffer);
            Log5(("nemuNetLwfWinOidRequest: reporting to the host the following packet filters:\n"));
            nemuNetLwfWinDumpFilterTypes(*(ULONG*)pOidRequest->DATA.QUERY_INFORMATION.InformationBuffer);
            nemuNetLwfWinCopyOidRequestResults(pClone, pOidRequest);
            NdisFreeCloneOidRequest(pModuleCtx->hFilter, pClone);
        }
        /* In case of async completion we do the rest in nemuNetLwfWinOidRequestComplete() */
    }
    else
    {
        Log(("ERROR! nemuNetLwfWinOidRequest: NdisAllocateCloneOidRequest failed with 0x%x\n", Status));
    }
    LogFlow(("<==nemuNetLwfWinOidRequest: Status=0x%x\n", Status));
    return Status;
}

VOID nemuNetLwfWinOidRequestComplete(IN NDIS_HANDLE hModuleCtx,
                                     IN PNDIS_OID_REQUEST pRequest,
                                     IN NDIS_STATUS Status)
{
    LogFlow(("==>nemuNetLwfWinOidRequestComplete: module=%p req=%p status=0x%x\n", hModuleCtx, pRequest, Status));
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)hModuleCtx;
    PNDIS_OID_REQUEST pOriginal = *((PNDIS_OID_REQUEST*)(pRequest->SourceReserved));
    if (pOriginal)
    {
        /* NDIS is supposed to serialize requests */
        PNDIS_OID_REQUEST pPrev = ASMAtomicXchgPtrT(&pModuleCtx->pPendingRequest, NULL, PNDIS_OID_REQUEST);
        Assert(pPrev == pRequest);

        Log5(("nemuNetLwfWinOidRequestComplete: completed rq type=%d oid=%x\n", pRequest->RequestType, pRequest->DATA.QUERY_INFORMATION.Oid));
        nemuNetLwfWinCopyOidRequestResults(pRequest, pOriginal);
        if (   pRequest->RequestType == NdisRequestQueryInformation
            && pRequest->DATA.QUERY_INFORMATION.Oid == OID_GEN_CURRENT_PACKET_FILTER)
        {
            Log5(("nemuNetLwfWinOidRequestComplete: underlying miniport reports its packet filters:\n"));
            nemuNetLwfWinDumpFilterTypes(*(ULONG*)pRequest->DATA.QUERY_INFORMATION.InformationBuffer);
            nemuNetLwfWinOverridePacketFiltersUp(pModuleCtx, (ULONG*)pRequest->DATA.QUERY_INFORMATION.InformationBuffer);
            Log5(("nemuNetLwfWinOidRequestComplete: reporting the following packet filters to upper protocol:\n"));
            nemuNetLwfWinDumpFilterTypes(*(ULONG*)pRequest->DATA.QUERY_INFORMATION.InformationBuffer);
        }
        NdisFreeCloneOidRequest(pModuleCtx->hFilter, pRequest);
        NdisFOidRequestComplete(pModuleCtx->hFilter, pOriginal, Status);
    }
    else
    {
        /* This is not a clone, we originated it */
        Log(("nemuNetLwfWinOidRequestComplete: locally originated request (%p) completed, status=0x%x\n", pRequest, Status));
        PNEMUNETLWF_OIDREQ pRqWrapper = RT_FROM_MEMBER(pRequest, NEMUNETLWF_OIDREQ, Request);
        pRqWrapper->Status = Status;
        NdisSetEvent(&pRqWrapper->Event);
    }
    LogFlow(("<==nemuNetLwfWinOidRequestComplete\n"));
}


static bool nemuNetLwfWinIsPromiscuous(PNEMUNETLWF_MODULE pModuleCtx)
{
    return ASMAtomicReadBool(&pModuleCtx->fHostPromisc);
}

#if 0
static NDIS_STATUS nemuNetLwfWinGetPacketFilter(PNEMUNETLWF_MODULE pModuleCtx)
{
    LogFlow(("==>nemuNetLwfWinGetPacketFilter: module=%p\n", pModuleCtx));
    NEMUNETLWF_OIDREQ Rq;
    nemuNetLwfWinInitOidRequest(&Rq);
    Rq.Request.RequestType = NdisRequestQueryInformation;
    Rq.Request.DATA.QUERY_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
    Rq.Request.DATA.QUERY_INFORMATION.InformationBuffer = &pModuleCtx->uPacketFilter;
    Rq.Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(pModuleCtx->uPacketFilter);
    NDIS_STATUS Status = nemuNetLwfWinSyncOidRequest(pModuleCtx, &Rq);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Log(("ERROR! nemuNetLwfWinGetPacketFilter: nemuNetLwfWinSyncOidRequest(query, OID_GEN_CURRENT_PACKET_FILTER) failed with 0x%x\n", Status));
        return FALSE;
    }
    if (Rq.Request.DATA.QUERY_INFORMATION.BytesWritten != sizeof(pModuleCtx->uPacketFilter))
    {
        Log(("ERROR! nemuNetLwfWinGetPacketFilter: nemuNetLwfWinSyncOidRequest(query, OID_GEN_CURRENT_PACKET_FILTER) failed to write neccessary amount (%d bytes), actually written %d bytes\n", sizeof(pModuleCtx->uPacketFilter), Rq.Request.DATA.QUERY_INFORMATION.BytesWritten));
    }

    Log5(("nemuNetLwfWinGetPacketFilter: OID_GEN_CURRENT_PACKET_FILTER query returned the following filters:\n"));
    nemuNetLwfWinDumpFilterTypes(pModuleCtx->uPacketFilter);

    LogFlow(("<==nemuNetLwfWinGetPacketFilter: status=0x%x\n", Status));
    return Status;
}
#endif

static NDIS_STATUS nemuNetLwfWinSetPacketFilter(PNEMUNETLWF_MODULE pModuleCtx, bool fPromisc)
{
    LogFlow(("==>nemuNetLwfWinSetPacketFilter: module=%p %s\n", pModuleCtx, fPromisc ? "promiscuous" : "normal"));
    ULONG uFilter = 0;
    NEMUNETLWF_OIDREQ Rq;
    nemuNetLwfWinInitOidRequest(&Rq);
    Rq.Request.RequestType = NdisRequestQueryInformation;
    Rq.Request.DATA.QUERY_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
    Rq.Request.DATA.QUERY_INFORMATION.InformationBuffer = &uFilter;
    Rq.Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(uFilter);
    NDIS_STATUS Status = nemuNetLwfWinSyncOidRequest(pModuleCtx, &Rq);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Log(("ERROR! nemuNetLwfWinSetPacketFilter: nemuNetLwfWinSyncOidRequest(query, OID_GEN_CURRENT_PACKET_FILTER) failed with 0x%x\n", Status));
        return Status;
    }
    if (Rq.Request.DATA.QUERY_INFORMATION.BytesWritten != sizeof(uFilter))
    {
        Log(("ERROR! nemuNetLwfWinSetPacketFilter: nemuNetLwfWinSyncOidRequest(query, OID_GEN_CURRENT_PACKET_FILTER) failed to write neccessary amount (%d bytes), actually written %d bytes\n", sizeof(uFilter), Rq.Request.DATA.QUERY_INFORMATION.BytesWritten));
        return NDIS_STATUS_FAILURE;
    }

    Log5(("nemuNetLwfWinSetPacketFilter: OID_GEN_CURRENT_PACKET_FILTER query returned the following filters:\n"));
    nemuNetLwfWinDumpFilterTypes(uFilter);

    if (fPromisc)
    {
        /* If we about to go promiscuous, save the state before we change it. */
        ASMAtomicWriteBool(&pModuleCtx->fHostPromisc, !!(uFilter & NDIS_PACKET_TYPE_PROMISCUOUS));
        uFilter |= NDIS_PACKET_TYPE_PROMISCUOUS;
    }
    else
    {
        /* Reset promisc only if it was not enabled before we had changed it. */
        if (!ASMAtomicReadBool(&pModuleCtx->fHostPromisc))
            uFilter &= ~NDIS_PACKET_TYPE_PROMISCUOUS;
    }

    Log5(("nemuNetLwfWinSetPacketFilter: OID_GEN_CURRENT_PACKET_FILTER about to set the following filters:\n"));
    nemuNetLwfWinDumpFilterTypes(uFilter);

    NdisResetEvent(&Rq.Event); /* need to reset as it has been set by query op */
    Rq.Request.RequestType = NdisRequestSetInformation;
    Rq.Request.DATA.SET_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
    Rq.Request.DATA.SET_INFORMATION.InformationBuffer = &uFilter;
    Rq.Request.DATA.SET_INFORMATION.InformationBufferLength = sizeof(uFilter);
    Status = nemuNetLwfWinSyncOidRequest(pModuleCtx, &Rq);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Log(("ERROR! nemuNetLwfWinSetPacketFilter: nemuNetLwfWinSyncOidRequest(set, OID_GEN_CURRENT_PACKET_FILTER, vvv below vvv) failed with 0x%x\n", Status));
        nemuNetLwfWinDumpFilterTypes(uFilter);
    }
    LogFlow(("<==nemuNetLwfWinSetPacketFilter: status=0x%x\n", Status));
    return Status;
}


static NTSTATUS nemuNetLwfWinDevDispatch(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pIrpSl = IoGetCurrentIrpStackLocation(pIrp);;
    NTSTATUS Status = STATUS_SUCCESS;

    switch (pIrpSl->MajorFunction)
    {
        case IRP_MJ_DEVICE_CONTROL:
            Status = STATUS_NOT_SUPPORTED;
            break;
        case IRP_MJ_CREATE:
        case IRP_MJ_CLEANUP:
        case IRP_MJ_CLOSE:
            break;
        default:
            Assert(0);
            break;
    }

    pIrp->IoStatus.Status = Status;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return Status;
}

/** @todo So far we had no use for device, should we even bother to create it? */
static NDIS_STATUS nemuNetLwfWinDevCreate(PNEMUNETLWFGLOBALS pGlobals)
{
    NDIS_STRING DevName, LinkName;
    PDRIVER_DISPATCH aMajorFunctions[IRP_MJ_MAXIMUM_FUNCTION+1];
    NdisInitUnicodeString(&DevName, NEMUNETLWF_NAME_DEVICE);
    NdisInitUnicodeString(&LinkName, NEMUNETLWF_NAME_LINK);

    Assert(!pGlobals->hDevice);
    Assert(!pGlobals->pDevObj);
    NdisZeroMemory(aMajorFunctions, sizeof (aMajorFunctions));
    aMajorFunctions[IRP_MJ_CREATE] = nemuNetLwfWinDevDispatch;
    aMajorFunctions[IRP_MJ_CLEANUP] = nemuNetLwfWinDevDispatch;
    aMajorFunctions[IRP_MJ_CLOSE] = nemuNetLwfWinDevDispatch;
    aMajorFunctions[IRP_MJ_DEVICE_CONTROL] = nemuNetLwfWinDevDispatch;

    NDIS_DEVICE_OBJECT_ATTRIBUTES DeviceAttributes;
    NdisZeroMemory(&DeviceAttributes, sizeof(DeviceAttributes));
    DeviceAttributes.Header.Type = NDIS_OBJECT_TYPE_DEVICE_OBJECT_ATTRIBUTES;
    DeviceAttributes.Header.Revision = NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;
    DeviceAttributes.Header.Size = sizeof(DeviceAttributes);
    DeviceAttributes.DeviceName = &DevName;
    DeviceAttributes.SymbolicName = &LinkName;
    DeviceAttributes.MajorFunctions = aMajorFunctions;
    //DeviceAttributes.ExtensionSize = sizeof(FILTER_DEVICE_EXTENSION);

    NDIS_STATUS Status = NdisRegisterDeviceEx(pGlobals->hFilterDriver,
                                              &DeviceAttributes,
                                              &pGlobals->pDevObj,
                                              &pGlobals->hDevice);
    Log(("nemuNetLwfWinDevCreate: NdisRegisterDeviceEx returned 0x%x\n", Status));
    Assert(Status == NDIS_STATUS_SUCCESS);
#if 0
    if (Status == NDIS_STATUS_SUCCESS)
    {
        PFILTER_DEVICE_EXTENSION pExtension;
        pExtension = NdisGetDeviceReservedExtension(pGlobals->pDevObj);
        pExtension->Signature = NEMUNETLWF_MEM_TAG;
        pExtension->Handle = pGlobals->hFilterDriver;
    }
#endif
    return Status;
}

static void nemuNetLwfWinDevDestroy(PNEMUNETLWFGLOBALS pGlobals)
{
    Assert(pGlobals->hDevice);
    Assert(pGlobals->pDevObj);
    NdisDeregisterDeviceEx(pGlobals->hDevice);
    pGlobals->hDevice = NULL;
    pGlobals->pDevObj = NULL;
}

static void nemuNetLwfWinUpdateSavedOffloadConfig(PNEMUNETLWF_MODULE pModuleCtx, PNDIS_OFFLOAD pOffload)
{
    pModuleCtx->SavedOffloadConfig = *pOffload;
    pModuleCtx->fOffloadConfigValid = true;
}

static NDIS_STATUS nemuNetLwfWinAttach(IN NDIS_HANDLE hFilter, IN NDIS_HANDLE hDriverCtx,
                                       IN PNDIS_FILTER_ATTACH_PARAMETERS pParameters)
{
    LogFlow(("==>nemuNetLwfWinAttach: filter=%p\n", hFilter));

    PNEMUNETLWFGLOBALS pGlobals = (PNEMUNETLWFGLOBALS)hDriverCtx;
    AssertReturn(pGlobals, NDIS_STATUS_FAILURE);

    ANSI_STRING strMiniportName;
    /* We use the miniport name to associate this filter module with the netflt instance */
    NTSTATUS rc = RtlUnicodeStringToAnsiString(&strMiniportName,
                                               pParameters->BaseMiniportName,
                                               TRUE);
    if (rc != STATUS_SUCCESS)
    {
        Log(("ERROR! nemuNetLwfWinAttach: RtlUnicodeStringToAnsiString(%ls) failed with 0x%x\n",
             pParameters->BaseMiniportName, rc));
        return NDIS_STATUS_FAILURE;
    }
    DbgPrint("nemuNetLwfWinAttach: friendly name=%wZ\n", pParameters->BaseMiniportInstanceName);
    DbgPrint("nemuNetLwfWinAttach: name=%Z\n", strMiniportName);

    UINT cbModuleWithNameExtra = sizeof(NEMUNETLWF_MODULE) + strMiniportName.Length;
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)NdisAllocateMemoryWithTagPriority(hFilter,
                                                                      cbModuleWithNameExtra,
                                                                      NEMUNETLWF_MEM_TAG,
                                                                      LowPoolPriority);
    if (!pModuleCtx)
    {
        RtlFreeAnsiString(&strMiniportName);
        return NDIS_STATUS_RESOURCES;
    }
    Log4(("nemuNetLwfWinAttach: allocated module context 0x%p\n", pModuleCtx));

    NdisZeroMemory(pModuleCtx, cbModuleWithNameExtra);
    NdisMoveMemory(pModuleCtx->szMiniportName, strMiniportName.Buffer, strMiniportName.Length);
    RtlFreeAnsiString(&strMiniportName);

    pModuleCtx->hWorkItem = NdisAllocateIoWorkItem(g_NemuNetLwfGlobals.hFilterDriver);
    if (!pModuleCtx->hWorkItem)
    {
        Log(("ERROR! nemuNetLwfWinAttach: Failed to allocate work item for %ls\n",
             pParameters->BaseMiniportName));
        NdisFreeMemory(pModuleCtx, 0, 0);
        return NDIS_STATUS_RESOURCES;
    }

    Assert(pParameters->MacAddressLength == sizeof(RTMAC));
    NdisMoveMemory(&pModuleCtx->MacAddr, pParameters->CurrentMacAddress, RT_MIN(sizeof(RTMAC), pParameters->MacAddressLength));
    if (pParameters->DefaultOffloadConfiguration)
        nemuNetLwfWinUpdateSavedOffloadConfig(pModuleCtx, pParameters->DefaultOffloadConfiguration);

    pModuleCtx->pGlobals  = pGlobals;
    pModuleCtx->hFilter   = hFilter;
    nemuNetLwfWinChangeState(pModuleCtx, LwfState_Attaching);
    /* Initialize transmission mutex and events */
    NDIS_INIT_MUTEX(&pModuleCtx->InTransmit);
#ifdef NEMUNETLWF_SYNC_SEND
    KeInitializeEvent(&pModuleCtx->EventWire, SynchronizationEvent, FALSE);
    KeInitializeEvent(&pModuleCtx->EventHost, SynchronizationEvent, FALSE);
#else /* !NEMUNETLWF_SYNC_SEND */
    NdisInitializeEvent(&pModuleCtx->EventSendComplete);
    pModuleCtx->cPendingBuffers = 0;
#endif /* !NEMUNETLWF_SYNC_SEND */
    /* Allocate buffer pools */
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
    NdisZeroMemory(&PoolParams, sizeof(PoolParams));
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.ContextSize = 0; /** @todo Do we need to consider underlying drivers? I think not. */
    PoolParams.PoolTag = NEMUNETLWF_MEM_TAG;
#ifndef NEMUNETLWF_SYNC_SEND
    PoolParams.DataSize = 2048; /** @todo figure out the optimal size, use several pools if necessary, make configurable, etc */
#endif /* !NEMUNETLWF_SYNC_SEND */

    pModuleCtx->hPool = NdisAllocateNetBufferListPool(hFilter, &PoolParams);
    if (!pModuleCtx->hPool)
    {
        Log(("ERROR! nemuNetLwfWinAttach: NdisAllocateNetBufferListPool failed\n"));
        NdisFreeIoWorkItem(pModuleCtx->hWorkItem);
        NdisFreeMemory(pModuleCtx, 0, 0);
        return NDIS_STATUS_RESOURCES;
    }
    Log4(("nemuNetLwfWinAttach: allocated NBL+NB pool 0x%p\n", pModuleCtx->hPool));

    NDIS_FILTER_ATTRIBUTES Attributes;
    NdisZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = sizeof(Attributes);
    Attributes.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
    Attributes.Flags = 0;
    NDIS_STATUS Status = NdisFSetAttributes(hFilter, pModuleCtx, &Attributes);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Log(("ERROR! nemuNetLwfWinAttach: NdisFSetAttributes failed with 0x%x\n", Status));
        NdisFreeNetBufferListPool(pModuleCtx->hPool);
        Log4(("nemuNetLwfWinAttach: freed NBL+NB pool 0x%p\n", pModuleCtx->hPool));
        NdisFreeIoWorkItem(pModuleCtx->hWorkItem);
        NdisFreeMemory(pModuleCtx, 0, 0);
        return NDIS_STATUS_RESOURCES;
    }
    /* Insert into module chain */
    NdisAcquireSpinLock(&pGlobals->Lock);
    RTListPrepend(&pGlobals->listModules, &pModuleCtx->node);
    NdisReleaseSpinLock(&pGlobals->Lock);

    nemuNetLwfWinChangeState(pModuleCtx, LwfState_Paused);

    /// @todo Somehow the packet filter is 0 at this point: Status = nemuNetLwfWinGetPacketFilter(pModuleCtx);
    /// @todo We actually update it later in status handler, perhaps we should not do anything here.

    LogFlow(("<==nemuNetLwfWinAttach: Status = 0x%x\n", Status));
    return Status;
}

static VOID nemuNetLwfWinDetach(IN NDIS_HANDLE hModuleCtx)
{
    LogFlow(("==>nemuNetLwfWinDetach: module=%p\n", hModuleCtx));
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)hModuleCtx;
    nemuNetLwfWinChangeState(pModuleCtx, LwfState_Detached, LwfState_Paused);

    /* Remove from module chain */
    NdisAcquireSpinLock(&pModuleCtx->pGlobals->Lock);
    RTListNodeRemove(&pModuleCtx->node);
    NdisReleaseSpinLock(&pModuleCtx->pGlobals->Lock);

    PNEMUNETFLTINS pNetFltIns = pModuleCtx->pNetFlt; /// @todo Atomic?
    if (pNetFltIns && nemuNetFltTryRetainBusyNotDisconnected(pNetFltIns))
    {
        /*
         * Set hModuleCtx to null now in order to prevent filter restart,
         * OID requests and other stuff associated with NetFlt deactivation.
         */
        pNetFltIns->u.s.WinIf.hModuleCtx = NULL;
        /* Notify NetFlt that we are going down */
        pNetFltIns->pSwitchPort->pfnDisconnect(pNetFltIns->pSwitchPort, &pNetFltIns->MyPort, nemuNetFltPortReleaseBusy);
        /* We do not 'release' netflt instance since it has been done by pfnDisconnect */
    }
    pModuleCtx->pNetFlt = NULL;

    /*
     * We have to make sure that all NET_BUFFER_LIST structures have been freed by now, but
     * it does not require us to do anything here since it has already been taken care of
     * by nemuNetLwfWinPause().
     */
    if (pModuleCtx->hPool)
    {
        NdisFreeNetBufferListPool(pModuleCtx->hPool);
        Log4(("nemuNetLwfWinDetach: freed NBL+NB pool 0x%p\n", pModuleCtx->hPool));
    }
    NdisFreeIoWorkItem(pModuleCtx->hWorkItem);
    NdisFreeMemory(hModuleCtx, 0, 0);
    Log4(("nemuNetLwfWinDetach: freed module context 0x%p\n", pModuleCtx));
    LogFlow(("<==nemuNetLwfWinDetach\n"));
}


static NDIS_STATUS nemuNetLwfWinPause(IN NDIS_HANDLE hModuleCtx, IN PNDIS_FILTER_PAUSE_PARAMETERS pParameters)
{
    LogFlow(("==>nemuNetLwfWinPause: module=%p\n", hModuleCtx));
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)hModuleCtx;
    nemuNetLwfWinChangeState(pModuleCtx, LwfState_Pausing, LwfState_Running);
    /* Wait for pending send/indication operations to complete. */
    NDIS_WAIT_FOR_MUTEX(&pModuleCtx->InTransmit);
#ifndef NEMUNETLWF_SYNC_SEND
    NdisWaitEvent(&pModuleCtx->EventSendComplete, 1000 /* ms */);
#endif /* !NEMUNETLWF_SYNC_SEND */
    nemuNetLwfWinChangeState(pModuleCtx, LwfState_Paused, LwfState_Pausing);
    NDIS_RELEASE_MUTEX(&pModuleCtx->InTransmit);
    LogFlow(("<==nemuNetLwfWinPause\n"));
    return NDIS_STATUS_SUCCESS; /* Failure is not an option */
}


static void nemuNetLwfWinIndicateOffload(PNEMUNETLWF_MODULE pModuleCtx, PNDIS_OFFLOAD pOffload)
{
    Log5(("nemuNetLwfWinIndicateOffload: offload config changed to:\n"));
    nemuNetLwfWinDumpOffloadSettings(pOffload);
    NDIS_STATUS_INDICATION OffloadingIndication;
    NdisZeroMemory(&OffloadingIndication, sizeof(OffloadingIndication));
    OffloadingIndication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    OffloadingIndication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    OffloadingIndication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    OffloadingIndication.SourceHandle = pModuleCtx->hFilter;
    OffloadingIndication.StatusCode = NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG;
    OffloadingIndication.StatusBuffer = pOffload;
    OffloadingIndication.StatusBufferSize = sizeof(NDIS_OFFLOAD);
    NdisFIndicateStatus(pModuleCtx->hFilter, &OffloadingIndication);
}


static NDIS_STATUS nemuNetLwfWinRestart(IN NDIS_HANDLE hModuleCtx, IN PNDIS_FILTER_RESTART_PARAMETERS pParameters)
{
    LogFlow(("==>nemuNetLwfWinRestart: module=%p\n", hModuleCtx));
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)hModuleCtx;
    nemuNetLwfWinChangeState(pModuleCtx, LwfState_Restarting, LwfState_Paused);

    nemuNetLwfWinChangeState(pModuleCtx, LwfState_Running, LwfState_Restarting);
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    LogFlow(("<==nemuNetLwfWinRestart: Status = 0x%x\n", Status));
    return Status;
}


static void nemuNetLwfWinDumpPackets(const char *pszMsg, PNET_BUFFER_LIST pBufLists)
{
    for (PNET_BUFFER_LIST pList = pBufLists; pList; pList = NET_BUFFER_LIST_NEXT_NBL(pList))
    {
        for (PNET_BUFFER pBuf = NET_BUFFER_LIST_FIRST_NB(pList); pBuf; pBuf = NET_BUFFER_NEXT_NB(pBuf))
        {
            Log(("%s packet: cb=%d\n", pszMsg, NET_BUFFER_DATA_LENGTH(pBuf)));
        }
    }
}

DECLINLINE(const char *) nemuNetLwfWinEthTypeStr(uint16_t uType)
{
    switch (uType)
    {
        case RTNET_ETHERTYPE_IPV4: return "IP";
        case RTNET_ETHERTYPE_IPV6: return "IPv6";
        case RTNET_ETHERTYPE_ARP:  return "ARP";
    }
    return "unknown";
}

#define NEMUNETLWF_PKTDMPSIZE 0x50

/**
 * Dump a packet to debug log.
 *
 * @param   cpPacket    The packet.
 * @param   cb          The size of the packet.
 * @param   cszText     A string denoting direction of packet transfer.
 */
DECLINLINE(void) nemuNetLwfWinDumpPacket(PCINTNETSG pSG, const char *cszText)
{
    uint8_t bPacket[NEMUNETLWF_PKTDMPSIZE];

    uint32_t cb = pSG->cbTotal < NEMUNETLWF_PKTDMPSIZE ? pSG->cbTotal : NEMUNETLWF_PKTDMPSIZE;
    IntNetSgReadEx(pSG, 0, cb, bPacket);

    AssertReturnVoid(cb >= 14);

    uint8_t *pHdr = bPacket;
    uint8_t *pEnd = bPacket + cb;
    AssertReturnVoid(pEnd - pHdr >= 14);
    uint16_t uEthType = RT_N2H_U16(*(uint16_t*)(pHdr+12));
    Log2(("NetLWF: %s (%d bytes), %RTmac => %RTmac, EthType=%s(0x%x)\n",
          cszText, cb, pHdr+6, pHdr, nemuNetLwfWinEthTypeStr(uEthType), uEthType));
    pHdr += sizeof(RTNETETHERHDR);
    if (uEthType == RTNET_ETHERTYPE_VLAN)
    {
        AssertReturnVoid(pEnd - pHdr >= 4);
        uEthType = RT_N2H_U16(*(uint16_t*)(pHdr+2));
        Log2((" + VLAN: id=%d EthType=%s(0x%x)\n", RT_N2H_U16(*(uint16_t*)(pHdr)) & 0xFFF,
              nemuNetLwfWinEthTypeStr(uEthType), uEthType));
        pHdr += 2 * sizeof(uint16_t);
    }
    uint8_t uProto = 0xFF;
    switch (uEthType)
    {
        case RTNET_ETHERTYPE_IPV6:
            AssertReturnVoid(pEnd - pHdr >= 40);
            uProto = pHdr[6];
            Log2((" + IPv6: %RTnaipv6 => %RTnaipv6\n", pHdr+8, pHdr+24));
            pHdr += 40;
            break;
        case RTNET_ETHERTYPE_IPV4:
            AssertReturnVoid(pEnd - pHdr >= 20);
            uProto = pHdr[9];
            Log2((" + IP: %RTnaipv4 => %RTnaipv4\n", *(uint32_t*)(pHdr+12), *(uint32_t*)(pHdr+16)));
            pHdr += (pHdr[0] & 0xF) * 4;
            break;
        case RTNET_ETHERTYPE_ARP:
            AssertReturnVoid(pEnd - pHdr >= 28);
            AssertReturnVoid(RT_N2H_U16(*(uint16_t*)(pHdr+2)) == RTNET_ETHERTYPE_IPV4);
            switch (RT_N2H_U16(*(uint16_t*)(pHdr+6)))
            {
                case 1: /* ARP request */
                    Log2((" + ARP-REQ: who-has %RTnaipv4 tell %RTnaipv4\n",
                          *(uint32_t*)(pHdr+24), *(uint32_t*)(pHdr+14)));
                    break;
                case 2: /* ARP reply */
                    Log2((" + ARP-RPL: %RTnaipv4 is-at %RTmac\n",
                          *(uint32_t*)(pHdr+14), pHdr+8));
                    break;
                default:
                    Log2((" + ARP: unknown op %d\n", RT_N2H_U16(*(uint16_t*)(pHdr+6))));
                    break;
            }
            break;
        /* There is no default case as uProto is initialized with 0xFF */
    }
    while (uProto != 0xFF)
    {
        switch (uProto)
        {
            case 0:  /* IPv6 Hop-by-Hop option*/
            case 60: /* IPv6 Destination option*/
            case 43: /* IPv6 Routing option */
            case 44: /* IPv6 Fragment option */
                Log2((" + IPv6 option (%d): <not implemented>\n", uProto));
                uProto = pHdr[0];
                pHdr += pHdr[1] * 8 + 8; /* Skip to the next extension/protocol */
                break;
            case 51: /* IPv6 IPsec AH */
                Log2((" + IPv6 IPsec AH: <not implemented>\n"));
                uProto = pHdr[0];
                pHdr += (pHdr[1] + 2) * 4; /* Skip to the next extension/protocol */
                break;
            case 50: /* IPv6 IPsec ESP */
                /* Cannot decode IPsec, fall through */
                Log2((" + IPv6 IPsec ESP: <not implemented>\n"));
                uProto = 0xFF;
                break;
            case 59: /* No Next Header */
                Log2((" + IPv6 No Next Header\n"));
                uProto = 0xFF;
                break;
            case 58: /* IPv6-ICMP */
                switch (pHdr[0])
                {
                    case 1:   Log2((" + IPv6-ICMP: destination unreachable, code %d\n", pHdr[1])); break;
                    case 128: Log2((" + IPv6-ICMP: echo request\n")); break;
                    case 129: Log2((" + IPv6-ICMP: echo reply\n")); break;
                    default:  Log2((" + IPv6-ICMP: unknown type %d, code %d\n", pHdr[0], pHdr[1])); break;
                }
                uProto = 0xFF;
                break;
            case 1: /* ICMP */
                switch (pHdr[0])
                {
                    case 0:  Log2((" + ICMP: echo reply\n")); break;
                    case 8:  Log2((" + ICMP: echo request\n")); break;
                    case 3:  Log2((" + ICMP: destination unreachable, code %d\n", pHdr[1])); break;
                    default: Log2((" + ICMP: unknown type %d, code %d\n", pHdr[0], pHdr[1])); break;
                }
                uProto = 0xFF;
                break;
            case 6: /* TCP */
                Log2((" + TCP: src=%d dst=%d seq=%x ack=%x\n",
                      RT_N2H_U16(*(uint16_t*)(pHdr)), RT_N2H_U16(*(uint16_t*)(pHdr+2)),
                      RT_N2H_U32(*(uint32_t*)(pHdr+4)), RT_N2H_U32(*(uint32_t*)(pHdr+8))));
                uProto = 0xFF;
                break;
            case 17: /* UDP */
                Log2((" + UDP: src=%d dst=%d\n",
                      RT_N2H_U16(*(uint16_t*)(pHdr)), RT_N2H_U16(*(uint16_t*)(pHdr+2))));
                uProto = 0xFF;
                break;
            default:
                Log2((" + Unknown: proto=0x%x\n", uProto));
                uProto = 0xFF;
                break;
        }
    }
    Log3(("%.*Rhxd\n", cb, bPacket));
}

static void nemuNetLwfWinDestroySG(PINTNETSG pSG)
{
    NdisFreeMemory(pSG, 0, 0);
    Log4(("nemuNetLwfWinDestroySG: freed SG 0x%p\n", pSG));
}

DECLINLINE(ULONG) nemuNetLwfWinCalcSegments(PNET_BUFFER pNetBuf)
{
    ULONG cSegs = 0;
    for (PMDL pMdl = NET_BUFFER_CURRENT_MDL(pNetBuf); pMdl; pMdl = NDIS_MDL_LINKAGE(pMdl))
        cSegs++;
    return cSegs;
}

DECLINLINE(void) nemuNetLwfWinFreeMdlChain(PMDL pMdl)
{
#ifdef NEMUNETLWF_SYNC_SEND
    PMDL pMdlNext;
    while (pMdl)
    {
        pMdlNext = pMdl->Next;
        NdisFreeMdl(pMdl);
        Log4(("nemuNetLwfWinFreeMdlChain: freed MDL 0x%p\n", pMdl));
        pMdl = pMdlNext;
    }
#endif /* NEMUNETLWF_SYNC_SEND */
}

/** @todo
 * 1) Copy data from SG to MDL (if we decide to complete asynchronously).
 * 2) Provide context/backfill space. Nobody does it, should we?
 * 3) We always get a single segment from intnet. Simplify?
 */
static PNET_BUFFER_LIST nemuNetLwfWinSGtoNB(PNEMUNETLWF_MODULE pModule, PINTNETSG pSG)
{
    AssertReturn(pSG->cSegsUsed >= 1, NULL);
    LogFlow(("==>nemuNetLwfWinSGtoNB: segments=%d\n", pSG->cSegsUsed));

#ifdef NEMUNETLWF_SYNC_SEND
    PINTNETSEG pSeg = pSG->aSegs;
    PMDL pMdl = NdisAllocateMdl(pModule->hFilter, pSeg->pv, pSeg->cb);
    if (!pMdl)
    {
        Log(("ERROR! nemuNetLwfWinSGtoNB: failed to allocate an MDL\n"));
        LogFlow(("<==nemuNetLwfWinSGtoNB: return NULL\n"));
        return NULL;
    }
    Log4(("nemuNetLwfWinSGtoNB: allocated Mdl 0x%p\n", pMdl));
    PMDL pMdlCurr = pMdl;
    for (int i = 1; i < pSG->cSegsUsed; i++)
    {
        pSeg = &pSG->aSegs[i];
        pMdlCurr->Next = NdisAllocateMdl(pModule->hFilter, pSeg->pv, pSeg->cb);
        if (!pMdlCurr->Next)
        {
            Log(("ERROR! nemuNetLwfWinSGtoNB: failed to allocate an MDL\n"));
            /* Tear down all MDL we chained so far */
            nemuNetLwfWinFreeMdlChain(pMdl);
            return NULL;
        }
        pMdlCurr = pMdlCurr->Next;
        Log4(("nemuNetLwfWinSGtoNB: allocated Mdl 0x%p\n", pMdlCurr));
    }
    PNET_BUFFER_LIST pBufList = NdisAllocateNetBufferAndNetBufferList(pModule->hPool,
                                                                      0 /* ContextSize */,
                                                                      0 /* ContextBackFill */,
                                                                      pMdl,
                                                                      0 /* DataOffset */,
                                                                      pSG->cbTotal);
    if (pBufList)
    {
        Log4(("nemuNetLwfWinSGtoNB: allocated NBL+NB 0x%p\n", pBufList));
        pBufList->SourceHandle = pModule->hFilter;
        /** @todo Do we need to initialize anything else? */
    }
    else
    {
        Log(("ERROR! nemuNetLwfWinSGtoNB: failed to allocate an NBL+NB\n"));
        nemuNetLwfWinFreeMdlChain(pMdl);
    }
#else /* !NEMUNETLWF_SYNC_SEND */
    AssertReturn(pSG->cbTotal < 2048, NULL);
    PNET_BUFFER_LIST pBufList = NdisAllocateNetBufferList(pModule->hPool,
                                                          0 /** @todo ContextSize */,
                                                          0 /** @todo ContextBackFill */);
    NET_BUFFER_LIST_NEXT_NBL(pBufList) = NULL; /** @todo Is it even needed? */
    NET_BUFFER *pBuffer = NET_BUFFER_LIST_FIRST_NB(pBufList);
    NDIS_STATUS Status = NdisRetreatNetBufferDataStart(pBuffer, pSG->cbTotal, 0 /** @todo DataBackfill */, NULL);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        uint8_t *pDst = (uint8_t*)NdisGetDataBuffer(pBuffer, pSG->cbTotal, NULL, 1, 0);
        if (pDst)
        {
            for (int i = 0; i < pSG->cSegsUsed; i++)
            {
                NdisMoveMemory(pDst, pSG->aSegs[i].pv, pSG->aSegs[i].cb);
                pDst += pSG->aSegs[i].cb;
            }
            Log4(("nemuNetLwfWinSGtoNB: allocated NBL+NB+MDL+Data 0x%p\n", pBufList));
            pBufList->SourceHandle = pModule->hFilter;
            /** @todo Do we need to initialize anything else? */
        }
        else
        {
            Log(("ERROR! nemuNetLwfWinSGtoNB: failed to obtain the buffer pointer (size=%u)\n", pSG->cbTotal));
            NdisAdvanceNetBufferDataStart(pBuffer, pSG->cbTotal, false, NULL); /** @todo why bother? */
            NdisFreeNetBufferList(pBufList);
            pBufList = NULL;
        }
    }
    else
    {
        Log(("ERROR! nemuNetLwfWinSGtoNB: NdisRetreatNetBufferDataStart failed with 0x%x (size=%u)\n", Status, pSG->cbTotal));
        NdisFreeNetBufferList(pBufList);
        pBufList = NULL;
    }
#endif /* !NEMUNETLWF_SYNC_SEND */
    LogFlow(("<==nemuNetLwfWinSGtoNB: return %p\n", pBufList));
    return pBufList;
}

static PINTNETSG nemuNetLwfWinNBtoSG(PNEMUNETLWF_MODULE pModule, PNET_BUFFER pNetBuf)
{
    ULONG cbPacket = NET_BUFFER_DATA_LENGTH(pNetBuf);
    UINT cSegs = nemuNetLwfWinCalcSegments(pNetBuf);
    /* Allocate and initialize SG */
    PINTNETSG pSG = (PINTNETSG)NdisAllocateMemoryWithTagPriority(pModule->hFilter,
                                                                 RT_OFFSETOF(INTNETSG, aSegs[cSegs]),
                                                                 NEMUNETLWF_MEM_TAG,
                                                                 NormalPoolPriority);
    AssertReturn(pSG, pSG);
    Log4(("nemuNetLwfWinNBtoSG: allocated SG 0x%p\n", pSG));
    IntNetSgInitTempSegs(pSG, cbPacket /*cbTotal*/, cSegs, cSegs /*cSegsUsed*/);

    int rc = NDIS_STATUS_SUCCESS;
    ULONG uOffset = NET_BUFFER_CURRENT_MDL_OFFSET(pNetBuf);
    cSegs = 0;
    for (PMDL pMdl = NET_BUFFER_CURRENT_MDL(pNetBuf);
         pMdl != NULL && cbPacket > 0;
         pMdl = NDIS_MDL_LINKAGE(pMdl))
    {
        PUCHAR pSrc = (PUCHAR)MmGetSystemAddressForMdlSafe(pMdl, LowPagePriority);
        if (!pSrc)
        {
            rc = NDIS_STATUS_RESOURCES;
            break;
        }
        ULONG cbSrc = MmGetMdlByteCount(pMdl);
        if (uOffset)
        {
            Assert(uOffset < cbSrc);
            pSrc  += uOffset;
            cbSrc -= uOffset;
            uOffset = 0;
        }

        if (cbSrc > cbPacket)
            cbSrc = cbPacket;

        pSG->aSegs[cSegs].pv = pSrc;
        pSG->aSegs[cSegs].cb = cbSrc;
        pSG->aSegs[cSegs].Phys = NIL_RTHCPHYS;
        cSegs++;
        cbPacket -= cbSrc;
    }

    Assert(cSegs <= pSG->cSegsAlloc);

    if (RT_FAILURE(rc))
    {
        nemuNetLwfWinDestroySG(pSG);
        pSG = NULL;
    }
    else
    {
        Assert(cbPacket == 0);
        Assert(pSG->cSegsUsed == cSegs);
    }
    return pSG;
}

static void nemuNetLwfWinDisableOffloading(PNDIS_OFFLOAD pOffloadConfig)
{
    pOffloadConfig->Checksum.IPv4Transmit.Encapsulation               = NDIS_ENCAPSULATION_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv4Transmit.IpOptionsSupported          = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv4Transmit.TcpOptionsSupported         = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv4Transmit.TcpChecksum                 = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv4Transmit.UdpChecksum                 = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv4Transmit.IpChecksum                  = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv6Transmit.Encapsulation               = NDIS_ENCAPSULATION_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv6Transmit.IpExtensionHeadersSupported = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv6Transmit.TcpOptionsSupported         = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv6Transmit.TcpChecksum                 = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->Checksum.IPv6Transmit.UdpChecksum                 = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->LsoV1.IPv4.Encapsulation                          = NDIS_ENCAPSULATION_NOT_SUPPORTED;
    pOffloadConfig->LsoV1.IPv4.TcpOptions                             = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->LsoV1.IPv4.IpOptions                              = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->LsoV2.IPv4.Encapsulation                          = NDIS_ENCAPSULATION_NOT_SUPPORTED;
    pOffloadConfig->LsoV2.IPv6.Encapsulation                          = NDIS_ENCAPSULATION_NOT_SUPPORTED;
    pOffloadConfig->LsoV2.IPv6.IpExtensionHeadersSupported            = NDIS_OFFLOAD_NOT_SUPPORTED;
    pOffloadConfig->LsoV2.IPv6.TcpOptionsSupported                    = NDIS_OFFLOAD_NOT_SUPPORTED;
}

VOID nemuNetLwfWinStatus(IN NDIS_HANDLE hModuleCtx, IN PNDIS_STATUS_INDICATION pIndication)
{
    LogFlow(("==>nemuNetLwfWinStatus: module=%p\n", hModuleCtx));
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)hModuleCtx;
    Log(("nemuNetLwfWinStatus: Got status indication: %s\n", nemuNetLwfWinStatusToText(pIndication->StatusCode)));
    switch (pIndication->StatusCode)
    {
        case NDIS_STATUS_PACKET_FILTER:
            nemuNetLwfWinDumpFilterTypes(*(ULONG*)pIndication->StatusBuffer);
            nemuNetLwfWinOverridePacketFiltersUp(pModuleCtx, (ULONG*)pIndication->StatusBuffer);
            Log(("nemuNetLwfWinStatus: Reporting status: %s\n", nemuNetLwfWinStatusToText(pIndication->StatusCode)));
            nemuNetLwfWinDumpFilterTypes(*(ULONG*)pIndication->StatusBuffer);
            break;
        case NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG:
            Log5(("nemuNetLwfWinStatus: offloading currently set to:\n"));
            nemuNetLwfWinDumpOffloadSettings((PNDIS_OFFLOAD)pIndication->StatusBuffer);
            nemuNetLwfWinUpdateSavedOffloadConfig(pModuleCtx, (PNDIS_OFFLOAD)pIndication->StatusBuffer);
            if (ASMAtomicReadBool(&pModuleCtx->fActive))
                nemuNetLwfWinDisableOffloading((PNDIS_OFFLOAD)pIndication->StatusBuffer);
            Log5(("nemuNetLwfWinStatus: reporting offloading up as:\n"));
            nemuNetLwfWinDumpOffloadSettings((PNDIS_OFFLOAD)pIndication->StatusBuffer);
            break;
    }
    NdisFIndicateStatus(pModuleCtx->hFilter, pIndication);
    LogFlow(("<==nemuNetLwfWinStatus\n"));
}

static bool nemuNetLwfWinForwardToIntNet(PNEMUNETLWF_MODULE pModuleCtx, PNET_BUFFER_LIST pBufLists, uint32_t fSrc)
{
    /* We must not forward anything to the trunk unless it is ready to receive. */
    if (!ASMAtomicReadBool(&pModuleCtx->fActive))
    {
        Log(("nemuNetLwfWinForwardToIntNet: trunk is inactive, won't forward\n"));
        return false;
    }

    AssertReturn(pModuleCtx->pNetFlt, false);
    AssertReturn(pModuleCtx->pNetFlt->pSwitchPort, false);
    AssertReturn(pModuleCtx->pNetFlt->pSwitchPort->pfnRecv, false);
    LogFlow(("==>nemuNetLwfWinForwardToIntNet: module=%p\n", pModuleCtx));
    Assert(pBufLists);                                                   /* The chain must contain at least one list */
    Assert(NET_BUFFER_LIST_NEXT_NBL(pBufLists) == NULL); /* The caller is supposed to unlink the list from the chain */
    /*
     * Even if NBL contains more than one buffer we are prepared to deal with it.
     * When any of buffers should not be dropped we keep the whole list. It is
     * better to leak some "unexpected" packets to the wire/host than to loose any.
     */
    bool fDropIt   = false;
    bool fDontDrop = false;
    int nLists = 0;
    for (PNET_BUFFER_LIST pList = pBufLists; pList; pList = NET_BUFFER_LIST_NEXT_NBL(pList))
    {
        int nBuffers = 0;
        nLists++;
        for (PNET_BUFFER pBuf = NET_BUFFER_LIST_FIRST_NB(pList); pBuf; pBuf = NET_BUFFER_NEXT_NB(pBuf))
        {
            nBuffers++;
            PINTNETSG pSG = nemuNetLwfWinNBtoSG(pModuleCtx, pBuf);
            if (pSG)
            {
                nemuNetLwfWinDumpPacket(pSG, (fSrc & INTNETTRUNKDIR_WIRE)?"intnet <-- wire":"intnet <-- host");
                /* A bit paranoid, but we do not use any locks, so... */
                if (ASMAtomicReadBool(&pModuleCtx->fActive))
                    if (pModuleCtx->pNetFlt->pSwitchPort->pfnRecv(pModuleCtx->pNetFlt->pSwitchPort, NULL, pSG, fSrc))
                        fDropIt = true;
                    else
                        fDontDrop = true;
                nemuNetLwfWinDestroySG(pSG);
            }
        }
        Log(("nemuNetLwfWinForwardToIntNet: list=%d buffers=%d\n", nLists, nBuffers));
    }
    Log(("nemuNetLwfWinForwardToIntNet: lists=%d drop=%s don't=%s\n", nLists, fDropIt ? "true":"false", fDontDrop ? "true":"false"));
    LogFlow(("<==nemuNetLwfWinForwardToIntNet: return '%s'\n",
             fDropIt ? (fDontDrop ? "do not drop (some)" : "drop it") : "do not drop (any)"));
    return fDropIt && !fDontDrop; /* Drop the list if ALL its buffers are being dropped! */
}

DECLINLINE(bool) nemuNetLwfWinIsRunning(PNEMUNETLWF_MODULE pModule)
{
    Log(("nemuNetLwfWinIsRunning: state=%d\n", ASMAtomicReadU32(&pModule->enmState)));
    return ASMAtomicReadU32(&pModule->enmState) == LwfState_Running;
}

VOID nemuNetLwfWinSendNetBufferLists(IN NDIS_HANDLE hModuleCtx, IN PNET_BUFFER_LIST pBufLists, IN NDIS_PORT_NUMBER nPort, IN ULONG fFlags)
{
    size_t cb = 0;
    LogFlow(("==>nemuNetLwfWinSendNetBufferLists: module=%p\n", hModuleCtx));
    PNEMUNETLWF_MODULE pModule = (PNEMUNETLWF_MODULE)hModuleCtx;

    if (!ASMAtomicReadBool(&pModule->fActive))
    {
        /*
         * The trunk is inactive, jusp pass along all packets to the next
         * underlying driver.
         */
        NdisFSendNetBufferLists(pModule->hFilter, pBufLists, nPort, fFlags);
        return;
    }

    if (nemuNetLwfWinIsRunning(pModule))
    {
        PNET_BUFFER_LIST pNext     = NULL;
        PNET_BUFFER_LIST pDropHead = NULL;
        PNET_BUFFER_LIST pDropTail = NULL;
        PNET_BUFFER_LIST pPassHead = NULL;
        PNET_BUFFER_LIST pPassTail = NULL;
        for (PNET_BUFFER_LIST pList = pBufLists; pList; pList = pNext)
        {
            pNext = NET_BUFFER_LIST_NEXT_NBL(pList);
            NET_BUFFER_LIST_NEXT_NBL(pList) = NULL; /* Unlink */
            if (nemuNetLwfWinForwardToIntNet(pModule, pList, INTNETTRUNKDIR_HOST))
            {
                NET_BUFFER_LIST_STATUS(pList) = NDIS_STATUS_SUCCESS;
                if (pDropHead)
                {
                    NET_BUFFER_LIST_NEXT_NBL(pDropTail) = pList;
                    pDropTail = pList;
                }
                else
                    pDropHead = pDropTail = pList;
            }
            else
            {
                if (pPassHead)
                {
                    NET_BUFFER_LIST_NEXT_NBL(pPassTail) = pList;
                    pPassTail = pList;
                }
                else
                    pPassHead = pPassTail = pList;
            }
        }
        Assert((pBufLists == pPassHead) || (pBufLists == pDropHead));
        if (pPassHead)
        {
            nemuNetLwfWinDumpPackets("nemuNetLwfWinSendNetBufferLists: passing down", pPassHead);
            NdisFSendNetBufferLists(pModule->hFilter, pBufLists, nPort, fFlags);
        }
        if (pDropHead)
        {
            nemuNetLwfWinDumpPackets("nemuNetLwfWinSendNetBufferLists: consumed", pDropHead);
            NdisFSendNetBufferListsComplete(pModule->hFilter, pDropHead,
                                            fFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
        }
    }
    else
    {
        for (PNET_BUFFER_LIST pList = pBufLists; pList; pList = NET_BUFFER_LIST_NEXT_NBL(pList))
        {
            NET_BUFFER_LIST_STATUS(pList) = NDIS_STATUS_PAUSED;
        }
        nemuNetLwfWinDumpPackets("nemuNetLwfWinSendNetBufferLists: consumed", pBufLists);
        NdisFSendNetBufferListsComplete(pModule->hFilter, pBufLists,
                                        fFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);

    }
    LogFlow(("<==nemuNetLwfWinSendNetBufferLists\n"));
}

VOID nemuNetLwfWinSendNetBufferListsComplete(IN NDIS_HANDLE hModuleCtx, IN PNET_BUFFER_LIST pBufLists, IN ULONG fFlags)
{
    size_t cb = 0;
    LogFlow(("==>nemuNetLwfWinSendNetBufferListsComplete: module=%p\n", hModuleCtx));
    PNEMUNETLWF_MODULE pModule = (PNEMUNETLWF_MODULE)hModuleCtx;
    PNET_BUFFER_LIST pList = pBufLists;
    PNET_BUFFER_LIST pNextList;
    PNET_BUFFER_LIST pPrevList = NULL;
    while (pList)
    {
        pNextList = NET_BUFFER_LIST_NEXT_NBL(pList);
        if (pList->SourceHandle == pModule->hFilter)
        {
            /* We allocated this NET_BUFFER_LIST, let's free it up */
            Assert(NET_BUFFER_LIST_FIRST_NB(pList));
            Assert(NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(pList)));
            /*
             * All our NBLs hold a single NB each, no need to iterate over a list.
             * There is no need to free an associated NB explicitly either, as it was
             * preallocated with NBL structure.
             */
            Assert(!NET_BUFFER_NEXT_NB(NET_BUFFER_LIST_FIRST_NB(pList)));
            nemuNetLwfWinFreeMdlChain(NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(pList)));
            /* Unlink this list from the chain */
            if (pPrevList)
                NET_BUFFER_LIST_NEXT_NBL(pPrevList) = pNextList;
            else
                pBufLists = pNextList;
            Log(("nemuNetLwfWinSendNetBufferListsComplete: our list %p, next=%p, previous=%p, head=%p\n", pList, pNextList, pPrevList, pBufLists));
            NdisFreeNetBufferList(pList);
#ifdef NEMUNETLWF_SYNC_SEND
            Log4(("nemuNetLwfWinSendNetBufferListsComplete: freed NBL+NB 0x%p\n", pList));
            KeSetEvent(&pModule->EventWire, 0, FALSE);
#else /* !NEMUNETLWF_SYNC_SEND */
            Log4(("nemuNetLwfWinSendNetBufferListsComplete: freed NBL+NB+MDL+Data 0x%p\n", pList));
            Assert(ASMAtomicReadS32(&pModule->cPendingBuffers) > 0);
            if (ASMAtomicDecS32(&pModule->cPendingBuffers) == 0)
                NdisSetEvent(&pModule->EventSendComplete);
#endif /* !NEMUNETLWF_SYNC_SEND */
        }
        else
        {
            pPrevList = pList;
            Log(("nemuNetLwfWinSendNetBufferListsComplete: passing list %p, next=%p, previous=%p, head=%p\n", pList, pNextList, pPrevList, pBufLists));
        }
        pList = pNextList;
    }
    if (pBufLists)
    {
        /* There are still lists remaining in the chain, pass'em up */
        NdisFSendNetBufferListsComplete(pModule->hFilter, pBufLists, fFlags);
    }
    LogFlow(("<==nemuNetLwfWinSendNetBufferListsComplete\n"));
}

VOID nemuNetLwfWinReceiveNetBufferLists(IN NDIS_HANDLE hModuleCtx,
                                        IN PNET_BUFFER_LIST pBufLists,
                                        IN NDIS_PORT_NUMBER nPort,
                                        IN ULONG nBufLists,
                                        IN ULONG fFlags)
{
    /// @todo Do we need loopback handling?
    LogFlow(("==>nemuNetLwfWinReceiveNetBufferLists: module=%p\n", hModuleCtx));
    PNEMUNETLWF_MODULE pModule = (PNEMUNETLWF_MODULE)hModuleCtx;

    if (!ASMAtomicReadBool(&pModule->fActive))
    {
        /*
         * The trunk is inactive, jusp pass along all packets to the next
         * overlying driver.
         */
        NdisFIndicateReceiveNetBufferLists(pModule->hFilter, pBufLists, nPort, nBufLists, fFlags);
        LogFlow(("<==nemuNetLwfWinReceiveNetBufferLists: inactive trunk\n"));
        return;
    }

    if (nemuNetLwfWinIsRunning(pModule))
    {
        if (NDIS_TEST_RECEIVE_CANNOT_PEND(fFlags))
        {
            /* We do not own NBLs so we do not need to return them */
            /* First we need to scan through the list to see if some packets must be dropped */
            bool bDropIt = false;
            for (PNET_BUFFER_LIST pList = pBufLists; pList; pList = NET_BUFFER_LIST_NEXT_NBL(pList))
            {
                PNET_BUFFER_LIST pNext = NET_BUFFER_LIST_NEXT_NBL(pList);
                NET_BUFFER_LIST_NEXT_NBL(pList) = NULL; /* Unlink temporarily */
                if (nemuNetLwfWinForwardToIntNet(pModule, pList, INTNETTRUNKDIR_WIRE))
                    bDropIt = true;
                NET_BUFFER_LIST_NEXT_NBL(pList) = pNext; /* Restore the link */
            }
            if (bDropIt)
            {
                /* Some NBLs must be dropped, indicate selectively one by one */
                for (PNET_BUFFER_LIST pList = pBufLists; pList; pList = NET_BUFFER_LIST_NEXT_NBL(pList))
                {
                    PNET_BUFFER_LIST pNext = NET_BUFFER_LIST_NEXT_NBL(pList);
                    NET_BUFFER_LIST_NEXT_NBL(pList) = NULL; /* Unlink temporarily */
                    nemuNetLwfWinDumpPackets("nemuNetLwfWinReceiveNetBufferLists: passing up", pList);
                    NdisFIndicateReceiveNetBufferLists(pModule->hFilter, pList, nPort, nBufLists, fFlags);
                    NET_BUFFER_LIST_NEXT_NBL(pList) = pNext; /* Restore the link */
                }
            }
            else
            {
                /* All NBLs must be indicated, do it in bulk. */
                nemuNetLwfWinDumpPackets("nemuNetLwfWinReceiveNetBufferLists: passing up", pBufLists);
                NdisFIndicateReceiveNetBufferLists(pModule->hFilter, pBufLists, nPort, nBufLists, fFlags);
            }
        }
        else
        {
            /* We collect dropped NBLs in a separate list in order to "return" them. */
            PNET_BUFFER_LIST pNext     = NULL;
            PNET_BUFFER_LIST pDropHead = NULL;
            PNET_BUFFER_LIST pDropTail = NULL;
            PNET_BUFFER_LIST pPassHead = NULL;
            PNET_BUFFER_LIST pPassTail = NULL;
            ULONG nDrop = 0, nPass = 0;
            for (PNET_BUFFER_LIST pList = pBufLists; pList; pList = pNext)
            {
                pNext = NET_BUFFER_LIST_NEXT_NBL(pList);
                NET_BUFFER_LIST_NEXT_NBL(pList) = NULL; /* Unlink */
                if (nemuNetLwfWinForwardToIntNet(pModule, pList, INTNETTRUNKDIR_WIRE))
                {
                    if (nDrop++)
                    {
                        NET_BUFFER_LIST_NEXT_NBL(pDropTail) = pList;
                        pDropTail = pList;
                    }
                    else
                        pDropHead = pDropTail = pList;
                }
                else
                {
                    if (nPass++)
                    {
                        NET_BUFFER_LIST_NEXT_NBL(pPassTail) = pList;
                        pPassTail = pList;
                    }
                    else
                        pPassHead = pPassTail = pList;
                }
            }
            Assert((pBufLists == pPassHead) || (pBufLists == pDropHead));
            Assert(nDrop + nPass == nBufLists);
            if (pPassHead)
            {
                nemuNetLwfWinDumpPackets("nemuNetLwfWinReceiveNetBufferLists: passing up", pPassHead);
                NdisFIndicateReceiveNetBufferLists(pModule->hFilter, pPassHead, nPort, nPass, fFlags);
            }
            if (pDropHead)
            {
                nemuNetLwfWinDumpPackets("nemuNetLwfWinReceiveNetBufferLists: consumed", pDropHead);
                NdisFReturnNetBufferLists(pModule->hFilter, pDropHead,
                                          fFlags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL ? NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
            }
        }

    }
    else
    {
        nemuNetLwfWinDumpPackets("nemuNetLwfWinReceiveNetBufferLists: consumed", pBufLists);
        if ((fFlags & NDIS_RECEIVE_FLAGS_RESOURCES) == 0)
            NdisFReturnNetBufferLists(pModule->hFilter, pBufLists,
                                      fFlags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL ? NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
    }
    LogFlow(("<==nemuNetLwfWinReceiveNetBufferLists\n"));
}

VOID nemuNetLwfWinReturnNetBufferLists(IN NDIS_HANDLE hModuleCtx, IN PNET_BUFFER_LIST pBufLists, IN ULONG fFlags)
{
    size_t cb = 0;
    LogFlow(("==>nemuNetLwfWinReturnNetBufferLists: module=%p\n", hModuleCtx));
    PNEMUNETLWF_MODULE pModule = (PNEMUNETLWF_MODULE)hModuleCtx;
    PNET_BUFFER_LIST pList = pBufLists;
    PNET_BUFFER_LIST pNextList;
    PNET_BUFFER_LIST pPrevList = NULL;
    /** @todo Move common part into a separate function to be used by nemuNetLwfWinSendNetBufferListsComplete() as well */
    while (pList)
    {
        pNextList = NET_BUFFER_LIST_NEXT_NBL(pList);
        if (pList->SourceHandle == pModule->hFilter)
        {
            /* We allocated this NET_BUFFER_LIST, let's free it up */
            Assert(NET_BUFFER_LIST_FIRST_NB(pList));
            Assert(NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(pList)));
            /*
             * All our NBLs hold a single NB each, no need to iterate over a list.
             * There is no need to free an associated NB explicitly either, as it was
             * preallocated with NBL structure.
             */
            nemuNetLwfWinFreeMdlChain(NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(pList)));
            /* Unlink this list from the chain */
            if (pPrevList)
                NET_BUFFER_LIST_NEXT_NBL(pPrevList) = pNextList;
            else
                pBufLists = pNextList;
            NdisFreeNetBufferList(pList);
#ifdef NEMUNETLWF_SYNC_SEND
            Log4(("nemuNetLwfWinReturnNetBufferLists: freed NBL+NB 0x%p\n", pList));
            KeSetEvent(&pModule->EventHost, 0, FALSE);
#else /* !NEMUNETLWF_SYNC_SEND */
            Log4(("nemuNetLwfWinReturnNetBufferLists: freed NBL+NB+MDL+Data 0x%p\n", pList));
            Assert(ASMAtomicReadS32(&pModule->cPendingBuffers) > 0);
            if (ASMAtomicDecS32(&pModule->cPendingBuffers) == 0)
                NdisSetEvent(&pModule->EventSendComplete);
#endif /* !NEMUNETLWF_SYNC_SEND */
        }
        else
            pPrevList = pList;
        pList = pNextList;
    }
    if (pBufLists)
    {
        /* There are still lists remaining in the chain, pass'em up */
        NdisFReturnNetBufferLists(pModule->hFilter, pBufLists, fFlags);
    }
    LogFlow(("<==nemuNetLwfWinReturnNetBufferLists\n"));
}

/**
 * register the filter driver
 */
DECLHIDDEN(NDIS_STATUS) nemuNetLwfWinRegister(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPathStr)
{
    NDIS_FILTER_DRIVER_CHARACTERISTICS FChars;
    NDIS_STRING FriendlyName;
    NDIS_STRING UniqueName;
    NDIS_STRING ServiceName;

    NdisInitUnicodeString(&FriendlyName, NEMUNETLWF_NAME_FRIENDLY);
    NdisInitUnicodeString(&UniqueName, NEMUNETLWF_NAME_UNIQUE);
    NdisInitUnicodeString(&ServiceName, NEMUNETLWF_NAME_SERVICE);

    NdisZeroMemory(&FChars, sizeof (FChars));

    FChars.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
    FChars.Header.Size = sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS);
    FChars.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_1;

    FChars.MajorNdisVersion = NEMUNETLWF_VERSION_NDIS_MAJOR;
    FChars.MinorNdisVersion = NEMUNETLWF_VERSION_NDIS_MINOR;

    FChars.FriendlyName = FriendlyName;
    FChars.UniqueName = UniqueName;
    FChars.ServiceName = ServiceName;

    /* Mandatory functions */
    FChars.AttachHandler = nemuNetLwfWinAttach;
    FChars.DetachHandler = nemuNetLwfWinDetach;
    FChars.RestartHandler = nemuNetLwfWinRestart;
    FChars.PauseHandler = nemuNetLwfWinPause;

    /* Optional functions, non changeble at run-time */
    FChars.OidRequestHandler = nemuNetLwfWinOidRequest;
    FChars.OidRequestCompleteHandler = nemuNetLwfWinOidRequestComplete;
    //FChars.CancelOidRequestHandler = nemuNetLwfWinCancelOidRequest;
    FChars.StatusHandler = nemuNetLwfWinStatus;
    //FChars.NetPnPEventHandler = nemuNetLwfWinPnPEvent;

    /* Datapath functions */
    FChars.SendNetBufferListsHandler = nemuNetLwfWinSendNetBufferLists;
    FChars.SendNetBufferListsCompleteHandler = nemuNetLwfWinSendNetBufferListsComplete;
    FChars.ReceiveNetBufferListsHandler = nemuNetLwfWinReceiveNetBufferLists;
    FChars.ReturnNetBufferListsHandler = nemuNetLwfWinReturnNetBufferLists;

    pDriverObject->DriverUnload = nemuNetLwfWinUnloadDriver;

    NDIS_STATUS Status;
    g_NemuNetLwfGlobals.hFilterDriver = NULL;
    Log(("nemuNetLwfWinRegister: registering filter driver...\n"));
    Status = NdisFRegisterFilterDriver(pDriverObject,
                                       (NDIS_HANDLE)&g_NemuNetLwfGlobals,
                                       &FChars,
                                       &g_NemuNetLwfGlobals.hFilterDriver);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Log(("nemuNetLwfWinRegister: successfully registered filter driver; registering device...\n"));
        Status = nemuNetLwfWinDevCreate(&g_NemuNetLwfGlobals);
        Assert(Status == STATUS_SUCCESS);
        Log(("nemuNetLwfWinRegister: nemuNetLwfWinDevCreate() returned 0x%x\n", Status));
    }
    else
    {
        Log(("ERROR! nemuNetLwfWinRegister: failed to register filter driver, status=0x%x", Status));
    }
    return Status;
}

static int nemuNetLwfWinStartInitIdcThread()
{
    int rc = VERR_INVALID_STATE;

    if (ASMAtomicCmpXchgU32(&g_NemuNetLwfGlobals.enmIdcState, LwfIdcState_Connecting, LwfIdcState_Disconnected))
    {
        Log(("nemuNetLwfWinStartInitIdcThread: IDC state change Diconnected -> Connecting\n"));

        NTSTATUS Status = PsCreateSystemThread(&g_NemuNetLwfGlobals.hInitIdcThread,
                                               THREAD_ALL_ACCESS,
                                               NULL,
                                               NULL,
                                               NULL,
                                               nemuNetLwfWinInitIdcWorker,
                                               &g_NemuNetLwfGlobals);
        Log(("nemuNetLwfWinStartInitIdcThread: create IDC initialization thread, status=0x%x\n", Status));
        if (Status != STATUS_SUCCESS)
        {
            LogRel(("NETLWF: IDC initialization failed (system thread creation, status=0x%x)\n", Status));
            /*
             * We failed to init IDC and there will be no second chance.
             */
            Log(("nemuNetLwfWinStartInitIdcThread: IDC state change Connecting -> Diconnected\n"));
            ASMAtomicWriteU32(&g_NemuNetLwfGlobals.enmIdcState, LwfIdcState_Disconnected);
        }
        rc = RTErrConvertFromNtStatus(Status);
    }
    return rc;
}

static void nemuNetLwfWinStopInitIdcThread()
{
}


RT_C_DECLS_BEGIN

NTSTATUS DriverEntry(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath);

RT_C_DECLS_END

NTSTATUS DriverEntry(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    int rc;

    /* the idc registration is initiated via IOCTL since our driver
     * can be loaded when the NemuDrv is not in case we are a Ndis IM driver */
    rc = nemuNetLwfWinInitBase();
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        NdisZeroMemory(&g_NemuNetLwfGlobals, sizeof (g_NemuNetLwfGlobals));
        RTListInit(&g_NemuNetLwfGlobals.listModules);
        NdisAllocateSpinLock(&g_NemuNetLwfGlobals.Lock);
        /*
         * We choose to ignore IDC initialization errors here because if we fail to load
         * our filter the upper protocols won't bind to the associated adapter, causing
         * network failure at the host. Better to have non-working filter than broken
         * networking on the host.
         */
        rc = nemuNetLwfWinStartInitIdcThread();
        AssertRC(rc);

        Status = nemuNetLwfWinRegister(pDriverObject, pRegistryPath);
        Assert(Status == STATUS_SUCCESS);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            Log(("NETLWF: started successfully\n"));
            return STATUS_SUCCESS;
        }
        NdisFreeSpinLock(&g_NemuNetLwfGlobals.Lock);
        nemuNetLwfWinFini();
    }
    else
    {
        Status = NDIS_STATUS_FAILURE;
    }

    return Status;
}


static VOID nemuNetLwfWinUnloadDriver(IN PDRIVER_OBJECT pDriver)
{
    LogFlow(("==>nemuNetLwfWinUnloadDriver: driver=%p\n", pDriver));
    nemuNetLwfWinDevDestroy(&g_NemuNetLwfGlobals);
    NdisFDeregisterFilterDriver(g_NemuNetLwfGlobals.hFilterDriver);
    NdisFreeSpinLock(&g_NemuNetLwfGlobals.Lock);
    LogFlow(("<==nemuNetLwfWinUnloadDriver\n"));
    nemuNetLwfWinFini();
}

static const char *nemuNetLwfWinIdcStateToText(uint32_t enmState)
{
    switch (enmState)
    {
        case LwfIdcState_Disconnected: return "Disconnected";
        case LwfIdcState_Connecting: return "Connecting";
        case LwfIdcState_Connected: return "Connected";
        case LwfIdcState_Stopping: return "Stopping";
    }
    return "Unknown";
}

static VOID nemuNetLwfWinInitIdcWorker(PVOID pvContext)
{
    int rc;
    PNEMUNETLWFGLOBALS pGlobals = (PNEMUNETLWFGLOBALS)pvContext;

    while (ASMAtomicReadU32(&pGlobals->enmIdcState) == LwfIdcState_Connecting)
    {
        rc = nemuNetFltInitIdc(&g_NemuNetFltGlobals);
        if (RT_SUCCESS(rc))
        {
            if (!ASMAtomicCmpXchgU32(&pGlobals->enmIdcState, LwfIdcState_Connected, LwfIdcState_Connecting))
            {
                /* The state has been changed (the only valid transition is to "Stopping"), undo init */
                rc = nemuNetFltTryDeleteIdc(&g_NemuNetFltGlobals);
                Log(("nemuNetLwfWinInitIdcWorker: state change (Connecting -> %s) while initializing IDC, deleted IDC, rc=0x%x\n",
                     nemuNetLwfWinIdcStateToText(ASMAtomicReadU32(&pGlobals->enmIdcState)), rc));
            }
            else
            {
                Log(("nemuNetLwfWinInitIdcWorker: IDC state change Connecting -> Connected\n"));
            }
        }
        else
        {
            LARGE_INTEGER WaitIn100nsUnits;
            WaitIn100nsUnits.QuadPart = -(LONGLONG)10000000; /* 1 sec */
            KeDelayExecutionThread(KernelMode, FALSE /* non-alertable */, &WaitIn100nsUnits);
        }
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static int nemuNetLwfWinTryFiniIdc()
{
    int rc = VINF_SUCCESS;
    NTSTATUS Status;
    PKTHREAD pThread = NULL;
    uint32_t enmPrevState = ASMAtomicXchgU32(&g_NemuNetLwfGlobals.enmIdcState, LwfIdcState_Stopping);

    Log(("nemuNetLwfWinTryFiniIdc: IDC state change %s -> Stopping\n", nemuNetLwfWinIdcStateToText(enmPrevState)));

    switch (enmPrevState)
    {
        case LwfIdcState_Disconnected:
            /* Have not even attempted to connect -- nothing to do. */
            break;
        case LwfIdcState_Stopping:
            /* Impossible, but another thread is alreading doing FiniIdc, bail out */
            Log(("ERROR! nemuNetLwfWinTryFiniIdc: called in 'Stopping' state\n"));
            rc = VERR_INVALID_STATE;
            break;
        case LwfIdcState_Connecting:
            /* the worker thread is running, let's wait for it to stop */
            Status = ObReferenceObjectByHandle(g_NemuNetLwfGlobals.hInitIdcThread,
                                               THREAD_ALL_ACCESS, NULL, KernelMode,
                                               (PVOID*)&pThread, NULL);
            if (Status == STATUS_SUCCESS)
            {
                KeWaitForSingleObject(pThread, Executive, KernelMode, FALSE, NULL);
                ObDereferenceObject(pThread);
            }
            else
            {
                Log(("ERROR! nemuNetLwfWinTryFiniIdc: ObReferenceObjectByHandle(%p) failed with 0x%x\n",
                     g_NemuNetLwfGlobals.hInitIdcThread, Status));
            }
            rc = RTErrConvertFromNtStatus(Status);
            break;
        case LwfIdcState_Connected:
            /* the worker succeeded in IDC init and terminated */
            rc = nemuNetFltTryDeleteIdc(&g_NemuNetFltGlobals);
            Log(("nemuNetLwfWinTryFiniIdc: deleted IDC, rc=0x%x\n", rc));
            break;
    }
    return rc;
}

static void nemuNetLwfWinFiniBase()
{
    nemuNetFltDeleteGlobals(&g_NemuNetFltGlobals);

    /*
     * Undo the work done during start (in reverse order).
     */
    memset(&g_NemuNetFltGlobals, 0, sizeof(g_NemuNetFltGlobals));

    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
    RTLogDestroy(RTLogSetDefaultInstance(NULL));

    RTR0Term();
}

static int nemuNetLwfWinInitBase()
{
    int rc = RTR0Init(0);
    if (!RT_SUCCESS(rc))
        return rc;

    memset(&g_NemuNetFltGlobals, 0, sizeof(g_NemuNetFltGlobals));
    rc = nemuNetFltInitGlobals(&g_NemuNetFltGlobals);
    if (!RT_SUCCESS(rc))
        RTR0Term();

    return rc;
}

static int nemuNetLwfWinFini()
{
    int rc = nemuNetLwfWinTryFiniIdc();
    if (RT_SUCCESS(rc))
    {
        nemuNetLwfWinFiniBase();
    }
    return rc;
}


/*
 *
 * The OS specific interface definition
 *
 */


bool nemuNetFltOsMaybeRediscovered(PNEMUNETFLTINS pThis)
{
    LogFlow(("==>nemuNetFltOsMaybeRediscovered: instance=%p\n", pThis));
    LogFlow(("<==nemuNetFltOsMaybeRediscovered: return %RTbool\n", !ASMAtomicUoReadBool(&pThis->fDisconnectedFromHost)));
    /* AttachToInterface true if disconnected */
    return !ASMAtomicUoReadBool(&pThis->fDisconnectedFromHost);
}

int nemuNetFltPortOsXmit(PNEMUNETFLTINS pThis, void *pvIfData, PINTNETSG pSG, uint32_t fDst)
{
    int rc = VINF_SUCCESS;

    PNEMUNETLWF_MODULE pModule = (PNEMUNETLWF_MODULE)pThis->u.s.WinIf.hModuleCtx;
    LogFlow(("==>nemuNetFltPortOsXmit: instance=%p module=%p\n", pThis, pModule));
    if (!pModule)
    {
        LogFlow(("<==nemuNetFltPortOsXmit: pModule is null, return %d\n", VERR_INTERNAL_ERROR));
        return VERR_INTERNAL_ERROR;
    }
    /* Prevent going into "paused" state until all transmissions have been completed. */
    NDIS_WAIT_FOR_MUTEX(&pModule->InTransmit);
    /* Ignore all sends if the stack is paused or being paused, etc... */
    if (!nemuNetLwfWinIsRunning(pModule))
    {
        NDIS_RELEASE_MUTEX(&pModule->InTransmit);
        return VINF_SUCCESS;
    }

    const char *pszDir = (fDst & INTNETTRUNKDIR_WIRE) ?
        ( (fDst & INTNETTRUNKDIR_HOST) ? "intnet --> all" : "intnet --> wire" ) : "intnet --> host";
    nemuNetLwfWinDumpPacket(pSG, pszDir);
    /*
     * There are two possible strategies to deal with incoming SGs:
     * 1) make a copy of data and complete asynchronously;
     * 2) complete synchronously using the original data buffers.
     * Before we consider implementing (1) it is quite interesting to see
     * how well (2) performs. So we block until our requests are complete.
     * Actually there is third possibility -- to use SG retain/release
     * callbacks, but those seem not be fully implemented yet.
     * Note that ansynchronous completion will require different implementation
     * of nemuNetLwfWinPause(), not relying on InTransmit mutex.
     */
#ifdef NEMUNETLWF_SYNC_SEND
    PVOID aEvents[2]; /* To wire and to host */
    ULONG nEvents = 0;
    LARGE_INTEGER timeout;
    timeout.QuadPart = -(LONGLONG)10000000; /* 1 sec */
#endif /* NEMUNETLWF_SYNC_SEND */
    if (fDst & INTNETTRUNKDIR_WIRE)
    {
        PNET_BUFFER_LIST pBufList = nemuNetLwfWinSGtoNB(pModule, pSG);
        if (pBufList)
        {
#ifdef NEMUNETLWF_SYNC_SEND
            aEvents[nEvents++] = &pModule->EventWire;
#else /* !NEMUNETLWF_SYNC_SEND */
            if (ASMAtomicIncS32(&pModule->cPendingBuffers) == 1)
                NdisResetEvent(&pModule->EventSendComplete);
#endif /* !NEMUNETLWF_SYNC_SEND */
            NdisFSendNetBufferLists(pModule->hFilter, pBufList, NDIS_DEFAULT_PORT_NUMBER, 0); /** @todo sendFlags! */
        }
    }
    if (fDst & INTNETTRUNKDIR_HOST)
    {
        PNET_BUFFER_LIST pBufList = nemuNetLwfWinSGtoNB(pModule, pSG);
        if (pBufList)
        {
#ifdef NEMUNETLWF_SYNC_SEND
            aEvents[nEvents++] = &pModule->EventHost;
#else /* !NEMUNETLWF_SYNC_SEND */
            if (ASMAtomicIncS32(&pModule->cPendingBuffers) == 1)
                NdisResetEvent(&pModule->EventSendComplete);
#endif /* !NEMUNETLWF_SYNC_SEND */
            NdisFIndicateReceiveNetBufferLists(pModule->hFilter, pBufList, NDIS_DEFAULT_PORT_NUMBER, 1, 0);
        }
    }
#ifdef NEMUNETLWF_SYNC_SEND
    if (nEvents)
    {
        NTSTATUS Status = KeWaitForMultipleObjects(nEvents, aEvents, WaitAll, Executive, KernelMode, FALSE, &timeout, NULL);
        if (Status != STATUS_SUCCESS)
        {
            Log(("ERROR! nemuNetFltPortOsXmit: KeWaitForMultipleObjects() failed with 0x%x\n", Status));
            if (Status == STATUS_TIMEOUT)
                rc = VERR_TIMEOUT;
            else
                rc = RTErrConvertFromNtStatus(Status);
        }
    }
#endif /* NEMUNETLWF_SYNC_SEND */
    NDIS_RELEASE_MUTEX(&pModule->InTransmit);

    LogFlow(("<==nemuNetFltPortOsXmit: return %d\n", rc));
    return rc;
}


NDIS_IO_WORKITEM_FUNCTION nemuNetLwfWinToggleOffloading;

VOID nemuNetLwfWinToggleOffloading(PVOID WorkItemContext, NDIS_HANDLE NdisIoWorkItemHandle)
{
    /* WARNING! Call this with IRQL=Passive! */
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)WorkItemContext;

    if (ASMAtomicReadBool(&pModuleCtx->fActive))
    {
        /* Disable offloading temporarily by indicating offload config change. */
        /** @todo Be sure to revise this when implementing offloading support! */
        NDIS_OFFLOAD OffloadConfig;
        if (pModuleCtx->fOffloadConfigValid)
        {
            OffloadConfig = pModuleCtx->SavedOffloadConfig;
            nemuNetLwfWinDisableOffloading(&OffloadConfig);
        }
        else
        {
            DbgPrint("NemuNetLwf: no saved offload config to modify for %s\n", pModuleCtx->szMiniportName);
            NdisZeroMemory(&OffloadConfig, sizeof(OffloadConfig));
            OffloadConfig.Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
            OffloadConfig.Header.Revision = NDIS_OFFLOAD_REVISION_1;
            OffloadConfig.Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1;
        }
        nemuNetLwfWinIndicateOffload(pModuleCtx, &OffloadConfig);
        Log(("nemuNetLwfWinToggleOffloading: set offloading off\n"));
    }
    else
    {
        /* The filter is inactive -- restore offloading configuration. */
        if (pModuleCtx->fOffloadConfigValid)
        {
            nemuNetLwfWinIndicateOffload(pModuleCtx, &pModuleCtx->SavedOffloadConfig);
            Log(("nemuNetLwfWinToggleOffloading: restored offloading config\n"));
        }
        else
            DbgPrint("NemuNetLwf: no saved offload config to restore for %s\n", pModuleCtx->szMiniportName);
    }
}


void nemuNetFltPortOsSetActive(PNEMUNETFLTINS pThis, bool fActive)
{
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)pThis->u.s.WinIf.hModuleCtx;
    LogFlow(("==>nemuNetFltPortOsSetActive: instance=%p module=%p fActive=%RTbool\n", pThis, pModuleCtx, fActive));
    if (!pModuleCtx)
    {
        LogFlow(("<==nemuNetFltPortOsSetActive: pModuleCtx is null\n"));
        return;
    }

    NDIS_STATUS Status = STATUS_SUCCESS;
    bool fOldActive = ASMAtomicXchgBool(&pModuleCtx->fActive, fActive);
    if (fOldActive != fActive)
    {
        NdisQueueIoWorkItem(pModuleCtx->hWorkItem, nemuNetLwfWinToggleOffloading, pModuleCtx);
        Status = nemuNetLwfWinSetPacketFilter(pModuleCtx, fActive);
        LogFlow(("<==nemuNetFltPortOsSetActive: nemuNetLwfWinSetPacketFilter() returned 0x%x\n", Status));
    }
    else
        LogFlow(("<==nemuNetFltPortOsSetActive: no change, remain %sactive\n", fActive ? "":"in"));
}

int nemuNetFltOsDisconnectIt(PNEMUNETFLTINS pThis)
{
    LogFlow(("==>nemuNetFltOsDisconnectIt: instance=%p\n", pThis));
    LogFlow(("<==nemuNetFltOsDisconnectIt: return 0\n"));
    return VINF_SUCCESS;
}

int nemuNetFltOsConnectIt(PNEMUNETFLTINS pThis)
{
    LogFlow(("==>nemuNetFltOsConnectIt: instance=%p\n", pThis));
    LogFlow(("<==nemuNetFltOsConnectIt: return 0\n"));
    return VINF_SUCCESS;
}

/*
 * Uncommenting the following line produces debug log messages on IP address changes,
 * including wired interfaces. No actual calls to a switch port are made. This is for
 * debug purposes only!
 * #define NEMUNETLWFWIN_DEBUGIPADDRNOTIF 1
 */
static void nemuNetLwfWinIpAddrChangeCallback(IN PVOID pvCtx,
                                              IN PMIB_UNICASTIPADDRESS_ROW pRow,
                                              IN MIB_NOTIFICATION_TYPE enmNotifType)
{
    PNEMUNETFLTINS pThis = (PNEMUNETFLTINS)pvCtx;
    PNEMUNETLWF_MODULE pModule = (PNEMUNETLWF_MODULE)pThis->u.s.WinIf.hModuleCtx;

    /* We are only interested in add or remove notifications. */
    bool fAdded;
    if (enmNotifType == MibAddInstance)
        fAdded = true;
    else if (enmNotifType == MibDeleteInstance)
        fAdded = false;
    else
        return;

    if (   pRow
#ifndef NEMUNETLWFWIN_DEBUGIPADDRNOTIF
        && pThis->pSwitchPort->pfnNotifyHostAddress
#endif /* !NEMUNETLWFWIN_DEBUGIPADDRNOTIF */
       )
    {
        switch (pRow->Address.si_family)
        {
            case AF_INET:
                if (   IN4_IS_ADDR_LINKLOCAL(&pRow->Address.Ipv4.sin_addr)
                    || pRow->Address.Ipv4.sin_addr.s_addr == IN4ADDR_LOOPBACK)
                {
                    Log(("nemuNetLwfWinIpAddrChangeCallback: ignoring %s address (%RTnaipv4)\n",
                         pRow->Address.Ipv4.sin_addr.s_addr == IN4ADDR_LOOPBACK ? "loopback" : "link-local",
                         pRow->Address.Ipv4.sin_addr));
                    break;
                }
                Log(("nemuNetLwfWinIpAddrChangeCallback: %s IPv4 addr=%RTnaipv4 on luid=(%u,%u)\n",
                     fAdded ? "add" : "remove", pRow->Address.Ipv4.sin_addr,
                     pRow->InterfaceLuid.Info.IfType, pRow->InterfaceLuid.Info.NetLuidIndex));
#ifndef NEMUNETLWFWIN_DEBUGIPADDRNOTIF
                pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort, fAdded, kIntNetAddrType_IPv4,
                                                         &pRow->Address.Ipv4.sin_addr);
#endif /* !NEMUNETLWFWIN_DEBUGIPADDRNOTIF */
                break;
            case AF_INET6:
                if (Ipv6AddressScope(pRow->Address.Ipv6.sin6_addr.u.Byte) <= ScopeLevelLink)
                {
                    Log(("nemuNetLwfWinIpAddrChangeCallback: ignoring link-local address (%RTnaipv6)\n",
                         &pRow->Address.Ipv6.sin6_addr));
                    break;
                }
                Log(("nemuNetLwfWinIpAddrChangeCallback: %s IPv6 addr=%RTnaipv6 scope=%d luid=(%u,%u)\n",
                     fAdded ? "add" : "remove", &pRow->Address.Ipv6.sin6_addr,
                     Ipv6AddressScope(pRow->Address.Ipv6.sin6_addr.u.Byte),
                     pRow->InterfaceLuid.Info.IfType, pRow->InterfaceLuid.Info.NetLuidIndex));
#ifndef NEMUNETLWFWIN_DEBUGIPADDRNOTIF
                pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort, fAdded, kIntNetAddrType_IPv6,
                                                         &pRow->Address.Ipv6.sin6_addr);
#endif /* !NEMUNETLWFWIN_DEBUGIPADDRNOTIF */
                break;
        }
    }
    else
        Log(("nemuNetLwfWinIpAddrChangeCallback: pRow=%p pfnNotifyHostAddress=%p\n",
             pRow, pThis->pSwitchPort->pfnNotifyHostAddress));
}

void nemuNetLwfWinRegisterIpAddrNotifier(PNEMUNETFLTINS pThis)
{
    LogFlow(("==>nemuNetLwfWinRegisterIpAddrNotifier: instance=%p\n", pThis));
    if (   pThis->pSwitchPort
#ifndef NEMUNETLWFWIN_DEBUGIPADDRNOTIF
        && pThis->pSwitchPort->pfnNotifyHostAddress
#endif /* !NEMUNETLWFWIN_DEBUGIPADDRNOTIF */
       )
    {
        NETIO_STATUS Status;
        /* First we need to go over all host IP addresses and add them via pfnNotifyHostAddress. */
        PMIB_UNICASTIPADDRESS_TABLE HostIpAddresses = NULL;
        Status = GetUnicastIpAddressTable(AF_UNSPEC, &HostIpAddresses);
        if (NETIO_SUCCESS(Status))
        {
            for (unsigned i = 0; i < HostIpAddresses->NumEntries; i++)
                nemuNetLwfWinIpAddrChangeCallback(pThis, &HostIpAddresses->Table[i], MibAddInstance);
        }
        else
            Log(("ERROR! nemuNetLwfWinRegisterIpAddrNotifier: GetUnicastIpAddressTable failed with %x\n", Status));
        /* Now we can register a callback function to keep track of address changes. */
        Status = NotifyUnicastIpAddressChange(AF_UNSPEC, nemuNetLwfWinIpAddrChangeCallback,
                                              pThis, false, &pThis->u.s.WinIf.hNotifier);
        if (NETIO_SUCCESS(Status))
            Log(("nemuNetLwfWinRegisterIpAddrNotifier: notifier=%p\n", pThis->u.s.WinIf.hNotifier));
        else
            Log(("ERROR! nemuNetLwfWinRegisterIpAddrNotifier: NotifyUnicastIpAddressChange failed with %x\n", Status));
    }
    else
        pThis->u.s.WinIf.hNotifier = NULL;
    LogFlow(("<==nemuNetLwfWinRegisterIpAddrNotifier\n"));
}

void nemuNetLwfWinUnregisterIpAddrNotifier(PNEMUNETFLTINS pThis)
{
    Log(("nemuNetLwfWinUnregisterIpAddrNotifier: notifier=%p\n", pThis->u.s.WinIf.hNotifier));
    if (pThis->u.s.WinIf.hNotifier)
        CancelMibChangeNotify2(pThis->u.s.WinIf.hNotifier);
}

void nemuNetFltOsDeleteInstance(PNEMUNETFLTINS pThis)
{
    PNEMUNETLWF_MODULE pModuleCtx = (PNEMUNETLWF_MODULE)pThis->u.s.WinIf.hModuleCtx;
    LogFlow(("==>nemuNetFltOsDeleteInstance: instance=%p module=%p\n", pThis, pModuleCtx));
    /* Cancel IP address change notifications */
    nemuNetLwfWinUnregisterIpAddrNotifier(pThis);
    /* Technically it is possible that the module has already been gone by now. */
    if (pModuleCtx)
    {
        Assert(!pModuleCtx->fActive); /* Deactivation ensures bypass mode */
        pModuleCtx->pNetFlt = NULL;
        pThis->u.s.WinIf.hModuleCtx = NULL;
    }
    LogFlow(("<==nemuNetFltOsDeleteInstance\n"));
}

static void nemuNetLwfWinReportCapabilities(PNEMUNETFLTINS pThis, PNEMUNETLWF_MODULE pModuleCtx)
{
    if (pThis->pSwitchPort
        && nemuNetFltTryRetainBusyNotDisconnected(pThis))
    {
        pThis->pSwitchPort->pfnReportMacAddress(pThis->pSwitchPort, &pModuleCtx->MacAddr);
        pThis->pSwitchPort->pfnReportPromiscuousMode(pThis->pSwitchPort,
                                                     nemuNetLwfWinIsPromiscuous(pModuleCtx));
        pThis->pSwitchPort->pfnReportGsoCapabilities(pThis->pSwitchPort, 0,
                                                     INTNETTRUNKDIR_WIRE | INTNETTRUNKDIR_HOST);
        pThis->pSwitchPort->pfnReportNoPreemptDsts(pThis->pSwitchPort, 0 /* none */);
        nemuNetFltRelease(pThis, true /*fBusy*/);
    }
}

int nemuNetFltOsInitInstance(PNEMUNETFLTINS pThis, void *pvContext)
{
    LogFlow(("==>nemuNetFltOsInitInstance: instance=%p context=%p\n", pThis, pvContext));
    AssertReturn(pThis, VERR_INVALID_PARAMETER);
    Log(("nemuNetFltOsInitInstance: trunk name=%s\n", pThis->szName));
    PNEMUNETLWF_MODULE pModuleCtx = NULL;
    NdisAcquireSpinLock(&g_NemuNetLwfGlobals.Lock);
    RTListForEach(&g_NemuNetLwfGlobals.listModules, pModuleCtx, NEMUNETLWF_MODULE, node)
    {
        DbgPrint("nemuNetFltOsInitInstance: evaluating module, name=%s\n", pModuleCtx->szMiniportName);
        if (!RTStrICmp(pThis->szName, pModuleCtx->szMiniportName))
        {
            NdisReleaseSpinLock(&g_NemuNetLwfGlobals.Lock);
            Log(("nemuNetFltOsInitInstance: found matching module, name=%s\n", pThis->szName));
            pThis->u.s.WinIf.hModuleCtx = pModuleCtx;
            pModuleCtx->pNetFlt = pThis;
            nemuNetLwfWinReportCapabilities(pThis, pModuleCtx);
            nemuNetLwfWinRegisterIpAddrNotifier(pThis);
            LogFlow(("<==nemuNetFltOsInitInstance: return 0\n"));
            return VINF_SUCCESS;
        }
    }
    NdisReleaseSpinLock(&g_NemuNetLwfGlobals.Lock);
    LogFlow(("<==nemuNetFltOsInitInstance: return VERR_INTNET_FLT_IF_NOT_FOUND\n"));
    return VERR_INTNET_FLT_IF_NOT_FOUND;
}

int nemuNetFltOsPreInitInstance(PNEMUNETFLTINS pThis)
{
    LogFlow(("==>nemuNetFltOsPreInitInstance: instance=%p\n", pThis));
    pThis->u.s.WinIf.hModuleCtx = 0;
    pThis->u.s.WinIf.hNotifier  = NULL;
    LogFlow(("<==nemuNetFltOsPreInitInstance: return 0\n"));
    return VINF_SUCCESS;
}

void nemuNetFltPortOsNotifyMacAddress(PNEMUNETFLTINS pThis, void *pvIfData, PCRTMAC pMac)
{
    LogFlow(("==>nemuNetFltPortOsNotifyMacAddress: instance=%p data=%p mac=%RTmac\n", pThis, pvIfData, pMac));
    LogFlow(("<==nemuNetFltPortOsNotifyMacAddress\n"));
}

int nemuNetFltPortOsConnectInterface(PNEMUNETFLTINS pThis, void *pvIf, void **ppvIfData)
{
    LogFlow(("==>nemuNetFltPortOsConnectInterface: instance=%p if=%p data=%p\n", pThis, pvIf, ppvIfData));
    LogFlow(("<==nemuNetFltPortOsConnectInterface: return 0\n"));
    /* Nothing to do */
    return VINF_SUCCESS;
}

int nemuNetFltPortOsDisconnectInterface(PNEMUNETFLTINS pThis, void *pvIfData)
{
    LogFlow(("==>nemuNetFltPortOsDisconnectInterface: instance=%p data=%p\n", pThis, pvIfData));
    LogFlow(("<==nemuNetFltPortOsDisconnectInterface: return 0\n"));
    /* Nothing to do */
    return VINF_SUCCESS;
}

