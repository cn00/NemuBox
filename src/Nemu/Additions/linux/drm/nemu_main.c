/* $Id: nemu_main.c $ */
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
 * ast_main.c
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

#include <Nemu/NemuVideoGuest.h>
#include <Nemu/NemuVideo.h>

#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>

static void nemu_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
    struct nemu_framebuffer *nemu_fb = to_nemu_framebuffer(fb);
    if (nemu_fb->obj)
        drm_gem_object_unreference_unlocked(nemu_fb->obj);

    LogFunc(("nemuvideo: %d: nemu_fb=%p, nemu_fb->obj=%p\n", __LINE__,
             nemu_fb, nemu_fb->obj));
    drm_framebuffer_cleanup(fb);
    kfree(fb);
}

static int nemu_user_framebuffer_create_handle(struct drm_framebuffer *fb,
                          struct drm_file *file,
                          unsigned int *handle)
{
    return -EINVAL;
}

/** Send information about dirty rectangles to VBVA.  If necessary we enable
 * VBVA first, as this is normally disabled after a mode set in case a user
 * takes over the console that is not aware of VBVA (i.e. the VESA BIOS). */
void nemu_framebuffer_dirty_rectangles(struct drm_framebuffer *fb,
                                       struct drm_clip_rect *pRects,
                                       unsigned cRects)
{
    struct nemu_private *nemu = fb->dev->dev_private;
    unsigned i;
    unsigned long flags;

    LogFunc(("nemuvideo: %d: fb=%p, cRects=%u, nemu=%p\n", __LINE__, fb,
             cRects, nemu));
    spin_lock_irqsave(&nemu->dev_lock, flags);
    for (i = 0; i < cRects; ++i)
    {
        struct drm_crtc *crtc;
        list_for_each_entry(crtc, &fb->dev->mode_config.crtc_list, head)
        {
            unsigned iCrtc = to_nemu_crtc(crtc)->crtc_id;
            struct VBVABUFFER *pVBVA = nemu->paVBVACtx[iCrtc].pVBVA;
            VBVACMDHDR cmdHdr;

            if (!pVBVA)
            {
                pVBVA = (struct VBVABUFFER *) (  ((uint8_t *)nemu->vram)
                                               + nemu->vram_size
                                               + iCrtc * VBVA_MIN_BUFFER_SIZE);
                if (!NemuVBVAEnable(&nemu->paVBVACtx[iCrtc], &nemu->Ctx, pVBVA, iCrtc))
                    AssertReleaseMsgFailed(("NemuVBVAEnable failed - heap allocation error, very old host or driver error.\n"));
            }
            if (   CRTC_FB(crtc) != fb
                || pRects[i].x1 >   crtc->x
                                  + crtc->hwmode.hdisplay
                || pRects[i].y1 >   crtc->y
                                  + crtc->hwmode.vdisplay
                || pRects[i].x2 < crtc->x
                || pRects[i].y2 < crtc->y)
                continue;
            cmdHdr.x = (int16_t)pRects[i].x1;
            cmdHdr.y = (int16_t)pRects[i].y1;
            cmdHdr.w = (uint16_t)pRects[i].x2 - pRects[i].x1;
            cmdHdr.h = (uint16_t)pRects[i].y2 - pRects[i].y1;
            if (NemuVBVABufferBeginUpdate(&nemu->paVBVACtx[iCrtc],
                                          &nemu->Ctx))
            {
                NemuVBVAWrite(&nemu->paVBVACtx[iCrtc], &nemu->Ctx, &cmdHdr,
                              sizeof(cmdHdr));
                NemuVBVABufferEndUpdate(&nemu->paVBVACtx[iCrtc]);
            }
        }
    }
    spin_unlock_irqrestore(&nemu->dev_lock, flags);
    LogFunc(("nemuvideo: %d\n", __LINE__));
}

static int nemu_user_framebuffer_dirty(struct drm_framebuffer *fb,
                                       struct drm_file *file_priv,
                                       unsigned flags, unsigned color,
                                       struct drm_clip_rect *pRects,
                                       unsigned cRects)
{
    LogFunc(("nemuvideo: %d, flags=%u\n", __LINE__, flags));
    nemu_framebuffer_dirty_rectangles(fb, pRects, cRects);
    return 0;
}

static const struct drm_framebuffer_funcs nemu_fb_funcs =
{
    .destroy = nemu_user_framebuffer_destroy,
    .create_handle = nemu_user_framebuffer_create_handle,
    .dirty = nemu_user_framebuffer_dirty,
};


