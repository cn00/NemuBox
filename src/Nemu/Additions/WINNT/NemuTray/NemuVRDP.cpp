/* $Id: NemuVRDP.cpp $ */
/** @file
 * NemuVRDP - Nemu VRDP connection notification
 */

/*
 * Copyright (C) 2006-2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* 0x0501 for SPI_SETDROPSHADOW */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include "NemuTray.h"
#include "NemuHelpers.h"
#include "NemuVRDP.h"

#include <Nemu/VMMDev.h>
#ifdef DEBUG
# define LOG_ENABLED
# define LOG_GROUP LOG_GROUP_DEFAULT
#endif
#include <Nemu/log.h>

#include <NemuGuestInternal.h>
#include <iprt/assert.h>
#include <iprt/ldr.h>



/* The guest receives VRDP_ACTIVE/VRDP_INACTIVE notifications.
 *
 * When VRDP_ACTIVE is received, the guest asks host about the experience level.
 * The experience level is an integer value, different values disable some GUI effects.
 *
 * On VRDP_INACTIVE the original values are restored.
 *
 * Note: that this is not controlled from the client, that is a per VM settings.
 *
 * Note: theming is disabled separately by EnableTheming.
 */

#define NEMU_SPI_STRING   0
#define NEMU_SPI_BOOL_PTR 1
#define NEMU_SPI_BOOL     2
#define NEMU_SPI_PTR      3

static ANIMATIONINFO animationInfoDisable =
{
    sizeof (ANIMATIONINFO),
    FALSE
};

typedef struct _NemuExperienceParameter
{
    const char *name;
    UINT  uActionSet;
    UINT  uActionGet;
    uint32_t level;                    /* The parameter remain enabled at this or higher level. */
    int   type;
    void  *pvDisable;
    UINT  cbSavedValue;
    char  achSavedValue[2 * MAX_PATH]; /* Large enough to save the bitmap path. */
} NemuExperienceParameter;

#define SPI_(l, a) #a, SPI_SET##a, SPI_GET##a, VRDP_EXPERIENCE_LEVEL_##l

static NemuExperienceParameter parameters[] =
{
    { SPI_(MEDIUM, DESKWALLPAPER),           NEMU_SPI_STRING,   "" },
    { SPI_(FULL,   DROPSHADOW),              NEMU_SPI_BOOL_PTR,       },
    { SPI_(HIGH,   FONTSMOOTHING),           NEMU_SPI_BOOL,           },
    { SPI_(FULL,   MENUFADE),                NEMU_SPI_BOOL_PTR,       },
    { SPI_(FULL,   COMBOBOXANIMATION),       NEMU_SPI_BOOL_PTR,       },
    { SPI_(FULL,   CURSORSHADOW),            NEMU_SPI_BOOL_PTR,       },
    { SPI_(HIGH,   GRADIENTCAPTIONS),        NEMU_SPI_BOOL_PTR,       },
    { SPI_(FULL,   LISTBOXSMOOTHSCROLLING),  NEMU_SPI_BOOL_PTR,       },
    { SPI_(FULL,   MENUANIMATION),           NEMU_SPI_BOOL_PTR,       },
    { SPI_(FULL,   SELECTIONFADE),           NEMU_SPI_BOOL_PTR,       },
    { SPI_(FULL,   TOOLTIPANIMATION),        NEMU_SPI_BOOL_PTR,       },
    { SPI_(FULL,   ANIMATION),               NEMU_SPI_PTR,      &animationInfoDisable, sizeof (ANIMATIONINFO) },
    { SPI_(MEDIUM, DRAGFULLWINDOWS),         NEMU_SPI_BOOL,           }
};

#undef SPI_

