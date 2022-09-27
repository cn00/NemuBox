/* $Id: nemuvfs.h $ */
/** @file
 * NemuVFS - common header used across all the driver source files.
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

#define MODULE_NAME "NemuVFS"

#ifdef KERNEL
# include <libkern/libkern.h>
# include <sys/lock.h>
#endif

#define PINFO(fmt, args...) \
    printf(MODULE_NAME ": INFO: " fmt "\n", ## args)
#define PDEBUG(fmt, args...) \
    printf(MODULE_NAME ": %s(): DEBUG: " fmt "\n", __FUNCTION__, ## args)
#define PERROR(fmt, args...) \
    printf(MODULE_NAME ": ERROR: " fmt "\n", ## args)

#define NEMUVBFS_NAME               "nemuvfs"
#define NEMUVFS_MOUNTINFO_MAGIC     (0xCAFE)

#ifdef KERNEL

#include <iprt/types.h>
#undef PVM
#include <sys/vnode.h>

#include <Nemu/NemuGuestLibSharedFolders.h>


/** Global refernce to host service connection */
extern VBGLSFCLIENT g_nemuSFClient;

/** Private data assigned to each mounted shared folder. Assigned to mp structure. */
typedef struct nemuvfs_mount_data
{
    VBSFMAP             pMap;               /** Shared folder mapping */
    SHFLSTRING         *pShareName;         /** NemuVFS share name */
    uint64_t            cFileIdCounter;     /** Counter that used in order to assign unique ID to each vnode within mounted share */
    vnode_t             pRootVnode;         /** VFS object: vnode that corresponds shared folder root */
    uint8_t volatile    fRootVnodeState;    /** Sync flag that used in order to safely allocate pRootVnode */
    uid_t               owner;              /** User ID tha mounted shared folder */
    lck_grp_t          *pLockGroup;         /** BSD locking stuff */
    lck_grp_attr_t     *pLockGroupAttr;     /** BSD locking stuff */
} nemuvfs_mount_t;

/** Private data assigned to each vnode object. */
typedef struct nemuvfs_vnode_data
{
    SHFLHANDLE      pHandle;                /** NemuVFS object handle. */
    PSHFLSTRING     pPath;                  /** Path within shared folder */
    lck_attr_t     *pLockAttr;              /** BSD locking stuff */
    lck_rw_t       *pLock;                  /** BSD locking stuff */
} nemuvfs_vnode_t;

/**
 * Helper function to create XNU VFS vnode object.
 *
 * @param mp        Mount data structure
 * @param type      vnode type (directory, regular file, etc)
 * @param pParent   Parent vnode object (NULL for NemuVFS root vnode)
 * @param fIsRoot   Flag that indicates if created vnode object is
 *                  NemuVFS root vnode (TRUE for NemuVFS root vnode, FALSE
 *                  for all aother vnodes)
 * @param           Path within Shared Folder
 * @param ret       Returned newly created vnode
 *
 * @return 0 on success, error code otherwise
 */
extern int nemuvfs_create_vnode_internal(struct mount *mp, enum vtype type, vnode_t pParent, int fIsRoot, PSHFLSTRING Path, vnode_t *ret);

/**
 * Convert guest absolute VFS path (starting from VFS root) to a host path
 * within mounted shared folder (returning it as a char *).
 *
 * @param mp            Mount data structure
 * @param pszGuestPath  Guest absolute VFS path (starting from VFS root)
 * @param cbGuestPath   Size of pszGuestPath
 * @param pszHostPath   Returned char * wich contains host path
 * @param cbHostPath    Returned pszHostPath size
 *
 * @return 0 on success, error code otherwise
 */
extern int nemuvfs_guest_path_to_char_path_internal(mount_t mp, char *pszGuestPath, int cbGuestPath, char **pszHostPath, int *cbHostPath);

/**
 * Convert guest absolute VFS path (starting from VFS root) to a host path
 * within mounted shared folder.
 *
 * @param mp            Mount data structure
 * @param pszGuestPath  Guest absolute VFS path (starting from VFS root)
 * @param cbGuestPath   Size of pszGuestPath
 * @param ppResult      Returned PSHFLSTRING object wich contains host path
 *
 * @return 0 on success, error code otherwise
 */
