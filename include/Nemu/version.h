/** @file
 * Nemu Version Management.
 */

/*
 * Copyright (C) 2006-2015 Netease Corporation
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

#ifndef ___Nemu_version_h
#define ___Nemu_version_h

/* Product info. */
#include <product-generated.h>
#include <version-generated.h>

#ifdef RC_INVOKED
/* Some versions of RC has trouble with cdefs.h, so we duplicate these two here. */
# define RT_STR(str)             #str
# define RT_XSTR(str)            RT_STR(str)
#else  /* !RC_INVOKED */

/** Combined version number. */
# define NEMU_VERSION                    (NEMU_VERSION_MAJOR << 16 | NEMU_VERSION_MINOR)
/** Get minor version from combined version. */
# define NEMU_GET_VERSION_MINOR(uVer)    ((uVer) & 0xffff)
/** Get major version from combined version. */
# define NEMU_GET_VERSION_MAJOR(uVer)    ((uVer) >> 16)

/**
 * Make a full version number.
 *
 * The returned number can be used in normal integer comparsions and will yield
 * the expected results.
 *
 * @param   uMajor      The major version number.
 * @param   uMinor      The minor version number.
 * @param   uBuild      The build number.
 * @returns Full version number.
 */
# define NEMU_FULL_VERSION_MAKE(uMajor, uMinor, uBuild) \
    (  (uint32_t)((uMajor) &   0xff) << 24 \
     | (uint32_t)((uMinor) &   0xff) << 16 \
     | (uint32_t)((uBuild) & 0xffff)       \
    )

/** Combined version number. */
# define NEMU_FULL_VERSION              \
    NEMU_FULL_VERSION_MAKE(NEMU_VERSION_MAJOR, NEMU_VERSION_MINOR, NEMU_VERSION_BUILD)
/** Get the major version number from a NEMU_FULL_VERSION style number. */
# define NEMU_FULL_VERSION_GET_MAJOR(uFullVer)  ( ((uFullVer) >> 24) &   0xffU )
/** Get the minor version number from a NEMU_FULL_VERSION style number. */
# define NEMU_FULL_VERSION_GET_MINOR(uFullVer)  ( ((uFullVer) >> 16) &   0xffU )
/** Get the build version number from a NEMU_FULL_VERSION style number. */
# define NEMU_FULL_VERSION_GET_BUILD(uFullVer)  ( ((uFullVer)      ) & 0xffffU )

/**
 * Make a short version number for use in 16 bit version fields.
 *
 * The returned number can be used in normal integer comparsions and will yield
 * the expected results.
 *
 * @param   uMajor      The major version number.
 * @param   uMinor      The minor version number.
 * @returns Short version number.
 */
# define NEMU_SHORT_VERSION_MAKE(uMajor, uMinor) \
    (  (uint16_t)((uMajor) &   0xff) << 8 \
     | (uint16_t)((uMinor) &   0xff)      \
    )

/** Combined short version number. */
# define NEMU_SHORT_VERSION               \
    NEMU_SHORT_VERSION_MAKE(NEMU_VERSION_MAJOR, NEMU_VERSION_MINOR)
/** Get the major version number from a NEMU_SHORT_VERSION style number. */
# define NEMU_SHORT_VERSION_GET_MAJOR(uShortVer)  ( ((uShortVer) >> 8) &   0xffU )
/** Get the minor version number from a NEMU_SHORT_VERSION style number. */
# define NEMU_SHORT_VERSION_GET_MINOR(uShortVer)  ( (uShortVer) &   0xffU )

#endif /* !RC_INVOKED */

/** @name Prefined strings for Windows resource files
 * @{ */
