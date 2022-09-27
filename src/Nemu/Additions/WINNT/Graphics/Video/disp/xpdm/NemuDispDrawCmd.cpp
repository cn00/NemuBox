/* $Id: NemuDispDrawCmd.cpp $ */

/** @file
 * Nemu XPDM Display driver drawing interface functions
 */

/*
 * Copyright (C) 2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* The driver operates in 3 modes:
 * 1) BASE        : Driver does not report to host about any operations.
 *                  All Drv* are immediately routed to Eng*.
 * 2) VBVA        : Driver reports dirty rectangles to host.
 * 3) VBVA + VRDP : Driver also creates orders pipeline from which VRDP
 *                  can reconstruct all drawing operations, including
 *                  bitmap updates.
 *
 * These modes affect only NemuDispDrv* functions in this file.
 *
 * VBVA mode is enabled by a registry key for the miniport driver
 * (as it is implemented now).
 *
 * VRDP mode is enabled when a VRDP client connects and VBVA is enabled.
 * The host sets a bit flag in VBVAMemory when VRDP client is connected.
 *
 * The VRDP mode pipeline consists of 3 types of commands:
 *
 * 1) RDP orders: BitBlt, RectFill, Text.
 *        These are the simplest ones.
 *
 * 2) Caching: Bitmap, glyph, brush.
 *        The driver maintains a bitmap (or other objects) cache.
 *        All source bitmaps are cached. The driver verifies
 *        iUniq and also computes CRC for these bitmaps
 *        for searching. The driver will use SURFOBJ::dhsurf
 *        field to save a pointer to in driver structure, even
 *        for Engine managed bitmaps (hope that will work).
 *
 *
 * 3) Bitmap updates, when given draw operation can not be done
 *    using orders.
 *
 */

#include "NemuDisp.h"
#include "NemuDispDrawCmd.h"

typedef struct _NEMUDISPCALLSTATS
{
    ULONG NemuDispDrvLineTo;
    ULONG NemuDispDrvStrokePath;
    ULONG NemuDispDrvFillPath;
    ULONG NemuDispDrvTextOut;
    ULONG NemuDispDrvPaint;
    ULONG NemuDispDrvSaveScreenBits;
    ULONG NemuDispDrvBitBlt;
    ULONG NemuDispDrvStretchBlt;
    ULONG NemuDispDrvCopyBits;
} NEMUDISPCALLSTATS;

static NEMUDISPCALLSTATS gDispCallStats = {0,0,0,0,0,0,0,0,0};

#ifdef STAT_sunlover
# define STATDRVENTRY(a, b) do {if (NemuDispIsScreenSurface(b)) gDispCallStats.NemuDispDrv##a++; } while (0)
# define STATPRINT do {NemuDispPrintStats();} while (0)
# define DUMPSURF(_s, _name) NemuDispDumpPSO(_s, _name);
#else
# define STATDRVENTRY(a, b)
# define STATPRINT
# define DUMPSURF(_s, _name)
#endif

#define VBVA_OPERATION(__psoDest, __fn, __a) do {                            \
    if (NemuDispIsScreenSurface(__psoDest))                                  \
    {                                                                        \
        PNEMUDISPDEV pDev = (PNEMUDISPDEV)__psoDest->dhpdev;                 \
                                                                             \
        if (   pDev->hgsmi.bSupported                                        \
            && NemuVBVABufferBeginUpdate(&pDev->vbvaCtx, &pDev->hgsmi.ctx))  \
        {                                                                    \
            vbvaDrv##__fn __a;                                               \
                                                                             \
            if (  pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents               \
                & NEMU_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET)                  \
            {                                                                \
                vrdpReset(pDev);                                             \
                                                                             \
                pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents &=              \
                          ~NEMU_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET;         \
            }                                                                \
                                                                             \
            if (pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents                 \
                & VBVA_F_MODE_VRDP)                                          \
            {                                                                \
                vrdpDrv##__fn __a;                                           \
            }                                                                \
                                                                             \
            NemuVBVABufferEndUpdate(&pDev->vbvaCtx);                         \
        }                                                                    \
    }                                                                        \
} while (0)

