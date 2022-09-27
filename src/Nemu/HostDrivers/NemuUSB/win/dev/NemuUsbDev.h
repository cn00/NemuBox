/* $Id: NemuUsbDev.h $ */
/** @file
 * NemuUsbDev.h - USB device.
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
#ifndef ___NemuUsbDev_h___
#define ___NemuUsbDev_h___
#include "NemuUsbCmn.h"
#include <iprt/assert.h>

typedef struct NEMUUSB_GLOBALS
{
    PDRIVER_OBJECT pDrvObj;
    UNICODE_STRING RegPath;
    NEMUUSBRT_IDC RtIdc;
} NEMUUSB_GLOBALS, *PNEMUUSB_GLOBALS;

extern NEMUUSB_GLOBALS g_NemuUsbGlobals;

/* pnp state decls */
typedef enum
{
    ENMNEMUUSB_PNPSTATE_UNKNOWN = 0,
    ENMNEMUUSB_PNPSTATE_START_PENDING,
    ENMNEMUUSB_PNPSTATE_STARTED,
    ENMNEMUUSB_PNPSTATE_STOP_PENDING,
    ENMNEMUUSB_PNPSTATE_STOPPED,
    ENMNEMUUSB_PNPSTATE_SURPRISE_REMOVED,
    ENMNEMUUSB_PNPSTATE_REMOVE_PENDING,
    ENMNEMUUSB_PNPSTATE_REMOVED,
    ENMNEMUUSB_PNPSTATE_FORSEDWORD = 0x8fffffff
} ENMNEMUUSB_PNPSTATE;
AssertCompile(sizeof (ENMNEMUUSB_PNPSTATE) == sizeof (uint32_t));

#ifdef DEBUG
DECLHIDDEN(VOID) nemuUsbPnPStateGbgChange(ENMNEMUUSB_PNPSTATE enmOld, ENMNEMUUSB_PNPSTATE enmNew);
# define NEMUUSB_PNP_GBG_STATE_CHANGE(_old, _new) nemuUsbPnPStateGbgChange((_old), (_new))
#else
# define NEMUUSB_PNP_GBG_STATE_CHANGE(_old, _new) do { } while (0)
#endif


typedef struct NEMUUSB_PNPSTATE
{
    /* Current state */
    volatile ENMNEMUUSB_PNPSTATE Curr;
    /* Previous state, used to restore state info on cancell stop device */
    ENMNEMUUSB_PNPSTATE Prev;
} NEMUUSB_PNPSTATE, *PNEMUUSB_PNPSTATE;

typedef struct NEMUUSBDEV_DDISTATE
{
    /* Lock */
    KSPIN_LOCK Lock;
    NEMUDRVTOOL_REF Ref;
    NEMUUSB_PNPSTATE PnPState;
    NEMUUSB_PWRSTATE PwrState;
    /* current dev caps */
    DEVICE_CAPABILITIES DevCaps;
} NEMUUSBDEV_DDISTATE, *PNEMUUSBDEV_DDISTATE;

typedef struct NEMUUSBDEV_EXT
{
    PDEVICE_OBJECT pFDO;
    PDEVICE_OBJECT pPDO;
    PDEVICE_OBJECT pLowerDO;

    NEMUUSBDEV_DDISTATE DdiState;

    uint32_t cHandles;

    NEMUUSB_RT Rt;

} NEMUUSBDEV_EXT, *PNEMUUSBDEV_EXT;

/* pnp state api */
static DECLINLINE(ENMNEMUUSB_PNPSTATE) nemuUsbPnPStateGet(PNEMUUSBDEV_EXT pDevExt)
{
    return (ENMNEMUUSB_PNPSTATE)ASMAtomicUoReadU32((volatile uint32_t*)&pDevExt->DdiState.PnPState.Curr);
}

static DECLINLINE(ENMNEMUUSB_PNPSTATE) nemuUsbPnPStateSet(PNEMUUSBDEV_EXT pDevExt, ENMNEMUUSB_PNPSTATE enmState)
{
    KIRQL Irql;
    ENMNEMUUSB_PNPSTATE enmOldState;
    KeAcquireSpinLock(&pDevExt->DdiState.Lock, &Irql);
    pDevExt->DdiState.PnPState.Prev = (ENMNEMUUSB_PNPSTATE)ASMAtomicUoReadU32((volatile uint32_t*)&pDevExt->DdiState.PnPState.Curr);
    ASMAtomicWriteU32((volatile uint32_t*)&pDevExt->DdiState.PnPState.Curr, (uint32_t)enmState);
    pDevExt->DdiState.PnPState.Curr = enmState;
    enmOldState = pDevExt->DdiState.PnPState.Prev;
    KeReleaseSpinLock(&pDevExt->DdiState.Lock, Irql);
    NEMUUSB_PNP_GBG_STATE_CHANGE(enmOldState, enmState);
    return enmState;
}

