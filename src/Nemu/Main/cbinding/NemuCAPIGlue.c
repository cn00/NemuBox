/* $Id: NemuCAPIGlue.c $ */
/** @file
 * Glue code for dynamically linking to NemuCAPI.
 */

/*
 * Copyright (C) 2008-2015 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "NemuCAPIGlue.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#ifndef WIN32
# include <dlfcn.h>
# include <pthread.h>
#else /* WIN32 */
# include <Windows.h>
#endif /* WIN32 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(__linux__) || defined(__linux_gnu__) || defined(__sun__) || defined(__FreeBSD__)
# define DYNLIB_NAME        "NemuXPCOMC.so"
#elif defined(__APPLE__)
# define DYNLIB_NAME        "NemuXPCOMC.dylib"
#elif defined(__OS2__)
# define DYNLIB_NAME        "NemuXPCOMC.dll"
#elif defined(WIN32)
# define DYNLIB_NAME        "NemuCAPI.dll"
#else
# error "Port me"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The so/dynsym/dll handle for NemuCAPI. */
#ifndef WIN32
void *g_hNemuCAPI = NULL;
#else /* WIN32 */
HMODULE g_hNemuCAPI = NULL;
#endif /* WIN32 */
/** The last load error. */
char g_szNemuErrMsg[256] = "";
/** Pointer to the NEMUCAPI function table. */
PCNEMUCAPI g_pNemuFuncs = NULL;
/** Pointer to NemuGetCAPIFunctions for the loaded NemuCAPI so/dylib/dll. */
PFNNEMUGETCAPIFUNCTIONS g_pfnGetFunctions = NULL;

typedef void FNDUMMY(void);
typedef FNDUMMY *PFNDUMMY;
/** Just a dummy global structure containing a bunch of
 * function pointers to code which is wanted in the link. */
PFNDUMMY g_apfnNemuCAPIGlue[] =
{
#ifndef WIN32
    /* The following link dependency is for helping gdb as it gets hideously
     * confused if the application doesn't drag in pthreads, but uses it. */
    (PFNDUMMY)pthread_create,
#endif /* !WIN32 */
    NULL
};


/**
 * Wrapper for setting g_szNemuErrMsg. Can be an empty stub.
 *
 * @param   fAlways         When 0 the g_szNemuErrMsg is only set if empty.
 * @param   pszFormat       The format string.
 * @param   ...             The arguments.
 */
static void setErrMsg(int fAlways, const char *pszFormat, ...)
{
    if (    fAlways
        ||  !g_szNemuErrMsg[0])
    {
        va_list va;
        va_start(va, pszFormat);
        vsnprintf(g_szNemuErrMsg, sizeof(g_szNemuErrMsg), pszFormat, va);
        va_end(va);
    }
}


/**
 * Try load C API .so/dylib/dll from the specified location and resolve all
 * the symbols we need. Tries both the new style and legacy name.
 *
 * @returns 0 on success, -1 on failure.
 * @param   pszHome         The directory where to try load NemuCAPI/NemuXPCOMC
 *                          from. Can be NULL.
 * @param   fSetAppHome     Whether to set the NEMU_APP_HOME env.var. or not
 *                          (boolean).
 */
static int tryLoadLibrary(const char *pszHome, int fSetAppHome)
{
    size_t      cchHome = pszHome ? strlen(pszHome) : 0;
    size_t      cbBufNeeded;
    char        szName[4096];

    /*
     * Construct the full name.
     */
    cbBufNeeded = cchHome + sizeof("/" DYNLIB_NAME);
    if (cbBufNeeded > sizeof(szName))
    {
        setErrMsg(1, "path buffer too small: %u bytes needed",
                  (unsigned)cbBufNeeded);
        return -1;
    }
    if (cchHome)
    {
        memcpy(szName, pszHome, cchHome);
        szName[cchHome] = '/';
        cchHome++;
    }
    memcpy(&szName[cchHome], DYNLIB_NAME, sizeof(DYNLIB_NAME));

    /*
     * Try load it by that name, setting the NEMU_APP_HOME first (for now).
     * Then resolve and call the function table getter.
     */
    if (fSetAppHome)
    {
#ifndef WIN32
        if (pszHome)
            setenv("NEMU_APP_HOME", pszHome, 1 /* always override */);
        else
            unsetenv("NEMU_APP_HOME");
#endif /* !WIN32 */
    }

#ifndef WIN32
    g_hNemuCAPI = dlopen(szName, RTLD_NOW | RTLD_LOCAL);
#else /* WIN32 */
    g_hNemuCAPI = LoadLibraryExA(szName, NULL /* hFile */, 0 /* dwFlags */);
#endif /* WIN32 */
    if (g_hNemuCAPI)
    {
        PFNNEMUGETCAPIFUNCTIONS pfnGetFunctions;
#ifndef WIN32
        pfnGetFunctions = (PFNNEMUGETCAPIFUNCTIONS)(uintptr_t)
            dlsym(g_hNemuCAPI, NEMU_GET_CAPI_FUNCTIONS_SYMBOL_NAME);
# ifdef NEMU_GET_XPCOM_FUNCTIONS_SYMBOL_NAME
        if (!pfnGetFunctions)
            pfnGetFunctions = (PFNNEMUGETCAPIFUNCTIONS)(uintptr_t)
                dlsym(g_hNemuCAPI, NEMU_GET_XPCOM_FUNCTIONS_SYMBOL_NAME);
# endif /* NEMU_GET_XPCOM_FUNCTIONS_SYMBOL_NAME */
#else /* WIN32 */
        pfnGetFunctions = (PFNNEMUGETCAPIFUNCTIONS)
            GetProcAddress(g_hNemuCAPI, NEMU_GET_CAPI_FUNCTIONS_SYMBOL_NAME);
#endif /* WIN32 */
        if (pfnGetFunctions)
        {
            g_pNemuFuncs = pfnGetFunctions(NEMU_CAPI_VERSION);
            if (g_pNemuFuncs)
            {
                if (   (   NEMU_CAPI_MAJOR(g_pNemuFuncs->uVersion)
                        == NEMU_CAPI_MAJOR(NEMU_CAPI_VERSION))
                    && (   NEMU_CAPI_MINOR(g_pNemuFuncs->uVersion)
                        >= NEMU_CAPI_MINOR(NEMU_CAPI_VERSION)))
                {
                    g_pfnGetFunctions = pfnGetFunctions;
                    return 0;
                }
                setErrMsg(1, "%.80s: pfnGetFunctions(%#x) returned incompatible version %#x",
                          szName, NEMU_CAPI_VERSION, g_pNemuFuncs->uVersion);
                g_pNemuFuncs = NULL;
            }
            else
            {
                /* bail out */
                setErrMsg(1, "%.80s: pfnGetFunctions(%#x) failed",
                          szName, NEMU_CAPI_VERSION);
            }
        }
        else
        {
#ifndef WIN32
            setErrMsg(1, "dlsym(%.80s/%.32s): %.128s",
                      szName, NEMU_GET_CAPI_FUNCTIONS_SYMBOL_NAME, dlerror());
#else /* WIN32 */
            setErrMsg(1, "GetProcAddress(%.80s/%.32s): %d",
                      szName, NEMU_GET_CAPI_FUNCTIONS_SYMBOL_NAME, GetLastError());
#endif /* WIN32 */
        }

#ifndef WIN32
        dlclose(g_hNemuCAPI);
#else /* WIN32 */
        FreeLibrary(g_hNemuCAPI);
#endif /* WIN32 */
        g_hNemuCAPI = NULL;
    }
    else
    {
#ifndef WIN32
        setErrMsg(0, "dlopen(%.80s): %.160s", szName, dlerror());
#else /* WIN32 */
        setErrMsg(0, "LoadLibraryEx(%.80s): %d", szName, GetLastError());
#endif /* WIN32 */
    }

    return -1;
}


