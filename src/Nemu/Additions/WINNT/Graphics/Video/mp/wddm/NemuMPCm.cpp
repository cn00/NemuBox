/* $Id: NemuMPCm.cpp $ */

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

#include "NemuMPWddm.h"

typedef struct NEMUVIDEOCM_CMD_DR
{
    LIST_ENTRY QueueList;
    PNEMUVIDEOCM_CTX pContext;
    uint32_t cbMaxCmdSize;
    volatile uint32_t cRefs;

    NEMUVIDEOCM_CMD_HDR CmdHdr;
} NEMUVIDEOCM_CMD_DR, *PNEMUVIDEOCM_CMD_DR;

typedef enum
{
    NEMUVIDEOCM_CMD_CTL_KM_TYPE_POST_INVOKE = 1,
    NEMUVIDEOCM_CMD_CTL_KM_TYPE_PRE_INVOKE,
    NEMUVIDEOCM_CMD_CTL_KM_TYPE_DUMMY_32BIT = 0x7fffffff
} NEMUVIDEOCM_CMD_CTL_KM_TYPE;

typedef DECLCALLBACK(VOID) FNNEMUVIDEOCM_CMD_CB(PNEMUVIDEOCM_CTX pContext, struct NEMUVIDEOCM_CMD_CTL_KM *pCmd, PVOID pvContext);
typedef FNNEMUVIDEOCM_CMD_CB *PFNNEMUVIDEOCM_CMD_CB;

typedef struct NEMUVIDEOCM_CMD_CTL_KM
{
    NEMUVIDEOCM_CMD_CTL_KM_TYPE enmType;
    uint32_t u32Reserved;
    PFNNEMUVIDEOCM_CMD_CB pfnCb;
    PVOID pvCb;
} NEMUVIDEOCM_CMD_CTL_KM, *PNEMUVIDEOCM_CMD_CTL_KM;

AssertCompile(NEMUWDDM_ROUNDBOUND(RT_OFFSETOF(NEMUVIDEOCM_CMD_DR, CmdHdr), 8) == RT_OFFSETOF(NEMUVIDEOCM_CMD_DR, CmdHdr));

#define NEMUVIDEOCM_HEADER_SIZE() (NEMUWDDM_ROUNDBOUND(sizeof (NEMUVIDEOCM_CMD_DR), 8))
#define NEMUVIDEOCM_SIZE_FROMBODYSIZE(_s) (NEMUVIDEOCM_HEADER_SIZE() + (_s))
//#define NEMUVIDEOCM_SIZE(_t) (NEMUVIDEOCM_SIZE_FROMBODYSIZE(sizeof (_t)))
#define NEMUVIDEOCM_BODY(_pCmd, _t) ( (_t*)(((uint8_t*)(_pCmd)) + NEMUVIDEOCM_HEADER_SIZE()) )
#define NEMUVIDEOCM_HEAD(_pCmd) ( (PNEMUVIDEOCM_CMD_DR)(((uint8_t*)(_pCmd)) - NEMUVIDEOCM_HEADER_SIZE()) )

#define NEMUVIDEOCM_SENDSIZE_FROMBODYSIZE(_s) ( NEMUVIDEOCM_SIZE_FROMBODYSIZE(_s) - RT_OFFSETOF(NEMUVIDEOCM_CMD_DR, CmdHdr))

//#define NEMUVIDEOCM_BODY_FIELD_OFFSET(_ot, _t, _f) ( (_ot)( NEMUVIDEOCM_BODY(0, uint8_t) + RT_OFFSETOF(_t, _f) ) )

typedef struct NEMUVIDEOCM_SESSION
{
    /* contexts in this session */
    LIST_ENTRY QueueEntry;
    /* contexts in this session */
    LIST_ENTRY ContextList;
    /* commands list  */
    LIST_ENTRY CommandsList;
    /* post process commands list  */
    LIST_ENTRY PpCommandsList;
    /* event used to notify UMD about pending commands */
    PKEVENT pUmEvent;
    /* sync lock */
    KSPIN_LOCK SynchLock;
    /* indicates whether event signaling is needed on cmd add */
    bool bEventNeeded;
} NEMUVIDEOCM_SESSION, *PNEMUVIDEOCM_SESSION;

