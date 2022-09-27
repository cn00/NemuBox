/** @file
 *
 * NemuGINA -- Windows Logon DLL for VirtualBox
 *
 * Copyright (C) 2006-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include <iprt/buildconfig.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>

#include <Nemu/NemuGuestLib.h>

#include "winwlx.h"
#include "NemuGINA.h"
#include "Helper.h"
#include "Dialog.h"

/*
 * Global variables.
 */

/** DLL instance handle. */
HINSTANCE hDllInstance;

/** Version of Winlogon. */
DWORD wlxVersion;

/** Handle to Winlogon service. */
HANDLE hGinaWlx;
/** Winlog function dispatch table. */
PWLX_DISPATCH_VERSION_1_1 pWlxFuncs;

/**
 * Function pointers to MSGINA entry points.
 */
PGWLXNEGOTIATE GWlxNegotiate;
PGWLXINITIALIZE GWlxInitialize;
PGWLXDISPLAYSASNOTICE GWlxDisplaySASNotice;
PGWLXLOGGEDOUTSAS GWlxLoggedOutSAS;
PGWLXACTIVATEUSERSHELL GWlxActivateUserShell;
PGWLXLOGGEDONSAS GWlxLoggedOnSAS;
PGWLXDISPLAYLOCKEDNOTICE GWlxDisplayLockedNotice;
PGWLXWKSTALOCKEDSAS GWlxWkstaLockedSAS;
PGWLXISLOCKOK GWlxIsLockOk;
PGWLXISLOGOFFOK GWlxIsLogoffOk;
PGWLXLOGOFF GWlxLogoff;
PGWLXSHUTDOWN GWlxShutdown;
/* GINA 1.1. */
PGWLXSTARTAPPLICATION GWlxStartApplication;
PGWLXSCREENSAVERNOTIFY GWlxScreenSaverNotify;
/* GINA 1.3. */
PGWLXNETWORKPROVIDERLOAD GWlxNetworkProviderLoad;
PGWLXDISPLAYSTATUSMESSAGE GWlxDisplayStatusMessage;
PGWLXGETSTATUSMESSAGE GWlxGetStatusMessage;
PGWLXREMOVESTATUSMESSAGE GWlxRemoveStatusMessage;
/* GINA 1.4. */
PGWLXGETCONSOLESWITCHCREDENTIALS GWlxGetConsoleSwitchCredentials;
PGWLXRECONNECTNOTIFY GWlxReconnectNotify;
PGWLXDISCONNECTNOTIFY GWlxDisconnectNotify;


/**
 * DLL entry point.
 */
BOOL WINAPI DllMain(HINSTANCE hInstance,
                    DWORD     dwReason,
                    LPVOID    lpReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            RTR3InitDll(RTR3INIT_FLAGS_UNOBTRUSIVE);
            VbglR3Init();

            NemuGINALoadConfiguration();

            NemuGINAVerbose(0, "NemuGINA: v%s r%s (%s %s) loaded\n",
                            RTBldCfgVersion(), RTBldCfgRevisionStr(),
                            __DATE__, __TIME__);

            DisableThreadLibraryCalls(hInstance);
            hDllInstance = hInstance;
            break;
        }

        case DLL_PROCESS_DETACH:
        {
            NemuGINAVerbose(0, "NemuGINA: Unloaded\n");
            VbglR3Term();
            /// @todo RTR3Term();
            break;
        }

        default:
            break;
    }
    return TRUE;
}


