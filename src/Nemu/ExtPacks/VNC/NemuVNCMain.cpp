/* $Id: NemuVNCMain.cpp $ */
/** @file
 * VNC main module.
 */

/*
 * Copyright (C) 2010-2015 Oracle Corporation
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
#include <Nemu/ExtPack/ExtPack.h>

#include <Nemu/err.h>
#include <Nemu/version.h>
#include <Nemu/vmm/cfgm.h>
#include <iprt/string.h>
#include <iprt/param.h>
#include <iprt/path.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the extension pack helpers. */
static PCNEMUEXTPACKHLP g_pHlp;


// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnInstalled}
//  */
// static DECLCALLBACK(void) nemuVNCExtPack_Installed(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnUninstall}
//  */
// static DECLCALLBACK(int)  nemuVNCExtPack_Uninstall(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
//
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVirtualBoxReady}
//  */
// static DECLCALLBACK(void)  nemuVNCExtPack_VirtualBoxReady(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
//
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnUnload}
//  */
// static DECLCALLBACK(void) nemuVNCExtPack_Unload(PCNEMUEXTPACKREG pThis);
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMCreated}
//  */
// static DECLCALLBACK(int)  nemuVNCExtPack_VMCreated(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox, IMachine *pMachine);
//
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMConfigureVMM}
//  */
// static DECLCALLBACK(int)  nemuVNCExtPack_VMConfigureVMM(PCNEMUEXTPACKREG pThis, IConsole *pConsole, PVM pVM);
//
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMPowerOn}
//  */
// static DECLCALLBACK(int)  nemuVNCExtPack_VMPowerOn(PCNEMUEXTPACKREG pThis, IConsole *pConsole, PVM pVM);
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMPowerOff}
//  */
// static DECLCALLBACK(void) nemuVNCExtPack_VMPowerOff(PCNEMUEXTPACKREG pThis, IConsole *pConsole, PVM pVM);
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMPowerOff}
//  */
// static DECLCALLBACK(void) nemuVNCExtPack_QueryObject(PCNEMUEXTPACKREG pThis, PCRTUUID pObjectId);


static const NEMUEXTPACKREG g_nemuVNCExtPackReg =
{
    NEMUEXTPACKREG_VERSION,
    /* .pfnInstalled =      */  NULL,
    /* .pfnUninstall =      */  NULL,
    /* .pfnVirtualBoxReady =*/  NULL,
    /* .pfnConsoleReady =   */  NULL,
    /* .pfnUnload =         */  NULL,
    /* .pfnVMCreated =      */  NULL,
    /* .pfnVMConfigureVMM = */  NULL,
    /* .pfnVMPowerOn =      */  NULL,
    /* .pfnVMPowerOff =     */  NULL,
    /* .pfnQueryObject =    */  NULL,
    NEMUEXTPACKREG_VERSION
};


/** @callback_method_impl{FNNEMUEXTPACKREGISTER}  */
extern "C" DECLEXPORT(int) NemuExtPackRegister(PCNEMUEXTPACKHLP pHlp, PCNEMUEXTPACKREG *ppReg, PRTERRINFO pErrInfo)
{
    /*
     * Check the VirtualBox version.
     */
    if (!NEMUEXTPACK_IS_VER_COMPAT(pHlp->u32Version, NEMUEXTPACKHLP_VERSION))
        return RTErrInfoSetF(pErrInfo, VERR_VERSION_MISMATCH,
                             "Helper version mismatch - expected %#x got %#x",
                             NEMUEXTPACKHLP_VERSION, pHlp->u32Version);
    if (   NEMU_FULL_VERSION_GET_MAJOR(pHlp->uNemuFullVersion) != NEMU_VERSION_MAJOR
        || NEMU_FULL_VERSION_GET_MINOR(pHlp->uNemuFullVersion) != NEMU_VERSION_MINOR)
        return RTErrInfoSetF(pErrInfo, VERR_VERSION_MISMATCH,
                             "VirtualBox version mismatch - expected %u.%u got %u.%u",
                             NEMU_VERSION_MAJOR, NEMU_VERSION_MINOR,
                             NEMU_FULL_VERSION_GET_MAJOR(pHlp->uNemuFullVersion),
                             NEMU_FULL_VERSION_GET_MINOR(pHlp->uNemuFullVersion));

    /*
     * We're good, save input and return the registration structure.
     */
    g_pHlp = pHlp;
    *ppReg = &g_nemuVNCExtPackReg;

    return VINF_SUCCESS;
}

