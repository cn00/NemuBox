/* $Id: NemuUSB-solaris.c $ */
/** @file
 * VirtualBox USB Client Driver, Solaris Hosts.
 */

/*
 * Copyright (C) 2008-2015 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_USB_DRV
#include <Nemu/version.h>
#include <Nemu/log.h>
#include <Nemu/err.h>
#include <Nemu/cdefs.h>
#include <Nemu/sup.h>
#include <Nemu/usblib-solaris.h>

#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/semaphore.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/thread.h>

#define USBDRV_MAJOR_VER    2
#define USBDRV_MINOR_VER    0
#include <sys/usb/usba.h>
#include <sys/strsun.h>
#include "usbai_private.h"
#include <sys/archsystm.h>
#include <sys/disp.h>

/** @todo review the locking here, verify assumptions about code executed
 *        without the nemuusb_state_t::Mtx mutex */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The module name. */
#define DEVICE_NAME                                     "nemuusb"
/** The module description as seen in 'modinfo'. */
#define DEVICE_DESC_DRV                                 "VirtualBox USB"

/** Endpoint states */
#define NEMUUSB_EP_INITIALIZED                          0xa1fa1fa
#define NEMUUSB_EP_STATE_NONE                           RT_BIT(0)
#define NEMUUSB_EP_STATE_CLOSED                         RT_BIT(1)
#define NEMUUSB_EP_STATE_OPENED                         RT_BIT(2)
/** Polling states */
#define NEMUUSB_POLL_OFF                                RT_BIT(0)
#define NEMUUSB_POLL_ON                                 RT_BIT(1)
#define NEMUUSB_POLL_REAP_PENDING                       RT_BIT(2)
#define NEMUUSB_POLL_DEV_UNPLUGGED                      RT_BIT(3)

/** -=-=-=-=-=-=- Standard Specifics -=-=-=-=-=-=- */
/** Max. supported endpoints */
#define NEMUUSB_MAX_ENDPOINTS                           32
/** Size of USB Ctrl Xfer Header */
#define NEMUUSB_CTRL_XFER_SIZE                          0x08
/**
 * USB2.0 (Sec. 9-13) Bits 10..0 is the max packet size; for high speed Isoc/Intr, bits 12..11 is
 * number of additional transaction opportunities per microframe.
 */
#define NEMUUSB_PKT_SIZE(pkt)                          (pkt & 0x07FF) * (1 + ((pkt >> 11) & 3))
/** Endpoint Xfer Type */
#define NEMUUSB_XFER_TYPE(endp)                        ((endp)->EpDesc.bmAttributes & USB_EP_ATTR_MASK)
/** Endpoint Xfer Direction */
#define NEMUUSB_XFER_DIR(endp)                         ((endp)->EpDesc.bEndpointAddress & USB_EP_DIR_IN)

/** -=-=-=-=-=-=- Tunable Parameters -=-=-=-=-=-=- */
/** Time to wait while draining inflight UBRs on suspend, in seconds. */
#define NEMUUSB_DRAIN_TIME                              20
/** Ctrl Xfer timeout in seconds. */
#define NEMUUSB_CTRL_XFER_TIMEOUT                       10
/** Bulk Xfer timeout in seconds. */
#define NEMUUSB_BULK_XFER_TIMEOUT                       10
/** Intr Xfer timeout in seconds. */
#define NEMUUSB_INTR_XFER_TIMEOUT                       10
/** Maximum URB queue length. */
#define NEMUUSB_URB_QUEUE_SIZE                          64
/** Maximum asynchronous requests per pipe */
#define NEMUUSB_MAX_PIPE_ASYNC_REQS                     2

/** For enabling global symbols while debugging  **/
#if defined(DEBUG_ramshankar)
# define LOCAL
#else
# define LOCAL    static
#endif


/*********************************************************************************************************************************
*   Kernel Entry Hooks                                                                                                           *
*********************************************************************************************************************************/
int NemuUSBSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred);
int NemuUSBSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred);
int NemuUSBSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred);
int NemuUSBSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred);
int NemuUSBSolarisIOCtl(dev_t Dev, int Cmd, intptr_t pArg, int Mode, cred_t *pCred, int *pVal);
int NemuUSBSolarisPoll(dev_t Dev, short fEvents, int fAnyYet, short *pReqEvents, struct pollhead **ppPollHead);
int NemuUSBSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pArg, void **ppResult);
int NemuUSBSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
int NemuUSBSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);
int NemuUSBSolarisPower(dev_info_t *pDip, int Component, int Level);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * cb_ops: for drivers that support char/block entry points
 */
static struct cb_ops g_NemuUSBSolarisCbOps =
{
    NemuUSBSolarisOpen,
    NemuUSBSolarisClose,
    nodev,                      /* b strategy */
    nodev,                      /* b dump */
    nodev,                      /* b print */
    NemuUSBSolarisRead,
    NemuUSBSolarisWrite,
    NemuUSBSolarisIOCtl,
    nodev,                      /* c devmap */
    nodev,                      /* c mmap */
    nodev,                      /* c segmap */
    NemuUSBSolarisPoll,
    ddi_prop_op,                /* property ops */
    NULL,                       /* streamtab  */
    D_NEW | D_MP,               /* compat. flag */
    CB_REV,                     /* revision */
    nodev,                      /* c aread */
    nodev                       /* c awrite */
};

/**
 * dev_ops: for driver device operations
 */
static struct dev_ops g_NemuUSBSolarisDevOps =
{
    DEVO_REV,                   /* driver build revision */
    0,                          /* ref count */
    NemuUSBSolarisGetInfo,
    nulldev,                    /* identify */
    nulldev,                    /* probe */
    NemuUSBSolarisAttach,
    NemuUSBSolarisDetach,
    nodev,                      /* reset */
    &g_NemuUSBSolarisCbOps,
    NULL,                       /* bus ops */
    NemuUSBSolarisPower,
    ddi_quiesce_not_needed
};

/**
 * modldrv: export driver specifics to the kernel
 */
static struct modldrv g_NemuUSBSolarisModule =
{
    &mod_driverops,             /* extern from kernel */
    DEVICE_DESC_DRV " " NEMU_VERSION_STRING "r" RT_XSTR(NEMU_SVN_REV),
    &g_NemuUSBSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_NemuUSBSolarisModLinkage =
{
    MODREV_1,
    &g_NemuUSBSolarisModule,
    NULL,
};

/**
 * nemuusb_ep_t: Endpoint structure with info. for managing an endpoint.
 */
typedef struct nemuusb_ep_t
{
    uint_t                  fInitialized;    /* Whether this Endpoint is initialized */
    uint_t                  EpState;         /* Endpoint state */
    usb_ep_descr_t          EpDesc;          /* Endpoint descriptor */
    uchar_t                 uCfgValue;       /* Configuration value */
    uchar_t                 uInterface;      /* Interface number */
    uchar_t                 uAlt;            /* Alternate number */
    usb_pipe_handle_t       pPipe;           /* Endpoint pipe handle */
    usb_pipe_policy_t       PipePolicy;      /* Endpoint policy */
    bool                    fIsocPolling;    /* Whether Isoc. IN polling is enabled */
    list_t                  hIsocInUrbs;     /* Isoc. IN inflight URBs */
    uint16_t                cIsocInUrbs;     /* Number of Isoc. IN inflight URBs */
    list_t                  hIsocInLandedReqs;   /* Isoc. IN landed requests */
    uint16_t                cbIsocInLandedReqs;  /* Cumulative size of landed Isoc. IN requests */
    size_t                  cbMaxIsocData;   /* Maximum size of Isoc. IN landed buffer */
} nemuusb_ep_t;

/**
 * nemuusb_isoc_req_t: Isoc IN. requests queued from device till they are reaped.
 */
typedef struct nemuusb_isoc_req_t
{
    mblk_t                 *pMsg;            /* Pointer to the data buffer */
    uint32_t                cIsocPkts;       /* Number of Isoc pkts */
    VUSBISOC_PKT_DESC       aIsocPkts[8];    /* Array of Isoc pkt descriptors */
    list_node_t             hListLink;
} nemuusb_isoc_req_t;

/**
 * NEMUUSB_URB_STATE: Internal USB URB state.
 */
typedef enum NEMUUSB_URB_STATE
{
    NEMUUSB_URB_STATE_FREE     = 0x00,
    NEMUUSB_URB_STATE_INFLIGHT = 0x04,
    NEMUUSB_URB_STATE_LANDED   = 0x08
} NEMUUSB_URB_STATE;

/**
 * nemuusb_urb_t: kernel URB representation.
 */
typedef struct nemuusb_urb_t
{
    void                   *pvUrbR3;         /* Userspace URB address (untouched, returned while reaping) */
    uint8_t                 bEndpoint;       /* Endpoint address */
    VUSBXFERTYPE            enmType;         /* Xfer type */
    VUSBDIRECTION           enmDir;          /* Xfer direction */
    VUSBSTATUS              enmStatus;       /* URB status */
    bool                    fShortOk;        /* Whether receiving less data than requested is acceptable. */
    RTR3PTR                 pvDataR3;        /* Userspace address of the original data buffer */
    size_t                  cbDataR3;        /* Size of the data buffer */
    mblk_t                 *pMsg;            /* Pointer to the data buffer */
    uint32_t                cIsocPkts;       /* Number of Isoc pkts */
    VUSBISOC_PKT_DESC       aIsocPkts[8];    /* Array of Isoc pkt descriptors */
    NEMUUSB_URB_STATE       enmState;        /* Whether free/in-flight etc. */
    struct nemuusb_state_t *pState;          /* Pointer to the device instance */
    list_node_t             hListLink;       /* List node link handle */
} nemuusb_urb_t;

/**
 * nemuusb_power_t: Per Device Power Management info.
 */
typedef struct nemuusb_power_t
{
    uint_t                  PowerStates;     /* Bit mask of the power states */
    int                     PowerBusy;       /* Busy counter */
    bool                    fPowerWakeup;    /* Whether remote power wakeup is enabled */
    bool                    fPowerRaise;     /* Whether to raise the power level */
    uint8_t                 PowerLevel;      /* Current power level */
} nemuusb_power_t;

/**
 * nemuusb_state_t: Per Device instance state info.
 */
typedef struct nemuusb_state_t
{
    dev_info_t             *pDip;            /* Per instance device info. */
    usb_client_dev_data_t  *pDevDesc;        /* Parsed & complete device descriptor */
    uint8_t                 DevState;        /* Current USB Device state */
    bool                    fClosed;         /* Whether the device (default control pipe) is closed */
    bool                    fRestoreCfg;     /* Whether we changed configs to restore while tearing down */
    bool                    fGetCfgReqDone;  /* First GET_CONFIG request has been circumvented */
    kmutex_t                Mtx;             /* Mutex state protection */
    usb_serialization_t     StateMulti;      /* State serialization */
    size_t                  cbMaxBulkXfer;   /* Maximum bulk xfer size */
    nemuusb_ep_t            aEps[NEMUUSB_MAX_ENDPOINTS]; /* All endpoints structures */
    list_t                  hUrbs;           /* Handle to list of free/inflight URBs */
    list_t                  hLandedUrbs;     /* Handle to list of landed URBs */
    uint16_t                cInflightUrbs;   /* Number of inflight URBs. */
    pollhead_t              PollHead;        /* Handle to pollhead for waking polling processes  */
    int                     fPoll;           /* Polling status flag */
    RTPROCESS               Process;         /* The process (id) of the session */
    NEMUUSBREQ_CLIENT_INFO  ClientInfo;      /* Registration data */
    nemuusb_power_t        *pPower;          /* Power Management */
} nemuusb_state_t;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
LOCAL int nemuUSBSolarisInitEndPoint(nemuusb_state_t *pState, usb_ep_data_t *pEpData, uchar_t uCfgValue,
                                uchar_t uInterface, uchar_t uAlt);
LOCAL int nemuUSBSolarisInitAllEndPoints(nemuusb_state_t *pState);
LOCAL int nemuUSBSolarisInitEndPointsForConfig(nemuusb_state_t *pState, uint8_t uCfgIndex);
LOCAL int nemuUSBSolarisInitEndPointsForInterfaceAlt(nemuusb_state_t *pState, uint8_t uInterface, uint8_t uAlt);
LOCAL void nemuUSBSolarisDestroyAllEndPoints(nemuusb_state_t *pState);
LOCAL void nemuUSBSolarisDestroyEndPoint(nemuusb_state_t *pState, nemuusb_ep_t *pEp);
LOCAL void nemuUSBSolarisCloseAllPipes(nemuusb_state_t *pState, bool fControlPipe);
LOCAL int nemuUSBSolarisOpenPipe(nemuusb_state_t *pState, nemuusb_ep_t *pEp);
LOCAL void nemuUSBSolarisClosePipe(nemuusb_state_t *pState, nemuusb_ep_t *pEp);
LOCAL int nemuUSBSolarisCtrlXfer(nemuusb_state_t *pState, nemuusb_ep_t *pEp, nemuusb_urb_t *pUrb);
LOCAL void nemuUSBSolarisCtrlXferCompleted(usb_pipe_handle_t pPipe, usb_ctrl_req_t *pReq);
LOCAL int nemuUSBSolarisBulkXfer(nemuusb_state_t *pState, nemuusb_ep_t *pEp, nemuusb_urb_t *purb);
LOCAL void nemuUSBSolarisBulkXferCompleted(usb_pipe_handle_t pPipe, usb_bulk_req_t *pReq);
LOCAL int nemuUSBSolarisIntrXfer(nemuusb_state_t *pState, nemuusb_ep_t *pEp, nemuusb_urb_t *pUrb);
LOCAL void nemuUSBSolarisIntrXferCompleted(usb_pipe_handle_t pPipe, usb_intr_req_t *pReq);
LOCAL int nemuUSBSolarisIsocXfer(nemuusb_state_t *pState, nemuusb_ep_t *pEp, nemuusb_urb_t *pUrb);
LOCAL void nemuUSBSolarisIsocInXferCompleted(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq);
LOCAL void nemuUSBSolarisIsocInXferError(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq);
LOCAL void nemuUSBSolarisIsocOutXferCompleted(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq);
LOCAL nemuusb_urb_t *nemuUSBSolarisGetIsocInURB(nemuusb_state_t *pState, PNEMUUSBREQ_URB pUrbReq);
LOCAL nemuusb_urb_t *nemuUSBSolarisQueueURB(nemuusb_state_t *pState, PNEMUUSBREQ_URB pUrbReq, mblk_t *pMsg);
LOCAL inline void nemuUSBSolarisConcatMsg(nemuusb_urb_t *pUrb);
LOCAL inline VUSBSTATUS nemuUSBSolarisGetUrbStatus(usb_cr_t Status);
LOCAL inline void nemuUSBSolarisDeQueueURB(nemuusb_urb_t *pUrb, int URBStatus);
LOCAL inline void nemuUSBSolarisNotifyComplete(nemuusb_state_t *pState);
LOCAL int nemuUSBSolarisProcessIOCtl(int iFunction, void *pvState, int Mode, PNEMUUSBREQ pUSBReq, void *pvBuf,
                                     size_t *pcbDataOut);
LOCAL bool nemuUSBSolarisIsUSBDevice(dev_info_t *pDip);

/** @name Device Operation Hooks
 * @{ */
LOCAL int nemuUSBSolarisSendURB(nemuusb_state_t *pState, PNEMUUSBREQ_URB pUrbReq, int Mode);
LOCAL int nemuUSBSolarisReapURB(nemuusb_state_t *pState, PNEMUUSBREQ_URB pUrbReq, int Mode);
LOCAL int nemuUSBSolarisClearEndPoint(nemuusb_state_t *pState, uint8_t bEndpoint);
LOCAL int nemuUSBSolarisSetConfig(nemuusb_state_t *pState, uint8_t bCfgValue);
LOCAL int nemuUSBSolarisGetConfig(nemuusb_state_t *pState, uint8_t *pCfgValue);
LOCAL int nemuUSBSolarisSetInterface(nemuusb_state_t *pState, uint8_t uInterface, uint8_t uAlt);
LOCAL int nemuUSBSolarisCloseDevice(nemuusb_state_t *pState, NEMUUSB_RESET_LEVEL enmReset);
LOCAL int nemuUSBSolarisAbortPipe(nemuusb_state_t *pState, uint8_t bEndpoint);
LOCAL int nemuUSBSolarisGetConfigIndex(nemuusb_state_t *pState, uint_t uCfgValue);
/** @} */

/** @name Hotplug & Power Management Hooks
 * @{ */
LOCAL inline void nemuUSBSolarisNotifyHotplug(nemuusb_state_t *pState);
LOCAL int nemuUSBSolarisDeviceDisconnected(dev_info_t *pDip);
LOCAL int nemuUSBSolarisDeviceReconnected(dev_info_t *pDip);

LOCAL int nemuUSBSolarisInitPower(nemuusb_state_t *pState);
LOCAL void nemuUSBSolarisDestroyPower(nemuusb_state_t *pState);
LOCAL int nemuUSBSolarisDeviceSuspend(nemuusb_state_t *pState);
LOCAL void nemuUSBSolarisDeviceResume(nemuusb_state_t *pState);
LOCAL void nemuUSBSolarisDeviceRestore(nemuusb_state_t *pState);
LOCAL void nemuUSBSolarisPowerBusy(nemuusb_state_t *pState);
LOCAL void nemuUSBSolarisPowerIdle(nemuusb_state_t *pState);
/** @} */

/** @name Monitor Hooks
 * @{ */
int NemuUSBMonSolarisRegisterClient(dev_info_t *pClientDip, PNEMUUSB_CLIENT_INFO pClientInfo);
int NemuUSBMonSolarisUnregisterClient(dev_info_t *pClientDip);
/** @} */

/** @name Callbacks from Monitor
 * @{ */
LOCAL int nemuUSBSolarisSetConsumerCredentials(RTPROCESS Process, int Instance, void *pvReserved);
/** @} */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Global list of all device instances. */
static void *g_pNemuUSBSolarisState;

/** The default endpoint descriptor */
static usb_ep_descr_t g_NemuUSBSolarisDefaultEpDesc = {7, 5, 0, USB_EP_ATTR_CONTROL, 8, 0};

/** Hotplug events */
static usb_event_t g_NemuUSBSolarisEvents =
{
    nemuUSBSolarisDeviceDisconnected,
    nemuUSBSolarisDeviceReconnected,
    NULL,                             /* presuspend */
    NULL                              /* postresume */
};


/**
 * Kernel entry points
 */
int _init(void)
{
    LogFunc((DEVICE_NAME ":_init\n"));

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_NemuUSBSolarisModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        LogRel((DEVICE_NAME ":failed to disable autounloading!\n"));

    /*
     * Initialize IPRT R0 driver, which internally calls OS-specific r0 init.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        rc = ddi_soft_state_init(&g_pNemuUSBSolarisState, sizeof(nemuusb_state_t), 4 /* pre-alloc */);
        if (!rc)
        {
            rc = mod_install(&g_NemuUSBSolarisModLinkage);
            if (!rc)
                return rc;

            LogRel((DEVICE_NAME ":mod_install failed! rc=%d\n", rc));
            ddi_soft_state_fini(&g_pNemuUSBSolarisState);
        }
        else
            LogRel((DEVICE_NAME ":failed to initialize soft state.\n"));

