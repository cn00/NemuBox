/* $Id: NemuTray.cpp $ */
/** @file
 * NemuTray - Guest Additions Tray Application
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
#include <package-generated.h>
#include "product-generated.h"

#include "NemuTray.h"
#include "NemuTrayMsg.h"
#include "NemuHelpers.h"
#include "NemuSeamless.h"
#include "NemuClipboard.h"
#include "NemuDisplay.h"
#include "NemuVRDP.h"
#include "NemuHostVersion.h"
#include "NemuSharedFolders.h"
#ifdef NEMU_WITH_DRAG_AND_DROP
# include "NemuDnD.h"
#endif
#include "NemuIPC.h"
#include "NemuLA.h"
#include <NemuHook.h>
#include "resource.h"
#include <malloc.h>
#include <NemuGuestInternal.h>

#include <sddl.h>

#include <iprt/buildconfig.h>
#include <iprt/ldr.h>
#include <iprt/process.h>
#include <iprt/system.h>
#include <iprt/time.h>

#ifdef DEBUG
# define LOG_ENABLED
# define LOG_GROUP LOG_GROUP_DEFAULT
#endif
#include <Nemu/log.h>

/* Default desktop state tracking */
#include <Wtsapi32.h>

/*
 * St (session [state] tracking) functionality API
 *
 * !!!NOTE: this API is NOT thread-safe!!!
 * it is supposed to be called & used from within the window message handler thread
 * of the window passed to nemuStInit */
static int nemuStInit(HWND hWnd);
static void nemuStTerm(void);
/* @returns true on "IsActiveConsole" state change */
static BOOL nemuStHandleEvent(WPARAM EventID, LPARAM SessionID);
static BOOL nemuStIsActiveConsole();
static BOOL nemuStCheckTimer(WPARAM wEvent);

/*
 * Dt (desktop [state] tracking) functionality API
 *
 * !!!NOTE: this API is NOT thread-safe!!!
 * */
static int nemuDtInit();
static void nemuDtTerm();
/* @returns true on "IsInputDesktop" state change */
static BOOL nemuDtHandleEvent();
/* @returns true iff the application (NemuTray) desktop is input */
static BOOL nemuDtIsInputDesktop();
static HANDLE nemuDtGetNotifyEvent();
static BOOL nemuDtCheckTimer(WPARAM wParam);

/* caps API */
#define NEMUCAPS_ENTRY_IDX_SEAMLESS  0
#define NEMUCAPS_ENTRY_IDX_GRAPHICS  1
#define NEMUCAPS_ENTRY_IDX_COUNT     2

typedef enum NEMUCAPS_ENTRY_FUNCSTATE
{
    /* the cap is unsupported */
    NEMUCAPS_ENTRY_FUNCSTATE_UNSUPPORTED = 0,
    /* the cap is supported */
    NEMUCAPS_ENTRY_FUNCSTATE_SUPPORTED,
    /* the cap functionality is started, it can be disabled however if its AcState is not ACQUIRED */
    NEMUCAPS_ENTRY_FUNCSTATE_STARTED,
} NEMUCAPS_ENTRY_FUNCSTATE;


static void NemuCapsEntryFuncStateSet(uint32_t iCup, NEMUCAPS_ENTRY_FUNCSTATE enmFuncState);
static int NemuCapsInit();
static int NemuCapsReleaseAll();
static void NemuCapsTerm();
static BOOL NemuCapsEntryIsAcquired(uint32_t iCap);
static BOOL NemuCapsEntryIsEnabled(uint32_t iCap);
static BOOL NemuCapsCheckTimer(WPARAM wParam);
static int NemuCapsEntryRelease(uint32_t iCap);
static int NemuCapsEntryAcquire(uint32_t iCap);
static int NemuCapsAcquireAllSupported();

/* console-related caps API */
static BOOL NemuConsoleIsAllowed();
static void NemuConsoleEnable(BOOL fEnable);
static void NemuConsoleCapSetSupported(uint32_t iCap, BOOL fSupported);

static void NemuGrapicsSetSupported(BOOL fSupported);


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int nemuTrayCreateTrayIcon(void);
static LRESULT CALLBACK nemuToolWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* Global message handler prototypes. */
static int nemuTrayGlMsgTaskbarCreated(WPARAM lParam, LPARAM wParam);
/*static int nemuTrayGlMsgShowBalloonMsg(WPARAM lParam, LPARAM wParam);*/

static int NemuAcquireGuestCaps(uint32_t fOr, uint32_t fNot, bool fCfg);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
HANDLE                ghNemuDriver;
HANDLE                ghStopSem;
HANDLE                ghSeamlessWtNotifyEvent = 0;
HANDLE                ghSeamlessKmNotifyEvent = 0;
SERVICE_STATUS        gNemuServiceStatus;
SERVICE_STATUS_HANDLE gNemuServiceStatusHandle;
HINSTANCE             ghInstance;
HWND                  ghwndToolWindow;
NOTIFYICONDATA        gNotifyIconData;
DWORD                 gMajorVersion;

static PRTLOGGER      g_pLoggerRelease = NULL;
static uint32_t       g_cHistory = 10;                   /* Enable log rotation, 10 files. */
static uint32_t       g_uHistoryFileTime = RT_SEC_1DAY;  /* Max 1 day per file. */
static uint64_t       g_uHistoryFileSize = 100 * _1M;    /* Max 100MB per file. */

/* The service table. */
static NEMUSERVICEINFO nemuServiceTable[] =
{
    {
        "Display",
        NemuDisplayInit,
        NemuDisplayThread,
        NULL /* pfnStop */,
        NemuDisplayDestroy
    },
    {
        "Shared Clipboard",
        NemuClipboardInit,
        NemuClipboardThread,
        NULL /* pfnStop */,
        NemuClipboardDestroy
    },
    {
        "Seamless Windows",
        NemuSeamlessInit,
        NemuSeamlessThread,
        NULL /* pfnStop */,
        NemuSeamlessDestroy
    },
    {
        "VRDP",
        NemuVRDPInit,
        NemuVRDPThread,
        NULL /* pfnStop */,
        NemuVRDPDestroy
    },
    {
        "IPC",
        NemuIPCInit,
        NemuIPCThread,
        NemuIPCStop,
        NemuIPCDestroy
    },
    {
        "Location Awareness",
        NemuLAInit,
        NemuLAThread,
        NULL /* pfnStop */,
        NemuLADestroy
    },
#ifdef NEMU_WITH_DRAG_AND_DROP
    {
        "Drag and Drop",
        NemuDnDInit,
        NemuDnDThread,
        NemuDnDStop,
        NemuDnDDestroy
    },
#endif
    {
        NULL
    }
};

/* The global message table. */
static NEMUGLOBALMESSAGE s_nemuGlobalMessageTable[] =
{
    /* Windows specific stuff. */
    {
        "TaskbarCreated",
        nemuTrayGlMsgTaskbarCreated
    },

    /* NemuTray specific stuff. */
    /** @todo Add new messages here! */

    {
        NULL
    }
};

/**
 * Gets called whenever the Windows main taskbar
 * get (re-)created. Nice to install our tray icon.
 *
 * @return  IPRT status code.
 * @param   wParam
 * @param   lParam
 */
static int nemuTrayGlMsgTaskbarCreated(WPARAM wParam, LPARAM lParam)
{
    return nemuTrayCreateTrayIcon();
}

static int nemuTrayCreateTrayIcon(void)
{
    HICON hIcon = LoadIcon(ghInstance, MAKEINTRESOURCE(IDI_VIRTUALBOX));
    if (hIcon == NULL)
    {
        DWORD dwErr = GetLastError();
        Log(("Could not load tray icon, error %08X\n", dwErr));
        return RTErrConvertFromWin32(dwErr);
    }

    /* Prepare the system tray icon. */
    RT_ZERO(gNotifyIconData);
    gNotifyIconData.cbSize           = NOTIFYICONDATA_V1_SIZE; // sizeof(NOTIFYICONDATA);
    gNotifyIconData.hWnd             = ghwndToolWindow;
    gNotifyIconData.uID              = ID_TRAYICON;
    gNotifyIconData.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gNotifyIconData.uCallbackMessage = WM_NEMUTRAY_TRAY_ICON;
    gNotifyIconData.hIcon            = hIcon;

    sprintf(gNotifyIconData.szTip, "%s Guest Additions %d.%d.%dr%d",
            NEMU_PRODUCT, NEMU_VERSION_MAJOR, NEMU_VERSION_MINOR, NEMU_VERSION_BUILD, NEMU_SVN_REV);

    int rc = VINF_SUCCESS;
    if (!Shell_NotifyIcon(NIM_ADD, &gNotifyIconData))
    {
        DWORD dwErr = GetLastError();
        Log(("Could not create tray icon, error = %08X\n", dwErr));
        rc = RTErrConvertFromWin32(dwErr);
        RT_ZERO(gNotifyIconData);
    }

    if (hIcon)
        DestroyIcon(hIcon);
    return rc;
}

