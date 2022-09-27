/* $Id: NemuNetFltM-win.cpp $ */
/** @file
 * NemuNetFltM-win.cpp - Bridged Networking Driver, Windows Specific Code.
 * Miniport edge
 */
/*
 * Copyright (C) 2011-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#include "NemuNetFltCmn-win.h"

static const char* nemuNetFltWinMpDumpOid(ULONG oid);

#ifndef NEMUNETADP
static NDIS_STATUS nemuNetFltWinMpInitialize(OUT PNDIS_STATUS OpenErrorStatus,
        OUT PUINT SelectedMediumIndex,
        IN PNDIS_MEDIUM MediumArray,
        IN UINT MediumArraySize,
        IN NDIS_HANDLE MiniportAdapterHandle,
        IN NDIS_HANDLE WrapperConfigurationContext)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)NdisIMGetDeviceContext(MiniportAdapterHandle);
    NDIS_STATUS Status = NDIS_STATUS_FAILURE;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));

    pNetFlt->u.s.WinIf.hMiniport = MiniportAdapterHandle;
    Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Initializing);
    /* the MP state should be already set to kNemuNetDevOpState_Initializing, just a paranoia
     * in case NDIS for some reason calls us in some irregular way */
    nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Initializing);

    NDIS_MEDIUM enmMedium = pNetFlt->u.s.WinIf.enmMedium;
    if (enmMedium == NdisMediumWan)
        enmMedium = NdisMedium802_3;

    UINT i = 0;
    for (; i < MediumArraySize; i++)
    {
        if (MediumArray[i] == enmMedium)
        {
            *SelectedMediumIndex = i;
            break;
        }
    }

    do
    {
        if (i != MediumArraySize)
        {
            NdisMSetAttributesEx(MiniportAdapterHandle, pNetFlt, 0,
                                     NDIS_ATTRIBUTE_IGNORE_PACKET_TIMEOUT    |
                                     NDIS_ATTRIBUTE_IGNORE_REQUEST_TIMEOUT|
                                     NDIS_ATTRIBUTE_INTERMEDIATE_DRIVER |
                                     NDIS_ATTRIBUTE_DESERIALIZE |
                                     NDIS_ATTRIBUTE_NO_HALT_ON_SUSPEND,
                                     NdisInterfaceInternal /* 0 */);

            pNetFlt->u.s.WinIf.MpIndicatedMediaStatus = NDIS_STATUS_MEDIA_CONNECT;
            Assert(nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) == NdisDeviceStateD3);
            nemuNetFltWinSetPowerState(&pNetFlt->u.s.WinIf.MpState, NdisDeviceStateD0);
            Assert(pNetFlt->u.s.WinIf.MpState.OpState == kNemuNetDevOpState_Initializing);
            nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Initialized);

            Status = NDIS_STATUS_SUCCESS;
            break;
        }
        else
        {
            Status = NDIS_STATUS_UNSUPPORTED_MEDIA;
        }

        Assert(Status != NDIS_STATUS_SUCCESS);
        Assert(nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) == NdisDeviceStateD3);
        Assert(pNetFlt->u.s.WinIf.MpState.OpState == kNemuNetDevOpState_Initializing);
        nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
    } while (0);

    NdisSetEvent(&pNetFlt->u.s.WinIf.MpInitCompleteEvent);

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Status (0x%x)\n", pNetFlt, Status));

    *OpenErrorStatus = Status;

    return Status;
}

/**
 * process the packet send in a "passthru" mode
 */
static NDIS_STATUS nemuNetFltWinSendPassThru(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket
#ifdef NEMUNETFLT_NO_PACKET_QUEUE
    , bool bNetFltActive
#endif
    )
{
    PNDIS_PACKET pMyPacket;
    NDIS_STATUS Status = nemuNetFltWinPrepareSendPacket(pNetFlt, pPacket, &pMyPacket);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
#if !defined(NEMU_LOOPBACK_USEFLAGS) /* || defined(DEBUG_NETFLT_PACKETS) */
# ifdef NEMUNETFLT_NO_PACKET_QUEUE
        if (bNetFltActive)
            nemuNetFltWinLbPutSendPacket(pNetFlt, pMyPacket, false /* bFromIntNet */);
# else
        /* no need for the loop enqueue & check in a passthru mode , ndis will do everything for us */
# endif
#endif
        NdisSend(&Status, pNetFlt->u.s.WinIf.hBinding, pMyPacket);
        if (Status != NDIS_STATUS_PENDING)
        {
            NdisIMCopySendCompletePerPacketInfo(pPacket, pMyPacket);
#if defined(NEMUNETFLT_NO_PACKET_QUEUE) && !defined(NEMU_LOOPBACK_USEFLAGS)
        if (bNetFltActive)
            nemuNetFltWinLbRemoveSendPacket(pNetFlt, pMyPacket);
#endif
            NdisFreePacket(pMyPacket);
        }
    }
    return Status;
}

