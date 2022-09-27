/* $Id: NemuNetFltRt-win.h $ */
/** @file
 * NemuNetFltRt-win.h - Bridged Networking Driver, Windows Specific Code.
 * NetFlt Runtime API
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
#ifndef ___NemuNetFltRt_win_h___
#define ___NemuNetFltRt_win_h___
DECLHIDDEN(VOID) nemuNetFltWinUnload(IN PDRIVER_OBJECT DriverObject);

#ifndef NEMUNETADP
# if !defined(NEMU_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)
DECLHIDDEN(bool) nemuNetFltWinMatchPackets(PNDIS_PACKET pPacket1, PNDIS_PACKET pPacket2, const INT cbMatch);
DECLHIDDEN(bool) nemuNetFltWinMatchPacketAndSG(PNDIS_PACKET pPacket, PINTNETSG pSG, const INT cbMatch);
# endif
#endif

/*************************
 * packet queue API      *
 *************************/


#define LIST_ENTRY_2_PACKET_INFO(pListEntry) \
    ( (PNEMUNETFLT_PACKET_INFO)((uint8_t *)(pListEntry) - RT_OFFSETOF(NEMUNETFLT_PACKET_INFO, ListEntry)) )

#if !defined(NEMU_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

#define NEMU_SLE_2_PKTRSVD_PT(_pEntry) \
    ( (PNEMUNETFLT_PKTRSVD_PT)((uint8_t *)(_pEntry) - RT_OFFSETOF(NEMUNETFLT_PKTRSVD_PT, ListEntry)) )

#define NEMU_SLE_2_SENDPACKET(_pEntry) \
    ( (PNDIS_PACKET)((uint8_t *)(NEMU_SLE_2_PKTRSVD_PT(_pEntry)) - RT_OFFSETOF(NDIS_PACKET, ProtocolReserved)) )

#endif
/**
 * enqueus the packet info to the tail of the queue
 */
DECLINLINE(void) nemuNetFltWinQuEnqueueTail(PNEMUNETFLT_PACKET_QUEUE pQueue, PNEMUNETFLT_PACKET_INFO pPacketInfo)
{
    InsertTailList(pQueue, &pPacketInfo->ListEntry);
}

DECLINLINE(void) nemuNetFltWinQuEnqueueHead(PNEMUNETFLT_PACKET_QUEUE pQueue, PNEMUNETFLT_PACKET_INFO pPacketInfo)
{
    Assert(pPacketInfo->pPool);
    InsertHeadList(pQueue, &pPacketInfo->ListEntry);
}

/**
 * enqueus the packet info to the tail of the queue
 */
DECLINLINE(void) nemuNetFltWinQuInterlockedEnqueueTail(PNEMUNETFLT_INTERLOCKED_PACKET_QUEUE pQueue, PNEMUNETFLT_PACKET_INFO pPacketInfo)
{
    Assert(pPacketInfo->pPool);
    NdisAcquireSpinLock(&pQueue->Lock);
    nemuNetFltWinQuEnqueueTail(&pQueue->Queue, pPacketInfo);
    NdisReleaseSpinLock(&pQueue->Lock);
}

DECLINLINE(void) nemuNetFltWinQuInterlockedEnqueueHead(PNEMUNETFLT_INTERLOCKED_PACKET_QUEUE pQueue, PNEMUNETFLT_PACKET_INFO pPacketInfo)
{
    NdisAcquireSpinLock(&pQueue->Lock);
    nemuNetFltWinQuEnqueueHead(&pQueue->Queue, pPacketInfo);
    NdisReleaseSpinLock(&pQueue->Lock);
}

/**
 * dequeus the packet info from the head of the queue
 */
DECLINLINE(PNEMUNETFLT_PACKET_INFO) nemuNetFltWinQuDequeueHead(PNEMUNETFLT_PACKET_QUEUE pQueue)
{
    PLIST_ENTRY pListEntry = RemoveHeadList(pQueue);
    if(pListEntry != pQueue)
    {
        PNEMUNETFLT_PACKET_INFO pInfo = LIST_ENTRY_2_PACKET_INFO(pListEntry);
        Assert(pInfo->pPool);
        return pInfo;
    }
    return NULL;
}

DECLINLINE(PNEMUNETFLT_PACKET_INFO) nemuNetFltWinQuDequeueTail(PNEMUNETFLT_PACKET_QUEUE pQueue)
{
    PLIST_ENTRY pListEntry = RemoveTailList(pQueue);
    if(pListEntry != pQueue)
    {
        PNEMUNETFLT_PACKET_INFO pInfo = LIST_ENTRY_2_PACKET_INFO(pListEntry);
        Assert(pInfo->pPool);
        return pInfo;
    }
    return NULL;
}

DECLINLINE(PNEMUNETFLT_PACKET_INFO) nemuNetFltWinQuInterlockedDequeueHead(PNEMUNETFLT_INTERLOCKED_PACKET_QUEUE pInterlockedQueue)
{
    PNEMUNETFLT_PACKET_INFO pInfo;
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    pInfo = nemuNetFltWinQuDequeueHead(&pInterlockedQueue->Queue);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
    return pInfo;
}

