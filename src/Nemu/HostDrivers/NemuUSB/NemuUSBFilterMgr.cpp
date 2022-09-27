/* $Id: NemuUSBFilterMgr.cpp $ */
/** @file
 * VirtualBox Ring-0 USB Filter Manager.
 */

/*
 * Copyright (C) 2007-2015 Oracle Corporation
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
#include <Nemu/usbfilter.h>
#include "NemuUSBFilterMgr.h"

#include <iprt/mem.h>
#ifdef NEMUUSBFILTERMGR_USB_SPINLOCK
# include <iprt/spinlock.h>
#else
# include <iprt/semaphore.h>
#endif
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** @def NEMUUSBFILTERMGR_LOCK
 * Locks the filter list. Careful with scoping since this may
 * create a temporary variable. Don't call twice in the same function.
 */

/** @def NEMUUSBFILTERMGR_UNLOCK
 * Unlocks the filter list.
 */
#ifdef NEMUUSBFILTERMGR_USB_SPINLOCK

# define NEMUUSBFILTERMGR_LOCK() \
    RTSpinlockAcquire(g_Spinlock)

# define NEMUUSBFILTERMGR_UNLOCK() \
    RTSpinlockRelease(g_Spinlock)

#else

# define NEMUUSBFILTERMGR_LOCK() \
    do { int rc2 = RTSemFastMutexRequest(g_Mtx); AssertRC(rc2); } while (0)

# define NEMUUSBFILTERMGR_UNLOCK() \
    do { int rc2 = RTSemFastMutexRelease(g_Mtx); AssertRC(rc2); } while (0)

#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to an NemuUSB filter. */
typedef struct NEMUUSBFILTER *PNEMUUSBFILTER;
/** Pointer to PNEMUUSBFILTER. */
typedef PNEMUUSBFILTER *PPNEMUUSBFILTER;

/**
 * NemuUSB internal filter representation.
 */
typedef struct NEMUUSBFILTER
{
    /** The core filter. */
    USBFILTER       Core;
    /** The filter owner. */
    NEMUUSBFILTER_CONTEXT       Owner;
    /** The filter Id. */
    uintptr_t       uId;
    /** Pointer to the next filter in the list. */
    PNEMUUSBFILTER  pNext;
} NEMUUSBFILTER;

/**
 * NemuUSB filter list.
 */
typedef struct NEMUUSBFILTERLIST
{
    /** The head pointer. */
    PNEMUUSBFILTER      pHead;
    /** The tail pointer. */
    PNEMUUSBFILTER      pTail;
} NEMUUSBFILTERLIST;
/** Pointer to a NEMUUSBFILTERLIST. */
typedef NEMUUSBFILTERLIST *PNEMUUSBFILTERLIST;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef NEMUUSBFILTERMGR_USB_SPINLOCK
/** Spinlock protecting the filter lists. */
static RTSPINLOCK           g_Spinlock = NIL_RTSPINLOCK;
#else
/** Mutex protecting the filter lists. */
static RTSEMFASTMUTEX       g_Mtx = NIL_RTSEMFASTMUTEX;
#endif
/** The per-type filter lists.
 * @remark The first entry is empty (USBFILTERTYPE_INVALID). */
static NEMUUSBFILTERLIST    g_aLists[USBFILTERTYPE_END];



/**
 * Initializes the NemuUSB filter manager.
 *
 * @returns IPRT status code.
 */
int NemuUSBFilterInit(void)
{
#ifdef NEMUUSBFILTERMGR_USB_SPINLOCK
    int rc = RTSpinlockCreate(&g_Spinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "NemuUSBFilter");
#else
    int rc = RTSemFastMutexCreate(&g_Mtx);
#endif
    if (RT_SUCCESS(rc))
    {
        /* not really required, but anyway... */
        for (unsigned i = USBFILTERTYPE_FIRST; i < RT_ELEMENTS(g_aLists); i++)
            g_aLists[i].pHead = g_aLists[i].pTail = NULL;
    }
    return rc;
}


