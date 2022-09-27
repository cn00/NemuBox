/* $Id: NemuDispCm.cpp $ */

/** @file
 * NemuVideo Display D3D User mode dll
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

#include "NemuDispD3DCmn.h"
#include "NemuDispD3D.h"
#include <NemuDisplay.h>

#include <iprt/list.h>

#ifdef NEMU_WITH_CROGL
#include <cr_protocol.h>
#endif

typedef struct NEMUDISPCM_SESSION
{
    HANDLE hEvent;
    CRITICAL_SECTION CritSect;
    /** List of NEMUWDDMDISP_CONTEXT nodes. */
    RTLISTANCHOR CtxList;
    bool bQueryMp;
} NEMUDISPCM_SESSION, *PNEMUDISPCM_SESSION;

typedef struct NEMUDISPCM_MGR
{
    NEMUDISPCM_SESSION Session;
} NEMUDISPCM_MGR, *PNEMUDISPCM_MGR;

/* the cm is one per process */
static NEMUDISPCM_MGR g_pNemuCmMgr;

HRESULT nemuDispCmSessionTerm(PNEMUDISPCM_SESSION pSession)
{
#ifdef DEBUG_misha
    Assert(RTListIsEmpty(&pSession->CtxList));
#endif
    BOOL bRc = CloseHandle(pSession->hEvent);
    Assert(bRc);
    if (bRc)
    {
        DeleteCriticalSection(&pSession->CritSect);
        return S_OK;
    }
    DWORD winEr = GetLastError();
    HRESULT hr = HRESULT_FROM_WIN32(winEr);
    return hr;
}

HRESULT nemuDispCmSessionInit(PNEMUDISPCM_SESSION pSession)
{
    HANDLE hEvent = CreateEvent(NULL,
            FALSE, /* BOOL bManualReset */
            FALSE, /* BOOL bInitialState */
            NULL /* LPCTSTR lpName */
            );
    Assert(hEvent);
    if (hEvent)
    {
        pSession->hEvent = hEvent;
        InitializeCriticalSection(&pSession->CritSect);
        RTListInit(&pSession->CtxList);
        pSession->bQueryMp = false;
        return S_OK;
    }
    DWORD winEr = GetLastError();
    HRESULT hr = HRESULT_FROM_WIN32(winEr);
    return hr;
}

void nemuDispCmSessionCtxAdd(PNEMUDISPCM_SESSION pSession, PNEMUWDDMDISP_CONTEXT pContext)
{
    EnterCriticalSection(&pSession->CritSect);
    RTListAppend(&pSession->CtxList, &pContext->ListNode);
    LeaveCriticalSection(&pSession->CritSect);
}

void nemuDispCmSessionCtxRemoveLocked(PNEMUDISPCM_SESSION pSession, PNEMUWDDMDISP_CONTEXT pContext)
{
    RTListNodeRemove(&pContext->ListNode);
}

void nemuDispCmSessionCtxRemove(PNEMUDISPCM_SESSION pSession, PNEMUWDDMDISP_CONTEXT pContext)
{
    EnterCriticalSection(&pSession->CritSect);
    nemuDispCmSessionCtxRemoveLocked(pSession, pContext);
    LeaveCriticalSection(&pSession->CritSect);
}

HRESULT nemuDispCmInit()
{
    HRESULT hr = nemuDispCmSessionInit(&g_pNemuCmMgr.Session);
    Assert(hr == S_OK);
    return hr;
}

HRESULT nemuDispCmTerm()
{
    HRESULT hr = nemuDispCmSessionTerm(&g_pNemuCmMgr.Session);
    Assert(hr == S_OK);
    return hr;
}

