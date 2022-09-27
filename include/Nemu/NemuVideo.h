/** @file
 * VirtualBox Video interface.
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

#ifndef ___Nemu_NemuVideo_h
#define ___Nemu_NemuVideo_h

#include <Nemu/VMMDev.h>
#include <Nemu/Hardware/NemuVideoVBE.h>

#include <iprt/cdefs.h>
#include <iprt/types.h>

/*
 * The last 4096 bytes of the guest VRAM contains the generic info for all
 * DualView chunks: sizes and offsets of chunks. This is filled by miniport.
 *
 * Last 4096 bytes of each chunk contain chunk specific data: framebuffer info,
 * etc. This is used exclusively by the corresponding instance of a display driver.
 *
 * The VRAM layout:
 *     Last 4096 bytes - Adapter information area.
 *     4096 bytes aligned miniport heap (value specified in the config rouded up).
 *     Slack - what left after dividing the VRAM.
 *     4096 bytes aligned framebuffers:
 *       last 4096 bytes of each framebuffer is the display information area.
 *
 * The Virtual Graphics Adapter information in the guest VRAM is stored by the
 * guest video driver using structures prepended by NEMUVIDEOINFOHDR.
 *
 * When the guest driver writes dword 0 to the VBE_DISPI_INDEX_NEMU_VIDEO
 * the host starts to process the info. The first element at the start of
 * the 4096 bytes region should be normally be a LINK that points to
 * actual information chain. That way the guest driver can have some
 * fixed layout of the information memory block and just rewrite
 * the link to point to relevant memory chain.
 *
 * The processing stops at the END element.
 *
 * The host can access the memory only when the port IO is processed.
 * All data that will be needed later must be copied from these 4096 bytes.
 * But other VRAM can be used by host until the mode is disabled.
 *
 * The guest driver writes dword 0xffffffff to the VBE_DISPI_INDEX_NEMU_VIDEO
 * to disable the mode.
 *
 * VBE_DISPI_INDEX_NEMU_VIDEO is used to read the configuration information
 * from the host and issue commands to the host.
 *
 * The guest writes the VBE_DISPI_INDEX_NEMU_VIDEO index register, the the
 * following operations with the VBE data register can be performed:
 *
 * Operation            Result
 * write 16 bit value   NOP
 * read 16 bit value    count of monitors
 * write 32 bit value   sets the nemu command value and the command processed by the host
 * read 32 bit value    result of the last nemu command is returned
 */

#define NEMU_VIDEO_PRIMARY_SCREEN 0
#define NEMU_VIDEO_NO_SCREEN ~0

/* The size of the information. */
/*
 * The minimum HGSMI heap size is PAGE_SIZE (4096 bytes) and is a restriction of the
 * runtime heapsimple API. Use minimum 2 pages here, because the info area also may
 * contain other data (for example HGSMIHOSTFLAGS structure).
 */
#ifndef NEMU_XPDM_MINIPORT
# define VBVA_ADAPTER_INFORMATION_SIZE (64*_1K)
#else
#define VBVA_ADAPTER_INFORMATION_SIZE  (16*_1K)
#define VBVA_DISPLAY_INFORMATION_SIZE  (64*_1K)
#endif
#define VBVA_MIN_BUFFER_SIZE           (64*_1K)


/* The value for port IO to let the adapter to interpret the adapter memory. */
#define NEMU_VIDEO_DISABLE_ADAPTER_MEMORY        0xFFFFFFFF

/* The value for port IO to let the adapter to interpret the adapter memory. */
#define NEMU_VIDEO_INTERPRET_ADAPTER_MEMORY      0x00000000

/* The value for port IO to let the adapter to interpret the display memory.
 * The display number is encoded in low 16 bits.
 */
#define NEMU_VIDEO_INTERPRET_DISPLAY_MEMORY_BASE 0x00010000


/* The end of the information. */
#define NEMU_VIDEO_INFO_TYPE_END          0
/* Instructs the host to fetch the next NEMUVIDEOINFOHDR at the given offset of VRAM. */
#define NEMU_VIDEO_INFO_TYPE_LINK         1
/* Information about a display memory position. */
#define NEMU_VIDEO_INFO_TYPE_DISPLAY      2
/* Information about a screen. */
#define NEMU_VIDEO_INFO_TYPE_SCREEN       3
/* Information about host notifications for the driver. */
#define NEMU_VIDEO_INFO_TYPE_HOST_EVENTS  4
/* Information about non-volatile guest VRAM heap. */
#define NEMU_VIDEO_INFO_TYPE_NV_HEAP      5
/* VBVA enable/disable. */
#define NEMU_VIDEO_INFO_TYPE_VBVA_STATUS  6
/* VBVA flush. */
#define NEMU_VIDEO_INFO_TYPE_VBVA_FLUSH   7
/* Query configuration value. */
#define NEMU_VIDEO_INFO_TYPE_QUERY_CONF32 8


#pragma pack(1)
typedef struct NEMUVIDEOINFOHDR
{
    uint8_t u8Type;
    uint8_t u8Reserved;
    uint16_t u16Length;
} NEMUVIDEOINFOHDR;


typedef struct NEMUVIDEOINFOLINK
{
    /* Relative offset in VRAM */
    int32_t i32Offset;
} NEMUVIDEOINFOLINK;


/* Resides in adapter info memory. Describes a display VRAM chunk. */
typedef struct NEMUVIDEOINFODISPLAY
{
    /* Index of the framebuffer assigned by guest. */
    uint32_t u32Index;

    /* Absolute offset in VRAM of the framebuffer to be displayed on the monitor. */
    uint32_t u32Offset;

    /* The size of the memory that can be used for the screen. */
    uint32_t u32FramebufferSize;

    /* The size of the memory that is used for the Display information.
     * The information is at u32Offset + u32FramebufferSize
     */
    uint32_t u32InformationSize;

} NEMUVIDEOINFODISPLAY;


/* Resides in display info area, describes the current video mode. */
#define NEMU_VIDEO_INFO_SCREEN_F_NONE   0x00
#define NEMU_VIDEO_INFO_SCREEN_F_ACTIVE 0x01

typedef struct NEMUVIDEOINFOSCREEN
{
    /* Physical X origin relative to the primary screen. */
    int32_t xOrigin;

    /* Physical Y origin relative to the primary screen. */
    int32_t yOrigin;

    /* The scan line size in bytes. */
    uint32_t u32LineSize;

    /* Width of the screen. */
    uint16_t u16Width;

    /* Height of the screen. */
    uint16_t u16Height;

    /* Color depth. */
    uint8_t bitsPerPixel;

    /* NEMU_VIDEO_INFO_SCREEN_F_* */
    uint8_t u8Flags;
} NEMUVIDEOINFOSCREEN;

/* The guest initializes the structure to 0. The positions of the structure in the
 * display info area must not be changed, host will update the structure. Guest checks
 * the events and modifies the structure as a response to host.
 */
#define NEMU_VIDEO_INFO_HOST_EVENTS_F_NONE        0x00000000
#define NEMU_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET  0x00000080

typedef struct NEMUVIDEOINFOHOSTEVENTS
{
    /* Host events. */
    uint32_t fu32Events;
} NEMUVIDEOINFOHOSTEVENTS;

/* Resides in adapter info memory. Describes the non-volatile VRAM heap. */
typedef struct NEMUVIDEOINFONVHEAP
{
    /* Absolute offset in VRAM of the start of the heap. */
    uint32_t u32HeapOffset;

    /* The size of the heap. */
    uint32_t u32HeapSize;

} NEMUVIDEOINFONVHEAP;

/* Display information area. */
typedef struct NEMUVIDEOINFOVBVASTATUS
{
    /* Absolute offset in VRAM of the start of the VBVA QUEUE. 0 to disable VBVA. */
    uint32_t u32QueueOffset;

    /* The size of the VBVA QUEUE. 0 to disable VBVA. */
    uint32_t u32QueueSize;

} NEMUVIDEOINFOVBVASTATUS;

typedef struct NEMUVIDEOINFOVBVAFLUSH
{
    uint32_t u32DataStart;

    uint32_t u32DataEnd;

} NEMUVIDEOINFOVBVAFLUSH;

#define NEMU_VIDEO_QCI32_MONITOR_COUNT       0
#define NEMU_VIDEO_QCI32_OFFSCREEN_HEAP_SIZE 1

typedef struct NEMUVIDEOINFOQUERYCONF32
{
    uint32_t u32Index;

    uint32_t u32Value;

} NEMUVIDEOINFOQUERYCONF32;
#pragma pack()

#ifdef NEMU_WITH_VIDEOHWACCEL
#pragma pack(1)

#define NEMUVHWA_VERSION_MAJ 0
#define NEMUVHWA_VERSION_MIN 0
#define NEMUVHWA_VERSION_BLD 6
#define NEMUVHWA_VERSION_RSV 0

typedef enum
{
    NEMUVHWACMD_TYPE_SURF_CANCREATE = 1,
    NEMUVHWACMD_TYPE_SURF_CREATE,
    NEMUVHWACMD_TYPE_SURF_DESTROY,
    NEMUVHWACMD_TYPE_SURF_LOCK,
    NEMUVHWACMD_TYPE_SURF_UNLOCK,
    NEMUVHWACMD_TYPE_SURF_BLT,
    NEMUVHWACMD_TYPE_SURF_FLIP,
    NEMUVHWACMD_TYPE_SURF_OVERLAY_UPDATE,
    NEMUVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION,
    NEMUVHWACMD_TYPE_SURF_COLORKEY_SET,
    NEMUVHWACMD_TYPE_QUERY_INFO1,
    NEMUVHWACMD_TYPE_QUERY_INFO2,
    NEMUVHWACMD_TYPE_ENABLE,
    NEMUVHWACMD_TYPE_DISABLE,
    NEMUVHWACMD_TYPE_HH_CONSTRUCT,
    NEMUVHWACMD_TYPE_HH_RESET
#ifdef NEMU_WITH_WDDM
    , NEMUVHWACMD_TYPE_SURF_GETINFO
    , NEMUVHWACMD_TYPE_SURF_COLORFILL
#endif
    , NEMUVHWACMD_TYPE_HH_DISABLE
    , NEMUVHWACMD_TYPE_HH_ENABLE
    , NEMUVHWACMD_TYPE_HH_SAVESTATE_SAVEBEGIN
    , NEMUVHWACMD_TYPE_HH_SAVESTATE_SAVEEND
    , NEMUVHWACMD_TYPE_HH_SAVESTATE_SAVEPERFORM
    , NEMUVHWACMD_TYPE_HH_SAVESTATE_LOADPERFORM
} NEMUVHWACMD_TYPE;

/* the command processing was asynch, set by the host to indicate asynch command completion
 * must not be cleared once set, the command completion is performed by issuing a host->guest completion command
 * while keeping this flag unchanged */
#define NEMUVHWACMD_FLAG_HG_ASYNCH               0x00010000
/* asynch completion is performed by issuing the event */
#define NEMUVHWACMD_FLAG_GH_ASYNCH_EVENT         0x00000001
/* issue interrupt on asynch completion */
#define NEMUVHWACMD_FLAG_GH_ASYNCH_IRQ           0x00000002
/* guest does not do any op on completion of this command, the host may copy the command and indicate that it does not need the command anymore
 * by setting the NEMUVHWACMD_FLAG_HG_ASYNCH_RETURNED flag */
