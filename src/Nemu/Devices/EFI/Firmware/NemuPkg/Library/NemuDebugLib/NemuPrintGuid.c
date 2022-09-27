/* $Id: NemuPrintGuid.c $ */
/** @file
 * NemuPrintGuid.c - Implementation of the NemuPrintGuid() debug logging routine.
 */

/*
 * Copyright (C) 2009-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */




/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "NemuDebugLib.h"
#include "DevEFI.h"


/**
 * Prints a EFI GUID.
 *
 * @returns Number of bytes printed.
 *
 * @param   pGuid           The GUID to print
 */
size_t NemuPrintGuid(CONST EFI_GUID *pGuid)
{
    NemuPrintHex(pGuid->Data1, sizeof(pGuid->Data1));
    NemuPrintChar('-');
    NemuPrintHex(pGuid->Data2, sizeof(pGuid->Data2));
    NemuPrintChar('-');
    NemuPrintHex(pGuid->Data3, sizeof(pGuid->Data3));
    NemuPrintChar('-');
    NemuPrintHex(pGuid->Data4[0], sizeof(pGuid->Data4[0]));
    NemuPrintHex(pGuid->Data4[1], sizeof(pGuid->Data4[1]));
    NemuPrintChar('-');
    NemuPrintHex(pGuid->Data4[2], sizeof(pGuid->Data4[2]));
    NemuPrintHex(pGuid->Data4[3], sizeof(pGuid->Data4[3]));
    NemuPrintHex(pGuid->Data4[4], sizeof(pGuid->Data4[4]));
    NemuPrintHex(pGuid->Data4[5], sizeof(pGuid->Data4[5]));
    NemuPrintHex(pGuid->Data4[6], sizeof(pGuid->Data4[6]));
    NemuPrintHex(pGuid->Data4[7], sizeof(pGuid->Data4[7]));
    return 37;
}

