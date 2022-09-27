/* $Id: UIMiniToolBar.cpp $ */
/** @file
 * Nemu Qt GUI - UIMiniToolBar class implementation.
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

#ifdef NEMU_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !NEMU_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
# include <QApplication>
# include <QTimer>
# include <QMdiArea>
# include <QMdiSubWindow>
# include <QLabel>
# include <QMenu>
# include <QToolButton>
# include <QStateMachine>
# include <QPainter>
# ifdef Q_WS_X11
#  include <QX11Info>
# endif /* Q_WS_X11 */

/* GUI includes: */
# include "UIMiniToolBar.h"
# include "UIAnimationFramework.h"
# include "UIIconPool.h"
# include "NemuGlobal.h"

#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */

/** UIToolBar reimplementation
  * providing UIMiniToolBar with mini-toolbar. */
class UIMiniToolBarPrivate : public UIToolBar
{
    Q_OBJECT;

signals:

    /** Notifies listeners about we are resized. */
    void sigResized();

    /** Notifies listeners about action triggered to toggle auto-hide. */
    void sigAutoHideToggled();
    /** Notifies listeners about action triggered to minimize. */
    void sigMinimizeAction();
    /** Notifies listeners about action triggered to exit. */
    void sigExitAction();
    /** Notifies listeners about action triggered to close. */
    void sigCloseAction();

public:

    /** Constructor. */
    UIMiniToolBarPrivate();

    /** Defines @a alignment. */
    void setAlignment(Qt::Alignment alignment);

    /** Returns whether we do auto-hide. */
    bool autoHide() const;
    /** Defines whether we do @a fAutoHide. */
    void setAutoHide(bool fAutoHide);

    /** Defines our @a strText. */
    void setText(const QString &strText);

    /** Adds our @a menus. */
    void addMenus(const QList<QMenu*> &menus);

protected:

    /** Show @a pEvent handler. */
    virtual void showEvent(QShowEvent *pEvent);
    /** Polish @a pEvent handler. */
    virtual void polishEvent(QShowEvent *pEvent);
    /** Resize @a pEvent handler. */
    virtual void resizeEvent(QResizeEvent *pEvent);
    /** Paint @a pEvent handler. */
    virtual void paintEvent(QPaintEvent *pEvent);

private:

    /** Prepare routine. */
    void prepare();

    /** Rebuilds our shape. */
    void rebuildShape();

    /** Holds whether this widget was polished. */
    bool m_fPolished;
    /** Holds the alignment type. */
    Qt::Alignment m_alignment;
    /** Holds the shape. */
    QPainterPath m_shape;

    /** Holds the action to toggle auto-hide. */
    QAction *m_pAutoHideAction;
    /** Holds the name label. */
    QLabel *m_pLabel;
    /** Holds the action to trigger minimize. */
    QAction *m_pMinimizeAction;
    /** Holds the action to trigger exit. */
    QAction *m_pRestoreAction;
    /** Holds the action to trigger close. */
    QAction *m_pCloseAction;

    /** Holds the pointer to the place to insert menu. */
    QAction *m_pMenuInsertPosition;

    /** Holds the spacings. */
    QList<QWidget*> m_spacings;
    /** Holds the margins. */
    QList<QWidget*> m_margins;
};

UIMiniToolBarPrivate::UIMiniToolBarPrivate()
    /* Variables: General stuff: */
    : m_fPolished(false)
    , m_alignment(Qt::AlignBottom)
    /* Variables: Contents stuff: */
    , m_pAutoHideAction(0)
    , m_pLabel(0)
    , m_pMinimizeAction(0)
    , m_pRestoreAction(0)
    , m_pCloseAction(0)
    /* Variables: Menu stuff: */
    , m_pMenuInsertPosition(0)
{
    /* Prepare: */
    prepare();
}

void UIMiniToolBarPrivate::setAlignment(Qt::Alignment alignment)
{
    /* Make sure alignment really changed: */
    if (m_alignment == alignment)
        return;

    /* Update alignment: */
    m_alignment = alignment;

    /* Rebuild shape: */
    rebuildShape();
}