        RTR0Term();
    }
    else
        LogRel((DEVICE_NAME ":RTR0Init failed! rc=%d\n", rc));
    return RTErrConvertToErrno(rc);
}


int _fini(void)
{
    int rc;

    LogFunc((DEVICE_NAME ":_fini\n"));

    rc = mod_remove(&g_NemuUSBSolarisModLinkage);
    if (!rc)
    {
        ddi_soft_state_fini(&g_pNemuUSBSolarisState);
        RTR0Term();
    }

    return rc;
}


int _info(struct modinfo *pModInfo)
{
    LogFunc((DEVICE_NAME ":_info\n"));

    return mod_info(&g_NemuUSBSolarisModLinkage, pModInfo);
}


/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @returns corresponding solaris error code.
 */
int NemuUSBSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisAttach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    int rc;
    int instance = ddi_get_instance(pDip);
    nemuusb_state_t *pState = NULL;

    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            rc = ddi_soft_state_zalloc(g_pNemuUSBSolarisState, instance);
            if (rc == DDI_SUCCESS)
            {
                pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
                if (RT_LIKELY(pState))
                {
                    pState->pDip = pDip;
                    pState->pDevDesc = NULL;
                    pState->fClosed = false;
                    pState->fRestoreCfg = false;
                    pState->fGetCfgReqDone = false;
                    bzero(pState->aEps, sizeof(pState->aEps));
                    list_create(&pState->hUrbs, sizeof(nemuusb_urb_t), offsetof(nemuusb_urb_t, hListLink));
                    list_create(&pState->hLandedUrbs, sizeof(nemuusb_urb_t), offsetof(nemuusb_urb_t, hListLink));
                    pState->cInflightUrbs = 0;
                    pState->fPoll = NEMUUSB_POLL_OFF;
                    pState->Process = NIL_RTPROCESS;
                    pState->pPower = NULL;

                    /*
                     * There is a bug in usb_client_attach() as of Nevada 120 which panics when we bind to
                     * a non-USB device. So check if we are really binding to a USB device or not.
                     */
                    if (nemuUSBSolarisIsUSBDevice(pState->pDip))
                    {
                        /*
                         * Here starts the USB specifics.
                         */
                        rc = usb_client_attach(pState->pDip, USBDRV_VERSION, 0);
                        if (rc == USB_SUCCESS)
                        {
                            /*
                             * Parse out the entire descriptor.
                             */
                            rc = usb_get_dev_data(pState->pDip, &pState->pDevDesc, USB_PARSE_LVL_ALL, 0 /* Unused */);
                            if (rc == USB_SUCCESS)
                            {
#ifdef DEBUG_ramshankar
                                usb_print_descr_tree(pState->pDip, pState->pDevDesc);
#endif

                                /*
                                 * Initialize state locks.
                                 */
                                mutex_init(&pState->Mtx, NULL, MUTEX_DRIVER, pState->pDevDesc->dev_iblock_cookie);
                                pState->StateMulti = usb_init_serialization(pState->pDip, USB_INIT_SER_CHECK_SAME_THREAD);

                                /*
                                 * Get maximum bulk transfer size supported by the HCD.
                                 */
                                rc = usb_pipe_get_max_bulk_transfer_size(pState->pDip, &pState->cbMaxBulkXfer);
                                if (rc == USB_SUCCESS)
                                {
                                    Log((DEVICE_NAME ":NemuUSBSolarisAttach cbMaxBulkXfer=%d\n", pState->cbMaxBulkXfer));

                                    /*
                                     * Initialize all endpoints.
                                     */
                                    rc = nemuUSBSolarisInitAllEndPoints(pState);
                                    if (RT_SUCCESS(rc))
                                    {
                                        /*
                                         * Set the device state.
                                         */
                                        pState->DevState = USB_DEV_ONLINE;

                                        /*
                                         * Initialize power management for the device.
                                         */
                                        rc = nemuUSBSolarisInitPower(pState);
                                        if (RT_SUCCESS(rc))
                                        {
                                            /*
                                             * Update endpoints (descriptors) for the current config.
                                             */
                                            nemuUSBSolarisInitEndPointsForConfig(pState, usb_get_current_cfgidx(pState->pDip));

                                            /*
                                             * Publish the minor node.
                                             */
                                            rc = ddi_create_priv_minor_node(pDip, DEVICE_NAME, S_IFCHR, instance, DDI_PSEUDO, 0,
                                                        "none", "none", 0666);
                                            if (RT_LIKELY(rc == DDI_SUCCESS))
                                            {
                                                /*
                                                 * Register hotplug callbacks.
                                                 */
                                                rc = usb_register_event_cbs(pState->pDip, &g_NemuUSBSolarisEvents, 0 /* flags */);
                                                if (RT_LIKELY(rc == USB_SUCCESS))
                                                {
                                                    /*
                                                     * Register with our monitor driver.
                                                     */
                                                    bzero(&pState->ClientInfo, sizeof(pState->ClientInfo));
                                                    char szDevicePath[MAXPATHLEN];
                                                    ddi_pathname(pState->pDip, szDevicePath);
                                                    RTStrPrintf(pState->ClientInfo.szClientPath,
                                                                sizeof(pState->ClientInfo.szClientPath),
                                                                "/devices%s:%s", szDevicePath,DEVICE_NAME);
                                                    RTPathStripFilename(szDevicePath);
                                                    RTStrPrintf(pState->ClientInfo.szDeviceIdent,
                                                                sizeof(pState->ClientInfo.szDeviceIdent),
                                                                "%#x:%#x:%d:%s",
                                                                pState->pDevDesc->dev_descr->idVendor,
                                                                pState->pDevDesc->dev_descr->idProduct,
                                                                pState->pDevDesc->dev_descr->bcdDevice, szDevicePath);
                                                    pState->ClientInfo.Instance = instance;
                                                    pState->ClientInfo.pfnSetConsumerCredentials = &nemuUSBSolarisSetConsumerCredentials;
                                                    rc = NemuUSBMonSolarisRegisterClient(pState->pDip, &pState->ClientInfo);
                                                    if (RT_SUCCESS(rc))
                                                    {
                                                        LogRel((DEVICE_NAME ": Captured %s %#x:%#x:%d:%s\n",
                                                                pState->pDevDesc->dev_product ? pState->pDevDesc->dev_product
                                                                    : "<Unnamed USB device>",
                                                                pState->pDevDesc->dev_descr->idVendor,
                                                                pState->pDevDesc->dev_descr->idProduct,
                                                                pState->pDevDesc->dev_descr->bcdDevice,
                                                                pState->ClientInfo.szClientPath));

                                                        return DDI_SUCCESS;
                                                    }
                                                    else
                                                    {
                                                        LogRel((DEVICE_NAME ":NemuUSBMonSolarisRegisterClient failed! rc=%d "
                                                                "path=%s instance=%d\n", rc, pState->ClientInfo.szClientPath,
                                                                instance));
                                                    }

                                                    usb_unregister_event_cbs(pState->pDip, &g_NemuUSBSolarisEvents);
                                                }
                                                else
                                                    LogRel((DEVICE_NAME ":NemuUSBSolarisAttach failed to register hotplug "
                                                            "callbacks! rc=%d\n", rc));

                                                ddi_remove_minor_node(pState->pDip, NULL);
                                            }
                                            else
                                            {
                                                LogRel((DEVICE_NAME ":NemuUSBSolarisAttach ddi_create_minor_node failed! rc=%d\n",
                                                        rc));
                                            }
                                        }
                                        else
                                        {
                                            LogRel((DEVICE_NAME ":NemuUSBSolarisAttach failed to initialize power management! "
                                                    "rc=%d\n", rc));
                                        }
                                    }
                                    else
                                    {
                                        LogRel((DEVICE_NAME ":NemuUSBSolarisAttach nemuUSBSolarisInitAllEndPoints failed! "
                                                "rc=%d\n"));
                                    }
                                }
                                else
                                {
                                    LogRel((DEVICE_NAME ":NemuUSBSolarisAttach usb_pipe_get_max_bulk_transfer_size failed! "
                                            "rc=%d\n", rc));
                                }

                                usb_fini_serialization(pState->StateMulti);
                                mutex_destroy(&pState->Mtx);
                                usb_free_dev_data(pState->pDip, pState->pDevDesc);
                            }
                            else
                                LogRel((DEVICE_NAME ":NemuUSBSolarisAttach failed to get device descriptor. rc=%d\n", rc));

                            usb_client_detach(pState->pDip, NULL);
                        }
                        else
                            LogRel((DEVICE_NAME ":NemuUSBSolarisAttach usb_client_attach failed! rc=%d\n", rc));
                    }
                    else
                    {
                        /* This would appear on every boot if it were LogRel() */
                        Log((DEVICE_NAME ":NemuUSBSolarisAttach not a USB device.\n"));
                    }
                }
                else
                    LogRel((DEVICE_NAME ":NemuUSBSolarisAttach failed to get soft state\n", sizeof(*pState)));

                ddi_soft_state_free(g_pNemuUSBSolarisState, instance);
            }
            else
                LogRel((DEVICE_NAME ":NemuUSBSolarisAttach failed to alloc soft state. rc=%d\n", rc));

            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
            pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
            if (RT_UNLIKELY(!pState))
            {
                LogRel((DEVICE_NAME ":NemuUSBSolarisAttach DDI_RESUME: failed to get soft state on detach.\n"));
                return DDI_FAILURE;
            }

            nemuUSBSolarisDeviceResume(pState);
            return DDI_SUCCESS;
        }

        default:
            return DDI_FAILURE;
    }
}


/**
 * Detach entry point, to detach a device to the system or suspend it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @returns corresponding solaris error code.
 */
int NemuUSBSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisDetach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    int instance = ddi_get_instance(pDip);
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisDetach failed to get soft state on detach.\n"));
        return DDI_FAILURE;
    }

    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            /*
             * At this point it must be assumed that the default control pipe has
             * already been closed by userland (via NemuUSBSolarisClose() entry point).
             * Once it's closed we can no longer open or reference the device here.
             */

            /*
             * Notify userland if any that we're gone (while resetting device held by us).
             */
            nemuUSBSolarisNotifyHotplug(pState);

            /*
             * Unregister hotplug callback events first without holding the mutex as the callbacks
             * would otherwise block on the mutex.
             */
            usb_unregister_event_cbs(pDip, &g_NemuUSBSolarisEvents);


            /*
             * Serialize: paranoid; drain other driver activity.
             */
            usb_serialize_access(pState->StateMulti, USB_WAIT, 0);
            usb_release_access(pState->StateMulti);
            mutex_enter(&pState->Mtx);

            /*
             * Close all endpoints.
             */
            nemuUSBSolarisCloseAllPipes(pState, true /* ControlPipe */);
            pState->fClosed = true;

            /*
             * Deinitialize power, destroy endpoints.
             */
            nemuUSBSolarisDestroyPower(pState);
            nemuUSBSolarisDestroyAllEndPoints(pState);

            /*
             * Free up all URBs.
             */
            nemuusb_urb_t *pUrb = NULL;
            while ((pUrb = list_remove_head(&pState->hUrbs)) != NULL)
            {
                if (pUrb->pMsg)
                    freemsg(pUrb->pMsg);
                RTMemFree(pUrb);
            }

            while ((pUrb = list_remove_head(&pState->hLandedUrbs)) != NULL)
            {
                if (pUrb->pMsg)
                    freemsg(pUrb->pMsg);
                RTMemFree(pUrb);
            }
            pState->cInflightUrbs = 0;
            list_destroy(&pState->hUrbs);
            list_destroy(&pState->hLandedUrbs);

            /*
             * Destroy locks, free up descriptor and detach from USBA.
             */
            mutex_exit(&pState->Mtx);
            usb_fini_serialization(pState->StateMulti);
            mutex_destroy(&pState->Mtx);

            usb_free_dev_data(pState->pDip, pState->pDevDesc);
            usb_client_detach(pState->pDip, NULL);

            /*
             * Deregister with our Monitor driver.
             */
            NemuUSBMonSolarisUnregisterClient(pState->pDip);

            ddi_remove_minor_node(pState->pDip, NULL);

            LogRel((DEVICE_NAME ": Released %s %s\n",
                    pState->pDevDesc->dev_product ? pState->pDevDesc->dev_product : "<Unnamed USB device>",
                    pState->ClientInfo.szDeviceIdent));

            ddi_soft_state_free(g_pNemuUSBSolarisState, instance);
            pState = NULL;

            return DDI_SUCCESS;
        }

        case DDI_SUSPEND:
        {
            int rc = nemuUSBSolarisDeviceSuspend(pState);
            if (RT_SUCCESS(rc))
                return DDI_SUCCESS;

            return DDI_FAILURE;
        }

        default:
            return DDI_FAILURE;
    }
}


/**
 * Info entry point, called by solaris kernel for obtaining driver info.
 *
 * @param   pDip            The module structure instance (do not use).
 * @param   enmCmd          Information request type.
 * @param   pvArg           Type specific argument.
 * @param   ppvResult       Where to store the requested info.
 *
 * @returns corresponding solaris error code.
 */
int NemuUSBSolarisGetInfo(dev_info_t *pDip, ddi_info_cmd_t enmCmd, void *pvArg, void **ppvResult)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisGetInfo\n"));

    nemuusb_state_t *pState = NULL;
    int instance = getminor((dev_t)pvArg);

    switch (enmCmd)
    {
        case DDI_INFO_DEVT2DEVINFO:
        {
            /*
             * One is to one mapping of instance & minor number as we publish only one minor node per device.
             */
            pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
            if (pState)
            {
                *ppvResult = (void *)pState->pDip;
                return DDI_SUCCESS;
            }
            else
                LogRel((DEVICE_NAME ":NemuUSBSolarisGetInfo failed to get device state.\n"));
            return DDI_FAILURE;
        }

        case DDI_INFO_DEVT2INSTANCE:
        {
            *ppvResult = (void *)(uintptr_t)instance;
            return DDI_SUCCESS;
        }

        default:
            return DDI_FAILURE;
    }
}


/**
 * Callback invoked from the Monitor driver when a VM process tries to access
 * this client instance. This determines which VM process will be allowed to
 * open and access the USB device.
 *
 * @returns  Nemu status code.
 *
 * @param    Process        The VM process performing the client info. query.
 * @param    Instance       This client instance (the one set while we register
 *                          ourselves to the Monitor driver)
 * @param    pvReserved     Reserved for future, unused.
 */
LOCAL int nemuUSBSolarisSetConsumerCredentials(RTPROCESS Process, int Instance, void *pvReserved)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisSetConsumerCredentials Process=%u Instance=%d\n", Process, Instance));
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, Instance);
    if (!pState)
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisSetConsumerCredentials failed to get device state for instance %d\n", Instance));
        return VERR_INVALID_STATE;
    }

    int rc = VINF_SUCCESS;
    mutex_enter(&pState->Mtx);

    if (pState->Process == NIL_RTPROCESS)
        pState->Process = Process;
    else
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisSetConsumerCredentials failed! Process %u already has client open.\n",
                pState->Process));
        rc = VERR_RESOURCE_BUSY;
    }

    mutex_exit(&pState->Mtx);

    return rc;
}


int NemuUSBSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisOpen pDev=%p fFlag=%d fType=%d pCred=%p\n", pDev, fFlag, fType, pCred));

    /*
     * Verify we are being opened as a character device
     */
    if (fType != OTYP_CHR)
        return EINVAL;

    /*
     * One is to one mapping. (Minor<=>Instance).
     */
    int instance = getminor((dev_t)*pDev);
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
    if (!pState)
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisOpen failed to get device state for instance %d\n", instance));
        return ENXIO;
    }

    mutex_enter(&pState->Mtx);

    /*
     * Only one user process can open a device instance at a time.
     */
    if (pState->Process != RTProcSelf())
    {
        if (pState->Process == NIL_RTPROCESS)
            LogRel((DEVICE_NAME ":NemuUSBSolarisOpen No prior information about authorized process.\n"));
        else
            LogRel((DEVICE_NAME ":NemuUSBSolarisOpen Process %u is already using this device instance.\n", pState->Process));

        mutex_exit(&pState->Mtx);
        return EPERM;
    }

    pState->fPoll = NEMUUSB_POLL_ON;

    mutex_exit(&pState->Mtx);

    NOREF(fFlag);
    NOREF(pCred);

    return 0;
}


int NemuUSBSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisClose Dev=%d fFlag=%d fType=%d pCred=%p\n", Dev, fFlag, fType, pCred));

    int instance = getminor((dev_t)Dev);
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisClose failed to get device state for instance %d\n", instance));
        return ENXIO;
    }

    mutex_enter(&pState->Mtx);
    pState->fPoll = NEMUUSB_POLL_OFF;
    pState->Process = NIL_RTPROCESS;
    mutex_exit(&pState->Mtx);

    return 0;
}


int NemuUSBSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisRead\n"));
    return ENOTSUP;
}


int NemuUSBSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisWrite\n"));
    return ENOTSUP;
}


int NemuUSBSolarisPoll(dev_t Dev, short fEvents, int fAnyYet, short *pReqEvents, struct pollhead **ppPollHead)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisPoll Dev=%d fEvents=%d fAnyYet=%d pReqEvents=%p\n", Dev, fEvents, fAnyYet, pReqEvents));

    /*
     * Get the device state (one to one mapping).
     */
    int instance = getminor((dev_t)Dev);
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisPoll: no state data for %d\n", instance));
        return ENXIO;
    }

    mutex_enter(&pState->Mtx);

    /*
     * "fEvents" HAS to be POLLIN. We won't bother to test it. The caller
     * must always requests input events. Disconnect event (POLLHUP) is invalid in "fEvents".
     */
    fEvents = 0;
    if (pState->fPoll & NEMUUSB_POLL_DEV_UNPLUGGED)
    {
        fEvents |= POLLHUP;
        pState->fPoll &= ~NEMUUSB_POLL_DEV_UNPLUGGED;
    }

    if (pState->fPoll & NEMUUSB_POLL_REAP_PENDING)
    {
        fEvents |= POLLIN;
        pState->fPoll &= ~NEMUUSB_POLL_REAP_PENDING;
    }

    if (   !fEvents
        && !fAnyYet)
    {
        *ppPollHead = &pState->PollHead;
    }

    *pReqEvents = fEvents;

    mutex_exit(&pState->Mtx);

    return 0;
}


int NemuUSBSolarisPower(dev_info_t *pDip, int Component, int Level)
{
    LogFunc((DEVICE_NAME ":NemuUSBSolarisPower pDip=%p Component=%d Level=%d\n", pDip, Component, Level));

    int instance = ddi_get_instance(pDip);
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisPower Failed! missing state.\n"));
        return DDI_FAILURE;
    }

    if (!pState->pPower)
        return DDI_SUCCESS;

    usb_serialize_access(pState->StateMulti, USB_WAIT, 0);
    mutex_enter(&pState->Mtx);

    int rc = USB_FAILURE;
    if (pState->DevState == USB_DEV_ONLINE)
    {
        /*
         * Check if we are transitioning to a valid power state.
         */
        if (!USB_DEV_PWRSTATE_OK(pState->pPower->PowerStates, Level))
        {
            switch (Level)
            {
                case USB_DEV_OS_PWR_OFF:
                {
                    if (pState->pPower->PowerBusy)
                        break;

                    /*
                     * USB D3 command.
                     */
                    pState->pPower->PowerLevel = USB_DEV_OS_PWR_OFF;
                    mutex_exit(&pState->Mtx);
                    rc = usb_set_device_pwrlvl3(pDip);
                    mutex_enter(&pState->Mtx);
                    break;
                }

                case USB_DEV_OS_FULL_PWR:
                {
                    /*
                     * Can happen during shutdown of the OS.
                     */
                    pState->pPower->PowerLevel = USB_DEV_OS_FULL_PWR;
                    mutex_exit(&pState->Mtx);
                    rc = usb_set_device_pwrlvl0(pDip);
                    mutex_enter(&pState->Mtx);
                    break;
                }

                default:    /* Power levels 1, 2 not implemented */
                    break;
            }
        }
        else
            Log((DEVICE_NAME ":USB_DEV_PWRSTATE_OK failed.\n"));
    }
    else
        rc = USB_SUCCESS;

    mutex_exit(&pState->Mtx);
    usb_release_access(pState->StateMulti);
    return rc == USB_SUCCESS ? DDI_SUCCESS : DDI_FAILURE;
}


