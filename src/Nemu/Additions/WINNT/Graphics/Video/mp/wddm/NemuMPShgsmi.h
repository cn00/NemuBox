/* $Id: NemuMPShgsmi.h $ */

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

#ifndef ___NemuMPShgsmi_h___
#define ___NemuMPShgsmi_h___

#include <iprt/cdefs.h>
#include <Nemu/NemuVideo.h>

#include "common/NemuMPUtils.h"

typedef struct NEMUSHGSMI
{
    KSPIN_LOCK HeapLock;
    HGSMIHEAP Heap;
} NEMUSHGSMI, *PNEMUSHGSMI;

typedef DECLCALLBACK(void) FNNEMUSHGSMICMDCOMPLETION(PNEMUSHGSMI pHeap, void *pvCmd, void *pvContext);
typedef FNNEMUSHGSMICMDCOMPLETION *PFNNEMUSHGSMICMDCOMPLETION;

typedef DECLCALLBACK(PFNNEMUSHGSMICMDCOMPLETION) FNNEMUSHGSMICMDCOMPLETION_IRQ(PNEMUSHGSMI pHeap, void *pvCmd, void *pvContext, void **ppvCompletion);
typedef FNNEMUSHGSMICMDCOMPLETION_IRQ *PFNNEMUSHGSMICMDCOMPLETION_IRQ;


const NEMUSHGSMIHEADER* NemuSHGSMICommandPrepAsynchEvent(PNEMUSHGSMI pHeap, PVOID pvBuff, RTSEMEVENT hEventSem);
const NEMUSHGSMIHEADER* NemuSHGSMICommandPrepSynch(PNEMUSHGSMI pHeap, PVOID pCmd);
const NEMUSHGSMIHEADER* NemuSHGSMICommandPrepAsynch(PNEMUSHGSMI pHeap, PVOID pvBuff, PFNNEMUSHGSMICMDCOMPLETION pfnCompletion, PVOID pvCompletion, uint32_t fFlags);
const NEMUSHGSMIHEADER* NemuSHGSMICommandPrepAsynchIrq(PNEMUSHGSMI pHeap, PVOID pvBuff, PFNNEMUSHGSMICMDCOMPLETION_IRQ pfnCompletion, PVOID pvCompletion, uint32_t fFlags);

void NemuSHGSMICommandDoneAsynch(PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader);
int NemuSHGSMICommandDoneSynch(PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader);
void NemuSHGSMICommandCancelAsynch(PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader);
void NemuSHGSMICommandCancelSynch(PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader);

DECLINLINE(HGSMIOFFSET) NemuSHGSMICommandOffset(const PNEMUSHGSMI pHeap, const NEMUSHGSMIHEADER* pHeader)
{
    return HGSMIHeapBufferOffset(&pHeap->Heap, (void*)pHeader);
}

/* allows getting VRAM offset of arbitrary pointer within the SHGSMI command
 * if invalid pointer is passed in, behavior is undefined */
DECLINLINE(HGSMIOFFSET) NemuSHGSMICommandPtrOffset(const PNEMUSHGSMI pHeap, const void * pvPtr)
{
    return HGSMIPointerToOffset (&pHeap->Heap.area, (const HGSMIBUFFERHEADER *)pvPtr);
}

int NemuSHGSMIInit(PNEMUSHGSMI pHeap, void *pvBase, HGSMISIZE cbArea, HGSMIOFFSET offBase, const HGSMIENV *pEnv);
void NemuSHGSMITerm(PNEMUSHGSMI pHeap);
void* NemuSHGSMIHeapAlloc(PNEMUSHGSMI pHeap, HGSMISIZE cbData, uint8_t u8Channel, uint16_t u16ChannelInfo);
void NemuSHGSMIHeapFree(PNEMUSHGSMI pHeap, void *pvBuffer);
void* NemuSHGSMIHeapBufferAlloc(PNEMUSHGSMI pHeap, HGSMISIZE cbData);
void NemuSHGSMIHeapBufferFree(PNEMUSHGSMI pHeap, void *pvBuffer);
void* NemuSHGSMICommandAlloc(PNEMUSHGSMI pHeap, HGSMISIZE cbData, uint8_t u8Channel, uint16_t u16ChannelInfo);
void NemuSHGSMICommandFree(PNEMUSHGSMI pHeap, void *pvBuffer);
int NemuSHGSMICommandProcessCompletion(PNEMUSHGSMI pHeap, NEMUSHGSMIHEADER* pCmd, bool bIrq, struct NEMUVTLIST * pPostProcessList);
int NemuSHGSMICommandPostprocessCompletion(PNEMUSHGSMI pHeap, struct NEMUVTLIST * pPostProcessList);

#endif /* #ifndef ___NemuMPShgsmi_h___ */
