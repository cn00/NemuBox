/* $Id: NemuNetFltP-win.h $ */
/** @file
 * NemuNetFltP-win.h - Bridged Networking Driver, Windows Specific Code.
 * Protocol edge API
 */
/*
 * Copyright (C) 2011-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___NemuNetFltP_win_h___
#define ___NemuNetFltP_win_h___

#ifdef NEMUNETADP
# error "No protocol edge"
#endif
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtRegister(PNEMUNETFLTGLOBALS_PT pGlobalsPt, PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPathStr);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtDeregister(PNEMUNETFLTGLOBALS_PT pGlobalsPt);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtDoUnbinding(PNEMUNETFLTINS pNetFlt, bool bOnUnbind);
DECLHIDDEN(VOID) nemuNetFltWinPtRequestComplete(NDIS_HANDLE hContext, PNDIS_REQUEST pNdisRequest, NDIS_STATUS Status);
DECLHIDDEN(bool) nemuNetFltWinPtCloseInterface(PNEMUNETFLTINS pNetFlt, PNDIS_STATUS pStatus);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtDoBinding(PNEMUNETFLTINS pThis, PNDIS_STRING pOurDeviceName, PNDIS_STRING pBindToDeviceName);
#endif /* #ifndef ___NemuNetFltP_win_h___ */
