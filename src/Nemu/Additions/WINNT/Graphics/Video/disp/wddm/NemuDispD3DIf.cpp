/* $Id: NemuDispD3DIf.cpp $ */

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

#include "NemuDispD3DIf.h"
#include "NemuDispD3DCmn.h"

#include <iprt/assert.h>

/** Convert a given FourCC code to a D3DDDIFORMAT enum. */
#define NEMUWDDM_D3DDDIFORMAT_FROM_FOURCC(_a, _b, _c, _d) \
    ((D3DDDIFORMAT)MAKEFOURCC(_a, _b, _c, _d))

void NemuDispD3DClose(NEMUDISPD3D *pD3D)
{
    FreeLibrary(pD3D->hD3DLib);
    pD3D->hD3DLib = NULL;
}

/**
 * Loads a system DLL.
 *
 * @returns Module handle or NULL
 * @param   pszName             The DLL name.
 */
static HMODULE loadSystemDll(const char *pszName)
{
    char   szPath[MAX_PATH];
    UINT   cchPath = GetSystemDirectoryA(szPath, sizeof(szPath));
    size_t cbName  = strlen(pszName) + 1;
    if (cchPath + 1 + cbName > sizeof(szPath))
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    szPath[cchPath] = '\\';
    memcpy(&szPath[cchPath + 1], pszName, cbName);
    return LoadLibraryA(szPath);
}

