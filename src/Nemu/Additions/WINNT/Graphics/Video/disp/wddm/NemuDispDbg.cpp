/* $Id: NemuDispDbg.cpp $ */

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

/* @todo: move this to NemuDispD3DCmn.h ? */
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

#include "NemuDispD3DCmn.h"

#include <stdio.h>
#include <stdarg.h>

#include <iprt/asm.h>
#include <iprt/assert.h>

static DWORD g_NemuVDbgFIsModuleNameInited = 0;
static char g_NemuVDbgModuleName[MAX_PATH];

char *nemuVDbgDoGetModuleName()
{
    if (!g_NemuVDbgFIsModuleNameInited)
    {
        DWORD cName = GetModuleFileNameA(NULL, g_NemuVDbgModuleName, RT_ELEMENTS(g_NemuVDbgModuleName));
        if (!cName)
        {
            DWORD winEr = GetLastError();
            WARN(("GetModuleFileNameA failed, winEr %d", winEr));
            return NULL;
        }
        g_NemuVDbgFIsModuleNameInited = TRUE;
    }
    return g_NemuVDbgModuleName;
}

static void nemuDispLogDbgFormatStringV(char * szBuffer, uint32_t cbBuffer, const char * szString, va_list pArgList)
{
    uint32_t cbWritten = sprintf(szBuffer, "['%s' 0x%x.0x%x] Disp: ", nemuVDbgDoGetModuleName(), GetCurrentProcessId(), GetCurrentThreadId());
    if (cbWritten > cbBuffer)
    {
        AssertReleaseFailed();
        return;
    }

    _vsnprintf(szBuffer + cbWritten, cbBuffer - cbWritten, szString, pArgList);
}

#if defined(NEMUWDDMDISP_DEBUG) || defined(NEMU_WDDMDISP_WITH_PROFILE)
LONG g_NemuVDbgFIsDwm = -1;

DWORD g_NemuVDbgPid = 0;

DWORD g_NemuVDbgFLogRel = 1;
# if !defined(NEMUWDDMDISP_DEBUG)
DWORD g_NemuVDbgFLog = 0;
# else
DWORD g_NemuVDbgFLog = 1;
# endif
DWORD g_NemuVDbgFLogFlow = 0;

#endif

#ifdef NEMUWDDMDISP_DEBUG

# ifndef IN_NEMUCRHGSMI
#define NEMUWDDMDISP_DEBUG_DUMP_DEFAULT 0
DWORD g_NemuVDbgFDumpSetTexture = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpDrawPrim = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpTexBlt = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpBlt = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpRtSynch = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpFlush = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpShared = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpLock = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpUnlock = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpPresentEnter = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpPresentLeave = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFDumpScSync = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;

DWORD g_NemuVDbgFBreakShared = NEMUWDDMDISP_DEBUG_DUMP_DEFAULT;
DWORD g_NemuVDbgFBreakDdi = 0;

DWORD g_NemuVDbgFCheckSysMemSync = 0;
DWORD g_NemuVDbgFCheckBlt = 0;
DWORD g_NemuVDbgFCheckTexBlt = 0;
DWORD g_NemuVDbgFCheckScSync = 0;

DWORD g_NemuVDbgFSkipCheckTexBltDwmWndUpdate = 1;

DWORD g_NemuVDbgCfgMaxDirectRts = 3;
DWORD g_NemuVDbgCfgForceDummyDevCreate = 0;

PNEMUWDDMDISP_DEVICE g_NemuVDbgInternalDevice = NULL;
PNEMUWDDMDISP_RESOURCE g_NemuVDbgInternalRc = NULL;

DWORD g_NemuVDbgCfgCreateSwapchainOnDdiOnce = 0;

void nemuDispLogDbgPrintF(char * szString, ...)
{
    char szBuffer[4096] = {0};
    va_list pArgList;
    va_start(pArgList, szString);
    nemuDispLogDbgFormatStringV(szBuffer, sizeof (szBuffer), szString, pArgList);
    va_end(pArgList);

    OutputDebugStringA(szBuffer);
}

