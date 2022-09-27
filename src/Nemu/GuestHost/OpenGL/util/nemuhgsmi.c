/* $Id: nemuhgsmi.c $ */

/** @file
 * Nemu HGCM connection
 */

/*
 * Copyright (C) 2008-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
//#define IN_GUEST
#ifdef IN_GUEST
#ifdef RT_OS_WINDOWS
    #include <windows.h>
    #include <ddraw.h>
#else
    #include <sys/ioctl.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <string.h>
    #include <unistd.h>
#endif

#include "cr_error.h"
#include "cr_net.h"
#include "cr_bufpool.h"
#include "cr_mem.h"
#include "cr_string.h"
#include "cr_endian.h"
#include "cr_threads.h"
#include "net_internals.h"
#include "cr_process.h"

#include <iprt/thread.h>
#include <iprt/assert.h>

#include <Nemu/NemuCrHgsmi.h>
#if 1 /** @todo Try use the Vbgl interface instead of talking directly to the driver? */
# include <Nemu/NemuGuest.h>
#else
# include <Nemu/NemuGuestLib.h>
#endif
#include <Nemu/HostServices/NemuCrOpenGLSvc.h>

#ifndef IN_GUEST
# error "Hgsmi is supported for guest lib only!!"
#endif

#ifdef DEBUG_misha
# ifdef CRASSERT
#  undef CRASSERT
# endif
# define CRASSERT Assert
#endif

#define NEMU_WITH_CRHGSMIPROFILE
#ifdef NEMU_WITH_CRHGSMIPROFILE
#include <iprt/time.h>
#include <stdio.h>

typedef struct NEMUCRHGSMIPROFILE
{
    uint64_t cStartTime;
    uint64_t cStepsTime;
    uint64_t cSteps;
} NEMUCRHGSMIPROFILE, *PNEMUCRHGSMIPROFILE;

#define NEMUCRHGSMIPROFILE_GET_TIME_NANO() RTTimeNanoTS()
#define NEMUCRHGSMIPROFILE_GET_TIME_MILLI() RTTimeMilliTS()

/* 10 sec */
#define NEMUCRHGSMIPROFILE_LOG_STEP_TIME (10000000000.)

DECLINLINE(void) nemuCrHgsmiProfileStart(PNEMUCRHGSMIPROFILE pProfile)
{
    pProfile->cStepsTime = 0;
    pProfile->cSteps = 0;
    pProfile->cStartTime = NEMUCRHGSMIPROFILE_GET_TIME_NANO();
}

DECLINLINE(void) nemuCrHgsmiProfileStep(PNEMUCRHGSMIPROFILE pProfile, uint64_t cStepTime)
{
    pProfile->cStepsTime += cStepTime;
    ++pProfile->cSteps;
}

typedef struct NEMUCRHGSMIPROFILE_SCOPE
{
    uint64_t cStartTime;
//    bool bDisable;
} NEMUCRHGSMIPROFILE_SCOPE, *PNEMUCRHGSMIPROFILE_SCOPE;

static NEMUCRHGSMIPROFILE g_NemuProfile;

static void nemuCrHgsmiLog(char * szString, ...)
{
    char szBuffer[4096] = {0};
     va_list pArgList;
     va_start(pArgList, szString);
     _vsnprintf(szBuffer, sizeof(szBuffer) / sizeof(szBuffer[0]), szString, pArgList);
     va_end(pArgList);

     NemuCrHgsmiLog(szBuffer);
}

DECLINLINE(void) nemuCrHgsmiProfileLog(PNEMUCRHGSMIPROFILE pProfile, uint64_t cTime)
{
    uint64_t profileTime = cTime - pProfile->cStartTime;
    double percent = ((double)100.0) * pProfile->cStepsTime / profileTime;
    double cps = ((double)1000000000.) * pProfile->cSteps / profileTime;
    nemuCrHgsmiLog("hgsmi: cps: %.1f, host %.1f%%\n", cps, percent);
}

DECLINLINE(void) nemuCrHgsmiProfileScopeEnter(PNEMUCRHGSMIPROFILE_SCOPE pScope)
{
//    pScope->bDisable = false;
    pScope->cStartTime = NEMUCRHGSMIPROFILE_GET_TIME_NANO();
}

DECLINLINE(void) nemuCrHgsmiProfileScopeExit(PNEMUCRHGSMIPROFILE_SCOPE pScope)
{
//    if (!pScope->bDisable)
    {
        uint64_t cTime = NEMUCRHGSMIPROFILE_GET_TIME_NANO();
        nemuCrHgsmiProfileStep(&g_NemuProfile, cTime - pScope->cStartTime);
        if (NEMUCRHGSMIPROFILE_LOG_STEP_TIME < cTime - g_NemuProfile.cStartTime)
        {
            nemuCrHgsmiProfileLog(&g_NemuProfile, cTime);
            nemuCrHgsmiProfileStart(&g_NemuProfile);
        }
    }
}


#define NEMUCRHGSMIPROFILE_INIT() nemuCrHgsmiProfileStart(&g_NemuProfile)
#define NEMUCRHGSMIPROFILE_TERM() do {} while (0)

#define NEMUCRHGSMIPROFILE_FUNC_PROLOGUE() \
        NEMUCRHGSMIPROFILE_SCOPE __nemuCrHgsmiProfileScope; \
        nemuCrHgsmiProfileScopeEnter(&__nemuCrHgsmiProfileScope);

#define NEMUCRHGSMIPROFILE_FUNC_EPILOGUE() \
        nemuCrHgsmiProfileScopeExit(&__nemuCrHgsmiProfileScope); \


#else
#define NEMUCRHGSMIPROFILE_INIT() do {} while (0)
#define NEMUCRHGSMIPROFILE_TERM() do {} while (0)
#define NEMUCRHGSMIPROFILE_FUNC_PROLOGUE() do {} while (0)
#define NEMUCRHGSMIPROFILE_FUNC_EPILOGUE() do {} while (0)
#endif


typedef struct {
    int                  initialized;
    int                  num_conns;
    CRConnection         **conns;
#ifdef CHROMIUM_THREADSAFE
    CRmutex              mutex;
    CRmutex              recvmutex;
#endif
    CRNetReceiveFuncList *recv_list;
    CRNetCloseFuncList   *close_list;
    CRBufferPool *mempool;
#ifdef RT_OS_WINDOWS
    HANDLE               hGuestDrv;
#else
    int                  iGuestDrv;
#endif
} CRNEMUHGSMIDATA;

#define CR_NEMUHGSMI_BUFFER_MAGIC 0xEDCBA123

typedef struct CRNEMUHGSMIBUFFER {
    uint32_t             magic;
    union
    {
        struct
        {
            uint32_t             cbLock : 31;
            uint32_t             bIsBuf : 1;
        };
        uint32_t Value;
    };
    union
    {
        PNEMUUHGSMI_BUFFER pBuffer;
        uint32_t u32Len; /* <- sys mem buf specific */
        uint64_t u64Align;
    };
} CRNEMUHGSMIBUFFER;

typedef struct CRNEMUHGSMI_CLIENT {
    PNEMUUHGSMI pHgsmi;
    PNEMUUHGSMI_BUFFER pCmdBuffer;
    PNEMUUHGSMI_BUFFER pHGBuffer;
    void *pvHGBuffer;
    CRBufferPool *bufpool;
} CRNEMUHGSMI_CLIENT, *PCRNEMUHGSMI_CLIENT;


static CRNEMUHGSMIDATA g_crnemuhgsmi = {0,};

