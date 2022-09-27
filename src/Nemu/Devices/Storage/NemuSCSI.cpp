/* $Id: NemuSCSI.cpp $ */
/** @file
 * Nemu storage devices - Simple SCSI interface for BIOS access.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
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
//#define DEBUG
#define LOG_GROUP LOG_GROUP_DEV_BUSLOGIC /** @todo Create extra group. */

#if defined(IN_R0) || defined(IN_RC)
# error This device has no R0 or RC components
#endif

#include <Nemu/vmm/pdmdev.h>
#include <Nemu/vmm/pgm.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/string.h>

#include "NemuSCSI.h"


/**
 * Resets the state.
 */
static void nemuscsiReset(PNEMUSCSI pNemuSCSI, bool fEverything)
{
    if (fEverything)
    {
        pNemuSCSI->regIdentify = 0;
        pNemuSCSI->fBusy       = false;
    }
    pNemuSCSI->cbCDB         = 0;
    RT_ZERO(pNemuSCSI->abCDB);
    pNemuSCSI->iCDB          = 0;
    pNemuSCSI->rcCompletion  = 0;
    pNemuSCSI->uTargetDevice = 0;
    pNemuSCSI->cbBuf         = 0;
    pNemuSCSI->cbBufLeft     = 0;
    pNemuSCSI->iBuf          = 0;
    if (pNemuSCSI->pbBuf)
        RTMemFree(pNemuSCSI->pbBuf);
    pNemuSCSI->pbBuf         = NULL;
    pNemuSCSI->enmState      = NEMUSCSISTATE_NO_COMMAND;
}

/**
 * Initializes the state for the SCSI interface.
 *
 * @returns Nemu status code.
 * @param   pNemuSCSI    Pointer to the unitialized SCSI state.
 */
int nemuscsiInitialize(PNEMUSCSI pNemuSCSI)
{
    pNemuSCSI->pbBuf = NULL;
    nemuscsiReset(pNemuSCSI, true /*fEverything*/);

    return VINF_SUCCESS;
}

/**
 * Reads a register value.
 *
 * @returns Nemu status code.
 * @param   pNemuSCSI    Pointer to the SCSI state.
 * @param   iRegister    Index of the register to read.
 * @param   pu32Value    Where to store the content of the register.
 */
int nemuscsiReadRegister(PNEMUSCSI pNemuSCSI, uint8_t iRegister, uint32_t *pu32Value)
{
    uint8_t uVal = 0;

    switch (iRegister)
    {
        case 0:
        {
            if (ASMAtomicReadBool(&pNemuSCSI->fBusy) == true)
            {
                uVal |= NEMU_SCSI_BUSY;
                /* There is an I/O operation in progress.
                 * Yield the execution thread to let the I/O thread make progress.
                 */
                RTThreadYield();
            }
            if (pNemuSCSI->rcCompletion)
                uVal |= NEMU_SCSI_ERROR;
            break;
        }
        case 1:
        {
            /* If we're not in the 'command ready' state, there may not even be a buffer yet. */
            if (   pNemuSCSI->enmState == NEMUSCSISTATE_COMMAND_READY
                && pNemuSCSI->cbBufLeft > 0)
            {
                AssertMsg(pNemuSCSI->pbBuf, ("pBuf is NULL\n"));
                Assert(!pNemuSCSI->fBusy);
                uVal = pNemuSCSI->pbBuf[pNemuSCSI->iBuf];
                pNemuSCSI->iBuf++;
                pNemuSCSI->cbBufLeft--;

                /* When the guest reads the last byte from the data in buffer, clear
                   everything and reset command buffer. */
                if (pNemuSCSI->cbBufLeft == 0)
                    nemuscsiReset(pNemuSCSI, false /*fEverything*/);
            }
            break;
        }
        case 2:
        {
            uVal = pNemuSCSI->regIdentify;
            break;
        }
        case 3:
        {
            uVal = pNemuSCSI->rcCompletion;
            break;
        }
        default:
            AssertMsgFailed(("Invalid register to read from %u\n", iRegister));
    }

    *pu32Value = uVal;

    return VINF_SUCCESS;
}

