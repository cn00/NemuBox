/* $Id: NemuDTraceTypes.h $ */
/** @file
 * NemuDTraceTypes.h - Fake a bunch of Solaris types.
 *
 * Contributed by: bird
 */

/*
 * Copyright (C) 2012-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the Common
 * Development and Distribution License Version 1.0 (CDDL) only, as it
 * comes in the "COPYING.CDDL" file of the VirtualBox OSE distribution.
 * VirtualBox OSE is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY of any kind.
 *
 */

#ifndef ___NemuDTraceTypes_h___
#define ___NemuDTraceTypes_h___

#include <iprt/types.h>
#include <iprt/stdarg.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/errno.h>
#ifdef IN_RING0
# include <iprt/list.h>
#endif
#ifdef IN_RING3
# include <sys/types.h>
# include <limits.h>
# ifdef RT_OS_LINUX
#  include <sys/ucontext.h> /* avoid greg_t trouble */
# endif
# if defined(_MSC_VER)
#  include <stdio.h>
# endif
#endif

RT_C_DECLS_BEGIN

struct modctl;

typedef unsigned char               uchar_t;
typedef unsigned short              ushort_t;
typedef unsigned int                uint_t;
typedef uintptr_t                   ulong_t;
#ifndef RT_OS_SOLARIS
typedef int64_t                     longlong_t;
typedef uint64_t                    u_longlong_t;
typedef uint64_t                    hrtime_t;
typedef uint32_t                    id_t;
typedef uint32_t                    zoneid_t;
#endif
#if !defined(NGREG) || !defined(RT_OS_LINUX)
typedef RTCCINTREG                  greg_t;
#else
AssertCompileSize(greg_t, sizeof(RTCCINTREG));
#endif
typedef uintptr_t                   pc_t;
typedef unsigned int                model_t;
typedef RTCPUID                     processorid_t;
#if defined(_MSC_VER) || defined(IN_RING0)
typedef RTUID                       uid_t;
typedef RTPROCESS                   pid_t;
#endif
#if defined(_MSC_VER) || defined(IN_RING0) || defined(RT_OS_LINUX)
typedef char                       *caddr_t;
#endif

#if !defined(NANOSEC) || !defined(RT_OS_SOLARIS)
# define NANOSEC                    RT_NS_1SEC
#endif
#if !defined(MICROSEC) || !defined(RT_OS_SOLARIS)
# define MICROSEC                   RT_US_1SEC
#endif
#if !defined(MILLISEC) || !defined(RT_OS_SOLARIS)
# define MILLISEC                   RT_MS_1SEC
#endif
#if !defined(SEC) || !defined(RT_OS_SOLARIS)
# define SEC                        (1)
#endif
#define MAXPATHLEN                  RTPATH_MAX
#undef PATH_MAX
#define PATH_MAX                    RTPATH_MAX
#undef NBBY
#define NBBY                        (8)
#define NCPU                        RTCPUSET_MAX_CPUS
#define B_FALSE                     (0)
#define B_TRUE                      (1)
#define MIN(a1, a2)                 RT_MIN(a1, a2)
#define MAX(a1, a2)                 RT_MAX(a1, a2)
#define ABS(a_iValue)               RT_ABS(a_iValue)
#define IS_P2ALIGNED(uWhat, uAlign) ( !((uWhat) & ((uAlign) - 1)) )
#define P2ROUNDUP(uWhat, uAlign)    ( ((uWhat) + (uAlign) - 1) & ~(uAlign - 1) )
#define roundup(uWhat, uUnit)       ( ( (uWhat) + ((uUnit) - 1)) / (uUnit) * (uUnit) )

#if defined(RT_ARCH_X86)
# ifndef __i386
#  define __i386                    1
# endif
# ifndef __x86
#  define __x86                     1
# endif
# ifndef _IPL32
#  define _IPL32                    1
# endif
# if !defined(_LITTLE_ENDIAN) || !defined(RT_OS_SOLARIS)
#  define _LITTLE_ENDIAN            1
# endif

