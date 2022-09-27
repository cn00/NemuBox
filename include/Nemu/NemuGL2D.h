/** @file
 *
 * Nemu frontends: Qt GUI ("VirtualBox"):
 * OpenGL support info used for 2D support detection
 */

/*
 * Copyright (C) 2009-2015 Oracle Corporation
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
#ifndef __NemuGLSupportInfo_h__
#define __NemuGLSupportInfo_h__

#include <iprt/types.h>

typedef char GLchar;

#ifndef GL_COMPILE_STATUS
# define GL_COMPILE_STATUS 0x8b81
#endif
#ifndef GL_LINK_STATUS
# define GL_LINK_STATUS    0x8b82
#endif
#ifndef GL_FRAGMENT_SHADER
# define GL_FRAGMENT_SHADER 0x8b30
#endif
#ifndef GL_VERTEX_SHADER
# define GL_VERTEX_SHADER 0x8b31
#endif

/* GL_ARB_multitexture */
#ifndef GL_TEXTURE0
# define GL_TEXTURE0                    0x84c0
#endif
#ifndef GL_TEXTURE1
# define GL_TEXTURE1                    0x84c1
#endif
#ifndef GL_MAX_TEXTURE_COORDS
# define GL_MAX_TEXTURE_COORDS          0x8871
#endif
#ifndef GL_MAX_TEXTURE_IMAGE_UNITS
# define GL_MAX_TEXTURE_IMAGE_UNITS     0x8872
#endif

#ifndef APIENTRY
# define APIENTRY
#endif

typedef GLvoid (APIENTRY *PFNNEMUVHWA_ACTIVE_TEXTURE) (GLenum texture);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_MULTI_TEX_COORD2I) (GLenum texture, GLint v0, GLint v1);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_MULTI_TEX_COORD2F) (GLenum texture, GLfloat v0, GLfloat v1);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_MULTI_TEX_COORD2D) (GLenum texture, GLdouble v0, GLdouble v1);

/* GL_ARB_texture_rectangle */
#ifndef GL_TEXTURE_RECTANGLE
# define GL_TEXTURE_RECTANGLE 0x84F5
#endif

/* GL_ARB_shader_objects */
/* GL_ARB_fragment_shader */

typedef GLuint (APIENTRY *PFNNEMUVHWA_CREATE_SHADER)  (GLenum type);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_SHADER_SOURCE)  (GLuint shader, GLsizei count, const GLchar **string, const GLint *length);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_COMPILE_SHADER) (GLuint shader);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_DELETE_SHADER)  (GLuint shader);

typedef GLuint (APIENTRY *PFNNEMUVHWA_CREATE_PROGRAM) ();
typedef GLvoid (APIENTRY *PFNNEMUVHWA_ATTACH_SHADER)  (GLuint program, GLuint shader);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_DETACH_SHADER)  (GLuint program, GLuint shader);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_LINK_PROGRAM)   (GLuint program);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_USE_PROGRAM)    (GLuint program);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_DELETE_PROGRAM) (GLuint program);

typedef GLboolean (APIENTRY *PFNNEMUVHWA_IS_SHADER)   (GLuint shader);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_GET_SHADERIV)   (GLuint shader, GLenum pname, GLint *params);
typedef GLboolean (APIENTRY *PFNNEMUVHWA_IS_PROGRAM)  (GLuint program);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_GET_PROGRAMIV)  (GLuint program, GLenum pname, GLint *params);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_GET_ATTACHED_SHADERS) (GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_GET_SHADER_INFO_LOG)  (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_GET_PROGRAM_INFO_LOG) (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLint (APIENTRY *PFNNEMUVHWA_GET_UNIFORM_LOCATION) (GLint programObj, const GLchar *name);

typedef GLvoid (APIENTRY *PFNNEMUVHWA_UNIFORM1F)(GLint location, GLfloat v0);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_UNIFORM2F)(GLint location, GLfloat v0, GLfloat v1);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_UNIFORM3F)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_UNIFORM4F)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

