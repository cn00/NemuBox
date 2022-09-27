/* $Id: UIMenuBar.cpp $ */
/** @file
 * Nemu Qt GUI - UIMenuBar class implementation.
 */

/*
 * Copyright (C) 2010-2014 Oracle Corporation
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
# include <QPainter>
# include <QPaintEvent>
# include <QPixmapCache>

/* GUI includes: */
# include "UIMenuBar.h"
# include "UIImageTools.h"
# include "NemuGlobal.h"

#endif /* !NEMU_WITH_PRECOMPILED_HEADERS */


UIMenuBar::UIMenuBar(QWidget *pParent /* = 0 */)
    : QMenuBar(pParent)
    , m_fShowBetaLabel(false)
{
    /* Check for beta versions: */
    if (nemuGlobal().isBeta())
        m_fShowBetaLabel = true;
}

void UIMenuBar::paintEvent(QPaintEvent *pEvent)
{
    QMenuBar::paintEvent(pEvent);
    if (m_fShowBetaLabel)
    {
        QPixmap betaLabel;
        const QString key("nemu:betaLabel");
        if (!QPixmapCache::find(key, betaLabel))
        {
            betaLabel = ::betaLabel();
            QPixmapCache::insert(key, betaLabel);
        }
        QSize s = size();
        QPainter painter(this);
        painter.setClipRect(pEvent->rect());
        painter.drawPixmap(s.width() - betaLabel.width() - 10, (height() - betaLabel.height()) / 2, betaLabel);
    }
}

