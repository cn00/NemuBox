/* $Id: NemuDispVHWA.cpp $ */

/** @file
 * Nemu XPDM Display driver
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
#include "NemuDispMini.h"
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>

static void NemuDispVHWACommandFree(PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd)
{
    NemuHGSMIBufferFree(&pDev->hgsmi.ctx, pCmd);
}

static void NemuDispVHWACommandRetain(PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd)
{
    ASMAtomicIncU32(&pCmd->cRefs);
}

static void NemuDispVHWACommandSubmitAsynchByEvent(PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd, NEMUPEVENT pEvent)
{
    pCmd->GuestVBVAReserved1 = (uintptr_t)pEvent;
    pCmd->GuestVBVAReserved2 = 0;
    /* ensure the command is not removed until we're processing it */
    NemuDispVHWACommandRetain(pDev, pCmd);

    /* complete it asynchronously by setting event */
    pCmd->Flags |= NEMUVHWACMD_FLAG_GH_ASYNCH_EVENT;
    NemuHGSMIBufferSubmit(&pDev->hgsmi.ctx, pCmd);

    if(!(ASMAtomicReadU32((volatile uint32_t *)&pCmd->Flags)  & NEMUVHWACMD_FLAG_HG_ASYNCH))
    {
        /* the command is completed */
        pDev->vpAPI.VideoPortProcs.pfnSetEvent(pDev->vpAPI.pContext, pEvent);
    }

    NemuDispVHWACommandRelease(pDev, pCmd);
}

static void NemuDispVHWAHanldeVHWACmdCompletion(PNEMUDISPDEV pDev, VBVAHOSTCMD * pHostCmd)
{
    VBVAHOSTCMDVHWACMDCOMPLETE * pComplete = VBVAHOSTCMD_BODY(pHostCmd, VBVAHOSTCMDVHWACMDCOMPLETE);
    NEMUVHWACMD* pComplCmd = (NEMUVHWACMD*)HGSMIOffsetToPointer(&pDev->hgsmi.ctx.heapCtx.area, pComplete->offCmd);
    PFNNEMUVHWACMDCOMPLETION pfnCompletion = (PFNNEMUVHWACMDCOMPLETION)pComplCmd->GuestVBVAReserved1;
    void *pContext = (void *)pComplCmd->GuestVBVAReserved2;

    pfnCompletion(pDev, pComplCmd, pContext);

    NemuDispVBVAHostCommandComplete(pDev, pHostCmd);
}

static void NemuVHWAHostCommandHandler(PNEMUDISPDEV pDev, VBVAHOSTCMD * pCmd)
{
    switch(pCmd->customOpCode)
    {
        case VBVAHG_DCUSTOM_VHWA_CMDCOMPLETE:
        {
            NemuDispVHWAHanldeVHWACmdCompletion(pDev, pCmd);
            break;
        }
        default:
        {
            NemuDispVBVAHostCommandComplete(pDev, pCmd);
        }
    }
}

void NemuDispVHWAInit(PNEMUDISPDEV pDev)
{
    VHWAQUERYINFO info;
    int rc;

    rc = NemuDispMPVHWAQueryInfo(pDev->hDriver, &info);
    NEMU_WARNRC(rc);

    if (RT_SUCCESS(rc))
    {
        pDev->vhwa.offVramBase = info.offVramBase;
    }
}

int NemuDispVHWAEnable(PNEMUDISPDEV pDev)
{
    int rc = VERR_GENERAL_FAILURE;
    NEMUVHWACMD* pCmd;

    if (!pDev->hgsmi.bSupported)
    {
        return VERR_NOT_SUPPORTED;
    }

    pCmd = NemuDispVHWACommandCreate(pDev, NEMUVHWACMD_TYPE_ENABLE, 0);
    if (!pCmd)
    {
        WARN(("NemuDispVHWACommandCreate failed"));
        return rc;
    }

    if(NemuDispVHWACommandSubmit(pDev, pCmd))
    {
        if(RT_SUCCESS(pCmd->rc))
        {
            rc = VINF_SUCCESS;
        }
    }

    NemuDispVHWACommandRelease(pDev, pCmd);
    return rc;
}

