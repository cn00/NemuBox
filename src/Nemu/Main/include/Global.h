/* $Id: Global.h $ */
/** @file
 * VirtualBox COM API - Global Declarations and Definitions.
 */

/*
 * Copyright (C) 2008-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_GLOBAL
#define ____H_GLOBAL

/* interface definitions */
#include "Nemu/com/VirtualBox.h"

#include <Nemu/ostypes.h>

#include <iprt/types.h>

#define NEMUOSHINT_NONE                 0
#define NEMUOSHINT_64BIT                RT_BIT(0)
#define NEMUOSHINT_HWVIRTEX             RT_BIT(1)
#define NEMUOSHINT_IOAPIC               RT_BIT(2)
#define NEMUOSHINT_EFI                  RT_BIT(3)
#define NEMUOSHINT_PAE                  RT_BIT(4)
#define NEMUOSHINT_USBHID               RT_BIT(5)
#define NEMUOSHINT_HPET                 RT_BIT(6)
#define NEMUOSHINT_USBTABLET            RT_BIT(7)
#define NEMUOSHINT_RTCUTC               RT_BIT(8)
#define NEMUOSHINT_ACCEL2D              RT_BIT(9)
#define NEMUOSHINT_ACCEL3D              RT_BIT(10)
#define NEMUOSHINT_FLOPPY               RT_BIT(11)
#define NEMUOSHINT_NOUSB                RT_BIT(12)
#define NEMUOSHINT_TFRESET              RT_BIT(13)

/** The NemuVRDP kludge extension pack name.
 *
 * This is not a valid extension pack name (dashes are not allowed), and
 * hence will not conflict with real extension packs.
 */
#define NEMUVRDP_KLUDGE_EXTPACK_NAME    "Built-in-NemuVRDP"

/**
 * Contains global static definitions that can be referenced by all COM classes
 * regardless of the apartment.
 */
class Global
{
public:

    /** Represents OS Type <-> string mappings. */
    struct OSType
    {
        const char                    *familyId;          /* utf-8 */
        const char                    *familyDescription; /* utf-8 */
        const char                    *id;          /* utf-8, VM config file value */
        const char                    *description; /* utf-8 */
        const NEMUOSTYPE               osType;
        const uint32_t                 osHint;
        const uint32_t                 recommendedRAM;
        const uint32_t                 recommendedVRAM;
        const uint64_t                 recommendedHDD;
        const NetworkAdapterType_T     networkAdapterType;
        const uint32_t                 numSerialEnabled;
        const StorageControllerType_T  dvdStorageControllerType;
        const StorageBus_T             dvdStorageBusType;
        const StorageControllerType_T  hdStorageControllerType;
        const StorageBus_T             hdStorageBusType;
        const ChipsetType_T            chipsetType;
        const AudioControllerType_T    audioControllerType;
        const AudioCodecType_T         audioCodecType;
    };

    static const OSType sOSTypes[];
    static uint32_t cOSTypes;

    /**
     * Maps NEMUOSTYPE to the OS type which is used in VM configs.
     */
    static const char *OSTypeId(NEMUOSTYPE aOSType);

    /**
     * Get the network adapter limit for each chipset type.
     */
    static uint32_t getMaxNetworkAdapters(ChipsetType_T aChipsetType);

    /**
     * Returns @c true if the given machine state is an online state. This is a
     * recommended way to detect if the VM is online (being executed in a
     * dedicated process) or not. Note that some online states are also
     * transitional states (see #IsTransitional()).
     */
    static bool IsOnline(MachineState_T aState)
    {
        return aState >= MachineState_FirstOnline &&
               aState <= MachineState_LastOnline;
    }

    /**
     * Returns @c true if the given machine state is a transient state. This is
     * a recommended way to detect if the VM is performing some potentially
     * lengthy operation (such as starting, stopping, saving, deleting
     * snapshot, etc.). Note some (but not all) transitional states are also
     * online states (see #IsOnline()).
     */
    static bool IsTransient(MachineState_T aState)
    {
        return aState >= MachineState_FirstTransient &&
               aState <= MachineState_LastTransient;
    }

    /**
     * Shortcut to <tt>IsOnline(aState) || IsTransient(aState)</tt>. When it returns
     * @false, the VM is turned off (no VM process) and not busy with
     * another exclusive operation.
     */
    static bool IsOnlineOrTransient(MachineState_T aState)
    {
        return IsOnline(aState) || IsTransient(aState);
    }

    /**
     * Stringify a machine state.
     *
     * @returns Pointer to a read only string.
     * @param   aState      Valid machine state.
     */
    static const char *stringifyMachineState(MachineState_T aState);

    /**
     * Stringify a session state.
     *
     * @returns Pointer to a read only string.
     * @param   aState      Valid session state.
     */
    static const char *stringifySessionState(SessionState_T aState);

    /**
     * Stringify a device type.
     *
     * @returns Pointer to a read only string.
     * @param   aType       The device type.
     */
    static const char *stringifyDeviceType(DeviceType_T aType);

    /**
     * Stringify a reason.
     *
     * @returns Pointer to a read only string.
     * @param   aReason     The reason code.
     */
    static const char *stringifyReason(Reason_T aReason);

    /**
     * Try convert a COM status code to a VirtualBox status code (Nemu/err.h).
     *
     * @returns Nemu status code.
     * @param   aComStatus      COM status code.
     */
    static int nemuStatusCodeFromCOM(HRESULT aComStatus);

    /**
     * Try convert a VirtualBox status code (Nemu/err.h) to a COM status code.
     *
     * This is mainly intended for dealing with nemuStatusCodeFromCOM() return
     * values.  If used on anything else, it won't be able to cope with most of the
     * input!
     *
     * @returns COM status code.
     * @param   aNemuStatus      Nemu status code.
     */
    static HRESULT nemuStatusCodeToCOM(int aNemuStatus);
};

#endif /* !____H_GLOBAL */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
