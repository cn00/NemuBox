/* $Id: dri_glx.h $ */

/** @file
 *
 * VirtualBox guest OpenGL DRI GLX header
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
 */

#ifndef ___CROPENGL_DRI_GLX_H
#define ___CROPENGL_DRI_GLX_H

#include "chromium.h"
#include "stub.h"

#if defined(NEMUOGL_FAKEDRI) || defined(NEMUOGL_DRI)
 #define NEMUGLXTAG(Func) nemustub_##Func
 #define NEMUGLXENTRYTAG(Func) nemu_##Func
 #define NEMUGLTAG(Func) cr_##Func
#else
 #define NEMUGLXTAG(Func) Func
 #define NEMUGLXENTRYTAG(Func) Func
 #define NEMUGLTAG(Func) Func
#endif

#ifdef NEMUOGL_FAKEDRI
extern DECLEXPORT(const char *) NEMUGLXTAG(glXGetDriverConfig)(const char *driverName);
extern DECLEXPORT(void) NEMUGLXTAG(glXFreeMemoryMESA)(Display *dpy, int scrn, void *pointer);
extern DECLEXPORT(GLXContext) NEMUGLXTAG(glXImportContextEXT)(Display *dpy, GLXContextID contextID);
extern DECLEXPORT(GLXContextID) NEMUGLXTAG(glXGetContextIDEXT)(const GLXContext ctx);
extern DECLEXPORT(Bool) NEMUGLXTAG(glXMakeCurrentReadSGI)(Display *display, GLXDrawable draw, GLXDrawable read, GLXContext ctx);
extern DECLEXPORT(const char *) NEMUGLXTAG(glXGetScreenDriver)(Display *dpy, int scrNum);
extern DECLEXPORT(Display *) NEMUGLXTAG(glXGetCurrentDisplayEXT)(void);
extern DECLEXPORT(void) NEMUGLXTAG(glXFreeContextEXT)(Display *dpy, GLXContext ctx);
/*Mesa internal*/
extern DECLEXPORT(int) NEMUGLXTAG(glXQueryContextInfoEXT)(Display *dpy, GLXContext ctx);
extern DECLEXPORT(void *) NEMUGLXTAG(glXAllocateMemoryMESA)(Display *dpy, int scrn,
                                                       size_t size, float readFreq,
                                                       float writeFreq, float priority);
extern DECLEXPORT(GLuint) NEMUGLXTAG(glXGetMemoryOffsetMESA)(Display *dpy, int scrn, const void *pointer );
extern DECLEXPORT(GLXPixmap) NEMUGLXTAG(glXCreateGLXPixmapMESA)(Display *dpy, XVisualInfo *visual, Pixmap pixmap, Colormap cmap);
#endif

