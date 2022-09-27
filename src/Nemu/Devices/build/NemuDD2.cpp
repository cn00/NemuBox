/* $Id: NemuDD2.cpp $ */
/** @file
 * NemuDD2 - Built-in drivers & devices part 2.
 *
 * These drivers and devices are in separate modules because of LGPL.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
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
#define LOG_GROUP LOG_GROUP_DEV
#include <Nemu/vmm/pdm.h>
#include <Nemu/version.h>
#include <Nemu/err.h>

#include <Nemu/log.h>
#include <iprt/assert.h>

#include "NemuDD2.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
const void *g_apvNemuDDDependencies2[] =
{
    (void *)&g_abPcBiosBinary,
    (void *)&g_abVgaBiosBinary,
#ifdef NEMU_WITH_PXE_ROM
    (void *)&g_abNetBiosBinary,
#endif
};


/**
 * Register builtin devices.
 *
 * @returns Nemu status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      Nemu version number.
 */
extern "C" DECLEXPORT(int) NemuDevicesRegister(PPDMDEVREGCB pCallbacks, uint32_t u32Version)
{
    LogFlow(("NemuDevicesRegister: u32Version=%#x\n", u32Version));
    AssertReleaseMsg(u32Version == NEMU_VERSION, ("u32Version=%#x NEMU_VERSION=%#x\n", u32Version, NEMU_VERSION));
    int rc;

    rc = pCallbacks->pfnRegister(pCallbacks, &g_DeviceAPIC);
    if (RT_FAILURE(rc))
        return rc;
    rc = pCallbacks->pfnRegister(pCallbacks, &g_DeviceIOAPIC);
    if (RT_FAILURE(rc))
        return rc;
    rc = pCallbacks->pfnRegister(pCallbacks, &g_DeviceLPC);
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}

