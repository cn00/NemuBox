/* $Id: NemuMPVModes.h $ */

/** @file
 * Nemu WDDM Miniport driver
 */

/*
 * Copyright (C) 2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuMPVModes_h__
#define ___NemuMPVModes_h__

//#include "../../common/NemuVideoTools.h"

#include <cr_sortarray.h>


#define _CR_TYPECAST(_Type, _pVal) ((_Type*)((void*)(_pVal)))

DECLINLINE(uint64_t) nemuRSize2U64(RTRECTSIZE size) { return *_CR_TYPECAST(uint64_t, &(size)); }
DECLINLINE(RTRECTSIZE) nemuU642RSize2(uint64_t size) { return *_CR_TYPECAST(RTRECTSIZE, &(size)); }

#define CR_RSIZE2U64 nemuRSize2U64
#define CR_U642RSIZE nemuU642RSize2

int NemuWddmVModesInit(PNEMUMP_DEVEXT pExt);
void NemuWddmVModesCleanup();
const CR_SORTARRAY* NemuWddmVModesGet(PNEMUMP_DEVEXT pExt, uint32_t u32Target);
int NemuWddmVModesRemove(PNEMUMP_DEVEXT pExt, uint32_t u32Target, const RTRECTSIZE *pResolution);
int NemuWddmVModesAdd(PNEMUMP_DEVEXT pExt, uint32_t u32Target, const RTRECTSIZE *pResolution, BOOLEAN fTrancient);

NTSTATUS NemuWddmChildStatusReportReconnected(PNEMUMP_DEVEXT pDevExt, uint32_t iChild);
NTSTATUS NemuWddmChildStatusConnect(PNEMUMP_DEVEXT pDevExt, uint32_t iChild, BOOLEAN fConnect);

#endif /* ___NemuMPVModes_h__ */
