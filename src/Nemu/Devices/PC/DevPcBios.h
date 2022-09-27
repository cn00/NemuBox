/* $Id: DevPcBios.h $ */
/** @file
 * DevPcBios - PC BIOS Device, header shared with the BIOS code.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef DEV_PCBIOS_H
#define DEV_PCBIOS_H

/** @def NEMU_DMI_TABLE_BASE */
#define NEMU_DMI_TABLE_BASE         0xe1000
#define NEMU_DMI_TABLE_VER          0x25

/** def NEMU_DMI_TABLE_SIZE
 *
 * The size should be at least 16-byte aligned for a proper alignment of
 * the MPS table.
 */
#define NEMU_DMI_TABLE_SIZE         768


/** @def NEMU_LANBOOT_SEG
 *
 * Should usually start right after the DMI BIOS page
 */
#define NEMU_LANBOOT_SEG            0xe200

#define NEMU_SMBIOS_MAJOR_VER       2
#define NEMU_SMBIOS_MINOR_VER       5
#define NEMU_SMBIOS_MAXSS           0xff   /* Not very accurate */

#endif