/** @def IOCPARM_LEN
 * Gets the length from the ioctl number.
 * This is normally defined by sys/ioccom.h on BSD systems...
 */
#ifndef IOCPARM_LEN
# define IOCPARM_LEN(Code)                      (((Code) >> 16) & IOCPARM_MASK)
#endif

int NemuUSBSolarisIOCtl(dev_t Dev, int Cmd, intptr_t pArg, int Mode, cred_t *pCred, int *pVal)
{
/*    LogFunc((DEVICE_NAME ":NemuUSBSolarisIOCtl Dev=%d Cmd=%d pArg=%p Mode=%d\n", Dev, Cmd, pArg)); */

    /*
     * Get the device state (one to one mapping).
     */
    int instance = getminor((dev_t)Dev);
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);
    if (RT_UNLIKELY(!pState))
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisIOCtl: no state data for %d\n", instance));
        return EINVAL;
    }

    /*
     * Read the request wrapper.
     */
    NEMUUSBREQ ReqWrap;
    if (IOCPARM_LEN(Cmd) != sizeof(ReqWrap))
    {
        LogRel((DEVICE_NAME ": NemuUSBSolarisIOCtl: bad request %#x size=%d expected=%d\n", Cmd, IOCPARM_LEN(Cmd),
                sizeof(ReqWrap)));
        return ENOTTY;
    }

    int rc = ddi_copyin((void *)pArg, &ReqWrap, sizeof(ReqWrap), Mode);
    if (RT_UNLIKELY(rc))
    {
        LogRel((DEVICE_NAME ": NemuUSBSolarisIOCtl: ddi_copyin failed to read header pArg=%p Cmd=%d. rc=%d.\n", pArg, Cmd, rc));
        return EINVAL;
    }

    if (ReqWrap.u32Magic != NEMUUSB_MAGIC)
    {
        LogRel((DEVICE_NAME ": NemuUSBSolarisIOCtl: bad magic %#x; pArg=%p Cmd=%d.\n", ReqWrap.u32Magic, pArg, Cmd));
        return EINVAL;
    }
    if (RT_UNLIKELY(   ReqWrap.cbData == 0
                    || ReqWrap.cbData > _1M*16))
    {
        LogRel((DEVICE_NAME ": NemuUSBSolarisIOCtl: bad size %#x; pArg=%p Cmd=%d.\n", ReqWrap.cbData, pArg, Cmd));
        return EINVAL;
    }

    /*
     * Read the request.
     */
    void *pvBuf = RTMemTmpAlloc(ReqWrap.cbData);
    if (RT_UNLIKELY(!pvBuf))
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisIOCtl: RTMemTmpAlloc failed to alloc %d bytes.\n", ReqWrap.cbData));
        return ENOMEM;
    }

    rc = ddi_copyin((void *)(uintptr_t)ReqWrap.pvDataR3, pvBuf, ReqWrap.cbData, Mode);
    if (RT_UNLIKELY(rc))
    {
        RTMemTmpFree(pvBuf);
        LogRel((DEVICE_NAME ":NemuUSBSolarisIOCtl: ddi_copyin failed; pvBuf=%p pArg=%p Cmd=%d. rc=%d\n", pvBuf, pArg, Cmd, rc));
        return EFAULT;
    }
    if (RT_UNLIKELY(   ReqWrap.cbData == 0
                    || pvBuf == NULL))
    {
        RTMemTmpFree(pvBuf);
        LogRel((DEVICE_NAME ":NemuUSBSolarisIOCtl: invalid request pvBuf=%p cbData=%d\n", pvBuf, ReqWrap.cbData));
        return EINVAL;
    }

    /*
     * Process the IOCtl.
     */
    size_t cbDataOut = 0;
    rc = nemuUSBSolarisProcessIOCtl(Cmd, pState, Mode, &ReqWrap, pvBuf, &cbDataOut);
    ReqWrap.rc = rc;
    rc = 0;

    if (RT_UNLIKELY(cbDataOut > ReqWrap.cbData))
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisIOCtl: too much output data %d expected %d Truncating!\n", cbDataOut,
                ReqWrap.cbData));
        cbDataOut = ReqWrap.cbData;
    }

    ReqWrap.cbData = cbDataOut;

    /*
     * Copy NEMUUSBREQ back to userspace (which contains rc for USB operation).
     */
    rc = ddi_copyout(&ReqWrap, (void *)pArg, sizeof(ReqWrap), Mode);
    if (RT_LIKELY(!rc))
    {
        /*
         * Copy payload (if any) back to userspace.
         */
        if (cbDataOut > 0)
        {
            rc = ddi_copyout(pvBuf, (void *)(uintptr_t)ReqWrap.pvDataR3, cbDataOut, Mode);
            if (RT_UNLIKELY(rc))
            {
                LogRel((DEVICE_NAME ":NemuUSBSolarisIOCtl: ddi_copyout failed; pvBuf=%p pArg=%p Cmd=%d. rc=%d\n", pvBuf, pArg,
                        Cmd, rc));
                rc = EFAULT;
            }
        }
    }
    else
    {
        LogRel((DEVICE_NAME ":NemuUSBSolarisIOCtl: ddi_copyout(1)failed; pReqWrap=%p pArg=%p Cmd=%d. rc=%d\n", &ReqWrap, pArg,
                Cmd, rc));
        rc = EFAULT;
    }

    *pVal = rc;
    RTMemTmpFree(pvBuf);
    return rc;
}


/**
 * IOCtl processor for user to kernel and kernel to kernel communication.
 *
 * @returns  Nemu status code.
 *
 * @param   iFunction           The requested function.
 * @param   pvState             The USB device instance.
 * @param   Mode                The IOCtl mode.
 * @param   pUSBReq             Pointer to the NEMUUSB request.
 * @param   pvBuf               Pointer to the ring-3 URB.
 * @param   pcbDataOut          Where to store the IOCtl OUT data size.
 */
