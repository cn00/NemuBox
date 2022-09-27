/* $Id: UIConverterBackendCOM.cpp $ */
/** @file
 * Nemu Qt GUI - UIConverterBackend implementation.
 */

/*
 * Copyright (C) 2012-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef NEMU_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !NEMU_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
# include <QApplication>
# include <QHash>

/* GUI includes: */
# include "UIConverterBackend.h"
# include "UIIconPool.h"

/* COM includes: */
# include "COMEnums.h"

#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */


/* Determines if <Object of type X> can be converted to object of other type.
 * These functions returns 'true' for all allowed conversions. */
template<> bool canConvert<KMachineState>() { return true; }
template<> bool canConvert<KSessionState>() { return true; }
template<> bool canConvert<KParavirtProvider>() { return true; }
template<> bool canConvert<KDeviceType>() { return true; }
template<> bool canConvert<KClipboardMode>() { return true; }
template<> bool canConvert<KDnDMode>() { return true; }
template<> bool canConvert<KPointingHIDType>() { return true; }
template<> bool canConvert<KMediumType>() { return true; }
template<> bool canConvert<KMediumVariant>() { return true; }
template<> bool canConvert<KNetworkAttachmentType>() { return true; }
template<> bool canConvert<KNetworkAdapterType>() { return true; }
template<> bool canConvert<KNetworkAdapterPromiscModePolicy>() { return true; }
template<> bool canConvert<KPortMode>() { return true; }
template<> bool canConvert<KUSBControllerType>() { return true; }
template<> bool canConvert<KUSBDeviceState>() { return true; }
template<> bool canConvert<KUSBDeviceFilterAction>() { return true; }
template<> bool canConvert<KAudioDriverType>() { return true; }
template<> bool canConvert<KAudioControllerType>() { return true; }
template<> bool canConvert<KAuthType>() { return true; }
template<> bool canConvert<KStorageBus>() { return true; }
template<> bool canConvert<KStorageControllerType>() { return true; }
template<> bool canConvert<KChipsetType>() { return true; }
template<> bool canConvert<KNATProtocol>() { return true; }

/* QColor <= KMachineState: */
template<> QColor toColor(const KMachineState &state)
{
    switch (state)
    {
        case KMachineState_PoweredOff:             return QColor(Qt::gray);
        case KMachineState_Saved:                  return QColor(Qt::yellow);
        case KMachineState_Aborted:                return QColor(Qt::darkRed);
        case KMachineState_Teleported:             return QColor(Qt::red);
        case KMachineState_Running:                return QColor(Qt::green);
        case KMachineState_Paused:                 return QColor(Qt::darkGreen);
        case KMachineState_Stuck:                  return QColor(Qt::darkMagenta);
        case KMachineState_Teleporting:            return QColor(Qt::blue);
        case KMachineState_Snapshotting:           return QColor(Qt::green);
        case KMachineState_OnlineSnapshotting:     return QColor(Qt::green);
        case KMachineState_LiveSnapshotting:       return QColor(Qt::green);
        case KMachineState_Starting:               return QColor(Qt::green);
        case KMachineState_Stopping:               return QColor(Qt::green);
        case KMachineState_Saving:                 return QColor(Qt::green);
        case KMachineState_Restoring:              return QColor(Qt::green);
        case KMachineState_TeleportingPausedVM:    return QColor(Qt::blue);
        case KMachineState_TeleportingIn:          return QColor(Qt::blue);
        // case KMachineState_FaultTolerantSyncing:
        case KMachineState_DeletingSnapshotOnline: return QColor(Qt::green);
        case KMachineState_DeletingSnapshotPaused: return QColor(Qt::darkGreen);
        case KMachineState_RestoringSnapshot:      return QColor(Qt::green);
        case KMachineState_DeletingSnapshot:       return QColor(Qt::green);
        case KMachineState_SettingUp:              return QColor(Qt::green);
        // case KMachineState_FirstOnline:
        // case KMachineState_LastOnline:
        // case KMachineState_FirstTransient:
        // case KMachineState_LastTransient:
        default: AssertMsgFailed(("No color for %d", state)); break;
    }
    return QColor();
}

