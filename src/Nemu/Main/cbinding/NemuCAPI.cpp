/* $Id: NemuCAPI.cpp $ */
/** @file NemuCAPI.cpp
 * Utility functions to use with the C API binding.
 */

/*
 * Copyright (C) 2009-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define LOG_GROUP LOG_GROUP_MAIN

#include "NemuCAPI.h"

#ifdef NEMU_WITH_XPCOM
# include <nsMemory.h>
# include <nsIServiceManager.h>
# include <nsEventQueueUtils.h>
# include <nsIExceptionService.h>
# include <stdlib.h>
#endif /* NEMU_WITH_XPCOM */

#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <Nemu/log.h>
#include <Nemu/version.h>

#include "Nemu/com/com.h"
#include "Nemu/com/NativeEventQueue.h"

using namespace std;

/* The following 2 object references should be eliminated once the legacy
 * way to initialize the COM/XPCOM C bindings is removed. */
static ISession            *g_Session           = NULL;
static IVirtualBox         *g_VirtualBox        = NULL;

#ifdef NEMU_WITH_XPCOM
/* This object reference should be eliminated once the legacy way of handling
 * the event queue (XPCOM specific) is removed. */
static nsIEventQueue       *g_EventQueue        = NULL;
#endif /* NEMU_WITH_XPCOM */

static void NemuComUninitialize(void);
static void NemuClientUninitialize(void);

static int
NemuUtf16ToUtf8(CBSTR pwszString, char **ppszString)
{
    if (!pwszString)
    {
        *ppszString = NULL;
        return VINF_SUCCESS;
    }
    return RTUtf16ToUtf8(pwszString, ppszString);
}

static int
NemuUtf8ToUtf16(const char *pszString, BSTR *ppwszString)
{
    if (!pszString)
    {
        *ppwszString = NULL;
        return VINF_SUCCESS;
    }
#ifdef NEMU_WITH_XPCOM
    return RTStrToUtf16(pszString, ppwszString);
#else /* !NEMU_WITH_XPCOM */
    PRTUTF16 pwsz;
    int vrc = RTStrToUtf16(pszString, &pwsz);
    *ppwszString = ::SysAllocString(pwsz);
    RTUtf16Free(pwsz);
    return vrc;
#endif /* !NEMU_WITH_XPCOM */
}

static void
NemuUtf8Clear(char *pszString)
{
    RT_BZERO(pszString, strlen(pszString));
}

static void
NemuUtf16Clear(BSTR pwszString)
{
    RT_BZERO(pwszString, RTUtf16Len(pwszString) * sizeof(RTUTF16));
}

static void
NemuUtf16Free(BSTR pwszString)
{
#ifdef NEMU_WITH_XPCOM
    RTUtf16Free(pwszString);
#else /* !NEMU_WITH_XPCOM */
    ::SysFreeString(pwszString);
#endif /* !NEMU_WITH_XPCOM */
}

static void
NemuUtf8Free(char *pszString)
{
    RTStrFree(pszString);
}

static void
NemuComUnallocString(BSTR pwsz)
{
    if (pwsz)
    {
#ifdef NEMU_WITH_XPCOM
        nsMemory::Free(pwsz);
#else /* !NEMU_WITH_XPCOM */
        ::SysFreeString(pwsz);
#endif /* !NEMU_WITH_XPCOM */
    }
}

static void
NemuComUnallocMem(void *pv)
{
    NemuComUnallocString((BSTR)pv);
}

static ULONG
NemuVTElemSize(VARTYPE vt)
{
    switch (vt)
    {
        case VT_BOOL:
        case VT_I1:
        case VT_UI1:
            return 1;
        case VT_I2:
        case VT_UI2:
            return 2;
        case VT_I4:
        case VT_UI4:
        case VT_HRESULT:
            return 4;
        case VT_I8:
        case VT_UI8:
            return 8;
        case VT_BSTR:
        case VT_DISPATCH:
        case VT_UNKNOWN:
            return sizeof(void *);
        default:
            return 0;
    }
}

