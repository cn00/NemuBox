/* $Id: NemuCredProvProvider.cpp $ */
/** @file
 * NemuCredProvProvider - The actual credential provider class.
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
#include <new> /* For bad_alloc. */

#include <credentialprovider.h>

#include <iprt/err.h>
#include <Nemu/NemuGuestLib.h>

#include "NemuCredentialProvider.h"
#include "NemuCredProvProvider.h"
#include "NemuCredProvCredential.h"



NemuCredProvProvider::NemuCredProvProvider(void) :
    m_cRefs(1),
    m_pPoller(NULL),
    m_pCred(NULL),
    m_pEvents(NULL),
    m_fHandleRemoteSessions(false)
{
    NemuCredentialProviderAcquire();

    NemuCredProvReportStatus(NemuGuestFacilityStatus_Init);
}


NemuCredProvProvider::~NemuCredProvProvider(void)
{
    NemuCredProvVerbose(0, "NemuCredProv: Destroying\n");

    if (m_pCred)
    {
        m_pCred->Release();
        m_pCred = NULL;
    }

    if (m_pPoller)
    {
        m_pPoller->Shutdown();
        delete m_pPoller;
        m_pPoller = NULL;
    }

    NemuCredProvReportStatus(NemuGuestFacilityStatus_Terminated);

    NemuCredentialProviderRelease();
}


/* IUnknown overrides. */
ULONG
NemuCredProvProvider::AddRef(void)
{
    LONG cRefs = InterlockedIncrement(&m_cRefs);
    NemuCredProvVerbose(0, "NemuCredProv: AddRef: Returning refcount=%ld\n",
                        cRefs);
    return cRefs;
}


ULONG
NemuCredProvProvider::Release(void)
{
    LONG cRefs = InterlockedDecrement(&m_cRefs);
    NemuCredProvVerbose(0, "NemuCredProv: Release: Returning refcount=%ld\n",
                        cRefs);
    if (!cRefs)
    {
        NemuCredProvVerbose(0, "NemuCredProv: Calling destructor\n");
        delete this;
    }
    return cRefs;
}


HRESULT
NemuCredProvProvider::QueryInterface(REFIID interfaceID, void **ppvInterface)
{
    HRESULT hr = S_OK;
    if (ppvInterface)
    {
        if (   IID_IUnknown            == interfaceID
            || IID_ICredentialProvider == interfaceID)
        {
            *ppvInterface = static_cast<IUnknown*>(this);
            reinterpret_cast<IUnknown*>(*ppvInterface)->AddRef();
        }
        else
        {
            *ppvInterface = NULL;
            hr = E_NOINTERFACE;
        }
    }
    else
        hr = E_INVALIDARG;

    return hr;
}


/**
 * Loads the global configuration from registry.
 *
 * @return  DWORD       Windows error code.
 */
DWORD
NemuCredProvProvider::LoadConfiguration(void)
{
    HKEY hKey;
    /** @todo Add some registry wrapper function(s) as soon as we got more values to retrieve. */
    DWORD dwRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Oracle\\VirtualBox Guest Additions\\AutoLogon",
                               0L, KEY_QUERY_VALUE, &hKey);
    if (dwRet == ERROR_SUCCESS)
    {
        DWORD dwValue;
        DWORD dwType = REG_DWORD;
        DWORD dwSize = sizeof(DWORD);

        dwRet = RegQueryValueEx(hKey, L"HandleRemoteSessions", NULL, &dwType, (LPBYTE)&dwValue, &dwSize);
        if (   dwRet  == ERROR_SUCCESS
            && dwType == REG_DWORD
            && dwSize == sizeof(DWORD))
        {
            m_fHandleRemoteSessions = RT_BOOL(dwValue);
        }

        dwRet = RegQueryValueEx(hKey, L"LoggingEnabled", NULL, &dwType, (LPBYTE)&dwValue, &dwSize);
        if (   dwRet  == ERROR_SUCCESS
            && dwType == REG_DWORD
            && dwSize == sizeof(DWORD))
        {
            g_dwVerbosity = 1; /* Default logging level. */
        }

        if (g_dwVerbosity) /* Do we want logging at all? */
        {
            dwRet = RegQueryValueEx(hKey, L"LoggingLevel", NULL, &dwType, (LPBYTE)&dwValue, &dwSize);
            if (   dwRet  == ERROR_SUCCESS
                && dwType == REG_DWORD
                && dwSize == sizeof(DWORD))
            {
                g_dwVerbosity = dwValue;
            }
        }

        RegCloseKey(hKey);
    }
    /* Do not report back an error here yet. */
    return ERROR_SUCCESS;
}


