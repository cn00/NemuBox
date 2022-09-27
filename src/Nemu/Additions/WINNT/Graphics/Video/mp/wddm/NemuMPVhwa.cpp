/* $Id: NemuMPVhwa.cpp $ */

/** @file
 * Nemu WDDM Miniport driver
 */

/*
 * Copyright (C) 2011-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "NemuMPWddm.h"
#include "NemuMPVhwa.h"

#ifndef NEMUVHWA_WITH_SHGSMI
# include <iprt/semaphore.h>
# include <iprt/asm.h>
#endif

#define NEMUVHWA_PRIMARY_ALLOCATION(_pSrc) ((_pSrc)->pPrimaryAllocation)


DECLINLINE(void) nemuVhwaHdrInit(NEMUVHWACMD* pHdr, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId, NEMUVHWACMD_TYPE enmCmd)
{
    memset(pHdr, 0, sizeof(NEMUVHWACMD));
    pHdr->iDisplay = srcId;
    pHdr->rc = VERR_GENERAL_FAILURE;
    pHdr->enmCmd = enmCmd;
#ifndef NEMUVHWA_WITH_SHGSMI
    pHdr->cRefs = 1;
#endif
}

#ifdef NEMUVHWA_WITH_SHGSMI
static int nemuVhwaCommandSubmitHgsmi(struct _DEVICE_EXTENSION* pDevExt, HGSMIOFFSET offDr)
{
    NemuHGSMIGuestWrite(pDevExt, offDr);
    return VINF_SUCCESS;
}
#else
DECLINLINE(void) vbvaVhwaCommandRelease(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd)
{
    uint32_t cRefs = ASMAtomicDecU32(&pCmd->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if(!cRefs)
    {
        NemuHGSMIBufferFree(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, pCmd);
    }
}

DECLINLINE(void) vbvaVhwaCommandRetain(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd)
{
    ASMAtomicIncU32(&pCmd->cRefs);
}

/* do not wait for completion */
void nemuVhwaCommandSubmitAsynch(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd, PFNNEMUVHWACMDCOMPLETION pfnCompletion, void * pContext)
{
    pCmd->GuestVBVAReserved1 = (uintptr_t)pfnCompletion;
    pCmd->GuestVBVAReserved2 = (uintptr_t)pContext;
    vbvaVhwaCommandRetain(pDevExt, pCmd);

    NemuHGSMIBufferSubmit(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, pCmd);

    if(!(pCmd->Flags & NEMUVHWACMD_FLAG_HG_ASYNCH)
            || ((pCmd->Flags & NEMUVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION)
                    && (pCmd->Flags & NEMUVHWACMD_FLAG_HG_ASYNCH_RETURNED)))
    {
        /* the command is completed */
        pfnCompletion(pDevExt, pCmd, pContext);
    }

    vbvaVhwaCommandRelease(pDevExt, pCmd);
}

static DECLCALLBACK(void) nemuVhwaCompletionSetEvent(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD * pCmd, void * pvContext)
{
    RTSemEventSignal((RTSEMEVENT)pvContext);
}

void nemuVhwaCommandSubmitAsynchByEvent(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd, RTSEMEVENT hEvent)
{
    nemuVhwaCommandSubmitAsynch(pDevExt, pCmd, nemuVhwaCompletionSetEvent, hEvent);
}
#endif

void nemuVhwaCommandCheckCompletion(PNEMUMP_DEVEXT pDevExt)
{
    NTSTATUS Status = nemuWddmCallIsr(pDevExt);
    Assert(Status == STATUS_SUCCESS);
}

NEMUVHWACMD* nemuVhwaCommandCreate(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId, NEMUVHWACMD_TYPE enmCmd, NEMUVHWACMD_LENGTH cbCmd)
{
    nemuVhwaCommandCheckCompletion(pDevExt);
#ifdef NEMUVHWA_WITH_SHGSMI
    NEMUVHWACMD* pHdr = (NEMUVHWACMD*)NemuSHGSMICommandAlloc(&pDevExt->u.primary.hgsmiAdapterHeap,
                              cbCmd + NEMUVHWACMD_HEADSIZE(),
                              HGSMI_CH_VBVA,
                              VBVA_VHWA_CMD);
#else
    NEMUVHWACMD* pHdr = (NEMUVHWACMD*)NemuHGSMIBufferAlloc(&NemuCommonFromDeviceExt(pDevExt)->guestCtx,
                              cbCmd + NEMUVHWACMD_HEADSIZE(),
                              HGSMI_CH_VBVA,
                              VBVA_VHWA_CMD);
#endif
    Assert(pHdr);
    if (!pHdr)
    {
        LOGREL(("NemuHGSMIBufferAlloc failed"));
    }
    else
    {
        nemuVhwaHdrInit(pHdr, srcId, enmCmd);
    }

    return pHdr;
}

void nemuVhwaCommandFree(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd)
{
#ifdef NEMUVHWA_WITH_SHGSMI
    NemuSHGSMICommandFree(&pDevExt->u.primary.hgsmiAdapterHeap, pCmd);
#else
    vbvaVhwaCommandRelease(pDevExt, pCmd);
#endif
}

int nemuVhwaCommandSubmit(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd)
{
#ifdef NEMUVHWA_WITH_SHGSMI
    const NEMUSHGSMIHEADER* pHdr = NemuSHGSMICommandPrepSynch(&pDevExt->u.primary.hgsmiAdapterHeap, pCmd);
    Assert(pHdr);
    int rc = VERR_GENERAL_FAILURE;
    if (pHdr)
    {
        do
        {
            HGSMIOFFSET offCmd = NemuSHGSMICommandOffset(&pDevExt->u.primary.hgsmiAdapterHeap, pHdr);
            Assert(offCmd != HGSMIOFFSET_VOID);
            if (offCmd != HGSMIOFFSET_VOID)
            {
                rc = nemuVhwaCommandSubmitHgsmi(pDevExt, offCmd);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    NemuSHGSMICommandDoneSynch(&pDevExt->u.primary.hgsmiAdapterHeap, pHdr);
                    AssertRC(rc);
                    break;
                }
            }
            else
                rc = VERR_INVALID_PARAMETER;
            /* fail to submit, cancel it */
            NemuSHGSMICommandCancelSynch(&pDevExt->u.primary.hgsmiAdapterHeap, pHdr);
        } while (0);
    }
    else
        rc = VERR_INVALID_PARAMETER;
    return rc;
