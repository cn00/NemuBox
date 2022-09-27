/* $Id: NemuServiceInternal.h $ */
/** @file
 * NemuService - Guest Additions Services.
 */

/*
 * Copyright (C) 2007-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuServiceInternal_h
#define ___NemuServiceInternal_h

#include <stdio.h>
#ifdef RT_OS_WINDOWS
# include <Windows.h>
# include <process.h> /* Needed for file version information. */
#endif

#include <iprt/list.h>
#include <iprt/critsect.h>

#include <Nemu/NemuGuestLib.h>
#include <Nemu/HostServices/GuestControlSvc.h>

/**
 * A service descriptor.
 */
typedef struct
{
    /** The short service name. */
    const char *pszName;
    /** The longer service name. */
    const char *pszDescription;
    /** The usage options stuff for the --help screen. */
    const char *pszUsage;
    /** The option descriptions for the --help screen. */
    const char *pszOptions;

    /**
     * Called before parsing arguments.
     * @returns Nemu status code.
     */
    DECLCALLBACKMEMBER(int, pfnPreInit)(void);

    /**
     * Tries to parse the given command line option.
     *
     * @returns 0 if we parsed, -1 if it didn't and anything else means exit.
     * @param   ppszShort   If not NULL it points to the short option iterator. a short argument.
     *                      If NULL examine argv[*pi].
     * @param   argc        The argument count.
     * @param   argv        The argument vector.
     * @param   pi          The argument vector index. Update if any value(s) are eaten.
     */
    DECLCALLBACKMEMBER(int, pfnOption)(const char **ppszShort, int argc, char **argv, int *pi);

    /**
     * Called before parsing arguments.
     * @returns Nemu status code.
     */
    DECLCALLBACKMEMBER(int, pfnInit)(void);

    /** Called from the worker thread.
     *
     * @returns Nemu status code.
     * @retval  VINF_SUCCESS if exitting because *pfShutdown was set.
     * @param   pfShutdown      Pointer to a per service termination flag to check
     *                          before and after blocking.
     */
    DECLCALLBACKMEMBER(int, pfnWorker)(bool volatile *pfShutdown);

    /**
     * Stops a service.
     */
    DECLCALLBACKMEMBER(void, pfnStop)(void);

    /**
     * Does termination cleanups.
     *
     * @remarks This may be called even if pfnInit hasn't been called!
     */
    DECLCALLBACKMEMBER(void, pfnTerm)(void);
} NEMUSERVICE;
/** Pointer to a NEMUSERVICE. */
typedef NEMUSERVICE *PNEMUSERVICE;
/** Pointer to a const NEMUSERVICE. */
typedef NEMUSERVICE const *PCNEMUSERVICE;

/* Default call-backs for services which do not need special behaviour. */
DECLCALLBACK(int)  VGSvcDefaultPreInit(void);
DECLCALLBACK(int)  VGSvcDefaultOption(const char **ppszShort, int argc, char **argv, int *pi);
DECLCALLBACK(int)  VGSvcDefaultInit(void);
DECLCALLBACK(void) VGSvcDefaultTerm(void);

/** The service name.
 * @note Used on windows to name the service as well as the global mutex. */
#define NEMUSERVICE_NAME            "NemuService"

#ifdef RT_OS_WINDOWS
/** The friendly service name. */
# define NEMUSERVICE_FRIENDLY_NAME  "VirtualBox Guest Additions Service"
/** The service description (only W2K+ atm) */
# define NEMUSERVICE_DESCRIPTION    "Manages VM runtime information, time synchronization, guest control execution and miscellaneous utilities for guest operating systems."
/** The following constant may be defined by including NtStatus.h. */
# define STATUS_SUCCESS             ((NTSTATUS)0x00000000L)
#endif /* RT_OS_WINDOWS */

#ifdef NEMU_WITH_GUEST_PROPS
/**
 * A guest property cache.
 */
typedef struct NEMUSERVICEVEPROPCACHE
{
    /** The client ID for HGCM communication. */
    uint32_t        uClientID;
    /** Head in a list of NEMUSERVICEVEPROPCACHEENTRY nodes. */
    RTLISTANCHOR    NodeHead;
    /** Critical section for thread-safe use. */
    RTCRITSECT      CritSect;
} NEMUSERVICEVEPROPCACHE;
/** Pointer to a guest property cache. */
typedef NEMUSERVICEVEPROPCACHE *PNEMUSERVICEVEPROPCACHE;