#define NEMUVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION  0x00000004
/* the host has copied the NEMUVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION command and returned it to the guest */
#define NEMUVHWACMD_FLAG_HG_ASYNCH_RETURNED      0x00020000
/* this is the host->host cmd, i.e. a configuration command posted by the host to the framebuffer */
#define NEMUVHWACMD_FLAG_HH_CMD                  0x10000000

typedef struct NEMUVHWACMD
{
    NEMUVHWACMD_TYPE enmCmd; /* command type */
    volatile int32_t rc; /* command result */
    int32_t iDisplay; /* display index */
    volatile int32_t Flags; /* ored NEMUVHWACMD_FLAG_xxx values */
    uint64_t GuestVBVAReserved1; /* field internally used by the guest VBVA cmd handling, must NOT be modified by clients */
    uint64_t GuestVBVAReserved2; /* field internally used by the guest VBVA cmd handling, must NOT be modified by clients */
    volatile uint32_t cRefs;
    int32_t Reserved;
    union
    {
        struct NEMUVHWACMD *pNext;
        uint32_t             offNext;
        uint64_t Data; /* the body is 64-bit aligned */
    } u;
    char body[1];
} NEMUVHWACMD;

#define NEMUVHWACMD_HEADSIZE() (RT_OFFSETOF(NEMUVHWACMD, body))
#define NEMUVHWACMD_SIZE_FROMBODYSIZE(_s) (NEMUVHWACMD_HEADSIZE() + (_s))
#define NEMUVHWACMD_SIZE(_tCmd) (NEMUVHWACMD_SIZE_FROMBODYSIZE(sizeof(_tCmd)))
typedef unsigned int NEMUVHWACMD_LENGTH;
typedef uint64_t NEMUVHWA_SURFHANDLE;
#define NEMUVHWA_SURFHANDLE_INVALID 0ULL
#define NEMUVHWACMD_BODY(_p, _t) ((_t*)(_p)->body)
#define NEMUVHWACMD_HEAD(_pb) ((NEMUVHWACMD*)((uint8_t *)(_pb) - RT_OFFSETOF(NEMUVHWACMD, body)))

typedef struct NEMUVHWA_RECTL
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} NEMUVHWA_RECTL;

typedef struct NEMUVHWA_COLORKEY
{
    uint32_t low;
    uint32_t high;
} NEMUVHWA_COLORKEY;

typedef struct NEMUVHWA_PIXELFORMAT
{
    uint32_t flags;
    uint32_t fourCC;
    union
    {
        uint32_t rgbBitCount;
        uint32_t yuvBitCount;
    } c;

    union
    {
        uint32_t rgbRBitMask;
        uint32_t yuvYBitMask;
    } m1;

    union
    {
        uint32_t rgbGBitMask;
        uint32_t yuvUBitMask;
    } m2;

    union
    {
        uint32_t rgbBBitMask;
        uint32_t yuvVBitMask;
    } m3;

    union
    {
        uint32_t rgbABitMask;
    } m4;

    uint32_t Reserved;
} NEMUVHWA_PIXELFORMAT;

typedef struct NEMUVHWA_SURFACEDESC
{
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitch;
    uint32_t sizeX;
    uint32_t sizeY;
    uint32_t cBackBuffers;
    uint32_t Reserved;
    NEMUVHWA_COLORKEY DstOverlayCK;
    NEMUVHWA_COLORKEY DstBltCK;
    NEMUVHWA_COLORKEY SrcOverlayCK;
    NEMUVHWA_COLORKEY SrcBltCK;
    NEMUVHWA_PIXELFORMAT PixelFormat;
    uint32_t surfCaps;
    uint32_t Reserved2;
    NEMUVHWA_SURFHANDLE hSurf;
    uint64_t offSurface;
} NEMUVHWA_SURFACEDESC;

typedef struct NEMUVHWA_BLTFX
{
    uint32_t flags;
    uint32_t rop;
    uint32_t rotationOp;
    uint32_t rotation;
    uint32_t fillColor;
    uint32_t Reserved;
    NEMUVHWA_COLORKEY DstCK;
    NEMUVHWA_COLORKEY SrcCK;
} NEMUVHWA_BLTFX;

typedef struct NEMUVHWA_OVERLAYFX
{
    uint32_t flags;
    uint32_t Reserved1;
    uint32_t fxFlags;
    uint32_t Reserved2;
    NEMUVHWA_COLORKEY DstCK;
    NEMUVHWA_COLORKEY SrcCK;
} NEMUVHWA_OVERLAYFX;

#define NEMUVHWA_CAPS_BLT               0x00000040
#define NEMUVHWA_CAPS_BLTCOLORFILL      0x04000000
#define NEMUVHWA_CAPS_BLTFOURCC         0x00000100
#define NEMUVHWA_CAPS_BLTSTRETCH        0x00000200
#define NEMUVHWA_CAPS_BLTQUEUE          0x00000080

#define NEMUVHWA_CAPS_OVERLAY           0x00000800
#define NEMUVHWA_CAPS_OVERLAYFOURCC     0x00002000
#define NEMUVHWA_CAPS_OVERLAYSTRETCH    0x00004000
#define NEMUVHWA_CAPS_OVERLAYCANTCLIP   0x00001000

#define NEMUVHWA_CAPS_COLORKEY          0x00400000
#define NEMUVHWA_CAPS_COLORKEYHWASSIST  0x01000000

#define NEMUVHWA_SCAPS_BACKBUFFER       0x00000004
#define NEMUVHWA_SCAPS_COMPLEX          0x00000008
#define NEMUVHWA_SCAPS_FLIP             0x00000010
#define NEMUVHWA_SCAPS_FRONTBUFFER      0x00000020
#define NEMUVHWA_SCAPS_OFFSCREENPLAIN   0x00000040
#define NEMUVHWA_SCAPS_OVERLAY          0x00000080
#define NEMUVHWA_SCAPS_PRIMARYSURFACE   0x00000200
#define NEMUVHWA_SCAPS_SYSTEMMEMORY     0x00000800
#define NEMUVHWA_SCAPS_VIDEOMEMORY      0x00004000
#define NEMUVHWA_SCAPS_VISIBLE          0x00008000
#define NEMUVHWA_SCAPS_LOCALVIDMEM      0x10000000

#define NEMUVHWA_PF_PALETTEINDEXED8     0x00000020
#define NEMUVHWA_PF_RGB                 0x00000040
#define NEMUVHWA_PF_RGBTOYUV            0x00000100
#define NEMUVHWA_PF_YUV                 0x00000200
#define NEMUVHWA_PF_FOURCC              0x00000004

#define NEMUVHWA_LOCK_DISCARDCONTENTS   0x00002000

#define NEMUVHWA_CFG_ENABLED            0x00000001

#define NEMUVHWA_SD_BACKBUFFERCOUNT     0x00000020
#define NEMUVHWA_SD_CAPS                0x00000001
#define NEMUVHWA_SD_CKDESTBLT           0x00004000
#define NEMUVHWA_SD_CKDESTOVERLAY       0x00002000
#define NEMUVHWA_SD_CKSRCBLT            0x00010000
#define NEMUVHWA_SD_CKSRCOVERLAY        0x00008000
#define NEMUVHWA_SD_HEIGHT              0x00000002
#define NEMUVHWA_SD_PITCH               0x00000008
#define NEMUVHWA_SD_PIXELFORMAT         0x00001000
/*#define NEMUVHWA_SD_REFRESHRATE       0x00040000*/
#define NEMUVHWA_SD_WIDTH               0x00000004

#define NEMUVHWA_CKEYCAPS_DESTBLT                  0x00000001
#define NEMUVHWA_CKEYCAPS_DESTBLTCLRSPACE          0x00000002
#define NEMUVHWA_CKEYCAPS_DESTBLTCLRSPACEYUV       0x00000004
#define NEMUVHWA_CKEYCAPS_DESTBLTYUV               0x00000008
#define NEMUVHWA_CKEYCAPS_DESTOVERLAY              0x00000010
#define NEMUVHWA_CKEYCAPS_DESTOVERLAYCLRSPACE      0x00000020
#define NEMUVHWA_CKEYCAPS_DESTOVERLAYCLRSPACEYUV   0x00000040
#define NEMUVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE     0x00000080
#define NEMUVHWA_CKEYCAPS_DESTOVERLAYYUV           0x00000100
#define NEMUVHWA_CKEYCAPS_SRCBLT                   0x00000200
#define NEMUVHWA_CKEYCAPS_SRCBLTCLRSPACE           0x00000400
#define NEMUVHWA_CKEYCAPS_SRCBLTCLRSPACEYUV        0x00000800
#define NEMUVHWA_CKEYCAPS_SRCBLTYUV                0x00001000
#define NEMUVHWA_CKEYCAPS_SRCOVERLAY               0x00002000
#define NEMUVHWA_CKEYCAPS_SRCOVERLAYCLRSPACE       0x00004000
#define NEMUVHWA_CKEYCAPS_SRCOVERLAYCLRSPACEYUV    0x00008000
#define NEMUVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE      0x00010000
#define NEMUVHWA_CKEYCAPS_SRCOVERLAYYUV            0x00020000
#define NEMUVHWA_CKEYCAPS_NOCOSTOVERLAY            0x00040000

#define NEMUVHWA_BLT_COLORFILL                      0x00000400
#define NEMUVHWA_BLT_DDFX                           0x00000800
#define NEMUVHWA_BLT_EXTENDED_FLAGS                 0x40000000
#define NEMUVHWA_BLT_EXTENDED_LINEAR_CONTENT        0x00000004
#define NEMUVHWA_BLT_EXTENDED_PRESENTATION_STRETCHFACTOR 0x00000010
#define NEMUVHWA_BLT_KEYDESTOVERRIDE                0x00004000
#define NEMUVHWA_BLT_KEYSRCOVERRIDE                 0x00010000
#define NEMUVHWA_BLT_LAST_PRESENTATION              0x20000000
#define NEMUVHWA_BLT_PRESENTATION                   0x10000000
#define NEMUVHWA_BLT_ROP                            0x00020000


#define NEMUVHWA_OVER_DDFX                          0x00080000
#define NEMUVHWA_OVER_HIDE                          0x00000200
#define NEMUVHWA_OVER_KEYDEST                       0x00000400
#define NEMUVHWA_OVER_KEYDESTOVERRIDE               0x00000800
#define NEMUVHWA_OVER_KEYSRC                        0x00001000
#define NEMUVHWA_OVER_KEYSRCOVERRIDE                0x00002000
#define NEMUVHWA_OVER_SHOW                          0x00004000

#define NEMUVHWA_CKEY_COLORSPACE                    0x00000001
#define NEMUVHWA_CKEY_DESTBLT                       0x00000002
#define NEMUVHWA_CKEY_DESTOVERLAY                   0x00000004
#define NEMUVHWA_CKEY_SRCBLT                        0x00000008
#define NEMUVHWA_CKEY_SRCOVERLAY                    0x00000010

#define NEMUVHWA_BLT_ARITHSTRETCHY                  0x00000001
#define NEMUVHWA_BLT_MIRRORLEFTRIGHT                0x00000002
#define NEMUVHWA_BLT_MIRRORUPDOWN                   0x00000004

