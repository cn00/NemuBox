/* $Id: NemuNetFltM-win.h $ */
/** @file
 * NemuNetFltM-win.h - Bridged Networking Driver, Windows Specific Code - Miniport edge API
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

#ifndef ___NemuNetFltM_win_h___
#define ___NemuNetFltM_win_h___

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpRegister(PNEMUNETFLTGLOBALS_MP pGlobalsMp, PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPathStr);
DECLHIDDEN(VOID) nemuNetFltWinMpDeregister(PNEMUNETFLTGLOBALS_MP pGlobalsMp);
DECLHIDDEN(VOID) nemuNetFltWinMpReturnPacket(IN NDIS_HANDLE hMiniportAdapterContext, IN PNDIS_PACKET pPacket);

#ifdef NEMUNETADP
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpDoInitialization(PNEMUNETFLTINS pThis, NDIS_HANDLE hMiniportAdapter, NDIS_HANDLE hWrapperConfigurationContext);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpDoDeinitialization(PNEMUNETFLTINS pThis);

#else

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpInitializeDevideInstance(PNEMUNETFLTINS pThis);
DECLHIDDEN(bool) nemuNetFltWinMpDeInitializeDeviceInstance(PNEMUNETFLTINS pThis, PNDIS_STATUS pStatus);

DECLINLINE(VOID) nemuNetFltWinMpRequestStateComplete(PNEMUNETFLTINS pNetFlt)
{
    RTSpinlockAcquire(pNetFlt->hSpinlock);
    pNetFlt->u.s.WinIf.StateFlags.fRequestInfo = 0;
    RTSpinlockRelease(pNetFlt->hSpinlock);
}

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpRequestPost(PNEMUNETFLTINS pNetFlt);
#endif

#endif /* !___NemuNetFltM_win_h___ */

