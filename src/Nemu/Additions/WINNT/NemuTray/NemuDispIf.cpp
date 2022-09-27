/* $Id: NemuDispIf.cpp $ */
/** @file
 * NemuTray - Display Settings Interface abstraction for XPDM & WDDM
 */

/*
 * Copyright (C) 2006-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "NemuTray.h"
#define _WIN32_WINNT 0x0601
#include <iprt/log.h>
#include <iprt/err.h>
#include <iprt/assert.h>

#include <malloc.h>

#ifdef DEBUG_misha
#define WARN(_m) do { \
            Assert(0); \
            Log(_m); \
        } while (0)
#else
#define WARN(_m) do { \
            Log(_m); \
        } while (0)
#endif

#ifdef NEMU_WITH_WDDM
#include <iprt/asm.h>
#endif

#include "NemuDisplay.h"

#ifndef NT_SUCCESS
# define NT_SUCCESS(_Status) ((_Status) >= 0)
#endif

typedef struct NEMUDISPIF_OP
{
    PCNEMUDISPIF pIf;
    NEMUDISPKMT_ADAPTER Adapter;
    NEMUDISPKMT_DEVICE Device;
    NEMUDISPKMT_CONTEXT Context;
} NEMUDISPIF_OP;

DWORD EnableAndResizeDispDev(DEVMODE *paDeviceModes, DISPLAY_DEVICE *paDisplayDevices, DWORD totalDispNum, UINT Id, DWORD aWidth, DWORD aHeight,
                                    DWORD aBitsPerPixel, LONG aPosX, LONG aPosY, BOOL fEnabled, BOOL fExtDispSup);

static DWORD nemuDispIfWddmResizeDisplay(PCNEMUDISPIF const pIf, UINT Id, BOOL fEnable, DISPLAY_DEVICE * paDisplayDevices, DEVMODE *paDeviceMode, UINT devModes);

static DWORD nemuDispIfResizePerform(PCNEMUDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes);

static DWORD nemuDispIfWddmEnableDisplaysTryingTopology(PCNEMUDISPIF const pIf, UINT cIds, UINT *pIds, BOOL fEnable);

static DWORD nemuDispIfResizeStartedWDDMOp(NEMUDISPIF_OP *pOp);

/* APIs specific to win7 and above WDDM architecture. Not available for Vista WDDM.
 * This is the reason they have not been put in the NEMUDISPIF struct in NemuDispIf.h
 */
typedef struct _NEMUDISPLAYWDDMAPICONTEXT
{
    LONG (WINAPI * pfnSetDisplayConfig)(UINT numPathArrayElements,DISPLAYCONFIG_PATH_INFO *pathArray,UINT numModeInfoArrayElements,
                                    DISPLAYCONFIG_MODE_INFO *modeInfoArray, UINT Flags);
    LONG (WINAPI * pfnQueryDisplayConfig)(UINT Flags,UINT *pNumPathArrayElements, DISPLAYCONFIG_PATH_INFO *pPathInfoArray,
                                      UINT *pNumModeInfoArrayElements, DISPLAYCONFIG_MODE_INFO *pModeInfoArray,
                                      DISPLAYCONFIG_TOPOLOGY_ID *pCurrentTopologyId);
    LONG (WINAPI * pfnGetDisplayConfigBufferSizes)(UINT Flags, UINT *pNumPathArrayElements, UINT *pNumModeInfoArrayElements);
} _NEMUDISPLAYWDDMAPICONTEXT;

static _NEMUDISPLAYWDDMAPICONTEXT gCtx = {0};

typedef struct NEMUDISPIF_WDDM_DISPCFG
{
    UINT32 cPathInfoArray;
    DISPLAYCONFIG_PATH_INFO *pPathInfoArray;
    UINT32 cModeInfoArray;
    DISPLAYCONFIG_MODE_INFO *pModeInfoArray;
} NEMUDISPIF_WDDM_DISPCFG;

static DWORD nemuDispIfWddmDcCreate(NEMUDISPIF_WDDM_DISPCFG *pCfg, UINT32 fFlags)
{
    UINT32 cPathInfoArray = 0;
    UINT32 cModeInfoArray = 0;
    DISPLAYCONFIG_PATH_INFO *pPathInfoArray;
    DISPLAYCONFIG_MODE_INFO *pModeInfoArray;
    DWORD winEr = gCtx.pfnGetDisplayConfigBufferSizes(fFlags, &cPathInfoArray, &cModeInfoArray);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray: (WDDM) Failed GetDisplayConfigBufferSizes\n"));
        return winEr;
    }

    pPathInfoArray = (DISPLAYCONFIG_PATH_INFO *)malloc(cPathInfoArray * sizeof(DISPLAYCONFIG_PATH_INFO));
    if (!pPathInfoArray)
    {
        WARN(("NemuTray: (WDDM) malloc failed!\n"));
        return ERROR_OUTOFMEMORY;
    }
    pModeInfoArray = (DISPLAYCONFIG_MODE_INFO *)malloc(cModeInfoArray * sizeof(DISPLAYCONFIG_MODE_INFO));
    if (!pModeInfoArray)
    {
        WARN(("NemuTray: (WDDM) malloc failed!\n"));
        free(pPathInfoArray);
        return ERROR_OUTOFMEMORY;
    }

    winEr = gCtx.pfnQueryDisplayConfig(fFlags, &cPathInfoArray, pPathInfoArray, &cModeInfoArray, pModeInfoArray, NULL);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray: (WDDM) Failed QueryDisplayConfig\n"));
        free(pPathInfoArray);
        free(pModeInfoArray);
        return winEr;
    }

    pCfg->cPathInfoArray = cPathInfoArray;
    pCfg->pPathInfoArray = pPathInfoArray;
    pCfg->cModeInfoArray = cModeInfoArray;
    pCfg->pModeInfoArray = pModeInfoArray;
    return ERROR_SUCCESS;
}

static DWORD nemuDispIfWddmDcClone(NEMUDISPIF_WDDM_DISPCFG *pCfg, NEMUDISPIF_WDDM_DISPCFG *pCfgDst)
{
    memset(pCfgDst, 0, sizeof (*pCfgDst));

    if (pCfg->cPathInfoArray)
    {
        pCfgDst->pPathInfoArray = (DISPLAYCONFIG_PATH_INFO *)malloc(pCfg->cPathInfoArray * sizeof (DISPLAYCONFIG_PATH_INFO));
        if (!pCfgDst->pPathInfoArray)
        {
            WARN(("NemuTray: (WDDM) malloc failed!\n"));
            return ERROR_OUTOFMEMORY;
        }

        memcpy(pCfgDst->pPathInfoArray, pCfg->pPathInfoArray, pCfg->cPathInfoArray * sizeof (DISPLAYCONFIG_PATH_INFO));

        pCfgDst->cPathInfoArray = pCfg->cPathInfoArray;
    }

    if (pCfg->cModeInfoArray)
    {
        pCfgDst->pModeInfoArray = (DISPLAYCONFIG_MODE_INFO *)malloc(pCfg->cModeInfoArray * sizeof (DISPLAYCONFIG_MODE_INFO));
        if (!pCfgDst->pModeInfoArray)
        {
            WARN(("NemuTray: (WDDM) malloc failed!\n"));
            if (pCfgDst->pPathInfoArray)
            {
                free(pCfgDst->pPathInfoArray);
                pCfgDst->pPathInfoArray = NULL;
            }
            return ERROR_OUTOFMEMORY;
        }

        memcpy(pCfgDst->pModeInfoArray, pCfg->pModeInfoArray, pCfg->cModeInfoArray * sizeof (DISPLAYCONFIG_MODE_INFO));

        pCfgDst->cModeInfoArray = pCfg->cModeInfoArray;
    }

    return ERROR_SUCCESS;
}


static VOID nemuDispIfWddmDcTerm(NEMUDISPIF_WDDM_DISPCFG *pCfg)
{
    if (pCfg->pPathInfoArray)
        free(pCfg->pPathInfoArray);
    if (pCfg->pModeInfoArray)
        free(pCfg->pModeInfoArray);
    /* sanity */
    memset(pCfg, 0, sizeof (*pCfg));
}

static UINT32 g_cNemuDispIfWddmDisplays = 0;
static DWORD nemuDispIfWddmDcQueryNumDisplays(UINT32 *pcDisplays)
{
    if (!g_cNemuDispIfWddmDisplays)
    {
        NEMUDISPIF_WDDM_DISPCFG DispCfg;
        *pcDisplays = 0;
        DWORD winEr = nemuDispIfWddmDcCreate(&DispCfg, QDC_ALL_PATHS);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("NemuTray:(WDDM) nemuDispIfWddmDcCreate Failed winEr %d\n", winEr));
            return winEr;
        }

        int cDisplays = -1;

        for (UINT iter = 0; iter < DispCfg.cPathInfoArray; ++iter)
        {
            if (cDisplays < (int)(DispCfg.pPathInfoArray[iter].sourceInfo.id))
                cDisplays = (int)(DispCfg.pPathInfoArray[iter].sourceInfo.id);
        }

        cDisplays++;

        g_cNemuDispIfWddmDisplays = cDisplays;
        Assert(g_cNemuDispIfWddmDisplays);

        nemuDispIfWddmDcTerm(&DispCfg);
    }

    *pcDisplays = g_cNemuDispIfWddmDisplays;
    return ERROR_SUCCESS;
}

static int nemuDispIfWddmDcSearchPath(NEMUDISPIF_WDDM_DISPCFG *pCfg, UINT srcId, UINT trgId)
{
    for (UINT iter = 0; iter < pCfg->cPathInfoArray; ++iter)
    {
        if ((srcId == ~0UL || pCfg->pPathInfoArray[iter].sourceInfo.id == srcId)
                && (trgId == ~0UL || pCfg->pPathInfoArray[iter].targetInfo.id == trgId))
        {
            return (int)iter;
        }
    }
    return -1;
}

static int nemuDispIfWddmDcSearchActivePath(NEMUDISPIF_WDDM_DISPCFG *pCfg, UINT srcId, UINT trgId)
{
    int idx = nemuDispIfWddmDcSearchPath(pCfg, srcId, trgId);
    if (idx < 0)
        return idx;

    if (!(pCfg->pPathInfoArray[idx].flags & DISPLAYCONFIG_PATH_ACTIVE))
        return -1;

    return idx;
}

static VOID nemuDispIfWddmDcSettingsInvalidateModeIndex(NEMUDISPIF_WDDM_DISPCFG *pCfg, int idx)
{
    pCfg->pPathInfoArray[idx].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    pCfg->pPathInfoArray[idx].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
}