bool UIMiniToolBarPrivate::autoHide() const
{
    /* Return auto-hide: */
    return !m_pAutoHideAction->isChecked();
}

void UIMiniToolBarPrivate::setAutoHide(bool fAutoHide)
{
    /* Make sure auto-hide really changed: */
    if (m_pAutoHideAction->isChecked() == !fAutoHide)
        return;

    /* Update auto-hide: */
    m_pAutoHideAction->setChecked(!fAutoHide);
}

void UIMiniToolBarPrivate::setText(const QString &strText)
{
    /* Make sure text really changed: */
    if (m_pLabel->text() == strText)
        return;

    /* Update text: */
    m_pLabel->setText(strText);

    /* Resize to sizehint: */
    resize(sizeHint());
}

void UIMiniToolBarPrivate::addMenus(const QList<QMenu*> &menus)
{
    /* For each of the passed menu items: */
    for (int i = 0; i < menus.size(); ++i)
    {
        /* Get corresponding menu-action: */
        QAction *pAction = menus[i]->menuAction();
        /* Insert it into corresponding place: */
        insertAction(m_pMenuInsertPosition, pAction);
        /* Configure corresponding tool-button: */
        if (QToolButton *pButton = qobject_cast<QToolButton*>(widgetForAction(pAction)))
        {
            pButton->setPopupMode(QToolButton::InstantPopup);
            pButton->setAutoRaise(true);
        }
        /* Add some spacing: */
        if (i != menus.size() - 1)
            m_spacings << widgetForAction(insertWidget(m_pMenuInsertPosition, new QWidget(this)));
    }

    /* Resize to sizehint: */
    resize(sizeHint());
}

void UIMiniToolBarPrivate::showEvent(QShowEvent *pEvent)
{
    /* Make sure we should polish dialog: */
    if (m_fPolished)
        return;

    /* Call to polish-event: */
    polishEvent(pEvent);

    /* Mark dialog as polished: */
    m_fPolished = true;
}

void UIMiniToolBarPrivate::polishEvent(QShowEvent*)
{
    /* Toolbar spacings: */
    foreach(QWidget *pSpacing, m_spacings)
        pSpacing->setMinimumWidth(5);

    /* Title spacings: */
    foreach(QWidget *pLableMargin, m_margins)
        pLableMargin->setMinimumWidth(15);

    /* Resize to sizehint: */
    resize(sizeHint());
}

void UIMiniToolBarPrivate::resizeEvent(QResizeEvent*)
{
    /* Rebuild shape: */
    rebuildShape();

    /* Notify listeners: */
    emit sigResized();
}

void UIMiniToolBarPrivate::paintEvent(QPaintEvent*)
{
    /* Prepare painter: */
    QPainter painter(this);

    /* Fill background: */
    if (!m_shape.isEmpty())
    {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipPath(m_shape);
    }
    QRect backgroundRect = rect();
    QColor backgroundColor = palette().color(QPalette::Window);
    QLinearGradient headerGradient(backgroundRect.bottomLeft(), backgroundRect.topLeft());
    headerGradient.setColorAt(0, backgroundColor.darker(120));
    headerGradient.setColorAt(1, backgroundColor.darker(90));
    painter.fillRect(backgroundRect, headerGradient);
}

