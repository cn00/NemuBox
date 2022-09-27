/* $Id: NemuNetAdp-win.cpp $ */
/** @file
 * NemuNetAdp-win.cpp - NDIS6 Host-only Networking Driver, Windows-specific code.
 */
/*
 * Copyright (C) 2014-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define LOG_GROUP LOG_GROUP_NET_ADP_DRV

#include <Nemu/log.h>
#include <Nemu/version.h>
#include <Nemu/err.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>

RT_C_DECLS_BEGIN
#include <ndis.h>
RT_C_DECLS_END

#include "NemuNetAdp-win.h"
#include "Nemu/NemuNetCmn-win.h"

/* Forward declarations */
MINIPORT_INITIALIZE                nemuNetAdpWinInitializeEx;
MINIPORT_HALT                      nemuNetAdpWinHaltEx;
MINIPORT_UNLOAD                    nemuNetAdpWinUnload;
MINIPORT_PAUSE                     nemuNetAdpWinPause;
MINIPORT_RESTART                   nemuNetAdpWinRestart;
MINIPORT_OID_REQUEST               nemuNetAdpWinOidRequest;
MINIPORT_SEND_NET_BUFFER_LISTS     nemuNetAdpWinSendNetBufferLists;
MINIPORT_RETURN_NET_BUFFER_LISTS   nemuNetAdpWinReturnNetBufferLists;
MINIPORT_CANCEL_SEND               nemuNetAdpWinCancelSend;
MINIPORT_CHECK_FOR_HANG            nemuNetAdpWinCheckForHangEx;
MINIPORT_RESET                     nemuNetAdpWinResetEx;
MINIPORT_DEVICE_PNP_EVENT_NOTIFY   nemuNetAdpWinDevicePnPEventNotify;
MINIPORT_SHUTDOWN                  nemuNetAdpWinShutdownEx;
MINIPORT_CANCEL_OID_REQUEST        nemuNetAdpWinCancelOidRequest;


typedef struct _NEMUNETADPGLOBALS
{
    /** ndis device */
    NDIS_HANDLE hDevice;
    /** device object */
    PDEVICE_OBJECT pDevObj;
    /** our miniport driver handle */
    NDIS_HANDLE hMiniportDriver;
    /** power management capabilities, shared by all instances, do not change after init */
    NDIS_PNP_CAPABILITIES PMCaps;
} NEMUNETADPGLOBALS, *PNEMUNETADPGLOBALS;

/* win-specific global data */
NEMUNETADPGLOBALS g_NemuNetAdpGlobals;


typedef struct _NEMUNETADP_ADAPTER {
    NDIS_HANDLE hAdapter;
    PNEMUNETADPGLOBALS pGlobals;
    RTMAC MacAddr;
} NEMUNETADP_ADAPTER;
typedef NEMUNETADP_ADAPTER *PNEMUNETADP_ADAPTER;


static NTSTATUS nemuNetAdpWinDevDispatch(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp)
{
    PIO_STACK_LOCATION pIrpSl = IoGetCurrentIrpStackLocation(pIrp);;
    NTSTATUS Status = STATUS_SUCCESS;

    switch (pIrpSl->MajorFunction)
    {
        case IRP_MJ_DEVICE_CONTROL:
            Status = STATUS_NOT_SUPPORTED; // TODO: add/remove ioctls
            break;
        case IRP_MJ_CREATE:
        case IRP_MJ_CLEANUP:
        case IRP_MJ_CLOSE:
            break;
        default:
            Assert(0);
            break;
    }

    pIrp->IoStatus.Status = Status;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return Status;
}

