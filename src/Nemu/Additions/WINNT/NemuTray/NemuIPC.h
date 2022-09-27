/* $Id: NemuIPC.h $ */
/** @file
 * NemuIPC - IPC thread, acts as a (purely) local IPC server.
 *           Multiple sessions are supported, whereas every session
 *           has its own thread for processing requests.
 */

/*
 * Copyright (C) 2006-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __NEMUTRAYIPCSERVER__H
#define __NEMUTRAYIPCSERVER__H

int                NemuIPCInit    (const NEMUSERVICEENV *pEnv, void **ppInstance, bool *pfStartThread);
unsigned __stdcall NemuIPCThread  (void *pInstance);
void               NemuIPCStop    (const NEMUSERVICEENV *pEnv, void *pInstance);
void               NemuIPCDestroy (const NEMUSERVICEENV *pEnv, void *pInstance);

#endif /* __NEMUTRAYIPCSERVER__H */