NEMUVHWACMD* NemuDispVHWACommandCreate(PNEMUDISPDEV pDev, NEMUVHWACMD_TYPE enmCmd, NEMUVHWACMD_LENGTH cbCmd)
{
    NEMUVHWACMD* pHdr = (NEMUVHWACMD*)NemuHGSMIBufferAlloc(&pDev->hgsmi.ctx,
                              cbCmd + NEMUVHWACMD_HEADSIZE(),
                              HGSMI_CH_VBVA,
                              VBVA_VHWA_CMD);
    if (!pHdr)
    {
        WARN(("HGSMIHeapAlloc failed"));
    }
    else
    {
        memset(pHdr, 0, sizeof(NEMUVHWACMD));
        pHdr->iDisplay = pDev->iDevice;
        pHdr->rc = VERR_GENERAL_FAILURE;
        pHdr->enmCmd = enmCmd;
        pHdr->cRefs = 1;
    }

    /* @todo: temporary hack */
    NemuDispVHWACommandCheckHostCmds(pDev);

    return pHdr;
}

void NemuDispVHWACommandRelease(PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd)
{
    uint32_t cRefs = ASMAtomicDecU32(&pCmd->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if(!cRefs)
    {
        NemuDispVHWACommandFree(pDev, pCmd);
    }
}

BOOL NemuDispVHWACommandSubmit(PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd)
{
    NEMUPEVENT pEvent;
    NEMUVP_STATUS rc = pDev->vpAPI.VideoPortProcs.pfnCreateEvent(pDev->vpAPI.pContext, NEMUNOTIFICATION_EVENT, NULL, &pEvent);
    /* don't assert here, otherwise NT4 will be unhappy */
    if(rc == NEMUNO_ERROR)
    {
        pCmd->Flags |= NEMUVHWACMD_FLAG_GH_ASYNCH_IRQ;
        NemuDispVHWACommandSubmitAsynchByEvent(pDev, pCmd, pEvent);

        rc = pDev->vpAPI.VideoPortProcs.pfnWaitForSingleObject(pDev->vpAPI.pContext, pEvent,
                NULL /*IN PLARGE_INTEGER  pTimeOut*/
                );
        Assert(rc == NEMUNO_ERROR);
        if(rc == NEMUNO_ERROR)
        {
            pDev->vpAPI.VideoPortProcs.pfnDeleteEvent(pDev->vpAPI.pContext, pEvent);
        }
    }
    return rc == NEMUNO_ERROR;
}

void NemuDispVHWACommandCheckHostCmds(PNEMUDISPDEV pDev)
{
    VBVAHOSTCMD *pCmd, *pNextCmd;
    int rc = pDev->hgsmi.mp.pfnRequestCommandsHandler(pDev->hgsmi.mp.hContext, HGSMI_CH_VBVA, pDev->iDevice, &pCmd);
    /* don't assert here, otherwise NT4 will be unhappy */
    if(RT_SUCCESS(rc))
    {
        for(;pCmd; pCmd = pNextCmd)
        {
            pNextCmd = pCmd->u.pNext;
            NemuVHWAHostCommandHandler(pDev, pCmd);
        }
    }
}

static DECLCALLBACK(void) NemuDispVHWACommandCompletionCallbackEvent(PNEMUDISPDEV pDev, NEMUVHWACMD * pCmd, void * pContext)
{
    NEMUPEVENT pEvent = (NEMUPEVENT)pContext;
    LONG oldState = pDev->vpAPI.VideoPortProcs.pfnSetEvent(pDev->vpAPI.pContext, pEvent);
    Assert(!oldState);
}

/* do not wait for completion */
void NemuDispVHWACommandSubmitAsynch (PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd, PFNNEMUVHWACMDCOMPLETION pfnCompletion, void * pContext)
{
    pCmd->GuestVBVAReserved1 = (uintptr_t)pfnCompletion;
    pCmd->GuestVBVAReserved2 = (uintptr_t)pContext;
    NemuDispVHWACommandRetain(pDev, pCmd);

    NemuHGSMIBufferSubmit(&pDev->hgsmi.ctx, pCmd);

    if(!(pCmd->Flags & NEMUVHWACMD_FLAG_HG_ASYNCH))
    {
        /* the command is completed */
        pfnCompletion(pDev, pCmd, pContext);
    }

    NemuDispVHWACommandRelease(pDev, pCmd);
}

static DECLCALLBACK(void) NemuDispVHWAFreeCmdCompletion(PNEMUDISPDEV pDev, NEMUVHWACMD * pCmd, void * pContext)
{
    NemuDispVHWACommandRelease(pDev, pCmd);
}

void NemuDispVHWACommandSubmitAsynchAndComplete (PNEMUDISPDEV pDev, NEMUVHWACMD* pCmd)
{
    pCmd->GuestVBVAReserved1 = (uintptr_t)NemuDispVHWAFreeCmdCompletion;

    NemuDispVHWACommandRetain(pDev, pCmd);

    pCmd->Flags |= NEMUVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION;

    NemuHGSMIBufferSubmit(&pDev->hgsmi.ctx, pCmd);

    if(!(pCmd->Flags & NEMUVHWACMD_FLAG_HG_ASYNCH)
            || pCmd->Flags & NEMUVHWACMD_FLAG_HG_ASYNCH_RETURNED)
    {
        /* the command is completed */
        NemuDispVHWAFreeCmdCompletion(pDev, pCmd, NULL);
    }

    NemuDispVHWACommandRelease(pDev, pCmd);
}

void NemuDispVHWAFreeHostInfo1(PNEMUDISPDEV pDev, NEMUVHWACMD_QUERYINFO1* pInfo)
{
    NEMUVHWACMD* pCmd = NEMUVHWACMD_HEAD(pInfo);
    NemuDispVHWACommandRelease(pDev, pCmd);
}

void NemuDispVHWAFreeHostInfo2(PNEMUDISPDEV pDev, NEMUVHWACMD_QUERYINFO2* pInfo)
{
    NEMUVHWACMD* pCmd = NEMUVHWACMD_HEAD(pInfo);
    NemuDispVHWACommandRelease(pDev, pCmd);
}

NEMUVHWACMD_QUERYINFO1* NemuDispVHWAQueryHostInfo1(PNEMUDISPDEV pDev)
{
    NEMUVHWACMD* pCmd = NemuDispVHWACommandCreate (pDev, NEMUVHWACMD_TYPE_QUERY_INFO1, sizeof(NEMUVHWACMD_QUERYINFO1));
    NEMUVHWACMD_QUERYINFO1 *pInfo1;
    if (!pCmd)
    {
        WARN(("NemuDispVHWACommandCreate failed"));
        return NULL;
    }

    pInfo1 = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO1);
    pInfo1->u.in.guestVersion.maj = NEMUVHWA_VERSION_MAJ;
    pInfo1->u.in.guestVersion.min = NEMUVHWA_VERSION_MIN;
    pInfo1->u.in.guestVersion.bld = NEMUVHWA_VERSION_BLD;
    pInfo1->u.in.guestVersion.reserved = NEMUVHWA_VERSION_RSV;

    if(NemuDispVHWACommandSubmit (pDev, pCmd))
    {
        if(RT_SUCCESS(pCmd->rc))
        {
            return NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO1);
        }
    }

    NemuDispVHWACommandRelease(pDev, pCmd);
    return NULL;
}

