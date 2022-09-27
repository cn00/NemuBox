/* $Id: NemuMPCm.h $ */

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

#ifndef ___NemuMPCm_h___
#define ___NemuMPCm_h___

typedef struct NEMUVIDEOCM_MGR
{
    KSPIN_LOCK SynchLock;
    /* session list */
    LIST_ENTRY SessionList;
} NEMUVIDEOCM_MGR, *PNEMUVIDEOCM_MGR;

typedef struct NEMUVIDEOCM_CTX
{
    LIST_ENTRY SessionEntry;
    struct NEMUVIDEOCM_SESSION *pSession;
    uint64_t u64UmData;
    NEMUWDDM_HTABLE AllocTable;
} NEMUVIDEOCM_CTX, *PNEMUVIDEOCM_CTX;

void nemuVideoCmCtxInitEmpty(PNEMUVIDEOCM_CTX pContext);

NTSTATUS nemuVideoCmCtxAdd(PNEMUVIDEOCM_MGR pMgr, PNEMUVIDEOCM_CTX pContext, HANDLE hUmEvent, uint64_t u64UmData);
NTSTATUS nemuVideoCmCtxRemove(PNEMUVIDEOCM_MGR pMgr, PNEMUVIDEOCM_CTX pContext);
NTSTATUS nemuVideoCmInit(PNEMUVIDEOCM_MGR pMgr);
NTSTATUS nemuVideoCmTerm(PNEMUVIDEOCM_MGR pMgr);
NTSTATUS nemuVideoCmSignalEvents(PNEMUVIDEOCM_MGR pMgr);

NTSTATUS nemuVideoCmCmdSubmitCompleteEvent(PNEMUVIDEOCM_CTX pContext, PKEVENT pEvent);
void* nemuVideoCmCmdCreate(PNEMUVIDEOCM_CTX pContext, uint32_t cbSize);
void* nemuVideoCmCmdReinitForContext(void *pvCmd, PNEMUVIDEOCM_CTX pContext);
void nemuVideoCmCmdRetain(void *pvCmd);
void nemuVideoCmCmdRelease(void *pvCmd);
#define NEMUVIDEOCM_SUBMITSIZE_DEFAULT (~0UL)
void nemuVideoCmCmdSubmit(void *pvCmd, uint32_t cbSize);

#define NEMUVIDEOCMCMDVISITOR_RETURN_BREAK    0x00000001
#define NEMUVIDEOCMCMDVISITOR_RETURN_RMCMD    0x00000002
typedef DECLCALLBACK(UINT) FNNEMUVIDEOCMCMDVISITOR(PNEMUVIDEOCM_CTX pContext, PVOID pvCmd, uint32_t cbCmd, PVOID pvVisitor);
typedef FNNEMUVIDEOCMCMDVISITOR *PFNNEMUVIDEOCMCMDVISITOR;
NTSTATUS nemuVideoCmCmdVisit(PNEMUVIDEOCM_CTX pContext, BOOLEAN bEntireSession, PFNNEMUVIDEOCMCMDVISITOR pfnVisitor, PVOID pvVisitor);

NTSTATUS nemuVideoCmEscape(PNEMUVIDEOCM_CTX pContext, PNEMUDISPIFESCAPE_GETNEMUVIDEOCMCMD pCmd, uint32_t cbCmd);

#endif /* #ifndef ___NemuMPCm_h___ */
