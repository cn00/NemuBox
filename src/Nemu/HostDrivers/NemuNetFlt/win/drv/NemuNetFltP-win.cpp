/* $Id: NemuNetFltP-win.cpp $ */
/** @file
 * NemuNetFltP-win.cpp - Bridged Networking Driver, Windows Specific Code.
 * Protocol edge
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

#ifdef NEMUNETADP
# error "No protocol edge"
#endif

#define NEMUNETFLT_PT_STATUS_IS_FILTERED(_s) (\
       (_s) == NDIS_STATUS_MEDIA_CONNECT \
    || (_s) == NDIS_STATUS_MEDIA_DISCONNECT \
    )

/**
 * performs binding to the given adapter
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtDoBinding(PNEMUNETFLTINS pThis, PNDIS_STRING pOurDeviceName, PNDIS_STRING pBindToDeviceName)
{
    Assert(pThis->u.s.WinIf.PtState.PowerState == NdisDeviceStateD3);
    Assert(pThis->u.s.WinIf.PtState.OpState == kNemuNetDevOpState_Deinitialized);
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    nemuNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kNemuNetDevOpState_Initializing);

    NDIS_STATUS Status = nemuNetFltWinCopyString(&pThis->u.s.WinIf.MpDeviceName, pOurDeviceName);
    Assert (Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        nemuNetFltWinSetPowerState(&pThis->u.s.WinIf.PtState, NdisDeviceStateD0);
        pThis->u.s.WinIf.OpenCloseStatus = NDIS_STATUS_SUCCESS;

        UINT iMedium;
        NDIS_STATUS TmpStatus;
        NDIS_MEDIUM aenmNdisMedium[] =
        {
                /* Ethernet */
                NdisMedium802_3,
                /* Wan */
                NdisMediumWan
        };

        NdisResetEvent(&pThis->u.s.WinIf.OpenCloseEvent);

        NdisOpenAdapter(&Status, &TmpStatus, &pThis->u.s.WinIf.hBinding, &iMedium,
                aenmNdisMedium, RT_ELEMENTS(aenmNdisMedium),
                g_NemuNetFltGlobalsWin.Pt.hProtocol,
                pThis,
                pBindToDeviceName,
                0, /* IN UINT OpenOptions, (reserved, should be NULL) */
                NULL /* IN PSTRING AddressingInformation  OPTIONAL */
                );
        Assert(Status == NDIS_STATUS_PENDING || Status == STATUS_SUCCESS);
        if (Status == NDIS_STATUS_PENDING)
        {
            NdisWaitEvent(&pThis->u.s.WinIf.OpenCloseEvent, 0);
            Status = pThis->u.s.WinIf.OpenCloseStatus;
        }

        Assert(Status == NDIS_STATUS_SUCCESS);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            Assert(pThis->u.s.WinIf.hBinding);
            pThis->u.s.WinIf.enmMedium = aenmNdisMedium[iMedium];
            nemuNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kNemuNetDevOpState_Initialized);

            Status = nemuNetFltWinMpInitializeDevideInstance(pThis);
            Assert(Status == NDIS_STATUS_SUCCESS);
            if (Status == NDIS_STATUS_SUCCESS)
            {
                return NDIS_STATUS_SUCCESS;
            }
            else
            {
                LogRel((__FUNCTION__": nemuNetFltWinMpInitializeDevideInstance failed, Status 0x%x\n", Status));
            }

            nemuNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kNemuNetDevOpState_Deinitializing);
            nemuNetFltWinPtCloseInterface(pThis, &TmpStatus);
            Assert(TmpStatus == NDIS_STATUS_SUCCESS);
            nemuNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kNemuNetDevOpState_Deinitialized);
        }
        else
        {
            LogRel((__FUNCTION__"NdisOpenAdapter failed, Status (0x%x)", Status));
        }

        nemuNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kNemuNetDevOpState_Deinitialized);
        pThis->u.s.WinIf.hBinding = NULL;
    }

    return Status;
}

static VOID nemuNetFltWinPtBindAdapter(OUT PNDIS_STATUS pStatus,
        IN NDIS_HANDLE hBindContext,
        IN PNDIS_STRING pDeviceNameStr,
        IN PVOID pvSystemSpecific1,
        IN PVOID pvSystemSpecific2)
{
    LogFlow(("==>"__FUNCTION__"\n"));

    NDIS_STATUS Status;
    NDIS_HANDLE hConfig = NULL;

    NdisOpenProtocolConfiguration(&Status, &hConfig, (PNDIS_STRING)pvSystemSpecific1);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        PNDIS_CONFIGURATION_PARAMETER pParam;
        NDIS_STRING UppedBindStr = NDIS_STRING_CONST("UpperBindings");
        NdisReadConfiguration(&Status, &pParam, hConfig, &UppedBindStr, NdisParameterString);
        Assert(Status == NDIS_STATUS_SUCCESS);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            PNEMUNETFLTINS pNetFlt;
            Status = nemuNetFltWinPtInitBind(&pNetFlt, &pParam->ParameterData.StringData, pDeviceNameStr);
            Assert(Status == NDIS_STATUS_SUCCESS);
        }

        NdisCloseConfiguration(hConfig);
    }

    *pStatus = Status;

    LogFlow(("<=="__FUNCTION__": Status 0x%x\n", Status));
}

static VOID nemuNetFltWinPtOpenAdapterComplete(IN NDIS_HANDLE hProtocolBindingContext, IN NDIS_STATUS Status, IN NDIS_STATUS OpenErrorStatus)
{
    PNEMUNETFLTINS pNetFlt =(PNEMUNETFLTINS)hProtocolBindingContext;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p), Status (0x%x), OpenErrorStatus(0x%x)\n", pNetFlt, Status, OpenErrorStatus));
    Assert(pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS)
    {
        pNetFlt->u.s.WinIf.OpenCloseStatus = Status;
        Assert(Status == NDIS_STATUS_SUCCESS);
        if (Status != NDIS_STATUS_SUCCESS)
            LogRel((__FUNCTION__" : Open Complete status is 0x%x", Status));
    }
    else
        LogRel((__FUNCTION__" : Adapter maintained status is 0x%x", pNetFlt->u.s.WinIf.OpenCloseStatus));
    NdisSetEvent(&pNetFlt->u.s.WinIf.OpenCloseEvent);
    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), Status (0x%x), OpenErrorStatus(0x%x)\n", pNetFlt, Status, OpenErrorStatus));
}

static void nemuNetFltWinPtRequestsWaitComplete(PNEMUNETFLTINS pNetFlt)
{
    /* wait for request to complete */
    while (nemuNetFltWinAtomicUoReadWinState(pNetFlt->u.s.WinIf.StateFlags).fRequestInfo == NEMUNDISREQUEST_INPROGRESS)
    {
        nemuNetFltWinSleep(2);
    }

    /*
     * If the below miniport is going to low power state, complete the queued request
     */
    RTSpinlockAcquire(pNetFlt->hSpinlock);
    if (pNetFlt->u.s.WinIf.StateFlags.fRequestInfo & NEMUNDISREQUEST_QUEUED)
    {
        /* mark the request as InProgress before posting it to RequestComplete */
        pNetFlt->u.s.WinIf.StateFlags.fRequestInfo = NEMUNDISREQUEST_INPROGRESS;
        RTSpinlockRelease(pNetFlt->hSpinlock);
        nemuNetFltWinPtRequestComplete(pNetFlt, &pNetFlt->u.s.WinIf.PassDownRequest, NDIS_STATUS_FAILURE);
    }
    else
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
    }
}

DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtDoUnbinding(PNEMUNETFLTINS pNetFlt, bool bOnUnbind)
{
    NDIS_STATUS Status;
    uint64_t NanoTS = RTTimeSystemNanoTS();
    int cPPUsage;

    LogFlow(("==>"__FUNCTION__": pNetFlt 0x%p\n", pNetFlt));

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.PtState) == kNemuNetDevOpState_Initialized);

    RTSpinlockAcquire(pNetFlt->hSpinlock);

    ASMAtomicUoWriteBool(&pNetFlt->fDisconnectedFromHost, true);
    ASMAtomicUoWriteBool(&pNetFlt->fRediscoveryPending, false);
    ASMAtomicUoWriteU64(&pNetFlt->NanoTSLastRediscovery, NanoTS);

    nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.PtState, kNemuNetDevOpState_Deinitializing);
    if (!bOnUnbind)
    {
        nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitializing);
    }

    RTSpinlockRelease(pNetFlt->hSpinlock);

    nemuNetFltWinPtRequestsWaitComplete(pNetFlt);

    nemuNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.MpState);
    nemuNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.PtState);

    /* check packet pool is empty */
    cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hSendPacketPool);
    Assert(cPPUsage == 0);
    cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hRecvPacketPool);
    Assert(cPPUsage == 0);
    /* for debugging only, ignore the err in release */
    NOREF(cPPUsage);

    if (!bOnUnbind || !nemuNetFltWinMpDeInitializeDeviceInstance(pNetFlt, &Status))
    {
        nemuNetFltWinPtCloseInterface(pNetFlt, &Status);
        nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.PtState, kNemuNetDevOpState_Deinitialized);

        if (!bOnUnbind)
        {
            Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitializing);
            nemuNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kNemuNetDevOpState_Deinitialized);
        }
        else
        {
            Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
        }
    }
    else
    {
        Assert(nemuNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kNemuNetDevOpState_Deinitialized);
    }

    LogFlow(("<=="__FUNCTION__": pNetFlt 0x%p\n", pNetFlt));

    return Status;
}

static VOID nemuNetFltWinPtUnbindAdapter(OUT PNDIS_STATUS pStatus,
        IN NDIS_HANDLE hContext,
        IN NDIS_HANDLE hUnbindContext)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hContext;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));

    *pStatus = nemuNetFltWinDetachFromInterface(pNetFlt, true);
    Assert(*pStatus == NDIS_STATUS_SUCCESS);

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p)\n", pNetFlt));
}

static VOID nemuNetFltWinPtUnloadProtocol()
{
    LogFlow(("==>"__FUNCTION__"\n"));
    NDIS_STATUS Status = nemuNetFltWinPtDeregister(&g_NemuNetFltGlobalsWin.Pt);
    Assert(Status == NDIS_STATUS_SUCCESS);
    LogFlow(("<=="__FUNCTION__": PtDeregister Status (0x%x)\n", Status));
}


static VOID nemuNetFltWinPtCloseAdapterComplete(IN NDIS_HANDLE ProtocolBindingContext, IN NDIS_STATUS Status)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)ProtocolBindingContext;

    LogFlow(("==>"__FUNCTION__" : pNetFlt (0x%p), Status (0x%x)\n", pNetFlt, Status));
    Assert(pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS);
    Assert(Status == NDIS_STATUS_SUCCESS);
    Assert(pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS);
    if (pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS)
    {
        pNetFlt->u.s.WinIf.OpenCloseStatus = Status;
    }
    NdisSetEvent(&pNetFlt->u.s.WinIf.OpenCloseEvent);
    LogFlow(("<=="__FUNCTION__" : pNetFlt (0x%p), Status (0x%x)\n", pNetFlt, Status));
}

static VOID nemuNetFltWinPtResetComplete(IN NDIS_HANDLE hProtocolBindingContext, IN NDIS_STATUS Status)
{
    LogFlow(("==>"__FUNCTION__" : pNetFlt 0x%p, Status 0x%x\n", hProtocolBindingContext, Status));
    /*
     * should never be here
     */
    Assert(0);
    LogFlow(("<=="__FUNCTION__" : pNetFlt 0x%p, Status 0x%x\n", hProtocolBindingContext, Status));
}

static NDIS_STATUS nemuNetFltWinPtHandleQueryInfoComplete(PNEMUNETFLTINS pNetFlt, NDIS_STATUS Status)
{
    PNDIS_REQUEST pRequest = &pNetFlt->u.s.WinIf.PassDownRequest;

    switch (pRequest->DATA.QUERY_INFORMATION.Oid)
    {
        case OID_PNP_CAPABILITIES:
        {
            if (Status == NDIS_STATUS_SUCCESS)
            {
                if (pRequest->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof (NDIS_PNP_CAPABILITIES))
                {
                    PNDIS_PNP_CAPABILITIES pPnPCaps = (PNDIS_PNP_CAPABILITIES)(pRequest->DATA.QUERY_INFORMATION.InformationBuffer);
                    PNDIS_PM_WAKE_UP_CAPABILITIES pPmWuCaps = &pPnPCaps->WakeUpCapabilities;
                    pPmWuCaps->MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
                    pPmWuCaps->MinPatternWakeUp = NdisDeviceStateUnspecified;
                    pPmWuCaps->MinLinkChangeWakeUp = NdisDeviceStateUnspecified;
                    *pNetFlt->u.s.WinIf.pcPDRBytesRW = sizeof (NDIS_PNP_CAPABILITIES);
                    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = 0;
                    Status = NDIS_STATUS_SUCCESS;
                }
                else
                {
                    Assert(0);
                    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof(NDIS_PNP_CAPABILITIES);
                    Status = NDIS_STATUS_RESOURCES;
                }
            }
            break;
        }

        case OID_GEN_MAC_OPTIONS:
        {
            if (Status == NDIS_STATUS_SUCCESS)
            {
                if (pRequest->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof (ULONG))
                {
                    pNetFlt->u.s.WinIf.fMacOptions = *(PULONG)pRequest->DATA.QUERY_INFORMATION.InformationBuffer;
#ifndef NEMU_LOOPBACK_USEFLAGS
                    /* clearing this flag tells ndis we'll handle loopback ourselves
                     * the ndis layer or nic driver below us would loopback packets as necessary */
                    *(PULONG)pRequest->DATA.QUERY_INFORMATION.InformationBuffer &= ~NDIS_MAC_OPTION_NO_LOOPBACK;
#else
                    /* we have to catch loopbacks from the underlying driver, so no duplications will occur,
                     * just indicate NDIS to handle loopbacks for the packets coming from the protocol */
                    *(PULONG)pRequest->DATA.QUERY_INFORMATION.InformationBuffer |= NDIS_MAC_OPTION_NO_LOOPBACK;
#endif
                }
                else
                {
                    Assert(0);
                    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof (ULONG);
                    Status = NDIS_STATUS_RESOURCES;
                }
            }
            break;
        }

        case OID_GEN_CURRENT_PACKET_FILTER:
        {
            if (NEMUNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt))
            {
                /* we're here _ONLY_ in the passthru mode */
                Assert(pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter && !pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt);
                if (pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter && !pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt)
                {
                    Assert(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE);
                    nemuNetFltWinDereferenceModePassThru(pNetFlt);
                    nemuNetFltWinDereferenceWinIf(pNetFlt);
                }

                if (Status == NDIS_STATUS_SUCCESS)
                {
                    if (pRequest->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof (ULONG))
                    {
                        /* the filter request is issued below only in case netflt is not active,
                         * simply update the cache here */
                        /* cache the filter used by upper protocols */
                        pNetFlt->u.s.WinIf.fUpperProtocolSetFilter = *(PULONG)pRequest->DATA.QUERY_INFORMATION.InformationBuffer;
                        pNetFlt->u.s.WinIf.StateFlags.fUpperProtSetFilterInitialized = TRUE;
                    }
                    else
                    {
                        Assert(0);
                        *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof (ULONG);
                        Status = NDIS_STATUS_RESOURCES;
                    }
                }
            }
            break;
        }

        default:
            Assert(pRequest->DATA.QUERY_INFORMATION.Oid != OID_PNP_QUERY_POWER);
            break;
    }

    *pNetFlt->u.s.WinIf.pcPDRBytesRW = pRequest->DATA.QUERY_INFORMATION.BytesWritten;
    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = pRequest->DATA.QUERY_INFORMATION.BytesNeeded;

    return Status;
}

