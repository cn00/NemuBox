/* $Id: NemuNetAdp-linux.c $ */
/** @file
 * NemuNetAdp - Virtual Network Adapter Driver (Host), Linux Specific Code.
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
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-linux-kernel.h"
#include "version-generated.h"
#include "product-generated.h"
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/miscdevice.h>

#define LOG_GROUP LOG_GROUP_NET_ADP_DRV
#include <Nemu/log.h>
#include <Nemu/err.h>
#include <iprt/process.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/string.h>

/*
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/alloca.h>
*/

#define NEMUNETADP_OS_SPECFIC 1
#include "../NemuNetAdpInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define NEMUNETADP_LINUX_NAME      "nemunet%d"
#define NEMUNETADP_CTL_DEV_NAME    "nemunetctl"

#define NEMUNETADP_FROM_IFACE(iface) ((PNEMUNETADP) ifnet_softc(iface))


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  NemuNetAdpLinuxInit(void);
static void NemuNetAdpLinuxUnload(void);

static int NemuNetAdpLinuxOpen(struct inode *pInode, struct file *pFilp);
static int NemuNetAdpLinuxClose(struct inode *pInode, struct file *pFilp);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
static int NemuNetAdpLinuxIOCtl(struct inode *pInode, struct file *pFilp,
                                unsigned int uCmd, unsigned long ulArg);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36) */
static long NemuNetAdpLinuxIOCtlUnlocked(struct file *pFilp,
                                         unsigned int uCmd, unsigned long ulArg);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36) */

static void nemuNetAdpEthGetDrvinfo(struct net_device *dev, struct ethtool_drvinfo *info);
static int nemuNetAdpEthGetSettings(struct net_device *dev, struct ethtool_cmd *cmd);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
module_init(NemuNetAdpLinuxInit);
module_exit(NemuNetAdpLinuxUnload);

MODULE_AUTHOR(NEMU_VENDOR);
MODULE_DESCRIPTION(NEMU_PRODUCT " Network Adapter Driver");
MODULE_LICENSE("GPL");
#ifdef MODULE_VERSION
MODULE_VERSION(NEMU_VERSION_STRING " (" RT_XSTR(INTNETTRUNKIFPORT_VERSION) ")");
#endif

/**
 * The (common) global data.
 */
static struct file_operations gFileOpsNemuNetAdp =
{
    owner:      THIS_MODULE,
    open:       NemuNetAdpLinuxOpen,
    release:    NemuNetAdpLinuxClose,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
    ioctl:      NemuNetAdpLinuxIOCtl,
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36) */
    unlocked_ioctl: NemuNetAdpLinuxIOCtlUnlocked,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36) */
};

/** The miscdevice structure. */
static struct miscdevice g_CtlDev =
{
    minor:      MISC_DYNAMIC_MINOR,
    name:       NEMUNETADP_CTL_DEV_NAME,
    fops:       &gFileOpsNemuNetAdp,
# if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 17)
    devfs_name: NEMUNETADP_CTL_DEV_NAME
# endif
};

static const struct ethtool_ops gEthToolOpsNemuNetAdp =
{
    .get_drvinfo        = nemuNetAdpEthGetDrvinfo,
    .get_settings       = nemuNetAdpEthGetSettings,
    .get_link           = ethtool_op_get_link,
};


struct NemuNetAdpPriv
{
    struct net_device_stats Stats;
};

typedef struct NemuNetAdpPriv NEMUNETADPPRIV;
typedef NEMUNETADPPRIV *PNEMUNETADPPRIV;

static int nemuNetAdpLinuxOpen(struct net_device *pNetDev)
{
    netif_start_queue(pNetDev);
    return 0;
}

static int nemuNetAdpLinuxStop(struct net_device *pNetDev)
{
    netif_stop_queue(pNetDev);
    return 0;
}

static int nemuNetAdpLinuxXmit(struct sk_buff *pSkb, struct net_device *pNetDev)
{
    PNEMUNETADPPRIV pPriv = netdev_priv(pNetDev);

    /* Update the stats. */
    pPriv->Stats.tx_packets++;
    pPriv->Stats.tx_bytes += pSkb->len;
    /* Update transmission time stamp. */
    pNetDev->trans_start = jiffies;
    /* Nothing else to do, just free the sk_buff. */
    dev_kfree_skb(pSkb);
    return 0;
}

