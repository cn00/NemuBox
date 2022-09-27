/* $Id: NemuMPCr.h $ */
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
#ifndef ___NemuMPCr_h__
#define ___NemuMPCr_h__

#ifdef NEMU_WITH_CROGL

# include <Nemu/NemuGuestLib.h>

typedef struct NEMUMP_CRCTLCON
{
    VBGLCRCTLHANDLE hCrCtl;
    uint32_t cCrCtlRefs;
} NEMUMP_CRCTLCON, *PNEMUMP_CRCTLCON;

void NemuMpCrCtlConInit();

bool NemuMpCrCtlConIs3DSupported();

int NemuMpCrCtlConConnect(PNEMUMP_DEVEXT pDevExt, PNEMUMP_CRCTLCON pCrCtlCon,
        uint32_t crVersionMajor, uint32_t crVersionMinor,
        uint32_t *pu32ClientID);
int NemuMpCrCtlConDisconnect(PNEMUMP_DEVEXT pDevExt, PNEMUMP_CRCTLCON pCrCtlCon, uint32_t u32ClientID);
int NemuMpCrCtlConCall(PNEMUMP_CRCTLCON pCrCtlCon, struct NemuGuestHGCMCallInfo *pData, uint32_t cbData);
int NemuMpCrCtlConCallUserData(PNEMUMP_CRCTLCON pCrCtlCon, struct NemuGuestHGCMCallInfo *pData, uint32_t cbData);

# include <cr_pack.h>

typedef struct NEMUMP_CRSHGSMICON_BUFDR
{
    uint32_t cbBuf;
    void *pvBuf;
} NEMUMP_CRSHGSMICON_BUFDR, *PNEMUMP_CRSHGSMICON_BUFDR;

typedef struct NEMUMP_CRSHGSMICON_BUFDR_CACHE
{
    volatile PNEMUMP_CRSHGSMICON_BUFDR pBufDr;
} NEMUMP_CRSHGSMICON_BUFDR_CACHE, *PNEMUMP_CRSHGSMICON_BUFDR_CACHE;

typedef struct NEMUMP_CRSHGSMITRANSPORT
{
    PNEMUMP_DEVEXT pDevExt;
    NEMUMP_CRSHGSMICON_BUFDR_CACHE WbDrCache;
} NEMUMP_CRSHGSMITRANSPORT, *PNEMUMP_CRSHGSMITRANSPORT;

/** the rx buffer passed here is only valid in the context of the callback.
 * the callee must NOT free it or use outside of the callback context.
 * */
typedef DECLCALLBACK(void) FNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION(PNEMUMP_CRSHGSMITRANSPORT pCon, int rc, void *pvRx, uint32_t cbRx, void *pvCtx);
typedef FNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION *PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION;

typedef DECLCALLBACK(void) FNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION(PNEMUMP_CRSHGSMITRANSPORT pCon, int rc, void *pvCtx);
typedef FNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION *PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION;

int NemuMpCrShgsmiTransportCreate(PNEMUMP_CRSHGSMITRANSPORT pCon, PNEMUMP_DEVEXT pDevExt);
void NemuMpCrShgsmiTransportTerm(PNEMUMP_CRSHGSMITRANSPORT pCon);
void* NemuMpCrShgsmiTransportCmdCreateWriteReadAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, uint32_t u32ClientID, void *pvBuffer, uint32_t cbBuffer,
        PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEREADASYNC_COMPLETION pfnCompletion, uint32_t cbContextData);
void* NemuMpCrShgsmiTransportCmdCreateWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, uint32_t u32ClientID, void *pvBuffer, uint32_t cbBuffer,
        PFNNEMUMP_CRSHGSMITRANSPORT_SENDWRITEASYNC_COMPLETION pfnCompletion, uint32_t cbContextData);
int NemuMpCrShgsmiTransportCmdSubmitWriteReadAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext);
int NemuMpCrShgsmiTransportCmdSubmitWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext);
void NemuMpCrShgsmiTransportCmdTermWriteReadAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext);
void NemuMpCrShgsmiTransportCmdTermWriteAsync(PNEMUMP_CRSHGSMITRANSPORT pCon, void *pvContext);

void* NemuMpCrShgsmiTransportBufAlloc(PNEMUMP_CRSHGSMITRANSPORT pCon, uint32_t cbBuffer);
void NemuMpCrShgsmiTransportBufFree(PNEMUMP_CRSHGSMITRANSPORT pCon, void* pvBuffer);

typedef struct NEMUMP_CRPACKER
{
    CRPackContext CrPacker;
    CRPackBuffer CrBuffer;
} NEMUMP_CRPACKER, *PNEMUMP_CRPACKER;

DECLINLINE(void) NemuMpCrPackerInit(PNEMUMP_CRPACKER pPacker)
{
    memset(pPacker, 0, sizeof (*pPacker));
}

DECLINLINE(void) NemuMpCrPackerTerm(PNEMUMP_CRPACKER pPacker)
{}

DECLINLINE(void) NemuMpCrPackerTxBufferInit(PNEMUMP_CRPACKER pPacker, void *pvBuffer, uint32_t cbBuffer, uint32_t cCommands)
{
    crPackInitBuffer(&pPacker->CrBuffer, pvBuffer, cbBuffer, cbBuffer, cCommands);
    crPackSetBuffer(&pPacker->CrPacker, &pPacker->CrBuffer);
}

