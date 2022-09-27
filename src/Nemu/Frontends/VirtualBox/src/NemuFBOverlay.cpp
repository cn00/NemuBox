/* $Id: NemuFBOverlay.cpp $ */
/** @file
 * Nemu Qt GUI - NemuFBOverlay implementation.
 */

/*
 * Copyright (C) 2009-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#if defined(NEMU_GUI_USE_QGL)

#ifdef NEMU_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !NEMU_WITH_PRECOMPILED_HEADERS */

# define LOG_GROUP LOG_GROUP_GUI

/* Qt includes: */
# include <QGLWidget>
# include <QFile>
# include <QTextStream>

/* GUI includes: */
# include "NemuFBOverlay.h"
# include "UIMessageCenter.h"
# include "UIPopupCenter.h"
# include "UIExtraDataManager.h"
# include "NemuGlobal.h"

/* COM includes: */
# include "CSession.h"
# include "CConsole.h"
# include "CMachine.h"
# include "CDisplay.h"

/* Other Nemu includes: */
# include <iprt/asm.h>
# include <iprt/semaphore.h>

# include <Nemu/NemuGL2D.h>

#ifdef Q_WS_MAC
# include "NemuUtils-darwin.h"
#endif /* Q_WS_MAC */

#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */

/* Other Nemu includes: */
#include <iprt/memcache.h>
#include <Nemu/err.h>

#ifdef NEMU_WITH_VIDEOHWACCEL
# include <Nemu/NemuVideo.h>
# include <Nemu/vmm/ssm.h>
#endif /* NEMU_WITH_VIDEOHWACCEL */

/* Other includes: */
#include <math.h>


#ifdef NEMUQGL_PROF_BASE
# ifdef NEMUQGL_DBG_SURF
#  define NEMUQGL_PROF_WIDTH 1400
#  define NEMUQGL_PROF_HEIGHT 1050
# else
# define NEMUQGL_PROF_WIDTH 1400
# define NEMUQGL_PROF_HEIGHT 1050
//#define NEMUQGL_PROF_WIDTH 720
//#define NEMUQGL_PROF_HEIGHT 480
# endif
#endif

#define NEMUQGL_STATE_NAMEBASE "QGLVHWAData"
#define NEMUQGL_STATE_VERSION_PIPESAVED    3
#define NEMUQGL_STATE_VERSION              3

#ifdef DEBUG
NemuVHWADbgTimer::NemuVHWADbgTimer(uint32_t cPeriods) :
        mPeriodSum(0LL),
        mPrevTime(0LL),
        mcFrames(0LL),
        mcPeriods(cPeriods),
        miPeriod(0)
{
    mpaPeriods = new uint64_t[cPeriods];
    memset(mpaPeriods, 0, cPeriods * sizeof(mpaPeriods[0]));
}

NemuVHWADbgTimer::~NemuVHWADbgTimer()
{
    delete[] mpaPeriods;
}

void NemuVHWADbgTimer::frame()
{
    uint64_t cur = NEMUGETTIME();
    if(mPrevTime)
    {
        uint64_t curPeriod = cur - mPrevTime;
        mPeriodSum += curPeriod - mpaPeriods[miPeriod];
        mpaPeriods[miPeriod] = curPeriod;
        ++miPeriod;
        miPeriod %= mcPeriods;
    }
    mPrevTime = cur;
    ++mcFrames;
}
#endif

//#define NEMUQGLOVERLAY_STATE_NAMEBASE "QGLOverlayVHWAData"
//#define NEMUQGLOVERLAY_STATE_VERSION 1

#ifdef DEBUG_misha
//# define NEMUQGL_STATE_DEBUG
#endif

#ifdef NEMUQGL_STATE_DEBUG
#define NEMUQGL_STATE_START_MAGIC        0x12345678
#define NEMUQGL_STATE_STOP_MAGIC         0x87654321

#define NEMUQGL_STATE_SURFSTART_MAGIC    0x9abcdef1
#define NEMUQGL_STATE_SURFSTOP_MAGIC     0x1fedcba9

#define NEMUQGL_STATE_OVERLAYSTART_MAGIC 0x13579bdf
#define NEMUQGL_STATE_OVERLAYSTOP_MAGIC  0xfdb97531

#define NEMUQGL_SAVE_START(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, NEMUQGL_STATE_START_MAGIC); AssertRC(rc);}while(0)
#define NEMUQGL_SAVE_STOP(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, NEMUQGL_STATE_STOP_MAGIC); AssertRC(rc);}while(0)

#define NEMUQGL_SAVE_SURFSTART(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, NEMUQGL_STATE_SURFSTART_MAGIC); AssertRC(rc);}while(0)
#define NEMUQGL_SAVE_SURFSTOP(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, NEMUQGL_STATE_SURFSTOP_MAGIC); AssertRC(rc);}while(0)

#define NEMUQGL_SAVE_OVERLAYSTART(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, NEMUQGL_STATE_OVERLAYSTART_MAGIC); AssertRC(rc);}while(0)
#define NEMUQGL_SAVE_OVERLAYSTOP(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, NEMUQGL_STATE_OVERLAYSTOP_MAGIC); AssertRC(rc);}while(0)

#define NEMUQGL_LOAD_CHECK(_pSSM, _v) \
    do{ \
        uint32_t _u32; \
        int rc = SSMR3GetU32(_pSSM, &_u32); AssertRC(rc); \
        if(_u32 != (_v)) \
        { \
            NEMUQGLLOG(("load error: expected magic (0x%x), but was (0x%x)\n", (_v), _u32));\
        }\
        Assert(_u32 == (_v)); \
    }while(0)

#define NEMUQGL_LOAD_START(_pSSM) NEMUQGL_LOAD_CHECK(_pSSM, NEMUQGL_STATE_START_MAGIC)
#define NEMUQGL_LOAD_STOP(_pSSM) NEMUQGL_LOAD_CHECK(_pSSM, NEMUQGL_STATE_STOP_MAGIC)

#define NEMUQGL_LOAD_SURFSTART(_pSSM) NEMUQGL_LOAD_CHECK(_pSSM, NEMUQGL_STATE_SURFSTART_MAGIC)
#define NEMUQGL_LOAD_SURFSTOP(_pSSM) NEMUQGL_LOAD_CHECK(_pSSM, NEMUQGL_STATE_SURFSTOP_MAGIC)

#define NEMUQGL_LOAD_OVERLAYSTART(_pSSM) NEMUQGL_LOAD_CHECK(_pSSM, NEMUQGL_STATE_OVERLAYSTART_MAGIC)
#define NEMUQGL_LOAD_OVERLAYSTOP(_pSSM) NEMUQGL_LOAD_CHECK(_pSSM, NEMUQGL_STATE_OVERLAYSTOP_MAGIC)

#else

#define NEMUQGL_SAVE_START(_pSSM) do{}while(0)
#define NEMUQGL_SAVE_STOP(_pSSM) do{}while(0)

#define NEMUQGL_SAVE_SURFSTART(_pSSM) do{}while(0)
#define NEMUQGL_SAVE_SURFSTOP(_pSSM) do{}while(0)

#define NEMUQGL_SAVE_OVERLAYSTART(_pSSM) do{}while(0)
#define NEMUQGL_SAVE_OVERLAYSTOP(_pSSM) do{}while(0)

#define NEMUQGL_LOAD_START(_pSSM) do{}while(0)
#define NEMUQGL_LOAD_STOP(_pSSM) do{}while(0)

#define NEMUQGL_LOAD_SURFSTART(_pSSM) do{}while(0)
#define NEMUQGL_LOAD_SURFSTOP(_pSSM) do{}while(0)

#define NEMUQGL_LOAD_OVERLAYSTART(_pSSM) do{}while(0)
#define NEMUQGL_LOAD_OVERLAYSTOP(_pSSM) do{}while(0)

#endif

static NemuVHWAInfo g_NemuVHWASupportInfo;
static bool g_bNemuVHWAChecked = false;
static bool g_bNemuVHWASupported = false;

class NemuVHWAEntriesCache
{
public:
    NemuVHWAEntriesCache()
    {
        int rc = RTMemCacheCreate(&mNemuCmdEntryCache, sizeof (NemuVHWACommandElement),
                                    0, /* size_t cbAlignment */
                                    UINT32_MAX, /* uint32_t cMaxObjects */
                                    NULL, /* PFNMEMCACHECTOR pfnCtor*/
                                    NULL, /* PFNMEMCACHEDTOR pfnDtor*/
                                    NULL, /* void *pvUser*/
                                    0 /* uint32_t fFlags*/
                                    );
        AssertRC(rc);
    }

    ~NemuVHWAEntriesCache()
    {
        RTMemCacheDestroy(mNemuCmdEntryCache);
    }

    NemuVHWACommandElement * alloc()
    {
        return (NemuVHWACommandElement*)RTMemCacheAlloc(mNemuCmdEntryCache);
    }

    void free(NemuVHWACommandElement * pEl)
    {
        RTMemCacheFree(mNemuCmdEntryCache, pEl);
    }

private:
    RTMEMCACHE mNemuCmdEntryCache;
};

static struct NEMUVHWACMD * vhwaHHCmdCreate(NEMUVHWACMD_TYPE type, size_t size)
{
    char *buf = (char*)malloc(NEMUVHWACMD_SIZE_FROMBODYSIZE(size));
    memset(buf, 0, size);
    NEMUVHWACMD * pCmd = (NEMUVHWACMD*)buf;
    pCmd->enmCmd = type;
    pCmd->Flags = NEMUVHWACMD_FLAG_HH_CMD;
    return pCmd;
}

static const NemuVHWAInfo & nemuVHWAGetSupportInfo(const QGLContext *pContext)
{
    if(!g_NemuVHWASupportInfo.isInitialized())
    {
        if(pContext)
        {
            g_NemuVHWASupportInfo.init(pContext);
        }
        else
        {
            NemuGLTmpContext ctx;
            const QGLContext *pContext = ctx.makeCurrent();
            Assert(pContext);
            if(pContext)
            {
                g_NemuVHWASupportInfo.init(pContext);
            }
        }
    }
    return g_NemuVHWASupportInfo;
}

class NemuVHWACommandProcessEvent : public QEvent
{
public:
    NemuVHWACommandProcessEvent ()
        : QEvent ((QEvent::Type) VHWACommandProcessType),
          fProcessed(false)
    {
#ifdef DEBUG_misha
        g_EventCounter.inc();
#endif
    }

    void setProcessed()
    {
        fProcessed = true;
    }

    ~NemuVHWACommandProcessEvent()
    {
        if (!fProcessed)
        {
            AssertMsgFailed(("VHWA command beinf destroyed unproceessed!"));
            LogRel(("GUI: VHWA command being destroyed unproceessed!"));
        }
#ifdef DEBUG_misha
        g_EventCounter.dec();
#endif
    }
#ifdef DEBUG_misha
    static uint32_t cPending() { return g_EventCounter.refs(); }
#endif

private:
    bool fProcessed;
#ifdef DEBUG_misha
    static NemuVHWARefCounter g_EventCounter;
#endif
};

#ifdef DEBUG_misha
NemuVHWARefCounter NemuVHWACommandProcessEvent::g_EventCounter;
#endif

NemuVHWAHandleTable::NemuVHWAHandleTable(uint32_t initialSize)
{
    mTable = new void*[initialSize];
    memset(mTable, 0, initialSize*sizeof(void*));
    mcSize = initialSize;
    mcUsage = 0;
    mCursor = 1; /* 0 is treated as invalid */
}

NemuVHWAHandleTable::~NemuVHWAHandleTable()
{
    delete[] mTable;
}

uint32_t NemuVHWAHandleTable::put(void * data)
{
    Assert(data);
    if(!data)
        return NEMUVHWA_SURFHANDLE_INVALID;

    if(mcUsage == mcSize)
    {
        /* @todo: resize */
        AssertFailed();
    }

    Assert(mcUsage < mcSize);
    if(mcUsage >= mcSize)
        return NEMUVHWA_SURFHANDLE_INVALID;

    for(int k = 0; k < 2; ++k)
    {
        Assert(mCursor != 0);
        for(uint32_t i = mCursor; i < mcSize; ++i)
        {
            if(!mTable[i])
            {
                doPut(i, data);
                mCursor = i+1;
                return i;
            }
        }
        mCursor = 1; /* 0 is treated as invalid */
    }

    AssertFailed();
    return NEMUVHWA_SURFHANDLE_INVALID;
}

bool NemuVHWAHandleTable::mapPut(uint32_t h, void * data)
{
    if(mcSize <= h)
        return false;
    if(h == 0)
        return false;
    if(mTable[h])
        return false;

    doPut(h, data);
    return true;
}

void* NemuVHWAHandleTable::get(uint32_t h)
{
    Assert(h < mcSize);
    Assert(h > 0);
    return mTable[h];
}

void* NemuVHWAHandleTable::remove(uint32_t h)
{
    Assert(mcUsage);
    Assert(h < mcSize);
    void* val = mTable[h];
    Assert(val);
    if(val)
    {
        doRemove(h);
    }
    return val;
}

void NemuVHWAHandleTable::doPut(uint32_t h, void * data)
{
    ++mcUsage;
    mTable[h] = data;
}

void NemuVHWAHandleTable::doRemove(uint32_t h)
{
    mTable[h] = 0;
    --mcUsage;
}

static NemuVHWATextureImage* nemuVHWAImageCreate(const QRect & aRect, const NemuVHWAColorFormat & aFormat, class NemuVHWAGlProgramMngr * pMgr, NEMUVHWAIMG_TYPE flags)
{
    bool bCanLinearNonFBO = false;
    if (!aFormat.fourcc())
    {
        flags &= ~NEMUVHWAIMG_FBO;
        bCanLinearNonFBO = true;
    }

    const NemuVHWAInfo & info = nemuVHWAGetSupportInfo(NULL);
    if((flags & NEMUVHWAIMG_PBO) && !info.getGlInfo().isPBOSupported())
        flags &= ~NEMUVHWAIMG_PBO;

    if((flags & NEMUVHWAIMG_PBOIMG) &&
            (!info.getGlInfo().isPBOSupported() || !info.getGlInfo().isPBOOffsetSupported()))
        flags &= ~NEMUVHWAIMG_PBOIMG;

    if((flags & NEMUVHWAIMG_FBO) && !info.getGlInfo().isFBOSupported())
        flags &= ~NEMUVHWAIMG_FBO;

    /* ensure we don't create a PBO-based texture in case we use a PBO-based image */
    if(flags & NEMUVHWAIMG_PBOIMG)
        flags &= ~NEMUVHWAIMG_PBO;

    if(flags & NEMUVHWAIMG_FBO)
    {
        if(flags & NEMUVHWAIMG_PBOIMG)
        {
            NEMUQGLLOG(("FBO PBO Image\n"));
            return new NemuVHWATextureImageFBO<NemuVHWATextureImagePBO>(aRect, aFormat, pMgr, flags);
        }
        NEMUQGLLOG(("FBO Generic Image\n"));
        return new NemuVHWATextureImageFBO<NemuVHWATextureImage>(aRect, aFormat, pMgr, flags);
    }

    if (!bCanLinearNonFBO)
    {
        NEMUQGLLOG(("Disabling Linear stretching\n"));
        flags &= ~NEMUVHWAIMG_LINEAR;
    }

    if(flags & NEMUVHWAIMG_PBOIMG)
    {
        NEMUQGLLOG(("PBO Image\n"));
        return new NemuVHWATextureImagePBO(aRect, aFormat, pMgr, flags);
    }

    NEMUQGLLOG(("Generic Image\n"));
    return new NemuVHWATextureImage(aRect, aFormat, pMgr, flags);
}

static NemuVHWATexture* nemuVHWATextureCreate(const QGLContext * pContext, const QRect & aRect, const NemuVHWAColorFormat & aFormat, uint32_t bytesPerLine, NEMUVHWAIMG_TYPE flags)
{
    const NemuVHWAInfo & info = nemuVHWAGetSupportInfo(pContext);
    GLint scaleFunc = (flags & NEMUVHWAIMG_LINEAR) ? GL_LINEAR : GL_NEAREST;
    if((flags & NEMUVHWAIMG_PBO) && info.getGlInfo().isPBOSupported())
    {
        NEMUQGLLOG(("NemuVHWATextureNP2RectPBO\n"));
        return new NemuVHWATextureNP2RectPBO(aRect, aFormat, bytesPerLine, scaleFunc);
    }
    else if(info.getGlInfo().isTextureRectangleSupported())
    {
        NEMUQGLLOG(("NemuVHWATextureNP2Rect\n"));
        return new NemuVHWATextureNP2Rect(aRect, aFormat, bytesPerLine, scaleFunc);
    }
    else if(info.getGlInfo().isTextureNP2Supported())
    {
        NEMUQGLLOG(("NemuVHWATextureNP2\n"));
        return new NemuVHWATextureNP2(aRect, aFormat, bytesPerLine, scaleFunc);
    }
    NEMUQGLLOG(("NemuVHWATexture\n"));
    return new NemuVHWATexture(aRect, aFormat, bytesPerLine, scaleFunc);
}

class NemuVHWAGlShaderComponent
{
public:
    NemuVHWAGlShaderComponent(const char *aRcName, GLenum aType) :
        mRcName(aRcName),
        mType(aType),
        mInitialized(false)
    {}


    int init();

    const char * contents() { return mSource.constData(); }
    bool isInitialized() { return mInitialized; }
private:
    const char *mRcName;
    GLenum mType;
    QByteArray mSource;
    bool mInitialized;
};

int NemuVHWAGlShaderComponent::init()
{
    if(isInitialized())
        return VINF_ALREADY_INITIALIZED;

    QFile fi(mRcName);
    if (!fi.open(QIODevice::ReadOnly))
    {
        AssertFailed();
        return VERR_GENERAL_FAILURE;
    }

    QTextStream is(&fi);
    QString program = is.readAll();

    mSource = program.toAscii();

    mInitialized = true;
    return VINF_SUCCESS;
}

class NemuVHWAGlShader
{
public:
    NemuVHWAGlShader() :
        mType(GL_FRAGMENT_SHADER),
        mcComponents(0)
    {}

    NemuVHWAGlShader & operator= (const NemuVHWAGlShader & other)
    {
        mcComponents = other.mcComponents;
        mType = other.mType;
        if(mcComponents)
        {
            maComponents = new NemuVHWAGlShaderComponent*[mcComponents];
            memcpy(maComponents, other.maComponents, mcComponents * sizeof(maComponents[0]));
        }
        return *this;
    }

    NemuVHWAGlShader(const NemuVHWAGlShader & other)
    {
        mcComponents = other.mcComponents;
        mType = other.mType;
        if(mcComponents)
        {
            maComponents = new NemuVHWAGlShaderComponent*[mcComponents];
            memcpy(maComponents, other.maComponents, mcComponents * sizeof(maComponents[0]));
        }
    }

    NemuVHWAGlShader(GLenum aType, NemuVHWAGlShaderComponent ** aComponents, int cComponents)
        : mType(aType)
    {
        maComponents = new NemuVHWAGlShaderComponent*[cComponents];
        mcComponents = cComponents;
        memcpy(maComponents, aComponents, cComponents * sizeof(maComponents[0]));
    }

    ~NemuVHWAGlShader() {delete[] maComponents;}
    int init();
    GLenum type() { return mType; }
    GLuint shader() { return mShader; }
private:
    GLenum mType;
    GLuint mShader;
    NemuVHWAGlShaderComponent ** maComponents;
    int mcComponents;
};

int NemuVHWAGlShader::init()
{
    int rc = VERR_GENERAL_FAILURE;
    GLint *length;
    const char **sources;
    length = new GLint[mcComponents];
    sources = new const char*[mcComponents];
    for(int i = 0; i < mcComponents; i++)
    {
        length[i] = -1;
        rc = maComponents[i]->init();
        AssertRC(rc);
        if(RT_FAILURE(rc))
            break;
        sources[i] = maComponents[i]->contents();
    }

    if(RT_SUCCESS(rc))
    {
#ifdef DEBUG
        NEMUQGLLOG(("\ncompiling shaders:\n------------\n"));
        for(int i = 0; i < mcComponents; i++)
        {
            NEMUQGLLOG(("**********\n%s\n***********\n", sources[i]));
        }
        NEMUQGLLOG(("------------\n"));
#endif
        mShader = nemuglCreateShader(mType);

        NEMUQGL_CHECKERR(
                nemuglShaderSource(mShader, mcComponents, sources, length);
                );

        NEMUQGL_CHECKERR(
                nemuglCompileShader(mShader);
                );

        GLint compiled;
        NEMUQGL_CHECKERR(
                nemuglGetShaderiv(mShader, GL_COMPILE_STATUS, &compiled);
                );

#ifdef DEBUG
        GLchar * pBuf = new GLchar[16300];
        nemuglGetShaderInfoLog(mShader, 16300, NULL, pBuf);
        NEMUQGLLOG(("\ncompile log:\n-----------\n%s\n---------\n", pBuf));
        delete[] pBuf;
#endif

        Assert(compiled);
        if(compiled)
        {
            rc = VINF_SUCCESS;
        }
        else
        {
            NEMUQGL_CHECKERR(
                    nemuglDeleteShader(mShader);
                    );
            mShader = 0;
        }
    }

    delete[] length;
    delete[] sources;
    return rc;
}

class NemuVHWAGlProgram
{
public:
    NemuVHWAGlProgram(NemuVHWAGlShader ** apShaders, int acShaders);

    ~NemuVHWAGlProgram();

    virtual int init();
    virtual void uninit();
    virtual int start();
    virtual int stop();
    bool isInitialized() { return mProgram; }
    GLuint program() {return mProgram;}
private:
    GLuint mProgram;
    NemuVHWAGlShader *mShaders;
    int mcShaders;
};

NemuVHWAGlProgram::NemuVHWAGlProgram(NemuVHWAGlShader ** apShaders, int acShaders) :
       mProgram(0),
       mcShaders(0)
{
    Assert(acShaders);
    if(acShaders)
    {
        mShaders = new NemuVHWAGlShader[acShaders];
        for(int i = 0; i < acShaders; i++)
        {
            mShaders[i] = *apShaders[i];
        }
        mcShaders = acShaders;
    }
}

NemuVHWAGlProgram::~NemuVHWAGlProgram()
{
    uninit();

    if(mShaders)
    {
        delete[] mShaders;
    }
}

int NemuVHWAGlProgram::init()
{
    Assert(!isInitialized());
    if(isInitialized())
        return VINF_ALREADY_INITIALIZED;

    Assert(mcShaders);
    if(!mcShaders)
        return VERR_GENERAL_FAILURE;

    int rc = VINF_SUCCESS;
    for(int i = 0; i < mcShaders; i++)
    {
        int rc = mShaders[i].init();
        AssertRC(rc);
        if(RT_FAILURE(rc))
        {
            break;
        }
    }
    if(RT_FAILURE(rc))
    {
        return rc;
    }

    mProgram = nemuglCreateProgram();
    Assert(mProgram);
    if(mProgram)
    {
        for(int i = 0; i < mcShaders; i++)
        {
            NEMUQGL_CHECKERR(
                    nemuglAttachShader(mProgram, mShaders[i].shader());
                    );
        }

        NEMUQGL_CHECKERR(
                nemuglLinkProgram(mProgram);
                );


        GLint linked;
        nemuglGetProgramiv(mProgram, GL_LINK_STATUS, &linked);

#ifdef DEBUG
        GLchar * pBuf = new GLchar[16300];
        nemuglGetProgramInfoLog(mProgram, 16300, NULL, pBuf);
        NEMUQGLLOG(("link log: %s\n", pBuf));
        Assert(linked);
        delete pBuf;
#endif

        if(linked)
        {
            return VINF_SUCCESS;
        }

        NEMUQGL_CHECKERR(
                nemuglDeleteProgram(mProgram);
                );
        mProgram = 0;
    }
    return VERR_GENERAL_FAILURE;
}

void NemuVHWAGlProgram::uninit()
{
    if(!isInitialized())
        return;

    NEMUQGL_CHECKERR(
            nemuglDeleteProgram(mProgram);
            );
    mProgram = 0;
}

int NemuVHWAGlProgram::start()
{
    NEMUQGL_CHECKERR(
            nemuglUseProgram(mProgram);
            );
    return VINF_SUCCESS;
}

int NemuVHWAGlProgram::stop()
{
    NEMUQGL_CHECKERR(
            nemuglUseProgram(0);
            );
    return VINF_SUCCESS;
}

#define NEMUVHWA_PROGRAM_DSTCOLORKEY        0x00000001
#define NEMUVHWA_PROGRAM_SRCCOLORKEY        0x00000002
#define NEMUVHWA_PROGRAM_COLORCONV          0x00000004
#define NEMUVHWA_PROGRAM_COLORKEYNODISCARD  0x00000008

#define NEMUVHWA_SUPPORTED_PROGRAM ( \
        NEMUVHWA_PROGRAM_DSTCOLORKEY \
        | NEMUVHWA_PROGRAM_SRCCOLORKEY \
        | NEMUVHWA_PROGRAM_COLORCONV \
        | NEMUVHWA_PROGRAM_COLORKEYNODISCARD \
        )

