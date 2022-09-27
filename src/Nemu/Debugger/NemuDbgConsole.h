/* $Id: NemuDbgConsole.h $ */
/** @file
 * Nemu Debugger GUI - Console.
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

#ifndef ___Debugger_NemuDbgConsole_h
#define ___Debugger_NemuDbgConsole_h

#include "NemuDbgBase.h"

#include <QTextEdit>
#include <QComboBox>
#include <QTimer>
#include <QEvent>

#include <iprt/critsect.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>


class NemuDbgConsoleOutput : public QTextEdit
{
    Q_OBJECT

public:
    /**
     * Constructor.
     *
     * @param   pParent     Parent Widget.
     * @param   pszName     Widget name.
     */
    NemuDbgConsoleOutput(QWidget *pParent = NULL, const char *pszName = NULL);

    /**
     * Destructor
     */
    virtual ~NemuDbgConsoleOutput();

    /**
     * Appends text.
     * This differs from QTextEdit::append() in that it won't start on a new paragraph
     * unless the previous char was a newline ('\n').
     *
     * @param   rStr        The text string to append.
     * @param   fClearSelection     Whether to clear selected text before appending.
     *                              If @c false the selection and window position
     *                              are preserved.
     */
    virtual void appendText(const QString &rStr, bool fClearSelection);

    /** The action to switch to black-on-white color scheme. */
    QAction *m_pBlackOnWhiteAction;
    /** The action to switch to green-on-black color scheme. */
    QAction *m_pGreenOnBlackAction;

    /** The action to switch to Courier font. */
    QAction *m_pCourierFontAction;
    /** The action to switch to Monospace font. */
    QAction *m_pMonospaceFontAction;

protected:
    typedef enum  { kGreenOnBlack, kBlackOnWhite } NemuDbgConsoleColor;

    /**
     * Context menu event.
     * This adds custom menu items for the output view.
     *
     * @param pEvent   Pointer to the event.
     */
    virtual void contextMenuEvent(QContextMenuEvent *pEvent);

    /** The current line (paragraph) number. */
    unsigned m_uCurLine;
    /** The position in the current line. */
    unsigned m_uCurPos;
    /** The handle to the GUI thread. */
    RTNATIVETHREAD m_hGUIThread;
    /** The current color scheme (foreground on background). */
    NemuDbgConsoleColor m_enmColorScheme;

private slots:
    /**
     * The green-on-black color scheme context-menu item was triggered.
     */
    void        setColorGreenOnBlack();

    /**
     * The black-on-white color scheme context-menu item was triggered.
     */
    void        setColorBlackOnWhite();

    /**
     * The courier font family context-menu item was triggered.
     */
    void        setFontCourier();

    /**
     * The monospace font family context-menu item was triggered.
     */
    void        setFontMonospace();
};


/**
 * The Debugger Console Input widget.
 *
 * This is a combobox which only responds to <return>.
 */
class NemuDbgConsoleInput : public QComboBox
{
    Q_OBJECT

public:
    /**
     * Constructor.
     *
     * @param   pParent     Parent Widget.
     * @param   pszName     Widget name.
     */
    NemuDbgConsoleInput(QWidget *pParent = NULL, const char *pszName = NULL);

    /**
     * Destructor
     */
    virtual ~NemuDbgConsoleInput();

    /**
     * We overload this method to get signaled upon returnPressed().
     *
     * See QComboBox::setLineEdit for full description.
     * @param   pEdit   The new line edit widget.
     * @remark  This won't be called during the constructor.
     */
    virtual void setLineEdit(QLineEdit *pEdit);

signals:
    /**
     * New command submitted.
     */
    void commandSubmitted(const QString &rCommand);

private slots:
    /**
     * Returned was pressed.
     *
     * Will emit commandSubmitted().
     */
    void returnPressed();

protected:
    /** The handle to the GUI thread. */
    RTNATIVETHREAD m_hGUIThread;
};


/**
 * The Debugger Console.
 */
class NemuDbgConsole : public NemuDbgBaseWindow
{
    Q_OBJECT

public:
    /**
     * Constructor.
     *
     * @param   a_pDbgGui       Pointer to the debugger gui object.
     * @param   a_pParent       Parent Widget.
     */
    NemuDbgConsole(NemuDbgGui *a_pDbgGui, QWidget *a_pParent = NULL);

    /**
     * Destructor
     */
    virtual ~NemuDbgConsole();

protected slots:
    /**
     * Handler called when a command is submitted.
     * (Enter or return pressed in the combo box.)
     *
     * @param   rCommand        The submitted command.
     */
    void commandSubmitted(const QString &rCommand);

    /**
     * Updates the output with what's currently in the output buffer.
     * This is called by a timer or a User event posted by the debugger thread.
     */
    void updateOutput();

    /**
     * Changes the focus to the input field.
     */
    void actFocusToInput();

    /**
     * Changes the focus to the output viewer widget.
     */
    void actFocusToOutput();

protected:
    /**
     * Override the closeEvent so we can choose delete the window when
     * it is closed.
     *
     * @param  a_pCloseEvt  The close event.
     */
    virtual void closeEvent(QCloseEvent *a_pCloseEvt);

    /**
     * Lock the object.
     */
    void lock();

