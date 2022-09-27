/* $Id: NemuFUSE.cpp $ */
/** @file
 * NemuFUSE - Disk Image Flattening FUSE Program.
 */

/*
 * Copyright (C) 2009-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEFAULT /** @todo log group */
#include <iprt/types.h>

#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD) || defined(RT_OS_LINUX)
# include <sys/param.h>
# undef PVM     /* Blasted old BSD mess still hanging around darwin. */
#endif
#define FUSE_USE_VERSION 27
#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#ifndef EDOOFUS
# ifdef EBADMACHO
#  define EDOOFUS EBADMACHO
# elif defined(EPROTO)
#  define EDOOFUS EPROTO                /* What a boring lot. */
//# elif defined(EXYZ)
//#  define EDOOFUS EXYZ
# else
#  error "Choose an unlikely and (if possible) fun error number for EDOOFUS."
# endif
#endif

#include <Nemu/vd.h>
#include <Nemu/log.h>
#include <Nemu/err.h>
#include <iprt/critsect.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Node type.
 */
typedef enum NEMUFUSETYPE
{
    NEMUFUSETYPE_INVALID = 0,
    NEMUFUSETYPE_DIRECTORY,
    NEMUFUSETYPE_FLAT_IMAGE,
    NEMUFUSETYPE_CONTROL_PIPE
} NEMUFUSETYPE;

/**
 * Stuff common to both directories and files.
 */
typedef struct NEMUFUSENODE
{
    /** The directory name. */
    const char             *pszName;
    /** The name length. */
    size_t                  cchName;
    /** The node type. */
    NEMUFUSETYPE            enmType;
    /** The number of references.
     * The directory linking this node will always retain one. */
    int32_t volatile        cRefs;
    /** Critical section serializing access to the node data. */
    RTCRITSECT              CritSect;
    /** Pointer to the directory (parent). */
    struct NEMUFUSEDIR     *pDir;
    /** The mode mask. */
    RTFMODE                 fMode;
    /** The User ID of the directory owner. */
    RTUID                   Uid;
    /** The Group ID of the directory. */
    RTUID                   Gid;
    /** The link count. */
    uint32_t                cLinks;
    /** The inode number. */
    RTINODE                 Ino;
    /** The size of the primary stream. */
    RTFOFF                  cbPrimary;
} NEMUFUSENODE;
typedef NEMUFUSENODE *PNEMUFUSENODE;

/**
 * A flat image file.
 */
typedef struct NEMUFUSEFLATIMAGE
{
    /** The standard bits. */
    NEMUFUSENODE            Node;
    /** The virtual disk container. */
    PNEMUHDD                pDisk;
    /** The format name. */
    char                   *pszFormat;
    /** The number of readers.
     * Read only images will have this set to INT32_MAX/2 on creation. */
    int32_t                 cReaders;
    /** The number of writers. (Just 1 or 0 really.) */
    int32_t                 cWriters;
} NEMUFUSEFLATIMAGE;
typedef NEMUFUSEFLATIMAGE *PNEMUFUSEFLATIMAGE;

/**
 * A control pipe (file).
 */
typedef struct NEMUFUSECTRLPIPE
{
    /** The standard bits. */
    NEMUFUSENODE            Node;
} NEMUFUSECTRLPIPE;
typedef NEMUFUSECTRLPIPE *PNEMUFUSECTRLPIPE;


/**
 * A Directory.
 *
 * This is just a container of files and subdirectories, nothing special.
 */
typedef struct NEMUFUSEDIR
{
    /** The standard bits. */
    NEMUFUSENODE            Node;
    /** The number of directory entries. */
    uint32_t                cEntries;
    /** Array of pointers to directory entries.
     * Whether what's being pointed to is a file, directory or something else can be
     * determined by the enmType field. */
    PNEMUFUSENODE          *paEntries;
} NEMUFUSEDIR;
typedef NEMUFUSEDIR *PNEMUFUSEDIR;

/** The number of elements to grow NEMUFUSEDIR::paEntries by. */
#define NEMUFUSE_DIR_GROW_BY    2 /* 32 */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The root of the file hierarchy. */
static NEMUFUSEDIR     *g_pTreeRoot;
/** The next inode number. */
static RTINODE volatile g_NextIno = 1;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int nemufuseTreeLookupParent(const char *pszPath, const char **ppszName, PNEMUFUSEDIR *ppDir);
static int nemufuseTreeLookupParentForInsert(const char *pszPath, const char **ppszName, PNEMUFUSEDIR *ppDir);


/**
 * Node destructor.
 *
 * @returns true.
 * @param   pNode           The node.
 * @param   fLocked         Whether it's locked.
 */
static bool nemufuseNodeDestroy(PNEMUFUSENODE pNode, bool fLocked)
{
    Assert(pNode->cRefs == 0);

    /*
     * Type specific cleanups.
     */
    switch (pNode->enmType)
    {
        case NEMUFUSETYPE_DIRECTORY:
        {
            PNEMUFUSEDIR pDir = (PNEMUFUSEDIR)pNode;
            RTMemFree(pDir->paEntries);
            pDir->paEntries = NULL;
            pDir->cEntries = 0;
            break;
        }

        case NEMUFUSETYPE_FLAT_IMAGE:
        {
            PNEMUFUSEFLATIMAGE pFlatImage = (PNEMUFUSEFLATIMAGE)pNode;
            if (pFlatImage->pDisk)
            {
                int rc2 = VDClose(pFlatImage->pDisk, false /* fDelete */); AssertRC(rc2);
                pFlatImage->pDisk = NULL;
            }
            RTStrFree(pFlatImage->pszFormat);
            pFlatImage->pszFormat = NULL;
            break;
        }

        case NEMUFUSETYPE_CONTROL_PIPE:
            break;

        default:
            AssertMsgFailed(("%d\n", pNode->enmType));
            break;
    }

    /*
     * Generic cleanup.
     */
    pNode->enmType = NEMUFUSETYPE_INVALID;
    pNode->pszName = NULL;

    /*
     * Unlock and destroy the lock, before we finally frees the node.
     */
    if (fLocked)
        RTCritSectLeave(&pNode->CritSect);
    RTCritSectDelete(&pNode->CritSect);

    RTMemFree(pNode);

    return true;
}


/**
 * Locks a FUSE node.
 *
 * @param   pNode   The node.
 */
