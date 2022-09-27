/* $Id: NemuDebugLib.h $ */
/** @file
 * NemuDebugLib.h - Debug and logging routines implemented by NemuDebugLib.
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

#ifndef ___NemuPkg_NemuDebugLib_h
#define ___NemuPkg_NemuDebugLib_h

#include <Uefi/UefiBaseType.h>
#include "NemuPkg.h"

size_t NemuPrintChar(int ch);
size_t NemuPrintGuid(CONST EFI_GUID *pGuid);
size_t NemuPrintHex(UINT64 uValue, size_t cbType);
size_t NemuPrintHexDump(const void *pv, size_t cb);
size_t NemuPrintString(const char *pszString);

#endif

