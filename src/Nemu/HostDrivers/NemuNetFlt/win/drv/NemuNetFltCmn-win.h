/* $Id: NemuNetFltCmn-win.h $ */
/** @file
 * NemuNetFltCmn-win.h - Bridged Networking Driver, Windows Specific Code.
 * Common header with configuration defines and global defs
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

#ifndef ___NemuNetFltCmn_win_h___
#define ___NemuNetFltCmn_win_h___

#define LOG_GROUP LOG_GROUP_NET_FLT_DRV

/* debugging flags */
#ifdef DEBUG
//# define DEBUG_NETFLT_PACKETS
# ifndef DEBUG_misha
#  define RT_NO_STRICT
# endif
/* # define DEBUG_NETFLT_LOOPBACK */
/* receive logic has several branches */
/* the DEBUG_NETFLT_RECV* macros used to debug the ProtocolReceive callback
 * which is typically not used in case the underlying miniport indicates the packets with NdisMIndicateReceivePacket
 * the best way to debug the ProtocolReceive (which in turn has several branches) is to enable the DEBUG_NETFLT_RECV
 * one by one in the below order, i.e.
 * first DEBUG_NETFLT_RECV
 * then DEBUG_NETFLT_RECV + DEBUG_NETFLT_RECV_NOPACKET */
//# define DEBUG_NETFLT_RECV
//# define DEBUG_NETFLT_RECV_NOPACKET
//# define DEBUG_NETFLT_RECV_TRANSFERDATA
/* use ExAllocatePoolWithTag instead of NdisAllocateMemoryWithTag */
// #define DEBUG_NETFLT_USE_EXALLOC
#endif

#include <Nemu/intnet.h>
#include <Nemu/log.h>
#include <Nemu/err.h>
#include <Nemu/version.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/spinlock.h>
#include <iprt/semaphore.h>
#include <iprt/process.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/time.h>
#include <iprt/net.h>
#include <iprt/list.h>

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
#else
//#  include <ntddk.h>
/* can include ndis.h right away */
#  include <ndis.h>
#endif
RT_C_DECLS_END

#define NEMUNETFLT_OS_SPECFIC 1

/** version
 * NOTE: we are NOT using NDIS 5.1 features now */
#ifdef NDIS51_MINIPORT
# define NEMUNETFLT_VERSION_MP_NDIS_MAJOR     5
# define NEMUNETFLT_VERSION_MP_NDIS_MINOR     1
#else
# define NEMUNETFLT_VERSION_MP_NDIS_MAJOR     5
# define NEMUNETFLT_VERSION_MP_NDIS_MINOR     0
#endif

#ifndef NEMUNETADP
#ifdef NDIS51
# define NEMUNETFLT_VERSION_PT_NDIS_MAJOR     5
# define NEMUNETFLT_VERSION_PT_NDIS_MINOR     1 /* todo: use 0 here as well ? */
#else
# define NEMUNETFLT_VERSION_PT_NDIS_MAJOR     5
# define NEMUNETFLT_VERSION_PT_NDIS_MINOR     0
#endif

# define NEMUNETFLT_NAME_PROTOCOL             L"NemuNetFlt"
/** device to be used to prevent the driver unload & ioctl interface (if necessary in the future) */
# define NEMUNETFLT_NAME_LINK                 L"\\DosDevices\\Global\\NemuNetFlt"
# define NEMUNETFLT_NAME_DEVICE               L"\\Device\\NemuNetFlt"
#else
# define NEMUNETFLT_NAME_LINK                 L"\\DosDevices\\Global\\NemuNetAdp"
# define NEMUNETFLT_NAME_DEVICE               L"\\Device\\NemuNetAdp"
#endif

typedef struct NEMUNETFLTINS *PNEMUNETFLTINS;

/** configuration */

/** Ndis Packet pool settings
 * these are applied to both receive and send packet pools */
/* number of packets for normal used */
#define NEMUNETFLT_PACKET_POOL_SIZE_NORMAL    0x000000FF
/* number of additional overflow packets */
#define NEMUNETFLT_PACKET_POOL_SIZE_OVERFLOW  0x0000FF00