#else
    RTSEMEVENT hEvent;
    int rc = RTSemEventCreate(&hEvent);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        pCmd->Flags |= NEMUVHWACMD_FLAG_GH_ASYNCH_IRQ;
        nemuVhwaCommandSubmitAsynchByEvent(pDevExt, pCmd, hEvent);
        rc = RTSemEventWait(hEvent, RT_INDEFINITE_WAIT);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            RTSemEventDestroy(hEvent);
    }
    return rc;
#endif
}

#ifndef NEMUVHWA_WITH_SHGSMI
static DECLCALLBACK(void) nemuVhwaCompletionFreeCmd(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD * pCmd, void * pContext)
{
    nemuVhwaCommandFree(pDevExt, pCmd);
}

void nemuVhwaCompletionListProcess(PNEMUMP_DEVEXT pDevExt, NEMUVTLIST *pList)
{
    PNEMUVTLIST_ENTRY pNext, pCur;
    for (pCur = pList->pFirst; pCur; pCur = pNext)
    {
        /* need to save next since the command may be released in a pfnCallback and thus its data might be invalid */
        pNext = pCur->pNext;
        NEMUVHWACMD *pCmd = NEMUVHWA_LISTENTRY2CMD(pCur);
        PFNNEMUVHWACMDCOMPLETION pfnCallback = (PFNNEMUVHWACMDCOMPLETION)pCmd->GuestVBVAReserved1;
        void *pvCallback = (void*)pCmd->GuestVBVAReserved2;
        pfnCallback(pDevExt, pCmd, pvCallback);
    }
}

#endif

void nemuVhwaCommandSubmitAsynchAndComplete(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd)
{
#ifdef NEMUVHWA_WITH_SHGSMI
# error "port me"
#else
    pCmd->Flags |= NEMUVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION;

    nemuVhwaCommandSubmitAsynch(pDevExt, pCmd, nemuVhwaCompletionFreeCmd, NULL);
#endif
}

void nemuVhwaFreeHostInfo1(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD_QUERYINFO1* pInfo)
{
    NEMUVHWACMD* pCmd = NEMUVHWACMD_HEAD(pInfo);
    nemuVhwaCommandFree(pDevExt, pCmd);
}

void nemuVhwaFreeHostInfo2(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD_QUERYINFO2* pInfo)
{
    NEMUVHWACMD* pCmd = NEMUVHWACMD_HEAD(pInfo);
    nemuVhwaCommandFree(pDevExt, pCmd);
}

NEMUVHWACMD_QUERYINFO1* nemuVhwaQueryHostInfo1(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    NEMUVHWACMD* pCmd = nemuVhwaCommandCreate(pDevExt, srcId, NEMUVHWACMD_TYPE_QUERY_INFO1, sizeof(NEMUVHWACMD_QUERYINFO1));
    NEMUVHWACMD_QUERYINFO1 *pInfo1;

    Assert(pCmd);
    if (!pCmd)
    {
        LOGREL(("nemuVhwaCommandCreate failed"));
        return NULL;
    }

    pInfo1 = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO1);
    pInfo1->u.in.guestVersion.maj = NEMUVHWA_VERSION_MAJ;
    pInfo1->u.in.guestVersion.min = NEMUVHWA_VERSION_MIN;
    pInfo1->u.in.guestVersion.bld = NEMUVHWA_VERSION_BLD;
    pInfo1->u.in.guestVersion.reserved = NEMUVHWA_VERSION_RSV;

    int rc = nemuVhwaCommandSubmit(pDevExt, pCmd);
    AssertRC(rc);
    if(RT_SUCCESS(rc))
    {
        if(RT_SUCCESS(pCmd->rc))
        {
            return NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO1);
        }
    }

    nemuVhwaCommandFree(pDevExt, pCmd);
    return NULL;
}

NEMUVHWACMD_QUERYINFO2* nemuVhwaQueryHostInfo2(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId, uint32_t numFourCC)
{
    NEMUVHWACMD* pCmd = nemuVhwaCommandCreate(pDevExt, srcId, NEMUVHWACMD_TYPE_QUERY_INFO2, NEMUVHWAINFO2_SIZE(numFourCC));
    NEMUVHWACMD_QUERYINFO2 *pInfo2;
    Assert(pCmd);
    if (!pCmd)
    {
        LOGREL(("nemuVhwaCommandCreate failed"));
        return NULL;
    }

    pInfo2 = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO2);
    pInfo2->numFourCC = numFourCC;

    int rc = nemuVhwaCommandSubmit(pDevExt, pCmd);
    AssertRC(rc);
    if(RT_SUCCESS(rc))
    {
        AssertRC(pCmd->rc);
        if(RT_SUCCESS(pCmd->rc))
        {
            if(pInfo2->numFourCC == numFourCC)
            {
                return pInfo2;
            }
        }
    }

    nemuVhwaCommandFree(pDevExt, pCmd);
    return NULL;
}

int nemuVhwaEnable(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    int rc = VERR_GENERAL_FAILURE;
    NEMUVHWACMD* pCmd;

    pCmd = nemuVhwaCommandCreate(pDevExt, srcId, NEMUVHWACMD_TYPE_ENABLE, 0);
    Assert(pCmd);
    if (!pCmd)
    {
        LOGREL(("nemuVhwaCommandCreate failed"));
        return rc;
    }

    rc = nemuVhwaCommandSubmit(pDevExt, pCmd);
    AssertRC(rc);
    if(RT_SUCCESS(rc))
    {
        AssertRC(pCmd->rc);
        if(RT_SUCCESS(pCmd->rc))
            rc = VINF_SUCCESS;
        else
            rc = pCmd->rc;
    }

    nemuVhwaCommandFree(pDevExt, pCmd);
    return rc;
}

int nemuVhwaDisable(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    nemuVhwaCommandCheckCompletion(pDevExt);

    int rc = VERR_GENERAL_FAILURE;
    NEMUVHWACMD* pCmd;

    pCmd = nemuVhwaCommandCreate(pDevExt, srcId, NEMUVHWACMD_TYPE_DISABLE, 0);
    Assert(pCmd);
    if (!pCmd)
    {
        LOGREL(("nemuVhwaCommandCreate failed"));
        return rc;
    }

    rc = nemuVhwaCommandSubmit(pDevExt, pCmd);
    AssertRC(rc);
    if(RT_SUCCESS(rc))
    {
        if(RT_SUCCESS(pCmd->rc))
            rc = VINF_SUCCESS;
        else
            rc = pCmd->rc;
    }

    nemuVhwaCommandFree(pDevExt, pCmd);
    return rc;
}

