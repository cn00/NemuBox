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
#ifndef ___Nemu_NemuCrHgsmi_h
#define ___Nemu_NemuCrHgsmi_h

#include <iprt/cdefs.h>
#include <Nemu/NemuUhgsmi.h>

RT_C_DECLS_BEGIN

#if 0
/* enable this in case we include this in a dll*/
# ifdef IN_NEMUCRHGSMI
#  define NEMUCRHGSMI_DECL(a_Type) DECLEXPORT(a_Type) RTCALL
# else
#  define NEMUCRHGSMI_DECL(a_Type) DECLIMPORT(a_Type) RTCALL
# endif
#else
/*enable this in case we include this in a static lib*/
# define NEMUCRHGSMI_DECL(a_Type) a_Type RTCALL
#endif

NEMUCRHGSMI_DECL(int) NemuCrHgsmiInit();
NEMUCRHGSMI_DECL(PNEMUUHGSMI) NemuCrHgsmiCreate(void);
NEMUCRHGSMI_DECL(void) NemuCrHgsmiDestroy(PNEMUUHGSMI pHgsmi);
NEMUCRHGSMI_DECL(int) NemuCrHgsmiTerm(void);

NEMUCRHGSMI_DECL(int) NemuCrHgsmiCtlConGetClientID(PNEMUUHGSMI pHgsmi, uint32_t *pu32ClientID);
NEMUCRHGSMI_DECL(int) NemuCrHgsmiCtlConGetHostCaps(PNEMUUHGSMI pHgsmi, uint32_t *pu32HostCaps);
NEMUCRHGSMI_DECL(int) NemuCrHgsmiCtlConCall(PNEMUUHGSMI pHgsmi, struct NemuGuestHGCMCallInfo *pCallInfo, int cbCallInfo);

RT_C_DECLS_END

#endif

