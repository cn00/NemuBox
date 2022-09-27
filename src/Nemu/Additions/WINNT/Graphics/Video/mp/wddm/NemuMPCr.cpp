/* $Id: NemuMPCr.cpp $ */

/** @file
 * Nemu WDDM Miniport driver
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifdef NEMU_WITH_CROGL

#include "NemuMPWddm.h"
#include "NemuMPCr.h"

#include <Nemu/HostServices/NemuCrOpenGLSvc.h>

#include <cr_protocol.h>

CR_CAPS_INFO g_NemuMpCrHostCapsInfo;
static uint32_t g_NemuMpCr3DSupported = 0;

uint32_t NemuMpCrGetHostCaps()
{
    return g_NemuMpCrHostCapsInfo.u32Caps;
}

bool NemuMpCrCtlConIs3DSupported()
{
    return !!g_NemuMpCr3DSupported;
}

static void* nemuMpCrShgsmiBufferAlloc(PNEMUMP_DEVEXT pDevExt, HGSMISIZE cbData)
{
    return NemuSHGSMIHeapBufferAlloc(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, cbData);
}

static NEMUVIDEOOFFSET nemuMpCrShgsmiBufferOffset(PNEMUMP_DEVEXT pDevExt, void *pvBuffer)
{
    return (NEMUVIDEOOFFSET)HGSMIPointerToOffset(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx.Heap.area, (const HGSMIBUFFERHEADER *)pvBuffer);
}

static void* nemuMpCrShgsmiBufferFromOffset(PNEMUMP_DEVEXT pDevExt, NEMUVIDEOOFFSET offBuffer)
{
    return HGSMIOffsetToPointer(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx.Heap.area, (HGSMIOFFSET)offBuffer);
}

static void nemuMpCrShgsmiBufferFree(PNEMUMP_DEVEXT pDevExt, void *pvBuffer)
{
    NemuSHGSMIHeapBufferFree(&NemuCommonFromDeviceExt(pDevExt)->guestCtx.heapCtx, pvBuffer);
}

static NEMUVIDEOOFFSET nemuMpCrShgsmiTransportBufOffset(PNEMUMP_CRSHGSMITRANSPORT pCon, void* pvBuffer)
{
    return nemuMpCrShgsmiBufferOffset(pCon->pDevExt, pvBuffer);
}

static void* nemuMpCrShgsmiTransportBufFromOffset(PNEMUMP_CRSHGSMITRANSPORT pCon, NEMUVIDEOOFFSET offBuffer)
{
    return nemuMpCrShgsmiBufferFromOffset(pCon->pDevExt, offBuffer);
}

void* NemuMpCrShgsmiTransportBufAlloc(PNEMUMP_CRSHGSMITRANSPORT pCon, uint32_t cbBuffer)
{
    return nemuMpCrShgsmiBufferAlloc(pCon->pDevExt, cbBuffer);
}

void NemuMpCrShgsmiTransportBufFree(PNEMUMP_CRSHGSMITRANSPORT pCon, void* pvBuffer)
{
    nemuMpCrShgsmiBufferFree(pCon->pDevExt, pvBuffer);
}

static int nemuMpCrShgsmiBufCacheBufReinit(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUMP_CRSHGSMICON_BUFDR_CACHE pCache, PNEMUMP_CRSHGSMICON_BUFDR pDr, uint32_t cbRequested)
{
    if (pDr->cbBuf >= cbRequested)
        return VINF_SUCCESS;

    if (pDr->pvBuf)
        NemuMpCrShgsmiTransportBufFree(pCon, pDr->pvBuf);

    pDr->pvBuf = NemuMpCrShgsmiTransportBufAlloc(pCon, cbRequested);
    if (!pDr->pvBuf)
    {
        WARN(("NemuMpCrShgsmiTransportBufAlloc failed"));
        pDr->cbBuf = 0;
        return VERR_NO_MEMORY;
    }

    pDr->cbBuf = cbRequested;
    return VINF_SUCCESS;
}

static void nemuMpCrShgsmiBufCacheFree(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUMP_CRSHGSMICON_BUFDR_CACHE pCache, PNEMUMP_CRSHGSMICON_BUFDR pDr)
{
    if (ASMAtomicCmpXchgPtr(&pCache->pBufDr, pDr, NULL))
        return;

    /* the value is already cached, free the current one */
    NemuMpCrShgsmiTransportBufFree(pCon, pDr->pvBuf);
    nemuWddmMemFree(pDr);
}

static PNEMUMP_CRSHGSMICON_BUFDR nemuMpCrShgsmiBufCacheGetAllocDr(PNEMUMP_CRSHGSMICON_BUFDR_CACHE pCache)
{
    PNEMUMP_CRSHGSMICON_BUFDR pBufDr = (PNEMUMP_CRSHGSMICON_BUFDR)ASMAtomicXchgPtr((void * volatile *)&pCache->pBufDr, NULL);
    if (!pBufDr)
    {
        pBufDr = (PNEMUMP_CRSHGSMICON_BUFDR)nemuWddmMemAllocZero(sizeof (*pBufDr));
        if (!pBufDr)
        {
            WARN(("nemuWddmMemAllocZero failed!"));
            return NULL;
        }
    }
    return pBufDr;
}

static PNEMUMP_CRSHGSMICON_BUFDR nemuMpCrShgsmiBufCacheAlloc(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUMP_CRSHGSMICON_BUFDR_CACHE pCache, uint32_t cbBuffer)
{
    PNEMUMP_CRSHGSMICON_BUFDR pBufDr = nemuMpCrShgsmiBufCacheGetAllocDr(pCache);
    int rc = nemuMpCrShgsmiBufCacheBufReinit(pCon, pCache, pBufDr, cbBuffer);
    if (RT_SUCCESS(rc))
        return pBufDr;

    WARN(("nemuMpCrShgsmiBufCacheBufReinit failed, rc %d", rc));

    nemuMpCrShgsmiBufCacheFree(pCon, pCache, pBufDr);
    return NULL;
}

static PNEMUMP_CRSHGSMICON_BUFDR nemuMpCrShgsmiBufCacheAllocAny(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUMP_CRSHGSMICON_BUFDR_CACHE pCache, uint32_t cbBuffer)
{
    PNEMUMP_CRSHGSMICON_BUFDR pBufDr = nemuMpCrShgsmiBufCacheGetAllocDr(pCache);

    if (pBufDr->cbBuf)
        return pBufDr;

    int rc = nemuMpCrShgsmiBufCacheBufReinit(pCon, pCache, pBufDr, cbBuffer);
    if (RT_SUCCESS(rc))
        return pBufDr;

    WARN(("nemuMpCrShgsmiBufCacheBufReinit failed, rc %d", rc));

    nemuMpCrShgsmiBufCacheFree(pCon, pCache, pBufDr);
    return NULL;
}


static int nemuMpCrShgsmiBufCacheInit(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUMP_CRSHGSMICON_BUFDR_CACHE pCache)
{
    memset(pCache, 0, sizeof (*pCache));
    return VINF_SUCCESS;
}

static void nemuMpCrShgsmiBufCacheTerm(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUMP_CRSHGSMICON_BUFDR_CACHE pCache)
{
    if (pCache->pBufDr)
        nemuMpCrShgsmiBufCacheFree(pCon, pCache, pCache->pBufDr);
}