static NDIS_STATUS nemuNetAdpWinDevCreate(PNEMUNETADPGLOBALS pGlobals)
{
    NDIS_STRING DevName, LinkName;
    PDRIVER_DISPATCH aMajorFunctions[IRP_MJ_MAXIMUM_FUNCTION+1];
    NdisInitUnicodeString(&DevName, NEMUNETADP_NAME_DEVICE);
    NdisInitUnicodeString(&LinkName, NEMUNETADP_NAME_LINK);

    Assert(!pGlobals->hDevice);
    Assert(!pGlobals->pDevObj);
    NdisZeroMemory(aMajorFunctions, sizeof (aMajorFunctions));
    aMajorFunctions[IRP_MJ_CREATE] = nemuNetAdpWinDevDispatch;
    aMajorFunctions[IRP_MJ_CLEANUP] = nemuNetAdpWinDevDispatch;
    aMajorFunctions[IRP_MJ_CLOSE] = nemuNetAdpWinDevDispatch;
    aMajorFunctions[IRP_MJ_DEVICE_CONTROL] = nemuNetAdpWinDevDispatch;

    NDIS_DEVICE_OBJECT_ATTRIBUTES DeviceAttributes;
    NdisZeroMemory(&DeviceAttributes, sizeof(DeviceAttributes));
    DeviceAttributes.Header.Type = NDIS_OBJECT_TYPE_DEVICE_OBJECT_ATTRIBUTES;
    DeviceAttributes.Header.Revision = NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;
    DeviceAttributes.Header.Size = sizeof(DeviceAttributes);
    DeviceAttributes.DeviceName = &DevName;
    DeviceAttributes.SymbolicName = &LinkName;
    DeviceAttributes.MajorFunctions = aMajorFunctions;

    NDIS_STATUS Status = NdisRegisterDeviceEx(pGlobals->hMiniportDriver,
                                              &DeviceAttributes,
                                              &pGlobals->pDevObj,
                                              &pGlobals->hDevice);
    Log(("nemuNetAdpWinDevCreate: NdisRegisterDeviceEx returned 0x%x\n", Status));
    Assert(Status == NDIS_STATUS_SUCCESS);
    return Status;
}

static void nemuNetAdpWinDevDestroy(PNEMUNETADPGLOBALS pGlobals)
{
    Assert(pGlobals->hDevice);
    Assert(pGlobals->pDevObj);
    NdisDeregisterDeviceEx(pGlobals->hDevice);
    pGlobals->hDevice = NULL;
    pGlobals->pDevObj = NULL;
}





NDIS_OID g_SupportedOids[] =
{
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_INTERRUPT_MODERATION,
    OID_GEN_LINK_PARAMETERS,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_RCV_OK,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_STATISTICS,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_VENDOR_ID,
    OID_GEN_XMIT_OK,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_PNP_CAPABILITIES,
    OID_PNP_QUERY_POWER,
    OID_PNP_SET_POWER
};

DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinAllocAdapter(NDIS_HANDLE hAdapter, PNEMUNETADP_ADAPTER *ppAdapter, ULONG64 NetLuid)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    PNEMUNETADP_ADAPTER pAdapter = NULL;

    LogFlow(("==>nemuNetAdpWinAllocAdapter: adapter handle=%p\n", hAdapter));

    *ppAdapter = NULL;

    pAdapter = (PNEMUNETADP_ADAPTER)NdisAllocateMemoryWithTagPriority(g_NemuNetAdpGlobals.hMiniportDriver,
                                                                         sizeof(NEMUNETADP_ADAPTER),
                                                                         NEMUNETADPWIN_TAG,
                                                                         NormalPoolPriority);
    if (!pAdapter)
    {
        Status = NDIS_STATUS_RESOURCES;
        Log(("nemuNetAdpWinAllocAdapter: Out of memory while allocating adapter context (size=%d)\n", sizeof(NEMUNETADP_ADAPTER)));
    }
    else
    {
        NdisZeroMemory(pAdapter, sizeof(NEMUNETADP_ADAPTER));
        pAdapter->hAdapter = hAdapter;
        pAdapter->pGlobals = &g_NemuNetAdpGlobals;
        // TODO: Use netadp structure instead!
    /* Use a locally administered version of the OUI we use for the guest NICs. */
    pAdapter->MacAddr.au8[0] = 0x08 | 2;
    pAdapter->MacAddr.au8[1] = 0x00;
    pAdapter->MacAddr.au8[2] = 0x27;

    pAdapter->MacAddr.au8[3] = (NetLuid >> 16) & 0xFF;
    pAdapter->MacAddr.au8[4] = (NetLuid >> 8) & 0xFF;
    pAdapter->MacAddr.au8[5] = NetLuid & 0xFF;

        //TODO: Statistics?

        *ppAdapter = pAdapter;
    }
    LogFlow(("<==nemuNetAdpWinAllocAdapter: status=0x%x\n", Status));
    return Status;
}