static void nemufuseNodeLock(PNEMUFUSENODE pNode)
{
    int rc = RTCritSectEnter(&pNode->CritSect);
    AssertRC(rc);
}


/**
 * Unlocks a FUSE node.
 *
 * @param   pNode   The node.
 */
static void nemufuseNodeUnlock(PNEMUFUSENODE pNode)
{
    int rc = RTCritSectLeave(&pNode->CritSect);
    AssertRC(rc);
}


/**
 * Retain a NemuFUSE node.
 *
 * @param   pNode   The node.
 */
static void nemufuseNodeRetain(PNEMUFUSENODE pNode)
{
    int32_t cNewRefs = ASMAtomicIncS32(&pNode->cRefs);
    Assert(cNewRefs != 1);
}


/**
 * Releases a NemuFUSE node reference.
 *
 * @returns true if deleted, false if not.
 * @param   pNode   The node.
 */
static bool nemufuseNodeRelease(PNEMUFUSENODE pNode)
{
    if (ASMAtomicDecS32(&pNode->cRefs) == 0)
        return nemufuseNodeDestroy(pNode, false /* fLocked */);
    return false;
}


/**
 * Locks and retains a NemuFUSE node.
 *
 * @param   pNode   The node.
 */
static void nemufuseNodeLockAndRetain(PNEMUFUSENODE pNode)
{
    nemufuseNodeLock(pNode);
    nemufuseNodeRetain(pNode);
}


/**
 * Releases a NemuFUSE node reference and unlocks it.
 *
 * @returns true if deleted, false if not.
 * @param   pNode   The node.
 */
static bool nemufuseNodeReleaseAndUnlock(PNEMUFUSENODE pNode)
{
    if (ASMAtomicDecS32(&pNode->cRefs) == 0)
        return nemufuseNodeDestroy(pNode, true /* fLocked */);
    nemufuseNodeUnlock(pNode);
    return false;
}


/**
 * Creates stat info for a locked node.
 *
 * @param   pNode   The node (locked).
 */
static void nemufuseNodeFillStat(PNEMUFUSENODE pNode, struct stat *pStat)
{
    pStat->st_dev       = 0;                /* ignored */
    pStat->st_ino       = pNode->Ino;       /* maybe ignored */
    pStat->st_mode      = pNode->fMode;
    pStat->st_nlink     = pNode->cLinks;
    pStat->st_uid       = pNode->Uid;
    pStat->st_gid       = pNode->Gid;
    pStat->st_rdev      = 0;
    /** @todo file times  */
    pStat->st_atime     = 0;
//    pStat->st_atimensec = 0;
    pStat->st_mtime     = 0;
//    pStat->st_mtimensec = 0;
    pStat->st_ctime     = 0;
//    pStat->st_ctimensec = 0;
    pStat->st_size      = pNode->cbPrimary;
    pStat->st_blocks    = (pNode->cbPrimary + DEV_BSIZE - 1) / DEV_BSIZE;
    pStat->st_blksize   = 0x1000;           /* ignored */
#ifndef RT_OS_LINUX
    pStat->st_flags     = 0;
    pStat->st_gen       = 0;
#endif
}


/**
 * Allocates a new node and initialize the node part of it.
 *
 * The returned node has one reference.
 *
 * @returns Nemu status code.
 *
 * @param   cbNode      The size of the node.
 * @param   pszName     The name of the node.
 * @param   enmType     The node type.
 * @param   pDir        The directory (parent).
 * @param   ppNode      Where to return the pointer to the node.
 */
static int nemufuseNodeAlloc(size_t cbNode, const char *pszName, NEMUFUSETYPE enmType, PNEMUFUSEDIR pDir,
                             PNEMUFUSENODE *ppNode)
{
    Assert(cbNode >= sizeof(NEMUFUSENODE));

    /*
     * Allocate the memory for it and init the critical section.
     */
    size_t cchName = strlen(pszName);
    PNEMUFUSENODE pNode = (PNEMUFUSENODE)RTMemAlloc(cchName + 1 + RT_ALIGN_Z(cbNode, 8));
    if (!pNode)
        return VERR_NO_MEMORY;

    int rc = RTCritSectInit(&pNode->CritSect);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pNode);
        return rc;
    }

    /*
     * Initialize the members.
     */
    pNode->pszName = (char *)memcpy((uint8_t *)pNode + RT_ALIGN_Z(cbNode, 8), pszName, cchName + 1);
    pNode->cchName = cchName;
    pNode->enmType = enmType;
    pNode->cRefs   = 1;
    pNode->pDir    = pDir;
#if 0
    pNode->fMode   = enmType == NEMUFUSETYPE_DIRECTORY ? S_IFDIR | 0755 : S_IFREG | 0644;
#else
    pNode->fMode   = enmType == NEMUFUSETYPE_DIRECTORY ? S_IFDIR | 0777 : S_IFREG | 0666;
#endif
    pNode->Uid     = 0;
    pNode->Gid     = 0;
    pNode->cLinks  = 0;
    pNode->Ino     = g_NextIno++; /** @todo make this safe! */
    pNode->cbPrimary = 0;

    *ppNode = pNode;
    return VINF_SUCCESS;
}


/**
 * Inserts a node into a directory
 *
 * The caller has locked and referenced the directory as well as checked that
 * the name doesn't already exist within it. On success both the reference and
 * and link counters will be incremented.
 *
 * @returns Nemu status code.
 *
 * @param   pDir        The parent directory. Can be NULL when creating the root
 *                      directory.
 * @param   pNode       The node to insert.
 */
static int nemufuseDirInsertChild(PNEMUFUSEDIR pDir, PNEMUFUSENODE pNode)
{
    if (!pDir)
    {
        /*
         * Special case: Root Directory.
         */
        AssertReturn(!g_pTreeRoot, VERR_ALREADY_EXISTS);
        AssertReturn(pNode->enmType == NEMUFUSETYPE_DIRECTORY, VERR_INTERNAL_ERROR);
        g_pTreeRoot = (PNEMUFUSEDIR)pNode;
    }
    else
    {
        /*
         * Common case.
         */
        if (!(pDir->cEntries % NEMUFUSE_DIR_GROW_BY))
        {
            void *pvNew = RTMemRealloc(pDir->paEntries, sizeof(*pDir->paEntries) * (pDir->cEntries + NEMUFUSE_DIR_GROW_BY));
            if (!pvNew)
                return VERR_NO_MEMORY;
            pDir->paEntries = (PNEMUFUSENODE *)pvNew;
        }
        pDir->paEntries[pDir->cEntries++] = pNode;
        pDir->Node.cLinks++;
    }

    nemufuseNodeRetain(pNode);
    pNode->cLinks++;
    return VINF_SUCCESS;
}


