/* $Id: memobj-r0drv-solaris.h $ */
/** @file
 * IPRT - Ring-0 Memory Objects - Segment driver, Solaris.
 */

/*
 * Copyright (C) 2012-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


#ifndef ___r0drv_solaris_memobj_r0drv_solaris_h
#define ___r0drv_solaris_memobj_r0drv_solaris_h

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-solaris-kernel.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
typedef struct SEGNEMU_CRARGS
{
    uint64_t *paPhysAddrs;
    size_t    cbPageSize;
    uint_t    fPageAccess;
} SEGNEMU_CRARGS;
typedef SEGNEMU_CRARGS *PSEGNEMU_CRARGS;

typedef struct SEGNEMU_DATA
{
    uint_t    fPageAccess;
    size_t    cbPageSize;
} SEGNEMU_DATA;
typedef SEGNEMU_DATA *PSEGNEMU_DATA;

static struct seg_ops s_SegNemuOps;
static vnode_t s_segNemuVnode;


DECLINLINE(int) rtR0SegNemuSolCreate(seg_t *pSeg, void *pvArgs)
{
    struct as      *pAddrSpace = pSeg->s_as;
    PSEGNEMU_CRARGS pArgs      = pvArgs;
    PSEGNEMU_DATA   pData      = kmem_zalloc(sizeof(*pData), KM_SLEEP);

    AssertPtr(pAddrSpace);
    AssertPtr(pArgs);
    AssertPtr(pData);

    /*
     * Currently we only map _4K pages but this segment driver can handle any size
     * supported by the Solaris HAT layer.
     */
    size_t cbPageSize  = pArgs->cbPageSize;
    size_t uPageShift  = 0;
    switch (cbPageSize)
    {
        case _4K: uPageShift = 12; break;
        case _2M: uPageShift = 21; break;
        default:  AssertReleaseMsgFailed(("Unsupported page size for mapping cbPageSize=%llx\n", cbPageSize)); break;
    }

    hat_map(pAddrSpace->a_hat, pSeg->s_base, pSeg->s_size, HAT_MAP);
    pData->fPageAccess = pArgs->fPageAccess | PROT_USER;
    pData->cbPageSize  = cbPageSize;

    pSeg->s_ops  = &s_SegNemuOps;
    pSeg->s_data = pData;

    /*
     * Now load and lock down the mappings to the physical addresses.
     */
    caddr_t virtAddr = pSeg->s_base;
    pgcnt_t cPages   = (pSeg->s_size + cbPageSize - 1) >> uPageShift;
    for (pgcnt_t iPage = 0; iPage < cPages; ++iPage, virtAddr += cbPageSize)
    {
        hat_devload(pAddrSpace->a_hat, virtAddr, cbPageSize, pArgs->paPhysAddrs[iPage] >> uPageShift,
                    pData->fPageAccess | HAT_UNORDERED_OK, HAT_LOAD_LOCK);
    }

    return 0;
}


static int rtR0SegNemuSolDup(seg_t *pSrcSeg, seg_t *pDstSeg)
{
    /*
     * Duplicate a segment and return the new segment in 'pDstSeg'.
     */
    PSEGNEMU_DATA pSrcData = pSrcSeg->s_data;
    PSEGNEMU_DATA pDstData = kmem_zalloc(sizeof(*pDstData), KM_SLEEP);

    AssertPtr(pDstData);
    AssertPtr(pSrcData);

    pDstData->fPageAccess  = pSrcData->fPageAccess;
    pDstData->cbPageSize   = pSrcData->cbPageSize;
    pDstSeg->s_ops         = &s_SegNemuOps;
    pDstSeg->s_data        = pDstData;

    return 0;
}


static int rtR0SegNemuSolUnmap(seg_t *pSeg, caddr_t virtAddr, size_t cb)
{
    PSEGNEMU_DATA pData = pSeg->s_data;

    AssertRelease(pData);
    AssertReleaseMsg(virtAddr >= pSeg->s_base, ("virtAddr=%p s_base=%p\n", virtAddr, pSeg->s_base));
    AssertReleaseMsg(virtAddr + cb <= pSeg->s_base + pSeg->s_size, ("virtAddr=%p cb=%llu s_base=%p s_size=%llu\n", virtAddr,
                                                                    cb, pSeg->s_base, pSeg->s_size));
    size_t cbPageOffset = pData->cbPageSize - 1;
    AssertRelease(!(cb & cbPageOffset));
    AssertRelease(!((uintptr_t)virtAddr & cbPageOffset));

    if (   virtAddr != pSeg->s_base
        || cb       != pSeg->s_size)
    {
        return ENOTSUP;
    }

    hat_unload(pSeg->s_as->a_hat, virtAddr, cb, HAT_UNLOAD_UNMAP | HAT_UNLOAD_UNLOCK);

    seg_free(pSeg);
    return 0;
}


static void rtR0SegNemuSolFree(seg_t *pSeg)
{
    PSEGNEMU_DATA pData = pSeg->s_data;
    kmem_free(pData, sizeof(*pData));
}