#else /* defined NEMUNETADP */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpDoDeinitialization(PNEMUNETFLTINS pNetFlt)
{
    uint64_t NanoTS = RTTimeSystemNanoTS();

    Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Initialized);

    RTSpinlockAcquire(pNetFlt->hSpinlock);
    ASMAtomicUoWriteBool(&pNetFlt->fDisconnectedFromHost, true);
    ASMAtomicUoWriteBool(&pNetFlt->fRediscoveryPending, false);
    ASMAtomicUoWriteU64(&pNetFlt->NanoTSLastRediscovery, NanoTS);

    nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitializing);

    RTSpinlockRelease(pNetFlt->hSpinlock);

    nemuNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.MpState);

    /* check packet pool is empty */
    int cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hRecvPacketPool);
    Assert(cPPUsage == 0);
    /* for debugging only, ignore the err in release */
    NOREF(cPPUsage);

    nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);

    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS nemuNetFltWinMpReadApplyConfig(PNEMUNETFLTINS pThis, NDIS_HANDLE hMiniportAdapter, NDIS_HANDLE hWrapperConfigurationContext)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    NDIS_HANDLE hConfiguration;
    PNDIS_CONFIGURATION_PARAMETER pParameterValue;
    NDIS_STRING strMAC = NDIS_STRING_CONST("MAC");
    RTMAC mac;

    NdisOpenConfiguration(
        &Status,
        &hConfiguration,
        hWrapperConfigurationContext);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        do
        {
            int rc;
            NDIS_CONFIGURATION_PARAMETER param;
            WCHAR MacBuf[13];

            NdisReadConfiguration(&Status,
                                  &pParameterValue,
                                  hConfiguration,
                                  &strMAC,
                                  NdisParameterString);
//            Assert(Status == NDIS_STATUS_SUCCESS);
            if (Status == NDIS_STATUS_SUCCESS)
            {

                rc = nemuNetFltWinMACFromNdisString(&mac, &pParameterValue->ParameterData.StringData);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    break;
                }
            }

            nemuNetFltWinGenerateMACAddress(&mac);
            param.ParameterType = NdisParameterString;
            param.ParameterData.StringData.Buffer = MacBuf;
            param.ParameterData.StringData.MaximumLength = sizeof(MacBuf);

            rc = nemuNetFltWinMAC2NdisString(&mac, &param.ParameterData.StringData);
            Assert(RT_SUCCESS(rc));
            if (RT_SUCCESS(rc))
            {
                NdisWriteConfiguration(&Status,
                        hConfiguration,
                        &strMAC,
                        &param);
                Assert(Status == NDIS_STATUS_SUCCESS);
                if (Status != NDIS_STATUS_SUCCESS)
                {
                    /* ignore the failure */
                    Status = NDIS_STATUS_SUCCESS;
                }
            }
        } while (0);

        NdisCloseConfiguration(hConfiguration);
    }
    else
    {
        nemuNetFltWinGenerateMACAddress(&mac);
    }

    pThis->u.s.MacAddr = mac;

    return NDIS_STATUS_SUCCESS;
}

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpDoInitialization(PNEMUNETFLTINS pNetFlt, NDIS_HANDLE hMiniportAdapter, NDIS_HANDLE hWrapperConfigurationContext)
{
    NDIS_STATUS Status;
    pNetFlt->u.s.WinIf.hMiniport = hMiniportAdapter;

    LogFlow(("==>"__FUNCTION__" : pNetFlt 0x%p\n", pNetFlt));

    Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
    nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Initializing);

    nemuNetFltWinMpReadApplyConfig(pNetFlt, hMiniportAdapter, hWrapperConfigurationContext);

    NdisMSetAttributesEx(hMiniportAdapter, pNetFlt,
                            0, /* CheckForHangTimeInSeconds */
                            NDIS_ATTRIBUTE_DESERIALIZE |
                            NDIS_ATTRIBUTE_NO_HALT_ON_SUSPEND,
                            NdisInterfaceInternal/* 0 */);

    Assert(nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) == NdisDeviceStateD3);
    nemuNetFltWinSetPowerState(&pNetFlt->u.s.WinIf.MpState, NdisDeviceStateD0);
    Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Initializing);
    nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Initialized);

    Status = NDIS_STATUS_SUCCESS;

    LogFlow(("<=="__FUNCTION__" : pNetFlt 0x%p, Status 0x%x\n", pNetFlt, Status));

    return Status;
}

static NDIS_STATUS nemuNetFltWinMpInitialize(OUT PNDIS_STATUS OpenErrorStatus,
                        OUT PUINT SelectedMediumIndex,
                        IN PNDIS_MEDIUM MediumArray,
                        IN UINT MediumArraySize,
                        IN NDIS_HANDLE MiniportAdapterHandle,
                        IN NDIS_HANDLE WrapperConfigurationContext)
{

    NDIS_STATUS Status = NDIS_STATUS_FAILURE;
    UINT i = 0;

    LogFlow(("==>"__FUNCTION__"\n"));

    for (; i < MediumArraySize; i++)
    {
        if (MediumArray[i] == NdisMedium802_3)
        {
            *SelectedMediumIndex = i;
            break;
        }
    }

    if (i != MediumArraySize)
    {
        PDEVICE_OBJECT pPdo, pFdo;
#define KEY_PREFIX L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\"
        UCHAR Buf[512];
        PUCHAR pSuffix;
        ULONG cbBuf;
        NDIS_STRING RtlStr;

        wcscpy((WCHAR*)Buf, KEY_PREFIX);
        pSuffix = Buf + (sizeof(KEY_PREFIX)-2);

        NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &pPdo,
                           &pFdo,
                           NULL, //Next Device Object
                           NULL,
                           NULL);

        Status = IoGetDeviceProperty (pPdo,
                                      DevicePropertyDriverKeyName,
                                      sizeof(Buf) - (sizeof(KEY_PREFIX)-2),
                                      pSuffix,
                                      &cbBuf);
        if (Status == STATUS_SUCCESS)
        {
            OBJECT_ATTRIBUTES ObjAttr;
            HANDLE hDrvKey;
            RtlStr.Buffer=(WCHAR*)Buf;
            RtlStr.Length=(USHORT)cbBuf - 2 + sizeof(KEY_PREFIX) - 2;
            RtlStr.MaximumLength=sizeof(Buf);

            InitializeObjectAttributes(&ObjAttr, &RtlStr, OBJ_CASE_INSENSITIVE, NULL, NULL);

            Status = ZwOpenKey(&hDrvKey, KEY_READ, &ObjAttr);
            if (Status == STATUS_SUCCESS)
            {
                static UNICODE_STRING NetCfgInstanceIdValue = NDIS_STRING_CONST("NetCfgInstanceId");
//                UCHAR valBuf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + RTUUID_STR_LENGTH*2 + 10];
//                ULONG cLength = sizeof(valBuf);
#define NAME_PREFIX L"\\DEVICE\\"
                PKEY_VALUE_PARTIAL_INFORMATION pInfo = (PKEY_VALUE_PARTIAL_INFORMATION)Buf;
                Status = ZwQueryValueKey(hDrvKey,
                            &NetCfgInstanceIdValue,
                            KeyValuePartialInformation,
                            pInfo,
                            sizeof(Buf),
                            &cbBuf);
                if (Status == STATUS_SUCCESS)
                {
                    if (pInfo->Type == REG_SZ && pInfo->DataLength > 2)
                    {
                        WCHAR *pName;
                        Status = nemuNetFltWinMemAlloc((PVOID*)&pName, pInfo->DataLength + sizeof(NAME_PREFIX));
                        if (Status == STATUS_SUCCESS)
                        {
                            PNEMUNETFLTINS pNetFlt;
                            wcscpy(pName, NAME_PREFIX);
                            wcscpy(pName+(sizeof(NAME_PREFIX)-2)/2, (WCHAR*)pInfo->Data);
                            RtlStr.Buffer=pName;
                            RtlStr.Length = (USHORT)pInfo->DataLength - 2 + sizeof(NAME_PREFIX) - 2;
                            RtlStr.MaximumLength = (USHORT)pInfo->DataLength + sizeof(NAME_PREFIX);

                            Status = nemuNetFltWinPtInitBind(&pNetFlt, MiniportAdapterHandle, &RtlStr, WrapperConfigurationContext);

                            if (Status == STATUS_SUCCESS)
                            {
                                Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Initialized);
                                nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Initialized);
#if 0
                                NdisMIndicateStatus(pNetFlt->u.s.WinIf.hMiniport,
                                                         NDIS_STATUS_MEDIA_CONNECT,
                                                         (PVOID)NULL,
                                                         0);
#endif
                            }
                            else
                            {
                                Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
                                nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
                            }

                            nemuNetFltWinMemFree(pName);

                        }
                    }
                    else
                    {
                        Status = NDIS_STATUS_FAILURE;
                    }
                }
            }
        }
    }
    else
    {
        Status = NDIS_STATUS_UNSUPPORTED_MEDIA;
    }

    /* TODO: */
    *OpenErrorStatus = Status;

    LogFlow(("<=="__FUNCTION__": Status (0x%x)\n", Status));

    return Status;
}
#endif

