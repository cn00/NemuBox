/* $Id: NemuVFS.cpp $ */
/** @file
 * NemuVFS - Guest Additions Shared Folders driver. KEXT entry point.
 */

/*
 * Copyright (C) 2013-2015 Oracle Corporation
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

#include <IOKit/IOLib.h> /* Assert as function */
#include <IOKit/IOService.h>
#include <mach/mach_port.h>


#include <mach/kmod.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <sys/mount.h>

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <sys/param.h>
#include <Nemu/version.h>
#include <iprt/asm.h>

#include <Nemu/log.h>

#include "nemuvfs.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * The service class for dealing with Share Folder filesystem.
 */
class org_virtualbox_NemuVFS : public IOService
{
    OSDeclareDefaultStructors(org_virtualbox_NemuVFS);

private:
    IOService * waitForCoreService(void);

    IOService * coreService;

public:
    virtual bool start(IOService *pProvider);
    virtual void stop(IOService *pProvider);
};

OSDefineMetaClassAndStructors(org_virtualbox_NemuVFS, IOService);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Declare the module stuff.
 */
RT_C_DECLS_BEGIN
static kern_return_t NemuVFSModuleLoad(struct kmod_info *pKModInfo, void *pvData);
static kern_return_t NemuVFSModuleUnLoad(struct kmod_info *pKModInfo, void *pvData);
extern kern_return_t _start(struct kmod_info *pKModInfo, void *pvData);
extern kern_return_t _stop(struct kmod_info *pKModInfo, void *pvData);
KMOD_EXPLICIT_DECL(NemuVFS, NEMU_VERSION_STRING, _start, _stop)
DECLHIDDEN(kmod_start_func_t *) _realmain      = NemuVFSModuleLoad;
DECLHIDDEN(kmod_stop_func_t *)  _antimain      = NemuVFSModuleUnLoad;
DECLHIDDEN(int)                 _kext_apple_cc = __APPLE_CC__;
RT_C_DECLS_END

/** The number of IOService class instances. */
static bool volatile        g_fInstantiated = 0;
/* Global connection to the host service */
VBGLSFCLIENT                  g_nemuSFClient;
/* NemuVFS filesystem handle. Needed for FS unregistering. */
static vfstable_t           g_oNemuVFSHandle;


/**
 * KEXT Module BSD entry point
 */
static kern_return_t NemuVFSModuleLoad(struct kmod_info *pKModInfo, void *pvData)
{
    int rc;

    /* Initialize the R0 guest library. */
#if 0
    rc = VbglR0SfInit();
    if (RT_FAILURE(rc))
        return KERN_FAILURE;
#endif

    PINFO("VirtualBox " NEMU_VERSION_STRING " shared folders "
          "driver is loaded");

    return KERN_SUCCESS;
}


/**
 * KEXT Module BSD exit point
 */
static kern_return_t NemuVFSModuleUnLoad(struct kmod_info *pKModInfo, void *pvData)
{
    int rc;

#if 0
   VbglR0SfTerminate();
#endif

    PINFO("VirtualBox " NEMU_VERSION_STRING " shared folders driver is unloaded");

    return KERN_SUCCESS;
}


/**
 * Register NemuFS filesystem.
 *
 * @returns IPRT status code.
 */
int NemuVFSRegisterFilesystem(void)
{
    struct vfs_fsentry oVFsEntry;
    int rc;

    memset(&oVFsEntry, 0, sizeof(oVFsEntry));
    /* Attach filesystem operations set */
    oVFsEntry.vfe_vfsops = &g_oNemuVFSOpts;
    /* Attach vnode operations */
    oVFsEntry.vfe_vopcnt = g_cNemuVFSVnodeOpvDescListSize;
    oVFsEntry.vfe_opvdescs = g_NemuVFSVnodeOpvDescList;
    /* Set flags */
    oVFsEntry.vfe_flags =
#if ARCH_BITS == 64
            VFS_TBL64BITREADY |
#endif
            VFS_TBLTHREADSAFE |
            VFS_TBLFSNODELOCK |
            VFS_TBLNOTYPENUM;

    memcpy(oVFsEntry.vfe_fsname, NEMUVBFS_NAME, MFSNAMELEN);

    rc = vfs_fsadd(&oVFsEntry, &g_oNemuVFSHandle);
    if (rc)
    {
        PINFO("Unable to register NemuVFS filesystem (%d)", rc);
        return VERR_GENERAL_FAILURE;
    }

    PINFO("NemuVFS filesystem successfully registered");
    return VINF_SUCCESS;
}

