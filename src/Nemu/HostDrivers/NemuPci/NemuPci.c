/* $Id: NemuPci.c $ */
/** @file
 * NemuPci - PCI card passthrough support (Host), Common Code.
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

/** @page pg_rawpci     NemuPci - host PCI support
 *
 * This is a kernel module that works as host proxy between guest and
 * PCI hardware.
 *
 */

#define LOG_GROUP LOG_GROUP_DEV_PCI_RAW
#include <Nemu/log.h>
#include <Nemu/err.h>
#include <Nemu/sup.h>
#include <Nemu/version.h>

#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/spinlock.h>
#include <iprt/uuid.h>
#include <iprt/asm.h>
#include <iprt/mem.h>

#include "NemuPciInternal.h"


#define DEVPORT_2_NEMURAWPCIINS(pPort) \
    ( (PNEMURAWPCIINS)((uint8_t *)pPort - RT_OFFSETOF(NEMURAWPCIINS, DevPort)) )


/**
 * Implements the SUPDRV component factor interface query method.
 *
 * @returns Pointer to an interface. NULL if not supported.
 *
 * @param   pSupDrvFactory      Pointer to the component factory registration structure.
 * @param   pSession            The session - unused.
 * @param   pszInterfaceUuid    The factory interface id.
 */
static DECLCALLBACK(void *) nemuPciQueryFactoryInterface(PCSUPDRVFACTORY pSupDrvFactory, PSUPDRVSESSION pSession, const char *pszInterfaceUuid)
{
    PNEMURAWPCIGLOBALS pGlobals = (PNEMURAWPCIGLOBALS)((uint8_t *)pSupDrvFactory - RT_OFFSETOF(NEMURAWPCIGLOBALS, SupDrvFactory));

    /*
     * Convert the UUID strings and compare them.
     */
    RTUUID UuidReq;
    int rc = RTUuidFromStr(&UuidReq, pszInterfaceUuid);
    if (RT_SUCCESS(rc))
    {
        if (!RTUuidCompareStr(&UuidReq, RAWPCIFACTORY_UUID_STR))
        {
            ASMAtomicIncS32(&pGlobals->cFactoryRefs);
            return &pGlobals->RawPciFactory;
        }
    }
    else
        Log(("NemuRawPci: rc=%Rrc, uuid=%s\n", rc, pszInterfaceUuid));

    return NULL;
}
DECLINLINE(int) nemuPciDevLock(PNEMURAWPCIINS pThis)
{
#ifdef NEMU_WITH_SHARED_PCI_INTERRUPTS
    RTSpinlockAcquire(pThis->hSpinlock);
    return VINF_SUCCESS;
#else
    int rc = RTSemFastMutexRequest(pThis->hFastMtx);

    AssertRC(rc);
    return rc;
#endif
}

DECLINLINE(void) nemuPciDevUnlock(PNEMURAWPCIINS pThis)
{
#ifdef NEMU_WITH_SHARED_PCI_INTERRUPTS
    RTSpinlockRelease(pThis->hSpinlock);
#else
    RTSemFastMutexRelease(pThis->hFastMtx);
#endif
}

DECLINLINE(int) nemuPciVmLock(PNEMURAWPCIDRVVM pThis)
{
    int rc = RTSemFastMutexRequest(pThis->hFastMtx);
    AssertRC(rc);
    return rc;
}

DECLINLINE(void) nemuPciVmUnlock(PNEMURAWPCIDRVVM pThis)
{
    RTSemFastMutexRelease(pThis->hFastMtx);
}

DECLINLINE(int) nemuPciGlobalsLock(PNEMURAWPCIGLOBALS pGlobals)
{
    int rc = RTSemFastMutexRequest(pGlobals->hFastMtx);
    AssertRC(rc);
    return rc;
}

DECLINLINE(void) nemuPciGlobalsUnlock(PNEMURAWPCIGLOBALS pGlobals)
{
    RTSemFastMutexRelease(pGlobals->hFastMtx);
}

static PNEMURAWPCIINS nemuPciFindInstanceLocked(PNEMURAWPCIGLOBALS pGlobals, uint32_t iHostAddress)
{
    PNEMURAWPCIINS pCur;
    for (pCur = pGlobals->pInstanceHead; pCur != NULL; pCur = pCur->pNext)
    {
        if (iHostAddress == pCur->HostPciAddress)
            return pCur;
    }
    return NULL;
}