DECLINLINE(VOID) nemuVhwaHlpOverlayListInit(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];
    pSource->cOverlays = 0;
    InitializeListHead(&pSource->OverlayList);
    KeInitializeSpinLock(&pSource->OverlayListLock);
}

static void nemuVhwaInitSrc(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    Assert(srcId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
    NEMUVHWA_INFO *pSettings = &pDevExt->aSources[srcId].Vhwa.Settings;
    memset (pSettings, 0, sizeof (NEMUVHWA_INFO));

    nemuVhwaHlpOverlayListInit(pDevExt, srcId);

    NEMUVHWACMD_QUERYINFO1* pInfo1 = nemuVhwaQueryHostInfo1(pDevExt, srcId);
    if (pInfo1)
    {
        if ((pInfo1->u.out.cfgFlags & NEMUVHWA_CFG_ENABLED)
                && pInfo1->u.out.numOverlays)
        {
            if ((pInfo1->u.out.caps & NEMUVHWA_CAPS_OVERLAY)
                    && (pInfo1->u.out.caps & NEMUVHWA_CAPS_OVERLAYSTRETCH)
                    && (pInfo1->u.out.surfaceCaps & NEMUVHWA_SCAPS_OVERLAY)
                    && (pInfo1->u.out.surfaceCaps & NEMUVHWA_SCAPS_FLIP)
                    && (pInfo1->u.out.surfaceCaps & NEMUVHWA_SCAPS_LOCALVIDMEM)
                    && pInfo1->u.out.numOverlays)
            {
                pSettings->fFlags |= NEMUVHWA_F_ENABLED;

                if (pInfo1->u.out.caps & NEMUVHWA_CAPS_COLORKEY)
                {
                    if (pInfo1->u.out.colorKeyCaps & NEMUVHWA_CKEYCAPS_SRCOVERLAY)
                    {
                        pSettings->fFlags |= NEMUVHWA_F_CKEY_SRC;
                        /* todo: NEMUVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE ? */
                    }

                    if (pInfo1->u.out.colorKeyCaps & NEMUVHWA_CKEYCAPS_DESTOVERLAY)
                    {
                        pSettings->fFlags |= NEMUVHWA_F_CKEY_DST;
                        /* todo: NEMUVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE ? */
                    }
                }

                pSettings->cOverlaysSupported = pInfo1->u.out.numOverlays;

                pSettings->cFormats = 0;

                pSettings->aFormats[pSettings->cFormats] = D3DDDIFMT_X8R8G8B8;
                ++pSettings->cFormats;

                if (pInfo1->u.out.numFourCC
                        && (pInfo1->u.out.caps & NEMUVHWA_CAPS_OVERLAYFOURCC))
                {
                    NEMUVHWACMD_QUERYINFO2* pInfo2 = nemuVhwaQueryHostInfo2(pDevExt, srcId, pInfo1->u.out.numFourCC);
                    if (pInfo2)
                    {
                        for (uint32_t i = 0; i < pInfo2->numFourCC; ++i)
                        {
                            pSettings->aFormats[pSettings->cFormats] = (D3DDDIFORMAT)pInfo2->FourCC[i];
                            ++pSettings->cFormats;
                        }
                        nemuVhwaFreeHostInfo2(pDevExt, pInfo2);
                    }
                }
            }
        }
        nemuVhwaFreeHostInfo1(pDevExt, pInfo1);
    }
}

void nemuVhwaInit(PNEMUMP_DEVEXT pDevExt)
{
    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        nemuVhwaInitSrc(pDevExt, (D3DDDI_VIDEO_PRESENT_SOURCE_ID)i);
    }
}

void nemuVhwaFree(PNEMUMP_DEVEXT pDevExt)
{
    /* we do not allocate/map anything, just issue a Disable command
     * to ensure all pending commands are flushed */
    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        nemuVhwaDisable(pDevExt, i);
    }
}

int nemuVhwaHlpTranslateFormat(NEMUVHWA_PIXELFORMAT *pFormat, D3DDDIFORMAT enmFormat)
{
    pFormat->Reserved = 0;
    switch (enmFormat)
    {
        case D3DDDIFMT_A8R8G8B8:
        case D3DDDIFMT_X8R8G8B8:
            pFormat->flags = NEMUVHWA_PF_RGB;
            pFormat->c.rgbBitCount = 32;
            pFormat->m1.rgbRBitMask = 0xff0000;
            pFormat->m2.rgbGBitMask = 0xff00;
            pFormat->m3.rgbBBitMask = 0xff;
            /* always zero for now */
            pFormat->m4.rgbABitMask = 0;
            return VINF_SUCCESS;
        case D3DDDIFMT_R8G8B8:
            pFormat->flags = NEMUVHWA_PF_RGB;
            pFormat->c.rgbBitCount = 24;
            pFormat->m1.rgbRBitMask = 0xff0000;
            pFormat->m2.rgbGBitMask = 0xff00;
            pFormat->m3.rgbBBitMask = 0xff;
            /* always zero for now */
            pFormat->m4.rgbABitMask = 0;
            return VINF_SUCCESS;
        case D3DDDIFMT_R5G6B5:
            pFormat->flags = NEMUVHWA_PF_RGB;
            pFormat->c.rgbBitCount = 16;
            pFormat->m1.rgbRBitMask = 0xf800;
            pFormat->m2.rgbGBitMask = 0x7e0;
            pFormat->m3.rgbBBitMask = 0x1f;
            /* always zero for now */
            pFormat->m4.rgbABitMask = 0;
            return VINF_SUCCESS;
        case D3DDDIFMT_P8:
        case D3DDDIFMT_A8:
        case D3DDDIFMT_X1R5G5B5:
        case D3DDDIFMT_A1R5G5B5:
        case D3DDDIFMT_A4R4G4B4:
        case D3DDDIFMT_R3G3B2:
        case D3DDDIFMT_A8R3G3B2:
        case D3DDDIFMT_X4R4G4B4:
        case D3DDDIFMT_A2B10G10R10:
        case D3DDDIFMT_A8B8G8R8:
        case D3DDDIFMT_X8B8G8R8:
        case D3DDDIFMT_G16R16:
        case D3DDDIFMT_A2R10G10B10:
        case D3DDDIFMT_A16B16G16R16:
        case D3DDDIFMT_A8P8:
        default:
        {
            uint32_t fourcc = nemuWddmFormatToFourcc(enmFormat);
            Assert(fourcc);
            if (fourcc)
            {
                pFormat->flags = NEMUVHWA_PF_FOURCC;
                pFormat->fourCC = fourcc;
                return VINF_SUCCESS;
            }
            return VERR_NOT_SUPPORTED;
        }
    }
}