NEMUVHWACMD_QUERYINFO2* NemuDispVHWAQueryHostInfo2(PNEMUDISPDEV pDev, uint32_t numFourCC)
{
    NEMUVHWACMD* pCmd = NemuDispVHWACommandCreate (pDev, NEMUVHWACMD_TYPE_QUERY_INFO2, NEMUVHWAINFO2_SIZE(numFourCC));
    NEMUVHWACMD_QUERYINFO2 *pInfo2;
    if (!pCmd)
    {
        WARN(("NemuDispVHWACommandCreate failed"));
        return NULL;
    }

    pInfo2 = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO2);
    pInfo2->numFourCC = numFourCC;

    if(NemuDispVHWACommandSubmit (pDev, pCmd))
    {
        if(RT_SUCCESS(pCmd->rc))
        {
            if(pInfo2->numFourCC == numFourCC)
            {
                return pInfo2;
            }
        }
    }

    NemuDispVHWACommandRelease(pDev, pCmd);
    return NULL;
}

int NemuDispVHWAInitHostInfo1(PNEMUDISPDEV pDev)
{
    NEMUVHWACMD_QUERYINFO1* pInfo;

    if (!pDev->hgsmi.bSupported)
        return VERR_NOT_SUPPORTED;

    pInfo = NemuDispVHWAQueryHostInfo1(pDev);
    if(!pInfo)
    {
        pDev->vhwa.bEnabled = false;
        return VERR_OUT_OF_RESOURCES;
    }

    pDev->vhwa.caps = pInfo->u.out.caps;
    pDev->vhwa.caps2 = pInfo->u.out.caps2;
    pDev->vhwa.colorKeyCaps = pInfo->u.out.colorKeyCaps;
    pDev->vhwa.stretchCaps = pInfo->u.out.stretchCaps;
    pDev->vhwa.surfaceCaps = pInfo->u.out.surfaceCaps;
    pDev->vhwa.numOverlays = pInfo->u.out.numOverlays;
    pDev->vhwa.numFourCC = pInfo->u.out.numFourCC;
    pDev->vhwa.bEnabled = (pInfo->u.out.cfgFlags & NEMUVHWA_CFG_ENABLED);
    NemuDispVHWAFreeHostInfo1(pDev, pInfo);
    return VINF_SUCCESS;
}