DECLINLINE(PNEMUNETFLT_PACKET_INFO) nemuNetFltWinQuInterlockedDequeueTail(PNEMUNETFLT_INTERLOCKED_PACKET_QUEUE pInterlockedQueue)
{
    PNEMUNETFLT_PACKET_INFO pInfo;
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    pInfo = nemuNetFltWinQuDequeueTail(&pInterlockedQueue->Queue);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
    return pInfo;
}

DECLINLINE(void) nemuNetFltWinQuDequeue(PNEMUNETFLT_PACKET_INFO pInfo)
{
    RemoveEntryList(&pInfo->ListEntry);
}

DECLINLINE(void) nemuNetFltWinQuInterlockedDequeue(PNEMUNETFLT_INTERLOCKED_PACKET_QUEUE pInterlockedQueue, PNEMUNETFLT_PACKET_INFO pInfo)
{
    NdisAcquireSpinLock(&pInterlockedQueue->Lock);
    nemuNetFltWinQuDequeue(pInfo);
    NdisReleaseSpinLock(&pInterlockedQueue->Lock);
}

/**
 * allocates the packet info from the pool
 */
DECLINLINE(PNEMUNETFLT_PACKET_INFO) nemuNetFltWinPpAllocPacketInfo(PNEMUNETFLT_PACKET_INFO_POOL pPool)
{
    return nemuNetFltWinQuInterlockedDequeueHead(&pPool->Queue);
}

/**
 * returns the packet info to the pool
 */
DECLINLINE(void) nemuNetFltWinPpFreePacketInfo(PNEMUNETFLT_PACKET_INFO pInfo)
{
    PNEMUNETFLT_PACKET_INFO_POOL pPool = pInfo->pPool;
    nemuNetFltWinQuInterlockedEnqueueHead(&pPool->Queue, pInfo);
}

/** initializes the packet queue */
#define INIT_PACKET_QUEUE(_pQueue) InitializeListHead((_pQueue))

/** initializes the packet queue */
#define INIT_INTERLOCKED_PACKET_QUEUE(_pQueue) \
    { \
        INIT_PACKET_QUEUE(&(_pQueue)->Queue); \
        NdisAllocateSpinLock(&(_pQueue)->Lock); \
    }

/** delete the packet queue */
#define FINI_INTERLOCKED_PACKET_QUEUE(_pQueue) NdisFreeSpinLock(&(_pQueue)->Lock)

/** returns the packet the packet info contains */
#define GET_PACKET_FROM_INFO(_pPacketInfo) (ASMAtomicUoReadPtr((void * volatile *)&(_pPacketInfo)->pPacket))

/** assignes the packet to the packet info */
#define SET_PACKET_TO_INFO(_pPacketInfo, _pPacket) (ASMAtomicUoWritePtr(&(_pPacketInfo)->pPacket, (_pPacket)))

/** returns the flags the packet info contains */
#define GET_FLAGS_FROM_INFO(_pPacketInfo) (ASMAtomicUoReadU32((volatile uint32_t *)&(_pPacketInfo)->fFlags))

/** sets flags to the packet info */
#define SET_FLAGS_TO_INFO(_pPacketInfo, _fFlags) (ASMAtomicUoWriteU32((volatile uint32_t *)&(_pPacketInfo)->fFlags, (_fFlags)))

#ifdef NEMUNETFLT_NO_PACKET_QUEUE
DECLHIDDEN(bool) nemuNetFltWinPostIntnet(PNEMUNETFLTINS pInstance, PVOID pvPacket, const UINT fFlags);
#else
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinQuEnqueuePacket(PNEMUNETFLTINS pInstance, PVOID pPacket, const UINT fPacketFlags);
DECLHIDDEN(void) nemuNetFltWinQuFiniPacketQueue(PNEMUNETFLTINS pInstance);
DECLHIDDEN(NTSTATUS) nemuNetFltWinQuInitPacketQueue(PNEMUNETFLTINS pInstance);
#endif /* #ifndef NEMUNETFLT_NO_PACKET_QUEUE */


#ifndef NEMUNETADP
/**
 * searches the list entry in a single-linked list
 */