static VOID nemuNetFltWinMpSendPackets(IN NDIS_HANDLE hMiniportAdapterContext,
        IN PPNDIS_PACKET pPacketArray,
        IN UINT cNumberOfPackets)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hMiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    bool bNetFltActive;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));

    Assert(cNumberOfPackets);

    if (nemuNetFltWinIncReferenceWinIfNetFlt(pNetFlt, cNumberOfPackets, &bNetFltActive))
    {
        uint32_t cAdaptRefs = cNumberOfPackets;
        uint32_t cNetFltRefs;
        uint32_t cPassThruRefs;
        if (bNetFltActive)
        {
            cNetFltRefs = cNumberOfPackets;
            cPassThruRefs = 0;
        }
        else
        {
            cPassThruRefs = cNumberOfPackets;
            cNetFltRefs = 0;
        }

        for (UINT i = 0; i < cNumberOfPackets; i++)
        {
            PNDIS_PACKET pPacket;

            pPacket = pPacketArray[i];

            if (!cNetFltRefs
#ifdef NEMUNETFLT_NO_PACKET_QUEUE
                    || !nemuNetFltWinPostIntnet(pNetFlt, pPacket, NEMUNETFLT_PACKET_SRC_HOST)
#else
                    || (fStatus = nemuNetFltWinQuEnqueuePacket(pNetFlt, pPacket, NEMUNETFLT_PACKET_SRC_HOST)) != NDIS_STATUS_SUCCESS
#endif
                    )
            {
#ifndef NEMUNETADP
                Status = nemuNetFltWinSendPassThru(pNetFlt, pPacket
#ifdef NEMUNETFLT_NO_PACKET_QUEUE
                               , !!cNetFltRefs
#endif
                        );
#else
                if (!cNetFltRefs)
                {
# ifdef NEMUNETADP_REPORT_DISCONNECTED
                    Status = NDIS_STATUS_MEDIA_DISCONNECT;
                    STATISTIC_INCREASE(pNetFlt->u.s.WinIf.cTxError);
# else
                    Status = NDIS_STATUS_SUCCESS;
# endif
                }
#endif

                if (Status != NDIS_STATUS_PENDING)
                {
                    NdisMSendComplete(pNetFlt->u.s.WinIf.hMiniport, pPacket, Status);
                }
                else
                {
                    cAdaptRefs--;
                }
            }
            else
            {
#ifdef NEMUNETFLT_NO_PACKET_QUEUE
                NdisMSendComplete(pNetFlt->u.s.WinIf.hMiniport, pPacket, NDIS_STATUS_SUCCESS);
#else
                cAdaptRefs--;
                cNetFltRefs--;
#endif
            }
        }

        if (cNetFltRefs)
        {
            nemuNetFltWinDecReferenceNetFlt(pNetFlt, cNetFltRefs);
        }
        else if (cPassThruRefs)
        {
            nemuNetFltWinDecReferenceModePassThru(pNetFlt, cPassThruRefs);
        }
        if (cAdaptRefs)
        {
            nemuNetFltWinDecReferenceWinIf(pNetFlt, cAdaptRefs);
        }
    }
    else
    {
        NDIS_HANDLE h = pNetFlt->u.s.WinIf.hMiniport;
        Assert(0);
        if (h)
        {
            for (UINT i = 0; i < cNumberOfPackets; i++)
            {
                PNDIS_PACKET pPacket;
                pPacket = pPacketArray[i];
                NdisMSendComplete(h, pPacket, NDIS_STATUS_FAILURE);
            }
        }
    }

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));
}

#ifndef NEMUNETADP
static UINT nemuNetFltWinMpRequestStatePrep(PNEMUNETFLTINS pNetFlt, NDIS_STATUS *pStatus)
{
    Assert(!pNetFlt->u.s.WinIf.StateFlags.fRequestInfo);

    if (nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.PtState) > kNemuNetDevOpState_Initialized /* protocol unbind in progress */
            || nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) > NdisDeviceStateD0)
    {
        *pStatus = NDIS_STATUS_FAILURE;
        return 0;
    }

    RTSpinlockAcquire(pNetFlt->hSpinlock);
    Assert(!pNetFlt->u.s.WinIf.StateFlags.fRequestInfo);
    if (nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.PtState) > kNemuNetDevOpState_Initialized /* protocol unbind in progress */
            || nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) > NdisDeviceStateD0)
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        *pStatus = NDIS_STATUS_FAILURE;
        return 0;
    }

    if ((nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.PtState) > NdisDeviceStateD0)
            && !pNetFlt->u.s.WinIf.StateFlags.fStandBy)
    {
        pNetFlt->u.s.WinIf.StateFlags.fRequestInfo = NEMUNDISREQUEST_INPROGRESS | NEMUNDISREQUEST_QUEUED;
        RTSpinlockRelease(pNetFlt->hSpinlock);
        *pStatus = NDIS_STATUS_PENDING;
        return NEMUNDISREQUEST_INPROGRESS | NEMUNDISREQUEST_QUEUED;
    }

    if (pNetFlt->u.s.WinIf.StateFlags.fStandBy)
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        *pStatus = NDIS_STATUS_FAILURE;
        return 0;
    }

    pNetFlt->u.s.WinIf.StateFlags.fRequestInfo = NEMUNDISREQUEST_INPROGRESS;

    RTSpinlockRelease(pNetFlt->hSpinlock);

    *pStatus = NDIS_STATUS_SUCCESS;
    return NEMUNDISREQUEST_INPROGRESS;
}

static NDIS_STATUS nemuNetFltWinMpRequestPostQuery(PNEMUNETFLTINS pNetFlt)
{
    if (pNetFlt->u.s.WinIf.PassDownRequest.DATA.QUERY_INFORMATION.Oid == OID_GEN_CURRENT_PACKET_FILTER && NEMUNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt))
    {
        bool fNetFltActive;
        const bool fWinIfActive = nemuNetFltWinReferenceWinIfNetFlt(pNetFlt, &fNetFltActive);

        Assert(pNetFlt->u.s.WinIf.PassDownRequest.DATA.QUERY_INFORMATION.InformationBuffer);
        Assert(!pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter);

        if (fNetFltActive)
        {
            /* netflt is active, simply return the cached value */
            *((PULONG)pNetFlt->u.s.WinIf.PassDownRequest.DATA.QUERY_INFORMATION.InformationBuffer) = pNetFlt->u.s.WinIf.fUpperProtocolSetFilter;

            /* we've intercepted the query and completed it */
            nemuNetFltWinMpRequestStateComplete(pNetFlt);

            nemuNetFltWinDereferenceNetFlt(pNetFlt);
            nemuNetFltWinDereferenceWinIf(pNetFlt);

            return NDIS_STATUS_SUCCESS;
        }
        else if (fWinIfActive)
        {
            pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter = 1;
            pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt = 0;
            /* we're cleaning it in RequestComplete */
        }
    }

    NDIS_STATUS Status;
    /* issue the request */
    NdisRequest(&Status, pNetFlt->u.s.WinIf.hBinding, &pNetFlt->u.s.WinIf.PassDownRequest);
    if (Status != NDIS_STATUS_PENDING)
    {
        nemuNetFltWinPtRequestComplete(pNetFlt, &pNetFlt->u.s.WinIf.PassDownRequest, Status);
        Status = NDIS_STATUS_PENDING;
    }

    return Status;
}

