/* $Id: tstNemuAPIPerf.cpp $ */
/** @file
 * tstNemuAPIPerf - Checks the performance of the COM / XPOM API.
 */

/*
 * Copyright (C) 2006-2013 Oracle Corporation
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
#include <Nemu/com/com.h>
#include <Nemu/com/string.h>
#include <Nemu/com/array.h>
#include <Nemu/com/Guid.h>
#include <Nemu/com/ErrorInfo.h>
#include <Nemu/com/VirtualBox.h>
#include <Nemu/sup.h>

#include <iprt/test.h>
#include <iprt/time.h>



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


/** Worker fro TST_COM_EXPR(). */
static HRESULT tstComExpr(HRESULT hrc, const char *pszOperation, int iLine)
{
    if (FAILED(hrc))
        RTTestFailed(g_hTest, "%s failed on line %u with hrc=%Rhrc", pszOperation, iLine, hrc);
    return hrc;
}

/** Macro that executes the given expression and report any failure.
 *  The expression must return a HRESULT. */
#define TST_COM_EXPR(expr) tstComExpr(expr, #expr, __LINE__)



static void tstApiPrf1(IVirtualBox *pNemu)
{
    RTTestSub(g_hTest, "IVirtualBox::Revision performance");

    uint32_t const cCalls   = 65536;
    uint32_t       cLeft    = cCalls;
    uint64_t       uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        ULONG uRev;
        HRESULT hrc = pNemu->COMGETTER(Revision)(&uRev);
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IVirtualBox::Revision", __LINE__);
            return;
        }
    }
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Revision average", uElapsed / cCalls, RTTESTUNIT_NS_PER_CALL);
    RTTestSubDone(g_hTest);
}


static void tstApiPrf2(IVirtualBox *pNemu)
{
    RTTestSub(g_hTest, "IVirtualBox::Version performance");

    uint32_t const cCalls   = 65536;
    uint32_t       cLeft    = cCalls;
    uint64_t       uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        com::Bstr bstrVersion;
        HRESULT hrc = pNemu->COMGETTER(Version)(bstrVersion.asOutParam());
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IVirtualBox::Version", __LINE__);
            return;
        }
    }
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Version average", uElapsed / cCalls, RTTESTUNIT_NS_PER_CALL);
    RTTestSubDone(g_hTest);
}


static void tstApiPrf3(IVirtualBox *pNemu)
{
    RTTestSub(g_hTest, "IVirtualBox::Host performance");

    /* The first call. */
    uint64_t    uStartTS = RTTimeNanoTS();
    IHost      *pHost = NULL;
    HRESULT     hrc = pNemu->COMGETTER(Host)(&pHost);
    if (FAILED(hrc))
    {
        tstComExpr(hrc, "IVirtualBox::Host", __LINE__);
        return;
    }
    pHost->Release();
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Host first", uElapsed, RTTESTUNIT_NS);

    /* Subsequent calls. */
    uint32_t const cCalls1  = 4096;
    uint32_t       cLeft    = cCalls1;
    uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        IHost *pHost2 = NULL;
        hrc = pNemu->COMGETTER(Host)(&pHost2);
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IVirtualBox::Host", __LINE__);
            return;
        }
        pHost2->Release();
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Host average", uElapsed / cCalls1, RTTESTUNIT_NS_PER_CALL);

    /* Keep a reference around and see how that changes things.
       Note! NemuSVC is not creating and destroying Host().  */
    pHost = NULL;
    hrc = pNemu->COMGETTER(Host)(&pHost);

    uint32_t const cCalls2  = 16384;
    cLeft    = cCalls2;
    uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        IHost *pHost2 = NULL;
        hrc = pNemu->COMGETTER(Host)(&pHost2);
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IVirtualBox::Host", __LINE__);
            pHost->Release();
            return;
        }
        pHost2->Release();
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Host 2nd ref", uElapsed / cCalls2, RTTESTUNIT_NS_PER_CALL);
    pHost->Release();

    RTTestSubDone(g_hTest);
}


static void tstApiPrf4(IVirtualBox *pNemu)
{
    RTTestSub(g_hTest, "IHost::GetProcessorFeature performance");

    IHost      *pHost = NULL;
    HRESULT     hrc = pNemu->COMGETTER(Host)(&pHost);
    if (FAILED(hrc))
    {
        tstComExpr(hrc, "IVirtualBox::Host", __LINE__);
        return;
    }

    uint32_t const  cCalls   = 65536;
    uint32_t        cLeft    = cCalls;
    uint64_t        uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        BOOL fSupported;
        hrc = pHost->GetProcessorFeature(ProcessorFeature_PAE, &fSupported);
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IHost::GetProcessorFeature", __LINE__);
            pHost->Release();
            return;
        }
    }
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IHost::GetProcessorFeature average", uElapsed / cCalls, RTTESTUNIT_NS_PER_CALL);
    pHost->Release();
    RTTestSubDone(g_hTest);
}



int main(int argc, char **argv)
{
    /*
     * Initialization.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstNemuAPIPerf", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    SUPR3Init(NULL); /* Better time support. */
    RTTestBanner(g_hTest);

    RTTestSub(g_hTest, "Initializing COM and singletons");
    HRESULT hrc = com::Initialize();
    if (SUCCEEDED(hrc))
    {
        ComPtr<IVirtualBox> ptrNemu;
        hrc = TST_COM_EXPR(ptrNemu.createLocalObject(CLSID_VirtualBox));
        if (SUCCEEDED(hrc))
        {
            ComPtr<ISession> ptrSession;
            hrc = TST_COM_EXPR(ptrSession.createInprocObject(CLSID_Session));
            if (SUCCEEDED(hrc))
            {
                RTTestSubDone(g_hTest);

                /*
                 * Call test functions.
                 */
                tstApiPrf1(ptrNemu);
                tstApiPrf2(ptrNemu);
                tstApiPrf3(ptrNemu);

                /** @todo Find something that returns a 2nd instance of an interface and see
                 *        how if wrapper stuff is reused in any way. */
                tstApiPrf4(ptrNemu);
            }
        }

        ptrNemu.setNull();
        com::Shutdown();
    }
    else
        RTTestIFailed("com::Initialize failed with hrc=%Rhrc", hrc);
    return RTTestSummaryAndDestroy(g_hTest);
}