#elif defined(RT_ARCH_AMD64)
# ifndef __x86_64
#  define __x86_64                  1
# endif
# ifndef __x86
#  define __x86                     1
# endif
# ifndef _LP64
#  define _LP64                     1
# endif
# if !defined(_LITTLE_ENDIAN) || !defined(RT_OS_SOLARIS)
#  define _LITTLE_ENDIAN            1
# endif

#else
# error "unsupported arch!"
#endif

/** Mark a cast added when porting the code to Nemu.
 * Avoids lots of \#ifdef NEMU otherwise needed to mark up the changes. */
#define VBDTCAST(a_Type)        (a_Type)
/** Mark a type change made when porting the code to Nemu.
 * This is usually signed -> unsigned type changes that avoids a whole lot of
 * comparsion warnings. */
#define VBDTTYPE(a_Nemu, a_Org) a_Nemu
/** Mark missing void in a parameter list. */
#define VBDTVOID                void
/** Mark missing static in a function definition. */
#define VBDTSTATIC              static
#define VBDTUNASS(a_Value)      = a_Value
#define VBDTGCC(a_Value)        = a_Value

/*
 * string
 */
#ifdef IN_RING0
# undef bcopy
# define bcopy(a_pSrc, a_pDst, a_cb) ((void)memmove(a_pDst, a_pSrc, a_cb))
# undef bzero
# define bzero(a_pDst, a_cb)        ((void)memset(a_pDst, 0, a_cb))
# undef bcmp
# define bcmp(a_p1, a_p2, a_cb)     (memcmp(a_p1, a_p2, a_cb))
#endif
#if defined(_MSC_VER) || defined(IN_RING0)
# define snprintf                   RTStrPrintf
# define vsnprintf                  RTStrPrintfV
#endif

/*
 * Bitmap stuff.
 */
#define BT_SIZEOFMAP(a_cBits)       ( (a_cBits + 63) / 8 )
#define BT_SET(a_aulBitmap, iBit)   ASMBitSet(a_aulBitmap, iBit)
#define BT_CLEAR(a_aulBitmap, iBit) ASMBitClear(a_aulBitmap, iBit)
#define BT_TEST(a_aulBitmap, iBit)  ASMBitTest(a_aulBitmap, iBit)
#if ARCH_BITS == 32
# define BT_NBIPUL                  32
# define BT_ULSHIFT                 5 /* log2(32) = 5 */
# define BT_ULMASK                  0x1f
# define BT_BITOUL(a_cBits)         ( ((a_cBits) + 31) / 32 )
#elif ARCH_BITS == 64
# define BT_NBIPUL                  64
# define BT_ULSHIFT                 6 /* log2(32) = 6 */
# define BT_ULMASK                  0x3f
# define BT_BITOUL(a_cBits)         ( ((a_cBits) + 63) / 64 )
#else
# error Bad ARCH_BITS...
#endif


#ifdef IN_RING0

/*
 * Kernel stuff...
 */
#define CPU_ON_INTR(a_pCpu)         (false)

#define KERNELBASE                  NemuDtGetKernelBase()
uintptr_t NemuDtGetKernelBase(void);


typedef struct NemuDtCred
{
    int32_t                 cr_refs;
    RTUID                   cr_uid;
    RTUID                   cr_ruid;
    RTUID                   cr_suid;
    RTGID                   cr_gid;
    RTGID                   cr_rgid;
    RTGID                   cr_sgid;
    zoneid_t                cr_zone;
} cred_t;
#define PRIV_POLICY_ONLY(a_pCred, a_uPriv, a_fAll)  (true)
#define priv_isequalset(a, b)                       (true)
#define crgetuid(a_pCred)                           ((a_pCred)->cr_uid)
#define crgetzoneid(a_pCred)                        ((a_pCred)->cr_zone)
#define crhold                                      NemuDtCredHold
#define crfree                                      NemuDtCredFree
void NemuDtCredHold(struct NemuDtCred *pCred);
void NemuDtCredFree(struct NemuDtCred *pCred);


