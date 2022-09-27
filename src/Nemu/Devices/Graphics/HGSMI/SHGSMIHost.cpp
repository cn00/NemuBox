/* $Id: SHGSMIHost.cpp $ */
/*
 * Copyright (C) 2010-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#include "SHGSMIHost.h"
#include <Nemu/NemuVideo.h>

/*
 * NEMUSHGSMI made on top HGSMI and allows receiving notifications
 * about G->H command completion
 */
static bool nemuSHGSMICommandCanCompleteSynch (PNEMUSHGSMIHEADER pHdr)
{
    return !(pHdr->fFlags & NEMUSHGSMI_FLAG_GH_ASYNCH_FORCE);
}

static int nemuSHGSMICommandCompleteAsynch (PHGSMIINSTANCE pIns, PNEMUSHGSMIHEADER pHdr)
{
    bool bDoIrq = !!(pHdr->fFlags & NEMUSHGSMI_FLAG_GH_ASYNCH_IRQ)
            || !!(pHdr->fFlags & NEMUSHGSMI_FLAG_GH_ASYNCH_IRQ_FORCE);
    return HGSMICompleteGuestCommand(pIns, pHdr, bDoIrq);
}

void NemuSHGSMICommandMarkAsynchCompletion (void *pvData)
{
    PNEMUSHGSMIHEADER pHdr = NemuSHGSMIBufferHeader (pvData);
    Assert(!(pHdr->fFlags & NEMUSHGSMI_FLAG_HG_ASYNCH));
    pHdr->fFlags |= NEMUSHGSMI_FLAG_HG_ASYNCH;
}

int NemuSHGSMICommandComplete (PHGSMIINSTANCE pIns, void *pvData)
{
    PNEMUSHGSMIHEADER pHdr = NemuSHGSMIBufferHeader (pvData);
    if (!(pHdr->fFlags & NEMUSHGSMI_FLAG_HG_ASYNCH) /* <- check if synchronous completion */
            && nemuSHGSMICommandCanCompleteSynch(pHdr)) /* <- check if can complete synchronously */
        return VINF_SUCCESS;
    pHdr->fFlags |= NEMUSHGSMI_FLAG_HG_ASYNCH;
    return nemuSHGSMICommandCompleteAsynch(pIns, pHdr);
}
