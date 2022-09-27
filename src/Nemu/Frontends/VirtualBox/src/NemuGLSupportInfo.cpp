/* $Id: NemuGLSupportInfo.cpp $ */
/** @file
 * Nemu Qt GUI - OpenGL support info used for 2D support detection.
 */

/*
 * Copyright (C) 2009-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef NEMU_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !NEMU_WITH_PRECOMPILED_HEADERS */

# include <iprt/assert.h>
# include <iprt/log.h>
# include <iprt/env.h>
# include <iprt/param.h>
# include <iprt/path.h>
# include <iprt/process.h>
# include <iprt/string.h>
# include <iprt/time.h>
# include <iprt/thread.h>

# include <QGLWidget>

# include <Nemu/NemuGL2D.h>
# include "NemuFBOverlayCommon.h"
#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */

#include <iprt/err.h>

#include <QGLContext>


/*****************/

/* functions */

PFNNEMUVHWA_ACTIVE_TEXTURE nemuglActiveTexture = NULL;
PFNNEMUVHWA_MULTI_TEX_COORD2I nemuglMultiTexCoord2i = NULL;
PFNNEMUVHWA_MULTI_TEX_COORD2D nemuglMultiTexCoord2d = NULL;
PFNNEMUVHWA_MULTI_TEX_COORD2F nemuglMultiTexCoord2f = NULL;


PFNNEMUVHWA_CREATE_SHADER   nemuglCreateShader  = NULL;
PFNNEMUVHWA_SHADER_SOURCE   nemuglShaderSource  = NULL;
PFNNEMUVHWA_COMPILE_SHADER  nemuglCompileShader = NULL;
PFNNEMUVHWA_DELETE_SHADER   nemuglDeleteShader  = NULL;

PFNNEMUVHWA_CREATE_PROGRAM  nemuglCreateProgram = NULL;
PFNNEMUVHWA_ATTACH_SHADER   nemuglAttachShader  = NULL;
PFNNEMUVHWA_DETACH_SHADER   nemuglDetachShader  = NULL;
PFNNEMUVHWA_LINK_PROGRAM    nemuglLinkProgram   = NULL;
PFNNEMUVHWA_USE_PROGRAM     nemuglUseProgram    = NULL;
PFNNEMUVHWA_DELETE_PROGRAM  nemuglDeleteProgram = NULL;

PFNNEMUVHWA_IS_SHADER       nemuglIsShader      = NULL;
PFNNEMUVHWA_GET_SHADERIV    nemuglGetShaderiv   = NULL;
PFNNEMUVHWA_IS_PROGRAM      nemuglIsProgram     = NULL;
PFNNEMUVHWA_GET_PROGRAMIV   nemuglGetProgramiv  = NULL;
PFNNEMUVHWA_GET_ATTACHED_SHADERS nemuglGetAttachedShaders = NULL;
PFNNEMUVHWA_GET_SHADER_INFO_LOG  nemuglGetShaderInfoLog   = NULL;
PFNNEMUVHWA_GET_PROGRAM_INFO_LOG nemuglGetProgramInfoLog  = NULL;

PFNNEMUVHWA_GET_UNIFORM_LOCATION nemuglGetUniformLocation = NULL;

PFNNEMUVHWA_UNIFORM1F nemuglUniform1f = NULL;
PFNNEMUVHWA_UNIFORM2F nemuglUniform2f = NULL;
PFNNEMUVHWA_UNIFORM3F nemuglUniform3f = NULL;
PFNNEMUVHWA_UNIFORM4F nemuglUniform4f = NULL;

PFNNEMUVHWA_UNIFORM1I nemuglUniform1i = NULL;
PFNNEMUVHWA_UNIFORM2I nemuglUniform2i = NULL;
PFNNEMUVHWA_UNIFORM3I nemuglUniform3i = NULL;
PFNNEMUVHWA_UNIFORM4I nemuglUniform4i = NULL;

