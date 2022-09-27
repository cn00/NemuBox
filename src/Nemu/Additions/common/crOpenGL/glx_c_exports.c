/* $Id: glx_c_exports.c $ */
/** @file
 *
 * VirtualBox guest OpenGL DRI GLX C stubs
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

#include "stub.h"
#include "dri_glx.h"
#include "fakedri_drv.h"


#ifdef NEMUOGL_FAKEDRI
/*DECLEXPORT(void) NEMUGLXENTRYTAG(glXGetDriverConfig)(const char *driverName)
{
    return glxim.GetDriverConfig(driverName);
}*/

DECLEXPORT(void) NEMUGLXENTRYTAG(glXFreeMemoryMESA)(Display *dpy, int scrn, void *pointer)
{
    return glxim.FreeMemoryMESA(dpy, scrn, pointer);
}

DECLEXPORT(GLXContext) NEMUGLXENTRYTAG(glXImportContextEXT)(Display *dpy, GLXContextID contextID)
{
    return glxim.ImportContextEXT(dpy, contextID);
}

DECLEXPORT(GLXContextID) NEMUGLXENTRYTAG(glXGetContextIDEXT)(const GLXContext ctx)
{
    return glxim.GetContextIDEXT(ctx);
}

DECLEXPORT(Bool) NEMUGLXENTRYTAG(glXMakeCurrentReadSGI)(Display *display, GLXDrawable draw, GLXDrawable read, GLXContext ctx)
{
    return glxim.MakeCurrentReadSGI(display, draw, read, ctx);
}


/*const char * NEMUGLXENTRYTAG(glXGetScreenDriver)(Display *dpy, int scrNum)
{
    return glxim.GetScreenDriver(dpy, scrNum);
}*/


DECLEXPORT(Display*) NEMUGLXENTRYTAG(glXGetCurrentDisplayEXT)(void)
{
    return glxim.GetCurrentDisplayEXT();
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXFreeContextEXT)(Display *dpy, GLXContext ctx)
{
    return glxim.FreeContextEXT(dpy, ctx);
}

/*Mesa internal*/
DECLEXPORT(int) NEMUGLXENTRYTAG(glXQueryContextInfoEXT)(Display *dpy, GLXContext ctx)
{
    return glxim.QueryContextInfoEXT(dpy, ctx);
}

DECLEXPORT(void *) NEMUGLXENTRYTAG(glXAllocateMemoryMESA)(Display *dpy, int scrn,
                                                       size_t size, float readFreq,
                                                       float writeFreq, float priority)
{
    return glxim.AllocateMemoryMESA(dpy, scrn, size, readFreq, writeFreq, priority);
}

DECLEXPORT(GLuint) NEMUGLXENTRYTAG(glXGetMemoryOffsetMESA)(Display *dpy, int scrn, const void *pointer )
{
    return glxim.GetMemoryOffsetMESA(dpy, scrn, pointer);
}


DECLEXPORT(GLXPixmap) NEMUGLXENTRYTAG(glXCreateGLXPixmapMESA)(Display *dpy, XVisualInfo *visual, Pixmap pixmap, Colormap cmap)
{
    return glxim.CreateGLXPixmapMESA(dpy, visual, pixmap, cmap);
}
#endif

/*Common glX functions*/
DECLEXPORT(void) NEMUGLXENTRYTAG(glXCopyContext)( Display *dpy, GLXContext src, GLXContext dst, unsigned long mask)
{
    return glxim.CopyContext(dpy, src, dst, mask);
}


DECLEXPORT(void) NEMUGLXENTRYTAG(glXUseXFont)(Font font, int first, int count, int listBase)
{
    return glxim.UseXFont(font, first, count, listBase);
}

DECLEXPORT(CR_GLXFuncPtr) NEMUGLXENTRYTAG(glXGetProcAddress)(const GLubyte *name)
{
    return glxim.GetProcAddress(name);
}

DECLEXPORT(Bool) NEMUGLXENTRYTAG(glXQueryExtension)(Display *dpy, int *errorBase, int *eventBase)
{
    return glxim.QueryExtension(dpy, errorBase, eventBase);
}

DECLEXPORT(Bool) NEMUGLXENTRYTAG(glXIsDirect)(Display *dpy, GLXContext ctx)
{
    return glxim.IsDirect(dpy, ctx);
}

DECLEXPORT(GLXPixmap) NEMUGLXENTRYTAG(glXCreateGLXPixmap)(Display *dpy, XVisualInfo *vis, Pixmap pixmap)
{
    return glxim.CreateGLXPixmap(dpy, vis, pixmap);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXSwapBuffers)(Display *dpy, GLXDrawable drawable)
{
    return glxim.SwapBuffers(dpy, drawable);
}


DECLEXPORT(GLXDrawable) NEMUGLXENTRYTAG(glXGetCurrentDrawable)(void)
{
    return glxim.GetCurrentDrawable();
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXWaitGL)(void)
{
    return glxim.WaitGL();
}