LOCAL int nemuUSBSolarisProcessIOCtl(int iFunction, void *pvState, int Mode, PNEMUUSBREQ pUSBReq, void *pvBuf, size_t *pcbDataOut)
{
//    LogFunc((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl iFunction=%d pvState=%p pUSBReq=%p\n", iFunction, pvState, pUSBReq));

    AssertPtrReturn(pvState, VERR_INVALID_PARAMETER);
    nemuusb_state_t *pState = (nemuusb_state_t *)pvState;
    size_t cbData = pUSBReq->cbData;
    int rc;

#define CHECKRET_MIN_SIZE(mnemonic, cbMin) \
    do { \
        if (cbData < (cbMin)) \
        { \
            LogRel((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: " mnemonic ": cbData=%#zx (%zu) min is %#zx (%zu)\n", \
                 cbData, cbData, (size_t)(cbMin), (size_t)(cbMin))); \
            return VERR_BUFFER_OVERFLOW; \
        } \
        if ((cbMin) != 0 && !VALID_PTR(pvBuf)) \
        { \
            LogRel((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: " mnemonic ": Invalid pointer %p\n", pvBuf)); \
            return VERR_INVALID_PARAMETER; \
        } \
    } while (0)

    switch (iFunction)
    {
        case NEMUUSB_IOCTL_SEND_URB:
        {
            CHECKRET_MIN_SIZE("SEND_URB", sizeof(NEMUUSBREQ_URB));

            PNEMUUSBREQ_URB pUrbReq = (PNEMUUSBREQ_URB)pvBuf;
            rc = nemuUSBSolarisSendURB(pState, pUrbReq, Mode);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: SEND_URB returned %d\n", rc));
            break;
        }

        case NEMUUSB_IOCTL_REAP_URB:
        {
            CHECKRET_MIN_SIZE("REAP_URB", sizeof(NEMUUSBREQ_URB));

            PNEMUUSBREQ_URB pUrbReq = (PNEMUUSBREQ_URB)pvBuf;
            rc = nemuUSBSolarisReapURB(pState, pUrbReq, Mode);
            *pcbDataOut = sizeof(NEMUUSBREQ_URB);
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: REAP_URB returned %d\n", rc));
            break;
        }

        case NEMUUSB_IOCTL_CLEAR_EP:
        {
            CHECKRET_MIN_SIZE("CLEAR_EP", sizeof(NEMUUSBREQ_CLEAR_EP));

            PNEMUUSBREQ_CLEAR_EP pClearEpReq = (PNEMUUSBREQ_CLEAR_EP)pvBuf;
            rc = nemuUSBSolarisClearEndPoint(pState, pClearEpReq->bEndpoint);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: CLEAR_EP returned %d\n", rc));
            break;
        }

        case NEMUUSB_IOCTL_SET_CONFIG:
        {
            CHECKRET_MIN_SIZE("SET_CONFIG", sizeof(NEMUUSBREQ_SET_CONFIG));

            PNEMUUSBREQ_SET_CONFIG pSetCfgReq = (PNEMUUSBREQ_SET_CONFIG)pvBuf;
            rc = nemuUSBSolarisSetConfig(pState, pSetCfgReq->bConfigValue);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: SET_CONFIG returned %d\n", rc));
            break;
        }

        case NEMUUSB_IOCTL_SET_INTERFACE:
        {
            CHECKRET_MIN_SIZE("SET_INTERFACE", sizeof(NEMUUSBREQ_SET_INTERFACE));

            PNEMUUSBREQ_SET_INTERFACE pSetInterfaceReq = (PNEMUUSBREQ_SET_INTERFACE)pvBuf;
            rc = nemuUSBSolarisSetInterface(pState, pSetInterfaceReq->bInterface, pSetInterfaceReq->bAlternate);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: SET_INTERFACE returned %d\n", rc));
            break;
        }

        case NEMUUSB_IOCTL_CLOSE_DEVICE:
        {
            CHECKRET_MIN_SIZE("CLOSE_DEVICE", sizeof(NEMUUSBREQ_CLOSE_DEVICE));

            PNEMUUSBREQ_CLOSE_DEVICE pCloseDeviceReq = (PNEMUUSBREQ_CLOSE_DEVICE)pvBuf;
            rc = nemuUSBSolarisCloseDevice(pState, pCloseDeviceReq->ResetLevel);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: CLOSE_DEVICE returned %d\n", rc));
            break;
        }

        case NEMUUSB_IOCTL_ABORT_PIPE:
        {
            CHECKRET_MIN_SIZE("ABORT_PIPE", sizeof(NEMUUSBREQ_ABORT_PIPE));

            PNEMUUSBREQ_ABORT_PIPE pAbortPipeReq = (PNEMUUSBREQ_ABORT_PIPE)pvBuf;
            rc = nemuUSBSolarisAbortPipe(pState, pAbortPipeReq->bEndpoint);
            *pcbDataOut = 0;
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: ABORT_PIPE returned %d\n", rc));
            break;
        }

        case NEMUUSB_IOCTL_GET_CONFIG:
        {
            CHECKRET_MIN_SIZE("GET_CONFIG", sizeof(NEMUUSBREQ_GET_CONFIG));

            PNEMUUSBREQ_GET_CONFIG pGetCfgReq = (PNEMUUSBREQ_GET_CONFIG)pvBuf;
            rc = nemuUSBSolarisGetConfig(pState, &pGetCfgReq->bConfigValue);
            *pcbDataOut = sizeof(NEMUUSBREQ_GET_CONFIG);
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: GET_CONFIG returned %d\n", rc));
            break;
        }

        case NEMUUSB_IOCTL_GET_VERSION:
        {
            CHECKRET_MIN_SIZE("GET_VERSION", sizeof(NEMUUSBREQ_GET_VERSION));

            PNEMUUSBREQ_GET_VERSION pGetVersionReq = (PNEMUUSBREQ_GET_VERSION)pvBuf;
            pGetVersionReq->u32Major = NEMUUSB_VERSION_MAJOR;
            pGetVersionReq->u32Minor = NEMUUSB_VERSION_MINOR;
            *pcbDataOut = sizeof(NEMUUSBREQ_GET_VERSION);
            rc = VINF_SUCCESS;
            Log((DEVICE_NAME ":nemuUSBSolarisProcessIOCtl: GET_VERSION returned %d\n", rc));
            break;
        }

        default:
        {
            LogRel((DEVICE_NAME ":solarisUSBProcessIOCtl: Unknown request %#x\n", iFunction));
            rc = VERR_NOT_SUPPORTED;
            *pcbDataOut = 0;
            break;
        }
    }

    pUSBReq->cbData = *pcbDataOut;
    return rc;
}


/**
 * Initialize device power management functions.
 *
 * @param   pState          The USB device instance.
 *
 * @returns Nemu status code.
 */
LOCAL int nemuUSBSolarisInitPower(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisInitPower pState=%p\n", pState));

    int rc = usb_handle_remote_wakeup(pState->pDip, USB_REMOTE_WAKEUP_ENABLE);
    if (rc == USB_SUCCESS)
    {
        nemuusb_power_t *pPower = RTMemAlloc(sizeof(nemuusb_power_t));
        if (RT_LIKELY(pPower))
        {
            mutex_enter(&pState->Mtx);
            pState->pPower = pPower;
            pState->pPower->fPowerWakeup = false;
            mutex_exit(&pState->Mtx);

            uint_t PowerStates;
            rc = usb_create_pm_components(pState->pDip, &PowerStates);
            if (rc == USB_SUCCESS)
            {
                pState->pPower->fPowerWakeup = true;
                pState->pPower->PowerLevel = USB_DEV_OS_FULL_PWR;
                pState->pPower->PowerStates = PowerStates;

                rc = pm_raise_power(pState->pDip, 0 /* component */, USB_DEV_OS_FULL_PWR);

                if (rc != DDI_SUCCESS)
                {
                    LogRel((DEVICE_NAME ":nemuUSBSolarisInitPower failed to raise power level usb(%#x,%#x).\n",
                            pState->pDevDesc->dev_descr->idVendor, pState->pDevDesc->dev_descr->idProduct));
                }
            }
            else
                Log((DEVICE_NAME ":nemuUSBSolarisInitPower failed to create power components.\n"));

            return VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        Log((DEVICE_NAME ":nemuUSBSolarisInitPower failed to enable remote wakeup. No PM.\n"));
        rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Destroy device power management functions.
 *
 * @param   pState          The USB device instance.
 * @remarks Requires the device state mutex to be held.
 *
 * @returns Nemu status code.
 */
LOCAL void nemuUSBSolarisDestroyPower(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDestroyPower pState=%p\n", pState));

    if (pState->pPower)
    {
        mutex_exit(&pState->Mtx);
        nemuUSBSolarisPowerBusy(pState);
        mutex_enter(&pState->Mtx);

        int rc = -1;
        if (   pState->pPower->fPowerWakeup
            && pState->DevState != USB_DEV_DISCONNECTED)
        {
            mutex_exit(&pState->Mtx);
            rc = pm_raise_power(pState->pDip, 0 /* component */, USB_DEV_OS_FULL_PWR);
            if (rc != DDI_SUCCESS)
                Log((DEVICE_NAME ":nemuUSBSolarisDestroyPower raising power failed! rc=%d\n", rc));

            rc = usb_handle_remote_wakeup(pState->pDip, USB_REMOTE_WAKEUP_DISABLE);
            if (rc != DDI_SUCCESS)
                Log((DEVICE_NAME ":nemuUSBSolarisDestroyPower failed to disable remote wakeup.\n"));
        }
        else
            mutex_exit(&pState->Mtx);

        rc = pm_lower_power(pState->pDip, 0 /* component */, USB_DEV_OS_PWR_OFF);
        if (rc != DDI_SUCCESS)
            Log((DEVICE_NAME ":nemuUSBSolarisDestroyPower lowering power failed! rc=%d\n", rc));

        nemuUSBSolarisPowerIdle(pState);
        mutex_enter(&pState->Mtx);
        RTMemFree(pState->pPower);
        pState->pPower = NULL;
    }
}


/**
 * Convert Solaris' USBA URB status to Nemu's USB URB status.
 *
 * @param   Status          Solaris USBA USB URB status.
 *
 * @returns Nemu USB URB status.
 */
LOCAL inline VUSBSTATUS nemuUSBSolarisGetUrbStatus(usb_cr_t Status)
{
    switch (Status)
    {
        case USB_CR_OK:                 return VUSBSTATUS_OK;
        case USB_CR_CRC:                return VUSBSTATUS_CRC;
        case USB_CR_DEV_NOT_RESP:       return VUSBSTATUS_DNR;
        case USB_CR_DATA_UNDERRUN:      return VUSBSTATUS_DATA_UNDERRUN;
        case USB_CR_DATA_OVERRUN:       return VUSBSTATUS_DATA_OVERRUN;
        case USB_CR_STALL:              return VUSBSTATUS_STALL;
        /*
        case USB_CR_BITSTUFFING:
        case USB_CR_DATA_TOGGLE_MM:
        case USB_CR_PID_CHECKFAILURE:
        case USB_CR_UNEXP_PID:
        case USB_CR_BUFFER_OVERRUN:
        case USB_CR_BUFFER_UNDERRUN:
        case USB_CR_TIMEOUT:
        case USB_CR_NOT_ACCESSED:
        case USB_CR_NO_RESOURCES:
        case USB_CR_UNSPECIFIED_ERR:
        case USB_CR_STOPPED_POLLING:
        case USB_CR_PIPE_CLOSING:
        case USB_CR_PIPE_RESET:
        case USB_CR_NOT_SUPPORTED:
        case USB_CR_FLUSHED:
        case USB_CR_HC_HARDWARE_ERR:
        */
        default:                        return VUSBSTATUS_INVALID;
    }
}


/**
 * Convert Solaris' USBA error code to Nemu's error code.
 *
 * @param   UsbRc           Solaris USBA error code.
 *
 * @returns Nemu error code.
 */
static inline int nemuUSBSolarisToNemuRC(int UsbRc)
{
    switch (UsbRc)
    {
        case USB_SUCCESS:           return VINF_SUCCESS;
        case USB_INVALID_ARGS:      return VERR_INVALID_PARAMETER;
        case USB_INVALID_PIPE:      return VERR_BAD_PIPE;
        case USB_INVALID_CONTEXT:   return VERR_INVALID_CONTEXT;
        case USB_BUSY:              return VERR_PIPE_BUSY;
        case USB_PIPE_ERROR:        return VERR_PIPE_IO_ERROR;
        /*
        case USB_FAILURE:
        case USB_NO_RESOURCES:
        case USB_NO_BANDWIDTH:
        case USB_NOT_SUPPORTED:
        case USB_PIPE_ERROR:
        case USB_NO_FRAME_NUMBER:
        case USB_INVALID_START_FRAME:
        case USB_HC_HARDWARE_ERROR:
        case USB_INVALID_REQUEST:
        case USB_INVALID_VERSION:
        case USB_INVALID_PERM:
        */
        default:                    return VERR_GENERAL_FAILURE;
    }
}


/**
 * Convert Solaris' USBA device state to Nemu's error code.
 *
 * @param   uDeviceState        The USB device state to convert.
 *
 * @returns Nemu error code.
 */
static inline int nemuUSBSolarisDeviceState(uint8_t uDeviceState)
{
    switch (uDeviceState)
    {
        case USB_DEV_ONLINE:        return VINF_SUCCESS;
        case USB_DEV_SUSPENDED:     return VERR_VUSB_DEVICE_IS_SUSPENDED;
        case USB_DEV_DISCONNECTED:
        case USB_DEV_PWRED_DOWN:    return VERR_VUSB_DEVICE_NOT_ATTACHED;
        default:                    return VERR_GENERAL_FAILURE;
    }
}


/**
 * Check if the device is a USB device.
 *
 * @param   pDip            Pointer to this device info. structure.
 *
 * @returns If this is really a USB device returns true, otherwise false.
 */
LOCAL bool nemuUSBSolarisIsUSBDevice(dev_info_t *pDip)
{
    int rc = DDI_FAILURE;

    /*
     * Check device for "usb" compatible property, root hubs->device would likely mean parent has no "usb" property.
     */
    char **ppszCompatible = NULL;
    uint_t cCompatible;
    rc = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, pDip, DDI_PROP_DONTPASS, "compatible", &ppszCompatible, &cCompatible);
    if (RT_LIKELY(rc == DDI_PROP_SUCCESS))
    {
        while (cCompatible--)
        {
            Log((DEVICE_NAME ":nemuUSBSolarisIsUSBDevice compatible[%d]=%s\n", cCompatible, ppszCompatible[cCompatible]));
            if (!strncmp(ppszCompatible[cCompatible], "usb", 3))
            {
                Log((DEVICE_NAME ":nemuUSBSolarisIsUSBDevice verified device as USB. pszCompatible=%s\n",
                     ppszCompatible[cCompatible]));
                ddi_prop_free(ppszCompatible);
                return true;
            }
        }

        ddi_prop_free(ppszCompatible);
        ppszCompatible = NULL;
    }
    else
        Log((DEVICE_NAME ":nemuUSBSolarisIsUSBDevice USB property lookup failed. rc=%d\n", rc));

    /*
     * Check parent for "usb" compatible property.
     */
    dev_info_t *pParentDip = ddi_get_parent(pDip);
    if (pParentDip)
    {
        rc = ddi_prop_lookup_string_array(DDI_DEV_T_ANY, pParentDip, DDI_PROP_DONTPASS, "compatible", &ppszCompatible,
                                          &cCompatible);
        if (RT_LIKELY(rc == DDI_PROP_SUCCESS))
        {
            while (cCompatible--)
            {
                Log((DEVICE_NAME ":nemuUSBSolarisIsUSBDevice parent compatible[%d]=%s\n", cCompatible,
                     ppszCompatible[cCompatible]));
                if (!strncmp(ppszCompatible[cCompatible], "usb", 3))
                {
                    Log((DEVICE_NAME ":nemuUSBSolarisIsUSBDevice verified device as USB. parent pszCompatible=%s\n",
                            ppszCompatible[cCompatible]));
                    ddi_prop_free(ppszCompatible);
                    return true;
                }
            }

            ddi_prop_free(ppszCompatible);
            ppszCompatible = NULL;
        }
        else
            Log((DEVICE_NAME ":nemuUSBSolarisIsUSBDevice USB parent property lookup failed. rc=%d\n", rc));
    }
    else
        Log((DEVICE_NAME ":nemuUSBSolarisIsUSBDevice failed to obtain parent device for property lookup.\n"));

    return false;
}


/**
 * Submit a URB.
 *
 * @param   pState          The USB device instance.
 * @param   pUrbReq         Pointer to the Nemu USB URB.
 * @param   Mode            The IOCtl mode.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisSendURB(nemuusb_state_t *pState, PNEMUUSBREQ_URB pUrbReq, int Mode)
{
    uchar_t EndPtIndex = usb_get_ep_index(pUrbReq->bEndpoint);
    nemuusb_ep_t *pEp = &pState->aEps[EndPtIndex];
    AssertPtrReturn(pEp, VERR_INVALID_POINTER);

    /* LogFunc((DEVICE_NAME ":nemuUSBSolarisSendUrb pState=%p pUrbReq=%p bEndpoint=%#x[%d] enmDir=%#x enmType=%#x cbData=%d pvData=%p\n",
            pState, pUrbReq, pUrbReq->bEndpoint, EndPtIndex, pUrbReq->enmDir, pUrbReq->enmType, pUrbReq->cbData, pUrbReq->pvData)); */

    if (RT_UNLIKELY(!pUrbReq->pvData))
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisSendUrb Invalid request. No data.\n"));
        return VERR_INVALID_POINTER;
    }

    /*
     * Allocate message block & copy userspace buffer for host to device Xfers and for
     * Control Xfers (since input has Setup header that needs copying).
     */
    mblk_t *pMsg = NULL;
    int rc = VINF_SUCCESS;
    if (   pUrbReq->enmDir == VUSBDIRECTION_OUT
        || pUrbReq->enmType == VUSBXFERTYPE_MSG)
    {
        pMsg = allocb(pUrbReq->cbData, BPRI_HI);
        if (RT_UNLIKELY(!pMsg))
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisSendUrb: failed to allocate %d bytes\n", pUrbReq->cbData));
            return VERR_NO_MEMORY;
        }

        rc = ddi_copyin(pUrbReq->pvData, pMsg->b_wptr, pUrbReq->cbData, Mode);
        if (RT_UNLIKELY(rc))
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisSendUrb: ddi_copyin failed! rc=%d\n", rc));
            freemsg(pMsg);
            return VERR_NO_MEMORY;
        }

        pMsg->b_wptr += pUrbReq->cbData;
    }

    mutex_enter(&pState->Mtx);
    rc = nemuUSBSolarisDeviceState(pState->DevState);

    if (pState->fClosed)    /* Required for Isoc. IN Xfers which don't Xfer through the pipe after polling starts */
        rc = VERR_VUSB_DEVICE_NOT_ATTACHED;

    if (RT_SUCCESS(rc))
    {
        /*
         * Open the pipe if needed.
         */
        rc = nemuUSBSolarisOpenPipe(pState, pEp);
        if (RT_UNLIKELY(RT_FAILURE(rc)))
        {
            mutex_exit(&pState->Mtx);
            freemsg(pMsg);
            LogRel((DEVICE_NAME ":nemuUSBSolarisSendUrb OpenPipe failed. pState=%p pUrbReq=%p bEndpoint=%#x enmDir=%#x "
                    "enmType=%#x cbData=%d pvData=%p rc=%d\n", pState, pUrbReq, pUrbReq->bEndpoint, pUrbReq->enmDir,
                    pUrbReq->enmType, pUrbReq->cbData, pUrbReq->pvData, rc));
            return VERR_BAD_PIPE;
        }

        mutex_exit(&pState->Mtx);

        nemuusb_urb_t *pUrb = NULL;
        if (   pUrbReq->enmType == VUSBXFERTYPE_ISOC
            && pUrbReq->enmDir == VUSBDIRECTION_IN)
            pUrb = nemuUSBSolarisGetIsocInURB(pState, pUrbReq);
        else
            pUrb = nemuUSBSolarisQueueURB(pState, pUrbReq, pMsg);

        if (RT_LIKELY(pUrb))
        {
            switch (pUrb->enmType)
            {
                case VUSBXFERTYPE_MSG:
                {
                    rc = nemuUSBSolarisCtrlXfer(pState, pEp, pUrb);
                    break;
                }

                case VUSBXFERTYPE_BULK:
                {
                    rc = nemuUSBSolarisBulkXfer(pState, pEp, pUrb);
                    break;
                }

                case VUSBXFERTYPE_INTR:
                {
                    rc = nemuUSBSolarisIntrXfer(pState, pEp, pUrb);
                    break;
                }

                case VUSBXFERTYPE_ISOC:
                {
                    rc = nemuUSBSolarisIsocXfer(pState, pEp, pUrb);
                    break;
                }

                default:
                {
                    rc = VERR_NOT_SUPPORTED;
                    break;
                }
            }

            if (RT_FAILURE(rc))
            {
                /** @todo We share the state mutex for protecting concurrent accesses to both
                 *        the inflight URB list as well as pUrb->pMsg (data). Probably make this
                 *        more fine grained later by having a different mutex for the URB if
                 *        it's really worth the trouble. */
                mutex_enter(&pState->Mtx);
                if (pUrb->pMsg)
                {
                    freemsg(pUrb->pMsg);
                    pUrb->pMsg = NULL;
                }

                if (   pUrb->enmType == VUSBXFERTYPE_ISOC
                    && pUrb->enmDir  == VUSBDIRECTION_IN)
                {
                    RTMemFree(pUrb);
                    pUrb = NULL;
                }
                else
                {
                    pUrb->pMsg = NULL;
                    pUrb->enmState = NEMUUSB_URB_STATE_FREE;
                }
                mutex_exit(&pState->Mtx);
            }
        }
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisSendUrb failed to queue URB.\n"));
            rc = VERR_NO_MEMORY;
        }

        if (   RT_FAILURE(rc)
            && pUrb)
        {
            if (   pUrb->enmType != VUSBXFERTYPE_ISOC
                || pUrb->enmDir  != VUSBDIRECTION_IN)
            {
                mutex_enter(&pState->Mtx);
                pState->cInflightUrbs--;
                mutex_exit(&pState->Mtx);
            }
        }
    }
    else
    {
        mutex_exit(&pState->Mtx);
        freemsg(pMsg);
    }

    return rc;
}


/**
 * Reap a completed/error'd URB.
 *
 * @param   pState          The USB device instance.
 * @param   pUrbReq         Pointer to the Nemu USB URB.
 * @param   Mode            The IOCtl mode.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisReapURB(nemuusb_state_t *pState, PNEMUUSBREQ_URB pUrbReq, int Mode)
{
//    LogFunc((DEVICE_NAME ":nemuUSBSolarisReapUrb pState=%p pUrbReq=%p\n", pState, pUrbReq));

    AssertPtrReturn(pUrbReq, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    mutex_enter(&pState->Mtx);
    rc = nemuUSBSolarisDeviceState(pState->DevState);
    if (pState->fClosed)
        rc = VERR_VUSB_DEVICE_NOT_ATTACHED;
    if (RT_SUCCESS(rc))
    {
        nemuusb_urb_t *pUrb = list_remove_head(&pState->hLandedUrbs);

        /*
         * It is safe to access pUrb->pMsg outside the state mutex because this is from the landed URB list
         * and not the inflight URB list.
         */
        mutex_exit(&pState->Mtx);
        if (pUrb)
        {
            /*
             * Copy the URB which will then be copied to user-space.
             */
            pUrbReq->pvUrbR3   = pUrb->pvUrbR3;
            pUrbReq->bEndpoint = pUrb->bEndpoint;
            pUrbReq->enmType   = pUrb->enmType;
            pUrbReq->enmDir    = pUrb->enmDir;
            pUrbReq->enmStatus = pUrb->enmStatus;

            if (RT_LIKELY(pUrb->pMsg))
            {
                /*
                 * Chain copy the message back into the user buffer.
                 */
                if (RT_LIKELY(pUrb->pvDataR3 != NIL_RTR3PTR))
                {
                    size_t cbData = RT_MIN(msgdsize(pUrb->pMsg), pUrb->cbDataR3);
                    pUrbReq->cbData = cbData;
                    pUrbReq->pvData = (void *)pUrb->pvDataR3;

                    /*
                     * Paranoia: we should have a single message block almost always.
                     */
                    if (RT_LIKELY(   !pUrb->pMsg->b_cont
                                  && cbData))
                    {
                        rc = ddi_copyout(pUrb->pMsg->b_rptr, (void *)pUrbReq->pvData, cbData, Mode);
                        if (RT_UNLIKELY(rc))
                        {
                            LogRel((DEVICE_NAME ":nemuUSBSolarisReapUrb ddi_copyout failed! rc=%d\n", rc));
                            pUrbReq->enmStatus = VUSBSTATUS_INVALID;
                        }
                    }
                    else
                    {
                        RTR3PTR pvDataR3 = pUrb->pvDataR3;
                        mblk_t *pMsg = pUrb->pMsg;
                        while (pMsg)
                        {
                            size_t cbMsg = MBLKL(pMsg);
                            if (cbMsg > 0)
                            {
                                rc = ddi_copyout(pMsg->b_rptr, (void *)pvDataR3, cbMsg, Mode);
                                if (RT_UNLIKELY(rc != 0))
                                {
                                    LogRel((DEVICE_NAME ":nemuUSBSolarisReapUrb ddi_copyout (2) failed! rc=%d\n", rc));
                                    pUrbReq->enmStatus = VUSBSTATUS_INVALID;
                                    break;
                                }
                            }

                            pMsg = pMsg->b_cont;
                            pvDataR3 += cbMsg;
                            if ((pvDataR3 - pUrb->pvDataR3) >= cbData)
                                break;
                        }
                    }

                    Log((DEVICE_NAME ":nemuUSBSolarisReapUrb pvUrbR3=%p pvDataR3=%p cbData=%d\n", pUrbReq->pvUrbR3,
                         pUrbReq->pvData, pUrbReq->cbData));
                }
                else
                {
                    pUrbReq->cbData = 0;
                    rc = VERR_INVALID_POINTER;
                    Log((DEVICE_NAME ":nemuUSBSolarisReapUrb missing pvDataR3!!\n"));
                }

                /*
                 * Free buffer allocated in NEMUUSB_IOCTL_SEND_URB.
                 */
                freemsg(pUrb->pMsg);
                pUrb->pMsg = NULL;
            }
            else
            {
                if (pUrb->enmType == VUSBXFERTYPE_ISOC)
                {
                    if (pUrb->enmDir == VUSBDIRECTION_OUT)
                        pUrbReq->cbData = pUrb->cbDataR3;
                    else
                    {
                        pUrbReq->enmStatus = VUSBSTATUS_INVALID;
                        pUrbReq->cbData = 0;
                    }
                }
                else
                {
                    Log((DEVICE_NAME ":nemuUSBSolarisReapUrb missing message.\n"));
                    pUrbReq->cbData = 0;
                }
            }

            /*
             * Copy Isoc packet descriptors.
             */
            if (pUrb->enmType == VUSBXFERTYPE_ISOC)
            {
                AssertCompile(sizeof(pUrbReq->aIsocPkts) == sizeof(pUrb->aIsocPkts));
                pUrbReq->cIsocPkts = pUrb->cIsocPkts;

                for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
                {
                    pUrbReq->aIsocPkts[i].cbPkt     = pUrb->aIsocPkts[i].cbPkt;
                    pUrbReq->aIsocPkts[i].cbActPkt  = pUrb->aIsocPkts[i].cbActPkt;
                    pUrbReq->aIsocPkts[i].enmStatus = pUrb->aIsocPkts[i].enmStatus;
                }

                if (pUrb->enmDir == VUSBDIRECTION_IN)
                {
                    RTMemFree(pUrb);
                    pUrb = NULL;
                }
            }

            if (pUrb)
            {
                /*
                 * Add URB back to the head of the free/inflight list.
                 */
                pUrb->cbDataR3 = 0;
                pUrb->pvDataR3 = NIL_RTR3PTR;
                pUrb->enmState = NEMUUSB_URB_STATE_FREE;
                mutex_enter(&pState->Mtx);
                list_insert_head(&pState->hUrbs, pUrb);
                mutex_exit(&pState->Mtx);
            }
        }
        else
            pUrbReq->pvUrbR3 = NULL;
    }
    else
        mutex_exit(&pState->Mtx);

    return rc;
}


/**
 * Clear a pipe (CLEAR_FEATURE).
 *
 * @param   pState          The USB device instance.
 * @param   bEndpoint       The Endpoint address.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisClearEndPoint(nemuusb_state_t *pState, uint8_t bEndpoint)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisClearEndPoint pState=%p bEndpoint=%#x\n", pState, bEndpoint));

    /*
     * Serialize access: single threaded per Endpoint, one request at a time.
     */
    mutex_enter(&pState->Mtx);
    int rc = nemuUSBSolarisDeviceState(pState->DevState);
    if (RT_SUCCESS(rc))
    {
        uchar_t EndPtIndex = usb_get_ep_index(bEndpoint);
        nemuusb_ep_t *pEp = &pState->aEps[EndPtIndex];
        if (RT_LIKELY(pEp))
        {
            /*
             * Check if the endpoint is open to be cleared.
             */
            if (pEp->pPipe)
            {
                mutex_exit(&pState->Mtx);
#if 0
                /*
                 * Asynchronous clear pipe.
                 */
                rc = usb_clr_feature(pState->pDip, USB_DEV_REQ_RCPT_EP, USB_EP_HALT, bEndpoint,
                                        USB_FLAGS_NOSLEEP, /* Asynchronous */
                                        NULL,              /* Completion callback */
                                        NULL);             /* Exception callback */
#endif
                /*
                 * Synchronous reset pipe.
                 */
                usb_pipe_reset(pState->pDip, pEp->pPipe,
                                        USB_FLAGS_SLEEP,   /* Synchronous */
                                        NULL,              /* Completion callback */
                                        NULL);             /* Exception callback */

                mutex_enter(&pState->Mtx);

                Log((DEVICE_NAME ":nemuUSBSolarisClearEndPoint bEndpoint=%#x[%d] returns %d\n", bEndpoint, EndPtIndex, rc));

                rc = VINF_SUCCESS;
            }
            else
            {
                Log((DEVICE_NAME ":nemuUSBSolarisClearEndPoint not opened to be cleared. Faking success. bEndpoint=%#x.\n",
                     bEndpoint));
                rc = VINF_SUCCESS;
            }
        }
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisClearEndPoint Endpoint missing!! bEndpoint=%#x EndPtIndex=%d.\n", bEndpoint,
                    EndPtIndex));
            rc = VERR_GENERAL_FAILURE;
        }
    }
    else
        Log((DEVICE_NAME ":nemuUSBSolarisClearEndPoint device state=%d not online.\n", pState->DevState));

    mutex_exit(&pState->Mtx);
    return rc;
}


