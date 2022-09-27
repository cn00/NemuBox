/* $Id: NemuCrHgsmi.cpp $ */

/** @file
 * NemuVideo Display D3D User mode dll
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

#include <Nemu/NemuCrHgsmi.h>
#include <iprt/err.h>

#include "NemuUhgsmiKmt.h"

static NEMUDISPKMT_CALLBACKS g_NemuCrHgsmiKmtCallbacks;
static int g_bNemuKmtCallbacksInited = 0;

static int nemuCrHgsmiInitPerform(NEMUDISPKMT_CALLBACKS *pCallbacks)
{
    HRESULT hr = nemuDispKmtCallbacksInit(pCallbacks);
    /*Assert(hr == S_OK);*/
    if (hr == S_OK)
    {
        /* check if we can create the hgsmi */
        PNEMUUHGSMI pHgsmi = NemuCrHgsmiCreate();
        if (pHgsmi)
        {
            /* yes, we can, so this is wddm mode */
            NemuCrHgsmiDestroy(pHgsmi);
            Log(("CrHgsmi: WDDM mode supported\n"));
            return 1;
        }
        nemuDispKmtCallbacksTerm(pCallbacks);
    }
    Log(("CrHgsmi: unsupported\n"));
    return -1;
}

NEMUCRHGSMI_DECL(int) NemuCrHgsmiInit()
{
    if (!g_bNemuKmtCallbacksInited)
    {
        g_bNemuKmtCallbacksInited = nemuCrHgsmiInitPerform(&g_NemuCrHgsmiKmtCallbacks);
        Assert(g_bNemuKmtCallbacksInited);
    }

    return g_bNemuKmtCallbacksInited > 0 ? VINF_SUCCESS : VERR_NOT_SUPPORTED;
}

NEMUCRHGSMI_DECL(PNEMUUHGSMI) NemuCrHgsmiCreate()
{
    PNEMUUHGSMI_PRIVATE_KMT pHgsmiGL = (PNEMUUHGSMI_PRIVATE_KMT)RTMemAllocZ(sizeof (*pHgsmiGL));
    if (pHgsmiGL)
    {
        HRESULT hr = nemuUhgsmiKmtCreate(pHgsmiGL, TRUE /* bD3D tmp for injection thread*/);
        Log(("CrHgsmi: faled to create KmtEsc NEMUUHGSMI instance, hr (0x%x)\n", hr));
        if (hr == S_OK)
        {
            return &pHgsmiGL->BasePrivate.Base;
        }
        RTMemFree(pHgsmiGL);
    }

    return NULL;
}

NEMUCRHGSMI_DECL(void) NemuCrHgsmiDestroy(PNEMUUHGSMI pHgsmi)
{
    PNEMUUHGSMI_PRIVATE_KMT pHgsmiGL = NEMUUHGSMIKMT_GET(pHgsmi);
    HRESULT hr = nemuUhgsmiKmtDestroy(pHgsmiGL);
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        RTMemFree(pHgsmiGL);
    }
}

NEMUCRHGSMI_DECL(int) NemuCrHgsmiTerm()
{
#if 0
    PNEMUUHGSMI_PRIVATE_KMT pHgsmiGL = gt_pHgsmiGL;
    if (pHgsmiGL)
    {
        g_NemuCrHgsmiCallbacks.pfnClientDestroy(pHgsmiGL->BasePrivate.hClient);
        nemuUhgsmiKmtDestroy(pHgsmiGL);
        gt_pHgsmiGL = NULL;
    }

    if (g_pfnNemuDispCrHgsmiTerm)
        g_pfnNemuDispCrHgsmiTerm();
#endif
    if (g_bNemuKmtCallbacksInited > 0)
    {
        nemuDispKmtCallbacksTerm(&g_NemuCrHgsmiKmtCallbacks);
    }
    return VINF_SUCCESS;
}

NEMUCRHGSMI_DECL(int) NemuCrHgsmiCtlConGetClientID(PNEMUUHGSMI pHgsmi, uint32_t *pu32ClientID)
{
    PNEMUUHGSMI_PRIVATE_BASE pHgsmiPrivate = (PNEMUUHGSMI_PRIVATE_BASE)pHgsmi;
    int rc = nemuCrHgsmiPrivateCtlConGetClientID(pHgsmiPrivate, pu32ClientID);
    if (!RT_SUCCESS(rc))
    {
        WARN(("nemuCrHgsmiPrivateCtlConGetClientID failed with rc (%d)", rc));
    }
    return rc;
}

NEMUCRHGSMI_DECL(int) NemuCrHgsmiCtlConGetHostCaps(PNEMUUHGSMI pHgsmi, uint32_t *pu32HostCaps)
{
    PNEMUUHGSMI_PRIVATE_BASE pHgsmiPrivate = (PNEMUUHGSMI_PRIVATE_BASE)pHgsmi;
    int rc = nemuCrHgsmiPrivateCtlConGetHostCaps(pHgsmiPrivate, pu32HostCaps);
    if (!RT_SUCCESS(rc))
    {
        WARN(("nemuCrHgsmiPrivateCtlConGetHostCaps failed with rc (%d)", rc));
    }
    return rc;
}

NEMUCRHGSMI_DECL(int) NemuCrHgsmiCtlConCall(PNEMUUHGSMI pHgsmi, struct NemuGuestHGCMCallInfo *pCallInfo, int cbCallInfo)
{
    PNEMUUHGSMI_PRIVATE_BASE pHgsmiPrivate = (PNEMUUHGSMI_PRIVATE_BASE)pHgsmi;
    int rc = nemuCrHgsmiPrivateCtlConCall(pHgsmiPrivate, pCallInfo, cbCallInfo);
    if (!RT_SUCCESS(rc))
    {
        WARN(("NemuCrHgsmiPrivateCtlConCall failed with rc (%d)", rc));
    }
    return rc;
}
