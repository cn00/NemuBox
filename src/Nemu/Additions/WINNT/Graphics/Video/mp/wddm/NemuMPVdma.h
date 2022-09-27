/* $Id: NemuMPVdma.h $ */
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

#ifndef ___NemuMPVdma_h___
#define ___NemuMPVdma_h___

#include <iprt/cdefs.h>
#include <iprt/asm.h>
#include <Nemu/NemuVideo.h>
#include <Nemu/HGSMI/HGSMI.h>

typedef struct _NEMUMP_DEVEXT *PNEMUMP_DEVEXT;

/* ddi dma command queue handling */
typedef enum
{
    NEMUVDMADDI_STATE_UNCKNOWN = 0,
    NEMUVDMADDI_STATE_NOT_DX_CMD,
    NEMUVDMADDI_STATE_NOT_QUEUED,
    NEMUVDMADDI_STATE_PENDING,
    NEMUVDMADDI_STATE_SUBMITTED,
    NEMUVDMADDI_STATE_COMPLETED
} NEMUVDMADDI_STATE;

typedef struct NEMUVDMADDI_CMD *PNEMUVDMADDI_CMD;
typedef DECLCALLBACK(VOID) FNNEMUVDMADDICMDCOMPLETE_DPC(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, PVOID pvContext);
typedef FNNEMUVDMADDICMDCOMPLETE_DPC *PFNNEMUVDMADDICMDCOMPLETE_DPC;

typedef struct NEMUVDMADDI_CMD
{
    LIST_ENTRY QueueEntry;
    NEMUVDMADDI_STATE enmState;
    uint32_t u32NodeOrdinal;
    uint32_t u32FenceId;
    DXGK_INTERRUPT_TYPE enmComplType;
    PFNNEMUVDMADDICMDCOMPLETE_DPC pfnComplete;
    PVOID pvComplete;
} NEMUVDMADDI_CMD, *PNEMUVDMADDI_CMD;

typedef struct NEMUVDMADDI_CMD_QUEUE
{
    volatile uint32_t cQueuedCmds;
    LIST_ENTRY CmdQueue;
} NEMUVDMADDI_CMD_QUEUE, *PNEMUVDMADDI_CMD_QUEUE;

typedef struct NEMUVDMADDI_NODE
{
    NEMUVDMADDI_CMD_QUEUE CmdQueue;
    UINT uLastCompletedFenceId;
} NEMUVDMADDI_NODE, *PNEMUVDMADDI_NODE;

VOID nemuVdmaDdiNodesInit(PNEMUMP_DEVEXT pDevExt);
BOOLEAN nemuVdmaDdiCmdCompletedIrq(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, DXGK_INTERRUPT_TYPE enmComplType);
VOID nemuVdmaDdiCmdSubmittedIrq(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd);

NTSTATUS nemuVdmaDdiCmdCompleted(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, DXGK_INTERRUPT_TYPE enmComplType);
NTSTATUS nemuVdmaDdiCmdSubmitted(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd);

DECLINLINE(VOID) nemuVdmaDdiCmdInit(PNEMUVDMADDI_CMD pCmd,
        uint32_t u32NodeOrdinal, uint32_t u32FenceId,
        PFNNEMUVDMADDICMDCOMPLETE_DPC pfnComplete, PVOID pvComplete)
{
    pCmd->QueueEntry.Blink = NULL;
    pCmd->QueueEntry.Flink = NULL;
    pCmd->enmState = NEMUVDMADDI_STATE_NOT_QUEUED;
    pCmd->u32NodeOrdinal = u32NodeOrdinal;
    pCmd->u32FenceId = u32FenceId;
    pCmd->pfnComplete = pfnComplete;
    pCmd->pvComplete = pvComplete;
}

/* marks the command a submitted in a way that it is invisible for dx runtime,
 * i.e. the dx runtime won't be notified about the command completion
 * this is used to submit commands initiated by the driver, but not by the dx runtime */
DECLINLINE(VOID) nemuVdmaDdiCmdSubmittedNotDx(PNEMUVDMADDI_CMD pCmd)
{
    Assert(pCmd->enmState == NEMUVDMADDI_STATE_NOT_QUEUED);
    pCmd->enmState = NEMUVDMADDI_STATE_NOT_DX_CMD;
}

NTSTATUS nemuVdmaDdiCmdFenceComplete(PNEMUMP_DEVEXT pDevExt, uint32_t u32NodeOrdinal, uint32_t u32FenceId, DXGK_INTERRUPT_TYPE enmComplType);

DECLCALLBACK(VOID) nemuVdmaDdiCmdCompletionCbFree(PNEMUMP_DEVEXT pDevExt, PNEMUVDMADDI_CMD pCmd, PVOID pvContext);