#define NEMUCMENTRY_2_CMD(_pE) ((PNEMUVIDEOCM_CMD_DR)((uint8_t*)(_pE) - RT_OFFSETOF(NEMUVIDEOCM_CMD_DR, QueueList)))

void* nemuVideoCmCmdReinitForContext(void *pvCmd, PNEMUVIDEOCM_CTX pContext)
{
    PNEMUVIDEOCM_CMD_DR pHdr = NEMUVIDEOCM_HEAD(pvCmd);
    pHdr->pContext = pContext;
    pHdr->CmdHdr.u64UmData = pContext->u64UmData;
    return pvCmd;
}

void* nemuVideoCmCmdCreate(PNEMUVIDEOCM_CTX pContext, uint32_t cbSize)
{
    Assert(cbSize);
    if (!cbSize)
        return NULL;

    Assert(NEMUWDDM_ROUNDBOUND(cbSize, 8) == cbSize);
    cbSize = NEMUWDDM_ROUNDBOUND(cbSize, 8);

    Assert(pContext->pSession);
    if (!pContext->pSession)
        return NULL;

    uint32_t cbCmd = NEMUVIDEOCM_SIZE_FROMBODYSIZE(cbSize);
    PNEMUVIDEOCM_CMD_DR pCmd = (PNEMUVIDEOCM_CMD_DR)nemuWddmMemAllocZero(cbCmd);
    Assert(pCmd);
    if (pCmd)
    {
        InitializeListHead(&pCmd->QueueList);
        pCmd->pContext = pContext;
        pCmd->cbMaxCmdSize = NEMUVIDEOCM_SENDSIZE_FROMBODYSIZE(cbSize);
        pCmd->cRefs = 1;
        pCmd->CmdHdr.u64UmData = pContext->u64UmData;
        pCmd->CmdHdr.cbCmd = pCmd->cbMaxCmdSize;
    }
    return NEMUVIDEOCM_BODY(pCmd, void);
}

static PNEMUVIDEOCM_CMD_CTL_KM nemuVideoCmCmdCreateKm(PNEMUVIDEOCM_CTX pContext, NEMUVIDEOCM_CMD_CTL_KM_TYPE enmType,
        PFNNEMUVIDEOCM_CMD_CB pfnCb, PVOID pvCb,
        uint32_t cbSize)
{
    PNEMUVIDEOCM_CMD_CTL_KM pCmd = (PNEMUVIDEOCM_CMD_CTL_KM)nemuVideoCmCmdCreate(pContext, cbSize + sizeof (*pCmd));
    pCmd->enmType = enmType;
    pCmd->pfnCb = pfnCb;
    pCmd->pvCb = pvCb;
    PNEMUVIDEOCM_CMD_DR pHdr = NEMUVIDEOCM_HEAD(pCmd);
    pHdr->CmdHdr.enmType = NEMUVIDEOCM_CMD_TYPE_CTL_KM;
    return pCmd;
}

static DECLCALLBACK(VOID) nemuVideoCmCmdCbSetEventAndDereference(PNEMUVIDEOCM_CTX pContext, PNEMUVIDEOCM_CMD_CTL_KM pCmd, PVOID pvContext)
{
    PKEVENT pEvent = (PKEVENT)pvContext;
    KeSetEvent(pEvent, 0, FALSE);
    ObDereferenceObject(pEvent);
    nemuVideoCmCmdRelease(pCmd);
}

NTSTATUS nemuVideoCmCmdSubmitCompleteEvent(PNEMUVIDEOCM_CTX pContext, PKEVENT pEvent)
{
    Assert(pEvent);
    PNEMUVIDEOCM_CMD_CTL_KM pCmd = nemuVideoCmCmdCreateKm(pContext, NEMUVIDEOCM_CMD_CTL_KM_TYPE_POST_INVOKE,
            nemuVideoCmCmdCbSetEventAndDereference, pEvent, 0);
    if (!pCmd)
    {
        WARN(("nemuVideoCmCmdCreateKm failed"));
        return STATUS_NO_MEMORY;
    }

    nemuVideoCmCmdSubmit(pCmd, NEMUVIDEOCM_SUBMITSIZE_DEFAULT);

    return STATUS_SUCCESS;
}

