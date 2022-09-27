/* $Id: NemuMPVbva.cpp $ */

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
#include "common/NemuMPCommon.h"

/*
 * Public hardware buffer methods.
 */
int nemuVbvaEnable (PNEMUMP_DEVEXT pDevExt, NEMUVBVAINFO *pVbva)
{
    if (NemuVBVAEnable(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx,
            pVbva->Vbva.pVBVA, pVbva->srcId))
        return VINF_SUCCESS;

    WARN(("NemuVBVAEnable failed!"));
    return VERR_GENERAL_FAILURE;
}

int nemuVbvaDisable (PNEMUMP_DEVEXT pDevExt, NEMUVBVAINFO *pVbva)
{
    NemuVBVADisable(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx, pVbva->srcId);
    return VINF_SUCCESS;
}

int nemuVbvaCreate(PNEMUMP_DEVEXT pDevExt, NEMUVBVAINFO *pVbva, ULONG offBuffer, ULONG cbBuffer, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    memset(pVbva, 0, sizeof(NEMUVBVAINFO));

    KeInitializeSpinLock(&pVbva->Lock);

    int rc = NemuMPCmnMapAdapterMemory(NemuCommonFromDeviceExt(pDevExt),
                                       (void**)&pVbva->Vbva.pVBVA,
                                       offBuffer,
                                       cbBuffer);
    if (RT_SUCCESS(rc))
    {
        Assert(pVbva->Vbva.pVBVA);
        NemuVBVASetupBufferContext(&pVbva->Vbva, offBuffer, cbBuffer);
        pVbva->srcId = srcId;
    }
    else
    {
        WARN(("NemuMPCmnMapAdapterMemory failed rc %d", rc));
    }


    return rc;
}

int nemuVbvaDestroy(PNEMUMP_DEVEXT pDevExt, NEMUVBVAINFO *pVbva)
{
    int rc = VINF_SUCCESS;
    NemuMPCmnUnmapAdapterMemory(NemuCommonFromDeviceExt(pDevExt), (void**)&pVbva->Vbva.pVBVA);
    memset(pVbva, 0, sizeof (NEMUVBVAINFO));
    return rc;
}

int nemuVbvaReportDirtyRect (PNEMUMP_DEVEXT pDevExt, PNEMUWDDM_SOURCE pSrc, RECT *pRectOrig)
{
    VBVACMDHDR hdr;

    RECT rect = *pRectOrig;

//        if (rect.left < 0) rect.left = 0;
//        if (rect.top < 0) rect.top = 0;
//        if (rect.right > (int)ppdev->cxScreen) rect.right = ppdev->cxScreen;
//        if (rect.bottom > (int)ppdev->cyScreen) rect.bottom = ppdev->cyScreen;

    hdr.x = (int16_t)rect.left;
    hdr.y = (int16_t)rect.top;
    hdr.w = (uint16_t)(rect.right - rect.left);
    hdr.h = (uint16_t)(rect.bottom - rect.top);

    hdr.x += (int16_t)pSrc->VScreenPos.x;
    hdr.y += (int16_t)pSrc->VScreenPos.y;

    if (NemuVBVAWrite(&pSrc->Vbva.Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx, &hdr, sizeof(hdr)))
        return VINF_SUCCESS;

    WARN(("NemuVBVAWrite failed"));
    return VERR_GENERAL_FAILURE;
}

#ifdef NEMU_WITH_CROGL
/* command vbva ring buffer */

/* customized VBVA implementation */

/* Forward declarations of internal functions. */
static void nemuHwBufferPlaceDataAt(PVBVAEXBUFFERCONTEXT pCtx, const void *p,
                                    uint32_t cb, uint32_t offset);
static bool nemuHwBufferWrite(PVBVAEXBUFFERCONTEXT pCtx,
                              PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                              const void *p, uint32_t cb);

DECLINLINE(void) nemuVBVAExFlush(struct VBVAEXBUFFERCONTEXT *pCtx, PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx)
{
    pCtx->pfnFlush(pCtx, pHGSMICtx, pCtx->pvFlush);
}

static int nemuCmdVbvaSubmitHgsmi(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, HGSMIOFFSET offDr)
{
    NemuVideoCmnPortWriteUlong(pHGSMICtx->port, offDr);
    /* Make the compiler aware that the host has changed memory. */
    ASMCompilerBarrier();
    return VINF_SUCCESS;
}
#define nemuCmdVbvaSubmit nemuCmdVbvaSubmitHgsmi

static NEMUCMDVBVA_CTL * nemuCmdVbvaCtlCreate(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, uint32_t cbCtl)
{
    Assert(cbCtl >= sizeof (NEMUCMDVBVA_CTL));
    return (NEMUCMDVBVA_CTL*)NemuSHGSMICommandAlloc(&pHGSMICtx->heapCtx, cbCtl, HGSMI_CH_VBVA, VBVA_CMDVBVA_CTL);
}

static void nemuCmdVbvaCtlFree(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, NEMUCMDVBVA_CTL * pCtl)
{
    NemuSHGSMICommandFree(&pHGSMICtx->heapCtx, pCtl);
}

static int nemuCmdVbvaCtlSubmitSync(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, NEMUCMDVBVA_CTL * pCtl)
{
    const NEMUSHGSMIHEADER* pHdr = NemuSHGSMICommandPrepSynch(&pHGSMICtx->heapCtx, pCtl);
    if (!pHdr)
    {
        WARN(("NemuSHGSMICommandPrepSynch returnd NULL"));
        return VERR_INVALID_PARAMETER;
    }

    HGSMIOFFSET offCmd = NemuSHGSMICommandOffset(&pHGSMICtx->heapCtx, pHdr);
    if (offCmd == HGSMIOFFSET_VOID)
    {
        WARN(("NemuSHGSMICommandOffset returnd NULL"));
        NemuSHGSMICommandCancelSynch(&pHGSMICtx->heapCtx, pHdr);
        return VERR_INVALID_PARAMETER;
    }

    int rc = nemuCmdVbvaSubmit(pHGSMICtx, offCmd);
    if (RT_SUCCESS(rc))
    {
        rc = NemuSHGSMICommandDoneSynch(&pHGSMICtx->heapCtx, pHdr);
        if (RT_SUCCESS(rc))
        {
            rc = pCtl->i32Result;
            if (!RT_SUCCESS(rc))
                WARN(("pCtl->i32Result %d", pCtl->i32Result));

            return rc;
        }
        else
            WARN(("NemuSHGSMICommandDoneSynch returnd %d", rc));
    }
    else
        WARN(("nemuCmdVbvaSubmit returnd %d", rc));

    NemuSHGSMICommandCancelSynch(&pHGSMICtx->heapCtx, pHdr);

    return rc;
}

static int nemuCmdVbvaCtlSubmitAsync(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, NEMUCMDVBVA_CTL * pCtl, FNNEMUSHGSMICMDCOMPLETION pfnCompletion, void *pvCompletion)
{
    const NEMUSHGSMIHEADER* pHdr = NemuSHGSMICommandPrepAsynch(&pHGSMICtx->heapCtx, pCtl, pfnCompletion, pvCompletion, NEMUSHGSMI_FLAG_GH_ASYNCH_IRQ);
    HGSMIOFFSET offCmd = NemuSHGSMICommandOffset(&pHGSMICtx->heapCtx, pHdr);
    if (offCmd == HGSMIOFFSET_VOID)
    {
        WARN(("NemuSHGSMICommandOffset returnd NULL"));
        NemuSHGSMICommandCancelAsynch(&pHGSMICtx->heapCtx, pHdr);
        return VERR_INVALID_PARAMETER;
    }

    int rc = nemuCmdVbvaSubmit(pHGSMICtx, offCmd);
    if (RT_SUCCESS(rc))
    {
        NemuSHGSMICommandDoneAsynch(&pHGSMICtx->heapCtx, pHdr);
        return rc;
    }
    else
        WARN(("nemuCmdVbvaSubmit returnd %d", rc));

    NemuSHGSMICommandCancelAsynch(&pHGSMICtx->heapCtx, pHdr);

    return rc;
}