DECLINLINE(PCRNEMUHGSMI_CLIENT) _crNemuHGSMIClientGet()
{
    PCRNEMUHGSMI_CLIENT pClient = (PCRNEMUHGSMI_CLIENT)NemuCrHgsmiQueryClient();
    Assert(pClient);
    return pClient;
}

#ifndef RT_OS_WINDOWS
    #define TRUE true
    #define FALSE false
    #define INVALID_HANDLE_VALUE (-1)
#endif

/* Some forward declarations */
static void _crNemuHGSMIReceiveMessage(CRConnection *conn, PCRNEMUHGSMI_CLIENT pClient);

/* we still use hgcm for establishing the connection
 * @todo: join the common code of HGSMI & HGCM */
/**
 * Send an HGCM request
 *
 * @return Nemu status code
 * @param   pvData      Data pointer
 * @param   cbData      Data size
 */
/** @todo use vbglR3DoIOCtl here instead */
static int crNemuHGCMCall(void *pvData, unsigned cbData)
{
#ifdef IN_GUEST

# ifdef RT_OS_WINDOWS
    DWORD cbReturned;

    if (DeviceIoControl (g_crnemuhgsmi.hGuestDrv,
                         NEMUGUEST_IOCTL_HGCM_CALL(cbData),
                         pvData, cbData,
                         pvData, cbData,
                         &cbReturned,
                         NULL))
    {
        return VINF_SUCCESS;
    }
    Assert(0);
    crDebug("nemuCall failed with %x\n", GetLastError());
    return VERR_NOT_SUPPORTED;
# else
    int rc;
#  if defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
    VBGLBIGREQ Hdr;
    Hdr.u32Magic = VBGLBIGREQ_MAGIC;
    Hdr.cbData = cbData;
    Hdr.pvDataR3 = pvData;
#   if HC_ARCH_BITS == 32
    Hdr.u32Padding = 0;
#   endif
    rc = ioctl(g_crnemuhgsmi.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CALL(cbData), &Hdr);
#  else
    rc = ioctl(g_crnemuhgsmi.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CALL(cbData), pvData);
#  endif
#  ifdef RT_OS_LINUX
    if (rc == 0)
#  else
    if (rc >= 0)
#  endif
    {
        return VINF_SUCCESS;
    }
#  ifdef RT_OS_LINUX
    if (rc >= 0) /* positive values are negated Nemu error status codes. */
    {
        crWarning("nemuCall failed with Nemu status code %d\n", -rc);
        if (rc==VINF_INTERRUPTED)
        {
            RTMSINTERVAL sl;
            int i;

            for (i=0, sl=50; i<6; i++, sl=sl*2)
            {
                RTThreadSleep(sl);
                rc = ioctl(g_crnemuhgsmi.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CALL(cbData), pvData);
                if (rc==0)
                {
                    crWarning("nemuCall retry(%i) succeeded", i+1);
                    return VINF_SUCCESS;
                }
                else if (rc==VINF_INTERRUPTED)
                {
                    continue;
                }
                else
                {
                    crWarning("nemuCall retry(%i) failed with Nemu status code %d", i+1, -rc);
                    break;
                }
            }
        }
    }
    else
#  endif
        crWarning("nemuCall failed with %x\n", errno);
    return VERR_NOT_SUPPORTED;
# endif /*#ifdef RT_OS_WINDOWS*/

#else /*#ifdef IN_GUEST*/
    crError("crNemuHGCMCall called on host side!");
    CRASSERT(FALSE);
    return VERR_NOT_SUPPORTED;
#endif
}


/* add sizeof header + page align */
#define CRNEMUHGSMI_PAGE_ALIGN(_s) (((_s)  + 0xfff) & ~0xfff)
#define CRNEMUHGSMI_BUF_HDR_SIZE() (sizeof (CRNEMUHGSMIBUFFER))
#define CRNEMUHGSMI_BUF_SIZE(_s) CRNEMUHGSMI_PAGE_ALIGN((_s) + CRNEMUHGSMI_BUF_HDR_SIZE())
#define CRNEMUHGSMI_BUF_LOCK_SIZE(_s) ((_s) + CRNEMUHGSMI_BUF_HDR_SIZE())
#define CRNEMUHGSMI_BUF_DATA(_p) ((void*)(((CRNEMUHGSMIBUFFER*)(_p)) + 1))
#define CRNEMUHGSMI_BUF_HDR(_p) (((CRNEMUHGSMIBUFFER*)(_p)) - 1)
#define CRNEMUHGSMI_BUF_OFFSET(_st2, _st1) ((uint32_t)(((uint8_t*)(_st2)) - ((uint8_t*)(_st1))))

static CRNEMUHGSMIHDR *_crNemuHGSMICmdBufferLock(PCRNEMUHGSMI_CLIENT pClient, uint32_t cbBuffer)
{
    /* in theory it is OK to use one cmd buffer for async cmd submission
     * because bDiscard flag should result in allocating a new memory backend if the
     * allocation is still in use.
     * However, NOTE: since one and the same semaphore sync event is used for completion notification,
     * for the notification mechanism working as expected
     * 1. host must complete commands in the same order as it receives them
     * (to avoid situation when guest receives notification for another command completion)
     * 2. guest must eventually wait for command completion unless he specified bDoNotSignalCompletion
     * 3. guest must wait for command completion in the same order as it submits them
     * in case we can not satisfy any of the above, we should introduce multiple command buffers */
    CRNEMUHGSMIHDR * pHdr;
    NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
    int rc;
    fFlags.Value = 0;
    fFlags.bDiscard = 1;
    rc = pClient->pCmdBuffer->pfnLock(pClient->pCmdBuffer, 0, cbBuffer, fFlags, (void**)&pHdr);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
        return pHdr;

    crWarning("Failed to Lock the command buffer of size(%d), rc(%d)\n", cbBuffer, rc);
    return NULL;
}

static CRNEMUHGSMIHDR *_crNemuHGSMICmdBufferLockRo(PCRNEMUHGSMI_CLIENT pClient, uint32_t cbBuffer)
{
    /* in theory it is OK to use one cmd buffer for async cmd submission
     * because bDiscard flag should result in allocating a new memory backend if the
     * allocation is still in use.
     * However, NOTE: since one and the same semaphore sync event is used for completion notification,
     * for the notification mechanism working as expected
     * 1. host must complete commands in the same order as it receives them
     * (to avoid situation when guest receives notification for another command completion)
     * 2. guest must eventually wait for command completion unless he specified bDoNotSignalCompletion
     * 3. guest must wait for command completion in the same order as it submits them
     * in case we can not satisfy any of the above, we should introduce multiple command buffers */
    CRNEMUHGSMIHDR * pHdr;
    NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
    int rc;
    fFlags.Value = 0;
    fFlags.bReadOnly = 1;
    rc = pClient->pCmdBuffer->pfnLock(pClient->pCmdBuffer, 0, cbBuffer, fFlags, (void**)&pHdr);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        crWarning("Failed to Lock the command buffer of size(%d), rc(%d)\n", cbBuffer, rc);
    return pHdr;
}

static void _crNemuHGSMICmdBufferUnlock(PCRNEMUHGSMI_CLIENT pClient)
{
    int rc = pClient->pCmdBuffer->pfnUnlock(pClient->pCmdBuffer);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        crWarning("Failed to Unlock the command buffer rc(%d)\n", rc);
}