static NDIS_STATUS nemuNetFltWinPtHandleSetInfoComplete(PNEMUNETFLTINS pNetFlt, NDIS_STATUS Status)
{
    PNDIS_REQUEST pRequest = &pNetFlt->u.s.WinIf.PassDownRequest;

    switch (pRequest->DATA.SET_INFORMATION.Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
        {
            if (NEMUNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt))
            {
                Assert(Status == NDIS_STATUS_SUCCESS);
                if (pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter)
                {
                    if (pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt)
                    {
                        Assert(pNetFlt->enmTrunkState == INTNETTRUNKIFSTATE_ACTIVE);
                        pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt = 0;
                        if (Status == NDIS_STATUS_SUCCESS)
                        {
                            if (pRequest->DATA.SET_INFORMATION.InformationBufferLength >= sizeof (ULONG))
                            {
                                pNetFlt->u.s.WinIf.fOurSetFilter = *((PULONG)pRequest->DATA.SET_INFORMATION.InformationBuffer);
                                Assert(pNetFlt->u.s.WinIf.fOurSetFilter == NDIS_PACKET_TYPE_PROMISCUOUS);
                            }
                            else
                            {
                                Assert(0);
                                *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof (ULONG);
                                Status = NDIS_STATUS_RESOURCES;
                            }
                        }
                        nemuNetFltWinDereferenceNetFlt(pNetFlt);
                    }
                    else
                    {
                        Assert(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE);

                        if (Status == NDIS_STATUS_SUCCESS)
                        {
                            if (pRequest->DATA.SET_INFORMATION.InformationBufferLength >= sizeof (ULONG))
                            {
                                /* the request was issued when the netflt was not active, simply update the cache here */
                                pNetFlt->u.s.WinIf.fUpperProtocolSetFilter = *((PULONG)pRequest->DATA.SET_INFORMATION.InformationBuffer);
                                pNetFlt->u.s.WinIf.StateFlags.fUpperProtSetFilterInitialized = TRUE;
                            }
                            else
                            {
                                Assert(0);
                                *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof (ULONG);
                                Status = NDIS_STATUS_RESOURCES;
                            }
                        }
                        nemuNetFltWinDereferenceModePassThru(pNetFlt);
                    }

                    pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter = 0;
                    nemuNetFltWinDereferenceWinIf(pNetFlt);
                }
#ifdef DEBUG_misha
                else
                {
                    Assert(0);
                }
#endif
            }
            break;
        }

        default:
            Assert(pRequest->DATA.SET_INFORMATION.Oid != OID_PNP_SET_POWER);
            break;
    }

    *pNetFlt->u.s.WinIf.pcPDRBytesRW = pRequest->DATA.SET_INFORMATION.BytesRead;
    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = pRequest->DATA.SET_INFORMATION.BytesNeeded;

    return Status;
}

DECLHIDDEN(VOID) nemuNetFltWinPtRequestComplete(NDIS_HANDLE hContext, PNDIS_REQUEST pNdisRequest, NDIS_STATUS Status)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hContext;
    PNDIS_REQUEST pSynchRequest = pNetFlt->u.s.WinIf.pSynchRequest;
    NDIS_OID Oid = pNetFlt->u.s.WinIf.PassDownRequest.DATA.SET_INFORMATION.Oid;

    LogFlow(("==>"__FUNCTION__" : pNetFlt (0x%p), pNdisRequest (0x%p), Status (0x%x)\n", pNetFlt, pNdisRequest, Status));

    if (pSynchRequest == pNdisRequest)
    {
        /* asynchronous completion of our sync request */
        /*1.set the status */
        pNetFlt->u.s.WinIf.SynchCompletionStatus = Status;
        /* 2. set event */
        KeSetEvent(&pNetFlt->u.s.WinIf.hSynchCompletionEvent, 0, FALSE);
        /* 3. return; */

        LogFlow(("<=="__FUNCTION__" : pNetFlt (0x%p), pNdisRequest (0x%p), Status (0x%x)\n", pNetFlt, pNdisRequest, Status));
        return;
    }

    Assert(&pNetFlt->u.s.WinIf.PassDownRequest == pNdisRequest);
    Assert(pNetFlt->u.s.WinIf.StateFlags.fRequestInfo == NEMUNDISREQUEST_INPROGRESS);
    nemuNetFltWinMpRequestStateComplete(pNetFlt);

    switch (pNdisRequest->RequestType)
    {
      case NdisRequestQueryInformation:
          Status = nemuNetFltWinPtHandleQueryInfoComplete(pNetFlt, Status);
          NdisMQueryInformationComplete(pNetFlt->u.s.WinIf.hMiniport, Status);
          break;

      case NdisRequestSetInformation:
          Status = nemuNetFltWinPtHandleSetInfoComplete(pNetFlt, Status);
          NdisMSetInformationComplete(pNetFlt->u.s.WinIf.hMiniport, Status);
          break;

      default:
          Assert(0);
          break;
    }

    LogFlow(("<=="__FUNCTION__" : pNetFlt (0x%p), pNdisRequest (0x%p), Status (0x%x)\n", pNetFlt, pNdisRequest, Status));
}

static VOID nemuNetFltWinPtStatus(IN NDIS_HANDLE hProtocolBindingContext, IN NDIS_STATUS GeneralStatus, IN PVOID pvStatusBuffer, IN UINT cbStatusBuffer)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hProtocolBindingContext;

    LogFlow(("==>"__FUNCTION__" : pNetFlt (0x%p), GeneralStatus (0x%x)\n", pNetFlt, GeneralStatus));

    if (nemuNetFltWinReferenceWinIf(pNetFlt))
    {
        Assert(pNetFlt->u.s.WinIf.hMiniport);

        if (NEMUNETFLT_PT_STATUS_IS_FILTERED(GeneralStatus))
        {
            pNetFlt->u.s.WinIf.MpIndicatedMediaStatus = GeneralStatus;
        }
        NdisMIndicateStatus(pNetFlt->u.s.WinIf.hMiniport,
                            GeneralStatus,
                            pvStatusBuffer,
                            cbStatusBuffer);

        nemuNetFltWinDereferenceWinIf(pNetFlt);
    }
    else
    {
        if (pNetFlt->u.s.WinIf.hMiniport != NULL
                && NEMUNETFLT_PT_STATUS_IS_FILTERED(GeneralStatus)
           )
        {
            pNetFlt->u.s.WinIf.MpUnindicatedMediaStatus = GeneralStatus;
        }
    }

    LogFlow(("<=="__FUNCTION__" : pNetFlt (0x%p), GeneralStatus (0x%x)\n", pNetFlt, GeneralStatus));
}


static VOID nemuNetFltWinPtStatusComplete(IN NDIS_HANDLE hProtocolBindingContext)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hProtocolBindingContext;

    LogFlow(("==>"__FUNCTION__" : pNetFlt (0x%p)\n", pNetFlt));

    if (nemuNetFltWinReferenceWinIf(pNetFlt))
    {
        NdisMIndicateStatusComplete(pNetFlt->u.s.WinIf.hMiniport);

        nemuNetFltWinDereferenceWinIf(pNetFlt);
    }

    LogFlow(("<=="__FUNCTION__" : pNetFlt (0x%p)\n", pNetFlt));
}

