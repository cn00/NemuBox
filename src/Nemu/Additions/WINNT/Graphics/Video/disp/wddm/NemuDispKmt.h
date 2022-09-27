/* $Id: NemuDispKmt.h $ */

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

#ifndef ___NemuDispKmt_h__
#define ___NemuDispKmt_h__

#include <D3dkmthk.h>

#include "../../common/wddm/NemuMPIf.h"

/* win8 release preview-specific stuff */
typedef struct _D3DKMT_ADAPTERINFO
{
  D3DKMT_HANDLE hAdapter;
  LUID          AdapterLuid;
  ULONG         NumOfSources;
  BOOL          bPresentMoveRegionsPreferred;
} D3DKMT_ADAPTERINFO;

#define MAX_ENUM_ADAPTERS 16

typedef struct _D3DKMT_ENUMADAPTERS
{
  ULONG              NumAdapters;
  D3DKMT_ADAPTERINFO Adapters[MAX_ENUM_ADAPTERS];
} D3DKMT_ENUMADAPTERS;

typedef NTSTATUS (APIENTRY *PFND3DKMT_ENUMADAPTERS)(IN OUT D3DKMT_ENUMADAPTERS*);

typedef struct _D3DKMT_OPENADAPTERFROMLUID
{
  LUID          AdapterLuid;
  D3DKMT_HANDLE hAdapter;
} D3DKMT_OPENADAPTERFROMLUID;

typedef NTSTATUS (APIENTRY *PFND3DKMT_OPENADAPTERFROMLUID)(IN OUT D3DKMT_OPENADAPTERFROMLUID*);
/* END OF win8 release preview-specific stuff */

typedef enum
{
    NEMUDISPKMT_CALLBACKS_VERSION_UNDEFINED = 0,
    NEMUDISPKMT_CALLBACKS_VERSION_VISTA_WIN7,
    NEMUDISPKMT_CALLBACKS_VERSION_WIN8
} NEMUDISPKMT_CALLBACKS_VERSION;

typedef struct NEMUDISPKMT_CALLBACKS
{
    HMODULE hGdi32;
    NEMUDISPKMT_CALLBACKS_VERSION enmVersion;
    /* open adapter */
    PFND3DKMT_OPENADAPTERFROMHDC pfnD3DKMTOpenAdapterFromHdc;
    PFND3DKMT_OPENADAPTERFROMGDIDISPLAYNAME pfnD3DKMTOpenAdapterFromGdiDisplayName;
    /* close adapter */
    PFND3DKMT_CLOSEADAPTER pfnD3DKMTCloseAdapter;
    /* escape */
    PFND3DKMT_ESCAPE pfnD3DKMTEscape;

    PFND3DKMT_QUERYADAPTERINFO pfnD3DKMTQueryAdapterInfo;

    PFND3DKMT_CREATEDEVICE pfnD3DKMTCreateDevice;
    PFND3DKMT_DESTROYDEVICE pfnD3DKMTDestroyDevice;
    PFND3DKMT_CREATECONTEXT pfnD3DKMTCreateContext;
    PFND3DKMT_DESTROYCONTEXT pfnD3DKMTDestroyContext;

    PFND3DKMT_RENDER pfnD3DKMTRender;

    PFND3DKMT_CREATEALLOCATION pfnD3DKMTCreateAllocation;
    PFND3DKMT_DESTROYALLOCATION pfnD3DKMTDestroyAllocation;

    PFND3DKMT_LOCK pfnD3DKMTLock;
    PFND3DKMT_UNLOCK pfnD3DKMTUnlock;

    /* auto resize support */
    PFND3DKMT_INVALIDATEACTIVEVIDPN pfnD3DKMTInvalidateActiveVidPn;
    PFND3DKMT_POLLDISPLAYCHILDREN pfnD3DKMTPollDisplayChildren;

    /* win8 specifics */
    PFND3DKMT_ENUMADAPTERS pfnD3DKMTEnumAdapters;
    PFND3DKMT_OPENADAPTERFROMLUID pfnD3DKMTOpenAdapterFromLuid;
} NEMUDISPKMT_CALLBACKS, *PNEMUDISPKMT_CALLBACKS;

typedef struct NEMUDISPKMT_ADAPTER
{
    D3DKMT_HANDLE hAdapter;
    HDC hDc;
    LUID Luid;
    const NEMUDISPKMT_CALLBACKS *pCallbacks;
}NEMUDISPKMT_ADAPTER, *PNEMUDISPKMT_ADAPTER;

typedef struct NEMUDISPKMT_DEVICE
{
    struct NEMUDISPKMT_ADAPTER *pAdapter;
    D3DKMT_HANDLE hDevice;
    VOID *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    UINT AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT PatchLocationListSize;
}NEMUDISPKMT_DEVICE, *PNEMUDISPKMT_DEVICE;

typedef struct NEMUDISPKMT_CONTEXT
{
    struct NEMUDISPKMT_DEVICE *pDevice;
    D3DKMT_HANDLE hContext;
    VOID *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    UINT AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT PatchLocationListSize;
} NEMUDISPKMT_CONTEXT, *PNEMUDISPKMT_CONTEXT;

HRESULT nemuDispKmtCallbacksInit(PNEMUDISPKMT_CALLBACKS pCallbacks);
HRESULT nemuDispKmtCallbacksTerm(PNEMUDISPKMT_CALLBACKS pCallbacks);

HRESULT nemuDispKmtOpenAdapter(const NEMUDISPKMT_CALLBACKS *pCallbacks, PNEMUDISPKMT_ADAPTER pAdapter);
HRESULT nemuDispKmtCloseAdapter(PNEMUDISPKMT_ADAPTER pAdapter);
HRESULT nemuDispKmtCreateDevice(PNEMUDISPKMT_ADAPTER pAdapter, PNEMUDISPKMT_DEVICE pDevice);
HRESULT nemuDispKmtDestroyDevice(PNEMUDISPKMT_DEVICE pDevice);
HRESULT nemuDispKmtCreateContext(PNEMUDISPKMT_DEVICE pDevice, PNEMUDISPKMT_CONTEXT pContext,
        NEMUWDDM_CONTEXT_TYPE enmType,
        uint32_t crVersionMajor, uint32_t crVersionMinor,
        HANDLE hEvent, uint64_t u64UmInfo);
HRESULT nemuDispKmtDestroyContext(PNEMUDISPKMT_CONTEXT pContext);


#endif /* #ifndef ___NemuDispKmt_h__ */