#define NEMUVHWA_OVERFX_ARITHSTRETCHY               0x00000001
#define NEMUVHWA_OVERFX_MIRRORLEFTRIGHT             0x00000002
#define NEMUVHWA_OVERFX_MIRRORUPDOWN                0x00000004

#define NEMUVHWA_CAPS2_CANRENDERWINDOWED            0x00080000
#define NEMUVHWA_CAPS2_WIDESURFACES                 0x00001000
#define NEMUVHWA_CAPS2_COPYFOURCC                   0x00008000
/*#define NEMUVHWA_CAPS2_FLIPINTERVAL                 0x00200000*/
/*#define NEMUVHWA_CAPS2_FLIPNOVSYNC                  0x00400000*/


#define NEMUVHWA_OFFSET64_VOID        (UINT64_MAX)

typedef struct NEMUVHWA_VERSION
{
    uint32_t maj;
    uint32_t min;
    uint32_t bld;
    uint32_t reserved;
} NEMUVHWA_VERSION;

#define NEMUVHWA_VERSION_INIT(_pv) do { \
        (_pv)->maj = NEMUVHWA_VERSION_MAJ; \
        (_pv)->min = NEMUVHWA_VERSION_MIN; \
        (_pv)->bld = NEMUVHWA_VERSION_BLD; \
        (_pv)->reserved = NEMUVHWA_VERSION_RSV; \
        } while(0)

typedef struct NEMUVHWACMD_QUERYINFO1
{
    union
    {
        struct
        {
            NEMUVHWA_VERSION guestVersion;
        } in;

        struct
        {
            uint32_t cfgFlags;
            uint32_t caps;

            uint32_t caps2;
            uint32_t colorKeyCaps;

            uint32_t stretchCaps;
            uint32_t surfaceCaps;

            uint32_t numOverlays;
            uint32_t curOverlays;

            uint32_t numFourCC;
            uint32_t reserved;
        } out;
    } u;
} NEMUVHWACMD_QUERYINFO1;

typedef struct NEMUVHWACMD_QUERYINFO2
{
    uint32_t numFourCC;
    uint32_t FourCC[1];
} NEMUVHWACMD_QUERYINFO2;

#define NEMUVHWAINFO2_SIZE(_cFourCC) RT_OFFSETOF(NEMUVHWACMD_QUERYINFO2, FourCC[_cFourCC])

typedef struct NEMUVHWACMD_SURF_CANCREATE
{
    NEMUVHWA_SURFACEDESC SurfInfo;
    union
    {
        struct
        {
            uint32_t bIsDifferentPixelFormat;
            uint32_t Reserved;
        } in;

        struct
        {
            int32_t ErrInfo;
        } out;
    } u;
} NEMUVHWACMD_SURF_CANCREATE;

typedef struct NEMUVHWACMD_SURF_CREATE
{
    NEMUVHWA_SURFACEDESC SurfInfo;
} NEMUVHWACMD_SURF_CREATE;

#ifdef NEMU_WITH_WDDM
typedef struct NEMUVHWACMD_SURF_GETINFO
{
    NEMUVHWA_SURFACEDESC SurfInfo;
} NEMUVHWACMD_SURF_GETINFO;
#endif

typedef struct NEMUVHWACMD_SURF_DESTROY
{
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hSurf;
        } in;
    } u;
} NEMUVHWACMD_SURF_DESTROY;

typedef struct NEMUVHWACMD_SURF_LOCK
{
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hSurf;
            uint64_t offSurface;
            uint32_t flags;
            uint32_t rectValid;
            NEMUVHWA_RECTL rect;
        } in;
    } u;
} NEMUVHWACMD_SURF_LOCK;

typedef struct NEMUVHWACMD_SURF_UNLOCK
{
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hSurf;
            uint32_t xUpdatedMemValid;
            uint32_t reserved;
            NEMUVHWA_RECTL xUpdatedMemRect;
        } in;
    } u;
} NEMUVHWACMD_SURF_UNLOCK;

typedef struct NEMUVHWACMD_SURF_BLT
{
    uint64_t DstGuestSurfInfo;
    uint64_t SrcGuestSurfInfo;
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hDstSurf;
            uint64_t offDstSurface;
            NEMUVHWA_RECTL dstRect;
            NEMUVHWA_SURFHANDLE hSrcSurf;
            uint64_t offSrcSurface;
            NEMUVHWA_RECTL srcRect;
            uint32_t flags;
            uint32_t xUpdatedSrcMemValid;
            NEMUVHWA_BLTFX desc;
            NEMUVHWA_RECTL xUpdatedSrcMemRect;
        } in;
    } u;
} NEMUVHWACMD_SURF_BLT;

#ifdef NEMU_WITH_WDDM
typedef struct NEMUVHWACMD_SURF_COLORFILL
{
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hSurf;
            uint64_t offSurface;
            uint32_t u32Reserved;
            uint32_t cRects;
            NEMUVHWA_RECTL aRects[1];
        } in;
    } u;
} NEMUVHWACMD_SURF_COLORFILL;
#endif

typedef struct NEMUVHWACMD_SURF_FLIP
{
    uint64_t TargGuestSurfInfo;
    uint64_t CurrGuestSurfInfo;
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hTargSurf;
            uint64_t offTargSurface;
            NEMUVHWA_SURFHANDLE hCurrSurf;
            uint64_t offCurrSurface;
            uint32_t flags;
            uint32_t xUpdatedTargMemValid;
            NEMUVHWA_RECTL xUpdatedTargMemRect;
        } in;
    } u;
} NEMUVHWACMD_SURF_FLIP;

typedef struct NEMUVHWACMD_SURF_COLORKEY_SET
{
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hSurf;
            uint64_t offSurface;
            NEMUVHWA_COLORKEY CKey;
            uint32_t flags;
            uint32_t reserved;
        } in;
    } u;
} NEMUVHWACMD_SURF_COLORKEY_SET;

#define NEMUVHWACMD_SURF_OVERLAY_UPDATE_F_SRCMEMRECT 0x00000001
#define NEMUVHWACMD_SURF_OVERLAY_UPDATE_F_DSTMEMRECT 0x00000002

typedef struct NEMUVHWACMD_SURF_OVERLAY_UPDATE
{
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hDstSurf;
            uint64_t offDstSurface;
            NEMUVHWA_RECTL dstRect;
            NEMUVHWA_SURFHANDLE hSrcSurf;
            uint64_t offSrcSurface;
            NEMUVHWA_RECTL srcRect;
            uint32_t flags;
            uint32_t xFlags;
            NEMUVHWA_OVERLAYFX desc;
            NEMUVHWA_RECTL xUpdatedSrcMemRect;
            NEMUVHWA_RECTL xUpdatedDstMemRect;
        } in;
    } u;
}NEMUVHWACMD_SURF_OVERLAY_UPDATE;

typedef struct NEMUVHWACMD_SURF_OVERLAY_SETPOSITION
{
    union
    {
        struct
        {
            NEMUVHWA_SURFHANDLE hDstSurf;
            uint64_t offDstSurface;
            NEMUVHWA_SURFHANDLE hSrcSurf;
            uint64_t offSrcSurface;
            uint32_t xPos;
            uint32_t yPos;
            uint32_t flags;
            uint32_t reserved;
        } in;
    } u;
} NEMUVHWACMD_SURF_OVERLAY_SETPOSITION;

typedef struct NEMUVHWACMD_HH_CONSTRUCT
{
    void    *pVM;
    /* VRAM info for the backend to be able to properly translate VRAM offsets */
    void    *pvVRAM;
    uint32_t cbVRAM;
} NEMUVHWACMD_HH_CONSTRUCT;

typedef struct NEMUVHWACMD_HH_SAVESTATE_SAVEPERFORM
{
    struct SSMHANDLE * pSSM;
} NEMUVHWACMD_HH_SAVESTATE_SAVEPERFORM;

typedef struct NEMUVHWACMD_HH_SAVESTATE_LOADPERFORM
{
    struct SSMHANDLE * pSSM;
} NEMUVHWACMD_HH_SAVESTATE_LOADPERFORM;

typedef DECLCALLBACK(void) FNNEMUVHWA_HH_CALLBACK(void*);
typedef FNNEMUVHWA_HH_CALLBACK *PFNNEMUVHWA_HH_CALLBACK;

#define NEMUVHWA_HH_CALLBACK_SET(_pCmd, _pfn, _parg) \
    do { \
        (_pCmd)->GuestVBVAReserved1 = (uint64_t)(uintptr_t)(_pfn); \
        (_pCmd)->GuestVBVAReserved2 = (uint64_t)(uintptr_t)(_parg); \
    }while(0)

#define NEMUVHWA_HH_CALLBACK_GET(_pCmd) ((PFNNEMUVHWA_HH_CALLBACK)(_pCmd)->GuestVBVAReserved1)
#define NEMUVHWA_HH_CALLBACK_GET_ARG(_pCmd) ((void*)(_pCmd)->GuestVBVAReserved2)

#pragma pack()
#endif /* #ifdef NEMU_WITH_VIDEOHWACCEL */

/* All structures are without alignment. */
#pragma pack(1)

typedef struct VBVAHOSTFLAGS
{
    uint32_t u32HostEvents;
    uint32_t u32SupportedOrders;
} VBVAHOSTFLAGS;

typedef struct VBVABUFFER
{
    VBVAHOSTFLAGS hostFlags;

    /* The offset where the data start in the buffer. */
    uint32_t off32Data;
    /* The offset where next data must be placed in the buffer. */
    uint32_t off32Free;

    /* The queue of record descriptions. */
    VBVARECORD aRecords[VBVA_MAX_RECORDS];
    uint32_t indexRecordFirst;
    uint32_t indexRecordFree;

    /* Space to leave free in the buffer when large partial records are transferred. */
    uint32_t cbPartialWriteThreshold;

    uint32_t cbData;
    uint8_t  au8Data[1]; /* variable size for the rest of the VBVABUFFER area in VRAM. */
} VBVABUFFER;

#define VBVA_MAX_RECORD_SIZE (128*_1M)

/* guest->host commands */
#define VBVA_QUERY_CONF32 1
#define VBVA_SET_CONF32   2
#define VBVA_INFO_VIEW    3
#define VBVA_INFO_HEAP    4
#define VBVA_FLUSH        5
#define VBVA_INFO_SCREEN  6
#define VBVA_ENABLE       7
#define VBVA_MOUSE_POINTER_SHAPE 8
#ifdef NEMU_WITH_VIDEOHWACCEL
# define VBVA_VHWA_CMD    9
#endif /* # ifdef NEMU_WITH_VIDEOHWACCEL */
#ifdef NEMU_WITH_VDMA
# define VBVA_VDMA_CTL   10 /* setup G<->H DMA channel info */
# define VBVA_VDMA_CMD    11 /* G->H DMA command             */
#endif
#define VBVA_INFO_CAPS   12 /* informs host about HGSMI caps. see VBVACAPS below */
#define VBVA_SCANLINE_CFG    13 /* configures scanline, see VBVASCANLINECFG below */
#define VBVA_SCANLINE_INFO   14 /* requests scanline info, see VBVASCANLINEINFO below */
#define VBVA_CMDVBVA_SUBMIT  16 /* inform host about VBVA Command submission */
#define VBVA_CMDVBVA_FLUSH   17 /* inform host about VBVA Command submission */
#define VBVA_CMDVBVA_CTL     18 /* G->H DMA command             */
#define VBVA_QUERY_MODE_HINTS 19 /* Query most recent mode hints sent. */
/** Report the guest virtual desktop position and size for mapping host and
 * guest pointer positions. */