typedef struct RTTIMER  *cyclic_id_t;
#define CYCLIC_NONE                                 ((struct RTTIMER *)NULL)
#define cyclic_remove(a_hTimer)                     RTTimerDestroy(a_hTimer)

typedef struct NemuDtThread
{
    /** The next thread with the same hash table entry.
     * Or the next free thread.  */
    struct NemuDtThread    *pNext;
    /** Age list node. */
    RTLISTNODE              AgeEntry;
    /** The native thread handle. */
    RTNATIVETHREAD          hNative;
    /** The process ID. */
    RTPROCESS               uPid;

    uint32_t                t_predcache;
    uintptr_t               t_dtrace_scrpc;
    uintptr_t               t_dtrace_astpc;
    hrtime_t                t_dtrace_vtime;
    hrtime_t                t_dtrace_start;
    uint8_t                 t_dtrace_stop;
} kthread_t;
struct NemuDtThread *NemuDtGetCurrentThread(void);
#define curthread               (NemuDtGetCurrentThread())


typedef struct NemuDtProcess    proc_t;
# if 0 /* not needed ? */
struct NemuDtProcess    proc_t;
{
/*    uint32_t                p_flag; - don't bother with this */
    RTPROCESS               p_pid;
    struct dtrace_helpers  *p_dtrace_helpers;
}
proc_t *NemuDtGetCurrentProc(void);
# define curproc                 (NemuDtGetCurrentProc())
/*# define SNOCD                  RT_BIT(0) - don't bother with this */
# endif

typedef struct NemuDtTaskQueue  taskq_t;

typedef struct NemuDtMutex
{
    RTSEMMUTEX              hMtx;
    RTNATIVETHREAD volatile hOwner;
} kmutex_t;
#define mutex_enter             NemuDtMutexEnter
#define mutex_exit              NemuDtMutexExit
#define MUTEX_HELD(a_pMtx)      NemuDtMutexIsOwner(a_pMtx)
#define MUTEX_NOT_HELD(a_pMtx)  (!NemuDtMutexIsOwner(a_pMtx))
#define mod_lock                g_DummyMtx
#define cpu_lock                g_DummyMtx
int  NemuDtMutexInit(struct NemuDtMutex *pMtx);
void NemuDtMutexDelete(struct NemuDtMutex *pMtx);
void NemuDtMutexEnter(struct NemuDtMutex *pMtx);
void NemuDtMutexExit(struct NemuDtMutex *pMtx);
bool NemuDtMutexIsOwner(struct NemuDtMutex *pMtx);
extern struct NemuDtMutex       g_DummyMtx;


typedef struct NemuDtCpuCore
{
    RTCPUID             cpu_id;
    uintptr_t           cpuc_dtrace_illval;
    uint16_t volatile   cpuc_dtrace_flags;

} cpucore_t;

#define CPU_DTRACE_BADADDR      RT_BIT(0)
#define CPU_DTRACE_BADALIGN     RT_BIT(1)
#define CPU_DTRACE_BADSTACK     RT_BIT(2)
#define CPU_DTRACE_KPRIV        RT_BIT(3)
#define CPU_DTRACE_DIVZERO      RT_BIT(4)
#define CPU_DTRACE_ILLOP        RT_BIT(5)
#define CPU_DTRACE_NOSCRATCH    RT_BIT(6)
#define CPU_DTRACE_UPRIV        RT_BIT(7)
#define CPU_DTRACE_TUPOFLOW     RT_BIT(8)
#define CPU_DTRACE_ENTRY        RT_BIT(9)
#define CPU_DTRACE_FAULT        UINT16_C(0x03ff)
#define CPU_DTRACE_DROP         RT_BIT(12)
#define CPU_DTRACE_ERROR        UINT16_C(0x13ff)
#define CPU_DTRACE_NOFAULT      RT_BIT(15)