int NemuMpCrShgsmiTransportCreate(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUMP_DEVEXT pDevExt)
{
    memset(pCon, 0, sizeof (*pCon));
    pCon->pDevExt = pDevExt;
    return VINF_SUCCESS;
    int rc;
//    int rc = nemuMpCrShgsmiBufCacheInit(pCon, &pCon->CmdDrCache);
//    if (RT_SUCCESS(rc))
    {
        rc = nemuMpCrShgsmiBufCacheInit(pCon, &pCon->WbDrCache);
        if (RT_SUCCESS(rc))
        {
        }
        else
        {
            WARN(("nemuMpCrShgsmiBufCacheInit2 failed rc %d", rc));
        }
//        nemuMpCrShgsmiBufCacheTerm(pCon, &pCon->CmdDrCache);
    }
//    else
//    {
//        WARN(("nemuMpCrShgsmiBufCacheInit1 failed rc %d", rc));
//    }

    return rc;
}

void NemuMpCrShgsmiTransportTerm(PNEMUMP_CRSHGSMITRANSPORT pCon)
{
    nemuMpCrShgsmiBufCacheTerm(pCon, &pCon->WbDrCache);
//    nemuMpCrShgsmiBufCacheTerm(pCon, &pCon->CmdDrCache);
}

typedef struct NEMUMP_CRHGSMICMD_BASE
{
//    NEMUMP_CRHGSMICMD_HDR Hdr;
    CRNEMUHGSMIHDR CmdHdr;
} NEMUMP_CRHGSMICMD_BASE, *PNEMUMP_CRHGSMICMD_BASE;

typedef struct NEMUMP_CRHGSMICMD_WRITEREAD
{
//    NEMUMP_CRHGSMICMD_HDR Hdr;
    CRNEMUHGSMIWRITEREAD Cmd;
} NEMUMP_CRHGSMICMD_WRITEREAD, *PNEMUMP_CRHGSMICMD_WRITEREAD;

typedef struct NEMUMP_CRHGSMICMD_READ
{
//    NEMUMP_CRHGSMICMD_HDR Hdr;
    CRNEMUHGSMIREAD Cmd;
} NEMUMP_CRHGSMICMD_READ, *PNEMUMP_CRHGSMICMD_READ;

typedef struct NEMUMP_CRHGSMICMD_WRITE
{
//    NEMUMP_CRHGSMICMD_HDR Hdr;
    CRNEMUHGSMIWRITE Cmd;
} NEMUMP_CRHGSMICMD_WRITE, *PNEMUMP_CRHGSMICMD_WRITE;


#define NEMUMP_CRSHGSMICON_CMD_CMDBUF_OFFSET(_cBuffers) NEMUWDDM_ROUNDBOUND(RT_OFFSETOF(NEMUVDMACMD_CHROMIUM_CMD, aBuffers[_cBuffers]), 8)
#define NEMUMP_CRSHGSMICON_CMD_CMDCTX_OFFSET(_cBuffers, _cbCmdBuf) ( NEMUMP_CRSHGSMICON_CMD_CMDBUF_OFFSET(_cBuffers) + NEMUWDDM_ROUNDBOUND(_cbCmdBuf, 8))
#define NEMUMP_CRSHGSMICON_CMD_GET_CMDBUF(_pCmd, _cBuffers, _type) ((_type*)(((uint8_t*)(_pCmd)) + NEMUMP_CRSHGSMICON_CMD_CMDBUF_OFFSET(_cBuffers)))
#define NEMUMP_CRSHGSMICON_CMD_GET_CMDCTX(_pCmd, _cBuffers, _cbCmdBuf, _type) ((_type*)(((uint8_t*)(_pCmd)) +  NEMUMP_CRSHGSMICON_CMD_CMDCTX_OFFSET(_cBuffers, _cbCmdBuf)))
#define NEMUMP_CRSHGSMICON_CMD_GET_FROM_CMDCTX(_pCtx, _cBuffers, _cbCmdBuf, _type) ((_type*)(((uint8_t*)(_pCtx)) -  NEMUMP_CRSHGSMICON_CMD_CMDCTX_OFFSET(_cBuffers, _cbCmdBuf)))
#define NEMUMP_CRSHGSMICON_CMD_SIZE(_cBuffers, _cbCmdBuf, _cbCtx) (NEMUMP_CRSHGSMICON_CMD_CMDCTX_OFFSET(_cBuffers, _cbCmdBuf) + (_cbCtx))


#define NEMUMP_CRSHGSMICON_DR_CMDBUF_OFFSET(_cBuffers) NEMUWDDM_ROUNDBOUND((NEMUVDMACMD_SIZE_FROMBODYSIZE(RT_OFFSETOF(NEMUVDMACMD_CHROMIUM_CMD, aBuffers[_cBuffers]))), 8)
#define NEMUMP_CRSHGSMICON_DR_CMDCTX_OFFSET(_cBuffers, _cbCmdBuf) ( NEMUMP_CRSHGSMICON_DR_CMDBUF_OFFSET(_cBuffers) + NEMUWDDM_ROUNDBOUND(_cbCmdBuf, 8))
#define NEMUMP_CRSHGSMICON_DR_GET_CRCMD(_pDr) (NEMUVDMACMD_BODY((_pDr), NEMUVDMACMD_CHROMIUM_CMD))
#define NEMUMP_CRSHGSMICON_DR_GET_CMDBUF(_pDr, _cBuffers, _type) ((_type*)(((uint8_t*)(_pDr)) + NEMUMP_CRSHGSMICON_DR_CMDBUF_OFFSET(_cBuffers)))
#define NEMUMP_CRSHGSMICON_DR_GET_CMDCTX(_pDr, _cBuffers, _cbCmdBuf, _type) ((_type*)(((uint8_t*)(_pDr)) +  NEMUMP_CRSHGSMICON_DR_CMDCTX_OFFSET(_cBuffers, _cbCmdBuf)))
#define NEMUMP_CRSHGSMICON_DR_GET_FROM_CMDCTX(_pCtx, _cBuffers, _cbCmdBuf) ((NEMUVDMACMD*)(((uint8_t*)(_pCtx)) -  NEMUMP_CRSHGSMICON_DR_CMDCTX_OFFSET(_cBuffers, _cbCmdBuf)))
#define NEMUMP_CRSHGSMICON_DR_SIZE(_cBuffers, _cbCmdBuf, _cbCtx) (NEMUMP_CRSHGSMICON_DR_CMDCTX_OFFSET(_cBuffers, _cbCmdBuf) + (_cbCtx))


static int nemuMpCrShgsmiTransportCmdSubmitDr(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUVDMACBUF_DR pDr, PFNNEMUVDMADDICMDCOMPLETE_DPC pfnComplete)
{

    PNEMUVDMADDI_CMD pDdiCmd = NEMUVDMADDI_CMD_FROM_BUF_DR(pDr);
    PNEMUMP_DEVEXT pDevExt = pCon->pDevExt;
    nemuVdmaDdiCmdInit(pDdiCmd, 0, 0, pfnComplete, pCon);
    /* mark command as submitted & invisible for the dx runtime since dx did not originate it */
    nemuVdmaDdiCmdSubmittedNotDx(pDdiCmd);
    int rc = nemuVdmaCBufDrSubmit(pDevExt, &pDevExt->u.primary.Vdma, pDr);
    if (RT_SUCCESS(rc))
    {
        return VINF_SUCCESS;
    }

    WARN(("nemuVdmaCBufDrSubmit failed rc %d", rc));
    return rc;
}

static int nemuMpCrShgsmiTransportCmdSubmitDmaCmd(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUVDMACMD pHdr, PFNNEMUVDMADDICMDCOMPLETE_DPC pfnComplete)
{
    PNEMUVDMACBUF_DR pDr = NEMUVDMACBUF_DR_FROM_TAIL(pHdr);
    return nemuMpCrShgsmiTransportCmdSubmitDr(pCon, pDr, pfnComplete);
}

