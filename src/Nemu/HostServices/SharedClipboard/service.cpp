/* $Id: service.cpp $ */
/** @file
 * Shared Clipboard: Host service entry points.
 */

/*
 * Copyright (C) 2006-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/** @page pg_hostclip       The Shared Clipboard Host Service
 *
 * The shared clipboard host service provides a proxy between the host's
 * clipboard and a similar proxy running on a guest.  The service is split
 * into a platform-independent core and platform-specific backends.  The
 * service defines two communication protocols - one to communicate with the
 * clipboard service running on the guest, and one to communicate with the
 * backend.  These will be described in a very skeletal fashion here.
 *
 * @section sec_hostclip_guest_proto  The guest communication protocol
 *
 * The guest clipboard service communicates with the host service via HGCM
 * (the host service runs as an HGCM service).  The guest clipboard must
 * connect to the host service before all else (Windows hosts currently only
 * support one simultaneous connection).  Once it has connected, it can send
 * HGCM messages to the host services, some of which will receive replies from
 * the host.  The host can only reply to a guest message, it cannot initiate
 * any communication.  The guest can in theory send any number of messages in
 * parallel (see the descriptions of the messages for the practice), and the
 * host will receive these in sequence, and may reply to them at once
 * (releasing the caller in the guest) or defer the reply until later.
 *
 * There are currently four messages defined.  The first is
 * NEMU_SHARED_CLIPBOARD_FN_GET_HOST_MSG, which waits for a message from the
 * host.  Host messages currently defined are
 * NEMU_SHARED_CLIPBOARD_HOST_MSG_QUIT (unused),
 * NEMU_SHARED_CLIPBOARD_HOST_MSG_READ_DATA (request that the guest send the
 * contents of its clipboard to the host) and
 * NEMU_SHARED_CLIPBOARD_HOST_MSG_FORMATS (to notify the guest that new
 * clipboard data is available).  If a host message is sent while the guest is
 * not waiting, it will be queued until the guest requests it.  At most one
 * host message of each type will be kept in the queue.  The host code only
 * supports a single simultaneous NEMU_SHARED_CLIPBOARD_FN_GET_HOST_MSG call
 * from the guest.
 *
 * The second guest message is NEMU_SHARED_CLIPBOARD_FN_FORMATS, which tells
 * the host that the guest has new clipboard data available.  The third is
 * NEMU_SHARED_CLIPBOARD_FN_READ_DATA, which asks the host to send its
 * clipboard data and waits until it arrives.  The host supports at most one
 * simultaneous NEMU_SHARED_CLIPBOARD_FN_READ_DATA call from the guest - if a
 * second call is made before the first has returned, the first will be
 * aborted.
 *
 * The last guest message is NEMU_SHARED_CLIPBOARD_FN_WRITE_DATA, which is
 * used to send the contents of the guest clipboard to the host.  This call
 * should be used after the host has requested data from the guest.
 *
 * @section sec_hostclip_backend_proto  The communication protocol with the
 *                                      platform-specific backend
 *
 * This section may be written in the future :)
 */

#include <Nemu/HostServices/NemuClipboardSvc.h>
#include <Nemu/HostServices/NemuClipboardExt.h>

#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <Nemu/vmm/ssm.h>

#include "NemuClipboard.h"

static void NemuHGCMParmUInt32Set (NEMUHGCMSVCPARM *pParm, uint32_t u32)
{
    pParm->type = NEMU_HGCM_SVC_PARM_32BIT;
    pParm->u.uint32 = u32;
}

static int NemuHGCMParmUInt32Get (NEMUHGCMSVCPARM *pParm, uint32_t *pu32)
{
    if (pParm->type == NEMU_HGCM_SVC_PARM_32BIT)
    {
        *pu32 = pParm->u.uint32;
        return VINF_SUCCESS;
    }

    return VERR_INVALID_PARAMETER;
}

#if 0
static void NemuHGCMParmPtrSet (NEMUHGCMSVCPARM *pParm, void *pv, uint32_t cb)
{
    pParm->type             = NEMU_HGCM_SVC_PARM_PTR;
    pParm->u.pointer.size   = cb;
    pParm->u.pointer.addr   = pv;
}
#endif

static int NemuHGCMParmPtrGet (NEMUHGCMSVCPARM *pParm, void **ppv, uint32_t *pcb)
{
    if (pParm->type == NEMU_HGCM_SVC_PARM_PTR)
    {
        *ppv = pParm->u.pointer.addr;
        *pcb = pParm->u.pointer.size;
        return VINF_SUCCESS;
    }

    return VERR_INVALID_PARAMETER;
}