DECLINLINE(bool) nemuNetFltWinSearchListEntry(PNEMUNETFLT_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry2Search, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    for(pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        if(pEntry2Search == pCur)
        {
            if(bRemove)
            {
                pPrev->Next = pCur->Next;
                if(pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return true;
        }
    }
    return false;
}

#if !defined(NEMU_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

DECLINLINE(PNDIS_PACKET) nemuNetFltWinSearchPacket(PNEMUNETFLT_SINGLE_LIST pList, PNDIS_PACKET pPacket2Search, int cbMatch, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    PNDIS_PACKET pCurPacket;
    for(pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        pCurPacket = NEMU_SLE_2_SENDPACKET(pCur);
        if(pCurPacket == pPacket2Search || nemuNetFltWinMatchPackets(pPacket2Search, pCurPacket, cbMatch))
        {
            if(bRemove)
            {
                pPrev->Next = pCur->Next;
                if(pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return pCurPacket;
        }
    }
    return NULL;
}

DECLINLINE(PNDIS_PACKET) nemuNetFltWinSearchPacketBySG(PNEMUNETFLT_SINGLE_LIST pList, PINTNETSG pSG, int cbMatch, bool bRemove)
{
    PSINGLE_LIST_ENTRY pHead = &pList->Head;
    PSINGLE_LIST_ENTRY pCur;
    PSINGLE_LIST_ENTRY pPrev;
    PNDIS_PACKET pCurPacket;
    for(pCur = pHead->Next, pPrev = pHead; pCur; pPrev = pCur, pCur = pCur->Next)
    {
        pCurPacket = NEMU_SLE_2_SENDPACKET(pCur);
        if(nemuNetFltWinMatchPacketAndSG(pCurPacket, pSG, cbMatch))
        {
            if(bRemove)
            {
                pPrev->Next = pCur->Next;
                if(pCur == pList->pTail)
                {
                    pList->pTail = pPrev;
                }
            }
            return pCurPacket;
        }
    }
    return NULL;
}

#endif /* #if !defined(NEMU_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS) */

DECLINLINE(bool) nemuNetFltWinSListIsEmpty(PNEMUNETFLT_SINGLE_LIST pList)
{
    return !pList->Head.Next;
}

DECLINLINE(void) nemuNetFltWinPutTail(PNEMUNETFLT_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    pList->pTail->Next = pEntry;
    pList->pTail = pEntry;
    pEntry->Next = NULL;
}

DECLINLINE(void) nemuNetFltWinPutHead(PNEMUNETFLT_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    pEntry->Next = pList->Head.Next;
    pList->Head.Next = pEntry;
    if(!pEntry->Next)
        pList->pTail = pEntry;
}

DECLINLINE(PSINGLE_LIST_ENTRY) nemuNetFltWinGetHead(PNEMUNETFLT_SINGLE_LIST pList)
{
    PSINGLE_LIST_ENTRY pEntry = pList->Head.Next;
    if(pEntry && pEntry == pList->pTail)
    {
        pList->Head.Next = NULL;
        pList->pTail = &pList->Head;
    }
    return pEntry;
}

DECLINLINE(bool) nemuNetFltWinInterlockedSearchListEntry(PNEMUNETFLT_INTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry2Search, bool bRemove)
{
    bool bFound;
    NdisAcquireSpinLock(&pList->Lock);
    bFound = nemuNetFltWinSearchListEntry(&pList->List, pEntry2Search, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return bFound;
}

#if !defined(NEMU_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)

DECLINLINE(PNDIS_PACKET) nemuNetFltWinInterlockedSearchPacket(PNEMUNETFLT_INTERLOCKED_SINGLE_LIST pList, PNDIS_PACKET pPacket2Search, int cbMatch, bool bRemove)
{
    PNDIS_PACKET pFound;
    NdisAcquireSpinLock(&pList->Lock);
    pFound = nemuNetFltWinSearchPacket(&pList->List, pPacket2Search, cbMatch, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return pFound;
}

DECLINLINE(PNDIS_PACKET) nemuNetFltWinInterlockedSearchPacketBySG(PNEMUNETFLT_INTERLOCKED_SINGLE_LIST pList, PINTNETSG pSG, int cbMatch, bool bRemove)
{
    PNDIS_PACKET pFound;
    NdisAcquireSpinLock(&pList->Lock);
    pFound = nemuNetFltWinSearchPacketBySG(&pList->List, pSG, cbMatch, bRemove);
    NdisReleaseSpinLock(&pList->Lock);
    return pFound;
}
#endif /* #if !defined(NEMU_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS) */

DECLINLINE(void) nemuNetFltWinInterlockedPutTail(PNEMUNETFLT_INTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    NdisAcquireSpinLock(&pList->Lock);
    nemuNetFltWinPutTail(&pList->List, pEntry);
    NdisReleaseSpinLock(&pList->Lock);
}

DECLINLINE(void) nemuNetFltWinInterlockedPutHead(PNEMUNETFLT_INTERLOCKED_SINGLE_LIST pList, PSINGLE_LIST_ENTRY pEntry)
{
    NdisAcquireSpinLock(&pList->Lock);
    nemuNetFltWinPutHead(&pList->List, pEntry);
    NdisReleaseSpinLock(&pList->Lock);
}

DECLINLINE(PSINGLE_LIST_ENTRY) nemuNetFltWinInterlockedGetHead(PNEMUNETFLT_INTERLOCKED_SINGLE_LIST pList)
{
    PSINGLE_LIST_ENTRY pEntry;
    NdisAcquireSpinLock(&pList->Lock);
    pEntry = nemuNetFltWinGetHead(&pList->List);
    NdisReleaseSpinLock(&pList->Lock);
    return pEntry;
}

# if defined(DEBUG_NETFLT_PACKETS) || !defined(NEMU_LOOPBACK_USEFLAGS)
DECLINLINE(void) nemuNetFltWinLbPutSendPacket(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket, bool bFromIntNet)
{
    PNEMUNETFLT_PKTRSVD_PT pSrv = (PNEMUNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
    pSrv->bFromIntNet = bFromIntNet;
    nemuNetFltWinInterlockedPutHead(&pNetFlt->u.s.WinIf.SendPacketQueue, &pSrv->ListEntry);
}

DECLINLINE(bool) nemuNetFltWinLbIsFromIntNet(PNDIS_PACKET pPacket)
{
    PNEMUNETFLT_PKTRSVD_PT pSrv = (PNEMUNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
    return pSrv->bFromIntNet;
}

DECLINLINE(PNDIS_PACKET) nemuNetFltWinLbSearchLoopBack(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket, bool bRemove)
{
    return nemuNetFltWinInterlockedSearchPacket(&pNetFlt->u.s.WinIf.SendPacketQueue, pPacket, NEMUNETFLT_PACKETMATCH_LENGTH, bRemove);
}

DECLINLINE(PNDIS_PACKET) nemuNetFltWinLbSearchLoopBackBySG(PNEMUNETFLTINS pNetFlt, PINTNETSG pSG, bool bRemove)
{
    return nemuNetFltWinInterlockedSearchPacketBySG(&pNetFlt->u.s.WinIf.SendPacketQueue, pSG, NEMUNETFLT_PACKETMATCH_LENGTH, bRemove);
}

DECLINLINE(bool) nemuNetFltWinLbRemoveSendPacket(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket)
{
    PNEMUNETFLT_PKTRSVD_PT pSrv = (PNEMUNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
    bool bRet = nemuNetFltWinInterlockedSearchListEntry(&pNetFlt->u.s.WinIf.SendPacketQueue, &pSrv->ListEntry, true);
#ifdef DEBUG_misha
    Assert(bRet == (pNetFlt->enmTrunkState == INTNETTRUNKIFSTATE_ACTIVE));
#endif
    return bRet;
}

# endif

#endif

#ifdef DEBUG_misha
DECLHIDDEN(bool) nemuNetFltWinCheckMACs(PNDIS_PACKET pPacket, PRTMAC pDst, PRTMAC pSrc);
DECLHIDDEN(bool) nemuNetFltWinCheckMACsSG(PINTNETSG pSG, PRTMAC pDst, PRTMAC pSrc);
extern RTMAC g_nemuNetFltWinVerifyMACBroadcast;
extern RTMAC g_nemuNetFltWinVerifyMACGuest;

# define NEMUNETFLT_LBVERIFY(_pnf, _p) \
    do { \
        Assert(!nemuNetFltWinCheckMACs(_p, NULL, &g_nemuNetFltWinVerifyMACGuest)); \
        Assert(!nemuNetFltWinCheckMACs(_p, NULL, &(_pnf)->u.s.MacAddr)); \
    } while (0)

# define NEMUNETFLT_LBVERIFYSG(_pnf, _p) \
    do { \
        Assert(!nemuNetFltWinCheckMACsSG(_p, NULL, &g_nemuNetFltWinVerifyMACGuest)); \
        Assert(!nemuNetFltWinCheckMACsSG(_p, NULL, &(_pnf)->u.s.MacAddr)); \
    } while (0)

#else
# define NEMUNETFLT_LBVERIFY(_pnf, _p) do { } while (0)
# define NEMUNETFLT_LBVERIFYSG(_pnf, _p) do { } while (0)
#endif

/** initializes the list */
#define INIT_SINGLE_LIST(_pList) \
    { \
        (_pList)->Head.Next = NULL; \
        (_pList)->pTail = &(_pList)->Head; \
    }

/** initializes the list */
#define INIT_INTERLOCKED_SINGLE_LIST(_pList) \
    do { \
        INIT_SINGLE_LIST(&(_pList)->List); \
        NdisAllocateSpinLock(&(_pList)->Lock); \
    } while (0)

/** delete the packet queue */
#define FINI_INTERLOCKED_SINGLE_LIST(_pList) \
    do { \
        Assert(nemuNetFltWinSListIsEmpty(&(_pList)->List)); \
        NdisFreeSpinLock(&(_pList)->Lock) \
    } while (0)


/**************************************************************************
 * PNEMUNETFLTINS , WinIf reference/dereference (i.e. retain/release) API *
 **************************************************************************/


DECLHIDDEN(void) nemuNetFltWinWaitDereference(PNEMUNETFLT_WINIF_DEVICE pState);

DECLINLINE(void) nemuNetFltWinReferenceModeNetFlt(PNEMUNETFLTINS pIns)
{
    ASMAtomicIncU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs);
}

DECLINLINE(void) nemuNetFltWinReferenceModePassThru(PNEMUNETFLTINS pIns)
{
    ASMAtomicIncU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs);
}

DECLINLINE(void) nemuNetFltWinIncReferenceModeNetFlt(PNEMUNETFLTINS pIns, uint32_t v)
{
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs, v);
}

DECLINLINE(void) nemuNetFltWinIncReferenceModePassThru(PNEMUNETFLTINS pIns, uint32_t v)
{
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs, v);
}

DECLINLINE(void) nemuNetFltWinDereferenceModeNetFlt(PNEMUNETFLTINS pIns)
{
    ASMAtomicDecU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs);
}

DECLINLINE(void) nemuNetFltWinDereferenceModePassThru(PNEMUNETFLTINS pIns)
{
    ASMAtomicDecU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs);
}

DECLINLINE(void) nemuNetFltWinDecReferenceModeNetFlt(PNEMUNETFLTINS pIns, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModeNetFltRefs, (uint32_t)(-((int32_t)v)));
}

DECLINLINE(void) nemuNetFltWinDecReferenceModePassThru(PNEMUNETFLTINS pIns, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((volatile uint32_t *)&pIns->u.s.cModePassThruRefs, (uint32_t)(-((int32_t)v)));
}

DECLINLINE(void) nemuNetFltWinSetPowerState(PNEMUNETFLT_WINIF_DEVICE pState, NDIS_DEVICE_POWER_STATE State)
{
    ASMAtomicUoWriteU32((volatile uint32_t *)&pState->PowerState, State);
}

DECLINLINE(NDIS_DEVICE_POWER_STATE) nemuNetFltWinGetPowerState(PNEMUNETFLT_WINIF_DEVICE pState)
{
    return (NDIS_DEVICE_POWER_STATE)ASMAtomicUoReadU32((volatile uint32_t *)&pState->PowerState);
}

DECLINLINE(void) nemuNetFltWinSetOpState(PNEMUNETFLT_WINIF_DEVICE pState, NEMUNETDEVOPSTATE State)
{
    ASMAtomicUoWriteU32((volatile uint32_t *)&pState->OpState, State);
}

DECLINLINE(NEMUNETDEVOPSTATE) nemuNetFltWinGetOpState(PNEMUNETFLT_WINIF_DEVICE pState)
{
    return (NEMUNETDEVOPSTATE)ASMAtomicUoReadU32((volatile uint32_t *)&pState->OpState);
}

DECLINLINE(bool) nemuNetFltWinDoReferenceDevice(PNEMUNETFLT_WINIF_DEVICE pState)
{
    if (nemuNetFltWinGetPowerState(pState) == NdisDeviceStateD0 && nemuNetFltWinGetOpState(pState) == kNemuNetDevOpState_Initialized)
    {
        /** @todo r=bird: Since this is a volatile member, why don't you declare it as
         *        such and save yourself all the casting? */
        ASMAtomicIncU32((uint32_t volatile *)&pState->cReferences);
        return true;
    }
    return false;
}

#ifndef NEMUNETADP
DECLINLINE(bool) nemuNetFltWinDoReferenceDevices(PNEMUNETFLT_WINIF_DEVICE pState1, PNEMUNETFLT_WINIF_DEVICE pState2)
{
    if (nemuNetFltWinGetPowerState(pState1) == NdisDeviceStateD0
            && nemuNetFltWinGetOpState(pState1) == kNemuNetDevOpState_Initialized
            && nemuNetFltWinGetPowerState(pState2) == NdisDeviceStateD0
            && nemuNetFltWinGetOpState(pState2) == kNemuNetDevOpState_Initialized)
    {
        ASMAtomicIncU32((uint32_t volatile *)&pState1->cReferences);
        ASMAtomicIncU32((uint32_t volatile *)&pState2->cReferences);
        return true;
    }
    return false;
}
#endif

DECLINLINE(void) nemuNetFltWinDereferenceDevice(PNEMUNETFLT_WINIF_DEVICE pState)
{
    ASMAtomicDecU32((uint32_t volatile *)&pState->cReferences);
    /** @todo r=bird: Add comment explaining why these cannot hit 0 or why
     *        reference are counted  */
}

#ifndef NEMUNETADP
DECLINLINE(void) nemuNetFltWinDereferenceDevices(PNEMUNETFLT_WINIF_DEVICE pState1, PNEMUNETFLT_WINIF_DEVICE pState2)
{
    ASMAtomicDecU32((uint32_t volatile *)&pState1->cReferences);
    ASMAtomicDecU32((uint32_t volatile *)&pState2->cReferences);
}
#endif

DECLINLINE(void) nemuNetFltWinDecReferenceDevice(PNEMUNETFLT_WINIF_DEVICE pState, uint32_t v)
{
    Assert(v);
    ASMAtomicAddU32((uint32_t volatile *)&pState->cReferences, (uint32_t)(-((int32_t)v)));
}

#ifndef NEMUNETADP
DECLINLINE(void) nemuNetFltWinDecReferenceDevices(PNEMUNETFLT_WINIF_DEVICE pState1, PNEMUNETFLT_WINIF_DEVICE pState2, uint32_t v)
{
    ASMAtomicAddU32((uint32_t volatile *)&pState1->cReferences, (uint32_t)(-((int32_t)v)));
    ASMAtomicAddU32((uint32_t volatile *)&pState2->cReferences, (uint32_t)(-((int32_t)v)));
}
#endif

DECLINLINE(bool) nemuNetFltWinDoIncReferenceDevice(PNEMUNETFLT_WINIF_DEVICE pState, uint32_t v)
{
    Assert(v);
    if (nemuNetFltWinGetPowerState(pState) == NdisDeviceStateD0 && nemuNetFltWinGetOpState(pState) == kNemuNetDevOpState_Initialized)
    {
        ASMAtomicAddU32((uint32_t volatile *)&pState->cReferences, v);
        return true;
    }
    return false;
}

#ifndef NEMUNETADP
DECLINLINE(bool) nemuNetFltWinDoIncReferenceDevices(PNEMUNETFLT_WINIF_DEVICE pState1, PNEMUNETFLT_WINIF_DEVICE pState2, uint32_t v)
{
    if (nemuNetFltWinGetPowerState(pState1) == NdisDeviceStateD0
            && nemuNetFltWinGetOpState(pState1) == kNemuNetDevOpState_Initialized
            && nemuNetFltWinGetPowerState(pState2) == NdisDeviceStateD0
            && nemuNetFltWinGetOpState(pState2) == kNemuNetDevOpState_Initialized)
    {
        ASMAtomicAddU32((uint32_t volatile *)&pState1->cReferences, v);
        ASMAtomicAddU32((uint32_t volatile *)&pState2->cReferences, v);
        return true;
    }
    return false;
}
#endif


DECLINLINE(bool) nemuNetFltWinReferenceWinIfNetFlt(PNEMUNETFLTINS pNetFlt, bool * pbNetFltActive)
{
    RTSpinlockAcquire((pNetFlt)->hSpinlock);
#ifndef NEMUNETADP
    if(!nemuNetFltWinDoReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState))
#else
    if(!nemuNetFltWinDoReferenceDevice(&pNetFlt->u.s.WinIf.MpState))
#endif
    {
        RTSpinlockRelease((pNetFlt)->hSpinlock);
        *pbNetFltActive = false;
        return false;
    }

    if(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE)
    {
        nemuNetFltWinReferenceModePassThru(pNetFlt);
        RTSpinlockRelease((pNetFlt)->hSpinlock);
        *pbNetFltActive = false;
        return true;
    }

    nemuNetFltRetain((pNetFlt), true /* fBusy */);
    nemuNetFltWinReferenceModeNetFlt(pNetFlt);
    RTSpinlockRelease((pNetFlt)->hSpinlock);

    *pbNetFltActive = true;
    return true;
}

DECLINLINE(bool) nemuNetFltWinIncReferenceWinIfNetFlt(PNEMUNETFLTINS pNetFlt, uint32_t v, bool *pbNetFltActive)
{
    uint32_t i;

    Assert(v);
    if(!v)
    {
        *pbNetFltActive = false;
        return false;
    }

    RTSpinlockAcquire((pNetFlt)->hSpinlock);
#ifndef NEMUNETADP
    if(!nemuNetFltWinDoIncReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState, v))
#else
    if(!nemuNetFltWinDoIncReferenceDevice(&pNetFlt->u.s.WinIf.MpState, v))
#endif
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        *pbNetFltActive = false;
        return false;
    }

    if(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE)
    {
        nemuNetFltWinIncReferenceModePassThru(pNetFlt, v);

        RTSpinlockRelease((pNetFlt)->hSpinlock);
        *pbNetFltActive = false;
        return true;
    }

    nemuNetFltRetain(pNetFlt, true /* fBusy */);

    nemuNetFltWinIncReferenceModeNetFlt(pNetFlt, v);

    RTSpinlockRelease(pNetFlt->hSpinlock);

    /* we have marked it as busy, so can do the res references outside the lock */
    for(i = 0; i < v-1; i++)
    {
        nemuNetFltRetain(pNetFlt, true /* fBusy */);
    }

    *pbNetFltActive = true;

    return true;
}