/**
 * Set configuration (SET_CONFIGURATION)
 *
 * @param   pState          The USB device instance.
 * @param   bCfgValue       The Configuration value.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisSetConfig(nemuusb_state_t *pState, uint8_t bCfgValue)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisSetConfig pState=%p bCfgValue=%#x\n", pState, bCfgValue));

    /*
     * Serialize access: single threaded per Endpoint, one request at a time.
     */
    mutex_enter(&pState->Mtx);
    int rc = nemuUSBSolarisDeviceState(pState->DevState);
    if (RT_SUCCESS(rc))
    {
        nemuUSBSolarisCloseAllPipes(pState, false /* ControlPipe */);
        int iCfgIndex = nemuUSBSolarisGetConfigIndex(pState, bCfgValue);

        if (iCfgIndex >= 0)
        {
            /*
             * Switch Config synchronously.
             */
            mutex_exit(&pState->Mtx);
            rc = usb_set_cfg(pState->pDip, (uint_t)iCfgIndex, USB_FLAGS_SLEEP, NULL /* callback */, NULL /* callback data */);
            mutex_enter(&pState->Mtx);

            if (rc == USB_SUCCESS)
            {
                pState->fRestoreCfg = true;
                nemuUSBSolarisInitEndPointsForConfig(pState, iCfgIndex);
                rc = VINF_SUCCESS;
            }
            else
            {
                LogRel((DEVICE_NAME ":nemuUSBSolarisSetConfig usb_set_cfg failed for iCfgIndex=%#x bCfgValue=%#x rc=%d\n",
                            iCfgIndex, bCfgValue, rc));
                rc = nemuUSBSolarisToNemuRC(rc);
            }
        }
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisSetConfig invalid iCfgIndex=%d bCfgValue=%#x\n", iCfgIndex, bCfgValue));
            rc = VERR_INVALID_HANDLE;
        }
    }

    mutex_exit(&pState->Mtx);

    return rc;
}


/**
 * Get configuration (GET_CONFIGURATION)
 *
 * @param   pState          The USB device instance.
 * @param   pCfgValue       Where to store the configuration value.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisGetConfig(nemuusb_state_t *pState, uint8_t *pCfgValue)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisGetConfig pState=%p pCfgValue=%p\n", pState, pCfgValue));
    AssertPtrReturn(pCfgValue, VERR_INVALID_POINTER);

    /*
     * Solaris keeps the currently active configuration for the first time. Thus for the first request
     * we simply pass the cached configuration back to the user.
     */
    if (!pState->fGetCfgReqDone)
    {
        pState->fGetCfgReqDone = true;
        AssertPtrReturn(pState->pDevDesc, VERR_GENERAL_FAILURE);
        usb_cfg_data_t *pCurrCfg = pState->pDevDesc->dev_curr_cfg;
        if (pCurrCfg)
        {
            *pCfgValue = pCurrCfg->cfg_descr.bConfigurationValue;
            Log((DEVICE_NAME ":nemuUSBSolarisGetConfig cached config returned. CfgValue=%d\n", *pCfgValue));
            return VINF_SUCCESS;
        }
    }

    /*
     * Get Config synchronously.
     */
    uint_t bCfgValue;
    int rc = usb_get_cfg(pState->pDip, &bCfgValue, USB_FLAGS_SLEEP);
    if (RT_LIKELY(rc == USB_SUCCESS))
    {
        *pCfgValue = bCfgValue;
        rc = VINF_SUCCESS;
    }
    else
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisGetConfig failed. rc=%d\n", rc));
        rc = nemuUSBSolarisToNemuRC(rc);
    }

    Log((DEVICE_NAME ":nemuUSBSolarisGetConfig returns %d CfgValue=%d\n", rc, *pCfgValue));
    return rc;
}


/**
 * Set interface (SET_INTERFACE)
 *
 * @param   pState          The USB device instance.
 * @param   uInterface      The Interface number.
 * @param   uAlt            The Alternate setting number.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisSetInterface(nemuusb_state_t *pState, uint8_t uInterface, uint8_t uAlt)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisSetInterface pState=%p uInterface=%#x uAlt=%#x\n", pState, uInterface, uAlt));

    /*
     * Serialize access: single threaded per Endpoint, one request at a time.
     */
    mutex_enter(&pState->Mtx);
    int rc = nemuUSBSolarisDeviceState(pState->DevState);
    if (RT_SUCCESS(rc))
    {
        nemuUSBSolarisCloseAllPipes(pState, false /* ControlPipe */);

        /*
         * Set Interface & Alt setting synchronously.
         */
        mutex_exit(&pState->Mtx);
        rc = usb_set_alt_if(pState->pDip, uInterface, uAlt, USB_FLAGS_SLEEP, NULL /* callback */, NULL /* callback data */);
        mutex_enter(&pState->Mtx);

        if (rc == USB_SUCCESS)
        {
            nemuUSBSolarisInitEndPointsForInterfaceAlt(pState, uInterface, uAlt);
            rc = VINF_SUCCESS;
        }
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisSetInterface usb_set_alt_if failed for uInterface=%#x bAlt=%#x rc=%d\n",
                        uInterface, uAlt, rc));
            rc = nemuUSBSolarisToNemuRC(rc);
        }
    }

    mutex_exit(&pState->Mtx);

    return rc;
}


/**
 * Close the USB device and reset it if required.
 *
 * @param   pState          The USB device instance.
 * @param   enmReset        The reset level.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisCloseDevice(nemuusb_state_t *pState, NEMUUSB_RESET_LEVEL enmReset)
{
    Log((DEVICE_NAME ":nemuUSBSolarisCloseDevice pState=%p enmReset=%d\n", pState, enmReset));

    /*
     * Serialize access: single threaded per Endpoint, one request at a time.
     */
    mutex_enter(&pState->Mtx);
    int rc = nemuUSBSolarisDeviceState(pState->DevState);

    if (enmReset == NEMUUSB_RESET_LEVEL_CLOSE)
    {
        nemuUSBSolarisCloseAllPipes(pState, true /* ControlPipe */);
        pState->fClosed = true;
    }
    else
        nemuUSBSolarisCloseAllPipes(pState, false /* ControlPipe */);


    mutex_exit(&pState->Mtx);

    if (RT_SUCCESS(rc))
    {
        switch (enmReset)
        {
            case NEMUUSB_RESET_LEVEL_REATTACH:
                rc = usb_reset_device(pState->pDip, USB_RESET_LVL_REATTACH);
                break;

            case NEMUUSB_RESET_LEVEL_SOFT:
                rc = usb_reset_device(pState->pDip, USB_RESET_LVL_DEFAULT);
                break;

            default:
                rc = USB_SUCCESS;
                break;
        }

        rc = nemuUSBSolarisToNemuRC(rc);
    }

    Log((DEVICE_NAME ":nemuUSBSolarisCloseDevice returns %d\n", rc));
    return rc;
}


/**
 * Abort pending requests and reset the pipe.
 *
 * @param   pState          The USB device instance.
 * @param   bEndpoint       The Endpoint address.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisAbortPipe(nemuusb_state_t *pState, uint8_t bEndpoint)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisAbortPipe pState=%p bEndpoint=%#x\n", pState, bEndpoint));

    /*
     * Serialize access: single threaded per Endpoint, one request at a time.
     */
    mutex_enter(&pState->Mtx);
    int rc = nemuUSBSolarisDeviceState(pState->DevState);
    if (RT_SUCCESS(rc))
    {
        uchar_t EndPtIndex = usb_get_ep_index(bEndpoint);
        nemuusb_ep_t *pEp = &pState->aEps[EndPtIndex];
        if (RT_LIKELY(pEp))
        {
            if (pEp->pPipe)
            {
                /*
                 * Default Endpoint; aborting requests not supported, fake success.
                 */
                if ((pEp->EpDesc.bEndpointAddress & USB_EP_NUM_MASK) == 0)
                {
                    mutex_exit(&pState->Mtx);
                    LogRel((DEVICE_NAME ":nemuUSBSolarisAbortPipe Cannot reset control pipe.\n"));
                    return VERR_NOT_SUPPORTED;
                }

                /*
                 * Serialize access: single threaded per Endpoint, one request at a time.
                 */
                mutex_exit(&pState->Mtx);
                usb_pipe_reset(pState->pDip, pEp->pPipe,
                                USB_FLAGS_SLEEP, /* Synchronous */
                                NULL,            /* Completion callback */
                                NULL);           /* Callback data */

                /*
                 * Allow pending async requests to complete.
                 */
                rc = usb_pipe_drain_reqs(pState->pDip, pEp->pPipe,
                                USB_FLAGS_SLEEP, /* Synchronous */
                                5,               /* Timeout (seconds) */
                                NULL,            /* Completion callback */
                                NULL);           /* Callback data*/

                mutex_enter(&pState->Mtx);

                Log((DEVICE_NAME ":usb_pipe_drain_reqs returns %d\n", rc));
                rc = nemuUSBSolarisToNemuRC(rc);
            }
            else
            {
                LogRel((DEVICE_NAME ":nemuUSBSolarisAbortPipe pipe not open. bEndpoint=%#x\n", bEndpoint));
                rc = VERR_PIPE_IO_ERROR;
            }
        }
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisAbortPipe invalid pipe index %d bEndpoint=%#x\n", EndPtIndex, bEndpoint));
            rc = VERR_INVALID_HANDLE;
        }
    }

    mutex_exit(&pState->Mtx);

    LogFunc((DEVICE_NAME ":nemuUSBSolarisAbortPipe returns %d\n", rc));
    return rc;
}


/**
 * Initialize an endpoint.
 *
 * @param   pState          The USB device instance.
 * @param   pEpData         The Endpoint data.
 * @param   uCfgValue       The Configuration value.
 * @param   uInterface      The Interface.
 * @param   uAlt            The Alternate setting.
 *
 * @returns Nemu error code.
 */
LOCAL int nemuUSBSolarisInitEndPoint(nemuusb_state_t *pState, usb_ep_data_t *pEpData, uchar_t uCfgValue,
                                     uchar_t uInterface, uchar_t uAlt)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisInitEndPoint pState=%p pEpData=%p CfgVal=%d Iface=%d Alt=%d", pState,
                    pEpData, uCfgValue, uInterface, uAlt));

    /*
     * Is this the default endpoint?
     */
    usb_ep_descr_t *pEpDesc = NULL;
    nemuusb_ep_t *pEp = NULL;
    int EpIndex = 0;
    if (!pEpData)
    {
        EpIndex = 0;
        pEpDesc = &g_NemuUSBSolarisDefaultEpDesc;
    }
    else
    {
        EpIndex = usb_get_ep_index(pEpData->ep_descr.bEndpointAddress);
        pEpDesc = &pEpData->ep_descr;
    }

    pEp = &pState->aEps[EpIndex];
    AssertRelease(pEp);

    /*
     * Initialize the endpoint data structure.
     */
    pEp->EpDesc = *pEpDesc;
    pEp->uCfgValue = uCfgValue;
    pEp->uInterface = uInterface;
    pEp->uAlt = uAlt;
    if (pEp->fInitialized != NEMUUSB_EP_INITIALIZED)
    {
        pEp->pPipe = NULL;
        pEp->EpState = NEMUUSB_EP_STATE_CLOSED;
        bzero(&pEp->PipePolicy, sizeof(pEp->PipePolicy));
        pEp->PipePolicy.pp_max_async_reqs = NEMUUSB_MAX_PIPE_ASYNC_REQS;
        pEp->fIsocPolling = false;
        list_create(&pEp->hIsocInUrbs, sizeof(nemuusb_urb_t), offsetof(nemuusb_urb_t, hListLink));
        pEp->cIsocInUrbs = 0;
        list_create(&pEp->hIsocInLandedReqs, sizeof(nemuusb_isoc_req_t), offsetof(nemuusb_isoc_req_t, hListLink));
        pEp->cbIsocInLandedReqs = 0;
        pEp->cbMaxIsocData = 0;
        pEp->fInitialized = NEMUUSB_EP_INITIALIZED;
    }
    Log((DEVICE_NAME ":nemuUSBSolarisInitEndPoint done. %s:[%d] bEndpoint=%#x\n", !pEpData ? "Default " : "Endpoint",
                    EpIndex, pEp->EpDesc.bEndpointAddress));
    return VINF_SUCCESS;
}


/**
 * Initialize all Endpoint structures.
 *
 * @param   pState          The USB device instance.
 *
 * @returns Nemu status code.
 */
LOCAL int nemuUSBSolarisInitAllEndPoints(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisInitAllEndPoints pState=%p\n", pState));

    /*
     * Initialize all Endpoints for all Alternate settings of all Interfaces of all Configs.
     */
    int rc = nemuUSBSolarisInitEndPoint(pState, NULL /* pEp */, 0 /* uCfgValue */, 0 /* uInterface */, 0 /* uAlt */);

    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize all Endpoints for all Alternate settings of all Interfaces of all Configs.
         */
        for (uchar_t uCfgIndex = 0; uCfgIndex < pState->pDevDesc->dev_n_cfg; uCfgIndex++)
        {
            rc = nemuUSBSolarisInitEndPointsForConfig(pState, uCfgIndex);
            if (RT_FAILURE(rc))
            {
                LogRel((DEVICE_NAME ":nemuUSBSolarisInitAllEndPoints: nemuUSBSolarisInitEndPoints uCfgIndex=%d failed. rc=%d\n",
                        uCfgIndex, rc));
                return rc;
            }
        }
    }
    else
        LogRel((DEVICE_NAME ":nemuUSBSolarisInitAllEndPoints default Endpoint initialization failed!\n"));

    return rc;
}


/**
 * Initialize Endpoints structures for the given Config.
 *
 * @param   pState          The USB device instance.
 * @param   uCfgIndex       The current Config. index.
 *
 * @returns Nemu status code.
 */
LOCAL int nemuUSBSolarisInitEndPointsForConfig(nemuusb_state_t *pState, uint8_t uCfgIndex)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisInitEndPointsForConfig pState=%p uCfgIndex=%d\n", pState, uCfgIndex));
    usb_cfg_data_t *pConfig = &pState->pDevDesc->dev_cfg[uCfgIndex];
    uchar_t uCfgValue = pConfig->cfg_descr.bConfigurationValue;

    for (uchar_t uInterface = 0; uInterface < pConfig->cfg_n_if; uInterface++)
    {
        usb_if_data_t *pInterface = &pConfig->cfg_if[uInterface];

        for (uchar_t uAlt = 0; uAlt < pInterface->if_n_alt; uAlt++)
        {
            usb_alt_if_data_t *pAlt = &pInterface->if_alt[uAlt];

            for (uchar_t uEp = 0; uEp < pAlt->altif_n_ep; uEp++)
            {
                usb_ep_data_t *pEpData = &pAlt->altif_ep[uEp];

                int rc = nemuUSBSolarisInitEndPoint(pState, pEpData, uCfgValue, uInterface, uAlt);
                if (RT_FAILURE(rc))
                {
                    LogRel((DEVICE_NAME ":nemuUSBSolarisInitEndPointsForConfig: nemuUSBSolarisInitEndPoint failed! pEp=%p "
                            "uCfgValue=%u uCfgIndex=%u uInterface=%u, uAlt=%u\n", uCfgValue, uCfgIndex, uInterface, uAlt));
                    return rc;
                }
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Initialize Endpoints structures for the given Interface & Alternate setting.
 *
 * @param   pState          The USB device instance.
 * @param   uInterface      The interface being switched to.
 * @param   uAlt            The alt being switched to.
 *
 * @returns Nemu status code.
 */
LOCAL int nemuUSBSolarisInitEndPointsForInterfaceAlt(nemuusb_state_t *pState, uint8_t uInterface, uint8_t uAlt)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisInitEndPointsForInterfaceAlt pState=%p uInterface=%d uAlt=%d\n", pState, uInterface,
             uAlt));

    /* Doesn't hurt to be paranoid */
    uint_t uCfgIndex = usb_get_current_cfgidx(pState->pDip);
    if (RT_UNLIKELY(uCfgIndex >= pState->pDevDesc->dev_n_cfg))
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisInitEndPointsForInterfaceAlt invalid current config index %d\n", uCfgIndex));
        return VERR_GENERAL_FAILURE;
    }

    usb_cfg_data_t *pConfig = &pState->pDevDesc->dev_cfg[uCfgIndex];
    uchar_t uCfgValue = pConfig->cfg_descr.bConfigurationValue;
    usb_if_data_t *pInterface = &pConfig->cfg_if[uInterface];

    int rc = VINF_SUCCESS;
    if (RT_LIKELY(pInterface))
    {
        usb_alt_if_data_t *pAlt = &pInterface->if_alt[uAlt];
        if (RT_LIKELY(pAlt))
        {
            for (uchar_t uEp = 0; uEp < pAlt->altif_n_ep; uEp++)
            {
                usb_ep_data_t *pEpData = &pAlt->altif_ep[uEp];
                rc = nemuUSBSolarisInitEndPoint(pState, pEpData, uCfgValue, uInterface, uAlt);
                if (RT_FAILURE(rc))
                {
                    LogRel((DEVICE_NAME ":nemuUSBSolarisInitEndPointsForInterfaceAlt: nemuUSBSolarisInitEndPoint failed! pEp=%p "
                            "uCfgValue=%u uCfgIndex=%u uInterface=%u, uAlt=%u\n", uCfgValue, uCfgIndex, uInterface, uAlt));
                    return rc;
                }
            }
        }
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisInitEndPointsForInterfaceAlt missing alternate.\n"));
            rc = VERR_INVALID_POINTER;
        }
    }
    else
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisInitEndPointsForInterfaceAlt missing interface.\n"));
        rc = VERR_INVALID_POINTER;
    }

    Log((DEVICE_NAME ":nemuUSBSolarisInitEndPointsForInterfaceAlt returns %d\n", rc));
    return rc;
}


/**
 * Destroy all Endpoint Xfer structures.
 *
 * @param   pState          The USB device instance.
 * @remarks Requires the state mutex to be held.
 *          Call only from Detach() or similar as callbacks
 */
LOCAL void nemuUSBSolarisDestroyAllEndPoints(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDestroyAllEndPoints pState=%p\n", pState));

    Assert(mutex_owned(&pState->Mtx));
    for (unsigned i = 0; i < NEMUUSB_MAX_ENDPOINTS; i++)
    {
        nemuusb_ep_t *pEp = &pState->aEps[i];
        if (pEp)
        {
            nemuUSBSolarisDestroyEndPoint(pState, pEp);
            pEp = NULL;
        }
    }
}


/**
 * Destroy an Endpoint.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint.
 * @remarks Requires the state mutex to be held.
 */
LOCAL void nemuUSBSolarisDestroyEndPoint(nemuusb_state_t *pState, nemuusb_ep_t *pEp)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDestroyEndPoint pState=%p pEp=%p\n", pState, pEp));

    Assert(mutex_owned(&pState->Mtx));
    if (pEp->fInitialized == NEMUUSB_EP_INITIALIZED)
    {
        nemuusb_urb_t *pUrb = list_remove_head(&pEp->hIsocInUrbs);
        while (pUrb)
        {
            if (pUrb->pMsg)
                freemsg(pUrb->pMsg);
            RTMemFree(pUrb);
            pUrb = list_remove_head(&pEp->hIsocInUrbs);
        }
        pEp->cIsocInUrbs = 0;
        list_destroy(&pEp->hIsocInUrbs);

        nemuusb_isoc_req_t *pIsocReq = list_remove_head(&pEp->hIsocInLandedReqs);
        while (pIsocReq)
        {
            kmem_free(pIsocReq, sizeof(nemuusb_isoc_req_t));
            pIsocReq = list_remove_head(&pEp->hIsocInLandedReqs);
        }
        pEp->cbIsocInLandedReqs = 0;
        list_destroy(&pEp->hIsocInLandedReqs);

        pEp->fInitialized = 0;
    }
}


/**
 * Close all non-default Endpoints and drains the default pipe.
 *
 * @param   pState          The USB device instance.
 * @param   fDefault        Whether to close the default control pipe.
 *
 * @remarks Requires the device state mutex to be held.
 */
