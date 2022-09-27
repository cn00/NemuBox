/* $Id: ExtPackUtil.h $ */
/** @file
 * VirtualBox Main - Extension Pack Utilities and definitions, NemuC, NemuSVC, ++.
 */

/*
 * Copyright (C) 2010-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_EXTPACKUTIL
#define ____H_EXTPACKUTIL

#include <iprt/cpp/ministring.h>
#include <iprt/fs.h>
#include <iprt/vfs.h>


/** @name NEMU_EXTPACK_DESCRIPTION_NAME
 * The name of the description file in an extension pack.  */
#define NEMU_EXTPACK_DESCRIPTION_NAME   "ExtPack.xml"
/** @name NEMU_EXTPACK_DESCRIPTION_NAME
 * The name of the manifest file in an extension pack.  */
#define NEMU_EXTPACK_MANIFEST_NAME      "ExtPack.manifest"
/** @name NEMU_EXTPACK_SIGNATURE_NAME
 * The name of the signature file in an extension pack.  */
#define NEMU_EXTPACK_SIGNATURE_NAME     "ExtPack.signature"
/** @name NEMU_EXTPACK_LICENSE_NAME_PREFIX
 * The name prefix of a license file in an extension pack. There can be
 * several license files in a pack, the variations being on locale, language
 * and format (HTML, RTF, plain text). All extension packages shall include
 * a  */
#define NEMU_EXTPACK_LICENSE_NAME_PREFIX "ExtPack-license"
/** @name NEMU_EXTPACK_SUFFIX
 * The suffix of a extension pack tarball. */
#define NEMU_EXTPACK_SUFFIX             ".nemu-extpack"

/** The minimum length (strlen) of a extension pack name. */
#define NEMU_EXTPACK_NAME_MIN_LEN       3
/** The max length (strlen) of a extension pack name. */
#define NEMU_EXTPACK_NAME_MAX_LEN       64

/** The architecture-dependent application data subdirectory where the
 * extension packs are installed.  Relative to RTPathAppPrivateArch. */
#define NEMU_EXTPACK_INSTALL_DIR        "ExtensionPacks"
/** The architecture-independent application data subdirectory where the
 * certificates are installed.  Relative to RTPathAppPrivateNoArch. */
#define NEMU_EXTPACK_CERT_DIR           "ExtPackCertificates"

/** The maximum entry name length.
 * Play short and safe. */
#define NEMU_EXTPACK_MAX_MEMBER_NAME_LENGTH 128


/**
 * Plug-in descriptor.
 */
typedef struct NEMUEXTPACKPLUGINDESC
{
    /** The name. */
    RTCString        strName;
    /** The module name. */
    RTCString        strModule;
    /** The description. */
    RTCString        strDescription;
    /** The frontend or component which it plugs into. */
    RTCString        strFrontend;
} NEMUEXTPACKPLUGINDESC;
/** Pointer to a plug-in descriptor. */
typedef NEMUEXTPACKPLUGINDESC *PNEMUEXTPACKPLUGINDESC;

/**
 * Extension pack descriptor
 *
 * This is the internal representation of the ExtPack.xml.
 */
typedef struct NEMUEXTPACKDESC
{
    /** The name. */
    RTCString               strName;
    /** The description. */
    RTCString               strDescription;
    /** The version string. */
    RTCString               strVersion;
    /** The edition string. */
    RTCString               strEdition;
    /** The internal revision number. */
    uint32_t                uRevision;
    /** The name of the main module. */
    RTCString               strMainModule;
    /** The name of the VRDE module, empty if none. */
    RTCString               strVrdeModule;
    /** The number of plug-in descriptors. */
    uint32_t                cPlugIns;
    /** Pointer to an array of plug-in descriptors. */
    PNEMUEXTPACKPLUGINDESC  paPlugIns;
    /** Whether to show the license prior to installation. */
    bool                    fShowLicense;
} NEMUEXTPACKDESC;

/** Pointer to a extension pack descriptor. */
typedef NEMUEXTPACKDESC *PNEMUEXTPACKDESC;
/** Pointer to a const extension pack descriptor. */
typedef NEMUEXTPACKDESC const *PCNEMUEXTPACKDESC;


void                NemuExtPackInitDesc(PNEMUEXTPACKDESC a_pExtPackDesc);
RTCString          *NemuExtPackLoadDesc(const char *a_pszDir, PNEMUEXTPACKDESC a_pExtPackDesc, PRTFSOBJINFO a_pObjInfo);
RTCString          *NemuExtPackLoadDescFromVfsFile(RTVFSFILE hVfsFile, PNEMUEXTPACKDESC a_pExtPackDesc, PRTFSOBJINFO a_pObjInfo);
RTCString          *NemuExtPackExtractNameFromTarballPath(const char *pszTarball);
void                NemuExtPackFreeDesc(PNEMUEXTPACKDESC a_pExtPackDesc);
bool                NemuExtPackIsValidName(const char *pszName);
bool                NemuExtPackIsValidMangledName(const char *pszMangledName, size_t cchMax = RTSTR_MAX);
RTCString          *NemuExtPackMangleName(const char *pszName);
RTCString          *NemuExtPackUnmangleName(const char *pszMangledName, size_t cbMax);
int                 NemuExtPackCalcDir(char *pszExtPackDir, size_t cbExtPackDir, const char *pszParentDir, const char *pszName);
bool                NemuExtPackIsValidVersionString(const char *pszVersion);
bool                NemuExtPackIsValidEditionString(const char *pszEdition);
bool                NemuExtPackIsValidModuleString(const char *pszModule);

int                 NemuExtPackValidateMember(const char *pszName, RTVFSOBJTYPE enmType, RTVFSOBJ hVfsObj, char *pszError, size_t cbError);
int                 NemuExtPackOpenTarFss(RTFILE hTarballFile, char *pszError, size_t cbError, PRTVFSFSSTREAM phTarFss, PRTMANIFEST phFileManifest);
int                 NemuExtPackValidateTarball(RTFILE hTarballFile, const char *pszExtPackName,
                                               const char *pszTarball, const char *pszTarballDigest,
                                               char *pszError, size_t cbError,
                                               PRTMANIFEST phValidManifest, PRTVFSFILE phXmlFile, RTCString *pStrDigest);


#endif

