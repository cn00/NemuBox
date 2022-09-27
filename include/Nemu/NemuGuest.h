/** @file
 * NemuGuest - VirtualBox Guest Additions Driver Interface. (ADD,DEV)
 *
 * @remarks This is in the process of being split up and usage cleaned up.
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

#ifndef ___Nemu_NemuGuest_h
#define ___Nemu_NemuGuest_h

#include <Nemu/cdefs.h>
#include <Nemu/types.h>
#include <iprt/assert.h>
#include <Nemu/VMMDev2.h>
#include <Nemu/NemuGuest2.h>


/** @defgroup grp_nemuguest  VirtualBox Guest Additions Device Driver
 *
 * Also know as NemuGuest.
 *
 * @{
 */

/** @defgroup grp_nemuguest_ioc  VirtualBox Guest Additions Driver Interface
 * @{
 */

/** @todo It would be nice if we could have two defines without paths. */

/** @def NEMUGUEST_DEVICE_NAME
 * The support device name. */
/** @def NEMUGUEST_USER_DEVICE_NAME
 * The support device name of the user accessible device node. */

#if defined(RT_OS_OS2)
# define NEMUGUEST_DEVICE_NAME          "\\Dev\\NemuGst$"

#elif defined(RT_OS_WINDOWS)
# define NEMUGUEST_DEVICE_NAME          "\\\\.\\NemuGuest"

/** The support service name. */
# define NEMUGUEST_SERVICE_NAME         "NemuGuest"
/** Global name for Win2k+ */
# define NEMUGUEST_DEVICE_NAME_GLOBAL   "\\\\.\\Global\\NemuGuest"
/** Win32 driver name */
# define NEMUGUEST_DEVICE_NAME_NT       L"\\Device\\NemuGuest"
/** Device name. */
# define NEMUGUEST_DEVICE_NAME_DOS      L"\\DosDevices\\NemuGuest"

#elif defined(RT_OS_HAIKU)
# define NEMUGUEST_DEVICE_NAME          "/dev/misc/nemuguest"

#else /* (PORTME) */
# define NEMUGUEST_DEVICE_NAME          "/dev/nemuguest"
# if defined(RT_OS_LINUX)
#  define NEMUGUEST_USER_DEVICE_NAME    "/dev/nemuuser"
# endif
#endif

#ifndef NEMUGUEST_USER_DEVICE_NAME
# define NEMUGUEST_USER_DEVICE_NAME     NEMUGUEST_DEVICE_NAME
#endif

/** Fictive start address of the hypervisor physical memory for MmMapIoSpace. */
#define NEMUGUEST_HYPERVISOR_PHYSICAL_START     UINT32_C(0xf8000000)

#ifdef RT_OS_DARWIN
/** Cookie used to fend off some unwanted clients to the IOService. */
# define NEMUGUEST_DARWIN_IOSERVICE_COOKIE      UINT32_C(0x56426f78) /* 'Nemu' */
#endif

#if !defined(IN_RC) && !defined(IN_RING0_AGNOSTIC) && !defined(IPRT_NO_CRT)

/** @name NemuGuest IOCTL codes and structures.
 *
 * The range 0..15 is for basic driver communication.
 * The range 16..31 is for HGCM communication.
 * The range 32..47 is reserved for future use.
 * The range 48..63 is for OS specific communication.
 * The 7th bit is reserved for future hacks.
 * The 8th bit is reserved for distinguishing between 32-bit and 64-bit
 * processes in future 64-bit guest additions.
 *
 * @remarks When creating new IOCtl interfaces keep in mind that not all OSes supports
 *          reporting back the output size. (This got messed up a little bit in NemuDrv.)
 *
 *          The request size is also a little bit tricky as it's passed as part of the
 *          request code on unix. The size field is 14 bits on Linux, 12 bits on *BSD,
 *          13 bits Darwin, and 8-bits on Solaris. All the BSDs and Darwin kernels
 *          will make use of the size field, while Linux and Solaris will not. We're of
 *          course using the size to validate and/or map/lock the request, so it has
 *          to be valid.
 *
 *          For Solaris we will have to do something special though, 255 isn't
 *          sufficient for all we need. A 4KB restriction (BSD) is probably not
 *          too problematic (yet) as a general one.
 *
 *          More info can be found in SUPDRVIOC.h and related sources.
 *
 * @remarks If adding interfaces that only has input or only has output, some new macros
 *          needs to be created so the most efficient IOCtl data buffering method can be
 *          used.
 * @{
 */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_SPARC64)