PFNNEMUVHWA_GEN_BUFFERS nemuglGenBuffers = NULL;
PFNNEMUVHWA_DELETE_BUFFERS nemuglDeleteBuffers = NULL;
PFNNEMUVHWA_BIND_BUFFER nemuglBindBuffer = NULL;
PFNNEMUVHWA_BUFFER_DATA nemuglBufferData = NULL;
PFNNEMUVHWA_MAP_BUFFER nemuglMapBuffer = NULL;
PFNNEMUVHWA_UNMAP_BUFFER nemuglUnmapBuffer = NULL;

PFNNEMUVHWA_IS_FRAMEBUFFER nemuglIsFramebuffer = NULL;
PFNNEMUVHWA_BIND_FRAMEBUFFER nemuglBindFramebuffer = NULL;
PFNNEMUVHWA_DELETE_FRAMEBUFFERS nemuglDeleteFramebuffers = NULL;
PFNNEMUVHWA_GEN_FRAMEBUFFERS nemuglGenFramebuffers = NULL;
PFNNEMUVHWA_CHECK_FRAMEBUFFER_STATUS nemuglCheckFramebufferStatus = NULL;
PFNNEMUVHWA_FRAMEBUFFER_TEXTURE1D nemuglFramebufferTexture1D = NULL;
PFNNEMUVHWA_FRAMEBUFFER_TEXTURE2D nemuglFramebufferTexture2D = NULL;
PFNNEMUVHWA_FRAMEBUFFER_TEXTURE3D nemuglFramebufferTexture3D = NULL;
PFNNEMUVHWA_GET_FRAMEBUFFER_ATTACHMENT_PARAMETRIV nemuglGetFramebufferAttachmentParameteriv = NULL;

#define NEMUVHWA_GETPROCADDRESS(_c, _t, _n) ((_t)(uintptr_t)(_c).getProcAddress(QString(_n)))

