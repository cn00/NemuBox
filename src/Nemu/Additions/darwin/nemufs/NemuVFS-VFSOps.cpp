/* $Id: NemuVFS-VFSOps.cpp $ */
/** @file
 * NemuVFS - Filesystem operations.
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

#include <mach/kmod.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>

#include <sys/mount.h>
#include <sys/vnode.h>

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/asm.h>

#include "nemuvfs.h"

/* States of NemuVFS object used in atomic variables
 * in order to reach sync between several concurrently running threads. */
#define NEMUVFS_OBJECT_UNINITIALIZED    (0)
#define NEMUVFS_OBJECT_INITIALIZING     (1)
#define NEMUVFS_OBJECT_INITIALIZED      (2)
#define NEMUVFS_OBJECT_INVALID          (3)


/**
 * Mount helper: Get mounting parameters from user space and validate them.
 *
 * @param pUserData     Mounting parameters provided by user space mount tool.
 * @param pKernelData   Buffer to store pUserData in kernel space.
 *
 * @return 0 on success or BSD error code otherwise.
 */
static int
nemuvfs_get_mount_info(user_addr_t pUserData, struct nemuvfs_mount_info *pKernelData)
{
    AssertReturn(pKernelData, EINVAL);
    AssertReturn(pUserData,   EINVAL);

    /* Get mount parameters from user space */
    if (copyin(pUserData, pKernelData, sizeof(struct nemuvfs_mount_info)) != 0)
    {
        PERROR("Unable to copy mount parameters from user space");
        return EINVAL;
    }

    /* Validate data magic */
    if (pKernelData->magic != NEMUVFS_MOUNTINFO_MAGIC)
    {
        PERROR("Mount parameter magic mismatch");
        return EINVAL;
    }

    return 0;
}


/**
 * Mount helper: Provide VFS layer with a Nemu share name (stored as mounted device).
 *
 * @param mp            Mount data provided by VFS layer.
 * @param szShareName   Nemu share name.
 * @param cbShareName   Returning parameter which contains Nemu share name string length.
 *
 * @return 0 on success or BSD error code otherwise.
 */
static int
nemuvfs_set_share_name(struct mount *mp, char *szShareName, size_t *cbShareName)
{
    struct vfsstatfs *pVfsInfo;

    AssertReturn(mp,          EINVAL);
    AssertReturn(szShareName, EINVAL);
    AssertReturn(cbShareName, EINVAL);

    pVfsInfo = vfs_statfs(mp);
    if (!pVfsInfo)
    {
        PERROR("Unable to get VFS data for the mount structure");
        return EINVAL;
    }

    return copystr(szShareName, pVfsInfo->f_mntfromname, MAXPATHLEN, cbShareName);
}


/**
 * Mount helper: allocate locking group attribute and locking group itself.
 * Store allocated data into NemuVFS private data.
 *
 * @param pMount   NemuVFS global data which will be updated with
 *                          locking group and its attribute in case of success;
 *                          otherwise pMount unchanged.
 *
 * @return 0 on success or BSD error code otherwise.
 *
 */
static int
nemuvfs_prepare_locking(nemuvfs_mount_t *pMount)
{
    lck_grp_attr_t *pGrpAttr;
    lck_grp_t      *pGrp;

    AssertReturn(pMount, EINVAL);

    pGrpAttr = lck_grp_attr_alloc_init();
    if (pGrpAttr)
    {
        pGrp = lck_grp_alloc_init("NemuVFS", pGrpAttr);
        if (pGrp)
        {
            pMount->pLockGroupAttr = pGrpAttr;
            pMount->pLockGroup      = pGrp;

            return 0;
        }
        else
            PERROR("Unable to allocate locking group");

        lck_grp_attr_free(pGrpAttr);
    }
    else
        PERROR("Unable to allocate locking group attribute");

    return ENOMEM;
}


/**
 * Mount and unmount helper: destroy locking group attribute and locking group itself.
 *
 * @param pMount   NemuVFS global data for which locking
 *                          group and attribute will be deallocated and set to NULL.
 */
static void
nemuvfs_destroy_locking(nemuvfs_mount_t *pMount)
{
    AssertReturnVoid(pMount);

    if (pMount->pLockGroup)
    {
        lck_grp_free(pMount->pLockGroup);
        pMount->pLockGroup = NULL;
    }

    if (pMount->pLockGroupAttr)
    {
        lck_grp_attr_free(pMount->pLockGroupAttr);
        pMount->pLockGroupAttr = NULL;
    }
}

/**
 * Mount helper: Allocate and init NemuVFS global data.
 *
 * @param mp            Mount data provided by VFS layer.
 * @param pUserData     Mounting parameters provided by user space mount tool.
 *
 * @return NemuVFS global data or NULL.
 */