static NDIS_STATUS nemuNetFltWinMpQueryInformation(IN NDIS_HANDLE MiniportAdapterContext,
        IN NDIS_OID Oid,
        IN PVOID InformationBuffer,
        IN ULONG InformationBufferLength,
        OUT PULONG BytesWritten,
        OUT PULONG BytesNeeded)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_FAILURE;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p), Oid (%s)\n", pNetFlt, nemuNetFltWinMpDumpOid(Oid)));

    /* fist check if this is the oid we want to pass down */
    switch (Oid)
    {
        case OID_PNP_QUERY_POWER:
            Status = NDIS_STATUS_SUCCESS;
            break;
        case OID_TCP_TASK_OFFLOAD:
        case OID_GEN_SUPPORTED_GUIDS:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
        default:
        {
            /* the oid is to be passed down,
             * check the device state if we can do it
             * and update device state accordingly */
            UINT uOp = nemuNetFltWinMpRequestStatePrep(pNetFlt, &Status);
            if (uOp)
            {
                /* save the request info */
                pNetFlt->u.s.WinIf.PassDownRequest.RequestType = NdisRequestQueryInformation;
                pNetFlt->u.s.WinIf.PassDownRequest.DATA.QUERY_INFORMATION.Oid = Oid;
                pNetFlt->u.s.WinIf.PassDownRequest.DATA.QUERY_INFORMATION.InformationBuffer = InformationBuffer;
                pNetFlt->u.s.WinIf.PassDownRequest.DATA.QUERY_INFORMATION.InformationBufferLength = InformationBufferLength;
                pNetFlt->u.s.WinIf.pcPDRBytesNeeded = BytesNeeded;
                pNetFlt->u.s.WinIf.pcPDRBytesRW = BytesWritten;

                /* the oid can be processed */
                if (!(uOp & NEMUNDISREQUEST_QUEUED))
                {
                    Status = nemuNetFltWinMpRequestPostQuery(pNetFlt);
                }
            }
            break;
        }
    }

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Oid (%s), Status (0x%x)\n", pNetFlt, nemuNetFltWinMpDumpOid(Oid), Status));

    return Status;
}

#endif /* ifndef NEMUNETADP*/

static NDIS_STATUS nemuNetFltWinMpHandlePowerState(PNEMUNETFLTINS pNetFlt, NDIS_DEVICE_POWER_STATE enmState)
{
    if (nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) > NdisDeviceStateD0
            && enmState != NdisDeviceStateD0)
    {
        /* invalid state transformation */
        Assert(0);
        return NDIS_STATUS_FAILURE;
    }

#ifndef NEMUNETADP
    if (nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) == NdisDeviceStateD0
            && enmState > NdisDeviceStateD0)
    {
        pNetFlt->u.s.WinIf.StateFlags.fStandBy = TRUE;
    }

    if (nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) > NdisDeviceStateD0
            && enmState == NdisDeviceStateD0)
    {
        pNetFlt->u.s.WinIf.StateFlags.fStandBy = FALSE;
    }
#endif

    nemuNetFltWinSetPowerState(&pNetFlt->u.s.WinIf.MpState, enmState);

#ifndef NEMUNETADP
    if (pNetFlt->u.s.WinIf.StateFlags.fStandBy == FALSE)
    {
        if (pNetFlt->u.s.WinIf.MpIndicatedMediaStatus != pNetFlt->u.s.WinIf.MpUnindicatedMediaStatus)
        {
           NdisMIndicateStatus(pNetFlt->u.s.WinIf.hMiniport, pNetFlt->u.s.WinIf.MpUnindicatedMediaStatus, NULL, 0);
           NdisMIndicateStatusComplete(pNetFlt->u.s.WinIf.hMiniport);
           pNetFlt->u.s.WinIf.MpIndicatedMediaStatus = pNetFlt->u.s.WinIf.MpUnindicatedMediaStatus;
        }
    }
    else
    {
        pNetFlt->u.s.WinIf.MpUnindicatedMediaStatus = pNetFlt->u.s.WinIf.MpIndicatedMediaStatus;
    }
#endif

    return NDIS_STATUS_SUCCESS;
}

#ifndef NEMUNETADP
static NDIS_STATUS nemuNetFltWinMpRequestPostSet(PNEMUNETFLTINS pNetFlt)
{
    if (pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.Oid == OID_GEN_CURRENT_PACKET_FILTER && NEMUNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt))
    {
        /* need to disable cleaning promiscuous here ?? */
        bool fNetFltActive;
        const bool fWinIfActive = nemuNetFltWinReferenceWinIfNetFlt(pNetFlt, &fNetFltActive);

        Assert(pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.InformationBuffer);
        Assert(!pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter);

        if (fNetFltActive)
        {
            Assert(fWinIfActive);

            /* netflt is active, update the cached value */
            /* TODO: in case we are are not in promiscuous now, we are issuing a request.
             * what should we do in case of a failure?
             * i.e. should we update the fUpperProtocolSetFilter in completion routine in this case? etc. */
            pNetFlt->u.s.WinIf.fUpperProtocolSetFilter = *((PULONG)pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.InformationBuffer);
            pNetFlt->u.s.WinIf.StateFlags.fUpperProtSetFilterInitialized = TRUE;

            if (!(pNetFlt->u.s.WinIf.fOurSetFilter & NDIS_PACKET_TYPE_PROMISCUOUS))
            {
                pNetFlt->u.s.WinIf.fSetFilterBuffer = NDIS_PACKET_TYPE_PROMISCUOUS;
                pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.InformationBuffer = &pNetFlt->u.s.WinIf.fSetFilterBuffer;
                pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.InformationBufferLength = sizeof (pNetFlt->u.s.WinIf.fSetFilterBuffer);
                pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter = 1;
                pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt = 1;
                /* we'll do dereferencing in request complete */
            }
            else
            {
                nemuNetFltWinDereferenceNetFlt(pNetFlt);
                nemuNetFltWinDereferenceWinIf(pNetFlt);

                /* we've intercepted the query and completed it */
                nemuNetFltWinMpRequestStateComplete(pNetFlt);
                return NDIS_STATUS_SUCCESS;
            }
        }
        else if (fWinIfActive)
        {
            pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter = 1;
            pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt = 0;
            /* dereference on completion */
        }
    }

    NDIS_STATUS Status;

    NdisRequest(&Status, pNetFlt->u.s.WinIf.hBinding, &pNetFlt->u.s.WinIf.PassDownRequest);
    if (Status != NDIS_STATUS_PENDING)
    {
        nemuNetFltWinPtRequestComplete(pNetFlt, &pNetFlt->u.s.WinIf.PassDownRequest, Status);
    }

    return Status;
}

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpRequestPost(PNEMUNETFLTINS pNetFlt)
{
    switch (pNetFlt->u.s.WinIf.PassDownRequest.RequestType)
    {
        case NdisRequestQueryInformation:
            return nemuNetFltWinMpRequestPostQuery(pNetFlt);
        case NdisRequestSetInformation:
            return nemuNetFltWinMpRequestPostSet(pNetFlt);
        default:
            AssertBreakpoint();
            return NDIS_STATUS_FAILURE;
    }
}

