/* $Id: NemuUhgsmiDisp.h $ */

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

#ifndef ___NemuUhgsmiDisp_h__
#define ___NemuUhgsmiDisp_h__

#include "NemuUhgsmiBase.h"
#include "NemuDispD3DCmn.h"

typedef struct NEMUUHGSMI_PRIVATE_D3D
{
    NEMUUHGSMI_PRIVATE_BASE BasePrivate;
    struct NEMUWDDMDISP_DEVICE *pDevice;
} NEMUUHGSMI_PRIVATE_D3D, *PNEMUUHGSMI_PRIVATE_D3D;

void nemuUhgsmiD3DInit(PNEMUUHGSMI_PRIVATE_D3D pHgsmi, struct NEMUWDDMDISP_DEVICE *pDevice);

void nemuUhgsmiD3DEscInit(PNEMUUHGSMI_PRIVATE_D3D pHgsmi, struct NEMUWDDMDISP_DEVICE *pDevice);

#endif /* #ifndef ___NemuUhgsmiDisp_h__ */
