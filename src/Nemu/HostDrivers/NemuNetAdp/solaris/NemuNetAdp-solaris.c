/* $Id: NemuNetAdp-solaris.c $ */
/** @file
 * NemuNetAdapter - Network Adapter Driver (Host), Solaris Specific Code.
 */

/*
 * Copyright (C) 2009-2015 Oracle Corporation
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
#define LOG_GROUP LOG_GROUP_NET_ADP_DRV
#include <Nemu/log.h>
#include <Nemu/err.h>
#include <Nemu/version.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/rand.h>

#include <sys/types.h>
#include <sys/dlpi.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strsun.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/gld.h>

#include "../NemuNetAdpInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define DEVICE_NAME              "nemunet"
/** The module descriptions as seen in 'modinfo'. */
#define DEVICE_DESC_DRV          "VirtualBox NetAdp"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int NemuNetAdpSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd);
static int NemuNetAdpSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd);
static int NemuNetAdpSolarisQuiesceNotNeeded(dev_info_t *pDip);

/**
 * Streams: module info.
 */
static struct module_info g_NemuNetAdpSolarisModInfo =
{
    0x0dd,                            /* module id */
    DEVICE_NAME,
    0,                                /* min. packet size */
    INFPSZ,                           /* max. packet size */
    0,                                /* hi-water mark */
    0                                 /* lo-water mark */
};

/**
 * Streams: read queue hooks.
 */
static struct qinit g_NemuNetAdpSolarisReadQ =
{
    NULL,                             /* read */
    gld_rsrv,
    gld_open,
    gld_close,
    NULL,                             /* admin (reserved) */
    &g_NemuNetAdpSolarisModInfo,
    NULL                              /* module stats */
};

/**
 * Streams: write queue hooks.
 */
static struct qinit g_NemuNetAdpSolarisWriteQ =
{
    gld_wput,
    gld_wsrv,
    NULL,                             /* open */
    NULL,                             /* close */
    NULL,                             /* admin (reserved) */
    &g_NemuNetAdpSolarisModInfo,
    NULL                              /* module stats */
};

/**
 * Streams: IO stream tab.
 */
static struct streamtab g_NemuNetAdpSolarisStreamTab =
{
    &g_NemuNetAdpSolarisReadQ,
    &g_NemuNetAdpSolarisWriteQ,
    NULL,                           /* muxread init */
    NULL                            /* muxwrite init */
};

/**
 * cb_ops: driver char/block entry points
 */
static struct cb_ops g_NemuNetAdpSolarisCbOps =
{
    nulldev,                        /* cb open */
    nulldev,                        /* cb close */
    nodev,                          /* b strategy */
    nodev,                          /* b dump */
    nodev,                          /* b print */
    nodev,                          /* cb read */
    nodev,                          /* cb write */
    nodev,                          /* cb ioctl */
    nodev,                          /* c devmap */
    nodev,                          /* c mmap */
    nodev,                          /* c segmap */
    nochpoll,                       /* c poll */
    ddi_prop_op,                    /* property ops */
    &g_NemuNetAdpSolarisStreamTab,
    D_MP,                           /* compat. flag */
    CB_REV                          /* revision */
};

/**
 * dev_ops: driver entry/exit and other ops.
 */
static struct dev_ops g_NemuNetAdpSolarisDevOps =
{
    DEVO_REV,                       /* driver build revision */
    0,                              /* ref count */
    gld_getinfo,
    nulldev,                        /* identify */
    nulldev,                        /* probe */
    NemuNetAdpSolarisAttach,
    NemuNetAdpSolarisDetach,
    nodev,                          /* reset */
    &g_NemuNetAdpSolarisCbOps,
    (struct bus_ops *)0,
    nodev,                          /* power */
    NemuNetAdpSolarisQuiesceNotNeeded
};

/**
 * modldrv: export driver specifics to kernel
 */