static VOID nemuDispIfWddmDcSettingsInvalidateModeIndeces(NEMUDISPIF_WDDM_DISPCFG *pCfg)
{
    for (UINT iter = 0; iter < pCfg->cPathInfoArray; ++iter)
    {
        nemuDispIfWddmDcSettingsInvalidateModeIndex(pCfg, (int)iter);
    }

    if (pCfg->pModeInfoArray)
    {
        free(pCfg->pModeInfoArray);
        pCfg->pModeInfoArray = NULL;
    }
    pCfg->cModeInfoArray = 0;
}

static DWORD nemuDispIfWddmDcSettingsModeAdd(NEMUDISPIF_WDDM_DISPCFG *pCfg, UINT *pIdx)
{
    UINT32 cModeInfoArray = pCfg->cModeInfoArray + 1;
    DISPLAYCONFIG_MODE_INFO *pModeInfoArray = (DISPLAYCONFIG_MODE_INFO *)malloc(cModeInfoArray * sizeof (DISPLAYCONFIG_MODE_INFO));
    if (!pModeInfoArray)
    {
        WARN(("NemuTray: (WDDM) malloc failed!\n"));
        return ERROR_OUTOFMEMORY;
    }

    memcpy (pModeInfoArray, pCfg->pModeInfoArray, pCfg->cModeInfoArray * sizeof(DISPLAYCONFIG_MODE_INFO));
    memset(&pModeInfoArray[cModeInfoArray-1], 0, sizeof (pModeInfoArray[0]));
    free(pCfg->pModeInfoArray);
    *pIdx = cModeInfoArray-1;
    pCfg->pModeInfoArray = pModeInfoArray;
    pCfg->cModeInfoArray = cModeInfoArray;
    return ERROR_SUCCESS;
}

static DWORD nemuDispIfWddmDcSettingsUpdate(NEMUDISPIF_WDDM_DISPCFG *pCfg, int idx, DEVMODE *pDeviceMode, BOOL fInvalidateSrcMode, BOOL fEnable)
{
    UINT Id = pCfg->pPathInfoArray[idx].sourceInfo.id;

    if (fInvalidateSrcMode)
        pCfg->pPathInfoArray[idx].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    else if (pDeviceMode)
    {
        UINT iSrcMode = pCfg->pPathInfoArray[idx].sourceInfo.modeInfoIdx;
        if (iSrcMode == DISPLAYCONFIG_PATH_MODE_IDX_INVALID)
        {

            WARN(("NemuTray: (WDDM) no source mode index specified"));
            DWORD winEr = nemuDispIfWddmDcSettingsModeAdd(pCfg, &iSrcMode);
            if (winEr != ERROR_SUCCESS)
            {
                WARN(("NemuTray:(WDDM) nemuDispIfWddmDcSettingsModeAdd Failed winEr %d\n", winEr));
                return winEr;
            }
            pCfg->pPathInfoArray[idx].sourceInfo.modeInfoIdx = iSrcMode;
        }

        for (int i = 0; i < (int)pCfg->cPathInfoArray; ++i)
        {
            if (i == idx)
                continue;

            if (pCfg->pPathInfoArray[i].sourceInfo.modeInfoIdx == iSrcMode)
            {
                /* this is something we're not expecting/supporting */
                WARN(("NemuTray: (WDDM) multiple paths have the same mode index"));
                return ERROR_NOT_SUPPORTED;
            }
        }

        if (pDeviceMode->dmFields & DM_PELSWIDTH)
            pCfg->pModeInfoArray[iSrcMode].sourceMode.width = pDeviceMode->dmPelsWidth;
        if (pDeviceMode->dmFields & DM_PELSHEIGHT)
            pCfg->pModeInfoArray[iSrcMode].sourceMode.height = pDeviceMode->dmPelsHeight;
        if (pDeviceMode->dmFields & DM_POSITION)
        {
            LogFlowFunc(("DM_POSITION %d,%d -> %d,%d\n",
                         pCfg->pModeInfoArray[iSrcMode].sourceMode.position.x,
                         pCfg->pModeInfoArray[iSrcMode].sourceMode.position.y,
                         pDeviceMode->dmPosition.x, pDeviceMode->dmPosition.y));
            pCfg->pModeInfoArray[iSrcMode].sourceMode.position.x = pDeviceMode->dmPosition.x;
            pCfg->pModeInfoArray[iSrcMode].sourceMode.position.y = pDeviceMode->dmPosition.y;
        }
        if (pDeviceMode->dmFields & DM_BITSPERPEL)
        {
            switch (pDeviceMode->dmBitsPerPel)
            {
                case 32:
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                    break;
                case 24:
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_24BPP;
                    break;
                case 16:
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_16BPP;
                    break;
                case 8:
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_8BPP;
                    break;
                default:
                    LogRel(("NemuTray: (WDDM) invalid bpp %d, using 32\n", pDeviceMode->dmBitsPerPel));
                    pCfg->pModeInfoArray[iSrcMode].sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
                    break;
            }
        }
    }

    pCfg->pPathInfoArray[idx].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;

    if (fEnable)
        pCfg->pPathInfoArray[idx].flags |= DISPLAYCONFIG_PATH_ACTIVE;
    else
        pCfg->pPathInfoArray[idx].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;

    return ERROR_SUCCESS;
}

static DWORD nemuDispIfWddmDcSet(NEMUDISPIF_WDDM_DISPCFG *pCfg, UINT fFlags)
{
    DWORD winEr = gCtx.pfnSetDisplayConfig(pCfg->cPathInfoArray, pCfg->pPathInfoArray, pCfg->cModeInfoArray, pCfg->pModeInfoArray, fFlags);
    if (winEr != ERROR_SUCCESS)
        Log(("NemuTray:(WDDM) pfnSetDisplayConfig Failed for Flags 0x%x\n", fFlags));
    return winEr;
}

static BOOL nemuDispIfWddmDcSettingsAdjustSupportedPaths(NEMUDISPIF_WDDM_DISPCFG *pCfg)
{
    BOOL fAdjusted = FALSE;
    for (UINT iter = 0; iter < pCfg->cPathInfoArray; ++iter)
    {
        if (pCfg->pPathInfoArray[iter].sourceInfo.id == pCfg->pPathInfoArray[iter].targetInfo.id)
            continue;

        if (!(pCfg->pPathInfoArray[iter].flags & DISPLAYCONFIG_PATH_ACTIVE))
            continue;

        pCfg->pPathInfoArray[iter].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
        fAdjusted = TRUE;
    }

    return fAdjusted;
}

static void nemuDispIfWddmDcSettingsAttachDisbledToPrimary(NEMUDISPIF_WDDM_DISPCFG *pCfg)
{
    for (UINT iter = 0; iter < pCfg->cPathInfoArray; ++iter)
    {
        if ((pCfg->pPathInfoArray[iter].flags & DISPLAYCONFIG_PATH_ACTIVE))
            continue;

        pCfg->pPathInfoArray[iter].sourceInfo.id = 0;
        pCfg->pPathInfoArray[iter].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        pCfg->pPathInfoArray[iter].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    }
}

static DWORD nemuDispIfWddmDcSettingsIncludeAllTargets(NEMUDISPIF_WDDM_DISPCFG *pCfg)
{
    UINT32 cDisplays = 0;
    NEMUDISPIF_WDDM_DISPCFG AllCfg;
    BOOL fAllCfgInited = FALSE;

    DWORD winEr = nemuDispIfWddmDcQueryNumDisplays(&cDisplays);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray:(WDDM) nemuDispIfWddmDcQueryNumDisplays Failed winEr %d\n", winEr));
        return winEr;
    }

    DISPLAYCONFIG_PATH_INFO *pPathInfoArray = (DISPLAYCONFIG_PATH_INFO *)malloc(cDisplays * sizeof(DISPLAYCONFIG_PATH_INFO));
    if (!pPathInfoArray)
    {
        WARN(("malloc failed\n"));
        return ERROR_OUTOFMEMORY;
    }

    for (UINT i = 0; i < cDisplays; ++i)
    {
        int idx = nemuDispIfWddmDcSearchPath(pCfg, i, i);
        if (idx < 0)
        {
            idx = nemuDispIfWddmDcSearchPath(pCfg, -1, i);
            if (idx >= 0)
            {
                WARN(("NemuTray:(WDDM) different source and target paare enabled, this is something we would not expect\n"));
            }
        }

        if (idx >= 0)
            pPathInfoArray[i] = pCfg->pPathInfoArray[idx];
        else
        {
            if (!fAllCfgInited)
            {
                winEr = nemuDispIfWddmDcCreate(&AllCfg, QDC_ALL_PATHS);
                if (winEr != ERROR_SUCCESS)
                {
                    WARN(("NemuTray:(WDDM) nemuDispIfWddmDcCreate Failed winEr %d\n", winEr));
                    free(pPathInfoArray);
                    return winEr;
                }
                fAllCfgInited = TRUE;
            }

            idx = nemuDispIfWddmDcSearchPath(&AllCfg, i, i);
            if (idx < 0)
            {
                WARN(("NemuTray:(WDDM) %d %d path not supported\n", i, i));
                idx = nemuDispIfWddmDcSearchPath(pCfg, -1, i);
                if (idx < 0)
                {
                    WARN(("NemuTray:(WDDM) %d %d path not supported\n", -1, i));
                }
            }

            if (idx >= 0)
            {
                pPathInfoArray[i] = AllCfg.pPathInfoArray[idx];

                if (pPathInfoArray[i].flags & DISPLAYCONFIG_PATH_ACTIVE)
                {
                    WARN(("NemuTray:(WDDM) disabled path %d %d is marked active\n",
                            pPathInfoArray[i].sourceInfo.id, pPathInfoArray[i].targetInfo.id));
                    pPathInfoArray[i].flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
                }

                Assert(pPathInfoArray[i].sourceInfo.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID);
                Assert(pPathInfoArray[i].sourceInfo.statusFlags == 0);

                Assert(pPathInfoArray[i].targetInfo.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID);
                Assert(pPathInfoArray[i].targetInfo.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15);
                Assert(pPathInfoArray[i].targetInfo.rotation == DISPLAYCONFIG_ROTATION_IDENTITY);
                Assert(pPathInfoArray[i].targetInfo.scaling == DISPLAYCONFIG_SCALING_PREFERRED);
                Assert(pPathInfoArray[i].targetInfo.refreshRate.Numerator == 0);
                Assert(pPathInfoArray[i].targetInfo.refreshRate.Denominator == 0);
                Assert(pPathInfoArray[i].targetInfo.scanLineOrdering == DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED);
                Assert(pPathInfoArray[i].targetInfo.targetAvailable == TRUE);
                Assert(pPathInfoArray[i].targetInfo.statusFlags == DISPLAYCONFIG_TARGET_FORCIBLE);

                Assert(pPathInfoArray[i].flags == 0);
            }
            else
            {
                pPathInfoArray[i].sourceInfo.adapterId = pCfg->pPathInfoArray[0].sourceInfo.adapterId;
                pPathInfoArray[i].sourceInfo.id = i;
                pPathInfoArray[i].sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
                pPathInfoArray[i].sourceInfo.statusFlags = 0;

                pPathInfoArray[i].targetInfo.adapterId = pPathInfoArray[i].sourceInfo.adapterId;
                pPathInfoArray[i].targetInfo.id = i;
                pPathInfoArray[i].targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
                pPathInfoArray[i].targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15;
                pPathInfoArray[i].targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
                pPathInfoArray[i].targetInfo.scaling = DISPLAYCONFIG_SCALING_PREFERRED;
                pPathInfoArray[i].targetInfo.refreshRate.Numerator = 0;
                pPathInfoArray[i].targetInfo.refreshRate.Denominator = 0;
                pPathInfoArray[i].targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;
                pPathInfoArray[i].targetInfo.targetAvailable = TRUE;
                pPathInfoArray[i].targetInfo.statusFlags = DISPLAYCONFIG_TARGET_FORCIBLE;

                pPathInfoArray[i].flags = 0;
            }
        }
    }

    free(pCfg->pPathInfoArray);
    pCfg->pPathInfoArray = pPathInfoArray;
    pCfg->cPathInfoArray = cDisplays;
    if (fAllCfgInited)
        nemuDispIfWddmDcTerm(&AllCfg);

    return ERROR_SUCCESS;
}