void UIMiniToolBarPrivate::prepare()
{
    /* Determine icon metric: */
    const QStyle *pStyle = QApplication::style();
    const int iIconMetric = pStyle->pixelMetric(QStyle::PM_SmallIconSize);

    /* Configure toolbar: */
    setIconSize(QSize(iIconMetric, iIconMetric));

    /* Left margin: */
#ifdef Q_WS_X11
    if (QX11Info::isCompositingManagerRunning())
        m_spacings << widgetForAction(addWidget(new QWidget));
#else /* !Q_WS_X11 */
    m_spacings << widgetForAction(addWidget(new QWidget));
#endif /* !Q_WS_X11 */

    /* Prepare push-pin: */
    m_pAutoHideAction = new QAction(this);
    m_pAutoHideAction->setIcon(UIIconPool::iconSet(":/pin_16px.png"));
    m_pAutoHideAction->setToolTip(UIMiniToolBar::tr("Always show the toolbar"));
    m_pAutoHideAction->setCheckable(true);
    connect(m_pAutoHideAction, SIGNAL(toggled(bool)), this, SIGNAL(sigAutoHideToggled()));
    addAction(m_pAutoHideAction);

    /* Left menu margin: */
    m_spacings << widgetForAction(addWidget(new QWidget));

    /* Right menu margin: */
    m_pMenuInsertPosition = addWidget(new QWidget);
    m_spacings << widgetForAction(m_pMenuInsertPosition);

    /* Left label margin: */
    m_margins << widgetForAction(addWidget(new QWidget));

    /* Insert a label for VM Name: */
    m_pLabel = new QLabel;
    m_pLabel->setAlignment(Qt::AlignCenter);
    addWidget(m_pLabel);

    /* Right label margin: */
    m_margins << widgetForAction(addWidget(new QWidget));

    /* Minimize action: */
    m_pMinimizeAction = new QAction(this);
    m_pMinimizeAction->setIcon(UIIconPool::iconSet(":/minimize_16px.png"));
    m_pMinimizeAction->setToolTip(UIMiniToolBar::tr("Minimize Window"));
    connect(m_pMinimizeAction, SIGNAL(triggered()), this, SIGNAL(sigMinimizeAction()));
    addAction(m_pMinimizeAction);

    /* Exit action: */
    m_pRestoreAction = new QAction(this);
    m_pRestoreAction->setIcon(UIIconPool::iconSet(":/restore_16px.png"));
    m_pRestoreAction->setToolTip(UIMiniToolBar::tr("Exit Full Screen or Seamless Mode"));
    connect(m_pRestoreAction, SIGNAL(triggered()), this, SIGNAL(sigExitAction()));
    addAction(m_pRestoreAction);

    /* Close action: */
    m_pCloseAction = new QAction(this);
    m_pCloseAction->setIcon(UIIconPool::iconSet(":/close_16px.png"));
    m_pCloseAction->setToolTip(UIMiniToolBar::tr("Close VM"));
    connect(m_pCloseAction, SIGNAL(triggered()), this, SIGNAL(sigCloseAction()));
    addAction(m_pCloseAction);

    /* Right margin: */
#ifdef Q_WS_X11
    if (QX11Info::isCompositingManagerRunning())
        m_spacings << widgetForAction(addWidget(new QWidget));
#else /* !Q_WS_X11 */
    m_spacings << widgetForAction(addWidget(new QWidget));
#endif /* !Q_WS_X11 */

    /* Resize to sizehint: */
    resize(sizeHint());
}

void UIMiniToolBarPrivate::rebuildShape()
{
#ifdef Q_WS_X11
    if (!QX11Info::isCompositingManagerRunning())
        return;
#endif /* Q_WS_X11 */

    /* Rebuild shape: */
    QPainterPath shape;
    switch (m_alignment)
    {
        case Qt::AlignTop:
        {
            shape.moveTo(0, 0);
            shape.lineTo(shape.currentPosition().x(), height() - 10);
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(20, 20)).translated(0, -10), 180, 90);
            shape.lineTo(width() - 10, shape.currentPosition().y());
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(20, 20)).translated(-10, -20), 270, 90);
            shape.lineTo(shape.currentPosition().x(), 0);
            shape.closeSubpath();
            break;
        }
        case Qt::AlignBottom:
        {
            shape.moveTo(0, height());
            shape.lineTo(shape.currentPosition().x(), 10);
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(20, 20)).translated(0, -10), 180, -90);
            shape.lineTo(width() - 10, shape.currentPosition().y());
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(20, 20)).translated(-10, 0), 90, -90);
            shape.lineTo(shape.currentPosition().x(), height());
            shape.closeSubpath();
            break;
        }
        default:
            break;
    }
    m_shape = shape;

    /* Update: */
    update();
}

