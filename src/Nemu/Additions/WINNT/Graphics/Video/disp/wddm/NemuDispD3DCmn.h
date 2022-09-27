/* $Id: NemuDispD3DCmn.h $ */
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

#ifndef ___NemuDispD3DCmn_h___
#define ___NemuDispD3DCmn_h___

#include "NemuDispD3DBase.h"

#include <iprt/initterm.h>
#include <iprt/log.h>
#include <iprt/mem.h>

#include <Nemu/Log.h>

#include <Nemu/NemuGuestLib.h>

#include "NemuDispDbg.h"
#include "NemuDispD3DIf.h"
#include "../../common/wddm/NemuMPIf.h"
#include "NemuDispCm.h"
#include "NemuDispMpInternal.h"
#include "NemuDispKmt.h"
#ifdef NEMU_WITH_CRHGSMI
#include "NemuUhgsmiBase.h"
#include "NemuUhgsmiDisp.h"
#include "NemuUhgsmiKmt.h"
#endif
#include "NemuDispD3D.h"
#ifndef IN_NEMUCRHGSMI
#include "NemuD3DIf.h"
#endif
#ifdef NEMU_WITH_CROGL
#include <cr_protocol.h>
#endif

# ifdef NEMUWDDMDISP
#  define NEMUWDDMDISP_DECL(_type) DECLEXPORT(_type)
# else
#  define NEMUWDDMDISP_DECL(_type) DECLIMPORT(_type)
# endif

#endif /* #ifndef ___NemuDispD3DCmn_h___ */