static struct modldrv g_NemuNetAdpSolarisDriver =
{
    &mod_driverops,                 /* extern from kernel */
    DEVICE_DESC_DRV " " NEMU_VERSION_STRING "r"  RT_XSTR(NEMU_SVN_REV),
    &g_NemuNetAdpSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_NemuNetAdpSolarisModLinkage =
{
    MODREV_1,                       /* loadable module system revision */
    {
        &g_NemuNetAdpSolarisDriver, /* adapter streams driver framework */
        NULL                        /* terminate array of linkage structures */
    }
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The default ethernet broadcast address */
static uchar_t achBroadcastAddr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/**
 * nemunetadp_state_t: per-instance data
 */
typedef struct nemunetadp_state_t
{
    dev_info_t   *pDip;           /* device info. */
    RTMAC         FactoryMac;     /* default 'factory' MAC address */
    RTMAC         CurrentMac;     /* current MAC address */
} nemunetadp_state_t;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int nemuNetAdpSolarisGenerateMac(PRTMAC pMac);
static int nemuNetAdpSolarisSetMacAddress(gld_mac_info_t *pMacInfo, unsigned char *pszMacAddr);
static int nemuNetAdpSolarisSend(gld_mac_info_t *pMacInfo, mblk_t *pMsg);
static int nemuNetAdpSolarisStub(gld_mac_info_t *pMacInfo);
static int nemuNetAdpSolarisSetPromisc(gld_mac_info_t *pMacInfo, int fPromisc);
static int nemuNetAdpSolarisSetMulticast(gld_mac_info_t *pMacInfo, unsigned char *pMulticastAddr, int fMulticast);
static int nemuNetAdpSolarisGetStats(gld_mac_info_t *pMacInfo, struct gld_stats *pStats);


/**
 * Kernel entry points
 */
int _init(void)
{
    LogFunc((DEVICE_NAME ":_init\n"));

    /*
     * Prevent module autounloading.
     */
    modctl_t *pModCtl = mod_getctl(&g_NemuNetAdpSolarisModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;
    else
        LogRel((DEVICE_NAME ":failed to disable autounloading!\n"));

    /*
     * Initialize IPRT.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        rc = mod_install(&g_NemuNetAdpSolarisModLinkage);
        if (!rc)
            return rc;

        LogRel((DEVICE_NAME ":mod_install failed. rc=%d\n", rc));
        RTR0Term();
    }
    else
        LogRel((DEVICE_NAME ":failed to initialize IPRT (rc=%d)\n", rc));

    return RTErrConvertToErrno(rc);
}


int _fini(void)
{
    LogFunc((DEVICE_NAME ":_fini\n"));

    /*
     * Undo the work done during start (in reverse order).
     */
    int rc = mod_remove(&g_NemuNetAdpSolarisModLinkage);
    if (!rc)
        RTR0Term();

    return rc;
}


int _info(struct modinfo *pModInfo)
{
    LogFunc((DEVICE_NAME ":_info\n"));

    int rc = mod_info(&g_NemuNetAdpSolarisModLinkage, pModInfo);

    Log((DEVICE_NAME ":_info returns %d\n", rc));
    return rc;
}


/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Operation type (attach/resume).
 *
 * @returns corresponding solaris error code.
 */
static int NemuNetAdpSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ":NemuNetAdpSolarisAttach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    int rc = -1;
    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            gld_mac_info_t *pMacInfo = gld_mac_alloc(pDip);
            if (pMacInfo)
            {
                nemunetadp_state_t *pState = RTMemAllocZ(sizeof(nemunetadp_state_t));
                if (pState)
                {
                    pState->pDip = pDip;

                    /*
                     * Setup GLD MAC layer registration info.
                     */
                    pMacInfo->gldm_reset = nemuNetAdpSolarisStub;
                    pMacInfo->gldm_start = nemuNetAdpSolarisStub;
                    pMacInfo->gldm_stop = nemuNetAdpSolarisStub;
                    pMacInfo->gldm_set_mac_addr = nemuNetAdpSolarisSetMacAddress;
                    pMacInfo->gldm_set_multicast = nemuNetAdpSolarisSetMulticast;
                    pMacInfo->gldm_set_promiscuous = nemuNetAdpSolarisSetPromisc;
                    pMacInfo->gldm_send = nemuNetAdpSolarisSend;
                    pMacInfo->gldm_intr = NULL;
                    pMacInfo->gldm_get_stats = nemuNetAdpSolarisGetStats;
                    pMacInfo->gldm_ioctl = NULL;
                    pMacInfo->gldm_ident = DEVICE_NAME;
                    pMacInfo->gldm_type = DL_ETHER;
                    pMacInfo->gldm_minpkt = 0;
                    pMacInfo->gldm_maxpkt = NEMUNETADP_MTU;
                    pMacInfo->gldm_capabilities = GLD_CAP_LINKSTATE;
                    AssertCompile(sizeof(RTMAC) == ETHERADDRL);

                    pMacInfo->gldm_addrlen = ETHERADDRL;
                    pMacInfo->gldm_saplen = -2;
                    pMacInfo->gldm_broadcast_addr = achBroadcastAddr;
                    pMacInfo->gldm_ppa = ddi_get_instance(pState->pDip);
                    pMacInfo->gldm_devinfo = pState->pDip;
                    pMacInfo->gldm_private = (caddr_t)pState;

                    /*
                     * We use a semi-random MAC addresses similar to a guest NIC's MAC address
                     * as the default factory address of the interface.
                     */
                    rc = nemuNetAdpSolarisGenerateMac(&pState->FactoryMac);
                    if (RT_SUCCESS(rc))
                    {
                        bcopy(&pState->FactoryMac, &pState->CurrentMac, sizeof(RTMAC));
                        pMacInfo->gldm_vendor_addr = (unsigned char *)&pState->FactoryMac;

                        /*
                         * Now try registering our GLD with the MAC layer.
                         * Registration can fail on some S10 versions when the MTU size is more than 1500.
                         * When we implement jumbo frames we should probably retry with MTU 1500 for S10.
                         */
                        rc = gld_register(pDip, (char *)ddi_driver_name(pDip), pMacInfo);
                        if (rc == DDI_SUCCESS)
                        {
                            ddi_report_dev(pDip);
                            gld_linkstate(pMacInfo, GLD_LINKSTATE_UP);
                            return DDI_SUCCESS;
                        }
                        else
                            LogRel((DEVICE_NAME ":NemuNetAdpSolarisAttach failed to register GLD. rc=%d\n", rc));
                    }
                    else
                        LogRel((DEVICE_NAME ":NemuNetAdpSolarisAttach failed to generate mac address.rc=%d\n"));

                    RTMemFree(pState);
                }
                else
                    LogRel((DEVICE_NAME ":NemuNetAdpSolarisAttach failed to alloc state.\n"));

                gld_mac_free(pMacInfo);
            }
            else
                LogRel((DEVICE_NAME ":NemuNetAdpSolarisAttach failed to alloc mac structure.\n"));
            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
            /* Nothing to do here... */
            return DDI_SUCCESS;
        }

        /* case DDI_PM_RESUME: */
        default:
            return DDI_FAILURE;
    }
}