static void nemuTrayRemoveTrayIcon()
{
    if (gNotifyIconData.cbSize > 0)
    {
        /* Remove the system tray icon and refresh system tray. */
        Shell_NotifyIcon(NIM_DELETE, &gNotifyIconData);
        HWND hTrayWnd = FindWindow("Shell_TrayWnd", NULL); /* We assume we only have one tray atm. */
        if (hTrayWnd)
        {
            HWND hTrayNotifyWnd = FindWindowEx(hTrayWnd, 0, "TrayNotifyWnd", NULL);
            if (hTrayNotifyWnd)
                SendMessage(hTrayNotifyWnd, WM_PAINT, 0, NULL);
        }
        RT_ZERO(gNotifyIconData);
    }
}

static int nemuTrayStartServices(NEMUSERVICEENV *pEnv, NEMUSERVICEINFO *pTable)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);
    AssertPtrReturn(pTable, VERR_INVALID_POINTER);

    Log(("Starting services ...\n"));

    /** @todo Use IPRT events here. */
    pEnv->hStopEvent = CreateEvent(NULL, TRUE /* bManualReset */,
                                   FALSE /* bInitialState */, NULL);

    if (!pEnv->hStopEvent)
    {
        /* Could not create event. */
        return VERR_NOT_SUPPORTED;
    }

    while (   pTable
           && pTable->pszName)
    {
        Log(("Starting %s ...\n", pTable->pszName));

        int rc = VINF_SUCCESS;

        bool fStartThread = false;

        pTable->hThread = (HANDLE)0;
        pTable->pInstance = NULL;
        pTable->fStarted = false;

        if (pTable->pfnInit)
            rc = pTable->pfnInit(pEnv, &pTable->pInstance, &fStartThread);

        if (RT_FAILURE(rc))
        {
            LogRel(("Failed to initialize service \"%s\", rc=%Rrc\n",
                    pTable->pszName, rc));
        }
        else
        {
            if (   pTable->pfnThread
                && fStartThread)
            {
                unsigned threadid;
                /** @todo Use RTThread* here. */
                pTable->hThread = (HANDLE)_beginthreadex(NULL,  /* security */
                                                         0,     /* stacksize */
                                                         pTable->pfnThread,
                                                         pTable->pInstance,
                                                         0,     /* initflag */
                                                         &threadid);
                if (pTable->hThread == (HANDLE)(0))
                    rc = VERR_NOT_SUPPORTED;
            }

            if (RT_SUCCESS(rc))
                pTable->fStarted = true;
            else
            {
                Log(("Failed to start the thread\n"));
                if (pTable->pfnDestroy)
                    pTable->pfnDestroy(pEnv, pTable->pInstance);
            }
        }

        /* Advance to next table element. */
        pTable++;
    }

    return VINF_SUCCESS;
}

static void nemuTrayStopServices(NEMUSERVICEENV *pEnv, NEMUSERVICEINFO *pTable)
{
    if (!pEnv->hStopEvent)
        return;

    /* Signal to all threads. */
    SetEvent(pEnv->hStopEvent);

    NEMUSERVICEINFO *pCurTable = pTable;
    while (   pCurTable
           && pCurTable->pszName)
    {
        if (pCurTable->pfnStop)
            pCurTable->pfnStop(pEnv, pCurTable->pInstance);

        /* Advance to next table element. */
        pCurTable++;
    }

    pCurTable = pTable; /* Reset to first element. */
    while (   pCurTable
           && pCurTable->pszName)
    {
        if (pCurTable->fStarted)
        {
            if (pCurTable->pfnThread)
            {
                /* There is a thread, wait for termination. */
                /** @todo Use RTThread* here. */
                /** @todo Don't wait forever here. Use a sensible default. */
                WaitForSingleObject(pCurTable->hThread, INFINITE);

                /** @todo Dito. */
                CloseHandle(pCurTable->hThread);
                pCurTable->hThread = NULL;
            }

            if (pCurTable->pfnDestroy)
                pCurTable->pfnDestroy(pEnv, pCurTable->pInstance);
            pCurTable->fStarted = false;
        }

        /* Advance to next table element. */
        pCurTable++;
    }

    CloseHandle(pEnv->hStopEvent);
}

static int nemuTrayRegisterGlobalMessages(PNEMUGLOBALMESSAGE pTable)
{
    int rc = VINF_SUCCESS;
    if (pTable == NULL) /* No table to register? Skip. */
        return rc;
    while (   pTable->pszName
           && RT_SUCCESS(rc))
    {
        /* Register global accessible window messages. */
        pTable->uMsgID = RegisterWindowMessage(TEXT(pTable->pszName));
        if (!pTable->uMsgID)
        {
            DWORD dwErr = GetLastError();
            Log(("Registering global message \"%s\" failed, error = %08X\n", dwErr));
            rc = RTErrConvertFromWin32(dwErr);
        }

        /* Advance to next table element. */
        pTable++;
    }
    return rc;
}

static bool nemuTrayHandleGlobalMessages(PNEMUGLOBALMESSAGE pTable, UINT uMsg,
                                         WPARAM wParam, LPARAM lParam)
{
    if (pTable == NULL)
        return false;
    while (pTable && pTable->pszName)
    {
        if (pTable->uMsgID == uMsg)
        {
            if (pTable->pfnHandler)
                pTable->pfnHandler(wParam, lParam);
            return true;
        }

        /* Advance to next table element. */
        pTable++;
    }
    return false;
}

static int nemuTrayOpenBaseDriver(void)
{
    /* Open Nemu guest driver. */
    DWORD dwErr = ERROR_SUCCESS;
    ghNemuDriver = CreateFile(NEMUGUEST_DEVICE_NAME,
                             GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                             NULL);
    if (ghNemuDriver == INVALID_HANDLE_VALUE)
    {
        dwErr = GetLastError();
        LogRel(("Could not open VirtualBox Guest Additions driver! Please install / start it first! Error = %08X\n", dwErr));
    }
    return RTErrConvertFromWin32(dwErr);
}

static void nemuTrayCloseBaseDriver(void)
{
    if (ghNemuDriver)
    {
        CloseHandle(ghNemuDriver);
        ghNemuDriver = NULL;
    }
}

/**
 * Release logger callback.
 *
 * @return  IPRT status code.
 * @param   pLoggerRelease
 * @param   enmPhase
 * @param   pfnLog
 */