int nemu_framebuffer_init(struct drm_device *dev,
             struct nemu_framebuffer *nemu_fb,
             struct DRM_MODE_FB_CMD *mode_cmd,
             struct drm_gem_object *obj)
{
    int ret;

    LogFunc(("nemuvideo: %d: dev=%p, nemu_fb=%p, obj=%p\n", __LINE__, dev,
             nemu_fb, obj));
    drm_helper_mode_fill_fb_struct(&nemu_fb->base, mode_cmd);
    nemu_fb->obj = obj;
    ret = drm_framebuffer_init(dev, &nemu_fb->base, &nemu_fb_funcs);
    if (ret)
    {
        DRM_ERROR("framebuffer init failed %d\n", ret);
        LogFunc(("nemuvideo: %d\n", __LINE__));
        return ret;
    }
    LogFunc(("nemuvideo: %d\n", __LINE__));
    return 0;
}

static struct drm_framebuffer *
nemu_user_framebuffer_create(struct drm_device *dev,
           struct drm_file *filp,
           struct drm_mode_fb_cmd2 *mode_cmd)
{
    struct drm_gem_object *obj;
    struct nemu_framebuffer *nemu_fb;
    int ret;

    LogFunc(("nemuvideo: %d\n", __LINE__));
    obj = drm_gem_object_lookup(dev, filp, mode_cmd->handles[0]);
    if (obj == NULL)
        return ERR_PTR(-ENOENT);

    nemu_fb = kzalloc(sizeof(*nemu_fb), GFP_KERNEL);
    if (!nemu_fb)
    {
        drm_gem_object_unreference_unlocked(obj);
        return ERR_PTR(-ENOMEM);
    }

    ret = nemu_framebuffer_init(dev, nemu_fb, mode_cmd, obj);
    if (ret)
    {
        drm_gem_object_unreference_unlocked(obj);
        kfree(nemu_fb);
        return ERR_PTR(ret);
    }
    LogFunc(("nemuvideo: %d\n", __LINE__));
    return &nemu_fb->base;
}

static const struct drm_mode_config_funcs nemu_mode_funcs =
{
    .fb_create = nemu_user_framebuffer_create,
};

static void disableVBVA(struct nemu_private *pNemu)
{
    unsigned i;

    if (pNemu->paVBVACtx)
    {
        for (i = 0; i < pNemu->cCrtcs; ++i)
            NemuVBVADisable(&pNemu->paVBVACtx[i], &pNemu->Ctx, i);
        kfree(pNemu->paVBVACtx);
        pNemu->paVBVACtx = NULL;
    }
}

static int nemu_vbva_init(struct nemu_private *nemu)
{
    unsigned i;
    bool fRC = true;
    LogFunc(("nemuvideo: %d: nemu=%p, nemu->cCrtcs=%u, nemu->paVBVACtx=%p\n",
             __LINE__, nemu, (unsigned)nemu->cCrtcs, nemu->paVBVACtx));
    if (!nemu->paVBVACtx)
    {
        nemu->paVBVACtx = kzalloc(  sizeof(struct VBVABUFFERCONTEXT)
                                  * nemu->cCrtcs,
                                  GFP_KERNEL);
        if (!nemu->paVBVACtx)
            return -ENOMEM;
    }
    /* Take a command buffer for each screen from the end of usable VRAM. */
    nemu->vram_size -= nemu->cCrtcs * VBVA_MIN_BUFFER_SIZE;
    for (i = 0; i < nemu->cCrtcs; ++i)
        NemuVBVASetupBufferContext(&nemu->paVBVACtx[i],
                                   nemu->vram_size + i * VBVA_MIN_BUFFER_SIZE,
                                   VBVA_MIN_BUFFER_SIZE);
    LogFunc(("nemuvideo: %d: nemu->paVBVACtx=%p, nemu->vram_size=%u\n",
             __LINE__, nemu->paVBVACtx, (unsigned)nemu->vram_size));
    return 0;
}


/** Allocation function for the HGSMI heap and data. */
static DECLCALLBACK(void *) hgsmiEnvAlloc(void *pvEnv, HGSMISIZE cb)
{
    NOREF(pvEnv);
    return kmalloc(cb, GFP_KERNEL);
}


/** Free function for the HGSMI heap and data. */
static DECLCALLBACK(void) hgsmiEnvFree(void *pvEnv, void *pv)
{
    NOREF(pvEnv);
    kfree(pv);
}