class NemuVHWAGlProgramVHWA : public NemuVHWAGlProgram
{
public:
    NemuVHWAGlProgramVHWA(uint32_t type, uint32_t fourcc, NemuVHWAGlShader ** apShaders, int acShaders);

    uint32_t type() const {return mType;}
    uint32_t fourcc() const {return mFourcc;}

    int setDstCKeyUpperRange(GLfloat r, GLfloat g, GLfloat b);

    int setDstCKeyLowerRange(GLfloat r, GLfloat g, GLfloat b);

    int setSrcCKeyUpperRange(GLfloat r, GLfloat g, GLfloat b);

    int setSrcCKeyLowerRange(GLfloat r, GLfloat g, GLfloat b);


    virtual int init();

    bool matches(uint32_t type, uint32_t fourcc)
    {
        return mType == type && mFourcc == fourcc;
    }

    bool equals(const NemuVHWAGlProgramVHWA & other)
    {
        return matches(other.mType, other.mFourcc);
    }

private:
    uint32_t mType;
    uint32_t mFourcc;

    GLfloat mDstUpperR, mDstUpperG, mDstUpperB;
    GLint mUniDstUpperColor;

    GLfloat mDstLowerR, mDstLowerG, mDstLowerB;
    GLint mUniDstLowerColor;

    GLfloat mSrcUpperR, mSrcUpperG, mSrcUpperB;
    GLint mUniSrcUpperColor;

    GLfloat mSrcLowerR, mSrcLowerG, mSrcLowerB;
    GLint mUniSrcLowerColor;

    GLint mDstTex;
    GLint mUniDstTex;

    GLint mSrcTex;
    GLint mUniSrcTex;

    GLint mVTex;
    GLint mUniVTex;

    GLint mUTex;
    GLint mUniUTex;
};

NemuVHWAGlProgramVHWA::NemuVHWAGlProgramVHWA(uint32_t type, uint32_t fourcc, NemuVHWAGlShader ** apShaders, int acShaders) :
    NemuVHWAGlProgram(apShaders, acShaders),
    mType(type),
    mFourcc(fourcc),
    mDstUpperR(0.0), mDstUpperG(0.0), mDstUpperB(0.0),
    mUniDstUpperColor(-1),
    mDstLowerR(0.0), mDstLowerG(0.0), mDstLowerB(0.0),
    mUniDstLowerColor(-1),
    mSrcUpperR(0.0), mSrcUpperG(0.0), mSrcUpperB(0.0),
    mUniSrcUpperColor(-1),
    mSrcLowerR(0.0), mSrcLowerG(0.0), mSrcLowerB(0.0),
    mUniSrcLowerColor(-1),
    mDstTex(-1),
    mUniDstTex(-1),
    mSrcTex(-1),
    mUniSrcTex(-1),
    mVTex(-1),
    mUniVTex(-1),
    mUTex(-1),
    mUniUTex(-1)
{}

int NemuVHWAGlProgramVHWA::init()
{
    int rc = NemuVHWAGlProgram::init();
    if(RT_FAILURE(rc))
        return rc;
    if(rc == VINF_ALREADY_INITIALIZED)
        return rc;

    start();

    rc = VERR_GENERAL_FAILURE;

    do
    {
        GLint tex = 0;
        mUniSrcTex = nemuglGetUniformLocation(program(), "uSrcTex");
        Assert(mUniSrcTex != -1);
        if(mUniSrcTex == -1)
            break;

        NEMUQGL_CHECKERR(
                nemuglUniform1i(mUniSrcTex, tex);
                );
        mSrcTex = tex;
        ++tex;

        if(type() & NEMUVHWA_PROGRAM_SRCCOLORKEY)
        {
            mUniSrcLowerColor = nemuglGetUniformLocation(program(), "uSrcClr");
            Assert(mUniSrcLowerColor != -1);
            if(mUniSrcLowerColor == -1)
                break;

            mSrcLowerR = 0.0; mSrcLowerG = 0.0; mSrcLowerB = 0.0;

            NEMUQGL_CHECKERR(
                    nemuglUniform4f(mUniSrcLowerColor, 0.0, 0.0, 0.0, 0.0);
                    );
        }

        if(type() & NEMUVHWA_PROGRAM_COLORCONV)
        {
            switch(fourcc())
            {
                case FOURCC_YV12:
                {
                    mUniVTex = nemuglGetUniformLocation(program(), "uVTex");
                    Assert(mUniVTex != -1);
                    if(mUniVTex == -1)
                        break;

                    NEMUQGL_CHECKERR(
                            nemuglUniform1i(mUniVTex, tex);
                            );
                    mVTex = tex;
                    ++tex;

                    mUniUTex = nemuglGetUniformLocation(program(), "uUTex");
                    Assert(mUniUTex != -1);
                    if(mUniUTex == -1)
                        break;
                    NEMUQGL_CHECKERR(
                            nemuglUniform1i(mUniUTex, tex);
                            );
                    mUTex = tex;
                    ++tex;

                    break;
                }
                case FOURCC_UYVY:
                case FOURCC_YUY2:
                case FOURCC_AYUV:
                    break;
                default:
                    AssertFailed();
                    break;
            }
        }

        if(type() & NEMUVHWA_PROGRAM_DSTCOLORKEY)
        {

            mUniDstTex = nemuglGetUniformLocation(program(), "uDstTex");
            Assert(mUniDstTex != -1);
            if(mUniDstTex == -1)
                break;
            NEMUQGL_CHECKERR(
                    nemuglUniform1i(mUniDstTex, tex);
                    );
            mDstTex = tex;
            ++tex;

            mUniDstLowerColor = nemuglGetUniformLocation(program(), "uDstClr");
            Assert(mUniDstLowerColor != -1);
            if(mUniDstLowerColor == -1)
                break;

            mDstLowerR = 0.0; mDstLowerG = 0.0; mDstLowerB = 0.0;

            NEMUQGL_CHECKERR(
                    nemuglUniform4f(mUniDstLowerColor, 0.0, 0.0, 0.0, 0.0);
                    );
        }

        rc = VINF_SUCCESS;
    } while(0);


    stop();
    if(rc == VINF_SUCCESS)
        return VINF_SUCCESS;

    AssertFailed();
    NemuVHWAGlProgram::uninit();
    return VERR_GENERAL_FAILURE;
}

int NemuVHWAGlProgramVHWA::setDstCKeyUpperRange(GLfloat r, GLfloat g, GLfloat b)
{
    Assert(isInitialized());
    if(!isInitialized())
        return VERR_GENERAL_FAILURE;
    if(mDstUpperR == r && mDstUpperG == g && mDstUpperB == b)
        return VINF_ALREADY_INITIALIZED;
    nemuglUniform4f(mUniDstUpperColor, r, g, b, 0.0);
    mDstUpperR = r;
    mDstUpperG = g;
    mDstUpperB = b;
    return VINF_SUCCESS;
}

int NemuVHWAGlProgramVHWA::setDstCKeyLowerRange(GLfloat r, GLfloat g, GLfloat b)
{
    Assert(isInitialized());
    if(!isInitialized())
        return VERR_GENERAL_FAILURE;
    if(mDstLowerR == r && mDstLowerG == g && mDstLowerB == b)
        return VINF_ALREADY_INITIALIZED;

    NEMUQGL_CHECKERR(
            nemuglUniform4f(mUniDstLowerColor, r, g, b, 0.0);
            );

    mDstLowerR = r;
    mDstLowerG = g;
    mDstLowerB = b;
    return VINF_SUCCESS;
}

int NemuVHWAGlProgramVHWA::setSrcCKeyUpperRange(GLfloat r, GLfloat g, GLfloat b)
{
    Assert(isInitialized());
    if(!isInitialized())
        return VERR_GENERAL_FAILURE;
    if(mSrcUpperR == r && mSrcUpperG == g && mSrcUpperB == b)
        return VINF_ALREADY_INITIALIZED;
    nemuglUniform4f(mUniSrcUpperColor, r, g, b, 0.0);
    mSrcUpperR = r;
    mSrcUpperG = g;
    mSrcUpperB = b;
    return VINF_SUCCESS;
}

int NemuVHWAGlProgramVHWA::setSrcCKeyLowerRange(GLfloat r, GLfloat g, GLfloat b)
{
    Assert(isInitialized());
    if(!isInitialized())
        return VERR_GENERAL_FAILURE;
    if(mSrcLowerR == r && mSrcLowerG == g && mSrcLowerB == b)
        return VINF_ALREADY_INITIALIZED;
    NEMUQGL_CHECKERR(
            nemuglUniform4f(mUniSrcLowerColor, r, g, b, 0.0);
            );
    mSrcLowerR = r;
    mSrcLowerG = g;
    mSrcLowerB = b;
    return VINF_SUCCESS;
}

class NemuVHWAGlProgramMngr
{
public:
    NemuVHWAGlProgramMngr() :
        mShaderCConvApplyAYUV(":/cconvApplyAYUV.c", GL_FRAGMENT_SHADER),
        mShaderCConvAYUV(":/cconvAYUV.c", GL_FRAGMENT_SHADER),
        mShaderCConvBGR(":/cconvBGR.c", GL_FRAGMENT_SHADER),
        mShaderCConvUYVY(":/cconvUYVY.c", GL_FRAGMENT_SHADER),
        mShaderCConvYUY2(":/cconvYUY2.c", GL_FRAGMENT_SHADER),
        mShaderCConvYV12(":/cconvYV12.c", GL_FRAGMENT_SHADER),
        mShaderSplitBGRA(":/splitBGRA.c", GL_FRAGMENT_SHADER),
        mShaderCKeyDst(":/ckeyDst.c", GL_FRAGMENT_SHADER),
        mShaderCKeyDst2(":/ckeyDst2.c", GL_FRAGMENT_SHADER),
        mShaderMainOverlay(":/mainOverlay.c", GL_FRAGMENT_SHADER),
        mShaderMainOverlayNoCKey(":/mainOverlayNoCKey.c", GL_FRAGMENT_SHADER),
        mShaderMainOverlayNoDiscard(":/mainOverlayNoDiscard.c", GL_FRAGMENT_SHADER),
        mShaderMainOverlayNoDiscard2(":/mainOverlayNoDiscard2.c", GL_FRAGMENT_SHADER)
    {}

    NemuVHWAGlProgramVHWA * getProgram(uint32_t type, const NemuVHWAColorFormat * pFrom, const NemuVHWAColorFormat * pTo);

    void stopCurrentProgram()
    {
        NEMUQGL_CHECKERR(
            nemuglUseProgram(0);
            );
    }
private:
    NemuVHWAGlProgramVHWA * searchProgram(uint32_t type, uint32_t fourcc, bool bCreate);

    NemuVHWAGlProgramVHWA * createProgram(uint32_t type, uint32_t fourcc);

    typedef std::list <NemuVHWAGlProgramVHWA*> ProgramList;

    ProgramList mPrograms;

    NemuVHWAGlShaderComponent mShaderCConvApplyAYUV;

    NemuVHWAGlShaderComponent mShaderCConvAYUV;
    NemuVHWAGlShaderComponent mShaderCConvBGR;
    NemuVHWAGlShaderComponent mShaderCConvUYVY;
    NemuVHWAGlShaderComponent mShaderCConvYUY2;
    NemuVHWAGlShaderComponent mShaderCConvYV12;
    NemuVHWAGlShaderComponent mShaderSplitBGRA;

    /* expected the dst surface texture to be bound to the 1-st tex unit */
    NemuVHWAGlShaderComponent mShaderCKeyDst;
    /* expected the dst surface texture to be bound to the 2-nd tex unit */
    NemuVHWAGlShaderComponent mShaderCKeyDst2;
    NemuVHWAGlShaderComponent mShaderMainOverlay;
    NemuVHWAGlShaderComponent mShaderMainOverlayNoCKey;
    NemuVHWAGlShaderComponent mShaderMainOverlayNoDiscard;
    NemuVHWAGlShaderComponent mShaderMainOverlayNoDiscard2;

    friend class NemuVHWAGlProgramVHWA;
};

NemuVHWAGlProgramVHWA * NemuVHWAGlProgramMngr::createProgram(uint32_t type, uint32_t fourcc)
{
    NemuVHWAGlShaderComponent * apShaders[16];
    uint32_t cShaders = 0;

    /* workaround for NVIDIA driver bug: ensure we attach the shader before those it is used in */
    /* reserve a slot for the mShaderCConvApplyAYUV,
     * in case it is not used the slot will be occupied by mShaderCConvBGR , which is ok */
    cShaders++;

    if(!!(type & NEMUVHWA_PROGRAM_DSTCOLORKEY)
            && !(type & NEMUVHWA_PROGRAM_COLORKEYNODISCARD))
    {
        if(fourcc == FOURCC_YV12)
        {
            apShaders[cShaders++] = &mShaderCKeyDst2;
        }
        else
        {
            apShaders[cShaders++] = &mShaderCKeyDst;
        }
    }

    if(type & NEMUVHWA_PROGRAM_SRCCOLORKEY)
    {
        AssertFailed();
        /* disabled for now, not really necessary for video overlaying */
    }

    bool bFound = false;

//    if(type & NEMUVHWA_PROGRAM_COLORCONV)
    {
        if(fourcc == FOURCC_UYVY)
        {
            apShaders[cShaders++] = &mShaderCConvUYVY;
            bFound = true;
        }
        else if(fourcc == FOURCC_YUY2)
        {
            apShaders[cShaders++] = &mShaderCConvYUY2;
            bFound = true;
        }
        else if(fourcc == FOURCC_YV12)
        {
            apShaders[cShaders++] = &mShaderCConvYV12;
            bFound = true;
        }
        else if(fourcc == FOURCC_AYUV)
        {
            apShaders[cShaders++] = &mShaderCConvAYUV;
            bFound = true;
        }
    }

    if(bFound)
    {
        type |= NEMUVHWA_PROGRAM_COLORCONV;
        apShaders[0] = &mShaderCConvApplyAYUV;
    }
    else
    {
        type &= (~NEMUVHWA_PROGRAM_COLORCONV);
        apShaders[0] = &mShaderCConvBGR;
    }

    if(type & NEMUVHWA_PROGRAM_DSTCOLORKEY)
    {
        if(type & NEMUVHWA_PROGRAM_COLORKEYNODISCARD)
        {
            if(fourcc == FOURCC_YV12)
            {
                apShaders[cShaders++] = &mShaderMainOverlayNoDiscard2;
            }
            else
            {
                apShaders[cShaders++] = &mShaderMainOverlayNoDiscard;
            }
        }
        else
        {
            apShaders[cShaders++] = &mShaderMainOverlay;
        }
    }
    else
    {
        // ensure we don't have empty functions /* paranoia for for ATI on linux */
        apShaders[cShaders++] = &mShaderMainOverlayNoCKey;
    }

    Assert(cShaders <= RT_ELEMENTS(apShaders));

    NemuVHWAGlShader shader(GL_FRAGMENT_SHADER, apShaders, cShaders);
    NemuVHWAGlShader *pShader = &shader;

    NemuVHWAGlProgramVHWA *pProgram =  new NemuVHWAGlProgramVHWA(/*this, */type, fourcc, &pShader, 1);
    pProgram->init();

    return pProgram;
}

NemuVHWAGlProgramVHWA * NemuVHWAGlProgramMngr::getProgram(uint32_t type, const NemuVHWAColorFormat * pFrom, const NemuVHWAColorFormat * pTo)
{
    Q_UNUSED(pTo);
    uint32_t fourcc = 0;
    type &= NEMUVHWA_SUPPORTED_PROGRAM;

    if(pFrom && pFrom->fourcc())
    {
        fourcc = pFrom->fourcc();
        type |= NEMUVHWA_PROGRAM_COLORCONV;
    }
    else
    {
        type &= (~NEMUVHWA_PROGRAM_COLORCONV);
    }

    if(!(type & NEMUVHWA_PROGRAM_DSTCOLORKEY)
            && !(type & NEMUVHWA_PROGRAM_SRCCOLORKEY))
    {
        type &= (~NEMUVHWA_PROGRAM_COLORKEYNODISCARD);
    }

    if(type)
        return searchProgram(type, fourcc, true);
    return NULL;
}

NemuVHWAGlProgramVHWA * NemuVHWAGlProgramMngr::searchProgram(uint32_t type, uint32_t fourcc, bool bCreate)
{
    for (ProgramList::const_iterator it = mPrograms.begin();
         it != mPrograms.end(); ++ it)
    {
        if (!(*it)->matches(type, fourcc))
        {
            continue;
        }
        return *it;
    }
    if(bCreate)
    {
        NemuVHWAGlProgramVHWA *pProgram = createProgram(type, fourcc);
        if(pProgram)
        {
            mPrograms.push_back(pProgram);
            return pProgram;
        }
    }
    return NULL;
}

void NemuVHWASurfaceBase::setAddress(uchar * addr)
{
    Assert(addr);
    if(!addr) return;
    if(addr == mAddress) return;

    if(mFreeAddress)
    {
        free(mAddress);
    }

    mAddress = addr;
    mFreeAddress = false;

    mImage->setAddress(mAddress);

    mUpdateMem2TexRect.set(mRect);
    Assert(!mUpdateMem2TexRect.isClear());
    Assert(mRect.contains(mUpdateMem2TexRect.rect()));
}

void NemuVHWASurfaceBase::globalInit()
{
    NEMUQGLLOG(("globalInit\n"));

    glEnable(GL_TEXTURE_RECTANGLE);
    glDisable(GL_DEPTH_TEST);

    NEMUQGL_CHECKERR(
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            );
    NEMUQGL_CHECKERR(
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            );
}

NemuVHWASurfaceBase::NemuVHWASurfaceBase(class NemuVHWAImage *pImage,
        const QSize & aSize,
        const QRect & aTargRect,
        const QRect & aSrcRect,
        const QRect & aVisTargRect,
        NemuVHWAColorFormat & aColorFormat,
        NemuVHWAColorKey * pSrcBltCKey, NemuVHWAColorKey * pDstBltCKey,
                    NemuVHWAColorKey * pSrcOverlayCKey, NemuVHWAColorKey * pDstOverlayCKey,
                    NEMUVHWAIMG_TYPE aImgFlags) :
                mRect(0,0,aSize.width(),aSize.height()),
                mAddress(NULL),
                mpSrcBltCKey(NULL),
                mpDstBltCKey(NULL),
                mpSrcOverlayCKey(NULL),
                mpDstOverlayCKey(NULL),
                mpDefaultDstOverlayCKey(NULL),
                mpDefaultSrcOverlayCKey(NULL),
                mLockCount(0),
                mFreeAddress(false),
                mbNotIntersected(false),
                mComplexList(NULL),
                mpPrimary(NULL),
                mHGHandle(NEMUVHWA_SURFHANDLE_INVALID),
                mpImage(pImage)
#ifdef DEBUG
                ,
                cFlipsCurr(0),
                cFlipsTarg(0)
#endif
{
    setDstBltCKey(pDstBltCKey);
    setSrcBltCKey(pSrcBltCKey);

    setDefaultDstOverlayCKey(pDstOverlayCKey);
    resetDefaultDstOverlayCKey();

    setDefaultSrcOverlayCKey(pSrcOverlayCKey);
    resetDefaultSrcOverlayCKey();

    mImage = nemuVHWAImageCreate(QRect(0,0,aSize.width(),aSize.height()), aColorFormat, getGlProgramMngr(), aImgFlags);

    setRectValues(aTargRect, aSrcRect);
    setVisibleRectValues(aVisTargRect);
}

NemuVHWASurfaceBase::~NemuVHWASurfaceBase()
{
    uninit();
}

GLsizei NemuVHWASurfaceBase::makePowerOf2(GLsizei val)
{
    int last = ASMBitLastSetS32(val);
    if(last>1)
    {
        last--;
        if((1 << last) != val)
        {
            Assert((1 << last) < val);
            val = (1 << (last+1));
        }
    }
    return val;
}

ulong NemuVHWASurfaceBase::calcBytesPerPixel(GLenum format, GLenum type)
{
    /* we now support only common byte-aligned data */
    int numComponents = 0;
    switch(format)
    {
    case GL_COLOR_INDEX:
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        numComponents = 1;
        break;
    case GL_RGB:
    case GL_BGR_EXT:
        numComponents = 3;
        break;
    case GL_RGBA:
    case GL_BGRA_EXT:
        numComponents = 4;
        break;
    case GL_LUMINANCE_ALPHA:
        numComponents = 2;
        break;
    default:
        AssertFailed();
        break;
    }

    int componentSize = 0;
    switch(type)
    {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
        componentSize = 1;
        break;
    //case GL_BITMAP:
    case  GL_UNSIGNED_SHORT:
    case GL_SHORT:
        componentSize = 2;
        break;
    case GL_UNSIGNED_INT:
    case GL_INT:
    case GL_FLOAT:
        componentSize = 4;
        break;
    default:
        AssertFailed();
        break;
    }
    return numComponents * componentSize;
}

void NemuVHWASurfaceBase::uninit()
{
    delete mImage;

    if(mAddress && mFreeAddress)
    {
        free(mAddress);
        mAddress = NULL;
    }
}

ulong NemuVHWASurfaceBase::memSize()
{
    return (ulong)mImage->memSize();
}

void NemuVHWASurfaceBase::init(NemuVHWASurfaceBase * pPrimary, uchar *pvMem)
{
    if(pPrimary)
    {
        NEMUQGL_CHECKERR(
                nemuglActiveTexture(GL_TEXTURE1);
            );
    }

    int size = memSize();
    uchar * address = (uchar *)malloc(size);
#ifdef DEBUG_misha
    int tex0Size = mImage->component(0)->memSize();
    if(pPrimary)
    {
        memset(address, 0xff, tex0Size);
        Assert(size >= tex0Size);
        if(size > tex0Size)
        {
            memset(address + tex0Size, 0x0, size - tex0Size);
        }
    }
    else
    {
        memset(address, 0x0f, tex0Size);
        Assert(size >= tex0Size);
        if(size > tex0Size)
        {
            memset(address + tex0Size, 0x3f, size - tex0Size);
        }
    }
#else
    memset(address, 0, size);
#endif

    mImage->init(address);
    mpPrimary = pPrimary;

    if(pvMem)
    {
        mAddress = pvMem;
        free(address);
        mFreeAddress = false;

    }
    else
    {
        mAddress = address;
        mFreeAddress = true;
    }

    mImage->setAddress(mAddress);

    initDisplay();

    mUpdateMem2TexRect.set(mRect);
    Assert(!mUpdateMem2TexRect.isClear());
    Assert(mRect.contains(mUpdateMem2TexRect.rect()));

    if(pPrimary)
    {
        NEMUQGLLOG(("restoring to tex 0"));
        NEMUQGL_CHECKERR(
                nemuglActiveTexture(GL_TEXTURE0);
            );
    }

}

void NemuVHWATexture::doUpdate(uchar * pAddress, const QRect * pRect)
{
    GLenum tt = texTarget();
    if(pRect)
    {
        Assert(mRect.contains(*pRect));
    }
    else
    {
        pRect = &mRect;
    }

    Assert(glIsTexture(mTexture));
    NEMUQGL_CHECKERR(
            glBindTexture(tt, mTexture);
            );

    int x = pRect->x()/mColorFormat.widthCompression();
    int y = pRect->y()/mColorFormat.heightCompression();
    int width = pRect->width()/mColorFormat.widthCompression();
    int height = pRect->height()/mColorFormat.heightCompression();

    uchar * address = pAddress + pointOffsetTex(x, y);

    NEMUQGL_CHECKERR(
            glPixelStorei(GL_UNPACK_ROW_LENGTH, mBytesPerLine * 8 /mColorFormat.bitsPerPixelTex());
            );

    NEMUQGL_CHECKERR(
            glTexSubImage2D(tt,
                0,
                x, y, width, height,
                mColorFormat.format(),
                mColorFormat.type(),
                address);
            );

    NEMUQGL_CHECKERR(
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            );
}

void NemuVHWATexture::texCoord(int x, int y)
{
    glTexCoord2f(((float)x)/mTexRect.width()/mColorFormat.widthCompression(), ((float)y)/mTexRect.height()/mColorFormat.heightCompression());
}

void NemuVHWATexture::multiTexCoord(GLenum texUnit, int x, int y)
{
    nemuglMultiTexCoord2f(texUnit, ((float)x)/mTexRect.width()/mColorFormat.widthCompression(), ((float)y)/mTexRect.height()/mColorFormat.heightCompression());
}

void NemuVHWATexture::uninit()
{
    if(mTexture)
    {
        glDeleteTextures(1,&mTexture);
    }
}

NemuVHWATexture::NemuVHWATexture(const QRect & aRect, const NemuVHWAColorFormat &aFormat, uint32_t bytesPerLine, GLint scaleFuncttion) :
            mAddress(NULL),
            mTexture(0),
            mBytesPerPixel(0),
            mBytesPerPixelTex(0),
            mBytesPerLine(0),
            mScaleFuncttion(scaleFuncttion)
{
    mColorFormat = aFormat;
    mRect = aRect;
    mBytesPerPixel = mColorFormat.bitsPerPixel()/8;
    mBytesPerPixelTex = mColorFormat.bitsPerPixelTex()/8;
    mBytesPerLine = bytesPerLine ? bytesPerLine : mBytesPerPixel * mRect.width();
    GLsizei wdt = NemuVHWASurfaceBase::makePowerOf2(mRect.width()/mColorFormat.widthCompression());
    GLsizei hgt = NemuVHWASurfaceBase::makePowerOf2(mRect.height()/mColorFormat.heightCompression());
    mTexRect = QRect(0,0,wdt,hgt);
}