/**
 * Unregister NemuFS filesystem.
 *
 * @returns IPRT status code.
 */
int NemuVFSUnRegisterFilesystem(void)
{
    int rc;

    if (g_oNemuVFSHandle == 0)
        return VERR_INVALID_PARAMETER;

    rc = vfs_fsremove(g_oNemuVFSHandle);
    if (rc)
    {
        PINFO("Unable to unregister NemuVFS filesystem (%d)", rc);
        return VERR_GENERAL_FAILURE;
    }

    g_oNemuVFSHandle = 0;

    PINFO("NemuVFS filesystem successfully unregistered");
    return VINF_SUCCESS;
}


/**
 * Start this service.
 */
bool org_virtualbox_NemuVFS::start(IOService *pProvider)
{
    int rc;

    if (!IOService::start(pProvider))
        return false;

    /* Low level initialization should be performed only once */
    if (!ASMAtomicCmpXchgBool(&g_fInstantiated, true, false))
    {
        IOService::stop(pProvider);
        return false;
    }

    /* Wait for NemuGuest to be started */
    coreService = waitForCoreService();
    if (coreService)
    {
        rc = VbglR0SfInit();
        if (RT_SUCCESS(rc))
        {
            /* Connect to the host service. */
            rc = VbglR0SfConnect(&g_nemuSFClient);
            if (RT_SUCCESS(rc))
            {
                PINFO("Nemu client connected");
                rc = VbglR0SfSetUtf8(&g_nemuSFClient);
                if (RT_SUCCESS(rc))
                {
                    rc = NemuVFSRegisterFilesystem();
                    if (RT_SUCCESS(rc))
                    {
                        registerService();
                        PINFO("Successfully started I/O kit class instance");
                        return true;
                    }
                    PERROR("Unable to register NemuVFS filesystem");
                }
                else
                {
                    PERROR("VbglR0SfSetUtf8 failed: rc=%d", rc);
                }
                VbglR0SfDisconnect(&g_nemuSFClient);
            }
            else
            {
                PERROR("Failed to get connection to host: rc=%d", rc);
            }
            VbglR0SfUninit();
        }
        else
        {
            PERROR("Failed to initialize low level library");
        }
        coreService->release();
    }
    else
    {
        PERROR("NemuGuest KEXT not started");
    }

    ASMAtomicXchgBool(&g_fInstantiated, false);
    IOService::stop(pProvider);

    return false;
}


/**
 * Stop this service.
 */
void org_virtualbox_NemuVFS::stop(IOService *pProvider)
{
    int rc;

    AssertReturnVoid(ASMAtomicReadBool(&g_fInstantiated));

    rc = NemuVFSUnRegisterFilesystem();
    if (RT_FAILURE(rc))
    {
        PERROR("NemuVFS filesystem is busy. Make sure all "
               "shares are unmounted (%d)", rc);
    }

    VbglR0SfDisconnect(&g_nemuSFClient);
    PINFO("Nemu client disconnected");

    VbglR0SfTerminate();
    PINFO("Low level uninit done");

    coreService->release();
    PINFO("NemuGuest service released");

    IOService::stop(pProvider);

    ASMAtomicWriteBool(&g_fInstantiated, false);

    PINFO("Successfully stopped I/O kit class instance");
}


/**
 * Wait for NemuGuest.kext to be started
 */
IOService * org_virtualbox_NemuVFS::waitForCoreService(void)
{
    IOService *service;

    OSDictionary *serviceToMatch = serviceMatching("org_virtualbox_NemuGuest");
    if (!serviceToMatch)
    {
        PINFO("unable to create matching dictionary");
        return false;
    }

    /* Wait 10 seconds for NemuGuest to be started */
    service = waitForMatchingService(serviceToMatch, 10ULL * 1000000000ULL);
    serviceToMatch->release();

    return service;
}