VOID nemuVDbgDoPrintDmlCmd(const char* pszDesc, const char* pszCmd)
{
    nemuVDbgPrint(("<?dml?><exec cmd=\"%s\">%s</exec>, ( %s )\n", pszCmd, pszDesc, pszCmd));
}

VOID nemuVDbgDoPrintDumpCmd(const char* pszDesc, const void *pvData, uint32_t width, uint32_t height, uint32_t bpp, uint32_t pitch)
{
    char Cmd[1024];
    sprintf(Cmd, "!vbvdbg.ms 0x%p 0n%d 0n%d 0n%d 0n%d", pvData, width, height, bpp, pitch);
    nemuVDbgDoPrintDmlCmd(pszDesc, Cmd);
}

VOID nemuVDbgDoPrintLopLastCmd(const char* pszDesc)
{
    nemuVDbgDoPrintDmlCmd(pszDesc, "ed @@(&nemuVDbgLoop) 0");
}

typedef struct NEMUVDBG_DUMP_INFO
{
    DWORD fFlags;
    const NEMUWDDMDISP_ALLOCATION *pAlloc;
    IDirect3DResource9 *pD3DRc;
    const RECT *pRect;
} NEMUVDBG_DUMP_INFO, *PNEMUVDBG_DUMP_INFO;

typedef DECLCALLBACK(void) FNNEMUVDBG_CONTENTS_DUMPER(PNEMUVDBG_DUMP_INFO pInfo, BOOLEAN fBreak, void *pvDumper);
typedef FNNEMUVDBG_CONTENTS_DUMPER *PFNNEMUVDBG_CONTENTS_DUMPER;

static VOID nemuVDbgDoDumpSummary(const char * pPrefix, PNEMUVDBG_DUMP_INFO pInfo, const char * pSuffix)
{
    const NEMUWDDMDISP_ALLOCATION *pAlloc = pInfo->pAlloc;
    IDirect3DResource9 *pD3DRc = pInfo->pD3DRc;
    char rectBuf[24];
    if (pInfo->pRect)
        _snprintf(rectBuf, sizeof(rectBuf) / sizeof(rectBuf[0]), "(%d:%d);(%d:%d)",
                pInfo->pRect->left, pInfo->pRect->top,
                pInfo->pRect->right, pInfo->pRect->bottom);
    else
        strcpy(rectBuf, "n/a");

    nemuVDbgPrint(("%s Sh(0x%p), Rc(0x%p), pAlloc(0x%x), pD3DIf(0x%p), Type(%s), Rect(%s), Locks(%d) %s",
                    pPrefix ? pPrefix : "",
                    pAlloc ? pAlloc->pRc->aAllocations[0].hSharedHandle : NULL,
                    pAlloc ? pAlloc->pRc : NULL,
                    pAlloc,
                    pD3DRc,
                    pD3DRc ? nemuDispLogD3DRcType(pD3DRc->GetType()) : "n/a",
                    rectBuf,
                    pAlloc ? pAlloc->LockInfo.cLocks : 0,
                    pSuffix ? pSuffix : ""));
}

VOID nemuVDbgDoDumpPerform(const char * pPrefix, PNEMUVDBG_DUMP_INFO pInfo, const char * pSuffix,
        PFNNEMUVDBG_CONTENTS_DUMPER pfnCd, void *pvCd)
{
    DWORD fFlags = pInfo->fFlags;

    if (!NEMUVDBG_DUMP_TYPE_ENABLED_FOR_INFO(pInfo, fFlags))
        return;

    if (!pInfo->pD3DRc && pInfo->pAlloc)
        pInfo->pD3DRc = (IDirect3DResource9*)pInfo->pAlloc->pD3DIf;

    BOOLEAN bLogOnly = NEMUVDBG_DUMP_TYPE_FLOW_ONLY(fFlags);
    if (bLogOnly || !pfnCd)
    {
        nemuVDbgDoDumpSummary(pPrefix, pInfo, pSuffix);
        if (NEMUVDBG_DUMP_FLAGS_IS_SET(fFlags, NEMUVDBG_DUMP_TYPEF_BREAK_ON_FLOW)
                || (!bLogOnly && NEMUVDBG_DUMP_FLAGS_IS_CLEARED(fFlags, NEMUVDBG_DUMP_TYPEF_DONT_BREAK_ON_CONTENTS)))
            Assert(0);
        return;
    }

    nemuVDbgDoDumpSummary(pPrefix, pInfo, NULL);

    pfnCd(pInfo, NEMUVDBG_DUMP_FLAGS_IS_CLEARED(fFlags, NEMUVDBG_DUMP_TYPEF_DONT_BREAK_ON_CONTENTS), pvCd);

    if (pSuffix && pSuffix[0] != '\0')
        nemuVDbgPrint(("%s", pSuffix));
}

