/* $Id: NemuVideoPortAPI.h $ */

/** @file
 * Nemu video port functions header
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

#ifndef NEMUVIDEOPORTAPI_H
#define NEMUVIDEOPORTAPI_H

/* To maintain binary backward compatibility with older windows versions
 * we query at runtime for video port functions which are not present in NT 4.0
 * Those could used in the display driver also.
 */

/*Basic datatypes*/
typedef long NEMUVP_STATUS;
#ifndef NEMU_USING_W2K3DDK
typedef struct _ENG_EVENT *NEMUPEVENT;
#else
typedef struct _VIDEO_PORT_EVENT *NEMUPEVENT;
#endif
typedef struct _VIDEO_PORT_SPIN_LOCK *NEMUPSPIN_LOCK;
typedef union _LARGE_INTEGER *NEMUPLARGE_INTEGER;

typedef enum NEMUVP_POOL_TYPE
{
    NemuVpNonPagedPool,
    NemuVpPagedPool,
    NemuVpNonPagedPoolCacheAligned = 4,
    NemuVpPagedPoolCacheAligned
} NEMUVP_POOL_TYPE;

#define NEMUNOTIFICATION_EVENT 0x00000001UL
#define NEMUNO_ERROR           0x00000000UL

/*VideoPort API functions*/
typedef NEMUVP_STATUS (*PFNWAITFORSINGLEOBJECT) (void*  HwDeviceExtension, void*  Object, NEMUPLARGE_INTEGER  Timeout);
typedef long (*PFNSETEVENT) (void* HwDeviceExtension, NEMUPEVENT  pEvent);
typedef void (*PFNCLEAREVENT) (void*  HwDeviceExtension, NEMUPEVENT  pEvent);
typedef NEMUVP_STATUS (*PFNCREATEEVENT) (void*  HwDeviceExtension, unsigned long  EventFlag, void*  Unused, NEMUPEVENT  *ppEvent);
typedef NEMUVP_STATUS (*PFNDELETEEVENT) (void*  HwDeviceExtension, NEMUPEVENT  pEvent);
typedef void* (*PFNALLOCATEPOOL) (void*  HwDeviceExtension, NEMUVP_POOL_TYPE PoolType, size_t NumberOfBytes, unsigned long Tag);
typedef void (*PFNFREEPOOL) (void*  HwDeviceExtension, void*  Ptr);
typedef unsigned char (*PFNQUEUEDPC) (void* HwDeviceExtension, void (*CallbackRoutine)(void* HwDeviceExtension, void *Context), void *Context);
typedef NEMUVP_STATUS (*PFNCREATESECONDARYDISPLAY)(void* HwDeviceExtension, void* SecondaryDeviceExtension, unsigned long ulFlag);

/* pfn*Event and pfnWaitForSingleObject functions are available */
#define NEMUVIDEOPORTPROCS_EVENT    0x00000002
/* pfn*Pool functions are available */
#define NEMUVIDEOPORTPROCS_POOL     0x00000004
/* pfnQueueDpc function is available */
#define NEMUVIDEOPORTPROCS_DPC      0x00000008
/* pfnCreateSecondaryDisplay function is available */
#define NEMUVIDEOPORTPROCS_CSD      0x00000010

typedef struct NEMUVIDEOPORTPROCS
{
    /* ored NEMUVIDEOPORTPROCS_xxx constants describing the supported functionality */
    uint32_t fSupportedTypes;

    PFNWAITFORSINGLEOBJECT pfnWaitForSingleObject;

    PFNSETEVENT pfnSetEvent;
    PFNCLEAREVENT pfnClearEvent;
    PFNCREATEEVENT pfnCreateEvent;
    PFNDELETEEVENT pfnDeleteEvent;

    PFNALLOCATEPOOL pfnAllocatePool;
    PFNFREEPOOL pfnFreePool;

    PFNQUEUEDPC pfnQueueDpc;

    PFNCREATESECONDARYDISPLAY pfnCreateSecondaryDisplay;
} NEMUVIDEOPORTPROCS;

#endif /*NEMUVIDEOPORTAPI_H*/
