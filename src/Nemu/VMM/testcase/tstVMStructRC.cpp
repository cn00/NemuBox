/* $Id: tstVMStructRC.cpp $ */
/** @file
 * tstVMMStructRC - Generate structure member and size checks from the
 *                  RC perspective.
 *
 * This is built using the NEMURC template but linked into a host
 * ring-3 executable, rather hacky.
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


/*
 * Sanity checks.
 */
#ifndef IN_RC
# error Incorrect template!
#endif
#if defined(IN_RING3) || defined(IN_RING0)
# error Incorrect template!
#endif

#include <Nemu/types.h>
#include <iprt/assert.h>
AssertCompileSize(uint8_t,  1);
AssertCompileSize(uint16_t, 2);
AssertCompileSize(uint32_t, 4);
AssertCompileSize(uint64_t, 8);
AssertCompileSize(RTRCPTR,  4);
#ifdef NEMU_WITH_64_BITS_GUESTS
AssertCompileSize(RTGCPTR,  8);
#else
AssertCompileSize(RTGCPTR,  4);
#endif
AssertCompileSize(RTGCPHYS, 8);
AssertCompileSize(RTHCPHYS, 8);


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define IN_TSTVMSTRUCTGC 1
#include <Nemu/vmm/cfgm.h>
#include <Nemu/vmm/cpum.h>
#include <Nemu/vmm/mm.h>
#include <Nemu/vmm/pgm.h>
#include <Nemu/vmm/selm.h>
#include <Nemu/vmm/trpm.h>
#include <Nemu/vmm/vmm.h>
#include <Nemu/vmm/stam.h>
#include "PDMInternal.h"
#include <Nemu/vmm/pdm.h>
#include "CFGMInternal.h"
#include "CPUMInternal.h"
#include "MMInternal.h"
#include "PGMInternal.h"
#include "SELMInternal.h"
#include "TRPMInternal.h"
#include "TMInternal.h"
#include "IOMInternal.h"
#include "REMInternal.h"
#include "HMInternal.h"
#include "PATMInternal.h"
#include "VMMInternal.h"
#include "DBGFInternal.h"
#include "GIMInternal.h"
#include "STAMInternal.h"
#include "CSAMInternal.h"
#include "EMInternal.h"
#include "IEMInternal.h"
#include "REMInternal.h"
#include <Nemu/vmm/vm.h>
#include <Nemu/vmm/hm_vmx.h>
#include <Nemu/param.h>
#include <iprt/x86.h>
#include <iprt/assert.h>

/* we don't use iprt here because we're pretending to be in GC! */
#include <stdio.h>


int main()
{
#define GEN_CHECK_SIZE(s)       printf("    CHECK_SIZE(%s, %u);\n",    #s, (unsigned)sizeof(s))
#define GEN_CHECK_OFF(s, m)     printf("    CHECK_OFF(%s, %u, %s);\n", #s, (unsigned)RT_OFFSETOF(s, m), #m)
#define GEN_CHECK_OFF_DOT(s, m) printf("    CHECK_OFF(%s, %u, %s);\n", #s, (unsigned)RT_OFFSETOF(s, m), #m)
#include "tstVMStruct.h"
    return (0);
}

