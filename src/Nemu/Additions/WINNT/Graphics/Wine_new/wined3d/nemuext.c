/* $Id: nemuext.c $ */
/** @file
 *
 * Nemu extension to Wine D3D
 *
 * Copyright (C) 2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#include "config.h"
#include "wine/port.h"
#include "wined3d_private.h"
#include "nemuext.h"
#ifdef NEMU_WITH_WDDM
#include <Nemu/NemuCrHgsmi.h>
#include <iprt/err.h>
#endif

WINE_DEFAULT_DEBUG_CHANNEL(d3d_nemu);

typedef DECLCALLBACK(void) FNNEMUEXTWORKERCB(void *pvUser);
typedef FNNEMUEXTWORKERCB *PFNNEMUEXTWORKERCB;

HRESULT NemuExtDwSubmitProcSync(PFNNEMUEXTWORKERCB pfnCb, void *pvCb);
HRESULT NemuExtDwSubmitProcAsync(PFNNEMUEXTWORKERCB pfnCb, void *pvCb);

/*******************************/
#ifdef NEMU_WITH_WDDM
# if defined(NEMU_WDDM_WOW64)
# define NEMUEXT_WINE_MODULE_NAME "wined3dwddm-x86.dll"
# else
# define NEMUEXT_WINE_MODULE_NAME "wined3dwddm.dll"
# endif
#else
/* both 32bit and 64bit versions of xpdm wine libs are named identically */
# define NEMUEXT_WINE_MODULE_NAME "wined3d.dll"
#endif

typedef struct NEMUEXT_WORKER
{
    CRITICAL_SECTION CritSect;

    HANDLE hEvent;

    HANDLE hThread;
    DWORD  idThread;
    /* wine does not seem to guarantie the dll is not unloaded in case FreeLibrary is used
     * while d3d object is not terminated, keep an extra reference to ensure we're not unloaded
     * while we are active */
    HMODULE hSelf;
} NEMUEXT_WORKER, *PNEMUEXT_WORKER;



HRESULT NemuExtWorkerCreate(PNEMUEXT_WORKER pWorker);
HRESULT NemuExtWorkerDestroy(PNEMUEXT_WORKER pWorker);
HRESULT NemuExtWorkerSubmitProc(PNEMUEXT_WORKER pWorker, PFNNEMUEXTWORKERCB pfnCb, void *pvCb);


/*******************************/
typedef struct NEMUEXT_GLOBAL
{
    NEMUEXT_WORKER Worker;
} NEMUEXT_GLOBAL, *PNEMUEXT_GLOBAL;

static NEMUEXT_GLOBAL g_NemuExtGlobal;

#define WM_NEMUEXT_CALLPROC  (WM_APP+1)
#define WM_NEMUEXT_INIT_QUIT (WM_APP+2)

typedef struct NEMUEXT_CALLPROC
{
    PFNNEMUEXTWORKERCB pfnCb;
    void *pvCb;
} NEMUEXT_CALLPROC, *PNEMUEXT_CALLPROC;

static DWORD WINAPI nemuExtWorkerThread(void *pvUser)
{
    PNEMUEXT_WORKER pWorker = (PNEMUEXT_WORKER)pvUser;
    MSG Msg;

    PeekMessage(&Msg,
        NULL /* HWND hWnd */,
        WM_USER /* UINT wMsgFilterMin */,
        WM_USER /* UINT wMsgFilterMax */,
        PM_NOREMOVE);
    SetEvent(pWorker->hEvent);

    do
    {
        BOOL bResult = GetMessage(&Msg,
            0 /*HWND hWnd*/,
            0 /*UINT wMsgFilterMin*/,
            0 /*UINT wMsgFilterMax*/
            );

        if(!bResult) /* WM_QUIT was posted */
            break;

        Assert(bResult != -1);
        if(bResult == -1) /* error occurred */
            break;

        switch (Msg.message)
        {
            case WM_NEMUEXT_CALLPROC:
            {
                NEMUEXT_CALLPROC* pData = (NEMUEXT_CALLPROC*)Msg.lParam;
                pData->pfnCb(pData->pvCb);
                SetEvent(pWorker->hEvent);
                break;
            }
            case WM_NEMUEXT_INIT_QUIT:
            case WM_CLOSE:
            {
                PostQuitMessage(0);
                break;
            }
            default:
                TranslateMessage(&Msg);
                DispatchMessage(&Msg);
        }
    } while (1);
    return 0;
}

