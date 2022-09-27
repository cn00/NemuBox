/* $Id: NemuNetFltRt-win.cpp $ */
/** @file
 * NemuNetFltRt-win.cpp - Bridged Networking Driver, Windows Specific Code.
 * NetFlt Runtime
 */
/*
 * Copyright (C) 2011-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#include "NemuNetFltCmn-win.h"
#include <Nemu/intnetinline.h>
#include <iprt/thread.h>

RT_C_DECLS_BEGIN
#include <tdikrnl.h>
RT_C_DECLS_END
#include <mstcpip.h>

/** represents the job element of the job queue
 * see comments for NEMUNETFLT_JOB_QUEUE */
typedef struct NEMUNETFLT_JOB
{
    /** link in the job queue */
    LIST_ENTRY ListEntry;
    /** job function to be executed */
    PFNNEMUNETFLT_JOB_ROUTINE pfnRoutine;
    /** parameter to be passed to the job function */
    PVOID pContext;
    /** event that will be fired on job completion */
    KEVENT CompletionEvent;
    /** true if the job manager should use the completion even for completion indication, false-otherwise*/
    bool bUseCompletionEvent;
} NEMUNETFLT_JOB, *PNEMUNETFLT_JOB;

/**
 * represents the queue of jobs processed by the worker thread
 *
 * we use the thread to process tasks which are required to be done at passive level
 * our callbacks may be called at APC level by IntNet, there are some tasks that we can not create at APC,
 * e.g. thread creation. This is why we schedule such jobs to the worker thread working at passive level
 */
typedef struct NEMUNETFLT_JOB_QUEUE
{
    /* jobs */
    LIST_ENTRY Jobs;
    /* we are using ExInterlocked..List functions to access the jobs list */
    KSPIN_LOCK Lock;
    /** this event is used to initiate a job worker thread kill */
    KEVENT KillEvent;
    /** this event is used to notify a worker thread that jobs are added to the queue */
    KEVENT NotifyEvent;
    /** worker thread */
    PKTHREAD pThread;
} NEMUNETFLT_JOB_QUEUE, *PNEMUNETFLT_JOB_QUEUE;

typedef struct _CREATE_INSTANCE_CONTEXT
{
#ifndef NEMUNETADP
    PNDIS_STRING pOurName;
    PNDIS_STRING pBindToName;
#else
    NDIS_HANDLE hMiniportAdapter;
    NDIS_HANDLE hWrapperConfigurationContext;
#endif
    NDIS_STATUS Status;
}CREATE_INSTANCE_CONTEXT, *PCREATE_INSTANCE_CONTEXT;

/*contexts used for our jobs */
/* Attach context */
typedef struct _ATTACH_INFO
{
    PNEMUNETFLTINS pNetFltIf;
    PCREATE_INSTANCE_CONTEXT pCreateContext;
    bool fRediscovery;
    int Status;
}ATTACH_INFO, *PATTACH_INFO;

/* general worker context */
typedef struct _WORKER_INFO
{
    PNEMUNETFLTINS pNetFltIf;
    int Status;
}WORKER_INFO, *PWORKER_INFO;

/* idc initialization */
typedef struct _INIT_IDC_INFO
{
    NEMUNETFLT_JOB Job;
    bool bInitialized;
    volatile bool bStop;
    volatile int rc;
    KEVENT hCompletionEvent;
}INIT_IDC_INFO, *PINIT_IDC_INFO;


/** globals */
/** global job queue. some operations are required to be done at passive level, e.g. thread creation, adapter bind/unbind initiation,
 * while IntNet typically calls us APC_LEVEL, so we just create a system thread in our DriverEntry and enqueue the jobs to that thread */
static NEMUNETFLT_JOB_QUEUE g_NemuJobQueue;
volatile static bool g_bNemuIdcInitialized;
INIT_IDC_INFO g_NemuInitIdcInfo;
/**
 * The (common) global data.
 */
static NEMUNETFLTGLOBALS g_NemuNetFltGlobals;
/* win-specific global data */
NEMUNETFLTGLOBALS_WIN g_NemuNetFltGlobalsWin = {0};

#define LIST_ENTRY_2_JOB(pListEntry) \
    ( (PNEMUNETFLT_JOB)((uint8_t *)(pListEntry) - RT_OFFSETOF(NEMUNETFLT_JOB, ListEntry)) )

static int nemuNetFltWinAttachToInterface(PNEMUNETFLTINS pThis, void * pContext, bool fRediscovery);
static int nemuNetFltWinConnectIt(PNEMUNETFLTINS pThis);
static int nemuNetFltWinTryFiniIdc();
static void nemuNetFltWinFiniNetFltBase();
static int nemuNetFltWinInitNetFltBase();
static int nemuNetFltWinFiniNetFlt();
static int nemuNetFltWinStartInitIdcProbing();
static int nemuNetFltWinStopInitIdcProbing();

/** makes the current thread to sleep for the given number of miliseconds */
DECLHIDDEN(void) nemuNetFltWinSleep(ULONG milis)
{
    RTThreadSleep(milis);
}

/** wait for the given device to be dereferenced */
DECLHIDDEN(void) nemuNetFltWinWaitDereference(PNEMUNETFLT_WINIF_DEVICE pState)
{
#ifdef DEBUG
    uint64_t StartNanoTS = RTTimeSystemNanoTS();
    uint64_t CurNanoTS;
#endif
    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);

    while (ASMAtomicUoReadU32((volatile uint32_t *)&pState->cReferences))
    {
        nemuNetFltWinSleep(2);
#ifdef DEBUG
        CurNanoTS = RTTimeSystemNanoTS();
        if (CurNanoTS - StartNanoTS > 20000000)
        {
            LogRel(("device not idle"));
            AssertFailed();
//            break;
        }
#endif
    }
}

/**
 * mem functions
 */
/* allocates and zeroes the nonpaged memory of a given size */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMemAlloc(PVOID* ppMemBuf, UINT cbLength)
{
#ifdef DEBUG_NETFLT_USE_EXALLOC
    *ppMemBuf = ExAllocatePoolWithTag(NonPagedPool, cbLength, NEMUNETFLT_MEM_TAG);
    if (*ppMemBuf)
    {
        NdisZeroMemory(*ppMemBuf, cbLength);
        return NDIS_STATUS_SUCCESS;
    }
    return NDIS_STATUS_FAILURE;
#else
    NDIS_STATUS fStatus = NdisAllocateMemoryWithTag(ppMemBuf, cbLength, NEMUNETFLT_MEM_TAG);
    if (fStatus == NDIS_STATUS_SUCCESS)
    {
        NdisZeroMemory(*ppMemBuf, cbLength);
    }
    return fStatus;
#endif
}

/* frees memory allocated with nemuNetFltWinMemAlloc */
DECLHIDDEN(void) nemuNetFltWinMemFree(PVOID pvMemBuf)
{
#ifdef DEBUG_NETFLT_USE_EXALLOC
    ExFreePool(pvMemBuf);
#else
    NdisFreeMemory(pvMemBuf, 0, 0);
#endif
}

#ifndef NEMUNETFLT_NO_PACKET_QUEUE

/* initializes packet info pool and allocates the cSize packet infos for the pool */
static NDIS_STATUS nemuNetFltWinPpAllocatePacketInfoPool(PNEMUNETFLT_PACKET_INFO_POOL pPool, UINT cSize)
{
    UINT cbBufSize = sizeof(PACKET_INFO)*cSize;
    PACKET_INFO * pPacketInfos;
    NDIS_STATUS fStatus;
    UINT i;

    Assert(cSize > 0);

    INIT_INTERLOCKED_PACKET_QUEUE(&pPool->Queue);

    fStatus = nemuNetFltWinMemAlloc((PVOID*)&pPacketInfos, cbBufSize);

    if (fStatus == NDIS_STATUS_SUCCESS)
    {
        PNEMUNETFLTPACKET_INFO pInfo;
        pPool->pBuffer = pPacketInfos;

        for (i = 0; i < cSize; i++)
        {
            pInfo = &pPacketInfos[i];
            nemuNetFltWinQuEnqueueTail(&pPool->Queue.Queue, pInfo);
            pInfo->pPool = pPool;
        }
    }
    else
    {
        AssertFailed();
    }

    return fStatus;
}

/* frees the packet info pool */
VOID nemuNetFltWinPpFreePacketInfoPool(PNEMUNETFLT_PACKET_INFO_POOL pPool)
{
    nemuNetFltWinMemFree(pPool->pBuffer);

    FINI_INTERLOCKED_PACKET_QUEUE(&pPool->Queue)
}

#endif

/**
 * copies one string to another. in case the destination string size is not enough to hold the complete source string
 * does nothing and returns NDIS_STATUS_RESOURCES .
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinCopyString(PNDIS_STRING pDst, PNDIS_STRING pSrc)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    if (pDst != pSrc)
    {
        if (pDst->MaximumLength < pSrc->Length)
        {
            AssertFailed();
            Status = NDIS_STATUS_RESOURCES;
        }
        else
        {
            pDst->Length = pSrc->Length;

            if (pDst->Buffer != pSrc->Buffer)
            {
                NdisMoveMemory(pDst->Buffer, pSrc->Buffer, pSrc->Length);
            }
        }
    }
    return Status;
}

/************************************************************************************
 * PINTNETSG pSG manipulation functions
 ************************************************************************************/

/* moves the contents of the given NDIS_BUFFER and all other buffers chained to it to the PINTNETSG
 * the PINTNETSG is expected to contain one segment whose bugger is large enough to maintain
 * the contents of the given NDIS_BUFFER and all other buffers chained to it */
static NDIS_STATUS nemuNetFltWinNdisBufferMoveToSG0(PNDIS_BUFFER pBuffer, PINTNETSG pSG)
{
    UINT cSegs = 0;
    PINTNETSEG paSeg;
    uint8_t * ptr;
    PVOID pVirtualAddress;
    UINT cbCurrentLength;
    NDIS_STATUS fStatus = NDIS_STATUS_SUCCESS;

    Assert(pSG->cSegsAlloc == 1);

    paSeg = pSG->aSegs;
    ptr = (uint8_t*)paSeg->pv;
    paSeg->cb = 0;
    paSeg->Phys = NIL_RTHCPHYS;
    pSG->cbTotal = 0;

    Assert(paSeg->pv);

    while (pBuffer)
    {
        NdisQueryBufferSafe(pBuffer, &pVirtualAddress, &cbCurrentLength, NormalPagePriority);

        if (!pVirtualAddress)
        {
            fStatus = NDIS_STATUS_FAILURE;
            break;
        }

        pSG->cbTotal += cbCurrentLength;
        paSeg->cb += cbCurrentLength;
        NdisMoveMemory(ptr, pVirtualAddress, cbCurrentLength);
        ptr += cbCurrentLength;

        NdisGetNextBuffer(pBuffer, &pBuffer);
    }

    if (fStatus == NDIS_STATUS_SUCCESS)
    {
        pSG->cSegsUsed = 1;
        Assert(pSG->cbTotal == paSeg->cb);
    }
    return fStatus;
}

/* converts the PNDIS_BUFFER to PINTNETSG by making the PINTNETSG segments to point to the memory buffers the
 * ndis buffer(s) point to (as opposed to nemuNetFltWinNdisBufferMoveToSG0 which copies the memory from ndis buffers(s) to PINTNETSG) */
static NDIS_STATUS nemuNetFltWinNdisBuffersToSG(PNDIS_BUFFER pBuffer, PINTNETSG pSG)
{
    UINT cSegs = 0;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    PVOID pVirtualAddress;
    UINT cbCurrentLength;

    while (pBuffer)
    {
        NdisQueryBufferSafe(pBuffer, &pVirtualAddress, &cbCurrentLength, NormalPagePriority);

        if (!pVirtualAddress)
        {
            Status = NDIS_STATUS_FAILURE;
            break;
        }

        pSG->cbTotal += cbCurrentLength;
        pSG->aSegs[cSegs].cb = cbCurrentLength;
        pSG->aSegs[cSegs].pv = pVirtualAddress;
        pSG->aSegs[cSegs].Phys = NIL_RTHCPHYS;
        cSegs++;

        NdisGetNextBuffer(pBuffer, &pBuffer);
    }

    AssertFatal(cSegs <= pSG->cSegsAlloc);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        pSG->cSegsUsed = cSegs;
    }

    return Status;
}

static void nemuNetFltWinDeleteSG(PINTNETSG pSG)
{
    nemuNetFltWinMemFree(pSG);
}

static PINTNETSG nemuNetFltWinCreateSG(uint32_t cSegs)
{
    PINTNETSG pSG;
    NTSTATUS Status = nemuNetFltWinMemAlloc((PVOID*)&pSG, RT_OFFSETOF(INTNETSG, aSegs[cSegs]));
    if (Status == STATUS_SUCCESS)
    {
        IntNetSgInitTempSegs(pSG, 0 /*cbTotal*/, cSegs, 0 /*cSegsUsed*/);
        return pSG;
    }

    return NULL;
}

/************************************************************************************
 * packet queue functions
 ************************************************************************************/
#ifndef NEMUNETFLT_NO_PACKET_QUEUE
#if !defined(NEMUNETADP)
static NDIS_STATUS nemuNetFltWinQuPostPacket(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket, PINTNETSG pSG, uint32_t fFlags
# ifdef DEBUG_NETFLT_PACKETS
        , PNDIS_PACKET pTmpPacket
# endif
        )
{
    NDIS_STATUS Status;
    PNDIS_PACKET pMyPacket;
    bool bSrcHost = fFlags & PACKET_SRC_HOST;

    LogFlow(("posting packet back to driver stack..\n"));

    if (!pPacket)
    {
        /* INTNETSG was in the packet queue, create a new NdisPacket from INTNETSG*/
        pMyPacket = nemuNetFltWinNdisPacketFromSG(pNetFlt,
                pSG, /* PINTNETSG */
                pSG, /* PVOID pBufToFree */
                bSrcHost, /* bool bToWire */
                false); /* bool bCopyMemory */

        Assert(pMyPacket);

        NDIS_SET_PACKET_STATUS(pMyPacket, NDIS_STATUS_SUCCESS);

        DBG_CHECK_PACKET_AND_SG(pMyPacket, pSG);

#ifdef DEBUG_NETFLT_PACKETS
        Assert(pTmpPacket);

        DBG_CHECK_PACKET_AND_SG(pTmpPacket, pSG);

        DBG_CHECK_PACKETS(pTmpPacket, pMyPacket);
#endif

        LogFlow(("non-ndis packet info, packet created (%p)\n", pMyPacket));
    }
    else
    {
        /* NDIS_PACKET was in the packet queue */
        DBG_CHECK_PACKET_AND_SG(pPacket, pSG);

        if (!(fFlags & PACKET_MINE))
        {
            /* the packet is the one that was passed to us in send/receive callback
             * According to the DDK, we can not post it further,
             * instead we should allocate our own packet.
             * So, allocate our own packet (pMyPacket) and copy the packet info there */
            if (bSrcHost)
            {
                Status = nemuNetFltWinPrepareSendPacket(pNetFlt, pPacket, &pMyPacket/*, true*/);
                LogFlow(("packet from wire, packet created (%p)\n", pMyPacket));
            }
            else
            {
                Status = nemuNetFltWinPrepareRecvPacket(pNetFlt, pPacket, &pMyPacket, false);
                LogFlow(("packet from wire, packet created (%p)\n", pMyPacket));
            }
        }
        else
        {
            /* the packet enqueued is ours, simply assign pMyPacket and zero pPacket */
            pMyPacket = pPacket;
            pPacket = NULL;
        }
        Assert(pMyPacket);
    }

    if (pMyPacket)
    {
        /* we have successfully initialized our packet, post it to the host or to the wire */
        if (bSrcHost)
        {
#if defined(DEBUG_NETFLT_PACKETS) || !defined(NEMU_LOOPBACK_USEFLAGS)
            nemuNetFltWinLbPutSendPacket(pNetFlt, pMyPacket, false /* bFromIntNet */);
#endif
            NdisSend(&Status, pNetFlt->u.s.hBinding, pMyPacket);

            if (Status != NDIS_STATUS_PENDING)
            {
#if defined(DEBUG_NETFLT_PACKETS) || !defined(NEMU_LOOPBACK_USEFLAGS)
                /* the status is NOT pending, complete the packet */
                bool bTmp = nemuNetFltWinLbRemoveSendPacket(pNetFlt, pMyPacket);
                Assert(bTmp);
#endif
                if (pPacket)
                {
                    LogFlow(("status is not pending, completing packet (%p)\n", pPacket));

                    NdisIMCopySendCompletePerPacketInfo (pPacket, pMyPacket);

                    NdisFreePacket(pMyPacket);
                }
                else
                {
                    /* should never be here since the PINTNETSG is stored only when the underlying miniport
                     * indicates NDIS_STATUS_RESOURCES, we should never have this when processing
                     * the "from-host" packets */
                    AssertFailed();
                    LogFlow(("status is not pending, freeing myPacket (%p)\n", pMyPacket));
                    nemuNetFltWinFreeSGNdisPacket(pMyPacket, false);
                }
            }
        }
        else
        {
            NdisMIndicateReceivePacket(pNetFlt->u.s.hMiniport, &pMyPacket, 1);

            Status = NDIS_STATUS_PENDING;
            /* the packet receive completion is always indicated via MiniportReturnPacket */
        }
    }
    else
    {
        /*we failed to create our packet */
        AssertFailed();
        Status = NDIS_STATUS_FAILURE;
    }

    return Status;
}
#endif

