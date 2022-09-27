/* $Id: NemuBusMouseMain.cpp $ */
/** @file
 * Bus Mouse main module.
 */

/*
 * Copyright (C) 2010-2015 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
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
// static DECLCALLBACK(void) nemuSkeletonExtPack_Installed(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnUninstall}
//  */
// static DECLCALLBACK(int)  nemuSkeletonExtPack_Uninstall(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
//
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVirtualBoxReady}
//  */
// static DECLCALLBACK(void)  nemuSkeletonExtPack_VirtualBoxReady(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
//
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnUnload}
//  */
// static DECLCALLBACK(void) nemuSkeletonExtPack_Unload(PCNEMUEXTPACKREG pThis);
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMCreated}
//  */
// static DECLCALLBACK(int)  nemuSkeletonExtPack_VMCreated(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox, IMachine *pMachine);
//

/**
 * @interface_method_impl{NEMUEXTPACKREG,pfnVMConfigureVMM
 */
static DECLCALLBACK(int)  nemuBusMouseExtPack_VMConfigureVMM(PCNEMUEXTPACKREG pThis, IConsole *pConsole, PVM pVM)
{
    /*
     * Find the bus mouse module and tell PDM to load it.
     * ASSUME /PDM/Devices exists.
     */
    char szPath[RTPATH_MAX];
    int rc = g_pHlp->pfnFindModule(g_pHlp, "NemuBusMouseR3", NULL, NEMUEXTPACKMODKIND_R3, szPath, sizeof(szPath), NULL);
    if (RT_FAILURE(rc))
        return rc;

    PCFGMNODE pCfgRoot = CFGMR3GetRoot(pVM);
    AssertReturn(pCfgRoot, VERR_INTERNAL_ERROR_3);

    PCFGMNODE pCfgDevices = CFGMR3GetChild(pCfgRoot, "PDM/Devices");
    AssertReturn(pCfgDevices, VERR_INTERNAL_ERROR_3);

    PCFGMNODE pCfgMine;
    rc = CFGMR3InsertNode(pCfgDevices, "NemuBusMouse", &pCfgMine);
    AssertRCReturn(rc, rc);
    rc = CFGMR3InsertString(pCfgMine, "Path", szPath);
    AssertRCReturn(rc, rc);

    /*
     * Tell PDM where to find the R0 and RC modules for the bus mouse device.
     */
#ifdef NEMU_WITH_RAW_MODE
    rc = g_pHlp->pfnFindModule(g_pHlp, "NemuBusMouseRC", NULL, NEMUEXTPACKMODKIND_RC, szPath, sizeof(szPath), NULL);
    AssertRCReturn(rc, rc);
    RTPathStripFilename(szPath);
    rc = CFGMR3InsertString(pCfgMine, "RCSearchPath", szPath);
    AssertRCReturn(rc, rc);
#endif

    rc = g_pHlp->pfnFindModule(g_pHlp, "NemuBusMouseR0", NULL, NEMUEXTPACKMODKIND_R0, szPath, sizeof(szPath), NULL);
    AssertRCReturn(rc, rc);
    RTPathStripFilename(szPath);
    rc = CFGMR3InsertString(pCfgMine, "R0SearchPath", szPath);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMPowerOn}
//  */
// static DECLCALLBACK(int)  nemuSkeletonExtPack_VMPowerOn(PCNEMUEXTPACKREG pThis, IConsole *pConsole, PVM pVM);
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMPowerOff}
//  */
// static DECLCALLBACK(void) nemuSkeletonExtPack_VMPowerOff(PCNEMUEXTPACKREG pThis, IConsole *pConsole, PVM pVM);
// /**
//  * @interface_method_impl{NEMUEXTPACKREG,pfnVMPowerOff}
//  */
// static DECLCALLBACK(void) nemuSkeletonExtPack_QueryObject(PCNEMUEXTPACKREG pThis, PCRTUUID pObjectId);


static const NEMUEXTPACKREG g_nemuBusMouseExtPackReg =
{
    NEMUEXTPACKREG_VERSION,
    /* .pfnInstalled =      */  NULL,
    /* .pfnUninstall =      */  NULL,
    /* .pfnVirtualBoxReady =*/  NULL,
    /* .pfnConsoleReady =   */  NULL,
    /* .pfnUnload =         */  NULL,
    /* .pfnVMCreated =      */  NULL,
    /* .pfnVMConfigureVMM = */  nemuBusMouseExtPack_VMConfigureVMM,
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
    *ppReg = &g_nemuBusMouseExtPackReg;

    return VINF_SUCCESS;
}

