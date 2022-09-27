/* $Id: NemuUhgsmiKmt.h $ */

/** @file
 * NemuVideo Display D3D User mode dll
 */

/*
 * Copyright (C) 2011-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuUhgsmiKmt_h__
#define ___NemuUhgsmiKmt_h__

#include "NemuDispD3DCmn.h"


typedef struct NEMUUHGSMI_PRIVATE_KMT
{
    NEMUUHGSMI_PRIVATE_BASE BasePrivate;
    NEMUDISPKMT_CALLBACKS Callbacks;
    NEMUDISPKMT_ADAPTER Adapter;
    NEMUDISPKMT_DEVICE Device;
    NEMUDISPKMT_CONTEXT Context;
} NEMUUHGSMI_PRIVATE_KMT, *PNEMUUHGSMI_PRIVATE_KMT;

#define NEMUUHGSMIKMT_GET_PRIVATE(_p, _t) ((_t*)(((uint8_t*)_p) - RT_OFFSETOF(_t, BasePrivate.Base)))
#define NEMUUHGSMIKMT_GET(_p) NEMUUHGSMIKMT_GET_PRIVATE(_p, NEMUUHGSMI_PRIVATE_KMT)

HRESULT nemuUhgsmiKmtDestroy(PNEMUUHGSMI_PRIVATE_KMT pHgsmi);

HRESULT nemuUhgsmiKmtCreate(PNEMUUHGSMI_PRIVATE_KMT pHgsmi, BOOL bD3D);

#endif /* #ifndef ___NemuUhgsmiKmt_h__ */