#define NEMUVHWA_PFNINIT_SAME(_c, _t, _v, _rc) \
    do { \
        if((nemugl##_v = NEMUVHWA_GETPROCADDRESS(_c, _t, "gl"#_v)) == NULL) \
        { \
            NEMUQGLLOGREL(("ERROR: '%s' function not found\n", "gl"#_v));\
            AssertBreakpoint(); \
            if((nemugl##_v = NEMUVHWA_GETPROCADDRESS(_c, _t, "gl"#_v"ARB")) == NULL) \
            { \
                NEMUQGLLOGREL(("ERROR: '%s' function not found\n", "gl"#_v"ARB"));\
                AssertBreakpoint(); \
                if((nemugl##_v = NEMUVHWA_GETPROCADDRESS(_c, _t, "gl"#_v"EXT")) == NULL) \
                { \
                    NEMUQGLLOGREL(("ERROR: '%s' function not found\n", "gl"#_v"EXT"));\
                    AssertBreakpoint(); \
                    (_rc)++; \
                } \
            } \
        } \
    }while(0)

#define NEMUVHWA_PFNINIT(_c, _t, _v, _f,_rc) \
    do { \
        if((nemugl##_v = NEMUVHWA_GETPROCADDRESS(_c, _t, "gl"#_f)) == NULL) \
        { \
            NEMUQGLLOGREL(("ERROR: '%s' function is not found\n", "gl"#_f));\
            AssertBreakpoint(); \
            (_rc)++; \
        } \
    }while(0)

#define NEMUVHWA_PFNINIT_OBJECT_ARB(_c, _t, _v, _rc) \
        do { \
            if((nemugl##_v = NEMUVHWA_GETPROCADDRESS(_c, _t, "gl"#_v"ObjectARB")) == NULL) \
            { \
                NEMUQGLLOGREL(("ERROR: '%s' function is not found\n", "gl"#_v"ObjectARB"));\
                AssertBreakpoint(); \
                (_rc)++; \
            } \
        }while(0)

#define NEMUVHWA_PFNINIT_ARB(_c, _t, _v, _rc) \
        do { \
            if((nemugl##_v = NEMUVHWA_GETPROCADDRESS(_c, _t, "gl"#_v"ARB")) == NULL) \
            { \
                NEMUQGLLOGREL(("ERROR: '%s' function is not found\n", "gl"#_v"ARB"));\
                AssertBreakpoint(); \
                (_rc)++; \
            } \
        }while(0)

#define NEMUVHWA_PFNINIT_EXT(_c, _t, _v, _rc) \
        do { \
            if((nemugl##_v = NEMUVHWA_GETPROCADDRESS(_c, _t, "gl"#_v"EXT")) == NULL) \
            { \
                NEMUQGLLOGREL(("ERROR: '%s' function is not found\n", "gl"#_v"EXT"));\
                AssertBreakpoint(); \
                (_rc)++; \
            } \
        }while(0)

static int nemuVHWAGlParseSubver(const GLubyte * ver, const GLubyte ** pNext, bool bSpacePrefixAllowed)
{
    int val = 0;

    for(;;++ver)
    {
        if(*ver >= '0' && *ver <= '9')
        {
            if(!val)
            {
                if(*ver == '0')
                    continue;
            }
            else
            {
                val *= 10;
            }
            val += *ver - '0';
        }
        else if(*ver == '.')
        {
            *pNext = ver+1;
            break;
        }
        else if(*ver == '\0')
        {
            *pNext = NULL;
            break;
        }
        else if(*ver == ' ' || *ver == '\t' ||  *ver == 0x0d || *ver == 0x0a)
        {
            if(bSpacePrefixAllowed)
            {
                if(!val)
                {
                    continue;
                }
            }

            /* treat this as the end ov version string */
            *pNext = NULL;
            break;
        }
        else
        {
            Assert(0);
            val = -1;
            break;
        }
    }

    return val;
}

/* static */
int NemuGLInfo::parseVersion(const GLubyte * ver)
{
    int iVer = nemuVHWAGlParseSubver(ver, &ver, true);
    if(iVer)
    {
        iVer <<= 16;
        if(ver)
        {
            int tmp = nemuVHWAGlParseSubver(ver, &ver, false);
            if(tmp >= 0)
            {
                iVer |= tmp << 8;
                if(ver)
                {
                    tmp = nemuVHWAGlParseSubver(ver, &ver, false);
                    if(tmp >= 0)
                    {
                        iVer |= tmp;
                    }
                    else
                    {
                        Assert(0);
                        iVer = -1;
                    }
                }
            }
            else
            {
                Assert(0);
                iVer = -1;
            }
        }
    }
    return iVer;
}

void NemuGLInfo::init(const QGLContext * pContext)
{
    if(mInitialized)
        return;

    mInitialized = true;

    if (!QGLFormat::hasOpenGL())
    {
        NEMUQGLLOGREL (("no gl support available\n"));
        return;
    }

//    pContext->makeCurrent();

    const GLubyte * str;
    NEMUQGL_CHECKERR(
            str = glGetString(GL_VERSION);
            );

    if(str)
    {
        NEMUQGLLOGREL (("gl version string: 0%s\n", str));

        mGLVersion = parseVersion (str);
        Assert(mGLVersion > 0);
        if(mGLVersion < 0)
        {
            mGLVersion = 0;
        }
        else
        {
            NEMUQGLLOGREL (("gl version: 0x%x\n", mGLVersion));
            NEMUQGL_CHECKERR(
                    str = glGetString (GL_EXTENSIONS);
                    );

            NEMUQGLLOGREL (("gl extensions: %s\n", str));

            const char * pos = strstr((const char *)str, "GL_ARB_multitexture");
            m_GL_ARB_multitexture = pos != NULL;
            NEMUQGLLOGREL (("GL_ARB_multitexture: %d\n", m_GL_ARB_multitexture));

            pos = strstr((const char *)str, "GL_ARB_shader_objects");
            m_GL_ARB_shader_objects = pos != NULL;
            NEMUQGLLOGREL (("GL_ARB_shader_objects: %d\n", m_GL_ARB_shader_objects));

            pos = strstr((const char *)str, "GL_ARB_fragment_shader");
            m_GL_ARB_fragment_shader = pos != NULL;
            NEMUQGLLOGREL (("GL_ARB_fragment_shader: %d\n", m_GL_ARB_fragment_shader));

            pos = strstr((const char *)str, "GL_ARB_pixel_buffer_object");
            m_GL_ARB_pixel_buffer_object = pos != NULL;
            NEMUQGLLOGREL (("GL_ARB_pixel_buffer_object: %d\n", m_GL_ARB_pixel_buffer_object));

            pos = strstr((const char *)str, "GL_ARB_texture_rectangle");
            m_GL_ARB_texture_rectangle = pos != NULL;
            NEMUQGLLOGREL (("GL_ARB_texture_rectangle: %d\n", m_GL_ARB_texture_rectangle));

            pos = strstr((const char *)str, "GL_EXT_texture_rectangle");
            m_GL_EXT_texture_rectangle = pos != NULL;
            NEMUQGLLOGREL (("GL_EXT_texture_rectangle: %d\n", m_GL_EXT_texture_rectangle));

            pos = strstr((const char *)str, "GL_NV_texture_rectangle");
            m_GL_NV_texture_rectangle = pos != NULL;
            NEMUQGLLOGREL (("GL_NV_texture_rectangle: %d\n", m_GL_NV_texture_rectangle));

            pos = strstr((const char *)str, "GL_ARB_texture_non_power_of_two");
            m_GL_ARB_texture_non_power_of_two = pos != NULL;
            NEMUQGLLOGREL (("GL_ARB_texture_non_power_of_two: %d\n", m_GL_ARB_texture_non_power_of_two));

            pos = strstr((const char *)str, "GL_EXT_framebuffer_object");
            m_GL_EXT_framebuffer_object = pos != NULL;
            NEMUQGLLOGREL (("GL_EXT_framebuffer_object: %d\n", m_GL_EXT_framebuffer_object));


            initExtSupport(*pContext);
        }
    }
    else
    {
        NEMUQGLLOGREL (("failed to make the context current, treating as unsupported\n"));
    }
}

void NemuGLInfo::initExtSupport(const QGLContext & context)
{
    int rc = 0;
    do
    {
        rc = 0;
        mMultiTexNumSupported = 1; /* default, 1 means not supported */
        if(mGLVersion >= 0x010201) /* ogl >= 1.2.1 */
        {
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_ACTIVE_TEXTURE, ActiveTexture, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_MULTI_TEX_COORD2I, MultiTexCoord2i, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_MULTI_TEX_COORD2D, MultiTexCoord2d, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_MULTI_TEX_COORD2F, MultiTexCoord2f, rc);
        }
        else if(m_GL_ARB_multitexture)
        {
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_ACTIVE_TEXTURE, ActiveTexture, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_MULTI_TEX_COORD2I, MultiTexCoord2i, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_MULTI_TEX_COORD2D, MultiTexCoord2d, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_MULTI_TEX_COORD2F, MultiTexCoord2f, rc);
        }
        else
        {
            break;
        }

        if(RT_FAILURE(rc))
            break;

        GLint maxCoords, maxUnits;
        glGetIntegerv(GL_MAX_TEXTURE_COORDS, &maxCoords);
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits);

        NEMUQGLLOGREL(("Max Tex Coords (%d), Img Units (%d)\n", maxCoords, maxUnits));
        /* take the minimum of those */
        if(maxUnits < maxCoords)
            maxCoords = maxUnits;
        if(maxUnits < 2)
        {
            NEMUQGLLOGREL(("Max Tex Coord or Img Units < 2 disabling MultiTex support\n"));
            break;
        }

        mMultiTexNumSupported = maxUnits;
    }while(0);


    do
    {
        rc = 0;
        mPBOSupported = false;

        if(m_GL_ARB_pixel_buffer_object)
        {
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_GEN_BUFFERS, GenBuffers, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_DELETE_BUFFERS, DeleteBuffers, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_BIND_BUFFER, BindBuffer, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_BUFFER_DATA, BufferData, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_MAP_BUFFER, MapBuffer, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNMAP_BUFFER, UnmapBuffer, rc);
        }
        else
        {
            break;
        }

        if(RT_FAILURE(rc))
            break;

        mPBOSupported = true;
    } while(0);

    do
    {
        rc = 0;
        mFragmentShaderSupported = false;

        if(mGLVersion >= 0x020000)  /* if ogl >= 2.0*/
        {
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_CREATE_SHADER, CreateShader, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_SHADER_SOURCE, ShaderSource, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_COMPILE_SHADER, CompileShader, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_DELETE_SHADER, DeleteShader, rc);

            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_CREATE_PROGRAM, CreateProgram, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_ATTACH_SHADER, AttachShader, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_DETACH_SHADER, DetachShader, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_LINK_PROGRAM, LinkProgram, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_USE_PROGRAM, UseProgram, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_DELETE_PROGRAM, DeleteProgram, rc);

            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_IS_SHADER, IsShader, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_GET_SHADERIV, GetShaderiv, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_IS_PROGRAM, IsProgram, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_GET_PROGRAMIV, GetProgramiv, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_GET_ATTACHED_SHADERS, GetAttachedShaders,  rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_GET_SHADER_INFO_LOG, GetShaderInfoLog, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_GET_PROGRAM_INFO_LOG, GetProgramInfoLog, rc);

            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_GET_UNIFORM_LOCATION, GetUniformLocation, rc);

            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_UNIFORM1F, Uniform1f, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_UNIFORM2F, Uniform2f, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_UNIFORM3F, Uniform3f, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_UNIFORM4F, Uniform4f, rc);

            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_UNIFORM1I, Uniform1i, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_UNIFORM2I, Uniform2i, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_UNIFORM3I, Uniform3i, rc);
            NEMUVHWA_PFNINIT_SAME(context, PFNNEMUVHWA_UNIFORM4I, Uniform4i, rc);
        }
        else if(m_GL_ARB_shader_objects && m_GL_ARB_fragment_shader)
        {
            NEMUVHWA_PFNINIT_OBJECT_ARB(context, PFNNEMUVHWA_CREATE_SHADER, CreateShader, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_SHADER_SOURCE, ShaderSource, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_COMPILE_SHADER, CompileShader, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_DELETE_SHADER, DeleteShader, DeleteObjectARB, rc);

            NEMUVHWA_PFNINIT_OBJECT_ARB(context, PFNNEMUVHWA_CREATE_PROGRAM, CreateProgram, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_ATTACH_SHADER, AttachShader, AttachObjectARB, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_DETACH_SHADER, DetachShader, DetachObjectARB, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_LINK_PROGRAM, LinkProgram, rc);
            NEMUVHWA_PFNINIT_OBJECT_ARB(context, PFNNEMUVHWA_USE_PROGRAM, UseProgram, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_DELETE_PROGRAM, DeleteProgram, DeleteObjectARB, rc);

        //TODO:    NEMUVHWA_PFNINIT(PFNNEMUVHWA_IS_SHADER, IsShader, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_GET_SHADERIV, GetShaderiv, GetObjectParameterivARB, rc);
        //TODO:    NEMUVHWA_PFNINIT(PFNNEMUVHWA_IS_PROGRAM, IsProgram, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_GET_PROGRAMIV, GetProgramiv, GetObjectParameterivARB, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_GET_ATTACHED_SHADERS, GetAttachedShaders, GetAttachedObjectsARB, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_GET_SHADER_INFO_LOG, GetShaderInfoLog, GetInfoLogARB, rc);
            NEMUVHWA_PFNINIT(context, PFNNEMUVHWA_GET_PROGRAM_INFO_LOG, GetProgramInfoLog, GetInfoLogARB, rc);

            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_GET_UNIFORM_LOCATION, GetUniformLocation, rc);

            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNIFORM1F, Uniform1f, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNIFORM2F, Uniform2f, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNIFORM3F, Uniform3f, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNIFORM4F, Uniform4f, rc);

            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNIFORM1I, Uniform1i, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNIFORM2I, Uniform2i, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNIFORM3I, Uniform3i, rc);
            NEMUVHWA_PFNINIT_ARB(context, PFNNEMUVHWA_UNIFORM4I, Uniform4i, rc);
        }
        else
        {
            break;
        }

        if(RT_FAILURE(rc))
            break;

        mFragmentShaderSupported = true;
    } while(0);

    do
    {
        rc = 0;
        mFBOSupported = false;

        if(m_GL_EXT_framebuffer_object)
        {
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_IS_FRAMEBUFFER, IsFramebuffer, rc);
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_BIND_FRAMEBUFFER, BindFramebuffer, rc);
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_DELETE_FRAMEBUFFERS, DeleteFramebuffers, rc);
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_GEN_FRAMEBUFFERS, GenFramebuffers, rc);
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_CHECK_FRAMEBUFFER_STATUS, CheckFramebufferStatus, rc);
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_FRAMEBUFFER_TEXTURE1D, FramebufferTexture1D, rc);
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_FRAMEBUFFER_TEXTURE2D, FramebufferTexture2D, rc);
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_FRAMEBUFFER_TEXTURE3D, FramebufferTexture3D, rc);
            NEMUVHWA_PFNINIT_EXT(context, PFNNEMUVHWA_GET_FRAMEBUFFER_ATTACHMENT_PARAMETRIV, GetFramebufferAttachmentParameteriv, rc);
        }
        else
        {
            break;
        }

        if(RT_FAILURE(rc))
            break;

        mFBOSupported = true;
    } while(0);

    if(m_GL_ARB_texture_rectangle || m_GL_EXT_texture_rectangle || m_GL_NV_texture_rectangle)
    {
        mTextureRectangleSupported = true;
    }
    else
    {
        mTextureRectangleSupported = false;
    }

    mTextureNP2Supported = m_GL_ARB_texture_non_power_of_two;
}

void NemuVHWAInfo::init(const QGLContext * pContext)
{
    if(mInitialized)
        return;

    mInitialized = true;

    mglInfo.init(pContext);

    if(mglInfo.isFragmentShaderSupported() && mglInfo.isTextureRectangleSupported())
    {
        uint32_t num = 0;
        mFourccSupportedList[num++] = FOURCC_AYUV;
        mFourccSupportedList[num++] = FOURCC_UYVY;
        mFourccSupportedList[num++] = FOURCC_YUY2;
        if(mglInfo.getMultiTexNumSupported() >= 4)
        {
            /* YV12 currently requires 3 units (for each color component)
             * + 1 unit for dst texture for color-keying + 3 units for each color component
             * TODO: we could store YV12 data in one texture to eliminate this requirement*/
            mFourccSupportedList[num++] = FOURCC_YV12;
        }

        Assert(num <= NEMUVHWA_NUMFOURCC);
        mFourccSupportedCount = num;
    }
    else
    {
        mFourccSupportedCount = 0;
    }
}

bool NemuVHWAInfo::isVHWASupported() const
{
    if(mglInfo.getGLVersion() <= 0)
    {
        /* error occurred while gl info initialization */
        NEMUQGLLOGREL(("2D not supported: gl version info not initialized properly\n"));
        return false;
    }

#ifndef DEBUGVHWASTRICT
    /* in case we do not support shaders & multitexturing we can not support dst colorkey,
     * no sense to report Video Acceleration supported */
    if(!mglInfo.isFragmentShaderSupported())
    {
        NEMUQGLLOGREL(("2D not supported: fragment shader unsupported\n"));
        return false;
    }
#endif
    if(mglInfo.getMultiTexNumSupported() < 2)
    {
        NEMUQGLLOGREL(("2D not supported: multitexture unsupported\n"));
        return false;
    }

    /* color conversion now supported only GL_TEXTURE_RECTANGLE
     * in this case only stretching is accelerated
     * report as unsupported, TODO: probably should report as supported for stretch acceleration */
    if(!mglInfo.isTextureRectangleSupported())
    {
        NEMUQGLLOGREL(("2D not supported: texture rectangle unsupported\n"));
        return false;
    }

    NEMUQGLLOGREL(("2D is supported!\n"));
    return true;
}

/* static */
bool NemuVHWAInfo::checkVHWASupport()
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
    static char pszNemuPath[RTPATH_MAX];
    const char *papszArgs[] = { NULL, "-test", "2D", NULL};
    int rc;
    RTPROCESS Process;
    RTPROCSTATUS ProcStatus;
    uint64_t StartTS;

    rc = RTPathExecDir(pszNemuPath, RTPATH_MAX); AssertRCReturn(rc, false);
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    rc = RTPathAppend(pszNemuPath, RTPATH_MAX, "NemuTestOGL.exe");
#else
    rc = RTPathAppend(pszNemuPath, RTPATH_MAX, "NemuTestOGL");
#endif
    papszArgs[0] = pszNemuPath;         /* argv[0] */
    AssertRCReturn(rc, false);

    rc = RTProcCreate(pszNemuPath, papszArgs, RTENV_DEFAULT, 0, &Process);
    if (RT_FAILURE(rc))
    {
        NEMUQGLLOGREL(("2D support test failed: failed to create a test process\n"));
        return false;
    }

    StartTS = RTTimeMilliTS();

    while (1)
    {
        rc = RTProcWait(Process, RTPROCWAIT_FLAGS_NOBLOCK, &ProcStatus);
        if (rc != VERR_PROCESS_RUNNING)
            break;

        if (RTTimeMilliTS() - StartTS > 30*1000 /* 30 sec */)
        {
            RTProcTerminate(Process);
            RTThreadSleep(100);
            RTProcWait(Process, RTPROCWAIT_FLAGS_NOBLOCK, &ProcStatus);
            NEMUQGLLOGREL(("2D support test failed: the test did not complete within 30 sec\n"));
            return false;
        }
        RTThreadSleep(100);
    }

    if (RT_SUCCESS(rc))
    {
        if ((ProcStatus.enmReason==RTPROCEXITREASON_NORMAL) && (ProcStatus.iStatus==0))
        {
            NEMUQGLLOGREL(("2D support test succeeded\n"));
            return true;
        }
    }

    NEMUQGLLOGREL(("2D support test failed: err code (%Rra)\n", rc));

    return false;
#else
    /* @todo: test & enable external app approach*/
    NemuGLTmpContext ctx;
    const QGLContext *pContext = ctx.makeCurrent();
    Assert(pContext);
    if(pContext)
    {
        NemuVHWAInfo info;
        info.init(pContext);
        return info.isVHWASupported();
    }
    return false;
#endif
}

NemuGLTmpContext::NemuGLTmpContext()
{
    if(QGLFormat::hasOpenGL())
    {
        mWidget = new QGLWidget();
    }
    else
    {
        mWidget = NULL;
    }
}

NemuGLTmpContext::~NemuGLTmpContext()
{
    if(mWidget)
        delete mWidget;
}

const class QGLContext * NemuGLTmpContext::makeCurrent()
{
    if(mWidget)
    {
        mWidget->makeCurrent();
        return mWidget->context();
    }
    return NULL;
}

