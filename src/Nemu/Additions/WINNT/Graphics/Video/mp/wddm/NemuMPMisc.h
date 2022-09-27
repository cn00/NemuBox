/* $Id: NemuMPMisc.h $ */

/** @file
 * Nemu WDDM Miniport driver
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

#ifndef ___NemuMPMisc_h__
#define ___NemuMPMisc_h__

#include "../../common/NemuVideoTools.h"

DECLINLINE(void) nemuVideoLeDetach(LIST_ENTRY *pList, LIST_ENTRY *pDstList)
{
    if (IsListEmpty(pList))
    {
        InitializeListHead(pDstList);
    }
    else
    {
        *pDstList = *pList;
        Assert(pDstList->Flink->Blink == pList);
        Assert(pDstList->Blink->Flink == pList);
        /* pDstList->Flink & pDstList->Blink point to the "real| entries, never to pList
         * since we've checked IsListEmpty(pList) above */
        pDstList->Flink->Blink = pDstList;
        pDstList->Blink->Flink = pDstList;
        InitializeListHead(pList);
    }
}

typedef uint32_t NEMUWDDM_HANDLE;
#define NEMUWDDM_HANDLE_INVALID 0UL

typedef struct NEMUWDDM_HTABLE
{
    uint32_t cData;
    uint32_t iNext2Search;
    uint32_t cSize;
    PVOID *paData;
} NEMUWDDM_HTABLE, *PNEMUWDDM_HTABLE;

typedef struct NEMUWDDM_HTABLE_ITERATOR
{
    PNEMUWDDM_HTABLE pTbl;
    uint32_t iCur;
    uint32_t cLeft;
} NEMUWDDM_HTABLE_ITERATOR, *PNEMUWDDM_HTABLE_ITERATOR;

VOID nemuWddmHTableIterInit(PNEMUWDDM_HTABLE pTbl, PNEMUWDDM_HTABLE_ITERATOR pIter);
PVOID nemuWddmHTableIterNext(PNEMUWDDM_HTABLE_ITERATOR pIter, NEMUWDDM_HANDLE *phHandle);
BOOL nemuWddmHTableIterHasNext(PNEMUWDDM_HTABLE_ITERATOR pIter);
PVOID nemuWddmHTableIterRemoveCur(PNEMUWDDM_HTABLE_ITERATOR pIter);
NTSTATUS nemuWddmHTableCreate(PNEMUWDDM_HTABLE pTbl, uint32_t cSize);
VOID nemuWddmHTableDestroy(PNEMUWDDM_HTABLE pTbl);
NTSTATUS nemuWddmHTableRealloc(PNEMUWDDM_HTABLE pTbl, uint32_t cNewSize);
NEMUWDDM_HANDLE nemuWddmHTablePut(PNEMUWDDM_HTABLE pTbl, PVOID pvData);
PVOID nemuWddmHTableRemove(PNEMUWDDM_HTABLE pTbl, NEMUWDDM_HANDLE hHandle);
PVOID nemuWddmHTableGet(PNEMUWDDM_HTABLE pTbl, NEMUWDDM_HANDLE hHandle);

#ifdef NEMU_WITH_CROGL
PNEMUWDDM_SWAPCHAIN nemuWddmSwapchainCreate();
BOOLEAN nemuWddmSwapchainRetain(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain);
VOID nemuWddmSwapchainRelease(PNEMUWDDM_SWAPCHAIN pSwapchain);
PNEMUWDDM_SWAPCHAIN nemuWddmSwapchainRetainByAlloc(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOCATION *pAlloc);
PNEMUWDDM_SWAPCHAIN nemuWddmSwapchainRetainByAllocData(PNEMUMP_DEVEXT pDevExt, const struct NEMUWDDM_ALLOC_DATA *pAllocData);
VOID nemuWddmSwapchainAllocRemove(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain, PNEMUWDDM_ALLOCATION pAlloc);
BOOLEAN nemuWddmSwapchainAllocAdd(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain, PNEMUWDDM_ALLOCATION pAlloc);
VOID nemuWddmSwapchainAllocRemoveAll(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain);
VOID nemuWddmSwapchainDestroy(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SWAPCHAIN pSwapchain);
VOID nemuWddmSwapchainCtxDestroyAll(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext);
NTSTATUS nemuWddmSwapchainCtxEscape(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext, PNEMUDISPIFESCAPE_SWAPCHAININFO pSwapchainInfo, UINT cbSize);
NTSTATUS nemuWddmSwapchainCtxInit(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext);
VOID nemuWddmSwapchainCtxTerm(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_CONTEXT pContext);
#endif


