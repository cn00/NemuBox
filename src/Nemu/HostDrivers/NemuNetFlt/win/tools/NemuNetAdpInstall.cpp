/* $Id: NemuNetAdpInstall.cpp $ */
/** @file
 * NetAdpInstall - NemuNetAdp installer command line tool.
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
 */

#include <Nemu/NemuNetCfg-win.h>
#include <Nemu/NemuDrvCfg-win.h>
#include <stdio.h>
#include <devguid.h>

#define NEMU_NETADP_APP_NAME L"NetAdpInstall"

#define NEMU_NETADP_HWID L"sun_NemuNetAdp"
#ifdef NDIS60
#define NEMU_NETADP_INF L"NemuNetAdp6.inf"
#else /* !NDIS60 */
#define NEMU_NETADP_INF L"NemuNetAdp.inf"
#endif /* !NDIS60 */

static VOID winNetCfgLogger(LPCSTR szString)
{
    printf("%s\n", szString);
}

static int NemuNetAdpInstall(void)
{
    NemuNetCfgWinSetLogging(winNetCfgLogger);

    HRESULT hr = CoInitialize(NULL);
    if (SUCCEEDED(hr))
    {
        wprintf(L"adding host-only interface..\n");

        DWORD dwErr = ERROR_SUCCESS;
        WCHAR MpInf[MAX_PATH];

        if (!GetFullPathNameW(NEMU_NETADP_INF, sizeof(MpInf)/sizeof(MpInf[0]), MpInf, NULL))
            dwErr = GetLastError();

        if (dwErr == ERROR_SUCCESS)
        {
            INetCfg *pnc;
            LPWSTR lpszLockedBy = NULL;
            hr = NemuNetCfgWinQueryINetCfg(&pnc, TRUE, NEMU_NETADP_APP_NAME, 10000, &lpszLockedBy);
            if(hr == S_OK)
            {

                hr = NemuNetCfgWinNetAdpInstall(pnc, MpInf);

                if(hr == S_OK)
                {
                    wprintf(L"installed successfully\n");
                }
                else
                {
                    wprintf(L"error installing NemuNetAdp (0x%x)\n", hr);
                }

                NemuNetCfgWinReleaseINetCfg(pnc, TRUE);
            }
            else
                wprintf(L"NemuNetCfgWinQueryINetCfg failed: hr = 0x%x\n", hr);
            /*
            hr = NemuDrvCfgInfInstall(MpInf);
            if (FAILED(hr))
                printf("NemuDrvCfgInfInstall failed %#x\n", hr);

            GUID guid;
            BSTR name, errMsg;

            hr = NemuNetCfgWinCreateHostOnlyNetworkInterface (MpInf, true, &guid, &name, &errMsg);
            if (SUCCEEDED(hr))
            {
                ULONG ip, mask;
                hr = NemuNetCfgWinGenHostOnlyNetworkNetworkIp(&ip, &mask);
                if (SUCCEEDED(hr))
                {
                    // ip returned by NemuNetCfgWinGenHostOnlyNetworkNetworkIp is a network ip,
                    // i.e. 192.168.xxx.0, assign  192.168.xxx.1 for the hostonly adapter
                    ip = ip | (1 << 24);
                    hr = NemuNetCfgWinEnableStaticIpConfig(&guid, ip, mask);
                    if (SUCCEEDED(hr))
                    {
                        printf("installation successful\n");
                    }
                    else
                        printf("NemuNetCfgWinEnableStaticIpConfig failed: hr = 0x%x\n", hr);
                }
                else
                    printf("NemuNetCfgWinGenHostOnlyNetworkNetworkIp failed: hr = 0x%x\n", hr);
            }
            else
                printf("NemuNetCfgWinCreateHostOnlyNetworkInterface failed: hr = 0x%x\n", hr);
            */
        }
        else
        {
            wprintf(L"GetFullPathNameW failed: winEr = %d\n", dwErr);
            hr = HRESULT_FROM_WIN32(dwErr);

        }
        CoUninitialize();
    }
    else
        wprintf(L"Error initializing COM (0x%x)\n", hr);

    NemuNetCfgWinSetLogging(NULL);

    return SUCCEEDED(hr) ? 0 : 1;
}

