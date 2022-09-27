/* $Id: NemuCredentialProvider.cpp $ */
/** @file
 * NemuCredentialProvider - Main file of the VirtualBox Credential Provider.
 */

/*
 * Copyright (C) 2012-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <windows.h>
#include <initguid.h>
#include <new> /* For bad_alloc. */

#ifdef NEMU_WITH_SENS
# include <eventsys.h>
# include <sens.h>
# include <Sensevts.h>
#endif

#include <iprt/buildconfig.h>
#include <iprt/initterm.h>
#ifdef NEMU_WITH_SENS
# include <iprt/string.h>
#endif
#include <Nemu/NemuGuestLib.h>

#include "NemuCredentialProvider.h"
#include "NemuCredProvFactory.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static LONG g_cDllRefs  = 0;                 /**< Global DLL reference count. */
static HINSTANCE g_hDllInst = NULL;          /**< Global DLL hInstance. */

#ifdef NEMU_WITH_SENS
static bool g_fSENSEnabled = false;
static IEventSystem *g_pIEventSystem = NULL; /**< Pointer to IEventSystem interface. */

/**
 * Subscribed SENS events.
 */
static struct NEMUCREDPROVSENSEVENTS
{
    /** The actual method name the subscription is for. */
    char *pszMethod;
    /** A friendly name for the subscription. */
    char *pszSubscriptionName;
    /** The actual subscription UUID.
     *  Should not be changed. */
    char *pszSubscriptionUUID;
} g_aSENSEvents[] = {
    { "Logon",             "NemuCredProv SENS Logon",             "{561D0791-47C0-4BC3-87C0-CDC2621EA653}" },
    { "Logoff",            "NemuCredProv SENS Logoff",            "{12B618B1-F2E0-4390-BADA-7EB1DC31A70A}" },
    { "StartShell",        "NemuCredProv SENS StartShell",        "{5941931D-015A-4F91-98DA-81AAE262D090}" },
    { "DisplayLock",       "NemuCredProv SENS DisplayLock",       "{B7E2C510-501A-4961-938F-A458970930D7}" },
    { "DisplayUnlock",     "NemuCredProv SENS DisplayUnlock",     "{11305987-8FFC-41AD-A264-991BD5B7488A}" },
    { "StartScreenSaver",  "NemuCredProv SENS StartScreenSaver",  "{6E2D26DF-0095-4EC4-AE00-2395F09AF7F2}" },
    { "StopScreenSaver",   "NemuCredProv SENS StopScreenSaver",   "{F53426BC-412F-41E8-9A5F-E5FA8A164BD6}" }
};

/**
 * Implementation of the ISensLogon interface for getting
 * SENS (System Event Notification Service) events. SENS must be up
 * and running on this OS!
 */
interface NemuCredProvSensLogon : public ISensLogon
{
public:

    NemuCredProvSensLogon(void) :
        m_cRefs(1)
    {
    }

    STDMETHODIMP QueryInterface(REFIID interfaceID, void **ppvInterface)
    {
        if (   IsEqualIID(interfaceID, IID_IUnknown)
            || IsEqualIID(interfaceID, IID_IDispatch)
            || IsEqualIID(interfaceID, IID_ISensLogon))
        {
            *ppvInterface = this;
            AddRef();
            return S_OK;
        }

        *ppvInterface = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef(void)
    {
        return InterlockedIncrement(&m_cRefs);
    }

    ULONG STDMETHODCALLTYPE Release(void)
    {
        ULONG ulTemp = InterlockedDecrement(&m_cRefs);
        return ulTemp;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(unsigned int FAR* pctinfo)
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfo(unsigned int iTInfo, LCID lcid, ITypeInfo FAR* FAR* ppTInfo)
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID riid,
                                            OLECHAR FAR* FAR* rgszNames, unsigned int cNames,
                                            LCID lcid, DISPID FAR* rgDispId)
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
                                     DISPPARAMS FAR* pDispParams, VARIANT FAR* parResult, EXCEPINFO FAR* pExcepInfo,
                                     unsigned int FAR* puArgErr)
    {
        return E_NOTIMPL;
    }

    /* ISensLogon methods */
    STDMETHODIMP Logon(BSTR bstrUserName)
    {
        NemuCredProvVerbose(0, "NemuCredProvSensLogon: Logon\n");
        return S_OK;
    }

    STDMETHODIMP Logoff(BSTR bstrUserName)
    {
        NemuCredProvVerbose(0, "NemuCredProvSensLogon: Logoff\n");
        return S_OK;
    }