DECLINLINE(void) nemuNetFltWinDecReferenceNetFlt(PNEMUNETFLTINS pNetFlt, uint32_t n)
{
    uint32_t i;
    for(i = 0; i < n; i++)
    {
        nemuNetFltRelease(pNetFlt, true);
    }

    nemuNetFltWinDecReferenceModeNetFlt(pNetFlt, n);
}

DECLINLINE(void) nemuNetFltWinDereferenceNetFlt(PNEMUNETFLTINS pNetFlt)
{
    nemuNetFltRelease(pNetFlt, true);

    nemuNetFltWinDereferenceModeNetFlt(pNetFlt);
}

DECLINLINE(void) nemuNetFltWinDecReferenceWinIf(PNEMUNETFLTINS pNetFlt, uint32_t v)
{
#ifdef NEMUNETADP
    nemuNetFltWinDecReferenceDevice(&pNetFlt->u.s.WinIf.MpState, v);
#else
    nemuNetFltWinDecReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState, v);
#endif
}

DECLINLINE(void) nemuNetFltWinDereferenceWinIf(PNEMUNETFLTINS pNetFlt)
{
#ifdef NEMUNETADP
    nemuNetFltWinDereferenceDevice(&pNetFlt->u.s.WinIf.MpState);
#else
    nemuNetFltWinDereferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState);