HRESULT nemuDispCmCtxCreate(PNEMUWDDMDISP_DEVICE pDevice, PNEMUWDDMDISP_CONTEXT pContext)
{
    BOOL fIsCrContext;
    NEMUWDDM_CREATECONTEXT_INFO Info = {0};
    Info.u32IfVersion = pDevice->u32IfVersion;
    if (NEMUDISPMODE_IS_3D(pDevice->pAdapter))
    {
        Info.enmType = NEMUWDDM_CONTEXT_TYPE_CUSTOM_3D;
#ifdef NEMU_WITH_CROGL
        Info.crVersionMajor = CR_PROTOCOL_VERSION_MAJOR;
        Info.crVersionMinor = CR_PROTOCOL_VERSION_MINOR;
#else
        WARN(("not expected"));
        Info.crVersionMajor = 0;
        Info.crVersionMinor = 0;
#endif
        fIsCrContext = TRUE;
    }
    else
    {
        Info.enmType = NEMUWDDM_CONTEXT_TYPE_CUSTOM_2D;
        fIsCrContext = FALSE;
    }
    Info.hUmEvent = (uint64_t)g_pNemuCmMgr.Session.hEvent;
    Info.u64UmInfo = (uint64_t)pContext;

    if (NEMUDISPMODE_IS_3D(pDevice->pAdapter))
    {
        pContext->ContextInfo.NodeOrdinal = NEMUWDDM_NODE_ID_3D;
        pContext->ContextInfo.EngineAffinity = NEMUWDDM_ENGINE_ID_3D;
    }
    else
    {
        pContext->ContextInfo.NodeOrdinal = NEMUWDDM_NODE_ID_2D_VIDEO;
        pContext->ContextInfo.EngineAffinity = NEMUWDDM_ENGINE_ID_2D_VIDEO;
    }
    pContext->ContextInfo.Flags.Value = 0;
    pContext->ContextInfo.pPrivateDriverData = &Info;
    pContext->ContextInfo.PrivateDriverDataSize = sizeof (Info);
    pContext->ContextInfo.hContext = 0;
    pContext->ContextInfo.pCommandBuffer = NULL;
    pContext->ContextInfo.CommandBufferSize = 0;
    pContext->ContextInfo.pAllocationList = NULL;
    pContext->ContextInfo.AllocationListSize = 0;
    pContext->ContextInfo.pPatchLocationList = NULL;
    pContext->ContextInfo.PatchLocationListSize = 0;

    HRESULT hr = S_OK;
    hr = pDevice->RtCallbacks.pfnCreateContextCb(pDevice->hDevice, &pContext->ContextInfo);
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        Assert(pContext->ContextInfo.hContext);
        pContext->ContextInfo.pPrivateDriverData = NULL;
        pContext->ContextInfo.PrivateDriverDataSize = 0;
        nemuDispCmSessionCtxAdd(&g_pNemuCmMgr.Session, pContext);
        pContext->pDevice = pDevice;
        if (fIsCrContext)
        {
#ifdef NEMU_WITH_CRHGSMI
            if (pDevice->pAdapter->u32Nemu3DCaps & CR_NEMU_CAP_CMDVBVA)
                nemuUhgsmiD3DInit(&pDevice->Uhgsmi, pDevice);
            else
                nemuUhgsmiD3DEscInit(&pDevice->Uhgsmi, pDevice);
#endif
        }
    }
    else
    {
        exit(1);
    }

    return hr;
}

HRESULT nemuDispCmSessionCtxDestroy(PNEMUDISPCM_SESSION pSession, PNEMUWDDMDISP_DEVICE pDevice, PNEMUWDDMDISP_CONTEXT pContext)
{
    EnterCriticalSection(&pSession->CritSect);
    Assert(pContext->ContextInfo.hContext);
    D3DDDICB_DESTROYCONTEXT DestroyContext;
    Assert(pDevice->DefaultContext.ContextInfo.hContext);
    DestroyContext.hContext = pDevice->DefaultContext.ContextInfo.hContext;
    HRESULT hr = pDevice->RtCallbacks.pfnDestroyContextCb(pDevice->hDevice, &DestroyContext);
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        nemuDispCmSessionCtxRemoveLocked(pSession, pContext);
    }
    LeaveCriticalSection(&pSession->CritSect);
    return hr;
}

HRESULT nemuDispCmCtxDestroy(PNEMUWDDMDISP_DEVICE pDevice, PNEMUWDDMDISP_CONTEXT pContext)
{
    return nemuDispCmSessionCtxDestroy(&g_pNemuCmMgr.Session, pDevice, pContext);
}

