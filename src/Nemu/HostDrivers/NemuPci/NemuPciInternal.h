/* $Id: NemuPciInternal.h $ */
/** @file
 * NemuPci - PCI driver (Host), Internal Header.
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

#ifndef ___VBoPciInternal_h___
#define ___NemuPciInternal_h___

#include <Nemu/sup.h>
#include <Nemu/rawpci.h>
#include <iprt/semaphore.h>
#include <iprt/assert.h>

#ifdef RT_OS_LINUX

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35) && defined(CONFIG_IOMMU_API)
# define NEMU_WITH_IOMMU
#endif

#ifdef NEMU_WITH_IOMMU
#include <linux/errno.h>
#include <linux/iommu.h>
#endif

#endif

RT_C_DECLS_BEGIN

/* Forward declaration. */
typedef struct NEMURAWPCIGLOBALS *PNEMURAWPCIGLOBALS;
typedef struct NEMURAWPCIDRVVM   *PNEMURAWPCIDRVVM;
typedef struct NEMURAWPCIINS     *PNEMURAWPCIINS;

typedef struct NEMURAWPCIISRDESC
{
    /** Handler function. */
    PFNRAWPCIISR       pfnIrqHandler;
    /** Handler context. */
    void              *pIrqContext;
    /** Host IRQ. */
    int32_t            iHostIrq;
} NEMURAWPCIISRDESC;
typedef struct NEMURAWPCIISRDESC     *PNEMURAWPCIISRDESC;

/**
 * The per-instance data of the Nemu raw PCI interface.
 *
 * This is data associated with a host PCI card attached to the VM.
 *
 */
typedef struct NEMURAWPCIINS
{
    /** Pointer to the globals. */
    PNEMURAWPCIGLOBALS pGlobals;

     /** Mutex protecting device access. */
    RTSEMFASTMUTEX     hFastMtx;
    /** The spinlock protecting the state variables and device access. */
    RTSPINLOCK         hSpinlock;
    /** Pointer to the next device in the list. */
    PNEMURAWPCIINS     pNext;
    /** Reference count. */
    uint32_t volatile cRefs;

    /* Host PCI address of this device. */
    uint32_t           HostPciAddress;

#ifdef RT_OS_LINUX
    struct pci_dev  *  pPciDev;
    char               szPrevDriver[64];
#endif
    bool               fMsiUsed;
    bool               fMsixUsed;
    bool               fIommuUsed;
    bool               fPad0;

    /** Port, given to the outside world. */
    RAWPCIDEVPORT      DevPort;

    /** IRQ handler. */
    NEMURAWPCIISRDESC  IrqHandler;

    /** Pointer to per-VM context in hypervisor data. */
    PRAWPCIPERVM       pVmCtx;

    RTR0PTR            aRegionR0Mapping[/* XXX: magic */ 7];
} NEMURAWPCIINS;

/**
 * Per-VM data of the Nemu PCI driver. Pointed to by pGVM->rawpci.s.pDriverData.
 *
 */
typedef struct NEMURAWPCIDRVVM
{
    /** Mutex protecting state changes. */
    RTSEMFASTMUTEX hFastMtx;

#ifdef RT_OS_LINUX
# ifdef NEMU_WITH_IOMMU
    /* IOMMU domain. */
    struct iommu_domain* pIommuDomain;
# endif
#endif
    /* Back pointer to pGVM->rawpci.s. */
    PRAWPCIPERVM pPerVmData;
} NEMURAWPCIDRVVM;

/**
 * The global data of the Nemu PCI driver.
 *
 * This contains the bit required for communicating with support driver, NemuDrv
 * (start out as SupDrv).
 */
typedef struct NEMURAWPCIGLOBALS
{
    /** Mutex protecting the list of instances and state changes. */
    RTSEMFASTMUTEX hFastMtx;

    /** Pointer to a list of instance data. */
    PNEMURAWPCIINS pInstanceHead;

    /** The raw PCI interface factory. */
    RAWPCIFACTORY RawPciFactory;
    /** The SUPDRV component factory registration. */
    SUPDRVFACTORY SupDrvFactory;
    /** The number of current factory references. */
    int32_t volatile cFactoryRefs;
    /** Whether the IDC connection is open or not.
     * This is only for cleaning up correctly after the separate IDC init on Windows. */
    bool fIDCOpen;
    /** The SUPDRV IDC handle (opaque struct). */
    SUPDRVIDCHANDLE SupDrvIDC;
#ifdef RT_OS_LINUX
    bool fPciStubModuleAvail;
    struct module    * pciStubModule;
#endif
} NEMURAWPCIGLOBALS;

DECLHIDDEN(int)  nemuPciInit(PNEMURAWPCIGLOBALS pGlobals);
DECLHIDDEN(void) nemuPciShutdown(PNEMURAWPCIGLOBALS pGlobals);

DECLHIDDEN(int)  nemuPciOsInitVm(PNEMURAWPCIDRVVM pThis,   PVM pVM, PRAWPCIPERVM pVmData);
DECLHIDDEN(void) nemuPciOsDeinitVm(PNEMURAWPCIDRVVM pThis, PVM pVM);

DECLHIDDEN(int)  nemuPciOsDevInit  (PNEMURAWPCIINS pIns, uint32_t fFlags);
DECLHIDDEN(int)  nemuPciOsDevDeinit(PNEMURAWPCIINS pIns, uint32_t fFlags);
DECLHIDDEN(int)  nemuPciOsDevDestroy(PNEMURAWPCIINS pIns);

DECLHIDDEN(int)  nemuPciOsDevGetRegionInfo(PNEMURAWPCIINS pIns,
                                           int32_t        iRegion,
                                           RTHCPHYS       *pRegionStart,
                                           uint64_t       *pu64RegionSize,
                                           bool           *pfPresent,
                                           uint32_t       *pfFlags);
DECLHIDDEN(int)  nemuPciOsDevMapRegion(PNEMURAWPCIINS pIns,
                                       int32_t        iRegion,
                                       RTHCPHYS       pRegionStart,
                                       uint64_t       u64RegionSize,
                                       uint32_t       fFlags,
                                       RTR0PTR        *pRegionBase);
DECLHIDDEN(int)  nemuPciOsDevUnmapRegion(PNEMURAWPCIINS pIns,
                                         int32_t        iRegion,
                                         RTHCPHYS       RegionStart,
                                         uint64_t       u64RegionSize,
                                         RTR0PTR        RegionBase);

DECLHIDDEN(int)  nemuPciOsDevPciCfgWrite(PNEMURAWPCIINS pIns, uint32_t Register, PCIRAWMEMLOC *pValue);
DECLHIDDEN(int)  nemuPciOsDevPciCfgRead (PNEMURAWPCIINS pIns, uint32_t Register, PCIRAWMEMLOC *pValue);

DECLHIDDEN(int)  nemuPciOsDevRegisterIrqHandler  (PNEMURAWPCIINS pIns, PFNRAWPCIISR pfnHandler, void* pIrqContext, int32_t *piHostIrq);
DECLHIDDEN(int)  nemuPciOsDevUnregisterIrqHandler(PNEMURAWPCIINS pIns, int32_t iHostIrq);

DECLHIDDEN(int)  nemuPciOsDevPowerStateChange(PNEMURAWPCIINS pIns, PCIRAWPOWERSTATE  aState);

#define NEMU_DRV_VMDATA(pIns) ((PNEMURAWPCIDRVVM)(pIns->pVmCtx ? pIns->pVmCtx->pDriverData : NULL))

RT_C_DECLS_END

#endif