static int32_t _crNemuHGSMICmdBufferGetRc(PCRNEMUHGSMI_CLIENT pClient)
{
    CRNEMUHGSMIHDR * pHdr;
    NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
    int rc;

    fFlags.Value = 0;
    fFlags.bReadOnly = 1;
    rc = pClient->pCmdBuffer->pfnLock(pClient->pCmdBuffer, 0, sizeof (*pHdr), fFlags, (void**)&pHdr);
    AssertRC(rc);
    if (RT_FAILURE(rc))
    {
        crWarning("Failed to Lock the command buffer of size(%d), rc(%d)\n", sizeof (*pHdr), rc);
        return rc;
    }

    rc = pHdr->result;
    AssertRC(rc);
    pClient->pCmdBuffer->pfnUnlock(pClient->pCmdBuffer);

    return rc;
}

DECLINLINE(PNEMUUHGSMI_BUFFER) _crNemuHGSMIRecvBufGet(PCRNEMUHGSMI_CLIENT pClient)
{
    if (pClient->pvHGBuffer)
    {
        int rc = pClient->pHGBuffer->pfnUnlock(pClient->pHGBuffer);
        if (RT_FAILURE(rc))
        {
            return NULL;
        }
        pClient->pvHGBuffer = NULL;
    }
    return pClient->pHGBuffer;
}

DECLINLINE(void*) _crNemuHGSMIRecvBufData(PCRNEMUHGSMI_CLIENT pClient, uint32_t cbBuffer)
{
    NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
    int rc;
    Assert(!pClient->pvHGBuffer);
    fFlags.Value = 0;
    rc = pClient->pHGBuffer->pfnLock(pClient->pHGBuffer, 0, cbBuffer, fFlags, &pClient->pvHGBuffer);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        return pClient->pvHGBuffer;
    }
    return NULL;
}


static PNEMUUHGSMI_BUFFER _crNemuHGSMIBufFromMemPtr(void *pvBuf)
{
    CRNEMUHGSMIBUFFER *pHdr = CRNEMUHGSMI_BUF_HDR(pvBuf);
    PNEMUUHGSMI_BUFFER pBuf;
    int rc;

    CRASSERT(pHdr->magic == CR_NEMUHGSMI_BUFFER_MAGIC);
    pBuf = pHdr->pBuffer;
    rc = pBuf->pfnUnlock(pBuf);
    AssertRC(rc);
    if (RT_FAILURE(rc))
    {
        return NULL;
    }
    return pBuf;
}

static PNEMUUHGSMI_BUFFER _crNemuHGSMIBufAlloc(PCRNEMUHGSMI_CLIENT pClient, uint32_t cbSize)
{
    PNEMUUHGSMI_BUFFER buf;
    int rc;

    buf = (PNEMUUHGSMI_BUFFER ) crBufferPoolPop(pClient->bufpool, cbSize);

    if (!buf)
    {
        crDebug("Buffer pool %p was empty; allocating new %d byte buffer.",
                        (void *) pClient->bufpool,
                        cbSize);
        rc = pClient->pHgsmi->pfnBufferCreate(pClient->pHgsmi, cbSize,
                                NEMUUHGSMI_SYNCHOBJECT_TYPE_NONE, NULL,
                                &buf);
        AssertRC(rc);
        if (RT_FAILURE(rc))
            crWarning("Failed to Create a buffer of size(%d), rc(%d)\n", cbSize, rc);
    }
    return buf;
}

static void _crNemuHGSMIBufFree(PCRNEMUHGSMI_CLIENT pClient, PNEMUUHGSMI_BUFFER pBuf)
{
    crBufferPoolPush(pClient->bufpool, pBuf, pBuf->cbBuffer);
}

static CRNEMUHGSMIBUFFER* _crNemuHGSMISysMemAlloc(uint32_t cbSize)
{
    CRNEMUHGSMIBUFFER *pBuf;
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgsmi.mutex);
#endif
    pBuf = crBufferPoolPop(g_crnemuhgsmi.mempool, cbSize);
    if (!pBuf)
    {
        pBuf = (CRNEMUHGSMIBUFFER*)crAlloc(CRNEMUHGSMI_BUF_HDR_SIZE() + cbSize);
        Assert(pBuf);
        if (pBuf)
        {
            pBuf->magic = CR_NEMUHGSMI_BUFFER_MAGIC;
            pBuf->cbLock = cbSize;
            pBuf->bIsBuf = false;
            pBuf->u32Len = 0;
        }
    }
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgsmi.mutex);
#endif
    return pBuf;
}

static void *_crNemuHGSMIDoAlloc(PCRNEMUHGSMI_CLIENT pClient, uint32_t cbSize)
{
    PNEMUUHGSMI_BUFFER buf;
    CRNEMUHGSMIBUFFER *pData = NULL;
    int rc;

    buf = _crNemuHGSMIBufAlloc(pClient, CRNEMUHGSMI_BUF_SIZE(cbSize));
    Assert(buf);
    if (buf)
    {
        NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
        buf->pvUserData = pClient;
        fFlags.Value = 0;
        fFlags.bDiscard = 1;
        rc = buf->pfnLock(buf, 0, CRNEMUHGSMI_BUF_LOCK_SIZE(cbSize), fFlags, (void**)&pData);
        if (RT_SUCCESS(rc))
        {
            pData->magic = CR_NEMUHGSMI_BUFFER_MAGIC;
            pData->cbLock = CRNEMUHGSMI_BUF_LOCK_SIZE(cbSize);
            pData->bIsBuf = true;
            pData->pBuffer = buf;
        }
        else
        {
            crWarning("Failed to Lock the buffer, rc(%d)\n", rc);
        }
    }

    return CRNEMUHGSMI_BUF_DATA(pData);

}
static void *crNemuHGSMIAlloc(CRConnection *conn)
{
    PCRNEMUHGSMI_CLIENT pClient;
    void *pvBuf;

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    pClient = _crNemuHGSMIClientGet();
    pvBuf = _crNemuHGSMIDoAlloc(pClient, conn->buffer_size);
    Assert(pvBuf);

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();

    return pvBuf;
}

DECLINLINE(void) _crNemuHGSMIFillCmd(NEMUUHGSMI_BUFFER_SUBMIT *pSubm, PCRNEMUHGSMI_CLIENT pClient, uint32_t cbData)
{
    pSubm->pBuf = pClient->pCmdBuffer;
    pSubm->offData = 0;
    pSubm->cbData = cbData;
    pSubm->fFlags.Value = 0;
    pSubm->fFlags.bDoNotRetire = 1;
//    pSubm->fFlags.bDoNotSignalCompletion = 1; /* <- we do not need that actually since
//                                                       * in case we want completion,
//                                                       * we will block in _crNemuHGSMICmdBufferGetRc (when locking the buffer)
//                                                       * which is needed for getting the result */
}

#ifdef RT_OS_WINDOWS
#define CRNEMUHGSMI_BUF_WAIT(_pBub) WaitForSingleObject((_pBub)->hSynch, INFINITE);
#else
# error "Port Me!!"
#endif

DECLINLINE(void) _crNemuHGSMIWaitCmd(PCRNEMUHGSMI_CLIENT pClient)
{
    int rc = CRNEMUHGSMI_BUF_WAIT(pClient->pCmdBuffer);
    Assert(rc == 0);
}

