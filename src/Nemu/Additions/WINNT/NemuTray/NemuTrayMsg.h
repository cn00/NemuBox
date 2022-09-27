/* $Id: NemuTrayMsg.h $ */
/** @file
 * NemuTrayMsg - Globally registered messages (RPC) to/from NemuTray.
 */

/*
 * Copyright (C) 2010-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NEMUTRAY_MSG_H
#define ___NEMUTRAY_MSG_H

/** The IPC pipe's prefix. Will be followed by the
 *  username NemuTray runs under. */
#define NEMUTRAY_IPC_PIPE_PREFIX      "NemuTrayIPC-"
/** The IPC header's magic. */
#define NEMUTRAY_IPC_HDR_MAGIC        0x19840804

enum NEMUTRAYIPCMSGTYPE
{
    /** Restarts NemuTray. */
    NEMUTRAYIPCMSGTYPE_RESTART        = 10,
    /** Shows a balloon message in the tray area. */
    NEMUTRAYIPCMSGTYPE_SHOWBALLOONMSG = 100,
    /** Retrieves the current user's last input
     *  time. This will be the user NemuTray is running
     *  under. No actual message for this command
     *  required. */
    NEMUTRAYIPCMSGTYPE_USERLASTINPUT  = 120
};

/* NemuTray's IPC header. */
typedef struct NEMUTRAYIPCHEADER
{
    /** The header's magic. */
    uint32_t uMagic;
    /** Header version, must be 0 by now. */
    uint32_t uHdrVersion;
    /** Message type. Specifies a message
     *  of NEMUTRAYIPCMSGTYPE. */
    uint32_t uMsgType;
    /** Message length (in bytes). This must
     *  include the overall message length, including
     *  (eventual) dynamically allocated areas which
     *  are passed into the message structure.
     */
    uint32_t uMsgLen;

} NEMUTRAYIPCHEADER, *PNEMUTRAYIPCHEADER;

/**
 * Tells NemuTray to show a balloon message in Windows'
 * tray area. This may or may not work depending on the
 * system's configuration / set user preference.
 */
typedef struct NEMUTRAYIPCMSG_SHOWBALLOONMSG
{
    /** Length of message body (in bytes). */
    uint32_t cbMsgContent;
    /** Length of message title (in bytes). */
    uint32_t cbMsgTitle;
    /** Message type. */
    uint32_t uType;
    /** Time to show the message (in ms). */
    uint32_t uShowMS;
    /** Dynamically allocated stuff.
     *
     *  Note: These must come at the end of the
     *  structure to not overwrite any important
     *  stuff above.
     */
    /** Message body. Can be up to 256 chars
     *  long. */
    char     szMsgContent[1];
        /** Message title. Can be up to 73 chars
     *  long. */
    char     szMsgTitle[1];
} NEMUTRAYIPCMSG_SHOWBALLOONMSG, *PNEMUTRAYIPCMSG_SHOWBALLOONMSG;

/**
 * Response telling the last input of the current user.
 */
typedef struct NEMUTRAYIPCRES_USERLASTINPUT
{
    /** Last occurred user input event (in seconds). */
    uint32_t uLastInput;
} NEMUTRAYIPCRES_USERLASTINPUT, *PNEMUTRAYIPCRES_USERLASTINPUT;

#endif /* !___NEMUTRAY_MSG_H */