/**
 * Determines whether we should handle the current session or not.
 *
 * @return  bool        true if we should handle this session, false if not.
 */
bool
NemuCredProvProvider::HandleCurrentSession(void)
{
    /* Load global configuration from registry. */
    int rc = LoadConfiguration();
    if (RT_FAILURE(rc))
        NemuCredProvVerbose(0, "NemuCredProv: Error loading global configuration, rc=%Rrc\n",
                            rc);

    bool fHandle = false;
    if (VbglR3AutoLogonIsRemoteSession())
    {
        if (m_fHandleRemoteSessions) /* Force remote session handling. */
            fHandle = true;
    }
    else /* No remote session. */
        fHandle = true;

    NemuCredProvVerbose(3, "NemuCredProv: Handling current session=%RTbool\n", fHandle);
    return fHandle;
}


/**
 * Tells this provider the current usage scenario.
 *
 * @return  HRESULT
 * @param   enmUsageScenario    Current usage scenario this provider will be
 *                              used in.
 * @param   dwFlags             Optional flags for the usage scenario.
 */
HRESULT
NemuCredProvProvider::SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO enmUsageScenario, DWORD dwFlags)
{
    HRESULT hr = S_OK;
    DWORD dwErr;

    NemuCredProvVerbose(0, "NemuCredProv::SetUsageScenario: enmUsageScenario=%d, dwFlags=%ld\n",
                        enmUsageScenario, dwFlags);

    m_enmUsageScenario = enmUsageScenario;

    switch (m_enmUsageScenario)
    {
        case CPUS_LOGON:
        case CPUS_UNLOCK_WORKSTATION:
        {
            NemuCredProvReportStatus(NemuGuestFacilityStatus_Active);

            dwErr = LoadConfiguration();
            if (dwErr != ERROR_SUCCESS)
                NemuCredProvVerbose(0, "NemuCredProv: Error while loading configuration, error=%ld\n", dwErr);
            /* Do not stop running on a misconfigured system. */

            /*
             * If we're told to not handle the current session just bail out and let the
             * user know.
             */
            if (!HandleCurrentSession())
                break;

            if (!m_pPoller)
            {
                try
                {
                    m_pPoller = new NemuCredProvPoller();
                    AssertPtr(m_pPoller);
                    int rc = m_pPoller->Initialize(this);
                    if (RT_FAILURE(rc))
                        NemuCredProvVerbose(0, "NemuCredProv::SetUsageScenario: Error initializing poller thread, rc=%Rrc\n", rc);
                }
                catch (std::bad_alloc &ex)
                {
                    NOREF(ex);
                    hr = E_OUTOFMEMORY;
                }
            }

            if (   SUCCEEDED(hr)
                && !m_pCred)
            {
                try
                {
                    m_pCred = new NemuCredProvCredential();
                    AssertPtr(m_pPoller);
                    hr = m_pCred->Initialize(m_enmUsageScenario);
                }
                catch (std::bad_alloc &ex)
                {
                    NOREF(ex);
                    hr = E_OUTOFMEMORY;
                }
            }
            else
            {
                /* All set up already! Nothing to do here right now. */
            }

            /* If we failed, do some cleanup. */
            if (FAILED(hr))
            {
                if (m_pCred != NULL)
                {
                    m_pCred->Release();
                    m_pCred = NULL;
                }
            }
            break;
        }

        case CPUS_CHANGE_PASSWORD: /* Asks us to provide a way to change the password. */
        case CPUS_CREDUI:          /* Displays an own UI. We don't need that. */
        case CPUS_PLAP:            /* See Pre-Logon-Access Provider. Not needed (yet). */

            hr = E_NOTIMPL;
            break;

        default:

            hr = E_INVALIDARG;
            break;
    }

    NemuCredProvVerbose(0, "NemuCredProv::SetUsageScenario returned hr=0x%08x\n", hr);
    return hr;
}


