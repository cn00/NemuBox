/* $Id: NemuDD2.h $ */
/** @file
 * Built-in drivers & devices part 2 header.
 *
 * These drivers and devices are in separate modules because of LGPL.
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

#ifndef ___build_NemuDD2_h
#define ___build_NemuDD2_h

#include <Nemu/vmm/pdm.h>

RT_C_DECLS_BEGIN

#ifdef IN_NEMUDD2
extern DECLEXPORT(const unsigned char)  g_abPcBiosBinary[];
extern DECLEXPORT(const unsigned)       g_cbPcBiosBinary;
extern DECLEXPORT(const unsigned char)  g_abVgaBiosBinary[];
extern DECLEXPORT(const unsigned)       g_cbVgaBiosBinary;
# ifdef NEMU_WITH_PXE_ROM
extern DECLEXPORT(const unsigned char)  g_abNetBiosBinary[];
extern DECLEXPORT(const unsigned)       g_cbNetBiosBinary;
# endif
#else  /* !IN_NEMUDD2 */
extern DECLIMPORT(const unsigned char)  g_abPcBiosBinary[];
extern DECLIMPORT(const unsigned)       g_cbPcBiosBinary;
extern DECLIMPORT(const unsigned char)  g_abVgaBiosBinary[];
extern DECLIMPORT(const unsigned)       g_cbVgaBiosBinary;
# ifdef NEMU_WITH_PXE_ROM
extern DECLIMPORT(const unsigned char)  g_abNetBiosBinary[];
extern DECLIMPORT(const unsigned)       g_cbNetBiosBinary;
# endif
#endif /* !IN_NEMUDD2 */
extern const PDMDEVREG g_DeviceAPIC;
extern const PDMDEVREG g_DeviceIOAPIC;
extern const PDMDEVREG g_DeviceLPC;

RT_C_DECLS_END

#endif