static void nemuExperienceSet (uint32_t level)
{
    int i;
    for (i = 0; i < RT_ELEMENTS(parameters); i++)
    {
        if (parameters[i].level > level)
        {
            /*
             * The parameter has to be disabled.
             */
            LogFlowFunc(("Saving %s\n", parameters[i].name));

            /* Save the current value. */
            switch (parameters[i].type)
            {
                case NEMU_SPI_STRING:
                {
                    /* The 2nd parameter is size in characters of the buffer.
                     * The 3rd parameter points to the buffer.
                     */
                    SystemParametersInfo (parameters[i].uActionGet,
                                          MAX_PATH,
                                          parameters[i].achSavedValue,
                                          0);
                } break;

                case NEMU_SPI_BOOL:
                case NEMU_SPI_BOOL_PTR:
                {
                    /* The 3rd parameter points to BOOL. */
                    SystemParametersInfo (parameters[i].uActionGet,
                                          0,
                                          parameters[i].achSavedValue,
                                          0);
                } break;

                case NEMU_SPI_PTR:
                {
                    /* The 3rd parameter points to the structure.
                     * The cbSize member of this structure must be set.
                     * The uiParam parameter must alos be set.
                     */
                    if (parameters[i].cbSavedValue > sizeof (parameters[i].achSavedValue))
                    {
                        LogFlowFunc(("Not enough space %d > %d\n", parameters[i].cbSavedValue, sizeof (parameters[i].achSavedValue)));
                        break;
                    }

                    *(UINT *)&parameters[i].achSavedValue[0] = parameters[i].cbSavedValue;

                    SystemParametersInfo (parameters[i].uActionGet,
                                          parameters[i].cbSavedValue,
                                          parameters[i].achSavedValue,
                                          0);
                } break;

                default:
                    break;
            }

            LogFlowFunc(("Disabling %s\n", parameters[i].name));

            /* Disable the feature. */
            switch (parameters[i].type)
            {
                case NEMU_SPI_STRING:
                {
                    /* The 3rd parameter points to the string. */
                    SystemParametersInfo (parameters[i].uActionSet,
                                          0,
                                          parameters[i].pvDisable,
                                          SPIF_SENDCHANGE);
                } break;

                case NEMU_SPI_BOOL:
                {
                    /* The 2nd parameter is BOOL. */
                    SystemParametersInfo (parameters[i].uActionSet,
                                          FALSE,
                                          NULL,
                                          SPIF_SENDCHANGE);
                } break;

                case NEMU_SPI_BOOL_PTR:
                {
                    /* The 3rd parameter is NULL to disable. */
                    SystemParametersInfo (parameters[i].uActionSet,
                                          0,
                                          NULL,
                                          SPIF_SENDCHANGE);
                } break;

                case NEMU_SPI_PTR:
                {
                    /* The 3rd parameter points to the structure. */
                    SystemParametersInfo (parameters[i].uActionSet,
                                          0,
                                          parameters[i].pvDisable,
                                          SPIF_SENDCHANGE);
                } break;

                default:
                    break;
            }
        }
    }
}

static void nemuExperienceRestore (uint32_t level)
{
    int i;
    for (i = 0; i < RT_ELEMENTS(parameters); i++)
    {
        if (parameters[i].level > level)
        {
            LogFlowFunc(("Restoring %s\n", parameters[i].name));

            /* Restore the feature. */
            switch (parameters[i].type)
            {
                case NEMU_SPI_STRING:
                {
                    /* The 3rd parameter points to the string. */
                    SystemParametersInfo (parameters[i].uActionSet,
                                          0,
                                          parameters[i].achSavedValue,
                                          SPIF_SENDCHANGE);
                } break;

                case NEMU_SPI_BOOL:
                {
                    /* The 2nd parameter is BOOL. */
                    SystemParametersInfo (parameters[i].uActionSet,
                                          *(BOOL *)&parameters[i].achSavedValue[0],
                                          NULL,
                                          SPIF_SENDCHANGE);
                } break;

                case NEMU_SPI_BOOL_PTR:
                {
                    /* The 3rd parameter is NULL to disable. */
                    BOOL fSaved = *(BOOL *)&parameters[i].achSavedValue[0];

                    SystemParametersInfo (parameters[i].uActionSet,
                                          0,
                                          fSaved? &fSaved: NULL,
                                          SPIF_SENDCHANGE);
                } break;

                case NEMU_SPI_PTR:
                {
                    /* The 3rd parameter points to the structure. */
                    SystemParametersInfo (parameters[i].uActionSet,
                                          0,
                                          parameters[i].achSavedValue,
                                          SPIF_SENDCHANGE);
                } break;

                default:
                    break;
            }
        }
    }
}