extern cpucore_t                g_aNemuDtCpuCores[RTCPUSET_MAX_CPUS];
#define cpu_core                (g_aNemuDtCpuCores)

struct NemuDtCred *NemuDtGetCurrentCreds(void);
#define CRED()                  NemuDtGetCurrentCreds()

proc_t *NemuDtThreadToProc(kthread_t *);


#define ASSERT(a_Expr)          Assert(a_Expr)
#define panic                   NemuDtPanic
void NemuDtPanic(const char *pszFormat, ...);
#define cmn_err                 NemuDtCmnErr
void NemuDtCmnErr(int iLevel, const char *pszFormat, ...);
#define CE_WARN                 10
#define CE_NOTE                 11
#define uprintf                 NemuDtUPrintf
#define vuprintf                NemuDtUPrintfV
void NemuDtUPrintf(const char *pszFormat, ...);
void NemuDtUPrintfV(const char *pszFormat, va_list va);

/*
 * Memory allocation wrappers.
 */
#define KM_SLEEP                RT_BIT(0)
#define KM_NOSLEEP              RT_BIT(1)
#define kmem_alloc              NemuDtKMemAlloc
#define kmem_zalloc             NemuDtKMemAllocZ
#define kmem_free               NemuDtKMemFree
void *NemuDtKMemAlloc(size_t cbMem, uint32_t fFlags);
void *NemuDtKMemAllocZ(size_t cbMem, uint32_t fFlags);
void  NemuDtKMemFree(void *pvMem, size_t cbMem);


typedef struct NemuDtMemCache   kmem_cache_t;
#define kmem_cache_create       NemuDtKMemCacheCreate
#define kmem_cache_destroy      NemuDtKMemCacheDestroy
#define kmem_cache_alloc        NemuDtKMemCacheAlloc
#define kmem_cache_free         NemuDtKMemCacheFree
struct NemuDtMemCache *NemuDtKMemCacheCreate(const char *pszName, size_t cbBuf, size_t cbAlign,
                                             PFNRT pfnCtor, PFNRT pfnDtor, PFNRT pfnReclaim,
                                             void *pvUser, void *pvVM, uint32_t fFlags);
void  NemuDtKMemCacheDestroy(struct NemuDtMemCache *pCache);
void *NemuDtKMemCacheAlloc(struct NemuDtMemCache *pCache, uint32_t fFlags);
void  NemuDtKMemCacheFree(struct NemuDtMemCache *pCache, void *pvMem);


typedef struct NemuDtVMem       vmem_t;
#define VM_SLEEP                RT_BIT(0)
#define VM_BESTFIT              RT_BIT(1)
#define VMC_IDENTIFIER          RT_BIT(16)
#define vmem_create             NemuDtVMemCreate
#define vmem_destroy            NemuDtVMemDestroy
#define vmem_alloc              NemuDtVMemAlloc
#define vmem_free               NemuDtVMemFree
struct NemuDtVMem *NemuDtVMemCreate(const char *pszName, void *pvBase, size_t cb, size_t cbUnit,
                                    PFNRT pfnAlloc, PFNRT pfnFree, struct NemuDtVMem *pSrc,
                                    size_t cbQCacheMax, uint32_t fFlags);
void  NemuDtVMemDestroy(struct NemuDtVMem *pVMemArena);
void *NemuDtVMemAlloc(struct NemuDtVMem *pVMemArena, size_t cbMem, uint32_t fFlags);
void  NemuDtVMemFree(struct NemuDtVMem *pVMemArena, void *pvMem, size_t cbMem);

/*
 * Copy In/Out
 */
#define copyin                      NemuDtCopyIn
#define copyout                     NemuDtCopyOut
int  NemuDtCopyIn(void const *pvUser, void *pvDst, size_t cb);
int  NemuDtCopyOut(void const *pvSrc, void *pvUser, size_t cb);

/*
 * Device numbers.
 */