DECLINLINE(void) nemuVideoCmCmdRetainByHdr(PNEMUVIDEOCM_CMD_DR pHdr)
{
    ASMAtomicIncU32(&pHdr->cRefs);
}

DECLINLINE(void) nemuVideoCmCmdReleaseByHdr(PNEMUVIDEOCM_CMD_DR pHdr)
{
    uint32_t cRefs = ASMAtomicDecU32(&pHdr->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    if (!cRefs)
        nemuWddmMemFree(pHdr);
}

static void nemuVideoCmCmdCancel(PNEMUVIDEOCM_CMD_DR pHdr)
{
    InitializeListHead(&pHdr->QueueList);
    nemuVideoCmCmdReleaseByHdr(pHdr);
}

static void nemuVideoCmCmdPostByHdr(PNEMUVIDEOCM_SESSION pSession, PNEMUVIDEOCM_CMD_DR pHdr, uint32_t cbSize)
{
    bool bSignalEvent = false;
    if (cbSize != NEMUVIDEOCM_SUBMITSIZE_DEFAULT)
    {
        cbSize = NEMUVIDEOCM_SENDSIZE_FROMBODYSIZE(cbSize);
        Assert(cbSize <= pHdr->cbMaxCmdSize);
        pHdr->CmdHdr.cbCmd = cbSize;
    }

    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    InsertHeadList(&pSession->CommandsList, &pHdr->QueueList);
    if (pSession->bEventNeeded)
    {
        pSession->bEventNeeded = false;
        bSignalEvent = true;
    }

    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);

    if (bSignalEvent)
        KeSetEvent(pSession->pUmEvent, 0, FALSE);
}

void nemuVideoCmCmdRetain(void *pvCmd)
{
    PNEMUVIDEOCM_CMD_DR pHdr = NEMUVIDEOCM_HEAD(pvCmd);
    nemuVideoCmCmdRetainByHdr(pHdr);
}

void nemuVideoCmCmdRelease(void *pvCmd)
{
    PNEMUVIDEOCM_CMD_DR pHdr = NEMUVIDEOCM_HEAD(pvCmd);
    nemuVideoCmCmdReleaseByHdr(pHdr);
}

/**
 * @param pvCmd memory buffer returned by nemuVideoCmCmdCreate
 * @param cbSize should be <= cbSize posted to nemuVideoCmCmdCreate on command creation
 */
void nemuVideoCmCmdSubmit(void *pvCmd, uint32_t cbSize)
{
    PNEMUVIDEOCM_CMD_DR pHdr = NEMUVIDEOCM_HEAD(pvCmd);
    nemuVideoCmCmdPostByHdr(pHdr->pContext->pSession, pHdr, cbSize);
}

NTSTATUS nemuVideoCmCmdVisit(PNEMUVIDEOCM_CTX pContext, BOOLEAN bEntireSession, PFNNEMUVIDEOCMCMDVISITOR pfnVisitor, PVOID pvVisitor)
{
    PNEMUVIDEOCM_SESSION pSession = pContext->pSession;
    PLIST_ENTRY pCurEntry = NULL;
    PNEMUVIDEOCM_CMD_DR pHdr;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    pCurEntry = pSession->CommandsList.Blink;
    do
    {
        if (pCurEntry != &pSession->CommandsList)
        {
            pHdr = NEMUCMENTRY_2_CMD(pCurEntry);
            pCurEntry = pHdr->QueueList.Blink;
            if (bEntireSession || pHdr->pContext == pContext)
            {
                if (pHdr->CmdHdr.enmType == NEMUVIDEOCM_CMD_TYPE_UM)
                {
                    void * pvBody = NEMUVIDEOCM_BODY(pHdr, void);
                    UINT fRet = pfnVisitor(pHdr->pContext, pvBody, pHdr->CmdHdr.cbCmd, pvVisitor);
                    if (fRet & NEMUVIDEOCMCMDVISITOR_RETURN_RMCMD)
                    {
                        RemoveEntryList(&pHdr->QueueList);
                    }
                    if ((fRet & NEMUVIDEOCMCMDVISITOR_RETURN_BREAK))
                        break;
                }
                else
                {
                    WARN(("non-um cmd on visit, skipping"));
                }
            }
        }
        else
        {
            break;
        }
    } while (1);


    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);

    return STATUS_SUCCESS;
}