#endif
}

DECLINLINE(bool) nemuNetFltWinIncReferenceWinIf(PNEMUNETFLTINS pNetFlt, uint32_t v)
{
    Assert(v);
    if(!v)
    {
        return false;
    }

    RTSpinlockAcquire(pNetFlt->hSpinlock);
#ifdef NEMUNETADP
    if(nemuNetFltWinDoIncReferenceDevice(&pNetFlt->u.s.WinIf.MpState, v))
#else
    if(nemuNetFltWinDoIncReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState, v))
#endif
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        return true;
    }

    RTSpinlockRelease(pNetFlt->hSpinlock);
    return false;
}

DECLINLINE(bool) nemuNetFltWinReferenceWinIf(PNEMUNETFLTINS pNetFlt)
{
    RTSpinlockAcquire(pNetFlt->hSpinlock);
#ifdef NEMUNETADP
    if(nemuNetFltWinDoReferenceDevice(&pNetFlt->u.s.WinIf.MpState))
#else
    if(nemuNetFltWinDoReferenceDevices(&pNetFlt->u.s.WinIf.MpState, &pNetFlt->u.s.WinIf.PtState))
#endif
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        return true;
    }

    RTSpinlockRelease(pNetFlt->hSpinlock);
    return false;
}

/***********************************************
 * methods for accessing the network card info *
 ***********************************************/

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinGetMacAddress(PNEMUNETFLTINS pNetFlt, PRTMAC pMac);
DECLHIDDEN(bool) nemuNetFltWinIsPromiscuous(PNEMUNETFLTINS pNetFlt);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinSetPromiscuous(PNEMUNETFLTINS pNetFlt, bool bYes);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinQueryPhysicalMedium(PNEMUNETFLTINS pNetFlt, NDIS_PHYSICAL_MEDIUM * pMedium);