static SAFEARRAY *
NemuSafeArrayCreateVector(VARTYPE vt, LONG lLbound, ULONG cElements)
{
#ifdef NEMU_WITH_XPCOM
    NOREF(lLbound);
    ULONG cbElement = NemuVTElemSize(vt);
    if (!cbElement)
        return NULL;
    SAFEARRAY *psa = (SAFEARRAY *)RTMemAllocZ(sizeof(SAFEARRAY));
    if (!psa)
        return psa;
    if (cElements)
    {
        void *pv = nsMemory::Alloc(cElements * cbElement);
        if (!pv)
        {
            RTMemFree(psa);
            return NULL;
        }
        psa->pv = pv;
        psa->c = cElements;
    }
    return psa;
#else /* !NEMU_WITH_XPCOM */
    return SafeArrayCreateVector(vt, lLbound, cElements);
#endif /* !NEMU_WITH_XPCOM */
}

static SAFEARRAY *
NemuSafeArrayOutParamAlloc(void)
{
#ifdef NEMU_WITH_XPCOM
    return (SAFEARRAY *)RTMemAllocZ(sizeof(SAFEARRAY));
#else /* !NEMU_WITH_XPCOM */
    return NULL;
#endif /* !NEMU_WITH_XPCOM */
}

static HRESULT
NemuSafeArrayDestroy(SAFEARRAY *psa)
{
#ifdef NEMU_WITH_XPCOM
    if (psa)
    {
        if (psa->pv)
            nsMemory::Free(psa->pv);
        RTMemFree(psa);
    }
    return S_OK;
#else /* !NEMU_WITH_XPCOM */
    VARTYPE vt = VT_UNKNOWN;
    HRESULT rc = SafeArrayGetVartype(psa, &vt);
    if (FAILED(rc))
        return rc;
    if (vt == VT_BSTR)
    {
        /* Special treatment: strings are to be freed explicitly, see sample
         * C binding code, so zap it here. No way to reach compatible code
         * behavior between COM and XPCOM without this kind of trickery. */
        void *pData;
        rc = SafeArrayAccessData(psa, &pData);
        if (FAILED(rc))
            return rc;
        ULONG cbElement = NemuVTElemSize(vt);
        if (!cbElement)
            return E_INVALIDARG;
        Assert(cbElement = psa->cbElements);
        ULONG cElements = psa->rgsabound[0].cElements;
        memset(pData, '\0', cbElement * cElements);
        SafeArrayUnaccessData(psa);
    }
    return SafeArrayDestroy(psa);
#endif /* !NEMU_WITH_XPCOM */
}

static HRESULT
NemuSafeArrayCopyInParamHelper(SAFEARRAY *psa, const void *pv, ULONG cb)
{
    if (!pv || !psa)
        return E_POINTER;
    if (!cb)
        return S_OK;

    void *pData;
#ifdef NEMU_WITH_XPCOM
    pData = psa->pv;
#else /* !NEMU_WITH_XPCOM */
    HRESULT rc = SafeArrayAccessData(psa, &pData);
    if (FAILED(rc))
        return rc;
#endif /* !NEMU_WITH_XPCOM */
    memcpy(pData, pv, cb);
#ifndef NEMU_WITH_XPCOM
    SafeArrayUnaccessData(psa);
#endif /* !NEMU_WITH_XPCOM */
    return S_OK;
}

static HRESULT
NemuSafeArrayCopyOutParamHelper(void **ppv, ULONG *pcb, VARTYPE vt, SAFEARRAY *psa)
{
    if (!ppv)
        return E_POINTER;
    ULONG cbElement = NemuVTElemSize(vt);
    if (!cbElement)
    {
        *ppv = NULL;
        if (pcb)
            *pcb = 0;
        return E_INVALIDARG;
    }
#ifndef NEMU_WITH_XPCOM
    if (psa->cDims != 1)
    {
        *ppv = NULL;
        if (pcb)
            *pcb = 0;
        return E_INVALIDARG;
    }
    Assert(cbElement = psa->cbElements);
#endif /* !NEMU_WITH_XPCOM */
    void *pData;
    ULONG cElements;
#ifdef NEMU_WITH_XPCOM
    pData = psa->pv;
    cElements = psa->c;
#else /* !NEMU_WITH_XPCOM */
    HRESULT rc = SafeArrayAccessData(psa, &pData);
    if (FAILED(rc))
    {
        *ppv = NULL;
        if (pcb)
            *pcb = 0;
        return rc;
    }
    cElements = psa->rgsabound[0].cElements;
#endif /* !NEMU_WITH_XPCOM */
    size_t cbTotal = cbElement * cElements;
    void *pv = NULL;
    if (cbTotal)
    {
        pv = malloc(cbTotal);
        if (!pv)
        {
            *ppv = NULL;
            if (pcb)
                *pcb = 0;
            return E_OUTOFMEMORY;
        }
        else
            memcpy(pv, pData, cbTotal);
    }
    *ppv = pv;
    if (pcb)
        *pcb = (ULONG)cbTotal;
#ifndef NEMU_WITH_XPCOM
    SafeArrayUnaccessData(psa);
#endif /* !NEMU_WITH_XPCOM */
    return S_OK;
}

