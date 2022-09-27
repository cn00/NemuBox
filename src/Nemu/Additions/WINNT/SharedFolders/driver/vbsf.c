/* $Id: vbsf.c $ */
/** @file
 * VirtualBox Windows Guest Shared Folders.
 *
 * File System Driver initialization and generic routines
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "vbsf.h"

/*
 * The current state of the driver.
 */
typedef enum _MRX_NEMU_STATE_
{
    MRX_NEMU_STARTABLE,
    MRX_NEMU_START_IN_PROGRESS,
    MRX_NEMU_STARTED
} MRX_NEMU_STATE, *PMRX_NEMU_STATE;

static MRX_NEMU_STATE NemuMRxState = MRX_NEMU_STARTABLE;

/*
 * The NemuSF dispatch table.
 */
static struct _MINIRDR_DISPATCH NemuMRxDispatch;

/*
 * The NemuSF device object.
 */
PRDBSS_DEVICE_OBJECT NemuMRxDeviceObject;

static NTSTATUS NemuMRxFsdDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    UCHAR MajorFunctionCode = IrpSp->MajorFunction;
    ULONG MinorFunctionCode = IrpSp->MinorFunction;

    Log(("NEMUSF: MRxFsdDispatch: major %d, minor %d: %s\n",
         MajorFunctionCode, MinorFunctionCode, MajorFunctionString(MajorFunctionCode, MinorFunctionCode)));

    if (DeviceObject != (PDEVICE_OBJECT)NemuMRxDeviceObject)
    {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        Log(("NEMUSF: MRxFsdDispatch: Invalid device request detected %p %p\n",
             DeviceObject, (PDEVICE_OBJECT)NemuMRxDeviceObject));

        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = RxFsdDispatch((PRDBSS_DEVICE_OBJECT)NemuMRxDeviceObject, Irp);

    Log(("NEMUSF: MRxFsdDispatch: Returned 0x%X\n",
         Status));
    return Status;
}

static void NemuMRxUnload(IN PDRIVER_OBJECT DriverObject)
{
    NTSTATUS Status;
    UNICODE_STRING UserModeDeviceName;

    Log(("NEMUSF: MRxUnload\n"));

    if (NemuMRxDeviceObject)
    {
        PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension;
        pDeviceExtension = (PMRX_NEMU_DEVICE_EXTENSION)((PBYTE)NemuMRxDeviceObject + sizeof(RDBSS_DEVICE_OBJECT));
        VbglR0SfDisconnect(&pDeviceExtension->hgcmClient);
    }

    VbglR0SfTerm();

    if (NemuMRxDeviceObject)
    {
        PRX_CONTEXT RxContext;
        RxContext = RxCreateRxContext(NULL, NemuMRxDeviceObject, RX_CONTEXT_FLAG_IN_FSP);

        if (RxContext != NULL)
        {
            Status = RxStopMinirdr(RxContext, &RxContext->PostRequest);

            if (Status == STATUS_SUCCESS)
            {
                MRX_NEMU_STATE State;

                State = (MRX_NEMU_STATE)InterlockedCompareExchange((LONG *)&NemuMRxState, MRX_NEMU_STARTABLE, MRX_NEMU_STARTED);

                if (State != MRX_NEMU_STARTABLE)
                    Status = STATUS_REDIRECTOR_STARTED;
            }

            RxDereferenceAndDeleteRxContext(RxContext);
        }
        else
            Status = STATUS_INSUFFICIENT_RESOURCES;

        RxUnregisterMinirdr(NemuMRxDeviceObject);
    }

    RtlInitUnicodeString(&UserModeDeviceName, DD_MRX_NEMU_USERMODE_SHADOW_DEV_NAME_U);
    Status = IoDeleteSymbolicLink(&UserModeDeviceName);
    if (Status != STATUS_SUCCESS)
        Log(("NEMUSF: MRxUnload: IoDeleteSymbolicLink Status 0x%08X\n", Status));

    RxUnload(DriverObject);

    Log(("NEMUSF: MRxUnload: NemuSF.sys driver object %p unloaded\n", DriverObject));
}

static void vbsfInitMRxDispatch(void)
{
    Log(("NEMUSF: vbsfInitMRxDispatch: Called.\n"));

    ZeroAndInitializeNodeType(&NemuMRxDispatch, RDBSS_NTC_MINIRDR_DISPATCH, sizeof(MINIRDR_DISPATCH));

    NemuMRxDispatch.MRxFlags = (RDBSS_MANAGE_NET_ROOT_EXTENSION | RDBSS_MANAGE_FOBX_EXTENSION);

    NemuMRxDispatch.MRxSrvCallSize = 0;
    NemuMRxDispatch.MRxNetRootSize = sizeof(MRX_NEMU_NETROOT_EXTENSION);
    NemuMRxDispatch.MRxVNetRootSize = 0;
    NemuMRxDispatch.MRxFcbSize = 0;
    NemuMRxDispatch.MRxSrvOpenSize = 0;
    NemuMRxDispatch.MRxFobxSize = sizeof(MRX_NEMU_FOBX);

    NemuMRxDispatch.MRxStart = NemuMRxStart;
    NemuMRxDispatch.MRxStop = NemuMRxStop;

    NemuMRxDispatch.MRxCreate = NemuMRxCreate;
    NemuMRxDispatch.MRxCollapseOpen = NemuMRxCollapseOpen;
    NemuMRxDispatch.MRxShouldTryToCollapseThisOpen = NemuMRxShouldTryToCollapseThisOpen;
    NemuMRxDispatch.MRxFlush = NemuMRxFlush;
    NemuMRxDispatch.MRxTruncate = NemuMRxTruncate;
    NemuMRxDispatch.MRxCleanupFobx = NemuMRxCleanupFobx;
    NemuMRxDispatch.MRxCloseSrvOpen = NemuMRxCloseSrvOpen;
    NemuMRxDispatch.MRxDeallocateForFcb = NemuMRxDeallocateForFcb;
    NemuMRxDispatch.MRxDeallocateForFobx = NemuMRxDeallocateForFobx;
    NemuMRxDispatch.MRxForceClosed = NemuMRxForceClosed;

    NemuMRxDispatch.MRxQueryDirectory = NemuMRxQueryDirectory;
    NemuMRxDispatch.MRxQueryFileInfo = NemuMRxQueryFileInfo;
    NemuMRxDispatch.MRxSetFileInfo = NemuMRxSetFileInfo;
    NemuMRxDispatch.MRxSetFileInfoAtCleanup = NemuMRxSetFileInfoAtCleanup;
    NemuMRxDispatch.MRxQueryEaInfo = NemuMRxQueryEaInfo;
    NemuMRxDispatch.MRxSetEaInfo = NemuMRxSetEaInfo;
    NemuMRxDispatch.MRxQuerySdInfo = NemuMRxQuerySdInfo;
    NemuMRxDispatch.MRxSetSdInfo = NemuMRxSetSdInfo;
    NemuMRxDispatch.MRxQueryVolumeInfo = NemuMRxQueryVolumeInfo;

    NemuMRxDispatch.MRxComputeNewBufferingState = NemuMRxComputeNewBufferingState;

    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_READ] = NemuMRxRead;
    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_WRITE] = NemuMRxWrite;
    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_SHAREDLOCK] = NemuMRxLocks;
    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_EXCLUSIVELOCK] = NemuMRxLocks;
    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_UNLOCK] = NemuMRxLocks;
    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_UNLOCK_MULTIPLE] = NemuMRxLocks;
    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_FSCTL] = NemuMRxFsCtl;
    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_IOCTL] = NemuMRxIoCtl;
    NemuMRxDispatch.MRxLowIOSubmit[LOWIO_OP_NOTIFY_CHANGE_DIRECTORY] = NemuMRxNotifyChangeDirectory;

    NemuMRxDispatch.MRxExtendForCache = NemuMRxExtendStub;
    NemuMRxDispatch.MRxExtendForNonCache = NemuMRxExtendStub;
    NemuMRxDispatch.MRxCompleteBufferingStateChangeRequest = NemuMRxCompleteBufferingStateChangeRequest;

    NemuMRxDispatch.MRxCreateVNetRoot = NemuMRxCreateVNetRoot;
    NemuMRxDispatch.MRxFinalizeVNetRoot = NemuMRxFinalizeVNetRoot;
    NemuMRxDispatch.MRxFinalizeNetRoot = NemuMRxFinalizeNetRoot;
    NemuMRxDispatch.MRxUpdateNetRootState = NemuMRxUpdateNetRootState;
    NemuMRxDispatch.MRxExtractNetRootName = NemuMRxExtractNetRootName;

    NemuMRxDispatch.MRxCreateSrvCall = NemuMRxCreateSrvCall;
    NemuMRxDispatch.MRxSrvCallWinnerNotify = NemuMRxSrvCallWinnerNotify;
    NemuMRxDispatch.MRxFinalizeSrvCall = NemuMRxFinalizeSrvCall;

    NemuMRxDispatch.MRxDevFcbXXXControlFile = NemuMRxDevFcbXXXControlFile;

    Log(("NEMUSF: vbsfInitMRxDispatch: Success.\n"));
    return;
}