    STDMETHODIMP StartShell(BSTR bstrUserName)
    {
        NemuCredProvVerbose(0, "NemuCredProvSensLogon: Logon\n");
        return S_OK;
    }

    STDMETHODIMP DisplayLock(BSTR bstrUserName)
    {
        NemuCredProvVerbose(0, "NemuCredProvSensLogon: DisplayLock\n");
        return S_OK;
    }

    STDMETHODIMP DisplayUnlock(BSTR bstrUserName)
    {
        NemuCredProvVerbose(0, "NemuCredProvSensLogon: DisplayUnlock\n");
        return S_OK;
    }

    STDMETHODIMP StartScreenSaver(BSTR bstrUserName)
    {
        NemuCredProvVerbose(0, "NemuCredProvSensLogon: StartScreenSaver\n");
        return S_OK;
    }

    STDMETHODIMP StopScreenSaver(BSTR bstrUserName)
    {
        NemuCredProvVerbose(0, "NemuCredProvSensLogon: StopScreenSaver\n");
        return S_OK;
    }

protected:

    LONG m_cRefs;
};
static NemuCredProvSensLogon *g_pISensLogon = NULL;


/**
 * Register events to be called by SENS.
 *
 * @return  HRESULT
 */
static HRESULT NemuCredentialProviderRegisterSENS(void)
{
    NemuCredProvVerbose(0, "NemuCredentialProviderRegisterSENS\n");

    HRESULT hr = CoCreateInstance(CLSID_CEventSystem, 0, CLSCTX_SERVER, IID_IEventSystem, (void**)&g_pIEventSystem);
    if (FAILED(hr))
    {
        NemuCredProvVerbose(0, "NemuCredentialProviderRegisterSENS: Could not connect to CEventSystem, hr=%Rhrc\n",
                            hr);
        return hr;
    }

    try
    {
        g_pISensLogon = new NemuCredProvSensLogon();
        AssertPtr(g_pISensLogon);
    }
    catch (std::bad_alloc &ex)
    {
        NOREF(ex);
        hr = E_OUTOFMEMORY;
    }

    if (   SUCCEEDED(hr)
        && g_pIEventSystem)
    {
        IEventSubscription *pIEventSubscription;
        int i;
        for (i = 0; i < RT_ELEMENTS(g_aSENSEvents); i++)
        {
            NemuCredProvVerbose(0, "NemuCredProv: Registering \"%s\" (%s) ...\n",
                                g_aSENSEvents[i].pszMethod, g_aSENSEvents[i].pszSubscriptionName);

            hr = CoCreateInstance(CLSID_CEventSubscription, 0, CLSCTX_SERVER, IID_IEventSubscription, (LPVOID*)&pIEventSubscription);
            if (FAILED(hr))
                continue;

            hr = pIEventSubscription->put_EventClassID(L"{d5978630-5b9f-11d1-8dd2-00aa004abd5e}" /* SENSGUID_EVENTCLASS_LOGON */);
            if (FAILED(hr))
                break;

            hr = pIEventSubscription->put_SubscriberInterface((IUnknown*)g_pISensLogon);
            if (FAILED(hr))
                break;

            PRTUTF16 pwszTemp;
            int rc = RTStrToUtf16(g_aSENSEvents[i].pszMethod, &pwszTemp);
            if (RT_SUCCESS(rc))
            {
                hr = pIEventSubscription->put_MethodName(pwszTemp);
                RTUtf16Free(pwszTemp);
            }
            else
                hr = ERROR_OUTOFMEMORY;
            if (FAILED(hr))
                break;

            rc = RTStrToUtf16(g_aSENSEvents[i].pszSubscriptionName, &pwszTemp);
            if (RT_SUCCESS(rc))
            {
                hr = pIEventSubscription->put_SubscriptionName(pwszTemp);
                RTUtf16Free(pwszTemp);
            }
            else
                hr = ERROR_OUTOFMEMORY;
            if (FAILED(hr))
                break;

            rc = RTStrToUtf16(g_aSENSEvents[i].pszSubscriptionUUID, &pwszTemp);
            if (RT_SUCCESS(rc))
            {
                hr = pIEventSubscription->put_SubscriptionID(pwszTemp);
                RTUtf16Free(pwszTemp);
            }
            else
                hr = ERROR_OUTOFMEMORY;
            if (FAILED(hr))
                break;

            hr = pIEventSubscription->put_PerUser(TRUE);
            if (FAILED(hr))
                break;

            hr = g_pIEventSystem->Store(PROGID_EventSubscription, (IUnknown*)pIEventSubscription);
            if (FAILED(hr))
                break;

            pIEventSubscription->Release();
            pIEventSubscription = NULL;
        }

        if (FAILED(hr))
            NemuCredProvVerbose(0, "NemuCredentialProviderRegisterSENS: Could not register \"%s\" (%s), hr=%Rhrc\n",
                                g_aSENSEvents[i].pszMethod, g_aSENSEvents[i].pszSubscriptionName, hr);

        if (pIEventSubscription != NULL)
            pIEventSubscription->Release();
    }

    if (FAILED(hr))
    {
        NemuCredProvVerbose(0, "NemuCredentialProviderRegisterSENS: Error registering SENS provider, hr=%Rhrc\n", hr);
        if (g_pIEventSystem)
        {
            g_pIEventSystem->Release();
            g_pIEventSystem = NULL;
        }
    }

    NemuCredProvVerbose(0, "NemuCredentialProviderRegisterSENS: Returning hr=%Rhrc\n", hr);
    return hr;
}

