/* $Id: NemuCredentialProvider.h $ */
/** @file
 * NemuCredentialProvider - Main file of the VirtualBox Credential Provider.
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NEMU_CREDENTIALPROVIDER_H___
#define ___NEMU_CREDENTIALPROVIDER_H___

#include <Windows.h>
#include <credentialprovider.h>

#include "NemuCredProvUtils.h"

/** The VirtualBox credential provider class ID -- must not be changed. */
DEFINE_GUID(CLSID_NemuCredProvider, 0x275d3bcc, 0x22bb, 0x4948, 0xa7, 0xf6, 0x3a, 0x30, 0x54, 0xeb, 0xa9, 0x2b);

/**
 * The credential provider's UI field IDs, used for
 * handling / identifying them.
 */
enum NEMUCREDPROV_FIELDID
{
    NEMUCREDPROV_FIELDID_TILEIMAGE       = 0,
    NEMUCREDPROV_FIELDID_USERNAME        = 1,
    NEMUCREDPROV_FIELDID_PASSWORD        = 2,
    NEMUCREDPROV_FIELDID_DOMAINNAME      = 3,
    NEMUCREDPROV_FIELDID_SUBMIT_BUTTON   = 4
};

/* Note: If new fields are added to NEMUCREDPROV_FIELDID and s_NemuCredProvFields,
         don't forget to increase this define! */
#define NEMUCREDPROV_NUM_FIELDS            5

struct NEMUCREDPROV_FIELD
{
    /** The actual description of this field: It's label,
     *  official field type ID, ... */
    CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR desc;
    /** The field's display state within the. */
    CREDENTIAL_PROVIDER_FIELD_STATE state;
    /** The interactive state: Used when this field gets shown to determine
     *  its state -- currently, only focussing is implemented. */
    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE stateInteractive;
};

#ifndef PCREDENTIAL_PROVIDER_FIELD_DESCRIPTOR
 #define PCREDENTIAL_PROVIDER_FIELD_DESCRIPTOR CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*
#endif

static const NEMUCREDPROV_FIELD s_NemuCredProvFields[] =
{
    /** The user's profile image. */
    { { NEMUCREDPROV_FIELDID_TILEIMAGE,     CPFT_TILE_IMAGE,    L"Image" },       CPFS_DISPLAY_IN_BOTH,          CPFIS_NONE    },
    { { NEMUCREDPROV_FIELDID_USERNAME,      CPFT_LARGE_TEXT,    L"Username" },    CPFS_DISPLAY_IN_BOTH,          CPFIS_NONE    },
    { { NEMUCREDPROV_FIELDID_PASSWORD,      CPFT_PASSWORD_TEXT, L"Password" },    CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_FOCUSED },
    { { NEMUCREDPROV_FIELDID_SUBMIT_BUTTON, CPFT_SUBMIT_BUTTON, L"Submit" },      CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE    },
    { { NEMUCREDPROV_FIELDID_DOMAINNAME,    CPFT_LARGE_TEXT,    L"Domain Name" }, CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_FOCUSED }
};

/** Prototypes. */
void NemuCredentialProviderAcquire(void);
void NemuCredentialProviderRelease(void);
LONG NemuCredentialProviderRefCount(void);

HRESULT NemuCredentialProviderCreate(REFCLSID classID,
                                     REFIID interfaceID, void **ppvInterface);

#endif /* !___NEMU_CREDENTIALPROVIDER_H___ */

