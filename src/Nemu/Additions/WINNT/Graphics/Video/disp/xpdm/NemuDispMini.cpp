/* $Id: NemuDispMini.cpp $ */

/** @file
 * Nemu XPDM Display driver, helper functions which interacts with our miniport driver
 */

/*
 * Copyright (C) 2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "NemuDisp.h"
#include "NemuDispMini.h"
#include <iprt/asm.h>

/* Returns if given video mode is supported by display driver */
static BOOL NemuDispVideoModeSupported(const PVIDEO_MODE_INFORMATION pMode)
{
    if ((pMode->NumberOfPlanes==1)
        && (pMode->AttributeFlags & VIDEO_MODE_GRAPHICS)
        && !(pMode->AttributeFlags & VIDEO_MODE_BANKED)
        && (pMode->BitsPerPlane==8 || pMode->BitsPerPlane==16 || pMode->BitsPerPlane==24 || pMode->BitsPerPlane==32))
    {
        return TRUE;
    }
    return FALSE;
}

/* Returns list video modes supported by both miniport and display driver.
 * Note: caller is resposible to free up ppModesTable.
 */
int NemuDispMPGetVideoModes(HANDLE hDriver, PVIDEO_MODE_INFORMATION *ppModesTable, ULONG *pcModes)
{
    DWORD dwrc;
    VIDEO_NUM_MODES numModes;
    ULONG cbReturned, i, j, cSupportedModes;
    PVIDEO_MODE_INFORMATION pMiniportModes, pMode;

    LOGF_ENTER();

    /* Get number of video modes supported by miniport */
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES, NULL, 0,
                              &numModes, sizeof(VIDEO_NUM_MODES), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    if (numModes.ModeInformationLength != sizeof(VIDEO_MODE_INFORMATION))
    {
        WARN(("sizeof(VIDEO_MODE_INFORMATION) differs for miniport and display drivers. "
              "Check that both are compiled with same ddk version!"));
    }

    /* Allocate temp buffer */
    pMiniportModes = (PVIDEO_MODE_INFORMATION)
                     EngAllocMem(0, numModes.NumModes*numModes.ModeInformationLength, MEM_ALLOC_TAG);

    if (!pMiniportModes)
    {
        WARN(("not enough memory!"));
        return VERR_NO_MEMORY;
    }

    /* Get video modes supported by miniport */
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_AVAIL_MODES, NULL, 0,
                              pMiniportModes, numModes.NumModes*numModes.ModeInformationLength, &cbReturned);
    if (dwrc != NO_ERROR)
    {
        EngFreeMem(pMiniportModes);
        NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    }

    /* Check which of miniport modes are supprted by display driver.
     * Note: size of VIDEO_MODE_INFORMATION is returned by miniport driver in numModes.ModeInformationLength,
     *       it might be different from the one we have here.
     */
    cSupportedModes = 0;
    pMode = pMiniportModes;
    for (i=0; i<numModes.NumModes; ++i)
    {
        /*sanity check*/
        if (pMode->Length != sizeof(VIDEO_MODE_INFORMATION))
        {
            WARN(("Unexpected mode len %i expected %i!", pMode->Length, sizeof(VIDEO_MODE_INFORMATION)));
        }

        if (NemuDispVideoModeSupported(pMode))
        {
            cSupportedModes++;
        }
        else
        {
            pMode->Length = 0;
        }

        pMode = (PVIDEO_MODE_INFORMATION) (((PUCHAR)pMode)+numModes.ModeInformationLength);
    }
    *pcModes = cSupportedModes;

    if (0==cSupportedModes)
    {
        WARN(("0 video modes supported!"));
        EngFreeMem(pMiniportModes);
        return VERR_NOT_SUPPORTED;
    }

    /* Allocate and zero output buffer */
    *ppModesTable = (PVIDEO_MODE_INFORMATION)
                    EngAllocMem(FL_ZERO_MEMORY, cSupportedModes*sizeof(VIDEO_MODE_INFORMATION), MEM_ALLOC_TAG);

    if (!*ppModesTable)
    {
        WARN(("not enough memory!"));
        EngFreeMem(pMiniportModes);
        return VERR_NO_MEMORY;
    }

    /* Copy supported modes to output buffer */
    pMode = pMiniportModes;
    for (j=0, i=0; i<numModes.NumModes; ++i)
    {
        if (pMode->Length != 0)
        {
            memcpy(&(*ppModesTable)[j], pMode, numModes.ModeInformationLength);
            ++j;
        }

        pMode = (PVIDEO_MODE_INFORMATION) (((PUCHAR)pMode)+numModes.ModeInformationLength);
    }
    Assert(j==cSupportedModes);

    /* Free temp buffer */
    EngFreeMem(pMiniportModes);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

