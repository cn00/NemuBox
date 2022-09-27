/* $Id: NemuMPVhwa.h $ */

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

#ifndef ___NemuMPVhwa_h___
#define ___NemuMPVhwa_h___

#include <iprt/cdefs.h>

#include "NemuMPShgsmi.h"

NEMUVHWACMD* nemuVhwaCommandCreate(PNEMUMP_DEVEXT pDevExt,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId,
        NEMUVHWACMD_TYPE enmCmd,
        NEMUVHWACMD_LENGTH cbCmd);

void nemuVhwaCommandFree(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd);
int nemuVhwaCommandSubmit(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd);
int nemuVhwaCommandSubmit(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd);
void nemuVhwaCommandSubmitAsynchAndComplete(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd);

#ifndef NEMUVHWA_WITH_SHGSMI
typedef DECLCALLBACK(void) FNNEMUVHWACMDCOMPLETION(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD * pCmd, void * pContext);
typedef FNNEMUVHWACMDCOMPLETION *PFNNEMUVHWACMDCOMPLETION;

void nemuVhwaCommandSubmitAsynch(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd, PFNNEMUVHWACMDCOMPLETION pfnCompletion, void * pContext);
void nemuVhwaCommandSubmitAsynchByEvent(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD* pCmd, RTSEMEVENT hEvent);

#define NEMUVHWA_CMD2LISTENTRY(_pCmd) ((PNEMUVTLIST_ENTRY)&(_pCmd)->u.pNext)
#define NEMUVHWA_LISTENTRY2CMD(_pEntry) ( (NEMUVHWACMD*)((uint8_t *)(_pEntry) - RT_OFFSETOF(NEMUVHWACMD, u.pNext)) )

DECLINLINE(void) nemuVhwaPutList(NEMUVTLIST *pList, NEMUVHWACMD* pCmd)
{
    nemuVtListPut(pList, NEMUVHWA_CMD2LISTENTRY(pCmd), NEMUVHWA_CMD2LISTENTRY(pCmd));
}

void nemuVhwaCompletionListProcess(PNEMUMP_DEVEXT pDevExt, NEMUVTLIST *pList);
#endif

void nemuVhwaFreeHostInfo1(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD_QUERYINFO1* pInfo);
void nemuVhwaFreeHostInfo2(PNEMUMP_DEVEXT pDevExt, NEMUVHWACMD_QUERYINFO2* pInfo);
NEMUVHWACMD_QUERYINFO1* nemuVhwaQueryHostInfo1(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId);
NEMUVHWACMD_QUERYINFO2* nemuVhwaQueryHostInfo2(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId, uint32_t numFourCC);
int nemuVhwaEnable(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId);
int nemuVhwaDisable(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId);
void nemuVhwaInit(PNEMUMP_DEVEXT pDevExt);
void nemuVhwaFree(PNEMUMP_DEVEXT pDevExt);

int nemuVhwaHlpOverlayFlip(PNEMUWDDM_OVERLAY pOverlay, const DXGKARG_FLIPOVERLAY *pFlipInfo);
int nemuVhwaHlpOverlayUpdate(PNEMUWDDM_OVERLAY pOverlay, const DXGK_OVERLAYINFO *pOverlayInfo);
int nemuVhwaHlpOverlayDestroy(PNEMUWDDM_OVERLAY pOverlay);
int nemuVhwaHlpOverlayCreate(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, DXGK_OVERLAYINFO *pOverlayInfo, /* OUT */ PNEMUWDDM_OVERLAY pOverlay);

int nemuVhwaHlpColorFill(PNEMUWDDM_OVERLAY pOverlay, PNEMUWDDM_DMA_PRIVATEDATA_CLRFILL pCF);

int nemuVhwaHlpGetSurfInfo(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_ALLOCATION pSurf);

BOOLEAN nemuVhwaHlpOverlayListIsEmpty(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);
//void nemuVhwaHlpOverlayListAdd(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_OVERLAY pOverlay);
//void nemuVhwaHlpOverlayListRemove(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_OVERLAY pOverlay);
void nemuVhwaHlpOverlayDstRectUnion(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, RECT *pRect);
void nemuVhwaHlpOverlayDstRectGet(PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_OVERLAY pOverlay, RECT *pRect);

#endif /* #ifndef ___NemuMPVhwa_h___ */
