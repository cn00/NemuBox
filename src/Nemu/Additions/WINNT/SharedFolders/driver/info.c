/* $Id: info.c $ */
/** @file
 *
 * VirtualBox Windows Guest Shared Folders
 *
 * File System Driver query and set information routines
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

/** Macro for copying a SHFLSTRING file name into a FILE_DIRECTORY_INFORMATION structure. */
#define INIT_FILE_NAME(obj, str) \
    do { \
        ULONG cbLength = (str).u16Length; \
        (obj)->FileNameLength = cbLength; \
        RtlCopyMemory((obj)->FileName, &(str).String.ucs2[0], cbLength + 2); \
    } while (0)

NTSTATUS NemuMRxQueryDirectory(IN OUT PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFobx;
    RxCaptureFcb;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension = NemuMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_NEMU_FOBX pNemuFobx = NemuMRxGetFileObjectExtension(capFobx);

    PUNICODE_STRING DirectoryName = GET_ALREADY_PREFIXED_NAME_FROM_CONTEXT(RxContext);
    PUNICODE_STRING Template = &capFobx->UnicodeQueryTemplate;
    FILE_INFORMATION_CLASS FileInformationClass = RxContext->Info.FileInformationClass;
    PCHAR pInfoBuffer = (PCHAR)RxContext->Info.Buffer;
    LONG cbMaxSize = RxContext->Info.Length;
    LONG *pLengthRemaining = (LONG *)&RxContext->Info.LengthRemaining;

    LONG cbToCopy;
    int nemuRC;
    uint8_t *pHGCMBuffer;
    uint32_t index, fSFFlags, cFiles, u32BufSize;
    LONG cbHGCMBuffer;
    PSHFLDIRINFO pDirEntry;

    ULONG *pNextOffset = 0;
    PSHFLSTRING ParsedPath = NULL;

    Log(("NEMUSF: MrxQueryDirectory: FileInformationClass %d, pNemuFobx %p, hFile %RX64, pInfoBuffer %p\n",
         FileInformationClass, pNemuFobx, pNemuFobx->hFile, pInfoBuffer));

    if (!pNemuFobx)
    {
        Log(("NEMUSF: MrxQueryDirectory: pNemuFobx is invalid!\n"));
        return STATUS_INVALID_PARAMETER;
    }

    if (!DirectoryName)
        return STATUS_INVALID_PARAMETER;

    if (DirectoryName->Length == 0)
        Log(("NEMUSF: MrxQueryDirectory: DirectoryName = \\ (null string)\n"));
    else
        Log(("NEMUSF: MrxQueryDirectory: DirectoryName = %.*ls\n",
             DirectoryName->Length / sizeof(WCHAR), DirectoryName->Buffer));

    if (!Template)
        return STATUS_INVALID_PARAMETER;

    if (Template->Length == 0)
        Log(("NEMUSF: MrxQueryDirectory: Template = \\ (null string)\n"));
    else
        Log(("NEMUSF: MrxQueryDirectory: Template = %.*ls\n",
             Template->Length / sizeof(WCHAR), Template->Buffer));

    cbHGCMBuffer = RT_MAX(cbMaxSize, PAGE_SIZE);

    Log(("NEMUSF: MrxQueryDirectory: Allocating cbHGCMBuffer = %d\n",
         cbHGCMBuffer));

    pHGCMBuffer = (uint8_t *)vbsfAllocNonPagedMem(cbHGCMBuffer);
    if (!pHGCMBuffer)
    {
        AssertFailed();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Assume start from the beginning. */
    index = 0;
    if (RxContext->QueryDirectory.IndexSpecified == TRUE)
    {
        Log(("NEMUSF: MrxQueryDirectory: Index specified %d\n",
             index));
        index = RxContext->QueryDirectory.FileIndex;
    }

    fSFFlags = SHFL_LIST_NONE;
    if (RxContext->QueryDirectory.ReturnSingleEntry == TRUE)
    {
        Log(("NEMUSF: MrxQueryDirectory: Query single entry\n"));
        fSFFlags |= SHFL_LIST_RETURN_ONE;
    }

    if (Template->Length)
    {
        ULONG ParsedPathSize, cch;

        /* Calculate size required for parsed path: dir + \ + template + 0. */
        ParsedPathSize = SHFLSTRING_HEADER_SIZE + Template->Length + sizeof(WCHAR);
        if (DirectoryName->Length)
            ParsedPathSize += DirectoryName->Length + sizeof(WCHAR);
        Log(("NEMUSF: MrxQueryDirectory: ParsedPathSize = %d\n", ParsedPathSize));

        ParsedPath = (PSHFLSTRING)vbsfAllocNonPagedMem(ParsedPathSize);
        if (!ParsedPath)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto end;
        }

        if (!ShflStringInitBuffer(ParsedPath, ParsedPathSize))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto end;
        }

        cch = 0;
        if (DirectoryName->Length)
        {
            /* Copy directory name into ParsedPath. */
            RtlCopyMemory(ParsedPath->String.ucs2, DirectoryName->Buffer, DirectoryName->Length);
            cch += DirectoryName->Length / sizeof(WCHAR);

            /* Add terminating backslash. */
            ParsedPath->String.ucs2[cch] = L'\\';
            cch++;
        }

        RtlCopyMemory (&ParsedPath->String.ucs2[cch], Template->Buffer, Template->Length);
        cch += Template->Length / sizeof(WCHAR);

        /* Add terminating nul. */
        ParsedPath->String.ucs2[cch] = 0;

        /* cch is the number of chars without trailing nul. */
        ParsedPath->u16Length = (uint16_t)(cch * sizeof(WCHAR));

        AssertMsg(ParsedPath->u16Length + sizeof(WCHAR) == ParsedPath->u16Size,
                  ("u16Length %d, u16Size %d\n", ParsedPath->u16Length, ParsedPath->u16Size));

        Log(("NEMUSF: MrxQueryDirectory: ParsedPath = %.*ls\n",
             ParsedPath->u16Length / sizeof(WCHAR), ParsedPath->String.ucs2));
    }

    cFiles = 0;

    /* VbglR0SfDirInfo requires a pointer to uint32_t. */
    u32BufSize = cbHGCMBuffer;

    Log(("NEMUSF: MrxQueryDirectory: CallDirInfo: File = 0x%08x, Flags = 0x%08x, Index = %d, u32BufSize = %d\n",
         pNemuFobx->hFile, fSFFlags, index, u32BufSize));
    nemuRC = VbglR0SfDirInfo(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, pNemuFobx->hFile,
                             ParsedPath, fSFFlags, index, &u32BufSize, (PSHFLDIRINFO)pHGCMBuffer, &cFiles);
    Log(("NEMUSF: MrxQueryDirectory: u32BufSize after CallDirInfo = %d, rc = %Rrc\n",
         u32BufSize, nemuRC));

    switch (nemuRC)
    {
        case VINF_SUCCESS:
            /* Nothing to do here. */
            break;

        case VERR_NO_TRANSLATION:
            Log(("NEMUSF: MrxQueryDirectory: Host could not translate entry!\n"));
            break;

        case VERR_NO_MORE_FILES:
            if (cFiles <= 0) /* VERR_NO_MORE_FILES appears at the first lookup when just returning the current dir ".".
                              * So we also have to check for the cFiles counter. */
            {
                /* Not an error, but we have to handle the return value. */
                Log(("NEMUSF: MrxQueryDirectory: Host reported no more files!\n"));

                if (RxContext->QueryDirectory.InitialQuery)
                {
                    /* First call. MSDN on FindFirstFile: "If the function fails because no matching files
                     * can be found, the GetLastError function returns ERROR_FILE_NOT_FOUND."
                     * So map this rc to file not found.
                     */
                    Status = STATUS_NO_SUCH_FILE;
                }
                else
                {
                    /* Search continued. */
                    Status = STATUS_NO_MORE_FILES;
                }
            }
            break;

        case VERR_FILE_NOT_FOUND:
            Status = STATUS_NO_SUCH_FILE;
            Log(("NEMUSF: MrxQueryDirectory: no such file!\n"));
            break;

        default:
            Status = NemuErrorToNTStatus(nemuRC);
            Log(("NEMUSF: MrxQueryDirectory: Error %Rrc from CallDirInfo (cFiles=%d)!\n",
                 nemuRC, cFiles));
            break;
    }

    if (Status != STATUS_SUCCESS)
        goto end;

    /* Verify that the returned buffer length is not greater than the original one. */
    if (u32BufSize > (uint32_t)cbHGCMBuffer)
    {
        Log(("NEMUSF: MrxQueryDirectory: returned buffer size (%u) is invalid!!!\n",
             u32BufSize));
        Status = STATUS_INVALID_NETWORK_RESPONSE;
        goto end;
    }

    /* How many bytes remain in the buffer. */
    cbHGCMBuffer = u32BufSize;

    pDirEntry = (PSHFLDIRINFO)pHGCMBuffer;
    Status = STATUS_SUCCESS;

    Log(("NEMUSF: MrxQueryDirectory: cFiles=%d, Length=%d\n",
         cFiles, cbHGCMBuffer));

    while ((*pLengthRemaining) && (cFiles > 0) && (pDirEntry != NULL))
    {
        int cbEntry = RT_OFFSETOF(SHFLDIRINFO, name.String) + pDirEntry->name.u16Size;

        if (cbEntry > cbHGCMBuffer)
        {
            Log(("NEMUSF: MrxQueryDirectory: Entry size (%d) exceeds the buffer size (%d)!!!\n",
                 cbEntry, cbHGCMBuffer));
            Status = STATUS_INVALID_NETWORK_RESPONSE;
            goto end;
        }

        switch (FileInformationClass)
        {
            case FileDirectoryInformation:
            {
                PFILE_DIRECTORY_INFORMATION pInfo = (PFILE_DIRECTORY_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryDirectory: FileDirectoryInformation\n"));

                cbToCopy = sizeof(FILE_DIRECTORY_INFORMATION);
                /* Struct already contains one char for null terminator. */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pDirEntry->Info.BirthTime); /* ridiculous name */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pDirEntry->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pDirEntry->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pDirEntry->Info.ChangeTime);
                    pInfo->AllocationSize.QuadPart = pDirEntry->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pDirEntry->Info.cbObject;
                    pInfo->FileIndex               = index;
                    pInfo->FileAttributes          = NemuToNTFileAttributes(pDirEntry->Info.Attr.fMode);

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    /* Align to 8 byte boundary */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* last item */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            case FileFullDirectoryInformation:
            {
                PFILE_FULL_DIR_INFORMATION pInfo = (PFILE_FULL_DIR_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryDirectory: FileFullDirectoryInformation\n"));

                cbToCopy = sizeof(FILE_FULL_DIR_INFORMATION);
                /* Struct already contains one char for null terminator. */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pDirEntry->Info.BirthTime); /* ridiculous name */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pDirEntry->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pDirEntry->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pDirEntry->Info.ChangeTime);
                    pInfo->AllocationSize.QuadPart = pDirEntry->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pDirEntry->Info.cbObject;
                    pInfo->EaSize                  = 0;
                    pInfo->FileIndex               = index;
                    pInfo->FileAttributes          = NemuToNTFileAttributes(pDirEntry->Info.Attr.fMode);

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    /* Align to 8 byte boundary */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* last item */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            case FileBothDirectoryInformation:
            {
                PFILE_BOTH_DIR_INFORMATION pInfo = (PFILE_BOTH_DIR_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryDirectory: FileBothDirectoryInformation\n"));

                cbToCopy = sizeof(FILE_BOTH_DIR_INFORMATION);
                /* struct already contains one char for null terminator */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pDirEntry->Info.BirthTime); /* ridiculous name */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pDirEntry->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pDirEntry->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pDirEntry->Info.ChangeTime);
                    pInfo->AllocationSize.QuadPart = pDirEntry->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pDirEntry->Info.cbObject;
                    pInfo->EaSize                  = 0;
                    pInfo->ShortNameLength         = 0; /* @todo ? */
                    pInfo->FileIndex               = index;
                    pInfo->FileAttributes          = NemuToNTFileAttributes(pDirEntry->Info.Attr.fMode);

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    Log(("NEMUSF: MrxQueryDirectory: FileBothDirectoryInformation cbAlloc = %x cbObject = %x\n",
                         pDirEntry->Info.cbAllocated, pDirEntry->Info.cbObject));
                    Log(("NEMUSF: MrxQueryDirectory: FileBothDirectoryInformation cbToCopy = %d, name size=%d name len=%d\n",
                         cbToCopy, pDirEntry->name.u16Size, pDirEntry->name.u16Length));
                    Log(("NEMUSF: MrxQueryDirectory: FileBothDirectoryInformation File name %.*ls (DirInfo)\n",
                         pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));
                    Log(("NEMUSF: MrxQueryDirectory: FileBothDirectoryInformation File name %.*ls (DirEntry)\n",
                         pDirEntry->name.u16Size / sizeof(WCHAR), pDirEntry->name.String.ucs2));

                    /* Align to 8 byte boundary. */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* Last item. */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            case FileIdBothDirectoryInformation:
            {
                PFILE_ID_BOTH_DIR_INFORMATION pInfo = (PFILE_ID_BOTH_DIR_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryDirectory: FileIdBothDirectoryInformation\n"));

                cbToCopy = sizeof(FILE_ID_BOTH_DIR_INFORMATION);
                /* struct already contains one char for null terminator */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pDirEntry->Info.BirthTime); /* ridiculous name */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pDirEntry->Info.AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pDirEntry->Info.ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pDirEntry->Info.ChangeTime);
                    pInfo->AllocationSize.QuadPart = pDirEntry->Info.cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pDirEntry->Info.cbObject;
                    pInfo->EaSize                  = 0;
                    pInfo->ShortNameLength         = 0; /* @todo ? */
                    pInfo->EaSize                  = 0;
                    pInfo->FileId.QuadPart         = 0;
                    pInfo->FileAttributes          = NemuToNTFileAttributes(pDirEntry->Info.Attr.fMode);

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    Log(("NEMUSF: MrxQueryDirectory: FileIdBothDirectoryInformation cbAlloc = 0x%RX64 cbObject = 0x%RX64\n",
                         pDirEntry->Info.cbAllocated, pDirEntry->Info.cbObject));
                    Log(("NEMUSF: MrxQueryDirectory: FileIdBothDirectoryInformation cbToCopy = %d, name size=%d name len=%d\n",
                         cbToCopy, pDirEntry->name.u16Size, pDirEntry->name.u16Length));
                    Log(("NEMUSF: MrxQueryDirectory: FileIdBothDirectoryInformation File name %.*ls (DirInfo)\n",
                         pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));
                    Log(("NEMUSF: MrxQueryDirectory: FileIdBothDirectoryInformation File name %.*ls (DirEntry)\n",
                         pDirEntry->name.u16Size / sizeof(WCHAR), pDirEntry->name.String.ucs2));

                    /* Align to 8 byte boundary. */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* Last item. */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            case FileNamesInformation:
            {
                PFILE_NAMES_INFORMATION pInfo = (PFILE_NAMES_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryDirectory: FileNamesInformation\n"));

                cbToCopy = sizeof(FILE_NAMES_INFORMATION);
                /* Struct already contains one char for null terminator. */
                cbToCopy += pDirEntry->name.u16Size;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->FileIndex = index;

                    INIT_FILE_NAME(pInfo, pDirEntry->name);

                    Log(("NEMUSF: MrxQueryDirectory: FileNamesInformation: File name [%.*ls]\n",
                         pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));

                    /* Align to 8 byte boundary. */
                    cbToCopy = RT_ALIGN(cbToCopy, sizeof(LONGLONG));
                    pInfo->NextEntryOffset = cbToCopy;
                    pNextOffset = &pInfo->NextEntryOffset;
                }
                else
                {
                    pInfo->NextEntryOffset = 0; /* Last item. */
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                break;
            }

            default:
                Log(("NEMUSF: MrxQueryDirectory: Not supported FileInformationClass %d!\n",
                     FileInformationClass));
                Status = STATUS_INVALID_PARAMETER;
                goto end;
        }

        cbHGCMBuffer -= cbEntry;
        pDirEntry = (PSHFLDIRINFO)((uintptr_t)pDirEntry + cbEntry);

        Log(("NEMUSF: MrxQueryDirectory: %d bytes left in HGCM buffer\n",
             cbHGCMBuffer));

        if (*pLengthRemaining >= cbToCopy)
        {
            pInfoBuffer += cbToCopy;
            *pLengthRemaining -= cbToCopy;
        }
        else
            break;

        if (RxContext->QueryDirectory.ReturnSingleEntry)
            break;

        /* More left? */
        if (cbHGCMBuffer <= 0)
            break;

        index++; /* File Index. */

        cFiles--;
    }

    if (pNextOffset)
        *pNextOffset = 0; /* Last pInfo->NextEntryOffset should be set to zero! */

end:
    if (pHGCMBuffer)
        vbsfFreeNonPagedMem(pHGCMBuffer);

    if (ParsedPath)
        vbsfFreeNonPagedMem(ParsedPath);

    Log(("NEMUSF: MrxQueryDirectory: Returned 0x%08X\n",
         Status));
    return Status;
}

