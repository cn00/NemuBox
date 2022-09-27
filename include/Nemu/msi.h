/** @file
 * MSI - Message signalled interrupts support.
 */

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

#ifndef ___Nemu_msi_h
#define ___Nemu_msi_h

#include <Nemu/cdefs.h>
#include <Nemu/types.h>
#include <iprt/assert.h>

#include <Nemu/pci.h>

/* Constants for Intel APIC MSI messages */
#define NEMU_MSI_DATA_VECTOR_SHIFT           0
#define NEMU_MSI_DATA_VECTOR_MASK            0x000000ff
#define NEMU_MSI_DATA_VECTOR(v)              (((v) << NEMU_MSI_DATA_VECTOR_SHIFT) & \
                                                  NEMU_MSI_DATA_VECTOR_MASK)
#define NEMU_MSI_DATA_DELIVERY_MODE_SHIFT    8
#define  NEMU_MSI_DATA_DELIVERY_FIXED        (0 << NEMU_MSI_DATA_DELIVERY_MODE_SHIFT)
#define  NEMU_MSI_DATA_DELIVERY_LOWPRI       (1 << NEMU_MSI_DATA_DELIVERY_MODE_SHIFT)

#define NEMU_MSI_DATA_LEVEL_SHIFT            14
#define  NEMU_MSI_DATA_LEVEL_DEASSERT        (0 << NEMU_MSI_DATA_LEVEL_SHIFT)
#define  NEMU_MSI_DATA_LEVEL_ASSERT          (1 << NEMU_MSI_DATA_LEVEL_SHIFT)

#define NEMU_MSI_DATA_TRIGGER_SHIFT          15
#define  NEMU_MSI_DATA_TRIGGER_EDGE          (0 << NEMU_MSI_DATA_TRIGGER_SHIFT)
#define  NEMU_MSI_DATA_TRIGGER_LEVEL         (1 << NEMU_MSI_DATA_TRIGGER_SHIFT)

/**
 * MSI region, actually same as LAPIC MMIO region, but listens on bus,
 * not CPU, accesses.
 */
#define NEMU_MSI_ADDR_BASE                   0xfee00000
#define NEMU_MSI_ADDR_SIZE                   0x100000

#define NEMU_MSI_ADDR_DEST_MODE_SHIFT        2
#define  NEMU_MSI_ADDR_DEST_MODE_PHYSICAL    (0 << NEMU_MSI_ADDR_DEST_MODE_SHIFT)
#define  NEMU_MSI_ADDR_DEST_MODE_LOGICAL     (1 << NEMU_MSI_ADDR_DEST_MODE_SHIFT)

#define NEMU_MSI_ADDR_REDIRECTION_SHIFT      3
#define  NEMU_MSI_ADDR_REDIRECTION_CPU       (0 << NEMU_MSI_ADDR_REDIRECTION_SHIFT)
                                        /* dedicated cpu */
#define  NEMU_MSI_ADDR_REDIRECTION_LOWPRI    (1 << NEMU_MSI_ADDR_REDIRECTION_SHIFT)
                                        /* lowest priority */

#define NEMU_MSI_ADDR_DEST_ID_SHIFT          12
#define  NEMU_MSI_ADDR_DEST_ID_MASK          0x00ffff0
#define  NEMU_MSI_ADDR_DEST_ID(dest)         (((dest) << NEMU_MSI_ADDR_DEST_ID_SHIFT) & \
                                         NEMU_MSI_ADDR_DEST_ID_MASK)
#define NEMU_MSI_ADDR_EXT_DEST_ID(dest)      ((dest) & 0xffffff00)

#define NEMU_MSI_ADDR_IR_EXT_INT             (1 << 4)
#define NEMU_MSI_ADDR_IR_SHV                 (1 << 3)
#define NEMU_MSI_ADDR_IR_INDEX1(index)       ((index & 0x8000) >> 13)
#define NEMU_MSI_ADDR_IR_INDEX2(index)       ((index & 0x7fff) << 5)

/* Maximum number of vectors, per device/function */
#define NEMU_MSI_MAX_ENTRIES                  32

/* Offsets in MSI PCI capability structure (NEMU_PCI_CAP_ID_MSI) */
#define NEMU_MSI_CAP_MESSAGE_CONTROL          0x02
#define NEMU_MSI_CAP_MESSAGE_ADDRESS_32       0x04
#define NEMU_MSI_CAP_MESSAGE_ADDRESS_LO       0x04
#define NEMU_MSI_CAP_MESSAGE_ADDRESS_HI       0x08
#define NEMU_MSI_CAP_MESSAGE_DATA_32          0x08
#define NEMU_MSI_CAP_MESSAGE_DATA_64          0x0c
#define NEMU_MSI_CAP_MASK_BITS_32             0x0c
#define NEMU_MSI_CAP_PENDING_BITS_32          0x10
#define NEMU_MSI_CAP_MASK_BITS_64             0x10
#define NEMU_MSI_CAP_PENDING_BITS_64          0x14

/* We implement MSI with per-vector masking */
#define NEMU_MSI_CAP_SIZE_32                  0x14
#define NEMU_MSI_CAP_SIZE_64                  0x18

/**
 * MSI-X different from MSI by the fact that dedicated physical page
 * (in device memory) is assigned for MSI-X table, and Pending Bit Array (PBA),
 * which is recommended to be separated from the main table by at least 2K.
 */
/* Size of a MSI-X page */
#define NEMU_MSIX_PAGE_SIZE                   0x1000
/* Pending interrupts (PBA) */
#define NEMU_MSIX_PAGE_PENDING                (NEMU_MSIX_PAGE_SIZE / 2)
/* Maximum number of vectors, per device/function */
#define NEMU_MSIX_MAX_ENTRIES                 32
#define NEMU_MSIX_ENTRY_SIZE                  16
/* Size of MSI-X PCI capability */
#define NEMU_MSIX_CAP_SIZE                    12
/* Offsets in MSI-X PCI capability structure (NEMU_PCI_CAP_ID_MSIX) */
#define NEMU_MSIX_CAP_MESSAGE_CONTROL         0x02
#define NEMU_MSIX_TABLE_BIROFFSET             0x04
#define NEMU_MSIX_PBA_BIROFFSET               0x08
/* Size of single MSI-X table entry */
#define NEMU_MSIX_ENTRY_SIZE                  16


#endif
