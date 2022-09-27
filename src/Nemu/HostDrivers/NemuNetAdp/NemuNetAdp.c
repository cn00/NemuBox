/* $Id: NemuNetAdp.c $ */
/** @file
 * NemuNetAdp - Virtual Network Adapter Driver (Host), Common Code.
 */

/*
 * Copyright (C) 2008-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/** @page pg_netadp     NemuNetAdp - Network Adapter
 *
 * This is a kernel module that creates a virtual interface that can be attached
 * to an internal network.
 *
 * In the big picture we're one of the three trunk interface on the internal
 * network, the one named "TAP Interface": @image html Networking_Overview.gif
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_NET_ADP_DRV
#include "NemuNetAdpInternal.h"

#include <Nemu/log.h>
#include <Nemu/err.h>
#include <iprt/string.h>


NEMUNETADP g_aAdapters[NEMUNETADP_MAX_INSTANCES];
static uint8_t g_aUnits[NEMUNETADP_MAX_UNITS/8];


DECLINLINE(int) nemuNetAdpGetUnitByName(const char *pcszName)
{
    uint32_t iUnit = RTStrToUInt32(pcszName + sizeof(NEMUNETADP_NAME) - 1);
    bool fOld;

    if (iUnit >= NEMUNETADP_MAX_UNITS)
        return -1;

    fOld = ASMAtomicBitTestAndSet(g_aUnits, iUnit);
    return fOld ? -1 : (int)iUnit;
}

DECLINLINE(int) nemuNetAdpGetNextAvailableUnit(void)
{
    bool fOld;
    int iUnit;
    /* There is absolutely no chance that all units are taken */
    do {
        iUnit = ASMBitFirstClear(g_aUnits, NEMUNETADP_MAX_UNITS);
        if (iUnit < 0)
            break;
        fOld = ASMAtomicBitTestAndSet(g_aUnits, iUnit);
    } while (fOld);

    return iUnit;
}

DECLINLINE(void) nemuNetAdpReleaseUnit(int iUnit)
{
    bool fSet = ASMAtomicBitTestAndClear(g_aUnits, iUnit);
    NOREF(fSet);
    Assert(fSet);
}

/**
 * Generate a suitable MAC address.
 *
 * @param   pThis       The instance.
 * @param   pMac        Where to return the MAC address.
 */
DECLHIDDEN(void) nemuNetAdpComposeMACAddress(PNEMUNETADP pThis, PRTMAC pMac)
{
    /* Use a locally administered version of the OUI we use for the guest NICs. */
    pMac->au8[0] = 0x08 | 2;
    pMac->au8[1] = 0x00;
    pMac->au8[2] = 0x27;

    pMac->au8[3] = 0; /* pThis->iUnit >> 16; */
    pMac->au8[4] = 0; /* pThis->iUnit >> 8; */
    pMac->au8[5] = pThis->iUnit;
}

