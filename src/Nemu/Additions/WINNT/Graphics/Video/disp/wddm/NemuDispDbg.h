/* $Id: NemuDispDbg.h $ */

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

#ifndef ___NemuDispDbg_h__
#define ___NemuDispDbg_h__

#define NEMU_VIDEO_LOG_NAME "NemuD3D"
#define NEMU_VIDEO_LOG_LOGGER nemuVDbgInternalLogLogger
#define NEMU_VIDEO_LOGREL_LOGGER nemuVDbgInternalLogRelLogger
#define NEMU_VIDEO_LOGFLOW_LOGGER nemuVDbgInternalLogFlowLogger
#define NEMU_VIDEO_LOG_FN_FMT "%s"

#include "../../common/NemuVideoLog.h"

#ifdef DEBUG
/* debugging configuration flags */

/* generic debugging facilities & extra data checks */
# define NEMUWDDMDISP_DEBUG
# if defined(DEBUG_misha) || defined(DEBUG_leo)
/* for some reason when debugging with VirtualKD, user-mode DbgPrint's are discarded
 * the workaround so far is to pass the log info to the kernel driver and DbgPrint'ed from there,
 * which is enabled by this define */
//#  define NEMUWDDMDISP_DEBUG_PRINTDRV
/* use OutputDebugString */
//#  define NEMUWDDMDISP_DEBUG_PRINT
/* adds vectored exception handler to be able to catch non-debug UM exceptions in kernel debugger */
//#  define NEMUWDDMDISP_DEBUG_VEHANDLER
/* disable shared resource creation with wine */
//#  define NEMUWDDMDISP_DEBUG_NOSHARED

//#  define NEMUWDDMDISP_DEBUG_PRINT_SHARED_CREATE

//#  define NEMUWDDMDISP_DEBUG_TIMER
# endif

# ifndef IN_NEMUCRHGSMI
/* debug config vars */
extern DWORD g_NemuVDbgFDumpSetTexture;
extern DWORD g_NemuVDbgFDumpDrawPrim;
extern DWORD g_NemuVDbgFDumpTexBlt;
extern DWORD g_NemuVDbgFDumpBlt;
extern DWORD g_NemuVDbgFDumpRtSynch;
extern DWORD g_NemuVDbgFDumpFlush;
extern DWORD g_NemuVDbgFDumpShared;
extern DWORD g_NemuVDbgFDumpLock;
extern DWORD g_NemuVDbgFDumpUnlock;
extern DWORD g_NemuVDbgFDumpPresentEnter;
extern DWORD g_NemuVDbgFDumpPresentLeave;
extern DWORD g_NemuVDbgFDumpScSync;

extern DWORD g_NemuVDbgFBreakShared;
extern DWORD g_NemuVDbgFBreakDdi;

extern DWORD g_NemuVDbgFCheckSysMemSync;
extern DWORD g_NemuVDbgFCheckBlt;
extern DWORD g_NemuVDbgFCheckTexBlt;
extern DWORD g_NemuVDbgFCheckScSync;

extern DWORD g_NemuVDbgFSkipCheckTexBltDwmWndUpdate;

extern DWORD g_NemuVDbgCfgMaxDirectRts;
extern DWORD g_NemuVDbgCfgForceDummyDevCreate;

extern struct NEMUWDDMDISP_DEVICE *g_NemuVDbgInternalDevice;
extern struct NEMUWDDMDISP_RESOURCE *g_NemuVDbgInternalRc;

extern DWORD g_NemuVDbgCfgCreateSwapchainOnDdiOnce;

# endif /* #ifndef IN_NEMUCRHGSMI */
#endif

#if defined(NEMUWDDMDISP_DEBUG) || defined(NEMU_WDDMDISP_WITH_PROFILE)
/* log enable flags */
extern DWORD g_NemuVDbgFLogRel;
extern DWORD g_NemuVDbgFLog;
extern DWORD g_NemuVDbgFLogFlow;
#endif

#ifdef NEMUWDDMDISP_DEBUG_VEHANDLER
void nemuVDbgVEHandlerRegister();
void nemuVDbgVEHandlerUnregister();
#endif

#if defined(LOG_TO_BACKDOOR_DRV) || defined(NEMUWDDMDISP_DEBUG_PRINTDRV)
# define DbgPrintDrv(_m) do { nemuDispLogDrvF _m; } while (0)
# define DbgPrintDrvRel(_m) do { nemuDispLogDrvF _m; } while (0)
# define DbgPrintDrvFlow(_m) do { nemuDispLogDrvF _m; } while (0)
#else
# define DbgPrintDrv(_m) do { } while (0)
# define DbgPrintDrvRel(_m) do { } while (0)
# define DbgPrintDrvFlow(_m) do { } while (0)
#endif