/**
 * Create a directory node.
 *
 * @returns Nemu status code.
 * @param   pszPath     The path to the directory.
 * @param   ppDir       Optional, where to return the new directory locked and
 *                      referenced (making cRefs == 2).
 */
static int nemufuseDirCreate(const char *pszPath, PNEMUFUSEDIR *ppDir)
{
    /*
     * Figure out where the directory is going.
     */
    const char *pszName;
    PNEMUFUSEDIR pParent;
    int rc = nemufuseTreeLookupParentForInsert(pszPath, &pszName, &pParent);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Allocate and initialize the new directory node.
     */
    PNEMUFUSEDIR pNewDir;
    rc = nemufuseNodeAlloc(sizeof(*pNewDir), pszName, NEMUFUSETYPE_DIRECTORY, pParent, (PNEMUFUSENODE *)&pNewDir);
    if (RT_SUCCESS(rc))
    {
        pNewDir->cEntries  = 0;
        pNewDir->paEntries = NULL;

        /*
         * Insert it.
         */
        rc = nemufuseDirInsertChild(pParent, &pNewDir->Node);
        if (    RT_SUCCESS(rc)
            &&  ppDir)
        {
            nemufuseNodeLockAndRetain(&pNewDir->Node);
            *ppDir = pNewDir;
        }
        nemufuseNodeRelease(&pNewDir->Node);
    }
    if (pParent)
        nemufuseNodeReleaseAndUnlock(&pParent->Node);
    return rc;
}


/**
 * Creates a flattened image
 *
 * @returns Nemu status code.
 * @param   pszPath         Where to create the flattened file in the FUSE file
 *                          system.
 * @param   pszImage        The image to flatten.
 * @param   ppFile          Where to return the pointer to the instance.
 *                          Optional.
 */
static int nemufuseFlatImageCreate(const char *pszPath, const char *pszImage, PNEMUFUSEFLATIMAGE *ppFile)
{
    /*
     * Check that we can create this file.
     */
    const char *pszName;
    PNEMUFUSEDIR pParent;
    int rc = nemufuseTreeLookupParentForInsert(pszPath, &pszName, &pParent);
    if (RT_FAILURE(rc))
        return rc;
    if (pParent)
        nemufuseNodeReleaseAndUnlock(&pParent->Node);

    /*
     * Try open the image file (without holding any locks).
     */
    char *pszFormat;
    VDTYPE enmType;
    rc = VDGetFormat(NULL /* pVDIIfsDisk */, NULL /* pVDIIfsImage*/, pszImage, &pszFormat, &enmType);
    if (RT_FAILURE(rc))
    {
        LogRel(("VDGetFormat(%s,) failed, rc=%Rrc\n", pszImage, rc));
        return rc;
    }

    PNEMUHDD pDisk = NULL;
    rc = VDCreate(NULL /* pVDIIfsDisk */, enmType, &pDisk);
    if (RT_SUCCESS(rc))
    {
        rc = VDOpen(pDisk, pszFormat, pszImage, 0, NULL /* pVDIfsImage */);
        if (RT_FAILURE(rc))
        {
            LogRel(("VDCreate(,%s,%s,,,) failed, rc=%Rrc\n", pszFormat, pszImage, rc));
            VDClose(pDisk, false /* fDeletes */);
        }
    }
    else
        Log(("VDCreate failed, rc=%Rrc\n", rc));
    if (RT_FAILURE(rc))
    {
        RTStrFree(pszFormat);
        return rc;
    }

    /*
     * Allocate and initialize the new directory node.
     */
    rc = nemufuseTreeLookupParentForInsert(pszPath, &pszName, &pParent);
    if (RT_SUCCESS(rc))
    {
        PNEMUFUSEFLATIMAGE pNewFlatImage;
        rc = nemufuseNodeAlloc(sizeof(*pNewFlatImage), pszName, NEMUFUSETYPE_FLAT_IMAGE, pParent, (PNEMUFUSENODE *)&pNewFlatImage);
        if (RT_SUCCESS(rc))
        {
            pNewFlatImage->pDisk          = pDisk;
            pNewFlatImage->pszFormat      = pszFormat;
            pNewFlatImage->cReaders       = VDIsReadOnly(pNewFlatImage->pDisk) ? INT32_MAX / 2 : 0;
            pNewFlatImage->cWriters       = 0;
            pNewFlatImage->Node.cbPrimary = VDGetSize(pNewFlatImage->pDisk, 0 /* base */);

            /*
             * Insert it.
             */
            rc = nemufuseDirInsertChild(pParent, &pNewFlatImage->Node);
            if (    RT_SUCCESS(rc)
                &&  ppFile)
            {
                nemufuseNodeLockAndRetain(&pNewFlatImage->Node);
                *ppFile = pNewFlatImage;
            }
            nemufuseNodeRelease(&pNewFlatImage->Node);
            pDisk = NULL;
        }
        if (pParent)
            nemufuseNodeReleaseAndUnlock(&pParent->Node);
    }
    if (RT_FAILURE(rc) && pDisk != NULL)
        VDClose(pDisk, false /* fDelete */);
    return rc;
}


//static int nemufuseTreeMkCtrlPipe(const char *pszPath, PNEMUFUSECTRLPIPE *ppPipe)
//{
//}


/**
 * Looks up a file in the tree.
 *
 * Upon successful return the returned node is both referenced and locked. The
 * call will have to release and unlock it.
 *
 * @returns Nemu status code
 * @param   pszPath     The path to the file.
 * @param   ppNode      Where to return the node.
 */