static VOID nemuNetFltWinPtSendComplete(IN NDIS_HANDLE hProtocolBindingContext, IN PNDIS_PACKET pPacket, IN NDIS_STATUS Status)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hProtocolBindingContext;
    PNEMUNETFLT_PKTRSVD_PT pSendInfo = (PNEMUNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
    PNDIS_PACKET pOrigPacket = pSendInfo->pOrigPacket;
    PVOID pBufToFree = pSendInfo->pBufToFree;
    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p), pPacket (0x%p), Status (0x%x)\n", pNetFlt, pPacket, Status));

#if defined(DEBUG_NETFLT_PACKETS) || !defined(NEMU_LOOPBACK_USEFLAGS)
    /* @todo: for optimization we could check only for netflt-mode packets
     * do it for all for now */
     nemuNetFltWinLbRemoveSendPacket(pNetFlt, pPacket);
#endif

     if (pOrigPacket)
     {
         NdisIMCopySendCompletePerPacketInfo(pOrigPacket, pPacket);
         NdisFreePacket(pPacket);
         /* the ptk was posted from the upperlying protocol */
         NdisMSendComplete(pNetFlt->u.s.WinIf.hMiniport, pOrigPacket, Status);
     }
     else
     {
         /* if the pOrigPacket is zero - the ptk was originated by netFlt send/receive
          * need to free packet buffers */
         nemuNetFltWinFreeSGNdisPacket(pPacket, !pBufToFree);
     }

     if (pBufToFree)
     {
         nemuNetFltWinMemFree(pBufToFree);
     }

    nemuNetFltWinDereferenceWinIf(pNetFlt);

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), pPacket (0x%p), Status (0x%x)\n", pNetFlt, pPacket, Status));
}

/**
 * removes searches for the packet in the list and removes it if found
 * @return true if the packet was found and removed, false - otherwise
 */
static bool nemuNetFltWinRemovePacketFromList(PNEMUNETFLT_INTERLOCKED_SINGLE_LIST pList, PNDIS_PACKET pPacket)
{
    PNEMUNETFLT_PKTRSVD_TRANSFERDATA_PT pTDR = (PNEMUNETFLT_PKTRSVD_TRANSFERDATA_PT)pPacket->ProtocolReserved;
    return nemuNetFltWinInterlockedSearchListEntry(pList, &pTDR->ListEntry, true /* remove*/);
}

/**
 * puts the packet to the tail of the list
 */
static void nemuNetFltWinPutPacketToList(PNEMUNETFLT_INTERLOCKED_SINGLE_LIST pList, PNDIS_PACKET pPacket, PNDIS_BUFFER pOrigBuffer)
{
    PNEMUNETFLT_PKTRSVD_TRANSFERDATA_PT pTDR = (PNEMUNETFLT_PKTRSVD_TRANSFERDATA_PT)pPacket->ProtocolReserved;
    pTDR->pOrigBuffer = pOrigBuffer;
    nemuNetFltWinInterlockedPutTail(pList, &pTDR->ListEntry);
}

static bool nemuNetFltWinPtTransferDataCompleteActive(PNEMUNETFLTINS pNetFltIf, PNDIS_PACKET pPacket, NDIS_STATUS Status)
{
    PNDIS_BUFFER pBuffer;
    PNEMUNETFLT_PKTRSVD_TRANSFERDATA_PT pTDR;

    if (!nemuNetFltWinRemovePacketFromList(&pNetFltIf->u.s.WinIf.TransferDataList, pPacket))
        return false;

    pTDR = (PNEMUNETFLT_PKTRSVD_TRANSFERDATA_PT)pPacket->ProtocolReserved;
    Assert(pTDR);
    Assert(pTDR->pOrigBuffer);

    do
    {
        NdisUnchainBufferAtFront(pPacket, &pBuffer);

        Assert(pBuffer);

        NdisFreeBuffer(pBuffer);

        pBuffer = pTDR->pOrigBuffer;

        NdisChainBufferAtBack(pPacket, pBuffer);

        /* data transfer was initiated when the netFlt was active
         * the netFlt is still retained by us
         * 1. check if loopback
         * 2. enqueue packet
         * 3. release netFlt */

        if (Status == NDIS_STATUS_SUCCESS)
        {

#ifdef NEMU_LOOPBACK_USEFLAGS
            if (nemuNetFltWinIsLoopedBackPacket(pPacket))
            {
                /* should not be here */
                Assert(0);
            }
#else
            PNDIS_PACKET pLb = nemuNetFltWinLbSearchLoopBack(pNetFltIf, pPacket, false);
            if (pLb)
            {
#ifndef DEBUG_NETFLT_RECV_TRANSFERDATA
                /* should not be here */
                Assert(0);
#endif
                if (!nemuNetFltWinLbIsFromIntNet(pLb))
                {
                    /* the packet is not from int net, need to pass it up to the host */
                    NdisMIndicateReceivePacket(pNetFltIf->u.s.WinIf.hMiniport, &pPacket, 1);
                    /* dereference NetFlt, WinIf will be dereferenced on Packet return */
                    nemuNetFltWinDereferenceNetFlt(pNetFltIf);
                    break;
                }
            }
#endif
            else
            {
                /* 2. enqueue */
                /* use the same packet info to put the packet in the processing packet queue */
                PNEMUNETFLT_PKTRSVD_MP pRecvInfo = (PNEMUNETFLT_PKTRSVD_MP)pPacket->MiniportReserved;

                NEMUNETFLT_LBVERIFY(pNetFltIf, pPacket);

                pRecvInfo->pOrigPacket = NULL;
                pRecvInfo->pBufToFree = NULL;

                NdisGetPacketFlags(pPacket) = 0;
# ifdef NEMUNETFLT_NO_PACKET_QUEUE
                if (nemuNetFltWinPostIntnet(pNetFltIf, pPacket, 0))
                {
                    /* drop it */
                    nemuNetFltWinFreeSGNdisPacket(pPacket, true);
                    nemuNetFltWinDereferenceWinIf(pNetFltIf);
                }
                else
                {
                    NdisMIndicateReceivePacket(pNetFltIf->u.s.WinIf.hMiniport, &pPacket, 1);
                }
                nemuNetFltWinDereferenceNetFlt(pNetFltIf);
                break;
# else
                Status = nemuNetFltWinQuEnqueuePacket(pNetFltIf, pPacket, PACKET_MINE);
                if (Status == NDIS_STATUS_SUCCESS)
                {
                    break;
                }
                Assert(0);
# endif
            }
        }
        else
        {
            Assert(0);
        }
        /* we are here because of error either in data transfer or in enqueueing the packet */
        nemuNetFltWinFreeSGNdisPacket(pPacket, true);
        nemuNetFltWinDereferenceNetFlt(pNetFltIf);
        nemuNetFltWinDereferenceWinIf(pNetFltIf);
    } while (0);

    return true;
}

static VOID nemuNetFltWinPtTransferDataComplete(IN NDIS_HANDLE hProtocolBindingContext,
                    IN PNDIS_PACKET pPacket,
                    IN NDIS_STATUS Status,
                    IN UINT cbTransferred)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hProtocolBindingContext;
    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p), pPacket (0x%p), Status (0x%x), cbTransfered (%d)\n", pNetFlt, pPacket, Status, cbTransferred));
    if (!nemuNetFltWinPtTransferDataCompleteActive(pNetFlt, pPacket, Status))
    {
        if (pNetFlt->u.s.WinIf.hMiniport)
        {
            NdisMTransferDataComplete(pNetFlt->u.s.WinIf.hMiniport,
                                      pPacket,
                                      Status,
                                      cbTransferred);
        }

        nemuNetFltWinDereferenceWinIf(pNetFlt);
    }
    /* else - all processing is done with nemuNetFltWinPtTransferDataCompleteActive already */

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), pPacket (0x%p), Status (0x%x), cbTransfered (%d)\n", pNetFlt, pPacket, Status, cbTransferred));
}