#ifdef DEBUG_misha
void NemuVHWATexture::dbgDump()
{
#if 0
    bind();
    GLvoid *pvBuf = malloc(4 * mRect.width() * mRect.height());
    NEMUQGL_CHECKERR(
        glGetTexImage(texTarget(),
            0, /*GLint level*/
            mColorFormat.format(),
            mColorFormat.type(),
            pvBuf);
    );
    NEMUQGLDBGPRINT(("<?dml?><exec cmd=\"!vbvdbg.ms 0x%p 0n%d 0n%d\">texture info</exec>\n",
            pvBuf, mRect.width(), mRect.height()));
    AssertFailed();

    free(pvBuf);
#endif
}
#endif


void NemuVHWATexture::initParams()
{
    GLenum tt = texTarget();

    glTexParameteri(tt, GL_TEXTURE_MIN_FILTER, mScaleFuncttion);
    NEMUQGL_ASSERTNOERR();
    glTexParameteri(tt, GL_TEXTURE_MAG_FILTER, mScaleFuncttion);
    NEMUQGL_ASSERTNOERR();
    glTexParameteri(tt, GL_TEXTURE_WRAP_S, GL_CLAMP);
    NEMUQGL_ASSERTNOERR();

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    NEMUQGL_ASSERTNOERR();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    NEMUQGL_ASSERTNOERR();

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    NEMUQGL_ASSERTNOERR();
}

void NemuVHWATexture::load()
{
    NEMUQGL_CHECKERR(
            glPixelStorei(GL_UNPACK_ROW_LENGTH, mTexRect.width());
            );

    NEMUQGL_CHECKERR(
        glTexImage2D(texTarget(),
                0,
                  mColorFormat.internalFormat(),
                  mTexRect.width(),
                  mTexRect.height(),
                  0,
                  mColorFormat.format(),
                  mColorFormat.type(),
                  (GLvoid *)mAddress);
        );
}

void NemuVHWATexture::init(uchar *pvMem)
{
//    GLsizei wdt = mTexRect.width();
//    GLsizei hgt = mTexRect.height();

    NEMUQGL_CHECKERR(
            glGenTextures(1, &mTexture);
        );

    NEMUQGLLOG(("tex: %d", mTexture));

    bind();

    initParams();

    setAddress(pvMem);

    load();
}

NemuVHWATexture::~NemuVHWATexture()
{
    uninit();
}

void NemuVHWATextureNP2Rect::texCoord(int x, int y)
{
    glTexCoord2i(x/mColorFormat.widthCompression(), y/mColorFormat.heightCompression());
}

void NemuVHWATextureNP2Rect::multiTexCoord(GLenum texUnit, int x, int y)
{
    nemuglMultiTexCoord2i(texUnit, x/mColorFormat.widthCompression(), y/mColorFormat.heightCompression());
}

GLenum NemuVHWATextureNP2Rect::texTarget() {return GL_TEXTURE_RECTANGLE; }

bool NemuVHWASurfaceBase::synchTexMem(const QRect * pRect)
{
    if(pRect)
    {
        Assert(mRect.contains(*pRect));
    }

    if(mUpdateMem2TexRect.isClear())
        return false;

    if(pRect && !mUpdateMem2TexRect.rect().intersects(*pRect))
        return false;

#ifdef NEMUVHWA_PROFILE_FPS
    mpImage->reportNewFrame();
#endif

    mImage->update(&mUpdateMem2TexRect.rect());

    mUpdateMem2TexRect.clear();
    Assert(mUpdateMem2TexRect.isClear());

    return true;
}

void NemuVHWATextureNP2RectPBO::init(uchar *pvMem)
{
    NEMUQGL_CHECKERR(
            nemuglGenBuffers(1, &mPBO);
            );
    NemuVHWATextureNP2Rect::init(pvMem);
}

void NemuVHWATextureNP2RectPBO::doUpdate(uchar * pAddress, const QRect * pRect)
{
    Q_UNUSED(pAddress);
    Q_UNUSED(pRect);

    nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);

    GLvoid *buf;

    NEMUQGL_CHECKERR(
            buf = nemuglMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
            );
    Assert(buf);
    if(buf)
    {
        memcpy(buf, mAddress, memSize());

        bool unmapped;
        NEMUQGL_CHECKERR(
                unmapped = nemuglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                );

        Assert(unmapped); NOREF(unmapped);

        NemuVHWATextureNP2Rect::doUpdate(0, &mRect);

        nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    else
    {
        NEMUQGLLOGREL(("failed to map PBO, trying fallback to non-PBO approach\n"));
        /* try fallback to non-PBO approach */
        nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        NemuVHWATextureNP2Rect::doUpdate(pAddress, pRect);
    }
}

NemuVHWATextureNP2RectPBO::~NemuVHWATextureNP2RectPBO()
{
    NEMUQGL_CHECKERR(
            nemuglDeleteBuffers(1, &mPBO);
            );
}


void NemuVHWATextureNP2RectPBO::load()
{
    NemuVHWATextureNP2Rect::load();

    NEMUQGL_CHECKERR(
            nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
        );

    NEMUQGL_CHECKERR(
            nemuglBufferData(GL_PIXEL_UNPACK_BUFFER, memSize(), NULL, GL_STREAM_DRAW);
        );

    GLvoid *buf = nemuglMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    Assert(buf);
    if(buf)
    {
        memcpy(buf, mAddress, memSize());

        bool unmapped = nemuglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        Assert(unmapped); NOREF(unmapped);
    }

    nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

uchar* NemuVHWATextureNP2RectPBOMapped::mapAlignedBuffer()
{
    Assert(!mpMappedAllignedBuffer);
    if(!mpMappedAllignedBuffer)
    {
        NEMUQGL_CHECKERR(
                nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
            );

        uchar* buf;
        NEMUQGL_CHECKERR(
                buf = (uchar*)nemuglMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_READ_WRITE);
        );

        Assert(buf);

        NEMUQGL_CHECKERR(
                nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            );

        mpMappedAllignedBuffer = (uchar*)alignBuffer(buf);

        mcbOffset = calcOffset(buf, mpMappedAllignedBuffer);
    }
    return mpMappedAllignedBuffer;
}

void NemuVHWATextureNP2RectPBOMapped::unmapBuffer()
{
    Assert(mpMappedAllignedBuffer);
    if(mpMappedAllignedBuffer)
    {
        NEMUQGL_CHECKERR(
                nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
        );

        bool unmapped;
        NEMUQGL_CHECKERR(
                unmapped = nemuglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                );

        Assert(unmapped); NOREF(unmapped);

        NEMUQGL_CHECKERR(
                nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        );

        mpMappedAllignedBuffer = NULL;
    }
}

void NemuVHWATextureNP2RectPBOMapped::load()
{
    NemuVHWATextureNP2Rect::load();

    NEMUQGL_CHECKERR(
            nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
        );

    NEMUQGL_CHECKERR(
            nemuglBufferData(GL_PIXEL_UNPACK_BUFFER, mcbActualBufferSize, NULL, GL_STREAM_DRAW);
        );

    nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void NemuVHWATextureNP2RectPBOMapped::doUpdate(uchar * pAddress, const QRect * pRect)
{
    Q_UNUSED(pAddress);
    Q_UNUSED(pRect);

    NEMUQGL_CHECKERR(
            nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
    );

    if(mpMappedAllignedBuffer)
    {
        bool unmapped;
        NEMUQGL_CHECKERR(
                unmapped = nemuglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                );

        Assert(unmapped); NOREF(unmapped);

        mpMappedAllignedBuffer = NULL;
    }

    NemuVHWATextureNP2Rect::doUpdate((uchar *)mcbOffset, &mRect);

    NEMUQGL_CHECKERR(
            nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    );
}

int NemuVHWASurfaceBase::lock(const QRect * pRect, uint32_t flags)
{
    Q_UNUSED(flags);

    if(pRect)
    {
        Assert(mRect.contains(*pRect));
    }

    Assert(mLockCount >= 0);
    if(pRect && pRect->isEmpty())
        return VERR_GENERAL_FAILURE;
    if(mLockCount < 0)
        return VERR_GENERAL_FAILURE;

    NEMUQGLLOG(("lock (0x%x)", this));
    NEMUQGLLOG_QRECT("rect: ", pRect ? pRect : &mRect, "\n");
    NEMUQGLLOG_METHODTIME("time ");

    mUpdateMem2TexRect.add(pRect ? *pRect : mRect);

    Assert(!mUpdateMem2TexRect.isClear());
    Assert(mRect.contains(mUpdateMem2TexRect.rect()));
    return VINF_SUCCESS;
}

int NemuVHWASurfaceBase::unlock()
{
    NEMUQGLLOG(("unlock (0x%x)\n", this));
    mLockCount = 0;
    return VINF_SUCCESS;
}

void NemuVHWASurfaceBase::setRectValues (const QRect & aTargRect, const QRect & aSrcRect)
{
    mTargRect = aTargRect;
    mSrcRect = aSrcRect;
}

void NemuVHWASurfaceBase::setVisibleRectValues (const QRect & aVisTargRect)
{
    mVisibleTargRect = aVisTargRect.intersected(mTargRect);
    if(mVisibleTargRect.isEmpty() || mTargRect.isEmpty())
    {
        mVisibleSrcRect.setSize(QSize(0, 0));
    }
    else
    {
        float stretchX = float(mSrcRect.width()) / mTargRect.width();
        float stretchY = float(mSrcRect.height()) / mTargRect.height();
        int tx1, tx2, ty1, ty2, vtx1, vtx2, vty1, vty2;
        int sx1, sx2, sy1, sy2;
        mVisibleTargRect.getCoords(&vtx1, &vty1, &vtx2, &vty2);
        mTargRect.getCoords(&tx1, &ty1, &tx2, &ty2);
        mSrcRect.getCoords(&sx1, &sy1, &sx2, &sy2);
        int dx1 = vtx1 - tx1;
        int dy1 = vty1 - ty1;
        int dx2 = vtx2 - tx2;
        int dy2 = vty2 - ty2;
        int vsx1, vsy1, vsx2, vsy2;
        Assert(dx1 >= 0);
        Assert(dy1 >= 0);
        Assert(dx2 <= 0);
        Assert(dy2 <= 0);
        vsx1 = sx1 + int(dx1*stretchX);
        vsy1 = sy1 + int(dy1*stretchY);
        vsx2 = sx2 + int(dx2*stretchX);
        vsy2 = sy2 + int(dy2*stretchY);
        mVisibleSrcRect.setCoords(vsx1, vsy1, vsx2, vsy2);
        Assert(!mVisibleSrcRect.isEmpty());
        Assert(mSrcRect.contains(mVisibleSrcRect));
    }
}


void NemuVHWASurfaceBase::setRects(const QRect & aTargRect, const QRect & aSrcRect)
{
    if(mTargRect != aTargRect || mSrcRect != aSrcRect)
    {
        setRectValues(aTargRect, aSrcRect);
    }
}

void NemuVHWASurfaceBase::setTargRectPosition(const QPoint & aPoint)
{
    QRect tRect = targRect();
    tRect.moveTopLeft(aPoint);
    setRects(tRect, srcRect());
}

void NemuVHWASurfaceBase::updateVisibility (NemuVHWASurfaceBase *pPrimary, const QRect & aVisibleTargRect, bool bNotIntersected, bool bForce)
{
    if(bForce || aVisibleTargRect.intersected(mTargRect) != mVisibleTargRect)
    {
        setVisibleRectValues(aVisibleTargRect);
    }

    mpPrimary = pPrimary;
    mbNotIntersected = bNotIntersected;

    initDisplay();
}

void NemuVHWASurfaceBase::initDisplay()
{
    if(mVisibleTargRect.isEmpty() || mVisibleSrcRect.isEmpty())
    {
        Assert(mVisibleTargRect.isEmpty() && mVisibleSrcRect.isEmpty());
        mImage->deleteDisplay();
        return;
    }

    int rc = mImage->initDisplay(mpPrimary ? mpPrimary->mImage : NULL, &mVisibleTargRect, &mVisibleSrcRect, getActiveDstOverlayCKey(mpPrimary), getActiveSrcOverlayCKey(), mbNotIntersected);
    AssertRC(rc);
}

void NemuVHWASurfaceBase::updatedMem(const QRect *rec)
{
    if(rec)
    {
        Assert(mRect.contains(*rec));
    }
    mUpdateMem2TexRect.add(*rec);
}

bool NemuVHWASurfaceBase::performDisplay(NemuVHWASurfaceBase *pPrimary, bool bForce)
{
    Assert(mImage->displayInitialized());

    if(mVisibleTargRect.isEmpty())
    {
        /* nothing to display, i.e. the surface is not visible,
         * in the sense that it's located behind the viewport ranges */
        Assert(mVisibleSrcRect.isEmpty());
        return false;
    }
    else
    {
        Assert(!mVisibleSrcRect.isEmpty());
    }

    bForce |= synchTexMem(&mVisibleSrcRect);

    const NemuVHWAColorKey * pDstCKey = getActiveDstOverlayCKey(pPrimary);
    if(pPrimary && pDstCKey)
    {
        bForce |= pPrimary->synchTexMem(&mVisibleTargRect);
    }

    if(!bForce)
        return false;

    mImage->display();

    Assert(bForce);
    return true;
}

class NemuVHWAGlProgramMngr * NemuVHWASurfaceBase::getGlProgramMngr()
{
    return mpImage->nemuVHWAGetGlProgramMngr();
}

class NemuGLContext : public QGLContext
{
public:
    NemuGLContext (const QGLFormat & format ) :
        QGLContext(format),
        mAllowDoneCurrent(true)
    {
    }

    void doneCurrent()
    {
        if(!mAllowDoneCurrent)
            return;
        QGLContext::doneCurrent();
    }

    bool isDoneCurrentAllowed() { return mAllowDoneCurrent; }
    void allowDoneCurrent(bool bAllow) { mAllowDoneCurrent = bAllow; }
private:
    bool mAllowDoneCurrent;
};


NemuGLWgt::NemuGLWgt(NemuVHWAImage * pImage,
            QWidget* parent, const QGLWidget* shareWidget)

        : QGLWidget(new NemuGLContext(shareWidget->format()), parent, shareWidget),
          mpImage(pImage)
{
    /* work-around to disable done current needed to old ATI drivers on Linux */
    NemuGLContext *pc = (NemuGLContext*)context();
    pc->allowDoneCurrent (false);
    Assert(isSharing());
}


NemuVHWAImage::NemuVHWAImage ()
    : mSurfHandleTable(128), /* 128 should be enough */
    mRepaintNeeded(false),
//    mbVGASurfCreated(false),
    mConstructingList(NULL),
    mcRemaining2Contruct(0),
    mSettings(NULL)
#ifdef NEMUVHWA_PROFILE_FPS
    ,
    mFPSCounter(64),
    mbNewFrame(false)
#endif
{
    mpMngr = new NemuVHWAGlProgramMngr();
//        /* No need for background drawing */
//        setAttribute (Qt::WA_OpaquePaintEvent);
}

int NemuVHWAImage::init(NemuVHWASettings *aSettings)
{
    mSettings = aSettings;
    return VINF_SUCCESS;
}

const QGLFormat & NemuVHWAImage::nemuGLFormat()
{
    static QGLFormat nemuFormat = QGLFormat();
    nemuFormat.setAlpha(true);
    Assert(nemuFormat.alpha());
    nemuFormat.setSwapInterval(0);
    Assert(nemuFormat.swapInterval() == 0);
    nemuFormat.setAccum(false);
    Assert(!nemuFormat.accum());
    nemuFormat.setDepth(false);
    Assert(!nemuFormat.depth());
//  nemuFormat.setRedBufferSize(8);
//  nemuFormat.setGreenBufferSize(8);
//  nemuFormat.setBlueBufferSize(8);
    return nemuFormat;
}


NemuVHWAImage::~NemuVHWAImage()
{
    delete mpMngr;
}

#ifdef NEMUVHWA_OLD_COORD
void NemuVHWAImage::doSetupMatrix(const QSize & aSize, bool bInverted)
{
    NEMUQGL_CHECKERR(
            glLoadIdentity();
            );
    if(bInverted)
    {
        NEMUQGL_CHECKERR(
                glScalef(1.0f/aSize.width(), 1.0f/aSize.height(), 1.0f);
                );
    }
    else
    {
        /* make display coordinates be scaled to pixel coordinates */
        NEMUQGL_CHECKERR(
                glTranslatef(0.0f, 1.0f, 0.0f);
                );
        NEMUQGL_CHECKERR(
                glScalef(1.0f/aSize.width(), 1.0f/aSize.height(), 1.0f);
                );
        NEMUQGL_CHECKERR(
                glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
                );
    }
}
#endif

void NemuVHWAImage::adjustViewport(const QSize &display, const QRect &viewport)
{
    glViewport(-viewport.x(),
               viewport.height() + viewport.y() - display.height(),
               display.width(),
               display.height());
}

void NemuVHWAImage::setupMatricies(const QSize &display, bool bInverted)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    if(bInverted)
        glOrtho(0., (GLdouble)display.width(), (GLdouble)display.height(), 0., -1., 1.);
    else
        glOrtho(0., (GLdouble)display.width(), 0., (GLdouble)display.height(), -1., 1.);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

int NemuVHWAImage::reset(VHWACommandList * pCmdList)
{
    NEMUVHWACMD * pCmd;
    const OverlayList & overlays = mDisplay.overlays();
    for (OverlayList::const_iterator oIt = overlays.begin();
            oIt != overlays.end(); ++ oIt)
    {
        NemuVHWASurfList * pSurfList = *oIt;
        if(pSurfList->current())
        {
            /* 1. hide overlay */
            pCmd = vhwaHHCmdCreate(NEMUVHWACMD_TYPE_SURF_OVERLAY_UPDATE, sizeof(NEMUVHWACMD_SURF_OVERLAY_UPDATE));
            NEMUVHWACMD_SURF_OVERLAY_UPDATE *pOUCmd = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_OVERLAY_UPDATE);
            pOUCmd->u.in.hSrcSurf = pSurfList->current()->handle();
            pOUCmd->u.in.flags = NEMUVHWA_OVER_HIDE;

            pCmdList->push_back(pCmd);
        }

        /* 2. destroy overlay */
        const SurfList & surfaces = pSurfList->surfaces();

        for (SurfList::const_iterator sIt = surfaces.begin();
                sIt != surfaces.end(); ++ sIt)
        {
            NemuVHWASurfaceBase *pCurSurf = (*sIt);
            pCmd = vhwaHHCmdCreate(NEMUVHWACMD_TYPE_SURF_DESTROY, sizeof(NEMUVHWACMD_SURF_DESTROY));
            NEMUVHWACMD_SURF_DESTROY *pSDCmd = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_DESTROY);
            pSDCmd->u.in.hSurf = pCurSurf->handle();

            pCmdList->push_back(pCmd);
        }
    }

    /* 3. destroy primaries */
    const SurfList & surfaces = mDisplay.primaries().surfaces();
    for (SurfList::const_iterator sIt = surfaces.begin();
            sIt != surfaces.end(); ++ sIt)
    {
        NemuVHWASurfaceBase *pCurSurf = (*sIt);
        if(pCurSurf->handle() != NEMUVHWA_SURFHANDLE_INVALID)
        {
            pCmd = vhwaHHCmdCreate(NEMUVHWACMD_TYPE_SURF_DESTROY, sizeof(NEMUVHWACMD_SURF_DESTROY));
            NEMUVHWACMD_SURF_DESTROY *pSDCmd = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_DESTROY);
            pSDCmd->u.in.hSurf = pCurSurf->handle();

            pCmdList->push_back(pCmd);
        }
    }

    return VINF_SUCCESS;
}

#ifdef NEMU_WITH_VIDEOHWACCEL
int NemuVHWAImage::vhwaSurfaceCanCreate(struct NEMUVHWACMD_SURF_CANCREATE *pCmd)
{
    NEMUQGLLOG_ENTER(("\n"));

    const NemuVHWAInfo & info = nemuVHWAGetSupportInfo(NULL);

    if(!(pCmd->SurfInfo.flags & NEMUVHWA_SD_CAPS))
    {
        AssertFailed();
        pCmd->u.out.ErrInfo = -1;
        return VINF_SUCCESS;
    }
#ifdef NEMUVHWA_ALLOW_PRIMARY_AND_OVERLAY_ONLY
    if(pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_OFFSCREENPLAIN)
    {
#ifdef DEBUGVHWASTRICT
        AssertFailed();
#endif
        pCmd->u.out.ErrInfo = -1;
        return VINF_SUCCESS;
    }
#endif

    if(pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_PRIMARYSURFACE)
    {
        if(pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_COMPLEX)
        {
#ifdef DEBUG_misha
            AssertFailed();
#endif
            pCmd->u.out.ErrInfo = -1;
        }
        else
        {
            pCmd->u.out.ErrInfo = 0;
        }
        return VINF_SUCCESS;
    }

#ifdef NEMUVHWA_ALLOW_PRIMARY_AND_OVERLAY_ONLY
    if ((pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_OVERLAY) == 0)
    {
#ifdef DEBUGVHWASTRICT
        AssertFailed();
#endif
        pCmd->u.out.ErrInfo = -1;
        return VINF_SUCCESS;
    }
#endif

    if (pCmd->u.in.bIsDifferentPixelFormat)
    {
        if (!(pCmd->SurfInfo.flags & NEMUVHWA_SD_PIXELFORMAT))
        {
            AssertFailed();
            pCmd->u.out.ErrInfo = -1;
            return VINF_SUCCESS;
        }

        if (pCmd->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_RGB)
        {
            if (pCmd->SurfInfo.PixelFormat.c.rgbBitCount != 32
                    && pCmd->SurfInfo.PixelFormat.c.rgbBitCount != 24)
            {
                AssertFailed();
                pCmd->u.out.ErrInfo = -1;
                return VINF_SUCCESS;
            }
        }
        else if (pCmd->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_FOURCC)
        {
            /* detect whether we support this format */
            bool bFound = mSettings->isSupported (info, pCmd->SurfInfo.PixelFormat.fourCC);

            if (!bFound)
            {
                NEMUQGLLOG (("!!unsupported fourcc!!!: %c%c%c%c\n",
                             (pCmd->SurfInfo.PixelFormat.fourCC & 0x000000ff),
                             (pCmd->SurfInfo.PixelFormat.fourCC & 0x0000ff00) >> 8,
                             (pCmd->SurfInfo.PixelFormat.fourCC & 0x00ff0000) >> 16,
                             (pCmd->SurfInfo.PixelFormat.fourCC & 0xff000000) >> 24
                             ));
                pCmd->u.out.ErrInfo = -1;
                return VINF_SUCCESS;
            }
        }
        else
        {
            AssertFailed();
            pCmd->u.out.ErrInfo = -1;
            return VINF_SUCCESS;
        }
    }

    pCmd->u.out.ErrInfo = 0;
    return VINF_SUCCESS;
}

