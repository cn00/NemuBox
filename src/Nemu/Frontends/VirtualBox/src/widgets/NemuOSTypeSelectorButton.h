/* $Id: NemuOSTypeSelectorButton.h $ */
/** @file
 * Nemu Qt GUI - NemuOSTypeSelectorButton class declaration.
 */

/*
 * Copyright (C) 2009-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __NemuOSTypeSelectorButton_h__
#define __NemuOSTypeSelectorButton_h__

/* Nemu includes */
#include "QIWithRetranslateUI.h"

/* Qt includes */
#include <QPushButton>

class QSignalMapper;

class NemuOSTypeSelectorButton: public QIWithRetranslateUI<QPushButton>
{
    Q_OBJECT;

public:
    NemuOSTypeSelectorButton (QWidget *aParent);
    QString osTypeId() const { return mOSTypeId; }

    bool isMenuShown() const;

    void retranslateUi();

public slots:
    void setOSTypeId (const QString& aOSTypeId);

private:
    void populateMenu();

    /* Private member vars */
    QString mOSTypeId;
    QMenu *mMainMenu;
    QSignalMapper *mSignalMapper;
};

#endif /* __NemuOSTypeSelectorButton_h__ */