/** packet queue size used when the driver is working in the "active" mode */
#define NEMUNETFLT_PACKET_INFO_POOL_SIZE      0x0000FFFF

#ifndef NEMUNETADP
/** memory tag used for memory allocations
 * (VBNF stands for Nemu NetFlt) */
# define NEMUNETFLT_MEM_TAG                   'FNBV'
#else
/** memory tag used for memory allocations
 * (VBNA stands for Nemu NetAdp) */
# define NEMUNETFLT_MEM_TAG                   'ANBV'
#endif

/** receive and transmit Ndis buffer pool size */
#define NEMUNETFLT_BUFFER_POOL_SIZE_TX        128
#define NEMUNETFLT_BUFFER_POOL_SIZE_RX        128

#define NEMUNETFLT_PACKET_ETHEADER_SIZE       14
#define NEMUNETFLT_PACKET_HEADER_MATCH_SIZE   24
#define NEMUNETFLT_PACKET_QUEUE_SG_SEGS_ALLOC 32


#if defined(DEBUG_NETFLT_PACKETS) || !defined(NEMU_LOOPBACK_USEFLAGS)
# define NEMUNETFLT_PACKETMATCH_LENGTH        (NEMUNETFLT_PACKET_ETHEADER_SIZE + 2)
#endif

#ifdef NEMUNETADP
#define NEMUNETADP_HEADER_SIZE                14
#define NEMUNETADP_MAX_DATA_SIZE              1500
#define NEMUNETADP_MAX_PACKET_SIZE            (NEMUNETADP_HEADER_SIZE + NEMUNETADP_MAX_DATA_SIZE)
#define NEMUNETADP_MIN_PACKET_SIZE            60
/* link speed 100Mbps (measured in 100 bps) */
#define     NEMUNETADP_LINK_SPEED             1000000
#define NEMUNETADP_MAX_LOOKAHEAD_SIZE         NEMUNETADP_MAX_DATA_SIZE
#define NEMUNETADP_VENDOR_ID 0x080027
#define NEMUNETADP_VENDOR_DRIVER_VERSION      0x00010000
#define NEMUNETADP_VENDOR_DESC                "Sun"
#define NEMUNETADP_MAX_MCAST_LIST             32
#define NEMUNETADP_ETH_ADDRESS_LENGTH         6

//#define NEMUNETADP_REPORT_DISCONNECTED
#endif
/* type defs */

/** Flag specifying that the type of enqueued packet
 * if set the info contains the PINTNETSG packet
 * if clear the packet info contains the PNDIS_PACKET packet
 * Typically the packet queue we are maintaining contains PNDIS_PACKETs only,
 * however in case the underlying miniport indicates a packet with the NDIS_STATUS_RESOURCES status
 * we MUST return the packet back to the miniport immediately
 * this is why we are creating the INTNETSG, copying the ndis packet info there and enqueueing it */
#define NEMUNETFLT_PACKET_SG                  0x00000001

/** the flag specifying that the packet source
 * if set the packet comes from the host (upperlying protocol)
 * if clear the packet comes from the wire (underlying miniport) */
#define NEMUNETFLT_PACKET_SRC_HOST            0x00000002

#ifndef NEMUNETFLT_NO_PACKET_QUEUE
/** flag specifying the packet was originated by our driver
 * i.e. we could use it on our needs and should not return it
 * we are enqueueing "our" packets on ProtocolReceive call-back when
 * Ndis does not give us a receive packet (the driver below us has called NdisM..IndicateReceive)
 * this is supported for Ndis Packet only */
#define NEMUNETFLT_PACKET_MINE                0x00000004

/** flag passed to nemuNetFltWinQuEnqueuePacket specifying that the packet should be copied
 * this is supported for Ndis Packet only */
#define NEMUNETFLT_PACKET_COPY                0x00000008
#endif