/* QIcon <= KMachineState: */
template<> QIcon toIcon(const KMachineState &state)
{
    switch (state)
    {
        case KMachineState_PoweredOff:             return UIIconPool::iconSet(":/state_powered_off_16px.png");
        case KMachineState_Saved:                  return UIIconPool::iconSet(":/state_saved_16px.png");
        case KMachineState_Aborted:                return UIIconPool::iconSet(":/state_aborted_16px.png");
        case KMachineState_Teleported:             return UIIconPool::iconSet(":/state_saved_16px.png");
        case KMachineState_Running:                return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Paused:                 return UIIconPool::iconSet(":/state_paused_16px.png");
        case KMachineState_Stuck:                  return UIIconPool::iconSet(":/state_stuck_16px.png");
        case KMachineState_Teleporting:            return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Snapshotting:           return UIIconPool::iconSet(":/state_saving_16px.png");
        case KMachineState_OnlineSnapshotting:     return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_LiveSnapshotting:       return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Starting:               return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Stopping:               return UIIconPool::iconSet(":/state_running_16px.png");
        case KMachineState_Saving:                 return UIIconPool::iconSet(":/state_saving_16px.png");
        case KMachineState_Restoring:              return UIIconPool::iconSet(":/state_restoring_16px.png");
        case KMachineState_TeleportingPausedVM:    return UIIconPool::iconSet(":/state_saving_16px.png");
        case KMachineState_TeleportingIn:          return UIIconPool::iconSet(":/state_restoring_16px.png");
        // case KMachineState_FaultTolerantSyncing:
        case KMachineState_DeletingSnapshotOnline: return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KMachineState_DeletingSnapshotPaused: return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KMachineState_RestoringSnapshot:      return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KMachineState_DeletingSnapshot:       return UIIconPool::iconSet(":/state_discarding_16px.png");
        case KMachineState_SettingUp:              return UIIconPool::iconSet(":/vm_settings_16px.png"); // TODO: Change icon!
        // case KMachineState_FirstOnline:
        // case KMachineState_LastOnline:
        // case KMachineState_FirstTransient:
        // case KMachineState_LastTransient:
        default: AssertMsgFailed(("No icon for %d", state)); break;
    }
    return QIcon();
}

/* QString <= KMachineState: */
template<> QString toString(const KMachineState &state)
{
    switch (state)
    {
        case KMachineState_PoweredOff:             return QApplication::translate("NemuGlobal", "Powered Off", "MachineState");
        case KMachineState_Saved:                  return QApplication::translate("NemuGlobal", "Saved", "MachineState");
        case KMachineState_Aborted:                return QApplication::translate("NemuGlobal", "Aborted", "MachineState");
        case KMachineState_Teleported:             return QApplication::translate("NemuGlobal", "Teleported", "MachineState");
        case KMachineState_Running:                return QApplication::translate("NemuGlobal", "Running", "MachineState");
        case KMachineState_Paused:                 return QApplication::translate("NemuGlobal", "Paused", "MachineState");
        case KMachineState_Stuck:                  return QApplication::translate("NemuGlobal", "Guru Meditation", "MachineState");
        case KMachineState_Teleporting:            return QApplication::translate("NemuGlobal", "Teleporting", "MachineState");
        case KMachineState_Snapshotting:           return QApplication::translate("NemuGlobal", "Taking Snapshot", "MachineState");
        case KMachineState_OnlineSnapshotting:     return QApplication::translate("NemuGlobal", "Taking Online Snapshot", "MachineState");
        case KMachineState_LiveSnapshotting:       return QApplication::translate("NemuGlobal", "Taking Live Snapshot", "MachineState");
        case KMachineState_Starting:               return QApplication::translate("NemuGlobal", "Starting", "MachineState");
        case KMachineState_Stopping:               return QApplication::translate("NemuGlobal", "Stopping", "MachineState");
        case KMachineState_Saving:                 return QApplication::translate("NemuGlobal", "Saving", "MachineState");
        case KMachineState_Restoring:              return QApplication::translate("NemuGlobal", "Restoring", "MachineState");
        case KMachineState_TeleportingPausedVM:    return QApplication::translate("NemuGlobal", "Teleporting Paused VM", "MachineState");
        case KMachineState_TeleportingIn:          return QApplication::translate("NemuGlobal", "Teleporting", "MachineState");
        case KMachineState_FaultTolerantSyncing:   return QApplication::translate("NemuGlobal", "Fault Tolerant Syncing", "MachineState");
        case KMachineState_DeletingSnapshotOnline: return QApplication::translate("NemuGlobal", "Deleting Snapshot", "MachineState");
        case KMachineState_DeletingSnapshotPaused: return QApplication::translate("NemuGlobal", "Deleting Snapshot", "MachineState");
        case KMachineState_RestoringSnapshot:      return QApplication::translate("NemuGlobal", "Restoring Snapshot", "MachineState");
        case KMachineState_DeletingSnapshot:       return QApplication::translate("NemuGlobal", "Deleting Snapshot", "MachineState");
        case KMachineState_SettingUp:              return QApplication::translate("NemuGlobal", "Setting Up", "MachineState");
        // case KMachineState_FirstOnline:
        // case KMachineState_LastOnline:
        // case KMachineState_FirstTransient:
        // case KMachineState_LastTransient:
        default: AssertMsgFailed(("No text for %d", state)); break;
    }
    return QString();
}

