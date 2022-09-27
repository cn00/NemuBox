/** @file
 * VirtualBox - Global Guest Operating System definition.
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

#ifndef ___Nemu_ostypes_h
#define ___Nemu_ostypes_h

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

/**
 * Global list of guest operating system types.
 *
 * They are grouped into families. A family identifer is always has
 * mod 0x10000 == 0. New entries can be added, however other components
 * depend on the values (e.g. the Qt GUI and guest additions) so the
 * existing values MUST stay the same.
 *
 * Note: distinguish between 32 & 64 bits guest OSes by checking bit 8 (mod 0x100)
 */
typedef enum NEMUOSTYPE
{
    NEMUOSTYPE_Unknown          = 0,
    NEMUOSTYPE_Unknown_x64      = 0x00100,
    NEMUOSTYPE_DOS              = 0x10000,
    NEMUOSTYPE_Win31            = 0x15000,
    NEMUOSTYPE_Win9x            = 0x20000,
    NEMUOSTYPE_Win95            = 0x21000,
    NEMUOSTYPE_Win98            = 0x22000,
    NEMUOSTYPE_WinMe            = 0x23000,
    NEMUOSTYPE_WinNT            = 0x30000,
    NEMUOSTYPE_WinNT_x64        = 0x30100,
    NEMUOSTYPE_WinNT4           = 0x31000,
    NEMUOSTYPE_Win2k            = 0x32000,
    NEMUOSTYPE_WinXP            = 0x33000,
    NEMUOSTYPE_WinXP_x64        = 0x33100,
    NEMUOSTYPE_Win2k3           = 0x34000,
    NEMUOSTYPE_Win2k3_x64       = 0x34100,
    NEMUOSTYPE_WinVista         = 0x35000,
    NEMUOSTYPE_WinVista_x64     = 0x35100,
    NEMUOSTYPE_Win2k8           = 0x36000,
    NEMUOSTYPE_Win2k8_x64       = 0x36100,
    NEMUOSTYPE_Win7             = 0x37000,
    NEMUOSTYPE_Win7_x64         = 0x37100,
    NEMUOSTYPE_Win8             = 0x38000,
    NEMUOSTYPE_Win8_x64         = 0x38100,
    NEMUOSTYPE_Win2k12_x64      = 0x39100,
    NEMUOSTYPE_Win81            = 0x3A000,
    NEMUOSTYPE_Win81_x64        = 0x3A100,
    NEMUOSTYPE_Win10            = 0x3B000,
    NEMUOSTYPE_Win10_x64        = 0x3B100,
    NEMUOSTYPE_OS2              = 0x40000,
    NEMUOSTYPE_OS2Warp3         = 0x41000,
    NEMUOSTYPE_OS2Warp4         = 0x42000,
    NEMUOSTYPE_OS2Warp45        = 0x43000,
    NEMUOSTYPE_ECS              = 0x44000,
    NEMUOSTYPE_OS21x            = 0x48000,
    NEMUOSTYPE_Linux            = 0x50000,
    NEMUOSTYPE_Linux_x64        = 0x50100,
    NEMUOSTYPE_Linux22          = 0x51000,
    NEMUOSTYPE_Linux24          = 0x52000,
    NEMUOSTYPE_Linux24_x64      = 0x52100,
    NEMUOSTYPE_Linux26          = 0x53000,
    NEMUOSTYPE_Linux26_x64      = 0x53100,
    NEMUOSTYPE_ArchLinux        = 0x54000,
    NEMUOSTYPE_ArchLinux_x64    = 0x54100,
    NEMUOSTYPE_Debian           = 0x55000,
    NEMUOSTYPE_Debian_x64       = 0x55100,
    NEMUOSTYPE_OpenSUSE         = 0x56000,
    NEMUOSTYPE_OpenSUSE_x64     = 0x56100,
    NEMUOSTYPE_FedoraCore       = 0x57000,
    NEMUOSTYPE_FedoraCore_x64   = 0x57100,
    NEMUOSTYPE_Gentoo           = 0x58000,
    NEMUOSTYPE_Gentoo_x64       = 0x58100,
    NEMUOSTYPE_Mandriva         = 0x59000,
    NEMUOSTYPE_Mandriva_x64     = 0x59100,
    NEMUOSTYPE_RedHat           = 0x5A000,
    NEMUOSTYPE_RedHat_x64       = 0x5A100,
    NEMUOSTYPE_Turbolinux       = 0x5B000,
    NEMUOSTYPE_Turbolinux_x64   = 0x5B100,
    NEMUOSTYPE_Ubuntu           = 0x5C000,
    NEMUOSTYPE_Ubuntu_x64       = 0x5C100,
    NEMUOSTYPE_Xandros          = 0x5D000,
    NEMUOSTYPE_Xandros_x64      = 0x5D100,
    NEMUOSTYPE_Oracle           = 0x5E000,
    NEMUOSTYPE_Oracle_x64       = 0x5E100,
    NEMUOSTYPE_FreeBSD          = 0x60000,
    NEMUOSTYPE_FreeBSD_x64      = 0x60100,
    NEMUOSTYPE_OpenBSD          = 0x61000,
    NEMUOSTYPE_OpenBSD_x64      = 0x61100,
    NEMUOSTYPE_NetBSD           = 0x62000,
    NEMUOSTYPE_NetBSD_x64       = 0x62100,
    NEMUOSTYPE_Netware          = 0x70000,
    NEMUOSTYPE_Solaris          = 0x80000,
    NEMUOSTYPE_Solaris_x64      = 0x80100,
    NEMUOSTYPE_OpenSolaris      = 0x81000,
    NEMUOSTYPE_OpenSolaris_x64  = 0x81100,
    NEMUOSTYPE_Solaris11_x64    = 0x82100,
    NEMUOSTYPE_L4               = 0x90000,
    NEMUOSTYPE_QNX              = 0xA0000,
    NEMUOSTYPE_MacOS            = 0xB0000,
    NEMUOSTYPE_MacOS_x64        = 0xB0100,
    NEMUOSTYPE_MacOS106         = 0xB2000,
    NEMUOSTYPE_MacOS106_x64     = 0xB2100,
    NEMUOSTYPE_MacOS107_x64     = 0xB3100,
    NEMUOSTYPE_MacOS108_x64     = 0xB4100,
    NEMUOSTYPE_MacOS109_x64     = 0xB5100,
    NEMUOSTYPE_MacOS1010_x64    = 0xB6100,
    NEMUOSTYPE_MacOS1011_x64    = 0xB7100,
    NEMUOSTYPE_JRockitVE        = 0xC0000,
    NEMUOSTYPE_Haiku            = 0xD0000,
    NEMUOSTYPE_Haiku_x64        = 0xD0100,
/** The bit number which indicates 64-bit or 32-bit. */
#define NEMUOSTYPE_x64_BIT       8
    /** The mask which indicates 64-bit. */
    NEMUOSTYPE_x64            = 1 << NEMUOSTYPE_x64_BIT,
    /** The usual 32-bit hack. */
    NEMUOSTYPE_32BIT_HACK = 0x7fffffff
} NEMUOSTYPE;


/**
 * Global list of guest OS families.
 */
typedef enum NEMUOSFAMILY
{
    NEMUOSFAMILY_Unknown          = 0,
    NEMUOSFAMILY_Windows32        = 1,
    NEMUOSFAMILY_Windows64        = 2,
    NEMUOSFAMILY_Linux32          = 3,
    NEMUOSFAMILY_Linux64          = 4,
    NEMUOSFAMILY_FreeBSD32        = 5,
    NEMUOSFAMILY_FreeBSD64        = 6,
    NEMUOSFAMILY_Solaris32        = 7,
    NEMUOSFAMILY_Solaris64        = 8,
    NEMUOSFAMILY_MacOSX32         = 9,
    NEMUOSFAMILY_MacOSX64         = 10,
    /** The usual 32-bit hack. */
    NEMUOSFAMILY_32BIT_HACK = 0x7fffffff
} NEMUOSFAMILY;

RT_C_DECLS_END

#endif