DECLHIDDEN(void) nemuNetAdpWinFreeAdapter(PNEMUNETADP_ADAPTER pAdapter)
{
    NdisFreeMemory(pAdapter, 0, 0);
}

DECLINLINE(NDIS_MEDIA_CONNECT_STATE) nemuNetAdpWinGetConnectState(PNEMUNETADP_ADAPTER pAdapter)
{
    return MediaConnectStateConnected;
}


DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinInitializeEx(IN NDIS_HANDLE NdisMiniportHandle,
                                                  IN NDIS_HANDLE MiniportDriverContext,
                                                  IN PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PNEMUNETADP_ADAPTER pAdapter = NULL;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    LogFlow(("==>nemuNetAdpWinInitializeEx: miniport=0x%x\n", NdisMiniportHandle));

    do
    {
        NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RAttrs = {0};
        NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GAttrs = {0};

        Status = nemuNetAdpWinAllocAdapter(NdisMiniportHandle, &pAdapter, MiniportInitParameters->NetLuid.Value);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Log(("nemuNetAdpWinInitializeEx: Failed to allocate the adapter context with 0x%x\n", Status));
            break;
        }

        RAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
        RAttrs.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
        RAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
        RAttrs.MiniportAdapterContext = pAdapter;
        RAttrs.AttributeFlags = NEMUNETADPWIN_ATTR_FLAGS; // NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM
        RAttrs.CheckForHangTimeInSeconds = NEMUNETADPWIN_HANG_CHECK_TIME;
        RAttrs.InterfaceType = NdisInterfaceInternal;

        Status = NdisMSetMiniportAttributes(NdisMiniportHandle,
                                            (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RAttrs);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Log(("nemuNetAdpWinInitializeEx: NdisMSetMiniportAttributes(registration) failed with 0x%x\n", Status));
            break;
        }

        // TODO: Registry?

        // TODO: WDM stack?

        // TODO: DPC?

        GAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
        GAttrs.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
        GAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;

        GAttrs.MediaType = NdisMedium802_3;
        GAttrs.PhysicalMediumType = NdisPhysicalMediumUnspecified;
        GAttrs.MtuSize = 1500; //TODO
        GAttrs.MaxXmitLinkSpeed = 1000000000ULL;
        GAttrs.XmitLinkSpeed = 1000000000ULL;
        GAttrs.MaxRcvLinkSpeed = 1000000000ULL;
        GAttrs.RcvLinkSpeed = 1000000000ULL;
        GAttrs.MediaConnectState = nemuNetAdpWinGetConnectState(pAdapter);
        GAttrs.MediaDuplexState = MediaDuplexStateFull;
        GAttrs.LookaheadSize = 1500; //TODO
        GAttrs.MacOptions = NEMUNETADP_MAC_OPTIONS;
        GAttrs.SupportedPacketFilters = NEMUNETADP_SUPPORTED_FILTERS;
        GAttrs.MaxMulticastListSize = 32; //TODO

        GAttrs.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
        Assert(GAttrs.MacAddressLength == sizeof(pAdapter->MacAddr));
        memcpy(GAttrs.PermanentMacAddress, pAdapter->MacAddr.au8, GAttrs.MacAddressLength);
        memcpy(GAttrs.CurrentMacAddress, pAdapter->MacAddr.au8, GAttrs.MacAddressLength);

        GAttrs.RecvScaleCapabilities = NULL;
        GAttrs.AccessType = NET_IF_ACCESS_BROADCAST;
        GAttrs.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
        GAttrs.ConnectionType = NET_IF_CONNECTION_DEDICATED;
        GAttrs.IfType = IF_TYPE_ETHERNET_CSMACD;
        GAttrs.IfConnectorPresent = false;
        GAttrs.SupportedStatistics = NEMUNETADPWIN_SUPPORTED_STATISTICS;
        GAttrs.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
        GAttrs.DataBackFillSize = 0;
        GAttrs.ContextBackFillSize = 0;
        GAttrs.SupportedOidList = g_SupportedOids;
        GAttrs.SupportedOidListLength = sizeof(g_SupportedOids);
        GAttrs.AutoNegotiationFlags = NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
        GAttrs.PowerManagementCapabilities = &g_NemuNetAdpGlobals.PMCaps;

        Status = NdisMSetMiniportAttributes(NdisMiniportHandle,
                                            (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GAttrs);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Log(("nemuNetAdpWinInitializeEx: NdisMSetMiniportAttributes(general) failed with 0x%x\n", Status));
            break;
        }
    } while (false);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        if (pAdapter)
            nemuNetAdpWinFreeAdapter(pAdapter);
    }

    LogFlow(("<==nemuNetAdpWinInitializeEx: status=0x%x\n", Status));
    return Status;
}