typedef GLvoid (APIENTRY *PFNNEMUVHWA_UNIFORM1I)(GLint location, GLint v0);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_UNIFORM2I)(GLint location, GLint v0, GLint v1);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_UNIFORM3I)(GLint location, GLint v0, GLint v1, GLint v2);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_UNIFORM4I)(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);

/* GL_ARB_pixel_buffer_object*/
#ifndef Q_WS_MAC
/* apears to be defined on mac */
typedef ptrdiff_t GLsizeiptr;
#endif

#ifndef GL_READ_ONLY
# define GL_READ_ONLY                   0x88B8
#endif
#ifndef GL_WRITE_ONLY
# define GL_WRITE_ONLY                  0x88B9
#endif
#ifndef GL_READ_WRITE
# define GL_READ_WRITE                  0x88BA
#endif
#ifndef GL_STREAM_DRAW
# define GL_STREAM_DRAW                 0x88E0
#endif
#ifndef GL_STREAM_READ
# define GL_STREAM_READ                 0x88E1
#endif
#ifndef GL_STREAM_COPY
# define GL_STREAM_COPY                 0x88E2
#endif
#ifndef GL_DYNAMIC_DRAW
# define GL_DYNAMIC_DRAW                0x88E8
#endif

#ifndef GL_PIXEL_PACK_BUFFER
# define GL_PIXEL_PACK_BUFFER           0x88EB
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
# define GL_PIXEL_UNPACK_BUFFER         0x88EC
#endif
#ifndef GL_PIXEL_PACK_BUFFER_BINDING
# define GL_PIXEL_PACK_BUFFER_BINDING   0x88ED
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
# define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif

typedef GLvoid (APIENTRY *PFNNEMUVHWA_GEN_BUFFERS)(GLsizei n, GLuint *buffers);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_DELETE_BUFFERS)(GLsizei n, const GLuint *buffers);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_BIND_BUFFER)(GLenum target, GLuint buffer);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_BUFFER_DATA)(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
typedef GLvoid* (APIENTRY *PFNNEMUVHWA_MAP_BUFFER)(GLenum target, GLenum access);
typedef GLboolean (APIENTRY *PFNNEMUVHWA_UNMAP_BUFFER)(GLenum target);

/* GL_EXT_framebuffer_object */
#ifndef GL_FRAMEBUFFER
# define GL_FRAMEBUFFER                0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
# define GL_COLOR_ATTACHMENT0          0x8CE0
#endif

typedef GLboolean (APIENTRY *PFNNEMUVHWA_IS_FRAMEBUFFER)(GLuint framebuffer);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_BIND_FRAMEBUFFER)(GLenum target, GLuint framebuffer);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_DELETE_FRAMEBUFFERS)(GLsizei n, const GLuint *framebuffers);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_GEN_FRAMEBUFFERS)(GLsizei n, GLuint *framebuffers);
typedef GLenum (APIENTRY *PFNNEMUVHWA_CHECK_FRAMEBUFFER_STATUS)(GLenum target);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_FRAMEBUFFER_TEXTURE1D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_FRAMEBUFFER_TEXTURE2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_FRAMEBUFFER_TEXTURE3D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset);
typedef GLvoid (APIENTRY *PFNNEMUVHWA_GET_FRAMEBUFFER_ATTACHMENT_PARAMETRIV)(GLenum target, GLenum attachment, GLenum pname, GLint *params);


/*****************/

/* functions */

/* @todo: move those to NemuGLInfo class instance members ??? */
extern PFNNEMUVHWA_ACTIVE_TEXTURE nemuglActiveTexture;
extern PFNNEMUVHWA_MULTI_TEX_COORD2I nemuglMultiTexCoord2i;
extern PFNNEMUVHWA_MULTI_TEX_COORD2D nemuglMultiTexCoord2d;
extern PFNNEMUVHWA_MULTI_TEX_COORD2F nemuglMultiTexCoord2f;


extern PFNNEMUVHWA_CREATE_SHADER   nemuglCreateShader;
extern PFNNEMUVHWA_SHADER_SOURCE   nemuglShaderSource;
extern PFNNEMUVHWA_COMPILE_SHADER  nemuglCompileShader;
extern PFNNEMUVHWA_DELETE_SHADER   nemuglDeleteShader;

