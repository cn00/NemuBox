/* $Id: nemusharedrc.h $ */
/** @file
 *
 * Nemu extension to Wine D3D - shared resource
 *
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___nemusharedrc_h___
#define ___nemusharedrc_h___

#define NEMUSHRC_F_SHARED              0x00000001 /* shared rc */
#define NEMUSHRC_F_SHARED_OPENED       0x00000002 /* if set shared rc is opened, otherwise it is created */

#define NEMUSHRC_GET_SHAREFLAFS(_o) ((_o)->resource.sharerc_flags)
#define NEMUSHRC_GET_SHAREHANDLE(_o) ((HANDLE)(_o)->resource.sharerc_handle)
#define NEMUSHRC_SET_SHAREHANDLE(_o, _h) ((_o)->resource.sharerc_handle = (DWORD)(_h))
#define NEMUSHRC_COPY_SHAREDATA(_oDst, _oSrc) do { \
        NEMUSHRC_GET_SHAREFLAFS(_oDst) = NEMUSHRC_GET_SHAREFLAFS(_oSrc);   \
        NEMUSHRC_SET_SHAREHANDLE(_oDst, NEMUSHRC_GET_SHAREHANDLE(_oSrc)); \
    } while (0)
#define NEMUSHRC_SET_SHARED(_o) (NEMUSHRC_GET_SHAREFLAFS(_o) |= NEMUSHRC_F_SHARED)
#define NEMUSHRC_SET_SHARED_OPENED(_o) (NEMUSHRC_GET_SHAREFLAFS(_o) |= NEMUSHRC_F_SHARED_OPENED)

#define NEMUSHRC_IS_SHARED(_o) (!!(NEMUSHRC_GET_SHAREFLAFS(_o) & NEMUSHRC_F_SHARED))
#define NEMUSHRC_IS_SHARED_OPENED(_o) (!!(NEMUSHRC_GET_SHAREFLAFS(_o) & NEMUSHRC_F_SHARED_OPENED))
#define NEMUSHRC_IS_SHARED_UNLOCKED(_o) (NEMUSHRC_IS_SHARED(_o) && !NEMUSHRC_IS_LOCKED(_o))

#define NEMUSHRC_LOCK(_o) do{ \
        Assert(NEMUSHRC_IS_SHARED(_o)); \
        ++(_o)->resource.sharerc_locks; \
    } while (0)
#define NEMUSHRC_UNLOCK(_o) do{ \
        Assert(NEMUSHRC_IS_SHARED(_o)); \
        --(_o)->resource.sharerc_locks; \
        Assert((_o)->resource.sharerc_locks < UINT32_MAX/2); \
    } while (0)
#define NEMUSHRC_IS_LOCKED(_o) ( \
        !!((_o)->resource.sharerc_locks) \
        )

#endif /* #ifndef ___nemusharedrc_h___ */