#define VBVA_REPORT_INPUT_MAPPING 20
/** Report the guest cursor position and query the host position. */
#define VBVA_CURSOR_POSITION 21

/* host->guest commands */
#define VBVAHG_EVENT              1
#define VBVAHG_DISPLAY_CUSTOM     2
#ifdef NEMU_WITH_VDMA
#define VBVAHG_SHGSMI_COMPLETION  3
#endif

#ifdef NEMU_WITH_VIDEOHWACCEL
#define VBVAHG_DCUSTOM_VHWA_CMDCOMPLETE 1
#pragma pack(1)
typedef struct VBVAHOSTCMDVHWACMDCOMPLETE
{
    uint32_t offCmd;
}VBVAHOSTCMDVHWACMDCOMPLETE;
#pragma pack()
#endif /* # ifdef NEMU_WITH_VIDEOHWACCEL */

#pragma pack(1)
typedef enum
{
    VBVAHOSTCMD_OP_EVENT = 1,
    VBVAHOSTCMD_OP_CUSTOM
}VBVAHOSTCMD_OP_TYPE;

typedef struct VBVAHOSTCMDEVENT
{
    uint64_t pEvent;
}VBVAHOSTCMDEVENT;


typedef struct VBVAHOSTCMD
{
    /* destination ID if >=0 specifies display index, otherwize the command is directed to the miniport */
    int32_t iDstID;
    int32_t customOpCode;
    union
    {
        struct VBVAHOSTCMD *pNext;
        uint32_t             offNext;
        uint64_t Data; /* the body is 64-bit aligned */
    } u;
    char body[1];
}VBVAHOSTCMD;

#define VBVAHOSTCMD_SIZE(_size) (sizeof(VBVAHOSTCMD) + (_size))
#define VBVAHOSTCMD_BODY(_pCmd, _tBody) ((_tBody*)(_pCmd)->body)
#define VBVAHOSTCMD_HDR(_pBody) ((VBVAHOSTCMD*)(((uint8_t*)_pBody) - RT_OFFSETOF(VBVAHOSTCMD, body)))
#define VBVAHOSTCMD_HDRSIZE (RT_OFFSETOF(VBVAHOSTCMD, body))

#pragma pack()

/* VBVACONF32::u32Index */
#define NEMU_VBVA_CONF32_MONITOR_COUNT  0
#define NEMU_VBVA_CONF32_HOST_HEAP_SIZE 1
/** Returns VINF_SUCCESS if the host can report mode hints via VBVA.
 * Set value to VERR_NOT_SUPPORTED before calling. */
#define NEMU_VBVA_CONF32_MODE_HINT_REPORTING  2
/** Returns VINF_SUCCESS if the host can receive guest cursor information via
 * VBVA.  Set value to VERR_NOT_SUPPORTED before calling. */
#define NEMU_VBVA_CONF32_GUEST_CURSOR_REPORTING  3
/** Returns the currently available host cursor capabilities.  Available if
 * VBVACONF32::NEMU_VBVA_CONF32_GUEST_CURSOR_REPORTING returns success.
 * @see VMMDevReqMouseStatus::mouseFeatures. */
#define NEMU_VBVA_CONF32_CURSOR_CAPABILITIES  4
/** Returns the supported flags in VBVAINFOSCREEN::u8Flags. */
#define NEMU_VBVA_CONF32_SCREEN_FLAGS 5
/** Returns the max size of VBVA record. */
#define NEMU_VBVA_CONF32_MAX_RECORD_SIZE 6

typedef struct VBVACONF32
{
    uint32_t u32Index;
    uint32_t u32Value;
} VBVACONF32;

typedef struct VBVAINFOVIEW
{
    /* Index of the screen, assigned by the guest. */
    uint32_t u32ViewIndex;

    /* The screen offset in VRAM, the framebuffer starts here. */
    uint32_t u32ViewOffset;

    /* The size of the VRAM memory that can be used for the view. */
    uint32_t u32ViewSize;

    /* The recommended maximum size of the VRAM memory for the screen. */
    uint32_t u32MaxScreenSize;
} VBVAINFOVIEW;

typedef struct VBVAINFOHEAP
{
    /* Absolute offset in VRAM of the start of the heap. */
    uint32_t u32HeapOffset;

    /* The size of the heap. */
    uint32_t u32HeapSize;

} VBVAINFOHEAP;

typedef struct VBVAFLUSH
{
    uint32_t u32Reserved;

} VBVAFLUSH;

typedef struct VBVACMDVBVASUBMIT
{
    uint32_t u32Reserved;
} VBVACMDVBVASUBMIT;

/* flush is requested because due to guest command buffer overflow */
#define VBVACMDVBVAFLUSH_F_GUEST_BUFFER_OVERFLOW 1

typedef struct VBVACMDVBVAFLUSH
{
    uint32_t u32Flags;
} VBVACMDVBVAFLUSH;


/* VBVAINFOSCREEN::u8Flags */
#define VBVA_SCREEN_F_NONE     0x0000
#define VBVA_SCREEN_F_ACTIVE   0x0001
/** The virtual monitor has been disabled by the guest and should be removed
 * by the host and ignored for purposes of pointer position calculation. */
#define VBVA_SCREEN_F_DISABLED 0x0002
/** The virtual monitor has been blanked by the guest and should be blacked
 * out by the host. */
#define VBVA_SCREEN_F_BLANK    0x0004

typedef struct VBVAINFOSCREEN
{
    /* Which view contains the screen. */
    uint32_t u32ViewIndex;

    /* Physical X origin relative to the primary screen. */
    int32_t i32OriginX;

    /* Physical Y origin relative to the primary screen. */
    int32_t i32OriginY;

    /* Offset of visible framebuffer relative to the framebuffer start. */
    uint32_t u32StartOffset;

    /* The scan line size in bytes. */
    uint32_t u32LineSize;

    /* Width of the screen. */
    uint32_t u32Width;

    /* Height of the screen. */
    uint32_t u32Height;

    /* Color depth. */
    uint16_t u16BitsPerPixel;

    /* VBVA_SCREEN_F_* */
    uint16_t u16Flags;
} VBVAINFOSCREEN;


/* VBVAENABLE::u32Flags */
#define VBVA_F_NONE    0x00000000
#define VBVA_F_ENABLE  0x00000001
#define VBVA_F_DISABLE 0x00000002
/* extended VBVA to be used with WDDM */
#define VBVA_F_EXTENDED 0x00000004
/* vbva offset is absolute VRAM offset */
#define VBVA_F_ABSOFFSET 0x00000008

typedef struct VBVAENABLE
{
    uint32_t u32Flags;
    uint32_t u32Offset;
    int32_t  i32Result;
} VBVAENABLE;

typedef struct VBVAENABLE_EX
{
    VBVAENABLE Base;
    uint32_t u32ScreenId;
} VBVAENABLE_EX;


typedef struct VBVAMOUSEPOINTERSHAPE
{
    /* The host result. */
    int32_t i32Result;

    /* NEMU_MOUSE_POINTER_* bit flags. */
    uint32_t fu32Flags;

    /* X coordinate of the hot spot. */
    uint32_t u32HotX;

    /* Y coordinate of the hot spot. */
    uint32_t u32HotY;

    /* Width of the pointer in pixels. */
    uint32_t u32Width;

    /* Height of the pointer in scanlines. */
    uint32_t u32Height;

    /* Pointer data.
     *
     ****
     * The data consists of 1 bpp AND mask followed by 32 bpp XOR (color) mask.
     *
     * For pointers without alpha channel the XOR mask pixels are 32 bit values: (lsb)BGR0(msb).
     * For pointers with alpha channel the XOR mask consists of (lsb)BGRA(msb) 32 bit values.
     *
     * Guest driver must create the AND mask for pointers with alpha channel, so if host does not
     * support alpha, the pointer could be displayed as a normal color pointer. The AND mask can
     * be constructed from alpha values. For example alpha value >= 0xf0 means bit 0 in the AND mask.
     *
     * The AND mask is 1 bpp bitmap with byte aligned scanlines. Size of AND mask,
     * therefore, is cbAnd = (width + 7) / 8 * height. The padding bits at the
     * end of any scanline are undefined.
     *
     * The XOR mask follows the AND mask on the next 4 bytes aligned offset:
     * uint8_t *pXor = pAnd + (cbAnd + 3) & ~3
     * Bytes in the gap between the AND and the XOR mask are undefined.
     * XOR mask scanlines have no gap between them and size of XOR mask is:
     * cXor = width * 4 * height.
     ****
     *
     * Preallocate 4 bytes for accessing actual data as p->au8Data.
     */
    uint8_t au8Data[4];

} VBVAMOUSEPOINTERSHAPE;

/* the guest driver can handle asynch guest cmd completion by reading the command offset from io port */
#define VBVACAPS_COMPLETEGCMD_BY_IOREAD 0x00000001
/* the guest driver can handle video adapter IRQs */
#define VBVACAPS_IRQ                    0x00000002
/** The guest can read video mode hints sent via VBVA. */
#define VBVACAPS_VIDEO_MODE_HINTS       0x00000004
/** The guest can switch to a software cursor on demand. */
#define VBVACAPS_DISABLE_CURSOR_INTEGRATION 0x00000008
typedef struct VBVACAPS
{
    int32_t rc;
    uint32_t fCaps;
} VBVACAPS;

/* makes graphics device generate IRQ on VSYNC */
#define VBVASCANLINECFG_ENABLE_VSYNC_IRQ        0x00000001
/* guest driver may request the current scanline */
#define VBVASCANLINECFG_ENABLE_SCANLINE_INFO    0x00000002
/* request the current refresh period, returned in u32RefreshPeriodMs */
#define VBVASCANLINECFG_QUERY_REFRESH_PERIOD    0x00000004
/* set new refresh period specified in u32RefreshPeriodMs.
 * if used with VBVASCANLINECFG_QUERY_REFRESH_PERIOD,
 * u32RefreshPeriodMs is set to the previous refresh period on return */
#define VBVASCANLINECFG_SET_REFRESH_PERIOD      0x00000008

typedef struct VBVASCANLINECFG
{
    int32_t rc;
    uint32_t fFlags;
    uint32_t u32RefreshPeriodMs;
    uint32_t u32Reserved;
} VBVASCANLINECFG;

typedef struct VBVASCANLINEINFO
{
    int32_t rc;
    uint32_t u32ScreenId;
    uint32_t u32InVBlank;
    uint32_t u32ScanLine;
} VBVASCANLINEINFO;

/** Query the most recent mode hints received from the host. */
typedef struct VBVAQUERYMODEHINTS
{
    /** The maximum number of screens to return hints for. */
    uint16_t cHintsQueried;
    /** The size of the mode hint structures directly following this one. */
    uint16_t cbHintStructureGuest;
    /** The return code for the operation.  Initialise to VERR_NOT_SUPPORTED. */
    int32_t  rc;
} VBVAQUERYMODEHINTS;