HRESULT NemuExtWorkerCreate(PNEMUEXT_WORKER pWorker)
{
    if(!GetModuleHandleEx(0, NEMUEXT_WINE_MODULE_NAME, &pWorker->hSelf))
    {
        DWORD dwEr = GetLastError();
        ERR("GetModuleHandleEx failed, %d", dwEr);
        return E_FAIL;
    }

    InitializeCriticalSection(&pWorker->CritSect);
    pWorker->hEvent = CreateEvent(NULL, /* LPSECURITY_ATTRIBUTES lpEventAttributes */
            FALSE, /* BOOL bManualReset */
            FALSE, /* BOOL bInitialState */
            NULL /* LPCTSTR lpName */
          );
    if (pWorker->hEvent)
    {
        pWorker->hThread = CreateThread(
                              NULL /* LPSECURITY_ATTRIBUTES lpThreadAttributes */,
                              0 /* SIZE_T dwStackSize */,
                              nemuExtWorkerThread,
                              pWorker,
                              0 /* DWORD dwCreationFlags */,
                              &pWorker->idThread);
        if (pWorker->hThread)
        {
            DWORD dwResult = WaitForSingleObject(pWorker->hEvent, INFINITE);
            if (WAIT_OBJECT_0 == dwResult)
                return S_OK;
            ERR("WaitForSingleObject returned %d\n", dwResult);
        }
        else
        {
            DWORD winErr = GetLastError();
            ERR("CreateThread failed, winErr = (%d)", winErr);
        }

        DeleteCriticalSection(&pWorker->CritSect);
    }
    else
    {
        DWORD winErr = GetLastError();
        ERR("CreateEvent failed, winErr = (%d)", winErr);
    }

    FreeLibrary(pWorker->hSelf);

    return E_FAIL;
}

HRESULT NemuExtWorkerDestroy(PNEMUEXT_WORKER pWorker)
{
    BOOL bResult = PostThreadMessage(pWorker->idThread, WM_NEMUEXT_INIT_QUIT, 0, 0);
    DWORD dwErr;
    if (!bResult)
    {
        DWORD winErr = GetLastError();
        ERR("PostThreadMessage failed, winErr = (%d)", winErr);
        return E_FAIL;
    }

    dwErr = WaitForSingleObject(pWorker->hThread, INFINITE);
    if (dwErr != WAIT_OBJECT_0)
    {
        ERR("WaitForSingleObject returned (%d)", dwErr);
        return E_FAIL;
    }

    CloseHandle(pWorker->hEvent);
    DeleteCriticalSection(&pWorker->CritSect);

    FreeLibrary(pWorker->hSelf);

    CloseHandle(pWorker->hThread);

    return S_OK;
}

static HRESULT nemuExtWorkerSubmit(NEMUEXT_WORKER *pWorker, UINT Msg, LPARAM lParam, BOOL fSync)
{
    HRESULT hr = E_FAIL;
    BOOL bResult;
    /* need to serialize since nemuExtWorkerThread is using one pWorker->hEvent
     * to signal job completion */
    EnterCriticalSection(&pWorker->CritSect);
    bResult = PostThreadMessage(pWorker->idThread, Msg, 0, lParam);
    if (bResult)
    {
        if (fSync)
        {
            DWORD dwErr = WaitForSingleObject(pWorker->hEvent, INFINITE);
            if (dwErr == WAIT_OBJECT_0)
            {
                hr = S_OK;
            }
            else
            {
                ERR("WaitForSingleObject returned (%d)", dwErr);
            }
        }
        else
            hr = S_OK;
    }
    else
    {
        DWORD winErr = GetLastError();
        ERR("PostThreadMessage failed, winErr = (%d)", winErr);
        return E_FAIL;
    }
    LeaveCriticalSection(&pWorker->CritSect);
    return hr;
}

HRESULT NemuExtWorkerSubmitProcSync(PNEMUEXT_WORKER pWorker, PFNNEMUEXTWORKERCB pfnCb, void *pvCb)
{
    NEMUEXT_CALLPROC Ctx;
    Ctx.pfnCb = pfnCb;
    Ctx.pvCb = pvCb;
    return nemuExtWorkerSubmit(pWorker, WM_NEMUEXT_CALLPROC, (LPARAM)&Ctx, TRUE);
}