static void nemuTrayLogHeaderFooter(PRTLOGGER pLoggerRelease, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
{
    /* Some introductory information. */
    static RTTIMESPEC s_TimeSpec;
    char szTmp[256];
    if (enmPhase == RTLOGPHASE_BEGIN)
        RTTimeNow(&s_TimeSpec);
    RTTimeSpecToString(&s_TimeSpec, szTmp, sizeof(szTmp));

    switch (enmPhase)
    {
        case RTLOGPHASE_BEGIN:
        {
            pfnLog(pLoggerRelease,
                   "NemuTray %s r%s %s (%s %s) release log\n"
                   "Log opened %s\n",
                   RTBldCfgVersion(), RTBldCfgRevisionStr(), NEMU_BUILD_TARGET,
                   __DATE__, __TIME__, szTmp);

            int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Product: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Release: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Version: %s\n", szTmp);
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Service Pack: %s\n", szTmp);

            /* the package type is interesting for Linux distributions */
            char szExecName[RTPATH_MAX];
            char *pszExecName = RTProcGetExecutablePath(szExecName, sizeof(szExecName));
            pfnLog(pLoggerRelease,
                   "Executable: %s\n"
                   "Process ID: %u\n"
                   "Package type: %s"
#ifdef NEMU_OSE
                   " (OSE)"
#endif
                   "\n",
                   pszExecName ? pszExecName : "unknown",
                   RTProcSelf(),
                   NEMU_PACKAGE_STRING);
            break;
        }

        case RTLOGPHASE_PREROTATE:
            pfnLog(pLoggerRelease, "Log rotated - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_POSTROTATE:
            pfnLog(pLoggerRelease, "Log continuation - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_END:
            pfnLog(pLoggerRelease, "End of log file - Log started %s\n", szTmp);
            break;

        default:
            /* nothing */;
    }
}

/**
 * Creates the default release logger outputting to the specified file.
 * Pass NULL for disabled logging.
 *
 * @return  IPRT status code.
 * @param   pszLogFile              Filename for log output.  Optional.
 */
static int nemuTrayLogCreate(const char *pszLogFile)
{
    /* Create release logger (stdout + file). */
    static const char * const s_apszGroups[] = NEMU_LOGGROUP_NAMES;
    RTUINT fFlags = RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif
    char szError[RTPATH_MAX + 128] = "";
    int rc = RTLogCreateEx(&g_pLoggerRelease, fFlags,
#ifdef DEBUG
                           "all.e.l.f",
                           "NEMUTRAY_LOG",
#else
                           "all",
                           "NEMUTRAY_RELEASE_LOG",
#endif
                           RT_ELEMENTS(s_apszGroups), s_apszGroups, RTLOGDEST_STDOUT,
                           nemuTrayLogHeaderFooter, g_cHistory, g_uHistoryFileSize, g_uHistoryFileTime,
                           szError, sizeof(szError), pszLogFile);
    if (RT_SUCCESS(rc))
    {
#ifdef DEBUG
        RTLogSetDefaultInstance(g_pLoggerRelease);
#else
        /* Register this logger as the release logger. */
        RTLogRelSetDefaultInstance(g_pLoggerRelease);
#endif
        /* Explicitly flush the log in case of NEMUTRAY_RELEASE_LOG=buffered. */
        RTLogFlush(g_pLoggerRelease);
    }
    else
        MessageBox(GetDesktopWindow(),
                   szError, "NemuTray - Logging Error", MB_OK | MB_ICONERROR);

    return rc;
}

static void nemuTrayLogDestroy(void)
{
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
}

static void nemuTrayDestroyToolWindow(void)
{
    if (ghwndToolWindow)
    {
        Log(("Destroying tool window ...\n"));

        /* Destroy the tool window. */
        DestroyWindow(ghwndToolWindow);
        ghwndToolWindow = NULL;

        UnregisterClass("NemuTrayToolWndClass", ghInstance);
    }
}

static int nemuTrayCreateToolWindow(void)
{
    DWORD dwErr = ERROR_SUCCESS;

    /* Create a custom window class. */
    WNDCLASS windowClass = {0};
    windowClass.style         = CS_NOCLOSE;
    windowClass.lpfnWndProc   = (WNDPROC)nemuToolWndProc;
    windowClass.hInstance     = ghInstance;
    windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = "NemuTrayToolWndClass";
    if (!RegisterClass(&windowClass))
    {
        dwErr = GetLastError();
        Log(("Registering invisible tool window failed, error = %08X\n", dwErr));
    }
    else
    {
        /*
         * Create our (invisible) tool window.
         * Note: The window name ("NemuTrayToolWnd") and class ("NemuTrayToolWndClass") is
         * needed for posting globally registered messages to NemuTray and must not be
         * changed! Otherwise things get broken!
         *
         */
        ghwndToolWindow = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                         "NemuTrayToolWndClass", "NemuTrayToolWnd",
                                         WS_POPUPWINDOW,
                                         -200, -200, 100, 100, NULL, NULL, ghInstance, NULL);
        if (!ghwndToolWindow)
        {
            dwErr = GetLastError();
            Log(("Creating invisible tool window failed, error = %08X\n", dwErr));
        }
        else
        {
            /* Reload the cursor(s). */
            hlpReloadCursor();

            Log(("Invisible tool window handle = %p\n", ghwndToolWindow));
        }
    }

    if (dwErr != ERROR_SUCCESS)
         nemuTrayDestroyToolWindow();
    return RTErrConvertFromWin32(dwErr);
}

static int nemuTraySetupSeamless(void)
{
    OSVERSIONINFO info;
    gMajorVersion = 5; /* Default to Windows XP. */
    info.dwOSVersionInfoSize = sizeof(info);
    if (GetVersionEx(&info))
    {
        Log(("Windows version %ld.%ld\n", info.dwMajorVersion, info.dwMinorVersion));
        gMajorVersion = info.dwMajorVersion;
    }

    /* We need to setup a security descriptor to allow other processes modify access to the seamless notification event semaphore. */
    SECURITY_ATTRIBUTES     SecAttr;
    DWORD                   dwErr = ERROR_SUCCESS;
    char                    secDesc[SECURITY_DESCRIPTOR_MIN_LENGTH];
    BOOL                    fRC;

    SecAttr.nLength              = sizeof(SecAttr);
    SecAttr.bInheritHandle       = FALSE;
    SecAttr.lpSecurityDescriptor = &secDesc;
    InitializeSecurityDescriptor(SecAttr.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    fRC = SetSecurityDescriptorDacl(SecAttr.lpSecurityDescriptor, TRUE, 0, FALSE);
    if (!fRC)
    {
        dwErr = GetLastError();
        Log(("SetSecurityDescriptorDacl failed with last error = %08X\n", dwErr));
    }
    else
    {
        /* For Vista and up we need to change the integrity of the security descriptor, too. */
        if (gMajorVersion >= 6)
        {
            BOOL (WINAPI * pfnConvertStringSecurityDescriptorToSecurityDescriptorA)(LPCSTR StringSecurityDescriptor, DWORD StringSDRevision, PSECURITY_DESCRIPTOR  *SecurityDescriptor, PULONG  SecurityDescriptorSize);
            *(void **)&pfnConvertStringSecurityDescriptorToSecurityDescriptorA =
                RTLdrGetSystemSymbol("advapi32.dll", "ConvertStringSecurityDescriptorToSecurityDescriptorA");
            Log(("pfnConvertStringSecurityDescriptorToSecurityDescriptorA = %x\n", pfnConvertStringSecurityDescriptorToSecurityDescriptorA));
            if (pfnConvertStringSecurityDescriptorToSecurityDescriptorA)
            {
                PSECURITY_DESCRIPTOR    pSD;
                PACL                    pSacl          = NULL;
                BOOL                    fSaclPresent   = FALSE;
                BOOL                    fSaclDefaulted = FALSE;

                fRC = pfnConvertStringSecurityDescriptorToSecurityDescriptorA("S:(ML;;NW;;;LW)", /* this means "low integrity" */
                                                                              SDDL_REVISION_1, &pSD, NULL);
                if (!fRC)
                {
                    dwErr = GetLastError();
                    Log(("ConvertStringSecurityDescriptorToSecurityDescriptorA failed with last error = %08X\n", dwErr));
                }
                else
                {
                    fRC = GetSecurityDescriptorSacl(pSD, &fSaclPresent, &pSacl, &fSaclDefaulted);
                    if (!fRC)
                    {
                        dwErr = GetLastError();
                        Log(("GetSecurityDescriptorSacl failed with last error = %08X\n", dwErr));
                    }
                    else
                    {
                        fRC = SetSecurityDescriptorSacl(SecAttr.lpSecurityDescriptor, TRUE, pSacl, FALSE);
                        if (!fRC)
                        {
                            dwErr = GetLastError();
                            Log(("SetSecurityDescriptorSacl failed with last error = %08X\n", dwErr));
                        }
                    }
                }
            }
        }

        if (   dwErr == ERROR_SUCCESS
            && gMajorVersion >= 5) /* Only for W2K and up ... */
        {
            ghSeamlessWtNotifyEvent = CreateEvent(&SecAttr, FALSE, FALSE, NEMUHOOK_GLOBAL_WT_EVENT_NAME);
            if (ghSeamlessWtNotifyEvent == NULL)
            {
                dwErr = GetLastError();
                Log(("CreateEvent for Seamless failed, last error = %08X\n", dwErr));
            }

            ghSeamlessKmNotifyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            if (ghSeamlessKmNotifyEvent == NULL)
            {
                dwErr = GetLastError();
                Log(("CreateEvent for Seamless failed, last error = %08X\n", dwErr));
            }
        }
    }
    return RTErrConvertFromWin32(dwErr);
}

static void nemuTrayShutdownSeamless(void)
{
    if (ghSeamlessWtNotifyEvent)
    {
        CloseHandle(ghSeamlessWtNotifyEvent);
        ghSeamlessWtNotifyEvent = NULL;
    }

    if (ghSeamlessKmNotifyEvent)
    {
        CloseHandle(ghSeamlessKmNotifyEvent);
        ghSeamlessKmNotifyEvent = NULL;
    }
}

static void NemuTrayCheckDt()
{
    BOOL fOldAllowedState = NemuConsoleIsAllowed();
    if (nemuDtHandleEvent())
    {
        if (!NemuConsoleIsAllowed() != !fOldAllowedState)
            NemuConsoleEnable(!fOldAllowedState);
    }
}

static int nemuTrayServiceMain(void)
{
    int rc = VINF_SUCCESS;
    Log(("Entering nemuTrayServiceMain\n"));

    ghStopSem = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ghStopSem == NULL)
    {
        rc = RTErrConvertFromWin32(GetLastError());
        Log(("CreateEvent for stopping NemuTray failed, rc=%Rrc\n", rc));
    }
    else
    {
        /*
         * Start services listed in the nemuServiceTable.
         */
        NEMUSERVICEENV svcEnv;
        svcEnv.hInstance = ghInstance;
        svcEnv.hDriver   = ghNemuDriver;

        /* Initializes disp-if to default (XPDM) mode. */
        NemuDispIfInit(&svcEnv.dispIf); /* Cannot fail atm. */
    #ifdef NEMU_WITH_WDDM
        /*
         * For now the display mode will be adjusted to WDDM mode if needed
         * on display service initialization when it detects the display driver type.
         */
    #endif

        /* Finally start all the built-in services! */
        rc = nemuTrayStartServices(&svcEnv, nemuServiceTable);
        if (RT_FAILURE(rc))
        {
            /* Terminate service if something went wrong. */
            nemuTrayStopServices(&svcEnv, nemuServiceTable);
        }
        else
        {
            rc = nemuTrayCreateTrayIcon();
            if (   RT_SUCCESS(rc)
                && gMajorVersion >= 5) /* Only for W2K and up ... */
            {
                /* We're ready to create the tooltip balloon.
                   Check in 10 seconds (@todo make seconds configurable) ... */
                SetTimer(ghwndToolWindow,
                         TIMERID_NEMUTRAY_CHECK_HOSTVERSION,
                         10 * 1000, /* 10 seconds */
                         NULL       /* No timerproc */);
            }

            if (RT_SUCCESS(rc))
            {
                /* Do the Shared Folders auto-mounting stuff. */
                rc = NemuSharedFoldersAutoMount();
                if (RT_SUCCESS(rc))
                {
                    /* Report the host that we're up and running! */
                    hlpReportStatus(NemuGuestFacilityStatus_Active);
                }
            }

            if (RT_SUCCESS(rc))
            {
                /* Boost thread priority to make sure we wake up early for seamless window notifications
                 * (not sure if it actually makes any difference though). */
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

                /*
                 * Main execution loop
                 * Wait for the stop semaphore to be posted or a window event to arrive
                 */

                HANDLE hWaitEvent[4] = {0};
                DWORD dwEventCount = 0;

                hWaitEvent[dwEventCount++] = ghStopSem;

                /* Check if seamless mode is not active and add seamless event to the list */
                if (0 != ghSeamlessWtNotifyEvent)
                {
                    hWaitEvent[dwEventCount++] = ghSeamlessWtNotifyEvent;
                }

                if (0 != ghSeamlessKmNotifyEvent)
                {
                    hWaitEvent[dwEventCount++] = ghSeamlessKmNotifyEvent;
                }

                if (0 != nemuDtGetNotifyEvent())
                {
                    hWaitEvent[dwEventCount++] = nemuDtGetNotifyEvent();
                }

                Log(("Number of events to wait in main loop: %ld\n", dwEventCount));
                while (true)
                {
                    DWORD waitResult = MsgWaitForMultipleObjectsEx(dwEventCount, hWaitEvent, 500, QS_ALLINPUT, 0);
                    waitResult = waitResult - WAIT_OBJECT_0;

                    /* Only enable for message debugging, lots of traffic! */
                    //Log(("Wait result  = %ld\n", waitResult));

                    if (waitResult == 0)
                    {
                        Log(("Event 'Exit' triggered\n"));
                        /* exit */
                        break;
                    }
                    else
                    {
                        BOOL fHandled = FALSE;
                        if (waitResult < RT_ELEMENTS(hWaitEvent))
                        {
                            if (hWaitEvent[waitResult])
                            {
                                if (hWaitEvent[waitResult] == ghSeamlessWtNotifyEvent)
                                {
                                    Log(("Event 'Seamless' triggered\n"));

                                    /* seamless window notification */
                                    NemuSeamlessCheckWindows(false);
                                    fHandled = TRUE;
                                }
                                else if (hWaitEvent[waitResult] == ghSeamlessKmNotifyEvent)
                                {
                                    Log(("Event 'Km Seamless' triggered\n"));

                                    /* seamless window notification */
                                    NemuSeamlessCheckWindows(true);
                                    fHandled = TRUE;
                                }
                                else if (hWaitEvent[waitResult] == nemuDtGetNotifyEvent())
                                {
                                    Log(("Event 'Dt' triggered\n"));
                                    NemuTrayCheckDt();
                                    fHandled = TRUE;
                                }
                            }
                        }

                        if (!fHandled)
                        {
                            /* timeout or a window message, handle it */
                            MSG msg;
                            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                            {
#ifndef DEBUG_andy
                                Log(("msg %p\n", msg.message));
#endif
                                if (msg.message == WM_QUIT)
                                {
                                    Log(("WM_QUIT!\n"));
                                    SetEvent(ghStopSem);
                                }
                                TranslateMessage(&msg);
                                DispatchMessage(&msg);
                            }
                        }
                    }
                }
                Log(("Returned from main loop, exiting ...\n"));
            }
            Log(("Waiting for services to stop ...\n"));
            nemuTrayStopServices(&svcEnv, nemuServiceTable);
        } /* Services started */
        CloseHandle(ghStopSem);
    } /* Stop event created */

    nemuTrayRemoveTrayIcon();

    Log(("Leaving nemuTrayServiceMain with rc=%Rrc\n", rc));
    return rc;
}

/**
 * Main function
 */
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    /* Note: Do not use a global namespace ("Global\\") for mutex name here,
     * will blow up NT4 compatibility! */
    HANDLE hMutexAppRunning = CreateMutex(NULL, FALSE, "NemuTray");
    if (   hMutexAppRunning != NULL
        && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        /* NemuTray already running? Bail out. */
        CloseHandle (hMutexAppRunning);
        hMutexAppRunning = NULL;
        return 0;
    }

    LogRel(("%s r%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr()));

    int rc = RTR3InitExeNoArguments(0);
    if (RT_SUCCESS(rc))
        rc = nemuTrayLogCreate(NULL /* pszLogFile */);

    if (RT_SUCCESS(rc))
    {
        rc = VbglR3Init();
        if (RT_SUCCESS(rc))
            rc = nemuTrayOpenBaseDriver();
    }

    if (RT_SUCCESS(rc))
    {
        /* Save instance handle. */
        ghInstance = hInstance;

        hlpReportStatus(NemuGuestFacilityStatus_Init);
        rc = nemuTrayCreateToolWindow();
        if (RT_SUCCESS(rc))
        {
            NemuCapsInit();

            rc = nemuStInit(ghwndToolWindow);
            if (!RT_SUCCESS(rc))
            {
                LogFlowFunc(("nemuStInit failed, rc %d\n"));
                /* ignore the St Init failure. this can happen for < XP win that do not support WTS API
                 * in that case the session is treated as active connected to the physical console
                 * (i.e. fallback to the old behavior that was before introduction of NemuSt) */
                Assert(nemuStIsActiveConsole());
            }

            rc = nemuDtInit();
            if (!RT_SUCCESS(rc))
            {
                LogFlowFunc(("nemuDtInit failed, rc %d\n"));
                /* ignore the Dt Init failure. this can happen for < XP win that do not support WTS API
                 * in that case the session is treated as active connected to the physical console
                 * (i.e. fallback to the old behavior that was before introduction of NemuSt) */
                Assert(nemuDtIsInputDesktop());
            }

            rc = NemuAcquireGuestCaps(VMMDEV_GUEST_SUPPORTS_SEAMLESS | VMMDEV_GUEST_SUPPORTS_GRAPHICS, 0, true);
            if (!RT_SUCCESS(rc))
            {
                LogFlowFunc(("NemuAcquireGuestCaps cfg failed rc %d, ignoring..\n", rc));
            }

            rc = nemuTraySetupSeamless();
            if (RT_SUCCESS(rc))
            {
                Log(("Init successful\n"));
                rc = nemuTrayServiceMain();
                if (RT_SUCCESS(rc))
                    hlpReportStatus(NemuGuestFacilityStatus_Terminating);
                nemuTrayShutdownSeamless();
            }

            /* it should be safe to call nemuDtTerm even if nemuStInit above failed */
            nemuDtTerm();

            /* it should be safe to call nemuStTerm even if nemuStInit above failed */
            nemuStTerm();

            NemuCapsTerm();

            nemuTrayDestroyToolWindow();
        }
        if (RT_SUCCESS(rc))
            hlpReportStatus(NemuGuestFacilityStatus_Terminated);
    }

    if (RT_FAILURE(rc))
    {
        LogRel(("Error while starting, rc=%Rrc\n", rc));
        hlpReportStatus(NemuGuestFacilityStatus_Failed);
    }
    LogRel(("Ended\n"));
    nemuTrayCloseBaseDriver();

    /* Release instance mutex. */
    if (hMutexAppRunning != NULL)
    {
        CloseHandle(hMutexAppRunning);
        hMutexAppRunning = NULL;
    }

    VbglR3Term();

    nemuTrayLogDestroy();

    return RT_SUCCESS(rc) ? 0 : 1;
}

/**
 * Window procedure for our tool window
 */
static LRESULT CALLBACK nemuToolWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
        {
            Log(("Tool window created\n"));

            int rc = nemuTrayRegisterGlobalMessages(&s_nemuGlobalMessageTable[0]);
            if (RT_FAILURE(rc))
                Log(("Error registering global window messages, rc=%Rrc\n", rc));
            return 0;
        }

        case WM_CLOSE:
            return 0;

        case WM_DESTROY:
            Log(("Tool window destroyed\n"));
            KillTimer(ghwndToolWindow, TIMERID_NEMUTRAY_CHECK_HOSTVERSION);
            return 0;

        case WM_TIMER:
            if (NemuCapsCheckTimer(wParam))
                return 0;
            if (nemuDtCheckTimer(wParam))
                return 0;
            if (nemuStCheckTimer(wParam))
                return 0;

            switch (wParam)
            {
                case TIMERID_NEMUTRAY_CHECK_HOSTVERSION:
                    if (RT_SUCCESS(NemuCheckHostVersion()))
                    {
                        /* After successful run we don't need to check again. */
                        KillTimer(ghwndToolWindow, TIMERID_NEMUTRAY_CHECK_HOSTVERSION);
                    }
                    return 0;

                default:
                    break;
            }
            break; /* Make sure other timers get processed the usual way! */

        case WM_NEMUTRAY_TRAY_ICON:
            switch (lParam)
            {
                case WM_LBUTTONDBLCLK:
                    break;

                case WM_RBUTTONDOWN:
                    break;
            }
            return 0;

        case WM_NEMU_SEAMLESS_ENABLE:
            NemuCapsEntryFuncStateSet(NEMUCAPS_ENTRY_IDX_SEAMLESS, NEMUCAPS_ENTRY_FUNCSTATE_STARTED);
            return 0;

        case WM_NEMU_SEAMLESS_DISABLE:
            NemuCapsEntryFuncStateSet(NEMUCAPS_ENTRY_IDX_SEAMLESS, NEMUCAPS_ENTRY_FUNCSTATE_SUPPORTED);
            return 0;

        case WM_DISPLAYCHANGE:
        case WM_NEMU_SEAMLESS_UPDATE:
            if (NemuCapsEntryIsEnabled(NEMUCAPS_ENTRY_IDX_SEAMLESS))
                NemuSeamlessCheckWindows(true);
            return 0;

        case WM_NEMU_GRAPHICS_SUPPORTED:
            NemuGrapicsSetSupported(TRUE);
            return 0;

        case WM_NEMU_GRAPHICS_UNSUPPORTED:
            NemuGrapicsSetSupported(FALSE);
            return 0;

        case WM_WTSSESSION_CHANGE:
        {
            BOOL fOldAllowedState = NemuConsoleIsAllowed();
            if (nemuStHandleEvent(wParam, lParam))
            {
                if (!NemuConsoleIsAllowed() != !fOldAllowedState)
                    NemuConsoleEnable(!fOldAllowedState);
            }
            return 0;
        }
        default:

            /* Handle all globally registered window messages. */
            if (nemuTrayHandleGlobalMessages(&s_nemuGlobalMessageTable[0], uMsg,
                                             wParam, lParam))
            {
                return 0; /* We handled the message. @todo Add return value!*/
            }
            break; /* We did not handle the message, dispatch to DefWndProc. */
    }

    /* Only if message was *not* handled by our switch above, dispatch
     * to DefWindowProc. */
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

/* St (session [state] tracking) functionality API impl */

typedef struct NEMUST
{
    HWND hWTSAPIWnd;
    RTLDRMOD hLdrModWTSAPI32;
    BOOL fIsConsole;
    WTS_CONNECTSTATE_CLASS enmConnectState;
    UINT_PTR idDelayedInitTimer;
    BOOL (WINAPI * pfnWTSRegisterSessionNotification)(HWND hWnd, DWORD dwFlags);
    BOOL (WINAPI * pfnWTSUnRegisterSessionNotification)(HWND hWnd);
    BOOL (WINAPI * pfnWTSQuerySessionInformationA)(HANDLE hServer, DWORD SessionId, WTS_INFO_CLASS WTSInfoClass, LPTSTR *ppBuffer, DWORD *pBytesReturned);
} NEMUST;

static NEMUST gNemuSt;

static int nemuStCheckState()
{
    int rc = VINF_SUCCESS;
    WTS_CONNECTSTATE_CLASS *penmConnectState = NULL;
    USHORT *pProtocolType = NULL;
    DWORD cbBuf = 0;
    if (gNemuSt.pfnWTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSConnectState,
                                               (LPTSTR *)&penmConnectState, &cbBuf))
    {
        if (gNemuSt.pfnWTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSClientProtocolType,
                                                   (LPTSTR *)&pProtocolType, &cbBuf))
        {
            gNemuSt.fIsConsole = (*pProtocolType == 0);
            gNemuSt.enmConnectState = *penmConnectState;
            return VINF_SUCCESS;
        }

        DWORD dwErr = GetLastError();
        LogFlowFunc(("WTSQuerySessionInformationA WTSClientProtocolType failed, error = %08X\n", dwErr));
        rc = RTErrConvertFromWin32(dwErr);
    }
    else
    {
        DWORD dwErr = GetLastError();
        LogFlowFunc(("WTSQuerySessionInformationA WTSConnectState failed, error = %08X\n", dwErr));
        rc = RTErrConvertFromWin32(dwErr);
    }

    /* failure branch, set to "console-active" state */
    gNemuSt.fIsConsole = TRUE;
    gNemuSt.enmConnectState = WTSActive;

    return rc;
}

