/* $Id: NemuMPVidPn.h $ */

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

#ifndef ___NemuMPVidPn_h___
#define ___NemuMPVidPn_h___

#define NEMUVDPN_C_DISPLAY_HBLANK_SIZE 200
#define NEMUVDPN_C_DISPLAY_VBLANK_SIZE 180

void NemuVidPnAllocDataInit(struct NEMUWDDM_ALLOC_DATA *pData, D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);

void NemuVidPnSourceInit(PNEMUWDDM_SOURCE pSource, const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, uint8_t u8SyncState);
void NemuVidPnTargetInit(PNEMUWDDM_TARGET pTarget, const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, uint8_t u8SyncState);
void NemuVidPnSourceCopy(NEMUWDDM_SOURCE *pDst, const NEMUWDDM_SOURCE *pSrc);
void NemuVidPnTargetCopy(NEMUWDDM_TARGET *pDst, const NEMUWDDM_TARGET *pSrc);

void NemuVidPnSourcesInit(PNEMUWDDM_SOURCE pSources, uint32_t cScreens, uint8_t u8SyncState);
void NemuVidPnTargetsInit(PNEMUWDDM_TARGET pTargets, uint32_t cScreens, uint8_t u8SyncState);
void NemuVidPnSourcesCopy(NEMUWDDM_SOURCE *pDst, const NEMUWDDM_SOURCE *pSrc, uint32_t cScreens);
void NemuVidPnTargetsCopy(NEMUWDDM_TARGET *pDst, const NEMUWDDM_TARGET *pSrc, uint32_t cScreens);

typedef struct NEMUWDDM_TARGET_ITER
{
    PNEMUWDDM_SOURCE pSource;
    PNEMUWDDM_TARGET paTargets;
    uint32_t cTargets;
    uint32_t i;
    uint32_t c;
} NEMUWDDM_TARGET_ITER;

void NemuVidPnStCleanup(PNEMUWDDM_SOURCE paSources, PNEMUWDDM_TARGET paTargets, uint32_t cScreens);
void NemuVidPnStTIterInit(PNEMUWDDM_SOURCE pSource, PNEMUWDDM_TARGET paTargets, uint32_t cTargets, NEMUWDDM_TARGET_ITER *pIter);
PNEMUWDDM_TARGET NemuVidPnStTIterNext(NEMUWDDM_TARGET_ITER *pIter);

/* !!!NOTE: The callback is responsible for releasing the path */
typedef DECLCALLBACK(BOOLEAN) FNNEMUVIDPNENUMPATHS(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo, PVOID pContext);
typedef FNNEMUVIDPNENUMPATHS *PFNNEMUVIDPNENUMPATHS;

/* !!!NOTE: The callback is responsible for releasing the source mode info */
typedef DECLCALLBACK(BOOLEAN) FNNEMUVIDPNENUMSOURCEMODES(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo, PVOID pContext);
typedef FNNEMUVIDPNENUMSOURCEMODES *PFNNEMUVIDPNENUMSOURCEMODES;

/* !!!NOTE: The callback is responsible for releasing the target mode info */
typedef DECLCALLBACK(BOOLEAN) FNNEMUVIDPNENUMTARGETMODES(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, PVOID pContext);
typedef FNNEMUVIDPNENUMTARGETMODES *PFNNEMUVIDPNENUMTARGETMODES;

/* !!!NOTE: The callback is responsible for releasing the source mode info */
typedef DECLCALLBACK(BOOLEAN) FNNEMUVIDPNENUMMONITORSOURCEMODES(D3DKMDT_HMONITORSOURCEMODESET hMonitorSMS, CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSMSIf,
        CONST D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSMI, PVOID pContext);
typedef FNNEMUVIDPNENUMMONITORSOURCEMODES *PFNNEMUVIDPNENUMMONITORSOURCEMODES;

typedef DECLCALLBACK(BOOLEAN) FNNEMUVIDPNENUMTARGETSFORSOURCE(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, SIZE_T cTgtPaths, PVOID pContext);
typedef FNNEMUVIDPNENUMTARGETSFORSOURCE *PFNNEMUVIDPNENUMTARGETSFORSOURCE;

NTSTATUS NemuVidPnCommitSourceModeForSrcId(PNEMUMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        PNEMUWDDM_ALLOCATION pAllocation,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId, NEMUWDDM_SOURCE *paSources, NEMUWDDM_TARGET *paTargets);