static BOOL nemuIsPrefixOK (const WCHAR *FilePathName, ULONG PathNameLength)
{
    BOOL PrefixOK;

    /* The FilePathName here looks like: \nemusrv\... */
    if (PathNameLength >= 8 * sizeof (WCHAR)) /* Number of bytes in '\nemusrv' unicode string. */
    {
        PrefixOK =  (FilePathName[0] == L'\\');
        PrefixOK &= (FilePathName[1] == L'V') || (FilePathName[1] == L'v');
        PrefixOK &= (FilePathName[2] == L'B') || (FilePathName[2] == L'b');
        PrefixOK &= (FilePathName[3] == L'O') || (FilePathName[3] == L'o');
        PrefixOK &= (FilePathName[4] == L'X') || (FilePathName[4] == L'x');
        PrefixOK &= (FilePathName[5] == L'S') || (FilePathName[5] == L's');
        /* Both nemusvr & nemusrv are now accepted */
        if ((FilePathName[6] == L'V') || (FilePathName[6] == L'v'))
        {
            PrefixOK &= (FilePathName[6] == L'V') || (FilePathName[6] == L'v');
            PrefixOK &= (FilePathName[7] == L'R') || (FilePathName[7] == L'r');
        }
        else
        {
            PrefixOK &= (FilePathName[6] == L'R') || (FilePathName[6] == L'r');
            PrefixOK &= (FilePathName[7] == L'V') || (FilePathName[7] == L'v');
        }
        if (PathNameLength > 8 * sizeof (WCHAR))
        {
            /* There is something after '\nemusrv'. */
            PrefixOK &= (FilePathName[8] == L'\\') || (FilePathName[8] == 0);
        }
    }
    else
        PrefixOK = FALSE;

    return PrefixOK;
}