/** packet queue element containing the packet info */
typedef struct NEMUNETFLT_PACKET_INFO
{
    /** list entry used for enqueueing the info */
    LIST_ENTRY ListEntry;
    /** pointer to the pool containing this packet info */
    struct NEMUNETFLT_PACKET_INFO_POOL *pPool;
    /** flags describing the referenced packet. Contains PACKET_xxx flags (i.e. PACKET_SG, PACKET_SRC_HOST) */
    uint32_t fFlags;
    /** pointer to the packet this info represents */
    PVOID pPacket;
} NEMUNETFLT_PACKET_INFO, *PNEMUNETFLT_PACKET_INFO;

/* paranoid check to make sure the elements in the packet info array are properly aligned */
AssertCompile((sizeof(NEMUNETFLT_PACKET_INFO) & (sizeof(PVOID) - 1)) == 0);

/** represents the packet queue */
typedef LIST_ENTRY PNEMUNETFLT_ACKET_QUEUE, *PNEMUNETFLT_PACKET_QUEUE;

/*
 * we are using non-interlocked versions of LIST_ENTRY-related operations macros and synchronize
 * access to the queue and its elements by acquiring/releasing a spinlock using Ndis[Acquire,Release]Spinlock
 *
 * we are NOT using interlocked versions of insert/remove head/tail list functions because we need to iterate though
 * the queue elements as well as remove elements from the midle of the queue
 *
 * * @todo: it seems that we can switch to using interlocked versions of list-entry functions
 * since we have removed all functionality (mentioned above, i.e. queue elements iteration, etc.) that might prevent us from doing this
 */
typedef struct NEMUNETFLT_INTERLOCKED_PACKET_QUEUE
{
    /** queue */
    PNEMUNETFLT_ACKET_QUEUE Queue;
    /** queue lock */
    NDIS_SPIN_LOCK Lock;
} NEMUNETFLT_INTERLOCKED_PACKET_QUEUE, *PNEMUNETFLT_INTERLOCKED_PACKET_QUEUE;

typedef struct NEMUNETFLT_SINGLE_LIST
{
    /** queue */
    SINGLE_LIST_ENTRY Head;
    /** pointer to the list tail. used to enqueue elements to the tail of the list */
    PSINGLE_LIST_ENTRY pTail;
} NEMUNETFLT_SINGLE_LIST, *PNEMUNETFLT_SINGLE_LIST;

typedef struct NEMUNETFLT_INTERLOCKED_SINGLE_LIST
{
    /** queue */
    NEMUNETFLT_SINGLE_LIST List;
    /** queue lock */
    NDIS_SPIN_LOCK Lock;
} NEMUNETFLT_INTERLOCKED_SINGLE_LIST, *PNEMUNETFLT_INTERLOCKED_SINGLE_LIST;

/** packet info pool contains free packet info elements to be used for the packet queue
 * we are using the pool mechanism to allocate packet queue elements
 * the pool mechanism is pretty simple now, we are allocating a bunch of memory
 * for maintaining NEMUNETFLT_PACKET_INFO_POOL_SIZE queue elements and just returning null when the pool is exhausted
 * This mechanism seems to be enough for now since we are using NEMUNETFLT_PACKET_INFO_POOL_SIZE = 0xffff which is
 * the maximum size of packets the ndis packet pool supports */
typedef struct NEMUNETFLT_PACKET_INFO_POOL
{
    /** free packet info queue */
    NEMUNETFLT_INTERLOCKED_PACKET_QUEUE Queue;
    /** memory bugger used by the pool */
    PVOID pBuffer;
} NEMUNETFLT_PACKET_INFO_POOL, *PNEMUNETFLT_PACKET_INFO_POOL;

typedef enum NEMUNETDEVOPSTATE
{
    kNemuNetDevOpState_InvalidValue = 0,
    kNemuNetDevOpState_Initializing,
    kNemuNetDevOpState_Initialized,
    kNemuNetDevOpState_Deinitializing,
    kNemuNetDevOpState_Deinitialized,

} NEMUNETDEVOPSTATE;