NTSTATUS nemuWddmRegQueryDisplaySettingsKeyName(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);
NTSTATUS nemuWddmRegOpenDisplaySettingsKey(IN PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, OUT PHANDLE phKey);
NTSTATUS nemuWddmRegDisplaySettingsQueryRelX(HANDLE hKey, int * pResult);
NTSTATUS nemuWddmRegDisplaySettingsQueryRelY(HANDLE hKey, int * pResult);
NTSTATUS nemuWddmDisplaySettingsQueryPos(IN PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, POINT * pPos);
void nemuWddmDisplaySettingsCheckPos(IN PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);
NTSTATUS nemuWddmRegQueryVideoGuidString(PNEMUMP_DEVEXT pDevExt, ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);

NTSTATUS nemuWddmRegQueryDrvKeyName(PNEMUMP_DEVEXT pDevExt, ULONG cbBuf, PWCHAR pBuf, PULONG pcbResult);

NTSTATUS nemuWddmRegOpenKeyEx(OUT PHANDLE phKey, IN HANDLE hRootKey, IN PWCHAR pName, IN ACCESS_MASK fAccess);
NTSTATUS nemuWddmRegOpenKey(OUT PHANDLE phKey, IN PWCHAR pName, IN ACCESS_MASK fAccess);
NTSTATUS nemuWddmRegQueryValueDword(IN HANDLE hKey, IN PWCHAR pName, OUT PDWORD pDword);
NTSTATUS nemuWddmRegSetValueDword(IN HANDLE hKey, IN PWCHAR pName, IN DWORD val);

NTSTATUS nemuWddmRegDrvFlagsSet(PNEMUMP_DEVEXT pDevExt, DWORD fVal);
DWORD nemuWddmRegDrvFlagsGet(PNEMUMP_DEVEXT pDevExt, DWORD fDefault);

UNICODE_STRING* nemuWddmVGuidGet(PNEMUMP_DEVEXT pDevExt);
VOID nemuWddmVGuidFree(PNEMUMP_DEVEXT pDevExt);

#define NEMUWDDM_MM_VOID 0xffffffffUL

typedef struct NEMUWDDM_MM
{
    RTL_BITMAP BitMap;
    UINT cPages;
    UINT cAllocs;
    PULONG pBuffer;
} NEMUWDDM_MM, *PNEMUWDDM_MM;

NTSTATUS nemuMmInit(PNEMUWDDM_MM pMm, UINT cPages);
ULONG nemuMmAlloc(PNEMUWDDM_MM pMm, UINT cPages);
VOID nemuMmFree(PNEMUWDDM_MM pMm, UINT iPage, UINT cPages);
NTSTATUS nemuMmTerm(PNEMUWDDM_MM pMm);

typedef struct NEMUVIDEOCM_ALLOC_MGR
{
    /* synch lock */
    FAST_MUTEX Mutex;
    NEMUWDDM_HTABLE AllocTable;
    NEMUWDDM_MM Mm;
//    PHYSICAL_ADDRESS PhData;
    uint8_t *pvData;
    uint32_t offData;
    uint32_t cbData;
} NEMUVIDEOCM_ALLOC_MGR, *PNEMUVIDEOCM_ALLOC_MGR;

typedef struct NEMUVIDEOCM_ALLOC_CONTEXT
{
    PNEMUVIDEOCM_ALLOC_MGR pMgr;
    /* synch lock */
    FAST_MUTEX Mutex;
    NEMUWDDM_HTABLE AllocTable;
} NEMUVIDEOCM_ALLOC_CONTEXT, *PNEMUVIDEOCM_ALLOC_CONTEXT;