typedef struct _NEMUVRDPCONTEXT
{
    const NEMUSERVICEENV *pEnv;

    uint32_t level;
    BOOL fSavedThemeEnabled;

    RTLDRMOD hModUxTheme;

    HRESULT (* pfnEnableTheming)(BOOL fEnable);
    BOOL (* pfnIsThemeActive)(VOID);
} NEMUVRDPCONTEXT;


static NEMUVRDPCONTEXT gCtx = {0};


int NemuVRDPInit(const NEMUSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread)
{
    LogFlowFunc(("NemuVRDPInit\n"));

    gCtx.pEnv      = pEnv;
    gCtx.level     = VRDP_EXPERIENCE_LEVEL_FULL;
    gCtx.fSavedThemeEnabled = FALSE;

    int rc = RTLdrLoadSystem("UxTheme.dll", false /*fNoUnload*/, &gCtx.hModUxTheme);
    if (RT_SUCCESS(rc))
    {
        *(PFNRT *)&gCtx.pfnEnableTheming = RTLdrGetFunction(gCtx.hModUxTheme, "EnableTheming");
        *(PFNRT *)&gCtx.pfnIsThemeActive = RTLdrGetFunction(gCtx.hModUxTheme, "IsThemeActive");
    }
    else
    {
        gCtx.hModUxTheme = NIL_RTLDRMOD;
        gCtx.pfnEnableTheming = NULL;
        gCtx.pfnIsThemeActive = NULL;
    }

    *pfStartThread = true;
    *ppInstance = &gCtx;
    return VINF_SUCCESS;
}


void NemuVRDPDestroy(const NEMUSERVICEENV *pEnv, void *pInstance)
{
    LogFlowFunc(("NemuVRDPDestroy\n"));
    NEMUVRDPCONTEXT *pCtx = (NEMUVRDPCONTEXT *)pInstance;
    nemuExperienceRestore (pCtx->level);
    if (gCtx.hModUxTheme != NIL_RTLDRMOD)
    {
        RTLdrClose(gCtx.hModUxTheme);
        gCtx.hModUxTheme = NIL_RTLDRMOD;
    }
    return;
}

/**
 * Thread function to wait for and process mode change requests
 */
