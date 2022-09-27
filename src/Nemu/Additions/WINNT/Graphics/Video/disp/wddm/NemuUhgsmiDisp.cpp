/* $Id: NemuUhgsmiDisp.cpp $ */

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

#define NEMUUHGSMID3D_GET_PRIVATE(_p, _t) ((_t*)(((uint8_t*)_p) - RT_OFFSETOF(_t, BasePrivate.Base)))
#define NEMUUHGSMID3D_GET(_p) NEMUUHGSMID3D_GET_PRIVATE(_p, NEMUUHGSMI_PRIVATE_D3D)

#include <iprt/mem.h>
#include <iprt/err.h>

DECLCALLBACK(int) nemuUhgsmiD3DBufferDestroy(PNEMUUHGSMI_BUFFER pBuf)
{
    PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE pBuffer = NEMUUHGSMDXALLOCBASE_GET_BUFFER(pBuf);
    struct NEMUWDDMDISP_DEVICE *pDevice = NEMUUHGSMID3D_GET(pBuffer->BasePrivate.pHgsmi)->pDevice;
    D3DDDICB_DEALLOCATE DdiDealloc;
    DdiDealloc.hResource = 0;
    DdiDealloc.NumAllocations = 1;
    DdiDealloc.HandleList = &pBuffer->hAllocation;
    HRESULT hr = pDevice->RtCallbacks.pfnDeallocateCb(pDevice->hDevice, &DdiDealloc);
    if (hr == S_OK)
    {
#ifdef DEBUG_misha
        memset(pBuffer, 0, sizeof (*pBuffer));
#endif
        RTMemFree(pBuffer);
        return VINF_SUCCESS;
    }

    WARN(("pfnDeallocateCb failed, hr %#x", hr));
    return VERR_GENERAL_FAILURE;
}

/* typedef DECLCALLBACK(int) FNNEMUUHGSMI_BUFFER_LOCK(PNEMUUHGSMI_BUFFER pBuf, uint32_t offLock, uint32_t cbLock, NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags, void**pvLock); */
DECLCALLBACK(int) nemuUhgsmiD3DBufferLock(PNEMUUHGSMI_BUFFER pBuf, uint32_t offLock, uint32_t cbLock, NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags, void**pvLock)
{
    PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE pBuffer = NEMUUHGSMDXALLOCBASE_GET_BUFFER(pBuf);
    struct NEMUWDDMDISP_DEVICE *pDevice = NEMUUHGSMID3D_GET(pBuffer->BasePrivate.pHgsmi)->pDevice;
    D3DDDICB_LOCK DdiLock = {0};
    DdiLock.hAllocation = pBuffer->hAllocation;
    DdiLock.PrivateDriverData = 0;

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

    HRESULT hr = pDevice->RtCallbacks.pfnLockCb(pDevice->hDevice, &DdiLock);
    if (hr == S_OK)
    {
        *pvLock = (void*)(((uint8_t*)DdiLock.pData) + (offLock & 0xfff));
        return VINF_SUCCESS;
    }

    WARN(("pfnLockCb failed, hr %#x", hr));
    return VERR_GENERAL_FAILURE;
}

DECLCALLBACK(int) nemuUhgsmiD3DBufferUnlock(PNEMUUHGSMI_BUFFER pBuf)
{
    PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE pBuffer = NEMUUHGSMDXALLOCBASE_GET_BUFFER(pBuf);
    struct NEMUWDDMDISP_DEVICE *pDevice = NEMUUHGSMID3D_GET(pBuffer->BasePrivate.pHgsmi)->pDevice;
    D3DDDICB_UNLOCK DdiUnlock;
    DdiUnlock.NumAllocations = 1;
    DdiUnlock.phAllocations = &pBuffer->hAllocation;
    HRESULT hr = pDevice->RtCallbacks.pfnUnlockCb(pDevice->hDevice, &DdiUnlock);
    if (hr == S_OK)
        return VINF_SUCCESS;

    WARN(("pfnUnlockCb failed, hr %#x", hr));
    return VERR_GENERAL_FAILURE;
}