NTSTATUS NemuMRxQueryVolumeInfo(IN OUT PRX_CONTEXT RxContext)
{
    NTSTATUS Status;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension = NemuMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_NEMU_FOBX pNemuFobx = NemuMRxGetFileObjectExtension(capFobx);

    FS_INFORMATION_CLASS FsInformationClass = RxContext->Info.FsInformationClass;
    PVOID pInfoBuffer = RxContext->Info.Buffer;
    ULONG cbInfoBuffer = RxContext->Info.LengthRemaining;
    ULONG cbToCopy = 0;
    ULONG cbString = 0;

    Log(("NEMUSF: MrxQueryVolumeInfo: pInfoBuffer = %p, cbInfoBuffer = %d\n",
         RxContext->Info.Buffer, RxContext->Info.LengthRemaining));
    Log(("NEMUSF: MrxQueryVolumeInfo: nemuFobx = %p, Handle = 0x%RX64\n",
         pNemuFobx, pNemuFobx? pNemuFobx->hFile: 0));

    Status = STATUS_INVALID_PARAMETER;

    switch (FsInformationClass)
    {
        case FileFsVolumeInformation:
        {
            PFILE_FS_VOLUME_INFORMATION pInfo = (PFILE_FS_VOLUME_INFORMATION)pInfoBuffer;

            PMRX_NET_ROOT pNetRoot = capFcb->pNetRoot;
            PMRX_SRV_CALL pSrvCall = pNetRoot->pSrvCall;

            PWCHAR pRootName;
            ULONG cbRootName;

            PSHFLVOLINFO pShflVolInfo;
            uint32_t cbHGCMBuffer;
            uint8_t *pHGCMBuffer;
            int nemuRC;

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsVolumeInformation\n"));

            if (!pNemuFobx)
            {
                Log(("NEMUSF: MrxQueryVolumeInfo: pNemuFobx is NULL!\n"));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            cbRootName = pNetRoot->pNetRootName->Length - pSrvCall->pSrvCallName->Length;
            cbRootName -= sizeof(WCHAR); /* Remove the leading backslash. */
            pRootName = pNetRoot->pNetRootName->Buffer + (pSrvCall->pSrvCallName->Length / sizeof(WCHAR));
            pRootName++; /* Remove the leading backslash. */

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsVolumeInformation: Root name = %.*ls, %d bytes\n",
                 cbRootName / sizeof(WCHAR), pRootName, cbRootName));

            cbToCopy = FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel);

            cbString  = NEMU_VOLNAME_PREFIX_SIZE;
            cbString += cbRootName;
            cbString += sizeof(WCHAR);

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsVolumeInformation: cbToCopy %d, cbString %d\n",
                 cbToCopy, cbString));

            if (cbInfoBuffer < cbToCopy)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlZeroMemory(pInfo, cbToCopy);

            /* Query serial number. */
            cbHGCMBuffer = sizeof(SHFLVOLINFO);
            pHGCMBuffer = (uint8_t *)vbsfAllocNonPagedMem(cbHGCMBuffer);
            if (!pHGCMBuffer)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            nemuRC = VbglR0SfFsInfo(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, pNemuFobx->hFile,
                                    SHFL_INFO_GET | SHFL_INFO_VOLUME, &cbHGCMBuffer, (PSHFLDIRINFO)pHGCMBuffer);

            if (nemuRC != VINF_SUCCESS)
            {
                Status = NemuErrorToNTStatus(nemuRC);
                vbsfFreeNonPagedMem(pHGCMBuffer);
                break;
            }

            pShflVolInfo = (PSHFLVOLINFO)pHGCMBuffer;
            pInfo->VolumeSerialNumber = pShflVolInfo->ulSerial;
            vbsfFreeNonPagedMem(pHGCMBuffer);

            pInfo->VolumeCreationTime.QuadPart = 0;
            pInfo->SupportsObjects = FALSE;

            if (cbInfoBuffer >= cbToCopy + cbString)
            {
                RtlCopyMemory(&pInfo->VolumeLabel[0],
                              NEMU_VOLNAME_PREFIX,
                              NEMU_VOLNAME_PREFIX_SIZE);
                RtlCopyMemory(&pInfo->VolumeLabel[NEMU_VOLNAME_PREFIX_SIZE / sizeof(WCHAR)],
                              pRootName,
                              cbRootName);
                pInfo->VolumeLabel[cbString / sizeof(WCHAR) -  1] = 0;
            }
            else
            {
                cbString = cbInfoBuffer - cbToCopy;

                RtlCopyMemory(&pInfo->VolumeLabel[0],
                              NEMU_VOLNAME_PREFIX,
                              RT_MIN(cbString, NEMU_VOLNAME_PREFIX_SIZE));
                if (cbString > NEMU_VOLNAME_PREFIX_SIZE)
                {
                    RtlCopyMemory(&pInfo->VolumeLabel[NEMU_VOLNAME_PREFIX_SIZE / sizeof(WCHAR)],
                                  pRootName,
                                  cbString - NEMU_VOLNAME_PREFIX_SIZE);
                }
            }

            pInfo->VolumeLabelLength = cbString;

            cbToCopy += cbString;

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsVolumeInformation: VolumeLabelLength %d\n",
                 pInfo->VolumeLabelLength));

            Status = STATUS_SUCCESS;
            break;
        }

        case FileFsLabelInformation:
        {
            PFILE_FS_LABEL_INFORMATION pInfo = (PFILE_FS_LABEL_INFORMATION)pInfoBuffer;

            PMRX_NET_ROOT pNetRoot = capFcb->pNetRoot;
            PMRX_SRV_CALL pSrvCall = pNetRoot->pSrvCall;

            PWCHAR pRootName;
            ULONG cbRootName;

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsLabelInformation\n"));

            cbRootName = pNetRoot->pNetRootName->Length - pSrvCall->pSrvCallName->Length;
            cbRootName -= sizeof(WCHAR); /* Remove the leading backslash. */
            pRootName = pNetRoot->pNetRootName->Buffer + (pSrvCall->pSrvCallName->Length / sizeof(WCHAR));
            pRootName++; /* Remove the leading backslash. */

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsLabelInformation: Root name = %.*ls, %d bytes\n",
                 cbRootName / sizeof(WCHAR), pRootName, cbRootName));

            cbToCopy = FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel);

            cbString  = NEMU_VOLNAME_PREFIX_SIZE;
            cbString += cbRootName;
            cbString += sizeof(WCHAR);

            if (cbInfoBuffer < cbToCopy)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlZeroMemory(pInfo, cbToCopy);

            if (cbInfoBuffer >= cbToCopy + cbString)
            {
                RtlCopyMemory(&pInfo->VolumeLabel[0],
                              NEMU_VOLNAME_PREFIX,
                              NEMU_VOLNAME_PREFIX_SIZE);
                RtlCopyMemory(&pInfo->VolumeLabel[NEMU_VOLNAME_PREFIX_SIZE / sizeof(WCHAR)],
                              pRootName,
                              cbRootName);
                pInfo->VolumeLabel[cbString / sizeof(WCHAR) -  1] = 0;
            }
            else
            {
                cbString = cbInfoBuffer - cbToCopy;

                RtlCopyMemory(&pInfo->VolumeLabel[0],
                              NEMU_VOLNAME_PREFIX,
                              RT_MIN(cbString, NEMU_VOLNAME_PREFIX_SIZE));
                if (cbString > NEMU_VOLNAME_PREFIX_SIZE)
                {
                    RtlCopyMemory(&pInfo->VolumeLabel[NEMU_VOLNAME_PREFIX_SIZE / sizeof(WCHAR)],
                                  pRootName,
                                  cbString - NEMU_VOLNAME_PREFIX_SIZE);
                }
            }

            pInfo->VolumeLabelLength = cbString;

            cbToCopy += cbString;

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsLabelInformation: VolumeLabelLength %d\n",
                 pInfo->VolumeLabelLength));

            Status = STATUS_SUCCESS;
            break;
        }

        case FileFsFullSizeInformation:
        case FileFsSizeInformation:
        {
            PFILE_FS_FULL_SIZE_INFORMATION pFullSizeInfo = (PFILE_FS_FULL_SIZE_INFORMATION)pInfoBuffer;
            PFILE_FS_SIZE_INFORMATION pSizeInfo = (PFILE_FS_SIZE_INFORMATION)pInfoBuffer;

            uint32_t cbHGCMBuffer;
            uint8_t *pHGCMBuffer;
            int nemuRC;
            PSHFLVOLINFO pShflVolInfo;

            LARGE_INTEGER TotalAllocationUnits;
            LARGE_INTEGER AvailableAllocationUnits;
            ULONG         SectorsPerAllocationUnit;
            ULONG         BytesPerSector;

            if (FsInformationClass == FileFsFullSizeInformation)
            {
                Log(("NEMUSF: MrxQueryVolumeInfo: FileFsFullSizeInformation\n"));
                cbToCopy = sizeof(FILE_FS_FULL_SIZE_INFORMATION);
            }
            else
            {
                Log(("NEMUSF: MrxQueryVolumeInfo: FileFsSizeInformation\n"));
                cbToCopy = sizeof(FILE_FS_SIZE_INFORMATION);
            }

            if (!pNemuFobx)
            {
                Log(("NEMUSF: MrxQueryVolumeInfo: pNemuFobx is NULL!\n"));
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (cbInfoBuffer < cbToCopy)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlZeroMemory(pInfoBuffer, cbToCopy);

            cbHGCMBuffer = sizeof(SHFLVOLINFO);
            pHGCMBuffer = (uint8_t *)vbsfAllocNonPagedMem(cbHGCMBuffer);
            if (!pHGCMBuffer)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            nemuRC = VbglR0SfFsInfo(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, pNemuFobx->hFile,
                                    SHFL_INFO_GET | SHFL_INFO_VOLUME, &cbHGCMBuffer, (PSHFLDIRINFO)pHGCMBuffer);

            if (nemuRC != VINF_SUCCESS)
            {
                Status = NemuErrorToNTStatus(nemuRC);
                vbsfFreeNonPagedMem(pHGCMBuffer);
                break;
            }

            pShflVolInfo = (PSHFLVOLINFO)pHGCMBuffer;

            TotalAllocationUnits.QuadPart     = pShflVolInfo->ullTotalAllocationBytes / pShflVolInfo->ulBytesPerAllocationUnit;
            AvailableAllocationUnits.QuadPart = pShflVolInfo->ullAvailableAllocationBytes / pShflVolInfo->ulBytesPerAllocationUnit;
            SectorsPerAllocationUnit          = pShflVolInfo->ulBytesPerAllocationUnit / pShflVolInfo->ulBytesPerSector;
            BytesPerSector                    = pShflVolInfo->ulBytesPerSector;

            Log(("NEMUSF: MrxQueryVolumeInfo: TotalAllocationUnits     0x%RX64\n", TotalAllocationUnits.QuadPart));
            Log(("NEMUSF: MrxQueryVolumeInfo: AvailableAllocationUnits 0x%RX64\n", AvailableAllocationUnits.QuadPart));
            Log(("NEMUSF: MrxQueryVolumeInfo: SectorsPerAllocationUnit 0x%X\n", SectorsPerAllocationUnit));
            Log(("NEMUSF: MrxQueryVolumeInfo: BytesPerSector           0x%X\n", BytesPerSector));

            if (FsInformationClass == FileFsFullSizeInformation)
            {
                pFullSizeInfo->TotalAllocationUnits           = TotalAllocationUnits;
                pFullSizeInfo->CallerAvailableAllocationUnits = AvailableAllocationUnits;
                pFullSizeInfo->ActualAvailableAllocationUnits = AvailableAllocationUnits;
                pFullSizeInfo->SectorsPerAllocationUnit       = SectorsPerAllocationUnit;
                pFullSizeInfo->BytesPerSector                 = BytesPerSector;
            }
            else
            {
                pSizeInfo->TotalAllocationUnits     = TotalAllocationUnits;
                pSizeInfo->AvailableAllocationUnits = AvailableAllocationUnits;
                pSizeInfo->SectorsPerAllocationUnit = SectorsPerAllocationUnit;
                pSizeInfo->BytesPerSector           = BytesPerSector;
            }

            vbsfFreeNonPagedMem(pHGCMBuffer);

            Status = STATUS_SUCCESS;
            break;
        }

        case FileFsDeviceInformation:
        {
            PFILE_FS_DEVICE_INFORMATION pInfo = (PFILE_FS_DEVICE_INFORMATION)pInfoBuffer;
            PMRX_NET_ROOT NetRoot = capFcb->pNetRoot;

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsDeviceInformation: Type = %d\n",
                 NetRoot->DeviceType));

            cbToCopy = sizeof(FILE_FS_DEVICE_INFORMATION);

            if (cbInfoBuffer < cbToCopy)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            pInfo->DeviceType = NetRoot->DeviceType;
            pInfo->Characteristics = FILE_REMOTE_DEVICE;

            Status = STATUS_SUCCESS;
            break;
        }

        case FileFsAttributeInformation:
        {
            PFILE_FS_ATTRIBUTE_INFORMATION pInfo = (PFILE_FS_ATTRIBUTE_INFORMATION)pInfoBuffer;

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsAttributeInformation\n"));

            cbToCopy = FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);

            cbString = sizeof(MRX_NEMU_FILESYS_NAME_U);

            if (cbInfoBuffer < cbToCopy)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            pInfo->FileSystemAttributes = 0; /** @todo set unicode, case sensitive etc? */
            pInfo->MaximumComponentNameLength = 255; /** @todo should query from the host */

            if (cbInfoBuffer >= cbToCopy + cbString)
            {
                RtlCopyMemory(pInfo->FileSystemName,
                              MRX_NEMU_FILESYS_NAME_U,
                              sizeof(MRX_NEMU_FILESYS_NAME_U));
            }
            else
            {
                cbString = cbInfoBuffer - cbToCopy;

                RtlCopyMemory(pInfo->FileSystemName,
                              MRX_NEMU_FILESYS_NAME_U,
                              RT_MIN(cbString, sizeof(MRX_NEMU_FILESYS_NAME_U)));
            }

            pInfo->FileSystemNameLength = cbString;

            cbToCopy += cbString;

            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsAttributeInformation: FileSystemNameLength %d\n",
                 pInfo->FileSystemNameLength));

            Status = STATUS_SUCCESS;
            break;
        }

        case FileFsControlInformation:
            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsControlInformation: not supported\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;

        case FileFsObjectIdInformation:
            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsObjectIdInformation: not supported\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;

        case FileFsMaximumInformation:
            Log(("NEMUSF: MrxQueryVolumeInfo: FileFsMaximumInformation: not supported\n"));
            Status = STATUS_INVALID_PARAMETER;
            break;

        default:
            Log(("NEMUSF: MrxQueryVolumeInfo: Not supported FsInformationClass %d!\n",
                 FsInformationClass));
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    if (Status == STATUS_SUCCESS)
        RxContext->Info.LengthRemaining = cbInfoBuffer - cbToCopy;
    else if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        Log(("NEMUSF: MrxQueryVolumeInfo: Insufficient buffer size %d, required %d\n",
             cbInfoBuffer, cbToCopy));
        RxContext->InformationToReturn = cbToCopy;
    }

    Log(("NEMUSF: MrxQueryVolumeInfo: cbToCopy = %d, LengthRemaining = %d, Status = 0x%08X\n",
         cbToCopy, RxContext->Info.LengthRemaining, Status));

    return Status;
}

