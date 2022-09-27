/* $Id:  $ */
/** @file
 * Nemu Qt GUI - NemuFrameBuffer Overlay classes declarations.
 */

/*
 * Copyright (C) 2009-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef __NemuFBOverlayCommon_h__
#define __NemuFBOverlayCommon_h__

#if 0 //defined(DEBUG_misha)
DECLINLINE(VOID) nemuDbgPrintF(LPCSTR szString, ...)
{
    char szBuffer[4096] = {0};
    va_list pArgList;
    va_start(pArgList, szString);
    _vsnprintf(szBuffer, sizeof(szBuffer) / sizeof(szBuffer[0]), szString, pArgList);
    va_end(pArgList);

    OutputDebugStringA(szBuffer);
}

# include "iprt/stream.h"
# define NEMUQGLLOG(_m) RTPrintf _m
# define NEMUQGLLOGREL(_m) do { RTPrintf _m ; LogRel( _m ); } while(0)
# define NEMUQGLDBGPRINT(_m) nemuDbgPrintF _m
#else
# define NEMUQGLLOG(_m)    do {}while(0)
# define NEMUQGLLOGREL(_m) LogRel( _m )
# define NEMUQGLDBGPRINT(_m) do {}while(0)
#endif
#define NEMUQGLLOG_ENTER(_m) do {}while(0)
//do{NEMUQGLLOG(("==>[%s]:", __FUNCTION__)); NEMUQGLLOG(_m);}while(0)
#define NEMUQGLLOG_EXIT(_m) do {}while(0)
//do{NEMUQGLLOG(("<==[%s]:", __FUNCTION__)); NEMUQGLLOG(_m);}while(0)
#ifdef DEBUG
 #define NEMUQGL_ASSERTNOERR() \
    do { GLenum err = glGetError(); \
        if(err != GL_NO_ERROR) NEMUQGLLOG(("gl error occurred (0x%x)\n", err)); \
        Assert(err == GL_NO_ERROR); \
    }while(0)

 #define NEMUQGL_CHECKERR(_op) \
    do { \
        glGetError(); \
        _op \
        NEMUQGL_ASSERTNOERR(); \
    }while(0)
#else
 #define NEMUQGL_ASSERTNOERR() \
    do {}while(0)

 #define NEMUQGL_CHECKERR(_op) \
    do { \
        _op \
    }while(0)
#endif

#ifdef DEBUG
#include <iprt/time.h>

#define NEMUGETTIME() RTTimeNanoTS()

#define NEMUPRINTDIF(_nano, _m) do{\
        uint64_t cur = NEMUGETTIME(); NOREF(cur); \
        NEMUQGLLOG(_m); \
        NEMUQGLLOG(("(%Lu)\n", cur - (_nano))); \
    }while(0)

class NemuVHWADbgTimeCounter
{
public:
    NemuVHWADbgTimeCounter(const char* msg) {mTime = NEMUGETTIME(); mMsg=msg;}
    ~NemuVHWADbgTimeCounter() {NEMUPRINTDIF(mTime, (mMsg));}
private:
    uint64_t mTime;
    const char* mMsg;
};

#define NEMUQGLLOG_METHODTIME(_m) NemuVHWADbgTimeCounter _dbgTimeCounter(_m)

#define NEMUQG_CHECKCONTEXT() \
        { \
            const GLubyte * str; \
            NEMUQGL_CHECKERR(   \
                    str = glGetString(GL_VERSION); \
            ); \
            Assert(str); \
            if(str) \
            { \
                Assert(str[0]); \
            } \
        }
#else
#define NEMUQGLLOG_METHODTIME(_m)
#define NEMUQG_CHECKCONTEXT() do{}while(0)
#endif

#define NEMUQGLLOG_QRECT(_p, _pr, _s) do{\
    NEMUQGLLOG((_p " x(%d), y(%d), w(%d), h(%d)" _s, (_pr)->x(), (_pr)->y(), (_pr)->width(), (_pr)->height()));\
    }while(0)

#define NEMUQGLLOG_CKEY(_p, _pck, _s) do{\
    NEMUQGLLOG((_p " l(0x%x), u(0x%x)" _s, (_pck)->lower(), (_pck)->upper()));\
    }while(0)

#endif /* #ifndef __NemuFBOverlayCommon_h__ */