static int nemuStInit(HWND hWnd)
{
    RT_ZERO(gNemuSt);
    int rc = RTLdrLoadSystem("WTSAPI32.DLL", false /*fNoUnload*/, &gNemuSt.hLdrModWTSAPI32);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(gNemuSt.hLdrModWTSAPI32, "WTSRegisterSessionNotification",
                            (void **)&gNemuSt.pfnWTSRegisterSessionNotification);
        if (RT_SUCCESS(rc))
        {
            rc = RTLdrGetSymbol(gNemuSt.hLdrModWTSAPI32, "WTSUnRegisterSessionNotification",
                                (void **)&gNemuSt.pfnWTSUnRegisterSessionNotification);
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrGetSymbol(gNemuSt.hLdrModWTSAPI32, "WTSQuerySessionInformationA",
                                    (void **)&gNemuSt.pfnWTSQuerySessionInformationA);
                if (RT_FAILURE(rc))
                    LogFlowFunc(("WTSQuerySessionInformationA not found\n"));
            }
            else
                LogFlowFunc(("WTSUnRegisterSessionNotification not found\n"));
        }
        else
            LogFlowFunc(("WTSRegisterSessionNotification not found\n"));
        if (RT_SUCCESS(rc))
        {
            gNemuSt.hWTSAPIWnd = hWnd;
            if (gNemuSt.pfnWTSRegisterSessionNotification(gNemuSt.hWTSAPIWnd, NOTIFY_FOR_THIS_SESSION))
                nemuStCheckState();
            else
            {
                DWORD dwErr = GetLastError();
                LogFlowFunc(("WTSRegisterSessionNotification failed, error = %08X\n", dwErr));
                if (dwErr == RPC_S_INVALID_BINDING)
                {
                    gNemuSt.idDelayedInitTimer = SetTimer(gNemuSt.hWTSAPIWnd, TIMERID_NEMUTRAY_ST_DELAYED_INIT_TIMER,
                                                          2000, (TIMERPROC)NULL);
                    gNemuSt.fIsConsole = TRUE;
                    gNemuSt.enmConnectState = WTSActive;
                    rc = VINF_SUCCESS;
                }
                else
                    rc = RTErrConvertFromWin32(dwErr);
            }

            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
        }

        RTLdrClose(gNemuSt.hLdrModWTSAPI32);
    }
    else
        LogFlowFunc(("WTSAPI32 load failed, rc = %Rrc\n", rc));

    RT_ZERO(gNemuSt);
    gNemuSt.fIsConsole = TRUE;
    gNemuSt.enmConnectState = WTSActive;
    return rc;
}

