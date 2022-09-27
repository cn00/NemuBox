/* $Id: NemuMPShgsmi.cpp $ */

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
#include <iprt/semaphore.h>

/* SHGSMI */
DECLINLINE(void) nemuSHGSMICommandRetain (PNEMUSHGSMIHEADER pCmd)
{
    ASMAtomicIncU32(&pCmd->cRefs);
}

void nemuSHGSMICommandFree (PNEMUSHGSMI pHeap, PNEMUSHGSMIHEADER pCmd)
{
    NemuSHGSMIHeapFree(pHeap, pCmd);
}

DECLINLINE(void) nemuSHGSMICommandRelease (PNEMUSHGSMI pHeap, PNEMUSHGSMIHEADER pCmd)
{
    uint32_t cRefs = ASMAtomicDecU32(&pCmd->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if(!cRefs)
        nemuSHGSMICommandFree (pHeap, pCmd);
}

static DECLCALLBACK(void) nemuSHGSMICompletionSetEvent(PNEMUSHGSMI pHeap, void *pvCmd, void *pvContext)
{
    RTSemEventSignal((RTSEMEVENT)pvContext);
}

DECLCALLBACK(void) nemuSHGSMICompletionCommandRelease(PNEMUSHGSMI pHeap, void *pvCmd, void *pvContext)
{
    nemuSHGSMICommandRelease (pHeap, NemuSHGSMIBufferHeader(pvCmd));
}

/* do not wait for completion */
DECLINLINE(const NEMUSHGSMIHEADER*) nemuSHGSMICommandPrepAsynch (PNEMUSHGSMI pHeap, PNEMUSHGSMIHEADER pHeader)
{
    /* ensure the command is not removed until we're processing it */
    nemuSHGSMICommandRetain(pHeader);
    return pHeader;
}

DECLINLINE(void) nemuSHGSMICommandDoneAsynch (PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader)
{
    if(!(ASMAtomicReadU32((volatile uint32_t *)&pHeader->fFlags) & NEMUSHGSMI_FLAG_HG_ASYNCH))
    {
        PFNNEMUSHGSMICMDCOMPLETION pfnCompletion = (PFNNEMUSHGSMICMDCOMPLETION)pHeader->u64Info1;
        if (pfnCompletion)
            pfnCompletion(pHeap, NemuSHGSMIBufferData (pHeader), (PVOID)pHeader->u64Info2);
    }

    nemuSHGSMICommandRelease(pHeap, (PNEMUSHGSMIHEADER)pHeader);
}

const NEMUSHGSMIHEADER* NemuSHGSMICommandPrepAsynchEvent (PNEMUSHGSMI pHeap, PVOID pvBuff, RTSEMEVENT hEventSem)
{
    PNEMUSHGSMIHEADER pHeader = NemuSHGSMIBufferHeader (pvBuff);
    pHeader->u64Info1 = (uint64_t)nemuSHGSMICompletionSetEvent;
    pHeader->u64Info2 = (uint64_t)hEventSem;
    pHeader->fFlags   = NEMUSHGSMI_FLAG_GH_ASYNCH_IRQ;

    return nemuSHGSMICommandPrepAsynch (pHeap, pHeader);
}

const NEMUSHGSMIHEADER* NemuSHGSMICommandPrepSynch (PNEMUSHGSMI pHeap, PVOID pCmd)
{
    RTSEMEVENT hEventSem;
    int rc = RTSemEventCreate(&hEventSem);
    Assert(RT_SUCCESS(rc));
    if (RT_SUCCESS(rc))
    {
        return NemuSHGSMICommandPrepAsynchEvent (pHeap, pCmd, hEventSem);
    }
    return NULL;
}

void NemuSHGSMICommandDoneAsynch (PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER * pHeader)
{
    nemuSHGSMICommandDoneAsynch(pHeap, pHeader);
}

int NemuSHGSMICommandDoneSynch (PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader)
{
    NemuSHGSMICommandDoneAsynch (pHeap, pHeader);
    RTSEMEVENT hEventSem = (RTSEMEVENT)pHeader->u64Info2;
    int rc = RTSemEventWait(hEventSem, RT_INDEFINITE_WAIT);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
        RTSemEventDestroy(hEventSem);
    return rc;
}

void NemuSHGSMICommandCancelAsynch (PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader)
{
    nemuSHGSMICommandRelease(pHeap, (PNEMUSHGSMIHEADER)pHeader);
}

void NemuSHGSMICommandCancelSynch (PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader)
{
    NemuSHGSMICommandCancelAsynch (pHeap, pHeader);
    RTSEMEVENT hEventSem = (RTSEMEVENT)pHeader->u64Info2;
    RTSemEventDestroy(hEventSem);
}

const NEMUSHGSMIHEADER* NemuSHGSMICommandPrepAsynch (PNEMUSHGSMI pHeap, PVOID pvBuff, PFNNEMUSHGSMICMDCOMPLETION pfnCompletion, PVOID pvCompletion, uint32_t fFlags)
{
    fFlags &= ~NEMUSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ;
    PNEMUSHGSMIHEADER pHeader = NemuSHGSMIBufferHeader (pvBuff);
    pHeader->u64Info1 = (uint64_t)pfnCompletion;
    pHeader->u64Info2 = (uint64_t)pvCompletion;
    pHeader->fFlags = fFlags;

    return nemuSHGSMICommandPrepAsynch (pHeap, pHeader);
}

const NEMUSHGSMIHEADER* NemuSHGSMICommandPrepAsynchIrq (PNEMUSHGSMI pHeap, PVOID pvBuff, PFNNEMUSHGSMICMDCOMPLETION_IRQ pfnCompletion, PVOID pvCompletion, uint32_t fFlags)
{
    fFlags |= NEMUSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ | NEMUSHGSMI_FLAG_GH_ASYNCH_IRQ;
    PNEMUSHGSMIHEADER pHeader = NemuSHGSMIBufferHeader (pvBuff);
    pHeader->u64Info1 = (uint64_t)pfnCompletion;
    pHeader->u64Info2 = (uint64_t)pvCompletion;
    /* we must assign rather than or because flags field does not get zeroed on command creation */
    pHeader->fFlags = fFlags;

    return nemuSHGSMICommandPrepAsynch (pHeap, pHeader);
}

void* NemuSHGSMIHeapAlloc(PNEMUSHGSMI pHeap, HGSMISIZE cbData, uint8_t u8Channel, uint16_t u16ChannelInfo)
{
    KIRQL OldIrql;
    void* pvData;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&pHeap->HeapLock, &OldIrql);
    pvData = HGSMIHeapAlloc(&pHeap->Heap, cbData, u8Channel, u16ChannelInfo);
    KeReleaseSpinLock(&pHeap->HeapLock, OldIrql);
    if (!pvData)
        WARN(("HGSMIHeapAlloc failed!"));
    return pvData;
}