DECLINLINE(CRMessageOpcodes*) nemuMpCrPackerPrependHeader( const CRPackBuffer *pBuffer, uint32_t *cbData, void **ppvPackBuffer)
{
    UINT num_opcodes;
    CRMessageOpcodes *hdr;

    Assert(pBuffer);
    Assert(pBuffer->opcode_current < pBuffer->opcode_start);
    Assert(pBuffer->opcode_current >= pBuffer->opcode_end);
    Assert(pBuffer->data_current > pBuffer->data_start);
    Assert(pBuffer->data_current <= pBuffer->data_end);

    num_opcodes = (UINT)(pBuffer->opcode_start - pBuffer->opcode_current);
    hdr = (CRMessageOpcodes *)
        ( pBuffer->data_start - ( ( num_opcodes + 3 ) & ~0x3 ) - sizeof(*hdr) );

    Assert((void *) hdr >= pBuffer->pack);

    hdr->header.type = CR_MESSAGE_OPCODES;
    hdr->numOpcodes  = num_opcodes;

    *cbData = (UINT)(pBuffer->data_current - (unsigned char *) hdr);
    *ppvPackBuffer = pBuffer->pack;

    return hdr;
}

DECLINLINE(void*) NemuMpCrPackerTxBufferComplete(PNEMUMP_CRPACKER pPacker, uint32_t *pcbBuffer, void **ppvPackBuffer)
{
    crPackReleaseBuffer(&pPacker->CrPacker);
    uint32_t cbData;
    CRMessageOpcodes *pHdr;
    void *pvPackBuffer;
    if (pPacker->CrBuffer.opcode_current != pPacker->CrBuffer.opcode_start)
        pHdr = nemuMpCrPackerPrependHeader(&pPacker->CrBuffer, &cbData, &pvPackBuffer);
    else
    {
        cbData = 0;
        pHdr = NULL;
        pvPackBuffer = NULL;
    }
    *pcbBuffer = cbData;
    *ppvPackBuffer = pvPackBuffer;
    return pHdr;
}

DECLINLINE(uint32_t) NemuMpCrPackerTxBufferGetFreeBufferSize(PNEMUMP_CRPACKER pPacker)
{
    return (uint32_t)(pPacker->CrBuffer.data_end - pPacker->CrBuffer.data_start);
}

DECLINLINE(void) nemuMpCrUnpackerRxWriteback(CRMessageWriteback *pWb)
{
    int *pWriteback;
    memcpy(&pWriteback, &(pWb->writeback_ptr), sizeof (pWriteback));
    (*pWriteback)--;
}

DECLINLINE(void) nemuMpCrUnpackerRxReadback(CRMessageReadback *pRb, uint32_t cbRx)
{
    int cbPayload = cbRx - sizeof (*pRb);
    int *pWriteback;
    void *pDst;
    memcpy(&pWriteback, &(pRb->writeback_ptr), sizeof (pWriteback));
    memcpy(&pDst, &(pRb->readback_ptr), sizeof (pDst));

    (*pWriteback)--;
    memcpy(pDst, ((uint8_t*)pRb) + sizeof (*pRb), cbPayload);
}

DECLINLINE(int) NemuMpCrUnpackerRxBufferProcess(void *pvBuffer, uint32_t cbBuffer)
{
    CRMessage *pMsg = (CRMessage*)pvBuffer;
    switch (pMsg->header.type)
    {
        case CR_MESSAGE_WRITEBACK:
            nemuMpCrUnpackerRxWriteback(&(pMsg->writeback));
            return VINF_SUCCESS;
        case CR_MESSAGE_READBACK:
            nemuMpCrUnpackerRxReadback(&(pMsg->readback), cbBuffer);
            return VINF_SUCCESS;
        default:
            WARN(("unknown msg code %d", pMsg->header.type));
            return VERR_NOT_SUPPORTED;
    }
}

DECLINLINE(void*) NemuMpCrCmdRxReadbackData(CRMessageReadback *pRx)
{
    return (void*)(pRx+1);
}

DECLINLINE(uint32_t) NemuMpCrCmdRxReadbackDataSize(CRMessageReadback *pRx, uint32_t cbRx)
{
    return cbRx - sizeof (*pRx);
}
int NemuMpCrCmdRxReadbackHandler(CRMessageReadback *pRx, uint32_t cbRx);
int NemuMpCrCmdRxHandler(CRMessageHeader *pRx, uint32_t cbRx);

/* must be called after calling NemuMpCrCtlConIs3DSupported only */
uint32_t NemuMpCrGetHostCaps();

#define NEMUMP_CRCMD_HEADER_SIZE sizeof (CRMessageOpcodes)
/* last +4 below is 4-aligned command opcode size (i.e. ((1 + 3) & ~3)) */
#define NEMUMP_CRCMD_SIZE_WINDOWPOSITION (20 + 4)
#define NEMUMP_CRCMD_SIZE_WINDOWVISIBLEREGIONS(_cRects) (16 + (_cRects) * 4 * sizeof (GLint) + 4)
#define NEMUMP_CRCMD_SIZE_NEMUTEXPRESENT(_cRects) (28 + (_cRects) * 4 * sizeof (GLint) + 4)
#define NEMUMP_CRCMD_SIZE_WINDOWSHOW (16 + 4)
#define NEMUMP_CRCMD_SIZE_WINDOWSIZE (20 + 4)
#define NEMUMP_CRCMD_SIZE_GETCHROMIUMPARAMETERVCR (36 + 4)
#define NEMUMP_CRCMD_SIZE_CHROMIUMPARAMETERICR (16 + 4)
#define NEMUMP_CRCMD_SIZE_WINDOWCREATE (256 + 28 + 4)
#define NEMUMP_CRCMD_SIZE_WINDOWDESTROY (12 + 4)
#define NEMUMP_CRCMD_SIZE_CREATECONTEXT (256 + 32 + 4)
#define NEMUMP_CRCMD_SIZE_DESTROYCONTEXT (12 + 4)

#endif /* #ifdef NEMU_WITH_CROGL */

#endif /* #ifndef ___NemuMPCr_h__ */

