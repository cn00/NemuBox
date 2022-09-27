/* $Id: UIMachineWindowScale.h $ */
/** @file
 * Nemu Qt GUI - UIMachineWindowScale class declaration.
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

#ifndef ___UIMachineWindowScale_h___
#define ___UIMachineWindowScale_h___

/* GUI includes: */
#include "UIMachineWindow.h"

/** UIMachineWindow reimplementation,
  * providing GUI with machine-window for the scale mode. */
class UIMachineWindowScale : public UIMachineWindow
{
    Q_OBJECT;

protected:

    /** Constructor, passes @a pMachineLogic and @a uScreenId to the UIMachineWindow constructor. */
    UIMachineWindowScale(UIMachineLogic *pMachineLogic, ulong uScreenId);

private:

    /** Prepare main-layout routine. */
    void prepareMainLayout();
#ifdef Q_WS_MAC
    /** Prepare visual-state routine. */
    void prepareVisualState();
#endif /* Q_WS_MAC */
    /** Load settings routine. */
    void loadSettings();

    /** Save settings routine. */
    void saveSettings();
#ifdef Q_WS_MAC
    /** Cleanup visual-state routine. */
    void cleanupVisualState();
#endif /* Q_WS_MAC */

    /** Updates visibility according to visual-state. */
    void showInNecessaryMode();

    /** Common @a pEvent handler. */
    bool event(QEvent *pEvent);
#ifdef Q_WS_WIN
    /** Windows: Common native @a pEvent handler. */
    bool winEvent(MSG *pMessage, long *pResult);
#endif /* Q_WS_WIN */

    /** Returns whether this window is maximized. */
    bool isMaximizedChecked();

    /** Holds the current window geometry. */
    QRect m_normalGeometry;

    /** Factory support. */
    friend class UIMachineWindow;
};

#endif /* !___UIMachineWindowScale_h___ */