static void _crNemuHGSMIWriteExact(CRConnection *conn, PCRNEMUHGSMI_CLIENT pClient, PNEMUUHGSMI_BUFFER pBuf, uint32_t offStart, unsigned int len)
{
    int rc;
    int32_t callRes;
    NEMUUHGSMI_BUFFER_SUBMIT aSubmit[2];

#ifdef IN_GUEST
    if (conn->u32InjectClientID)
    {
        CRNEMUHGSMIINJECT *parms = (CRNEMUHGSMIINJECT *)_crNemuHGSMICmdBufferLock(pClient, sizeof (*parms));
        Assert(parms);
        if (!parms)
        {
            return;
        }

        parms->hdr.result      = VERR_WRONG_ORDER;
        parms->hdr.u32ClientID = conn->u32ClientID;
        parms->hdr.u32Function = SHCRGL_GUEST_FN_INJECT;
//        parms->hdr.u32Reserved = 0;

        parms->u32ClientID = conn->u32InjectClientID;

        parms->iBuffer = 1;
        _crNemuHGSMICmdBufferUnlock(pClient);

        _crNemuHGSMIFillCmd(&aSubmit[0], pClient, sizeof (*parms));

        aSubmit[1].pBuf = pBuf;
        aSubmit[1].offData = offStart;
        aSubmit[1].cbData = len;
        aSubmit[1].fFlags.Value = 0;
        aSubmit[1].fFlags.bHostReadOnly = 1;

        rc = pClient->pHgsmi->pfnBufferSubmitAsynch(pClient->pHgsmi, aSubmit, 2);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            _crNemuHGSMIWaitCmd(pClient);
                /* @todo: do we need to wait for completion actually?
                 * NOTE: in case we do not need completion,
                 * we MUST specify bDoNotSignalCompletion flag for the command buffer */
//                CRNEMUHGSMI_BUF_WAIT(pClient->pCmdBuffer);

            callRes = _crNemuHGSMICmdBufferGetRc(pClient);
        }
    }
    else
#endif
    {
        CRNEMUHGSMIWRITE * parms = (CRNEMUHGSMIWRITE *)_crNemuHGSMICmdBufferLock(pClient, sizeof (*parms));;

        parms->hdr.result      = VERR_WRONG_ORDER;
        parms->hdr.u32ClientID = conn->u32ClientID;
        parms->hdr.u32Function = SHCRGL_GUEST_FN_WRITE;
//        parms->hdr.u32Reserved = 0;

        parms->iBuffer = 1;
        _crNemuHGSMICmdBufferUnlock(pClient);

        _crNemuHGSMIFillCmd(&aSubmit[0], pClient, sizeof (*parms));

        aSubmit[1].pBuf = pBuf;
        aSubmit[1].offData = offStart;
        aSubmit[1].cbData = len;
        aSubmit[1].fFlags.Value = 0;
        aSubmit[1].fFlags.bHostReadOnly = 1;

        rc = pClient->pHgsmi->pfnBufferSubmitAsynch(pClient->pHgsmi, aSubmit, 2);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            _crNemuHGSMIWaitCmd(pClient);
                /* @todo: do we need to wait for completion actually?
                 * NOTE: in case we do not need completion,
                 * we MUST specify bDoNotSignalCompletion flag for the command buffer */
//                CRNEMUHGSMI_BUF_WAIT(pClient->pCmdBuffer);

            callRes = _crNemuHGSMICmdBufferGetRc(pClient);
        }
    }

    if (RT_FAILURE(rc) || RT_FAILURE(callRes))
    {
        crWarning("SHCRGL_GUEST_FN_WRITE failed with %x %x\n", rc, callRes);
    }
}

static void crNemuHGSMIWriteExact(CRConnection *conn, const void *buf, unsigned int len)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    Assert(0);

    CRASSERT(0);
//    PCRNEMUHGSMI_CLIENT pClient;
//    PNEMUUHGSMI_BUFFER pBuf = _crNemuHGSMIBufFromMemPtr(buf);
//    if (!pBuf)
//        return;
//    pClient = (PCRNEMUHGSMI_CLIENT)pBuf->pvUserData;
//    _crNemuHGSMIWriteExact(conn, pClient, pBuf, 0, len);
//    _crNemuHGSMIBufFree(pClient, pBuf);
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void _crNemuHGSMIPollHost(CRConnection *conn, PCRNEMUHGSMI_CLIENT pClient)
{
    CRNEMUHGSMIREAD *parms = (CRNEMUHGSMIREAD *)_crNemuHGSMICmdBufferLock(pClient, sizeof (*parms));
    int rc;
    NEMUUHGSMI_BUFFER_SUBMIT aSubmit[2];
    PNEMUUHGSMI_BUFFER pRecvBuffer;
    uint32_t cbBuffer;

    Assert(parms);

    parms->hdr.result      = VERR_WRONG_ORDER;
    parms->hdr.u32ClientID = conn->u32ClientID;
    parms->hdr.u32Function = SHCRGL_GUEST_FN_READ;
//    parms->hdr.u32Reserved = 0;

    CRASSERT(!conn->pBuffer); //make sure there's no data to process
    parms->iBuffer = 1;
    parms->cbBuffer = 0;

    _crNemuHGSMICmdBufferUnlock(pClient);

    pRecvBuffer = _crNemuHGSMIRecvBufGet(pClient);
    Assert(pRecvBuffer);
    if (!pRecvBuffer)
        return;

    _crNemuHGSMIFillCmd(&aSubmit[0], pClient, sizeof (*parms));

    aSubmit[1].pBuf = pRecvBuffer;
    aSubmit[1].offData = 0;
    aSubmit[1].cbData = pRecvBuffer->cbBuffer;
    aSubmit[1].fFlags.Value = 0;
    aSubmit[1].fFlags.bHostWriteOnly = 1;

    rc = pClient->pHgsmi->pfnBufferSubmitAsynch(pClient->pHgsmi, aSubmit, 2);
    AssertRC(rc);
    if (RT_FAILURE(rc))
    {
        crWarning("pfnBufferSubmitAsynch failed with %d \n", rc);
        return;
    }

    _crNemuHGSMIWaitCmd(pClient);

    parms = (CRNEMUHGSMIREAD *)_crNemuHGSMICmdBufferLockRo(pClient, sizeof (*parms));
    Assert(parms);
    if (!parms)
    {
        crWarning("_crNemuHGSMICmdBufferLockRo failed\n");
        return;
    }

    if (RT_SUCCESS(parms->hdr.result))
        cbBuffer = parms->cbBuffer;
    else
        cbBuffer = 0;

    _crNemuHGSMICmdBufferUnlock(pClient);

    if (cbBuffer)
    {
        void *pvData = _crNemuHGSMIRecvBufData(pClient, cbBuffer);
        Assert(pvData);
        if (pvData)
        {
            conn->pBuffer  = pvData;
            conn->cbBuffer = cbBuffer;
        }
    }
}

static void _crNemuHGSMIReadExact(CRConnection *conn, PCRNEMUHGSMI_CLIENT pClient)
{
    _crNemuHGSMIPollHost(conn, pClient);

    if (conn->cbBuffer)
        _crNemuHGSMIReceiveMessage(conn, pClient);
}

/* Same as crNemuHGCMWriteExact, but combined with read of writeback data.
 * This halves the number of HGCM calls we do,
 * most likely crNemuHGCMPollHost shouldn't be called at all now.
 */