NTSTATUS NemuMRxQueryFileInfo(IN PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension = NemuMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_NEMU_FOBX pNemuFobx = NemuMRxGetFileObjectExtension(capFobx);

    PUNICODE_STRING FileName = GET_ALREADY_PREFIXED_NAME_FROM_CONTEXT(RxContext);
    FILE_INFORMATION_CLASS FunctionalityRequested = RxContext->Info.FileInformationClass;
    PCHAR pInfoBuffer = (PCHAR)RxContext->Info.Buffer;
    uint32_t cbInfoBuffer = RxContext->Info.Length;
    ULONG *pLengthRemaining = (PULONG) & RxContext->Info.LengthRemaining;

    int nemuRC = 0;

    ULONG cbToCopy = 0;
    uint8_t *pHGCMBuffer = 0;
    uint32_t cbHGCMBuffer;
    PSHFLFSOBJINFO pFileEntry = NULL;

    if (!pLengthRemaining)
    {
        Log(("NEMUSF: MrxQueryFileInfo: length pointer is NULL!\n"));
        return STATUS_INVALID_PARAMETER;
    }

    Log(("NEMUSF: MrxQueryFileInfo: InfoBuffer = %p, Size = %d bytes, LenRemain = %d bytes\n",
         pInfoBuffer, cbInfoBuffer, *pLengthRemaining));

    if (!pNemuFobx)
    {
        Log(("NEMUSF: MrxQueryFileInfo: pNemuFobx is NULL!\n"));
        return STATUS_INVALID_PARAMETER;
    }

    if (!pInfoBuffer)
    {
        Log(("NEMUSF: MrxQueryFileInfo: pInfoBuffer is NULL!\n"));
        return STATUS_INVALID_PARAMETER;
    }

    if (pNemuFobx->FileStandardInfo.Directory == TRUE)
    {
        Log(("NEMUSF: MrxQueryFileInfo: Directory -> Copy info retrieved during the create call\n"));
        Status = STATUS_SUCCESS;

        switch (FunctionalityRequested)
        {
            case FileBasicInformation:
            {
                PFILE_BASIC_INFORMATION pInfo = (PFILE_BASIC_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileBasicInformation\n"));

                cbToCopy = sizeof(FILE_BASIC_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    *pInfo = pNemuFobx->FileBasicInfo;
                    Log(("NEMUSF: MrxQueryFileInfo: FileBasicInformation: File attributes: 0x%x\n",
                         pInfo->FileAttributes));
                }
                else
                {
                    Status = STATUS_BUFFER_TOO_SMALL;
                }
                break;
            }

            case FileStandardInformation:
            {
                PFILE_STANDARD_INFORMATION pInfo = (PFILE_STANDARD_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileStandardInformation\n"));

                cbToCopy = sizeof(FILE_STANDARD_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                    *pInfo = pNemuFobx->FileStandardInfo;
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileNamesInformation:
            {
                PFILE_NAMES_INFORMATION pInfo = (PFILE_NAMES_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileNamesInformation\n"));

                cbToCopy = sizeof(FILE_NAMES_INFORMATION);
                /* And size in bytes of the WCHAR name. */
                cbToCopy += FileName->Length;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->FileNameLength = FileName->Length;

                    RtlCopyMemory(pInfo->FileName, FileName->Buffer, FileName->Length);
                    pInfo->FileName[FileName->Length] = 0; /* FILE_NAMES_INFORMATION had space for the nul. */
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileInternalInformation:
            {
                PFILE_INTERNAL_INFORMATION pInfo = (PFILE_INTERNAL_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileInternalInformation\n"));

                cbToCopy = sizeof(FILE_INTERNAL_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    /* A 8-byte file reference number for the file. */
                    pInfo->IndexNumber.QuadPart = (ULONG_PTR)capFcb;
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            case FileEaInformation:
            {
                PFILE_EA_INFORMATION pInfo = (PFILE_EA_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileEaInformation\n"));

                cbToCopy = sizeof(FILE_EA_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    pInfo->EaSize = 0;
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileNetworkOpenInformation:
            {
                PFILE_NETWORK_OPEN_INFORMATION pInfo = (PFILE_NETWORK_OPEN_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileNetworkOpenInformation\n"));

                cbToCopy = sizeof(FILE_NETWORK_OPEN_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    pInfo->CreationTime   = pNemuFobx->FileBasicInfo.CreationTime;
                    pInfo->LastAccessTime = pNemuFobx->FileBasicInfo.LastAccessTime;
                    pInfo->LastWriteTime  = pNemuFobx->FileBasicInfo.LastWriteTime;
                    pInfo->ChangeTime     = pNemuFobx->FileBasicInfo.ChangeTime;
                    pInfo->AllocationSize.QuadPart = 0;
                    pInfo->EndOfFile.QuadPart      = 0;
                    pInfo->FileAttributes = pNemuFobx->FileBasicInfo.FileAttributes;
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileStreamInformation:
                Log(("NEMUSF: MrxQueryFileInfo: FileStreamInformation: not supported\n"));
                Status = STATUS_INVALID_PARAMETER;
                goto end;

            default:
                Log(("NEMUSF: MrxQueryFileInfo: Not supported FunctionalityRequested %d!\n",
                     FunctionalityRequested));
                Status = STATUS_INVALID_PARAMETER;
                goto end;
        }
    }
    else /* Entry is a file. */
    {
        cbHGCMBuffer = RT_MAX(cbInfoBuffer, PAGE_SIZE);
        pHGCMBuffer = (uint8_t *)vbsfAllocNonPagedMem(cbHGCMBuffer);

        if (!pHGCMBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        Assert(pNemuFobx && pNetRootExtension && pDeviceExtension);
        nemuRC = VbglR0SfFsInfo(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, pNemuFobx->hFile,
                                SHFL_INFO_GET | SHFL_INFO_FILE, &cbHGCMBuffer, (PSHFLDIRINFO)pHGCMBuffer);

        if (nemuRC != VINF_SUCCESS)
        {
            Status = NemuErrorToNTStatus(nemuRC);
            goto end;
        }

        pFileEntry = (PSHFLFSOBJINFO)pHGCMBuffer;
        Status = STATUS_SUCCESS;

        switch (FunctionalityRequested)
        {
            case FileBasicInformation:
            {
                PFILE_BASIC_INFORMATION pInfo = (PFILE_BASIC_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileBasicInformation\n"));

                cbToCopy = sizeof(FILE_BASIC_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pFileEntry->BirthTime); /* Ridiculous name. */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pFileEntry->AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pFileEntry->ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pFileEntry->ChangeTime);
                    pInfo->FileAttributes          = NemuToNTFileAttributes(pFileEntry->Attr.fMode);

                    Log(("NEMUSF: MrxQueryFileInfo: FileBasicInformation: File attributes = 0x%x\n",
                         pInfo->FileAttributes));
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileStandardInformation:
            {
                PFILE_STANDARD_INFORMATION pInfo = (PFILE_STANDARD_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileStandardInformation\n"));

                cbToCopy = sizeof(FILE_STANDARD_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    pInfo->AllocationSize.QuadPart = pFileEntry->cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pFileEntry->cbObject;
                    pInfo->NumberOfLinks           = 1; /* @todo 0? */
                    pInfo->DeletePending           = FALSE;

                    if (pFileEntry->Attr.fMode & RTFS_DOS_DIRECTORY)
                        pInfo->Directory = TRUE;
                    else
                        pInfo->Directory = FALSE;
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileNamesInformation:
            {
                PFILE_NAMES_INFORMATION pInfo = (PFILE_NAMES_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileNamesInformation\n"));

                cbToCopy = sizeof(FILE_NAMES_INFORMATION);
                /* And size in bytes of the WCHAR name. */
                cbToCopy += FileName->Length;

                if (*pLengthRemaining >= cbToCopy)
                {
                    RtlZeroMemory(pInfo, cbToCopy);

                    pInfo->FileNameLength = FileName->Length;

                    RtlCopyMemory(pInfo->FileName, FileName->Buffer, FileName->Length);
                    pInfo->FileName[FileName->Length] = 0; /* FILE_NAMES_INFORMATION had space for the nul. */
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileInternalInformation:
            {
                PFILE_INTERNAL_INFORMATION pInfo = (PFILE_INTERNAL_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileInternalInformation\n"));

                cbToCopy = sizeof(FILE_INTERNAL_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    /* A 8-byte file reference number for the file. */
                    pInfo->IndexNumber.QuadPart = (ULONG_PTR)capFcb;
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileEaInformation:
            {
                PFILE_EA_INFORMATION pInfo = (PFILE_EA_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileEaInformation\n"));

                cbToCopy = sizeof(FILE_EA_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                    pInfo->EaSize = 0;
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileAttributeTagInformation:
            {
                PFILE_ATTRIBUTE_TAG_INFORMATION pInfo = (PFILE_ATTRIBUTE_TAG_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileAttributeTagInformation\n"));

                cbToCopy = sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    pInfo->FileAttributes = NemuToNTFileAttributes(pFileEntry->Attr.fMode);
                    pInfo->ReparseTag = 0;
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileEndOfFileInformation:
            {
                PFILE_END_OF_FILE_INFORMATION pInfo = (PFILE_END_OF_FILE_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileEndOfFileInformation\n"));

                cbToCopy = sizeof(FILE_END_OF_FILE_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                    pInfo->EndOfFile.QuadPart = pFileEntry->cbObject;
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileAllocationInformation:
            {
                PFILE_ALLOCATION_INFORMATION pInfo = (PFILE_ALLOCATION_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileAllocationInformation\n"));

                cbToCopy = sizeof(FILE_ALLOCATION_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                    pInfo->AllocationSize.QuadPart = pFileEntry->cbAllocated;
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileNetworkOpenInformation:
            {
                PFILE_NETWORK_OPEN_INFORMATION pInfo = (PFILE_NETWORK_OPEN_INFORMATION)pInfoBuffer;
                Log(("NEMUSF: MrxQueryFileInfo: FileNetworkOpenInformation\n"));

                cbToCopy = sizeof(FILE_NETWORK_OPEN_INFORMATION);

                if (*pLengthRemaining >= cbToCopy)
                {
                    pInfo->CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pFileEntry->BirthTime); /* Ridiculous name. */
                    pInfo->LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pFileEntry->AccessTime);
                    pInfo->LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pFileEntry->ModificationTime);
                    pInfo->ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pFileEntry->ChangeTime);
                    pInfo->AllocationSize.QuadPart = pFileEntry->cbAllocated;
                    pInfo->EndOfFile.QuadPart      = pFileEntry->cbObject;
                    pInfo->FileAttributes          = NemuToNTFileAttributes(pFileEntry->Attr.fMode);
                }
                else
                    Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            case FileStreamInformation:
                Log(("NEMUSF: MrxQueryFileInfo: FileStreamInformation: not supported\n"));
                Status = STATUS_INVALID_PARAMETER;
                goto end;

            default:
                Log(("NEMUSF: MrxQueryFileInfo: Not supported FunctionalityRequested %d!\n",
                     FunctionalityRequested));
                Status = STATUS_INVALID_PARAMETER;
                goto end;
        }
    }

    if (Status == STATUS_SUCCESS)
    {
        if (*pLengthRemaining < cbToCopy)
        {
            /* This situation must be already taken into account by the above code. */
            AssertMsgFailed(("NEMUSF: MrxQueryFileInfo: Length remaining is below 0! (%d - %d)!\n",
                             *pLengthRemaining, cbToCopy));
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            pInfoBuffer += cbToCopy;
            *pLengthRemaining -= cbToCopy;
        }
    }

end:
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        Log(("NEMUSF: MrxQueryFileInfo: Buffer too small %d, required %d!\n",
             *pLengthRemaining, cbToCopy));
        RxContext->InformationToReturn = cbToCopy;
    }

    if (pHGCMBuffer)
        vbsfFreeNonPagedMem(pHGCMBuffer);

    if (Status == STATUS_SUCCESS)
    {
        Log(("NEMUSF: MrxQueryFileInfo: Remaining length = %d\n",
             *pLengthRemaining));
    }

    Log(("NEMUSF: MrxQueryFileInfo: Returned 0x%08X\n", Status));
    return Status;
}

NTSTATUS NemuMRxSetFileInfo(IN PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_NEMU_DEVICE_EXTENSION pDeviceExtension = NemuMRxGetDeviceExtension(RxContext);
    PMRX_NEMU_NETROOT_EXTENSION pNetRootExtension = NemuMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_NEMU_FOBX pNemuFobx = NemuMRxGetFileObjectExtension(capFobx);

    FILE_INFORMATION_CLASS FunctionalityRequested = RxContext->Info.FileInformationClass;
    PVOID pInfoBuffer = (PVOID)RxContext->Info.Buffer;

    int nemuRC;

    uint8_t *pHGCMBuffer = NULL;
    uint32_t cbBuffer = 0;

    Log(("NEMUSF: MrxSetFileInfo: pInfoBuffer %p\n",
         pInfoBuffer));

    switch (FunctionalityRequested)
    {
        case FileBasicInformation:
        {
            PFILE_BASIC_INFORMATION pInfo = (PFILE_BASIC_INFORMATION)pInfoBuffer;
            PSHFLFSOBJINFO pSHFLFileInfo;

            Log(("NEMUSF: MRxSetFileInfo: FileBasicInformation: CreationTime   %RX64\n", pInfo->CreationTime.QuadPart));
            Log(("NEMUSF: MRxSetFileInfo: FileBasicInformation: LastAccessTime %RX64\n", pInfo->LastAccessTime.QuadPart));
            Log(("NEMUSF: MRxSetFileInfo: FileBasicInformation: LastWriteTime  %RX64\n", pInfo->LastWriteTime.QuadPart));
            Log(("NEMUSF: MRxSetFileInfo: FileBasicInformation: ChangeTime     %RX64\n", pInfo->ChangeTime.QuadPart));
            Log(("NEMUSF: MRxSetFileInfo: FileBasicInformation: FileAttributes %RX32\n", pInfo->FileAttributes));

            /* When setting file attributes, a value of -1 indicates to the server that it MUST NOT change this attribute
             * for all subsequent operations on the same file handle.
             */
            if (pInfo->CreationTime.QuadPart == -1)
            {
                pNemuFobx->fKeepCreationTime = TRUE;
                pNemuFobx->SetFileInfoOnCloseFlags |= NEMU_FOBX_F_INFO_CREATION_TIME;
            }
            if (pInfo->LastAccessTime.QuadPart == -1)
            {
                pNemuFobx->fKeepLastAccessTime = TRUE;
                pNemuFobx->SetFileInfoOnCloseFlags |= NEMU_FOBX_F_INFO_LASTACCESS_TIME;
            }
            if (pInfo->LastWriteTime.QuadPart == -1)
            {
                pNemuFobx->fKeepLastWriteTime = TRUE;
                pNemuFobx->SetFileInfoOnCloseFlags |= NEMU_FOBX_F_INFO_LASTWRITE_TIME;
            }
            if (pInfo->ChangeTime.QuadPart == -1)
            {
                pNemuFobx->fKeepChangeTime = TRUE;
                pNemuFobx->SetFileInfoOnCloseFlags |= NEMU_FOBX_F_INFO_CHANGE_TIME;
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

            Log(("NEMUSF: MrxSetFileInfo: FileBasicInformation: keeps %d %d %d %d\n",
                 pNemuFobx->fKeepCreationTime, pNemuFobx->fKeepLastAccessTime, pNemuFobx->fKeepLastWriteTime, pNemuFobx->fKeepChangeTime));

            /* The properties, that need to be changed, are set to something other than zero */
            if (pInfo->CreationTime.QuadPart && !pNemuFobx->fKeepCreationTime)
            {
                RTTimeSpecSetNtTime(&pSHFLFileInfo->BirthTime, pInfo->CreationTime.QuadPart);
                pNemuFobx->SetFileInfoOnCloseFlags |= NEMU_FOBX_F_INFO_CREATION_TIME;
            }
            if (pInfo->LastAccessTime.QuadPart && !pNemuFobx->fKeepLastAccessTime)
            {
                RTTimeSpecSetNtTime(&pSHFLFileInfo->AccessTime, pInfo->LastAccessTime.QuadPart);
                pNemuFobx->SetFileInfoOnCloseFlags |= NEMU_FOBX_F_INFO_LASTACCESS_TIME;
            }
            if (pInfo->LastWriteTime.QuadPart && !pNemuFobx->fKeepLastWriteTime)
            {
                RTTimeSpecSetNtTime(&pSHFLFileInfo->ModificationTime, pInfo->LastWriteTime.QuadPart);
                pNemuFobx->SetFileInfoOnCloseFlags |= NEMU_FOBX_F_INFO_LASTWRITE_TIME;
            }
            if (pInfo->ChangeTime.QuadPart && !pNemuFobx->fKeepChangeTime)
            {
                RTTimeSpecSetNtTime(&pSHFLFileInfo->ChangeTime, pInfo->ChangeTime.QuadPart);
                pNemuFobx->SetFileInfoOnCloseFlags |= NEMU_FOBX_F_INFO_CHANGE_TIME;
            }
            if (pInfo->FileAttributes)
            {
                pSHFLFileInfo->Attr.fMode = NTToNemuFileAttributes(pInfo->FileAttributes);
            }

            Assert(pNemuFobx && pNetRootExtension && pDeviceExtension);
            nemuRC = VbglR0SfFsInfo(&pDeviceExtension->hgcmClient, &pNetRootExtension->map, pNemuFobx->hFile,
                                    SHFL_INFO_SET | SHFL_INFO_FILE, &cbBuffer, (PSHFLDIRINFO)pSHFLFileInfo);

            if (nemuRC != VINF_SUCCESS)
            {
                Status = NemuErrorToNTStatus(nemuRC);
                goto end;
            }
            else
            {
                /* Update our internal copy. Ignore zero fields! */
                if (pInfo->CreationTime.QuadPart && !pNemuFobx->fKeepCreationTime)
                    pNemuFobx->FileBasicInfo.CreationTime = pInfo->CreationTime;
                if (pInfo->LastAccessTime.QuadPart && !pNemuFobx->fKeepLastAccessTime)
                    pNemuFobx->FileBasicInfo.LastAccessTime = pInfo->LastAccessTime;
                if (pInfo->LastWriteTime.QuadPart && !pNemuFobx->fKeepLastWriteTime)
                    pNemuFobx->FileBasicInfo.LastWriteTime = pInfo->LastWriteTime;
                if (pInfo->ChangeTime.QuadPart && !pNemuFobx->fKeepChangeTime)
                    pNemuFobx->FileBasicInfo.ChangeTime = pInfo->ChangeTime;
                if (pInfo->FileAttributes)
                    pNemuFobx->FileBasicInfo.FileAttributes = pInfo->FileAttributes;
            }

            break;
        }

        case FileDispositionInformation:
        {
            PFILE_DISPOSITION_INFORMATION pInfo = (PFILE_DISPOSITION_INFORMATION)pInfoBuffer;

            Log(("NEMUSF: MrxSetFileInfo: FileDispositionInformation: Delete = %d\n",
                 pInfo->DeleteFile));

            if (pInfo->DeleteFile && capFcb->OpenCount == 1)
                Status = vbsfRemove(RxContext);
            else
                Status = STATUS_SUCCESS;
            break;
        }

        case FilePositionInformation:
        {
            PFILE_POSITION_INFORMATION pInfo = (PFILE_POSITION_INFORMATION)pInfoBuffer;

            Log(("NEMUSF: MrxSetFileInfo: FilePositionInformation: CurrentByteOffset = 0x%RX64. Unsupported!\n",
                 pInfo->CurrentByteOffset.QuadPart));

            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        case FileAllocationInformation:
        {
            PFILE_ALLOCATION_INFORMATION pInfo = (PFILE_ALLOCATION_INFORMATION)pInfoBuffer;

            Log(("NEMUSF: MrxSetFileInfo: FileAllocationInformation: new AllocSize = 0x%RX64, FileSize = 0x%RX64\n",
                 pInfo->AllocationSize.QuadPart, capFcb->Header.FileSize.QuadPart));

            /* Check if the new allocation size changes the file size. */
            if (pInfo->AllocationSize.QuadPart > capFcb->Header.FileSize.QuadPart)
            {
                /* Ignore this request and return success. Shared folders do not distinguish between
                 * AllocationSize and FileSize.
                 */
                Status = STATUS_SUCCESS;
            }
            else
            {
                /* Treat the request as a EndOfFile update. */
                LARGE_INTEGER NewAllocationSize;
                Status = vbsfSetEndOfFile(RxContext, &pInfo->AllocationSize, &NewAllocationSize);
            }

            break;
        }

        case FileEndOfFileInformation:
        {
            PFILE_END_OF_FILE_INFORMATION pInfo = (PFILE_END_OF_FILE_INFORMATION)pInfoBuffer;
            LARGE_INTEGER NewAllocationSize;

            Log(("NEMUSF: MrxSetFileInfo: FileEndOfFileInformation: new EndOfFile 0x%RX64, FileSize = 0x%RX64\n",
                 pInfo->EndOfFile.QuadPart, capFcb->Header.FileSize.QuadPart));

            Status = vbsfSetEndOfFile(RxContext, &pInfo->EndOfFile, &NewAllocationSize);

            Log(("NEMUSF: MrxSetFileInfo: FileEndOfFileInformation: AllocSize = 0x%RX64, Status 0x%08X\n",
                 NewAllocationSize.QuadPart, Status));

            break;
        }

        case FileLinkInformation:
        {
            PFILE_LINK_INFORMATION pInfo = (PFILE_LINK_INFORMATION )pInfoBuffer;

            Log(("NEMUSF: MrxSetFileInfo: FileLinkInformation: ReplaceIfExists = %d, RootDirectory = 0x%x = [%.*ls]. Not implemented!\n",
                 pInfo->ReplaceIfExists, pInfo->RootDirectory, pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));

            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        case FileRenameInformation:
        {
            PFILE_RENAME_INFORMATION pInfo = (PFILE_RENAME_INFORMATION)pInfoBuffer;

            Log(("NEMUSF: MrxSetFileInfo: FileRenameInformation: ReplaceIfExists = %d, RootDirectory = 0x%x = [%.*ls]\n",
                 pInfo->ReplaceIfExists, pInfo->RootDirectory, pInfo->FileNameLength / sizeof(WCHAR), pInfo->FileName));

            Status = vbsfRename(RxContext, FileRenameInformation, pInfoBuffer, RxContext->Info.Length);
            break;
        }

        default:
            Log(("NEMUSF: MrxSetFileInfo: Not supported FunctionalityRequested %d!\n",
                 FunctionalityRequested));
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

end:
    if (pHGCMBuffer)
        vbsfFreeNonPagedMem(pHGCMBuffer);

    Log(("NEMUSF: MrxSetFileInfo: Returned 0x%08X\n", Status));
    return Status;
}

NTSTATUS NemuMRxSetFileInfoAtCleanup(IN PRX_CONTEXT RxContext)
{
    Log(("NEMUSF: MRxSetFileInfoAtCleanup\n"));
    return STATUS_SUCCESS;
}