static DECLCALLBACK(void) nemuExtWorkerSubmitProcAsyncWorker(void *pvUser)
{
    PNEMUEXT_CALLPROC pCallInfo = (PNEMUEXT_CALLPROC)pvUser;
    pCallInfo[1].pfnCb(pCallInfo[1].pvCb);
    HeapFree(GetProcessHeap(), 0, pCallInfo);
}

HRESULT NemuExtWorkerSubmitProcAsync(PNEMUEXT_WORKER pWorker, PFNNEMUEXTWORKERCB pfnCb, void *pvCb)
{
    HRESULT hr;
    PNEMUEXT_CALLPROC pCallInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof (NEMUEXT_CALLPROC) * 2);
    if (!pCallInfo)
    {
        ERR("HeapAlloc failed\n");
        return E_OUTOFMEMORY;
    }
    pCallInfo[0].pfnCb = nemuExtWorkerSubmitProcAsyncWorker;
    pCallInfo[0].pvCb = pCallInfo;
    pCallInfo[1].pfnCb = pfnCb;
    pCallInfo[1].pvCb = pvCb;
    hr = nemuExtWorkerSubmit(pWorker, WM_NEMUEXT_CALLPROC, (LPARAM)pCallInfo, FALSE);
    if (FAILED(hr))
    {
        ERR("nemuExtWorkerSubmit failed, hr 0x%x\n", hr);
        HeapFree(GetProcessHeap(), 0, pCallInfo);
        return hr;
    }
    return S_OK;
}


static HRESULT nemuExtInit()
{
    HRESULT hr = S_OK;
#ifdef NEMU_WITH_WDDM
    int rc = NemuCrHgsmiInit();
    if (!RT_SUCCESS(rc))
    {
        ERR("NemuCrHgsmiInit failed rc %d", rc);
        return E_FAIL;
    }
#endif
    memset(&g_NemuExtGlobal, 0, sizeof (g_NemuExtGlobal));
    hr = NemuExtWorkerCreate(&g_NemuExtGlobal.Worker);
    if (SUCCEEDED(hr))
        return S_OK;

    /* failure branch */
#ifdef NEMU_WITH_WDDM
    NemuCrHgsmiTerm();
#endif
    return hr;
}


static HRESULT nemuExtWndCleanup();

static HRESULT nemuExtTerm()
{
    HRESULT hr = nemuExtWndCleanup();
    if (!SUCCEEDED(hr))
    {
        ERR("nemuExtWndCleanup failed, hr %d", hr);
        return hr;
    }

    hr = NemuExtWorkerDestroy(&g_NemuExtGlobal.Worker);
    if (!SUCCEEDED(hr))
    {
        ERR("NemuExtWorkerDestroy failed, hr %d", hr);
        return hr;
    }

#ifdef NEMU_WITH_WDDM
    NemuCrHgsmiTerm();
#endif

    return S_OK;
}

/* wine serializes all calls to us, so no need for any synchronization here */
static DWORD g_cNemuExtInits = 0;

static DWORD nemuExtAddRef()
{
    return ++g_cNemuExtInits;
}

static DWORD nemuExtRelease()
{
    DWORD cNemuExtInits = --g_cNemuExtInits;
    Assert(cNemuExtInits < UINT32_MAX/2);
    return cNemuExtInits;
}

static DWORD nemuExtGetRef()
{
    return g_cNemuExtInits;
}

HRESULT NemuExtCheckInit()
{
    HRESULT hr = S_OK;
    if (!nemuExtGetRef())
    {
        hr = nemuExtInit();
        if (FAILED(hr))
        {
            ERR("nemuExtInit failed, hr (0x%x)", hr);
            return hr;
        }
    }
    nemuExtAddRef();
    return S_OK;
}

HRESULT NemuExtCheckTerm()
{
    HRESULT hr = S_OK;
    if (nemuExtGetRef() == 1)
    {
        hr = nemuExtTerm();
        if (FAILED(hr))
        {
            ERR("nemuExtTerm failed, hr (0x%x)", hr);
            return hr;
        }
    }
    nemuExtRelease();
    return S_OK;
}

HRESULT NemuExtDwSubmitProcSync(PFNNEMUEXTWORKERCB pfnCb, void *pvCb)
{
    return NemuExtWorkerSubmitProcSync(&g_NemuExtGlobal.Worker, pfnCb, pvCb);
}