static NTSTATUS NemuMRXDeviceControl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS Status = STATUS_SUCCESS;

    QUERY_PATH_REQUEST *pReq = NULL;
    QUERY_PATH_REQUEST_EX *pReqEx = NULL;
    QUERY_PATH_RESPONSE *pResp = NULL;

    BOOL PrefixOK = FALSE;

    PIO_STACK_LOCATION pStack = IoGetCurrentIrpStackLocation(pIrp);

    /* Make a local copy, it will be needed after the Irp completion. */
    ULONG IoControlCode = pStack->Parameters.DeviceIoControl.IoControlCode;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = (PMRX_NEMU_DEVICE_EXTENSION)((PBYTE)pDevObj + sizeof(RDBSS_DEVICE_OBJECT));

    Log(("NEMUSF: MRXDeviceControl: pDevObj %p, pDeviceExtension %p, code %x\n",
         pDevObj, pDevObj->DeviceExtension, IoControlCode));

    switch (IoControlCode)
    {
        case IOCTL_REDIR_QUERY_PATH_EX: /* Vista */
        case IOCTL_REDIR_QUERY_PATH:    /* XP and earlier */
        {
            /* This IOCTL is intercepted for 2 reasons:
             * 1) Claim the nemusvr and nemusrv prefixes. All name-based operations for them
             *    will be routed to the Nemu provider automatically without any prefix resolution
             *    since the prefix is already in the prefix cache.
             * 2) Reject other prefixes immediately to speed up the UNC path resolution a bit,
             *    because RDBSS will not be involved then.
             */

            const WCHAR *FilePathName = NULL;
            ULONG PathNameLength = 0;

            if (pIrp->RequestorMode != KernelMode)
            {
                /* MSDN: Network redirectors should only honor kernel-mode senders of this IOCTL, by verifying
                 * that RequestorMode member of the IRP structure is KernelMode.
                 */
                Log(("NEMUSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH(_EX): not kernel mode!!!\n",
                      pStack->Parameters.DeviceIoControl.InputBufferLength));
                /* Continue to RDBSS. */
                break;
            }

            if (IoControlCode == IOCTL_REDIR_QUERY_PATH)
            {
                Log(("NEMUSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH: Called (pid %x).\n", IoGetCurrentProcess()));

                if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(QUERY_PATH_REQUEST))
                {
                    Log(("NEMUSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH: short input buffer %d.\n",
                          pStack->Parameters.DeviceIoControl.InputBufferLength));
                    /* Continue to RDBSS. */
                    break;
                }

                pReq = (QUERY_PATH_REQUEST *)pStack->Parameters.DeviceIoControl.Type3InputBuffer;

                Log(("NEMUSF: MRxDeviceControl: PathNameLength = %d.\n", pReq->PathNameLength));
                Log(("NEMUSF: MRxDeviceControl: SecurityContext = %p.\n", pReq->SecurityContext));
                Log(("NEMUSF: MRxDeviceControl: FilePathName = %.*ls.\n", pReq->PathNameLength / sizeof (WCHAR), pReq->FilePathName));

                FilePathName = pReq->FilePathName;
                PathNameLength = pReq->PathNameLength;
            }
            else
            {
                Log(("NEMUSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH_EX: Called.\n"));

                if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(QUERY_PATH_REQUEST_EX))
                {
                    Log(("NEMUSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH_EX: short input buffer %d.\n",
                          pStack->Parameters.DeviceIoControl.InputBufferLength));
                    /* Continue to RDBSS. */
                    break;
                }

                pReqEx = (QUERY_PATH_REQUEST_EX *)pStack->Parameters.DeviceIoControl.Type3InputBuffer;

                Log(("NEMUSF: MRxDeviceControl: pSecurityContext = %p.\n", pReqEx->pSecurityContext));
                Log(("NEMUSF: MRxDeviceControl: EaLength = %d.\n", pReqEx->EaLength));
                Log(("NEMUSF: MRxDeviceControl: pEaBuffer = %p.\n", pReqEx->pEaBuffer));
                Log(("NEMUSF: MRxDeviceControl: PathNameLength = %d.\n", pReqEx->PathName.Length));
                Log(("NEMUSF: MRxDeviceControl: FilePathName = %.*ls.\n", pReqEx->PathName.Length / sizeof (WCHAR), pReqEx->PathName.Buffer));

                FilePathName = pReqEx->PathName.Buffer;
                PathNameLength = pReqEx->PathName.Length;
            }

            pResp = (QUERY_PATH_RESPONSE *)pIrp->UserBuffer;

            PrefixOK = nemuIsPrefixOK (FilePathName, PathNameLength);
            Log(("NEMUSF: MRxDeviceControl PrefixOK %d\n", PrefixOK));

            if (!PrefixOK)
            {
                /* Immediately fail the IOCTL with STATUS_BAD_NETWORK_NAME as recommended by MSDN.
                 * No need to involve RDBSS.
                 */
                Status = STATUS_BAD_NETWORK_NAME;

                pIrp->IoStatus.Status = Status;
                pIrp->IoStatus.Information = 0;

                IoCompleteRequest(pIrp, IO_NO_INCREMENT);

                Log(("NEMUSF: MRxDeviceControl: returned STATUS_BAD_NETWORK_NAME\n"));
                return Status;
            }

            Log(("NEMUSF: MRxDeviceControl pResp %p verifying the path.\n", pResp));
            if (pResp)
            {
                /* Always claim entire \nemusrv prefix. The LengthAccepted initially is equal to entire path.
                 * Here it is assigned to the length of \nemusrv prefix.
                 */
                pResp->LengthAccepted = 8 * sizeof (WCHAR);

                Status = STATUS_SUCCESS;

                pIrp->IoStatus.Status = Status;
                pIrp->IoStatus.Information = 0;

                IoCompleteRequest(pIrp, IO_NO_INCREMENT);

                Log(("NEMUSF: MRxDeviceControl: claiming the path.\n"));
                return Status;
            }

            /* No pResp pointer, should not happen. Just a precaution. */
            Status = STATUS_INVALID_PARAMETER;

            pIrp->IoStatus.Status = Status;
            pIrp->IoStatus.Information = 0;

            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            Log(("NEMUSF: MRxDeviceControl: returned STATUS_INVALID_PARAMETER\n"));
            return Status;
        }

        default:
            break;
    }

    /* Pass the IOCTL to RDBSS. */
    if (pDeviceExtension && pDeviceExtension->pfnRDBSSDeviceControl)
    {
        Log(("NEMUSF: MRxDeviceControl calling RDBSS %p\n", pDeviceExtension->pfnRDBSSDeviceControl));
        Status = pDeviceExtension->pfnRDBSSDeviceControl (pDevObj, pIrp);
        Log(("NEMUSF: MRxDeviceControl RDBSS status 0x%08X\n", Status));
    }
    else
    {
        /* No RDBSS, should not happen. Just a precaution. */
        Status = STATUS_NOT_IMPLEMENTED;

        pIrp->IoStatus.Status = Status;
        pIrp->IoStatus.Information = 0;

        IoCompleteRequest(pIrp, IO_NO_INCREMENT);

        Log(("NEMUSF: MRxDeviceControl: returned STATUS_NOT_IMPLEMENTED\n"));
    }

    return Status;
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT  DriverObject,
                     IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    UNICODE_STRING NemuMRxName;
    UNICODE_STRING UserModeDeviceName;
    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension;
    ULONG i;
    int nemuRC;
    VBGLSFCLIENT hgcmClient;

    Log(("NEMUSF: DriverEntry: Driver object %p\n", DriverObject));

    if (!DriverObject)
    {
        Log(("NEMUSF: DriverEntry: driver object is NULL.\n"));
        return STATUS_UNSUCCESSFUL;
    }

    /* Initialize Nemu subsystem. */
    nemuRC = VbglR0SfInit();
    if (RT_FAILURE(nemuRC))
    {
        Log(("NEMUSF: DriverEntry: ERROR while initializing Nemu subsystem (%Rrc)!\n", nemuRC));
        return STATUS_UNSUCCESSFUL;
    }

    /* Connect the HGCM client */
    RT_ZERO(hgcmClient);
    nemuRC = VbglR0SfConnect(&hgcmClient);
    if (RT_FAILURE(nemuRC))
    {
        Log(("NEMUSF: DriverEntry: ERROR while connecting to host (%Rrc)!\n",
             nemuRC));
        VbglR0SfTerm();
        return STATUS_UNSUCCESSFUL;
    }

    /* Init the driver object. */
    DriverObject->DriverUnload = NemuMRxUnload;
    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        DriverObject->MajorFunction[i] = (PDRIVER_DISPATCH)NemuMRxFsdDispatch;
    }

    /* Forward to RDBSS. */
    Status = RxDriverEntry(DriverObject, RegistryPath);
    if (Status != STATUS_SUCCESS)
    {
        Log(("NEMUSF: DriverEntry: RxDriverEntry failed: 0x%08X\n", Status));
        goto failure;
    }

    __try
    {
        Log(("NEMUSF: DriverEntry: RxRegisterMinirdr: calling NemuMRxDeviceObject %p\n",
             NemuMRxDeviceObject));

        RtlInitUnicodeString(&NemuMRxName, DD_MRX_NEMU_FS_DEVICE_NAME_U);

        /* Don use RX_REGISTERMINI_FLAG_DONT_PROVIDE_UNCS or else
         * UNC mappings don't work (including Windows explorer browsing).
         */
        Status = RxRegisterMinirdr(&NemuMRxDeviceObject,
                                   DriverObject,
                                   &NemuMRxDispatch,
                                   RX_REGISTERMINI_FLAG_DONT_PROVIDE_MAILSLOTS,
                                   &NemuMRxName,
                                   sizeof(MRX_NEMU_DEVICE_EXTENSION),
                                   FILE_DEVICE_NETWORK_FILE_SYSTEM,
                                   FILE_REMOTE_DEVICE);

        Log(("NEMUSF: DriverEntry: RxRegisterMinirdr: returned 0x%08X NemuMRxDeviceObject %p\n",
             Status, NemuMRxDeviceObject));

        if (Status!=STATUS_SUCCESS)
        {
            Log(("NEMUSF: DriverEntry: RxRegisterMinirdr failed: 0x%08X\n", Status ));
            try_return(Status);
        }

        /* Init the device extension.
         * NOTE: the device extension actually points to fields
         * in the RDBSS_DEVICE_OBJECT. Our space is past the end
         * of this struct!!
         */
        pDeviceExtension = (PMRX_NEMU_DEVICE_EXTENSION)((PBYTE)NemuMRxDeviceObject + sizeof(RDBSS_DEVICE_OBJECT));

        pDeviceExtension->pDeviceObject = NemuMRxDeviceObject;

        for (i = 0; i < RT_ELEMENTS(pDeviceExtension->cLocalConnections); i++)
        {
            pDeviceExtension->cLocalConnections[i] = FALSE;
        }

        /* Mutex for synchronizining our connection list */
        ExInitializeFastMutex(&pDeviceExtension->mtxLocalCon);

        /* The device object has been created. Need to setup a symbolic
         * link so that the device may be accessed from a Win32 user mode
         * application.
         */

        RtlInitUnicodeString(&UserModeDeviceName, DD_MRX_NEMU_USERMODE_SHADOW_DEV_NAME_U);
        Log(("NEMUSF: DriverEntry: Calling IoCreateSymbolicLink\n"));
        Status = IoCreateSymbolicLink(&UserModeDeviceName, &NemuMRxName);
        if (Status != STATUS_SUCCESS)
        {
            Log(("NEMUSF: DriverEntry: IoCreateSymbolicLink: 0x%08X\n",
                 Status));
            try_return(Status);
        }
        Log(("NEMUSF: DriverEntry: Symbolic link created.\n"));

        /*
         * Build the dispatch tables for the minirdr
         */
        vbsfInitMRxDispatch();

    try_exit:
         ;
    }
    __finally
    {
        ;
    }

    if (Status != STATUS_SUCCESS)
    {
        Log(("NEMUSF: DriverEntry: NemuSF.sys failed to start with Status = 0x%08X\n",
             Status));
        goto failure;
    }

    pDeviceExtension->hgcmClient = hgcmClient;

    /* The redirector driver must intercept the IOCTL to avoid NEMUSVR name resolution
     * by other redirectors. These additional name resolutions cause long delays.
     */
    Log(("NEMUSF: DriverEntry: NemuMRxDeviceObject = %p, rdbss %p, devext %p\n",
         NemuMRxDeviceObject, DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL], pDeviceExtension));
    pDeviceExtension->pfnRDBSSDeviceControl = DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = NemuMRXDeviceControl;

    /* @todo start the redirector here RxStartMiniRdr. */

    Log(("NEMUSF: DriverEntry: Init successful!\n"));
    return STATUS_SUCCESS;