static bool nemuNetFltWinQuProcessInfo(PNEMUNETFLTINS pNetFltIf, PPACKET_QUEUE_WORKER pWorker, PVOID pvPacket, const UINT fFlags)
#else
DECLHIDDEN(bool) nemuNetFltWinPostIntnet(PNEMUNETFLTINS pNetFltIf, PVOID pvPacket, const UINT fFlags)
#endif
{
    PNDIS_PACKET pPacket = NULL;
    PINTNETSG pSG = NULL;
    NDIS_STATUS Status;
#ifndef NEMUNETADP
    bool bSrcHost;
    bool bDropIt;
# ifndef NEMUNETFLT_NO_PACKET_QUEUE
    bool bPending;
# endif
#endif
#ifdef NEMUNETFLT_NO_PACKET_QUEUE
    bool bDeleteSG = false;
#endif
#ifdef DEBUG_NETFLT_PACKETS
    /* packet used for matching */
    PNDIS_PACKET pTmpPacket = NULL;
#endif

#ifndef NEMUNETADP
    bSrcHost = (fFlags & NEMUNETFLT_PACKET_SRC_HOST) != 0;
#endif

    /* we first need to obtain the INTNETSG to be passed to intnet */

    /* the queue may contain two "types" of packets:
     * the NDIS_PACKET and the INTNETSG.
     * I.e. on send/receive we typically enqueue the NDIS_PACKET passed to us by ndis,
     * however in case our ProtocolReceive is called or the packet's status is set to NDIS_STSTUS_RESOURCES
     * in ProtocolReceivePacket, we must return the packet immediately on ProtocolReceive*** exit
     * In this case we allocate the INTNETSG, copy the ndis packet data there and enqueue it.
     * In this case the packet info flags has the NEMUNETFLT_PACKET_SG fag set
     *
     * Besides that the NDIS_PACKET contained in the queue could be either the one passed to us in our send/receive callback
     * or the one created by us. The latter is possible in case our ProtocolReceive callback is called and we call NdisTransferData
     * in this case we need to allocate the packet the data to be transferred to.
     * If the enqueued packet is the one allocated by us the NEMUNETFLT_PACKET_MINE flag is set
     * */
    if ((fFlags & NEMUNETFLT_PACKET_SG) == 0)
    {
        /* we have NDIS_PACKET enqueued, we need to convert it to INTNETSG to be passed to intnet */
        PNDIS_BUFFER pCurrentBuffer = NULL;
        UINT cBufferCount;
        UINT uBytesCopied = 0;
        UINT cbPacketLength;

        pPacket = (PNDIS_PACKET)pvPacket;

        LogFlow(("ndis packet info, packet (%p)\n", pPacket));

        LogFlow(("preparing pSG"));
        NdisQueryPacket(pPacket, NULL, &cBufferCount, &pCurrentBuffer, &cbPacketLength);
        Assert(cBufferCount);

#ifdef NEMUNETFLT_NO_PACKET_QUEUE
        pSG = nemuNetFltWinCreateSG(cBufferCount);
#else
        /* we can not allocate the INTNETSG on stack since in this case we may get stack overflow
         * somewhere outside of our driver (3 pages of system thread stack does not seem to be enough)
         *
         * since we have a "serialized" packet processing, i.e. all packets are being processed and passed
         * to intnet by this thread, we just use one previously allocated INTNETSG which is stored in PNEMUNETFLTINS */
        pSG = pWorker->pSG;

        if (cBufferCount > pSG->cSegsAlloc)
        {
            pSG = nemuNetFltWinCreateSG(cBufferCount + 2);
            if (pSG)
            {
                nemuNetFltWinDeleteSG(pWorker->pSG);
                pWorker->pSG = pSG;
            }
            else
            {
                LogRel(("Failed to reallocate the pSG\n"));
            }
        }
#endif

        if (pSG)
        {
#ifdef NEMUNETFLT_NO_PACKET_QUEUE
            bDeleteSG = true;
#endif
            /* reinitialize */
            IntNetSgInitTempSegs(pSG, 0 /*cbTotal*/, pSG->cSegsAlloc, 0 /*cSegsUsed*/);

            /* convert the ndis buffers to INTNETSG */
            Status = nemuNetFltWinNdisBuffersToSG(pCurrentBuffer, pSG);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                pSG = NULL;
            }
            else
            {
                DBG_CHECK_PACKET_AND_SG(pPacket, pSG);
            }
        }
    }
    else
    {
        /* we have the INTNETSG enqueued. (see the above comment explaining why/when this may happen)
         * just use the INTNETSG to pass it to intnet */
#ifndef NEMUNETADP
        /* the PINTNETSG is stored only when the underlying miniport
         * indicates NDIS_STATUS_RESOURCES, we should never have this when processing
         * the "from-host" packedts */
        Assert(!bSrcHost);
#endif
        pSG = (PINTNETSG)pvPacket;

        LogFlow(("not ndis packet info, pSG (%p)\n", pSG));
    }

#ifdef DEBUG_NETFLT_PACKETS
    if (!pPacket && !pTmpPacket)
    {
        /* create tmp packet that woud be used for matching */
        pTmpPacket = nemuNetFltWinNdisPacketFromSG(pNetFltIf,
                    pSG, /* PINTNETSG */
                    pSG, /* PVOID pBufToFree */
                    bSrcHost, /* bool bToWire */
                    true); /* bool bCopyMemory */

        NDIS_SET_PACKET_STATUS(pTmpPacket, NDIS_STATUS_SUCCESS);

        DBG_CHECK_PACKET_AND_SG(pTmpPacket, pSG);

        Assert(pTmpPacket);
    }
#endif
    do
    {
#ifndef NEMUNETADP
        /* the pSG was successfully initialized, post it to the netFlt*/
        bDropIt = pSG ? pNetFltIf->pSwitchPort->pfnRecv(pNetFltIf->pSwitchPort, NULL /* pvIf */, pSG,
                    bSrcHost ? INTNETTRUNKDIR_HOST : INTNETTRUNKDIR_WIRE
                            )
              : false;
#else
        if (pSG)
        {
            pNetFltIf->pSwitchPort->pfnRecv(pNetFltIf->pSwitchPort, NULL /* pvIf */, pSG, INTNETTRUNKDIR_HOST);
            STATISTIC_INCREASE(pNetFltIf->u.s.WinIf.cTxSuccess);
        }
        else
        {
            STATISTIC_INCREASE(pNetFltIf->u.s.WinIf.cTxError);
        }
#endif

#ifndef NEMUNETFLT_NO_PACKET_QUEUE

# if !defined(NEMUNETADP)
        if (!bDropIt)
        {
            Status = nemuNetFltWinQuPostPacket(pNetFltIf, pPacket, pSG, fFlags
#  ifdef DEBUG_NETFLT_PACKETS
                               , pTmpPacket
#  endif
            );

            if (Status == NDIS_STATUS_PENDING)
            {
                /* we will process packet completion in the completion routine */
                bPending = true;
                break;
            }
        }
        else
# endif
        {
            Status = NDIS_STATUS_SUCCESS;
        }

        /* drop it */
        if (pPacket)
        {
            if (!(fFlags & PACKET_MINE))
            {
# if !defined(NEMUNETADP)
                /* complete the packets */
                if (fFlags & PACKET_SRC_HOST)
                {
# endif
/*                    NDIS_SET_PACKET_STATUS(pPacket, Status); */
                    NdisMSendComplete(pNetFltIf->u.s.hMiniport, pPacket, Status);
# if !defined(NEMUNETADP)
                }
                else
                {
# endif
# ifndef NEMUNETADP
                    NdisReturnPackets(&pPacket, 1);
# endif
# if !defined(NEMUNETADP)
                }
# endif
            }
            else
            {
                Assert(!(fFlags & PACKET_SRC_HOST));
                nemuNetFltWinFreeSGNdisPacket(pPacket, true);
            }
        }
        else
        {
            Assert(pSG);
            nemuNetFltWinMemFree(pSG);
        }
# ifndef NEMUNETADP
        bPending = false;
# endif
    } while (0);

#ifdef DEBUG_NETFLT_PACKETS
    if (pTmpPacket)
    {
        nemuNetFltWinFreeSGNdisPacket(pTmpPacket, true);
    }
#endif

#ifndef NEMUNETADP
    return bPending;
#else
    return false;
#endif
#else /* #ifdef NEMUNETFLT_NO_PACKET_QUEUE */
    } while (0);

    if (bDeleteSG)
        nemuNetFltWinMemFree(pSG);

# ifndef NEMUNETADP
    return bDropIt;
# else
    return true;
# endif
#endif
}
#ifndef NEMUNETFLT_NO_PACKET_QUEUE
/*
 * thread start function for the thread which processes the packets enqueued in our send and receive callbacks called by ndis
 *
 * ndis calls us at DISPATCH_LEVEL, while IntNet is using kernel functions which require Irql<DISPATCH_LEVEL
 * this is why we can not immediately post packets to IntNet from our sen/receive callbacks
 * instead we put the incoming packets to the queue and maintain the system thread running at passive level
 * which processes the queue and posts the packets to IntNet, and further to the host or to the wire.
 */
static VOID nemuNetFltWinQuPacketQueueWorkerThreadProc(PNEMUNETFLTINS pNetFltIf)
{
    bool fResume = true;
    NTSTATUS fStatus;
    PPACKET_QUEUE_WORKER pWorker = &pNetFltIf->u.s.PacketQueueWorker;

    PVOID apEvents[] = {
        (PVOID)&pWorker->KillEvent,
        (PVOID)&pWorker->NotifyEvent
    };

    while (fResume)
    {
        uint32_t cNumProcessed;
        uint32_t cNumPostedToHostWire;

        fStatus = KeWaitForMultipleObjects(RT_ELEMENTS(apEvents), apEvents, WaitAny, Executive, KernelMode, FALSE, NULL, NULL);
        if (!NT_SUCCESS(fStatus) || fStatus == STATUS_WAIT_0)
        {
            /* "kill" event was set
             * will process queued packets and exit */
            fResume = false;
        }

        LogFlow(("processing nemuNetFltWinQuPacketQueueWorkerThreadProc\n"));

        cNumProcessed = 0;
        cNumPostedToHostWire = 0;

        do
        {
            PNEMUNETFLTPACKET_INFO pInfo;

#ifdef DEBUG_NETFLT_PACKETS
            /* packet used for matching */
            PNDIS_PACKET pTmpPacket = NULL;
#endif

            /*TODO: FIXME: !!! the better approach for performance would be to dequeue all packets at once
             * and then go through all dequeued packets
             * the same should be done for enqueue !!! */
            pInfo = nemuNetFltWinQuInterlockedDequeueHead(&pWorker->PacketQueue);

            if (!pInfo)
            {
                break;
            }

            LogFlow(("found info (0x%p)\n", pInfo));

            if (nemuNetFltWinQuProcessInfo(pNetFltIf, pWorker, pInfo->pPacket, pInfo->fFlags))
            {
                cNumPostedToHostWire++;
            }

            nemuNetFltWinPpFreePacketInfo(pInfo);

            cNumProcessed++;
        } while (TRUE);

        if (cNumProcessed)
        {
            nemuNetFltWinDecReferenceNetFlt(pNetFltIf, cNumProcessed);

            Assert(cNumProcessed >= cNumPostedToHostWire);

            if (cNumProcessed != cNumPostedToHostWire)
            {
                nemuNetFltWinDecReferenceWinIf(pNetFltIf, cNumProcessed - cNumPostedToHostWire);
            }
        }
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}
#endif
/**
 * thread start function for the job processing thread
 *
 * see comments for PNEMUNETFLT_JOB_QUEUE
 */
static VOID nemuNetFltWinJobWorkerThreadProc(PNEMUNETFLT_JOB_QUEUE pQueue)
{
    bool fResume = true;
    NTSTATUS Status;

    PVOID apEvents[] = {
        (PVOID)&pQueue->KillEvent,
        (PVOID)&pQueue->NotifyEvent,
    };

    do
    {
        Status = KeWaitForMultipleObjects(RT_ELEMENTS(apEvents), apEvents, WaitAny, Executive, KernelMode, FALSE, NULL, NULL);
        Assert(NT_SUCCESS(Status));
        if (!NT_SUCCESS(Status) || Status == STATUS_WAIT_0)
        {
            /* will process queued jobs and exit */
            Assert(Status == STATUS_WAIT_0);
            fResume = false;
        }

        do
        {
            PLIST_ENTRY pJobEntry = ExInterlockedRemoveHeadList(&pQueue->Jobs, &pQueue->Lock);
            PNEMUNETFLT_JOB pJob;

            if (!pJobEntry)
                break;

            pJob = LIST_ENTRY_2_JOB(pJobEntry);

            Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
            pJob->pfnRoutine(pJob->pContext);
            Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

            if (pJob->bUseCompletionEvent)
            {
                KeSetEvent(&pJob->CompletionEvent, 1, FALSE);
            }
        } while (TRUE);
    } while (fResume);

    Assert(Status == STATUS_WAIT_0);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

/**
 * enqueues the job to the job queue to be processed by the job worker thread
 * see comments for PNEMUNETFLT_JOB_QUEUE
 */
static VOID nemuNetFltWinJobEnqueueJob(PNEMUNETFLT_JOB_QUEUE pQueue, PNEMUNETFLT_JOB pJob, bool bEnqueueHead)
{
    if (bEnqueueHead)
    {
        ExInterlockedInsertHeadList(&pQueue->Jobs, &pJob->ListEntry, &pQueue->Lock);
    }
    else
    {
        ExInterlockedInsertTailList(&pQueue->Jobs, &pJob->ListEntry, &pQueue->Lock);
    }

    KeSetEvent(&pQueue->NotifyEvent, 1, FALSE);
}

DECLINLINE(VOID) nemuNetFltWinJobInit(PNEMUNETFLT_JOB pJob, PFNNEMUNETFLT_JOB_ROUTINE pfnRoutine, PVOID pContext, bool bUseEvent)
{
    pJob->pfnRoutine = pfnRoutine;
    pJob->pContext = pContext;
    pJob->bUseCompletionEvent = bUseEvent;
    if (bUseEvent)
        KeInitializeEvent(&pJob->CompletionEvent, NotificationEvent, FALSE);
}

/**
 * enqueues the job to the job queue to be processed by the job worker thread and
 * blocks until the job is done
 * see comments for PNEMUNETFLT_JOB_QUEUE
 */
static VOID nemuNetFltWinJobSynchExec(PNEMUNETFLT_JOB_QUEUE pQueue, PFNNEMUNETFLT_JOB_ROUTINE pfnRoutine, PVOID pContext)
{
    NEMUNETFLT_JOB Job;

    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);

    nemuNetFltWinJobInit(&Job, pfnRoutine, pContext, true);

    nemuNetFltWinJobEnqueueJob(pQueue, &Job, false);

    KeWaitForSingleObject(&Job.CompletionEvent, Executive, KernelMode, FALSE, NULL);
}

/**
 * enqueues the job to be processed by the job worker thread at passive level and
 * blocks until the job is done
 */
DECLHIDDEN(VOID) nemuNetFltWinJobSynchExecAtPassive(PFNNEMUNETFLT_JOB_ROUTINE pfnRoutine, PVOID pContext)
{
    nemuNetFltWinJobSynchExec(&g_NemuJobQueue, pfnRoutine, pContext);
}

/**
 * helper function used for system thread creation
 */
static NTSTATUS nemuNetFltWinQuCreateSystemThread(PKTHREAD *ppThread, PKSTART_ROUTINE pfnStartRoutine, PVOID pvStartContext)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    HANDLE hThread;
    NTSTATUS Status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, (PKSTART_ROUTINE)pfnStartRoutine, pvStartContext);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Status = ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID*)ppThread, NULL);
        Assert(Status == STATUS_SUCCESS);
        ZwClose(hThread);
        if (Status == STATUS_SUCCESS)
        {
            return STATUS_SUCCESS;
        }

        /* @todo: how would we fail in this case ?*/
    }
    return Status;
}