static int nemufuseTreeLookup(const char *pszPath, PNEMUFUSENODE *ppNode)
{
    /*
     * Root first.
     */
    const char *psz = pszPath;
    if (*psz != '/')
        return VERR_FILE_NOT_FOUND;

    PNEMUFUSEDIR pDir = g_pTreeRoot;
    nemufuseNodeLockAndRetain(&pDir->Node);

    do psz++;
    while (*psz == '/');
    if (!*psz)
    {
        /* looking for the root. */
        *ppNode = &pDir->Node;
        return VINF_SUCCESS;
    }

    /*
     * Take it bit by bit from here on.
     */
    for (;;)
    {
        /*
         * Find the length of the current directory entry and check if it must be file.
         */
        const char * const pszName = psz;
        psz = strchr(psz, '/');
        if (!psz)
            psz = strchr(pszName, '\0');
        size_t cchName = psz - pszName;

        bool fMustBeDir = *psz == '/';
        while (*psz == '/')
            psz++;

        /*
         * Look it up.
         * This is safe as the directory will hold a reference to each node
         * so the nodes cannot possibly be destroyed while we're searching them.
         */
        PNEMUFUSENODE   pNode = NULL;
        uint32_t        i = pDir->cEntries;
        PNEMUFUSENODE  *paEntries = pDir->paEntries;
        while (i-- > 0)
        {
            PNEMUFUSENODE pCur = paEntries[i];
            if (    pCur->cchName == cchName
                &&  !memcmp(pCur->pszName, pszName, cchName))
            {
                pNode = pCur;
                nemufuseNodeLockAndRetain(pNode);
                break;
            }
        }
        nemufuseNodeReleaseAndUnlock(&pDir->Node);

        if (!pNode)
            return *psz ? VERR_PATH_NOT_FOUND : VERR_FILE_NOT_FOUND;
        if (    fMustBeDir
            &&  pNode->enmType != NEMUFUSETYPE_DIRECTORY)
        {
            nemufuseNodeReleaseAndUnlock(pNode);
            return VERR_NOT_A_DIRECTORY;
        }

        /*
         * Are we done?
         */
        if (!*psz)
        {
            *ppNode = pNode;
            return VINF_SUCCESS;
        }

        /* advance */
        pDir = (PNEMUFUSEDIR)pNode;
    }
}


/**
 * Errno conversion wrapper around nemufuseTreeLookup().
 *
 * @returns 0 on success, negated errno on failure.
 * @param   pszPath     The path to the file.
 * @param   ppNode      Where to return the node.
 */
static int nemufuseTreeLookupErrno(const char *pszPath, PNEMUFUSENODE *ppNode)
{
    int rc = nemufuseTreeLookup(pszPath, ppNode);
    if (RT_SUCCESS(rc))
        return 0;
    return -RTErrConvertToErrno(rc);
}


/**
 * Looks up a parent directory in the tree.
 *
 * Upon successful return the returned directory is both referenced and locked.
 * The call will have to release and unlock it.
 *
 * @returns Nemu status code.
 *
 * @param   pszPath     The path to the file which parent we seek.
 * @param   ppszName    Where to return the pointer to the child's name within
 *                      pszPath.
 * @param   ppDir       Where to return the parent directory.
 */
static int nemufuseTreeLookupParent(const char *pszPath, const char **ppszName, PNEMUFUSEDIR *ppDir)
{
    /*
     * Root first.
     */
    const char *psz = pszPath;
    if (*psz != '/')
        return VERR_INVALID_PARAMETER;
    do psz++;
    while (*psz == '/');
    if (!*psz)
    {
        /* looking for the root. */
        *ppszName = psz + 1;
        *ppDir = NULL;
        return VINF_SUCCESS;
    }

    /*
     * Take it bit by bit from here on.
     */
    PNEMUFUSEDIR pDir = g_pTreeRoot;
    AssertReturn(pDir, VERR_WRONG_ORDER);
    nemufuseNodeLockAndRetain(&pDir->Node);
    for (;;)
    {
        /*
         * Find the length of the current directory entry and check if it must be file.
         */
        const char * const pszName = psz;
        psz = strchr(psz, '/');
        if (!psz)
        {
            /* that's all folks.*/
            *ppszName = pszName;
            *ppDir = pDir;
            return VINF_SUCCESS;
        }
        size_t cchName = psz - pszName;

        bool fMustBeDir = *psz == '/';
        while (*psz == '/')
            psz++;

        /* Trailing slashes are not allowed (because it's simpler without them). */
        if (!*psz)
            return VERR_INVALID_PARAMETER;

        /*
         * Look it up.
         * This is safe as the directory will hold a reference to each node
         * so the nodes cannot possibly be destroyed while we're searching them.
         */
        PNEMUFUSENODE   pNode = NULL;
        uint32_t        i = pDir->cEntries;
        PNEMUFUSENODE  *paEntries = pDir->paEntries;
        while (i-- > 0)
        {
            PNEMUFUSENODE pCur = paEntries[i];
            if (    pCur->cchName == cchName
                &&  !memcmp(pCur->pszName, pszName, cchName))
            {
                pNode = pCur;
                nemufuseNodeLockAndRetain(pNode);
                break;
            }
        }
        nemufuseNodeReleaseAndUnlock(&pDir->Node);

        if (!pNode)
            return VERR_FILE_NOT_FOUND;
        if (    fMustBeDir
            &&  pNode->enmType != NEMUFUSETYPE_DIRECTORY)
        {
            nemufuseNodeReleaseAndUnlock(pNode);
            return VERR_PATH_NOT_FOUND;
        }

        /* advance */
        pDir = (PNEMUFUSEDIR)pNode;
    }
}


/**
 * Looks up a parent directory in the tree and checking that the specified child
 * doesn't already exist.
 *
 * Upon successful return the returned directory is both referenced and locked.
 * The call will have to release and unlock it.
 *
 * @returns Nemu status code.
 *
 * @param   pszPath     The path to the file which parent we seek.
 * @param   ppszName    Where to return the pointer to the child's name within
 *                      pszPath.
 * @param   ppDir       Where to return the parent directory.
 */
static int nemufuseTreeLookupParentForInsert(const char *pszPath, const char **ppszName, PNEMUFUSEDIR *ppDir)
{
    /*
     * Lookup the parent directory using nemufuseTreeLookupParent first.
     */
    const char     *pszName;
    PNEMUFUSEDIR    pDir;
    int rc = nemufuseTreeLookupParent(pszPath, &pszName, &pDir);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check that it doesn't exist already
         */
        if (pDir)
        {
            size_t const    cchName   = strlen(pszName);
            uint32_t        i         = pDir->cEntries;
            PNEMUFUSENODE  *paEntries = pDir->paEntries;
            while (i-- > 0)
            {
                PNEMUFUSENODE pCur = paEntries[i];
                if (    pCur->cchName == cchName
                    &&  !memcmp(pCur->pszName, pszName, cchName))
                {
                    nemufuseNodeReleaseAndUnlock(&pDir->Node);
                    rc = VERR_ALREADY_EXISTS;
                    break;
                }
            }
        }
        if (RT_SUCCESS(rc))
        {
            *ppDir = pDir;
            *ppszName = pszName;
        }
    }
    return rc;
}





