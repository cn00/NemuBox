/* $Id: NemuDrvCfg-win.h $ */
/** @file
 * Windows Driver Manipulation API.
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

#ifndef ___Nemu_NemuDrvCfg_win_h
#define ___Nemu_NemuDrvCfg_win_h

#include <Windows.h>
#include <Nemu/cdefs.h>

RT_C_DECLS_BEGIN

#if 0
/* enable this in case we include this in a dll*/
# ifdef IN_NEMUDRVCFG
#  define NEMUDRVCFG_DECL(a_Type) DECLEXPORT(a_Type)
# else
#  define NEMUDRVCFG_DECL(a_Type) DECLIMPORT(a_Type)
# endif
#else
/*enable this in case we include this in a static lib*/
# define NEMUDRVCFG_DECL(a_Type) a_Type NEMUCALL
#endif

typedef enum
{
    NEMUDRVCFG_LOG_SEVERITY_FLOW = 1,
    NEMUDRVCFG_LOG_SEVERITY_REGULAR,
    NEMUDRVCFG_LOG_SEVERITY_REL
} NEMUDRVCFG_LOG_SEVERITY;

typedef DECLCALLBACK(void) FNNEMUDRVCFG_LOG(NEMUDRVCFG_LOG_SEVERITY enmSeverity, char *pszMsg, void *pvContext);
typedef FNNEMUDRVCFG_LOG *PFNNEMUDRVCFG_LOG;

NEMUDRVCFG_DECL(void) NemuDrvCfgLoggerSet(PFNNEMUDRVCFG_LOG pfnLog, void *pvLog);

typedef DECLCALLBACK(void) FNNEMUDRVCFG_PANIC(void * pvPanic);
typedef FNNEMUDRVCFG_PANIC *PFNNEMUDRVCFG_PANIC;
NEMUDRVCFG_DECL(void) NemuDrvCfgPanicSet(PFNNEMUDRVCFG_PANIC pfnPanic, void *pvPanic);

/* Driver package API*/
NEMUDRVCFG_DECL(HRESULT) NemuDrvCfgInfInstall(IN LPCWSTR lpszInfPath);
NEMUDRVCFG_DECL(HRESULT) NemuDrvCfgInfUninstall(IN LPCWSTR lpszInfPath, IN DWORD fFlags);
NEMUDRVCFG_DECL(HRESULT) NemuDrvCfgInfUninstallAllSetupDi(IN const GUID * pGuidClass, IN LPCWSTR lpszClassName, IN LPCWSTR lpszPnPId, IN DWORD fFlags);
NEMUDRVCFG_DECL(HRESULT) NemuDrvCfgInfUninstallAllF(IN LPCWSTR lpszClassName, IN LPCWSTR lpszPnPId, IN DWORD fFlags);

/* Service API */
NEMUDRVCFG_DECL(HRESULT) NemuDrvCfgSvcStart(LPCWSTR lpszSvcName);

HRESULT NemuDrvCfgDrvUpdate(LPCWSTR pcszwHwId, LPCWSTR pcsxwInf, BOOL *pbRebootRequired);

RT_C_DECLS_END

#endif

