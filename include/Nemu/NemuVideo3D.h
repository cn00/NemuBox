/** @file
 *
 * VirtualBox 3D common tooling
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
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___Nemu_NemuVideo3D_h
#define ___Nemu_NemuVideo3D_h

#include <iprt/cdefs.h>
#include <iprt/asm.h>
#ifndef NemuTlsRefGetImpl
# ifdef NemuTlsRefSetImpl
#  error "NemuTlsRefSetImpl is defined, unexpected!"
# endif
# include <iprt/thread.h>
# define NemuTlsRefGetImpl(_tls) (RTTlsGet((RTTLS)(_tls)))
# define NemuTlsRefSetImpl(_tls, _val) (RTTlsSet((RTTLS)(_tls), (_val)))
#else
# ifndef NemuTlsRefSetImpl
#  error "NemuTlsRefSetImpl is NOT defined, unexpected!"
# endif
#endif

#ifndef NemuTlsRefAssertImpl
# define NemuTlsRefAssertImpl(_a) do {} while (0)
#endif

typedef DECLCALLBACK(void) FNNEMUTLSREFDTOR(void*);
typedef FNNEMUTLSREFDTOR *PFNNEMUTLSREFDTOR;

typedef enum {
    NEMUTLSREFDATA_STATE_UNDEFINED = 0,
    NEMUTLSREFDATA_STATE_INITIALIZED,
    NEMUTLSREFDATA_STATE_TOBE_DESTROYED,
    NEMUTLSREFDATA_STATE_DESTROYING,
    NEMUTLSREFDATA_STATE_32BIT_HACK = 0x7fffffff
} NEMUTLSREFDATA_STATE;

#define NEMUTLSREFDATA \
    volatile int32_t cTlsRefs; \
    NEMUTLSREFDATA_STATE enmTlsRefState; \
    PFNNEMUTLSREFDTOR pfnTlsRefDtor; \

struct NEMUTLSREFDATA_DUMMY
{
    NEMUTLSREFDATA
};

#define NEMUTLSREFDATA_OFFSET(_t) RT_OFFSETOF(_t, cTlsRefs)
#define NEMUTLSREFDATA_SIZE() (sizeof (struct NEMUTLSREFDATA_DUMMY))
#define NEMUTLSREFDATA_COPY(_pDst, _pSrc) do { \
        (_pDst)->cTlsRefs = (_pSrc)->cTlsRefs; \
        (_pDst)->enmTlsRefState = (_pSrc)->enmTlsRefState; \
        (_pDst)->pfnTlsRefDtor = (_pSrc)->pfnTlsRefDtor; \
    } while (0)

#define NEMUTLSREFDATA_EQUAL(_pDst, _pSrc) ( \
           (_pDst)->cTlsRefs == (_pSrc)->cTlsRefs \
        && (_pDst)->enmTlsRefState == (_pSrc)->enmTlsRefState \
        && (_pDst)->pfnTlsRefDtor == (_pSrc)->pfnTlsRefDtor \
    )


#define NemuTlsRefInit(_p, _pfnDtor) do { \
        (_p)->cTlsRefs = 1; \
        (_p)->enmTlsRefState = NEMUTLSREFDATA_STATE_INITIALIZED; \
        (_p)->pfnTlsRefDtor = (_pfnDtor); \
    } while (0)

#define NemuTlsRefIsFunctional(_p) (!!((_p)->enmTlsRefState == NEMUTLSREFDATA_STATE_INITIALIZED))

#define NemuTlsRefAddRef(_p) do { \
        int cRefs = ASMAtomicIncS32(&(_p)->cTlsRefs); \
        NemuTlsRefAssertImpl(cRefs > 1 || (_p)->enmTlsRefState == NEMUTLSREFDATA_STATE_DESTROYING); \
    } while (0)

#define NemuTlsRefCountGet(_p) (ASMAtomicReadS32(&(_p)->cTlsRefs))

#define NemuTlsRefRelease(_p) do { \
        int cRefs = ASMAtomicDecS32(&(_p)->cTlsRefs); \
        NemuTlsRefAssertImpl(cRefs >= 0); \
        if (!cRefs && (_p)->enmTlsRefState != NEMUTLSREFDATA_STATE_DESTROYING /* <- avoid recursion if NemuTlsRefAddRef/Release is called from dtor */) { \
            (_p)->enmTlsRefState = NEMUTLSREFDATA_STATE_DESTROYING; \
            (_p)->pfnTlsRefDtor((_p)); \
        } \
    } while (0)

#define NemuTlsRefMarkDestroy(_p) do { \
        (_p)->enmTlsRefState = NEMUTLSREFDATA_STATE_TOBE_DESTROYED; \
    } while (0)

#define NemuTlsRefGetCurrent(_t, _Tsd) ((_t*) NemuTlsRefGetImpl((_Tsd)))

#define NemuTlsRefGetCurrentFunctional(_val, _t, _Tsd) do { \
       _t * cur = NemuTlsRefGetCurrent(_t, _Tsd); \
       if (!cur || NemuTlsRefIsFunctional(cur)) { \
           (_val) = cur; \
       } else { \
           NemuTlsRefSetCurrent(_t, _Tsd, NULL); \
           (_val) = NULL; \
       } \
   } while (0)

#define NemuTlsRefSetCurrent(_t, _Tsd, _p) do { \
        _t * oldCur = NemuTlsRefGetCurrent(_t, _Tsd); \
        if (oldCur != (_p)) { \
            NemuTlsRefSetImpl((_Tsd), (_p)); \
            if (oldCur) { \
                NemuTlsRefRelease(oldCur); \
            } \
            if ((_p)) { \
                NemuTlsRefAddRef((_t*)(_p)); \
            } \
        } \
    } while (0)


/* host 3D->Fe[/Qt] notification mechanism defines */
#define NEMU3D_NOTIFY_EVENT_TYPE_TEST_FUNCTIONAL 3
#define NEMU3D_NOTIFY_EVENT_TYPE_3DDATA_VISIBLE  4
#define NEMU3D_NOTIFY_EVENT_TYPE_3DDATA_HIDDEN   5


#endif /* #ifndef ___Nemu_NemuVideo3D_h */