static void nemuMpCrShgsmiTransportCmdTermDmaCmd(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUVDMACMD pHdr)
{
    PNEMUVDMACBUF_DR pDr = NEMUVDMACBUF_DR_FROM_TAIL(pHdr);
    PNEMUMP_DEVEXT pDevExt = pCon->pDevExt;
    nemuVdmaCBufDrFree (&pDevExt->u.primary.Vdma, pDr);
}


typedef DECLCALLBACK(void) FNNEMUMP_CRSHGSMITRANSPORT_SENDREADASYNC_COMPLETION(PNEMUMP_CRSHGSMITRANSPORT pCon, int rc, void *pvRx, uint32_t cbRx, void *pvCtx);
typedef FNNEMUMP_CRSHGSMITRANSPORT_SENDREADASYNC_COMPLETION *PFNNEMUMP_CRSHGSMITRANSPORT_SENDREADASYNC_COMPLETION;

static DECLCALLBACK(VOID) nemuMpCrShgsmiTransportSendReadAsyncCompletion(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pDdiCmd, PVOID pvContext)
{
    /* we should be called from our DPC routine */
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PNEMUMP_CRSHGSMITRANSPORT pCon = (PNEMUMP_CRSHGSMITRANSPORT)pvContext;
    PNEMUVDMACBUF_DR pDr = NEMUVDMACBUF_DR_FROM_DDI_CMD(pDdiCmd);
    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUMP_CRSHGSMICON_DR_GET_CRCMD(pHdr);
    const UINT cBuffers = 2;
    Assert(pBody->cBuffers == cBuffers);
    PNEMUMP_CRHGSMICMD_READ pWrData = NEMUMP_CRSHGSMICON_DR_GET_CMDBUF(pHdr, cBuffers, NEMUMP_CRHGSMICMD_READ);
    CRNEMUHGSMIREAD *pCmd = &pWrData->Cmd;
    NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[0];
    Assert(pBufCmd->cbBuffer == sizeof (CRNEMUHGSMIREAD));
    CRNEMUHGSMIREAD * pWr = (CRNEMUHGSMIREAD*)nemuMpCrShgsmiTransportBufFromOffset(pCon, pBufCmd->offBuffer);
    PFNNEMUMP_CRSHGSMITRANSPORT_SENDREADASYNC_COMPLETION pfnCompletion = (PFNNEMUMP_CRSHGSMITRANSPORT_SENDREADASYNC_COMPLETION)pBufCmd->u64GuestData;
    NEMUVDMACMD_CHROMIUM_BUFFER *pRxBuf = &pBody->aBuffers[1];
    PNEMUMP_CRSHGSMICON_BUFDR pWbDr = (PNEMUMP_CRSHGSMICON_BUFDR)pRxBuf->u64GuestData;
    void *pvRx = NULL;
    uint32_t cbRx = 0;

    int rc = pDr->rc;
    if (RT_SUCCESS(rc))
    {
        rc = pWr->hdr.result;
        if (RT_SUCCESS(rc))
        {
            cbRx = pCmd->cbBuffer;
            if (cbRx)
                pvRx = pWbDr->pvBuf;
        }
        else
        {
            WARN(("CRNEMUHGSMIREAD failed, rc %d", rc));
        }
    }
    else
    {
        WARN(("dma command buffer failed rc %d!", rc));
    }

    if (pfnCompletion)
    {
        void *pvCtx = NEMUMP_CRSHGSMICON_DR_GET_CMDCTX(pHdr, cBuffers, sizeof (NEMUMP_CRHGSMICMD_READ), void);
        pfnCompletion(pCon, rc, pvRx, cbRx, pvCtx);
    }

    nemuMpCrShgsmiBufCacheFree(pCon, &pCon->WbDrCache, pWbDr);
}

static void* nemuMpCrShgsmiTransportCmdCreateReadAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, uint32_t u32ClientID, PNEMUVDMACBUF_DR pDr, uint32_t cbDrData, PNEMUMP_CRSHGSMICON_BUFDR pWbDr,
        PFNNEMUMP_CRSHGSMITRANSPORT_SENDREADASYNC_COMPLETION pfnCompletion, uint32_t cbContextData)
{
    const uint32_t cBuffers = 2;
    const uint32_t cbCmd = NEMUMP_CRSHGSMICON_DR_SIZE(cBuffers, sizeof (NEMUMP_CRHGSMICMD_READ), cbContextData);
    PNEMUMP_DEVEXT pDevExt = pCon->pDevExt;
    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUMP_CRSHGSMICON_DR_GET_CRCMD(pHdr);
    PNEMUMP_CRHGSMICMD_READ pWrData = NEMUMP_CRSHGSMICON_DR_GET_CMDBUF(pHdr, cBuffers, NEMUMP_CRHGSMICMD_READ);
    CRNEMUHGSMIREAD *pCmd = &pWrData->Cmd;

    if (cbCmd > cbContextData)
    {
        ERR(("the passed descriptor is less than needed!"));
        return NULL;
    }

    memset(pDr, 0, NEMUVDMACBUF_DR_SIZE(cbCmd));

    pDr->fFlags = NEMUVDMACBUF_FLAG_BUF_VRAM_OFFSET;
    pDr->cbBuf = cbCmd;
    pDr->rc = VERR_NOT_IMPLEMENTED;
    pDr->Location.offVramBuf = nemuMpCrShgsmiTransportBufOffset(pCon, pCmd);

    pHdr->enmType = NEMUVDMACMD_TYPE_CHROMIUM_CMD;
    pHdr->u32CmdSpecific = 0;

    pBody->cBuffers = cBuffers;

    pCmd->hdr.result      = VERR_WRONG_ORDER;
    pCmd->hdr.u32ClientID = u32ClientID;
    pCmd->hdr.u32Function = SHCRGL_GUEST_FN_WRITE_READ;
    //    pCmd->hdr.u32Reserved = 0;
    pCmd->iBuffer = 1;

    NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[0];
    pBufCmd->offBuffer = nemuMpCrShgsmiTransportBufOffset(pCon, pCmd);
    pBufCmd->cbBuffer = sizeof (*pCmd);
    pBufCmd->u32GuestData = 0;
    pBufCmd->u64GuestData = (uint64_t)pfnCompletion;

    pBufCmd = &pBody->aBuffers[1];
    pBufCmd->offBuffer = nemuMpCrShgsmiTransportBufOffset(pCon, pWbDr->pvBuf);
    pBufCmd->cbBuffer = pWbDr->cbBuf;
    pBufCmd->u32GuestData = 0;
    pBufCmd->u64GuestData = (uint64_t)pWbDr;

    return NEMUMP_CRSHGSMICON_DR_GET_CMDCTX(pHdr, cBuffers, sizeof (NEMUMP_CRHGSMICMD_READ), void);
}

static int nemuMpCrShgsmiTransportCmdSubmitReadAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    NEMUVDMACMD* pHdr = NEMUMP_CRSHGSMICON_DR_GET_FROM_CMDCTX(pvContext, 2, sizeof (NEMUMP_CRHGSMICMD_READ));
    return nemuMpCrShgsmiTransportCmdSubmitDmaCmd(pCon, pHdr, nemuMpCrShgsmiTransportSendReadAsyncCompletion);
}