/** Pointers to the HGSMI heap and data manipulation functions. */
static HGSMIENV g_hgsmiEnv =
{
    NULL,
    hgsmiEnvAlloc,
    hgsmiEnvFree
};


/** Set up our heaps and data exchange buffers in VRAM before handing the rest
 *  to the memory manager. */
static int setupAcceleration(struct nemu_private *pNemu, uint32_t *poffBase)
{
    uint32_t offBase, offGuestHeap, cbGuestHeap;
    void *pvGuestHeap;

    NemuHGSMIGetBaseMappingInfo(pNemu->full_vram_size, &offBase, NULL,
                                &offGuestHeap, &cbGuestHeap, NULL);
    if (poffBase)
        *poffBase = offBase;
    pvGuestHeap =   ((uint8_t *)pNemu->vram) + offBase + offGuestHeap;
    if (RT_FAILURE(NemuHGSMISetupGuestContext(&pNemu->Ctx, pvGuestHeap,
                                              cbGuestHeap,
                                              offBase + offGuestHeap,
                                              &g_hgsmiEnv)))
        return -ENOMEM;
    /* Reduce available VRAM size to reflect the guest heap. */
    pNemu->vram_size = offBase;
    /* Linux drm represents monitors as a 32-bit array. */
    pNemu->cCrtcs = RT_MIN(NemuHGSMIGetMonitorCount(&pNemu->Ctx), 32);
    return nemu_vbva_init(pNemu);
}


int nemu_driver_load(struct drm_device *dev, unsigned long flags)
{
    struct nemu_private *nemu;
    int ret = 0;
    uint32_t offBase;

    LogFunc(("nemuvideo: %d: dev=%p\n", __LINE__, dev));
    if (!NemuHGSMIIsSupported())
        return -ENODEV;
    nemu = kzalloc(sizeof(struct nemu_private), GFP_KERNEL);
    if (!nemu)
        return -ENOMEM;

    dev->dev_private = nemu;
    nemu->dev = dev;

    spin_lock_init(&nemu->dev_lock);
    /* I hope this won't interfere with the memory manager. */
    nemu->vram = pci_iomap(dev->pdev, 0, 0);
    if (!nemu->vram)
    {
        ret = -EIO;
        goto out_free;
    }
    nemu->full_vram_size = NemuVideoGetVRAMSize();
    nemu->fAnyX = NemuVideoAnyWidthAllowed();
    DRM_INFO("VRAM %08x\n", nemu->full_vram_size);

    ret = setupAcceleration(nemu, &offBase);
    if (ret)
        goto out_free;

    ret = nemu_mm_init(nemu);
    if (ret)
        goto out_free;

    drm_mode_config_init(dev);

    dev->mode_config.funcs = (void *)&nemu_mode_funcs;
    dev->mode_config.min_width = 64;
    dev->mode_config.min_height = 64;
    dev->mode_config.preferred_depth = 24;
    dev->mode_config.max_width = VBE_DISPI_MAX_XRES;
    dev->mode_config.max_height = VBE_DISPI_MAX_YRES;

    ret = nemu_mode_init(dev);
    if (ret)
        goto out_free;

    ret = nemu_fbdev_init(dev);
    if (ret)
        goto out_free;
    LogFunc(("nemuvideo: %d: nemu=%p, nemu->vram=%p, nemu->full_vram_size=%u\n",
             __LINE__, nemu, nemu->vram, (unsigned)nemu->full_vram_size));
    return 0;
out_free:
    if (nemu->vram)
        pci_iounmap(dev->pdev, nemu->vram);
    kfree(nemu);
    dev->dev_private = NULL;
    LogFunc(("nemuvideo: %d: ret=%d\n", __LINE__, ret));
    return ret;
}

int nemu_driver_unload(struct drm_device *dev)
{
    struct nemu_private *nemu = dev->dev_private;

    LogFunc(("nemuvideo: %d\n", __LINE__));
    nemu_mode_fini(dev);
    nemu_fbdev_fini(dev);
    drm_mode_config_cleanup(dev);

    disableVBVA(nemu);
    nemu_mm_fini(nemu);
    pci_iounmap(dev->pdev, nemu->vram);
    kfree(nemu);
    LogFunc(("nemuvideo: %d\n", __LINE__));
    return 0;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
static bool drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper
                                                          *pHelper)
{
       bool rc;

       drm_modeset_lock_all(pHelper->dev);
       rc = drm_fb_helper_restore_fbdev_mode(pHelper);
       drm_modeset_unlock_all(pHelper->dev);
       return rc;
}
#endif