/**
 * Detach entry point, to detach a device to the system or suspend it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Operation type (detach/suspend).
 *
 * @returns corresponding solaris error code.
 */
static int NemuNetAdpSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    LogFunc((DEVICE_NAME ":NemuNetAdpSolarisDetach pDip=%p enmCmd=%d\n", pDip, enmCmd));

    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            /*
             * Unregister and clean up.
             */
            gld_mac_info_t *pMacInfo = ddi_get_driver_private(pDip);
            if (pMacInfo)
            {
                nemunetadp_state_t *pState = (nemunetadp_state_t *)pMacInfo->gldm_private;
                if (pState)
                {
                    gld_linkstate(pMacInfo, GLD_LINKSTATE_DOWN);
                    int rc = gld_unregister(pMacInfo);
                    if (rc == DDI_SUCCESS)
                    {
                        gld_mac_free(pMacInfo);
                        RTMemFree(pState);
                        return DDI_SUCCESS;
                    }
                    else
                        LogRel((DEVICE_NAME ":NemuNetAdpSolarisDetach failed to unregister GLD from MAC layer.rc=%d\n", rc));
                }
                else
                    LogRel((DEVICE_NAME ":NemuNetAdpSolarisDetach failed to get internal state.\n"));
            }
            else
                LogRel((DEVICE_NAME ":NemuNetAdpSolarisDetach failed to get driver private GLD data.\n"));

            return DDI_FAILURE;
        }

        case DDI_RESUME:
        {
            /* Nothing to do here... */
            return DDI_SUCCESS;
        }

        /* case DDI_SUSPEND: */
        /* case DDI_HOTPLUG_DETACH: */
        default:
            return DDI_FAILURE;
    }
}


/**
 * Quiesce not-needed entry point, as Solaris 10 doesn't have any
 * ddi_quiesce_not_needed() function.
 *
 * @param   pDip            The module structure instance.
 *
 * @return  corresponding solaris error code.
 */
static int NemuNetAdpSolarisQuiesceNotNeeded(dev_info_t *pDip)
{
    return DDI_SUCCESS;
}


static int nemuNetAdpSolarisGenerateMac(PRTMAC pMac)
{
    pMac->au8[0] = 0x08;
    pMac->au8[1] = 0x00;
    pMac->au8[2] = 0x27;
    RTRandBytes(&pMac->au8[3], 3);
    Log((DEVICE_NAME ":NemuNetAdpSolarisGenerateMac Generated %.*Rhxs\n", sizeof(RTMAC), &pMac));
    return VINF_SUCCESS;
}


