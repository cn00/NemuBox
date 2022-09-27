/* $Id: NemuDrvTool.h $ */
/** @file
 * Windows Driver R0 Tooling.
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
#ifndef ___NemuDrvTool_win_h___
#define ___NemuDrvTool_win_h___
#include <Nemu/cdefs.h>
#include <iprt/stdint.h>
#include <iprt/assert.h>
#include <iprt/asm.h>

RT_C_DECLS_BEGIN
#if (_MSC_VER >= 1400) && !defined(NEMU_WITH_PATCHED_DDK)
# define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
# define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
# define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
# define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
# pragma warning(disable : 4163)
#endif
#if (_MSC_VER >= 1600) && !defined(NEMU_WITH_PATCHED_DDK)
# define _interlockedbittestandset      _interlockedbittestandset_StillStupidDdkVsCompilerCrap
# define _interlockedbittestandreset    _interlockedbittestandreset_StillStupidDdkVsCompilerCrap
# define _interlockedbittestandset64    _interlockedbittestandset64_StillStupidDdkVsCompilerCrap
# define _interlockedbittestandreset64  _interlockedbittestandreset64_StillStupidDdkVsCompilerCrap
# pragma warning(disable : 4163)
#endif

#include <wdm.h>

#if (_MSC_VER >= 1400) && !defined(NEMU_WITH_PATCHED_DDK)
# pragma warning(default : 4163)
# undef  _InterlockedExchange
# undef  _InterlockedExchangeAdd
# undef  _InterlockedCompareExchange
# undef  _InterlockedAddLargeStatistic
#endif
#if (_MSC_VER >= 1600) && !defined(NEMU_WITH_PATCHED_DDK)
# pragma warning(default : 4163)
# undef _interlockedbittestandset
# undef _interlockedbittestandreset
# undef _interlockedbittestandset64
# undef _interlockedbittestandreset64
#endif


#if 0
/* enable this in case we include this in a dll*/
# ifdef IN_NEMUDRVTOOL
#  define NEMUDRVTOOL_DECL(a_Type) DECLEXPORT(a_Type)
# else
#  define NEMUDRVTOOL_DECL(a_Type) DECLIMPORT(a_Type)
# endif
#else
/*enable this in case we include this in a static lib*/
# define NEMUDRVTOOL_DECL(a_Type) a_Type NEMUCALL
#endif

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegOpenKeyU(OUT PHANDLE phKey, IN PUNICODE_STRING pName, IN ACCESS_MASK fAccess);
NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegOpenKey(OUT PHANDLE phKey, IN PWCHAR pName, IN ACCESS_MASK fAccess);
NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegCloseKey(IN HANDLE hKey);
NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegQueryValueDword(IN HANDLE hKey, IN PWCHAR pName, OUT PULONG pDword);
NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolRegSetValueDword(IN HANDLE hKey, IN PWCHAR pName, OUT ULONG val);

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolIoPostAsync(PDEVICE_OBJECT pDevObj, PIRP pIrp, PKEVENT pEvent);
NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolIoPostSync(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolIoPostSyncWithTimeout(PDEVICE_OBJECT pDevObj, PIRP pIrp, ULONG dwTimeoutMs);
DECLINLINE(NTSTATUS) NemuDrvToolIoComplete(PIRP pIrp, NTSTATUS Status, ULONG ulInfo)
{
    pIrp->IoStatus.Status = Status;
    pIrp->IoStatus.Information = ulInfo;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    return Status;
}

typedef struct NEMUDRVTOOL_REF
{
    volatile uint32_t cRefs;
} NEMUDRVTOOL_REF, *PNEMUDRVTOOL_REF;

DECLINLINE(void) NemuDrvToolRefInit(PNEMUDRVTOOL_REF pRef)
{
    pRef->cRefs = 1;
}

DECLINLINE(uint32_t) NemuDrvToolRefRetain(PNEMUDRVTOOL_REF pRef)
{
    Assert(pRef->cRefs);
    Assert(pRef->cRefs < UINT32_MAX / 2);
    return ASMAtomicIncU32(&pRef->cRefs);
}

DECLINLINE(uint32_t) NemuDrvToolRefRelease(PNEMUDRVTOOL_REF pRef)
{
    uint32_t cRefs = ASMAtomicDecU32(&pRef->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    return cRefs;
}

NEMUDRVTOOL_DECL(VOID) NemuDrvToolRefWaitEqual(PNEMUDRVTOOL_REF pRef, uint32_t u32Val);

NEMUDRVTOOL_DECL(NTSTATUS) NemuDrvToolStrCopy(PUNICODE_STRING pDst, CONST PUNICODE_STRING pSrc);
NEMUDRVTOOL_DECL(VOID) NemuDrvToolStrFree(PUNICODE_STRING pStr);

RT_C_DECLS_END

#endif /* #ifndef ___NemuDrvTool_win_h___ */