BOOL NemuDispIsScreenSurface(SURFOBJ *pso)
{
    if (pso)
    {
        PNEMUDISPDEV pDev = (PNEMUDISPDEV)pso->dhpdev;

        /* The screen surface has the 'pso->dhpdev' field,
         * and is either the screen device surface with handle = hsurfScreen,
         * or a surface derived from DDRAW with address equal to the framebuffer.
         */
        if (pDev && (pso->hsurf == pDev->surface.hSurface || pso->pvBits == pDev->memInfo.FrameBufferBase))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static void NemuDispPrintStats(void)
{
    LOG(("LineTo = %u\n",
         "StrokePath = %u\n",
         "FillPath = %u\n",
         "TextOut = %u\n",
         "DrvPaint = %u\n",
         "SaveScreenBits = %u\n",
         "BitBlt = %u\n",
         "StretchBlt = %u\n",
         "CopyBits = %u",
         gDispCallStats.NemuDispDrvLineTo,
         gDispCallStats.NemuDispDrvStrokePath,
         gDispCallStats.NemuDispDrvFillPath,
         gDispCallStats.NemuDispDrvTextOut,
         gDispCallStats.NemuDispDrvPaint,
         gDispCallStats.NemuDispDrvSaveScreenBits,
         gDispCallStats.NemuDispDrvBitBlt,
         gDispCallStats.NemuDispDrvStretchBlt,
         gDispCallStats.NemuDispDrvCopyBits
       ));
}

void NemuDispDumpPSO(SURFOBJ *pso, char *s)
{
    if (pso)
    {
        LOG(("Surface %s: %p\n"
             "    DHSURF  dhsurf        = %p\n"
             "    HSURF   hsurf         = %p\n"
             "    DHPDEV  dhpdev        = %p\n"
             "    HDEV    hdev          = %p\n"
             "    SIZEL   sizlBitmap    = %dx%d\n"
             "    ULONG   cjBits        = %p\n"
             "    PVOID   pvBits        = %p\n"
             "    PVOID   pvScan0       = %p\n"
             "    LONG    lDelta        = %p\n"
             "    ULONG   iUniq         = %p\n"
             "    ULONG   iBitmapFormat = %p\n"
             "    USHORT  iType         = %p\n"
             "    USHORT  fjBitmap      = %p",
             s, pso, pso->dhsurf, pso->hsurf, pso->dhpdev, pso->hdev,
             pso->sizlBitmap.cx, pso->sizlBitmap.cy, pso->cjBits, pso->pvBits,
             pso->pvScan0, pso->lDelta, pso->iUniq, pso->iBitmapFormat, pso->iType, pso->fjBitmap));
    }
    else
    {
        LOG(("Surface %s: %p", s, pso));
    }
}

static void ssbDiscardTopSlot(PNEMUDISPDEV pDev)
{
    SSB *pSSB = &pDev->aSSB[--pDev->cSSB];

    if (pSSB->pBuffer)
    {
        EngFreeMem (pSSB->pBuffer);
        pSSB->pBuffer = NULL;
    }

    pSSB->ident = 0;
}

static void ssbDiscardUpperSlots(PNEMUDISPDEV pDev, ULONG_PTR ident)
{
    while (pDev->cSSB > ident)
    {
        ssbDiscardTopSlot (pDev);
    }
}

static BOOL ssbCopy(SSB *pSSB, SURFOBJ *pso, RECTL *prcl, BOOL bToScreen)
{
    BYTE *pSrc;
    BYTE *pDst;

    LONG lDeltaSrc;
    LONG lDeltaDst;

    ULONG cWidth;
    ULONG cHeight;

    int cbPixel = format2BytesPerPixel(pso);

    LOGF(("pSSB = %p, pso = %p, prcl = %p, bToScreen = %d", pSSB, pso, prcl, bToScreen));

    if (cbPixel == 0)
    {
        WARN(("unsupported pixel format!!!"));
        return FALSE;
    }

    cWidth  = prcl->right - prcl->left;
    cHeight = prcl->bottom - prcl->top;

    if (bToScreen)
    {
        if (pSSB->pBuffer == NULL)
        {
            WARN(("source buffer is NULL!!!"));
            return FALSE;
        }

        pSrc = pSSB->pBuffer;
        lDeltaSrc = cWidth * cbPixel;

        pDst = (BYTE *)pso->pvScan0 +
                       pso->lDelta * prcl->top +
                       cbPixel * prcl->left;
        lDeltaDst  = pso->lDelta;
    }
    else
    {
        if (pSSB->pBuffer != NULL)
        {
            WARN(("source buffer is not NULL!!!"));
            return FALSE;
        }

        pSSB->pBuffer = (BYTE *)EngAllocMem (0, cWidth * cHeight * cbPixel, MEM_ALLOC_TAG);

        if (pSSB->pBuffer == NULL)
        {
            WARN(("Failed to allocate buffer!!!"));
            return FALSE;
        }

        pDst = pSSB->pBuffer;
        lDeltaDst = cWidth * cbPixel;

        pSrc = (BYTE *)pso->pvScan0 +
                       pso->lDelta * prcl->top +
                       cbPixel * prcl->left;
        lDeltaSrc  = pso->lDelta;
    }

    LOG(("cHeight = %d, pDst = %p, pSrc = %p, lDeltaSrc = %d, lDeltaDst = %d",
         cHeight, pDst, pSrc, lDeltaSrc, lDeltaDst));

    while (cHeight--)
    {
        memcpy (pDst, pSrc, cWidth * cbPixel);

        pDst += lDeltaDst;
        pSrc += lDeltaSrc;
    }

    LOGF(("completed."));
    return TRUE;
}

/*
 * Display driver callbacks.
 */

BOOL APIENTRY
NemuDispDrvLineTo(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo, LONG x1, LONG y1, LONG x2, LONG y2,
                  RECTL *prclBounds, MIX mix)
{
    BOOL bRc;
    LOGF_ENTER();
    STATDRVENTRY(LineTo, pso);

    bRc = EngLineTo(getSurfObj(pso), pco, pbo, x1, y1, x2, y2, prclBounds, mix);
    VBVA_OPERATION(pso, LineTo, (pso, pco, pbo, x1, y1, x2, y2, prclBounds, mix));

    LOGF_LEAVE();
    return bRc;
}

BOOL APIENTRY
NemuDispDrvStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, XFORMOBJ *pxo,
                      BRUSHOBJ  *pbo, POINTL *pptlBrushOrg, LINEATTRS *plineattrs, MIX mix)
{
    BOOL bRc;
    LOGF_ENTER();
    STATDRVENTRY(StrokePath, pso);

    bRc = EngStrokePath(getSurfObj(pso), ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix);
    VBVA_OPERATION(pso, StrokePath, (pso, ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix));

    LOGF_LEAVE();
    return bRc;
}

BOOL APIENTRY
NemuDispDrvFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg,
                    MIX mix, FLONG flOptions)
{
    BOOL bRc;
    LOGF_ENTER();
    STATDRVENTRY(FillPath, pso);

    bRc = EngFillPath(getSurfObj(pso), ppo, pco, pbo, pptlBrushOrg, mix, flOptions);
    VBVA_OPERATION(pso, FillPath, (pso, ppo, pco, pbo, pptlBrushOrg, mix, flOptions));

    LOGF_LEAVE();
    return bRc;
}