static DWORD nemuDispIfOpBegin(PCNEMUDISPIF pIf, NEMUDISPIF_OP *pOp)
{
    pOp->pIf = pIf;

    HRESULT hr = nemuDispKmtOpenAdapter(&pIf->modeData.wddm.KmtCallbacks, &pOp->Adapter);
    if (SUCCEEDED(hr))
    {
        hr = nemuDispKmtCreateDevice(&pOp->Adapter, &pOp->Device);
        if (SUCCEEDED(hr))
        {
            hr = nemuDispKmtCreateContext(&pOp->Device, &pOp->Context, NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_RESIZE,
                    0, 0, NULL, 0ULL);
            if (SUCCEEDED(hr))
                return ERROR_SUCCESS;
            else
                WARN(("NemuTray: nemuDispKmtCreateContext failed hr 0x%x", hr));

            nemuDispKmtDestroyDevice(&pOp->Device);
        }
        else
            WARN(("NemuTray: nemuDispKmtCreateDevice failed hr 0x%x", hr));

        nemuDispKmtCloseAdapter(&pOp->Adapter);
    }

    return hr;
}

static VOID nemuDispIfOpEnd(NEMUDISPIF_OP *pOp)
{
    nemuDispKmtDestroyContext(&pOp->Context);
    nemuDispKmtDestroyDevice(&pOp->Device);
    nemuDispKmtCloseAdapter(&pOp->Adapter);
}

/* display driver interface abstraction for XPDM & WDDM
 * with WDDM we can not use ExtEscape to communicate with our driver
 * because we do not have XPDM display driver any more, i.e. escape requests are handled by cdd
 * that knows nothing about us */
DWORD NemuDispIfInit(PNEMUDISPIF pIf)
{
    pIf->enmMode = NEMUDISPIF_MODE_XPDM;
    return NO_ERROR;
}

#ifdef NEMU_WITH_WDDM
static void nemuDispIfWddmTerm(PCNEMUDISPIF pIf);
static DWORD nemuDispIfWddmInit(PCNEMUDISPIF pIf);
#endif

DWORD NemuDispIfTerm(PNEMUDISPIF pIf)
{
#ifdef NEMU_WITH_WDDM
    if (pIf->enmMode >= NEMUDISPIF_MODE_WDDM)
    {
        nemuDispIfWddmTerm(pIf);

        nemuDispKmtCallbacksTerm(&pIf->modeData.wddm.KmtCallbacks);
    }
#endif

    pIf->enmMode = NEMUDISPIF_MODE_UNKNOWN;
    return NO_ERROR;
}

static DWORD nemuDispIfEscapeXPDM(PCNEMUDISPIF pIf, PNEMUDISPIFESCAPE pEscape, int cbData, int iDirection)
{
    HDC  hdc = GetDC(HWND_DESKTOP);
    VOID *pvData = cbData ? NEMUDISPIFESCAPE_DATA(pEscape, VOID) : NULL;
    int iRet = ExtEscape(hdc, pEscape->escapeCode,
            iDirection >= 0 ? cbData : 0,
            iDirection >= 0 ? (LPSTR)pvData : NULL,
            iDirection <= 0 ? cbData : 0,
            iDirection <= 0 ? (LPSTR)pvData : NULL);
    ReleaseDC(HWND_DESKTOP, hdc);
    if (iRet > 0)
        return VINF_SUCCESS;
    else if (iRet == 0)
        return ERROR_NOT_SUPPORTED;
    /* else */
    return ERROR_GEN_FAILURE;
}

#ifdef NEMU_WITH_WDDM
static DWORD nemuDispIfSwitchToWDDM(PNEMUDISPIF pIf)
{
    DWORD err = NO_ERROR;
    OSVERSIONINFO OSinfo;
    OSinfo.dwOSVersionInfoSize = sizeof (OSinfo);
    GetVersionEx (&OSinfo);
    bool bSupported = true;

    if (OSinfo.dwMajorVersion >= 6)
    {
        Log((__FUNCTION__": this is vista and up\n"));
        HMODULE hUser = GetModuleHandle("user32.dll");
        if (hUser)
        {
            *(uintptr_t *)&pIf->modeData.wddm.pfnChangeDisplaySettingsEx = (uintptr_t)GetProcAddress(hUser, "ChangeDisplaySettingsExA");
            Log((__FUNCTION__": NemuDisplayInit: pfnChangeDisplaySettingsEx = %p\n", pIf->modeData.wddm.pfnChangeDisplaySettingsEx));
            bSupported &= !!(pIf->modeData.wddm.pfnChangeDisplaySettingsEx);

            *(uintptr_t *)&pIf->modeData.wddm.pfnEnumDisplayDevices = (uintptr_t)GetProcAddress(hUser, "EnumDisplayDevicesA");
            Log((__FUNCTION__": NemuDisplayInit: pfnEnumDisplayDevices = %p\n", pIf->modeData.wddm.pfnEnumDisplayDevices));
            bSupported &= !!(pIf->modeData.wddm.pfnEnumDisplayDevices);
            /* for win 7 and above */
             if (OSinfo.dwMinorVersion >= 1)
            {
                *(uintptr_t *)&gCtx.pfnSetDisplayConfig = (uintptr_t)GetProcAddress(hUser, "SetDisplayConfig");
                Log((__FUNCTION__": NemuDisplayInit: pfnSetDisplayConfig = %p\n", gCtx.pfnSetDisplayConfig));
                bSupported &= !!(gCtx.pfnSetDisplayConfig);

                *(uintptr_t *)&gCtx.pfnQueryDisplayConfig = (uintptr_t)GetProcAddress(hUser, "QueryDisplayConfig");
                Log((__FUNCTION__": NemuDisplayInit: pfnQueryDisplayConfig = %p\n", gCtx.pfnQueryDisplayConfig));
                bSupported &= !!(gCtx.pfnQueryDisplayConfig);

                *(uintptr_t *)&gCtx.pfnGetDisplayConfigBufferSizes = (uintptr_t)GetProcAddress(hUser, "GetDisplayConfigBufferSizes");
                Log((__FUNCTION__": NemuDisplayInit: pfnGetDisplayConfigBufferSizes = %p\n", gCtx.pfnGetDisplayConfigBufferSizes));
                bSupported &= !!(gCtx.pfnGetDisplayConfigBufferSizes);
            }

            /* this is vista and up */
            HRESULT hr = nemuDispKmtCallbacksInit(&pIf->modeData.wddm.KmtCallbacks);
            if (FAILED(hr))
            {
                WARN(("NemuTray: nemuDispKmtCallbacksInit failed hr 0x%x\n", hr));
                err = hr;
            }
        }
        else
        {
            WARN((__FUNCTION__": GetModuleHandle(USER32) failed, err(%d)\n", GetLastError()));
            err = ERROR_NOT_SUPPORTED;
        }
    }
    else
    {
        WARN((__FUNCTION__": can not switch to NEMUDISPIF_MODE_WDDM, because os is not Vista or upper\n"));
        err = ERROR_NOT_SUPPORTED;
    }

    if (err == ERROR_SUCCESS)
    {
        err = nemuDispIfWddmInit(pIf);
    }

    return err;
}

static DWORD nemuDispIfSwitchToWDDM_W7(PNEMUDISPIF pIf)
{
    return nemuDispIfSwitchToWDDM(pIf);
}

