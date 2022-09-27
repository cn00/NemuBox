/* $Id: NemuDispMpLogger.cpp $ */

/** @file
 * Nemu WDDM Display backdoor logger implementation
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* We're unable to use standard r3 vbgl-based backdoor logging API because win8 Metro apps
 * can not do CreateFile/Read/Write by default
 * this is why we use miniport escape functionality to issue backdoor log string to the miniport
 * and submit it to host via standard r0 backdoor logging api accordingly */
#   if (_MSC_VER >= 1400) && !defined(NEMU_WITH_PATCHED_DDK)
#       define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
#       define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
#       define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
#       define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
#       define _interlockedbittestandset      _interlockedbittestandset_StupidDDKVsCompilerCrap
#       define _interlockedbittestandreset    _interlockedbittestandreset_StupidDDKVsCompilerCrap
#       define _interlockedbittestandset64    _interlockedbittestandset64_StupidDDKVsCompilerCrap
#       define _interlockedbittestandreset64  _interlockedbittestandreset64_StupidDDKVsCompilerCrap
#       pragma warning(disable : 4163)
#       include <windows.h>
#       pragma warning(default : 4163)
#       undef  _InterlockedExchange
#       undef  _InterlockedExchangeAdd
#       undef  _InterlockedCompareExchange
#       undef  _InterlockedAddLargeStatistic
#       undef  _interlockedbittestandset
#       undef  _interlockedbittestandreset
#       undef  _interlockedbittestandset64
#       undef  _interlockedbittestandreset64
#   else
#       include <windows.h>
#   endif
#include "NemuDispMpLogger.h"
#include <d3d9types.h>
#include <D3dumddi.h>
#include <d3dhal.h>
#include "../../common/wddm/NemuMPIf.h"
#include "NemuDispKmt.h"

#define NEMU_VIDEO_LOG_NAME "NemuDispMpLogger"
#include <common/NemuVideoLog.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>

#include <stdio.h>

typedef enum
{
    NEMUDISPMPLOGGER_STATE_UNINITIALIZED = 0,
    NEMUDISPMPLOGGER_STATE_INITIALIZING,
    NEMUDISPMPLOGGER_STATE_INITIALIZED,
    NEMUDISPMPLOGGER_STATE_UNINITIALIZING
} NEMUDISPMPLOGGER_STATE;

typedef struct NEMUDISPMPLOGGER
{
    NEMUDISPKMT_CALLBACKS KmtCallbacks;
    NEMUDISPMPLOGGER_STATE enmState;
} NEMUDISPMPLOGGER, *PNEMUDISPMPLOGGER;

static NEMUDISPMPLOGGER g_NemuDispMpLogger = {0};

static PNEMUDISPMPLOGGER nemuDispMpLoggerGet()
{
    if (ASMAtomicCmpXchgU32((volatile uint32_t *)&g_NemuDispMpLogger.enmState, NEMUDISPMPLOGGER_STATE_INITIALIZING, NEMUDISPMPLOGGER_STATE_UNINITIALIZED))
    {
        HRESULT hr = nemuDispKmtCallbacksInit(&g_NemuDispMpLogger.KmtCallbacks);
        if (hr == S_OK)
        {
            /* we are on Vista+
             * check if we can Open Adapter, i.e. WDDM driver is installed */
            NEMUDISPKMT_ADAPTER Adapter;
            hr = nemuDispKmtOpenAdapter(&g_NemuDispMpLogger.KmtCallbacks, &Adapter);
            if (hr == S_OK)
            {
                ASMAtomicWriteU32((volatile uint32_t *)&g_NemuDispMpLogger.enmState, NEMUDISPMPLOGGER_STATE_INITIALIZED);
                nemuDispKmtCloseAdapter(&Adapter);
                return &g_NemuDispMpLogger;
            }
            nemuDispKmtCallbacksTerm(&g_NemuDispMpLogger.KmtCallbacks);
        }
    }
    else if (ASMAtomicReadU32((volatile uint32_t *)&g_NemuDispMpLogger.enmState) == NEMUDISPMPLOGGER_STATE_INITIALIZED)
    {
        return &g_NemuDispMpLogger;
    }
    return NULL;
}

NEMUDISPMPLOGGER_DECL(int) NemuDispMpLoggerInit()
{
    PNEMUDISPMPLOGGER pLogger = nemuDispMpLoggerGet();
    if (!pLogger)
        return VERR_NOT_SUPPORTED;
    return VINF_SUCCESS;
}

NEMUDISPMPLOGGER_DECL(int) NemuDispMpLoggerTerm()
{
    if (ASMAtomicCmpXchgU32((volatile uint32_t *)&g_NemuDispMpLogger.enmState, NEMUDISPMPLOGGER_STATE_UNINITIALIZING, NEMUDISPMPLOGGER_STATE_INITIALIZED))
    {
        nemuDispKmtCallbacksTerm(&g_NemuDispMpLogger.KmtCallbacks);
        ASMAtomicWriteU32((volatile uint32_t *)&g_NemuDispMpLogger.enmState, NEMUDISPMPLOGGER_STATE_UNINITIALIZED);
        return S_OK;
    }
    else if (ASMAtomicReadU32((volatile uint32_t *)&g_NemuDispMpLogger.enmState) == NEMUDISPMPLOGGER_STATE_UNINITIALIZED)
    {
        return S_OK;
    }
    return VERR_NOT_SUPPORTED;
}