typedef enum NEMUNETFLT_WINIFSTATE
{
   /** The usual invalid state. */
    kNemuWinIfState_Invalid = 0,
    /** Initialization. */
    kNemuWinIfState_Connecting,
    /** Connected fuly functional state */
    kNemuWinIfState_Connected,
    /** Disconnecting  */
    kNemuWinIfState_Disconnecting,
    /** Disconnected  */
    kNemuWinIfState_Disconnected,
} NEMUNETFLT_WINIFSTATE;

/** structure used to maintain the state and reference count of the miniport and protocol */
typedef struct NEMUNETFLT_WINIF_DEVICE
{
    /** initialize state */
    NEMUNETDEVOPSTATE OpState;
    /** ndis power state */
    NDIS_DEVICE_POWER_STATE PowerState;
    /** reference count */
    uint32_t cReferences;
} NEMUNETFLT_WINIF_DEVICE, *PNEMUNETFLT_WINIF_DEVICE;

#define NEMUNDISREQUEST_INPROGRESS  1
#define NEMUNDISREQUEST_QUEUED      2

typedef struct NEMUNETFLTWIN_STATE
{
    union
    {
        struct
        {
            UINT fRequestInfo : 2;
            UINT fInterfaceClosing : 1;
            UINT fStandBy : 1;
            UINT fProcessingPacketFilter : 1;
            UINT fPPFNetFlt : 1;
            UINT fUpperProtSetFilterInitialized : 1;
            UINT Reserved : 25;
        };
        UINT Value;
    };
} NEMUNETFLTWIN_STATE, *PNEMUNETFLTWIN_STATE;

DECLINLINE(NEMUNETFLTWIN_STATE) nemuNetFltWinAtomicUoReadWinState(NEMUNETFLTWIN_STATE State)
{
    UINT fValue = ASMAtomicUoReadU32((volatile uint32_t *)&State.Value);
    return *((PNEMUNETFLTWIN_STATE)((void*)&fValue));
}

/* miniport layer globals */
typedef struct NEMUNETFLTGLOBALS_MP
{
    /** our miniport handle */
    NDIS_HANDLE hMiniport;
    /** ddis wrapper handle */
    NDIS_HANDLE hNdisWrapper;
} NEMUNETFLTGLOBALS_MP, *PNEMUNETFLTGLOBALS_MP;

#ifndef NEMUNETADP
/* protocol layer globals */
typedef struct NEMUNETFLTGLOBALS_PT
{
    /** our protocol handle */
    NDIS_HANDLE hProtocol;
} NEMUNETFLTGLOBALS_PT, *PNEMUNETFLTGLOBALS_PT;
#endif /* #ifndef NEMUNETADP */

typedef struct NEMUNETFLTGLOBALS_WIN
{
    /** synch event used for device creation synchronization */
    KEVENT SynchEvent;
    /** Device reference count */
    int cDeviceRefs;
    /** ndis device */
    NDIS_HANDLE hDevice;
    /** device object */
    PDEVICE_OBJECT pDevObj;
    /* loopback flags */
    /* ndis packet flags to disable packet loopback */
    UINT fPacketDontLoopBack;
    /* ndis packet flags specifying whether the packet is looped back */
    UINT fPacketIsLoopedBack;
    /* Minport info */
    NEMUNETFLTGLOBALS_MP Mp;
#ifndef NEMUNETADP
    /* Protocol info */
    NEMUNETFLTGLOBALS_PT Pt;
    /** lock protecting the filter list */
    NDIS_SPIN_LOCK lockFilters;
    /** the head of filter list */
    RTLISTANCHOR listFilters;
    /** IP address change notifier handle */
    HANDLE hNotifier;
#endif
} NEMUNETFLTGLOBALS_WIN, *PNEMUNETFLTGLOBALS_WIN;

extern NEMUNETFLTGLOBALS_WIN g_NemuNetFltGlobalsWin;