int NemuVHWAImage::vhwaSurfaceCreate (struct NEMUVHWACMD_SURF_CREATE *pCmd)
{
    NEMUQGLLOG_ENTER (("\n"));

    uint32_t handle = NEMUVHWA_SURFHANDLE_INVALID;
    if(pCmd->SurfInfo.hSurf != NEMUVHWA_SURFHANDLE_INVALID)
    {
        handle = pCmd->SurfInfo.hSurf;
        if(mSurfHandleTable.get(handle))
        {
            AssertFailed();
            return VERR_GENERAL_FAILURE;
        }
    }

    NemuVHWASurfaceBase *surf = NULL;
    /* in case the Framebuffer is working in "not using VRAM" mode,
     * we need to report the pitch, etc. info of the form guest expects from us*/
    NemuVHWAColorFormat reportedFormat;
    /* paranoia to ensure the NemuVHWAColorFormat API works properly */
    Assert(!reportedFormat.isValid());
    bool bNoPBO = false;
    bool bPrimary = false;

    NemuVHWAColorKey *pDstBltCKey = NULL, DstBltCKey;
    NemuVHWAColorKey *pSrcBltCKey = NULL, SrcBltCKey;
    NemuVHWAColorKey *pDstOverlayCKey = NULL, DstOverlayCKey;
    NemuVHWAColorKey *pSrcOverlayCKey = NULL, SrcOverlayCKey;
    if(pCmd->SurfInfo.flags & NEMUVHWA_SD_CKDESTBLT)
    {
        DstBltCKey = NemuVHWAColorKey(pCmd->SurfInfo.DstBltCK.high, pCmd->SurfInfo.DstBltCK.low);
        pDstBltCKey = &DstBltCKey;
    }
    if(pCmd->SurfInfo.flags & NEMUVHWA_SD_CKSRCBLT)
    {
        SrcBltCKey = NemuVHWAColorKey(pCmd->SurfInfo.SrcBltCK.high, pCmd->SurfInfo.SrcBltCK.low);
        pSrcBltCKey = &SrcBltCKey;
    }
    if(pCmd->SurfInfo.flags & NEMUVHWA_SD_CKDESTOVERLAY)
    {
        DstOverlayCKey = NemuVHWAColorKey(pCmd->SurfInfo.DstOverlayCK.high, pCmd->SurfInfo.DstOverlayCK.low);
        pDstOverlayCKey = &DstOverlayCKey;
    }
    if(pCmd->SurfInfo.flags & NEMUVHWA_SD_CKSRCOVERLAY)
    {
        SrcOverlayCKey = NemuVHWAColorKey(pCmd->SurfInfo.SrcOverlayCK.high, pCmd->SurfInfo.SrcOverlayCK.low);
        pSrcOverlayCKey = &SrcOverlayCKey;
    }

    if (pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_PRIMARYSURFACE)
    {
        bNoPBO = true;
        bPrimary = true;
        NemuVHWASurfaceBase * pVga = vgaSurface();
#ifdef NEMU_WITH_WDDM
        uchar * addr = nemuVRAMAddressFromOffset(pCmd->SurfInfo.offSurface);
        Assert(addr);
        if (addr)
        {
            pVga->setAddress(addr);
        }
#endif

        Assert((pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_OFFSCREENPLAIN) == 0);

        reportedFormat = NemuVHWAColorFormat(pCmd->SurfInfo.PixelFormat.c.rgbBitCount,
                                    pCmd->SurfInfo.PixelFormat.m1.rgbRBitMask,
                                    pCmd->SurfInfo.PixelFormat.m2.rgbGBitMask,
                                    pCmd->SurfInfo.PixelFormat.m3.rgbBBitMask);

        if (pVga->handle() == NEMUVHWA_SURFHANDLE_INVALID
                && (pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_OFFSCREENPLAIN) == 0)
        {
            Assert(pCmd->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_RGB);
//            if(pCmd->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_RGB)
            {
                Assert(pCmd->SurfInfo.width == pVga->width());
                Assert(pCmd->SurfInfo.height == pVga->height());
//                if(pCmd->SurfInfo.width == pVga->width()
//                        && pCmd->SurfInfo.height == pVga->height())
                {
                    // the assert below is incorrect in case the Framebuffer is working in "not using VRAM" mode
//                    Assert(pVga->pixelFormat().equals(format));
//                    if(pVga->pixelFormat().equals(format))
                    {
                        surf = pVga;

                        surf->setDstBltCKey(pDstBltCKey);
                        surf->setSrcBltCKey(pSrcBltCKey);

                        surf->setDefaultDstOverlayCKey(pDstOverlayCKey);
                        surf->resetDefaultDstOverlayCKey();

                        surf->setDefaultSrcOverlayCKey(pSrcOverlayCKey);
                        surf->resetDefaultSrcOverlayCKey();
//                        mbVGASurfCreated = true;
                    }
                }
            }
        }
    }
    else if(pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_OFFSCREENPLAIN)
    {
        bNoPBO = true;
    }

    if(!surf)
    {
        NEMUVHWAIMG_TYPE fFlags = 0;
        if(!bNoPBO)
        {
            fFlags |= NEMUVHWAIMG_PBO | NEMUVHWAIMG_PBOIMG | NEMUVHWAIMG_LINEAR;
            if(mSettings->isStretchLinearEnabled())
                fFlags |= NEMUVHWAIMG_FBO;
        }

        QSize surfSize(pCmd->SurfInfo.width, pCmd->SurfInfo.height);
        QRect primaryRect = mDisplay.getPrimary()->rect();
        NemuVHWAColorFormat format;
        if (bPrimary)
            format = mDisplay.getVGA()->pixelFormat();
        else if (pCmd->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_RGB)
            format = NemuVHWAColorFormat(pCmd->SurfInfo.PixelFormat.c.rgbBitCount,
                                            pCmd->SurfInfo.PixelFormat.m1.rgbRBitMask,
                                            pCmd->SurfInfo.PixelFormat.m2.rgbGBitMask,
                                            pCmd->SurfInfo.PixelFormat.m3.rgbBBitMask);
        else if (pCmd->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_FOURCC)
            format = NemuVHWAColorFormat(pCmd->SurfInfo.PixelFormat.fourCC);
        else
            AssertBreakpoint();

        if (format.isValid())
        {
            surf = new NemuVHWASurfaceBase(this,
                        surfSize,
                        primaryRect,
                        QRect(0, 0, surfSize.width(), surfSize.height()),
                        mViewport,
                        format,
                        pSrcBltCKey, pDstBltCKey, pSrcOverlayCKey, pDstOverlayCKey,
#ifdef NEMUVHWA_USE_TEXGROUP
                        0,
#endif
                        fFlags);
        }
        else
        {
            AssertBreakpoint();
            NEMUQGLLOG_EXIT(("pSurf (0x%p)\n",surf));
            return VERR_GENERAL_FAILURE;
        }

        uchar * addr = nemuVRAMAddressFromOffset(pCmd->SurfInfo.offSurface);
        surf->init(mDisplay.getPrimary(), addr);

        if(pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_OVERLAY)
        {
#ifdef DEBUG_misha
            Assert(!bNoPBO);
#endif

            if(!mConstructingList)
            {
                mConstructingList = new NemuVHWASurfList();
                mcRemaining2Contruct = pCmd->SurfInfo.cBackBuffers+1;
                mDisplay.addOverlay(mConstructingList);
            }

            mConstructingList->add(surf);
            mcRemaining2Contruct--;
            if(!mcRemaining2Contruct)
            {
                mConstructingList = NULL;
            }
        }
        else
        {
            NemuVHWASurfaceBase * pVga = vgaSurface();
            Assert(pVga->handle() != NEMUVHWA_SURFHANDLE_INVALID);
            Assert(pVga != surf); NOREF(pVga);
            mDisplay.getVGA()->getComplexList()->add(surf);
#ifdef DEBUGVHWASTRICT
            Assert(pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_VISIBLE);
#endif
            if(bPrimary)
            {
                Assert(surf->getComplexList() == mDisplay.getVGA()->getComplexList());
                surf->getComplexList()->setCurrentVisible(surf);
                mDisplay.updateVGA(surf);
            }
        }
    }
    else
    {
        Assert(pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_PRIMARYSURFACE);
    }

    Assert(mDisplay.getVGA() == mDisplay.getPrimary());

    /* tell the guest how we think the memory is organized */
    NEMUQGLLOG(("bps: %d\n", surf->bitsPerPixel()));

    if (!reportedFormat.isValid())
    {
        pCmd->SurfInfo.pitch = surf->bytesPerLine();
        pCmd->SurfInfo.sizeX = surf->memSize();
        pCmd->SurfInfo.sizeY = 1;
    }
    else
    {
        /* this is the case of Framebuffer not using Guest VRAM */
        /* can happen for primary surface creation only */
        Assert(pCmd->SurfInfo.surfCaps & NEMUVHWA_SCAPS_PRIMARYSURFACE);
        pCmd->SurfInfo.pitch = (reportedFormat.bitsPerPixel() * surf->width() + 7) / 8;
        /* we support only RGB case now, otherwise we would need more complicated mechanism of memsize calculation */
        Assert(!reportedFormat.fourcc());
        pCmd->SurfInfo.sizeX = (reportedFormat.bitsPerPixel() * surf->width() + 7) / 8 * surf->height();
        pCmd->SurfInfo.sizeY = 1;
    }

    if(handle != NEMUVHWA_SURFHANDLE_INVALID)
    {
        bool bSuccess = mSurfHandleTable.mapPut(handle, surf);
        Assert(bSuccess);
        if(!bSuccess)
        {
            /* @todo: this is very bad, should not be here */
            return VERR_GENERAL_FAILURE;
        }
    }
    else
    {
        /* tell the guest our handle */
        handle = mSurfHandleTable.put(surf);
        pCmd->SurfInfo.hSurf = (NEMUVHWA_SURFHANDLE)handle;
    }

    Assert(handle != NEMUVHWA_SURFHANDLE_INVALID);
    Assert(surf->handle() == NEMUVHWA_SURFHANDLE_INVALID);
    surf->setHandle(handle);
    Assert(surf->handle() == handle);

    NEMUQGLLOG_EXIT(("pSurf (0x%p)\n",surf));

    return VINF_SUCCESS;
}

#ifdef NEMU_WITH_WDDM
int NemuVHWAImage::vhwaSurfaceGetInfo(struct NEMUVHWACMD_SURF_GETINFO *pCmd)
{
    NemuVHWAColorFormat format;
    Assert(!format.isValid());
    if (pCmd->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_RGB)
        format = NemuVHWAColorFormat(pCmd->SurfInfo.PixelFormat.c.rgbBitCount,
                                        pCmd->SurfInfo.PixelFormat.m1.rgbRBitMask,
                                        pCmd->SurfInfo.PixelFormat.m2.rgbGBitMask,
                                        pCmd->SurfInfo.PixelFormat.m3.rgbBBitMask);
    else if (pCmd->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_FOURCC)
        format = NemuVHWAColorFormat(pCmd->SurfInfo.PixelFormat.fourCC);
    else
        AssertBreakpoint();

    Assert(format.isValid());
    if (format.isValid())
    {
        pCmd->SurfInfo.pitch = NemuVHWATextureImage::calcBytesPerLine(format, pCmd->SurfInfo.width);
        pCmd->SurfInfo.sizeX = NemuVHWATextureImage::calcMemSize(format,
                pCmd->SurfInfo.width, pCmd->SurfInfo.height);
        pCmd->SurfInfo.sizeY = 1;
        return VINF_SUCCESS;
    }
    return VERR_INVALID_PARAMETER;
}
#endif
int NemuVHWAImage::vhwaSurfaceDestroy(struct NEMUVHWACMD_SURF_DESTROY *pCmd)
{
    NemuVHWASurfaceBase *pSurf = handle2Surface(pCmd->u.in.hSurf);
    NemuVHWASurfList *pList = pSurf->getComplexList();
    Assert(pSurf->handle() != NEMUVHWA_SURFHANDLE_INVALID);

    NEMUQGLLOG_ENTER(("pSurf (0x%x)\n",pSurf));
    if(pList != mDisplay.getVGA()->getComplexList())
    {
        Assert(pList);
        pList->remove(pSurf);
        if(pList->surfaces().empty())
        {
            mDisplay.removeOverlay(pList);
            if(pList == mConstructingList)
            {
                mConstructingList = NULL;
                mcRemaining2Contruct = 0;
            }
            delete pList;
        }

        delete(pSurf);
    }
    else
    {
        Assert(pList);
        Assert(pList->size() >= 1);
        if(pList->size() > 1)
        {
            if(pSurf == mDisplay.getVGA())
            {
                const SurfList & surfaces = pList->surfaces();

                for (SurfList::const_iterator it = surfaces.begin();
                         it != surfaces.end(); ++ it)
                {
                    NemuVHWASurfaceBase *pCurSurf = (*it);
                    Assert(pCurSurf);
                    if (pCurSurf != pSurf)
                    {
                        mDisplay.updateVGA(pCurSurf);
                        pList->setCurrentVisible(pCurSurf);
                        break;
                    }
                }
            }

            pList->remove(pSurf);
            delete(pSurf);
        }
        else
        {
            pSurf->setHandle(NEMUVHWA_SURFHANDLE_INVALID);
        }
    }

    /* just in case we destroy a visible overlay surface */
    mRepaintNeeded = true;

    void * test = mSurfHandleTable.remove(pCmd->u.in.hSurf);
    Assert(test); NOREF(test);

    return VINF_SUCCESS;
}

#define NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_RB(_pr) \
             QRect((_pr)->left,                     \
                 (_pr)->top,                        \
                 (_pr)->right - (_pr)->left + 1,    \
                 (_pr)->bottom - (_pr)->top + 1)

#define NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(_pr) \
             QRect((_pr)->left,                     \
                 (_pr)->top,                        \
                 (_pr)->right - (_pr)->left,        \
                 (_pr)->bottom - (_pr)->top)

int NemuVHWAImage::vhwaSurfaceLock(struct NEMUVHWACMD_SURF_LOCK *pCmd)
{
    NemuVHWASurfaceBase *pSurf = handle2Surface(pCmd->u.in.hSurf);
    NEMUQGLLOG_ENTER(("pSurf (0x%x)\n",pSurf));
    nemuCheckUpdateAddress (pSurf, pCmd->u.in.offSurface);
    if (pCmd->u.in.rectValid)
    {
        QRect r = NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.rect);
        return pSurf->lock(&r, pCmd->u.in.flags);
    }
    return pSurf->lock(NULL, pCmd->u.in.flags);
}

int NemuVHWAImage::vhwaSurfaceUnlock(struct NEMUVHWACMD_SURF_UNLOCK *pCmd)
{
    NemuVHWASurfaceBase *pSurf = handle2Surface(pCmd->u.in.hSurf);
#ifdef DEBUG_misha
    /* for performance reasons we should receive unlock for visible surfaces only
     * other surfaces receive unlock only once becoming visible, e.g. on DdFlip
     * Ensure this is so*/
    if(pSurf != mDisplay.getPrimary())
    {
        const OverlayList & overlays = mDisplay.overlays();
        bool bFound = false;

        if(!mDisplay.isPrimary(pSurf))
        {
            for (OverlayList::const_iterator it = overlays.begin();
                 it != overlays.end(); ++ it)
            {
                NemuVHWASurfList * pSurfList = *it;
                if(pSurfList->current() == pSurf)
                {
                    bFound = true;
                    break;
                }
            }

            Assert(bFound);
        }

//        Assert(bFound);
    }
#endif
    NEMUQGLLOG_ENTER(("pSurf (0x%x)\n",pSurf));
    if(pCmd->u.in.xUpdatedMemValid)
    {
        QRect r = NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.xUpdatedMemRect);
        pSurf->updatedMem(&r);
    }

    return pSurf->unlock();
}

int NemuVHWAImage::vhwaSurfaceBlt(struct NEMUVHWACMD_SURF_BLT *pCmd)
{
    Q_UNUSED(pCmd);
    return VERR_NOT_IMPLEMENTED;
}

int NemuVHWAImage::vhwaSurfaceFlip(struct NEMUVHWACMD_SURF_FLIP *pCmd)
{
    NemuVHWASurfaceBase *pTargSurf = handle2Surface(pCmd->u.in.hTargSurf);
    NemuVHWASurfaceBase *pCurrSurf = handle2Surface(pCmd->u.in.hCurrSurf);
    NEMUQGLLOG_ENTER(("pTargSurf (0x%x), pCurrSurf (0x%x)\n",pTargSurf,pCurrSurf));
    nemuCheckUpdateAddress (pCurrSurf, pCmd->u.in.offCurrSurface);
    nemuCheckUpdateAddress (pTargSurf, pCmd->u.in.offTargSurface);

    if(pCmd->u.in.xUpdatedTargMemValid)
    {
        QRect r = NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.xUpdatedTargMemRect);
        pTargSurf->updatedMem(&r);
    }
    pTargSurf->getComplexList()->setCurrentVisible(pTargSurf);

    mRepaintNeeded = true;
#ifdef DEBUG
    pCurrSurf->cFlipsCurr++;
    pTargSurf->cFlipsTarg++;
#endif

    return VINF_SUCCESS;
}

int NemuVHWAImage::vhwaSurfaceColorFill(struct NEMUVHWACMD_SURF_COLORFILL *pCmd)
{
    NOREF(pCmd);
    return VERR_NOT_IMPLEMENTED;
}

void NemuVHWAImage::vhwaDoSurfaceOverlayUpdate(NemuVHWASurfaceBase *pDstSurf, NemuVHWASurfaceBase *pSrcSurf, struct NEMUVHWACMD_SURF_OVERLAY_UPDATE *pCmd)
{
    if(pCmd->u.in.flags & NEMUVHWA_OVER_KEYDEST)
    {
        NEMUQGLLOG((", KEYDEST"));
        /* we use src (overlay) surface to maintain overridden dst ckey info
         * to allow multiple overlays have different overridden dst keys for one primary surface */
        /* non-null dstOverlayCKey for overlay would mean the overlay surface contains the overridden
         * dst ckey value in defaultDstOverlayCKey
         * this allows the NULL to be a valid overridden value as well
         *  i.e.
         * 1. indicate the value is NUL overridden, just set NULL*/
        pSrcSurf->setOverriddenDstOverlayCKey(NULL);
    }
    else if(pCmd->u.in.flags & NEMUVHWA_OVER_KEYDESTOVERRIDE)
    {
        NEMUQGLLOG((", KEYDESTOVERRIDE"));
        /* we use src (overlay) surface to maintain overridden dst ckey info
         * to allow multiple overlays have different overridden dst keys for one primary surface */
        /* non-null dstOverlayCKey for overlay would mean the overlay surface contains the overridden
         * dst ckey value in defaultDstOverlayCKey
         * this allows the NULL to be a valid overridden value as well
         *  i.e.
         * 1. indicate the value is overridden (no matter what we write here, bu it should be not NULL)*/
        NemuVHWAColorKey ckey(pCmd->u.in.desc.DstCK.high, pCmd->u.in.desc.DstCK.low);
        NEMUQGLLOG_CKEY(" ckey: ",&ckey, "\n");
        pSrcSurf->setOverriddenDstOverlayCKey(&ckey);
        /* tell the ckey is enabled */
        pSrcSurf->setDefaultDstOverlayCKey(&ckey);
    }
    else
    {
        NEMUQGLLOG((", no KEYDEST"));
        /* we use src (overlay) surface to maintain overridden dst ckey info
         * to allow multiple overlays have different overridden dst keys for one primary surface */
        /* non-null dstOverlayCKey for overlay would mean the overlay surface contains the overridden
         * dst ckey value in defaultDstOverlayCKey
         * this allows the NULL to be a valid overridden value as well
         * i.e.
         * 1. indicate the value is overridden (no matter what we write here, bu it should be not NULL)*/
        NemuVHWAColorKey dummyCKey(0, 0);
        pSrcSurf->setOverriddenDstOverlayCKey(&dummyCKey);
        /* tell the ckey is disabled */
        pSrcSurf->setDefaultDstOverlayCKey(NULL);
    }

    if(pCmd->u.in.flags & NEMUVHWA_OVER_KEYSRC)
    {
        NEMUQGLLOG((", KEYSRC"));
        pSrcSurf->resetDefaultSrcOverlayCKey();
    }
    else if(pCmd->u.in.flags & NEMUVHWA_OVER_KEYSRCOVERRIDE)
    {
        NEMUQGLLOG((", KEYSRCOVERRIDE"));
        NemuVHWAColorKey ckey(pCmd->u.in.desc.SrcCK.high, pCmd->u.in.desc.SrcCK.low);
        pSrcSurf->setOverriddenSrcOverlayCKey(&ckey);
    }
    else
    {
        NEMUQGLLOG((", no KEYSRC"));
        pSrcSurf->setOverriddenSrcOverlayCKey(NULL);
    }
    NEMUQGLLOG(("\n"));
    if(pDstSurf)
    {
        QRect dstRect = NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.dstRect);
        QRect srcRect = NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.srcRect);

        NEMUQGLLOG(("*******overlay update*******\n"));
        NEMUQGLLOG(("dstSurfSize: w(%d), h(%d)\n", pDstSurf->width(), pDstSurf->height()));
        NEMUQGLLOG(("srcSurfSize: w(%d), h(%d)\n", pSrcSurf->width(), pSrcSurf->height()));
        NEMUQGLLOG_QRECT("dstRect:", &dstRect, "\n");
        NEMUQGLLOG_QRECT("srcRect:", &srcRect, "\n");

        pSrcSurf->setPrimary(pDstSurf);

        pSrcSurf->setRects(dstRect, srcRect);
    }
}

int NemuVHWAImage::vhwaSurfaceOverlayUpdate(struct NEMUVHWACMD_SURF_OVERLAY_UPDATE *pCmd)
{
    NemuVHWASurfaceBase *pSrcSurf = handle2Surface(pCmd->u.in.hSrcSurf);
    NemuVHWASurfList *pList = pSrcSurf->getComplexList();
    nemuCheckUpdateAddress (pSrcSurf, pCmd->u.in.offSrcSurface);
    NEMUQGLLOG(("OverlayUpdate: pSrcSurf (0x%x)\n",pSrcSurf));
    NemuVHWASurfaceBase *pDstSurf = NULL;

    if(pCmd->u.in.hDstSurf)
    {
        pDstSurf = handle2Surface(pCmd->u.in.hDstSurf);
        nemuCheckUpdateAddress (pDstSurf, pCmd->u.in.offDstSurface);
        NEMUQGLLOG(("pDstSurf (0x%x)\n",pDstSurf));
#ifdef DEBUGVHWASTRICT
        Assert(pDstSurf == mDisplay.getVGA());
        Assert(mDisplay.getVGA() == mDisplay.getPrimary());
#endif
        Assert(pDstSurf->getComplexList() == mDisplay.getVGA()->getComplexList());

        if(pCmd->u.in.flags & NEMUVHWA_OVER_SHOW)
        {
            if(pDstSurf != mDisplay.getPrimary())
            {
                mDisplay.updateVGA(pDstSurf);
                pDstSurf->getComplexList()->setCurrentVisible(pDstSurf);
            }
        }
    }

#ifdef NEMU_WITH_WDDM
    if(pCmd->u.in.xFlags & NEMUVHWACMD_SURF_OVERLAY_UPDATE_F_SRCMEMRECT)
    {
        QRect r = NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.xUpdatedSrcMemRect);
        pSrcSurf->updatedMem(&r);
    }
    if(pCmd->u.in.xFlags & NEMUVHWACMD_SURF_OVERLAY_UPDATE_F_DSTMEMRECT)
    {
        QRect r = NEMUVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.xUpdatedDstMemRect);
        pDstSurf->updatedMem(&r);
    }
#endif

    const SurfList & surfaces = pList->surfaces();

    for (SurfList::const_iterator it = surfaces.begin();
             it != surfaces.end(); ++ it)
    {
        NemuVHWASurfaceBase *pCurSrcSurf = (*it);
        vhwaDoSurfaceOverlayUpdate(pDstSurf, pCurSrcSurf, pCmd);
    }

    if(pCmd->u.in.flags & NEMUVHWA_OVER_HIDE)
    {
        NEMUQGLLOG(("hide\n"));
        pList->setCurrentVisible(NULL);
    }
    else if(pCmd->u.in.flags & NEMUVHWA_OVER_SHOW)
    {
        NEMUQGLLOG(("show\n"));
        pList->setCurrentVisible(pSrcSurf);
    }

    mRepaintNeeded = true;

    return VINF_SUCCESS;
}

int NemuVHWAImage::vhwaSurfaceOverlaySetPosition(struct NEMUVHWACMD_SURF_OVERLAY_SETPOSITION *pCmd)
{
    NemuVHWASurfaceBase *pDstSurf = handle2Surface(pCmd->u.in.hDstSurf);
    NemuVHWASurfaceBase *pSrcSurf = handle2Surface(pCmd->u.in.hSrcSurf);

    NEMUQGLLOG_ENTER(("pDstSurf (0x%x), pSrcSurf (0x%x)\n",pDstSurf,pSrcSurf));

    nemuCheckUpdateAddress (pSrcSurf, pCmd->u.in.offSrcSurface);
    nemuCheckUpdateAddress (pDstSurf, pCmd->u.in.offDstSurface);

    NemuVHWASurfList *pList = pSrcSurf->getComplexList();
    const SurfList & surfaces = pList->surfaces();

    QPoint pos(pCmd->u.in.xPos, pCmd->u.in.yPos);

#ifdef DEBUGVHWASTRICT
    Assert(pDstSurf == mDisplay.getVGA());
    Assert(mDisplay.getVGA() == mDisplay.getPrimary());
#endif
    if (pSrcSurf->getComplexList()->current() != NULL)
    {
        Assert(pDstSurf);
        if (pDstSurf != mDisplay.getPrimary())
        {
            mDisplay.updateVGA(pDstSurf);
            pDstSurf->getComplexList()->setCurrentVisible(pDstSurf);
        }
    }

    mRepaintNeeded = true;

    for (SurfList::const_iterator it = surfaces.begin();
             it != surfaces.end(); ++ it)
    {
        NemuVHWASurfaceBase *pCurSrcSurf = (*it);
        pCurSrcSurf->setTargRectPosition(pos);
    }

    return VINF_SUCCESS;
}