static nemuvfs_mount_t *
nemuvfs_alloc_internal_data(struct mount *mp, user_addr_t pUserData)
{
    nemuvfs_mount_t             *pMount;
    struct nemuvfs_mount_info    mountInfo;
    struct vfsstatfs            *pVfsInfo;
    size_t                       cbShareName;

    int rc;

    AssertReturn(mp,        NULL);
    AssertReturn(pUserData, NULL);

    pVfsInfo = vfs_statfs(mp);
    AssertReturn(pVfsInfo, NULL);

    /* Allocate memory for NemuVFS internal data */
    pMount = (nemuvfs_mount_t *)RTMemAllocZ(sizeof(nemuvfs_mount_t));
    if (pMount)
    {
        rc = nemuvfs_get_mount_info(pUserData, &mountInfo);
        if (rc == 0)
        {
            PDEBUG("Mounting shared folder '%s'", mountInfo.name);

            /* Prepare for locking. We prepare locking group and attr data here,
             * but allocate and initialize real lock in nemuvfs_create_vnode_internal().
             * We use the same pLockGroup and pLockAttr for all vnodes related to this mount point. */
            rc = nemuvfs_prepare_locking(pMount);
            if (rc == 0)
            {
                rc = nemuvfs_set_share_name(mp, (char *)&mountInfo.name, &cbShareName);
                if (rc == 0)
                {
                    pMount->pShareName = nemuvfs_construct_shflstring((char *)&mountInfo.name, cbShareName);
                    if (pMount->pShareName)
                    {
                        /* Remember user who mounted this share */
                        pMount->owner = pVfsInfo->f_owner;

                        /* Mark root vnode as uninitialized */
                        ASMAtomicWriteU8(&pMount->fRootVnodeState, NEMUVFS_OBJECT_UNINITIALIZED);

                        return pMount;
                    }
                }
            }

            nemuvfs_destroy_locking(pMount);
        }

        RTMemFree(pMount);
    }

    return NULL;
}


/**
 * Mount and unmount helper: Release NemuVFS internal resources.
 * Deallocates ppMount as well.
 *
 * @param ppMount      Pointer to reference of NemuVFS internal data.
 */
static void
nemuvfs_destroy_internal_data(nemuvfs_mount_t **ppMount)
{
    AssertReturnVoid(ppMount);
    AssertReturnVoid(*ppMount);
    AssertReturnVoid((*ppMount)->pShareName);

    RTMemFree((*ppMount)->pShareName);
    (*ppMount)->pShareName = NULL;

    nemuvfs_destroy_locking(*ppMount);
    RTMemFree(*ppMount);
    *ppMount = NULL;
}


/**
 * Mount NemuVFS.
 *
 * @param mp        Mount data provided by VFS layer.
 * @param pDev      Device structure provided by VFS layer.
 * @param pUserData Mounting parameters provided by user space mount tool.
 * @param pContext  kAuth context needed in order to authentificate mount operation.
 *
 * @return 0 on success or BSD error code otherwise.
 */
static int
nemuvfs_mount(struct mount *mp, vnode_t pDev, user_addr_t pUserData, vfs_context_t pContext)
{
    NOREF(pDev);
    NOREF(pContext);

    nemuvfs_mount_t *pMount;

    int rc = ENOMEM;

    PDEBUG("Mounting...");

    pMount = nemuvfs_alloc_internal_data(mp, pUserData);
    if (pMount)
    {
        rc = VbglR0SfMapFolder(&g_nemuSFClient, pMount->pShareName, &pMount->pMap);
        if (RT_SUCCESS(rc))
        {
            /* Private data should be set before nemuvfs_create_vnode_internal() call
             * because we need it in order to create vnode. */
            vfs_setfsprivate(mp, pMount);

            /* Reset root vnode */
            pMount->pRootVnode = NULL;

            vfs_setflags(mp, MNT_RDONLY | MNT_SYNCHRONOUS | MNT_NOEXEC | MNT_NOSUID | MNT_NODEV);

            PDEBUG("VirtualBox shared folder successfully mounted");

            return 0;
        }

        PDEBUG("Unable to map shared folder");
        nemuvfs_destroy_internal_data(&pMount);
    }
    else
        PDEBUG("Unable to allocate internal data");

    return rc;
}


/**
 * Unmount NemuVFS.
 *
 * @param mp        Mount data provided by VFS layer.
 * @param fFlags    Unmounting flags.
 * @param pContext  kAuth context needed in order to authentificate mount operation.
 *
 * @return 0 on success or BSD error code otherwise.
 */