/** Structure in which a mode hint is returned.  The guest allocates an array
 *  of these immediately after the VBVAQUERYMODEHINTS structure.  To accomodate
 *  future extensions, the VBVAQUERYMODEHINTS structure specifies the size of
 *  the VBVAMODEHINT structures allocated by the guest, and the host only fills
 *  out structure elements which fit into that size.  The host should fill any
 *  unused members (e.g. dx, dy) or structure space on the end with ~0.  The
 *  whole structure can legally be set to ~0 to skip a screen. */
typedef struct VBVAMODEHINT
{
    uint32_t magic;
    uint32_t cx;
    uint32_t cy;
    uint32_t cBPP;  /* Which has never been used... */
    uint32_t cDisplay;
    uint32_t dx;  /**< X offset into the virtual frame-buffer. */
    uint32_t dy;  /**< Y offset into the virtual frame-buffer. */
    uint32_t fEnabled;  /* Not fFlags.  Add new members for new flags. */
} VBVAMODEHINT;

#define VBVAMODEHINT_MAGIC UINT32_C(0x0801add9)

/** Report the rectangle relative to which absolute pointer events should be
 *  expressed.  This information remains valid until the next VBVA resize event
 *  for any screen, at which time it is reset to the bounding rectangle of all
 *  virtual screens and must be re-set.
 *  @see VBVA_REPORT_INPUT_MAPPING. */
typedef struct VBVAREPORTINPUTMAPPING
{
    int32_t x;    /**< Upper left X co-ordinate relative to the first screen. */
    int32_t y;    /**< Upper left Y co-ordinate relative to the first screen. */
    uint32_t cx;  /**< Rectangle width. */
    uint32_t cy;  /**< Rectangle height. */
} VBVAREPORTINPUTMAPPING;

/** Report the guest cursor position and query the host one.  The host may wish
 *  to use the guest information to re-position its own cursor (though this is
 *  currently unlikely).
 *  @see VBVA_CURSOR_POSITION */
typedef struct VBVACURSORPOSITION
{
    uint32_t fReportPosition;  /**< Are we reporting a position? */
    uint32_t x;                /**< Guest cursor X position */
    uint32_t y;                /**< Guest cursor Y position */
} VBVACURSORPOSITION;

#pragma pack()

typedef uint64_t NEMUVIDEOOFFSET;

#define NEMUVIDEOOFFSET_VOID ((NEMUVIDEOOFFSET)~0)

#pragma pack(1)

/*
 * NEMUSHGSMI made on top HGSMI and allows receiving notifications
 * about G->H command completion
 */
/* SHGSMI command header */
typedef struct NEMUSHGSMIHEADER
{
    uint64_t pvNext;    /*<- completion processing queue */
    uint32_t fFlags;    /*<- see NEMUSHGSMI_FLAG_XXX Flags */
    uint32_t cRefs;     /*<- command referece count */
    uint64_t u64Info1;  /*<- contents depends on the fFlags value */
    uint64_t u64Info2;  /*<- contents depends on the fFlags value */
} NEMUSHGSMIHEADER, *PNEMUSHGSMIHEADER;

typedef enum
{
    NEMUVDMACMD_TYPE_UNDEFINED         = 0,
    NEMUVDMACMD_TYPE_DMA_PRESENT_BLT   = 1,
    NEMUVDMACMD_TYPE_DMA_BPB_TRANSFER,
    NEMUVDMACMD_TYPE_DMA_BPB_FILL,
    NEMUVDMACMD_TYPE_DMA_PRESENT_SHADOW2PRIMARY,
    NEMUVDMACMD_TYPE_DMA_PRESENT_CLRFILL,
    NEMUVDMACMD_TYPE_DMA_PRESENT_FLIP,
    NEMUVDMACMD_TYPE_DMA_NOP,
    NEMUVDMACMD_TYPE_CHROMIUM_CMD, /* chromium cmd */
    NEMUVDMACMD_TYPE_DMA_BPB_TRANSFER_VRAMSYS,
    NEMUVDMACMD_TYPE_CHILD_STATUS_IRQ /* make the device notify child (monitor) state change IRQ */
} NEMUVDMACMD_TYPE;

#pragma pack()

/* the command processing was asynch, set by the host to indicate asynch command completion
 * must not be cleared once set, the command completion is performed by issuing a host->guest completion command
 * while keeping this flag unchanged */
#define NEMUSHGSMI_FLAG_HG_ASYNCH               0x00010000
#if 0
/* if set     - asynch completion is performed by issuing the event,
 * if cleared - asynch completion is performed by calling a callback */
#define NEMUSHGSMI_FLAG_GH_ASYNCH_EVENT         0x00000001
#endif
/* issue interrupt on asynch completion, used for critical G->H commands,
 * i.e. for completion of which guest is waiting. */
#define NEMUSHGSMI_FLAG_GH_ASYNCH_IRQ           0x00000002
/* guest does not do any op on completion of this command,
 * the host may copy the command and indicate that it does not need the command anymore
 * by not setting NEMUSHGSMI_FLAG_HG_ASYNCH */
#define NEMUSHGSMI_FLAG_GH_ASYNCH_NOCOMPLETION  0x00000004
/* guest requires the command to be processed asynchronously,
 * not setting NEMUSHGSMI_FLAG_HG_ASYNCH by the host in this case is treated as command failure */
#define NEMUSHGSMI_FLAG_GH_ASYNCH_FORCE         0x00000008
/* force IRQ on cmd completion */
#define NEMUSHGSMI_FLAG_GH_ASYNCH_IRQ_FORCE     0x00000010
/* an IRQ-level callback is associated with the command */
#define NEMUSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ  0x00000020
/* guest expects this command to be completed synchronously */
#define NEMUSHGSMI_FLAG_GH_SYNCH                0x00000040


DECLINLINE(uint8_t *) NemuSHGSMIBufferData (const NEMUSHGSMIHEADER* pHeader)
{
    return (uint8_t *)pHeader + sizeof (NEMUSHGSMIHEADER);
}

#define NemuSHGSMIBufferHeaderSize() (sizeof (NEMUSHGSMIHEADER))

DECLINLINE(PNEMUSHGSMIHEADER) NemuSHGSMIBufferHeader (const void *pvData)
{
    return (PNEMUSHGSMIHEADER)((uint8_t *)pvData - sizeof (NEMUSHGSMIHEADER));
}

#ifdef NEMU_WITH_VDMA
# pragma pack(1)

/* VDMA - Video DMA */

/* VDMA Control API */
/* NEMUVDMA_CTL::u32Flags */
typedef enum
{
    NEMUVDMA_CTL_TYPE_NONE = 0,
    NEMUVDMA_CTL_TYPE_ENABLE,
    NEMUVDMA_CTL_TYPE_DISABLE,
    NEMUVDMA_CTL_TYPE_FLUSH,
    NEMUVDMA_CTL_TYPE_WATCHDOG
} NEMUVDMA_CTL_TYPE;

typedef struct NEMUVDMA_CTL
{
    NEMUVDMA_CTL_TYPE enmCtl;
    uint32_t u32Offset;
    int32_t  i32Result;
} NEMUVDMA_CTL, *PNEMUVDMA_CTL;

typedef struct NEMUVDMA_RECTL
{
    int16_t left;
    int16_t top;
    uint16_t width;
    uint16_t height;
} NEMUVDMA_RECTL, *PNEMUVDMA_RECTL;

typedef enum
{
    NEMUVDMA_PIXEL_FORMAT_UNKNOWN      =  0,
    NEMUVDMA_PIXEL_FORMAT_R8G8B8       = 20,
    NEMUVDMA_PIXEL_FORMAT_A8R8G8B8     = 21,
    NEMUVDMA_PIXEL_FORMAT_X8R8G8B8     = 22,
    NEMUVDMA_PIXEL_FORMAT_R5G6B5       = 23,
    NEMUVDMA_PIXEL_FORMAT_X1R5G5B5     = 24,
    NEMUVDMA_PIXEL_FORMAT_A1R5G5B5     = 25,
    NEMUVDMA_PIXEL_FORMAT_A4R4G4B4     = 26,
    NEMUVDMA_PIXEL_FORMAT_R3G3B2       = 27,
    NEMUVDMA_PIXEL_FORMAT_A8           = 28,
    NEMUVDMA_PIXEL_FORMAT_A8R3G3B2     = 29,
    NEMUVDMA_PIXEL_FORMAT_X4R4G4B4     = 30,
    NEMUVDMA_PIXEL_FORMAT_A2B10G10R10  = 31,
    NEMUVDMA_PIXEL_FORMAT_A8B8G8R8     = 32,
    NEMUVDMA_PIXEL_FORMAT_X8B8G8R8     = 33,
    NEMUVDMA_PIXEL_FORMAT_G16R16       = 34,
    NEMUVDMA_PIXEL_FORMAT_A2R10G10B10  = 35,
    NEMUVDMA_PIXEL_FORMAT_A16B16G16R16 = 36,
    NEMUVDMA_PIXEL_FORMAT_A8P8         = 40,
    NEMUVDMA_PIXEL_FORMAT_P8           = 41,
    NEMUVDMA_PIXEL_FORMAT_L8           = 50,
    NEMUVDMA_PIXEL_FORMAT_A8L8         = 51,
    NEMUVDMA_PIXEL_FORMAT_A4L4         = 52,
    NEMUVDMA_PIXEL_FORMAT_V8U8         = 60,
    NEMUVDMA_PIXEL_FORMAT_L6V5U5       = 61,
    NEMUVDMA_PIXEL_FORMAT_X8L8V8U8     = 62,
    NEMUVDMA_PIXEL_FORMAT_Q8W8V8U8     = 63,
    NEMUVDMA_PIXEL_FORMAT_V16U16       = 64,
    NEMUVDMA_PIXEL_FORMAT_W11V11U10    = 65,
    NEMUVDMA_PIXEL_FORMAT_A2W10V10U10  = 67
} NEMUVDMA_PIXEL_FORMAT;

typedef struct NEMUVDMA_SURF_DESC
{
    uint32_t width;
    uint32_t height;
    NEMUVDMA_PIXEL_FORMAT format;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t fFlags;
} NEMUVDMA_SURF_DESC, *PNEMUVDMA_SURF_DESC;

/*typedef uint64_t NEMUVDMAPHADDRESS;*/
typedef uint64_t NEMUVDMASURFHANDLE;

/* region specified as a rectangle, otherwize it is a size of memory pointed to by phys address */
#define NEMUVDMAOPERAND_FLAGS_RECTL       0x1
/* Surface handle is valid */
#define NEMUVDMAOPERAND_FLAGS_PRIMARY        0x2
/* address is offset in VRAM */
#define NEMUVDMAOPERAND_FLAGS_VRAMOFFSET  0x4


/* NEMUVDMACBUF_DR::phBuf specifies offset in VRAM */
#define NEMUVDMACBUF_FLAG_BUF_VRAM_OFFSET 0x00000001
/* command buffer follows the NEMUVDMACBUF_DR in VRAM, NEMUVDMACBUF_DR::phBuf is ignored */
#define NEMUVDMACBUF_FLAG_BUF_FOLLOWS_DR  0x00000002

/*
 * We can not submit the DMA command via VRAM since we do not have control over
 * DMA command buffer [de]allocation, i.e. we only control the buffer contents.
 * In other words the system may call one of our callbacks to fill a command buffer
 * with the necessary commands and then discard the buffer w/o any notification.
 *
 * We have only DMA command buffer physical address at submission time.
 *
 * so the only way is to */