/** represents filter driver device context*/
typedef struct NEMUNETFLTWIN
{
    /** handle used by miniport edge for ndis calls */
    NDIS_HANDLE hMiniport;
    /** miniport edge state */
    NEMUNETFLT_WINIF_DEVICE MpState;
    /** ndis packet pool used for receives */
    NDIS_HANDLE hRecvPacketPool;
    /** ndis buffer pool used for receives */
    NDIS_HANDLE hRecvBufferPool;
    /** driver bind adapter state. */
    NEMUNETFLT_WINIFSTATE enmState;
#ifndef NEMUNETADP
    /* misc state flags */
    NEMUNETFLTWIN_STATE StateFlags;
    /** handle used by protocol edge for ndis calls */
    NDIS_HANDLE hBinding;
    /** protocol edge state */
    NEMUNETFLT_WINIF_DEVICE PtState;
    /** ndis packet pool used for receives */
    NDIS_HANDLE hSendPacketPool;
    /** ndis buffer pool used for receives */
    NDIS_HANDLE hSendBufferPool;
    /** used for maintaining the pending send packets for handling packet loopback */
    NEMUNETFLT_INTERLOCKED_SINGLE_LIST SendPacketQueue;
    /** used for serializing calls to the NdisRequest in the nemuNetFltWinSynchNdisRequest */
    RTSEMFASTMUTEX hSynchRequestMutex;
    /** event used to synchronize with the Ndis Request completion in the nemuNetFltWinSynchNdisRequest */
    KEVENT hSynchCompletionEvent;
    /** status of the Ndis Request initiated by the nemuNetFltWinSynchNdisRequest */
    NDIS_STATUS volatile SynchCompletionStatus;
    /** pointer to the Ndis Request being executed by the nemuNetFltWinSynchNdisRequest */
    PNDIS_REQUEST volatile pSynchRequest;
    /** open/close adapter status.
     * Since ndis adapter open and close requests may complete asynchronously,
     * we are using event mechanism to wait for open/close completion
     * the status field is being set by the completion call-back */
    NDIS_STATUS OpenCloseStatus;
    /** open/close adaptor completion event */
    NDIS_EVENT OpenCloseEvent;
    /** medium we are attached to */
    NDIS_MEDIUM enmMedium;
    /**
     * Passdown request info
     */
    /** ndis request we pass down to the miniport below */
    NDIS_REQUEST PassDownRequest;
    /** Ndis pass down request bytes read or written original pointer */
    PULONG pcPDRBytesRW;
    /** Ndis pass down request bytes needed original pointer */
    PULONG pcPDRBytesNeeded;
    /** true if we should indicate the receive complete used by the ProtocolReceive mechanism.
     * We need to indicate it only with the ProtocolReceive + NdisMEthIndicateReceive path.
     * Note: we're using KeGetCurrentProcessorNumber, which is not entirely correct in case
     * we're running on 64bit win7+, which can handle > 64 CPUs, however since KeGetCurrentProcessorNumber
     * always returns the number < than the number of CPUs in the first group, we're guaranteed to have CPU index < 64
     * @todo: use KeGetCurrentProcessorNumberEx for Win7+ 64 and dynamically extended array */
    bool abIndicateRxComplete[64];
    /** Pending transfer data packet queue (i.e. packets that were indicated as pending on NdisTransferData call */
    NEMUNETFLT_INTERLOCKED_SINGLE_LIST TransferDataList;
    /* mac options initialized on OID_GEN_MAC_OPTIONS */
    ULONG fMacOptions;
    /** our miniport devuice name */
    NDIS_STRING MpDeviceName;
    /** synchronize with unbind with Miniport initialization */
    NDIS_EVENT MpInitCompleteEvent;
    /** media connect status that we indicated */
    NDIS_STATUS MpIndicatedMediaStatus;
    /** media connect status pending to indicate */
    NDIS_STATUS MpUnindicatedMediaStatus;
    /** packet filter flags set by the upper protocols */
    ULONG fUpperProtocolSetFilter;
    /** packet filter flags set by the upper protocols */
    ULONG fSetFilterBuffer;
    /** packet filter flags set by us */
    ULONG fOurSetFilter;
    /** our own list of filters, needed by notifier */
    RTLISTNODE node;
#else
    volatile ULONG cTxSuccess;
    volatile ULONG cRxSuccess;
    volatile ULONG cTxError;
    volatile ULONG cRxError;
#endif
} NEMUNETFLTWIN, *PNEMUNETFLTWIN;

