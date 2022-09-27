/* $Id: nemu_mode.c $ */
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
 * ast_mode.c
 * with the following copyright and permission notice:
 *
 * Copyright 2012 Red Hat Inc.
 * Parts based on xf86-video-nemu
 * Copyright (c) 2005 ASPEED Technology Inc.
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

#include <Nemu/NemuVideo.h>

#include <linux/export.h>
#include <drm/drm_crtc_helper.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
# include <drm/drm_plane_helper.h>
#endif

static int nemu_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv,
                            uint32_t handle, uint32_t width, uint32_t height,
                            int32_t hot_x, int32_t hot_y);
static int nemu_cursor_move(struct drm_crtc *crtc, int x, int y);

/** Set a graphics mode.  Poke any required values into registers, do an HGSMI
 * mode set and tell the host we support advanced graphics functions.
 */
static void nemu_do_modeset(struct drm_crtc *crtc,
                            const struct drm_display_mode *mode)
{
    struct nemu_crtc   *nemu_crtc = to_nemu_crtc(crtc);
    struct nemu_private *nemu;
    int width, height, cBPP, pitch;
    unsigned iCrtc;
    uint16_t fFlags;

    LogFunc(("nemuvideo: %d: nemu_crtc=%p, CRTC_FB(crtc)=%p\n", __LINE__,
             nemu_crtc, CRTC_FB(crtc)));
    nemu = crtc->dev->dev_private;
    width = mode->hdisplay ? mode->hdisplay : 640;
    height = mode->vdisplay ? mode->vdisplay : 480;
    iCrtc = nemu_crtc->crtc_id;
    cBPP = crtc->enabled ? CRTC_FB(crtc)->bits_per_pixel : 32;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
    pitch = crtc->enabled ? CRTC_FB(crtc)->pitch : width * cBPP / 8;
#else
    pitch = crtc->enabled ? CRTC_FB(crtc)->pitches[0] : width * cBPP / 8;
#endif
    /* if (nemu_crtc->crtc_id == 0 && crtc->enabled)
        NemuVideoSetModeRegisters(width, height, pitch * 8 / cBPP,
                                  CRTC_FB(crtc)->bits_per_pixel, 0,
                                  crtc->x, crtc->y); */
    fFlags = VBVA_SCREEN_F_ACTIVE;
    fFlags |= (crtc->enabled ? 0 : VBVA_SCREEN_F_DISABLED);
    NemuHGSMIProcessDisplayInfo(&nemu->Ctx, nemu_crtc->crtc_id,
                                crtc->x, crtc->y,
                                crtc->x * cBPP / 8 + crtc->y * pitch,
                                pitch, width, height,
                                nemu_crtc->fBlanked ? 0 : cBPP, fFlags);
    LogFunc(("nemuvideo: %d\n", __LINE__));
}

static int nemu_set_view(struct drm_crtc *crtc)
{
    struct nemu_crtc   *nemu_crtc = to_nemu_crtc(crtc);
    struct nemu_private *nemu = crtc->dev->dev_private;
    void *p;

    LogFunc(("nemuvideo: %d: nemu_crtc=%p\n", __LINE__, nemu_crtc));
    /* Tell the host about the view.  This design originally targeted the
     * Windows XP driver architecture and assumed that each screen would have
     * a dedicated frame buffer with the command buffer following it, the whole
     * being a "view".  The host works out which screen a command buffer belongs
     * to by checking whether it is in the first view, then whether it is in the
     * second and so on.  The first match wins.  We cheat around this by making
     * the first view be the managed memory plus the first command buffer, the
     * second the same plus the second buffer and so on. */
    p = NemuHGSMIBufferAlloc(&nemu->Ctx, sizeof(VBVAINFOVIEW), HGSMI_CH_VBVA,
                             VBVA_INFO_VIEW);
    if (p)
    {
        VBVAINFOVIEW *pInfo = (VBVAINFOVIEW *)p;
        pInfo->u32ViewIndex = nemu_crtc->crtc_id;
        pInfo->u32ViewOffset = nemu_crtc->offFB;
        pInfo->u32ViewSize =   nemu->vram_size - nemu_crtc->offFB
                             + nemu_crtc->crtc_id * VBVA_MIN_BUFFER_SIZE;
        pInfo->u32MaxScreenSize = nemu->vram_size - nemu_crtc->offFB;
        NemuHGSMIBufferSubmit(&nemu->Ctx, p);
        NemuHGSMIBufferFree(&nemu->Ctx, p);
    }
    else
        return -ENOMEM;
    LogFunc(("nemuvideo: %d: p=%p\n", __LINE__, p));
    return 0;
}