HRESULT NemuExtDwSubmitProcAsync(PFNNEMUEXTWORKERCB pfnCb, void *pvCb)
{
    return NemuExtWorkerSubmitProcAsync(&g_NemuExtGlobal.Worker, pfnCb, pvCb);
}

#if defined(NEMU_WINE_WITH_SINGLE_CONTEXT) || defined(NEMU_WINE_WITH_SINGLE_SWAPCHAIN_CONTEXT)
# ifndef NEMU_WITH_WDDM
typedef struct NEMUEXT_GETDC_CB
{
    HWND hWnd;
    HDC hDC;
} NEMUEXT_GETDC_CB, *PNEMUEXT_GETDC_CB;

static DECLCALLBACK(void) nemuExtGetDCWorker(void *pvUser)
{
    PNEMUEXT_GETDC_CB pData = (PNEMUEXT_GETDC_CB)pvUser;
    pData->hDC = GetDC(pData->hWnd);
}

typedef struct NEMUEXT_RELEASEDC_CB
{
    HWND hWnd;
    HDC hDC;
    int ret;
} NEMUEXT_RELEASEDC_CB, *PNEMUEXT_RELEASEDC_CB;

static DECLCALLBACK(void) nemuExtReleaseDCWorker(void *pvUser)
{
    PNEMUEXT_RELEASEDC_CB pData = (PNEMUEXT_RELEASEDC_CB)pvUser;
    pData->ret = ReleaseDC(pData->hWnd, pData->hDC);
}

HDC NemuExtGetDC(HWND hWnd)
{
    HRESULT hr;
    NEMUEXT_GETDC_CB Data = {0};
    Data.hWnd = hWnd;
    Data.hDC = NULL;

    hr = NemuExtDwSubmitProcSync(nemuExtGetDCWorker, &Data);
    if (FAILED(hr))
    {
        ERR("NemuExtDwSubmitProcSync feiled, hr (0x%x)\n", hr);
        return NULL;
    }

    return Data.hDC;
}

int NemuExtReleaseDC(HWND hWnd, HDC hDC)
{
    HRESULT hr;
    NEMUEXT_RELEASEDC_CB Data = {0};
    Data.hWnd = hWnd;
    Data.hDC = hDC;
    Data.ret = 0;

    hr = NemuExtDwSubmitProcSync(nemuExtReleaseDCWorker, &Data);
    if (FAILED(hr))
    {
        ERR("NemuExtDwSubmitProcSync feiled, hr (0x%x)\n", hr);
        return -1;
    }

    return Data.ret;
}
# endif /* #ifndef NEMU_WITH_WDDM */

static DECLCALLBACK(void) nemuExtReleaseContextWorker(void *pvUser)
{
    struct wined3d_context *context = (struct wined3d_context *)pvUser;
    NemuTlsRefRelease(context);
}

void NemuExtReleaseContextAsync(struct wined3d_context *context)
{
    HRESULT hr;

    hr = NemuExtDwSubmitProcAsync(nemuExtReleaseContextWorker, context);
    if (FAILED(hr))
    {
        ERR("NemuExtDwSubmitProcAsync feiled, hr (0x%x)\n", hr);
        return;
    }
}

#endif /* #if defined(NEMU_WINE_WITH_SINGLE_CONTEXT) || defined(NEMU_WINE_WITH_SINGLE_SWAPCHAIN_CONTEXT) */

