/* $Id: VBVABase.cpp $ */
/** @file
 * VirtualBox Video driver, common code - VBVA initialisation and helper
 * functions.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <Nemu/NemuVideoGuest.h>
#include <Nemu/NemuVideo.h>
#include <Nemu/err.h>
#include <Nemu/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>

/*
 * There is a hardware ring buffer in the graphics device video RAM, formerly
 * in the Nemu VMMDev PCI memory space.
 * All graphics commands go there serialized by NemuVBVABufferBeginUpdate.
 * and nemuHwBufferEndUpdate.
 *
 * off32Free is writing position. off32Data is reading position.
 * off32Free == off32Data means buffer is empty.
 * There must be always gap between off32Data and off32Free when data
 * are in the buffer.
 * Guest only changes off32Free, host changes off32Data.
 */

/* Forward declarations of internal functions. */
static void nemuHwBufferFlush(PHGSMIGUESTCOMMANDCONTEXT pCtx);
static void nemuHwBufferPlaceDataAt(PVBVABUFFERCONTEXT pCtx, const void *p,
                                    uint32_t cb, uint32_t offset);
static bool nemuHwBufferWrite(PVBVABUFFERCONTEXT pCtx,
                              PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                              const void *p, uint32_t cb);


static bool nemuVBVAInformHost(PVBVABUFFERCONTEXT pCtx,
                               PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                               int32_t cScreen, bool bEnable)
{
    bool bRc = false;

#if 0  /* All callers check this */
    if (ppdev->bHGSMISupported)
#endif
    {
        void *p = NemuHGSMIBufferAlloc(pHGSMICtx,
                                       sizeof (VBVAENABLE_EX),
                                       HGSMI_CH_VBVA,
                                       VBVA_ENABLE);
        if (!p)
        {
            LogFunc(("HGSMIHeapAlloc failed\n"));
        }
        else
        {
            VBVAENABLE_EX *pEnable = (VBVAENABLE_EX *)p;

            pEnable->Base.u32Flags  = bEnable? VBVA_F_ENABLE: VBVA_F_DISABLE;
            pEnable->Base.u32Offset = pCtx->offVRAMBuffer;
            pEnable->Base.i32Result = VERR_NOT_SUPPORTED;
            if (cScreen >= 0)
            {
                pEnable->Base.u32Flags |= VBVA_F_EXTENDED | VBVA_F_ABSOFFSET;
                pEnable->u32ScreenId    = cScreen;
            }

            NemuHGSMIBufferSubmit(pHGSMICtx, p);

            if (bEnable)
            {
                bRc = RT_SUCCESS(pEnable->Base.i32Result);
            }
            else
            {
                bRc = true;
            }

            NemuHGSMIBufferFree(pHGSMICtx, p);
        }
    }

    return bRc;
}

/*
 * Public hardware buffer methods.
 */
RTDECL(bool) NemuVBVAEnable(PVBVABUFFERCONTEXT pCtx,
                            PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                            VBVABUFFER *pVBVA, int32_t cScreen)
{
    bool bRc = false;

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

        bRc = nemuVBVAInformHost(pCtx, pHGSMICtx, cScreen, true);
    }

    if (!bRc)
    {
        NemuVBVADisable(pCtx, pHGSMICtx, cScreen);
    }

    return bRc;
}

RTDECL(void) NemuVBVADisable(PVBVABUFFERCONTEXT pCtx,
                             PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                             int32_t cScreen)
{
    LogFlowFunc(("\n"));

    pCtx->fHwBufferOverflow = false;
    pCtx->pRecord           = NULL;
    pCtx->pVBVA             = NULL;

    nemuVBVAInformHost(pCtx, pHGSMICtx, cScreen, false);

    return;
}

