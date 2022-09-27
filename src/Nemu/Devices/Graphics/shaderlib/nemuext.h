/* $Id: nemuext.h $ */
/** @file
 *
 * Nemu extension to Wine D3D
 *
 * Copyright (C) 2011-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___NEMUEXT_H__
#define ___NEMUEXT_H__

#ifdef NEMU_WINE_WITHOUT_LIBWINE
# include <windows.h>
#endif

#include <iprt/list.h>

HRESULT NemuExtCheckInit();
HRESULT NemuExtCheckTerm();
#if defined(NEMU_WINE_WITH_SINGLE_CONTEXT) || defined(NEMU_WINE_WITH_SINGLE_SWAPCHAIN_CONTEXT)
# ifndef NEMU_WITH_WDDM
/* Windows destroys HDC created by a given thread when the thread is terminated
 * this leads to a mess-up in Wine & Chromium code in some situations, e.g.
 * D3D device is created in one thread, then the thread is terminated,
 * then device is started to be used in another thread */
HDC NemuExtGetDC(HWND hWnd);
int NemuExtReleaseDC(HWND hWnd, HDC hDC);
# endif
/* We need to do a NemuTlsRefRelease for the current thread context on thread exit to avoid memory leaking
 * Calling NemuTlsRefRelease may result in a call to context dtor callback, which is supposed to be run under wined3d lock.
 * We can not acquire a wined3d lock in DllMain since this would result in a lock order violation, which may result in a deadlock.
 * In other words, wined3d may internally call Win32 API functions which result in a DLL lock acquisition while holding wined3d lock.
 * So lock order should always be "wined3d lock" -> "dll lock".
 * To avoid possible deadlocks we make an asynchronous call to a worker thread to make a context release from there. */
struct wined3d_context;
void NemuExtReleaseContextAsync(struct wined3d_context *context);
#endif

/* API for creating & destroying windows */
HRESULT NemuExtWndDestroy(HWND hWnd, HDC hDC);
HRESULT NemuExtWndCreate(DWORD width, DWORD height, HWND *phWnd, HDC *phDC);


/* hashmap */
typedef DECLCALLBACK(uint32_t) FNNEMUEXT_HASHMAP_HASH(void *pvKey);
typedef FNNEMUEXT_HASHMAP_HASH *PFNNEMUEXT_HASHMAP_HASH;

typedef DECLCALLBACK(bool) FNNEMUEXT_HASHMAP_EQUAL(void *pvKey1, void *pvKey2);
typedef FNNEMUEXT_HASHMAP_EQUAL *PFNNEMUEXT_HASHMAP_EQUAL;

struct NEMUEXT_HASHMAP;
struct NEMUEXT_HASHMAP_ENTRY;
typedef DECLCALLBACK(bool) FNNEMUEXT_HASHMAP_VISITOR(struct NEMUEXT_HASHMAP *pMap, void *pvKey, struct NEMUEXT_HASHMAP_ENTRY *pValue, void *pvVisitor);
typedef FNNEMUEXT_HASHMAP_VISITOR *PFNNEMUEXT_HASHMAP_VISITOR;

typedef struct NEMUEXT_HASHMAP_ENTRY
{
    RTLISTNODE ListNode;
    void *pvKey;
    uint32_t u32Hash;
} NEMUEXT_HASHMAP_ENTRY, *PNEMUEXT_HASHMAP_ENTRY;

typedef struct NEMUEXT_HASHMAP_BUCKET
{
    RTLISTNODE EntryList;
} NEMUEXT_HASHMAP_BUCKET, *PNEMUEXT_HASHMAP_BUCKET;

#define NEMUEXT_HASHMAP_NUM_BUCKETS 29

typedef struct NEMUEXT_HASHMAP
{
    PFNNEMUEXT_HASHMAP_HASH pfnHash;
    PFNNEMUEXT_HASHMAP_EQUAL pfnEqual;
    uint32_t cEntries;
    NEMUEXT_HASHMAP_BUCKET aBuckets[NEMUEXT_HASHMAP_NUM_BUCKETS];
} NEMUEXT_HASHMAP, *PNEMUEXT_HASHMAP;