typedef struct NEMUMP_CRHGSMICON_WRR_COMPLETION_CTX
{
    PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION pfnCompletion;
    void *pvContext;

} NEMUMP_CRHGSMICON_WRR_COMPLETION_CTX, *PNEMUMP_CRHGSMICON_WRR_COMPLETION_CTX;

static DECLCALLBACK(void) nemuMpCrShgsmiTransportSendWriteReadReadRepostCompletion(PNEMUMP_CRSHGSMITRANSPORT pCon, int rc, void *pvRx, uint32_t cbRx, void *pvCtx)
{
    PNEMUMP_CRHGSMICON_WRR_COMPLETION_CTX pData = (PNEMUMP_CRHGSMICON_WRR_COMPLETION_CTX)pvCtx;
    PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION pfnCompletion = pData->pfnCompletion;
    if (pfnCompletion)
        pfnCompletion(pCon, rc, pvRx, cbRx, pData->pvContext);
}

static DECLCALLBACK(VOID) nemuMpCrShgsmiTransportSendWriteReadAsyncCompletion(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pDdiCmd, PVOID pvContext)
{
    /* we should be called from our DPC routine */
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PNEMUMP_CRSHGSMITRANSPORT pCon = (PNEMUMP_CRSHGSMITRANSPORT)pvContext;
    PNEMUVDMACBUF_DR pDr = NEMUVDMACBUF_DR_FROM_DDI_CMD(pDdiCmd);
    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUMP_CRSHGSMICON_DR_GET_CRCMD(pHdr);
    const UINT cBuffers = 3;
    Assert(pBody->cBuffers == cBuffers);
    PNEMUMP_CRHGSMICMD_WRITEREAD pWrData = NEMUMP_CRSHGSMICON_DR_GET_CMDBUF(pHdr, cBuffers, NEMUMP_CRHGSMICMD_WRITEREAD);
    CRNEMUHGSMIWRITEREAD *pCmd = &pWrData->Cmd;
    NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[0];
    Assert(pBufCmd->cbBuffer == sizeof (CRNEMUHGSMIWRITEREAD));
    CRNEMUHGSMIWRITEREAD * pWr = (CRNEMUHGSMIWRITEREAD*)nemuMpCrShgsmiTransportBufFromOffset(pCon, pBufCmd->offBuffer);
    NEMUVDMACMD_CHROMIUM_BUFFER *pRxBuf = &pBody->aBuffers[2];
    PNEMUMP_CRSHGSMICON_BUFDR pWbDr = (PNEMUMP_CRSHGSMICON_BUFDR)pRxBuf->u64GuestData;
    PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION pfnCompletion = (PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION)pBufCmd->u64GuestData;
    void *pvRx = NULL;
    uint32_t cbRx = 0;

    int rc = pDr->rc;
    if (RT_SUCCESS(rc))
    {
        rc = pWr->hdr.result;
        if (RT_SUCCESS(rc))
        {
            cbRx = pCmd->cbWriteback;
            if (cbRx)
                pvRx = pWbDr->pvBuf;
        }
        else if (rc == VERR_BUFFER_OVERFLOW)
        {
            /* issue read */
            void *pvCtx = NEMUMP_CRSHGSMICON_DR_GET_CMDCTX(pHdr, cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITEREAD), void);
            nemuMpCrShgsmiBufCacheFree(pCon, &pCon->WbDrCache, pWbDr);
            pWbDr =  nemuMpCrShgsmiBufCacheAlloc(pCon, &pCon->WbDrCache, pCmd->cbWriteback);
            if (pWbDr)
            {
                /* the Read Command is shorter than WriteRead, so just reuse the Write-Read descriptor here */
                PNEMUMP_CRHGSMICON_WRR_COMPLETION_CTX pReadCtx = (PNEMUMP_CRHGSMICON_WRR_COMPLETION_CTX)nemuMpCrShgsmiTransportCmdCreateReadAsync(pCon, pCmd->hdr.u32ClientID,
                            pDr, NEMUMP_CRSHGSMICON_DR_SIZE(cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITEREAD), 0),
                            pWbDr, nemuMpCrShgsmiTransportSendWriteReadReadRepostCompletion, sizeof (*pReadCtx));
                pReadCtx->pfnCompletion = pfnCompletion;
                pReadCtx->pvContext = pvCtx;
                nemuMpCrShgsmiTransportCmdSubmitReadAsync(pCon, pReadCtx);
                /* don't do completion here, the completion will be called from the read completion we issue here */
                pfnCompletion = NULL;
                /* the current pWbDr was already freed, and we'll free the Read dr in the Read Completion */
                pWbDr = NULL;
            }
            else
            {
                WARN(("nemuMpCrShgsmiBufCacheAlloc failed for %d", pCmd->cbWriteback));
                rc = VERR_NO_MEMORY;
            }
        }
        else
        {
            WARN(("CRNEMUHGSMIWRITEREAD failed, rc %d", rc));
        }
    }
    else
    {
        WARN(("dma command buffer failed rc %d!", rc));
    }

    if (pfnCompletion)
    {
        void *pvCtx = NEMUMP_CRSHGSMICON_DR_GET_CMDCTX(pHdr, cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITEREAD), void);
        pfnCompletion(pCon, rc, pvRx, cbRx, pvCtx);
    }

    if (pWbDr)
        nemuMpCrShgsmiBufCacheFree(pCon, &pCon->WbDrCache, pWbDr);
}

static DECLCALLBACK(VOID) nemuMpCrShgsmiTransportVdmaSendWriteAsyncCompletion(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pDdiCmd, PVOID pvContext)
{
    /* we should be called from our DPC routine */
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PNEMUMP_CRSHGSMITRANSPORT pCon = (PNEMUMP_CRSHGSMITRANSPORT)pvContext;
    PNEMUVDMACBUF_DR pDr = NEMUVDMACBUF_DR_FROM_DDI_CMD(pDdiCmd);
    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUMP_CRSHGSMICON_DR_GET_CRCMD(pHdr);
    const UINT cBuffers = 2;
    Assert(pBody->cBuffers == cBuffers);
    PNEMUMP_CRHGSMICMD_WRITE pWrData = NEMUMP_CRSHGSMICON_DR_GET_CMDBUF(pHdr, cBuffers, NEMUMP_CRHGSMICMD_WRITE);
    CRNEMUHGSMIWRITE *pCmd = &pWrData->Cmd;
    NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[0];
    Assert(pBufCmd->cbBuffer == sizeof (CRNEMUHGSMIWRITE));
    PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION pfnCompletion = (PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION)pBufCmd->u64GuestData;

    int rc = pDr->rc;
    if (RT_SUCCESS(rc))
    {
        rc = pCmd->hdr.result;
        if (!RT_SUCCESS(rc))
        {
            WARN(("CRNEMUHGSMIWRITE failed, rc %d", rc));
        }
    }
    else
    {
        WARN(("dma command buffer failed rc %d!", rc));
    }

    if (pfnCompletion)
    {
        void *pvCtx = NEMUMP_CRSHGSMICON_DR_GET_CMDCTX(pHdr, cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITE), void);
        pfnCompletion(pCon, rc, pvCtx);
    }
}