int NemuVHWAImage::vhwaSurfaceColorkeySet(struct NEMUVHWACMD_SURF_COLORKEY_SET *pCmd)
{
    NemuVHWASurfaceBase *pSurf = handle2Surface(pCmd->u.in.hSurf);

    NEMUQGLLOG_ENTER(("pSurf (0x%x)\n",pSurf));

    nemuCheckUpdateAddress (pSurf, pCmd->u.in.offSurface);

    mRepaintNeeded = true;

    if (pCmd->u.in.flags & NEMUVHWA_CKEY_DESTBLT)
    {
        NemuVHWAColorKey ckey(pCmd->u.in.CKey.high, pCmd->u.in.CKey.low);
        pSurf->setDstBltCKey(&ckey);
    }
    if (pCmd->u.in.flags & NEMUVHWA_CKEY_DESTOVERLAY)
    {
        NemuVHWAColorKey ckey(pCmd->u.in.CKey.high, pCmd->u.in.CKey.low);
        pSurf->setDefaultDstOverlayCKey(&ckey);
    }
    if (pCmd->u.in.flags & NEMUVHWA_CKEY_SRCBLT)
    {
        NemuVHWAColorKey ckey(pCmd->u.in.CKey.high, pCmd->u.in.CKey.low);
        pSurf->setSrcBltCKey(&ckey);

    }
    if (pCmd->u.in.flags & NEMUVHWA_CKEY_SRCOVERLAY)
    {
        NemuVHWAColorKey ckey(pCmd->u.in.CKey.high, pCmd->u.in.CKey.low);
        pSurf->setDefaultSrcOverlayCKey(&ckey);
    }

    return VINF_SUCCESS;
}

int NemuVHWAImage::vhwaQueryInfo1(struct NEMUVHWACMD_QUERYINFO1 *pCmd)
{
    NEMUQGLLOG_ENTER(("\n"));
    bool bEnabled = false;
    const NemuVHWAInfo & info = nemuVHWAGetSupportInfo(NULL);
    if(info.isVHWASupported())
    {
        Assert(pCmd->u.in.guestVersion.maj == NEMUVHWA_VERSION_MAJ);
        if(pCmd->u.in.guestVersion.maj == NEMUVHWA_VERSION_MAJ)
        {
            Assert(pCmd->u.in.guestVersion.min == NEMUVHWA_VERSION_MIN);
            if(pCmd->u.in.guestVersion.min == NEMUVHWA_VERSION_MIN)
            {
                Assert(pCmd->u.in.guestVersion.bld == NEMUVHWA_VERSION_BLD);
                if(pCmd->u.in.guestVersion.bld == NEMUVHWA_VERSION_BLD)
                {
                    Assert(pCmd->u.in.guestVersion.reserved == NEMUVHWA_VERSION_RSV);
                    if(pCmd->u.in.guestVersion.reserved == NEMUVHWA_VERSION_RSV)
                    {
                        bEnabled = true;
                    }
                }
            }
        }
    }

    memset(pCmd, 0, sizeof(NEMUVHWACMD_QUERYINFO1));
    if(bEnabled)
    {
        pCmd->u.out.cfgFlags = NEMUVHWA_CFG_ENABLED;

        pCmd->u.out.caps =
                    /* we do not support blitting for now */
//                        NEMUVHWA_CAPS_BLT | NEMUVHWA_CAPS_BLTSTRETCH | NEMUVHWA_CAPS_BLTQUEUE
//                                 | NEMUVHWA_CAPS_BLTCOLORFILL not supported, although adding it is trivial
//                                 | NEMUVHWA_CAPS_BLTFOURCC set below if shader support is available
                                 NEMUVHWA_CAPS_OVERLAY
                                 | NEMUVHWA_CAPS_OVERLAYSTRETCH
                                 | NEMUVHWA_CAPS_OVERLAYCANTCLIP
                                 // | NEMUVHWA_CAPS_OVERLAYFOURCC set below if shader support is available
                                 ;

        /* @todo: check if we could use DDSCAPS_ALPHA instead of colorkeying */

        pCmd->u.out.caps2 = NEMUVHWA_CAPS2_CANRENDERWINDOWED
                                    | NEMUVHWA_CAPS2_WIDESURFACES;

        //TODO: setup stretchCaps
        pCmd->u.out.stretchCaps = 0;

        pCmd->u.out.numOverlays = 1;
        /* TODO: set curOverlays properly */
        pCmd->u.out.curOverlays = 0;

        pCmd->u.out.surfaceCaps =
                            NEMUVHWA_SCAPS_PRIMARYSURFACE
#ifndef NEMUVHWA_ALLOW_PRIMARY_AND_OVERLAY_ONLY
                            | NEMUVHWA_SCAPS_OFFSCREENPLAIN
#endif
                            | NEMUVHWA_SCAPS_FLIP
                            | NEMUVHWA_SCAPS_LOCALVIDMEM
                            | NEMUVHWA_SCAPS_OVERLAY
                    //        | NEMUVHWA_SCAPS_BACKBUFFER
                    //        | NEMUVHWA_SCAPS_FRONTBUFFER
                    //        | NEMUVHWA_SCAPS_VIDEOMEMORY
                    //        | NEMUVHWA_SCAPS_COMPLEX
                    //        | NEMUVHWA_SCAPS_VISIBLE
                            ;

        if(info.getGlInfo().isFragmentShaderSupported() && info.getGlInfo().getMultiTexNumSupported() >= 2)
        {
            pCmd->u.out.caps |= NEMUVHWA_CAPS_COLORKEY
                            | NEMUVHWA_CAPS_COLORKEYHWASSIST
                            ;

            pCmd->u.out.colorKeyCaps =
//                          NEMUVHWA_CKEYCAPS_DESTBLT | NEMUVHWA_CKEYCAPS_DESTBLTCLRSPACE | NEMUVHWA_CKEYCAPS_SRCBLT| NEMUVHWA_CKEYCAPS_SRCBLTCLRSPACE |
//                          NEMUVHWA_CKEYCAPS_SRCOVERLAY | NEMUVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE |
                            NEMUVHWA_CKEYCAPS_DESTOVERLAY          |
                            NEMUVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE;
                            ;

            if(info.getGlInfo().isTextureRectangleSupported())
            {
                pCmd->u.out.caps |= NEMUVHWA_CAPS_OVERLAYFOURCC
//                              | NEMUVHWA_CAPS_BLTFOURCC
                                ;

                pCmd->u.out.colorKeyCaps |=
//                               NEMUVHWA_CKEYCAPS_SRCOVERLAYYUV |
                                 NEMUVHWA_CKEYCAPS_DESTOVERLAYYUV;
                                 ;

//              pCmd->u.out.caps2 |= NEMUVHWA_CAPS2_COPYFOURCC;

                pCmd->u.out.numFourCC = mSettings->getIntersection(info, 0, NULL);
            }
        }
    }

    return VINF_SUCCESS;
}

int NemuVHWAImage::vhwaQueryInfo2(struct NEMUVHWACMD_QUERYINFO2 *pCmd)
{
    NEMUQGLLOG_ENTER(("\n"));

    const NemuVHWAInfo & info = nemuVHWAGetSupportInfo(NULL);
    uint32_t aFourcc[NEMUVHWA_NUMFOURCC];
    int num = mSettings->getIntersection(info, NEMUVHWA_NUMFOURCC, aFourcc);
    Assert(pCmd->numFourCC >= (uint32_t)num);
    if(pCmd->numFourCC < (uint32_t)num)
        return VERR_GENERAL_FAILURE;

    pCmd->numFourCC = (uint32_t)num;
    memcpy(pCmd->FourCC, aFourcc, num*sizeof(aFourcc[0]));
    return VINF_SUCCESS;
}

//static DECLCALLBACK(void) nemuQGLSaveExec(PSSMHANDLE pSSM, void *pvUser)
//{
//    NemuVHWAImage * pw = (NemuVHWAImage*)pvUser;
//    pw->vhwaSaveExec(pSSM);
//}
//
//static DECLCALLBACK(int) nemuQGLLoadExec(PSSMHANDLE pSSM, void *pvUser, uint32_t u32Version, uint32_t uPass)
//{
//    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
//    NemuVHWAImage * pw = (NemuVHWAImage*)pvUser;
//    return NemuVHWAImage::vhwaLoadExec(&pw->onResizeCmdList(), pSSM, u32Version);
//}

int NemuVHWAImage::vhwaSaveSurface(struct SSMHANDLE * pSSM, NemuVHWASurfaceBase *pSurf, uint32_t surfCaps)
{
    NEMUQGL_SAVE_SURFSTART(pSSM);

    uint64_t u64 = nemuVRAMOffset(pSurf);
    int rc;
    rc = SSMR3PutU32(pSSM, pSurf->handle());         AssertRC(rc);
    rc = SSMR3PutU64(pSSM, u64);         AssertRC(rc);
    rc = SSMR3PutU32(pSSM, pSurf->width());         AssertRC(rc);
    rc = SSMR3PutU32(pSSM, pSurf->height());         AssertRC(rc);
    rc = SSMR3PutU32(pSSM, surfCaps);         AssertRC(rc);
    uint32_t flags = 0;
    const NemuVHWAColorKey * pDstBltCKey = pSurf->dstBltCKey();
    const NemuVHWAColorKey * pSrcBltCKey = pSurf->srcBltCKey();
    const NemuVHWAColorKey * pDstOverlayCKey = pSurf->dstOverlayCKey();
    const NemuVHWAColorKey * pSrcOverlayCKey = pSurf->srcOverlayCKey();
    if(pDstBltCKey)
    {
        flags |= NEMUVHWA_SD_CKDESTBLT;
    }
    if(pSrcBltCKey)
    {
        flags |= NEMUVHWA_SD_CKSRCBLT;
    }
    if(pDstOverlayCKey)
    {
        flags |= NEMUVHWA_SD_CKDESTOVERLAY;
    }
    if(pSrcOverlayCKey)
    {
        flags |= NEMUVHWA_SD_CKSRCOVERLAY;
    }

    rc = SSMR3PutU32(pSSM, flags);         AssertRC(rc);
    if(pDstBltCKey)
    {
        rc = SSMR3PutU32(pSSM, pDstBltCKey->lower());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, pDstBltCKey->upper());         AssertRC(rc);
    }
    if(pSrcBltCKey)
    {
        rc = SSMR3PutU32(pSSM, pSrcBltCKey->lower());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, pSrcBltCKey->upper());         AssertRC(rc);
    }
    if(pDstOverlayCKey)
    {
        rc = SSMR3PutU32(pSSM, pDstOverlayCKey->lower());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, pDstOverlayCKey->upper());         AssertRC(rc);
    }
    if(pSrcOverlayCKey)
    {
        rc = SSMR3PutU32(pSSM, pSrcOverlayCKey->lower());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, pSrcOverlayCKey->upper());         AssertRC(rc);
    }

    const NemuVHWAColorFormat & format = pSurf->pixelFormat();
    flags = 0;
    if(format.fourcc())
    {
        flags |= NEMUVHWA_PF_FOURCC;
        rc = SSMR3PutU32(pSSM, flags);         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.fourcc());         AssertRC(rc);
    }
    else
    {
        flags |= NEMUVHWA_PF_RGB;
        rc = SSMR3PutU32(pSSM, flags);         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.bitsPerPixel());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.r().mask());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.g().mask());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.b().mask());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.a().mask());         AssertRC(rc);
    }

    NEMUQGL_SAVE_SURFSTOP(pSSM);

    return rc;
}

int NemuVHWAImage::vhwaLoadSurface(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t cBackBuffers, uint32_t u32Version)
{
    Q_UNUSED(u32Version);

    NEMUQGL_LOAD_SURFSTART(pSSM);

    char *buf = (char*)malloc(NEMUVHWACMD_SIZE(NEMUVHWACMD_SURF_CREATE));
    memset(buf, 0, sizeof(NEMUVHWACMD_SIZE(NEMUVHWACMD_SURF_CREATE)));
    NEMUVHWACMD * pCmd = (NEMUVHWACMD*)buf;
    pCmd->enmCmd = NEMUVHWACMD_TYPE_SURF_CREATE;
    pCmd->Flags = NEMUVHWACMD_FLAG_HH_CMD;

    NEMUVHWACMD_SURF_CREATE * pCreateSurf = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_CREATE);
    int rc;
    uint32_t u32;
    rc = SSMR3GetU32(pSSM, &u32);         AssertRC(rc);
    pCreateSurf->SurfInfo.hSurf = (NEMUVHWA_SURFHANDLE)u32;
    if(RT_SUCCESS(rc))
    {
        rc = SSMR3GetU64(pSSM, &pCreateSurf->SurfInfo.offSurface);         AssertRC(rc);
        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.width);         AssertRC(rc);
        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.height);         AssertRC(rc);
        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.surfCaps);         AssertRC(rc);
        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.flags);         AssertRC(rc);
        if(pCreateSurf->SurfInfo.flags & NEMUVHWA_SD_CKDESTBLT)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.DstBltCK.low);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.DstBltCK.high);         AssertRC(rc);
        }
        if(pCreateSurf->SurfInfo.flags & NEMUVHWA_SD_CKSRCBLT)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.SrcBltCK.low);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.SrcBltCK.high);         AssertRC(rc);
        }
        if(pCreateSurf->SurfInfo.flags & NEMUVHWA_SD_CKDESTOVERLAY)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.DstOverlayCK.low);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.DstOverlayCK.high);         AssertRC(rc);
        }
        if(pCreateSurf->SurfInfo.flags & NEMUVHWA_SD_CKSRCOVERLAY)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.SrcOverlayCK.low);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.SrcOverlayCK.high);         AssertRC(rc);
        }

        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.flags);         AssertRC(rc);
        if(pCreateSurf->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_RGB)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.c.rgbBitCount);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.m1.rgbRBitMask);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.m2.rgbGBitMask);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.m3.rgbBBitMask);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.m4.rgbABitMask);         AssertRC(rc);
        }
        else if(pCreateSurf->SurfInfo.PixelFormat.flags & NEMUVHWA_PF_FOURCC)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.fourCC);         AssertRC(rc);
        }
        else
        {
            AssertFailed();
        }

        if(cBackBuffers)
        {
            pCreateSurf->SurfInfo.cBackBuffers = cBackBuffers;
            pCreateSurf->SurfInfo.surfCaps |= NEMUVHWA_SCAPS_COMPLEX;
        }

        pCmdList->push_back(pCmd);
//        nemuExecOnResize(&NemuVHWAImage::nemuDoVHWACmdAndFree, pCmd); AssertRC(rc);
//        if(RT_SUCCESS(rc))
//        {
//            rc = pCmd->rc;
//            AssertRC(rc);
//        }
    }
    else
        free(buf);

    NEMUQGL_LOAD_SURFSTOP(pSSM);

    return rc;
}

int NemuVHWAImage::vhwaSaveOverlayData(struct SSMHANDLE * pSSM, NemuVHWASurfaceBase *pSurf, bool bVisible)
{
    NEMUQGL_SAVE_OVERLAYSTART(pSSM);

    uint32_t flags = 0;
    const NemuVHWAColorKey * dstCKey = pSurf->dstOverlayCKey();
    const NemuVHWAColorKey * defaultDstCKey = pSurf->defaultDstOverlayCKey();
    const NemuVHWAColorKey * srcCKey = pSurf->srcOverlayCKey();;
    const NemuVHWAColorKey * defaultSrcCKey = pSurf->defaultSrcOverlayCKey();
    bool bSaveDstCKey = false;
    bool bSaveSrcCKey = false;

    if(bVisible)
    {
        flags |= NEMUVHWA_OVER_SHOW;
    }
    else
    {
        flags |= NEMUVHWA_OVER_HIDE;
    }

    if(!dstCKey)
    {
        flags |= NEMUVHWA_OVER_KEYDEST;
    }
    else if(defaultDstCKey)
    {
        flags |= NEMUVHWA_OVER_KEYDESTOVERRIDE;
        bSaveDstCKey = true;
    }

    if(srcCKey == defaultSrcCKey)
    {
        flags |= NEMUVHWA_OVER_KEYSRC;
    }
    else if(srcCKey)
    {
        flags |= NEMUVHWA_OVER_KEYSRCOVERRIDE;
        bSaveSrcCKey = true;
    }

    int rc = SSMR3PutU32(pSSM, flags); AssertRC(rc);

    rc = SSMR3PutU32(pSSM, mDisplay.getPrimary()->handle()); AssertRC(rc);
    rc = SSMR3PutU32(pSSM, pSurf->handle()); AssertRC(rc);

    if(bSaveDstCKey)
    {
        rc = SSMR3PutU32(pSSM, dstCKey->lower()); AssertRC(rc);
        rc = SSMR3PutU32(pSSM, dstCKey->upper()); AssertRC(rc);
    }
    if(bSaveSrcCKey)
    {
        rc = SSMR3PutU32(pSSM, srcCKey->lower()); AssertRC(rc);
        rc = SSMR3PutU32(pSSM, srcCKey->upper()); AssertRC(rc);
    }

    int x1, x2, y1, y2;
    pSurf->targRect().getCoords(&x1, &y1, &x2, &y2);
    rc = SSMR3PutS32(pSSM, x1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, x2+1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, y1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, y2+1); AssertRC(rc);

    pSurf->srcRect().getCoords(&x1, &y1, &x2, &y2);
    rc = SSMR3PutS32(pSSM, x1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, x2+1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, y1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, y2+1); AssertRC(rc);

    NEMUQGL_SAVE_OVERLAYSTOP(pSSM);

    return rc;
}

int NemuVHWAImage::vhwaLoadOverlayData(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t u32Version)
{
    Q_UNUSED(u32Version);

    NEMUQGL_LOAD_OVERLAYSTART(pSSM);

    char *buf = new char[NEMUVHWACMD_SIZE(NEMUVHWACMD_SURF_CREATE)];
    memset(buf, 0, NEMUVHWACMD_SIZE(NEMUVHWACMD_SURF_CREATE));
    NEMUVHWACMD * pCmd = (NEMUVHWACMD*)buf;
    pCmd->enmCmd = NEMUVHWACMD_TYPE_SURF_OVERLAY_UPDATE;
    pCmd->Flags = NEMUVHWACMD_FLAG_HH_CMD;

    NEMUVHWACMD_SURF_OVERLAY_UPDATE * pUpdateOverlay = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_OVERLAY_UPDATE);
    int rc;

    rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.flags); AssertRC(rc);
    uint32_t hSrc, hDst;
    rc = SSMR3GetU32(pSSM, &hDst); AssertRC(rc);
    rc = SSMR3GetU32(pSSM, &hSrc); AssertRC(rc);
    pUpdateOverlay->u.in.hSrcSurf = hSrc;
    pUpdateOverlay->u.in.hDstSurf = hDst;
    {
        pUpdateOverlay->u.in.offDstSurface = NEMUVHWA_OFFSET64_VOID;
        pUpdateOverlay->u.in.offSrcSurface = NEMUVHWA_OFFSET64_VOID;

        if(pUpdateOverlay->u.in.flags & NEMUVHWA_OVER_KEYDESTOVERRIDE)
        {
            rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.desc.DstCK.low); AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.desc.DstCK.high); AssertRC(rc);
        }

        if(pUpdateOverlay->u.in.flags & NEMUVHWA_OVER_KEYSRCOVERRIDE)
        {
            rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.desc.SrcCK.low); AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.desc.SrcCK.high); AssertRC(rc);
        }

        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.dstRect.left); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.dstRect.right); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.dstRect.top); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.dstRect.bottom); AssertRC(rc);

        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.srcRect.left); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.srcRect.right); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.srcRect.top); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.srcRect.bottom); AssertRC(rc);

        pCmdList->push_back(pCmd);
    }

    NEMUQGL_LOAD_OVERLAYSTOP(pSSM);

    return rc;
}

void NemuVHWAImage::vhwaSaveExecVoid(struct SSMHANDLE * pSSM)
{
    NEMUQGL_SAVE_START(pSSM);
    int rc = SSMR3PutU32(pSSM, 0);         AssertRC(rc); /* 0 primaries */
    NEMUQGL_SAVE_STOP(pSSM);
}

void NemuVHWAImage::vhwaSaveExec(struct SSMHANDLE * pSSM)
{
    NEMUQGL_SAVE_START(pSSM);

    /* the mechanism of restoring data is based on generating VHWA commands that restore the surfaces state
     * the following commands are generated:
     * I.    CreateSurface
     * II.   UpdateOverlay
     *
     * Data format is the following:
     * I.    u32 - Num primary surfaces - (the current frontbuffer is detected in the stored surf flags which are posted to the generated CreateSurface cmd)
     * II.   for each primary surf
     * II.1    generate & execute CreateSurface cmd (see below on the generation logic)
     * III.  u32 - Num overlays
     * IV.   for each overlay
     * IV.1    u32 - Num surfaces in overlay (the current frontbuffer is detected in the stored surf flags which are posted to the generated CreateSurface cmd)
     * IV.2    for each surface in overlay
     * IV.2.a    generate & execute CreateSurface cmd (see below on the generation logic)
     * IV.2.b    generate & execute UpdateOverlay cmd (see below on the generation logic)
     *
     */
    const SurfList & primaryList = mDisplay.primaries().surfaces();
    uint32_t cPrimary = (uint32_t)primaryList.size();
    if(cPrimary &&
            (mDisplay.getVGA() == NULL || mDisplay.getVGA()->handle() == NEMUVHWA_SURFHANDLE_INVALID))
    {
        cPrimary -= 1;
    }

    int rc = SSMR3PutU32(pSSM, cPrimary);         AssertRC(rc);
    if(cPrimary)
    {
        for (SurfList::const_iterator pr = primaryList.begin();
             pr != primaryList.end(); ++ pr)
        {
            NemuVHWASurfaceBase *pSurf = *pr;
    //        bool bVga = (pSurf == mDisplay.getVGA());
            bool bVisible = (pSurf == mDisplay.getPrimary());
            uint32_t flags = NEMUVHWA_SCAPS_PRIMARYSURFACE;
            if(bVisible)
                flags |= NEMUVHWA_SCAPS_VISIBLE;

            if(pSurf->handle() != NEMUVHWA_SURFHANDLE_INVALID)
            {
                rc = vhwaSaveSurface(pSSM, *pr, flags);    AssertRC(rc);
#ifdef DEBUG
                --cPrimary;
                Assert(cPrimary < UINT32_MAX / 2);
#endif
            }
            else
            {
                Assert(pSurf == mDisplay.getVGA());
            }
        }

#ifdef DEBUG
        Assert(!cPrimary);
#endif

        const OverlayList & overlays = mDisplay.overlays();
        rc = SSMR3PutU32(pSSM, (uint32_t)overlays.size());         AssertRC(rc);

        for (OverlayList::const_iterator it = overlays.begin();
             it != overlays.end(); ++ it)
        {
            NemuVHWASurfList * pSurfList = *it;
            const SurfList & surfaces = pSurfList->surfaces();
            uint32_t cSurfs = (uint32_t)surfaces.size();
            uint32_t flags = NEMUVHWA_SCAPS_OVERLAY;
            if(cSurfs > 1)
                flags |= NEMUVHWA_SCAPS_COMPLEX;
            rc = SSMR3PutU32(pSSM, cSurfs);         AssertRC(rc);
            for (SurfList::const_iterator sit = surfaces.begin();
                 sit != surfaces.end(); ++ sit)
            {
                rc = vhwaSaveSurface(pSSM, *sit, flags);    AssertRC(rc);
            }

            bool bVisible = true;
            NemuVHWASurfaceBase * pOverlayData = pSurfList->current();
            if(!pOverlayData)
            {
                pOverlayData = surfaces.front();
                bVisible = false;
            }

            rc = vhwaSaveOverlayData(pSSM, pOverlayData, bVisible);    AssertRC(rc);
        }
    }

    NEMUQGL_SAVE_STOP(pSSM);
}

int NemuVHWAImage::vhwaLoadVHWAEnable(VHWACommandList * pCmdList)
{
    char *buf = (char*)malloc(sizeof(NEMUVHWACMD));
    Assert(buf);
    if(buf)
    {
        memset(buf, 0, sizeof(NEMUVHWACMD));
        NEMUVHWACMD * pCmd = (NEMUVHWACMD*)buf;
        pCmd->enmCmd = NEMUVHWACMD_TYPE_ENABLE;
        pCmd->Flags = NEMUVHWACMD_FLAG_HH_CMD;
        pCmdList->push_back(pCmd);
        return VINF_SUCCESS;
    }

    return VERR_OUT_OF_RESOURCES;
}

int NemuVHWAImage::vhwaLoadExec(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t u32Version)
{
    NEMUQGL_LOAD_START(pSSM);

    if(u32Version > NEMUQGL_STATE_VERSION)
        return VERR_VERSION_MISMATCH;

    int rc;
    uint32_t u32;

    rc = vhwaLoadVHWAEnable(pCmdList); AssertRC(rc);
    if(RT_SUCCESS(rc))
    {
        rc = SSMR3GetU32(pSSM, &u32); AssertRC(rc);
        if(RT_SUCCESS(rc))
        {
            if(u32Version == 1U && u32 == (~0U)) /* work around the v1 bug */
                u32 = 0;
            if(u32)
            {
                for(uint32_t i = 0; i < u32; ++i)
                {
                    rc = vhwaLoadSurface(pCmdList, pSSM, 0, u32Version);  AssertRC(rc);
                    if(RT_FAILURE(rc))
                        break;
                }

                if(RT_SUCCESS(rc))
                {
                    rc = SSMR3GetU32(pSSM, &u32); AssertRC(rc);
                    if(RT_SUCCESS(rc))
                    {
                        for(uint32_t i = 0; i < u32; ++i)
                        {
                            uint32_t cSurfs;
                            rc = SSMR3GetU32(pSSM, &cSurfs); AssertRC(rc);
                            for(uint32_t j = 0; j < cSurfs; ++j)
                            {
                                rc = vhwaLoadSurface(pCmdList, pSSM, cSurfs - 1, u32Version);  AssertRC(rc);
                                if(RT_FAILURE(rc))
                                    break;
                            }

                            if(RT_SUCCESS(rc))
                            {
                                rc = vhwaLoadOverlayData(pCmdList, pSSM, u32Version);  AssertRC(rc);
                            }

                            if(RT_FAILURE(rc))
                            {
                                break;
                            }
                        }
                    }
                }
            }
#ifdef NEMUQGL_STATE_DEBUG
            else if(u32Version == 1) /* read the 0 overlay count to ensure the following NEMUQGL_LOAD_STOP succeeds */
            {
                rc = SSMR3GetU32(pSSM, &u32); AssertRC(rc);
                Assert(u32 == 0);
            }
#endif
        }
    }

    NEMUQGL_LOAD_STOP(pSSM);

    return rc;
}