static int rtR0SegNemuSolFault(struct hat *pHat, seg_t *pSeg, caddr_t virtAddr, size_t cb, enum fault_type FaultType,
                               enum seg_rw ReadWrite)
{
    /*
     * We would demand fault if the (u)read() path would SEGOP_FAULT() on buffers mapped in via our
     * segment driver i.e. prefaults before DMA. Don't fail in such case where we're called directly,
     * see @bugref{5047}.
     */
    return 0;
}


static int rtR0SegNemuSolFaultA(seg_t *pSeg, caddr_t virtAddr)
{
    return 0;
}


static int rtR0SegNemuSolSetProt(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t fPageAccess)
{
    return EACCES;
}


static int rtR0SegNemuSolCheckProt(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t fPageAccess)
{
    return EINVAL;
}


static int rtR0SegNemuSolKluster(seg_t *pSeg, caddr_t virtAddr, ssize_t Delta)
{
    return -1;
}


static int rtR0SegNemuSolSync(seg_t *pSeg, caddr_t virtAddr, size_t cb, int Attr, uint_t fFlags)
{
    return 0;
}


static size_t rtR0SegNemuSolInCore(seg_t *pSeg, caddr_t virtAddr, size_t cb, char *pVec)
{
    PSEGNEMU_DATA pData = pSeg->s_data;
    AssertRelease(pData);
    size_t uPageOffset  = pData->cbPageSize - 1;
    size_t uPageMask    = ~uPageOffset;
    size_t cbLen        = (cb + uPageOffset) & uPageMask;
    for (virtAddr = 0; cbLen != 0; cbLen -= pData->cbPageSize, virtAddr += pData->cbPageSize)
        *pVec++ = 1;
    return cbLen;
}


static int rtR0SegNemuSolLockOp(seg_t *pSeg, caddr_t virtAddr, size_t cb, int Attr, int Op, ulong_t *pLockMap, size_t off)
{
    return 0;
}


static int rtR0SegNemuSolGetProt(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t *pafPageAccess)
{
    PSEGNEMU_DATA pData = pSeg->s_data;
    size_t iPage = seg_page(pSeg, virtAddr + cb) - seg_page(pSeg, virtAddr) + 1;
    if (iPage)
    {
        do
        {
            iPage--;
            pafPageAccess[iPage] = pData->fPageAccess;
        } while (iPage);
    }
    return 0;
}


static u_offset_t rtR0SegNemuSolGetOffset(seg_t *pSeg, caddr_t virtAddr)
{
    return ((uintptr_t)virtAddr - (uintptr_t)pSeg->s_base);
}


static int rtR0SegNemuSolGetType(seg_t *pSeg, caddr_t virtAddr)
{
    return MAP_SHARED;
}


static int rtR0SegNemuSolGetVp(seg_t *pSeg, caddr_t virtAddr, vnode_t **ppVnode)
{
    *ppVnode = &s_segNemuVnode;
    return 0;
}


static int rtR0SegNemuSolAdvise(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t Behav /* wut? */)
{
    return 0;
}


static void rtR0SegNemuSolDump(seg_t *pSeg)
{
    /* Nothing to do. */
}


static int rtR0SegNemuSolPageLock(seg_t *pSeg, caddr_t virtAddr, size_t cb, page_t ***pppPage, enum lock_type LockType, enum seg_rw ReadWrite)
{
    return ENOTSUP;
}


static int rtR0SegNemuSolSetPageSize(seg_t *pSeg, caddr_t virtAddr, size_t cb, uint_t SizeCode)
{
    return ENOTSUP;
}


static int rtR0SegNemuSolGetMemId(seg_t *pSeg, caddr_t virtAddr, memid_t *pMemId)
{
    return ENODEV;
}


static lgrp_mem_policy_info_t *rtR0SegNemuSolGetPolicy(seg_t *pSeg, caddr_t virtAddr)
{
    return NULL;
}


static int rtR0SegNemuSolCapable(seg_t *pSeg, segcapability_t Capab)
{
    return 0;
}


static struct seg_ops s_SegNemuOps =
{
    rtR0SegNemuSolDup,
    rtR0SegNemuSolUnmap,
    rtR0SegNemuSolFree,
    rtR0SegNemuSolFault,
    rtR0SegNemuSolFaultA,
    rtR0SegNemuSolSetProt,
    rtR0SegNemuSolCheckProt,
    rtR0SegNemuSolKluster,
    NULL,                       /* swapout */
    rtR0SegNemuSolSync,
    rtR0SegNemuSolInCore,
    rtR0SegNemuSolLockOp,
    rtR0SegNemuSolGetProt,
    rtR0SegNemuSolGetOffset,
    rtR0SegNemuSolGetType,
    rtR0SegNemuSolGetVp,
    rtR0SegNemuSolAdvise,
    rtR0SegNemuSolDump,
    rtR0SegNemuSolPageLock,
    rtR0SegNemuSolSetPageSize,
    rtR0SegNemuSolGetMemId,
    rtR0SegNemuSolGetPolicy,
    rtR0SegNemuSolCapable
};

#endif /* !___r0drv_solaris_memobj_r0drv_solaris_h */

