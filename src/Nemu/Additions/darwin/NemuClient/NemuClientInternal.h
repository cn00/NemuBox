/** $Id: NemuClientInternal.h $ */
/** @file
 * NemuClient - common definitions, Darwin.
 */

/*
 * Copyright (C) 2007-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuClientInternal_h
#define ___NemuClientInternal_h

#include <Nemu/NemuGuestLib.h>
#include <Carbon/Carbon.h>

/* Service description */
typedef struct
{
    /** The service name. */
    const char *pszName;

    /**
     * Start service.
     * @returns Nemu status code.
     */
    DECLCALLBACKMEMBER(int, pfnStart)(void);

    /**
     * Stop service.
     * @returns Nemu status code.
     */
    DECLCALLBACKMEMBER(int, pfnStop)(void);

} NEMUCLIENTSERVICE;


/*
 * Services
 */

RT_C_DECLS_BEGIN

extern NEMUCLIENTSERVICE g_ClipboardService;

RT_C_DECLS_END


/*
 * Functions
 */

/**
 * Displays a verbose message.
 *
 * @param   iLevel      Minimum log level required to display this message.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
extern void NemuClientVerbose(int iLevel, const char *pszFormat, ...);

/**
 * Walk through pasteboard items and report currently available item types.
 *
 * @param   pPasteboard       Reference to guest Pasteboard.
 # @returns Available formats bit field.
 */
extern uint32_t vbclClipboardGetAvailableFormats(PasteboardRef pPasteboard);

/**
 * Read host's clipboard buffer and put its content to guest clipboard.
 *
 * @param   u32ClientId    Host connection.
 * @param   pPasteboard    Guest PasteBoard reference.
 * @param   fFormats       List of data formats (bit field) received from host.
 *
 * @returns IPRT status code.
 */
extern int vbclClipboardForwardToGuest(uint32_t u32ClientId, PasteboardRef pPasteboard, uint32_t fFormats);

/**
 * Read guest's clipboard buffer and forward its content to host.
 *
 * @param   u32ClientId    Host clipboard connection.
 * @param   pPasteboard    Guest PasteBoard reference.
 * @param   fFormats       List of data formats (bit field) received from host.
 *
 * @returns IPRT status code.
 */
extern int vbclClipboardForwardToHost(uint32_t u32ClientId, PasteboardRef pPasteboard, uint32_t fFormats);

#endif /* ___NemuClientInternal_h */