BOOL APIENTRY NemuDispDrvPaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix)
{
    BOOL bRc;
    LOGF_ENTER();
    STATDRVENTRY(Paint, pso);

    bRc = EngPaint (getSurfObj(pso), pco, pbo, pptlBrushOrg, mix);
    VBVA_OPERATION(pso, Paint, (pso, pco, pbo, pptlBrushOrg, mix));

    LOGF_LEAVE();
    return bRc;
}

BOOL APIENTRY
NemuDispDrvTextOut(SURFOBJ *pso, STROBJ *pstro, FONTOBJ *pfo, CLIPOBJ *pco,
                   RECTL *prclExtra, RECTL *prclOpaque, BRUSHOBJ *pboFore,
                   BRUSHOBJ *pboOpaque, POINTL *pptlOrg, MIX mix)
{
    BOOL bRc;
    LOGF_ENTER();
    STATDRVENTRY(TextOut, pso);

    bRc = EngTextOut(getSurfObj(pso), pstro, pfo, pco, prclExtra, prclOpaque, pboFore, pboOpaque, pptlOrg, mix);
    VBVA_OPERATION(pso, TextOut, (pso, pstro, pfo, pco, prclExtra, prclOpaque, pboFore, pboOpaque, pptlOrg, mix));

    LOGF_LEAVE();
    return bRc;
}

