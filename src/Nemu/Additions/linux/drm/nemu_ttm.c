/* $Id: nemu_ttm.c $ */
/** @file
 * VirtualBox Additions Linux kernel video driver
 */

/*
 * Copyright (C) 2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 * --------------------------------------------------------------------
 *
 * This code is based on
 * ast_ttm.c
 * with the following copyright and permission notice:
 *
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */
#include "nemu_drv.h"
#include <ttm/ttm_page_alloc.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)
# define PLACEMENT_FLAGS(placement) (placement)
#else
# define PLACEMENT_FLAGS(placement) (placement).flags
#endif

static inline struct nemu_private *
nemu_bdev(struct ttm_bo_device *bd)
{
    return container_of(bd, struct nemu_private, ttm.bdev);
}

static int
nemu_ttm_mem_global_init(struct drm_global_reference *ref)
{
    return ttm_mem_global_init(ref->object);
}

static void
nemu_ttm_mem_global_release(struct drm_global_reference *ref)
{
    ttm_mem_global_release(ref->object);
}

/**
 * Adds the nemu memory manager object/structures to the global memory manager.
 */
static int nemu_ttm_global_init(struct nemu_private *nemu)
{
    struct drm_global_reference *global_ref;
    int r;

    global_ref = &nemu->ttm.mem_global_ref;
    global_ref->global_type = DRM_GLOBAL_TTM_MEM;
    global_ref->size = sizeof(struct ttm_mem_global);
    global_ref->init = &nemu_ttm_mem_global_init;
    global_ref->release = &nemu_ttm_mem_global_release;
    r = drm_global_item_ref(global_ref);
    if (r != 0)
    {
        DRM_ERROR("Failed setting up TTM memory accounting "
              "subsystem.\n");
        return r;
    }

    nemu->ttm.bo_global_ref.mem_glob =
        nemu->ttm.mem_global_ref.object;
    global_ref = &nemu->ttm.bo_global_ref.ref;
    global_ref->global_type = DRM_GLOBAL_TTM_BO;
    global_ref->size = sizeof(struct ttm_bo_global);
    global_ref->init = &ttm_bo_global_init;
    global_ref->release = &ttm_bo_global_release;
    r = drm_global_item_ref(global_ref);
    if (r != 0)
    {
        DRM_ERROR("Failed setting up TTM BO subsystem.\n");
        drm_global_item_unref(&nemu->ttm.mem_global_ref);
        return r;
    }
    return 0;
}

/**
 * Removes the nemu memory manager object from the global memory manager.
 */
void
nemu_ttm_global_release(struct nemu_private *nemu)
{
    if (nemu->ttm.mem_global_ref.release == NULL)
        return;

    drm_global_item_unref(&nemu->ttm.bo_global_ref.ref);
    drm_global_item_unref(&nemu->ttm.mem_global_ref);
    nemu->ttm.mem_global_ref.release = NULL;
}


static void nemu_bo_ttm_destroy(struct ttm_buffer_object *tbo)
{
    struct nemu_bo *bo;

    bo = container_of(tbo, struct nemu_bo, bo);

    drm_gem_object_release(&bo->gem);
    kfree(bo);
}

bool nemu_ttm_bo_is_nemu_bo(struct ttm_buffer_object *bo)
{
    if (bo->destroy == &nemu_bo_ttm_destroy)
        return true;
    return false;
}

static int
nemu_bo_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
             struct ttm_mem_type_manager *man)
{
    switch (type)
    {
    case TTM_PL_SYSTEM:
        man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
        man->available_caching = TTM_PL_MASK_CACHING;
        man->default_caching = TTM_PL_FLAG_CACHED;
        break;
    case TTM_PL_VRAM:
        man->func = &ttm_bo_manager_func;
        man->flags = TTM_MEMTYPE_FLAG_FIXED |
            TTM_MEMTYPE_FLAG_MAPPABLE;
        man->available_caching = TTM_PL_FLAG_UNCACHED |
            TTM_PL_FLAG_WC;
        man->default_caching = TTM_PL_FLAG_WC;
        break;
    default:
        DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
        return -EINVAL;
    }
    return 0;
}

static void
nemu_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
    struct nemu_bo *nemubo = nemu_bo(bo);

    if (!nemu_ttm_bo_is_nemu_bo(bo))
        return;

    nemu_ttm_placement(nemubo, TTM_PL_FLAG_SYSTEM);
    *pl = nemubo->placement;
}

static int nemu_bo_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
    return 0;
}

static int nemu_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
                  struct ttm_mem_reg *mem)
{
    struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
    struct nemu_private *nemu = nemu_bdev(bdev);