/* window creation API */
static LRESULT CALLBACK nemuExtWndProc(HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
)
{
    switch(uMsg)
    {
        case WM_CLOSE:
            TRACE("got WM_CLOSE for hwnd(0x%x)", hwnd);
            return 0;
        case WM_DESTROY:
            TRACE("got WM_DESTROY for hwnd(0x%x)", hwnd);
            return 0;
        case WM_NCHITTEST:
            TRACE("got WM_NCHITTEST for hwnd(0x%x)\n", hwnd);
            return HTNOWHERE;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#define NEMUEXTWND_NAME "VboxDispD3DWineWnd"

static HRESULT nemuExtWndDoCleanup()
{
    HRESULT hr = S_OK;
    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);
    WNDCLASS wc;
    if (GetClassInfo(hInstance, NEMUEXTWND_NAME, &wc))
    {
        if (!UnregisterClass(NEMUEXTWND_NAME, hInstance))
        {
            DWORD winEr = GetLastError();
            ERR("UnregisterClass failed, winErr(%d)\n", winEr);
            hr = E_FAIL;
        }
    }
    return hr;
}

static HRESULT nemuExtWndDoCreate(DWORD w, DWORD h, HWND *phWnd, HDC *phDC)
{
    HRESULT hr = S_OK;
    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);
    /* Register the Window Class. */
    WNDCLASS wc;
    if (!GetClassInfo(hInstance, NEMUEXTWND_NAME, &wc))
    {
        wc.style = 0;//CS_OWNDC;
        wc.lpfnWndProc = nemuExtWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = NULL;
        wc.hCursor = NULL;
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = NEMUEXTWND_NAME;
        if (!RegisterClass(&wc))
        {
            DWORD winErr = GetLastError();
            ERR("RegisterClass failed, winErr(%d)\n", winErr);
            hr = E_FAIL;
        }
    }

    if (hr == S_OK)
    {
        HWND hWnd = CreateWindowEx (WS_EX_TOOLWINDOW,
                                        NEMUEXTWND_NAME, NEMUEXTWND_NAME,
                                        WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_DISABLED,
                                        0, 0,
                                        w, h,
                                        NULL, //GetDesktopWindow() /* hWndParent */,
                                        NULL /* hMenu */,
                                        hInstance,
                                        NULL /* lpParam */);
        Assert(hWnd);
        if (hWnd)
        {
            *phWnd = hWnd;
            *phDC = GetDC(hWnd);
            /* make sure we keep inited until the window is active */
            nemuExtAddRef();
        }
        else
        {
            DWORD winErr = GetLastError();
            ERR("CreateWindowEx failed, winErr(%d)\n", winErr);
            hr = E_FAIL;
        }
    }

    return hr;
}

static HRESULT nemuExtWndDoDestroy(HWND hWnd, HDC hDC)
{
    BOOL bResult;
    DWORD winErr;
    ReleaseDC(hWnd, hDC);
    bResult = DestroyWindow(hWnd);
    Assert(bResult);
    if (bResult)
    {
        /* release the reference we previously acquired on window creation */
        nemuExtRelease();
        return S_OK;
    }

    winErr = GetLastError();
    ERR("DestroyWindow failed, winErr(%d) for hWnd(0x%x)\n", winErr, hWnd);

    return E_FAIL;
}

typedef struct NEMUEXTWND_CREATE_INFO
{
    int hr;
    HWND hWnd;
    HDC hDC;
    DWORD width;
    DWORD height;
} NEMUEXTWND_CREATE_INFO;

typedef struct NEMUEXTWND_DESTROY_INFO
{
    int hr;
    HWND hWnd;
    HDC hDC;
} NEMUEXTWND_DESTROY_INFO;

typedef struct NEMUEXTWND_CLEANUP_INFO
{
    int hr;
} NEMUEXTWND_CLEANUP_INFO;

static DECLCALLBACK(void) nemuExtWndDestroyWorker(void *pvUser)
{
    NEMUEXTWND_DESTROY_INFO *pInfo = (NEMUEXTWND_DESTROY_INFO*)pvUser;
    pInfo->hr = nemuExtWndDoDestroy(pInfo->hWnd, pInfo->hDC);
    Assert(pInfo->hr == S_OK);
}

static DECLCALLBACK(void) nemuExtWndCreateWorker(void *pvUser)
{
    NEMUEXTWND_CREATE_INFO *pInfo = (NEMUEXTWND_CREATE_INFO*)pvUser;
    pInfo->hr = nemuExtWndDoCreate(pInfo->width, pInfo->height, &pInfo->hWnd, &pInfo->hDC);
    Assert(pInfo->hr == S_OK);
}

static DECLCALLBACK(void) nemuExtWndCleanupWorker(void *pvUser)
{
    NEMUEXTWND_CLEANUP_INFO *pInfo = (NEMUEXTWND_CLEANUP_INFO*)pvUser;
    pInfo-> hr = nemuExtWndDoCleanup();
}

HRESULT NemuExtWndDestroy(HWND hWnd, HDC hDC)
{
    HRESULT hr;
    NEMUEXTWND_DESTROY_INFO Info;
    Info.hr = E_FAIL;
    Info.hWnd = hWnd;
    Info.hDC = hDC;
    hr = NemuExtDwSubmitProcSync(nemuExtWndDestroyWorker, &Info);
    if (!SUCCEEDED(hr))
    {
        ERR("NemuExtDwSubmitProcSync-nemuExtWndDestroyWorker failed hr %d", hr);
        return hr;
    }

    if (!SUCCEEDED(Info.hr))
    {
        ERR("nemuExtWndDestroyWorker failed hr %d", Info.hr);
        return Info.hr;
    }

    return S_OK;
}