static PNEMUHGCMSVCHELPERS g_pHelpers;

static RTCRITSECT critsect;
static uint32_t g_u32Mode;

static PFNHGCMSVCEXT g_pfnExtension;
static void *g_pvExtension;

static NEMUCLIPBOARDCLIENTDATA *g_pClient;

/* Serialization of data reading and format announcements from the RDP client. */
static bool g_fReadingData = false;
static bool g_fDelayedAnnouncement = false;
static uint32_t g_u32DelayedFormats = 0;

/** Is the clipboard running in headless mode? */
static bool g_fHeadless = false;

static uint32_t nemuSvcClipboardMode (void)
{
    return g_u32Mode;
}

#ifdef UNIT_TEST
/** Testing interface, getter for clipboard mode */
uint32_t TestClipSvcGetMode(void)
{
    return nemuSvcClipboardMode();
}
#endif

/** Getter for headless setting */
bool nemuSvcClipboardGetHeadless(void)
{
    return g_fHeadless;
}

static void nemuSvcClipboardModeSet (uint32_t u32Mode)
{
    switch (u32Mode)
    {
        case NEMU_SHARED_CLIPBOARD_MODE_OFF:
        case NEMU_SHARED_CLIPBOARD_MODE_HOST_TO_GUEST:
        case NEMU_SHARED_CLIPBOARD_MODE_GUEST_TO_HOST:
        case NEMU_SHARED_CLIPBOARD_MODE_BIDIRECTIONAL:
            g_u32Mode = u32Mode;
            break;

        default:
            g_u32Mode = NEMU_SHARED_CLIPBOARD_MODE_OFF;
    }
}

bool nemuSvcClipboardLock (void)
{
    return RT_SUCCESS(RTCritSectEnter (&critsect));
}

void nemuSvcClipboardUnlock (void)
{
    RTCritSectLeave (&critsect);
}

/* Set the HGCM parameters according to pending messages.
 * Executed under the clipboard lock.
 */
static bool nemuSvcClipboardReturnMsg (NEMUCLIPBOARDCLIENTDATA *pClient, NEMUHGCMSVCPARM paParms[])
{
    /* Message priority is taken into account. */
    if (pClient->fMsgQuit)
    {
        LogRelFlow(("nemuSvcClipboardReturnMsg: Quit\n"));
        NemuHGCMParmUInt32Set (&paParms[0], NEMU_SHARED_CLIPBOARD_HOST_MSG_QUIT);
        NemuHGCMParmUInt32Set (&paParms[1], 0);
        pClient->fMsgQuit = false;
    }
    else if (pClient->fMsgReadData)
    {
        LogRelFlow(("nemuSvcClipboardReturnMsg: ReadData %02X\n", pClient->u32RequestedFormat));
        NemuHGCMParmUInt32Set (&paParms[0], NEMU_SHARED_CLIPBOARD_HOST_MSG_READ_DATA);
        NemuHGCMParmUInt32Set (&paParms[1], pClient->u32RequestedFormat);
        pClient->fMsgReadData = false;
    }
    else if (pClient->fMsgFormats)
    {
        LogRelFlow(("nemuSvcClipboardReturnMsg: Formats %02X\n", pClient->u32AvailableFormats));
        NemuHGCMParmUInt32Set (&paParms[0], NEMU_SHARED_CLIPBOARD_HOST_MSG_FORMATS);
        NemuHGCMParmUInt32Set (&paParms[1], pClient->u32AvailableFormats);
        pClient->fMsgFormats = false;
    }
    else
    {
        /* No pending messages. */
        LogRelFlow(("nemuSvcClipboardReturnMsg: no message\n"));
        return false;
    }

    /* Message information assigned. */
    return true;
}

