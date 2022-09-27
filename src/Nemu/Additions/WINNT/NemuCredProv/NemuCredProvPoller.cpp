/* $Id: NemuCredProvPoller.cpp $ */
/** @file
 * NemuCredPoller - Thread for querying / retrieving user credentials.
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
#include <Windows.h>

#include <Nemu/NemuGuest.h>
#include <Nemu/NemuGuestLib.h>
#include <Nemu/VMMDev.h>
#include <iprt/string.h>

#include "NemuCredProvProvider.h"

#include "NemuCredProvCredential.h"
#include "NemuCredProvPoller.h"
#include "NemuCredProvUtils.h"


NemuCredProvPoller::NemuCredProvPoller(void) :
    m_hThreadPoller(NIL_RTTHREAD),
    m_pProv(NULL)
{
}


NemuCredProvPoller::~NemuCredProvPoller(void)
{
    NemuCredProvVerbose(0, "NemuCredProvPoller: Destroying ...\n");

    Shutdown();
}


int
NemuCredProvPoller::Initialize(NemuCredProvProvider *pProvider)
{
    AssertPtrReturn(pProvider, VERR_INVALID_POINTER);

    NemuCredProvVerbose(0, "NemuCredProvPoller: Initializing\n");

    /* Don't create more than one of them. */
    if (m_hThreadPoller != NIL_RTTHREAD)
    {
        NemuCredProvVerbose(0, "NemuCredProvPoller: Thread already running, returning\n");
        return VINF_SUCCESS;
    }

    if (m_pProv != NULL)
        m_pProv->Release();

    m_pProv = pProvider;
    /*
     * We must not add a reference via AddRef() here, otherwise
     * the credential provider does not get destructed properly.
     * In order to get this thread terminated normally the credential
     * provider has to call Shutdown().
     */

    /* Create the poller thread. */
    int rc = RTThreadCreate(&m_hThreadPoller, NemuCredProvPoller::threadPoller, this, 0, RTTHREADTYPE_INFREQUENT_POLLER,
                            RTTHREADFLAGS_WAITABLE, "credpoll");
    if (RT_FAILURE(rc))
        NemuCredProvVerbose(0, "NemuCredProvPoller::Initialize: Failed to create thread, rc=%Rrc\n", rc);

    return rc;
}


int
NemuCredProvPoller::Shutdown(void)
{
    NemuCredProvVerbose(0, "NemuCredProvPoller: Shutdown\n");

    if (m_hThreadPoller == NIL_RTTHREAD)
        return VINF_SUCCESS;

    /* Post termination event semaphore. */
    int rc = RTThreadUserSignal(m_hThreadPoller);
    if (RT_SUCCESS(rc))
    {
        NemuCredProvVerbose(0, "NemuCredProvPoller: Waiting for thread to terminate\n");
        /* Wait until the thread has terminated. */
        rc = RTThreadWait(m_hThreadPoller, RT_INDEFINITE_WAIT, NULL);
        if (RT_FAILURE(rc))
            NemuCredProvVerbose(0, "NemuCredProvPoller: Wait returned error rc=%Rrc\n", rc);
    }
    else
        NemuCredProvVerbose(0, "NemuCredProvPoller: Error waiting for thread shutdown, rc=%Rrc\n", rc);

    m_pProv = NULL;
    m_hThreadPoller = NIL_RTTHREAD;

    NemuCredProvVerbose(0, "NemuCredProvPoller: Shutdown returned with rc=%Rrc\n", rc);
    return rc;
}


/*static*/ DECLCALLBACK(int)
NemuCredProvPoller::threadPoller(RTTHREAD hThreadSelf, void *pvUser)
{
    NemuCredProvVerbose(0, "NemuCredProvPoller: Starting, pvUser=0x%p\n", pvUser);

    NemuCredProvPoller *pThis = (NemuCredProvPoller*)pvUser;
    AssertPtr(pThis);

    for (;;)
    {
        int rc;
        rc = VbglR3CredentialsQueryAvailability();
        if (RT_FAILURE(rc))
        {
            if (rc != VERR_NOT_FOUND)
                NemuCredProvVerbose(0, "NemuCredProvPoller: Could not retrieve credentials! rc=%Rc\n", rc);
        }
        else
        {
            NemuCredProvVerbose(0, "NemuCredProvPoller: Credentials available, notifying provider\n");

            if (pThis->m_pProv)
                pThis->m_pProv->OnCredentialsProvided();
        }

        /* Wait a bit. */
        if (RTThreadUserWait(hThreadSelf, 500) == VINF_SUCCESS)
        {
            NemuCredProvVerbose(0, "NemuCredProvPoller: Terminating\n");
            break;
        }
    }

    return VINF_SUCCESS;
}

