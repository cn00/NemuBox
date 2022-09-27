/* $Id:  $ */
/** @file
 * Nemu Qt GUI - NemuFrameBuffer Overly classes declarations.
 */

/*
 * Copyright (C) 2006-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef __NemuFBOverlay_h__
#define __NemuFBOverlay_h__
#if defined (NEMU_GUI_USE_QGL) || defined(NEMU_WITH_VIDEOHWACCEL)

/* Defines: */
//#define NEMUQGL_PROF_BASE 1
//#define NEMUQGL_DBG_SURF 1
//#define NEMUVHWADBG_RENDERCHECK
#define NEMUVHWA_ALLOW_PRIMARY_AND_OVERLAY_ONLY 1

/* Qt includes: */
#include <QGLWidget>

/* GUI includes: */
#include "UIDefs.h"
#include "NemuFBOverlayCommon.h"
#include "runtime/UIFrameBuffer.h"
#include "runtime/UIMachineView.h"

/* COM includes: */
#include "COMEnums.h"

#include "CDisplay.h"

/* Other Nemu includes: */
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <Nemu/NemuGL2D.h>
#ifdef NEMUVHWA_PROFILE_FPS
# include <iprt/stream.h>
#endif /* NEMUVHWA_PROFILE_FPS */

#ifndef S_FALSE
# define S_FALSE ((HRESULT)1L)
#endif

#ifdef DEBUG_misha
# define NEMUVHWA_PROFILE_FPS
#endif /* DEBUG_misha */

/* Forward declarations: */
class CSession;

#ifdef DEBUG
class NemuVHWADbgTimer
{
public:
    NemuVHWADbgTimer(uint32_t cPeriods);
    ~NemuVHWADbgTimer();
    void frame();
    uint64_t everagePeriod() {return mPeriodSum / mcPeriods; }
    double fps() {return ((double)1000000000.0) / everagePeriod(); }
    uint64_t frames() {return mcFrames; }
private:
    uint64_t mPeriodSum;
    uint64_t *mpaPeriods;
    uint64_t mPrevTime;
    uint64_t mcFrames;
    uint32_t mcPeriods;
    uint32_t miPeriod;
};

#endif /* DEBUG */

class NemuVHWASettings
{
public:
    NemuVHWASettings ();
    void init(CSession &session);

    int fourccEnabledCount() const { return mFourccEnabledCount; }
    const uint32_t * fourccEnabledList() const { return mFourccEnabledList; }

    bool isStretchLinearEnabled() const { return mStretchLinearEnabled; }

    static int calcIntersection (int c1, const uint32_t *a1, int c2, const uint32_t *a2, int cOut, uint32_t *aOut);

    int getIntersection (const NemuVHWAInfo &aInfo, int cOut, uint32_t *aOut)
    {
        return calcIntersection (mFourccEnabledCount, mFourccEnabledList, aInfo.getFourccSupportedCount(), aInfo.getFourccSupportedList(), cOut, aOut);
    }

    bool isSupported(const NemuVHWAInfo &aInfo, uint32_t format)
    {
        return calcIntersection (mFourccEnabledCount, mFourccEnabledList, 1, &format, 0, NULL)
                && calcIntersection (aInfo.getFourccSupportedCount(), aInfo.getFourccSupportedList(), 1, &format, 0, NULL);
    }
private:
    uint32_t mFourccEnabledList[NEMUVHWA_NUMFOURCC];
    int mFourccEnabledCount;
    bool mStretchLinearEnabled;
};

class NemuVHWADirtyRect
{
public:
    NemuVHWADirtyRect() :
        mIsClear(true)
    {}

    NemuVHWADirtyRect(const QRect & aRect)
    {
        if(aRect.isEmpty())
        {
            mIsClear = false;
            mRect = aRect;
        }
        else
        {
            mIsClear = true;
        }
    }

    bool isClear() const { return mIsClear; }

    void add(const QRect & aRect)
    {
        if(aRect.isEmpty())
            return;

        mRect = mIsClear ? aRect : mRect.united(aRect);
        mIsClear = false;
    }

    void add(const NemuVHWADirtyRect & aRect)
    {
        if(aRect.isClear())
            return;
        add(aRect.rect());
    }

    void set(const QRect & aRect)
    {
        if(aRect.isEmpty())
        {
            mIsClear = true;
        }
        else
        {
            mRect = aRect;
            mIsClear = false;
        }
    }

    void clear() { mIsClear = true; }

    const QRect & rect() const {return mRect;}

    const QRect & toRect()
    {
        if(isClear())
        {
            mRect.setCoords(0, 0, -1, -1);
        }
        return mRect;
    }

    bool intersects(const QRect & aRect) const {return mIsClear ? false : mRect.intersects(aRect);}

    bool intersects(const NemuVHWADirtyRect & aRect) const {return mIsClear ? false : aRect.intersects(mRect);}

    QRect united(const QRect & aRect) const {return mIsClear ? aRect : aRect.united(mRect);}

    bool contains(const QRect & aRect) const {return mIsClear ? false : aRect.contains(mRect);}

    void subst(const NemuVHWADirtyRect & aRect) { if(!mIsClear && aRect.contains(mRect)) clear(); }

private:
    QRect mRect;
    bool mIsClear;
};

class NemuVHWAColorKey
{
public:
    NemuVHWAColorKey() :
        mUpper(0),
        mLower(0)
    {}

    NemuVHWAColorKey(uint32_t aUpper, uint32_t aLower) :
        mUpper(aUpper),
        mLower(aLower)
    {}

    uint32_t upper() const {return mUpper; }
    uint32_t lower() const {return mLower; }

    bool operator==(const NemuVHWAColorKey & other) const { return mUpper == other.mUpper && mLower == other.mLower; }
    bool operator!=(const NemuVHWAColorKey & other) const { return !(*this == other); }
private:
    uint32_t mUpper;
    uint32_t mLower;
};

class NemuVHWAColorComponent
{
public:
    NemuVHWAColorComponent() :
        mMask(0),
        mRange(0),
        mOffset(32),
        mcBits(0)
    {}

    NemuVHWAColorComponent(uint32_t aMask);

    uint32_t mask() const { return mMask; }
    uint32_t range() const { return mRange; }
    uint32_t offset() const { return mOffset; }
    uint32_t cBits() const { return mcBits; }
    uint32_t colorVal(uint32_t col) const { return (col & mMask) >> mOffset; }
    float colorValNorm(uint32_t col) const { return ((float)colorVal(col))/mRange; }
private:
    uint32_t mMask;
    uint32_t mRange;
    uint32_t mOffset;
    uint32_t mcBits;
};

class NemuVHWAColorFormat
{
public:

    NemuVHWAColorFormat(uint32_t bitsPerPixel, uint32_t r, uint32_t g, uint32_t b);
    NemuVHWAColorFormat(uint32_t fourcc);
    NemuVHWAColorFormat() :
        mBitsPerPixel(0) /* needed for isValid() to work */
    {}
    GLint internalFormat() const {return mInternalFormat; }
    GLenum format() const {return mFormat; }
    GLenum type() const {return mType; }
    bool isValid() const {return mBitsPerPixel != 0; }
    uint32_t fourcc() const {return mDataFormat;}
    uint32_t bitsPerPixel() const { return mBitsPerPixel; }
    uint32_t bitsPerPixelTex() const { return mBitsPerPixelTex; }
    void pixel2Normalized(uint32_t pix, float *r, float *g, float *b) const;
    uint32_t widthCompression() const {return mWidthCompression;}
    uint32_t heightCompression() const {return mHeightCompression;}
    const NemuVHWAColorComponent& r() const {return mR;}
    const NemuVHWAColorComponent& g() const {return mG;}
    const NemuVHWAColorComponent& b() const {return mB;}
    const NemuVHWAColorComponent& a() const {return mA;}

