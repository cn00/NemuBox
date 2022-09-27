/* $Id: NemuDispDDrawVHWA.cpp $ */

/** @file
 * Nemu XPDM Display driver, DirectDraw callbacks VHWA related
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

#include "NemuDisp.h"
#include "NemuDispDDraw.h"
#include <iprt/asm.h>

static DECLCALLBACK(void) NemuDispVHWASurfBltCompletion(PNEMUDISPDEV pDev, NEMUVHWACMD * pCmd, void * pContext)
{
    NEMUVHWACMD_SURF_BLT *pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_BLT);
    PNEMUVHWASURFDESC pSrcDesc = (PNEMUVHWASURFDESC)pBody->SrcGuestSurfInfo;
    PNEMUVHWASURFDESC pDestDesc = (PNEMUVHWASURFDESC)pBody->DstGuestSurfInfo;

    ASMAtomicDecU32(&pSrcDesc->cPendingBltsSrc);
    ASMAtomicDecU32(&pDestDesc->cPendingBltsDst);

    NemuDispVHWACommandRelease(pDev, pCmd);
}

static DECLCALLBACK(void) NemuDispVHWASurfFlipCompletion(PNEMUDISPDEV pDev, NEMUVHWACMD * pCmd, void * pContext)
{
    NEMUVHWACMD_SURF_FLIP *pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_FLIP);
    PNEMUVHWASURFDESC pCurrDesc = (PNEMUVHWASURFDESC)pBody->CurrGuestSurfInfo;
    PNEMUVHWASURFDESC pTargDesc = (PNEMUVHWASURFDESC)pBody->TargGuestSurfInfo;

    ASMAtomicDecU32(&pCurrDesc->cPendingFlipsCurr);
    ASMAtomicDecU32(&pTargDesc->cPendingFlipsTarg);

    NemuDispVHWACommandRelease(pDev, pCmd);
}

#define NEMUVHWA_CAP(_pdev, _cap) ((_pdev)->vhwa.caps & (_cap))
#define ROP_INDEX(_rop) ((BYTE)((_rop)>>16))
#define SET_SUPPORT_ROP(_aRops, _rop) _aRops[ROP_INDEX(_rop)/32] |= 1L << ((DWORD)(ROP_INDEX(_rop)%32))

int NemuDispVHWAUpdateDDHalInfo(PNEMUDISPDEV pDev, DD_HALINFO *pHalInfo)
{
    if (!NEMUVHWA_CAP(pDev, NEMUVHWA_CAPS_BLT) && !NEMUVHWA_CAP(pDev, NEMUVHWA_CAPS_OVERLAY))
    {
        return VERR_NOT_SUPPORTED;
    }

    pHalInfo->ddCaps.dwCaps |= NemuDispVHWAToDDCAPS(pDev->vhwa.caps);
    if (NEMUVHWA_CAP(pDev, NEMUVHWA_CAPS_BLT))
    {
        /* we only support simple dst=src copy
         * Note: search "ternary raster operations" on msdn for more info
         */
        SET_SUPPORT_ROP(pHalInfo->ddCaps.dwRops, SRCCOPY);
    }

    pHalInfo->ddCaps.ddsCaps.dwCaps |= NemuDispVHWAToDDSCAPS(pDev->vhwa.surfaceCaps);
    pHalInfo->ddCaps.dwCaps2 |= NemuDispVHWAToDDCAPS2(pDev->vhwa.caps2);

    if (NEMUVHWA_CAP(pDev, NEMUVHWA_CAPS_BLT) && NEMUVHWA_CAP(pDev, NEMUVHWA_CAPS_BLTSTRETCH))
    {
        pHalInfo->ddCaps.dwFXCaps |= DDFXCAPS_BLTSTRETCHX|DDFXCAPS_BLTSTRETCHY|
                                     DDFXCAPS_BLTSTRETCHXN|DDFXCAPS_BLTSTRETCHYN|
                                     DDFXCAPS_BLTSHRINKX|DDFXCAPS_BLTSHRINKY|
                                     DDFXCAPS_BLTSHRINKXN|DDFXCAPS_BLTSHRINKYN|
                                     DDFXCAPS_BLTARITHSTRETCHY;
    }

    if (NEMUVHWA_CAP(pDev, NEMUVHWA_CAPS_OVERLAY) && NEMUVHWA_CAP(pDev, NEMUVHWA_CAPS_OVERLAYSTRETCH))
    {
        pHalInfo->ddCaps.dwFXCaps |= DDFXCAPS_OVERLAYSTRETCHX|DDFXCAPS_OVERLAYSTRETCHY|
                                     DDFXCAPS_OVERLAYSTRETCHXN|DDFXCAPS_OVERLAYSTRETCHYN|
                                     DDFXCAPS_OVERLAYSHRINKX|DDFXCAPS_OVERLAYSHRINKY|
                                     DDFXCAPS_OVERLAYSHRINKXN|DDFXCAPS_OVERLAYSHRINKYN|
                                     DDFXCAPS_OVERLAYARITHSTRETCHY;
    }

    pHalInfo->ddCaps.dwCKeyCaps = NemuDispVHWAToDDCKEYCAPS(pDev->vhwa.colorKeyCaps);

    if (NEMUVHWA_CAP(pDev, NEMUVHWA_CAPS_OVERLAY))
    {
        pHalInfo->ddCaps.dwMaxVisibleOverlays = pDev->vhwa.numOverlays;
        pHalInfo->ddCaps.dwCurrVisibleOverlays = 0;
        pHalInfo->ddCaps.dwMinOverlayStretch = 1;
        pHalInfo->ddCaps.dwMaxOverlayStretch = 32000;
    }

    return VINF_SUCCESS;
}