BOOL WINAPI WlxNegotiate(DWORD dwWinlogonVersion,
                         DWORD *pdwDllVersion)
{
    NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: dwWinlogonVersion: %ld\n", dwWinlogonVersion);

    /* Load the standard Microsoft GINA DLL. */
    RTLDRMOD hLdrMod;
    int rc = RTLdrLoadSystem("MSGINA.DLL", true /*fNoUnload*/, &hLdrMod);
    if (RT_FAILURE(rc))
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed loading MSGINA! rc=%Rrc\n", rc);
        return FALSE;
    }

    /*
     * Now get the entry points of the MSGINA
     */
    GWlxNegotiate = (PGWLXNEGOTIATE)RTLdrGetFunction(hLdrMod, "WlxNegotiate");
    if (!GWlxNegotiate)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxNegotiate\n");
        return FALSE;
    }
    GWlxInitialize = (PGWLXINITIALIZE)RTLdrGetFunction(hLdrMod, "WlxInitialize");
    if (!GWlxInitialize)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxInitialize\n");
        return FALSE;
    }
    GWlxDisplaySASNotice =
        (PGWLXDISPLAYSASNOTICE)RTLdrGetFunction(hLdrMod, "WlxDisplaySASNotice");
    if (!GWlxDisplaySASNotice)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxDisplaySASNotice\n");
        return FALSE;
    }
    GWlxLoggedOutSAS =
        (PGWLXLOGGEDOUTSAS)RTLdrGetFunction(hLdrMod, "WlxLoggedOutSAS");
    if (!GWlxLoggedOutSAS)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxLoggedOutSAS\n");
        return FALSE;
    }
    GWlxActivateUserShell =
        (PGWLXACTIVATEUSERSHELL)RTLdrGetFunction(hLdrMod, "WlxActivateUserShell");
    if (!GWlxActivateUserShell)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxActivateUserShell\n");
        return FALSE;
    }
    GWlxLoggedOnSAS =
        (PGWLXLOGGEDONSAS)RTLdrGetFunction(hLdrMod, "WlxLoggedOnSAS");
    if (!GWlxLoggedOnSAS)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxLoggedOnSAS\n");
        return FALSE;
    }
    GWlxDisplayLockedNotice =
        (PGWLXDISPLAYLOCKEDNOTICE)RTLdrGetFunction(hLdrMod, "WlxDisplayLockedNotice");
    if (!GWlxDisplayLockedNotice)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxDisplayLockedNotice\n");
        return FALSE;
    }
    GWlxIsLockOk = (PGWLXISLOCKOK)RTLdrGetFunction(hLdrMod, "WlxIsLockOk");
    if (!GWlxIsLockOk)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxIsLockOk\n");
        return FALSE;
    }
    GWlxWkstaLockedSAS =
        (PGWLXWKSTALOCKEDSAS)RTLdrGetFunction(hLdrMod, "WlxWkstaLockedSAS");
    if (!GWlxWkstaLockedSAS)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxWkstaLockedSAS\n");
        return FALSE;
    }
    GWlxIsLogoffOk = (PGWLXISLOGOFFOK)RTLdrGetFunction(hLdrMod, "WlxIsLogoffOk");
    if (!GWlxIsLogoffOk)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxIsLogoffOk\n");
        return FALSE;
    }
    GWlxLogoff = (PGWLXLOGOFF)RTLdrGetFunction(hLdrMod, "WlxLogoff");
    if (!GWlxLogoff)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxLogoff\n");
        return FALSE;
    }
    GWlxShutdown = (PGWLXSHUTDOWN)RTLdrGetFunction(hLdrMod, "WlxShutdown");
    if (!GWlxShutdown)
    {
        NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: failed resolving WlxShutdown\n");
        return FALSE;
    }
    /* GINA 1.1, optional */
    GWlxStartApplication = (PGWLXSTARTAPPLICATION)RTLdrGetFunction(hLdrMod, "WlxStartApplication");
    GWlxScreenSaverNotify = (PGWLXSCREENSAVERNOTIFY)RTLdrGetFunction(hLdrMod, "WlxScreenSaverNotify");
    /* GINA 1.3, optional */
    GWlxNetworkProviderLoad = (PGWLXNETWORKPROVIDERLOAD)RTLdrGetFunction(hLdrMod, "WlxNetworkProviderLoad");
    GWlxDisplayStatusMessage = (PGWLXDISPLAYSTATUSMESSAGE)RTLdrGetFunction(hLdrMod, "WlxDisplayStatusMessage");
    GWlxGetStatusMessage = (PGWLXGETSTATUSMESSAGE)RTLdrGetFunction(hLdrMod, "WlxGetStatusMessage");
    GWlxRemoveStatusMessage = (PGWLXREMOVESTATUSMESSAGE)RTLdrGetFunction(hLdrMod, "WlxRemoveStatusMessage");
    /* GINA 1.4, optional */
    GWlxGetConsoleSwitchCredentials =
        (PGWLXGETCONSOLESWITCHCREDENTIALS)RTLdrGetFunction(hLdrMod, "WlxGetConsoleSwitchCredentials");
    GWlxReconnectNotify = (PGWLXRECONNECTNOTIFY)RTLdrGetFunction(hLdrMod, "WlxReconnectNotify");
    GWlxDisconnectNotify = (PGWLXDISCONNECTNOTIFY)RTLdrGetFunction(hLdrMod, "WlxDisconnectNotify");
    NemuGINAVerbose(0, "NemuGINA::WlxNegotiate: optional function pointers:\n"
                    "  WlxStartApplication: %p\n"
                    "  WlxScreenSaverNotify: %p\n"
                    "  WlxNetworkProviderLoad: %p\n"
                    "  WlxDisplayStatusMessage: %p\n"
                    "  WlxGetStatusMessage: %p\n"
                    "  WlxRemoveStatusMessage: %p\n"
                    "  WlxGetConsoleSwitchCredentials: %p\n"
                    "  WlxReconnectNotify: %p\n"
                    "  WlxDisconnectNotify: %p\n",
                    GWlxStartApplication, GWlxScreenSaverNotify, GWlxNetworkProviderLoad,
                    GWlxDisplayStatusMessage, GWlxGetStatusMessage, GWlxRemoveStatusMessage,
                    GWlxGetConsoleSwitchCredentials, GWlxReconnectNotify, GWlxDisconnectNotify);

    wlxVersion = dwWinlogonVersion;

    /* Acknowledge interface version. */
    if (pdwDllVersion)
        *pdwDllVersion = dwWinlogonVersion;

    return TRUE; /* We're ready to rumble! */
}


