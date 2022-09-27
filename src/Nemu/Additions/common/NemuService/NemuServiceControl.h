/* $Id: NemuServiceControl.h $ */
/** @file
 * NemuServiceControl.h - Internal guest control definitions.
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
 */

#ifndef ___NemuServiceControl_h
#define ___NemuServiceControl_h

#include <iprt/critsect.h>
#include <iprt/list.h>
#include <iprt/req.h>

#include <Nemu/NemuGuestLib.h>
#include <Nemu/HostServices/GuestControlSvc.h>


/**
 * Pipe IDs for handling the guest process poll set.
 */
typedef enum NEMUSERVICECTRLPIPEID
{
    NEMUSERVICECTRLPIPEID_UNKNOWN           = 0,
    NEMUSERVICECTRLPIPEID_STDIN             = 10,
    NEMUSERVICECTRLPIPEID_STDIN_WRITABLE    = 11,
    /** Pipe for reading from guest process' stdout. */
    NEMUSERVICECTRLPIPEID_STDOUT            = 40,
    /** Pipe for reading from guest process' stderr. */
    NEMUSERVICECTRLPIPEID_STDERR            = 50,
    /** Notification pipe for waking up the guest process
     *  control thread. */
    NEMUSERVICECTRLPIPEID_IPC_NOTIFY        = 100
} NEMUSERVICECTRLPIPEID;

/**
 * Structure for one (opened) guest file.
 */
typedef struct NEMUSERVICECTRLFILE
{
    /** Pointer to list archor of following
     *  list node.
     *  @todo Would be nice to have a RTListGetAnchor(). */
    PRTLISTANCHOR                   pAnchor;
    /** Node to global guest control file list. */
    /** @todo Use a map later? */
    RTLISTNODE                      Node;
    /** The file name. */
    char                            szName[RTPATH_MAX];
    /** The file handle on the guest. */
    RTFILE                          hFile;
    /** File handle to identify this file. */
    uint32_t                        uHandle;
    /** Context ID. */
    uint32_t                        uContextID;
} NEMUSERVICECTRLFILE;
/** Pointer to thread data. */
typedef NEMUSERVICECTRLFILE *PNEMUSERVICECTRLFILE;

typedef struct NEMUSERVICECTRLSESSIONSTARTUPINFO
{
    /** The session's protocol version to use. */
    uint32_t                        uProtocol;
    /** The session's ID. */
    uint32_t                        uSessionID;
    /** User name (account) to start the guest session under. */
    char                            szUser[GUESTPROCESS_MAX_USER_LEN];
    /** Password of specified user name (account). */
    char                            szPassword[GUESTPROCESS_MAX_PASSWORD_LEN];
    /** Domain of the user account. */
    char                            szDomain[GUESTPROCESS_MAX_DOMAIN_LEN];
    /** Session creation flags.
     *  @sa NEMUSERVICECTRLSESSIONSTARTUPFLAG_* flags. */
    uint32_t                        fFlags;
} NEMUSERVICECTRLSESSIONSTARTUPINFO;
/** Pointer to thread data. */
typedef NEMUSERVICECTRLSESSIONSTARTUPINFO *PNEMUSERVICECTRLSESSIONSTARTUPINFO;

/**
 * Structure for a guest session thread to
 * observe/control the forked session instance from
 * the NemuService main executable.
 */
typedef struct NEMUSERVICECTRLSESSIONTHREAD
{
    /** Node to global guest control session list. */
    /** @todo Use a map later? */
    RTLISTNODE                      Node;
    /** The sessions's startup info. */
    NEMUSERVICECTRLSESSIONSTARTUPINFO
                                    StartupInfo;
    /** The worker thread. */
    RTTHREAD                        Thread;
    /** Critical section for thread-safe use. */
    RTCRITSECT                      CritSect;
    /** Process handle for forked child. */
    RTPROCESS                       hProcess;
    /** Shutdown indicator; will be set when the thread
      * needs (or is asked) to shutdown. */
    bool volatile                   fShutdown;
    /** Indicator set by the service thread exiting. */
    bool volatile                   fStopped;
    /** Whether the thread was started or not. */
    bool                            fStarted;
#if 0 /* Pipe IPC not used yet. */
    /** Pollset containing all the pipes. */
    RTPOLLSET                       hPollSet;
    RTPIPE                          hStdInW;
    RTPIPE                          hStdOutR;
    RTPIPE                          hStdErrR;
    struct StdPipe
    {
        RTHANDLE  hChild;
        PRTHANDLE phChild;
    }                               StdIn,
                                    StdOut,
                                    StdErr;
    /** The notification pipe associated with this guest session.
     *  This is NIL_RTPIPE for output pipes. */
    RTPIPE                          hNotificationPipeW;
    /** The other end of hNotificationPipeW. */
    RTPIPE                          hNotificationPipeR;
#endif
} NEMUSERVICECTRLSESSIONTHREAD;
/** Pointer to thread data. */
typedef NEMUSERVICECTRLSESSIONTHREAD *PNEMUSERVICECTRLSESSIONTHREAD;