/**
 * initialize the job queue
 * see comments for PNEMUNETFLT_JOB_QUEUE
 */
static NTSTATUS nemuNetFltWinJobInitQueue(PNEMUNETFLT_JOB_QUEUE pQueue)
{
    NTSTATUS fStatus;

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    NdisZeroMemory(pQueue, sizeof(NEMUNETFLT_JOB_QUEUE));

    KeInitializeEvent(&pQueue->KillEvent, NotificationEvent, FALSE);

    KeInitializeEvent(&pQueue->NotifyEvent, SynchronizationEvent, FALSE);

    InitializeListHead(&pQueue->Jobs);

    fStatus = nemuNetFltWinQuCreateSystemThread(&pQueue->pThread, (PKSTART_ROUTINE)nemuNetFltWinJobWorkerThreadProc, pQueue);
    if (fStatus != STATUS_SUCCESS)
    {
        pQueue->pThread = NULL;
    }
    else
    {
        Assert(pQueue->pThread);
    }

    return fStatus;
}

/**
 * deinitialize the job queue
 * see comments for PNEMUNETFLT_JOB_QUEUE
 */
static void nemuNetFltWinJobFiniQueue(PNEMUNETFLT_JOB_QUEUE pQueue)
{
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (pQueue->pThread)
    {
        KeSetEvent(&pQueue->KillEvent, 0, FALSE);

        KeWaitForSingleObject(pQueue->pThread, Executive,
                            KernelMode, FALSE, NULL);
    }
}

#ifndef NEMUNETFLT_NO_PACKET_QUEUE

/**
 * initializes the packet queue
 * */
DECLHIDDEN(NTSTATUS) nemuNetFltWinQuInitPacketQueue(PNEMUNETFLTINS pInstance)
{
    NTSTATUS Status;
    PPACKET_QUEUE_WORKER pWorker = &pInstance->u.s.PacketQueueWorker;

    AssertFatal(!pWorker->pSG);

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    KeInitializeEvent(&pWorker->KillEvent, NotificationEvent, FALSE);

    KeInitializeEvent(&pWorker->NotifyEvent, SynchronizationEvent, FALSE);

    INIT_INTERLOCKED_PACKET_QUEUE(&pWorker->PacketQueue);

    do
    {
    Status = nemuNetFltWinPpAllocatePacketInfoPool(&pWorker->PacketInfoPool, NEMUNETFLT_PACKET_INFO_POOL_SIZE);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        pWorker->pSG = nemuNetFltWinCreateSG(PACKET_QUEUE_SG_SEGS_ALLOC);
        if (!pWorker->pSG)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = nemuNetFltWinQuCreateSystemThread(&pWorker->pThread, (PKSTART_ROUTINE)nemuNetFltWinQuPacketQueueWorkerThreadProc, pInstance);
        if (Status != STATUS_SUCCESS)
        {
            nemuNetFltWinPpFreePacketInfoPool(&pWorker->PacketInfoPool);
            nemuNetFltWinMemFree(pWorker->pSG);
            pWorker->pSG = NULL;
            break;
        }
    }

    } while (0);

    return Status;
}

/*
 * deletes the packet queue
 */
DECLHIDDEN(void) nemuNetFltWinQuFiniPacketQueue(PNEMUNETFLTINS pInstance)
{
    PINTNETSG pSG;
    PPACKET_QUEUE_WORKER pWorker = &pInstance->u.s.PacketQueueWorker;
    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);

    /* using the pPacketQueueSG as an indicator that the packet queue is initialized */
    RTSpinlockAcquire((pInstance)->hSpinlock);
    if (pWorker->pSG)
    {
        pSG = pWorker->pSG;
        pWorker->pSG = NULL;
        RTSpinlockRelease((pInstance)->hSpinlock);
        KeSetEvent(&pWorker->KillEvent, 0, FALSE);

        KeWaitForSingleObject(pWorker->pThread, Executive,
                            KernelMode, FALSE, NULL);

        nemuNetFltWinPpFreePacketInfoPool(&pWorker->PacketInfoPool);

        nemuNetFltWinDeleteSG(pSG);

        FINI_INTERLOCKED_PACKET_QUEUE(&pWorker->PacketQueue);
    }
    else
    {
        RTSpinlockRelease((pInstance)->hSpinlock);
    }
}

#endif

/*
 * creates the INTNETSG containing one segment pointing to the buffer of size cbBufSize
 * the INTNETSG created should be cleaned with nemuNetFltWinMemFree
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinAllocSG(UINT cbPacket, PINTNETSG *ppSG)
{
    NDIS_STATUS Status;
    PINTNETSG pSG;

    /* allocation:
     * 1. SG_PACKET - with one aSegs pointing to
     * 2. buffer of cbPacket containing the entire packet */
    AssertCompileSizeAlignment(INTNETSG, sizeof(PVOID));
    Status = nemuNetFltWinMemAlloc((PVOID*)&pSG, cbPacket + sizeof(INTNETSG));
    if (Status == NDIS_STATUS_SUCCESS)
    {
        IntNetSgInitTemp(pSG, pSG + 1, cbPacket);
        LogFlow(("pSG created (%p)\n", pSG));
        *ppSG = pSG;
    }
    return Status;
}

#ifndef NEMUNETFLT_NO_PACKET_QUEUE
/**
 * put the packet info to the queue
 */
DECLINLINE(void) nemuNetFltWinQuEnqueueInfo(PNEMUNETFLTPACKET_QUEUE_WORKER pWorker, PNEMUNETFLTPACKET_INFO pInfo)
{
    nemuNetFltWinQuInterlockedEnqueueTail(&pWorker->PacketQueue, pInfo);

    KeSetEvent(&pWorker->NotifyEvent, IO_NETWORK_INCREMENT, FALSE);
}

/**
 * puts the packet to the queue
 *
 * @return NDIST_STATUS_SUCCESS iff the packet was enqueued successfully
 * and error status otherwise.
 * NOTE: that the success status does NOT mean that the packet processing is completed, but only that it was enqueued successfully
 * the packet can be returned to the caller protocol/moniport only in case the bReleasePacket was set to true (in this case the copy of the packet was enqueued)
 * or if nemuNetFltWinQuEnqueuePacket failed, i.e. the packet was NOT enqueued
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinQuEnqueuePacket(PNEMUNETFLTINS pInstance, PVOID pPacket, const UINT fPacketFlags)
{
    PNEMUNETFLT_PACKET_INFO pInfo;
    PNEMUNETFLT_PACKET_QUEUE_WORKER pWorker = &pInstance->u.s.PacketQueueWorker;
    NDIS_STATUS fStatus = NDIS_STATUS_SUCCESS;

    do
    {
        if (fPacketFlags & PACKET_COPY)
        {
            PNDIS_BUFFER pBuffer = NULL;
            UINT cBufferCount;
            UINT uBytesCopied = 0;
            UINT cbPacketLength;
            PINTNETSG pSG;

            /* the packet is Ndis packet */
            Assert(!(fPacketFlags & PACKET_SG));
            Assert(!(fPacketFlags & PACKET_MINE));

            NdisQueryPacket((PNDIS_PACKET)pPacket,
                    NULL,
                    &cBufferCount,
                    &pBuffer,
                    &cbPacketLength);


            Assert(cBufferCount);

            fStatus = nemuNetFltWinAllocSG(cbPacketLength, &pSG);
            if (fStatus != NDIS_STATUS_SUCCESS)
            {
                AssertFailed();
                break;
            }

            pInfo = nemuNetFltWinPpAllocPacketInfo(&pWorker->PacketInfoPool);

            if (!pInfo)
            {
                AssertFailed();
                /* TODO: what status to set? */
                fStatus = NDIS_STATUS_FAILURE;
                nemuNetFltWinMemFree(pSG);
                break;
            }

            Assert(pInfo->pPool);

            /* the packet we are queueing is SG, add PACKET_SG to flags */
            SET_FLAGS_TO_INFO(pInfo, fPacketFlags | PACKET_SG);
            SET_PACKET_TO_INFO(pInfo, pSG);

            fStatus = nemuNetFltWinNdisBufferMoveToSG0(pBuffer, pSG);
            if (fStatus != NDIS_STATUS_SUCCESS)
            {
                AssertFailed();
                nemuNetFltWinPpFreePacketInfo(pInfo);
                nemuNetFltWinMemFree(pSG);
                break;
            }

            DBG_CHECK_PACKET_AND_SG((PNDIS_PACKET)pPacket, pSG);
        }
        else
        {
            pInfo = nemuNetFltWinPpAllocPacketInfo(&pWorker->PacketInfoPool);

            if (!pInfo)
            {
                AssertFailed();
                /* TODO: what status to set? */
                fStatus = NDIS_STATUS_FAILURE;
                break;
            }

            Assert(pInfo->pPool);

            SET_FLAGS_TO_INFO(pInfo, fPacketFlags);
            SET_PACKET_TO_INFO(pInfo, pPacket);
        }

        nemuNetFltWinQuEnqueueInfo(pWorker, pInfo);

    } while (0);

    return fStatus;
}
#endif


/*
 * netflt
 */
#ifndef NEMUNETADP
static NDIS_STATUS nemuNetFltWinSynchNdisRequest(PNEMUNETFLTINS pNetFlt, PNDIS_REQUEST pRequest)
{
    int rc;

    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);

    /* 1. serialize */
    rc = RTSemFastMutexRequest(pNetFlt->u.s.WinIf.hSynchRequestMutex); AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        NDIS_STATUS fRequestStatus = NDIS_STATUS_SUCCESS;

        /* 2. set pNetFlt->u.s.pSynchRequest */
        Assert(!pNetFlt->u.s.WinIf.pSynchRequest);
        pNetFlt->u.s.WinIf.pSynchRequest = pRequest;

        /* 3. call NdisRequest */
        NdisRequest(&fRequestStatus, pNetFlt->u.s.WinIf.hBinding, pRequest);

        if (fRequestStatus == NDIS_STATUS_PENDING)
        {
        /* 3.1 if pending wait and assign the resulting status */
            KeWaitForSingleObject(&pNetFlt->u.s.WinIf.hSynchCompletionEvent, Executive,
                            KernelMode, FALSE, NULL);

            fRequestStatus = pNetFlt->u.s.WinIf.SynchCompletionStatus;
        }

        /* 4. clear the pNetFlt->u.s.pSynchRequest */
        pNetFlt->u.s.WinIf.pSynchRequest = NULL;

        RTSemFastMutexRelease(pNetFlt->u.s.WinIf.hSynchRequestMutex); AssertRC(rc);
        return fRequestStatus;
    }
    return NDIS_STATUS_FAILURE;
}


DECLHIDDEN(NDIS_STATUS) nemuNetFltWinGetMacAddress(PNEMUNETFLTINS pNetFlt, PRTMAC pMac)
{
    NDIS_REQUEST request;
    NDIS_STATUS status;
    request.RequestType = NdisRequestQueryInformation;
    request.DATA.QUERY_INFORMATION.InformationBuffer = pMac;
    request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(RTMAC);
    request.DATA.QUERY_INFORMATION.Oid = OID_802_3_CURRENT_ADDRESS;
    status = nemuNetFltWinSynchNdisRequest(pNetFlt, &request);
    if (status != NDIS_STATUS_SUCCESS)
    {
        /* TODO */
        AssertFailed();
    }

    return status;

}

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinQueryPhysicalMedium(PNEMUNETFLTINS pNetFlt, NDIS_PHYSICAL_MEDIUM * pMedium)
{
    NDIS_REQUEST Request;
    NDIS_STATUS Status;
    Request.RequestType = NdisRequestQueryInformation;
    Request.DATA.QUERY_INFORMATION.InformationBuffer = pMedium;
    Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(NDIS_PHYSICAL_MEDIUM);
    Request.DATA.QUERY_INFORMATION.Oid = OID_GEN_PHYSICAL_MEDIUM;
    Status = nemuNetFltWinSynchNdisRequest(pNetFlt, &Request);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        if (Status == NDIS_STATUS_NOT_SUPPORTED || Status == NDIS_STATUS_NOT_RECOGNIZED || Status == NDIS_STATUS_INVALID_OID)
        {
            Status = NDIS_STATUS_NOT_SUPPORTED;
        }
        else
        {
            LogRel(("OID_GEN_PHYSICAL_MEDIUM failed: Status (0x%x)", Status));
            AssertFailed();
        }
    }
    return Status;
}

DECLHIDDEN(bool) nemuNetFltWinIsPromiscuous(PNEMUNETFLTINS pNetFlt)
{
    /** @todo r=bird: This is too slow and is probably returning the wrong
     *        information. What we're interested in is whether someone besides us
     *        has put the interface into promiscuous mode. */
    NDIS_REQUEST request;
    NDIS_STATUS status;
    ULONG filter;
    Assert(NEMUNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt));
    request.RequestType = NdisRequestQueryInformation;
    request.DATA.QUERY_INFORMATION.InformationBuffer = &filter;
    request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(filter);
    request.DATA.QUERY_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
    status = nemuNetFltWinSynchNdisRequest(pNetFlt, &request);
    if (status != NDIS_STATUS_SUCCESS)
    {
        /* TODO */
        AssertFailed();
        return false;
    }
    return (filter & NDIS_PACKET_TYPE_PROMISCUOUS) != 0;
}

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinSetPromiscuous(PNEMUNETFLTINS pNetFlt, bool bYes)
{
/** @todo Need to report changes to the switch via:
 *  pThis->pSwitchPort->pfnReportPromiscuousMode(pThis->pSwitchPort, fPromisc);
 */
    Assert(NEMUNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt));
    if (NEMUNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt))
    {
        NDIS_REQUEST Request;
        NDIS_STATUS fStatus;
        ULONG fFilter;
        ULONG fExpectedFilter;
        ULONG fOurFilter;
        Request.RequestType = NdisRequestQueryInformation;
        Request.DATA.QUERY_INFORMATION.InformationBuffer = &fFilter;
        Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(fFilter);
        Request.DATA.QUERY_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
        fStatus = nemuNetFltWinSynchNdisRequest(pNetFlt, &Request);
        if (fStatus != NDIS_STATUS_SUCCESS)
        {
            /* TODO: */
            AssertFailed();
            return fStatus;
        }

        if (!pNetFlt->u.s.WinIf.StateFlags.fUpperProtSetFilterInitialized)
        {
            /* the cache was not initialized yet, initiate it with the current filter value */
            pNetFlt->u.s.WinIf.fUpperProtocolSetFilter = fFilter;
            pNetFlt->u.s.WinIf.StateFlags.fUpperProtSetFilterInitialized = TRUE;
        }


        if (bYes)
        {
            fExpectedFilter = NDIS_PACKET_TYPE_PROMISCUOUS;
            fOurFilter = NDIS_PACKET_TYPE_PROMISCUOUS;
        }
        else
        {
            fExpectedFilter = pNetFlt->u.s.WinIf.fUpperProtocolSetFilter;
            fOurFilter = 0;
        }

        if (fExpectedFilter != fFilter)
        {
            Request.RequestType = NdisRequestSetInformation;
            Request.DATA.SET_INFORMATION.InformationBuffer = &fExpectedFilter;
            Request.DATA.SET_INFORMATION.InformationBufferLength = sizeof(fExpectedFilter);
            Request.DATA.SET_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
            fStatus = nemuNetFltWinSynchNdisRequest(pNetFlt, &Request);
            if (fStatus != NDIS_STATUS_SUCCESS)
            {
                /* TODO */
                AssertFailed();
                return fStatus;
            }
        }
        pNetFlt->u.s.WinIf.fOurSetFilter = fOurFilter;
        return fStatus;
    }
    return NDIS_STATUS_NOT_SUPPORTED;
}
#else /* if defined NEMUNETADP */