int NemuDispVHWAInitHostInfo2(PNEMUDISPDEV pDev, DWORD *pFourCC)
{
    NEMUVHWACMD_QUERYINFO2* pInfo;
    int rc = VINF_SUCCESS;

    if (!pDev->hgsmi.bSupported)
        return VERR_NOT_SUPPORTED;

    pInfo = NemuDispVHWAQueryHostInfo2(pDev, pDev->vhwa.numFourCC);

    Assert(pInfo);
    if(!pInfo)
        return VERR_OUT_OF_RESOURCES;

    if(pDev->vhwa.numFourCC)
    {
        memcpy(pFourCC, pInfo->FourCC, pDev->vhwa.numFourCC * sizeof(pFourCC[0]));
    }
    else
    {
        Assert(0);
        rc = VERR_GENERAL_FAILURE;
    }

    NemuDispVHWAFreeHostInfo2(pDev, pInfo);

    return rc;
}

int NemuDispVHWADisable(PNEMUDISPDEV pDev)
{
    int rc = VERR_GENERAL_FAILURE;
    NEMUVHWACMD* pCmd;

    if (!pDev->hgsmi.bSupported)
        return VERR_NOT_SUPPORTED;

    pCmd = NemuDispVHWACommandCreate (pDev, NEMUVHWACMD_TYPE_DISABLE, 0);
    if (!pCmd)
    {
        WARN(("NemuDispVHWACommandCreate failed"));
        return rc;
    }

    if(NemuDispVHWACommandSubmit (pDev, pCmd))
    {
        if(RT_SUCCESS(pCmd->rc))
        {
            rc = VINF_SUCCESS;
        }
    }

    NemuDispVHWACommandRelease(pDev, pCmd);

    NemuDispVHWACommandCheckHostCmds(pDev);

    return rc;
}

#define MEMTAG 'AWHV'
PNEMUVHWASURFDESC NemuDispVHWASurfDescAlloc()
{
    return (PNEMUVHWASURFDESC) EngAllocMem(FL_NONPAGED_MEMORY | FL_ZERO_MEMORY, sizeof(NEMUVHWASURFDESC), MEMTAG);
}

void NemuDispVHWASurfDescFree(PNEMUVHWASURFDESC pDesc)
{
    EngFreeMem(pDesc);
}

uint64_t NemuDispVHWAVramOffsetFromPDEV(PNEMUDISPDEV pDev, ULONG_PTR offPdev)
{
    return (uint64_t)(pDev->vhwa.offVramBase + offPdev);
}

#define NEMU_DD(_f) DD##_f
#define NEMU_VHWA(_f) NEMUVHWA_##_f
#define NEMU_DD2VHWA(_out, _in, _f) do {if((_in) & NEMU_DD(_f)) _out |= NEMU_VHWA(_f); }while(0)
#define NEMU_DD_VHWA_PAIR(_v) {NEMU_DD(_v), NEMU_VHWA(_v)}
#define NEMU_DD_DUMMY_PAIR(_v) {NEMU_DD(_v), 0}

#define NEMUVHWA_SUPPORTED_CAPS ( \
        NEMUVHWA_CAPS_BLT \
        | NEMUVHWA_CAPS_BLTCOLORFILL \
        | NEMUVHWA_CAPS_BLTFOURCC \
        | NEMUVHWA_CAPS_BLTSTRETCH \
        | NEMUVHWA_CAPS_BLTQUEUE \
        | NEMUVHWA_CAPS_OVERLAY \
        | NEMUVHWA_CAPS_OVERLAYFOURCC \
        | NEMUVHWA_CAPS_OVERLAYSTRETCH \
        | NEMUVHWA_CAPS_OVERLAYCANTCLIP \
        | NEMUVHWA_CAPS_COLORKEY \
        | NEMUVHWA_CAPS_COLORKEYHWASSIST \
        )

#define NEMUVHWA_SUPPORTED_SCAPS ( \
        NEMUVHWA_SCAPS_BACKBUFFER \
        | NEMUVHWA_SCAPS_COMPLEX \
        | NEMUVHWA_SCAPS_FLIP \
        | NEMUVHWA_SCAPS_FRONTBUFFER \
        | NEMUVHWA_SCAPS_OFFSCREENPLAIN \
        | NEMUVHWA_SCAPS_OVERLAY \
        | NEMUVHWA_SCAPS_PRIMARYSURFACE \
        | NEMUVHWA_SCAPS_SYSTEMMEMORY \
        | NEMUVHWA_SCAPS_VIDEOMEMORY \
        | NEMUVHWA_SCAPS_VISIBLE \
        | NEMUVHWA_SCAPS_LOCALVIDMEM \
        )