/*
 * DirectDraw callbacks.
 */

#define IF_NOT_SUPPORTED(_guid)                  \
    if (IsEqualIID(&lpData->guidInfo, &(_guid))) \
    {                                            \
        LOG((#_guid));                           \
    }

DWORD APIENTRY NemuDispDDGetDriverInfo(DD_GETDRIVERINFODATA *lpData)
{
    LOGF_ENTER();

    lpData->ddRVal = DDERR_CURRENTLYNOTAVAIL;

    if (IsEqualIID(&lpData->guidInfo, &GUID_NTPrivateDriverCaps))
    {
        LOG(("GUID_NTPrivateDriverCaps"));

        DD_NTPRIVATEDRIVERCAPS caps;
        memset(&caps, 0, sizeof(caps));
        caps.dwSize = sizeof(DD_NTPRIVATEDRIVERCAPS);
        caps.dwPrivateCaps = DDHAL_PRIVATECAP_NOTIFYPRIMARYCREATION;

        lpData->dwActualSize = sizeof(DD_NTPRIVATEDRIVERCAPS);
        lpData->ddRVal = DD_OK;
        memcpy(lpData->lpvData, &caps, min(lpData->dwExpectedSize, sizeof(DD_NTPRIVATEDRIVERCAPS)));
    }
    else IF_NOT_SUPPORTED(GUID_NTCallbacks)
    else IF_NOT_SUPPORTED(GUID_D3DCallbacks2)
    else IF_NOT_SUPPORTED(GUID_D3DCallbacks3)
    else IF_NOT_SUPPORTED(GUID_D3DExtendedCaps)
    else IF_NOT_SUPPORTED(GUID_ZPixelFormats)
    else IF_NOT_SUPPORTED(GUID_D3DParseUnknownCommandCallback)
    else IF_NOT_SUPPORTED(GUID_Miscellaneous2Callbacks)
    else IF_NOT_SUPPORTED(GUID_UpdateNonLocalHeap)
    else IF_NOT_SUPPORTED(GUID_GetHeapAlignment)
    else IF_NOT_SUPPORTED(GUID_DDStereoMode)
    else IF_NOT_SUPPORTED(GUID_NonLocalVidMemCaps)
    else IF_NOT_SUPPORTED(GUID_KernelCaps)
    else IF_NOT_SUPPORTED(GUID_KernelCallbacks)
    else IF_NOT_SUPPORTED(GUID_MotionCompCallbacks)
    else IF_NOT_SUPPORTED(GUID_VideoPortCallbacks)
    else IF_NOT_SUPPORTED(GUID_ColorControlCallbacks)
    else IF_NOT_SUPPORTED(GUID_VideoPortCaps)
    else IF_NOT_SUPPORTED(GUID_DDMoreSurfaceCaps)
    else
    {
        LOG(("unknown guid"));
    }


    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY NemuDispDDSetColorKey(PDD_SETCOLORKEYDATA lpSetColorKey)
{
    PNEMUDISPDEV pDev = (PNEMUDISPDEV) lpSetColorKey->lpDD->dhpdev;
    LOGF_ENTER();

    DD_SURFACE_LOCAL *pSurf = lpSetColorKey->lpDDSurface;
    PNEMUVHWASURFDESC pDesc = (PNEMUVHWASURFDESC)pSurf->lpGbl->dwReserved1;
    NEMUVHWACMD* pCmd;

    lpSetColorKey->ddRVal = DD_OK;

    if (pDesc)
    {
        pCmd = NemuDispVHWACommandCreate(pDev, NEMUVHWACMD_TYPE_SURF_COLORKEY_SET, sizeof(NEMUVHWACMD_SURF_COLORKEY_SET));

        if (pCmd)
        {
            NEMUVHWACMD_SURF_COLORKEY_SET *pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_COLORKEY_SET);
            memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_COLORKEY_SET));

            pBody->u.in.offSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pSurf->lpGbl->fpVidMem);
            pBody->u.in.hSurf = pDesc->hHostHandle;
            pBody->u.in.flags = NemuDispVHWAFromDDCKEYs(lpSetColorKey->dwFlags);
            NemuDispVHWAFromDDCOLORKEY(&pBody->u.in.CKey, &lpSetColorKey->ckNew);

            NemuDispVHWACommandSubmitAsynchAndComplete(pDev, pCmd);
        }
        else
        {
            WARN(("NemuDispVHWACommandCreate failed!"));
            lpSetColorKey->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!pDesc"));
        lpSetColorKey->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY NemuDispDDAddAttachedSurface(PDD_ADDATTACHEDSURFACEDATA lpAddAttachedSurface)
{
    LOGF_ENTER();

    lpAddAttachedSurface->ddRVal = DD_OK;

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY NemuDispDDBlt(PDD_BLTDATA lpBlt)
{
    PNEMUDISPDEV pDev = (PNEMUDISPDEV) lpBlt->lpDD->dhpdev;
    LOGF_ENTER();

    DD_SURFACE_LOCAL *pSrcSurf = lpBlt->lpDDSrcSurface;
    DD_SURFACE_LOCAL *pDstSurf = lpBlt->lpDDDestSurface;
    PNEMUVHWASURFDESC pSrcDesc = (PNEMUVHWASURFDESC) pSrcSurf->lpGbl->dwReserved1;
    PNEMUVHWASURFDESC pDstDesc = (PNEMUVHWASURFDESC) pDstSurf->lpGbl->dwReserved1;

    if (pSrcDesc && pDstDesc)
    {
        NEMUVHWACMD *pCmd;

        pCmd = NemuDispVHWACommandCreate(pDev, NEMUVHWACMD_TYPE_SURF_BLT, sizeof(NEMUVHWACMD_SURF_BLT));
        if (pCmd)
        {
            NEMUVHWACMD_SURF_BLT *pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_BLT);
            memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_BLT));

            pBody->u.in.offSrcSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pSrcSurf->lpGbl->fpVidMem);
            pBody->u.in.offDstSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pDstSurf->lpGbl->fpVidMem);

            pBody->u.in.hDstSurf = pDstDesc->hHostHandle;
            NemuDispVHWAFromRECTL(&pBody->u.in.dstRect, &lpBlt->rDest);
            pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;
            NemuDispVHWAFromRECTL(&pBody->u.in.srcRect, &lpBlt->rSrc);
            pBody->DstGuestSurfInfo = (uint64_t)pDstDesc;
            pBody->SrcGuestSurfInfo = (uint64_t)pSrcDesc;

            pBody->u.in.flags = NemuDispVHWAFromDDBLTs(lpBlt->dwFlags);
            NemuDispVHWAFromDDBLTFX(&pBody->u.in.desc, &lpBlt->bltFX);

            ASMAtomicIncU32(&pSrcDesc->cPendingBltsSrc);
            ASMAtomicIncU32(&pDstDesc->cPendingBltsDst);

            NemuDispVHWARegionAdd(&pDstDesc->NonupdatedMemRegion, &lpBlt->rDest);
            NemuDispVHWARegionTrySubstitute(&pDstDesc->UpdatedMemRegion, &lpBlt->rDest);

            if(pSrcDesc->UpdatedMemRegion.bValid)
            {
                pBody->u.in.xUpdatedSrcMemValid = 1;
                NemuDispVHWAFromRECTL(&pBody->u.in.xUpdatedSrcMemRect, &pSrcDesc->UpdatedMemRegion.Rect);
                NemuDispVHWARegionClear(&pSrcDesc->UpdatedMemRegion);
            }

            NemuDispVHWACommandSubmitAsynch(pDev, pCmd, NemuDispVHWASurfBltCompletion, NULL);

            lpBlt->ddRVal = DD_OK;
        }
        else
        {
            WARN(("NemuDispVHWACommandCreate failed!"));
            lpBlt->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!(pSrcDesc && pDstDesc)"));
        lpBlt->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY NemuDispDDFlip(PDD_FLIPDATA lpFlip)
{
    PNEMUDISPDEV pDev = (PNEMUDISPDEV) lpFlip->lpDD->dhpdev;
    LOGF_ENTER();

    DD_SURFACE_LOCAL *pCurrSurf = lpFlip->lpSurfCurr;
    DD_SURFACE_LOCAL *pTargSurf = lpFlip->lpSurfTarg;
    PNEMUVHWASURFDESC pCurrDesc = (PNEMUVHWASURFDESC) pCurrSurf->lpGbl->dwReserved1;
    PNEMUVHWASURFDESC pTargDesc = (PNEMUVHWASURFDESC) pTargSurf->lpGbl->dwReserved1;

    if (pCurrDesc && pTargDesc)
    {
        if(ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsCurr)
           || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsCurr))
        {
            NemuDispVHWACommandCheckHostCmds(pDev);

            if(ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pCurrDesc->cPendingFlipsCurr)
               || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pTargDesc->cPendingFlipsCurr))
            {
                lpFlip->ddRVal = DDERR_WASSTILLDRAWING;
                return DDHAL_DRIVER_HANDLED;
            }
        }

        NEMUVHWACMD *pCmd;

        pCmd = NemuDispVHWACommandCreate(pDev, NEMUVHWACMD_TYPE_SURF_FLIP, sizeof(NEMUVHWACMD_SURF_FLIP));

        if (pCmd)
        {
            NEMUVHWACMD_SURF_FLIP *pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_FLIP);
            memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_FLIP));

            pBody->u.in.offCurrSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pCurrSurf->lpGbl->fpVidMem);
            pBody->u.in.offTargSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pTargSurf->lpGbl->fpVidMem);

            pBody->u.in.hTargSurf = pTargDesc->hHostHandle;
            pBody->u.in.hCurrSurf = pCurrDesc->hHostHandle;
            pBody->TargGuestSurfInfo = (uint64_t)pTargDesc;
            pBody->CurrGuestSurfInfo = (uint64_t)pCurrDesc;

            pTargDesc->bVisible = pCurrDesc->bVisible;
            pCurrDesc->bVisible = false;


            ASMAtomicIncU32(&pCurrDesc->cPendingFlipsCurr);
            ASMAtomicIncU32(&pTargDesc->cPendingFlipsTarg);
