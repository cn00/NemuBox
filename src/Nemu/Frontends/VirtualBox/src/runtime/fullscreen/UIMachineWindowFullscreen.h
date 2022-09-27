/* $Id: UIMachineWindowFullscreen.h $ */
/** @file
 * Nemu Qt GUI - UIMachineWindowFullscreen class declaration.
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

#ifndef ___UIMachineWindowFullscreen_h___
#define ___UIMachineWindowFullscreen_h___

/* GUI includes: */
#include "UIMachineWindow.h"

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
/* Forward declarations: */
class UIMiniToolBar;
#endif /* Q_WS_WIN || Q_WS_X11 */

/** UIMachineWindow reimplementation,
  * providing GUI with machine-window for the full-screen mode. */
class UIMachineWindowFullscreen : public UIMachineWindow
{
    Q_OBJECT;

#ifdef RT_OS_DARWIN
signals:
    /** Mac OS X: Notifies listener about native 'fullscreen' will be entered. */
    void sigNotifyAboutNativeFullscreenWillEnter();
    /** Mac OS X: Notifies listener about native 'fullscreen' entered. */
    void sigNotifyAboutNativeFullscreenDidEnter();
    /** Mac OS X: Notifies listener about native 'fullscreen' will be exited. */
    void sigNotifyAboutNativeFullscreenWillExit();
    /** Mac OS X: Notifies listener about native 'fullscreen' exited. */
    void sigNotifyAboutNativeFullscreenDidExit();
    /** Mac OS X: Notifies listener about native 'fullscreen' fail to enter. */
    void sigNotifyAboutNativeFullscreenFailToEnter();
#endif /* RT_OS_DARWIN */

protected:

    /** Constructor, passes @a pMachineLogic and @a uScreenId to the UIMachineWindow constructor. */
    UIMachineWindowFullscreen(UIMachineLogic *pMachineLogic, ulong uScreenId);

#ifdef Q_WS_MAC
    /** Mac OS X: Handles native notifications @a strNativeNotificationName for 'fullscreen' window. */
    void handleNativeNotification(const QString &strNativeNotificationName);
    /** Mac OS X: Returns whether window is in 'fullscreen' transition. */
    bool isInFullscreenTransition() const { return m_fIsInFullscreenTransition; }
#endif /* Q_WS_MAC */

private slots:

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /** Handles machine state change event. */
    void sltMachineStateChanged();

    /** Revokes window activation. */
    void sltRevokeWindowActivation();
#endif /* Q_WS_WIN || Q_WS_X11 */

#ifdef RT_OS_DARWIN
    /** Mac OS X: Commands @a pMachineWindow to enter native 'fullscreen' mode if possible. */
    void sltEnterNativeFullscreen(UIMachineWindow *pMachineWindow);
    /** Mac OS X: Commands @a pMachineWindow to exit native 'fullscreen' mode if possible. */
    void sltExitNativeFullscreen(UIMachineWindow *pMachineWindow);
#endif /* RT_OS_DARWIN */

private:

    /** Prepare visual-state routine. */
    void prepareVisualState();
#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /** Prepare mini-toolbar routine. */
    void prepareMiniToolbar();
#endif /* Q_WS_WIN || Q_WS_X11 */

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /** Cleanup mini-toolbar routine. */
    void cleanupMiniToolbar();
#endif /* Q_WS_WIN || Q_WS_X11 */
    /** Cleanup visual-state routine. */
    void cleanupVisualState();

    /** Updates geometry according to visual-state. */
    void placeOnScreen();
    /** Updates visibility according to visual-state. */
    void showInNecessaryMode();

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /** Common update routine. */
    void updateAppearanceOf(int iElement);
#endif /* Q_WS_WIN || Q_WS_X11 */

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /** Holds the mini-toolbar instance. */
    UIMiniToolBar *m_pMiniToolBar;
#endif /* Q_WS_WIN || Q_WS_X11 */

#ifdef Q_WS_MAC
    /** Mac OS X: Reflects whether window is in 'fullscreen' transition. */
    bool m_fIsInFullscreenTransition;
    /** Mac OS X: Allows 'fullscreen' API access: */
    friend class UIMachineLogicFullscreen;
#endif /* Q_WS_MAC */

    /** Factory support. */
    friend class UIMachineWindow;
};

#endif /* !___UIMachineWindowFullscreen_h___ */