/** @copydoc fuse_operations::getattr */
static int nemufuseOp_getattr(const char *pszPath, struct stat *pStat)
{
    PNEMUFUSENODE pNode;
    int rc = nemufuseTreeLookupErrno(pszPath, &pNode);
    if (!rc)
    {
        nemufuseNodeFillStat(pNode, pStat);
        nemufuseNodeReleaseAndUnlock(pNode);
    }
    LogFlow(("nemufuseOp_getattr: rc=%d \"%s\"\n", rc, pszPath));
    return rc;
}


/** @copydoc fuse_operations::opendir */
static int nemufuseOp_opendir(const char *pszPath, struct fuse_file_info *pInfo)
{
    PNEMUFUSENODE pNode;
    int rc = nemufuseTreeLookupErrno(pszPath, &pNode);
    if (!rc)
    {
        /*
         * Check that it's a directory and that the caller should see it.
         */
        if (pNode->enmType != NEMUFUSETYPE_DIRECTORY)
            rc = -ENOTDIR;
        /** @todo access checks. */
        else
        {
            /** @todo update the accessed TS?    */

            /*
             * Put a reference to the node in the fuse_file_info::fh member so
             * we don't have to parse the path in readdir.
             */
            pInfo->fh = (uintptr_t)pNode;
            nemufuseNodeUnlock(pNode);
        }

        /* cleanup */
        if (rc)
            nemufuseNodeReleaseAndUnlock(pNode);
    }
    LogFlow(("nemufuseOp_opendir: rc=%d \"%s\"\n", rc, pszPath));
    return rc;
}


/** @copydoc fuse_operations::readdir */
static int nemufuseOp_readdir(const char *pszPath, void *pvBuf, fuse_fill_dir_t pfnFiller,
                              off_t offDir, struct fuse_file_info *pInfo)
{
    PNEMUFUSEDIR pDir = (PNEMUFUSEDIR)(uintptr_t)pInfo->fh;
    AssertPtr(pDir);
    Assert(pDir->Node.enmType == NEMUFUSETYPE_DIRECTORY);
    nemufuseNodeLock(&pDir->Node);
    LogFlow(("nemufuseOp_readdir: offDir=%llx \"%s\"\n", (uint64_t)offDir, pszPath));

#define NEMUFUSE_FAKE_DIRENT_SIZE   512

    /*
     * First the mandatory dot and dot-dot entries.
     */
    struct stat st;
    int rc = 0;
    if (!offDir)
    {
        offDir += NEMUFUSE_FAKE_DIRENT_SIZE;
        nemufuseNodeFillStat(&pDir->Node, &st);
        rc = pfnFiller(pvBuf, ".", &st, offDir);
    }
    if (    offDir == NEMUFUSE_FAKE_DIRENT_SIZE
        &&  !rc)
    {
        offDir += NEMUFUSE_FAKE_DIRENT_SIZE;
        rc = pfnFiller(pvBuf, "..", NULL, offDir);
    }

    /*
     * Convert the offset to a directory index and start/continue filling the buffer.
     * The entries only needs locking since the directory already has a reference
     * to each of them.
     */
    Assert(offDir >= NEMUFUSE_FAKE_DIRENT_SIZE * 2 || rc);
    uint32_t i = offDir / NEMUFUSE_FAKE_DIRENT_SIZE - 2;
    while (    !rc
           &&  i < pDir->cEntries)
    {
        PNEMUFUSENODE pNode = pDir->paEntries[i];
        nemufuseNodeLock(pNode);

        nemufuseNodeFillStat(pNode, &st);
        offDir = (i + 3) * NEMUFUSE_FAKE_DIRENT_SIZE;
        rc = pfnFiller(pvBuf, pNode->pszName, &st, offDir);

        nemufuseNodeUnlock(pNode);

        /* next */
        i++;
    }

    nemufuseNodeUnlock(&pDir->Node);
    LogFlow(("nemufuseOp_readdir: returns offDir=%llx\n", (uint64_t)offDir));
    return 0;
}


/** @copydoc fuse_operations::releasedir */
static int nemufuseOp_releasedir(const char *pszPath, struct fuse_file_info *pInfo)
{
    PNEMUFUSEDIR pDir = (PNEMUFUSEDIR)(uintptr_t)pInfo->fh;
    AssertPtr(pDir);
    Assert(pDir->Node.enmType == NEMUFUSETYPE_DIRECTORY);
    pInfo->fh = 0;
    nemufuseNodeRelease(&pDir->Node);
    LogFlow(("nemufuseOp_releasedir: \"%s\"\n", pszPath));
    return 0;
}


/** @copydoc fuse_operations::symlink */
static int nemufuseOp_symlink(const char *pszDst, const char *pszPath)
{
    /*
     * "Interface" for mounting a image.
     */
    int rc = nemufuseFlatImageCreate(pszPath, pszDst, NULL);
    if (RT_SUCCESS(rc))
    {
        Log(("nemufuseOp_symlink: \"%s\" => \"%s\" SUCCESS!\n", pszPath, pszDst));
        return 0;
    }

    LogFlow(("nemufuseOp_symlink: \"%s\" => \"%s\" rc=%Rrc\n", pszPath, pszDst, rc));
    return -RTErrConvertToErrno(rc);
}