HRESULT NemuDispD3DOpen(NEMUDISPD3D *pD3D)
{
#ifdef NEMU_WDDM_WOW64
    pD3D->hD3DLib = loadSystemDll("NemuD3D9wddm-x86.dll");
#else
    pD3D->hD3DLib = loadSystemDll("NemuD3D9wddm.dll");
#endif
    if (!pD3D->hD3DLib)
    {
        DWORD winErr = GetLastError();
        WARN((__FUNCTION__": LoadLibrary failed, winErr = (%d)", winErr));
        return E_FAIL;
    }

    do
    {
        pD3D->pfnDirect3DCreate9Ex = (PFNNEMUDISPD3DCREATE9EX)GetProcAddress(pD3D->hD3DLib, "Direct3DCreate9Ex");
        if (!pD3D->pfnDirect3DCreate9Ex)
        {
            WARN(("no Direct3DCreate9Ex"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9CreateTexture = (PFNNEMUWINEEXD3DDEV9_CREATETEXTURE)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9CreateTexture");
        if (!pD3D->pfnNemuWineExD3DDev9CreateTexture)
        {
            WARN(("no NemuWineExD3DDev9CreateTexture"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9CreateCubeTexture = (PFNNEMUWINEEXD3DDEV9_CREATECUBETEXTURE)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9CreateCubeTexture");
        if (!pD3D->pfnNemuWineExD3DDev9CreateCubeTexture)
        {
            WARN(("no NemuWineExD3DDev9CreateCubeTexture"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9CreateVolumeTexture = (PFNNEMUWINEEXD3DDEV9_CREATEVOLUMETEXTURE)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9CreateVolumeTexture");
        if (!pD3D->pfnNemuWineExD3DDev9CreateVolumeTexture)
        {
            WARN(("no NemuWineExD3DDev9CreateVolumeTexture"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9Flush = (PFNNEMUWINEEXD3DDEV9_FLUSH)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9Flush");
        if (!pD3D->pfnNemuWineExD3DDev9Flush)
        {
            WARN(("no NemuWineExD3DDev9Flush"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9FlushToHost = (PFNNEMUWINEEXD3DDEV9_FLUSHTOHOST)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9FlushToHost");
        if (!pD3D->pfnNemuWineExD3DDev9FlushToHost)
        {
            WARN(("no NemuWineExD3DDev9FlushToHost"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9Finish = (PFNNEMUWINEEXD3DDEV9_FINISH)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9Finish");
        if (!pD3D->pfnNemuWineExD3DDev9Finish)
        {
            WARN(("no NemuWineExD3DDev9Finish"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9VolBlt = (PFNNEMUWINEEXD3DDEV9_VOLBLT)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9VolBlt");
        if (!pD3D->pfnNemuWineExD3DDev9VolBlt)
        {
            WARN(("no NemuWineExD3DDev9VolBlt"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9VolTexBlt = (PFNNEMUWINEEXD3DDEV9_VOLTEXBLT)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9VolTexBlt");
        if (!pD3D->pfnNemuWineExD3DDev9VolTexBlt)
        {
            WARN(("no NemuWineExD3DDev9VolTexBlt"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9Term = (PFNNEMUWINEEXD3DDEV9_TERM)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9Term");
        if (!pD3D->pfnNemuWineExD3DDev9Term)
        {
            WARN(("no NemuWineExD3DDev9Term"));
            break;
        }

        pD3D->pfnNemuWineExD3DSwapchain9Present = (PFNNEMUWINEEXD3DSWAPCHAIN9_PRESENT)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DSwapchain9Present");
        if (!pD3D->pfnNemuWineExD3DSwapchain9Present)
        {
            WARN(("no NemuWineExD3DSwapchain9Present"));
            break;
        }

        pD3D->pfnNemuWineExD3DSurf9GetHostId = (PFNNEMUWINEEXD3DSURF9_GETHOSTID)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DSurf9GetHostId");
        if (!pD3D->pfnNemuWineExD3DSurf9GetHostId)
        {
            WARN(("no NemuWineExD3DSurf9GetHostId"));
            break;
        }

        pD3D->pfnNemuWineExD3DSurf9SyncToHost = (PFNNEMUWINEEXD3DSURF9_SYNCTOHOST)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DSurf9SyncToHost");
        if (!pD3D->pfnNemuWineExD3DSurf9SyncToHost)
        {
            WARN(("no NemuWineExD3DSurf9SyncToHost"));
            break;
        }

        pD3D->pfnNemuWineExD3DSwapchain9GetHostWinID = (PFNNEMUWINEEXD3DSWAPCHAIN9_GETHOSTWINID)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DSwapchain9GetHostWinID");
        if (!pD3D->pfnNemuWineExD3DSwapchain9GetHostWinID)
        {
            WARN(("no NemuWineExD3DSwapchain9GetHostWinID"));
            break;
        }

        pD3D->pfnNemuWineExD3DDev9GetHostId = (PFNNEMUWINEEXD3DDEV9_GETHOSTID)GetProcAddress(pD3D->hD3DLib, "NemuWineExD3DDev9GetHostId");
        if (!pD3D->pfnNemuWineExD3DDev9GetHostId)
        {
            WARN(("no NemuWineExD3DDev9GetHostId"));
            break;
        }

        return S_OK;

    } while (0);

    NemuDispD3DClose(pD3D);

    return E_FAIL;
}



static FORMATOP gNemuFormatOps3D[] = {
    {D3DDDIFMT_A8R8G8B8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_SRGBWRITE|FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_X8R8G8B8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_DISPLAYMODE|FORMATOP_3DACCELERATION|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_SRGBWRITE|FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A2R10G10B10,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_X1R5G5B5,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A1R5G5B5,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A4R4G4B4,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_R5G6B5,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_DISPLAYMODE|FORMATOP_3DACCELERATION|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_L16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A8L8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_L8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_D16,   FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_D24S8, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_D24X8, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_D16_LOCKABLE, FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_X8D24, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_D32F_LOCKABLE, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},
    {D3DDDIFMT_S8D24, FORMATOP_TEXTURE|FORMATOP_ZSTENCIL|FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH, 0, 0, 0},

    {D3DDDIFMT_DXT1,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_DXT2,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_DXT3,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_DXT4,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_DXT5,
        FORMATOP_TEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_X8L8V8U8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        0|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A2W10V10U10,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        0|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_V8U8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        0|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_Q8W8V8U8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        0|
        0|
        FORMATOP_OFFSCREENPLAIN|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_CxV8U8, FORMATOP_NOFILTER|FORMATOP_NOALPHABLEND|FORMATOP_NOTEXCOORDWRAPNORMIP, 0, 0, 0},

    {D3DDDIFMT_R16F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_R32F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_G16R16F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_G32R32F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

// Floating-point formats are not implemented in Chromium.
    {D3DDDIFMT_A16B16G16R16F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A32B32G32R32F,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_G16R16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A16B16G16R16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        0|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_V16U16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        0|
        FORMATOP_BUMPMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_P8, FORMATOP_DISPLAYMODE|FORMATOP_3DACCELERATION|FORMATOP_OFFSCREENPLAIN, 0, 0, 0},

    {D3DDDIFMT_UYVY,
        0|
        0|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_NOFILTER|
        FORMATOP_NOALPHABLEND|
        FORMATOP_NOTEXCOORDWRAPNORMIP, 0, 0, 0},

    {D3DDDIFMT_YUY2,
        0|
        0|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_NOFILTER|
        FORMATOP_NOALPHABLEND|
        FORMATOP_NOTEXCOORDWRAPNORMIP, 0, 0, 0},

    {NEMUWDDM_D3DDDIFORMAT_FROM_FOURCC('Y', 'V', '1', '2'),
        0|
        0|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_NOFILTER|
        FORMATOP_NOALPHABLEND|
        FORMATOP_NOTEXCOORDWRAPNORMIP, 0, 0, 0},

    {D3DDDIFMT_Q16W16V16U16,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_OFFSCREENPLAIN|
        FORMATOP_BUMPMAP|FORMATOP_DMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_X8B8G8R8,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        FORMATOP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|FORMATOP_SRGBREAD|
        FORMATOP_DMAP|FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_SRGBWRITE|FORMATOP_AUTOGENMIPMAP|FORMATOP_VERTEXTEXTURE|
        FORMATOP_OVERLAY, 0, 0, 0},

    {D3DDDIFMT_BINARYBUFFER, FORMATOP_OFFSCREENPLAIN, 0, 0, 0},

    {D3DDDIFMT_A4L4,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|
        0|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_DMAP|
        FORMATOP_VERTEXTEXTURE, 0, 0, 0},

    {D3DDDIFMT_A2B10G10R10,
        FORMATOP_TEXTURE|FORMATOP_VOLUMETEXTURE|FORMATOP_CUBETEXTURE|FORMATOP_OFFSCREEN_RENDERTARGET|
        FORMATOP_SAME_FORMAT_RENDERTARGET|
        0|
        FORMATOP_CONVERT_TO_ARGB|FORMATOP_OFFSCREENPLAIN|
        FORMATOP_DMAP|FORMATOP_MEMBEROFGROUP_ARGB|
        FORMATOP_AUTOGENMIPMAP|FORMATOP_VERTEXTEXTURE, 0, 0, 0},
};

static FORMATOP gNemuFormatOpsBase[] = {
    {D3DDDIFMT_X8R8G8B8, FORMATOP_DISPLAYMODE, 0, 0, 0},

    {D3DDDIFMT_R8G8B8, FORMATOP_DISPLAYMODE, 0, 0, 0},

    {D3DDDIFMT_R5G6B5, FORMATOP_DISPLAYMODE, 0, 0, 0},

    {D3DDDIFMT_P8, FORMATOP_DISPLAYMODE, 0, 0, 0},
};

static DDSURFACEDESC gNemuSurfDescsBase[] = {
        {
            sizeof (DDSURFACEDESC), /*    DWORD   dwSize;  */
            DDSD_CAPS | DDSD_PIXELFORMAT,    /* DWORD dwFlags;    */
            0,    /* DWORD dwHeight;   */
            0,    /* DWORD dwWidth;    */
            {
                0, /* Union             */
                   /*   LONG lPitch; */
                   /*   DWORD dwLinearSize; */
            },
            0,  /*    DWORD dwBackBufferCount; */
            {
                0, /* Union */
                   /*  DWORD dwMipMapCount; */
                   /*    DWORD dwZBufferBitDepth; */
                   /*   DWORD dwRefreshRate; */
            },
            0, /*    DWORD dwAlphaBitDepth; */
            0, /*   DWORD dwReserved; */
            NULL, /*   LPVOID lpSurface; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestBlt; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKSrcOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY ddckCKSrcBlt; */
            {
                sizeof (DDPIXELFORMAT), /* DWORD dwSize; */
                DDPF_RGB, /* DWORD dwFlags; */
                0, /* DWORD dwFourCC; */
                {
                    32, /* union */
                       /* DWORD dwRGBBitCount; */
                       /* DWORD dwYUVBitCount; */
                       /* DWORD dwZBufferBitDepth; */
                       /* DWORD dwAlphaBitDepth; */
                       /* DWORD dwLuminanceBitCount; */
                       /* DWORD dwBumpBitCount; */
                },
                {
                    0xff0000, /* union */
                       /* DWORD dwRBitMask; */
                       /* DWORD dwYBitMask; */
                        /* DWORD dwStencilBitDepth; */
                        /* DWORD dwLuminanceBitMask; */
                        /* DWORD dwBumpDuBitMask; */
                },
                {
                    0xff00,
                        /* DWORD dwGBitMask; */
                        /* DWORD dwUBitMask; */
                        /* DWORD dwZBitMask; */
                        /* DWORD dwBumpDvBitMask; */
                },
                {
                    0xff,
                        /* DWORD dwBBitMask; */
                        /* DWORD dwVBitMask; */
                        /* DWORD dwStencilBitMask; */
                        /* DWORD dwBumpLuminanceBitMask; */
                },
                {
                    0,
                        /* DWORD dwRGBAlphaBitMask; */
                        /* DWORD dwYUVAlphaBitMask; */
                        /* DWORD dwLuminanceAlphaBitMask; */
                        /* DWORD dwRGBZBitMask; */
                        /* DWORD dwYUVZBitMask; */
                },
            }, /* DDPIXELFORMAT ddpfPixelFormat; */
            {
                DDSCAPS_BACKBUFFER
                | DDSCAPS_COMPLEX
                | DDSCAPS_FLIP
                | DDSCAPS_FRONTBUFFER
                | DDSCAPS_LOCALVIDMEM
                | DDSCAPS_PRIMARYSURFACE
                | DDSCAPS_VIDEOMEMORY
                | DDSCAPS_VISIBLE   /* DWORD dwCaps; */
            } /* DDSCAPS ddsCaps; */
        },
        {
            sizeof (DDSURFACEDESC), /*    DWORD   dwSize;  */
            DDSD_CAPS | DDSD_PIXELFORMAT,    /* DWORD dwFlags;    */
            0,    /* DWORD dwHeight;   */
            0,    /* DWORD dwWidth;    */
            {
                0, /* Union             */
                   /*   LONG lPitch; */
                   /*   DWORD dwLinearSize; */
            },
            0,  /*    DWORD dwBackBufferCount; */
            {
                0, /* Union */
                   /*  DWORD dwMipMapCount; */
                   /*    DWORD dwZBufferBitDepth; */
                   /*   DWORD dwRefreshRate; */
            },
            0, /*    DWORD dwAlphaBitDepth; */
            0, /*   DWORD dwReserved; */
            NULL, /*   LPVOID lpSurface; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestBlt; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKSrcOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY ddckCKSrcBlt; */
            {
                sizeof (DDPIXELFORMAT), /* DWORD dwSize; */
                DDPF_RGB, /* DWORD dwFlags; */
                0, /* DWORD dwFourCC; */
                {
                    24, /* union */
                       /* DWORD dwRGBBitCount; */
                       /* DWORD dwYUVBitCount; */
                       /* DWORD dwZBufferBitDepth; */
                       /* DWORD dwAlphaBitDepth; */
                       /* DWORD dwLuminanceBitCount; */
                       /* DWORD dwBumpBitCount; */
                },
                {
                    0xff0000, /* union */
                       /* DWORD dwRBitMask; */
                       /* DWORD dwYBitMask; */
                        /* DWORD dwStencilBitDepth; */
                        /* DWORD dwLuminanceBitMask; */
                        /* DWORD dwBumpDuBitMask; */
                },
                {
                    0xff00,
                        /* DWORD dwGBitMask; */
                        /* DWORD dwUBitMask; */
                        /* DWORD dwZBitMask; */
                        /* DWORD dwBumpDvBitMask; */
                },
                {
                    0xff,
                        /* DWORD dwBBitMask; */
                        /* DWORD dwVBitMask; */
                        /* DWORD dwStencilBitMask; */
                        /* DWORD dwBumpLuminanceBitMask; */
                },
                {
                    0,
                        /* DWORD dwRGBAlphaBitMask; */
                        /* DWORD dwYUVAlphaBitMask; */
                        /* DWORD dwLuminanceAlphaBitMask; */
                        /* DWORD dwRGBZBitMask; */
                        /* DWORD dwYUVZBitMask; */
                },
            }, /* DDPIXELFORMAT ddpfPixelFormat; */
            {
                DDSCAPS_BACKBUFFER
                | DDSCAPS_COMPLEX
                | DDSCAPS_FLIP
                | DDSCAPS_FRONTBUFFER
                | DDSCAPS_LOCALVIDMEM
                | DDSCAPS_PRIMARYSURFACE
                | DDSCAPS_VIDEOMEMORY
                | DDSCAPS_VISIBLE  /* DWORD dwCaps; */
            } /* DDSCAPS ddsCaps; */
        },
        {
            sizeof (DDSURFACEDESC), /*    DWORD   dwSize;  */
            DDSD_CAPS | DDSD_PIXELFORMAT,    /* DWORD dwFlags;    */
            0,    /* DWORD dwHeight;   */
            0,    /* DWORD dwWidth;    */
            {
                0, /* Union             */
                   /*   LONG lPitch; */
                   /*   DWORD dwLinearSize; */
            },
            0,  /*    DWORD dwBackBufferCount; */
            {
                0, /* Union */
                   /*  DWORD dwMipMapCount; */
                   /*    DWORD dwZBufferBitDepth; */
                   /*   DWORD dwRefreshRate; */
            },
            0, /*    DWORD dwAlphaBitDepth; */
            0, /*   DWORD dwReserved; */
            NULL, /*   LPVOID lpSurface; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKDestBlt; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY  ddckCKSrcOverlay; */
            {
                0, /* DWORD dwColorSpaceLowValue; */
                0, /* DWORD dwColorSpaceHighValue; */
            }, /* DDCOLORKEY ddckCKSrcBlt; */
            {
                sizeof (DDPIXELFORMAT), /* DWORD dwSize; */
                DDPF_RGB, /* DWORD dwFlags; */
                0, /* DWORD dwFourCC; */
                {
                    16, /* union */
                       /* DWORD dwRGBBitCount; */
                       /* DWORD dwYUVBitCount; */
                       /* DWORD dwZBufferBitDepth; */
                       /* DWORD dwAlphaBitDepth; */
                       /* DWORD dwLuminanceBitCount; */
                       /* DWORD dwBumpBitCount; */
                },
                {
                    0xf800, /* union */
                       /* DWORD dwRBitMask; */
                       /* DWORD dwYBitMask; */
                        /* DWORD dwStencilBitDepth; */
                        /* DWORD dwLuminanceBitMask; */
                        /* DWORD dwBumpDuBitMask; */
                },
                {
                    0x7e0,
                        /* DWORD dwGBitMask; */
                        /* DWORD dwUBitMask; */
                        /* DWORD dwZBitMask; */
                        /* DWORD dwBumpDvBitMask; */
                },
                {
                    0x1f,
                        /* DWORD dwBBitMask; */
                        /* DWORD dwVBitMask; */
                        /* DWORD dwStencilBitMask; */
                        /* DWORD dwBumpLuminanceBitMask; */
                },
                {
                    0,
                        /* DWORD dwRGBAlphaBitMask; */
                        /* DWORD dwYUVAlphaBitMask; */
                        /* DWORD dwLuminanceAlphaBitMask; */
                        /* DWORD dwRGBZBitMask; */
                        /* DWORD dwYUVZBitMask; */
                },
            }, /* DDPIXELFORMAT ddpfPixelFormat; */
            {
                DDSCAPS_BACKBUFFER
                | DDSCAPS_COMPLEX
                | DDSCAPS_FLIP
                | DDSCAPS_FRONTBUFFER
                | DDSCAPS_LOCALVIDMEM
                | DDSCAPS_PRIMARYSURFACE
                | DDSCAPS_VIDEOMEMORY
                | DDSCAPS_VISIBLE /* DWORD dwCaps; */
            } /* DDSCAPS ddsCaps; */
        },
};

#ifdef NEMU_WITH_VIDEOHWACCEL

static void nemuVhwaPopulateOverlayFourccSurfDesc(DDSURFACEDESC *pDesc, uint32_t fourcc)
{
    memset(pDesc, 0, sizeof (DDSURFACEDESC));

    pDesc->dwSize = sizeof (DDSURFACEDESC);
    pDesc->dwFlags = DDSD_CAPS | DDSD_PIXELFORMAT;
    pDesc->ddpfPixelFormat.dwSize = sizeof (DDPIXELFORMAT);
    pDesc->ddpfPixelFormat.dwFlags = DDPF_FOURCC;
    pDesc->ddpfPixelFormat.dwFourCC = fourcc;
    pDesc->ddsCaps.dwCaps = DDSCAPS_BACKBUFFER
            | DDSCAPS_COMPLEX
            | DDSCAPS_FLIP
            | DDSCAPS_FRONTBUFFER
            | DDSCAPS_LOCALVIDMEM
            | DDSCAPS_OVERLAY
            | DDSCAPS_VIDEOMEMORY
            | DDSCAPS_VISIBLE;
}

static bool nemuPixFormatMatch(DDPIXELFORMAT *pFormat1, DDPIXELFORMAT *pFormat2)
{
    return !memcmp(pFormat1, pFormat2, sizeof (DDPIXELFORMAT));
}

HRESULT nemuSurfDescMerge(DDSURFACEDESC *paDescs, uint32_t *pcDescs, uint32_t cMaxDescs, DDSURFACEDESC *pDesc)
{
    uint32_t cDescs = *pcDescs;

    Assert(cMaxDescs >= cDescs);
    Assert(pDesc->dwFlags == (DDSD_CAPS | DDSD_PIXELFORMAT));
    if (pDesc->dwFlags != (DDSD_CAPS | DDSD_PIXELFORMAT))
        return E_INVALIDARG;

    for (uint32_t i = 0; i < cDescs; ++i)
    {
        DDSURFACEDESC *pCur = &paDescs[i];
        if (nemuPixFormatMatch(&pCur->ddpfPixelFormat, &pDesc->ddpfPixelFormat))
        {
            if (pDesc->dwFlags & DDSD_CAPS)
            {
                pCur->dwFlags |= DDSD_CAPS;
                pCur->ddsCaps.dwCaps |= pDesc->ddsCaps.dwCaps;
            }
            return S_OK;
        }
    }

    if (cMaxDescs > cDescs)
    {
        paDescs[cDescs] = *pDesc;
        ++cDescs;
        *pcDescs = cDescs;
        return VINF_SUCCESS;
    }
    return E_FAIL;
}

HRESULT nemuFormatOpsMerge(FORMATOP *paOps, uint32_t *pcOps, uint32_t cMaxOps, FORMATOP *pOp)
{
    uint32_t cOps = *pcOps;

    Assert(cMaxOps >= cOps);

    for (uint32_t i = 0; i < cOps; ++i)
    {
        FORMATOP *pCur = &paOps[i];
        if (pCur->Format == pOp->Format)
        {
            pCur->Operations |= pOp->Operations;
            Assert(pCur->FlipMsTypes == pOp->FlipMsTypes);
            Assert(pCur->BltMsTypes == pOp->BltMsTypes);
            Assert(pCur->PrivateFormatBitCount == pOp->PrivateFormatBitCount);
            return S_OK;
        }
    }

    if (cMaxOps > cOps)
    {
        paOps[cOps] = *pOp;
        ++cOps;
        *pcOps = cOps;
        return VINF_SUCCESS;
    }
    return E_FAIL;
}

HRESULT NemuDispD3DGlobal2DFormatsInit(PNEMUWDDMDISP_ADAPTER pAdapter)
{
    HRESULT hr = S_OK;
    memset(&pAdapter->D3D, 0, sizeof (pAdapter->D3D));
    memset(&pAdapter->Formats, 0, sizeof (pAdapter->Formats));

    /* just calc the max number of formats */
    uint32_t cFormats = RT_ELEMENTS(gNemuFormatOpsBase);
    uint32_t cSurfDescs = RT_ELEMENTS(gNemuSurfDescsBase);
    uint32_t cOverlayFormats = 0;
    for (uint32_t i = 0; i < pAdapter->cHeads; ++i)
    {
        NEMUDISPVHWA_INFO *pVhwa = &pAdapter->aHeads[i].Vhwa;
        if (pVhwa->Settings.fFlags & NEMUVHWA_F_ENABLED)
        {
            cOverlayFormats += pVhwa->Settings.cFormats;
        }
    }

    cFormats += cOverlayFormats;
    cSurfDescs += cOverlayFormats;

    uint32_t cbFormatOps = cFormats * sizeof (FORMATOP);
    cbFormatOps = (cbFormatOps + 7) & ~3;
    /* ensure the surf descs are 8 byte aligned */
    uint32_t offSurfDescs = (cbFormatOps + 7) & ~3;
    uint32_t cbSurfDescs = cSurfDescs * sizeof (DDSURFACEDESC);
    uint32_t cbBuf = offSurfDescs + cbSurfDescs;
    uint8_t* pvBuf = (uint8_t*)RTMemAllocZ(cbBuf);
    if (pvBuf)
    {
        pAdapter->Formats.paFormstOps = (FORMATOP*)pvBuf;
        memcpy ((void*)pAdapter->Formats.paFormstOps , gNemuFormatOpsBase, sizeof (gNemuFormatOpsBase));
        pAdapter->Formats.cFormstOps = RT_ELEMENTS(gNemuFormatOpsBase);

        FORMATOP fo = {D3DDDIFMT_UNKNOWN, 0, 0, 0, 0};
        for (uint32_t i = 0; i < pAdapter->cHeads; ++i)
        {
            NEMUDISPVHWA_INFO *pVhwa = &pAdapter->aHeads[i].Vhwa;
            if (pVhwa->Settings.fFlags & NEMUVHWA_F_ENABLED)
            {
                for (uint32_t j = 0; j < pVhwa->Settings.cFormats; ++j)
                {
                    fo.Format = pVhwa->Settings.aFormats[j];
                    fo.Operations = FORMATOP_OVERLAY;
                    hr = nemuFormatOpsMerge((FORMATOP *)pAdapter->Formats.paFormstOps, &pAdapter->Formats.cFormstOps, cFormats, &fo);
                    if (FAILED(hr))
                    {
                        WARN(("nemuFormatOpsMerge failed, hr 0x%x", hr));
                    }
                }
            }
        }

        pAdapter->Formats.paSurfDescs = (DDSURFACEDESC*)(pvBuf + offSurfDescs);
        memcpy ((void*)pAdapter->Formats.paSurfDescs , gNemuSurfDescsBase, sizeof (gNemuSurfDescsBase));
        pAdapter->Formats.cSurfDescs = RT_ELEMENTS(gNemuSurfDescsBase);

        DDSURFACEDESC sd;
        for (uint32_t i = 0; i < pAdapter->cHeads; ++i)
        {
            NEMUDISPVHWA_INFO *pVhwa = &pAdapter->aHeads[i].Vhwa;
            if (pVhwa->Settings.fFlags & NEMUVHWA_F_ENABLED)
            {
                for (uint32_t j = 0; j < pVhwa->Settings.cFormats; ++j)
                {
                    uint32_t fourcc = nemuWddmFormatToFourcc(pVhwa->Settings.aFormats[j]);
                    if (fourcc)
                    {
                        nemuVhwaPopulateOverlayFourccSurfDesc(&sd, fourcc);
                        hr = nemuSurfDescMerge((DDSURFACEDESC *)pAdapter->Formats.paSurfDescs, &pAdapter->Formats.cSurfDescs, cSurfDescs, &sd);
                        if (FAILED(hr))
                        {
                            WARN(("nemuFormatOpsMerge failed, hr 0x%x", hr));
                        }
                    }
                }
            }
        }
    }
    else
    {
        WARN(("RTMemAllocZ failed"));
        return E_FAIL;
    }
    return S_OK;
}

void NemuDispD3DGlobal2DFormatsTerm(PNEMUWDDMDISP_ADAPTER pAdapter)
{
    if (pAdapter->Formats.paFormstOps)
        RTMemFree((void *)pAdapter->Formats.paFormstOps);
}

#endif

static CRITICAL_SECTION g_NemuDispD3DGlobalCritSect;
static NEMUWDDMDISP_D3D g_NemuDispD3DGlobalD3D;
static NEMUWDDMDISP_FORMATS g_NemuDispD3DGlobalD3DFormats;
static uint32_t g_cNemuDispD3DGlobalOpens;

void nemuDispD3DGlobalLock()
{
    EnterCriticalSection(&g_NemuDispD3DGlobalCritSect);
}

void nemuDispD3DGlobalUnlock()
{
    LeaveCriticalSection(&g_NemuDispD3DGlobalCritSect);
}

void NemuDispD3DGlobalInit()
{
    g_cNemuDispD3DGlobalOpens = 0;
    InitializeCriticalSection(&g_NemuDispD3DGlobalCritSect);
}

void NemuDispD3DGlobalTerm()
{
    DeleteCriticalSection(&g_NemuDispD3DGlobalCritSect);
}

static void nemuDispD3DGlobalD3DFormatsInit(PNEMUWDDMDISP_FORMATS pFormats)
{
    memset(pFormats, 0, sizeof (*pFormats));
    pFormats->paFormstOps = gNemuFormatOps3D;
    pFormats->cFormstOps = RT_ELEMENTS(gNemuFormatOps3D);
}

#ifndef D3DCAPS2_CANRENDERWINDOWED
#define D3DCAPS2_CANRENDERWINDOWED UINT32_C(0x00080000)
#endif

#ifdef DEBUG
/*
 * Check capabilities reported by wine and log any which are not good enough for a D3D feature level.
 */

#define NEMU_D3D_CHECK_FLAGS(level, field, flags) do { \
        if (((field) & (flags)) != (flags)) \
        { \
            LogRel(("D3D level %s %s flags: 0x%08X -> 0x%08X\n", #level, #field, (field), (flags))); \
        } \
    } while (0)

#define NEMU_D3D_CHECK_VALUE(level, field, value) do { \
        if ((int64_t)(value) >= 0? (field) < (value): (field) > (value)) \
        { \
            LogRel(("D3D level %s %s value: %lld -> %lld\n", #level, #field, (int64_t)(field), (int64_t)(value))); \
        } \
    } while (0)

#define NEMU_D3D_CHECK_VALUE_HEX(level, field, value) do { \
        if ((field) < (value)) \
        { \
            LogRel(("D3D level %s %s value: 0x%08X -> 0x%08X\n", #level, #field, (field), (value))); \
        } \
    } while (0)

static void nemuDispCheckCapsLevel(const D3DCAPS9 *pCaps)
{
    /* Misc. */
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->Caps,                     D3DCAPS_READ_SCANLINE);
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->Caps2,                    D3DCAPS2_CANRENDERWINDOWED | D3DCAPS2_CANSHARERESOURCE);
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->DevCaps,                  D3DDEVCAPS_FLOATTLVERTEX
                                                            /*| D3DDEVCAPS_HWVERTEXBUFFER | D3DDEVCAPS_HWINDEXBUFFER |  D3DDEVCAPS_SUBVOLUMELOCK */);
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->PrimitiveMiscCaps,        D3DPMISCCAPS_INDEPENDENTWRITEMASKS /** @todo needs GL_EXT_draw_buffers2 */
                                                              | D3DPMISCCAPS_FOGINFVF
                                                              | D3DPMISCCAPS_SEPARATEALPHABLEND
                                                              | D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS);
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->RasterCaps,               D3DPRASTERCAPS_SUBPIXEL
                                                              | D3DPRASTERCAPS_STIPPLE
                                                              | D3DPRASTERCAPS_ZBIAS
                                                              | D3DPRASTERCAPS_COLORPERSPECTIVE);
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->TextureCaps,              D3DPTEXTURECAPS_TRANSPARENCY
                                                              | D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE);
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->TextureAddressCaps,       D3DPTADDRESSCAPS_MIRRORONCE); /** @todo needs GL_ARB_texture_mirror_clamp_to_edge */
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->VolumeTextureAddressCaps, D3DPTADDRESSCAPS_MIRRORONCE); /** @todo needs GL_ARB_texture_mirror_clamp_to_edge */
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->StencilCaps,              D3DSTENCILCAPS_TWOSIDED);
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->DeclTypes,                D3DDTCAPS_FLOAT16_2 | D3DDTCAPS_FLOAT16_4); /** @todo both need GL_ARB_half_float_vertex */
    NEMU_D3D_CHECK_FLAGS(misc, pCaps->VertexTextureFilterCaps,  D3DPTFILTERCAPS_MINFPOINT
                                                              | D3DPTFILTERCAPS_MAGFPOINT);
    NEMU_D3D_CHECK_VALUE(misc, pCaps->GuardBandLeft,  -8192.);
    NEMU_D3D_CHECK_VALUE(misc, pCaps->GuardBandTop,   -8192.);
    NEMU_D3D_CHECK_VALUE(misc, pCaps->GuardBandRight,  8192.);
    NEMU_D3D_CHECK_VALUE(misc, pCaps->GuardBandBottom, 8192.);
    NEMU_D3D_CHECK_VALUE(misc, pCaps->VS20Caps.DynamicFlowControlDepth, 24);
    NEMU_D3D_CHECK_VALUE(misc, pCaps->VS20Caps.NumTemps, D3DVS20_MAX_NUMTEMPS);
    NEMU_D3D_CHECK_VALUE(misc, pCaps->PS20Caps.DynamicFlowControlDepth, 24);
    NEMU_D3D_CHECK_VALUE(misc, pCaps->PS20Caps.NumTemps, D3DVS20_MAX_NUMTEMPS);

    /* 9_1 */
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->Caps2,                 D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_FULLSCREENGAMMA);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->PresentationIntervals, D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->PrimitiveMiscCaps,     D3DPMISCCAPS_COLORWRITEENABLE);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->ShadeCaps,             D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_COLORGOURAUDRGB
                                                          | D3DPSHADECAPS_FOGGOURAUD | D3DPSHADECAPS_SPECULARGOURAUDRGB);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->TextureFilterCaps,     D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFPOINT
                                                          | D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFPOINT);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->TextureCaps,           D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_CUBEMAP
                                                          | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_PERSPECTIVE);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->TextureAddressCaps,    D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_INDEPENDENTUV
                                                          | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_WRAP);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->TextureOpCaps,         D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_MODULATE
                                                          | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->SrcBlendCaps,          D3DPBLENDCAPS_INVDESTALPHA | D3DPBLENDCAPS_INVDESTCOLOR
                                                          | D3DPBLENDCAPS_INVSRCALPHA | D3DPBLENDCAPS_ONE
                                                          | D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_ZERO);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->DestBlendCaps,         D3DPBLENDCAPS_ONE | D3DPBLENDCAPS_INVSRCALPHA
                                                          | D3DPBLENDCAPS_INVSRCCOLOR | D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_ZERO);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->StretchRectFilterCaps, D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFPOINT
                                                          | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFPOINT);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->ZCmpCaps,              D3DPCMPCAPS_ALWAYS | D3DPCMPCAPS_LESSEQUAL);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->RasterCaps,            D3DPRASTERCAPS_DEPTHBIAS | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS);
    NEMU_D3D_CHECK_FLAGS(9.1, pCaps->StencilCaps,           D3DSTENCILCAPS_TWOSIDED);

    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxTextureWidth,         2048);
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxTextureHeight,        2048);
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->NumSimultaneousRTs,      1);
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxSimultaneousTextures, 8);
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxTextureBlendStages,   8);
    NEMU_D3D_CHECK_VALUE_HEX(9.1, pCaps->PixelShaderVersion,  D3DPS_VERSION(2,0));
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxPrimitiveCount,       65535);
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxVertexIndex,          65534);
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxVolumeExtent,         256);
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxTextureRepeat,        128); /* Must be zero, or 128, or greater. */
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxAnisotropy,           2);
    NEMU_D3D_CHECK_VALUE(9.1, pCaps->MaxVertexW,              0.f);

    /* 9_2 */
    NEMU_D3D_CHECK_FLAGS(9.2, pCaps->PrimitiveMiscCaps,     D3DPMISCCAPS_SEPARATEALPHABLEND);
    NEMU_D3D_CHECK_FLAGS(9.2, pCaps->DevCaps2,              D3DDEVCAPS2_VERTEXELEMENTSCANSHARESTREAMOFFSET);
    NEMU_D3D_CHECK_FLAGS(9.2, pCaps->TextureAddressCaps,    D3DPTADDRESSCAPS_MIRRORONCE);
    NEMU_D3D_CHECK_FLAGS(9.2, pCaps->VolumeTextureAddressCaps, D3DPTADDRESSCAPS_MIRRORONCE);
    NEMU_D3D_CHECK_VALUE(9.2, pCaps->MaxTextureWidth,         2048);
    NEMU_D3D_CHECK_VALUE(9.2, pCaps->MaxTextureHeight,        2048);
    NEMU_D3D_CHECK_VALUE(9.2, pCaps->MaxTextureRepeat,        2048); /* Must be zero, or 2048, or greater. */
    NEMU_D3D_CHECK_VALUE_HEX(9.2, pCaps->VertexShaderVersion, D3DVS_VERSION(2,0));
    NEMU_D3D_CHECK_VALUE(9.2, pCaps->MaxAnisotropy,           16);
    NEMU_D3D_CHECK_VALUE(9.2, pCaps->MaxPrimitiveCount,       1048575);
    NEMU_D3D_CHECK_VALUE(9.2, pCaps->MaxVertexIndex,          1048575);
    NEMU_D3D_CHECK_VALUE(9.2, pCaps->MaxVertexW,              10000000000.f);

    /* 9_3 */
    NEMU_D3D_CHECK_FLAGS(9.3, pCaps->PS20Caps.Caps,         D3DPS20CAPS_GRADIENTINSTRUCTIONS);
    NEMU_D3D_CHECK_FLAGS(9.3, pCaps->VS20Caps.Caps,         D3DVS20CAPS_PREDICATION);
    NEMU_D3D_CHECK_FLAGS(9.3, pCaps->PrimitiveMiscCaps,     D3DPMISCCAPS_INDEPENDENTWRITEMASKS | D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING);
    NEMU_D3D_CHECK_FLAGS(9.3, pCaps->TextureAddressCaps,    D3DPTADDRESSCAPS_BORDER);
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->MaxTextureWidth,         4096);
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->MaxTextureHeight,        4096);
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->MaxTextureRepeat,        8192); /* Must be zero, or 8192, or greater. */
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->NumSimultaneousRTs,      4);
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->PS20Caps.NumInstructionSlots, 512); /* (Pixel Shader Version 2b) */
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->PS20Caps.NumTemps,       32); /* (Pixel Shader Version 2b) */
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->VS20Caps.NumTemps,       32); /* (Vertex Shader Version 2a) */
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->VS20Caps.StaticFlowControlDepth, 4);
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->MaxVertexShaderConst,    256); /* (Vertex Shader Version 2a); */
    NEMU_D3D_CHECK_VALUE(9.3, pCaps->MaxVertexShader30InstructionSlots, 512);
    NEMU_D3D_CHECK_VALUE_HEX(9.3, pCaps->VertexShaderVersion, D3DVS_VERSION(3,0));

    LogRel(("Capabilities check completed\n"));
}

