/* $Id: NemuFilePathSelectorWidget.h $ */
/** @file
 * Nemu Qt GUI - VirtualBox Qt extensions: NemuFilePathSelectorWidget class declaration.
 */

/*
 * Copyright (C) 2008-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __NemuFilePathSelectorWidget_h__
#define __NemuFilePathSelectorWidget_h__

/* Nemu includes */
#include "QIWithRetranslateUI.h"

/* Qt includes */
#include <QComboBox>

/* Nemu forward declarations */
class QILabel;
class QILineEdit;

/* Qt forward declarations */
class QHBoxLayout;
class QAction;
class QIToolButton;

////////////////////////////////////////////////////////////////////////////////
// NemuFilePathSelectorWidget

class NemuFilePathSelectorWidget: public QIWithRetranslateUI<QComboBox>
{
    Q_OBJECT;

public:

    enum Mode
    {
        Mode_Folder = 0,
        Mode_File_Open,
        Mode_File_Save
    };

    NemuFilePathSelectorWidget (QWidget *aParent);
   ~NemuFilePathSelectorWidget();

    void setMode (Mode aMode);
    Mode mode() const;

    void setEditable (bool aOn);
    bool isEditable() const;

    void setResetEnabled (bool aEnabled);
    bool isResetEnabled () const;

    void setFileDialogTitle (const QString& aTitle);
    QString fileDialogTitle() const;

    void setFileFilters (const QString& aFilters);
    QString fileFilters() const;

    void setDefaultSaveExt (const QString &aExt);
    QString defaultSaveExt() const;

    void resetModified();
    bool isModified() const;
    bool isPathSelected() const;

    QString path() const;

signals:
    void pathChanged (const QString &);

public slots:

    void setPath (const QString &aPath, bool aRefreshText = true);
    void setHomeDir (const QString &aHomeDir);

protected:

    void resizeEvent (QResizeEvent *aEvent);
    void focusInEvent (QFocusEvent *aEvent);
    void focusOutEvent (QFocusEvent *aEvent);
    bool eventFilter (QObject *aObj, QEvent *aEv);
    void retranslateUi();

private slots:

    void onActivated (int aIndex);
    void onTextEdited (const QString &aPath);
    void copyToClipboard();
    void refreshText();

private:

    void changePath (const QString &aPath, bool aRefreshText = true);
    void selectPath();
    QIcon defaultIcon() const;
    QString fullPath (bool aAbsolute = true) const;
    QString shrinkText (int aWidth) const;

    /* Private member vars */
    QAction *mCopyAction;
    Mode mMode;
    QString mPath;
    QString mHomeDir;
    QString mFileFilters;
    QString mDefaultSaveExt;
    QString mFileDialogTitle;
    QString mNoneStr;
    QString mNoneTip;
    bool mIsEditable;
    bool mIsEditableMode;
    bool mIsMouseAwaited;

    bool mModified;
};

////////////////////////////////////////////////////////////////////////////////
// NemuEmptyFileSelector

class NemuEmptyFileSelector: public QIWithRetranslateUI<QWidget>
{
    Q_OBJECT;

public:
    enum ButtonPosition
    {
        LeftPosition,
        RightPosition
    };

    NemuEmptyFileSelector (QWidget *aParent = NULL);

    void setMode (NemuFilePathSelectorWidget::Mode aMode);
    NemuFilePathSelectorWidget::Mode mode() const;

    void setButtonPosition (ButtonPosition aPos);
    ButtonPosition buttonPosition() const;

    void setEditable (bool aOn);
    bool isEditable() const;

    void setChooserVisible (bool aOn);
    bool isChooserVisible() const;

    QString path() const;

    void setDefaultSaveExt (const QString &aExt);
    QString defaultSaveExt() const;

    bool isModified () const { return mIsModified; }
    void resetModified () { mIsModified = false; }

    void setChooseButtonToolTip(const QString &strToolTip);
    QString chooseButtonToolTip() const;

    void setFileDialogTitle (const QString& aTitle);
    QString fileDialogTitle() const;

    void setFileFilters (const QString& aFilters);
    QString fileFilters() const;

    void setHomeDir (const QString& aDir);
    QString homeDir() const;

signals:
    void pathChanged (QString);

public slots:
    void setPath (const QString& aPath);

protected:
    void retranslateUi();

private slots:
    void choose();
    void textChanged (const QString& aPath);

private:
    /* Private member vars */
    QHBoxLayout *mMainLayout;
    QWidget *mPathWgt;
    QILabel *mLabel;
    NemuFilePathSelectorWidget::Mode mMode;
    QILineEdit *mLineEdit;
    QIToolButton *mSelectButton;
    bool m_fButtonToolTipSet;
    QString mFileDialogTitle;
    QString mFileFilters;
    QString mDefaultSaveExt;
    QString mHomeDir;
    bool mIsModified;
    QString mPath;
};

#endif /* __NemuFilePathSelectorWidget_h__ */

