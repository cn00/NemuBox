/** @file
 * OpenGL: Common header for host service and guest clients.
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
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___Nemu_HostService_NemuOpenGLSvc_h
#define ___Nemu_HostService_NemuOpenGLSvc_h

#include <Nemu/types.h>
#include <Nemu/NemuGuest.h>
#include <Nemu/hgcmsvc.h>
#include <Nemu/VMMDev.h>

/* OpenGL command buffer size */
#define NEMU_OGL_MAX_CMD_BUFFER                     (128*1024)
#define NEMU_OGL_CMD_ALIGN                          4
#define NEMU_OGL_CMD_ALIGN_MASK                     (NEMU_OGL_CMD_ALIGN-1)
#define NEMU_OGL_CMD_MAGIC                          0x1234ABCD

/* for debugging */
#define NEMU_OGL_CMD_STRICT

/* OpenGL command block */
typedef struct
{
#ifdef NEMU_OGL_CMD_STRICT
    uint32_t    Magic;
#endif
    uint32_t    enmOp;
    uint32_t    cbCmd;
    uint32_t    cParams;
    /* start of variable size parameter array */
} NEMU_OGL_CMD, *PNEMU_OGL_CMD;

typedef struct
{
#ifdef NEMU_OGL_CMD_STRICT
    uint32_t    Magic;
#endif
    uint32_t    cbParam;
    /* start of variable size parameter */
} NEMU_OGL_VAR_PARAM, *PNEMU_OGL_VAR_PARAM;

/** OpenGL Folders service functions. (guest)
 *  @{
 */

/** Query mappings changes. */
#define NEMUOGL_FN_GLGETSTRING          (1)
#define NEMUOGL_FN_GLFLUSH              (2)
#define NEMUOGL_FN_GLFLUSHPTR           (3)
#define NEMUOGL_FN_GLCHECKEXT           (4)

/** @} */

/** Function parameter structures.
 *  @{
 */

/**
 * NEMUOGL_FN_GLGETSTRING
 */

/** Parameters structure. */
typedef struct
{
    NemuGuestHGCMCallInfo   hdr;

    /** 32bit, in: name
     * GLenum name parameter
     */
    HGCMFunctionParameter   name;

    /** pointer, in/out
     * Buffer for requested string
     */
    HGCMFunctionParameter   pString;
} NemuOGLglGetString;

/** Number of parameters */
#define NEMUOGL_CPARMS_GLGETSTRING (2)



/**
 * NEMUOGL_FN_GLFLUSH
 */

/** Parameters structure. */
typedef struct
{
    NemuGuestHGCMCallInfo   hdr;

    /** pointer, in
     * Command buffer
     */
    HGCMFunctionParameter   pCmdBuffer;

    /** 32bit, out: cCommands
     * Number of commands in the buffer
     */
    HGCMFunctionParameter   cCommands;

    /** 64bit, out: retval
     * uint64_t return code of last command
     */
    HGCMFunctionParameter   retval;

    /** 32bit, out: lasterror
     * GLenum current last error
     */
    HGCMFunctionParameter   lasterror;

} NemuOGLglFlush;

/** Number of parameters */
#define NEMUOGL_CPARMS_GLFLUSH (4)

/**
 * NEMUOGL_FN_GLFLUSHPTR
 */

/** Parameters structure. */
typedef struct
{
    NemuGuestHGCMCallInfo   hdr;

    /** pointer, in
     * Command buffer
     */
    HGCMFunctionParameter   pCmdBuffer;

    /** 32bit, out: cCommands
     * Number of commands in the buffer
     */
    HGCMFunctionParameter   cCommands;

    /** pointer, in
     * Last command's final parameter memory block
     */
    HGCMFunctionParameter   pLastParam;

    /** 64bit, out: retval
     * uint64_t return code of last command
     */
    HGCMFunctionParameter   retval;

    /** 32bit, out: lasterror
     * GLenum current last error
     */
    HGCMFunctionParameter   lasterror;

} NemuOGLglFlushPtr;

/** Number of parameters */
#define NEMUOGL_CPARMS_GLFLUSHPTR (5)


/**
 * NEMUOGL_FN_GLCHECKEXT
 */

/** Parameters structure. */
typedef struct
{
    NemuGuestHGCMCallInfo   hdr;

    /** pointer, in
     * Extension function name
     */
    HGCMFunctionParameter   pszExtFnName;

} NemuOGLglCheckExt;

/** Number of parameters */
#define NEMUOGL_CPARMS_GLCHECKEXT (1)

/** @} */


#endif