    bool equals (const NemuVHWAColorFormat & other) const;

    ulong toNemuPixelFormat() const
    {
        if (!mDataFormat)
        {
            /* RGB data */
            switch (mFormat)
            {
                case GL_BGRA_EXT:
                    return KBitmapFormat_BGR;
            }
        }
        return KBitmapFormat_Opaque;
    }

private:
    void init(uint32_t bitsPerPixel, uint32_t r, uint32_t g, uint32_t b);
    void init(uint32_t fourcc);

    GLint mInternalFormat;
    GLenum mFormat;
    GLenum mType;
    uint32_t mDataFormat;

    uint32_t mBitsPerPixel;
    uint32_t mBitsPerPixelTex;
    uint32_t mWidthCompression;
    uint32_t mHeightCompression;
    NemuVHWAColorComponent mR;
    NemuVHWAColorComponent mG;
    NemuVHWAColorComponent mB;
    NemuVHWAColorComponent mA;
};

class NemuVHWATexture
{
public:
    NemuVHWATexture() :
            mAddress(NULL),
            mTexture(0),
            mBytesPerPixel(0),
            mBytesPerPixelTex(0),
            mBytesPerLine(0),
            mScaleFuncttion(GL_NEAREST)
{}
    NemuVHWATexture(const QRect & aRect, const NemuVHWAColorFormat &aFormat, uint32_t bytesPerLine, GLint scaleFuncttion);
    virtual ~NemuVHWATexture();
    virtual void init(uchar *pvMem);
    void setAddress(uchar *pvMem) {mAddress = pvMem;}
    void update(const QRect * pRect) { doUpdate(mAddress, pRect);}
    void bind() {glBindTexture(texTarget(), mTexture);}

    virtual void texCoord(int x, int y);
    virtual void multiTexCoord(GLenum texUnit, int x, int y);

    const QRect & texRect() {return mTexRect;}
    const QRect & rect() {return mRect;}
    uchar * address(){ return mAddress; }
    uint32_t rectSizeTex(const QRect * pRect) {return pRect->width() * pRect->height() * mBytesPerPixelTex;}
    uchar * pointAddress(int x, int y)
    {
        x = toXTex(x);
        y = toYTex(y);
        return pointAddressTex(x, y);
    }
    uint32_t pointOffsetTex(int x, int y) { return y*mBytesPerLine + x*mBytesPerPixelTex; }
    uchar * pointAddressTex(int x, int y) { return mAddress + pointOffsetTex(x, y); }
    int toXTex(int x) {return x/mColorFormat.widthCompression();}
    int toYTex(int y) {return y/mColorFormat.heightCompression();}
    ulong memSize(){ return mBytesPerLine * mRect.height(); }
    uint32_t bytesPerLine() {return mBytesPerLine; }
#ifdef DEBUG_misha
    void dbgDump();
#endif

protected:
    virtual void doUpdate(uchar * pAddress, const QRect * pRect);
    virtual void initParams();
    virtual void load();
    virtual GLenum texTarget() {return GL_TEXTURE_2D; }
    GLuint texture() {return mTexture;}

    QRect mTexRect; /* texture size */
    QRect mRect; /* img size */
    uchar * mAddress;
    GLuint mTexture;
    uint32_t mBytesPerPixel;
    uint32_t mBytesPerPixelTex;
    uint32_t mBytesPerLine;
    NemuVHWAColorFormat mColorFormat;
    GLint mScaleFuncttion;
private:
    void uninit();

    friend class NemuVHWAFBO;
};

class NemuVHWATextureNP2 : public NemuVHWATexture
{
public:
    NemuVHWATextureNP2() : NemuVHWATexture() {}
    NemuVHWATextureNP2(const QRect & aRect, const NemuVHWAColorFormat &aFormat, uint32_t bytesPerLine, GLint scaleFuncttion) :
        NemuVHWATexture(aRect, aFormat, bytesPerLine, scaleFuncttion){
        mTexRect = QRect(0, 0, aRect.width()/aFormat.widthCompression(), aRect.height()/aFormat.heightCompression());
    }
};

class NemuVHWATextureNP2Rect : public NemuVHWATextureNP2
{
public:
    NemuVHWATextureNP2Rect() : NemuVHWATextureNP2() {}
    NemuVHWATextureNP2Rect(const QRect & aRect, const NemuVHWAColorFormat &aFormat, uint32_t bytesPerLine, GLint scaleFuncttion) :
        NemuVHWATextureNP2(aRect, aFormat, bytesPerLine, scaleFuncttion){}

    virtual void texCoord(int x, int y);
    virtual void multiTexCoord(GLenum texUnit, int x, int y);
protected:
    virtual GLenum texTarget();
};

class NemuVHWATextureNP2RectPBO : public NemuVHWATextureNP2Rect
{
public:
    NemuVHWATextureNP2RectPBO() :
        NemuVHWATextureNP2Rect(),
        mPBO(0)
    {}
    NemuVHWATextureNP2RectPBO(const QRect & aRect, const NemuVHWAColorFormat &aFormat, uint32_t bytesPerLine, GLint scaleFuncttion) :
        NemuVHWATextureNP2Rect(aRect, aFormat, bytesPerLine, scaleFuncttion),
        mPBO(0)
    {}

    virtual ~NemuVHWATextureNP2RectPBO();

    virtual void init(uchar *pvMem);
protected:
    virtual void load();
    virtual void doUpdate(uchar * pAddress, const QRect * pRect);
    GLuint mPBO;
};

class NemuVHWATextureNP2RectPBOMapped : public NemuVHWATextureNP2RectPBO
{
public:
    NemuVHWATextureNP2RectPBOMapped() :
        NemuVHWATextureNP2RectPBO(),
        mpMappedAllignedBuffer(NULL),
        mcbAllignedBufferSize(0),
        mcbOffset(0)
    {}
    NemuVHWATextureNP2RectPBOMapped(const QRect & aRect, const NemuVHWAColorFormat &aFormat, uint32_t bytesPerLine, GLint scaleFuncttion) :
            NemuVHWATextureNP2RectPBO(aRect, aFormat, bytesPerLine, scaleFuncttion),
            mpMappedAllignedBuffer(NULL),
            mcbOffset(0)
    {
        mcbAllignedBufferSize = alignSize((size_t)memSize());
        mcbActualBufferSize = mcbAllignedBufferSize + 0x1fff;
    }

    uchar* mapAlignedBuffer();
    void   unmapBuffer();
    size_t alignedBufferSize() { return mcbAllignedBufferSize; }

    static size_t alignSize(size_t size)
    {
        size_t alSize = size & ~((size_t)0xfff);
        return alSize == size ? alSize : alSize + 0x1000;
    }

    static void* alignBuffer(void* pvMem) { return (void*)(((uintptr_t)pvMem) & ~((uintptr_t)0xfff)); }
    static size_t calcOffset(void* pvBase, void* pvOffset) { return (size_t)(((uintptr_t)pvBase) - ((uintptr_t)pvOffset)); }
protected:
    virtual void load();
    virtual void doUpdate(uchar * pAddress, const QRect * pRect);
private:
    uchar* mpMappedAllignedBuffer;
    size_t mcbAllignedBufferSize;
    size_t mcbOffset;
    size_t mcbActualBufferSize;
};