DECLHIDDEN(VOID) nemuNetAdpWinHaltEx(IN NDIS_HANDLE MiniportAdapterContext,
                                     IN NDIS_HALT_ACTION HaltAction)
{
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinHaltEx\n"));
    // TODO: Stop something?
    if (pAdapter)
        nemuNetAdpWinFreeAdapter(pAdapter);
    LogFlow(("<==nemuNetAdpWinHaltEx\n"));
}

DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinPause(IN NDIS_HANDLE MiniportAdapterContext,
                                           IN PNDIS_MINIPORT_PAUSE_PARAMETERS MiniportPauseParameters)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    LogFlow(("==>nemuNetAdpWinPause\n"));
    LogFlow(("<==nemuNetAdpWinPause: status=0x%x\n", Status));
    return Status;
}

DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinRestart(IN NDIS_HANDLE MiniportAdapterContext,
                                             IN PNDIS_MINIPORT_RESTART_PARAMETERS MiniportRestartParameters)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    LogFlow(("==>nemuNetAdpWinRestart\n"));
    LogFlow(("<==nemuNetAdpWinRestart: status=0x%x\n", Status));
    return Status;
}

DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinOidRqQuery(PNEMUNETADP_ADAPTER pAdapter,
                                                PNDIS_OID_REQUEST pRequest)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    struct _NDIS_OID_REQUEST::_REQUEST_DATA::_QUERY *pQuery = &pRequest->DATA.QUERY_INFORMATION;

    LogFlow(("==>nemuNetAdpWinOidRqQuery\n"));

    uint64_t u64Tmp = 0;
    ULONG ulTmp = 0;
    PVOID pInfo = &ulTmp;
    ULONG cbInfo = sizeof(ulTmp);

    switch (pQuery->Oid)
    {
        case OID_GEN_INTERRUPT_MODERATION:
        {
            PNDIS_INTERRUPT_MODERATION_PARAMETERS pParams =
                (PNDIS_INTERRUPT_MODERATION_PARAMETERS)pQuery->InformationBuffer;
            cbInfo = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            if (cbInfo > pQuery->InformationBufferLength)
                break;
            pParams->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pParams->Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            pParams->Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            pParams->Flags = 0;
            pParams->InterruptModeration = NdisInterruptModerationNotSupported;
            pInfo = NULL; /* Do not copy */
            break;
        }
        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
            ulTmp = NEMUNETADP_MAX_FRAME_SIZE;
            break;
        case OID_GEN_RCV_OK:
        case OID_GEN_XMIT_OK:
            u64Tmp = 0;
            pInfo = &u64Tmp;
            cbInfo = sizeof(u64Tmp);
            break;
        case OID_GEN_RECEIVE_BUFFER_SPACE:
        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            // TODO: Make configurable
            ulTmp = NEMUNETADP_MAX_FRAME_SIZE * 40;
            break;
        case OID_GEN_STATISTICS:
        {
            PNDIS_STATISTICS_INFO pStats =
                (PNDIS_STATISTICS_INFO)pQuery->InformationBuffer;
            cbInfo = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
            if (cbInfo > pQuery->InformationBufferLength)
                break;
            pInfo = NULL; /* Do not copy */
            memset(pStats, 0, cbInfo);
            pStats->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            pStats->Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            pStats->Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
            // TODO: We need some stats, don't we?
            break;
        }
        case OID_GEN_VENDOR_DESCRIPTION:
            pInfo = NEMUNETADP_VENDOR_NAME;
            cbInfo = sizeof(NEMUNETADP_VENDOR_NAME);
            break;
        case OID_GEN_VENDOR_DRIVER_VERSION:
            ulTmp = (NEMUNETADP_VERSION_NDIS_MAJOR << 16) | NEMUNETADP_VERSION_NDIS_MINOR;
            break;
        case OID_GEN_VENDOR_ID:
            ulTmp = NEMUNETADP_VENDOR_ID;
            break;
        case OID_802_3_PERMANENT_ADDRESS:
        case OID_802_3_CURRENT_ADDRESS:
            pInfo = &pAdapter->MacAddr;
            cbInfo = sizeof(pAdapter->MacAddr);
            break;
            //case OID_802_3_MULTICAST_LIST:
        case OID_802_3_MAXIMUM_LIST_SIZE:
            ulTmp = NEMUNETADP_MCAST_LIST_SIZE;
            break;
        case OID_PNP_CAPABILITIES:
            pInfo = &pAdapter->pGlobals->PMCaps;
            cbInfo = sizeof(pAdapter->pGlobals->PMCaps);
            break;
        case OID_PNP_QUERY_POWER:
            pInfo = NULL; /* Do not copy */
            cbInfo = 0;
            break;
        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    if (Status == NDIS_STATUS_SUCCESS)
    {
        if (cbInfo > pQuery->InformationBufferLength)
        {
            pQuery->BytesNeeded = cbInfo;
            Status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        else
        {
            if (pInfo)
                NdisMoveMemory(pQuery->InformationBuffer, pInfo, cbInfo);
            pQuery->BytesWritten = cbInfo;
        }
    }

    LogFlow(("<==nemuNetAdpWinOidRqQuery: status=0x%x\n", Status));
    return Status;
}

DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinOidRqSet(PNEMUNETADP_ADAPTER pAdapter,
                                              PNDIS_OID_REQUEST pRequest)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    struct _NDIS_OID_REQUEST::_REQUEST_DATA::_SET *pSet = &pRequest->DATA.SET_INFORMATION;

    LogFlow(("==>nemuNetAdpWinOidRqSet\n"));

    switch (pSet->Oid)
    {
        case OID_GEN_CURRENT_LOOKAHEAD:
            if (pSet->InformationBufferLength != sizeof(ULONG))
            {
                pSet->BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }
            // TODO: For the time being we simply ignore lookahead settings.
            pSet->BytesRead = sizeof(ULONG);
            Status = NDIS_STATUS_SUCCESS;
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            if (pSet->InformationBufferLength != sizeof(ULONG))
            {
                pSet->BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }
            // TODO: For the time being we simply ignore packet filter settings.
            pSet->BytesRead = pSet->InformationBufferLength;
            Status = NDIS_STATUS_SUCCESS;
            break;

        case OID_GEN_INTERRUPT_MODERATION:
            pSet->BytesNeeded = 0;
            pSet->BytesRead = 0;
            Status = NDIS_STATUS_INVALID_DATA;
            break;

        case OID_PNP_SET_POWER:
            if (pSet->InformationBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }
            pSet->BytesRead = sizeof(NDIS_DEVICE_POWER_STATE);
            Status = NDIS_STATUS_SUCCESS;
            break;

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    LogFlow(("<==nemuNetAdpWinOidRqSet: status=0x%x\n", Status));
    return Status;
}

DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinOidRequest(IN NDIS_HANDLE MiniportAdapterContext,
                                                IN PNDIS_OID_REQUEST NdisRequest)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinOidRequest\n"));
    nemuNetCmnWinDumpOidRequest(__FUNCTION__, NdisRequest);

    switch (NdisRequest->RequestType)
    {
#if 0
        case NdisRequestMethod:
            Status = nemuNetAdpWinOidRqMethod(pAdapter, NdisRequest);
            break;
#endif

        case NdisRequestSetInformation:
            Status = nemuNetAdpWinOidRqSet(pAdapter, NdisRequest);
            break;

        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            Status = nemuNetAdpWinOidRqQuery(pAdapter, NdisRequest);
            break;

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }
    LogFlow(("<==nemuNetAdpWinOidRequest: status=0x%x\n", Status));
    return Status;
}

