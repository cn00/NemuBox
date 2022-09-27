/* $Id: UIMainEventListener.cpp $ */
/** @file
 * Nemu Qt GUI - UIMainEventListener class implementation.
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
 */

#ifdef NEMU_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !NEMU_WITH_PRECOMPILED_HEADERS */

/* GUI includes: */
# include "UIMainEventListener.h"
# include "NemuGlobal.h"

/* COM includes: */
# include "COMEnums.h"
# include "CEvent.h"
# include "CNemuSVCAvailabilityChangedEvent.h"
# include "CVirtualBoxErrorInfo.h"
# include "CMachineStateChangedEvent.h"
# include "CMachineDataChangedEvent.h"
# include "CMachineRegisteredEvent.h"
# include "CSessionStateChangedEvent.h"
# include "CSnapshotTakenEvent.h"
# include "CSnapshotDeletedEvent.h"
# include "CSnapshotChangedEvent.h"
# include "CSnapshotRestoredEvent.h"
# include "CExtraDataCanChangeEvent.h"
# include "CExtraDataChangedEvent.h"
# include "CMousePointerShapeChangedEvent.h"
# include "CMouseCapabilityChangedEvent.h"
# include "CKeyboardLedsChangedEvent.h"
# include "CStateChangedEvent.h"
# include "CNetworkAdapterChangedEvent.h"
# include "CStorageDeviceChangedEvent.h"
# include "CMediumChangedEvent.h"
# include "CUSBDevice.h"
# include "CUSBDeviceStateChangedEvent.h"
# include "CGuestMonitorChangedEvent.h"
# include "CRuntimeErrorEvent.h"
# include "CCanShowWindowEvent.h"
# include "CShowWindowEvent.h"
#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */

UIMainEventListener::UIMainEventListener()
{
    /* Register meta-types for required enums. */
    qRegisterMetaType<KMachineState>("KMachineState");
    qRegisterMetaType<KSessionState>("KSessionState");
    qRegisterMetaType< QVector<uint8_t> >("QVector<uint8_t>");
    qRegisterMetaType<CNetworkAdapter>("CNetworkAdapter");
    qRegisterMetaType<CMediumAttachment>("CMediumAttachment");
    qRegisterMetaType<CUSBDevice>("CUSBDevice");
    qRegisterMetaType<CVirtualBoxErrorInfo>("CVirtualBoxErrorInfo");
    qRegisterMetaType<KGuestMonitorChangedEventType>("KGuestMonitorChangedEventType");
}