#ifdef NEMUWDDMDISP_DEBUG_PRINT
# define DbgPrintUsr(_m) do { nemuDispLogDbgPrintF _m; } while (0)
# define DbgPrintUsrRel(_m) do { nemuDispLogDbgPrintF _m; } while (0)
# define DbgPrintUsrFlow(_m) do { nemuDispLogDbgPrintF _m; } while (0)
#else
# define DbgPrintUsr(_m) do { } while (0)
# define DbgPrintUsrRel(_m) do { } while (0)
# define DbgPrintUsrFlow(_m) do { } while (0)
#endif

#if defined(NEMUWDDMDISP_DEBUG) || defined(NEMU_WDDMDISP_WITH_PROFILE)
#define nemuVDbgInternalLog(_p) if (g_NemuVDbgFLog) { _p }
#define nemuVDbgInternalLogFlow(_p) if (g_NemuVDbgFLogFlow) { _p }
#define nemuVDbgInternalLogRel(_p) if (g_NemuVDbgFLogRel) { _p }
#else
#define nemuVDbgInternalLog(_p) do {} while (0)
#define nemuVDbgInternalLogFlow(_p) do {} while (0)
#define nemuVDbgInternalLogRel(_p) do { _p } while (0)
#endif

/* @todo: remove these from the code and from here */
#define nemuVDbgPrint(_m) LOG_EXACT(_m)
#define nemuVDbgPrintF(_m) LOGF_EXACT(_m)
#define nemuVDbgPrintR(_m)  LOGREL_EXACT(_m)

#define nemuVDbgInternalLogLogger(_m) do { \
        nemuVDbgInternalLog( \
            Log(_m); \
            DbgPrintUsr(_m); \
            DbgPrintDrv(_m); \
        ); \
    } while (0)

#define nemuVDbgInternalLogFlowLogger(_m)  do { \
        nemuVDbgInternalLogFlow( \
            LogFlow(_m); \
            DbgPrintUsrFlow(_m); \
            DbgPrintDrvFlow(_m); \
        ); \
    } while (0)

#define nemuVDbgInternalLogRelLogger(_m)  do { \
        nemuVDbgInternalLogRel( \
            LogRel(_m); \
            DbgPrintUsrRel(_m); \
            DbgPrintDrvRel(_m); \
        ); \
    } while (0)

#if defined(NEMUWDDMDISP_DEBUG) || defined(NEMU_WDDMDISP_WITH_PROFILE)
extern DWORD g_NemuVDbgPid;
extern LONG g_NemuVDbgFIsDwm;
#define NEMUVDBG_CHECK_EXE(_pszName) (nemuVDbgDoCheckExe(_pszName))
#define NEMUVDBG_IS_DWM() (!!(g_NemuVDbgFIsDwm >=0 ? g_NemuVDbgFIsDwm : (g_NemuVDbgFIsDwm = NEMUVDBG_CHECK_EXE("dwm.exe"))))
BOOL nemuVDbgDoCheckExe(const char * pszName);
#endif
#if defined(NEMUWDDMDISP_DEBUG) || defined(LOG_TO_BACKDOOR_DRV)

#define NEMUVDBG_STRCASE(_t) \
        case _t: return #_t;
#define NEMUVDBG_STRCASE_UNKNOWN() \
        default: Assert(0); return "Unknown";

static DECLINLINE(const char*) nemuDispLogD3DRcType(D3DRESOURCETYPE enmType)
{
    switch (enmType)
    {
        NEMUVDBG_STRCASE(D3DRTYPE_SURFACE);
        NEMUVDBG_STRCASE(D3DRTYPE_VOLUME);
        NEMUVDBG_STRCASE(D3DRTYPE_TEXTURE);
        NEMUVDBG_STRCASE(D3DRTYPE_VOLUMETEXTURE);
        NEMUVDBG_STRCASE(D3DRTYPE_CUBETEXTURE);
        NEMUVDBG_STRCASE(D3DRTYPE_VERTEXBUFFER);
        NEMUVDBG_STRCASE(D3DRTYPE_INDEXBUFFER);
        NEMUVDBG_STRCASE_UNKNOWN();
    }
}

#include "NemuDispMpLogger.h"

NEMUDISPMPLOGGER_DECL(void) NemuDispMpLoggerDumpD3DCAPS9(struct _D3DCAPS9 *pCaps);

void nemuDispLogDrvF(char * szString, ...);

# define nemuDispDumpD3DCAPS9(_pCaps) do { NemuDispMpLoggerDumpD3DCAPS9(_pCaps); } while (0)
#else
# define nemuDispDumpD3DCAPS9(_pCaps) do { } while (0)
#endif

#ifdef NEMUWDDMDISP_DEBUG

void nemuDispLogDbgPrintF(char * szString, ...);

# ifndef IN_NEMUCRHGSMI
typedef struct NEMUWDDMDISP_ALLOCATION *PNEMUWDDMDISP_ALLOCATION;
typedef struct NEMUWDDMDISP_RESOURCE *PNEMUWDDMDISP_RESOURCE;

