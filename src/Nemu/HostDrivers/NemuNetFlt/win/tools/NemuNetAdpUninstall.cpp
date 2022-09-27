/* $Id: NemuNetAdpUninstall.cpp $ */
/** @file
 * NetAdpUninstall - NemuNetAdp uninstaller command line tool
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

#ifdef NDIS60
#define NEMU_NETADP_HWID L"sun_NemuNetAdp6"
#else /* !NDIS60 */
#define NEMU_NETADP_HWID L"sun_NemuNetAdp"
#endif /* !NDIS60 */

static VOID winNetCfgLogger (LPCSTR szString)
{
    printf("%s", szString);
}

static int NemuNetAdpUninstall()
{
    int r = 1;
    NemuNetCfgWinSetLogging(winNetCfgLogger);

    printf("uninstalling all Host-Only interfaces..\n");

    HRESULT hr = CoInitialize(NULL);
    if(hr == S_OK)
    {
        hr = NemuNetCfgWinRemoveAllNetDevicesOfId(NEMU_NETADP_HWID);
        if(hr == S_OK)
        {
            hr = NemuDrvCfgInfUninstallAllSetupDi(&GUID_DEVCLASS_NET, L"Net", NEMU_NETADP_HWID, 0/* could be SUOI_FORCEDELETE */);
            if(hr == S_OK)
            {
                printf("uninstalled successfully\n");
            }
            else
            {
                printf("uninstalled successfully, but failed to remove infs\n");
            }
            r = 0;
        }
        else
        {
            printf("uninstall failed, hr = 0x%x\n", hr);
        }

        CoUninitialize();
    }
    else
    {
        wprintf(L"Error initializing COM (0x%x)\n", hr);
    }

    NemuNetCfgWinSetLogging(NULL);

    return r;
}

int __cdecl main(int argc, char **argv)
{
    return NemuNetAdpUninstall();
}