LOCAL void nemuUSBSolarisCloseAllPipes(nemuusb_state_t *pState, bool fDefault)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisCloseAllPipes pState=%p\n", pState));

    for (int i = 1; i < NEMUUSB_MAX_ENDPOINTS; i++)
    {
        nemuusb_ep_t *pEp = &pState->aEps[i];
        if (   pEp
            && pEp->pPipe)
        {
            Log((DEVICE_NAME ":nemuUSBSolarisCloseAllPipes closing[%d]\n", i));
            nemuUSBSolarisClosePipe(pState, pEp);
        }
    }

    if (fDefault)
    {
        nemuusb_ep_t *pEp = &pState->aEps[0];
        if (   pEp
            && pEp->pPipe)
        {
            nemuUSBSolarisClosePipe(pState, pEp);
            Log((DEVICE_NAME ":nemuUSBSolarisCloseAllPipes closed default pipe.\n"));
        }
    }
}


/**
 * Open the pipe for an Endpoint.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint.
 * @remarks Requires the device state mutex to be held.
 *
 * @returns Nemu status code.
 */
LOCAL int nemuUSBSolarisOpenPipe(nemuusb_state_t *pState, nemuusb_ep_t *pEp)
{
    Assert(mutex_owned(&pState->Mtx));

    /*
     * Make sure the Endpoint isn't open already.
     */
    if (pEp->pPipe)
        return VINF_SUCCESS;


    /*
     * Default Endpoint; already opened just copy the pipe handle.
     */
    if ((pEp->EpDesc.bEndpointAddress & USB_EP_NUM_MASK) == 0)
    {
        pEp->pPipe = pState->pDevDesc->dev_default_ph;
        pEp->EpState |= NEMUUSB_EP_STATE_OPENED;
        Log((DEVICE_NAME ":nemuUSBSolarisOpenPipe default pipe opened.\n"));
        return VINF_SUCCESS;
    }

    /*
     * Open the non-default pipe for the Endpoint.
     */
    mutex_exit(&pState->Mtx);
    int rc = usb_pipe_open(pState->pDip, &pEp->EpDesc, &pEp->PipePolicy, USB_FLAGS_NOSLEEP, &pEp->pPipe);
    mutex_enter(&pState->Mtx);
    if (rc == USB_SUCCESS)
    {
        LogFunc((DEVICE_NAME ":nemuUSBSolarisOpenPipe: Opened pipe. pState=%p pEp=%p\n", pState, pEp));
        usb_pipe_set_private(pEp->pPipe, (usb_opaque_t)pEp);

        /*
         * Determine input buffer size for Isoc. IN transfers.
         */
        if (   NEMUUSB_XFER_TYPE(pEp) == VUSBXFERTYPE_ISOC
            && NEMUUSB_XFER_DIR(pEp) == VUSB_DIR_TO_HOST)
        {
            /*
             * wMaxPacketSize bits 10..0 specifies maximum packet size which can hold 1024 bytes.
             * If bits 12..11 is non-zero, cbMax will be more than 1024 and thus the Endpoint is a
             * high-bandwidth Endpoint.
             */
            uint16_t cbMax = NEMUUSB_PKT_SIZE(pEp->EpDesc.wMaxPacketSize);
            if (cbMax <= 1024)
            {
                /* Buffer 1 second for highspeed and 8 seconds for fullspeed Endpoints. */
                pEp->cbMaxIsocData = 1000 * cbMax * 8;
            }
            else
            {
                /* Buffer about 400 milliseconds of data for highspeed high-bandwidth endpoints. */
                pEp->cbMaxIsocData = 400 * cbMax * 8;
            }
            Log((DEVICE_NAME ":nemuUSBSolarisOpenPipe pEp=%p cbMaxIsocData=%u\n", pEp->cbMaxIsocData));
        }

        pEp->EpState |= NEMUUSB_EP_STATE_OPENED;
        rc = VINF_SUCCESS;
    }
    else
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisOpenPipe failed! rc=%d pState=%p pEp=%p\n", rc, pState, pEp));
        rc = VERR_BAD_PIPE;
    }

    return rc;
}


/**
 * Close the pipe of the Endpoint.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint.
 *
 * @remarks Requires the device state mutex to be held.
 */
LOCAL void nemuUSBSolarisClosePipe(nemuusb_state_t *pState, nemuusb_ep_t *pEp)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisClosePipe pState=%p pEp=%p\n", pState, pEp));
    AssertPtr(pEp);

    if (pEp->pPipe)
    {
        pEp->EpState &= ~(NEMUUSB_EP_STATE_OPENED);

        /*
         * Default pipe: allow completion of pending requests.
         */
        if (pEp->pPipe == pState->pDevDesc->dev_default_ph)
        {
            mutex_exit(&pState->Mtx);
            usb_pipe_drain_reqs(pState->pDip, pEp->pPipe, 0, USB_FLAGS_SLEEP, NULL /* callback */, NULL /* callback arg. */);
            mutex_enter(&pState->Mtx);
            Log((DEVICE_NAME ":nemuUSBSolarisClosePipe closed default pipe\n"));
        }
        else
        {
            /*
             * Stop Isoc. IN polling if required.
             */
            if (pEp->fIsocPolling)
            {
                pEp->fIsocPolling = false;
                mutex_exit(&pState->Mtx);
                usb_pipe_stop_isoc_polling(pEp->pPipe, USB_FLAGS_NOSLEEP);
                mutex_enter(&pState->Mtx);
            }

            /*
             * Non-default pipe: close it.
             */
            Log((DEVICE_NAME ":nemuUSBSolarisClosePipe pipe bmAttributes=%#x bEndpointAddress=%#x\n", pEp->EpDesc.bmAttributes,
                 pEp->EpDesc.bEndpointAddress));
            mutex_exit(&pState->Mtx);
            usb_pipe_close(pState->pDip, pEp->pPipe, USB_FLAGS_SLEEP, NULL /* callback */, NULL /* callback arg. */);
            mutex_enter(&pState->Mtx);
        }

        /*
         * Free the Endpoint data message block and reset pipe handle.
         */
        pEp->pPipe = NULL;

        Log((DEVICE_NAME ":nemuUSBSolarisClosePipe successful. pEp=%p\n", pEp));
    }

    Assert(pEp->pPipe == NULL);
}


/**
 * Find the Configuration index for the passed in Configuration value.
 *
 * @param   pState          The USB device instance.
 * @param   uCfgValue       The Configuration value.
 *
 * @returns The configuration index if found, otherwise -1.
 */
LOCAL int nemuUSBSolarisGetConfigIndex(nemuusb_state_t *pState, uint_t uCfgValue)
{
    for (int CfgIndex = 0; CfgIndex < pState->pDevDesc->dev_n_cfg; CfgIndex++)
    {
        usb_cfg_data_t *pConfig = &pState->pDevDesc->dev_cfg[CfgIndex];
        if (pConfig->cfg_descr.bConfigurationValue == uCfgValue)
            return CfgIndex;
    }

    return -1;
}


/**
 * Allocates and initializes an Isoc. In URB from the ring-3 equivalent.
 *
 * @param   pState          The USB device instance.
 * @param   pUrbReq         Opaque pointer to the complete request.
 *
 * @returns The allocated Isoc. In URB to be used.
 */
LOCAL nemuusb_urb_t *nemuUSBSolarisGetIsocInURB(nemuusb_state_t *pState, PNEMUUSBREQ_URB pUrbReq)
{
    /*
     * Isoc. In URBs are not queued into the Inflight list like every other URBs.
     * For now we allocate each URB which gets queued into the respective Endpoint during Xfer.
     */
    nemuusb_urb_t *pUrb = RTMemAllocZ(sizeof(nemuusb_urb_t));
    if (RT_LIKELY(pUrb))
    {
        pUrb->enmState = NEMUUSB_URB_STATE_INFLIGHT;
        pUrb->pState = pState;

        if (RT_LIKELY(pUrbReq))
        {
            pUrb->pvUrbR3 = pUrbReq->pvUrbR3;
            pUrb->bEndpoint = pUrbReq->bEndpoint;
            pUrb->enmType = pUrbReq->enmType;
            pUrb->enmDir = pUrbReq->enmDir;
            pUrb->enmStatus = pUrbReq->enmStatus;
            pUrb->cbDataR3 = pUrbReq->cbData;
            pUrb->pvDataR3 = (RTR3PTR)pUrbReq->pvData;
            pUrb->cIsocPkts = pUrbReq->cIsocPkts;

            for (unsigned i = 0; i < pUrbReq->cIsocPkts; i++)
                pUrb->aIsocPkts[i].cbPkt = pUrbReq->aIsocPkts[i].cbPkt;

            pUrb->pMsg = NULL;
        }
    }
    else
        LogRel((DEVICE_NAME ":nemuUSBSolarisGetIsocInURB failed to alloc %d bytes.\n", sizeof(nemuusb_urb_t)));
    return pUrb;
}


/**
 * Queues a URB reusing previously allocated URBs as required.
 *
 * @param   pState          The USB device instance.
 * @param   pUrbReq         Opaque pointer to the complete request.
 * @param   pMsg            Pointer to the allocated request data.
 *
 * @returns The allocated URB to be used, or NULL upon failure.
 */
LOCAL nemuusb_urb_t *nemuUSBSolarisQueueURB(nemuusb_state_t *pState, PNEMUUSBREQ_URB pUrbReq, mblk_t *pMsg)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisQueueURB pState=%p pUrbReq=%p\n", pState, pUrbReq));

    mutex_enter(&pState->Mtx);

    /*
     * Discard oldest queued URB if we've queued max URBs and none of them have completed.
     */
    if (pState->cInflightUrbs >= NEMUUSB_URB_QUEUE_SIZE)
    {
        nemuusb_urb_t *pUrb = list_head(&pState->hUrbs);
        if (RT_LIKELY(pUrb))
        {
            if (pUrb->pMsg)
            {
                freemsg(pUrb->pMsg);
                pUrb->pMsg = NULL;
            }
            pUrb->enmState = NEMUUSB_URB_STATE_FREE;
        }
    }

    nemuusb_urb_t *pUrb = list_head(&pState->hUrbs);
    if (   !pUrb
        || (   pUrb
            && pUrb->enmState != NEMUUSB_URB_STATE_FREE))
    {
        mutex_exit(&pState->Mtx);
        pUrb = RTMemAllocZ(sizeof(nemuusb_urb_t));
        if (RT_UNLIKELY(!pUrb))
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisQueueURB failed to alloc %d bytes.\n", sizeof(nemuusb_urb_t)));
            return NULL;
        }
        mutex_enter(&pState->Mtx);
    }
    else
    {
        /*
         * Remove from head and move to tail so that when several URBs are reaped continuously we get to use
         * up each one free 'head'.
         */
        Assert(pUrb && pUrb->enmState == NEMUUSB_URB_STATE_FREE);
        list_remove_head(&pState->hUrbs);
    }

    list_insert_tail(&pState->hUrbs, pUrb);
    ++pState->cInflightUrbs;

    pUrb->enmState = NEMUUSB_URB_STATE_INFLIGHT;

    Assert(pUrb->pMsg == NULL);
    pUrb->pState = pState;
    Log((DEVICE_NAME ":nemuUSBSolarisQueueURB cInflightUrbs=%d\n", pState->cInflightUrbs));

    if (RT_LIKELY(pUrbReq))
    {
        pUrb->pvUrbR3   = pUrbReq->pvUrbR3;
        pUrb->bEndpoint = pUrbReq->bEndpoint;
        pUrb->enmType   = pUrbReq->enmType;
        pUrb->enmDir    = pUrbReq->enmDir;
        pUrb->enmStatus = pUrbReq->enmStatus;
        pUrb->fShortOk  = pUrbReq->fShortOk;
        pUrb->pvDataR3  = (RTR3PTR)pUrbReq->pvData;
        pUrb->cbDataR3  = pUrbReq->cbData;
        pUrb->cIsocPkts = pUrbReq->cIsocPkts;

        if (pUrbReq->enmType == VUSBXFERTYPE_ISOC)
        {
            for (unsigned i = 0; i < pUrbReq->cIsocPkts; i++)
                pUrb->aIsocPkts[i].cbPkt = pUrbReq->aIsocPkts[i].cbPkt;
        }

        pUrb->pMsg = pMsg;
    }

    mutex_exit(&pState->Mtx);

    return pUrb;
}


/**
 * Dequeues a completed URB into the landed list and informs user-land.
 *
 * @param   pUrb                The URB to move.
 * @param   URBStatus           The Solaris URB completion code.
 *
 * @remarks All pipes could be closed at this point (e.g. Device disconnected during inflight URBs)
 */
LOCAL inline void nemuUSBSolarisDeQueueURB(nemuusb_urb_t *pUrb, int URBStatus)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDeQueue pUrb=%p\n", pUrb));
    AssertPtrReturnVoid(pUrb);

    pUrb->enmStatus = nemuUSBSolarisGetUrbStatus(URBStatus);

    nemuusb_state_t *pState = pUrb->pState;
    if (RT_LIKELY(pState))
    {
        mutex_enter(&pState->Mtx);
        pUrb->enmState = NEMUUSB_URB_STATE_LANDED;

        /*
         * Remove it from the inflight list & move it to landed list.
         */
        list_remove(&pState->hUrbs, pUrb);
        --pState->cInflightUrbs;
        list_insert_tail(&pState->hLandedUrbs, pUrb);

        nemuUSBSolarisNotifyComplete(pUrb->pState);
        mutex_exit(&pState->Mtx);
    }
    else
    {
        Log((DEVICE_NAME ":nemuUSBSolarisDeQueue State Gone.\n"));
        freemsg(pUrb->pMsg);
        pUrb->pMsg = NULL;
        pUrb->enmStatus = VUSBSTATUS_INVALID;
    }
}


/**
 * Concatenates a chain message block into a single message block if possible.
 *
 * @param   pUrb                The URB to move.
 */
LOCAL inline void nemuUSBSolarisConcatMsg(nemuusb_urb_t *pUrb)
{
    /*
     * Concatenate the whole message rather than doing a chained copy while reaping.
     */
    if (   pUrb->pMsg
        && pUrb->pMsg->b_cont)
    {
        mblk_t *pFullMsg = msgpullup(pUrb->pMsg, -1 /* all data */);
        if (RT_LIKELY(pFullMsg))
        {
            freemsg(pUrb->pMsg);
            pUrb->pMsg = pFullMsg;
        }
    }
}


/**
 * User process poll wake up wrapper for asynchronous URB completion.
 *
 * @param   pState          The USB device instance.
 * @remarks Requires the device state mutex to be held.
 */
LOCAL inline void nemuUSBSolarisNotifyComplete(nemuusb_state_t *pState)
{
    if (pState->fPoll & NEMUUSB_POLL_ON)
    {
        pollhead_t *pPollHead = &pState->PollHead;
        pState->fPoll |= NEMUUSB_POLL_REAP_PENDING;
        mutex_exit(&pState->Mtx);
        pollwakeup(pPollHead, POLLIN);
        mutex_enter(&pState->Mtx);
    }
}


/**
 * User process poll wake up wrapper for hotplug events.
 *
 * @param   pState          The USB device instance.
 * @remarks Requires the device state mutex to be held.
 */
LOCAL inline void nemuUSBSolarisNotifyHotplug(nemuusb_state_t *pState)
{
    if (pState->fPoll & NEMUUSB_POLL_ON)
    {
        pollhead_t *pPollHead = &pState->PollHead;
        pState->fPoll |= NEMUUSB_POLL_DEV_UNPLUGGED;
        mutex_exit(&pState->Mtx);
        pollwakeup(pPollHead, POLLHUP);
        mutex_enter(&pState->Mtx);
    }
}


/**
 * Perform a Control Xfer.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint for the Xfer.
 * @param   pUrb            The Nemu USB URB.
 *
 * @returns Nemu status code.
 * @remarks Any errors, the caller should free pUrb->pMsg.
 */
LOCAL int nemuUSBSolarisCtrlXfer(nemuusb_state_t *pState, nemuusb_ep_t *pEp, nemuusb_urb_t *pUrb)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisCtrlXfer pState=%p pEp=%p pUrb=%p enmDir=%d cbData=%d\n", pState, pEp, pUrb,
             pUrb->enmDir, pUrb->cbDataR3));

    AssertPtrReturn(pUrb->pMsg, VERR_INVALID_PARAMETER);
    uchar_t *pSetupData = pUrb->pMsg->b_rptr;
    size_t cbData = pUrb->cbDataR3 > NEMUUSB_CTRL_XFER_SIZE ? pUrb->cbDataR3 - NEMUUSB_CTRL_XFER_SIZE : 0;

    /*
     * Allocate a wrapper request.
     */
    int rc = VINF_SUCCESS;
    usb_ctrl_req_t *pReq = usb_alloc_ctrl_req(pState->pDip, cbData, USB_FLAGS_NOSLEEP);
    if (RT_LIKELY(pReq))
    {
        /*
         * Initialize the Ctrl Xfer Header.
         */
        pReq->ctrl_bmRequestType  = pSetupData[0];
        pReq->ctrl_bRequest       = pSetupData[1];
        pReq->ctrl_wValue         = (pSetupData[3] << NEMUUSB_CTRL_XFER_SIZE) | pSetupData[2];
        pReq->ctrl_wIndex         = (pSetupData[5] << NEMUUSB_CTRL_XFER_SIZE) | pSetupData[4];
        pReq->ctrl_wLength        = (pSetupData[7] << NEMUUSB_CTRL_XFER_SIZE) | pSetupData[6];

        if (   pUrb->enmDir == VUSBDIRECTION_OUT
            && cbData)
        {
            pUrb->pMsg->b_rptr += NEMUUSB_CTRL_XFER_SIZE;
            bcopy(pUrb->pMsg->b_rptr, pReq->ctrl_data->b_wptr, cbData);
            pReq->ctrl_data->b_wptr += cbData;
        }

        freemsg(pUrb->pMsg);
        pUrb->pMsg = NULL;

        /*
         * Initialize callbacks and timeouts.
         */
        usb_req_attrs_t fAttributes = USB_ATTRS_AUTOCLEARING;
        if (   pUrb->enmDir == VUSBDIRECTION_IN
            && pUrb->fShortOk)
        {
            fAttributes |= USB_ATTRS_SHORT_XFER_OK;
        }
        pReq->ctrl_cb             = nemuUSBSolarisCtrlXferCompleted;
        pReq->ctrl_exc_cb         = nemuUSBSolarisCtrlXferCompleted;
        pReq->ctrl_timeout        = NEMUUSB_CTRL_XFER_TIMEOUT;
        pReq->ctrl_attributes     = fAttributes;

        pReq->ctrl_client_private = (usb_opaque_t)pUrb;

        LogFunc((DEVICE_NAME ":nemuUSBSolarisCtrlXfer ctrl_wLength=%#RX16 cbData=%#zx fShortOk=%RTbool\n", pReq->ctrl_wLength,
                 cbData, !!(fAttributes & USB_ATTRS_SHORT_XFER_OK)));
        Log((DEVICE_NAME ":nemuUSBSolarisCtrlXfer %.*Rhxd\n", NEMUUSB_CTRL_XFER_SIZE, pSetupData));

        /*
         * Submit the request.
         */
        rc = usb_pipe_ctrl_xfer(pEp->pPipe, pReq, USB_FLAGS_NOSLEEP);

        if (RT_LIKELY(rc == USB_SUCCESS))
            return VINF_SUCCESS;
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisCtrlXfer usb_pipe_ctrl_xfer failed! rc=%d\n", rc));
            rc = VERR_PIPE_IO_ERROR;
        }

        usb_free_ctrl_req(pReq);
    }
    else
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisCtrlXfer failed to alloc request.\n"));
        rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Completion/Exception callback for Control Xfers.
 *
 * @param   pPipe            The Ctrl pipe handle.
 * @param   pReq             The Ctrl request.
 */