static DECLCALLBACK(void) nemuVDbgAllocRectContentsDumperCb(PNEMUVDBG_DUMP_INFO pInfo, BOOLEAN fBreak, void *pvDumper)
{
    const NEMUWDDMDISP_ALLOCATION *pAlloc = pInfo->pAlloc;
    const RECT *pRect = pInfo->pRect;

    Assert(pAlloc->hAllocation);

    D3DDDICB_LOCK LockData;
    LockData.hAllocation = pAlloc->hAllocation;
    LockData.PrivateDriverData = 0;
    LockData.NumPages = 0;
    LockData.pPages = NULL;
    LockData.pData = NULL; /* out */
    LockData.Flags.Value = 0;
    LockData.Flags.LockEntire =1;
    LockData.Flags.ReadOnly = 1;

    PNEMUWDDMDISP_DEVICE pDevice = pAlloc->pRc->pDevice;

    HRESULT hr = pDevice->RtCallbacks.pfnLockCb(pDevice->hDevice, &LockData);
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        UINT bpp = nemuWddmCalcBitsPerPixel(pAlloc->SurfDesc.format);
        nemuVDbgDoPrintDumpCmd("Surf Info", LockData.pData, pAlloc->SurfDesc.d3dWidth, pAlloc->SurfDesc.height, bpp, pAlloc->SurfDesc.pitch);
        if (pRect)
        {
            Assert(pRect->right > pRect->left);
            Assert(pRect->bottom > pRect->top);
            nemuVDbgDoPrintRect("rect: ", pRect, "\n");
            nemuVDbgDoPrintDumpCmd("Rect Info", ((uint8_t*)LockData.pData) + (pRect->top * pAlloc->SurfDesc.pitch) + ((pRect->left * bpp) >> 3),
                    pRect->right - pRect->left, pRect->bottom - pRect->top, bpp, pAlloc->SurfDesc.pitch);
        }
        Assert(0);

        D3DDDICB_UNLOCK DdiUnlock;

        DdiUnlock.NumAllocations = 1;
        DdiUnlock.phAllocations = &pAlloc->hAllocation;

        hr = pDevice->RtCallbacks.pfnUnlockCb(pDevice->hDevice, &DdiUnlock);
        Assert(hr == S_OK);
    }
}

VOID nemuVDbgDoDumpAllocRect(const char * pPrefix, PNEMUWDDMDISP_ALLOCATION pAlloc, RECT *pRect, const char* pSuffix, DWORD fFlags)
{
    NEMUVDBG_DUMP_INFO Info;
    Info.fFlags = fFlags;
    Info.pAlloc = pAlloc;
    Info.pD3DRc = NULL;
    Info.pRect = pRect;
    nemuVDbgDoDumpPerform(pPrefix, &Info, pSuffix, nemuVDbgAllocRectContentsDumperCb, NULL);
}

