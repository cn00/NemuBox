/* $Id: nemufs.h $ */
/** @file
 * VirtualBox File System Driver for Solaris Guests, Internal Header.
 */

/*
 * Copyright (C) 2009-2010 Oracle Corporation
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

#ifndef ___NemuVFS_Solaris_h
#define ___NemuVFS_Solaris_h

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_HOST_NAME   256
#define MAX_NLS_NAME    32
/** Default stat cache ttl (in ms) */
#define DEF_STAT_TTL_MS 200

/** The module name. */
#define DEVICE_NAME              "nemufs"

#ifdef _KERNEL

#include <Nemu/NemuGuestLibSharedFolders.h>
#include <sys/vfs.h>

/** VNode for NemuVFS */
typedef struct nemuvfs_vnode
{
    vnode_t     *pVNode;
    vattr_t     Attr;
    SHFLSTRING  *pPath;
    kmutex_t    MtxContents;
} nemuvfs_vnode_t;


/** Per-file system mount instance data. */
typedef struct nemuvfs_globinfo
{
    VBGLSFMAP       Map;
    int             Ttl;
    int             Uid;
    int             Gid;
    vfs_t           *pVFS;
    nemuvfs_vnode_t *pVNodeRoot;
    kmutex_t        MtxFS;
} nemuvfs_globinfo_t;

extern struct vnodeops *g_pNemuVFS_vnodeops;
extern const fs_operation_def_t g_NemuVFS_vnodeops_template[];
extern VBGLSFCLIENT g_NemuVFSClient;

/** Helper functions */
extern int nemuvfs_Stat(const char *pszCaller, nemuvfs_globinfo_t *pNemuVFSGlobalInfo, SHFLSTRING *pPath,
                        PSHFLFSOBJINFO pResult, boolean_t fAllowFailure);
extern void nemuvfs_InitVNode(nemuvfs_globinfo_t *pNemuVFSGlobalInfo, nemuvfs_vnode_t *pNemuVNode,
                              PSHFLFSOBJINFO pFSInfo);


/** Helper macros */
#define VFS_TO_NEMUVFS(vfs)      ((nemuvfs_globinfo_t *)((vfs)->vfs_data))
#define NEMUVFS_TO_VFS(nemuvfs)  ((nemuvfs)->pVFS)
#define VN_TO_NEMUVN(vnode)      ((nemuvfs_vnode_t *)((vnode)->v_data))
#define NEMUVN_TO_VN(nemuvnode)  ((nemuvnode)->pVNode)

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* !___NemuVFS_Solaris_h */

