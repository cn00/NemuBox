/* $Id: NemuMPUtils.h $ */
/** @file
 * Nemu Miniport common utils header
 */

/*
 * Copyright (C) 2011-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef NEMUMPUTILS_H
#define NEMUMPUTILS_H

/*Sanity check*/
#if defined(NEMU_XPDM_MINIPORT)==defined(NEMU_WDDM_MINIPORT)
#error One of the NEMU_XPDM_MINIPORT or NEMU_WDDM_MINIPORT should be defined!
#endif

#include <iprt/cdefs.h>
#define LOG_GROUP LOG_GROUP_DRV_MINIPORT
#include <Nemu/log.h>
#define NEMU_VIDEO_LOG_NAME "NemuMP"
#ifdef NEMU_WDDM_MINIPORT
# ifndef NEMU_WDDM_MINIPORT_WITH_FLOW_LOGGING
#  define NEMU_VIDEO_LOGFLOW_LOGGER(_m) do {} while (0)
# endif
#endif
#include "common/NemuVideoLog.h"
#include <iprt/err.h>
#include <iprt/assert.h>

RT_C_DECLS_BEGIN
#ifdef NEMU_XPDM_MINIPORT
#  include <dderror.h>
#  include <devioctl.h>
#else
#  ifdef PAGE_SIZE
#    undef PAGE_SIZE
#  endif
#  ifdef PAGE_SHIFT
#    undef PAGE_SHIFT
#  endif
#  define NEMU_WITH_WORKAROUND_MISSING_PACK
#  if (_MSC_VER >= 1400) && !defined(NEMU_WITH_PATCHED_DDK)
#    define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
#    define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
#    define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
#    define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
#    define _interlockedbittestandset      _interlockedbittestandset_StupidDDKVsCompilerCrap
#    define _interlockedbittestandreset    _interlockedbittestandreset_StupidDDKVsCompilerCrap
#    define _interlockedbittestandset64    _interlockedbittestandset64_StupidDDKVsCompilerCrap
#    define _interlockedbittestandreset64  _interlockedbittestandreset64_StupidDDKVsCompilerCrap
#    pragma warning(disable : 4163)
#    ifdef NEMU_WITH_WORKAROUND_MISSING_PACK
#      pragma warning(disable : 4103)
#    endif
#    include <ntddk.h>
#    pragma warning(default : 4163)
#    ifdef NEMU_WITH_WORKAROUND_MISSING_PACK
#      pragma pack()
#      pragma warning(default : 4103)
#    endif
#    undef  _InterlockedExchange
#    undef  _InterlockedExchangeAdd
#    undef  _InterlockedCompareExchange
#    undef  _InterlockedAddLargeStatistic
#    undef  _interlockedbittestandset
#    undef  _interlockedbittestandreset
#    undef  _interlockedbittestandset64
#    undef  _interlockedbittestandreset64
#  else
#    include <ntddk.h>
#  endif
#  include <dispmprt.h>
#  include <ntddvdeo.h>
#  include <dderror.h>
#endif
RT_C_DECLS_END

/*Windows version identifier*/
typedef enum
{
    WINVERSION_UNKNOWN = 0,
    WINVERSION_NT4     = 1,
    WINVERSION_2K      = 2,
    WINVERSION_XP      = 3,
    WINVERSION_VISTA   = 4,
    WINVERSION_7       = 5,
    WINVERSION_8       = 6,
    WINVERSION_81      = 7,
    WINVERSION_10      = 8
} nemuWinVersion_t;

RT_C_DECLS_BEGIN
nemuWinVersion_t NemuQueryWinVersion();
uint32_t NemuGetHeightReduction();
bool NemuLikesVideoMode(uint32_t display, uint32_t width, uint32_t height, uint32_t bpp);
bool NemuQueryDisplayRequest(uint32_t *xres, uint32_t *yres, uint32_t *bpp, uint32_t *pDisplayId);
bool NemuQueryHostWantsAbsolute();
bool NemuQueryPointerPos(uint16_t *pPosX, uint16_t *pPosY);
RT_C_DECLS_END

#define VBE_DISPI_TOTAL_VIDEO_MEMORY_BYTES 4*_1M

#define NEMUMP_WARN_VPS_NOBP(_vps)     \
if ((_vps) != NO_ERROR)           \
{                                 \
    WARN_NOBP(("vps(%#x)!=NO_ERROR", _vps)); \
}

#define NEMUMP_WARN_VPS(_vps)     \
if ((_vps) != NO_ERROR)           \
{                                 \
    WARN(("vps(%#x)!=NO_ERROR", _vps)); \
}


#define NEMUMP_CHECK_VPS_BREAK(_vps)    \
if ((_vps) != NO_ERROR)                 \
{                                       \
    break;                              \
}

#ifdef DEBUG_misha
/* specifies whether the nemuVDbgBreakF should break in the debugger
 * windbg seems to have some issues when there is a lot ( >~50) of sw breakpoints defined
 * to simplify things we just insert breaks for the case of intensive debugging WDDM driver*/
extern int g_bNemuVDbgBreakF;
extern int g_bNemuVDbgBreakFv;
#define nemuVDbgBreakF() do { if (g_bNemuVDbgBreakF) AssertBreakpoint(); } while (0)
#define nemuVDbgBreakFv() do { if (g_bNemuVDbgBreakFv) AssertBreakpoint(); } while (0)
#else
#define nemuVDbgBreakF() do { } while (0)
#define nemuVDbgBreakFv() do { } while (0)
#endif

#endif /*NEMUMPUTILS_H*/