LOCAL void nemuUSBSolarisCtrlXferCompleted(usb_pipe_handle_t pPipe, usb_ctrl_req_t *pReq)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisCtrlXferCompleted pPipe=%p pReq=%p\n", pPipe, pReq));

    nemuusb_urb_t *pUrb   = (nemuusb_urb_t *)pReq->ctrl_client_private;
    if (RT_LIKELY(pUrb))
    {
        /*
         * Funky stuff: We need to reconstruct the header for control transfers.
         * Let us chain along the data and while we dequeue the URB we attempt to
         * concatenate the entire message there.
         */
        mblk_t *pSetupMsg = allocb(sizeof(VUSBSETUP), BPRI_MED);
        if (RT_LIKELY(pSetupMsg))
        {
            VUSBSETUP SetupData;
            SetupData.bmRequestType = pReq->ctrl_bmRequestType;
            SetupData.bRequest = pReq->ctrl_bRequest;
            SetupData.wValue = pReq->ctrl_wValue;
            SetupData.wIndex = pReq->ctrl_wIndex;
            SetupData.wLength = pReq->ctrl_wLength;
            bcopy(&SetupData, pSetupMsg->b_wptr, sizeof(VUSBSETUP));
            pSetupMsg->b_wptr += sizeof(VUSBSETUP);

            /*
             * Should be safe to update pMsg here without the state mutex, see nemuUSBSolarisSendURB()
             * and nemuUSBSolarisQueueURB() as the URB state is (still) not NEMUUSB_URB_STATE_FREE.
             */
            pUrb->pMsg = pSetupMsg;
            pUrb->pMsg->b_cont = pReq->ctrl_data;
            pReq->ctrl_data = NULL;
            nemuUSBSolarisConcatMsg(pUrb);

#ifdef DEBUG_ramshankar
            if (   pUrb->pMsg
                && pUrb->pMsg->b_cont == NULL)  /* Concat succeeded */
            {
                Log((DEVICE_NAME ":nemuUSBSolarisCtrlXferCompleted prepended header rc=%d cbData=%d.\n",
                     pReq->ctrl_completion_reason, MBLKL(pUrb->pMsg)));
                Log((DEVICE_NAME ":%.*Rhxd\n", MBLKL(pUrb->pMsg), pUrb->pMsg->b_rptr));
            }
#endif

            /*
             * Update the URB and move to landed list for reaping.
             */
            nemuUSBSolarisDeQueueURB(pUrb, pReq->ctrl_completion_reason);
        }
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisCtrlXferCompleted failed to alloc %d bytes for Setup Header.\n",
                    sizeof(VUSBSETUP)));
        }
    }
    else
        LogRel((DEVICE_NAME ":nemuUSBSolarisCtrlXferCompleted Extreme error! missing private data.\n"));

    usb_free_ctrl_req(pReq);
}


/**
 * Perform a Bulk Xfer.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint for the Xfer.
 * @param   pUrb            The Nemu USB URB.
 *
 * @returns Nemu status code.
 * @remarks Any errors, the caller should free pUrb->pMsg.
 */
LOCAL int nemuUSBSolarisBulkXfer(nemuusb_state_t *pState, nemuusb_ep_t *pEp, nemuusb_urb_t *pUrb)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisBulkXfer pState=%p pEp=%p pUrb=%p enmDir=%d cbData=%d\n", pState, pEp, pUrb,
             pUrb->enmDir, pUrb->cbDataR3));

    /*
     * Allocate a wrapper request.
     */
    int rc = VINF_SUCCESS;
    usb_bulk_req_t *pReq = usb_alloc_bulk_req(pState->pDip, pUrb->enmDir == VUSBDIRECTION_IN ? pUrb->cbDataR3 : 0,
                                              USB_FLAGS_NOSLEEP);
    if (RT_LIKELY(pReq))
    {
        /*
         * Initialize Bulk Xfer, callbacks and timeouts.
         */
        usb_req_attrs_t fAttributes = USB_ATTRS_AUTOCLEARING;
        if (pUrb->enmDir == VUSBDIRECTION_OUT)
            pReq->bulk_data = pUrb->pMsg;
        else if (   pUrb->enmDir == VUSBDIRECTION_IN
                 && pUrb->fShortOk)
        {
            fAttributes |= USB_ATTRS_SHORT_XFER_OK;
        }

        pReq->bulk_len            = pUrb->cbDataR3;
        pReq->bulk_cb             = nemuUSBSolarisBulkXferCompleted;
        pReq->bulk_exc_cb         = nemuUSBSolarisBulkXferCompleted;
        pReq->bulk_timeout        = NEMUUSB_BULK_XFER_TIMEOUT;
        pReq->bulk_attributes     = fAttributes;
        pReq->bulk_client_private = (usb_opaque_t)pUrb;

        /* Don't obtain state lock here, we're just reading unchanging data... */
        if (RT_UNLIKELY(pUrb->cbDataR3 > pState->cbMaxBulkXfer))
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisBulkXfer requesting %d bytes when only %d bytes supported by device\n",
                        pUrb->cbDataR3, pState->cbMaxBulkXfer));
        }

        /*
         * Submit the request.
         */
        rc = usb_pipe_bulk_xfer(pEp->pPipe, pReq, USB_FLAGS_NOSLEEP);

        if (RT_LIKELY(rc == USB_SUCCESS))
            return VINF_SUCCESS;
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisBulkXfer usb_pipe_bulk_xfer enmDir=%#x Ep=%#x failed! rc=%d\n", pUrb->enmDir,
                    pUrb->bEndpoint, rc));
            rc = VERR_PIPE_IO_ERROR;
        }

        if (pUrb->enmDir == VUSBDIRECTION_OUT) /* pUrb->pMsg freed by caller */
            pReq->bulk_data = NULL;

        usb_free_bulk_req(pReq);
    }
    else
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisBulkXfer failed to alloc bulk request.\n"));
        rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Completion/Exception callback for Bulk Xfers.
 *
 * @param   pPipe           The Bulk pipe handle.
 * @param   pReq            The Bulk request.
 */
LOCAL void nemuUSBSolarisBulkXferCompleted(usb_pipe_handle_t pPipe, usb_bulk_req_t *pReq)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisBulkXferCompleted pPipe=%p pReq=%p\n", pPipe, pReq));

    nemuusb_ep_t *pEp = (nemuusb_ep_t *)usb_pipe_get_private(pPipe);
    if (RT_LIKELY(pEp))
    {
        nemuusb_urb_t *pUrb = (nemuusb_urb_t *)pReq->bulk_client_private;
        if (RT_LIKELY(pUrb))
        {
            if (pUrb->enmDir == VUSBDIRECTION_OUT)
                pReq->bulk_data = NULL;
            else
            {
                if (pReq->bulk_completion_reason == USB_CR_OK)
                {
                    pUrb->pMsg = pReq->bulk_data;
                    pReq->bulk_data = NULL;
                    nemuUSBSolarisConcatMsg(pUrb);
                }
            }

            Log((DEVICE_NAME ":nemuUSBSolarisBulkXferCompleted %s. rc=%d cbData=%d\n",
                    pReq->bulk_completion_reason != USB_CR_OK ? "failed URB" : "success",
                    pReq->bulk_completion_reason, pUrb->pMsg ? MBLKL(pUrb->pMsg) : 0));

            /*
             * Update the URB and move to tail for reaping.
             */
            nemuUSBSolarisDeQueueURB(pUrb, pReq->bulk_completion_reason);
            usb_free_bulk_req(pReq);
            return;
        }
        else
            LogRel((DEVICE_NAME ":nemuUSBSolarisBulkXferCompleted Extreme error! private request data missing.\n"));
    }
    else
        Log((DEVICE_NAME ":nemuUSBSolarisBulkXferCompleted Pipe Gone.\n"));

    usb_free_bulk_req(pReq);
}


/**
 * Perform an Interrupt Xfer.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint for the Xfer.
 * @param   pUrb            The Nemu USB URB.
 *
 * @returns Nemu status code.
 * @remarks Any errors, the caller should free pUrb->pMsg.
 */
LOCAL int nemuUSBSolarisIntrXfer(nemuusb_state_t *pState, nemuusb_ep_t *pEp, nemuusb_urb_t *pUrb)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisIntrXfer pState=%p pEp=%p pUrb=%p enmDir=%d cbData=%d\n", pState, pEp, pUrb,
             pUrb->enmDir, pUrb->cbDataR3));

    int rc = VINF_SUCCESS;
    usb_intr_req_t *pReq = usb_alloc_intr_req(pState->pDip, 0 /* length */, USB_FLAGS_NOSLEEP);
    if (RT_LIKELY(pReq))
    {
        /*
         * Initialize Intr Xfer, callbacks & timeouts.
         */
        if (pUrb->enmDir == VUSBDIRECTION_OUT)
        {
            pReq->intr_data       = pUrb->pMsg;
            pReq->intr_attributes = USB_ATTRS_AUTOCLEARING;
        }
        else
        {
            Assert(pUrb->enmDir == VUSBDIRECTION_IN);
            pReq->intr_data       = NULL;
            pReq->intr_attributes = USB_ATTRS_AUTOCLEARING | USB_ATTRS_ONE_XFER | (pUrb->fShortOk ? USB_ATTRS_SHORT_XFER_OK : 0);
        }

        pReq->intr_len            = pUrb->cbDataR3; /* Not pEp->EpDesc.wMaxPacketSize */
        pReq->intr_cb             = nemuUSBSolarisIntrXferCompleted;
        pReq->intr_exc_cb         = nemuUSBSolarisIntrXferCompleted;
        pReq->intr_timeout        = NEMUUSB_INTR_XFER_TIMEOUT;
        pReq->intr_client_private = (usb_opaque_t)pUrb;

        /*
         * Submit the request.
         */
        rc = usb_pipe_intr_xfer(pEp->pPipe, pReq, USB_FLAGS_NOSLEEP);

        if (RT_LIKELY(rc == USB_SUCCESS))
            return VINF_SUCCESS;
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisIntrXfer usb_pipe_intr_xfer failed! rc=%d\n", rc));
            rc = VERR_PIPE_IO_ERROR;
        }

        pReq->intr_data = NULL;
        usb_free_intr_req(pReq);
    }
    else
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisIntrXfer failed to alloc intr request.\n"));
        rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Completion/Exception callback for Intr Xfers.
 *
 * @param   pPipe           The Intr pipe handle.
 * @param   pReq            The Intr request.
 */
LOCAL void nemuUSBSolarisIntrXferCompleted(usb_pipe_handle_t pPipe, usb_intr_req_t *pReq)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisIntrXferCompleted pPipe=%p pReq=%p\n", pPipe, pReq));

    nemuusb_ep_t *pEp = (nemuusb_ep_t *)usb_pipe_get_private(pPipe);
    if (RT_LIKELY(pEp))
    {
        nemuusb_urb_t *pUrb = (nemuusb_urb_t *)pReq->intr_client_private;
        if (RT_LIKELY(pUrb))
        {
            if (pUrb->enmDir == VUSBDIRECTION_OUT)
                pReq->intr_data = NULL;
            else
            {
                if (pReq->intr_completion_reason == USB_CR_OK)
                {
                    pUrb->pMsg = pReq->intr_data;
                    pReq->intr_data = NULL;
                }
            }

            Log((DEVICE_NAME ":nemuUSBSolarisIntrXferCompleted rc=%d pMsg=%p enmDir=%#x\n", pReq->intr_completion_reason,
                 pUrb->pMsg, pUrb->enmDir));

            /*
             * Update the URB and move to landed list for reaping.
             */
            nemuUSBSolarisDeQueueURB(pUrb, pReq->intr_completion_reason);
            usb_free_intr_req(pReq);
            return;
        }
        else
            LogRel((DEVICE_NAME ":nemuUSBSolarisIntrXferCompleted Extreme error! private request data missing.\n"));
    }
    else
        Log((DEVICE_NAME ":nemuUSBSolarisIntrXferCompleted Pipe Gone.\n"));

    usb_free_intr_req(pReq);
}


/**
 * Perform an Isochronous Xfer.
 *
 * @param   pState          The USB device instance.
 * @param   pEp             The Endpoint for the Xfer.
 * @param   pUrb            The Nemu USB URB.
 *
 * @returns Nemu status code.
 * @remarks Any errors, the caller should free pUrb->pMsg.
 */