void nemuVideoCmCtxInitEmpty(PNEMUVIDEOCM_CTX pContext)
{
    InitializeListHead(&pContext->SessionEntry);
    pContext->pSession = NULL;
    pContext->u64UmData = 0ULL;
}

static void nemuVideoCmSessionCtxAddLocked(PNEMUVIDEOCM_SESSION pSession, PNEMUVIDEOCM_CTX pContext)
{
    InsertHeadList(&pSession->ContextList, &pContext->SessionEntry);
    pContext->pSession = pSession;
}

void nemuVideoCmSessionCtxAdd(PNEMUVIDEOCM_SESSION pSession, PNEMUVIDEOCM_CTX pContext)
{
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    nemuVideoCmSessionCtxAddLocked(pSession, pContext);

    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);
}

void nemuVideoCmSessionSignalEvent(PNEMUVIDEOCM_SESSION pSession)
{
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    if (pSession->pUmEvent)
        KeSetEvent(pSession->pUmEvent, 0, FALSE);
}

static void nemuVideoCmSessionDestroyLocked(PNEMUVIDEOCM_SESSION pSession)
{
    /* signal event so that user-space client can figure out the context is destroyed
     * in case the context destroyal is caused by Graphics device reset or miniport driver update */
    KeSetEvent(pSession->pUmEvent, 0, FALSE);
    ObDereferenceObject(pSession->pUmEvent);
    Assert(IsListEmpty(&pSession->ContextList));
    Assert(IsListEmpty(&pSession->CommandsList));
    Assert(IsListEmpty(&pSession->PpCommandsList));
    RemoveEntryList(&pSession->QueueEntry);
    nemuWddmMemFree(pSession);
}

static void nemuVideoCmSessionCtxPpList(PNEMUVIDEOCM_CTX pContext, PLIST_ENTRY pHead)
{
    LIST_ENTRY *pCur;
    for (pCur = pHead->Flink; pCur != pHead; pCur = pHead->Flink)
    {
        RemoveEntryList(pCur);
        PNEMUVIDEOCM_CMD_DR pHdr = NEMUCMENTRY_2_CMD(pCur);
        PNEMUVIDEOCM_CMD_CTL_KM pCmd = NEMUVIDEOCM_BODY(pHdr, NEMUVIDEOCM_CMD_CTL_KM);
        pCmd->pfnCb(pContext, pCmd, pCmd->pvCb);
    }
}

static void nemuVideoCmSessionCtxDetachCmdsLocked(PLIST_ENTRY pEntriesHead, PNEMUVIDEOCM_CTX pContext, PLIST_ENTRY pDstHead)
{
    LIST_ENTRY *pCur;
    LIST_ENTRY *pPrev;
    pCur = pEntriesHead->Flink;
    pPrev = pEntriesHead;
    while (pCur != pEntriesHead)
    {
        PNEMUVIDEOCM_CMD_DR pCmd = NEMUCMENTRY_2_CMD(pCur);
        if (pCmd->pContext == pContext)
        {
            RemoveEntryList(pCur);
            InsertTailList(pDstHead, pCur);
            pCur = pPrev;
            /* pPrev - remains unchanged */
        }
        else
        {
            pPrev = pCur;
        }
        pCur = pCur->Flink;
    }
}
/**
 * @return true iff the given session is destroyed
 */
