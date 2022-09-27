/* $Id: UIMachineWindowSeamless.h $ */
/** @file
 * Nemu Qt GUI - UIMachineWindowSeamless class declaration.
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

#ifndef ___UIMachineWindowSeamless_h___
#define ___UIMachineWindowSeamless_h___

/* GUI includes: */
#include "UIMachineWindow.h"

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
/* Forward declarations: */
class UIMiniToolBar;
#endif /* Q_WS_WIN || Q_WS_X11 */

/** UIMachineWindow reimplementation,
  * providing GUI with machine-window for the seamless mode. */
class UIMachineWindowSeamless : public UIMachineWindow
{
    Q_OBJECT;

protected:

    /** Constructor, passes @a pMachineLogic and @a uScreenId to the UIMachineWindow constructor. */
    UIMachineWindowSeamless(UIMachineLogic *pMachineLogic, ulong uScreenId);

private slots:

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /** Handles machine state change event. */
    void sltMachineStateChanged();

    /** Revokes window activation. */
    void sltRevokeWindowActivation();
#endif /* Q_WS_WIN || Q_WS_X11 */

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

#if defined(NEMU_WITH_TRANSLUCENT_SEAMLESS) && defined(Q_WS_WIN)
    /** Windows: Translucency stuff workaround. */
    void showEvent(QShowEvent *pEvent);
#endif /* NEMU_WITH_TRANSLUCENT_SEAMLESS && Q_WS_WIN */

#ifdef NEMU_WITH_MASKED_SEAMLESS
    /** Assigns guest seamless mask. */
    void setMask(const QRegion &maskGuest);
#endif /* NEMU_WITH_MASKED_SEAMLESS */

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /** Holds the mini-toolbar instance. */
    UIMiniToolBar *m_pMiniToolBar;
#endif /* Q_WS_WIN || Q_WS_X11 */

#ifdef NEMU_WITH_MASKED_SEAMLESS
    /** Holds the full seamless mask. */
    QRegion m_maskFull;
    /** Holds the guest seamless mask. */
    QRegion m_maskGuest;
#endif /* NEMU_WITH_MASKED_SEAMLESS */

    /** Factory support. */
    friend class UIMachineWindow;
};

#endif /* !___UIMachineWindowSeamless_h___ */