VOID nemuVdmaDdiCmdGetCompletedListIsr(PNEMUMP_DEVEXT pDevExt, LIST_ENTRY *pList);

BOOLEAN nemuVdmaDdiCmdIsCompletedListEmptyIsr(PNEMUMP_DEVEXT pDevExt);

#define NEMUVDMADDI_CMD_FROM_ENTRY(_pEntry) ((PNEMUVDMADDI_CMD)(((uint8_t*)(_pEntry)) - RT_OFFSETOF(NEMUVDMADDI_CMD, QueueEntry)))

DECLINLINE(VOID) nemuVdmaDdiCmdHandleCompletedList(PNEMUMP_DEVEXT pDevExt, LIST_ENTRY *pList)
{
    LIST_ENTRY *pEntry = pList->Flink;
    while (pEntry != pList)
    {
        PNEMUVDMADDI_CMD pCmd = NEMUVDMADDI_CMD_FROM_ENTRY(pEntry);
        pEntry = pEntry->Flink;
        if (pCmd->pfnComplete)
            pCmd->pfnComplete(pDevExt, pCmd, pCmd->pvComplete);
    }
}

#if 0
typedef DECLCALLBACK(int) FNNEMUVDMASUBMIT(struct _DEVICE_EXTENSION* pDevExt, struct NEMUVDMAINFO * pInfo, HGSMIOFFSET offDr, PVOID pvContext);
typedef FNNEMUVDMASUBMIT *PFNNEMUVDMASUBMIT;

typedef struct NEMUVDMASUBMIT
{
    PFNNEMUVDMASUBMIT pfnSubmit;
    PVOID pvContext;
} NEMUVDMASUBMIT, *PNEMUVDMASUBMIT;
#endif

/* start */
typedef enum
{
    NEMUVDMAPIPE_STATE_CLOSED    = 0,
    NEMUVDMAPIPE_STATE_CREATED   = 1,
    NEMUVDMAPIPE_STATE_OPENNED   = 2,
    NEMUVDMAPIPE_STATE_CLOSING   = 3
} NEMUVDMAPIPE_STATE;

typedef struct NEMUVDMAPIPE
{
    KSPIN_LOCK SinchLock;
    KEVENT Event;
    LIST_ENTRY CmdListHead;
    NEMUVDMAPIPE_STATE enmState;
    /* true iff the other end needs Event notification */
    bool bNeedNotify;
} NEMUVDMAPIPE, *PNEMUVDMAPIPE;

typedef struct NEMUVDMAPIPE_CMD_HDR
{
    LIST_ENTRY ListEntry;
} NEMUVDMAPIPE_CMD_HDR, *PNEMUVDMAPIPE_CMD_HDR;

#define NEMUVDMAPIPE_CMD_HDR_FROM_ENTRY(_pE)  ( (PNEMUVDMAPIPE_CMD_HDR)((uint8_t *)(_pE) - RT_OFFSETOF(NEMUVDMAPIPE_CMD_HDR, ListEntry)) )

typedef enum
{
    NEMUVDMAPIPE_CMD_TYPE_UNDEFINED = 0,
    NEMUVDMAPIPE_CMD_TYPE_RECTSINFO,
    NEMUVDMAPIPE_CMD_TYPE_DMACMD,
    NEMUVDMAPIPE_CMD_TYPE_FINISH, /* ensures all previously submitted commands are completed */
    NEMUVDMAPIPE_CMD_TYPE_CANCEL
} NEMUVDMAPIPE_CMD_TYPE;

typedef struct NEMUVDMAPIPE_CMD_DR
{
    NEMUVDMAPIPE_CMD_HDR PipeHdr;
    NEMUVDMAPIPE_CMD_TYPE enmType;
    volatile uint32_t cRefs;
} NEMUVDMAPIPE_CMD_DR, *PNEMUVDMAPIPE_CMD_DR;

#define NEMUVDMAPIPE_CMD_DR_FROM_ENTRY(_pE)  ( (PNEMUVDMAPIPE_CMD_DR)NEMUVDMAPIPE_CMD_HDR_FROM_ENTRY(_pE) )

typedef struct NEMUWDDM_DMA_ALLOCINFO
{
    PNEMUWDDM_ALLOCATION pAlloc;
    NEMUVIDEOOFFSET offAlloc;
    UINT segmentIdAlloc : 31;
    UINT fWriteOp : 1;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId;
} NEMUWDDM_DMA_ALLOCINFO, *PNEMUWDDM_DMA_ALLOCINFO;

