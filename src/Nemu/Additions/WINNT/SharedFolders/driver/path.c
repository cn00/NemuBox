/* $Id: path.c $ */
/** @file
 *
 * VirtualBox Windows Guest Shared Folders
 *
 * File System Driver path related routines
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

static UNICODE_STRING UnicodeBackslash = { 2, 4, L"\\" };

static NTSTATUS vbsfProcessCreate(PRX_CONTEXT RxContext,
                                  PUNICODE_STRING RemainingName,
                                  FILE_BASIC_INFORMATION *pFileBasicInfo,
                                  FILE_STANDARD_INFORMATION *pFileStandardInfo,
                                  PVOID EaBuffer,
                                  ULONG EaLength,
                                  ULONG *pulCreateAction,
                                  SHFLHANDLE *pHandle)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension = NemuMRxGetNetRootExtension(capFcb->pNetRoot);

    int nemuRC = VINF_SUCCESS;

    /* Various boolean flags. */
    struct
    {
        ULONG CreateDirectory :1;
        ULONG OpenDirectory :1;
        ULONG DirectoryFile :1;
        ULONG NonDirectoryFile :1;
        ULONG DeleteOnClose :1;
        ULONG TemporaryFile :1;
    } bf;

    ACCESS_MASK DesiredAccess;
    ULONG Options;
    UCHAR FileAttributes;
    ULONG ShareAccess;
    ULONG CreateDisposition;
    SHFLCREATEPARMS *pCreateParms = NULL;

    if (EaLength)
    {
        Log(("NEMUSF: vbsfProcessCreate: Unsupported: extended attributes!\n"));
        Status = STATUS_NOT_SUPPORTED;
        goto failure;
    }

    if (BooleanFlagOn(capFcb->FcbState, FCB_STATE_PAGING_FILE))
    {
        Log(("NEMUSF: vbsfProcessCreate: Unsupported: paging file!\n"));
        Status = STATUS_NOT_IMPLEMENTED;
        goto failure;
    }

    Log(("NEMUSF: vbsfProcessCreate: FileAttributes = 0x%08x\n",
         RxContext->Create.NtCreateParameters.FileAttributes));
    Log(("NEMUSF: vbsfProcessCreate: CreateOptions = 0x%08x\n",
         RxContext->Create.NtCreateParameters.CreateOptions));

    RtlZeroMemory (&bf, sizeof (bf));

    DesiredAccess = RxContext->Create.NtCreateParameters.DesiredAccess;
    Options = RxContext->Create.NtCreateParameters.CreateOptions & FILE_VALID_OPTION_FLAGS;
    FileAttributes = (UCHAR)(RxContext->Create.NtCreateParameters.FileAttributes & ~FILE_ATTRIBUTE_NORMAL);
    ShareAccess = RxContext->Create.NtCreateParameters.ShareAccess;

    /* We do not support opens by file ids. */
    if (FlagOn(Options, FILE_OPEN_BY_FILE_ID))
    {
        Log(("NEMUSF: vbsfProcessCreate: Unsupported: file open by id!\n"));
        Status = STATUS_NOT_IMPLEMENTED;
        goto failure;
    }

    /* Mask out unsupported attribute bits. */
    FileAttributes &= (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);

    bf.DirectoryFile = BooleanFlagOn(Options, FILE_DIRECTORY_FILE);
    bf.NonDirectoryFile = BooleanFlagOn(Options, FILE_NON_DIRECTORY_FILE);
    bf.DeleteOnClose = BooleanFlagOn(Options, FILE_DELETE_ON_CLOSE);
    if (bf.DeleteOnClose)
        Log(("NEMUSF: vbsfProcessCreate: Delete on close!\n"));

    CreateDisposition = RxContext->Create.NtCreateParameters.Disposition;

    bf.CreateDirectory = (BOOLEAN)(bf.DirectoryFile && ((CreateDisposition == FILE_CREATE) || (CreateDisposition == FILE_OPEN_IF)));
    bf.OpenDirectory = (BOOLEAN)(bf.DirectoryFile && ((CreateDisposition == FILE_OPEN) || (CreateDisposition == FILE_OPEN_IF)));
    bf.TemporaryFile = BooleanFlagOn(RxContext->Create.NtCreateParameters.FileAttributes, FILE_ATTRIBUTE_TEMPORARY);

    if (FlagOn(capFcb->FcbState, FCB_STATE_TEMPORARY))
        bf.TemporaryFile = TRUE;

    Log(("NEMUSF: vbsfProcessCreate: bf.TemporaryFile %d, bf.CreateDirectory %d, bf.DirectoryFile = %d\n",
         (ULONG)bf.TemporaryFile, (ULONG)bf.CreateDirectory, (ULONG)bf.DirectoryFile));

    /* Check consistency in specified flags. */
    if (bf.TemporaryFile && bf.CreateDirectory) /* Directories with temporary flag set are not allowed! */
    {
        Log(("NEMUSF: vbsfProcessCreate: Not allowed: Temporary directories!\n"));
        Status = STATUS_INVALID_PARAMETER;
        goto failure;
    }

    if (bf.DirectoryFile && bf.NonDirectoryFile)
    {
        Log(("NEMUSF: vbsfProcessCreate: Unsupported combination: dir && !dir\n"));
        Status = STATUS_INVALID_PARAMETER;
        goto failure;
    }

    /* Initialize create parameters. */
    pCreateParms = (SHFLCREATEPARMS *)vbsfAllocNonPagedMem(sizeof(SHFLCREATEPARMS));
    if (!pCreateParms)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto failure;
    }

    RtlZeroMemory(pCreateParms, sizeof (SHFLCREATEPARMS));

    pCreateParms->Handle = SHFL_HANDLE_NIL;
    pCreateParms->Result = SHFL_NO_RESULT;

    if (bf.DirectoryFile)
    {
        if (CreateDisposition != FILE_CREATE && CreateDisposition != FILE_OPEN && CreateDisposition != FILE_OPEN_IF)
        {
            Log(("NEMUSF: vbsfProcessCreate: Invalid disposition 0x%08X for directory!\n",
                 CreateDisposition));
            Status = STATUS_INVALID_PARAMETER;
            goto failure;
        }

        pCreateParms->CreateFlags |= SHFL_CF_DIRECTORY;
    }

    Log(("NEMUSF: vbsfProcessCreate: CreateDisposition = 0x%08X\n",
         CreateDisposition));

    switch (CreateDisposition)
    {
        case FILE_SUPERSEDE:
            pCreateParms->CreateFlags |= SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            Log(("NEMUSF: vbsfProcessCreate: CreateFlags |= SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW\n"));
            break;

        case FILE_OPEN:
            pCreateParms->CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            Log(("NEMUSF: vbsfProcessCreate: CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW\n"));
            break;

        case FILE_CREATE:
            pCreateParms->CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            Log(("NEMUSF: vbsfProcessCreate: CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW\n"));
            break;

        case FILE_OPEN_IF:
            pCreateParms->CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            Log(("NEMUSF: vbsfProcessCreate: CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW\n"));
            break;

        case FILE_OVERWRITE:
            pCreateParms->CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            Log(("NEMUSF: vbsfProcessCreate: CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW\n"));
            break;

        case FILE_OVERWRITE_IF:
            pCreateParms->CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            Log(("NEMUSF: vbsfProcessCreate: CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW\n"));
            break;

        default:
            Log(("NEMUSF: vbsfProcessCreate: Unexpected create disposition: 0x%08X\n",
                 CreateDisposition));
            Status = STATUS_INVALID_PARAMETER;
            goto failure;
    }

    Log(("NEMUSF: vbsfProcessCreate: DesiredAccess = 0x%08X\n",
         DesiredAccess));
    Log(("NEMUSF: vbsfProcessCreate: ShareAccess   = 0x%08X\n",
         ShareAccess));

    if (DesiredAccess & FILE_READ_DATA)
    {
        Log(("NEMUSF: vbsfProcessCreate: FILE_READ_DATA\n"));
        pCreateParms->CreateFlags |= SHFL_CF_ACCESS_READ;
    }

    if (DesiredAccess & FILE_WRITE_DATA)
    {
        Log(("NEMUSF: vbsfProcessCreate: FILE_WRITE_DATA\n"));
        /* FILE_WRITE_DATA means write access regardless of FILE_APPEND_DATA bit.
         */
        pCreateParms->CreateFlags |= SHFL_CF_ACCESS_WRITE;
    }
    else if (DesiredAccess & FILE_APPEND_DATA)
    {
        Log(("NEMUSF: vbsfProcessCreate: FILE_APPEND_DATA\n"));
        /* FILE_APPEND_DATA without FILE_WRITE_DATA means append only mode.
         *
         * Both write and append access flags are required for shared folders,
         * as on Windows FILE_APPEND_DATA implies write access.
         */
        pCreateParms->CreateFlags |= SHFL_CF_ACCESS_WRITE | SHFL_CF_ACCESS_APPEND;
    }

    if (DesiredAccess & FILE_READ_ATTRIBUTES)
        pCreateParms->CreateFlags |= SHFL_CF_ACCESS_ATTR_READ;
    if (DesiredAccess & FILE_WRITE_ATTRIBUTES)
        pCreateParms->CreateFlags |= SHFL_CF_ACCESS_ATTR_WRITE;

    if (ShareAccess & (FILE_SHARE_READ | FILE_SHARE_WRITE))
        pCreateParms->CreateFlags |= SHFL_CF_ACCESS_DENYNONE;
    else if (ShareAccess & FILE_SHARE_READ)
        pCreateParms->CreateFlags |= SHFL_CF_ACCESS_DENYWRITE;
    else if (ShareAccess & FILE_SHARE_WRITE)
        pCreateParms->CreateFlags |= SHFL_CF_ACCESS_DENYREAD;
    else pCreateParms->CreateFlags |= SHFL_CF_ACCESS_DENYALL;

    /* Set initial allocation size. */
    pCreateParms->Info.cbObject = RxContext->Create.NtCreateParameters.AllocationSize.QuadPart;

    if (FileAttributes == 0)
        FileAttributes = FILE_ATTRIBUTE_NORMAL;

    pCreateParms->Info.Attr.fMode = NTToNemuFileAttributes(FileAttributes);

    {
        PSHFLSTRING ParsedPath;
        Log(("NEMUSF: vbsfProcessCreate: RemainingName->Length = %d\n", RemainingName->Length));

        Status = vbsfShflStringFromUnicodeAlloc(&ParsedPath, RemainingName->Buffer, RemainingName->Length);
        if (Status != STATUS_SUCCESS)
        {
            goto failure;
        }

        Log(("NEMUSF: ParsedPath: %.*ls\n",
             ParsedPath->u16Length / sizeof(WCHAR), ParsedPath->String.ucs2));

        /* Call host. */
        Log(("NEMUSF: vbsfProcessCreate: VbglR0SfCreate called.\n"));
        nemuRC = VbglR0SfCreate(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, ParsedPath, pCreateParms);

        vbsfFreeNonPagedMem(ParsedPath);
    }

    Log(("NEMUSF: vbsfProcessCreate: VbglR0SfCreate returns nemuRC = %Rrc, Result = 0x%x\n",
         nemuRC, pCreateParms->Result));

    if (RT_FAILURE(nemuRC))
    {
        /* Map some NemuRC to STATUS codes expected by the system. */
        switch (nemuRC)
        {
            case VERR_ALREADY_EXISTS:
            {
                *pulCreateAction = FILE_EXISTS;
                Status = STATUS_OBJECT_NAME_COLLISION;
                goto failure;
            }

            /* On POSIX systems, the "mkdir" command returns VERR_FILE_NOT_FOUND when
               doing a recursive directory create. Handle this case. */
            case VERR_FILE_NOT_FOUND:
            {
                pCreateParms->Result = SHFL_PATH_NOT_FOUND;
                break;
            }

            default:
            {
                *pulCreateAction = FILE_DOES_NOT_EXIST;
                Status = NemuErrorToNTStatus(nemuRC);
                goto failure;
            }
        }
    }

    /*
     * The request succeeded. Analyze host response,
     */
    switch (pCreateParms->Result)
    {
        case SHFL_PATH_NOT_FOUND:
        {
            /* Path to object does not exist. */
            Log(("NEMUSF: vbsfProcessCreate: Path not found\n"));
            *pulCreateAction = FILE_DOES_NOT_EXIST;
            Status = STATUS_OBJECT_PATH_NOT_FOUND;
            goto failure;
        }

        case SHFL_FILE_NOT_FOUND:
        {
            Log(("NEMUSF: vbsfProcessCreate: File not found\n"));
            *pulCreateAction = FILE_DOES_NOT_EXIST;
            if (pCreateParms->Handle == SHFL_HANDLE_NIL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                goto failure;
            }

            Log(("NEMUSF: vbsfProcessCreate: File not found but have a handle!\n"));
            Status = STATUS_UNSUCCESSFUL;
            goto failure;

            break;
        }

        case SHFL_FILE_EXISTS:
        {
            Log(("NEMUSF: vbsfProcessCreate: File exists, Handle = 0x%RX64\n",
                 pCreateParms->Handle));
            if (pCreateParms->Handle == SHFL_HANDLE_NIL)
            {
                *pulCreateAction = FILE_EXISTS;
                if (CreateDisposition == FILE_CREATE)
                {
                    /* File was not opened because we requested a create. */
                    Status = STATUS_OBJECT_NAME_COLLISION;
                    goto failure;
                }

                /* Actually we should not go here, unless we have no rights to open the object. */
                Log(("NEMUSF: vbsfProcessCreate: Existing file was not opened!\n"));
                Status = STATUS_ACCESS_DENIED;
                goto failure;
            }

            *pulCreateAction = FILE_OPENED;

            /* Existing file was opened. Go check flags and create FCB. */
            break;
        }

        case SHFL_FILE_CREATED:
        {
            /* A new file was created. */
            Assert(pCreateParms->Handle != SHFL_HANDLE_NIL);

            *pulCreateAction = FILE_CREATED;

            /* Go check flags and create FCB. */
            break;
        }

        case SHFL_FILE_REPLACED:
        {
            /* Existing file was replaced or overwriting. */
            Assert(pCreateParms->Handle != SHFL_HANDLE_NIL);

            if (CreateDisposition == FILE_SUPERSEDE)
                *pulCreateAction = FILE_SUPERSEDED;
            else
                *pulCreateAction = FILE_OVERWRITTEN;
            /* Go check flags and create FCB. */
            break;
        }

        default:
        {
            Log(("NEMUSF: vbsfProcessCreate: Invalid CreateResult from host (0x%08X)\n",
                 pCreateParms->Result));
            *pulCreateAction = FILE_DOES_NOT_EXIST;
            Status = STATUS_OBJECT_PATH_NOT_FOUND;
            goto failure;
        }
    }

    /* Check flags. */
    if (bf.NonDirectoryFile && FlagOn(pCreateParms->Info.Attr.fMode, RTFS_DOS_DIRECTORY))
    {
        /* Caller wanted only a file, but the object is a directory. */
        Log(("NEMUSF: vbsfProcessCreate: File is a directory!\n"));
        Status = STATUS_FILE_IS_A_DIRECTORY;
        goto failure;
    }

    if (bf.DirectoryFile && !FlagOn(pCreateParms->Info.Attr.fMode, RTFS_DOS_DIRECTORY))
    {
        /* Caller wanted only a directory, but the object is not a directory. */
        Log(("NEMUSF: vbsfProcessCreate: File is not a directory!\n"));
        Status = STATUS_NOT_A_DIRECTORY;
        goto failure;
    }

    *pHandle = pCreateParms->Handle;

    /* Translate attributes */
    pFileBasicInfo->FileAttributes = NemuToNTFileAttributes(pCreateParms->Info.Attr.fMode);

    /* Translate file times */
    pFileBasicInfo->CreationTime.QuadPart = RTTimeSpecGetNtTime(&pCreateParms->Info.BirthTime); /* ridiculous name */
    pFileBasicInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pCreateParms->Info.AccessTime);
    pFileBasicInfo->LastWriteTime.QuadPart = RTTimeSpecGetNtTime(&pCreateParms->Info.ModificationTime);
    pFileBasicInfo->ChangeTime.QuadPart = RTTimeSpecGetNtTime(&pCreateParms->Info.ChangeTime);

    if (!FlagOn(pCreateParms->Info.Attr.fMode, RTFS_DOS_DIRECTORY))
    {
        pFileStandardInfo->AllocationSize.QuadPart = pCreateParms->Info.cbAllocated;
        pFileStandardInfo->EndOfFile.QuadPart = pCreateParms->Info.cbObject;
        pFileStandardInfo->Directory = FALSE;

        Log(("NEMUSF: vbsfProcessCreate: AllocationSize = 0x%RX64, EndOfFile = 0x%RX64\n",
             pCreateParms->Info.cbAllocated, pCreateParms->Info.cbObject));
    }
    else
    {
        pFileStandardInfo->AllocationSize.QuadPart = 0;
        pFileStandardInfo->EndOfFile.QuadPart = 0;
        pFileStandardInfo->Directory = TRUE;
    }
    pFileStandardInfo->NumberOfLinks = 0;
    pFileStandardInfo->DeletePending = FALSE;

    vbsfFreeNonPagedMem(pCreateParms);

    return Status;