static void
_crNemuHGSMIWriteReadExact(CRConnection *conn, PCRNEMUHGSMI_CLIENT pClient, void *buf, uint32_t offBuffer, unsigned int len, bool bIsBuffer)
{
    CRNEMUHGSMIWRITEREAD *parms = (CRNEMUHGSMIWRITEREAD*)_crNemuHGSMICmdBufferLock(pClient, sizeof (*parms));
    int rc;
    NEMUUHGSMI_BUFFER_SUBMIT aSubmit[3];
    PNEMUUHGSMI_BUFFER pBuf = NULL;
    NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
//    uint32_t cbBuffer;

    parms->hdr.result      = VERR_WRONG_ORDER;
    parms->hdr.u32ClientID = conn->u32ClientID;
    parms->hdr.u32Function = SHCRGL_GUEST_FN_WRITE_READ;
//    parms->hdr.u32Reserved = 0;

    parms->iBuffer = 1;

    CRASSERT(!conn->pBuffer); //make sure there's no data to process
    parms->iWriteback = 2;
    parms->cbWriteback = 0;

    _crNemuHGSMICmdBufferUnlock(pClient);

    if (!bIsBuffer)
    {
        void *pvBuf;
        pBuf = _crNemuHGSMIBufAlloc(pClient, len);
        Assert(pBuf);
        if (!pBuf)
            return;

        Assert(!offBuffer);

        offBuffer = 0;
        fFlags.Value = 0;
        fFlags.bDiscard = 1;
        fFlags.bWriteOnly = 1;
        rc = pBuf->pfnLock(pBuf, 0, len, fFlags, &pvBuf);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            memcpy(pvBuf, buf, len);
            rc = pBuf->pfnUnlock(pBuf);
            AssertRC(rc);
            CRASSERT(RT_SUCCESS(rc));
        }
        else
        {
            _crNemuHGSMIBufFree(pClient, pBuf);
            return;
        }
    }
    else
    {
        pBuf = (PNEMUUHGSMI_BUFFER)buf;
    }

    do
    {
        PNEMUUHGSMI_BUFFER pRecvBuffer = _crNemuHGSMIRecvBufGet(pClient);
        Assert(pRecvBuffer);
        if (!pRecvBuffer)
            return;

        _crNemuHGSMIFillCmd(&aSubmit[0], pClient, sizeof (*parms));

        aSubmit[1].pBuf = pBuf;
        aSubmit[1].offData = offBuffer;
        aSubmit[1].cbData = len;
        aSubmit[1].fFlags.Value = 0;
        aSubmit[1].fFlags.bHostReadOnly = 1;

        aSubmit[2].pBuf = pRecvBuffer;
        aSubmit[2].offData = 0;
        aSubmit[2].cbData = pRecvBuffer->cbBuffer;
        aSubmit[2].fFlags.Value = 0;

        rc = pClient->pHgsmi->pfnBufferSubmitAsynch(pClient->pHgsmi, aSubmit, 3);
        AssertRC(rc);
        if (RT_FAILURE(rc))
        {
            crWarning("pfnBufferSubmitAsynch failed with %d \n", rc);
            break;
        }

        _crNemuHGSMIWaitCmd(pClient);

        parms = (CRNEMUHGSMIWRITEREAD *)_crNemuHGSMICmdBufferLockRo(pClient, sizeof (*parms));
        Assert(parms);
        if (parms)
        {
            uint32_t cbWriteback = parms->cbWriteback;
            rc = parms->hdr.result;
            _crNemuHGSMICmdBufferUnlock(pClient);
#ifdef DEBUG
            parms = NULL;
#endif
            if (RT_SUCCESS(rc))
            {
                if (cbWriteback)
                {
                    void *pvData = _crNemuHGSMIRecvBufData(pClient, cbWriteback);
                    Assert(pvData);
                    if (pvData)
                    {
                        conn->pBuffer  = pvData;
                        conn->cbBuffer = cbWriteback;
                        _crNemuHGSMIReceiveMessage(conn, pClient);
                    }
                }
            }
            else if (VERR_BUFFER_OVERFLOW == rc)
            {
                PNEMUUHGSMI_BUFFER pOldBuf = pClient->pHGBuffer;
                Assert(!pClient->pvHGBuffer);
                CRASSERT(cbWriteback>pClient->pHGBuffer->cbBuffer);
                crDebug("Reallocating host buffer from %d to %d bytes", conn->cbHostBufferAllocated, cbWriteback);

                rc = pClient->pHgsmi->pfnBufferCreate(pClient->pHgsmi, CRNEMUHGSMI_PAGE_ALIGN(cbWriteback),
                                NEMUUHGSMI_SYNCHOBJECT_TYPE_NONE, NULL, &pClient->pHGBuffer);
                AssertRC(rc);
                CRASSERT(RT_SUCCESS(rc));
                if (RT_SUCCESS(rc))
                {
                    rc = pOldBuf->pfnDestroy(pOldBuf);
                    CRASSERT(RT_SUCCESS(rc));

                    _crNemuHGSMIReadExact(conn, pClient/*, cbWriteback*/);
                }
            }
            else
            {
                crWarning("SHCRGL_GUEST_FN_WRITE_READ (%i) failed with %x \n", len, rc);
            }
        }
        else
        {
            crWarning("_crNemuHGSMICmdBufferLockRo failed\n");
            break;
        }
    } while (0);

    if (!bIsBuffer)
        _crNemuHGSMIBufFree(pClient, pBuf);
}

