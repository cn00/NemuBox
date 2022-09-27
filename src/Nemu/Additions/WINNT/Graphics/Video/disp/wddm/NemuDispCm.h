/* $Id: NemuDispCm.h $ */

/** @file
 * NemuVideo Display D3D User mode dll
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

#ifndef ___NemuDispCm_h___
#define ___NemuDispCm_h___

typedef struct NEMUWDDMDISP_DEVICE *PNEMUWDDMDISP_DEVICE;
typedef struct NEMUWDDMDISP_CONTEXT *PNEMUWDDMDISP_CONTEXT;

HRESULT nemuDispCmInit();
HRESULT nemuDispCmTerm();

HRESULT nemuDispCmCtxDestroy(PNEMUWDDMDISP_DEVICE pDevice, PNEMUWDDMDISP_CONTEXT pContext);
HRESULT nemuDispCmCtxCreate(PNEMUWDDMDISP_DEVICE pDevice, PNEMUWDDMDISP_CONTEXT pContext);

HRESULT nemuDispCmCmdGet(PNEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD pCmd, uint32_t cbCmd, DWORD dwMilliseconds);

HRESULT nemuDispCmCmdInterruptWait();

void nemuDispCmLog(LPCSTR pszMsg);

#endif /* #ifdef ___NemuDispCm_h___ */