typedef uint64_t                    dev_t;
typedef uint32_t                    major_t;
typedef uint32_t                    minor_t;
#define makedevice(a_Maj, a_Min)    RT_MAKE_U64(a_Min, a_Maj)
#define getemajor(a_Dev)            RT_HIDWORD(a_Dev)
#define geteminor(a_Dev)            RT_LODWORD(a_Dev)
#define getminor(a_Dev)             RT_LODWORD(a_Dev)

/*
 * DDI
 */
# define DDI_SUCCESS                (0)
# define DDI_FAILURE                (-1)
# if 0 /* not needed */
# define ddi_soft_state_init        NemuDtDdiSoftStateInit
# define ddi_soft_state_fini        NemuDtDdiSoftStateTerm
# define ddi_soft_state_zalloc      NemuDtDdiSoftStateAllocZ
# define ddi_get_soft_state         NemuDtDdiSoftStateGet
# define ddi_soft_state_free        NemuDtDdiSoftStateFree
int   NemuDtDdiSoftStateInit(void **ppvSoftStates, size_t cbSoftState, uint32_t cMaxItems);
int   NemuDtDdiSoftStateTerm(void **ppvSoftStates);
int   NemuDtDdiSoftStateAllocZ(void *pvSoftStates, RTDEV uMinor);
int   NemuDtDdiSoftStateFree(void *pvSoftStates, RTDEV uMinor);
void *NemuDtDdiSoftStateGet(void *pvSoftStates, RTDEV uMinor);

typedef enum { DDI_ATT_CMD_DUMMY }  ddi_attach_cmd_t;
typedef enum { DDI_DETACH, DDI_SUSPEND }  ddi_detach_cmd_t;
typedef struct NemuDtDevInfo        dev_info_t;
# define ddi_driver_major           NemuDtDdiDriverMajor
# define ddi_report_dev             NemuDtDdiReportDev
major_t NemuDtDdiDriverMajor(struct NemuDtDevInfo *pDevInfo);
void    NemuDtDdiReportDev(struct NemuDtDevInfo *pDevInfo);
# endif

/*
 * DTrace bits we've made external.
 */
extern int dtrace_attach(void);
extern int dtrace_detach(void);
struct dtrace_state;
extern int dtrace_open(struct dtrace_state **ppState, struct NemuDtCred *cred_p);
extern int dtrace_ioctl(struct dtrace_state *state, int cmd, intptr_t arg, int32_t *rv);
extern int dtrace_close(struct dtrace_state *state);

#endif /* IN_RING0 */


#ifdef IN_RING3
/*
 * Make life a little easier in ring-3.
 */

/* Replacement for strndup(), requires editing the code unfortunately. */
# define MY_STRDUPA(a_pszRes, a_pszSrc) \
    do { \
        size_t cb = strlen(a_pszSrc) + 1; \
        (a_pszRes) = (char *)alloca(cb); \
        memcpy(a_pszRes, a_pszSrc, cb); \
    } while (0)

/*
 * gelf
 */
# include "../../../Runtime/include/internal/ldrELF64.h"
typedef Elf64_Half  GElf_Half;
typedef Elf64_Xword GElf_Xword;
typedef Elf64_Shdr  GElf_Shdr;
typedef Elf64_Ehdr  GElf_Ehdr;
typedef Elf64_Sym   GElf_Sym;
typedef Elf64_Addr  GElf_Addr;
#define GELF_ST_INFO ELF64_ST_INFO
#define GELF_ST_TYPE ELF64_ST_TYPE
#define GELF_ST_BIND ELF64_ST_BIND

/*
 * MSC stuff.
 */
# ifdef _MSC_VER
#  ifndef SIZE_MAX
#   if ARCH_BITS == 32
#    define SIZE_MAX UINT32_MAX
#   else
#    define SIZE_MAX UINT64_MAX
#   endif
#  endif
# endif /* _MSC_VER */

#endif /* IN_RING3 */

RT_C_DECLS_END
#endif