static HRESULT
NemuSafeArrayCopyOutIfaceParamHelper(IUnknown ***ppaObj, ULONG *pcObj, SAFEARRAY *psa)
{
    ULONG mypcb;
    HRESULT rc = NemuSafeArrayCopyOutParamHelper((void **)ppaObj, &mypcb, VT_UNKNOWN, psa);
    if (FAILED(rc))
    {
        if (pcObj)
            *pcObj = 0;
        return rc;
    }
    ULONG cElements = mypcb / sizeof(void *);
    if (pcObj)
        *pcObj = cElements;
#ifndef NEMU_WITH_XPCOM
    /* Do this only for COM, as there the SAFEARRAY destruction will release
     * the contained references automatically. XPCOM doesn't do that, which
     * means that copying implicitly transfers ownership. */
    IUnknown **paObj = *ppaObj;
    for (ULONG i = 0; i < cElements; i++)
    {
        IUnknown *pObj = paObj[i];
        if (pObj)
            pObj->AddRef();
    }
#endif /* NEMU_WITH_XPCOM */
    return S_OK;
}

static HRESULT
NemuArrayOutFree(void *pv)
{
    free(pv);
    return S_OK;
}

static void
NemuComInitialize(const char *pszVirtualBoxIID, IVirtualBox **ppVirtualBox,
                  const char *pszSessionIID, ISession **ppSession)
{
    int vrc;
    IID virtualBoxIID;
    IID sessionIID;

    *ppSession    = NULL;
    *ppVirtualBox = NULL;

    /* convert the string representation of the UUIDs (if provided) to IID */
    if (pszVirtualBoxIID && *pszVirtualBoxIID)
    {
        vrc = ::RTUuidFromStr((RTUUID *)&virtualBoxIID, pszVirtualBoxIID);
        if (RT_FAILURE(vrc))
            return;
    }
    else
        virtualBoxIID = IID_IVirtualBox;
    if (pszSessionIID && *pszSessionIID)
    {
        vrc = ::RTUuidFromStr((RTUUID *)&sessionIID, pszSessionIID);
        if (RT_FAILURE(vrc))
            return;
    }
    else
        sessionIID = IID_ISession;

    HRESULT rc = com::Initialize();
    if (FAILED(rc))
    {
        Log(("Cbinding: COM/XPCOM could not be initialized! rc=%Rhrc\n", rc));
        NemuComUninitialize();
        return;
    }

#ifdef NEMU_WITH_XPCOM
    rc = NS_GetMainEventQ(&g_EventQueue);
    if (FAILED(rc))
    {
        Log(("Cbinding: Could not get XPCOM event queue! rc=%Rhrc\n", rc));
        NemuComUninitialize();
        return;
    }
#endif /* NEMU_WITH_XPCOM */

#ifdef NEMU_WITH_XPCOM
    nsIComponentManager *pManager;
    rc = NS_GetComponentManager(&pManager);
    if (FAILED(rc))
    {
        Log(("Cbinding: Could not get component manager! rc=%Rhrc\n", rc));
        NemuComUninitialize();
        return;
    }

    rc = pManager->CreateInstanceByContractID(NS_VIRTUALBOX_CONTRACTID,
                                              nsnull,
                                              virtualBoxIID,
                                              (void **)&g_VirtualBox);
#else /* !NEMU_WITH_XPCOM */
    rc = CoCreateInstance(CLSID_VirtualBox, NULL, CLSCTX_LOCAL_SERVER, virtualBoxIID, (void **)&g_VirtualBox);
#endif /* !NEMU_WITH_XPCOM */
    if (FAILED(rc))
    {
        Log(("Cbinding: Could not instantiate VirtualBox object! rc=%Rhrc\n",rc));
#ifdef NEMU_WITH_XPCOM
        pManager->Release();
        pManager = NULL;
#endif /* NEMU_WITH_XPCOM */
        NemuComUninitialize();
        return;
    }

    Log(("Cbinding: IVirtualBox object created.\n"));

#ifdef NEMU_WITH_XPCOM
    rc = pManager->CreateInstanceByContractID(NS_SESSION_CONTRACTID,
                                              nsnull,
                                              sessionIID,
                                              (void **)&g_Session);
#else /* !NEMU_WITH_XPCOM */
    rc = CoCreateInstance(CLSID_Session, NULL, CLSCTX_INPROC_SERVER, sessionIID, (void **)&g_Session);
#endif /* !NEMU_WITH_XPCOM */
    if (FAILED(rc))
    {
        Log(("Cbinding: Could not instantiate Session object! rc=%Rhrc\n",rc));
#ifdef NEMU_WITH_XPCOM
        pManager->Release();
        pManager = NULL;
#endif /* NEMU_WITH_XPCOM */
        NemuComUninitialize();
        return;
    }

    Log(("Cbinding: ISession object created.\n"));

#ifdef NEMU_WITH_XPCOM
    pManager->Release();
    pManager = NULL;
#endif /* NEMU_WITH_XPCOM */

    *ppSession = g_Session;
    *ppVirtualBox = g_VirtualBox;
}

