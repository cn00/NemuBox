/* $Id: UIDesktopWidgetWatchdog.h $ */
/** @file
 * Nemu Qt GUI - UIDesktopWidgetWatchdog class declaration.
 */

/*
 * Copyright (C) 2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___UIDesktopWidgetWatchdog_h___
#define ___UIDesktopWidgetWatchdog_h___

/* Qt includes: */
#include <QObject>
#include <QVector>
#include <QRect>

/* Forward declarations: */
class QDesktopWidget;

/** QObject extension used as
  * a desktop-widget watchdog aware
  * of the host-screen geometry changes. */
class UIDesktopWidgetWatchdog : public QObject
{
    Q_OBJECT;

public:

    /** Constructs watchdog for the @a pParent being passed into the base-class. */
    UIDesktopWidgetWatchdog(QObject *pParent);
    /** Destructs watchdog. */
    ~UIDesktopWidgetWatchdog();

    /** Returns the geometry of the host-screen with @a iHostScreenIndex.
      * @note The default screen is used if @a iHostScreenIndex is -1. */
    const QRect	screenGeometry(int iHostScreenIndex = -1) const;
    /** Returns the available-geometry of the host-screen with @a iHostScreenIndex.
      * @note The default screen is used if @a iHostScreenIndex is -1. */
    const QRect availableGeometry(int iHostScreenIndex = -1) const;

private slots:

    /** Updates host-screen configuration according to new @a cHostScreenCount.
      * @note cHostScreenCount can be equal to -1 which means we have to acquire it ourselves. */
    void sltUpdateHostScreenConfiguration(int cHostScreenCount = -1);

    /** Recalculates available-geometry for the host-screen with @a iHostScreenIndex. */
    void sltRecalculateHostScreenAvailableGeometry(int iHostScreenIndex);

    /** Handles @a availableGeometry calculation result for the host-screen with @a iHostScreenIndex. */
    void sltHandleHostScreenAvailableGeometryCalculated(int iHostScreenIndex, QRect availableGeometry);

private:

    /** Prepare routine. */
    void prepare();
    /** Cleanup routine. */
    void cleanup();

    /** Holds the desktop-widget reference pointer. */
    QDesktopWidget *m_pDesktopWidget;

    /** Holds current host-screen count. */
    int m_cHostScreenCount;
    /** Holds current host-screen available-geometries. */
    QVector<QRect> m_availableGeometryData;
    /** Holds current workers determining host-screen available-geometries. */
    QVector<QWidget*> m_availableGeometryWorkers;
};

#endif /* !___UIDesktopWidgetWatchdog_h___ */