/*********************
 * mem alloc API     *
 *********************/

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMemAlloc(PVOID* ppMemBuf, UINT cbLength);

DECLHIDDEN(void) nemuNetFltWinMemFree(PVOID pMemBuf);

/* convenience method used which allocates and initializes the PINTNETSG containing one
 * segment referring the buffer of size cbBufSize
 * the allocated PINTNETSG should be freed with the nemuNetFltWinMemFree.
 *
 * This is used when our ProtocolReceive callback is called and we have to return the indicated NDIS_PACKET
 * on a callback exit. This is why we allocate the PINTNETSG and put the packet info there and enqueue it
 * for the packet queue */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinAllocSG(UINT cbBufSize, PINTNETSG *ppSG);

/************************
 * WinIf init/fini API *
 ************************/
#if defined(NEMUNETADP)
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtInitBind(PNEMUNETFLTINS *ppNetFlt, NDIS_HANDLE hMiniportAdapter, PNDIS_STRING pBindToMiniportName /* actually this is our miniport name*/, NDIS_HANDLE hWrapperConfigurationContext);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtInitWinIf(PNEMUNETFLTWIN pWinIf);
#else
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtInitBind(PNEMUNETFLTINS *ppNetFlt, PNDIS_STRING pOurMiniportName, PNDIS_STRING pBindToMiniportName);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtInitWinIf(PNEMUNETFLTWIN pWinIf, PNDIS_STRING pOurDeviceName);
#endif