DECLHIDDEN(VOID) nemuNetAdpWinSendNetBufferLists(IN NDIS_HANDLE MiniportAdapterContext,
                                                 IN PNET_BUFFER_LIST NetBufferLists,
                                                 IN NDIS_PORT_NUMBER PortNumber,
                                                 IN ULONG SendFlags)
{
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinSendNetBufferLists\n"));
    PNET_BUFFER_LIST pNbl = NetBufferLists;
    for (pNbl = NetBufferLists; pNbl; pNbl = NET_BUFFER_LIST_NEXT_NBL(pNbl))
        NET_BUFFER_LIST_STATUS(pNbl) = NDIS_STATUS_SUCCESS;
    NdisMSendNetBufferListsComplete(pAdapter->hAdapter, NetBufferLists,
                                    (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL) ?
                                    NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
    LogFlow(("<==nemuNetAdpWinSendNetBufferLists\n"));
}

DECLHIDDEN(VOID) nemuNetAdpWinReturnNetBufferLists(IN NDIS_HANDLE MiniportAdapterContext,
                                                   IN PNET_BUFFER_LIST NetBufferLists,
                                                   IN ULONG ReturnFlags)
{
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinReturnNetBufferLists\n"));
    Log(("nemuNetAdpWinReturnNetBufferLists: We should not be here!\n"));
    LogFlow(("<==nemuNetAdpWinReturnNetBufferLists\n"));
}

DECLHIDDEN(VOID) nemuNetAdpWinCancelSend(IN NDIS_HANDLE MiniportAdapterContext,
                                         IN PVOID CancelId)
{
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinCancelSend\n"));
    Log(("nemuNetAdpWinCancelSend: We should not be here!\n"));
    LogFlow(("<==nemuNetAdpWinCancelSend\n"));
}


DECLHIDDEN(BOOLEAN) nemuNetAdpWinCheckForHangEx(IN NDIS_HANDLE MiniportAdapterContext)
{
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinCheckForHangEx\n"));
    LogFlow(("<==nemuNetAdpWinCheckForHangEx return false\n"));
    return FALSE;
}

DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinResetEx(IN NDIS_HANDLE MiniportAdapterContext,
                                             OUT PBOOLEAN AddressingReset)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    LogFlow(("==>nemuNetAdpWinResetEx\n"));
    LogFlow(("<==nemuNetAdpWinResetEx: status=0x%x\n", Status));
    return Status;
}

