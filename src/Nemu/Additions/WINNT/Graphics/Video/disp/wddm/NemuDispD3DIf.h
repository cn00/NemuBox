/* $Id: NemuDispD3DIf.h $ */

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

#ifndef ___NemuDispD3DIf_h___
#define ___NemuDispD3DIf_h___

/* D3D headers */
#include <iprt/critsect.h>
#include <iprt/semaphore.h>

#       define _InterlockedExchange           _InterlockedExchange_StupidDDKVsCompilerCrap
#       define _InterlockedExchangeAdd        _InterlockedExchangeAdd_StupidDDKVsCompilerCrap
#       define _InterlockedCompareExchange    _InterlockedCompareExchange_StupidDDKVsCompilerCrap
#       define _InterlockedAddLargeStatistic  _InterlockedAddLargeStatistic_StupidDDKVsCompilerCrap
#       define _interlockedbittestandset      _interlockedbittestandset_StupidDDKVsCompilerCrap
#       define _interlockedbittestandreset    _interlockedbittestandreset_StupidDDKVsCompilerCrap
#       define _interlockedbittestandset64    _interlockedbittestandset64_StupidDDKVsCompilerCrap
#       define _interlockedbittestandreset64  _interlockedbittestandreset64_StupidDDKVsCompilerCrap
#       pragma warning(disable : 4163)
#include <D3D9.h>
#       pragma warning(default : 4163)
#       undef  _InterlockedExchange
#       undef  _InterlockedExchangeAdd
#       undef  _InterlockedCompareExchange
#       undef  _InterlockedAddLargeStatistic
#       undef  _interlockedbittestandset
#       undef  _interlockedbittestandreset
#       undef  _interlockedbittestandset64
#       undef  _interlockedbittestandreset64

#include "../../../Wine_new/nemu/NemuWineEx.h"

/* D3D functionality the NEMUDISPD3D provides */
typedef HRESULT WINAPI FNNEMUDISPD3DCREATE9EX(UINT SDKVersion, IDirect3D9Ex **ppD3D);
typedef FNNEMUDISPD3DCREATE9EX *PFNNEMUDISPD3DCREATE9EX;

typedef struct NEMUDISPD3D
{
    /* D3D functionality the NEMUDISPD3D provides */
    PFNNEMUDISPD3DCREATE9EX pfnDirect3DCreate9Ex;

    PFNNEMUWINEEXD3DDEV9_CREATETEXTURE pfnNemuWineExD3DDev9CreateTexture;

    PFNNEMUWINEEXD3DDEV9_CREATECUBETEXTURE pfnNemuWineExD3DDev9CreateCubeTexture;

    PFNNEMUWINEEXD3DDEV9_CREATEVOLUMETEXTURE pfnNemuWineExD3DDev9CreateVolumeTexture;

    PFNNEMUWINEEXD3DDEV9_FLUSH pfnNemuWineExD3DDev9Flush;

    PFNNEMUWINEEXD3DDEV9_VOLBLT pfnNemuWineExD3DDev9VolBlt;

    PFNNEMUWINEEXD3DDEV9_VOLTEXBLT pfnNemuWineExD3DDev9VolTexBlt;

    PFNNEMUWINEEXD3DDEV9_TERM pfnNemuWineExD3DDev9Term;

    PFNNEMUWINEEXD3DSWAPCHAIN9_PRESENT pfnNemuWineExD3DSwapchain9Present;

    PFNNEMUWINEEXD3DDEV9_FLUSHTOHOST pfnNemuWineExD3DDev9FlushToHost;

    PFNNEMUWINEEXD3DDEV9_FINISH pfnNemuWineExD3DDev9Finish;

    PFNNEMUWINEEXD3DSURF9_GETHOSTID pfnNemuWineExD3DSurf9GetHostId;

    PFNNEMUWINEEXD3DSURF9_SYNCTOHOST pfnNemuWineExD3DSurf9SyncToHost;

    PFNNEMUWINEEXD3DSWAPCHAIN9_GETHOSTWINID pfnNemuWineExD3DSwapchain9GetHostWinID;

    PFNNEMUWINEEXD3DDEV9_GETHOSTID pfnNemuWineExD3DDev9GetHostId;

    /* module handle */
    HMODULE hD3DLib;
} NEMUDISPD3D;

typedef struct NEMUWDDMDISP_FORMATS
{
    uint32_t cFormstOps;
    const struct _FORMATOP* paFormstOps;
    uint32_t cSurfDescs;
    struct _DDSURFACEDESC *paSurfDescs;
} NEMUWDDMDISP_FORMATS, *PNEMUWDDMDISP_FORMATS;

typedef struct NEMUWDDMDISP_D3D
{
    NEMUDISPD3D D3D;
    IDirect3D9Ex * pD3D9If;
    D3DCAPS9 Caps;
    UINT cMaxSimRTs;
} NEMUWDDMDISP_D3D, *PNEMUWDDMDISP_D3D;

void NemuDispD3DGlobalInit();
void NemuDispD3DGlobalTerm();
HRESULT NemuDispD3DGlobalOpen(PNEMUWDDMDISP_D3D pD3D, PNEMUWDDMDISP_FORMATS pFormats);
void NemuDispD3DGlobalClose(PNEMUWDDMDISP_D3D pD3D, PNEMUWDDMDISP_FORMATS pFormats);

HRESULT NemuDispD3DOpen(NEMUDISPD3D *pD3D);
void NemuDispD3DClose(NEMUDISPD3D *pD3D);

#ifdef NEMU_WITH_VIDEOHWACCEL
HRESULT NemuDispD3DGlobal2DFormatsInit(struct NEMUWDDMDISP_ADAPTER *pAdapter);
void NemuDispD3DGlobal2DFormatsTerm(struct NEMUWDDMDISP_ADAPTER *pAdapter);
#endif

#endif /* ifndef ___NemuDispD3DIf_h___ */
