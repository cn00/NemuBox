/* $Id: NemuCompilerPlugIns.h $ */
/** @file
 * NemuCompilerPlugIns - Types, Prototypes and Macros common to the Nemu compiler plug-ins.
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

#ifndef ___NemuCompilerPlugIns_h__
#define ___NemuCompilerPlugIns_h__

#include <iprt/types.h>
#include <stdio.h>


/** @def dprintf
 * Macro for debug printing using fprintf.  Only active when DEBUG is defined.
 */
#ifdef DEBUG
# define dprintf(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
# define dprintf(...) do { } while (0)
#endif


/**
 * Checker state.
 */
typedef struct VFMTCHKSTATE
{
    long        iFmt;
    long        iArgs;
    const char *pszFmt;
    bool        fMaybeNull;
#if defined(__GNUC__) && !defined(NEMU_COMPILER_PLUG_IN_AGNOSTIC)
    gimple      hStmt;
    location_t  hFmtLoc;
#endif
} VFMTCHKSTATE;
/** Pointer to my checker state. */
typedef VFMTCHKSTATE *PVFMTCHKSTATE;

#define MYSTATE_FMT_FILE(a_pState)      VFmtChkGetFmtLocFile(a_pState)
#define MYSTATE_FMT_LINE(a_pState)      VFmtChkGetFmtLocLine(a_pState)
#define MYSTATE_FMT_COLUMN(a_pState)    VFmtChkGetFmtLocColumn(a_pState)

const char     *VFmtChkGetFmtLocFile(PVFMTCHKSTATE pState);

unsigned int    VFmtChkGetFmtLocLine(PVFMTCHKSTATE pState);

unsigned int    VFmtChkGetFmtLocColumn(PVFMTCHKSTATE pState);

/**
 * Implements checking format string replacement (%M).
 *
 * Caller will have checked all that can be checked.  This means that there is a
 * string argument present, or it won't make the call.
 *
 * @param   pState              The format string checking state.
 * @param   pszPctM             The position of the '%M'.
 * @param   iArg                The next argument number.
 */
void            VFmtChkHandleReplacementFormatString(PVFMTCHKSTATE pState, const char *pszPctM, unsigned iArg);

/**
 * Warning.
 *
 * @returns
 * @param   pState              .
 * @param   pszLoc              .
 * @param   pszFormat           .
 * @param   ...                 .
 */
void            VFmtChkWarnFmt(PVFMTCHKSTATE pState, const char *pszLoc, const char *pszFormat, ...);

/**
 * Error.
 *
 * @returns
 * @param   pState              .
 * @param   pszLoc              .
 * @param   pszFormat           .
 * @param   ...                 .
 */
void            VFmtChkErrFmt(PVFMTCHKSTATE pState, const char *pszLoc, const char *pszFormat, ...);

/**
 * Checks that @a iFmtArg isn't present or a valid final dummy argument.
 *
 * Will issue warning/error if there are more arguments at @a iFmtArg.
 *
 * @param   pState              The format string checking state.
 * @param   iArg                The index of the end of arguments, this is
 *                              relative to VFMTCHKSTATE::iArgs.
 */
void            VFmtChkVerifyEndOfArgs(PVFMTCHKSTATE pState, unsigned iArg);

bool            VFmtChkRequirePresentArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage);

bool            VFmtChkRequireIntArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage);

bool            VFmtChkRequireStringArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage);

bool            VFmtChkRequireVaListPtrArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage);

/* NemuCompilerPlugInsCommon.cpp */
void            MyCheckFormatCString(PVFMTCHKSTATE pState, const char *pszFmt);


#endif