/**
 * Internal worker that frees a filter.
 *
 * @param   pFilter     The filter to free.
 */
static void nemuUSBFilterFree(PNEMUUSBFILTER pFilter)
{
    USBFilterDelete(&pFilter->Core);
    pFilter->Owner = NEMUUSBFILTER_CONTEXT_NIL;
    pFilter->pNext = NULL;
    RTMemFree(pFilter);
}


/**
 * Terminates the NemuUSB filter manager.
 */
void NemuUSBFilterTerm(void)
{
#ifdef NEMUUSBFILTERMGR_USB_SPINLOCK
    RTSpinlockDestroy(g_Spinlock);
    g_Spinlock = NIL_RTSPINLOCK;
#else
    RTSemFastMutexDestroy(g_Mtx);
    g_Mtx = NIL_RTSEMFASTMUTEX;
#endif

    for (unsigned i = USBFILTERTYPE_FIRST; i < RT_ELEMENTS(g_aLists); i++)
    {
        PNEMUUSBFILTER pCur = g_aLists[i].pHead;
        g_aLists[i].pHead = g_aLists[i].pTail = NULL;
        while (pCur)
        {
            PNEMUUSBFILTER pNext = pCur->pNext;
            nemuUSBFilterFree(pCur);
            pCur = pNext;
        }
    }
}


/**
 * Adds a new filter.
 *
 * The filter will be validate, duplicated and added.
 *
 * @returns IPRT status code.
 * @param   pFilter     The filter.
 * @param   Owner       The filter owner. Must be non-zero.
 * @param   puId        Where to store the filter ID.
 */