ULONG_PTR APIENTRY NemuDispDrvSaveScreenBits(SURFOBJ *pso, ULONG iMode, ULONG_PTR ident, RECTL *prcl)
{
    ULONG_PTR rc = 0; /* 0 means the function failure for every iMode. */
    RECTL rcl;
    SSB *pSSB;
    SURFOBJ *psoOrg = pso;
    BOOL bCallVBVA = FALSE;

    PNEMUDISPDEV pDev = (PNEMUDISPDEV) pso->dhpdev;

    LOGF(("%p, %d, %d, %d,%d %d,%d", pso, iMode, ident, prcl->left, prcl->top, prcl->right, prcl->bottom));

    if (!pDev)
    {
        return rc;
    }

    pso = getSurfObj(pso);

    /* Order the rectangle. */
    if (prcl->left <= prcl->right)
    {
        rcl.left = prcl->left;
        rcl.right = prcl->right;
    }
    else
    {
        rcl.left = prcl->right;
        rcl.right = prcl->left;
    }

    if (prcl->top <= prcl->bottom)
    {
        rcl.top = prcl->top;
        rcl.bottom = prcl->bottom;
    }
    else
    {
        rcl.top = prcl->bottom;
        rcl.bottom = prcl->top;
    }

    /* Implementation of the save/restore is a bit complicated because RDP
     * requires "the sequencing of saves and restores is such that they
     * behave as a last-in, first-out stack.".
     */
    switch (iMode)
    {
        case SS_SAVE:
        {
            LOG(("SS_SAVE %d", pDev->cSSB));

            if (pDev->cSSB >= RT_ELEMENTS(pDev->aSSB))
            {
                /* All slots are already in use. Fail. */
                WARN(("no more slots %d!!!", pDev->cSSB));
                break;
            }

            /* Get pointer to the slot where bits will be saved. */
            pSSB = &pDev->aSSB[pDev->cSSB];

            /* Allocate memory for screen bits and copy them to the buffer. */
            if (ssbCopy(pSSB, pso, &rcl, FALSE /* bToScreen */))
            {
                /* Bits where successfully copied. Increase the active slot number
                 * and call VBVA levels, 'ident' is also assigned, the VBVA level
                 * will use it even for the SS_SAVE.
                 */
                ident = rc = pSSB->ident = ++pDev->cSSB;
                bCallVBVA = TRUE;
            }
        } break;

        case SS_RESTORE:
        {
            LOG(("SS_RESTORE"));

            if (pDev->cSSB == 0 || ident == 0 || ident > pDev->cSSB)
            {
                WARN(("no slot: pDev->cSSB = %d!!!", pDev->cSSB));
                break;
            }

            if (ident < pDev->cSSB)
            {
                ssbDiscardUpperSlots(pDev, ident);
            }

            Assert(ident == pDev->cSSB);
            Assert(ident != 0);

            pSSB = &pDev->aSSB[ident - 1];

            ssbCopy(pSSB, pso, &rcl, TRUE /* bToScreen */);

            /* Bits must be discarded. */
            ssbDiscardTopSlot (pDev);

            rc = TRUE;
            bCallVBVA = TRUE;
        } break;

        case SS_FREE:
        {
            LOG(("SS_FREE"));

            if (pDev->cSSB == 0 || ident == 0 || ident > pDev->cSSB)
            {
                WARN(("no slot: pDev->cSSB = %d!!!", pDev->cSSB));
                break;
            }

            if (ident < pDev->cSSB)
            {
                ssbDiscardUpperSlots(pDev, ident);
            }

            Assert(ident == pDev->cSSB);
            Assert(ident != 0);

            /* Bits must be discarded. */
            ssbDiscardTopSlot(pDev);

            rc = TRUE;
        } break;
    }

    /* Now call the VBVA/VRDP levels. */
    if (bCallVBVA)
    {
        LOG(("calling VBVA"));
        VBVA_OPERATION(psoOrg, SaveScreenBits, (psoOrg, iMode, ident, &rcl));
    }

    LOGF(("return %d", rc));
    return rc;
}