LOCAL int nemuUSBSolarisIsocXfer(nemuusb_state_t *pState, nemuusb_ep_t *pEp, nemuusb_urb_t *pUrb)
{
//    LogFunc((DEVICE_NAME ":nemuUSBSolarisIsocXfer pState=%p pEp=%p pUrb=%p\n", pState, pEp, pUrb));

    /*
     * For Isoc. IN transfers we perform one request and USBA polls the device continuously
     * and supplies our Xfer callback with input data. We cannot perform one-shot Isoc. In transfers.
     */
    size_t cbData = (pUrb->enmDir == VUSBDIRECTION_IN ? pUrb->cIsocPkts * pUrb->aIsocPkts[0].cbPkt : 0);
    if (pUrb->enmDir == VUSBDIRECTION_IN)
    {
        Log((DEVICE_NAME ":nemuUSBSolarisIsocXfer Isoc. In queueing.\n"));

        mutex_enter(&pState->Mtx);
        if (pEp->fIsocPolling)
        {
            /*
             * Queue a maximum of cbMaxIsocData bytes, else fail.
             */
            if (pEp->cbIsocInLandedReqs + cbData > pEp->cbMaxIsocData)
            {
                mutex_exit(&pState->Mtx);
                Log((DEVICE_NAME ":nemuUSBSolarisIsocXfer Max Isoc. data %d bytes queued\n", pEp->cbMaxIsocData));
                return VERR_TOO_MUCH_DATA;
            }

            list_insert_tail(&pEp->hIsocInUrbs, pUrb);
            ++pEp->cIsocInUrbs;

            mutex_exit(&pState->Mtx);
            return VINF_SUCCESS;
        }
        mutex_exit(&pState->Mtx);
    }

    int rc = VINF_SUCCESS;
    usb_isoc_req_t *pReq = usb_alloc_isoc_req(pState->pDip, pUrb->cIsocPkts, cbData, USB_FLAGS_NOSLEEP);
    Log((DEVICE_NAME ":nemuUSBSolarisIsocXfer enmDir=%#x cIsocPkts=%d aIsocPkts[0]=%d cbDataR3=%d\n", pUrb->enmDir,
                    pUrb->cIsocPkts, pUrb->aIsocPkts[0].cbPkt, pUrb->cbDataR3));
    if (RT_LIKELY(pReq))
    {
        /*
         * Initialize Isoc Xfer, callbacks & timeouts.
         */
        for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
            pReq->isoc_pkt_descr[i].isoc_pkt_length = pUrb->aIsocPkts[i].cbPkt;

        if (pUrb->enmDir == VUSBDIRECTION_OUT)
        {
            pReq->isoc_data           = pUrb->pMsg;
            pReq->isoc_attributes     = USB_ATTRS_AUTOCLEARING | USB_ATTRS_ISOC_XFER_ASAP;
            pReq->isoc_cb             = nemuUSBSolarisIsocOutXferCompleted;
            pReq->isoc_exc_cb         = nemuUSBSolarisIsocOutXferCompleted;
            pReq->isoc_client_private = (usb_opaque_t)pUrb;
        }
        else
        {
            pReq->isoc_attributes     = USB_ATTRS_AUTOCLEARING | USB_ATTRS_ISOC_XFER_ASAP | USB_ATTRS_SHORT_XFER_OK;
            pReq->isoc_cb             = nemuUSBSolarisIsocInXferCompleted;
            pReq->isoc_exc_cb         = nemuUSBSolarisIsocInXferError;
            pReq->isoc_client_private = (usb_opaque_t)pState;
        }
        pReq->isoc_pkts_count         = pUrb->cIsocPkts;
        pReq->isoc_pkts_length        = 0;  /* auto compute */

        /*
         * Submit the request.
         */
        rc = usb_pipe_isoc_xfer(pEp->pPipe, pReq, USB_FLAGS_NOSLEEP);
        if (RT_LIKELY(rc == USB_SUCCESS))
        {
            if (pUrb->enmDir == VUSBDIRECTION_IN)
            {
                /*
                 * Add the first Isoc. IN URB to the queue as well.
                 */
                mutex_enter(&pState->Mtx);
                list_insert_tail(&pEp->hIsocInUrbs, pUrb);
                ++pEp->cIsocInUrbs;
                pEp->fIsocPolling = true;
                mutex_exit(&pState->Mtx);
            }

            return VINF_SUCCESS;
        }
        else
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisIsocXfer usb_pipe_isoc_xfer failed! rc=%d\n", rc));
            rc = VERR_PIPE_IO_ERROR;

            if (pUrb->enmDir == VUSBDIRECTION_IN)
            {
                mutex_enter(&pState->Mtx);
                nemuusb_urb_t *pIsocFailedUrb = list_remove_tail(&pEp->hIsocInUrbs);
                if (pIsocFailedUrb)
                {
                    RTMemFree(pIsocFailedUrb);
                    --pEp->cIsocInUrbs;
                }
                pEp->fIsocPolling = false;
                mutex_exit(&pState->Mtx);
            }
        }

        if (pUrb->enmDir == VUSBDIRECTION_OUT) /* pUrb->pMsg freed by caller */
            pReq->isoc_data = NULL;

        usb_free_isoc_req(pReq);
    }
    else
    {
        LogRel((DEVICE_NAME ":nemuUSBSolarisIsocXfer failed to alloc isoc req for %d packets\n", pUrb->cIsocPkts));
        rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Completion/Exception callback for Isoc IN Xfers.
 *
 * @param   pPipe           The Intr pipe handle.
 * @param   pReq            The Intr request.
 *
 * @remarks Completion callback executes in interrupt context!
 */
LOCAL void nemuUSBSolarisIsocInXferCompleted(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq)
{
//    LogFunc((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted pPipe=%p pReq=%p\n", pPipe, pReq));

    nemuusb_state_t *pState = (nemuusb_state_t *)pReq->isoc_client_private;
    if (RT_LIKELY(pState))
    {
        nemuusb_ep_t *pEp = (nemuusb_ep_t *)usb_pipe_get_private(pPipe);
        if (   pEp
            && pEp->pPipe)
        {
#if 0
            /*
             * Stop polling if all packets failed.
             */
            if (pReq->isoc_error_count == pReq->isoc_pkts_count)
            {
                Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted stopping polling! Too many errors.\n"));
                mutex_exit(&pState->Mtx);
                usb_pipe_stop_isoc_polling(pPipe, USB_FLAGS_NOSLEEP);
                mutex_enter(&pState->Mtx);
                pEp->fIsocPolling = false;
            }
#endif

            AssertCompile(sizeof(VUSBISOC_PKT_DESC) == sizeof(usb_isoc_pkt_descr_t));

            if (RT_LIKELY(pReq->isoc_data))
            {
                Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted cIsocInUrbs=%d cbIsocInLandedReqs=%d\n", pEp->cIsocInUrbs,
                     pEp->cbIsocInLandedReqs));

                mutex_enter(&pState->Mtx);

                /*
                 * If there are waiting URBs, satisfy the oldest one.
                 */
                if (   pEp->cIsocInUrbs > 0
                    && pEp->cbIsocInLandedReqs == 0)
                {
                    nemuusb_urb_t *pUrb = list_remove_head(&pEp->hIsocInUrbs);
                    if (RT_LIKELY(pUrb))
                    {
                        --pEp->cIsocInUrbs;
                        mutex_exit(&pState->Mtx);

                        for (unsigned i = 0; i < pReq->isoc_pkts_count; i++)
                        {
                            pUrb->aIsocPkts[i].cbActPkt = pReq->isoc_pkt_descr[i].isoc_pkt_actual_length;
                            pUrb->aIsocPkts[i].enmStatus = nemuUSBSolarisGetUrbStatus(pReq->isoc_pkt_descr[i].isoc_pkt_status);
                        }

                        pUrb->pMsg = pReq->isoc_data;
                        pReq->isoc_data = NULL;

                        /*
                         * Move to landed list
                         */
                        mutex_enter(&pState->Mtx);
                        list_insert_tail(&pState->hLandedUrbs, pUrb);
                        nemuUSBSolarisNotifyComplete(pState);
                    }
                    else
                    {
                        /* Huh!? cIsocInUrbs is wrong then! Should never happen unless we decide to decrement cIsocInUrbs in
                           Reap time */
                        pEp->cIsocInUrbs = 0;
                        LogRel((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted Extreme error! Isoc. counter b0rked!\n"));
                    }

                    mutex_exit(&pState->Mtx);
                    usb_free_isoc_req(pReq);
                    return;
                }

#if 0
                /*
                 * If the maximum buffer size is reached, discard the oldest data.
                 */
                if (pEp->cbIsocInLandedReqs + MBLKL(pReq->isoc_data) > pEp->cbMaxIsocData)
                {
                    nemuusb_isoc_req_t *pOldReq = list_remove_head(&pEp->hIsocInLandedReqs);
                    if (RT_LIKELY(pOldReq))
                    {
                        pEp->cbIsocInLandedReqs -= MBLKL(pOldReq->pMsg);
                        kmem_free(pOldReq, sizeof(nemuusb_isoc_req_t));
                    }
                }

                mutex_exit(&pState->Mtx);

                /*
                 * Buffer incoming data if the guest has not yet queued any Input URBs.
                 */
                Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted Buffering\n"));
                nemuusb_isoc_req_t *pIsocReq = kmem_alloc(sizeof(nemuusb_isoc_req_t), KM_NOSLEEP);
                if (RT_LIKELY(pIsocReq))
                {
                    pIsocReq->pMsg = pReq->isoc_data;
                    pReq->isoc_data = NULL;
                    pIsocReq->cIsocPkts = pReq->isoc_pkts_count;
#if 0
                    for (unsigned i = 0; i < pReq->isoc_pkts_count; i++)
                    {
                        pIsocReq->aIsocPkts[i].cbActPkt = pReq->isoc_pkt_descr[i].isoc_pkt_actual_length;
                        pIsocReq->aIsocPkts[i].enmStatus = nemuUSBSolarisGetUrbStatus(pReq->isoc_pkt_descr[i].isoc_pkt_status);
                    }
#else
                    bcopy(pReq->isoc_pkt_descr, pIsocReq->aIsocPkts, pReq->isoc_pkts_count * sizeof(VUSBISOC_PKT_DESC));
#endif

                    mutex_enter(&pState->Mtx);
                    list_insert_tail(&pEp->hIsocInLandedReqs, pIsocReq);
                    pEp->cbIsocInLandedReqs += MBLKL(pIsocReq->pMsg);
                    mutex_exit(&pState->Mtx);
                }
                else
                {
                    LogRel((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted failed to alloc %d bytes for Isoc. queueing\n",
                            sizeof(nemuusb_isoc_req_t)));
                }

                /*
                 * Drain the input URB buffer with the device buffer, queueing them with the landed URBs.
                 */
                mutex_enter(&pState->Mtx);
                while (pEp->cIsocInUrbs)
                {
                    nemuusb_urb_t *pUrb = list_remove_head(&pEp->hIsocInUrbs);
                    if (RT_UNLIKELY(!pUrb))
                        break;

                    nemuusb_isoc_req_t *pBuffReq = list_remove_head(&pEp->hIsocInLandedReqs);
                    if (!pBuffReq)
                    {
                        list_insert_head(&pEp->hIsocInUrbs, pUrb);
                        break;
                    }

                    --pEp->cIsocInUrbs;
                    pEp->cbIsocInLandedReqs -= MBLKL(pBuffReq->pMsg);
                    mutex_exit(&pState->Mtx);

#if 0
                    for (unsigned i = 0; i < pBuffReq->cIsocPkts; i++)
                    {
                        pUrb->aIsocPkts[i].cbActPkt = pBuffReq->aIsocPkts[i].cbActPkt;
                        pUrb->aIsocPkts[i].enmStatus = pBuffReq->aIsocPkts[i].enmStatus;
                    }
#else
                    bcopy(pBuffReq->aIsocPkts, pUrb->aIsocPkts, pBuffReq->cIsocPkts * sizeof(VUSBISOC_PKT_DESC));
#endif
                    pUrb->pMsg = pBuffReq->pMsg;
                    pBuffReq->pMsg = NULL;
                    kmem_free(pBuffReq, sizeof(nemuusb_isoc_req_t));

                    /*
                     * Move to landed list
                     */
                    mutex_enter(&pState->Mtx);
                    list_insert_tail(&pState->hLandedUrbs, pUrb);
                    nemuUSBSolarisNotifyComplete(pState);
                }
#endif

                mutex_exit(&pState->Mtx);
                usb_free_isoc_req(pReq);
                return;
            }
            else
                LogRel((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted data missing.\n"));
        }
        else
            LogRel((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted Pipe Gone.\n"));
    }
    else
        Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferCompleted State Gone.\n"));

    usb_free_isoc_req(pReq);
}


/**
 * Exception callback for Isoc IN Xfers.
 *
 * @param   pPipe           The Intr pipe handle.
 * @param   pReq            The Intr request.
 * @remarks Completion callback executes in interrupt context!
 */
LOCAL void nemuUSBSolarisIsocInXferError(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisIsocInXferError pPipe=%p pReq=%p\n", pPipe, pReq));

    nemuusb_state_t *pState = (nemuusb_state_t *)pReq->isoc_client_private;
    if (RT_UNLIKELY(!pState))
    {
        Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferError State Gone.\n"));
        usb_free_isoc_req(pReq);
        return;
    }

    mutex_enter(&pState->Mtx);
    nemuusb_ep_t *pEp = (nemuusb_ep_t *)usb_pipe_get_private(pPipe);
    if (RT_UNLIKELY(!pEp))
    {
        Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferError Pipe Gone.\n"));
        mutex_exit(&pState->Mtx);
        usb_free_isoc_req(pReq);
        return;
    }

    switch(pReq->isoc_completion_reason)
    {
        case USB_CR_NO_RESOURCES:
        {
            /*
             * Resubmit the request in case the original request did not complete due to
             * immediately unavailable requests
             */
            mutex_exit(&pState->Mtx);
            usb_pipe_isoc_xfer(pPipe, pReq, USB_FLAGS_NOSLEEP);
            Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferError resubmitted Isoc. IN request due to immediately unavailable "
                 "resources.\n"));

            return;
        }

        case USB_CR_PIPE_CLOSING:
        case USB_CR_STOPPED_POLLING:
        case USB_CR_PIPE_RESET:
        {
            pEp->fIsocPolling = false;
            usb_free_isoc_req(pReq);
            break;
        }

        default:
        {
            Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferError stopping Isoc. In. polling due to rc=%d\n",
                 pReq->isoc_completion_reason));
            pEp->fIsocPolling = false;
            mutex_exit(&pState->Mtx);
            usb_pipe_stop_isoc_polling(pPipe, USB_FLAGS_NOSLEEP);
            usb_free_isoc_req(pReq);
            mutex_enter(&pState->Mtx);
            break;
        }
    }

    /*
     * Dequeue i.e. delete the last queued Isoc In. URB. as failed.
     */
    nemuusb_urb_t *pUrb = list_remove_tail(&pEp->hIsocInUrbs);
    if (pUrb)
    {
        --pEp->cIsocInUrbs;
        Log((DEVICE_NAME ":nemuUSBSolarisIsocInXferError Deleting last queued URB as it failed.\n"));
        freemsg(pUrb->pMsg);
        RTMemFree(pUrb);
        nemuUSBSolarisNotifyComplete(pState);
    }

    mutex_exit(&pState->Mtx);
}


/**
 * Completion/Exception callback for Isoc OUT Xfers.
 *
 * @param   pPipe           The Intr pipe handle.
 * @param   pReq            The Intr request.
 * @remarks Completion callback executes in interrupt context!
 */
LOCAL void nemuUSBSolarisIsocOutXferCompleted(usb_pipe_handle_t pPipe, usb_isoc_req_t *pReq)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisIsocOutXferCompleted pPipe=%p pReq=%p\n", pPipe, pReq));

    nemuusb_ep_t *pEp = (nemuusb_ep_t *)usb_pipe_get_private(pPipe);
    if (RT_LIKELY(pEp))
    {
        nemuusb_urb_t *pUrb = (nemuusb_urb_t *)pReq->isoc_client_private;
        if (RT_LIKELY(pUrb))
        {
            size_t cbActPkt = 0;
            for (int i = 0; i < pReq->isoc_pkts_count; i++)
            {
                cbActPkt += pReq->isoc_pkt_descr[i].isoc_pkt_actual_length;
                pUrb->aIsocPkts[i].cbActPkt = pReq->isoc_pkt_descr[i].isoc_pkt_actual_length;
                pUrb->aIsocPkts[i].enmStatus = nemuUSBSolarisGetUrbStatus(pReq->isoc_pkt_descr[i].isoc_pkt_status);
            }

            Log((DEVICE_NAME ":nemuUSBSolarisIsocOutXferCompleted cIsocPkts=%d cbData=%d cbActPkt=%d\n", pUrb->cIsocPkts,
                 pUrb->cbDataR3, cbActPkt));

            if (pReq->isoc_completion_reason == USB_CR_OK)
            {
                if (RT_UNLIKELY(pUrb->pMsg != pReq->isoc_data))  /* Paranoia */
                {
                    freemsg(pUrb->pMsg);
                    pUrb->pMsg = pReq->isoc_data;
                }
            }
            pReq->isoc_data = NULL;

            pUrb->cIsocPkts = pReq->isoc_pkts_count;
            pUrb->cbDataR3 = cbActPkt;

            /*
             * Update the URB and move to landed list for reaping.
             */
            nemuUSBSolarisDeQueueURB(pUrb, pReq->isoc_completion_reason);
            usb_free_isoc_req(pReq);
            return;
        }
        else
            Log((DEVICE_NAME ":nemuUSBSolarisIsocOutXferCompleted missing private data!?! Dropping OUT pUrb.\n"));
    }
    else
        Log((DEVICE_NAME ":nemuUSBSolarisIsocOutXferCompleted Pipe Gone.\n"));

    usb_free_isoc_req(pReq);
}


/**
 * Callback when the device gets disconnected.
 *
 * @param   pDip            The module structure instance.
 *
 * @returns Solaris USB error code.
 */
LOCAL int nemuUSBSolarisDeviceDisconnected(dev_info_t *pDip)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDeviceDisconnected pDip=%p\n", pDip));

    int instance = ddi_get_instance(pDip);
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);

    if (RT_LIKELY(pState))
    {
        /*
         * Serialize access: exclusive access to the state.
         */
        usb_serialize_access(pState->StateMulti, USB_WAIT, 0);
        mutex_enter(&pState->Mtx);

        pState->DevState = USB_DEV_DISCONNECTED;

        nemuUSBSolarisCloseAllPipes(pState, true /* ControlPipe */);
        nemuUSBSolarisNotifyHotplug(pState);

        mutex_exit(&pState->Mtx);
        usb_release_access(pState->StateMulti);

        return USB_SUCCESS;
    }

    LogRel((DEVICE_NAME ":nemuUSBSolarisDeviceDisconnected failed to get device state!\n"));
    return USB_FAILURE;
}


/**
 * Callback when the device gets reconnected.
 *
 * @param   pDip            The module structure instance.
 *
 * @returns Solaris USB error code.
 */
LOCAL int nemuUSBSolarisDeviceReconnected(dev_info_t *pDip)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDeviceReconnected pDip=%p\n", pDip));

    int instance = ddi_get_instance(pDip);
    nemuusb_state_t *pState = ddi_get_soft_state(g_pNemuUSBSolarisState, instance);

    if (RT_LIKELY(pState))
    {
        nemuUSBSolarisDeviceRestore(pState);
        return USB_SUCCESS;
    }

    LogRel((DEVICE_NAME ":nemuUSBSolarisDeviceReconnected failed to get device state!\n"));
    return USB_FAILURE;
}


/**
 * Restore device state after a reconnect or resume.
 *
 * @param   pState          The USB device instance.
 */
LOCAL void nemuUSBSolarisDeviceRestore(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDeviceRestore pState=%p\n", pState));
    AssertPtrReturnVoid(pState);

    /*
     * Raise device power.
     */
    nemuUSBSolarisPowerBusy(pState);
    int rc = pm_raise_power(pState->pDip, 0 /* component */, USB_DEV_OS_FULL_PWR);

    /*
     * Check if the same device is resumed/reconnected.
     */
    rc = usb_check_same_device(pState->pDip,
                                NULL,           /* log handle */
                                USB_LOG_L2,     /* log level */
                                -1,             /* log mask */
                                USB_CHK_ALL,    /* check level */
                                NULL);          /* device string */

    if (rc != USB_SUCCESS)
    {
        mutex_enter(&pState->Mtx);
        pState->DevState = USB_DEV_DISCONNECTED;
        mutex_exit(&pState->Mtx);

        /* Do we need to inform userland here? */
        nemuUSBSolarisPowerIdle(pState);
        Log((DEVICE_NAME ":nemuUSBSolarisDeviceRestore not the same device.\n"));
        return;
    }

    /*
     * Serialize access to not race with other PM functions.
     */
    usb_serialize_access(pState->StateMulti, USB_WAIT, 0);

    mutex_enter(&pState->Mtx);
    if (pState->DevState == USB_DEV_DISCONNECTED)
        pState->DevState = USB_DEV_ONLINE;
    else if (pState->DevState == USB_DEV_SUSPENDED)
        pState->DevState = USB_DEV_ONLINE;

    mutex_exit(&pState->Mtx);
    usb_release_access(pState->StateMulti);

    nemuUSBSolarisPowerIdle(pState);
}


/**
 * Restore device state after a reconnect or resume.
 *
 * @param   pState          The USB device instance.
 *
 * @returns Nemu status code.
 */
LOCAL int nemuUSBSolarisDeviceSuspend(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDeviceSuspend pState=%p\n", pState));

    int rc = VERR_VUSB_DEVICE_IS_SUSPENDED;
    mutex_enter(&pState->Mtx);

    switch (pState->DevState)
    {
        case USB_DEV_SUSPENDED:
        {
            LogRel((DEVICE_NAME ":nemuUSBSolarisDeviceSuspend: Invalid device state %d\n", pState->DevState));
            break;
        }

        case USB_DEV_ONLINE:
        case USB_DEV_DISCONNECTED:
        case USB_DEV_PWRED_DOWN:
        {
            int PreviousState = pState->DevState;
            pState->DevState = USB_DEV_DISCONNECTED;

            /*
             * Drain pending URBs.
             */
            for (int i = 0; i < NEMUUSB_DRAIN_TIME; i++)
            {
                if (pState->cInflightUrbs < 1)
                    break;

                mutex_exit(&pState->Mtx);
                delay(drv_usectohz(100000));
                mutex_enter(&pState->Mtx);
            }

            /*
             * Deny suspend if we still have pending URBs.
             */
            if (pState->cInflightUrbs > 0)
            {
                pState->DevState = PreviousState;
                LogRel((DEVICE_NAME ":Cannot suspend, still have %d inflight URBs.\n", pState->cInflightUrbs));

                mutex_exit(&pState->Mtx);
                return VERR_RESOURCE_BUSY;
            }

            pState->cInflightUrbs = 0;

            /*
             * Serialize access to not race with Open/Detach/Close and
             * Close all pipes including the default pipe.
             */
            mutex_exit(&pState->Mtx);
            usb_serialize_access(pState->StateMulti, USB_WAIT, 0);
            mutex_enter(&pState->Mtx);

            nemuUSBSolarisCloseAllPipes(pState, true /* default pipe */);
            nemuUSBSolarisNotifyHotplug(pState);

            mutex_exit(&pState->Mtx);
            usb_release_access(pState->StateMulti);
            return VINF_SUCCESS;
        }
    }

    mutex_exit(&pState->Mtx);
    Log((DEVICE_NAME ":nemuUSBSolarisDeviceSuspend returns %d\n", rc));
    return rc;
}


/**
 * Restore device state after a reconnect or resume.
 *
 * @param   pState          The USB device instance.
 */
LOCAL void nemuUSBSolarisDeviceResume(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisDeviceResume pState=%p\n", pState));
    return nemuUSBSolarisDeviceRestore(pState);
}


/**
 * Flag the PM component as busy so the system will not manage it's power.
 *
 * @param   pState          The USB device instance.
 */
LOCAL void nemuUSBSolarisPowerBusy(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisPowerBusy pState=%p\n", pState));
    AssertPtrReturnVoid(pState);

    mutex_enter(&pState->Mtx);
    if (pState->pPower)
    {
        pState->pPower->PowerBusy++;
        mutex_exit(&pState->Mtx);

        int rc = pm_busy_component(pState->pDip, 0 /* component */);
        if (rc != DDI_SUCCESS)
        {
            Log((DEVICE_NAME ":nemuUSBSolarisPowerBusy busy component failed! rc=%d\n", rc));
            mutex_enter(&pState->Mtx);
            pState->pPower->PowerBusy--;
            mutex_exit(&pState->Mtx);
        }
    }
    else
        mutex_exit(&pState->Mtx);
}


/**
 * Flag the PM component as idle so its power managed by the system.
 *
 * @param   pState          The USB device instance.
 */
LOCAL void nemuUSBSolarisPowerIdle(nemuusb_state_t *pState)
{
    LogFunc((DEVICE_NAME ":nemuUSBSolarisPowerIdle pState=%p\n", pState));
    AssertPtrReturnVoid(pState);

    if (pState->pPower)
    {
        int rc = pm_idle_component(pState->pDip, 0 /* component */);
        if (rc == DDI_SUCCESS)
        {
            mutex_enter(&pState->Mtx);
            Assert(pState->pPower->PowerBusy > 0);
            pState->pPower->PowerBusy--;
            mutex_exit(&pState->Mtx);
        }
        else
            Log((DEVICE_NAME ":nemuUSBSolarisPowerIdle idle component failed! rc=%d\n", rc));
    }
}