extern PFNNEMUVHWA_CREATE_PROGRAM  nemuglCreateProgram;
extern PFNNEMUVHWA_ATTACH_SHADER   nemuglAttachShader;
extern PFNNEMUVHWA_DETACH_SHADER   nemuglDetachShader;
extern PFNNEMUVHWA_LINK_PROGRAM    nemuglLinkProgram;
extern PFNNEMUVHWA_USE_PROGRAM     nemuglUseProgram;
extern PFNNEMUVHWA_DELETE_PROGRAM  nemuglDeleteProgram;

extern PFNNEMUVHWA_IS_SHADER       nemuglIsShader;
extern PFNNEMUVHWA_GET_SHADERIV    nemuglGetShaderiv;
extern PFNNEMUVHWA_IS_PROGRAM      nemuglIsProgram;
extern PFNNEMUVHWA_GET_PROGRAMIV   nemuglGetProgramiv;
extern PFNNEMUVHWA_GET_ATTACHED_SHADERS nemuglGetAttachedShaders;
extern PFNNEMUVHWA_GET_SHADER_INFO_LOG  nemuglGetShaderInfoLog;
extern PFNNEMUVHWA_GET_PROGRAM_INFO_LOG nemuglGetProgramInfoLog;

extern PFNNEMUVHWA_GET_UNIFORM_LOCATION nemuglGetUniformLocation;

extern PFNNEMUVHWA_UNIFORM1F nemuglUniform1f;
extern PFNNEMUVHWA_UNIFORM2F nemuglUniform2f;
extern PFNNEMUVHWA_UNIFORM3F nemuglUniform3f;
extern PFNNEMUVHWA_UNIFORM4F nemuglUniform4f;

extern PFNNEMUVHWA_UNIFORM1I nemuglUniform1i;
extern PFNNEMUVHWA_UNIFORM2I nemuglUniform2i;
extern PFNNEMUVHWA_UNIFORM3I nemuglUniform3i;
extern PFNNEMUVHWA_UNIFORM4I nemuglUniform4i;

extern PFNNEMUVHWA_GEN_BUFFERS nemuglGenBuffers;
extern PFNNEMUVHWA_DELETE_BUFFERS nemuglDeleteBuffers;
extern PFNNEMUVHWA_BIND_BUFFER nemuglBindBuffer;
extern PFNNEMUVHWA_BUFFER_DATA nemuglBufferData;
extern PFNNEMUVHWA_MAP_BUFFER nemuglMapBuffer;
extern PFNNEMUVHWA_UNMAP_BUFFER nemuglUnmapBuffer;

extern PFNNEMUVHWA_IS_FRAMEBUFFER nemuglIsFramebuffer;
extern PFNNEMUVHWA_BIND_FRAMEBUFFER nemuglBindFramebuffer;
extern PFNNEMUVHWA_DELETE_FRAMEBUFFERS nemuglDeleteFramebuffers;
extern PFNNEMUVHWA_GEN_FRAMEBUFFERS nemuglGenFramebuffers;
extern PFNNEMUVHWA_CHECK_FRAMEBUFFER_STATUS nemuglCheckFramebufferStatus;
extern PFNNEMUVHWA_FRAMEBUFFER_TEXTURE1D nemuglFramebufferTexture1D;
extern PFNNEMUVHWA_FRAMEBUFFER_TEXTURE2D nemuglFramebufferTexture2D;
extern PFNNEMUVHWA_FRAMEBUFFER_TEXTURE3D nemuglFramebufferTexture3D;
extern PFNNEMUVHWA_GET_FRAMEBUFFER_ATTACHMENT_PARAMETRIV nemuglGetFramebufferAttachmentParameteriv;