BOOL WINAPI WlxInitialize(LPWSTR lpWinsta, HANDLE hWlx, PVOID pvReserved,
                          PVOID pWinlogonFunctions, PVOID *pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxInitialize\n");

    /* Store Winlogon function table */
    pWlxFuncs = (PWLX_DISPATCH_VERSION_1_1)pWinlogonFunctions;

    /* Store handle to Winlogon service*/
    hGinaWlx = hWlx;

    NemuGINAReportStatus(NemuGuestFacilityStatus_Init);

    /* Hook the dialogs */
    hookDialogBoxes(pWlxFuncs, wlxVersion);

    /* Forward call */
    if (GWlxInitialize)
        return GWlxInitialize(lpWinsta, hWlx, pvReserved, pWinlogonFunctions, pWlxContext);
    return TRUE;
}


VOID WINAPI WlxDisplaySASNotice(PVOID pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxDisplaySASNotice\n");

    /* Check if there are credentials for us, if so simulate C-A-D */
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_SUCCESS(rc))
    {
        NemuGINAVerbose(0, "NemuGINA::WlxDisplaySASNotice: simulating C-A-D\n");
        /* Wutomatic C-A-D */
        pWlxFuncs->WlxSasNotify(hGinaWlx, WLX_SAS_TYPE_CTRL_ALT_DEL);
    }
    else
    {
        NemuGINAVerbose(0, "NemuGINA::WlxDisplaySASNotice: starting credentials poller\n");
        /* start the credentials poller thread */
        NemuGINACredentialsPollerCreate();
        /* Forward call to MSGINA. */
        if (GWlxDisplaySASNotice)
            GWlxDisplaySASNotice(pWlxContext);
    }
}