#define NEMUVHWAIMG_PBO    0x00000001U
#define NEMUVHWAIMG_PBOIMG 0x00000002U
#define NEMUVHWAIMG_FBO    0x00000004U
#define NEMUVHWAIMG_LINEAR 0x00000008U
typedef uint32_t NEMUVHWAIMG_TYPE;

class NemuVHWATextureImage
{
public:
    NemuVHWATextureImage(const QRect &size, const NemuVHWAColorFormat &format, class NemuVHWAGlProgramMngr * aMgr, NEMUVHWAIMG_TYPE flags);

    virtual ~NemuVHWATextureImage()
    {
        for(uint i = 0; i < mcTex; i++)
        {
            delete mpTex[i];
        }
    }

    virtual void init(uchar *pvMem)
    {
        for(uint32_t i = 0; i < mcTex; i++)
        {
            mpTex[i]->init(pvMem);
            pvMem += mpTex[i]->memSize();
        }
    }

    virtual void update(const QRect * pRect)
    {
        mpTex[0]->update(pRect);
        if(mColorFormat.fourcc() == FOURCC_YV12)
        {
            if(pRect)
            {
                QRect rect(pRect->x()/2, pRect->y()/2,
                        pRect->width()/2, pRect->height()/2);
                mpTex[1]->update(&rect);
                mpTex[2]->update(&rect);
            }
            else
            {
                mpTex[1]->update(NULL);
                mpTex[2]->update(NULL);
            }
        }
    }

    virtual void display(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
            const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected);


    virtual void display();

    void deleteDisplay();

    int initDisplay(NemuVHWATextureImage *pDst,
            const QRect * pDstRect, const QRect * pSrcRect,
            const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected);

    bool displayInitialized() { return !!mVisibleDisplay;}

    virtual void setAddress(uchar *pvMem)
    {
        for(uint32_t i = 0; i < mcTex; i++)
        {
            mpTex[i]->setAddress(pvMem);
            pvMem += mpTex[i]->memSize();
        }
    }

    const QRect &rect()
    {
        return mpTex[0]->rect();
    }

    size_t memSize()
    {
        size_t size = 0;
        for(uint32_t i = 0; i < mcTex; i++)
        {
            size+=mpTex[i]->memSize();
        }
        return size;
    }

    uint32_t bytesPerLine() { return mpTex[0]->bytesPerLine(); }

    const NemuVHWAColorFormat &pixelFormat() { return mColorFormat; }

    uint32_t numComponents() {return mcTex;}

    NemuVHWATexture* component(uint32_t i) {return mpTex[i]; }

    const NemuVHWATextureImage *dst() { return mpDst;}
    const QRect& dstRect() { return mDstRect; }
    const QRect& srcRect() { return mSrcRect; }
    const NemuVHWAColorKey* dstCKey() { return mpDstCKey; }
    const NemuVHWAColorKey* srcCKey() { return mpSrcCKey; }
    bool notIntersectedMode() { return mbNotIntersected; }

    static uint32_t calcBytesPerLine(const NemuVHWAColorFormat & format, int width);
    static uint32_t calcMemSize(const NemuVHWAColorFormat & format, int width, int height);

#ifdef DEBUG_misha
    void dbgDump();
#endif

protected:
    static int setCKey(class NemuVHWAGlProgramVHWA * pProgram, const NemuVHWAColorFormat * pFormat, const NemuVHWAColorKey * pCKey, bool bDst);

    static bool matchCKeys(const NemuVHWAColorKey * pCKey1, const NemuVHWAColorKey * pCKey2)
    {
        return (pCKey1 == NULL && pCKey2 == NULL)
                || (*pCKey1 == *pCKey2);
    }

    void runDisplay(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect)
    {
        bind(pDst);

        draw(pDst, pDstRect, pSrcRect);
    }

    virtual void draw(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect);

    virtual uint32_t texCoord(GLenum tex, int x, int y)
    {
        uint32_t c = 1;
        mpTex[0]->multiTexCoord(tex, x, y);
        if(mColorFormat.fourcc() == FOURCC_YV12)
        {
            int x2 = x/2;
            int y2 = y/2;
            mpTex[1]->multiTexCoord(tex + 1, x2, y2);
            ++c;
        }
        return c;
    }

    virtual void bind(NemuVHWATextureImage * pPrimary);

    virtual uint32_t calcProgramType(NemuVHWATextureImage *pDst, const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected);

    virtual class NemuVHWAGlProgramVHWA * calcProgram(NemuVHWATextureImage *pDst, const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected);

    virtual int createDisplay(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
            const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected,
            GLuint *pDisplay, class NemuVHWAGlProgramVHWA ** ppProgram);

    int createSetDisplay(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
            const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected);

    virtual int createDisplayList(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
            const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected,
            GLuint *pDisplay);

    virtual void deleteDisplayList();

    virtual void updateCKeys(NemuVHWATextureImage * pDst, class NemuVHWAGlProgramVHWA * pProgram, const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey);
    virtual void updateSetCKeys(const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey);

    void internalSetDstCKey(const NemuVHWAColorKey * pDstCKey);
    void internalSetSrcCKey(const NemuVHWAColorKey * pSrcCKey);

    NemuVHWATexture *mpTex[3];
    uint32_t mcTex;
    GLuint mVisibleDisplay;
    class NemuVHWAGlProgramVHWA * mpProgram;
    class NemuVHWAGlProgramMngr * mProgramMngr;
    NemuVHWAColorFormat mColorFormat;

    /* display info */
    NemuVHWATextureImage *mpDst;
    QRect mDstRect;
    QRect mSrcRect;
    NemuVHWAColorKey * mpDstCKey;
    NemuVHWAColorKey * mpSrcCKey;
    NemuVHWAColorKey mDstCKey;
    NemuVHWAColorKey mSrcCKey;
    bool mbNotIntersected;
};

class NemuVHWATextureImagePBO : public NemuVHWATextureImage
{
public:
    NemuVHWATextureImagePBO(const QRect &size, const NemuVHWAColorFormat &format, class NemuVHWAGlProgramMngr * aMgr, NEMUVHWAIMG_TYPE flags) :
            NemuVHWATextureImage(size, format, aMgr, flags & (~NEMUVHWAIMG_PBO)),
            mPBO(0)
    {
    }

    virtual ~NemuVHWATextureImagePBO()
    {
        if(mPBO)
        {
            NEMUQGL_CHECKERR(
                    nemuglDeleteBuffers(1, &mPBO);
                    );
        }
    }