/**
 * Tells this provider how the serialization will be handled. Currently not used.
 *
 * @return  STDMETHODIMP
 * @param   pcpCredentialSerialization      Credentials serialization.
 */
STDMETHODIMP
NemuCredProvProvider::SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION *pcpCredentialSerialization)
{
    NOREF(pcpCredentialSerialization);
    return E_NOTIMPL;
}


/**
 * Initializes the communication with LogonUI through callbacks events which we can later
 * use to start re-enumeration of credentials.
 *
 * @return  HRESULT
 * @param   pcpEvents               Pointer to event interface.
 * @param   upAdviseContext         The current advise context.
 */
HRESULT
NemuCredProvProvider::Advise(ICredentialProviderEvents *pcpEvents, UINT_PTR upAdviseContext)
{
    NemuCredProvVerbose(0, "NemuCredProv::Advise, pcpEvents=0x%p, upAdviseContext=%u\n",
                        pcpEvents, upAdviseContext);
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = NULL;
    }

    m_pEvents = pcpEvents;
    if (m_pEvents)
        m_pEvents->AddRef();

    /*
     * Save advice context for later use when binding to
     * certain ICredentialProviderEvents events.
     */
    m_upAdviseContext = upAdviseContext;
    return S_OK;
}


/**
 * Uninitializes the callback events so that they're no longer valid.
 *
 * @return  HRESULT
 */
HRESULT
NemuCredProvProvider::UnAdvise(void)
{
    NemuCredProvVerbose(0, "NemuCredProv::UnAdvise: pEvents=0x%p\n",
                        m_pEvents);
    if (m_pEvents)
    {
        m_pEvents->Release();
        m_pEvents = NULL;
    }

    return S_OK;
}


/**
 * Retrieves the total count of fields we're handling (needed for field enumeration
 * through LogonUI).
 *
 * @return  HRESULT
 * @param   pdwCount        Receives total count of fields.
 */
HRESULT
NemuCredProvProvider::GetFieldDescriptorCount(DWORD *pdwCount)
{
    if (pdwCount)
    {
        *pdwCount = NEMUCREDPROV_NUM_FIELDS;
        NemuCredProvVerbose(0, "NemuCredProv::GetFieldDescriptorCount: %ld\n", *pdwCount);
    }
    return S_OK;
}


/**
 * Retrieves a descriptor of a specified field.
 *
 * @return  HRESULT
 * @param   dwIndex                 ID of field to retrieve descriptor for.
 * @param   ppFieldDescriptor       Pointer which receives the allocated field
 *                                  descriptor.
 */
HRESULT
NemuCredProvProvider::GetFieldDescriptorAt(DWORD dwIndex, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR **ppFieldDescriptor)
{
    HRESULT hr = S_OK;
    if (   dwIndex < NEMUCREDPROV_NUM_FIELDS
        && ppFieldDescriptor)
    {
        PCREDENTIAL_PROVIDER_FIELD_DESCRIPTOR pcpFieldDesc =
            (PCREDENTIAL_PROVIDER_FIELD_DESCRIPTOR)CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));

        if (pcpFieldDesc)
        {
            const NEMUCREDPROV_FIELD &field = s_NemuCredProvFields[dwIndex];

            RT_BZERO(pcpFieldDesc, sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));

            pcpFieldDesc->dwFieldID = field.desc.dwFieldID;
            pcpFieldDesc->cpft      = field.desc.cpft;
            if (field.desc.pszLabel)
                hr = SHStrDupW(field.desc.pszLabel, &pcpFieldDesc->pszLabel);
        }
        else
            hr = E_OUTOFMEMORY;

        if (SUCCEEDED(hr))
            *ppFieldDescriptor = pcpFieldDesc;
        else
            CoTaskMemFree(pcpFieldDesc);
    }
    else
        hr = E_INVALIDARG;

    NemuCredProvVerbose(0, "NemuCredProv::GetFieldDescriptorAt: dwIndex=%ld, ppDesc=0x%p, hr=0x%08x\n",
                        dwIndex, ppFieldDescriptor, hr);
    return hr;
}