bool nemuVideoCmSessionCtxRemoveLocked(PNEMUVIDEOCM_SESSION pSession, PNEMUVIDEOCM_CTX pContext)
{
    bool bDestroy;
    LIST_ENTRY RemainedList;
    LIST_ENTRY RemainedPpList;
    LIST_ENTRY *pCur;
    InitializeListHead(&RemainedList);
    InitializeListHead(&RemainedPpList);
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    pContext->pSession = NULL;
    RemoveEntryList(&pContext->SessionEntry);
    bDestroy = !!(IsListEmpty(&pSession->ContextList));
    /* ensure there are no commands left for the given context */
    if (bDestroy)
    {
        nemuVideoLeDetach(&pSession->CommandsList, &RemainedList);
        nemuVideoLeDetach(&pSession->PpCommandsList, &RemainedPpList);
    }
    else
    {
        nemuVideoCmSessionCtxDetachCmdsLocked(&pSession->CommandsList, pContext, &RemainedList);
        nemuVideoCmSessionCtxDetachCmdsLocked(&pSession->PpCommandsList, pContext, &RemainedPpList);
    }

    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);

    for (pCur = RemainedList.Flink; pCur != &RemainedList; pCur = RemainedList.Flink)
    {
        RemoveEntryList(pCur);
        PNEMUVIDEOCM_CMD_DR pCmd = NEMUCMENTRY_2_CMD(pCur);
        nemuVideoCmCmdCancel(pCmd);
    }

    nemuVideoCmSessionCtxPpList(pContext, &RemainedPpList);

    if (bDestroy)
    {
        nemuVideoCmSessionDestroyLocked(pSession);
    }

    return bDestroy;
}

/* the session gets destroyed once the last context is removed from it */
NTSTATUS nemuVideoCmSessionCreateLocked(PNEMUVIDEOCM_MGR pMgr, PNEMUVIDEOCM_SESSION *ppSession, PKEVENT pUmEvent, PNEMUVIDEOCM_CTX pContext)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PNEMUVIDEOCM_SESSION pSession = (PNEMUVIDEOCM_SESSION)nemuWddmMemAllocZero(sizeof (NEMUVIDEOCM_SESSION));
    Assert(pSession);
    if (pSession)
    {
        InitializeListHead(&pSession->ContextList);
        InitializeListHead(&pSession->CommandsList);
        InitializeListHead(&pSession->PpCommandsList);
        pSession->pUmEvent = pUmEvent;
        Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
        KeInitializeSpinLock(&pSession->SynchLock);
        pSession->bEventNeeded = true;
        nemuVideoCmSessionCtxAddLocked(pSession, pContext);
        InsertHeadList(&pMgr->SessionList, &pSession->QueueEntry);
        *ppSession = pSession;
        return STATUS_SUCCESS;
//        nemuWddmMemFree(pSession);
    }
    else
    {
        Status = STATUS_NO_MEMORY;
    }
    return Status;
}

#define NEMUCMENTRY_2_SESSION(_pE) ((PNEMUVIDEOCM_SESSION)((uint8_t*)(_pE) - RT_OFFSETOF(NEMUVIDEOCM_SESSION, QueueEntry)))

NTSTATUS nemuVideoCmCtxAdd(PNEMUVIDEOCM_MGR pMgr, PNEMUVIDEOCM_CTX pContext, HANDLE hUmEvent, uint64_t u64UmData)
{
    PKEVENT pUmEvent = NULL;
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);
    NTSTATUS Status = ObReferenceObjectByHandle(hUmEvent, EVENT_MODIFY_STATE, *ExEventObjectType, UserMode,
        (PVOID*)&pUmEvent,
        NULL);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        KIRQL OldIrql;
        KeAcquireSpinLock(&pMgr->SynchLock, &OldIrql);

        bool bFound = false;
        PNEMUVIDEOCM_SESSION pSession = NULL;
        for (PLIST_ENTRY pEntry = pMgr->SessionList.Flink; pEntry != &pMgr->SessionList; pEntry = pEntry->Flink)
        {
            pSession = NEMUCMENTRY_2_SESSION(pEntry);
            if (pSession->pUmEvent == pUmEvent)
            {
                bFound = true;
                break;
            }
        }

        pContext->u64UmData = u64UmData;

        if (!bFound)
        {
            Status = nemuVideoCmSessionCreateLocked(pMgr, &pSession, pUmEvent, pContext);
            Assert(Status == STATUS_SUCCESS);
        }
        else
        {
            /* Status = */nemuVideoCmSessionCtxAdd(pSession, pContext);
            /*Assert(Status == STATUS_SUCCESS);*/
        }

        KeReleaseSpinLock(&pMgr->SynchLock, OldIrql);

        if (Status == STATUS_SUCCESS)
        {
            return STATUS_SUCCESS;
        }

        ObDereferenceObject(pUmEvent);
    }
    return Status;
}