static NDIS_STATUS nemuNetFltWinMpSetInformation(IN NDIS_HANDLE MiniportAdapterContext,
        IN NDIS_OID Oid,
        IN PVOID InformationBuffer,
        IN ULONG InformationBufferLength,
        OUT PULONG BytesRead,
        OUT PULONG BytesNeeded)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_FAILURE;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p), Oid (%s)\n", pNetFlt, nemuNetFltWinMpDumpOid(Oid)));

    switch (Oid)
    {
        case OID_PNP_SET_POWER:
        {
            if (InformationBufferLength >= sizeof (NDIS_DEVICE_POWER_STATE))
            {
                NDIS_DEVICE_POWER_STATE *penmState = (NDIS_DEVICE_POWER_STATE*)InformationBuffer;
                Status = nemuNetFltWinMpHandlePowerState(pNetFlt, *penmState);
            }
            else
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
            }

            if (Status == NDIS_STATUS_SUCCESS)
            {
                *BytesRead = sizeof (NDIS_DEVICE_POWER_STATE);
                *BytesNeeded = 0;
            }
            else
            {
                *BytesRead = 0;
                *BytesNeeded = sizeof (NDIS_DEVICE_POWER_STATE);
            }
            break;
        }
        default:
        {
            /* the oid is to be passed down,
             * check the device state if we can do it
             * and update device state accordingly */
            UINT uOp = nemuNetFltWinMpRequestStatePrep(pNetFlt, &Status);
            if (uOp)
            {
                /* save the request info */
                pNetFlt->u.s.WinIf.PassDownRequest.RequestType = NdisRequestSetInformation;
                pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.Oid = Oid;
                pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.InformationBuffer = InformationBuffer;
                pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.InformationBufferLength = InformationBufferLength;
                pNetFlt->u.s.WinIf.pcPDRBytesNeeded = BytesNeeded;
                pNetFlt->u.s.WinIf.pcPDRBytesRW = BytesRead;

                /* the oid can be processed */
                if (!(uOp & NEMUNDISREQUEST_QUEUED))
                {
                    Status = nemuNetFltWinMpRequestPostSet(pNetFlt);
                }
            }
            break;
        }
    }

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Oid (%s), Status (0x%x)\n", pNetFlt, nemuNetFltWinMpDumpOid(Oid), Status));

    return Status;
}
#else
static NDIS_OID g_nemuNetFltWinMpSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_PNP_CAPABILITIES,
    OID_PNP_QUERY_POWER,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_RCV_CRC_ERROR,
    OID_GEN_TRANSMIT_QUEUE_LENGTH,
    OID_PNP_SET_POWER,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAC_OPTIONS,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
    OID_802_3_XMIT_DEFERRED,
    OID_802_3_XMIT_MAX_COLLISIONS,
    OID_802_3_RCV_OVERRUN,
    OID_802_3_XMIT_UNDERRUN,
    OID_802_3_XMIT_HEARTBEAT_FAILURE,
    OID_802_3_XMIT_TIMES_CRS_LOST,
    OID_802_3_XMIT_LATE_COLLISIONS,
};

static NDIS_STATUS nemuNetFltWinMpQueryInformation(IN NDIS_HANDLE MiniportAdapterContext,
        IN NDIS_OID Oid,
        IN PVOID InformationBuffer,
        IN ULONG InformationBufferLength,
        OUT PULONG BytesWritten,
        OUT PULONG BytesNeeded)
{
    /* static data */
    static const NDIS_HARDWARE_STATUS enmHwStatus = NdisHardwareStatusReady;
    static const NDIS_MEDIUM enmMedium = NdisMedium802_3;
    static NDIS_PNP_CAPABILITIES PnPCaps = {0};
    static BOOLEAN bPnPCapsInited = FALSE;

    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    ULONG64 u64Info = 0;
    ULONG u32Info = 0;
    USHORT u16Info = 0;
    /* default is 4bytes */
    const void* pvInfo = (void*)&u32Info;
    ULONG cbInfo = sizeof (u32Info);

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p), Oid (%s)\n", pNetFlt, nemuNetFltWinMpDumpOid(Oid)));

    *BytesWritten = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            pvInfo = g_nemuNetFltWinMpSupportedOids;
            cbInfo = sizeof (g_nemuNetFltWinMpSupportedOids);
            break;

        case OID_GEN_HARDWARE_STATUS:
            pvInfo = &enmHwStatus;
            cbInfo = sizeof (NDIS_HARDWARE_STATUS);
            break;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            pvInfo = &enmMedium;
            cbInfo = sizeof (NDIS_MEDIUM);
            break;

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
            u32Info = NEMUNETADP_MAX_LOOKAHEAD_SIZE;
            break;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            u32Info = NEMUNETADP_MAX_PACKET_SIZE - NEMUNETADP_HEADER_SIZE;
            break;

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            u32Info = NEMUNETADP_MAX_PACKET_SIZE;
            break;

        case OID_GEN_MAC_OPTIONS:
            u32Info = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                        NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                        NDIS_MAC_OPTION_NO_LOOPBACK;
            break;

        case OID_GEN_LINK_SPEED:
            u32Info = NEMUNETADP_LINK_SPEED;
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        case OID_GEN_RECEIVE_BUFFER_SPACE:
            u32Info = NEMUNETADP_MAX_PACKET_SIZE * NEMUNETFLT_PACKET_INFO_POOL_SIZE;
            break;

        case OID_GEN_VENDOR_ID:
            u32Info = NEMUNETADP_VENDOR_ID;
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
            pvInfo = NEMUNETADP_VENDOR_DESC;
            cbInfo = sizeof (NEMUNETADP_VENDOR_DESC);
            break;

        case OID_GEN_VENDOR_DRIVER_VERSION:
            u32Info = NEMUNETADP_VENDOR_DRIVER_VERSION;
            break;

        case OID_GEN_DRIVER_VERSION:
            u16Info = (USHORT)((NEMUNETFLT_VERSION_MP_NDIS_MAJOR << 8) + NEMUNETFLT_VERSION_MP_NDIS_MINOR);
            pvInfo = (PVOID)&u16Info;
            cbInfo = sizeof (USHORT);
            break;

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            u32Info = NEMUNETFLT_PACKET_INFO_POOL_SIZE;
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
#ifdef NEMUNETADP_REPORT_DISCONNECTED
            {
                bool bNetFltActive;
                bool bActive = nemuNetFltWinReferenceWinIfNetFltFromAdapt(pNetFlt, bNetFltActive);
                if (bActive && bNetFltActive)
                {
                    u32Info = NdisMediaStateConnected;
                }
                else
                {
                    u32Info = NdisMediaStateDisconnected;
                }

                if (bActive)
                {
                    nemuNetFltWinDereferenceWinIf(pNetFlt);
                }
                if (bNetFltActive)
                {
                    nemuNetFltWinDereferenceNetFlt(pNetFlt);
                }
                else
                {
                    nemuNetFltWinDereferenceModePassThru(pNetFlt);
                }
            }