/**
 * Unregisters registered SENS events.
 */
static void NemuCredentialProviderUnregisterSENS(void)
{
    if (g_pIEventSystem)
    {
        g_pIEventSystem->Release();
        g_pIEventSystem = NULL;
    }

    /* We need to reconnecto to the event system because we can be called
     * in a different context COM can't handle. */
    HRESULT hr = CoCreateInstance(CLSID_CEventSystem, 0,
                                  CLSCTX_SERVER, IID_IEventSystem, (void**)&g_pIEventSystem);
    if (   SUCCEEDED(hr)
        && g_pIEventSystem)
    {
        NemuCredProvVerbose(0, "NemuCredentialProviderUnregisterSENS\n");

        HRESULT hr;

        for (int i = 0; i < RT_ELEMENTS(g_aSENSEvents); i++)
        {
            int iErrorIdX;

            char *pszSubToRemove;
            if (!RTStrAPrintf(&pszSubToRemove, "SubscriptionID=%s",
                              g_aSENSEvents[i].pszSubscriptionUUID))
            {
                continue; /* Keep going. */
            }

            PRTUTF16 pwszTemp;
            int rc2 = RTStrToUtf16(pszSubToRemove, &pwszTemp);
            if (RT_SUCCESS(rc2))
            {
                hr = g_pIEventSystem->Remove(PROGID_EventSubscription, pwszTemp,
                                             &iErrorIdX);
                RTUtf16Free(pwszTemp);
            }
            else
                hr = ERROR_OUTOFMEMORY;

            if (FAILED(hr))
                NemuCredProvVerbose(0, "NemuCredentialProviderUnregisterSENS: Could not unregister \"%s\" (query: %s), hr=%Rhrc (index: %d)\n",
                                    g_aSENSEvents[i].pszMethod, pszSubToRemove, hr, iErrorIdX);
                /* Keep going. */

            RTStrFree(pszSubToRemove);
        }

        g_pIEventSystem->Release();
        g_pIEventSystem = NULL;
    }

    if (g_pISensLogon)
    {
        delete g_pISensLogon;
        g_pISensLogon = NULL;
    }

    NemuCredProvVerbose(0, "NemuCredentialProviderUnregisterSENS: Returning hr=%Rhrc\n", hr);
}
#endif /* NEMU_WITH_SENS */


BOOL WINAPI DllMain(HINSTANCE hInst, DWORD dwReason, LPVOID pReserved)
{
    NOREF(pReserved);

    g_hDllInst = hInst;

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            int rc = RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            if (RT_SUCCESS(rc))
                rc = VbglR3Init();

            if (RT_SUCCESS(rc))
            {
                NemuCredProvVerbose(0, "NemuCredProv: v%s r%s (%s %s) loaded (refs=%ld)\n",
                                    RTBldCfgVersion(), RTBldCfgRevisionStr(),
                                    __DATE__, __TIME__, g_cDllRefs);
            }

            DisableThreadLibraryCalls(hInst);
            break;
        }

        case DLL_PROCESS_DETACH:

            NemuCredProvVerbose(0, "NemuCredProv: Unloaded (refs=%ld)\n", g_cDllRefs);
            if (!g_cDllRefs)
                VbglR3Term();
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;

        default:
            break;
    }

    return TRUE;
}


/**
 * Increments the reference count by one. Must be released
 * with NemuCredentialProviderRelease() when finished.
 */