extern int nemuvfs_guest_path_to_shflstring_path_internal(mount_t mp, char *pszGuestPath, int cbGuestPath, PSHFLSTRING *ppResult);

/**
 * Wrapper function for nemuvfs_guest_path_to_char_path_internal() which
 * converts guest path to host path using vnode object information.
 *
 * @param vnode     Guest's VFS object
 * @param ppPath    Allocated  char * which contain a path
 * @param pcbPath   Size of ppPath
 *
 * @return 0 on success, error code otherwise.
 */
extern int nemuvfs_guest_vnode_to_char_path_internal(vnode_t vnode, char **ppHostPath, int *pcbHostPath);

/**
 * Wrapper function for nemuvfs_guest_path_to_shflstring_path_internal() which
 * converts guest path to host path using vnode object information.
 *
 * @param vnode     Guest's VFS object
 * @param ppResult  Allocated  PSHFLSTRING object which contain a path
 *
 * @return 0 on success, error code otherwise.
 */
extern int nemuvfs_guest_vnode_to_shflstring_path_internal(vnode_t vnode, PSHFLSTRING *ppResult);

/**
 * Free resources allocated by nemuvfs_path_internal() and nemuvfs_guest_vnode_to_shflstring_path_internal().
 *
 * @param ppHandle  Reference to object to be freed.
 */
extern void nemuvfs_put_path_internal(void **ppHandle);

/**
 * Open existing NemuVFS object and return its handle.
 *
 * @param pMount            Mount session data.
 * @param pPath             VFS path to the object relative to mount point.
 * @param fFlags            For directory object it should be
 *                          SHFL_CF_DIRECTORY and 0 for any other object.
 * @param pHandle           Returned handle.
 *
 * @return 0 on success, error code otherwise.
 */
extern int nemuvfs_open_internal(nemuvfs_mount_t *pMount, PSHFLSTRING pPath, uint32_t fFlags, SHFLHANDLE *pHandle);

/**
 * Release NemuVFS object handle openned by nemuvfs_open_internal().
 *
 * @param pMount            Mount session data.
 * @param pHandle           Handle to close.
 *
 * @return 0 on success, IPRT error code otherwise.
 */
extern int nemuvfs_close_internal(nemuvfs_mount_t *pMount, SHFLHANDLE pHandle);

/**
 * Get information about host VFS object.
 *
 * @param mp           Mount point data
 * @param pSHFLDPath   Path to VFS object within mounted shared folder
 * @param Info         Returned info
 *
 * @return  0 on success, error code otherwise.
 */
extern int nemuvfs_get_info_internal(mount_t mp, PSHFLSTRING pSHFLDPath, PSHFLFSOBJINFO Info);

/**
 * Check if VFS object exists on a host side.
 *
 * @param vnode     Guest VFS vnode that corresponds to host VFS object
 *
 * @return 1 if exists, 0 otherwise.
 */
extern int nemuvfs_exist_internal(vnode_t vnode);

/**
 * Convert host VFS object mode flags into guest ones.
 *
 * @param fHostMode     Host flags
 *
 * @return Guest flags
 */
extern mode_t nemuvfs_h2g_mode_inernal(RTFMODE fHostMode);

/**
 * Convert guest VFS object mode flags into host ones.
 *
 * @param fGuestMode     Host flags
 *
 * @return Host flags
 */
extern uint32_t nemuvfs_g2h_mode_inernal(mode_t fGuestMode);

extern SHFLSTRING *nemuvfs_construct_shflstring(char *szName, size_t cbName);

#endif /* KERNEL */

extern int nemuvfs_register_filesystem(void);
extern int nemuvfs_unregister_filesystem(void);

/* VFS options */
extern struct vfsops g_oNemuVFSOpts;

extern int (**g_NemuVFSVnodeDirOpsVector)(void *);
extern int g_cNemuVFSVnodeOpvDescListSize;
extern struct vnodeopv_desc *g_NemuVFSVnodeOpvDescList[];

/* Mount info */
struct nemuvfs_mount_info
{
    uint32_t    magic;
    char        name[MAXPATHLEN];   /* share name */
};