static DECLCALLBACK(void) nemuVDbgRcRectContentsDumperCb(PNEMUVDBG_DUMP_INFO pInfo, BOOLEAN fBreak, void *pvDumper)
{
    const NEMUWDDMDISP_ALLOCATION *pAlloc = pInfo->pAlloc;
    IDirect3DResource9 *pD3DRc = pInfo->pD3DRc;
    const RECT *pRect = pInfo->pRect;
    IDirect3DSurface9 *pSurf;
    HRESULT hr = NemuD3DIfSurfGet(pAlloc->pRc, pAlloc->iAlloc, &pSurf);
    if (hr != S_OK)
    {
        WARN(("NemuD3DIfSurfGet failed, hr 0x%x", hr));
        return;
    }

    D3DSURFACE_DESC Desc;
    hr = pSurf->GetDesc(&Desc);
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        D3DLOCKED_RECT Lr;
        hr = pSurf->LockRect(&Lr, NULL, D3DLOCK_READONLY);
        Assert(hr == S_OK);
        if (hr == S_OK)
        {
            UINT bpp = nemuWddmCalcBitsPerPixel((D3DDDIFORMAT)Desc.Format);
            nemuVDbgDoPrintDumpCmd("Surf Info", Lr.pBits, Desc.Width, Desc.Height, bpp, Lr.Pitch);
            if (pRect)
            {
                Assert(pRect->right > pRect->left);
                Assert(pRect->bottom > pRect->top);
                nemuVDbgDoPrintRect("rect: ", pRect, "\n");
                nemuVDbgDoPrintDumpCmd("Rect Info", ((uint8_t*)Lr.pBits) + (pRect->top * Lr.Pitch) + ((pRect->left * bpp) >> 3),
                        pRect->right - pRect->left, pRect->bottom - pRect->top, bpp, Lr.Pitch);
            }

            if (fBreak)
            {
                Assert(0);
            }
            hr = pSurf->UnlockRect();
            Assert(hr == S_OK);
        }
    }

    pSurf->Release();
}

VOID nemuVDbgDoDumpRcRect(const char * pPrefix, PNEMUWDDMDISP_ALLOCATION pAlloc,
        IDirect3DResource9 *pD3DRc, RECT *pRect, const char * pSuffix, DWORD fFlags)
{
    NEMUVDBG_DUMP_INFO Info;
    Info.fFlags = fFlags;
    Info.pAlloc = pAlloc;
    Info.pD3DRc = pD3DRc;
    Info.pRect = pRect;
    nemuVDbgDoDumpPerform(pPrefix, &Info, pSuffix, nemuVDbgRcRectContentsDumperCb, NULL);
}

VOID nemuVDbgDoDumpBb(const char * pPrefix, IDirect3DSwapChain9 *pSwapchainIf, const char * pSuffix, DWORD fFlags)
{
    IDirect3DSurface9 *pBb = NULL;
    HRESULT hr = pSwapchainIf->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBb);
    Assert(hr == S_OK);
    if (FAILED(hr))
    {
        return;
    }

    Assert(pBb);
    nemuVDbgDoDumpRcRect(pPrefix, NULL, pBb, NULL, pSuffix, fFlags);
    pBb->Release();
}

VOID nemuVDbgDoDumpFb(const char * pPrefix, IDirect3DSwapChain9 *pSwapchainIf, const char * pSuffix, DWORD fFlags)
{
    IDirect3DSurface9 *pBb = NULL;
    HRESULT hr = pSwapchainIf->GetBackBuffer(-1, D3DBACKBUFFER_TYPE_MONO, &pBb);
    Assert(hr == S_OK);
    if (FAILED(hr))
    {
        return;
    }

    Assert(pBb);
    nemuVDbgDoDumpRcRect(pPrefix, NULL, pBb, NULL, pSuffix, fFlags);
    pBb->Release();
}


#define NEMUVDBG_STRCASE(_t) \
        case _t: return #_t;
#define NEMUVDBG_STRCASE_UNKNOWN() \
        default: Assert(0); return "Unknown";

const char* nemuVDbgStrCubeFaceType(D3DCUBEMAP_FACES enmFace)
{
    switch (enmFace)
    {
    NEMUVDBG_STRCASE(D3DCUBEMAP_FACE_POSITIVE_X);
    NEMUVDBG_STRCASE(D3DCUBEMAP_FACE_NEGATIVE_X);
    NEMUVDBG_STRCASE(D3DCUBEMAP_FACE_POSITIVE_Y);
    NEMUVDBG_STRCASE(D3DCUBEMAP_FACE_NEGATIVE_Y);
    NEMUVDBG_STRCASE(D3DCUBEMAP_FACE_POSITIVE_Z);
    NEMUVDBG_STRCASE(D3DCUBEMAP_FACE_NEGATIVE_Z);
    NEMUVDBG_STRCASE_UNKNOWN();
    }
}