static INT nemuNetFltWinRecvPacketPassThru(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket, BOOLEAN bForceIndicate)
{
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PNDIS_PACKET pMyPacket;
    NDIS_STATUS Status = nemuNetFltWinPrepareRecvPacket(pNetFlt, pPacket, &pMyPacket, true);
    /* the Status holds the current packet status it will be checked for NDIS_STATUS_RESOURCES later
     * (see below) */
    Assert(pMyPacket);
    if (pMyPacket)
    {
        NdisMIndicateReceivePacket(pNetFlt->u.s.WinIf.hMiniport, &pMyPacket, 1);
        if (Status == NDIS_STATUS_RESOURCES)
        {
            NdisDprFreePacket(pMyPacket);
            return 0;
        }

        return 1;
    }

    return 0;
}

/**
 * process the packet receive in a "passthru" mode
 */
static NDIS_STATUS nemuNetFltWinRecvPassThru(PNEMUNETFLTINS pNetFlt, PNDIS_PACKET pPacket)
{
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    NDIS_STATUS Status;
    PNDIS_PACKET pMyPacket;

    NdisDprAllocatePacket(&Status, &pMyPacket, pNetFlt->u.s.WinIf.hRecvPacketPool);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        nemuNetFltWinCopyPacketInfoOnRecv(pMyPacket, pPacket, true /* force NDIS_STATUS_RESOURCES */);
        Assert(NDIS_GET_PACKET_STATUS(pMyPacket) == NDIS_STATUS_RESOURCES);

        NdisMIndicateReceivePacket(pNetFlt->u.s.WinIf.hMiniport, &pMyPacket, 1);

        NdisDprFreePacket(pMyPacket);
    }
    return Status;
}

static VOID nemuNetFltWinRecvIndicatePassThru(PNEMUNETFLTINS pNetFlt, NDIS_HANDLE MacReceiveContext,
                PVOID pHeaderBuffer, UINT cbHeaderBuffer, PVOID pLookAheadBuffer, UINT cbLookAheadBuffer, UINT cbPacket)
{
    /* Note: we're using KeGetCurrentProcessorNumber, which is not entirely correct in case
    * we're running on 64bit win7+, which can handle > 64 CPUs, however since KeGetCurrentProcessorNumber
    * always returns the number < than the number of CPUs in the first group, we're guaranteed to have CPU index < 64
    * @todo: use KeGetCurrentProcessorNumberEx for Win7+ 64 and dynamically extended array */
    ULONG Proc = KeGetCurrentProcessorNumber();
    Assert(Proc < RT_ELEMENTS(pNetFlt->u.s.WinIf.abIndicateRxComplete));
    pNetFlt->u.s.WinIf.abIndicateRxComplete[Proc] = TRUE;
    switch (pNetFlt->u.s.WinIf.enmMedium)
    {
        case NdisMedium802_3:
        case NdisMediumWan:
            NdisMEthIndicateReceive(pNetFlt->u.s.WinIf.hMiniport,
                                         MacReceiveContext,
                                         (PCHAR)pHeaderBuffer,
                                         cbHeaderBuffer,
                                         pLookAheadBuffer,
                                         cbLookAheadBuffer,
                                         cbPacket);
            break;
        default:
            Assert(FALSE);
            break;
    }
}

/**
 * process the ProtocolReceive in an "active" mode
 *
 * @return NDIS_STATUS_SUCCESS - the packet is processed
 * NDIS_STATUS_PENDING - the packet is being processed, we are waiting for the ProtocolTransferDataComplete to be called
 * NDIS_STATUS_NOT_ACCEPTED - the packet is not needed - typically this is because this is a loopback packet
 * NDIS_STATUS_FAILURE - packet processing failed
 */
static NDIS_STATUS nemuNetFltWinPtReceiveActive(PNEMUNETFLTINS pNetFlt, NDIS_HANDLE MacReceiveContext, PVOID pHeaderBuffer, UINT cbHeaderBuffer,
                        PVOID pLookaheadBuffer, UINT cbLookaheadBuffer, UINT cbPacket)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    do
    {
        if (cbHeaderBuffer != NEMUNETFLT_PACKET_ETHEADER_SIZE)
        {
            Status = NDIS_STATUS_NOT_ACCEPTED;
            break;
        }

#ifndef DEBUG_NETFLT_RECV_TRANSFERDATA
        if (cbPacket == cbLookaheadBuffer)
        {
            PINTNETSG pSG;
            PUCHAR pRcvData;
#ifndef NEMU_LOOPBACK_USEFLAGS
            PNDIS_PACKET pLb;
#endif

            /* allocate SG buffer */
            Status = nemuNetFltWinAllocSG(cbPacket + cbHeaderBuffer, &pSG);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                Assert(0);
                break;
            }

            pRcvData = (PUCHAR)pSG->aSegs[0].pv;

            NdisMoveMappedMemory(pRcvData, pHeaderBuffer, cbHeaderBuffer);

            NdisCopyLookaheadData(pRcvData+cbHeaderBuffer,
                                                  pLookaheadBuffer,
                                                  cbLookaheadBuffer,
                                                  pNetFlt->u.s.WinIf.fMacOptions);
#ifndef NEMU_LOOPBACK_USEFLAGS
            pLb = nemuNetFltWinLbSearchLoopBackBySG(pNetFlt, pSG, false);
            if (pLb)
            {
#ifndef DEBUG_NETFLT_RECV_NOPACKET
                /* should not be here */
                Assert(0);
#endif
                if (!nemuNetFltWinLbIsFromIntNet(pLb))
                {
                    PNDIS_PACKET pMyPacket;
                    pMyPacket = nemuNetFltWinNdisPacketFromSG(pNetFlt, /* PNEMUNETFLTINS */
                        pSG, /* PINTNETSG */
                        pSG, /* PVOID pBufToFree */
                        false, /* bool bToWire */
                        false); /* bool bCopyMemory */
                    if (pMyPacket)
                    {
                        NdisMIndicateReceivePacket(pNetFlt->u.s.WinIf.hMiniport, &pMyPacket, 1);
                        /* dereference the NetFlt here & indicate SUCCESS, which would mean the caller would not do a dereference
                         * the WinIf dereference will be done on packet return */
                        nemuNetFltWinDereferenceNetFlt(pNetFlt);
                        Status = NDIS_STATUS_SUCCESS;
                    }
                    else
                    {
                        nemuNetFltWinMemFree(pSG);
                        Status = NDIS_STATUS_FAILURE;
                    }
                }
                else
                {
                    nemuNetFltWinMemFree(pSG);
                    Status = NDIS_STATUS_NOT_ACCEPTED;
                }
                break;
            }
#endif
            NEMUNETFLT_LBVERIFYSG(pNetFlt, pSG);

                /* enqueue SG */
# ifdef NEMUNETFLT_NO_PACKET_QUEUE
            if (nemuNetFltWinPostIntnet(pNetFlt, pSG, NEMUNETFLT_PACKET_SG))
            {
                /* drop it */
                nemuNetFltWinMemFree(pSG);
                nemuNetFltWinDereferenceWinIf(pNetFlt);
            }
            else
            {
                PNDIS_PACKET pMyPacket = nemuNetFltWinNdisPacketFromSG(pNetFlt, /* PNEMUNETFLTINS */
                        pSG, /* PINTNETSG */
                        pSG, /* PVOID pBufToFree */
                        false, /* bool bToWire */
                        false); /* bool bCopyMemory */
                Assert(pMyPacket);
                if (pMyPacket)
                {
                    NDIS_SET_PACKET_STATUS(pMyPacket, NDIS_STATUS_SUCCESS);

                    DBG_CHECK_PACKET_AND_SG(pMyPacket, pSG);

                    LogFlow(("non-ndis packet info, packet created (%p)\n", pMyPacket));
                    NdisMIndicateReceivePacket(pNetFlt->u.s.WinIf.hMiniport, &pMyPacket, 1);
                }
                else
                {
                    nemuNetFltWinDereferenceWinIf(pNetFlt);
                    Status = NDIS_STATUS_RESOURCES;
                }
            }
            nemuNetFltWinDereferenceNetFlt(pNetFlt);
# else
            Status = nemuNetFltWinQuEnqueuePacket(pNetFlt, pSG, PACKET_SG | PACKET_MINE);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                Assert(0);
                nemuNetFltWinMemFree(pSG);
                break;
            }