static void nemu_crtc_load_lut(struct drm_crtc *crtc)
{

}

static void nemu_crtc_dpms(struct drm_crtc *crtc, int mode)
{
    struct nemu_crtc *nemu_crtc = to_nemu_crtc(crtc);
    struct nemu_private *nemu = crtc->dev->dev_private;
    unsigned long flags;

    LogFunc(("nemuvideo: %d: nemu_crtc=%p, mode=%d\n", __LINE__, nemu_crtc,
             mode));
    switch (mode)
    {
    case DRM_MODE_DPMS_ON:
        nemu_crtc->fBlanked = false;
        break;
    case DRM_MODE_DPMS_STANDBY:
    case DRM_MODE_DPMS_SUSPEND:
    case DRM_MODE_DPMS_OFF:
        nemu_crtc->fBlanked = true;
        break;
    }
    spin_lock_irqsave(&nemu->dev_lock, flags);
    nemu_do_modeset(crtc, &crtc->hwmode);
    spin_unlock_irqrestore(&nemu->dev_lock, flags);
    LogFunc(("nemuvideo: %d\n", __LINE__));
}

static bool nemu_crtc_mode_fixup(struct drm_crtc *crtc,
                const struct drm_display_mode *mode,
                struct drm_display_mode *adjusted_mode)
{
    return true;
}

static int nemu_crtc_do_set_base(struct drm_crtc *crtc,
                struct drm_framebuffer *fb,
                int x, int y, int atomic)
{
    struct nemu_private *nemu = crtc->dev->dev_private;
    struct nemu_crtc *nemu_crtc = to_nemu_crtc(crtc);
    struct drm_gem_object *obj;
    struct nemu_framebuffer *nemu_fb;
    struct nemu_bo *bo;
    int ret;
    u64 gpu_addr;

    LogFunc(("nemuvideo: %d: fb=%p, nemu_crtc=%p\n", __LINE__, fb, nemu_crtc));

    nemu_fb = to_nemu_framebuffer(CRTC_FB(crtc));
    obj = nemu_fb->obj;
    bo = gem_to_nemu_bo(obj);

    ret = nemu_bo_reserve(bo, false);
    if (ret)
        return ret;

    ret = nemu_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
    if (ret)
    {
        nemu_bo_unreserve(bo);
        return ret;
    }

    if (&nemu->fbdev->afb == nemu_fb)
    {
        /* if pushing console in kmap it */
        ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
        if (ret)
            DRM_ERROR("failed to kmap fbcon\n");
    }
    nemu_bo_unreserve(bo);

    /* nemu_set_start_address_crt1(crtc, (u32)gpu_addr); */
    nemu_crtc->offFB = gpu_addr;

    LogFunc(("nemuvideo: %d: nemu_fb=%p, obj=%p, bo=%p, gpu_addr=%u\n",
             __LINE__, nemu_fb, obj, bo, (unsigned)gpu_addr));
    return 0;
}

static int nemu_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
                 struct drm_framebuffer *old_fb)
{
    LogFunc(("nemuvideo: %d\n", __LINE__));
    return nemu_crtc_do_set_base(crtc, old_fb, x, y, 0);
}

static int nemu_crtc_mode_set(struct drm_crtc *crtc,
                 struct drm_display_mode *mode,
                 struct drm_display_mode *adjusted_mode,
                 int x, int y,
                 struct drm_framebuffer *old_fb)
{
    struct nemu_private *nemu = crtc->dev->dev_private;
    unsigned long flags;
    int rc = 0;

    LogFunc(("nemuvideo: %d: nemu=%p\n", __LINE__, nemu));
    nemu_crtc_mode_set_base(crtc, x, y, old_fb);
    spin_lock_irqsave(&nemu->dev_lock, flags);
    rc = nemu_set_view(crtc);
    if (!rc)
        nemu_do_modeset(crtc, mode);
    spin_unlock_irqrestore(&nemu->dev_lock, flags);
    LogFunc(("nemuvideo: %d\n", __LINE__));
    return rc;
}