int NemuVHWAImage::vhwaConstruct(struct NEMUVHWACMD_HH_CONSTRUCT *pCmd)
{
//    PVM pVM = (PVM)pCmd->pVM;
//    uint32_t intsId = 0; /* @todo: set the proper id */
//
//    char nameFuf[sizeof(NEMUQGL_STATE_NAMEBASE) + 8];
//
//    char * pszName = nameFuf;
//    sprintf(pszName, "%s%d", NEMUQGL_STATE_NAMEBASE, intsId);
//    int rc = SSMR3RegisterExternal(
//            pVM,                    /* The VM handle*/
//            pszName,                /* Data unit name. */
//            intsId,                 /* The instance identifier of the data unit.
//                                     * This must together with the name be unique. */
//            NEMUQGL_STATE_VERSION,   /* Data layout version number. */
//            128,             /* The approximate amount of data in the unit.
//                              * Only for progress indicators. */
//            NULL, NULL, NULL, /* pfnLiveXxx */
//            NULL,            /* Prepare save callback, optional. */
//            nemuQGLSaveExec, /* Execute save callback, optional. */
//            NULL,            /* Done save callback, optional. */
//            NULL,            /* Prepare load callback, optional. */
//            nemuQGLLoadExec, /* Execute load callback, optional. */
//            NULL,            /* Done load callback, optional. */
//            this             /* User argument. */
//            );
//    AssertRC(rc);
    mpvVRAM = pCmd->pvVRAM;
    mcbVRAM = pCmd->cbVRAM;
    return VINF_SUCCESS;
}

uchar * NemuVHWAImage::nemuVRAMAddressFromOffset(uint64_t offset)
{
    /* @todo: check vramSize() */
    return (offset != NEMUVHWA_OFFSET64_VOID) ? ((uint8_t*)vramBase()) + offset : NULL;
}

uint64_t NemuVHWAImage::nemuVRAMOffsetFromAddress(uchar* addr)
{
    return uint64_t(addr - ((uchar*)vramBase()));
}

uint64_t NemuVHWAImage::nemuVRAMOffset(NemuVHWASurfaceBase * pSurf)
{
    return pSurf->addressAlocated() ? NEMUVHWA_OFFSET64_VOID : nemuVRAMOffsetFromAddress(pSurf->address());
}

#endif

#ifdef NEMUQGL_DBG_SURF

int g_iCur = 0;
NemuVHWASurfaceBase * g_apSurf[] = {NULL, NULL, NULL};

void NemuVHWAImage::nemuDoTestSurfaces(void* context)
{
    if(g_iCur >= RT_ELEMENTS(g_apSurf))
        g_iCur = 0;
    NemuVHWASurfaceBase * pSurf1 = g_apSurf[g_iCur];
    if(pSurf1)
    {
        pSurf1->getComplexList()->setCurrentVisible(pSurf1);
    }
}
#endif

void NemuVHWAImage::nemuDoUpdateViewport(const QRect & aRect)
{
    adjustViewport(mDisplay.getPrimary()->size(), aRect);
    mViewport = aRect;

    const SurfList & primaryList = mDisplay.primaries().surfaces();

    for (SurfList::const_iterator pr = primaryList.begin();
         pr != primaryList.end(); ++ pr)
    {
        NemuVHWASurfaceBase *pSurf = *pr;
        pSurf->updateVisibility(NULL, aRect, false, false);
    }

    const OverlayList & overlays = mDisplay.overlays();
    QRect overInter = overlaysRectIntersection();
    overInter = overInter.intersect(aRect);

    bool bDisplayPrimary = true;

    for (OverlayList::const_iterator it = overlays.begin();
         it != overlays.end(); ++ it)
    {
        NemuVHWASurfList * pSurfList = *it;
        const SurfList & surfaces = pSurfList->surfaces();
        if(surfaces.size())
        {
            bool bNotIntersected = !overInter.isEmpty() && surfaces.front()->targRect().contains(overInter);
            Assert(bNotIntersected);

            bDisplayPrimary &= !bNotIntersected;
            for (SurfList::const_iterator sit = surfaces.begin();
                 sit != surfaces.end(); ++ sit)
            {
                NemuVHWASurfaceBase *pSurf = *sit;
                pSurf->updateVisibility(mDisplay.getPrimary(), aRect, bNotIntersected, false);
            }
        }
    }

    Assert(!bDisplayPrimary);
    mDisplay.setDisplayPrimary(bDisplayPrimary);
}

bool NemuVHWAImage::hasSurfaces() const
{
    if (mDisplay.overlays().size() != 0)
        return true;
    if (mDisplay.primaries().size() > 1)
        return true;
    /* in case gl was never turned on, we have no surfaces at all including VGA */
    if (!mDisplay.getVGA())
        return false;
    return mDisplay.getVGA()->handle() != NEMUVHWA_SURFHANDLE_INVALID;
}

bool NemuVHWAImage::hasVisibleOverlays()
{
    const OverlayList & overlays = mDisplay.overlays();
    for (OverlayList::const_iterator it = overlays.begin();
         it != overlays.end(); ++ it)
    {
        NemuVHWASurfList * pSurfList = *it;
        if(pSurfList->current() != NULL)
            return true;
    }
    return false;
}

QRect NemuVHWAImage::overlaysRectUnion()
{
    const OverlayList & overlays = mDisplay.overlays();
    NemuVHWADirtyRect un;
    for (OverlayList::const_iterator it = overlays.begin();
         it != overlays.end(); ++ it)
    {
        NemuVHWASurfaceBase * pOverlay = (*it)->current();
        if(pOverlay != NULL)
        {
            un.add(pOverlay->targRect());
        }
    }
    return un.toRect();
}

QRect NemuVHWAImage::overlaysRectIntersection()
{
    const OverlayList & overlays = mDisplay.overlays();
    QRect rect;
    NemuVHWADirtyRect un;
    for (OverlayList::const_iterator it = overlays.begin();
         it != overlays.end(); ++ it)
    {
        NemuVHWASurfaceBase * pOverlay = (*it)->current();
        if(pOverlay != NULL)
        {
            if(rect.isNull())
            {
                rect = pOverlay->targRect();
            }
            else
            {
                rect = rect.intersected(pOverlay->targRect());
                if(rect.isNull())
                    break;
            }
        }
    }
    return rect;
}

void NemuVHWAImage::nemuDoUpdateRect(const QRect * pRect)
{
    mDisplay.getPrimary()->updatedMem(pRect);
}

void NemuVHWAImage::resize(const NemuFBSizeInfo & size)
{
    NEMUQGL_CHECKERR(
            nemuglActiveTexture(GL_TEXTURE0);
        );

    bool remind = false;
    bool fallback = false;

    NEMUQGLLOG(("resizing: fmt=%d, vram=%p, bpp=%d, bpl=%d, width=%d, height=%d\n",
                      size.pixelFormat(), size.VRAM(),
                      size.bitsPerPixel(), size.bytesPerLine(),
                      size.width(), size.height()));

    /* clean the old values first */

    ulong bytesPerLine;
    uint32_t bitsPerPixel;
    uint32_t b = 0xff, g = 0xff00, r = 0xff0000;
    bool bUsesGuestVram;

    /* check if we support the pixel format and can use the guest VRAM directly */
    if (size.pixelFormat() == KBitmapFormat_BGR)
    {

        bitsPerPixel = size.bitsPerPixel();
        bytesPerLine = size.bytesPerLine();
        ulong bitsPerLine = bytesPerLine * 8;

        switch (bitsPerPixel)
        {
            case 32:
                break;
            case 24:
#ifdef DEBUG_misha
                AssertFailed();
#endif
                break;
            case 8:
#ifdef DEBUG_misha
                AssertFailed();
#endif
                g = b = 0;
                remind = true;
                break;
            case 1:
#ifdef DEBUG_misha
                AssertFailed();
#endif
                r = 1;
                g = b = 0;
                remind = true;
                break;
            default:
#ifdef DEBUG_misha
                AssertFailed();
#endif
                remind = true;
                fallback = true;
                break;
        }

        if (!fallback)
        {
            /* QImage only supports 32-bit aligned scan lines... */
            Assert ((size.bytesPerLine() & 3) == 0);
            fallback = ((size.bytesPerLine() & 3) != 0);
            Assert(!fallback);
        }
        if (!fallback)
        {
            /* ...and the scan lines ought to be a whole number of pixels. */
            Assert ((bitsPerLine & (size.bitsPerPixel() - 1)) == 0);
            fallback = ((bitsPerLine & (size.bitsPerPixel() - 1)) != 0);
            Assert(!fallback);
        }
        if (!fallback)
        {
            // ulong virtWdt = bitsPerLine / size.bitsPerPixel();
            bUsesGuestVram = true;
        }
    }
    else
    {
        AssertBreakpoint();
        fallback = true;
    }

    if (fallback)
    {
        /* we should never come to fallback more now */
        AssertBreakpoint();
        /* we don't support either the pixel format or the color depth,
         * fallback to a self-provided 32bpp RGB buffer */
        bitsPerPixel = 32;
        b = 0xff;
        g = 0xff00;
        r = 0xff0000;
        bytesPerLine = size.width()*bitsPerPixel/8;
        bUsesGuestVram = false;
    }

    ulong bytesPerPixel = bitsPerPixel/8;
    const QSize scaledSize = size.scaledSize();
    const ulong displayWidth = scaledSize.isValid() ? scaledSize.width() : bytesPerLine / bytesPerPixel;
    const ulong displayHeight = scaledSize.isValid() ? scaledSize.height() : size.height();

#ifdef NEMUQGL_DBG_SURF
    for(int i = 0; i < RT_ELEMENTS(g_apSurf); i++)
    {
        NemuVHWASurfaceBase * pSurf1 = g_apSurf[i];
        if(pSurf1)
        {
            NemuVHWASurfList *pConstructingList = pSurf1->getComplexList();
            delete pSurf1;
            if(pConstructingList)
                delete pConstructingList;
        }
    }
#endif

    NemuVHWASurfaceBase * pDisplay = mDisplay.setVGA(NULL);
    if(pDisplay)
        delete pDisplay;

    NemuVHWAColorFormat format(bitsPerPixel, r,g,b);
    QSize dispSize(displayWidth, displayHeight);
    QRect dispRect(0, 0, displayWidth, displayHeight);
    pDisplay = new NemuVHWASurfaceBase(this,
            dispSize,
            dispRect,
            dispRect,
            dispRect, /* we do not know viewport at the stage of precise, set as a disp rect, it will be updated on repaint */
            format,
            (NemuVHWAColorKey*)NULL, (NemuVHWAColorKey*)NULL, (NemuVHWAColorKey*)NULL, (NemuVHWAColorKey*)NULL,
#ifdef NEMUVHWA_USE_TEXGROUP
            0,
#endif
            0 /* NEMUVHWAIMG_TYPE fFlags */);
    pDisplay->init(NULL, bUsesGuestVram ? size.VRAM() : NULL);
    mDisplay.setVGA(pDisplay);
//    NEMUQGLLOG(("\n\n*******\n\n     viewport size is: (%d):(%d)\n\n*******\n\n", size().width(), size().height()));
    mViewport = QRect(0,0,displayWidth, displayHeight);
    adjustViewport(dispSize, mViewport);
    setupMatricies(dispSize, true);

#ifdef NEMUQGL_DBG_SURF
    {
        uint32_t width = 100;
        uint32_t height = 60;

        for(int i = 0; i < RT_ELEMENTS(g_apSurf); i++)
        {
            NemuVHWAColorFormat tmpFormat(FOURCC_YV12);
            QSize tmpSize(width, height) ;
            NemuVHWASurfaceBase *pSurf1 = new NemuVHWASurfaceBase(this, tmpSize,
                             mDisplay.getPrimary()->rect(),
                             QRect(0, 0, width, height),
                             mViewport,
                             tmpFormat,
                             NULL, NULL, NULL, &NemuVHWAColorKey(0,0),
#ifdef NEMUVHWA_USE_TEXGROUP
                             0,
#endif
                             false);

            Assert(mDisplay.getVGA());
            pSurf1->init(mDisplay.getVGA(), NULL);
            uchar *addr = pSurf1->address();
            uchar cur = 0;
            for(uint32_t k = 0; k < width*height; k++)
            {
                addr[k] = cur;
                cur+=64;
            }
            pSurf1->updatedMem(&QRect(0,0,width, height));

            NemuVHWASurfList *pConstructingList = new NemuVHWASurfList();
            mDisplay.addOverlay(pConstructingList);
            pConstructingList->add(pSurf1);
            g_apSurf[i] = pSurf1;

        }

        NEMUVHWACMD_SURF_OVERLAY_UPDATE updateCmd;
        memset(&updateCmd, 0, sizeof(updateCmd));
        updateCmd.u.in.hSrcSurf = (NEMUVHWA_SURFHANDLE)g_apSurf[0];
        updateCmd.u.in.hDstSurf = (NEMUVHWA_SURFHANDLE)pDisplay;
        updateCmd.u.in.flags =
                NEMUVHWA_OVER_SHOW
                | NEMUVHWA_OVER_KEYDESTOVERRIDE;

        updateCmd.u.in.desc.DstCK.high = 1;
        updateCmd.u.in.desc.DstCK.low = 1;

        updateCmd.u.in.dstRect.left = 0;
        updateCmd.u.in.dstRect.right = pDisplay->width();
        updateCmd.u.in.dstRect.top = (pDisplay->height() - height)/2;
        updateCmd.u.in.dstRect.bottom = updateCmd.u.in.dstRect.top + height;

        updateCmd.u.in.srcRect.left = 0;
        updateCmd.u.in.srcRect.right = width;
        updateCmd.u.in.srcRect.top = 0;
        updateCmd.u.in.srcRect.bottom = height;

        updateCmd.u.in.offDstSurface = NEMUVHWA_OFFSET64_VOID; /* just a magic to avoid surf mem buffer change  */
        updateCmd.u.in.offSrcSurface = NEMUVHWA_OFFSET64_VOID; /* just a magic to avoid surf mem buffer change  */

        vhwaSurfaceOverlayUpdate(&updateCmd);
    }
#endif

//    if(!mOnResizeCmdList.empty())
//    {
//        for (VHWACommandList::const_iterator it = mOnResizeCmdList.begin();
//             it != mOnResizeCmdList.end(); ++ it)
//        {
//            NEMUVHWACMD * pCmd = (*it);
//            nemuDoVHWACmdExec(pCmd);
//            free(pCmd);
//        }
//        mOnResizeCmdList.clear();
//    }

    if (remind)
        popupCenter().remindAboutWrongColorDepth(nemuGlobal().activeMachineWindow(), size.bitsPerPixel(), 32);
    else
        popupCenter().forgetAboutWrongColorDepth(nemuGlobal().activeMachineWindow());
}

NemuVHWAColorFormat::NemuVHWAColorFormat (uint32_t bitsPerPixel, uint32_t r, uint32_t g, uint32_t b) :
    mWidthCompression (1),
    mHeightCompression (1)
{
    init (bitsPerPixel, r, g, b);
}

NemuVHWAColorFormat::NemuVHWAColorFormat (uint32_t fourcc) :
    mWidthCompression (1),
    mHeightCompression (1)
{
    init (fourcc);
}

void NemuVHWAColorFormat::init (uint32_t fourcc)
{
    mDataFormat = fourcc;
    mInternalFormat = GL_RGBA8;//GL_RGB;
    mFormat = GL_BGRA_EXT;//GL_RGBA;
    mType = GL_UNSIGNED_BYTE;
    mR = NemuVHWAColorComponent (0xff);
    mG = NemuVHWAColorComponent (0xff);
    mB = NemuVHWAColorComponent (0xff);
    mA = NemuVHWAColorComponent (0xff);
    mBitsPerPixelTex = 32;

    switch(fourcc)
    {
        case FOURCC_AYUV:
            mBitsPerPixel = 32;
            mWidthCompression = 1;
            break;
        case FOURCC_UYVY:
        case FOURCC_YUY2:
            mBitsPerPixel = 16;
            mWidthCompression = 2;
            break;
        case FOURCC_YV12:
            mBitsPerPixel = 8;
            mWidthCompression = 4;
            break;
        default:
            AssertFailed();
            mBitsPerPixel = 0;
            mBitsPerPixelTex = 0;
            mWidthCompression = 0;
            break;
    }
}

void NemuVHWAColorFormat::init (uint32_t bitsPerPixel, uint32_t r, uint32_t g, uint32_t b)
{
    mBitsPerPixel = bitsPerPixel;
    mBitsPerPixelTex = bitsPerPixel;
    mDataFormat = 0;
    switch (bitsPerPixel)
    {
        case 32:
            mInternalFormat = GL_RGB;//3;//GL_RGB;
            mFormat = GL_BGRA_EXT;//GL_RGBA;
            mType = GL_UNSIGNED_BYTE;
            mR = NemuVHWAColorComponent (r);
            mG = NemuVHWAColorComponent (g);
            mB = NemuVHWAColorComponent (b);
            break;
        case 24:
#ifdef DEBUG_misha
            AssertFailed();
#endif
            mInternalFormat = 3;//GL_RGB;
            mFormat = GL_BGR_EXT;
            mType = GL_UNSIGNED_BYTE;
            mR = NemuVHWAColorComponent (r);
            mG = NemuVHWAColorComponent (g);
            mB = NemuVHWAColorComponent (b);
            break;
        case 16:
#ifdef DEBUG_misha
            AssertFailed();
#endif
            mInternalFormat = GL_RGB5;
            mFormat = GL_BGR_EXT;
            mType = GL_UNSIGNED_BYTE; /* TODO" ??? */
            mR = NemuVHWAColorComponent (r);
            mG = NemuVHWAColorComponent (g);
            mB = NemuVHWAColorComponent (b);
            break;
        case 8:
#ifdef DEBUG_misha
            AssertFailed();
#endif
            mInternalFormat = 1;//GL_RGB;
            mFormat = GL_RED;//GL_RGB;
            mType = GL_UNSIGNED_BYTE;
            mR = NemuVHWAColorComponent (0xff);
            break;
        case 1:
#ifdef DEBUG_misha
            AssertFailed();
#endif
            mInternalFormat = 1;
            mFormat = GL_COLOR_INDEX;
            mType = GL_BITMAP;
            mR = NemuVHWAColorComponent (0x1);
            break;
        default:
#ifdef DEBUG_misha
            AssertFailed();
#endif
            mBitsPerPixel = 0;
            mBitsPerPixelTex = 0;
            break;
    }
}

bool NemuVHWAColorFormat::equals (const NemuVHWAColorFormat & other) const
{
    if(fourcc())
        return fourcc() == other.fourcc();
    if(other.fourcc())
        return false;

    return bitsPerPixel() == other.bitsPerPixel();
}

NemuVHWAColorComponent::NemuVHWAColorComponent (uint32_t aMask)
{
    unsigned f = ASMBitFirstSetU32 (aMask);
    if(f)
    {
        mOffset = f - 1;
        f = ASMBitFirstSetU32 (~(aMask >> mOffset));
        if(f)
        {
            mcBits = f - 1;
        }
        else
        {
            mcBits = 32 - mOffset;
        }

        Assert (mcBits);
        mMask = (((uint32_t)0xffffffff) >> (32 - mcBits)) << mOffset;
        Assert (mMask == aMask);

        mRange = (mMask >> mOffset) + 1;
    }
    else
    {
        mMask = 0;
        mRange = 0;
        mOffset = 32;
        mcBits = 0;
    }
}

void NemuVHWAColorFormat::pixel2Normalized (uint32_t pix, float *r, float *g, float *b) const
{
    *r = mR.colorValNorm (pix);
    *g = mG.colorValNorm (pix);
    *b = mB.colorValNorm (pix);
}

NemuQGLOverlay::NemuQGLOverlay ()
    : mpOverlayWgt (NULL),
      mpViewport (NULL),
      mGlOn (false),
      mOverlayWidgetVisible (false),
      mOverlayVisible (false),
      mGlCurrent (false),
      mProcessingCommands (false),
      mNeedOverlayRepaint (false),
      mNeedSetVisible (false),
      mCmdPipe (),
      mSettings (),
      mpSession(),
      mpShareWgt (NULL),
      m_id(0)
{
    /* postpone the gl widget initialization to avoid conflict with 3D on Mac */
}

void NemuQGLOverlay::init(QWidget *pViewport, QObject *pPostEventObject,  CSession * aSession, uint32_t id)
{
    mpViewport = pViewport;
    mpSession = aSession;
    m_id = id;
    mSettings.init(*aSession);
    mCmdPipe.init(pPostEventObject);
}

class NemuGLShareWgt : public QGLWidget
{
public:
    NemuGLShareWgt() :
        QGLWidget(new NemuGLContext(NemuVHWAImage::nemuGLFormat()))
    {
        /* work-around to disable done current needed to old ATI drivers on Linux */
        NemuGLContext *pc = (NemuGLContext*)context();
        pc->allowDoneCurrent (false);
    }

protected:
    void initializeGL()
    {
        nemuVHWAGetSupportInfo(context());
        NemuVHWASurfaceBase::globalInit();
    }
};
void NemuQGLOverlay::initGl()
{
    if(mpOverlayWgt)
    {
        Assert(mpShareWgt);
        return;
    }

    if (!mpShareWgt)
    {
        mpShareWgt = new NemuGLShareWgt();
        /* force initializeGL */
        mpShareWgt->updateGL();
    }

    mOverlayImage.init(&mSettings);
    mpOverlayWgt = new NemuGLWgt(&mOverlayImage, mpViewport, mpShareWgt);

    mOverlayWidgetVisible = true; /* to ensure it is set hidden with nemuShowOverlay */
    nemuShowOverlay (false);

    mpOverlayWgt->setMouseTracking (true);
}

void NemuQGLOverlay::updateAttachment(QWidget *pViewport, QObject *pPostEventObject)
{
    if (mpViewport != pViewport)
    {
        mpViewport = pViewport;
        mpOverlayWgt = NULL;
        mOverlayWidgetVisible = false;
        if (mOverlayImage.hasSurfaces())
        {
//            Assert(!mOverlayVisible);
            if (pViewport)
            {
                initGl();
//            nemuDoCheckUpdateViewport();
            }
//            Assert(!mOverlayVisible);
        }
        mGlCurrent = false;
    }
    mCmdPipe.setNotifyObject(pPostEventObject);
}

int NemuQGLOverlay::reset()
{
    CDisplay display = mpSession->GetConsole().GetDisplay();
    Assert (!display.isNull());

    mCmdPipe.reset(&display);

    resetGl();

    return VINF_SUCCESS;
}

static DECLCALLBACK(void) vbvaVHWAHHCommandFreeCmd(void * pContext)
{
    free(pContext);
}

int NemuQGLOverlay::resetGl()
{
    VHWACommandList list;
    int rc = mOverlayImage.reset(&list);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        for (VHWACommandList::const_iterator sIt = list.begin();
                    sIt != list.end(); ++ sIt)
        {
            NEMUVHWACMD *pCmd = (*sIt);
            NEMUVHWA_HH_CALLBACK_SET(pCmd, vbvaVHWAHHCommandFreeCmd, pCmd);
            mCmdPipe.postCmd(NEMUVHWA_PIPECMD_VHWA, pCmd);
        }
    }
    return VINF_SUCCESS;
}

