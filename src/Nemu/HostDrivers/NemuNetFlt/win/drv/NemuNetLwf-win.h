/* $Id: NemuNetLwf-win.h $ */
/** @file
 * NemuNetLwf-win.h - Bridged Networking Driver, Windows-specific code.
 */
/*
 * Copyright (C) 2014-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___NemuNetLwf_win_h___
#define ___NemuNetLwf_win_h___

#define NEMUNETLWF_VERSION_NDIS_MAJOR        6
#define NEMUNETLWF_VERSION_NDIS_MINOR        0

#define NEMUNETLWF_NAME_FRIENDLY             L"VirtualBox NDIS Light-Weight Filter"
#define NEMUNETLWF_NAME_UNIQUE               L"{7af6b074-048d-4444-bfce-1ecc8bc5cb76}"
#define NEMUNETLWF_NAME_SERVICE              L"NemuNetLwf"

#define NEMUNETLWF_NAME_LINK                 L"\\DosDevices\\Global\\NemuNetLwf"
#define NEMUNETLWF_NAME_DEVICE               L"\\Device\\NemuNetLwf"

#define NEMUNETLWF_MEM_TAG                   'FLBV'
#define NEMUNETLWF_REQ_ID                    'fLBV'

#endif /* #ifndef ___NemuNetLwf_win_h___ */