/** @copydoc fuse_operations::open */
static int nemufuseOp_open(const char *pszPath, struct fuse_file_info *pInfo)
{
    LogFlow(("nemufuseOp_open(\"%s\", .flags=%#x)\n", pszPath, pInfo->flags));

    /*
     * Validate incoming flags.
     */
#ifdef RT_OS_DARWIN
    if (pInfo->flags & (O_APPEND | O_NONBLOCK | O_SYMLINK | O_NOCTTY | O_SHLOCK | O_EXLOCK | O_ASYNC
                        | O_CREAT | O_TRUNC | O_EXCL | O_EVTONLY))
        return -EINVAL;
    if ((pInfo->flags & O_ACCMODE) == O_ACCMODE)
        return -EINVAL;
#elif defined(RT_OS_LINUX)
    if (pInfo->flags & (  O_APPEND | O_ASYNC | O_DIRECT /* | O_LARGEFILE ? */
                        | O_NOATIME | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK
                        /* | O_SYNC ? */))
        return -EINVAL;
    if ((pInfo->flags & O_ACCMODE) == O_ACCMODE)
        return -EINVAL;
#elif defined(RT_OS_FREEBSD)
    if (pInfo->flags & (  O_APPEND | O_ASYNC | O_DIRECT /* | O_LARGEFILE ? */
                        | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK
                        /* | O_SYNC ? */))
        return -EINVAL;
    if ((pInfo->flags & O_ACCMODE) == O_ACCMODE)
        return -EINVAL;
#else
# error "Port me"
#endif

    PNEMUFUSENODE pNode;
    int rc = nemufuseTreeLookupErrno(pszPath, &pNode);
    if (!rc)
    {
        /*
         * Check flags and stuff.
         */
        switch (pNode->enmType)
        {
            /* not expected here? */
            case NEMUFUSETYPE_DIRECTORY:
                AssertFailed();
                rc = -EISDIR;
                break;

            case NEMUFUSETYPE_FLAT_IMAGE:
            {
                PNEMUFUSEFLATIMAGE pFlatImage = (PNEMUFUSEFLATIMAGE)pNode;
#ifdef O_DIRECTORY
                if (pInfo->flags & O_DIRECTORY)
                    rc = -ENOTDIR;
#endif
                if (    (pInfo->flags & O_ACCMODE) == O_WRONLY
                    ||  (pInfo->flags & O_ACCMODE) == O_RDWR)
                {
                    if (    pFlatImage->cWriters == 0
                        &&  pFlatImage->cReaders == 0)
                        pFlatImage->cWriters++;
                    else
                        rc = -ETXTBSY;
                }
                else if ((pInfo->flags & O_ACCMODE) == O_RDONLY)
                {
                    if (pFlatImage->cWriters == 0)
                    {
                        if (pFlatImage->cReaders + 1 < (  pFlatImage->cReaders < INT32_MAX / 2
                                                        ? INT32_MAX / 4
                                                        : INT32_MAX / 2 + INT32_MAX / 4) )
                            pFlatImage->cReaders++;
                        else
                            rc = -EMLINK;
                    }
                    else
                        rc = -ETXTBSY;
                }
                break;
            }

            case NEMUFUSETYPE_CONTROL_PIPE:
                rc = -ENOTSUP;
                break;

            default:
                rc = -EDOOFUS;
                break;
        }
        if (!rc)
        {
            /*
             * Put a reference to the node in the fuse_file_info::fh member so
             * we don't have to parse the path in the other file methods.
             */
            pInfo->fh = (uintptr_t)pNode;
            nemufuseNodeUnlock(pNode);
        }
        else
        {
            /* cleanup */
            nemufuseNodeReleaseAndUnlock(pNode);
        }
    }
    LogFlow(("nemufuseOp_opendir: rc=%d \"%s\"\n", rc, pszPath));
    return rc;
}


/** @copydoc fuse_operations::release */
static int nemufuseOp_release(const char *pszPath, struct fuse_file_info *pInfo)
{
    PNEMUFUSENODE pNode = (PNEMUFUSENODE)(uintptr_t)pInfo->fh;
    AssertPtr(pNode);
    pInfo->fh = 0;

    switch (pNode->enmType)
    {
        case NEMUFUSETYPE_DIRECTORY:
            /* nothing to do */
            nemufuseNodeRelease(pNode);
            break;

        case NEMUFUSETYPE_FLAT_IMAGE:
        {
            PNEMUFUSEFLATIMAGE pFlatImage = (PNEMUFUSEFLATIMAGE)pNode;
            nemufuseNodeLock(&pFlatImage->Node);

            if (    (pInfo->flags & O_ACCMODE) == O_WRONLY
                ||  (pInfo->flags & O_ACCMODE) == O_RDWR)
            {
                pFlatImage->cWriters--;
                Assert(pFlatImage->cWriters >= 0);
            }
            else if ((pInfo->flags & O_ACCMODE) == O_RDONLY)
            {
                pFlatImage->cReaders--;
                Assert(pFlatImage->cReaders >= 0);
            }
            else
                AssertFailed();

            nemufuseNodeReleaseAndUnlock(&pFlatImage->Node);
            break;
        }

        case NEMUFUSETYPE_CONTROL_PIPE:
            /* nothing to do yet */
            nemufuseNodeRelease(pNode);
            break;

        default:
            AssertMsgFailed(("%s\n", pszPath));
            return -EDOOFUS;
    }

    LogFlow(("nemufuseOp_release: \"%s\"\n", pszPath));
    return 0;
}

/** The VDRead/VDWrite block granularity. */
#define NEMUFUSE_MIN_SIZE               512
/** Offset mask corresponding to NEMUFUSE_MIN_SIZE. */
#define NEMUFUSE_MIN_SIZE_MASK_OFF      (0x1ff)
/** Block mask corresponding to NEMUFUSE_MIN_SIZE. */
#define NEMUFUSE_MIN_SIZE_MASK_BLK      (~UINT64_C(0x1ff))