struct net_device_stats *nemuNetAdpLinuxGetStats(struct net_device *pNetDev)
{
    PNEMUNETADPPRIV pPriv = netdev_priv(pNetDev);
    return &pPriv->Stats;
}


/* ethtool_ops::get_drvinfo */
static void nemuNetAdpEthGetDrvinfo(struct net_device *pNetDev, struct ethtool_drvinfo *info)
{
    PNEMUNETADPPRIV pPriv = netdev_priv(pNetDev);
    NOREF(pPriv);

    RTStrPrintf(info->driver, sizeof(info->driver),
                "%s", NEMUNETADP_NAME);

    /*
     * Would be nice to include NEMU_SVN_REV, but it's not available
     * here.  Use file's svn revision via svn keyword?
     */
    RTStrPrintf(info->version, sizeof(info->version),
                "%s", NEMU_VERSION_STRING);

    RTStrPrintf(info->fw_version, sizeof(info->fw_version),
                "0x%08X", INTNETTRUNKIFPORT_VERSION);

    RTStrPrintf(info->bus_info, sizeof(info->driver),
                "N/A");
}


/* ethtool_ops::get_settings */
static int nemuNetAdpEthGetSettings(struct net_device *pNetDev, struct ethtool_cmd *cmd)
{
    cmd->supported      = 0;
    cmd->advertising    = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
    ethtool_cmd_speed_set(cmd, SPEED_10);
#else
    cmd->speed          = SPEED_10;
#endif
    cmd->duplex         = DUPLEX_FULL;
    cmd->port           = PORT_TP;
    cmd->phy_address    = 0;
    cmd->transceiver    = XCVR_INTERNAL;
    cmd->autoneg        = AUTONEG_DISABLE;
    cmd->maxtxpkt       = 0;
    cmd->maxrxpkt       = 0;
    return 0;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29)
static const struct net_device_ops nemuNetAdpNetdevOps = {
    .ndo_open               = nemuNetAdpLinuxOpen,
    .ndo_stop               = nemuNetAdpLinuxStop,
    .ndo_start_xmit         = nemuNetAdpLinuxXmit,
    .ndo_get_stats          = nemuNetAdpLinuxGetStats
};
#endif

static void nemuNetAdpNetDevInit(struct net_device *pNetDev)
{
    PNEMUNETADPPRIV pPriv;

    ether_setup(pNetDev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29)
    pNetDev->netdev_ops = &nemuNetAdpNetdevOps;
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29) */
    pNetDev->open = nemuNetAdpLinuxOpen;
    pNetDev->stop = nemuNetAdpLinuxStop;
    pNetDev->hard_start_xmit = nemuNetAdpLinuxXmit;
    pNetDev->get_stats = nemuNetAdpLinuxGetStats;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29) */

    pNetDev->ethtool_ops = &gEthToolOpsNemuNetAdp;

    pPriv = netdev_priv(pNetDev);
    memset(pPriv, 0, sizeof(*pPriv));
}


int nemuNetAdpOsCreate(PNEMUNETADP pThis, PCRTMAC pMACAddress)
{
    int rc = VINF_SUCCESS;
    struct net_device *pNetDev;

    /* No need for private data. */
    pNetDev = alloc_netdev(sizeof(NEMUNETADPPRIV),
                           pThis->szName[0] ? pThis->szName : NEMUNETADP_LINUX_NAME,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
                           NET_NAME_UNKNOWN,
#endif
                           nemuNetAdpNetDevInit);
    if (pNetDev)
    {
        int err;

        if (pNetDev->dev_addr)
        {
            memcpy(pNetDev->dev_addr, pMACAddress, ETH_ALEN);
            Log2(("nemuNetAdpOsCreate: pNetDev->dev_addr = %.6Rhxd\n", pNetDev->dev_addr));

            /*
             * We treat presence of NemuNetFlt filter as our "carrier",
             * see nemuNetFltSetLinkState().
             *
             * operstates.txt: "On device allocation, networking core
             * sets the flags equivalent to netif_carrier_ok() and
             * !netif_dormant()" - so turn carrier off here.
             */
            netif_carrier_off(pNetDev);

            err = register_netdev(pNetDev);
            if (!err)
            {
                strncpy(pThis->szName, pNetDev->name, sizeof(pThis->szName));
                pThis->szName[sizeof(pThis->szName) - 1] = '\0';
                pThis->u.s.pNetDev = pNetDev;
                Log2(("nemuNetAdpOsCreate: pThis=%p pThis->szName = %p\n", pThis, pThis->szName));
                return VINF_SUCCESS;
            }
        }
        else
        {
            LogRel(("NemuNetAdp: failed to set MAC address (dev->dev_addr == NULL)\n"));
            err = EFAULT;
        }
        free_netdev(pNetDev);
        rc = RTErrConvertFromErrno(err);
    }
    return rc;
}

