/* $Id: tstNemuAPI.cpp $ */
/** @file
 * tstNemuAPI - Checks VirtualBox API.
 */

/*
 * Copyright (C) 2006-2014 Oracle Corporation
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
#include <Nemu/com/com.h>
#include <Nemu/com/string.h>
#include <Nemu/com/array.h>
#include <Nemu/com/Guid.h>
#include <Nemu/com/ErrorInfo.h>
#include <Nemu/com/errorprint.h>
#include <Nemu/com/VirtualBox.h>
#include <Nemu/sup.h>

#include <iprt/test.h>
#include <iprt/time.h>

using namespace com;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;
static Bstr   tstMachineName = "tstNemuAPI test VM";


/** Worker for TST_COM_EXPR(). */
static HRESULT tstComExpr(HRESULT hrc, const char *pszOperation, int iLine)
{
    if (FAILED(hrc))
        RTTestFailed(g_hTest, "%s failed on line %u with hrc=%Rhrc", pszOperation, iLine, hrc);
    return hrc;
}

/** Macro that executes the given expression and report any failure.
 *  The expression must return a HRESULT. */
#define TST_COM_EXPR(expr) tstComExpr(expr, #expr, __LINE__)


static BOOL tstApiIVirtualBox(IVirtualBox *pNemu)
{
    HRESULT rc;
    Bstr bstrTmp;
    ULONG ulTmp;

    RTTestSub(g_hTest, "IVirtualBox::version");
    CHECK_ERROR(pNemu, COMGETTER(Version)(bstrTmp.asOutParam()));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::version");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::version failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::versionNormalized");
    CHECK_ERROR(pNemu, COMGETTER(VersionNormalized)(bstrTmp.asOutParam()));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::versionNormalized");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::versionNormalized failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::revision");
    CHECK_ERROR(pNemu, COMGETTER(Revision)(&ulTmp));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::revision");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::revision failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::packageType");
    CHECK_ERROR(pNemu, COMGETTER(PackageType)(bstrTmp.asOutParam()));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::packageType");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::packageType failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::APIVersion");
    CHECK_ERROR(pNemu, COMGETTER(APIVersion)(bstrTmp.asOutParam()));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::APIVersion");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::APIVersion failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::homeFolder");
    CHECK_ERROR(pNemu, COMGETTER(HomeFolder)(bstrTmp.asOutParam()));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::homeFolder");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::homeFolder failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::settingsFilePath");
    CHECK_ERROR(pNemu, COMGETTER(SettingsFilePath)(bstrTmp.asOutParam()));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::settingsFilePath");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::settingsFilePath failed", __LINE__);

    com::SafeIfaceArray<IGuestOSType> guestOSTypes;
    RTTestSub(g_hTest, "IVirtualBox::guestOSTypes");
    CHECK_ERROR(pNemu, COMGETTER(GuestOSTypes)(ComSafeArrayAsOutParam(guestOSTypes)));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::guestOSTypes");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::guestOSTypes failed", __LINE__);

    /** Create VM */
    RTTestSub(g_hTest, "IVirtualBox::CreateMachine");
    ComPtr<IMachine> ptrMachine;
    com::SafeArray<BSTR> groups;
    /** Default VM settings */
    CHECK_ERROR(pNemu, CreateMachine(NULL,                          /** Settings */
                                     tstMachineName.raw(),          /** Name */
                                     ComSafeArrayAsInParam(groups), /** Groups */
                                     NULL,                          /** OS Type */
                                     NULL,                          /** Create flags */
                                     ptrMachine.asOutParam()));     /** Machine */
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::CreateMachine");
    else
    {
        RTTestFailed(g_hTest, "%d: IVirtualBox::CreateMachine failed", __LINE__);
        return FALSE;
    }

    RTTestSub(g_hTest, "IVirtualBox::RegisterMachine");
    CHECK_ERROR(pNemu, RegisterMachine(ptrMachine));
    if (SUCCEEDED(rc))
        RTTestPassed(g_hTest, "IVirtualBox::RegisterMachine");
    else
    {
        RTTestFailed(g_hTest, "%d: IVirtualBox::RegisterMachine failed", __LINE__);
        return FALSE;
    }

    ComPtr<IHost> host;
    RTTestSub(g_hTest, "IVirtualBox::host");
    CHECK_ERROR(pNemu, COMGETTER(Host)(host.asOutParam()));
    if (SUCCEEDED(rc))
    {
        /** @todo Add IHost testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::host");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::host failed", __LINE__);

    ComPtr<ISystemProperties> sysprop;
    RTTestSub(g_hTest, "IVirtualBox::systemProperties");
    CHECK_ERROR(pNemu, COMGETTER(SystemProperties)(sysprop.asOutParam()));
    if (SUCCEEDED(rc))
    {
        /** @todo Add ISystemProperties testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::systemProperties");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::systemProperties failed", __LINE__);

    com::SafeIfaceArray<IMachine> machines;
    RTTestSub(g_hTest, "IVirtualBox::machines");
    CHECK_ERROR(pNemu, COMGETTER(Machines)(ComSafeArrayAsOutParam(machines)));
    if (SUCCEEDED(rc))
    {
        bool bFound = FALSE;
        for (size_t i = 0; i < machines.size(); ++i)
        {
            if (machines[i])
            {
                Bstr tmpName;
                rc = machines[i]->COMGETTER(Name)(tmpName.asOutParam());
                if (SUCCEEDED(rc))
                {
                    if (tmpName == tstMachineName)
                    {
                        bFound = TRUE;
                        break;
                    }
                }
            }
        }

        if (bFound)
            RTTestPassed(g_hTest, "IVirtualBox::machines");
        else
            RTTestFailed(g_hTest, "%d: IVirtualBox::machines failed. No created machine found", __LINE__);
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::machines failed", __LINE__);

#if 0 /** Not yet implemented */
    com::SafeIfaceArray<ISharedFolder> sharedFolders;
    RTTestSub(g_hTest, "IVirtualBox::sharedFolders");
    CHECK_ERROR(pNemu, COMGETTER(SharedFolders)(ComSafeArrayAsOutParam(sharedFolders)));
    if (SUCCEEDED(rc))
    {
        /** @todo Add ISharedFolders testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::sharedFolders");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::sharedFolders failed", __LINE__);
#endif

    com::SafeIfaceArray<IMedium> hardDisks;
    RTTestSub(g_hTest, "IVirtualBox::hardDisks");
    CHECK_ERROR(pNemu, COMGETTER(HardDisks)(ComSafeArrayAsOutParam(hardDisks)));
    if (SUCCEEDED(rc))
    {
        /** @todo Add hardDisks testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::hardDisks");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::hardDisks failed", __LINE__);

    com::SafeIfaceArray<IMedium> DVDImages;
    RTTestSub(g_hTest, "IVirtualBox::DVDImages");
    CHECK_ERROR(pNemu, COMGETTER(DVDImages)(ComSafeArrayAsOutParam(DVDImages)));
    if (SUCCEEDED(rc))
    {
        /** @todo Add DVDImages testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::DVDImages");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::DVDImages failed", __LINE__);

    com::SafeIfaceArray<IMedium> floppyImages;
    RTTestSub(g_hTest, "IVirtualBox::floppyImages");
    CHECK_ERROR(pNemu, COMGETTER(FloppyImages)(ComSafeArrayAsOutParam(floppyImages)));
    if (SUCCEEDED(rc))
    {
        /** @todo Add floppyImages testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::floppyImages");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::floppyImages failed", __LINE__);

    com::SafeIfaceArray<IProgress> progressOperations;
    RTTestSub(g_hTest, "IVirtualBox::progressOperations");
    CHECK_ERROR(pNemu, COMGETTER(ProgressOperations)(ComSafeArrayAsOutParam(progressOperations)));
    if (SUCCEEDED(rc))
    {
        /** @todo Add IProgress testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::progressOperations");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::progressOperations failed", __LINE__);

    ComPtr<IPerformanceCollector> performanceCollector;
    RTTestSub(g_hTest, "IVirtualBox::performanceCollector");
    CHECK_ERROR(pNemu, COMGETTER(PerformanceCollector)(performanceCollector.asOutParam()));
    if (SUCCEEDED(rc))
    {
        /** @todo Add IPerformanceCollector testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::performanceCollector");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::performanceCollector failed", __LINE__);

    com::SafeIfaceArray<IDHCPServer> DHCPServers;
    RTTestSub(g_hTest, "IVirtualBox::DHCPServers");
    CHECK_ERROR(pNemu, COMGETTER(DHCPServers)(ComSafeArrayAsOutParam(DHCPServers)));
    if (SUCCEEDED(rc))
    {
        /** @todo Add IDHCPServers testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::DHCPServers");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::DHCPServers failed", __LINE__);

    com::SafeIfaceArray<INATNetwork> NATNetworks;
    RTTestSub(g_hTest, "IVirtualBox::NATNetworks");
    CHECK_ERROR(pNemu, COMGETTER(NATNetworks)(ComSafeArrayAsOutParam(NATNetworks)));
    if (SUCCEEDED(rc))
    {
        /** @todo Add INATNetworks testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::NATNetworks");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::NATNetworks failed", __LINE__);

    ComPtr<IEventSource> eventSource;
    RTTestSub(g_hTest, "IVirtualBox::eventSource");
    CHECK_ERROR(pNemu, COMGETTER(EventSource)(eventSource.asOutParam()));
    if (SUCCEEDED(rc))
    {
        /** @todo Add IEventSource testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::eventSource");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::eventSource failed", __LINE__);

    ComPtr<IExtPackManager> extensionPackManager;
    RTTestSub(g_hTest, "IVirtualBox::extensionPackManager");
    CHECK_ERROR(pNemu, COMGETTER(ExtensionPackManager)(extensionPackManager.asOutParam()));
    if (SUCCEEDED(rc))
    {
        /** @todo Add IExtPackManager testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::extensionPackManager");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::extensionPackManager failed", __LINE__);

    com::SafeArray<BSTR> internalNetworks;
    RTTestSub(g_hTest, "IVirtualBox::internalNetworks");
    CHECK_ERROR(pNemu, COMGETTER(InternalNetworks)(ComSafeArrayAsOutParam(internalNetworks)));
    if (SUCCEEDED(rc))
    {
        RTTestPassed(g_hTest, "IVirtualBox::internalNetworks");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::internalNetworks failed", __LINE__);

    com::SafeArray<BSTR> genericNetworkDrivers;
    RTTestSub(g_hTest, "IVirtualBox::genericNetworkDrivers");
    CHECK_ERROR(pNemu, COMGETTER(GenericNetworkDrivers)(ComSafeArrayAsOutParam(genericNetworkDrivers)));
    if (SUCCEEDED(rc))
    {
        RTTestPassed(g_hTest, "IVirtualBox::genericNetworkDrivers");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::genericNetworkDrivers failed", __LINE__);

    return TRUE;
}


static BOOL tstApiClean(IVirtualBox *pNemu)
{
    HRESULT rc;

    /** Delete created VM and its files */
    ComPtr<IMachine> machine;
    CHECK_ERROR_RET(pNemu, FindMachine(Bstr(tstMachineName).raw(), machine.asOutParam()), FALSE);
    SafeIfaceArray<IMedium> media;
    CHECK_ERROR_RET(machine, Unregister(CleanupMode_DetachAllReturnHardDisksOnly,
                                    ComSafeArrayAsOutParam(media)), FALSE);
    ComPtr<IProgress> progress;
    CHECK_ERROR_RET(machine, DeleteConfig(ComSafeArrayAsInParam(media), progress.asOutParam()), FALSE);
    CHECK_ERROR_RET(progress, WaitForCompletion(-1), FALSE);

    return TRUE;
}


int main(int argc, char **argv)
{
    /*
     * Initialization.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstNemuAPI", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    SUPR3Init(NULL); /* Better time support. */
    RTTestBanner(g_hTest);

    RTTestSub(g_hTest, "Initializing COM and singletons");
    HRESULT hrc = com::Initialize();
    if (SUCCEEDED(hrc))
    {
        ComPtr<IVirtualBox> ptrNemu;
        hrc = TST_COM_EXPR(ptrNemu.createLocalObject(CLSID_VirtualBox));
        if (SUCCEEDED(hrc))
        {
            ComPtr<ISession> ptrSession;
            hrc = TST_COM_EXPR(ptrSession.createInprocObject(CLSID_Session));
            if (SUCCEEDED(hrc))
            {
                RTTestSubDone(g_hTest);

                /*
                 * Call test functions.
                 */

                /** Test IVirtualBox interface */
                tstApiIVirtualBox(ptrNemu);


                /** Clean files/configs */
                tstApiClean(ptrNemu);
            }
        }

        ptrNemu.setNull();
        com::Shutdown();
    }
    else
        RTTestIFailed("com::Initialize failed with hrc=%Rhrc", hrc);
    return RTTestSummaryAndDestroy(g_hTest);
}