    virtual void init(uchar *pvMem)
    {
        NemuVHWATextureImage::init(pvMem);

        NEMUQGL_CHECKERR(
                nemuglGenBuffers(1, &mPBO);
                );
        mAddress = pvMem;

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

    virtual void update(const QRect * pRect)
    {
        NEMUQGL_CHECKERR(
                nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
        );

        GLvoid *buf;

        NEMUQGL_CHECKERR(
                buf = nemuglMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
                );
        Assert(buf);
        if(buf)
        {
#ifdef NEMUVHWADBG_RENDERCHECK
            uint32_t * pBuf32 = (uint32_t*)buf;
            uchar * pBuf8 = (uchar*)buf;
            for(uint32_t i = 0; i < mcTex; i++)
            {
                uint32_t dbgSetVal = 0x40404040 * (i+1);
                for(uint32_t k = 0; k < mpTex[i]->memSize()/sizeof(pBuf32[0]); k++)
                {
                    pBuf32[k] = dbgSetVal;
                }

                pBuf8 += mpTex[i]->memSize();
                pBuf32 = (uint32_t *)pBuf8;
            }
#else
            memcpy(buf, mAddress, memSize());
#endif

            bool unmapped;
            NEMUQGL_CHECKERR(
                    unmapped = nemuglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                    );

            Assert(unmapped); NOREF(unmapped);

            NemuVHWATextureImage::setAddress(0);

            NemuVHWATextureImage::update(NULL);

            NemuVHWATextureImage::setAddress(mAddress);

            nemuglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        }
        else
        {
            NEMUQGLLOGREL(("failed to map PBO, trying fallback to non-PBO approach\n"));

            NemuVHWATextureImage::setAddress(mAddress);

            NemuVHWATextureImage::update(pRect);
        }
    }

    virtual void setAddress(uchar *pvMem)
    {
        mAddress = pvMem;
    }
private:
    GLuint mPBO;
    uchar* mAddress;
};

class NemuVHWAHandleTable
{
public:
    NemuVHWAHandleTable(uint32_t initialSize);
    ~NemuVHWAHandleTable();
    uint32_t put(void * data);
    bool mapPut(uint32_t h, void * data);
    void* get(uint32_t h);
    void* remove(uint32_t h);
private:
    void doPut(uint32_t h, void * data);
    void doRemove(uint32_t h);
    void** mTable;
    uint32_t mcSize;
    uint32_t mcUsage;
    uint32_t mCursor;
};

/* data flow:
 * I. NON-Yinverted surface:
 * 1.direct memory update (paint, lock/unlock):
 *  mem->tex->fb
 * 2.blt
 *  srcTex->invFB->tex->fb
 *              |->mem
 *
 * II. Yinverted surface:
 * 1.direct memory update (paint, lock/unlock):
 *  mem->tex->fb
 * 2.blt
 *  srcTex->fb->tex
 *           |->mem
 *
 * III. flip support:
 * 1. Yinverted<->NON-YInverted conversion :
 *  mem->tex-(rotate model view, force LAZY complete fb update)->invFB->tex
 *  fb-->|                                                           |->mem
 * */
class NemuVHWASurfaceBase
{
public:
    NemuVHWASurfaceBase (class NemuVHWAImage *pImage,
            const QSize & aSize,
            const QRect & aTargRect,
            const QRect & aSrcRect,
            const QRect & aVisTargRect,
            NemuVHWAColorFormat & aColorFormat,
            NemuVHWAColorKey * pSrcBltCKey, NemuVHWAColorKey * pDstBltCKey,
            NemuVHWAColorKey * pSrcOverlayCKey, NemuVHWAColorKey * pDstOverlayCKey,
            NEMUVHWAIMG_TYPE aImgFlags);

    virtual ~NemuVHWASurfaceBase();

    void init (NemuVHWASurfaceBase * pPrimary, uchar *pvMem);

    void uninit();

    static void globalInit();

    int lock (const QRect * pRect, uint32_t flags);

    int unlock();

    void updatedMem (const QRect * aRect);

    bool performDisplay (NemuVHWASurfaceBase *pPrimary, bool bForce);

    void setRects (const QRect & aTargRect, const QRect & aSrcRect);
    void setTargRectPosition (const QPoint & aPoint);

    void updateVisibility (NemuVHWASurfaceBase *pPrimary, const QRect & aVisibleTargRect, bool bNotIntersected, bool bForce);

    static ulong calcBytesPerPixel (GLenum format, GLenum type);

    static GLsizei makePowerOf2 (GLsizei val);

    bool    addressAlocated() const { return mFreeAddress; }
    uchar * address() { return mAddress; }

    ulong   memSize();

    ulong width() const { return mRect.width();  }
    ulong height() const { return mRect.height(); }
    const QSize size() const {return mRect.size();}

    uint32_t fourcc() const {return mImage->pixelFormat().fourcc(); }

    ulong  bitsPerPixel() const { return mImage->pixelFormat().bitsPerPixel(); }
    ulong  bytesPerLine() const { return mImage->bytesPerLine(); }

    const NemuVHWAColorKey * dstBltCKey() const { return mpDstBltCKey; }
    const NemuVHWAColorKey * srcBltCKey() const { return mpSrcBltCKey; }
    const NemuVHWAColorKey * dstOverlayCKey() const { return mpDstOverlayCKey; }
    const NemuVHWAColorKey * defaultSrcOverlayCKey() const { return mpDefaultSrcOverlayCKey; }
    const NemuVHWAColorKey * defaultDstOverlayCKey() const { return mpDefaultDstOverlayCKey; }
    const NemuVHWAColorKey * srcOverlayCKey() const { return mpSrcOverlayCKey; }
    void resetDefaultSrcOverlayCKey() { mpSrcOverlayCKey = mpDefaultSrcOverlayCKey; }
    void resetDefaultDstOverlayCKey() { mpDstOverlayCKey = mpDefaultDstOverlayCKey; }

    void setDstBltCKey (const NemuVHWAColorKey * ckey)
    {
        if(ckey)
        {
            mDstBltCKey = *ckey;
            mpDstBltCKey = &mDstBltCKey;
        }
        else
        {
            mpDstBltCKey = NULL;
        }
    }

    void setSrcBltCKey (const NemuVHWAColorKey * ckey)
    {
        if(ckey)
        {
            mSrcBltCKey = *ckey;
            mpSrcBltCKey = &mSrcBltCKey;
        }
        else
        {
            mpSrcBltCKey = NULL;
        }
    }

    void setDefaultDstOverlayCKey (const NemuVHWAColorKey * ckey)
    {
        if(ckey)
        {
            mDefaultDstOverlayCKey = *ckey;
            mpDefaultDstOverlayCKey = &mDefaultDstOverlayCKey;
        }
        else
        {
            mpDefaultDstOverlayCKey = NULL;
        }
    }

    void setDefaultSrcOverlayCKey (const NemuVHWAColorKey * ckey)
    {
        if(ckey)
        {
            mDefaultSrcOverlayCKey = *ckey;
            mpDefaultSrcOverlayCKey = &mDefaultSrcOverlayCKey;
        }
        else
        {
            mpDefaultSrcOverlayCKey = NULL;
        }
    }

    void setOverriddenDstOverlayCKey (const NemuVHWAColorKey * ckey)
    {
        if(ckey)
        {
            mOverriddenDstOverlayCKey = *ckey;
            mpDstOverlayCKey = &mOverriddenDstOverlayCKey;
        }
        else
        {
            mpDstOverlayCKey = NULL;
        }
    }

    void setOverriddenSrcOverlayCKey (const NemuVHWAColorKey * ckey)
    {
        if(ckey)
        {
            mOverriddenSrcOverlayCKey = *ckey;
            mpSrcOverlayCKey = &mOverriddenSrcOverlayCKey;
        }
        else
        {
            mpSrcOverlayCKey = NULL;
        }
    }

    const NemuVHWAColorKey * getActiveSrcOverlayCKey()
    {
        return mpSrcOverlayCKey;
    }

    const NemuVHWAColorKey * getActiveDstOverlayCKey (NemuVHWASurfaceBase * pPrimary)
    {
        return mpDstOverlayCKey ? mpDefaultDstOverlayCKey : (pPrimary ? pPrimary->mpDstOverlayCKey : NULL);
    }

    const NemuVHWAColorFormat & pixelFormat() const { return mImage->pixelFormat(); }

    void setAddress(uchar * addr);

    const QRect& rect() const {return mRect;}
    const QRect& srcRect() const {return mSrcRect; }
    const QRect& targRect() const {return mTargRect; }
    class NemuVHWASurfList * getComplexList() {return mComplexList; }