/* QString <= KSessionState: */
template<> QString toString(const KSessionState &state)
{
    switch (state)
    {
        case KSessionState_Unlocked:  return QApplication::translate("NemuGlobal", "Unlocked", "SessionState");
        case KSessionState_Locked:    return QApplication::translate("NemuGlobal", "Locked", "SessionState");
        case KSessionState_Spawning:  return QApplication::translate("NemuGlobal", "Spawning", "SessionState");
        case KSessionState_Unlocking: return QApplication::translate("NemuGlobal", "Unlocking", "SessionState");
        default: AssertMsgFailed(("No text for %d", state)); break;
    }
    return QString();
}

/* QString <= KParavirtProvider: */
template<> QString toString(const KParavirtProvider &type)
{
    switch (type)
    {
        case KParavirtProvider_None:    return QApplication::translate("NemuGlobal", "None", "ParavirtProvider");
        case KParavirtProvider_Default: return QApplication::translate("NemuGlobal", "Default", "ParavirtProvider");
        case KParavirtProvider_Legacy:  return QApplication::translate("NemuGlobal", "Legacy", "ParavirtProvider");
        case KParavirtProvider_Minimal: return QApplication::translate("NemuGlobal", "Minimal", "ParavirtProvider");
        case KParavirtProvider_HyperV:  return QApplication::translate("NemuGlobal", "Hyper-V", "ParavirtProvider");
        case KParavirtProvider_KVM:     return QApplication::translate("NemuGlobal", "KVM", "ParavirtProvider");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KDeviceType: */
template<> QString toString(const KDeviceType &type)
{
    switch (type)
    {
        case KDeviceType_Null:         return QApplication::translate("NemuGlobal", "None", "DeviceType");
        case KDeviceType_Floppy:       return QApplication::translate("NemuGlobal", "Floppy", "DeviceType");
        case KDeviceType_DVD:          return QApplication::translate("NemuGlobal", "Optical", "DeviceType");
        case KDeviceType_HardDisk:     return QApplication::translate("NemuGlobal", "Hard Disk", "DeviceType");
        case KDeviceType_Network:      return QApplication::translate("NemuGlobal", "Network", "DeviceType");
        case KDeviceType_USB:          return QApplication::translate("NemuGlobal", "USB", "DeviceType");
        case KDeviceType_SharedFolder: return QApplication::translate("NemuGlobal", "Shared Folder", "DeviceType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KClipboardMode: */
template<> QString toString(const KClipboardMode &mode)
{
    switch (mode)
    {
        case KClipboardMode_Disabled:      return QApplication::translate("NemuGlobal", "Disabled", "ClipboardType");
        case KClipboardMode_HostToGuest:   return QApplication::translate("NemuGlobal", "Host To Guest", "ClipboardType");
        case KClipboardMode_GuestToHost:   return QApplication::translate("NemuGlobal", "Guest To Host", "ClipboardType");
        case KClipboardMode_Bidirectional: return QApplication::translate("NemuGlobal", "Bidirectional", "ClipboardType");
        default: AssertMsgFailed(("No text for %d", mode)); break;
    }
    return QString();
}

/* QString <= KDnDMode: */
template<> QString toString(const KDnDMode &mode)
{
    switch (mode)
    {
        case KDnDMode_Disabled:      return QApplication::translate("NemuGlobal", "Disabled", "DragAndDropType");
        case KDnDMode_HostToGuest:   return QApplication::translate("NemuGlobal", "Host To Guest", "DragAndDropType");
        case KDnDMode_GuestToHost:   return QApplication::translate("NemuGlobal", "Guest To Host", "DragAndDropType");
        case KDnDMode_Bidirectional: return QApplication::translate("NemuGlobal", "Bidirectional", "DragAndDropType");
        default: AssertMsgFailed(("No text for %d", mode)); break;
    }
    return QString();
}

/* QString <= KPointingHIDType: */
template<> QString toString(const KPointingHIDType &type)
{
    switch (type)
    {
        case KPointingHIDType_PS2Mouse:      return QApplication::translate("NemuGlobal", "PS/2 Mouse", "PointingHIDType");
        case KPointingHIDType_USBMouse:      return QApplication::translate("NemuGlobal", "USB Mouse", "PointingHIDType");
        case KPointingHIDType_USBTablet:     return QApplication::translate("NemuGlobal", "USB Tablet", "PointingHIDType");
        case KPointingHIDType_ComboMouse:    return QApplication::translate("NemuGlobal", "PS/2 and USB Mouse", "PointingHIDType");
        case KPointingHIDType_USBMultiTouch: return QApplication::translate("NemuGlobal", "USB Multi-Touch Tablet", "PointingHIDType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KMediumType: */
template<> QString toString(const KMediumType &type)
{
    switch (type)
    {
        case KMediumType_Normal:       return QApplication::translate("NemuGlobal", "Normal", "MediumType");
        case KMediumType_Immutable:    return QApplication::translate("NemuGlobal", "Immutable", "MediumType");
        case KMediumType_Writethrough: return QApplication::translate("NemuGlobal", "Writethrough", "MediumType");
        case KMediumType_Shareable:    return QApplication::translate("NemuGlobal", "Shareable", "MediumType");
        case KMediumType_Readonly:     return QApplication::translate("NemuGlobal", "Readonly", "MediumType");
        case KMediumType_MultiAttach:  return QApplication::translate("NemuGlobal", "Multi-attach", "MediumType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KMediumVariant: */
template<> QString toString(const KMediumVariant &variant)
{
    /* Note: KMediumVariant_Diff and KMediumVariant_Fixed are so far mutually exclusive: */
    switch (variant)
    {
        case KMediumVariant_Standard:
            return QApplication::translate("NemuGlobal", "Dynamically allocated storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VdiZeroExpand):
            return QApplication::translate("NemuGlobal", "New dynamically allocated storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Diff):
            return QApplication::translate("NemuGlobal", "Dynamically allocated differencing storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Fixed):
            return QApplication::translate("NemuGlobal", "Fixed size storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VmdkSplit2G):
            return QApplication::translate("NemuGlobal", "Dynamically allocated storage split into files of less than 2GB", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VmdkSplit2G | KMediumVariant_Diff):
            return QApplication::translate("NemuGlobal", "Dynamically allocated differencing storage split into files of less than 2GB", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Fixed | KMediumVariant_VmdkSplit2G):
            return QApplication::translate("NemuGlobal", "Fixed size storage split into files of less than 2GB", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VmdkStreamOptimized):
            return QApplication::translate("NemuGlobal", "Dynamically allocated compressed storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_VmdkStreamOptimized | KMediumVariant_Diff):
            return QApplication::translate("NemuGlobal", "Dynamically allocated differencing compressed storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Fixed | KMediumVariant_VmdkESX):
            return QApplication::translate("NemuGlobal", "Fixed size ESX storage", "MediumVariant");
        case (KMediumVariant)(KMediumVariant_Standard | KMediumVariant_Fixed | KMediumVariant_VmdkRawDisk):
            return QApplication::translate("NemuGlobal", "Fixed size storage on raw disk", "MediumVariant");
        default:
            AssertMsgFailed(("No text for %d", variant)); break;
    }
    return QString();
}

/* QString <= KNetworkAttachmentType: */
template<> QString toString(const KNetworkAttachmentType &type)
{
    switch (type)
    {
        case KNetworkAttachmentType_Null:       return QApplication::translate("NemuGlobal", "Not attached", "NetworkAttachmentType");
        case KNetworkAttachmentType_NAT:        return QApplication::translate("NemuGlobal", "NAT", "NetworkAttachmentType");
        case KNetworkAttachmentType_Bridged:    return QApplication::translate("NemuGlobal", "Bridged Adapter", "NetworkAttachmentType");
        case KNetworkAttachmentType_Internal:   return QApplication::translate("NemuGlobal", "Internal Network", "NetworkAttachmentType");
        case KNetworkAttachmentType_HostOnly:   return QApplication::translate("NemuGlobal", "Host-only Adapter", "NetworkAttachmentType");
        case KNetworkAttachmentType_Generic:    return QApplication::translate("NemuGlobal", "Generic Driver", "NetworkAttachmentType");
        case KNetworkAttachmentType_NATNetwork: return QApplication::translate("NemuGlobal", "NAT Network", "NetworkAttachmentType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KNetworkAdapterType: */
template<> QString toString(const KNetworkAdapterType &type)
{
    switch (type)
    {
        case KNetworkAdapterType_Am79C970A: return QApplication::translate("NemuGlobal", "PCnet-PCI II (Am79C970A)", "NetworkAdapterType");
        case KNetworkAdapterType_Am79C973:  return QApplication::translate("NemuGlobal", "PCnet-FAST III (Am79C973)", "NetworkAdapterType");
        case KNetworkAdapterType_I82540EM:  return QApplication::translate("NemuGlobal", "Intel PRO/1000 MT Desktop (82540EM)", "NetworkAdapterType");
        case KNetworkAdapterType_I82543GC:  return QApplication::translate("NemuGlobal", "Intel PRO/1000 T Server (82543GC)", "NetworkAdapterType");
        case KNetworkAdapterType_I82545EM:  return QApplication::translate("NemuGlobal", "Intel PRO/1000 MT Server (82545EM)", "NetworkAdapterType");
#ifdef NEMU_WITH_VIRTIO
        case KNetworkAdapterType_Virtio:    return QApplication::translate("NemuGlobal", "Paravirtualized Network (virtio-net)", "NetworkAdapterType");
#endif /* NEMU_WITH_VIRTIO */
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KNetworkAdapterPromiscModePolicy: */
template<> QString toString(const KNetworkAdapterPromiscModePolicy &policy)
{
    switch (policy)
    {
        case KNetworkAdapterPromiscModePolicy_Deny:
            return QApplication::translate("NemuGlobal", "Deny", "NetworkAdapterPromiscModePolicy");
        case KNetworkAdapterPromiscModePolicy_AllowNetwork:
            return QApplication::translate("NemuGlobal", "Allow VMs", "NetworkAdapterPromiscModePolicy");
        case KNetworkAdapterPromiscModePolicy_AllowAll:
            return QApplication::translate("NemuGlobal", "Allow All", "NetworkAdapterPromiscModePolicy");
        default:
            AssertMsgFailed(("No text for %d", policy)); break;
    }
    return QString();
}

/* QString <= KPortMode: */
template<> QString toString(const KPortMode &mode)
{
    switch (mode)
    {
        case KPortMode_Disconnected: return QApplication::translate("NemuGlobal", "Disconnected", "PortMode");
        case KPortMode_HostPipe:     return QApplication::translate("NemuGlobal", "Host Pipe", "PortMode");
        case KPortMode_HostDevice:   return QApplication::translate("NemuGlobal", "Host Device", "PortMode");
        case KPortMode_RawFile:      return QApplication::translate("NemuGlobal", "Raw File", "PortMode");
        case KPortMode_TCP:          return QApplication::translate("NemuGlobal", "TCP", "PortMode");
        default: AssertMsgFailed(("No text for %d", mode)); break;
    }
    return QString();
}

/* QString <= KUSBControllerType: */
template<> QString toString(const KUSBControllerType &type)
{
    switch (type)
    {
        case KUSBControllerType_OHCI: return QApplication::translate("NemuGlobal", "OHCI", "USBControllerType");
        case KUSBControllerType_EHCI: return QApplication::translate("NemuGlobal", "EHCI", "USBControllerType");
        case KUSBControllerType_XHCI: return QApplication::translate("NemuGlobal", "xHCI", "USBControllerType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KUSBDeviceState: */
template<> QString toString(const KUSBDeviceState &state)
{
    switch (state)
    {
        case KUSBDeviceState_NotSupported: return QApplication::translate("NemuGlobal", "Not supported", "USBDeviceState");
        case KUSBDeviceState_Unavailable:  return QApplication::translate("NemuGlobal", "Unavailable", "USBDeviceState");
        case KUSBDeviceState_Busy:         return QApplication::translate("NemuGlobal", "Busy", "USBDeviceState");
        case KUSBDeviceState_Available:    return QApplication::translate("NemuGlobal", "Available", "USBDeviceState");
        case KUSBDeviceState_Held:         return QApplication::translate("NemuGlobal", "Held", "USBDeviceState");
        case KUSBDeviceState_Captured:     return QApplication::translate("NemuGlobal", "Captured", "USBDeviceState");
        default: AssertMsgFailed(("No text for %d", state)); break;
    }
    return QString();
}

/* QString <= KUSBDeviceFilterAction: */
template<> QString toString(const KUSBDeviceFilterAction &action)
{
    switch (action)
    {
        case KUSBDeviceFilterAction_Ignore: return QApplication::translate("NemuGlobal", "Ignore", "USBDeviceFilterAction");
        case KUSBDeviceFilterAction_Hold:   return QApplication::translate("NemuGlobal", "Hold", "USBDeviceFilterAction");
        default: AssertMsgFailed(("No text for %d", action)); break;
    }
    return QString();
}

/* QString <= KAudioDriverType: */
template<> QString toString(const KAudioDriverType &type)
{
    switch (type)
    {
        case KAudioDriverType_Null:        return QApplication::translate("NemuGlobal", "Null Audio Driver", "AudioDriverType");
        case KAudioDriverType_WinMM:       return QApplication::translate("NemuGlobal", "Windows Multimedia", "AudioDriverType");
        case KAudioDriverType_OSS:         return QApplication::translate("NemuGlobal", "OSS Audio Driver", "AudioDriverType");
        case KAudioDriverType_ALSA:        return QApplication::translate("NemuGlobal", "ALSA Audio Driver", "AudioDriverType");
        case KAudioDriverType_DirectSound: return QApplication::translate("NemuGlobal", "Windows DirectSound", "AudioDriverType");
        case KAudioDriverType_CoreAudio:   return QApplication::translate("NemuGlobal", "CoreAudio", "AudioDriverType");
        // case KAudioDriverType_MMPM:
        case KAudioDriverType_Pulse:       return QApplication::translate("NemuGlobal", "PulseAudio", "AudioDriverType");
        case KAudioDriverType_SolAudio:    return QApplication::translate("NemuGlobal", "Solaris Audio", "AudioDriverType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KAudioControllerType: */
template<> QString toString(const KAudioControllerType &type)
{
    switch (type)
    {
        case KAudioControllerType_AC97: return QApplication::translate("NemuGlobal", "ICH AC97", "AudioControllerType");
        case KAudioControllerType_SB16: return QApplication::translate("NemuGlobal", "SoundBlaster 16", "AudioControllerType");
        case KAudioControllerType_HDA:  return QApplication::translate("NemuGlobal", "Intel HD Audio", "AudioControllerType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KAuthType: */
template<> QString toString(const KAuthType &type)
{
    switch (type)
    {
        case KAuthType_Null:     return QApplication::translate("NemuGlobal", "Null", "AuthType");
        case KAuthType_External: return QApplication::translate("NemuGlobal", "External", "AuthType");
        case KAuthType_Guest:    return QApplication::translate("NemuGlobal", "Guest", "AuthType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KStorageBus: */
template<> QString toString(const KStorageBus &bus)
{
    switch (bus)
    {
        case KStorageBus_IDE:    return QApplication::translate("NemuGlobal", "IDE", "StorageBus");
        case KStorageBus_SATA:   return QApplication::translate("NemuGlobal", "SATA", "StorageBus");
        case KStorageBus_SCSI:   return QApplication::translate("NemuGlobal", "SCSI", "StorageBus");
        case KStorageBus_Floppy: return QApplication::translate("NemuGlobal", "Floppy", "StorageBus");
        case KStorageBus_SAS:    return QApplication::translate("NemuGlobal", "SAS", "StorageBus");
        case KStorageBus_USB:    return QApplication::translate("NemuGlobal", "USB", "StorageControllerType"); // TODO: change to proper context later
        default: AssertMsgFailed(("No text for %d", bus)); break;
    }
    return QString();
}

/* QString <= KStorageControllerType: */
template<> QString toString(const KStorageControllerType &type)
{
    switch (type)
    {
        case KStorageControllerType_LsiLogic:    return QApplication::translate("NemuGlobal", "Lsilogic", "StorageControllerType");
        case KStorageControllerType_BusLogic:    return QApplication::translate("NemuGlobal", "BusLogic", "StorageControllerType");
        case KStorageControllerType_IntelAhci:   return QApplication::translate("NemuGlobal", "AHCI", "StorageControllerType");
        case KStorageControllerType_PIIX3:       return QApplication::translate("NemuGlobal", "PIIX3", "StorageControllerType");
        case KStorageControllerType_PIIX4:       return QApplication::translate("NemuGlobal", "PIIX4", "StorageControllerType");
        case KStorageControllerType_ICH6:        return QApplication::translate("NemuGlobal", "ICH6", "StorageControllerType");
        case KStorageControllerType_I82078:      return QApplication::translate("NemuGlobal", "I82078", "StorageControllerType");
        case KStorageControllerType_LsiLogicSas: return QApplication::translate("NemuGlobal", "LsiLogic SAS", "StorageControllerType");
        case KStorageControllerType_USB:         return QApplication::translate("NemuGlobal", "USB", "StorageControllerType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KChipsetType: */
template<> QString toString(const KChipsetType &type)
{
    switch (type)
    {
        case KChipsetType_PIIX3: return QApplication::translate("NemuGlobal", "PIIX3", "ChipsetType");
        case KChipsetType_ICH9:  return QApplication::translate("NemuGlobal", "ICH9", "ChipsetType");
        default: AssertMsgFailed(("No text for %d", type)); break;
    }
    return QString();
}

/* QString <= KNATProtocol: */
template<> QString toString(const KNATProtocol &protocol)
{
    switch (protocol)
    {
        case KNATProtocol_UDP: return QApplication::translate("NemuGlobal", "UDP", "NATProtocol");
        case KNATProtocol_TCP: return QApplication::translate("NemuGlobal", "TCP", "NATProtocol");
        default: AssertMsgFailed(("No text for %d", protocol)); break;
    }
    return QString();
}

/* QString <= KNATProtocol: */
template<> QString toInternalString(const KNATProtocol &protocol)
{
    QString strResult;
    switch (protocol)
    {
        case KNATProtocol_UDP: strResult = "udp"; break;
        case KNATProtocol_TCP: strResult = "tcp"; break;
        default: AssertMsgFailed(("No text for protocol type=%d", protocol)); break;
    }
    return strResult;
}

/* KNATProtocol <= QString: */
template<> KNATProtocol fromInternalString<KNATProtocol>(const QString &strProtocol)
{
    /* Here we have some fancy stuff allowing us
     * to search through the keys using 'case-insensitive' rule: */
    QStringList keys; QList<KNATProtocol> values;
    keys << "udp";    values << KNATProtocol_UDP;
    keys << "tcp";    values << KNATProtocol_TCP;
    /* Invalid type for unknown words: */
    if (!keys.contains(strProtocol, Qt::CaseInsensitive))
    {
        AssertMsgFailed(("No value for '%s'", strProtocol.toAscii().constData()));
        return KNATProtocol_UDP;
    }
    /* Corresponding type for known words: */
    return values.at(keys.indexOf(QRegExp(strProtocol, Qt::CaseInsensitive)));
}

/* KPortMode <= QString: */
template<> KPortMode fromString<KPortMode>(const QString &strMode)
{
    QHash<QString, KPortMode> list;
    list.insert(QApplication::translate("NemuGlobal", "Disconnected", "PortMode"), KPortMode_Disconnected);
    list.insert(QApplication::translate("NemuGlobal", "Host Pipe", "PortMode"),    KPortMode_HostPipe);
    list.insert(QApplication::translate("NemuGlobal", "Host Device", "PortMode"),  KPortMode_HostDevice);
    list.insert(QApplication::translate("NemuGlobal", "Raw File", "PortMode"),     KPortMode_RawFile);
    list.insert(QApplication::translate("NemuGlobal", "TCP", "PortMode"),          KPortMode_TCP);
    if (!list.contains(strMode))
    {
        AssertMsgFailed(("No value for '%s'", strMode.toAscii().constData()));
    }
    return list.value(strMode, KPortMode_Disconnected);
}

/* KUSBDeviceFilterAction <= QString: */
template<> KUSBDeviceFilterAction fromString<KUSBDeviceFilterAction>(const QString &strAction)
{
    QHash<QString, KUSBDeviceFilterAction> list;
    list.insert(QApplication::translate("NemuGlobal", "Ignore", "USBDeviceFilterAction"), KUSBDeviceFilterAction_Ignore);
    list.insert(QApplication::translate("NemuGlobal", "Hold", "USBDeviceFilterAction"),   KUSBDeviceFilterAction_Hold);
    if (!list.contains(strAction))
    {
        AssertMsgFailed(("No value for '%s'", strAction.toAscii().constData()));
    }
    return list.value(strAction, KUSBDeviceFilterAction_Null);
}

/* KAudioDriverType <= QString: */
template<> KAudioDriverType fromString<KAudioDriverType>(const QString &strType)
{
    QHash<QString, KAudioDriverType> list;
    list.insert(QApplication::translate("NemuGlobal", "Null Audio Driver", "AudioDriverType"),   KAudioDriverType_Null);
    list.insert(QApplication::translate("NemuGlobal", "Windows Multimedia", "AudioDriverType"),  KAudioDriverType_WinMM);
    list.insert(QApplication::translate("NemuGlobal", "OSS Audio Driver", "AudioDriverType"),    KAudioDriverType_OSS);
    list.insert(QApplication::translate("NemuGlobal", "ALSA Audio Driver", "AudioDriverType"),   KAudioDriverType_ALSA);
    list.insert(QApplication::translate("NemuGlobal", "Windows DirectSound", "AudioDriverType"), KAudioDriverType_DirectSound);
    list.insert(QApplication::translate("NemuGlobal", "CoreAudio", "AudioDriverType"),           KAudioDriverType_CoreAudio);
    // list.insert(..., KAudioDriverType_MMPM);
    list.insert(QApplication::translate("NemuGlobal", "PulseAudio", "AudioDriverType"),          KAudioDriverType_Pulse);
    list.insert(QApplication::translate("NemuGlobal", "Solaris Audio", "AudioDriverType"),       KAudioDriverType_SolAudio);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toAscii().constData()));
    }
    return list.value(strType, KAudioDriverType_Null);
}

/* KAudioControllerType <= QString: */
template<> KAudioControllerType fromString<KAudioControllerType>(const QString &strType)
{
    QHash<QString, KAudioControllerType> list;
    list.insert(QApplication::translate("NemuGlobal", "ICH AC97", "AudioControllerType"),        KAudioControllerType_AC97);
    list.insert(QApplication::translate("NemuGlobal", "SoundBlaster 16", "AudioControllerType"), KAudioControllerType_SB16);
    list.insert(QApplication::translate("NemuGlobal", "Intel HD Audio", "AudioControllerType"),  KAudioControllerType_HDA);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toAscii().constData()));
    }
    return list.value(strType, KAudioControllerType_AC97);
}

/* KAuthType <= QString: */
template<> KAuthType fromString<KAuthType>(const QString &strType)
{
    QHash<QString, KAuthType> list;
    list.insert(QApplication::translate("NemuGlobal", "Null", "AuthType"),     KAuthType_Null);
    list.insert(QApplication::translate("NemuGlobal", "External", "AuthType"), KAuthType_External);
    list.insert(QApplication::translate("NemuGlobal", "Guest", "AuthType"),    KAuthType_Guest);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toAscii().constData()));
    }
    return list.value(strType, KAuthType_Null);
}

/* KStorageControllerType <= QString: */
template<> KStorageControllerType fromString<KStorageControllerType>(const QString &strType)
{
    QHash<QString, KStorageControllerType> list;
    list.insert(QApplication::translate("NemuGlobal", "Lsilogic", "StorageControllerType"),     KStorageControllerType_LsiLogic);
    list.insert(QApplication::translate("NemuGlobal", "BusLogic", "StorageControllerType"),     KStorageControllerType_BusLogic);
    list.insert(QApplication::translate("NemuGlobal", "AHCI", "StorageControllerType"),         KStorageControllerType_IntelAhci);
    list.insert(QApplication::translate("NemuGlobal", "PIIX3", "StorageControllerType"),        KStorageControllerType_PIIX3);
    list.insert(QApplication::translate("NemuGlobal", "PIIX4", "StorageControllerType"),        KStorageControllerType_PIIX4);
    list.insert(QApplication::translate("NemuGlobal", "ICH6", "StorageControllerType"),         KStorageControllerType_ICH6);
    list.insert(QApplication::translate("NemuGlobal", "I82078", "StorageControllerType"),       KStorageControllerType_I82078);
    list.insert(QApplication::translate("NemuGlobal", "LsiLogic SAS", "StorageControllerType"), KStorageControllerType_LsiLogicSas);
    list.insert(QApplication::translate("NemuGlobal", "USB", "StorageControllerType"),          KStorageControllerType_USB);
    if (!list.contains(strType))
    {
        AssertMsgFailed(("No value for '%s'", strType.toAscii().constData()));
    }
    return list.value(strType, KStorageControllerType_Null);
}

