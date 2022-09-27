/* $Id: NemuUhgsmiBase.cpp $ */

/** @file
 * NemuVideo Display D3D User mode dll
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

#include "NemuDispD3DCmn.h"

DECLCALLBACK(int) nemuUhgsmiBaseEscBufferLock(PNEMUUHGSMI_BUFFER pBuf, uint32_t offLock, uint32_t cbLock, NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags, void**pvLock)
{
    PNEMUUHGSMI_BUFFER_PRIVATE_ESC_BASE pBuffer = NEMUUHGSMIESCBASE_GET_BUFFER(pBuf);
    *pvLock = (void*)(pBuffer->Alloc.pvData + offLock);
    return VINF_SUCCESS;
}

DECLCALLBACK(int) nemuUhgsmiBaseEscBufferUnlock(PNEMUUHGSMI_BUFFER pBuf)
{
    return VINF_SUCCESS;
}

int nemuUhgsmiBaseBufferTerm(PNEMUUHGSMI_BUFFER_PRIVATE_ESC_BASE pBuffer)
{
    PNEMUUHGSMI_PRIVATE_BASE pPrivate = NEMUUHGSMIBASE_GET(pBuffer->BasePrivate.pHgsmi);
    NEMUDISPIFESCAPE_UHGSMI_DEALLOCATE DeallocInfo = {0};
    DeallocInfo.EscapeHdr.escapeCode = NEMUESC_UHGSMI_DEALLOCATE;
    DeallocInfo.hAlloc = pBuffer->Alloc.hAlloc;
    return nemuCrHgsmiPrivateEscape(pPrivate, &DeallocInfo, sizeof (DeallocInfo), FALSE);
}

static int nemuUhgsmiBaseEventChkCreate(NEMUUHGSMI_BUFFER_TYPE_FLAGS fUhgsmiType, HANDLE *phSynch)
{
    *phSynch = NULL;

    if (fUhgsmiType.fCommand)
    {
        *phSynch = CreateEvent(
                  NULL, /* LPSECURITY_ATTRIBUTES lpEventAttributes */
                  FALSE, /* BOOL bManualReset */
                  FALSE, /* BOOL bInitialState */
                  NULL /* LPCTSTR lpName */
            );
        Assert(*phSynch);
        if (!*phSynch)
        {
            DWORD winEr = GetLastError();
            /* todo: translate winer */
            return VERR_GENERAL_FAILURE;
        }
    }
    return VINF_SUCCESS;
}

int nemuUhgsmiKmtEscBufferInit(PNEMUUHGSMI_PRIVATE_BASE pPrivate, PNEMUUHGSMI_BUFFER_PRIVATE_ESC_BASE pBuffer, uint32_t cbBuf, NEMUUHGSMI_BUFFER_TYPE_FLAGS fUhgsmiType, PFNNEMUUHGSMI_BUFFER_DESTROY pfnDestroy)
{
    HANDLE hSynch = NULL;
    if (!cbBuf)
        return VERR_INVALID_PARAMETER;

    int rc = nemuUhgsmiBaseEventChkCreate(fUhgsmiType, &hSynch);
    if (RT_FAILURE(rc))
    {
        WARN(("nemuUhgsmiBaseEventChkCreate failed, rc %d", rc));
        return rc;
    }

    cbBuf = NEMUWDDM_ROUNDBOUND(cbBuf, 0x1000);
    Assert(cbBuf);
    uint32_t cPages = cbBuf >> 12;
    Assert(cPages);

    NEMUDISPIFESCAPE_UHGSMI_ALLOCATE AllocInfo = {0};
    AllocInfo.EscapeHdr.escapeCode = NEMUESC_UHGSMI_ALLOCATE;
    AllocInfo.Alloc.cbData = cbBuf;
    AllocInfo.Alloc.hSynch = (uint64_t)hSynch;
    AllocInfo.Alloc.fUhgsmiType = fUhgsmiType;

    rc = nemuCrHgsmiPrivateEscape(pPrivate, &AllocInfo, sizeof (AllocInfo), FALSE);
    if (RT_FAILURE(rc))
    {
        if (hSynch)
            CloseHandle(hSynch);
        WARN(("nemuCrHgsmiPrivateEscape failed, rc %d", rc));
        return rc;
    }

    pBuffer->Alloc = AllocInfo.Alloc;
    Assert(pBuffer->Alloc.pvData);
    pBuffer->BasePrivate.pHgsmi = pPrivate;
    pBuffer->BasePrivate.Base.pfnLock = nemuUhgsmiBaseEscBufferLock;
    pBuffer->BasePrivate.Base.pfnUnlock = nemuUhgsmiBaseEscBufferUnlock;
    pBuffer->BasePrivate.Base.pfnDestroy = pfnDestroy;
    pBuffer->BasePrivate.Base.fType = fUhgsmiType;
    pBuffer->BasePrivate.Base.cbBuffer = AllocInfo.Alloc.cbData;
    pBuffer->hSynch = hSynch;
    return VINF_SUCCESS;
}

