/* $Id: NemuUSBFilterMgr.h $ */
/** @file
 * VirtualBox Ring-0 USB Filter Manager.
 */

/*
 * Copyright (C) 2007-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuUSBFilterMgr_h
#define ___NemuUSBFilterMgr_h

#include <Nemu/usbfilter.h>

RT_C_DECLS_BEGIN

/** @todo r=bird: NEMUUSBFILTER_CONTEXT isn't following the coding
 *        guildlines. Don't know which clueless dude did this...  */
#if defined(RT_OS_WINDOWS)
typedef struct NEMUUSBFLTCTX *NEMUUSBFILTER_CONTEXT;
#define NEMUUSBFILTER_CONTEXT_NIL NULL
#else
typedef RTPROCESS NEMUUSBFILTER_CONTEXT;
#define NEMUUSBFILTER_CONTEXT_NIL NIL_RTPROCESS
#endif

int     NemuUSBFilterInit(void);
void    NemuUSBFilterTerm(void);
void    NemuUSBFilterRemoveOwner(NEMUUSBFILTER_CONTEXT Owner);
int     NemuUSBFilterAdd(PCUSBFILTER pFilter, NEMUUSBFILTER_CONTEXT Owner, uintptr_t *puId);
int     NemuUSBFilterRemove(NEMUUSBFILTER_CONTEXT Owner, uintptr_t uId);
NEMUUSBFILTER_CONTEXT NemuUSBFilterMatch(PCUSBFILTER pDevice, uintptr_t *puId);
NEMUUSBFILTER_CONTEXT NemuUSBFilterMatchEx(PCUSBFILTER pDevice, uintptr_t *puId, bool fRemoveFltIfOneShot, bool *pfFilter, bool *pfIsOneShot);
NEMUUSBFILTER_CONTEXT NemuUSBFilterGetOwner(uintptr_t uId);

RT_C_DECLS_END

#endif
