/** @file
 *
 * VirtualBox 3D host inter-components interfaces
 */

/*
 * Copyright (C) 2011-2015 Oracle Corporation
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
#ifndef ___Nemu_NemuVideoHost3D_h
#define ___Nemu_NemuVideoHost3D_h
#include <iprt/cdefs.h>
#include <Nemu/NemuVideo.h>
#include <Nemu/hgcmsvc.h>
#include <Nemu/vmm/pdmifs.h>
#include <iprt/list.h>

/* screen update instance */
typedef struct PDMIDISPLAYCONNECTOR *HNEMUCRCMDCLTSCR;
struct VBVACMDHDR;

typedef struct NEMUCMDVBVA_HDR *PNEMUCMDVBVA_HDR;

typedef DECLCALLBACKPTR(void, PFNNEMUCRCMD_CLTSCR_UPDATE_BEGIN)(HNEMUCRCMDCLTSCR hClt, unsigned u32Screen);
typedef DECLCALLBACKPTR(void, PFNNEMUCRCMD_CLTSCR_UPDATE_END)(HNEMUCRCMDCLTSCR hClt, unsigned uScreenId, int32_t x, int32_t y, uint32_t cx, uint32_t cy);
typedef DECLCALLBACKPTR(void, PFNNEMUCRCMD_CLTSCR_UPDATE_PROCESS)(HNEMUCRCMDCLTSCR hClt, unsigned u32Screen, struct VBVACMDHDR *pCmd, size_t cbCmd);

/*client callbacks to be used by the server
 * when working in the CrCmd mode */
typedef struct NEMUCRCMD_SVRENABLE_INFO
{
    HNEMUCRCMDCLTSCR hCltScr;
    PFNNEMUCRCMD_CLTSCR_UPDATE_BEGIN pfnCltScrUpdateBegin;
    PFNNEMUCRCMD_CLTSCR_UPDATE_PROCESS pfnCltScrUpdateProcess;
    PFNNEMUCRCMD_CLTSCR_UPDATE_END pfnCltScrUpdateEnd;
} NEMUCRCMD_SVRENABLE_INFO;

typedef struct NEMUVDMAHOST * HNEMUCRCLIENT;
struct NEMUCRCMDCTL_CALLOUT_LISTENTRY;
typedef DECLCALLBACKPTR(void, PFNNEMUCRCMDCTL_CALLOUT_CB)(struct NEMUCRCMDCTL_CALLOUT_LISTENTRY *pEntry);

#pragma pack(1)
typedef struct NEMUCRCMDCTL_CALLOUT_LISTENTRY
{
    RTLISTNODE Node;
    PFNNEMUCRCMDCTL_CALLOUT_CB pfnCb;
} NEMUCRCMDCTL_CALLOUT_LISTENTRY;

typedef struct NEMUCRCMDCTL_CALLOUT_LIST
{
    RTLISTANCHOR List;
} NEMUCRCMDCTL_CALLOUT_LIST;
#pragma pack()

struct NEMUCRCMDCTL;

typedef DECLCALLBACKPTR(int, PFNNEMUCRCLIENT_CALLOUT)(HNEMUCRCLIENT hClient, struct NEMUCRCMDCTL* pCmd, NEMUCRCMDCTL_CALLOUT_LISTENTRY *pEntry, PFNNEMUCRCMDCTL_CALLOUT_CB pfnCb);

typedef struct NEMUCRCLIENT_INFO
{
    HNEMUCRCLIENT hClient;
    PFNNEMUCRCLIENT_CALLOUT pfnCallout;
} NEMUCRCLIENT_INFO;

typedef void * HNEMUCRCMDSVR;

/* enables the CrCmd interface, thus the hgcm interface gets disabled.
 * all subsequent calls will be done in the thread Enable was done,
 * until the Disable is called */
typedef DECLCALLBACKPTR(int, PFNNEMUCRCMD_SVR_ENABLE)(HNEMUCRCMDSVR hSvr, NEMUCRCMD_SVRENABLE_INFO *pInfo);
/* Opposite to Enable (see above) */
typedef DECLCALLBACKPTR(int, PFNNEMUCRCMD_SVR_DISABLE)(HNEMUCRCMDSVR hSvr);
/* process command */
typedef DECLCALLBACKPTR(int8_t, PFNNEMUCRCMD_SVR_CMD)(HNEMUCRCMDSVR hSvr, const NEMUCMDVBVA_HDR *pCmd, uint32_t cbCmd);
/* process host control */
typedef DECLCALLBACKPTR(int, PFNNEMUCRCMD_SVR_HOSTCTL)(HNEMUCRCMDSVR hSvr, uint8_t* pCtl, uint32_t cbCmd);
/* process guest control */
typedef DECLCALLBACKPTR(int, PFNNEMUCRCMD_SVR_GUESTCTL)(HNEMUCRCMDSVR hSvr, uint8_t* pCtl, uint32_t cbCmd);
/* screen resize */
typedef DECLCALLBACKPTR(int, PFNNEMUCRCMD_SVR_RESIZE)(HNEMUCRCMDSVR hSvr, const struct VBVAINFOSCREEN *pScreen, const uint32_t *pTargetMap);
/* process SaveState */
typedef DECLCALLBACKPTR(int, PFNNEMUCRCMD_SVR_SAVESTATE)(HNEMUCRCMDSVR hSvr, PSSMHANDLE pSSM);
/* process LoadState */
typedef DECLCALLBACKPTR(int, PFNNEMUCRCMD_SVR_LOADSTATE)(HNEMUCRCMDSVR hSvr, PSSMHANDLE pSSM, uint32_t u32Version);


