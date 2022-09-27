/* $Id: vbsf.h $ */
/** @file
 *
 * VirtualBox Windows Guest Shared Folders
 *
 * File System Driver header file
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

#ifndef VBSF_H
#define VBSF_H

/*
 * This must be defined before including RX headers.
 */
#define MINIRDR__NAME NemuMRx
#define ___MINIRDR_IMPORTS_NAME (NemuMRxDeviceObject->RdbssExports)

/*
 * System and RX headers.
 */
#include <ntifs.h>
#include <windef.h>

#include "rx.h"

/*
 * Nemu shared folders.
 */
#include "vbsfhlp.h"
#include "vbsfshared.h"

extern PRDBSS_DEVICE_OBJECT NemuMRxDeviceObject;

/*
 * Maximum drive letters (A - Z).
 */
#define _MRX_MAX_DRIVE_LETTERS 26

/*
 * The shared folders device extension.
 */
typedef struct _MRX_NEMU_DEVICE_EXTENSION
{
    /* The shared folders device object pointer. */
    PRDBSS_DEVICE_OBJECT pDeviceObject;

    /*
     * Keep a list of local connections used.
     * The size (_MRX_MAX_DRIVE_LETTERS = 26) of the array presents the available drive letters C: - Z: of Windows.
     */
    CHAR cLocalConnections[_MRX_MAX_DRIVE_LETTERS];
    PUNICODE_STRING wszLocalConnectionName[_MRX_MAX_DRIVE_LETTERS];
    FAST_MUTEX mtxLocalCon;

    /* The HGCM client information. */
    VBGLSFCLIENT hgcmClient;

    /* Saved pointer to the original IRP_MJ_DEVICE_CONTROL handler. */
    NTSTATUS (* pfnRDBSSDeviceControl) (PDEVICE_OBJECT pDevObj, PIRP pIrp);

} MRX_NEMU_DEVICE_EXTENSION, *PMRX_NEMU_DEVICE_EXTENSION;

/*
 * The shared folders NET_ROOT extension.
 */
typedef struct _MRX_NEMU_NETROOT_EXTENSION
{
    /* The pointert to HGCM client information in device extension. */
    VBGLSFCLIENT *phgcmClient;

    /* The shared folder map handle of this netroot. */
    VBGLSFMAP map;
} MRX_NEMU_NETROOT_EXTENSION, *PMRX_NEMU_NETROOT_EXTENSION;

#define NEMU_FOBX_F_INFO_CREATION_TIME   0x01
#define NEMU_FOBX_F_INFO_LASTACCESS_TIME 0x02
#define NEMU_FOBX_F_INFO_LASTWRITE_TIME  0x04
#define NEMU_FOBX_F_INFO_CHANGE_TIME     0x08
#define NEMU_FOBX_F_INFO_ATTRIBUTES      0x10

/*
 * The shared folders file extension.
 */
typedef struct _MRX_NEMU_FOBX_
{
    SHFLHANDLE hFile;
    PMRX_SRV_CALL pSrvCall;
    FILE_BASIC_INFORMATION FileBasicInfo;
    FILE_STANDARD_INFORMATION FileStandardInfo;
    BOOLEAN fKeepCreationTime;
    BOOLEAN fKeepLastAccessTime;
    BOOLEAN fKeepLastWriteTime;
    BOOLEAN fKeepChangeTime;
    BYTE SetFileInfoOnCloseFlags;
} MRX_NEMU_FOBX, *PMRX_NEMU_FOBX;

#define NemuMRxGetDeviceExtension(RxContext) \
        (PMRX_NEMU_DEVICE_EXTENSION)((PBYTE)(RxContext->RxDeviceObject) + sizeof(RDBSS_DEVICE_OBJECT))

#define NemuMRxGetNetRootExtension(pNetRoot) \
        (((pNetRoot) == NULL) ? NULL : (PMRX_NEMU_NETROOT_EXTENSION)((pNetRoot)->Context))

#define NemuMRxGetSrvOpenExtension(pSrvOpen)  \
        (((pSrvOpen) == NULL) ? NULL : (PMRX_NEMU_SRV_OPEN)((pSrvOpen)->Context))

#define NemuMRxGetFileObjectExtension(pFobx)  \
        (((pFobx) == NULL) ? NULL : (PMRX_NEMU_FOBX)((pFobx)->Context))

/*
 * Prototypes for the dispatch table routines.
 */
NTSTATUS NemuMRxStart(IN OUT struct _RX_CONTEXT * RxContext,
                      IN OUT PRDBSS_DEVICE_OBJECT RxDeviceObject);
NTSTATUS NemuMRxStop(IN OUT struct _RX_CONTEXT * RxContext,
                     IN OUT PRDBSS_DEVICE_OBJECT RxDeviceObject);