void NemuExtHashInit(PNEMUEXT_HASHMAP pMap, PFNNEMUEXT_HASHMAP_HASH pfnHash, PFNNEMUEXT_HASHMAP_EQUAL pfnEqual);
PNEMUEXT_HASHMAP_ENTRY NemuExtHashPut(PNEMUEXT_HASHMAP pMap, void *pvKey, PNEMUEXT_HASHMAP_ENTRY pEntry);
PNEMUEXT_HASHMAP_ENTRY NemuExtHashGet(PNEMUEXT_HASHMAP pMap, void *pvKey);
PNEMUEXT_HASHMAP_ENTRY NemuExtHashRemove(PNEMUEXT_HASHMAP pMap, void *pvKey);
void* NemuExtHashRemoveEntry(PNEMUEXT_HASHMAP pMap, PNEMUEXT_HASHMAP_ENTRY pEntry);
void NemuExtHashVisit(PNEMUEXT_HASHMAP pMap, PFNNEMUEXT_HASHMAP_VISITOR pfnVisitor, void *pvVisitor);
void NemuExtHashCleanup(PNEMUEXT_HASHMAP pMap, PFNNEMUEXT_HASHMAP_VISITOR pfnVisitor, void *pvVisitor);

DECLINLINE(uint32_t) NemuExtHashSize(PNEMUEXT_HASHMAP pMap)
{
    return pMap->cEntries;
}

DECLINLINE(void*) NemuExtHashEntryKey(PNEMUEXT_HASHMAP_ENTRY pEntry)
{
    return pEntry->pvKey;
}

struct NEMUEXT_HASHCACHE_ENTRY;
typedef DECLCALLBACK(void) FNNEMUEXT_HASHCACHE_CLEANUP_ENTRY(void *pvKey, struct NEMUEXT_HASHCACHE_ENTRY *pEntry);
typedef FNNEMUEXT_HASHCACHE_CLEANUP_ENTRY *PFNNEMUEXT_HASHCACHE_CLEANUP_ENTRY;

typedef struct NEMUEXT_HASHCACHE_ENTRY
{
    NEMUEXT_HASHMAP_ENTRY MapEntry;
    uint32_t u32Usage;
} NEMUEXT_HASHCACHE_ENTRY, *PNEMUEXT_HASHCACHE_ENTRY;

typedef struct NEMUEXT_HASHCACHE
{
    NEMUEXT_HASHMAP Map;
    uint32_t cMaxElements;
    PFNNEMUEXT_HASHCACHE_CLEANUP_ENTRY pfnCleanupEntry;
} NEMUEXT_HASHCACHE, *PNEMUEXT_HASHCACHE;

#define NEMUEXT_HASHCACHE_FROM_MAP(_pMap) RT_FROM_MEMBER((_pMap), NEMUEXT_HASHCACHE, Map)
#define NEMUEXT_HASHCACHE_ENTRY_FROM_MAP(_pEntry) RT_FROM_MEMBER((_pEntry), NEMUEXT_HASHCACHE_ENTRY, MapEntry)

DECLINLINE(void) NemuExtCacheInit(PNEMUEXT_HASHCACHE pCache, uint32_t cMaxElements,
        PFNNEMUEXT_HASHMAP_HASH pfnHash,
        PFNNEMUEXT_HASHMAP_EQUAL pfnEqual,
        PFNNEMUEXT_HASHCACHE_CLEANUP_ENTRY pfnCleanupEntry)
{
    NemuExtHashInit(&pCache->Map, pfnHash, pfnEqual);
    pCache->cMaxElements = cMaxElements;
    pCache->pfnCleanupEntry = pfnCleanupEntry;
}

DECLINLINE(PNEMUEXT_HASHCACHE_ENTRY) NemuExtCacheGet(PNEMUEXT_HASHCACHE pCache, void *pvKey)
{
    PNEMUEXT_HASHMAP_ENTRY pEntry = NemuExtHashRemove(&pCache->Map, pvKey);
    return NEMUEXT_HASHCACHE_ENTRY_FROM_MAP(pEntry);
}

DECLINLINE(void) NemuExtCachePut(PNEMUEXT_HASHCACHE pCache, void *pvKey, PNEMUEXT_HASHCACHE_ENTRY pEntry)
{
    PNEMUEXT_HASHMAP_ENTRY pOldEntry = NemuExtHashPut(&pCache->Map, pvKey, &pEntry->MapEntry);
    PNEMUEXT_HASHCACHE_ENTRY pOld;
    if (!pOldEntry)
        return;
    pOld = NEMUEXT_HASHCACHE_ENTRY_FROM_MAP(pOldEntry);
    if (pOld != pEntry)
        pCache->pfnCleanupEntry(pvKey, pOld);
}

void NemuExtCacheCleanup(PNEMUEXT_HASHCACHE pCache);

DECLINLINE(void) NemuExtCacheTerm(PNEMUEXT_HASHCACHE pCache)
{
    NemuExtCacheCleanup(pCache);
}

#endif /* #ifndef ___NEMUEXT_H__*/