void nemu_driver_lastclose(struct drm_device *pDev)
{
    struct nemu_private *pNemu = pDev->dev_private;

    if (pNemu->fbdev)
        drm_fb_helper_restore_fbdev_mode_unlocked(&pNemu->fbdev->helper);
}


int nemu_gem_create(struct drm_device *dev,
           u32 size, bool iskernel,
           struct drm_gem_object **obj)
{
    struct nemu_bo *nemubo;
    int ret;

    LogFunc(("nemuvideo: %d: dev=%p, size=%u, iskernel=%u\n", __LINE__,
             dev, (unsigned)size, (unsigned)iskernel));
    *obj = NULL;

    size = roundup(size, PAGE_SIZE);
    if (size == 0)
        return -EINVAL;

    ret = nemu_bo_create(dev, size, 0, 0, &nemubo);
    if (ret)
    {
        if (ret != -ERESTARTSYS)
            DRM_ERROR("failed to allocate GEM object\n");
        return ret;
    }
    *obj = &nemubo->gem;
    LogFunc(("nemuvideo: %d: obj=%p\n", __LINE__, obj));
    return 0;
}

int nemu_dumb_create(struct drm_file *file,
            struct drm_device *dev,
            struct drm_mode_create_dumb *args)
{
    int ret;
    struct drm_gem_object *gobj;
    u32 handle;

    LogFunc(("nemuvideo: %d: args->width=%u, args->height=%u, args->bpp=%u\n",
             __LINE__, (unsigned)args->width, (unsigned)args->height,
             (unsigned)args->bpp));
    args->pitch = args->width * ((args->bpp + 7) / 8);
    args->size = args->pitch * args->height;

    ret = nemu_gem_create(dev, args->size, false,
                 &gobj);
    if (ret)
        return ret;

    ret = drm_gem_handle_create(file, gobj, &handle);
    drm_gem_object_unreference_unlocked(gobj);
    if (ret)
        return ret;

    args->handle = handle;
    LogFunc(("nemuvideo: %d: args->handle=%u\n", __LINE__,
             (unsigned)args->handle));
    return 0;
}

int nemu_dumb_destroy(struct drm_file *file,
             struct drm_device *dev,
             uint32_t handle)
{
    LogFunc(("nemuvideo: %d: dev=%p, handle=%u\n", __LINE__, dev,
             (unsigned)handle));
    return drm_gem_handle_delete(file, handle);
}

void nemu_bo_unref(struct nemu_bo **bo)
{
    struct ttm_buffer_object *tbo;

    if ((*bo) == NULL)
        return;

    LogFunc(("nemuvideo: %d: bo=%p\n", __LINE__, bo));
    tbo = &((*bo)->bo);
    ttm_bo_unref(&tbo);
    if (tbo == NULL)
        *bo = NULL;

}
void nemu_gem_free_object(struct drm_gem_object *obj)
{
    struct nemu_bo *nemu_bo = gem_to_nemu_bo(obj);

    LogFunc(("nemuvideo: %d: nemu_bo=%p\n", __LINE__, nemu_bo));
    if (!nemu_bo)
        return;
    nemu_bo_unref(&nemu_bo);
}


static inline u64 nemu_bo_mmap_offset(struct nemu_bo *bo)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
    return bo->bo.addr_space_offset;
#else
    return drm_vma_node_offset_addr(&bo->bo.vma_node);
#endif
}
int
nemu_dumb_mmap_offset(struct drm_file *file,
             struct drm_device *dev,
             uint32_t handle,
             uint64_t *offset)
{
    struct drm_gem_object *obj;
    int ret;
    struct nemu_bo *bo = NULL;

    LogFunc(("nemuvideo: %d: dev=%p, handle=%u\n", __LINE__,
             dev, (unsigned)handle));
    mutex_lock(&dev->struct_mutex);
    obj = drm_gem_object_lookup(dev, file, handle);
    if (obj == NULL)
    {
        ret = -ENOENT;
        goto out_unlock;
    }

    bo = gem_to_nemu_bo(obj);
    *offset = nemu_bo_mmap_offset(bo);

    drm_gem_object_unreference(obj);
    ret = 0;
out_unlock:
    mutex_unlock(&dev->struct_mutex);
    LogFunc(("nemuvideo: %d: bo=%p, offset=%llu\n", __LINE__,
             bo, (unsigned long long)*offset));
    return ret;

}
