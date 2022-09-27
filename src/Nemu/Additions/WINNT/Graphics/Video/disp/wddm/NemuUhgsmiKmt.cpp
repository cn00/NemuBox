/* $Id: NemuUhgsmiKmt.cpp $ */

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

#include <iprt/mem.h>
#include <iprt/err.h>

#include <cr_protocol.h>

#ifndef NT_SUCCESS
# define NT_SUCCESS(_Status) (((NTSTATUS)(_Status)) >= 0)
#endif


DECLCALLBACK(int) nemuUhgsmiKmtBufferDestroy(PNEMUUHGSMI_BUFFER pBuf)
{
    PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE pBuffer = NEMUUHGSMDXALLOCBASE_GET_BUFFER(pBuf);
    PNEMUUHGSMI_PRIVATE_KMT pPrivate = NEMUUHGSMIKMT_GET(pBuffer->BasePrivate.pHgsmi);

    D3DKMT_DESTROYALLOCATION DdiDealloc;
    DdiDealloc.hDevice = pPrivate->Device.hDevice;
    DdiDealloc.hResource = NULL;
    DdiDealloc.phAllocationList = &pBuffer->hAllocation;
    DdiDealloc.AllocationCount = 1;
    NTSTATUS Status = pPrivate->Callbacks.pfnD3DKMTDestroyAllocation(&DdiDealloc);
    if (NT_SUCCESS(Status))
    {
#ifdef DEBUG_misha
        memset(pBuffer, 0, sizeof(*pBuffer));
#endif
        RTMemFree(pBuffer);
        return VINF_SUCCESS;
    }
    else
    {
        WARN(("pfnD3DKMTDestroyAllocation failed, Status (0x%x)", Status));
    }
    return VERR_GENERAL_FAILURE;
}

DECLCALLBACK(int) nemuUhgsmiKmtBufferLock(PNEMUUHGSMI_BUFFER pBuf, uint32_t offLock, uint32_t cbLock, NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags, void**pvLock)
{
    PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE pBuffer = NEMUUHGSMDXALLOCBASE_GET_BUFFER(pBuf);
    PNEMUUHGSMI_PRIVATE_KMT pPrivate = NEMUUHGSMIKMT_GET(pBuffer->BasePrivate.pHgsmi);
    D3DKMT_LOCK DdiLock = {0};
    DdiLock.hDevice = pPrivate->Device.hDevice;
    DdiLock.hAllocation = pBuffer->hAllocation;
    DdiLock.PrivateDriverData = NULL;

    int rc = nemuUhgsmiBaseDxLockData(pBuffer, offLock, cbLock, fFlags,
                                         &DdiLock.Flags, &DdiLock.NumPages);
    if (!RT_SUCCESS(rc))
    {
        WARN(("nemuUhgsmiBaseDxLockData failed rc %d", rc));
        return rc;
    }


    if (DdiLock.NumPages)
        DdiLock.pPages = pBuffer->aLockPageIndices;
    else
        DdiLock.pPages = NULL;

    NTSTATUS Status = pPrivate->Callbacks.pfnD3DKMTLock(&DdiLock);
    if (NT_SUCCESS(Status))
    {
        *pvLock = (void*)(((uint8_t*)DdiLock.pData) + (offLock & 0xfff));
        return VINF_SUCCESS;
    }
    else
    {
        WARN(("pfnD3DKMTLock failed, Status (0x%x)", Status));
    }

    return VERR_GENERAL_FAILURE;
}

DECLCALLBACK(int) nemuUhgsmiKmtBufferUnlock(PNEMUUHGSMI_BUFFER pBuf)
{
    PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE pBuffer = NEMUUHGSMDXALLOCBASE_GET_BUFFER(pBuf);
    D3DKMT_UNLOCK DdiUnlock;

    PNEMUUHGSMI_PRIVATE_KMT pPrivate = NEMUUHGSMIKMT_GET(pBuffer->BasePrivate.pHgsmi);
    DdiUnlock.hDevice = pPrivate->Device.hDevice;
    DdiUnlock.NumAllocations = 1;
    DdiUnlock.phAllocations = &pBuffer->hAllocation;
    NTSTATUS Status = pPrivate->Callbacks.pfnD3DKMTUnlock(&DdiUnlock);
    if (NT_SUCCESS(Status))
        return VINF_SUCCESS;
    else
        WARN(("pfnD3DKMTUnlock failed, Status (0x%x)", Status));

    return VERR_GENERAL_FAILURE;
}