static int nemuVBVAExCtlSubmitEnableDisable(PVBVAEXBUFFERCONTEXT pCtx, PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, bool fEnable)
{
    NEMUCMDVBVA_CTL_ENABLE *pCtl = (NEMUCMDVBVA_CTL_ENABLE*)nemuCmdVbvaCtlCreate(pHGSMICtx, sizeof (*pCtl));
    if (!pCtl)
    {
        WARN(("nemuCmdVbvaCtlCreate failed"));
        return VERR_NO_MEMORY;
    }

    pCtl->Hdr.u32Type = NEMUCMDVBVACTL_TYPE_ENABLE;
    pCtl->Hdr.i32Result = VERR_NOT_IMPLEMENTED;
    memset(&pCtl->Enable, 0, sizeof (pCtl->Enable));
    pCtl->Enable.u32Flags  = fEnable? VBVA_F_ENABLE: VBVA_F_DISABLE;
    pCtl->Enable.u32Offset = pCtx->offVRAMBuffer;
    pCtl->Enable.i32Result = VERR_NOT_SUPPORTED;
    pCtl->Enable.u32Flags |= VBVA_F_ABSOFFSET;

    int rc = nemuCmdVbvaCtlSubmitSync(pHGSMICtx, &pCtl->Hdr);
    if (RT_SUCCESS(rc))
    {
        rc = pCtl->Hdr.i32Result;
        if (!RT_SUCCESS(rc))
            WARN(("nemuCmdVbvaCtlSubmitSync Disable failed %d", rc));
    }
    else
        WARN(("nemuCmdVbvaCtlSubmitSync returnd %d", rc));

    nemuCmdVbvaCtlFree(pHGSMICtx, &pCtl->Hdr);

    return rc;
}

/*
 * Public hardware buffer methods.
 */
RTDECL(int) NemuVBVAExEnable(PVBVAEXBUFFERCONTEXT pCtx,
                            PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                            VBVABUFFER *pVBVA)
{
    int rc = VERR_GENERAL_FAILURE;

    LogFlowFunc(("pVBVA %p\n", pVBVA));

#if 0  /* All callers check this */
    if (ppdev->bHGSMISupported)
#endif
    {
        LogFunc(("pVBVA %p vbva off 0x%x\n", pVBVA, pCtx->offVRAMBuffer));

        pVBVA->hostFlags.u32HostEvents      = 0;
        pVBVA->hostFlags.u32SupportedOrders = 0;
        pVBVA->off32Data          = 0;
        pVBVA->off32Free          = 0;
        memset(pVBVA->aRecords, 0, sizeof (pVBVA->aRecords));
        pVBVA->indexRecordFirst   = 0;
        pVBVA->indexRecordFree    = 0;
        pVBVA->cbPartialWriteThreshold = 256;
        pVBVA->cbData             = pCtx->cbBuffer - sizeof (VBVABUFFER) + sizeof (pVBVA->au8Data);

        pCtx->fHwBufferOverflow = false;
        pCtx->pRecord    = NULL;
        pCtx->pVBVA      = pVBVA;

        rc = nemuVBVAExCtlSubmitEnableDisable(pCtx, pHGSMICtx, true);
    }

    if (!RT_SUCCESS(rc))
    {
        WARN(("enable failed %d", rc));
        NemuVBVAExDisable(pCtx, pHGSMICtx);
    }

    return rc;
}

RTDECL(void) NemuVBVAExDisable(PVBVAEXBUFFERCONTEXT pCtx,
                             PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx)
{
    LogFlowFunc(("\n"));

    nemuVBVAExCtlSubmitEnableDisable(pCtx, pHGSMICtx, false);

    pCtx->fHwBufferOverflow = false;
    pCtx->pRecord           = NULL;
    pCtx->pVBVA             = NULL;

    return;
}

RTDECL(bool) NemuVBVAExBufferBeginUpdate(PVBVAEXBUFFERCONTEXT pCtx,
                                       PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx)
{
    bool bRc = false;

    // LogFunc(("flags = 0x%08X\n", pCtx->pVBVA? pCtx->pVBVA->u32HostEvents: -1));

    Assert(pCtx->pVBVA);
    /* we do not use u32HostEvents & VBVA_F_MODE_ENABLED,
     * VBVA stays enabled once ENABLE call succeeds, until it is disabled with DISABLED call */
//    if (   pCtx->pVBVA
//        && (pCtx->pVBVA->hostFlags.u32HostEvents & VBVA_F_MODE_ENABLED))
    {
        uint32_t indexRecordNext;

        Assert(!pCtx->fHwBufferOverflow);
        Assert(pCtx->pRecord == NULL);

        indexRecordNext = (pCtx->pVBVA->indexRecordFree + 1) % VBVA_MAX_RECORDS;

        if (indexRecordNext == pCtx->indexRecordFirstUncompleted)
        {
            /* All slots in the records queue are used. */
            nemuVBVAExFlush (pCtx, pHGSMICtx);
        }

        if (indexRecordNext == pCtx->indexRecordFirstUncompleted)
        {
            /* Even after flush there is no place. Fail the request. */
            LogFunc(("no space in the queue of records!!! first %d, last %d\n",
                    indexRecordNext, pCtx->pVBVA->indexRecordFree));
        }
        else
        {
            /* Initialize the record. */
            VBVARECORD *pRecord = &pCtx->pVBVA->aRecords[pCtx->pVBVA->indexRecordFree];

            pRecord->cbRecord = VBVA_F_RECORD_PARTIAL;

            pCtx->pVBVA->indexRecordFree = indexRecordNext;

            // LogFunc(("indexRecordNext = %d\n", indexRecordNext));

            /* Remember which record we are using. */
            pCtx->pRecord = pRecord;

            bRc = true;
        }
    }

    return bRc;
}

RTDECL(void) NemuVBVAExBufferEndUpdate(PVBVAEXBUFFERCONTEXT pCtx)
{
    VBVARECORD *pRecord;

    // LogFunc(("\n"));

    Assert(pCtx->pVBVA);

    pRecord = pCtx->pRecord;
    Assert(pRecord && (pRecord->cbRecord & VBVA_F_RECORD_PARTIAL));

    /* Mark the record completed. */
    pRecord->cbRecord &= ~VBVA_F_RECORD_PARTIAL;

    pCtx->fHwBufferOverflow = false;
    pCtx->pRecord = NULL;

    return;
}

DECLINLINE(bool) nemuVBVAExIsEntryInRange(uint32_t u32First, uint32_t u32Entry, uint32_t u32Free)
{
    return (     u32First != u32Free
             && (
                     (u32First < u32Free && u32Entry >= u32First && u32Entry < u32Free)
                  || (u32First > u32Free && (u32Entry >= u32First || u32Entry < u32Free))
                 )
           );
}

DECLINLINE(bool) nemuVBVAExIsEntryInRangeOrEmpty(uint32_t u32First, uint32_t u32Entry, uint32_t u32Free)
{
    return nemuVBVAExIsEntryInRange(u32First, u32Entry, u32Free)
            || (    u32First == u32Entry
                 && u32Entry == u32Free);
}
#ifdef DEBUG

DECLINLINE(void) nemuHwBufferVerifyCompleted(PVBVAEXBUFFERCONTEXT pCtx)
{
    VBVABUFFER *pVBVA = pCtx->pVBVA;
    if (!nemuVBVAExIsEntryInRangeOrEmpty(pCtx->indexRecordFirstUncompleted, pVBVA->indexRecordFirst, pVBVA->indexRecordFree))
    {
        WARN(("invalid record set"));
    }

    if (!nemuVBVAExIsEntryInRangeOrEmpty(pCtx->off32DataUncompleted, pVBVA->off32Data, pVBVA->off32Free))
    {
        WARN(("invalid data set"));
    }
}
#endif