static DECLCALLBACK(VOID) nemuMpCrShgsmiTransportVbvaSendWriteAsyncCompletion(PNEMUSHGSMI pHeap, void *pvCmd, void *pvContext)
{
    /* we should be called from our DPC routine */
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PNEMUMP_CRSHGSMITRANSPORT pCon = (PNEMUMP_CRSHGSMITRANSPORT)pvContext;
    PNEMUMP_DEVEXT pDevExt = pCon->pDevExt;
    NEMUCMDVBVA_CRCMD_CMD *pCmd;
    int rc = NemuCmdVbvaConCmdCompletionData(pvCmd, &pCmd);
    const UINT cBuffers = 2;
    Assert(pCmd->cBuffers == cBuffers);
    PNEMUMP_CRHGSMICMD_WRITE pWrData = NEMUMP_CRSHGSMICON_CMD_GET_CMDBUF(pCmd, cBuffers, NEMUMP_CRHGSMICMD_WRITE);
    uint64_t*pu64Completion = NEMUMP_CRSHGSMICON_CMD_GET_CMDCTX(pCmd, cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITE), uint64_t);
    PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION pfnCompletion = (PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION)(*pu64Completion);

    if (!RT_SUCCESS(rc))
        WARN(("CRNEMUHGSMIWRITE failed, rc %d", rc));

    if (pfnCompletion)
        pfnCompletion(pCon, rc, (void*)(pu64Completion+1));
}

void* NemuMpCrShgsmiTransportCmdCreateWriteReadAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, uint32_t u32ClientID, void *pvBuffer, uint32_t cbBuffer,
        PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION pfnCompletion, uint32_t cbContextData)
{
    const uint32_t cBuffers = 3;
    const uint32_t cbCmd = NEMUMP_CRSHGSMICON_DR_SIZE(cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITEREAD), cbContextData);
    PNEMUMP_DEVEXT pDevExt = pCon->pDevExt;
    PNEMUVDMACBUF_DR pDr = nemuVdmaCBufDrCreate(&pDevExt->u.primary.Vdma, cbCmd);
    if (!pDr)
    {
        WARN(("nemuVdmaCBufDrCreate failed"));
        return NULL;
    }

    PNEMUMP_CRSHGSMICON_BUFDR pWbDr = nemuMpCrShgsmiBufCacheAllocAny(pCon, &pCon->WbDrCache, 1000);
    if (!pWbDr)
    {
        WARN(("nemuMpCrShgsmiBufCacheAlloc for wb dr failed"));
        nemuVdmaCBufDrFree(&pDevExt->u.primary.Vdma, pDr);
        return NULL;
    }

    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUMP_CRSHGSMICON_DR_GET_CRCMD(pHdr);
    PNEMUMP_CRHGSMICMD_WRITEREAD pWrData = NEMUMP_CRSHGSMICON_DR_GET_CMDBUF(pHdr, cBuffers, NEMUMP_CRHGSMICMD_WRITEREAD);
    CRNEMUHGSMIWRITEREAD *pCmd = &pWrData->Cmd;

    pDr->fFlags = NEMUVDMACBUF_FLAG_BUF_FOLLOWS_DR;
    pDr->cbBuf = cbCmd;
    pDr->rc = VERR_NOT_IMPLEMENTED;
//    pDr->Location.offVramBuf = nemuMpCrShgsmiTransportBufOffset(pCon, pCmd);


    pHdr->enmType = NEMUVDMACMD_TYPE_CHROMIUM_CMD;
    pHdr->u32CmdSpecific = 0;

    pBody->cBuffers = cBuffers;

    pCmd->hdr.result      = VERR_WRONG_ORDER;
    pCmd->hdr.u32ClientID = u32ClientID;
    pCmd->hdr.u32Function = SHCRGL_GUEST_FN_WRITE_READ;
    //    pCmd->hdr.u32Reserved = 0;
    pCmd->iBuffer = 1;
    pCmd->iWriteback = 2;
    pCmd->cbWriteback = 0;

    NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[0];
    pBufCmd->offBuffer = nemuVdmaCBufDrPtrOffset(&pDevExt->u.primary.Vdma, pCmd);
    pBufCmd->cbBuffer = sizeof (*pCmd);
    pBufCmd->u32GuestData = 0;
    pBufCmd->u64GuestData = (uint64_t)pfnCompletion;

    pBufCmd = &pBody->aBuffers[1];
    pBufCmd->offBuffer = nemuMpCrShgsmiTransportBufOffset(pCon, pvBuffer);
    pBufCmd->cbBuffer = cbBuffer;
    pBufCmd->u32GuestData = 0;
    pBufCmd->u64GuestData = 0;

    pBufCmd = &pBody->aBuffers[2];
    pBufCmd->offBuffer = nemuMpCrShgsmiTransportBufOffset(pCon, pWbDr->pvBuf);
    pBufCmd->cbBuffer = pWbDr->cbBuf;
    pBufCmd->u32GuestData = 0;
    pBufCmd->u64GuestData = (uint64_t)pWbDr;

    return NEMUMP_CRSHGSMICON_DR_GET_CMDCTX(pHdr, cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITEREAD), void);
}

static void * nemuMpCrShgsmiTransportCmdVbvaCreateWriteAsync(PNEMUMP_DEVEXT pDevExt, uint32_t u32ClientID, void *pvBuffer, uint32_t cbBuffer, PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION pfnCompletion, uint32_t cbContextData)
{
    const uint32_t cBuffers = 2;
    const uint32_t cbCmd = NEMUMP_CRSHGSMICON_CMD_SIZE(cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITE) + 8, cbContextData);
    NEMUCMDVBVA_CRCMD_CMD* pCmd = NemuCmdVbvaConCmdAlloc(pDevExt, cbCmd);
    if (!pCmd)
    {
        WARN(("NemuCmdVbvaConCmdAlloc failed"));
        return NULL;
    }

    pCmd->cBuffers = cBuffers;

    PNEMUMP_CRHGSMICMD_WRITE pWrData = NEMUMP_CRSHGSMICON_CMD_GET_CMDBUF(pCmd, cBuffers, NEMUMP_CRHGSMICMD_WRITE);
    CRNEMUHGSMIWRITE *pCmdWrite = &pWrData->Cmd;

    pCmdWrite->hdr.result      = VERR_WRONG_ORDER;
    pCmdWrite->hdr.u32ClientID = u32ClientID;
    pCmdWrite->hdr.u32Function = SHCRGL_GUEST_FN_WRITE;
    //    pCmdWrite->hdr.u32Reserved = 0;
    pCmdWrite->iBuffer = 1;

    NEMUCMDVBVA_CRCMD_BUFFER *pBufCmd = &pCmd->aBuffers[0];
    pBufCmd->offBuffer = (NEMUCMDVBVAOFFSET)nemuMpCrShgsmiBufferOffset(pDevExt, pCmdWrite);
    pBufCmd->cbBuffer = sizeof (*pCmdWrite);

    pBufCmd = &pCmd->aBuffers[1];
    pBufCmd->offBuffer = (NEMUCMDVBVAOFFSET)nemuMpCrShgsmiBufferOffset(pDevExt, pvBuffer);
    pBufCmd->cbBuffer = cbBuffer;

    uint64_t*pu64Completion = NEMUMP_CRSHGSMICON_CMD_GET_CMDCTX(pCmd, cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITE), uint64_t);
    *pu64Completion = (uint64_t)pfnCompletion;
    return (void*)(pu64Completion+1);
}