int WINAPI WlxLoggedOutSAS(PVOID pWlxContext, DWORD dwSasType, PLUID pAuthenticationId,
                           PSID pLogonSid, PDWORD pdwOptions, PHANDLE phToken,
                           PWLX_MPR_NOTIFY_INFO pMprNotifyInfo, PVOID *pProfile)
{
    NemuGINAVerbose(0, "NemuGINA::WlxLoggedOutSAS\n");

    /* When performing a direct logon without C-A-D, our poller might not be running */
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_FAILURE(rc))
        NemuGINACredentialsPollerCreate();

    if (GWlxLoggedOutSAS)
    {
        int iRet;
        iRet = GWlxLoggedOutSAS(pWlxContext, dwSasType, pAuthenticationId, pLogonSid,
                                pdwOptions, phToken, pMprNotifyInfo, pProfile);

        if (iRet == WLX_SAS_ACTION_LOGON)
        {
            //
            // Copy pMprNotifyInfo and pLogonSid for later use
            //

            // pMprNotifyInfo->pszUserName
            // pMprNotifyInfo->pszDomain
            // pMprNotifyInfo->pszPassword
            // pMprNotifyInfo->pszOldPassword
        }

        return iRet;
    }

    return WLX_SAS_ACTION_NONE;
}


/**
 * WinLogon calls this function following a successful logon to request that the GINA activate the user's shell program.
 */
BOOL WINAPI WlxActivateUserShell(PVOID pWlxContext, PWSTR pszDesktopName,
                                 PWSTR pszMprLogonScript, PVOID pEnvironment)
{
    NemuGINAVerbose(0, "NemuGINA::WlxActivateUserShell\n");

    /*
     * Report status "terminated" to the host -- this means that a user
     * got logged in (either manually or automatically using the provided credentials).
     */
    NemuGINAReportStatus(NemuGuestFacilityStatus_Terminated);

    /* Forward call to MSGINA. */
    if (GWlxActivateUserShell)
        return GWlxActivateUserShell(pWlxContext, pszDesktopName, pszMprLogonScript, pEnvironment);
    return TRUE; /* Activate the user shell. */
}


int WINAPI WlxLoggedOnSAS(PVOID pWlxContext, DWORD dwSasType, PVOID pReserved)
{
    NemuGINAVerbose(0, "NemuGINA::WlxLoggedOnSAS: dwSasType=%ld\n", dwSasType);

    /*
     * We don't want to do anything special here since the OS should behave
     * as NemuGINA wouldn't have been installed. So pass all calls down
     * to the original MSGINA.
     */

    /* Forward call to MSGINA. */
    NemuGINAVerbose(0, "NemuGINA::WlxLoggedOnSAS: Forwarding call to MSGINA ...\n");
    if (GWlxLoggedOnSAS)
        return GWlxLoggedOnSAS(pWlxContext, dwSasType, pReserved);
    return WLX_SAS_ACTION_NONE;
}

VOID WINAPI WlxDisplayLockedNotice(PVOID pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxDisplayLockedNotice\n");

    /* Check if there are credentials for us, if so simulate C-A-D */
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_SUCCESS(rc))
    {
        NemuGINAVerbose(0, "NemuGINA::WlxDisplayLockedNotice: simulating C-A-D\n");
        /* Automatic C-A-D */
        pWlxFuncs->WlxSasNotify(hGinaWlx, WLX_SAS_TYPE_CTRL_ALT_DEL);
    }
    else
    {
        NemuGINAVerbose(0, "NemuGINA::WlxDisplayLockedNotice: starting credentials poller\n");
        /* start the credentials poller thread */
        NemuGINACredentialsPollerCreate();
        /* Forward call to MSGINA. */
        if (GWlxDisplayLockedNotice)
            GWlxDisplayLockedNotice(pWlxContext);
    }
}


/*
 * Winlogon calls this function before it attempts to lock the workstation.
 */
BOOL WINAPI WlxIsLockOk(PVOID pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxIsLockOk\n");

    /* Forward call to MSGINA. */
    if (GWlxIsLockOk)
        return GWlxIsLockOk(pWlxContext);
    return TRUE; /* Locking is OK. */
}


int WINAPI WlxWkstaLockedSAS(PVOID pWlxContext, DWORD dwSasType)
{
    NemuGINAVerbose(0, "NemuGINA::WlxWkstaLockedSAS, dwSasType=%ld\n", dwSasType);

    /* When performing a direct logon without C-A-D, our poller might not be running */
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_FAILURE(rc))
        NemuGINACredentialsPollerCreate();

    /* Forward call to MSGINA. */
    if (GWlxWkstaLockedSAS)
        return GWlxWkstaLockedSAS(pWlxContext, dwSasType);
    return WLX_SAS_ACTION_NONE;
}


