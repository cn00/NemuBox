/* $Id: nemu_drv.h $ */
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
 * ast_drv.h
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
#ifndef __NEMU_DRV_H__
#define __NEMU_DRV_H__

#include "the-linux-kernel.h"

#include <Nemu/NemuVideoGuest.h>

#include <iprt/log.h>

#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>

#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_memory.h>
#include <drm/ttm/ttm_module.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
# include <drm/drm_gem.h>
#endif

/* #include "nemuvideo.h" */

#include "product-generated.h"

#define DRIVER_AUTHOR       NEMU_VENDOR

#define DRIVER_NAME         "nemuvideo"
#define DRIVER_DESC         NEMU_PRODUCT " Graphics Card"
#define DRIVER_DATE         "20130823"

#define DRIVER_MAJOR        1
#define DRIVER_MINOR        0
#define DRIVER_PATCHLEVEL   0

#define NEMU_MAX_CURSOR_WIDTH  64
#define NEMU_MAX_CURSOR_HEIGHT 64

struct nemu_fbdev;

struct nemu_private
{
    struct drm_device *dev;

    void __iomem *vram;
    HGSMIGUESTCOMMANDCONTEXT Ctx;
    struct VBVABUFFERCONTEXT *paVBVACtx;
    bool fAnyX;
    unsigned cCrtcs;
    bool vga2_clone;
    /** Amount of available VRAM, including space used for buffers. */
    uint32_t full_vram_size;
    /** Amount of available VRAM, not including space used for buffers. */
    uint32_t vram_size;

    struct nemu_fbdev *fbdev;

    int fb_mtrr;

    struct
    {
        struct drm_global_reference mem_global_ref;
        struct ttm_bo_global_ref bo_global_ref;
        struct ttm_bo_device bdev;
    } ttm;

    spinlock_t dev_lock;
};

int nemu_driver_load(struct drm_device *dev, unsigned long flags);
int nemu_driver_unload(struct drm_device *dev);
void nemu_driver_lastclose(struct drm_device *pDev);

struct nemu_gem_object;

struct nemu_connector
{
    struct drm_connector base;
    char szName[32];
    /** Device attribute for sysfs file used for receiving mode hints from user
     * space. */
    struct device_attribute deviceAttribute;
    struct
    {
        uint16_t cX;
        uint16_t cY;
    } modeHint;
};

struct nemu_crtc
{
    struct drm_crtc base;
    bool fBlanked;
    unsigned crtc_id;
    uint32_t offFB;
    struct drm_gem_object *cursor_bo;
    uint64_t cursor_addr;
    int cursor_width, cursor_height;
    u8 offset_x, offset_y;
};

struct nemu_encoder
{
    struct drm_encoder base;
};

struct nemu_framebuffer
{
    struct drm_framebuffer base;
    struct drm_gem_object *obj;
};

struct nemu_fbdev
{
    struct drm_fb_helper helper;
    struct nemu_framebuffer afb;
    struct list_head fbdev_list;
    void *sysram;
    int size;
    struct ttm_bo_kmap_obj mapping;
    int x1, y1, x2, y2; /* dirty rect */
};

#define to_nemu_crtc(x) container_of(x, struct nemu_crtc, base)
#define to_nemu_connector(x) container_of(x, struct nemu_connector, base)
#define to_nemu_encoder(x) container_of(x, struct nemu_encoder, base)
#define to_nemu_framebuffer(x) container_of(x, struct nemu_framebuffer, base)

extern int nemu_mode_init(struct drm_device *dev);
extern void nemu_mode_fini(struct drm_device *dev);
extern void NemuRefreshModes(struct drm_device *pDev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
# define DRM_MODE_FB_CMD drm_mode_fb_cmd
#else
# define DRM_MODE_FB_CMD drm_mode_fb_cmd2
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
# define CRTC_FB(crtc) (crtc)->fb
#else
# define CRTC_FB(crtc) (crtc)->primary->fb
#endif

void nemu_framebuffer_dirty_rectangles(struct drm_framebuffer *fb,
                                       struct drm_clip_rect *pRects,
                                       unsigned cRects);

int nemu_framebuffer_init(struct drm_device *dev,
             struct nemu_framebuffer *nemu_fb,
             struct DRM_MODE_FB_CMD *mode_cmd,
             struct drm_gem_object *obj);

int nemu_fbdev_init(struct drm_device *dev);
void nemu_fbdev_fini(struct drm_device *dev);
void nemu_fbdev_set_suspend(struct drm_device *dev, int state);

struct nemu_bo
{
    struct ttm_buffer_object bo;
    struct ttm_placement placement;
    struct ttm_bo_kmap_obj kmap;
    struct drm_gem_object gem;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)
    u32 placements[3];
#else
    struct ttm_place placements[3];
#endif
    int pin_count;
};
#define gem_to_nemu_bo(gobj) container_of((gobj), struct nemu_bo, gem)

static inline struct nemu_bo * nemu_bo(struct ttm_buffer_object *bo)
{
    return container_of(bo, struct nemu_bo, bo);
}


#define to_nemu_obj(x) container_of(x, struct nemu_gem_object, base)

extern int nemu_dumb_create(struct drm_file *file,
               struct drm_device *dev,
               struct drm_mode_create_dumb *args);
extern int nemu_dumb_destroy(struct drm_file *file,
                struct drm_device *dev,
                uint32_t handle);

extern void nemu_gem_free_object(struct drm_gem_object *obj);
extern int nemu_dumb_mmap_offset(struct drm_file *file,
                struct drm_device *dev,
                uint32_t handle,
                uint64_t *offset);

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

int nemu_mm_init(struct nemu_private *nemu);
void nemu_mm_fini(struct nemu_private *nemu);

int nemu_bo_create(struct drm_device *dev, int size, int align,
          uint32_t flags, struct nemu_bo **pnemubo);

int nemu_gem_create(struct drm_device *dev,
           u32 size, bool iskernel,
           struct drm_gem_object **obj);

int nemu_bo_pin(struct nemu_bo *bo, u32 pl_flag, u64 *gpu_addr);
int nemu_bo_unpin(struct nemu_bo *bo);

int nemu_bo_reserve(struct nemu_bo *bo, bool no_wait);
void nemu_bo_unreserve(struct nemu_bo *bo);
void nemu_ttm_placement(struct nemu_bo *bo, int domain);
int nemu_bo_push_sysram(struct nemu_bo *bo);
int nemu_mmap(struct file *filp, struct vm_area_struct *vma);

/* nemu post */
void nemu_post_gpu(struct drm_device *dev);
#endif