int nemuVhwaHlpDestroySurface(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pSurf,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    Assert(pSurf->hHostHandle);
    if (!pSurf->hHostHandle)
        return VERR_INVALID_STATE;

    NEMUVHWACMD* pCmd = nemuVhwaCommandCreate(pDevExt, VidPnSourceId,
            NEMUVHWACMD_TYPE_SURF_DESTROY, sizeof(NEMUVHWACMD_SURF_DESTROY));
    Assert(pCmd);
    if(pCmd)
    {
        NEMUVHWACMD_SURF_DESTROY * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_DESTROY);

        memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_DESTROY));

        pBody->u.in.hSurf = pSurf->hHostHandle;

        /* we're not interested in completion, just send the command */
        nemuVhwaCommandSubmitAsynchAndComplete(pDevExt, pCmd);

        pSurf->hHostHandle = NEMUVHWA_SURFHANDLE_INVALID;

        return VINF_SUCCESS;
    }

    return VERR_OUT_OF_RESOURCES;
}

int nemuVhwaHlpPopulateSurInfo(NEMUVHWA_SURFACEDESC *pInfo, PNEMUWDDM_ALLOCATION pSurf,
        uint32_t fFlags, uint32_t cBackBuffers, uint32_t fSCaps,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    memset(pInfo, 0, sizeof(NEMUVHWA_SURFACEDESC));

#if 0
    /**
     * The following breaks 2D accelerated video playback because this method is called just after the surface was created
     * and most its members are still 0.
     *
     * @todo: Not 100% sure this is the correct way. It looks like the SegmentId specifies where the  memory
     *        for the surface is stored (VRAM vs. system memory) but because this method is only used
     *        to query some parameters (using NEMUVHWACMD_SURF_GETINFO) and this command doesn't access any surface memory
     *        on the host it should be safe.
     */
    if (pSurf->AllocData.Addr.SegmentId != 1)
    {
        WARN(("invalid segment id!"));
        return VERR_INVALID_PARAMETER;
    }
#endif

    pInfo->height = pSurf->AllocData.SurfDesc.height;
    pInfo->width = pSurf->AllocData.SurfDesc.width;
    pInfo->flags |= NEMUVHWA_SD_HEIGHT | NEMUVHWA_SD_WIDTH;
    if (fFlags & NEMUVHWA_SD_PITCH)
    {
        pInfo->pitch = pSurf->AllocData.SurfDesc.pitch;
        pInfo->flags |= NEMUVHWA_SD_PITCH;
        pInfo->sizeX = pSurf->AllocData.SurfDesc.cbSize;
        pInfo->sizeY = 1;
    }

    if (cBackBuffers)
    {
        pInfo->cBackBuffers = cBackBuffers;
        pInfo->flags |= NEMUVHWA_SD_BACKBUFFERCOUNT;
    }
    else
        pInfo->cBackBuffers = 0;
    pInfo->Reserved = 0;
        /* @todo: color keys */
//                        pInfo->DstOverlayCK;
//                        pInfo->DstBltCK;
//                        pInfo->SrcOverlayCK;
//                        pInfo->SrcBltCK;
    int rc = nemuVhwaHlpTranslateFormat(&pInfo->PixelFormat, pSurf->AllocData.SurfDesc.format);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        pInfo->flags |= NEMUVHWA_SD_PIXELFORMAT;
        pInfo->surfCaps = fSCaps;
        pInfo->flags |= NEMUVHWA_SD_CAPS;
        pInfo->offSurface = pSurf->AllocData.Addr.offVram;
    }

    return rc;
}

int nemuVhwaHlpCheckApplySurfInfo(PNEMUWDDM_ALLOCATION pSurf, NEMUVHWA_SURFACEDESC *pInfo,
        uint32_t fFlags, bool bApplyHostHandle)
{
    int rc = VINF_SUCCESS;
    if (!(fFlags & NEMUVHWA_SD_PITCH))
    {
        /* should be set by host */
//        Assert(pInfo->flags & NEMUVHWA_SD_PITCH);
        pSurf->AllocData.SurfDesc.cbSize = pInfo->sizeX * pInfo->sizeY;
        Assert(pSurf->AllocData.SurfDesc.cbSize);
        pSurf->AllocData.SurfDesc.pitch = pInfo->pitch;
        Assert(pSurf->AllocData.SurfDesc.pitch);
        /* @todo: make this properly */
        pSurf->AllocData.SurfDesc.bpp = pSurf->AllocData.SurfDesc.pitch * 8 / pSurf->AllocData.SurfDesc.width;
        Assert(pSurf->AllocData.SurfDesc.bpp);
    }
    else
    {
        Assert(pSurf->AllocData.SurfDesc.cbSize ==  pInfo->sizeX);
        Assert(pInfo->sizeY == 1);
        Assert(pInfo->pitch == pSurf->AllocData.SurfDesc.pitch);
        if (pSurf->AllocData.SurfDesc.cbSize !=  pInfo->sizeX
                || pInfo->sizeY != 1
                || pInfo->pitch != pSurf->AllocData.SurfDesc.pitch)
        {
            rc = VERR_INVALID_PARAMETER;
        }
    }

    if (bApplyHostHandle && RT_SUCCESS(rc))
    {
        pSurf->hHostHandle = pInfo->hSurf;
    }

    return rc;
}

int nemuVhwaHlpCreateSurface(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pSurf,
        uint32_t fFlags, uint32_t cBackBuffers, uint32_t fSCaps,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    /* the first thing we need is to post create primary */
    NEMUVHWACMD* pCmd = nemuVhwaCommandCreate(pDevExt, VidPnSourceId,
                NEMUVHWACMD_TYPE_SURF_CREATE, sizeof(NEMUVHWACMD_SURF_CREATE));
    Assert(pCmd);
    if (pCmd)
    {
        NEMUVHWACMD_SURF_CREATE * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_CREATE);
        int rc = VINF_SUCCESS;

        memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_CREATE));

        rc = nemuVhwaHlpPopulateSurInfo(&pBody->SurfInfo, pSurf,
                fFlags, cBackBuffers, fSCaps,
                VidPnSourceId);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            nemuVhwaCommandSubmit(pDevExt, pCmd);
            Assert(pCmd->rc == VINF_SUCCESS);
            if(pCmd->rc == VINF_SUCCESS)
            {
                rc = nemuVhwaHlpCheckApplySurfInfo(pSurf, &pBody->SurfInfo, fFlags, true);
            }
            else
                rc = pCmd->rc;
        }
        nemuVhwaCommandFree(pDevExt, pCmd);
        return rc;
    }

    return VERR_OUT_OF_RESOURCES;
}