static void nemu_crtc_disable(struct drm_crtc *crtc)
{

}

static void nemu_crtc_prepare(struct drm_crtc *crtc)
{

}

static void nemu_crtc_commit(struct drm_crtc *crtc)
{

}


static const struct drm_crtc_helper_funcs nemu_crtc_helper_funcs =
{
    .dpms = nemu_crtc_dpms,
    .mode_fixup = nemu_crtc_mode_fixup,
    .mode_set = nemu_crtc_mode_set,
    /* .mode_set_base = nemu_crtc_mode_set_base, */
    .disable = nemu_crtc_disable,
    .load_lut = nemu_crtc_load_lut,
    .prepare = nemu_crtc_prepare,
    .commit = nemu_crtc_commit,

};

static void nemu_crtc_reset(struct drm_crtc *crtc)
{

}


static void nemu_crtc_destroy(struct drm_crtc *crtc)
{
    drm_crtc_cleanup(crtc);
    kfree(crtc);
}

static const struct drm_crtc_funcs nemu_crtc_funcs =
{
    .cursor_move = nemu_cursor_move,
#ifdef DRM_IOCTL_MODE_CURSOR2
    .cursor_set2 = nemu_cursor_set2,
#endif
    .reset = nemu_crtc_reset,
    .set_config = drm_crtc_helper_set_config,
    /* .gamma_set = nemu_crtc_gamma_set, */
    .destroy = nemu_crtc_destroy,
};

int nemu_crtc_init(struct drm_device *pDev, unsigned i)
{
    struct nemu_crtc *pCrtc;

    LogFunc(("nemuvideo: %d\n", __LINE__));
    pCrtc = kzalloc(sizeof(struct nemu_crtc), GFP_KERNEL);
    if (!pCrtc)
        return -ENOMEM;
    pCrtc->crtc_id = i;

    drm_crtc_init(pDev, &pCrtc->base, &nemu_crtc_funcs);
    drm_mode_crtc_set_gamma_size(&pCrtc->base, 256);
    drm_crtc_helper_add(&pCrtc->base, &nemu_crtc_helper_funcs);
    LogFunc(("nemuvideo: %d: pCrtc=%p\n", __LINE__, pCrtc));

    return 0;
}


static void nemu_encoder_destroy(struct drm_encoder *encoder)
{
    LogFunc(("nemuvideo: %d: encoder=%p\n", __LINE__, encoder));
    drm_encoder_cleanup(encoder);
    kfree(encoder);
}


static struct drm_encoder *nemu_best_single_encoder(struct drm_connector *connector)
{
    int enc_id = connector->encoder_ids[0];
    struct drm_mode_object *obj;
    struct drm_encoder *encoder;

    LogFunc(("nemuvideo: %d: connector=%p\n", __LINE__, connector));
    /* pick the encoder ids */
    if (enc_id)
    {
        obj = drm_mode_object_find(connector->dev, enc_id, DRM_MODE_OBJECT_ENCODER);
        if (!obj)
            return NULL;
        encoder = obj_to_encoder(obj);
        LogFunc(("nemuvideo: %d: encoder=%p\n", __LINE__, encoder));
        return encoder;
    }
    LogFunc(("nemuvideo: %d\n", __LINE__));
    return NULL;
}


static const struct drm_encoder_funcs nemu_enc_funcs =
{
    .destroy = nemu_encoder_destroy,
};

static void nemu_encoder_dpms(struct drm_encoder *encoder, int mode)
{

}

static bool nemu_mode_fixup(struct drm_encoder *encoder,
               const struct drm_display_mode *mode,
               struct drm_display_mode *adjusted_mode)
{
    return true;
}

static void nemu_encoder_mode_set(struct drm_encoder *encoder,
                   struct drm_display_mode *mode,
                   struct drm_display_mode *adjusted_mode)
{
}

static void nemu_encoder_prepare(struct drm_encoder *encoder)
{

}

static void nemu_encoder_commit(struct drm_encoder *encoder)
{

}


static const struct drm_encoder_helper_funcs nemu_enc_helper_funcs =
{
    .dpms = nemu_encoder_dpms,
    .mode_fixup = nemu_mode_fixup,
    .prepare = nemu_encoder_prepare,
    .commit = nemu_encoder_commit,
    .mode_set = nemu_encoder_mode_set,
};