/*typedef DECLCALLBACK(int) FNNEMUUHGSMI_BUFFER_CREATE(PNEMUUHGSMI pHgsmi, uint32_t cbBuf, NEMUUHGSMI_BUFFER_TYPE_FLAGS fType, PNEMUUHGSMI_BUFFER* ppBuf);*/
DECLCALLBACK(int) nemuUhgsmiD3DBufferCreate(PNEMUUHGSMI pHgsmi, uint32_t cbBuf, NEMUUHGSMI_BUFFER_TYPE_FLAGS fType, PNEMUUHGSMI_BUFFER* ppBuf)
{
    if (!cbBuf)
        return VERR_INVALID_PARAMETER;

    int rc = VINF_SUCCESS;

    cbBuf = NEMUWDDM_ROUNDBOUND(cbBuf, 0x1000);
    Assert(cbBuf);
    uint32_t cPages = cbBuf >> 12;
    Assert(cPages);

    PNEMUUHGSMI_PRIVATE_D3D pPrivate = NEMUUHGSMID3D_GET(pHgsmi);
    PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE pBuf = (PNEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE)RTMemAllocZ(RT_OFFSETOF(NEMUUHGSMI_BUFFER_PRIVATE_DX_ALLOC_BASE, aLockPageIndices[cPages]));
    if (pBuf)
    {
        D3DDDICB_ALLOCATE DdiAlloc;
        D3DDDI_ALLOCATIONINFO DdiAllocInfo;
        NEMUWDDM_ALLOCINFO AllocInfo;

        memset(&DdiAlloc, 0, sizeof (DdiAlloc));
        DdiAlloc.hResource = NULL;
        DdiAlloc.hKMResource = NULL;
        DdiAlloc.NumAllocations = 1;
        DdiAlloc.pAllocationInfo = &DdiAllocInfo;
        nemuUhgsmiBaseDxAllocInfoFill(&DdiAllocInfo, &AllocInfo, cbBuf, fType);

        HRESULT hr = pPrivate->pDevice->RtCallbacks.pfnAllocateCb(pPrivate->pDevice->hDevice, &DdiAlloc);
        if (hr == S_OK)
        {
            Assert(DdiAllocInfo.hAllocation);
            pBuf->BasePrivate.Base.pfnLock = nemuUhgsmiD3DBufferLock;
            pBuf->BasePrivate.Base.pfnUnlock = nemuUhgsmiD3DBufferUnlock;
            pBuf->BasePrivate.Base.pfnDestroy = nemuUhgsmiD3DBufferDestroy;

            pBuf->BasePrivate.Base.fType = fType;
            pBuf->BasePrivate.Base.cbBuffer = cbBuf;

            pBuf->BasePrivate.pHgsmi = &pPrivate->BasePrivate;

            pBuf->hAllocation = DdiAllocInfo.hAllocation;

            *ppBuf = &pBuf->BasePrivate.Base;

            return VINF_SUCCESS;
        }
        else
        {
            WARN(("pfnAllocateCb failed hr %#x"));
            rc = VERR_GENERAL_FAILURE;
        }

        RTMemFree(pBuf);
    }
    else
    {
        WARN(("RTMemAllocZ failed"));
        rc = VERR_NO_MEMORY;
    }

    return rc;
}

