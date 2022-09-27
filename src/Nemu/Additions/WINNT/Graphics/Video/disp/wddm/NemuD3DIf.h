/* $Id: NemuD3DIf.h $ */

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

#ifndef ___NemuDispD3DRcIf_h__
#define ___NemuDispD3DRcIf_h__

#include "NemuDispD3DCmn.h"

static D3DFORMAT nemuDDI2D3DFormat(D3DDDIFORMAT format);
D3DMULTISAMPLE_TYPE nemuDDI2D3DMultiSampleType(D3DDDIMULTISAMPLE_TYPE enmType);
D3DPOOL nemuDDI2D3DPool(D3DDDI_POOL enmPool);
D3DRENDERSTATETYPE nemuDDI2D3DRenderStateType(D3DDDIRENDERSTATETYPE enmType);
NEMUWDDMDISP_TSS_LOOKUP nemuDDI2D3DTestureStageStateType(D3DDDITEXTURESTAGESTATETYPE enmType);
DWORD nemuDDI2D3DUsage(D3DDDI_RESOURCEFLAGS fFlags);
DWORD nemuDDI2D3DLockFlags(D3DDDI_LOCKFLAGS fLockFlags);
D3DTEXTUREFILTERTYPE nemuDDI2D3DBltFlags(D3DDDI_BLTFLAGS fFlags);
D3DQUERYTYPE nemuDDI2D3DQueryType(D3DDDIQUERYTYPE enmType);
DWORD nemuDDI2D3DIssueQueryFlags(D3DDDI_ISSUEQUERYFLAGS Flags);

HRESULT NemuD3DIfCreateForRc(struct NEMUWDDMDISP_RESOURCE *pRc);
HRESULT NemuD3DIfLockRect(struct NEMUWDDMDISP_RESOURCE *pRc, UINT iAlloc,
        D3DLOCKED_RECT * pLockedRect,
        CONST RECT *pRect,
        DWORD fLockFlags);
HRESULT NemuD3DIfUnlockRect(struct NEMUWDDMDISP_RESOURCE *pRc, UINT iAlloc);
void NemuD3DIfLockUnlockMemSynch(struct NEMUWDDMDISP_ALLOCATION *pAlloc, D3DLOCKED_RECT *pLockInfo, RECT *pRect, bool bToLockInfo);

IUnknown* nemuD3DIfCreateSharedPrimary(PNEMUWDDMDISP_ALLOCATION pAlloc);


/* NOTE: does NOT increment a ref counter! NO Release needed!! */
DECLINLINE(IUnknown*) nemuD3DIfGet(PNEMUWDDMDISP_ALLOCATION pAlloc)
{
    if (pAlloc->pD3DIf)
        return pAlloc->pD3DIf;

    if (pAlloc->enmType != NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE)
    {
        WARN(("dynamic creation is supported for NEMUWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE only!, current type is %d", pAlloc->enmType));
        return NULL;
    }

    return nemuD3DIfCreateSharedPrimary(pAlloc);
}

/* on success increments the surface ref counter,
 * i.e. one must call pSurf->Release() once the surface is not needed*/
DECLINLINE(HRESULT) NemuD3DIfSurfGet(PNEMUWDDMDISP_RESOURCE pRc, UINT iAlloc, IDirect3DSurface9 **ppSurf)
{
    HRESULT hr = S_OK;
    Assert(pRc->cAllocations > iAlloc);
    *ppSurf = NULL;
    IUnknown* pD3DIf = nemuD3DIfGet(&pRc->aAllocations[iAlloc]);

    switch (pRc->aAllocations[0].enmD3DIfType)
    {
        case NEMUDISP_D3DIFTYPE_SURFACE:
        {
            IDirect3DSurface9 *pD3DIfSurf = (IDirect3DSurface9*)pD3DIf;
            Assert(pD3DIfSurf);
            pD3DIfSurf->AddRef();
            *ppSurf = pD3DIfSurf;
            break;
        }
        case NEMUDISP_D3DIFTYPE_TEXTURE:
        {
            /* @todo NemuD3DIfSurfGet is typically used in Blt & ColorFill functions
             * in this case, if texture is used as a destination,
             * we should update sub-layers as well which is not done currently. */
            IDirect3DTexture9 *pD3DIfTex = (IDirect3DTexture9*)pD3DIf;
            IDirect3DSurface9 *pSurfaceLevel;
            Assert(pD3DIfTex);
            hr = pD3DIfTex->GetSurfaceLevel(iAlloc, &pSurfaceLevel);
            Assert(hr == S_OK);
            if (hr == S_OK)
            {
                *ppSurf = pSurfaceLevel;
            }
            break;
        }
        case NEMUDISP_D3DIFTYPE_CUBE_TEXTURE:
        {
            IDirect3DCubeTexture9 *pD3DIfCubeTex = (IDirect3DCubeTexture9*)pD3DIf;
            IDirect3DSurface9 *pSurfaceLevel;
            Assert(pD3DIfCubeTex);
            hr = pD3DIfCubeTex->GetCubeMapSurface(NEMUDISP_CUBEMAP_INDEX_TO_FACE(pRc, iAlloc),
                                                  NEMUDISP_CUBEMAP_INDEX_TO_LEVEL(pRc, iAlloc), &pSurfaceLevel);
            Assert(hr == S_OK);
            if (hr == S_OK)
            {
                *ppSurf = pSurfaceLevel;
            }
            break;
        }
        default:
        {
            WARN(("unexpected enmD3DIfType %d", pRc->aAllocations[0].enmD3DIfType));
            hr = E_FAIL;
            break;
        }
    }
    return hr;
}

VOID NemuD3DIfFillPresentParams(D3DPRESENT_PARAMETERS *pParams, PNEMUWDDMDISP_RESOURCE pRc, UINT cRTs);
HRESULT NemuD3DIfDeviceCreateDummy(PNEMUWDDMDISP_DEVICE pDevice);

DECLINLINE(IDirect3DDevice9*) NemuD3DIfDeviceGet(PNEMUWDDMDISP_DEVICE pDevice)
{
    if (pDevice->pDevice9If)
        return pDevice->pDevice9If;

#ifdef NEMUWDDMDISP_DEBUG
    g_NemuVDbgInternalDevice = pDevice;
#endif

    HRESULT hr = NemuD3DIfDeviceCreateDummy(pDevice);
    Assert(hr == S_OK);
    Assert(pDevice->pDevice9If);
    return pDevice->pDevice9If;
}

#define NEMUDISPMODE_IS_3D(_p) (!!((_p)->D3D.pD3D9If))
#ifdef NEMUDISP_EARLYCREATEDEVICE
#define NEMUDISP_D3DEV(_p) (_p)->pDevice9If
#else
#define NEMUDISP_D3DEV(_p) NemuD3DIfDeviceGet(_p)
#endif

#endif /* ___NemuDispD3DRcIf_h__ */