static int
nemuvfs_unmount(struct mount *mp, int fFlags, vfs_context_t pContext)
{
    NOREF(pContext);

    nemuvfs_mount_t *pMount;
    int                  rc = EBUSY;
    int                  fFlush = (fFlags & MNT_FORCE) ? FORCECLOSE : 0;

    PDEBUG("Attempting to %s unmount a shared folder", (fFlags & MNT_FORCE) ? "forcibly" : "normally");

    AssertReturn(mp, EINVAL);

    pMount = (nemuvfs_mount_t *)vfs_fsprivate(mp);

    AssertReturn(pMount,             EINVAL);
    AssertReturn(pMount->pRootVnode, EINVAL);

    /* Check if we can do unmount at the moment */
    if (!vnode_isinuse(pMount->pRootVnode, 1))
    {
        /* Flush child vnodes first */
        rc = vflush(mp, pMount->pRootVnode, fFlush);
        if (rc == 0)
        {
            /* Flush root vnode */
            rc = vflush(mp, NULL, fFlush);
            if (rc == 0)
            {
                vfs_setfsprivate(mp, NULL);

                rc = VbglR0SfUnmapFolder(&g_nemuSFClient, &pMount->pMap);
                if (RT_SUCCESS(rc))
                {
                    nemuvfs_destroy_internal_data(&pMount);
                    PDEBUG("A shared folder has been successfully unmounted");
                    return 0;
                }

                PDEBUG("Unable to unmount shared folder");
                rc = EPROTO;
            }
            else
                PDEBUG("Unable to flush filesystem before unmount, some data might be lost");
        }
        else
            PDEBUG("Unable to flush child vnodes");

    }
    else
        PDEBUG("Root vnode is in use, can't unmount");

    vnode_put(pMount->pRootVnode);

    return rc;
}


/**
 * Get NemuVFS root vnode.
 *
 * Handle three cases here:
 *  - vnode does not exist yet: create a new one
 *  - currently creating vnode: wait till the end, increment usage count and return existing one
 *  - vnode already created: increment usage count and return existing one
 *  - vnode was failed to create: give a chance to try to re-create it later
 *
 * @param mp        Mount data provided by VFS layer.
 * @param ppVnode   vnode to return.
 * @param pContext  kAuth context needed in order to authentificate mount operation.
 *
 * @return 0 on success or BSD error code otherwise.
 */
static int
nemuvfs_root(struct mount *mp, struct vnode **ppVnode, vfs_context_t pContext)
{
    NOREF(pContext);

    nemuvfs_mount_t *pMount;
    int rc = 0;
    uint32_t vid;

    PDEBUG("Getting root vnode...");

    AssertReturn(mp,      EINVAL);
    AssertReturn(ppVnode, EINVAL);

    pMount = (nemuvfs_mount_t *)vfs_fsprivate(mp);
    AssertReturn(pMount, EINVAL);

    /* Check case when vnode does not exist yet */
    if (ASMAtomicCmpXchgU8(&pMount->fRootVnodeState, NEMUVFS_OBJECT_INITIALIZING, NEMUVFS_OBJECT_UNINITIALIZED))
    {
        PDEBUG("Create new root vnode");

        /* Allocate empty SHFLSTRING to indicate path to root vnode within Shared Folder */
        char        szEmpty[1];
        SHFLSTRING *pSFVnodePath;

        pSFVnodePath = nemuvfs_construct_shflstring((char *)szEmpty, 0);
        if (pSFVnodePath)
        {
            int rc2;
            rc2 = nemuvfs_create_vnode_internal(mp, VDIR, NULL, TRUE, pSFVnodePath, &pMount->pRootVnode);
            if (rc2 != 0)
            {
                RTMemFree(pSFVnodePath);
                rc = ENOTSUP;
            }
        }
        else
            rc = ENOMEM;

        /* Notify other threads about result */
        if (rc == 0)
            ASMAtomicWriteU8(&pMount->fRootVnodeState, NEMUVFS_OBJECT_INITIALIZED);
        else
            ASMAtomicWriteU8(&pMount->fRootVnodeState, NEMUVFS_OBJECT_INVALID);
    }
    else
    {
        /* Check case if we are currently creating vnode. Wait while other thread to finish allocation. */
        uint8_t fRootVnodeState = NEMUVFS_OBJECT_UNINITIALIZED;
        while (fRootVnodeState != NEMUVFS_OBJECT_INITIALIZED
            && fRootVnodeState != NEMUVFS_OBJECT_INVALID)
        {
            /* @todo: Currently, we are burning CPU cycles while waiting. This is for a short
             * time but we should relax here! */
            fRootVnodeState = ASMAtomicReadU8(&pMount->fRootVnodeState);

        }

        /* Check if the other thread initialized root vnode and it is ready to be returned */
        if (fRootVnodeState == NEMUVFS_OBJECT_INITIALIZED)
        {
            /* Take care about iocount */
            vid = vnode_vid(pMount->pRootVnode);
            rc = vnode_getwithvid(pMount->pRootVnode, vid);
        }
        else
        {
            /* Other thread reported initialization failure.
             * Set vnode state NEMUVFS_OBJECT_UNINITIALIZED in order to try recreate root
             * vnode in other attempt */
            ASMAtomicWriteU8(&pMount->fRootVnodeState, NEMUVFS_OBJECT_UNINITIALIZED);
        }

    }

    /* Only return vnode if we got success */
    if (rc == 0)
    {
        PDEBUG("Root vnode can be returned");
        *ppVnode = pMount->pRootVnode;
    }
    else
        PDEBUG("Root vnode cannot be returned: 0x%X", rc);

    return rc;
}