# endif
#endif
        }
        else
        {
            PNDIS_PACKET pPacket;
            PNDIS_BUFFER pTransferBuffer;
            PNDIS_BUFFER pOrigBuffer;
            PUCHAR pMemBuf;
            UINT cbBuf = cbPacket + cbHeaderBuffer;
            UINT cbTransferred;

            /* allocate NDIS Packet buffer */
            NdisAllocatePacket(&Status, &pPacket, pNetFlt->u.s.WinIf.hRecvPacketPool);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                Assert(0);
                break;
            }

            NEMUNETFLT_OOB_INIT(pPacket);

#ifdef NEMU_LOOPBACK_USEFLAGS
            /* set "don't loopback" flags */
            NdisGetPacketFlags(pPacket) = g_NemuNetFltGlobalsWin.fPacketDontLoopBack;
#else
            NdisGetPacketFlags(pPacket) =  0;
#endif

            Status = nemuNetFltWinMemAlloc((PVOID*)(&pMemBuf), cbBuf);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                Assert(0);
                NdisFreePacket(pPacket);
                break;
            }
            NdisAllocateBuffer(&Status, &pTransferBuffer, pNetFlt->u.s.WinIf.hRecvBufferPool, pMemBuf + cbHeaderBuffer, cbPacket);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                Assert(0);
                Status = NDIS_STATUS_FAILURE;
                NdisFreePacket(pPacket);
                nemuNetFltWinMemFree(pMemBuf);
                break;
            }

            NdisAllocateBuffer(&Status, &pOrigBuffer, pNetFlt->u.s.WinIf.hRecvBufferPool, pMemBuf, cbBuf);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                Assert(0);
                Status = NDIS_STATUS_FAILURE;
                NdisFreeBuffer(pTransferBuffer);
                NdisFreePacket(pPacket);
                nemuNetFltWinMemFree(pMemBuf);
                break;
            }

            NdisChainBufferAtBack(pPacket, pTransferBuffer);

            NdisMoveMappedMemory(pMemBuf, pHeaderBuffer, cbHeaderBuffer);

            nemuNetFltWinPutPacketToList(&pNetFlt->u.s.WinIf.TransferDataList, pPacket, pOrigBuffer);

#ifdef DEBUG_NETFLT_RECV_TRANSFERDATA
            if (cbPacket == cbLookaheadBuffer)
            {
                NdisCopyLookaheadData(pMemBuf+cbHeaderBuffer,
                                                  pLookaheadBuffer,
                                                  cbLookaheadBuffer,
                                                  pNetFlt->u.s.WinIf.fMacOptions);
            }
            else
#endif
            {
                Assert(cbPacket > cbLookaheadBuffer);

                NdisTransferData(&Status, pNetFlt->u.s.WinIf.hBinding, MacReceiveContext,
                        0,  /* ByteOffset */
                        cbPacket, pPacket, &cbTransferred);
            }

            if (Status != NDIS_STATUS_PENDING)
            {
                nemuNetFltWinPtTransferDataComplete(pNetFlt, pPacket, Status, cbTransferred);
            }
        }
    } while (0);

    return Status;
}

static NDIS_STATUS nemuNetFltWinPtReceive(IN NDIS_HANDLE hProtocolBindingContext,
                        IN NDIS_HANDLE MacReceiveContext,
                        IN PVOID pHeaderBuffer,
                        IN UINT cbHeaderBuffer,
                        IN PVOID pLookAheadBuffer,
                        IN UINT cbLookAheadBuffer,
                        IN UINT cbPacket)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hProtocolBindingContext;
    PNDIS_PACKET pPacket = NULL;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    bool bNetFltActive;
    bool fWinIfActive = nemuNetFltWinReferenceWinIfNetFlt(pNetFlt, &bNetFltActive);
    const bool bPassThruActive = !bNetFltActive;

    LogFlow(("==>"__FUNCTION__" : pNetFlt (0x%p)\n", pNetFlt));

    if (fWinIfActive)
    {
        do
        {
#ifndef DEBUG_NETFLT_RECV_NOPACKET
            pPacket = NdisGetReceivedPacket(pNetFlt->u.s.WinIf.hBinding, MacReceiveContext);
            if (pPacket)
            {
# ifndef NEMU_LOOPBACK_USEFLAGS
                PNDIS_PACKET pLb = NULL;
# else
                if (nemuNetFltWinIsLoopedBackPacket(pPacket))
                {
                    Assert(0);
                    /* nothing else to do here, just return the packet */
                    //NdisReturnPackets(&pPacket, 1);
                    Status = NDIS_STATUS_NOT_ACCEPTED;
                    break;
                }

                NEMUNETFLT_LBVERIFY(pNetFlt, pPacket);
# endif

                if (bNetFltActive)
                {
# ifndef NEMU_LOOPBACK_USEFLAGS
                    pLb = nemuNetFltWinLbSearchLoopBack(pNetFlt, pPacket, false);
                    if (!pLb)
# endif
                    {
                        NEMUNETFLT_LBVERIFY(pNetFlt, pPacket);

# ifdef NEMUNETFLT_NO_PACKET_QUEUE
                        if (nemuNetFltWinPostIntnet(pNetFlt, pPacket, 0))
                        {
                            /* drop it */
                            break;
                        }
# else
                        Status = nemuNetFltWinQuEnqueuePacket(pNetFlt, pPacket, PACKET_COPY);
                        Assert(Status == NDIS_STATUS_SUCCESS);
                        if (Status == NDIS_STATUS_SUCCESS)
                        {
                            //NdisReturnPackets(&pPacket, 1);
                            fWinIfActive = false;
                            bNetFltActive = false;
                            break;
                        }
# endif
                    }
# ifndef NEMU_LOOPBACK_USEFLAGS
                    else if (nemuNetFltWinLbIsFromIntNet(pLb))
                    {
                        /* nothing else to do here, just return the packet */
                        //NdisReturnPackets(&pPacket, 1);
                        Status = NDIS_STATUS_NOT_ACCEPTED;
                        break;
                    }
                    /* we are here because this is a looped back packet set not from intnet
                     * we will post it to the upper protocol */
# endif
                }

                Assert(Status == STATUS_SUCCESS);
                if (Status == STATUS_SUCCESS)
                {
# ifndef NEMU_LOOPBACK_USEFLAGS
                    Assert(!pLb || !nemuNetFltWinLbIsFromIntNet(pLb));
# endif
                    Status = nemuNetFltWinRecvPassThru(pNetFlt, pPacket);
                    Assert(Status == STATUS_SUCCESS);
                    /* we are done with packet processing, and we will
                     * not receive packet return event for this packet,
                     * fWinIfActive should be true to ensure we release WinIf*/
                    Assert(fWinIfActive);
                    if (Status == STATUS_SUCCESS)
                        break;
                }
                else
                {
                    /* intnet processing failed - fall back to no-packet mode */
                    Assert(bNetFltActive);
                    Assert(fWinIfActive);
                }

            }
#endif /* #ifndef DEBUG_NETFLT_RECV_NOPACKET */

            if (bNetFltActive)
            {
                Status = nemuNetFltWinPtReceiveActive(pNetFlt, MacReceiveContext, pHeaderBuffer, cbHeaderBuffer,
                        pLookAheadBuffer, cbLookAheadBuffer, cbPacket);
                if (NT_SUCCESS(Status))
                {
                    if (Status != NDIS_STATUS_NOT_ACCEPTED)
                    {
                        fWinIfActive = false;
                        bNetFltActive = false;
                    }
                    else
                    {
#ifndef NEMU_LOOPBACK_USEFLAGS
                        /* this is a loopback packet, nothing to do here */
#else
                        Assert(0);
                        /* should not be here */
#endif
                    }
                    break;
                }
            }

            /* we are done with packet processing, and we will
             * not receive packet return event for this packet,
             * fWinIfActive should be true to ensure we release WinIf*/
            Assert(fWinIfActive);

            nemuNetFltWinRecvIndicatePassThru(pNetFlt, MacReceiveContext, pHeaderBuffer, cbHeaderBuffer, pLookAheadBuffer, cbLookAheadBuffer, cbPacket);
            /* the status could contain an error value here in case the IntNet recv failed,
             * ensure we return back success status */
            Status = NDIS_STATUS_SUCCESS;

        } while (0);

        if (bNetFltActive)
        {
            nemuNetFltWinDereferenceNetFlt(pNetFlt);
        }
        else if (bPassThruActive)
        {
            nemuNetFltWinDereferenceModePassThru(pNetFlt);
        }
        if (fWinIfActive)
        {
            nemuNetFltWinDereferenceWinIf(pNetFlt);
        }
    }
    else
    {
        Status = NDIS_STATUS_FAILURE;
    }

    LogFlow(("<=="__FUNCTION__" : pNetFlt (0x%p)\n", pNetFlt));

    return Status;

}