    mem->bus.addr = NULL;
    mem->bus.offset = 0;
    mem->bus.size = mem->num_pages << PAGE_SHIFT;
    mem->bus.base = 0;
    mem->bus.is_iomem = false;
    if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
        return -EINVAL;
    switch (mem->mem_type)
    {
    case TTM_PL_SYSTEM:
        /* system memory */
        return 0;
    case TTM_PL_VRAM:
        mem->bus.offset = mem->start << PAGE_SHIFT;
        mem->bus.base = pci_resource_start(nemu->dev->pdev, 0);
        mem->bus.is_iomem = true;
        break;
    default:
        return -EINVAL;
        break;
    }
    return 0;
}

static void nemu_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
}

static int nemu_bo_move(struct ttm_buffer_object *bo,
               bool evict, bool interruptible,
               bool no_wait_gpu,
               struct ttm_mem_reg *new_mem)
{
    int r;
    r = ttm_bo_move_memcpy(bo, evict, no_wait_gpu, new_mem);
    return r;
}


static void nemu_ttm_backend_destroy(struct ttm_tt *tt)
{
    ttm_tt_fini(tt);
    kfree(tt);
}

static struct ttm_backend_func nemu_tt_backend_func =
{
    .destroy = &nemu_ttm_backend_destroy,
};


struct ttm_tt *nemu_ttm_tt_create(struct ttm_bo_device *bdev,
                 unsigned long size, uint32_t page_flags,
                 struct page *dummy_read_page)
{
    struct ttm_tt *tt;

    tt = kzalloc(sizeof(struct ttm_tt), GFP_KERNEL);
    if (tt == NULL)
        return NULL;
    tt->func = &nemu_tt_backend_func;
    if (ttm_tt_init(tt, bdev, size, page_flags, dummy_read_page))
    {
        kfree(tt);
        return NULL;
    }
    return tt;
}

static int nemu_ttm_tt_populate(struct ttm_tt *ttm)
{
    return ttm_pool_populate(ttm);
}

static void nemu_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
    ttm_pool_unpopulate(ttm);
}

struct ttm_bo_driver nemu_bo_driver =
{
    .ttm_tt_create = nemu_ttm_tt_create,
    .ttm_tt_populate = nemu_ttm_tt_populate,
    .ttm_tt_unpopulate = nemu_ttm_tt_unpopulate,
    .init_mem_type = nemu_bo_init_mem_type,
    .evict_flags = nemu_bo_evict_flags,
    .move = nemu_bo_move,
    .verify_access = nemu_bo_verify_access,
    .io_mem_reserve = &nemu_ttm_io_mem_reserve,
    .io_mem_free = &nemu_ttm_io_mem_free,
};

int nemu_mm_init(struct nemu_private *nemu)
{
    int ret;
    struct drm_device *dev = nemu->dev;
    struct ttm_bo_device *bdev = &nemu->ttm.bdev;

    ret = nemu_ttm_global_init(nemu);
    if (ret)
        return ret;

    ret = ttm_bo_device_init(&nemu->ttm.bdev,
                 nemu->ttm.bo_global_ref.ref.object,
                 &nemu_bo_driver,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
                 dev->anon_inode->i_mapping,
#endif
                 DRM_FILE_PAGE_OFFSET,
                 true);
    if (ret)
    {
        DRM_ERROR("Error initialising bo driver; %d\n", ret);
        return ret;
    }

    ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
                 nemu->vram_size >> PAGE_SHIFT);
    if (ret)
    {
        DRM_ERROR("Failed ttm VRAM init: %d\n", ret);
        return ret;
    }

#ifdef DRM_MTRR_WC
    nemu->fb_mtrr = drm_mtrr_add(pci_resource_start(dev->pdev, 0),
                    pci_resource_len(dev->pdev, 0),
                    DRM_MTRR_WC);
#else
    nemu->fb_mtrr = arch_phys_wc_add(pci_resource_start(dev->pdev, 0),
                    pci_resource_len(dev->pdev, 0));
#endif

    return 0;
}

void nemu_mm_fini(struct nemu_private *nemu)
{
    struct drm_device *dev = nemu->dev;
    ttm_bo_device_release(&nemu->ttm.bdev);

    nemu_ttm_global_release(nemu);

    if (nemu->fb_mtrr >= 0)
    {
#ifdef DRM_MTRR_WC
        drm_mtrr_del(nemu->fb_mtrr,
                 pci_resource_start(dev->pdev, 0),
                 pci_resource_len(dev->pdev, 0), DRM_MTRR_WC);
#else
        arch_phys_wc_del(nemu->fb_mtrr);
#endif
        nemu->fb_mtrr = -1;
    }
}

void nemu_ttm_placement(struct nemu_bo *bo, int domain)
{
    u32 c = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)
    bo->placement.fpfn = 0;
    bo->placement.lpfn = 0;
#else
    unsigned i;
