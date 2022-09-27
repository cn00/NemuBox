/* $Id: nemuext.h $ */
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

#ifdef NEMU_WINE_WITH_PROFILE

#include <iprt/time.h>

void nemuWDbgPrintF(char * szString, ...);

#define NEMUWINEPROFILE_GET_TIME_NANO() RTTimeNanoTS()
#define NEMUWINEPROFILE_GET_TIME_MILLI() RTTimeMilliTS()

# define PRLOG(_m) do {\
        nemuWDbgPrintF _m ; \
    } while (0)

typedef struct NEMUWINEPROFILE_ELEMENT
{
    uint64_t u64Time;
    uint32_t cu32Calls;
} NEMUWINEPROFILE_ELEMENT, *PNEMUWINEPROFILE_ELEMENT;

typedef struct NEMUWINEPROFILE_HASHMAP_ELEMENT
{
    NEMUEXT_HASHMAP_ENTRY MapEntry;
    NEMUWINEPROFILE_ELEMENT Data;
} NEMUWINEPROFILE_HASHMAP_ELEMENT, *PNEMUWINEPROFILE_HASHMAP_ELEMENT;

#define NEMUWINEPROFILE_HASHMAP_ELEMENT_FROMENTRY(_p) ((PNEMUWINEPROFILE_HASHMAP_ELEMENT)(((uint8_t*)(_p)) - RT_OFFSETOF(NEMUWINEPROFILE_HASHMAP_ELEMENT, MapEntry)))

#define NEMUWINEPROFILE_ELEMENT_DUMP(_p, _pn) do { \
        PRLOG(("%s: t(%u);c(%u)\n", \
            (_pn), \
            (uint32_t)((_p)->u64Time / 1000000), \
            (_p)->cu32Calls \
            )); \
    } while (0)

#define NEMUWINEPROFILE_ELEMENT_RESET(_p) do { \
        memset(_p, 0, sizeof (*(_p))); \
    } while (0)

#define NEMUWINEPROFILE_ELEMENT_STEP(_p, _t) do { \
        (_p)->u64Time += (_t); \
        ++(_p)->cu32Calls; \
    } while (0)

#define NEMUWINEPROFILE_HASHMAP_ELEMENT_CREATE()  ( (PNEMUWINEPROFILE_HASHMAP_ELEMENT)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof (NEMUWINEPROFILE_HASHMAP_ELEMENT)) )

#define NEMUWINEPROFILE_HASHMAP_ELEMENT_TERM(_pe) do { \
        HeapFree(GetProcessHeap(), 0, (_pe)); \
    } while (0)

DECLINLINE(PNEMUWINEPROFILE_HASHMAP_ELEMENT) nemuWineProfileHashMapElementGet(PNEMUEXT_HASHMAP pMap, void *pvKey)
{
    PNEMUEXT_HASHMAP_ENTRY pEntry = NemuExtHashGet(pMap, pvKey);
    if (pEntry)
    {
        return NEMUWINEPROFILE_HASHMAP_ELEMENT_FROMENTRY(pEntry);
    }
    else
    {
        PNEMUWINEPROFILE_HASHMAP_ELEMENT pElement = NEMUWINEPROFILE_HASHMAP_ELEMENT_CREATE();
        Assert(pElement);
        if (pElement)
            NemuExtHashPut(pMap, pvKey, &pElement->MapEntry);
        return pElement;
    }
}

#define NEMUWINEPROFILE_HASHMAP_ELEMENT_STEP(_pm, _pk, _t) do { \
        PNEMUWINEPROFILE_HASHMAP_ELEMENT pElement = nemuWineProfileHashMapElementGet(_pm, _pk); \
        NEMUWINEPROFILE_ELEMENT_STEP(&pElement->Data, _t); \
    } while (0)

static DECLCALLBACK(bool) nemuWineProfileElementResetCb(struct NEMUEXT_HASHMAP *pMap, void *pvKey, struct NEMUEXT_HASHMAP_ENTRY *pValue, void *pvVisitor)
{
    PNEMUWINEPROFILE_HASHMAP_ELEMENT pElement = NEMUWINEPROFILE_HASHMAP_ELEMENT_FROMENTRY(pValue);
    NEMUWINEPROFILE_ELEMENT_RESET(&pElement->Data);
    return true;
}

static DECLCALLBACK(bool) nemuWineProfileElementDumpCb(struct NEMUEXT_HASHMAP *pMap, void *pvKey, struct NEMUEXT_HASHMAP_ENTRY *pValue, void *pvVisitor)
{
    PNEMUWINEPROFILE_HASHMAP_ELEMENT pElement = NEMUWINEPROFILE_HASHMAP_ELEMENT_FROMENTRY(pValue);
    char *pName = (char*)pvVisitor;
    PRLOG(("%s[%d]:", pName, (uint32_t)pvKey));
    NEMUWINEPROFILE_ELEMENT_DUMP(&pElement->Data, "");
    return true;
}


