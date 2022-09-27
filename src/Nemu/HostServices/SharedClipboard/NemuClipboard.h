/** @file
 *
 * Shared Clipboard
 */

/*
 * Copyright (C) 2006-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __NEMUCLIPBOARD__H
#define __NEMUCLIPBOARD__H

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <Nemu/hgcmsvc.h>
#include <Nemu/log.h>

struct _NEMUCLIPBOARDCONTEXT;
typedef struct _NEMUCLIPBOARDCONTEXT NEMUCLIPBOARDCONTEXT;


typedef struct _NEMUCLIPBOARDCLIENTDATA
{
    struct _NEMUCLIPBOARDCLIENTDATA *pNext;
    struct _NEMUCLIPBOARDCLIENTDATA *pPrev;

    NEMUCLIPBOARDCONTEXT *pCtx;

    uint32_t u32ClientID;

    bool fAsync;        /* Guest is waiting for a message. */
    bool fReadPending;  /* The guest is waiting for data from the host */

    bool fMsgQuit;
    bool fMsgReadData;
    bool fMsgFormats;

    struct {
        NEMUHGCMCALLHANDLE callHandle;
        NEMUHGCMSVCPARM *paParms;
    } async;

    struct {
        NEMUHGCMCALLHANDLE callHandle;
        NEMUHGCMSVCPARM *paParms;
    } asyncRead;

    struct {
         void *pv;
         uint32_t cb;
         uint32_t u32Format;
    } data;

    uint32_t u32AvailableFormats;
    uint32_t u32RequestedFormat;

} NEMUCLIPBOARDCLIENTDATA;

/*
 * The service functions. Locking is between the service thread and the platform dependent windows thread.
 */
bool nemuSvcClipboardLock (void);
void nemuSvcClipboardUnlock (void);

void nemuSvcClipboardReportMsg (NEMUCLIPBOARDCLIENTDATA *pClient, uint32_t u32Msg, uint32_t u32Formats);

void nemuSvcClipboardCompleteReadData(NEMUCLIPBOARDCLIENTDATA *pClient, int rc, uint32_t cbActual);

bool nemuSvcClipboardGetHeadless(void);

/*
 * Platform dependent functions.
 */
int nemuClipboardInit (void);
void nemuClipboardDestroy (void);

int nemuClipboardConnect (NEMUCLIPBOARDCLIENTDATA *pClient, bool fHeadless);
void nemuClipboardDisconnect (NEMUCLIPBOARDCLIENTDATA *pClient);

void nemuClipboardFormatAnnounce (NEMUCLIPBOARDCLIENTDATA *pClient, uint32_t u32Formats);

int nemuClipboardReadData (NEMUCLIPBOARDCLIENTDATA *pClient, uint32_t u32Format, void *pv, uint32_t cb, uint32_t *pcbActual);

void nemuClipboardWriteData (NEMUCLIPBOARDCLIENTDATA *pClient, void *pv, uint32_t cb, uint32_t u32Format);

int nemuClipboardSync (NEMUCLIPBOARDCLIENTDATA *pClient);

/* Host unit testing interface */
#ifdef UNIT_TEST
uint32_t TestClipSvcGetMode(void);
#endif

#endif /* __NEMUCLIPBOARD__H */
