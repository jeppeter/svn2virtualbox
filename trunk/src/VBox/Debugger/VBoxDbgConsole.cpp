/* $Id$ */
/** @file
 * VBox Debugger GUI - Console.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "VBoxDbgConsole.h"

#ifdef VBOXDBG_USE_QT4
# include <QLabel>
# include <QApplication>
# include <QFont>
# include <QLineEdit>
# include <QHBoxLayout>
#else
# include <qlabel.h>
# include <qapplication.h>
# include <qfont.h>
# include <qtextview.h>
# include <qlineedit.h>
#endif 

#include <VBox/dbg.h>
#include <VBox/cfgm.h>
#include <VBox/err.h>

#include <iprt/thread.h>
#include <iprt/tcp.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/alloc.h>
#include <iprt/string.h>




/*
 *
 *          V B o x D b g C o n s o l e O u t p u t
 *          V B o x D b g C o n s o l e O u t p u t
 *          V B o x D b g C o n s o l e O u t p u t
 *
 *
 */


VBoxDbgConsoleOutput::VBoxDbgConsoleOutput(QWidget *pParent/* = NULL*/, const char *pszName/* = NULL*/)
#ifdef VBOXDBG_USE_QT4
    : QTextEdit(pParent), 
#else
    : QTextEdit(pParent, pszName), 
#endif
      m_uCurLine(0), m_uCurPos(0), m_hGUIThread(RTThreadNativeSelf())
{
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setOverwriteMode(true);
#ifdef VBOXDBG_USE_QT4
    setPlainText("");
#else
    setTextFormat(PlainText);   /* minimal HTML: setTextFormat(LogText); */
#endif

#ifdef Q_WS_MAC
    QFont Font("Monaco", 10, QFont::Normal, FALSE);
    Font.setStyleStrategy(QFont::NoAntialias);
#else
    QFont Font = font();
    Font.setStyleHint(QFont::TypeWriter);
    Font.setFamily("Courier [Monotype]");
#endif
    setFont(Font);

    /* green on black */
#ifdef VBOXDBG_USE_QT4
    QPalette Pal(palette());
    Pal.setColor(QPalette::Active, QPalette::Base, QColor(Qt::black));
    setPalette(Pal);
    setTextColor(QColor(qRgb(0, 0xe0, 0)));
#else
    setPaper(QBrush(Qt::black));
    setColor(QColor(qRgb(0, 0xe0, 0)));
#endif 
    NOREF(pszName);
}

VBoxDbgConsoleOutput::~VBoxDbgConsoleOutput()
{
    Assert(m_hGUIThread == RTThreadNativeSelf());
}

void VBoxDbgConsoleOutput::appendText(const QString &rStr)
{
    Assert(m_hGUIThread == RTThreadNativeSelf());

    if (rStr.isEmpty() || rStr.isNull() || !rStr.length())
        return;

    /*
     * Insert line by line.
     */
    unsigned cch = rStr.length();
    unsigned iPos = 0;
    while (iPos < cch)
    {
#ifdef VBOXDBG_USE_QT4
        int iPosNL = rStr.indexOf('\n', iPos);
#else
        int iPosNL = rStr.find('\n', iPos);
#endif 
        int iPosEnd = iPosNL >= 0 ? iPosNL : cch;
        if ((unsigned)iPosNL != iPos)
        {
            QString Str = rStr.mid(iPos, iPosEnd - iPos);
            if (m_uCurPos == 0)
                append(Str);
            else
#ifdef VBOXDBG_USE_QT4
                insertPlainText(Str);
#else
                insertAt(Str, m_uCurLine, m_uCurPos);
#endif 
            if (iPosNL >= 0)
            {
                m_uCurLine++;
                m_uCurPos = 0;
            }
            else
                m_uCurPos += Str.length();
        }
        else
        {
            m_uCurLine++;
            m_uCurPos = 0;
        }
        /* next */
        iPos = iPosEnd + 1;
    }
}




/*
 *
 *      V B o x D b g C o n s o l e I n p u t
 *      V B o x D b g C o n s o l e I n p u t
 *      V B o x D b g C o n s o l e I n p u t
 *
 *
 */


VBoxDbgConsoleInput::VBoxDbgConsoleInput(QWidget *pParent/* = NULL*/, const char *pszName/* = NULL*/)
#ifdef VBOXDBG_USE_QT4
    : QComboBox(pParent), 
#else
    : QComboBox(true, pParent, pszName), 