/**
 * Retrieves the total number of credentials this provider can offer at the current time and
 * if a logon attempt should be made.
 *
 * @return  HRESULT
 * @param   pdwCount                    Receives number of credentials to serve.
 * @param   pdwDefault                  Receives the credentials index to try
 *                                      logging on if there is more than one
 *                                      credential provided. 0 is default.
 * @param   pfAutoLogonWithDefault      Receives a flag indicating whether a
 *                                      logon attempt using the default
 *                                      credential should be made or not.
 */
HRESULT
NemuCredProvProvider::GetCredentialCount(DWORD *pdwCount, DWORD *pdwDefault, BOOL *pfAutoLogonWithDefault)
{
    AssertPtr(pdwCount);
    AssertPtr(pdwDefault);
    AssertPtr(pfAutoLogonWithDefault);

    bool fHasCredentials = false;

    /* Do we have credentials? */
    if (m_pCred)
    {
        int rc = m_pCred->RetrieveCredentials();
        fHasCredentials = rc == VINF_SUCCESS;
    }

    if (fHasCredentials)
    {
        *pdwCount = 1;                   /* This provider always has the same number of credentials (1). */
        *pdwDefault = 0;                 /* The credential we provide is *always* at index 0! */
        *pfAutoLogonWithDefault = TRUE;  /* We always at least try to auto-login (if password is correct). */
    }
    else
    {
        *pdwCount = 0;
        *pdwDefault = CREDENTIAL_PROVIDER_NO_DEFAULT;
        *pfAutoLogonWithDefault = FALSE;
    }

    NemuCredProvVerbose(0, "NemuCredProv::GetCredentialCount: *pdwCount=%ld, *pdwDefault=%ld, *pfAutoLogonWithDefault=%s\n",
                        *pdwCount, *pdwDefault, *pfAutoLogonWithDefault ? "true" : "false");
    return S_OK;
}


/**
 * Called by Winlogon to retrieve the interface of our current ICredentialProviderCredential interface.
 *
 * @return  HRESULT
 * @param   dwIndex                     Index of credential (in case there is more than one credential at a time) to
 *                                      retrieve the interface for.
 * @param   ppCredProvCredential        Pointer that receives the credential interface.
 */
HRESULT
NemuCredProvProvider::GetCredentialAt(DWORD dwIndex, ICredentialProviderCredential **ppCredProvCredential)
{
    NemuCredProvVerbose(0, "NemuCredProv::GetCredentialAt: Index=%ld, ppCredProvCredential=0x%p\n",
                        dwIndex, ppCredProvCredential);
    if (!m_pCred)
    {
        NemuCredProvVerbose(0, "NemuCredProv::GetCredentialAt: No credentials available\n");
        return E_INVALIDARG;
    }

    HRESULT hr;
    if (   dwIndex == 0
        && ppCredProvCredential)
    {
        hr = m_pCred->QueryInterface(IID_ICredentialProviderCredential,
                                     reinterpret_cast<void**>(ppCredProvCredential));
    }
    else
    {
        NemuCredProvVerbose(0, "NemuCredProv::GetCredentialAt: More than one credential not supported!\n");
        hr = E_INVALIDARG;
    }
    return hr;
}


/**
 * Triggers a credential re-enumeration -- will be called by our poller thread. This then invokes
 * GetCredentialCount() and GetCredentialAt() called by Winlogon.
 */
void
NemuCredProvProvider::OnCredentialsProvided(void)
{
    NemuCredProvVerbose(0, "NemuCredProv::OnCredentialsProvided\n");

    if (m_pEvents)
        m_pEvents->CredentialsChanged(m_upAdviseContext);
}


/**
 * Creates our provider. This happens *before* CTRL-ALT-DEL was pressed!
 */
HRESULT
NemuCredProvProviderCreate(REFIID interfaceID, void **ppvInterface)
{
    HRESULT hr;

    try
    {
        NemuCredProvProvider *pProvider = new NemuCredProvProvider();
        AssertPtr(pProvider);
        hr = pProvider->QueryInterface(interfaceID, ppvInterface);
        pProvider->Release();
    }
    catch (std::bad_alloc &ex)
    {
        NOREF(ex);
        hr = E_OUTOFMEMORY;
    }

    return hr;
}