#ifdef DEBUG
            ASMAtomicIncU32(&pCurrDesc->cFlipsCurr);
            ASMAtomicIncU32(&pTargDesc->cFlipsTarg);
#endif

            if(pTargDesc->UpdatedMemRegion.bValid)
            {
                pBody->u.in.xUpdatedTargMemValid = 1;
                NemuDispVHWAFromRECTL(&pBody->u.in.xUpdatedTargMemRect, &pTargDesc->UpdatedMemRegion.Rect);
                NemuDispVHWARegionClear(&pTargDesc->UpdatedMemRegion);
            }

            NemuDispVHWACommandSubmitAsynch(pDev, pCmd, NemuDispVHWASurfFlipCompletion, NULL);

            lpFlip->ddRVal = DD_OK;
        }
        else
        {
            WARN(("NemuDispVHWACommandCreate failed!"));
            lpFlip->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!(pCurrDesc && pTargDesc)"));
        lpFlip->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY NemuDispDDGetBltStatus(PDD_GETBLTSTATUSDATA lpGetBltStatus)
{
    PNEMUDISPDEV pDev = (PNEMUDISPDEV) lpGetBltStatus->lpDD->dhpdev;
    PNEMUVHWASURFDESC pDesc = (PNEMUVHWASURFDESC)lpGetBltStatus->lpDDSurface->lpGbl->dwReserved1;
    LOGF_ENTER();

    if(lpGetBltStatus->dwFlags == DDGBS_CANBLT)
    {
        lpGetBltStatus->ddRVal = DD_OK;
    }
    else /* DDGBS_ISBLTDONE */
    {
        if (pDesc)
        {
            if(ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc) || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst))
            {
                NemuDispVHWACommandCheckHostCmds(pDev);

                if(ASMAtomicUoReadU32(&pDesc->cPendingBltsSrc) || ASMAtomicUoReadU32(&pDesc->cPendingBltsDst))
                {
                    lpGetBltStatus->ddRVal = DDERR_WASSTILLDRAWING;
                }
                else
                {
                    lpGetBltStatus->ddRVal = DD_OK;
                }
            }
            else
            {
                lpGetBltStatus->ddRVal = DD_OK;
            }
        }
        else
        {
            WARN(("!pDesc"));
            lpGetBltStatus->ddRVal = DDERR_GENERIC;
        }
    }


    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY NemuDispDDGetFlipStatus(PDD_GETFLIPSTATUSDATA lpGetFlipStatus)
{
    PNEMUDISPDEV pDev = (PNEMUDISPDEV) lpGetFlipStatus->lpDD->dhpdev;
    PNEMUVHWASURFDESC pDesc = (PNEMUVHWASURFDESC)lpGetFlipStatus->lpDDSurface->lpGbl->dwReserved1;
    LOGF_ENTER();

    /*can't flip is there's a flip pending, so result is same for DDGFS_CANFLIP/DDGFS_ISFLIPDONE */

    if (pDesc)
    {
        if(ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr))
        {
            NemuDispVHWACommandCheckHostCmds(pDev);

            if(ASMAtomicUoReadU32(&pDesc->cPendingFlipsTarg) || ASMAtomicUoReadU32(&pDesc->cPendingFlipsCurr))
            {
                lpGetFlipStatus->ddRVal = DDERR_WASSTILLDRAWING;
            }
            else
            {
                lpGetFlipStatus->ddRVal = DD_OK;
            }
        }
        else
        {
            lpGetFlipStatus->ddRVal = DD_OK;
        }
    }
    else
    {
        WARN(("!pDesc"));
        lpGetFlipStatus->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY NemuDispDDSetOverlayPosition(PDD_SETOVERLAYPOSITIONDATA lpSetOverlayPosition)
{
    PNEMUDISPDEV pDev = (PNEMUDISPDEV) lpSetOverlayPosition->lpDD->dhpdev;
    DD_SURFACE_LOCAL *pSrcSurf = lpSetOverlayPosition->lpDDSrcSurface;
    DD_SURFACE_LOCAL *pDstSurf = lpSetOverlayPosition->lpDDDestSurface;
    PNEMUVHWASURFDESC pSrcDesc = (PNEMUVHWASURFDESC) pSrcSurf->lpGbl->dwReserved1;
    PNEMUVHWASURFDESC pDstDesc = (PNEMUVHWASURFDESC) pDstSurf->lpGbl->dwReserved1;

    LOGF_ENTER();

    if (pSrcDesc && pDstDesc)
    {
        if (!pSrcDesc->bVisible)
        {
            WARN(("!pSrcDesc->bVisible"));
            lpSetOverlayPosition->ddRVal = DDERR_GENERIC;
            return DDHAL_DRIVER_HANDLED;
        }

        NEMUVHWACMD *pCmd;

        pCmd = NemuDispVHWACommandCreate(pDev, NEMUVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION, sizeof(NEMUVHWACMD_SURF_OVERLAY_SETPOSITION));
        if (pCmd)
        {
            NEMUVHWACMD_SURF_OVERLAY_SETPOSITION *pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_OVERLAY_SETPOSITION);
            memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_OVERLAY_SETPOSITION));

            pBody->u.in.offSrcSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pSrcSurf->lpGbl->fpVidMem);
            pBody->u.in.offDstSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pDstSurf->lpGbl->fpVidMem);

            pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;
            pBody->u.in.hDstSurf = pDstDesc->hHostHandle;

            pBody->u.in.xPos = lpSetOverlayPosition->lXPos;
            pBody->u.in.yPos = lpSetOverlayPosition->lYPos;

            NemuDispVHWACommandSubmitAsynchAndComplete(pDev, pCmd);

            lpSetOverlayPosition->ddRVal = DD_OK;
        }
        else
        {
            WARN(("NemuDispVHWACommandCreate failed!"));
            lpSetOverlayPosition->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!(pSrcDesc && pDstDesc)"));
        lpSetOverlayPosition->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}