/** Flag indicating that this session has been spawned from
 *  the main executable. */
#define NEMUSERVICECTRLSESSION_FLAG_SPAWN                RT_BIT(0)
/** Flag indicating that this session is anonymous, that is,
 *  it will run start guest processes with the same credentials
 *  as the main executable. */
#define NEMUSERVICECTRLSESSION_FLAG_ANONYMOUS            RT_BIT(1)
/** Flag indicating that started guest processes will dump their
 *  stdout output to a separate file on disk. For debugging. */
#define NEMUSERVICECTRLSESSION_FLAG_DUMPSTDOUT           RT_BIT(2)
/** Flag indicating that started guest processes will dump their
 *  stderr output to a separate file on disk. For debugging. */
#define NEMUSERVICECTRLSESSION_FLAG_DUMPSTDERR           RT_BIT(3)

/**
 * Structure for maintaining a guest session. This also
 * contains all started threads (e.g. for guest processes).
 *
 * This structure can act in two different ways:
 * - For legacy guest control handling (protocol version < 2)
 *   this acts as a per-guest process structure containing all
 *   the information needed to get a guest process up and running.
 * - For newer guest control protocols (>= 2) this structure is
 *   part of the forked session child, maintaining all guest
 *   control objects under it.
 */
typedef struct NEMUSERVICECTRLSESSION
{
    /* The session's startup information. */
    NEMUSERVICECTRLSESSIONSTARTUPINFO
                                    StartupInfo;
    /** List of active guest process threads
     *  (NEMUSERVICECTRLPROCESS). */
    RTLISTANCHOR                    lstProcesses;
    /** List of guest control files (NEMUSERVICECTRLFILE). */
    RTLISTANCHOR                    lstFiles;
    /** The session's critical section. */
    RTCRITSECT                      CritSect;
    /** Internal session flags, not related
     *  to StartupInfo stuff.
     *  @sa NEMUSERVICECTRLSESSION_FLAG_* flags. */
    uint32_t                        fFlags;
    /** How many processes do we allow keeping around at a time? */
    uint32_t                        uProcsMaxKept;
} NEMUSERVICECTRLSESSION;
/** Pointer to guest session. */
typedef NEMUSERVICECTRLSESSION *PNEMUSERVICECTRLSESSION;

/**
 * Structure holding information for starting a guest
 * process.
 */
typedef struct NEMUSERVICECTRLPROCSTARTUPINFO
{
    /** Full qualified path of process to start (without arguments). */
    char szCmd[GUESTPROCESS_MAX_CMD_LEN];
    /** Process execution flags. @sa */
    uint32_t uFlags;
    /** Command line arguments. */
    char szArgs[GUESTPROCESS_MAX_ARGS_LEN];
    /** Number of arguments specified in pszArgs. */
    uint32_t uNumArgs;
    /** String of environment variables ("FOO=BAR") to pass to the process
      * to start. */
    char szEnv[GUESTPROCESS_MAX_ENV_LEN];
    /** Size (in bytes) of environment variables block. */
    uint32_t cbEnv;
    /** Number of environment variables specified in pszEnv. */
    uint32_t uNumEnvVars;
    /** User name (account) to start the process under. */
    char szUser[GUESTPROCESS_MAX_USER_LEN];
    /** Password of specified user name (account). */
    char szPassword[GUESTPROCESS_MAX_PASSWORD_LEN];
    /** Time limit (in ms) of the process' life time. */
    uint32_t uTimeLimitMS;
    /** Process priority. */
    uint32_t uPriority;
    /** Process affinity. At the moment we support
     *  up to 4 * 64 = 256 CPUs. */
    uint64_t uAffinity[4];
    /** Number of used process affinity blocks. */
    uint32_t uNumAffinity;
} NEMUSERVICECTRLPROCSTARTUPINFO;
/** Pointer to a guest process block. */
typedef NEMUSERVICECTRLPROCSTARTUPINFO *PNEMUSERVICECTRLPROCSTARTUPINFO;

/**
 * Structure for holding data for one (started) guest process.
 */