failure:

    Log(("NEMUSF: DriverEntry: Failure! Status = 0x%08X\n", Status));

    VbglR0SfDisconnect(&hgcmClient);
    VbglR0SfTerm();

    if (NemuMRxDeviceObject)
    {
        RxUnregisterMinirdr(NemuMRxDeviceObject);
        NemuMRxDeviceObject = NULL;
    }

    return Status;
}

NTSTATUS NemuMRxStart(PRX_CONTEXT RxContext, IN OUT PRDBSS_DEVICE_OBJECT RxDeviceObject)
{
    NTSTATUS Status;
    MRX_NEMU_STATE CurrentState;

    Log(("NEMUSF: MRxStart\n"));

    CurrentState = (MRX_NEMU_STATE)InterlockedCompareExchange((PLONG)&NemuMRxState, MRX_NEMU_STARTED, MRX_NEMU_START_IN_PROGRESS);

    if (CurrentState == MRX_NEMU_START_IN_PROGRESS)
    {
        Log(("NEMUSF: MRxStart: Start in progress -> started\n"));
        Status = STATUS_SUCCESS;
    }
    else if (NemuMRxState == MRX_NEMU_STARTED)
    {
        Log(("NEMUSF: MRxStart: Already started\n"));
        Status = STATUS_REDIRECTOR_STARTED;
    }
    else
    {
        Log(("NEMUSF: MRxStart: Bad state! NemuMRxState = %d\n", NemuMRxState));
        Status = STATUS_UNSUCCESSFUL;
    }

    return Status;
}

NTSTATUS NemuMRxStop(PRX_CONTEXT RxContext, IN OUT PRDBSS_DEVICE_OBJECT RxDeviceObject)
{
    Log(("NEMUSF: MRxStop\n"));
    return STATUS_SUCCESS;
}

NTSTATUS NemuMRxIoCtl(IN OUT PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxIoCtl: IoControlCode = 0x%08X\n",
         RxContext->LowIoContext.ParamsFor.FsCtl.FsControlCode));
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSYSAPI NTSTATUS NTAPI ZwSetSecurityObject(IN HANDLE Handle,
                                            IN SECURITY_INFORMATION SecurityInformation,
                                            IN PSECURITY_DESCRIPTOR SecurityDescriptor);