BOOL WINAPI WlxIsLogoffOk(PVOID pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxIsLogoffOk\n");

    if (GWlxIsLogoffOk)
        return GWlxIsLogoffOk(pWlxContext);
    return TRUE; /* Log off is OK. */
}


/*
 * Winlogon calls this function to notify the GINA of a logoff operation on this
 * workstation. This allows the GINA to perform any logoff operations that may be required.
 */
VOID WINAPI WlxLogoff(PVOID pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxLogoff\n");

    /* No need to report the "active" status to the host here -- this will be done
     * when NemuGINA gets the chance to hook the dialogs (again). */

    /* Forward call to MSGINA. */
    if (GWlxLogoff)
        GWlxLogoff(pWlxContext);
}


/*
 * Winlogon calls this function just before shutting down.
 * This allows the GINA to perform any necessary shutdown tasks.
 * Will be called *after* WlxLogoff!
 */
VOID WINAPI WlxShutdown(PVOID pWlxContext, DWORD ShutdownType)
{
    NemuGINAVerbose(0, "NemuGINA::WlxShutdown\n");

    /*
     * Report status "inactive" to the host -- this means the
     * auto-logon feature won't be active anymore at this point
     * (until it maybe gets loaded again after a reboot).
     */
    NemuGINAReportStatus(NemuGuestFacilityStatus_Inactive);

    /* Forward call to MSGINA. */
    if (GWlxShutdown)
        GWlxShutdown(pWlxContext, ShutdownType);
}


/*
 * GINA 1.1 entry points
 */
BOOL WINAPI WlxScreenSaverNotify(PVOID pWlxContext, BOOL *pSecure)
{
    NemuGINAVerbose(0, "NemuGINA::WlxScreenSaverNotify, pSecure=%d\n",
                    pSecure ? *pSecure : 0);

    /* Report the status to "init" since the screensaver
     * (Winlogon) does not give NemuGINA yet the chance to hook into dialogs
     * which only then in turn would set the status to "active" -- so
     * at least set some status here. */
    NemuGINAReportStatus(NemuGuestFacilityStatus_Init);

    /* Note: Disabling the screensaver's grace period is necessary to get
     *       NemuGINA loaded and set the status to "terminated" again properly
     *       after the logging-in handling was done. To do this:
     *       - on a non-domain machine, set:
     *         HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\ScreenSaverGracePeriod (REG_SZ)
     *         to "0"
     *       - on a machine joined a domain:
     *         use the group policy preferences and/or the registry key above,
     *         depending on the domain's policies.
     */

    /* Indicate that the workstation should be locked. */
    *pSecure = TRUE;

    return TRUE; /* Screensaver should be activated. */
}


BOOL WINAPI WlxStartApplication(PVOID pWlxContext, PWSTR pszDesktopName,
                                PVOID pEnvironment, PWSTR pszCmdLine)
{
    NemuGINAVerbose(0, "NemuGINA::WlxStartApplication: pWlxCtx=%p, pszDesktopName=%ls, pEnvironment=%p, pszCmdLine=%ls\n",
                    pWlxContext, pszDesktopName, pEnvironment, pszCmdLine);

    /* Forward to MSGINA if present. */
    if (GWlxStartApplication)
        return GWlxStartApplication(pWlxContext, pszDesktopName, pEnvironment, pszCmdLine);
    return FALSE;
}


/*
 * GINA 1.3 entry points
 */
BOOL WINAPI WlxNetworkProviderLoad(PVOID pWlxContext, PWLX_MPR_NOTIFY_INFO pNprNotifyInfo)
{
    NemuGINAVerbose(0, "NemuGINA::WlxNetworkProviderLoad\n");

    /* Forward to MSGINA if present. */
    if (GWlxNetworkProviderLoad)
        return GWlxNetworkProviderLoad(pWlxContext, pNprNotifyInfo);
    return FALSE;
}