static void
NemuComInitializeV1(IVirtualBox **ppVirtualBox, ISession **ppSession)
{
    NemuComInitialize(NULL, ppVirtualBox, NULL, ppSession);
}

static void
NemuComUninitialize(void)
{
    if (g_Session)
    {
        g_Session->Release();
        g_Session = NULL;
    }
    if (g_VirtualBox)
    {
        g_VirtualBox->Release();
        g_VirtualBox = NULL;
    }
#ifdef NEMU_WITH_XPCOM
    if (g_EventQueue)
    {
        g_EventQueue->Release();
        g_EventQueue = NULL;
    }
#endif /* NEMU_WITH_XPCOM */
    com::Shutdown();
    Log(("Cbinding: Cleaned up the created objects.\n"));
}

#ifdef NEMU_WITH_XPCOM
static void
NemuGetEventQueue(nsIEventQueue **ppEventQueue)
{
    *ppEventQueue = g_EventQueue;
}
#endif /* NEMU_WITH_XPCOM */

static int
NemuProcessEventQueue(LONG64 iTimeoutMS)
{
    RTMSINTERVAL iTimeout;
    if (iTimeoutMS < 0 || iTimeoutMS > UINT32_MAX)
        iTimeout = RT_INDEFINITE_WAIT;
    else
        iTimeout = (RTMSINTERVAL)iTimeoutMS;
    int vrc = com::NativeEventQueue::getMainEventQueue()->processEventQueue(iTimeout);
    switch (vrc)
    {
        case VINF_SUCCESS:
            return 0;
        case VINF_INTERRUPTED:
            return 1;
        case VERR_INTERRUPTED:
            return 2;
        case VERR_TIMEOUT:
            return 3;
        case VERR_INVALID_CONTEXT:
            return 4;
        default:
            return 5;
    }
}

static int
NemuInterruptEventQueueProcessing(void)
{
    com::NativeEventQueue::getMainEventQueue()->interruptEventQueueProcessing();
    return 0;
}

static HRESULT
NemuGetException(IErrorInfo **ppException)
{
    HRESULT rc;

    *ppException = NULL;

#ifdef NEMU_WITH_XPCOM
    nsIServiceManager *mgr = NULL;
    rc = NS_GetServiceManager(&mgr);
    if (FAILED(rc) || !mgr)
        return rc;

    IID esid = NS_IEXCEPTIONSERVICE_IID;
    nsIExceptionService *es = NULL;
    rc = mgr->GetServiceByContractID(NS_EXCEPTIONSERVICE_CONTRACTID, esid, (void **)&es);
    if (FAILED(rc) || !es)
    {
        mgr->Release();
        return rc;
    }

    nsIExceptionManager *em;
    rc = es->GetCurrentExceptionManager(&em);
    if (FAILED(rc) || !em)
    {
        es->Release();
        mgr->Release();
        return rc;
    }

    nsIException *ex;
    rc = em->GetCurrentException(&ex);
    if (FAILED(rc))
    {
        em->Release();
        es->Release();
        mgr->Release();
        return rc;
    }

    *ppException = ex;
    em->Release();
    es->Release();
    mgr->Release();
#else /* !NEMU_WITH_XPCOM */
    IErrorInfo *ex;
    rc = ::GetErrorInfo(0, &ex);
    if (FAILED(rc))
        return rc;

    *ppException = ex;
#endif /* !NEMU_WITH_XPCOM */

    return rc;
}

