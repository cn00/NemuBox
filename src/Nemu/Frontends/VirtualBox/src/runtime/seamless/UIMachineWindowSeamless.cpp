/* $Id: UIMachineWindowSeamless.cpp $ */
/** @file
 * Nemu Qt GUI - UIMachineWindowSeamless class implementation.
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

/* Qt includes: */
# include <QMenu>
# include <QTimer>

/* GUI includes: */
# include "NemuGlobal.h"
# include "UIExtraDataManager.h"
# include "UISession.h"
# include "UIActionPoolRuntime.h"
# include "UIMachineLogicSeamless.h"
# include "UIMachineWindowSeamless.h"
# include "UIMachineView.h"
# if   defined(Q_WS_WIN) || defined(Q_WS_X11)
#  include "UIMachineDefs.h"
#  include "UIMiniToolBar.h"
# elif defined(Q_WS_MAC)
#  include "NemuUtils.h"
# endif /* Q_WS_MAC */

/* COM includes: */
# include "CSnapshot.h"

#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */


UIMachineWindowSeamless::UIMachineWindowSeamless(UIMachineLogic *pMachineLogic, ulong uScreenId)
    : UIMachineWindow(pMachineLogic, uScreenId)
#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    , m_pMiniToolBar(0)
#endif /* Q_WS_WIN || Q_WS_X11 */
{
}

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
void UIMachineWindowSeamless::sltMachineStateChanged()
{
    /* Call to base-class: */
    UIMachineWindow::sltMachineStateChanged();

    /* Update mini-toolbar: */
    updateAppearanceOf(UIVisualElement_MiniToolBar);
}

void UIMachineWindowSeamless::sltRevokeWindowActivation()
{
    /* Make sure window is visible: */
    if (!isVisible() || isMinimized())
        return;

    /* Revoke stolen activation: */
#ifdef Q_WS_X11
    raise();
#endif /* Q_WS_X11 */
    activateWindow();
}
#endif /* Q_WS_WIN || Q_WS_X11 */

void UIMachineWindowSeamless::prepareVisualState()
{
    /* Call to base-class: */
    UIMachineWindow::prepareVisualState();

    /* Make sure we have no background
     * until the first one paint-event: */
    setAttribute(Qt::WA_NoSystemBackground);

#ifdef NEMU_WITH_TRANSLUCENT_SEAMLESS
# ifdef Q_WS_MAC
    /* Using native API to enable translucent background for the Mac host.
     * - We also want to disable window-shadows which is possible
     *   using Qt::WA_MacNoShadow only since Qt 4.8,
     *   while minimum supported version is 4.7.1 for now: */
    ::darwinSetShowsWindowTransparent(this, true);
# else /* Q_WS_MAC */
    /* Using Qt API to enable translucent background:
     * - Under Win host Qt conflicts with 3D stuff (black seamless regions).
     * - Under Mac host Qt doesn't allows to disable window-shadows
     *   until version 4.8, but minimum supported version is 4.7.1 for now. */
    setAttribute(Qt::WA_TranslucentBackground);
# endif /* !Q_WS_MAC */
#endif /* NEMU_WITH_TRANSLUCENT_SEAMLESS */

#ifdef NEMU_WITH_MASKED_SEAMLESS
    /* Make sure we have no background
     * until the first one set-region-event: */
    setMask(m_maskGuest);
#endif /* NEMU_WITH_MASKED_SEAMLESS */

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /* Prepare mini-toolbar: */
    prepareMiniToolbar();
#endif /* Q_WS_WIN || Q_WS_X11 */
}

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
void UIMachineWindowSeamless::prepareMiniToolbar()
{
    /* Make sure mini-toolbar is not restricted: */
    if (!gEDataManager->miniToolbarEnabled(nemuGlobal().managedVMUuid()))
        return;

    /* Create mini-toolbar: */
    m_pMiniToolBar = new UIMiniToolBar(this,
                                       GeometryType_Available,
                                       gEDataManager->miniToolbarAlignment(nemuGlobal().managedVMUuid()),
                                       gEDataManager->autoHideMiniToolbar(nemuGlobal().managedVMUuid()));
    AssertPtrReturnVoid(m_pMiniToolBar);
    {
        /* Configure mini-toolbar: */
        m_pMiniToolBar->addMenus(actionPool()->menus());
        connect(m_pMiniToolBar, SIGNAL(sigMinimizeAction()),
                this, SLOT(showMinimized()), Qt::QueuedConnection);
        connect(m_pMiniToolBar, SIGNAL(sigExitAction()),
                actionPool()->action(UIActionIndexRT_M_View_T_Seamless), SLOT(trigger()));
        connect(m_pMiniToolBar, SIGNAL(sigCloseAction()),
                actionPool()->action(UIActionIndex_M_Application_S_Close), SLOT(trigger()));
        connect(m_pMiniToolBar, SIGNAL(sigNotifyAboutWindowActivationStolen()),
                this, SLOT(sltRevokeWindowActivation()), Qt::QueuedConnection);
    }
}
#endif /* Q_WS_WIN || Q_WS_X11 */

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
void UIMachineWindowSeamless::cleanupMiniToolbar()
{
    /* Make sure mini-toolbar was created: */
    if (!m_pMiniToolBar)
        return;

    /* Save mini-toolbar settings: */
    gEDataManager->setAutoHideMiniToolbar(m_pMiniToolBar->autoHide(), nemuGlobal().managedVMUuid());
    /* Delete mini-toolbar: */
    delete m_pMiniToolBar;
    m_pMiniToolBar = 0;
}
#endif /* Q_WS_WIN || Q_WS_X11 */