int NemuUSBFilterAdd(PCUSBFILTER pFilter, NEMUUSBFILTER_CONTEXT Owner, uintptr_t *puId)
{
    /*
     * Validate input.
     */
    int rc = USBFilterValidate(pFilter);
    if (RT_FAILURE(rc))
        return rc;
    if (!Owner || Owner == NEMUUSBFILTER_CONTEXT_NIL)
        return VERR_INVALID_PARAMETER;
    if (!VALID_PTR(puId))
        return VERR_INVALID_POINTER;

    /*
     * Allocate a new filter.
     */
    PNEMUUSBFILTER pNew = (PNEMUUSBFILTER)RTMemAlloc(sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;
    memcpy(&pNew->Core, pFilter, sizeof(pNew->Core));
    pNew->Owner = Owner;
    pNew->uId   = (uintptr_t)pNew;
    pNew->pNext = NULL;

    *puId = pNew->uId;

    /*
     * Insert it.
     */
    PNEMUUSBFILTERLIST pList = &g_aLists[pFilter->enmType];

    NEMUUSBFILTERMGR_LOCK();

    if (pList->pTail)
        pList->pTail->pNext = pNew;
    else
        pList->pHead = pNew;
    pList->pTail = pNew;

    NEMUUSBFILTERMGR_UNLOCK();

    return VINF_SUCCESS;
}


/**
 * Removes an existing filter.
 *
 * The filter will be validate, duplicated and added.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if successfully removed.
 * @retval  VERR_FILE_NOT_FOUND if the specified filter/owner cannot be found.
 *
 * @param   Owner       The filter owner.
 * @param   uId         The ID of the filter that's to be removed.
 *                      Returned by NemuUSBFilterAdd().
 */
int NemuUSBFilterRemove(NEMUUSBFILTER_CONTEXT Owner, uintptr_t uId)
{
    /*
     * Validate input.
     */
    if (!uId)
        return VERR_INVALID_PARAMETER;
    if (!Owner || Owner == NEMUUSBFILTER_CONTEXT_NIL)
        return VERR_INVALID_PARAMETER;

    /*
     * Locate and unlink it.
     */
    PNEMUUSBFILTER pCur = NULL;

    NEMUUSBFILTERMGR_LOCK();

    for (unsigned i = USBFILTERTYPE_FIRST; !pCur && i < RT_ELEMENTS(g_aLists); i++)
    {
        PNEMUUSBFILTER pPrev = NULL;
        pCur = g_aLists[i].pHead;
        while (pCur)
        {
            if (    pCur->uId == uId
                &&  pCur->Owner == Owner)
            {
                PNEMUUSBFILTER pNext = pCur->pNext;
                if (pPrev)
                    pPrev->pNext = pNext;
                else
                    g_aLists[i].pHead = pNext;
                if (!pNext)
                    g_aLists[i].pTail = pPrev;
                break;
            }

            pPrev = pCur;
            pCur = pCur->pNext;
        }
    }

    NEMUUSBFILTERMGR_UNLOCK();

    /*
     * Free it (if found).
     */
    if (pCur)
    {
        nemuUSBFilterFree(pCur);
        return VINF_SUCCESS;
    }

    return VERR_FILE_NOT_FOUND;
}

NEMUUSBFILTER_CONTEXT NemuUSBFilterGetOwner(uintptr_t uId)
{
    Assert(uId);
    /*
     * Validate input.
     */
    if (!uId)
        return NEMUUSBFILTER_CONTEXT_NIL;

    /*
     * Result.
     */
    NEMUUSBFILTER_CONTEXT Owner = NEMUUSBFILTER_CONTEXT_NIL;

    NEMUUSBFILTERMGR_LOCK();

    for (unsigned i = USBFILTERTYPE_FIRST; i < RT_ELEMENTS(g_aLists); i++)
    {
        for (PNEMUUSBFILTER pCur = g_aLists[i].pHead; pCur; pCur = pCur->pNext)
        {
            if (pCur->uId == uId)
            {
                Owner = pCur->Owner;
                Assert(Owner != NEMUUSBFILTER_CONTEXT_NIL);
                break;
            }
        }
    }

    Assert(Owner != NEMUUSBFILTER_CONTEXT_NIL);

    NEMUUSBFILTERMGR_UNLOCK();

    return Owner;
}

/**
 * Removes all filters belonging to the specified owner.
 *
 * This is typically called when an owner disconnects or
 * terminates unexpectedly.
 *
 * @param   Owner       The owner
 */
void NemuUSBFilterRemoveOwner(NEMUUSBFILTER_CONTEXT Owner)
{
    /*
     * Collect the filters that should be freed.
     */
    PNEMUUSBFILTER pToFree = NULL;

    NEMUUSBFILTERMGR_LOCK();

    for (unsigned i = USBFILTERTYPE_FIRST; i < RT_ELEMENTS(g_aLists); i++)
    {
        PNEMUUSBFILTER pPrev = NULL;
        PNEMUUSBFILTER pCur = g_aLists[i].pHead;
        while (pCur)
        {
            if (pCur->Owner == Owner)
            {
                PNEMUUSBFILTER pNext = pCur->pNext;
                if (pPrev)
                    pPrev->pNext = pNext;
                else
                    g_aLists[i].pHead = pNext;
                if (!pNext)
                    g_aLists[i].pTail = pPrev;

                pCur->pNext = pToFree;
                pToFree = pCur;

                pCur = pNext;
            }
            else
            {
                pPrev = pCur;
                pCur = pCur->pNext;
            }
        }
    }

    NEMUUSBFILTERMGR_UNLOCK();

    /*
     * Free any filters we've found.
     */
    while (pToFree)
    {
        PNEMUUSBFILTER pNext = pToFree->pNext;
        nemuUSBFilterFree(pToFree);
        pToFree = pNext;
    }
}

/**
 * Match the specified device against the filters.
 * Unlike the NemuUSBFilterMatch, returns Owner also if exclude filter is matched
 *
 * @returns Owner on if matched, NEMUUSBFILTER_CONTEXT_NIL it not matched.
 * @param   pDevice             The device data as a filter structure.
 *                              See USBFilterMatch for how to construct this.
 * @param   puId                Where to store the filter id (optional).
 * @param   fRemoveFltIfOneShot Whether or not to remove one-shot filters on
 *                              match.
 * @param   pfFilter            Where to store whether the device must be filtered or not
 * @param   pfIsOneShot         Where to return whetehr the match was a one-shot
 *                              filter or not.  Optional.
 *
 */
NEMUUSBFILTER_CONTEXT NemuUSBFilterMatchEx(PCUSBFILTER pDevice, uintptr_t *puId,
                                           bool fRemoveFltIfOneShot, bool *pfFilter, bool *pfIsOneShot)
{
    /*
     * Validate input.
     */
    int rc = USBFilterValidate(pDevice);
    AssertRCReturn(rc, NEMUUSBFILTER_CONTEXT_NIL);

    *pfFilter = false;
    if (puId)
        *puId = 0;

    /*
     * Search the lists for a match.
     * (The lists are ordered by priority.)
     */
    NEMUUSBFILTERMGR_LOCK();

    for (unsigned i = USBFILTERTYPE_FIRST; i < RT_ELEMENTS(g_aLists); i++)
    {
        PNEMUUSBFILTER pPrev = NULL;
        PNEMUUSBFILTER pCur = g_aLists[i].pHead;
        while (pCur)
        {
            if (USBFilterMatch(&pCur->Core, pDevice))
            {
                /*
                 * Take list specific actions and return.
                 *
                 * The code does NOT implement the case where there are two or more
                 * filter clients, and one of them is releasing a device that's
                 * requested by some of the others. It's just too much work for a
                 * situation that noone will encounter.
                 */
                if (puId)
                    *puId = pCur->uId;
                NEMUUSBFILTER_CONTEXT Owner = pCur->Owner;
                *pfFilter = !!(i != USBFILTERTYPE_IGNORE
                            && i != USBFILTERTYPE_ONESHOT_IGNORE);

                if (    i == USBFILTERTYPE_ONESHOT_IGNORE
                    ||  i == USBFILTERTYPE_ONESHOT_CAPTURE)
                {
                    if (fRemoveFltIfOneShot)
                    {
                        /* unlink */
                        PNEMUUSBFILTER pNext = pCur->pNext;
                        if (pPrev)
                            pPrev->pNext = pNext;
                        else
                            g_aLists[i].pHead = pNext;
                        if (!pNext)
                            g_aLists[i].pTail = pPrev;
                    }
                }

                NEMUUSBFILTERMGR_UNLOCK();

                if (    i == USBFILTERTYPE_ONESHOT_IGNORE
                    ||  i == USBFILTERTYPE_ONESHOT_CAPTURE)
                {
                    if (fRemoveFltIfOneShot)
                    {
                        nemuUSBFilterFree(pCur);
                    }
                    if (pfIsOneShot)
                        *pfIsOneShot = true;
                }
                else
                {
                    if (pfIsOneShot)
                        *pfIsOneShot = false;
                }
                return Owner;
            }

            pPrev = pCur;
            pCur = pCur->pNext;
        }
    }

    NEMUUSBFILTERMGR_UNLOCK();
    return NEMUUSBFILTER_CONTEXT_NIL;
}

/**
 * Match the specified device against the filters.
 *
 * @returns Owner on if matched, NEMUUSBFILTER_CONTEXT_NIL it not matched.
 * @param   pDevice     The device data as a filter structure.
 *                      See USBFilterMatch for how to construct this.
 * @param   puId        Where to store the filter id (optional).
 */
NEMUUSBFILTER_CONTEXT NemuUSBFilterMatch(PCUSBFILTER pDevice, uintptr_t *puId)
{
    bool fFilter = false;
    NEMUUSBFILTER_CONTEXT Owner = NemuUSBFilterMatchEx(pDevice, puId,
                                    true, /* remove filter is it's a one-shot*/
                                    &fFilter, NULL /* bool * fIsOneShot */);
    if (fFilter)
    {
        Assert(Owner != NEMUUSBFILTER_CONTEXT_NIL);
        return Owner;
    }
    return NEMUUSBFILTER_CONTEXT_NIL;
}