static VOID nemuNetFltWinPtReceiveComplete(NDIS_HANDLE hProtocolBindingContext)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hProtocolBindingContext;
    ULONG cPackets = 0;
    bool bNetFltActive;
    bool fWinIfActive = nemuNetFltWinReferenceWinIfNetFlt(pNetFlt, &bNetFltActive);
    NDIS_HANDLE hMiniport = pNetFlt->u.s.WinIf.hMiniport;
    /* Note: we're using KeGetCurrentProcessorNumber, which is not entirely correct in case
    * we're running on 64bit win7+, which can handle > 64 CPUs, however since KeGetCurrentProcessorNumber
    * always returns the number < than the number of CPUs in the first group, we're guaranteed to have CPU index < 64
    * @todo: use KeGetCurrentProcessorNumberEx for Win7+ 64 and dynamically extended array */
    ULONG iProc = KeGetCurrentProcessorNumber();
    Assert(iProc < RT_ELEMENTS(pNetFlt->u.s.WinIf.abIndicateRxComplete));

    LogFlow(("==>"__FUNCTION__" : pNetFlt (0x%p)\n", pNetFlt));

    if (hMiniport != NULL && pNetFlt->u.s.WinIf.abIndicateRxComplete[iProc])
    {
        switch (pNetFlt->u.s.WinIf.enmMedium)
        {
            case NdisMedium802_3:
            case NdisMediumWan:
                NdisMEthIndicateReceiveComplete(hMiniport);
                break;
            default:
                Assert(0);
                break;
        }
    }

    pNetFlt->u.s.WinIf.abIndicateRxComplete[iProc] = FALSE;

    if (fWinIfActive)
    {
        if (bNetFltActive)
        {
            nemuNetFltWinDereferenceNetFlt(pNetFlt);
        }
        else
        {
            nemuNetFltWinDereferenceModePassThru(pNetFlt);
        }
        nemuNetFltWinDereferenceWinIf(pNetFlt);
    }

    LogFlow(("<=="__FUNCTION__" : pNetFlt (0x%p)\n", pNetFlt));
}

static INT nemuNetFltWinPtReceivePacket(NDIS_HANDLE hProtocolBindingContext, PNDIS_PACKET pPacket)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hProtocolBindingContext;
    INT cRefCount = 0;
    bool bNetFltActive;
    bool fWinIfActive = nemuNetFltWinReferenceWinIfNetFlt(pNetFlt, &bNetFltActive);
    const bool bPassThruActive = !bNetFltActive;

    LogFlow(("==>"__FUNCTION__" : pNetFlt (0x%p)\n", pNetFlt));

    if (fWinIfActive)
    {
        do
        {
#ifdef NEMU_LOOPBACK_USEFLAGS
            if (nemuNetFltWinIsLoopedBackPacket(pPacket))
            {
                Assert(0);
                Log(("lb_rp"));

                /* nothing else to do here, just return the packet */
                cRefCount = 0;
                //NdisReturnPackets(&pPacket, 1);
                break;
            }

            NEMUNETFLT_LBVERIFY(pNetFlt, pPacket);
#endif

            if (bNetFltActive)
            {
#ifndef NEMU_LOOPBACK_USEFLAGS
                PNDIS_PACKET pLb = nemuNetFltWinLbSearchLoopBack(pNetFlt, pPacket, false);
                if (!pLb)
#endif
                {
#ifndef NEMUNETFLT_NO_PACKET_QUEUE
                    NDIS_STATUS fStatus;
#endif
                    bool bResources = NDIS_GET_PACKET_STATUS(pPacket) == NDIS_STATUS_RESOURCES;

                    NEMUNETFLT_LBVERIFY(pNetFlt, pPacket);
#ifdef DEBUG_misha
                    /*TODO: remove this assert.
                     * this is a temporary assert for debugging purposes:
                     * we're probably doing something wrong with the packets if the miniport reports NDIS_STATUS_RESOURCES */
                    Assert(!bResources);
#endif

#ifdef NEMUNETFLT_NO_PACKET_QUEUE
                    if (nemuNetFltWinPostIntnet(pNetFlt, pPacket, 0))
                    {
                        /* drop it */
                        cRefCount = 0;
                        break;
                    }

#else
                    fStatus = nemuNetFltWinQuEnqueuePacket(pNetFlt, pPacket, bResources ? PACKET_COPY : 0);
                    if (fStatus == NDIS_STATUS_SUCCESS)
                    {
                        bNetFltActive = false;
                        fWinIfActive = false;
                        if (bResources)
                        {
                            cRefCount = 0;
                            //NdisReturnPackets(&pPacket, 1);
                        }
                        else
                        {
                            cRefCount = 1;
                        }
                        break;
                    }
                    else
                    {
                        Assert(0);
                    }
#endif
                }
#ifndef NEMU_LOOPBACK_USEFLAGS
                else if (nemuNetFltWinLbIsFromIntNet(pLb))
                {
                    /* the packet is from intnet, it has already been set to the host,
                     * no need for loopng it back to the host again */
                    /* nothing else to do here, just return the packet */
                    cRefCount = 0;
                    //NdisReturnPackets(&pPacket, 1);
                    break;
                }
#endif
            }

            cRefCount = nemuNetFltWinRecvPacketPassThru(pNetFlt, pPacket, bNetFltActive);
            if (cRefCount)
            {
                Assert(cRefCount == 1);
                fWinIfActive = false;
            }

        } while (FALSE);

        if (bNetFltActive)
        {
            nemuNetFltWinDereferenceNetFlt(pNetFlt);
        }
        else if (bPassThruActive)
        {
            nemuNetFltWinDereferenceModePassThru(pNetFlt);
        }
        if (fWinIfActive)
        {
            nemuNetFltWinDereferenceWinIf(pNetFlt);
        }
    }
    else
    {
        cRefCount = 0;
        //NdisReturnPackets(&pPacket, 1);
    }

    LogFlow(("<=="__FUNCTION__" : pNetFlt (0x%p), cRefCount (%d)\n", pNetFlt, cRefCount));

    return cRefCount;
}

