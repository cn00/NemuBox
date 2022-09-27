/* $Id: NemuStubBld.h $ */
/** @file
 * NemuStubBld - VirtualBox's Windows installer stub builder.
 */

/*
 * Copyright (C) 2009-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuStubBld_h___
#define ___NemuStubBld_h___

#define NEMUSTUB_MAX_PACKAGES 128

typedef struct NEMUSTUBPKGHEADER
{
    /** Some magic string not defined by this header? Turns out it's a write only
     *  field... */
    char    szMagic[9];
    /* Inbetween szMagic and dwVersion there are 3 bytes of implicit padding. */
    /** Some version number not defined by this header? Also write only field.
     *  Should be a uint32_t, not DWORD. */
    DWORD   dwVersion;
    /** Number of packages following the header. byte is prefixed 'b', not 'by'!
     *  Use uint8_t instead of BYTE. */
    BYTE    byCntPkgs;
    /* There are 3 bytes of implicit padding here. */
} NEMUSTUBPKGHEADER;
typedef NEMUSTUBPKGHEADER *PNEMUSTUBPKGHEADER;

typedef enum NEMUSTUBPKGARCH
{
    NEMUSTUBPKGARCH_ALL = 0,
    NEMUSTUBPKGARCH_X86,
    NEMUSTUBPKGARCH_AMD64
} NEMUSTUBPKGARCH;

typedef struct NEMUSTUBPKG
{
    BYTE byArch;
    /** Probably the name of the PE resource or something, read the source to
     *  find out for sure.  Don't use _MAX_PATH, define your own max lengths! */
    char szResourceName[_MAX_PATH];
    char szFileName[_MAX_PATH];
} NEMUSTUBPKG;
typedef NEMUSTUBPKG *PNEMUSTUBPKG;

/* Only for construction. */
/* Since it's only used by NemuStubBld.cpp, why not just keep it there? */

typedef struct NEMUSTUBBUILDPKG
{
    char szSourcePath[_MAX_PATH];
    BYTE byArch;
} NEMUSTUBBUILDPKG;
typedef NEMUSTUBBUILDPKG *PNEMUSTUBBUILDPKG;

#endif