int nemuNetAdpCreate(PNEMUNETADP *ppNew, const char *pcszName)
{
    int rc;
    unsigned i;
    for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
    {
        PNEMUNETADP pThis = &g_aAdapters[i];

        if (ASMAtomicCmpXchgU32((uint32_t volatile *)&pThis->enmState, kNemuNetAdpState_Transitional, kNemuNetAdpState_Invalid))
        {
            RTMAC Mac;
            /* Found an empty slot -- use it. */
            Log(("nemuNetAdpCreate: found empty slot: %d\n", i));
            if (pcszName)
            {
                Log(("nemuNetAdpCreate: using name: %s\n", pcszName));
                pThis->iUnit = nemuNetAdpGetUnitByName(pcszName);
                strncpy(pThis->szName, pcszName, sizeof(pThis->szName));
                pThis->szName[sizeof(pThis->szName) - 1] = '\0';
            }
            else
            {
                pThis->iUnit = nemuNetAdpGetNextAvailableUnit();
                pThis->szName[0] = '\0';
            }
            if (pThis->iUnit < 0)
                rc = VERR_INVALID_PARAMETER;
            else
            {
                nemuNetAdpComposeMACAddress(pThis, &Mac);
                rc = nemuNetAdpOsCreate(pThis, &Mac);
                Log(("nemuNetAdpCreate: pThis=%p pThis->iUnit=%d, pThis->szName=%s\n",
                     pThis, pThis->iUnit, pThis->szName));
            }
            if (RT_SUCCESS(rc))
            {
                *ppNew = pThis;
                ASMAtomicWriteU32((uint32_t volatile *)&pThis->enmState, kNemuNetAdpState_Active);
                Log2(("NemuNetAdpCreate: Created %s\n", g_aAdapters[i].szName));
            }
            else
            {
                ASMAtomicWriteU32((uint32_t volatile *)&pThis->enmState, kNemuNetAdpState_Invalid);
                Log(("nemuNetAdpCreate: nemuNetAdpOsCreate failed with '%Rrc'.\n", rc));
            }
            for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
                Log2(("NemuNetAdpCreate: Scanning entry: state=%d unit=%d name=%s\n",
                      g_aAdapters[i].enmState, g_aAdapters[i].iUnit, g_aAdapters[i].szName));
            return rc;
        }
    }
    Log(("nemuNetAdpCreate: no empty slots!\n"));

    /* All slots in adapter array are busy. */
    return VERR_OUT_OF_RESOURCES;
}

int nemuNetAdpDestroy(PNEMUNETADP pThis)
{
    int rc = VINF_SUCCESS;

    if (!ASMAtomicCmpXchgU32((uint32_t volatile *)&pThis->enmState, kNemuNetAdpState_Transitional, kNemuNetAdpState_Active))
        return VERR_INTNET_FLT_IF_BUSY;

    Assert(pThis->iUnit >= 0 && pThis->iUnit < NEMUNETADP_MAX_UNITS);
    nemuNetAdpOsDestroy(pThis);
    nemuNetAdpReleaseUnit(pThis->iUnit);
    pThis->iUnit = -1;
    pThis->szName[0] = '\0';

    ASMAtomicWriteU32((uint32_t volatile *)&pThis->enmState, kNemuNetAdpState_Invalid);

    return rc;
}

int  nemuNetAdpInit(void)
{
    unsigned i;
    /*
     * Init common members and call OS-specific init.
     */
    memset(g_aUnits, 0, sizeof(g_aUnits));
    memset(g_aAdapters, 0, sizeof(g_aAdapters));
    LogFlow(("nemunetadp: max host-only interfaces supported: %d (%d bytes)\n",
             NEMUNETADP_MAX_INSTANCES, sizeof(g_aAdapters)));
    for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
    {
        g_aAdapters[i].enmState = kNemuNetAdpState_Invalid;
        g_aAdapters[i].iUnit    = -1;
        nemuNetAdpOsInit(&g_aAdapters[i]);
    }

    return VINF_SUCCESS;
}

/**
 * Finds an adapter by its name.
 *
 * @returns Pointer to the instance by the given name. NULL if not found.
 * @param   pszName         The name of the instance.
 */
PNEMUNETADP nemuNetAdpFindByName(const char *pszName)
{
    unsigned i;

    for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
    {
        PNEMUNETADP pThis = &g_aAdapters[i];
        Log2(("NemuNetAdp: Scanning entry: state=%d name=%s\n", pThis->enmState, pThis->szName));
        if (   strcmp(pThis->szName, pszName) == 0
            && ASMAtomicReadU32((uint32_t volatile *)&pThis->enmState) == kNemuNetAdpState_Active)
            return pThis;
    }
    return NULL;
}

void nemuNetAdpShutdown(void)
{
    unsigned i;

    /* Remove virtual adapters */
    for (i = 0; i < RT_ELEMENTS(g_aAdapters); i++)
        nemuNetAdpDestroy(&g_aAdapters[i]);
}