    class NemuVHWAGlProgramMngr * getGlProgramMngr();

    uint32_t handle() const {return mHGHandle;}
    void setHandle(uint32_t h) {mHGHandle = h;}

    const NemuVHWADirtyRect & getDirtyRect() { return mUpdateMem2TexRect; }

    NemuVHWASurfaceBase * primary() { return mpPrimary; }
    void setPrimary(NemuVHWASurfaceBase *apPrimary) { mpPrimary = apPrimary; }
private:
    void setRectValues (const QRect & aTargRect, const QRect & aSrcRect);
    void setVisibleRectValues (const QRect & aVisTargRect);

    void setComplexList (NemuVHWASurfList *aComplexList) { mComplexList = aComplexList; }
    void initDisplay();

    bool synchTexMem (const QRect * aRect);

    int performBlt (const QRect * pDstRect, NemuVHWASurfaceBase * pSrcSurface, const QRect * pSrcRect, const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool blt);

    QRect mRect; /* == Inv FB size */

    QRect mSrcRect;
    QRect mTargRect; /* == Vis FB size */

    QRect mVisibleTargRect;
    QRect mVisibleSrcRect;

    class NemuVHWATextureImage * mImage;

    uchar * mAddress;

    NemuVHWAColorKey *mpSrcBltCKey;
    NemuVHWAColorKey *mpDstBltCKey;
    NemuVHWAColorKey *mpSrcOverlayCKey;
    NemuVHWAColorKey *mpDstOverlayCKey;

    NemuVHWAColorKey *mpDefaultDstOverlayCKey;
    NemuVHWAColorKey *mpDefaultSrcOverlayCKey;

    NemuVHWAColorKey mSrcBltCKey;
    NemuVHWAColorKey mDstBltCKey;
    NemuVHWAColorKey mOverriddenSrcOverlayCKey;
    NemuVHWAColorKey mOverriddenDstOverlayCKey;
    NemuVHWAColorKey mDefaultDstOverlayCKey;
    NemuVHWAColorKey mDefaultSrcOverlayCKey;

    int mLockCount;
    /* memory buffer not reflected in fm and texture, e.g if memory buffer is replaced or in case of lock/unlock  */
    NemuVHWADirtyRect mUpdateMem2TexRect;

    bool mFreeAddress;
    bool mbNotIntersected;

    class NemuVHWASurfList *mComplexList;

    NemuVHWASurfaceBase *mpPrimary;

    uint32_t mHGHandle;

    class NemuVHWAImage *mpImage;

#ifdef DEBUG
public:
    uint64_t cFlipsCurr;
    uint64_t cFlipsTarg;
#endif
    friend class NemuVHWASurfList;
};

typedef std::list <NemuVHWASurfaceBase*> SurfList;
typedef std::list <NemuVHWASurfList*> OverlayList;
typedef std::list <struct NEMUVHWACMD *> VHWACommandList;

class NemuVHWASurfList
{
public:

    NemuVHWASurfList() : mCurrent(NULL) {}

    void moveTo(NemuVHWASurfList *pDst)
    {
        for (SurfList::iterator it = mSurfaces.begin();
             it != mSurfaces.end(); it = mSurfaces.begin())
        {
            pDst->add((*it));
        }

        Assert(empty());
    }

    void add(NemuVHWASurfaceBase *pSurf)
    {
        NemuVHWASurfList * pOld = pSurf->getComplexList();
        if(pOld)
        {
            pOld->remove(pSurf);
        }
        mSurfaces.push_back(pSurf);
        pSurf->setComplexList(this);
    }
/*
    void clear()
    {
        for (SurfList::iterator it = mSurfaces.begin();
             it != mSurfaces.end(); ++ it)
        {
            (*it)->setComplexList(NULL);
        }
        mSurfaces.clear();
        mCurrent = NULL;
    }
*/
    size_t size() const {return mSurfaces.size(); }

    void remove(NemuVHWASurfaceBase *pSurf)
    {
        mSurfaces.remove(pSurf);
        pSurf->setComplexList(NULL);
        if(mCurrent == pSurf)
            mCurrent = NULL;
    }

    bool empty() { return mSurfaces.empty(); }

    void setCurrentVisible(NemuVHWASurfaceBase *pSurf)
    {
        mCurrent = pSurf;
    }

    NemuVHWASurfaceBase * current() { return mCurrent; }
    const SurfList & surfaces() const {return mSurfaces;}

private:

    SurfList mSurfaces;
    NemuVHWASurfaceBase* mCurrent;
};

class NemuVHWADisplay
{
public:
    NemuVHWADisplay() :
        mSurfVGA(NULL),
        mbDisplayPrimary(true)
//        ,
//        mSurfPrimary(NULL)
    {}

    NemuVHWASurfaceBase * setVGA(NemuVHWASurfaceBase * pVga)
    {
        NemuVHWASurfaceBase * old = mSurfVGA;
        mSurfVGA = pVga;
        if (!mPrimary.empty())
        {
            NemuVHWASurfList *pNewList = new NemuVHWASurfList();
            mPrimary.moveTo(pNewList);
            Assert(mPrimary.empty());
        }
        if(pVga)
        {
            Assert(!pVga->getComplexList());
            mPrimary.add(pVga);
            mPrimary.setCurrentVisible(pVga);
        }
        mOverlays.clear();
        return old;
    }

    NemuVHWASurfaceBase * updateVGA(NemuVHWASurfaceBase * pVga)
    {
        NemuVHWASurfaceBase * old = mSurfVGA;
        Assert(old);
        mSurfVGA = pVga;
        return old;
    }

    NemuVHWASurfaceBase * getVGA() const
    {
        return mSurfVGA;
    }

    NemuVHWASurfaceBase * getPrimary()
    {
        return mPrimary.current();
    }

    void addOverlay(NemuVHWASurfList * pSurf)
    {
        mOverlays.push_back(pSurf);
    }

    void checkAddOverlay(NemuVHWASurfList * pSurf)
    {
        if(!hasOverlay(pSurf))
            addOverlay(pSurf);
    }

    bool hasOverlay(NemuVHWASurfList * pSurf)
    {
        for (OverlayList::iterator it = mOverlays.begin();
             it != mOverlays.end(); ++ it)
        {
            if((*it) == pSurf)
            {
                return true;
            }
        }
        return false;
    }

    void removeOverlay(NemuVHWASurfList * pSurf)
    {
        mOverlays.remove(pSurf);
    }

    bool performDisplay(bool bForce)
    {
        NemuVHWASurfaceBase * pPrimary = mPrimary.current();

        if(mbDisplayPrimary)
        {
#ifdef DEBUG_misha
            /* should only display overlay now */
            AssertBreakpoint();
#endif
            bForce |= pPrimary->performDisplay(NULL, bForce);
        }

        for (OverlayList::const_iterator it = mOverlays.begin();
             it != mOverlays.end(); ++ it)
        {
            NemuVHWASurfaceBase * pOverlay = (*it)->current();
            if(pOverlay)
            {
                bForce |= pOverlay->performDisplay(pPrimary, bForce);
            }
        }
        return bForce;
    }

    bool isPrimary(NemuVHWASurfaceBase * pSurf) { return pSurf->getComplexList() == &mPrimary; }

    void setDisplayPrimary(bool bDisplay) { mbDisplayPrimary = bDisplay; }

    const OverlayList & overlays() const {return mOverlays;}
    const NemuVHWASurfList & primaries() const { return mPrimary; }

private:
    NemuVHWASurfaceBase *mSurfVGA;
    NemuVHWASurfList mPrimary;

    OverlayList mOverlays;