int NemuQGLOverlay::onVHWACommand(struct NEMUVHWACMD * pCmd)
{
    Log(("VHWA Command >>> %#p, %d\n", pCmd, pCmd->enmCmd));
    switch(pCmd->enmCmd)
    {
        case NEMUVHWACMD_TYPE_SURF_FLIP:
        case NEMUVHWACMD_TYPE_SURF_OVERLAY_UPDATE:
        case NEMUVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION:
            break;
        case NEMUVHWACMD_TYPE_HH_CONSTRUCT:
        {
            NEMUVHWACMD_HH_CONSTRUCT * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_HH_CONSTRUCT);
            pCmd->Flags &= ~NEMUVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = vhwaConstruct(pBody);
            Log(("VHWA Command <<< Sync %#p, %d\n", pCmd, pCmd->enmCmd));
            return VINF_SUCCESS;
        }
        case NEMUVHWACMD_TYPE_HH_RESET:
        {
            /* we do not post a reset command to the gui thread since this may lead to a deadlock
             * when reset is initiated by the gui thread*/
            pCmd->Flags &= ~NEMUVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = reset();
            Log(("VHWA Command <<< Sync %#p, %d\n", pCmd, pCmd->enmCmd));
            return VINF_SUCCESS;
        }
        case NEMUVHWACMD_TYPE_HH_ENABLE:
            pCmd->Flags &= ~NEMUVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = VINF_SUCCESS;
            Log(("VHWA Command <<< Sync %#p, %d\n", pCmd, pCmd->enmCmd));
            return VINF_SUCCESS;
        case NEMUVHWACMD_TYPE_HH_DISABLE:
            pCmd->Flags &= ~NEMUVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = VINF_SUCCESS;
            Log(("VHWA Command <<< Sync %#p, %d\n", pCmd, pCmd->enmCmd));
            return VINF_SUCCESS;
        case NEMUVHWACMD_TYPE_HH_SAVESTATE_SAVEBEGIN:
            mCmdPipe.disable();
            pCmd->Flags &= ~NEMUVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = VINF_SUCCESS;
            Log(("VHWA Command <<< Sync %#p, %d\n", pCmd, pCmd->enmCmd));
            return VINF_SUCCESS;
        case NEMUVHWACMD_TYPE_HH_SAVESTATE_SAVEEND:
            mCmdPipe.enable();
            pCmd->Flags &= ~NEMUVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = VINF_SUCCESS;
            Log(("VHWA Command <<< Sync %#p, %d\n", pCmd, pCmd->enmCmd));
            return VINF_SUCCESS;
        case NEMUVHWACMD_TYPE_HH_SAVESTATE_SAVEPERFORM:
        {
            NEMUVHWACMD_HH_SAVESTATE_SAVEPERFORM *pSave = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_HH_SAVESTATE_SAVEPERFORM);
            PSSMHANDLE pSSM = pSave->pSSM;
            int rc = SSMR3PutU32(pSSM, NEMUQGL_STATE_VERSION); AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                vhwaSaveExec(pSSM);
            }
            pCmd->Flags &= ~NEMUVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = rc;
            Log(("VHWA Command <<< Sync %#p, %d\n", pCmd, pCmd->enmCmd));
            return VINF_SUCCESS;
        }
        case NEMUVHWACMD_TYPE_HH_SAVESTATE_LOADPERFORM:
        {
            NEMUVHWACMD_HH_SAVESTATE_LOADPERFORM *pLoad = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_HH_SAVESTATE_LOADPERFORM);
            PSSMHANDLE pSSM = pLoad->pSSM;
            uint32_t u32Version = 0;
            int rc = SSMR3GetU32(pSSM, &u32Version); Assert(RT_SUCCESS(rc) || rc == VERR_SSM_LOADED_TOO_MUCH);
            if (RT_SUCCESS(rc))
            {
                rc = vhwaLoadExec(pSSM, u32Version); AssertRC(rc);
            }
            else
            {
                /* sanity */
                u32Version = 0;

                if (rc == VERR_SSM_LOADED_TOO_MUCH)
                    rc = VINF_SUCCESS;
            }
            pCmd->Flags &= ~NEMUVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = rc;
            Log(("VHWA Command <<< Sync %#p, %d\n", pCmd, pCmd->enmCmd));
            return VINF_SUCCESS;
        }
        case NEMUVHWACMD_TYPE_QUERY_INFO1:
        {
#ifdef RT_STRICT
            NEMUVHWACMD_QUERYINFO1 * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO1);
#endif
            Assert(pBody->u.in.guestVersion.maj == NEMUVHWA_VERSION_MAJ);
            Assert(pBody->u.in.guestVersion.min == NEMUVHWA_VERSION_MIN);
            Assert(pBody->u.in.guestVersion.bld == NEMUVHWA_VERSION_BLD);
            Assert(pBody->u.in.guestVersion.reserved == NEMUVHWA_VERSION_RSV);
            /* do NOT break!! make it proceed asynchronously */
        }
        default:
            break;
    }

    Log(("VHWA Command --- Going Async %#p, %d\n", pCmd, pCmd->enmCmd));
    /* indicate that we process and complete the command asynchronously */
    pCmd->Flags |= NEMUVHWACMD_FLAG_HG_ASYNCH;

    mCmdPipe.postCmd(NEMUVHWA_PIPECMD_VHWA, pCmd);
    return VINF_CALLBACK_RETURN;

}

void NemuQGLOverlay::onVHWACommandEvent(QEvent * pEvent)
{
    NemuVHWACommandProcessEvent *pVhwaEvent = (NemuVHWACommandProcessEvent*)pEvent;
    /* sanity actually */
    pVhwaEvent->setProcessed();

    Assert(!mProcessingCommands);
    mProcessingCommands = true;
    Assert(!mGlCurrent);
    mGlCurrent = false; /* just a fall-back */
    NemuVHWACommandElement *pCmd = mCmdPipe.getCmd();
    if (pCmd)
    {
        processCmd(pCmd);
        mCmdPipe.doneCmd();
    }

    mProcessingCommands = false;
    repaint();
    mGlCurrent = false;
}

bool NemuQGLOverlay::onNotifyUpdate(ULONG uX, ULONG uY,
                                    ULONG uW, ULONG uH)
{
    /* Prepare corresponding viewport part: */
    QRect rect(uX, uY, uW, uH);

    /* Take the scaling into account: */
    const double dScaleFactor = mSizeInfo.scaleFactor();
    const QSize scaledSize = mSizeInfo.scaledSize();
    if (scaledSize.isValid())
    {
        /* Calculate corresponding scale-factors: */
        const double xScaleFactor = mSizeInfo.visualState() == UIVisualStateType_Scale ?
                                    (double)scaledSize.width()  / mSizeInfo.width()  : dScaleFactor;
        const double yScaleFactor = mSizeInfo.visualState() == UIVisualStateType_Scale ?
                                    (double)scaledSize.height() / mSizeInfo.height() : dScaleFactor;
        /* Adjust corresponding viewport part: */
        rect.moveTo((int)floor((double)rect.x() * xScaleFactor) - 1,
                    (int)floor((double)rect.y() * yScaleFactor) - 1);
        rect.setSize(QSize((int)ceil((double)rect.width()  * xScaleFactor) + 2,
                           (int)ceil((double)rect.height() * yScaleFactor) + 2));
    }

#ifdef Q_WS_MAC
    /* Take the backing-scale-factor into account: */
    if (mSizeInfo.useUnscaledHiDPIOutput())
    {
        const double dBackingScaleFactor = darwinBackingScaleFactor(mpViewport->window());
        if (dBackingScaleFactor > 1.0)
        {
            rect.moveTo((int)floor((double)rect.x() / dBackingScaleFactor) - 1,
                        (int)floor((double)rect.y() / dBackingScaleFactor) - 1);
            rect.setSize(QSize((int)ceil((double)rect.width()  / dBackingScaleFactor) + 2,
                               (int)ceil((double)rect.height() / dBackingScaleFactor) + 2));
        }
    }
#endif /* Q_WS_MAC */

    /* we do not to miss notify updates, because we have to update bg textures for it,
     * so no not check for m_fUnused here,
     * mOverlay will store the required info for us */
    mCmdPipe.postCmd(NEMUVHWA_PIPECMD_PAINT, &rect);

    return true;
}

void NemuQGLOverlay::onResizeEventPostprocess (const NemuFBSizeInfo &re, const QPoint & topLeft)
{
    mSizeInfo = re;
    mContentsTopLeft = topLeft;

    if (mGlOn)
    {
        Assert(mOverlayImage.hasSurfaces());
        Assert(!mGlCurrent);
        Assert(!mNeedOverlayRepaint);
        mGlCurrent = false;
        makeCurrent();
        /* need to ensure we're in sync */
        mNeedOverlayRepaint = nemuSynchGl();

        if (!mOverlayImage.hasSurfaces())
            nemuSetGlOn(false);
    }
    else
        Assert(!mOverlayImage.hasSurfaces());

    if (!mOnResizeCmdList.empty())
    {
        for (VHWACommandList::const_iterator it = mOnResizeCmdList.begin();
             it != mOnResizeCmdList.end(); ++ it)
        {
            NEMUVHWACMD * pCmd = (*it);
            nemuDoVHWACmdExec(pCmd);
            free(pCmd);
        }
        mOnResizeCmdList.clear();
    }

    repaintOverlay();
    mGlCurrent = false;
}

void NemuQGLOverlay::repaintMain()
{
    if(mMainDirtyRect.isClear())
        return;

    const QRect &rect = mMainDirtyRect.rect();
    if(mOverlayWidgetVisible)
    {
        if(mOverlayViewport.contains(rect))
            return;
    }

    mpViewport->repaint (rect.x() - mContentsTopLeft.x(),
            rect.y() - mContentsTopLeft.y(),
            rect.width(), rect.height());

    mMainDirtyRect.clear();
}

void NemuQGLOverlay::nemuDoVHWACmd(void *cmd)
{
    nemuDoVHWACmdExec(cmd);

    CDisplay display = mpSession->GetConsole().GetDisplay();
    Assert (!display.isNull());

    Log(("VHWA Command <<< Async %#p, %d\n", cmd, ((NEMUVHWACMD *)cmd)->enmCmd));

    display.CompleteVHWACommand((BYTE*)cmd);
}

bool NemuQGLOverlay::nemuSynchGl()
{
    NemuVHWASurfaceBase * pVGA = mOverlayImage.vgaSurface();
    if(pVGA
            && mSizeInfo.pixelFormat() == pVGA->pixelFormat().toNemuPixelFormat()
            && mSizeInfo.VRAM() == pVGA->address()
            && mSizeInfo.bitsPerPixel() == pVGA->bitsPerPixel()
            && mSizeInfo.bytesPerLine() == pVGA->bytesPerLine()
            && mSizeInfo.width() == pVGA->width()
            && mSizeInfo.height() == pVGA->height()
            )
    {
        return false;
    }
    /* create and issue a resize event to the gl widget to ensure we have all gl data initialized
     * and synchronized with the framebuffer */
    mOverlayImage.resize(mSizeInfo);
    return true;
}

void NemuQGLOverlay::nemuSetGlOn(bool on)
{
    if(on == mGlOn)
        return;

    mGlOn = on;

    if(on)
    {
        /* need to ensure we have gl functions initialized */
        mpOverlayWgt->makeCurrent();
        nemuVHWAGetSupportInfo(mpOverlayWgt->context());

        NEMUQGLLOGREL(("Switching Gl mode on\n"));
        Assert(!mpOverlayWgt->isVisible());
        /* just to ensure */
        nemuShowOverlay(false);
        mOverlayVisible = false;
        nemuSynchGl();
    }
    else
    {
        NEMUQGLLOGREL(("Switching Gl mode off\n"));
        mOverlayVisible = false;
        nemuShowOverlay(false);
        /* for now just set the flag w/o destroying anything */
    }
}

void NemuQGLOverlay::nemuDoCheckUpdateViewport()
{
    if(!mOverlayVisible)
    {
        nemuShowOverlay(false);
        return;
    }

    int cX = mContentsTopLeft.x();
    int cY = mContentsTopLeft.y();
    QRect fbVp(cX, cY, mpViewport->width(), mpViewport->height());
    QRect overVp = fbVp.intersected(mOverlayViewport);

    if(overVp.isEmpty())
    {
        nemuShowOverlay(false);
    }
    else
    {
        if(overVp != mOverlayImage.nemuViewport())
        {
            makeCurrent();
            mOverlayImage.nemuDoUpdateViewport(overVp);
            mNeedOverlayRepaint = true;
        }

        QRect rect(overVp.x() - cX, overVp.y() - cY, overVp.width(), overVp.height());

        nemuCheckUpdateOverlay(rect);

        nemuShowOverlay(true);

        /* workaround for linux ATI issue: need to update gl viewport after widget becomes visible */
        mOverlayImage.nemuDoUpdateViewport(overVp);
    }
}

void NemuQGLOverlay::nemuShowOverlay(bool show)
{
    if(mOverlayWidgetVisible != show)
    {
        mpOverlayWgt->setVisible(show);
        mOverlayWidgetVisible = show;
        mGlCurrent = false;
        if(!show)
        {
            mMainDirtyRect.add(mOverlayImage.nemuViewport());
        }
    }
}

void NemuQGLOverlay::nemuCheckUpdateOverlay(const QRect & rect)
{
    QRect overRect(mpOverlayWgt->pos(), mpOverlayWgt->size());
    if(overRect.x() != rect.x() || overRect.y() != rect.y())
    {
#if defined(RT_OS_WINDOWS)
        mpOverlayWgt->setVisible(false);
        mNeedSetVisible = true;
#endif
        NEMUQGLLOG_QRECT("moving wgt to " , &rect, "\n");
        mpOverlayWgt->move(rect.x(), rect.y());
        mGlCurrent = false;
    }

    if(overRect.width() != rect.width() || overRect.height() != rect.height())
    {
#if defined(RT_OS_WINDOWS)
        mpOverlayWgt->setVisible(false);
        mNeedSetVisible = true;
#endif
        NEMUQGLLOG(("resizing wgt to w(%d) ,h(%d)\n" , rect.width(), rect.height()));
        mpOverlayWgt->resize(rect.width(), rect.height());
        mGlCurrent = false;
    }
}

void NemuQGLOverlay::addMainDirtyRect(const QRect & aRect)
{
    mMainDirtyRect.add(aRect);
    if(mGlOn)
    {
        mOverlayImage.nemuDoUpdateRect(&aRect);
        mNeedOverlayRepaint = true;
    }
}

int NemuQGLOverlay::vhwaSurfaceUnlock(struct NEMUVHWACMD_SURF_UNLOCK *pCmd)
{
    int rc = mOverlayImage.vhwaSurfaceUnlock(pCmd);
    NemuVHWASurfaceBase * pVGA = mOverlayImage.vgaSurface();
    const NemuVHWADirtyRect & rect = pVGA->getDirtyRect();
    mNeedOverlayRepaint = true;
    if(!rect.isClear())
    {
        mMainDirtyRect.add(rect);
    }
    return rc;
}

void NemuQGLOverlay::nemuDoVHWACmdExec(void *cmd)
{
    struct NEMUVHWACMD * pCmd = (struct NEMUVHWACMD *)cmd;
    switch(pCmd->enmCmd)
    {
        case NEMUVHWACMD_TYPE_SURF_CANCREATE:
        {
            NEMUVHWACMD_SURF_CANCREATE * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_CANCREATE);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceCanCreate(pBody);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_SURF_CREATE:
        {
            NEMUVHWACMD_SURF_CREATE * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_CREATE);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            nemuSetGlOn(true);
            pCmd->rc = mOverlayImage.vhwaSurfaceCreate(pBody);
            if(!mOverlayImage.hasSurfaces())
            {
                nemuSetGlOn(false);
            }
            else
            {
                mOverlayVisible = mOverlayImage.hasVisibleOverlays();
                if(mOverlayVisible)
                {
                    mOverlayViewport = mOverlayImage.overlaysRectUnion();
                }
                nemuDoCheckUpdateViewport();
                mNeedOverlayRepaint = true;
            }

            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_SURF_DESTROY:
        {
            NEMUVHWACMD_SURF_DESTROY * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_DESTROY);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceDestroy(pBody);
            if(!mOverlayImage.hasSurfaces())
            {
                nemuSetGlOn(false);
            }
            else
            {
                mOverlayVisible = mOverlayImage.hasVisibleOverlays();
                if(mOverlayVisible)
                {
                    mOverlayViewport = mOverlayImage.overlaysRectUnion();
                }
                nemuDoCheckUpdateViewport();
                mNeedOverlayRepaint = true;
            }
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_SURF_LOCK:
        {
            NEMUVHWACMD_SURF_LOCK * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_LOCK);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceLock(pBody);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_SURF_UNLOCK:
        {
            NEMUVHWACMD_SURF_UNLOCK * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_UNLOCK);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = vhwaSurfaceUnlock(pBody);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            /* mNeedOverlayRepaint is set inside the vhwaSurfaceUnlock */
        } break;
        case NEMUVHWACMD_TYPE_SURF_BLT:
        {
            NEMUVHWACMD_SURF_BLT * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_BLT);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceBlt(pBody);
            mNeedOverlayRepaint = true;
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_SURF_FLIP:
        {
            NEMUVHWACMD_SURF_FLIP * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_FLIP);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceFlip(pBody);
            mNeedOverlayRepaint = true;
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_SURF_OVERLAY_UPDATE:
        {
            NEMUVHWACMD_SURF_OVERLAY_UPDATE * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_OVERLAY_UPDATE);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceOverlayUpdate(pBody);
            mOverlayVisible = mOverlayImage.hasVisibleOverlays();
            if(mOverlayVisible)
            {
                mOverlayViewport = mOverlayImage.overlaysRectUnion();
            }
            nemuDoCheckUpdateViewport();
            mNeedOverlayRepaint = true;
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION:
        {
            NEMUVHWACMD_SURF_OVERLAY_SETPOSITION * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_OVERLAY_SETPOSITION);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceOverlaySetPosition(pBody);
            mOverlayVisible = mOverlayImage.hasVisibleOverlays();
            if(mOverlayVisible)
            {
                mOverlayViewport = mOverlayImage.overlaysRectUnion();
            }
            nemuDoCheckUpdateViewport();
            mNeedOverlayRepaint = true;
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
#ifdef NEMU_WITH_WDDM
        case NEMUVHWACMD_TYPE_SURF_COLORFILL:
        {
            NEMUVHWACMD_SURF_COLORFILL * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_COLORFILL);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceColorFill(pBody);
            mNeedOverlayRepaint = true;
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
#endif
        case NEMUVHWACMD_TYPE_SURF_COLORKEY_SET:
        {
            NEMUVHWACMD_SURF_COLORKEY_SET * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_COLORKEY_SET);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceColorkeySet(pBody);
            /* this is here to ensure we have color key changes picked up */
            nemuDoCheckUpdateViewport();
            mNeedOverlayRepaint = true;
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_QUERY_INFO1:
        {
            NEMUVHWACMD_QUERYINFO1 * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO1);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaQueryInfo1(pBody);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_QUERY_INFO2:
        {
            NEMUVHWACMD_QUERYINFO2 * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_QUERYINFO2);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaQueryInfo2(pBody);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
        case NEMUVHWACMD_TYPE_ENABLE:
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            initGl();
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        case NEMUVHWACMD_TYPE_DISABLE:
            pCmd->rc = VINF_SUCCESS;
            break;
        case NEMUVHWACMD_TYPE_HH_CONSTRUCT:
        {
            NEMUVHWACMD_HH_CONSTRUCT * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_HH_CONSTRUCT);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            pCmd->rc = vhwaConstruct(pBody);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
#ifdef NEMU_WITH_WDDM
        case NEMUVHWACMD_TYPE_SURF_GETINFO:
        {
            NEMUVHWACMD_SURF_GETINFO * pBody = NEMUVHWACMD_BODY(pCmd, NEMUVHWACMD_SURF_GETINFO);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
            pCmd->rc = mOverlayImage.vhwaSurfaceGetInfo(pBody);
            Assert(!mGlOn == !mOverlayImage.hasSurfaces());
        } break;
#endif
        default:
            AssertFailed();
            pCmd->rc = VERR_NOT_IMPLEMENTED;
            break;
    }
}

#if 0
static DECLCALLBACK(void) nemuQGLOverlaySaveExec(PSSMHANDLE pSSM, void *pvUser)
{
    NemuQGLOverlay * fb = (NemuQGLOverlay*)pvUser;
    fb->vhwaSaveExec(pSSM);
}
#endif

static DECLCALLBACK(int) nemuQGLOverlayLoadExec(PSSMHANDLE pSSM, void *pvUser, uint32_t u32Version, uint32_t uPass)
{
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
    NemuQGLOverlay * fb = (NemuQGLOverlay*)pvUser;
    return fb->vhwaLoadExec(pSSM, u32Version);
}

int NemuQGLOverlay::vhwaLoadExec(struct SSMHANDLE * pSSM, uint32_t u32Version)
{
    int rc = NemuVHWAImage::vhwaLoadExec(&mOnResizeCmdList, pSSM, u32Version);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        if (u32Version >= NEMUQGL_STATE_VERSION_PIPESAVED)
        {
            rc = mCmdPipe.loadExec(pSSM, u32Version, mOverlayImage.vramBase());
            AssertRC(rc);
        }
    }
    return rc;
}

void NemuQGLOverlay::vhwaSaveExec(struct SSMHANDLE * pSSM)
{
    mOverlayImage.vhwaSaveExec(pSSM);
    mCmdPipe.saveExec(pSSM, mOverlayImage.vramBase());
}

int NemuQGLOverlay::vhwaConstruct(struct NEMUVHWACMD_HH_CONSTRUCT *pCmd)
{
    PUVM pUVM = VMR3GetUVM((PVM)pCmd->pVM);
    uint32_t intsId = m_id;

    char nameFuf[sizeof(NEMUQGL_STATE_NAMEBASE) + 8];

    char * pszName = nameFuf;
    sprintf(pszName, "%s%d", NEMUQGL_STATE_NAMEBASE, intsId);
    int rc = SSMR3RegisterExternal(
            pUVM,                   /* The VM handle*/
            pszName,                /* Data unit name. */
            intsId,                 /* The instance identifier of the data unit.
                                     * This must together with the name be unique. */
            NEMUQGL_STATE_VERSION,   /* Data layout version number. */
            128,             /* The approximate amount of data in the unit.
                              * Only for progress indicators. */
            NULL, NULL, NULL, /* pfnLiveXxx */
            NULL,            /* Prepare save callback, optional. */
            NULL, //nemuQGLOverlaySaveExec, /* Execute save callback, optional. */
            NULL,            /* Done save callback, optional. */
            NULL,            /* Prepare load callback, optional. */
            nemuQGLOverlayLoadExec, /* Execute load callback, optional. */
            NULL,            /* Done load callback, optional. */
            this             /* User argument. */
            );
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        rc = mOverlayImage.vhwaConstruct(pCmd);
        AssertRC(rc);
    }
    return rc;
}

/* static */
bool NemuQGLOverlay::isAcceleration2DVideoAvailable()
{
#ifndef DEBUG_misha
    if(!g_bNemuVHWAChecked)
#endif
    {
        g_bNemuVHWAChecked = true;
        g_bNemuVHWASupported = NemuVHWAInfo::checkVHWASupport();
    }
    return g_bNemuVHWASupported;
}

/** additional video memory required for the best 2D support performance
 *  total amount of VRAM required is thus calculated as requiredVideoMemory + required2DOffscreenVideoMemory  */
/* static */
quint64 NemuQGLOverlay::required2DOffscreenVideoMemory()
{
    /* HDTV == 1920x1080 ~ 2M
     * for the 4:2:2 formats each pixel is 2Bytes
     * so each frame may be 4MiB
     * so for triple-buffering we would need 12 MiB */
    return _1M * 12;
}

void NemuQGLOverlay::processCmd(NemuVHWACommandElement * pCmd)
{
    switch(pCmd->type())
    {
        case NEMUVHWA_PIPECMD_PAINT:
            addMainDirtyRect(pCmd->rect());
            break;
#ifdef NEMU_WITH_VIDEOHWACCEL
        case NEMUVHWA_PIPECMD_VHWA:
            nemuDoVHWACmd(pCmd->vhwaCmd());
            break;
        case NEMUVHWA_PIPECMD_FUNC:
        {
            const NEMUVHWAFUNCCALLBACKINFO & info = pCmd->func();
            info.pfnCallback(info.pContext1, info.pContext2);
            break;
        }
#endif
        default:
            AssertFailed();
    }
}

NemuVHWACommandElementProcessor::NemuVHWACommandElementProcessor() :
    m_pNotifyObject(NULL),
    mpCurCmd(NULL),
    mbResetting(false),
    mcDisabled(0)
{
    int rc = RTCritSectInit(&mCritSect);
    AssertRC(rc);

    RTListInit(&mCommandList);

    m_pCmdEntryCache = new NemuVHWAEntriesCache;
}

void NemuVHWACommandElementProcessor::init(QObject *pNotifyObject)
{
    m_pNotifyObject = pNotifyObject;
}

NemuVHWACommandElementProcessor::~NemuVHWACommandElementProcessor()
{
    Assert(!m_NotifyObjectRefs.refs());
    RTListIsEmpty(&mCommandList);

    RTCritSectDelete(&mCritSect);

    delete m_pCmdEntryCache;
}

void NemuVHWACommandElementProcessor::postCmd(NEMUVHWA_PIPECMD_TYPE aType, void * pvData)
{
    QObject *pNotifyObject = NULL;

    Log(("VHWA post %d %#p\n", aType, pvData));

    /* 1. lock*/
    RTCritSectEnter(&mCritSect);

    NemuVHWACommandElement * pCmd = m_pCmdEntryCache->alloc();
    if(!pCmd)
    {
        NEMUQGLLOG(("!!!no more free elements!!!\n"));
#ifdef NEMUQGL_PROF_BASE
        RTCritSectLeave(&mCritSect);
        return;
#else
    //TODO:
#endif
    }
    pCmd->setData(aType, pvData);

    /* 2. if can add to current*/
    if (m_pNotifyObject)
    {
        m_NotifyObjectRefs.inc(); /* ensure the parent does not get destroyed while we are using it */
        pNotifyObject = m_pNotifyObject;
    }

    RTListAppend(&mCommandList, &pCmd->ListNode);

    RTCritSectLeave(&mCritSect);

    if (pNotifyObject)
    {
        NemuVHWACommandProcessEvent *pCurrentEvent = new NemuVHWACommandProcessEvent();
        QApplication::postEvent (pNotifyObject, pCurrentEvent);
        m_NotifyObjectRefs.dec();
    }
}