typedef struct NEMUSERVICECTRLPROCESS
{
    /** Node. */
    RTLISTNODE                      Node;
    /** Process handle. */
    RTPROCESS                       hProcess;
    /** Number of references using this struct. */
    uint32_t                        cRefs;
    /** The worker thread. */
    RTTHREAD                        Thread;
    /** The session this guest process
     *  is bound to. */
    PNEMUSERVICECTRLSESSION         pSession;
    /** Shutdown indicator; will be set when the thread
      * needs (or is asked) to shutdown. */
    bool volatile                   fShutdown;
    /** Whether the guest process thread was stopped
     *  or not. */
    bool volatile                   fStopped;
    /** Whether the guest process thread was started
     *  or not. */
    bool                            fStarted;
    /** Client ID. */
    uint32_t                        uClientID;
    /** Context ID. */
    uint32_t                        uContextID;
    /** Critical section for thread-safe use. */
    RTCRITSECT                      CritSect;
    /** Process startup information. */
    NEMUSERVICECTRLPROCSTARTUPINFO
                                    StartupInfo;
    /** The process' PID assigned by the guest OS. */
    uint32_t                        uPID;
    /** The process' request queue to handle requests
     *  from the outside, e.g. the session. */
    RTREQQUEUE                      hReqQueue;
    /** Our pollset, used for accessing the process'
     *  std* pipes + the notification pipe. */
    RTPOLLSET                       hPollSet;
    /** StdIn pipe for addressing writes to the
     *  guest process' stdin.*/
    RTPIPE                          hPipeStdInW;
    /** StdOut pipe for addressing reads from
     *  guest process' stdout.*/
    RTPIPE                          hPipeStdOutR;
    /** StdOut pipe for addressing reads from
     *  guest process' stdout.*/
    RTPIPE                          hPipeStdErrR;
    /** The notification pipe associated with this guest process.
     *  This is NIL_RTPIPE for output pipes. */
    RTPIPE                          hNotificationPipeW;
    /** The other end of hNotificationPipeW. */
    RTPIPE                          hNotificationPipeR;
} NEMUSERVICECTRLPROCESS;
/** Pointer to thread data. */
typedef NEMUSERVICECTRLPROCESS *PNEMUSERVICECTRLPROCESS;

RT_C_DECLS_BEGIN

extern RTLISTANCHOR             g_lstControlSessionThreads;
extern NEMUSERVICECTRLSESSION   g_Session;


/** @name Guest session thread handling.
 * @{ */
extern int                      VGSvcGstCtrlSessionThreadCreate(PRTLISTANCHOR pList, const PNEMUSERVICECTRLSESSIONSTARTUPINFO pSessionStartupInfo, PNEMUSERVICECTRLSESSIONTHREAD *ppSessionThread);
extern int                      VGSvcGstCtrlSessionThreadDestroy(PNEMUSERVICECTRLSESSIONTHREAD pSession, uint32_t uFlags);
extern int                      VGSvcGstCtrlSessionThreadDestroyAll(PRTLISTANCHOR pList, uint32_t uFlags);
extern int                      VGSvcGstCtrlSessionThreadTerminate(PNEMUSERVICECTRLSESSIONTHREAD pSession);
extern RTEXITCODE               VGSvcGstCtrlSessionSpawnInit(int argc, char **argv);
/** @} */
/** @name Per-session functions.
 * @{ */
extern PNEMUSERVICECTRLPROCESS  VGSvcGstCtrlSessionRetainProcess(PNEMUSERVICECTRLSESSION pSession, uint32_t uPID);
extern int                      VGSvcGstCtrlSessionClose(PNEMUSERVICECTRLSESSION pSession);
extern int                      VGSvcGstCtrlSessionDestroy(PNEMUSERVICECTRLSESSION pSession);
extern int                      VGSvcGstCtrlSessionInit(PNEMUSERVICECTRLSESSION pSession, uint32_t uFlags);
extern int                      VGSvcGstCtrlSessionHandler(PNEMUSERVICECTRLSESSION pSession, uint32_t uMsg, PVBGLR3GUESTCTRLCMDCTX pHostCtx, void *pvScratchBuf, size_t cbScratchBuf, volatile bool *pfShutdown);
extern int                      VGSvcGstCtrlSessionProcessAdd(PNEMUSERVICECTRLSESSION pSession, PNEMUSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlSessionProcessRemove(PNEMUSERVICECTRLSESSION pSession, PNEMUSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlSessionProcessStartAllowed(const PNEMUSERVICECTRLSESSION pSession, bool *pbAllowed);
extern int                      VGSvcGstCtrlSessionReapProcesses(PNEMUSERVICECTRLSESSION pSession);
/** @} */
/** @name Per-guest process functions.
 * @{ */
extern int                      VGSvcGstCtrlProcessFree(PNEMUSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlProcessHandleInput(PNEMUSERVICECTRLPROCESS pProcess, PVBGLR3GUESTCTRLCMDCTX pHostCtx, bool fPendingClose, void *pvBuf, uint32_t cbBuf);
extern int                      VGSvcGstCtrlProcessHandleOutput(PNEMUSERVICECTRLPROCESS pProcess, PVBGLR3GUESTCTRLCMDCTX pHostCtx, uint32_t uHandle, uint32_t cbToRead, uint32_t uFlags);
extern int                      VGSvcGstCtrlProcessHandleTerm(PNEMUSERVICECTRLPROCESS pProcess);
extern void                     VGSvcGstCtrlProcessRelease(PNEMUSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlProcessStart(const PNEMUSERVICECTRLSESSION pSession, const PNEMUSERVICECTRLPROCSTARTUPINFO pStartupInfo, uint32_t uContext);
extern int                      VGSvcGstCtrlProcessStop(PNEMUSERVICECTRLPROCESS pProcess);
extern int                      VGSvcGstCtrlProcessWait(const PNEMUSERVICECTRLPROCESS pProcess, RTMSINTERVAL msTimeout, int *pRc);
/** @} */

RT_C_DECLS_END

#endif