DECLHIDDEN(VOID) nemuNetFltWinPtFiniWinIf(PNEMUNETFLTWIN pWinIf);

/************************************
 * Execute Job at passive level API *
 ************************************/

typedef VOID (*PFNNEMUNETFLT_JOB_ROUTINE) (PVOID pContext);

DECLHIDDEN(VOID) nemuNetFltWinJobSynchExecAtPassive(PFNNEMUNETFLT_JOB_ROUTINE pfnRoutine, PVOID pContext);

/*******************************
 * Ndis Packets processing API *
 *******************************/
DECLHIDDEN(PNDIS_PACKET) nemuNetFltWinNdisPacketFromSG(PNEMUNETFLTINS pNetFlt, PINTNETSG pSG, PVOID pBufToFree, bool bToWire, bool bCopyMemory);

DECLHIDDEN(void) nemuNetFltWinFreeSGNdisPacket(PNDIS_PACKET pPacket, bool bFreeMem);

#ifdef DEBUG_NETFLT_PACKETS
#define DBG_CHECK_PACKETS(_p1, _p2) \
    {   \
        bool _b = nemuNetFltWinMatchPackets(_p1, _p2, -1);  \
        Assert(_b);  \
    }

#define DBG_CHECK_PACKET_AND_SG(_p, _sg) \
    {   \
        bool _b = nemuNetFltWinMatchPacketAndSG(_p, _sg, -1);  \
        Assert(_b);  \
    }

#define DBG_CHECK_SGS(_sg1, _sg2) \
    {   \
        bool _b = nemuNetFltWinMatchSGs(_sg1, _sg2, -1);  \
        Assert(_b);  \
    }

#else
#define DBG_CHECK_PACKETS(_p1, _p2)
#define DBG_CHECK_PACKET_AND_SG(_p, _sg)
#define DBG_CHECK_SGS(_sg1, _sg2)
#endif

/**
 * Ndis loops back broadcast packets posted to the wire by IntNet
 * This routine is used in the mechanism of preventing this looping
 *
 * @param pAdapt
 * @param pPacket
 * @param bOnRecv true is we are receiving the packet from the wire
 * false otherwise (i.e. the packet is from the host)
 *
 * @return true if the packet is a looped back one, false otherwise
 */
#ifdef NEMU_LOOPBACK_USEFLAGS
DECLINLINE(bool) nemuNetFltWinIsLoopedBackPacket(PNDIS_PACKET pPacket)
{
    return (NdisGetPacketFlags(pPacket) & g_fPacketIsLoopedBack) == g_fPacketIsLoopedBack;
}
#endif

/**************************************************************
 * utility methods for ndis packet creation/initialization    *
 **************************************************************/

#define NEMUNETFLT_OOB_INIT(_p) \
    { \
        NdisZeroMemory(NDIS_OOB_DATA_FROM_PACKET(_p), sizeof(NDIS_PACKET_OOB_DATA)); \
        NDIS_SET_PACKET_HEADER_SIZE(_p, NEMUNETFLT_PACKET_ETHEADER_SIZE); \
    }

#ifndef NEMUNETADP

