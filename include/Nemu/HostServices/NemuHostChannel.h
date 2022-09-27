/** @file
 *
 * Host Channel: the service definition.
 */

/*
 * Copyright (C) 2012-2015 Oracle Corporation
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

#ifndef ___Nemu_HostService_NemuHostChannel_h
#define ___Nemu_HostService_NemuHostChannel_h

#include <Nemu/VMMDev.h>
#include <Nemu/NemuGuest2.h>
#include <Nemu/hgcmsvc.h>

/*
 * Host calls.
 */
#define NEMU_HOST_CHANNEL_HOST_FN_REGISTER      1
#define NEMU_HOST_CHANNEL_HOST_FN_UNREGISTER    2

/*
 * Guest calls.
 */
#define NEMU_HOST_CHANNEL_FN_ATTACH       1 /* Attach to a channel. */
#define NEMU_HOST_CHANNEL_FN_DETACH       2 /* Detach from the channel. */
#define NEMU_HOST_CHANNEL_FN_SEND         3 /* Send data to the host. */
#define NEMU_HOST_CHANNEL_FN_RECV         4 /* Receive data from the host. */
#define NEMU_HOST_CHANNEL_FN_CONTROL      5 /* Generic data exchange using a channel instance. */
#define NEMU_HOST_CHANNEL_FN_EVENT_WAIT   6 /* Blocking wait for a host event. */
#define NEMU_HOST_CHANNEL_FN_EVENT_CANCEL 7 /* Cancel the blocking wait. */
#define NEMU_HOST_CHANNEL_FN_QUERY        8 /* Generic data exchange using a channel name. */

/*
 * The host event ids for the guest.
 */
#define NEMU_HOST_CHANNEL_EVENT_CANCELLED    0    /* Event was cancelled by FN_EVENT_CANCEL. */
#define NEMU_HOST_CHANNEL_EVENT_UNREGISTERED 1    /* Channel was unregistered on host. */
#define NEMU_HOST_CHANNEL_EVENT_RECV         2    /* Data is available for receiving. */
#define NEMU_HOST_CHANNEL_EVENT_USER         1000 /* Base of channel specific events. */

/*
 * The common control code ids for the NEMU_HOST_CHANNEL_FN_[CONTROL|QUERY]
 */
#define NEMU_HOST_CHANNEL_CTRL_EXISTS     0    /* Whether the channel instance or provider exists. */
#define NEMU_HOST_CHANNEL_CTRL_USER       1000 /* Base of channel specific events. */

#pragma pack(1)

/* Parameter of NEMU_HOST_CHANNEL_EVENT_RECV */
typedef struct NEMUHOSTCHANNELEVENTRECV
{
    uint32_t u32SizeAvailable; /* How many bytes can be read from the channel. */
} NEMUHOSTCHANNELEVENTRECV;

/*
 * Guest calls.
 */

typedef struct NemuHostChannelAttach
{
    NemuGuestHGCMCallInfo hdr;
    HGCMFunctionParameter name;   /* IN linear ptr: Channel name utf8 nul terminated. */
    HGCMFunctionParameter flags;  /* IN uint32_t: Channel specific flags. */
    HGCMFunctionParameter handle; /* OUT uint32_t: The channel handle. */
} NemuHostChannelAttach;

typedef struct NemuHostChannelDetach
{
    NemuGuestHGCMCallInfo hdr;
    HGCMFunctionParameter handle; /* IN uint32_t: The channel handle. */
} NemuHostChannelDetach;

typedef struct NemuHostChannelSend
{
    NemuGuestHGCMCallInfo hdr;
    HGCMFunctionParameter handle; /* IN uint32_t: The channel handle. */
    HGCMFunctionParameter data;   /* IN linear pointer: Data to be sent. */
} NemuHostChannelSend;

typedef struct NemuHostChannelRecv
{
    NemuGuestHGCMCallInfo hdr;
    HGCMFunctionParameter handle;        /* IN uint32_t: The channel handle. */
    HGCMFunctionParameter data;          /* OUT linear pointer: Buffer for data to be received. */
    HGCMFunctionParameter sizeReceived;  /* OUT uint32_t: Bytes received. */
    HGCMFunctionParameter sizeRemaining; /* OUT uint32_t: Bytes remaining in the channel. */
} NemuHostChannelRecv;

typedef struct NemuHostChannelControl
{
    NemuGuestHGCMCallInfo hdr;
    HGCMFunctionParameter handle; /* IN uint32_t: The channel handle. */
    HGCMFunctionParameter code;   /* IN uint32_t: The channel specific control code. */
    HGCMFunctionParameter parm;   /* IN linear pointer: Parameters of the function. */
    HGCMFunctionParameter data;   /* OUT linear pointer: Buffer for results. */
    HGCMFunctionParameter sizeDataReturned; /* OUT uint32_t: Bytes returned in the 'data' buffer. */
} NemuHostChannelControl;