/*
 * Private operations.
 */
static uint32_t nemuHwBufferAvail(PVBVAEXBUFFERCONTEXT pCtx, const VBVABUFFER *pVBVA)
{
    int32_t i32Diff = pCtx->off32DataUncompleted - pVBVA->off32Free;

    return i32Diff > 0? i32Diff: pVBVA->cbData + i32Diff;
}

static uint32_t nemuHwBufferContiguousAvail(PVBVAEXBUFFERCONTEXT pCtx, const VBVABUFFER *pVBVA)
{
    int32_t i32Diff = pCtx->off32DataUncompleted - pVBVA->off32Free;

    return i32Diff > 0 ? i32Diff: pVBVA->cbData - pVBVA->off32Free;
}

static void nemuHwBufferPlaceDataAt(PVBVAEXBUFFERCONTEXT pCtx, const void *p,
                                    uint32_t cb, uint32_t offset)
{
    VBVABUFFER *pVBVA = pCtx->pVBVA;
    uint32_t u32BytesTillBoundary = pVBVA->cbData - offset;
    uint8_t  *dst                 = &pVBVA->au8Data[offset];
    int32_t i32Diff               = cb - u32BytesTillBoundary;

    if (i32Diff <= 0)
    {
        /* Chunk will not cross buffer boundary. */
        memcpy (dst, p, cb);
    }
    else
    {
        /* Chunk crosses buffer boundary. */
        memcpy (dst, p, u32BytesTillBoundary);
        memcpy (&pVBVA->au8Data[0], (uint8_t *)p + u32BytesTillBoundary, i32Diff);
    }

    return;
}

static bool nemuHwBufferWrite(PVBVAEXBUFFERCONTEXT pCtx,
                              PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                              const void *p, uint32_t cb)
{
    VBVARECORD *pRecord;
    uint32_t cbHwBufferAvail;

    uint32_t cbWritten = 0;

    VBVABUFFER *pVBVA = pCtx->pVBVA;
    Assert(pVBVA);

    if (!pVBVA || pCtx->fHwBufferOverflow)
    {
        return false;
    }

    Assert(pVBVA->indexRecordFirst != pVBVA->indexRecordFree);
    Assert(pCtx->indexRecordFirstUncompleted != pVBVA->indexRecordFree);

    pRecord = pCtx->pRecord;
    Assert(pRecord && (pRecord->cbRecord & VBVA_F_RECORD_PARTIAL));

    // LogFunc(("%d\n", cb));

    cbHwBufferAvail = nemuHwBufferAvail(pCtx, pVBVA);

    while (cb > 0)
    {
        uint32_t cbChunk = cb;

        // LogFunc(("pVBVA->off32Free %d, pRecord->cbRecord 0x%08X, cbHwBufferAvail %d, cb %d, cbWritten %d\n",
        //             pVBVA->off32Free, pRecord->cbRecord, cbHwBufferAvail, cb, cbWritten));

        if (cbChunk >= cbHwBufferAvail)
        {
            LogFunc(("1) avail %d, chunk %d\n", cbHwBufferAvail, cbChunk));

            nemuVBVAExFlush(pCtx, pHGSMICtx);

            cbHwBufferAvail = nemuHwBufferAvail(pCtx, pVBVA);

            if (cbChunk >= cbHwBufferAvail)
            {
                WARN(("no place for %d bytes. Only %d bytes available after flush. Going to partial writes.\n",
                            cb, cbHwBufferAvail));

                if (cbHwBufferAvail <= pVBVA->cbPartialWriteThreshold)
                {
                    WARN(("Buffer overflow!!!\n"));
                    pCtx->fHwBufferOverflow = true;
                    Assert(false);
                    return false;
                }

                cbChunk = cbHwBufferAvail - pVBVA->cbPartialWriteThreshold;
            }
        }

        Assert(cbChunk <= cb);
        Assert(cbChunk <= nemuHwBufferAvail(pCtx, pVBVA));

        nemuHwBufferPlaceDataAt (pCtx, (uint8_t *)p + cbWritten, cbChunk, pVBVA->off32Free);

        pVBVA->off32Free   = (pVBVA->off32Free + cbChunk) % pVBVA->cbData;
        pRecord->cbRecord += cbChunk;
        cbHwBufferAvail -= cbChunk;

        cb        -= cbChunk;
        cbWritten += cbChunk;
    }

    return true;
}

/*
 * Public writer to the hardware buffer.
 */
RTDECL(uint32_t) NemuVBVAExGetFreeTail(PVBVAEXBUFFERCONTEXT pCtx)
{
    VBVABUFFER *pVBVA = pCtx->pVBVA;
    if (pVBVA->off32Data <= pVBVA->off32Free)
        return pVBVA->cbData - pVBVA->off32Free;
    return 0;
}

RTDECL(void*) NemuVBVAExAllocContiguous(PVBVAEXBUFFERCONTEXT pCtx, PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, uint32_t cb)
{
    VBVARECORD *pRecord;
    uint32_t cbHwBufferContiguousAvail;
    uint32_t offset;

    VBVABUFFER *pVBVA = pCtx->pVBVA;
    Assert(pVBVA);

    if (!pVBVA || pCtx->fHwBufferOverflow)
    {
        return NULL;
    }

    Assert(pVBVA->indexRecordFirst != pVBVA->indexRecordFree);
    Assert(pCtx->indexRecordFirstUncompleted != pVBVA->indexRecordFree);

    pRecord = pCtx->pRecord;
    Assert(pRecord && (pRecord->cbRecord & VBVA_F_RECORD_PARTIAL));

    // LogFunc(("%d\n", cb));

    if (pVBVA->cbData < cb)
    {
        WARN(("requested to allocate buffer of size %d bigger than the VBVA ring buffer size %d", cb, pVBVA->cbData));
        return NULL;
    }

    cbHwBufferContiguousAvail = nemuHwBufferContiguousAvail(pCtx, pVBVA);

    if (cbHwBufferContiguousAvail < cb)
    {
        if (cb > pVBVA->cbData - pVBVA->off32Free)
        {
            /* the entire contiguous part is smaller than the requested buffer */
            return NULL;
        }

        nemuVBVAExFlush(pCtx, pHGSMICtx);

        cbHwBufferContiguousAvail = nemuHwBufferContiguousAvail(pCtx, pVBVA);
        if (cbHwBufferContiguousAvail < cb)
        {
            /* this is really bad - the host did not clean up buffer even after we requested it to flush */
            WARN(("Host did not clean up the buffer!"));
            return NULL;
        }
    }

    offset = pVBVA->off32Free;

    pVBVA->off32Free = (pVBVA->off32Free + cb) % pVBVA->cbData;
    pRecord->cbRecord += cb;

    return &pVBVA->au8Data[offset];
}

RTDECL(bool) NemuVBVAExIsProcessing(PVBVAEXBUFFERCONTEXT pCtx)
{
    uint32_t u32HostEvents = pCtx->pVBVA->hostFlags.u32HostEvents;
    return !!(u32HostEvents & VBVA_F_STATE_PROCESSING);
}

RTDECL(void) NemuVBVAExCBufferCompleted(PVBVAEXBUFFERCONTEXT pCtx)
{
    VBVABUFFER *pVBVA = pCtx->pVBVA;
    uint32_t cbBuffer = pVBVA->aRecords[pCtx->indexRecordFirstUncompleted].cbRecord;
    pCtx->indexRecordFirstUncompleted = (pCtx->indexRecordFirstUncompleted + 1) % VBVA_MAX_RECORDS;
    pCtx->off32DataUncompleted = (pCtx->off32DataUncompleted + cbBuffer) % pVBVA->cbData;
}