void nemuSvcClipboardReportMsg (NEMUCLIPBOARDCLIENTDATA *pClient, uint32_t u32Msg, uint32_t u32Formats)
{
    if (nemuSvcClipboardLock ())
    {
        switch (u32Msg)
        {
            case NEMU_SHARED_CLIPBOARD_HOST_MSG_QUIT:
            {
                LogRelFlow(("nemuSvcClipboardReportMsg: Quit\n"));
                pClient->fMsgQuit = true;
            } break;
            case NEMU_SHARED_CLIPBOARD_HOST_MSG_READ_DATA:
            {
                if (   nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_GUEST_TO_HOST
                    && nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_BIDIRECTIONAL)
                {
                    /* Skip the message. */
                    break;
                }

                LogRelFlow(("nemuSvcClipboardReportMsg: ReadData %02X\n", u32Formats));
                pClient->u32RequestedFormat = u32Formats;
                pClient->fMsgReadData = true;
            } break;
            case NEMU_SHARED_CLIPBOARD_HOST_MSG_FORMATS:
            {
                if (   nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_HOST_TO_GUEST
                    && nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_BIDIRECTIONAL)
                {
                    /* Skip the message. */
                    break;
                }

                LogRelFlow(("nemuSvcClipboardReportMsg: Formats %02X\n", u32Formats));
                pClient->u32AvailableFormats = u32Formats;
                pClient->fMsgFormats = true;
            } break;
            default:
            {
                /* Invalid message. */
                LogRelFlow(("nemuSvcClipboardReportMsg: invalid message %d\n", u32Msg));
            } break;
        }

        if (pClient->fAsync)
        {
            /* The client waits for a response. */
            bool fMessageReturned = nemuSvcClipboardReturnMsg (pClient, pClient->async.paParms);

            /* Make a copy of the handle. */
            NEMUHGCMCALLHANDLE callHandle = pClient->async.callHandle;

            if (fMessageReturned)
            {
                /* There is a response. */
                pClient->fAsync = false;
            }

            nemuSvcClipboardUnlock ();

            if (fMessageReturned)
            {
                LogRelFlow(("nemuSvcClipboardReportMsg: CallComplete\n"));
                g_pHelpers->pfnCallComplete (callHandle, VINF_SUCCESS);
            }
        }
        else
        {
            nemuSvcClipboardUnlock ();
        }
    }
}

static int svcInit (void)
{
    int rc = RTCritSectInit (&critsect);

    if (RT_SUCCESS (rc))
    {
        nemuSvcClipboardModeSet (NEMU_SHARED_CLIPBOARD_MODE_OFF);

        rc = nemuClipboardInit ();

        /* Clean up on failure, because 'svnUnload' will not be called
         * if the 'svcInit' returns an error.
         */
        if (RT_FAILURE (rc))
        {
            RTCritSectDelete (&critsect);
        }
    }

    return rc;
}

static DECLCALLBACK(int) svcUnload (void *)
{
    nemuClipboardDestroy ();
    RTCritSectDelete (&critsect);
    return VINF_SUCCESS;
}

/**
 * Disconnect the host side of the shared clipboard and send a "host disconnected" message
 * to the guest side.
 */
static DECLCALLBACK(int) svcDisconnect (void *, uint32_t u32ClientID, void *pvClient)
{
    NEMUCLIPBOARDCLIENTDATA *pClient = (NEMUCLIPBOARDCLIENTDATA *)pvClient;

    LogRel2(("svcDisconnect: u32ClientID = %d\n", u32ClientID));

    nemuSvcClipboardReportMsg (pClient, NEMU_SHARED_CLIPBOARD_HOST_MSG_QUIT, 0);

    nemuSvcClipboardCompleteReadData(pClient, VERR_NO_DATA, 0);

    nemuClipboardDisconnect (pClient);

    memset (pClient, 0, sizeof (*pClient));

    g_pClient = NULL;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) svcConnect (void *, uint32_t u32ClientID, void *pvClient)
{
    NEMUCLIPBOARDCLIENTDATA *pClient = (NEMUCLIPBOARDCLIENTDATA *)pvClient;

    int rc = VINF_SUCCESS;

    /* If there is already a client connected then we want to release it first. */
    if (g_pClient != NULL)
    {
        uint32_t u32OldClientID = g_pClient->u32ClientID;

        svcDisconnect(NULL, u32OldClientID, g_pClient);
        /* And free the resources in the hgcm subsystem. */
        g_pHelpers->pfnDisconnectClient(g_pHelpers->pvInstance, u32OldClientID);
    }

    /* Register the client. */
    memset (pClient, 0, sizeof (*pClient));

    pClient->u32ClientID = u32ClientID;

    rc = nemuClipboardConnect (pClient, nemuSvcClipboardGetHeadless());

    if (RT_SUCCESS (rc))
    {
        g_pClient = pClient;
    }

    LogRel2(("nemuClipboardConnect: rc = %Rrc\n", rc));

    return rc;
}