    bool mbDisplayPrimary;
};

typedef void (*PFNNEMUQGLFUNC)(void*, void*);

typedef enum
{
    NEMUVHWA_PIPECMD_PAINT = 1,
    NEMUVHWA_PIPECMD_VHWA,
    NEMUVHWA_PIPECMD_FUNC
}NEMUVHWA_PIPECMD_TYPE;

typedef struct NEMUVHWAFUNCCALLBACKINFO
{
    PFNNEMUQGLFUNC pfnCallback;
    void * pContext1;
    void * pContext2;
}NEMUVHWAFUNCCALLBACKINFO;

class NemuVHWACommandElement
{
public:
    void setVHWACmd(struct NEMUVHWACMD * pCmd)
    {
        mType = NEMUVHWA_PIPECMD_VHWA;
        u.mpCmd = pCmd;
    }

    void setPaintCmd(const QRect & aRect)
    {
        mType = NEMUVHWA_PIPECMD_PAINT;
        mRect = aRect;
    }

    void setFunc(const NEMUVHWAFUNCCALLBACKINFO & aOp)
    {
        mType = NEMUVHWA_PIPECMD_FUNC;
        u.mFuncCallback = aOp;
    }

    void setData(NEMUVHWA_PIPECMD_TYPE aType, void * pvData)
    {
        switch(aType)
        {
        case NEMUVHWA_PIPECMD_PAINT:
            setPaintCmd(*((QRect*)pvData));
            break;
        case NEMUVHWA_PIPECMD_VHWA:
            setVHWACmd((struct NEMUVHWACMD *)pvData);
            break;
        case NEMUVHWA_PIPECMD_FUNC:
            setFunc(*((NEMUVHWAFUNCCALLBACKINFO *)pvData));
            break;
        default:
            Assert(0);
            break;
        }
    }

    NEMUVHWA_PIPECMD_TYPE type() const {return mType;}
    const QRect & rect() const {return mRect;}
    struct NEMUVHWACMD * vhwaCmd() const {return u.mpCmd;}
    const NEMUVHWAFUNCCALLBACKINFO & func() const {return u.mFuncCallback; }

    RTLISTNODE ListNode;
private:
    NEMUVHWA_PIPECMD_TYPE mType;
    union
    {
        struct NEMUVHWACMD * mpCmd;
        NEMUVHWAFUNCCALLBACKINFO mFuncCallback;
    }u;
    QRect                 mRect;
};

class NemuVHWARefCounter
{
#define NEMUVHWA_INIFITE_WAITCOUNT (~0U)
public:
    NemuVHWARefCounter() : m_cRefs(0) {}
    NemuVHWARefCounter(uint32_t cRefs) : m_cRefs(cRefs) {}
    void inc() { ASMAtomicIncU32(&m_cRefs); }
    uint32_t dec()
    {
        uint32_t cRefs = ASMAtomicDecU32(&m_cRefs);
        Assert(cRefs < UINT32_MAX / 2);
        return cRefs;
    }

    uint32_t refs() { return ASMAtomicReadU32(&m_cRefs); }

    int wait0(RTMSINTERVAL ms = 1000, uint32_t cWaits = NEMUVHWA_INIFITE_WAITCOUNT)
    {
        int rc = VINF_SUCCESS;
        do
        {
            if (!refs())
                break;
            if (!cWaits)
            {
                rc = VERR_TIMEOUT;
                break;
            }
            if (cWaits != NEMUVHWA_INIFITE_WAITCOUNT)
                --cWaits;
            rc = RTThreadSleep(ms);
            AssertRC(rc);
            if (!RT_SUCCESS(rc))
                break;
        } while(1);
        return rc;
    }
private:
    volatile uint32_t m_cRefs;
};

class NemuVHWAEntriesCache;
class NemuVHWACommandElementProcessor
{
public:
    NemuVHWACommandElementProcessor();
    void init(QObject *pNotifyObject);
    ~NemuVHWACommandElementProcessor();
    void postCmd(NEMUVHWA_PIPECMD_TYPE aType, void * pvData);
    NemuVHWACommandElement *getCmd();
    void doneCmd();
    void reset(CDisplay *pDisplay);
    void setNotifyObject(QObject *pNotifyObject);
    int loadExec (struct SSMHANDLE * pSSM, uint32_t u32Version, void *pvVRAM);
    void saveExec (struct SSMHANDLE * pSSM, void *pvVRAM);
    void disable();
    void enable();
    void lock();
    void unlock();
private:
    RTCRITSECT mCritSect;
    RTLISTNODE mCommandList;
    QObject *m_pNotifyObject;
    NemuVHWARefCounter m_NotifyObjectRefs;
    NemuVHWACommandElement *mpCurCmd;
    bool mbResetting;
    uint32_t mcDisabled;
    NemuVHWAEntriesCache *m_pCmdEntryCache;
};

/* added to workaround this ** [Nemu|UI] duplication */
class NemuFBSizeInfo
{
public:

    NemuFBSizeInfo() {}
    template<class T> NemuFBSizeInfo(T *pFb) :
        m_visualState(pFb->visualState()),
        mPixelFormat(pFb->pixelFormat()), mVRAM(pFb->address()), mBitsPerPixel(pFb->bitsPerPixel()),
        mBytesPerLine(pFb->bytesPerLine()), mWidth(pFb->width()), mHeight(pFb->height()),
        m_dScaleFactor(pFb->scaleFactor()), m_scaledSize(pFb->scaledSize()), m_fUseUnscaledHiDPIOutput(pFb->useUnscaledHiDPIOutput()),
        mUsesGuestVram(true) {}

    NemuFBSizeInfo(UIVisualStateType visualState,
                   ulong aPixelFormat, uchar *aVRAM,
                   ulong aBitsPerPixel, ulong aBytesPerLine,
                   ulong aWidth, ulong aHeight,
                   double dScaleFactor, const QSize &scaledSize, bool fUseUnscaledHiDPIOutput,
                   bool bUsesGuestVram) :
        m_visualState(visualState),
        mPixelFormat(aPixelFormat), mVRAM(aVRAM), mBitsPerPixel(aBitsPerPixel),
        mBytesPerLine(aBytesPerLine), mWidth(aWidth), mHeight(aHeight),
        m_dScaleFactor(dScaleFactor), m_scaledSize(scaledSize), m_fUseUnscaledHiDPIOutput(fUseUnscaledHiDPIOutput),
        mUsesGuestVram(bUsesGuestVram) {}

    UIVisualStateType visualState() const { return m_visualState; }
    ulong pixelFormat() const { return mPixelFormat; }
    uchar *VRAM() const { return mVRAM; }
    ulong bitsPerPixel() const { return mBitsPerPixel; }
    ulong bytesPerLine() const { return mBytesPerLine; }
    ulong width() const { return mWidth; }
    ulong height() const { return mHeight; }
    double scaleFactor() const { return m_dScaleFactor; }
    QSize scaledSize() const { return m_scaledSize; }
    bool useUnscaledHiDPIOutput() const { return m_fUseUnscaledHiDPIOutput; }
    bool usesGuestVram() const {return mUsesGuestVram;}

private:

    UIVisualStateType m_visualState;
    ulong mPixelFormat;
    uchar *mVRAM;
    ulong mBitsPerPixel;
    ulong mBytesPerLine;
    ulong mWidth;
    ulong mHeight;
    double m_dScaleFactor;
    QSize m_scaledSize;
    bool m_fUseUnscaledHiDPIOutput;
    bool mUsesGuestVram;
};

class NemuVHWAImage
{
public:
    NemuVHWAImage ();
    ~NemuVHWAImage();

