/** @file
 * VirtualBox - Extension Pack Interface.
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

#ifndef ___Nemu_ExtPack_ExtPack_h
#define ___Nemu_ExtPack_ExtPack_h

#include <Nemu/types.h>

/** @def NEMUEXTPACK_IF_CS
 * Selects 'class' on 'struct' for interface references.
 * @param I         The interface name
 */
#if defined(__cplusplus) && !defined(RT_OS_WINDOWS)
# define NEMUEXTPACK_IF_CS(I)   class I
#else
# define NEMUEXTPACK_IF_CS(I)   struct I
#endif

NEMUEXTPACK_IF_CS(IConsole);
NEMUEXTPACK_IF_CS(IMachine);
NEMUEXTPACK_IF_CS(IVirtualBox);

/**
 * Module kind for use with NEMUEXTPACKHLP::pfnFindModule.
 */
typedef enum NEMUEXTPACKMODKIND
{
    /** Zero is invalid as alwasy. */
    NEMUEXTPACKMODKIND_INVALID = 0,
    /** Raw-mode context module. */
    NEMUEXTPACKMODKIND_RC,
    /** Ring-0 context module. */
    NEMUEXTPACKMODKIND_R0,
    /** Ring-3 context module. */
    NEMUEXTPACKMODKIND_R3,
    /** End of the valid values (exclusive). */
    NEMUEXTPACKMODKIND_END,
    /** The usual 32-bit type hack. */
    NEMUEXTPACKMODKIND_32BIT_HACK = 0x7fffffff
} NEMUEXTPACKMODKIND;

/**
 * Contexts returned by NEMUEXTPACKHLP::pfnGetContext.
 */
typedef enum NEMUEXTPACKCTX
{
    /** Zero is invalid as alwasy. */
    NEMUEXTPACKCTX_INVALID = 0,
    /** The per-user daemon process (NemuSVC). */
    NEMUEXTPACKCTX_PER_USER_DAEMON,
    /** A VM process.
     * @remarks This will also include the client processes in v4.0.  */
    NEMUEXTPACKCTX_VM_PROCESS,
    /** A API client process.
     * @remarks This will not be returned by VirtualBox 4.0. */
    NEMUEXTPACKCTX_CLIENT_PROCESS,
    /** End of the valid values (exclusive). */
    NEMUEXTPACKCTX_END,
    /** The usual 32-bit type hack. */
    NEMUEXTPACKCTX_32BIT_HACK = 0x7fffffff
} NEMUEXTPACKCTX;


/** Pointer to const helpers passed to the NemuExtPackRegister() call. */
typedef const struct NEMUEXTPACKHLP *PCNEMUEXTPACKHLP;
/**
 * Extension pack helpers passed to NemuExtPackRegister().
 *
 * This will be valid until the module is unloaded.
 */
