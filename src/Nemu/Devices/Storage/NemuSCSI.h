/* $Id: NemuSCSI.h $ */
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

/** @page pg_drv_scsi   Simple SCSI interface for BIOS access.
 *
 * This is a simple interface to access SCSI devices from the BIOS which is
 * shared between the BusLogic and the LsiLogic SCSI host adapters to simplify
 * the BIOS part.
 *
 * The first interface (if available) will be starting at port 0x430 and
 * each will occupy 4 ports. The ports are used as described below:
 *
 * +--------+--------+----------+
 * | Offset | Access | Purpose  |
 * +--------+--------+----------+
 * |   0    |  Write | Command  |
 * +--------+--------+----------+
 * |   0    |  Read  | Status   |
 * +--------+--------+----------+
 * |   1    |  Write | Data in  |
 * +--------+--------+----------+
 * |   1    |  Read  | Data out |
 * +--------+--------+----------+
 * |   2    |  R/W   | Detect   |
 * +--------+--------+----------+
 * |   3    |  Read  | SCSI rc  |
 * +--------+--------+----------+
 * |   3    |  Write | Reset    |
 * +--------+--------+----------+
 *
 * The register at port 0 receives the SCSI CDB issued from the driver when
 * writing to it but before writing the actual CDB the first write gives the
 * size of the CDB in bytes.
 *
 * Reading the port at offset 0 gives status information about the adapter. If
 * the busy bit is set the adapter is processing a previous issued request if it is
 * cleared the command finished and the adapter can process another request.
 * The driver has to poll this bit because the adapter will not assert an IRQ
 * for simplicity reasons.
 *
 * The register at offset 2 is to detect if a host adapter is available. If the
 * driver writes a value to this port and gets the same value after reading it
 * again the adapter is available.
 *
 * Any write to the register at offset 3 causes the interface to be reset. A
 * read returns the SCSI status code of the last operation.
 *
 * This part has no R0 or RC components.
 */

#ifndef ___Storage_NemuSCSI_h
#define ___Storage_NemuSCSI_h

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
//#define DEBUG
#include <Nemu/vmm/pdmdev.h>
#include <Nemu/scsi.h>

typedef enum NEMUSCSISTATE
{
    NEMUSCSISTATE_NO_COMMAND            = 0x00,
    NEMUSCSISTATE_READ_TXDIR            = 0x01,
    NEMUSCSISTATE_READ_CDB_SIZE_BUFHI   = 0x02,
    NEMUSCSISTATE_READ_BUFFER_SIZE_LSB  = 0x03,
    NEMUSCSISTATE_READ_BUFFER_SIZE_MID  = 0x04,
    NEMUSCSISTATE_READ_COMMAND          = 0x05,
    NEMUSCSISTATE_COMMAND_READY         = 0x06
} NEMUSCSISTATE;

#define NEMUSCSI_TXDIR_FROM_DEVICE 0
#define NEMUSCSI_TXDIR_TO_DEVICE   1

/** Maximum CDB size the BIOS driver sends. */
#define NEMUSCSI_CDB_SIZE_MAX     16

typedef struct NEMUSCSI
{
    /** The identify register. */
    uint8_t              regIdentify;
    /** The target device. */
    uint8_t              uTargetDevice;
    /** Transfer direction. */
    uint8_t              uTxDir;
    /** The size of the CDB we are issuing. */
    uint8_t              cbCDB;
    /** The command to issue. */
    uint8_t              abCDB[NEMUSCSI_CDB_SIZE_MAX + 4];
    /** Current position in the array. */
    uint8_t              iCDB;

#if HC_ARCH_BITS == 64
    uint32_t             Alignment0;
#endif

    /** Pointer to the buffer holding the data. */
    R3PTRTYPE(uint8_t *) pbBuf;
    /** Size of the buffer in bytes. */
    uint32_t             cbBuf;
    /** The number of bytes left to read/write in the
     *  buffer.  It is decremented when the guest (BIOS) accesses
     *  the buffer data. */
    uint32_t             cbBufLeft;
    /** Current position in the buffer (offBuf if you like). */
    uint32_t             iBuf;
    /** The result code of last operation. */
    int32_t              rcCompletion;
    /** Flag whether a request is pending. */
    volatile bool        fBusy;
    /** The state we are in when fetching a command from the BIOS. */
    NEMUSCSISTATE        enmState;
} NEMUSCSI, *PNEMUSCSI;

#define NEMU_SCSI_BUSY  RT_BIT(0)
#define NEMU_SCSI_ERROR RT_BIT(1)

#ifdef IN_RING3
RT_C_DECLS_BEGIN
int nemuscsiInitialize(PNEMUSCSI pNemuSCSI);
int nemuscsiReadRegister(PNEMUSCSI pNemuSCSI, uint8_t iRegister, uint32_t *pu32Value);
int nemuscsiWriteRegister(PNEMUSCSI pNemuSCSI, uint8_t iRegister, uint8_t uVal);
int nemuscsiSetupRequest(PNEMUSCSI pNemuSCSI, PPDMSCSIREQUEST pScsiRequest, uint32_t *puTargetDevice);
int nemuscsiRequestFinished(PNEMUSCSI pNemuSCSI, PPDMSCSIREQUEST pScsiRequest, int rcCompletion);
void nemuscsiSetRequestRedo(PNEMUSCSI pNemuSCSI, PPDMSCSIREQUEST pScsiRequest);
int nemuscsiWriteString(PPDMDEVINS pDevIns, PNEMUSCSI pNemuSCSI, uint8_t iRegister,
                        uint8_t const *pbSrc, uint32_t *pcTransfers, unsigned cb);
int nemuscsiReadString(PPDMDEVINS pDevIns, PNEMUSCSI pNemuSCSI, uint8_t iRegister,
                       uint8_t *pbDst, uint32_t *pcTransfers, unsigned cb);

DECLHIDDEN(int) nemuscsiR3LoadExec(PNEMUSCSI pNemuSCSI, PSSMHANDLE pSSM);
DECLHIDDEN(int) nemuscsiR3SaveExec(PNEMUSCSI pNemuSCSI, PSSMHANDLE pSSM);
RT_C_DECLS_END
#endif /* IN_RING3 */

#endif /* !___Storage_NemuSCSI_h */