DECLEXPORT(Display *) NEMUGLXENTRYTAG(glXGetCurrentDisplay)(void)
{
    return glxim.GetCurrentDisplay();
}

DECLEXPORT(const char *) NEMUGLXENTRYTAG(glXQueryServerString)(Display *dpy, int screen, int name)
{
    return glxim.QueryServerString(dpy, screen, name);
}

DECLEXPORT(GLXContext) NEMUGLXENTRYTAG(glXCreateContext)(Display *dpy, XVisualInfo *vis, GLXContext share, Bool direct)
{
    return glxim.CreateContext(dpy, vis, share, direct);
}

DECLEXPORT(int) NEMUGLXENTRYTAG(glXGetConfig)(Display *dpy, XVisualInfo *vis, int attrib, int *value)
{
    return glxim.GetConfig(dpy, vis, attrib, value);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXWaitX)(void)
{
    return glxim.WaitX();
}

DECLEXPORT(GLXContext) NEMUGLXENTRYTAG(glXGetCurrentContext)(void)
{
    return glxim.GetCurrentContext();
}

DECLEXPORT(const char *) NEMUGLXENTRYTAG(glXGetClientString)(Display *dpy, int name)
{
    return glxim.GetClientString(dpy, name);
}

DECLEXPORT(Bool) NEMUGLXENTRYTAG(glXMakeCurrent)(Display *dpy, GLXDrawable drawable, GLXContext ctx)
{
    return glxim.MakeCurrent(dpy, drawable, ctx);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXDestroyContext)(Display *dpy, GLXContext ctx)
{
    return glxim.DestroyContext(dpy, ctx);
}

DECLEXPORT(CR_GLXFuncPtr) NEMUGLXENTRYTAG(glXGetProcAddressARB)(const GLubyte *name)
{
    return glxim.GetProcAddressARB(name);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXDestroyGLXPixmap)(Display *dpy, GLXPixmap pix)
{
    return glxim.DestroyGLXPixmap(dpy, pix);
}

DECLEXPORT(Bool) NEMUGLXENTRYTAG(glXQueryVersion)(Display *dpy, int *major, int *minor)
{
    return glxim.QueryVersion(dpy, major, minor);
}

DECLEXPORT(XVisualInfo *) NEMUGLXENTRYTAG(glXChooseVisual)(Display *dpy, int screen, int *attribList)
{
    return glxim.ChooseVisual(dpy, screen, attribList);
}

DECLEXPORT(const char *) NEMUGLXENTRYTAG(glXQueryExtensionsString)(Display *dpy, int screen)
{
    return glxim.QueryExtensionsString(dpy, screen);
}

#if GLX_EXTRAS
DECLEXPORT(GLXPbufferSGIX) NEMUGLXENTRYTAG(glXCreateGLXPbufferSGIX)
(Display *dpy, GLXFBConfigSGIX config, unsigned int width, unsigned int height, int *attrib_list)
{
    return glxim.CreateGLXPbufferSGIX(dpy, config, width, height, attrib_list);
}

DECLEXPORT(int) NEMUGLXENTRYTAG(glXQueryGLXPbufferSGIX)
(Display *dpy, GLXPbuffer pbuf, int attribute, unsigned int *value)
{
    return glxim.QueryGLXPbufferSGIX(dpy, pbuf, attribute, value);
}

DECLEXPORT(GLXFBConfigSGIX *) NEMUGLXENTRYTAG(glXChooseFBConfigSGIX)
(Display *dpy, int screen, int *attrib_list, int *nelements)
{
    return glxim.ChooseFBConfigSGIX(dpy, screen, attrib_list, nelements);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXDestroyGLXPbufferSGIX)(Display *dpy, GLXPbuffer pbuf)
{
    return glxim.DestroyGLXPbufferSGIX(dpy, pbuf);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXSelectEventSGIX)(Display *dpy, GLXDrawable drawable, unsigned long mask)
{
    return glxim.SelectEventSGIX(dpy, drawable, mask);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXGetSelectedEventSGIX)(Display *dpy, GLXDrawable drawable, unsigned long *mask)
{
    return glxim.GetSelectedEventSGIX(dpy, drawable, mask);
}

DECLEXPORT(GLXFBConfigSGIX) NEMUGLXENTRYTAG(glXGetFBConfigFromVisualSGIX)(Display *dpy, XVisualInfo *vis)
{
    return glxim.GetFBConfigFromVisualSGIX(dpy, vis);
}

DECLEXPORT(XVisualInfo *) NEMUGLXENTRYTAG(glXGetVisualFromFBConfigSGIX)(Display *dpy, GLXFBConfig config)
{
    return glxim.GetVisualFromFBConfigSGIX(dpy, config);
}

DECLEXPORT(GLXContext) NEMUGLXENTRYTAG(glXCreateContextWithConfigSGIX)
(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, Bool direct)
{
    return glxim.CreateContextWithConfigSGIX(dpy, config, render_type, share_list, direct);
}