/**
 * Writes to a register.
 *
 * @returns Nemu status code.
 * @retval  VERR_MORE_DATA if a command is ready to be sent to the SCSI driver.
 * @param   pNemuSCSI    Pointer to the SCSI state.
 * @param   iRegister    Index of the register to write to.
 * @param   uVal         Value to write.
 */
int nemuscsiWriteRegister(PNEMUSCSI pNemuSCSI, uint8_t iRegister, uint8_t uVal)
{
    int rc = VINF_SUCCESS;

    switch (iRegister)
    {
        case 0:
        {
            if (pNemuSCSI->enmState == NEMUSCSISTATE_NO_COMMAND)
            {
                pNemuSCSI->enmState = NEMUSCSISTATE_READ_TXDIR;
                pNemuSCSI->uTargetDevice = uVal;
            }
            else if (pNemuSCSI->enmState == NEMUSCSISTATE_READ_TXDIR)
            {
                if (uVal != NEMUSCSI_TXDIR_FROM_DEVICE && uVal != NEMUSCSI_TXDIR_TO_DEVICE)
                    nemuscsiReset(pNemuSCSI, true /*fEverything*/);
                else
                {
                    pNemuSCSI->enmState = NEMUSCSISTATE_READ_CDB_SIZE_BUFHI;
                    pNemuSCSI->uTxDir = uVal;
                }
            }
            else if (pNemuSCSI->enmState == NEMUSCSISTATE_READ_CDB_SIZE_BUFHI)
            {
                uint8_t cbCDB = uVal & 0x0F;

                if (cbCDB == 0)
                    cbCDB = 16;
                if (cbCDB > NEMUSCSI_CDB_SIZE_MAX)
                    nemuscsiReset(pNemuSCSI, true /*fEverything*/);
                else
                {
                    pNemuSCSI->enmState = NEMUSCSISTATE_READ_BUFFER_SIZE_LSB;
                    pNemuSCSI->cbCDB = cbCDB;
                    pNemuSCSI->cbBuf = (uVal & 0xF0) << 12;     /* Bits 16-19 of buffer size. */
                }
            }
            else if (pNemuSCSI->enmState == NEMUSCSISTATE_READ_BUFFER_SIZE_LSB)
            {
                pNemuSCSI->enmState = NEMUSCSISTATE_READ_BUFFER_SIZE_MID;
                pNemuSCSI->cbBuf |= uVal;                       /* Bits 0-7 of buffer size. */
            }
            else if (pNemuSCSI->enmState == NEMUSCSISTATE_READ_BUFFER_SIZE_MID)
            {
                pNemuSCSI->enmState = NEMUSCSISTATE_READ_COMMAND;
                pNemuSCSI->cbBuf |= (((uint16_t)uVal) << 8);    /* Bits 8-15 of buffer size. */
            }
            else if (pNemuSCSI->enmState == NEMUSCSISTATE_READ_COMMAND)
            {
                pNemuSCSI->abCDB[pNemuSCSI->iCDB] = uVal;
                pNemuSCSI->iCDB++;

                /* Check if we have all necessary command data. */
                if (pNemuSCSI->iCDB == pNemuSCSI->cbCDB)
                {
                    Log(("%s: Command ready for processing\n", __FUNCTION__));
                    pNemuSCSI->enmState = NEMUSCSISTATE_COMMAND_READY;
                    pNemuSCSI->cbBufLeft = pNemuSCSI->cbBuf;
                    if (pNemuSCSI->uTxDir == NEMUSCSI_TXDIR_TO_DEVICE)
                    {
                        /* This is a write allocate buffer. */
                        pNemuSCSI->pbBuf = (uint8_t *)RTMemAllocZ(pNemuSCSI->cbBuf);
                        if (!pNemuSCSI->pbBuf)
                            return VERR_NO_MEMORY;
                    }
                    else
                    {
                        /* This is a read from the device. */
                        ASMAtomicXchgBool(&pNemuSCSI->fBusy, true);
                        rc = VERR_MORE_DATA; /** @todo Better return value to indicate ready command? */
                    }
                }
            }
            else
                AssertMsgFailed(("Invalid state %d\n", pNemuSCSI->enmState));
            break;
        }

        case 1:
        {
            if (   pNemuSCSI->enmState != NEMUSCSISTATE_COMMAND_READY
                || pNemuSCSI->uTxDir != NEMUSCSI_TXDIR_TO_DEVICE)
            {
                /* Reset the state */
                nemuscsiReset(pNemuSCSI, true /*fEverything*/);
            }
            else if (pNemuSCSI->cbBufLeft > 0)
            {
                pNemuSCSI->pbBuf[pNemuSCSI->iBuf++] = uVal;
                pNemuSCSI->cbBufLeft--;
                if (pNemuSCSI->cbBufLeft == 0)
                {
                    rc = VERR_MORE_DATA;
                    ASMAtomicXchgBool(&pNemuSCSI->fBusy, true);
                }
            }
            /* else: Ignore extra data, request pending or something. */
            break;
        }

        case 2:
        {
            pNemuSCSI->regIdentify = uVal;
            break;
        }

        case 3:
        {
            /* Reset */
            nemuscsiReset(pNemuSCSI, true /*fEverything*/);
            break;
        }

        default:
            AssertMsgFailed(("Invalid register to write to %u\n", iRegister));
    }

    return rc;
}

