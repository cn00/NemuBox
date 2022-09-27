/* $Id: nemuvfs_vnops.c $ */
/** @file
 * Description.
 */

/*
 * Copyright (C) 2008-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "nemuvfs.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/limits.h>
#include <sys/lockf.h>
#include <sys/stat.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

/*
 * Prototypes for NEMUVFS vnode operations
 */
static vop_create_t     nemuvfs_create;
static vop_mknod_t      nemuvfs_mknod;
static vop_open_t       nemuvfs_open;
static vop_close_t      nemuvfs_close;
static vop_access_t     nemuvfs_access;
static vop_getattr_t    nemuvfs_getattr;
static vop_setattr_t    nemuvfs_setattr;
static vop_read_t       nemuvfs_read;
static vop_write_t      nemuvfs_write;
static vop_fsync_t      nemuvfs_fsync;
static vop_remove_t     nemuvfs_remove;
static vop_link_t       nemuvfs_link;
static vop_lookup_t     nemuvfs_lookup;
static vop_rename_t     nemuvfs_rename;
static vop_mkdir_t      nemuvfs_mkdir;
static vop_rmdir_t      nemuvfs_rmdir;
static vop_symlink_t    nemuvfs_symlink;
static vop_readdir_t    nemuvfs_readdir;
static vop_strategy_t   nemuvfs_strategy;
static vop_print_t      nemuvfs_print;
static vop_pathconf_t   nemuvfs_pathconf;
static vop_advlock_t    nemuvfs_advlock;
static vop_getextattr_t nemuvfs_getextattr;
static vop_ioctl_t      nemuvfs_ioctl;
static vop_getpages_t   nemuvfs_getpages;
static vop_inactive_t   nemuvfs_inactive;
static vop_putpages_t   nemuvfs_putpages;
static vop_reclaim_t    nemuvfs_reclaim;

struct vop_vector nemuvfs_vnodeops = {
    .vop_default    =   &default_vnodeops,

    .vop_access     =   nemuvfs_access,
    .vop_advlock    =   nemuvfs_advlock,
    .vop_close      =   nemuvfs_close,
    .vop_create     =   nemuvfs_create,
    .vop_fsync      =   nemuvfs_fsync,
    .vop_getattr    =   nemuvfs_getattr,
    .vop_getextattr =   nemuvfs_getextattr,
    .vop_getpages   =   nemuvfs_getpages,
    .vop_inactive   =   nemuvfs_inactive,
    .vop_ioctl      =   nemuvfs_ioctl,
    .vop_link       =   nemuvfs_link,
    .vop_lookup     =   nemuvfs_lookup,
    .vop_mkdir      =   nemuvfs_mkdir,
    .vop_mknod      =   nemuvfs_mknod,
    .vop_open       =   nemuvfs_open,
    .vop_pathconf   =   nemuvfs_pathconf,
    .vop_print      =   nemuvfs_print,
    .vop_putpages   =   nemuvfs_putpages,
    .vop_read       =   nemuvfs_read,
    .vop_readdir    =   nemuvfs_readdir,
    .vop_reclaim    =   nemuvfs_reclaim,
    .vop_remove     =   nemuvfs_remove,
    .vop_rename     =   nemuvfs_rename,
    .vop_rmdir      =   nemuvfs_rmdir,
    .vop_setattr    =   nemuvfs_setattr,
    .vop_strategy   =   nemuvfs_strategy,
    .vop_symlink    =   nemuvfs_symlink,
    .vop_write      =   nemuvfs_write,
};

static int nemuvfs_access(struct vop_access_args *ap)
{
    return 0;
}

static int nemuvfs_open(struct vop_open_args *ap)
{
    return 0;
}

static int nemuvfs_close(struct vop_close_args *ap)
{
    return 0;
}

static int nemuvfs_getattr(struct vop_getattr_args *ap)
{
    return 0;
}

static int nemuvfs_setattr(struct vop_setattr_args *ap)
{
    return 0;
}

static int nemuvfs_read(struct vop_read_args *ap)
{
    return 0;
}

static int nemuvfs_write(struct vop_write_args *ap)
{
    return 0;
}

static int nemuvfs_create(struct vop_create_args *ap)
{
    return 0;
}

static int nemuvfs_remove(struct vop_remove_args *ap)
{
    return 0;
}

static int nemuvfs_rename(struct vop_rename_args *ap)
{
    return 0;
}

static int nemuvfs_link(struct vop_link_args *ap)
{
    return EOPNOTSUPP;
}

static int nemuvfs_symlink(struct vop_symlink_args *ap)
{
    return EOPNOTSUPP;
}

static int nemuvfs_mknod(struct vop_mknod_args *ap)
{
    return EOPNOTSUPP;
}

static int nemuvfs_mkdir(struct vop_mkdir_args *ap)
{
    return 0;
}

static int nemuvfs_rmdir(struct vop_rmdir_args *ap)
{
    return 0;
}

static int nemuvfs_readdir(struct vop_readdir_args *ap)
{
    return 0;
}

static int nemuvfs_fsync(struct vop_fsync_args *ap)
{
    return 0;
}

static int nemuvfs_print (struct vop_print_args *ap)
{
    return 0;
}

static int nemuvfs_pathconf (struct vop_pathconf_args *ap)
{
    return 0;
}

static int nemuvfs_strategy (struct vop_strategy_args *ap)
{
    return 0;
}

static int nemuvfs_ioctl(struct vop_ioctl_args *ap)
{
    return ENOTTY;
}

static int nemuvfs_getextattr(struct vop_getextattr_args *ap)
{
    return 0;
}

static int nemuvfs_advlock(struct vop_advlock_args *ap)
{
    return 0;
}

static int nemuvfs_lookup(struct vop_lookup_args *ap)
{
    return 0;
}

static int nemuvfs_inactive(struct vop_inactive_args *ap)
{
    return 0;
}

static int nemuvfs_reclaim(struct vop_reclaim_args *ap)
{
    return 0;
}

static int nemuvfs_getpages(struct vop_getpages_args *ap)
{
    return 0;
}

static int nemuvfs_putpages(struct vop_putpages_args *ap)
{
    return 0;
}