void NemuSHGSMIHeapFree(PNEMUSHGSMI pHeap, void *pvBuffer)
{
    KIRQL OldIrql;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&pHeap->HeapLock, &OldIrql);
    HGSMIHeapFree(&pHeap->Heap, pvBuffer);
    KeReleaseSpinLock(&pHeap->HeapLock, OldIrql);
}

void* NemuSHGSMIHeapBufferAlloc(PNEMUSHGSMI pHeap, HGSMISIZE cbData)
{
    KIRQL OldIrql;
    void* pvData;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&pHeap->HeapLock, &OldIrql);
    pvData = HGSMIHeapBufferAlloc(&pHeap->Heap, cbData);
    KeReleaseSpinLock(&pHeap->HeapLock, OldIrql);
    if (!pvData)
        WARN(("HGSMIHeapAlloc failed!"));
    return pvData;
}

void NemuSHGSMIHeapBufferFree(PNEMUSHGSMI pHeap, void *pvBuffer)
{
    KIRQL OldIrql;
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeAcquireSpinLock(&pHeap->HeapLock, &OldIrql);
    HGSMIHeapBufferFree(&pHeap->Heap, pvBuffer);
    KeReleaseSpinLock(&pHeap->HeapLock, OldIrql);
}

int NemuSHGSMIInit(PNEMUSHGSMI pHeap, void *pvBase, HGSMISIZE cbArea, HGSMIOFFSET offBase,
                   const HGSMIENV *pEnv)
{
    KeInitializeSpinLock(&pHeap->HeapLock);
    return HGSMIHeapSetup(&pHeap->Heap, pvBase, cbArea, offBase, pEnv);
}