static void nemuStTerm(void)
{
    if (!gNemuSt.hWTSAPIWnd)
    {
        LogFlowFunc(("nemuStTerm called for non-initialized St\n"));
        return;
    }

    if (gNemuSt.idDelayedInitTimer)
    {
        /* notification is not registered, just kill timer */
        KillTimer(gNemuSt.hWTSAPIWnd, gNemuSt.idDelayedInitTimer);
        gNemuSt.idDelayedInitTimer = 0;
    }
    else
    {
        if (!gNemuSt.pfnWTSUnRegisterSessionNotification(gNemuSt.hWTSAPIWnd))
        {
            DWORD dwErr = GetLastError();
            LogFlowFunc(("WTSAPI32 load failed, error = %08X\n", dwErr));
        }
    }

    RTLdrClose(gNemuSt.hLdrModWTSAPI32);
    RT_ZERO(gNemuSt);
}

#define NEMUST_DBG_MAKECASE(_val) case _val: return #_val;

static const char* nemuStDbgGetString(DWORD val)
{
    switch (val)
    {
        NEMUST_DBG_MAKECASE(WTS_CONSOLE_CONNECT);
        NEMUST_DBG_MAKECASE(WTS_CONSOLE_DISCONNECT);
        NEMUST_DBG_MAKECASE(WTS_REMOTE_CONNECT);
        NEMUST_DBG_MAKECASE(WTS_REMOTE_DISCONNECT);
        NEMUST_DBG_MAKECASE(WTS_SESSION_LOGON);
        NEMUST_DBG_MAKECASE(WTS_SESSION_LOGOFF);
        NEMUST_DBG_MAKECASE(WTS_SESSION_LOCK);
        NEMUST_DBG_MAKECASE(WTS_SESSION_UNLOCK);
        NEMUST_DBG_MAKECASE(WTS_SESSION_REMOTE_CONTROL);
        default:
            LogFlowFunc(("invalid WTS state %d\n", val));
            return "Unknown";
    }
}