/*Common glX functions*/
extern DECLEXPORT(void) NEMUGLXTAG(glXCopyContext)( Display *dpy, GLXContext src, GLXContext dst, 
#if defined(SunOS)
unsigned long mask);
#else
unsigned long mask);
#endif
extern DECLEXPORT(void) NEMUGLXTAG(glXUseXFont)(Font font, int first, int count, int listBase);
extern DECLEXPORT(CR_GLXFuncPtr) NEMUGLXTAG(glXGetProcAddress)(const GLubyte *name);
extern DECLEXPORT(Bool) NEMUGLXTAG(glXQueryExtension)(Display *dpy, int *errorBase, int *eventBase);
extern DECLEXPORT(Bool) NEMUGLXTAG(glXIsDirect)(Display *dpy, GLXContext ctx);
extern DECLEXPORT(GLXPixmap) NEMUGLXTAG(glXCreateGLXPixmap)(Display *dpy, XVisualInfo *vis, Pixmap pixmap);
extern DECLEXPORT(void) NEMUGLXTAG(glXSwapBuffers)(Display *dpy, GLXDrawable drawable);
extern DECLEXPORT(GLXDrawable) NEMUGLXTAG(glXGetCurrentDrawable)(void);
extern DECLEXPORT(void) NEMUGLXTAG(glXWaitGL)(void);
extern DECLEXPORT(Display *) NEMUGLXTAG(glXGetCurrentDisplay)(void);
extern DECLEXPORT(const char *) NEMUGLXTAG(glXQueryServerString)(Display *dpy, int screen, int name);
extern DECLEXPORT(GLXContext) NEMUGLXTAG(glXCreateContext)(Display *dpy, XVisualInfo *vis, GLXContext share, Bool direct);
extern DECLEXPORT(int) NEMUGLXTAG(glXGetConfig)(Display *dpy, XVisualInfo *vis, int attrib, int *value);
extern DECLEXPORT(void) NEMUGLXTAG(glXWaitX)(void);
extern DECLEXPORT(GLXContext) NEMUGLXTAG(glXGetCurrentContext)(void);
extern DECLEXPORT(const char *) NEMUGLXTAG(glXGetClientString)(Display *dpy, int name);
extern DECLEXPORT(Bool) NEMUGLXTAG(glXMakeCurrent)(Display *dpy, GLXDrawable drawable, GLXContext ctx);
extern DECLEXPORT(void) NEMUGLXTAG(glXDestroyContext)(Display *dpy, GLXContext ctx);
extern DECLEXPORT(CR_GLXFuncPtr) NEMUGLXTAG(glXGetProcAddressARB)(const GLubyte *name);
extern DECLEXPORT(void) NEMUGLXTAG(glXDestroyGLXPixmap)(Display *dpy, GLXPixmap pix);
extern DECLEXPORT(Bool) NEMUGLXTAG(glXQueryVersion)(Display *dpy, int *major, int *minor);
extern DECLEXPORT(XVisualInfo *) NEMUGLXTAG(glXChooseVisual)(Display *dpy, int screen, int *attribList);
extern DECLEXPORT(const char *) NEMUGLXTAG(glXQueryExtensionsString)(Display *dpy, int screen);

/**
 * Set this to 1 if you want to build stub functions for the
 * GL_SGIX_pbuffer and GLX_SGIX_fbconfig extensions.
 * This used to be disabled, due to "messy compilation issues",
 * according to the earlier comment; but they're needed just
 * to resolve symbols for OpenInventor applications, and I
 * haven't found any reference to exactly what the "messy compilation
 * issues" are, so I'm re-enabling the code by default.
 */
#define GLX_EXTRAS 1

#define GLX_SGIX_video_resize 1

/**
 * Prototypes, in case they're not in glx.h or glxext.h
 * Unfortunately, there's some inconsistency between the extension
 * specs, and the SGI, NVIDIA, XFree86 and common glxext.h header
 * files.
 */
#if defined(GLX_GLXEXT_VERSION)
/* match glxext.h, XFree86, Mesa */
#define ATTRIB_TYPE const int
#else
#define ATTRIB_TYPE int
#endif

#if GLX_EXTRAS
extern DECLEXPORT(GLXPbufferSGIX) NEMUGLXTAG(glXCreateGLXPbufferSGIX)
(Display *dpy, GLXFBConfigSGIX config, unsigned int width, unsigned int height, int *attrib_list);

extern DECLEXPORT(int) NEMUGLXTAG(glXQueryGLXPbufferSGIX)
(Display *dpy, GLXPbuffer pbuf, int attribute, unsigned int *value);

extern DECLEXPORT(GLXFBConfigSGIX *) NEMUGLXTAG(glXChooseFBConfigSGIX)
(Display *dpy, int screen, int *attrib_list, int *nelements);

extern DECLEXPORT(void) NEMUGLXTAG(glXDestroyGLXPbufferSGIX)(Display *dpy, GLXPbuffer pbuf);
extern DECLEXPORT(void) NEMUGLXTAG(glXSelectEventSGIX)(Display *dpy, GLXDrawable drawable, unsigned long mask);
extern DECLEXPORT(void) NEMUGLXTAG(glXGetSelectedEventSGIX)(Display *dpy, GLXDrawable drawable, unsigned long *mask);