    int init(NemuVHWASettings *aSettings);
#ifdef NEMU_WITH_VIDEOHWACCEL
    uchar *nemuVRAMAddressFromOffset(uint64_t offset);
    uint64_t nemuVRAMOffsetFromAddress(uchar* addr);
    uint64_t nemuVRAMOffset(NemuVHWASurfaceBase * pSurf);

    void vhwaSaveExec(struct SSMHANDLE * pSSM);
    static void vhwaSaveExecVoid(struct SSMHANDLE * pSSM);
    static int vhwaLoadExec(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t u32Version);

    int vhwaSurfaceCanCreate(struct NEMUVHWACMD_SURF_CANCREATE *pCmd);
    int vhwaSurfaceCreate(struct NEMUVHWACMD_SURF_CREATE *pCmd);
#ifdef NEMU_WITH_WDDM
    int vhwaSurfaceGetInfo(struct NEMUVHWACMD_SURF_GETINFO *pCmd);
#endif
    int vhwaSurfaceDestroy(struct NEMUVHWACMD_SURF_DESTROY *pCmd);
    int vhwaSurfaceLock(struct NEMUVHWACMD_SURF_LOCK *pCmd);
    int vhwaSurfaceUnlock(struct NEMUVHWACMD_SURF_UNLOCK *pCmd);
    int vhwaSurfaceBlt(struct NEMUVHWACMD_SURF_BLT *pCmd);
    int vhwaSurfaceFlip(struct NEMUVHWACMD_SURF_FLIP *pCmd);
    int vhwaSurfaceColorFill(struct NEMUVHWACMD_SURF_COLORFILL *pCmd);
    int vhwaSurfaceOverlayUpdate(struct NEMUVHWACMD_SURF_OVERLAY_UPDATE *pCmf);
    int vhwaSurfaceOverlaySetPosition(struct NEMUVHWACMD_SURF_OVERLAY_SETPOSITION *pCmd);
    int vhwaSurfaceColorkeySet(struct NEMUVHWACMD_SURF_COLORKEY_SET *pCmd);
    int vhwaQueryInfo1(struct NEMUVHWACMD_QUERYINFO1 *pCmd);
    int vhwaQueryInfo2(struct NEMUVHWACMD_QUERYINFO2 *pCmd);
    int vhwaConstruct(struct NEMUVHWACMD_HH_CONSTRUCT *pCmd);

    void *vramBase() { return mpvVRAM; }
    uint32_t vramSize() { return mcbVRAM; }

    bool hasSurfaces() const;
    bool hasVisibleOverlays();
    QRect overlaysRectUnion();
    QRect overlaysRectIntersection();
#endif

    static const QGLFormat & nemuGLFormat();

    int reset(VHWACommandList * pCmdList);

    int nemuFbWidth() {return mDisplay.getVGA()->width(); }
    int nemuFbHeight() {return mDisplay.getVGA()->height(); }
    bool isInitialized() {return mDisplay.getVGA() != NULL; }

    void resize(const NemuFBSizeInfo & size);

    class NemuVHWAGlProgramMngr * nemuVHWAGetGlProgramMngr() { return mpMngr; }

    NemuVHWASurfaceBase * vgaSurface() { return mDisplay.getVGA(); }

#ifdef NEMUVHWA_OLD_COORD
    static void doSetupMatrix(const QSize & aSize, bool bInverted);
#endif

    void nemuDoUpdateViewport(const QRect & aRect);
    void nemuDoUpdateRect(const QRect * pRect);

    const QRect & nemuViewport() const {return mViewport;}

#ifdef NEMUVHWA_PROFILE_FPS
    void reportNewFrame() { mbNewFrame = true; }
#endif

    bool performDisplay(bool bForce)
    {
        bForce = mDisplay.performDisplay(bForce | mRepaintNeeded);

#ifdef NEMUVHWA_PROFILE_FPS
        if(mbNewFrame)
        {
            mFPSCounter.frame();
            double fps = mFPSCounter.fps();
            if(!(mFPSCounter.frames() % 31))
            {
                LogRel(("fps: %f\n", fps));
            }
            mbNewFrame = false;
        }
#endif
        return bForce;
    }

    static void pushSettingsAndSetupViewport(const QSize &display, const QRect &viewport)
    {
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        setupMatricies(display, false);
        adjustViewport(display, viewport);
    }

    static void popSettingsAfterSetupViewport()
    {
        glPopAttrib();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

private:
    static void setupMatricies(const QSize &display, bool bInvert);
    static void adjustViewport(const QSize &display, const QRect &viewport);


#ifdef NEMUQGL_DBG_SURF
    void nemuDoTestSurfaces(void *context);
#endif
#ifdef NEMU_WITH_VIDEOHWACCEL

    void nemuCheckUpdateAddress(NemuVHWASurfaceBase * pSurface, uint64_t offset)
    {
        if (pSurface->addressAlocated())
        {
            Assert(!mDisplay.isPrimary(pSurface));
            uchar * addr = nemuVRAMAddressFromOffset(offset);
            if (addr)
            {
                pSurface->setAddress(addr);
            }
        }
    }

    int vhwaSaveSurface(struct SSMHANDLE * pSSM, NemuVHWASurfaceBase *pSurf, uint32_t surfCaps);
    static int vhwaLoadSurface(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t cBackBuffers, uint32_t u32Version);
    int vhwaSaveOverlayData(struct SSMHANDLE * pSSM, NemuVHWASurfaceBase *pSurf, bool bVisible);
    static int vhwaLoadOverlayData(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t u32Version);
    static int vhwaLoadVHWAEnable(VHWACommandList * pCmdList);

    void vhwaDoSurfaceOverlayUpdate(NemuVHWASurfaceBase *pDstSurf, NemuVHWASurfaceBase *pSrcSurf, struct NEMUVHWACMD_SURF_OVERLAY_UPDATE *pCmd);
#endif

    NemuVHWADisplay mDisplay;

    NemuVHWASurfaceBase* handle2Surface(uint32_t h)
    {
        NemuVHWASurfaceBase* pSurf = (NemuVHWASurfaceBase*)mSurfHandleTable.get(h);
        Assert(pSurf);
        return pSurf;
    }

    NemuVHWAHandleTable mSurfHandleTable;

    bool mRepaintNeeded;

    QRect mViewport;

    NemuVHWASurfList *mConstructingList;
    int32_t mcRemaining2Contruct;

    class NemuVHWAGlProgramMngr *mpMngr;

    NemuVHWASettings *mSettings;

    void    *mpvVRAM;
    uint32_t mcbVRAM;

#ifdef NEMUVHWA_PROFILE_FPS
    NemuVHWADbgTimer mFPSCounter;
    bool mbNewFrame;
#endif
};

class NemuGLWgt : public QGLWidget
{
public:
    NemuGLWgt(NemuVHWAImage * pImage,
            QWidget* parent, const QGLWidget* shareWidget);

protected:
    void paintGL()
    {
        mpImage->performDisplay(true);
    }
private:
    NemuVHWAImage * mpImage;
};

class NemuVHWAFBO
{
public:
    NemuVHWAFBO() :
            mFBO(0)
    {}

    ~NemuVHWAFBO()
    {
        if(mFBO)
        {
            nemuglDeleteFramebuffers(1, &mFBO);
        }
    }

    void init()
    {
        NEMUQGL_CHECKERR(
                nemuglGenFramebuffers(1, &mFBO);
        );
    }

    void bind()
    {
        NEMUQGL_CHECKERR(
            nemuglBindFramebuffer(GL_FRAMEBUFFER, mFBO);
        );
    }