NTSTATUS NemuMRxCreate(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxCollapseOpen(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxShouldTryToCollapseThisOpen(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxFlush(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxTruncate(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxCleanupFobx(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxCloseSrvOpen(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxDeallocateForFcb(IN OUT PMRX_FCB pFcb);
NTSTATUS NemuMRxDeallocateForFobx(IN OUT PMRX_FOBX pFobx);
NTSTATUS NemuMRxForceClosed(IN OUT PMRX_SRV_OPEN SrvOpen);

NTSTATUS NemuMRxQueryDirectory(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxQueryFileInfo(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxSetFileInfo(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxSetFileInfoAtCleanup(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxQueryEaInfo(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxSetEaInfo(IN OUT struct _RX_CONTEXT * RxContext);
NTSTATUS NemuMRxQuerySdInfo(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxSetSdInfo(IN OUT struct _RX_CONTEXT * RxContext);
NTSTATUS NemuMRxQueryVolumeInfo(IN OUT PRX_CONTEXT RxContext);

NTSTATUS NemuMRxComputeNewBufferingState(IN OUT PMRX_SRV_OPEN pSrvOpen,
                                         IN PVOID pMRxContext,
                                         OUT ULONG *pNewBufferingState);

NTSTATUS NemuMRxRead(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxWrite(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxLocks(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxFsCtl(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxIoCtl(IN OUT PRX_CONTEXT RxContext);
NTSTATUS NemuMRxNotifyChangeDirectory(IN OUT PRX_CONTEXT RxContext);

NTSTATUS NemuMRxExtendStub(IN OUT struct _RX_CONTEXT * RxContext,
                           IN OUT PLARGE_INTEGER pNewFileSize,
                           OUT PLARGE_INTEGER pNewAllocationSize);
NTSTATUS NemuMRxCompleteBufferingStateChangeRequest(IN OUT PRX_CONTEXT RxContext,
                                                    IN OUT PMRX_SRV_OPEN SrvOpen,
                                                    IN PVOID pContext);

NTSTATUS NemuMRxCreateVNetRoot(IN OUT PMRX_CREATENETROOT_CONTEXT pContext);
NTSTATUS NemuMRxFinalizeVNetRoot(IN OUT PMRX_V_NET_ROOT pVirtualNetRoot,
                                 IN PBOOLEAN ForceDisconnect);
NTSTATUS NemuMRxFinalizeNetRoot(IN OUT PMRX_NET_ROOT pNetRoot,
                                IN PBOOLEAN ForceDisconnect);
NTSTATUS NemuMRxUpdateNetRootState(IN PMRX_NET_ROOT pNetRoot);
VOID NemuMRxExtractNetRootName(IN PUNICODE_STRING FilePathName,
                               IN PMRX_SRV_CALL SrvCall,
                               OUT PUNICODE_STRING NetRootName,
                               OUT PUNICODE_STRING RestOfName OPTIONAL);

NTSTATUS NemuMRxCreateSrvCall(PMRX_SRV_CALL pSrvCall,
                              PMRX_SRVCALL_CALLBACK_CONTEXT pCallbackContext);
NTSTATUS NemuMRxSrvCallWinnerNotify(IN OUT PMRX_SRV_CALL pSrvCall,
                                    IN BOOLEAN ThisMinirdrIsTheWinner,
                                    IN OUT PVOID pSrvCallContext);
NTSTATUS NemuMRxFinalizeSrvCall(PMRX_SRV_CALL pSrvCall,
                                BOOLEAN Force);

NTSTATUS NemuMRxDevFcbXXXControlFile(IN OUT PRX_CONTEXT RxContext);

/*
 * Support functions.
 */
NTSTATUS vbsfDeleteConnection(IN PRX_CONTEXT RxContext,
                              OUT PBOOLEAN PostToFsp);
NTSTATUS vbsfCreateConnection(IN PRX_CONTEXT RxContext,
                              OUT PBOOLEAN PostToFsp);

NTSTATUS vbsfSetEndOfFile(IN OUT struct _RX_CONTEXT * RxContext,
                          IN OUT PLARGE_INTEGER pNewFileSize,
                          OUT PLARGE_INTEGER pNewAllocationSize);
NTSTATUS vbsfRename(IN PRX_CONTEXT RxContext,
                    IN FILE_INFORMATION_CLASS FileInformationClass,
                    IN PVOID pBuffer,
                    IN ULONG BufferLength);
NTSTATUS vbsfRemove(IN PRX_CONTEXT RxContext);
NTSTATUS vbsfCloseFileHandle(PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension,
                             PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension,
                             PMRX_NEMU_FOBX pNemuFobx);

#endif /* VBSF_H */