NTSTATUS NemuMRxDevFcbXXXControlFile(IN OUT PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;
    RxCaptureFobx;
    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PLOWIO_CONTEXT LowIoContext = &RxContext->LowIoContext;

    Log(("NEMUSF: MRxDevFcbXXXControlFile: MajorFunction = 0x%02X\n",
         RxContext->MajorFunction));

    switch (RxContext->MajorFunction)
    {
        case IRP_MJ_FILE_SYSTEM_CONTROL:
        {
            Log(("NEMUSF: MRxDevFcbXXXControlFile: IRP_MN_USER_FS_REQUEST: 0x%08X\n",
                 LowIoContext->ParamsFor.FsCtl.MinorFunction));
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        case IRP_MJ_DEVICE_CONTROL:
        {
            Log(("NEMUSF: MRxDevFcbXXXControlFile: IRP_MJ_DEVICE_CONTROL: InputBuffer %p/%d, OutputBuffer %p/%d\n",
                 LowIoContext->ParamsFor.IoCtl.pInputBuffer,
                 LowIoContext->ParamsFor.IoCtl.InputBufferLength,
                 LowIoContext->ParamsFor.IoCtl.pOutputBuffer,
                 LowIoContext->ParamsFor.IoCtl.OutputBufferLength));

            switch (LowIoContext->ParamsFor.IoCtl.IoControlCode)
            {
                case IOCTL_MRX_NEMU_ADDCONN:
                {
                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_ADDCONN\n"));
                    Status = vbsfCreateConnection(RxContext, &RxContext->PostRequest);
                    break;
                }

                case IOCTL_MRX_NEMU_DELCONN:
                {
                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_DELCONN\n"));
                    Status = vbsfDeleteConnection(RxContext, &RxContext->PostRequest);
                    break;
                }

                case IOCTL_MRX_NEMU_GETLIST:
                {
                    ULONG cbOut = LowIoContext->ParamsFor.IoCtl.OutputBufferLength;
                    uint8_t *pu8Out = (uint8_t *)LowIoContext->ParamsFor.IoCtl.pOutputBuffer;

                    BOOLEAN fLocked = FALSE;

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETLIST\n"));

                    RxContext->InformationToReturn = 0;

                    if (   !pDeviceExtension
                        || cbOut < _MRX_MAX_DRIVE_LETTERS)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETLIST: Copying local connections\n"));

                    fLocked = ExTryToAcquireFastMutex(&pDeviceExtension->mtxLocalCon);

                    __try
                    {
                        RtlCopyMemory(pu8Out, pDeviceExtension->cLocalConnections, _MRX_MAX_DRIVE_LETTERS);
                        RxContext->InformationToReturn = _MRX_MAX_DRIVE_LETTERS;
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                    }

                    if (fLocked)
                    {
                        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
                        fLocked = FALSE;
                    }

                    break;
                }

                /*
                 * Returns the root IDs of shared folder mappings.
                 */
                case IOCTL_MRX_NEMU_GETGLOBALLIST:
                {
                    ULONG cbOut = LowIoContext->ParamsFor.IoCtl.OutputBufferLength;
                    uint8_t *pu8Out = (uint8_t *)LowIoContext->ParamsFor.IoCtl.pOutputBuffer;

                    int nemuRC;
                    SHFLMAPPING mappings[_MRX_MAX_DRIVE_LETTERS];
                    uint32_t cMappings = RT_ELEMENTS(mappings);

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETGLOBALLIST\n"));

                    RxContext->InformationToReturn = 0;

                    if (   !pDeviceExtension
                        || cbOut < _MRX_MAX_DRIVE_LETTERS)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    nemuRC = VbglR0SfQueryMappings(&pDeviceExtension->hgcmClient, mappings, &cMappings);
                    if (nemuRC == VINF_SUCCESS)
                    {
                        __try
                        {
                            uint32_t i;

                            RtlZeroMemory(pu8Out, _MRX_MAX_DRIVE_LETTERS);

                            for (i = 0; i < RT_MIN(cMappings, cbOut); i++)
                            {
                                pu8Out[i] = mappings[i].root;
                                pu8Out[i] |= 0x80; /* mark active */ /** @todo fix properly */
                            }

                            RxContext->InformationToReturn = _MRX_MAX_DRIVE_LETTERS;
                        }
                        __except(EXCEPTION_EXECUTE_HANDLER)
                        {
                            Status = STATUS_INVALID_PARAMETER;
                        }
                    }
                    else
                    {
                        Status = NemuErrorToNTStatus(nemuRC);
                        Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETGLOBALLIST failed: 0x%08X\n",
                             Status));
                    }

                    break;
                }

                /*
                 * Translates a local connection name (e.g. drive "S:") to the
                 * corresponding remote name (e.g. \\nemusrv\share).
                 */
                case IOCTL_MRX_NEMU_GETCONN:
                {
                    ULONG cbConnectName = LowIoContext->ParamsFor.IoCtl.InputBufferLength;
                    PWCHAR pwcConnectName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pInputBuffer;
                    ULONG cbRemoteName = LowIoContext->ParamsFor.IoCtl.OutputBufferLength;
                    PWCHAR pwcRemoteName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pOutputBuffer;

                    BOOLEAN fMutexAcquired = FALSE;

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETCONN\n"));

                    RxContext->InformationToReturn = 0;

                    if (   !pDeviceExtension
                        || cbConnectName < sizeof(WCHAR))
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETCONN: Looking up connection name and connections\n"));

                    __try
                    {
                        uint32_t idx = *pwcConnectName - L'A';

                        Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETCONN: ConnectName = %.*ls, Len = %d, Index = %d\n",
                             cbConnectName / sizeof(WCHAR), pwcConnectName, cbConnectName, idx));

                        if (idx < RTL_NUMBER_OF(pDeviceExtension->wszLocalConnectionName))
                        {
                            ExAcquireFastMutex(&pDeviceExtension->mtxLocalCon);
                            fMutexAcquired = TRUE;

                            if (pDeviceExtension->wszLocalConnectionName[idx])
                            {
                                ULONG cbLocalConnectionName = pDeviceExtension->wszLocalConnectionName[idx]->Length;

                                Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETCONN: LocalConnectionName = %.*ls\n",
                                     cbLocalConnectionName / sizeof(WCHAR), pDeviceExtension->wszLocalConnectionName[idx]->Buffer));

                                if ((pDeviceExtension->cLocalConnections[idx]) && (cbLocalConnectionName <= cbRemoteName))
                                {
                                    RtlZeroMemory(pwcRemoteName, cbRemoteName);
                                    RtlCopyMemory(pwcRemoteName,
                                                  pDeviceExtension->wszLocalConnectionName[idx]->Buffer,
                                                  cbLocalConnectionName);

                                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETCONN: Remote name = %.*ls, Len = %d\n",
                                         cbLocalConnectionName / sizeof(WCHAR), pwcRemoteName, cbLocalConnectionName));
                                }
                                else
                                {
                                    Status = STATUS_BUFFER_TOO_SMALL;
                                }

                                RxContext->InformationToReturn = cbLocalConnectionName;
                            }
                            else
                            {
                                Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETCONN: LocalConnectionName is NULL!\n"));
                                Status = STATUS_BAD_NETWORK_NAME;
                            }
                        }
                        else
                        {
                            Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETCONN: Index is invalid!\n"));
                            Status = STATUS_INVALID_PARAMETER;
                        }
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                    }

                    if (fMutexAcquired)
                    {
                        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
                        fMutexAcquired = FALSE;
                    }

                    break;
                }

                case IOCTL_MRX_NEMU_GETGLOBALCONN:
                {
                    ULONG cbConnectId = LowIoContext->ParamsFor.IoCtl.InputBufferLength;
                    uint8_t *pu8ConnectId = (uint8_t *)LowIoContext->ParamsFor.IoCtl.pInputBuffer;
                    ULONG cbRemoteName = LowIoContext->ParamsFor.IoCtl.OutputBufferLength;
                    PWCHAR pwcRemoteName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pOutputBuffer;

                    int nemuRC;
                    PSHFLSTRING pString;

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETGLOBALCONN\n"));

                    RxContext->InformationToReturn = 0;

                    if (   !pDeviceExtension
                        || cbConnectId < sizeof(uint8_t))
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    /* Allocate empty string where the host can store cbRemoteName bytes. */
                    Status = vbsfShflStringFromUnicodeAlloc(&pString, NULL, (uint16_t)cbRemoteName);
                    if (Status != STATUS_SUCCESS)
                        break;

                    __try
                    {
                        Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETGLOBALCONN: Connection ID = %d\n",
                             *pu8ConnectId));

                        nemuRC = VbglR0SfQueryMapName(&pDeviceExtension->hgcmClient,
                                                      (*pu8ConnectId) & ~0x80 /** @todo fix properly */,
                                                      pString, ShflStringSizeOfBuffer(pString));
                        if (   nemuRC == VINF_SUCCESS
                            && pString->u16Length < cbRemoteName)
                        {
                            RtlCopyMemory(pwcRemoteName, pString->String.ucs2, pString->u16Length);
                            Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_GETGLOBALCONN: Returned name = %.*ls, Len = %d\n",
                                 pString->u16Length / sizeof(WCHAR), pwcRemoteName, pString->u16Length));
                            RxContext->InformationToReturn = pString->u16Length;
                        }
                        else
                        {
                            Status = STATUS_BAD_NETWORK_NAME;
                        }
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                    }

                    vbsfFreeNonPagedMem(pString);

                    break;
                }

                case IOCTL_MRX_NEMU_START:
                {
                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: capFobx %p\n",
                         capFobx));

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: process: current 0x%X, RDBSS 0x%X\n",
                         IoGetCurrentProcess(), RxGetRDBSSProcess()));

                    switch (NemuMRxState)
                    {
                        case MRX_NEMU_STARTABLE:

                            Log(("NEMUSF: MRxDevFcbXXXControlFile: MRX_NEMU_STARTABLE\n"));

                            if (capFobx)
                            {
                                Status = STATUS_INVALID_DEVICE_REQUEST;
                                break;;
                            }

                            InterlockedCompareExchange((PLONG)&NemuMRxState, MRX_NEMU_START_IN_PROGRESS, MRX_NEMU_STARTABLE);

                        case MRX_NEMU_START_IN_PROGRESS:
                            Status = RxStartMinirdr(RxContext, &RxContext->PostRequest);

                            Log(("NEMUSF: MRxDevFcbXXXControlFile: MRX_NEMU_START_IN_PROGRESS RxStartMiniRdr Status 0x%08X, post %d\n",
                                 Status, RxContext->PostRequest));

                            if (Status == STATUS_REDIRECTOR_STARTED)
                            {
                                Status = STATUS_SUCCESS;
                                break;
                            }

                            if (   Status == STATUS_PENDING
                                && RxContext->PostRequest == TRUE)
                            {
                                /* Will be restarted in RDBSS process. */
                                Status = STATUS_MORE_PROCESSING_REQUIRED;
                                break;
                            }

                            /* Allow restricted users to use shared folders; works only in XP and Vista. (@@todo hack) */
                            if (Status == STATUS_SUCCESS)
                            {
                                SECURITY_DESCRIPTOR SecurityDescriptor;
                                OBJECT_ATTRIBUTES InitializedAttributes;
                                HANDLE hDevice;
                                IO_STATUS_BLOCK IoStatusBlock;
                                UNICODE_STRING UserModeDeviceName;

                                RtlInitUnicodeString(&UserModeDeviceName, DD_MRX_NEMU_USERMODE_SHADOW_DEV_NAME_U);

                                /* Create empty security descriptor */
                                RtlZeroMemory (&SecurityDescriptor, sizeof (SecurityDescriptor));
                                Status = RtlCreateSecurityDescriptor(&SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
                                if (Status != STATUS_SUCCESS)
                                {
                                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: MRX_NEMU_START_IN_PROGRESS: RtlCreateSecurityDescriptor failed with 0x%08X!\n",
                                         Status));
                                    return Status;
                                }

                                RtlZeroMemory (&InitializedAttributes, sizeof (InitializedAttributes));
                                InitializeObjectAttributes(&InitializedAttributes, &UserModeDeviceName, OBJ_KERNEL_HANDLE, 0, 0);

                                /* Open our symbolic link device name */
                                Status = ZwOpenFile(&hDevice, WRITE_DAC, &InitializedAttributes, &IoStatusBlock, 0, 0);
                                if (Status != STATUS_SUCCESS)
                                {
                                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: MRX_NEMU_START_IN_PROGRESS: ZwOpenFile %ls failed with 0x%08X!\n",
                                         DD_MRX_NEMU_USERMODE_SHADOW_DEV_NAME_U, Status));
                                    return Status;
                                }

                                /* Override the discretionary access control list (DACL) settings */
                                Status = ZwSetSecurityObject(hDevice, DACL_SECURITY_INFORMATION, &SecurityDescriptor);
                                if (Status != STATUS_SUCCESS)
                                {
                                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: MRX_NEMU_START_IN_PROGRESS: ZwSetSecurityObject failed with 0x%08X!\n",
                                         Status));
                                    return Status;
                                }

                                Status = ZwClose(hDevice);
                                if (Status != STATUS_SUCCESS)
                                {
                                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: MRX_NEMU_START_IN_PROGRESS: ZwClose failed with 0x%08X\n",
                                         Status));
                                    return Status;
                                }
                            }
                            break;

                        case MRX_NEMU_STARTED:
                            Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: MRX_NEMU_STARTED: Already started\n"));
                            Status = STATUS_SUCCESS;
                            break;

                        default:
                            Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: Invalid state (%d)!\n",
                                 NemuMRxState));
                            Status = STATUS_INVALID_PARAMETER;
                            break;
                    }

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_START: Returned 0x%08X\n",
                         Status));
                    break;
                }

                case IOCTL_MRX_NEMU_STOP:
                {
                    MRX_NEMU_STATE CurrentState;

                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_STOP: capFobx %p\n",
                         capFobx));

                    if (capFobx)
                    {
                        Status = STATUS_INVALID_DEVICE_REQUEST;
                        break;
                    }

                    if (RxContext->RxDeviceObject->NumberOfActiveFcbs > 0)
                    {
                        Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_STOP: Open handles = %d\n",
                             RxContext->RxDeviceObject->NumberOfActiveFcbs));
                        Status = STATUS_REDIRECTOR_HAS_OPEN_HANDLES;
                        break;
                    }

                    CurrentState = (MRX_NEMU_STATE)InterlockedCompareExchange((PLONG) & NemuMRxState, MRX_NEMU_STARTABLE, MRX_NEMU_STARTED);

                    Status = RxStopMinirdr(RxContext, &RxContext->PostRequest);
                    Log(("NEMUSF: MRxDevFcbXXXControlFile: IOCTL_MRX_NEMU_STOP: Returned 0x%08X\n",
                         Status));

                    if (Status == STATUS_PENDING && RxContext->PostRequest == TRUE)
                        Status = STATUS_MORE_PROCESSING_REQUIRED;
                    break;
                }

                default:
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
            }
            break;
        }

        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        default:
            Log(("NEMUSF: MRxDevFcbXXXControlFile: unimplemented major function 0x%02X\n",
                 RxContext->MajorFunction));
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Log(("NEMUSF: MRxDevFcbXXXControlFile: Status = 0x%08X, Info = 0x%08X\n",
         Status, RxContext->InformationToReturn));

    return Status;
}