    void unbind()
    {
        NEMUQGL_CHECKERR(
            nemuglBindFramebuffer(GL_FRAMEBUFFER, 0);
        );
    }

    void attachBound(NemuVHWATexture *pTex)
    {
        NEMUQGL_CHECKERR(
                nemuglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, pTex->texTarget(), pTex->texture(), 0);
        );
    }

private:
    GLuint mFBO;
};

template <class T>
class NemuVHWATextureImageFBO : public T
{
public:
    NemuVHWATextureImageFBO(const QRect &size, const NemuVHWAColorFormat &format, class NemuVHWAGlProgramMngr * aMgr, NEMUVHWAIMG_TYPE flags) :
            T(size, format, aMgr, flags & (~(NEMUVHWAIMG_FBO | NEMUVHWAIMG_LINEAR))),
            mFBOTex(size, NemuVHWAColorFormat(32, 0xff0000, 0xff00, 0xff), aMgr, (flags & (~NEMUVHWAIMG_FBO))),
            mpvFBOTexMem(NULL)
    {
    }

    virtual ~NemuVHWATextureImageFBO()
    {
        if(mpvFBOTexMem)
            free(mpvFBOTexMem);
    }

    virtual void init(uchar *pvMem)
    {
        mFBO.init();
        mpvFBOTexMem = (uchar*)malloc(mFBOTex.memSize());
        mFBOTex.init(mpvFBOTexMem);
        T::init(pvMem);
        mFBO.bind();
        mFBO.attachBound(mFBOTex.component(0));
        mFBO.unbind();
    }

    virtual int createDisplay(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
            const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected,
            GLuint *pDisplay, class NemuVHWAGlProgramVHWA ** ppProgram)
    {
        T::createDisplay(NULL, &mFBOTex.rect(), &rect(),
                NULL, NULL, false,
                pDisplay, ppProgram);

        return mFBOTex.initDisplay(pDst, pDstRect, pSrcRect,
                pDstCKey, pSrcCKey, bNotIntersected);
    }

    virtual void update(const QRect * pRect)
    {
        T::update(pRect);

        NemuVHWAImage::pushSettingsAndSetupViewport(rect().size(), rect());
        mFBO.bind();
        T::display();
        mFBO.unbind();
        NemuVHWAImage::popSettingsAfterSetupViewport();
    }

    virtual void display(NemuVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
            const NemuVHWAColorKey * pDstCKey, const NemuVHWAColorKey * pSrcCKey, bool bNotIntersected)
    {
        mFBOTex.display(pDst, pDstRect, pSrcRect, pDstCKey, pSrcCKey, bNotIntersected);
    }

    virtual void display()
    {
        mFBOTex.display();
    }

    const QRect &rect() { return T::rect(); }
private:
    NemuVHWAFBO mFBO;
    NemuVHWATextureImage mFBOTex;
    uchar * mpvFBOTexMem;
};

class NemuQGLOverlay
{
public:
    NemuQGLOverlay();
    void init(QWidget *pViewport, QObject *pPostEventObject, CSession * aSession, uint32_t id);
    ~NemuQGLOverlay()
    {
        if (mpShareWgt)
            delete mpShareWgt;
    }

    void updateAttachment(QWidget *pViewport, QObject *pPostEventObject);

    int onVHWACommand (struct NEMUVHWACMD * pCommand);

    void onVHWACommandEvent (QEvent * pEvent);

    /**
     * to be called on NotifyUpdate framebuffer call
     * @return true if the request was processed & should not be forwarded to the framebuffer
     * false - otherwise */
    bool onNotifyUpdate (ULONG aX, ULONG aY,
                             ULONG aW, ULONG aH);

    void onNotifyUpdateIgnore (ULONG aX, ULONG aY,
                             ULONG aW, ULONG aH)
    {
        Q_UNUSED(aX);
        Q_UNUSED(aY);
        Q_UNUSED(aW);
        Q_UNUSED(aH);
        /* @todo: we actually should not miss notify updates, since we need to update the texture on it */
    }

    void onResizeEventPostprocess (const NemuFBSizeInfo &re, const QPoint & topLeft);

    void onViewportResized (QResizeEvent * /*re*/)
    {
        nemuDoCheckUpdateViewport();
        mGlCurrent = false;
    }

    void onViewportScrolled (const QPoint & newTopLeft)
    {
        mContentsTopLeft = newTopLeft;
        nemuDoCheckUpdateViewport();
        mGlCurrent = false;
    }

    static bool isAcceleration2DVideoAvailable();

    /** additional video memory required for the best 2D support performance
     *  total amount of VRAM required is thus calculated as requiredVideoMemory + required2DOffscreenVideoMemory  */
    static quint64 required2DOffscreenVideoMemory();

    /* not supposed to be called by clients */
    int vhwaLoadExec (struct SSMHANDLE * pSSM, uint32_t u32Version);
    void vhwaSaveExec (struct SSMHANDLE * pSSM);
private:
    int vhwaSurfaceUnlock (struct NEMUVHWACMD_SURF_UNLOCK *pCmd);

    void repaintMain();
    void repaintOverlay()
    {
        if(mNeedOverlayRepaint)
        {
            mNeedOverlayRepaint = false;
            performDisplayOverlay();
        }
        if(mNeedSetVisible)
        {
            mNeedSetVisible = false;
            mpOverlayWgt->setVisible (true);
        }
    }
    void repaint()
    {
        repaintOverlay();
        repaintMain();
    }

    void makeCurrent()
    {
        if (!mGlCurrent)
        {
            mGlCurrent = true;
            mpOverlayWgt->makeCurrent();
        }
    }

    void performDisplayOverlay()
    {
        if (mOverlayVisible)
        {
            makeCurrent();
            if (mOverlayImage.performDisplay(false))
                mpOverlayWgt->swapBuffers();
        }
    }

    void nemuSetGlOn (bool on);
    bool nemuGetGlOn() { return mGlOn; }
    bool nemuSynchGl();
    void nemuDoVHWACmdExec(void *cmd);
    void nemuShowOverlay (bool show);
    void nemuDoCheckUpdateViewport();
    void nemuDoVHWACmd (void *cmd);
    void addMainDirtyRect (const QRect & aRect);
    void nemuCheckUpdateOverlay (const QRect & rect);
    void processCmd (NemuVHWACommandElement * pCmd);

    int vhwaConstruct (struct NEMUVHWACMD_HH_CONSTRUCT *pCmd);

    int reset();

    int resetGl();

    void initGl();

    NemuGLWgt *mpOverlayWgt;
    NemuVHWAImage mOverlayImage;
    QWidget *mpViewport;
    bool mGlOn;
    bool mOverlayWidgetVisible;
    bool mOverlayVisible;
    bool mGlCurrent;
    bool mProcessingCommands;
    bool mNeedOverlayRepaint;
    bool mNeedSetVisible;
    QRect mOverlayViewport;
    NemuVHWADirtyRect mMainDirtyRect;

    NemuVHWACommandElementProcessor mCmdPipe;

    /* this is used in saved state restore to postpone surface restoration
     * till the framebuffer size is restored */
    VHWACommandList mOnResizeCmdList;

    NemuVHWASettings mSettings;
    CSession * mpSession;

    NemuFBSizeInfo mSizeInfo;
    NemuFBSizeInfo mPostponedResize;
    QPoint mContentsTopLeft;

    QGLWidget *mpShareWgt;

    uint32_t m_id;
};

#endif

#endif /* #ifndef __NemuFBOverlay_h__ */