#define NEMUVDBG_DUMP_TYPEF_FLOW                   0x00000001
#define NEMUVDBG_DUMP_TYPEF_CONTENTS               0x00000002
#define NEMUVDBG_DUMP_TYPEF_DONT_BREAK_ON_CONTENTS 0x00000004
#define NEMUVDBG_DUMP_TYPEF_BREAK_ON_FLOW          0x00000008
#define NEMUVDBG_DUMP_TYPEF_SHARED_ONLY            0x00000010

#define NEMUVDBG_DUMP_FLAGS_IS_SETANY(_fFlags, _Value) (((_fFlags) & (_Value)) != 0)
#define NEMUVDBG_DUMP_FLAGS_IS_SET(_fFlags, _Value) (((_fFlags) & (_Value)) == (_Value))
#define NEMUVDBG_DUMP_FLAGS_IS_CLEARED(_fFlags, _Value) (((_fFlags) & (_Value)) == 0)
#define NEMUVDBG_DUMP_FLAGS_CLEAR(_fFlags, _Value) ((_fFlags) & (~(_Value)))
#define NEMUVDBG_DUMP_FLAGS_SET(_fFlags, _Value) ((_fFlags) | (_Value))

#define NEMUVDBG_DUMP_TYPE_ENABLED(_fFlags) (NEMUVDBG_DUMP_FLAGS_IS_SETANY(_fFlags, NEMUVDBG_DUMP_TYPEF_FLOW | NEMUVDBG_DUMP_TYPEF_CONTENTS))
#define NEMUVDBG_DUMP_TYPE_ENABLED_FOR_INFO(_pInfo, _fFlags) ( \
        NEMUVDBG_DUMP_TYPE_ENABLED(_fFlags) \
        && ( \
                NEMUVDBG_DUMP_FLAGS_IS_CLEARED(_fFlags, NEMUVDBG_DUMP_TYPEF_SHARED_ONLY) \
                || ((_pInfo)->pAlloc && (_pInfo)->pAlloc->pRc->aAllocations[0].hSharedHandle) \
            ))

#define NEMUVDBG_DUMP_TYPE_FLOW_ONLY(_fFlags) (NEMUVDBG_DUMP_FLAGS_IS_SET(_fFlags, NEMUVDBG_DUMP_TYPEF_FLOW) \
        && NEMUVDBG_DUMP_FLAGS_IS_CLEARED(_fFlags, NEMUVDBG_DUMP_TYPEF_CONTENTS))
#define NEMUVDBG_DUMP_TYPE_CONTENTS(_fFlags) (NEMUVDBG_DUMP_FLAGS_IS_SET(_fFlags, NEMUVDBG_DUMP_TYPEF_CONTENTS))
#define NEMUVDBG_DUMP_TYPE_GET_FLOW_ONLY(_fFlags) ( \
        NEMUVDBG_DUMP_FLAGS_SET( \
                NEMUVDBG_DUMP_FLAGS_CLEAR(_fFlags, NEMUVDBG_DUMP_TYPEF_CONTENTS), \
                NEMUVDBG_DUMP_TYPEF_FLOW) \
        )

VOID nemuVDbgDoDumpAllocRect(const char * pPrefix, PNEMUWDDMDISP_ALLOCATION pAlloc, RECT *pRect, const char* pSuffix, DWORD fFlags);
VOID nemuVDbgDoDumpRcRect(const char * pPrefix, PNEMUWDDMDISP_ALLOCATION pAlloc, IDirect3DResource9 *pD3DRc, RECT *pRect, const char * pSuffix, DWORD fFlags);
VOID nemuVDbgDoDumpLockUnlockSurfTex(const char * pPrefix, const NEMUWDDMDISP_ALLOCATION *pAlloc, const char * pSuffix, DWORD fFlags);
VOID nemuVDbgDoDumpRt(const char * pPrefix, struct NEMUWDDMDISP_DEVICE *pDevice, const char * pSuffix, DWORD fFlags);
VOID nemuVDbgDoDumpBb(const char * pPrefix, IDirect3DSwapChain9 *pSwapchainIf, const char * pSuffix, DWORD fFlags);
VOID nemuVDbgDoDumpFb(const char * pPrefix, IDirect3DSwapChain9 *pSwapchainIf, const char * pSuffix, DWORD fFlags);
VOID nemuVDbgDoDumpSamplers(const char * pPrefix, struct NEMUWDDMDISP_DEVICE *pDevice, const char * pSuffix, DWORD fFlags);

void nemuVDbgDoPrintRect(const char * pPrefix, const RECT *pRect, const char * pSuffix);
void nemuVDbgDoPrintAlloc(const char * pPrefix, const NEMUWDDMDISP_RESOURCE *pRc, uint32_t iAlloc, const char * pSuffix);