RTDECL(bool) NemuVBVAExWrite(PVBVAEXBUFFERCONTEXT pCtx,
                           PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                           const void *pv, uint32_t cb)
{
    return nemuHwBufferWrite(pCtx, pHGSMICtx, pv, cb);
}

RTDECL(bool) NemuVBVAExOrderSupported(PVBVAEXBUFFERCONTEXT pCtx, unsigned code)
{
    VBVABUFFER *pVBVA = pCtx->pVBVA;

    if (!pVBVA)
    {
        return false;
    }

    if (pVBVA->hostFlags.u32SupportedOrders & (1 << code))
    {
        return true;
    }

    return false;
}

RTDECL(void) NemuVBVAExSetupBufferContext(PVBVAEXBUFFERCONTEXT pCtx,
                                        uint32_t offVRAMBuffer,
                                        uint32_t cbBuffer,
                                        PFNVBVAEXBUFFERFLUSH pfnFlush,
                                        void *pvFlush)
{
    memset(pCtx, 0, RT_OFFSETOF(VBVAEXBUFFERCONTEXT, pVBVA));
    pCtx->offVRAMBuffer = offVRAMBuffer;
    pCtx->cbBuffer      = cbBuffer;
    pCtx->pfnFlush = pfnFlush;
    pCtx->pvFlush = pvFlush;
}

static void* nemuVBVAExIterCur(PVBVAEXBUFFERITERBASE pIter, struct VBVABUFFER *pVBVA, uint32_t *pcbBuffer, bool *pfProcessed)
{
    uint32_t cbRecord = pVBVA->aRecords[pIter->iCurRecord].cbRecord;
    if (cbRecord == VBVA_F_RECORD_PARTIAL)
        return NULL;
    if (pcbBuffer)
        *pcbBuffer = cbRecord;
    if (pfProcessed)
        *pfProcessed = !nemuVBVAExIsEntryInRange(pVBVA->indexRecordFirst, pIter->iCurRecord, pVBVA->indexRecordFree);
    return &pVBVA->au8Data[pIter->off32CurCmd];
}

DECLINLINE(uint32_t) nemuVBVAExSubst(uint32_t x, uint32_t val, uint32_t maxVal)
{
    int32_t result = (int32_t)(x - val);
    return result >= 0 ? (uint32_t)result : maxVal - (((uint32_t)(-result)) % maxVal);
}

RTDECL(void) NemuVBVAExBIterInit(PVBVAEXBUFFERCONTEXT pCtx, PVBVAEXBUFFERBACKWARDITER pIter)
{
    struct VBVABUFFER *pVBVA = pCtx->pVBVA;
    pIter->Base.pCtx = pCtx;
    uint32_t iCurRecord = nemuVBVAExSubst(pVBVA->indexRecordFree, 1, VBVA_MAX_RECORDS);
    if (nemuVBVAExIsEntryInRange(pCtx->indexRecordFirstUncompleted, iCurRecord, pVBVA->indexRecordFree))
    {
        /* even if the command gets completed by the time we're doing the pCtx->pVBVA->aRecords[iCurRecord].cbRecord below,
         * the pCtx->pVBVA->aRecords[iCurRecord].cbRecord will still be valid, as it can only be modified by a submitter,
         * and we are in a submitter context now */
        pIter->Base.iCurRecord = iCurRecord;
        pIter->Base.off32CurCmd = nemuVBVAExSubst(pVBVA->off32Free, pCtx->pVBVA->aRecords[iCurRecord].cbRecord, pVBVA->cbData);
    }
    else
    {
        /* no data */
        pIter->Base.iCurRecord = pVBVA->indexRecordFree;
        pIter->Base.off32CurCmd = pVBVA->off32Free;
    }
}

RTDECL(void*) NemuVBVAExBIterNext(PVBVAEXBUFFERBACKWARDITER pIter, uint32_t *pcbBuffer, bool *pfProcessed)
{
    PVBVAEXBUFFERCONTEXT pCtx = pIter->Base.pCtx;
    struct VBVABUFFER *pVBVA = pCtx->pVBVA;
    uint32_t indexRecordFirstUncompleted = pCtx->indexRecordFirstUncompleted;
    if (!nemuVBVAExIsEntryInRange(indexRecordFirstUncompleted, pIter->Base.iCurRecord, pVBVA->indexRecordFree))
        return NULL;

    void *pvBuffer = nemuVBVAExIterCur(&pIter->Base, pVBVA, pcbBuffer, pfProcessed);
    AssertRelease(pvBuffer);

    /* even if the command gets completed by the time we're doing the pCtx->pVBVA->aRecords[pIter->Base.iCurRecord].cbRecord below,
     * the pCtx->pVBVA->aRecords[pIter->Base.iCurRecord].cbRecord will still be valid, as it can only be modified by a submitter,
     * and we are in a submitter context now */
    pIter->Base.iCurRecord = nemuVBVAExSubst(pIter->Base.iCurRecord, 1, VBVA_MAX_RECORDS);
    pIter->Base.off32CurCmd = nemuVBVAExSubst(pIter->Base.off32CurCmd, pCtx->pVBVA->aRecords[pIter->Base.iCurRecord].cbRecord, pVBVA->cbData);

    return pvBuffer;
}

RTDECL(void) NemuVBVAExCFIterInit(PVBVAEXBUFFERCONTEXT pCtx, PVBVAEXBUFFERFORWARDITER pIter)
{
    pIter->Base.pCtx = pCtx;
    pIter->Base.iCurRecord = pCtx->indexRecordFirstUncompleted;
    pIter->Base.off32CurCmd = pCtx->off32DataUncompleted;
}

RTDECL(void*) NemuVBVAExCFIterNext(PVBVAEXBUFFERFORWARDITER pIter, uint32_t *pcbBuffer, bool *pfProcessed)
{
    PVBVAEXBUFFERCONTEXT pCtx = pIter->Base.pCtx;
    struct VBVABUFFER *pVBVA = pCtx->pVBVA;
    uint32_t indexRecordFree = pVBVA->indexRecordFree;
    if (!nemuVBVAExIsEntryInRange(pCtx->indexRecordFirstUncompleted, pIter->Base.iCurRecord, indexRecordFree))
        return NULL;

    uint32_t cbBuffer;
    void *pvData = nemuVBVAExIterCur(&pIter->Base, pVBVA, &cbBuffer, pfProcessed);
    if (!pvData)
        return NULL;

    pIter->Base.iCurRecord = (pIter->Base.iCurRecord + 1) % VBVA_MAX_RECORDS;
    pIter->Base.off32CurCmd = (pIter->Base.off32CurCmd + cbBuffer) % pVBVA->cbData;

    if (pcbBuffer)
        *pcbBuffer = cbBuffer;

    return pvData;
}

/**/

int NemuCmdVbvaEnable(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva)
{
    return NemuVBVAExEnable(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx, pVbva->Vbva.pVBVA);
}

int NemuCmdVbvaDisable(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva)
{
    NemuVBVAExDisable(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx);
    return VINF_SUCCESS;
}

int NemuCmdVbvaDestroy(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva)
{
    int rc = VINF_SUCCESS;
    NemuMPCmnUnmapAdapterMemory(NemuCommonFromDeviceExt(pDevExt), (void**)&pVbva->Vbva.pVBVA);
    memset(pVbva, 0, sizeof (*pVbva));
    return rc;
}

