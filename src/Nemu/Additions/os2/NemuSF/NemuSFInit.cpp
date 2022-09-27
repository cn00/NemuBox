/* $Id: NemuSFInit.cpp $ */
/** @file
 * NemuSF - OS/2 Shared Folders, Initialization.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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
#define LOG_GROUP LOG_GROUP_DEFAULT
#include "NemuSFInternal.h"

#include <Nemu/NemuGuestLib.h>
#include <Nemu/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
/* from NemuSFA.asm */
extern RTFAR16 g_fpfnDevHlp;
extern NEMUGUESTOS2IDCCONNECT g_NemuGuestIDC;
extern uint32_t g_u32Info;
/* from sys0.asm and the linker/end.lib. */
extern char _text, _etext, _data, _end;
RT_C_DECLS_END



/**
 * 32-bit Ring-0 init routine.
 *
 * This is called the first time somebody tries to use the IFS.
 * It will initialize IPRT, Vbgl and whatever else is required.
 *
 * The caller will do the necessary AttachDD and calling of the 16 bit
 * IDC to initialize the g_NemuGuestIDC global. Perhaps we should move
 * this bit to VbglInitClient? It's just that it's so much simpler to do it
 * while we're on the way here...
 *
 */
DECLASM(void) NemuSFR0Init(void)
{
    Log(("NemuSFR0Init: g_fpfnDevHlp=%lx u32Version=%RX32 u32Session=%RX32 pfnServiceEP=%p g_u32Info=%u (%#x)\n",
         g_fpfnDevHlp, g_NemuGuestIDC.u32Version, g_NemuGuestIDC.u32Session, g_NemuGuestIDC.pfnServiceEP, g_u32Info, g_u32Info));

    /*
     * Start by initializing IPRT.
     */
    if (    g_NemuGuestIDC.u32Version == VMMDEV_VERSION
        &&  VALID_PTR(g_NemuGuestIDC.u32Session)
        &&  VALID_PTR(g_NemuGuestIDC.pfnServiceEP))
    {
        int rc = RTR0Init(0);
        if (RT_SUCCESS(rc))
        {
            rc = VbglInitClient();
            if (RT_SUCCESS(rc))
            {
#ifndef DONT_LOCK_SEGMENTS
                /*
                 * Lock the 32-bit segments in memory.
                 */
                static KernVMLock_t s_Text32, s_Data32;
                rc = KernVMLock(VMDHL_LONG,
                                &_text, (uintptr_t)&_etext - (uintptr_t)&_text,
                                &s_Text32, (KernPageList_t *)-1, NULL);
                AssertMsg(rc == NO_ERROR, ("locking text32 failed, rc=%d\n"));
                rc = KernVMLock(VMDHL_LONG | VMDHL_WRITE,
                                &_data, (uintptr_t)&_end - (uintptr_t)&_data,
                                &s_Data32, (KernPageList_t *)-1, NULL);
                AssertMsg(rc == NO_ERROR, ("locking text32 failed, rc=%d\n"));
#endif

                Log(("NemuSFR0Init: completed successfully\n"));
                return;
            }
        }

        LogRel(("NemuSF: RTR0Init failed, rc=%Rrc\n", rc));
    }
    else
        LogRel(("NemuSF: Failed to connect to NemuGuest.sys.\n"));
}