typedef struct NEMUVDMAPIPE_RECTS
{
    RECT ContextRect;
    NEMUWDDM_RECTS_INFO UpdateRects;
} NEMUVDMAPIPE_RECTS, *PNEMUVDMAPIPE_RECTS;

typedef struct NEMUVDMAPIPE_CMD_RECTSINFO
{
    NEMUVDMAPIPE_CMD_DR Hdr;
    PNEMUWDDM_CONTEXT pContext;
    struct NEMUWDDM_SWAPCHAIN *pSwapchain;
    NEMUVDMAPIPE_RECTS ContextsRects;
} NEMUVDMAPIPE_CMD_RECTSINFO, *PNEMUVDMAPIPE_CMD_RECTSINFO;

typedef struct NEMUVDMAPIPE_CMD_DMACMD
{
    NEMUVDMAPIPE_CMD_DR Hdr;
#ifndef NEMU_WDDM_IRQ_COMPLETION
    NEMUVDMADDI_CMD DdiCmd;
#endif
    PNEMUWDDM_CONTEXT pContext;
    NEMUVDMACMD_TYPE enmCmd;
//    NEMUVDMAPIPE_FLAGS_DMACMD fFlags;
} NEMUVDMAPIPE_CMD_DMACMD, *PNEMUVDMAPIPE_CMD_DMACMD;

typedef struct NEMUVDMA_CLRFILL
{
    NEMUWDDM_DMA_ALLOCINFO Alloc;
    UINT Color;
    NEMUWDDM_RECTS_INFO Rects;
} NEMUVDMA_CLRFILL, *PNEMUVDMA_CLRFILL;

typedef struct NEMUVDMAPIPE_CMD_DMACMD_CLRFILL
{
    NEMUVDMAPIPE_CMD_DMACMD Hdr;
    NEMUVDMA_CLRFILL ClrFill;
} NEMUVDMAPIPE_CMD_DMACMD_CLRFILL, *PNEMUVDMAPIPE_CMD_DMACMD_CLRFILL;

typedef struct NEMUVDMA_BLT
{
    NEMUWDDM_DMA_ALLOCINFO SrcAlloc;
    NEMUWDDM_DMA_ALLOCINFO DstAlloc;
    RECT SrcRect;
    NEMUVDMAPIPE_RECTS DstRects;
} NEMUVDMA_BLT, *PNEMUVDMA_BLT;

typedef struct NEMUVDMAPIPE_CMD_DMACMD_BLT
{
    NEMUVDMAPIPE_CMD_DMACMD Hdr;
    NEMUVDMA_BLT Blt;
} NEMUVDMAPIPE_CMD_DMACMD_BLT, *PNEMUVDMAPIPE_CMD_DMACMD_BLT;

typedef struct NEMUVDMA_FLIP
{
    NEMUWDDM_DMA_ALLOCINFO Alloc;
} NEMUVDMA_FLIP, *PNEMUVDMA_FLIP;

typedef struct NEMUVDMAPIPE_CMD_DMACMD_FLIP
{
    NEMUVDMAPIPE_CMD_DMACMD Hdr;
    NEMUVDMA_FLIP Flip;
} NEMUVDMAPIPE_CMD_DMACMD_FLIP, *PNEMUVDMAPIPE_CMD_DMACMD_FLIP;

typedef struct NEMUVDMA_SHADOW2PRIMARY
{
    NEMUWDDM_DMA_ALLOCINFO ShadowAlloc;
    RECT SrcRect;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} NEMUVDMA_SHADOW2PRIMARY, *PNEMUVDMA_SHADOW2PRIMARY;

typedef struct NEMUVDMAGG
{
    NEMUVDMAPIPE CmdPipe;
    PKTHREAD pThread;
} NEMUVDMAGG, *PNEMUVDMAGG;

/* DMA commands are currently submitted over HGSMI */
typedef struct NEMUVDMAINFO
{
#ifdef NEMU_WITH_VDMA
    NEMUSHGSMI CmdHeap;
#endif
    UINT      uLastCompletedPagingBufferCmdFenceId;
    BOOL      fEnabled;
} NEMUVDMAINFO, *PNEMUVDMAINFO;

int nemuVdmaCreate (PNEMUMP_DEVEXT pDevExt, NEMUVDMAINFO *pInfo
#ifdef NEMU_WITH_VDMA
        , ULONG offBuffer, ULONG cbBuffer
#endif
#if 0
        , PFNNEMUVDMASUBMIT pfnSubmit, PVOID pvContext
#endif
        );
int nemuVdmaDisable(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo);
int nemuVdmaEnable(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo);
int nemuVdmaDestroy(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo);