typedef struct NEMUVDMACBUF_DR
{
    uint16_t fFlags;
    uint16_t cbBuf;
    /* RT_SUCCESS()     - on success
     * VERR_INTERRUPTED - on preemption
     * VERR_xxx         - on error */
    int32_t  rc;
    union
    {
        uint64_t phBuf;
        NEMUVIDEOOFFSET offVramBuf;
    } Location;
    uint64_t aGuestData[7];
} NEMUVDMACBUF_DR, *PNEMUVDMACBUF_DR;

#define NEMUVDMACBUF_DR_TAIL(_pCmd, _t) ( (_t*)(((uint8_t*)(_pCmd)) + sizeof (NEMUVDMACBUF_DR)) )
#define NEMUVDMACBUF_DR_FROM_TAIL(_pCmd) ( (NEMUVDMACBUF_DR*)(((uint8_t*)(_pCmd)) - sizeof (NEMUVDMACBUF_DR)) )

typedef struct NEMUVDMACMD
{
    NEMUVDMACMD_TYPE enmType;
    uint32_t u32CmdSpecific;
} NEMUVDMACMD, *PNEMUVDMACMD;

#define NEMUVDMACMD_HEADER_SIZE() sizeof (NEMUVDMACMD)
#define NEMUVDMACMD_SIZE_FROMBODYSIZE(_s) (NEMUVDMACMD_HEADER_SIZE() + (_s))
#define NEMUVDMACMD_SIZE(_t) (NEMUVDMACMD_SIZE_FROMBODYSIZE(sizeof (_t)))
#define NEMUVDMACMD_BODY(_pCmd, _t) ( (_t*)(((uint8_t*)(_pCmd)) + NEMUVDMACMD_HEADER_SIZE()) )
#define NEMUVDMACMD_BODY_SIZE(_s) ( (_s) - NEMUVDMACMD_HEADER_SIZE() )
#define NEMUVDMACMD_FROM_BODY(_pCmd) ( (NEMUVDMACMD*)(((uint8_t*)(_pCmd)) - NEMUVDMACMD_HEADER_SIZE()) )
#define NEMUVDMACMD_BODY_FIELD_OFFSET(_ot, _t, _f) ( (_ot)(uintptr_t)( NEMUVDMACMD_BODY(0, uint8_t) + RT_OFFSETOF(_t, _f) ) )

typedef struct NEMUVDMACMD_DMA_PRESENT_BLT
{
    NEMUVIDEOOFFSET offSrc;
    NEMUVIDEOOFFSET offDst;
    NEMUVDMA_SURF_DESC srcDesc;
    NEMUVDMA_SURF_DESC dstDesc;
    NEMUVDMA_RECTL srcRectl;
    NEMUVDMA_RECTL dstRectl;
    uint32_t u32Reserved;
    uint32_t cDstSubRects;
    NEMUVDMA_RECTL aDstSubRects[1];
} NEMUVDMACMD_DMA_PRESENT_BLT, *PNEMUVDMACMD_DMA_PRESENT_BLT;

typedef struct NEMUVDMACMD_DMA_PRESENT_SHADOW2PRIMARY
{
    NEMUVDMA_RECTL Rect;
} NEMUVDMACMD_DMA_PRESENT_SHADOW2PRIMARY, *PNEMUVDMACMD_DMA_PRESENT_SHADOW2PRIMARY;


#define NEMUVDMACMD_DMA_BPB_TRANSFER_F_SRC_VRAMOFFSET 0x00000001
#define NEMUVDMACMD_DMA_BPB_TRANSFER_F_DST_VRAMOFFSET 0x00000002

typedef struct NEMUVDMACMD_DMA_BPB_TRANSFER
{
    uint32_t cbTransferSize;
    uint32_t fFlags;
    union
    {
        uint64_t phBuf;
        NEMUVIDEOOFFSET offVramBuf;
    } Src;
    union
    {
        uint64_t phBuf;
        NEMUVIDEOOFFSET offVramBuf;
    } Dst;
} NEMUVDMACMD_DMA_BPB_TRANSFER, *PNEMUVDMACMD_DMA_BPB_TRANSFER;

#define NEMUVDMACMD_SYSMEMEL_F_PAGELIST 0x00000001

typedef struct NEMUVDMACMD_SYSMEMEL
{
    uint32_t cPages;
    uint32_t fFlags;
    uint64_t phBuf[1];
} NEMUVDMACMD_SYSMEMEL, *PNEMUVDMACMD_SYSMEMEL;

#define NEMUVDMACMD_SYSMEMEL_NEXT(_pEl) (((_pEl)->fFlags & NEMUVDMACMD_SYSMEMEL_F_PAGELIST) ? \
        ((PNEMUVDMACMD_SYSMEMEL)(((uint8_t*)(_pEl))+RT_OFFSETOF(NEMUVDMACMD_SYSMEMEL, phBuf[(_pEl)->cPages]))) \
        : \
        ((_pEl)+1)

#define NEMUVDMACMD_DMA_BPB_TRANSFER_VRAMSYS_SYS2VRAM 0x00000001

typedef struct NEMUVDMACMD_DMA_BPB_TRANSFER_VRAMSYS
{
    uint32_t cTransferPages;
    uint32_t fFlags;
    NEMUVIDEOOFFSET offVramBuf;
    NEMUVDMACMD_SYSMEMEL FirstEl;
} NEMUVDMACMD_DMA_BPB_TRANSFER_VRAMSYS, *PNEMUVDMACMD_DMA_BPB_TRANSFER_VRAMSYS;

typedef struct NEMUVDMACMD_DMA_BPB_FILL
{
    NEMUVIDEOOFFSET offSurf;
    uint32_t cbFillSize;
    uint32_t u32FillPattern;
} NEMUVDMACMD_DMA_BPB_FILL, *PNEMUVDMACMD_DMA_BPB_FILL;

#define NEMUVDMA_CHILD_STATUS_F_CONNECTED    0x01
#define NEMUVDMA_CHILD_STATUS_F_DISCONNECTED 0x02
#define NEMUVDMA_CHILD_STATUS_F_ROTATED      0x04

typedef struct NEMUVDMA_CHILD_STATUS
{
    uint32_t iChild;
    uint8_t  fFlags;
    uint8_t  u8RotationAngle;
    uint16_t u16Reserved;
} NEMUVDMA_CHILD_STATUS, *PNEMUVDMA_CHILD_STATUS;

/* apply the aInfos are applied to all targets, the iTarget is ignored */
#define NEMUVDMACMD_CHILD_STATUS_IRQ_F_APPLY_TO_ALL 0x00000001

typedef struct NEMUVDMACMD_CHILD_STATUS_IRQ
{
    uint32_t cInfos;
    uint32_t fFlags;
    NEMUVDMA_CHILD_STATUS aInfos[1];
} NEMUVDMACMD_CHILD_STATUS_IRQ, *PNEMUVDMACMD_CHILD_STATUS_IRQ;

# pragma pack()
#endif /* #ifdef NEMU_WITH_VDMA */

#pragma pack(1)
typedef struct NEMUVDMACMD_CHROMIUM_BUFFER
{
    NEMUVIDEOOFFSET offBuffer;
    uint32_t cbBuffer;
    uint32_t u32GuestData;
    uint64_t u64GuestData;
} NEMUVDMACMD_CHROMIUM_BUFFER, *PNEMUVDMACMD_CHROMIUM_BUFFER;

typedef struct NEMUVDMACMD_CHROMIUM_CMD
{
    uint32_t cBuffers;
    uint32_t u32Reserved;
    NEMUVDMACMD_CHROMIUM_BUFFER aBuffers[1];
} NEMUVDMACMD_CHROMIUM_CMD, *PNEMUVDMACMD_CHROMIUM_CMD;

typedef enum
{
    NEMUVDMACMD_CHROMIUM_CTL_TYPE_UNKNOWN = 0,
    NEMUVDMACMD_CHROMIUM_CTL_TYPE_CRHGSMI_SETUP,
    NEMUVDMACMD_CHROMIUM_CTL_TYPE_SAVESTATE_BEGIN,
    NEMUVDMACMD_CHROMIUM_CTL_TYPE_SAVESTATE_END,
    NEMUVDMACMD_CHROMIUM_CTL_TYPE_CRHGSMI_SETUP_MAINCB,
    NEMUVDMACMD_CHROMIUM_CTL_TYPE_CRCONNECT,
    NEMUVDMACMD_CHROMIUM_CTL_TYPE_SIZEHACK = 0x7fffffff
} NEMUVDMACMD_CHROMIUM_CTL_TYPE;

typedef struct NEMUVDMACMD_CHROMIUM_CTL
{
    NEMUVDMACMD_CHROMIUM_CTL_TYPE enmType;
    uint32_t cbCmd;
} NEMUVDMACMD_CHROMIUM_CTL, *PNEMUVDMACMD_CHROMIUM_CTL;


typedef struct PDMIDISPLAYVBVACALLBACKS *HCRHGSMICMDCOMPLETION;
typedef DECLCALLBACK(int) FNCRHGSMICMDCOMPLETION(HCRHGSMICMDCOMPLETION hCompletion, PNEMUVDMACMD_CHROMIUM_CMD pCmd, int rc);
typedef FNCRHGSMICMDCOMPLETION *PFNCRHGSMICMDCOMPLETION;

/* tells whether 3D backend has some 3D overlay data displayed */
typedef DECLCALLBACK(bool) FNCROGLHASDATA(void);
typedef FNCROGLHASDATA *PFNCROGLHASDATA;

/* same as PFNCROGLHASDATA, but for specific screen */
typedef DECLCALLBACK(bool) FNCROGLHASDATAFORSCREEN(uint32_t i32ScreenID);
typedef FNCROGLHASDATAFORSCREEN *PFNCROGLHASDATAFORSCREEN;

/* callbacks chrogl gives to main */
typedef struct CR_MAIN_INTERFACE
{
    PFNCROGLHASDATA pfnHasData;
    PFNCROGLHASDATAFORSCREEN pfnHasDataForScreen;
} CR_MAIN_INTERFACE;

typedef struct NEMUVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP_MAINCB
{
    NEMUVDMACMD_CHROMIUM_CTL Hdr;
    /*in*/
    HCRHGSMICMDCOMPLETION hCompletion;
    PFNCRHGSMICMDCOMPLETION pfnCompletion;
    /*out*/
    CR_MAIN_INTERFACE MainInterface;
} NEMUVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP_MAINCB, *PNEMUVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP_MAINCB;

typedef struct NEMUCRCON_SERVER *HNEMUCRCON_SERVER;
typedef struct PDMIDISPLAYVBVACALLBACKS* HNEMUCRCON_CLIENT;

typedef struct NEMUCRCON_3DRGN_CLIENT* HNEMUCRCON_3DRGN_CLIENT;
typedef struct NEMUCRCON_3DRGN_ASYNCCLIENT* HNEMUCRCON_3DRGN_ASYNCCLIENT;

/* server callbacks */
/* submit chromium cmd */
typedef DECLCALLBACK(int) FNNEMUCRCON_SVR_CRCMD(HNEMUCRCON_SERVER hServer, PNEMUVDMACMD_CHROMIUM_CMD pCmd, uint32_t cbCmd);
typedef FNNEMUCRCON_SVR_CRCMD *PFNNEMUCRCON_SVR_CRCMD;

