/* $Id: NemuLicenseViewer.cpp $ */
/** @file
 * Nemu Qt GUI - NemuLicenseViewer class implementation.
 */

/*
 * Copyright (C) 2006-2011 Oracle Corporation
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

# include "NemuLicenseViewer.h"
# include "QIDialogButtonBox.h"
# include "UIMessageCenter.h"

/* Qt includes */
# include <QTextBrowser>
# include <QPushButton>
# include <QVBoxLayout>
# include <QScrollBar>
# include <QFile>

#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */


NemuLicenseViewer::NemuLicenseViewer(QWidget *pParent /* = 0 */)
    : QIWithRetranslateUI2<QDialog>(pParent)
    , mLicenseText (0)
    , mAgreeButton (0)
    , mDisagreeButton (0)
{
#ifndef Q_WS_WIN
    /* Application icon. On Win32, it's built-in to the executable. */
    setWindowIcon (QIcon (":/VirtualBox_48px.png"));
#endif

    mLicenseText = new QTextBrowser (this);
    mAgreeButton = new QPushButton (this);
    mDisagreeButton = new QPushButton (this);
    QDialogButtonBox *dbb = new QIDialogButtonBox (this);
    dbb->addButton (mAgreeButton, QDialogButtonBox::AcceptRole);
    dbb->addButton (mDisagreeButton, QDialogButtonBox::RejectRole);

    connect (mLicenseText->verticalScrollBar(), SIGNAL (valueChanged (int)),
             SLOT (onScrollBarMoving (int)));
    connect (mAgreeButton, SIGNAL (clicked()), SLOT (accept()));
    connect (mDisagreeButton, SIGNAL (clicked()), SLOT (reject()));

    QVBoxLayout *mainLayout = new QVBoxLayout (this);
    mainLayout->setSpacing (10);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->addWidget (mLicenseText);
    mainLayout->addWidget (dbb);

    mLicenseText->verticalScrollBar()->installEventFilter (this);

    resize (600, 450);

    retranslateUi();
}

int NemuLicenseViewer::showLicenseFromFile(const QString &strLicenseFileName)
{
    /* Read license file: */
    QFile file(strLicenseFileName);
    if (file.open(QIODevice::ReadOnly))
    {
        return showLicenseFromString(file.readAll());
    }
    else
    {
        msgCenter().cannotOpenLicenseFile(strLicenseFileName, this);
        return QDialog::Rejected;
    }
}

int NemuLicenseViewer::showLicenseFromString(const QString &strLicenseText)
{
    /* Set license text: */
    mLicenseText->setText(strLicenseText);
    return exec();
}

void NemuLicenseViewer::retranslateUi()
{
    setWindowTitle (tr ("VirtualBox License"));

    mAgreeButton->setText (tr ("I &Agree"));
    mDisagreeButton->setText (tr ("I &Disagree"));
}

int NemuLicenseViewer::exec()
{
    return QDialog::exec();
}

void NemuLicenseViewer::onScrollBarMoving (int aValue)
{
    if (aValue == mLicenseText->verticalScrollBar()->maximum())
        unlockButtons();
}

void NemuLicenseViewer::unlockButtons()
{
    mAgreeButton->setEnabled (true);
    mDisagreeButton->setEnabled (true);
}

void NemuLicenseViewer::showEvent (QShowEvent *aEvent)
{
    QDialog::showEvent (aEvent);
    bool isScrollBarHidden = !mLicenseText->verticalScrollBar()->isVisible()
        && !(windowState() & Qt::WindowMinimized);
    mAgreeButton->setEnabled (isScrollBarHidden);
    mDisagreeButton->setEnabled (isScrollBarHidden);
}

bool NemuLicenseViewer::eventFilter (QObject *aObject, QEvent *aEvent)
{
    switch (aEvent->type())
    {
        case QEvent::Hide:
            if (aObject == mLicenseText->verticalScrollBar())
                /* Doesn't work on wm's like ion3 where the window starts
                 * maximized: isActiveWindow() */
                unlockButtons();
        default:
            break;
    }
    return QDialog::eventFilter (aObject, aEvent);
}