static BOOL nemuStCheckTimer(WPARAM wEvent)
{
    if (wEvent != gNemuSt.idDelayedInitTimer)
        return FALSE;

    if (gNemuSt.pfnWTSRegisterSessionNotification(gNemuSt.hWTSAPIWnd, NOTIFY_FOR_THIS_SESSION))
    {
        KillTimer(gNemuSt.hWTSAPIWnd, gNemuSt.idDelayedInitTimer);
        gNemuSt.idDelayedInitTimer = 0;
        nemuStCheckState();
    }
    else
    {
        DWORD dwErr = GetLastError();
        LogFlowFunc(("timer WTSRegisterSessionNotification failed, error = %08X\n", dwErr));
        Assert(gNemuSt.fIsConsole == TRUE);
        Assert(gNemuSt.enmConnectState == WTSActive);
    }

    return TRUE;
}


static BOOL nemuStHandleEvent(WPARAM wEvent, LPARAM SessionID)
{
    LogFlowFunc(("WTS Event: %s\n", nemuStDbgGetString(wEvent)));
    BOOL fOldIsActiveConsole = nemuStIsActiveConsole();

    nemuStCheckState();

    return !nemuStIsActiveConsole() != !fOldIsActiveConsole;
}

static BOOL nemuStIsActiveConsole()
{
    return (gNemuSt.enmConnectState == WTSActive && gNemuSt.fIsConsole);
}

/*
 * Dt (desktop [state] tracking) functionality API impl
 *
 * !!!NOTE: this API is NOT thread-safe!!!
 * */

typedef struct NEMUDT
{
    HANDLE hNotifyEvent;
    BOOL fIsInputDesktop;
    UINT_PTR idTimer;
    RTLDRMOD hLdrModHook;
    BOOL (* pfnNemuHookInstallActiveDesktopTracker)(HMODULE hDll);
    BOOL (* pfnNemuHookRemoveActiveDesktopTracker)();
    HDESK (WINAPI * pfnGetThreadDesktop)(DWORD dwThreadId);
    HDESK (WINAPI * pfnOpenInputDesktop)(DWORD dwFlags, BOOL fInherit, ACCESS_MASK dwDesiredAccess);
    BOOL (WINAPI * pfnCloseDesktop)(HDESK hDesktop);
} NEMUDT;

static NEMUDT gNemuDt;

static BOOL nemuDtCalculateIsInputDesktop()
{
    BOOL fIsInputDt = FALSE;
    HDESK hInput = gNemuDt.pfnOpenInputDesktop(0, FALSE, DESKTOP_CREATEWINDOW);
    if (hInput)
    {
//        DWORD dwThreadId = GetCurrentThreadId();
//        HDESK hThreadDt = gNemuDt.pfnGetThreadDesktop(dwThreadId);
//        if (hThreadDt)
//        {
            fIsInputDt = TRUE;
//        }
//        else
//        {
//            DWORD dwErr = GetLastError();
//            LogFlowFunc(("pfnGetThreadDesktop for Seamless failed, last error = %08X\n", dwErr));
//        }

        gNemuDt.pfnCloseDesktop(hInput);
    }
    else
    {
        DWORD dwErr = GetLastError();
//        LogFlowFunc(("pfnOpenInputDesktop for Seamless failed, last error = %08X\n", dwErr));
    }
    return fIsInputDt;
}

static BOOL nemuDtCheckTimer(WPARAM wParam)
{
    if (wParam != gNemuDt.idTimer)
        return FALSE;

    NemuTrayCheckDt();

    return TRUE;
}

static int nemuDtInit()
{
    int rc = VINF_SUCCESS;
    OSVERSIONINFO info;
    gMajorVersion = 5; /* Default to Windows XP. */
    info.dwOSVersionInfoSize = sizeof(info);
    if (GetVersionEx(&info))
    {
        LogRel(("Windows version %ld.%ld\n", info.dwMajorVersion, info.dwMinorVersion));
        gMajorVersion = info.dwMajorVersion;
    }

    RT_ZERO(gNemuDt);

    gNemuDt.hNotifyEvent = CreateEvent(NULL, FALSE, FALSE, NEMUHOOK_GLOBAL_DT_EVENT_NAME);
    if (gNemuDt.hNotifyEvent != NULL)
    {
        /* Load the hook dll and resolve the necessary entry points. */
        rc = RTLdrLoadAppPriv(NEMUHOOK_DLL_NAME, &gNemuDt.hLdrModHook);
        if (RT_SUCCESS(rc))
        {
            rc = RTLdrGetSymbol(gNemuDt.hLdrModHook, "NemuHookInstallActiveDesktopTracker",
                                (void **)&gNemuDt.pfnNemuHookInstallActiveDesktopTracker);
            if (RT_SUCCESS(rc))
            {
                rc = RTLdrGetSymbol(gNemuDt.hLdrModHook, "NemuHookRemoveActiveDesktopTracker",
                                    (void **)&gNemuDt.pfnNemuHookRemoveActiveDesktopTracker);
                if (RT_FAILURE(rc))
                    LogFlowFunc(("NemuHookRemoveActiveDesktopTracker not found\n"));
            }
            else
                LogFlowFunc(("NemuHookInstallActiveDesktopTracker not found\n"));
            if (RT_SUCCESS(rc))
            {
                /* Try get the system APIs we need. */
                *(void **)&gNemuDt.pfnGetThreadDesktop = RTLdrGetSystemSymbol("user32.dll", "GetThreadDesktop");
                if (!gNemuDt.pfnGetThreadDesktop)
                {
                    LogFlowFunc(("GetThreadDesktop not found\n"));
                    rc = VERR_NOT_SUPPORTED;
                }

                *(void **)&gNemuDt.pfnOpenInputDesktop = RTLdrGetSystemSymbol("user32.dll", "OpenInputDesktop");
                if (!gNemuDt.pfnOpenInputDesktop)
                {
                    LogFlowFunc(("OpenInputDesktop not found\n"));
                    rc = VERR_NOT_SUPPORTED;
                }

                *(void **)&gNemuDt.pfnCloseDesktop = RTLdrGetSystemSymbol("user32.dll", "CloseDesktop");
                if (!gNemuDt.pfnCloseDesktop)
                {
                    LogFlowFunc(("CloseDesktop not found\n"));
                    rc = VERR_NOT_SUPPORTED;
                }

                if (RT_SUCCESS(rc))
                {
                    BOOL fRc = FALSE;
                    /* For Vista and up we need to change the integrity of the security descriptor, too. */
                    if (gMajorVersion >= 6)
                    {
                        HMODULE hModHook = (HMODULE)RTLdrGetNativeHandle(gNemuDt.hLdrModHook);
                        Assert((uintptr_t)hModHook != ~(uintptr_t)0);
                        fRc = gNemuDt.pfnNemuHookInstallActiveDesktopTracker(hModHook);
                        if (!fRc)
                        {
                            DWORD dwErr = GetLastError();
                            LogFlowFunc(("pfnNemuHookInstallActiveDesktopTracker failed, last error = %08X\n", dwErr));
                        }
                    }

                    if (!fRc)
                    {
                        gNemuDt.idTimer = SetTimer(ghwndToolWindow, TIMERID_NEMUTRAY_DT_TIMER, 500, (TIMERPROC)NULL);
                        if (!gNemuDt.idTimer)
                        {
                            DWORD dwErr = GetLastError();
                            LogFlowFunc(("SetTimer error %08X\n", dwErr));
                            rc = RTErrConvertFromWin32(dwErr);
                        }
                    }

                    if (RT_SUCCESS(rc))
                    {
                        gNemuDt.fIsInputDesktop = nemuDtCalculateIsInputDesktop();
                        return VINF_SUCCESS;
                    }
                }
            }

            RTLdrClose(gNemuDt.hLdrModHook);
        }
        else
        {
            DWORD dwErr = GetLastError();
            LogFlowFunc(("CreateEvent for Seamless failed, last error = %08X\n", dwErr));
            rc = RTErrConvertFromWin32(dwErr);
        }

        CloseHandle(gNemuDt.hNotifyEvent);
    }
    else
    {
        DWORD dwErr = GetLastError();
        LogFlowFunc(("CreateEvent for Seamless failed, last error = %08X\n", dwErr));
        rc = RTErrConvertFromWin32(dwErr);
    }


    RT_ZERO(gNemuDt);
    gNemuDt.fIsInputDesktop = TRUE;

    return rc;
}