UIMiniToolBar::UIMiniToolBar(QWidget *pParent,
                             GeometryType geometryType,
                             Qt::Alignment alignment,
                             bool fAutoHide /* = true */)
    : QWidget(pParent, Qt::Tool | Qt::FramelessWindowHint)
    /* Variables: General stuff: */
    , m_geometryType(geometryType)
    , m_alignment(alignment)
    , m_fAutoHide(fAutoHide)
    /* Variables: Contents stuff: */
    , m_pMdiArea(0)
    , m_pToolbar(0)
    , m_pEmbeddedToolbar(0)
    /* Variables: Hover stuff: */
    , m_fHovered(false)
    , m_pHoverEnterTimer(0)
    , m_pHoverLeaveTimer(0)
    , m_pAnimation(0)
{
    /* Prepare: */
    prepare();
}

UIMiniToolBar::~UIMiniToolBar()
{
    /* Cleanup: */
    cleanup();
}

void UIMiniToolBar::setAlignment(Qt::Alignment alignment)
{
    /* Make sure toolbar created: */
    AssertPtrReturnVoid(m_pToolbar);

    /* Make sure alignment really changed: */
    if (m_alignment == alignment)
        return;

    /* Update alignment: */
    m_alignment = alignment;

    /* Adjust geometry: */
    adjustGeometry();

    /* Propagate to child to update shape: */
    m_pToolbar->setAlignment(m_alignment);
}

void UIMiniToolBar::setAutoHide(bool fAutoHide, bool fPropagateToChild /* = true */)
{
    /* Make sure toolbar created: */
    AssertPtrReturnVoid(m_pToolbar);

    /* Make sure auto-hide really changed: */
    if (m_fAutoHide == fAutoHide)
        return;

    /* Update auto-hide: */
    m_fAutoHide = fAutoHide;

    /* Adjust geometry: */
    adjustGeometry();

    /* Propagate to child to update action if necessary: */
    if (fPropagateToChild)
        m_pToolbar->setAutoHide(m_fAutoHide);
}

void UIMiniToolBar::setText(const QString &strText)
{
    /* Make sure toolbar created: */
    AssertPtrReturnVoid(m_pToolbar);

    /* Propagate to child: */
    m_pToolbar->setText(strText);
}

void UIMiniToolBar::addMenus(const QList<QMenu*> &menus)
{
    /* Make sure toolbar created: */
    AssertPtrReturnVoid(m_pToolbar);

    /* Propagate to child: */
    m_pToolbar->addMenus(menus);
}

void UIMiniToolBar::adjustGeometry()
{
    /* Resize embedded-toolbar to minimum size: */
    m_pEmbeddedToolbar->resize(m_pEmbeddedToolbar->sizeHint());

    /* Calculate embedded-toolbar position: */
    int iX = 0, iY = 0;
    iX = width() / 2 - m_pEmbeddedToolbar->width() / 2;
    switch (m_alignment)
    {
        case Qt::AlignTop:    iY = 0; break;
        case Qt::AlignBottom: iY = height() - m_pEmbeddedToolbar->height(); break;
        default: break;
    }

    /* Update auto-hide animation: */
    m_shownToolbarPosition = QPoint(iX, iY);
    switch (m_alignment)
    {
        case Qt::AlignTop:    m_hiddenToolbarPosition = m_shownToolbarPosition - QPoint(0, m_pEmbeddedToolbar->height() - 3); break;
        case Qt::AlignBottom: m_hiddenToolbarPosition = m_shownToolbarPosition + QPoint(0, m_pEmbeddedToolbar->height() - 3); break;
    }
    m_pAnimation->update();

    /* Update embedded-toolbar geometry if known: */
    if (property("AnimationState").toString() == "Final")
        m_pEmbeddedToolbar->move(m_shownToolbarPosition);
    else
        m_pEmbeddedToolbar->move(m_hiddenToolbarPosition);

#ifdef Q_WS_X11
    /* Adjust window mask: */
    setMask(m_pEmbeddedToolbar->geometry());
#endif /* Q_WS_X11 */

    /* Simulate toolbar auto-hiding: */
    simulateToolbarAutoHiding();
}

void UIMiniToolBar::sltHandleToolbarResize()
{
    /* Adjust geometry: */
    adjustGeometry();
}

void UIMiniToolBar::sltAutoHideToggled()
{
    /* Propagate from child: */
    setAutoHide(m_pToolbar->autoHide(), false);
}