HRESULT NemuExtWndCreate(DWORD width, DWORD height, HWND *phWnd, HDC *phDC)
{
    HRESULT hr;
    NEMUEXTWND_CREATE_INFO Info;
    Info.hr = E_FAIL;
    Info.width = width;
    Info.height = height;
    hr = NemuExtDwSubmitProcSync(nemuExtWndCreateWorker, &Info);
    if (!SUCCEEDED(hr))
    {
        ERR("NemuExtDwSubmitProcSync-nemuExtWndCreateWorker failed hr %d", hr);
        return hr;
    }

    Assert(Info.hr == S_OK);
    if (!SUCCEEDED(Info.hr))
    {
        ERR("nemuExtWndCreateWorker failed hr %d", Info.hr);
        return Info.hr;
    }

    *phWnd = Info.hWnd;
    *phDC = Info.hDC;
    return S_OK;
}

static HRESULT nemuExtWndCleanup()
{
    HRESULT hr;
    NEMUEXTWND_CLEANUP_INFO Info;
    Info.hr = E_FAIL;
    hr = NemuExtDwSubmitProcSync(nemuExtWndCleanupWorker, &Info);
    if (!SUCCEEDED(hr))
    {
        ERR("NemuExtDwSubmitProcSync-nemuExtWndCleanupWorker failed hr %d", hr);
        return hr;
    }

    if (!SUCCEEDED(Info.hr))
    {
        ERR("nemuExtWndCleanupWorker failed hr %d", Info.hr);
        return Info.hr;
    }

    return S_OK;
}


/* hash map impl */
static void nemuExtHashInitEntries(PNEMUEXT_HASHMAP pMap)
{
    uint32_t i;
    pMap->cEntries = 0;
    for (i = 0; i < RT_ELEMENTS(pMap->aBuckets); ++i)
    {
        RTListInit(&pMap->aBuckets[i].EntryList);
    }
}

void NemuExtHashInit(PNEMUEXT_HASHMAP pMap, PFNNEMUEXT_HASHMAP_HASH pfnHash, PFNNEMUEXT_HASHMAP_EQUAL pfnEqual)
{
    pMap->pfnHash = pfnHash;
    pMap->pfnEqual = pfnEqual;
    nemuExtHashInitEntries(pMap);
}

static DECLINLINE(uint32_t) nemuExtHashIdx(uint32_t u32Hash)
{
    return u32Hash % NEMUEXT_HASHMAP_NUM_BUCKETS;
}

#define NEMUEXT_FOREACH_NODE(_pNode, _pList, _op) do { \
        PRTLISTNODE _pNode; \
        PRTLISTNODE __pNext; \
        for (_pNode = (_pList)->pNext; \
                _pNode != (_pList); \
                _pNode = __pNext) \
        { \
            __pNext = _pNode->pNext; /* <- the _pNode should not be referenced after the _op */ \
            _op \
        } \
    } while (0)

DECLINLINE(PNEMUEXT_HASHMAP_ENTRY) nemuExtHashSearchEntry(PNEMUEXT_HASHMAP pMap, void *pvKey)
{
    uint32_t u32Hash = pMap->pfnHash(pvKey);
    uint32_t u32HashIdx = nemuExtHashIdx(u32Hash);
    PNEMUEXT_HASHMAP_BUCKET pBucket = &pMap->aBuckets[u32HashIdx];
    PNEMUEXT_HASHMAP_ENTRY pEntry;
    NEMUEXT_FOREACH_NODE(pNode, &pBucket->EntryList,
        pEntry = RT_FROM_MEMBER(pNode, NEMUEXT_HASHMAP_ENTRY, ListNode);
        if (pEntry->u32Hash != u32Hash)
            continue;

        if (!pMap->pfnEqual(pvKey, pEntry->pvKey))
            continue;
        return pEntry;
    );
    return NULL;
}

void* NemuExtHashRemoveEntry(PNEMUEXT_HASHMAP pMap, PNEMUEXT_HASHMAP_ENTRY pEntry)
{
    RTListNodeRemove(&pEntry->ListNode);
    --pMap->cEntries;
    Assert(pMap->cEntries <= UINT32_MAX/2);
    return pEntry->pvKey;
}

