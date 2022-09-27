/** @file
 *
 * Nemu host drivers - USB drivers - Filter & driver installation
 *
 * Installation code
 *
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <Nemu/err.h>
#include <stdio.h>

#include <Nemu/NemuDrvCfg-win.h>

static DECLCALLBACK(void) nemuUsbLog(NEMUDRVCFG_LOG_SEVERITY enmSeverity, char * msg, void * pvContext)
{
    switch (enmSeverity)
    {
        case NEMUDRVCFG_LOG_SEVERITY_FLOW:
        case NEMUDRVCFG_LOG_SEVERITY_REGULAR:
            break;
        case NEMUDRVCFG_LOG_SEVERITY_REL:
            RTPrintf("%s", msg);
            break;
        default:
            break;
    }
}

static DECLCALLBACK(void) nemuUsbPanic(void * pvPanic)
{
#ifndef DEBUG_bird
    AssertFailed();
#endif
}

int usblibOsCreateService(void);

int __cdecl main(int argc, char **argv)
{
    if (RTR3InitExe(argc, &argv, 0) != VINF_SUCCESS)
    {
        printf("Could not init IPRT!\n");
        return 1;
    }

    NemuDrvCfgLoggerSet(nemuUsbLog, NULL);
    NemuDrvCfgPanicSet(nemuUsbPanic, NULL);

    RTPrintf("USB installation\n");

    int rc = usblibOsCreateService();

    if (RT_SUCCESS(rc))
    {
        LPWSTR  lpszFilePart;
        WCHAR  szFullPath[MAX_PATH];
        DWORD  len;

        len = GetFullPathNameW(L".\\NemuUSB.inf", RT_ELEMENTS(szFullPath), szFullPath, &lpszFilePart);
        Assert(len);

        HRESULT hr = NemuDrvCfgInfInstall(szFullPath);
        if (hr == S_OK)
        {
            RTPrintf("Installation successful.\n");
        }
        else
        {
            rc = -1;
        }
    }

    if (RT_SUCCESS(rc))
        rc = 0;

    /** @todo RTR3Term(); */
    return rc;
}

/** The support service name. */
#define SERVICE_NAME    "NemuUSBMon"
/** Win32 Device name. */
#define DEVICE_NAME     "\\\\.\\NemuUSBMon"
/** NT Device name. */
#define DEVICE_NAME_NT   L"\\Device\\NemuUSBMon"
/** Win32 Symlink name. */
#define DEVICE_NAME_DOS  L"\\DosDevices\\NemuUSBMon"


/**
 * Changes the USB driver service to specified driver path.
 *
 * @returns 0 on success.
 * @returns < 0 on failure.
 */
int usblibOsChangeService(const char *pszDriverPath)
{
    SC_HANDLE hSMgrCreate = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG);
    DWORD dwLastError = GetLastError();
    int rc = RTErrConvertFromWin32(dwLastError);
    AssertPtr(pszDriverPath);
    AssertMsg(hSMgrCreate, ("OpenSCManager(,,create) failed rc=%d\n", dwLastError));
    if (hSMgrCreate)
    {
        SC_HANDLE hService = OpenService(hSMgrCreate,
                                         SERVICE_NAME,
                                         GENERIC_ALL);
        DWORD dwLastError = GetLastError();
        if (hService == NULL)
        {
            AssertMsg(hService, ("OpenService failed! LastError=%Rwa, pszDriver=%s\n", dwLastError, pszDriverPath));
            rc = RTErrConvertFromWin32(dwLastError);
        }
        else
        {
            /* We only gonna change the driver image path, the rest remains like it already is */
            if (ChangeServiceConfig(hService,
                                    SERVICE_NO_CHANGE,
                                    SERVICE_NO_CHANGE,
                                    SERVICE_NO_CHANGE,
                                    pszDriverPath,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL))
            {
                RTPrintf("Changed service config to new driver path: %s\n", pszDriverPath);
            }
            else
            {
                AssertMsg(hService, ("ChangeServiceConfig failed! LastError=%Rwa, pszDriver=%s\n", dwLastError, pszDriverPath));
                rc = RTErrConvertFromWin32(dwLastError);
            }
            if (hService != NULL)
                CloseServiceHandle(hService);
        }

        CloseServiceHandle(hSMgrCreate);
    }
    return rc;
}


/**
 * Creates the service.
 *
 * @returns 0 on success.
 * @returns < 0 on failure.
 */
int usblibOsCreateService(void)
{
    /*
     * Assume it didn't exist, so we'll create the service.
     */
    SC_HANDLE hSMgrCreate = OpenSCManager(NULL, NULL, SERVICE_CHANGE_CONFIG);
    DWORD dwLastError = GetLastError();
    int rc = RTErrConvertFromWin32(dwLastError);
    AssertMsg(hSMgrCreate, ("OpenSCManager(,,create) failed rc=%d\n", dwLastError));
    if (hSMgrCreate)
    {
        char szDriver[RTPATH_MAX];
        int rc = RTPathExecDir(szDriver, sizeof(szDriver) - sizeof("\\NemuUSBMon.sys"));
        if (RT_SUCCESS(rc))
        {
            strcat(szDriver, "\\NemuUSBMon.sys");
            RTPrintf("Creating USB monitor driver service with path %s ...\n", szDriver);
            SC_HANDLE hService = CreateService(hSMgrCreate,
                                               SERVICE_NAME,
                                               "Nemu USB Monitor Driver",
                                               SERVICE_QUERY_STATUS,
                                               SERVICE_KERNEL_DRIVER,
                                               SERVICE_DEMAND_START,
                                               SERVICE_ERROR_NORMAL,
                                               szDriver,
                                               NULL, NULL, NULL, NULL, NULL);
            DWORD dwLastError = GetLastError();
            if (dwLastError == ERROR_SERVICE_EXISTS)
            {
                RTPrintf("USB monitor driver service already exists, skipping creation.\n");
                rc = usblibOsChangeService(szDriver);
            }
            else
            {
                AssertMsg(hService, ("CreateService failed! LastError=%Rwa, szDriver=%s\n", dwLastError, szDriver));
                rc = RTErrConvertFromWin32(dwLastError);
                if (hService != NULL)
                    CloseServiceHandle(hService);
            }
        }
        CloseServiceHandle(hSMgrCreate);
    }
    return rc;
}