#undef NEMU_D3D_CHECK_FLAGS
#undef NEMU_D3D_CHECK_VALUE
#undef NEMU_D3D_CHECK_VALUE_HEX

#endif /* DEBUG */

static HRESULT nemuWddmGetD3D9Caps(PNEMUWDDMDISP_D3D pD3D, D3DCAPS9 *pCaps)
{
    HRESULT hr = pD3D->pD3D9If->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pCaps);
    if (FAILED(hr))
    {
        WARN(("GetDeviceCaps failed hr(0x%x)",hr));
        return hr;
    }

#if DEBUG
    nemuDispCheckCapsLevel(pCaps);
#endif

    /* needed for Windows Media Player to work properly */
    pCaps->Caps |= D3DCAPS_READ_SCANLINE;
    pCaps->Caps2 |= 0x00080000 /*D3DCAPS2_CANRENDERWINDOWED*/;
    pCaps->Caps2 |= D3DCAPS2_CANSHARERESOURCE;
    pCaps->DevCaps |= D3DDEVCAPS_FLOATTLVERTEX /* <- must be set according to the docs */
            /*| D3DDEVCAPS_HWVERTEXBUFFER | D3DDEVCAPS_HWINDEXBUFFER |  D3DDEVCAPS_SUBVOLUMELOCK */;
    pCaps->PrimitiveMiscCaps |= D3DPMISCCAPS_INDEPENDENTWRITEMASKS
            | D3DPMISCCAPS_FOGINFVF
            | D3DPMISCCAPS_SEPARATEALPHABLEND | D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS;
    pCaps->RasterCaps |= D3DPRASTERCAPS_SUBPIXEL | D3DPRASTERCAPS_STIPPLE | D3DPRASTERCAPS_ZBIAS | D3DPRASTERCAPS_COLORPERSPECTIVE /* keep */;
    pCaps->TextureCaps |= D3DPTEXTURECAPS_TRANSPARENCY | D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE;
    pCaps->TextureAddressCaps |= D3DPTADDRESSCAPS_MIRRORONCE;
    pCaps->VolumeTextureAddressCaps |= D3DPTADDRESSCAPS_MIRRORONCE;
    pCaps->StencilCaps |= D3DSTENCILCAPS_TWOSIDED;
    pCaps->DeclTypes |= D3DDTCAPS_FLOAT16_2 | D3DDTCAPS_FLOAT16_4;
    pCaps->VertexTextureFilterCaps |= D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MAGFPOINT;
    pCaps->GuardBandLeft = -8192.;
    pCaps->GuardBandTop = -8192.;
    pCaps->GuardBandRight = 8192.;
    pCaps->GuardBandBottom = 8192.;
    pCaps->VS20Caps.DynamicFlowControlDepth = 24;
    pCaps->VS20Caps.NumTemps = D3DVS20_MAX_NUMTEMPS;
    pCaps->PS20Caps.DynamicFlowControlDepth = 24;
    pCaps->PS20Caps.NumTemps = D3DVS20_MAX_NUMTEMPS;

    /* workaround for wine not returning InstructionSlots correctly for  shaders v3.0 */
    if ((pCaps->VertexShaderVersion & 0xff00) == 0x0300)
    {
        pCaps->MaxVertexShader30InstructionSlots = RT_MIN(32768, pCaps->MaxVertexShader30InstructionSlots);
        pCaps->MaxPixelShader30InstructionSlots = RT_MIN(32768, pCaps->MaxPixelShader30InstructionSlots);
    }
