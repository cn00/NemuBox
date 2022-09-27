/* $Id: NemuDbgBase.h $ */
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


#ifndef ___Debugger_NemuDbgBase_h
#define ___Debugger_NemuDbgBase_h


#include <Nemu/vmm/stam.h>
#include <Nemu/vmm/vmapi.h>
#include <Nemu/dbg.h>
#include <iprt/thread.h>
#include <QString>
#include <QWidget>

class NemuDbgGui;


/**
 * Nemu Debugger GUI Base Class.
 *
 * The purpose of this class is to hide the VM handle, abstract VM
 * operations, and finally to make sure the GUI won't crash when
 * the VM dies.
 */
class NemuDbgBase
{
public:
    /**
     * Construct the object.
     *
     * @param   pDbgGui     Pointer to the debugger gui object.
     */
    NemuDbgBase(NemuDbgGui *a_pDbgGui);

    /**
     * Destructor.
     */
    virtual ~NemuDbgBase();


    /**
     * Checks if the VM is OK for normal operations.
     * @returns true if ok, false if not.
     */
    bool isVMOk() const
    {
        return m_pUVM != NULL;
    }

    /**
     * Checks if the current thread is the GUI thread or not.
     * @return true/false accordingly.
     */
    bool isGUIThread() const
    {
        return m_hGUIThread == RTThreadNativeSelf();
    }

    /** @name Operations
     * @{ */
    /**
     * Wrapper for STAMR3Reset().
     */
    int stamReset(const QString &rPat);
    /**
     * Wrapper for STAMR3Enum().
     */
    int stamEnum(const QString &rPat, PFNSTAMR3ENUM pfnEnum, void *pvUser);
    /**
     * Wrapper for DBGCCreate().
     */
    int dbgcCreate(PDBGCBACK pBack, unsigned fFlags);
    /** @} */


protected:
    /** @name Signals
     * @{ */
    /**
     * Called when the VM is being destroyed.
     */
    virtual void sigDestroying();
    /**
     * Called when the VM has been terminated.
     */
    virtual void sigTerminated();
    /** @} */


private:
    /** @callback_method_impl{FNVMATSTATE}  */
    static DECLCALLBACK(void) atStateChange(PUVM pUVM, VMSTATE enmState, VMSTATE enmOldState, void *pvUser);

private:
    /** Pointer to the debugger GUI object. */
    NemuDbgGui *m_pDbgGui;
    /** The user mode VM handle. */
    PUVM volatile m_pUVM;
    /** The handle of the GUI thread. */
    RTNATIVETHREAD m_hGUIThread;
};


/**
 * Nemu Debugger GUI Base Window Class.
 *
 * This is just a combination of QWidget and NemuDbgBase with some additional
 * functionality for window management. This class is not intended for control
 * widgets, only normal top-level windows.
 */
class NemuDbgBaseWindow : public QWidget, public NemuDbgBase
{
public:
    /**
     * Construct the object.
     *
     * @param   pDbgGui     Pointer to the debugger gui object.
     */
    NemuDbgBaseWindow(NemuDbgGui *a_pDbgGui, QWidget *a_pParent);

    /**
     * Destructor.
     */
    virtual ~NemuDbgBaseWindow();

    /**
     * Shows the window and gives it focus.
     */
    void vShow();

    /**
     * Repositions the window, taking the frame decoration into account.
     *
     * @param   a_x         The new x coordinate.
     * @param   a_y         The new x coordinate.
     * @param   a_cx        The total width.
     * @param   a_cy        The total height.
     * @param   a_fResize   Whether to resize it as well.
     */
    void vReposition(int a_x, int a_y, unsigned a_cx, unsigned a_cy, bool a_fResize);

protected:
    /**
     * For polishing the window size (X11 mess).
     *
     * @returns true / false.
     * @param   a_pEvt       The event.
     */
    virtual bool event(QEvent *a_pEvt);

    /**
     * Internal worker for polishing the size and position (X11 hacks).
     */
    void vPolishSizeAndPos();

    /**
     * Internal worker that guesses the border sizes.
     */
    QSize vGuessBorderSizes();


private:
    /** Whether we've done the size polishing in showEvent or not. */
    bool m_fPolished;
    /** The desired x coordinate. */
    int m_x;
    /** The desired y coordinate. */
    int m_y;
    /** The desired width. */
    unsigned m_cx;
    /** The desired height. */
    unsigned m_cy;

    /** Best effort x border size (for X11). */
    static unsigned m_cxBorder;
    /** Best effort y border size (for X11). */
    static unsigned m_cyBorder;
};

#endif