static void nemuCmdVbvaDdiNotifyCompleteIrq(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, UINT u32FenceId, DXGK_INTERRUPT_TYPE enmComplType)
{
    DXGKARGCB_NOTIFY_INTERRUPT_DATA notify;
    memset(&notify, 0, sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA));
    switch (enmComplType)
    {
        case DXGK_INTERRUPT_DMA_COMPLETED:
            notify.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
            notify.DmaCompleted.SubmissionFenceId = u32FenceId;
            notify.DmaCompleted.NodeOrdinal = pVbva->idNode;
            break;

        case DXGK_INTERRUPT_DMA_PREEMPTED:
            notify.InterruptType = DXGK_INTERRUPT_DMA_PREEMPTED;
            notify.DmaPreempted.PreemptionFenceId = u32FenceId;
            notify.DmaPreempted.NodeOrdinal = pVbva->idNode;
            notify.DmaPreempted.LastCompletedFenceId = pVbva->u32FenceCompleted;
            break;

        case DXGK_INTERRUPT_DMA_FAULTED:
            Assert(0);
            notify.InterruptType = DXGK_INTERRUPT_DMA_FAULTED;
            notify.DmaFaulted.FaultedFenceId = u32FenceId;
            notify.DmaFaulted.Status = STATUS_UNSUCCESSFUL; /* @todo: better status ? */
            notify.DmaFaulted.NodeOrdinal = pVbva->idNode;
            break;

        default:
            WARN(("unrecognized completion type %d", enmComplType));
            break;
    }

    pDevExt->u.primary.DxgkInterface.DxgkCbNotifyInterrupt(pDevExt->u.primary.DxgkInterface.DeviceHandle, &notify);
}

typedef struct NEMUCMDVBVA_NOTIFYPREEMPT_CB
{
    PNEMUMP_DEVEXT pDevExt;
    NEMUCMDVBVA *pVbva;
    int rc;
    UINT u32SubmitFenceId;
    UINT u32PreemptFenceId;
} NEMUCMDVBVA_NOTIFYPREEMPT_CB;

static BOOLEAN nemuCmdVbvaDdiNotifyPreemptCb(PVOID pvContext)
{
    NEMUCMDVBVA_NOTIFYPREEMPT_CB* pData = (NEMUCMDVBVA_NOTIFYPREEMPT_CB*)pvContext;
    PNEMUMP_DEVEXT pDevExt = pData->pDevExt;
    NEMUCMDVBVA *pVbva = pData->pVbva;
    Assert(pVbva->u32FenceProcessed >= pVbva->u32FenceCompleted);
    if (!pData->u32SubmitFenceId || pVbva->u32FenceProcessed == pData->u32SubmitFenceId)
    {
        nemuCmdVbvaDdiNotifyCompleteIrq(pDevExt, pVbva, pData->u32PreemptFenceId, DXGK_INTERRUPT_DMA_PREEMPTED);

        pDevExt->u.primary.DxgkInterface.DxgkCbQueueDpc(pDevExt->u.primary.DxgkInterface.DeviceHandle);
    }
    else
    {
        Assert(pVbva->u32FenceProcessed < pData->u32SubmitFenceId);
        Assert(pVbva->cPreempt <= NEMUCMDVBVA_PREEMPT_EL_SIZE);
        if (pVbva->cPreempt == NEMUCMDVBVA_PREEMPT_EL_SIZE)
        {
            WARN(("no more free elements in preempt map"));
            pData->rc = VERR_BUFFER_OVERFLOW;
            return FALSE;
        }
        uint32_t iNewEl = (pVbva->iCurPreempt + pVbva->cPreempt) % NEMUCMDVBVA_PREEMPT_EL_SIZE;
        Assert(iNewEl < NEMUCMDVBVA_PREEMPT_EL_SIZE);
        pVbva->aPreempt[iNewEl].u32SubmitFence = pData->u32SubmitFenceId;
        pVbva->aPreempt[iNewEl].u32PreemptFence = pData->u32PreemptFenceId;
        ++pVbva->cPreempt;
    }

    pData->rc = VINF_SUCCESS;
    return TRUE;
}

static int nemuCmdVbvaDdiNotifyPreempt(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, UINT u32SubmitFenceId, UINT u32PreemptFenceId)
{
    NEMUCMDVBVA_NOTIFYPREEMPT_CB Data;
    Data.pDevExt = pDevExt;
    Data.pVbva = pVbva;
    Data.rc = VERR_INTERNAL_ERROR;
    Data.u32SubmitFenceId = u32SubmitFenceId;
    Data.u32PreemptFenceId = u32PreemptFenceId;
    BOOLEAN bDummy;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
            pDevExt->u.primary.DxgkInterface.DeviceHandle,
            nemuCmdVbvaDdiNotifyPreemptCb,
            &Data,
            0, /* IN ULONG MessageNumber */
            &bDummy);
    if (!NT_SUCCESS(Status))
    {
        WARN(("DxgkCbSynchronizeExecution failed Status %#x", Status));
        return VERR_GENERAL_FAILURE;
    }

    if (!RT_SUCCESS(Data.rc))
    {
        WARN(("nemuCmdVbvaDdiNotifyPreemptCb failed rc %d", Data.rc));
        return Data.rc;
    }

    return VINF_SUCCESS;
}

static int nemuCmdVbvaFlush(PNEMUMP_DEVEXT pDevExt, HGSMIGUESTCOMMANDCONTEXT *pCtx, bool fBufferOverflow)
{
    /* Issue the flush command. */
    VBVACMDVBVAFLUSH *pFlush = (VBVACMDVBVAFLUSH*)NemuHGSMIBufferAlloc(pCtx,
                                   sizeof (VBVACMDVBVAFLUSH),
                                   HGSMI_CH_VBVA,
                                   VBVA_CMDVBVA_FLUSH);
    if (!pFlush)
    {
        WARN(("NemuHGSMIBufferAlloc failed\n"));
        return VERR_OUT_OF_RESOURCES;
    }

    pFlush->u32Flags = fBufferOverflow ?  VBVACMDVBVAFLUSH_F_GUEST_BUFFER_OVERFLOW : 0;

    NemuHGSMIBufferSubmit(pCtx, pFlush);

    NemuHGSMIBufferFree(pCtx, pFlush);

    return VINF_SUCCESS;
}

typedef struct NEMUCMDVBVA_CHECK_COMPLETED_CB
{
    PNEMUMP_DEVEXT pDevExt;
    NEMUCMDVBVA *pVbva;
    /* last completted fence id */
    uint32_t u32FenceCompleted;
    /* last submitted fence id */
    uint32_t u32FenceSubmitted;
    /* last processed fence id (i.e. either completed or cancelled) */
    uint32_t u32FenceProcessed;
} NEMUCMDVBVA_CHECK_COMPLETED_CB;

static BOOLEAN nemuCmdVbvaCheckCompletedIrqCb(PVOID pContext)
{
    NEMUCMDVBVA_CHECK_COMPLETED_CB *pCompleted = (NEMUCMDVBVA_CHECK_COMPLETED_CB*)pContext;
    BOOLEAN bRc = DxgkDdiInterruptRoutineNew(pCompleted->pDevExt, 0);
    if (pCompleted->pVbva)
    {
        pCompleted->u32FenceCompleted = pCompleted->pVbva->u32FenceCompleted;
        pCompleted->u32FenceSubmitted = pCompleted->pVbva->u32FenceSubmitted;
        pCompleted->u32FenceProcessed = pCompleted->pVbva->u32FenceProcessed;
    }
    else
    {
        WARN(("no vbva"));
        pCompleted->u32FenceCompleted = 0;
        pCompleted->u32FenceSubmitted = 0;
        pCompleted->u32FenceProcessed = 0;
    }
    return bRc;
}