#if defined(DEBUG)
    if ((pCaps->VertexShaderVersion & 0xff00) == 0x0300)
    {
        Assert(pCaps->MaxVertexShader30InstructionSlots >= 512);
        Assert(pCaps->MaxVertexShader30InstructionSlots <= 32768);
        Assert(pCaps->MaxPixelShader30InstructionSlots >= 512);
        Assert(pCaps->MaxPixelShader30InstructionSlots <= 32768);
    }
    else if ((pCaps->VertexShaderVersion & 0xff00) == 0x0200)
    {
        Assert(pCaps->MaxVertexShader30InstructionSlots == 0);
        Assert(pCaps->MaxPixelShader30InstructionSlots == 0);
    }
    else
    {
        WARN(("incorect shader caps!"));
    }
#endif

    pCaps->MaxVertexW = 10000000000.f; /* Required by D3D feature level 9.3. */

#if DEBUG
    nemuDispCheckCapsLevel(pCaps);
#endif

    nemuDispDumpD3DCAPS9(pCaps);

    return S_OK;
}

static void nemuDispD3DGlobalDoClose(PNEMUWDDMDISP_D3D pD3D)
{
    pD3D->pD3D9If->Release();
    NemuDispD3DClose(&pD3D->D3D);
}

static HRESULT nemuDispD3DGlobalDoOpen(PNEMUWDDMDISP_D3D pD3D)
{
    memset(pD3D, 0, sizeof (*pD3D));
    HRESULT hr = NemuDispD3DOpen(&pD3D->D3D);
    if (SUCCEEDED(hr))
    {
        hr = pD3D->D3D.pfnDirect3DCreate9Ex(D3D_SDK_VERSION, &pD3D->pD3D9If);
        if (SUCCEEDED(hr))
        {
            hr = nemuWddmGetD3D9Caps(pD3D, &pD3D->Caps);
            if (SUCCEEDED(hr))
            {
                pD3D->cMaxSimRTs = pD3D->Caps.NumSimultaneousRTs;
                Assert(pD3D->cMaxSimRTs);
                Assert(pD3D->cMaxSimRTs < UINT32_MAX/2);
                LOG(("SUCCESS 3D Enabled, pD3D (0x%p)", pD3D));
                return S_OK;
            }
            else
            {
                WARN(("nemuWddmGetD3D9Caps failed hr = 0x%x", hr));
            }
            pD3D->pD3D9If->Release();
        }
        else
        {
            WARN(("pfnDirect3DCreate9Ex failed hr = 0x%x", hr));
        }
        NemuDispD3DClose(&pD3D->D3D);
    }
    else
    {
        WARN(("NemuDispD3DOpen failed hr = 0x%x", hr));
    }
    return hr;
}

