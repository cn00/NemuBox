/* $Id: NemuMPVbva.h $ */

/** @file
 * Nemu WDDM Miniport driver
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

#ifndef ___NemuMPVbva_h___
#define ___NemuMPVbva_h___

typedef struct NEMUVBVAINFO
{
    VBVABUFFERCONTEXT Vbva;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId;
    KSPIN_LOCK Lock;
} NEMUVBVAINFO;

int nemuVbvaEnable(PNEMUMP_DEVEXT pDevExt, NEMUVBVAINFO *pVbva);
int nemuVbvaDisable(PNEMUMP_DEVEXT pDevExt, NEMUVBVAINFO *pVbva);
int nemuVbvaDestroy(PNEMUMP_DEVEXT pDevExt, NEMUVBVAINFO *pVbva);
int nemuVbvaCreate(PNEMUMP_DEVEXT pDevExt, NEMUVBVAINFO *pVbva, ULONG offBuffer, ULONG cbBuffer, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId);
int nemuVbvaReportDirtyRect(PNEMUMP_DEVEXT pDevExt, struct NEMUWDDM_SOURCE *pSrc, RECT *pRectOrig);

#define NEMUVBVA_OP(_op, _pdext, _psrc, _arg) \
        do { \
            if (NemuVBVABufferBeginUpdate(&(_psrc)->Vbva.Vbva, &NemuCommonFromDeviceExt(_pdext)->guestCtx)) \
            { \
                nemuVbva##_op(_pdext, _psrc, _arg); \
                NemuVBVABufferEndUpdate(&(_psrc)->Vbva.Vbva); \
            } \
        } while (0)

#define NEMUVBVA_OP_WITHLOCK_ATDPC(_op, _pdext, _psrc, _arg) \
        do { \
            Assert(KeGetCurrentIrql() == DISPATCH_LEVEL); \
            KeAcquireSpinLockAtDpcLevel(&(_psrc)->Vbva.Lock);  \
            NEMUVBVA_OP(_op, _pdext, _psrc, _arg);        \
            KeReleaseSpinLockFromDpcLevel(&(_psrc)->Vbva.Lock);\
        } while (0)

#define NEMUVBVA_OP_WITHLOCK(_op, _pdext, _psrc, _arg) \
        do { \
            KIRQL OldIrql; \
            KeAcquireSpinLock(&(_psrc)->Vbva.Lock, &OldIrql);  \
            NEMUVBVA_OP(_op, _pdext, _psrc, _arg);        \
            KeReleaseSpinLock(&(_psrc)->Vbva.Lock, OldIrql);   \
        } while (0)


#ifdef NEMU_WITH_CROGL
/* customized VBVA implementation */
struct VBVAEXBUFFERCONTEXT;

typedef DECLCALLBACKPTR(void, PFNVBVAEXBUFFERFLUSH) (struct VBVAEXBUFFERCONTEXT *pCtx, PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, void *pvFlush);

/**
 * Structure grouping the context needed for sending graphics acceleration
 * information to the host via VBVA.  Each screen has its own VBVA buffer.
 */
typedef struct VBVAEXBUFFERCONTEXT
{
    /** Offset of the buffer in the VRAM section for the screen */
    uint32_t    offVRAMBuffer;
    /** Length of the buffer in bytes */
    uint32_t    cbBuffer;
    /** This flag is set if we wrote to the buffer faster than the host could
     * read it. */
    bool        fHwBufferOverflow;
    /* the window between indexRecordFirstUncompleted and pVBVA->::indexRecordFirst represents
     * command records processed by the host, but not completed by the guest yet */
    volatile uint32_t    indexRecordFirstUncompleted;
    /* the window between off32DataUncompleted and pVBVA->::off32Data represents
     * command data processed by the host, but not completed by the guest yet */
    uint32_t    off32DataUncompleted;
    /* flush function */
    PFNVBVAEXBUFFERFLUSH pfnFlush;
    void *pvFlush;
    /** The VBVA record that we are currently preparing for the host, NULL if
     * none. */
    struct VBVARECORD *pRecord;
    /** Pointer to the VBVA buffer mapped into the current address space.  Will
     * be NULL if VBVA is not enabled. */
    struct VBVABUFFER *pVBVA;
} VBVAEXBUFFERCONTEXT, *PVBVAEXBUFFERCONTEXT;

typedef struct VBVAEXBUFFERITERBASE
{
    struct VBVAEXBUFFERCONTEXT *pCtx;
    /* index of the current record */
    uint32_t iCurRecord;
    /* offset of the current command */
    uint32_t off32CurCmd;
} VBVAEXBUFFERITERBASE, *PVBVAEXBUFFERITERBASE;