DECLHIDDEN(VOID) nemuNetAdpWinDevicePnPEventNotify(IN NDIS_HANDLE MiniportAdapterContext,
                                                   IN PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinDevicePnPEventNotify\n"));
    Log(("nemuNetAdpWinDevicePnPEventNotify: PnP event=%d\n", NetDevicePnPEvent->DevicePnPEvent));
    LogFlow(("<==nemuNetAdpWinDevicePnPEventNotify\n"));
}


DECLHIDDEN(VOID) nemuNetAdpWinShutdownEx(IN NDIS_HANDLE MiniportAdapterContext,
                                         IN NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinShutdownEx\n"));
    Log(("nemuNetAdpWinShutdownEx: action=%d\n", ShutdownAction));
    LogFlow(("<==nemuNetAdpWinShutdownEx\n"));
}

DECLHIDDEN(VOID) nemuNetAdpWinCancelOidRequest(IN NDIS_HANDLE MiniportAdapterContext,
                                               IN PVOID RequestId)
{
    PNEMUNETADP_ADAPTER pAdapter = (PNEMUNETADP_ADAPTER)MiniportAdapterContext;
    LogFlow(("==>nemuNetAdpWinCancelOidRequest\n"));
    Log(("nemuNetAdpWinCancelOidRequest: req id=%p\n", RequestId));
    LogFlow(("<==nemuNetAdpWinCancelOidRequest\n"));
}



DECLHIDDEN(VOID) nemuNetAdpWinUnload(IN PDRIVER_OBJECT DriverObject)
{
    LogFlow(("==>nemuNetAdpWinUnload\n"));
    //nemuNetAdpWinDevDestroy(&g_NemuNetAdpGlobals);
    if (g_NemuNetAdpGlobals.hMiniportDriver)
        NdisMDeregisterMiniportDriver(g_NemuNetAdpGlobals.hMiniportDriver);
    //NdisFreeSpinLock(&g_NemuNetAdpGlobals.Lock);
    LogFlow(("<==nemuNetAdpWinUnload\n"));
    RTR0Term();
}


