/* $Id: nemuhgcm.c $ */

/** @file
 * Nemu HGCM connection
 */

/*
 * Copyright (C) 2008-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
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

#if 1 /** @todo Try use the Vbgl interface instead of talking directly to the driver? */
# include <Nemu/NemuGuest.h>
#else
# include <Nemu/NemuGuestLib.h>
#endif
#include <Nemu/HostServices/NemuCrOpenGLSvc.h>

#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
#include <Nemu/NemuCrHgsmi.h>
#endif

/*@todo move define somewhere else, and make sure it's less than VBGLR0_MAX_HGCM_KERNEL_PARM*/
/*If we fail to pass data in one chunk, send it in chunks of this size instead*/
#define CR_HGCM_SPLIT_BUFFER_SIZE (8*_1M)

#ifndef MIN
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifdef DEBUG_misha
#ifdef CRASSERT
# undef CRASSERT
#endif
#define CRASSERT Assert
#endif
//#define IN_GUEST
//#if defined(IN_GUEST)
//#define NEMU_WITH_CRHGSMIPROFILE
//#endif
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

#ifdef NEMU_WITH_CRHGSMI
     NemuCrHgsmiLog(szBuffer);
#else
     OutputDebugString(szBuffer);
#endif
}

DECLINLINE(void) nemuCrHgsmiProfileLog(PNEMUCRHGSMIPROFILE pProfile, uint64_t cTime)
{
    uint64_t profileTime = cTime - pProfile->cStartTime;
    double percent = ((double)100.0) * pProfile->cStepsTime / profileTime;
    double cps = ((double)1000000000.) * pProfile->cSteps / profileTime;
    nemuCrHgsmiLog("hgcm: cps: %.1f, host %.1f%%\n", cps, percent);
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
    CRBufferPool         *bufpool;
#ifdef CHROMIUM_THREADSAFE
    CRmutex              mutex;
    CRmutex              recvmutex;
#endif
    CRNetReceiveFuncList *recv_list;
    CRNetCloseFuncList   *close_list;
#ifdef RT_OS_WINDOWS
    HANDLE               hGuestDrv;
    LPDIRECTDRAW         pDirectDraw;
#else
    int                  iGuestDrv;
#endif
#ifdef IN_GUEST
    uint32_t             u32HostCaps;
    bool                 fHostCapsInitialized;
#endif
#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
    bool bHgsmiOn;
#endif
} CRNEMUHGCMDATA;

static CRNEMUHGCMDATA g_crnemuhgcm = {0,};

typedef enum {
    CR_NEMUHGCM_USERALLOCATED,
    CR_NEMUHGCM_MEMORY,
    CR_NEMUHGCM_MEMORY_BIG
#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
    ,CR_NEMUHGCM_UHGSMI_BUFFER
#endif
} CRNEMUHGCMBUFFERKIND;

#define CR_NEMUHGCM_BUFFER_MAGIC 0xABCDE321

typedef struct CRNEMUHGCMBUFFER {
    uint32_t             magic;
    CRNEMUHGCMBUFFERKIND kind;
    union
    {
        struct
        {
            uint32_t             len;
            uint32_t             allocated;
        };

#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
        PNEMUUHGSMI_BUFFER pBuffer;
#endif
    };
} CRNEMUHGCMBUFFER;

#ifndef RT_OS_WINDOWS
    #define TRUE true
    #define FALSE false
    #define INVALID_HANDLE_VALUE (-1)
#endif


#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)

/* add sizeof header + page align */
#define CRNEMUHGSMI_PAGE_ALIGN(_s) (((_s)  + 0xfff) & ~0xfff)
#define CRNEMUHGSMI_BUF_HDR_SIZE() (sizeof (CRNEMUHGCMBUFFER))
#define CRNEMUHGSMI_BUF_SIZE(_s) CRNEMUHGSMI_PAGE_ALIGN((_s) + CRNEMUHGSMI_BUF_HDR_SIZE())
#define CRNEMUHGSMI_BUF_LOCK_SIZE(_s) ((_s) + CRNEMUHGSMI_BUF_HDR_SIZE())
#define CRNEMUHGSMI_BUF_DATA(_p) ((void*)(((CRNEMUHGCMBUFFER*)(_p)) + 1))
#define CRNEMUHGSMI_BUF_HDR(_p) (((CRNEMUHGCMBUFFER*)(_p)) - 1)
#define CRNEMUHGSMI_BUF_OFFSET(_st2, _st1) ((uint32_t)(((uint8_t*)(_st2)) - ((uint8_t*)(_st1))))

static int _crNemuHGSMIClientInit(PCRNEMUHGSMI_CLIENT pClient, PNEMUUHGSMI pHgsmi)
{
    int rc;
    NEMUUHGSMI_BUFFER_TYPE_FLAGS Flags = {0};
    pClient->pHgsmi = pHgsmi;
    Flags.fCommand = 1;
    rc = pHgsmi->pfnBufferCreate(pHgsmi, CRNEMUHGSMI_PAGE_ALIGN(1), Flags, &pClient->pCmdBuffer);
    if (RT_SUCCESS(rc))
    {
        Flags.Value = 0;
        rc = pHgsmi->pfnBufferCreate(pHgsmi, CRNEMUHGSMI_PAGE_ALIGN(1), Flags, &pClient->pHGBuffer);
        if (RT_SUCCESS(rc))
        {
            pClient->pvHGBuffer = NULL;
            pClient->bufpool = crBufferPoolInit(16);
            return VINF_SUCCESS;
        }
        else
            crWarning("_crNemuHGSMIClientInit: pfnBufferCreate failed to allocate host->guest buffer");

        pClient->pCmdBuffer->pfnDestroy(pClient->pCmdBuffer);
    }
    else
        crWarning("_crNemuHGSMIClientInit: pfnBufferCreate failed to allocate cmd buffer");

    pClient->pHgsmi = NULL;
    return rc;
}

void _crNemuHGSMIBufferFree(void *data)
{
    PNEMUUHGSMI_BUFFER pBuffer = (PNEMUUHGSMI_BUFFER)data;
    pBuffer->pfnDestroy(pBuffer);
}

static int _crNemuHGSMIClientTerm(PCRNEMUHGSMI_CLIENT pClient, PNEMUUHGSMI *ppHgsmi)
{
    if (pClient->bufpool)
        crBufferPoolCallbackFree(pClient->bufpool, _crNemuHGSMIBufferFree);
    pClient->bufpool = NULL;

    if (pClient->pHGBuffer)
    {
        pClient->pHGBuffer->pfnDestroy(pClient->pHGBuffer);
        pClient->pHGBuffer = NULL;
    }

    if (pClient->pCmdBuffer)
    {
        pClient->pCmdBuffer->pfnDestroy(pClient->pCmdBuffer);
        pClient->pCmdBuffer = NULL;
    }

    if (ppHgsmi)
    {
        *ppHgsmi = pClient->pHgsmi;
    }
    pClient->pHgsmi = NULL;

    return VINF_SUCCESS;
}


#ifdef NEMU_CRHGSMI_WITH_D3DDEV

static DECLCALLBACK(HNEMUCRHGSMI_CLIENT) _crNemuHGSMIClientCreate(PNEMUUHGSMI pHgsmi)
{
    PCRNEMUHGSMI_CLIENT pClient = crAlloc(sizeof (CRNEMUHGSMI_CLIENT));

    if (pClient)
    {
        int rc = _crNemuHGSMIClientInit(pClient, pHgsmi);
        if (RT_SUCCESS(rc))
            return (HNEMUCRHGSMI_CLIENT)pClient;
        else
            crWarning("_crNemuHGSMIClientCreate: _crNemuHGSMIClientInit failed rc %d", rc);

        crFree(pCLient);
    }

    return NULL;
}

static DECLCALLBACK(void) _crNemuHGSMIClientDestroy(HNEMUCRHGSMI_CLIENT hClient)
{
    PCRNEMUHGSMI_CLIENT pClient = (PCRNEMUHGSMI_CLIENT)hClient;
    _crNemuHGSMIClientTerm(pClient, NULL);
    crFree(pClient);
}
#endif

DECLINLINE(PCRNEMUHGSMI_CLIENT) _crNemuHGSMIClientGet(CRConnection *conn)
{
#ifdef NEMU_CRHGSMI_WITH_D3DDEV
    PCRNEMUHGSMI_CLIENT pClient = (PCRNEMUHGSMI_CLIENT)NemuCrHgsmiQueryClient();
    CRASSERT(pClient);
    return pClient;
#else
    if (conn->HgsmiClient.pHgsmi)
        return &conn->HgsmiClient;
    {
        PNEMUUHGSMI pHgsmi = conn->pExternalHgsmi ? conn->pExternalHgsmi : NemuCrHgsmiCreate();
        if (pHgsmi)
        {
            int rc = _crNemuHGSMIClientInit(&conn->HgsmiClient, pHgsmi);
            if (RT_SUCCESS(rc))
            {
                CRASSERT(conn->HgsmiClient.pHgsmi);
                return &conn->HgsmiClient;
            }
            else
                crWarning("_crNemuHGSMIClientGet: _crNemuHGSMIClientInit failed rc %d", rc);
            if (!conn->pExternalHgsmi)
                NemuCrHgsmiDestroy(pHgsmi);
        }
        else
        {
            crWarning("NemuCrHgsmiCreate failed");
        }
    }
    return NULL;
#endif
}