# define NEMUGUEST_IOCTL_FLAG     128
#elif defined(RT_ARCH_X86) || defined(RT_ARCH_SPARC)
# define NEMUGUEST_IOCTL_FLAG     0
#else
# error "dunno which arch this is!"
#endif
/** @} */

/** Ring-3 request wrapper for big requests.
 *
 * This is necessary because the ioctl number scheme on many Unixy OSes (esp. Solaris)
 * only allows a relatively small size to be encoded into the request. So, for big
 * request this generic form is used instead. */
typedef struct VBGLBIGREQ
{
    /** Magic value (VBGLBIGREQ_MAGIC). */
    uint32_t    u32Magic;
    /** The size of the data buffer. */
    uint32_t    cbData;
    /** The user address of the data buffer. */
    RTR3PTR     pvDataR3;
#if HC_ARCH_BITS == 32
    uint32_t    u32Padding;
#endif
/** @todo r=bird: We need a 'rc' field for passing Nemu status codes. Reused
 *        some input field as rc on output. */
} VBGLBIGREQ;
/** Pointer to a request wrapper for solaris guests. */
typedef VBGLBIGREQ *PVBGLBIGREQ;
/** Pointer to a const request wrapper for solaris guests. */
typedef const VBGLBIGREQ *PCVBGLBIGREQ;

/** The VBGLBIGREQ::u32Magic value (Ryuu Murakami). */
#define VBGLBIGREQ_MAGIC                            0x19520219