typedef struct NEMUCRCMD_SVRINFO
{
    HNEMUCRCMDSVR hSvr;
    PFNNEMUCRCMD_SVR_ENABLE pfnEnable;
    PFNNEMUCRCMD_SVR_DISABLE pfnDisable;
    PFNNEMUCRCMD_SVR_CMD pfnCmd;
    PFNNEMUCRCMD_SVR_HOSTCTL pfnHostCtl;
    PFNNEMUCRCMD_SVR_GUESTCTL pfnGuestCtl;
    PFNNEMUCRCMD_SVR_RESIZE pfnResize;
    PFNNEMUCRCMD_SVR_SAVESTATE pfnSaveState;
    PFNNEMUCRCMD_SVR_LOADSTATE pfnLoadState;
} NEMUCRCMD_SVRINFO;


typedef struct NEMUVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP
{
    NEMUVDMACMD_CHROMIUM_CTL Hdr;
    union
    {
        void *pvVRamBase;
        uint64_t uAlignment;
    };
    uint64_t cbVRam;
    PPDMLED pLed;
    NEMUCRCLIENT_INFO CrClientInfo;
    /* out */
    struct NEMUCRCMD_SVRINFO CrCmdServerInfo;
} NEMUVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP, *PNEMUVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP;

typedef enum
{
    NEMUCRCMDCTL_TYPE_HGCM = 1,
    NEMUCRCMDCTL_TYPE_DISABLE,
    NEMUCRCMDCTL_TYPE_ENABLE,
    NEMUCRCMDCTL_TYPE_32bit = 0x7fffffff
} NEMUCRCMDCTL_TYPE;

#pragma pack(1)

typedef struct NEMUCRCMDCTL
{
    NEMUCRCMDCTL_TYPE enmType;
    uint32_t u32Function;
    /* not to be used by clients */
    NEMUCRCMDCTL_CALLOUT_LIST CalloutList;
    union
    {
        void(*pfnInternal)();
        void* pvInternal;
    } u;
} NEMUCRCMDCTL;

#define NEMUCRCMDCTL_IS_CALLOUT_AVAILABLE(_pCtl) (!!((_pCtl)->CalloutList.List.pNext))

typedef struct NEMUCRCMDCTL_HGCM
{
    NEMUCRCMDCTL Hdr;
    NEMUHGCMSVCPARM aParms[1];
} NEMUCRCMDCTL_HGCM;
#pragma pack()

typedef struct NEMUVDMAHOST * HNEMUCRCMDCTL_REMAINING_HOST_COMMAND;

typedef DECLCALLBACKPTR(uint8_t*, PFNNEMUCRCMDCTL_REMAINING_HOST_COMMAND)(HNEMUCRCMDCTL_REMAINING_HOST_COMMAND hClient, uint32_t *pcbCtl, int prevCmdRc);

typedef struct NEMUCRCMDCTL_HGCMENABLE_DATA
{
    HNEMUCRCMDCTL_REMAINING_HOST_COMMAND hRHCmd;
    PFNNEMUCRCMDCTL_REMAINING_HOST_COMMAND pfnRHCmd;
} NEMUCRCMDCTL_HGCMENABLE_DATA;

typedef struct NEMUVDMAHOST * HNEMUCRCMDCTL_NOTIFY_TERMINATING;

typedef DECLCALLBACKPTR(int, PFNNEMUCRCMDCTL_NOTIFY_TERMINATING)(HNEMUCRCMDCTL_NOTIFY_TERMINATING hClient, NEMUCRCMDCTL_HGCMENABLE_DATA *pHgcmEnableData);

typedef DECLCALLBACKPTR(void, PFNNEMUCRCMDCTL_NOTIFY_TERMINATING_DONE)(HNEMUCRCMDCTL_NOTIFY_TERMINATING hClient);

typedef struct NEMUCRCMDCTL_HGCMDISABLE_DATA
{
    HNEMUCRCMDCTL_NOTIFY_TERMINATING hNotifyTerm;
    PFNNEMUCRCMDCTL_NOTIFY_TERMINATING pfnNotifyTerm;
    PFNNEMUCRCMDCTL_NOTIFY_TERMINATING_DONE pfnNotifyTermDone;
} NEMUCRCMDCTL_HGCMDISABLE_DATA;

#pragma pack(1)
typedef struct NEMUCRCMDCTL_ENABLE
{
    NEMUCRCMDCTL Hdr;
    NEMUCRCMDCTL_HGCMENABLE_DATA Data;
} NEMUCRCMDCTL_ENABLE;

typedef struct NEMUCRCMDCTL_DISABLE
{
    NEMUCRCMDCTL Hdr;
    NEMUCRCMDCTL_HGCMDISABLE_DATA Data;
} NEMUCRCMDCTL_DISABLE;
#pragma pack()

#endif /*#ifndef ___Nemu_NemuVideoHost3D_h*/