struct drm_encoder *nemu_encoder_init(struct drm_device *dev, unsigned i)
{
    struct nemu_encoder *nemu_encoder;

    LogFunc(("nemuvideo: %d: dev=%d\n", __LINE__));
    nemu_encoder = kzalloc(sizeof(struct nemu_encoder), GFP_KERNEL);
    if (!nemu_encoder)
        return NULL;

    drm_encoder_init(dev, &nemu_encoder->base, &nemu_enc_funcs,
             DRM_MODE_ENCODER_DAC);
    drm_encoder_helper_add(&nemu_encoder->base, &nemu_enc_helper_funcs);

    nemu_encoder->base.possible_crtcs = 1 << i;
    LogFunc(("nemuvideo: %d: nemu_encoder=%p\n", __LINE__, nemu_encoder));
    return &nemu_encoder->base;
}

static int nemu_get_modes(struct drm_connector *pConnector)
{
    struct nemu_connector *pNemuConnector = NULL;
    struct drm_display_mode *pMode = NULL;
    unsigned cModes = 0;

    LogFunc(("nemuvideo: %d: pConnector=%p\n", __LINE__, pConnector));
    pNemuConnector = to_nemu_connector(pConnector);
    cModes = drm_add_modes_noedid(pConnector, 1024, 768);
    if (pNemuConnector->modeHint.cX && pNemuConnector->modeHint.cY)
    {
        pMode = drm_cvt_mode(pConnector->dev, pNemuConnector->modeHint.cX,
                             pNemuConnector->modeHint.cY, 60, false, false,
                             false);
        if (pMode)
        {
            pMode->type |= DRM_MODE_TYPE_PREFERRED;
            drm_mode_probed_add(pConnector, pMode);
            ++cModes;
        }
    }
    return cModes;
}

static int nemu_mode_valid(struct drm_connector *connector,
              struct drm_display_mode *mode)
{
    return MODE_OK;
}

static void nemu_connector_destroy(struct drm_connector *pConnector)
{
    struct nemu_connector *pNemuConnector = NULL;

    LogFunc(("nemuvideo: %d: connector=%p\n", __LINE__, pConnector));
    pNemuConnector = to_nemu_connector(pConnector);
    device_remove_file(pConnector->dev->dev, &pNemuConnector->deviceAttribute);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
    drm_sysfs_connector_remove(pConnector);
#else
    drm_connector_unregister(pConnector);
#endif
    drm_connector_cleanup(pConnector);
    kfree(pConnector);
}

static enum drm_connector_status
nemu_connector_detect(struct drm_connector *connector, bool force)
{
    return connector_status_connected;
}

static const struct drm_connector_helper_funcs nemu_connector_helper_funcs =
{
    .mode_valid = nemu_mode_valid,
    .get_modes = nemu_get_modes,
    .best_encoder = nemu_best_single_encoder,
};

static const struct drm_connector_funcs nemu_connector_funcs =
{
    .dpms = drm_helper_connector_dpms,
    .detect = nemu_connector_detect,
    .fill_modes = drm_helper_probe_single_connector_modes,
    .destroy = nemu_connector_destroy,
};

ssize_t nemu_connector_write_sysfs(struct device *pDev,
                                   struct device_attribute *pAttr,
                                   const char *psz, size_t cch)
{
    struct nemu_connector *pNemuConnector;
    struct drm_device *pDrmDev;
    struct nemu_private *pNemu;
    int cX, cY;
    char ch;

    LogFunc(("nemuvideo: %d: pDev=%p, pAttr=%p, psz=%s, cch=%llu\n", __LINE__,
             pDev, pAttr, psz, (unsigned long long)cch));
    pNemuConnector = container_of(pAttr, struct nemu_connector,
                                  deviceAttribute);
    pDrmDev = pNemuConnector->base.dev;
    pNemu = pDrmDev->dev_private;
    if (sscanf(psz, "%5dx%5d\n%c", &cX, &cY, &ch) != 2)
        return -EINVAL;
    if (   cX < 64 || cX > VBE_DISPI_MAX_XRES
        || cY < 64 || cY > VBE_DISPI_MAX_YRES)
        return -EINVAL;
    pNemuConnector->modeHint.cX = (uint16_t)cX;
    pNemuConnector->modeHint.cY = (uint16_t)cY;
    drm_helper_hpd_irq_event(pNemuConnector->base.dev);
    if (pNemu->fbdev)
        drm_fb_helper_hotplug_event(&pNemu->fbdev->helper);
    LogFunc(("nemuvideo: %d\n", __LINE__));
    return cch;
}