extern DECLEXPORT(GLXFBConfigSGIX) NEMUGLXTAG(glXGetFBConfigFromVisualSGIX)(Display *dpy, XVisualInfo *vis);
extern DECLEXPORT(XVisualInfo *) NEMUGLXTAG(glXGetVisualFromFBConfigSGIX)(Display *dpy, GLXFBConfig config);
extern DECLEXPORT(GLXContext) NEMUGLXTAG(glXCreateContextWithConfigSGIX)
(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, Bool direct);

extern DECLEXPORT(GLXPixmap) NEMUGLXTAG(glXCreateGLXPixmapWithConfigSGIX)(Display *dpy, GLXFBConfig config, Pixmap pixmap);
extern DECLEXPORT(int) NEMUGLXTAG(glXGetFBConfigAttribSGIX)(Display *dpy, GLXFBConfig config, int attribute, int *value);

/*
 * GLX 1.3 functions
 */
extern DECLEXPORT(GLXFBConfig *) NEMUGLXTAG(glXChooseFBConfig)(Display *dpy, int screen, ATTRIB_TYPE *attrib_list, int *nelements);
extern DECLEXPORT(GLXPbuffer) NEMUGLXTAG(glXCreatePbuffer)(Display *dpy, GLXFBConfig config, ATTRIB_TYPE *attrib_list);
extern DECLEXPORT(GLXPixmap) NEMUGLXTAG(glXCreatePixmap)(Display *dpy, GLXFBConfig config, Pixmap pixmap, const ATTRIB_TYPE *attrib_list);
extern DECLEXPORT(GLXWindow) NEMUGLXTAG(glXCreateWindow)(Display *dpy, GLXFBConfig config, Window win, ATTRIB_TYPE *attrib_list);
extern DECLEXPORT(GLXContext) NEMUGLXTAG(glXCreateNewContext)
(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, Bool direct);

extern DECLEXPORT(void) NEMUGLXTAG(glXDestroyPbuffer)(Display *dpy, GLXPbuffer pbuf);
extern DECLEXPORT(void) NEMUGLXTAG(glXDestroyPixmap)(Display *dpy, GLXPixmap pixmap);
extern DECLEXPORT(void) NEMUGLXTAG(glXDestroyWindow)(Display *dpy, GLXWindow win);
extern DECLEXPORT(GLXDrawable) NEMUGLXTAG(glXGetCurrentReadDrawable)(void);
extern DECLEXPORT(int) NEMUGLXTAG(glXGetFBConfigAttrib)(Display *dpy, GLXFBConfig config, int attribute, int *value);
extern DECLEXPORT(GLXFBConfig *) NEMUGLXTAG(glXGetFBConfigs)(Display *dpy, int screen, int *nelements);
extern DECLEXPORT(void) NEMUGLXTAG(glXGetSelectedEvent)(Display *dpy, GLXDrawable draw, unsigned long *event_mask);
extern DECLEXPORT(XVisualInfo *) NEMUGLXTAG(glXGetVisualFromFBConfig)(Display *dpy, GLXFBConfig config);
extern DECLEXPORT(Bool) NEMUGLXTAG(glXMakeContextCurrent)(Display *display, GLXDrawable draw, GLXDrawable read, GLXContext ctx);
extern DECLEXPORT(int) NEMUGLXTAG(glXQueryContext)(Display *dpy, GLXContext ctx, int attribute, int *value);
extern DECLEXPORT(void) NEMUGLXTAG(glXQueryDrawable)(Display *dpy, GLXDrawable draw, int attribute, unsigned int *value);
extern DECLEXPORT(void) NEMUGLXTAG(glXSelectEvent)(Display *dpy, GLXDrawable draw, unsigned long event_mask);

#ifdef CR_EXT_texture_from_pixmap
extern DECLEXPORT(void) NEMUGLXTAG(glXBindTexImageEXT)(Display *dpy, GLXDrawable draw, int buffer, const int *attrib_list);
extern DECLEXPORT(void) NEMUGLXTAG(glXReleaseTexImageEXT)(Display *dpy, GLXDrawable draw, int buffer);
#endif

#endif /* GLX_EXTRAS */

#endif //___CROPENGL_DRI_GLX_H