void NemuSHGSMITerm(PNEMUSHGSMI pHeap)
{
    HGSMIHeapDestroy(&pHeap->Heap);
}

void* NemuSHGSMICommandAlloc(PNEMUSHGSMI pHeap, HGSMISIZE cbData, uint8_t u8Channel, uint16_t u16ChannelInfo)
{
    /* Issue the flush command. */
    PNEMUSHGSMIHEADER pHeader = (PNEMUSHGSMIHEADER)NemuSHGSMIHeapAlloc(pHeap, cbData + sizeof (NEMUSHGSMIHEADER), u8Channel, u16ChannelInfo);
    Assert(pHeader);
    if (pHeader)
    {
        pHeader->cRefs = 1;
        return NemuSHGSMIBufferData(pHeader);
    }
    return NULL;
}

void NemuSHGSMICommandFree(PNEMUSHGSMI pHeap, void *pvBuffer)
{
    PNEMUSHGSMIHEADER pHeader = NemuSHGSMIBufferHeader(pvBuffer);
    nemuSHGSMICommandRelease (pHeap, pHeader);
}

#define NEMUSHGSMI_CMD2LISTENTRY(_pCmd) ((PNEMUVTLIST_ENTRY)&(_pCmd)->pvNext)
#define NEMUSHGSMI_LISTENTRY2CMD(_pEntry) ( (PNEMUSHGSMIHEADER)((uint8_t *)(_pEntry) - RT_OFFSETOF(NEMUSHGSMIHEADER, pvNext)) )

int NemuSHGSMICommandProcessCompletion (PNEMUSHGSMI pHeap, NEMUSHGSMIHEADER* pCur, bool bIrq, PNEMUVTLIST pPostProcessList)
{
    int rc = VINF_SUCCESS;

    do
    {
        if (pCur->fFlags & NEMUSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ)
        {
            Assert(bIrq);

            PFNNEMUSHGSMICMDCOMPLETION pfnCompletion = NULL;
            void *pvCompletion;
            PFNNEMUSHGSMICMDCOMPLETION_IRQ pfnCallback = (PFNNEMUSHGSMICMDCOMPLETION_IRQ)pCur->u64Info1;
            void *pvCallback = (void*)pCur->u64Info2;

            pfnCompletion = pfnCallback(pHeap, NemuSHGSMIBufferData(pCur), pvCallback, &pvCompletion);
            if (pfnCompletion)
            {
                pCur->u64Info1 = (uint64_t)pfnCompletion;
                pCur->u64Info2 = (uint64_t)pvCompletion;
                pCur->fFlags &= ~NEMUSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ;
            }
            else
            {
                /* nothing to do with this command */
                break;
            }
        }

        if (!bIrq)
        {
            PFNNEMUSHGSMICMDCOMPLETION pfnCallback = (PFNNEMUSHGSMICMDCOMPLETION)pCur->u64Info1;
            void *pvCallback = (void*)pCur->u64Info2;
            pfnCallback(pHeap, NemuSHGSMIBufferData(pCur), pvCallback);
        }
        else
            nemuVtListPut(pPostProcessList, NEMUSHGSMI_CMD2LISTENTRY(pCur), NEMUSHGSMI_CMD2LISTENTRY(pCur));
    } while (0);


    return rc;
}

int NemuSHGSMICommandPostprocessCompletion (PNEMUSHGSMI pHeap, PNEMUVTLIST pPostProcessList)
{
    PNEMUVTLIST_ENTRY pNext, pCur;
    for (pCur = pPostProcessList->pFirst; pCur; pCur = pNext)
    {
        /* need to save next since the command may be released in a pfnCallback and thus its data might be invalid */
        pNext = pCur->pNext;
        PNEMUSHGSMIHEADER pCmd = NEMUSHGSMI_LISTENTRY2CMD(pCur);
        PFNNEMUSHGSMICMDCOMPLETION pfnCallback = (PFNNEMUSHGSMICMDCOMPLETION)pCmd->u64Info1;
        void *pvCallback = (void*)pCmd->u64Info2;
        pfnCallback(pHeap, NemuSHGSMIBufferData(pCmd), pvCallback);
    }

    return VINF_SUCCESS;
}