static HRESULT nemuDispCmSessionCmdQueryData(PNEMUDISPCM_SESSION pSession, PNEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD pCmd, uint32_t cbCmd)
{

    HRESULT hr = S_OK;
    D3DDDICB_ESCAPE DdiEscape;
    DdiEscape.Flags.Value = 0;
    DdiEscape.pPrivateDriverData = pCmd;
    DdiEscape.PrivateDriverDataSize = cbCmd;

    pCmd->EscapeHdr.escapeCode = NEMUESC_GETNEMUVIDEOCMCMD;

    PNEMUWDDMDISP_CONTEXT pContext = NULL, pCurCtx;

    /* lock to ensure the context is not destroyed */
    EnterCriticalSection(&pSession->CritSect);
    /* use any context for identifying the kernel CmSession. We're using the first one */
    RTListForEach(&pSession->CtxList, pCurCtx, NEMUWDDMDISP_CONTEXT, ListNode)
    {
        PNEMUWDDMDISP_DEVICE pDevice = pCurCtx->pDevice;
        if (NEMUDISPMODE_IS_3D(pDevice->pAdapter))
        {
            pContext = pCurCtx;
            break;
        }
    }
    if (pContext)
    {
        PNEMUWDDMDISP_DEVICE pDevice = pContext->pDevice;
        DdiEscape.hDevice = pDevice->hDevice;
        DdiEscape.hContext = pContext->ContextInfo.hContext;
        Assert (DdiEscape.hContext);
        Assert (DdiEscape.hDevice);
        hr = pDevice->RtCallbacks.pfnEscapeCb(pDevice->pAdapter->hAdapter, &DdiEscape);
        LeaveCriticalSection(&pSession->CritSect);
        Assert(hr == S_OK);
        if (hr == S_OK)
        {
            if (!pCmd->Hdr.cbCmdsReturned && !pCmd->Hdr.cbRemainingFirstCmd)
                hr = S_FALSE;
        }
        else
        {
            nemuVDbgPrint(("DispD3D: nemuDispCmSessionCmdQueryData, pfnEscapeCb failed hr (0x%x)\n", hr));
            exit(1);
        }
    }
    else
    {
        LeaveCriticalSection(&pSession->CritSect);
        hr = S_FALSE;
    }

    return hr;
}

HRESULT nemuDispCmCmdSessionInterruptWait(PNEMUDISPCM_SESSION pSession)
{
    SetEvent(pSession->hEvent);
    return S_OK;
}

HRESULT nemuDispCmSessionCmdGet(PNEMUDISPCM_SESSION pSession, PNEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD pCmd, uint32_t cbCmd, DWORD dwMilliseconds)
{
    Assert(cbCmd >= sizeof (NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD));
    if (cbCmd < sizeof (NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD))
        return E_INVALIDARG;

    do
    {

        if (pSession->bQueryMp)
        {
            HRESULT hr = nemuDispCmSessionCmdQueryData(pSession, pCmd, cbCmd);
            Assert(hr == S_OK || hr == S_FALSE);
            if (hr == S_OK || hr != S_FALSE)
            {
                return hr;
            }

            pSession->bQueryMp = false;
        }

        DWORD dwResult = WaitForSingleObject(pSession->hEvent, dwMilliseconds);
        switch(dwResult)
        {
            case WAIT_OBJECT_0:
            {
                pSession->bQueryMp = true;
                break; /* <- query commands */
            }
            case WAIT_TIMEOUT:
            {
                Assert(!pSession->bQueryMp);
                return WAIT_TIMEOUT;
            }
            default:
                Assert(0);
                return E_FAIL;
        }
    } while (1);

    /* should never be here */
    Assert(0);
    return E_FAIL;
}

HRESULT nemuDispCmCmdGet(PNEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD pCmd, uint32_t cbCmd, DWORD dwMilliseconds)
{
    return nemuDispCmSessionCmdGet(&g_pNemuCmMgr.Session, pCmd, cbCmd, dwMilliseconds);
}

HRESULT nemuDispCmCmdInterruptWait()
{
    return nemuDispCmCmdSessionInterruptWait(&g_pNemuCmMgr.Session);
}

void nemuDispCmLog(LPCSTR pszMsg)
{
    PNEMUDISPCM_SESSION pSession = &g_pNemuCmMgr.Session;

    EnterCriticalSection(&pSession->CritSect);
    /* use any context for identifying the kernel CmSession. We're using the first one */
    PNEMUWDDMDISP_CONTEXT pContext = RTListGetFirst(&pSession->CtxList, NEMUWDDMDISP_CONTEXT, ListNode);
    Assert(pContext);
    if (pContext)
    {
        PNEMUWDDMDISP_DEVICE pDevice = pContext->pDevice;
        Assert(pDevice);
        nemuVDbgPrint(("%s", pszMsg));
    }
    LeaveCriticalSection(&pSession->CritSect);
}