void NemuCredentialProviderAcquire(void)
{
    LONG cRefCount = InterlockedIncrement(&g_cDllRefs);
    NemuCredProvVerbose(0, "NemuCredentialProviderAcquire: Increasing global refcount to %ld\n",
                        cRefCount);
}


/**
 * Decrements the reference count by one.
 */
void NemuCredentialProviderRelease(void)
{
    LONG cRefCount = InterlockedDecrement(&g_cDllRefs);
    NemuCredProvVerbose(0, "NemuCredentialProviderRelease: Decreasing global refcount to %ld\n",
                        cRefCount);
}


/**
 * Returns the current DLL reference count.
 *
 * @return  LONG                The current reference count.
 */
LONG NemuCredentialProviderRefCount(void)
{
    return g_cDllRefs;
}


/**
 * Entry point for determining whether the credential
 * provider DLL can be unloaded or not.
 *
 * @return  HRESULT
 */
HRESULT __stdcall DllCanUnloadNow(void)
{
    NemuCredProvVerbose(0, "DllCanUnloadNow (refs=%ld)\n",
                        g_cDllRefs);

#ifdef NEMU_WITH_SENS
    if (!g_cDllRefs)
    {
        if (g_fSENSEnabled)
            NemuCredentialProviderUnregisterSENS();

        CoUninitialize();
    }
#endif
    return (g_cDllRefs > 0) ? S_FALSE : S_OK;
}


/**
 * Create the VirtualBox credential provider by creating
 * its factory which then in turn can create instances of the
 * provider itself.
 *
 * @return  HRESULT
 * @param   classID             The class ID.
 * @param   interfaceID         The interface ID.
 * @param   ppvInterface        Receives the interface pointer on successful
 *                              object creation.
 */
HRESULT NemuCredentialProviderCreate(REFCLSID classID, REFIID interfaceID,
                                     void **ppvInterface)
{
    HRESULT hr;
    if (classID == CLSID_NemuCredProvider)
    {
        try
        {
            NemuCredProvFactory* pFactory = new NemuCredProvFactory();
            AssertPtr(pFactory);
            hr = pFactory->QueryInterface(interfaceID,
                                          ppvInterface);
            pFactory->Release();

#ifdef NEMU_WITH_SENS
            g_fSENSEnabled = true; /* By default SENS support is enabled. */

            HKEY hKey;
            /** @todo Add some registry wrapper function(s) as soon as we got more values to retrieve. */
            DWORD dwRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Oracle\\VirtualBox Guest Additions\\AutoLogon",
                                       0L, KEY_QUERY_VALUE, &hKey);
            if (dwRet == ERROR_SUCCESS)
            {
                DWORD dwValue;
                DWORD dwType = REG_DWORD;
                DWORD dwSize = sizeof(DWORD);

                dwRet = RegQueryValueEx(hKey, L"HandleSENS", NULL, &dwType, (LPBYTE)&dwValue, &dwSize);
                if (   dwRet  == ERROR_SUCCESS
                    && dwType == REG_DWORD
                    && dwSize == sizeof(DWORD))
                {
                    g_fSENSEnabled = RT_BOOL(dwValue);
                }

                RegCloseKey(hKey);
            }

            NemuCredProvVerbose(0, "NemuCredentialProviderCreate: g_fSENSEnabled=%RTbool\n",
                                g_fSENSEnabled);
            if (   SUCCEEDED(hr)
                && g_fSENSEnabled)
            {
                HRESULT hRes = CoInitializeEx(NULL, COINIT_MULTITHREADED);
                NemuCredentialProviderRegisterSENS();
            }
#else
            NemuCredProvVerbose(0, "NemuCredentialProviderCreate: SENS support is disabled\n");
#endif
        }
        catch (std::bad_alloc &ex)
        {
            NOREF(ex);
            hr = E_OUTOFMEMORY;
        }
    }
    else
        hr = CLASS_E_CLASSNOTAVAILABLE;

    return hr;
}


/**
 * Entry point for getting the actual credential provider
 * class object.
 *
 * @return  HRESULT
 * @param   classID             The class ID.
 * @param   interfaceID         The interface ID.
 * @param   ppvInterface        Receives the interface pointer on successful
 *                              object creation.
 */
HRESULT __stdcall DllGetClassObject(REFCLSID classID, REFIID interfaceID,
                                    void **ppvInterface)
{
    NemuCredProvVerbose(0, "DllGetClassObject (refs=%ld)\n",
                        g_cDllRefs);

    return NemuCredentialProviderCreate(classID, interfaceID, ppvInterface);
}