failure:

    Log(("NEMUSF: vbsfProcessCreate: Returned with status = 0x%08X\n",
          Status));

    if (pCreateParms && pCreateParms->Handle != SHFL_HANDLE_NIL)
    {
        VbglR0SfClose(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, pCreateParms->Handle);
        *pHandle = SHFL_HANDLE_NIL;
    }

    if (pCreateParms)
        vbsfFreeNonPagedMem(pCreateParms);

    return Status;
}

NTSTATUS NemuMRxCreate(IN OUT PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_NET_ROOT pNetRoot = capFcb->pNetRoot;
    PMRX_SRV_OPEN SrvOpen = RxContext->pRelevantSrvOpen;
    PUNICODE_STRING RemainingName = GET_ALREADY_PREFIXED_NAME_FROM_CONTEXT(RxContext);

    FILE_BASIC_INFORMATION FileBasicInfo;
    FILE_STANDARD_INFORMATION FileStandardInfo;

    ULONG CreateAction = FILE_CREATED;
    SHFLHANDLE Handle = SHFL_HANDLE_NIL;
    PMRX_NEMU_FOBX pNemuFobx;

    Log(("NEMUSF: MRxCreate: name ptr %p length=%d, SrvOpen->Flags 0x%08X\n",
         RemainingName, RemainingName->Length, SrvOpen->Flags));

    /* Disable FastIO. It causes a verifier bugcheck. */
#ifdef SRVOPEN_FLAG_DONTUSE_READ_CACHING
    SetFlag(SrvOpen->Flags, SRVOPEN_FLAG_DONTUSE_READ_CACHING | SRVOPEN_FLAG_DONTUSE_WRITE_CACHING);
#else
    SetFlag(SrvOpen->Flags, SRVOPEN_FLAG_DONTUSE_READ_CACHEING | SRVOPEN_FLAG_DONTUSE_WRITE_CACHEING);
#endif

    if (RemainingName->Length)
    {
        Log(("NEMUSF: MRxCreate: Attempt to open %.*ls\n",
             RemainingName->Length/sizeof(WCHAR), RemainingName->Buffer));
    }
    else
    {
        if (FlagOn(RxContext->Create.Flags, RX_CONTEXT_CREATE_FLAG_STRIPPED_TRAILING_BACKSLASH))
        {
            Log(("NEMUSF: MRxCreate: Empty name -> Only backslash used\n"));
            RemainingName = &UnicodeBackslash;
        }
    }

    if (   pNetRoot->Type != NET_ROOT_WILD
        && pNetRoot->Type != NET_ROOT_DISK)
    {
        Log(("NEMUSF: MRxCreate: netroot type %d not supported\n",
             pNetRoot->Type));
        Status = STATUS_NOT_IMPLEMENTED;
        goto Exit;
    }

    FileBasicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;

    Status = vbsfProcessCreate(RxContext,
                               RemainingName,
                               &FileBasicInfo,
                               &FileStandardInfo,
                               RxContext->Create.EaBuffer,
                               RxContext->Create.EaLength,
                               &CreateAction,
                               &Handle);

    if (Status != STATUS_SUCCESS)
    {
        Log(("NEMUSF: MRxCreate: vbsfProcessCreate failed 0x%08X\n",
             Status));
        goto Exit;
    }

    Log(("NEMUSF: MRxCreate: EOF is 0x%RX64 AllocSize is 0x%RX64\n",
         FileStandardInfo.EndOfFile.QuadPart, FileStandardInfo.AllocationSize.QuadPart));

    RxContext->pFobx = RxCreateNetFobx(RxContext, SrvOpen);
    if (!RxContext->pFobx)
    {
        Log(("NEMUSF: MRxCreate: RxCreateNetFobx failed\n"));
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Log(("NEMUSF: MRxCreate: CreateAction = 0x%08X\n",
         CreateAction));

    RxContext->Create.ReturnedCreateInformation = CreateAction;

    if (capFcb->OpenCount == 0)
    {
        FCB_INIT_PACKET InitPacket;
        RxFormInitPacket(InitPacket,
                         &FileBasicInfo.FileAttributes,
                         &FileStandardInfo.NumberOfLinks,
                         &FileBasicInfo.CreationTime,
                         &FileBasicInfo.LastAccessTime,
                         &FileBasicInfo.LastWriteTime,
                         &FileBasicInfo.ChangeTime,
                         &FileStandardInfo.AllocationSize,
                         &FileStandardInfo.EndOfFile,
                         &FileStandardInfo.EndOfFile);
        RxFinishFcbInitialization(capFcb, RDBSS_STORAGE_NTC(FileTypeFile), &InitPacket);
    }

    SrvOpen->BufferingFlags = 0;

    RxContext->pFobx->OffsetOfNextEaToReturn = 1;

    pNemuFobx = NemuMRxGetFileObjectExtension(RxContext->pFobx);

    Log(("NEMUSF: MRxCreate: NemuFobx = %p\n",
         pNemuFobx));

    if (!pNemuFobx)
    {
        Log(("NEMUSF: MRxCreate: no NemuFobx!\n"));
        AssertFailed();
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Log(("NEMUSF: MRxCreate: FileBasicInformation: CreationTime   %RX64\n", FileBasicInfo.CreationTime.QuadPart));
    Log(("NEMUSF: MRxCreate: FileBasicInformation: LastAccessTime %RX64\n", FileBasicInfo.LastAccessTime.QuadPart));
    Log(("NEMUSF: MRxCreate: FileBasicInformation: LastWriteTime  %RX64\n", FileBasicInfo.LastWriteTime.QuadPart));
    Log(("NEMUSF: MRxCreate: FileBasicInformation: ChangeTime     %RX64\n", FileBasicInfo.ChangeTime.QuadPart));
    Log(("NEMUSF: MRxCreate: FileBasicInformation: FileAttributes %RX32\n", FileBasicInfo.FileAttributes));

    pNemuFobx->hFile = Handle;
    pNemuFobx->pSrvCall = RxContext->Create.pSrvCall;
    pNemuFobx->FileStandardInfo = FileStandardInfo;
    pNemuFobx->FileBasicInfo = FileBasicInfo;
    pNemuFobx->fKeepCreationTime = FALSE;
    pNemuFobx->fKeepLastAccessTime = FALSE;
    pNemuFobx->fKeepLastWriteTime = FALSE;
    pNemuFobx->fKeepChangeTime = FALSE;
    pNemuFobx->SetFileInfoOnCloseFlags = 0;

    if (!RxIsFcbAcquiredExclusive(capFcb))
    {
        RxAcquireExclusiveFcbResourceInMRx(capFcb);
    }

    Log(("NEMUSF: MRxCreate: NetRoot is %p, Fcb is %p, SrvOpen is %p, Fobx is %p\n",
         pNetRoot, capFcb, SrvOpen, RxContext->pFobx));
    Log(("NEMUSF: MRxCreate: return 0x%08X\n",
         Status));

Exit:
    return Status;
}

NTSTATUS NemuMRxComputeNewBufferingState(IN OUT PMRX_SRV_OPEN pMRxSrvOpen,
                                         IN PVOID pMRxContext,
                                         OUT PULONG pNewBufferingState)
{
    Log(("NEMUSF: MRxComputeNewBufferingState\n"));
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NemuMRxDeallocateForFcb(IN OUT PMRX_FCB pFcb)
{
    Log(("NEMUSF: MRxDeallocateForFcb\n"));
    return STATUS_SUCCESS;
}

NTSTATUS NemuMRxDeallocateForFobx(IN OUT PMRX_FOBX pFobx)
{
    Log(("NEMUSF: MRxDeallocateForFobx\n"));
    return STATUS_SUCCESS;
}

NTSTATUS NemuMRxTruncate(IN PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxTruncate\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS NemuMRxCleanupFobx(IN PRX_CONTEXT RxContext)
{
    PMRX_NEMU_FOBX pNemuFobx = NemuMRxGetFileObjectExtension(RxContext->pFobx);

    Log(("NEMUSF: MRxCleanupFobx: pNemuFobx = %p, Handle = 0x%RX64\n",
          pNemuFobx, pNemuFobx? pNemuFobx->hFile: 0));

    if (!pNemuFobx)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

NTSTATUS NemuMRxForceClosed(IN PMRX_SRV_OPEN pSrvOpen)
{
    Log(("NEMUSF: MRxForceClosed\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS vbsfSetFileInfo(PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension,
                         PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension,
                         PMRX_NEMU_FOBX pNemuFobx,
                         PFILE_BASIC_INFORMATION pInfo,
                         BYTE SetAttrFlags)
{
    NTSTATUS Status = STATUS_SUCCESS;

    int nemuRC;
    PSHFLFSOBJINFO pSHFLFileInfo;

    uint8_t *pHGCMBuffer = NULL;
    uint32_t cbBuffer = 0;

    Log(("NEMUSF: vbsfSetFileInfo: SetAttrFlags 0x%02X\n", SetAttrFlags));
    Log(("NEMUSF: vbsfSetFileInfo: FileBasicInformation: CreationTime   %RX64\n", pInfo->CreationTime.QuadPart));
    Log(("NEMUSF: vbsfSetFileInfo: FileBasicInformation: LastAccessTime %RX64\n", pInfo->LastAccessTime.QuadPart));
    Log(("NEMUSF: vbsfSetFileInfo: FileBasicInformation: LastWriteTime  %RX64\n", pInfo->LastWriteTime.QuadPart));
    Log(("NEMUSF: vbsfSetFileInfo: FileBasicInformation: ChangeTime     %RX64\n", pInfo->ChangeTime.QuadPart));
    Log(("NEMUSF: vbsfSetFileInfo: FileBasicInformation: FileAttributes %RX32\n", pInfo->FileAttributes));

    if (SetAttrFlags == 0)
    {
        Log(("NEMUSF: vbsfSetFileInfo: nothing to set\n"));
        return STATUS_SUCCESS;
    }

    cbBuffer = sizeof(SHFLFSOBJINFO);
    pHGCMBuffer = (uint8_t *)vbsfAllocNonPagedMem(cbBuffer);
    if (!pHGCMBuffer)
    {
        AssertFailed();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pHGCMBuffer, cbBuffer);
    pSHFLFileInfo = (PSHFLFSOBJINFO)pHGCMBuffer;

    /* The properties, that need to be changed, are set to something other than zero */
    if (pInfo->CreationTime.QuadPart && (SetAttrFlags & NEMU_FOBX_F_INFO_CREATION_TIME) != 0)
        RTTimeSpecSetNtTime(&pSHFLFileInfo->BirthTime, pInfo->CreationTime.QuadPart);
    if (pInfo->LastAccessTime.QuadPart && (SetAttrFlags & NEMU_FOBX_F_INFO_LASTACCESS_TIME) != 0)
        RTTimeSpecSetNtTime(&pSHFLFileInfo->AccessTime, pInfo->LastAccessTime.QuadPart);
    if (pInfo->LastWriteTime.QuadPart && (SetAttrFlags & NEMU_FOBX_F_INFO_LASTWRITE_TIME) != 0)
        RTTimeSpecSetNtTime(&pSHFLFileInfo->ModificationTime, pInfo->LastWriteTime.QuadPart);
    if (pInfo->ChangeTime.QuadPart && (SetAttrFlags & NEMU_FOBX_F_INFO_CHANGE_TIME) != 0)
        RTTimeSpecSetNtTime(&pSHFLFileInfo->ChangeTime, pInfo->ChangeTime.QuadPart);
    if (pInfo->FileAttributes && (SetAttrFlags & NEMU_FOBX_F_INFO_ATTRIBUTES) != 0)
        pSHFLFileInfo->Attr.fMode = NTToNemuFileAttributes(pInfo->FileAttributes);

    nemuRC = VbglR0SfFsInfo(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, pNemuFobx->hFile,
                            SHFL_INFO_SET | SHFL_INFO_FILE, &cbBuffer, (PSHFLDIRINFO)pSHFLFileInfo);

    if (nemuRC != VINF_SUCCESS)
        Status = NemuErrorToNTStatus(nemuRC);

    if (pHGCMBuffer)
        vbsfFreeNonPagedMem(pHGCMBuffer);

    Log(("NEMUSF: vbsfSetFileInfo: Returned 0x%08X\n", Status));
    return Status;
}

/*
 * Closes an opened file handle of a MRX_NEMU_FOBX.
 * Updates file attributes if necessary.
 */
NTSTATUS vbsfCloseFileHandle(PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension,
                             PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension,
                             PMRX_NEMU_FOBX pNemuFobx)
{
    NTSTATUS Status = STATUS_SUCCESS;

    int nemuRC;

    if (pNemuFobx->hFile == SHFL_HANDLE_NIL)
    {
        Log(("NEMUSF: vbsfCloseFileHandle: SHFL_HANDLE_NIL\n"));
        return STATUS_SUCCESS;
    }

    Log(("NEMUSF: vbsfCloseFileHandle: 0x%RX64, on close info 0x%02X\n",
         pNemuFobx->hFile, pNemuFobx->SetFileInfoOnCloseFlags));

    if (pNemuFobx->SetFileInfoOnCloseFlags)
    {
        /* If the file timestamps were set by the user, then update them before closing the handle,
         * to cancel any effect of the file read/write operations on the host.
         */
        Status = vbsfSetFileInfo(pDeviceExtension,
                                 pNetRootExtension,
                                 pNemuFobx,
                                 &pNemuFobx->FileBasicInfo,
                                 pNemuFobx->SetFileInfoOnCloseFlags);
    }

    nemuRC = VbglR0SfClose(&pDeviceExtension->hgcmClient,
                           &pNetRootExtension->map,
                           pNemuFobx->hFile);

    pNemuFobx->hFile = SHFL_HANDLE_NIL;

    if (nemuRC != VINF_SUCCESS)
        Status = NemuErrorToNTStatus(nemuRC);

    Log(("NEMUSF: vbsfCloseFileHandle: Returned 0x%08X\n", Status));
    return Status;
}

NTSTATUS NemuMRxCloseSrvOpen(IN PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension = NemuMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_NEMU_FOBX pNemuFobx = NemuMRxGetFileObjectExtension(capFobx);
    PMRX_SRV_OPEN pSrvOpen = capFobx->pSrvOpen;

    int nemuRC = 0;
    PUNICODE_STRING RemainingName = NULL;

    Log(("NEMUSF: MRxCloseSrvOpen: capFcb = %p, capFobx = %p, pNemuFobx = %p, pSrvOpen = %p\n",
          capFcb, capFobx, pNemuFobx, pSrvOpen));

    RemainingName = pSrvOpen->pAlreadyPrefixedName;

    Log(("NEMUSF: MRxCloseSrvOpen: Remaining name = %.*ls, Len = %d\n",
         RemainingName->Length / sizeof(WCHAR), RemainingName->Buffer, RemainingName->Length));

    if (!pNemuFobx)
        return STATUS_INVALID_PARAMETER;

    if (FlagOn(pSrvOpen->Flags, (SRVOPEN_FLAG_FILE_RENAMED | SRVOPEN_FLAG_FILE_DELETED)))
    {
        /* If we renamed or delete the file/dir, then it's already closed */
        Assert(pNemuFobx->hFile == SHFL_HANDLE_NIL);
        Log(("NEMUSF: MRxCloseSrvOpen: File was renamed, handle 0x%RX64 ignore close.\n",
             pNemuFobx->hFile));
        return STATUS_SUCCESS;
    }

    /* Close file */
    if (pNemuFobx->hFile != SHFL_HANDLE_NIL)
        vbsfCloseFileHandle(pDeviceExtension, pNetRootExtension, pNemuFobx);

    if (capFcb->FcbState & FCB_STATE_DELETE_ON_CLOSE)
    {
        Log(("NEMUSF: MRxCloseSrvOpen: Delete on close. Open count = %d\n",
             capFcb->OpenCount));

        /* Remove file or directory if delete action is pending. */
        if (capFcb->OpenCount == 0)
            Status = vbsfRemove(RxContext);
    }

    return Status;
}

NTSTATUS vbsfRemove(IN PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension = NemuMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_NEMU_FOBX pNemuFobx = NemuMRxGetFileObjectExtension(capFobx);

    PUNICODE_STRING RemainingName = GET_ALREADY_PREFIXED_NAME_FROM_CONTEXT(RxContext);

    int nemuRC;
    PSHFLSTRING ParsedPath = NULL;

    Log(("NEMUSF: vbsfRemove: Delete %.*ls. open count = %d\n",
         RemainingName->Length / sizeof(WCHAR), RemainingName->Buffer, capFcb->OpenCount));

    /* Close file first if not already done. */
    if (pNemuFobx->hFile != SHFL_HANDLE_NIL)
        vbsfCloseFileHandle(pDeviceExtension, pNetRootExtension, pNemuFobx);

    Log(("NEMUSF: vbsfRemove: RemainingName->Length %d\n", RemainingName->Length));
    Status = vbsfShflStringFromUnicodeAlloc(&ParsedPath, RemainingName->Buffer, RemainingName->Length);
    if (Status != STATUS_SUCCESS)
        return Status;

    /* Call host. */
    nemuRC = VbglR0SfRemove(&pDeviceExtension->hgcmClient, &pNetRootExtension->map,
                            ParsedPath,
                            (pNemuFobx->FileStandardInfo.Directory) ? SHFL_REMOVE_DIR : SHFL_REMOVE_FILE);

    if (ParsedPath)
        vbsfFreeNonPagedMem(ParsedPath);

    if (nemuRC == VINF_SUCCESS)
        SetFlag(capFobx->pSrvOpen->Flags, SRVOPEN_FLAG_FILE_DELETED);

    Status = NemuErrorToNTStatus(nemuRC);
    if (nemuRC != VINF_SUCCESS)
        Log(("NEMUSF: vbsfRemove: VbglR0SfRemove failed with %Rrc\n", nemuRC));

    Log(("NEMUSF: vbsfRemove: Returned 0x%08X\n", Status));
    return Status;
}

NTSTATUS vbsfRename(IN PRX_CONTEXT RxContext,
                       IN FILE_INFORMATION_CLASS FileInformationClass,
                       IN PVOID pBuffer,
                       IN ULONG BufferLength)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension = NemuMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_NEMU_FOBX pNemuFobx = NemuMRxGetFileObjectExtension(capFobx);
    PMRX_SRV_OPEN pSrvOpen = capFobx->pSrvOpen;

    PFILE_RENAME_INFORMATION RenameInformation = (PFILE_RENAME_INFORMATION)RxContext->Info.Buffer;
    PUNICODE_STRING RemainingName = GET_ALREADY_PREFIXED_NAME(pSrvOpen, capFcb);

    int nemuRC;
    PSHFLSTRING SrcPath = 0, DestPath = 0;
    ULONG flags;

    Assert(FileInformationClass == FileRenameInformation);

    Log(("NEMUSF: vbsfRename: FileName = %.*ls\n",
         RenameInformation->FileNameLength / sizeof(WCHAR), &RenameInformation->FileName[0]));

    /* Must close the file before renaming it! */
    if (pNemuFobx->hFile != SHFL_HANDLE_NIL)
        vbsfCloseFileHandle(pDeviceExtension, pNetRootExtension, pNemuFobx);

    /* Mark it as renamed, so we do nothing during close */
    SetFlag(pSrvOpen->Flags, SRVOPEN_FLAG_FILE_RENAMED);

    Log(("NEMUSF: vbsfRename: RenameInformation->FileNameLength = %d\n", RenameInformation->FileNameLength));
    Status = vbsfShflStringFromUnicodeAlloc(&DestPath, RenameInformation->FileName, (uint16_t)RenameInformation->FileNameLength);
    if (Status != STATUS_SUCCESS)
        return Status;

    Log(("NEMUSF: vbsfRename: Destination path = %.*ls\n",
         DestPath->u16Length / sizeof(WCHAR), &DestPath->String.ucs2[0]));

    Log(("NEMUSF: vbsfRename: RemainingName->Length = %d\n", RemainingName->Length));
    Status = vbsfShflStringFromUnicodeAlloc(&SrcPath, RemainingName->Buffer, RemainingName->Length);
    if (Status != STATUS_SUCCESS)
    {
        vbsfFreeNonPagedMem(DestPath);
        return Status;
    }

    Log(("NEMUSF: vbsfRename: Source path = %.*ls\n",
         SrcPath->u16Length / sizeof(WCHAR), &SrcPath->String.ucs2[0]));

    /* Call host. */
    flags = pNemuFobx->FileStandardInfo.Directory? SHFL_RENAME_DIR : SHFL_RENAME_FILE;
    if (RenameInformation->ReplaceIfExists)
        flags |= SHFL_RENAME_REPLACE_IF_EXISTS;

    Log(("NEMUSF: vbsfRename: Calling VbglR0SfRename\n"));
    nemuRC = VbglR0SfRename(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, SrcPath, DestPath, flags);

    vbsfFreeNonPagedMem(SrcPath);
    vbsfFreeNonPagedMem(DestPath);

    Status = NemuErrorToNTStatus(nemuRC);
    if (nemuRC != VINF_SUCCESS)
        Log(("NEMUSF: vbsfRename: VbglR0SfRename failed with %Rrc\n", nemuRC));

    Log(("NEMUSF: vbsfRename: Returned 0x%08X\n", Status));
    return Status;
}

NTSTATUS NemuMRxShouldTryToCollapseThisOpen(IN PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxShouldTryToCollapseThisOpen\n"));
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS NemuMRxCollapseOpen(IN OUT PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxCollapseOpen\n"));
    return STATUS_MORE_PROCESSING_REQUIRED;
}