static NTSTATUS vbsfVerifyConnectionName(PUNICODE_STRING ConnectionName)
{
    /* Check that the connection name is valid:
     * "\Device\NemuMiniRdr\;X:\nemusvr\sf"
     */
    NTSTATUS Status = STATUS_BAD_NETWORK_NAME;

    ULONG i;
    PWCHAR pwc;
    PWCHAR pwc1;

    static PWCHAR spwszPrefix = L"\\Device\\NemuMiniRdr\\;";

    /* Unicode chars in the string. */
    ULONG cConnectionName = ConnectionName->Length / sizeof(WCHAR);
    ULONG cRemainingName;

    /* Check that the name starts with correct prefix. */
    pwc1 = &spwszPrefix[0];
    pwc = ConnectionName->Buffer;
    for (i = 0; i < cConnectionName; i++, pwc1++, pwc++)
    {
        if (*pwc1 == 0 || *pwc == 0 || *pwc1 != *pwc)
            break;
    }

    cRemainingName = cConnectionName - i;

    Log(("NEMUSF: vbsfVerifyConnectionName: prefix %d remaining %d [%.*ls]\n",
         *pwc1 == 0, cRemainingName, cRemainingName, &ConnectionName->Buffer[i]));

    if (*pwc1 == 0)
    {
        /* pwc should point to a drive letter followed by ':\' that is at least 3 chars more. */
        if (cRemainingName >= 3)
        {
           if (   pwc[0] >= L'A' && pwc[0] <= L'Z'
               && pwc[1] == L':')
           {
               pwc += 2;
               cRemainingName -= 2;

               /* @todo should also check that the drive letter corresponds to the name. */
               if (nemuIsPrefixOK(pwc, cRemainingName * sizeof (WCHAR)))
                   Status = STATUS_SUCCESS;
           }
        }
    }

    return Status;
}