#define NEMU_RC_COMPANY_NAME            NEMU_VENDOR
#define NEMU_RC_LEGAL_COPYRIGHT         "Copyright (C) 2009-" NEMU_C_YEAR " Netease Corporation\0"
#define NEMU_RC_PRODUCT_NAME                    NEMU_PRODUCT
#define NEMU_RC_PRODUCT_NAME_GA                 NEMU_PRODUCT " Guest Additions"
#define NEMU_RC_PRODUCT_NAME_PUEL_EXTPACK       NEMU_PRODUCT " Extension Pack"
#define NEMU_RC_PRODUCT_NAME_DTRACE_EXTPACK     NEMU_PRODUCT " NemuDTrace Extension Pack"
#define NEMU_RC_PRODUCT_NAME_STR                NEMU_RC_PRODUCT_NAME "\0"
#define NEMU_RC_PRODUCT_NAME_GA_STR             NEMU_RC_PRODUCT_NAME_GA "\0"
#define NEMU_RC_PRODUCT_NAME_PUEL_EXTPACK_STR   NEMU_RC_PRODUCT_NAME_PUEL_EXTPACK "\0"
#define NEMU_RC_PRODUCT_NAME_DTRACE_EXTPACK_STR NEMU_RC_PRODUCT_NAME_DTRACE_EXTPACK "\0"
#define NEMU_RC_PRODUCT_VERSION         NEMU_VERSION_MAJOR , NEMU_VERSION_MINOR , NEMU_VERSION_BUILD , NEMU_SVN_REV_MOD_5K
#define NEMU_RC_FILE_VERSION            NEMU_VERSION_MAJOR , NEMU_VERSION_MINOR , NEMU_VERSION_BUILD , NEMU_SVN_REV_MOD_5K
#ifndef NEMU_VERSION_PRERELEASE
# define NEMU_RC_PRODUCT_VERSION_STR    RT_XSTR(NEMU_VERSION_MAJOR) "." RT_XSTR(NEMU_VERSION_MINOR) "." RT_XSTR(NEMU_VERSION_BUILD) "." RT_XSTR(NEMU_SVN_REV) "\0"
# define NEMU_RC_FILE_VERSION_STR       RT_XSTR(NEMU_VERSION_MAJOR) "." RT_XSTR(NEMU_VERSION_MINOR) "." RT_XSTR(NEMU_VERSION_BUILD) "." RT_XSTR(NEMU_SVN_REV) "\0"
#else
# define NEMU_RC_PRODUCT_VERSION_STR    RT_XSTR(NEMU_VERSION_MAJOR) "." RT_XSTR(NEMU_VERSION_MINOR) "." RT_XSTR(NEMU_VERSION_BUILD) "." RT_XSTR(NEMU_SVN_REV) " (" NEMU_VERSION_PRERELEASE ")\0"
# define NEMU_RC_FILE_VERSION_STR       RT_XSTR(NEMU_VERSION_MAJOR) "." RT_XSTR(NEMU_VERSION_MINOR) "." RT_XSTR(NEMU_VERSION_BUILD) "." RT_XSTR(NEMU_SVN_REV) " (" NEMU_VERSION_PRERELEASE ")\0"
#endif
#define NEMU_RC_FILE_OS                 VOS_NT_WINDOWS32
#define NEMU_RC_TYPE_DLL                VFT_DLL
#define NEMU_RC_TYPE_APP                VFT_APP
#define NEMU_RC_TYPE_DRV                VFT_DRV
/* Flags and extra strings depending on the build type and who's building. */
#if defined(DEBUG) || defined(LOG_ENABLED) || defined(RT_STRICT) || defined(NEMU_STRICT) || defined(NEMU_WITH_STATISTICS)
# define NEMU_RC_FILE_FLAGS_DEBUG       VS_FF_DEBUG
#else
# define NEMU_RC_FILE_FLAGS_DEBUG       0
#endif
#if NEMU_VERSION_MINOR >= 51 || defined(NEMU_VERSION_PRERELEASE)
# define NEMU_RC_FILE_FLAGS_PRERELEASE  VS_FF_PRERELEASE
#else
# define NEMU_RC_FILE_FLAGS_PRERELEASE  0
#endif
#if defined(NEMU_BUILD_SERVER_BUILD) && (NEMU_VERSION_MINOR & 1) == 0
# define NEMU_RC_FILE_FLAGS_BUILD       0
# define NEMU_RC_MORE_STRINGS
#elif defined(NEMU_BUILD_SERVER_BUILD)
# define NEMU_RC_FILE_FLAGS_BUILD       VS_FF_SPECIALBUILD
# define NEMU_RC_MORE_STRINGS           VALUE "SpecialBuild", "r" RT_XSTR(NEMU_SVN_REV) "\0"
#else
# define NEMU_RC_FILE_FLAGS_BUILD       VS_FF_PRIVATEBUILD
# ifdef NEMU_PRIVATE_BUILD_DESC
#  define NEMU_RC_MORE_STRINGS          VALUE "PrivateBuild", NEMU_PRIVATE_BUILD_DESC "\0"
# else
#  define NEMU_RC_MORE_STRINGS          VALUE "PrivateBuild", "r" RT_XSTR(NEMU_SVN_REV) "\0"
# error
# endif
#endif
#define NEMU_RC_FILE_FLAGS              (NEMU_RC_FILE_FLAGS_DEBUG | NEMU_RC_FILE_FLAGS_PRERELEASE | NEMU_RC_FILE_FLAGS_BUILD)
/** @} */

#endif