NTSTATUS nemuVideoAMgrCreate(PNEMUMP_DEVEXT pDevExt, PNEMUVIDEOCM_ALLOC_MGR pMgr, uint32_t offData, uint32_t cbData);
NTSTATUS nemuVideoAMgrDestroy(PNEMUMP_DEVEXT pDevExt, PNEMUVIDEOCM_ALLOC_MGR pMgr);

NTSTATUS nemuVideoAMgrCtxCreate(PNEMUVIDEOCM_ALLOC_MGR pMgr, PNEMUVIDEOCM_ALLOC_CONTEXT pCtx);
NTSTATUS nemuVideoAMgrCtxDestroy(PNEMUVIDEOCM_ALLOC_CONTEXT pCtx);

NTSTATUS nemuVideoAMgrCtxAllocCreate(PNEMUVIDEOCM_ALLOC_CONTEXT pContext, PNEMUVIDEOCM_UM_ALLOC pUmAlloc);
NTSTATUS nemuVideoAMgrCtxAllocDestroy(PNEMUVIDEOCM_ALLOC_CONTEXT pContext, NEMUDISP_KMHANDLE hSesionHandle);

#ifdef NEMU_WITH_CRHGSMI
NTSTATUS nemuVideoAMgrCtxAllocSubmit(PNEMUMP_DEVEXT pDevExt, PNEMUVIDEOCM_ALLOC_CONTEXT pContext, UINT cBuffers, NEMUWDDM_UHGSMI_BUFFER_UI_INFO_ESCAPE *paBuffers);
#endif

VOID nemuWddmSleep(uint32_t u32Val);
VOID nemuWddmCounterU32Wait(uint32_t volatile * pu32, uint32_t u32Val);

NTSTATUS nemuUmdDumpBuf(PNEMUDISPIFESCAPE_DBGDUMPBUF pBuf, uint32_t cbBuffer);

#if 0
/* wine shrc handle -> allocation map */
VOID nemuShRcTreeInit(PNEMUMP_DEVEXT pDevExt);
VOID nemuShRcTreeTerm(PNEMUMP_DEVEXT pDevExt);
BOOLEAN nemuShRcTreePut(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pAlloc);
PNEMUWDDM_ALLOCATION nemuShRcTreeGet(PNEMUMP_DEVEXT pDevExt, HANDLE hSharedRc);
BOOLEAN nemuShRcTreeRemove(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pAlloc);
#endif

NTSTATUS nemuWddmDrvCfgInit(PUNICODE_STRING pRegStr);

#ifdef NEMU_VDMA_WITH_WATCHDOG
NTSTATUS nemuWddmWdInit(PNEMUMP_DEVEXT pDevExt);
NTSTATUS nemuWddmWdTerm(PNEMUMP_DEVEXT pDevExt);
#endif

NTSTATUS NemuWddmSlEnableVSyncNotification(PNEMUMP_DEVEXT pDevExt, BOOLEAN fEnable);
NTSTATUS NemuWddmSlGetScanLine(PNEMUMP_DEVEXT pDevExt, DXGKARG_GETSCANLINE *pSl);
NTSTATUS NemuWddmSlInit(PNEMUMP_DEVEXT pDevExt);
NTSTATUS NemuWddmSlTerm(PNEMUMP_DEVEXT pDevExt);

#ifdef NEMU_WDDM_WIN8
void nemuWddmDiInitDefault(DXGK_DISPLAY_INFORMATION *pInfo, PHYSICAL_ADDRESS PhAddr, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);
void nemuWddmDiToAllocData(PNEMUMP_DEVEXT pDevExt, const DXGK_DISPLAY_INFORMATION *pInfo, struct NEMUWDDM_ALLOC_DATA *pAllocData);
void nemuWddmDmSetupDefaultVramLocation(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID ModifiedVidPnSourceId, struct NEMUWDDM_SOURCE *paSources);
#endif

#endif /* #ifndef ___NemuMPMisc_h__ */