int nemuVhwaHlpGetSurfInfoForSource(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pSurf, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    /* the first thing we need is to post create primary */
    NEMUVHWACMD* pCmd = nemuVhwaCommandCreate(pDevExt, VidPnSourceId,
            NEMUVHWACMD_TYPE_SURF_GETINFO, sizeof(NEMUVHWACMD_SURF_GETINFO));
    Assert(pCmd);
    if (pCmd)
    {
        NEMUVHWACMD_SURF_GETINFO * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_GETINFO);
        int rc = VINF_SUCCESS;

        memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_GETINFO));

        rc = nemuVhwaHlpPopulateSurInfo(&pBody->SurfInfo, pSurf,
                0, 0, NEMUVHWA_SCAPS_OVERLAY | NEMUVHWA_SCAPS_VIDEOMEMORY | NEMUVHWA_SCAPS_LOCALVIDMEM | NEMUVHWA_SCAPS_COMPLEX,
                VidPnSourceId);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            nemuVhwaCommandSubmit(pDevExt, pCmd);
            Assert(pCmd->rc == VINF_SUCCESS);
            if(pCmd->rc == VINF_SUCCESS)
            {
                rc = nemuVhwaHlpCheckApplySurfInfo(pSurf, &pBody->SurfInfo, 0, true);
            }
            else
                rc = pCmd->rc;
        }
        nemuVhwaCommandFree(pDevExt, pCmd);
        return rc;
    }

    return VERR_OUT_OF_RESOURCES;
}

int nemuVhwaHlpGetSurfInfo(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pSurf)
{
    for (int i = 0; i < NemuCommonFromDeviceExt(pDevExt)->cDisplays; ++i)
    {
        PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[i];
        if (pSource->Vhwa.Settings.fFlags & NEMUVHWA_F_ENABLED)
        {
            int rc = nemuVhwaHlpGetSurfInfoForSource(pDevExt, pSurf, i);
            AssertRC(rc);
            return rc;
        }
    }
    AssertBreakpoint();
    return VERR_NOT_SUPPORTED;
}

int nemuVhwaHlpDestroyPrimary(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SOURCE pSource, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PNEMUWDDM_ALLOCATION pFbSurf = NEMUVHWA_PRIMARY_ALLOCATION(pSource);

    int rc = nemuVhwaHlpDestroySurface(pDevExt, pFbSurf, VidPnSourceId);
    AssertRC(rc);
    return rc;
}

int nemuVhwaHlpCreatePrimary(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SOURCE pSource, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PNEMUWDDM_ALLOCATION pFbSurf = NEMUVHWA_PRIMARY_ALLOCATION(pSource);
    Assert(pSource->Vhwa.cOverlaysCreated == 1);
    Assert(pFbSurf->hHostHandle == NEMUVHWA_SURFHANDLE_INVALID);
    if (pFbSurf->hHostHandle != NEMUVHWA_SURFHANDLE_INVALID)
        return VERR_INVALID_STATE;

    int rc = nemuVhwaHlpCreateSurface(pDevExt, pFbSurf,
            NEMUVHWA_SD_PITCH, 0, NEMUVHWA_SCAPS_PRIMARYSURFACE | NEMUVHWA_SCAPS_VIDEOMEMORY | NEMUVHWA_SCAPS_LOCALVIDMEM,
            VidPnSourceId);
    AssertRC(rc);
    return rc;
}

int nemuVhwaHlpCheckInit(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    Assert(VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
    if (VidPnSourceId >= (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays)
        return VERR_INVALID_PARAMETER;

    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];

    Assert(!!(pSource->Vhwa.Settings.fFlags & NEMUVHWA_F_ENABLED));
    if (!(pSource->Vhwa.Settings.fFlags & NEMUVHWA_F_ENABLED))
        return VERR_NOT_SUPPORTED;

    int rc = VINF_SUCCESS;
    /* @todo: need a better sync */
    uint32_t cNew = ASMAtomicIncU32(&pSource->Vhwa.cOverlaysCreated);
    if (cNew == 1)
    {
        rc = nemuVhwaEnable(pDevExt, VidPnSourceId);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = nemuVhwaHlpCreatePrimary(pDevExt, pSource, VidPnSourceId);
            AssertRC(rc);
            if (RT_FAILURE(rc))
            {
                int tmpRc = nemuVhwaDisable(pDevExt, VidPnSourceId);
                AssertRC(tmpRc);
            }
        }
    }
    else
    {
        PNEMUWDDM_ALLOCATION pFbSurf = NEMUVHWA_PRIMARY_ALLOCATION(pSource);
        Assert(pFbSurf->hHostHandle);
        if (pFbSurf->hHostHandle)
            rc = VINF_ALREADY_INITIALIZED;
        else
            rc = VERR_INVALID_STATE;
    }

    if (RT_FAILURE(rc))
        ASMAtomicDecU32(&pSource->Vhwa.cOverlaysCreated);

    return rc;
}

int nemuVhwaHlpCheckTerm(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    Assert(VidPnSourceId < (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays);
    if (VidPnSourceId >= (D3DDDI_VIDEO_PRESENT_SOURCE_ID)NemuCommonFromDeviceExt(pDevExt)->cDisplays)
        return VERR_INVALID_PARAMETER;

    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];

    Assert(!!(pSource->Vhwa.Settings.fFlags & NEMUVHWA_F_ENABLED));

    /* @todo: need a better sync */
    uint32_t cNew = ASMAtomicDecU32(&pSource->Vhwa.cOverlaysCreated);
    int rc = VINF_SUCCESS;
    if (!cNew)
    {
        rc = nemuVhwaHlpDestroyPrimary(pDevExt, pSource, VidPnSourceId);
        AssertRC(rc);
    }
    else
    {
        Assert(cNew < UINT32_MAX / 2);
    }

    return rc;
}