/* submit chromium control cmd */
typedef DECLCALLBACK(int) FNNEMUCRCON_SVR_CRCTL(HNEMUCRCON_SERVER hServer, PNEMUVDMACMD_CHROMIUM_CTL pCtl, uint32_t cbCmd);
typedef FNNEMUCRCON_SVR_CRCTL *PFNNEMUCRCON_SVR_CRCTL;

/* request 3D data.
 * The protocol is the following:
 * 1. if there is no 3D data displayed on screen, returns VINF_EOF immediately w/o calling any PFNNEMUCRCON_3DRGN_XXX callbacks
 * 2. otherwise calls PFNNEMUCRCON_3DRGN_ONSUBMIT, submits the "regions get" request to the CrOpenGL server to process it asynchronously and returns VINF_SUCCESS
 * 2.a on "regions get" request processing calls PFNNEMUCRCON_3DRGN_BEGIN,
 * 2.b then PFNNEMUCRCON_3DRGN_REPORT zero or more times for each 3D region,
 * 2.c and then PFNNEMUCRCON_3DRGN_END
 * 3. returns VERR_XXX code on failure
 * */
typedef DECLCALLBACK(int) FNNEMUCRCON_SVR_3DRGN_GET(HNEMUCRCON_SERVER hServer, HNEMUCRCON_3DRGN_CLIENT hRgnClient, uint32_t idScreen);
typedef FNNEMUCRCON_SVR_3DRGN_GET *PFNNEMUCRCON_SVR_3DRGN_GET;

/* 3D Regions Client callbacks */
/* called from the PFNNEMUCRCON_SVR_3DRGN_GET callback in case server has 3D data and is going to process the request asynchronously,
 * see comments for PFNNEMUCRCON_SVR_3DRGN_GET above */
typedef DECLCALLBACK(int) FNNEMUCRCON_3DRGN_ONSUBMIT(HNEMUCRCON_3DRGN_CLIENT hRgnClient, uint32_t idScreen, HNEMUCRCON_3DRGN_ASYNCCLIENT *phRgnAsyncClient);
typedef FNNEMUCRCON_3DRGN_ONSUBMIT *PFNNEMUCRCON_3DRGN_ONSUBMIT;

/* called from the "regions get" command processing thread, to indicate that the "regions get" is started.
 * see comments for PFNNEMUCRCON_SVR_3DRGN_GET above */
typedef DECLCALLBACK(int) FNNEMUCRCON_3DRGN_BEGIN(HNEMUCRCON_3DRGN_ASYNCCLIENT hRgnAsyncClient, uint32_t idScreen);
typedef FNNEMUCRCON_3DRGN_BEGIN *PFNNEMUCRCON_3DRGN_BEGIN;

/* called from the "regions get" command processing thread, to report a 3D region.
 * see comments for PFNNEMUCRCON_SVR_3DRGN_GET above */
typedef DECLCALLBACK(int) FNNEMUCRCON_3DRGN_REPORT(HNEMUCRCON_3DRGN_ASYNCCLIENT hRgnAsyncClient, uint32_t idScreen, void *pvData, uint32_t cbStride, const RTRECT *pRect);
typedef FNNEMUCRCON_3DRGN_REPORT *PFNNEMUCRCON_3DRGN_REPORT;

/* called from the "regions get" command processing thread, to indicate that the "regions get" is completed.
 * see comments for PFNNEMUCRCON_SVR_3DRGN_GET above */
typedef DECLCALLBACK(int) FNNEMUCRCON_3DRGN_END(HNEMUCRCON_3DRGN_ASYNCCLIENT hRgnAsyncClient, uint32_t idScreen);
typedef FNNEMUCRCON_3DRGN_END *PFNNEMUCRCON_3DRGN_END;


/* client callbacks */
/* complete chromium cmd */
typedef DECLCALLBACK(int) FNNEMUCRCON_CLT_CRCTL_COMPLETE(HNEMUCRCON_CLIENT hClient, PNEMUVDMACMD_CHROMIUM_CTL pCtl, int rc);
typedef FNNEMUCRCON_CLT_CRCTL_COMPLETE *PFNNEMUCRCON_CLT_CRCTL_COMPLETE;

/* complete chromium control cmd */
typedef DECLCALLBACK(int) FNNEMUCRCON_CLT_CRCMD_COMPLETE(HNEMUCRCON_CLIENT hClient, PNEMUVDMACMD_CHROMIUM_CMD pCmd, int rc);
typedef FNNEMUCRCON_CLT_CRCMD_COMPLETE *PFNNEMUCRCON_CLT_CRCMD_COMPLETE;

typedef struct NEMUCRCON_SERVER_CALLBACKS
{
    HNEMUCRCON_SERVER hServer;
    PFNNEMUCRCON_SVR_CRCMD pfnCrCmd;
    PFNNEMUCRCON_SVR_CRCTL pfnCrCtl;
    PFNNEMUCRCON_SVR_3DRGN_GET pfn3DRgnGet;
} NEMUCRCON_SERVER_CALLBACKS, *PNEMUCRCON_SERVER_CALLBACKS;

typedef struct NEMUCRCON_CLIENT_CALLBACKS
{
    HNEMUCRCON_CLIENT hClient;
    PFNNEMUCRCON_CLT_CRCMD_COMPLETE pfnCrCmdComplete;
    PFNNEMUCRCON_CLT_CRCTL_COMPLETE pfnCrCtlComplete;
    PFNNEMUCRCON_3DRGN_ONSUBMIT pfn3DRgnOnSubmit;
    PFNNEMUCRCON_3DRGN_BEGIN pfn3DRgnBegin;
    PFNNEMUCRCON_3DRGN_REPORT pfn3DRgnReport;
    PFNNEMUCRCON_3DRGN_END pfn3DRgnEnd;
} NEMUCRCON_CLIENT_CALLBACKS, *PNEMUCRCON_CLIENT_CALLBACKS;

/* issued by Main to establish connection between Main and CrOpenGL service */
typedef struct NEMUVDMACMD_CHROMIUM_CTL_CRCONNECT
{
    NEMUVDMACMD_CHROMIUM_CTL Hdr;
    /*input (filled by Client) :*/
    /*class VMMDev*/void *pVMMDev;
    NEMUCRCON_CLIENT_CALLBACKS ClientCallbacks;
    /*output (filled by Server) :*/
    NEMUCRCON_SERVER_CALLBACKS ServerCallbacks;
} NEMUVDMACMD_CHROMIUM_CTL_CRCONNECT, *PNEMUVDMACMD_CHROMIUM_CTL_CRCONNECT;

/* ring command buffer dr */
#define NEMUCMDVBVA_STATE_SUBMITTED   1
#define NEMUCMDVBVA_STATE_CANCELLED   2
#define NEMUCMDVBVA_STATE_IN_PROGRESS 3
/* the "completed" state is signalled via the ring buffer values */

/* CrHgsmi command */
#define NEMUCMDVBVA_OPTYPE_CRCMD                        1
/* blit command that does blitting of allocations identified by VRAM offset or host id
 * for VRAM-offset ones the size and format are same as primary */
#define NEMUCMDVBVA_OPTYPE_BLT                          2
/* flip */
#define NEMUCMDVBVA_OPTYPE_FLIP                         3
/* ColorFill */
#define NEMUCMDVBVA_OPTYPE_CLRFILL                      4
/* allocation paging transfer request */
#define NEMUCMDVBVA_OPTYPE_PAGING_TRANSFER              5
/* allocation paging fill request */
#define NEMUCMDVBVA_OPTYPE_PAGING_FILL                  6
/* same as NEMUCMDVBVA_OPTYPE_NOP, but contains NEMUCMDVBVA_HDR data */
#define NEMUCMDVBVA_OPTYPE_NOPCMD                       7
/* actual command is stored in guest system memory */
#define NEMUCMDVBVA_OPTYPE_SYSMEMCMD                    8
/* complex command - i.e. can contain multiple commands
 * i.e. the NEMUCMDVBVA_OPTYPE_COMPLEXCMD NEMUCMDVBVA_HDR is followed
 * by one or more NEMUCMDVBVA_HDR commands.
 * Each command's size is specified in it's NEMUCMDVBVA_HDR's u32FenceID field */
#define NEMUCMDVBVA_OPTYPE_COMPLEXCMD                   9

/* nop - is a one-bit command. The buffer size to skip is determined by VBVA buffer size */
#define NEMUCMDVBVA_OPTYPE_NOP                          0x80

/* u8Flags flags */
/* transfer from RAM to Allocation */
#define NEMUCMDVBVA_OPF_PAGING_TRANSFER_IN                  0x80

#define NEMUCMDVBVA_OPF_BLT_TYPE_SAMEDIM_A8R8G8B8           0
#define NEMUCMDVBVA_OPF_BLT_TYPE_GENERIC_A8R8G8B8           1
#define NEMUCMDVBVA_OPF_BLT_TYPE_OFFPRIMSZFMT_OR_ID         2

#define NEMUCMDVBVA_OPF_BLT_TYPE_MASK                       3


#define NEMUCMDVBVA_OPF_CLRFILL_TYPE_GENERIC_A8R8G8B8       0

#define NEMUCMDVBVA_OPF_CLRFILL_TYPE_MASK                   1


/* blit direction is from first operand to second */
#define NEMUCMDVBVA_OPF_BLT_DIR_IN_2                        0x10
/* operand 1 contains host id */
#define NEMUCMDVBVA_OPF_OPERAND1_ISID                       0x20
/* operand 2 contains host id */
#define NEMUCMDVBVA_OPF_OPERAND2_ISID                       0x40
/* primary hint id is src */
#define NEMUCMDVBVA_OPF_PRIMARY_HINT_SRC                    0x80

/* trying to make the header as small as possible,
 * we'd have pretty few op codes actually, so 8bit is quite enough,
 * we will be able to extend it in any way. */
typedef struct NEMUCMDVBVA_HDR
{
    /* one NEMUCMDVBVA_OPTYPE_XXX, except NOP, see comments above */
    uint8_t u8OpCode;
    /* command-specific
     * NEMUCMDVBVA_OPTYPE_CRCMD                     - must be null
     * NEMUCMDVBVA_OPTYPE_BLT                       - OR-ed NEMUCMDVBVA_OPF_ALLOC_XXX flags
     * NEMUCMDVBVA_OPTYPE_PAGING_TRANSFER           - must be null
     * NEMUCMDVBVA_OPTYPE_PAGING_FILL               - must be null
     * NEMUCMDVBVA_OPTYPE_NOPCMD                    - must be null
     * NEMUCMDVBVA_OPTYPE_NOP                       - not applicable (as the entire NEMUCMDVBVA_HDR is not valid) */
    uint8_t u8Flags;
    /* one of NEMUCMDVBVA_STATE_XXX*/
    volatile uint8_t u8State;
    union
    {
        /* result, 0 on success, otherwise contains the failure code TBD */
        int8_t i8Result;
        uint8_t u8PrimaryID;
    } u;
    union
    {
        /* complex command (NEMUCMDVBVA_OPTYPE_COMPLEXCMD) element data */
        struct
        {
            /* command length */
            uint16_t u16CbCmdHost;
            /* guest-specific data, host expects it to be NULL */
            uint16_t u16CbCmdGuest;
        } complexCmdEl;
        /* DXGK DDI fence ID */
        uint32_t u32FenceID;
    } u2;
} NEMUCMDVBVA_HDR;