/**
 * Tries to locate and load NemuCAPI.so/dylib/dll, resolving all the related
 * function pointers.
 *
 * @returns 0 on success, -1 on failure.
 *
 * @remark  This should be considered moved into a separate glue library since
 *          its its going to be pretty much the same for any user of NemuCAPI
 *          and it will just cause trouble to have duplicate versions of this
 *          source code all around the place.
 */
int NemuCGlueInit(void)
{
    const char *pszHome;

    memset(g_szNemuErrMsg, 0, sizeof(g_szNemuErrMsg));

    /*
     * If the user specifies the location, try only that.
     */
    pszHome = getenv("NEMU_APP_HOME");
    if (pszHome)
        return tryLoadLibrary(pszHome, 0);

    /*
     * Try the known standard locations.
     */
#if defined(__gnu__linux__) || defined(__linux__)
    if (tryLoadLibrary("/opt/VirtualBox", 1) == 0)
        return 0;
    if (tryLoadLibrary("/usr/lib/virtualbox", 1) == 0)
        return 0;
#elif defined(__sun__)
    if (tryLoadLibrary("/opt/VirtualBox/amd64", 1) == 0)
        return 0;
    if (tryLoadLibrary("/opt/VirtualBox/i386", 1) == 0)
        return 0;
#elif defined(__APPLE__)
    if (tryLoadLibrary("/Applications/VirtualBox.app/Contents/MacOS", 1) == 0)
        return 0;
#elif defined(__FreeBSD__)
    if (tryLoadLibrary("/usr/local/lib/virtualbox", 1) == 0)
        return 0;
#elif defined(__OS2__)
    if (tryLoadLibrary("C:/Apps/VirtualBox", 1) == 0)
        return 0;
#elif defined(WIN32)
    pszHome = getenv("ProgramFiles");
    if (pszHome)
    {
        char szPath[4096];
        size_t cb = sizeof(szPath);
        char *tmp = szPath;
        strncpy(tmp, pszHome, cb);
        tmp[cb - 1] = '\0';
        cb -= strlen(tmp);
        tmp += strlen(tmp);
        strncpy(tmp, "/Oracle/VirtualBox", cb);
        tmp[cb - 1] = '\0';
        if (tryLoadLibrary(szPath, 1) == 0)
            return 0;
    }
    if (tryLoadLibrary("C:/Program Files/Oracle/VirtualBox", 1) == 0)
        return 0;
#else
# error "port me"
#endif

    /*
     * Finally try the dynamic linker search path.
     */
    if (tryLoadLibrary(NULL, 1) == 0)
        return 0;

    /* No luck, return failure. */
    return -1;
}


/**
 * Terminate the C glue library.
 */
void NemuCGlueTerm(void)
{
    if (g_hNemuCAPI)
    {
#if 0 /* NemuRT.so doesn't like being reloaded. See @bugref{3725}. */
#ifndef WIN32
        dlclose(g_hNemuCAPI);
#else
        FreeLibrary(g_hNemuCAPI);
#endif
#endif
        g_hNemuCAPI = NULL;
    }
    g_pNemuFuncs = NULL;
    g_pfnGetFunctions = NULL;
    memset(g_szNemuErrMsg, 0, sizeof(g_szNemuErrMsg));
}