/** @copydoc fuse_operations::read */
static int nemufuseOp_read(const char *pszPath, char *pbBuf, size_t cbBuf,
                           off_t offFile, struct fuse_file_info *pInfo)
{
    /* paranoia */
    AssertReturn((int)cbBuf >= 0, -EINVAL);
    AssertReturn((unsigned)cbBuf == cbBuf, -EINVAL);
    AssertReturn(offFile >= 0, -EINVAL);
    AssertReturn((off_t)(offFile + cbBuf) >= offFile, -EINVAL);

    PNEMUFUSENODE pNode = (PNEMUFUSENODE)(uintptr_t)pInfo->fh;
    AssertPtr(pNode);
    switch (pNode->enmType)
    {
        case NEMUFUSETYPE_DIRECTORY:
            return -ENOTSUP;

        case NEMUFUSETYPE_FLAT_IMAGE:
        {
            PNEMUFUSEFLATIMAGE pFlatImage = (PNEMUFUSEFLATIMAGE)(uintptr_t)pInfo->fh;
            LogFlow(("nemufuseOp_read: offFile=%#llx cbBuf=%#zx pszPath=\"%s\"\n", (uint64_t)offFile, cbBuf, pszPath));
            nemufuseNodeLock(&pFlatImage->Node);

            int rc;
            if ((off_t)(offFile + cbBuf) < offFile)
                rc = -EINVAL;
            else if (offFile >= pFlatImage->Node.cbPrimary)
                rc = 0;
            else if (!cbBuf)
                rc = 0;
            else
            {
                /* Adjust for EOF. */
                if ((off_t)(offFile + cbBuf) >= pFlatImage->Node.cbPrimary)
                    cbBuf = pFlatImage->Node.cbPrimary - offFile;

                /*
                 * Aligned read?
                 */
                int rc2;
                if (    !(offFile & NEMUFUSE_MIN_SIZE_MASK_OFF)
                    &&  !(cbBuf   & NEMUFUSE_MIN_SIZE_MASK_OFF))
                    rc2 = VDRead(pFlatImage->pDisk, offFile, pbBuf, cbBuf);
                else
                {
                    /*
                     * Unaligned read - lots of extra work.
                     */
                    uint8_t abBlock[NEMUFUSE_MIN_SIZE];
                    if (((offFile + cbBuf) & NEMUFUSE_MIN_SIZE_MASK_BLK) == (offFile & NEMUFUSE_MIN_SIZE_MASK_BLK))
                    {
                        /* a single partial block. */
                        rc2 = VDRead(pFlatImage->pDisk, offFile & NEMUFUSE_MIN_SIZE_MASK_BLK, abBlock, NEMUFUSE_MIN_SIZE);
                        if (RT_SUCCESS(rc2))
                            memcpy(pbBuf, &abBlock[offFile & NEMUFUSE_MIN_SIZE_MASK_OFF], cbBuf);
                    }
                    else
                    {
                        /* read unaligned head. */
                        rc2 = VINF_SUCCESS;
                        if (offFile & NEMUFUSE_MIN_SIZE_MASK_OFF)
                        {
                            rc2 = VDRead(pFlatImage->pDisk, offFile & NEMUFUSE_MIN_SIZE_MASK_BLK, abBlock, NEMUFUSE_MIN_SIZE);
                            if (RT_SUCCESS(rc2))
                            {
                                size_t cbCopy = NEMUFUSE_MIN_SIZE - (offFile & NEMUFUSE_MIN_SIZE_MASK_OFF);
                                memcpy(pbBuf, &abBlock[offFile & NEMUFUSE_MIN_SIZE_MASK_OFF], cbCopy);
                                pbBuf   += cbCopy;
                                offFile += cbCopy;
                                cbBuf   -= cbCopy;
                            }
                        }

                        /* read the middle. */
                        Assert(!(offFile & NEMUFUSE_MIN_SIZE_MASK_OFF));
                        if (cbBuf >= NEMUFUSE_MIN_SIZE && RT_SUCCESS(rc2))
                        {
                            size_t cbRead = cbBuf & NEMUFUSE_MIN_SIZE_MASK_BLK;
                            rc2 = VDRead(pFlatImage->pDisk, offFile, pbBuf, cbRead);
                            if (RT_SUCCESS(rc2))
                            {
                                pbBuf   += cbRead;
                                offFile += cbRead;
                                cbBuf   -= cbRead;
                            }
                        }

                        /* unaligned tail read. */
                        Assert(cbBuf < NEMUFUSE_MIN_SIZE);
                        Assert(!(offFile & NEMUFUSE_MIN_SIZE_MASK_OFF));
                        if (cbBuf && RT_SUCCESS(rc2))
                        {
                            rc2 = VDRead(pFlatImage->pDisk, offFile, abBlock, NEMUFUSE_MIN_SIZE);
                            if (RT_SUCCESS(rc2))
                                memcpy(pbBuf, &abBlock[0], cbBuf);
                        }
                    }
                }

                /* convert the return code */
                if (RT_SUCCESS(rc2))
                    rc = cbBuf;
                else
                    rc = -RTErrConvertToErrno(rc2);
            }

            nemufuseNodeUnlock(&pFlatImage->Node);
            return rc;
        }

        case NEMUFUSETYPE_CONTROL_PIPE:
            return -ENOTSUP;

        default:
            AssertMsgFailed(("%s\n", pszPath));
            return -EDOOFUS;
    }
}