unsigned __stdcall NemuVRDPThread(void *pInstance)
{
    NEMUVRDPCONTEXT *pCtx = (NEMUVRDPCONTEXT *)pInstance;
    HANDLE gNemuDriver = pCtx->pEnv->hDriver;
    bool fTerminate = false;
    NemuGuestFilterMaskInfo maskInfo;
    DWORD cbReturned;

    maskInfo.u32OrMask = VMMDEV_EVENT_VRDP;
    maskInfo.u32NotMask = 0;
    if (DeviceIoControl (gNemuDriver, NEMUGUEST_IOCTL_CTL_FILTER_MASK, &maskInfo, sizeof (maskInfo), NULL, 0, &cbReturned, NULL))
    {
        LogFlowFunc(("DeviceIOControl(CtlMask - or) succeeded\n"));
    }
    else
    {
        LogFlowFunc(("DeviceIOControl(CtlMask) failed\n"));
        return 0;
    }

    do
    {
        /* wait for the event */
        NemuGuestWaitEventInfo waitEvent;
        waitEvent.u32TimeoutIn   = 5000;
        waitEvent.u32EventMaskIn = VMMDEV_EVENT_VRDP;
        if (DeviceIoControl(gNemuDriver, NEMUGUEST_IOCTL_WAITEVENT, &waitEvent, sizeof(waitEvent), &waitEvent, sizeof(waitEvent), &cbReturned, NULL))
        {
            LogFlowFunc(("DeviceIOControl succeeded\n"));

            /* are we supposed to stop? */
            if (WaitForSingleObject(pCtx->pEnv->hStopEvent, 0) == WAIT_OBJECT_0)
                break;

            LogFlowFunc(("checking event\n"));

            /* did we get the right event? */
            if (waitEvent.u32EventFlagsOut & VMMDEV_EVENT_VRDP)
            {
                /* Call the host to get VRDP status and the experience level. */
                VMMDevVRDPChangeRequest vrdpChangeRequest = {0};

                vrdpChangeRequest.header.size            = sizeof(VMMDevVRDPChangeRequest);
                vrdpChangeRequest.header.version         = VMMDEV_REQUEST_HEADER_VERSION;
                vrdpChangeRequest.header.requestType     = VMMDevReq_GetVRDPChangeRequest;
                vrdpChangeRequest.u8VRDPActive           = 0;
                vrdpChangeRequest.u32VRDPExperienceLevel = 0;

                if (DeviceIoControl (gNemuDriver,
                                     NEMUGUEST_IOCTL_VMMREQUEST(sizeof(VMMDevVRDPChangeRequest)),
                                     &vrdpChangeRequest,
                                     sizeof(VMMDevVRDPChangeRequest),
                                     &vrdpChangeRequest,
                                     sizeof(VMMDevVRDPChangeRequest),
                                     &cbReturned, NULL))
                {
                    LogFlowFunc(("u8VRDPActive = %d, level %d\n", vrdpChangeRequest.u8VRDPActive, vrdpChangeRequest.u32VRDPExperienceLevel));

                    if (vrdpChangeRequest.u8VRDPActive)
                    {
                        pCtx->level = vrdpChangeRequest.u32VRDPExperienceLevel;
                        nemuExperienceSet (pCtx->level);

                        if (pCtx->level == VRDP_EXPERIENCE_LEVEL_ZERO
                            && pCtx->pfnEnableTheming
                            && pCtx->pfnIsThemeActive)
                        {
                            pCtx->fSavedThemeEnabled = pCtx->pfnIsThemeActive ();

                            LogFlowFunc(("pCtx->fSavedThemeEnabled = %d\n", pCtx->fSavedThemeEnabled));

                            if (pCtx->fSavedThemeEnabled)
                            {
                                pCtx->pfnEnableTheming (FALSE);
                            }
                        }
                    }
                    else
                    {
                        if (pCtx->level == VRDP_EXPERIENCE_LEVEL_ZERO
                            && pCtx->pfnEnableTheming
                            && pCtx->pfnIsThemeActive)
                        {
                            if (pCtx->fSavedThemeEnabled)
                            {
                                /* @todo the call returns S_OK but theming remains disabled. */
                                HRESULT hrc = pCtx->pfnEnableTheming (TRUE);
                                LogFlowFunc(("enabling theme rc = 0x%08X\n", hrc));
                                pCtx->fSavedThemeEnabled = FALSE;
                            }
                        }

                        nemuExperienceRestore (pCtx->level);

                        pCtx->level = VRDP_EXPERIENCE_LEVEL_FULL;
                    }
                }
                else
                {
#ifndef DEBUG_andy /* Too noisy for me. */
                    LogFlowFunc(("Error from DeviceIoControl NEMUGUEST_IOCTL_VMMREQUEST\n"));
#endif
                    /* sleep a bit to not eat too much CPU in case the above call always fails */
                    if (WaitForSingleObject(pCtx->pEnv->hStopEvent, 10) == WAIT_OBJECT_0)
                    {
                        fTerminate = true;
                        break;
                    }
                }
            }
        }
        else
        {
#ifndef DEBUG_andy
            LogFlowFunc(("Error from DeviceIoControl NEMUGUEST_IOCTL_WAITEVENT\n"));
#endif
            /* sleep a bit to not eat too much CPU in case the above call always fails */
            if (WaitForSingleObject(pCtx->pEnv->hStopEvent, 10) == WAIT_OBJECT_0)
            {
                fTerminate = true;
                break;
            }
        }
    } while (!fTerminate);

    maskInfo.u32OrMask = 0;
    maskInfo.u32NotMask = VMMDEV_EVENT_VRDP;
    if (DeviceIoControl (gNemuDriver, NEMUGUEST_IOCTL_CTL_FILTER_MASK, &maskInfo, sizeof (maskInfo), NULL, 0, &cbReturned, NULL))
    {
        LogFlowFunc(("DeviceIOControl(CtlMask - not) succeeded\n"));
    }
    else
    {
        LogFlowFunc(("DeviceIOControl(CtlMask) failed\n"));
    }

    LogFlowFunc(("Finished VRDP change request thread\n"));
    return 0;
}