DECLCALLBACK(int) nemuUhgsmiKmtBufferCreate(PNEMUUHGSMI pHgsmi, uint32_t cbBuf, NEMUUHGSMI_BUFFER_TYPE_FLAGS fType, PNEMUUHGSMI_BUFFER* ppBuf)
{
    if (!cbBuf)
        return VERR_INVALID_PARAMETER;

    int rc = VINF_SUCCESS;

    cbBuf = NEMUWDDM_ROUNDBOUND(cbBuf, 0x1000);
    Assert(cbBuf);
    uint32_t cPages = cbBuf >> 12;
    Assert(cPages);

    PNEMUUHGSMI_PRIVATE_KMT pPrivate = NEMUUHGSMIKMT_GET(pHgsmi);
    PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE pBuf = (PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE)RTMemAllocZ(RT_OFFSETOF(NEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE, aLockPageIndices[cPages]));
    if (!pBuf)
    {
        WARN(("RTMemAllocZ failed"));
        return VERR_NO_MEMORY;
    }

    D3DKMT_CREATEALLOCATION DdiAlloc;
    D3DDDI_ALLOCATIONINFO DdiAllocInfo;
    NEMUWDDM_ALLOCINFO AllocInfo;

    memset(&DdiAlloc, 0, sizeof (DdiAlloc));
    DdiAlloc.hDevice = pPrivate->Device.hDevice;
    DdiAlloc.NumAllocations = 1;
    DdiAlloc.pAllocationInfo = &DdiAllocInfo;

    nemuUhgsmiBaseDxAllocInfoFill(&DdiAllocInfo, &AllocInfo, cbBuf, fType);

    NTSTATUS Status = pPrivate->Callbacks.pfnD3DKMTCreateAllocation(&DdiAlloc);
    if (NT_SUCCESS(Status))
    {
        Assert(DdiAllocInfo.hAllocation);
        pBuf->BasePrivate.Base.pfnLock = nemuUhgsmiKmtBufferLock;
        pBuf->BasePrivate.Base.pfnUnlock = nemuUhgsmiKmtBufferUnlock;
        pBuf->BasePrivate.Base.pfnDestroy = nemuUhgsmiKmtBufferDestroy;

        pBuf->BasePrivate.Base.fType = fType;
        pBuf->BasePrivate.Base.cbBuffer = cbBuf;

        pBuf->BasePrivate.pHgsmi = &pPrivate->BasePrivate;

        pBuf->hAllocation = DdiAllocInfo.hAllocation;


        *ppBuf = &pBuf->BasePrivate.Base;

        return VINF_SUCCESS;
    }
    else
    {
        WARN(("pfnD3DKMTCreateAllocation failes, Status(0x%x)", Status));
        rc = VERR_OUT_OF_RESOURCES;
    }

    RTMemFree(pBuf);

    return rc;
}

DECLCALLBACK(int) nemuUhgsmiKmtBufferSubmit(PNEMUUHGSMI pHgsmi, PNEMUUHGSMI_BUFFER_SUBMIT aBuffers, uint32_t cBuffers)
{
    PNEMUUHGSMI_PRIVATE_KMT pHg = NEMUUHGSMIKMT_GET(pHgsmi);
    UINT cbDmaCmd = pHg->Context.CommandBufferSize;
    int rc = nemuUhgsmiBaseDxDmaFill(aBuffers, cBuffers,
            pHg->Context.pCommandBuffer, &cbDmaCmd,
            pHg->Context.pAllocationList, pHg->Context.AllocationListSize,
            pHg->Context.pPatchLocationList, pHg->Context.PatchLocationListSize);
    if (RT_FAILURE(rc))
    {
        WARN(("nemuUhgsmiBaseDxDmaFill failed, rc %d", rc));
        return rc;
    }

    D3DKMT_RENDER DdiRender = {0};
    DdiRender.hContext = pHg->Context.hContext;
    DdiRender.CommandLength = cbDmaCmd;
    DdiRender.AllocationCount = cBuffers;
    Assert(DdiRender.CommandLength);
    Assert(DdiRender.CommandLength < UINT32_MAX/2);

    NTSTATUS Status = pHg->Callbacks.pfnD3DKMTRender(&DdiRender);
    if (NT_SUCCESS(Status))
    {
        pHg->Context.CommandBufferSize = DdiRender.NewCommandBufferSize;
        pHg->Context.pCommandBuffer = DdiRender.pNewCommandBuffer;
        pHg->Context.AllocationListSize = DdiRender.NewAllocationListSize;
        pHg->Context.pAllocationList = DdiRender.pNewAllocationList;
        pHg->Context.PatchLocationListSize = DdiRender.NewPatchLocationListSize;
        pHg->Context.pPatchLocationList = DdiRender.pNewPatchLocationList;

        return VINF_SUCCESS;
    }
    else
    {
        WARN(("pfnD3DKMTRender failed, Status (0x%x)", Status));
    }

    return VERR_GENERAL_FAILURE;
}