static PNEMUUHGSMI_BUFFER _crNemuHGSMIBufAlloc(PCRNEMUHGSMI_CLIENT pClient, uint32_t cbSize)
{
    PNEMUUHGSMI_BUFFER buf;
    int rc;

    buf = (PNEMUUHGSMI_BUFFER ) crBufferPoolPop(pClient->bufpool, cbSize);

    if (!buf)
    {
        NEMUUHGSMI_BUFFER_TYPE_FLAGS Flags = {0};
        crDebug("Buffer pool %p was empty; allocating new %d byte buffer.",
                        (void *) pClient->bufpool,
                        cbSize);
        rc = pClient->pHgsmi->pfnBufferCreate(pClient->pHgsmi, cbSize, Flags, &buf);
        if (RT_FAILURE(rc))
            crWarning("_crNemuHGSMIBufAlloc: Failed to Create a buffer of size(%d), rc(%d)\n", cbSize, rc);
    }
    return buf;
}

static PNEMUUHGSMI_BUFFER _crNemuHGSMIBufFromHdr(CRNEMUHGCMBUFFER *pHdr)
{
    PNEMUUHGSMI_BUFFER pBuf;
    int rc;
    CRASSERT(pHdr->magic == CR_NEMUHGCM_BUFFER_MAGIC);
    CRASSERT(pHdr->kind == CR_NEMUHGCM_UHGSMI_BUFFER);
    pBuf = pHdr->pBuffer;
    rc = pBuf->pfnUnlock(pBuf);
    if (RT_FAILURE(rc))
    {
        crWarning("_crNemuHGSMIBufFromHdr: pfnUnlock failed rc %d", rc);
        return NULL;
    }
    return pBuf;
}

static void _crNemuHGSMIBufFree(PCRNEMUHGSMI_CLIENT pClient, PNEMUUHGSMI_BUFFER pBuf)
{
    crBufferPoolPush(pClient->bufpool, pBuf, pBuf->cbBuffer);
}

static CRNEMUHGSMIHDR *_crNemuHGSMICmdBufferLock(PCRNEMUHGSMI_CLIENT pClient, uint32_t cbBuffer)
{
    /* in theory it is OK to use one cmd buffer for asynch cmd submission
     * because bDiscard flag should result in allocating a new memory backend if the
     * allocation is still in use.
     * However, NOTE: since one and the same semaphore synch event is used for completion notification,
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
    if (RT_SUCCESS(rc))
        return pHdr;
    else
        crWarning("_crNemuHGSMICmdBufferLock: pfnLock failed rc %d", rc);

    crWarning("Failed to Lock the command buffer of size(%d), rc(%d)\n", cbBuffer, rc);
    return NULL;
}

static CRNEMUHGSMIHDR *_crNemuHGSMICmdBufferLockRo(PCRNEMUHGSMI_CLIENT pClient, uint32_t cbBuffer)
{
    /* in theory it is OK to use one cmd buffer for asynch cmd submission
     * because bDiscard flag should result in allocating a new memory backend if the
     * allocation is still in use.
     * However, NOTE: since one and the same semaphore synch event is used for completion notification,
     * for the notification mechanism working as expected
     * 1. host must complete commands in the same order as it receives them
     * (to avoid situation when guest receives notification for another command completion)
     * 2. guest must eventually wait for command completion unless he specified bDoNotSignalCompletion
     * 3. guest must wait for command completion in the same order as it submits them
     * in case we can not satisfy any of the above, we should introduce multiple command buffers */
    CRNEMUHGSMIHDR * pHdr = NULL;
    NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
    int rc;
    fFlags.Value = 0;
    fFlags.bReadOnly = 1;
    rc = pClient->pCmdBuffer->pfnLock(pClient->pCmdBuffer, 0, cbBuffer, fFlags, (void**)&pHdr);
    if (RT_FAILURE(rc))
        crWarning("Failed to Lock the command buffer of size(%d), rc(%d)\n", cbBuffer, rc);
    return pHdr;
}

static void _crNemuHGSMICmdBufferUnlock(PCRNEMUHGSMI_CLIENT pClient)
{
    int rc = pClient->pCmdBuffer->pfnUnlock(pClient->pCmdBuffer);
    if (RT_FAILURE(rc))
        crError("Failed to Unlock the command buffer rc(%d)\n", rc);
}

static int32_t _crNemuHGSMICmdBufferGetRc(PCRNEMUHGSMI_CLIENT pClient)
{
    CRNEMUHGSMIHDR * pHdr;
    NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
    int rc;

    fFlags.Value = 0;
    fFlags.bReadOnly = 1;
    rc = pClient->pCmdBuffer->pfnLock(pClient->pCmdBuffer, 0, sizeof (*pHdr), fFlags, (void**)&pHdr);
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
    CRASSERT(!pClient->pvHGBuffer);
    fFlags.Value = 0;
    rc = pClient->pHGBuffer->pfnLock(pClient->pHGBuffer, 0, cbBuffer, fFlags, &pClient->pvHGBuffer);
    if (RT_SUCCESS(rc))
        return pClient->pvHGBuffer;
    else
        crWarning("_crNemuHGSMIRecvBufData: pfnLock failed rc %d", rc);

    return NULL;
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
#endif

/* Some forward declarations */
static void _crNemuHGCMReceiveMessage(CRConnection *conn);

#ifndef IN_GUEST
static bool _crNemuHGCMReadBytes(CRConnection *conn, void *buf, uint32_t len)
{
    CRASSERT(conn && buf);

    if (!conn->pBuffer || (conn->cbBuffer<len))
        return FALSE;

    crMemcpy(buf, conn->pBuffer, len);

    conn->cbBuffer -= len;
    conn->pBuffer = conn->cbBuffer>0 ? (uint8_t*)conn->pBuffer+len : NULL;

    return TRUE;
}
#endif

/*@todo get rid of it*/
static bool _crNemuHGCMWriteBytes(CRConnection *conn, const void *buf, uint32_t len)
{
    CRASSERT(conn && buf);

    /* make sure there's host buffer and it's clear */
    CRASSERT(conn->pHostBuffer && !conn->cbHostBuffer);

    if (conn->cbHostBufferAllocated < len)
    {
        crDebug("Host buffer too small %d out of requested %d bytes, reallocating", conn->cbHostBufferAllocated, len);
        crFree(conn->pHostBuffer);
        conn->pHostBuffer = crAlloc(len);
        if (!conn->pHostBuffer)
        {
            conn->cbHostBufferAllocated = 0;
            crError("OUT_OF_MEMORY trying to allocate %d bytes", len);
            return FALSE;
        }
        conn->cbHostBufferAllocated = len;
    }

    crMemcpy(conn->pHostBuffer, buf, len);
    conn->cbHostBuffer = len;

    return TRUE;
}

/**
 * Send an HGCM request
 *
 * @return Nemu status code
 * @param   pvData      Data pointer
 * @param   cbData      Data size
 */
/** @todo use vbglR3DoIOCtl here instead */
static int crNemuHGCMCall(CRConnection *conn, void *pvData, unsigned cbData)
{
#ifdef IN_GUEST
# if defined(NEMU_WITH_CRHGSMI)
    PCRNEMUHGSMI_CLIENT pClient = g_crnemuhgcm.bHgsmiOn ? _crNemuHGSMIClientGet(conn) : NULL;
    if (pClient)
    {
        return NemuCrHgsmiCtlConCall(pClient->pHgsmi, (struct NemuGuestHGCMCallInfo *)pvData, cbData);
    }
    else
# endif
    {
# ifdef RT_OS_WINDOWS
    DWORD cbReturned, lerr;

    if (DeviceIoControl (g_crnemuhgcm.hGuestDrv,
                         NEMUGUEST_IOCTL_HGCM_CALL(cbData),
                         pvData, cbData,
                         pvData, cbData,
                         &cbReturned,
                         NULL))
    {
        return VINF_SUCCESS;
    }
    lerr=GetLastError();
    crDebug("nemuCall failed with %x\n", lerr);
    /*On windows if we exceed max buffer len, we only get ERROR_GEN_FAILURE, and parms.hdr.result isn't changed.
     *Before every call here we set it to VERR_WRONG_ORDER, so checking it here as well.
     */
    if (ERROR_GEN_FAILURE==lerr && VERR_WRONG_ORDER==((NemuGuestHGCMCallInfo*)pvData)->result)
    {
        return VERR_OUT_OF_RANGE;
    }
    else
    {
        return VERR_NOT_SUPPORTED;
    }
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
    rc = ioctl(g_crnemuhgcm.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CALL(cbData), &Hdr);
#  else
    rc = ioctl(g_crnemuhgcm.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CALL(cbData), pvData);
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
                rc = ioctl(g_crnemuhgcm.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CALL(cbData), pvData);
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
        return -rc;
    }
    else
#  endif
        crWarning("nemuCall failed with %x\n", errno);
    return VERR_NOT_SUPPORTED;
# endif /*#ifdef RT_OS_WINDOWS*/
    }
#else /*#ifdef IN_GUEST*/
    crError("crNemuHGCMCall called on host side!");
    CRASSERT(FALSE);
    return VERR_NOT_SUPPORTED;
#endif
}