/** @copydoc fuse_operations::write */
static int nemufuseOp_write(const char *pszPath, const char *pbBuf, size_t cbBuf,
                           off_t offFile, struct fuse_file_info *pInfo)
{
    /* paranoia */
    AssertReturn((int)cbBuf >= 0, -EINVAL);
    AssertReturn((unsigned)cbBuf == cbBuf, -EINVAL);
    AssertReturn(offFile >= 0, -EINVAL);
    AssertReturn((off_t)(offFile + cbBuf) >= offFile, -EINVAL);

    PNEMUFUSENODE pNode = (PNEMUFUSENODE)(uintptr_t)pInfo->fh;
    AssertPtr(pNode);
    switch (pNode->enmType)
    {
        case NEMUFUSETYPE_DIRECTORY:
            return -ENOTSUP;

        case NEMUFUSETYPE_FLAT_IMAGE:
        {
            PNEMUFUSEFLATIMAGE pFlatImage = (PNEMUFUSEFLATIMAGE)(uintptr_t)pInfo->fh;
            LogFlow(("nemufuseOp_write: offFile=%#llx cbBuf=%#zx pszPath=\"%s\"\n", (uint64_t)offFile, cbBuf, pszPath));
            nemufuseNodeLock(&pFlatImage->Node);

            int rc;
            if ((off_t)(offFile + cbBuf) < offFile)
                rc = -EINVAL;
            else if (offFile >= pFlatImage->Node.cbPrimary)
                rc = 0;
            else if (!cbBuf)
                rc = 0;
            else
            {
                /* Adjust for EOF. */
                if ((off_t)(offFile + cbBuf) >= pFlatImage->Node.cbPrimary)
                    cbBuf = pFlatImage->Node.cbPrimary - offFile;

                /*
                 * Aligned write?
                 */
                int rc2;
                if (    !(offFile & NEMUFUSE_MIN_SIZE_MASK_OFF)
                    &&  !(cbBuf   & NEMUFUSE_MIN_SIZE_MASK_OFF))
                    rc2 = VDWrite(pFlatImage->pDisk, offFile, pbBuf, cbBuf);
                else
                {
                    /*
                     * Unaligned write - lots of extra work.
                     */
                    uint8_t abBlock[NEMUFUSE_MIN_SIZE];
                    if (((offFile + cbBuf) & NEMUFUSE_MIN_SIZE_MASK_BLK) == (offFile & NEMUFUSE_MIN_SIZE_MASK_BLK))
                    {
                        /* a single partial block. */
                        rc2 = VDRead(pFlatImage->pDisk, offFile & NEMUFUSE_MIN_SIZE_MASK_BLK, abBlock, NEMUFUSE_MIN_SIZE);
                        if (RT_SUCCESS(rc2))
                        {
                            memcpy(&abBlock[offFile & NEMUFUSE_MIN_SIZE_MASK_OFF], pbBuf, cbBuf);
                            /* Update the block */
                            rc2 = VDWrite(pFlatImage->pDisk, offFile & NEMUFUSE_MIN_SIZE_MASK_BLK, abBlock, NEMUFUSE_MIN_SIZE);
                        }
                    }
                    else
                    {
                        /* read unaligned head. */
                        rc2 = VINF_SUCCESS;
                        if (offFile & NEMUFUSE_MIN_SIZE_MASK_OFF)
                        {
                            rc2 = VDRead(pFlatImage->pDisk, offFile & NEMUFUSE_MIN_SIZE_MASK_BLK, abBlock, NEMUFUSE_MIN_SIZE);
                            if (RT_SUCCESS(rc2))
                            {
                                size_t cbCopy = NEMUFUSE_MIN_SIZE - (offFile & NEMUFUSE_MIN_SIZE_MASK_OFF);
                                memcpy(&abBlock[offFile & NEMUFUSE_MIN_SIZE_MASK_OFF], pbBuf, cbCopy);
                                pbBuf   += cbCopy;
                                offFile += cbCopy;
                                cbBuf   -= cbCopy;
                                rc2 = VDWrite(pFlatImage->pDisk, offFile & NEMUFUSE_MIN_SIZE_MASK_BLK, abBlock, NEMUFUSE_MIN_SIZE);
                            }
                        }

                        /* write the middle. */
                        Assert(!(offFile & NEMUFUSE_MIN_SIZE_MASK_OFF));
                        if (cbBuf >= NEMUFUSE_MIN_SIZE && RT_SUCCESS(rc2))
                        {
                            size_t cbWrite = cbBuf & NEMUFUSE_MIN_SIZE_MASK_BLK;
                            rc2 = VDWrite(pFlatImage->pDisk, offFile, pbBuf, cbWrite);
                            if (RT_SUCCESS(rc2))
                            {
                                pbBuf   += cbWrite;
                                offFile += cbWrite;
                                cbBuf   -= cbWrite;
                            }
                        }

                        /* unaligned tail write. */
                        Assert(cbBuf < NEMUFUSE_MIN_SIZE);
                        Assert(!(offFile & NEMUFUSE_MIN_SIZE_MASK_OFF));
                        if (cbBuf && RT_SUCCESS(rc2))
                        {
                            rc2 = VDRead(pFlatImage->pDisk, offFile, abBlock, NEMUFUSE_MIN_SIZE);
                            if (RT_SUCCESS(rc2))
                            {
                                memcpy(&abBlock[0], pbBuf, cbBuf);
                                rc2 = VDWrite(pFlatImage->pDisk, offFile, abBlock, NEMUFUSE_MIN_SIZE);
                            }
                        }
                    }
                }

                /* convert the return code */
                if (RT_SUCCESS(rc2))
                    rc = cbBuf;
                else
                    rc = -RTErrConvertToErrno(rc2);
            }

            nemufuseNodeUnlock(&pFlatImage->Node);
            return rc;
        }

        case NEMUFUSETYPE_CONTROL_PIPE:
            return -ENOTSUP;

        default:
            AssertMsgFailed(("%s\n", pszPath));
            return -EDOOFUS;
    }
}


/**
 * The FUSE operations.
 *
 * @remarks We'll initialize this manually since we cannot use C99 style
 *          initialzer designations in C++ (yet).
 */
static struct fuse_operations   g_nemufuseOps;



int main(int argc, char **argv)
{
    /*
     * Initialize the runtime and VD.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
    {
        RTStrmPrintf(g_pStdErr, "NemuFUSE: RTR3InitExe failed, rc=%Rrc\n", rc);
        return 1;
    }
    RTPrintf("NemuFUSE: Hello...\n");
    rc = VDInit();
    if (RT_FAILURE(rc))
    {
        RTStrmPrintf(g_pStdErr, "NemuFUSE: VDInit failed, rc=%Rrc\n", rc);
        return 1;
    }

    /*
     * Initializes the globals and populate the file hierarchy.
     */
    rc = nemufuseDirCreate("/", NULL);
    if (RT_SUCCESS(rc))
        rc = nemufuseDirCreate("/FlattenedImages", NULL);
    if (RT_FAILURE(rc))
    {
        RTStrmPrintf(g_pStdErr, "NemuFUSE: nemufuseDirCreate failed, rc=%Rrc\n", rc);
        return 1;
    }

    /*
     * Initialize the g_nemufuseOps. (C++ sucks!)
     */
    memset(&g_nemufuseOps, 0, sizeof(g_nemufuseOps));
    g_nemufuseOps.getattr    = nemufuseOp_getattr;
    g_nemufuseOps.opendir    = nemufuseOp_opendir;
    g_nemufuseOps.readdir    = nemufuseOp_readdir;
    g_nemufuseOps.releasedir = nemufuseOp_releasedir;
    g_nemufuseOps.symlink    = nemufuseOp_symlink;
    g_nemufuseOps.open       = nemufuseOp_open;
    g_nemufuseOps.read       = nemufuseOp_read;
    g_nemufuseOps.write      = nemufuseOp_write;
    g_nemufuseOps.release    = nemufuseOp_release;

    /*
     * Hand control over to libfuse.
     */

#if 0
    /** @todo multithreaded fun. */
#else
    rc = fuse_main(argc, argv, &g_nemufuseOps, NULL);
#endif
    RTPrintf("NemuFUSE: fuse_main -> %d\n", rc);
    return rc;
}