/**
 * register the miniport driver
 */
DECLHIDDEN(NDIS_STATUS) nemuNetAdpWinRegister(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPathStr)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS MChars;

    NdisZeroMemory(&MChars, sizeof (MChars));

    MChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    MChars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    MChars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;

    MChars.MajorNdisVersion = NEMUNETADP_VERSION_NDIS_MAJOR;
    MChars.MinorNdisVersion = NEMUNETADP_VERSION_NDIS_MINOR;

    MChars.MajorDriverVersion = NEMUNETADP_VERSION_MAJOR;
    MChars.MinorDriverVersion = NEMUNETADP_VERSION_MINOR;

    MChars.InitializeHandlerEx         = nemuNetAdpWinInitializeEx;
    MChars.HaltHandlerEx               = nemuNetAdpWinHaltEx;
    MChars.UnloadHandler               = nemuNetAdpWinUnload;
    MChars.PauseHandler                = nemuNetAdpWinPause;
    MChars.RestartHandler              = nemuNetAdpWinRestart;
    MChars.OidRequestHandler           = nemuNetAdpWinOidRequest;
    MChars.SendNetBufferListsHandler   = nemuNetAdpWinSendNetBufferLists;
    MChars.ReturnNetBufferListsHandler = nemuNetAdpWinReturnNetBufferLists;
    MChars.CancelSendHandler           = nemuNetAdpWinCancelSend;
    MChars.CheckForHangHandlerEx       = nemuNetAdpWinCheckForHangEx;
    MChars.ResetHandlerEx              = nemuNetAdpWinResetEx;
    MChars.DevicePnPEventNotifyHandler = nemuNetAdpWinDevicePnPEventNotify;
    MChars.ShutdownHandlerEx           = nemuNetAdpWinShutdownEx;
    MChars.CancelOidRequestHandler     = nemuNetAdpWinCancelOidRequest;

    NDIS_STATUS Status;
    g_NemuNetAdpGlobals.hMiniportDriver = NULL;
    Log(("nemuNetAdpWinRegister: registering miniport driver...\n"));
    Status = NdisMRegisterMiniportDriver(pDriverObject,
                                         pRegistryPathStr,
                                         (NDIS_HANDLE)&g_NemuNetAdpGlobals,
                                         &MChars,
                                         &g_NemuNetAdpGlobals.hMiniportDriver);
    Assert(Status == STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        Log(("nemuNetAdpWinRegister: successfully registered miniport driver; registering device...\n"));
        //Status = nemuNetAdpWinDevCreate(&g_NemuNetAdpGlobals);
        //Assert(Status == STATUS_SUCCESS);
        //Log(("nemuNetAdpWinRegister: nemuNetAdpWinDevCreate() returned 0x%x\n", Status));
    }
    else
    {
        Log(("ERROR! nemuNetAdpWinRegister: failed to register miniport driver, status=0x%x", Status));
    }
    return Status;
}


RT_C_DECLS_BEGIN

NTSTATUS DriverEntry(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath);

RT_C_DECLS_END

NTSTATUS DriverEntry(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING pRegistryPath)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    int rc;


    rc = RTR0Init(0);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        NdisZeroMemory(&g_NemuNetAdpGlobals, sizeof (g_NemuNetAdpGlobals));
        //NdisAllocateSpinLock(&g_NemuNetAdpGlobals.Lock);
        //g_NemuNetAdpGlobals.PMCaps.WakeUpCapabilities.Flags = NDIS_DEVICE_WAKE_UP_ENABLE;
        g_NemuNetAdpGlobals.PMCaps.WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
        g_NemuNetAdpGlobals.PMCaps.WakeUpCapabilities.MinPatternWakeUp = NdisDeviceStateUnspecified;

        Status = nemuNetAdpWinRegister(pDriverObject, pRegistryPath);
        Assert(Status == STATUS_SUCCESS);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            Log(("NETADP: started successfully\n"));
            return STATUS_SUCCESS;
        }
        //NdisFreeSpinLock(&g_NemuNetAdpGlobals.Lock);
        RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
        RTLogDestroy(RTLogSetDefaultInstance(NULL));

        RTR0Term();
    }
    else
    {
        Status = NDIS_STATUS_FAILURE;
    }

    return Status;
}