static DECLCALLBACK(void) svcCall (void *,
                                   NEMUHGCMCALLHANDLE callHandle,
                                   uint32_t u32ClientID,
                                   void *pvClient,
                                   uint32_t u32Function,
                                   uint32_t cParms,
                                   NEMUHGCMSVCPARM paParms[])
{
    int rc = VINF_SUCCESS;

    LogRel2(("svcCall: u32ClientID = %d, fn = %d, cParms = %d, pparms = %d\n",
             u32ClientID, u32Function, cParms, paParms));

    NEMUCLIPBOARDCLIENTDATA *pClient = (NEMUCLIPBOARDCLIENTDATA *)pvClient;

    bool fAsynchronousProcessing = false;

#ifdef DEBUG
    uint32_t i;

    for (i = 0; i < cParms; i++)
    {
        /** @todo parameters other than 32 bit */
        LogRel2(("    pparms[%d]: type %d value %d\n", i, paParms[i].type, paParms[i].u.uint32));
    }
#endif

    switch (u32Function)
    {
        case NEMU_SHARED_CLIPBOARD_FN_GET_HOST_MSG:
        {
            /* The quest requests a host message. */
            LogRel2(("svcCall: NEMU_SHARED_CLIPBOARD_FN_GET_HOST_MSG\n"));

            if (cParms != NEMU_SHARED_CLIPBOARD_CPARMS_GET_HOST_MSG)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != NEMU_HGCM_SVC_PARM_32BIT   /* msg */
                     || paParms[1].type != NEMU_HGCM_SVC_PARM_32BIT   /* formats */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Atomically verify the client's state. */
                if (nemuSvcClipboardLock ())
                {
                    bool fMessageReturned = nemuSvcClipboardReturnMsg (pClient, paParms);

                    if (fMessageReturned)
                    {
                        /* Just return to the caller. */
                        pClient->fAsync = false;
                    }
                    else
                    {
                        /* No event available at the time. Process asynchronously. */
                        fAsynchronousProcessing = true;

                        pClient->fAsync           = true;
                        pClient->async.callHandle = callHandle;
                        pClient->async.paParms    = paParms;

                        LogRel2(("svcCall: async.\n"));
                    }

                    nemuSvcClipboardUnlock ();
                }
                else
                {
                    rc = VERR_NOT_SUPPORTED;
                }
            }
        } break;

        case NEMU_SHARED_CLIPBOARD_FN_FORMATS:
        {
            /* The guest reports that some formats are available. */
            LogRel2(("svcCall: NEMU_SHARED_CLIPBOARD_FN_FORMATS\n"));

            if (cParms != NEMU_SHARED_CLIPBOARD_CPARMS_FORMATS)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != NEMU_HGCM_SVC_PARM_32BIT   /* formats */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                uint32_t u32Formats;

                rc = NemuHGCMParmUInt32Get (&paParms[0], &u32Formats);

                if (RT_SUCCESS (rc))
                {
                    if (   nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_GUEST_TO_HOST
                        && nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_BIDIRECTIONAL)
                    {
                        rc = VERR_NOT_SUPPORTED;
                        break;
                    }

                    if (g_pfnExtension)
                    {
                        NEMUCLIPBOARDEXTPARMS parms;

                        parms.u32Format = u32Formats;

                        g_pfnExtension (g_pvExtension, NEMU_CLIPBOARD_EXT_FN_FORMAT_ANNOUNCE, &parms, sizeof (parms));
                    }
                    else
                    {
                        nemuClipboardFormatAnnounce (pClient, u32Formats);
                    }
                }
            }
        } break;

        case NEMU_SHARED_CLIPBOARD_FN_READ_DATA:
        {
            /* The guest wants to read data in the given format. */
            LogRel2(("svcCall: NEMU_SHARED_CLIPBOARD_FN_READ_DATA\n"));

            if (cParms != NEMU_SHARED_CLIPBOARD_CPARMS_READ_DATA)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != NEMU_HGCM_SVC_PARM_32BIT   /* format */
                     || paParms[1].type != NEMU_HGCM_SVC_PARM_PTR     /* ptr */
                     || paParms[2].type != NEMU_HGCM_SVC_PARM_32BIT   /* size */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                uint32_t u32Format;
                void     *pv;
                uint32_t cb;

                rc = NemuHGCMParmUInt32Get (&paParms[0], &u32Format);

                if (RT_SUCCESS (rc))
                {
                    rc = NemuHGCMParmPtrGet (&paParms[1], &pv, &cb);

                    if (RT_SUCCESS (rc))
                    {
                        if (   nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_HOST_TO_GUEST
                            && nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_BIDIRECTIONAL)
                        {
                            rc = VERR_NOT_SUPPORTED;
                            break;
                        }

                        uint32_t cbActual = 0;

                        if (g_pfnExtension)
                        {
                            NEMUCLIPBOARDEXTPARMS parms;

                            parms.u32Format = u32Format;
                            parms.u.pvData = pv;
                            parms.cbData = cb;

                            g_fReadingData = true;
                            rc = g_pfnExtension (g_pvExtension, NEMU_CLIPBOARD_EXT_FN_DATA_READ, &parms, sizeof (parms));
                            LogRelFlow(("DATA: g_fDelayedAnnouncement = %d, g_u32DelayedFormats = 0x%x\n", g_fDelayedAnnouncement, g_u32DelayedFormats));
                            if (g_fDelayedAnnouncement)
                            {
                                nemuSvcClipboardReportMsg (g_pClient, NEMU_SHARED_CLIPBOARD_HOST_MSG_FORMATS, g_u32DelayedFormats);
                                g_fDelayedAnnouncement = false;
                                g_u32DelayedFormats = 0;
                            }
                            g_fReadingData = false;

                            if (RT_SUCCESS (rc))
                            {
                                cbActual = parms.cbData;
                            }
                        }
                        else
                        {
                            /* Release any other pending read, as we only
                             * support one pending read at one time. */
                            nemuSvcClipboardCompleteReadData(pClient, VERR_NO_DATA, 0);
                            rc = nemuClipboardReadData (pClient, u32Format, pv, cb, &cbActual);
                        }

                        /* Remember our read request until it is completed.
                         * See the protocol description above for more
                         * information. */
                        if (rc == VINF_HGCM_ASYNC_EXECUTE)
                        {
                            if (nemuSvcClipboardLock())
                            {
                                pClient->asyncRead.callHandle = callHandle;
                                pClient->asyncRead.paParms    = paParms;
                                pClient->fReadPending         = true;
                                fAsynchronousProcessing = true;
                                nemuSvcClipboardUnlock();
                            }
                            else
                                rc = VERR_NOT_SUPPORTED;
                        }
                        else if (RT_SUCCESS (rc))
                        {
                            NemuHGCMParmUInt32Set (&paParms[2], cbActual);
                        }
                    }
                }
            }
        } break;

        case NEMU_SHARED_CLIPBOARD_FN_WRITE_DATA:
        {
            /* The guest writes the requested data. */
            LogRel2(("svcCall: NEMU_SHARED_CLIPBOARD_FN_WRITE_DATA\n"));

            if (cParms != NEMU_SHARED_CLIPBOARD_CPARMS_WRITE_DATA)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != NEMU_HGCM_SVC_PARM_32BIT   /* format */
                     || paParms[1].type != NEMU_HGCM_SVC_PARM_PTR     /* ptr */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                void *pv;
                uint32_t cb;
                uint32_t u32Format;

                rc = NemuHGCMParmUInt32Get (&paParms[0], &u32Format);

                if (RT_SUCCESS (rc))
                {
                    rc = NemuHGCMParmPtrGet (&paParms[1], &pv, &cb);

                    if (RT_SUCCESS (rc))
                    {
                        if (   nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_GUEST_TO_HOST
                            && nemuSvcClipboardMode () != NEMU_SHARED_CLIPBOARD_MODE_BIDIRECTIONAL)
                        {
                            rc = VERR_NOT_SUPPORTED;
                            break;
                        }

                        if (g_pfnExtension)
                        {
                            NEMUCLIPBOARDEXTPARMS parms;

                            parms.u32Format = u32Format;
                            parms.u.pvData = pv;
                            parms.cbData = cb;

                            g_pfnExtension (g_pvExtension, NEMU_CLIPBOARD_EXT_FN_DATA_WRITE, &parms, sizeof (parms));
                        }
                        else
                        {
                            nemuClipboardWriteData (pClient, pv, cb, u32Format);
                        }
                    }
                }
            }
        } break;

        default:
        {
            rc = VERR_NOT_IMPLEMENTED;
        }
    }

    LogRelFlow(("svcCall: rc = %Rrc\n", rc));

    if (!fAsynchronousProcessing)
    {
        g_pHelpers->pfnCallComplete (callHandle, rc);
    }
}