NEMUDISPMPLOGGER_DECL(void) NemuDispMpLoggerLog(char * szString)
{
    PNEMUDISPMPLOGGER pLogger = nemuDispMpLoggerGet();
    if (!pLogger)
        return;

    NEMUDISPKMT_ADAPTER Adapter;
    HRESULT hr = nemuDispKmtOpenAdapter(&pLogger->KmtCallbacks, &Adapter);
    if (hr == S_OK)
    {
        uint32_t cbString = (uint32_t)strlen(szString) + 1;
        uint32_t cbCmd = RT_OFFSETOF(NEMUDISPIFESCAPE_DBGPRINT, aStringBuf[cbString]);
        PNEMUDISPIFESCAPE_DBGPRINT pCmd = (PNEMUDISPIFESCAPE_DBGPRINT)RTMemAllocZ(cbCmd);
        if (pCmd)
        {
            pCmd->EscapeHdr.escapeCode = NEMUESC_DBGPRINT;
            memcpy(pCmd->aStringBuf, szString, cbString);

            D3DKMT_ESCAPE EscapeData = {0};
            EscapeData.hAdapter = Adapter.hAdapter;
            //EscapeData.hDevice = NULL;
            EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    //        EscapeData.Flags.HardwareAccess = 1;
            EscapeData.pPrivateDriverData = pCmd;
            EscapeData.PrivateDriverDataSize = cbCmd;
            //EscapeData.hContext = NULL;

            int Status = pLogger->KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
            if (Status)
            {
                BP_WARN();
            }

            RTMemFree(pCmd);
        }
        else
        {
            BP_WARN();
        }
        hr = nemuDispKmtCloseAdapter(&Adapter);
        if(hr != S_OK)
        {
            BP_WARN();
        }
    }
}

NEMUDISPMPLOGGER_DECL(void) NemuDispMpLoggerLogF(char * szString, ...)
{
    PNEMUDISPMPLOGGER pLogger = nemuDispMpLoggerGet();
    if (!pLogger)
        return;

    char szBuffer[4096] = {0};
    va_list pArgList;
    va_start(pArgList, szString);
    _vsnprintf(szBuffer, sizeof(szBuffer) / sizeof(szBuffer[0]), szString, pArgList);
    va_end(pArgList);

    NemuDispMpLoggerLog(szBuffer);
}

static void nemuDispMpLoggerDumpBuf(void *pvBuf, uint32_t cbBuf, NEMUDISPIFESCAPE_DBGDUMPBUF_TYPE enmBuf)
{
    PNEMUDISPMPLOGGER pLogger = nemuDispMpLoggerGet();
    if (!pLogger)
        return;

    NEMUDISPKMT_ADAPTER Adapter;
    HRESULT hr = nemuDispKmtOpenAdapter(&pLogger->KmtCallbacks, &Adapter);
    if (hr == S_OK)
    {
        uint32_t cbCmd = RT_OFFSETOF(NEMUDISPIFESCAPE_DBGDUMPBUF, aBuf[cbBuf]);
        PNEMUDISPIFESCAPE_DBGDUMPBUF pCmd = (PNEMUDISPIFESCAPE_DBGDUMPBUF)RTMemAllocZ(cbCmd);
        if (pCmd)
        {
            pCmd->EscapeHdr.escapeCode = NEMUESC_DBGDUMPBUF;
            pCmd->enmType = enmBuf;
#ifdef NEMU_WDDM_WOW64
            pCmd->Flags.WoW64 = 1;
#endif
            memcpy(pCmd->aBuf, pvBuf, cbBuf);

            D3DKMT_ESCAPE EscapeData = {0};
            EscapeData.hAdapter = Adapter.hAdapter;
            //EscapeData.hDevice = NULL;
            EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    //        EscapeData.Flags.HardwareAccess = 1;
            EscapeData.pPrivateDriverData = pCmd;
            EscapeData.PrivateDriverDataSize = cbCmd;
            //EscapeData.hContext = NULL;

            int Status = pLogger->KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
            if (Status)
            {
                BP_WARN();
            }

            RTMemFree(pCmd);
        }
        else
        {
            BP_WARN();
        }
        hr = nemuDispKmtCloseAdapter(&Adapter);
        if(hr != S_OK)
        {
            BP_WARN();
        }
    }
}

NEMUDISPMPLOGGER_DECL(void) NemuDispMpLoggerDumpD3DCAPS9(struct _D3DCAPS9 *pCaps)
{
    nemuDispMpLoggerDumpBuf(pCaps, sizeof (*pCaps), NEMUDISPIFESCAPE_DBGDUMPBUF_TYPE_D3DCAPS9);
}