static void nemuDtTerm()
{
    if (!gNemuDt.hLdrModHook)
        return;

    gNemuDt.pfnNemuHookRemoveActiveDesktopTracker();

    RTLdrClose(gNemuDt.hLdrModHook);
    CloseHandle(gNemuDt.hNotifyEvent);

    RT_ZERO(gNemuDt);
}
/* @returns true on "IsInputDesktop" state change */
static BOOL nemuDtHandleEvent()
{
    BOOL fIsInputDesktop = gNemuDt.fIsInputDesktop;
    gNemuDt.fIsInputDesktop = nemuDtCalculateIsInputDesktop();
    return !fIsInputDesktop != !gNemuDt.fIsInputDesktop;
}

static HANDLE nemuDtGetNotifyEvent()
{
    return gNemuDt.hNotifyEvent;
}

/* @returns true iff the application (NemuTray) desktop is input */
static BOOL nemuDtIsInputDesktop()
{
    return gNemuDt.fIsInputDesktop;
}


/* we need to perform Acquire/Release using the file handled we use for rewuesting events from NemuGuest
 * otherwise Acquisition mechanism will treat us as different client and will not propagate necessary requests
 * */
static int NemuAcquireGuestCaps(uint32_t fOr, uint32_t fNot, bool fCfg)
{
    DWORD cbReturned = 0;
    NemuGuestCapsAquire Info;
    Log(("NemuAcquireGuestCaps or(0x%x), not(0x%x), cfx(%d)\n", fOr, fNot, fCfg));
    Info.enmFlags = fCfg ? NEMUGUESTCAPSACQUIRE_FLAGS_CONFIG_ACQUIRE_MODE : NEMUGUESTCAPSACQUIRE_FLAGS_NONE;
    Info.rc = VERR_NOT_IMPLEMENTED;
    Info.u32OrMask = fOr;
    Info.u32NotMask = fNot;
    if (!DeviceIoControl(ghNemuDriver, NEMUGUEST_IOCTL_GUEST_CAPS_ACQUIRE, &Info, sizeof(Info), &Info, sizeof(Info), &cbReturned, NULL))
    {
        DWORD LastErr = GetLastError();
        LogFlowFunc(("DeviceIoControl NEMUGUEST_IOCTL_GUEST_CAPS_ACQUIRE failed LastErr %d\n", LastErr));
        return RTErrConvertFromWin32(LastErr);
    }

    int rc = Info.rc;
    if (!RT_SUCCESS(rc))
    {
        LogFlowFunc(("NEMUGUEST_IOCTL_GUEST_CAPS_ACQUIRE failed rc %d\n", rc));
        return rc;
    }

    return rc;
}

typedef enum NEMUCAPS_ENTRY_ACSTATE
{
    /* the given cap is released */
    NEMUCAPS_ENTRY_ACSTATE_RELEASED = 0,
    /* the given cap acquisition is in progress */
    NEMUCAPS_ENTRY_ACSTATE_ACQUIRING,
    /* the given cap is acquired */
    NEMUCAPS_ENTRY_ACSTATE_ACQUIRED
} NEMUCAPS_ENTRY_ACSTATE;


struct NEMUCAPS_ENTRY;
struct NEMUCAPS;

typedef DECLCALLBACKPTR(void, PFNNEMUCAPS_ENTRY_ON_ENABLE)(struct NEMUCAPS *pConsole, struct NEMUCAPS_ENTRY *pCap, BOOL fEnabled);

typedef struct NEMUCAPS_ENTRY
{
    uint32_t fCap;
    uint32_t iCap;
    NEMUCAPS_ENTRY_FUNCSTATE enmFuncState;
    NEMUCAPS_ENTRY_ACSTATE enmAcState;
    PFNNEMUCAPS_ENTRY_ON_ENABLE pfnOnEnable;
} NEMUCAPS_ENTRY;


typedef struct NEMUCAPS
{
    UINT_PTR idTimer;
    NEMUCAPS_ENTRY aCaps[NEMUCAPS_ENTRY_IDX_COUNT];
} NEMUCAPS;

static NEMUCAPS gNemuCaps;

static DECLCALLBACK(void) nemuCapsOnEnableSeamles(struct NEMUCAPS *pConsole, struct NEMUCAPS_ENTRY *pCap, BOOL fEnabled)
{
    if (fEnabled)
    {
        Log(("nemuCapsOnEnableSeamles: ENABLED\n"));
        Assert(pCap->enmAcState == NEMUCAPS_ENTRY_ACSTATE_ACQUIRED);
        Assert(pCap->enmFuncState == NEMUCAPS_ENTRY_FUNCSTATE_STARTED);
        NemuSeamlessEnable();
    }
    else
    {
        Log(("nemuCapsOnEnableSeamles: DISABLED\n"));
        Assert(pCap->enmAcState != NEMUCAPS_ENTRY_ACSTATE_ACQUIRED || pCap->enmFuncState != NEMUCAPS_ENTRY_FUNCSTATE_STARTED);
        NemuSeamlessDisable();
    }
}

static void nemuCapsEntryAcStateSet(NEMUCAPS_ENTRY *pCap, NEMUCAPS_ENTRY_ACSTATE enmAcState)
{
    NEMUCAPS *pConsole = &gNemuCaps;

    Log(("nemuCapsEntryAcStateSet: new state enmAcState(%d); pCap: fCap(%d), iCap(%d), enmFuncState(%d), enmAcState(%d)\n",
            enmAcState, pCap->fCap, pCap->iCap, pCap->enmFuncState, pCap->enmAcState));

    if (pCap->enmAcState == enmAcState)
        return;

    NEMUCAPS_ENTRY_ACSTATE enmOldAcState = pCap->enmAcState;
    pCap->enmAcState = enmAcState;

    if (enmAcState == NEMUCAPS_ENTRY_ACSTATE_ACQUIRED)
    {
        if (pCap->enmFuncState == NEMUCAPS_ENTRY_FUNCSTATE_STARTED)
        {
            if (pCap->pfnOnEnable)
                pCap->pfnOnEnable(pConsole, pCap, TRUE);
        }
    }
    else if (enmOldAcState == NEMUCAPS_ENTRY_ACSTATE_ACQUIRED && pCap->enmFuncState == NEMUCAPS_ENTRY_FUNCSTATE_STARTED)
    {
        if (pCap->pfnOnEnable)
            pCap->pfnOnEnable(pConsole, pCap, FALSE);
    }
}

static void nemuCapsEntryFuncStateSet(NEMUCAPS_ENTRY *pCap, NEMUCAPS_ENTRY_FUNCSTATE enmFuncState)
{
    NEMUCAPS *pConsole = &gNemuCaps;

    Log(("nemuCapsEntryFuncStateSet: new state enmAcState(%d); pCap: fCap(%d), iCap(%d), enmFuncState(%d), enmAcState(%d)\n",
            enmFuncState, pCap->fCap, pCap->iCap, pCap->enmFuncState, pCap->enmAcState));

    if (pCap->enmFuncState == enmFuncState)
        return;

    NEMUCAPS_ENTRY_FUNCSTATE enmOldFuncState = pCap->enmFuncState;

    pCap->enmFuncState = enmFuncState;

    if (enmFuncState == NEMUCAPS_ENTRY_FUNCSTATE_STARTED)
    {
        Assert(enmOldFuncState == NEMUCAPS_ENTRY_FUNCSTATE_SUPPORTED);
        if (pCap->enmAcState == NEMUCAPS_ENTRY_ACSTATE_ACQUIRED)
        {
            if (pCap->pfnOnEnable)
                pCap->pfnOnEnable(pConsole, pCap, TRUE);
        }
    }
    else if (pCap->enmAcState == NEMUCAPS_ENTRY_ACSTATE_ACQUIRED && enmOldFuncState == NEMUCAPS_ENTRY_FUNCSTATE_STARTED)
    {
        if (pCap->pfnOnEnable)
            pCap->pfnOnEnable(pConsole, pCap, FALSE);
    }
}

static void NemuCapsEntryFuncStateSet(uint32_t iCup, NEMUCAPS_ENTRY_FUNCSTATE enmFuncState)
{
    NEMUCAPS *pConsole = &gNemuCaps;
    NEMUCAPS_ENTRY *pCap = &pConsole->aCaps[iCup];
    nemuCapsEntryFuncStateSet(pCap, enmFuncState);
}