STDMETHODIMP UIMainEventListener::HandleEvent(NemuEventType_T /* type */, IEvent *pEvent)
{
    /* Try to acquire COM cleanup protection token first: */
    if (!nemuGlobal().comTokenTryLockForRead())
        return S_OK;

    CEvent event(pEvent);
    // printf("Event received: %d\n", event.GetType());
    switch (event.GetType())
    {
        case KNemuEventType_OnNemuSVCAvailabilityChanged:
        {
            CNemuSVCAvailabilityChangedEvent es(pEvent);
            emit sigNemuSVCAvailabilityChange(es.GetAvailable());
            break;
        }

        case KNemuEventType_OnMachineStateChanged:
        {
            CMachineStateChangedEvent es(pEvent);
            emit sigMachineStateChange(es.GetMachineId(), es.GetState());
            break;
        }
        case KNemuEventType_OnMachineDataChanged:
        {
            CMachineDataChangedEvent es(pEvent);
            emit sigMachineDataChange(es.GetMachineId());
            break;
        }
        case KNemuEventType_OnMachineRegistered:
        {
            CMachineRegisteredEvent es(pEvent);
            emit sigMachineRegistered(es.GetMachineId(), es.GetRegistered());
            break;
        }
        case KNemuEventType_OnSessionStateChanged:
        {
            CSessionStateChangedEvent es(pEvent);
            emit sigSessionStateChange(es.GetMachineId(), es.GetState());
            break;
        }
        case KNemuEventType_OnSnapshotTaken:
        {
            CSnapshotTakenEvent es(pEvent);
            emit sigSnapshotTake(es.GetMachineId(), es.GetSnapshotId());
            break;
        }
        case KNemuEventType_OnSnapshotDeleted:
        {
            CSnapshotDeletedEvent es(pEvent);
            emit sigSnapshotDelete(es.GetMachineId(), es.GetSnapshotId());
            break;
        }
        case KNemuEventType_OnSnapshotChanged:
        {
            CSnapshotChangedEvent es(pEvent);
            emit sigSnapshotChange(es.GetMachineId(), es.GetSnapshotId());
            break;
        }
        case KNemuEventType_OnSnapshotRestored:
        {
            CSnapshotRestoredEvent es(pEvent);
            emit sigSnapshotRestore(es.GetMachineId(), es.GetSnapshotId());
            break;
        }
//        case KNemuEventType_OnMediumRegistered:
//        case KNemuEventType_OnGuestPropertyChange:

        case KNemuEventType_OnExtraDataCanChange:
        {
            CExtraDataCanChangeEvent es(pEvent);
            /* Has to be done in place to give an answer: */
            bool fVeto = false;
            QString strReason;
            emit sigExtraDataCanChange(es.GetMachineId(), es.GetKey(), es.GetValue(), fVeto, strReason);
            if (fVeto)
                es.AddVeto(strReason);
            break;
        }
        case KNemuEventType_OnExtraDataChanged:
        {
            CExtraDataChangedEvent es(pEvent);
            emit sigExtraDataChange(es.GetMachineId(), es.GetKey(), es.GetValue());
            break;
        }

        case KNemuEventType_OnMousePointerShapeChanged:
        {
            CMousePointerShapeChangedEvent es(pEvent);
            emit sigMousePointerShapeChange(es.GetVisible(), es.GetAlpha(), QPoint(es.GetXhot(), es.GetYhot()), QSize(es.GetWidth(), es.GetHeight()), es.GetShape());
            break;
        }
        case KNemuEventType_OnMouseCapabilityChanged:
        {
            CMouseCapabilityChangedEvent es(pEvent);
            emit sigMouseCapabilityChange(es.GetSupportsAbsolute(), es.GetSupportsRelative(), es.GetSupportsMultiTouch(), es.GetNeedsHostCursor());
            break;
        }
        case KNemuEventType_OnKeyboardLedsChanged:
        {
            CKeyboardLedsChangedEvent es(pEvent);
            emit sigKeyboardLedsChangeEvent(es.GetNumLock(), es.GetCapsLock(), es.GetScrollLock());
            break;
        }
        case KNemuEventType_OnStateChanged:
        {
            CStateChangedEvent es(pEvent);
            emit sigStateChange(es.GetState());
            break;
        }
        case KNemuEventType_OnAdditionsStateChanged:
        {
            emit sigAdditionsChange();
            break;
        }
        case KNemuEventType_OnNetworkAdapterChanged:
        {
            CNetworkAdapterChangedEvent es(pEvent);
            emit sigNetworkAdapterChange(es.GetNetworkAdapter());
            break;
        }
        case KNemuEventType_OnStorageDeviceChanged:
        {
            CStorageDeviceChangedEvent es(pEvent);
            emit sigStorageDeviceChange(es.GetStorageDevice(), es.GetRemoved(), es.GetSilent());
            break;
        }
        case KNemuEventType_OnMediumChanged:
        {
            CMediumChangedEvent es(pEvent);
            emit sigMediumChange(es.GetMediumAttachment());
            break;
        }
        case KNemuEventType_OnVRDEServerChanged:
        case KNemuEventType_OnVRDEServerInfoChanged:
        {
            emit sigVRDEChange();
            break;
        }
        case KNemuEventType_OnVideoCaptureChanged:
        {
            emit sigVideoCaptureChange();
            break;
        }
        case KNemuEventType_OnUSBControllerChanged:
        {
            emit sigUSBControllerChange();
            break;
        }
        case KNemuEventType_OnUSBDeviceStateChanged:
        {
            CUSBDeviceStateChangedEvent es(pEvent);
            emit sigUSBDeviceStateChange(es.GetDevice(), es.GetAttached(), es.GetError());
            break;
        }
        case KNemuEventType_OnSharedFolderChanged:
        {
            emit sigSharedFolderChange();
            break;
        }
        case KNemuEventType_OnCPUExecutionCapChanged:
        {
            emit sigCPUExecutionCapChange();
            break;
        }
        case KNemuEventType_OnGuestMonitorChanged:
        {
            CGuestMonitorChangedEvent es(pEvent);
            emit sigGuestMonitorChange(es.GetChangeType(), es.GetScreenId(),
                                       QRect(es.GetOriginX(), es.GetOriginY(), es.GetWidth(), es.GetHeight()));
            break;
        }
        case KNemuEventType_OnRuntimeError:
        {
            CRuntimeErrorEvent es(pEvent);
            emit sigRuntimeError(es.GetFatal(), es.GetId(), es.GetMessage());
            break;
        }
        case KNemuEventType_OnCanShowWindow:
        {
            CCanShowWindowEvent es(pEvent);
            /* Has to be done in place to give an answer: */
            bool fVeto = false;
            QString strReason;
            emit sigCanShowWindow(fVeto, strReason);
            if (fVeto)
                es.AddVeto(strReason);
            else
                es.AddApproval(strReason);
            break;
        }
        case KNemuEventType_OnShowWindow:
        {
            CShowWindowEvent es(pEvent);
            /* Has to be done in place to give an answer: */
            LONG64 winId = es.GetWinId();
            if (winId != 0)
                break; /* Already set by some listener. */
            emit sigShowWindow(winId);
            es.SetWinId(winId);
            break;
        }
//        case KNemuEventType_OnSerialPortChanged:
//        case KNemuEventType_OnParallelPortChanged:
//        case KNemuEventType_OnStorageControllerChanged:
//        case KNemuEventType_OnCPUChange:

        default: break;
    }

    /* Unlock COM cleanup protection token: */
    nemuGlobal().comTokenUnlock();

    return S_OK;
}