/**
 *  Generates a new unique MAC address based on our vendor ID
 */
DECLHIDDEN(void) nemuNetFltWinGenerateMACAddress(RTMAC *pMac)
{
    /* temporary use a time info */
    uint64_t NanoTS = RTTimeSystemNanoTS();
    pMac->au8[0] = (uint8_t)((NEMUNETADP_VENDOR_ID >> 16) & 0xff);
    pMac->au8[1] = (uint8_t)((NEMUNETADP_VENDOR_ID >> 8) & 0xff);
    pMac->au8[2] = (uint8_t)(NEMUNETADP_VENDOR_ID & 0xff);
    pMac->au8[3] = (uint8_t)(NanoTS & 0xff0000);
    pMac->au16[2] = (uint16_t)(NanoTS & 0xffff);
}

DECLHIDDEN(int) nemuNetFltWinMAC2NdisString(RTMAC *pMac, PNDIS_STRING pNdisString)
{
    static const char s_achDigits[17] = "0123456789abcdef";
    PWSTR pString;

    /* validate parameters */
    AssertPtrReturn(pMac, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pNdisString, VERR_INVALID_PARAMETER);
    AssertReturn(pNdisString->MaximumLength >= 13*sizeof(pNdisString->Buffer[0]), VERR_INVALID_PARAMETER);

    pString = pNdisString->Buffer;

    for (int i = 0; i < 6; i++)
    {
        uint8_t u8 = pMac->au8[i];
        pString[0] = s_achDigits[(u8 >>  4) & 0xf];
        pString[1] = s_achDigits[(u8/*>>0*/)& 0xf];
        pString += 2;
    }

    pNdisString->Length = 12*sizeof(pNdisString->Buffer[0]);

    *pString = L'\0';

    return VINF_SUCCESS;
}

static int nemuNetFltWinWchar2Int(WCHAR c, uint8_t * pv)
{
    if (c >= L'A' && c <= L'F')
    {
        *pv = (c - L'A') + 10;
    }
    else if (c >= L'a' && c <= L'f')
    {
        *pv = (c - L'a') + 10;
    }
    else if (c >= L'0' && c <= L'9')
    {
        *pv = (c - L'0');
    }
    else
    {
        return VERR_INVALID_PARAMETER;
    }
    return VINF_SUCCESS;
}

DECLHIDDEN(int) nemuNetFltWinMACFromNdisString(RTMAC *pMac, PNDIS_STRING pNdisString)
{
    int i, rc;
    PWSTR pString;

    /* validate parameters */
    AssertPtrReturn(pMac, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pNdisString, VERR_INVALID_PARAMETER);
    AssertReturn(pNdisString->Length >= 12*sizeof(pNdisString->Buffer[0]), VERR_INVALID_PARAMETER);

    pString = pNdisString->Buffer;

    for (i = 0; i < 6; i++)
    {
        uint8_t v1, v2;
        rc = nemuNetFltWinWchar2Int(pString[0], &v1);
        if (RT_FAILURE(rc))
        {
            break;
        }

        rc = nemuNetFltWinWchar2Int(pString[1], &v2);
        if (RT_FAILURE(rc))
        {
            break;
        }

        pMac->au8[i] = (v1 << 4) | v2;

        pString += 2;
    }

    return rc;
}

#endif
/**
 * creates a NDIS_PACKET from the PINTNETSG
 */
DECLHIDDEN(PNDIS_PACKET) nemuNetFltWinNdisPacketFromSG(PNEMUNETFLTINS pNetFlt, PINTNETSG pSG, PVOID pBufToFree, bool bToWire, bool bCopyMemory)
{
    NDIS_STATUS fStatus;
    PNDIS_PACKET pPacket;

    Assert(pSG->aSegs[0].pv);
    Assert(pSG->cbTotal >= sizeof(NEMUNETFLT_PACKET_ETHEADER_SIZE));

/** @todo Hrmpf, how can we fix this assumption?  I fear this'll cause data
 *        corruption and maybe even BSODs ... */
    AssertReturn(pSG->cSegsUsed == 1 || bCopyMemory, NULL);

#ifdef NEMUNETADP
    NdisAllocatePacket(&fStatus, &pPacket, pNetFlt->u.s.WinIf.hRecvPacketPool);
#else
    NdisAllocatePacket(&fStatus, &pPacket, bToWire ? pNetFlt->u.s.WinIf.hSendPacketPool : pNetFlt->u.s.WinIf.hRecvPacketPool);
#endif
    if (fStatus == NDIS_STATUS_SUCCESS)
    {
        PNDIS_BUFFER pBuffer;
        PVOID pvMemBuf;

        /* @todo: generally we do not always need to zero-initialize the complete OOB data here, reinitialize only when/what we need,
         * however we DO need to reset the status for the packets we indicate via NdisMIndicateReceivePacket to avoid packet loss
         * in case the status contains NDIS_STATUS_RESOURCES */
        NEMUNETFLT_OOB_INIT(pPacket);

        if (bCopyMemory)
        {
            fStatus = nemuNetFltWinMemAlloc(&pvMemBuf, pSG->cbTotal);
            Assert(fStatus == NDIS_STATUS_SUCCESS);
            if (fStatus == NDIS_STATUS_SUCCESS)
                IntNetSgRead(pSG, pvMemBuf);
        }
        else
        {
            pvMemBuf = pSG->aSegs[0].pv;
        }
        if (fStatus == NDIS_STATUS_SUCCESS)
        {
#ifdef NEMUNETADP
            NdisAllocateBuffer(&fStatus, &pBuffer,
                    pNetFlt->u.s.WinIf.hRecvBufferPool,
                    pvMemBuf,
                    pSG->cbTotal);
#else
            NdisAllocateBuffer(&fStatus, &pBuffer,
                    bToWire ? pNetFlt->u.s.WinIf.hSendBufferPool : pNetFlt->u.s.WinIf.hRecvBufferPool,
                    pvMemBuf,
                    pSG->cbTotal);
#endif

            if (fStatus == NDIS_STATUS_SUCCESS)
            {
                NdisChainBufferAtBack(pPacket, pBuffer);

                if (bToWire)
                {
                    PNEMUNETFLT_PKTRSVD_PT pSendInfo = (PNEMUNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
                    pSendInfo->pOrigPacket = NULL;
                    pSendInfo->pBufToFree = pBufToFree;
#ifdef NEMU_LOOPBACK_USEFLAGS
                    /* set "don't loopback" flags */
                    NdisGetPacketFlags(pPacket) = g_NemuNetFltGlobalsWin.fPacketDontLoopBack;
#else
                    NdisGetPacketFlags(pPacket) = 0;
#endif
                }
                else
                {
                    PNEMUNETFLT_PKTRSVD_MP pRecvInfo = (PNEMUNETFLT_PKTRSVD_MP)pPacket->MiniportReserved;
                    pRecvInfo->pOrigPacket = NULL;
                    pRecvInfo->pBufToFree = pBufToFree;

                    /* we must set the header size on receive */
                    NDIS_SET_PACKET_HEADER_SIZE(pPacket, NEMUNETFLT_PACKET_ETHEADER_SIZE);
                    /* NdisAllocatePacket zero-initializes the OOB data,
                     * but keeps the packet flags, clean them here */
                    NdisGetPacketFlags(pPacket) = 0;
                }
                /* TODO: set out of bound data */
            }
            else
            {
                AssertFailed();
                if (bCopyMemory)
                {
                    nemuNetFltWinMemFree(pvMemBuf);
                }
                NdisFreePacket(pPacket);
                pPacket = NULL;
            }
        }
        else
        {
            AssertFailed();
            NdisFreePacket(pPacket);
            pPacket = NULL;
        }
    }
    else
    {
        pPacket = NULL;
    }

    DBG_CHECK_PACKET_AND_SG(pPacket, pSG);

    return pPacket;
}

/*
 * frees NDIS_PACKET created with nemuNetFltWinNdisPacketFromSG
 */
DECLHIDDEN(void) nemuNetFltWinFreeSGNdisPacket(PNDIS_PACKET pPacket, bool bFreeMem)
{
    UINT cBufCount;
    PNDIS_BUFFER pFirstBuffer;
    UINT uTotalPacketLength;
    PNDIS_BUFFER pBuffer;

    NdisQueryPacket(pPacket, NULL, &cBufCount, &pFirstBuffer, &uTotalPacketLength);

    Assert(cBufCount == 1);

    do
    {
        NdisUnchainBufferAtBack(pPacket, &pBuffer);
        if (pBuffer != NULL)
        {
            PVOID pvMemBuf;
            UINT cbLength;

            NdisQueryBufferSafe(pBuffer, &pvMemBuf, &cbLength, NormalPagePriority);
            NdisFreeBuffer(pBuffer);
            if (bFreeMem)
            {
                nemuNetFltWinMemFree(pvMemBuf);
            }
        }
        else
        {
            break;
        }
    } while (true);

    NdisFreePacket(pPacket);
}

#if !defined(NEMUNETADP)
static void nemuNetFltWinAssociateMiniportProtocol(PNEMUNETFLTGLOBALS_WIN pGlobalsWin)
{
    NdisIMAssociateMiniport(pGlobalsWin->Mp.hMiniport, pGlobalsWin->Pt.hProtocol);
}
#endif

/*
 * NetFlt driver unload function
 */
DECLHIDDEN(VOID) nemuNetFltWinUnload(IN PDRIVER_OBJECT DriverObject)
{
    int rc;
    UNREFERENCED_PARAMETER(DriverObject);

    LogFlow((__FUNCTION__" ==> DO (0x%x)\n", DriverObject));

    rc = nemuNetFltWinTryFiniIdc();
    if (RT_FAILURE(rc))
    {
        /* TODO: we can not prevent driver unload here */
        AssertFailed();

        Log((__FUNCTION__": nemuNetFltWinTryFiniIdc - failed, busy.\n"));
    }

    nemuNetFltWinJobFiniQueue(&g_NemuJobQueue);
#ifndef NEMUNETADP
    nemuNetFltWinPtDeregister(&g_NemuNetFltGlobalsWin.Pt);
#endif

    nemuNetFltWinMpDeregister(&g_NemuNetFltGlobalsWin.Mp);

#ifndef NEMUNETADP
    NdisFreeSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);
#endif /* NEMUNETADP */

    LogFlow((__FUNCTION__" <== DO (0x%x)\n", DriverObject));

    nemuNetFltWinFiniNetFltBase();
    /* don't use logging or any RT after de-init */
}

RT_C_DECLS_BEGIN

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath);

RT_C_DECLS_END

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    int rc;

    /* the idc registration is initiated via IOCTL since our driver
     * can be loaded when the NemuDrv is not in case we are a Ndis IM driver */
    rc = nemuNetFltWinInitNetFltBase();
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        Status = nemuNetFltWinJobInitQueue(&g_NemuJobQueue);
        Assert(Status == STATUS_SUCCESS);
        if (Status == STATUS_SUCCESS)
        {
            ULONG MjVersion;
            ULONG MnVersion;

            /* note: we do it after we initialize the Job Queue */
            nemuNetFltWinStartInitIdcProbing();

            NdisZeroMemory(&g_NemuNetFltGlobalsWin, sizeof (g_NemuNetFltGlobalsWin));
            KeInitializeEvent(&g_NemuNetFltGlobalsWin.SynchEvent, SynchronizationEvent, TRUE /* signalled*/);

            PsGetVersion(&MjVersion, &MnVersion,
              NULL, /* PULONG BuildNumber OPTIONAL */
              NULL /* PUNICODE_STRING CSDVersion OPTIONAL */
              );

            g_NemuNetFltGlobalsWin.fPacketDontLoopBack = NDIS_FLAGS_DONT_LOOPBACK;

            if (MjVersion == 5 && MnVersion == 0)
            {
                /* this is Win2k, we don't support it actually, but just in case */
                g_NemuNetFltGlobalsWin.fPacketDontLoopBack |= NDIS_FLAGS_SKIP_LOOPBACK_W2K;
            }

            g_NemuNetFltGlobalsWin.fPacketIsLoopedBack = NDIS_FLAGS_IS_LOOPBACK_PACKET;

#ifndef NEMUNETADP
            RTListInit(&g_NemuNetFltGlobalsWin.listFilters);
            NdisAllocateSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);
#endif

            Status = nemuNetFltWinMpRegister(&g_NemuNetFltGlobalsWin.Mp, DriverObject, RegistryPath);
            Assert(Status == STATUS_SUCCESS);
            if (Status == NDIS_STATUS_SUCCESS)
            {
#ifndef NEMUNETADP
                Status = nemuNetFltWinPtRegister(&g_NemuNetFltGlobalsWin.Pt, DriverObject, RegistryPath);
                Assert(Status == STATUS_SUCCESS);
                if (Status == NDIS_STATUS_SUCCESS)
#endif
                {
#ifndef NEMUNETADP
                    nemuNetFltWinAssociateMiniportProtocol(&g_NemuNetFltGlobalsWin);
#endif
                    return STATUS_SUCCESS;

//#ifndef NEMUNETADP
//                nemuNetFltWinPtDeregister(&g_NemuNetFltGlobalsWin.Pt);
//#endif
                }
                nemuNetFltWinMpDeregister(&g_NemuNetFltGlobalsWin.Mp);
#ifndef NEMUNETADP
                NdisFreeSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);
#endif /* NEMUNETADP */
            }
            nemuNetFltWinJobFiniQueue(&g_NemuJobQueue);
        }
        nemuNetFltWinFiniNetFlt();
    }
    else
    {
        Status = NDIS_STATUS_FAILURE;
    }

    return Status;
}

#ifndef NEMUNETADP
/**
 * creates and initializes the packet to be sent to the underlying miniport given a packet posted to our miniport edge
 * according to DDK docs we must create our own packet rather than posting the one passed to us
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPrepareSendPacket(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket, PNDIS_PACKET *ppMyPacket)
{
    NDIS_STATUS Status;

    NdisAllocatePacket(&Status, ppMyPacket, pNetFlt->u.s.WinIf.hSendPacketPool);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        PNEMUNETFLT_PKTRSVD_PT pSendInfo = (PNEMUNETFLT_PKTRSVD_PT)((*ppMyPacket)->ProtocolReserved);
        pSendInfo->pOrigPacket = pPacket;
        pSendInfo->pBufToFree = NULL;
        /* the rest will be filled on send */

        nemuNetFltWinCopyPacketInfoOnSend(*ppMyPacket, pPacket);

#ifdef NEMU_LOOPBACK_USEFLAGS
        NdisGetPacketFlags(*ppMyPacket) |= g_NemuNetFltGlobalsWin.fPacketDontLoopBack;
#endif
    }
    else
    {
        *ppMyPacket = NULL;
    }

    return Status;
}

/**
 * creates and initializes the packet to be sent to the upperlying protocol given a packet indicated to our protocol edge
 * according to DDK docs we must create our own packet rather than posting the one passed to us
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPrepareRecvPacket(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket, PNDIS_PACKET *ppMyPacket, bool bDpr)
{
    NDIS_STATUS Status;

    if (bDpr)
    {
        Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);
        NdisDprAllocatePacket(&Status, ppMyPacket, pNetFlt->u.s.WinIf.hRecvPacketPool);
    }
    else
    {
        NdisAllocatePacket(&Status, ppMyPacket, pNetFlt->u.s.WinIf.hRecvPacketPool);
    }

    if (Status == NDIS_STATUS_SUCCESS)
    {
        PNEMUNETFLT_PKTRSVD_MP pRecvInfo = (PNEMUNETFLT_PKTRSVD_MP)((*ppMyPacket)->MiniportReserved);
        pRecvInfo->pOrigPacket = pPacket;
        pRecvInfo->pBufToFree = NULL;

        Status = nemuNetFltWinCopyPacketInfoOnRecv(*ppMyPacket, pPacket, false);
    }
    else
    {
        *ppMyPacket = NULL;
    }
    return Status;
}
#endif
/**
 * initializes the NEMUNETFLTINS (our context structure) and binds to the given adapter
 */