void NemuVHWACommandElementProcessor::setNotifyObject(QObject *pNotifyObject)
{
    int cEventsNeeded = 0;
    RTCritSectEnter(&mCritSect);
    if (m_pNotifyObject == pNotifyObject)
    {
        RTCritSectLeave(&mCritSect);
        return;
    }

    if (m_pNotifyObject)
    {
        m_pNotifyObject = NULL;
        RTCritSectLeave(&mCritSect);

        m_NotifyObjectRefs.wait0();

        RTCritSectEnter(&mCritSect);
    }
    else
    {
        /* NULL can not be references */
        Assert (!m_NotifyObjectRefs.refs());
    }

    if (pNotifyObject)
    {
        m_pNotifyObject = pNotifyObject;

        NemuVHWACommandElement *pCur;
        RTListForEachCpp(&mCommandList, pCur, NemuVHWACommandElement, ListNode)
        {
            ++cEventsNeeded;
        }

        if (cEventsNeeded)
            m_NotifyObjectRefs.inc();
    }
    else
    {
        /* should be zeroed already */
        Assert(!m_pNotifyObject);
    }

    RTCritSectLeave(&mCritSect);

    if (cEventsNeeded)
    {
        /* cEventsNeeded can only be != 0 if pNotifyObject is valid */
        Assert (pNotifyObject);
        for (int i = 0; i < cEventsNeeded; ++i)
        {
            NemuVHWACommandProcessEvent *pCurrentEvent = new NemuVHWACommandProcessEvent();
            QApplication::postEvent(pNotifyObject, pCurrentEvent);
        }
        m_NotifyObjectRefs.dec();
    }
}

void NemuVHWACommandElementProcessor::doneCmd()
{
    NemuVHWACommandElement * pEl;
    RTCritSectEnter(&mCritSect);
    pEl = mpCurCmd;
    Assert(mpCurCmd);
    mpCurCmd = NULL;
    RTCritSectLeave(&mCritSect);

    if (pEl)
        m_pCmdEntryCache->free(pEl);
}

NemuVHWACommandElement * NemuVHWACommandElementProcessor::getCmd()
{
    NemuVHWACommandElement * pEl = NULL;
    RTCritSectEnter(&mCritSect);

    Assert(!mpCurCmd);

    if (mbResetting)
    {
        RTCritSectLeave(&mCritSect);
        return NULL;
    }

    if (mcDisabled)
    {
        QObject * pNotifyObject = NULL;

        if (!RTListIsEmpty(&mCommandList))
        {
            Assert(m_pNotifyObject);
            if (m_pNotifyObject)
            {
                m_NotifyObjectRefs.inc(); /* ensure the parent does not get destroyed while we are using it */
                pNotifyObject = m_pNotifyObject;
            }
        }

        RTCritSectLeave(&mCritSect);

        if (pNotifyObject)
        {
            NemuVHWACommandProcessEvent *pCurrentEvent = new NemuVHWACommandProcessEvent();
            QApplication::postEvent(pNotifyObject, pCurrentEvent);
            m_NotifyObjectRefs.dec();
        }
        return NULL;
    }

    pEl = RTListGetFirstCpp(&mCommandList, NemuVHWACommandElement, ListNode);
    if (pEl)
    {
        RTListNodeRemove(&pEl->ListNode);
        mpCurCmd = pEl;
    }

    RTCritSectLeave(&mCritSect);

    return pEl;
}

/* it is currently assumed no one sends any new commands while reset is in progress */
void NemuVHWACommandElementProcessor::reset(CDisplay *pDisplay)
{
    NemuVHWACommandElement *pCur, *pNext;

    RTCritSectEnter(&mCritSect);

    mbResetting = true;

    if(mpCurCmd)
    {
        for(;;)
        {
            RTCritSectLeave(&mCritSect);
            RTThreadSleep(2); /* 2 ms */
            RTCritSectEnter(&mCritSect);
            /* it is assumed no one sends any new commands while reset is in progress */
            if(!mpCurCmd)
            {
                break;
            }
        }
    }

    RTCritSectLeave(&mCritSect);

    RTListForEachSafeCpp(&mCommandList, pCur, pNext, NemuVHWACommandElement, ListNode)
    {
        switch(pCur->type())
        {
#ifdef NEMU_WITH_VIDEOHWACCEL
        case NEMUVHWA_PIPECMD_VHWA:
            {
                struct NEMUVHWACMD * pCmd = pCur->vhwaCmd();
                pCmd->rc = VERR_INVALID_STATE;
                Log(("VHWA Command <<< Async RESET %#p, %d\n", pCmd, pCmd->enmCmd));
                pDisplay->CompleteVHWACommand((BYTE*)pCmd);
            }
            break;
        case NEMUVHWA_PIPECMD_FUNC:
            /* should not happen, don't handle this for now */
            AssertFailed();
            break;
#endif
        case NEMUVHWA_PIPECMD_PAINT:
            break;
        default:
            /* should not happen, don't handle this for now */
            AssertFailed();
            break;
        }

        RTListNodeRemove(&pCur->ListNode);
        m_pCmdEntryCache->free(pCur);
    }

    RTCritSectEnter(&mCritSect);

    mbResetting = false;

    RTCritSectLeave(&mCritSect);
}

#define NEMUVHWACOMMANDELEMENTLISTBEGIN_MAGIC 0x89abcdef
#define NEMUVHWACOMMANDELEMENTLISTEND_MAGIC   0xfedcba98

int NemuVHWACommandElementProcessor::loadExec (struct SSMHANDLE * pSSM, uint32_t u32Version, void *pvVRAM)
{
    uint32_t u32;
    bool b;
    int rc;

    Q_UNUSED(u32Version);

    rc = SSMR3GetU32(pSSM, &u32); AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        Assert(u32 == NEMUVHWACOMMANDELEMENTLISTBEGIN_MAGIC);
        if (u32 == NEMUVHWACOMMANDELEMENTLISTBEGIN_MAGIC)
        {
            rc = SSMR3GetU32(pSSM, &u32);
            rc = SSMR3GetBool(pSSM, &b);         AssertRC(rc);
        //    m_NotifyObjectRefs = NemuVHWARefCounter(u32);
            bool bContinue = true;
            do
            {
                rc = SSMR3GetU32(pSSM, &u32);         AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    bool bNewEvent;
                    switch (u32)
                    {
                        case NEMUVHWA_PIPECMD_PAINT:
                        {
                            int x,y,w,h;
                            rc = SSMR3GetS32(pSSM, &x);         AssertRC(rc);
                            rc = SSMR3GetS32(pSSM, &y);         AssertRC(rc);
                            rc = SSMR3GetS32(pSSM, &w);         AssertRC(rc);
                            rc = SSMR3GetS32(pSSM, &h);         AssertRC(rc);

                            rc = SSMR3GetBool(pSSM, &bNewEvent);         AssertRC(rc);

                            if (RT_SUCCESS(rc))
                            {
                                QRect r = QRect(x, y, w, h);
                                postCmd(NEMUVHWA_PIPECMD_PAINT, &r);
                            }
                            break;
                        }
                        case NEMUVHWA_PIPECMD_VHWA:
                        {
                            uint32_t offCmd;
                            rc = SSMR3GetU32(pSSM, &offCmd);         AssertRC(rc);

                            rc = SSMR3GetBool(pSSM, &bNewEvent);         AssertRC(rc);

                            if (RT_SUCCESS(rc))
                            {
                                postCmd(NEMUVHWA_PIPECMD_VHWA, (NEMUVHWACMD*)(((uint8_t*)pvVRAM) + offCmd));
                            }
                            break;
                        }
                        case NEMUVHWACOMMANDELEMENTLISTEND_MAGIC:
                        {
                            bContinue = false;
                            break;
                        }
                        default:
                            AssertFailed();
                            break;
                    }

                }

            } while(bContinue && RT_SUCCESS(rc));
        }
        else
        {
            rc = VERR_INVALID_MAGIC;
        }
    }

    return rc;
}

void NemuVHWACommandElementProcessor::saveExec (struct SSMHANDLE * pSSM, void *pvVRAM)
{
    int rc;

    rc = SSMR3PutU32(pSSM, NEMUVHWACOMMANDELEMENTLISTBEGIN_MAGIC);         AssertRC(rc);
    rc = SSMR3PutU32(pSSM, m_NotifyObjectRefs.refs());         AssertRC(rc);
    rc = SSMR3PutBool(pSSM, true);         AssertRC(rc);

    NemuVHWACommandElement *pCur;
    RTListForEachCpp(&mCommandList, pCur, NemuVHWACommandElement, ListNode)
    {
        rc = SSMR3PutU32(pSSM, pCur->type());         AssertRC(rc);

        switch (pCur->type())
        {
            case NEMUVHWA_PIPECMD_PAINT:
                rc = SSMR3PutS32(pSSM, pCur->rect().x());         AssertRC(rc);
                rc = SSMR3PutS32(pSSM, pCur->rect().y());         AssertRC(rc);
                rc = SSMR3PutS32(pSSM, pCur->rect().width());         AssertRC(rc);
                rc = SSMR3PutS32(pSSM, pCur->rect().height());         AssertRC(rc);
                rc = SSMR3PutBool(pSSM, true);         AssertRC(rc);
                break;
            case NEMUVHWA_PIPECMD_VHWA:
            {
                rc = SSMR3PutU32(pSSM, (uint32_t)((uintptr_t)((uint8_t*)pCur->vhwaCmd() - (uint8_t*)pvVRAM)));         AssertRC(rc);
                rc = SSMR3PutBool(pSSM, true);         AssertRC(rc);
                break;
            }
            default:
                AssertFailed();
                break;
        }
    }

    rc = SSMR3PutU32(pSSM, NEMUVHWACOMMANDELEMENTLISTEND_MAGIC);         AssertRC(rc);
}

void NemuVHWACommandElementProcessor::lock()
{
    RTCritSectEnter(&mCritSect);

    if(mpCurCmd)
    {
        for(;;)
        {
            RTCritSectLeave(&mCritSect);
            RTThreadSleep(2); /* 2 ms */
            RTCritSectEnter(&mCritSect);
            /* it is assumed no one sends any new commands while reset is in progress */
            if(!mpCurCmd)
            {
                break;
            }
        }
    }

    Assert(!mpCurCmd);
}

void NemuVHWACommandElementProcessor::unlock()
{
    RTCritSectLeave(&mCritSect);
}

void NemuVHWACommandElementProcessor::disable()
{
    lock();
    ++mcDisabled;
    unlock();
}

void NemuVHWACommandElementProcessor::enable()
{
    lock();
    --mcDisabled;
    unlock();
}

/* static */
uint32_t NemuVHWATextureImage::calcBytesPerLine(const NemuVHWAColorFormat & format, int width)
{
    uint32_t pitch = (format.bitsPerPixel() * width + 7)/8;
    switch (format.fourcc())
    {
        case FOURCC_YV12:
            /* make sure the color components pitch is multiple of 8
             * where 8 is 2 (for color component width is Y width / 2) * 4 for 4byte texture format */
            pitch = (pitch + 7) & ~7;
            break;
        default:
            pitch = (pitch + 3) & ~3;
            break;
    }
    return pitch;
}

/* static */
uint32_t NemuVHWATextureImage::calcMemSize(const NemuVHWAColorFormat & format, int width, int height)
{
    uint32_t pitch = calcBytesPerLine(format, width);
    switch (format.fourcc())
    {
        case FOURCC_YV12:
            /* we have 3 separate planes here
             * Y - pitch x height
             * U - pitch/2 x height/2
             * V - pitch/2 x height/2
             * */
            return 3 * pitch * height / 2;
            break;
        default:
            return pitch * height;
            break;
    }
}

NemuVHWATextureImage::NemuVHWATextureImage(const QRect &size, const NemuVHWAColorFormat &format, class NemuVHWAGlProgramMngr * aMgr, NEMUVHWAIMG_TYPE flags) :
        mVisibleDisplay(0),
        mpProgram(0),
        mProgramMngr(aMgr),
        mpDst(NULL),
        mpDstCKey(NULL),
        mpSrcCKey(NULL),
        mbNotIntersected(false)
{
    uint32_t pitch = calcBytesPerLine(format, size.width());

    mpTex[0] = nemuVHWATextureCreate(NULL, size, format, pitch, flags);
    mColorFormat = format;
    if(mColorFormat.fourcc() == FOURCC_YV12)
    {
        QRect rect(size.x()/2,size.y()/2,size.width()/2,size.height()/2);
        mpTex[1] = nemuVHWATextureCreate(NULL, rect, format, pitch/2, flags);
        mpTex[2] = nemuVHWATextureCreate(NULL, rect, format, pitch/2, flags);
        mcTex = 3;
    }
    else
    {
        mcTex = 1;
    }
}

void NemuVHWATextureImage::deleteDisplayList()
{
    if(mVisibleDisplay)
    {
        glDeleteLists(mVisibleDisplay, 1);
        mVisibleDisplay = 0;
    }
}

void NemuVHWATextureImage::deleteDisplay()
{
    deleteDisplayList();
    mpProgram = NULL;
}

void NemuVHWATextureImage::draw(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect)
{
    int tx1, ty1, tx2, ty2;
    pSrcRect->getCoords(&tx1, &ty1, &tx2, &ty2);
    int bx1, by1, bx2, by2;
    pDstRect->getCoords(&bx1, &by1, &bx2, &by2);
    tx2++; ty2++;bx2++; by2++;

    glBegin(GL_QUADS);
    uint32_t c = texCoord(GL_TEXTURE0, tx1, ty1);
    if(pDst)
    {
        pDst->texCoord(GL_TEXTURE0 + c, bx1, by1);
    }
    glVertex2i(bx1, by1);

    texCoord(GL_TEXTURE0, tx1, ty2);
    if(pDst)
    {
        pDst->texCoord(GL_TEXTURE0 + c, bx1, by2);
    }
    glVertex2i(bx1, by2);

    texCoord(GL_TEXTURE0, tx2, ty2);
    if(pDst)
    {
        pDst->texCoord(GL_TEXTURE0 + c, bx2, by2);
    }
    glVertex2i(bx2, by2);

    texCoord(GL_TEXTURE0, tx2, ty1);
    if(pDst)
    {
        pDst->texCoord(GL_TEXTURE0 + c, bx2, by1);
    }
    glVertex2i(bx2, by1);

    glEnd();
}

void NemuVHWATextureImage::internalSetDstCKey(const NemuVHWAColorKey * pDstCKey)
{
    if(pDstCKey)
    {
        mDstCKey = *pDstCKey;
        mpDstCKey = &mDstCKey;
    }
    else
    {
        mpDstCKey = NULL;
    }
}

void NemuVHWATextureImage::internalSetSrcCKey(const NemuVHWAColorKey * pSrcCKey)
{
    if(pSrcCKey)
    {
        mSrcCKey = *pSrcCKey;
        mpSrcCKey = &mSrcCKey;
    }
    else
    {
        mpSrcCKey = NULL;
    }
}

int NemuVHWATextureImage::initDisplay(NemuVHWATextureImage *pDst,
        const QRect * pDstRect, const QRect * pSrcRect,
        const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    if(!mVisibleDisplay
            || mpDst != pDst
            || *pDstRect != mDstRect
            || *pSrcRect != mSrcRect
            || !!(pDstCKey) != !!(mpDstCKey)
            || !!(pSrcCKey) != !!(mpSrcCKey)
            || mbNotIntersected != bNotIntersected
            || mpProgram != calcProgram(pDst, pDstCKey, pSrcCKey, bNotIntersected))
    {
        return createSetDisplay(pDst, pDstRect, pSrcRect,
                pDstCKey, pSrcCKey, bNotIntersected);

    }
    else if((pDstCKey && mpDstCKey && *pDstCKey != *mpDstCKey)
            || (pSrcCKey && mpSrcCKey && *pSrcCKey != *mpSrcCKey))
    {
        Assert(mpProgram);
        updateSetCKeys(pDstCKey, pSrcCKey);
        return VINF_SUCCESS;
    }
    return VINF_SUCCESS;
}

void NemuVHWATextureImage::bind(NemuVHWATextureImage * pPrimary)
{
    for(uint32_t i = 1; i < mcTex; i++)
    {
        nemuglActiveTexture(GL_TEXTURE0 + i);
        mpTex[i]->bind();
    }
    if(pPrimary)
    {
        for(uint32_t i = 0; i < pPrimary->mcTex; i++)
        {
            nemuglActiveTexture(GL_TEXTURE0 + i + mcTex);
            pPrimary->mpTex[i]->bind();
        }
    }

    nemuglActiveTexture(GL_TEXTURE0);
    mpTex[0]->bind();
}

uint32_t NemuVHWATextureImage::calcProgramType(NemuVHWATextureImage *pDst, const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    uint32_t type = 0;

    if(pDstCKey != NULL)
        type |= NEMUVHWA_PROGRAM_DSTCOLORKEY;
    if(pSrcCKey)
        type |= NEMUVHWA_PROGRAM_SRCCOLORKEY;
    if((pDstCKey || pSrcCKey) && bNotIntersected)
        type |= NEMUVHWA_PROGRAM_COLORKEYNODISCARD;

    NOREF(pDst);
    return type;
}

class NemuVHWAGlProgramVHWA * NemuVHWATextureImage::calcProgram(NemuVHWATextureImage *pDst, const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    uint32_t type = calcProgramType(pDst, pDstCKey, pSrcCKey, bNotIntersected);

    return mProgramMngr->getProgram(type, &pixelFormat(), pDst ? &pDst->pixelFormat() : NULL);
}

int NemuVHWATextureImage::createSetDisplay(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
        const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    deleteDisplay();
    int rc = createDisplay(pDst, pDstRect, pSrcRect,
            pDstCKey, pSrcCKey, bNotIntersected,
            &mVisibleDisplay, &mpProgram);
    if(RT_FAILURE(rc))
    {
        mVisibleDisplay = 0;
        mpProgram = NULL;
    }

    mpDst = pDst;

    mDstRect = *pDstRect;
    mSrcRect = *pSrcRect;

    internalSetDstCKey(pDstCKey);
    internalSetSrcCKey(pSrcCKey);

    mbNotIntersected = bNotIntersected;

    return rc;
}


int NemuVHWATextureImage::createDisplayList(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
        const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected,
        GLuint *pDisplay)
{
    Q_UNUSED(pDstCKey);
    Q_UNUSED(pSrcCKey);
    Q_UNUSED(bNotIntersected);

    glGetError(); /* clear the err flag */
    GLuint display = glGenLists(1);
    GLenum err = glGetError();
    if(err == GL_NO_ERROR)
    {
        Assert(display);
        if(!display)
        {
            /* well, it seems it should not return 0 on success according to the spec,
             * but just in case, pick another one */
            display = glGenLists(1);
            err = glGetError();
            if(err == GL_NO_ERROR)
            {
                Assert(display);
            }
            else
            {
                /* we are failed */
                Assert(!display);
                display = 0;
            }
        }

        if(display)
        {
            glNewList(display, GL_COMPILE);

            runDisplay(pDst, pDstRect, pSrcRect);

            glEndList();
            NEMUQGL_ASSERTNOERR();
            *pDisplay = display;
            return VINF_SUCCESS;
        }
    }
    else
    {
        NEMUQGLLOG(("gl error ocured (0x%x)\n", err));
        Assert(err == GL_NO_ERROR);
    }
    return VERR_GENERAL_FAILURE;
}

void NemuVHWATextureImage::updateCKeys(NemuVHWATextureImage * pDst, class NemuVHWAGlProgramVHWA * pProgram, const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey)
{
    if(pProgram)
    {
        pProgram->start();
        if(pSrcCKey)
        {
            NemuVHWATextureImage::setCKey(pProgram, &pixelFormat(), pSrcCKey, false);
        }
        if(pDstCKey)
        {
            NemuVHWATextureImage::setCKey(pProgram, &pDst->pixelFormat(), pDstCKey, true);
        }
        pProgram->stop();
    }
}

void NemuVHWATextureImage::updateSetCKeys(const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey)
{
    updateCKeys(mpDst, mpProgram, pDstCKey, pSrcCKey);
    internalSetDstCKey(pDstCKey);
    internalSetSrcCKey(pSrcCKey);
}

int NemuVHWATextureImage::createDisplay(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
        const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected,
        GLuint *pDisplay, class NemuVHWAGlProgramVHWA ** ppProgram)
{
    NemuVHWAGlProgramVHWA * pProgram = NULL;
    if (!pDst)
    {
        /* sanity */
        Assert(pDstCKey == NULL);
        pDstCKey = NULL;
    }

    Assert(!pSrcCKey);
    if (pSrcCKey)
        pSrcCKey = NULL; /* fallback */

    pProgram = calcProgram(pDst, pDstCKey, pSrcCKey, bNotIntersected);

    updateCKeys(pDst, pProgram, pDstCKey, pSrcCKey);

    GLuint displ;
    int rc = createDisplayList(pDst, pDstRect, pSrcRect,
            pDstCKey, pSrcCKey, bNotIntersected,
            &displ);
    if(RT_SUCCESS(rc))
    {
        *pDisplay = displ;
        *ppProgram = pProgram;
    }

    return rc;
}

void NemuVHWATextureImage::display(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
        const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    NemuVHWAGlProgramVHWA * pProgram = calcProgram(pDst, pDstCKey, pSrcCKey, bNotIntersected);
    if(pProgram)
        pProgram->start();

    runDisplay(pDst, pDstRect, pSrcRect);

    if(pProgram)
        pProgram->stop();
}

void NemuVHWATextureImage::display()
{
#ifdef DEBUG_misha
    if (mpDst)
    {
        dbgDump();
    }

    static bool bDisplayOn = true;
#endif
    Assert(mVisibleDisplay);
    if(mVisibleDisplay
#ifdef DEBUG_misha
            && bDisplayOn
#endif
            )
    {
        if(mpProgram)
            mpProgram->start();

        NEMUQGL_CHECKERR(
                glCallList(mVisibleDisplay);
                );

        if(mpProgram)
            mpProgram->stop();
    }
    else
    {
        display(mpDst, &mDstRect, &mSrcRect,
                mpDstCKey, mpSrcCKey, mbNotIntersected);
    }
}

#ifdef DEBUG_misha
void NemuVHWATextureImage::dbgDump()
{
    for (uint32_t i = 0; i < mcTex; ++i)
    {
        mpTex[i]->dbgDump();
    }
}
#endif

int NemuVHWATextureImage::setCKey (NemuVHWAGlProgramVHWA * pProgram, const NemuVHWAColorFormat * pFormat, const NemuVHWAColorKey * pCKey, bool bDst)
{
    float r,g,b;
    pFormat->pixel2Normalized (pCKey->lower(), &r, &g, &b);
    int rcL = bDst ? pProgram->setDstCKeyLowerRange (r, g, b) : pProgram->setSrcCKeyLowerRange (r, g, b);
    Assert(RT_SUCCESS(rcL));

    return RT_SUCCESS(rcL) /*&& RT_SUCCESS(rcU)*/ ? VINF_SUCCESS: VERR_GENERAL_FAILURE;
}

NemuVHWASettings::NemuVHWASettings ()
{
}

void NemuVHWASettings::init(CSession &session)
{
    const QString strMachineID = session.GetMachine().GetId();

    mStretchLinearEnabled = gEDataManager->useLinearStretch(strMachineID);

    uint32_t aFourccs[NEMUVHWA_NUMFOURCC];
    int num = 0;
    if (gEDataManager->usePixelFormatAYUV(strMachineID))
        aFourccs[num++] = FOURCC_AYUV;
    if (gEDataManager->usePixelFormatUYVY(strMachineID))
        aFourccs[num++] = FOURCC_UYVY;
    if (gEDataManager->usePixelFormatYUY2(strMachineID))
        aFourccs[num++] = FOURCC_YUY2;
    if (gEDataManager->usePixelFormatYV12(strMachineID))
        aFourccs[num++] = FOURCC_YV12;

    mFourccEnabledCount = num;
    memcpy(mFourccEnabledList, aFourccs, num* sizeof (aFourccs[0]));
}

int NemuVHWASettings::calcIntersection (int c1, const uint32_t *a1, int c2, const uint32_t *a2, int cOut, uint32_t *aOut)
{
    /* fourcc arrays are not big, so linear search is enough,
     * also no need to check for duplicates */
    int cMatch = 0;
    for (int i = 0; i < c1; ++i)
    {
        uint32_t cur1 = a1[i];
        for (int j = 0; j < c2; ++j)
        {
            uint32_t cur2 = a2[j];
            if(cur1 == cur2)
            {
                if(cOut > cMatch && aOut)
                    aOut[cMatch] = cur1;
                ++cMatch;
                break;
            }
        }
    }

    return cMatch;
}

#endif /* NEMU_GUI_USE_QGL */

