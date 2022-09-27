/* $Id:  $ */
/** @file
 * NemuSeamless - Seamless windows
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __NEMUSERVICESEAMLESS__H
#define __NEMUSERVICESEAMLESS__H

/* The seamless windows service prototypes */
int                NemuSeamlessInit     (const NEMUSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread);
unsigned __stdcall NemuSeamlessThread   (void *pInstance);
void               NemuSeamlessDestroy  (const NEMUSERVICEENV *pEnv, void *pInstance);


void NemuSeamlessEnable();
void NemuSeamlessDisable();
void NemuSeamlessCheckWindows(bool fForce);

void NemuSeamlessSetSupported(BOOL fSupported);

#endif /* __NEMUSERVICESEAMLESS__H */
