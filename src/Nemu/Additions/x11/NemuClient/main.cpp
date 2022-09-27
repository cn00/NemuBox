/* $Id: main.cpp $ */
/** @file
 *
 * VirtualBox Guest Service:
 * Linux guest.
 */

/*
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

#include <sys/types.h>
#include <stdlib.h>       /* For exit */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <iprt/buildconfig.h>
#include <iprt/critsect.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/types.h>
#include <Nemu/NemuGuestLib.h>
#include <Nemu/log.h>

#include "NemuClient.h"

static int (*gpfnOldIOErrorHandler)(Display *) = NULL;

/** Object representing the service we are running.  This has to be global
 * so that the cleanup routine can access it. */
struct VBCLSERVICE **g_pService;
/** The name of our pidfile.  It is global for the benefit of the cleanup
 * routine. */
static char g_szPidFile[RTPATH_MAX];
/** The file handle of our pidfile.  It is global for the benefit of the
 * cleanup routine. */
static RTFILE g_hPidFile;
/** Global critical section held during the clean-up routine (to prevent it
 * being called on multiple threads at once) or things which may not happen
 * during clean-up (e.g. pausing and resuming the service).
 */
RTCRITSECT g_critSect;
/** Counter of how often our deamon has been respawned. */
unsigned cRespawn = 0;

/** Exit with a fatal error. */
void vbclFatalError(char *pszMessage)
{
    char *pszCommand;
    if (pszMessage && cRespawn == 0)
    {
        pszCommand = RTStrAPrintf2("notify-send \"NemuClient: %s\"", pszMessage);
        if (pszCommand)
            system(pszCommand);
    }
    _exit(1);
}

/** Clean up if we get a signal or something.  This is extern so that we
 * can call it from other compilation units. */
void VBClCleanUp()
{
    /* We never release this, as we end up with a call to exit(3) which is not
     * async-safe.  Unless we fix this application properly, we should be sure
     * never to exit from anywhere except from this method. */
    int rc = RTCritSectEnter(&g_critSect);
    if (RT_FAILURE(rc))
        VBClFatalError(("NemuClient: Failure while acquiring the global critical section, rc=%Rrc\n", rc));
    if (g_pService)
        (*g_pService)->cleanup(g_pService);
    if (g_szPidFile[0] && g_hPidFile)
        VbglR3ClosePidFile(g_szPidFile, g_hPidFile);
    exit(0);
}

/**
 * A standard signal handler which cleans up and exits.
 */
static void nemuClientSignalHandler(int cSignal)
{
    LogRel(("NemuClient: terminated with signal %d\n", cSignal));
    /** Disable seamless mode */
    RTPrintf(("NemuClient: terminating...\n"));
    VBClCleanUp();
}

/**
 * Xlib error handler for certain errors that we can't avoid.
 */
static int nemuClientXLibErrorHandler(Display *pDisplay, XErrorEvent *pError)
{
    char errorText[1024];

    XGetErrorText(pDisplay, pError->error_code, errorText, sizeof(errorText));
    LogRelFlow(("NemuClient: an X Window protocol error occurred: %s (error code %d).  Request code: %d, minor code: %d, serial number: %d\n", errorText, pError->error_code, pError->request_code, pError->minor_code, pError->serial));
    return 0;
}

/**
 * Xlib error handler for fatal errors.  This often means that the programme is still running
 * when X exits.
 */
static int nemuClientXLibIOErrorHandler(Display *pDisplay)
{
    LogRel(("NemuClient: a fatal guest X Window error occurred.  This may just mean that the Window system was shut down while the client was still running.\n"));
    VBClCleanUp();
    return 0;  /* We should never reach this. */
}

/**
 * Reset all standard termination signals to call our signal handler, which
 * cleans up and exits.
 */
static void nemuClientSetSignalHandlers(void)
{
    struct sigaction sigAction;

    LogRelFlowFunc(("\n"));
    sigAction.sa_handler = nemuClientSignalHandler;
    sigemptyset(&sigAction.sa_mask);
    sigAction.sa_flags = 0;
    sigaction(SIGHUP, &sigAction, NULL);
    sigaction(SIGINT, &sigAction, NULL);
    sigaction(SIGQUIT, &sigAction, NULL);
    sigaction(SIGPIPE, &sigAction, NULL);
    sigaction(SIGALRM, &sigAction, NULL);
    sigaction(SIGTERM, &sigAction, NULL);
    sigaction(SIGUSR1, &sigAction, NULL);
    sigaction(SIGUSR2, &sigAction, NULL);
    LogRelFlowFunc(("returning\n"));
}

/**
 * Print out a usage message and exit with success.
 */
void nemuClientUsage(const char *pcszFileName)
{
    RTPrintf("Usage: %s --clipboard|"
#ifdef NEMU_WITH_DRAG_AND_DROP
             "--draganddrop|"
#endif
             "--display|"
# ifdef NEMU_WITH_GUEST_PROPS
             "--checkhostversion|"
#endif
             "--seamless [-d|--nodaemon]\n", pcszFileName);
    RTPrintf("Starts the VirtualBox X Window System guest services.\n\n");
    RTPrintf("Options:\n");
    RTPrintf("  --clipboard        starts the shared clipboard service\n");
#ifdef NEMU_WITH_DRAG_AND_DROP
    RTPrintf("  --draganddrop      starts the drag and drop service\n");
#endif
    RTPrintf("  --display          starts the display management service\n");
#ifdef NEMU_WITH_GUEST_PROPS
    RTPrintf("  --checkhostversion starts the host version notifier service\n");
#endif
    RTPrintf("  --seamless         starts the seamless windows service\n");
    RTPrintf("  -d, --nodaemon     continues running as a system service\n");
    RTPrintf("  -h, --help         shows this help text\n");
    RTPrintf("  -V, --version      shows version information\n");
    RTPrintf("\n");
    exit(0);
}