DECLCALLBACK(int) nemuUhgsmiBaseEscBufferDestroy(PNEMUUHGSMI_BUFFER pBuf)
{
    PNEMUUHGSMI_BUFFER_PRIVATE_ESC_BASE pBuffer = NEMUUHGSMIESCBASE_GET_BUFFER(pBuf);
    int rc = nemuUhgsmiBaseBufferTerm(pBuffer);
    if (RT_FAILURE(rc))
    {
        WARN(("nemuUhgsmiBaseBufferTerm failed rc %d", rc));
        return rc;
    }

    RTMemFree(pBuffer);
    return VINF_SUCCESS;
}

DECLCALLBACK(int) nemuUhgsmiBaseEscBufferCreate(PNEMUUHGSMI pHgsmi, uint32_t cbBuf, NEMUUHGSMI_BUFFER_TYPE_FLAGS fUhgsmiType, PNEMUUHGSMI_BUFFER* ppBuf)
{
    *ppBuf = NULL;

    PNEMUUHGSMI_BUFFER_PRIVATE_ESC_BASE pBuffer = (PNEMUUHGSMI_BUFFER_PRIVATE_ESC_BASE)RTMemAllocZ(sizeof (*pBuffer));
    if (!pBuffer)
    {
        WARN(("RTMemAllocZ failed"));
        return VERR_NO_MEMORY;
    }

    PNEMUUHGSMI_PRIVATE_BASE pPrivate = NEMUUHGSMIBASE_GET(pHgsmi);
    int rc = nemuUhgsmiKmtEscBufferInit(pPrivate, pBuffer, cbBuf, fUhgsmiType, nemuUhgsmiBaseEscBufferDestroy);
    if (RT_SUCCESS(rc))
    {
        *ppBuf = &pBuffer->BasePrivate.Base;
        return VINF_SUCCESS;
    }

    WARN(("nemuUhgsmiKmtEscBufferInit failed, rc %d", rc));
    RTMemFree(pBuffer);
    return rc;
}

DECLCALLBACK(int) nemuUhgsmiBaseEscBufferSubmit(PNEMUUHGSMI pHgsmi, PNEMUUHGSMI_BUFFER_SUBMIT aBuffers, uint32_t cBuffers)
{
    /* We know chromium will not submit more than three buffers actually,
     * for simplicity allocate it statically on the stack  */
    struct
    {
        NEMUDISPIFESCAPE_UHGSMI_SUBMIT SubmitInfo;
        NEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE aBufInfos[3];
    } Buf;

    if (!cBuffers || cBuffers > RT_ELEMENTS(Buf.aBufInfos))
    {
        WARN(("invalid cBuffers!"));
        return VERR_INVALID_PARAMETER;
    }

    HANDLE hSynch = NEMUUHGSMIESCBASE_GET_BUFFER(aBuffers[0].pBuf)->hSynch;
    if (!hSynch)
    {
        WARN(("the fist buffer is not command!"));
        return VERR_INVALID_PARAMETER;
    }

    PNEMUUHGSMI_PRIVATE_BASE pPrivate = NEMUUHGSMIBASE_GET(pHgsmi);
    Buf.SubmitInfo.EscapeHdr.escapeCode = NEMUESC_UHGSMI_SUBMIT;
    Buf.SubmitInfo.EscapeHdr.u32CmdSpecific = cBuffers;
    for (UINT i = 0; i < cBuffers; ++i)
    {
        NEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE *pSubmInfo = &Buf.SubmitInfo.aBuffers[i];
        PNEMUUHGSMI_BUFFER_SUBMIT pBufInfo = &aBuffers[i];
        PNEMUUHGSMI_BUFFER_PRIVATE_ESC_BASE pBuf = NEMUUHGSMIESCBASE_GET_BUFFER(pBufInfo->pBuf);
        pSubmInfo->hAlloc = pBuf->Alloc.hAlloc;
        if (pBufInfo->fFlags.bEntireBuffer)
        {
            pSubmInfo->Info.offData = 0;
            pSubmInfo->Info.cbData = pBuf->BasePrivate.Base.cbBuffer;
        }
        else
        {
            pSubmInfo->Info.offData = pBufInfo->offData;
            pSubmInfo->Info.cbData = pBufInfo->cbData;
        }
    }

    int rc = nemuCrHgsmiPrivateEscape(pPrivate, &Buf.SubmitInfo, RT_OFFSETOF(NEMUDISPIFESCAPE_UHGSMI_SUBMIT, aBuffers[cBuffers]), FALSE);
    if (RT_SUCCESS(rc))
    {
        DWORD dwResult = WaitForSingleObject(hSynch, INFINITE);
        if (dwResult == WAIT_OBJECT_0)
            return VINF_SUCCESS;
        WARN(("wait failed, (0x%x)", dwResult));
        return VERR_GENERAL_FAILURE;
    }
    else
    {
        WARN(("nemuCrHgsmiPrivateEscape failed, rc (%d)", rc));
    }

    return VERR_GENERAL_FAILURE;
}