RTDECL(bool) NemuVBVABufferBeginUpdate(PVBVABUFFERCONTEXT pCtx,
                                       PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx)
{
    bool bRc = false;

    // LogFunc(("flags = 0x%08X\n", pCtx->pVBVA? pCtx->pVBVA->u32HostEvents: -1));

    if (   pCtx->pVBVA
        && (pCtx->pVBVA->hostFlags.u32HostEvents & VBVA_F_MODE_ENABLED))
    {
        uint32_t indexRecordNext;

        Assert(!pCtx->fHwBufferOverflow);
        Assert(pCtx->pRecord == NULL);

        indexRecordNext = (pCtx->pVBVA->indexRecordFree + 1) % VBVA_MAX_RECORDS;

        if (indexRecordNext == pCtx->pVBVA->indexRecordFirst)
        {
            /* All slots in the records queue are used. */
            nemuHwBufferFlush (pHGSMICtx);
        }

        if (indexRecordNext == pCtx->pVBVA->indexRecordFirst)
        {
            /* Even after flush there is no place. Fail the request. */
            LogFunc(("no space in the queue of records!!! first %d, last %d\n",
                     pCtx->pVBVA->indexRecordFirst, pCtx->pVBVA->indexRecordFree));
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

RTDECL(void) NemuVBVABufferEndUpdate(PVBVABUFFERCONTEXT pCtx)
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

/*
 * Private operations.
 */
static uint32_t nemuHwBufferAvail (const VBVABUFFER *pVBVA)
{
    int32_t i32Diff = pVBVA->off32Data - pVBVA->off32Free;

    return i32Diff > 0? i32Diff: pVBVA->cbData + i32Diff;
}

static void nemuHwBufferFlush(PHGSMIGUESTCOMMANDCONTEXT pCtx)
{
    /* Issue the flush command. */
    void *p = NemuHGSMIBufferAlloc(pCtx,
                                   sizeof (VBVAFLUSH),
                                   HGSMI_CH_VBVA,
                                   VBVA_FLUSH);
    if (!p)
    {
        LogFunc(("HGSMIHeapAlloc failed\n"));
    }
    else
    {
        VBVAFLUSH *pFlush = (VBVAFLUSH *)p;

        pFlush->u32Reserved = 0;

        NemuHGSMIBufferSubmit(pCtx, p);

        NemuHGSMIBufferFree(pCtx, p);
    }

    return;
}

static void nemuHwBufferPlaceDataAt(PVBVABUFFERCONTEXT pCtx, const void *p,
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

static bool nemuHwBufferWrite(PVBVABUFFERCONTEXT pCtx,
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

    pRecord = pCtx->pRecord;
    Assert(pRecord && (pRecord->cbRecord & VBVA_F_RECORD_PARTIAL));

    // LogFunc(("%d\n", cb));

    cbHwBufferAvail = nemuHwBufferAvail (pVBVA);

    while (cb > 0)
    {
        uint32_t cbChunk = cb;

        // LogFunc(("pVBVA->off32Free %d, pRecord->cbRecord 0x%08X, cbHwBufferAvail %d, cb %d, cbWritten %d\n",
        //             pVBVA->off32Free, pRecord->cbRecord, cbHwBufferAvail, cb, cbWritten));

        if (cbChunk >= cbHwBufferAvail)
        {
            LogFunc(("1) avail %d, chunk %d\n", cbHwBufferAvail, cbChunk));

            nemuHwBufferFlush (pHGSMICtx);

            cbHwBufferAvail = nemuHwBufferAvail (pVBVA);

            if (cbChunk >= cbHwBufferAvail)
            {
                LogFunc(("no place for %d bytes. Only %d bytes available after flush. Going to partial writes.\n",
                            cb, cbHwBufferAvail));

                if (cbHwBufferAvail <= pVBVA->cbPartialWriteThreshold)
                {
                    LogFunc(("Buffer overflow!!!\n"));
                    pCtx->fHwBufferOverflow = true;
                    Assert(false);
                    return false;
                }

                cbChunk = cbHwBufferAvail - pVBVA->cbPartialWriteThreshold;
            }
        }

        Assert(cbChunk <= cb);
        Assert(cbChunk <= nemuHwBufferAvail (pVBVA));

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
RTDECL(bool) NemuVBVAWrite(PVBVABUFFERCONTEXT pCtx,
                           PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                           const void *pv, uint32_t cb)
{
    return nemuHwBufferWrite (pCtx, pHGSMICtx, pv, cb);
}

RTDECL(bool) NemuVBVAOrderSupported(PVBVABUFFERCONTEXT pCtx, unsigned code)
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

RTDECL(void) NemuVBVASetupBufferContext(PVBVABUFFERCONTEXT pCtx,
                                        uint32_t offVRAMBuffer,
                                        uint32_t cbBuffer)
{
    pCtx->offVRAMBuffer = offVRAMBuffer;
    pCtx->cbBuffer      = cbBuffer;
}
