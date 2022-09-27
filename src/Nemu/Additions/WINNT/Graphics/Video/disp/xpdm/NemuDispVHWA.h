/* $Id: NemuDispVHWA.h $ */

/** @file
 * Nemu XPDM Display driver, helper functions which interacts with our miniport driver
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

#ifndef NEMUDISPVHWA_H
#define NEMUDISPVHWA_H

#include "NemuDisp.h"

#ifdef NEMU_WITH_VIDEOHWACCEL
typedef struct _NEMUDISPVHWAINFO
{
    uint32_t caps;
    uint32_t caps2;
    uint32_t colorKeyCaps;
    uint32_t stretchCaps;
    uint32_t surfaceCaps;
    uint32_t numOverlays;
    uint32_t numFourCC;
    HGSMIOFFSET FourCC;
    ULONG_PTR offVramBase;
    BOOLEAN bEnabled;
} NEMUDISPVHWAINFO;
#endif

typedef struct _NEMUVHWAREGION
{
    RECTL Rect;
    bool bValid;
}NEMUVHWAREGION, *PNEMUVHWAREGION;

typedef struct _NEMUVHWASURFDESC
{
    NEMUVHWA_SURFHANDLE hHostHandle;
    volatile uint32_t cPendingBltsSrc;
    volatile uint32_t cPendingBltsDst;
    volatile uint32_t cPendingFlipsCurr;
    volatile uint32_t cPendingFlipsTarg;
#ifdef DEBUG
    volatile uint32_t cFlipsCurr;
    volatile uint32_t cFlipsTarg;
#endif
    bool bVisible;
    NEMUVHWAREGION UpdatedMemRegion;
    NEMUVHWAREGION NonupdatedMemRegion;
}NEMUVHWASURFDESC, *PNEMUVHWASURFDESC;

typedef DECLCALLBACK(void) FNNEMUVHWACMDCOMPLETION(PNEMUDISPDEV pDev, NEMUVHWACMD * pCmd, void * pContext);
typedef FNNEMUVHWACMDCOMPLETION *PFNNEMUVHWACMDCOMPLETION;

void NemuDispVHWAInit(PNEMUDISPDEV pDev);
int NemuDispVHWAEnable(PNEMUDISPDEV pDev);
int NemuDispVHWADisable(PNEMUDISPDEV pDev);
int NemuDispVHWAInitHostInfo1(PNEMUDISPDEV pDev);
int NemuDispVHWAInitHostInfo2(PNEMUDISPDEV pDev, DWORD *pFourCC);

NEMUVHWACMD* NemuDispVHWACommandCreate(PNEMUDISPDEV pDev, NEMUVHWACMD_TYPE enmCmd, NEMUVHWACMD_LENGTH cbCmd);
void NemuDispVHWACommandRelease(PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd);
BOOL NemuDispVHWACommandSubmit(PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd);
void NemuDispVHWACommandSubmitAsynch (PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd, PFNNEMUVHWACMDCOMPLETION pfnCompletion, void * pContext);
void NemuDispVHWACommandSubmitAsynchAndComplete (PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd);
void NemuDispVHWACommandCheckHostCmds(PNEMUDISPDEV pDev);

PNEMUVHWASURFDESC NemuDispVHWASurfDescAlloc();
void NemuDispVHWASurfDescFree(PNEMUVHWASURFDESC pDesc);

uint64_t NemuDispVHWAVramOffsetFromPDEV(PNEMUDISPDEV pDev, ULONG_PTR offPdev);

void NemuDispVHWARectUnited(RECTL * pDst, RECTL * pRect1, RECTL * pRect2);
bool NemuDispVHWARectIsEmpty(RECTL * pRect);
bool NemuDispVHWARectIntersect(RECTL * pRect1, RECTL * pRect2);
bool NemuDispVHWARectInclude(RECTL * pRect1, RECTL * pRect2);
bool NemuDispVHWARegionIntersects(PNEMUVHWAREGION pReg, RECTL * pRect);
bool NemuDispVHWARegionIncludes(PNEMUVHWAREGION pReg, RECTL * pRect);
bool NemuDispVHWARegionIncluded(PNEMUVHWAREGION pReg, RECTL * pRect);
void NemuDispVHWARegionSet(PNEMUVHWAREGION pReg, RECTL * pRect);
void NemuDispVHWARegionAdd(PNEMUVHWAREGION pReg, RECTL * pRect);
void NemuDispVHWARegionInit(PNEMUVHWAREGION pReg);
void NemuDispVHWARegionClear(PNEMUVHWAREGION pReg);
bool NemuDispVHWARegionValid(PNEMUVHWAREGION pReg);
void NemuDispVHWARegionTrySubstitute(PNEMUVHWAREGION pReg, const RECTL *pRect);

uint32_t NemuDispVHWAFromDDCAPS(uint32_t caps);
uint32_t NemuDispVHWAToDDCAPS(uint32_t caps);
uint32_t NemuDispVHWAFromDDSCAPS(uint32_t caps);
uint32_t NemuDispVHWAToDDSCAPS(uint32_t caps);
uint32_t NemuDispVHWAFromDDPFS(uint32_t caps);
uint32_t NemuDispVHWAToDDPFS(uint32_t caps);
uint32_t NemuDispVHWAFromDDCKEYCAPS(uint32_t caps);
uint32_t NemuDispVHWAToDDCKEYCAPS(uint32_t caps);
uint32_t NemuDispVHWAToDDBLTs(uint32_t caps);
uint32_t NemuDispVHWAFromDDBLTs(uint32_t caps);
uint32_t NemuDispVHWAFromDDCAPS2(uint32_t caps);
uint32_t NemuDispVHWAToDDCAPS2(uint32_t caps);
uint32_t NemuDispVHWAFromDDOVERs(uint32_t caps);
uint32_t NemuDispVHWAToDDOVERs(uint32_t caps);
uint32_t NemuDispVHWAFromDDCKEYs(uint32_t caps);
uint32_t NemuDispVHWAToDDCKEYs(uint32_t caps);

int NemuDispVHWAFromDDSURFACEDESC(NEMUVHWA_SURFACEDESC *pVHWADesc, DDSURFACEDESC *pDdDesc);
int NemuDispVHWAFromDDPIXELFORMAT(NEMUVHWA_PIXELFORMAT *pVHWAFormat, DDPIXELFORMAT *pDdFormat);
void NemuDispVHWAFromDDOVERLAYFX(NEMUVHWA_OVERLAYFX *pVHWAOverlay, DDOVERLAYFX *pDdOverlay);
void NemuDispVHWAFromDDCOLORKEY(NEMUVHWA_COLORKEY *pVHWACKey, DDCOLORKEY  *pDdCKey);
void NemuDispVHWAFromDDBLTFX(NEMUVHWA_BLTFX *pVHWABlt, DDBLTFX *pDdBlt);
void NemuDispVHWAFromRECTL(NEMUVHWA_RECTL *pDst, RECTL *pSrc);

uint32_t NemuDispVHWAUnsupportedDDCAPS(uint32_t caps);
uint32_t NemuDispVHWAUnsupportedDDSCAPS(uint32_t caps);
uint32_t NemuDispVHWAUnsupportedDDPFS(uint32_t caps);
uint32_t NemuDispVHWAUnsupportedDDCEYCAPS(uint32_t caps);
uint32_t NemuDispVHWASupportedDDCAPS(uint32_t caps);
uint32_t NemuDispVHWASupportedDDSCAPS(uint32_t caps);
uint32_t NemuDispVHWASupportedDDPFS(uint32_t caps);
uint32_t NemuDispVHWASupportedDDCEYCAPS(uint32_t caps);

#endif /*NEMUDISPVHWA_H*/