typedef struct NEMUEXTPACKHLP
{
    /** Interface version.
     * This is set to NEMUEXTPACKHLP_VERSION. */
    uint32_t                    u32Version;

    /** The VirtualBox full version (see NEMU_FULL_VERSION).  */
    uint32_t                    uNemuFullVersion;
    /** The VirtualBox subversion tree revision.  */
    uint32_t                    uNemuInternalRevision;
    /** Explicit alignment padding, must be zero. */
    uint32_t                    u32Padding;
    /** Pointer to the version string (read-only). */
    const char                 *pszNemuVersion;

    /**
     * Finds a module belonging to this extension pack.
     *
     * @returns Nemu status code.
     * @param   pHlp            Pointer to this helper structure.
     * @param   pszName         The module base name.
     * @param   pszExt          The extension. If NULL the default ring-3
     *                          library extension will be used.
     * @param   enmKind         The kind of module to locate.
     * @param   pszFound        Where to return the path to the module on
     *                          success.
     * @param   cbFound         The size of the buffer @a pszFound points to.
     * @param   pfNative        Where to return the native/agnostic indicator.
     */
    DECLR3CALLBACKMEMBER(int, pfnFindModule,(PCNEMUEXTPACKHLP pHlp, const char *pszName, const char *pszExt,
                                             NEMUEXTPACKMODKIND enmKind,
                                             char *pszFound, size_t cbFound, bool *pfNative));

    /**
     * Gets the path to a file belonging to this extension pack.
     *
     * @returns Nemu status code.
     * @retval  VERR_INVALID_POINTER if any of the pointers are invalid.
     * @retval  VERR_BUFFER_OVERFLOW if the buffer is too small.  The buffer
     *          will contain nothing.
     *
     * @param   pHlp            Pointer to this helper structure.
     * @param   pszFilename     The filename.
     * @param   pszPath         Where to return the path to the file on
     *                          success.
     * @param   cbPath          The size of the buffer @a pszPath.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetFilePath,(PCNEMUEXTPACKHLP pHlp, const char *pszFilename, char *pszPath, size_t cbPath));

    /**
     * Gets the context the extension pack is operating in.
     *
     * @returns The context.
     * @retval  NEMUEXTPACKCTX_INVALID if @a pHlp is invalid.
     *
     * @param   pHlp            Pointer to this helper structure.
     */
    DECLR3CALLBACKMEMBER(NEMUEXTPACKCTX, pfnGetContext,(PCNEMUEXTPACKHLP pHlp));

    /**
     * Loads a HGCM service provided by an extension pack.
     *
     * @returns Nemu status code.
     * @param   pHlp            Pointer to this helper structure.
     * @param   pConsole        Pointer to the VM's console object.
     * @param   pszServiceLibrary Name of the library file containing the
     *                          service implementation, without extension.
     * @param   pszServiceName  Name of HGCM service.
     */
    DECLR3CALLBACKMEMBER(int, pfnLoadHGCMService,(PCNEMUEXTPACKHLP pHlp, NEMUEXTPACK_IF_CS(IConsole) *pConsole,
                                                  const char *pszServiceLibrary, const char *pszServiceName));

    /**
     * Loads a VD plugin provided by an extension pack.
     *
     * This makes sense only in the context of the per-user service (NemuSVC).
     *
     * @returns Nemu status code.
     * @param   pHlp            Pointer to this helper structure.
     * @param   pVirtualBox     Pointer to the VirtualBox object.
     * @param   pszPluginLibrary Name of the library file containing the plugin
     *                          implementation, without extension.
     */
    DECLR3CALLBACKMEMBER(int, pfnLoadVDPlugin,(PCNEMUEXTPACKHLP pHlp, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox,
                                               const char *pszPluginLibrary));

    /**
     * Unloads a VD plugin provided by an extension pack.
     *
     * This makes sense only in the context of the per-user service (NemuSVC).
     *
     * @returns Nemu status code.
     * @param   pHlp            Pointer to this helper structure.
     * @param   pVirtualBox     Pointer to the VirtualBox object.
     * @param   pszPluginLibrary Name of the library file containing the plugin
     *                          implementation, without extension.
     */
    DECLR3CALLBACKMEMBER(int, pfnUnloadVDPlugin,(PCNEMUEXTPACKHLP pHlp, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox,
                                                 const char *pszPluginLibrary));

    DECLR3CALLBACKMEMBER(int, pfnReserved1,(PCNEMUEXTPACKHLP pHlp)); /**< Reserved for minor structure revisions. */
    DECLR3CALLBACKMEMBER(int, pfnReserved2,(PCNEMUEXTPACKHLP pHlp)); /**< Reserved for minor structure revisions. */
    DECLR3CALLBACKMEMBER(int, pfnReserved3,(PCNEMUEXTPACKHLP pHlp)); /**< Reserved for minor structure revisions. */
    DECLR3CALLBACKMEMBER(int, pfnReserved4,(PCNEMUEXTPACKHLP pHlp)); /**< Reserved for minor structure revisions. */
    DECLR3CALLBACKMEMBER(int, pfnReserved5,(PCNEMUEXTPACKHLP pHlp)); /**< Reserved for minor structure revisions. */
    DECLR3CALLBACKMEMBER(int, pfnReserved6,(PCNEMUEXTPACKHLP pHlp)); /**< Reserved for minor structure revisions. */

    /** End of structure marker (NEMUEXTPACKHLP_VERSION). */
    uint32_t                    u32EndMarker;
} NEMUEXTPACKHLP;
/** Current version of the NEMUEXTPACKHLP structure.  */
#define NEMUEXTPACKHLP_VERSION          RT_MAKE_U32(2, 1)