#endif

    bo->placement.placement = bo->placements;
    bo->placement.busy_placement = bo->placements;
    if (domain & TTM_PL_FLAG_VRAM)
        PLACEMENT_FLAGS(bo->placements[c++]) = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_VRAM;
    if (domain & TTM_PL_FLAG_SYSTEM)
        PLACEMENT_FLAGS(bo->placements[c++]) = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
    if (!c)
        PLACEMENT_FLAGS(bo->placements[c++]) = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
    bo->placement.num_placement = c;
    bo->placement.num_busy_placement = c;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
    for (i = 0; i < c; ++i)
    {
        bo->placements[i].fpfn = 0;
        bo->placements[i].lpfn = 0;
    }
#endif
}

int nemu_bo_reserve(struct nemu_bo *bo, bool no_wait)
{
    int ret;

    ret = ttm_bo_reserve(&bo->bo, true, no_wait, false, 0);
    if (ret)
    {
        if (ret != -ERESTARTSYS && ret != -EBUSY)
            DRM_ERROR("reserve failed %p\n", bo);
        return ret;
    }
    return 0;
}

void nemu_bo_unreserve(struct nemu_bo *bo)
{
    ttm_bo_unreserve(&bo->bo);
}

int nemu_bo_create(struct drm_device *dev, int size, int align,
          uint32_t flags, struct nemu_bo **pnemubo)
{
    struct nemu_private *nemu = dev->dev_private;
    struct nemu_bo *nemubo;
    size_t acc_size;
    int ret;

    nemubo = kzalloc(sizeof(struct nemu_bo), GFP_KERNEL);
    if (!nemubo)
        return -ENOMEM;

    ret = drm_gem_object_init(dev, &nemubo->gem, size);
    if (ret)
    {
        kfree(nemubo);
        return ret;
    }

    nemubo->bo.bdev = &nemu->ttm.bdev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
    nemubo->bo.bdev->dev_mapping = dev->dev_mapping;
#endif

    nemu_ttm_placement(nemubo, TTM_PL_FLAG_VRAM | TTM_PL_FLAG_SYSTEM);

    acc_size = ttm_bo_dma_acc_size(&nemu->ttm.bdev, size,
                       sizeof(struct nemu_bo));

    ret = ttm_bo_init(&nemu->ttm.bdev, &nemubo->bo, size,
              ttm_bo_type_device, &nemubo->placement,
              align >> PAGE_SHIFT, false, NULL, acc_size,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
              NULL,
#endif
              NULL, nemu_bo_ttm_destroy);
    if (ret)
        return ret;

    *pnemubo = nemubo;
    return 0;
}

static inline u64 nemu_bo_gpu_offset(struct nemu_bo *bo)
{
    return bo->bo.offset;
}

int nemu_bo_pin(struct nemu_bo *bo, u32 pl_flag, u64 *gpu_addr)
{
    int i, ret;

    if (bo->pin_count)
    {
        bo->pin_count++;
        if (gpu_addr)
            *gpu_addr = nemu_bo_gpu_offset(bo);
        return 0;
    }

    nemu_ttm_placement(bo, pl_flag);
    for (i = 0; i < bo->placement.num_placement; i++)
        PLACEMENT_FLAGS(bo->placements[i]) |= TTM_PL_FLAG_NO_EVICT;
    ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
    if (ret)
        return ret;

    bo->pin_count = 1;
    if (gpu_addr)
        *gpu_addr = nemu_bo_gpu_offset(bo);
    return 0;
}

int nemu_bo_unpin(struct nemu_bo *bo)
{
    int i, ret;
    if (!bo->pin_count)
    {
        DRM_ERROR("unpin bad %p\n", bo);
        return 0;
    }
    bo->pin_count--;
    if (bo->pin_count)
        return 0;

    for (i = 0; i < bo->placement.num_placement ; i++)
        PLACEMENT_FLAGS(bo->placements[i]) &= ~TTM_PL_FLAG_NO_EVICT;
    ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
    if (ret)
        return ret;

    return 0;
}

/* Move a nemu-owned buffer object to system memory if no one else has it
 * pinned.  The caller must have pinned it previously, and this call will
 * release the caller's pin. */
int nemu_bo_push_sysram(struct nemu_bo *bo)
{
    int i, ret;
    if (!bo->pin_count)
    {
        DRM_ERROR("unpin bad %p\n", bo);
        return 0;
    }
    bo->pin_count--;
    if (bo->pin_count)
        return 0;

    if (bo->kmap.virtual)
        ttm_bo_kunmap(&bo->kmap);

    nemu_ttm_placement(bo, TTM_PL_FLAG_SYSTEM);
    for (i = 0; i < bo->placement.num_placement ; i++)
        PLACEMENT_FLAGS(bo->placements[i]) |= TTM_PL_FLAG_NO_EVICT;

    ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
    if (ret)
    {
        DRM_ERROR("pushing to VRAM failed\n");
        return ret;
    }
    return 0;
}

int nemu_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct drm_file *file_priv;
    struct nemu_private *nemu;

    if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
        return -EINVAL;

    file_priv = filp->private_data;
    nemu = file_priv->minor->dev->dev_private;
    return ttm_bo_mmap(filp, vma, &nemu->ttm.bdev);
}