typedef struct VBVAEXBUFFERFORWARDITER
{
    VBVAEXBUFFERITERBASE Base;
} VBVAEXBUFFERFORWARDITER, *PVBVAEXBUFFERFORWARDITER;

typedef struct VBVAEXBUFFERBACKWARDITER
{
    VBVAEXBUFFERITERBASE Base;
} VBVAEXBUFFERBACKWARDITER, *PVBVAEXBUFFERBACKWARDITER;

#define NEMUCMDVBVA_BUFFERSIZE(_cbCmdApprox) (RT_OFFSETOF(VBVABUFFER, au8Data) + ((RT_SIZEOFMEMB(VBVABUFFER, aRecords)/RT_SIZEOFMEMB(VBVABUFFER, aRecords[0])) * (_cbCmdApprox)))

typedef struct NEMUCMDVBVA_PREEMPT_EL
{
    uint32_t u32SubmitFence;
    uint32_t u32PreemptFence;
} NEMUCMDVBVA_PREEMPT_EL;

#define NEMUCMDVBVA_PREEMPT_EL_SIZE 16

typedef struct NEMUCMDVBVA
{
    VBVAEXBUFFERCONTEXT Vbva;

    /* last completted fence id */
    uint32_t u32FenceCompleted;
    /* last submitted fence id */
    uint32_t u32FenceSubmitted;
    /* last processed fence id (i.e. either completed or cancelled) */
    uint32_t u32FenceProcessed;

    /* node ordinal */
    uint32_t idNode;

    uint32_t cPreempt;
    uint32_t iCurPreempt;
    NEMUCMDVBVA_PREEMPT_EL aPreempt[NEMUCMDVBVA_PREEMPT_EL_SIZE];
} NEMUCMDVBVA;

/** @name VBVAEx APIs
 * @{ */
RTDECL(int) NemuVBVAExEnable(PVBVAEXBUFFERCONTEXT pCtx,
                            PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                            struct VBVABUFFER *pVBVA);
RTDECL(void) NemuVBVAExDisable(PVBVAEXBUFFERCONTEXT pCtx,
                             PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx);
RTDECL(bool) NemuVBVAExBufferBeginUpdate(PVBVAEXBUFFERCONTEXT pCtx,
                                       PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx);
RTDECL(void) NemuVBVAExBufferEndUpdate(PVBVAEXBUFFERCONTEXT pCtx);
RTDECL(bool) NemuVBVAExWrite(PVBVAEXBUFFERCONTEXT pCtx,
                           PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                           const void *pv, uint32_t cb);

RTDECL(bool) NemuVBVAExOrderSupported(PVBVAEXBUFFERCONTEXT pCtx, unsigned code);

RTDECL(void) NemuVBVAExSetupBufferContext(PVBVAEXBUFFERCONTEXT pCtx,
                                        uint32_t offVRAMBuffer,
                                        uint32_t cbBuffer,
                                        PFNVBVAEXBUFFERFLUSH pfnFlush,
                                        void *pvFlush);

DECLINLINE(uint32_t) NemuVBVAExGetSize(PVBVAEXBUFFERCONTEXT pCtx)
{
    return pCtx->pVBVA->cbData;
}

/* can be used to ensure the command will not cross the ring buffer boundary,
 * and thus will not be splitted */
RTDECL(uint32_t) NemuVBVAExGetFreeTail(PVBVAEXBUFFERCONTEXT pCtx);
/* allocates a contiguous buffer of a given size, i.e. the one that is not splitted across ringbuffer boundaries */
RTDECL(void*) NemuVBVAExAllocContiguous(PVBVAEXBUFFERCONTEXT pCtx, PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, uint32_t cb);
/* answers whether host is in "processing" state now,
 * i.e. if "processing" is true after the command is submitted, no notification is required to be posted to host to make the commandbe processed,
 * otherwise, host should be notified about the command */
RTDECL(bool) NemuVBVAExIsProcessing(PVBVAEXBUFFERCONTEXT pCtx);

/* initializes iterator that starts with free record,
 * i.e. NemuVBVAExIterNext would return the first uncompleted record.
 *
 * can be used by submitter only */
RTDECL(void) NemuVBVAExBIterInit(PVBVAEXBUFFERCONTEXT pCtx, PVBVAEXBUFFERBACKWARDITER pIter);
/* can be used by submitter only */
RTDECL(void*) NemuVBVAExBIterNext(PVBVAEXBUFFERBACKWARDITER pIter, uint32_t *pcbBuffer, bool *pfProcessed);