/** Pointer to the extension pack callback table. */
typedef struct NEMUEXTPACKREG const *PCNEMUEXTPACKREG;
/**
 * Callback table returned by NemuExtPackRegister.
 *
 * This must be valid until the extension pack main module is unloaded.
 */
typedef struct NEMUEXTPACKREG
{
    /** Interface version.
     * This is set to NEMUEXTPACKREG_VERSION. */
    uint32_t                    u32Version;

    /**
     * Hook for doing setups after the extension pack was installed.
     *
     * This is called in the context of the per-user service (NemuSVC).
     *
     * @returns Nemu status code.
     * @retval  VERR_EXTPACK_UNSUPPORTED_HOST_UNINSTALL if the extension pack
     *          requires some different host version or a prerequisite is
     *          missing from the host.  Automatic uninstall will be attempted.
     *          Must set error info.
     *
     * @param   pThis       Pointer to this structure.
     * @param   pVirtualBox The VirtualBox interface.
     * @param   pErrInfo    Where to return extended error information.
     */
    DECLCALLBACKMEMBER(int, pfnInstalled)(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox,
                                          PRTERRINFO pErrInfo);

    /**
     * Hook for cleaning up before the extension pack is uninstalled.
     *
     * This is called in the context of the per-user service (NemuSVC).
     *
     * @returns Nemu status code.
     * @param   pThis       Pointer to this structure.
     * @param   pVirtualBox The VirtualBox interface.
     *
     * @todo    This is currently called holding locks making pVirtualBox
     *          relatively unusable.
     */
    DECLCALLBACKMEMBER(int, pfnUninstall)(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);

    /**
     * Hook for doing work after the VirtualBox object is ready.
     *
     * This is called in the context of the per-user service (NemuSVC).  The
     * pfnConsoleReady method is the equivalent for the VM/client process.
     *
     * @param   pThis       Pointer to this structure.
     * @param   pVirtualBox The VirtualBox interface.
     */
    DECLCALLBACKMEMBER(void, pfnVirtualBoxReady)(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);

    /**
     * Hook for doing work after the Console object is ready.
     *
     * This is called in the context of the VM/client process.  The
     * pfnVirtualBoxReady method is the equivalent for the per-user service
     * (NemuSVC).
     *
     * @param   pThis       Pointer to this structure.
     * @param   pConsole    The Console interface.
     */
    DECLCALLBACKMEMBER(void, pfnConsoleReady)(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IConsole) *pConsole);

    /**
     * Hook for doing work before unloading.
     *
     * This is called both in the context of the per-user service (NemuSVC) and
     * in context of the VM process (NemuC).
     *
     * @param   pThis       Pointer to this structure.
     *
     * @remarks The helpers are not available at this point in time.
     * @remarks This is not called on uninstall, then pfnUninstall will be the
     *          last callback.
     */
    DECLCALLBACKMEMBER(void, pfnUnload)(PCNEMUEXTPACKREG pThis);

    /**
     * Hook for changing the default VM configuration upon creation.
     *
     * This is called in the context of the per-user service (NemuSVC).
     *
     * @returns Nemu status code.
     * @param   pThis       Pointer to this structure.
     * @param   pVirtualBox The VirtualBox interface.
     * @param   pMachine    The machine interface.
     */
    DECLCALLBACKMEMBER(int, pfnVMCreated)(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IVirtualBox) *pVirtualBox,
                                          NEMUEXTPACK_IF_CS(IMachine) *pMachine);

    /**
     * Hook for configuring the VMM for a VM.
     *
     * This is called in the context of the VM process (NemuC).
     *
     * @returns Nemu status code.
     * @param   pThis       Pointer to this structure.
     * @param   pConsole    The console interface.
     * @param   pVM         The cross context VM structure.
     */
    DECLCALLBACKMEMBER(int, pfnVMConfigureVMM)(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IConsole) *pConsole, PVM pVM);

    /**
     * Hook for doing work right before powering on the VM.
     *
     * This is called in the context of the VM process (NemuC).
     *
     * @returns Nemu status code.
     * @param   pThis       Pointer to this structure.
     * @param   pConsole    The console interface.
     * @param   pVM         The cross context VM structure.
     */
    DECLCALLBACKMEMBER(int, pfnVMPowerOn)(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IConsole) *pConsole, PVM pVM);

    /**
     * Hook for doing work after powering on the VM.
     *
     * This is called in the context of the VM process (NemuC).
     *
     * @param   pThis       Pointer to this structure.
     * @param   pConsole    The console interface.
     * @param   pVM         The cross context VM structure. Can be NULL.
     */
    DECLCALLBACKMEMBER(void, pfnVMPowerOff)(PCNEMUEXTPACKREG pThis, NEMUEXTPACK_IF_CS(IConsole) *pConsole, PVM pVM);

    /**
     * Query the IUnknown interface to an object in the main module.
     *
     * This is can be called in any context.
     *
     * @returns IUnknown pointer (referenced) on success, NULL on failure.
     * @param   pThis       Pointer to this structure.
     * @param   pObjectId   Pointer to the object ID (UUID).
     */
    DECLCALLBACKMEMBER(void *, pfnQueryObject)(PCNEMUEXTPACKREG pThis, PCRTUUID pObjectId);

    /** End of structure marker (NEMUEXTPACKREG_VERSION). */
    uint32_t                    u32EndMarker;
} NEMUEXTPACKREG;
/** Current version of the NEMUEXTPACKREG structure.  */
#define NEMUEXTPACKREG_VERSION        RT_MAKE_U32(0, 1)