void UIMachineWindowSeamless::cleanupVisualState()
{
#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /* Cleanup mini-toolbar: */
    cleanupMiniToolbar();
#endif /* Q_WS_WIN || Q_WS_X11 */

    /* Call to base-class: */
    UIMachineWindow::cleanupVisualState();
}

void UIMachineWindowSeamless::placeOnScreen()
{
    /* Get corresponding host-screen: */
    const int iHostScreen = qobject_cast<UIMachineLogicSeamless*>(machineLogic())->hostScreenForGuestScreen(m_uScreenId);
    /* And corresponding working area: */
    const QRect workingArea = nemuGlobal().availableGeometry(iHostScreen);

    /* Set appropriate geometry for window: */
    resize(workingArea.size());
    move(workingArea.topLeft());

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
    /* If there is a mini-toolbar: */
    if (m_pMiniToolBar)
    {
        /* Set appropriate geometry for mini-toolbar: */
        m_pMiniToolBar->resize(workingArea.size());
        m_pMiniToolBar->move(workingArea.topLeft());
    }
#endif /* Q_WS_WIN || Q_WS_X11 */
}

void UIMachineWindowSeamless::showInNecessaryMode()
{
    /* Make sure window has seamless logic: */
    UIMachineLogicSeamless *pSeamlessLogic = qobject_cast<UIMachineLogicSeamless*>(machineLogic());
    AssertPtrReturnVoid(pSeamlessLogic);

    /* Make sure window should be shown and mapped to some host-screen: */
    if (!uisession()->isScreenVisible(m_uScreenId) ||
        !pSeamlessLogic->hasHostScreenForGuestScreen(m_uScreenId))
    {
#if defined(Q_WS_WIN) || defined(Q_WS_X11)
        /* If there is a mini-toolbar: */
        if (m_pMiniToolBar)
        {
            /* Hide mini-toolbar: */
            m_pMiniToolBar->hide();
        }
#endif /* Q_WS_WIN || Q_WS_X11 */

        /* Hide window: */
        hide();
    }
    else
    {
        /* Ignore if window minimized: */
        if (isMinimized())
            return;

        /* Make sure window have appropriate geometry: */
        placeOnScreen();

        /* Show window in normal mode: */
        show();

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
        /* If there is a mini-toolbar: */
        if (m_pMiniToolBar)
        {
            /* Show mini-toolbar in normal mode: */
            m_pMiniToolBar->show();
        }
#endif /* Q_WS_WIN || Q_WS_X11 */

        /* Adjust machine-view size if necessary: */
        adjustMachineViewSize();

        /* Make sure machine-view have focus: */
        m_pMachineView->setFocus();
    }
}

#if defined(Q_WS_WIN) || defined(Q_WS_X11)
void UIMachineWindowSeamless::updateAppearanceOf(int iElement)
{
    /* Call to base-class: */
    UIMachineWindow::updateAppearanceOf(iElement);

    /* Update mini-toolbar: */
    if (iElement & UIVisualElement_MiniToolBar)
    {
        /* If there is a mini-toolbar: */
        if (m_pMiniToolBar)
        {
            /* Get snapshot(s): */
            QString strSnapshotName;
            if (machine().GetSnapshotCount() > 0)
            {
                CSnapshot snapshot = machine().GetCurrentSnapshot();
                strSnapshotName = " (" + snapshot.GetName() + ")";
            }
            /* Update mini-toolbar text: */
            m_pMiniToolBar->setText(machineName() + strSnapshotName);
        }
    }
}
#endif /* Q_WS_WIN || Q_WS_X11 */

#if defined(NEMU_WITH_TRANSLUCENT_SEAMLESS) && defined(Q_WS_WIN)
void UIMachineWindowSeamless::showEvent(QShowEvent *pShowEvent)
{
    /* Call to base class: */
    UIMachineWindow::showEvent(pShowEvent);

    /* Following workaround allows to fix the next Qt BUG:
     * https://bugreports.qt-project.org/browse/QTBUG-17548
     * https://bugreports.qt-project.org/browse/QTBUG-30974
     * Widgets with Qt::WA_TranslucentBackground attribute
     * stops repainting after minimizing/restoring, we have to call for single update. */
    QApplication::postEvent(this, new QEvent(QEvent::UpdateRequest), Qt::LowEventPriority);
}
#endif /* NEMU_WITH_TRANSLUCENT_SEAMLESS && Q_WS_WIN */

#ifdef NEMU_WITH_MASKED_SEAMLESS
void UIMachineWindowSeamless::setMask(const QRegion &maskGuest)
{
    /* Remember new guest mask: */
    m_maskGuest = maskGuest;

    /* Prepare full mask: */
    QRegion maskFull(m_maskGuest);

    /* Shift full mask if left or top spacer width is NOT zero: */
    if (m_pLeftSpacer->geometry().width() || m_pTopSpacer->geometry().height())
        maskFull.translate(m_pLeftSpacer->geometry().width(), m_pTopSpacer->geometry().height());

    /* Seamless-window for empty full mask should be empty too,
     * but the QWidget::setMask() wrapper doesn't allow this.
     * Instead, we see a full guest-screen of available-geometry size.
     * So we have to make sure full mask have at least one pixel. */
    if (maskFull.isEmpty())
        maskFull += QRect(0, 0, 1, 1);

    /* Make sure full mask had changed: */
    if (m_maskFull != maskFull)
    {
        /* Compose viewport region to update: */
        QRegion toUpdate = m_maskFull + maskFull;
        /* Remember new full mask: */
        m_maskFull = maskFull;
        /* Assign new full mask: */
        UIMachineWindow::setMask(m_maskFull);
        /* Update viewport region finally: */
        m_pMachineView->viewport()->update(toUpdate);
    }
}
#endif /* NEMU_WITH_MASKED_SEAMLESS */