static HRESULT nemuUhgsmiKmtEngineCreate(PNEMUUHGSMI_PRIVATE_KMT pHgsmi, BOOL bD3D)
{
    HRESULT hr = nemuDispKmtCallbacksInit(&pHgsmi->Callbacks);
    if (hr == S_OK)
    {
        hr = nemuDispKmtOpenAdapter(&pHgsmi->Callbacks, &pHgsmi->Adapter);
        if (hr == S_OK)
        {
            hr = nemuDispKmtCreateDevice(&pHgsmi->Adapter, &pHgsmi->Device);
            if (hr == S_OK)
            {
                hr = nemuDispKmtCreateContext(&pHgsmi->Device, &pHgsmi->Context,
                        bD3D ? NEMUWDDM_CONTEXT_TYPE_CUSTOM_UHGSMI_3D : NEMUWDDM_CONTEXT_TYPE_CUSTOM_UHGSMI_GL,
                                CR_PROTOCOL_VERSION_MAJOR, CR_PROTOCOL_VERSION_MINOR,
                                NULL, 0);
                if (hr == S_OK)
                {
                    return S_OK;
                }
                else
                {
                    WARN(("nemuDispKmtCreateContext failed, hr(0x%x)", hr));
                }
                nemuDispKmtDestroyDevice(&pHgsmi->Device);
            }
            else
            {
                WARN(("nemuDispKmtCreateDevice failed, hr(0x%x)", hr));
            }
            nemuDispKmtCloseAdapter(&pHgsmi->Adapter);
        }
        else
        {
//            WARN(("nemuDispKmtOpenAdapter failed, hr(0x%x)", hr));
        }

        nemuDispKmtCallbacksTerm(&pHgsmi->Callbacks);
    }
    else
    {
        WARN(("nemuDispKmtCallbacksInit failed, hr(0x%x)", hr));
    }
    return hr;
}

static DECLCALLBACK(int) nemuCrHhgsmiKmtEscape(struct NEMUUHGSMI_PRIVATE_BASE *pHgsmi, void *pvData, uint32_t cbData, BOOL fHwAccess)
{
    PNEMUUHGSMI_PRIVATE_KMT pPrivate = NEMUUHGSMIKMT_GET(pHgsmi);
    D3DKMT_ESCAPE DdiEscape = {0};
    DdiEscape.hAdapter = pPrivate->Adapter.hAdapter;
    DdiEscape.hDevice = pPrivate->Device.hDevice;
    DdiEscape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    DdiEscape.Flags.HardwareAccess = !!fHwAccess;
    DdiEscape.pPrivateDriverData = pvData;
    DdiEscape.PrivateDriverDataSize = cbData;
    DdiEscape.hContext = pPrivate->Context.hContext;

    NTSTATUS Status = pPrivate->Callbacks.pfnD3DKMTEscape(&DdiEscape);
    if (NT_SUCCESS(Status))
    {
        return VINF_SUCCESS;
    }

    WARN(("pfnD3DKMTEscape failed, Status (0x%x)", Status));
    return VERR_GENERAL_FAILURE;
}