/**
 * Sets up a SCSI request which the owning SCSI device can process.
 *
 * @returns Nemu status code.
 * @param   pNemuSCSI      Pointer to the SCSI state.
 * @param   pScsiRequest   Pointer to a scsi request to setup.
 * @param   puTargetDevice Where to store the target device ID.
 */
int nemuscsiSetupRequest(PNEMUSCSI pNemuSCSI, PPDMSCSIREQUEST pScsiRequest, uint32_t *puTargetDevice)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pNemuSCSI=%#p pScsiRequest=%#p puTargetDevice=%#p\n", pNemuSCSI, pScsiRequest, puTargetDevice));

    AssertMsg(pNemuSCSI->enmState == NEMUSCSISTATE_COMMAND_READY, ("Invalid state %u\n", pNemuSCSI->enmState));

    /* Clear any errors from a previous request. */
    pNemuSCSI->rcCompletion = 0;

    if (pNemuSCSI->uTxDir == NEMUSCSI_TXDIR_FROM_DEVICE)
    {
        if (pNemuSCSI->pbBuf)
            RTMemFree(pNemuSCSI->pbBuf);

        pNemuSCSI->pbBuf = (uint8_t *)RTMemAllocZ(pNemuSCSI->cbBuf);
        if (!pNemuSCSI->pbBuf)
            return VERR_NO_MEMORY;
    }

    /* Allocate scatter gather element. */
    pScsiRequest->paScatterGatherHead = (PRTSGSEG)RTMemAllocZ(sizeof(RTSGSEG) * 1); /* Only one element. */
    if (!pScsiRequest->paScatterGatherHead)
    {
        RTMemFree(pNemuSCSI->pbBuf);
        pNemuSCSI->pbBuf = NULL;
        return VERR_NO_MEMORY;
    }

    /* Allocate sense buffer. */
    pScsiRequest->cbSenseBuffer = 18;
    pScsiRequest->pbSenseBuffer = (uint8_t *)RTMemAllocZ(pScsiRequest->cbSenseBuffer);

    pScsiRequest->cbCDB = pNemuSCSI->cbCDB;
    pScsiRequest->pbCDB = pNemuSCSI->abCDB;
    pScsiRequest->uLogicalUnit = 0;
    pScsiRequest->cbScatterGather = pNemuSCSI->cbBuf;
    pScsiRequest->cScatterGatherEntries = 1;

    pScsiRequest->paScatterGatherHead[0].cbSeg = pNemuSCSI->cbBuf;
    pScsiRequest->paScatterGatherHead[0].pvSeg = pNemuSCSI->pbBuf;

    *puTargetDevice = pNemuSCSI->uTargetDevice;

    return rc;
}

/**
 * Notifies the device that a request finished and the incoming data
 * is ready at the incoming data port.
 */