NTSTATUS NemuVidPnCommitAll(PNEMUMP_DEVEXT pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        PNEMUWDDM_ALLOCATION pAllocation,
        NEMUWDDM_SOURCE *paSources, NEMUWDDM_TARGET *paTargets);

NTSTATUS nemuVidPnEnumPaths(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        PFNNEMUVIDPNENUMPATHS pfnCallback, PVOID pContext);

NTSTATUS nemuVidPnEnumSourceModes(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        PFNNEMUVIDPNENUMSOURCEMODES pfnCallback, PVOID pContext);

NTSTATUS nemuVidPnEnumTargetModes(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        PFNNEMUVIDPNENUMTARGETMODES pfnCallback, PVOID pContext);

NTSTATUS nemuVidPnEnumMonitorSourceModes(D3DKMDT_HMONITORSOURCEMODESET hMonitorSMS, CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSMSIf,
        PFNNEMUVIDPNENUMMONITORSOURCEMODES pfnCallback, PVOID pContext);

NTSTATUS nemuVidPnEnumTargetsForSource(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        PFNNEMUVIDPNENUMTARGETSFORSOURCE pfnCallback, PVOID pContext);

void NemuVidPnDumpTargetMode(const char *pPrefix, const D3DKMDT_VIDPN_TARGET_MODE* CONST  pVidPnTargetModeInfo, const char *pSuffix);
void NemuVidPnDumpMonitorMode(const char *pPrefix, const D3DKMDT_MONITOR_SOURCE_MODE *pVidPnModeInfo, const char *pSuffix);
NTSTATUS NemuVidPnDumpMonitorModeSet(const char *pPrefix, PNEMUMP_DEVEXT pDevExt, uint32_t u32Target, const char *pSuffix);
void NemuVidPnDumpSourceMode(const char *pPrefix, const D3DKMDT_VIDPN_SOURCE_MODE* pVidPnSourceModeInfo, const char *pSuffix);
void NemuVidPnDumpCofuncModalityInfo(const char *pPrefix, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmEnumPivotType, const DXGK_ENUM_PIVOT *pPivot, const char *pSuffix);

void nemuVidPnDumpVidPn(const char * pPrefix, PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, const char * pSuffix);
void nemuVidPnDumpCofuncModalityArg(const char *pPrefix, CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModalityArg, const char *pSuffix);
DECLCALLBACK(BOOLEAN) nemuVidPnDumpSourceModeSetEnum(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo, PVOID pContext);
DECLCALLBACK(BOOLEAN) nemuVidPnDumpTargetModeSetEnum(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, PVOID pContext);


typedef struct NEMUVIDPN_SOURCEMODE_ITER
{
    D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;
    const D3DKMDT_VIDPN_SOURCE_MODE *pCurVidPnModeInfo;
    NTSTATUS Status;
} NEMUVIDPN_SOURCEMODE_ITER;

DECLINLINE(void) NemuVidPnSourceModeIterInit(NEMUVIDPN_SOURCEMODE_ITER *pIter, D3DKMDT_HVIDPNSOURCEMODESET hVidPnModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnModeSetInterface)
{
    pIter->hVidPnModeSet = hVidPnModeSet;
    pIter->pVidPnModeSetInterface = pVidPnModeSetInterface;
    pIter->pCurVidPnModeInfo = NULL;
    pIter->Status = STATUS_SUCCESS;
}

DECLINLINE(void) NemuVidPnSourceModeIterTerm(NEMUVIDPN_SOURCEMODE_ITER *pIter)
{
    if (pIter->pCurVidPnModeInfo)
    {
        pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);
        pIter->pCurVidPnModeInfo = NULL;
    }
}

DECLINLINE(const D3DKMDT_VIDPN_SOURCE_MODE *) NemuVidPnSourceModeIterNext(NEMUVIDPN_SOURCEMODE_ITER *pIter)
{
    NTSTATUS Status;
    const D3DKMDT_VIDPN_SOURCE_MODE *pCurVidPnModeInfo;

    if (!pIter->pCurVidPnModeInfo)
        Status = pIter->pVidPnModeSetInterface->pfnAcquireFirstModeInfo(pIter->hVidPnModeSet, &pCurVidPnModeInfo);
    else
        Status = pIter->pVidPnModeSetInterface->pfnAcquireNextModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo, &pCurVidPnModeInfo);

    if (Status == STATUS_SUCCESS)
    {
        Assert(pCurVidPnModeInfo);

        if (pIter->pCurVidPnModeInfo)
            pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);

        pIter->pCurVidPnModeInfo = pCurVidPnModeInfo;
        return pCurVidPnModeInfo;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET
            || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return NULL;

    WARN(("getting Source info failed %#x", Status));

    pIter->Status = Status;
    return NULL;
}

