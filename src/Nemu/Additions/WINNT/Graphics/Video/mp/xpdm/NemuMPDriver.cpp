/* $Id: NemuMPDriver.cpp $ */

/** @file
 * Nemu XPDM Miniport driver interface functions
 */

/*
 * Copyright (C) 2011-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "NemuMPInternal.h"
#include <Nemu/Hardware/NemuVideoVBE.h>
#include <Nemu/NemuGuestLib.h>
#include <Nemu/NemuVideo.h>
#include "common/NemuMPHGSMI.h"
#include "common/NemuMPCommon.h"
#include "NemuDisplay.h"
#include <iprt/initterm.h>
#include <Nemu/version.h>

/* Resource list */
VIDEO_ACCESS_RANGE  NemuLegacyVGAResourceList[] =
{
    { 0x000003B0, 0x00000000, 0x0000000C, 1, 1, 1, 0 }, /* VGA regs (0x3B0-0x3BB) */
    { 0x000003C0, 0x00000000, 0x00000020, 1, 1, 1, 0 }, /* VGA regs (0x3C0-0x3DF) */
    { 0x000A0000, 0x00000000, 0x00020000, 0, 0, 1, 0 }, /* Frame buffer (0xA0000-0xBFFFF) */
};

/* Card info for property dialog */
static WCHAR NemuChipType[] = L"NEMU";
static WCHAR NemuDACType[] = L"Integrated RAMDAC";
static WCHAR NemuAdapterString[] = L"VirtualBox Video Adapter";
static WCHAR NemuBiosString[] = L"Version 0xB0C2 or later";

/* Checks if we have a device supported by our driver and initialize
 * our driver/card specific information.
 * In particular we obtain VM monitors configuration and configure related structures.
 */
