/* $Id: NemuDispIf.h $ */
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
#include <iprt/cdefs.h>

#ifdef NEMU_WITH_WDDM
# define D3DKMDT_SPECIAL_MULTIPLATFORM_TOOL
# include <d3dkmthk.h>
# include "../Graphics/Video/disp/wddm/NemuDispKmt.h"
#endif

#include <NemuDisplay.h>

typedef enum
{
    NEMUDISPIF_MODE_UNKNOWN  = 0,
    NEMUDISPIF_MODE_XPDM_NT4 = 1,
    NEMUDISPIF_MODE_XPDM
#ifdef NEMU_WITH_WDDM
    , NEMUDISPIF_MODE_WDDM
    , NEMUDISPIF_MODE_WDDM_W7
#endif
} NEMUDISPIF_MODE;
/* display driver interface abstraction for XPDM & WDDM
 * with WDDM we can not use ExtEscape to communicate with our driver
 * because we do not have XPDM display driver any more, i.e. escape requests are handled by cdd
 * that knows nothing about us
 * NOTE: DispIf makes no checks whether the display driver is actually a Nemu driver,
 * it just switches between using different backend OS API based on the NemuDispIfSwitchMode call
 * It's caller's responsibility to initiate it to work in the correct mode */
typedef struct NEMUDISPIF
{
    NEMUDISPIF_MODE enmMode;
    /* with WDDM the approach is to call into WDDM miniport driver via PFND3DKMT API provided by the GDI,
     * The PFND3DKMT is supposed to be used by the OpenGL ICD according to MSDN, so this approach is a bit hacky */
    union
    {
        struct
        {
            LONG (WINAPI * pfnChangeDisplaySettingsEx)(LPCSTR lpszDeviceName, LPDEVMODE lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam);
        } xpdm;
#ifdef NEMU_WITH_WDDM
        struct
        {
            /* ChangeDisplaySettingsEx does not exist in NT. ResizeDisplayDevice uses the function. */
            LONG (WINAPI * pfnChangeDisplaySettingsEx)(LPCTSTR lpszDeviceName, LPDEVMODE lpDevMode, HWND hwnd, DWORD dwflags, LPVOID lParam);

            /* EnumDisplayDevices does not exist in NT. isNemuDisplayDriverActive et al. are using these functions. */
            BOOL (WINAPI * pfnEnumDisplayDevices)(IN LPCSTR lpDevice, IN DWORD iDevNum, OUT PDISPLAY_DEVICEA lpDisplayDevice, IN DWORD dwFlags);

            NEMUDISPKMT_CALLBACKS KmtCallbacks;
        } wddm;
#endif
    } modeData;
} NEMUDISPIF, *PNEMUDISPIF;
typedef const struct NEMUDISPIF *PCNEMUDISPIF;

/* initializes the DispIf
 * Initially the DispIf is configured to work in XPDM mode
 * call NemuDispIfSwitchMode to switch the mode to WDDM */
DWORD NemuDispIfInit(PNEMUDISPIF pIf);
DWORD NemuDispIfSwitchMode(PNEMUDISPIF pIf, NEMUDISPIF_MODE enmMode, NEMUDISPIF_MODE *penmOldMode);
DECLINLINE(NEMUDISPIF_MODE) NemuDispGetMode(PNEMUDISPIF pIf) { return pIf->enmMode; }
DWORD NemuDispIfTerm(PNEMUDISPIF pIf);
DWORD NemuDispIfEscape(PCNEMUDISPIF const pIf, PNEMUDISPIFESCAPE pEscape, int cbData);
DWORD NemuDispIfEscapeInOut(PCNEMUDISPIF const pIf, PNEMUDISPIFESCAPE pEscape, int cbData);
DWORD NemuDispIfResizeModes(PCNEMUDISPIF const pIf, UINT iChangedMode, BOOL fEnable, BOOL fExtDispSup, DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes, UINT cDevModes);
DWORD NemuDispIfCancelPendingResize(PCNEMUDISPIF const pIf);

DWORD NemuDispIfResizeStarted(PCNEMUDISPIF const pIf);


typedef struct NEMUDISPIF_SEAMLESS
{
    PCNEMUDISPIF pIf;

    union
    {
#ifdef NEMU_WITH_WDDM
        struct
        {
            NEMUDISPKMT_ADAPTER Adapter;
# ifdef NEMU_DISPIF_WITH_OPCONTEXT
            NEMUDISPKMT_DEVICE Device;
            NEMUDISPKMT_CONTEXT Context;
# endif
        } wddm;
#endif
    } modeData;
} NEMUDISPIF_SEAMLESS;

DECLINLINE(bool) NemuDispIfSeamlesIsValid(NEMUDISPIF_SEAMLESS *pSeamless)
{
    return !!pSeamless->pIf;
}

DWORD NemuDispIfSeamlesCreate(PCNEMUDISPIF const pIf, NEMUDISPIF_SEAMLESS *pSeamless, HANDLE hEvent);
DWORD NemuDispIfSeamlesTerm(NEMUDISPIF_SEAMLESS *pSeamless);
DWORD NemuDispIfSeamlesSubmit(NEMUDISPIF_SEAMLESS *pSeamless, NEMUDISPIFESCAPE *pData, int cbData);