#if defined(NEMUNETADP)
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtInitBind(PNEMUNETFLTINS *ppNetFlt, NDIS_HANDLE hMiniportAdapter, PNDIS_STRING pBindToMiniportName /* actually this is our miniport name*/, NDIS_HANDLE hWrapperConfigurationContext)
#else
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtInitBind(PNEMUNETFLTINS *ppNetFlt, PNDIS_STRING pOurMiniportName, PNDIS_STRING pBindToMiniportName)
#endif
{
    NDIS_STATUS Status;
    do
    {
        ANSI_STRING AnsiString;
        int rc;
        PNEMUNETFLTINS pInstance;
        USHORT cbAnsiName = pBindToMiniportName->Length;/* the length is is bytes ; *2 ;RtlUnicodeStringToAnsiSize(pBindToMiniportName)*/
        CREATE_INSTANCE_CONTEXT Context;

# ifndef NEMUNETADP
        Context.pOurName = pOurMiniportName;
        Context.pBindToName = pBindToMiniportName;
# else
        Context.hMiniportAdapter = hMiniportAdapter;
        Context.hWrapperConfigurationContext = hWrapperConfigurationContext;
# endif
        Context.Status = NDIS_STATUS_SUCCESS;

        AnsiString.Buffer = 0; /* will be allocated by RtlUnicodeStringToAnsiString */
        AnsiString.Length = 0;
        AnsiString.MaximumLength = cbAnsiName;

        Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

        Status = RtlUnicodeStringToAnsiString(&AnsiString, pBindToMiniportName, true);

        if (Status != STATUS_SUCCESS)
        {
            break;
        }

        rc = nemuNetFltSearchCreateInstance(&g_NemuNetFltGlobals, AnsiString.Buffer, &pInstance, &Context);
        RtlFreeAnsiString(&AnsiString);
        if (RT_FAILURE(rc))
        {
            AssertFailed();
            Status = Context.Status != NDIS_STATUS_SUCCESS ? Context.Status : NDIS_STATUS_FAILURE;
            break;
        }

        Assert(pInstance);

        if (rc == VINF_ALREADY_INITIALIZED)
        {
            /* the case when our adapter was unbound while IntNet was connected to it */
            /* the instance remains valid until IntNet disconnects from it, we simply search and re-use it*/
            rc = nemuNetFltWinAttachToInterface(pInstance, &Context, true);
            if (RT_FAILURE(rc))
            {
                AssertFailed();
                Status = Context.Status != NDIS_STATUS_SUCCESS ? Context.Status : NDIS_STATUS_FAILURE;
                /* release netflt */
                nemuNetFltRelease(pInstance, false);

                break;
            }
        }

        *ppNetFlt = pInstance;

    } while (FALSE);

    return Status;
}
/*
 * deinitializes the NEMUNETFLTWIN
 */
DECLHIDDEN(VOID) nemuNetFltWinPtFiniWinIf(PNEMUNETFLTWIN pWinIf)
{
#ifndef NEMUNETADP
    int rc;
#endif

    LogFlow(("==>"__FUNCTION__" : pWinIf 0x%p\n", pWinIf));

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
#ifndef NEMUNETADP
    if (pWinIf->MpDeviceName.Buffer)
    {
        nemuNetFltWinMemFree(pWinIf->MpDeviceName.Buffer);
    }

    FINI_INTERLOCKED_SINGLE_LIST(&pWinIf->TransferDataList);
# if defined(DEBUG_NETFLT_LOOPBACK) || !defined(NEMU_LOOPBACK_USEFLAGS)
    FINI_INTERLOCKED_SINGLE_LIST(&pWinIf->SendPacketQueue);
# endif
    NdisFreeBufferPool(pWinIf->hSendBufferPool);
    NdisFreePacketPool(pWinIf->hSendPacketPool);
    rc = RTSemFastMutexDestroy(pWinIf->hSynchRequestMutex);  AssertRC(rc);
#endif

    /* NOTE: NULL is a valid handle */
    NdisFreeBufferPool(pWinIf->hRecvBufferPool);
    NdisFreePacketPool(pWinIf->hRecvPacketPool);

    LogFlow(("<=="__FUNCTION__" : pWinIf 0x%p\n", pWinIf));
}

#ifndef NEMUNETADP
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtInitWinIf(PNEMUNETFLTWIN pWinIf, IN PNDIS_STRING pOurDeviceName)
#else
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtInitWinIf(PNEMUNETFLTWIN pWinIf)
#endif
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
#ifndef NEMUNETADP
    int rc;
#endif
    BOOLEAN bCallFiniOnFail = FALSE;

    LogFlow(("==>"__FUNCTION__": pWinIf 0x%p\n", pWinIf));

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    NdisZeroMemory(pWinIf, sizeof (NEMUNETFLTWIN));
    NdisAllocatePacketPoolEx(&Status, &pWinIf->hRecvPacketPool,
                               NEMUNETFLT_PACKET_POOL_SIZE_NORMAL,
                               NEMUNETFLT_PACKET_POOL_SIZE_OVERFLOW,
                               PROTOCOL_RESERVED_SIZE_IN_PACKET);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        /* NOTE: NULL is a valid handle !!! */
        NdisAllocateBufferPool(&Status, &pWinIf->hRecvBufferPool, NEMUNETFLT_BUFFER_POOL_SIZE_RX);
        Assert(Status == NDIS_STATUS_SUCCESS);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            pWinIf->MpState.PowerState = NdisDeviceStateD3;
            nemuNetFltWinSetOpState(&pWinIf->MpState, kNemuNetDevOpState_Deinitialized);
#ifndef NEMUNETADP
            pWinIf->PtState.PowerState = NdisDeviceStateD3;
            nemuNetFltWinSetOpState(&pWinIf->PtState, kNemuNetDevOpState_Deinitialized);

            NdisAllocateBufferPool(&Status,
                    &pWinIf->hSendBufferPool,
                    NEMUNETFLT_BUFFER_POOL_SIZE_TX);
            Assert(Status == NDIS_STATUS_SUCCESS);
            if (Status == NDIS_STATUS_SUCCESS)
            {
                INIT_INTERLOCKED_SINGLE_LIST(&pWinIf->TransferDataList);

# if defined(DEBUG_NETFLT_LOOPBACK) || !defined(NEMU_LOOPBACK_USEFLAGS)
                INIT_INTERLOCKED_SINGLE_LIST(&pWinIf->SendPacketQueue);
# endif
                NdisInitializeEvent(&pWinIf->OpenCloseEvent);

                KeInitializeEvent(&pWinIf->hSynchCompletionEvent, SynchronizationEvent, FALSE);

                NdisInitializeEvent(&pWinIf->MpInitCompleteEvent);

                NdisAllocatePacketPoolEx(&Status, &pWinIf->hSendPacketPool,
                                           NEMUNETFLT_PACKET_POOL_SIZE_NORMAL,
                                           NEMUNETFLT_PACKET_POOL_SIZE_OVERFLOW,
                                           sizeof (PNEMUNETFLT_PKTRSVD_PT));
                Assert(Status == NDIS_STATUS_SUCCESS);
                if (Status == NDIS_STATUS_SUCCESS)
                {
                    rc = RTSemFastMutexCreate(&pWinIf->hSynchRequestMutex);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        Status = nemuNetFltWinMemAlloc((PVOID*)&pWinIf->MpDeviceName.Buffer, pOurDeviceName->Length);
                        Assert(Status == NDIS_STATUS_SUCCESS);
                        if (Status == NDIS_STATUS_SUCCESS)
                        {
                            pWinIf->MpDeviceName.MaximumLength = pOurDeviceName->Length;
                            pWinIf->MpDeviceName.Length = 0;
                            Status = nemuNetFltWinCopyString(&pWinIf->MpDeviceName, pOurDeviceName);
#endif
                            return NDIS_STATUS_SUCCESS;
#ifndef NEMUNETADP
                            nemuNetFltWinMemFree(pWinIf->MpDeviceName.Buffer);
                        }
                        RTSemFastMutexDestroy(pWinIf->hSynchRequestMutex);
                    }
                    else
                    {
                        Status = NDIS_STATUS_FAILURE;
                    }
                    NdisFreePacketPool(pWinIf->hSendPacketPool);
                }
                NdisFreeBufferPool(pWinIf->hSendBufferPool);
            }
#endif
            NdisFreeBufferPool(pWinIf->hRecvBufferPool);
        }
        NdisFreePacketPool(pWinIf->hRecvPacketPool);
    }

    LogFlow(("<=="__FUNCTION__": pWinIf 0x%p, Status 0x%x\n", pWinIf, Status));

    return Status;
}

/**
 * match packets
 */
#define NEXT_LIST_ENTRY(_Entry) ((_Entry)->Flink)
#define PREV_LIST_ENTRY(_Entry) ((_Entry)->Blink)
#define FIRST_LIST_ENTRY NEXT_LIST_ENTRY
#define LAST_LIST_ENTRY PREV_LIST_ENTRY

#define MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))

#ifndef NEMUNETADP

#ifdef DEBUG_misha

RTMAC g_nemuNetFltWinVerifyMACBroadcast = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
RTMAC g_nemuNetFltWinVerifyMACGuest = {0x08, 0x00, 0x27, 0x01, 0x02, 0x03};

DECLHIDDEN(PRTNETETHERHDR) nemuNetFltWinGetEthHdr(PNDIS_PACKET pPacket)
{
    UINT cBufCount1;
    PNDIS_BUFFER pBuffer1;
    UINT uTotalPacketLength1;
    RTNETETHERHDR* pEth;
    UINT cbLength1 = 0;
    UINT i = 0;

    NdisQueryPacket(pPacket, NULL, &cBufCount1, &pBuffer1, &uTotalPacketLength1);

    Assert(pBuffer1);
    Assert(uTotalPacketLength1 >= NEMUNETFLT_PACKET_ETHEADER_SIZE);
    if (uTotalPacketLength1 < NEMUNETFLT_PACKET_ETHEADER_SIZE)
        return NULL;

    NdisQueryBufferSafe(pBuffer1, &pEth, &cbLength1, NormalPagePriority);
    Assert(cbLength1 >= NEMUNETFLT_PACKET_ETHEADER_SIZE);
    if (cbLength1 < NEMUNETFLT_PACKET_ETHEADER_SIZE)
        return NULL;

    return pEth;
}

DECLHIDDEN(PRTNETETHERHDR) nemuNetFltWinGetEthHdrSG(PINTNETSG pSG)
{
    Assert(pSG->cSegsUsed);
    Assert(pSG->cSegsAlloc >= pSG->cSegsUsed);
    Assert(pSG->aSegs[0].cb >= NEMUNETFLT_PACKET_ETHEADER_SIZE);

    if (!pSG->cSegsUsed)
        return NULL;

    if (pSG->aSegs[0].cb < NEMUNETFLT_PACKET_ETHEADER_SIZE)
        return NULL;

    return (PRTNETETHERHDR)pSG->aSegs[0].pv;
}

DECLHIDDEN(bool) nemuNetFltWinCheckMACs(PNDIS_PACKET pPacket, PRTMAC pDst, PRTMAC pSrc)
{
    PRTNETETHERHDR pHdr = nemuNetFltWinGetEthHdr(pPacket);
    Assert(pHdr);

    if (!pHdr)
        return false;

    if (pDst && memcmp(pDst, &pHdr->DstMac, sizeof(RTMAC)))
        return false;

    if (pSrc && memcmp(pSrc, &pHdr->SrcMac, sizeof(RTMAC)))
        return false;

    return true;
}

DECLHIDDEN(bool) nemuNetFltWinCheckMACsSG(PINTNETSG pSG, PRTMAC pDst, PRTMAC pSrc)
{
    PRTNETETHERHDR pHdr = nemuNetFltWinGetEthHdrSG(pSG);
    Assert(pHdr);

    if (!pHdr)
        return false;

    if (pDst && memcmp(pDst, &pHdr->DstMac, sizeof(RTMAC)))
        return false;

    if (pSrc && memcmp(pSrc, &pHdr->SrcMac, sizeof(RTMAC)))
        return false;

    return true;
}
#endif

# if !defined(NEMU_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)
/*
 * answers whether the two given packets match based on the packet length and the first cbMatch bytes of the packets
 * if cbMatch < 0 matches complete packets.
 */
DECLHIDDEN(bool) nemuNetFltWinMatchPackets(PNDIS_PACKET pPacket1, PNDIS_PACKET pPacket2, const INT cbMatch)
{
    UINT cBufCount1;
    PNDIS_BUFFER pBuffer1;
    UINT uTotalPacketLength1;
    uint8_t* pMemBuf1;
    UINT cbLength1 = 0;

    UINT cBufCount2;
    PNDIS_BUFFER pBuffer2;
    UINT uTotalPacketLength2;
    uint8_t* pMemBuf2;
    UINT cbLength2 = 0;
    bool bMatch = true;

#ifdef DEBUG_NETFLT_PACKETS
    bool bCompleteMatch = false;
#endif

    NdisQueryPacket(pPacket1, NULL, &cBufCount1, &pBuffer1, &uTotalPacketLength1);
    NdisQueryPacket(pPacket2, NULL, &cBufCount2, &pBuffer2, &uTotalPacketLength2);

    Assert(pBuffer1);
    Assert(pBuffer2);

    if (uTotalPacketLength1 != uTotalPacketLength2)
    {
        bMatch = false;
    }
    else
    {
        UINT ucbLength2Match = 0;
        UINT ucbMatch;
        if (cbMatch < 0 || (UINT)cbMatch > uTotalPacketLength1)
        {
            /* NOTE: assuming uTotalPacketLength1 == uTotalPacketLength2*/
            ucbMatch = uTotalPacketLength1;
#ifdef DEBUG_NETFLT_PACKETS
            bCompleteMatch = true;
#endif
        }
        else
        {
            ucbMatch = (UINT)cbMatch;
        }

        for (;;)
        {
            if (!cbLength1)
            {
                NdisQueryBufferSafe(pBuffer1, &pMemBuf1, &cbLength1, NormalPagePriority);
                NdisGetNextBuffer(pBuffer1, &pBuffer1);
            }
            else
            {
                Assert(pMemBuf1);
                Assert(ucbLength2Match);
                pMemBuf1 += ucbLength2Match;
            }

            if (!cbLength2)
            {
                NdisQueryBufferSafe(pBuffer2, &pMemBuf2, &cbLength2, NormalPagePriority);
                NdisGetNextBuffer(pBuffer2, &pBuffer2);
            }
            else
            {
                Assert(pMemBuf2);
                Assert(ucbLength2Match);
                pMemBuf2 += ucbLength2Match;
            }

            ucbLength2Match = MIN(ucbMatch, cbLength1);
            ucbLength2Match = MIN(ucbLength2Match, cbLength2);

            if (memcmp((PVOID*)pMemBuf1, (PVOID*)pMemBuf2, ucbLength2Match))
            {
                bMatch = false;
                break;
            }

            ucbMatch -= ucbLength2Match;
            if (!ucbMatch)
                break;

            cbLength1 -= ucbLength2Match;
            cbLength2 -= ucbLength2Match;
        }
    }

#ifdef DEBUG_NETFLT_PACKETS
    if (bMatch && !bCompleteMatch)
    {
        /* check that the packets fully match */
        DBG_CHECK_PACKETS(pPacket1, pPacket2);
    }
#endif

    return bMatch;
}

/*
 * answers whether the ndis packet and PINTNETSG match based on the packet length and the first cbMatch bytes of the packet and PINTNETSG
 * if cbMatch < 0 matches complete packets.
 */
