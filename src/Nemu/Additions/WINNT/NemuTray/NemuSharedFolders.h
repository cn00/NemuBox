/* $Id: NemuSharedFolders.h $ */
/** @file
 * NemuSharedFolders - Handling for shared folders
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

#ifndef ___NemuSharedFolders_h
#define ___NemuSharedFolders_h

int NemuSharedFoldersAutoUnmount(void);
int NemuSharedFoldersAutoMount(void);

#endif /* !___NemuSharedFolders_h */