void nemuNetAdpOsDestroy(PNEMUNETADP pThis)
{
    struct net_device *pNetDev = pThis->u.s.pNetDev;
    AssertPtr(pThis->u.s.pNetDev);

    pThis->u.s.pNetDev = NULL;
    unregister_netdev(pNetDev);
    free_netdev(pNetDev);
}

/**
 * Device open. Called on open /dev/nemunetctl
 *
 * @param   pInode      Pointer to inode info structure.
 * @param   pFilp       Associated file pointer.
 */
static int NemuNetAdpLinuxOpen(struct inode *pInode, struct file *pFilp)
{
    Log(("NemuNetAdpLinuxOpen: pid=%d/%d %s\n", RTProcSelf(), current->pid, current->comm));

#ifdef NEMU_WITH_HARDENING
    /*
     * Only root is allowed to access the device, enforce it!
     */
    if (!capable(CAP_SYS_ADMIN))
    {
        Log(("NemuNetAdpLinuxOpen: admin privileges required!\n"));
        return -EPERM;
    }
#endif

    return 0;
}


/**
 * Close device.
 *
 * @param   pInode      Pointer to inode info structure.
 * @param   pFilp       Associated file pointer.
 */
static int NemuNetAdpLinuxClose(struct inode *pInode, struct file *pFilp)
{
    Log(("NemuNetAdpLinuxClose: pid=%d/%d %s\n",
         RTProcSelf(), current->pid, current->comm));
    pFilp->private_data = NULL;
    return 0;
}