static void crNemuHGSMISend(CRConnection *conn, void **bufp,
                           const void *start, unsigned int len)
{
    PCRNEMUHGSMI_CLIENT pClient;
    PNEMUUHGSMI_BUFFER pBuf;

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    if (!bufp) /* We're sending a user-allocated buffer. */
    {
        pClient = _crNemuHGSMIClientGet();
#ifndef IN_GUEST
            //@todo remove temp buffer allocation in unpacker
            /* we're at the host side, so just store data until guest polls us */
            _crNemuHGCMWriteBytes(conn, start, len);
#else
        CRASSERT(!conn->u32InjectClientID);
        crDebug("SHCRGL: sending userbuf with %d bytes\n", len);
        _crNemuHGSMIWriteReadExact(conn, pClient, (void*)start, 0, len, false);
#endif
        NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return;
    }

    /* The region [start .. start + len + 1] lies within a buffer that
     * was allocated with crNemuHGCMAlloc() and can be put into the free
     * buffer pool when we're done sending it.
     */

    pBuf = _crNemuHGSMIBufFromMemPtr(*bufp);
    Assert(pBuf);
    if (!pBuf)
    {
        NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return;
    }

    pClient = (PCRNEMUHGSMI_CLIENT)pBuf->pvUserData;

    /* Length would be passed as part of HGCM pointer description
     * No need to prepend it to the buffer
     */
#ifdef IN_GUEST
    if (conn->u32InjectClientID)
    {
        _crNemuHGSMIWriteExact(conn, pClient, pBuf, CRNEMUHGSMI_BUF_OFFSET(start, *bufp) + CRNEMUHGSMI_BUF_HDR_SIZE(), len);
    }
    else
#endif
    {
        _crNemuHGSMIWriteReadExact(conn, pClient, pBuf, CRNEMUHGSMI_BUF_OFFSET(start, *bufp) + CRNEMUHGSMI_BUF_HDR_SIZE(), len, true);
    }

    /* Reclaim this pointer for reuse */
    _crNemuHGSMIBufFree(pClient, pBuf);
    /* Since the buffer's now in the 'free' buffer pool, the caller can't
     * use it any more.  Setting bufp to NULL will make sure the caller
     * doesn't try to re-use the buffer.
     */
    *bufp = NULL;

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGSMISingleRecv(CRConnection *conn, void *buf, unsigned int len)
{
    PCRNEMUHGSMI_CLIENT pClient;
    PNEMUUHGSMI_BUFFER pBuf;
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    pBuf = _crNemuHGSMIBufFromMemPtr(buf);
    Assert(pBuf);
    Assert(0);
    CRASSERT(0);
    if (!pBuf)
    {
        NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return;
    }
    pClient = (PCRNEMUHGSMI_CLIENT)pBuf->pvUserData;
    _crNemuHGSMIReadExact(conn, pClient);
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void _crNemuHGSMIFree(CRConnection *conn, void *buf)
{
    CRNEMUHGSMIBUFFER *hgsmi_buffer = (CRNEMUHGSMIBUFFER *) buf - 1;

    CRASSERT(hgsmi_buffer->magic == CR_NEMUHGSMI_BUFFER_MAGIC);

    if (hgsmi_buffer->bIsBuf)
    {
        PNEMUUHGSMI_BUFFER pBuf = hgsmi_buffer->pBuffer;
        PCRNEMUHGSMI_CLIENT pClient = (PCRNEMUHGSMI_CLIENT)pBuf->pvUserData;
        pBuf->pfnUnlock(pBuf);
        _crNemuHGSMIBufFree(pClient, pBuf);
    }
    else
    {
        /*@todo wrong len for redir buffers*/
        conn->recv_credits += hgsmi_buffer->u32Len;

#ifdef CHROMIUM_THREADSAFE
            crLockMutex(&g_crnemuhgsmi.mutex);
#endif
            crBufferPoolPush(g_crnemuhgsmi.mempool, hgsmi_buffer, hgsmi_buffer->cbLock);
#ifdef CHROMIUM_THREADSAFE
            crUnlockMutex(&g_crnemuhgsmi.mutex);
#endif
    }
}
static void crNemuHGSMIFree(CRConnection *conn, void *buf)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    _crNemuHGSMIFree(conn, buf);
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void _crNemuHGSMIReceiveMessage(CRConnection *conn, PCRNEMUHGSMI_CLIENT pClient)
{
    uint32_t len;
    CRNEMUHGSMIBUFFER* pSysBuf;
    CRMessage *msg;
    CRMessageType cached_type;

    len = conn->cbBuffer;
    Assert(len > 0);
    Assert(conn->pBuffer);
    CRASSERT(len > 0);
    CRASSERT(conn->pBuffer);

#ifndef IN_GUEST
    if (conn->allow_redir_ptr)
    {
#endif //IN_GUEST
        CRASSERT(conn->buffer_size >= sizeof(CRMessageRedirPtr));

        pSysBuf = _crNemuHGSMISysMemAlloc( conn->buffer_size );
        pSysBuf->u32Len = sizeof(CRMessageRedirPtr);

        msg = (CRMessage *)CRNEMUHGSMI_BUF_DATA(pSysBuf);

        msg->header.type = CR_MESSAGE_REDIR_PTR;
        msg->redirptr.pMessage = (CRMessageHeader*) (conn->pBuffer);
        msg->header.conn_id = msg->redirptr.pMessage->conn_id;

        cached_type = msg->redirptr.pMessage->type;

        conn->cbBuffer = 0;
        conn->pBuffer  = NULL;
#ifndef IN_GUEST
    }
    else
    {
        if ( len <= conn->buffer_size )
        {
            /* put in pre-allocated buffer */
            hgcm_buffer = (CRNEMUHGCMBUFFER *) crNemuHGCMAlloc( conn ) - 1;
        }
        else
        {
            /* allocate new buffer,
             * not using pool here as it's most likely one time transfer of huge texture
             */
            hgcm_buffer            = (CRNEMUHGCMBUFFER *) crAlloc( sizeof(CRNEMUHGCMBUFFER) + len );
            hgcm_buffer->magic     = CR_NEMUHGCM_BUFFER_MAGIC;
            hgcm_buffer->kind      = CR_NEMUHGCM_MEMORY_BIG;
            hgcm_buffer->allocated = sizeof(CRNEMUHGCMBUFFER) + len;
# ifdef RT_OS_WINDOWS
            hgcm_buffer->pDDS      = NULL;
# endif
        }

        hgcm_buffer->len = len;

        _crNemuHGCMReadBytes(conn, hgcm_buffer + 1, len);

        msg = (CRMessage *) (hgcm_buffer + 1);
        cached_type = msg->header.type;
    }
#endif //IN_GUEST

    conn->recv_credits     -= len;
    conn->total_bytes_recv += len;
    conn->recv_count++;

    crNetDispatchMessage( g_crnemuhgsmi.recv_list, conn, msg, len );

    /* CR_MESSAGE_OPCODES is freed in crserverlib/server_stream.c with crNetFree.
     * OOB messages are the programmer's problem.  -- Humper 12/17/01
     */
    if (cached_type != CR_MESSAGE_OPCODES
        && cached_type != CR_MESSAGE_OOB
        && cached_type != CR_MESSAGE_GATHER)
    {
        _crNemuHGSMIFree(conn, msg);
    }
}

static void crNemuHGSMIReceiveMessage(CRConnection *conn)
{
    PCRNEMUHGSMI_CLIENT pClient;
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    pClient = _crNemuHGSMIClientGet();

    Assert(pClient);
    Assert(0);

    _crNemuHGSMIReceiveMessage(conn, pClient);
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}
/*
 * Called on host side only, to "accept" client connection
 */
static void crNemuHGSMIAccept( CRConnection *conn, const char *hostname, unsigned short port )
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    Assert(0);

    CRASSERT(conn && conn->pHostBuffer);
#ifdef IN_GUEST
    CRASSERT(FALSE);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static int crNemuHGSMISetVersion(CRConnection *conn, unsigned int vMajor, unsigned int vMinor)
{
    CRNEMUHGCMSETVERSION parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = conn->u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_SET_VERSION;
    parms.hdr.cParms      = SHCRGL_CPARMS_SET_VERSION;

    parms.vMajor.type      = VMMDevHGCMParmType_32bit;
    parms.vMajor.u.value32 = CR_PROTOCOL_VERSION_MAJOR;
    parms.vMinor.type      = VMMDevHGCMParmType_32bit;
    parms.vMinor.u.value32 = CR_PROTOCOL_VERSION_MINOR;

    rc = crNemuHGCMCall(&parms, sizeof(parms));

    AssertRC(rc);

    if (RT_FAILURE(rc) || RT_FAILURE(parms.hdr.result))
    {
        crWarning("Host doesn't accept our version %d.%d. Make sure you have appropriate additions installed!",
                  parms.vMajor.u.value32, parms.vMinor.u.value32);
        return FALSE;
    }

    conn->vMajor = CR_PROTOCOL_VERSION_MAJOR;
    conn->vMinor = CR_PROTOCOL_VERSION_MINOR;

    return TRUE;
}

static int crNemuHGSMISetPID(CRConnection *conn, unsigned long long pid)
{
    CRNEMUHGCMSETPID parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = conn->u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_SET_PID;
    parms.hdr.cParms      = SHCRGL_CPARMS_SET_PID;

    parms.u64PID.type     = VMMDevHGCMParmType_64bit;
    parms.u64PID.u.value64 = pid;

    rc = crNemuHGCMCall(&parms, sizeof(parms));

    if (RT_FAILURE(rc) || RT_FAILURE(parms.hdr.result))
    {
        Assert(0);

        crWarning("SHCRGL_GUEST_FN_SET_PID failed!");
        return FALSE;
    }

    return TRUE;
}

/**
 * The function that actually connects.  This should only be called by clients,
 * guests in nemu case.
 * Servers go through crNemuHGCMAccept;
 */
/*@todo use vbglR3Something here */
static int crNemuHGSMIDoConnect( CRConnection *conn )
{
#ifdef IN_GUEST
    NemuGuestHGCMConnectInfo info;

#ifdef RT_OS_WINDOWS
    DWORD cbReturned;
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    if (g_crnemuhgsmi.hGuestDrv == INVALID_HANDLE_VALUE)
    {
        /* open Nemu guest driver */
        g_crnemuhgsmi.hGuestDrv = CreateFile(NEMUGUEST_DEVICE_NAME,
                                        GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL,
                                        OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL,
                                        NULL);

        /* @todo check if we could rollback to softwareopengl */
        if (g_crnemuhgsmi.hGuestDrv == INVALID_HANDLE_VALUE)
        {
            Assert(0);
            crDebug("could not open Nemu Guest Additions driver! rc = %d\n", GetLastError());
            NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
            return FALSE;
        }
    }
#else
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    if (g_crnemuhgsmi.iGuestDrv == INVALID_HANDLE_VALUE)
    {
        g_crnemuhgsmi.iGuestDrv = open(NEMUGUEST_USER_DEVICE_NAME, O_RDWR, 0);
        if (g_crnemuhgsmi.iGuestDrv == INVALID_HANDLE_VALUE)
        {
            crDebug("could not open Guest Additions kernel module! rc = %d\n", errno);
            NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
            return FALSE;
        }
    }
#endif

    memset (&info, 0, sizeof (info));
    info.Loc.type = VMMDevHGCMLoc_LocalHost_Existing;
    strcpy (info.Loc.u.host.achName, "NemuSharedCrOpenGL");

#ifdef RT_OS_WINDOWS
    if (DeviceIoControl(g_crnemuhgsmi.hGuestDrv,
                        NEMUGUEST_IOCTL_HGCM_CONNECT,
                        &info, sizeof (info),
                        &info, sizeof (info),
                        &cbReturned,
                        NULL))
#elif defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
    VBGLBIGREQ Hdr;
    Hdr.u32Magic = VBGLBIGREQ_MAGIC;
    Hdr.cbData = sizeof(info);
    Hdr.pvDataR3 = &info;
# if HC_ARCH_BITS == 32
    Hdr.u32Padding = 0;
# endif
    if (ioctl(g_crnemuhgsmi.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CONNECT, &Hdr) >= 0)
#else
    if (ioctl(g_crnemuhgsmi.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CONNECT, &info, sizeof (info)) >= 0)
#endif
    {
        if (info.result == VINF_SUCCESS)
        {
            int rc;
            conn->u32ClientID = info.u32ClientID;
            crDebug("HGCM connect was successful: client id =0x%x\n", conn->u32ClientID);

            rc = crNemuHGSMISetVersion(conn, CR_PROTOCOL_VERSION_MAJOR, CR_PROTOCOL_VERSION_MINOR);
            if (!rc)
            {
                return rc;
            }
#ifdef RT_OS_WINDOWS
            rc = crNemuHGCMSetPID(conn, GetCurrentProcessId());
#else
            rc = crNemuHGCMSetPID(conn, crGetPID());
#endif
            NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
            return rc;
        }
        else
        {
            Assert(0);
            crDebug("HGCM connect failed with rc=0x%x\n", info.result);
        }
    }
    else
    {
#ifdef RT_OS_WINDOWS
        crDebug("IOCTL for HGCM connect failed with rc=0x%x\n", GetLastError());
#else
        crDebug("IOCTL for HGCM connect failed with rc=0x%x\n", errno);
#endif
    }

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();

    return FALSE;
#else /*#ifdef IN_GUEST*/
    crError("crNemuHGSMIDoConnect called on host side!");
    CRASSERT(FALSE);
    return FALSE;
#endif
}

/*@todo same, replace DeviceIoControl with vbglR3DoIOCtl */
static void crNemuHGSMIDoDisconnect( CRConnection *conn )
{
#ifdef IN_GUEST
    NemuGuestHGCMDisconnectInfo info;
# ifdef RT_OS_WINDOWS
    DWORD cbReturned;
# endif
    int i;
#endif
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    if (conn->pHostBuffer)
    {
        crFree(conn->pHostBuffer);
        conn->pHostBuffer = NULL;
        conn->cbHostBuffer = 0;
        conn->cbHostBufferAllocated = 0;
    }

    conn->pBuffer = NULL;
    conn->cbBuffer = 0;

    //@todo hold lock here?
    if (conn->type == CR_NEMUHGCM)
    {
        --g_crnemuhgsmi.num_conns;

        if (conn->index < g_crnemuhgsmi.num_conns)
        {
            g_crnemuhgsmi.conns[conn->index] = g_crnemuhgsmi.conns[g_crnemuhgsmi.num_conns];
            g_crnemuhgsmi.conns[conn->index]->index = conn->index;
        }
        else g_crnemuhgsmi.conns[conn->index] = NULL;

        conn->type = CR_NO_CONNECTION;
    }

#ifndef IN_GUEST
#else /* IN_GUEST */
    if (conn->u32ClientID)
    {
        memset (&info, 0, sizeof (info));
        info.u32ClientID = conn->u32ClientID;

# ifdef RT_OS_WINDOWS
        if ( !DeviceIoControl(g_crnemuhgsmi.hGuestDrv,
                               NEMUGUEST_IOCTL_HGCM_DISCONNECT,
                               &info, sizeof (info),
                               &info, sizeof (info),
                               &cbReturned,
                               NULL) )
        {
            crDebug("Disconnect failed with %x\n", GetLastError());
        }
# elif defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
        VBGLBIGREQ Hdr;
        Hdr.u32Magic = VBGLBIGREQ_MAGIC;
        Hdr.cbData = sizeof(info);
        Hdr.pvDataR3 = &info;
#  if HC_ARCH_BITS == 32
        Hdr.u32Padding = 0;
#  endif
        if (ioctl(g_crnemuhgsmi.iGuestDrv, NEMUGUEST_IOCTL_HGCM_DISCONNECT, &Hdr) >= 0)
# else
        if (ioctl(g_crnemuhgsmi.iGuestDrv, NEMUGUEST_IOCTL_HGCM_DISCONNECT, &info, sizeof (info)) < 0)
        {
            crDebug("Disconnect failed with %x\n", errno);
        }
# endif

        conn->u32ClientID = 0;
    }

    /* see if any connections remain */
    for (i = 0; i < g_crnemuhgsmi.num_conns; i++)
        if (g_crnemuhgsmi.conns[i] && g_crnemuhgsmi.conns[i]->type != CR_NO_CONNECTION)
            break;

    /* close guest additions driver*/
    if (i>=g_crnemuhgsmi.num_conns)
    {
# ifdef RT_OS_WINDOWS
        CloseHandle(g_crnemuhgsmi.hGuestDrv);
        g_crnemuhgsmi.hGuestDrv = INVALID_HANDLE_VALUE;
# else
        close(g_crnemuhgsmi.iGuestDrv);
        g_crnemuhgsmi.iGuestDrv = INVALID_HANDLE_VALUE;
# endif
    }
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
#endif /* IN_GUEST */
}

static void crNemuHGSMIInstantReclaim(CRConnection *conn, CRMessage *mess)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    Assert(0);

    _crNemuHGSMIFree(conn, mess);
    CRASSERT(FALSE);

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGSMIHandleNewMessage( CRConnection *conn, CRMessage *msg, unsigned int len )
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    Assert(0);

    CRASSERT(FALSE);
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static DECLCALLBACK(HNEMUCRHGSMI_CLIENT) _crNemuHGSMIClientCreate(PNEMUUHGSMI pHgsmi)
{
    PCRNEMUHGSMI_CLIENT pClient = crAlloc(sizeof (CRNEMUHGSMI_CLIENT));

    if (pClient)
    {
        int rc;
        pClient->pHgsmi = pHgsmi;
        rc = pHgsmi->pfnBufferCreate(pHgsmi, CRNEMUHGSMI_PAGE_ALIGN(1),
                                NEMUUHGSMI_SYNCHOBJECT_TYPE_EVENT,
                                NULL,
                                &pClient->pCmdBuffer);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = pHgsmi->pfnBufferCreate(pHgsmi, CRNEMUHGSMI_PAGE_ALIGN(0x800000),
                                            NEMUUHGSMI_SYNCHOBJECT_TYPE_EVENT,
                                            NULL,
                                            &pClient->pHGBuffer);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                pClient->pvHGBuffer = NULL;
                pClient->bufpool = crBufferPoolInit(16);
                return (HNEMUCRHGSMI_CLIENT) pClient;
            }
        }
    }

    return NULL;
}