/**
 * The NemuExtPackRegister callback function.
 *
 * PDM will invoke this function after loading a driver module and letting
 * the module decide which drivers to register and how to handle conflicts.
 *
 * @returns Nemu status code.
 * @param   pHlp            Pointer to the extension pack helper function
 *                          table.  This is valid until the module is unloaded.
 * @param   ppReg           Where to return the pointer to the registration
 *                          structure containing all the hooks.  This structure
 *                          be valid and unchanged until the module is unloaded
 *                          (i.e. use some static const data for it).
 * @param   pErrInfo        Where to return extended error information.
 */
typedef DECLCALLBACK(int) FNNEMUEXTPACKREGISTER(PCNEMUEXTPACKHLP pHlp, PCNEMUEXTPACKREG *ppReg, PRTERRINFO pErrInfo);
/** Pointer to a FNNEMUEXTPACKREGISTER. */
typedef FNNEMUEXTPACKREGISTER *PFNNEMUEXTPACKREGISTER;

/** The name of the main module entry point. */
#define NEMU_EXTPACK_MAIN_MOD_ENTRY_POINT   "NemuExtPackRegister"


/**
 * Checks if extension pack interface version is compatible.
 *
 * @returns true if the do, false if they don't.
 * @param   u32Provider     The provider version.
 * @param   u32User         The user version.
 */
#define NEMUEXTPACK_IS_VER_COMPAT(u32Provider, u32User) \
    (    NEMUEXTPACK_IS_MAJOR_VER_EQUAL(u32Provider, u32User) \
      && (int32_t)RT_LOWORD(u32Provider) >= (int32_t)RT_LOWORD(u32User) ) /* stupid casts to shut up gcc */

/**
 * Check if two extension pack interface versions has the same major version.
 *
 * @returns true if the do, false if they don't.
 * @param   u32Ver1         The first version number.
 * @param   u32Ver2         The second version number.
 */
#define NEMUEXTPACK_IS_MAJOR_VER_EQUAL(u32Ver1, u32Ver2)  (RT_HIWORD(u32Ver1) == RT_HIWORD(u32Ver2))

#endif