DECLINLINE(NTSTATUS) NemuVidPnSourceModeIterStatus(NEMUVIDPN_SOURCEMODE_ITER *pIter)
{
    return pIter->Status;
}

typedef struct NEMUVIDPN_TARGETMODE_ITER
{
    D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface;
    const D3DKMDT_VIDPN_TARGET_MODE *pCurVidPnModeInfo;
    NTSTATUS Status;
} NEMUVIDPN_TARGETMODE_ITER;

DECLINLINE(void) NemuVidPnTargetModeIterInit(NEMUVIDPN_TARGETMODE_ITER *pIter,D3DKMDT_HVIDPNTARGETMODESET hVidPnModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnModeSetInterface)
{
    pIter->hVidPnModeSet = hVidPnModeSet;
    pIter->pVidPnModeSetInterface = pVidPnModeSetInterface;
    pIter->pCurVidPnModeInfo = NULL;
    pIter->Status = STATUS_SUCCESS;
}

DECLINLINE(void) NemuVidPnTargetModeIterTerm(NEMUVIDPN_TARGETMODE_ITER *pIter)
{
    if (pIter->pCurVidPnModeInfo)
    {
        pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);
        pIter->pCurVidPnModeInfo = NULL;
    }
}

DECLINLINE(const D3DKMDT_VIDPN_TARGET_MODE *) NemuVidPnTargetModeIterNext(NEMUVIDPN_TARGETMODE_ITER *pIter)
{
    NTSTATUS Status;
    const D3DKMDT_VIDPN_TARGET_MODE *pCurVidPnModeInfo;

    if (!pIter->pCurVidPnModeInfo)
        Status = pIter->pVidPnModeSetInterface->pfnAcquireFirstModeInfo(pIter->hVidPnModeSet, &pCurVidPnModeInfo);
    else
        Status = pIter->pVidPnModeSetInterface->pfnAcquireNextModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo, &pCurVidPnModeInfo);

    if (Status == STATUS_SUCCESS)
    {
        Assert(pCurVidPnModeInfo);

        if (pIter->pCurVidPnModeInfo)
            pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);

        pIter->pCurVidPnModeInfo = pCurVidPnModeInfo;
        return pCurVidPnModeInfo;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET
            || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return NULL;

    WARN(("getting Target info failed %#x", Status));

    pIter->Status = Status;
    return NULL;
}

DECLINLINE(NTSTATUS) NemuVidPnTargetModeIterStatus(NEMUVIDPN_TARGETMODE_ITER *pIter)
{
    return pIter->Status;
}


typedef struct NEMUVIDPN_MONITORMODE_ITER
{
    D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet;
    const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface;
    const D3DKMDT_MONITOR_SOURCE_MODE *pCurVidPnModeInfo;
    NTSTATUS Status;
} NEMUVIDPN_MONITORMODE_ITER;


DECLINLINE(void) NemuVidPnMonitorModeIterInit(NEMUVIDPN_MONITORMODE_ITER *pIter, D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet, const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface)
{
    pIter->hVidPnModeSet = hVidPnModeSet;
    pIter->pVidPnModeSetInterface = pVidPnModeSetInterface;
    pIter->pCurVidPnModeInfo = NULL;
    pIter->Status = STATUS_SUCCESS;
}

DECLINLINE(void) NemuVidPnMonitorModeIterTerm(NEMUVIDPN_MONITORMODE_ITER *pIter)
{
    if (pIter->pCurVidPnModeInfo)
    {
        pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);
        pIter->pCurVidPnModeInfo = NULL;
    }
}

DECLINLINE(const D3DKMDT_MONITOR_SOURCE_MODE *) NemuVidPnMonitorModeIterNext(NEMUVIDPN_MONITORMODE_ITER *pIter)
{
    NTSTATUS Status;
    const D3DKMDT_MONITOR_SOURCE_MODE *pCurVidPnModeInfo;

    if (!pIter->pCurVidPnModeInfo)
        Status = pIter->pVidPnModeSetInterface->pfnAcquireFirstModeInfo(pIter->hVidPnModeSet, &pCurVidPnModeInfo);
    else
        Status = pIter->pVidPnModeSetInterface->pfnAcquireNextModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo, &pCurVidPnModeInfo);

    if (Status == STATUS_SUCCESS)
    {
        Assert(pCurVidPnModeInfo);

        if (pIter->pCurVidPnModeInfo)
            pIter->pVidPnModeSetInterface->pfnReleaseModeInfo(pIter->hVidPnModeSet, pIter->pCurVidPnModeInfo);

        pIter->pCurVidPnModeInfo = pCurVidPnModeInfo;
        return pCurVidPnModeInfo;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET
            || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return NULL;

    WARN(("getting Monitor info failed %#x", Status));

    pIter->Status = Status;
    return NULL;
}