HRESULT NemuDispD3DGlobalOpen(PNEMUWDDMDISP_D3D pD3D, PNEMUWDDMDISP_FORMATS pFormats)
{
    nemuDispD3DGlobalLock();
    if (!g_cNemuDispD3DGlobalOpens)
    {
        HRESULT hr = nemuDispD3DGlobalDoOpen(&g_NemuDispD3DGlobalD3D);
        if (!SUCCEEDED(hr))
        {
            nemuDispD3DGlobalUnlock();
            WARN(("nemuDispD3DGlobalDoOpen failed hr = 0x%x", hr));
            return hr;
        }

        nemuDispD3DGlobalD3DFormatsInit(&g_NemuDispD3DGlobalD3DFormats);
    }
    ++g_cNemuDispD3DGlobalOpens;
    nemuDispD3DGlobalUnlock();

    *pD3D = g_NemuDispD3DGlobalD3D;
    *pFormats = g_NemuDispD3DGlobalD3DFormats;
    return S_OK;
}

void NemuDispD3DGlobalClose(PNEMUWDDMDISP_D3D pD3D, PNEMUWDDMDISP_FORMATS pFormats)
{
    nemuDispD3DGlobalLock();
    --g_cNemuDispD3DGlobalOpens;
    if (!g_cNemuDispD3DGlobalOpens)
    {
        nemuDispD3DGlobalDoClose(&g_NemuDispD3DGlobalD3D);
    }
    nemuDispD3DGlobalUnlock();
}