#ifdef NEMU_WITH_VDMA
int nemuVdmaFlush(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo);
DECLINLINE(HGSMIOFFSET) nemuVdmaCBufDrPtrOffset(const PNEMUVDMAINFO pInfo, const void* pvPtr)
{
    return NemuSHGSMICommandPtrOffset(&pInfo->CmdHeap, pvPtr);
}
int nemuVdmaCBufDrSubmit(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo, PNEMUVDMACBUF_DR pDr);
int nemuVdmaCBufDrSubmitSynch(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAINFO pInfo, PNEMUVDMACBUF_DR pDr);
struct NEMUVDMACBUF_DR* nemuVdmaCBufDrCreate(PNEMUVDMAINFO pInfo, uint32_t cbTrailingData);
void nemuVdmaCBufDrFree(PNEMUVDMAINFO pInfo, struct NEMUVDMACBUF_DR* pDr);

#define NEMUVDMACBUF_DR_DATA_OFFSET() (sizeof (NEMUVDMACBUF_DR))
#define NEMUVDMACBUF_DR_SIZE(_cbData) (NEMUVDMACBUF_DR_DATA_OFFSET() + (_cbData))
#define NEMUVDMACBUF_DR_DATA(_pDr) ( ((uint8_t*)(_pDr)) + NEMUVDMACBUF_DR_DATA_OFFSET() )

AssertCompile(sizeof (NEMUVDMADDI_CMD) <= RT_SIZEOFMEMB(NEMUVDMACBUF_DR, aGuestData));
#define NEMUVDMADDI_CMD_FROM_BUF_DR(_pDr) ((PNEMUVDMADDI_CMD)(_pDr)->aGuestData)
#define NEMUVDMACBUF_DR_FROM_DDI_CMD(_pCmd) ((PNEMUVDMACBUF_DR)(((uint8_t*)(_pCmd)) - RT_OFFSETOF(NEMUVDMACBUF_DR, aGuestData)))

#endif
#ifdef NEMU_WITH_CROGL
NTSTATUS nemuVdmaPostHideSwapchain(PNEMUWDDM_SWAPCHAIN pSwapchain);
#endif

NTSTATUS nemuVdmaGgCmdDmaNotifyCompleted(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAPIPE_CMD_DMACMD pCmd, DXGK_INTERRUPT_TYPE enmComplType);
NTSTATUS nemuVdmaGgCmdDmaNotifySubmitted(PNEMUMP_DEVEXT pDevExt, PNEMUVDMAPIPE_CMD_DMACMD pCmd);
VOID nemuVdmaGgCmdDmaNotifyInit(PNEMUVDMAPIPE_CMD_DMACMD pCmd,
        uint32_t u32NodeOrdinal, uint32_t u32FenceId,
        PFNNEMUVDMADDICMDCOMPLETE_DPC pfnComplete, PVOID pvComplete);

NTSTATUS nemuVdmaGgDmaBltPerform(PNEMUMP_DEVEXT pDevExt, struct NEMUWDDM_ALLOC_DATA * pSrcAlloc, RECT* pSrcRect,
        struct NEMUWDDM_ALLOC_DATA *pDstAlloc, RECT* pDstRect);

#define NEMUVDMAPIPE_CMD_DR_FROM_DDI_CMD(_pCmd) ((PNEMUVDMAPIPE_CMD_DR)(((uint8_t*)(_pCmd)) - RT_OFFSETOF(NEMUVDMAPIPE_CMD_DR, DdiCmd)))

NTSTATUS nemuVdmaProcessBltCmd(PNEMUMP_DEVEXT pDevExt, struct NEMUWDDM_CONTEXT *pContext, struct NEMUWDDM_DMA_PRIVATEDATA_BLT *pBlt);
NTSTATUS nemuVdmaProcessFlipCmd(PNEMUMP_DEVEXT pDevExt, struct NEMUWDDM_CONTEXT *pContext, struct NEMUWDDM_DMA_PRIVATEDATA_FLIP *pFlip);
NTSTATUS nemuVdmaProcessClrFillCmd(PNEMUMP_DEVEXT pDevExt, struct NEMUWDDM_CONTEXT *pContext, struct NEMUWDDM_DMA_PRIVATEDATA_CLRFILL *pCF);
#ifdef NEMU_WITH_CROGL
NTSTATUS nemuVdmaTexPresentSetAlloc(PNEMUMP_DEVEXT pDevExt, const NEMUWDDM_ALLOC_DATA *pAllocData);
NTSTATUS NemuVdmaChromiumParameteriCRSubmit(PNEMUMP_DEVEXT pDevExt, uint32_t target, uint32_t value);
#endif

#endif /* #ifndef ___NemuMPVdma_h___ */