static DECLINLINE(ENMNEMUUSB_PNPSTATE) nemuUsbPnPStateRestore(PNEMUUSBDEV_EXT pDevExt)
{
    ENMNEMUUSB_PNPSTATE enmNewState, enmOldState;
    KIRQL Irql;
    KeAcquireSpinLock(&pDevExt->DdiState.Lock, &Irql);
    enmOldState = pDevExt->DdiState.PnPState.Curr;
    enmNewState = pDevExt->DdiState.PnPState.Prev;
    ASMAtomicWriteU32((volatile uint32_t*)&pDevExt->DdiState.PnPState.Curr, (uint32_t)pDevExt->DdiState.PnPState.Prev);
    KeReleaseSpinLock(&pDevExt->DdiState.Lock, Irql);
    NEMUUSB_PNP_GBG_STATE_CHANGE(enmOldState, enmNewState);
    Assert(enmNewState == ENMNEMUUSB_PNPSTATE_STARTED);
    Assert(enmOldState == ENMNEMUUSB_PNPSTATE_STOP_PENDING
            || enmOldState == ENMNEMUUSB_PNPSTATE_REMOVE_PENDING);
    return enmNewState;
}

static DECLINLINE(VOID) nemuUsbPnPStateInit(PNEMUUSBDEV_EXT pDevExt)
{
    pDevExt->DdiState.PnPState.Curr = pDevExt->DdiState.PnPState.Prev = ENMNEMUUSB_PNPSTATE_START_PENDING;
}

static DECLINLINE(VOID) nemuUsbDdiStateInit(PNEMUUSBDEV_EXT pDevExt)
{
    KeInitializeSpinLock(&pDevExt->DdiState.Lock);
    NemuDrvToolRefInit(&pDevExt->DdiState.Ref);
    nemuUsbPwrStateInit(pDevExt);
    nemuUsbPnPStateInit(pDevExt);
}

static DECLINLINE(bool) nemuUsbDdiStateRetainIfStarted(PNEMUUSBDEV_EXT pDevExt)
{
    KIRQL oldIrql;
    bool bRetained = true;
    KeAcquireSpinLock(&pDevExt->DdiState.Lock, &oldIrql);
    if (nemuUsbPnPStateGet(pDevExt) == ENMNEMUUSB_PNPSTATE_STARTED)
    {
        NemuDrvToolRefRetain(&pDevExt->DdiState.Ref);
    }
    else
    {
        bRetained = false;
    }
    KeReleaseSpinLock(&pDevExt->DdiState.Lock, oldIrql);
    return bRetained;
}

/* if device is removed - does nothing and returns zero,
 * otherwise increments a ref counter and returns the current pnp state
 * NOTE: never returns ENMNEMUUSB_PNPSTATE_REMOVED
 * */
static DECLINLINE(ENMNEMUUSB_PNPSTATE) nemuUsbDdiStateRetainIfNotRemoved(PNEMUUSBDEV_EXT pDevExt)
{
    KIRQL oldIrql;
    ENMNEMUUSB_PNPSTATE enmState;
    KeAcquireSpinLock(&pDevExt->DdiState.Lock, &oldIrql);
    enmState = nemuUsbPnPStateGet(pDevExt);
    if (enmState != ENMNEMUUSB_PNPSTATE_REMOVED)
    {
        NemuDrvToolRefRetain(&pDevExt->DdiState.Ref);
    }
    KeReleaseSpinLock(&pDevExt->DdiState.Lock, oldIrql);
    return enmState != ENMNEMUUSB_PNPSTATE_REMOVED ? enmState : (ENMNEMUUSB_PNPSTATE)0;
}

static DECLINLINE(uint32_t) nemuUsbDdiStateRetain(PNEMUUSBDEV_EXT pDevExt)
{
    return NemuDrvToolRefRetain(&pDevExt->DdiState.Ref);
}

static DECLINLINE(uint32_t) nemuUsbDdiStateRelease(PNEMUUSBDEV_EXT pDevExt)
{
    return NemuDrvToolRefRelease(&pDevExt->DdiState.Ref);
}

static DECLINLINE(VOID) nemuUsbDdiStateReleaseAndWaitCompleted(PNEMUUSBDEV_EXT pDevExt)
{
    NemuDrvToolRefRelease(&pDevExt->DdiState.Ref);
    NemuDrvToolRefWaitEqual(&pDevExt->DdiState.Ref, 1);
}

static DECLINLINE(VOID) nemuUsbDdiStateReleaseAndWaitRemoved(PNEMUUSBDEV_EXT pDevExt)
{
    NemuDrvToolRefRelease(&pDevExt->DdiState.Ref);
    NemuDrvToolRefWaitEqual(&pDevExt->DdiState.Ref, 0);
}

#endif /* #ifndef ___NemuUsbDev_h___ */