static int NemuNetAdpUninstall(void)
{
    NemuNetCfgWinSetLogging(winNetCfgLogger);

    printf("uninstalling all host-only interfaces..\n");

    HRESULT hr = CoInitialize(NULL);
    if (SUCCEEDED(hr))
    {
        hr = NemuNetCfgWinRemoveAllNetDevicesOfId(NEMU_NETADP_HWID);
        if (SUCCEEDED(hr))
        {
            hr = NemuDrvCfgInfUninstallAllSetupDi(&GUID_DEVCLASS_NET, L"Net", NEMU_NETADP_HWID, 0/* could be SUOI_FORCEDELETE */);
            if (SUCCEEDED(hr))
            {
                printf("uninstallation successful\n");
            }
            else
                printf("uninstalled successfully, but failed to remove infs\n");
        }
        else
            printf("uninstall failed, hr = 0x%x\n", hr);
        CoUninitialize();
    }
    else
        printf("Error initializing COM (0x%x)\n", hr);

    NemuNetCfgWinSetLogging(NULL);

    return SUCCEEDED(hr) ? 0 : 1;
}

static int NemuNetAdpUpdate(void)
{
    NemuNetCfgWinSetLogging(winNetCfgLogger);

    printf("uninstalling all host-only interfaces..\n");

    HRESULT hr = CoInitialize(NULL);
    if (SUCCEEDED(hr))
    {
        BOOL fRebootRequired = FALSE;
        /*
         * Before we can update the driver for existing adapters we need to remove
         * all old driver packages from the driver cache. Otherwise we may end up
         * with both NDIS5 and NDIS6 versions of NemuNetAdp in the cache which
         * will cause all sorts of trouble.
         */
        NemuDrvCfgInfUninstallAllF(L"Net", NEMU_NETADP_HWID, SUOI_FORCEDELETE);
        hr = NemuNetCfgWinUpdateHostOnlyNetworkInterface(NEMU_NETADP_INF, &fRebootRequired, NEMU_NETADP_HWID);
        if (SUCCEEDED(hr))
        {
            if (fRebootRequired)
                printf("!!REBOOT REQUIRED!!\n");
            printf("updated successfully\n");
        }
        else
            printf("update failed, hr = 0x%x\n", hr);

        CoUninitialize();
    }
    else
        printf("Error initializing COM (0x%x)\n", hr);

    NemuNetCfgWinSetLogging(NULL);

    return SUCCEEDED(hr) ? 0 : 1;
}

static int NemuNetAdpDisable(void)
{
    NemuNetCfgWinSetLogging(winNetCfgLogger);

    printf("disabling all host-only interfaces..\n");

    HRESULT hr = CoInitialize(NULL);
    if (SUCCEEDED(hr))
    {
        hr = NemuNetCfgWinPropChangeAllNetDevicesOfId(NEMU_NETADP_HWID, NEMUNECTFGWINPROPCHANGE_TYPE_DISABLE);
        if (SUCCEEDED(hr))
        {
            printf("disabling successful\n");
        }
        else
            printf("disable failed, hr = 0x%x\n", hr);

        CoUninitialize();
    }
    else
        printf("Error initializing COM (0x%x)\n", hr);

    NemuNetCfgWinSetLogging(NULL);

    return SUCCEEDED(hr) ? 0 : 1;
}

static int NemuNetAdpEnable(void)
{
    NemuNetCfgWinSetLogging(winNetCfgLogger);

    printf("enabling all host-only interfaces..\n");

    HRESULT hr = CoInitialize(NULL);
    if (SUCCEEDED(hr))
    {
        hr = NemuNetCfgWinPropChangeAllNetDevicesOfId(NEMU_NETADP_HWID, NEMUNECTFGWINPROPCHANGE_TYPE_ENABLE);
        if (SUCCEEDED(hr))
        {
            printf("enabling successful\n");
        }
        else
            printf("enabling failed, hr = 0x%x\n", hr);

        CoUninitialize();
    }
    else
        printf("Error initializing COM (0x%x)\n", hr);

    NemuNetCfgWinSetLogging(NULL);

    return SUCCEEDED(hr) ? 0 : 1;
}

static void printUsage(void)
{
    printf("host-only network adapter configuration tool\n"
            "  Usage: NemuNetAdpInstall [cmd]\n"
            "    cmd can be one of the following values:\n"
            "       i  - install a new host-only interface (default command)\n"
            "       u  - uninstall all host-only interfaces\n"
            "       a  - update the host-only driver\n"
            "       d  - disable all host-only interfaces\n"
            "       e  - enable all host-only interfaces\n"
            "       h  - print this message\n");
}

int __cdecl main(int argc, char **argv)
{
    if (argc < 2)
        return NemuNetAdpInstall();
    if (argc > 2)
    {
        printUsage();
        return 1;
    }

    if (!strcmp(argv[1], "i"))
        return NemuNetAdpInstall();
    if (!strcmp(argv[1], "u"))
        return NemuNetAdpUninstall();
    if (!strcmp(argv[1], "a"))
        return NemuNetAdpUpdate();
    if (!strcmp(argv[1], "d"))
        return NemuNetAdpDisable();
    if (!strcmp(argv[1], "e"))
        return NemuNetAdpEnable();

    printUsage();
    return !strcmp(argv[1], "h");
}