#else
            u32Info = NdisMediaStateConnected;
#endif
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            u32Info = NDIS_PACKET_TYPE_BROADCAST
                        | NDIS_PACKET_TYPE_DIRECTED
                        | NDIS_PACKET_TYPE_ALL_FUNCTIONAL
                        | NDIS_PACKET_TYPE_ALL_LOCAL
                        | NDIS_PACKET_TYPE_GROUP
                        | NDIS_PACKET_TYPE_MULTICAST;
            break;

        case OID_PNP_CAPABILITIES:
            if (!bPnPCapsInited)
            {
                PnPCaps.WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
                PnPCaps.WakeUpCapabilities.MinPatternWakeUp = NdisDeviceStateUnspecified;
                bPnPCapsInited = TRUE;
            }
            cbInfo = sizeof (NDIS_PNP_CAPABILITIES);
            pvInfo = &PnPCaps;

            break;

        case OID_PNP_QUERY_POWER:
            Status = NDIS_STATUS_SUCCESS;
            break;

        case OID_GEN_XMIT_OK:
            u64Info = pNetFlt->u.s.WinIf.cTxSuccess;
            pvInfo = &u64Info;
            if (InformationBufferLength >= sizeof (ULONG64) || InformationBufferLength == 0)
            {
                cbInfo = sizeof (ULONG64);
            }
            else
            {
                cbInfo = sizeof (ULONG);
            }
            *BytesNeeded = sizeof (ULONG64);
            break;

        case OID_GEN_RCV_OK:
            u64Info = pNetFlt->u.s.WinIf.cRxSuccess;
            pvInfo = &u64Info;
            if (InformationBufferLength >= sizeof (ULONG64) || InformationBufferLength == 0)
            {
                cbInfo = sizeof (ULONG64);
            }
            else
            {
                cbInfo = sizeof (ULONG);
            }
            *BytesNeeded = sizeof (ULONG64);
            break;

        case OID_GEN_XMIT_ERROR:
            u32Info = pNetFlt->u.s.WinIf.cTxError;
            break;

        case OID_GEN_RCV_ERROR:
            u32Info = pNetFlt->u.s.WinIf.cRxError;
            break;

        case OID_GEN_RCV_NO_BUFFER:
        case OID_GEN_RCV_CRC_ERROR:
            u32Info = 0;
            break;

        case OID_GEN_TRANSMIT_QUEUE_LENGTH:
            u32Info = NEMUNETFLT_PACKET_INFO_POOL_SIZE;
            break;

        case OID_802_3_PERMANENT_ADDRESS:
            pvInfo = &pNetFlt->u.s.MacAddr;
            cbInfo = NEMUNETADP_ETH_ADDRESS_LENGTH;
            break;

        case OID_802_3_CURRENT_ADDRESS:
            pvInfo = &pNetFlt->u.s.MacAddr;
            cbInfo = NEMUNETADP_ETH_ADDRESS_LENGTH;
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            u32Info = NEMUNETADP_MAX_MCAST_LIST;
            break;

        case OID_802_3_MAC_OPTIONS:
        case OID_802_3_RCV_ERROR_ALIGNMENT:
        case OID_802_3_XMIT_ONE_COLLISION:
        case OID_802_3_XMIT_MORE_COLLISIONS:
        case OID_802_3_XMIT_DEFERRED:
        case OID_802_3_XMIT_MAX_COLLISIONS:
        case OID_802_3_RCV_OVERRUN:
        case OID_802_3_XMIT_UNDERRUN:
        case OID_802_3_XMIT_HEARTBEAT_FAILURE:
        case OID_802_3_XMIT_TIMES_CRS_LOST:
        case OID_802_3_XMIT_LATE_COLLISIONS:
            u32Info = 0;
            break;

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    if (Status == NDIS_STATUS_SUCCESS)
    {
        if (cbInfo <= InformationBufferLength)
        {
            *BytesWritten = cbInfo;
            if (cbInfo)
                NdisMoveMemory(InformationBuffer, pvInfo, cbInfo);
        }
        else
        {
            *BytesNeeded = cbInfo;
            Status = NDIS_STATUS_INVALID_LENGTH;
        }
    }


    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Oid (%s), Status (0x%x)\n", pNetFlt, nemuNetFltWinMpDumpOid(Oid), Status));

    return Status;
}

static NDIS_STATUS nemuNetFltWinMpSetInformation(IN NDIS_HANDLE MiniportAdapterContext,
        IN NDIS_OID Oid,
        IN PVOID InformationBuffer,
        IN ULONG InformationBufferLength,
        OUT PULONG BytesRead,
        OUT PULONG BytesNeeded)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS) MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p), Oid (%s)\n", pNetFlt, nemuNetFltWinMpDumpOid(Oid)));

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_802_3_MULTICAST_LIST:
            *BytesRead = InformationBufferLength;
            if (InformationBufferLength % NEMUNETADP_ETH_ADDRESS_LENGTH)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            if (InformationBufferLength > (NEMUNETADP_MAX_MCAST_LIST * NEMUNETADP_ETH_ADDRESS_LENGTH))
            {
                Status = NDIS_STATUS_MULTICAST_FULL;
                *BytesNeeded = NEMUNETADP_MAX_MCAST_LIST * NEMUNETADP_ETH_ADDRESS_LENGTH;
                break;
            }
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InformationBufferLength != sizeof (ULONG))
            {
                *BytesNeeded = sizeof (ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            *BytesRead = InformationBufferLength;

            break;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (InformationBufferLength != sizeof (ULONG)){
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            break;

        case OID_PNP_SET_POWER:
            if (InformationBufferLength >= sizeof(NDIS_DEVICE_POWER_STATE))
            {
                NDIS_DEVICE_POWER_STATE *penmState = (NDIS_DEVICE_POWER_STATE*)InformationBuffer;
                Status = nemuNetFltWinMpHandlePowerState(pNetFlt, *penmState);
            }
            else
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
            }

            if (Status == NDIS_STATUS_SUCCESS)
            {
                *BytesRead = sizeof (NDIS_DEVICE_POWER_STATE);
                *BytesNeeded = 0;
            }
            else
            {
                *BytesRead = 0;
                *BytesNeeded = sizeof (NDIS_DEVICE_POWER_STATE);
            }
            break;

        default:
            Status = NDIS_STATUS_INVALID_OID;
            break;
    }

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Oid (%s), Status (0x%x)\n", pNetFlt, nemuNetFltWinMpDumpOid(Oid), Status));

    return Status;
}

