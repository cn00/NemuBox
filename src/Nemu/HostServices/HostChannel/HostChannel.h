/* @file
 *
 * Host Channel
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __NEMUHOSTCHANNEL__H
#define __NEMUHOSTCHANNEL__H

#include <iprt/list.h>

#define LOG_GROUP LOG_GROUP_HGCM
#include <Nemu/log.h>
#include <Nemu/HostServices/NemuHostChannel.h>

#define HOSTCHLOG Log

#ifdef DEBUG_sunlover
# undef HOSTCHLOG
# define HOSTCHLOG LogRel
#endif /* DEBUG_sunlover */

struct NEMUHOSTCHCTX;
typedef struct NEMUHOSTCHCTX NEMUHOSTCHCTX;

typedef struct NEMUHOSTCHCLIENT
{
    RTLISTNODE nodeClient;

    NEMUHOSTCHCTX *pCtx;

    uint32_t u32ClientID;

    RTLISTANCHOR listChannels;
    uint32_t volatile u32HandleSrc;

    RTLISTANCHOR listContexts; /* Callback contexts. */

    RTLISTANCHOR listEvents;

    bool fAsync;        /* Guest is waiting for a message. */

    struct {
        NEMUHGCMCALLHANDLE callHandle;
        NEMUHGCMSVCPARM *paParms;
    } async;

} NEMUHOSTCHCLIENT;


/*
 * The service functions. Locking is between the service thread and the host channel provider thread.
 */
int nemuHostChannelLock(void);
void nemuHostChannelUnlock(void);

int nemuHostChannelInit(void);
void nemuHostChannelDestroy(void);

int nemuHostChannelClientConnect(NEMUHOSTCHCLIENT *pClient);
void nemuHostChannelClientDisconnect(NEMUHOSTCHCLIENT *pClient);

int nemuHostChannelAttach(NEMUHOSTCHCLIENT *pClient,
                          uint32_t *pu32Handle,
                          const char *pszName,
                          uint32_t u32Flags);
int nemuHostChannelDetach(NEMUHOSTCHCLIENT *pClient,
                          uint32_t u32Handle);

int nemuHostChannelSend(NEMUHOSTCHCLIENT *pClient,
                        uint32_t u32Handle,
                        const void *pvData,
                        uint32_t cbData);
int nemuHostChannelRecv(NEMUHOSTCHCLIENT *pClient,
                        uint32_t u32Handle,
                        void *pvData,
                        uint32_t cbData,
                        uint32_t *pu32DataReceived,
                        uint32_t *pu32DataRemaining);
int nemuHostChannelControl(NEMUHOSTCHCLIENT *pClient,
                           uint32_t u32Handle,
                           uint32_t u32Code,
                           void *pvParm,
                           uint32_t cbParm,
                           void *pvData,
                           uint32_t cbData,
                           uint32_t *pu32SizeDataReturned);

int nemuHostChannelEventWait(NEMUHOSTCHCLIENT *pClient,
                             bool *pfEvent,
                             NEMUHGCMCALLHANDLE callHandle,
                             NEMUHGCMSVCPARM *paParms);

int nemuHostChannelEventCancel(NEMUHOSTCHCLIENT *pClient);

int nemuHostChannelQuery(NEMUHOSTCHCLIENT *pClient,
                         const char *pszName,
                         uint32_t u32Code,
                         void *pvParm,
                         uint32_t cbParm,
                         void *pvData,
                         uint32_t cbData,
                         uint32_t *pu32SizeDataReturned);

int nemuHostChannelRegister(const char *pszName,
                            const NEMUHOSTCHANNELINTERFACE *pInterface,
                            uint32_t cbInterface);
int nemuHostChannelUnregister(const char *pszName);


void nemuHostChannelEventParmsSet(NEMUHGCMSVCPARM *paParms,
                                  uint32_t u32ChannelHandle,
                                  uint32_t u32Id,
                                  const void *pvEvent,
                                  uint32_t cbEvent);

void nemuHostChannelReportAsync(NEMUHOSTCHCLIENT *pClient,
                                uint32_t u32ChannelHandle,
                                uint32_t u32Id,
                                const void *pvEvent,
                                uint32_t cbEvent);

#endif /* __NEMUHOSTCHANNEL__H */