VOID nemuVDbgDoDumpRt(const char * pPrefix, PNEMUWDDMDISP_DEVICE pDevice, const char * pSuffix, DWORD fFlags)
{
    for (UINT i = 0; i < pDevice->cRTs; ++i)
    {
        IDirect3DSurface9 *pRt;
        PNEMUWDDMDISP_ALLOCATION pAlloc = pDevice->apRTs[i];
        if (!pAlloc) continue;
        IDirect3DDevice9 *pDeviceIf = pDevice->pDevice9If;
        HRESULT hr = pDeviceIf->GetRenderTarget(i, &pRt);
        Assert(hr == S_OK);
        if (hr == S_OK)
        {
//            Assert(pAlloc->pD3DIf == pRt);
            nemuVDbgDoDumpRcRect(pPrefix, pAlloc, NULL, NULL, pSuffix, fFlags);
            pRt->Release();
        }
        else
        {
            nemuVDbgPrint((__FUNCTION__": ERROR getting rt: 0x%x", hr));
        }
    }
}

VOID nemuVDbgDoDumpSamplers(const char * pPrefix, PNEMUWDDMDISP_DEVICE pDevice, const char * pSuffix, DWORD fFlags)
{
    for (UINT i = 0, iSampler = 0; iSampler < pDevice->cSamplerTextures; ++i)
    {
        Assert(i < RT_ELEMENTS(pDevice->aSamplerTextures));
        if (!pDevice->aSamplerTextures[i]) continue;
        PNEMUWDDMDISP_RESOURCE pRc = pDevice->aSamplerTextures[i];
        for (UINT j = 0; j < pRc->cAllocations; ++j)
        {
            PNEMUWDDMDISP_ALLOCATION pAlloc = &pRc->aAllocations[j];
            nemuVDbgDoDumpRcRect(pPrefix, pAlloc, NULL, NULL, pSuffix, fFlags);
        }
        ++iSampler;
    }
}

static DECLCALLBACK(void) nemuVDbgLockUnlockSurfTexContentsDumperCb(PNEMUVDBG_DUMP_INFO pInfo, BOOLEAN fBreak, void *pvDumper)
{
    const NEMUWDDMDISP_ALLOCATION *pAlloc = pInfo->pAlloc;
    const RECT *pRect = pInfo->pRect;
    UINT bpp = nemuWddmCalcBitsPerPixel(pAlloc->SurfDesc.format);
    uint32_t width, height, pitch = 0;
    void *pvData;
    if (pAlloc->LockInfo.fFlags.AreaValid)
    {
        width = pAlloc->LockInfo.Area.left - pAlloc->LockInfo.Area.right;
        height = pAlloc->LockInfo.Area.bottom - pAlloc->LockInfo.Area.top;
    }
    else
    {
        width = pAlloc->SurfDesc.width;
        height = pAlloc->SurfDesc.height;
    }

    if (pAlloc->LockInfo.fFlags.NotifyOnly)
    {
        pitch = pAlloc->SurfDesc.pitch;
        pvData = ((uint8_t*)pAlloc->pvMem) + pitch*pRect->top + ((bpp*pRect->left) >> 3);
    }
    else
    {
        pvData = pAlloc->LockInfo.pvData;
    }

    nemuVDbgDoPrintDumpCmd("Surf Info", pvData, width, height, bpp, pitch);

    if (fBreak)
    {
        Assert(0);
    }
}

