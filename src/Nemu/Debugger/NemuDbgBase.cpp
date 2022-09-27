/* $Id: NemuDbgBase.cpp $ */
/** @file
 * Nemu Debugger GUI - Base classes.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGG
#include <Nemu/err.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <limits.h>
#include "NemuDbgBase.h"
#include "NemuDbgGui.h"

#include <QApplication>
#include <QWidgetList>



NemuDbgBase::NemuDbgBase(NemuDbgGui *a_pDbgGui)
    : m_pDbgGui(a_pDbgGui), m_pUVM(NULL), m_hGUIThread(RTThreadNativeSelf())
{
    /*
     * Register
     */
    m_pUVM = a_pDbgGui->getUvmHandle();
    if (m_pUVM)
    {
        VMR3RetainUVM(m_pUVM);

        int rc = VMR3AtStateRegister(m_pUVM, atStateChange, this);
        AssertRC(rc);
    }
}


NemuDbgBase::~NemuDbgBase()
{
    /*
     * If the VM is still around.
     */
    /** @todo need to do some locking here?  */
    PUVM pUVM = ASMAtomicXchgPtrT(&m_pUVM, NULL, PUVM);
    if (pUVM)
    {
        int rc = VMR3AtStateDeregister(pUVM, atStateChange, this);
        AssertRC(rc);

        VMR3ReleaseUVM(pUVM);
    }
}


int
NemuDbgBase::stamReset(const QString &rPat)
{
    QByteArray Utf8Array = rPat.toUtf8();
    const char *pszPat = !rPat.isEmpty() ? Utf8Array.constData() : NULL;
    PUVM pUVM = m_pUVM;
    if (    pUVM
        &&  VMR3GetStateU(pUVM) < VMSTATE_DESTROYING)
        return STAMR3Reset(pUVM, pszPat);
    return VERR_INVALID_HANDLE;
}


int
NemuDbgBase::stamEnum(const QString &rPat, PFNSTAMR3ENUM pfnEnum, void *pvUser)
{
    QByteArray Utf8Array = rPat.toUtf8();
    const char *pszPat = !rPat.isEmpty() ? Utf8Array.constData() : NULL;
    PUVM pUVM = m_pUVM;
    if (    pUVM
        &&  VMR3GetStateU(pUVM) < VMSTATE_DESTROYING)
        return STAMR3Enum(pUVM, pszPat, pfnEnum, pvUser);
    return VERR_INVALID_HANDLE;
}


int
NemuDbgBase::dbgcCreate(PDBGCBACK pBack, unsigned fFlags)
{
    PUVM pUVM = m_pUVM;
    if (    pUVM
        &&  VMR3GetStateU(pUVM) < VMSTATE_DESTROYING)
        return DBGCCreate(pUVM, pBack, fFlags);
    return VERR_INVALID_HANDLE;
}


/*static*/ DECLCALLBACK(void)
NemuDbgBase::atStateChange(PUVM pUVM, VMSTATE enmState, VMSTATE /*enmOldState*/, void *pvUser)
{
    NemuDbgBase *pThis = (NemuDbgBase *)pvUser; NOREF(pUVM);
    switch (enmState)
    {
        case VMSTATE_TERMINATED:
        {
            /** @todo need to do some locking here?  */
            PUVM pUVM2 = ASMAtomicXchgPtrT(&pThis->m_pUVM, NULL, PUVM);
            if (pUVM2)
            {
                Assert(pUVM2 == pUVM);
                pThis->sigTerminated();
                VMR3ReleaseUVM(pUVM2);
            }
            break;
        }

        case VMSTATE_DESTROYING:
            pThis->sigDestroying();
            break;

        default:
            break;
    }
}


void
NemuDbgBase::sigDestroying()
{
}


void
NemuDbgBase::sigTerminated()
{
}




//
//
//
//  V B o x D b g B a s e W i n d o w
//  V B o x D b g B a s e W i n d o w
//  V B o x D b g B a s e W i n d o w
//
//
//

unsigned NemuDbgBaseWindow::m_cxBorder = 0;
unsigned NemuDbgBaseWindow::m_cyBorder = 0;


NemuDbgBaseWindow::NemuDbgBaseWindow(NemuDbgGui *a_pDbgGui, QWidget *a_pParent)
    : QWidget(a_pParent, Qt::Window), NemuDbgBase(a_pDbgGui), m_fPolished(false),
    m_x(INT_MAX), m_y(INT_MAX), m_cx(0), m_cy(0)
{
}


NemuDbgBaseWindow::~NemuDbgBaseWindow()
{

}


void
NemuDbgBaseWindow::vShow()
{
    show();
    /** @todo this ain't working right. HELP! */
    setWindowState(windowState() & ~Qt::WindowMinimized);
    //activateWindow();
    //setFocus();
    vPolishSizeAndPos();
}


void
NemuDbgBaseWindow::vReposition(int a_x, int a_y, unsigned a_cx, unsigned a_cy, bool a_fResize)
{
    if (a_fResize)
    {
        m_cx = a_cx;
        m_cy = a_cy;

        QSize BorderSize = frameSize() - size();
        if (BorderSize == QSize(0,0))
            BorderSize = vGuessBorderSizes();

        resize(a_cx - BorderSize.width(), a_cy - BorderSize.height());
    }

    m_x = a_x;
    m_y = a_y;
    move(a_x, a_y);
}


bool
NemuDbgBaseWindow::event(QEvent *a_pEvt)
{
    bool fRc = QWidget::event(a_pEvt);
    vPolishSizeAndPos();
    return fRc;
}


void
NemuDbgBaseWindow::vPolishSizeAndPos()
{
    /* Ignore if already done or no size set. */
    if (    m_fPolished
        || (m_x == INT_MAX && m_y == INT_MAX))
        return;

    QSize BorderSize = frameSize() - size();
    if (BorderSize != QSize(0,0))
        m_fPolished = true;

    vReposition(m_x, m_y, m_cx, m_cy, m_cx || m_cy);
}


QSize
NemuDbgBaseWindow::vGuessBorderSizes()
{
#ifdef Q_WS_X11 /* (from the qt gui) */
    /* only once. */
    if (!m_cxBorder && !m_cyBorder)
    {

        /* On X11, there is no way to determine frame geometry (including WM
         * decorations) before the widget is shown for the first time. Stupidly
         * enumerate other top level widgets to find the thickest frame. The code
         * is based on the idea taken from QDialog::adjustPositionInternal(). */

        int extraw = 0, extrah = 0;

        QWidgetList list = QApplication::topLevelWidgets();
        QListIterator<QWidget*> it (list);
        while ((extraw == 0 || extrah == 0) && it.hasNext())
        {
            int framew, frameh;
            QWidget *current = it.next();
            if (!current->isVisible())
                continue;

            framew = current->frameGeometry().width() - current->width();
            frameh = current->frameGeometry().height() - current->height();

            extraw = qMax (extraw, framew);
            extrah = qMax (extrah, frameh);
        }

        if (extraw || extrah)
        {
            m_cxBorder = extraw;
            m_cyBorder = extrah;
        }
    }
#endif /* X11 */
    return QSize(m_cxBorder, m_cyBorder);
}