DECLINLINE(NDIS_STATUS) nemuNetFltWinCopyPacketInfoOnRecv(PNDIS_PACKET pDstPacket, PNDIS_PACKET pSrcPacket, bool bForceStatusResources)
{
    NDIS_STATUS Status = bForceStatusResources ? NDIS_STATUS_RESOURCES : NDIS_GET_PACKET_STATUS(pSrcPacket);
    NDIS_SET_PACKET_STATUS(pDstPacket, Status);

    NDIS_PACKET_FIRST_NDIS_BUFFER(pDstPacket) = NDIS_PACKET_FIRST_NDIS_BUFFER(pSrcPacket);
    NDIS_PACKET_LAST_NDIS_BUFFER(pDstPacket) = NDIS_PACKET_LAST_NDIS_BUFFER(pSrcPacket);

    NdisGetPacketFlags(pDstPacket) = NdisGetPacketFlags(pSrcPacket);

    NDIS_SET_ORIGINAL_PACKET(pDstPacket, NDIS_GET_ORIGINAL_PACKET(pSrcPacket));
    NDIS_SET_PACKET_HEADER_SIZE(pDstPacket, NDIS_GET_PACKET_HEADER_SIZE(pSrcPacket));

    return Status;
}

DECLINLINE(void) nemuNetFltWinCopyPacketInfoOnSend(PNDIS_PACKET pDstPacket, PNDIS_PACKET pSrcPacket)
{
    NDIS_PACKET_FIRST_NDIS_BUFFER(pDstPacket) = NDIS_PACKET_FIRST_NDIS_BUFFER(pSrcPacket);
    NDIS_PACKET_LAST_NDIS_BUFFER(pDstPacket) = NDIS_PACKET_LAST_NDIS_BUFFER(pSrcPacket);

    NdisGetPacketFlags(pDstPacket) = NdisGetPacketFlags(pSrcPacket);

    NdisMoveMemory(NDIS_OOB_DATA_FROM_PACKET(pDstPacket),
                    NDIS_OOB_DATA_FROM_PACKET(pSrcPacket),
                    sizeof (NDIS_PACKET_OOB_DATA));

    NdisIMCopySendPerPacketInfo(pDstPacket, pSrcPacket);

    PVOID pMediaSpecificInfo = NULL;
    UINT fMediaSpecificInfoSize = 0;

    NDIS_GET_PACKET_MEDIA_SPECIFIC_INFO(pSrcPacket, &pMediaSpecificInfo, &fMediaSpecificInfoSize);

    if (pMediaSpecificInfo || fMediaSpecificInfoSize)
    {
        NDIS_SET_PACKET_MEDIA_SPECIFIC_INFO(pDstPacket, pMediaSpecificInfo, fMediaSpecificInfoSize);
    }
}

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPrepareSendPacket(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket, PNDIS_PACKET *ppMyPacket);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPrepareRecvPacket(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket, PNDIS_PACKET *ppMyPacket, bool bDpr);
#endif

DECLHIDDEN(void) nemuNetFltWinSleep(ULONG milis);

#define MACS_EQUAL(_m1, _m2) \
    ((_m1).au16[0] == (_m2).au16[0] \
        && (_m1).au16[1] == (_m2).au16[1] \
        && (_m1).au16[2] == (_m2).au16[2])


DECLHIDDEN(NDIS_STATUS) nemuNetFltWinDetachFromInterface(PNEMUNETFLTINS pNetFlt, bool bOnUnbind);
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinCopyString(PNDIS_STRING pDst, PNDIS_STRING pSrc);


/**
 * Sets the enmState member atomically.
 *
 * Used for all updates.
 *
 * @param   pThis           The instance.
 * @param   enmNewState     The new value.
 */
DECLINLINE(void) nemuNetFltWinSetWinIfState(PNEMUNETFLTINS pNetFlt, NEMUNETFLT_WINIFSTATE enmNewState)
{
    ASMAtomicWriteU32((uint32_t volatile *)&pNetFlt->u.s.WinIf.enmState, enmNewState);
}

/**
 * Gets the enmState member atomically.
 *
 * Used for all reads.
 *
 * @returns The enmState value.
 * @param   pThis           The instance.
 */
DECLINLINE(NEMUNETFLT_WINIFSTATE) nemuNetFltWinGetWinIfState(PNEMUNETFLTINS pNetFlt)
{
    return (NEMUNETFLT_WINIFSTATE)ASMAtomicUoReadU32((uint32_t volatile *)&pNetFlt->u.s.WinIf.enmState);
}

/* reference the driver module to prevent driver unload */
DECLHIDDEN(void) nemuNetFltWinDrvReference();
/* dereference the driver module to prevent driver unload */
DECLHIDDEN(void) nemuNetFltWinDrvDereference();


#ifndef NEMUNETADP
# define NEMUNETFLT_PROMISCUOUS_SUPPORTED(_pNetFlt) (!(_pNetFlt)->fDisablePromiscuous)
#else
# define STATISTIC_INCREASE(_s) ASMAtomicIncU32((uint32_t volatile *)&(_s));

DECLHIDDEN(void) nemuNetFltWinGenerateMACAddress(RTMAC *pMac);
DECLHIDDEN(int) nemuNetFltWinMAC2NdisString(RTMAC *pMac, PNDIS_STRING pNdisString);
DECLHIDDEN(int) nemuNetFltWinMACFromNdisString(RTMAC *pMac, PNDIS_STRING pNdisString);

#endif
#endif /* #ifndef ___NemuNetFltRt_win_h___ */