void* nemuMpCrShgsmiTransportCmdVdmaCreateWriteAsync(PNEMUMP_DEVEXT pDevExt, uint32_t u32ClientID, void *pvBuffer, uint32_t cbBuffer,
        PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION pfnCompletion, uint32_t cbContextData)
{
    const uint32_t cBuffers = 2;
    const uint32_t cbCmd = NEMUMP_CRSHGSMICON_DR_SIZE(cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITE), cbContextData);
    PNEMUVDMACBUF_DR pDr = nemuVdmaCBufDrCreate(&pDevExt->u.primary.Vdma, cbCmd);
    if (!pDr)
    {
        WARN(("nemuVdmaCBufDrCreate failed"));
        return NULL;
    }

    PNEMUVDMACMD pHdr = NEMUVDMACBUF_DR_TAIL(pDr, NEMUVDMACMD);
    NEMUVDMACMD_CHROMIUM_CMD *pBody = NEMUMP_CRSHGSMICON_DR_GET_CRCMD(pHdr);
    PNEMUMP_CRHGSMICMD_WRITE pWrData = NEMUMP_CRSHGSMICON_DR_GET_CMDBUF(pHdr, cBuffers, NEMUMP_CRHGSMICMD_WRITE);
    CRNEMUHGSMIWRITE *pCmd = &pWrData->Cmd;

    pDr->fFlags = NEMUVDMACBUF_FLAG_BUF_FOLLOWS_DR;
    pDr->cbBuf = cbCmd;
    pDr->rc = VERR_NOT_IMPLEMENTED;
//    pDr->Location.offVramBuf = nemuMpCrShgsmiTransportBufOffset(pCon, pCmd);

    pHdr->enmType = NEMUVDMACMD_TYPE_CHROMIUM_CMD;
    pHdr->u32CmdSpecific = 0;

    pBody->cBuffers = cBuffers;

    pCmd->hdr.result      = VERR_WRONG_ORDER;
    pCmd->hdr.u32ClientID = u32ClientID;
    pCmd->hdr.u32Function = SHCRGL_GUEST_FN_WRITE;
    //    pCmd->hdr.u32Reserved = 0;
    pCmd->iBuffer = 1;

    NEMUVDMACMD_CHROMIUM_BUFFER *pBufCmd = &pBody->aBuffers[0];
    pBufCmd->offBuffer = nemuVdmaCBufDrPtrOffset(&pDevExt->u.primary.Vdma, pCmd);
    pBufCmd->cbBuffer = sizeof (*pCmd);
    pBufCmd->u32GuestData = 0;
    pBufCmd->u64GuestData = (uint64_t)pfnCompletion;

    pBufCmd = &pBody->aBuffers[1];
    pBufCmd->offBuffer = nemuMpCrShgsmiBufferOffset(pDevExt, pvBuffer);
    pBufCmd->cbBuffer = cbBuffer;
    pBufCmd->u32GuestData = 0;
    pBufCmd->u64GuestData = 0;

    return NEMUMP_CRSHGSMICON_DR_GET_CMDCTX(pHdr, cBuffers, sizeof (NEMUMP_CRHGSMICMD_WRITE), void);
}

void* NemuMpCrShgsmiTransportCmdCreateWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, uint32_t u32ClientID, void *pvBuffer, uint32_t cbBuffer,
        PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION pfnCompletion, uint32_t cbContextData)
{
    PNEMUMP_DEVEXT pDevExt = pCon->pDevExt;
    if (pDevExt->fCmdVbvaEnabled)
        return nemuMpCrShgsmiTransportCmdVbvaCreateWriteAsync(pDevExt, u32ClientID, pvBuffer, cbBuffer, pfnCompletion, cbContextData);
    return nemuMpCrShgsmiTransportCmdVdmaCreateWriteAsync(pDevExt, u32ClientID, pvBuffer, cbBuffer, pfnCompletion, cbContextData);
}

int NemuMpCrShgsmiTransportCmdSubmitWriteReadAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    NEMUVDMACMD* pHdr = NEMUMP_CRSHGSMICON_DR_GET_FROM_CMDCTX(pvContext, 3, sizeof (NEMUMP_CRHGSMICMD_WRITEREAD));
    return nemuMpCrShgsmiTransportCmdSubmitDmaCmd(pCon, pHdr, nemuMpCrShgsmiTransportSendWriteReadAsyncCompletion);
}

static int nemuMpCrShgsmiTransportCmdVdmaSubmitWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    NEMUVDMACMD* pHdr = NEMUMP_CRSHGSMICON_DR_GET_FROM_CMDCTX(pvContext, 2, sizeof (NEMUMP_CRHGSMICMD_WRITE));
    return nemuMpCrShgsmiTransportCmdSubmitDmaCmd(pCon, pHdr, nemuMpCrShgsmiTransportVdmaSendWriteAsyncCompletion);
}

static int nemuMpCrShgsmiTransportCmdVbvaSubmitWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    PNEMUMP_DEVEXT pDevExt = pCon->pDevExt;
    NEMUCMDVBVA_CRCMD_CMD* pCmd = NEMUMP_CRSHGSMICON_CMD_GET_FROM_CMDCTX(pvContext, 2, sizeof (NEMUMP_CRHGSMICMD_WRITE) + 8, NEMUCMDVBVA_CRCMD_CMD);
    return NemuCmdVbvaConCmdSubmitAsync(pDevExt, pCmd, nemuMpCrShgsmiTransportVbvaSendWriteAsyncCompletion, pCon);
}

int NemuMpCrShgsmiTransportCmdSubmitWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    if (pCon->pDevExt->fCmdVbvaEnabled)
        return nemuMpCrShgsmiTransportCmdVbvaSubmitWriteAsync(pCon, pvContext);
    return nemuMpCrShgsmiTransportCmdVdmaSubmitWriteAsync(pCon, pvContext);
}

void NemuMpCrShgsmiTransportCmdTermWriteReadAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    NEMUVDMACMD* pHdr = NEMUMP_CRSHGSMICON_DR_GET_FROM_CMDCTX(pvContext, 3, sizeof (NEMUMP_CRHGSMICMD_WRITEREAD));
    nemuMpCrShgsmiTransportCmdTermDmaCmd(pCon, pHdr);
}

static void nemuMpCrShgsmiTransportCmdVbvaTermWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    NEMUCMDVBVA_CRCMD_CMD* pCmd = NEMUMP_CRSHGSMICON_CMD_GET_FROM_CMDCTX(pvContext, 2, sizeof (NEMUMP_CRHGSMICMD_WRITE) + 8, NEMUCMDVBVA_CRCMD_CMD);
    NemuCmdVbvaConCmdFree(pCon->pDevExt, pCmd);
}

static void nemuMpCrShgsmiTransportCmdVdmaTermWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    NEMUVDMACMD* pHdr = NEMUMP_CRSHGSMICON_DR_GET_FROM_CMDCTX(pvContext, 2, sizeof (NEMUMP_CRHGSMICMD_WRITE));
    nemuMpCrShgsmiTransportCmdTermDmaCmd(pCon, pHdr);
}

void NemuMpCrShgsmiTransportCmdTermWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext)
{
    if (pCon->pDevExt->fCmdVbvaEnabled)
        nemuMpCrShgsmiTransportCmdVbvaTermWriteAsync(pCon, pvContext);
    else
        nemuMpCrShgsmiTransportCmdVdmaTermWriteAsync(pCon, pvContext);
}

static int nemuMpCrCtlAddRef(PNEMUMP_CRCTLCON pCrCtlCon)
{
    if (pCrCtlCon->cCrCtlRefs++)
        return VINF_ALREADY_INITIALIZED;

    int rc = VbglR0CrCtlCreate(&pCrCtlCon->hCrCtl);
    if (RT_SUCCESS(rc))
    {
        Assert(pCrCtlCon->hCrCtl);
        return VINF_SUCCESS;
    }

    WARN(("nemuCrCtlCreate failed, rc (%d)", rc));

    --pCrCtlCon->cCrCtlRefs;
    return rc;
}