VOID nemuVDbgDoDumpLockSurfTex(const char * pPrefix, const D3DDDIARG_LOCK* pData, const char * pSuffix, DWORD fFlags);
VOID nemuVDbgDoDumpUnlockSurfTex(const char * pPrefix, const D3DDDIARG_UNLOCK* pData, const char * pSuffix, DWORD fFlags);

BOOL nemuVDbgDoCheckRectsMatch(const NEMUWDDMDISP_RESOURCE *pDstRc, uint32_t iDstAlloc,
                            const NEMUWDDMDISP_RESOURCE *pSrcRc, uint32_t iSrcAlloc,
                            const RECT *pDstRect,
                            const RECT *pSrcRect,
                            BOOL fBreakOnMismatch);

VOID nemuVDbgDoPrintLopLastCmd(const char* pszDesc);

HRESULT nemuVDbgTimerStart(HANDLE hTimerQueue, HANDLE *phTimer, DWORD msTimeout);
HRESULT nemuVDbgTimerStop(HANDLE hTimerQueue, HANDLE hTimer);

#define NEMUVDBG_IS_PID(_pid) ((_pid) == (g_NemuVDbgPid ? g_NemuVDbgPid : (g_NemuVDbgPid = GetCurrentProcessId())))
#define NEMUVDBG_IS_DUMP_ALLOWED_PID(_pid) (((int)(_pid)) > 0 ? NEMUVDBG_IS_PID(_pid) : !NEMUVDBG_IS_PID(-((int)(_pid))))

#define NEMUVDBG_ASSERT_IS_DWM(_bDwm) do { \
        Assert((!NEMUVDBG_IS_DWM()) == (!(_bDwm))); \
    } while (0)

#define NEMUVDBG_DUMP_FLAGS_FOR_TYPE(_type) g_NemuVDbgFDump##_type
#define NEMUVDBG_BREAK_FLAGS_FOR_TYPE(_type) g_NemuVDbgFBreak##_type
#define NEMUVDBG_CHECK_FLAGS_FOR_TYPE(_type) g_NemuVDbgFCheck##_type
#define NEMUVDBG_IS_DUMP_ALLOWED(_type) ( NEMUVDBG_DUMP_TYPE_ENABLED(NEMUVDBG_DUMP_FLAGS_FOR_TYPE(_type)) )

#define NEMUVDBG_IS_BREAK_ALLOWED(_type) ( !!NEMUVDBG_BREAK_FLAGS_FOR_TYPE(_type) )

#define NEMUVDBG_IS_CHECK_ALLOWED(_type) ( !!NEMUVDBG_CHECK_FLAGS_FOR_TYPE(_type) )

#define NEMUVDBG_IS_DUMP_SHARED_ALLOWED(_pRc) (\
        (_pRc)->RcDesc.fFlags.SharedResource \
        && NEMUVDBG_IS_DUMP_ALLOWED(Shared) \
        )

#define NEMUVDBG_IS_BREAK_SHARED_ALLOWED(_pRc) (\
        (_pRc)->RcDesc.fFlags.SharedResource \
        && NEMUVDBG_IS_BREAK_ALLOWED(Shared) \
        )

#define NEMUVDBG_BREAK_SHARED(_pRc) do { \
        if (NEMUVDBG_IS_BREAK_SHARED_ALLOWED(_pRc)) { \
            nemuVDbgPrint(("Break on shared access: Rc(0x%p), SharedHandle(0x%p)\n", (_pRc), (_pRc)->aAllocations[0].hSharedHandle)); \
            AssertFailed(); \
        } \
    } while (0)

#define NEMUVDBG_BREAK_DDI() do { \
        if (NEMUVDBG_IS_BREAK_ALLOWED(Ddi)) { \
            AssertFailed(); \
        } \
    } while (0)

#define NEMUVDBG_LOOP_LAST() do { nemuVDbgLoop = 0; } while (0)

#define NEMUVDBG_LOOP(_op) do { \
        DWORD nemuVDbgLoop = 1; \
        do { \
            _op; \
        } while (nemuVDbgLoop); \
    } while (0)

#define NEMUVDBG_CHECK_SMSYNC(_pRc) do { \
        if (NEMUVDBG_IS_CHECK_ALLOWED(SysMemSync)) { \
            nemuWddmDbgRcSynchMemCheck((_pRc)); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_RECTS_INIT(_d) DWORD nemuVDbgDumpRects = _d;
#define NEMUVDBG_DUMP_RECTS_FORCE() nemuVDbgDumpRects = 1;
#define NEMUVDBG_DUMP_RECTS_FORCED() (!!nemuVDbgDumpRects)

#define NEMUVDBG_CHECK_RECTS(_opRests, _opDump, _pszOpName, _pDstRc, _iDstAlloc, _pSrcRc, _iSrcAlloc, _pDstRect, _pSrcRect) do { \
        NEMUVDBG_LOOP(\
                NEMUVDBG_DUMP_RECTS_INIT(0); \
                _opRests; \
                if (nemuVDbgDoCheckRectsMatch(_pDstRc, _iDstAlloc, _pSrcRc, _iSrcAlloc, _pDstRect, _pSrcRect, FALSE)) { \
                    NEMUVDBG_LOOP_LAST(); \
                } \
                else \
                { \
                    NEMUVDBG_DUMP_RECTS_FORCE(); \
                    nemuVDbgPrint(("nemuVDbgDoCheckRectsMatch failed! The " _pszOpName " will be re-done so it can be debugged\n")); \
                    nemuVDbgDoPrintLopLastCmd("Don't redo the" _pszOpName); \
                    Assert(0); \
                } \
                _opDump; \
         ); \
    } while (0)