static HANDLE vbsfOpenConnectionHandle(PUNICODE_STRING ConnectionName)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES ObjectAttributes;

    HANDLE Handle = INVALID_HANDLE_VALUE;

    Log(("NEMUSF: vbsfOpenConnectionHandle: ConnectionName = %.*ls\n",
         ConnectionName->Length / sizeof(WCHAR), ConnectionName->Buffer));

    Status = vbsfVerifyConnectionName(ConnectionName);

    if (NT_SUCCESS(Status))
    {
        /* Have to create a OBJ_KERNEL_HANDLE. Otherwise the driver verifier on Windows 7 bugchecks. */
        InitializeObjectAttributes(&ObjectAttributes,
                                   ConnectionName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);

        Status = ZwCreateFile(&Handle,
                              SYNCHRONIZE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              NULL,
                              FILE_ATTRIBUTE_NORMAL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              FILE_OPEN_IF,
                              FILE_CREATE_TREE_CONNECTION | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL,
                              0);
    }

    if (   Status != STATUS_SUCCESS
        || Handle == INVALID_HANDLE_VALUE)
    {
        Log(("NEMUSF: vbsfOpenConnectionHandle: ZwCreateFile failed status 0x%08X or invalid handle!\n",
             Status));
        Handle = INVALID_HANDLE_VALUE;
    }

    return Handle;
}

