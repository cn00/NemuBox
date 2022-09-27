/* $Id: NemuMPRegistry.cpp $ */

/** @file
 * Nemu XPDM Miniport registry related functions
 */

/*
 * Copyright (C) 2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "common/NemuMPCommon.h"

static VP_STATUS
NemuMPQueryNamedValueCB(PVOID HwDeviceExtension, PVOID Context,
                        PWSTR ValueName, PVOID ValueData, ULONG ValueLength)
{
    PAGED_CODE();

    if (!ValueLength || !Context)
    {
        WARN(("failed due to invalid parameters"));
        return ERROR_INVALID_PARAMETER;
    }

    *(uint32_t *)Context = *(uint32_t *)ValueData;

    return NO_ERROR;
}


VP_STATUS NemuMPCmnRegInit(IN PNEMUMP_DEVEXT pExt, OUT NEMUMPCMNREGISTRY *pReg)
{
    *pReg = pExt->pPrimary;
    return NO_ERROR;
}

VP_STATUS NemuMPCmnRegFini(IN NEMUMPCMNREGISTRY Reg)
{
    return NO_ERROR;
}

VP_STATUS NemuMPCmnRegSetDword(IN NEMUMPCMNREGISTRY Reg, PWSTR pName, uint32_t Val)
{
    return VideoPortSetRegistryParameters(Reg, pName, &Val, sizeof(Val));
}

VP_STATUS NemuMPCmnRegQueryDword(IN NEMUMPCMNREGISTRY Reg, PWSTR pName, uint32_t *pVal)
{
    VP_STATUS rc;

    rc = VideoPortGetRegistryParameters(Reg, pName, FALSE, NemuMPQueryNamedValueCB, pVal);
    if (rc!=NO_ERROR && pVal)
    {
        *pVal = 0;
    }
    return rc;
}