#define NEMUVDBG_DEV_CHECK_SHARED(_pDevice, _pIsShared) do { \
        *(_pIsShared) = FALSE; \
        for (UINT i = 0; i < (_pDevice)->cRTs; ++i) { \
            PNEMUWDDMDISP_ALLOCATION pRtVar = (_pDevice)->apRTs[i]; \
            if (pRtVar && pRtVar->pRc->RcDesc.fFlags.SharedResource) { *(_pIsShared) = TRUE; break; } \
        } \
        if (!*(_pIsShared)) { \
            for (UINT i = 0, iSampler = 0; iSampler < (_pDevice)->cSamplerTextures; ++i) { \
                Assert(i < RT_ELEMENTS((_pDevice)->aSamplerTextures)); \
                if (!(_pDevice)->aSamplerTextures[i]) continue; \
                ++iSampler; \
                if (!(_pDevice)->aSamplerTextures[i]->RcDesc.fFlags.SharedResource) continue; \
                *(_pIsShared) = TRUE; break; \
            } \
        } \
    } while (0)

#define NEMUVDBG_IS_DUMP_SHARED_ALLOWED_DEV(_pDevice, _pIsAllowed) do { \
        NEMUVDBG_DEV_CHECK_SHARED(_pDevice, _pIsAllowed); \
        if (*(_pIsAllowed)) \
        { \
            *(_pIsAllowed) = NEMUVDBG_IS_DUMP_ALLOWED(Shared); \
        } \
    } while (0)

#define NEMUVDBG_IS_BREAK_SHARED_ALLOWED_DEV(_pDevice, _pIsAllowed) do { \
        NEMUVDBG_DEV_CHECK_SHARED(_pDevice, _pIsAllowed); \
        if (*(_pIsAllowed)) \
        { \
            *(_pIsAllowed) = NEMUVDBG_IS_BREAK_ALLOWED(Shared); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_DRAWPRIM_ENTER(_pDevice) do { \
        BOOL fDumpShaded = FALSE; \
        NEMUVDBG_IS_DUMP_SHARED_ALLOWED_DEV(_pDevice, &fDumpShaded); \
        if (fDumpShaded \
                || NEMUVDBG_IS_DUMP_ALLOWED(DrawPrim)) \
        { \
            nemuVDbgDoDumpRt("==>"__FUNCTION__": Rt: ", (_pDevice), "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(DrawPrim)); \
            nemuVDbgDoDumpSamplers("==>"__FUNCTION__": Sl: ", (_pDevice), "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(DrawPrim)); \
        }\
    } while (0)

#define NEMUVDBG_DUMP_DRAWPRIM_LEAVE(_pDevice) do { \
        BOOL fDumpShaded = FALSE; \
        NEMUVDBG_IS_DUMP_SHARED_ALLOWED_DEV(_pDevice, &fDumpShaded); \
        if (fDumpShaded \
                || NEMUVDBG_IS_DUMP_ALLOWED(DrawPrim)) \
        { \
            nemuVDbgDoDumpRt("<=="__FUNCTION__": Rt: ", (_pDevice), "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(DrawPrim)); \
            nemuVDbgDoDumpSamplers("<=="__FUNCTION__": Sl: ", (_pDevice), "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(DrawPrim)); \
        }\
    } while (0)