/**
 * An entry in the property cache (NEMUSERVICEVEPROPCACHE).
 */
typedef struct NEMUSERVICEVEPROPCACHEENTRY
{
    /** Node to successor.
     * @todo r=bird: This is not really the node to the successor, but
     *       rather the OUR node in the list.  If it helps, remember that
     *       its a doubly linked list. */
    RTLISTNODE  NodeSucc;
    /** Name (and full path) of guest property. */
    char       *pszName;
    /** The last value stored (for reference). */
    char       *pszValue;
    /** Reset value to write if property is temporary.  If NULL, it will be
     *  deleted. */
    char       *pszValueReset;
    /** Flags. */
    uint32_t    fFlags;
} NEMUSERVICEVEPROPCACHEENTRY;
/** Pointer to a cached guest property. */
typedef NEMUSERVICEVEPROPCACHEENTRY *PNEMUSERVICEVEPROPCACHEENTRY;

#endif /* NEMU_WITH_GUEST_PROPS */

RT_C_DECLS_BEGIN

extern char        *g_pszProgName;
extern unsigned     g_cVerbosity;
extern char         g_szLogFile[RTPATH_MAX + 128];
extern uint32_t     g_DefaultInterval;
extern NEMUSERVICE  g_TimeSync;
extern NEMUSERVICE  g_Clipboard;
extern NEMUSERVICE  g_Control;
extern NEMUSERVICE  g_VMInfo;
extern NEMUSERVICE  g_CpuHotPlug;
#ifdef NEMUSERVICE_MANAGEMENT
extern NEMUSERVICE  g_MemBalloon;
extern NEMUSERVICE  g_VMStatistics;
#endif
#ifdef NEMUSERVICE_PAGE_SHARING
extern NEMUSERVICE  g_PageSharing;
#endif
#ifdef NEMU_WITH_SHARED_FOLDERS
extern NEMUSERVICE  g_AutoMount;
#endif
#ifdef DEBUG
extern RTCRITSECT   g_csLog; /* For guest process stdout dumping. */
#endif

extern RTEXITCODE               VGSvcSyntax(const char *pszFormat, ...);
extern RTEXITCODE               VGSvcError(const char *pszFormat, ...);
extern void                     VGSvcVerbose(unsigned iLevel, const char *pszFormat, ...);
extern int                      VGSvcLogCreate(const char *pszLogFile);
extern void                     VGSvcLogDestroy(void);
extern int                      VGSvcArgUInt32(int argc, char **argv, const char *psz, int *pi, uint32_t *pu32,
                                               uint32_t u32Min, uint32_t u32Max);

/* Exposing the following bits because of windows: */
extern int                      VGSvcStartServices(void);
extern int                      VGSvcStopServices(void);
extern void                     VGSvcMainWait(void);
extern int                      VGSvcReportStatus(NemuGuestFacilityStatus enmStatus);
#ifdef RT_OS_WINDOWS
extern RTEXITCODE               VGSvcWinInstall(void);
extern RTEXITCODE               VGSvcWinUninstall(void);
extern RTEXITCODE               VGSvcWinEnterCtrlDispatcher(void);
extern void                     VGSvcWinSetStopPendingStatus(uint32_t uCheckPoint);
#endif

#ifdef NEMUSERVICE_TOOLBOX
extern bool                     VGSvcToolboxMain(int argc, char **argv, RTEXITCODE *prcExit);
#endif

#ifdef RT_OS_WINDOWS
# ifdef NEMU_WITH_GUEST_PROPS
extern int                      VGSvcVMInfoWinWriteUsers(PNEMUSERVICEVEPROPCACHE pCache, char **ppszUserList, uint32_t *pcUsersInList);
extern int                      VGSvcVMInfoWinGetComponentVersions(uint32_t uClientID);
# endif /* NEMU_WITH_GUEST_PROPS */
#endif /* RT_OS_WINDOWS */

#ifdef NEMUSERVICE_MANAGEMENT
extern uint32_t                 VGSvcBalloonQueryPages(uint32_t cbPage);
#endif
#if defined(NEMUSERVICE_PAGE_SHARING)
extern RTEXITCODE               VGSvcPageSharingWorkerChild(void);
#endif
extern int                      VGSvcVMInfoSignal(void);

RT_C_DECLS_END

#endif