int nemuscsiRequestFinished(PNEMUSCSI pNemuSCSI, PPDMSCSIREQUEST pScsiRequest, int rcCompletion)
{
    LogFlowFunc(("pNemuSCSI=%#p pScsiRequest=%#p\n", pNemuSCSI, pScsiRequest));
    RTMemFree(pScsiRequest->paScatterGatherHead);
    RTMemFree(pScsiRequest->pbSenseBuffer);

    if (pNemuSCSI->uTxDir == NEMUSCSI_TXDIR_TO_DEVICE)
        nemuscsiReset(pNemuSCSI, false /*fEverything*/);

    pNemuSCSI->rcCompletion = rcCompletion;

    ASMAtomicXchgBool(&pNemuSCSI->fBusy, false);

    return VINF_SUCCESS;
}

int nemuscsiReadString(PPDMDEVINS pDevIns, PNEMUSCSI pNemuSCSI, uint8_t iRegister,
                       uint8_t *pbDst, uint32_t *pcTransfers, unsigned cb)
{
    LogFlowFunc(("pDevIns=%#p pNemuSCSI=%#p iRegister=%d cTransfers=%u cb=%u\n",
                 pDevIns, pNemuSCSI, iRegister, *pcTransfers, cb));

    /*
     * Check preconditions, fall back to non-string I/O handler.
     */
    Assert(*pcTransfers > 0);

    /* Read string only valid for data in register. */
    AssertMsgReturn(iRegister == 1, ("Hey! Only register 1 can be read from with string!\n"), VINF_SUCCESS);

    /* Accesses without a valid buffer will be ignored. */
    AssertReturn(pNemuSCSI->pbBuf, VINF_SUCCESS);

    /* Check state. */
    AssertReturn(pNemuSCSI->enmState == NEMUSCSISTATE_COMMAND_READY, VINF_SUCCESS);
    Assert(!pNemuSCSI->fBusy);

    /*
     * Also ignore attempts to read more data than is available.
     */
    int rc = VINF_SUCCESS;
    uint32_t cbTransfer = *pcTransfers * cb;
    if (pNemuSCSI->cbBufLeft > 0)
    {
        Assert(cbTransfer <= pNemuSCSI->cbBuf);
        if (cbTransfer > pNemuSCSI->cbBuf)
        {
            memset(pbDst + pNemuSCSI->cbBuf, 0xff, cbTransfer - pNemuSCSI->cbBuf);
            cbTransfer = pNemuSCSI->cbBuf;  /* Ignore excess data (not supposed to happen). */
        }

        /* Copy the data and adance the buffer position. */
        memcpy(pbDst, pNemuSCSI->pbBuf + pNemuSCSI->iBuf, cbTransfer);

        /* Advance current buffer position. */
        pNemuSCSI->iBuf      += cbTransfer;
        pNemuSCSI->cbBufLeft -= cbTransfer;

        /* When the guest reads the last byte from the data in buffer, clear
           everything and reset command buffer. */
        if (pNemuSCSI->cbBufLeft == 0)
            nemuscsiReset(pNemuSCSI, false /*fEverything*/);
    }
    else
    {
        AssertFailed();
        memset(pbDst, 0, cbTransfer);
    }
    *pcTransfers = 0;

    return rc;
}

int nemuscsiWriteString(PPDMDEVINS pDevIns, PNEMUSCSI pNemuSCSI, uint8_t iRegister,
                        uint8_t const *pbSrc, uint32_t *pcTransfers, unsigned cb)
{
    /*
     * Check preconditions, fall back to non-string I/O handler.
     */
    Assert(*pcTransfers > 0);
    /* Write string only valid for data in/out register. */
    AssertMsgReturn(iRegister == 1, ("Hey! Only register 1 can be written to with string!\n"), VINF_SUCCESS);

    /* Accesses without a valid buffer will be ignored. */
    AssertReturn(pNemuSCSI->pbBuf, VINF_SUCCESS);

    /* State machine assumptions. */
    AssertReturn(pNemuSCSI->enmState == NEMUSCSISTATE_COMMAND_READY, VINF_SUCCESS);
    AssertReturn(pNemuSCSI->uTxDir == NEMUSCSI_TXDIR_TO_DEVICE, VINF_SUCCESS);

    /*
     * Ignore excess data (not supposed to happen).
     */
    int rc = VINF_SUCCESS;
    if (pNemuSCSI->cbBufLeft > 0)
    {
        uint32_t cbTransfer = RT_MIN(*pcTransfers * cb, pNemuSCSI->cbBufLeft);

        /* Copy the data and adance the buffer position. */
        memcpy(pNemuSCSI->pbBuf + pNemuSCSI->iBuf, pbSrc, cbTransfer);
        pNemuSCSI->iBuf      += cbTransfer;
        pNemuSCSI->cbBufLeft -= cbTransfer;

        /* If we've reached the end, tell the caller to submit the command. */
        if (pNemuSCSI->cbBufLeft == 0)
        {
            ASMAtomicXchgBool(&pNemuSCSI->fBusy, true);
            rc = VERR_MORE_DATA;
        }
    }
    else
        AssertFailed();
    *pcTransfers = 0;

    return rc;
}