static void nemuPciUnlinkInstanceLocked(PNEMURAWPCIGLOBALS pGlobals, PNEMURAWPCIINS pToUnlink)
{
    if (pGlobals->pInstanceHead == pToUnlink)
        pGlobals->pInstanceHead = pToUnlink->pNext;
    else
    {
        PNEMURAWPCIINS pCur;
        for (pCur = pGlobals->pInstanceHead; pCur != NULL; pCur = pCur->pNext)
        {
            if (pCur->pNext == pToUnlink)
            {
                pCur->pNext = pToUnlink->pNext;
                break;
            }
        }
    }
    pToUnlink->pNext = NULL;
}


DECLHIDDEN(void) nemuPciDevCleanup(PNEMURAWPCIINS pThis)
{
    pThis->DevPort.pfnDeinit(&pThis->DevPort, 0);

    if (pThis->hFastMtx)
    {
        RTSemFastMutexDestroy(pThis->hFastMtx);
        pThis->hFastMtx = NIL_RTSEMFASTMUTEX;
    }

    if (pThis->hSpinlock)
    {
        RTSpinlockDestroy(pThis->hSpinlock);
        pThis->hSpinlock = NIL_RTSPINLOCK;
    }

    nemuPciGlobalsLock(pThis->pGlobals);
    nemuPciUnlinkInstanceLocked(pThis->pGlobals, pThis);
    nemuPciGlobalsUnlock(pThis->pGlobals);
}


/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnInit}
 */
static DECLCALLBACK(int) nemuPciDevInit(PRAWPCIDEVPORT pPort, uint32_t fFlags)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int rc;

    nemuPciDevLock(pThis);

    rc = nemuPciOsDevInit(pThis, fFlags);

    nemuPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnDeinit}
 */
static DECLCALLBACK(int) nemuPciDevDeinit(PRAWPCIDEVPORT pPort, uint32_t fFlags)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;

    nemuPciDevLock(pThis);

    if (pThis->IrqHandler.pfnIrqHandler)
    {
        nemuPciOsDevUnregisterIrqHandler(pThis, pThis->IrqHandler.iHostIrq);
        pThis->IrqHandler.iHostIrq = 0;
        pThis->IrqHandler.pfnIrqHandler = NULL;
    }

    rc = nemuPciOsDevDeinit(pThis, fFlags);

    nemuPciDevUnlock(pThis);

    return rc;
}


/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnDestroy}
 */
static DECLCALLBACK(int) nemuPciDevDestroy(PRAWPCIDEVPORT pPort)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int rc;

    rc = nemuPciOsDevDestroy(pThis);
    if (rc == VINF_SUCCESS)
    {
        if (pThis->hFastMtx)
        {
            RTSemFastMutexDestroy(pThis->hFastMtx);
            pThis->hFastMtx = NIL_RTSEMFASTMUTEX;
        }

        if (pThis->hSpinlock)
        {
            RTSpinlockDestroy(pThis->hSpinlock);
            pThis->hSpinlock = NIL_RTSPINLOCK;
        }

        nemuPciGlobalsLock(pThis->pGlobals);
        nemuPciUnlinkInstanceLocked(pThis->pGlobals, pThis);
        nemuPciGlobalsUnlock(pThis->pGlobals);

        RTMemFree(pThis);
    }

    return rc;
}
/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnGetRegionInfo}
 */
static DECLCALLBACK(int) nemuPciDevGetRegionInfo(PRAWPCIDEVPORT pPort,
                                                 int32_t        iRegion,
                                                 RTHCPHYS       *pRegionStart,
                                                 uint64_t       *pu64RegionSize,
                                                 bool           *pfPresent,
                                                 uint32_t        *pfFlags)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;

    nemuPciDevLock(pThis);

    rc = nemuPciOsDevGetRegionInfo(pThis, iRegion,
                                   pRegionStart, pu64RegionSize,
                                   pfPresent, pfFlags);
    nemuPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnMapRegion}
 */