void UIMiniToolBar::sltHoverEnter()
{
    /* Mark as 'hovered' if necessary: */
    if (!m_fHovered)
    {
        m_fHovered = true;
        emit sigHoverEnter();
    }
}

void UIMiniToolBar::sltHoverLeave()
{
    /* Mark as 'unhovered' if necessary: */
    if (m_fHovered)
    {
        m_fHovered = false;
        emit sigHoverLeave();
    }
}

void UIMiniToolBar::prepare()
{
    /* Install own event-filter
     * to handle window activation stealing: */
    installEventFilter(this);

#if   defined(Q_WS_WIN)
    /* No background until first paint-event: */
    setAttribute(Qt::WA_NoSystemBackground);
    /* Enable translucency through Qt API: */
    setAttribute(Qt::WA_TranslucentBackground);
#elif defined(Q_WS_X11)
    /* Enable translucency through Qt API if supported: */
    if (QX11Info::isCompositingManagerRunning())
        setAttribute(Qt::WA_TranslucentBackground);
#endif /* Q_WS_X11 */

    /* Make sure we have no focus: */
    setFocusPolicy(Qt::NoFocus);

    /* Prepare mdi-area: */
    m_pMdiArea = new QMdiArea;
    {
        /* Allow any MDI area size: */
        m_pMdiArea->setMinimumSize(QSize(1, 1));
        /* Configure own background: */
        QPalette pal = m_pMdiArea->palette();
        pal.setColor(QPalette::Window, QColor(Qt::transparent));
        m_pMdiArea->setPalette(pal);
        /* Configure viewport background: */
        m_pMdiArea->setBackground(QColor(Qt::transparent));
        /* Layout mdi-area according parent-widget: */
        QVBoxLayout *pMainLayout = new QVBoxLayout(this);
        pMainLayout->setContentsMargins(0, 0, 0, 0);
        pMainLayout->addWidget(m_pMdiArea);
        /* Make sure we have no focus: */
        m_pMdiArea->setFocusPolicy(Qt::NoFocus);
        m_pMdiArea->viewport()->setFocusPolicy(Qt::NoFocus);
    }

    /* Prepare mini-toolbar: */
    m_pToolbar = new UIMiniToolBarPrivate;
    {
        /* Make sure we have no focus: */
        m_pToolbar->setFocusPolicy(Qt::NoFocus);
        /* Propagate known options to child: */
        m_pToolbar->setAutoHide(m_fAutoHide);
        m_pToolbar->setAlignment(m_alignment);
        /* Configure own background: */
        QPalette pal = m_pToolbar->palette();
        pal.setColor(QPalette::Window, palette().color(QPalette::Window));
        m_pToolbar->setPalette(pal);
        /* Configure child connections: */
        connect(m_pToolbar, SIGNAL(sigResized()), this, SLOT(sltHandleToolbarResize()));
        connect(m_pToolbar, SIGNAL(sigAutoHideToggled()), this, SLOT(sltAutoHideToggled()));
        connect(m_pToolbar, SIGNAL(sigMinimizeAction()), this, SIGNAL(sigMinimizeAction()));
        connect(m_pToolbar, SIGNAL(sigExitAction()), this, SIGNAL(sigExitAction()));
        connect(m_pToolbar, SIGNAL(sigCloseAction()), this, SIGNAL(sigCloseAction()));
        /* Add child to mdi-area: */
        m_pEmbeddedToolbar = m_pMdiArea->addSubWindow(m_pToolbar, Qt::Window | Qt::FramelessWindowHint);
        /* Make sure we have no focus: */
        m_pEmbeddedToolbar->setFocusPolicy(Qt::NoFocus);
    }

    /* Prepare hover-enter/leave timers: */
    m_pHoverEnterTimer = new QTimer(this);
    {
        m_pHoverEnterTimer->setSingleShot(true);
        m_pHoverEnterTimer->setInterval(50);
        connect(m_pHoverEnterTimer, SIGNAL(timeout()), this, SLOT(sltHoverEnter()));
    }
    m_pHoverLeaveTimer = new QTimer(this);
    {
        m_pHoverLeaveTimer->setSingleShot(true);
        m_pHoverLeaveTimer->setInterval(500);
        connect(m_pHoverLeaveTimer, SIGNAL(timeout()), this, SLOT(sltHoverLeave()));
    }

    /* Install 'auto-hide' animation to 'toolbarPosition' property: */
    m_pAnimation = UIAnimation::installPropertyAnimation(this,
                                                         "toolbarPosition",
                                                         "hiddenToolbarPosition", "shownToolbarPosition",
                                                         SIGNAL(sigHoverEnter()), SIGNAL(sigHoverLeave()),
                                                         true);

    /* Adjust geometry first time: */
    adjustGeometry();
}