DECLHIDDEN(bool) nemuNetFltWinMatchPacketAndSG(PNDIS_PACKET pPacket, PINTNETSG pSG, const INT cbMatch)
{
    UINT cBufCount1;
    PNDIS_BUFFER pBuffer1;
    UINT uTotalPacketLength1;
    uint8_t* pMemBuf1;
    UINT cbLength1 = 0;
    UINT uTotalPacketLength2 = pSG->cbTotal;
    uint8_t* pMemBuf2;
    UINT cbLength2 = 0;
    bool bMatch = true;
    bool bCompleteMatch = false;
    UINT i = 0;

    NdisQueryPacket(pPacket, NULL, &cBufCount1, &pBuffer1, &uTotalPacketLength1);

    Assert(pBuffer1);
    Assert(pSG->cSegsUsed);
    Assert(pSG->cSegsAlloc >= pSG->cSegsUsed);

    if (uTotalPacketLength1 != uTotalPacketLength2)
    {
        AssertFailed();
        bMatch = false;
    }
    else
    {
        UINT ucbLength2Match = 0;
        UINT ucbMatch;

        if (cbMatch < 0 || (UINT)cbMatch > uTotalPacketLength1)
        {
            /* NOTE: assuming uTotalPacketLength1 == uTotalPacketLength2*/
            ucbMatch = uTotalPacketLength1;
            bCompleteMatch = true;
        }
        else
        {
            ucbMatch = (UINT)cbMatch;
        }

        for (;;)
        {
            if (!cbLength1)
            {
                NdisQueryBufferSafe(pBuffer1, &pMemBuf1, &cbLength1, NormalPagePriority);
                NdisGetNextBuffer(pBuffer1, &pBuffer1);
            }
            else
            {
                Assert(pMemBuf1);
                Assert(ucbLength2Match);
                pMemBuf1 += ucbLength2Match;
            }

            if (!cbLength2)
            {
                Assert(i < pSG->cSegsUsed);
                pMemBuf2 = (uint8_t*)pSG->aSegs[i].pv;
                cbLength2 = pSG->aSegs[i].cb;
                i++;
            }
            else
            {
                Assert(pMemBuf2);
                Assert(ucbLength2Match);
                pMemBuf2 += ucbLength2Match;
            }

            ucbLength2Match = MIN(ucbMatch, cbLength1);
            ucbLength2Match = MIN(ucbLength2Match, cbLength2);

            if (memcmp((PVOID*)pMemBuf1, (PVOID*)pMemBuf2, ucbLength2Match))
            {
                bMatch = false;
                AssertFailed();
                break;
            }

            ucbMatch -= ucbLength2Match;
            if (!ucbMatch)
                break;

            cbLength1 -= ucbLength2Match;
            cbLength2 -= ucbLength2Match;
        }
    }

    if (bMatch && !bCompleteMatch)
    {
        /* check that the packets fully match */
        DBG_CHECK_PACKET_AND_SG(pPacket, pSG);
    }
    return bMatch;
}

#  if 0
/*
 * answers whether the two PINTNETSGs match based on the packet length and the first cbMatch bytes of the PINTNETSG
 * if cbMatch < 0 matches complete packets.
 */
static bool nemuNetFltWinMatchSGs(PINTNETSG pSG1, PINTNETSG pSG2, const INT cbMatch)
{
    UINT uTotalPacketLength1 = pSG1->cbTotal;
    PVOID pMemBuf1;
    UINT cbLength1 = 0;
    UINT i1 = 0;
    UINT uTotalPacketLength2 = pSG2->cbTotal;
    PVOID pMemBuf2;
    UINT cbLength2 = 0;

    bool bMatch = true;
    bool bCompleteMatch = false;
    UINT i2 = 0;

    Assert(pSG1->cSegsUsed);
    Assert(pSG2->cSegsUsed);
    Assert(pSG1->cSegsAlloc >= pSG1->cSegsUsed);
    Assert(pSG2->cSegsAlloc >= pSG2->cSegsUsed);

    if (uTotalPacketLength1 != uTotalPacketLength2)
    {
        AssertFailed();
        bMatch = false;
    }
    else
    {
        UINT ucbMatch;
        if (cbMatch < 0 || (UINT)cbMatch > uTotalPacketLength1)
        {
            /* NOTE: assuming uTotalPacketLength1 == uTotalPacketLength2*/
            ucbMatch = uTotalPacketLength1;
            bCompleteMatch = true;
        }
        else
        {
            ucbMatch = (UINT)cbMatch;
        }

        do
        {
            UINT ucbLength2Match;
            if (!cbLength1)
            {
                Assert(i1 < pSG1->cSegsUsed);
                pMemBuf1 = pSG1->aSegs[i1].pv;
                cbLength1 = pSG1->aSegs[i1].cb;
                i1++;
            }

            if (!cbLength2)
            {
                Assert(i2 < pSG2->cSegsUsed);
                pMemBuf2 = pSG2->aSegs[i2].pv;
                cbLength2 = pSG2->aSegs[i2].cb;
                i2++;
            }

            ucbLength2Match = MIN(ucbMatch, cbLength1);
            ucbLength2Match = MIN(ucbLength2Match, cbLength2);

            if (memcmp(pMemBuf1, pMemBuf2, ucbLength2Match))
            {
                bMatch = false;
                AssertFailed();
                break;
            }
            ucbMatch -= ucbLength2Match;
            cbLength1 -= ucbLength2Match;
            cbLength2 -= ucbLength2Match;
        } while (ucbMatch);
    }

    if (bMatch && !bCompleteMatch)
    {
        /* check that the packets fully match */
        DBG_CHECK_SGS(pSG1, pSG2);
    }
    return bMatch;
}
#  endif
# endif
#endif

static void nemuNetFltWinFiniNetFltBase()
{
    do
    {
        nemuNetFltDeleteGlobals(&g_NemuNetFltGlobals);

        /*
         * Undo the work done during start (in reverse order).
         */
        memset(&g_NemuNetFltGlobals, 0, sizeof(g_NemuNetFltGlobals));

        RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
        RTLogDestroy(RTLogSetDefaultInstance(NULL));

        RTR0Term();
    } while (0);
}

static int nemuNetFltWinTryFiniIdc()
{
    int rc;

    nemuNetFltWinStopInitIdcProbing();

    if (g_bNemuIdcInitialized)
    {
        rc = nemuNetFltTryDeleteIdc(&g_NemuNetFltGlobals);
        if (RT_SUCCESS(rc))
        {
            g_bNemuIdcInitialized = false;
        }
    }
    else
    {
        rc = VINF_SUCCESS;
    }
    return rc;

}

static int nemuNetFltWinFiniNetFlt()
{
    int rc = nemuNetFltWinTryFiniIdc();
    if (RT_SUCCESS(rc))
    {
        nemuNetFltWinFiniNetFltBase();
    }
    return rc;
}

/**
 * base netflt initialization
 */
static int nemuNetFltWinInitNetFltBase()
{
    int rc;

    do
    {
        Assert(!g_bNemuIdcInitialized);

        rc = RTR0Init(0);
        if (!RT_SUCCESS(rc))
        {
            break;
        }

        memset(&g_NemuNetFltGlobals, 0, sizeof(g_NemuNetFltGlobals));
        rc = nemuNetFltInitGlobals(&g_NemuNetFltGlobals);
        if (!RT_SUCCESS(rc))
        {
            RTR0Term();
            break;
        }
    }while (0);

    return rc;
}

/**
 * initialize IDC
 */
static int nemuNetFltWinInitIdc()
{
    int rc;

    do
    {
        if (g_bNemuIdcInitialized)
        {
            rc = VINF_ALREADY_INITIALIZED;
            break;
        }

        /*
         * connect to the support driver.
         *
         * This will call back nemuNetFltOsOpenSupDrv (and maybe nemuNetFltOsCloseSupDrv)
         * for establishing the connect to the support driver.
         */
        rc = nemuNetFltInitIdc(&g_NemuNetFltGlobals);
        if (!RT_SUCCESS(rc))
        {
            break;
        }

        g_bNemuIdcInitialized = true;
    } while (0);

    return rc;
}

static VOID nemuNetFltWinInitIdcProbingWorker(PVOID pvContext)
{
    PINIT_IDC_INFO pInitIdcInfo = (PINIT_IDC_INFO)pvContext;
    int rc = nemuNetFltWinInitIdc();
    if (RT_FAILURE(rc))
    {
        bool bInterupted = ASMAtomicUoReadBool(&pInitIdcInfo->bStop);
        if (!bInterupted)
        {
            RTThreadSleep(1000); /* 1 s */
            bInterupted = ASMAtomicUoReadBool(&pInitIdcInfo->bStop);
            if (!bInterupted)
            {
                nemuNetFltWinJobEnqueueJob(&g_NemuJobQueue, &pInitIdcInfo->Job, false);
                return;
            }
        }

        /* it's interrupted */
        rc = VERR_INTERRUPTED;
    }

    ASMAtomicUoWriteS32(&pInitIdcInfo->rc, rc);
    KeSetEvent(&pInitIdcInfo->hCompletionEvent, 0, FALSE);
}

static int nemuNetFltWinStopInitIdcProbing()
{
    if (!g_NemuInitIdcInfo.bInitialized)
        return VERR_INVALID_STATE;

    ASMAtomicUoWriteBool(&g_NemuInitIdcInfo.bStop, true);
    KeWaitForSingleObject(&g_NemuInitIdcInfo.hCompletionEvent, Executive, KernelMode, FALSE, NULL);

    return g_NemuInitIdcInfo.rc;
}

static int nemuNetFltWinStartInitIdcProbing()
{
    Assert(!g_bNemuIdcInitialized);
    KeInitializeEvent(&g_NemuInitIdcInfo.hCompletionEvent, NotificationEvent, FALSE);
    g_NemuInitIdcInfo.bStop = false;
    g_NemuInitIdcInfo.bInitialized = true;
    nemuNetFltWinJobInit(&g_NemuInitIdcInfo.Job, nemuNetFltWinInitIdcProbingWorker, &g_NemuInitIdcInfo, false);
    nemuNetFltWinJobEnqueueJob(&g_NemuJobQueue, &g_NemuInitIdcInfo.Job, false);
    return VINF_SUCCESS;
}

static int nemuNetFltWinInitNetFlt()
{
    int rc;

    do
    {
        rc = nemuNetFltWinInitNetFltBase();
        if (RT_FAILURE(rc))
        {
            AssertFailed();
            break;
        }

        /*
         * connect to the support driver.
         *
         * This will call back nemuNetFltOsOpenSupDrv (and maybe nemuNetFltOsCloseSupDrv)
         * for establishing the connect to the support driver.
         */
        rc = nemuNetFltWinInitIdc();
        if (RT_FAILURE(rc))
        {
            AssertFailed();
            nemuNetFltWinFiniNetFltBase();
            break;
        }
    } while (0);

    return rc;
}

/* detach*/
static int nemuNetFltWinDeleteInstance(PNEMUNETFLTINS pThis)
{
    LogFlow(("nemuNetFltWinDeleteInstance: pThis=0x%p \n", pThis));

    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);
    Assert(pThis);
    Assert(pThis->fDisconnectedFromHost);
    Assert(!pThis->fRediscoveryPending);
    Assert(pThis->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE);
#ifndef NEMUNETADP
    Assert(pThis->u.s.WinIf.PtState.OpState == kNemuNetDevOpState_Deinitialized);
    Assert(!pThis->u.s.WinIf.hBinding);
#endif
    Assert(pThis->u.s.WinIf.MpState.OpState == kNemuNetDevOpState_Deinitialized);
#ifndef NEMUNETFLT_NO_PACKET_QUEUE
    Assert(!pThis->u.s.PacketQueueWorker.pSG);
#endif

    RTSemMutexDestroy(pThis->u.s.hWinIfMutex);

    nemuNetFltWinDrvDereference();

    return VINF_SUCCESS;
}

static NDIS_STATUS nemuNetFltWinDisconnectIt(PNEMUNETFLTINS pInstance)
{
#ifndef NEMUNETFLT_NO_PACKET_QUEUE
    nemuNetFltWinQuFiniPacketQueue(pInstance);
#endif
    return NDIS_STATUS_SUCCESS;
}

/* detach*/
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinDetachFromInterface(PNEMUNETFLTINS pNetFlt, bool bOnUnbind)
{
    NDIS_STATUS Status;
    int rc;
    LogFlow((__FUNCTION__": pThis=%0xp\n", pNetFlt));

    Assert(KeGetCurrentIrql() < DISPATCH_LEVEL);
    Assert(pNetFlt);

    /* paranoia to ensure the instance is not removed while we're waiting on the mutex
     * in case ndis does something unpredictable, e.g. calls our miniport halt independently
     * from protocol unbind and concurrently with it*/
    nemuNetFltRetain(pNetFlt, false);

    rc = RTSemMutexRequest(pNetFlt->u.s.hWinIfMutex, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        Assert(nemuNetFltWinGetWinIfState(pNetFlt) == kNemuWinIfState_Connected);
        Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Initialized);
#ifndef NEMUNETADP
        Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.PtState) == kNemuNetDevOpState_Initialized);
#endif
        if (nemuNetFltWinGetWinIfState(pNetFlt) == kNemuWinIfState_Connected)
        {
            nemuNetFltWinSetWinIfState(pNetFlt, kNemuWinIfState_Disconnecting);
#ifndef NEMUNETADP
            Status = nemuNetFltWinPtDoUnbinding(pNetFlt, bOnUnbind);
#else
            Status = nemuNetFltWinMpDoDeinitialization(pNetFlt);
#endif
            Assert(Status == NDIS_STATUS_SUCCESS);

            nemuNetFltWinSetWinIfState(pNetFlt, kNemuWinIfState_Disconnected);
            Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
#ifndef NEMUNETADP
            Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.PtState) == kNemuNetDevOpState_Deinitialized);
#endif
            nemuNetFltWinPtFiniWinIf(&pNetFlt->u.s.WinIf);

            /* we're unbinding, make an unbind-related release */
            nemuNetFltRelease(pNetFlt, false);
        }
        else
        {
            AssertBreakpoint();
#ifndef NEMUNETADP
            pNetFlt->u.s.WinIf.OpenCloseStatus = NDIS_STATUS_FAILURE;
#endif
            if (!bOnUnbind)
            {
                nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
            }
            Status = NDIS_STATUS_FAILURE;
        }
        RTSemMutexRelease(pNetFlt->u.s.hWinIfMutex);
    }
    else
    {
        AssertBreakpoint();
        Status = NDIS_STATUS_FAILURE;
    }

    /* release for the retain we made before waining on the mutex */
    nemuNetFltRelease(pNetFlt, false);

    return Status;
}


/**
 * Checks if the host (not us) has put the adapter in promiscuous mode.
 *
 * @returns true if promiscuous, false if not.
 * @param   pThis               The instance.
 */
static bool nemuNetFltWinIsPromiscuous2(PNEMUNETFLTINS pThis)
{
#ifndef NEMUNETADP
    if (NEMUNETFLT_PROMISCUOUS_SUPPORTED(pThis))
    {
        bool bPromiscuous;
        if (!nemuNetFltWinReferenceWinIf(pThis))
            return false;

        bPromiscuous = (pThis->u.s.WinIf.fUpperProtocolSetFilter & NDIS_PACKET_TYPE_PROMISCUOUS) == NDIS_PACKET_TYPE_PROMISCUOUS;
            /*nemuNetFltWinIsPromiscuous(pAdapt);*/

        nemuNetFltWinDereferenceWinIf(pThis);
        return bPromiscuous;
    }
    return false;
#else
    return true;
#endif
}


/**
 * Report the MAC address, promiscuous mode setting, GSO capabilities and
 * no-preempt destinations to the internal network.
 *
 * Does nothing if we're not currently connected to an internal network.
 *
 * @param   pThis           The instance data.
 */
static void nemuNetFltWinReportStuff(PNEMUNETFLTINS pThis)
{
    /** @todo Keep these up to date, esp. the promiscuous mode bit. */
    if (pThis->pSwitchPort
        && nemuNetFltTryRetainBusyNotDisconnected(pThis))
    {
        pThis->pSwitchPort->pfnReportMacAddress(pThis->pSwitchPort, &pThis->u.s.MacAddr);
        pThis->pSwitchPort->pfnReportPromiscuousMode(pThis->pSwitchPort,
                                                     nemuNetFltWinIsPromiscuous2(pThis));
        pThis->pSwitchPort->pfnReportGsoCapabilities(pThis->pSwitchPort, 0,
                                                     INTNETTRUNKDIR_WIRE | INTNETTRUNKDIR_HOST);
        /** @todo We should be able to do pfnXmit at DISPATCH_LEVEL... */
        pThis->pSwitchPort->pfnReportNoPreemptDsts(pThis->pSwitchPort, 0 /* none */);
        nemuNetFltRelease(pThis, true /*fBusy*/);
    }
}