    /**
     * Unlocks the object.
     */
    void unlock();

protected:
    /** @name Debug Console Backend.
     * @{
     */


    /**
     * Checks if there is input.
     *
     * @returns true if there is input ready.
     * @returns false if there not input ready.
     * @param   pBack       Pointer to NemuDbgConsole::m_Back.
     * @param   cMillies    Number of milliseconds to wait on input data.
     */
    static DECLCALLBACK(bool) backInput(PDBGCBACK pBack, uint32_t cMillies);

    /**
     * Read input.
     *
     * @returns Nemu status code.
     * @param   pBack       Pointer to NemuDbgConsole::m_Back.
     * @param   pvBuf       Where to put the bytes we read.
     * @param   cbBuf       Maximum nymber of bytes to read.
     * @param   pcbRead     Where to store the number of bytes actually read.
     *                      If NULL the entire buffer must be filled for a
     *                      successful return.
     */
    static DECLCALLBACK(int) backRead(PDBGCBACK pBack, void *pvBuf, size_t cbBuf, size_t *pcbRead);

    /**
     * Write (output).
     *
     * @returns Nemu status code.
     * @param   pBack       Pointer to NemuDbgConsole::m_Back.
     * @param   pvBuf       What to write.
     * @param   cbBuf       Number of bytes to write.
     * @param   pcbWritten  Where to store the number of bytes actually written.
     *                      If NULL the entire buffer must be successfully written.
     */
    static DECLCALLBACK(int) backWrite(PDBGCBACK pBack, const void *pvBuf, size_t cbBuf, size_t *pcbWritten);

    /**
     * @copydoc FNDBGCBACKSETREADY
     */
    static DECLCALLBACK(void) backSetReady(PDBGCBACK pBack, bool fReady);

    /**
     * The Debugger Console Thread
     *
     * @returns Nemu status code (ignored).
     * @param   Thread      The thread handle.
     * @param   pvUser      Pointer to the NemuDbgConsole object.s
     */
    static DECLCALLBACK(int) backThread(RTTHREAD Thread, void *pvUser);

    /** @} */

protected:
    /**
     * Processes GUI command posted by the console thread.
     *
     * Qt3 isn't thread safe on any platform, meaning there is no locking, so, as
     * a result we have to be very careful. All operations on objects which we share
     * with the main thread has to be posted to it so it can perform it.
     */
    bool event(QEvent *pEvent);

protected:
    /** The output widget. */
    NemuDbgConsoleOutput *m_pOutput;
    /** The input widget. */
    NemuDbgConsoleInput *m_pInput;
    /** A hack to restore focus to the combobox after a command execution. */
    bool m_fInputRestoreFocus;
    /** The input buffer. */
    char *m_pszInputBuf;
    /** The amount of input in the buffer. */
    size_t m_cbInputBuf;
    /** The allocated size of the buffer. */
    size_t m_cbInputBufAlloc;

    /** The output buffer. */
    char *m_pszOutputBuf;
    /** The amount of output in the buffer. */
    size_t m_cbOutputBuf;
    /** The allocated size of the buffer. */
    size_t m_cbOutputBufAlloc;
    /** The timer object used to process output in a delayed fashion. */
    QTimer *m_pTimer;
    /** Set when an output update is pending. */
    bool volatile m_fUpdatePending;

    /** The debugger console thread. */
    RTTHREAD m_Thread;
    /** The event semaphore used to signal the debug console thread about input. */
    RTSEMEVENT m_EventSem;
    /** The critical section used to lock the object. */
    RTCRITSECT m_Lock;
    /** When set the thread will cause the debug console thread to terminate. */
    bool volatile m_fTerminate;
    /** Has the thread terminated?
     * Used to do the right thing in closeEvent; the console is dead if the
     * thread has terminated. */
    bool volatile m_fThreadTerminated;

    /** The debug console backend structure.
     * Use NEMUDBGCONSOLE_FROM_DBGCBACK to convert the DBGCBACK pointer to a object pointer. */
    struct NemuDbgConsoleBack
    {
        DBGCBACK Core;
        NemuDbgConsole *pSelf;
    } m_Back;

    /**
     * Converts a pointer to NemuDbgConsole::m_Back to NemuDbgConsole pointer.
     * @todo find a better way because offsetof is undefined on objects and g++ gets very noisy because of that.
     */
#   define NEMUDBGCONSOLE_FROM_DBGCBACK(pBack) ( ((struct NemuDbgConsoleBack *)(pBack))->pSelf )

    /** Change focus to the input field. */
    QAction *m_pFocusToInput;
    /** Change focus to the output viewer widget. */
    QAction *m_pFocusToOutput;
};


/**
 * Simple event class for push certain operations over
 * onto the GUI thread.
 */
class NemuDbgConsoleEvent : public QEvent
{
public:
    typedef enum  { kUpdate, kInputEnable, kTerminatedUser, kTerminatedOther } NemuDbgConsoleEventType;
    enum { kEventNumber = QEvent::User + 42 };

    NemuDbgConsoleEvent(NemuDbgConsoleEventType enmCommand)
        : QEvent((QEvent::Type)kEventNumber), m_enmCommand(enmCommand)
    {
    }

    NemuDbgConsoleEventType command() const
    {
        return m_enmCommand;
    }

private:
    NemuDbgConsoleEventType m_enmCommand;
};


#endif