/** If the client in the guest is waiting for a read operation to complete
 * then complete it, otherwise return.  See the protocol description in the
 * shared clipboard module description. */
void nemuSvcClipboardCompleteReadData(NEMUCLIPBOARDCLIENTDATA *pClient, int rc, uint32_t cbActual)
{
    NEMUHGCMCALLHANDLE callHandle = NULL;
    NEMUHGCMSVCPARM *paParms = NULL;
    bool fReadPending = false;
    if (nemuSvcClipboardLock())  /* if not can we do anything useful? */
    {
        callHandle   = pClient->asyncRead.callHandle;
        paParms      = pClient->asyncRead.paParms;
        fReadPending = pClient->fReadPending;
        pClient->fReadPending = false;
        nemuSvcClipboardUnlock();
    }
    if (fReadPending)
    {
        NemuHGCMParmUInt32Set (&paParms[2], cbActual);
        g_pHelpers->pfnCallComplete (callHandle, rc);
    }
}

/*
 * We differentiate between a function handler for the guest and one for the host.
 */
static DECLCALLBACK(int) svcHostCall (void *,
                                      uint32_t u32Function,
                                      uint32_t cParms,
                                      NEMUHGCMSVCPARM paParms[])
{
    int rc = VINF_SUCCESS;

    LogRel2(("svcHostCall: fn = %d, cParms = %d, pparms = %d\n",
         u32Function, cParms, paParms));

    switch (u32Function)
    {
        case NEMU_SHARED_CLIPBOARD_HOST_FN_SET_MODE:
        {
            LogRel2(("svcCall: NEMU_SHARED_CLIPBOARD_HOST_FN_SET_MODE\n"));

            if (cParms != 1)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != NEMU_HGCM_SVC_PARM_32BIT   /* mode */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                uint32_t u32Mode = NEMU_SHARED_CLIPBOARD_MODE_OFF;

                rc = NemuHGCMParmUInt32Get (&paParms[0], &u32Mode);

                /* The setter takes care of invalid values. */
                nemuSvcClipboardModeSet (u32Mode);
            }
        } break;

        case NEMU_SHARED_CLIPBOARD_HOST_FN_SET_HEADLESS:
        {
            uint32_t u32Headless = g_fHeadless;

            rc = VERR_INVALID_PARAMETER;
            if (cParms != 1)
                break;
            rc = NemuHGCMParmUInt32Get (&paParms[0], &u32Headless);
            if (RT_SUCCESS(rc))
                LogRelFlow(("svcCall: NEMU_SHARED_CLIPBOARD_HOST_FN_SET_HEADLESS, u32Headless=%u\n",
                            (unsigned) u32Headless));
            g_fHeadless = RT_BOOL(u32Headless);
        } break;

        default:
            break;
    }

    LogRelFlow(("svcHostCall: rc = %Rrc\n", rc));
    return rc;
}