DECLEXPORT(GLXPixmap) NEMUGLXENTRYTAG(glXCreateGLXPixmapWithConfigSGIX)(Display *dpy, GLXFBConfig config, Pixmap pixmap)
{
    return glxim.CreateGLXPixmapWithConfigSGIX(dpy, config, pixmap);
}

DECLEXPORT(int) NEMUGLXENTRYTAG(glXGetFBConfigAttribSGIX)(Display *dpy, GLXFBConfig config, int attribute, int *value)
{
    return glxim.GetFBConfigAttribSGIX(dpy, config, attribute, value);
}


/*
 * GLX 1.3 functions
 */
DECLEXPORT(GLXFBConfig *) NEMUGLXENTRYTAG(glXChooseFBConfig)(Display *dpy, int screen, ATTRIB_TYPE *attrib_list, int *nelements)
{
    return glxim.ChooseFBConfig(dpy, screen, attrib_list, nelements);
}

DECLEXPORT(GLXPbuffer) NEMUGLXENTRYTAG(glXCreatePbuffer)(Display *dpy, GLXFBConfig config, ATTRIB_TYPE *attrib_list)
{
    return glxim.CreatePbuffer(dpy, config, attrib_list);
}

DECLEXPORT(GLXPixmap) NEMUGLXENTRYTAG(glXCreatePixmap)(Display *dpy, GLXFBConfig config, Pixmap pixmap, const ATTRIB_TYPE *attrib_list)
{
    return glxim.CreatePixmap(dpy, config, pixmap, attrib_list);
}

DECLEXPORT(GLXWindow) NEMUGLXENTRYTAG(glXCreateWindow)(Display *dpy, GLXFBConfig config, Window win, ATTRIB_TYPE *attrib_list)
{
    return glxim.CreateWindow(dpy, config, win, attrib_list);
}


DECLEXPORT(GLXContext) NEMUGLXENTRYTAG(glXCreateNewContext)
(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, Bool direct)
{
    return glxim.CreateNewContext(dpy, config, render_type, share_list, direct);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXDestroyPbuffer)(Display *dpy, GLXPbuffer pbuf)
{
    return glxim.DestroyPbuffer(dpy, pbuf);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXDestroyPixmap)(Display *dpy, GLXPixmap pixmap)
{
    return glxim.DestroyPixmap(dpy, pixmap);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXDestroyWindow)(Display *dpy, GLXWindow win)
{
    return glxim.DestroyWindow(dpy, win);
}

DECLEXPORT(GLXDrawable) NEMUGLXENTRYTAG(glXGetCurrentReadDrawable)(void)
{
    return glxim.GetCurrentReadDrawable();
}

DECLEXPORT(int) NEMUGLXENTRYTAG(glXGetFBConfigAttrib)(Display *dpy, GLXFBConfig config, int attribute, int *value)
{
    return glxim.GetFBConfigAttrib(dpy, config, attribute, value);
}

DECLEXPORT(GLXFBConfig *) NEMUGLXENTRYTAG(glXGetFBConfigs)(Display *dpy, int screen, int *nelements)
{
    return glxim.GetFBConfigs(dpy, screen, nelements);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXGetSelectedEvent)(Display *dpy, GLXDrawable draw, unsigned long *event_mask)
{
    return glxim.GetSelectedEvent(dpy, draw, event_mask);
}

DECLEXPORT(XVisualInfo *) NEMUGLXENTRYTAG(glXGetVisualFromFBConfig)(Display *dpy, GLXFBConfig config)
{
    return glxim.GetVisualFromFBConfig(dpy, config);
}

DECLEXPORT(Bool) NEMUGLXENTRYTAG(glXMakeContextCurrent)(Display *display, GLXDrawable draw, GLXDrawable read, GLXContext ctx)
{
    return glxim.MakeContextCurrent(display, draw, read, ctx);
}

DECLEXPORT(int) NEMUGLXENTRYTAG(glXQueryContext)(Display *dpy, GLXContext ctx, int attribute, int *value)
{
    return glxim.QueryContext(dpy, ctx, attribute, value);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXQueryDrawable)(Display *dpy, GLXDrawable draw, int attribute, unsigned int *value)
{
    return glxim.QueryDrawable(dpy, draw, attribute, value);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXSelectEvent)(Display *dpy, GLXDrawable draw, unsigned long event_mask)
{
    return glxim.SelectEvent(dpy, draw, event_mask);
}

/*
#ifdef CR_EXT_texture_from_pixmap
DECLEXPORT(void) NEMUGLXENTRYTAG(glXBindTexImageEXT)(Display *dpy, GLXDrawable draw, int buffer, const int *attrib_list)
{
    return glxim.BindTexImageEXT(dpy, draw, buffer, attrib_list);
}

DECLEXPORT(void) NEMUGLXENTRYTAG(glXReleaseTexImageEXT)(Display *dpy, GLXDrawable draw, int buffer)
{
    return glxim.ReleaseTexImageEXT(dpy, draw, buffer);
}
#endif
*/

#endif /* GLX_EXTRAS */