#define NEMUVHWA_SUPPORTED_SCAPS2 ( \
        NEMUVHWA_CAPS2_CANRENDERWINDOWED \
        | NEMUVHWA_CAPS2_WIDESURFACES \
        | NEMUVHWA_CAPS2_COPYFOURCC \
        )

#define NEMUVHWA_SUPPORTED_PF ( \
        NEMUVHWA_PF_PALETTEINDEXED8 \
        | NEMUVHWA_PF_RGB \
        | NEMUVHWA_PF_RGBTOYUV \
        | NEMUVHWA_PF_YUV \
        | NEMUVHWA_PF_FOURCC \
        )

#define NEMUVHWA_SUPPORTED_SD ( \
        NEMUVHWA_SD_BACKBUFFERCOUNT \
        | NEMUVHWA_SD_CAPS \
        | NEMUVHWA_SD_CKDESTBLT \
        | NEMUVHWA_SD_CKDESTOVERLAY \
        | NEMUVHWA_SD_CKSRCBLT \
        | NEMUVHWA_SD_CKSRCOVERLAY \
        | NEMUVHWA_SD_HEIGHT \
        | NEMUVHWA_SD_PITCH \
        | NEMUVHWA_SD_PIXELFORMAT \
        | NEMUVHWA_SD_WIDTH \
        )

#define NEMUVHWA_SUPPORTED_CKEYCAPS ( \
        NEMUVHWA_CKEYCAPS_DESTBLT \
        | NEMUVHWA_CKEYCAPS_DESTBLTCLRSPACE \
        | NEMUVHWA_CKEYCAPS_DESTBLTCLRSPACEYUV \
        | NEMUVHWA_CKEYCAPS_DESTBLTYUV \
        | NEMUVHWA_CKEYCAPS_DESTOVERLAY \
        | NEMUVHWA_CKEYCAPS_DESTOVERLAYCLRSPACE \
        | NEMUVHWA_CKEYCAPS_DESTOVERLAYCLRSPACEYUV \
        | NEMUVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE \
        | NEMUVHWA_CKEYCAPS_DESTOVERLAYYUV \
        | NEMUVHWA_CKEYCAPS_SRCBLT \
        | NEMUVHWA_CKEYCAPS_SRCBLTCLRSPACE \
        | NEMUVHWA_CKEYCAPS_SRCBLTCLRSPACEYUV \
        | NEMUVHWA_CKEYCAPS_SRCBLTYUV \
        | NEMUVHWA_CKEYCAPS_SRCOVERLAY \
        | NEMUVHWA_CKEYCAPS_SRCOVERLAYCLRSPACE \
        | NEMUVHWA_CKEYCAPS_SRCOVERLAYCLRSPACEYUV \
        | NEMUVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE \
        | NEMUVHWA_CKEYCAPS_SRCOVERLAYYUV \
        | NEMUVHWA_CKEYCAPS_NOCOSTOVERLAY \
        )

#define NEMUVHWA_SUPPORTED_CKEY ( \
        NEMUVHWA_CKEY_COLORSPACE \
        | NEMUVHWA_CKEY_DESTBLT \
        | NEMUVHWA_CKEY_DESTOVERLAY \
        | NEMUVHWA_CKEY_SRCBLT \
        | NEMUVHWA_CKEY_SRCOVERLAY \
        )

#define NEMUVHWA_SUPPORTED_OVER ( \
        NEMUVHWA_OVER_DDFX \
        | NEMUVHWA_OVER_HIDE \
        | NEMUVHWA_OVER_KEYDEST \
        | NEMUVHWA_OVER_KEYDESTOVERRIDE \
        | NEMUVHWA_OVER_KEYSRC \
        | NEMUVHWA_OVER_KEYSRCOVERRIDE \
        | NEMUVHWA_OVER_SHOW \
        )

uint32_t NemuDispVHWAUnsupportedDDCAPS(uint32_t caps)
{
    return caps & (~NEMUVHWA_SUPPORTED_CAPS);
}

uint32_t NemuDispVHWAUnsupportedDDSCAPS(uint32_t caps)
{
    return caps & (~NEMUVHWA_SUPPORTED_SCAPS);
}