static int NemuCapsInit()
{
    NEMUCAPS *pConsole = &gNemuCaps;
    memset(pConsole, 0, sizeof (*pConsole));
    pConsole->aCaps[NEMUCAPS_ENTRY_IDX_SEAMLESS].fCap = VMMDEV_GUEST_SUPPORTS_SEAMLESS;
    pConsole->aCaps[NEMUCAPS_ENTRY_IDX_SEAMLESS].iCap = NEMUCAPS_ENTRY_IDX_SEAMLESS;
    pConsole->aCaps[NEMUCAPS_ENTRY_IDX_SEAMLESS].pfnOnEnable = nemuCapsOnEnableSeamles;
    pConsole->aCaps[NEMUCAPS_ENTRY_IDX_GRAPHICS].fCap = VMMDEV_GUEST_SUPPORTS_GRAPHICS;
    pConsole->aCaps[NEMUCAPS_ENTRY_IDX_GRAPHICS].iCap = NEMUCAPS_ENTRY_IDX_GRAPHICS;
    return VINF_SUCCESS;
}

static int NemuCapsReleaseAll()
{
    NEMUCAPS *pConsole = &gNemuCaps;
    Log(("NemuCapsReleaseAll\n"));
    int rc = NemuAcquireGuestCaps(0, VMMDEV_GUEST_SUPPORTS_SEAMLESS | VMMDEV_GUEST_SUPPORTS_GRAPHICS, false);
    if (!RT_SUCCESS(rc))
    {
        LogFlowFunc(("nemuCapsEntryReleaseAll NemuAcquireGuestCaps failed rc %d\n", rc));
        return rc;
    }

    if (pConsole->idTimer)
    {
        Log(("killing console timer\n"));
        KillTimer(ghwndToolWindow, pConsole->idTimer);
        pConsole->idTimer = 0;
    }

    for (int i = 0; i < RT_ELEMENTS(pConsole->aCaps); ++i)
    {
        nemuCapsEntryAcStateSet(&pConsole->aCaps[i], NEMUCAPS_ENTRY_ACSTATE_RELEASED);
    }

    return rc;
}

static void NemuCapsTerm()
{
    NEMUCAPS *pConsole = &gNemuCaps;
    NemuCapsReleaseAll();
    memset(pConsole, 0, sizeof (*pConsole));
}

static BOOL NemuCapsEntryIsAcquired(uint32_t iCap)
{
    NEMUCAPS *pConsole = &gNemuCaps;
    return pConsole->aCaps[iCap].enmAcState == NEMUCAPS_ENTRY_ACSTATE_ACQUIRED;
}

static BOOL NemuCapsEntryIsEnabled(uint32_t iCap)
{
    NEMUCAPS *pConsole = &gNemuCaps;
    return pConsole->aCaps[iCap].enmAcState == NEMUCAPS_ENTRY_ACSTATE_ACQUIRED
            && pConsole->aCaps[iCap].enmFuncState == NEMUCAPS_ENTRY_FUNCSTATE_STARTED;
}

static BOOL NemuCapsCheckTimer(WPARAM wParam)
{
    NEMUCAPS *pConsole = &gNemuCaps;
    if (wParam != pConsole->idTimer)
        return FALSE;

    uint32_t u32AcquiredCaps = 0;
    BOOL fNeedNewTimer = FALSE;

    for (int i = 0; i < RT_ELEMENTS(pConsole->aCaps); ++i)
    {
        NEMUCAPS_ENTRY *pCap = &pConsole->aCaps[i];
        if (pCap->enmAcState != NEMUCAPS_ENTRY_ACSTATE_ACQUIRING)
            continue;

        int rc = NemuAcquireGuestCaps(pCap->fCap, 0, false);
        if (RT_SUCCESS(rc))
        {
            nemuCapsEntryAcStateSet(&pConsole->aCaps[i], NEMUCAPS_ENTRY_ACSTATE_ACQUIRED);
            u32AcquiredCaps |= pCap->fCap;
        }
        else
        {
            Assert(rc == VERR_RESOURCE_BUSY);
            fNeedNewTimer = TRUE;
        }
    }

    if (!fNeedNewTimer)
    {
        KillTimer(ghwndToolWindow, pConsole->idTimer);
        /* cleanup timer data */
        pConsole->idTimer = 0;
    }

    return TRUE;
}

static int NemuCapsEntryRelease(uint32_t iCap)
{
    NEMUCAPS *pConsole = &gNemuCaps;
    NEMUCAPS_ENTRY *pCap = &pConsole->aCaps[iCap];
    if (pCap->enmAcState == NEMUCAPS_ENTRY_ACSTATE_RELEASED)
    {
        LogFlowFunc(("invalid cap[%d] state[%d] on release\n", iCap, pCap->enmAcState));
        return VERR_INVALID_STATE;
    }

    if (pCap->enmAcState == NEMUCAPS_ENTRY_ACSTATE_ACQUIRED)
    {
        int rc = NemuAcquireGuestCaps(0, pCap->fCap, false);
        AssertRC(rc);
    }

    nemuCapsEntryAcStateSet(pCap, NEMUCAPS_ENTRY_ACSTATE_RELEASED);

    return VINF_SUCCESS;
}

static int NemuCapsEntryAcquire(uint32_t iCap)
{
    NEMUCAPS *pConsole = &gNemuCaps;
    Assert(NemuConsoleIsAllowed());
    NEMUCAPS_ENTRY *pCap = &pConsole->aCaps[iCap];
    Log(("NemuCapsEntryAcquire %d\n", iCap));
    if (pCap->enmAcState != NEMUCAPS_ENTRY_ACSTATE_RELEASED)
    {
        LogFlowFunc(("invalid cap[%d] state[%d] on acquire\n", iCap, pCap->enmAcState));
        return VERR_INVALID_STATE;
    }

    nemuCapsEntryAcStateSet(pCap, NEMUCAPS_ENTRY_ACSTATE_ACQUIRING);
    int rc = NemuAcquireGuestCaps(pCap->fCap, 0, false);
    if (RT_SUCCESS(rc))
    {
        nemuCapsEntryAcStateSet(pCap, NEMUCAPS_ENTRY_ACSTATE_ACQUIRED);
        return VINF_SUCCESS;
    }

    if (rc != VERR_RESOURCE_BUSY)
    {
        LogFlowFunc(("nemuCapsEntryReleaseAll NemuAcquireGuestCaps failed rc %d\n", rc));
        return rc;
    }

    LogFlowFunc(("iCap %d is busy!\n", iCap));

    /* the cap was busy, most likely it is still used by other NemuTray instance running in another session,
     * queue the retry timer */
    if (!pConsole->idTimer)
    {
        pConsole->idTimer = SetTimer(ghwndToolWindow, TIMERID_NEMUTRAY_CAPS_TIMER, 100, (TIMERPROC)NULL);
        if (!pConsole->idTimer)
        {
            DWORD dwErr = GetLastError();
            LogFlowFunc(("SetTimer error %08X\n", dwErr));
            return RTErrConvertFromWin32(dwErr);
        }
    }

    return rc;
}

static int NemuCapsAcquireAllSupported()
{
    NEMUCAPS *pConsole = &gNemuCaps;
    Log(("NemuCapsAcquireAllSupported\n"));
    for (int i = 0; i < RT_ELEMENTS(pConsole->aCaps); ++i)
    {
        if (pConsole->aCaps[i].enmFuncState >= NEMUCAPS_ENTRY_FUNCSTATE_SUPPORTED)
        {
            Log(("NemuCapsAcquireAllSupported acquiring cap %d, state %d\n", i, pConsole->aCaps[i].enmFuncState));
            NemuCapsEntryAcquire(i);
        }
        else
        {
            LogFlowFunc(("NemuCapsAcquireAllSupported: WARN: cap %d not supported, state %d\n", i, pConsole->aCaps[i].enmFuncState));
        }
    }
    return VINF_SUCCESS;
}

static BOOL NemuConsoleIsAllowed()
{
    return nemuDtIsInputDesktop() && nemuStIsActiveConsole();
}

static void NemuConsoleEnable(BOOL fEnable)
{
    if (fEnable)
        NemuCapsAcquireAllSupported();
    else
        NemuCapsReleaseAll();
}

static void NemuConsoleCapSetSupported(uint32_t iCap, BOOL fSupported)
{
    if (fSupported)
    {
        NemuCapsEntryFuncStateSet(iCap, NEMUCAPS_ENTRY_FUNCSTATE_SUPPORTED);

        if (NemuConsoleIsAllowed())
            NemuCapsEntryAcquire(iCap);
    }
    else
    {
        NemuCapsEntryFuncStateSet(iCap, NEMUCAPS_ENTRY_FUNCSTATE_UNSUPPORTED);

        NemuCapsEntryRelease(iCap);
    }
}

void NemuSeamlessSetSupported(BOOL fSupported)
{
    NemuConsoleCapSetSupported(NEMUCAPS_ENTRY_IDX_SEAMLESS, fSupported);
}

static void NemuGrapicsSetSupported(BOOL fSupported)
{
    NemuConsoleCapSetSupported(NEMUCAPS_ENTRY_IDX_GRAPHICS, fSupported);
}