static DECLCALLBACK(int) nemuPciDevMapRegion(PRAWPCIDEVPORT pPort,
                                             int32_t        iRegion,
                                             RTHCPHYS       RegionStart,
                                             uint64_t       u64RegionSize,
                                             int32_t        fFlags,
                                             RTR0PTR        *pRegionBase)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;

    nemuPciDevLock(pThis);

    rc = nemuPciOsDevMapRegion(pThis, iRegion, RegionStart, u64RegionSize, fFlags, pRegionBase);

    nemuPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnUnmapRegion}
 */
static DECLCALLBACK(int) nemuPciDevUnmapRegion(PRAWPCIDEVPORT pPort,
                                               int32_t        iRegion,
                                               RTHCPHYS       RegionStart,
                                               uint64_t       u64RegionSize,
                                               RTR0PTR        RegionBase)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;

    nemuPciDevLock(pThis);

    rc = nemuPciOsDevUnmapRegion(pThis, iRegion, RegionStart, u64RegionSize, RegionBase);

    nemuPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnPciCfgRead}
 */
static DECLCALLBACK(int) nemuPciDevPciCfgRead(PRAWPCIDEVPORT pPort,
                                              uint32_t       Register,
                                              PCIRAWMEMLOC   *pValue)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;

    nemuPciDevLock(pThis);

    rc = nemuPciOsDevPciCfgRead(pThis, Register, pValue);

    nemuPciDevUnlock(pThis);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIDEVPORT,pfnPciCfgWrite}
 */
static DECLCALLBACK(int) nemuPciDevPciCfgWrite(PRAWPCIDEVPORT pPort,
                                               uint32_t       Register,
                                               PCIRAWMEMLOC   *pValue)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;

    nemuPciDevLock(pThis);

    rc = nemuPciOsDevPciCfgWrite(pThis, Register, pValue);

    nemuPciDevUnlock(pThis);

    return rc;
}

static DECLCALLBACK(int) nemuPciDevRegisterIrqHandler(PRAWPCIDEVPORT  pPort,
                                                      PFNRAWPCIISR    pfnHandler,
                                                      void*           pIrqContext,
                                                      PCIRAWISRHANDLE *phIsr)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;
    int32_t        iHostIrq = 0;

    if (pfnHandler == NULL)
        return VERR_INVALID_PARAMETER;

    nemuPciDevLock(pThis);

    if (pThis->IrqHandler.pfnIrqHandler)
    {
        rc = VERR_ALREADY_EXISTS;
    }
    else
    {
        rc = nemuPciOsDevRegisterIrqHandler(pThis, pfnHandler, pIrqContext, &iHostIrq);
        if (RT_SUCCESS(rc))
        {
            *phIsr = 0xcafe0000;
            pThis->IrqHandler.iHostIrq      = iHostIrq;
            pThis->IrqHandler.pfnIrqHandler = pfnHandler;
            pThis->IrqHandler.pIrqContext   = pIrqContext;
        }
    }

    nemuPciDevUnlock(pThis);

    return rc;
}

static DECLCALLBACK(int) nemuPciDevUnregisterIrqHandler(PRAWPCIDEVPORT  pPort,
                                                        PCIRAWISRHANDLE hIsr)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;

    if (hIsr != 0xcafe0000)
        return VERR_INVALID_PARAMETER;

    nemuPciDevLock(pThis);

    rc = nemuPciOsDevUnregisterIrqHandler(pThis, pThis->IrqHandler.iHostIrq);
    if (RT_SUCCESS(rc))
    {
        pThis->IrqHandler.pfnIrqHandler = NULL;
        pThis->IrqHandler.pIrqContext   = NULL;
        pThis->IrqHandler.iHostIrq = 0;
    }
    nemuPciDevUnlock(pThis);

    return rc;
}

static DECLCALLBACK(int) nemuPciDevPowerStateChange(PRAWPCIDEVPORT    pPort,
                                                    PCIRAWPOWERSTATE  aState,
                                                    uint64_t          *pu64Param)
{
    PNEMURAWPCIINS pThis = DEVPORT_2_NEMURAWPCIINS(pPort);
    int            rc;

    nemuPciDevLock(pThis);

    rc = nemuPciOsDevPowerStateChange(pThis, aState);

    switch (aState)
    {
        case PCIRAW_POWER_ON:
            /*
             * Let virtual device know about VM caps.
             */
            *pu64Param = NEMU_DRV_VMDATA(pThis)->pPerVmData->fVmCaps;
            break;
        default:
            pu64Param = 0;
            break;
    }


    nemuPciDevUnlock(pThis);

    return rc;
}