NTSTATUS nemuVideoCmCtxRemove(PNEMUVIDEOCM_MGR pMgr, PNEMUVIDEOCM_CTX pContext)
{
    PNEMUVIDEOCM_SESSION pSession = pContext->pSession;
    if (!pSession)
        return STATUS_SUCCESS;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pMgr->SynchLock, &OldIrql);

    nemuVideoCmSessionCtxRemoveLocked(pSession, pContext);

    KeReleaseSpinLock(&pMgr->SynchLock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS nemuVideoCmInit(PNEMUVIDEOCM_MGR pMgr)
{
    KeInitializeSpinLock(&pMgr->SynchLock);
    InitializeListHead(&pMgr->SessionList);
    return STATUS_SUCCESS;
}

NTSTATUS nemuVideoCmTerm(PNEMUVIDEOCM_MGR pMgr)
{
    Assert(IsListEmpty(&pMgr->SessionList));
    return STATUS_SUCCESS;
}

NTSTATUS nemuVideoCmSignalEvents(PNEMUVIDEOCM_MGR pMgr)
{
    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    PNEMUVIDEOCM_SESSION pSession = NULL;

    KIRQL OldIrql;
    KeAcquireSpinLock(&pMgr->SynchLock, &OldIrql);

    for (PLIST_ENTRY pEntry = pMgr->SessionList.Flink; pEntry != &pMgr->SessionList; pEntry = pEntry->Flink)
    {
        pSession = NEMUCMENTRY_2_SESSION(pEntry);
        nemuVideoCmSessionSignalEvent(pSession);
    }

    KeReleaseSpinLock(&pMgr->SynchLock, OldIrql);

    return STATUS_SUCCESS;
}

VOID nemuVideoCmProcessKm(PNEMUVIDEOCM_CTX pContext, PNEMUVIDEOCM_CMD_CTL_KM pCmd)
{
    PNEMUVIDEOCM_SESSION pSession = pContext->pSession;

    switch (pCmd->enmType)
    {
        case NEMUVIDEOCM_CMD_CTL_KM_TYPE_PRE_INVOKE:
        {
            pCmd->pfnCb(pContext, pCmd, pCmd->pvCb);
            break;
        }

        case NEMUVIDEOCM_CMD_CTL_KM_TYPE_POST_INVOKE:
        {
            PNEMUVIDEOCM_CMD_DR pHdr = NEMUVIDEOCM_HEAD(pCmd);
            KIRQL OldIrql;
            KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);
            InsertTailList(&pSession->PpCommandsList, &pHdr->QueueList);
            KeReleaseSpinLock(&pSession->SynchLock, OldIrql);
            break;
        }

        default:
        {
            WARN(("unsupported cmd type %d", pCmd->enmType));
            break;
        }
    }
}