static int nemuMpCrCtlRelease(PNEMUMP_CRCTLCON pCrCtlCon)
{
    Assert(pCrCtlCon->cCrCtlRefs);
    if (--pCrCtlCon->cCrCtlRefs)
    {
        return VINF_SUCCESS;
    }

    int rc = VbglR0CrCtlDestroy(pCrCtlCon->hCrCtl);
    if (RT_SUCCESS(rc))
    {
        pCrCtlCon->hCrCtl = NULL;
        return VINF_SUCCESS;
    }

    WARN(("nemuCrCtlDestroy failed, rc (%d)", rc));

    ++pCrCtlCon->cCrCtlRefs;
    return rc;
}

static int nemuMpCrCtlConSetVersion(PNEMUMP_CRCTLCON pCrCtlCon, uint32_t u32ClientID, uint32_t vMajor, uint32_t vMinor)
{
    CRNEMUHGCMSETVERSION parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_SET_VERSION;
    parms.hdr.cParms      = SHCRGL_CPARMS_SET_VERSION;

    parms.vMajor.type      = VMMDevHGCMParmType_32bit;
    parms.vMajor.u.value32 = vMajor;
    parms.vMinor.type      = VMMDevHGCMParmType_32bit;
    parms.vMinor.u.value32 = vMinor;

    rc = VbglR0CrCtlConCall(pCrCtlCon->hCrCtl, &parms.hdr, sizeof (parms));
    if (RT_FAILURE(rc))
    {
        WARN(("nemuCrCtlConCall failed, rc (%d)", rc));
        return rc;
    }

    if (RT_FAILURE(parms.hdr.result))
    {
        WARN(("version validation failed, rc (%d)", parms.hdr.result));
        return parms.hdr.result;
    }
    return VINF_SUCCESS;
}

static int nemuMpCrCtlConGetCapsLegacy(PNEMUMP_CRCTLCON pCrCtlCon, uint32_t u32ClientID, uint32_t *pu32Caps)
{
    CRNEMUHGCMGETCAPS parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_GET_CAPS_LEGACY;
    parms.hdr.cParms      = SHCRGL_CPARMS_GET_CAPS_LEGACY;

    parms.Caps.type      = VMMDevHGCMParmType_32bit;
    parms.Caps.u.value32 = 0;

    *pu32Caps = 0;

    rc = VbglR0CrCtlConCall(pCrCtlCon->hCrCtl, &parms.hdr, sizeof (parms));
    if (RT_FAILURE(rc))
    {
        WARN(("nemuCrCtlConCall failed, rc (%d)", rc));
        return rc;
    }

    if (RT_FAILURE(parms.hdr.result))
    {
        WARN(("SHCRGL_GUEST_FN_GET_CAPS_LEGACY failed, rc (%d)", parms.hdr.result));
        return parms.hdr.result;
    }

    /* if host reports it supports CR_NEMU_CAP_CMDVBVA, clean it up,
     * we only support CR_NEMU_CAP_CMDVBVA of the proper version reported by SHCRGL_GUEST_FN_GET_CAPS_NEW */
    parms.Caps.u.value32 &= ~CR_NEMU_CAP_CMDVBVA;

    *pu32Caps = parms.Caps.u.value32;

    return VINF_SUCCESS;
}

static int nemuMpCrCtlConGetCapsNew(PNEMUMP_CRCTLCON pCrCtlCon, uint32_t u32ClientID, CR_CAPS_INFO *pCapsInfo)
{
    pCapsInfo->u32Caps = CR_NEMU_CAPS_ALL;
    pCapsInfo->u32CmdVbvaVersion = CR_CMDVBVA_VERSION;

    CRNEMUHGCMGETCAPS parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_GET_CAPS_NEW;
    parms.hdr.cParms      = SHCRGL_CPARMS_GET_CAPS_NEW;

    parms.Caps.type      = VMMDevHGCMParmType_LinAddr;
    parms.Caps.u.Pointer.u.linearAddr = (uintptr_t)pCapsInfo;
    parms.Caps.u.Pointer.size = sizeof (*pCapsInfo);

    rc = VbglR0CrCtlConCall(pCrCtlCon->hCrCtl, &parms.hdr, sizeof (parms));
    if (RT_FAILURE(rc))
    {
        WARN(("nemuCrCtlConCall failed, rc (%d)", rc));
        return rc;
    }

    if (RT_FAILURE(parms.hdr.result))
    {
        WARN(("SHCRGL_GUEST_FN_GET_CAPS_NEW failed, rc (%d)", parms.hdr.result));
        return parms.hdr.result;
    }

    if (pCapsInfo->u32CmdVbvaVersion != CR_CMDVBVA_VERSION)
    {
        WARN(("CmdVbva version mismatch (%d), expected(%d)", pCapsInfo->u32CmdVbvaVersion, CR_CMDVBVA_VERSION));
        pCapsInfo->u32Caps &= ~CR_NEMU_CAP_CMDVBVA;
    }

    pCapsInfo->u32Caps &= CR_NEMU_CAPS_ALL;

    return VINF_SUCCESS;
}

static int nemuMpCrCtlConSetPID(PNEMUMP_CRCTLCON pCrCtlCon, uint32_t u32ClientID)
{
    CRNEMUHGCMSETPID parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_SET_PID;
    parms.hdr.cParms      = SHCRGL_CPARMS_SET_PID;

    parms.u64PID.type     = VMMDevHGCMParmType_64bit;
    parms.u64PID.u.value64 = (uint64_t)PsGetCurrentProcessId();

    Assert(parms.u64PID.u.value64);

    rc = VbglR0CrCtlConCall(pCrCtlCon->hCrCtl, &parms.hdr, sizeof (parms));
    if (RT_FAILURE(rc))
    {
        WARN(("nemuCrCtlConCall failed, rc (%d)", rc));
        return rc;
    }

    if (RT_FAILURE(parms.hdr.result))
    {
        WARN(("set PID failed, rc (%d)", parms.hdr.result));
        return parms.hdr.result;
    }
    return VINF_SUCCESS;
}

int NemuMpCrCtlConConnectHgcm(PNEMUMP_CRCTLCON pCrCtlCon,
        uint32_t crVersionMajor, uint32_t crVersionMinor,
        uint32_t *pu32ClientID)
{
    uint32_t u32ClientID;
    int rc = nemuMpCrCtlAddRef(pCrCtlCon);
    if (RT_SUCCESS(rc))
    {
        rc = VbglR0CrCtlConConnect(pCrCtlCon->hCrCtl, &u32ClientID);
        if (RT_SUCCESS(rc))
        {
            rc = nemuMpCrCtlConSetVersion(pCrCtlCon, u32ClientID, crVersionMajor, crVersionMinor);
            if (RT_SUCCESS(rc))
            {
                rc = nemuMpCrCtlConSetPID(pCrCtlCon, u32ClientID);
                if (RT_SUCCESS(rc))
                {
                    *pu32ClientID = u32ClientID;
                    return VINF_SUCCESS;
                }
                else
                {
                    WARN(("nemuMpCrCtlConSetPID failed, rc (%d)", rc));
                }
            }
            else
            {
                WARN(("nemuMpCrCtlConSetVersion failed, rc (%d)", rc));
            }
            VbglR0CrCtlConDisconnect(pCrCtlCon->hCrCtl, u32ClientID);
        }
        else
        {
            WARN(("nemuCrCtlConConnect failed, rc (%d)", rc));
        }
        nemuMpCrCtlRelease(pCrCtlCon);
    }
    else
    {
        WARN(("nemuMpCrCtlAddRef failed, rc (%d)", rc));
    }

    *pu32ClientID = 0;
    Assert(RT_FAILURE(rc));
    return rc;
}