/**
 * Creates a new instance.
 *
 * @returns Nemu status code.
 * @param   pGlobals            The globals.
 * @param   pszName             The instance name.
 * @param   ppDevPort           Where to store the pointer to our port interface.
 */
static int nemuPciNewInstance(PNEMURAWPCIGLOBALS pGlobals,
                              uint32_t           u32HostAddress,
                              uint32_t           fFlags,
                              PRAWPCIPERVM       pVmCtx,
                              PRAWPCIDEVPORT     *ppDevPort,
                              uint32_t           *pfDevFlags)
{
    int             rc;
    PNEMURAWPCIINS  pNew = (PNEMURAWPCIINS)RTMemAllocZ(sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;

    pNew->pGlobals                      = pGlobals;
    pNew->hSpinlock                     = NIL_RTSPINLOCK;
    pNew->cRefs                         = 1;
    pNew->pNext                         = NULL;
    pNew->HostPciAddress                = u32HostAddress;
    pNew->pVmCtx                        = pVmCtx;

    pNew->DevPort.u32Version            = RAWPCIDEVPORT_VERSION;

    pNew->DevPort.pfnInit               = nemuPciDevInit;
    pNew->DevPort.pfnDeinit             = nemuPciDevDeinit;
    pNew->DevPort.pfnDestroy            = nemuPciDevDestroy;
    pNew->DevPort.pfnGetRegionInfo      = nemuPciDevGetRegionInfo;
    pNew->DevPort.pfnMapRegion          = nemuPciDevMapRegion;
    pNew->DevPort.pfnUnmapRegion        = nemuPciDevUnmapRegion;
    pNew->DevPort.pfnPciCfgRead         = nemuPciDevPciCfgRead;
    pNew->DevPort.pfnPciCfgWrite        = nemuPciDevPciCfgWrite;
    pNew->DevPort.pfnPciCfgRead         = nemuPciDevPciCfgRead;
    pNew->DevPort.pfnPciCfgWrite        = nemuPciDevPciCfgWrite;
    pNew->DevPort.pfnRegisterIrqHandler = nemuPciDevRegisterIrqHandler;
    pNew->DevPort.pfnUnregisterIrqHandler = nemuPciDevUnregisterIrqHandler;
    pNew->DevPort.pfnPowerStateChange   = nemuPciDevPowerStateChange;
    pNew->DevPort.u32VersionEnd         = RAWPCIDEVPORT_VERSION;

    rc = RTSpinlockCreate(&pNew->hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "NemuPCI");
    if (RT_SUCCESS(rc))
    {
        rc = RTSemFastMutexCreate(&pNew->hFastMtx);
        if (RT_SUCCESS(rc))
        {
            rc = pNew->DevPort.pfnInit(&pNew->DevPort, fFlags);
            if (RT_SUCCESS(rc))
            {
                *ppDevPort = &pNew->DevPort;

                pNew->pNext = pGlobals->pInstanceHead;
                pGlobals->pInstanceHead = pNew;
            }
            else
            {
                RTSemFastMutexDestroy(pNew->hFastMtx);
                RTSpinlockDestroy(pNew->hSpinlock);
                RTMemFree(pNew);
            }
        }
    }

    return rc;
}

/**
 * @interface_method_impl{RAWPCIFACTORY,pfnCreateAndConnect}
 */
static DECLCALLBACK(int) nemuPciFactoryCreateAndConnect(PRAWPCIFACTORY       pFactory,
                                                        uint32_t             u32HostAddress,
                                                        uint32_t             fFlags,
                                                        PRAWPCIPERVM         pVmCtx,
                                                        PRAWPCIDEVPORT       *ppDevPort,
                                                        uint32_t             *pfDevFlags)
{
    PNEMURAWPCIGLOBALS pGlobals = (PNEMURAWPCIGLOBALS)((uint8_t *)pFactory - RT_OFFSETOF(NEMURAWPCIGLOBALS, RawPciFactory));
    int rc;

    LogFlow(("nemuPciFactoryCreateAndConnect: PCI=%x fFlags=%#x\n", u32HostAddress, fFlags));
    Assert(pGlobals->cFactoryRefs > 0);
    rc = nemuPciGlobalsLock(pGlobals);
    AssertRCReturn(rc, rc);

    /* First search if there's no existing instance with same host device
     * address - if so - we cannot continue.
     */
    if (nemuPciFindInstanceLocked(pGlobals, u32HostAddress) != NULL)
    {
        rc = VERR_RESOURCE_BUSY;
        goto unlock;
    }

    rc = nemuPciNewInstance(pGlobals, u32HostAddress, fFlags, pVmCtx, ppDevPort, pfDevFlags);

unlock:
    nemuPciGlobalsUnlock(pGlobals);

    return rc;
}

/**
 * @interface_method_impl{RAWPCIFACTORY,pfnRelease}
 */
static DECLCALLBACK(void) nemuPciFactoryRelease(PRAWPCIFACTORY pFactory)
{
    PNEMURAWPCIGLOBALS pGlobals = (PNEMURAWPCIGLOBALS)((uint8_t *)pFactory - RT_OFFSETOF(NEMURAWPCIGLOBALS, RawPciFactory));

    int32_t cRefs = ASMAtomicDecS32(&pGlobals->cFactoryRefs);
    Assert(cRefs >= 0); NOREF(cRefs);
    LogFlow(("nemuPciFactoryRelease: cRefs=%d (new)\n", cRefs));
}

/**
 * @interface_method_impl{RAWPCIFACTORY,pfnInitVm}
 */
static DECLCALLBACK(int)  nemuPciFactoryInitVm(PRAWPCIFACTORY       pFactory,
                                               PVM                  pVM,
                                               PRAWPCIPERVM         pVmData)
{
    PNEMURAWPCIDRVVM pThis = (PNEMURAWPCIDRVVM)RTMemAllocZ(sizeof(NEMURAWPCIDRVVM));
    int rc;

    if (!pThis)
         return VERR_NO_MEMORY;

    rc = RTSemFastMutexCreate(&pThis->hFastMtx);
    if (RT_SUCCESS(rc))
    {
        rc = nemuPciOsInitVm(pThis, pVM, pVmData);

        if (RT_SUCCESS(rc))
        {
#ifdef NEMU_WITH_IOMMU
            /* If IOMMU notification routine in pVmData->pfnContigMemInfo
               is set - we have functional IOMMU hardware. */
            if (pVmData->pfnContigMemInfo)
                pVmData->fVmCaps |= PCIRAW_VMFLAGS_HAS_IOMMU;
#endif
            pThis->pPerVmData = pVmData;
            pVmData->pDriverData = pThis;
            return VINF_SUCCESS;
        }

        RTSemFastMutexDestroy(pThis->hFastMtx);
        pThis->hFastMtx = NIL_RTSEMFASTMUTEX;
        RTMemFree(pThis);
    }

    return rc;
}

/**
 * @interface_method_impl{RAWPCIFACTORY,pfnDeinitVm}
 */
static DECLCALLBACK(void)  nemuPciFactoryDeinitVm(PRAWPCIFACTORY       pFactory,
                                                  PVM                  pVM,
                                                  PRAWPCIPERVM         pPciData)
{
    if (pPciData->pDriverData)
    {
        PNEMURAWPCIDRVVM pThis = (PNEMURAWPCIDRVVM)pPciData->pDriverData;

#ifdef NEMU_WITH_IOMMU
        /* If we have IOMMU, need to unmap all guest's physical pages from IOMMU on VM termination. */
#endif

        nemuPciOsDeinitVm(pThis, pVM);

        if (pThis->hFastMtx)
        {
            RTSemFastMutexDestroy(pThis->hFastMtx);
            pThis->hFastMtx = NIL_RTSEMFASTMUTEX;
        }

        RTMemFree(pThis);
        pPciData->pDriverData = NULL;
    }
}


static bool nemuPciCanUnload(PNEMURAWPCIGLOBALS pGlobals)
{
    int rc = nemuPciGlobalsLock(pGlobals);
    bool fRc = !pGlobals->pInstanceHead
            && pGlobals->cFactoryRefs <= 0;
    nemuPciGlobalsUnlock(pGlobals);
    AssertRC(rc);
    return fRc;
}


static int nemuPciInitIdc(PNEMURAWPCIGLOBALS pGlobals)
{
    int rc;
    Assert(!pGlobals->fIDCOpen);

    /*
     * Establish a connection to SUPDRV and register our component factory.
     */
    rc = SUPR0IdcOpen(&pGlobals->SupDrvIDC, 0 /* iReqVersion = default */, 0 /* iMinVersion = default */, NULL, NULL, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = SUPR0IdcComponentRegisterFactory(&pGlobals->SupDrvIDC, &pGlobals->SupDrvFactory);
        if (RT_SUCCESS(rc))
        {
            pGlobals->fIDCOpen = true;
            Log(("NemuRawPci: pSession=%p\n", SUPR0IdcGetSession(&pGlobals->SupDrvIDC)));
            return rc;
        }

        /* bail out. */
        LogRel(("NemuRawPci: Failed to register component factory, rc=%Rrc\n", rc));
        SUPR0IdcClose(&pGlobals->SupDrvIDC);
    }

    return rc;
}

/**
 * Try to close the IDC connection to SUPDRV if established.
 *
 * @returns Nemu status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_WRONG_ORDER if we're busy.
 *
 * @param   pGlobals        Pointer to the globals.
 */
DECLHIDDEN(int) nemuPciDeleteIdc(PNEMURAWPCIGLOBALS pGlobals)
{
    int rc;

    Assert(pGlobals->hFastMtx != NIL_RTSEMFASTMUTEX);

    /*
     * Check before trying to deregister the factory.
     */
    if (!nemuPciCanUnload(pGlobals))
        return VERR_WRONG_ORDER;

    if (!pGlobals->fIDCOpen)
        rc = VINF_SUCCESS;
    else
    {
        /*
         * Disconnect from SUPDRV.
         */
        rc = SUPR0IdcComponentDeregisterFactory(&pGlobals->SupDrvIDC, &pGlobals->SupDrvFactory);
        AssertRC(rc);
        SUPR0IdcClose(&pGlobals->SupDrvIDC);
        pGlobals->fIDCOpen = false;
    }

    return rc;
}


/**
 * Initializes the globals.
 *
 * @returns Nemu status code.
 * @param   pGlobals        Pointer to the globals.
 */
DECLHIDDEN(int) nemuPciInitGlobals(PNEMURAWPCIGLOBALS pGlobals)
{
    /*
     * Initialize the common portions of the structure.
     */
    int rc = RTSemFastMutexCreate(&pGlobals->hFastMtx);
    if (RT_SUCCESS(rc))
    {
        pGlobals->pInstanceHead = NULL;
        pGlobals->RawPciFactory.pfnRelease = nemuPciFactoryRelease;
        pGlobals->RawPciFactory.pfnCreateAndConnect = nemuPciFactoryCreateAndConnect;
        pGlobals->RawPciFactory.pfnInitVm = nemuPciFactoryInitVm;
        pGlobals->RawPciFactory.pfnDeinitVm = nemuPciFactoryDeinitVm;
        memcpy(pGlobals->SupDrvFactory.szName, "NemuRawPci", sizeof("NemuRawPci"));
        pGlobals->SupDrvFactory.pfnQueryFactoryInterface = nemuPciQueryFactoryInterface;
        pGlobals->fIDCOpen = false;
    }
    return rc;
}


/**
 * Deletes the globals.
 *
 *
 * @param   pGlobals        Pointer to the globals.
 */
DECLHIDDEN(void) nemuPciDeleteGlobals(PNEMURAWPCIGLOBALS pGlobals)
{
    Assert(!pGlobals->fIDCOpen);

    /*
     * Release resources.
     */
    if (pGlobals->hFastMtx)
    {
        RTSemFastMutexDestroy(pGlobals->hFastMtx);
        pGlobals->hFastMtx = NIL_RTSEMFASTMUTEX;
    }
}


int  nemuPciInit(PNEMURAWPCIGLOBALS pGlobals)
{

    /*
     * Initialize the common portions of the structure.
     */
    int rc = nemuPciInitGlobals(pGlobals);
    if (RT_SUCCESS(rc))
    {
        rc = nemuPciInitIdc(pGlobals);
        if (RT_SUCCESS(rc))
            return rc;

        /* bail out. */
        nemuPciDeleteGlobals(pGlobals);
    }

    return rc;
}

void nemuPciShutdown(PNEMURAWPCIGLOBALS pGlobals)
{
    int rc = nemuPciDeleteIdc(pGlobals);

    if (RT_SUCCESS(rc))
        nemuPciDeleteGlobals(pGlobals);
}