DECLINLINE(NTSTATUS) NemuVidPnMonitorModeIterStatus(NEMUVIDPN_MONITORMODE_ITER *pIter)
{
    return pIter->Status;
}



typedef struct NEMUVIDPN_PATH_ITER
{
    D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface;
    const D3DKMDT_VIDPN_PRESENT_PATH *pCurVidPnPathInfo;
    NTSTATUS Status;
} NEMUVIDPN_PATH_ITER;


DECLINLINE(void) NemuVidPnPathIterInit(NEMUVIDPN_PATH_ITER *pIter, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface)
{
    pIter->hVidPnTopology = hVidPnTopology;
    pIter->pVidPnTopologyInterface = pVidPnTopologyInterface;
    pIter->pCurVidPnPathInfo = NULL;
    pIter->Status = STATUS_SUCCESS;
}

DECLINLINE(void) NemuVidPnPathIterTerm(NEMUVIDPN_PATH_ITER *pIter)
{
    if (pIter->pCurVidPnPathInfo)
    {
        pIter->pVidPnTopologyInterface->pfnReleasePathInfo(pIter->hVidPnTopology, pIter->pCurVidPnPathInfo);
        pIter->pCurVidPnPathInfo = NULL;
    }
}

DECLINLINE(const D3DKMDT_VIDPN_PRESENT_PATH *) NemuVidPnPathIterNext(NEMUVIDPN_PATH_ITER *pIter)
{
    NTSTATUS Status;
    const D3DKMDT_VIDPN_PRESENT_PATH *pCurVidPnPathInfo;

    if (!pIter->pCurVidPnPathInfo)
        Status = pIter->pVidPnTopologyInterface->pfnAcquireFirstPathInfo(pIter->hVidPnTopology, &pCurVidPnPathInfo);
    else
        Status = pIter->pVidPnTopologyInterface->pfnAcquireNextPathInfo(pIter->hVidPnTopology, pIter->pCurVidPnPathInfo, &pCurVidPnPathInfo);

    if (Status == STATUS_SUCCESS)
    {
        Assert(pCurVidPnPathInfo);

        if (pIter->pCurVidPnPathInfo)
            pIter->pVidPnTopologyInterface->pfnReleasePathInfo(pIter->hVidPnTopology, pIter->pCurVidPnPathInfo);

        pIter->pCurVidPnPathInfo = pCurVidPnPathInfo;
        return pCurVidPnPathInfo;
    }

    if (Status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET
            || Status == STATUS_GRAPHICS_DATASET_IS_EMPTY)
        return NULL;

    WARN(("getting Path info failed %#x", Status));

    pIter->Status = Status;
    return NULL;
}

DECLINLINE(NTSTATUS)  NemuVidPnPathIterStatus(NEMUVIDPN_PATH_ITER *pIter)
{
    return pIter->Status;
}

NTSTATUS NemuVidPnRecommendMonitorModes(PNEMUMP_DEVEXT pDevExt, D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
                        D3DKMDT_HMONITORSOURCEMODESET hVidPnModeSet, const DXGK_MONITORSOURCEMODESET_INTERFACE *pVidPnModeSetInterface);

NTSTATUS NemuVidPnRecommendFunctional(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, const NEMUWDDM_RECOMMENDVIDPN *pData);

NTSTATUS NemuVidPnCofuncModality(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE enmPivot, const DXGK_ENUM_PIVOT *pPivot);

NTSTATUS NemuVidPnIsSupported(PNEMUMP_DEVEXT pDevExt, D3DKMDT_HVIDPN hVidPn, BOOLEAN *pfSupported);

NTSTATUS NemuVidPnUpdateModes(PNEMUMP_DEVEXT pDevExt, uint32_t u32TargetId, const RTRECTSIZE *pSize);

#endif /* #ifndef ___NemuMPVidPn_h___ */