/**
 * Worker for nemuNetFltWinAttachToInterface.
 *
 * @param   pAttachInfo     Structure for communicating with
 *                          nemuNetFltWinAttachToInterface.
 */
static void nemuNetFltWinAttachToInterfaceWorker(PATTACH_INFO pAttachInfo)
{
    PNEMUNETFLTINS pThis = pAttachInfo->pNetFltIf;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    int rc;

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    /* to ensure we're not removed while we're here */
    nemuNetFltRetain(pThis, false);

    rc = RTSemMutexRequest(pThis->u.s.hWinIfMutex, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        Assert(nemuNetFltWinGetWinIfState(pThis) == kNemuWinIfState_Disconnected);
        Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
#ifndef NEMUNETADP
        Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.PtState) == kNemuNetDevOpState_Deinitialized);
#endif
        if (nemuNetFltWinGetWinIfState(pThis) == kNemuWinIfState_Disconnected)
        {
            if (pAttachInfo->fRediscovery)
            {
                /* rediscovery means adaptor bind is performed while intnet is already using it
                 * i.e. adaptor was unbound while being used by intnet and now being bound back again */
                Assert(((NEMUNETFTLINSSTATE)ASMAtomicUoReadU32((uint32_t volatile *)&pThis->enmState)) == kNemuNetFltInsState_Connected);
            }
#ifndef NEMUNETADP
            Status = nemuNetFltWinPtInitWinIf(&pThis->u.s.WinIf, pAttachInfo->pCreateContext->pOurName);
#else
            Status = nemuNetFltWinPtInitWinIf(&pThis->u.s.WinIf);
#endif
            if (Status == NDIS_STATUS_SUCCESS)
            {
                nemuNetFltWinSetWinIfState(pThis, kNemuWinIfState_Connecting);

#ifndef NEMUNETADP
                Status = nemuNetFltWinPtDoBinding(pThis, pAttachInfo->pCreateContext->pOurName, pAttachInfo->pCreateContext->pBindToName);
#else
                Status = nemuNetFltWinMpDoInitialization(pThis, pAttachInfo->pCreateContext->hMiniportAdapter, pAttachInfo->pCreateContext->hWrapperConfigurationContext);
#endif
                if (Status == NDIS_STATUS_SUCCESS)
                {
                    if (!pAttachInfo->fRediscovery)
                    {
                        nemuNetFltWinDrvReference();
                    }
#ifndef NEMUNETADP
                    if (pThis->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS)
#endif
                    {
                        nemuNetFltWinSetWinIfState(pThis, kNemuWinIfState_Connected);
#ifndef NEMUNETADP
                        Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.PtState) == kNemuNetDevOpState_Initialized);
#endif
                        /* 4. mark as connected */
                        RTSpinlockAcquire(pThis->hSpinlock);
                        ASMAtomicUoWriteBool(&pThis->fDisconnectedFromHost, false);
                        RTSpinlockRelease(pThis->hSpinlock);

                        pAttachInfo->Status = VINF_SUCCESS;
                        pAttachInfo->pCreateContext->Status = NDIS_STATUS_SUCCESS;

                        RTSemMutexRelease(pThis->u.s.hWinIfMutex);

                        nemuNetFltRelease(pThis, false);

                        /* 5. Report MAC address, promiscuousness and GSO capabilities. */
                        nemuNetFltWinReportStuff(pThis);

                        return;
                    }
                    AssertBreakpoint();

                    if (!pAttachInfo->fRediscovery)
                    {
                        nemuNetFltWinDrvDereference();
                    }
#ifndef NEMUNETADP
                    nemuNetFltWinPtDoUnbinding(pThis, true);
#else
                    nemuNetFltWinMpDoDeinitialization(pThis);
#endif
                }
                AssertBreakpoint();
                nemuNetFltWinPtFiniWinIf(&pThis->u.s.WinIf);
            }
            AssertBreakpoint();
            nemuNetFltWinSetWinIfState(pThis, kNemuWinIfState_Disconnected);
            Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
#ifndef NEMUNETADP
            Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.PtState) == kNemuNetDevOpState_Deinitialized);
#endif
        }
        AssertBreakpoint();

        pAttachInfo->Status = VERR_GENERAL_FAILURE;
        pAttachInfo->pCreateContext->Status = Status;
        RTSemMutexRelease(pThis->u.s.hWinIfMutex);
    }
    else
    {
        AssertBreakpoint();
        pAttachInfo->Status = rc;
    }

    nemuNetFltRelease(pThis, false);

    return;
}

/**
 * Common code for nemuNetFltOsInitInstance and
 * nemuNetFltOsMaybeRediscovered.
 *
 * @returns IPRT status code.
 * @param   pThis           The instance.
 * @param   fRediscovery    True if nemuNetFltOsMaybeRediscovered is calling,
 *                          false if it's nemuNetFltOsInitInstance.
 */
static int nemuNetFltWinAttachToInterface(PNEMUNETFLTINS pThis, void * pContext, bool fRediscovery)
{
    ATTACH_INFO Info;
    Info.pNetFltIf = pThis;
    Info.fRediscovery = fRediscovery;
    Info.pCreateContext = (PCREATE_INSTANCE_CONTEXT)pContext;

    nemuNetFltWinAttachToInterfaceWorker(&Info);

    return Info.Status;
}
static NTSTATUS nemuNetFltWinPtDevDispatch(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pIrpSl = IoGetCurrentIrpStackLocation(pIrp);;
    NTSTATUS Status = STATUS_SUCCESS;

    switch (pIrpSl->MajorFunction)
    {
        case IRP_MJ_DEVICE_CONTROL:
            Status = STATUS_NOT_SUPPORTED;
            break;
        case IRP_MJ_CREATE:
        case IRP_MJ_CLEANUP:
        case IRP_MJ_CLOSE:
            break;
        default:
            Assert(0);
            break;
    }

    pIrp->IoStatus.Status = Status;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return Status;
}

static NDIS_STATUS nemuNetFltWinDevCreate(PNEMUNETFLTGLOBALS_WIN pGlobals)
{
    NDIS_STRING DevName, LinkName;
    PDRIVER_DISPATCH aMajorFunctions[IRP_MJ_MAXIMUM_FUNCTION+1];
    NdisInitUnicodeString(&DevName, NEMUNETFLT_NAME_DEVICE);
    NdisInitUnicodeString(&LinkName, NEMUNETFLT_NAME_LINK);

    Assert(!pGlobals->hDevice);
    Assert(!pGlobals->pDevObj);
    NdisZeroMemory(aMajorFunctions, sizeof (aMajorFunctions));
    aMajorFunctions[IRP_MJ_CREATE] = nemuNetFltWinPtDevDispatch;
    aMajorFunctions[IRP_MJ_CLEANUP] = nemuNetFltWinPtDevDispatch;
    aMajorFunctions[IRP_MJ_CLOSE] = nemuNetFltWinPtDevDispatch;
    aMajorFunctions[IRP_MJ_DEVICE_CONTROL] = nemuNetFltWinPtDevDispatch;

    NDIS_STATUS Status = NdisMRegisterDevice(pGlobals->Mp.hNdisWrapper,
              &DevName, &LinkName,
              aMajorFunctions,
              &pGlobals->pDevObj,
              &pGlobals->hDevice);
    Assert(Status == NDIS_STATUS_SUCCESS);
    return Status;
}

static NDIS_STATUS nemuNetFltWinDevDestroy(PNEMUNETFLTGLOBALS_WIN pGlobals)
{
    Assert(pGlobals->hDevice);
    Assert(pGlobals->pDevObj);
    NDIS_STATUS Status = NdisMDeregisterDevice(pGlobals->hDevice);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        pGlobals->hDevice = NULL;
        pGlobals->pDevObj = NULL;
    }
    return Status;
}

static NDIS_STATUS nemuNetFltWinDevCreateReference(PNEMUNETFLTGLOBALS_WIN pGlobals)
{
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
    NDIS_STATUS Status = KeWaitForSingleObject(&pGlobals->SynchEvent, Executive, KernelMode, FALSE, NULL);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Assert(pGlobals->cDeviceRefs >= 0);
        if (++pGlobals->cDeviceRefs == 1)
        {
            Status = nemuNetFltWinDevCreate(pGlobals);
            if (Status == NDIS_STATUS_SUCCESS)
            {
                ObReferenceObject(pGlobals->pDevObj);
            }
        }
        else
        {
            Status = NDIS_STATUS_SUCCESS;
        }
        KeSetEvent(&pGlobals->SynchEvent, 0, FALSE);
    }
    else
    {
        /* should never happen actually */
        Assert(0);
        Status = NDIS_STATUS_FAILURE;
    }
    return Status;
}

static NDIS_STATUS nemuNetFltWinDevDereference(PNEMUNETFLTGLOBALS_WIN pGlobals)
{
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
    NDIS_STATUS Status = KeWaitForSingleObject(&pGlobals->SynchEvent, Executive, KernelMode, FALSE, NULL);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Assert(pGlobals->cDeviceRefs > 0);
        if (!(--pGlobals->cDeviceRefs))
        {
            ObDereferenceObject(pGlobals->pDevObj);
            Status = nemuNetFltWinDevDestroy(pGlobals);
        }
        else
        {
            Status = NDIS_STATUS_SUCCESS;
        }
        KeSetEvent(&pGlobals->SynchEvent, 0, FALSE);
    }
    else
    {
        /* should never happen actually */
        Assert(0);
        Status = NDIS_STATUS_FAILURE;
    }
    return Status;
}

/* reference the driver module to prevent driver unload */
DECLHIDDEN(void) nemuNetFltWinDrvReference()
{
    nemuNetFltWinDevCreateReference(&g_NemuNetFltGlobalsWin);
}

/* dereference the driver module to prevent driver unload */
DECLHIDDEN(void) nemuNetFltWinDrvDereference()
{
    nemuNetFltWinDevDereference(&g_NemuNetFltGlobalsWin);
}

/*
 *
 * The OS specific interface definition
 *
 */


bool nemuNetFltOsMaybeRediscovered(PNEMUNETFLTINS pThis)
{
    /* AttachToInterface true if disconnected */
    return !ASMAtomicUoReadBool(&pThis->fDisconnectedFromHost);
}

int nemuNetFltPortOsXmit(PNEMUNETFLTINS pThis, void *pvIfData, PINTNETSG pSG, uint32_t fDst)
{
    int rc = VINF_SUCCESS;
    uint32_t cRefs = 0;
#ifndef NEMUNETADP
    if (fDst & INTNETTRUNKDIR_WIRE)
    {
        cRefs++;
    }
    if (fDst & INTNETTRUNKDIR_HOST)
    {
        cRefs++;
    }
#else
    if (fDst & INTNETTRUNKDIR_WIRE || fDst & INTNETTRUNKDIR_HOST)
    {
        cRefs = 1;
    }
#endif

    AssertReturn(cRefs, VINF_SUCCESS);

    if (!nemuNetFltWinIncReferenceWinIf(pThis, cRefs))
    {
        return VERR_GENERAL_FAILURE;
    }
#ifndef NEMUNETADP
    if (fDst & INTNETTRUNKDIR_WIRE)
    {
        PNDIS_PACKET pPacket;

        pPacket = nemuNetFltWinNdisPacketFromSG(pThis, pSG, NULL /*pBufToFree*/,
                                                true /*fToWire*/, true /*fCopyMemory*/);

        if (pPacket)
        {
            NDIS_STATUS fStatus;

#ifndef NEMU_LOOPBACK_USEFLAGS
            /* force "don't loopback" flags to prevent loopback branch invocation in any case
             * to avoid ndis misbehave */
            NdisGetPacketFlags(pPacket) |= g_NemuNetFltGlobalsWin.fPacketDontLoopBack;
#else
            /* this is done by default in nemuNetFltWinNdisPacketFromSG */
#endif

#if defined(DEBUG_NETFLT_PACKETS) || !defined(NEMU_LOOPBACK_USEFLAGS)
            nemuNetFltWinLbPutSendPacket(pThis, pPacket, true /* bFromIntNet */);
#endif
            NdisSend(&fStatus, pThis->u.s.WinIf.hBinding, pPacket);
            if (fStatus != NDIS_STATUS_PENDING)
            {
#if defined(DEBUG_NETFLT_PACKETS) || !defined(NEMU_LOOPBACK_USEFLAGS)
                /* the status is NOT pending, complete the packet */
                bool bTmp = nemuNetFltWinLbRemoveSendPacket(pThis, pPacket);
                Assert(bTmp);
#endif
                if (!NT_SUCCESS(fStatus))
                {
                    /* TODO: convert status to VERR_xxx */
                    rc = VERR_GENERAL_FAILURE;
                }

                nemuNetFltWinFreeSGNdisPacket(pPacket, true);
            }
            else
            {
                /* pending, dereference on packet complete */
                cRefs--;
            }
        }
        else
        {
            AssertFailed();
            rc = VERR_NO_MEMORY;
        }
    }
#endif

#ifndef NEMUNETADP
    if (fDst & INTNETTRUNKDIR_HOST)
#else
    if (cRefs)
#endif
    {
        PNDIS_PACKET pPacket = nemuNetFltWinNdisPacketFromSG(pThis, pSG, NULL /*pBufToFree*/,
                                                             false /*fToWire*/, true /*fCopyMemory*/);
        if (pPacket)
        {
            NdisMIndicateReceivePacket(pThis->u.s.WinIf.hMiniport, &pPacket, 1);
            cRefs--;
#ifdef NEMUNETADP
            STATISTIC_INCREASE(pThis->u.s.WinIf.cRxSuccess);
#endif
        }
        else
        {
            AssertFailed();
#ifdef NEMUNETADP
            STATISTIC_INCREASE(pThis->u.s.WinIf.cRxError);
#endif
            rc = VERR_NO_MEMORY;
        }
    }

    Assert(cRefs <= 2);

    if (cRefs)
    {
        nemuNetFltWinDecReferenceWinIf(pThis, cRefs);
    }

    return rc;
}

void nemuNetFltPortOsSetActive(PNEMUNETFLTINS pThis, bool fActive)
{
#ifndef NEMUNETADP
    NDIS_STATUS Status;
#endif
    /* we first wait for all pending ops to complete
     * this might include all packets queued for processing */
    for (;;)
    {
        if (fActive)
        {
            if (!pThis->u.s.cModePassThruRefs)
            {
                break;
            }
        }
        else
        {
            if (!pThis->u.s.cModeNetFltRefs)
            {
                break;
            }
        }
        nemuNetFltWinSleep(2);
    }

    if (!nemuNetFltWinReferenceWinIf(pThis))
        return;
#ifndef NEMUNETADP

    if (fActive)
    {
#ifdef DEBUG_misha
        NDIS_PHYSICAL_MEDIUM PhMedium;
        bool bPromiscSupported;

        Status = nemuNetFltWinQueryPhysicalMedium(pThis, &PhMedium);
        if (Status != NDIS_STATUS_SUCCESS)
        {

            LogRel(("nemuNetFltWinQueryPhysicalMedium failed, Status (0x%x), setting medium to NdisPhysicalMediumUnspecified\n", Status));
            Assert(Status == NDIS_STATUS_NOT_SUPPORTED);
            if (Status != NDIS_STATUS_NOT_SUPPORTED)
            {
                LogRel(("nemuNetFltWinQueryPhysicalMedium failed, Status (0x%x), setting medium to NdisPhysicalMediumUnspecified\n", Status));
            }
            PhMedium = NdisPhysicalMediumUnspecified;
        }
        else
        {
            LogRel(("(SUCCESS) nemuNetFltWinQueryPhysicalMedium SUCCESS\n"));
        }

        bPromiscSupported = (!(PhMedium == NdisPhysicalMediumWirelessWan
                        || PhMedium == NdisPhysicalMediumWirelessLan
                        || PhMedium == NdisPhysicalMediumNative802_11
                        || PhMedium == NdisPhysicalMediumBluetooth
                        /*|| PhMedium == NdisPhysicalMediumWiMax */
                        ));

        Assert(bPromiscSupported == NEMUNETFLT_PROMISCUOUS_SUPPORTED(pThis));
#endif
    }

    if (NEMUNETFLT_PROMISCUOUS_SUPPORTED(pThis))
    {
        Status = nemuNetFltWinSetPromiscuous(pThis, fActive);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            LogRel(("nemuNetFltWinSetPromiscuous failed, Status (0x%x), fActive (%d)\n", Status, fActive));
            AssertFailed();
        }
    }
