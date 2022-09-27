/* $Id: server.h $ */
/** @file
 *
 * Common header for XPCOM server and its module counterpart
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

#ifndef ____H_LINUX_SERVER
#define ____H_LINUX_SERVER

#include <Nemu/com/com.h>

#include <Nemu/version.h>

/**
 * IPC name used to resolve the client ID of the server.
 */
#define NEMUSVC_IPC_NAME "NemuSVC-" NEMU_VERSION_STRING


/**
 * Tag for the file descriptor passing for the daemonizing control.
 */
#define NEMUSVC_STARTUP_PIPE_NAME "nemusvc:startup-pipe"

#endif /* ____H_LINUX_SERVER */