int nemu_connector_init(struct drm_device *pDev, unsigned cScreen,
                        struct drm_encoder *pEncoder)
{
    struct nemu_connector *pNemuConnector;
    struct drm_connector *pConnector;
    int rc;

    LogFunc(("nemuvideo: %d: pDev=%p, pEncoder=%p\n", __LINE__, pDev,
             pEncoder));
    pNemuConnector = kzalloc(sizeof(struct nemu_connector), GFP_KERNEL);
    if (!pNemuConnector)
        return -ENOMEM;

    pConnector = &pNemuConnector->base;

    /*
     * Set up the sysfs file we use for getting video mode hints from user
     * space.
     */
    snprintf(pNemuConnector->szName, sizeof(pNemuConnector->szName),
             "nemu_screen_%u", cScreen);
    pNemuConnector->deviceAttribute.attr.name = pNemuConnector->szName;
    pNemuConnector->deviceAttribute.attr.mode = S_IWUSR;
    pNemuConnector->deviceAttribute.show      = NULL;
    pNemuConnector->deviceAttribute.store     = nemu_connector_write_sysfs;
    rc = device_create_file(pDev->dev, &pNemuConnector->deviceAttribute);
    if (rc < 0)
    {
        kfree(pNemuConnector);
        return rc;
    }
    drm_connector_init(pDev, pConnector, &nemu_connector_funcs,
                       DRM_MODE_CONNECTOR_VGA);
    drm_connector_helper_add(pConnector, &nemu_connector_helper_funcs);

    pConnector->interlace_allowed = 0;
    pConnector->doublescan_allowed = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
    drm_sysfs_connector_add(pConnector);
#else
    drm_connector_register(pConnector);
#endif

    /* The connector supports hot-plug detection: we promise to call
     * "drm_helper_hpd_irq_event" when hot-plugging occurs. */
    pConnector->polled = DRM_CONNECTOR_POLL_HPD;

    drm_mode_connector_attach_encoder(pConnector, pEncoder);

    LogFunc(("nemuvideo: %d: pConnector=%p\n", __LINE__, pConnector));
    return 0;
}

#if 0
/* allocate cursor cache and pin at start of VRAM */
int nemu_cursor_init(struct drm_device *dev)
{
    struct nemu_private *nemu = dev->dev_private;
    int size;
    int ret;
    struct drm_gem_object *obj;
    struct nemu_bo *bo;
    uint64_t gpu_addr;

    size = (AST_HWC_SIZE + AST_HWC_SIGNATURE_SIZE) * AST_DEFAULT_HWC_NUM;

    ret = nemu_gem_create(dev, size, true, &obj);
    if (ret)
        return ret;
    bo = gem_to_nemu_bo(obj);
    ret = nemu_bo_reserve(bo, false);
    if (unlikely(ret != 0))
        goto fail;

    ret = nemu_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
    nemu_bo_unreserve(bo);
    if (ret)
        goto fail;

    /* kmap the object */
    ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &nemu->cache_kmap);
    if (ret)
        goto fail;

    nemu->cursor_cache = obj;
    nemu->cursor_cache_gpu_addr = gpu_addr;
    DRM_DEBUG_KMS("pinned cursor cache at %llx\n", nemu->cursor_cache_gpu_addr);
    return 0;
fail:
    return ret;
}

void nemu_cursor_fini(struct drm_device *dev)
{
    struct nemu_private *nemu = dev->dev_private;
    ttm_bo_kunmap(&nemu->cache_kmap);
    drm_gem_object_unreference_unlocked(nemu->cursor_cache);
}
#endif

int nemu_mode_init(struct drm_device *pDev)
{
    struct nemu_private *pNemu = pDev->dev_private;
    struct drm_encoder *pEncoder;
    unsigned i;
    /* nemu_cursor_init(dev); */
    LogFunc(("nemuvideo: %d: pDev=%p\n", __LINE__, pDev));
    for (i = 0; i < pNemu->cCrtcs; ++i)
    {
        nemu_crtc_init(pDev, i);
        pEncoder = nemu_encoder_init(pDev, i);
        if (pEncoder)
            nemu_connector_init(pDev, i, pEncoder);
    }
    return 0;
}