#endif

#define NEMUNETFLTDUMP_STRCASE(_t) \
        case _t: return #_t;
#define NEMUNETFLTDUMP_STRCASE_UNKNOWN() \
        default: /*Assert(0);*/ return "Unknown";

static const char* nemuNetFltWinMpDumpOid(ULONG oid)
{
    switch (oid)
    {
        NEMUNETFLTDUMP_STRCASE(OID_GEN_SUPPORTED_LIST)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_HARDWARE_STATUS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MEDIA_SUPPORTED)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MEDIA_IN_USE)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MAXIMUM_LOOKAHEAD)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MAXIMUM_FRAME_SIZE)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_LINK_SPEED)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_TRANSMIT_BUFFER_SPACE)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_RECEIVE_BUFFER_SPACE)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_TRANSMIT_BLOCK_SIZE)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_RECEIVE_BLOCK_SIZE)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_VENDOR_ID)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_VENDOR_DESCRIPTION)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_CURRENT_PACKET_FILTER)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_CURRENT_LOOKAHEAD)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_DRIVER_VERSION)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MAXIMUM_TOTAL_SIZE)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_PROTOCOL_OPTIONS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MAC_OPTIONS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MEDIA_CONNECT_STATUS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MAXIMUM_SEND_PACKETS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_VENDOR_DRIVER_VERSION)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_SUPPORTED_GUIDS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_NETWORK_LAYER_ADDRESSES)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_TRANSPORT_HEADER_OFFSET)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MACHINE_NAME)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_RNDIS_CONFIG_PARAMETER)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_VLAN_ID)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MEDIA_CAPABILITIES)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_PHYSICAL_MEDIUM)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_XMIT_OK)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_RCV_OK)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_XMIT_ERROR)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_RCV_ERROR)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_RCV_NO_BUFFER)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_DIRECTED_BYTES_XMIT)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_DIRECTED_FRAMES_XMIT)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MULTICAST_BYTES_XMIT)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MULTICAST_FRAMES_XMIT)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_BROADCAST_BYTES_XMIT)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_BROADCAST_FRAMES_XMIT)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_DIRECTED_BYTES_RCV)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_DIRECTED_FRAMES_RCV)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MULTICAST_BYTES_RCV)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MULTICAST_FRAMES_RCV)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_BROADCAST_BYTES_RCV)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_BROADCAST_FRAMES_RCV)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_RCV_CRC_ERROR)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_TRANSMIT_QUEUE_LENGTH)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_GET_TIME_CAPS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_GET_NETCARD_TIME)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_NETCARD_LOAD)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_DEVICE_PROFILE)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_INIT_TIME_MS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_RESET_COUNTS)
        NEMUNETFLTDUMP_STRCASE(OID_GEN_MEDIA_SENSE_COUNTS)
        NEMUNETFLTDUMP_STRCASE(OID_PNP_CAPABILITIES)
        NEMUNETFLTDUMP_STRCASE(OID_PNP_SET_POWER)
        NEMUNETFLTDUMP_STRCASE(OID_PNP_QUERY_POWER)
        NEMUNETFLTDUMP_STRCASE(OID_PNP_ADD_WAKE_UP_PATTERN)
        NEMUNETFLTDUMP_STRCASE(OID_PNP_REMOVE_WAKE_UP_PATTERN)
        NEMUNETFLTDUMP_STRCASE(OID_PNP_ENABLE_WAKE_UP)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_PERMANENT_ADDRESS)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_CURRENT_ADDRESS)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_MULTICAST_LIST)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_MAXIMUM_LIST_SIZE)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_MAC_OPTIONS)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_RCV_ERROR_ALIGNMENT)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_XMIT_ONE_COLLISION)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_XMIT_MORE_COLLISIONS)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_XMIT_DEFERRED)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_XMIT_MAX_COLLISIONS)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_RCV_OVERRUN)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_XMIT_UNDERRUN)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_XMIT_HEARTBEAT_FAILURE)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_XMIT_TIMES_CRS_LOST)
        NEMUNETFLTDUMP_STRCASE(OID_802_3_XMIT_LATE_COLLISIONS)
        NEMUNETFLTDUMP_STRCASE(OID_TCP_TASK_OFFLOAD)
        NEMUNETFLTDUMP_STRCASE(OID_TCP_TASK_IPSEC_ADD_SA)
        NEMUNETFLTDUMP_STRCASE(OID_TCP_TASK_IPSEC_DELETE_SA)
        NEMUNETFLTDUMP_STRCASE(OID_TCP_SAN_SUPPORT)
        NEMUNETFLTDUMP_STRCASE(OID_TCP_TASK_IPSEC_ADD_UDPESP_SA)
        NEMUNETFLTDUMP_STRCASE(OID_TCP_TASK_IPSEC_DELETE_UDPESP_SA)
        NEMUNETFLTDUMP_STRCASE_UNKNOWN()
    }
}

DECLHIDDEN(VOID) nemuNetFltWinMpReturnPacket(IN NDIS_HANDLE hMiniportAdapterContext, IN PNDIS_PACKET pPacket)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hMiniportAdapterContext;
    PNEMUNETFLT_PKTRSVD_MP pInfo = (PNEMUNETFLT_PKTRSVD_MP)pPacket->MiniportReserved;
    PNDIS_PACKET pOrigPacket = pInfo->pOrigPacket;
    PVOID pBufToFree = pInfo->pBufToFree;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));

    if (pOrigPacket)
    {
        /* the packet was sent from underlying miniport */
        NdisFreePacket(pPacket);
        NdisReturnPackets(&pOrigPacket, 1);
    }
    else
    {
        /* the packet was sent from IntNet or it is a packet we allocated on PtReceive for TransferData processing */
        nemuNetFltWinFreeSGNdisPacket(pPacket, !pBufToFree /* bFreeMem */);
    }

    if (pBufToFree)
    {
        nemuNetFltWinMemFree(pBufToFree);
    }

    nemuNetFltWinDereferenceWinIf(pNetFlt);

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));
}