int nemuVhwaHlpOverlayFlip(PNEMUWDDM_OVERLAY pOverlay, const DXGKARG_FLIPOVERLAY *pFlipInfo)
{
    PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pFlipInfo->hSource;
    Assert(pAlloc->hHostHandle);
    Assert(pAlloc->pResource);
    Assert(pAlloc->pResource == pOverlay->pResource);
    Assert(pFlipInfo->PrivateDriverDataSize == sizeof (NEMUWDDM_OVERLAYFLIP_INFO));
    Assert(pFlipInfo->pPrivateDriverData);
    PNEMUWDDM_SOURCE pSource = &pOverlay->pDevExt->aSources[pOverlay->VidPnSourceId];
    Assert(!!(pSource->Vhwa.Settings.fFlags & NEMUVHWA_F_ENABLED));
    PNEMUWDDM_ALLOCATION pFbSurf = NEMUVHWA_PRIMARY_ALLOCATION(pSource);
    Assert(pFbSurf);
    Assert(pFbSurf->hHostHandle);
    Assert(pFbSurf->AllocData.Addr.offVram != NEMUVIDEOOFFSET_VOID);
    Assert(pOverlay->pCurentAlloc);
    Assert(pOverlay->pCurentAlloc->pResource == pOverlay->pResource);
    Assert(pOverlay->pCurentAlloc != pAlloc);
    int rc = VINF_SUCCESS;

    if (pFbSurf->AllocData.Addr.SegmentId != 1)
    {
        WARN(("invalid segment id on flip"));
        return VERR_INVALID_PARAMETER;
    }

    if (pFlipInfo->PrivateDriverDataSize == sizeof (NEMUWDDM_OVERLAYFLIP_INFO))
    {
        PNEMUWDDM_OVERLAYFLIP_INFO pOurInfo = (PNEMUWDDM_OVERLAYFLIP_INFO)pFlipInfo->pPrivateDriverData;

        NEMUVHWACMD* pCmd = nemuVhwaCommandCreate(pOverlay->pDevExt, pOverlay->VidPnSourceId,
                NEMUVHWACMD_TYPE_SURF_FLIP, sizeof(NEMUVHWACMD_SURF_FLIP));
        Assert(pCmd);
        if(pCmd)
        {
            NEMUVHWACMD_SURF_FLIP * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_FLIP);

            memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_FLIP));

//            pBody->TargGuestSurfInfo;
//            pBody->CurrGuestSurfInfo;
            pBody->u.in.hTargSurf = pAlloc->hHostHandle;
            pBody->u.in.offTargSurface = pFlipInfo->SrcPhysicalAddress.QuadPart;
            pAlloc->AllocData.Addr.offVram = pFlipInfo->SrcPhysicalAddress.QuadPart;
            pBody->u.in.hCurrSurf = pOverlay->pCurentAlloc->hHostHandle;
            pBody->u.in.offCurrSurface = pOverlay->pCurentAlloc->AllocData.Addr.offVram;
            if (pOurInfo->DirtyRegion.fFlags & NEMUWDDM_DIRTYREGION_F_VALID)
            {
                pBody->u.in.xUpdatedTargMemValid = 1;
                if (pOurInfo->DirtyRegion.fFlags & NEMUWDDM_DIRTYREGION_F_RECT_VALID)
                    pBody->u.in.xUpdatedTargMemRect = *(NEMUVHWA_RECTL*)((void*)&pOurInfo->DirtyRegion.Rect);
                else
                {
                    pBody->u.in.xUpdatedTargMemRect.right = pAlloc->AllocData.SurfDesc.width;
                    pBody->u.in.xUpdatedTargMemRect.bottom = pAlloc->AllocData.SurfDesc.height;
                    /* top & left are zero-inited with the above memset */
                }
            }

            /* we're not interested in completion, just send the command */
            nemuVhwaCommandSubmitAsynchAndComplete(pOverlay->pDevExt, pCmd);

            pOverlay->pCurentAlloc = pAlloc;

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_OUT_OF_RESOURCES;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

AssertCompile(sizeof (RECT) == sizeof (NEMUVHWA_RECTL));
AssertCompile(RT_SIZEOFMEMB(RECT, left) == RT_SIZEOFMEMB(NEMUVHWA_RECTL, left));
AssertCompile(RT_SIZEOFMEMB(RECT, right) == RT_SIZEOFMEMB(NEMUVHWA_RECTL, right));
AssertCompile(RT_SIZEOFMEMB(RECT, top) == RT_SIZEOFMEMB(NEMUVHWA_RECTL, top));
AssertCompile(RT_SIZEOFMEMB(RECT, bottom) == RT_SIZEOFMEMB(NEMUVHWA_RECTL, bottom));
AssertCompile(RT_OFFSETOF(RECT, left) == RT_OFFSETOF(NEMUVHWA_RECTL, left));
AssertCompile(RT_OFFSETOF(RECT, right) == RT_OFFSETOF(NEMUVHWA_RECTL, right));
AssertCompile(RT_OFFSETOF(RECT, top) == RT_OFFSETOF(NEMUVHWA_RECTL, top));
AssertCompile(RT_OFFSETOF(RECT, bottom) == RT_OFFSETOF(NEMUVHWA_RECTL, bottom));

int nemuVhwaHlpColorFill(PNEMUWDDM_OVERLAY pOverlay, PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL pCF)
{
    PNEMUWDDM_ALLOCATION pAlloc = pCF->ClrFill.Alloc.pAlloc;
    Assert(pAlloc->pResource == pOverlay->pResource);

    if (pAlloc->AllocData.Addr.SegmentId != 1)
    {
        WARN(("invalid segment id on color fill"));
        return VERR_INVALID_PARAMETER;
    }

    Assert(pAlloc->hHostHandle);
    Assert(pAlloc->pResource);
    Assert(pAlloc->AllocData.Addr.offVram != NEMUVIDEOOFFSET_VOID);

    int rc;
    NEMUVHWACMD* pCmd = nemuVhwaCommandCreate(pOverlay->pDevExt, pOverlay->VidPnSourceId,
                NEMUVHWACMD_TYPE_SURF_FLIP, RT_OFFSETOF(NEMUVHWACMD_SURF_COLORFILL, u.in.aRects[pCF->ClrFill.Rects.cRects]));
    Assert(pCmd);
    if(pCmd)
    {
        NEMUVHWACMD_SURF_COLORFILL * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_COLORFILL);

        memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_COLORFILL));

        pBody->u.in.hSurf = pAlloc->hHostHandle;
        pBody->u.in.offSurface = pAlloc->AllocData.Addr.offVram;
        pBody->u.in.cRects = pCF->ClrFill.Rects.cRects;
        memcpy (pBody->u.in.aRects, pCF->ClrFill.Rects.aRects, pCF->ClrFill.Rects.cRects * sizeof (pCF->ClrFill.Rects.aRects[0]));
        nemuVhwaCommandSubmitAsynchAndComplete(pOverlay->pDevExt, pCmd);

        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_OUT_OF_RESOURCES;

    return rc;
}