#define NEMUVDBG_BREAK_SHARED_DEV(_pDevice)  do { \
        BOOL fBreakShaded = FALSE; \
        NEMUVDBG_IS_BREAK_SHARED_ALLOWED_DEV(_pDevice, &fBreakShaded); \
        if (fBreakShaded) { \
            nemuVDbgPrint((__FUNCTION__"== Break on shared access\n")); \
            AssertFailed(); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_SETTEXTURE(_pRc) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(SetTexture) \
                || NEMUVDBG_IS_DUMP_SHARED_ALLOWED(_pRc) \
                ) \
        { \
            nemuVDbgDoDumpRcRect("== "__FUNCTION__": ", &(_pRc)->aAllocations[0], NULL, NULL, "", \
                    NEMUVDBG_DUMP_FLAGS_FOR_TYPE(SetTexture) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(TexBlt) \
                || NEMUVDBG_IS_DUMP_SHARED_ALLOWED(_pSrcRc) \
                || NEMUVDBG_IS_DUMP_SHARED_ALLOWED(_pDstRc) \
                ) \
        { \
            RECT SrcRect = *(_pSrcRect); \
            RECT _DstRect; \
            nemuWddmRectMoved(&_DstRect, &SrcRect, (_pDstPoint)->x, (_pDstPoint)->y); \
            nemuVDbgDoDumpRcRect("==> "__FUNCTION__": Src: ", &(_pSrcRc)->aAllocations[0], NULL, &SrcRect, "", \
                    NEMUVDBG_DUMP_FLAGS_FOR_TYPE(TexBlt) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
            nemuVDbgDoDumpRcRect("==> "__FUNCTION__": Dst: ", &(_pDstRc)->aAllocations[0], NULL, &_DstRect, "", \
                    NEMUVDBG_DUMP_FLAGS_FOR_TYPE(TexBlt) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { \
        if (NEMUVDBG_DUMP_RECTS_FORCED() \
                || NEMUVDBG_IS_DUMP_ALLOWED(TexBlt) \
                || NEMUVDBG_IS_DUMP_SHARED_ALLOWED(_pSrcRc) \
                || NEMUVDBG_IS_DUMP_SHARED_ALLOWED(_pDstRc) \
                ) \
        { \
            RECT SrcRect = *(_pSrcRect); \
            RECT _DstRect; \
            nemuWddmRectMoved(&_DstRect, &SrcRect, (_pDstPoint)->x, (_pDstPoint)->y); \
            nemuVDbgDoDumpRcRect("<== "__FUNCTION__": Src: ", &(_pSrcRc)->aAllocations[0], NULL, &SrcRect, "", \
                    NEMUVDBG_DUMP_FLAGS_FOR_TYPE(TexBlt) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
            nemuVDbgDoDumpRcRect("<== "__FUNCTION__": Dst: ", &(_pDstRc)->aAllocations[0], NULL, &_DstRect, "", \
                    NEMUVDBG_DUMP_FLAGS_FOR_TYPE(TexBlt) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared)); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_STRETCH_RECT(_type, _str, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(_type) \
                || NEMUVDBG_IS_DUMP_SHARED_ALLOWED((_pSrcAlloc)->pRc) \
                || NEMUVDBG_IS_DUMP_SHARED_ALLOWED((_pDstAlloc)->pRc) \
                ) \
        { \
            DWORD fFlags = NEMUVDBG_DUMP_FLAGS_FOR_TYPE(_type) | NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Shared); \
            if (NEMUVDBG_DUMP_TYPE_CONTENTS(fFlags) && \
                    ((_pSrcSurf) == (_pDstSurf) \
                    && ( ((_pSrcRect) && (_pDstRect) && !memcmp((_pSrcRect), (_pDstRect), sizeof (_pDstRect))) \
                            || ((_pSrcRect) == (_pDstRect)) \
                            )) ) \
            { \
                nemuVDbgPrint((_str #_type ": skipping content dump of the same rect for one surfcace\n")); \
                fFlags = NEMUVDBG_DUMP_TYPE_GET_FLOW_ONLY(fFlags); \
            } \
            RECT Rect, *pRect; \
            if (_pSrcRect) \
            { \
                Rect = *((RECT*)(_pSrcRect)); \
                pRect = &Rect; \
            } \
            else \
                pRect = NULL; \
            nemuVDbgDoDumpRcRect(_str __FUNCTION__" Src: ", (_pSrcAlloc), (_pSrcSurf), pRect, "", fFlags); \
            if (_pDstRect) \
            { \
                Rect = *((RECT*)(_pDstRect)); \
                pRect = &Rect; \
            } \
            else \
                pRect = NULL; \
            nemuVDbgDoDumpRcRect(_str __FUNCTION__" Dst: ", (_pDstAlloc), (_pDstSurf), pRect, "", fFlags); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_BLT_ENTER(_pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
    NEMUVDBG_DUMP_STRETCH_RECT(Blt, "==>", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define NEMUVDBG_DUMP_BLT_LEAVE(_pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
        NEMUVDBG_DUMP_STRETCH_RECT(Blt, "<==", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define NEMUVDBG_DUMP_SWAPCHAIN_SYNC_ENTER(_pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
        NEMUVDBG_DUMP_STRETCH_RECT(ScSync, "==>", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define NEMUVDBG_DUMP_SWAPCHAIN_SYNC_LEAVE(_pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
        NEMUVDBG_DUMP_STRETCH_RECT(ScSync, "<==", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define NEMUVDBG_IS_SKIP_DWM_WND_UPDATE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) ( \
            g_NemuVDbgFSkipCheckTexBltDwmWndUpdate \
            && ( \
                NEMUVDBG_IS_DWM() \
                && (_pSrcRc)->RcDesc.enmPool == D3DDDIPOOL_SYSTEMMEM \
                && (_pSrcRc)->RcDesc.enmFormat == D3DDDIFMT_A8R8G8B8 \
                && (_pSrcRc)->cAllocations == 1 \
                && (_pDstRc)->RcDesc.enmPool == D3DDDIPOOL_VIDEOMEMORY \
                && (_pDstRc)->RcDesc.enmFormat == D3DDDIFMT_A8R8G8B8 \
                && (_pDstRc)->RcDesc.fFlags.RenderTarget \
                && (_pDstRc)->RcDesc.fFlags.NotLockable \
                && (_pDstRc)->cAllocations == 1 \
                && (_pSrcRc)->aAllocations[0].SurfDesc.width == (_pDstRc)->aAllocations[0].SurfDesc.width \
                && (_pSrcRc)->aAllocations[0].SurfDesc.height == (_pDstRc)->aAllocations[0].SurfDesc.height \
            ) \
        )

#define NEMUVDBG_CHECK_TEXBLT(_opTexBlt, _pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { \
        if (NEMUVDBG_IS_CHECK_ALLOWED(TexBlt)) { \
            if (NEMUVDBG_IS_SKIP_DWM_WND_UPDATE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint)) \
            { \
                nemuVDbgPrint(("TEXBLT: skipping check for dwm wnd update\n")); \
            } \
            else \
            { \
                RECT DstRect; \
                DstRect.left = (_pDstPoint)->x; \
                DstRect.right = (_pDstPoint)->x + (_pSrcRect)->right - (_pSrcRect)->left; \
                DstRect.top = (_pDstPoint)->y; \
                DstRect.bottom = (_pDstPoint)->y + (_pSrcRect)->bottom - (_pSrcRect)->top; \
                NEMUVDBG_CHECK_RECTS(\
                        NEMUVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint); \
                        _opTexBlt ,\
                        NEMUVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint), \
                        "TexBlt", \
                        _pDstRc, 0, _pSrcRc, 0, &DstRect, _pSrcRect); \
                break; \
            } \
        } \
        NEMUVDBG_DUMP_RECTS_INIT(0); \
        NEMUVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint); \
        _opTexBlt;\
        NEMUVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint); \
    } while (0)

#define NEMUVDBG_CHECK_STRETCH_RECT(_type, _op, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) do { \
        if (NEMUVDBG_IS_CHECK_ALLOWED(_type)) { \
            NEMUVDBG_CHECK_RECTS(\
                    NEMUVDBG_DUMP_STRETCH_RECT(_type, "==>", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect); \
                    _op ,\
                    NEMUVDBG_DUMP_STRETCH_RECT(_type, "<==", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect), \
                    #_type , \
                    _pDstAlloc->pRc, _pDstAlloc->iAlloc, _pSrcAlloc->pRc, _pSrcAlloc->iAlloc, _pDstRect, _pSrcRect); \
        } \
        else \
        { \
            NEMUVDBG_DUMP_RECTS_INIT(0); \
            NEMUVDBG_DUMP_STRETCH_RECT(_type, "==>", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect); \
            _op;\
            NEMUVDBG_DUMP_STRETCH_RECT(_type, "<==", _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect); \
        } \
    } while (0)

#define NEMUVDBG_CHECK_BLT(_opBlt, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
        NEMUVDBG_CHECK_STRETCH_RECT(Blt, _opBlt, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define NEMUVDBG_CHECK_SWAPCHAIN_SYNC(_op, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) \
        NEMUVDBG_CHECK_STRETCH_RECT(ScSync, _op, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect)

#define NEMUVDBG_DUMP_SYNC_RT(_pBbSurf) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(RtSynch)) \
        { \
            nemuVDbgDoDumpRcRect("== "__FUNCTION__" Bb:\n", NULL, (_pBbSurf), NULL, "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(RtSynch)); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_PRESENT_ENTER(_pDevice, _pSwapchain) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(PresentEnter)) { \
            if (!(_pSwapchain)->fFlags.bRtReportingPresent) { \
                nemuVDbgDoDumpBb("==>"__FUNCTION__" Bb:\n", (_pSwapchain)->pSwapChainIf, "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(PresentEnter)); \
            } \
            else  { \
                PNEMUWDDMDISP_ALLOCATION pCurBb = nemuWddmSwapchainGetBb((_pSwapchain))->pAlloc; \
                IDirect3DSurface9 *pSurf; \
                HRESULT hr = nemuWddmSwapchainSurfGet(_pDevice, _pSwapchain, pCurBb, &pSurf); \
                Assert(hr == S_OK); \
                nemuVDbgDoDumpRcRect("== "__FUNCTION__" Bb:\n", pCurBb, pSurf, NULL, "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(PresentEnter)); \
                pSurf->Release(); \
            } \
        } \
    } while (0)

#define NEMUVDBG_DUMP_PRESENT_LEAVE(_pDevice, _pSwapchain) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(PresentLeave)) { \
            if (!(_pSwapchain)->fFlags.bRtReportingPresent) { \
                nemuVDbgDoDumpFb("<=="__FUNCTION__" Fb:\n", (_pSwapchain)->pSwapChainIf, "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(PresentLeave)); \
            } \
            else  { \
                nemuVDbgPrint(("PRESENT_LEAVE: unsupported for Rt Reporting mode\n")); \
            } \
        } \
    } while (0)


#define NEMUVDBG_DUMP_FLUSH(_pDevice) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(Flush)) \
        { \
            nemuVDbgDoDumpRt("== "__FUNCTION__": Rt: ", (_pDevice), "", \
                    NEMUVDBG_DUMP_FLAGS_CLEAR(NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Flush), NEMUVDBG_DUMP_TYPEF_SHARED_ONLY)); \
        }\
    } while (0)

#define NEMUVDBG_DUMP_LOCK_ST(_pData) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(Lock) \
                || NEMUVDBG_IS_DUMP_ALLOWED(Unlock) \
                ) \
        { \
            nemuVDbgDoDumpLockSurfTex("== "__FUNCTION__": ", (_pData), "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Lock)); \
        } \
    } while (0)

#define NEMUVDBG_DUMP_UNLOCK_ST(_pData) do { \
        if (NEMUVDBG_IS_DUMP_ALLOWED(Unlock) \
                ) \
        { \
            nemuVDbgDoDumpUnlockSurfTex("== "__FUNCTION__": ", (_pData), "", NEMUVDBG_DUMP_FLAGS_FOR_TYPE(Unlock)); \
        } \
    } while (0)


#define NEMUVDBG_CREATE_CHECK_SWAPCHAIN() do { \
            if (g_NemuVDbgCfgCreateSwapchainOnDdiOnce && g_NemuVDbgInternalRc) { \
                PNEMUWDDMDISP_SWAPCHAIN pSwapchain; \
                HRESULT hr = nemuWddmSwapchainCreateIfForRc(g_NemuVDbgInternalDevice, g_NemuVDbgInternalRc, &pSwapchain); \
                Assert(hr == S_OK); \
                g_NemuVDbgInternalRc = NULL; \
                g_NemuVDbgCfgCreateSwapchainOnDdiOnce = 0; \
            } \
        } while (0)

# endif /* # ifndef IN_NEMUCRHGSMI */
#else
#define NEMUVDBG_DUMP_DRAWPRIM_ENTER(_pDevice) do { } while (0)
#define NEMUVDBG_DUMP_DRAWPRIM_LEAVE(_pDevice) do { } while (0)
#define NEMUVDBG_DUMP_SETTEXTURE(_pRc) do { } while (0)
#define NEMUVDBG_DUMP_TEXBLT_ENTER(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { } while (0)
#define NEMUVDBG_DUMP_TEXBLT_LEAVE(_pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { } while (0)
#define NEMUVDBG_DUMP_BLT_ENTER(_pSrcRc, _pSrcSurf, _pSrcRect, _pDstRc, _pDstSurf, _pDstRect) do { } while (0)
#define NEMUVDBG_DUMP_BLT_LEAVE(_pSrcRc, _pSrcSurf, _pSrcRect, _pDstRc, _pDstSurf, _pDstRect) do { } while (0)
#define NEMUVDBG_DUMP_SYNC_RT(_pBbSurf) do { } while (0)
#define NEMUVDBG_DUMP_FLUSH(_pDevice) do { } while (0)
#define NEMUVDBG_DUMP_LOCK_ST(_pData) do { } while (0)
#define NEMUVDBG_DUMP_UNLOCK_ST(_pData) do { } while (0)
#define NEMUVDBG_DUMP_PRESENT_ENTER(_pDevice, _pSwapchain) do { } while (0)
#define NEMUVDBG_DUMP_PRESENT_LEAVE(_pDevice, _pSwapchain) do { } while (0)
#define NEMUVDBG_BREAK_SHARED(_pRc) do { } while (0)
#define NEMUVDBG_BREAK_SHARED_DEV(_pDevice) do { } while (0)
#define NEMUVDBG_BREAK_DDI() do { } while (0)
#define NEMUVDBG_CHECK_SMSYNC(_pRc) do { } while (0)
#define NEMUVDBG_CHECK_BLT(_opBlt, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) do { _opBlt; } while (0)
#define NEMUVDBG_CHECK_TEXBLT(_opTexBlt, _pSrcRc, _pSrcRect, _pDstRc, _pDstPoint) do { _opTexBlt; } while (0)
#define NEMUVDBG_ASSERT_IS_DWM(_bDwm) do { } while (0)
#define NEMUVDBG_CHECK_SWAPCHAIN_SYNC(_op, _pSrcAlloc, _pSrcSurf, _pSrcRect, _pDstAlloc, _pDstSurf, _pDstRect) do { _op; } while (0)
#define NEMUVDBG_CREATE_CHECK_SWAPCHAIN() do { } while (0)
#endif


#endif /* #ifndef ___NemuDispDbg_h__ */
