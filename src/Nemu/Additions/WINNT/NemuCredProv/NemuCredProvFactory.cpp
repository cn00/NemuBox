/* $Id: NemuCredProvFactory.cpp $ */
/** @file
 * NemuCredentialProvFactory - The VirtualBox Credential Provider Factory.
 */

/*
 * Copyright (C) 2012 Oracle Corporation
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
#include "NemuCredentialProvider.h"
#include "NemuCredProvFactory.h"
#include "NemuCredProvProvider.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
extern HRESULT NemuCredProvProviderCreate(REFIID interfaceID, void **ppvInterface);


NemuCredProvFactory::NemuCredProvFactory(void) :
    m_cRefs(1) /* Start with one instance. */
{
}

NemuCredProvFactory::~NemuCredProvFactory(void)
{
}

ULONG
NemuCredProvFactory::AddRef(void)
{
    LONG cRefs = InterlockedIncrement(&m_cRefs);
    NemuCredProvVerbose(0, "NemuCredProvFactory: AddRef: Returning refcount=%ld\n",
                        cRefs);
    return cRefs;
}

ULONG
NemuCredProvFactory::Release(void)
{
    LONG cRefs = InterlockedDecrement(&m_cRefs);
    NemuCredProvVerbose(0, "NemuCredProvFactory: Release: Returning refcount=%ld\n",
                        cRefs);
    if (!cRefs)
    {
        NemuCredProvVerbose(0, "NemuCredProvFactory: Calling destructor\n");
        delete this;
    }
    return cRefs;
}

HRESULT
NemuCredProvFactory::QueryInterface(REFIID interfaceID, void **ppvInterface)
{
    NemuCredProvVerbose(0, "NemuCredProvFactory: QueryInterface\n");

    HRESULT hr = S_OK;
    if (ppvInterface)
    {
        if (   IID_IClassFactory == interfaceID
            || IID_IUnknown      == interfaceID)
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

HRESULT
NemuCredProvFactory::CreateInstance(IUnknown *pUnkOuter, REFIID interfaceID, void **ppvInterface)
{
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;
    return NemuCredProvProviderCreate(interfaceID, ppvInterface);
}

HRESULT
NemuCredProvFactory::LockServer(BOOL fLock)
{
    if (fLock)
        NemuCredentialProviderAcquire();
    else
        NemuCredentialProviderRelease();
    return S_OK;
}