typedef struct NemuHostChannelEventWait
{
    NemuGuestHGCMCallInfo hdr;
    HGCMFunctionParameter handle;       /* OUT uint32_t: The channel which generated the event. */
    HGCMFunctionParameter id;           /* OUT uint32_t: The event NEMU_HOST_CHANNEL_EVENT_*. */
    HGCMFunctionParameter parm;         /* OUT linear pointer: Parameters of the event. */
    HGCMFunctionParameter sizeReturned; /* OUT uint32_t: Size of the parameters. */
} NemuHostChannelEventWait;

typedef struct NemuHostChannelEventCancel
{
    NemuGuestHGCMCallInfo hdr;
} NemuHostChannelEventCancel;

typedef struct NemuHostChannelQuery
{
    NemuGuestHGCMCallInfo hdr;
    HGCMFunctionParameter name;   /* IN linear ptr: Channel name utf8 nul terminated. */
    HGCMFunctionParameter code;   /* IN uint32_t: The control code. */
    HGCMFunctionParameter parm;   /* IN linear pointer: Parameters of the function. */
    HGCMFunctionParameter data;   /* OUT linear pointer: Buffer for results. */
    HGCMFunctionParameter sizeDataReturned; /* OUT uint32_t: Bytes returned in the 'data' buffer. */
} NemuHostChannelQuery;


/*
 * Host calls
 */

typedef struct NemuHostChannelHostRegister
{
    NEMUHGCMSVCPARM name;      /* IN ptr: Channel name utf8 nul terminated. */
    NEMUHGCMSVCPARM iface;     /* IN ptr: NEMUHOSTCHANNELINTERFACE. */
} NemuHostChannelHostRegister;

typedef struct NemuHostChannelHostUnregister
{
    NEMUHGCMSVCPARM name;   /* IN ptr: Channel name utf8 nul terminated */
} NemuHostChannelHostUnregister;

/* The channel provider will invoke this callback to report channel events. */
typedef struct NEMUHOSTCHANNELCALLBACKS
{
    /* A channel event occured.
     *
     * @param pvCallbacks The callback context specified in HostChannelAttach.
     * @param pvChannel   The channel instance returned by HostChannelAttach.
     * @param u32Id       The event id.
     * @param pvEvent     The event parameters.
     * @param cbEvent     The size of event parameters.
     */
    DECLR3CALLBACKMEMBER(void, HostChannelCallbackEvent, (void *pvCallbacks, void *pvChannel,
                                                          uint32_t u32Id, const void *pvEvent, uint32_t cbEvent));

    /* The channel has been deleted by the provider. pvCallback will not be used anymore.
     *
     * @param pvCallbacks The callback context specified in HostChannelAttach.
     * @param pvChannel   The channel instance returned by HostChannelAttach.
     */
    DECLR3CALLBACKMEMBER(void, HostChannelCallbackDeleted, (void *pvCallbacks, void *pvChannel));
} NEMUHOSTCHANNELCALLBACKS;

typedef struct NEMUHOSTCHANNELINTERFACE
{
    /* The channel provider context. */
    void *pvProvider;

    /* A new channel is requested.
     *
     * @param pvProvider   The provider context NEMUHOSTCHANNELINTERFACE::pvProvider.
     * @param ppvChannel   Where to store pointer to the channel instance created by the provider.
     * @param u32Flags     Channel specific flags.
     * @param pCallbacks   Callbacks to be invoked by the channel provider.
     * @param pvCallbacks  The context of callbacks.
     */
    DECLR3CALLBACKMEMBER(int, HostChannelAttach,  (void *pvProvider, void **ppvChannel, uint32_t u32Flags,
                                                   NEMUHOSTCHANNELCALLBACKS *pCallbacks, void *pvCallbacks));

    /* The channel is closed. */
    DECLR3CALLBACKMEMBER(void, HostChannelDetach, (void *pvChannel));

    /* The guest sends data to the channel. */
    DECLR3CALLBACKMEMBER(int, HostChannelSend,    (void *pvChannel, const void *pvData, uint32_t cbData));

    /* The guest reads data from the channel. */
    DECLR3CALLBACKMEMBER(int, HostChannelRecv,    (void *pvChannel, void *pvData, uint32_t cbData,
                                                   uint32_t *pcbReceived, uint32_t *pcbRemaining));

    /* The guest talks to the provider of the channel.
     * @param pvChannel The channel instance. NULL if the target is the provider, rather than a channel.
     */
    DECLR3CALLBACKMEMBER(int, HostChannelControl, (void *pvChannel, uint32_t u32Code,
                                                   const void *pvParm, uint32_t cbParm,
                                                   const void *pvData, uint32_t cbData, uint32_t *pcbDataReturned));
} NEMUHOSTCHANNELINTERFACE;

#pragma pack()

#endif