VOID nemuVDbgDoDumpLockUnlockSurfTex(const char * pPrefix, const NEMUWDDMDISP_ALLOCATION *pAlloc, const char * pSuffix, DWORD fFlags)
{
    Assert(!pAlloc->hSharedHandle);

    RECT Rect;
    const RECT *pRect;
    Assert(!pAlloc->LockInfo.fFlags.RangeValid);
    Assert(!pAlloc->LockInfo.fFlags.BoxValid);
    if (pAlloc->LockInfo.fFlags.AreaValid)
    {
        pRect = &pAlloc->LockInfo.Area;
    }
    else
    {
        Rect.top = 0;
        Rect.bottom = pAlloc->SurfDesc.height;
        Rect.left = 0;
        Rect.right = pAlloc->SurfDesc.width;
        pRect = &Rect;
    }

    NEMUVDBG_DUMP_INFO Info;
    Info.fFlags = fFlags;
    Info.pAlloc = pAlloc;
    Info.pD3DRc = NULL;
    Info.pRect = pRect;
    nemuVDbgDoDumpPerform(pPrefix, &Info, pSuffix, nemuVDbgLockUnlockSurfTexContentsDumperCb, NULL);
}

VOID nemuVDbgDoDumpLockSurfTex(const char * pPrefix, const D3DDDIARG_LOCK* pData, const char * pSuffix, DWORD fFlags)
{
    const NEMUWDDMDISP_RESOURCE *pRc = (const NEMUWDDMDISP_RESOURCE*)pData->hResource;
    const NEMUWDDMDISP_ALLOCATION *pAlloc = &pRc->aAllocations[pData->SubResourceIndex];
#ifdef NEMUWDDMDISP_DEBUG
    NEMUWDDMDISP_ALLOCATION *pUnconstpAlloc = (NEMUWDDMDISP_ALLOCATION *)pAlloc;
    pUnconstpAlloc->LockInfo.pvData = pData->pSurfData;
#endif
    nemuVDbgDoDumpLockUnlockSurfTex(pPrefix, pAlloc, pSuffix, fFlags);
}

VOID nemuVDbgDoDumpUnlockSurfTex(const char * pPrefix, const D3DDDIARG_UNLOCK* pData, const char * pSuffix, DWORD fFlags)
{
    const NEMUWDDMDISP_RESOURCE *pRc = (const NEMUWDDMDISP_RESOURCE*)pData->hResource;
    const NEMUWDDMDISP_ALLOCATION *pAlloc = &pRc->aAllocations[pData->SubResourceIndex];
    nemuVDbgDoDumpLockUnlockSurfTex(pPrefix, pAlloc, pSuffix, fFlags);
}

BOOL nemuVDbgDoCheckLRects(D3DLOCKED_RECT *pDstLRect, const RECT *pDstRect, D3DLOCKED_RECT *pSrcLRect, const RECT *pSrcRect, DWORD bpp, BOOL fBreakOnMismatch)
{
    LONG DstH, DstW, SrcH, SrcW, DstWBytes;
    BOOL fMatch = FALSE;
    DstH = pDstRect->bottom - pDstRect->top;
    DstW = pDstRect->right - pDstRect->left;
    SrcH = pSrcRect->bottom - pSrcRect->top;
    SrcW = pSrcRect->right - pSrcRect->left;

    DstWBytes = ((DstW * bpp + 7) >> 3);

    if(DstW != SrcW && DstH != SrcH)
    {
        WARN(("stretched comparison not supported!!"));
        return FALSE;
    }

    uint8_t *pDst = (uint8_t*)pDstLRect->pBits;
    uint8_t *pSrc = (uint8_t*)pSrcLRect->pBits;
    for (LONG i = 0; i < DstH; ++i)
    {
        if (!(fMatch = !memcmp(pDst, pSrc, DstWBytes)))
        {
            nemuVDbgPrint(("not match!\n"));
            if (fBreakOnMismatch)
                Assert(0);
            break;
        }
        pDst += pDstLRect->Pitch;
        pSrc += pSrcLRect->Pitch;
    }
    return fMatch;
}