static uint32_t nemuCmdVbvaCheckCompleted(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, bool fPingHost, HGSMIGUESTCOMMANDCONTEXT *pCtx, bool fBufferOverflow, uint32_t *pu32FenceSubmitted, uint32_t *pu32FenceProcessed)
{
    if (fPingHost)
        nemuCmdVbvaFlush(pDevExt, pCtx, fBufferOverflow);

    NEMUCMDVBVA_CHECK_COMPLETED_CB context;
    context.pDevExt = pDevExt;
    context.pVbva = pVbva;
    context.u32FenceCompleted = 0;
    context.u32FenceSubmitted = 0;
    context.u32FenceProcessed = 0;
    BOOLEAN bRet;
    NTSTATUS Status = pDevExt->u.primary.DxgkInterface.DxgkCbSynchronizeExecution(
                            pDevExt->u.primary.DxgkInterface.DeviceHandle,
                            nemuCmdVbvaCheckCompletedIrqCb,
                            &context,
                            0, /* IN ULONG MessageNumber */
                            &bRet);
    Assert(Status == STATUS_SUCCESS);

    if (pu32FenceSubmitted)
        *pu32FenceSubmitted = context.u32FenceSubmitted;

    if (pu32FenceProcessed)
        *pu32FenceProcessed = context.u32FenceProcessed;

    return context.u32FenceCompleted;
}

static DECLCALLBACK(void) voxCmdVbvaFlushCb(struct VBVAEXBUFFERCONTEXT *pCtx, PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, void *pvFlush)
{
    PNEMUMP_DEVEXT pDevExt = (PNEMUMP_DEVEXT)pvFlush;

    nemuCmdVbvaCheckCompleted(pDevExt, NULL,  true /*fPingHost*/, pHGSMICtx, true /*fBufferOverflow*/, NULL, NULL);
}

int NemuCmdVbvaCreate(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, ULONG offBuffer, ULONG cbBuffer)
{
    memset(pVbva, 0, sizeof (*pVbva));

    int rc = NemuMPCmnMapAdapterMemory(NemuCommonFromDeviceExt(pDevExt),
                                       (void**)&pVbva->Vbva.pVBVA,
                                       offBuffer,
                                       cbBuffer);
    if (RT_SUCCESS(rc))
    {
        Assert(pVbva->Vbva.pVBVA);
        NemuVBVAExSetupBufferContext(&pVbva->Vbva, offBuffer, cbBuffer, voxCmdVbvaFlushCb, pDevExt);
    }
    else
    {
        WARN(("NemuMPCmnMapAdapterMemory failed rc %d", rc));
    }

    return rc;
}

void NemuCmdVbvaSubmitUnlock(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, NEMUCMDVBVA_HDR* pCmd, uint32_t u32FenceID)
{
    if (u32FenceID)
        pVbva->u32FenceSubmitted = u32FenceID;
    else
        WARN(("no cmd fence specified"));

    pCmd->u8State = NEMUCMDVBVA_STATE_SUBMITTED;

    pCmd->u2.u32FenceID = u32FenceID;

    NemuVBVAExBufferEndUpdate(&pVbva->Vbva);

    if (!NemuVBVAExIsProcessing(&pVbva->Vbva))
    {
        /* Issue the submit command. */
        HGSMIGUESTCOMMANDCONTEXT *pCtx = &NemuCommonFromDeviceExt(pDevExt)->guestCtx;
        VBVACMDVBVASUBMIT *pSubmit = (VBVACMDVBVASUBMIT*)NemuHGSMIBufferAlloc(pCtx,
                                       sizeof (VBVACMDVBVASUBMIT),
                                       HGSMI_CH_VBVA,
                                       VBVA_CMDVBVA_SUBMIT);
        if (!pSubmit)
        {
            WARN(("NemuHGSMIBufferAlloc failed\n"));
            return;
        }

        pSubmit->u32Reserved = 0;

        NemuHGSMIBufferSubmit(pCtx, pSubmit);

        NemuHGSMIBufferFree(pCtx, pSubmit);
    }
}

NEMUCMDVBVA_HDR* NemuCmdVbvaSubmitLock(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, uint32_t cbCmd)
{
    if (NemuVBVAExGetSize(&pVbva->Vbva) < cbCmd)
    {
        WARN(("buffer does not fit the vbva buffer, we do not support splitting buffers"));
        return NULL;
    }

    if (!NemuVBVAExBufferBeginUpdate(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx))
    {
        WARN(("NemuVBVAExBufferBeginUpdate failed!"));
        return NULL;
    }

    void* pvBuffer = NemuVBVAExAllocContiguous(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx, cbCmd);
    if (!pvBuffer)
    {
        LOG(("failed to allocate contiguous buffer %d bytes, trying nopping the tail", cbCmd));
        uint32_t cbTail = NemuVBVAExGetFreeTail(&pVbva->Vbva);
        if (!cbTail)
        {
            WARN(("this is not a free tail case, cbTail is NULL"));
            return NULL;
        }

        Assert(cbTail < cbCmd);

        pvBuffer = NemuVBVAExAllocContiguous(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx, cbTail);

        Assert(pvBuffer);

        *((uint8_t*)pvBuffer) = NEMUCMDVBVA_OPTYPE_NOP;

        NemuVBVAExBufferEndUpdate(&pVbva->Vbva);

        if (!NemuVBVAExBufferBeginUpdate(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx))
        {
            WARN(("NemuVBVAExBufferBeginUpdate 2 failed!"));
            return NULL;
        }

        pvBuffer = NemuVBVAExAllocContiguous(&pVbva->Vbva, &NemuCommonFromDeviceExt(pDevExt)->guestCtx, cbCmd);
        if (!pvBuffer)
        {
            WARN(("failed to allocate contiguous buffer %d bytes", cbCmd));
            return NULL;
        }
    }

    Assert(pvBuffer);

    return (NEMUCMDVBVA_HDR*)pvBuffer;
}

int NemuCmdVbvaSubmit(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, struct NEMUCMDVBVA_HDR *pCmd, uint32_t u32FenceID, uint32_t cbCmd)
{
    NEMUCMDVBVA_HDR* pHdr = NemuCmdVbvaSubmitLock(pDevExt, pVbva, cbCmd);

    if (!pHdr)
    {
        WARN(("NemuCmdVbvaSubmitLock failed"));
        return VERR_GENERAL_FAILURE;
    }

    memcpy(pHdr, pCmd, cbCmd);

    NemuCmdVbvaSubmitUnlock(pDevExt, pVbva, pCmd, u32FenceID);

    return VINF_SUCCESS;
}

bool NemuCmdVbvaPreempt(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, uint32_t u32FenceID)
{
    VBVAEXBUFFERBACKWARDITER Iter;
    NemuVBVAExBIterInit(&pVbva->Vbva, &Iter);

    uint32_t cbBuffer;
    bool fProcessed;
    uint8_t* pu8Cmd;
    uint32_t u32SubmitFence = 0;

    /* we can do it right here */
    while ((pu8Cmd = (uint8_t*)NemuVBVAExBIterNext(&Iter, &cbBuffer, &fProcessed)) != NULL)
    {
        if (*pu8Cmd == NEMUCMDVBVA_OPTYPE_NOP)
            continue;

        NEMUCMDVBVA_HDR *pCmd = (NEMUCMDVBVA_HDR*)pu8Cmd;

        if (ASMAtomicCmpXchgU8(&pCmd->u8State, NEMUCMDVBVA_STATE_CANCELLED, NEMUCMDVBVA_STATE_SUBMITTED)
                || pCmd->u8State == NEMUCMDVBVA_STATE_CANCELLED)
            continue;

        Assert(pCmd->u8State == NEMUCMDVBVA_STATE_IN_PROGRESS);

        u32SubmitFence = pCmd->u2.u32FenceID;
        break;
    }

    nemuCmdVbvaDdiNotifyPreempt(pDevExt, pVbva, u32SubmitFence, u32FenceID);

    return false;
}