static void *_crNemuHGCMAlloc(CRConnection *conn)
{
    CRNEMUHGCMBUFFER *buf;

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    buf = (CRNEMUHGCMBUFFER *) crBufferPoolPop(g_crnemuhgcm.bufpool, conn->buffer_size);

    if (!buf)
    {
        crDebug("Buffer pool %p was empty; allocating new %d byte buffer.",
                        (void *) g_crnemuhgcm.bufpool,
                        (unsigned int)sizeof(CRNEMUHGCMBUFFER) + conn->buffer_size);

        /* We're either on host side, or we failed to allocate DDRAW buffer */
        if (!buf)
        {
            crDebug("Using system malloc\n");
            buf = (CRNEMUHGCMBUFFER *) crAlloc( sizeof(CRNEMUHGCMBUFFER) + conn->buffer_size );
            CRASSERT(buf);
            buf->magic = CR_NEMUHGCM_BUFFER_MAGIC;
            buf->kind  = CR_NEMUHGCM_MEMORY;
            buf->allocated = conn->buffer_size;
        }
    }

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif

    return (void *)( buf + 1 );

}

static void *crNemuHGCMAlloc(CRConnection *conn)
{
    void *pvBuff;
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    pvBuff = _crNemuHGCMAlloc(conn);
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
    return pvBuff;
}

static void _crNemuHGCMWriteExact(CRConnection *conn, const void *buf, unsigned int len)
{
    int rc;
    int32_t callRes;

#ifdef IN_GUEST
    if (conn->u32InjectClientID)
    {
        CRNEMUHGCMINJECT parms;

        parms.hdr.result      = VERR_WRONG_ORDER;
        parms.hdr.u32ClientID = conn->u32ClientID;
        parms.hdr.u32Function = SHCRGL_GUEST_FN_INJECT;
        parms.hdr.cParms      = SHCRGL_CPARMS_INJECT;

        parms.u32ClientID.type       = VMMDevHGCMParmType_32bit;
        parms.u32ClientID.u.value32  = conn->u32InjectClientID;

        parms.pBuffer.type                   = VMMDevHGCMParmType_LinAddr_In;
        parms.pBuffer.u.Pointer.size         = len;
        parms.pBuffer.u.Pointer.u.linearAddr = (uintptr_t) buf;

        rc = crNemuHGCMCall(conn, &parms, sizeof(parms));
        callRes = parms.hdr.result;
    }
    else
#endif
    {
        CRNEMUHGCMWRITE parms;

        parms.hdr.result      = VERR_WRONG_ORDER;
        parms.hdr.u32ClientID = conn->u32ClientID;
        parms.hdr.u32Function = SHCRGL_GUEST_FN_WRITE;
        parms.hdr.cParms      = SHCRGL_CPARMS_WRITE;

        parms.pBuffer.type                   = VMMDevHGCMParmType_LinAddr_In;
        parms.pBuffer.u.Pointer.size         = len;
        parms.pBuffer.u.Pointer.u.linearAddr = (uintptr_t) buf;

        rc = crNemuHGCMCall(conn, &parms, sizeof(parms));
        callRes = parms.hdr.result;
    }

    if (RT_FAILURE(rc) || RT_FAILURE(callRes))
    {
        crWarning("SHCRGL_GUEST_FN_WRITE failed with %x %x\n", rc, callRes);
    }
}

static void crNemuHGCMWriteExact(CRConnection *conn, const void *buf, unsigned int len)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    _crNemuHGCMWriteExact(conn, buf, len);
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGCMReadExact( CRConnection *conn, const void *buf, unsigned int len )
{
    CRNEMUHGCMREAD parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = conn->u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_READ;
    parms.hdr.cParms      = SHCRGL_CPARMS_READ;

    CRASSERT(!conn->pBuffer); //make sure there's no data to process
    parms.pBuffer.type                   = VMMDevHGCMParmType_LinAddr_Out;
    parms.pBuffer.u.Pointer.size         = conn->cbHostBufferAllocated;
    parms.pBuffer.u.Pointer.u.linearAddr = (uintptr_t) conn->pHostBuffer;

    parms.cbBuffer.type      = VMMDevHGCMParmType_32bit;
    parms.cbBuffer.u.value32 = 0;

    rc = crNemuHGCMCall(conn, &parms, sizeof(parms));

    if (RT_FAILURE(rc) || RT_FAILURE(parms.hdr.result))
    {
        crWarning("SHCRGL_GUEST_FN_READ failed with %x %x\n", rc, parms.hdr.result);
        return;
    }

    if (parms.cbBuffer.u.value32)
    {
        //conn->pBuffer  = (uint8_t*) parms.pBuffer.u.Pointer.u.linearAddr;
        conn->pBuffer  = conn->pHostBuffer;
        conn->cbBuffer = parms.cbBuffer.u.value32;
    }

    if (conn->cbBuffer)
        _crNemuHGCMReceiveMessage(conn);

}

/* Same as crNemuHGCMWriteExact, but combined with read of writeback data.
 * This halves the number of HGCM calls we do,
 * most likely crNemuHGCMPollHost shouldn't be called at all now.
 */
static void
crNemuHGCMWriteReadExact(CRConnection *conn, const void *buf, unsigned int len, CRNEMUHGCMBUFFERKIND bufferKind)
{
    CRNEMUHGCMWRITEREAD parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = conn->u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_WRITE_READ;
    parms.hdr.cParms      = SHCRGL_CPARMS_WRITE_READ;

    parms.pBuffer.type                   = VMMDevHGCMParmType_LinAddr_In;
    parms.pBuffer.u.Pointer.size         = len;
    parms.pBuffer.u.Pointer.u.linearAddr = (uintptr_t) buf;

    CRASSERT(!conn->pBuffer); //make sure there's no data to process
    parms.pWriteback.type                   = VMMDevHGCMParmType_LinAddr_Out;
    parms.pWriteback.u.Pointer.size         = conn->cbHostBufferAllocated;
    parms.pWriteback.u.Pointer.u.linearAddr = (uintptr_t) conn->pHostBuffer;

    parms.cbWriteback.type      = VMMDevHGCMParmType_32bit;
    parms.cbWriteback.u.value32 = 0;

    rc = crNemuHGCMCall(conn, &parms, sizeof(parms));

#if defined(RT_OS_LINUX) || defined(RT_OS_WINDOWS)
    if (VERR_OUT_OF_RANGE==rc && CR_NEMUHGCM_USERALLOCATED==bufferKind)
    {
        /*Buffer is too big, so send it in split chunks*/
        CRNEMUHGCMWRITEBUFFER wbParms;

        wbParms.hdr.result = VERR_WRONG_ORDER;
        wbParms.hdr.u32ClientID = conn->u32ClientID;
        wbParms.hdr.u32Function = SHCRGL_GUEST_FN_WRITE_BUFFER;
        wbParms.hdr.cParms = SHCRGL_CPARMS_WRITE_BUFFER;

        wbParms.iBufferID.type = VMMDevHGCMParmType_32bit;
        wbParms.iBufferID.u.value32 = 0;

        wbParms.cbBufferSize.type = VMMDevHGCMParmType_32bit;
        wbParms.cbBufferSize.u.value32 = len;

        wbParms.ui32Offset.type = VMMDevHGCMParmType_32bit;
        wbParms.ui32Offset.u.value32 = 0;

        wbParms.pBuffer.type = VMMDevHGCMParmType_LinAddr_In;
        wbParms.pBuffer.u.Pointer.size         = MIN(CR_HGCM_SPLIT_BUFFER_SIZE, len);
        wbParms.pBuffer.u.Pointer.u.linearAddr = (uintptr_t) buf;

        if (len<CR_HGCM_SPLIT_BUFFER_SIZE)
        {
            crError("VERR_OUT_OF_RANGE in crNemuHGCMWriteReadExact for %u bytes write", len);
            return;
        }

        while (wbParms.pBuffer.u.Pointer.size)
        {
            crDebug("SHCRGL_GUEST_FN_WRITE_BUFFER, offset=%u, size=%u", wbParms.ui32Offset.u.value32, wbParms.pBuffer.u.Pointer.size);

            rc = crNemuHGCMCall(conn, &wbParms, sizeof(wbParms));
            if (RT_FAILURE(rc) || RT_FAILURE(wbParms.hdr.result))
            {
                crError("SHCRGL_GUEST_FN_WRITE_BUFFER (%i) failed with %x %x\n", wbParms.pBuffer.u.Pointer.size, rc, wbParms.hdr.result);
                return;
            }

            wbParms.ui32Offset.u.value32 += wbParms.pBuffer.u.Pointer.size;
            wbParms.pBuffer.u.Pointer.u.linearAddr += (uintptr_t) wbParms.pBuffer.u.Pointer.size;
            wbParms.pBuffer.u.Pointer.size = MIN(CR_HGCM_SPLIT_BUFFER_SIZE, len-wbParms.ui32Offset.u.value32);
        }

        /*now issue GUEST_FN_WRITE_READ_BUFFERED referencing the buffer we'd made*/
        {
            CRNEMUHGCMWRITEREADBUFFERED wrbParms;

            wrbParms.hdr.result = VERR_WRONG_ORDER;
            wrbParms.hdr.u32ClientID = conn->u32ClientID;
            wrbParms.hdr.u32Function = SHCRGL_GUEST_FN_WRITE_READ_BUFFERED;
            wrbParms.hdr.cParms = SHCRGL_CPARMS_WRITE_READ_BUFFERED;

            crMemcpy(&wrbParms.iBufferID, &wbParms.iBufferID, sizeof(HGCMFunctionParameter));
            crMemcpy(&wrbParms.pWriteback, &parms.pWriteback, sizeof(HGCMFunctionParameter));
            crMemcpy(&wrbParms.cbWriteback, &parms.cbWriteback, sizeof(HGCMFunctionParameter));

            rc = crNemuHGCMCall(conn, &wrbParms, sizeof(wrbParms));

            /*bit of hack to reuse code below*/
            parms.hdr.result = wrbParms.hdr.result;
            crMemcpy(&parms.cbWriteback, &wrbParms.cbWriteback, sizeof(HGCMFunctionParameter));
            crMemcpy(&parms.pWriteback, &wrbParms.pWriteback, sizeof(HGCMFunctionParameter));
        }
    }
#endif

    if (RT_FAILURE(rc) || RT_FAILURE(parms.hdr.result))
    {

        if ((VERR_BUFFER_OVERFLOW == parms.hdr.result) && RT_SUCCESS(rc))
        {
            /* reallocate buffer and retry */

            CRASSERT(parms.cbWriteback.u.value32>conn->cbHostBufferAllocated);

            crDebug("Reallocating host buffer from %d to %d bytes", conn->cbHostBufferAllocated, parms.cbWriteback.u.value32);

            crFree(conn->pHostBuffer);
            conn->cbHostBufferAllocated = parms.cbWriteback.u.value32;
            conn->pHostBuffer = crAlloc(conn->cbHostBufferAllocated);

            crNemuHGCMReadExact(conn, buf, len);

            return;
        }
        else
        {
            crWarning("SHCRGL_GUEST_FN_WRITE_READ (%i) failed with %x %x\n", len, rc, parms.hdr.result);
            return;
        }
    }

    if (parms.cbWriteback.u.value32)
    {
        //conn->pBuffer  = (uint8_t*) parms.pWriteback.u.Pointer.u.linearAddr;
        conn->pBuffer  = conn->pHostBuffer;
        conn->cbBuffer = parms.cbWriteback.u.value32;
    }

    if (conn->cbBuffer)
        _crNemuHGCMReceiveMessage(conn);
}