typedef struct NEMUNETFLT_PACKET_QUEUE_WORKER
{
    /** this event is used to initiate a packet queue worker thread kill */
    KEVENT KillEvent;
    /** this event is used to notify a worker thread that the packets are added to the queue */
    KEVENT NotifyEvent;
    /** pointer to the packet queue worker thread object */
    PKTHREAD pThread;
    /** pointer to the SG used by the packet queue for IntNet receive notifications */
    PINTNETSG pSG;
    /** Packet queue */
    NEMUNETFLT_INTERLOCKED_PACKET_QUEUE PacketQueue;
    /** Packet info pool, i.e. the pool for the packet queue elements */
    NEMUNETFLT_PACKET_INFO_POOL PacketInfoPool;
} NEMUNETFLT_PACKET_QUEUE_WORKER, *PNEMUNETFLT_PACKET_QUEUE_WORKER;

/* protocol reserved data held in ndis packet */
typedef struct NEMUNETFLT_PKTRSVD_PT
{
    /** original packet received from the upperlying protocol
     * can be null if the packet was originated by intnet */
    PNDIS_PACKET pOrigPacket;
    /** pointer to the buffer to be freed on send completion
     * can be null if no buffer is to be freed */
    PVOID pBufToFree;
#if !defined(NEMU_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)
    SINGLE_LIST_ENTRY ListEntry;
    /* true if the packet is from IntNet */
    bool bFromIntNet;
#endif
} NEMUNETFLT_PKTRSVD_PT, *PNEMUNETFLT_PKTRSVD_PT;

/** miniport reserved data held in ndis packet */
typedef struct NEMUNETFLT_PKTRSVD_MP
{
    /** original packet received from the underling miniport
     * can be null if the packet was originated by intnet */
    PNDIS_PACKET pOrigPacket;
    /** pointer to the buffer to be freed on receive completion
     * can be null if no buffer is to be freed */
    PVOID pBufToFree;
} NEMUNETFLT_PKTRSVD_MP, *PNEMUNETFLT_PKTRSVD_MP;

/** represents the data stored in the protocol reserved field of ndis packet on NdisTransferData processing */
typedef struct NEMUNETFLT_PKTRSVD_TRANSFERDATA_PT
{
    /** next packet in a list */
    SINGLE_LIST_ENTRY ListEntry;
    /* packet buffer start */
    PNDIS_BUFFER pOrigBuffer;
} NEMUNETFLT_PKTRSVD_TRANSFERDATA_PT, *PNEMUNETFLT_PKTRSVD_TRANSFERDATA_PT;

/* NEMUNETFLT_PKTRSVD_TRANSFERDATA_PT should fit into PROTOCOL_RESERVED_SIZE_IN_PACKET because we use protocol reserved part
 * of our miniport edge on transfer data processing for honding our own info */
AssertCompile(sizeof (NEMUNETFLT_PKTRSVD_TRANSFERDATA_PT) <= PROTOCOL_RESERVED_SIZE_IN_PACKET);
/* this should fit in MiniportReserved */
AssertCompile(sizeof (NEMUNETFLT_PKTRSVD_MP) <= RT_SIZEOFMEMB(NDIS_PACKET, MiniportReserved));
/* we use RTAsmAtomic*U32 for those, make sure we're correct */
AssertCompile(sizeof (NDIS_DEVICE_POWER_STATE) == sizeof (uint32_t));
AssertCompile(sizeof (UINT) == sizeof (uint32_t));


#define NDIS_FLAGS_SKIP_LOOPBACK_W2K    0x400

#include "../../NemuNetFltInternal.h"
#include "NemuNetFltRt-win.h"
#ifndef NEMUNETADP
# include "NemuNetFltP-win.h"
#endif
#include "NemuNetFltM-win.h"

#endif /* #ifndef ___NemuNetFltCmn_win_h___ */
