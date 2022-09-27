/* $Id: NemuMPHGSMI.cpp $ */

/** @file
 * Nemu Miniport HGSMI related functions
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

#include "NemuMPHGSMI.h"
#include "NemuMPCommon.h"
#include <Nemu/VMMDev.h>
#include <iprt/alloc.h>

static DECLCALLBACK(void *) hgsmiEnvAlloc(void *pvEnv, HGSMISIZE cb)
{
    NOREF(pvEnv);
    return RTMemAlloc(cb);
}

static DECLCALLBACK(void) hgsmiEnvFree(void *pvEnv, void *pv)
{
    NOREF(pvEnv);
    RTMemFree(pv);
}

static HGSMIENV g_hgsmiEnvMP =
{
    NULL,
    hgsmiEnvAlloc,
    hgsmiEnvFree
};

/**
 * Helper function to register secondary displays (DualView). Note that this will not
 * be available on pre-XP versions, and some editions on XP will fail because they are
 * intentionally crippled.
 *
 * HGSMI variant is a bit different because it uses only HGSMI interface (VBVA channel)
 * to talk to the host.
 */
void NemuSetupDisplaysHGSMI(PNEMUMP_COMMON pCommon, PHYSICAL_ADDRESS phVRAM, uint32_t ulApertureSize,
                            uint32_t cbVRAM, uint32_t fCaps)
{
    /** @todo I simply converted this from Windows error codes.  That is wrong,
     * but we currently freely mix and match those (failure == rc > 0) and iprt
     * ones (failure == rc < 0) anyway.  This needs to be fully reviewed and
     * fixed. */
    int rc = VINF_SUCCESS;
    uint32_t offVRAMBaseMapping, cbMapping, offGuestHeapMemory, cbGuestHeapMemory,
             offHostFlags, offVRAMHostArea, cbHostArea;
    LOGF_ENTER();

    memset(pCommon, 0, sizeof(*pCommon));
    pCommon->phVRAM = phVRAM;
    pCommon->ulApertureSize = ulApertureSize;
    pCommon->cbVRAM    = cbVRAM;
    pCommon->cDisplays = 1;
    pCommon->bHGSMI    = NemuHGSMIIsSupported();

    if (pCommon->bHGSMI)
    {
        NemuHGSMIGetBaseMappingInfo(pCommon->cbVRAM, &offVRAMBaseMapping,
                                    &cbMapping, &offGuestHeapMemory,
                                    &cbGuestHeapMemory, &offHostFlags);

        /* Map the adapter information. It will be needed for HGSMI IO. */
        rc = NemuMPCmnMapAdapterMemory(pCommon, &pCommon->pvAdapterInformation, offVRAMBaseMapping, cbMapping);
        if (RT_FAILURE(rc))
        {
            LOG(("NemuMPCmnMapAdapterMemory failed rc = %d", rc));
            pCommon->bHGSMI = false;
        }
        else
        {
            /* Setup an HGSMI heap within the adapter information area. */
            rc = NemuHGSMISetupGuestContext(&pCommon->guestCtx,
                                            pCommon->pvAdapterInformation,
                                            cbGuestHeapMemory,
                                              offVRAMBaseMapping
                                            + offGuestHeapMemory,
                                            &g_hgsmiEnvMP);

            if (RT_FAILURE(rc))
            {
                LOG(("HGSMIHeapSetup failed rc = %d", rc));
                pCommon->bHGSMI = false;
            }
        }
    }

    /* Setup the host heap and the adapter memory. */
    if (pCommon->bHGSMI)
    {
        NemuHGSMIGetHostAreaMapping(&pCommon->guestCtx, pCommon->cbVRAM,
                                    offVRAMBaseMapping, &offVRAMHostArea,
                                    &cbHostArea);
        if (cbHostArea)
        {

            /* Map the heap region.
             *
             * Note: the heap will be used for the host buffers submitted to the guest.
             *       The miniport driver is responsible for reading FIFO and notifying
             *       display drivers.
             */
            pCommon->cbMiniportHeap = cbHostArea;
            rc = NemuMPCmnMapAdapterMemory (pCommon, &pCommon->pvMiniportHeap,
                                       offVRAMHostArea, cbHostArea);
            if (RT_FAILURE(rc))
            {
                pCommon->pvMiniportHeap = NULL;
                pCommon->cbMiniportHeap = 0;
                pCommon->bHGSMI = false;
            }
            else
                NemuHGSMISetupHostContext(&pCommon->hostCtx,
                                          pCommon->pvAdapterInformation,
                                          offHostFlags,
                                          pCommon->pvMiniportHeap,
                                          offVRAMHostArea, cbHostArea);
        }
        else
        {
            /* Host has not requested a heap. */
            pCommon->pvMiniportHeap = NULL;
            pCommon->cbMiniportHeap = 0;
        }
    }

    if (pCommon->bHGSMI)
    {
        /* Setup the information for the host. */
        rc = NemuHGSMISendHostCtxInfo(&pCommon->guestCtx,
                                      offVRAMBaseMapping + offHostFlags,
                                      fCaps, offVRAMHostArea,
                                      pCommon->cbMiniportHeap);

        if (RT_FAILURE(rc))
        {
            pCommon->bHGSMI = false;
        }
    }

    /* Check whether the guest supports multimonitors. */
    if (pCommon->bHGSMI)
    {
        /* Query the configured number of displays. */
        pCommon->cDisplays = NemuHGSMIGetMonitorCount(&pCommon->guestCtx);
    }
    else
    {
        NemuFreeDisplaysHGSMI(pCommon);
    }

    LOGF_LEAVE();
}

static bool NemuUnmapAdpInfoCallback(void *pvCommon)
{
    PNEMUMP_COMMON pCommon = (PNEMUMP_COMMON)pvCommon;

    pCommon->hostCtx.pfHostFlags = NULL;
    return true;
}

void NemuFreeDisplaysHGSMI(PNEMUMP_COMMON pCommon)
{
    NemuMPCmnUnmapAdapterMemory(pCommon, &pCommon->pvMiniportHeap);
#ifdef NEMU_WDDM_MINIPORT
    NemuSHGSMITerm(&pCommon->guestCtx.heapCtx);
#else
    HGSMIHeapDestroy(&pCommon->guestCtx.heapCtx);
#endif

    /* Unmap the adapter information needed for HGSMI IO. */
    NemuMPCmnSyncToVideoIRQ(pCommon, NemuUnmapAdpInfoCallback, pCommon);
    NemuMPCmnUnmapAdapterMemory(pCommon, &pCommon->pvAdapterInformation);
}