int NemuMpCrCtlConConnectVbva(PNEMUMP_DEVEXT pDevExt, PNEMUMP_CRCTLCON pCrCtlCon,
        uint32_t crVersionMajor, uint32_t crVersionMinor,
        uint32_t *pu32ClientID)
{
    if (pCrCtlCon->hCrCtl)
    {
        WARN(("pCrCtlCon is HGCM connection"));
        return VERR_INVALID_STATE;
    }

    Assert(!pCrCtlCon->cCrCtlRefs);
    return NemuCmdVbvaConConnect(pDevExt, &pDevExt->CmdVbva,
            crVersionMajor, crVersionMinor,
            pu32ClientID);
}

int NemuMpCrCtlConConnect(PNEMUMP_DEVEXT pDevExt, PNEMUMP_CRCTLCON pCrCtlCon,
        uint32_t crVersionMajor, uint32_t crVersionMinor,
        uint32_t *pu32ClientID)
{
    if (pDevExt->fCmdVbvaEnabled)
    {
        return NemuMpCrCtlConConnectVbva(pDevExt, pCrCtlCon,
                crVersionMajor, crVersionMinor,
                pu32ClientID);
    }
    return NemuMpCrCtlConConnectHgcm(pCrCtlCon,
            crVersionMajor, crVersionMinor,
            pu32ClientID);
}

int NemuMpCrCtlConDisconnectHgcm(PNEMUMP_CRCTLCON pCrCtlCon, uint32_t u32ClientID)
{
    int rc = VbglR0CrCtlConDisconnect(pCrCtlCon->hCrCtl, u32ClientID);
    if (RT_SUCCESS(rc))
    {
        nemuMpCrCtlRelease(pCrCtlCon);
        return VINF_SUCCESS;
    }
    WARN(("nemuCrCtlConDisconnect failed, rc (%d)", rc));
    return rc;
}

int NemuMpCrCtlConDisconnectVbva(PNEMUMP_DEVEXT pDevExt, PNEMUMP_CRCTLCON pCrCtlCon, uint32_t u32ClientID)
{
    Assert(!pCrCtlCon->hCrCtl);
    Assert(!pCrCtlCon->cCrCtlRefs);
    return NemuCmdVbvaConDisconnect(pDevExt, &pDevExt->CmdVbva, u32ClientID);
}

int NemuMpCrCtlConDisconnect(PNEMUMP_DEVEXT pDevExt, PNEMUMP_CRCTLCON pCrCtlCon, uint32_t u32ClientID)
{
    if (!pCrCtlCon->hCrCtl)
        return NemuMpCrCtlConDisconnectVbva(pDevExt, pCrCtlCon, u32ClientID);
    return NemuMpCrCtlConDisconnectHgcm(pCrCtlCon, u32ClientID);
}

int NemuMpCrCtlConCall(PNEMUMP_CRCTLCON pCrCtlCon, NemuGuestHGCMCallInfo *pData, uint32_t cbData)
{
    int rc = VbglR0CrCtlConCall(pCrCtlCon->hCrCtl, pData, cbData);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    WARN(("nemuCrCtlConCallUserData failed, rc(%d)", rc));
    return rc;
}

int NemuMpCrCtlConCallUserData(PNEMUMP_CRCTLCON pCrCtlCon, NemuGuestHGCMCallInfo *pData, uint32_t cbData)
{
    int rc = VbglR0CrCtlConCallUserData(pCrCtlCon->hCrCtl, pData, cbData);
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    WARN(("nemuCrCtlConCallUserData failed, rc(%d)", rc));
    return rc;
}

void NemuMpCrCtlConInit()
{
    g_NemuMpCr3DSupported = 0;
    memset(&g_NemuMpCrHostCapsInfo, 0, sizeof (g_NemuMpCrHostCapsInfo));

    NEMUMP_CRCTLCON CrCtlCon = {0};
    uint32_t u32ClientID = 0;
    int rc = NemuMpCrCtlConConnectHgcm(&CrCtlCon, CR_PROTOCOL_VERSION_MAJOR, CR_PROTOCOL_VERSION_MINOR, &u32ClientID);
    if (RT_FAILURE(rc))
    {
        LOGREL(("NemuMpCrCtlConConnectHgcm failed with rc(%d), 3D not supported!", rc));
        return;
    }

    rc = nemuMpCrCtlConGetCapsNew(&CrCtlCon, u32ClientID, &g_NemuMpCrHostCapsInfo);
    if (RT_FAILURE(rc))
    {
        WARN(("nemuMpCrCtlConGetCapsNew failed rc (%d), ignoring..", rc));
        g_NemuMpCrHostCapsInfo.u32CmdVbvaVersion = 0;
        rc = nemuMpCrCtlConGetCapsLegacy(&CrCtlCon, u32ClientID, &g_NemuMpCrHostCapsInfo.u32Caps);
        if (RT_FAILURE(rc))
        {
            WARN(("nemuMpCrCtlConGetCapsLegacy failed rc (%d), ignoring..", rc));
            g_NemuMpCrHostCapsInfo.u32Caps = 0;
        }
    }

    if (g_NemuMpCrHostCapsInfo.u32Caps & CR_NEMU_CAP_HOST_CAPS_NOT_SUFFICIENT)
    {
        LOGREL(("Insufficient host 3D capabilities"));
        g_NemuMpCr3DSupported = 0;
        memset(&g_NemuMpCrHostCapsInfo, 0, sizeof (g_NemuMpCrHostCapsInfo));
    }
    else
    {
        g_NemuMpCr3DSupported = 1;
    }

#if 0 //ndef DEBUG_misha
    g_NemuMpCrHostCapsInfo.u32Caps &= ~CR_NEMU_CAP_CMDVBVA;
    g_NemuMpCrHostCapsInfo.u32CmdVbvaVersion = 0;
#endif

    rc = NemuMpCrCtlConDisconnectHgcm(&CrCtlCon, u32ClientID);
    if (RT_FAILURE(rc))
        WARN(("NemuMpCrCtlConDisconnectHgcm failed rc (%d), ignoring..", rc));
}

int NemuMpCrCmdRxReadbackHandler(CRMessageReadback *pRx, uint32_t cbRx)
{
    if (cbRx < sizeof (*pRx))
    {
        WARN(("invalid rx size %d", cbRx));
        return VERR_INVALID_PARAMETER;
    }
    void* pvData = NemuMpCrCmdRxReadbackData(pRx);
    uint32_t cbData = NemuMpCrCmdRxReadbackDataSize(pRx, cbRx);
    void *pvDataPtr = *((void**)&pRx->readback_ptr);
    memcpy(pvDataPtr, pvData, cbData);
    return VINF_SUCCESS;
}

int NemuMpCrCmdRxHandler(CRMessageHeader *pRx, uint32_t cbRx)
{
    if (cbRx < sizeof (*pRx))
    {
        WARN(("invalid rx size %d", cbRx));
        return VERR_INVALID_PARAMETER;
    }
    CRMessageHeader *pHdr = (CRMessageHeader*)pRx;
    switch (pHdr->type)
    {
        case CR_MESSAGE_READBACK:
            return NemuMpCrCmdRxReadbackHandler((CRMessageReadback*)pRx, cbRx);
        default:
            WARN(("unsupported rx message type: %d", pHdr->type));
            return VERR_INVALID_PARAMETER;
    }
}

#endif
