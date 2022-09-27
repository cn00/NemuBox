/** @file
 * Document me, pretty please.
 */

/*
 * Copyright (C) 2010-2015 Oracle Corporation
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

#ifndef ___Nemu_NemuUhgsmi_h
#define ___Nemu_NemuUhgsmi_h

#include <iprt/cdefs.h>
#include <iprt/types.h>

typedef struct NEMUUHGSMI *PNEMUUHGSMI;

typedef struct NEMUUHGSMI_BUFFER *PNEMUUHGSMI_BUFFER;

typedef struct NEMUUHGSMI_BUFFER_TYPE_FLAGS
{
    union
    {
        struct
        {
            uint32_t fCommand    : 1;
            uint32_t Reserved    : 31;
        };
        uint32_t Value;
    };
} NEMUUHGSMI_BUFFER_TYPE_FLAGS;

typedef struct NEMUUHGSMI_BUFFER_LOCK_FLAGS
{
    union
    {
        struct
        {
            uint32_t bReadOnly   : 1;
            uint32_t bWriteOnly  : 1;
            uint32_t bDonotWait  : 1;
            uint32_t bDiscard    : 1;
            uint32_t bLockEntire : 1;
            uint32_t Reserved    : 27;
        };
        uint32_t Value;
    };
} NEMUUHGSMI_BUFFER_LOCK_FLAGS;

typedef struct NEMUUHGSMI_BUFFER_SUBMIT_FLAGS
{
    union
    {
        struct
        {
            uint32_t bHostReadOnly          : 1;
            uint32_t bHostWriteOnly         : 1;
            uint32_t bDoNotRetire           : 1; /**< the buffer will be used in a subsequent command */
            uint32_t bEntireBuffer          : 1;
            uint32_t Reserved               : 28;
        };
        uint32_t Value;
    };
} NEMUUHGSMI_BUFFER_SUBMIT_FLAGS, *PNEMUUHGSMI_BUFFER_SUBMIT_FLAGS;

/* the caller can specify NULL as a hSynch and specify a valid enmSynchType to make UHGSMI create a proper object itself,
 *  */
typedef DECLCALLBACK(int) FNNEMUUHGSMI_BUFFER_CREATE(PNEMUUHGSMI pHgsmi, uint32_t cbBuf, NEMUUHGSMI_BUFFER_TYPE_FLAGS fType, PNEMUUHGSMI_BUFFER* ppBuf);
typedef FNNEMUUHGSMI_BUFFER_CREATE *PFNNEMUUHGSMI_BUFFER_CREATE;

typedef struct NEMUUHGSMI_BUFFER_SUBMIT
{
    PNEMUUHGSMI_BUFFER pBuf;
    uint32_t offData;
    uint32_t cbData;
    NEMUUHGSMI_BUFFER_SUBMIT_FLAGS fFlags;
} NEMUUHGSMI_BUFFER_SUBMIT, *PNEMUUHGSMI_BUFFER_SUBMIT;

typedef DECLCALLBACK(int) FNNEMUUHGSMI_BUFFER_SUBMIT(PNEMUUHGSMI pHgsmi, PNEMUUHGSMI_BUFFER_SUBMIT aBuffers, uint32_t cBuffers);
typedef FNNEMUUHGSMI_BUFFER_SUBMIT *PFNNEMUUHGSMI_BUFFER_SUBMIT;

typedef DECLCALLBACK(int) FNNEMUUHGSMI_BUFFER_DESTROY(PNEMUUHGSMI_BUFFER pBuf);
typedef FNNEMUUHGSMI_BUFFER_DESTROY *PFNNEMUUHGSMI_BUFFER_DESTROY;

typedef DECLCALLBACK(int) FNNEMUUHGSMI_BUFFER_LOCK(PNEMUUHGSMI_BUFFER pBuf, uint32_t offLock, uint32_t cbLock, NEMUUHGSMI_BUFFER_LOCK_FLAGS fFlags, void**pvLock);
typedef FNNEMUUHGSMI_BUFFER_LOCK *PFNNEMUUHGSMI_BUFFER_LOCK;

typedef DECLCALLBACK(int) FNNEMUUHGSMI_BUFFER_UNLOCK(PNEMUUHGSMI_BUFFER pBuf);
typedef FNNEMUUHGSMI_BUFFER_UNLOCK *PFNNEMUUHGSMI_BUFFER_UNLOCK;

typedef struct NEMUUHGSMI
{
    PFNNEMUUHGSMI_BUFFER_CREATE pfnBufferCreate;
    PFNNEMUUHGSMI_BUFFER_SUBMIT pfnBufferSubmit;
    /** User custom data. */
    void *pvUserData;
} NEMUUHGSMI;

typedef struct NEMUUHGSMI_BUFFER
{
    PFNNEMUUHGSMI_BUFFER_LOCK pfnLock;
    PFNNEMUUHGSMI_BUFFER_UNLOCK pfnUnlock;
    PFNNEMUUHGSMI_BUFFER_DESTROY pfnDestroy;

    /* r/o data added for ease of access and simplicity
     * modifying it leads to unpredictable behavior */
    NEMUUHGSMI_BUFFER_TYPE_FLAGS fType;
    uint32_t cbBuffer;
    /** User custom data. */
    void *pvUserData;
} NEMUUHGSMI_BUFFER;

#define NemuUhgsmiBufferCreate(_pUhgsmi, _cbBuf, _fType, _ppBuf) ((_pUhgsmi)->pfnBufferCreate(_pUhgsmi, _cbBuf, _fType, _ppBuf))
#define NemuUhgsmiBufferSubmit(_pUhgsmi, _aBuffers, _cBuffers) ((_pUhgsmi)->pfnBufferSubmit(_pUhgsmi, _aBuffers, _cBuffers))

#define NemuUhgsmiBufferLock(_pBuf, _offLock, _cbLock, _fFlags, _pvLock) ((_pBuf)->pfnLock(_pBuf, _offLock, _cbLock, _fFlags, _pvLock))
#define NemuUhgsmiBufferUnlock(_pBuf) ((_pBuf)->pfnUnlock(_pBuf))
#define NemuUhgsmiBufferDestroy(_pBuf) ((_pBuf)->pfnDestroy(_pBuf))

#endif