/* Query miniport for mouse pointer caps */
int NemuDispMPGetPointerCaps(HANDLE hDriver, PVIDEO_POINTER_CAPABILITIES pCaps)
{
    DWORD dwrc;
    ULONG cbReturned;

    LOGF_ENTER();

    memset(pCaps, 0, sizeof(VIDEO_POINTER_CAPABILITIES));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES, NULL, 0,
                              pCaps, sizeof(VIDEO_POINTER_CAPABILITIES), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    NEMU_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES", cbReturned, sizeof(VIDEO_POINTER_CAPABILITIES), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

/* Set device mode */
int NemuDispMPSetCurrentMode(HANDLE hDriver, ULONG ulMode)
{
    DWORD dwrc;
    ULONG cbReturned;
    VIDEO_MODE mode;
    LOGF_ENTER();

    mode.RequestedMode = ulMode;
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_SET_CURRENT_MODE, &mode, sizeof(VIDEO_MODE), NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

/* Map device framebuffer and VRAM to our virtual address space */
int NemuDispMPMapMemory(PNEMUDISPDEV pDev, PVIDEO_MEMORY_INFORMATION pMemInfo)
{
    DWORD dwrc;
    ULONG cbReturned;
    VIDEO_MEMORY vMem;
    VIDEO_MEMORY_INFORMATION vMemInfo;
    LOGF_ENTER();

    Assert(!pDev->memInfo.FrameBufferBase && !pDev->memInfo.VideoRamBase);

    vMem.RequestedVirtualAddress = NULL;
    dwrc = EngDeviceIoControl(pDev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY, &vMem, sizeof(vMem), &vMemInfo, sizeof(vMemInfo), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    NEMU_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_MAP_VIDEO_MEMORY", cbReturned, sizeof(vMemInfo), VERR_DEV_IO_ERROR);

    if (vMemInfo.FrameBufferBase != vMemInfo.VideoRamBase)
    {
        WARN(("FrameBufferBase!=VideoRamBase."));
        return VERR_GENERAL_FAILURE;
    }

    /* Check if we can access mapped memory */
    uint32_t magic = (*(ULONG *)vMemInfo.FrameBufferBase == 0xDEADF00D) ? 0xBAADF00D : 0xDEADF00D;

    ASMAtomicWriteU32((uint32_t *)vMemInfo.FrameBufferBase, magic);
    if (ASMAtomicReadU32((uint32_t *)vMemInfo.FrameBufferBase) != magic)
    {
        WARN(("can't write to framebuffer memory!"));
        return VERR_GENERAL_FAILURE;
    }

    memcpy(pMemInfo, &vMemInfo, sizeof(vMemInfo));

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPUnmapMemory(PNEMUDISPDEV pDev)
{
    DWORD dwrc;
    ULONG cbReturned;
    VIDEO_MEMORY vMem;
    LOGF_ENTER();

    vMem.RequestedVirtualAddress = pDev->memInfo.VideoRamBase;
    dwrc = EngDeviceIoControl(pDev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY, &vMem, sizeof(vMem), NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    memset(&pDev->memInfo, 0, sizeof(VIDEO_MEMORY_INFORMATION));

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPQueryHGSMIInfo(HANDLE hDriver, QUERYHGSMIRESULT *pInfo)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    memset(pInfo, 0, sizeof(QUERYHGSMIRESULT));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_HGSMI_INFO, NULL, 0,
                              pInfo, sizeof(QUERYHGSMIRESULT), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    NEMU_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_QUERY_HGSMI_INFO", cbReturned, sizeof(QUERYHGSMIRESULT), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPQueryHGSMICallbacks(HANDLE hDriver, HGSMIQUERYCALLBACKS *pCallbacks)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    memset(pCallbacks, 0, sizeof(HGSMIQUERYCALLBACKS));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS, NULL, 0,
                              pCallbacks, sizeof(HGSMIQUERYCALLBACKS), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    NEMU_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS", cbReturned, sizeof(HGSMIQUERYCALLBACKS), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPHGSMIQueryPortProcs(HANDLE hDriver, HGSMIQUERYCPORTPROCS *pPortProcs)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    memset(pPortProcs, 0, sizeof(HGSMIQUERYCPORTPROCS));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS, NULL, 0,
                              pPortProcs, sizeof(HGSMIQUERYCPORTPROCS), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    NEMU_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS", cbReturned, sizeof(HGSMIQUERYCPORTPROCS), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPVHWAQueryInfo(HANDLE hDriver, VHWAQUERYINFO *pInfo)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    memset(pInfo, 0, sizeof(VHWAQUERYINFO));
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_VHWA_QUERY_INFO, NULL, 0,
                              pInfo, sizeof(VHWAQUERYINFO), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    NEMU_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_VHWA_QUERY_INFO", cbReturned, sizeof(VHWAQUERYINFO), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPSetColorRegisters(HANDLE hDriver, PVIDEO_CLUT pClut, DWORD cbClut)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_SET_COLOR_REGISTERS, pClut, cbClut, NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPDisablePointer(HANDLE hDriver)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_DISABLE_POINTER, NULL, 0, NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPSetPointerPosition(HANDLE hDriver, PVIDEO_POINTER_POSITION pPos)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_SET_POINTER_POSITION, pPos, sizeof(VIDEO_POINTER_POSITION),
                              NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPSetPointerAttrs(PNEMUDISPDEV pDev)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    Assert(pDev->pointer.pAttrs);

    dwrc = EngDeviceIoControl(pDev->hDriver, IOCTL_VIDEO_SET_POINTER_ATTR, pDev->pointer.pAttrs, pDev->pointer.cbAttrs,
                              NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPSetVisibleRegion(HANDLE hDriver, PRTRECT pRects, DWORD cRects)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_NEMU_SETVISIBLEREGION, pRects, cRects*sizeof(RTRECT),
                              NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPResetDevice(HANDLE hDriver)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_RESET_DEVICE, NULL, 0, NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPShareVideoMemory(HANDLE hDriver, PVIDEO_SHARE_MEMORY pSMem, PVIDEO_SHARE_MEMORY_INFORMATION pSMemInfo)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_SHARE_VIDEO_MEMORY, pSMem, sizeof(VIDEO_SHARE_MEMORY),
                              pSMemInfo, sizeof(VIDEO_SHARE_MEMORY_INFORMATION), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    NEMU_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_SHARE_VIDEO_MEMORY", cbReturned,
                            sizeof(VIDEO_SHARE_MEMORY_INFORMATION), VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPUnshareVideoMemory(HANDLE hDriver, PVIDEO_SHARE_MEMORY pSMem)
{
    DWORD dwrc;
    ULONG cbReturned;
    LOGF_ENTER();

    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_UNSHARE_VIDEO_MEMORY, pSMem, sizeof(VIDEO_SHARE_MEMORY),
                              NULL, 0, &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);

    LOGF_LEAVE();
    return VINF_SUCCESS;
}

int NemuDispMPQueryRegistryFlags(HANDLE hDriver, ULONG *pulFlags)
{
    DWORD dwrc;
    ULONG cbReturned;
    ULONG ulInfoLevel;
    LOGF_ENTER();

    *pulFlags = 0;
    ulInfoLevel = NEMUVIDEO_INFO_LEVEL_REGISTRY_FLAGS;
    dwrc = EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_NEMUVIDEO_INFO, &ulInfoLevel, sizeof(DWORD),
                              pulFlags, sizeof(DWORD), &cbReturned);
    NEMU_CHECK_WINERR_RETRC(dwrc, VERR_DEV_IO_ERROR);
    NEMU_WARN_IOCTLCB_RETRC("IOCTL_VIDEO_QUERY_INFO", cbReturned, sizeof(DWORD), VERR_DEV_IO_ERROR);

    if (*pulFlags != 0)
        LogRel(("NemuDisp: video flags 0x%08X\n", *pulFlags));

    LOGF_LEAVE();
    return VINF_SUCCESS;
}
