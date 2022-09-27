/** @file
 *
 * Nemu extension to Wine D3D
 *
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___NemuWineEx_h__
#define ___NemuWineEx_h__

typedef enum
{
    NEMUWINEEX_SHRC_STATE_UNDEFINED = 0,
    /* the underlying GL resource can not be used because it can be removed concurrently by other SHRC client */
    NEMUWINEEX_SHRC_STATE_GL_DISABLE,
    /* the given client is requested to delete the underlying GL resource on SHRC termination */
    NEMUWINEEX_SHRC_STATE_GL_DELETE
} NEMUWINEEX_SHRC_STATE;


#ifndef IN_NEMULIBWINE

#define NEMUWINEEX_VERSION 1

#ifndef IN_NEMUWINEEX
# define NEMUWINEEX_DECL(_type)   __declspec(dllimport) _type WINAPI
# else
# define NEMUWINEEX_DECL(_type)  __declspec(dllexport) _type WINAPI
#endif

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_CREATETEXTURE(IDirect3DDevice9Ex *iface,
            UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
            D3DPOOL pool, IDirect3DTexture9 **texture, HANDLE *shared_handle,
            void **pavClientMem);
typedef FNNEMUWINEEXD3DDEV9_CREATETEXTURE *PFNNEMUWINEEXD3DDEV9_CREATETEXTURE;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_CREATECUBETEXTURE(IDirect3DDevice9Ex *iface,
            UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format, 
            D3DPOOL pool, IDirect3DCubeTexture9 **texture, HANDLE *shared_handle,
            void **pavClientMem);
typedef FNNEMUWINEEXD3DDEV9_CREATECUBETEXTURE *PFNNEMUWINEEXD3DDEV9_CREATECUBETEXTURE;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_CREATEVOLUMETEXTURE(IDirect3DDevice9Ex *iface,
            UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT Format, D3DPOOL Pool,
            IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle,
            void **pavClientMem);
typedef FNNEMUWINEEXD3DDEV9_CREATEVOLUMETEXTURE *PFNNEMUWINEEXD3DDEV9_CREATEVOLUMETEXTURE;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_VOLBLT(IDirect3DDevice9Ex *iface,
                                                    IDirect3DVolume9 *pSourceVolume, IDirect3DVolume9 *pDestinationVolume,
                                                    const struct NEMUBOX3D *pSrcBoxArg,
                                                    const struct NEMUPOINT3D *pDstPoin3D);
typedef FNNEMUWINEEXD3DDEV9_VOLBLT *PFNNEMUWINEEXD3DDEV9_VOLBLT;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_VOLTEXBLT(IDirect3DDevice9Ex *iface,
                                                    IDirect3DVolumeTexture9 *pSourceTexture, IDirect3DVolumeTexture9 *pDestinationTexture,
                                                    const struct NEMUBOX3D *pSrcBoxArg,
                                                    const struct NEMUPOINT3D *pDstPoin3D);
typedef FNNEMUWINEEXD3DDEV9_VOLTEXBLT *PFNNEMUWINEEXD3DDEV9_VOLTEXBLT;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_FLUSH(IDirect3DDevice9Ex *iface);
typedef FNNEMUWINEEXD3DDEV9_FLUSH *PFNNEMUWINEEXD3DDEV9_FLUSH;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_FLUSHTOHOST(IDirect3DDevice9Ex *iface);
typedef FNNEMUWINEEXD3DDEV9_FLUSHTOHOST *PFNNEMUWINEEXD3DDEV9_FLUSHTOHOST;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_GETHOSTID(IDirect3DDevice9Ex *iface, int32_t *pi32Id);
typedef FNNEMUWINEEXD3DDEV9_GETHOSTID *PFNNEMUWINEEXD3DDEV9_GETHOSTID;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_FINISH(IDirect3DDevice9Ex *iface);
typedef FNNEMUWINEEXD3DDEV9_FINISH *PFNNEMUWINEEXD3DDEV9_FINISH;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DDEV9_TERM(IDirect3DDevice9Ex *iface);
typedef FNNEMUWINEEXD3DDEV9_TERM *PFNNEMUWINEEXD3DDEV9_TERM;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DSURF9_GETHOSTID(IDirect3DSurface9 *iface, uint32_t *pu32Id);
typedef FNNEMUWINEEXD3DSURF9_GETHOSTID *PFNNEMUWINEEXD3DSURF9_GETHOSTID;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DSURF9_SYNCTOHOST(IDirect3DSurface9 *iface);
typedef FNNEMUWINEEXD3DSURF9_SYNCTOHOST *PFNNEMUWINEEXD3DSURF9_SYNCTOHOST;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DSWAPCHAIN9_PRESENT(IDirect3DSwapChain9 *iface, IDirect3DSurface9 *surf);
typedef FNNEMUWINEEXD3DSWAPCHAIN9_PRESENT *PFNNEMUWINEEXD3DSWAPCHAIN9_PRESENT;