static DECLCALLBACK(void) _crNemuHGSMIClientDestroy(HNEMUCRHGSMI_CLIENT hClient)
{
    Assert(0);

    /* @todo */
}


bool crNemuHGSMIInit(CRNetReceiveFuncList *rfl, CRNetCloseFuncList *cfl, unsigned int mtu)
{
    /* static */ int bHasHGSMI = -1; /* do it for all connections */
    (void) mtu;

    if (bHasHGSMI < 0)
    {
        int rc;
        NEMUCRHGSMI_CALLBACKS Callbacks;
        Callbacks.pfnClientCreate = _crNemuHGSMIClientCreate;
        Callbacks.pfnClientDestroy = _crNemuHGSMIClientDestroy;
        rc = NemuCrHgsmiInit(&Callbacks);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            bHasHGSMI = 1;
        else
            bHasHGSMI = 0;
    }

    Assert(bHasHGSMI);

    if (!bHasHGSMI)
    {
#ifdef DEBUG_misha
        AssertRelease(0);
#endif
        return false;
    }

    g_crnemuhgsmi.recv_list = rfl;
    g_crnemuhgsmi.close_list = cfl;
    if (g_crnemuhgsmi.initialized)
    {
        return true;
    }

    g_crnemuhgsmi.initialized = 1;

    g_crnemuhgsmi.num_conns = 0;
    g_crnemuhgsmi.conns     = NULL;
    g_crnemuhgsmi.mempool = crBufferPoolInit(16);

    /* Can't open Nemu guest driver here, because it gets called for host side as well */
    /*@todo as we have 2 dll versions, can do it now.*/

#ifdef RT_OS_WINDOWS
    g_crnemuhgsmi.hGuestDrv = INVALID_HANDLE_VALUE;
#else
    g_crnemuhgsmi.iGuestDrv = INVALID_HANDLE_VALUE;
#endif

#ifdef CHROMIUM_THREADSAFE
    crInitMutex(&g_crnemuhgsmi.mutex);
    crInitMutex(&g_crnemuhgsmi.recvmutex);
#endif

    return true;
}