bool NemuCmdVbvaCheckCompletedIrq(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva)
{
    VBVAEXBUFFERFORWARDITER Iter;
    NemuVBVAExCFIterInit(&pVbva->Vbva, &Iter);

    bool fHasCommandsCompletedPreempted = false;
    bool fProcessed;
    uint8_t* pu8Cmd;


    while ((pu8Cmd = (uint8_t*)NemuVBVAExCFIterNext(&Iter, NULL, &fProcessed)) != NULL)
    {
        if (!fProcessed)
            break;

        if (*pu8Cmd == NEMUCMDVBVA_OPTYPE_NOP)
            continue;

        NEMUCMDVBVA_HDR *pCmd = (NEMUCMDVBVA_HDR*)pu8Cmd;
        uint8_t u8State = pCmd->u8State;
        uint32_t u32FenceID = pCmd->u2.u32FenceID;

        Assert(u8State == NEMUCMDVBVA_STATE_IN_PROGRESS
                || u8State == NEMUCMDVBVA_STATE_CANCELLED);
        Assert(u32FenceID);
        NemuVBVAExCBufferCompleted(&pVbva->Vbva);

        if (!u32FenceID)
        {
            WARN(("fence is NULL"));
            continue;
        }

        pVbva->u32FenceProcessed = u32FenceID;

        if (u8State == NEMUCMDVBVA_STATE_IN_PROGRESS)
            pVbva->u32FenceCompleted = u32FenceID;
        else
        {
            Assert(u8State == NEMUCMDVBVA_STATE_CANCELLED);
            continue;
        }

        Assert(u32FenceID);
        nemuCmdVbvaDdiNotifyCompleteIrq(pDevExt, pVbva, u32FenceID, DXGK_INTERRUPT_DMA_COMPLETED);

        if (pVbva->cPreempt && pVbva->aPreempt[pVbva->iCurPreempt].u32SubmitFence == u32FenceID)
        {
            Assert(pVbva->aPreempt[pVbva->iCurPreempt].u32PreemptFence);
            nemuCmdVbvaDdiNotifyCompleteIrq(pDevExt, pVbva, pVbva->aPreempt[pVbva->iCurPreempt].u32PreemptFence, DXGK_INTERRUPT_DMA_PREEMPTED);
            --pVbva->cPreempt;
            if (!pVbva->cPreempt)
                pVbva->iCurPreempt = 0;
            else
            {
                ++pVbva->iCurPreempt;
                pVbva->iCurPreempt %= NEMUCMDVBVA_PREEMPT_EL_SIZE;
            }
        }

        fHasCommandsCompletedPreempted = true;
    }

#ifdef DEBUG
    nemuHwBufferVerifyCompleted(&pVbva->Vbva);
#endif

    return fHasCommandsCompletedPreempted;
}

uint32_t NemuCmdVbvaCheckCompleted(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, bool fPingHost, uint32_t *pu32FenceSubmitted, uint32_t *pu32FenceProcessed)
{
    return nemuCmdVbvaCheckCompleted(pDevExt, pVbva, fPingHost, &NemuCommonFromDeviceExt(pDevExt)->guestCtx, false /* fBufferOverflow */, pu32FenceSubmitted, pu32FenceProcessed);
}

#if 0
static uint32_t nemuCVDdiSysMemElBuild(NEMUCMDVBVA_SYSMEMEL *pEl, PMDL pMdl, uint32_t iPfn, uint32_t cPages)
{
    PFN_NUMBER cur = MmGetMdlPfnArray(pMdl)[iPfn];
    uint32_t cbEl = sizeof (*pEl);
    uint32_t cStoredPages = 1;
    PFN_NUMBER next;
    pEl->iPage1 = (uint32_t)(cur & 0xfffff);
    pEl->iPage2 = (uint32_t)(cur >> 20);
    --cPages;
    for ( ; cPages && cStoredPages < NEMUCMDVBVA_SYSMEMEL_CPAGES_MAX; --cPages, ++cStoredPages, cur = next)
    {
        next = MmGetMdlPfnArray(pMdl)[iPfn+cStoredPages];
        if (next != cur+1)
            break;
    }

    Assert(cStoredPages);
    pEl->cPagesAfterFirst = cStoredPages - 1;

    return cPages;
}

uint32_t NemuCVDdiPTransferVRamSysBuildEls(NEMUCMDVBVA_PAGING_TRANSFER *pCmd, PMDL pMdl, uint32_t iPfn, uint32_t cPages, uint32_t cbBuffer, uint32_t *pcPagesWritten)
{
    uint32_t cInitPages = cPages;
    uint32_t cbInitBuffer = cbBuffer;
    uint32_t cEls = 0;
    NEMUCMDVBVA_SYSMEMEL *pEl = pCmd->aSysMem;

    Assert(cbBuffer >= sizeof (NEMUCMDVBVA_PAGING_TRANSFER));

    cbBuffer -= RT_OFFSETOF(NEMUCMDVBVA_PAGING_TRANSFER, aSysMem);

    for (; cPages && cbBuffer >= sizeof (NEMUCMDVBVA_SYSMEMEL); ++cEls, cbBuffer-=sizeof (NEMUCMDVBVA_SYSMEMEL), ++pEl)
    {
        cPages = nemuCVDdiSysMemElBuild(pEl, pMdl, iPfn + cInitPages - cPages, cPages);
    }

    *pcPagesWritten = cInitPages - cPages;
    return cbInitBuffer - cbBuffer;
}
#endif

uint32_t NemuCVDdiPTransferVRamSysBuildEls(NEMUCMDVBVA_PAGING_TRANSFER *pCmd, PMDL pMdl, uint32_t iPfn, uint32_t cPages, uint32_t cbBuffer, uint32_t *pcPagesWritten)
{
    uint32_t cbInitBuffer = cbBuffer;
    uint32_t i = 0;
    NEMUCMDVBVAPAGEIDX *pPageNumbers = pCmd->Data.aPageNumbers;

    cbBuffer -= RT_OFFSETOF(NEMUCMDVBVA_PAGING_TRANSFER, Data.aPageNumbers);

    for (; i < cPages && cbBuffer >= sizeof (*pPageNumbers); ++i, cbBuffer -= sizeof (*pPageNumbers))
    {
        pPageNumbers[i] = (NEMUCMDVBVAPAGEIDX)(MmGetMdlPfnArray(pMdl)[iPfn + i]);
    }

    *pcPagesWritten = i;
    Assert(cbInitBuffer - cbBuffer == RT_OFFSETOF(NEMUCMDVBVA_PAGING_TRANSFER, Data.aPageNumbers[i]));
    Assert(cbInitBuffer - cbBuffer >= sizeof (NEMUCMDVBVA_PAGING_TRANSFER));
    return cbInitBuffer - cbBuffer;
}


int nemuCmdVbvaConConnect(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
        uint32_t crVersionMajor, uint32_t crVersionMinor,
        uint32_t *pu32ClientID)
{
    NEMUCMDVBVA_CTL_3DCTL_CONNECT *pConnect = (NEMUCMDVBVA_CTL_3DCTL_CONNECT*)nemuCmdVbvaCtlCreate(pHGSMICtx, sizeof (NEMUCMDVBVA_CTL_3DCTL_CONNECT));
    if (!pConnect)
    {
        WARN(("nemuCmdVbvaCtlCreate failed"));
        return VERR_OUT_OF_RESOURCES;
    }
    pConnect->Hdr.u32Type = NEMUCMDVBVACTL_TYPE_3DCTL;
    pConnect->Hdr.i32Result = VERR_NOT_SUPPORTED;
    pConnect->Connect.Hdr.u32Type = NEMUCMDVBVA3DCTL_TYPE_CONNECT;
    pConnect->Connect.Hdr.u32CmdClientId = 0;
    pConnect->Connect.u32MajorVersion = crVersionMajor;
    pConnect->Connect.u32MinorVersion = crVersionMinor;
    pConnect->Connect.u64Pid = (uint64_t)PsGetCurrentProcessId();

    int rc = nemuCmdVbvaCtlSubmitSync(pHGSMICtx, &pConnect->Hdr);
    if (RT_SUCCESS(rc))
    {
        rc = pConnect->Hdr.i32Result;
        if (RT_SUCCESS(rc))
        {
            Assert(pConnect->Connect.Hdr.u32CmdClientId);
            *pu32ClientID = pConnect->Connect.Hdr.u32CmdClientId;
        }
        else
            WARN(("NEMUCMDVBVA3DCTL_TYPE_CONNECT Disable failed %d", rc));
    }
    else
        WARN(("nemuCmdVbvaCtlSubmitSync returnd %d", rc));

    nemuCmdVbvaCtlFree(pHGSMICtx, &pConnect->Hdr);

    return rc;
}