#define NEMUWINEPROFILE_HASHMAP_RESET(_pm) do { \
        NemuExtHashVisit((_pm), nemuWineProfileElementResetCb, NULL); \
    } while (0)

#define NEMUWINEPROFILE_HASHMAP_DUMP(_pm, _pn) do { \
        NemuExtHashVisit((_pm), nemuWineProfileElementDumpCb, (_pn)); \
    } while (0)

static DECLCALLBACK(bool) nemuWineProfileElementCleanupCb(struct NEMUEXT_HASHMAP *pMap, void *pvKey, struct NEMUEXT_HASHMAP_ENTRY *pValue, void *pvVisitor)
{
    PNEMUWINEPROFILE_HASHMAP_ELEMENT pElement = NEMUWINEPROFILE_HASHMAP_ELEMENT_FROMENTRY(pValue);
    NEMUWINEPROFILE_HASHMAP_ELEMENT_TERM(pElement);
    return true;
}

#define NEMUWINEPROFILE_HASHMAP_TERM(_pm) do { \
        NemuExtHashCleanup((_pm), nemuWineProfileElementCleanupCb, NULL); \
        NemuExtHashVisit((_pm), nemuWineProfileElementResetCb, NULL); \
    } while (0)

typedef struct NEMUWINEPROFILE_DRAWPRIM
{
    uint64_t u64LoadLocationTime;
    uint64_t u64CtxAcquireTime;
    uint64_t u64PostProcess;
    NEMUEXT_HASHMAP MapDrawPrimSlowVs;
    NEMUEXT_HASHMAP MapDrawPrimSlow;
    NEMUEXT_HASHMAP MapDrawPrimStrided;
    NEMUEXT_HASHMAP MapDrawPrimFast;
    uint32_t cu32Calls;
} NEMUWINEPROFILE_DRAWPRIM, *PNEMUWINEPROFILE_DRAWPRIM;

#define NEMUWINEPROFILE_DRAWPRIM_RESET_NEXT(_p) do { \
        (_p)->u64LoadLocationTime = 0; \
        (_p)->u64CtxAcquireTime = 0; \
        (_p)->u64PostProcess = 0; \
        NEMUWINEPROFILE_HASHMAP_RESET(&(_p)->MapDrawPrimSlowVs); \
        NEMUWINEPROFILE_HASHMAP_RESET(&(_p)->MapDrawPrimSlow); \
        NEMUWINEPROFILE_HASHMAP_RESET(&(_p)->MapDrawPrimStrided); \
        NEMUWINEPROFILE_HASHMAP_RESET(&(_p)->MapDrawPrimFast); \
    } while (0)

static DECLCALLBACK(uint32_t) nemuWineProfileDrawPrimHashMapHash(void *pvKey)
{
    return (uint32_t)pvKey;
}

static DECLCALLBACK(bool) nemuWineProfileDrawPrimHashMapEqual(void *pvKey1, void *pvKey2)
{
    return ((uint32_t)pvKey1) == ((uint32_t)pvKey2);
}

#define NEMUWINEPROFILE_DRAWPRIM_INIT(_p) do { \
        memset((_p), 0, sizeof (*(_p))); \
        NemuExtHashInit(&(_p)->MapDrawPrimSlowVs, nemuWineProfileDrawPrimHashMapHash, nemuWineProfileDrawPrimHashMapEqual); \
        NemuExtHashInit(&(_p)->MapDrawPrimSlow, nemuWineProfileDrawPrimHashMapHash, nemuWineProfileDrawPrimHashMapEqual); \
        NemuExtHashInit(&(_p)->MapDrawPrimStrided, nemuWineProfileDrawPrimHashMapHash, nemuWineProfileDrawPrimHashMapEqual); \
        NemuExtHashInit(&(_p)->MapDrawPrimFast, nemuWineProfileDrawPrimHashMapHash, nemuWineProfileDrawPrimHashMapEqual); \
    } while (0)

#define NEMUWINEPROFILE_DRAWPRIM_TERM(_p) do { \
        memset((_p), 0, sizeof (*(_p))); \
        NEMUWINEPROFILE_HASHMAP_TERM(&(_p)->MapDrawPrimSlowVs); \
        NEMUWINEPROFILE_HASHMAP_TERM(&(_p)->MapDrawPrimSlow); \
        NEMUWINEPROFILE_HASHMAP_TERM(&(_p)->MapDrawPrimStrided); \
        NEMUWINEPROFILE_HASHMAP_TERM(&(_p)->MapDrawPrimFast); \
    } while (0)
#else
# define PRLOG(_m) do {} while (0)
#endif

#endif /* #ifndef ___NEMUEXT_H__*/