BOOL WINAPI WlxDisplayStatusMessage(PVOID pWlxContext, HDESK hDesktop, DWORD dwOptions,
                                    PWSTR pTitle, PWSTR pMessage)
{
    NemuGINAVerbose(0, "NemuGINA::WlxDisplayStatusMessage\n");

    /* Forward to MSGINA if present. */
    if (GWlxDisplayStatusMessage)
        return GWlxDisplayStatusMessage(pWlxContext, hDesktop, dwOptions, pTitle, pMessage);
    return FALSE;
}


BOOL WINAPI WlxGetStatusMessage(PVOID pWlxContext, DWORD *pdwOptions,
                                PWSTR pMessage, DWORD dwBufferSize)
{
    NemuGINAVerbose(0, "NemuGINA::WlxGetStatusMessage\n");

    /* Forward to MSGINA if present. */
    if (GWlxGetStatusMessage)
        return GWlxGetStatusMessage(pWlxContext, pdwOptions, pMessage, dwBufferSize);
    return FALSE;
}


BOOL WINAPI WlxRemoveStatusMessage(PVOID pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxRemoveStatusMessage\n");

    /* Forward to MSGINA if present. */
    if (GWlxRemoveStatusMessage)
        return GWlxRemoveStatusMessage(pWlxContext);
    return FALSE;
}


/*
 * GINA 1.4 entry points
 */
BOOL WINAPI WlxGetConsoleSwitchCredentials(PVOID pWlxContext,PVOID pCredInfo)
{
    NemuGINAVerbose(0, "NemuGINA::WlxGetConsoleSwitchCredentials\n");

    /* Forward call to MSGINA if present */
    if (GWlxGetConsoleSwitchCredentials)
        return GWlxGetConsoleSwitchCredentials(pWlxContext,pCredInfo);
    return FALSE;
}


VOID WINAPI WlxReconnectNotify(PVOID pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxReconnectNotify\n");

    /* Forward to MSGINA if present. */
    if (GWlxReconnectNotify)
        GWlxReconnectNotify(pWlxContext);
}


VOID WINAPI WlxDisconnectNotify(PVOID pWlxContext)
{
    NemuGINAVerbose(0, "NemuGINA::WlxDisconnectNotify\n");

    /* Forward to MSGINA if present. */
    if (GWlxDisconnectNotify)
        GWlxDisconnectNotify(pWlxContext);
}


/*
 * Windows Notification Package callbacks
 */
void WnpScreenSaverStop(PWLX_NOTIFICATION_INFO pInfo)
{
    NemuGINAVerbose(0, "NemuGINA::WnpScreenSaverStop\n");

    /*
     * Because we set the status to "init" in WlxScreenSaverNotify when
     * the screensaver becomes active we also have to take into account
     * that in case the screensaver terminates (either within the grace
     * period or because the lock screen appears) we have to set the
     * status accordingly.
     */
    NemuGINAReportStatus(NemuGuestFacilityStatus_Terminated);
}


DWORD WINAPI NemuGINADebug(void)
{
#ifdef DEBUG
    DWORD dwVersion;
    BOOL fRes = WlxNegotiate(WLX_VERSION_1_4, &dwVersion);
    if (!fRes)
        return 1;

    void* pWlxContext = NULL;
    WLX_DISPATCH_VERSION_1_4 wlxDispatch;
    ZeroMemory(&wlxDispatch, sizeof(WLX_DISPATCH_VERSION_1_4));

    fRes = WlxInitialize(0, 0,
                         NULL /* Reserved */,
                         NULL /* Winlogon functions */,
                         &pWlxContext);
    if (!fRes)
        return 2;

    WlxDisplaySASNotice(pWlxContext);

    char szSID[MAX_PATH];
    LUID luidAuth;
    DWORD dwOpts;
    WLX_MPR_NOTIFY_INFO wlxNotifyInfo;
    void* pvProfile;
    HANDLE hToken;
    int iRes = WlxLoggedOutSAS(pWlxContext, WLX_SAS_TYPE_CTRL_ALT_DEL,
                               &luidAuth, szSID,
                               &dwOpts, &hToken, &wlxNotifyInfo, &pvProfile);
    return iRes;
#else
    return 0;
#endif
}