#endif 
      m_iBlankItem(0), m_hGUIThread(RTThreadNativeSelf())
{
#ifdef VBOXDBG_USE_QT4
    insertItem(m_iBlankItem, "");
    setEditable(true);
    setInsertPolicy(NoInsert);
#else
    insertItem("", m_iBlankItem);
    setInsertionPolicy(NoInsertion);
#endif 
    setMaxCount(50);
    const QLineEdit *pEdit = lineEdit();
    if (pEdit)
        connect(pEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));

    NOREF(pszName);
}

VBoxDbgConsoleInput::~VBoxDbgConsoleInput()
{
    Assert(m_hGUIThread == RTThreadNativeSelf());
}

void VBoxDbgConsoleInput::setLineEdit(QLineEdit *pEdit)
{
    Assert(m_hGUIThread == RTThreadNativeSelf());
    QComboBox::setLineEdit(pEdit);
    if (lineEdit() == pEdit && pEdit)
        connect(pEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
}

void VBoxDbgConsoleInput::returnPressed()
{
    Assert(m_hGUIThread == RTThreadNativeSelf());
    /* deal with the current command. */
    QString Str = currentText();
    emit commandSubmitted(Str);

    /* update the history and clear the entry field */
#ifdef VBOXDBG_USE_QT4
    if (itemText(m_iBlankItem - 1) != Str)
    {
        setItemText(m_iBlankItem, Str);
        removeItem(m_iBlankItem - maxCount() - 1);
        insertItem(++m_iBlankItem, "");
    }

    clearEditText();
    setCurrentIndex(m_iBlankItem);
#else
    if (text(m_iBlankItem - 1) != Str)
    {
        changeItem(Str, m_iBlankItem);
        removeItem(m_iBlankItem - maxCount() - 1);
        insertItem("", ++m_iBlankItem);
    }

    clearEdit();
    setCurrentItem(m_iBlankItem);
#endif 
}






/*
 *
 *      V B o x D b g C o n s o l e
 *      V B o x D b g C o n s o l e
 *      V B o x D b g C o n s o l e
 *
 *
 */


VBoxDbgConsole::VBoxDbgConsole(PVM pVM, QWidget *pParent/* = NULL*/, const char *pszName/* = NULL*/)
    : VBoxDbgBase(pVM), m_pOutput(NULL), m_pInput(NULL),
    m_pszInputBuf(NULL), m_cbInputBuf(0), m_cbInputBufAlloc(0),
    m_pszOutputBuf(NULL), m_cbOutputBuf(0), m_cbOutputBufAlloc(0),
    m_pTimer(NULL), m_fUpdatePending(false), m_Thread(NIL_RTTHREAD), m_EventSem(NIL_RTSEMEVENT), m_fTerminate(false)
{
#ifdef VBOXDBG_USE_QT4
    setWindowTitle("VBoxDbg - Console");
#else
    setCaption("VBoxDbg - Console");
#endif

    NOREF(pszName);
    NOREF(pParent);

    /*
     * Create the output text box.
     */
    m_pOutput = new VBoxDbgConsoleOutput(this);

    /* try figure a suitable size */
#ifdef VBOXDBG_USE_QT4
    QLabel *pLabel = new QLabel(      "11111111111111111111111111111111111111111111111111111111111111111111111111111112222222222", this);
#else
    QLabel *pLabel = new QLabel(NULL, "11111111111111111111111111111111111111111111111111111111111111111111111111111112222222222", this); /// @todo
#endif 
    pLabel->setFont(m_pOutput->font());
    QSize Size = pLabel->sizeHint();
    delete pLabel;
    Size.setWidth((int)(Size.width() * 1.10));
    Size.setHeight(Size.width() / 2);
    resize(Size);

    /*
     * Create the input combo box (with a label).
     */
#ifdef VBOXDBG_USE_QT4
    QHBoxLayout *pLayout = new QHBoxLayout();
    //pLayout->setSizeConstraint(QLayout::SetMaximumSize);

    pLabel = new QLabel(" Command ");
    pLayout->addWidget(pLabel);
    pLabel->setMaximumSize(pLabel->sizeHint());
    pLabel->setAlignment(Qt::AlignCenter);

    m_pInput = new VBoxDbgConsoleInput(NULL);
    pLayout->addWidget(m_pInput);
    m_pInput->setDuplicatesEnabled(false);
    connect(m_pInput, SIGNAL(commandSubmitted(const QString &)), this, SLOT(commandSubmitted(const QString &)));

# if 0//def Q_WS_MAC
    pLabel = new QLabel("  "); 
    pLayout->addWidget(pLabel);
    pLabel->setMaximumSize(20, m_pInput->sizeHint().height() + 6);
    pLabel->setMinimumSize(20, m_pInput->sizeHint().height() + 6);
# endif

    QWidget *pHBox = new QWidget(this);
    pHBox->setLayout(pLayout);

#else  /* QT3 */
    QHBox *pHBox = new QHBox(this);

    pLabel = new QLabel(NULL, " Command ", pHBox);
    pLabel->setMaximumSize(pLabel->sizeHint());
    pLabel->setAlignment(AlignHCenter | AlignVCenter);

    m_pInput = new VBoxDbgConsoleInput(pHBox);
    m_pInput->setDuplicatesEnabled(false);
    connect(m_pInput, SIGNAL(commandSubmitted(const QString &)), this, SLOT(commandSubmitted(const QString &)));

# ifdef Q_WS_MAC
    pLabel = new QLabel(NULL, "  ", pHBox); /// @todo
    pLabel->setMaximumSize(20, m_pInput->sizeHint().height() + 6);
    pLabel->setMinimumSize(20, m_pInput->sizeHint().height() + 6);
# endif
#endif /* QT3 */

#ifdef VBOXDBG_USE_QT4
    /*
     * Vertical layout box on the whole widget.
     */
    QVBoxLayout *pVLayout = new QVBoxLayout;
    pVLayout->setSpacing(5);
    pVLayout->setContentsMargins(0, 0, 0, 0);
    pVLayout->addWidget(m_pOutput);
    pVLayout->addWidget(pHBox);
    setLayout(pVLayout);
#endif 

    /*
     * The tab order is from input to output, not the otherway around as it is by default.
     */
    setTabOrder(m_pInput, m_pOutput);

    /*
     * Setup the timer.
     */
    m_pTimer = new QTimer(this);
    connect(m_pTimer, SIGNAL(timeout()), SLOT(updateOutput()));

    /*
     * Init the backend structure.
     */
    m_Back.Core.pfnInput = backInput;
    m_Back.Core.pfnRead  = backRead;
    m_Back.Core.pfnWrite = backWrite;
    m_Back.pSelf = this;

    /*
     * Create the critical section, the event semaphore and the debug console thread.
     */
    int rc = RTCritSectInit(&m_Lock);
    AssertRC(rc);

    rc = RTSemEventCreate(&m_EventSem);
    AssertRC(rc);

    rc = RTThreadCreate(&m_Thread, backThread, this, 0, RTTHREADTYPE_DEBUGGER, RTTHREADFLAGS_WAITABLE, "VBoxDbgC");
    AssertRC(rc);
    if (VBOX_FAILURE(rc))
        m_Thread = NIL_RTTHREAD;
}

VBoxDbgConsole::~VBoxDbgConsole()
{
    Assert(isGUIThread());

    /*
     * Wait for the thread.
     */
    ASMAtomicXchgSize(&m_fTerminate, true);
    RTSemEventSignal(m_EventSem);
    if (m_Thread != NIL_RTTHREAD)
    {
        int rc = RTThreadWait(m_Thread, 15000, NULL);
        AssertRC(rc);
        m_Thread = NIL_RTTHREAD;
    }

    /*
     * Free resources.
     */
    delete m_pTimer;
    m_pTimer = NULL;
    RTCritSectDelete(&m_Lock);
    RTSemEventDestroy(m_EventSem);
    m_EventSem = 0;
    m_pOutput = NULL;
    m_pInput = NULL;
    if (m_pszInputBuf)
    {
        RTMemFree(m_pszInputBuf);
        m_pszInputBuf = NULL;
    }
    m_cbInputBuf = 0;
    m_cbInputBufAlloc = 0;
}

void VBoxDbgConsole::commandSubmitted(const QString &rCommand)
{
    Assert(isGUIThread());

    lock();
    RTSemEventSignal(m_EventSem);

#ifdef VBOXDBG_USE_QT4
    QByteArray Utf8Array = rCommand.toUtf8();
    const char *psz = Utf8Array.constData();
#else                               
    const char *psz = rCommand;//.utf8();
#endif
    size_t cb = strlen(psz);

    /*
     * Make sure we've got space for the input.
     */
    if (cb + m_cbInputBuf >= m_cbInputBufAlloc)
    {
        size_t cbNew = RT_ALIGN_Z(cb + m_cbInputBufAlloc + 1, 128);
        void *pv = RTMemRealloc(m_pszInputBuf, cbNew);
        if (!pv)
        {
            unlock();
            return;
        }
        m_pszInputBuf = (char *)pv;
        m_cbInputBufAlloc = cbNew;
    }

    /*
     * Add the input and output it.
     */
    memcpy(m_pszInputBuf + m_cbInputBuf, psz, cb);
    m_cbInputBuf += cb;
    m_pszInputBuf[m_cbInputBuf++] = '\n';

    m_pOutput->appendText(rCommand + "\n");
#ifdef VBOXDBG_USE_QT4
    m_pOutput->ensureCursorVisible();
#else
    m_pOutput->scrollToBottom();
#endif 

    m_fInputRestoreFocus = m_pInput->hasFocus();    /* dirty focus hack */
    m_pInput->setEnabled(false);

    unlock();
}

void VBoxDbgConsole::updateOutput()
{
    Assert(isGUIThread());

    lock();
    m_fUpdatePending = false;
    if (m_cbOutputBuf)
    {
        m_pOutput->appendText(QString::fromUtf8((const char *)m_pszOutputBuf, m_cbOutputBuf));
        m_cbOutputBuf = 0;
    }
    unlock();
}


/**
 * Lock the object.
 */
void VBoxDbgConsole::lock()
{
    RTCritSectEnter(&m_Lock);
}

/**
 * Unlocks the object.
 */
void VBoxDbgConsole::unlock()
{
    RTCritSectLeave(&m_Lock);
}



/**
 * Checks if there is input.
 *
 * @returns true if there is input ready.
 * @returns false if there not input ready.
 * @param   pBack       Pointer to VBoxDbgConsole::m_Back.
 * @param   cMillies    Number of milliseconds to wait on input data.
 */
/*static*/ DECLCALLBACK(bool) VBoxDbgConsole::backInput(PDBGCBACK pBack, uint32_t cMillies)
{
    VBoxDbgConsole *pThis = VBOXDBGCONSOLE_FROM_DBGCBACK(pBack);
    pThis->lock();

    /* questing for input means it's done processing. */
    pThis->m_pInput->setEnabled(true);
    /* dirty focus hack: */
    if (pThis->m_fInputRestoreFocus)
        QApplication::postEvent(pThis, new VBoxDbgConsoleEvent(VBoxDbgConsoleEvent::kInputRestoreFocus));

    bool fRc = true;
    if (!pThis->m_cbInputBuf)
    {
        pThis->unlock();
        RTSemEventWait(pThis->m_EventSem, cMillies);
        pThis->lock();
        fRc = pThis->m_cbInputBuf || pThis->m_fTerminate;
    }

    pThis->unlock();
    return fRc;
}

/**
 * Read input.
 *
 * @returns VBox status code.
 * @param   pBack       Pointer to VBoxDbgConsole::m_Back.
 * @param   pvBuf       Where to put the bytes we read.
 * @param   cbBuf       Maximum nymber of bytes to read.
 * @param   pcbRead     Where to store the number of bytes actually read.
 *                      If NULL the entire buffer must be filled for a
 *                      successful return.
 */
/*static*/ DECLCALLBACK(int) VBoxDbgConsole::backRead(PDBGCBACK pBack, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    VBoxDbgConsole *pThis = VBOXDBGCONSOLE_FROM_DBGCBACK(pBack);
    Assert(pcbRead); /** @todo implement this bit */
    if (pcbRead)
        *pcbRead = 0;

    pThis->lock();
    int rc = VINF_SUCCESS;
    if (!pThis->m_fTerminate)
    {
        if (pThis->m_cbInputBuf)
        {
            const char *psz = pThis->m_pszInputBuf;
            size_t cbRead = RT_MIN(pThis->m_cbInputBuf, cbBuf);
            memcpy(pvBuf, psz, cbRead);
            psz += cbRead;
            pThis->m_cbInputBuf -= cbRead;
            if (*psz)
                memmove(pThis->m_pszInputBuf, psz, pThis->m_cbInputBuf);
            pThis->m_pszInputBuf[pThis->m_cbInputBuf] = '\0';
            *pcbRead = cbRead;
        }
    }
    else
        rc = VERR_GENERAL_FAILURE;
    pThis->unlock();
    return rc;
}

/**
 * Write (output).
 *
 * @returns VBox status code.
 * @param   pBack       Pointer to VBoxDbgConsole::m_Back.
 * @param   pvBuf       What to write.
 * @param   cbBuf       Number of bytes to write.
 * @param   pcbWritten  Where to store the number of bytes actually written.
 *                      If NULL the entire buffer must be successfully written.
 */
/*static*/ DECLCALLBACK(int) VBoxDbgConsole::backWrite(PDBGCBACK pBack, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    VBoxDbgConsole *pThis = VBOXDBGCONSOLE_FROM_DBGCBACK(pBack);
    int rc = VINF_SUCCESS;

    pThis->lock();
    if (cbBuf + pThis->m_cbOutputBuf >= pThis->m_cbOutputBufAlloc)
    {
        size_t cbNew = RT_ALIGN_Z(cbBuf + pThis->m_cbOutputBufAlloc + 1, 1024);
        void *pv = RTMemRealloc(pThis->m_pszOutputBuf, cbNew);
        if (!pv)
        {
            pThis->unlock();
            if (pcbWritten)
                *pcbWritten = 0;
            return VERR_NO_MEMORY;
        }
        pThis->m_pszOutputBuf = (char *)pv;
        pThis->m_cbOutputBufAlloc = cbNew;
    }

    /*
     * Add the output.
     */
    memcpy(pThis->m_pszOutputBuf + pThis->m_cbOutputBuf, pvBuf, cbBuf);
    pThis->m_cbOutputBuf += cbBuf;
    pThis->m_pszOutputBuf[pThis->m_cbOutputBuf] = '\0';
    if (pcbWritten)
        *pcbWritten = cbBuf;

    if (pThis->m_fTerminate)
        rc = VERR_GENERAL_FAILURE;

    /*
     * Tell the GUI thread to draw this text.
     * We cannot do it from here without frequent crashes.
     */
    if (!pThis->m_fUpdatePending)
        QApplication::postEvent(pThis, new VBoxDbgConsoleEvent(VBoxDbgConsoleEvent::kUpdate));

    pThis->unlock();

    return rc;
}

/**
 * The Debugger Console Thread
 *
 * @returns VBox status code (ignored).
 * @param   Thread      The thread handle.
 * @param   pvUser      Pointer to the VBoxDbgConsole object.s
 */
/*static*/ DECLCALLBACK(int) VBoxDbgConsole::backThread(RTTHREAD Thread, void *pvUser)
{
    VBoxDbgConsole *pThis = (VBoxDbgConsole *)pvUser;
    LogFlow(("backThread: Thread=%p pvUser=%p\n", (void *)Thread, pvUser));

    NOREF(Thread);

    /*
     * Create and execute the console.
     */
    int rc = pThis->dbgcCreate(&pThis->m_Back.Core, 0);
    LogFlow(("backThread: returns %Vrc\n", rc));
    if (!pThis->m_fTerminate)
        QApplication::postEvent(pThis, new VBoxDbgConsoleEvent(VBoxDbgConsoleEvent::kTerminated));
    return rc;
}

bool VBoxDbgConsole::event(QEvent *pGenEvent)
{
    Assert(isGUIThread());
    if (pGenEvent->type() == (QEvent::Type)VBoxDbgConsoleEvent::kEventNumber)
    {
        VBoxDbgConsoleEvent *pEvent = (VBoxDbgConsoleEvent *)pGenEvent;

        switch (pEvent->command())
        {
            /* make update pending. */
            case VBoxDbgConsoleEvent::kUpdate:
                if (!m_fUpdatePending)
                {
                    m_fUpdatePending = true;
#ifdef VBOXDBG_USE_QT4
                    m_pTimer->setSingleShot(true);
                    m_pTimer->start(10);
#else
                    m_pTimer->start(10, true /* single shot */);
#endif 
                }
                break;

            /* dirty hack: restores the focus */
            case VBoxDbgConsoleEvent::kInputRestoreFocus:
                if (m_fInputRestoreFocus)
                {
                    m_fInputRestoreFocus = false;
                    if (!m_pInput->hasFocus())
                        m_pInput->setFocus();
                }
                break;

            /* the thread terminated */
            case VBoxDbgConsoleEvent::kTerminated:
                m_pInput->setEnabled(false);
                break;

            /* paranoia */
            default:
                AssertMsgFailed(("command=%d\n", pEvent->command()));
                break;
        }
        return true;
    }

#ifdef VBOXDBG_USE_QT4
    return QWidget::event(pGenEvent);
#else
    return QVBox::event(pGenEvent);
#endif 
}