/* Cr calls have <= 3args, we try to allocate it on stack first */
typedef struct NEMUCRHGSMI_CALLDATA
{
    NEMUDISPIFESCAPE_CRHGSMICTLCON_CALL CallHdr;
    HGCMFunctionParameter aArgs[3];
} NEMUCRHGSMI_CALLDATA, *PNEMUCRHGSMI_CALLDATA;

int nemuCrHgsmiPrivateCtlConCall(struct NEMUUHGSMI_PRIVATE_BASE *pHgsmi, struct NemuGuestHGCMCallInfo *pCallInfo, int cbCallInfo)
{
    NEMUCRHGSMI_CALLDATA Buf;
    PNEMUCRHGSMI_CALLDATA pBuf;
    int cbBuffer = cbCallInfo + RT_OFFSETOF(NEMUCRHGSMI_CALLDATA, CallHdr.CallInfo);

    if (cbBuffer <= sizeof (Buf))
        pBuf = &Buf;
    else
    {
        pBuf = (PNEMUCRHGSMI_CALLDATA)RTMemAlloc(cbBuffer);
        if (!pBuf)
        {
            WARN(("RTMemAlloc failed!"));
            return VERR_NO_MEMORY;
        }
    }

    pBuf->CallHdr.EscapeHdr.escapeCode = NEMUESC_CRHGSMICTLCON_CALL;
    pBuf->CallHdr.EscapeHdr.u32CmdSpecific = (uint32_t)VERR_GENERAL_FAILURE;
    memcpy(&pBuf->CallHdr.CallInfo, pCallInfo, cbCallInfo);

    int rc = nemuCrHgsmiPrivateEscape(pHgsmi, pBuf, cbBuffer, FALSE);
    if (RT_SUCCESS(rc))
    {
        rc = (int)pBuf->CallHdr.EscapeHdr.u32CmdSpecific;
        if (RT_SUCCESS(rc))
        {
            memcpy(pCallInfo, &pBuf->CallHdr.CallInfo, cbCallInfo);
            rc = VINF_SUCCESS;
        }
        else
            WARN(("nemuCrHgsmiPrivateEscape u32CmdSpecific failed, rc (%d)", rc));
    }
    else
        WARN(("nemuCrHgsmiPrivateEscape failed, rc (%d)", rc));

    /* cleanup */
    if (pBuf != &Buf)
        RTMemFree(pBuf);

    return rc;
}

int nemuCrHgsmiPrivateCtlConGetClientID(struct NEMUUHGSMI_PRIVATE_BASE *pHgsmi, uint32_t *pu32ClientID)
{
    NEMUDISPIFESCAPE GetId = {0};
    GetId.escapeCode = NEMUESC_CRHGSMICTLCON_GETCLIENTID;

    int rc = nemuCrHgsmiPrivateEscape(pHgsmi, &GetId, sizeof (GetId), FALSE);
    if (RT_SUCCESS(rc))
    {
        Assert(GetId.u32CmdSpecific);
        *pu32ClientID = GetId.u32CmdSpecific;
        return VINF_SUCCESS;
    }
    else
    {
        *pu32ClientID = 0;
        WARN(("nemuCrHgsmiPrivateEscape failed, rc (%d)", rc));
    }
    return rc;
}

int nemuCrHgsmiPrivateCtlConGetHostCaps(struct NEMUUHGSMI_PRIVATE_BASE *pHgsmi, uint32_t *pu32HostCaps)
{
    NEMUDISPIFESCAPE GetHostCaps = {0};
    GetHostCaps.escapeCode = NEMUESC_CRHGSMICTLCON_GETHOSTCAPS;

    int rc = nemuCrHgsmiPrivateEscape(pHgsmi, &GetHostCaps, sizeof (GetHostCaps), FALSE);
    if (RT_SUCCESS(rc))
    {
        *pu32HostCaps = GetHostCaps.u32CmdSpecific;
        return VINF_SUCCESS;
    }
    else
    {
        *pu32HostCaps = 0;
        WARN(("nemuCrHgsmiPrivateEscape failed, rc (%d)", rc));
    }
    return rc;
}