void nemuscsiSetRequestRedo(PNEMUSCSI pNemuSCSI, PPDMSCSIREQUEST pScsiRequest)
{
    AssertMsg(pNemuSCSI->fBusy, ("No request to redo\n"));

    RTMemFree(pScsiRequest->paScatterGatherHead);
    RTMemFree(pScsiRequest->pbSenseBuffer);

    if (pNemuSCSI->uTxDir == NEMUSCSI_TXDIR_FROM_DEVICE)
    {
        AssertPtr(pNemuSCSI->pbBuf);
    }
}

DECLHIDDEN(int) nemuscsiR3LoadExec(PNEMUSCSI pNemuSCSI, PSSMHANDLE pSSM)
{
    SSMR3GetU8  (pSSM, &pNemuSCSI->regIdentify);
    SSMR3GetU8  (pSSM, &pNemuSCSI->uTargetDevice);
    SSMR3GetU8  (pSSM, &pNemuSCSI->uTxDir);
    SSMR3GetU8  (pSSM, &pNemuSCSI->cbCDB);
    SSMR3GetMem (pSSM, &pNemuSCSI->abCDB[0], sizeof(pNemuSCSI->abCDB));
    SSMR3GetU8  (pSSM, &pNemuSCSI->iCDB);
    SSMR3GetU32 (pSSM, &pNemuSCSI->cbBufLeft);
    SSMR3GetU32 (pSSM, &pNemuSCSI->iBuf);
    SSMR3GetBool(pSSM, (bool *)&pNemuSCSI->fBusy);
    SSMR3GetU8  (pSSM, (uint8_t *)&pNemuSCSI->enmState);

    /*
     * Old saved states only save the size of the buffer left to read/write.
     * To avoid changing the saved state version we can just calculate the original
     * buffer size from the offset and remaining size.
     */
    pNemuSCSI->cbBuf = pNemuSCSI->cbBufLeft + pNemuSCSI->iBuf;

    if (pNemuSCSI->cbBuf)
    {
        pNemuSCSI->pbBuf = (uint8_t *)RTMemAllocZ(pNemuSCSI->cbBuf);
        if (!pNemuSCSI->pbBuf)
            return VERR_NO_MEMORY;

        SSMR3GetMem(pSSM, pNemuSCSI->pbBuf, pNemuSCSI->cbBuf);
    }

    return VINF_SUCCESS;
}

DECLHIDDEN(int) nemuscsiR3SaveExec(PNEMUSCSI pNemuSCSI, PSSMHANDLE pSSM)
{
    SSMR3PutU8    (pSSM, pNemuSCSI->regIdentify);
    SSMR3PutU8    (pSSM, pNemuSCSI->uTargetDevice);
    SSMR3PutU8    (pSSM, pNemuSCSI->uTxDir);
    SSMR3PutU8    (pSSM, pNemuSCSI->cbCDB);
    SSMR3PutMem   (pSSM, pNemuSCSI->abCDB, sizeof(pNemuSCSI->abCDB));
    SSMR3PutU8    (pSSM, pNemuSCSI->iCDB);
    SSMR3PutU32   (pSSM, pNemuSCSI->cbBufLeft);
    SSMR3PutU32   (pSSM, pNemuSCSI->iBuf);
    SSMR3PutBool  (pSSM, pNemuSCSI->fBusy);
    SSMR3PutU8    (pSSM, pNemuSCSI->enmState);

    if (pNemuSCSI->cbBuf)
        SSMR3PutMem(pSSM, pNemuSCSI->pbBuf, pNemuSCSI->cbBuf);

    return VINF_SUCCESS;
}