uint32_t NemuDispVHWAUnsupportedDDPFS(uint32_t caps)
{
    return caps & (~NEMUVHWA_SUPPORTED_PF);
}

uint32_t NemuDispVHWAUnsupportedDSS(uint32_t caps)
{
    return caps & (~NEMUVHWA_SUPPORTED_SD);
}

uint32_t NemuDispVHWAUnsupportedDDCEYCAPS(uint32_t caps)
{
    return caps & (~NEMUVHWA_SUPPORTED_CKEYCAPS);
}

uint32_t NemuDispVHWASupportedDDCEYCAPS(uint32_t caps)
{
    return caps & (NEMUVHWA_SUPPORTED_CKEYCAPS);
}


uint32_t NemuDispVHWASupportedDDCAPS(uint32_t caps)
{
    return caps & (NEMUVHWA_SUPPORTED_CAPS);
}

uint32_t NemuDispVHWASupportedDDSCAPS(uint32_t caps)
{
    return caps & (NEMUVHWA_SUPPORTED_SCAPS);
}

uint32_t NemuDispVHWASupportedDDPFS(uint32_t caps)
{
    return caps & (NEMUVHWA_SUPPORTED_PF);
}

uint32_t NemuDispVHWASupportedDSS(uint32_t caps)
{
    return caps & (NEMUVHWA_SUPPORTED_SD);
}

uint32_t NemuDispVHWASupportedOVERs(uint32_t caps)
{
    return caps & (NEMUVHWA_SUPPORTED_OVER);
}

uint32_t NemuDispVHWAUnsupportedOVERs(uint32_t caps)
{
    return caps & (~NEMUVHWA_SUPPORTED_OVER);
}

uint32_t NemuDispVHWASupportedCKEYs(uint32_t caps)
{
    return caps & (NEMUVHWA_SUPPORTED_CKEY);
}

uint32_t NemuDispVHWAUnsupportedCKEYs(uint32_t caps)
{
    return caps & (~NEMUVHWA_SUPPORTED_CKEY);
}

uint32_t NemuDispVHWAFromDDOVERs(uint32_t caps) { return caps; }
uint32_t NemuDispVHWAToDDOVERs(uint32_t caps)   { return caps; }
uint32_t NemuDispVHWAFromDDCKEYs(uint32_t caps) { return caps; }
uint32_t NemuDispVHWAToDDCKEYs(uint32_t caps)   { return caps; }