/**
 * Device I/O Control entry point.
 *
 * @param   pFilp       Associated file pointer.
 * @param   uCmd        The function specified to ioctl().
 * @param   ulArg       The argument specified to ioctl().
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
static int NemuNetAdpLinuxIOCtl(struct inode *pInode, struct file *pFilp,
                                unsigned int uCmd, unsigned long ulArg)
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36) */
static long NemuNetAdpLinuxIOCtlUnlocked(struct file *pFilp,
                                         unsigned int uCmd, unsigned long ulArg)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36) */
{
    NEMUNETADPREQ Req;
    PNEMUNETADP pAdp;
    int rc;
    char *pszName = NULL;

    Log(("NemuNetAdpLinuxIOCtl: param len %#x; uCmd=%#x; add=%#x\n", _IOC_SIZE(uCmd), uCmd, NEMUNETADP_CTL_ADD));
    if (RT_UNLIKELY(_IOC_SIZE(uCmd) != sizeof(Req))) /* paranoia */
    {
        Log(("NemuNetAdpLinuxIOCtl: bad ioctl sizeof(Req)=%#x _IOC_SIZE=%#x; uCmd=%#x.\n", sizeof(Req), _IOC_SIZE(uCmd), uCmd));
        return -EINVAL;
    }

    switch (uCmd)
    {
        case NEMUNETADP_CTL_ADD:
            Log(("NemuNetAdpLinuxIOCtl: _IOC_DIR(uCmd)=%#x; IOC_OUT=%#x\n", _IOC_DIR(uCmd), IOC_OUT));
            if (RT_UNLIKELY(copy_from_user(&Req, (void *)ulArg, sizeof(Req))))
            {
                Log(("NemuNetAdpLinuxIOCtl: copy_from_user(,%#lx,) failed; uCmd=%#x.\n", ulArg, uCmd));
                return -EFAULT;
            }
            Log(("NemuNetAdpLinuxIOCtl: Add %s\n", Req.szName));

            if (Req.szName[0])
            {
                pAdp = nemuNetAdpFindByName(Req.szName);
                if (pAdp)
                {
                    Log(("NemuNetAdpLinuxIOCtl: '%s' already exists\n", Req.szName));
                    return -EINVAL;
                }
                pszName = Req.szName;
            }
            rc = nemuNetAdpCreate(&pAdp, pszName);
            if (RT_FAILURE(rc))
            {
                Log(("NemuNetAdpLinuxIOCtl: nemuNetAdpCreate -> %Rrc\n", rc));
                return -(rc == VERR_OUT_OF_RESOURCES ? ENOMEM : EINVAL);
            }

            Assert(strlen(pAdp->szName) < sizeof(Req.szName));
            strncpy(Req.szName, pAdp->szName, sizeof(Req.szName) - 1);
            Req.szName[sizeof(Req.szName) - 1] = '\0';

            if (RT_UNLIKELY(copy_to_user((void *)ulArg, &Req, sizeof(Req))))
            {
                /* this is really bad! */
                /** @todo remove the adapter again? */
                printk(KERN_ERR "NemuNetAdpLinuxIOCtl: copy_to_user(%#lx,,%#zx); uCmd=%#x!\n", ulArg, sizeof(Req), uCmd);
                return -EFAULT;
            }
            Log(("NemuNetAdpLinuxIOCtl: Successfully added '%s'\n", Req.szName));
            break;

        case NEMUNETADP_CTL_REMOVE:
            if (RT_UNLIKELY(copy_from_user(&Req, (void *)ulArg, sizeof(Req))))
            {
                Log(("NemuNetAdpLinuxIOCtl: copy_from_user(,%#lx,) failed; uCmd=%#x.\n", ulArg, uCmd));
                return -EFAULT;
            }
            Log(("NemuNetAdpLinuxIOCtl: Remove %s\n", Req.szName));

            pAdp = nemuNetAdpFindByName(Req.szName);
            if (!pAdp)
            {
                Log(("NemuNetAdpLinuxIOCtl: '%s' not found\n", Req.szName));
                return -EINVAL;
            }

            rc = nemuNetAdpDestroy(pAdp);
            if (RT_FAILURE(rc))
            {
                Log(("NemuNetAdpLinuxIOCtl: nemuNetAdpDestroy('%s') -> %Rrc\n", Req.szName, rc));
                return -EINVAL;
            }
            Log(("NemuNetAdpLinuxIOCtl: Successfully removed '%s'\n", Req.szName));
            break;

        default:
            printk(KERN_ERR "NemuNetAdpLinuxIOCtl: unknown command %x.\n", uCmd);
            return -EINVAL;
    }

    return 0;
}

int  nemuNetAdpOsInit(PNEMUNETADP pThis)
{
    /*
     * Init linux-specific members.
     */
    pThis->u.s.pNetDev = NULL;

    return VINF_SUCCESS;
}



/**
 * Initialize module.
 *
 * @returns appropriate status code.
 */
static int __init NemuNetAdpLinuxInit(void)
{
    int rc;
    /*
     * Initialize IPRT.
     */
    rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        Log(("NemuNetAdpLinuxInit\n"));

        rc = nemuNetAdpInit();
        if (RT_SUCCESS(rc))
        {
            rc = misc_register(&g_CtlDev);
            if (rc)
            {
                printk(KERN_ERR "NemuNetAdp: Can't register " NEMUNETADP_CTL_DEV_NAME " device! rc=%d\n", rc);
                return rc;
            }
            LogRel(("NemuNetAdp: Successfully started.\n"));
            return 0;
        }
        else
            LogRel(("NemuNetAdp: failed to register nemunet0 device (rc=%d)\n", rc));
    }
    else
        LogRel(("NemuNetAdp: failed to initialize IPRT (rc=%d)\n", rc));

    return -RTErrConvertToErrno(rc);
}


/**
 * Unload the module.
 *
 * @todo We have to prevent this if we're busy!
 */
static void __exit NemuNetAdpLinuxUnload(void)
{
    Log(("NemuNetAdpLinuxUnload\n"));

    /*
     * Undo the work done during start (in reverse order).
     */

    nemuNetAdpShutdown();
    /* Remove control device */
    misc_deregister(&g_CtlDev);

    RTR0Term();

    Log(("NemuNetAdpLinuxUnload - done\n"));
}

