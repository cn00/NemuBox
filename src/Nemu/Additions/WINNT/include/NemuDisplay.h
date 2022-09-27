/** @file
 *
 * NemuDisplay - private windows additions display header
 *
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
#ifndef __NemuDisplay_h__
#define __NemuDisplay_h__

#include <iprt/types.h>
#include <iprt/assert.h>

#define NEMUESC_SETVISIBLEREGION            0xABCD9001
#define NEMUESC_ISVRDPACTIVE                0xABCD9002
#ifdef NEMU_WITH_WDDM
# define NEMUESC_REINITVIDEOMODES           0xABCD9003
# define NEMUESC_GETNEMUVIDEOCMCMD          0xABCD9004
# define NEMUESC_DBGPRINT                   0xABCD9005
# define NEMUESC_SCREENLAYOUT               0xABCD9006
# define NEMUESC_SWAPCHAININFO              0xABCD9007
# define NEMUESC_UHGSMI_ALLOCATE            0xABCD9008
# define NEMUESC_UHGSMI_DEALLOCATE          0xABCD9009
# define NEMUESC_UHGSMI_SUBMIT              0xABCD900A
# define NEMUESC_SHRC_ADDREF                0xABCD900B
# define NEMUESC_SHRC_RELEASE               0xABCD900C
# define NEMUESC_DBGDUMPBUF                 0xABCD900D
# define NEMUESC_CRHGSMICTLCON_CALL         0xABCD900E
# define NEMUESC_CRHGSMICTLCON_GETCLIENTID  0xABCD900F
# define NEMUESC_REINITVIDEOMODESBYMASK     0xABCD9010
# define NEMUESC_ADJUSTVIDEOMODES           0xABCD9011
# define NEMUESC_SETCTXHOSTID               0xABCD9012
# define NEMUESC_CONFIGURETARGETS           0xABCD9013
# define NEMUESC_SETALLOCHOSTID             0xABCD9014
# define NEMUESC_CRHGSMICTLCON_GETHOSTCAPS  0xABCD9015
# define NEMUESC_UPDATEMODES                0xABCD9016
#endif /* #ifdef NEMU_WITH_WDDM */

# define NEMUESC_ISANYX                     0xABCD9200

typedef struct NEMUDISPIFESCAPE
{
    int32_t escapeCode;
    uint32_t u32CmdSpecific;
} NEMUDISPIFESCAPE, *PNEMUDISPIFESCAPE;

/* ensure command body is always 8-byte-aligned*/
AssertCompile((sizeof (NEMUDISPIFESCAPE) & 7) == 0);

#define NEMUDISPIFESCAPE_DATA_OFFSET() ((sizeof (NEMUDISPIFESCAPE) + 7) & ~7)
#define NEMUDISPIFESCAPE_DATA(_pHead, _t) ( (_t*)(((uint8_t*)(_pHead)) + NEMUDISPIFESCAPE_DATA_OFFSET()))
#define NEMUDISPIFESCAPE_DATA_SIZE(_s) ( (_s) < NEMUDISPIFESCAPE_DATA_OFFSET() ? 0 : (_s) - NEMUDISPIFESCAPE_DATA_OFFSET() )
#define NEMUDISPIFESCAPE_SIZE(_cbData) ((_cbData) ? NEMUDISPIFESCAPE_DATA_OFFSET() + (_cbData) : sizeof (NEMUDISPIFESCAPE))

#define IOCTL_VIDEO_NEMU_SETVISIBLEREGION \
    CTL_CODE(FILE_DEVICE_VIDEO, 0xA01, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_NEMU_ISANYX \
    CTL_CODE(FILE_DEVICE_VIDEO, 0xA02, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct NEMUDISPIFESCAPE_ISANYX
{
    NEMUDISPIFESCAPE EscapeHdr;
    uint32_t u32IsAnyX;
} NEMUDISPIFESCAPE_ISANYX, *PNEMUDISPIFESCAPE_ISANYX;

#ifdef NEMU_WITH_WDDM

/* for NEMU_VIDEO_MAX_SCREENS definition */
#include <Nemu/Hardware/NemuVideoVBE.h>

typedef struct NEMUWDDM_RECOMMENDVIDPN_SOURCE
{
    RTRECTSIZE Size;
} NEMUWDDM_RECOMMENDVIDPN_SOURCE;

typedef struct NEMUWDDM_RECOMMENDVIDPN_TARGET
{
    int32_t iSource;
} NEMUWDDM_RECOMMENDVIDPN_TARGET;

typedef struct
{
    NEMUWDDM_RECOMMENDVIDPN_SOURCE aSources[NEMU_VIDEO_MAX_SCREENS];
    NEMUWDDM_RECOMMENDVIDPN_TARGET aTargets[NEMU_VIDEO_MAX_SCREENS];
} NEMUWDDM_RECOMMENDVIDPN, *PNEMUWDDM_RECOMMENDVIDPN;

#define NEMUWDDM_SCREENMASK_SIZE ((NEMU_VIDEO_MAX_SCREENS + 7) >> 3)

typedef struct NEMUDISPIFESCAPE_UPDATEMODES
{
    NEMUDISPIFESCAPE EscapeHdr;
    uint32_t u32TargetId;
    RTRECTSIZE Size;
} NEMUDISPIFESCAPE_UPDATEMODES;

#endif /* NEMU_WITH_WDDM */

#endif /* __NemuDisplay_h__ */