/**
 * NemuVFS get VFS layer object attribute callback.
 *
 * @param mp        Mount data provided by VFS layer.
 * @param pAttr     Output buffer to return attributes.
 * @param pContext  kAuth context needed in order to authentificate mount operation.
 *
 * @returns     0 for success, else an error code.
 */
static int
nemuvfs_getattr(struct mount *mp, struct vfs_attr *pAttr, vfs_context_t pContext)
{
    NOREF(pContext);

    nemuvfs_mount_t     *pMount;
    SHFLVOLINFO          SHFLVolumeInfo;

    int      rc;
    uint32_t cbBuffer = sizeof(SHFLVolumeInfo);

    uint32_t u32bsize;
    uint64_t u64blocks;
    uint64_t u64bfree;

    PDEBUG("Getting attribute...\n");

    AssertReturn(mp, EINVAL);

    pMount = (nemuvfs_mount_t *)vfs_fsprivate(mp);
    AssertReturn(pMount, EINVAL);
    AssertReturn(pMount->pShareName, EINVAL);

    rc = VbglR0SfFsInfo(&g_nemuSFClient, &pMount->pMap, 0, SHFL_INFO_GET | SHFL_INFO_VOLUME,
                        &cbBuffer, (PSHFLDIRINFO)&SHFLVolumeInfo);
    AssertReturn(rc == 0, EPROTO);

    u32bsize  = (uint32_t)SHFLVolumeInfo.ulBytesPerAllocationUnit;
    AssertReturn(u32bsize > 0, ENOTSUP);

    u64blocks = (uint64_t)SHFLVolumeInfo.ullTotalAllocationBytes / (uint64_t)u32bsize;
    u64bfree  = (uint64_t)SHFLVolumeInfo.ullAvailableAllocationBytes / (uint64_t)u32bsize;

    VFSATTR_RETURN(pAttr, f_bsize,  u32bsize);
    VFSATTR_RETURN(pAttr, f_blocks, u64blocks);
    VFSATTR_RETURN(pAttr, f_bfree,  u64bfree);
    VFSATTR_RETURN(pAttr, f_bavail, u64bfree);
    VFSATTR_RETURN(pAttr, f_bused,  u64blocks - u64bfree);

    VFSATTR_RETURN(pAttr, f_owner,  pMount->owner);

    VFSATTR_CLEAR_ACTIVE(pAttr, f_iosize);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_files);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_ffree);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_fssubtype);

    /* todo: take care about f_capabilities and f_attributes, f_fsid */
    VFSATTR_CLEAR_ACTIVE(pAttr, f_capabilities);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_attributes);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_fsid);

    /* todo: take care about f_create_time, f_modify_time, f_access_time, f_backup_time */
    VFSATTR_CLEAR_ACTIVE(pAttr, f_create_time);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_modify_time);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_access_time);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_backup_time);

    VFSATTR_CLEAR_ACTIVE(pAttr, f_signature);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_carbon_fsid);
    VFSATTR_CLEAR_ACTIVE(pAttr, f_uuid);

    if (VFSATTR_IS_ACTIVE(pAttr, f_vol_name))
    {
        strlcpy(pAttr->f_vol_name, (char*)pMount->pShareName->String.utf8, MAXPATHLEN);
        VFSATTR_SET_SUPPORTED(pAttr, f_vol_name);
    }

    VFSATTR_ALL_SUPPORTED(pAttr);

    return 0;
}

/* VFS options */
struct vfsops g_oNemuVFSOpts = {
    /* Standard operations */
    &nemuvfs_mount,
    NULL,               /* Skipped: vfs_start() */
    &nemuvfs_unmount,
    &nemuvfs_root,
    NULL,               /* Skipped: vfs_quotactl */
    &nemuvfs_getattr,
    NULL,               /* Skipped: vfs_sync */
    NULL,               /* Skipped: vfs_vget */
    NULL,               /* Skipped: vfs_fhtovp */
    NULL,               /* Skipped: vfs_vptofh */
    NULL,               /* Skipped: vfs_init */
    NULL,               /* Skipped: vfs_sysctl */
    NULL,               /* Skipped: vfs_setattr */
    /* Reserved */
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, },
};