void UIMiniToolBar::cleanup()
{
    /* Stop hover-enter/leave timers: */
    if (m_pHoverEnterTimer && m_pHoverEnterTimer->isActive())
        m_pHoverEnterTimer->stop();
    if (m_pHoverLeaveTimer && m_pHoverLeaveTimer->isActive())
        m_pHoverLeaveTimer->stop();

    /* Destroy animation before mdi-toolbar: */
    delete m_pAnimation;
    m_pAnimation = 0;

    /* Destroy mdi-toolbar after animation: */
    delete m_pEmbeddedToolbar;
    m_pEmbeddedToolbar = 0;
}

void UIMiniToolBar::enterEvent(QEvent*)
{
    /* Stop the hover-leave timer if necessary: */
    if (m_pHoverLeaveTimer && m_pHoverLeaveTimer->isActive())
        m_pHoverLeaveTimer->stop();

    /* Start the hover-enter timer: */
    if (m_fAutoHide && m_pHoverEnterTimer)
        m_pHoverEnterTimer->start();
}

void UIMiniToolBar::leaveEvent(QEvent*)
{
    /* Stop the hover-enter timer if necessary: */
    if (m_pHoverEnterTimer && m_pHoverEnterTimer->isActive())
        m_pHoverEnterTimer->stop();

    /* Start the hover-leave timer: */
    if (m_fAutoHide && m_pHoverLeaveTimer)
        m_pHoverLeaveTimer->start();
}

void UIMiniToolBar::resizeEvent(QResizeEvent*)
{
    /* Adjust geometry: */
    adjustGeometry();
}

bool UIMiniToolBar::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Detect if we have window activation stolen: */
    if (pWatched == this && pEvent->type() == QEvent::WindowActivate)
    {
#if   defined(Q_WS_WIN)
        emit sigNotifyAboutWindowActivationStolen();
#elif defined(Q_WS_X11)
        switch (nemuGlobal().typeOfWindowManager())
        {
            case X11WMType_GNOMEShell:
            case X11WMType_Mutter:
            {
                // WORKAROUND:
                // Under certain WMs we can receive stolen activation event too early,
                // returning activation to initial source immediately makes no sense.
                // In fact, Qt is not become aware of actual window activation later,
                // so we are going to return window activation in let's say 100ms.
                QTimer::singleShot(100, this, SLOT(sltNotifyAboutWindowActivationStolen()));
                break;
            }
            default:
            {
                emit sigNotifyAboutWindowActivationStolen();
                break;
            }
        }
#endif /* Q_WS_X11 */
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pWatched, pEvent);
}

void UIMiniToolBar::simulateToolbarAutoHiding()
{
    /* This simulation helps user to notice
     * toolbar location, so it will be used only
     * 1. if toolbar unhovered and
     * 2. auto-hide feature enabled: */
    if (m_fHovered || !m_fAutoHide)
        return;

    /* Simulate hover-leave event: */
    m_fHovered = true;
    m_pHoverLeaveTimer->start();
}

void UIMiniToolBar::setToolbarPosition(QPoint point)
{
    /* Update position: */
    AssertPtrReturnVoid(m_pEmbeddedToolbar);
    m_pEmbeddedToolbar->move(point);

#ifdef Q_WS_X11
    /* Update window mask: */
    setMask(m_pEmbeddedToolbar->geometry());
#endif /* Q_WS_X11 */
}

QPoint UIMiniToolBar::toolbarPosition() const
{
    /* Return position: */
    AssertPtrReturn(m_pEmbeddedToolbar, QPoint());
    return m_pEmbeddedToolbar->pos();
}

#include "UIMiniToolBar.moc"