static void crNemuHGCMSend(CRConnection *conn, void **bufp,
                           const void *start, unsigned int len)
{
    CRNEMUHGCMBUFFER *hgcm_buffer;
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    if (!bufp) /* We're sending a user-allocated buffer. */
    {
#ifndef IN_GUEST
            //@todo remove temp buffer allocation in unpacker
            /* we're at the host side, so just store data until guest polls us */
            _crNemuHGCMWriteBytes(conn, start, len);
#else
            CRASSERT(!conn->u32InjectClientID);
            crDebug("SHCRGL: sending userbuf with %d bytes\n", len);
            crNemuHGCMWriteReadExact(conn, start, len, CR_NEMUHGCM_USERALLOCATED);
#endif
#ifdef CHROMIUM_THREADSAFE
            crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
            NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return;
    }

    /* The region [start .. start + len + 1] lies within a buffer that
     * was allocated with crNemuHGCMAlloc() and can be put into the free
     * buffer pool when we're done sending it.
     */

    hgcm_buffer = (CRNEMUHGCMBUFFER *)(*bufp) - 1;
    CRASSERT(hgcm_buffer->magic == CR_NEMUHGCM_BUFFER_MAGIC);

    /* Length would be passed as part of HGCM pointer description
     * No need to prepend it to the buffer
     */
#ifdef IN_GUEST
    if (conn->u32InjectClientID)
    {
        _crNemuHGCMWriteExact(conn, start, len);
    }
    else
#endif
    crNemuHGCMWriteReadExact(conn, start, len, hgcm_buffer->kind);

    /* Reclaim this pointer for reuse */
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    crBufferPoolPush(g_crnemuhgcm.bufpool, hgcm_buffer, hgcm_buffer->allocated);
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif

    /* Since the buffer's now in the 'free' buffer pool, the caller can't
     * use it any more.  Setting bufp to NULL will make sure the caller
     * doesn't try to re-use the buffer.
     */
    *bufp = NULL;

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGCMPollHost(CRConnection *conn)
{
    CRNEMUHGCMREAD parms;
    int rc;

    CRASSERT(!conn->pBuffer);

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = conn->u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_READ;
    parms.hdr.cParms      = SHCRGL_CPARMS_READ;

    parms.pBuffer.type                   = VMMDevHGCMParmType_LinAddr_Out;
    parms.pBuffer.u.Pointer.size         = conn->cbHostBufferAllocated;
    parms.pBuffer.u.Pointer.u.linearAddr = (uintptr_t) conn->pHostBuffer;

    parms.cbBuffer.type      = VMMDevHGCMParmType_32bit;
    parms.cbBuffer.u.value32 = 0;

    rc = crNemuHGCMCall(conn, &parms, sizeof(parms));

    if (RT_FAILURE(rc) || RT_FAILURE(parms.hdr.result))
    {
        crDebug("SHCRGL_GUEST_FN_READ failed with %x %x\n", rc, parms.hdr.result);
        return;
    }

    if (parms.cbBuffer.u.value32)
    {
        conn->pBuffer = (uint8_t*) parms.pBuffer.u.Pointer.u.linearAddr;
        conn->cbBuffer = parms.cbBuffer.u.value32;
    }
}

static void crNemuHGCMSingleRecv(CRConnection *conn, void *buf, unsigned int len)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    crNemuHGCMReadExact(conn, buf, len);
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void _crNemuHGCMFree(CRConnection *conn, void *buf)
{
    CRNEMUHGCMBUFFER *hgcm_buffer = (CRNEMUHGCMBUFFER *) buf - 1;

    CRASSERT(hgcm_buffer->magic == CR_NEMUHGCM_BUFFER_MAGIC);

    /*@todo wrong len for redir buffers*/
    conn->recv_credits += hgcm_buffer->len;

    switch (hgcm_buffer->kind)
    {
        case CR_NEMUHGCM_MEMORY:
#ifdef CHROMIUM_THREADSAFE
            crLockMutex(&g_crnemuhgcm.mutex);
#endif
            if (g_crnemuhgcm.bufpool) {
                //@todo o'rly?
                /* pool may have been deallocated just a bit earlier in response
                 * to a SIGPIPE (Broken Pipe) signal.
                 */
                crBufferPoolPush(g_crnemuhgcm.bufpool, hgcm_buffer, hgcm_buffer->allocated);
            }
#ifdef CHROMIUM_THREADSAFE
            crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
            break;

        case CR_NEMUHGCM_MEMORY_BIG:
            crFree( hgcm_buffer );
            break;

        default:
            crError( "Weird buffer kind trying to free in crNemuHGCMFree: %d", hgcm_buffer->kind );
    }
}

static void crNemuHGCMFree(CRConnection *conn, void *buf)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    _crNemuHGCMFree(conn, buf);
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void _crNemuHGCMReceiveMessage(CRConnection *conn)
{
    uint32_t len;
    CRNEMUHGCMBUFFER *hgcm_buffer;
    CRMessage *msg;
    CRMessageType cached_type;

    len = conn->cbBuffer;
    CRASSERT(len > 0);
    CRASSERT(conn->pBuffer);

#ifndef IN_GUEST
    if (conn->allow_redir_ptr)
    {
#endif //IN_GUEST
        CRASSERT(conn->buffer_size >= sizeof(CRMessageRedirPtr));

        hgcm_buffer = (CRNEMUHGCMBUFFER *) _crNemuHGCMAlloc( conn ) - 1;
        hgcm_buffer->len = sizeof(CRMessageRedirPtr);

        msg = (CRMessage *) (hgcm_buffer + 1);

        msg->header.type = CR_MESSAGE_REDIR_PTR;
        msg->redirptr.pMessage = (CRMessageHeader*) (conn->pBuffer);
        msg->header.conn_id = msg->redirptr.pMessage->conn_id;

#if defined(NEMU_WITH_CRHGSMI) && !defined(IN_GUEST)
        msg->redirptr.CmdData = conn->CmdData;
        CRNEMUHGSMI_CMDDATA_ASSERT_CONSISTENT(&msg->redirptr.CmdData);
        CRNEMUHGSMI_CMDDATA_CLEANUP(&conn->CmdData);
#endif

        cached_type = msg->redirptr.pMessage->type;

        conn->cbBuffer = 0;
        conn->pBuffer  = NULL;
#ifndef IN_GUEST
    }
    else
    {
        /* we should NEVER have redir_ptr disabled with HGSMI command now */
        CRASSERT(!conn->CmdData.pvCmd);
        if ( len <= conn->buffer_size )
        {
            /* put in pre-allocated buffer */
            hgcm_buffer = (CRNEMUHGCMBUFFER *) _crNemuHGCMAlloc( conn ) - 1;
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

    crNetDispatchMessage( g_crnemuhgcm.recv_list, conn, msg, len );

    /* CR_MESSAGE_OPCODES is freed in crserverlib/server_stream.c with crNetFree.
     * OOB messages are the programmer's problem.  -- Humper 12/17/01
     */
    if (cached_type != CR_MESSAGE_OPCODES
        && cached_type != CR_MESSAGE_OOB
        && cached_type != CR_MESSAGE_GATHER)
    {
        _crNemuHGCMFree(conn, msg);
    }
}

static void crNemuHGCMReceiveMessage(CRConnection *conn)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    _crNemuHGCMReceiveMessage(conn);
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}


/*
 * Called on host side only, to "accept" client connection
 */
static void crNemuHGCMAccept( CRConnection *conn, const char *hostname, unsigned short port )
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    CRASSERT(conn && conn->pHostBuffer);
#ifdef IN_GUEST
    CRASSERT(FALSE);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static int crNemuHGCMSetVersion(CRConnection *conn, unsigned int vMajor, unsigned int vMinor)
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

    rc = crNemuHGCMCall(conn, &parms, sizeof(parms));

    if (RT_SUCCESS(rc))
    {
        rc =  parms.hdr.result;
        if (RT_SUCCESS(rc))
        {
            conn->vMajor = CR_PROTOCOL_VERSION_MAJOR;
            conn->vMinor = CR_PROTOCOL_VERSION_MINOR;

            return VINF_SUCCESS;
        }
        else
            WARN(("Host doesn't accept our version %d.%d. Make sure you have appropriate additions installed!",
                  parms.vMajor.u.value32, parms.vMinor.u.value32));
    }
    else
        WARN(("crNemuHGCMCall failed %d", rc));

    return rc;
}

static int crNemuHGCMGetHostCapsLegacy(CRConnection *conn, uint32_t *pu32HostCaps)
{
    CRNEMUHGCMGETCAPS caps;
    int rc;

    caps.hdr.result      = VERR_WRONG_ORDER;
    caps.hdr.u32ClientID = conn->u32ClientID;
    caps.hdr.u32Function = SHCRGL_GUEST_FN_GET_CAPS_LEGACY;
    caps.hdr.cParms      = SHCRGL_CPARMS_GET_CAPS_LEGACY;

    caps.Caps.type       = VMMDevHGCMParmType_32bit;
    caps.Caps.u.value32  = 0;

    rc = crNemuHGCMCall(conn, &caps, sizeof(caps));

    if (RT_SUCCESS(rc))
    {
        rc =  caps.hdr.result;
        if (RT_SUCCESS(rc))
        {
            *pu32HostCaps = caps.Caps.u.value32;
            return VINF_SUCCESS;
        }
        else
            WARN(("SHCRGL_GUEST_FN_GET_CAPS failed %d", rc));
        return FALSE;
    }
    else
        WARN(("crNemuHGCMCall failed %d", rc));

    *pu32HostCaps = 0;

    return rc;
}

static int crNemuHGCMSetPID(CRConnection *conn, unsigned long long pid)
{
    CRNEMUHGCMSETPID parms;
    int rc;

    parms.hdr.result      = VERR_WRONG_ORDER;
    parms.hdr.u32ClientID = conn->u32ClientID;
    parms.hdr.u32Function = SHCRGL_GUEST_FN_SET_PID;
    parms.hdr.cParms      = SHCRGL_CPARMS_SET_PID;

    parms.u64PID.type     = VMMDevHGCMParmType_64bit;
    parms.u64PID.u.value64 = pid;

    rc = crNemuHGCMCall(conn, &parms, sizeof(parms));

    if (RT_FAILURE(rc) || RT_FAILURE(parms.hdr.result))
    {
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
static int crNemuHGCMDoConnect( CRConnection *conn )
{
#ifdef IN_GUEST
    NemuGuestHGCMConnectInfo info;

#ifdef RT_OS_WINDOWS
    DWORD cbReturned;

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    if (g_crnemuhgcm.hGuestDrv == INVALID_HANDLE_VALUE)
    {
        /* open Nemu guest driver */
        g_crnemuhgcm.hGuestDrv = CreateFile(NEMUGUEST_DEVICE_NAME,
                                        GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL,
                                        OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL,
                                        NULL);

        /* @todo check if we could rollback to softwareopengl */
        if (g_crnemuhgcm.hGuestDrv == INVALID_HANDLE_VALUE)
        {
            crWarning("could not open Nemu Guest Additions driver! rc = %d\n", GetLastError());
            NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
            return FALSE;
        }
    }
#else
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    if (g_crnemuhgcm.iGuestDrv == INVALID_HANDLE_VALUE)
    {
        g_crnemuhgcm.iGuestDrv = open(NEMUGUEST_USER_DEVICE_NAME, O_RDWR, 0);
        if (g_crnemuhgcm.iGuestDrv == INVALID_HANDLE_VALUE)
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
    if (DeviceIoControl(g_crnemuhgcm.hGuestDrv,
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
    if (ioctl(g_crnemuhgcm.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CONNECT, &Hdr) >= 0)
#else
    if (ioctl(g_crnemuhgcm.iGuestDrv, NEMUGUEST_IOCTL_HGCM_CONNECT, &info, sizeof (info)) == 0)
#endif
    {
        if (info.result == VINF_SUCCESS)
        {
            int rc;
            conn->u32ClientID = info.u32ClientID;
            crDebug("HGCM connect was successful: client id =0x%x\n", conn->u32ClientID);

            rc = crNemuHGCMSetVersion(conn, CR_PROTOCOL_VERSION_MAJOR, CR_PROTOCOL_VERSION_MINOR);
            if (RT_FAILURE(rc))
            {
                WARN(("crNemuHGCMSetVersion failed %d", rc));
                return FALSE;
            }
#ifdef RT_OS_WINDOWS
            rc = crNemuHGCMSetPID(conn, GetCurrentProcessId());
#else
            rc = crNemuHGCMSetPID(conn, crGetPID());
#endif
            if (RT_FAILURE(rc))
            {
                WARN(("crNemuHGCMSetPID failed %d", rc));
                return FALSE;
            }

            if (!g_crnemuhgcm.fHostCapsInitialized)
            {
                rc = crNemuHGCMGetHostCapsLegacy(conn, &g_crnemuhgcm.u32HostCaps);
                if (RT_FAILURE(rc))
                {
                    WARN(("NemuCrHgsmiCtlConGetHostCaps failed %d", rc));
                    g_crnemuhgcm.u32HostCaps = 0;
                }

                /* host may not support it, ignore any failures */
                g_crnemuhgcm.fHostCapsInitialized = true;
                rc = VINF_SUCCESS;
            }

            if (g_crnemuhgcm.u32HostCaps & CR_NEMU_CAP_HOST_CAPS_NOT_SUFFICIENT)
            {
                crDebug("HGCM connect: insufficient host capabilities\n");
                g_crnemuhgcm.u32HostCaps = 0;
                return FALSE;
            }

            NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
            return RT_SUCCESS(rc);
        }
        else
        {
            crDebug("HGCM connect failed with rc=0x%x\n", info.result);

            NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
            return FALSE;
        }
    }
    else
    {
#ifdef RT_OS_WINDOWS
        DWORD winEr = GetLastError();
        crDebug("IOCTL for HGCM connect failed with rc=0x%x\n", winEr);
#else
        crDebug("IOCTL for HGCM connect failed with rc=0x%x\n", errno);
#endif
        NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return FALSE;
    }

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
    return FALSE;

#else /*#ifdef IN_GUEST*/
    crError("crNemuHGCMDoConnect called on host side!");
    CRASSERT(FALSE);
    return FALSE;
#endif
}

static bool _crNemuCommonDoDisconnectLocked( CRConnection *conn )
{
    int i;
    if (conn->pHostBuffer)
    {
        crFree(conn->pHostBuffer);
        conn->pHostBuffer = NULL;
        conn->cbHostBuffer = 0;
        conn->cbHostBufferAllocated = 0;
    }

    conn->pBuffer = NULL;
    conn->cbBuffer = 0;

    if (conn->type == CR_NEMUHGCM)
    {
        --g_crnemuhgcm.num_conns;

        if (conn->index < g_crnemuhgcm.num_conns)
        {
            g_crnemuhgcm.conns[conn->index] = g_crnemuhgcm.conns[g_crnemuhgcm.num_conns];
            g_crnemuhgcm.conns[conn->index]->index = conn->index;
        }
        else g_crnemuhgcm.conns[conn->index] = NULL;

        conn->type = CR_NO_CONNECTION;
    }

    for (i = 0; i < g_crnemuhgcm.num_conns; i++)
        if (g_crnemuhgcm.conns[i] && g_crnemuhgcm.conns[i]->type != CR_NO_CONNECTION)
            return true;
    return false;
}

/*@todo same, replace DeviceIoControl with vbglR3DoIOCtl */
static void crNemuHGCMDoDisconnect( CRConnection *conn )
{
#ifdef IN_GUEST
    NemuGuestHGCMDisconnectInfo info;
# ifdef RT_OS_WINDOWS
    DWORD cbReturned;
# endif
#endif
    bool fHasActiveCons = false;

    if (!g_crnemuhgcm.initialized) return;

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    fHasActiveCons = _crNemuCommonDoDisconnectLocked(conn);

#ifndef IN_GUEST
#else /* IN_GUEST */
    if (conn->u32ClientID)
    {
        memset (&info, 0, sizeof (info));
        info.u32ClientID = conn->u32ClientID;

# ifdef RT_OS_WINDOWS
        if ( !DeviceIoControl(g_crnemuhgcm.hGuestDrv,
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
        if (ioctl(g_crnemuhgcm.iGuestDrv, NEMUGUEST_IOCTL_HGCM_DISCONNECT, &Hdr) >= 0)
# else
        if (ioctl(g_crnemuhgcm.iGuestDrv, NEMUGUEST_IOCTL_HGCM_DISCONNECT, &info, sizeof (info)) < 0)
        {
            crDebug("Disconnect failed with %x\n", errno);
        }
# endif

        conn->u32ClientID = 0;
    }

    /* close guest additions driver*/
    if (!fHasActiveCons)
    {
# ifdef RT_OS_WINDOWS
        CloseHandle(g_crnemuhgcm.hGuestDrv);
        g_crnemuhgcm.hGuestDrv = INVALID_HANDLE_VALUE;
# else
        close(g_crnemuhgcm.iGuestDrv);
        g_crnemuhgcm.iGuestDrv = INVALID_HANDLE_VALUE;
# endif
    }
#endif /* IN_GUEST */

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
}

static void crNemuHGCMInstantReclaim(CRConnection *conn, CRMessage *mess)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    _crNemuHGCMFree(conn, mess);
    CRASSERT(FALSE);
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGCMHandleNewMessage( CRConnection *conn, CRMessage *msg, unsigned int len )
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    CRASSERT(FALSE);
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)

bool _crNemuHGSMIInit()
{
#ifndef NEMU_CRHGSMI_WITH_D3DDEV
    static
#endif
    int bHasHGSMI = -1;

    if (bHasHGSMI < 0)
    {
        int rc;
#ifndef NEMU_CRHGSMI_WITH_D3DDEV
        rc = NemuCrHgsmiInit(CR_PROTOCOL_VERSION_MAJOR, CR_PROTOCOL_VERSION_MINOR);
#else
        NEMUCRHGSMI_CALLBACKS Callbacks;
        Callbacks.pfnClientCreate = _crNemuHGSMIClientCreate;
        Callbacks.pfnClientDestroy = _crNemuHGSMIClientDestroy;
        rc = NemuCrHgsmiInit(&Callbacks);
#endif
        if (RT_SUCCESS(rc))
            bHasHGSMI = 1;
        else
            bHasHGSMI = 0;

        crDebug("CrHgsmi is %s", bHasHGSMI ? "ENABLED" : "DISABLED");
    }

    CRASSERT(bHasHGSMI >= 0);

    return bHasHGSMI;
}

void _crNemuHGSMITearDown()
{
    NemuCrHgsmiTerm();
}

static void *_crNemuHGSMIDoAlloc(CRConnection *conn, PCRNEMUHGSMI_CLIENT pClient)
{
    PNEMUUHGSMI_BUFFER buf;
    CRNEMUHGCMBUFFER *pData = NULL;
    uint32_t cbSize = conn->buffer_size;
    int rc;

    buf = _crNemuHGSMIBufAlloc(pClient, CRNEMUHGSMI_BUF_SIZE(cbSize));
    if (buf)
    {
        NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags;
        buf->pvUserData = pClient;
        fFlags.Value = 0;
        fFlags.bDiscard = 1;
        rc = buf->pfnLock(buf, 0, CRNEMUHGSMI_BUF_LOCK_SIZE(cbSize), fFlags, (void**)&pData);
        if (RT_SUCCESS(rc))
        {
            pData->magic = CR_NEMUHGCM_BUFFER_MAGIC;
            pData->kind = CR_NEMUHGCM_UHGSMI_BUFFER;
            pData->pBuffer = buf;
        }
        else
        {
            crWarning("Failed to Lock the buffer, rc(%d)\n", rc);
        }
        return CRNEMUHGSMI_BUF_DATA(pData);
    }
    else
    {
        crWarning("_crNemuHGSMIBufAlloc failed to allocate buffer of size (%d)", CRNEMUHGSMI_BUF_SIZE(cbSize));
    }

    /* fall back */
    return _crNemuHGCMAlloc(conn);
}

static void _crNemuHGSMIFree(CRConnection *conn, void *buf)
{
    CRNEMUHGCMBUFFER *hgcm_buffer = (CRNEMUHGCMBUFFER *) buf - 1;

    CRASSERT(hgcm_buffer->magic == CR_NEMUHGCM_BUFFER_MAGIC);

    if (hgcm_buffer->kind == CR_NEMUHGCM_UHGSMI_BUFFER)
    {
        PNEMUUHGSMI_BUFFER pBuf = _crNemuHGSMIBufFromHdr(hgcm_buffer);
        PCRNEMUHGSMI_CLIENT pClient = (PCRNEMUHGSMI_CLIENT)pBuf->pvUserData;
        _crNemuHGSMIBufFree(pClient, pBuf);
    }
    else
    {
        _crNemuHGCMFree(conn, buf);
    }
}

static void *crNemuHGSMIAlloc(CRConnection *conn)
{
    PCRNEMUHGSMI_CLIENT pClient;
    void *pvBuf;

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    pClient = _crNemuHGSMIClientGet(conn);
    if (pClient)
    {
        pvBuf = _crNemuHGSMIDoAlloc(conn, pClient);
        CRASSERT(pvBuf);
    }
    else
    {
        pvBuf = _crNemuHGCMAlloc(conn);
    }

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();

    return pvBuf;
}

static void crNemuHGSMIFree(CRConnection *conn, void *buf)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    _crNemuHGSMIFree(conn, buf);
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void _crNemuHGSMIPollHost(CRConnection *conn, PCRNEMUHGSMI_CLIENT pClient)
{
    CRNEMUHGSMIREAD *parms = (CRNEMUHGSMIREAD *)_crNemuHGSMICmdBufferLock(pClient, sizeof (*parms));
    int rc;
    NEMUUHGSMI_BUFFER_SUBMIT aSubmit[2];
    PNEMUUHGSMI_BUFFER pRecvBuffer;
    uint32_t cbBuffer;

    CRASSERT(parms);

    parms->hdr.result      = VERR_WRONG_ORDER;
    parms->hdr.u32ClientID = conn->u32ClientID;
    parms->hdr.u32Function = SHCRGL_GUEST_FN_READ;
//    parms->hdr.u32Reserved = 0;

    CRASSERT(!conn->pBuffer); //make sure there's no data to process
    parms->iBuffer = 1;
    parms->cbBuffer = 0;

    _crNemuHGSMICmdBufferUnlock(pClient);

    pRecvBuffer = _crNemuHGSMIRecvBufGet(pClient);
    CRASSERT(pRecvBuffer);
    if (!pRecvBuffer)
        return;

    _crNemuHGSMIFillCmd(&aSubmit[0], pClient, sizeof (*parms));

    aSubmit[1].pBuf = pRecvBuffer;
    aSubmit[1].offData = 0;
    aSubmit[1].cbData = pRecvBuffer->cbBuffer;
    aSubmit[1].fFlags.Value = 0;
    aSubmit[1].fFlags.bHostWriteOnly = 1;

    rc = pClient->pHgsmi->pfnBufferSubmit(pClient->pHgsmi, aSubmit, 2);
    if (RT_FAILURE(rc))
    {
        crError("pfnBufferSubmit failed with %d \n", rc);
        return;
    }

    parms = (CRNEMUHGSMIREAD *)_crNemuHGSMICmdBufferLockRo(pClient, sizeof (*parms));
    CRASSERT(parms);
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
        CRASSERT(pvData);
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
        _crNemuHGCMReceiveMessage(conn);
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

        if (!pBuf)
        {
            /* fallback */
            crNemuHGCMWriteReadExact(conn, buf, len, CR_NEMUHGCM_USERALLOCATED);
            return;
        }

        CRASSERT(!offBuffer);

        offBuffer = 0;
        fFlags.Value = 0;
        fFlags.bDiscard = 1;
        fFlags.bWriteOnly = 1;
        rc = pBuf->pfnLock(pBuf, 0, len, fFlags, &pvBuf);
        if (RT_SUCCESS(rc))
        {
            memcpy(pvBuf, buf, len);
            rc = pBuf->pfnUnlock(pBuf);
            CRASSERT(RT_SUCCESS(rc));
        }
        else
        {
            crWarning("_crNemuHGSMIWriteReadExact: pfnUnlock failed rc %d", rc);
            _crNemuHGSMIBufFree(pClient, pBuf);
            /* fallback */
            crNemuHGCMWriteReadExact(conn, buf, len, CR_NEMUHGCM_USERALLOCATED);
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
        CRASSERT(pRecvBuffer);
        if (!pRecvBuffer)
        {
            break;
        }

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

        rc = pClient->pHgsmi->pfnBufferSubmit(pClient->pHgsmi, aSubmit, 3);
        if (RT_FAILURE(rc))
        {
            crError("pfnBufferSubmit failed with %d \n", rc);
            break;
        }

        parms = (CRNEMUHGSMIWRITEREAD *)_crNemuHGSMICmdBufferLockRo(pClient, sizeof (*parms));
        CRASSERT(parms);
        if (parms)
        {
            uint32_t cbWriteback = parms->cbWriteback;
            rc = parms->hdr.result;
#ifdef DEBUG_misha
            /* catch it here to test the buffer */
            Assert(RT_SUCCESS(parms->hdr.result) || parms->hdr.result == VERR_BUFFER_OVERFLOW);
#endif
            _crNemuHGSMICmdBufferUnlock(pClient);
#ifdef DEBUG
            parms = NULL;
#endif
            if (RT_SUCCESS(rc))
            {
                if (cbWriteback)
                {
                    void *pvData = _crNemuHGSMIRecvBufData(pClient, cbWriteback);
                    CRASSERT(pvData);
                    if (pvData)
                    {
                        conn->pBuffer  = pvData;
                        conn->cbBuffer = cbWriteback;
                        _crNemuHGCMReceiveMessage(conn);
                    }
                }
            }
            else if (VERR_BUFFER_OVERFLOW == rc)
            {
                NEMUUHGSMI_BUFFER_TYPE_FLAGS Flags = {0};
                PNEMUUHGSMI_BUFFER pNewBuf;
                CRASSERT(!pClient->pvHGBuffer);
                CRASSERT(cbWriteback>pClient->pHGBuffer->cbBuffer);
                crDebug("Reallocating host buffer from %d to %d bytes", conn->cbHostBufferAllocated, cbWriteback);

                rc = pClient->pHgsmi->pfnBufferCreate(pClient->pHgsmi, CRNEMUHGSMI_PAGE_ALIGN(cbWriteback), Flags, &pNewBuf);
                if (RT_SUCCESS(rc))
                {
                    rc = pClient->pHGBuffer->pfnDestroy(pClient->pHGBuffer);
                    CRASSERT(RT_SUCCESS(rc));

                    pClient->pHGBuffer = pNewBuf;

                    _crNemuHGSMIReadExact(conn, pClient/*, cbWriteback*/);
                }
                else
                {
                    crWarning("_crNemuHGSMIWriteReadExact: pfnBufferCreate(%d) failed!", CRNEMUHGSMI_PAGE_ALIGN(cbWriteback));
                    if (conn->cbHostBufferAllocated < cbWriteback)
                    {
                        crFree(conn->pHostBuffer);
                        conn->cbHostBufferAllocated = cbWriteback;
                        conn->pHostBuffer = crAlloc(conn->cbHostBufferAllocated);
                    }
                    crNemuHGCMReadExact(conn, NULL, cbWriteback);
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

    return;
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
        CRASSERT(parms);
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

        rc = pClient->pHgsmi->pfnBufferSubmit(pClient->pHgsmi, aSubmit, 2);
        if (RT_SUCCESS(rc))
        {
            callRes = _crNemuHGSMICmdBufferGetRc(pClient);
        }
        else
        {
            /* we can not recover at this point, report error & exit */
            crError("pfnBufferSubmit failed with %d \n", rc);
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

        rc = pClient->pHgsmi->pfnBufferSubmit(pClient->pHgsmi, aSubmit, 2);
        if (RT_SUCCESS(rc))
        {
            callRes = _crNemuHGSMICmdBufferGetRc(pClient);
        }
        else
        {
            /* we can not recover at this point, report error & exit */
            crError("Failed to submit CrHhgsmi buffer");
        }
    }

    if (RT_FAILURE(rc) || RT_FAILURE(callRes))
    {
        crWarning("SHCRGL_GUEST_FN_WRITE failed with %x %x\n", rc, callRes);
    }
}

static void crNemuHGSMISend(CRConnection *conn, void **bufp,
                           const void *start, unsigned int len)
{
    PCRNEMUHGSMI_CLIENT pClient;
    PNEMUUHGSMI_BUFFER pBuf;
    CRNEMUHGCMBUFFER *hgcm_buffer;

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    if (!bufp) /* We're sending a user-allocated buffer. */
    {
        pClient = _crNemuHGSMIClientGet(conn);
        if (pClient)
        {
#ifndef IN_GUEST
                //@todo remove temp buffer allocation in unpacker
                /* we're at the host side, so just store data until guest polls us */
                _crNemuHGCMWriteBytes(conn, start, len);
#else
            CRASSERT(!conn->u32InjectClientID);
            crDebug("SHCRGL: sending userbuf with %d bytes\n", len);
            _crNemuHGSMIWriteReadExact(conn, pClient, (void*)start, 0, len, false);
#endif
#ifdef CHROMIUM_THREADSAFE
            crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
            NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
            return;
        }

        /* fallback */
        crNemuHGCMSend(conn, bufp, start, len);
#ifdef CHROMIUM_THREADSAFE
        crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
        NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return;
    }

    hgcm_buffer = (CRNEMUHGCMBUFFER *) *bufp - 1;
    CRASSERT(hgcm_buffer->magic == CR_NEMUHGCM_BUFFER_MAGIC);
    if (hgcm_buffer->magic != CR_NEMUHGCM_BUFFER_MAGIC)
    {
        crError("HGCM buffer magic mismatch");
    }


    if (hgcm_buffer->kind != CR_NEMUHGCM_UHGSMI_BUFFER)
    {
        /* fallback */
        crNemuHGCMSend(conn, bufp, start, len);
#ifdef CHROMIUM_THREADSAFE
        crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
        NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return;
    }

    /* The region [start .. start + len + 1] lies within a buffer that
     * was allocated with crNemuHGCMAlloc() and can be put into the free
     * buffer pool when we're done sending it.
     */

    pBuf = _crNemuHGSMIBufFromHdr(hgcm_buffer);
    CRASSERT(pBuf);
    if (!pBuf)
    {
        crNemuHGCMSend(conn, bufp, start, len);
#ifdef CHROMIUM_THREADSAFE
        crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
        NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return;
    }

    pClient = (PCRNEMUHGSMI_CLIENT)pBuf->pvUserData;
    if (pClient != &conn->HgsmiClient)
    {
        crError("HGSMI client mismatch");
    }

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

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGSMIWriteExact(CRConnection *conn, const void *buf, unsigned int len)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    CRASSERT(0);

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGSMISingleRecv(CRConnection *conn, void *buf, unsigned int len)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    CRASSERT(0);

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGSMIReceiveMessage(CRConnection *conn)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    CRASSERT(0);

    _crNemuHGCMReceiveMessage(conn);

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

/*
 * Called on host side only, to "accept" client connection
 */
static void crNemuHGSMIAccept( CRConnection *conn, const char *hostname, unsigned short port )
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    CRASSERT(0);

    CRASSERT(conn && conn->pHostBuffer);
#ifdef IN_GUEST
    CRASSERT(FALSE);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static int crNemuHGSMIDoConnect( CRConnection *conn )
{
    PCRNEMUHGSMI_CLIENT pClient;
    int rc = VINF_SUCCESS;

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

    pClient = _crNemuHGSMIClientGet(conn);
    if (pClient)
    {
        rc = NemuCrHgsmiCtlConGetClientID(pClient->pHgsmi, &conn->u32ClientID);
        if (RT_FAILURE(rc))
        {
            WARN(("NemuCrHgsmiCtlConGetClientID failed %d", rc));
        }
        if (!g_crnemuhgcm.fHostCapsInitialized)
        {
            rc = NemuCrHgsmiCtlConGetHostCaps(pClient->pHgsmi, &g_crnemuhgcm.u32HostCaps);
            if (RT_SUCCESS(rc))
            {
                g_crnemuhgcm.fHostCapsInitialized = true;
            }
            else
            {
                WARN(("NemuCrHgsmiCtlConGetHostCaps failed %d", rc));
                g_crnemuhgcm.u32HostCaps = 0;
            }
        }
    }
    else
    {
        WARN(("_crNemuHGSMIClientGet failed %d", rc));
        rc = VERR_GENERAL_FAILURE;
    }

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    return RT_SUCCESS(rc);
}

static void crNemuHGSMIDoDisconnect( CRConnection *conn )
{
    bool fHasActiveCons = false;

    if (!g_crnemuhgcm.initialized) return;

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    fHasActiveCons = _crNemuCommonDoDisconnectLocked(conn);

#ifndef NEMU_CRHGSMI_WITH_D3DDEV
    if (conn->HgsmiClient.pHgsmi)
    {
        PNEMUUHGSMI pHgsmi;
        _crNemuHGSMIClientTerm(&conn->HgsmiClient, &pHgsmi);
        CRASSERT(pHgsmi);
        if (!conn->pExternalHgsmi)
            NemuCrHgsmiDestroy(pHgsmi);
    }
#else
# error "port me!"
#endif

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
}

static void crNemuHGSMIInstantReclaim(CRConnection *conn, CRMessage *mess)
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    CRASSERT(0);

    _crNemuHGSMIFree(conn, mess);

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}

static void crNemuHGSMIHandleNewMessage( CRConnection *conn, CRMessage *msg, unsigned int len )
{
    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();
    CRASSERT(0);

    CRASSERT(FALSE);
    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
}
#endif

void crNemuHGCMInit(CRNetReceiveFuncList *rfl, CRNetCloseFuncList *cfl, unsigned int mtu)
{
    (void) mtu;

    g_crnemuhgcm.recv_list = rfl;
    g_crnemuhgcm.close_list = cfl;
    if (g_crnemuhgcm.initialized)
    {
        return;
    }

    NEMUCRHGSMIPROFILE_INIT();

    g_crnemuhgcm.initialized = 1;

#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
    g_crnemuhgcm.bHgsmiOn = _crNemuHGSMIInit();
#endif

    g_crnemuhgcm.num_conns = 0;
    g_crnemuhgcm.conns     = NULL;

    /* Can't open Nemu guest driver here, because it gets called for host side as well */
    /*@todo as we have 2 dll versions, can do it now.*/

#ifdef RT_OS_WINDOWS
    g_crnemuhgcm.hGuestDrv = INVALID_HANDLE_VALUE;
    g_crnemuhgcm.pDirectDraw = NULL;
#else
    g_crnemuhgcm.iGuestDrv = INVALID_HANDLE_VALUE;
#endif

#ifdef CHROMIUM_THREADSAFE
    crInitMutex(&g_crnemuhgcm.mutex);
    crInitMutex(&g_crnemuhgcm.recvmutex);
#endif
    g_crnemuhgcm.bufpool = crBufferPoolInit(16);

#ifdef IN_GUEST
    g_crnemuhgcm.fHostCapsInitialized = false;
    g_crnemuhgcm.u32HostCaps = 0;
#endif
}

/* Callback function used to free buffer pool entries */
void crNemuHGCMBufferFree(void *data)
{
    CRNEMUHGCMBUFFER *hgcm_buffer = (CRNEMUHGCMBUFFER *) data;

    CRASSERT(hgcm_buffer->magic == CR_NEMUHGCM_BUFFER_MAGIC);

    switch (hgcm_buffer->kind)
    {
        case CR_NEMUHGCM_MEMORY:
            crDebug("crNemuHGCMBufferFree: CR_NEMUHGCM_MEMORY: %p", hgcm_buffer);
            crFree( hgcm_buffer );
            break;
        case CR_NEMUHGCM_MEMORY_BIG:
            crDebug("crNemuHGCMBufferFree: CR_NEMUHGCM_MEMORY_BIG: %p", hgcm_buffer);
            crFree( hgcm_buffer );
            break;

        default:
            crError( "Weird buffer kind trying to free in crNemuHGCMBufferFree: %d", hgcm_buffer->kind );
    }
}

void crNemuHGCMTearDown(void)
{
    int32_t i, cCons;

    if (!g_crnemuhgcm.initialized) return;

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

    /* Connection count would be changed in calls to crNetDisconnect, so we have to store original value.
     * Walking array backwards is not a good idea as it could cause some issues if we'd disconnect clients not in the
     * order of their connection.
     */
    cCons = g_crnemuhgcm.num_conns;
    for (i=0; i<cCons; i++)
    {
        /* Note that [0] is intended, as the connections array would be shifted in each call to crNetDisconnect */
        crNetDisconnect(g_crnemuhgcm.conns[0]);
    }
    CRASSERT(0==g_crnemuhgcm.num_conns);

    g_crnemuhgcm.initialized = 0;

    if (g_crnemuhgcm.bufpool)
        crBufferPoolCallbackFree(g_crnemuhgcm.bufpool, crNemuHGCMBufferFree);
    g_crnemuhgcm.bufpool = NULL;

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
    crFreeMutex(&g_crnemuhgcm.mutex);
    crFreeMutex(&g_crnemuhgcm.recvmutex);
#endif

    crFree(g_crnemuhgcm.conns);
    g_crnemuhgcm.conns = NULL;

#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
    if (g_crnemuhgcm.bHgsmiOn)
    {
        _crNemuHGSMITearDown();
    }
#endif

#ifdef RT_OS_WINDOWS
    if (g_crnemuhgcm.pDirectDraw)
    {
        IDirectDraw_Release(g_crnemuhgcm.pDirectDraw);
        g_crnemuhgcm.pDirectDraw = NULL;
        crDebug("DirectDraw released\n");
    }
#endif
}

void crNemuHGCMConnection(CRConnection *conn
#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
        , struct NEMUUHGSMI *pHgsmi
#endif
        )
{
    int i, found = 0;
    int n_bytes;

    CRASSERT(g_crnemuhgcm.initialized);

#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
    if (g_crnemuhgcm.bHgsmiOn)
    {
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
        conn->pExternalHgsmi = pHgsmi;
    }
    else
#endif
    {
        conn->type = CR_NEMUHGCM;
        conn->Alloc = crNemuHGCMAlloc;
        conn->Send = crNemuHGCMSend;
        conn->SendExact = crNemuHGCMWriteExact;
        conn->Recv = crNemuHGCMSingleRecv;
        conn->RecvMsg = crNemuHGCMReceiveMessage;
        conn->Free = crNemuHGCMFree;
        conn->Accept = crNemuHGCMAccept;
        conn->Connect = crNemuHGCMDoConnect;
        conn->Disconnect = crNemuHGCMDoDisconnect;
        conn->InstantReclaim = crNemuHGCMInstantReclaim;
        conn->HandleNewMessage = crNemuHGCMHandleNewMessage;
    }
    conn->sizeof_buffer_header = sizeof(CRNEMUHGCMBUFFER);
    conn->actual_network = 1;

    conn->krecv_buf_size = 0;

    conn->pBuffer = NULL;
    conn->cbBuffer = 0;
    conn->allow_redir_ptr = 1;

    //@todo remove this crap at all later
    conn->cbHostBufferAllocated = 2*1024;
    conn->pHostBuffer = (uint8_t*) crAlloc(conn->cbHostBufferAllocated);
    CRASSERT(conn->pHostBuffer);
    conn->cbHostBuffer = 0;

#if !defined(IN_GUEST)
    RTListInit(&conn->PendingMsgList);
#endif

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif
    /* Find a free slot */
    for (i = 0; i < g_crnemuhgcm.num_conns; i++) {
        if (g_crnemuhgcm.conns[i] == NULL) {
            conn->index = i;
            g_crnemuhgcm.conns[i] = conn;
            found = 1;
            break;
        }
    }

    /* Realloc connection stack if we couldn't find a free slot */
    if (found == 0) {
        n_bytes = ( g_crnemuhgcm.num_conns + 1 ) * sizeof(*g_crnemuhgcm.conns);
        crRealloc( (void **) &g_crnemuhgcm.conns, n_bytes );
        conn->index = g_crnemuhgcm.num_conns;
        g_crnemuhgcm.conns[g_crnemuhgcm.num_conns++] = conn;
    }
#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif
}

#if defined(IN_GUEST)
void _crNemuHGCMPerformPollHost(CRConnection *conn)
{
    if (conn->type == CR_NO_CONNECTION )
        return;

    if (!conn->pBuffer)
    {
#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
        PCRNEMUHGSMI_CLIENT pClient;
        if (g_crnemuhgcm.bHgsmiOn && !!(pClient = _crNemuHGSMIClientGet(conn)))
        {
            _crNemuHGSMIPollHost(conn, pClient);
        }
        else
#endif
        {
            crNemuHGCMPollHost(conn);
        }
    }
}
#endif

void _crNemuHGCMPerformReceiveMessage(CRConnection *conn)
{
    if ( conn->type == CR_NO_CONNECTION )
        return;

    if (conn->cbBuffer>0)
    {
        _crNemuHGCMReceiveMessage(conn);
    }
}

#ifdef IN_GUEST
uint32_t crNemuHGCMHostCapsGet()
{
    Assert(g_crnemuhgcm.fHostCapsInitialized);
    return g_crnemuhgcm.u32HostCaps;
}
#endif

int crNemuHGCMRecv(
#if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
        CRConnection *conn
#endif
        )
{
    int32_t i;

    NEMUCRHGSMIPROFILE_FUNC_PROLOGUE();

#ifdef CHROMIUM_THREADSAFE
    crLockMutex(&g_crnemuhgcm.mutex);
#endif

#ifdef IN_GUEST
# if defined(NEMU_WITH_CRHGSMI) && defined(IN_GUEST)
    if (conn && g_crnemuhgcm.bHgsmiOn)
    {
        _crNemuHGCMPerformPollHost(conn);
        _crNemuHGCMPerformReceiveMessage(conn);
        NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();
        return 0;
    }
# endif
    /* we're on guest side, poll host if it got something for us */
    for (i=0; i<g_crnemuhgcm.num_conns; i++)
    {
        CRConnection *conn = g_crnemuhgcm.conns[i];

        if ( !conn  )
            continue;

        _crNemuHGCMPerformPollHost(conn);
    }
#endif

    for (i=0; i<g_crnemuhgcm.num_conns; i++)
    {
        CRConnection *conn = g_crnemuhgcm.conns[i];

        if ( !conn )
            continue;

        _crNemuHGCMPerformReceiveMessage(conn);
    }

#ifdef CHROMIUM_THREADSAFE
    crUnlockMutex(&g_crnemuhgcm.mutex);
#endif

    NEMUCRHGSMIPROFILE_FUNC_EPILOGUE();

    return 0;
}

CRConnection** crNemuHGCMDump( int *num )
{
    *num = g_crnemuhgcm.num_conns;

    return g_crnemuhgcm.conns;
}