typedef uint32_t NEMUCMDVBVAOFFSET;
typedef uint64_t NEMUCMDVBVAPHADDR;
typedef uint32_t NEMUCMDVBVAPAGEIDX;

typedef struct NEMUCMDVBVA_CRCMD_BUFFER
{
    uint32_t cbBuffer;
    NEMUCMDVBVAOFFSET offBuffer;
} NEMUCMDVBVA_CRCMD_BUFFER;

typedef struct NEMUCMDVBVA_CRCMD_CMD
{
    uint32_t cBuffers;
    NEMUCMDVBVA_CRCMD_BUFFER aBuffers[1];
} NEMUCMDVBVA_CRCMD_CMD;

typedef struct NEMUCMDVBVA_CRCMD
{
    NEMUCMDVBVA_HDR Hdr;
    NEMUCMDVBVA_CRCMD_CMD Cmd;
} NEMUCMDVBVA_CRCMD;

typedef struct NEMUCMDVBVA_ALLOCINFO
{
    union
    {
        NEMUCMDVBVAOFFSET offVRAM;
        uint32_t id;
    } u;
} NEMUCMDVBVA_ALLOCINFO;

typedef struct NEMUCMDVBVA_ALLOCDESC
{
    NEMUCMDVBVA_ALLOCINFO Info;
    uint16_t u16Width;
    uint16_t u16Height;
} NEMUCMDVBVA_ALLOCDESC;

typedef struct NEMUCMDVBVA_RECT
{
   /** Coordinates of affected rectangle. */
   int16_t xLeft;
   int16_t yTop;
   int16_t xRight;
   int16_t yBottom;
} NEMUCMDVBVA_RECT;

typedef struct NEMUCMDVBVA_POINT
{
   int16_t x;
   int16_t y;
} NEMUCMDVBVA_POINT;

typedef struct NEMUCMDVBVA_BLT_HDR
{
    NEMUCMDVBVA_HDR Hdr;
    NEMUCMDVBVA_POINT Pos;
} NEMUCMDVBVA_BLT_HDR;

typedef struct NEMUCMDVBVA_BLT_PRIMARY
{
    NEMUCMDVBVA_BLT_HDR Hdr;
    NEMUCMDVBVA_ALLOCINFO alloc;
    /* the rects count is determined from the command size */
    NEMUCMDVBVA_RECT aRects[1];
} NEMUCMDVBVA_BLT_PRIMARY;

typedef struct NEMUCMDVBVA_BLT_PRIMARY_GENERIC_A8R8G8B8
{
    NEMUCMDVBVA_BLT_HDR Hdr;
    NEMUCMDVBVA_ALLOCDESC alloc;
    /* the rects count is determined from the command size */
    NEMUCMDVBVA_RECT aRects[1];
} NEMUCMDVBVA_BLT_PRIMARY_GENERIC_A8R8G8B8;

typedef struct NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID
{
    NEMUCMDVBVA_BLT_HDR Hdr;
    NEMUCMDVBVA_ALLOCINFO alloc;
    uint32_t id;
    /* the rects count is determined from the command size */
    NEMUCMDVBVA_RECT aRects[1];
} NEMUCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID;

typedef struct NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8
{
    NEMUCMDVBVA_BLT_HDR Hdr;
    NEMUCMDVBVA_ALLOCDESC alloc1;
    NEMUCMDVBVA_ALLOCINFO info2;
    /* the rects count is determined from the command size */
    NEMUCMDVBVA_RECT aRects[1];
} NEMUCMDVBVA_BLT_SAMEDIM_A8R8G8B8;

typedef struct NEMUCMDVBVA_BLT_GENERIC_A8R8G8B8
{
    NEMUCMDVBVA_BLT_HDR Hdr;
    NEMUCMDVBVA_ALLOCDESC alloc1;
    NEMUCMDVBVA_ALLOCDESC alloc2;
    /* the rects count is determined from the command size */
    NEMUCMDVBVA_RECT aRects[1];
} NEMUCMDVBVA_BLT_GENERIC_A8R8G8B8;

#define NEMUCMDVBVA_SIZEOF_BLTSTRUCT_MAX (sizeof (NEMUCMDVBVA_BLT_GENERIC_A8R8G8B8))

typedef struct NEMUCMDVBVA_FLIP
{
    NEMUCMDVBVA_HDR Hdr;
    NEMUCMDVBVA_ALLOCINFO src;
    NEMUCMDVBVA_RECT aRects[1];
} NEMUCMDVBVA_FLIP;

#define NEMUCMDVBVA_SIZEOF_FLIPSTRUCT_MIN (RT_OFFSETOF(NEMUCMDVBVA_FLIP, aRects))

typedef struct NEMUCMDVBVA_CLRFILL_HDR
{
    NEMUCMDVBVA_HDR Hdr;
    uint32_t u32Color;
} NEMUCMDVBVA_CLRFILL_HDR;

typedef struct NEMUCMDVBVA_CLRFILL_PRIMARY
{
    NEMUCMDVBVA_CLRFILL_HDR Hdr;
    NEMUCMDVBVA_RECT aRects[1];
} NEMUCMDVBVA_CLRFILL_PRIMARY;

typedef struct NEMUCMDVBVA_CLRFILL_GENERIC_A8R8G8B8
{
    NEMUCMDVBVA_CLRFILL_HDR Hdr;
    NEMUCMDVBVA_ALLOCDESC dst;
    NEMUCMDVBVA_RECT aRects[1];
} NEMUCMDVBVA_CLRFILL_GENERIC_A8R8G8B8;

#define NEMUCMDVBVA_SIZEOF_CLRFILLSTRUCT_MAX (sizeof (NEMUCMDVBVA_CLRFILL_GENERIC_A8R8G8B8))

#if 0
#define NEMUCMDVBVA_SYSMEMEL_CPAGES_MAX  0x1000

typedef struct NEMUCMDVBVA_SYSMEMEL
{
    uint32_t cPagesAfterFirst  : 12;
    uint32_t iPage1            : 20;
    uint32_t iPage2;
} NEMUCMDVBVA_SYSMEMEL;
#endif

typedef struct NEMUCMDVBVA_PAGING_TRANSFER_DATA
{
    /* for now can only contain offVRAM.
     * paging transfer can NOT be initiated for allocations having host 3D object (hostID) associated */
    NEMUCMDVBVA_ALLOCINFO Alloc;
    NEMUCMDVBVAPAGEIDX aPageNumbers[1];
} NEMUCMDVBVA_PAGING_TRANSFER_DATA;

typedef struct NEMUCMDVBVA_PAGING_TRANSFER
{
    NEMUCMDVBVA_HDR Hdr;
    NEMUCMDVBVA_PAGING_TRANSFER_DATA Data;
} NEMUCMDVBVA_PAGING_TRANSFER;

typedef struct NEMUCMDVBVA_PAGING_FILL
{
    NEMUCMDVBVA_HDR Hdr;
    uint32_t u32CbFill;
    uint32_t u32Pattern;
    /* paging transfer can NOT be initiated for allocations having host 3D object (hostID) associated */
    NEMUCMDVBVAOFFSET offVRAM;
} NEMUCMDVBVA_PAGING_FILL;

typedef struct NEMUCMDVBVA_SYSMEMCMD
{
    NEMUCMDVBVA_HDR Hdr;
    NEMUCMDVBVAPHADDR phCmd;
} NEMUCMDVBVA_SYSMEMCMD;

#define NEMUCMDVBVACTL_TYPE_ENABLE     1
#define NEMUCMDVBVACTL_TYPE_3DCTL      2
#define NEMUCMDVBVACTL_TYPE_RESIZE     3

typedef struct NEMUCMDVBVA_CTL
{
    uint32_t u32Type;
    int32_t i32Result;
} NEMUCMDVBVA_CTL;

typedef struct NEMUCMDVBVA_CTL_ENABLE
{
    NEMUCMDVBVA_CTL Hdr;
    VBVAENABLE Enable;
} NEMUCMDVBVA_CTL_ENABLE;

#define NEMUCMDVBVA_SCREENMAP_SIZE(_elType) ((NEMU_VIDEO_MAX_SCREENS + sizeof (_elType) - 1) / sizeof (_elType))
#define NEMUCMDVBVA_SCREENMAP_DECL(_elType, _name) _elType _name[NEMUCMDVBVA_SCREENMAP_SIZE(_elType)]

typedef struct NEMUCMDVBVA_RESIZE_ENTRY
{
    VBVAINFOSCREEN Screen;
    NEMUCMDVBVA_SCREENMAP_DECL(uint32_t, aTargetMap);
} NEMUCMDVBVA_RESIZE_ENTRY;

typedef struct NEMUCMDVBVA_RESIZE
{
    NEMUCMDVBVA_RESIZE_ENTRY aEntries[1];
} NEMUCMDVBVA_RESIZE;

typedef struct NEMUCMDVBVA_CTL_RESIZE
{
    NEMUCMDVBVA_CTL Hdr;
    NEMUCMDVBVA_RESIZE Resize;
} NEMUCMDVBVA_CTL_RESIZE;

#define NEMUCMDVBVA3DCTL_TYPE_CONNECT     1
#define NEMUCMDVBVA3DCTL_TYPE_DISCONNECT  2
#define NEMUCMDVBVA3DCTL_TYPE_CMD         3

typedef struct NEMUCMDVBVA_3DCTL
{
    uint32_t u32Type;
    uint32_t u32CmdClientId;
} NEMUCMDVBVA_3DCTL;

typedef struct NEMUCMDVBVA_3DCTL_CONNECT
{
    NEMUCMDVBVA_3DCTL Hdr;
    uint32_t u32MajorVersion;
    uint32_t u32MinorVersion;
    uint64_t u64Pid;
} NEMUCMDVBVA_3DCTL_CONNECT;

typedef struct NEMUCMDVBVA_3DCTL_CMD
{
    NEMUCMDVBVA_3DCTL Hdr;
    NEMUCMDVBVA_HDR Cmd;
} NEMUCMDVBVA_3DCTL_CMD;

typedef struct NEMUCMDVBVA_CTL_3DCTL_CMD
{
    NEMUCMDVBVA_CTL Hdr;
    NEMUCMDVBVA_3DCTL_CMD Cmd;
} NEMUCMDVBVA_CTL_3DCTL_CMD;

typedef struct NEMUCMDVBVA_CTL_3DCTL_CONNECT
{
    NEMUCMDVBVA_CTL Hdr;
    NEMUCMDVBVA_3DCTL_CONNECT Connect;
} NEMUCMDVBVA_CTL_3DCTL_CONNECT;

typedef struct NEMUCMDVBVA_CTL_3DCTL
{
    NEMUCMDVBVA_CTL Hdr;
    NEMUCMDVBVA_3DCTL Ctl;
} NEMUCMDVBVA_CTL_3DCTL;

#pragma pack()


#ifdef NEMUVDMA_WITH_VBVA
# pragma pack(1)

typedef struct NEMUVDMAVBVACMD
{
    HGSMIOFFSET offCmd;
} NEMUVDMAVBVACMD;

#pragma pack()
#endif

#endif