/* completer functions
 * completer can only use below ones, and submitter is NOT allowed to use them.
 * Completter functions are prefixed with NemuVBVAExC as opposed to submitter ones,
 * that do not have the last "C" in the prefix */
/* initializes iterator that starts with completed record,
 * i.e. NemuVBVAExIterPrev would return the first uncompleted record.
 * note that we can not have iterator that starts at processed record
 * (i.e. the one processed by host, but not completed by guest, since host modifies
 * VBVABUFFER::off32Data and VBVABUFFER::indexRecordFirst concurrently,
 * and so we may end up with inconsistent index-offData pair
 *
 * can be used by completter only */
RTDECL(void) NemuVBVAExCFIterInit(PVBVAEXBUFFERCONTEXT pCtx, PVBVAEXBUFFERFORWARDITER pIter);
/* can be used by completter only */
RTDECL(void*) NemuVBVAExCFIterNext(PVBVAEXBUFFERFORWARDITER pIter, uint32_t *pcbBuffer, bool *pfProcessed);

RTDECL(void) NemuVBVAExCBufferCompleted(PVBVAEXBUFFERCONTEXT pCtx);

/** @}  */

struct NEMUCMDVBVA_HDR;

int NemuCmdVbvaEnable(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva);
int NemuCmdVbvaDisable(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva);
int NemuCmdVbvaDestroy(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva);
int NemuCmdVbvaCreate(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, ULONG offBuffer, ULONG cbBuffer);
int NemuCmdVbvaSubmit(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, struct NEMUCMDVBVA_HDR *pCmd, uint32_t u32FenceID, uint32_t cbCmd);
void NemuCmdVbvaSubmitUnlock(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, NEMUCMDVBVA_HDR* pCmd, uint32_t u32FenceID);
NEMUCMDVBVA_HDR* NemuCmdVbvaSubmitLock(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, uint32_t cbCmd);
bool NemuCmdVbvaPreempt(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, uint32_t u32FenceID);
uint32_t NemuCmdVbvaCheckCompleted(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, bool fPingHost, uint32_t *pu32FenceSubmitted, uint32_t *pu32FenceProcessed);
bool NemuCmdVbvaCheckCompletedIrq(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva);

/*helper functions for filling vbva commands */
DECLINLINE(void) NemuCVDdiPackRect(NEMUCMDVBVA_RECT *pVbvaRect, const RECT *pRect)
{
    pVbvaRect->xLeft = (int16_t)pRect->left;
    pVbvaRect->yTop = (int16_t)pRect->top;
    pVbvaRect->xRight = (int16_t)pRect->right;
    pVbvaRect->yBottom = (int16_t)pRect->bottom;
}

DECLINLINE(void) NemuCVDdiPackRects(NEMUCMDVBVA_RECT *paVbvaRects, const RECT *paRects, uint32_t cRects)
{
    for (uint32_t i = 0; i < cRects; ++i)
    {
        NemuCVDdiPackRect(&paVbvaRects[i], &paRects[i]);
    }

}

uint32_t NemuCVDdiPTransferVRamSysBuildEls(NEMUCMDVBVA_PAGING_TRANSFER *pCmd, PMDL pMdl, uint32_t iPfn, uint32_t cPages, uint32_t cbBuffer, uint32_t *pcPagesWritten);

int NemuCmdVbvaConConnect(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva,
        uint32_t crVersionMajor, uint32_t crVersionMinor,
        uint32_t *pu32ClientID);
int NemuCmdVbvaConDisconnect(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA *pVbva, uint32_t u32ClientID);
NEMUCMDVBVA_CRCMD_CMD* NemuCmdVbvaConCmdAlloc(PNEMUMP_DEVEXT pDevExt, uint32_t cbCmd);
void NemuCmdVbvaConCmdFree(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA_CRCMD_CMD* pCmd);
int NemuCmdVbvaConCmdSubmitAsync(PNEMUMP_DEVEXT pDevExt, NEMUCMDVBVA_CRCMD_CMD* pCmd, FNNEMUSHGSMICMDCOMPLETION pfnCompletion, void *pvCompletion);
int NemuCmdVbvaConCmdCompletionData(void *pvCmd, NEMUCMDVBVA_CRCMD_CMD **ppCmd);
int NemuCmdVbvaConCmdResize(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData, const uint32_t *pTargetMap, const POINT * pVScreenPos, uint16_t fFlags);
#endif /* #ifdef NEMU_WITH_CROGL */

#endif /* #ifndef ___NemuMPVbva_h___ */
