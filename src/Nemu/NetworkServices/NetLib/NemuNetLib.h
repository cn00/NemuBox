/* $Id: NemuNetLib.h $ */
/** @file
 * NemuNetUDP - IntNet Client Library.
 */

/*
 * Copyright (C) 2009-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___NemuNetUDP_h___
#define ___NemuNetUDP_h___

#include <iprt/net.h>
#include <Nemu/intnet.h>

RT_C_DECLS_BEGIN


/**
 * Header pointers optionally returned by NemuNetUDPMatch.
 */
typedef struct NEMUNETUDPHDRS
{
    PCRTNETETHERHDR     pEth;           /**< Pointer to the ethernet header. */
    PCRTNETIPV4         pIpv4;          /**< Pointer to the IPV4 header if IPV4 packet. */
    PCRTNETUDP          pUdp;           /**< Pointer to the UDP header. */
} NEMUNETUDPHDRS;
/** Pointer to a NEMUNETUDPHDRS structure. */
typedef NEMUNETUDPHDRS *PNEMUNETUDPHDRS;


/** @name NemuNetUDPMatch flags.
 * @{ */
#define NEMUNETUDP_MATCH_UNICAST            RT_BIT_32(0)
#define NEMUNETUDP_MATCH_BROADCAST          RT_BIT_32(1)
#define NEMUNETUDP_MATCH_CHECKSUM           RT_BIT_32(2)
#define NEMUNETUDP_MATCH_REQUIRE_CHECKSUM   RT_BIT_32(3)
#define NEMUNETUDP_MATCH_PRINT_STDERR       RT_BIT_32(31)
/** @}  */

void *  NemuNetUDPMatch(PINTNETBUF pBuf, unsigned uDstPort, PCRTMAC pDstMac, uint32_t fFlags, PNEMUNETUDPHDRS pHdrs, size_t *pcb);
int     NemuNetUDPUnicast(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf,
                          RTNETADDRIPV4 SrcIPv4Addr, PCRTMAC SrcMacAddr, unsigned uSrcPort,
                          RTNETADDRIPV4 DstIPv4Addr, PCRTMAC DstMacAddr, unsigned uDstPort,
                          void const *pvData, size_t cbData);
int     NemuNetUDPBroadcast(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf,
                            RTNETADDRIPV4 SrcIPv4Addr, PCRTMAC SrcMacAddr, unsigned uSrcPort,
                            unsigned uDstPort,
                            void const *pvData, size_t cbData);

bool    NemuNetArpHandleIt(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf, PCRTMAC pMacAddr, RTNETADDRIPV4 IPv4Addr);

int     NemuNetIntIfFlush(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf);
int     NemuNetIntIfRingWriteFrame(PINTNETBUF pBuf, PINTNETRINGBUF pRingBuf, size_t cSegs, PCINTNETSEG paSegs);
int     NemuNetIntIfSend(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf, size_t cSegs, PCINTNETSEG paSegs, bool fFlush);


RT_C_DECLS_END

#endif