static HRESULT
NemuClearException(void)
{
    HRESULT rc;

#ifdef NEMU_WITH_XPCOM
    nsIServiceManager *mgr = NULL;
    rc = NS_GetServiceManager(&mgr);
    if (FAILED(rc) || !mgr)
        return rc;

    IID esid = NS_IEXCEPTIONSERVICE_IID;
    nsIExceptionService *es = NULL;
    rc = mgr->GetServiceByContractID(NS_EXCEPTIONSERVICE_CONTRACTID, esid, (void **)&es);
    if (FAILED(rc) || !es)
    {
        mgr->Release();
        return rc;
    }

    nsIExceptionManager *em;
    rc = es->GetCurrentExceptionManager(&em);
    if (FAILED(rc) || !em)
    {
        es->Release();
        mgr->Release();
        return rc;
    }

    rc = em->SetCurrentException(NULL);
    em->Release();
    es->Release();
    mgr->Release();
#else /* !NEMU_WITH_XPCOM */
    rc = ::SetErrorInfo(0, NULL);
#endif /* !NEMU_WITH_XPCOM */

    return rc;
}

static HRESULT
NemuClientInitialize(const char *pszVirtualBoxClientIID, IVirtualBoxClient **ppVirtualBoxClient)
{
    IID virtualBoxClientIID;

    *ppVirtualBoxClient = NULL;

    /* convert the string representation of UUID to IID type */
    if (pszVirtualBoxClientIID && *pszVirtualBoxClientIID)
    {
        int vrc = ::RTUuidFromStr((RTUUID *)&virtualBoxClientIID, pszVirtualBoxClientIID);
        if (RT_FAILURE(vrc))
            return E_INVALIDARG;
    }
    else
        virtualBoxClientIID = IID_IVirtualBoxClient;

    HRESULT rc = com::Initialize();
    if (FAILED(rc))
    {
        Log(("Cbinding: COM/XPCOM could not be initialized! rc=%Rhrc\n", rc));
        NemuClientUninitialize();
        return rc;
    }

#ifdef NEMU_WITH_XPCOM
    rc = NS_GetMainEventQ(&g_EventQueue);
    if (NS_FAILED(rc))
    {
        Log(("Cbinding: Could not get XPCOM event queue! rc=%Rhrc\n", rc));
        NemuClientUninitialize();
        return rc;
    }
#endif /* NEMU_WITH_XPCOM */

#ifdef NEMU_WITH_XPCOM
    nsIComponentManager *pManager;
    rc = NS_GetComponentManager(&pManager);
    if (FAILED(rc))
    {
        Log(("Cbinding: Could not get component manager! rc=%Rhrc\n", rc));
        NemuClientUninitialize();
        return rc;
    }

    rc = pManager->CreateInstanceByContractID(NS_VIRTUALBOXCLIENT_CONTRACTID,
                                              nsnull,
                                              virtualBoxClientIID,
                                              (void **)ppVirtualBoxClient);
#else /* !NEMU_WITH_XPCOM */
    rc = CoCreateInstance(CLSID_VirtualBoxClient, NULL, CLSCTX_INPROC_SERVER, virtualBoxClientIID, (void **)ppVirtualBoxClient);
#endif /* !NEMU_WITH_XPCOM */
    if (FAILED(rc))
    {
        Log(("Cbinding: Could not instantiate VirtualBoxClient object! rc=%Rhrc\n",rc));
#ifdef NEMU_WITH_XPCOM
        pManager->Release();
        pManager = NULL;
#endif /* NEMU_WITH_XPCOM */
        NemuClientUninitialize();
        return rc;
    }

#ifdef NEMU_WITH_XPCOM
    pManager->Release();
    pManager = NULL;
#endif /* NEMU_WITH_XPCOM */

    Log(("Cbinding: IVirtualBoxClient object created.\n"));

    return S_OK;
}

static HRESULT
NemuClientThreadInitialize(void)
{
    return com::Initialize();
}