/* Callback function used to free buffer pool entries */
void _crNemuHGSMISysMemFree(void *data)
{
    Assert(0);

    crFree(data);
}

void crNemuHGSMITearDown(void)
{
    int32_t i, cCons;

    Assert(0);

    if (!g_crnemuhgsmi.initialized) return;

    /* Connection count would be changed in calls to crNetDisconnect, so we have to store original value.
     * Walking array backwards is not a good idea as it could cause some issues if we'd disconnect clients not in the
     * order of their connection.
     */
    cCons = g_crnemuhgsmi.num_conns;
    for (i=0; i<cCons; i++)
    {
        /* Note that [0] is intended, as the connections array would be shifted in each call to crNetDisconnect */
        crNetDisconnect(g_crnemuhgsmi.conns[0]);
    }
    CRASSERT(0==g_crnemuhgsmi.num_conns);

#ifdef CHROMIUM_THREADSAFE
    crFreeMutex(&g_crnemuhgsmi.mutex);
    crFreeMutex(&g_crnemuhgsmi.recvmutex);
#endif

    if (g_crnemuhgsmi.mempool)
        crBufferPoolCallbackFree(g_crnemuhgsmi.mempool, _crNemuHGSMISysMemFree);
    g_crnemuhgsmi.mempool = NULL;

    g_crnemuhgsmi.initialized = 0;

    crFree(g_crnemuhgsmi.conns);
    g_crnemuhgsmi.conns = NULL;
}

void crNemuHGSMIConnection(CRConnection *conn)
{
    int i, found = 0;
    int n_bytes;

    CRASSERT(g_crnemuhgsmi.initialized);

    conn->type = CR_NEMUHGCM;
    conn->Alloc = crNemuHGSMIAlloc;
    conn->Send = crNemuHGSMISend;
    conn->SendExact = crNemuHGSMIWriteExact;
    conn->Recv = crNemuHGSMISingleRecv;
    conn->RecvMsg = crNemuHGSMIReceiveMessage;
    conn->Free = crNemuHGSMIFree;
    conn->Accept = crNemuHGSMIAccept;
    conn->Connect = crNemuHGSMIDoConnect;
    conn->Disconnect = crNemuHGSMIDoDisconnect;
    conn->InstantReclaim = crNemuHGSMIInstantReclaim;
    conn->HandleNewMessage = crNemuHGSMIHandleNewMessage;
    conn->index = g_crnemuhgsmi.num_conns;
    conn->sizeof_buffer_header = sizeof(CRNEMUHGSMIBUFFER);
    conn->actual_network = 1;

    conn->krecv_buf_size = 0;

    conn->pBuffer = NULL;
    conn->cbBuffer = 0;
    conn->allow_redir_ptr = 1;

    //@todo remove this crap at all later
    conn->cbHostBufferAllocated = 0;//2*1024;
    conn->pHostBuffer = NULL;//(uint8_t*) crAlloc(conn->cbHostBufferAllocated);
//    CRASSERT(conn->pHostBuffer);
    conn->cbHostBuffer = 0;

    /* Find a free slot */
    for (i = 0; i < g_crnemuhgsmi.num_conns; i++) {
        if (g_crnemuhgsmi.conns[i] == NULL) {
            conn->index = i;
            g_crnemuhgsmi.conns[i] = conn;
            found = 1;
            break;
        }
    }

    /* Realloc connection stack if we couldn't find a free slot */
    if (found == 0) {
        n_bytes = ( g_crnemuhgsmi.num_conns + 1 ) * sizeof(*g_crnemuhgsmi.conns);
        crRealloc( (void **) &g_crnemuhgsmi.conns, n_bytes );
        g_crnemuhgsmi.conns[g_crnemuhgsmi.num_conns++] = conn;
    }
}

int crNemuHGSMIRecv(void)
{
    int32_t i;
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

#ifdef IN_GUEST
    /* we're on guest side, poll host if it got something for us */
    for (i=0; i<g_crnemuhgsmi.num_conns; i++)
    {
        CRConnection *conn = g_crnemuhgsmi.conns[i];

        if ( !conn || conn->type == CR_NO_CONNECTION )
            continue;

        if (!conn->pBuffer)
        {
            PCRNEMUHGSMI_CLIENT pClient = _crNemuHGSMIClientGet();
            _crNemuHGSMIPollHost(conn, pClient);
        }
    }
#endif

    for (i=0; i<g_crnemuhgsmi.num_conns; i++)
    {
        CRConnection *conn = g_crnemuhgsmi.conns[i];

        if ( !conn || conn->type == CR_NO_CONNECTION )
            continue;

        if (conn->cbBuffer>0)
        {
            PCRNEMUHGSMI_CLIENT pClient = _crNemuHGSMIClientGet();
            _crNemuHGSMIReceiveMessage(conn, pClient);
        }
    }

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
    return 0;
}

CRConnection** crNemuHGSMIDump( int *num )
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    Assert(0);
    *num = g_crnemuhgsmi.num_conns;

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
    return g_crnemuhgsmi.conns;
}
#endif /* #ifdef IN_GUEST */