static DWORD nemuDispIfWDDMAdpHdcCreate(int iDisplay, HDC *phDc, DISPLAY_DEVICE *pDev)
{
    DWORD winEr = ERROR_INVALID_STATE;
    memset(pDev, 0, sizeof (*pDev));
    pDev->cb = sizeof (*pDev);

    for (int i = 0; ; ++i)
    {
        if (EnumDisplayDevices(NULL, /* LPCTSTR lpDevice */ i, /* DWORD iDevNum */
                pDev, 0 /* DWORD dwFlags*/))
        {
            if (i == iDisplay || (iDisplay < 0 && pDev->StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
            {
                HDC hDc = CreateDC(NULL, pDev->DeviceName, NULL, NULL);
                if (hDc)
                {
                    *phDc = hDc;
                    return NO_ERROR;
                }
                else
                {
                    winEr = GetLastError();
                    WARN(("CreateDC failed %d", winEr));
                    break;
                }
            }
            Log(("display data no match display(%d): i(%d), flags(%d)", iDisplay, i, pDev->StateFlags));
        }
        else
        {
            winEr = GetLastError();
            WARN(("EnumDisplayDevices failed %d", winEr));
            break;
        }
    }

    WARN(("nemuDispIfWDDMAdpHdcCreate failure branch %d", winEr));
    return winEr;
}

static DWORD nemuDispIfEscapeWDDM(PCNEMUDISPIF pIf, PNEMUDISPIFESCAPE pEscape, int cbData, BOOL fHwAccess)
{
    DWORD winEr = ERROR_SUCCESS;
    NEMUDISPKMT_ADAPTER Adapter;
    HRESULT hr = nemuDispKmtOpenAdapter(&pIf->modeData.wddm.KmtCallbacks, &Adapter);
    if (!SUCCEEDED(hr))
    {
        WARN(("NemuTray: nemuDispKmtOpenAdapter failed hr 0x%x\n", hr));
        return hr;
    }

    D3DKMT_ESCAPE EscapeData = {0};
    EscapeData.hAdapter = Adapter.hAdapter;
    //EscapeData.hDevice = NULL;
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    if (fHwAccess)
        EscapeData.Flags.HardwareAccess = 1;
    EscapeData.pPrivateDriverData = pEscape;
    EscapeData.PrivateDriverDataSize = NEMUDISPIFESCAPE_SIZE(cbData);
    //EscapeData.hContext = NULL;

    NTSTATUS Status = pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
        winEr = ERROR_SUCCESS;
    else
    {
        WARN(("NemuTray: pfnD3DKMTEscape failed Status 0x%x\n", Status));
        winEr = ERROR_GEN_FAILURE;
    }

    nemuDispKmtCloseAdapter(&Adapter);

    return winEr;
}
#endif

DWORD NemuDispIfEscape(PCNEMUDISPIF pIf, PNEMUDISPIFESCAPE pEscape, int cbData)
{
    switch (pIf->enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
        case NEMUDISPIF_MODE_XPDM:
            return nemuDispIfEscapeXPDM(pIf, pEscape, cbData, 1);
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        case NEMUDISPIF_MODE_WDDM_W7:
            return nemuDispIfEscapeWDDM(pIf, pEscape, cbData, TRUE /* BOOL fHwAccess */);
#endif
        default:
            Log((__FUNCTION__": unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

DWORD NemuDispIfEscapeInOut(PCNEMUDISPIF const pIf, PNEMUDISPIFESCAPE pEscape, int cbData)
{
    switch (pIf->enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
        case NEMUDISPIF_MODE_XPDM:
            return nemuDispIfEscapeXPDM(pIf, pEscape, cbData, 0);
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        case NEMUDISPIF_MODE_WDDM_W7:
            return nemuDispIfEscapeWDDM(pIf, pEscape, cbData, TRUE /* BOOL fHwAccess */);
#endif
        default:
            Log((__FUNCTION__": unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

#ifdef NEMU_WITH_WDDM

#define NEMURR_TIMER_ID 1234

typedef struct NEMURR
{
    HANDLE hThread;
    DWORD idThread;
    HANDLE hEvent;
    HWND hWnd;
    CRITICAL_SECTION CritSect;
    UINT_PTR idTimer;
    PCNEMUDISPIF pIf;
    UINT iChangedMode;
    BOOL fEnable;
    BOOL fExtDispSup;
    DISPLAY_DEVICE *paDisplayDevices;
    DEVMODE *paDeviceModes;
    UINT cDevModes;
} NEMURR, *PNEMURR;

static NEMURR g_NemuRr = {0};

#define NEMU_E_INSUFFICIENT_BUFFER HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)
#define NEMU_E_NOT_SUPPORTED HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)

static void nemuRrRetryStopLocked()
{
    PNEMURR pMon = &g_NemuRr;
    if (pMon->pIf)
    {
        if (pMon->paDisplayDevices)
        {
            free(pMon->paDisplayDevices);
            pMon->paDisplayDevices = NULL;
        }

        if (pMon->paDeviceModes)
        {
            free(pMon->paDeviceModes);
            pMon->paDeviceModes = NULL;
        }

        if (pMon->idTimer)
        {
            KillTimer(pMon->hWnd, pMon->idTimer);
            pMon->idTimer = 0;
        }

        pMon->cDevModes = 0;
        pMon->pIf = NULL;
    }
}

static void NemuRrRetryStop()
{
    PNEMURR pMon = &g_NemuRr;
    EnterCriticalSection(&pMon->CritSect);
    nemuRrRetryStopLocked();
    LeaveCriticalSection(&pMon->CritSect);
}

//static DWORD nemuDispIfWddmValidateFixResize(PCNEMUDISPIF const pIf, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes);

static void nemuRrRetryReschedule()
{
}

static void NemuRrRetrySchedule(PCNEMUDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes)
{
    PNEMURR pMon = &g_NemuRr;
    EnterCriticalSection(&pMon->CritSect);
    nemuRrRetryStopLocked();

    pMon->pIf = pIf;
    pMon->iChangedMode = iChangedMode;
    pMon->fEnable = fEnable;
    pMon->fExtDispSup = fExtDispSup;

    if (cDevModes)
    {
        pMon->paDisplayDevices = (DISPLAY_DEVICE*)malloc(sizeof (*paDisplayDevices) * cDevModes);
        Assert(pMon->paDisplayDevices);
        if (!pMon->paDisplayDevices)
        {
            Log(("malloc failed!"));
            nemuRrRetryStopLocked();
            LeaveCriticalSection(&pMon->CritSect);
            return;
        }
        memcpy(pMon->paDisplayDevices, paDisplayDevices, sizeof (*paDisplayDevices) * cDevModes);

        pMon->paDeviceModes = (DEVMODE*)malloc(sizeof (*paDeviceModes) * cDevModes);
        Assert(pMon->paDeviceModes);
        if (!pMon->paDeviceModes)
        {
            Log(("malloc failed!"));
            nemuRrRetryStopLocked();
            LeaveCriticalSection(&pMon->CritSect);
            return;
        }
        memcpy(pMon->paDeviceModes, paDeviceModes, sizeof (*paDeviceModes) * cDevModes);
    }
    pMon->cDevModes = cDevModes;

    pMon->idTimer = SetTimer(pMon->hWnd, NEMURR_TIMER_ID, 1000, (TIMERPROC)NULL);
    Assert(pMon->idTimer);
    if (!pMon->idTimer)
    {
        WARN(("NemuTray: SetTimer failed!, err %d\n", GetLastError()));
        nemuRrRetryStopLocked();
    }

    LeaveCriticalSection(&pMon->CritSect);
}

static void nemuRrRetryPerform()
{
    PNEMURR pMon = &g_NemuRr;
    EnterCriticalSection(&pMon->CritSect);
    if (pMon->pIf)
    {
        DWORD dwErr = nemuDispIfResizePerform(pMon->pIf, pMon->iChangedMode, pMon->fEnable, pMon->fExtDispSup, pMon->paDisplayDevices, pMon->paDeviceModes, pMon->cDevModes);
        if (ERROR_RETRY != dwErr)
            NemuRrRetryStop();
        else
            nemuRrRetryReschedule();
    }
    LeaveCriticalSection(&pMon->CritSect);
}

static LRESULT CALLBACK nemuRrWndProc(HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
)
{
    switch(uMsg)
    {
        case WM_DISPLAYCHANGE:
        {
            Log(("NemuTray: WM_DISPLAYCHANGE\n"));
            NemuRrRetryStop();
            return 0;
        }
        case WM_TIMER:
        {
            if (wParam == NEMURR_TIMER_ID)
            {
                Log(("NemuTray: NEMURR_TIMER_ID\n"));
                nemuRrRetryPerform();
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            Log((__FUNCTION__": got WM_CLOSE for hwnd(0x%x)", hwnd));
            return 0;
        case WM_DESTROY:
            Log((__FUNCTION__": got WM_DESTROY for hwnd(0x%x)", hwnd));
            return 0;
        case WM_NCHITTEST:
            Log((__FUNCTION__": got WM_NCHITTEST for hwnd(0x%x)\n", hwnd));
            return HTNOWHERE;
        default:
            break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#define NEMURRWND_NAME "NemuRrWnd"

static HRESULT nemuRrWndCreate(HWND *phWnd)
{
    HRESULT hr = S_OK;
    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);
    /* Register the Window Class. */
    WNDCLASS wc;
    if (!GetClassInfo(hInstance, NEMURRWND_NAME, &wc))
    {
        wc.style = 0;//CS_OWNDC;
        wc.lpfnWndProc = nemuRrWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = NULL;
        wc.hCursor = NULL;
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = NEMURRWND_NAME;
        if (!RegisterClass(&wc))
        {
            DWORD winErr = GetLastError();
            WARN((__FUNCTION__": RegisterClass failed, winErr(%d)\n", winErr));
            hr = E_FAIL;
        }
    }

    if (hr == S_OK)
    {
        HWND hWnd = CreateWindowEx (WS_EX_TOOLWINDOW,
                                        NEMURRWND_NAME, NEMURRWND_NAME,
                                        WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_DISABLED,
                                        -100, -100,
                                        10, 10,
                                        NULL, //GetDesktopWindow() /* hWndParent */,
                                        NULL /* hMenu */,
                                        hInstance,
                                        NULL /* lpParam */);
        Assert(hWnd);
        if (hWnd)
        {
            *phWnd = hWnd;
        }
        else
        {
            DWORD winErr = GetLastError();
            WARN((__FUNCTION__": CreateWindowEx failed, winErr(%d)\n", winErr));
            hr = E_FAIL;
        }
    }

    return hr;
}

static HRESULT nemuRrWndDestroy(HWND hWnd)
{
    BOOL bResult = DestroyWindow(hWnd);
    if (bResult)
        return S_OK;

    DWORD winErr = GetLastError();
    WARN((__FUNCTION__": DestroyWindow failed, winErr(%d) for hWnd(0x%x)\n", winErr, hWnd));

    return HRESULT_FROM_WIN32(winErr);
}

static HRESULT nemuRrWndInit()
{
    PNEMURR pMon = &g_NemuRr;
    return nemuRrWndCreate(&pMon->hWnd);
}

HRESULT nemuRrWndTerm()
{
    PNEMURR pMon = &g_NemuRr;
    HRESULT tmpHr = nemuRrWndDestroy(pMon->hWnd);
    Assert(tmpHr == S_OK);

    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);
    UnregisterClass(NEMURRWND_NAME, hInstance);

    return S_OK;
}

#define WM_NEMURR_INIT_QUIT (WM_APP+2)

HRESULT nemuRrRun()
{
    PNEMURR pMon = &g_NemuRr;
    MSG Msg;

    HRESULT hr = S_FALSE;

    PeekMessage(&Msg,
            NULL /* HWND hWnd */,
            WM_USER /* UINT wMsgFilterMin */,
            WM_USER /* UINT wMsgFilterMax */,
            PM_NOREMOVE);

    do
    {
        BOOL bResult = GetMessage(&Msg,
            0 /*HWND hWnd*/,
            0 /*UINT wMsgFilterMin*/,
            0 /*UINT wMsgFilterMax*/
            );

        if(!bResult) /* WM_QUIT was posted */
        {
            hr = S_FALSE;
            Log(("NemuTray: GetMessage returned FALSE\n"));
            NemuRrRetryStop();
            break;
        }

        if(bResult == -1) /* error occurred */
        {
            DWORD winEr = GetLastError();
            hr = HRESULT_FROM_WIN32(winEr);
            /* just ensure we never return success in this case */
            Assert(hr != S_OK);
            Assert(hr != S_FALSE);
            if (hr == S_OK || hr == S_FALSE)
                hr = E_FAIL;
            WARN(("NemuTray: GetMessage returned -1, err %d\n", winEr));
            NemuRrRetryStop();
            break;
        }

        switch (Msg.message)
        {
            case WM_NEMURR_INIT_QUIT:
            case WM_CLOSE:
            {
                Log(("NemuTray: closing Rr %d\n", Msg.message));
                NemuRrRetryStop();
                PostQuitMessage(0);
                break;
            }
            default:
                TranslateMessage(&Msg);
                DispatchMessage(&Msg);
                break;
        }
    } while (1);
    return 0;
}

static DWORD WINAPI nemuRrRunnerThread(void *pvUser)
{
    PNEMURR pMon = &g_NemuRr;

    BOOL bRc = SetEvent(pMon->hEvent);
    if (!bRc)
    {
        DWORD winErr = GetLastError();
        WARN((__FUNCTION__": SetEvent failed, winErr = (%d)", winErr));
        HRESULT tmpHr = HRESULT_FROM_WIN32(winErr);
        Assert(tmpHr != S_OK);
    }

    HRESULT hr = nemuRrWndInit();
    Assert(hr == S_OK);
    if (hr == S_OK)
    {
        hr = nemuRrRun();
        Assert(hr == S_OK);

        nemuRrWndTerm();
    }

    return 0;
}

HRESULT NemuRrInit()
{
    HRESULT hr = E_FAIL;
    PNEMURR pMon = &g_NemuRr;
    memset(pMon, 0, sizeof (*pMon));

    InitializeCriticalSection(&pMon->CritSect);

    pMon->hEvent = CreateEvent(NULL, /* LPSECURITY_ATTRIBUTES lpEventAttributes*/
            TRUE, /* BOOL bManualReset*/
            FALSE, /* BOOL bInitialState */
            NULL /* LPCTSTR lpName */
          );
    if (pMon->hEvent)
    {
        pMon->hThread = CreateThread(NULL /* LPSECURITY_ATTRIBUTES lpThreadAttributes */,
                                              0 /* SIZE_T dwStackSize */,
                                              nemuRrRunnerThread,
                                              pMon,
                                              0 /* DWORD dwCreationFlags */,
                                              &pMon->idThread);
        if (pMon->hThread)
        {
            DWORD dwResult = WaitForSingleObject(pMon->hEvent, INFINITE);
            if (dwResult == WAIT_OBJECT_0)
                return S_OK;
            else
            {
                Log(("WaitForSingleObject failed!"));
                hr = E_FAIL;
            }
        }
        else
        {
            DWORD winErr = GetLastError();
            WARN((__FUNCTION__": CreateThread failed, winErr = (%d)", winErr));
            hr = HRESULT_FROM_WIN32(winErr);
            Assert(hr != S_OK);
        }
        CloseHandle(pMon->hEvent);
    }
    else
    {
        DWORD winErr = GetLastError();
        WARN((__FUNCTION__": CreateEvent failed, winErr = (%d)", winErr));
        hr = HRESULT_FROM_WIN32(winErr);
        Assert(hr != S_OK);
    }

    DeleteCriticalSection(&pMon->CritSect);

    return hr;
}

VOID NemuRrTerm()
{
    HRESULT hr;
    PNEMURR pMon = &g_NemuRr;
    if (!pMon->hThread)
        return;

    BOOL bResult = PostThreadMessage(pMon->idThread, WM_NEMURR_INIT_QUIT, 0, 0);
    DWORD winErr;
    if (bResult
            || (winErr = GetLastError()) == ERROR_INVALID_THREAD_ID) /* <- could be that the thread is terminated */
    {
        DWORD dwErr = WaitForSingleObject(pMon->hThread, INFINITE);
        if (dwErr == WAIT_OBJECT_0)
        {
            hr = S_OK;
        }
        else
        {
            winErr = GetLastError();
            hr = HRESULT_FROM_WIN32(winErr);
        }
    }
    else
    {
        hr = HRESULT_FROM_WIN32(winErr);
    }

    DeleteCriticalSection(&pMon->CritSect);

    CloseHandle(pMon->hThread);
    pMon->hThread = 0;
    CloseHandle(pMon->hEvent);
    pMon->hThread = 0;
}

static DWORD nemuDispIfWddmInit(PCNEMUDISPIF pIf)
{
    HRESULT hr = NemuRrInit();
    if (SUCCEEDED(hr))
    {
        return ERROR_SUCCESS;
    }
    WARN(("NemuTray: NemuRrInit failed hr 0x%x\n", hr));
    return hr;
}

static void nemuDispIfWddmTerm(PCNEMUDISPIF pIf)
{
    NemuRrTerm();
}

static DWORD nemuDispIfQueryDisplayConnection(NEMUDISPIF_OP *pOp, UINT32 iDisplay, BOOL *pfConnected)
{
    if (pOp->pIf->enmMode == NEMUDISPIF_MODE_WDDM)
    {
        /* @todo: do we need ti impl it? */
        *pfConnected = TRUE;
        return ERROR_SUCCESS;
    }

    *pfConnected = FALSE;

    NEMUDISPIF_WDDM_DISPCFG DispCfg;
    DWORD winEr = nemuDispIfWddmDcCreate(&DispCfg, QDC_ALL_PATHS);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmDcCreate winEr %d\n", winEr));
        return winEr;
    }

    int idx = nemuDispIfWddmDcSearchPath(&DispCfg, iDisplay, iDisplay);
    *pfConnected = (idx >= 0);

    nemuDispIfWddmDcTerm(&DispCfg);

    return ERROR_SUCCESS;
}

static DWORD nemuDispIfWaitDisplayDataInited(NEMUDISPIF_OP *pOp)
{
    DWORD winEr = ERROR_SUCCESS;
    do
    {
        Sleep(100);

        D3DKMT_POLLDISPLAYCHILDREN PollData = {0};
        PollData.hAdapter = pOp->Adapter.hAdapter;
        PollData.NonDestructiveOnly = 1;
        NTSTATUS Status = pOp->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTPollDisplayChildren(&PollData);
        if (Status != 0)
        {
            Log(("NemuTray: (WDDM) pfnD3DKMTPollDisplayChildren failed, Status (0x%x)\n", Status));
            continue;
        }

        BOOL fFound = FALSE;
#if 0
        for (UINT i = 0; i < NEMUWDDM_SCREENMASK_SIZE; ++i)
        {
            if (pu8DisplayMask && !ASMBitTest(pu8DisplayMask, i))
                continue;

            BOOL fConnected = FALSE;
            winEr = nemuDispIfQueryDisplayConnection(pOp, i, &fConnected);
            if (winEr != ERROR_SUCCESS)
            {
                WARN(("NemuTray: (WDDM) Failed nemuDispIfQueryDisplayConnection winEr %d\n", winEr));
                return winEr;
            }

            if (!fConnected)
            {
                WARN(("NemuTray: (WDDM) Display %d not connected, not expected\n", i));
                fFound = TRUE;
                break;
            }
        }
#endif
        if (!fFound)
            break;
    } while (1);

    return winEr;
}

static DWORD nemuDispIfUpdateModesWDDM(NEMUDISPIF_OP *pOp, uint32_t u32TargetId, const RTRECTSIZE *pSize)
{
    DWORD winEr = ERROR_SUCCESS;
    NEMUDISPIFESCAPE_UPDATEMODES EscData = {0};
    EscData.EscapeHdr.escapeCode = NEMUESC_UPDATEMODES;
    EscData.u32TargetId = u32TargetId;
    EscData.Size = *pSize;

    D3DKMT_ESCAPE EscapeData = {0};
    EscapeData.hAdapter = pOp->Adapter.hAdapter;
#ifdef NEMU_DISPIF_WITH_OPCONTEXT
    /* win8.1 does not allow context-based escapes for display-only mode */
    EscapeData.hDevice = pOp->Device.hDevice;
    EscapeData.hContext = pOp->Context.hContext;
#endif
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess = 1;
    EscapeData.pPrivateDriverData = &EscData;
    EscapeData.PrivateDriverDataSize = sizeof (EscData);

    NTSTATUS Status = pOp->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
        winEr = ERROR_SUCCESS;
    else
    {
        WARN(("NemuTray: pfnD3DKMTEscape NEMUESC_UPDATEMODES failed Status 0x%x\n", Status));
        winEr = ERROR_GEN_FAILURE;
    }

    winEr =  nemuDispIfWaitDisplayDataInited(pOp);
    if (winEr != NO_ERROR)
        WARN(("NemuTray: (WDDM) Failed nemuDispIfWaitDisplayDataInited winEr %d\n", winEr));

    return winEr;
}

DWORD nemuDispIfCancelPendingResizeWDDM(PCNEMUDISPIF const pIf)
{
    Log(("NemuTray: cancelling pending resize\n"));
    NemuRrRetryStop();
    return NO_ERROR;
}

static DWORD nemuDispIfWddmResizeDisplayVista(DEVMODE *paDeviceModes, DISPLAY_DEVICE *paDisplayDevices, DWORD cDevModes, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup)
{
    /* Without this, Windows will not ask the miniport for its
     * mode table but uses an internal cache instead.
     */
    for (DWORD i = 0; i < cDevModes; i++)
    {
        DEVMODE tempDevMode;
        ZeroMemory (&tempDevMode, sizeof (tempDevMode));
        tempDevMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettings((LPSTR)paDisplayDevices[i].DeviceName, 0xffffff, &tempDevMode);
        Log(("NemuTray: ResizeDisplayDevice: EnumDisplaySettings last error %d\n", GetLastError ()));
    }

    DWORD winEr = EnableAndResizeDispDev(paDeviceModes, paDisplayDevices, cDevModes, iChangedMode, paDeviceModes[iChangedMode].dmPelsWidth, paDeviceModes[iChangedMode].dmPelsHeight,
            paDeviceModes[iChangedMode].dmBitsPerPel, paDeviceModes[iChangedMode].dmPosition.x, paDeviceModes[iChangedMode].dmPosition.y, fEnable, fExtDispSup);
    if (winEr != NO_ERROR)
        WARN(("NemuTray: (WDDM) Failed EnableAndResizeDispDev winEr %d\n", winEr));

    return winEr;
}

static DWORD nemuDispIfResizePerform(PCNEMUDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes)
{
    DWORD winEr;
    if (pIf->enmMode > NEMUDISPIF_MODE_WDDM)
    {
        winEr = nemuDispIfWddmResizeDisplay(pIf, iChangedMode, fEnable, paDisplayDevices, paDeviceModes, cDevModes);
        if (winEr != NO_ERROR)
            WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmResizeDisplay winEr %d\n", winEr));
    }
    else
    {
        winEr = nemuDispIfWddmResizeDisplayVista(paDeviceModes, paDisplayDevices, cDevModes, iChangedMode, fEnable, fExtDispSup);
        if (winEr != NO_ERROR)
            WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmResizeDisplayVista winEr %d\n", winEr));
    }
    return winEr;
}

DWORD nemuDispIfResizeModesWDDM(PCNEMUDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes)
{
    DWORD winEr = NO_ERROR;

    Log(("NemuTray: nemuDispIfResizeModesWDDM iChanged %d cDevModes %d\n", iChangedMode, cDevModes));
    NemuRrRetryStop();

    NEMUDISPIF_OP Op;

    winEr = nemuDispIfOpBegin(pIf, &Op);
    if (winEr != NO_ERROR)
    {
        WARN(("NemuTray: nemuDispIfOpBegin failed winEr 0x%x", winEr));
        return winEr;
    }

    NEMUWDDM_RECOMMENDVIDPN VidPnData;

    memset(&VidPnData, 0, sizeof (VidPnData));

    uint32_t cElements = 0;

    for (uint32_t i = 0; i < cDevModes; ++i)
    {
        if ((i == iChangedMode) ? fEnable : (paDisplayDevices[i].StateFlags & DISPLAY_DEVICE_ACTIVE))
        {
            VidPnData.aSources[cElements].Size.cx = paDeviceModes[i].dmPelsWidth;
            VidPnData.aSources[cElements].Size.cy = paDeviceModes[i].dmPelsHeight;
            VidPnData.aTargets[cElements].iSource = cElements;
            ++cElements;
        }
        else
            VidPnData.aTargets[cElements].iSource = -1;
    }

    D3DKMT_INVALIDATEACTIVEVIDPN DdiData = {0};

    DdiData.hAdapter = Op.Adapter.hAdapter;
    DdiData.pPrivateDriverData = &VidPnData;
    DdiData.PrivateDriverDataSize = sizeof (VidPnData);

    NTSTATUS Status = Op.pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTInvalidateActiveVidPn(&DdiData);
    LogFunc(("InvalidateActiveVidPn 0x%08x\n", Status));

    /* Resize displays always to keep the display layout because
     * "the D3DKMTInvalidateActiveVidPn function always resets a multimonitor desktop to the default configuration".
     */
    {
        winEr = NO_ERROR;

        if (fEnable)
        {
            RTRECTSIZE Size;
            Size.cx = paDeviceModes[iChangedMode].dmPelsWidth;
            Size.cy = paDeviceModes[iChangedMode].dmPelsHeight;
            winEr = nemuDispIfUpdateModesWDDM(&Op, iChangedMode, &Size);
            if (winEr != NO_ERROR)
                WARN(("nemuDispIfUpdateModesWDDM failed %d\n", winEr));
        }

        if (winEr == NO_ERROR)
        {
            winEr = nemuDispIfResizePerform(pIf, iChangedMode, fEnable, fExtDispSup, paDisplayDevices, paDeviceModes, cDevModes);

            if (winEr == ERROR_RETRY)
            {
                NemuRrRetrySchedule(pIf, iChangedMode, fEnable, fExtDispSup, paDisplayDevices, paDeviceModes, cDevModes);
                /* just pretend everything is fine so far */
                winEr = NO_ERROR;
            }
        }
    }

    nemuDispIfOpEnd(&Op);

    return winEr;
}

static DWORD nemuDispIfWddmEnableDisplays(PCNEMUDISPIF const pIf, UINT cIds, UINT *pIds, BOOL fEnabled, BOOL fSetTopology, DEVMODE *pDeviceMode)
{
    NEMUDISPIF_WDDM_DISPCFG DispCfg;

    DWORD winEr;
    int iPath;

    winEr = nemuDispIfWddmDcCreate(&DispCfg, QDC_ONLY_ACTIVE_PATHS);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmDcCreate winEr %d\n", winEr));
        return winEr;
    }

    UINT cChangeIds = 0;
    UINT *pChangeIds = (UINT*)alloca(cIds * sizeof (*pChangeIds));
    if (!pChangeIds)
    {
        WARN(("NemuTray: (WDDM) Failed to alloc change ids\n"));
        winEr = ERROR_OUTOFMEMORY;
        goto done;
    }

    for (UINT i = 0; i < cIds; ++i)
    {
        UINT Id = pIds[i];
        bool fIsDup = false;
        for (UINT j = 0; j < cChangeIds; ++j)
        {
            if (pChangeIds[j] == Id)
            {
                fIsDup = true;
                break;
            }
        }

        if (fIsDup)
            continue;

        iPath = nemuDispIfWddmDcSearchPath(&DispCfg, Id, Id);

        if (!((iPath >= 0) && (DispCfg.pPathInfoArray[iPath].flags & DISPLAYCONFIG_PATH_ACTIVE)) != !fEnabled)
        {
            pChangeIds[cChangeIds] = Id;
            ++cChangeIds;
        }
    }

    if (cChangeIds == 0)
    {
        Log(("NemuTray: (WDDM) nemuDispIfWddmEnableDisplay: settings are up to date\n"));
        winEr = ERROR_SUCCESS;
        goto done;
    }

    /* we want to set primary for every disabled for non-topoly mode only */
    winEr = nemuDispIfWddmDcSettingsIncludeAllTargets(&DispCfg);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmDcSettingsIncludeAllTargets winEr %d\n", winEr));
        return winEr;
    }

    if (fSetTopology)
        nemuDispIfWddmDcSettingsInvalidateModeIndeces(&DispCfg);

    for (UINT i = 0; i < cChangeIds; ++i)
    {
        UINT Id = pChangeIds[i];
        /* re-query paths */
        iPath = nemuDispIfWddmDcSearchPath(&DispCfg, -1, Id);
        if (iPath < 0)
        {
            WARN(("NemuTray: (WDDM) path index not found while it should"));
            winEr = ERROR_GEN_FAILURE;
            goto done;
        }

        winEr = nemuDispIfWddmDcSettingsUpdate(&DispCfg, iPath, pDeviceMode, !fEnabled || fSetTopology, fEnabled);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmDcSettingsUpdate winEr %d\n", winEr));
            goto done;
        }
    }

    if (!fSetTopology)
        nemuDispIfWddmDcSettingsAttachDisbledToPrimary(&DispCfg);

#if 0
    /* ensure the zero-index (primary) screen is enabled */
    iPath = nemuDispIfWddmDcSearchPath(&DispCfg, 0, 0);
    if (iPath < 0)
    {
        WARN(("NemuTray: (WDDM) path index not found while it should"));
        winEr = ERROR_GEN_FAILURE;
        goto done;
    }

    winEr = nemuDispIfWddmDcSettingsUpdate(&DispCfg, iPath, /* just re-use device node here*/ pDeviceMode, fSetTopology, TRUE);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmDcSettingsUpdate winEr %d\n", winEr));
        goto done;
    }
#endif

    UINT fSetFlags = !fSetTopology ? (SDC_USE_SUPPLIED_DISPLAY_CONFIG) : (SDC_ALLOW_PATH_ORDER_CHANGES | SDC_TOPOLOGY_SUPPLIED);
    winEr = nemuDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_VALIDATE);
    if (winEr != ERROR_SUCCESS)
    {
        if (!fSetTopology)
        {
            WARN(("NemuTray: (WDDM) nemuDispIfWddmDcSet validation failed winEr, trying with changes %d\n", winEr));
            fSetFlags |= SDC_ALLOW_CHANGES;
        }
        else
        {
            Log(("NemuTray: (WDDM) nemuDispIfWddmDcSet topology validation failed winEr %d\n", winEr));
            goto done;
        }
    }

    if (!fSetTopology)
        fSetFlags |= SDC_SAVE_TO_DATABASE;

    winEr = nemuDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_APPLY);
    if (winEr != ERROR_SUCCESS)
        WARN(("NemuTray: (WDDM) nemuDispIfWddmDcSet apply failed winEr %d\n", winEr));

done:
    nemuDispIfWddmDcTerm(&DispCfg);

    return winEr;
}

static DWORD nemuDispIfWddmEnableDisplaysTryingTopology(PCNEMUDISPIF const pIf, UINT cIds, UINT *pIds, BOOL fEnable)
{
    DWORD winEr = nemuDispIfWddmEnableDisplays(pIf, cIds, pIds, fEnable, FALSE, NULL);
    if (winEr != ERROR_SUCCESS)
    {
        if (fEnable)
            WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmEnableDisplay mode winEr %d\n", winEr));
        else
            Log(("NemuTray: (WDDM) Failed nemuDispIfWddmEnableDisplay mode winEr %d\n", winEr));
        winEr = nemuDispIfWddmEnableDisplays(pIf, cIds, pIds, fEnable, TRUE, NULL);
        if (winEr != ERROR_SUCCESS)
            WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmEnableDisplay mode winEr %d\n", winEr));
    }

    return winEr;
}

static DWORD nemuDispIfWddmResizeDisplay(PCNEMUDISPIF const pIf, UINT Id, BOOL fEnable, DISPLAY_DEVICE * paDisplayDevices, DEVMODE *paDeviceMode, UINT devModes)
{
    NEMUDISPIF_WDDM_DISPCFG DispCfg;
    DWORD winEr;
    int iPath;

    winEr = nemuDispIfWddmDcCreate(&DispCfg, QDC_ONLY_ACTIVE_PATHS);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmDcCreate\n"));
        return winEr;
    }

    iPath = nemuDispIfWddmDcSearchActivePath(&DispCfg, Id, Id);

    if (iPath < 0)
    {
        nemuDispIfWddmDcTerm(&DispCfg);

        if (!fEnable)
        {
            /* nothing to be done here, just leave */
            return ERROR_SUCCESS;
        }

        winEr = nemuDispIfWddmEnableDisplaysTryingTopology(pIf, 1, &Id, fEnable);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmEnableDisplaysTryingTopology winEr %d\n", winEr));
            return winEr;
        }

        winEr = nemuDispIfWddmDcCreate(&DispCfg, QDC_ONLY_ACTIVE_PATHS);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmDcCreate winEr %d\n", winEr));
            return winEr;
        }

        iPath = nemuDispIfWddmDcSearchPath(&DispCfg, Id, Id);
        if (iPath < 0)
        {
            WARN(("NemuTray: (WDDM) path (%d) is still disabled, going to retry winEr %d\n", winEr));
            nemuDispIfWddmDcTerm(&DispCfg);
            return ERROR_RETRY;
        }
    }

    Assert(iPath >= 0);

    if (!fEnable)
    {
        /* need to disable it, and we are done */
        nemuDispIfWddmDcTerm(&DispCfg);

        winEr = nemuDispIfWddmEnableDisplaysTryingTopology(pIf, 1, &Id, fEnable);
        if (winEr != ERROR_SUCCESS)
        {
            WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmEnableDisplaysTryingTopology winEr %d\n", winEr));
            return winEr;
        }

        return winEr;
    }

    Assert(fEnable);

    winEr = nemuDispIfWddmDcSettingsUpdate(&DispCfg, iPath, &paDeviceMode[Id], FALSE, fEnable);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray: (WDDM) Failed nemuDispIfWddmDcSettingsUpdate\n"));
        nemuDispIfWddmDcTerm(&DispCfg);
        return winEr;
    }

    UINT fSetFlags = SDC_USE_SUPPLIED_DISPLAY_CONFIG;
    winEr = nemuDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_VALIDATE);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray:(WDDM) pfnSetDisplayConfig Failed to validate winEr %d.\n", winEr));
        fSetFlags |= SDC_ALLOW_CHANGES;
    }

    winEr = nemuDispIfWddmDcSet(&DispCfg, fSetFlags | SDC_SAVE_TO_DATABASE | SDC_APPLY);
    if (winEr != ERROR_SUCCESS)
    {
        WARN(("NemuTray:(WDDM) pfnSetDisplayConfig Failed to validate winEr %d.\n", winEr));
    }

    nemuDispIfWddmDcTerm(&DispCfg);

    return winEr;
}