DECLHIDDEN(bool) nemuNetFltWinPtCloseInterface(PNEMUNETFLTINS pNetFlt, PNDIS_STATUS pStatus)
{
    RTSpinlockAcquire(pNetFlt->hSpinlock);

    if (pNetFlt->u.s.WinIf.StateFlags.fInterfaceClosing)
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        Assert(0);
        return false;
    }
    if (pNetFlt->u.s.WinIf.hBinding == NULL)
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        Assert(0);
        return false;
    }

    pNetFlt->u.s.WinIf.StateFlags.fInterfaceClosing = TRUE;
    RTSpinlockRelease(pNetFlt->hSpinlock);

    NdisResetEvent(&pNetFlt->u.s.WinIf.OpenCloseEvent);
    NdisCloseAdapter(pStatus, pNetFlt->u.s.WinIf.hBinding);
    if (*pStatus == NDIS_STATUS_PENDING)
    {
        NdisWaitEvent(&pNetFlt->u.s.WinIf.OpenCloseEvent, 0);
        *pStatus = pNetFlt->u.s.WinIf.OpenCloseStatus;
    }

    Assert (*pStatus == NDIS_STATUS_SUCCESS);

    pNetFlt->u.s.WinIf.hBinding = NULL;

    return true;
}

static NDIS_STATUS nemuNetFltWinPtPnPSetPower(PNEMUNETFLTINS pNetFlt, NDIS_DEVICE_POWER_STATE enmPowerState)
{
    NDIS_DEVICE_POWER_STATE enmPrevPowerState = nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.PtState);

    RTSpinlockAcquire(pNetFlt->hSpinlock);

    nemuNetFltWinSetPowerState(&pNetFlt->u.s.WinIf.PtState, enmPowerState);

    if (nemuNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.PtState) > NdisDeviceStateD0)
    {
        if (enmPrevPowerState == NdisDeviceStateD0)
        {
            pNetFlt->u.s.WinIf.StateFlags.fStandBy = TRUE;
        }
        RTSpinlockRelease(pNetFlt->hSpinlock);
        nemuNetFltWinPtRequestsWaitComplete(pNetFlt);
        nemuNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.MpState);
        nemuNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.PtState);

        /* check packet pool is empty */
        UINT cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hSendPacketPool);
        Assert(cPPUsage == 0);
        cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hRecvPacketPool);
        Assert(cPPUsage == 0);
        /* for debugging only, ignore the err in release */
        NOREF(cPPUsage);

        Assert(!pNetFlt->u.s.WinIf.StateFlags.fRequestInfo);
    }
    else
    {
        if (enmPrevPowerState > NdisDeviceStateD0)
        {
            pNetFlt->u.s.WinIf.StateFlags.fStandBy = FALSE;
        }

        if (pNetFlt->u.s.WinIf.StateFlags.fRequestInfo & NEMUNDISREQUEST_QUEUED)
        {
            pNetFlt->u.s.WinIf.StateFlags.fRequestInfo = NEMUNDISREQUEST_INPROGRESS;
            RTSpinlockRelease(pNetFlt->hSpinlock);

            nemuNetFltWinMpRequestPost(pNetFlt);
        }
        else
        {
            RTSpinlockRelease(pNetFlt->hSpinlock);
        }
    }

    return NDIS_STATUS_SUCCESS;
}


static NDIS_STATUS nemuNetFltWinPtPnPEvent(IN NDIS_HANDLE hProtocolBindingContext, IN PNET_PNP_EVENT pNetPnPEvent)
{
    PNEMUNETFLTINS pNetFlt = (PNEMUNETFLTINS)hProtocolBindingContext;

    LogFlow(("==>"__FUNCTION__": pNetFlt (0x%p), NetEvent (%d)\n", pNetFlt, pNetPnPEvent->NetEvent));

    switch (pNetPnPEvent->NetEvent)
    {
        case NetEventSetPower:
        {
            NDIS_DEVICE_POWER_STATE enmPowerState = *((PNDIS_DEVICE_POWER_STATE)pNetPnPEvent->Buffer);
            return nemuNetFltWinPtPnPSetPower(pNetFlt, enmPowerState);
        }
        case NetEventReconfigure:
        {
            if (!pNetFlt)
            {
                NdisReEnumerateProtocolBindings(g_NemuNetFltGlobalsWin.Pt.hProtocol);
            }
        }
        default:
            return NDIS_STATUS_SUCCESS;
    }

    LogFlow(("<=="__FUNCTION__": pNetFlt (0x%p), NetEvent (%d)\n", pNetFlt, pNetPnPEvent->NetEvent));
}

#ifdef __cplusplus
# define PTCHARS_40(_p) ((_p).Ndis40Chars)
#else
# define PTCHARS_40(_p) (_p)
#endif

/**
 * register the protocol edge
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtRegister(PNEMUNETFLTGLOBALS_PT pGlobalsPt, PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPathStr)
{
    NDIS_PROTOCOL_CHARACTERISTICS PtChars;
    NDIS_STRING NameStr;

    NdisInitUnicodeString(&NameStr, NEMUNETFLT_NAME_PROTOCOL);

    NdisZeroMemory(&PtChars, sizeof (PtChars));
    PTCHARS_40(PtChars).MajorNdisVersion = NEMUNETFLT_VERSION_PT_NDIS_MAJOR;
    PTCHARS_40(PtChars).MinorNdisVersion = NEMUNETFLT_VERSION_PT_NDIS_MINOR;

    PTCHARS_40(PtChars).Name = NameStr;
    PTCHARS_40(PtChars).OpenAdapterCompleteHandler = nemuNetFltWinPtOpenAdapterComplete;
    PTCHARS_40(PtChars).CloseAdapterCompleteHandler = nemuNetFltWinPtCloseAdapterComplete;
    PTCHARS_40(PtChars).SendCompleteHandler = nemuNetFltWinPtSendComplete;
    PTCHARS_40(PtChars).TransferDataCompleteHandler = nemuNetFltWinPtTransferDataComplete;
    PTCHARS_40(PtChars).ResetCompleteHandler = nemuNetFltWinPtResetComplete;
    PTCHARS_40(PtChars).RequestCompleteHandler = nemuNetFltWinPtRequestComplete;
    PTCHARS_40(PtChars).ReceiveHandler = nemuNetFltWinPtReceive;
    PTCHARS_40(PtChars).ReceiveCompleteHandler = nemuNetFltWinPtReceiveComplete;
    PTCHARS_40(PtChars).StatusHandler = nemuNetFltWinPtStatus;
    PTCHARS_40(PtChars).StatusCompleteHandler = nemuNetFltWinPtStatusComplete;
    PTCHARS_40(PtChars).BindAdapterHandler = nemuNetFltWinPtBindAdapter;
    PTCHARS_40(PtChars).UnbindAdapterHandler = nemuNetFltWinPtUnbindAdapter;
    PTCHARS_40(PtChars).UnloadHandler = nemuNetFltWinPtUnloadProtocol;
#if !defined(DEBUG_NETFLT_RECV)
    PTCHARS_40(PtChars).ReceivePacketHandler = nemuNetFltWinPtReceivePacket;
#endif
    PTCHARS_40(PtChars).PnPEventHandler = nemuNetFltWinPtPnPEvent;

    NDIS_STATUS Status;
    NdisRegisterProtocol(&Status, &pGlobalsPt->hProtocol, &PtChars, sizeof (PtChars));
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

/**
 * deregister the protocol edge
 */
DECLHIDDEN(NDIS_STATUS) nemuNetFltWinPtDeregister(PNEMUNETFLTGLOBALS_PT pGlobalsPt)
{
    if (!pGlobalsPt->hProtocol)
        return NDIS_STATUS_SUCCESS;

    NDIS_STATUS Status;

    NdisDeregisterProtocol(&Status, pGlobalsPt->hProtocol);
    Assert (Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        NdisZeroMemory(pGlobalsPt, sizeof (*pGlobalsPt));
    }
    return Status;
}