NTSTATUS vbsfCreateConnection(IN PRX_CONTEXT RxContext, OUT PBOOLEAN PostToFsp)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension;

    PLOWIO_CONTEXT LowIoContext;
    ULONG cbConnectName;
    PWCHAR pwcConnectName;

    HANDLE Handle;
    UNICODE_STRING FileName;

    BOOLEAN fMutexAcquired = FALSE;

    Log(("NEMUSF: vbsfCreateConnection\n"));

    if (!BooleanFlagOn(RxContext->Flags, RX_CONTEXT_FLAG_WAIT))
    {
        Log(("NEMUSF: vbsfCreateConnection: post to file system process\n"));
        *PostToFsp = TRUE;
        return STATUS_PENDING;
    }

    pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    if (!pDeviceExtension)
        return STATUS_INVALID_PARAMETER;

    LowIoContext = &RxContext->LowIoContext;
    cbConnectName = LowIoContext->ParamsFor.IoCtl.InputBufferLength;
    pwcConnectName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pInputBuffer;

    if (cbConnectName == 0 || !pwcConnectName)
    {
        Log(("NEMUSF: vbsfCreateConnection: Connection name / length is invalid!\n"));
        return STATUS_INVALID_PARAMETER;
    }

    __try
    {
        Log(("NEMUSF: vbsfCreateConnection: Name = %.*ls, Len = %d\n",
             cbConnectName / sizeof(WCHAR), pwcConnectName, cbConnectName));

        FileName.Buffer = pwcConnectName;
        FileName.Length = (USHORT)cbConnectName;
        FileName.MaximumLength = (USHORT)cbConnectName;

        Handle = vbsfOpenConnectionHandle(&FileName);

        if (Handle != INVALID_HANDLE_VALUE)
        {
            PWCHAR pwc;
            ULONG i;

            ZwClose(Handle);

            /* Skip the "\Device\NemuMiniRdr\;X:" of the string "\Device\NemuMiniRdr\;X:\nemusrv\sf" */
            pwc = pwcConnectName;
            for (i = 0; i < cbConnectName; i += sizeof(WCHAR))
            {
                if (*pwc == L':')
                    break;
                pwc++;
            }

            if (i >= sizeof(WCHAR) && i < cbConnectName)
            {
                pwc--; /* Go back to the drive letter, "X" for example. */

                if (*pwc >= L'A' && *pwc <= L'Z') /* Are we in range? */
                {
                    uint32_t idx = *pwc - L'A'; /* Get the index based on the driver letter numbers (26). */

                    if (idx >= RTL_NUMBER_OF(pDeviceExtension->cLocalConnections))
                    {
                        Log(("NEMUSF: vbsfCreateConnection: Index 0x%x is invalid!\n",
                             idx));
                        Status = STATUS_BAD_NETWORK_NAME;
                    }
                    else
                    {
                        ExAcquireFastMutex(&pDeviceExtension->mtxLocalCon);
                        fMutexAcquired = TRUE;

                        if (pDeviceExtension->wszLocalConnectionName[idx] != NULL)
                        {
                            Log(("NEMUSF: vbsfCreateConnection: LocalConnectionName at index %d is NOT empty!\n",
                                 idx));
                        }

                        pDeviceExtension->wszLocalConnectionName[idx] = (PUNICODE_STRING)vbsfAllocNonPagedMem(sizeof(UNICODE_STRING) + cbConnectName);

                        if (!pDeviceExtension->wszLocalConnectionName[idx])
                        {
                            Log(("NEMUSF: vbsfCreateConnection: LocalConnectionName at index %d NOT allocated!\n",
                                 idx));
                            Status = STATUS_INSUFFICIENT_RESOURCES;
                        }
                        else
                        {
                            PUNICODE_STRING pRemoteName = pDeviceExtension->wszLocalConnectionName[idx];

                            pRemoteName->Buffer = (PWSTR)(pRemoteName + 1);
                            pRemoteName->Length = (USHORT)(cbConnectName - i - sizeof(WCHAR));
                            pRemoteName->MaximumLength = pRemoteName->Length;
                            RtlCopyMemory(&pRemoteName->Buffer[0], pwc+2, pRemoteName->Length);

                            Log(("NEMUSF: vbsfCreateConnection: RemoteName %.*ls, Len = %d\n",
                                 pRemoteName->Length / sizeof(WCHAR), pRemoteName->Buffer, pRemoteName->Length));

                            pDeviceExtension->cLocalConnections[idx] = TRUE;
                        }

                        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
                        fMutexAcquired = FALSE;
                    }
                }
            }
            else
            {
                Log(("NEMUSF: vbsfCreateConnection: bad format\n"));
                Status = STATUS_BAD_NETWORK_NAME;
            }
        }
        else
        {
            Log(("NEMUSF: vbsfCreateConnection: connection was not found\n"));
            Status = STATUS_BAD_NETWORK_NAME;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (fMutexAcquired)
    {
        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
        fMutexAcquired = FALSE;
    }

    return Status;
}

NTSTATUS vbsfDeleteConnection(IN PRX_CONTEXT RxContext, OUT PBOOLEAN PostToFsp)
{
    NTSTATUS Status;
    UNICODE_STRING FileName;
    HANDLE Handle;
    PLOWIO_CONTEXT LowIoContext;
    PWCHAR pwcConnectName;
    ULONG cbConnectName;
    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension;

    BOOLEAN fMutexAcquired = FALSE;

    Log(("NEMUSF: vbsfDeleteConnection\n"));

    if (!BooleanFlagOn(RxContext->Flags, RX_CONTEXT_FLAG_WAIT))
    {
        Log(("NEMUSF: vbsfDeleteConnection: post to file system process\n"));
        *PostToFsp = TRUE;
        return STATUS_PENDING;
    }

    LowIoContext = &RxContext->LowIoContext;
    pwcConnectName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pInputBuffer;
    cbConnectName = LowIoContext->ParamsFor.IoCtl.InputBufferLength;

    pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    if (!pDeviceExtension)
        return STATUS_INVALID_PARAMETER;

    __try
    {
        Log(("NEMUSF: vbsfDeleteConnection: pwcConnectName = %.*ls\n",
             cbConnectName / sizeof(WCHAR), pwcConnectName));

        FileName.Buffer = pwcConnectName;
        FileName.Length = (USHORT)cbConnectName;
        FileName.MaximumLength = (USHORT)cbConnectName;

        Handle = vbsfOpenConnectionHandle(&FileName);

        if (Handle != INVALID_HANDLE_VALUE)
        {
            PFILE_OBJECT pFileObject;
            Status = ObReferenceObjectByHandle(Handle, 0L, NULL, KernelMode, (PVOID *)&pFileObject, NULL);

            Log(("NEMUSF: vbsfDeleteConnection: ObReferenceObjectByHandle Status 0x%08X\n",
                 Status));

            if (NT_SUCCESS(Status))
            {
                PFOBX Fobx = (PFOBX)pFileObject->FsContext2;
                Log(("NEMUSF: vbsfDeleteConnection: Fobx %p\n", Fobx));

                if (Fobx && NodeType(Fobx) == RDBSS_NTC_V_NETROOT)
                {
                    PV_NET_ROOT VNetRoot = (PV_NET_ROOT)Fobx;

                    Status = RxFinalizeConnection(VNetRoot->NetRoot, VNetRoot, TRUE);
                }
                else
                {
                    Log(("NEMUSF: vbsfDeleteConnection: wrong FsContext2\n"));
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                }

                ObDereferenceObject(pFileObject);
            }

            ZwClose(Handle);
        }

        if (NT_SUCCESS(Status))
        {
            PWCHAR pwc;
            ULONG i;

            /* Skip the "\Device\NemuMiniRdr\;X:" of the string "\Device\NemuMiniRdr\;X:\nemusrv\sf" */
            pwc = pwcConnectName;
            for (i = 0; i < cbConnectName; i += sizeof(WCHAR))
            {
                if (*pwc == L':')
                {
                    break;
                }
                pwc++;
            }

            if (i >= sizeof(WCHAR) && i < cbConnectName)
            {
                pwc--;

                if (*pwc >= L'A' && *pwc <= L'Z')
                {
                    uint32_t idx = *pwc - L'A';

                    if (idx >= RTL_NUMBER_OF(pDeviceExtension->cLocalConnections))
                    {
                        Log(("NEMUSF: vbsfDeleteConnection: Index 0x%x is invalid!\n",
                             idx));
                        Status = STATUS_BAD_NETWORK_NAME;
                    }
                    else
                    {
                        ExAcquireFastMutex(&pDeviceExtension->mtxLocalCon);
                        fMutexAcquired = TRUE;

                        pDeviceExtension->cLocalConnections[idx] = FALSE;

                        /* Free saved name */
                        if (pDeviceExtension->wszLocalConnectionName[idx])
                        {
                            vbsfFreeNonPagedMem(pDeviceExtension->wszLocalConnectionName[idx]);
                            pDeviceExtension->wszLocalConnectionName[idx] = NULL;
                        }

                        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
                        fMutexAcquired = FALSE;

                        Log(("NEMUSF: vbsfDeleteConnection: deleted index 0x%x\n",
                             idx));
                    }
                }
            }
            else
            {
                Log(("NEMUSF: vbsfCreateConnection: bad format\n"));
                Status = STATUS_BAD_NETWORK_NAME;
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (fMutexAcquired)
    {
        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
        fMutexAcquired = FALSE;
    }

    Log(("NEMUSF: vbsfDeleteConnection: Status 0x%08X\n", Status));
    return Status;
}

NTSTATUS NemuMRxQueryEaInfo(IN OUT PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxQueryEaInfo: Ea buffer len remaining is %d\n",
         RxContext->Info.LengthRemaining));
    return STATUS_SUCCESS;
}

NTSTATUS NemuMRxSetEaInfo(IN OUT PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxSetEaInfo\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NemuMRxFsCtl (IN OUT PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxFsCtl\n"));
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS NemuMRxNotifyChangeDirectory(IN OUT PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxNotifyChangeDirectory\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NemuMRxQuerySdInfo(IN OUT PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxQuerySdInfo\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NemuMRxSetSdInfo(IN OUT struct _RX_CONTEXT * RxContext)
{
    Log(("NEMUSF: MRxSetSdInfo\n"));
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * WML stubs which are referenced by rdbsslib.
 */
NTSTATUS WmlTinySystemControl(IN OUT PVOID pWmiLibInfo, IN PVOID pDevObj, IN PVOID pIrp)
{
    return STATUS_WMI_GUID_NOT_FOUND;
}

ULONG WmlTrace(IN ULONG ulType, IN PVOID pTraceUuid, IN ULONG64 ullLogger, ...)
{
    return STATUS_SUCCESS;
}