uint32_t NemuDispVHWAFromDDCAPS(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAToDDCAPS(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAFromDDCAPS2(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAToDDCAPS2(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAFromDDSCAPS(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAToDDSCAPS(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAFromDDPFS(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAToDDPFS(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAFromDDCKEYCAPS(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAToDDCKEYCAPS(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAToDDBLTs(uint32_t caps)
{
    return caps;
}

uint32_t NemuDispVHWAFromDDBLTs(uint32_t caps)
{
    return caps;
}

void NemuDispVHWAFromDDCOLORKEY(NEMUVHWA_COLORKEY *pVHWACKey, DDCOLORKEY  *pDdCKey)
{
    pVHWACKey->low = pDdCKey->dwColorSpaceLowValue;
    pVHWACKey->high = pDdCKey->dwColorSpaceHighValue;
}

void NemuDispVHWAFromDDOVERLAYFX(NEMUVHWA_OVERLAYFX *pVHWAOverlay, DDOVERLAYFX *pDdOverlay)
{
    //TODO: fxFlags
    NemuDispVHWAFromDDCOLORKEY(&pVHWAOverlay->DstCK, &pDdOverlay->dckDestColorkey);
    NemuDispVHWAFromDDCOLORKEY(&pVHWAOverlay->SrcCK, &pDdOverlay->dckSrcColorkey);
}

void NemuDispVHWAFromDDBLTFX(NEMUVHWA_BLTFX *pVHWABlt, DDBLTFX *pDdBlt)
{
    pVHWABlt->fillColor = pDdBlt->dwFillColor;

    NemuDispVHWAFromDDCOLORKEY(&pVHWABlt->DstCK, &pDdBlt->ddckDestColorkey);
    NemuDispVHWAFromDDCOLORKEY(&pVHWABlt->SrcCK, &pDdBlt->ddckSrcColorkey);
}

int NemuDispVHWAFromDDPIXELFORMAT(NEMUVHWA_PIXELFORMAT *pVHWAFormat, DDPIXELFORMAT *pDdFormat)
{
    uint32_t unsup = NemuDispVHWAUnsupportedDDPFS(pDdFormat->dwFlags);
    Assert(!unsup);
    if(unsup)
        return VERR_GENERAL_FAILURE;

    pVHWAFormat->flags = NemuDispVHWAFromDDPFS(pDdFormat->dwFlags);
    pVHWAFormat->fourCC = pDdFormat->dwFourCC;
    pVHWAFormat->c.rgbBitCount = pDdFormat->dwRGBBitCount;
    pVHWAFormat->m1.rgbRBitMask = pDdFormat->dwRBitMask;
    pVHWAFormat->m2.rgbGBitMask = pDdFormat->dwGBitMask;
    pVHWAFormat->m3.rgbBBitMask = pDdFormat->dwBBitMask;
    return VINF_SUCCESS;
}

int NemuDispVHWAFromDDSURFACEDESC(NEMUVHWA_SURFACEDESC *pVHWADesc, DDSURFACEDESC *pDdDesc)
{
    uint32_t unsupds = NemuDispVHWAUnsupportedDSS(pDdDesc->dwFlags);
    Assert(!unsupds);
    if(unsupds)
        return VERR_GENERAL_FAILURE;

    pVHWADesc->flags = 0;

    if(pDdDesc->dwFlags & DDSD_BACKBUFFERCOUNT)
    {
        pVHWADesc->flags |= NEMUVHWA_SD_BACKBUFFERCOUNT;
        pVHWADesc->cBackBuffers = pDdDesc->dwBackBufferCount;
    }
    if(pDdDesc->dwFlags & DDSD_CAPS)
    {
        uint32_t unsup = NemuDispVHWAUnsupportedDDSCAPS(pDdDesc->ddsCaps.dwCaps);
        Assert(!unsup);
        if(unsup)
            return VERR_GENERAL_FAILURE;
        pVHWADesc->flags |= NEMUVHWA_SD_CAPS;
        pVHWADesc->surfCaps = NemuDispVHWAFromDDSCAPS(pDdDesc->ddsCaps.dwCaps);
    }
    if(pDdDesc->dwFlags & DDSD_CKDESTBLT)
    {
        pVHWADesc->flags |= NEMUVHWA_SD_CKDESTBLT;
        NemuDispVHWAFromDDCOLORKEY(&pVHWADesc->DstBltCK, &pDdDesc->ddckCKDestBlt);
    }
    if(pDdDesc->dwFlags & DDSD_CKDESTOVERLAY)
    {
        pVHWADesc->flags |= NEMUVHWA_SD_CKDESTOVERLAY;
        NemuDispVHWAFromDDCOLORKEY(&pVHWADesc->DstOverlayCK, &pDdDesc->ddckCKDestOverlay);
    }
    if(pDdDesc->dwFlags & DDSD_CKSRCBLT)
    {
        pVHWADesc->flags |= NEMUVHWA_SD_CKSRCBLT;
        NemuDispVHWAFromDDCOLORKEY(&pVHWADesc->SrcBltCK, &pDdDesc->ddckCKSrcBlt);
    }
    if(pDdDesc->dwFlags & DDSD_CKSRCOVERLAY)
    {
        pVHWADesc->flags |= NEMUVHWA_SD_CKSRCOVERLAY;
        NemuDispVHWAFromDDCOLORKEY(&pVHWADesc->SrcOverlayCK, &pDdDesc->ddckCKSrcOverlay);
    }
    if(pDdDesc->dwFlags & DDSD_HEIGHT)
    {
        pVHWADesc->flags |= NEMUVHWA_SD_HEIGHT;
        pVHWADesc->height = pDdDesc->dwHeight;
    }
    if(pDdDesc->dwFlags & DDSD_WIDTH)
    {
        pVHWADesc->flags |= NEMUVHWA_SD_WIDTH;
        pVHWADesc->width = pDdDesc->dwWidth;
    }
    if(pDdDesc->dwFlags & DDSD_PITCH)
    {
        pVHWADesc->flags |= NEMUVHWA_SD_PITCH;
        pVHWADesc->pitch = pDdDesc->lPitch;
    }
    if(pDdDesc->dwFlags & DDSD_PIXELFORMAT)
    {
        int rc = NemuDispVHWAFromDDPIXELFORMAT(&pVHWADesc->PixelFormat, &pDdDesc->ddpfPixelFormat);
        if(RT_FAILURE(rc))
            return rc;
        pVHWADesc->flags |= NEMUVHWA_SD_PIXELFORMAT;
    }
    return VINF_SUCCESS;
}

void NemuDispVHWAFromRECTL(NEMUVHWA_RECTL *pDst, RECTL *pSrc)
{
    pDst->left = pSrc->left;
    pDst->top = pSrc->top;
    pDst->right = pSrc->right;
    pDst->bottom = pSrc->bottom;
}

#define MIN(_a, _b) (_a) < (_b) ? (_a) : (_b)
#define MAX(_a, _b) (_a) > (_b) ? (_a) : (_b)

void NemuDispVHWARectUnited(RECTL * pDst, RECTL * pRect1, RECTL * pRect2)
{
    pDst->left = MIN(pRect1->left, pRect2->left);
    pDst->top = MIN(pRect1->top, pRect2->top);
    pDst->right = MAX(pRect1->right, pRect2->right);
    pDst->bottom = MAX(pRect1->bottom, pRect2->bottom);
}

bool NemuDispVHWARectIsEmpty(RECTL * pRect)
{
    return pRect->left == pRect->right-1 && pRect->top == pRect->bottom-1;
}

bool NemuDispVHWARectIntersect(RECTL * pRect1, RECTL * pRect2)
{
    return !((pRect1->left < pRect2->left && pRect1->right < pRect2->left)
            || (pRect2->left < pRect1->left && pRect2->right < pRect1->left)
            || (pRect1->top < pRect2->top && pRect1->bottom < pRect2->top)
            || (pRect2->top < pRect1->top && pRect2->bottom < pRect1->top));
}

bool NemuDispVHWARectInclude(RECTL * pRect1, RECTL * pRect2)
{
    return ((pRect1->left <= pRect2->left && pRect1->right >= pRect2->right)
            && (pRect1->top <= pRect2->top && pRect1->bottom >= pRect2->bottom));
}


bool NemuDispVHWARegionIntersects(PNEMUVHWAREGION pReg, RECTL * pRect)
{
    if(!pReg->bValid)
        return false;
    return NemuDispVHWARectIntersect(&pReg->Rect, pRect);
}

bool NemuDispVHWARegionIncludes(PNEMUVHWAREGION pReg, RECTL * pRect)
{
    if(!pReg->bValid)
        return false;
    return NemuDispVHWARectInclude(&pReg->Rect, pRect);
}

bool NemuDispVHWARegionIncluded(PNEMUVHWAREGION pReg, RECTL * pRect)
{
    if(!pReg->bValid)
        return true;
    return NemuDispVHWARectInclude(pRect, &pReg->Rect);
}

void NemuDispVHWARegionSet(PNEMUVHWAREGION pReg, RECTL * pRect)
{
    if(NemuDispVHWARectIsEmpty(pRect))
    {
        pReg->bValid = false;
    }
    else
    {
        pReg->Rect = *pRect;
        pReg->bValid = true;
    }
}

void NemuDispVHWARegionAdd(PNEMUVHWAREGION pReg, RECTL * pRect)
{
    if(NemuDispVHWARectIsEmpty(pRect))
    {
        return;
    }
    else if(!pReg->bValid)
    {
        NemuDispVHWARegionSet(pReg, pRect);
    }
    else
    {
        NemuDispVHWARectUnited(&pReg->Rect, &pReg->Rect, pRect);
    }
}

void NemuDispVHWARegionInit(PNEMUVHWAREGION pReg)
{
    pReg->bValid = false;
}

void NemuDispVHWARegionClear(PNEMUVHWAREGION pReg)
{
    pReg->bValid = false;
}

bool NemuDispVHWARegionValid(PNEMUVHWAREGION pReg)
{
    return pReg->bValid;
}

void NemuDispVHWARegionTrySubstitute(PNEMUVHWAREGION pReg, const RECTL *pRect)
{
    if(!pReg->bValid)
        return;

    if(pReg->Rect.left >= pRect->left && pReg->Rect.right <= pRect->right)
    {
        LONG t = MAX(pReg->Rect.top, pRect->top);
        LONG b = MIN(pReg->Rect.bottom, pRect->bottom);
        if(t < b)
        {
            pReg->Rect.top = t;
            pReg->Rect.bottom = b;
        }
        else
        {
            pReg->bValid = false;
        }
    }
    else if(pReg->Rect.top >= pRect->top && pReg->Rect.bottom <= pRect->bottom)
    {
        LONG l = MAX(pReg->Rect.left, pRect->left);
        LONG r = MIN(pReg->Rect.right, pRect->right);
        if(l < r)
        {
            pReg->Rect.left = l;
            pReg->Rect.right = r;
        }
        else
        {
            pReg->bValid = false;
        }
    }
}