static void nemuVhwaHlpOverlayDstRectSet(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_OVERLAY pOverlay, const RECT *pRect)
{
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pOverlay->VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    pOverlay->DstRect = *pRect;
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}

static void nemuVhwaHlpOverlayListAdd(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_OVERLAY pOverlay)
{
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pOverlay->VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    ASMAtomicIncU32(&pSource->cOverlays);
    InsertHeadList(&pSource->OverlayList, &pOverlay->ListEntry);
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}

static void nemuVhwaHlpOverlayListRemove(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_OVERLAY pOverlay)
{
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pOverlay->VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    ASMAtomicDecU32(&pSource->cOverlays);
    RemoveEntryList(&pOverlay->ListEntry);
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}

AssertCompile(sizeof (RECT) == sizeof (NEMUVHWA_RECTL));
AssertCompile(RT_SIZEOFMEMB(RECT, left) == RT_SIZEOFMEMB(NEMUVHWA_RECTL, left));
AssertCompile(RT_SIZEOFMEMB(RECT, right) == RT_SIZEOFMEMB(NEMUVHWA_RECTL, right));
AssertCompile(RT_SIZEOFMEMB(RECT, top) == RT_SIZEOFMEMB(NEMUVHWA_RECTL, top));
AssertCompile(RT_SIZEOFMEMB(RECT, bottom) == RT_SIZEOFMEMB(NEMUVHWA_RECTL, bottom));
AssertCompile(RT_OFFSETOF(RECT, left) == RT_OFFSETOF(NEMUVHWA_RECTL, left));
AssertCompile(RT_OFFSETOF(RECT, right) == RT_OFFSETOF(NEMUVHWA_RECTL, right));
AssertCompile(RT_OFFSETOF(RECT, top) == RT_OFFSETOF(NEMUVHWA_RECTL, top));
AssertCompile(RT_OFFSETOF(RECT, bottom) == RT_OFFSETOF(NEMUVHWA_RECTL, bottom));