#else
# ifdef NEMUNETADP_REPORT_DISCONNECTED
    if (fActive)
    {
        NdisMIndicateStatus(pThis->u.s.WinIf.hMiniport,
                                 NDIS_STATUS_MEDIA_CONNECT,
                                 (PVOID)NULL,
                                 0);
    }
    else
    {
        NdisMIndicateStatus(pThis->u.s.WinIf.hMiniport,
                                 NDIS_STATUS_MEDIA_DISCONNECT,
                                 (PVOID)NULL,
                                 0);
    }
#else
    if (fActive)
    {
        /* indicate status change to make the ip settings be re-picked for dhcp */
        NdisMIndicateStatus(pThis->u.s.WinIf.hMiniport,
                                 NDIS_STATUS_MEDIA_DISCONNECT,
                                 (PVOID)NULL,
                                 0);

        NdisMIndicateStatus(pThis->u.s.WinIf.hMiniport,
                                 NDIS_STATUS_MEDIA_CONNECT,
                                 (PVOID)NULL,
                                 0);
    }
# endif
#endif
    nemuNetFltWinDereferenceWinIf(pThis);

    return;
}

#ifndef NEMUNETADP

DECLINLINE(bool) nemuNetFltWinIsAddrLinkLocal4(PCRTNETADDRIPV4 pAddr)
{
    return (pAddr->s.Lo == 0xfea9); /* 169.254 */
}

DECLINLINE(bool) nemuNetFltWinIsAddrLinkLocal6(PCRTNETADDRIPV6 pAddr)
{
    return ((pAddr->au8[0] == 0xfe) && ((pAddr->au8[1] & 0xc0) == 0x80));
}

void nemuNetFltWinNotifyHostAddress(PTA_ADDRESS pAddress, bool fAdded)
{
    void *pvAddr = NULL;
    INTNETADDRTYPE enmAddrType = kIntNetAddrType_Invalid;

    LogFlow(("==>nemuNetFltWinNotifyHostAddress: AddrType=%d %s\n",
             pAddress->AddressType, fAdded ? "added" : "deleted"));
    if (pAddress->AddressType == TDI_ADDRESS_TYPE_IP)
    {
        PTDI_ADDRESS_IP pTdiAddrIp = (PTDI_ADDRESS_IP)pAddress->Address;
        /*
         * Note that we do not get loopback addresses here. If we did we should
         * have checked and ignored them too.
         */
        if (!nemuNetFltWinIsAddrLinkLocal4((PCRTNETADDRIPV4)(&pTdiAddrIp->in_addr)))
        {
            pvAddr = &pTdiAddrIp->in_addr;
            enmAddrType = kIntNetAddrType_IPv4;
        }
        else
            Log2(("nemuNetFltWinNotifyHostAddress: ignoring link-local address %RTnaipv4\n",
                  pTdiAddrIp->in_addr));
    }
    else if (pAddress->AddressType == TDI_ADDRESS_TYPE_IP6)
    {
        PTDI_ADDRESS_IP6 pTdiAddrIp6 = (PTDI_ADDRESS_IP6)pAddress->Address;
        if (!nemuNetFltWinIsAddrLinkLocal6((PCRTNETADDRIPV6)(pTdiAddrIp6->sin6_addr)))
        {
            pvAddr = pTdiAddrIp6->sin6_addr;
            enmAddrType = kIntNetAddrType_IPv6;
        }
        else
            Log2(("nemuNetFltWinNotifyHostAddress: ignoring link-local address %RTnaipv6\n",
                  pTdiAddrIp6->sin6_addr));
    }
    else
    {
        Log2(("nemuNetFltWinNotifyHostAddress: ignoring irrelevant address type %d\n",
              pAddress->AddressType));
        LogFlow(("<==nemuNetFltWinNotifyHostAddress\n"));
        return;
    }
    if (pvAddr)
    {
        NdisAcquireSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);
        /* At this point the list must contain at least one element. */
        PNEMUNETFLTWIN pFilter   = NULL;
        PNEMUNETFLTINS pInstance = NULL;
        RTListForEach(&g_NemuNetFltGlobalsWin.listFilters, pFilter, NEMUNETFLTWIN, node)
        {
            pInstance = RT_FROM_MEMBER(pFilter, NEMUNETFLTINS, u.s.WinIf);
            if (nemuNetFltWinReferenceWinIf(pInstance))
            {
                if (pInstance->pSwitchPort && pInstance->pSwitchPort->pfnNotifyHostAddress)
                    break;
                nemuNetFltWinDereferenceWinIf(pInstance);
            }
            else
                Log2(("nemuNetFltWinNotifyHostAddress: failed to retain filter instance %p\n", pInstance));
            pInstance = NULL;
        }
        NdisReleaseSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);
        if (pInstance)
        {
            if (enmAddrType == kIntNetAddrType_IPv4)
                Log2(("nemuNetFltWin%sAddressHandler: %RTnaipv4\n",
                      fAdded ? "Add" : "Del", *(PCRTNETADDRIPV4)pvAddr));
            else
                Log2(("nemuNetFltWin%sAddressHandler: %RTnaipv6\n",
                      fAdded ? "Add" : "Del", pvAddr));
            pInstance->pSwitchPort->pfnNotifyHostAddress(pInstance->pSwitchPort, fAdded,
                                                         enmAddrType, pvAddr);
            nemuNetFltWinDereferenceWinIf(pInstance);
        }
        else
            Log2(("nemuNetFltWinNotifyHostAddress: no filters require notification\n"));
    }
    LogFlow(("<==nemuNetFltWinNotifyHostAddress\n"));
}

void nemuNetFltWinAddAddressHandler(PTA_ADDRESS Address,
                                    PUNICODE_STRING DeviceName,
                                    PTDI_PNP_CONTEXT Context)
{
    nemuNetFltWinNotifyHostAddress(Address, true);
}

void nemuNetFltWinDelAddressHandler(PTA_ADDRESS Address,
                                    PUNICODE_STRING DeviceName,
                                    PTDI_PNP_CONTEXT Context)
{
    nemuNetFltWinNotifyHostAddress(Address, false);
}

void nemuNetFltWinRegisterIpAddrNotifier(PNEMUNETFLTINS pThis)
{
    LogFlow(("==>nemuNetFltWinRegisterIpAddrNotifier: instance=%p pThis->pSwitchPort=%p pThis->pSwitchPort->pfnNotifyHostAddress=%p\n",
             pThis, pThis->pSwitchPort, pThis->pSwitchPort ? pThis->pSwitchPort->pfnNotifyHostAddress : NULL));
    if (pThis->pSwitchPort && pThis->pSwitchPort->pfnNotifyHostAddress)
    {
        NdisAcquireSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);
        bool fRegisterHandlers = RTListIsEmpty(&g_NemuNetFltGlobalsWin.listFilters);
        RTListPrepend(&g_NemuNetFltGlobalsWin.listFilters, &pThis->u.s.WinIf.node);
        NdisReleaseSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);

        if (fRegisterHandlers)
        {
            TDI_CLIENT_INTERFACE_INFO Info;
            UNICODE_STRING ClientName = RTL_CONSTANT_STRING(L"NemuNetFlt");
            memset(&Info, 0, sizeof(Info));
            Info.MajorTdiVersion = 2;
            Info.MinorTdiVersion = 0;
            Info.ClientName = &ClientName;
            Info.AddAddressHandlerV2 = nemuNetFltWinAddAddressHandler;
            Info.DelAddressHandlerV2 = nemuNetFltWinDelAddressHandler;
            Assert(!g_NemuNetFltGlobalsWin.hNotifier);
            NTSTATUS Status = TdiRegisterPnPHandlers(&Info, sizeof(Info), &g_NemuNetFltGlobalsWin.hNotifier);
            Log2(("nemuNetFltWinRegisterIpAddrNotifier: TdiRegisterPnPHandlers returned %d\n", Status));
        }
        else
            Log2(("nemuNetFltWinRegisterIpAddrNotifier: already registed\n"));
    }
    else
        Log2(("nemuNetFltWinRegisterIpAddrNotifier: this instance does not require notifications, ignoring...\n"));
    LogFlow(("<==nemuNetFltWinRegisterIpAddrNotifier: notifier=%p\n", g_NemuNetFltGlobalsWin.hNotifier));
}

void nemuNetFltWinUnregisterIpAddrNotifier(PNEMUNETFLTINS pThis)
{
    LogFlow(("==>nemuNetFltWinUnregisterIpAddrNotifier: notifier=%p\n", g_NemuNetFltGlobalsWin.hNotifier));
    if (pThis->pSwitchPort && pThis->pSwitchPort->pfnNotifyHostAddress)
    {
        NdisAcquireSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);
        /* At this point the list must contain at least one element. */
        Assert(!RTListIsEmpty(&g_NemuNetFltGlobalsWin.listFilters));
        RTListNodeRemove(&pThis->u.s.WinIf.node);
        HANDLE hNotifier = NULL;
        if (RTListIsEmpty(&g_NemuNetFltGlobalsWin.listFilters))
        {
            /*
             * The list has become empty, so we need to deregister handlers. We
             * grab hNotifier and reset it while still holding the lock. This
             * guaranties that we won't interfere with setting it in
             * nemuNetFltWinRegisterIpAddrNotifier(). It is inconceivable that
             * nemuNetFltWinUnregisterIpAddrNotifier() will be called for the
             * same filter instance while it is still being processed by
             * nemuNetFltWinRegisterIpAddrNotifier(). This would require trunk
             * destruction in the middle of its creation. It is possible that
             * nemuNetFltWinUnregisterIpAddrNotifier() is called for another
             * filter instance, but in such case we won't even get here as the
             * list won't be empty.
             */
            hNotifier = g_NemuNetFltGlobalsWin.hNotifier;
            g_NemuNetFltGlobalsWin.hNotifier = NULL;
        }
        NdisReleaseSpinLock(&g_NemuNetFltGlobalsWin.lockFilters);
        if (hNotifier)
        {
            NTSTATUS Status = TdiDeregisterPnPHandlers(hNotifier);
            Log2(("nemuNetFltWinUnregisterIpAddrNotifier: TdiDeregisterPnPHandlers(%p) returned %d\n",
                  hNotifier, Status));
        }
        else
            Log2(("nemuNetFltWinUnregisterIpAddrNotifier: filters remain, do not deregister handlers yet\n"));
    }
    else
        Log2(("nemuNetFltWinUnregisterIpAddrNotifier: this instance did not require notifications, ignoring...\n"));
    LogFlow(("<==nemuNetFltWinUnregisterIpAddrNotifier\n"));
}
#else /* NEMUNETADP */
#define nemuNetFltWinRegisterIpAddrNotifier(x)
#define nemuNetFltWinUnregisterIpAddrNotifier(x)
#endif /* NEMUNETADP */

int nemuNetFltOsDisconnectIt(PNEMUNETFLTINS pThis)
{
    NDIS_STATUS Status = nemuNetFltWinDisconnectIt(pThis);
    Log2(("nemuNetFltOsDisconnectIt: pThis=%p pThis->pSwitchPort=%p pThis->pSwitchPort->pfnNotifyHostAddress=%p\n",
          pThis, pThis->pSwitchPort, pThis->pSwitchPort ? pThis->pSwitchPort->pfnNotifyHostAddress : NULL));
    nemuNetFltWinUnregisterIpAddrNotifier(pThis);
    return Status == NDIS_STATUS_SUCCESS ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
}

static void nemuNetFltWinConnectItWorker(PVOID pvContext)
{
    PWORKER_INFO pInfo = (PWORKER_INFO)pvContext;
#if !defined(NEMUNETADP) || !defined(NEMUNETFLT_NO_PACKET_QUEUE)
    NDIS_STATUS Status;
#endif
    PNEMUNETFLTINS pInstance = pInfo->pNetFltIf;

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    /* this is not a rediscovery, initialize Mac cache */
    if (nemuNetFltWinReferenceWinIf(pInstance))
    {
#ifndef NEMUNETADP
        Status = nemuNetFltWinGetMacAddress(pInstance, &pInstance->u.s.MacAddr);
        if (Status == NDIS_STATUS_SUCCESS)
#endif
        {
#ifdef NEMUNETFLT_NO_PACKET_QUEUE
            pInfo->Status = VINF_SUCCESS;
#else
            Status = nemuNetFltWinQuInitPacketQueue(pInstance);
            if (Status == NDIS_STATUS_SUCCESS)
            {
                pInfo->Status = VINF_SUCCESS;
            }
            else
            {
                pInfo->Status = VERR_GENERAL_FAILURE;
            }
#endif
        }
#ifndef NEMUNETADP
        else
        {
            pInfo->Status = VERR_INTNET_FLT_IF_FAILED;
        }
#endif

        nemuNetFltWinDereferenceWinIf(pInstance);
    }
    else
    {
        pInfo->Status = VERR_INTNET_FLT_IF_NOT_FOUND;
    }
}

static int nemuNetFltWinConnectIt(PNEMUNETFLTINS pThis)
{
    WORKER_INFO Info;
    Info.pNetFltIf = pThis;

    nemuNetFltWinJobSynchExecAtPassive(nemuNetFltWinConnectItWorker, &Info);

    if (RT_SUCCESS(Info.Status))
        nemuNetFltWinReportStuff(pThis);

    return Info.Status;
}

int nemuNetFltOsConnectIt(PNEMUNETFLTINS pThis)
{
    Log2(("nemuNetFltOsConnectIt: pThis=%p pThis->pSwitchPort=%p pThis->pSwitchPort->pfnNotifyHostAddress=%p\n",
          pThis, pThis->pSwitchPort, pThis->pSwitchPort ? pThis->pSwitchPort->pfnNotifyHostAddress : NULL));
    nemuNetFltWinRegisterIpAddrNotifier(pThis);
    return nemuNetFltWinConnectIt(pThis);
}

void nemuNetFltOsDeleteInstance(PNEMUNETFLTINS pThis)
{
    nemuNetFltWinDeleteInstance(pThis);
}

int nemuNetFltOsInitInstance(PNEMUNETFLTINS pThis, void *pvContext)
{
    int rc = RTSemMutexCreate(&pThis->u.s.hWinIfMutex);
    if (RT_SUCCESS(rc))
    {
        rc = nemuNetFltWinAttachToInterface(pThis, pvContext, false /*fRediscovery*/ );
        if (RT_SUCCESS(rc))
        {
            return rc;
        }
        RTSemMutexDestroy(pThis->u.s.hWinIfMutex);
    }
    return rc;
}

int nemuNetFltOsPreInitInstance(PNEMUNETFLTINS pThis)
{
    pThis->u.s.cModeNetFltRefs = 0;
    pThis->u.s.cModePassThruRefs = 0;
    nemuNetFltWinSetWinIfState(pThis, kNemuWinIfState_Disconnected);
    nemuNetFltWinSetOpState(&pThis->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
#ifndef NEMUNETADP
    nemuNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kNemuNetDevOpState_Deinitialized);
#endif
    return VINF_SUCCESS;
}

void nemuNetFltPortOsNotifyMacAddress(PNEMUNETFLTINS pThis, void *pvIfData, PCRTMAC pMac)
{
}

int nemuNetFltPortOsConnectInterface(PNEMUNETFLTINS pThis, void *pvIf, void **ppvIfData)
{
    /* Nothing to do */
    return VINF_SUCCESS;
}

int nemuNetFltPortOsDisconnectInterface(PNEMUNETFLTINS pThis, void *pvIfData)
{
    /* Nothing to do */
    return VINF_SUCCESS;
}
