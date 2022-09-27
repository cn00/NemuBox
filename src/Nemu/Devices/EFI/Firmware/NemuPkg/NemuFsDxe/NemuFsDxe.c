/* $Id: NemuFsDxe.c $ */
/** @file
 * NemuFsDxe.c - VirtualBox FS wrapper
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/PciIo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <IndustryStandard/Pci22.h>

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static EFI_STATUS EFIAPI
NemuFsDB_Supported(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
                   IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL);
static EFI_STATUS EFIAPI
NemuFsDB_Start(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
               IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL);
static EFI_STATUS EFIAPI
NemuFsDB_Stop(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
              IN UINTN NumberOfChildren, IN EFI_HANDLE *ChildHandleBuffer OPTIONAL);

static EFI_STATUS EFIAPI
NemuFsCN_GetDriverName(IN EFI_COMPONENT_NAME_PROTOCOL *This,
                       IN CHAR8 *Language, OUT CHAR16 **DriverName);
static EFI_STATUS EFIAPI
NemuFsCN_GetControllerName(IN EFI_COMPONENT_NAME_PROTOCOL *This,
                           IN EFI_HANDLE ControllerHandle,
                           IN EFI_HANDLE ChildHandle OPTIONAL,
                           IN CHAR8 *Language,  OUT CHAR16 **ControllerName);

static EFI_STATUS EFIAPI
NemuFsCN2_GetDriverName(IN EFI_COMPONENT_NAME2_PROTOCOL *This,
                        IN CHAR8 *Language, OUT CHAR16 **DriverName);
static EFI_STATUS EFIAPI
NemuFsCN2_GetControllerName(IN EFI_COMPONENT_NAME2_PROTOCOL *This,
                            IN EFI_HANDLE ControllerHandle,
                            IN EFI_HANDLE ChildHandle OPTIONAL,
                            IN CHAR8 *Language,  OUT CHAR16 **ControllerName);


/** EFI Driver Binding Protocol. */
static EFI_DRIVER_BINDING_PROTOCOL          g_NemuFsDB =
{
    NemuFsDB_Supported,
    NemuFsDB_Start,
    NemuFsDB_Stop,
    /* .Version             = */    1,
    /* .ImageHandle         = */ NULL,
    /* .DriverBindingHandle = */ NULL
};

/** EFI Component Name Protocol. */
static const EFI_COMPONENT_NAME_PROTOCOL    g_NemuFsCN =
{
    NemuFsCN_GetDriverName,
    NemuFsCN_GetControllerName,
    "eng"
};

/** EFI Component Name 2 Protocol. */
static const EFI_COMPONENT_NAME2_PROTOCOL   g_NemuFsCN2 =
{
    NemuFsCN2_GetDriverName,
    NemuFsCN2_GetControllerName,
    "en"
};

/** Driver name translation table. */
static CONST EFI_UNICODE_STRING_TABLE       g_aNemuFsDriverLangAndNames[] =
{
    {   "eng;en",   L"Nemu Universal FS Wrapper Driver" },
    {   NULL,       NULL }
};



/**
 * NemuFsDxe entry point.
 *
 * @returns EFI status code.
 *
 * @param   ImageHandle     The image handle.
 * @param   SystemTable     The system table pointer.
 */
EFI_STATUS EFIAPI
DxeInitializeNemuFs(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS  rc;
    DEBUG((DEBUG_INFO, "DxeInitializeNemuFsDxe\n"));

    rc = EfiLibInstallDriverBindingComponentName2(ImageHandle, SystemTable,
                                                  &g_NemuFsDB, ImageHandle,
                                                  &g_NemuFsCN, &g_NemuFsCN2);
    ASSERT_EFI_ERROR(rc);
    return rc;
}

EFI_STATUS EFIAPI
DxeUninitializeNemuFs(IN EFI_HANDLE         ImageHandle)
{
    return EFI_SUCCESS;
}


/**
 * @copydoc EFI_DRIVER_BINDING_SUPPORTED
 */
static EFI_STATUS EFIAPI
NemuFsDB_Supported(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
                   IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL)
{
    EFI_STATUS              rcRet = EFI_UNSUPPORTED;
    /* EFI_STATUS              rc; */

    return rcRet;
}


/**
 * @copydoc EFI_DRIVER_BINDING_START
 */
static EFI_STATUS EFIAPI
NemuFsDB_Start(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
               IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL)
{
    /* EFI_STATUS              rc; */

    return  EFI_UNSUPPORTED;
}


/**
 * @copydoc EFI_DRIVER_BINDING_STOP
 */
static EFI_STATUS EFIAPI
NemuFsDB_Stop(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
              IN UINTN NumberOfChildren, IN EFI_HANDLE *ChildHandleBuffer OPTIONAL)
{
    /* EFI_STATUS                  rc; */

    return  EFI_UNSUPPORTED;
}


/** @copydoc EFI_COMPONENT_NAME_GET_DRIVER_NAME */
static EFI_STATUS EFIAPI
NemuFsCN_GetDriverName(IN EFI_COMPONENT_NAME_PROTOCOL *This,
                       IN CHAR8 *Language, OUT CHAR16 **DriverName)
{
    return LookupUnicodeString2(Language,
                                This->SupportedLanguages,
                                &g_aNemuFsDriverLangAndNames[0],
                                DriverName,
                                TRUE);
}

/** @copydoc EFI_COMPONENT_NAME_GET_CONTROLLER_NAME */
static EFI_STATUS EFIAPI
NemuFsCN_GetControllerName(IN EFI_COMPONENT_NAME_PROTOCOL *This,
                                    IN EFI_HANDLE ControllerHandle,
                                    IN EFI_HANDLE ChildHandle OPTIONAL,
                                    IN CHAR8 *Language, OUT CHAR16 **ControllerName)
{
    /** @todo try query the protocol from the controller and forward the query. */
    return EFI_UNSUPPORTED;
}

/** @copydoc EFI_COMPONENT_NAME2_GET_DRIVER_NAME */
static EFI_STATUS EFIAPI
NemuFsCN2_GetDriverName(IN EFI_COMPONENT_NAME2_PROTOCOL *This,
                        IN CHAR8 *Language, OUT CHAR16 **DriverName)
{
    return LookupUnicodeString2(Language,
                                This->SupportedLanguages,
                                &g_aNemuFsDriverLangAndNames[0],
                                DriverName,
                                FALSE);
}

/** @copydoc EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME */
static EFI_STATUS EFIAPI
NemuFsCN2_GetControllerName(IN EFI_COMPONENT_NAME2_PROTOCOL *This,
                            IN EFI_HANDLE ControllerHandle,
                            IN EFI_HANDLE ChildHandle OPTIONAL,
                            IN CHAR8 *Language, OUT CHAR16 **ControllerName)
{
    /** @todo try query the protocol from the controller and forward the query. */
    return EFI_UNSUPPORTED;
}