#endif /* NEMU_WITH_WDDM */

DWORD NemuDispIfResizeModes(PCNEMUDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes)
{
    switch (pIf->enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
            return ERROR_NOT_SUPPORTED;
        case NEMUDISPIF_MODE_XPDM:
            return ERROR_NOT_SUPPORTED;
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        case NEMUDISPIF_MODE_WDDM_W7:
            return nemuDispIfResizeModesWDDM(pIf, iChangedMode, fEnable, fExtDispSup, paDisplayDevices, paDeviceModes, cDevModes);
#endif
        default:
            WARN((__FUNCTION__": unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

DWORD NemuDispIfCancelPendingResize(PCNEMUDISPIF const pIf)
{
    switch (pIf->enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
            return NO_ERROR;
        case NEMUDISPIF_MODE_XPDM:
            return NO_ERROR;
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        case NEMUDISPIF_MODE_WDDM_W7:
            return nemuDispIfCancelPendingResizeWDDM(pIf);
#endif
        default:
            WARN((__FUNCTION__": unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

static DWORD nemuDispIfConfigureTargetsWDDM(NEMUDISPIF_OP *pOp, uint32_t *pcConnected)
{
    NEMUDISPIFESCAPE EscapeHdr = {0};
    EscapeHdr.escapeCode = NEMUESC_CONFIGURETARGETS;
    EscapeHdr.u32CmdSpecific = 0;

    D3DKMT_ESCAPE EscapeData = {0};
    EscapeData.hAdapter = pOp->Adapter.hAdapter;
#ifdef NEMU_DISPIF_WITH_OPCONTEXT
    /* win8.1 does not allow context-based escapes for display-only mode */
    EscapeData.hDevice = pOp->Device.hDevice;
    EscapeData.hContext = pOp->Context.hContext;
#endif
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    EscapeData.Flags.HardwareAccess = 1;
    EscapeData.pPrivateDriverData = &EscapeHdr;
    EscapeData.PrivateDriverDataSize = sizeof (EscapeHdr);

    NTSTATUS Status = pOp->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
    {
        if (pcConnected)
            *pcConnected = EscapeHdr.u32CmdSpecific;
        return NO_ERROR;
    }
    WARN(("NemuTray: pfnD3DKMTEscape NEMUESC_CONFIGURETARGETS failed Status 0x%x\n", Status));
    return Status;
}

static DWORD nemuDispIfResizeStartedWDDMOp(NEMUDISPIF_OP *pOp)
{
    DWORD NumDevices = NemuGetDisplayConfigCount();
    if (NumDevices == 0)
    {
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: Zero devices found\n"));
        return ERROR_GEN_FAILURE;
    }

    DISPLAY_DEVICE *paDisplayDevices = (DISPLAY_DEVICE *)alloca (sizeof (DISPLAY_DEVICE) * NumDevices);
    DEVMODE *paDeviceModes = (DEVMODE *)alloca (sizeof (DEVMODE) * NumDevices);
    DWORD DevNum = 0;
    DWORD DevPrimaryNum = 0;

    DWORD winEr = NemuGetDisplayConfig(NumDevices, &DevPrimaryNum, &DevNum, paDisplayDevices, paDeviceModes);
    if (winEr != NO_ERROR)
    {
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: NemuGetDisplayConfig failed, %d\n", winEr));
        return winEr;
    }

    if (NumDevices != DevNum)
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: NumDevices(%d) != DevNum(%d)\n", NumDevices, DevNum));


    uint32_t cConnected = 0;
    winEr = nemuDispIfConfigureTargetsWDDM(pOp, &cConnected);
    if (winEr != NO_ERROR)
    {
        WARN(("NemuTray: nemuDispIfConfigureTargetsWDDM failed winEr 0x%x\n", winEr));
        return winEr;
    }

    if (!cConnected)
    {
        Log(("NemuTray: all targets already connected, nothing to do\n"));
        return NO_ERROR;
    }

    winEr = nemuDispIfWaitDisplayDataInited(pOp);
    if (winEr != NO_ERROR)
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: nemuDispIfWaitDisplayDataInited failed winEr 0x%x\n", winEr));

    DWORD NewNumDevices = NemuGetDisplayConfigCount();
    if (NewNumDevices == 0)
    {
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: Zero devices found\n"));
        return ERROR_GEN_FAILURE;
    }

    if (NewNumDevices != NumDevices)
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: NumDevices(%d) != NewNumDevices(%d)\n", NumDevices, NewNumDevices));

    DISPLAY_DEVICE *paNewDisplayDevices = (DISPLAY_DEVICE *)alloca (sizeof (DISPLAY_DEVICE) * NewNumDevices);
    DEVMODE *paNewDeviceModes = (DEVMODE *)alloca (sizeof (DEVMODE) * NewNumDevices);
    DWORD NewDevNum = 0;
    DWORD NewDevPrimaryNum = 0;

    winEr = NemuGetDisplayConfig(NewNumDevices, &NewDevPrimaryNum, &NewDevNum, paNewDisplayDevices, paNewDeviceModes);
    if (winEr != NO_ERROR)
    {
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: NemuGetDisplayConfig failed for new devices, %d\n", winEr));
        return winEr;
    }

    if (NewNumDevices != NewDevNum)
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: NewNumDevices(%d) != NewDevNum(%d)\n", NewNumDevices, NewDevNum));

    DWORD minDevNum = RT_MIN(DevNum, NewDevNum);
    UINT *pIds = (UINT*)alloca (sizeof (UINT) * minDevNum);
    UINT cIds = 0;
    for (DWORD i = 0; i < minDevNum; ++i)
    {
        if ((paNewDisplayDevices[i].StateFlags & DISPLAY_DEVICE_ACTIVE)
                && !(paDisplayDevices[i].StateFlags & DISPLAY_DEVICE_ACTIVE))
        {
            pIds[cIds] = i;
            ++cIds;
        }
    }

    if (!cIds)
    {
        /* this is something we would not regularly expect */
        WARN(("NemuTray: all targets already have proper config, nothing to do\n"));
        return NO_ERROR;
    }

    if (pOp->pIf->enmMode > NEMUDISPIF_MODE_WDDM)
    {
        winEr = nemuDispIfWddmEnableDisplaysTryingTopology(pOp->pIf, cIds, pIds, FALSE);
        if (winEr != NO_ERROR)
            WARN(("NemuTray: nemuDispIfWddmEnableDisplaysTryingTopology failed to record current settings, %d, ignoring\n", winEr));
    }
    else
    {
        for (DWORD i = 0; i < cIds; ++i)
        {
            winEr = nemuDispIfWddmResizeDisplayVista(paNewDeviceModes, paNewDisplayDevices, NewDevNum, i, FALSE, TRUE);
            if (winEr != NO_ERROR)
                WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp: nemuDispIfWddmResizeDisplayVista failed winEr 0x%x\n", winEr));
        }
    }

    return winEr;
}


static DWORD nemuDispIfResizeStartedWDDM(PCNEMUDISPIF const pIf)
{
    NEMUDISPIF_OP Op;

    DWORD winEr = nemuDispIfOpBegin(pIf, &Op);
    if (winEr != NO_ERROR)
    {
        WARN(("NemuTray: nemuDispIfOpBegin failed winEr 0x%x\n", winEr));
        return winEr;
    }

    winEr = nemuDispIfResizeStartedWDDMOp(&Op);
    if (winEr != NO_ERROR)
    {
        WARN(("NemuTray: nemuDispIfResizeStartedWDDMOp failed winEr 0x%x\n", winEr));
    }

    nemuDispIfOpEnd(&Op);

    return winEr;
}

DWORD NemuDispIfResizeStarted(PCNEMUDISPIF const pIf)
{
    switch (pIf->enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
            return NO_ERROR;
        case NEMUDISPIF_MODE_XPDM:
            return NO_ERROR;
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        case NEMUDISPIF_MODE_WDDM_W7:
            return nemuDispIfResizeStartedWDDM(pIf);
#endif
        default:
            WARN((__FUNCTION__": unknown mode (%d)\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

static DWORD nemuDispIfSwitchToXPDM_NT4(PNEMUDISPIF pIf)
{
    return NO_ERROR;
}

static DWORD nemuDispIfSwitchToXPDM(PNEMUDISPIF pIf)
{
    DWORD err = NO_ERROR;
    AssertBreakpoint();
    OSVERSIONINFO OSinfo;
    OSinfo.dwOSVersionInfoSize = sizeof (OSinfo);
    GetVersionEx (&OSinfo);
    if (OSinfo.dwMajorVersion >= 5)
    {
        HMODULE hUser = GetModuleHandle("user32.dll");
        if (NULL != hUser)
        {
            bool bSupported = true;
            *(uintptr_t *)&pIf->modeData.xpdm.pfnChangeDisplaySettingsEx = (uintptr_t)GetProcAddress(hUser, "ChangeDisplaySettingsExA");
            Log((__FUNCTION__": pfnChangeDisplaySettingsEx = %p\n", pIf->modeData.xpdm.pfnChangeDisplaySettingsEx));
            bSupported &= !!(pIf->modeData.xpdm.pfnChangeDisplaySettingsEx);

            if (!bSupported)
            {
                WARN((__FUNCTION__": pfnChangeDisplaySettingsEx function pointer failed to initialize\n"));
                err = ERROR_NOT_SUPPORTED;
            }
        }
        else
        {
            WARN((__FUNCTION__": failed to get USER32 handle, err (%d)\n", GetLastError()));
            err = ERROR_NOT_SUPPORTED;
        }
    }
    else
    {
        WARN((__FUNCTION__": can not switch to NEMUDISPIF_MODE_XPDM, because os is not >= w2k\n"));
        err = ERROR_NOT_SUPPORTED;
    }

    return err;
}

DWORD NemuDispIfSwitchMode(PNEMUDISPIF pIf, NEMUDISPIF_MODE enmMode, NEMUDISPIF_MODE *penmOldMode)
{
    /* @todo: may need to addd synchronization in case we want to change modes dynamically
     * i.e. currently the mode is supposed to be initialized once on service initialization */
    if (penmOldMode)
        *penmOldMode = pIf->enmMode;

    if (enmMode == pIf->enmMode)
        return NO_ERROR;

#ifdef NEMU_WITH_WDDM
    if (pIf->enmMode >= NEMUDISPIF_MODE_WDDM)
    {
        nemuDispIfWddmTerm(pIf);

        nemuDispKmtCallbacksTerm(&pIf->modeData.wddm.KmtCallbacks);
    }
#endif

    DWORD err = NO_ERROR;
    switch (enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
            Log((__FUNCTION__": request to switch to NEMUDISPIF_MODE_XPDM_NT4\n"));
            err = nemuDispIfSwitchToXPDM_NT4(pIf);
            if (err == NO_ERROR)
            {
                Log((__FUNCTION__": successfully switched to XPDM_NT4 mode\n"));
                pIf->enmMode = NEMUDISPIF_MODE_XPDM_NT4;
            }
            else
                WARN((__FUNCTION__": failed to switch to XPDM_NT4 mode, err (%d)\n", err));
            break;
        case NEMUDISPIF_MODE_XPDM:
            Log((__FUNCTION__": request to switch to NEMUDISPIF_MODE_XPDM\n"));
            err = nemuDispIfSwitchToXPDM(pIf);
            if (err == NO_ERROR)
            {
                Log((__FUNCTION__": successfully switched to XPDM mode\n"));
                pIf->enmMode = NEMUDISPIF_MODE_XPDM;
            }
            else
                WARN((__FUNCTION__": failed to switch to XPDM mode, err (%d)\n", err));
            break;
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        {
            Log((__FUNCTION__": request to switch to NEMUDISPIF_MODE_WDDM\n"));
            err = nemuDispIfSwitchToWDDM(pIf);
            if (err == NO_ERROR)
            {
                Log((__FUNCTION__": successfully switched to WDDM mode\n"));
                pIf->enmMode = NEMUDISPIF_MODE_WDDM;
            }
            else
                WARN((__FUNCTION__": failed to switch to WDDM mode, err (%d)\n", err));
            break;
        }
        case NEMUDISPIF_MODE_WDDM_W7:
        {
            Log((__FUNCTION__": request to switch to NEMUDISPIF_MODE_WDDM_W7\n"));
            err = nemuDispIfSwitchToWDDM_W7(pIf);
            if (err == NO_ERROR)
            {
                Log((__FUNCTION__": successfully switched to WDDM mode\n"));
                pIf->enmMode = NEMUDISPIF_MODE_WDDM_W7;
            }
            else
                WARN((__FUNCTION__": failed to switch to WDDM mode, err (%d)\n", err));
            break;
        }
#endif
        default:
            err = ERROR_INVALID_PARAMETER;
            break;
    }
    return err;
}

static DWORD nemuDispIfSeamlesCreateWDDM(PCNEMUDISPIF const pIf, NEMUDISPIF_SEAMLESS *pSeamless, HANDLE hEvent)
{
    HRESULT hr = nemuDispKmtOpenAdapter(&pIf->modeData.wddm.KmtCallbacks, &pSeamless->modeData.wddm.Adapter);
    if (SUCCEEDED(hr))
    {
#ifdef NEMU_DISPIF_WITH_OPCONTEXT
        hr = nemuDispKmtCreateDevice(&pSeamless->modeData.wddm.Adapter, &pSeamless->modeData.wddm.Device);
        if (SUCCEEDED(hr))
        {
            hr = nemuDispKmtCreateContext(&pSeamless->modeData.wddm.Device, &pSeamless->modeData.wddm.Context, NEMUWDDM_CONTEXT_TYPE_CUSTOM_DISPIF_SEAMLESS,
                    0, 0, hEvent, 0ULL);
            if (SUCCEEDED(hr))
#endif
                return ERROR_SUCCESS;
#ifdef NEMU_DISPIF_WITH_OPCONTEXT
            else
                WARN(("NemuTray: nemuDispKmtCreateContext failed hr 0x%x", hr));

            nemuDispKmtDestroyDevice(&pSeamless->modeData.wddm.Device);
        }
        else
            WARN(("NemuTray: nemuDispKmtCreateDevice failed hr 0x%x", hr));

        nemuDispKmtCloseAdapter(&pSeamless->modeData.wddm.Adapter);
#endif
    }

    return hr;
}

static DWORD nemuDispIfSeamlesTermWDDM(NEMUDISPIF_SEAMLESS *pSeamless)
{
#ifdef NEMU_DISPIF_WITH_OPCONTEXT
    nemuDispKmtDestroyContext(&pSeamless->modeData.wddm.Context);
    nemuDispKmtDestroyDevice(&pSeamless->modeData.wddm.Device);
#endif
    nemuDispKmtCloseAdapter(&pSeamless->modeData.wddm.Adapter);

    return NO_ERROR;
}

static DWORD nemuDispIfSeamlesSubmitWDDM(NEMUDISPIF_SEAMLESS *pSeamless, NEMUDISPIFESCAPE *pData, int cbData)
{
    D3DKMT_ESCAPE EscapeData = {0};
    EscapeData.hAdapter = pSeamless->modeData.wddm.Adapter.hAdapter;
#ifdef NEMU_DISPIF_WITH_OPCONTEXT
    EscapeData.hDevice = pSeamless->modeData.wddm.Device.hDevice;
    EscapeData.hContext = pSeamless->modeData.wddm.Context.hContext;
#endif
    EscapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    /*EscapeData.Flags.HardwareAccess = 1;*/
    EscapeData.pPrivateDriverData = pData;
    EscapeData.PrivateDriverDataSize = NEMUDISPIFESCAPE_SIZE(cbData);

    NTSTATUS Status = pSeamless->pIf->modeData.wddm.KmtCallbacks.pfnD3DKMTEscape(&EscapeData);
    if (NT_SUCCESS(Status))
        return ERROR_SUCCESS;

    WARN(("NemuTray: pfnD3DKMTEscape Seamless failed Status 0x%x\n", Status));
    return Status;
}

DWORD NemuDispIfSeamlesCreate(PCNEMUDISPIF const pIf, NEMUDISPIF_SEAMLESS *pSeamless, HANDLE hEvent)
{
    memset(pSeamless, 0, sizeof (*pSeamless));
    pSeamless->pIf = pIf;

    switch (pIf->enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
        case NEMUDISPIF_MODE_XPDM:
            return NO_ERROR;
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        case NEMUDISPIF_MODE_WDDM_W7:
            return nemuDispIfSeamlesCreateWDDM(pIf, pSeamless, hEvent);
#endif
        default:
            WARN(("NemuTray: NemuDispIfSeamlesCreate: invalid mode %d\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}

DWORD NemuDispIfSeamlesTerm(NEMUDISPIF_SEAMLESS *pSeamless)
{
    PCNEMUDISPIF const pIf = pSeamless->pIf;
    DWORD winEr;
    switch (pIf->enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
        case NEMUDISPIF_MODE_XPDM:
            winEr = NO_ERROR;
            break;
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        case NEMUDISPIF_MODE_WDDM_W7:
            winEr = nemuDispIfSeamlesTermWDDM(pSeamless);
            break;
#endif
        default:
            WARN(("NemuTray: NemuDispIfSeamlesTerm: invalid mode %d\n", pIf->enmMode));
            winEr = ERROR_INVALID_PARAMETER;
            break;
    }

    if (winEr == NO_ERROR)
        memset(pSeamless, 0, sizeof (*pSeamless));

    return winEr;
}

DWORD NemuDispIfSeamlesSubmit(NEMUDISPIF_SEAMLESS *pSeamless, NEMUDISPIFESCAPE *pData, int cbData)
{
    PCNEMUDISPIF const pIf = pSeamless->pIf;

    if (pData->escapeCode != NEMUESC_SETVISIBLEREGION)
    {
        WARN(("NemuTray: invalid escape code for Seamless submit %d\n", pData->escapeCode));
        return ERROR_INVALID_PARAMETER;
    }

    switch (pIf->enmMode)
    {
        case NEMUDISPIF_MODE_XPDM_NT4:
        case NEMUDISPIF_MODE_XPDM:
            return NemuDispIfEscape(pIf, pData, cbData);
#ifdef NEMU_WITH_WDDM
        case NEMUDISPIF_MODE_WDDM:
        case NEMUDISPIF_MODE_WDDM_W7:
            return nemuDispIfSeamlesSubmitWDDM(pSeamless, pData, cbData);
#endif
        default:
            WARN(("NemuTray: NemuDispIfSeamlesSubmit: invalid mode %d\n", pIf->enmMode));
            return ERROR_INVALID_PARAMETER;
    }
}