/**
 * SSM descriptor table for the NEMUCLIPBOARDCLIENTDATA structure.
 */
static SSMFIELD const g_aClipboardClientDataFields[] =
{
    SSMFIELD_ENTRY(NEMUCLIPBOARDCLIENTDATA, u32ClientID),  /* for validation purposes */
    SSMFIELD_ENTRY(NEMUCLIPBOARDCLIENTDATA, fMsgQuit),
    SSMFIELD_ENTRY(NEMUCLIPBOARDCLIENTDATA, fMsgReadData),
    SSMFIELD_ENTRY(NEMUCLIPBOARDCLIENTDATA, fMsgFormats),
    SSMFIELD_ENTRY(NEMUCLIPBOARDCLIENTDATA, u32RequestedFormat),
    SSMFIELD_ENTRY_TERM()
};

static DECLCALLBACK(int) svcSaveState(void *, uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM)
{
#ifndef UNIT_TEST
    /*
     * When the state will be restored, pending requests will be reissued
     * by VMMDev. The service therefore must save state as if there were no
     * pending request.
     * Pending requests, if any, will be completed in svcDisconnect.
     */
    LogRel2 (("svcSaveState: u32ClientID = %d\n", u32ClientID));

    NEMUCLIPBOARDCLIENTDATA *pClient = (NEMUCLIPBOARDCLIENTDATA *)pvClient;

    /* This field used to be the length. We're using it as a version field
       with the high bit set. */
    SSMR3PutU32 (pSSM, UINT32_C (0x80000002));
    int rc = SSMR3PutStructEx (pSSM, pClient, sizeof(*pClient), 0 /*fFlags*/, &g_aClipboardClientDataFields[0], NULL);
    AssertRCReturn (rc, rc);
#endif /* !UNIT_TEST */
    return VINF_SUCCESS;
}