int nemuCmdVbvaConDisconnect(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, uint32_t u32ClientID)
{
    NEMUCMDVBVA_CTL_3DCTL *pDisconnect = (NEMUCMDVBVA_CTL_3DCTL*)nemuCmdVbvaCtlCreate(pHGSMICtx, sizeof (NEMUCMDVBVA_CTL_3DCTL));
    if (!pDisconnect)
    {
        WARN(("nemuCmdVbvaCtlCreate failed"));
        return VERR_OUT_OF_RESOURCES;
    }
    pDisconnect->Hdr.u32Type = NEMUCMDVBVACTL_TYPE_3DCTL;
    pDisconnect->Hdr.i32Result = VERR_NOT_SUPPORTED;
    pDisconnect->Ctl.u32Type = NEMUCMDVBVA3DCTL_TYPE_DISCONNECT;
    pDisconnect->Ctl.u32CmdClientId = u32ClientID;

    int rc = nemuCmdVbvaCtlSubmitSync(pHGSMICtx, &pDisconnect->Hdr);
    if (RT_SUCCESS(rc))
    {
        rc = pDisconnect->Hdr.i32Result;
        if (!RT_SUCCESS(rc))
            WARN(("NEMUCMDVBVA3DCTL_TYPE_DISCONNECT Disable failed %d", rc));
    }
    else
        WARN(("nemuCmdVbvaCtlSubmitSync returnd %d", rc));

    nemuCmdVbvaCtlFree(pHGSMICtx, &pDisconnect->Hdr);

    return rc;
}

int NemuCmdVbvaConConnect(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva,
        uint32_t crVersionMajor, uint32_t crVersionMinor,
        uint32_t *pu32ClientID)
{
    return nemuCmdVbvaConConnect(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, crVersionMajor, crVersionMinor, pu32ClientID);
}

int NemuCmdVbvaConDisconnect(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, uint32_t u32ClientID)
{
    return nemuCmdVbvaConDisconnect(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, u32ClientID);
}

NEMUCMDVBVA_CRCMD_CMD* nemuCmdVbvaConCmdAlloc(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, uint32_t cbCmd)
{
    NEMUCMDVBVA_CTL_3DCTL_CMD *pCmd = (NEMUCMDVBVA_CTL_3DCTL_CMD*)nemuCmdVbvaCtlCreate(pHGSMICtx, sizeof (NEMUCMDVBVA_CTL_3DCTL_CMD) + cbCmd);
    if (!pCmd)
    {
        WARN(("nemuCmdVbvaCtlCreate failed"));
        return NULL;
    }
    pCmd->Hdr.u32Type = NEMUCMDVBVACTL_TYPE_3DCTL;
    pCmd->Hdr.i32Result = VERR_NOT_SUPPORTED;
    pCmd->Cmd.Hdr.u32Type = NEMUCMDVBVA3DCTL_TYPE_CMD;
    pCmd->Cmd.Hdr.u32CmdClientId = 0;
    pCmd->Cmd.Cmd.u8OpCode = NEMUCMDVBVA_OPTYPE_CRCMD;
    pCmd->Cmd.Cmd.u8Flags = 0;
    pCmd->Cmd.Cmd.u8State = NEMUCMDVBVA_STATE_SUBMITTED;
    pCmd->Cmd.Cmd.u.i8Result = -1;
    pCmd->Cmd.Cmd.u2.u32FenceID = 0;

    return (NEMUCMDVBVA_CRCMD_CMD*)(pCmd+1);
}

void nemuCmdVbvaConCmdFree(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, NEMUCMDVBVA_CRCMD_CMD *pCmd)
{
    NEMUCMDVBVA_CTL_3DCTL_CMD *pHdr = ((NEMUCMDVBVA_CTL_3DCTL_CMD*)pCmd)-1;
    nemuCmdVbvaCtlFree(pHGSMICtx, &pHdr->Hdr);
}

int nemuCmdVbvaConSubmitAsync(PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, NEMUCMDVBVA_CRCMD_CMD* pCmd, FNNEMUSHGSMICMDCOMPLETION pfnCompletion, void *pvCompletion)
{
    NEMUCMDVBVA_CTL_3DCTL_CMD *pHdr = ((NEMUCMDVBVA_CTL_3DCTL_CMD*)pCmd)-1;
    return nemuCmdVbvaCtlSubmitAsync(pHGSMICtx, &pHdr->Hdr, pfnCompletion, pvCompletion);
}

NEMUCMDVBVA_CRCMD_CMD* NemuCmdVbvaConCmdAlloc(PNEMUMP_DEVEXT pDevExt, uint32_t cbCmd)
{
    return nemuCmdVbvaConCmdAlloc(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, cbCmd);
}

void NemuCmdVbvaConCmdFree(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA_CRCMD_CMD *pCmd)
{
    nemuCmdVbvaConCmdFree(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, pCmd);
}

int NemuCmdVbvaConCmdSubmitAsync(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA_CRCMD_CMD* pCmd, FNNEMUSHGSMICMDCOMPLETION pfnCompletion, void *pvCompletion)
{
    return nemuCmdVbvaConSubmitAsync(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, pCmd, pfnCompletion, pvCompletion);
}

int NemuCmdVbvaConCmdCompletionData(void *pvCmd, NEMUCMDVBVA_CRCMD_CMD **ppCmd)
{
    NEMUCMDVBVA_CTL_3DCTL_CMD *pCmd = (NEMUCMDVBVA_CTL_3DCTL_CMD*)pvCmd;
    if (ppCmd)
        *ppCmd = (NEMUCMDVBVA_CRCMD_CMD*)(pCmd+1);
    return pCmd->Hdr.i32Result;
}

int NemuCmdVbvaConCmdResize(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData, const uint32_t *pTargetMap, const POINT * pVScreenPos, uint16_t fFlags)
{
    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);

    NEMUCMDVBVA_CTL_RESIZE *pResize = (NEMUCMDVBVA_CTL_RESIZE*)nemuCmdVbvaCtlCreate(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, sizeof (NEMUCMDVBVA_CTL_RESIZE));
    if (!pResize)
    {
        WARN(("nemuCmdVbvaCtlCreate failed"));
        return VERR_OUT_OF_RESOURCES;
    }

    pResize->Hdr.u32Type = NEMUCMDVBVACTL_TYPE_RESIZE;
    pResize->Hdr.i32Result = VERR_NOT_IMPLEMENTED;

    int rc = nemuWddmScreenInfoInit(&pResize->Resize.aEntries[0].Screen, pAllocData, pVScreenPos, fFlags);
    if (RT_SUCCESS(rc))
    {
        memcpy(&pResize->Resize.aEntries[0].aTargetMap, pTargetMap, sizeof (pResize->Resize.aEntries[0].aTargetMap));
        rc = nemuCmdVbvaCtlSubmitSync(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, &pResize->Hdr);
        if (RT_SUCCESS(rc))
        {
            rc = pResize->Hdr.i32Result;
            if (RT_FAILURE(rc))
                WARN(("NEMUCMDVBVACTL_TYPE_RESIZE failed %d", rc));
        }
        else
            WARN(("nemuCmdVbvaCtlSubmitSync failed %d", rc));
    }
    else
        WARN(("nemuWddmScreenInfoInit failed %d", rc));

    nemuCmdVbvaCtlFree(&NemuCommonFromDeviceExt(pDevExt)->guestCtx, &pResize->Hdr);

    return rc;
}
#endif