BOOL APIENTRY
NemuDispDrvBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                  RECTL *prclTrg, POINTL *pptlSrc, POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush, ROP4 rop4)
{
    BOOL bRc;
    LOGF_ENTER();
    STATDRVENTRY(BitBlt, psoTrg);

    LOG(("psoTrg = %p, psoSrc = %p, psoMask = %p, pco = %p, pxlo = %p, prclTrg = %p, pptlSrc = %p, "
         "pptlMask = %p, pbo = %p, pptlBrush = %p, rop4 = %08X",
         psoTrg, psoSrc, psoMask, pco, pxlo, prclTrg, pptlSrc, pptlMask, pbo, pptlBrush, rop4));

    bRc = EngBitBlt(getSurfObj(psoTrg), getSurfObj(psoSrc), psoMask, pco, pxlo, prclTrg, pptlSrc, pptlMask, pbo, pptlBrush, rop4);
    VBVA_OPERATION(psoTrg, BitBlt,
                   (psoTrg, psoSrc, psoMask, pco, pxlo, prclTrg, pptlSrc, pptlMask, pbo, pptlBrush, rop4));

    LOGF_LEAVE();
    return bRc;
}

BOOL APIENTRY
NemuDispDrvStretchBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                      COLORADJUSTMENT *pca, POINTL *pptlHTOrg, RECTL *prclDest, RECTL *prclSrc,
                      POINTL *pptlMask, ULONG iMode)
{
    BOOL bRc;
    LOGF_ENTER();
    STATDRVENTRY(StretchBlt, psoDest);

    bRc = EngStretchBlt(getSurfObj(psoDest), getSurfObj(psoSrc), psoMask, pco, pxlo, pca, pptlHTOrg,
                        prclDest, prclSrc, pptlMask, iMode);
    VBVA_OPERATION(psoDest, StretchBlt,
                   (psoDest, psoSrc, psoMask, pco, pxlo, pca, pptlHTOrg, prclDest, prclSrc, pptlMask, iMode));

    LOGF_LEAVE();
    return bRc;
}

BOOL APIENTRY
NemuDispDrvCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc, CLIPOBJ *pco, XLATEOBJ *pxlo,
                    RECTL *prclDest, POINTL *pptlSrc)
{
    BOOL bRc;
    RECTL rclDest = *prclDest;
    POINTL ptlSrc = *pptlSrc;
    BOOL bDo = TRUE;
    LOGF_ENTER();
    STATDRVENTRY(CopyBits, psoDest);

    LOG(("psoDest = %p, psoSrc = %p, pco = %p, pxlo = %p, prclDest = %p, pptlSrc = %p",
         psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc));
    DUMPSURF(psoSrc, "psoSrc");
    DUMPSURF(psoDest, "psoDest");
    STATPRINT;

#ifdef NEMU_VBVA_ADJUST_RECT
    /* Experimental fix for too large bitmap updates.
     *
     * Some application do a large bitmap update event if only
     * a small part of the bitmap is actually changed.
     *
     * The driver will find the changed rectangle by comparing
     * the current framebuffer content with the source bitmap.
     *
     * The optimization is only active when:
     *  - the VBVA extension is enabled;
     *  - the source bitmap is not cacheable;
     *  - the bitmap formats of both the source and the screen surfaces are equal.
     *
     */
    if (   psoSrc
        && !NemuDispIsScreenSurface(psoSrc)
        && NemuDispIsScreenSurface(psoDest))
    {
        PNEMUDISPDEV pDev = ((PNEMUDISPDEV))psoDest->dhpdev;

        LOG(("offscreen->screen"));

        if (   pDev->vbvaCtx.pVBVA
            && (pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents & VBVA_F_MODE_ENABLED))
        {
            if (   (psoSrc->fjBitmap & BMF_DONTCACHE) != 0
                || psoSrc->iUniq == 0)
            {
                LOG(("non-cacheable %d->%d (pDev %p)", psoSrc->iBitmapFormat, psoDest->iBitmapFormat, pDev));

                /* It is possible to apply the fix. */
                bDo = vbvaFindChangedRect(getSurfObj(psoDest), getSurfObj(psoSrc), &rclDest, &ptlSrc);
            }
        }
    }

    if (!bDo)
    {
        /* The operation is a NOP. Just return success. */
        LOGF_LEAVE();
        return TRUE;
    }
#endif /* NEMU_VBVA_ADJUST_RECT */

    bRc = EngCopyBits(getSurfObj(psoDest), getSurfObj(psoSrc), pco, pxlo, &rclDest, &ptlSrc);
    VBVA_OPERATION(psoDest, CopyBits, (psoDest, psoSrc, pco, pxlo, &rclDest, &ptlSrc));

    LOGF_LEAVE();
    return bRc;
}