/**
 * The main loop for the NemuClient daemon.
 * @todo Clean up for readability.
 */
int main(int argc, char *argv[])
{
    bool fDaemonise = true, fRespawn = true;
    int rc;
    const char *pcszFileName;

    /* Initialise our runtime before all else. */
    rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
    /* This should never be called twice in one process - in fact one Display
     * object should probably never be used from multiple threads anyway. */
    if (!XInitThreads())
        VBClFatalError(("Failed to initialize X11 threads\n"));
    /* Get our file name for error output. */
    pcszFileName = RTPathFilename(argv[0]);
    if (!pcszFileName)
        pcszFileName = "NemuClient";

    /* Parse our option(s) */
    /** @todo Use RTGetOpt() if the arguments become more complex. */
    for (int i = 1; i < argc; ++i)
    {
        rc = VERR_INVALID_PARAMETER;
        if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--nodaemon"))
        {
            /* If the user is running in "no daemon" mode anyway, send critical
             * logging to stdout as well. */
            PRTLOGGER pReleaseLog = RTLogRelGetDefaultInstance();
            if (pReleaseLog)
                rc = RTLogDestinations(pReleaseLog, "stdout");
            if (pReleaseLog && RT_FAILURE(rc))
                RTPrintf("%s: failed to redivert error output, rc=%Rrc\n",
                         pcszFileName, rc);
            fDaemonise = false;
        }
        else if (!strcmp(argv[i], "--no-respawn"))
        {
            fRespawn = false;
        }
        else if (!strcmp(argv[i], "--clipboard"))
        {
            if (g_pService)
                break;
            g_pService = VBClGetClipboardService();
        }
        else if (!strcmp(argv[i], "--display"))
        {
            if (g_pService)
                break;
            g_pService = VBClGetDisplayService();
        }
        else if (!strcmp(argv[i], "--seamless"))
        {
            if (g_pService)
                break;
            g_pService = VBClGetSeamlessService();
        }
        else if (!strcmp(argv[i], "--checkhostversion"))
        {
            if (g_pService)
                break;
            g_pService = VBClGetHostVersionService();
        }
#ifdef NEMU_WITH_DRAG_AND_DROP
        else if (!strcmp(argv[i], "--draganddrop"))
        {
            if (g_pService)
                break;
            g_pService = VBClGetDragAndDropService();
        }
#endif /* NEMU_WITH_DRAG_AND_DROP */
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            nemuClientUsage(pcszFileName);
            return 0;
        }
        else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
        {
            RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
            return 0;
        }
        else
        {
            RTPrintf("%s: unrecognized option `%s'\n", pcszFileName, argv[i]);
            RTPrintf("Try `%s --help' for more information\n", pcszFileName);
            return 1;
        }
        rc = VINF_SUCCESS;
    }
    if (RT_FAILURE(rc) || !g_pService)
    {
        nemuClientUsage(pcszFileName);
        return 1;
    }

    rc = RTCritSectInit(&g_critSect);
    if (RT_FAILURE(rc))
        VBClFatalError(("Initialising critical section: %Rrc\n", rc));
    rc = RTPathUserHome(g_szPidFile, sizeof(g_szPidFile));
    if (RT_FAILURE(rc))
        VBClFatalError(("Getting home directory for pid-file: %Rrc\n", rc));
    rc = RTPathAppend(g_szPidFile, sizeof(g_szPidFile),
                      (*g_pService)->getPidFilePath());
    if (RT_FAILURE(rc))
        VBClFatalError(("Creating pid-file path: %Rrc\n", rc));
    if (fDaemonise)
        rc = VbglR3Daemonize(false /* fNoChDir */, false /* fNoClose */, fRespawn, &cRespawn);
    if (RT_FAILURE(rc))
        VBClFatalError(("Daemonizing: %Rrc\n", rc));
    if (g_szPidFile[0])
        rc = VbglR3PidFile(g_szPidFile, &g_hPidFile);
    if (rc == VERR_FILE_LOCK_VIOLATION)  /* Already running. */
        return 0;
    if (RT_FAILURE(rc))
        VBClFatalError(("Creating pid-file: %Rrc\n", rc));
    /* Set signal handlers to clean up on exit. */
    nemuClientSetSignalHandlers();
#ifndef NEMUCLIENT_WITHOUT_X11
    /* Set an X11 error handler, so that we don't die when we get unavoidable
     * errors. */
    XSetErrorHandler(nemuClientXLibErrorHandler);
    /* Set an X11 I/O error handler, so that we can shutdown properly on
     * fatal errors. */
    XSetIOErrorHandler(nemuClientXLibIOErrorHandler);
#endif
    rc = (*g_pService)->init(g_pService);
    if (RT_FAILURE(rc))
        VBClFatalError(("Initialising service: %Rrc\n", rc));
    rc = (*g_pService)->run(g_pService, fDaemonise);
    if (RT_FAILURE(rc))
        VBClFatalError(("Service main loop failed: %Rrc\n", rc));
    VBClCleanUp();
    return 0;
}
