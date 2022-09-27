/* $Id: NemuNetAdp-win.h $ */
/** @file
 * NemuNetAdp-win.h - Host-only Miniport Driver, Windows-specific code.
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
#ifndef ___NemuNetAdp_win_h___
#define ___NemuNetAdp_win_h___

#define NEMUNETADP_VERSION_NDIS_MAJOR        6
#define NEMUNETADP_VERSION_NDIS_MINOR        0

#define NEMUNETADP_VERSION_MAJOR             1
#define NEMUNETADP_VERSION_MINOR             0

#define NEMUNETADP_VENDOR_NAME               "Oracle"
#define NEMUNETADP_VENDOR_ID                 0xFFFFFF
#define NEMUNETADP_MCAST_LIST_SIZE           32
#define NEMUNETADP_MAX_FRAME_SIZE            1518 // TODO: 14+4+1500

//#define NEMUNETADP_NAME_UNIQUE               L"{7af6b074-048d-4444-bfce-1ecc8bc5cb76}"
#define NEMUNETADP_NAME_SERVICE              L"NemuNetAdp"

#define NEMUNETADP_NAME_LINK                 L"\\DosDevices\\Global\\NemuNetAdp"
#define NEMUNETADP_NAME_DEVICE               L"\\Device\\NemuNetAdp"

#define NEMUNETADPWIN_TAG                    'ANBV'

#define NEMUNETADPWIN_ATTR_FLAGS             NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM | NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND
#define NEMUNETADP_MAC_OPTIONS               NDIS_MAC_OPTION_NO_LOOPBACK
#define NEMUNETADP_SUPPORTED_FILTERS         (NDIS_PACKET_TYPE_DIRECTED | \
                                              NDIS_PACKET_TYPE_MULTICAST | \
                                              NDIS_PACKET_TYPE_BROADCAST | \
                                              NDIS_PACKET_TYPE_PROMISCUOUS | \
                                              NDIS_PACKET_TYPE_ALL_MULTICAST)
#define NEMUNETADPWIN_SUPPORTED_STATISTICS   0 //TODO!
#define NEMUNETADPWIN_HANG_CHECK_TIME        4

#endif /* #ifndef ___NemuNetAdp_win_h___ */