static VP_STATUS
NemuDrvFindAdapter(IN PVOID HwDeviceExtension, IN PVOID HwContext, IN PWSTR ArgumentString,
                   IN OUT PVIDEO_PORT_CONFIG_INFO ConfigInfo, OUT PUCHAR Again)
{
    PNEMUMP_DEVEXT pExt = (PNEMUMP_DEVEXT) HwDeviceExtension;
    VP_STATUS rc;
    USHORT DispiId;
    ULONG cbVRAM = VBE_DISPI_TOTAL_VIDEO_MEMORY_BYTES;
    PHYSICAL_ADDRESS phVRAM = {0};
    ULONG ulApertureSize = 0;

    PAGED_CODE();
    LOGF_ENTER();

    /* Init video port api */
    NemuSetupVideoPortAPI(pExt, ConfigInfo);

    VideoPortWritePortUshort((PUSHORT)VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    VideoPortWritePortUshort((PUSHORT)VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID2);
    DispiId = VideoPortReadPortUshort((PUSHORT)VBE_DISPI_IOPORT_DATA);

    if (DispiId != VBE_DISPI_ID2)
    {
        WARN(("VBE card not found, returning ERROR_DEV_NOT_EXIST"));
        return ERROR_DEV_NOT_EXIST;
    }
    LOG(("found the VBE card"));

    /*
     * Query the adapter's memory size. It's a bit of a hack, we just read
     * an ULONG from the data port without setting an index before.
     */
    cbVRAM = VideoPortReadPortUlong((PULONG)VBE_DISPI_IOPORT_DATA);

    /* Write hw information to registry, so that it's visible in windows property dialog */
    rc = VideoPortSetRegistryParameters(pExt, L"HardwareInformation.ChipType",
                                        NemuChipType, sizeof(NemuChipType));
    NEMUMP_WARN_VPS(rc);
    rc = VideoPortSetRegistryParameters(pExt, L"HardwareInformation.DacType",
                                        NemuDACType, sizeof(NemuDACType));
    NEMUMP_WARN_VPS(rc);
    rc = VideoPortSetRegistryParameters(pExt, L"HardwareInformation.MemorySize",
                                        &cbVRAM, sizeof(ULONG));
    NEMUMP_WARN_VPS(rc);
    rc = VideoPortSetRegistryParameters(pExt, L"HardwareInformation.AdapterString",
                                        NemuAdapterString, sizeof(NemuAdapterString));
    NEMUMP_WARN_VPS(rc);
    rc = VideoPortSetRegistryParameters(pExt, L"HardwareInformation.BiosString",
                                        NemuBiosString, sizeof(NemuBiosString));
    NEMUMP_WARN_VPS(rc);

    /* Call VideoPortGetAccessRanges to ensure interrupt info in ConfigInfo gets set up
     * and to get LFB aperture data.
     */
    {
        VIDEO_ACCESS_RANGE tmpRanges[4];
        ULONG slot = 0;

        VideoPortZeroMemory(tmpRanges, sizeof(tmpRanges));

        if (NemuQueryWinVersion() == WINVERSION_NT4)
        {
            /* NT crashes if either of 'vendorId, 'deviceId' or 'slot' parameters is NULL,
             * and needs PCI ids for a successful VideoPortGetAccessRanges call.
             */
            ULONG vendorId = 0x80EE;
            ULONG deviceId = 0xBEEF;
            rc = VideoPortGetAccessRanges(pExt, 0, NULL, RT_ELEMENTS(tmpRanges), tmpRanges,
                                          &vendorId, &deviceId, &slot);
        }
        else
        {
            rc = VideoPortGetAccessRanges(pExt, 0, NULL, RT_ELEMENTS(tmpRanges), tmpRanges, NULL, NULL, &slot);
        }
        NEMUMP_WARN_VPS(rc);
        if (rc != NO_ERROR) {
            return rc;
        }

        /* The first range is the framebuffer. We require that information. */
        phVRAM = tmpRanges[0].RangeStart;
        ulApertureSize = tmpRanges[0].RangeLength;
    }

    /* Initialize NemuGuest library, which is used for requests which go through VMMDev. */
    rc = VbglInitClient();
    NEMUMP_WARN_VPS(rc);

    /* Preinitialize the primary extension. */
    pExt->pNext                   = NULL;
    pExt->pPrimary                = pExt;
    pExt->iDevice                 = 0;
    pExt->ulFrameBufferOffset     = 0;
    pExt->ulFrameBufferSize       = 0;
    pExt->u.primary.ulVbvaEnabled = 0;
    VideoPortZeroMemory(&pExt->areaDisplay, sizeof(HGSMIAREA));

    /* Guest supports only HGSMI, the old VBVA via VMMDev is not supported. Old
     * code will be ifdef'ed and later removed.
     * The host will however support both old and new interface to keep compatibility
     * with old guest additions.
     */
    NemuSetupDisplaysHGSMI(&pExt->u.primary.commonInfo, phVRAM, ulApertureSize, cbVRAM, 0);

    /* Check if the chip restricts horizontal resolution or not.
     * Must be done after NemuSetupDisplaysHGSMI, because it initializes the common structure.
     */
    VideoPortWritePortUshort((PUSHORT)VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    VideoPortWritePortUshort((PUSHORT)VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID_ANYX);
    DispiId = VideoPortReadPortUshort((PUSHORT)VBE_DISPI_IOPORT_DATA);

    if (DispiId == VBE_DISPI_ID_ANYX)
        NemuCommonFromDeviceExt(pExt)->fAnyX = TRUE;
    else
        NemuCommonFromDeviceExt(pExt)->fAnyX = FALSE;

    if (pExt->u.primary.commonInfo.bHGSMI)
    {
        LOGREL(("using HGSMI"));
        NemuCreateDisplays(pExt, ConfigInfo);
    }

    /** @todo pretend success to make the driver work. */
    rc = NO_ERROR;

    LOGF_LEAVE();
    NEMUMP_WARN_VPS(rc);
    return rc;
}

/* Initial device configuration. */
static BOOLEAN
NemuDrvInitialize(PVOID HwDeviceExtension)
{
    PNEMUMP_DEVEXT pExt = (PNEMUMP_DEVEXT) HwDeviceExtension;

    PAGED_CODE();
    LOGF_ENTER();

    /* Initialize the request pointer. */
    pExt->u.primary.pvReqFlush = NULL;

    NemuMPCmnInitCustomVideoModes(pExt);

    LOGF_LEAVE();
    return TRUE;
}

/* NemuDrvStartIO parameter check helper macros */
#define STARTIO_IN(_type, _name)                                        \
    _type *_name = (_type*) RequestPacket->InputBuffer;                 \
    if (RequestPacket->InputBufferLength < sizeof(_type))               \
    {                                                                   \
        WARN(("Input buffer too small %d/%d bytes",                     \
               RequestPacket->InputBufferLength, sizeof(_type)));       \
        pStatus->Status = ERROR_INSUFFICIENT_BUFFER;                    \
        break;                                                          \
    }

#define STARTIO_OUT(_type, _name)                                 \
    _type *_name = (_type*) RequestPacket->OutputBuffer;          \
    if (RequestPacket->OutputBufferLength < sizeof(_type))        \
    {                                                             \
        WARN(("Output buffer too small %d/%d bytes",              \
              RequestPacket->OutputBufferLength, sizeof(_type))); \
        pStatus->Status = ERROR_INSUFFICIENT_BUFFER;              \
        break;                                                    \
    }

/* Process Video Request Packet. */
static BOOLEAN
NemuDrvStartIO(PVOID HwDeviceExtension, PVIDEO_REQUEST_PACKET RequestPacket)
{
    PNEMUMP_DEVEXT pExt = (PNEMUMP_DEVEXT) HwDeviceExtension;
    PSTATUS_BLOCK  pStatus = RequestPacket->StatusBlock;
    BOOLEAN        bResult = FALSE;

    PAGED_CODE();

    LOGF(("IOCTL %#p, fn(%#x)", (void*)RequestPacket->IoControlCode, (RequestPacket->IoControlCode >> 2) & 0xFFF));

    pStatus->Status = NO_ERROR;

    switch (RequestPacket->IoControlCode)
    {
        /* ==================== System VRPs ==================== */

        /*Maps FrameBuffer and video RAM to a caller's virtual adress space.*/
        case IOCTL_VIDEO_MAP_VIDEO_MEMORY:
        {
            STARTIO_IN(VIDEO_MEMORY, pMemory);
            STARTIO_OUT(VIDEO_MEMORY_INFORMATION, pMemInfo);

            bResult = NemuMPMapVideoMemory(pExt, pMemory, pMemInfo, pStatus);
            break;
        }

        /*Unmaps previously mapped FrameBuffer and video RAM from caller's virtual adress space.*/
        case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY:
        {
            STARTIO_IN(VIDEO_MEMORY, pMemory);

            bResult = NemuMPUnmapVideoMemory(pExt, pMemory, pStatus);
            break;
        }

        /*Maps FrameBuffer as a linear frame buffer to a caller's virtual adress space. (obsolete)*/
        case IOCTL_VIDEO_SHARE_VIDEO_MEMORY:
        {
            STARTIO_IN(VIDEO_SHARE_MEMORY, pShareMemory);
            STARTIO_OUT(VIDEO_SHARE_MEMORY_INFORMATION, pShareMemInfo);

            bResult = NemuMPShareVideoMemory(pExt, pShareMemory, pShareMemInfo, pStatus);
            break;
        }

        /*Unmaps framebuffer previously mapped with IOCTL_VIDEO_SHARE_VIDEO_MEMORY*/
        case IOCTL_VIDEO_UNSHARE_VIDEO_MEMORY:
        {
            STARTIO_IN(VIDEO_SHARE_MEMORY, pShareMemory);

            bResult = NemuMPUnshareVideoMemory(pExt, pShareMemory, pStatus);
            break;
        }

        /*Reset device to a state it comes at system boot time.*/
        case IOCTL_VIDEO_RESET_DEVICE:
        {
            bResult = NemuMPResetDevice(pExt, pStatus);
            break;
        }

        /*Set adapter video mode.*/
        case IOCTL_VIDEO_SET_CURRENT_MODE:
        {
            STARTIO_IN(VIDEO_MODE, pMode);

            bResult = NemuMPSetCurrentMode(pExt, pMode, pStatus);
            break;
        }

        /*Returns information about current video mode.*/
        case IOCTL_VIDEO_QUERY_CURRENT_MODE:
        {
            STARTIO_OUT(VIDEO_MODE_INFORMATION, pModeInfo);

            bResult = NemuMPQueryCurrentMode(pExt, pModeInfo, pStatus);
            break;
        }

        /* Returns count of supported video modes and structure size in bytes,
         * used to allocate buffer for the following IOCTL_VIDEO_QUERY_AVAIL_MODES call.
         */
        case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES:
        {
            STARTIO_OUT(VIDEO_NUM_MODES, pNumModes);

            bResult = NemuMPQueryNumAvailModes(pExt, pNumModes, pStatus);
            break;
        }

        /* Returns information about supported video modes. */
        case IOCTL_VIDEO_QUERY_AVAIL_MODES:
        {
            PVIDEO_MODE_INFORMATION pModes = (PVIDEO_MODE_INFORMATION) RequestPacket->OutputBuffer;

            if (RequestPacket->OutputBufferLength < NemuMPXpdmGetVideoModesCount(pExt)*sizeof(VIDEO_MODE_INFORMATION))
            {
                pStatus->Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            bResult = NemuMPQueryAvailModes(pExt, pModes, pStatus);
            break;
        }

        /* Sets adapter's color registers, have to be implemented if we support palette based modes. */
        case IOCTL_VIDEO_SET_COLOR_REGISTERS:
        {
            STARTIO_IN(VIDEO_CLUT, pClut);

            if (RequestPacket->InputBufferLength < (sizeof(VIDEO_CLUT) + pClut->NumEntries * sizeof(ULONG)))
            {
                pStatus->Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            bResult = NemuMPSetColorRegisters(pExt, pClut, pStatus);
            break;
        }

        /* Sets pointer attributes. */
        case IOCTL_VIDEO_SET_POINTER_ATTR:
        {
            STARTIO_IN(VIDEO_POINTER_ATTRIBUTES, pPointerAttrs);

            bResult = NemuMPSetPointerAttr(pExt, pPointerAttrs, RequestPacket->InputBufferLength, pStatus);
            break;
        }

        /* Makes pointer visible. */
        case IOCTL_VIDEO_ENABLE_POINTER:
        {
            bResult = NemuMPEnablePointer(pExt, TRUE, pStatus);
            break;
        }

        /* Hides pointer. */
        case IOCTL_VIDEO_DISABLE_POINTER:
        {
            bResult = NemuMPEnablePointer(pExt, FALSE, pStatus);
            break;
        }

        /* Sets pointer position, is called after IOCTL_VIDEO_ENABLE_POINTER. */
        case IOCTL_VIDEO_SET_POINTER_POSITION:
        {
            STARTIO_IN(VIDEO_POINTER_POSITION, pPos);

            /** @todo set pointer position*/
            bResult = NemuMPEnablePointer(pExt, TRUE, pStatus);
            break;
        }

        /* Query pointer position. */
        case IOCTL_VIDEO_QUERY_POINTER_POSITION:
        {
            STARTIO_OUT(VIDEO_POINTER_POSITION, pPos);

            bResult = NemuMPQueryPointerPosition(pExt, pPos, pStatus);
            break;
        }

        /* Query supported hardware pointer feaures. */
        case IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES:
        {
            STARTIO_OUT(VIDEO_POINTER_CAPABILITIES, pCaps);

            bResult = NemuMPQueryPointerCapabilities(pExt, pCaps, pStatus);
            break;
        }

        /* Query pointer attributes. (optional) */
        case IOCTL_VIDEO_QUERY_POINTER_ATTR:
        {
            STARTIO_OUT(VIDEO_POINTER_ATTRIBUTES, pPointerAttrs);

            /* Not Implemented */
            pStatus->Status = ERROR_INVALID_FUNCTION;

            bResult = FALSE;
            break;
        }

        /* Called when a secondary adapter is about to be enabled/disabled. */
        case IOCTL_VIDEO_SWITCH_DUALVIEW:
        {
            STARTIO_IN(ULONG, pAttach);

            LOGF(("IOCTL_VIDEO_SWITCH_DUALVIEW: [%d] attach = %d", pExt->iDevice, *pAttach));

            if (pExt->iDevice>0)
            {
                pExt->u.secondary.bEnabled = (BOOLEAN)(*pAttach);

                /* Inform the host.
                 * Currently only about secondary devices, because the driver does not support
                 * disconnecting the primary display (it does not allow to change the primary display).
                 */
                if (!pExt->u.secondary.bEnabled)
                {
                    PNEMUMP_COMMON pCommon = NemuCommonFromDeviceExt(pExt);
                    if (pCommon->bHGSMI)
                    {
                        NemuHGSMIProcessDisplayInfo(&pCommon->guestCtx, pExt->iDevice,
                                                    /* cOriginX = */ 0, /* cOriginY = */ 0,
                                                    /* offStart = */ 0, /* cbPitch = */ 0,
                                                    /* cWidth = */ 0, /* cHeight = */ 0, /* cBPP = */ 0,
                                                    VBVA_SCREEN_F_ACTIVE | VBVA_SCREEN_F_DISABLED);
                    }
                }
            }

            bResult = TRUE;
            break;
        }

        /* Called to get child device status */
        case IOCTL_VIDEO_GET_CHILD_STATE:
        {
            STARTIO_IN(ULONG, pChildIndex);
            STARTIO_OUT(ULONG, pChildState);

            LOGF(("IOCTL_VIDEO_GET_CHILD_STATE: [%d] idx = %d", pExt->iDevice, *pChildIndex));

            if (*pChildIndex>0 && *pChildIndex<=(ULONG)NemuCommonFromDeviceExt(pExt)->cDisplays)
            {
                *pChildState = VIDEO_CHILD_ACTIVE;
                pStatus->Information = sizeof(ULONG);
                bResult = TRUE;
            }
            else
            {
                pStatus->Status = ERROR_INVALID_PARAMETER;
                bResult = FALSE;
            }

            break;
        }

        /* ==================== VirtualBox specific VRPs ==================== */

        /* Called by the display driver when it is ready to switch to VBVA operation mode. */
        case IOCTL_VIDEO_VBVA_ENABLE:
        {
            STARTIO_IN(ULONG, pEnable);
            STARTIO_OUT(VBVAENABLERESULT, pResult);

            bResult = NemuMPVBVAEnable(pExt, (BOOLEAN)*pEnable, pResult, pStatus);
            break;
        }

        /* Called by the display driver when it recieves visible regions information. */
        case IOCTL_VIDEO_NEMU_SETVISIBLEREGION:
        {
            STARTIO_IN(RTRECT, pRects);

            uint32_t cRects = RequestPacket->InputBufferLength/sizeof(RTRECT);
            /*Sanity check*/
            if (   cRects > _1M
                || RequestPacket->InputBufferLength != cRects * sizeof(RTRECT))
            {
                pStatus->Status = ERROR_INSUFFICIENT_BUFFER;
                break;
            }

            bResult = NemuMPSetVisibleRegion(cRects, pRects, pStatus);
            break;
        }

        /* Returns video port api function pointers. */
        case IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS:
        {
            STARTIO_OUT(HGSMIQUERYCPORTPROCS, pProcs);

            bResult = NemuMPHGSMIQueryPortProcs(pExt, pProcs, pStatus);
            break;
        }

        /* Returns HGSMI related callbacks. */
        case IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS:
        {
            STARTIO_OUT(HGSMIQUERYCALLBACKS, pCallbacks);

            bResult = NemuMPHGSMIQueryCallbacks(pExt, pCallbacks, pStatus);
            break;
        }

        /* Returns hgsmi info for this adapter. */
        case IOCTL_VIDEO_QUERY_HGSMI_INFO:
        {
            STARTIO_OUT(QUERYHGSMIRESULT, pResult);

            bResult = NemuMPQueryHgsmiInfo(pExt, pResult, pStatus);
            break;
        }

        /* Enables HGSMI miniport channel. */
        case IOCTL_VIDEO_HGSMI_HANDLER_ENABLE:
        {
            STARTIO_IN(HGSMIHANDLERENABLE, pChannel);

            bResult = NemuMPHgsmiHandlerEnable(pExt, pChannel, pStatus);
            break;
        }

        case IOCTL_VIDEO_HGSMI_HANDLER_DISABLE:
        {
            /** @todo not implemented */
            break;
        }

#ifdef NEMU_WITH_VIDEOHWACCEL
        /* Returns framebuffer offset. */
        case IOCTL_VIDEO_VHWA_QUERY_INFO:
        {
            STARTIO_OUT(VHWAQUERYINFO, pInfo);

            bResult = NemuMPVhwaQueryInfo(pExt, pInfo, pStatus);
            break;
        }
#endif

        case IOCTL_VIDEO_NEMU_ISANYX:
        {
            STARTIO_OUT(uint32_t, pu32AnyX);
            *pu32AnyX = NemuCommonFromDeviceExt(pExt)->fAnyX;
            pStatus->Information = sizeof (uint32_t);
            bResult = TRUE;
            break;
        }

        case IOCTL_VIDEO_QUERY_NEMUVIDEO_INFO:
        {
            STARTIO_IN(ULONG, pulInfoLevel);
            if (*pulInfoLevel == NEMUVIDEO_INFO_LEVEL_REGISTRY_FLAGS)
            {
                STARTIO_OUT(ULONG, pulFlags);
                bResult = NemuMPQueryRegistryFlags(pExt, pulFlags, pStatus);
            }
            else
            {
                pStatus->Status = ERROR_INVALID_PARAMETER;
                bResult = FALSE;
            }

            break;
        }

        default:
        {
            WARN(("unsupported IOCTL %p, fn(%#x)",
                  (void*)RequestPacket->IoControlCode, (RequestPacket->IoControlCode >> 2) & 0xFFF));
            RequestPacket->StatusBlock->Status = ERROR_INVALID_FUNCTION;
        }
    }

    if (!bResult)
    {
        pStatus->Information = NULL;
    }

    NEMUMP_WARN_VPS(pStatus->Status);
    LOGF_LEAVE();
    return TRUE;
}

/* Called to set out hardware into desired power state, not supported at the moment.
 * Required to return NO_ERROR always.
 */
static VP_STATUS
NemuDrvSetPowerState(PVOID HwDeviceExtension, ULONG HwId, PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    PAGED_CODE();
    LOGF_ENTER();

    /*Not implemented*/

    LOGF_LEAVE();
    return NO_ERROR;
}

/* Called to check if our hardware supports given power state. */
static VP_STATUS
NemuDrvGetPowerState(PVOID HwDeviceExtension, ULONG HwId, PVIDEO_POWER_MANAGEMENT VideoPowerControl)
{
    PAGED_CODE();
    LOGF_ENTER();

    /*Not implemented*/

    LOGF_LEAVE();
    return NO_ERROR;
}

/* Called to enumerate child devices of our adapter, attached monitor(s) in our case */
static VP_STATUS
NemuDrvGetVideoChildDescriptor(PVOID HwDeviceExtension, PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
                               PVIDEO_CHILD_TYPE VideoChildType, PUCHAR pChildDescriptor, PULONG pUId,
                               PULONG pUnused)
{
    PNEMUMP_DEVEXT pExt = (PNEMUMP_DEVEXT) HwDeviceExtension;

    PAGED_CODE();
    LOGF_ENTER();

    if (ChildEnumInfo->ChildIndex>0)
    {
        if ((int)ChildEnumInfo->ChildIndex <= NemuCommonFromDeviceExt(pExt)->cDisplays)
        {
            *VideoChildType = Monitor;
            *pUId = ChildEnumInfo->ChildIndex;

            LOGF_LEAVE();
            return VIDEO_ENUM_MORE_DEVICES;
        }
    }
    LOGF_LEAVE();
    return ERROR_NO_MORE_DEVICES;
}

/* Called to reset adapter to a given character mode. */
static BOOLEAN
NemuDrvResetHW(PVOID HwDeviceExtension, ULONG Columns, ULONG Rows)
{
    PNEMUMP_DEVEXT pExt = (PNEMUMP_DEVEXT) HwDeviceExtension;

    LOGF_ENTER();

    if (pExt->iDevice==0) /* Primary device */
    {
        VideoPortWritePortUshort((PUSHORT)VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
        VideoPortWritePortUshort((PUSHORT)VBE_DISPI_IOPORT_DATA, VBE_DISPI_DISABLED);

#if 0
        /* ResetHW is not the place to do such cleanup. See MSDN. */
        if (pExt->u.primary.pvReqFlush != NULL)
        {
            VbglGRFree((VMMDevRequestHeader *)pExt->u.primary.pvReqFlush);
            pExt->u.primary.pvReqFlush = NULL;
        }

        VbglTerminate();

        NemuFreeDisplaysHGSMI(NemuCommonFromDeviceExt(pExt));
#endif
    }
    else
    {
        LOG(("ignoring non primary device %d", pExt->iDevice));
    }

    LOGF_LEAVE();
    /* Tell the system to use VGA BIOS to set the text video mode. */
    return FALSE;
}

#ifdef NEMU_WITH_VIDEOHWACCEL
static VOID NemuMPHGSMIDpc(IN PVOID  HwDeviceExtension, IN PVOID  Context)
{
    PNEMUMP_DEVEXT pExt = (PNEMUMP_DEVEXT) HwDeviceExtension;

    NemuHGSMIProcessHostQueue(&NemuCommonFromDeviceExt(pExt)->hostCtx);
}

static BOOLEAN
NemuDrvInterrupt(PVOID  HwDeviceExtension)
{
    PNEMUMP_DEVEXT pExt = (PNEMUMP_DEVEXT) HwDeviceExtension;

    //LOGF_ENTER();

    /* Check if it could be our IRQ*/
    if (NemuCommonFromDeviceExt(pExt)->hostCtx.pfHostFlags)
    {
        uint32_t flags = NemuCommonFromDeviceExt(pExt)->hostCtx.pfHostFlags->u32HostFlags;
        if ((flags & HGSMIHOSTFLAGS_IRQ) != 0)
        {
            /* queue a DPC*/
            BOOLEAN bResult = pExt->pPrimary->u.primary.VideoPortProcs.pfnQueueDpc(pExt->pPrimary, NemuMPHGSMIDpc, NULL);

            if (!bResult)
            {
                LOG(("VideoPortQueueDpc failed!"));
            }

            /* clear the IRQ */
            NemuHGSMIClearIrq(&NemuCommonFromDeviceExt(pExt)->hostCtx);
            //LOGF_LEAVE();
            return TRUE;
        }
    }

    //LOGF_LEAVE();
    return FALSE;
}
#endif

/* Video Miniport Driver entry point */
ULONG DriverEntry(IN PVOID Context1, IN PVOID Context2)
{
    PAGED_CODE();

    int irc = RTR0Init(0);
    if (RT_FAILURE(irc))
    {
        LogRel(("NemuMP::failed to init IPRT (rc=%#x)", irc));
        return ERROR_INVALID_FUNCTION;
    }

    LOGF_ENTER();

    LOGREL(("Nemu XPDM Driver for Windows version %d.%d.%dr%d, %d bit; Built %s %s",
            NEMU_VERSION_MAJOR, NEMU_VERSION_MINOR, NEMU_VERSION_BUILD, NEMU_SVN_REV,
            (sizeof (void*) << 3), __DATE__, __TIME__));

    VIDEO_HW_INITIALIZATION_DATA vhwData;

    /*Zero the structure*/
    VideoPortZeroMemory(&vhwData, sizeof(vhwData));

    /*Required driver callbacks*/
    vhwData.HwFindAdapter    = NemuDrvFindAdapter;
    vhwData.HwInitialize     = NemuDrvInitialize;
    vhwData.HwStartIO        = NemuDrvStartIO;
    vhwData.HwSetPowerState  = NemuDrvSetPowerState;
    vhwData.HwGetPowerState  = NemuDrvGetPowerState;
    vhwData.HwGetVideoChildDescriptor = NemuDrvGetVideoChildDescriptor;

    /*Optional callbacks*/
    vhwData.HwResetHw     = NemuDrvResetHW;
#ifdef NEMU_WITH_VIDEOHWACCEL
    vhwData.HwInterrupt   = NemuDrvInterrupt;
#endif

    /*Our private storage space*/
    vhwData.HwDeviceExtensionSize = sizeof(NEMUMP_DEVEXT);

    /*Claim legacy VGA resource ranges*/
    vhwData.HwLegacyResourceList  = NemuLegacyVGAResourceList;
    vhwData.HwLegacyResourceCount = RT_ELEMENTS(NemuLegacyVGAResourceList);

    /*Size of this structure changes between windows/ddk versions,
     *so we query current version and report the expected size
     *to allow our driver to be loaded.
     */
    switch (NemuQueryWinVersion())
    {
        case WINVERSION_NT4:
            LOG(("WINVERSION_NT4"));
            vhwData.HwInitDataSize = SIZE_OF_NT4_VIDEO_HW_INITIALIZATION_DATA;
            break;
        case WINVERSION_2K:
            LOG(("WINVERSION_2K"));
            vhwData.HwInitDataSize = SIZE_OF_W2K_VIDEO_HW_INITIALIZATION_DATA;
            break;
        default:
            vhwData.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
            break;
    }

    /*Even though msdn claims that this field is ignored and should remain zero-initialized,
      windows NT4 SP0 dies without the following line.
     */
    vhwData.AdapterInterfaceType = PCIBus;

    /*Allocate system resources*/
    ULONG rc = VideoPortInitialize(Context1, Context2, &vhwData, NULL);
    if (rc != NO_ERROR)
        LOG(("VideoPortInitialize failed with %#x", rc));

    LOGF_LEAVE();
    return rc;
}