static HRESULT
NemuClientThreadUninitialize(void)
{
    return com::Shutdown();
}

static void
NemuClientUninitialize(void)
{
#ifdef NEMU_WITH_XPCOM
    if (g_EventQueue)
    {
        NS_RELEASE(g_EventQueue);
        g_EventQueue = NULL;
    }
#endif /* NEMU_WITH_XPCOM */
    com::Shutdown();
    Log(("Cbinding: Cleaned up the created objects.\n"));
}

static unsigned int
NemuVersion(void)
{
    return NEMU_VERSION_MAJOR * 1000 * 1000 + NEMU_VERSION_MINOR * 1000 + NEMU_VERSION_BUILD;
}

static unsigned int
NemuAPIVersion(void)
{
    return NEMU_VERSION_MAJOR * 1000 + NEMU_VERSION_MINOR + (NEMU_VERSION_BUILD > 50 ? 1 : 0);
}

NEMUCAPI_DECL(PCNEMUCAPI)
NemuGetCAPIFunctions(unsigned uVersion)
{
    /* This is the first piece of code which knows that IPRT exists, so
     * initialize it properly. The limited initialization in NemuC is not
     * sufficient, and causes trouble with com::Initialize() misbehaving. */
    RTR3InitDll(0);

    /*
     * The current interface version.
     */
    static const NEMUCAPI s_Functions =
    {
        sizeof(NEMUCAPI),
        NEMU_CAPI_VERSION,

        NemuVersion,
        NemuAPIVersion,

        NemuClientInitialize,
        NemuClientThreadInitialize,
        NemuClientThreadUninitialize,
        NemuClientUninitialize,

        NemuComInitialize,
        NemuComUninitialize,

        NemuComUnallocString,

        NemuUtf16ToUtf8,
        NemuUtf8ToUtf16,
        NemuUtf8Free,
        NemuUtf16Free,

        NemuSafeArrayCreateVector,
        NemuSafeArrayOutParamAlloc,
        NemuSafeArrayCopyInParamHelper,
        NemuSafeArrayCopyOutParamHelper,
        NemuSafeArrayCopyOutIfaceParamHelper,
        NemuSafeArrayDestroy,
        NemuArrayOutFree,

#ifdef NEMU_WITH_XPCOM
        NemuGetEventQueue,
#endif /* NEMU_WITH_XPCOM */
        NemuGetException,
        NemuClearException,
        NemuProcessEventQueue,
        NemuInterruptEventQueueProcessing,

        NemuUtf8Clear,
        NemuUtf16Clear,

        NEMU_CAPI_VERSION
    };

    if ((uVersion & 0xffff0000U) == (NEMU_CAPI_VERSION & 0xffff0000U))
        return &s_Functions;

    /*
     * Legacy interface version 3.0.
     */
    static const struct NEMUCAPIV3
    {
        /** The size of the structure. */
        unsigned cb;
        /** The structure version. */
        unsigned uVersion;

        unsigned int (*pfnGetVersion)(void);

        unsigned int (*pfnGetAPIVersion)(void);

        HRESULT (*pfnClientInitialize)(const char *pszVirtualBoxClientIID,
                                       IVirtualBoxClient **ppVirtualBoxClient);
        void (*pfnClientUninitialize)(void);

        void (*pfnComInitialize)(const char *pszVirtualBoxIID,
                                 IVirtualBox **ppVirtualBox,
                                 const char *pszSessionIID,
                                 ISession **ppSession);

        void (*pfnComUninitialize)(void);

        void (*pfnComUnallocMem)(void *pv);

        int (*pfnUtf16ToUtf8)(CBSTR pwszString, char **ppszString);
        int (*pfnUtf8ToUtf16)(const char *pszString, BSTR *ppwszString);
        void (*pfnUtf8Free)(char *pszString);
        void (*pfnUtf16Free)(BSTR pwszString);

#ifdef NEMU_WITH_XPCOM
        void (*pfnGetEventQueue)(nsIEventQueue **ppEventQueue);
#endif /* NEMU_WITH_XPCOM */
        HRESULT (*pfnGetException)(IErrorInfo **ppException);
        HRESULT (*pfnClearException)(void);

        /** Tail version, same as uVersion. */
        unsigned uEndVersion;
    } s_Functions_v3_0 =
    {
        sizeof(s_Functions_v3_0),
        0x00030000U,

        NemuVersion,
        NemuAPIVersion,

        NemuClientInitialize,
        NemuClientUninitialize,

        NemuComInitialize,
        NemuComUninitialize,

        NemuComUnallocMem,

        NemuUtf16ToUtf8,
        NemuUtf8ToUtf16,
        NemuUtf8Free,
        NemuUtf16Free,

#ifdef NEMU_WITH_XPCOM
        NemuGetEventQueue,
#endif /* NEMU_WITH_XPCOM */
        NemuGetException,
        NemuClearException,

        0x00030000U
    };

    if ((uVersion & 0xffff0000U) == 0x00030000U)
        return (PCNEMUCAPI)&s_Functions_v3_0;

    /*
     * Legacy interface version 2.0.
     */
    static const struct NEMUCAPIV2
    {
        /** The size of the structure. */
        unsigned cb;
        /** The structure version. */
        unsigned uVersion;

        unsigned int (*pfnGetVersion)(void);

        void (*pfnComInitialize)(const char *pszVirtualBoxIID,
                                 IVirtualBox **ppVirtualBox,
                                 const char *pszSessionIID,
                                 ISession **ppSession);

        void (*pfnComUninitialize)(void);

        void (*pfnComUnallocMem)(void *pv);
        void (*pfnUtf16Free)(BSTR pwszString);
        void (*pfnUtf8Free)(char *pszString);

        int (*pfnUtf16ToUtf8)(CBSTR pwszString, char **ppszString);
        int (*pfnUtf8ToUtf16)(const char *pszString, BSTR *ppwszString);

#ifdef NEMU_WITH_XPCOM
        void (*pfnGetEventQueue)(nsIEventQueue **ppEventQueue);
#endif /* NEMU_WITH_XPCOM */

        /** Tail version, same as uVersion. */
        unsigned uEndVersion;
    } s_Functions_v2_0 =
    {
        sizeof(s_Functions_v2_0),
        0x00020000U,

        NemuVersion,

        NemuComInitialize,
        NemuComUninitialize,

        NemuComUnallocMem,
        NemuUtf16Free,
        NemuUtf8Free,

        NemuUtf16ToUtf8,
        NemuUtf8ToUtf16,

#ifdef NEMU_WITH_XPCOM
        NemuGetEventQueue,
#endif /* NEMU_WITH_XPCOM */

        0x00020000U
    };

    if ((uVersion & 0xffff0000U) == 0x00020000U)
        return (PCNEMUCAPI)&s_Functions_v2_0;

    /*
     * Legacy interface version 1.0.
     */
    static const struct NEMUCAPIV1
    {
        /** The size of the structure. */
        unsigned cb;
        /** The structure version. */
        unsigned uVersion;

        unsigned int (*pfnGetVersion)(void);

        void (*pfnComInitialize)(IVirtualBox **virtualBox, ISession **session);
        void (*pfnComUninitialize)(void);

        void (*pfnComUnallocMem)(void *pv);
        void (*pfnUtf16Free)(BSTR pwszString);
        void (*pfnUtf8Free)(char *pszString);

        int (*pfnUtf16ToUtf8)(CBSTR pwszString, char **ppszString);
        int (*pfnUtf8ToUtf16)(const char *pszString, BSTR *ppwszString);

        /** Tail version, same as uVersion. */
        unsigned uEndVersion;
    } s_Functions_v1_0 =
    {
        sizeof(s_Functions_v1_0),
        0x00010000U,

        NemuVersion,

        NemuComInitializeV1,
        NemuComUninitialize,

        NemuComUnallocMem,
        NemuUtf16Free,
        NemuUtf8Free,

        NemuUtf16ToUtf8,
        NemuUtf8ToUtf16,

        0x00010000U
    };

    if ((uVersion & 0xffff0000U) == 0x00010000U)
        return (PCNEMUCAPI)&s_Functions_v1_0;

    /*
     * Unsupported interface version.
     */
    return NULL;
}

#ifdef NEMU_WITH_XPCOM
NEMUCAPI_DECL(PCNEMUCAPI)
NemuGetXPCOMCFunctions(unsigned uVersion)
{
    return NemuGetCAPIFunctions(uVersion);
}
#endif /* NEMU_WITH_XPCOM */
/* vim: set ts=4 sw=4 et: */
