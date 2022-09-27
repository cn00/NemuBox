/* $Id: NemuDispInternal.h $ */

/** @file
 * Nemu XPDM Display driver, internal header
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

#ifndef NEMUDISPINTERNAL_H
#define NEMUDISPINTERNAL_H

#include <iprt/cdefs.h>
#define LOG_GROUP LOG_GROUP_DRV_DISPLAY
#include <Nemu/log.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <windef.h>
#include <wingdi.h>
#include <winddi.h>
#include <ntddvdeo.h>
#undef CO_E_NOTINITIALIZED
#include <winerror.h>
#include <devioctl.h>
#define NEMU_VIDEO_LOG_NAME "NemuDisp"
#include "common/NemuVideoLog.h"
#include "common/xpdm/NemuVideoPortAPI.h"
#include "common/xpdm/NemuVideoIOCTL.h"
#include <Nemu/HGSMI/HGSMI.h>
#include <Nemu/NemuVideo.h>
#include <Nemu/NemuVideoGuest.h>
#include <NemuDisplay.h>

typedef struct _NEMUDISPDEV *PNEMUDISPDEV;

#ifdef NEMU_WITH_VIDEOHWACCEL
# include "NemuDispVHWA.h"
#endif

/* 4bytes tag passed to EngAllocMem.
 * Note: chars are reverse order.
 */
#define MEM_ALLOC_TAG 'bvDD'

/* Helper macros */
#define NEMU_WARN_WINERR(_winerr)                     \
    do {                                              \
        if ((_winerr) != NO_ERROR)                    \
        {                                             \
            WARN(("winerr(%#x)!=NO_ERROR", _winerr)); \
        }                                             \
    } while (0)

#define NEMU_CHECK_WINERR_RETRC(_winerr, _rc)         \
    do {                                              \
        if ((_winerr) != NO_ERROR)                    \
        {                                             \
            WARN(("winerr(%#x)!=NO_ERROR", _winerr)); \
            return (_rc);                             \
        }                                             \
    } while (0)

#define NEMU_WARNRC_RETV(_rc, _ret)            \
    do {                                       \
        if (RT_FAILURE(_rc))                   \
        {                                      \
            WARN(("RT_FAILURE rc(%#x)", _rc)); \
            return (_ret);                     \
        }                                      \
    } while (0)

#define NEMU_WARNRC_RETRC(_rc) NEMU_WARNRC_RETV(_rc, _rc)

#define NEMU_WARNRC(_rc)                       \
    do {                                       \
        if (RT_FAILURE(_rc))                   \
        {                                      \
            WARN(("RT_FAILURE rc(%#x)", _rc)); \
        }                                      \
    } while (0)

#define NEMU_WARNRC_NOBP(_rc)                       \
    do {                                       \
        if (RT_FAILURE(_rc))                   \
        {                                      \
            WARN_NOBP(("RT_FAILURE rc(%#x)", _rc)); \
        }                                      \
    } while (0)


#define NEMU_WARN_IOCTLCB_RETRC(_ioctl, _cbreturned, _cbexpected, _rc)                   \
    do {                                                                                 \
        if ((_cbreturned)!=(_cbexpected))                                                \
        {                                                                                \
            WARN((_ioctl " returned %d, expected %d bytes!", _cbreturned, _cbexpected)); \
            return (_rc);                                                                \
        }                                                                                \
    } while (0)

#define abs(_v) ( ((_v)>0) ? (_v) : (-(_v)) )

typedef struct _CLIPRECTS {
    ULONG  c;
    RECTL  arcl[64];
} CLIPRECTS;

typedef struct _VRDPCLIPRECTS
{
    RECTL rclDstOrig; /* Original bounding rectangle. */
    RECTL rclDst;     /* Bounding rectangle of all rects. */
    CLIPRECTS rects;  /* Rectangles to update. */
} VRDPCLIPRECTS;

/* Mouse pointer related functions */
int NemuDispInitPointerCaps(PNEMUDISPDEV pDev, DEVINFO *pDevInfo);
int NemuDispInitPointerAttrs(PNEMUDISPDEV pDev);

/* Palette related functions */
int NemuDispInitPalette(PNEMUDISPDEV pDev, DEVINFO *pDevInfo);
void NemuDispDestroyPalette(PNEMUDISPDEV pDev);
int NemuDispSetPalette8BPP(PNEMUDISPDEV pDev);

/* VBVA related */
int NemuDispVBVAInit(PNEMUDISPDEV pDev);
void NemuDispVBVAHostCommandComplete(PNEMUDISPDEV pDev, VBVAHOSTCMD *pCmd);

void vrdpReportDirtyRect(PNEMUDISPDEV pDev, RECTL *prcl);
void vbvaReportDirtyRect(PNEMUDISPDEV pDev, RECTL *prcl);

#ifdef NEMU_VBVA_ADJUST_RECT
void vrdpAdjustRect (SURFOBJ *pso, RECTL *prcl);
BOOL vbvaFindChangedRect(SURFOBJ *psoDest, SURFOBJ *psoSrc, RECTL *prclDest, POINTL *pptlSrc);
#endif /* NEMU_VBVA_ADJUST_RECT */

#define VRDP_TEXT_MAX_GLYPH_SIZE 0x100
#define VRDP_TEXT_MAX_GLYPHS     0xfe
BOOL vrdpReportText(PNEMUDISPDEV pDev, VRDPCLIPRECTS *pClipRects, STROBJ *pstro, FONTOBJ *pfo,
                    RECTL *prclOpaque, ULONG ulForeRGB, ULONG ulBackRGB);

BOOL vrdpReportOrderGeneric(PNEMUDISPDEV pDev, const VRDPCLIPRECTS *pClipRects,
                             const void *pvOrder, unsigned cbOrder, unsigned code);

BOOL NemuDispIsScreenSurface(SURFOBJ *pso);
void NemuDispDumpPSO(SURFOBJ *pso, char *s);

BOOL vrdpDrvRealizeBrush(BRUSHOBJ *pbo, SURFOBJ *psoTarget, SURFOBJ *psoPattern, SURFOBJ *psoMask,
                         XLATEOBJ *pxlo, ULONG iHatch);
void vrdpReset(PNEMUDISPDEV pDev);

DECLINLINE(int) format2BytesPerPixel(const SURFOBJ *pso)
{
    switch (pso->iBitmapFormat)
    {
        case BMF_16BPP: return 2;
        case BMF_24BPP: return 3;
        case BMF_32BPP: return 4;
    }

    return 0;
}

#endif /*NEMUDISPINTERNAL_H*/