typedef NEMUWINEEX_DECL(HRESULT) FNNEMUWINEEXD3DSWAPCHAIN9_GETHOSTWINID(IDirect3DSwapChain9 *iface, int32_t *pID);
typedef FNNEMUWINEEXD3DSWAPCHAIN9_GETHOSTWINID *PFNNEMUWINEEXD3DSWAPCHAIN9_GETHOSTWINID;

#ifdef __cplusplus
extern "C"
{
#endif
NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9CreateTexture(IDirect3DDevice9Ex *iface,
            UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
            D3DPOOL pool, IDirect3DTexture9 **texture, HANDLE *shared_handle,
            void **pavClientMem); /* <- extension arg to pass in the client memory buffer,
                                 *    applicable ONLY for SYSMEM textures */

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9CreateCubeTexture(IDirect3DDevice9Ex *iface,
            UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format,
            D3DPOOL pool, IDirect3DCubeTexture9 **texture, HANDLE *shared_handle,
            void **pavClientMem); /* <- extension arg to pass in the client memory buffer,
                                 *    applicable ONLY for SYSMEM textures */

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9CreateVolumeTexture(IDirect3DDevice9Ex *iface,
            UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT Format, D3DPOOL Pool,
            IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle,
            void **pavClientMem);

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9VolBlt(IDirect3DDevice9Ex *iface,
                                                    IDirect3DVolume9 *pSourceVolume, IDirect3DVolume9 *pDestinationVolume,
                                                    const struct NEMUBOX3D *pSrcBoxArg,
                                                    const struct NEMUPOINT3D *pDstPoin3D);

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9VolTexBlt(IDirect3DDevice9Ex *iface,
                                                    IDirect3DVolumeTexture9 *pSourceTexture, IDirect3DVolumeTexture9 *pDestinationTexture,
                                                    const struct NEMUBOX3D *pSrcBoxArg,
                                                    const struct NEMUPOINT3D *pDstPoin3D);

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9Flush(IDirect3DDevice9Ex *iface); /* perform glFlush */

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9Finish(IDirect3DDevice9Ex *iface); /* perform glFinish */

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9FlushToHost(IDirect3DDevice9Ex *iface); /* flash data to host */

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9GetHostId(IDirect3DDevice9Ex *iface, int32_t *pi32Id);

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DDev9Term(IDirect3DDevice9Ex *iface);

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DSurf9GetHostId(IDirect3DSurface9 *iface, uint32_t *pu32Id);

/* makes the surface contents to be synched with host,
 * i.e. typically in case wine surface's location is in sysmem, puts it to texture*/
NEMUWINEEX_DECL(HRESULT) NemuWineExD3DSurf9SyncToHost(IDirect3DSurface9 *iface);

/* used for backwards compatibility purposes only with older host versions not supportgin new present mechanism */
NEMUWINEEX_DECL(HRESULT) NemuWineExD3DSwapchain9Present(IDirect3DSwapChain9 *iface,
                                IDirect3DSurface9 *surf); /* use the given surface as a frontbuffer content source */

NEMUWINEEX_DECL(HRESULT) NemuWineExD3DSwapchain9GetHostWinID(IDirect3DSwapChain9 *iface, int32_t *pi32Id);

typedef struct NEMUWINEEX_D3DPRESENT_PARAMETERS
{
    D3DPRESENT_PARAMETERS Base;
    struct NEMUUHGSMI *pHgsmi;
} NEMUWINEEX_D3DPRESENT_PARAMETERS, *PNEMUWINEEX_D3DPRESENT_PARAMETERS;
#ifdef __cplusplus
}
#endif

#endif /* #ifndef IN_NEMULIBWINE */

#endif