static void nemuUhgsmiKmtSetupCallbacks(PNEMUUHGSMI_PRIVATE_KMT pHgsmi)
{
    pHgsmi->BasePrivate.Base.pfnBufferCreate = nemuUhgsmiKmtBufferCreate;
    pHgsmi->BasePrivate.Base.pfnBufferSubmit = nemuUhgsmiKmtBufferSubmit;
    /* escape is still needed, since Ugfsmi uses it e.g. to query connection id */
    pHgsmi->BasePrivate.pfnEscape = nemuCrHhgsmiKmtEscape;
}

static void nemuUhgsmiKmtEscSetupCallbacks(PNEMUUHGSMI_PRIVATE_KMT pHgsmi)
{
    nemuUhgsmiBaseInit(&pHgsmi->BasePrivate, nemuCrHhgsmiKmtEscape);
}

#if 0
HRESULT nemuUhgsmiKmtCreate(PNEMUUHGSMI_PRIVATE_KMT pHgsmi, BOOL bD3D)
{
    nemuUhgsmiKmtSetupCallbacks(pHgsmi);
    return nemuUhgsmiKmtEngineCreate(pHgsmi, bD3D);
}

HRESULT nemuUhgsmiKmtEscCreate(PNEMUUHGSMI_PRIVATE_KMT pHgsmi, BOOL bD3D)
{
    nemuUhgsmiKmtEscSetupCallbacks(pHgsmi);
    return nemuUhgsmiKmtEngineCreate(pHgsmi, bD3D);
}
#endif

static HRESULT nemuUhgsmiKmtQueryCaps(PNEMUUHGSMI_PRIVATE_KMT pHgsmi, uint32_t *pu32Caps)
{
    NEMUWDDM_QI Query;
    D3DKMT_QUERYADAPTERINFO Info;
    Info.hAdapter = pHgsmi->Adapter.hAdapter;
    Info.Type = KMTQAITYPE_UMDRIVERPRIVATE;
    Info.pPrivateDriverData = &Query;
    Info.PrivateDriverDataSize = sizeof (Query);

    NTSTATUS Status = pHgsmi->Callbacks.pfnD3DKMTQueryAdapterInfo(&Info);
    if (!NT_SUCCESS(Status))
    {
        WARN(("pfnD3DKMTQueryAdapterInfo failed, Status %#x", Status));
        return Status;
    }

    if (Query.u32Version != NEMUVIDEOIF_VERSION)
    {
        WARN(("Version mismatch"));
        return E_FAIL;
    }

    *pu32Caps = Query.u32Nemu3DCaps;

    return S_OK;
}

HRESULT nemuUhgsmiKmtCreate(PNEMUUHGSMI_PRIVATE_KMT pHgsmi, BOOL bD3D)
{
    HRESULT hr = nemuUhgsmiKmtEngineCreate(pHgsmi, bD3D);
    if (!SUCCEEDED(hr))
        return hr;

    uint32_t u32Caps = 0;
    hr = nemuUhgsmiKmtQueryCaps(pHgsmi, &u32Caps);
    if (!SUCCEEDED(hr))
    {
        WARN(("nemuUhgsmiKmtQueryCaps failed hr %#x", hr));
        return hr;
    }

    if (u32Caps & CR_NEMU_CAP_CMDVBVA)
        nemuUhgsmiKmtSetupCallbacks(pHgsmi);
    else
        nemuUhgsmiKmtEscSetupCallbacks(pHgsmi);

    return S_OK;
}

HRESULT nemuUhgsmiKmtDestroy(PNEMUUHGSMI_PRIVATE_KMT pHgsmi)
{
    HRESULT hr = nemuDispKmtDestroyContext(&pHgsmi->Context);
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        hr = nemuDispKmtDestroyDevice(&pHgsmi->Device);
        Assert(hr == S_OK);
        if (hr == S_OK)
        {
            hr = nemuDispKmtCloseAdapter(&pHgsmi->Adapter);
            Assert(hr == S_OK);
            if (hr == S_OK)
            {
                hr = nemuDispKmtCallbacksTerm(&pHgsmi->Callbacks);
                Assert(hr == S_OK);
                if (hr == S_OK)
                {
#ifdef DEBUG_misha
                    memset(pHgsmi, 0, sizeof (*pHgsmi));
#endif
                    return S_OK;
                }
            }
        }
    }
    return hr;
}