/**
 * This structure corresponds to the original layout of the
 * NEMUCLIPBOARDCLIENTDATA structure.  As the structure was saved as a whole
 * when saving state, we need to remember it forever in order to preserve
 * compatibility.
 *
 * (Starting with 3.1 this is no longer used.)
 *
 * @remarks Putting this outside svcLoadState to avoid visibility warning caused
 *          by -Wattributes.
 */
typedef struct CLIPSAVEDSTATEDATA
{
    struct CLIPSAVEDSTATEDATA *pNext;
    struct CLIPSAVEDSTATEDATA *pPrev;

    NEMUCLIPBOARDCONTEXT *pCtx;

    uint32_t u32ClientID;

    bool fAsync: 1; /* Guest is waiting for a message. */

    bool fMsgQuit: 1;
    bool fMsgReadData: 1;
    bool fMsgFormats: 1;

    struct {
        NEMUHGCMCALLHANDLE callHandle;
        NEMUHGCMSVCPARM *paParms;
    } async;

    struct {
         void *pv;
         uint32_t cb;
         uint32_t u32Format;
    } data;

    uint32_t u32AvailableFormats;
    uint32_t u32RequestedFormat;

} CLIPSAVEDSTATEDATA;

static DECLCALLBACK(int) svcLoadState(void *, uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM)
{
#ifndef UNIT_TEST
    LogRel2 (("svcLoadState: u32ClientID = %d\n", u32ClientID));

    NEMUCLIPBOARDCLIENTDATA *pClient = (NEMUCLIPBOARDCLIENTDATA *)pvClient;

    /* Existing client can not be in async state yet. */
    Assert (!pClient->fAsync);

    /* Save the client ID for data validation. */
    /** @todo isn't this the same as u32ClientID? Playing safe for now... */
    uint32_t const u32ClientIDOld = pClient->u32ClientID;

    /* Restore the client data. */
    uint32_t lenOrVer;
    int rc = SSMR3GetU32 (pSSM, &lenOrVer);
    AssertRCReturn (rc, rc);
    if (lenOrVer == UINT32_C (0x80000002))
    {
        rc = SSMR3GetStructEx (pSSM, pClient, sizeof(*pClient), 0 /*fFlags*/, &g_aClipboardClientDataFields[0], NULL);
        AssertRCReturn (rc, rc);
    }
    else if (lenOrVer == (SSMR3HandleHostBits (pSSM) == 64 ? 72 : 48))
    {
        /**
         * SSM descriptor table for the CLIPSAVEDSTATEDATA structure.
         */
        static SSMFIELD const s_aClipSavedStateDataFields30[] =
        {
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, pNext),
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, pPrev),
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, pCtx),
            SSMFIELD_ENTRY(                 CLIPSAVEDSTATEDATA, u32ClientID),
            SSMFIELD_ENTRY_CUSTOM(fMsgQuit+fMsgReadData+fMsgFormats, RT_OFFSETOF(CLIPSAVEDSTATEDATA, u32ClientID) + 4, 4),
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, async.callHandle),
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, async.paParms),
            SSMFIELD_ENTRY_IGNORE(          CLIPSAVEDSTATEDATA, data.pv),
            SSMFIELD_ENTRY_IGNORE(          CLIPSAVEDSTATEDATA, data.cb),
            SSMFIELD_ENTRY_IGNORE(          CLIPSAVEDSTATEDATA, data.u32Format),
            SSMFIELD_ENTRY_IGNORE(          CLIPSAVEDSTATEDATA, u32AvailableFormats),
            SSMFIELD_ENTRY(                 CLIPSAVEDSTATEDATA, u32RequestedFormat),
            SSMFIELD_ENTRY_TERM()
        };

        CLIPSAVEDSTATEDATA savedState;
        RT_ZERO (savedState);
        rc = SSMR3GetStructEx (pSSM, &savedState, sizeof(savedState), SSMSTRUCT_FLAGS_MEM_BAND_AID,
                               &s_aClipSavedStateDataFields30[0], NULL);
        AssertRCReturn (rc, rc);

        pClient->fMsgQuit           = savedState.fMsgQuit;
        pClient->fMsgReadData       = savedState.fMsgReadData;
        pClient->fMsgFormats        = savedState.fMsgFormats;
        pClient->u32RequestedFormat = savedState.u32RequestedFormat;
    }
    else
    {
        LogRel (("Client data size mismatch: got %#x\n", lenOrVer));
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }

    /* Verify the client ID. */
    if (pClient->u32ClientID != u32ClientIDOld)
    {
        LogRel (("Client ID mismatch: expected %d, got %d\n", u32ClientIDOld, pClient->u32ClientID));
        pClient->u32ClientID = u32ClientIDOld;
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }

    /* Actual host data are to be reported to guest (SYNC). */
    nemuClipboardSync (pClient);