DWORD APIENTRY NemuDispDDUpdateOverlay(PDD_UPDATEOVERLAYDATA lpUpdateOverlay)
{
    PNEMUDISPDEV pDev = (PNEMUDISPDEV) lpUpdateOverlay->lpDD->dhpdev;
    DD_SURFACE_LOCAL* pSrcSurf = lpUpdateOverlay->lpDDSrcSurface;
    DD_SURFACE_LOCAL* pDstSurf = lpUpdateOverlay->lpDDDestSurface;
    PNEMUVHWASURFDESC pSrcDesc = (PNEMUVHWASURFDESC) pSrcSurf->lpGbl->dwReserved1;

    LOGF_ENTER();

    if (pSrcDesc)
    {
        NEMUVHWACMD* pCmd;

        pCmd = NemuDispVHWACommandCreate (pDev, NEMUVHWACMD_TYPE_SURF_OVERLAY_UPDATE, sizeof(NEMUVHWACMD_SURF_OVERLAY_UPDATE));
        if (pCmd)
        {
            NEMUVHWACMD_SURF_OVERLAY_UPDATE *pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_OVERLAY_UPDATE);
            memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_OVERLAY_UPDATE));

            pBody->u.in.offSrcSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pSrcSurf->lpGbl->fpVidMem);

            pBody->u.in.hSrcSurf = pSrcDesc->hHostHandle;

            NemuDispVHWAFromRECTL(&pBody->u.in.dstRect, &lpUpdateOverlay->rDest);
            NemuDispVHWAFromRECTL(&pBody->u.in.srcRect, &lpUpdateOverlay->rSrc);

            pBody->u.in.flags = NemuDispVHWAFromDDOVERs(lpUpdateOverlay->dwFlags);
            NemuDispVHWAFromDDOVERLAYFX(&pBody->u.in.desc, &lpUpdateOverlay->overlayFX);

            if (lpUpdateOverlay->dwFlags & DDOVER_HIDE)
            {
                pSrcDesc->bVisible = false;
            }
            else if(lpUpdateOverlay->dwFlags & DDOVER_SHOW)
            {
                pSrcDesc->bVisible = true;
                if(pSrcDesc->UpdatedMemRegion.bValid)
                {
                    pBody->u.in.xFlags = NEMUVHWACMD_SURF_OVERLAY_UPDATE_F_SRCMEMRECT;
                    NemuDispVHWAFromRECTL(&pBody->u.in.xUpdatedSrcMemRect, &pSrcDesc->UpdatedMemRegion.Rect);
                    NemuDispVHWARegionClear(&pSrcDesc->UpdatedMemRegion);
                }
            }

            if(pDstSurf)
            {
                PNEMUVHWASURFDESC pDstDesc = (PNEMUVHWASURFDESC) pDstSurf->lpGbl->dwReserved1;

                if (!pDstDesc)
                {
                    WARN(("!pDstDesc"));
                    lpUpdateOverlay->ddRVal = DDERR_GENERIC;
                    return DDHAL_DRIVER_HANDLED;
                }

                pBody->u.in.hDstSurf = pDstDesc->hHostHandle;
                pBody->u.in.offDstSurface = NemuDispVHWAVramOffsetFromPDEV(pDev, pDstSurf->lpGbl->fpVidMem);
            }

            NemuDispVHWACommandSubmitAsynchAndComplete(pDev, pCmd);

            lpUpdateOverlay->ddRVal = DD_OK;
        }
        else
        {
            WARN(("NemuDispVHWACommandCreate failed!"));
            lpUpdateOverlay->ddRVal = DDERR_GENERIC;
        }
    }
    else
    {
        WARN(("!pSrcDesc"));
        lpUpdateOverlay->ddRVal = DDERR_GENERIC;
    }

    LOGF_LEAVE();
    return DDHAL_DRIVER_HANDLED;
}