static void nemuExtHashPutEntry(PNEMUEXT_HASHMAP pMap, PNEMUEXT_HASHMAP_BUCKET pBucket, PNEMUEXT_HASHMAP_ENTRY pEntry)
{
    RTListNodeInsertAfter(&pBucket->EntryList, &pEntry->ListNode);
    ++pMap->cEntries;
}

PNEMUEXT_HASHMAP_ENTRY NemuExtHashRemove(PNEMUEXT_HASHMAP pMap, void *pvKey)
{
    PNEMUEXT_HASHMAP_ENTRY pEntry = nemuExtHashSearchEntry(pMap, pvKey);
    if (!pEntry)
        return NULL;

    NemuExtHashRemoveEntry(pMap, pEntry);
    return pEntry;
}

PNEMUEXT_HASHMAP_ENTRY NemuExtHashPut(PNEMUEXT_HASHMAP pMap, void *pvKey, PNEMUEXT_HASHMAP_ENTRY pEntry)
{
    PNEMUEXT_HASHMAP_ENTRY pOldEntry = NemuExtHashRemove(pMap, pvKey);
    uint32_t u32Hash = pMap->pfnHash(pvKey);
    uint32_t u32HashIdx = nemuExtHashIdx(u32Hash);
    pEntry->pvKey = pvKey;
    pEntry->u32Hash = u32Hash;
    nemuExtHashPutEntry(pMap, &pMap->aBuckets[u32HashIdx], pEntry);
    return pOldEntry;
}


PNEMUEXT_HASHMAP_ENTRY NemuExtHashGet(PNEMUEXT_HASHMAP pMap, void *pvKey)
{
    return nemuExtHashSearchEntry(pMap, pvKey);
}

void NemuExtHashVisit(PNEMUEXT_HASHMAP pMap, PFNNEMUEXT_HASHMAP_VISITOR pfnVisitor, void *pvVisitor)
{
    uint32_t iBucket = 0, iEntry = 0;
    uint32_t cEntries = pMap->cEntries;

    if (!cEntries)
        return;

    for (; ; ++iBucket)
    {
        PNEMUEXT_HASHMAP_ENTRY pEntry;
        PNEMUEXT_HASHMAP_BUCKET pBucket = &pMap->aBuckets[iBucket];
        Assert(iBucket < RT_ELEMENTS(pMap->aBuckets));
        NEMUEXT_FOREACH_NODE(pNode, &pBucket->EntryList,
            pEntry = RT_FROM_MEMBER(pNode, NEMUEXT_HASHMAP_ENTRY, ListNode);
            if (!pfnVisitor(pMap, pEntry->pvKey, pEntry, pvVisitor))
                return;

            if (++iEntry == cEntries)
                return;
        );
    }

    /* should not be here! */
    AssertFailed();
}

void NemuExtHashCleanup(PNEMUEXT_HASHMAP pMap, PFNNEMUEXT_HASHMAP_VISITOR pfnVisitor, void *pvVisitor)
{
    NemuExtHashVisit(pMap, pfnVisitor, pvVisitor);
    nemuExtHashInitEntries(pMap);
}

static DECLCALLBACK(bool) nemuExtCacheCleanupCb(struct NEMUEXT_HASHMAP *pMap, void *pvKey, struct NEMUEXT_HASHMAP_ENTRY *pValue, void *pvVisitor)
{
    PNEMUEXT_HASHCACHE pCache = NEMUEXT_HASHCACHE_FROM_MAP(pMap);
    PNEMUEXT_HASHCACHE_ENTRY pCacheEntry = NEMUEXT_HASHCACHE_ENTRY_FROM_MAP(pValue);
    pCache->pfnCleanupEntry(pvKey, pCacheEntry);
    return TRUE;
}

void NemuExtCacheCleanup(PNEMUEXT_HASHCACHE pCache)
{
    NemuExtHashCleanup(&pCache->Map, nemuExtCacheCleanupCb, NULL);
}

#if defined(NEMUWINEDBG_SHADERS) || defined(NEMU_WINE_WITH_PROFILE)
void nemuWDbgPrintF(char * szString, ...)
{
    char szBuffer[4096*2] = {0};
    va_list pArgList;
    va_start(pArgList, szString);
    _vsnprintf(szBuffer, sizeof(szBuffer) / sizeof(szBuffer[0]), szString, pArgList);
    va_end(pArgList);

    OutputDebugStringA(szBuffer);
}
#endif