#endif /* !UNIT_TEST */
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) extCallback (uint32_t u32Function, uint32_t u32Format, void *pvData, uint32_t cbData)
{
    if (g_pClient != NULL)
    {
        switch (u32Function)
        {
            case NEMU_CLIPBOARD_EXT_FN_FORMAT_ANNOUNCE:
            {
                LogRelFlow(("ANNOUNCE: g_fReadingData = %d\n", g_fReadingData));
                if (g_fReadingData)
                {
                    g_fDelayedAnnouncement = true;
                    g_u32DelayedFormats = u32Format;
                }
                else
                {
                    nemuSvcClipboardReportMsg (g_pClient, NEMU_SHARED_CLIPBOARD_HOST_MSG_FORMATS, u32Format);
                }
            } break;

            case NEMU_CLIPBOARD_EXT_FN_DATA_READ:
            {
                nemuSvcClipboardReportMsg (g_pClient, NEMU_SHARED_CLIPBOARD_HOST_MSG_READ_DATA, u32Format);
            } break;

            default:
                return VERR_NOT_SUPPORTED;
        }
    }

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) svcRegisterExtension(void *, PFNHGCMSVCEXT pfnExtension, void *pvExtension)
{
    LogRelFlowFunc(("pfnExtension = %p\n", pfnExtension));

    NEMUCLIPBOARDEXTPARMS parms;

    if (pfnExtension)
    {
        /* Install extension. */
        g_pfnExtension = pfnExtension;
        g_pvExtension = pvExtension;

        parms.u.pfnCallback = extCallback;
        g_pfnExtension (g_pvExtension, NEMU_CLIPBOARD_EXT_FN_SET_CALLBACK, &parms, sizeof (parms));
    }
    else
    {
        if (g_pfnExtension)
        {
            parms.u.pfnCallback = NULL;
            g_pfnExtension (g_pvExtension, NEMU_CLIPBOARD_EXT_FN_SET_CALLBACK, &parms, sizeof (parms));
        }

        /* Uninstall extension. */
        g_pfnExtension = NULL;
        g_pvExtension = NULL;
    }

    return VINF_SUCCESS;
}

extern "C" DECLCALLBACK(DECLEXPORT(int)) NemuHGCMSvcLoad (NEMUHGCMSVCFNTABLE *ptable)
{
    int rc = VINF_SUCCESS;

    LogRelFlowFunc(("ptable = %p\n", ptable));

    if (!ptable)
    {
        rc = VERR_INVALID_PARAMETER;
    }
    else
    {
        LogRel2(("NemuHGCMSvcLoad: ptable->cbSize = %d, ptable->u32Version = 0x%08X\n", ptable->cbSize, ptable->u32Version));

        if (   ptable->cbSize != sizeof (NEMUHGCMSVCFNTABLE)
            || ptable->u32Version != NEMU_HGCM_SVC_VERSION)
        {
            rc = VERR_INVALID_PARAMETER;
        }
        else
        {
            g_pHelpers = ptable->pHelpers;

            ptable->cbClient = sizeof (NEMUCLIPBOARDCLIENTDATA);

            ptable->pfnUnload     = svcUnload;
            ptable->pfnConnect    = svcConnect;
            ptable->pfnDisconnect = svcDisconnect;
            ptable->pfnCall       = svcCall;
            ptable->pfnHostCall   = svcHostCall;
            ptable->pfnSaveState  = svcSaveState;
            ptable->pfnLoadState  = svcLoadState;
            ptable->pfnRegisterExtension  = svcRegisterExtension;
            ptable->pvService     = NULL;

            /* Service specific initialization. */
            rc = svcInit ();
        }
    }

    return rc;
}