#if defined(RT_OS_WINDOWS)
/** @todo Remove IOCTL_CODE later! Integrate it in NEMUGUEST_IOCTL_CODE below. */
/** @todo r=bird: IOCTL_CODE is supposedly defined in some header included by Windows.h or ntddk.h, which is why it wasn't in the #if 0 earlier. See HostDrivers/Support/SUPDrvIOC.h... */
# define IOCTL_CODE(DeviceType, Function, Method, Access, DataSize_ignored) \
  ( ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
# define NEMUGUEST_IOCTL_CODE_(Function, Size)      IOCTL_CODE(FILE_DEVICE_UNKNOWN, 2048 + (Function), METHOD_BUFFERED, FILE_WRITE_ACCESS, 0)
# define NEMUGUEST_IOCTL_STRIP_SIZE(Code)           (Code)

#elif defined(RT_OS_OS2)
  /* No automatic buffering, size not encoded. */
# define NEMUGUEST_IOCTL_CATEGORY                   0xc2
# define NEMUGUEST_IOCTL_CODE_(Function, Size)      ((unsigned char)(Function))
# define NEMUGUEST_IOCTL_CATEGORY_FAST              0xc3 /**< Also defined in NemuGuestA-os2.asm. */
# define NEMUGUEST_IOCTL_CODE_FAST_(Function)       ((unsigned char)(Function))
# define NEMUGUEST_IOCTL_STRIP_SIZE(Code)           (Code)

#elif defined(RT_OS_SOLARIS)
  /* No automatic buffering, size limited to 255 bytes => use VBGLBIGREQ for everything. */
# include <sys/ioccom.h>
# define NEMUGUEST_IOCTL_CODE_(Function, Size)      _IOWRN('V', (Function), sizeof(VBGLBIGREQ))
# define NEMUGUEST_IOCTL_CODE_FAST_(Function)       _IO(  'V', (Function))
# define NEMUGUEST_IOCTL_STRIP_SIZE(Code)           (Code)

#elif defined(RT_OS_LINUX)
  /* No automatic buffering, size limited to 16KB. */
# include <linux/ioctl.h>
# define NEMUGUEST_IOCTL_CODE_(Function, Size)      _IOC(_IOC_READ|_IOC_WRITE, 'V', (Function), (Size))
# define NEMUGUEST_IOCTL_CODE_FAST_(Function)       _IO(  'V', (Function))
# define NEMUGUEST_IOCTL_STRIP_SIZE(Code)           NEMUGUEST_IOCTL_CODE_(_IOC_NR((Code)), 0)

#elif defined(RT_OS_HAIKU)
  /* No automatic buffering, size not encoded. */
  /** @todo do something better */
# define NEMUGUEST_IOCTL_CODE_(Function, Size)      (0x56420000 | (Function))
# define NEMUGUEST_IOCTL_CODE_FAST_(Function)       (0x56420000 | (Function))
# define NEMUGUEST_IOCTL_STRIP_SIZE(Code)           (Code)

#elif defined(RT_OS_FREEBSD) /** @todo r=bird: Please do it like SUPDRVIOC to keep it as similar as possible. */
# include <sys/ioccom.h>

# define NEMUGUEST_IOCTL_CODE_(Function, Size)      _IOWR('V', (Function), VBGLBIGREQ)
# define NEMUGUEST_IOCTL_CODE_FAST_(Function)       _IO(  'V', (Function))
# define NEMUGUEST_IOCTL_STRIP_SIZE(Code)           IOCBASECMD(Code)

#else /* BSD Like */
  /* Automatic buffering, size limited to 4KB on *BSD and 8KB on Darwin - commands the limit, 4KB. */
# include <sys/ioccom.h>
# define NEMUGUEST_IOCTL_CODE_(Function, Size)      _IOC(IOC_INOUT, 'V', (Function), (Size))
# define NEMUGUEST_IOCTL_CODE_FAST_(Function)       _IO('V', (Function))
# define NEMUGUEST_IOCTL_STRIP_SIZE(uIOCtl)         ( (uIOCtl) & ~_IOC(0,0,0,IOCPARM_MASK) )
#endif

#define NEMUGUEST_IOCTL_CODE(Function, Size)        NEMUGUEST_IOCTL_CODE_((Function) | NEMUGUEST_IOCTL_FLAG, Size)
#define NEMUGUEST_IOCTL_CODE_FAST(Function)         NEMUGUEST_IOCTL_CODE_FAST_((Function) | NEMUGUEST_IOCTL_FLAG)

/* Define 32 bit codes to support 32 bit applications requests in the 64 bit guest driver. */
#ifdef RT_ARCH_AMD64
# define NEMUGUEST_IOCTL_CODE_32(Function, Size)    NEMUGUEST_IOCTL_CODE_(Function, Size)
# define NEMUGUEST_IOCTL_CODE_FAST_32(Function)     NEMUGUEST_IOCTL_CODE_FAST_(Function)
#endif /* RT_ARCH_AMD64 */



/** IOCTL to NemuGuest to query the VMMDev IO port region start.
 * @remarks Ring-0 only. */
#define NEMUGUEST_IOCTL_GETVMMDEVPORT               NEMUGUEST_IOCTL_CODE(1, sizeof(NemuGuestPortInfo))

#pragma pack(4)
typedef struct NemuGuestPortInfo
{
    uint32_t portAddress;
    struct VMMDevMemory *pVMMDevMemory;
} NemuGuestPortInfo;


/** IOCTL to NemuGuest to wait for a VMMDev host notification */
#define NEMUGUEST_IOCTL_WAITEVENT                   NEMUGUEST_IOCTL_CODE_(2, sizeof(NemuGuestWaitEventInfo))

/** @name Result codes for NemuGuestWaitEventInfo::u32Result
 * @{
 */
/** Successful completion, an event occurred. */
#define NEMUGUEST_WAITEVENT_OK          (0)
/** Successful completion, timed out. */
#define NEMUGUEST_WAITEVENT_TIMEOUT     (1)
/** Wait was interrupted. */
#define NEMUGUEST_WAITEVENT_INTERRUPTED (2)
/** An error occurred while processing the request. */
#define NEMUGUEST_WAITEVENT_ERROR       (3)
/** @} */

/** Input and output buffers layout of the IOCTL_NEMUGUEST_WAITEVENT */
typedef struct NemuGuestWaitEventInfo
{
    /** timeout in milliseconds */
    uint32_t u32TimeoutIn;
    /** events to wait for */
    uint32_t u32EventMaskIn;
    /** result code */
    uint32_t u32Result;
    /** events occurred */
    uint32_t u32EventFlagsOut;
} NemuGuestWaitEventInfo;
AssertCompileSize(NemuGuestWaitEventInfo, 16);


/** IOCTL to NemuGuest to perform a VMM request
 * @remark  The data buffer for this IOCtl has an variable size, keep this in mind
 *          on systems where this matters. */
#define NEMUGUEST_IOCTL_VMMREQUEST(Size)            NEMUGUEST_IOCTL_CODE_(3, (Size))


/** IOCTL to NemuGuest to control event filter mask. */
#define NEMUGUEST_IOCTL_CTL_FILTER_MASK             NEMUGUEST_IOCTL_CODE_(4, sizeof(NemuGuestFilterMaskInfo))

/** Input and output buffer layout of the IOCTL_NEMUGUEST_CTL_FILTER_MASK. */
typedef struct NemuGuestFilterMaskInfo
{
    uint32_t u32OrMask;
    uint32_t u32NotMask;
} NemuGuestFilterMaskInfo;
AssertCompileSize(NemuGuestFilterMaskInfo, 8);
#pragma pack()

/** IOCTL to NemuGuest to interrupt (cancel) any pending WAITEVENTs and return.
 * Handled inside the guest additions and not seen by the host at all.
 * @see NEMUGUEST_IOCTL_WAITEVENT */
#define NEMUGUEST_IOCTL_CANCEL_ALL_WAITEVENTS       NEMUGUEST_IOCTL_CODE_(5, 0)

/** IOCTL to NemuGuest to perform backdoor logging.
 * The argument is a string buffer of the specified size. */
#define NEMUGUEST_IOCTL_LOG(Size)                   NEMUGUEST_IOCTL_CODE_(6, (Size))

/** IOCTL to NemuGuest to check memory ballooning.
 * The guest kernel module / device driver will ask the host for the current size of
 * the balloon and adjust the size. Or it will set fHandledInR0 = false and R3 is
 * responsible for allocating memory and calling R0 (NEMUGUEST_IOCTL_CHANGE_BALLOON). */
#define NEMUGUEST_IOCTL_CHECK_BALLOON               NEMUGUEST_IOCTL_CODE_(7, sizeof(NemuGuestCheckBalloonInfo))

/** Output buffer layout of the NEMUGUEST_IOCTL_CHECK_BALLOON. */
typedef struct NemuGuestCheckBalloonInfo
{
    /** The size of the balloon in chunks of 1MB. */
    uint32_t cBalloonChunks;
    /** false = handled in R0, no further action required.
     *   true = allocate balloon memory in R3. */
    uint32_t fHandleInR3;
} NemuGuestCheckBalloonInfo;
AssertCompileSize(NemuGuestCheckBalloonInfo, 8);


/** IOCTL to NemuGuest to supply or revoke one chunk for ballooning.
 * The guest kernel module / device driver will lock down supplied memory or
 * unlock reclaimed memory and then forward the physical addresses of the
 * changed balloon chunk to the host. */
#define NEMUGUEST_IOCTL_CHANGE_BALLOON              NEMUGUEST_IOCTL_CODE_(8, sizeof(NemuGuestChangeBalloonInfo))

/** Input buffer layout of the NEMUGUEST_IOCTL_CHANGE_BALLOON request.
 * Information about a memory chunk used to inflate or deflate the balloon. */
typedef struct NemuGuestChangeBalloonInfo
{
    /** Address of the chunk. */
    uint64_t u64ChunkAddr;
    /** true = inflate, false = deflate. */
    uint32_t fInflate;
    /** Alignment padding. */
    uint32_t u32Align;
} NemuGuestChangeBalloonInfo;
AssertCompileSize(NemuGuestChangeBalloonInfo, 16);

/** IOCTL to NemuGuest to write guest core. */
#define NEMUGUEST_IOCTL_WRITE_CORE_DUMP             NEMUGUEST_IOCTL_CODE(9, sizeof(NemuGuestWriteCoreDump))

/** Input and output buffer layout of the NEMUGUEST_IOCTL_WRITE_CORE
 *  request. */
typedef struct NemuGuestWriteCoreDump
{
    /** Flags (reserved, MBZ). */
    uint32_t fFlags;
} NemuGuestWriteCoreDump;
AssertCompileSize(NemuGuestWriteCoreDump, 4);

/** IOCTL to NemuGuest to update the mouse status features. */
# define NEMUGUEST_IOCTL_SET_MOUSE_STATUS           NEMUGUEST_IOCTL_CODE_(10, sizeof(uint32_t))

#ifdef NEMU_WITH_HGCM
/** IOCTL to NemuGuest to connect to a HGCM service. */
# define NEMUGUEST_IOCTL_HGCM_CONNECT               NEMUGUEST_IOCTL_CODE(16, sizeof(NemuGuestHGCMConnectInfo))

/** IOCTL to NemuGuest to disconnect from a HGCM service. */
# define NEMUGUEST_IOCTL_HGCM_DISCONNECT            NEMUGUEST_IOCTL_CODE(17, sizeof(NemuGuestHGCMDisconnectInfo))

/** IOCTL to NemuGuest to make a call to a HGCM service.
 * @see NemuGuestHGCMCallInfo */
# define NEMUGUEST_IOCTL_HGCM_CALL(Size)            NEMUGUEST_IOCTL_CODE(18, (Size))

/** IOCTL to NemuGuest to make a timed call to a HGCM service. */
# define NEMUGUEST_IOCTL_HGCM_CALL_TIMED(Size)      NEMUGUEST_IOCTL_CODE(20, (Size))

/** IOCTL to NemuGuest passed from the Kernel Mode driver, but containing a user mode data in NemuGuestHGCMCallInfo
 * the driver received from the UM. Called in the context of the process passing the data.
 * @see NemuGuestHGCMCallInfo */
# define NEMUGUEST_IOCTL_HGCM_CALL_USERDATA(Size)   NEMUGUEST_IOCTL_CODE(21, (Size))

# ifdef RT_ARCH_AMD64
/** @name IOCTL numbers that 32-bit clients, like the Windows OpenGL guest
 *        driver, will use when taking to a 64-bit driver.
 * @remarks These are only used by the driver implementation!
 * @{*/
#  define NEMUGUEST_IOCTL_HGCM_CONNECT_32           NEMUGUEST_IOCTL_CODE_32(16, sizeof(NemuGuestHGCMConnectInfo))
#  define NEMUGUEST_IOCTL_HGCM_DISCONNECT_32        NEMUGUEST_IOCTL_CODE_32(17, sizeof(NemuGuestHGCMDisconnectInfo))
#  define NEMUGUEST_IOCTL_HGCM_CALL_32(Size)        NEMUGUEST_IOCTL_CODE_32(18, (Size))
#  define NEMUGUEST_IOCTL_HGCM_CALL_TIMED_32(Size)  NEMUGUEST_IOCTL_CODE_32(20, (Size))
/** @} */
# endif /* RT_ARCH_AMD64 */

/** Get the pointer to the first HGCM parameter.  */
# define NEMUGUEST_HGCM_CALL_PARMS(a)             ( (HGCMFunctionParameter   *)((uint8_t *)(a) + sizeof(NemuGuestHGCMCallInfo)) )
/** Get the pointer to the first HGCM parameter in a 32-bit request.  */
# define NEMUGUEST_HGCM_CALL_PARMS32(a)           ( (HGCMFunctionParameter32 *)((uint8_t *)(a) + sizeof(NemuGuestHGCMCallInfo)) )

#endif /* NEMU_WITH_HGCM */

#ifdef NEMU_WITH_DPC_LATENCY_CHECKER
/** IOCTL to NemuGuest to perform DPC latency tests, printing the result in
 * the release log on the host.  Takes no data, returns no data. */
# define NEMUGUEST_IOCTL_DPC_LATENCY_CHECKER        NEMUGUEST_IOCTL_CODE_(30, 0)
#endif

/** IOCTL to for setting the mouse driver callback. (kernel only) */
/** @note The callback will be called in interrupt context with the NemuGuest
 * device event spinlock held. */
#define NEMUGUEST_IOCTL_SET_MOUSE_NOTIFY_CALLBACK   NEMUGUEST_IOCTL_CODE(31, sizeof(NemuGuestMouseSetNotifyCallback))

typedef DECLCALLBACK(void) FNNEMUGUESTMOUSENOTIFY(void *pfnUser);
typedef FNNEMUGUESTMOUSENOTIFY *PFNNEMUGUESTMOUSENOTIFY;

/** Input buffer for NEMUGUEST_IOCTL_INTERNAL_SET_MOUSE_NOTIFY_CALLBACK. */
typedef struct NemuGuestMouseSetNotifyCallback
{
    /**
     * Mouse notification callback.
     *
     * @param   pvUser      The callback argument.
     */
    PFNNEMUGUESTMOUSENOTIFY      pfnNotify;
    /** The callback argument*/
    void                       *pvUser;
} NemuGuestMouseSetNotifyCallback;


typedef enum NEMUGUESTCAPSACQUIRE_FLAGS
{
    NEMUGUESTCAPSACQUIRE_FLAGS_NONE = 0,
    /* configures NemuGuest to use the specified caps in Acquire mode, w/o making any caps acquisition/release.
     * so far it is only possible to set acquire mode for caps, but not clear it,
     * so u32NotMask is ignored for this request */
    NEMUGUESTCAPSACQUIRE_FLAGS_CONFIG_ACQUIRE_MODE,
    /* to ensure enum is 32bit*/
    NEMUGUESTCAPSACQUIRE_FLAGS_32bit = 0x7fffffff
} NEMUGUESTCAPSACQUIRE_FLAGS;

typedef struct NemuGuestCapsAquire
{
    /* result status
     * VINF_SUCCESS - on success
     * VERR_RESOURCE_BUSY    - some caps in the u32OrMask are acquired by some other NemuGuest connection.
     *                         NOTE: no u32NotMask caps are cleaned in this case, i.e. no modifications are done on failure
     * VER_INVALID_PARAMETER - invalid Caps are specified with either u32OrMask or u32NotMask. No modifications are done on failure.
     */
    int32_t rc;
    /* Acquire command */
    NEMUGUESTCAPSACQUIRE_FLAGS enmFlags;
    /* caps to acquire, OR-ed VMMDEV_GUEST_SUPPORTS_XXX flags */
    uint32_t u32OrMask;
    /* caps to release, OR-ed VMMDEV_GUEST_SUPPORTS_XXX flags */
    uint32_t u32NotMask;
} NemuGuestCapsAquire;

/** IOCTL to for Acquiring/Releasing Guest Caps
 * This is used for multiple purposes:
 * 1. By doing Acquire r3 client application (e.g. NemuTray) claims it will use
 *    the given connection for performing operations like Seamles or Auto-resize,
 *    thus, if the application terminates, the driver will automatically cleanup the caps reported to host,
 *    so that host knows guest does not support them anymore
 * 2. In a multy-user environment this will not allow r3 applications (like NemuTray)
 *    running in different user sessions simultaneously to interfere with each other.
 *    An r3 client application (like NemuTray) is responsible for Acquiring/Releasing caps properly as needed.
 **/
#define NEMUGUEST_IOCTL_GUEST_CAPS_ACQUIRE          NEMUGUEST_IOCTL_CODE(32, sizeof(NemuGuestCapsAquire))

/** IOCTL to NemuGuest to set guest capabilities. */
#define NEMUGUEST_IOCTL_SET_GUEST_CAPABILITIES      NEMUGUEST_IOCTL_CODE_(33, sizeof(NemuGuestSetCapabilitiesInfo))

/** Input and output buffer layout of the NEMUGUEST_IOCTL_SET_GUEST_CAPABILITIES
 *  IOCtl. */
typedef struct NemuGuestSetCapabilitiesInfo
{
    uint32_t u32OrMask;
    uint32_t u32NotMask;
} NemuGuestSetCapabilitiesInfo;
AssertCompileSize(NemuGuestSetCapabilitiesInfo, 8);


#ifdef RT_OS_OS2

/**
 * The data buffer layout for the IDC entry point (AttachDD).
 *
 * @remark  This is defined in multiple 16-bit headers / sources.
 *          Some places it's called VBGOS2IDC to short things a bit.
 */
typedef struct NEMUGUESTOS2IDCCONNECT
{
    /** VMMDEV_VERSION. */
    uint32_t u32Version;
    /** Opaque session handle. */
    uint32_t u32Session;

    /**
     * The 32-bit service entry point.
     *
     * @returns Nemu status code.
     * @param   u32Session          The above session handle.
     * @param   iFunction           The requested function.
     * @param   pvData              The input/output data buffer. The caller ensures that this
     *                              cannot be swapped out, or that it's acceptable to take a
     *                              page in fault in the current context. If the request doesn't
     *                              take input or produces output, apssing NULL is okay.
     * @param   cbData              The size of the data buffer.
     * @param   pcbDataReturned     Where to store the amount of data that's returned.
     *                              This can be NULL if pvData is NULL.
     */
    DECLCALLBACKMEMBER(int, pfnServiceEP)(uint32_t u32Session, unsigned iFunction, void *pvData, size_t cbData, size_t *pcbDataReturned);

    /** The 16-bit service entry point for C code (cdecl).
     *
     * It's the same as the 32-bit entry point, but the types has
     * changed to 16-bit equivalents.
     *
     * @code
     * int far cdecl
     * NemuGuestOs2IDCService16(uint32_t u32Session, uint16_t iFunction,
     *                          void far *fpvData, uint16_t cbData, uint16_t far *pcbDataReturned);
     * @endcode
     */
    RTFAR16 fpfnServiceEP;

    /** The 16-bit service entry point for Assembly code (register).
     *
     * This is just a wrapper around fpfnServiceEP to simplify calls
     * from 16-bit assembly code.
     *
     * @returns (e)ax: Nemu status code; cx: The amount of data returned.
     *
     * @param   u32Session          eax   - The above session handle.
     * @param   iFunction           dl    - The requested function.
     * @param   pvData              es:bx - The input/output data buffer.
     * @param   cbData              cx    - The size of the data buffer.
     */
    RTFAR16 fpfnServiceAsmEP;
} NEMUGUESTOS2IDCCONNECT;
/** Pointer to NEMUGUESTOS2IDCCONNECT buffer. */
typedef NEMUGUESTOS2IDCCONNECT *PNEMUGUESTOS2IDCCONNECT;

/** OS/2 specific: IDC client disconnect request.
 *
 * This takes no input and it doesn't return anything. Obviously this
 * is only recognized if it arrives thru the IDC service EP.
 */
# define NEMUGUEST_IOCTL_OS2_IDC_DISCONNECT     NEMUGUEST_IOCTL_CODE(48, sizeof(uint32_t))

#endif /* RT_OS_OS2 */

#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)

/* Private IOCtls between user space and the kernel video driver.  DRM private
 * IOCtls always have the type 'd' and a number between 0x40 and 0x99 (0x9F?) */

# define NEMU_DRM_IOCTL(a) (0x40 + DRM_NEMU_ ## a)

/** Stop using HGSMI in the kernel driver until it is re-enabled, so that a
 *  user-space driver can use it.  It must be re-enabled before the kernel
 *  driver can be used again in a sensible way. */
/** @note These IOCtls was removed from the code, but are left here as
 * templates as we may need similar ones in future. */
# define DRM_NEMU_DISABLE_HGSMI    0
# define DRM_IOCTL_NEMU_DISABLE_HGSMI    NEMU_DRM_IOCTL(DISABLE_HGSMI)
# define NEMUVIDEO_IOCTL_DISABLE_HGSMI   _IO('d', DRM_IOCTL_NEMU_DISABLE_HGSMI)
/** Enable HGSMI in the kernel driver after it was previously disabled. */
# define DRM_NEMU_ENABLE_HGSMI     1
# define DRM_IOCTL_NEMU_ENABLE_HGSMI     NEMU_DRM_IOCTL(ENABLE_HGSMI)
# define NEMUVIDEO_IOCTL_ENABLE_HGSMI    _IO('d', DRM_IOCTL_NEMU_ENABLE_HGSMI)

#endif /* RT_OS_LINUX || RT_OS_SOLARIS || RT_OS_FREEBSD */

#endif /* !defined(IN_RC) && !defined(IN_RING0_AGNOSTIC) && !defined(IPRT_NO_CRT) */

/** @} */

/** @} */
#endif