static NDIS_STATUS nemuNetFltWinMpTransferData(OUT PNDIS_PACKET Packet,
        OUT PUINT BytesTransferred,
        IN NDIS_HANDLE hContext,
        IN NDIS_HANDLE MiniportReceiveContext,
        IN UINT ByteOffset,
        IN UINT BytesToTransfer)
{
#ifndef NEMUNETADP
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hContext;
    NDIS_STATUS Status;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));

    if (nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.PtState) != NdisDeviceStateD0
            || nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.MpState) != NdisDeviceStateD0)
    {
        LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Status (0x%x)\n", pNetFlt, NDIS_STATUS_FAILURE));
        return NDIS_STATUS_FAILURE;
    }

    NdisTransferData(&Status, pNetFlt->u.s.WinIf.hBinding, MiniportReceiveContext,
            ByteOffset, BytesToTransfer, Packet, BytesTransferred);

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Status (0x%x)\n", pNetFlt, Status));
    return Status;
#else
    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p)\n", hContext));
    /* should never be here */
    Assert(0);
    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Status (0x%x)\n", hContext, NDIS_STATUS_FAILURE));
    return NDIS_STATUS_FAILURE;
#endif
}

static void nemuNetFltWinMpHalt(IN NDIS_HANDLE hContext)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hContext;
    NDIS_STATUS Status;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));

#ifndef NEMUNETADP
    if (nemuNetFltWinGetWinIfState(pNetFlt) == kNemuWinIfState_Disconnecting)
    {
        Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitializing);
        nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitializing);

        nemuNetFltWinPtCloseInterface(pNetFlt, &Status);

        Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.PtState) == kNemuNetDevOpState_Deinitializing);
        nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.PtState, kNemuNetDevOpState_Deinitialized);
        nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
    }
    else
#endif
    {
        /* we're NOT called from protocolUnbinAdapter, perform a full disconnect */
        Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Initialized);
#ifndef NEMUNETADP
        AssertBreakpoint();
#endif
        Status = nemuNetFltWinDetachFromInterface(pNetFlt, false);
        Assert(Status == NDIS_STATUS_SUCCESS);
    }

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));
}

/**
 * register the miniport edge
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpRegister(PNEMUNETFLTGLOBALS_MP pGlobalsMp, PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPathStr)
{
    NDIS_MINIPORT_CHARACTERISTICS MpChars;

    NdisMInitializeWrapper(&pGlobalsMp->hNdisWrapper, pDriverObject, pRegistryPathStr, NULL);

    NdisZeroMemory(&MpChars, sizeof (MpChars));

    MpChars.MajorNdisVersion = NEMUNETFLT_VERSION_MP_NDIS_MAJOR;
    MpChars.MinorNdisVersion = NEMUNETFLT_VERSION_MP_NDIS_MINOR;

    MpChars.HaltHandler = nemuNetFltWinMpHalt;
    MpChars.InitializeHandler = nemuNetFltWinMpInitialize;
    MpChars.QueryInformationHandler = nemuNetFltWinMpQueryInformation;
    MpChars.SetInformationHandler = nemuNetFltWinMpSetInformation;
    MpChars.TransferDataHandler = nemuNetFltWinMpTransferData;
    MpChars.ReturnPacketHandler = nemuNetFltWinMpReturnPacket;
    MpChars.SendPacketsHandler = nemuNetFltWinMpSendPackets;

#ifndef NEMUNETADP
    NDIS_STATUS Status = NdisIMRegisterLayeredMiniport(pGlobalsMp->hNdisWrapper, &MpChars, sizeof (MpChars), &pGlobalsMp->hMiniport);
#else
    NDIS_STATUS Status = NdisMRegisterMiniport(pGlobalsMp->hNdisWrapper, &MpChars, sizeof (MpChars));
#endif
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        NdisMRegisterUnloadHandler(pGlobalsMp->hNdisWrapper, nemuNetFltWinUnload);
    }

    return Status;
}

/**
 * deregister the miniport edge
 */
DECLHIDDEN(VOID) nemuNetFltWinMpDeregister(PNEMUNETFLTGLOBALS_MP pGlobalsMp)
{
#ifndef NEMUNETADP
    NdisIMDeregisterLayeredMiniport(pGlobalsMp->hMiniport);
#endif
    NdisTerminateWrapper(pGlobalsMp->hNdisWrapper, NULL);

    NdisZeroMemory(pGlobalsMp, sizeof (*pGlobalsMp));
}

#ifndef NEMUNETADP

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinMpInitializeDevideInstance(PNEMUNETFLTINS pThis)
{
    NDIS_STATUS Status;
    Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
    nemuNetFltWinSetOpState(&pThis->u.s.WinIf.MpState, kNemuNetDevOpState_Initializing);

    Status = NdisIMInitializeDeviceInstanceEx(g_NemuNetFltGlobalsWin.Mp.hMiniport,
                                       &pThis->u.s.WinIf.MpDeviceName,
                                       pThis);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        if (pThis->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS)
        {
            return NDIS_STATUS_SUCCESS;
        }
        AssertBreakpoint();
        nemuNetFltWinMpDeInitializeDeviceInstance(pThis, &Status);
        Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
        nemuNetFltWinSetOpState(&pThis->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
        return pThis->u.s.WinIf.OpenCloseStatus;
    }

    Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
    nemuNetFltWinSetOpState(&pThis->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);

    return Status;
}

DECLHIDDEN(bool) nemuNetFltWinMpDeInitializeDeviceInstance(PNEMUNETFLTINS pThis, PNDIS_STATUS pStatus)
{
    NDIS_STATUS Status;

    if (nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Initializing)
    {
        Status = NdisIMCancelInitializeDeviceInstance(g_NemuNetFltGlobalsWin.Mp.hMiniport, &pThis->u.s.WinIf.MpDeviceName);

        if (Status == NDIS_STATUS_SUCCESS)
        {
            /* we've canceled the initialization successfully */
            Assert(pThis->u.s.WinIf.hMiniport == NULL);
            Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
            nemuNetFltWinSetOpState(&pThis->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
        }
        else
        {
            NdisWaitEvent(&pThis->u.s.WinIf.MpInitCompleteEvent, 0);
        }
    }

    Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Initialized
            || nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
    if (nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Initialized)
    {
        nemuNetFltWinSetOpState(&pThis->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitializing);

        Status = NdisIMDeInitializeDeviceInstance(pThis->u.s.WinIf.hMiniport);

        nemuNetFltWinSetOpState(&pThis->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Status = NDIS_STATUS_FAILURE;
        }

        *pStatus = Status;
        return true;
    }

    Assert(nemuNetFltWinGetOpState(&pThis->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
    nemuNetFltWinSetOpState(&pThis->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);

    *pStatus = Status;
    return false;
}
#endif