int nemuVhwaHlpOverlayUpdate(PNEMUWDDM_OVERLAY pOverlay, const DXGK_OVERLAYINFO *pOverlayInfo, RECT * pDstUpdateRect)
{
    PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pOverlayInfo->hAllocation;
    Assert(pAlloc->hHostHandle);
    Assert(pAlloc->pResource);
    Assert(pAlloc->pResource == pOverlay->pResource);
    Assert(pOverlayInfo->PrivateDriverDataSize == sizeof (NEMUWDDM_OVERLAY_INFO));
    Assert(pOverlayInfo->pPrivateDriverData);
    PNEMUWDDM_SOURCE pSource = &pOverlay->pDevExt->aSources[pOverlay->VidPnSourceId];
    Assert(!!(pSource->Vhwa.Settings.fFlags & NEMUVHWA_F_ENABLED));
    PNEMUWDDM_ALLOCATION pFbSurf = NEMUVHWA_PRIMARY_ALLOCATION(pSource);
    Assert(pFbSurf);
    Assert(pFbSurf->hHostHandle);
    Assert(pFbSurf->AllocData.Addr.offVram != NEMUVIDEOOFFSET_VOID);
    int rc = VINF_SUCCESS;

    if (pFbSurf->AllocData.Addr.SegmentId != 1)
    {
        WARN(("invalid segment id on overlay update"));
        return VERR_INVALID_PARAMETER;
    }

    if (pOverlayInfo->PrivateDriverDataSize == sizeof (NEMUWDDM_OVERLAY_INFO))
    {
        PNEMUWDDM_OVERLAY_INFO pOurInfo = (PNEMUWDDM_OVERLAY_INFO)pOverlayInfo->pPrivateDriverData;

        NEMUVHWACMD* pCmd = nemuVhwaCommandCreate(pOverlay->pDevExt, pOverlay->VidPnSourceId,
                NEMUVHWACMD_TYPE_SURF_OVERLAY_UPDATE, sizeof(NEMUVHWACMD_SURF_OVERLAY_UPDATE));
        Assert(pCmd);
        if(pCmd)
        {
            NEMUVHWACMD_SURF_OVERLAY_UPDATE * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_OVERLAY_UPDATE);

            memset(pBody, 0, sizeof(NEMUVHWACMD_SURF_OVERLAY_UPDATE));

            pBody->u.in.hDstSurf = pFbSurf->hHostHandle;
            pBody->u.in.offDstSurface = pFbSurf->AllocData.Addr.offVram;
            pBody->u.in.dstRect = *(NEMUVHWA_RECTL*)((void*)&pOverlayInfo->DstRect);
            pBody->u.in.hSrcSurf = pAlloc->hHostHandle;
            pBody->u.in.offSrcSurface = pOverlayInfo->PhysicalAddress.QuadPart;
            pAlloc->AllocData.Addr.offVram = pOverlayInfo->PhysicalAddress.QuadPart;
            pBody->u.in.srcRect = *(NEMUVHWA_RECTL*)((void*)&pOverlayInfo->SrcRect);
            pBody->u.in.flags |= NEMUVHWA_OVER_SHOW;
            if (pOurInfo->OverlayDesc.fFlags & NEMUWDDM_OVERLAY_F_CKEY_DST)
            {
                pBody->u.in.flags |= NEMUVHWA_OVER_KEYDESTOVERRIDE /* ?? NEMUVHWA_OVER_KEYDEST */;
                pBody->u.in.desc.DstCK.high = pOurInfo->OverlayDesc.DstColorKeyHigh;
                pBody->u.in.desc.DstCK.low = pOurInfo->OverlayDesc.DstColorKeyLow;
            }

            if (pOurInfo->OverlayDesc.fFlags & NEMUWDDM_OVERLAY_F_CKEY_SRC)
            {
                pBody->u.in.flags |= NEMUVHWA_OVER_KEYSRCOVERRIDE /* ?? NEMUVHWA_OVER_KEYSRC */;
                pBody->u.in.desc.SrcCK.high = pOurInfo->OverlayDesc.SrcColorKeyHigh;
                pBody->u.in.desc.SrcCK.low = pOurInfo->OverlayDesc.SrcColorKeyLow;
            }

            if (pOurInfo->DirtyRegion.fFlags & NEMUWDDM_DIRTYREGION_F_VALID)
            {
                pBody->u.in.xFlags |= NEMUVHWACMD_SURF_OVERLAY_UPDATE_F_SRCMEMRECT;
                if (pOurInfo->DirtyRegion.fFlags & NEMUWDDM_DIRTYREGION_F_RECT_VALID)
                    pBody->u.in.xUpdatedSrcMemRect = *(NEMUVHWA_RECTL*)((void*)&pOurInfo->DirtyRegion.Rect);
                else
                {
                    pBody->u.in.xUpdatedSrcMemRect.right = pAlloc->AllocData.SurfDesc.width;
                    pBody->u.in.xUpdatedSrcMemRect.bottom = pAlloc->AllocData.SurfDesc.height;
                    /* top & left are zero-inited with the above memset */
                }
            }

            if (pDstUpdateRect)
            {
                pBody->u.in.xFlags |= NEMUVHWACMD_SURF_OVERLAY_UPDATE_F_DSTMEMRECT;
                pBody->u.in.xUpdatedDstMemRect = *(NEMUVHWA_RECTL*)((void*)pDstUpdateRect);
            }

            /* we're not interested in completion, just send the command */
            nemuVhwaCommandSubmitAsynchAndComplete(pOverlay->pDevExt, pCmd);

            pOverlay->pCurentAlloc = pAlloc;

            nemuVhwaHlpOverlayDstRectSet(pOverlay->pDevExt, pOverlay, &pOverlayInfo->DstRect);

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_OUT_OF_RESOURCES;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

int nemuVhwaHlpOverlayUpdate(PNEMUWDDM_OVERLAY pOverlay, const DXGK_OVERLAYINFO *pOverlayInfo)
{
    return nemuVhwaHlpOverlayUpdate(pOverlay, pOverlayInfo, NULL);
}

int nemuVhwaHlpOverlayDestroy(PNEMUWDDM_OVERLAY pOverlay)
{
    int rc = VINF_SUCCESS;

    nemuVhwaHlpOverlayListRemove(pOverlay->pDevExt, pOverlay);

    for (uint32_t i = 0; i < pOverlay->pResource->cAllocations; ++i)
    {
        PNEMUWDDM_ALLOCATION pCurAlloc = &pOverlay->pResource->aAllocations[i];
        rc = nemuVhwaHlpDestroySurface(pOverlay->pDevExt, pCurAlloc, pOverlay->VidPnSourceId);
        AssertRC(rc);
    }

    if (RT_SUCCESS(rc))
    {
        int tmpRc = nemuVhwaHlpCheckTerm(pOverlay->pDevExt, pOverlay->VidPnSourceId);
        AssertRC(tmpRc);
    }

    return rc;
}


int nemuVhwaHlpOverlayCreate(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, DXGK_OVERLAYINFO *pOverlayInfo,
        /* OUT */ PNEMUWDDM_OVERLAY pOverlay)
{
    int rc = nemuVhwaHlpCheckInit(pDevExt, VidPnSourceId);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        PNEMUWDDM_ALLOCATION pAlloc = (PNEMUWDDM_ALLOCATION)pOverlayInfo->hAllocation;
        PNEMUWDDM_RESOURCE pRc = pAlloc->pResource;
        Assert(pRc);
        for (uint32_t i = 0; i < pRc->cAllocations; ++i)
        {
            PNEMUWDDM_ALLOCATION pCurAlloc = &pRc->aAllocations[i];
            rc = nemuVhwaHlpCreateSurface(pDevExt, pCurAlloc,
                        0, pRc->cAllocations - 1, NEMUVHWA_SCAPS_OVERLAY | NEMUVHWA_SCAPS_VIDEOMEMORY | NEMUVHWA_SCAPS_LOCALVIDMEM | NEMUVHWA_SCAPS_COMPLEX,
                        VidPnSourceId);
            AssertRC(rc);
            if (!RT_SUCCESS(rc))
            {
                int tmpRc;
                for (uint32_t j = 0; j < i; ++j)
                {
                    PNEMUWDDM_ALLOCATION pDestroyAlloc = &pRc->aAllocations[j];
                    tmpRc = nemuVhwaHlpDestroySurface(pDevExt, pDestroyAlloc, VidPnSourceId);
                    AssertRC(tmpRc);
                }
                break;
            }
        }

        if (RT_SUCCESS(rc))
        {
            pOverlay->pDevExt = pDevExt;
            pOverlay->pResource = pRc;
            pOverlay->VidPnSourceId = VidPnSourceId;

            nemuVhwaHlpOverlayListAdd(pDevExt, pOverlay);

            RECT DstRect;
            nemuVhwaHlpOverlayDstRectGet(pDevExt, pOverlay, &DstRect);

            rc = nemuVhwaHlpOverlayUpdate(pOverlay, pOverlayInfo, DstRect.right ? &DstRect : NULL);
            if (!RT_SUCCESS(rc))
            {
                int tmpRc = nemuVhwaHlpOverlayDestroy(pOverlay);
                AssertRC(tmpRc);
            }
        }

        if (RT_FAILURE(rc))
        {
            int tmpRc = nemuVhwaHlpCheckTerm(pDevExt, VidPnSourceId);
            AssertRC(tmpRc);
            AssertRC(rc);
        }
    }

    return rc;
}

BOOLEAN nemuVhwaHlpOverlayListIsEmpty(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];
    return !ASMAtomicReadU32(&pSource->cOverlays);
}

#define NEMUWDDM_OVERLAY_FROM_ENTRY(_pEntry) ((PNEMUWDDM_OVERLAY)(((uint8_t*)(_pEntry)) - RT_OFFSETOF(NEMUWDDM_OVERLAY, ListEntry)))

void nemuVhwaHlpOverlayDstRectUnion(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, RECT *pRect)
{
    if (nemuVhwaHlpOverlayListIsEmpty(pDevExt, VidPnSourceId))
    {
        memset(pRect, 0, sizeof (*pRect));
        return;
    }

    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    if (pSource->cOverlays)
    {
        PNEMUWDDM_OVERLAY pOverlay = NEMUWDDM_OVERLAY_FROM_ENTRY(pSource->OverlayList.Flink);
        *pRect = pOverlay->DstRect;
        while (pOverlay->ListEntry.Flink != &pSource->OverlayList)
        {
            pOverlay = NEMUWDDM_OVERLAY_FROM_ENTRY(pOverlay->ListEntry.Flink);
            nemuWddmRectUnite(pRect, &pOverlay->DstRect);
        }
    }
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}

void nemuVhwaHlpOverlayDstRectGet(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_OVERLAY pOverlay, RECT *pRect)
{
    PNEMUWDDM_SOURCE pSource = &pDevExt->aSources[pOverlay->VidPnSourceId];
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->OverlayListLock, &OldIrql);
    *pRect = pOverlay->DstRect;
    KeReleaseSpinLock(&pSource->OverlayListLock, OldIrql);
}
