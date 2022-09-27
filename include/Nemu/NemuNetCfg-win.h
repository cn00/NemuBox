/* $Id: NemuNetCfg-win.h $ */
/** @file
 * Network Configuration API for Windows platforms.
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

#ifndef ___Nemu_NemuNetCfg_win_h
#define ___Nemu_NemuNetCfg_win_h

/*
 * Defining NEMUNETCFG_DELAYEDRENAME postpones renaming of host-only adapter
 * connection during adapter creation after it has been assigned with an
 * IP address. This hopefully prevents collisions that may happen when we
 * attempt to rename a connection too early, while its configuration is
 * still being 'committed' by the network setup engine.
 */
#define NEMUNETCFG_DELAYEDRENAME

#include <winsock2.h>
#include <Windows.h>
#include <Netcfgn.h>
#include <Setupapi.h>
#include <Nemu/cdefs.h>

/** @defgroup grp_nemunetcfgwin     The Windows Network Configration Library
 * @{ */

/** @def NEMUNETCFGWIN_DECL
 * The usual declaration wrapper.
 */
#if 0
/* enable this in case we include this in a dll*/
# ifdef IN_NEMUDDU
#  define NEMUNETCFGWIN_DECL(a_Type) DECLEXPORT(a_Type)
# else
#  define NEMUNETCFGWIN_DECL(a_Type) DECLIMPORT(a_Type)
# endif
#else
/*enable this in case we include this in a static lib*/
# define NEMUNETCFGWIN_DECL(a_Type) a_Type NEMUCALL
#endif

RT_C_DECLS_BEGIN

NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinQueryINetCfg(OUT INetCfg **ppNetCfg,
                          IN BOOL fGetWriteLock,
                          IN LPCWSTR pszwClientDescription,
                          IN DWORD cmsTimeout,
                          OUT LPWSTR *ppszwClientDescription);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinReleaseINetCfg(IN INetCfg *pNetCfg, IN BOOL fHasWriteLock);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinGetComponentByGuid(IN INetCfg *pNc, IN const GUID *pguidClass,
                                                            IN const GUID * pComponentGuid, OUT INetCfgComponent **ppncc);

NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinNetFltInstall(IN INetCfg *pNc, IN LPCWSTR const * apInfFullPaths, IN UINT cInfFullPaths);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinNetFltUninstall(IN INetCfg *pNc);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinNetLwfInstall(IN INetCfg *pNc, IN LPCWSTR const pInfFullPath);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinNetLwfUninstall(IN INetCfg *pNc);

NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinNetAdpUninstall(IN INetCfg *pNc, IN LPCWSTR pwszId);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinNetAdpInstall(IN INetCfg *pNc,IN LPCWSTR const pInfFullPath);

#ifndef NEMUNETCFG_DELAYEDRENAME
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinCreateHostOnlyNetworkInterface(IN LPCWSTR pInfPath, IN bool bIsInfPathFile,
                                                                        OUT GUID *pGuid, OUT BSTR *lppszName,
                                                                        OUT BSTR *pErrMsg);
#else /* NEMUNETCFG_DELAYEDRENAME */
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinCreateHostOnlyNetworkInterface(IN LPCWSTR pInfPath, IN bool bIsInfPathFile,
                                                                        OUT GUID *pGuid, OUT BSTR *lppszId,
                                                                        OUT BSTR *pErrMsg);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinRenameHostOnlyConnection(IN const GUID *pGuid, IN LPCWSTR pszId,  OUT BSTR *pDevName);
#endif /* NEMUNETCFG_DELAYEDRENAME */
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinUpdateHostOnlyNetworkInterface(LPCWSTR pcsxwInf, BOOL *pbRebootRequired, LPCWSTR pcsxwId);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinRemoveHostOnlyNetworkInterface(IN const GUID *pGUID, OUT BSTR *pErrMsg);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinRemoveAllNetDevicesOfId(IN LPCWSTR lpszPnPId);

typedef enum
{
    NEMUNECTFGWINPROPCHANGE_TYPE_UNDEFINED = 0,
    NEMUNECTFGWINPROPCHANGE_TYPE_DISABLE,
    NEMUNECTFGWINPROPCHANGE_TYPE_ENABLE
} NEMUNECTFGWINPROPCHANGE_TYPE;

NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinPropChangeAllNetDevicesOfId(IN LPCWSTR lpszPnPId, NEMUNECTFGWINPROPCHANGE_TYPE enmPcType);

NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinGenHostOnlyNetworkNetworkIp(OUT PULONG pNetIp, OUT PULONG pNetMask);

typedef struct ADAPTER_SETTINGS
{
    ULONG ip;
    ULONG mask;
    BOOL bDhcp;
} ADAPTER_SETTINGS, *PADAPTER_SETTINGS; /**< I'm not prefixed */

NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinEnableStaticIpConfig(IN const GUID *pGuid, IN ULONG ip, IN ULONG mask);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinGetAdapterSettings(IN const GUID * pGuid, OUT PADAPTER_SETTINGS pSettings);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinEnableDynamicIpConfig(IN const GUID *pGuid);
NEMUNETCFGWIN_DECL(HRESULT) NemuNetCfgWinDhcpRediscover(IN const GUID *pGuid);


typedef VOID (*LOG_ROUTINE)(LPCSTR szString); /**< I'm not prefixed. */
NEMUNETCFGWIN_DECL(VOID) NemuNetCfgWinSetLogging(IN LOG_ROUTINE pfnLog);

RT_C_DECLS_END

/** @} */

#endif