static int nemuNetAdpSolarisSetMacAddress(gld_mac_info_t *pMacInfo, unsigned char *pszMacAddr)
{
    nemunetadp_state_t *pState = (nemunetadp_state_t *)pMacInfo->gldm_private;
    if (pState)
    {
        bcopy(pszMacAddr, &pState->CurrentMac, sizeof(RTMAC));
        Log((DEVICE_NAME ":nemuNetAdpSolarisSetMacAddress updated MAC %.*Rhxs\n", sizeof(RTMAC), &pState->CurrentMac));
        return GLD_SUCCESS;
    }
    else
        LogRel((DEVICE_NAME ":nemuNetAdpSolarisSetMacAddress failed to get internal state.\n"));
    return GLD_FAILURE;
}


static int nemuNetAdpSolarisSend(gld_mac_info_t *pMacInfo, mblk_t *pMsg)
{
    while (pMsg)
    {
        mblk_t *pMsgNext = pMsg->b_cont;
        pMsg->b_cont = NULL;
        freemsg(pMsg);
        pMsg = pMsgNext;
    }
    return GLD_SUCCESS;
}


static int nemuNetAdpSolarisStub(gld_mac_info_t *pMacInfo)
{
    return GLD_SUCCESS;
}


static int nemuNetAdpSolarisSetMulticast(gld_mac_info_t *pMacInfo, unsigned char *pMulticastAddr, int fMulticast)
{
    return GLD_SUCCESS;
}


static int nemuNetAdpSolarisSetPromisc(gld_mac_info_t *pMacInfo, int fPromisc)
{
    /* Host requesting promiscuous intnet connection... */
    return GLD_SUCCESS;
}


static int nemuNetAdpSolarisGetStats(gld_mac_info_t *pMacInfo, struct gld_stats *pStats)
{
    /*
     * For now fake up stats. Stats like duplex and speed are better set as they
     * are used in utilities like dladm. Link state capabilities are critical
     * as they are used by ipadm while trying to restore persistent interface configs.
     */
    nemunetadp_state_t *pState = (nemunetadp_state_t *)pMacInfo->gldm_private;
    if (pState)
    {
        pStats->glds_speed                = 1000000000ULL;     /* Bits/sec. */
        pStats->glds_media                = GLDM_UNKNOWN;      /* Media/Connector Type */
        pStats->glds_intr                 = 0;                 /* Interrupt count */
        pStats->glds_norcvbuf             = 0;                 /* Recv. discards */
        pStats->glds_errxmt               = 0;                 /* Xmit errors */
        pStats->glds_errrcv               = 0;                 /* Recv. errors */
        pStats->glds_missed               = 0;                 /* Pkt Drops on Recv. */
        pStats->glds_underflow            = 0;                 /* Buffer underflows */
        pStats->glds_overflow             = 0;                 /* Buffer overflows */

        /* Ether */
        pStats->glds_frame                = 0;                 /* Align errors */
        pStats->glds_crc                  = 0;                 /* CRC errors */
        pStats->glds_duplex               = GLD_DUPLEX_FULL;   /* Link duplex state */
        pStats->glds_nocarrier            = 0;                 /* Carrier sense errors */
        pStats->glds_collisions           = 0;                 /* Xmit Collisions */
        pStats->glds_excoll               = 0;                 /* Frame discard due to excess collisions */
        pStats->glds_xmtlatecoll          = 0;                 /* Late collisions */
        pStats->glds_defer                = 0;                 /* Deferred Xmits */
        pStats->glds_dot3_first_coll      = 0;                 /* Single collision frames */
        pStats->glds_dot3_multi_coll      = 0;                 /* Multiple collision frames */
        pStats->glds_dot3_sqe_error       = 0;                 /* SQE errors */
        pStats->glds_dot3_mac_xmt_error   = 0;                 /* MAC Xmit errors */
        pStats->glds_dot3_mac_rcv_error   = 0;                 /* Mac Recv. errors */
        pStats->glds_dot3_frame_too_long  = 0;                 /* Frame too long errors */
        pStats->glds_short                = 0;                 /* Runt frames */

        pStats->glds_noxmtbuf             = 0;                 /* Xmit Buf errors */
        pStats->glds_xmtretry             = 0;                 /* Xmit retries */
        pStats->glds_multixmt             = 0;                 /* Multicast Xmits */
        pStats->glds_multircv             = 0;                 /* Multicast Recvs. */
        pStats->glds_brdcstxmt            = 0;                 /* Broadcast Xmits*/
        pStats->glds_brdcstrcv            = 0;                 /* Broadcast Recvs. */

        return GLD_SUCCESS;
    }
    else
        LogRel((DEVICE_NAME ":nemuNetAdpSolarisGetStats failed to get internal state.\n"));
    return GLD_FAILURE;
}