BOOL nemuVDbgDoCheckRectsMatch(const NEMUWDDMDISP_RESOURCE *pDstRc, uint32_t iDstAlloc,
                            const NEMUWDDMDISP_RESOURCE *pSrcRc, uint32_t iSrcAlloc,
                            const RECT *pDstRect,
                            const RECT *pSrcRect,
                            BOOL fBreakOnMismatch)
{
    BOOL fMatch = FALSE;
    RECT DstRect = {0}, SrcRect = {0};
    if (!pDstRect)
    {
        DstRect.left = 0;
        DstRect.right = pDstRc->aAllocations[iDstAlloc].SurfDesc.width;
        DstRect.top = 0;
        DstRect.bottom = pDstRc->aAllocations[iDstAlloc].SurfDesc.height;
        pDstRect = &DstRect;
    }

    if (!pSrcRect)
    {
        SrcRect.left = 0;
        SrcRect.right = pSrcRc->aAllocations[iSrcAlloc].SurfDesc.width;
        SrcRect.top = 0;
        SrcRect.bottom = pSrcRc->aAllocations[iSrcAlloc].SurfDesc.height;
        pSrcRect = &SrcRect;
    }

    if (pDstRc == pSrcRc
            && iDstAlloc == iSrcAlloc)
    {
        if (!memcmp(pDstRect, pSrcRect, sizeof (*pDstRect)))
        {
            nemuVDbgPrint(("matching same rect of one allocation, skipping..\n"));
            return TRUE;
        }
        WARN(("matching different rects of the same allocation, unsupported!"));
        return FALSE;
    }

    if (pDstRc->RcDesc.enmFormat != pSrcRc->RcDesc.enmFormat)
    {
        WARN(("matching different formats, unsupported!"));
        return FALSE;
    }

    DWORD bpp = pDstRc->aAllocations[iDstAlloc].SurfDesc.bpp;
    if (!bpp)
    {
        WARN(("uninited bpp! unsupported!"));
        return FALSE;
    }

    LONG DstH, DstW, SrcH, SrcW;
    DstH = pDstRect->bottom - pDstRect->top;
    DstW = pDstRect->right - pDstRect->left;
    SrcH = pSrcRect->bottom - pSrcRect->top;
    SrcW = pSrcRect->right - pSrcRect->left;

    if(DstW != SrcW && DstH != SrcH)
    {
        WARN(("stretched comparison not supported!!"));
        return FALSE;
    }

    D3DLOCKED_RECT SrcLRect, DstLRect;
    HRESULT hr = NemuD3DIfLockRect((NEMUWDDMDISP_RESOURCE *)pDstRc, iDstAlloc, &DstLRect, pDstRect, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        WARN(("NemuD3DIfLockRect failed, hr(0x%x)", hr));
        return FALSE;
    }

    hr = NemuD3DIfLockRect((NEMUWDDMDISP_RESOURCE *)pSrcRc, iSrcAlloc, &SrcLRect, pSrcRect, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        WARN(("NemuD3DIfLockRect failed, hr(0x%x)", hr));
        hr = NemuD3DIfUnlockRect((NEMUWDDMDISP_RESOURCE *)pDstRc, iDstAlloc);
        return FALSE;
    }

    fMatch = nemuVDbgDoCheckLRects(&DstLRect, pDstRect, &SrcLRect, pSrcRect, bpp, fBreakOnMismatch);

    hr = NemuD3DIfUnlockRect((NEMUWDDMDISP_RESOURCE *)pDstRc, iDstAlloc);
    Assert(hr == S_OK);

    hr = NemuD3DIfUnlockRect((NEMUWDDMDISP_RESOURCE *)pSrcRc, iSrcAlloc);
    Assert(hr == S_OK);

    return fMatch;
}

void nemuVDbgDoPrintAlloc(const char * pPrefix, const NEMUWDDMDISP_RESOURCE *pRc, uint32_t iAlloc, const char * pSuffix)
{
    Assert(pRc->cAllocations > iAlloc);
    const NEMUWDDMDISP_ALLOCATION *pAlloc = &pRc->aAllocations[iAlloc];
    BOOL bPrimary = pRc->RcDesc.fFlags.Primary;
    BOOL bFrontBuf = FALSE;
    if (bPrimary)
    {
        PNEMUWDDMDISP_SWAPCHAIN pSwapchain = nemuWddmSwapchainForAlloc((NEMUWDDMDISP_ALLOCATION *)pAlloc);
        Assert(pSwapchain);
        bFrontBuf = (nemuWddmSwapchainGetFb(pSwapchain)->pAlloc == pAlloc);
    }
    nemuVDbgPrint(("%s d3dWidth(%d), width(%d), height(%d), format(%d), usage(%s), %s", pPrefix,
            pAlloc->SurfDesc.d3dWidth, pAlloc->SurfDesc.width, pAlloc->SurfDesc.height, pAlloc->SurfDesc.format,
            bPrimary ?
                    (bFrontBuf ? "Front Buffer" : "Back Buffer")
                    : "?Everage? Alloc",
            pSuffix));
}