class NemuGLInfo
{
public:
    NemuGLInfo() :
        mGLVersion(0),
        mFragmentShaderSupported(false),
        mTextureRectangleSupported(false),
        mTextureNP2Supported(false),
        mPBOSupported(false),
        mFBOSupported(false),
        mMultiTexNumSupported(1), /* 1 would mean it is not supported */
        m_GL_ARB_multitexture(false),
        m_GL_ARB_shader_objects(false),
        m_GL_ARB_fragment_shader(false),
        m_GL_ARB_pixel_buffer_object(false),
        m_GL_ARB_texture_rectangle(false),
        m_GL_EXT_texture_rectangle(false),
        m_GL_NV_texture_rectangle(false),
        m_GL_ARB_texture_non_power_of_two(false),
        m_GL_EXT_framebuffer_object(false),
        mInitialized(false)
    {}

    void init(const class QGLContext * pContext);

    bool isInitialized() const { return mInitialized; }

    int getGLVersion() const { return mGLVersion; }
    bool isFragmentShaderSupported() const { return mFragmentShaderSupported; }
    bool isTextureRectangleSupported() const { return mTextureRectangleSupported; }
    bool isTextureNP2Supported() const { return mTextureNP2Supported; }
    bool isPBOSupported() const { return mPBOSupported; }
    /* some ATI drivers do not seem to support non-zero offsets when dealing with PBOs
     * @todo: add a check for that, always unsupported currently */
    bool isPBOOffsetSupported() const { return false; }
    bool isFBOSupported() const { return mFBOSupported; }
    /* 1 would mean it is not supported */
    int getMultiTexNumSupported() const { return mMultiTexNumSupported; }

    static int parseVersion(const GLubyte * ver);
private:
    void initExtSupport(const class QGLContext & context);

    int mGLVersion;
    bool mFragmentShaderSupported;
    bool mTextureRectangleSupported;
    bool mTextureNP2Supported;
    bool mPBOSupported;
    bool mFBOSupported;
    int mMultiTexNumSupported; /* 1 would mean it is not supported */

    bool m_GL_ARB_multitexture;
    bool m_GL_ARB_shader_objects;
    bool m_GL_ARB_fragment_shader;
    bool m_GL_ARB_pixel_buffer_object;
    bool m_GL_ARB_texture_rectangle;
    bool m_GL_EXT_texture_rectangle;
    bool m_GL_NV_texture_rectangle;
    bool m_GL_ARB_texture_non_power_of_two;
    bool m_GL_EXT_framebuffer_object;

    bool mInitialized;
};

class NemuGLTmpContext
{
public:
    NemuGLTmpContext();
    ~NemuGLTmpContext();

    const class QGLContext * makeCurrent();
private:
    class QGLWidget * mWidget;
};


#define NEMUQGL_MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
                ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) |       \
                ((uint32_t)(uint8_t)(ch2) << 16) | ((uint32_t)(uint8_t)(ch3) << 24 ))

#define FOURCC_AYUV NEMUQGL_MAKEFOURCC('A', 'Y', 'U', 'V')
#define FOURCC_UYVY NEMUQGL_MAKEFOURCC('U', 'Y', 'V', 'Y')
#define FOURCC_YUY2 NEMUQGL_MAKEFOURCC('Y', 'U', 'Y', '2')
#define FOURCC_YV12 NEMUQGL_MAKEFOURCC('Y', 'V', '1', '2')
#define NEMUVHWA_NUMFOURCC 4

class NemuVHWAInfo
{
public:
    NemuVHWAInfo() :
        mFourccSupportedCount(0),
        mInitialized(false)
    {}

    NemuVHWAInfo(const NemuGLInfo & glInfo) :
        mglInfo(glInfo),
        mFourccSupportedCount(0),
        mInitialized(false)
    {}

    void init(const class QGLContext * pContext);

    bool isInitialized() const { return mInitialized; }

    const NemuGLInfo & getGlInfo() const { return mglInfo; }

    bool isVHWASupported() const;

    int getFourccSupportedCount() const { return mFourccSupportedCount; }
    const uint32_t * getFourccSupportedList() const { return mFourccSupportedList; }

    static bool checkVHWASupport();
private:
    NemuGLInfo mglInfo;
    uint32_t mFourccSupportedList[NEMUVHWA_NUMFOURCC];
    int mFourccSupportedCount;

    bool mInitialized;
};

#endif /* #ifndef __NemuGLSupportInfo_h__ */