/* typedef DECLCALLBACK(int) FNNEMUUHGSMI_BUFFER_SUBMIT(PNEMUUHGSMI pHgsmi, PNEMUUHGSMI_BUFFER_SUBMIT aBuffers, uint32_t cBuffers); */
DECLCALLBACK(int) nemuUhgsmiD3DBufferSubmit(PNEMUUHGSMI pHgsmi, PNEMUUHGSMI_BUFFER_SUBMIT aBuffers, uint32_t cBuffers)
{
    PNEMUUHGSMI_PRIVATE_D3D pHg = NEMUUHGSMID3D_GET(pHgsmi);
    PNEMUWDDMDISP_DEVICE pDevice = pHg->pDevice;
    UINT cbDmaCmd = pDevice->DefaultContext.ContextInfo.CommandBufferSize;
    int rc = nemuUhgsmiBaseDxDmaFill(aBuffers, cBuffers,
            pDevice->DefaultContext.ContextInfo.pCommandBuffer, &cbDmaCmd,
            pDevice->DefaultContext.ContextInfo.pAllocationList, pDevice->DefaultContext.ContextInfo.AllocationListSize,
            pDevice->DefaultContext.ContextInfo.pPatchLocationList, pDevice->DefaultContext.ContextInfo.PatchLocationListSize);
    if (RT_FAILURE(rc))
    {
        WARN(("nemuUhgsmiBaseDxDmaFill failed, rc %d", rc));
        return rc;
    }

    D3DDDICB_RENDER DdiRender = {0};
    DdiRender.CommandLength = cbDmaCmd;
    Assert(DdiRender.CommandLength);
    Assert(DdiRender.CommandLength < UINT32_MAX/2);
    DdiRender.CommandOffset = 0;
    DdiRender.NumAllocations = cBuffers;
    DdiRender.NumPatchLocations = 0;
//    DdiRender.NewCommandBufferSize = sizeof (NEMUVDMACMD) + 4 * (100);
//    DdiRender.NewAllocationListSize = 100;
//    DdiRender.NewPatchLocationListSize = 100;
    DdiRender.hContext = pDevice->DefaultContext.ContextInfo.hContext;

    HRESULT hr = pDevice->RtCallbacks.pfnRenderCb(pDevice->hDevice, &DdiRender);
    if (hr == S_OK)
    {
        pDevice->DefaultContext.ContextInfo.CommandBufferSize = DdiRender.NewCommandBufferSize;
        pDevice->DefaultContext.ContextInfo.pCommandBuffer = DdiRender.pNewCommandBuffer;
        pDevice->DefaultContext.ContextInfo.AllocationListSize = DdiRender.NewAllocationListSize;
        pDevice->DefaultContext.ContextInfo.pAllocationList = DdiRender.pNewAllocationList;
        pDevice->DefaultContext.ContextInfo.PatchLocationListSize = DdiRender.NewPatchLocationListSize;
        pDevice->DefaultContext.ContextInfo.pPatchLocationList = DdiRender.pNewPatchLocationList;

        return VINF_SUCCESS;
    }

    WARN(("pfnRenderCb failed, hr %#x", hr));
    return VERR_GENERAL_FAILURE;
}

static DECLCALLBACK(int) nemuCrHhgsmiDispEscape(struct NEMUUHGSMI_PRIVATE_BASE *pHgsmi, void *pvData, uint32_t cbData, BOOL fHwAccess)
{
    PNEMUUHGSMI_PRIVATE_D3D pPrivate = NEMUUHGSMID3D_GET(pHgsmi);
    PNEMUWDDMDISP_DEVICE pDevice = pPrivate->pDevice;
    D3DDDICB_ESCAPE DdiEscape = {0};
    DdiEscape.hContext = pDevice->DefaultContext.ContextInfo.hContext;
    DdiEscape.hDevice = pDevice->hDevice;
    DdiEscape.Flags.HardwareAccess = !!fHwAccess;
    DdiEscape.pPrivateDriverData = pvData;
    DdiEscape.PrivateDriverDataSize = cbData;
    HRESULT hr = pDevice->RtCallbacks.pfnEscapeCb(pDevice->pAdapter->hAdapter, &DdiEscape);
    if (SUCCEEDED(hr))
    {
        return VINF_SUCCESS;
    }

    WARN(("pfnEscapeCb failed, hr 0x%x", hr));
    return VERR_GENERAL_FAILURE;
}

void nemuUhgsmiD3DInit(PNEMUUHGSMI_PRIVATE_D3D pHgsmi, PNEMUWDDMDISP_DEVICE pDevice)
{
    pHgsmi->BasePrivate.Base.pfnBufferCreate = nemuUhgsmiD3DBufferCreate;
    pHgsmi->BasePrivate.Base.pfnBufferSubmit = nemuUhgsmiD3DBufferSubmit;
    /* escape is still needed, since Ugfsmi uses it e.g. to query connection id */
    pHgsmi->BasePrivate.pfnEscape = nemuCrHhgsmiDispEscape;
    pHgsmi->pDevice = pDevice;
}

void nemuUhgsmiD3DEscInit(PNEMUUHGSMI_PRIVATE_D3D pHgsmi, struct NEMUWDDMDISP_DEVICE *pDevice)
{
    nemuUhgsmiBaseInit(&pHgsmi->BasePrivate, nemuCrHhgsmiDispEscape);
    pHgsmi->pDevice = pDevice;
}