void nemuVDbgDoPrintRect(const char * pPrefix, const RECT *pRect, const char * pSuffix)
{
    nemuVDbgPrint(("%s left(%d), top(%d), right(%d), bottom(%d) %s", pPrefix, pRect->left, pRect->top, pRect->right, pRect->bottom, pSuffix));
}

# endif

static VOID CALLBACK nemuVDbgTimerCb(__in PVOID lpParameter, __in BOOLEAN TimerOrWaitFired)
{
    Assert(0);
}

HRESULT nemuVDbgTimerStart(HANDLE hTimerQueue, HANDLE *phTimer, DWORD msTimeout)
{
    if (!CreateTimerQueueTimer(phTimer, hTimerQueue,
                               nemuVDbgTimerCb,
                               NULL,
                               msTimeout, /* ms*/
                               0,
                               WT_EXECUTEONLYONCE))
    {
        DWORD winEr = GetLastError();
        AssertMsgFailed(("CreateTimerQueueTimer failed, winEr (%d)\n", winEr));
        return E_FAIL;
    }
    return S_OK;
}

HRESULT nemuVDbgTimerStop(HANDLE hTimerQueue, HANDLE hTimer)
{
    if (!DeleteTimerQueueTimer(hTimerQueue, hTimer, NULL))
    {
        DWORD winEr = GetLastError();
        AssertMsg(winEr == ERROR_IO_PENDING, ("DeleteTimerQueueTimer failed, winEr (%d)\n", winEr));
    }
    return S_OK;
}
#endif

#if defined(NEMUWDDMDISP_DEBUG) || defined(NEMU_WDDMDISP_WITH_PROFILE)
BOOL nemuVDbgDoCheckExe(const char * pszName)
{
    char *pszModule = nemuVDbgDoGetModuleName();
    if (!pszModule)
        return FALSE;
    DWORD cbModule, cbName;
    cbModule = strlen(pszModule);
    cbName = strlen(pszName);
    if (cbName > cbModule)
        return FALSE;
    if (_stricmp(pszName, pszModule + (cbModule - cbName)))
        return FALSE;
    return TRUE;
}
#endif

#ifdef NEMUWDDMDISP_DEBUG_VEHANDLER

static PVOID g_NemuWDbgVEHandler = NULL;
LONG WINAPI nemuVDbgVectoredHandler(struct _EXCEPTION_POINTERS *pExceptionInfo)
{
    PEXCEPTION_RECORD pExceptionRecord = pExceptionInfo->ExceptionRecord;
    PCONTEXT pContextRecord = pExceptionInfo->ContextRecord;
    switch (pExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_BREAKPOINT:
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            AssertRelease(0);
            break;
        default:
            break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void nemuVDbgVEHandlerRegister()
{
    Assert(!g_NemuWDbgVEHandler);
    g_NemuWDbgVEHandler = AddVectoredExceptionHandler(1,nemuVDbgVectoredHandler);
    Assert(g_NemuWDbgVEHandler);
}

void nemuVDbgVEHandlerUnregister()
{
    Assert(g_NemuWDbgVEHandler);
    ULONG uResult = RemoveVectoredExceptionHandler(g_NemuWDbgVEHandler);
    Assert(uResult);
    g_NemuWDbgVEHandler = NULL;
}

#endif

#if defined(NEMUWDDMDISP_DEBUG) || defined(LOG_TO_BACKDOOR_DRV)
void nemuDispLogDrvF(char * szString, ...)
{
    char szBuffer[4096] = {0};
    va_list pArgList;
    va_start(pArgList, szString);
    nemuDispLogDbgFormatStringV(szBuffer, sizeof (szBuffer), szString, pArgList);
    va_end(pArgList);

    NemuDispMpLoggerLog(szBuffer);
}
#endif