NTSTATUS nemuVideoCmEscape(PNEMUVIDEOCM_CTX pContext, PNEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD pCmd, uint32_t cbCmd)
{
    Assert(cbCmd >= sizeof (NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD));
    if (cbCmd < sizeof (NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD))
        return STATUS_BUFFER_TOO_SMALL;

    PNEMUVIDEOCM_SESSION pSession = pContext->pSession;
    PNEMUVIDEOCM_CMD_DR pHdr;
    LIST_ENTRY DetachedList;
    LIST_ENTRY DetachedPpList;
    PLIST_ENTRY pCurEntry = NULL;
    uint32_t cbCmdsReturned = 0;
    uint32_t cbRemainingCmds = 0;
    uint32_t cbRemainingFirstCmd = 0;
    uint32_t cbData = cbCmd - sizeof (NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD);
    uint8_t * pvData = ((uint8_t *)pCmd) + sizeof (NEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD);
    bool bDetachMode = true;
    InitializeListHead(&DetachedList);
    InitializeListHead(&DetachedPpList);
//    PNEMUWDDM_GETNEMUVIDEOCMCMD_HDR *pvCmd

    Assert(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

    nemuVideoCmSessionCtxDetachCmdsLocked(&pSession->PpCommandsList, pContext, &DetachedPpList);

    do
    {
        if (bDetachMode)
        {
            if (!IsListEmpty(&pSession->CommandsList))
            {
                Assert(!pCurEntry);
                pHdr = NEMUCMENTRY_2_CMD(pSession->CommandsList.Blink);
                Assert(pHdr->CmdHdr.cbCmd);
                uint32_t cbUserCmd = pHdr->CmdHdr.enmType == NEMUVIDEOCM_CMD_TYPE_UM ? pHdr->CmdHdr.cbCmd : 0;
                if (cbData >= cbUserCmd)
                {
                    RemoveEntryList(&pHdr->QueueList);
                    InsertHeadList(&DetachedList, &pHdr->QueueList);
                    cbData -= cbUserCmd;
                }
                else
                {
                    Assert(cbUserCmd);
                    cbRemainingFirstCmd = cbUserCmd;
                    cbRemainingCmds = cbUserCmd;
                    pCurEntry = pHdr->QueueList.Blink;
                    bDetachMode = false;
                }
            }
            else
            {
                pSession->bEventNeeded = true;
                break;
            }
        }
        else
        {
            Assert(pCurEntry);
            if (pCurEntry != &pSession->CommandsList)
            {
                pHdr = NEMUCMENTRY_2_CMD(pCurEntry);
                uint32_t cbUserCmd = pHdr->CmdHdr.enmType == NEMUVIDEOCM_CMD_TYPE_UM ? pHdr->CmdHdr.cbCmd : 0;
                Assert(cbRemainingFirstCmd);
                cbRemainingCmds += cbUserCmd;
                pCurEntry = pHdr->QueueList.Blink;
            }
            else
            {
                Assert(cbRemainingFirstCmd);
                Assert(cbRemainingCmds);
                break;
            }
        }
    } while (1);

    KeReleaseSpinLock(&pSession->SynchLock, OldIrql);

    nemuVideoCmSessionCtxPpList(pContext, &DetachedPpList);

    pCmd->Hdr.cbCmdsReturned = 0;
    for (pCurEntry = DetachedList.Blink; pCurEntry != &DetachedList; pCurEntry = DetachedList.Blink)
    {
        pHdr = NEMUCMENTRY_2_CMD(pCurEntry);
        RemoveEntryList(pCurEntry);
        switch (pHdr->CmdHdr.enmType)
        {
            case NEMUVIDEOCM_CMD_TYPE_UM:
            {
                memcpy(pvData, &pHdr->CmdHdr, pHdr->CmdHdr.cbCmd);
                pvData += pHdr->CmdHdr.cbCmd;
                pCmd->Hdr.cbCmdsReturned += pHdr->CmdHdr.cbCmd;
                nemuVideoCmCmdReleaseByHdr(pHdr);
                break;
            }

            case NEMUVIDEOCM_CMD_TYPE_CTL_KM:
            {
                nemuVideoCmProcessKm(pContext, NEMUVIDEOCM_BODY(pHdr, NEMUVIDEOCM_CMD_CTL_KM));
                break;
            }

            default:
            {
                WARN(("unsupported cmd type %d", pHdr->CmdHdr.enmType));
                break;
            }
        }
    }

    pCmd->Hdr.cbRemainingCmds = cbRemainingCmds;
    pCmd->Hdr.cbRemainingFirstCmd = cbRemainingFirstCmd;
    pCmd->Hdr.u32Reserved = 0;

    return STATUS_SUCCESS;
}

static BOOLEAN nemuVideoCmHasUncompletedCmdsLocked(PNEMUVIDEOCM_MGR pMgr)
{
    PNEMUVIDEOCM_SESSION pSession = NULL;
    for (PLIST_ENTRY pEntry = pMgr->SessionList.Flink; pEntry != &pMgr->SessionList; pEntry = pEntry->Flink)
    {
        pSession = NEMUCMENTRY_2_SESSION(pEntry);
        KIRQL OldIrql;
        KeAcquireSpinLock(&pSession->SynchLock, &OldIrql);

        if (pSession->bEventNeeded)
        {
            /* commands still being processed */
            KeReleaseSpinLock(&pSession->SynchLock, OldIrql);
            return TRUE;
        }
        KeReleaseSpinLock(&pSession->SynchLock, OldIrql);
    }
    return FALSE;
}
