/* $Id: cr_sortarray.h $ */

/** @file
 * Sorted array API
 */

/*
 * Copyright (C) 2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___cr_sortarray_h_
#define ___cr_sortarray_h_

#include <iprt/types.h>
#include <iprt/assert.h>

typedef struct CR_SORTARRAY
{
    uint32_t cBufferSize;
    uint32_t cSize;
    uint64_t *pElements;
} CR_SORTARRAY;


#ifndef IN_RING0
# define NEMUSADECL(_type) DECLEXPORT(_type)
#else
# define NEMUSADECL(_type) RTDECL(_type)
#endif


DECLINLINE(uint32_t) CrSaGetSize(const CR_SORTARRAY *pArray)
{
    return pArray->cSize;
}

DECLINLINE(uint64_t) CrSaGetVal(const CR_SORTARRAY *pArray, uint32_t i)
{
    Assert(i < pArray->cSize);
    return pArray->pElements[i];
}

DECLINLINE(const uint64_t*) CrSaGetElements(const CR_SORTARRAY *pArray)
{
    return pArray->pElements;
}

DECLINLINE(void) CrSaClear(CR_SORTARRAY *pArray)
{
    pArray->cSize = 0;
}

NEMUSADECL(int) CrSaInit(CR_SORTARRAY *pArray, uint32_t cInitBuffer);
NEMUSADECL(void) CrSaCleanup(CR_SORTARRAY *pArray);
/*
 * @return true if element is found */
NEMUSADECL(bool) CrSaContains(const CR_SORTARRAY *pArray, uint64_t element);

/*
 * @return VINF_SUCCESS  if element is added
 * VINF_ALREADY_INITIALIZED if element was in array already
 * VERR_NO_MEMORY - no memory
 *  */
NEMUSADECL(int) CrSaAdd(CR_SORTARRAY *pArray, uint64_t element);

/*
 * @return VINF_SUCCESS  if element is removed
 * VINF_ALREADY_INITIALIZED if element was NOT in array
 *  */
NEMUSADECL(int) CrSaRemove(CR_SORTARRAY *pArray, uint64_t element);

/*
 * @return VINF_SUCCESS on success
 * VERR_NO_MEMORY - no memory
 *  */
NEMUSADECL(void) CrSaIntersect(CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2);
NEMUSADECL(int) CrSaIntersected(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2, CR_SORTARRAY *pResult);

/*
 * @return VINF_SUCCESS on success
 * VERR_NO_MEMORY - no memory
 *  */
NEMUSADECL(int) CrSaUnited(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2, CR_SORTARRAY *pResult);

/*
 * @return VINF_SUCCESS on success
 * VERR_NO_MEMORY - no memory
 *  */
NEMUSADECL(int) CrSaClone(const CR_SORTARRAY *pArray1, CR_SORTARRAY *pResult);

NEMUSADECL(int) CrSaCmp(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2);

NEMUSADECL(bool) CrSaCovers(const CR_SORTARRAY *pArray1, const CR_SORTARRAY *pArray2);

#endif /* ___cr_sortarray_h_ */