void nemu_mode_fini(struct drm_device *dev)
{
    /* nemu_cursor_fini(dev); */
}


void NemuRefreshModes(struct drm_device *pDev)
{
    struct nemu_private *nemu = pDev->dev_private;
    struct drm_crtc *crtci;
    unsigned long flags;

    LogFunc(("nemuvideo: %d\n", __LINE__));
    spin_lock_irqsave(&nemu->dev_lock, flags);
    list_for_each_entry(crtci, &pDev->mode_config.crtc_list, head)
        nemu_do_modeset(crtci, &crtci->hwmode);
    spin_unlock_irqrestore(&nemu->dev_lock, flags);
    LogFunc(("nemuvideo: %d\n", __LINE__));
}


/** Copy the ARGB image and generate the mask, which is needed in case the host
 *  does not support ARGB cursors.  The mask is a 1BPP bitmap with the bit set
 *  if the corresponding alpha value in the ARGB image is greater than 0xF0. */
static void copy_cursor_image(u8 *src, u8 *dst, int width, int height,
                              size_t cbMask)
{
    unsigned i, j;
    size_t cbLine = (width + 7) / 8;

    memcpy(dst + cbMask, src, width * height * 4);
    for (i = 0; i < height; ++i)
        for (j = 0; j < width; ++j)
            if (((uint32_t *)src)[i * width + j] > 0xf0000000)
                dst[i * cbLine + j / 8] |= (0x80 >> (j % 8));
}

static int nemu_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv,
                            uint32_t handle, uint32_t width, uint32_t height,
                            int32_t hot_x, int32_t hot_y)
{
    struct nemu_private *nemu = crtc->dev->dev_private;
    struct nemu_crtc *nemu_crtc = to_nemu_crtc(crtc);
    struct drm_gem_object *obj;
    struct nemu_bo *bo;
    int ret, rc;
    struct ttm_bo_kmap_obj uobj_map;
    u8 *src;
    u8 *dst = NULL;
    size_t cbData, cbMask;
    bool src_isiomem;

    if (!handle) {
        /* Hide cursor. */
        NemuHGSMIUpdatePointerShape(&nemu->Ctx, 0, 0, 0, 0, 0, NULL, 0);
        return 0;
    }
    if (   width > NEMU_MAX_CURSOR_WIDTH || height > NEMU_MAX_CURSOR_HEIGHT
        || width == 0 || hot_x > width || height == 0 || hot_y > height)
        return -EINVAL;

    obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
    if (obj)
    {
        bo = gem_to_nemu_bo(obj);
        ret = nemu_bo_reserve(bo, false);
        if (!ret)
        {
            /* The mask must be calculated based on the alpha channel, one bit
             * per ARGB word, and must be 32-bit padded. */
            cbMask  = ((width + 7) / 8 * height + 3) & ~3;
            cbData = width * height * 4 + cbMask;
            dst = kmalloc(cbData, GFP_KERNEL);
            if (dst)
            {
                ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &uobj_map);
                if (!ret)
                {
                    src = ttm_kmap_obj_virtual(&uobj_map, &src_isiomem);
                    if (!src_isiomem)
                    {
                        uint32_t fFlags =   NEMU_MOUSE_POINTER_VISIBLE
                                          | NEMU_MOUSE_POINTER_SHAPE
                                          | NEMU_MOUSE_POINTER_ALPHA;
                        copy_cursor_image(src, dst, width, height, cbMask);
                        rc = NemuHGSMIUpdatePointerShape(&nemu->Ctx, fFlags,
                                                         hot_x, hot_y, width,
                                                         height, dst, cbData);
                        ret = RTErrConvertToErrno(rc);
                    }
                    else
                        DRM_ERROR("src cursor bo should be in main memory\n");
                    ttm_bo_kunmap(&uobj_map);
                }
                kfree(dst);
            }
            nemu_bo_unreserve(bo);
        }
        drm_gem_object_unreference_unlocked(obj);
    }
    else
    {
        DRM_ERROR("Cannot find cursor object %x for crtc\n", handle);
        ret = -ENOENT;
    }
    return ret;
}

static int nemu_cursor_move(struct drm_crtc *crtc,
               int x, int y)
{
    return 0;
}
