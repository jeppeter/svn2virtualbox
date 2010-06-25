/* $Id$ */
/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIMouseHandler class implementation
 */

/*
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Global includes */
#include <QDesktopWidget>
#include <QMouseEvent>

/* Local includes */
#include "VBoxGlobal.h"
#include "VBoxProblemReporter.h"
#include "UIMouseHandler.h"
#include "UISession.h"
#include "UIMachineLogic.h"
#include "UIMachineView.h"

#ifdef Q_WS_MAC
# include "VBoxUtils-darwin.h"
#endif /* Q_WS_MAC */

/* Factory function to create mouse-handler: */
UIMouseHandler* UIMouseHandler::create(UIMachineLogic *pMachineLogic,
                                       UIVisualStateType visualStateType)
{
    /* Prepare mouse-handler: */
    UIMouseHandler *pMouseHandler = 0;
    /* Depending on visual-state type: */
    switch (visualStateType)
    {
        /* For now all the states using common mouse-handler: */
        case UIVisualStateType_Normal:
        case UIVisualStateType_Fullscreen:
        case UIVisualStateType_Seamless:
            pMouseHandler = new UIMouseHandler(pMachineLogic);
            break;
        default:
            break;
    }
    /* Return prepared mouse-handler: */
    return pMouseHandler;
}

/* Factory function to destroy mouse-handler: */
void UIMouseHandler::destroy(UIMouseHandler *pMouseHandler)
{
    /* Delete mouse-handler: */
    delete pMouseHandler;
}

/* Registers new machine-view: */
void UIMouseHandler::addMachineView(ulong uViewIndex, UIMachineView *pMachineView)
{
    /* If that view is NOT registered yet: */
    if (!m_views.contains(uViewIndex))
    {
        /* Register machine-view: */
        m_views.insert(uViewIndex, pMachineView);
        /* Install event-filter for machine-view: */
        m_views[uViewIndex]->installEventFilter(this);
        /* Make machine-view notify mouse-handler about resizeHintDone(): */
        connect(m_views[uViewIndex], SIGNAL(resizeHintDone()), this, SLOT(sltMousePointerShapeChanged()));
    }

    /* If that viewport is NOT registered yet: */
    if (!m_viewports.contains(uViewIndex))
    {
        /* Register machine-view-viewport: */
        m_viewports.insert(uViewIndex, pMachineView->viewport());
        /* Install event-filter for machine-view-viewport: */
        m_viewports[uViewIndex]->installEventFilter(this);
    }
}

/* Remove registered machine-view: */
void UIMouseHandler::delMachineView(ulong uViewIndex)
{
    /* If that view is registered yet: */
    if (m_views.contains(uViewIndex))
    {
        /* Unregister machine-view: */
        m_views.remove(uViewIndex);
    }

    /* If that viewport is registered yet: */
    if (m_viewports.contains(uViewIndex))
    {
        /* Unregister machine-view-viewport: */
        m_viewports.remove(uViewIndex);
    }
}

void UIMouseHandler::captureMouse(ulong uScreenId)
{
    /* Do not try to capture mouse if its captured already: */
    if (uisession()->isMouseCaptured())
        return;

    /* If such viewport exists: */
    if (m_viewports.contains(uScreenId))
    {
        /* Store mouse-capturing state value: */
        uisession()->setMouseCaptured(true);

        /* Memorize the index of machine-view-viewport captured mouse: */
        m_iMouseCaptureViewIndex = uScreenId;

        /* Memorize the host position where the cursor was captured: */
        m_capturedMousePos = QCursor::pos();
#ifdef Q_WS_WIN
        /* Move the mouse to the center of the visible area: */
        m_lastMousePos = m_views[m_iMouseCaptureViewIndex]->mapToGlobal(m_views[m_iMouseCaptureViewIndex]->visibleRegion().boundingRect().center());
        QCursor::setPos(m_lastMousePos);
        /* Update mouse clipping: */
        updateMouseCursorClipping();
#elif defined (Q_WS_MAC)
        /* Move the mouse to the center of the visible area: */
        m_lastMousePos = m_views[m_iMouseCaptureViewIndex]->mapToGlobal(m_views[m_iMouseCaptureViewIndex]->visibleRegion().boundingRect().center());
        QCursor::setPos(m_lastMousePos);
        /* Grab all mouse events: */
        m_viewports[m_iMouseCaptureViewIndex]->grabMouse();
#else /* Q_WS_MAC */
        /* Remember current mouse position: */
        m_lastMousePos = QCursor::pos();
        /* Grab all mouse events: */
        m_viewports[m_iMouseCaptureViewIndex]->grabMouse();
#endif /* !Q_WS_MAC */

        /* Switch guest mouse to the relative mode: */
        CMouse mouse = session().GetConsole().GetMouse();
        mouse.PutMouseEvent(0, 0, 0, 0, 0);

        /* Emit signal if required: */
        emit mouseStateChanged(mouseState());
    }
}

void UIMouseHandler::releaseMouse()
{
    /* Do not try to release mouse if its released already: */
    if (!uisession()->isMouseCaptured())
        return;

    /* If such viewport exists: */
    if (m_viewports.contains(m_iMouseCaptureViewIndex))
    {
        /* Store mouse-capturing state value: */
        uisession()->setMouseCaptured(false);

        /* Return the cursor to where it was when we captured it: */
        QCursor::setPos(m_capturedMousePos);
#ifdef Q_WS_WIN
        /* Update mouse clipping: */
        updateMouseCursorClipping();
#else /* Q_WS_WIN */
        /* Releasing grabbed mouse from that view: */
        m_viewports[m_iMouseCaptureViewIndex]->releaseMouse();
#endif /* !Q_WS_WIN */
        /* Reset mouse-capture index: */
        m_iMouseCaptureViewIndex = -1;

        /* Emit signal if required: */
        emit mouseStateChanged(mouseState());
    }
}

/* Setter for mouse-integration feature: */
void UIMouseHandler::setMouseIntegrationEnabled(bool fEnabled)
{
    /* Do not do anything if its already done: */
    if (uisession()->isMouseIntegrated() == fEnabled)
        return;

    /* Store mouse-integration state value: */
    uisession()->setMouseIntegrated(fEnabled);

    /* Reuse sltMouseCapabilityChanged() to update mouse state: */
    sltMouseCapabilityChanged();
}

/* Current mouse state: */
int UIMouseHandler::mouseState() const
{
    return (uisession()->isMouseCaptured() ? UIMouseStateType_MouseCaptured : 0) |
           (uisession()->isMouseSupportsAbsolute() ? UIMouseStateType_MouseAbsolute : 0) |
           (uisession()->isMouseIntegrated() ? 0 : UIMouseStateType_MouseAbsoluteDisabled);
}

#ifdef Q_WS_MAC
void UIMouseHandler::setMouseCoalescingEnabled(bool fOn)
{
    /* Enable mouse event compression if we leave the VM view.
     * This is necessary for having smooth resizing of the VM/other windows.
     * Disable mouse event compression if we enter the VM view.
     * So all mouse events are registered in the VM. */
    ::darwinSetMouseCoalescingEnabled(fOn);
}
#endif /* Q_WS_MAC */

/* Machine state-change handler: */
void UIMouseHandler::sltMachineStateChanged()
{
    /* Get machine state: */
    KMachineState state = uisession()->machineState();
    /* Handle particular machine states: */
    switch (state)
    {
        case KMachineState_Paused:
        case KMachineState_TeleportingPausedVM:
        case KMachineState_Stuck:
        {
            /* Release the mouse: */
            releaseMouse();
            break;
        }
        default:
            break;
    }
}

/* Mouse capability-change handler: */
void UIMouseHandler::sltMouseCapabilityChanged()
{
    /* If mouse supports absolute pointing and mouse-integration activated: */
    if (uisession()->isMouseSupportsAbsolute() && uisession()->isMouseIntegrated())
    {
        /* Release the mouse: */
        releaseMouse();
        /* Also we should switch guest mouse to the absolute mode: */
        CMouse mouse = session().GetConsole().GetMouse();
        mouse.PutMouseEventAbsolute(-1, -1, 0, 0, 0);
    }
#if 0 /* current team's decision is NOT to capture mouse on mouse-absolute mode loosing! */
    /* If mouse-integration deactivated or mouse doesn't supports absolute pointing: */
    else
    {
        /* Search for the machine-view focused now: */
        int iFocusedView = -1;
        QList<ulong> screenIds = m_views.keys();
        for (int i = 0; i < screenIds.size(); ++i)
        {
            if (m_views[screenIds[i]]->hasFocus())
            {
                iFocusedView = screenIds[i];
                break;
            }
        }
        /* If there is no focused view but views are present we will use the first one: */
        if (iFocusedView == -1 && !screenIds.isEmpty())
            iFocusedView = screenIds[0];
        /* Capture mouse using that view: */
        if (iFocusedView != -1)
            captureMouse(iFocusedView);
    }
#else /* but just to switch the guest mouse into relative mode! */
    /* If mouse-integration deactivated or mouse doesn't supports absolute pointing: */
    else
    {
        /* Switch guest mouse to the relative mode: */
        CMouse mouse = session().GetConsole().GetMouse();
        mouse.PutMouseEvent(0, 0, 0, 0, 0);
    }
#endif

    /* Notify user about mouse supports or not absolute pointing if that method was called by signal: */
    if (sender())
        vboxProblem().remindAboutMouseIntegration(uisession()->isMouseSupportsAbsolute());

    /* Notify all listeners: */
    emit mouseStateChanged(mouseState());
}

/* Mouse pointer-shape-change handler: */
void UIMouseHandler::sltMousePointerShapeChanged()
{
    /* First of all, we should check if the host pointer should be visible.
     * We should hide host pointer in case of:
     * 1. mouse is 'captured' or
     * 2. mouse is 'not captured', 'integrated', 'absolute', host pointer is hidden by the guest. */
    if (uisession()->isMouseCaptured() ||
        (uisession()->isMouseIntegrated() &&
         uisession()->isMouseSupportsAbsolute() &&
         uisession()->isHidingHostPointer()))
    {
        QList<ulong> screenIds = m_viewports.keys();
        for (int i = 0; i < screenIds.size(); ++i)
            m_viewports[screenIds[i]]->setCursor(Qt::BlankCursor);
    }

    else

    /* Otherwise we should show host pointer with guest shape assigned to it if:
     * mouse is 'integrated', 'absolute', valid pointer shape is present. */
    if (uisession()->isMouseIntegrated() &&
        uisession()->isMouseSupportsAbsolute() &&
        uisession()->isValidPointerShapePresent())
    {
        QList<ulong> screenIds = m_viewports.keys();
        for (int i = 0; i < screenIds.size(); ++i)
            m_viewports[screenIds[i]]->setCursor(uisession()->cursor());
    }

    else

    /* There could be other states covering such situations as:
     * 1. mouse is 'not captured', 'integrated', 'not absolute' or
     * 2. mouse is 'not captured', 'not integrated', 'absolute'.
     * We have nothing to do with that except just unset the cursor. */
    {
        QList<ulong> screenIds = m_viewports.keys();
        for (int i = 0; i < screenIds.size(); ++i)
            m_viewports[screenIds[i]]->unsetCursor();
    }
}

/* Mouse-handler constructor: */
UIMouseHandler::UIMouseHandler(UIMachineLogic *pMachineLogic)
    : QObject(pMachineLogic)
    , m_pMachineLogic(pMachineLogic)
    , m_iLastMouseWheelDelta(0)
    , m_iMouseCaptureViewIndex(-1)
{
    /* Machine state-change updater: */
    connect(uisession(), SIGNAL(sigMachineStateChange()), this, SLOT(sltMachineStateChanged()));

    /* Mouse capability state-change updater: */
    connect(uisession(), SIGNAL(sigMouseCapabilityChange()), this, SLOT(sltMouseCapabilityChanged()));

    /* Mouse pointer shape state-change updaters: */
    connect(uisession(), SIGNAL(sigMousePointerShapeChange()), this, SLOT(sltMousePointerShapeChanged()));
    connect(this, SIGNAL(mouseStateChanged(int)), this, SLOT(sltMousePointerShapeChanged()));

    /* Initialize: */
    sltMachineStateChanged();
    sltMousePointerShapeChanged();
    sltMouseCapabilityChanged();
}

/* Mouse-handler destructor: */
UIMouseHandler::~UIMouseHandler()
{
}

/* Machine-logic getter: */
UIMachineLogic* UIMouseHandler::machineLogic() const
{
    return m_pMachineLogic;
}

/* UI Session getter: */
UISession* UIMouseHandler::uisession() const
{
    return machineLogic()->uisession();
}

/* Main Session getter: */
CSession& UIMouseHandler::session() const
{
    return uisession()->session();
}

/* Event handler for registered machine-view(s): */
bool UIMouseHandler::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* If that object is of QWidget type: */
    if (QWidget *pWatchedWidget = qobject_cast<QWidget*>(pWatched))
    {
        /* If thats machine-view-viewport: */
        if (m_viewports.values().contains(pWatchedWidget))
        {
            /* Get current watched widget screen id: */
            ulong uScreenId = m_viewports.key(pWatchedWidget);
            /* Handle viewport events: */
            switch (pEvent->type())
            {
                case QEvent::MouseMove:
                case QEvent::MouseButtonRelease:
                {
                    /* Check if we should propagate this event to another window: */
                    QWidget *pHoveredWidget = QApplication::widgetAt(QCursor::pos());
                    if (pHoveredWidget && pHoveredWidget != pWatchedWidget && m_viewports.values().contains(pHoveredWidget))
                    {
                        /* Get current mouse-move event: */
                        QMouseEvent *pOldMouseEvent = static_cast<QMouseEvent*>(pEvent);

                        /* Prepare redirected mouse-move event: */
                        QMouseEvent *pNewMouseEvent = new QMouseEvent(pOldMouseEvent->type(),
                                                                      pHoveredWidget->mapFromGlobal(pOldMouseEvent->globalPos()),
                                                                      pOldMouseEvent->globalPos(),
                                                                      pOldMouseEvent->button(),
                                                                      pOldMouseEvent->buttons(),
                                                                      pOldMouseEvent->modifiers());

                        /* Send that event to real destination: */
                        QApplication::postEvent(pHoveredWidget, pNewMouseEvent);

                        /* Filter out that event: */
                        return true;
                    }

                    /* Check if we should activate window under cursor: */
                    if (!uisession()->isMouseCaptured() &&
                        QApplication::activeWindow() &&
                        QApplication::activeWindow()->inherits("UIMachineWindow") &&
                        QApplication::activeWindow() != pWatchedWidget->window())
                    {
                        /* Activating hovered machine window: */
                        pWatchedWidget->window()->activateWindow();
#ifdef Q_WS_X11
                        /* On X11 its not enough to just activate window if you
                         * want to raise it also, so we will make it separately: */
                        pWatchedWidget->window()->raise();
#endif /* Q_WS_X11 */
                    }

                    /* This event should be also processed using next 'case': */
                }
                case QEvent::MouseButtonPress:
                case QEvent::MouseButtonDblClick:
                {
                    QMouseEvent *pMouseEvent = static_cast<QMouseEvent*>(pEvent);
                    m_iLastMouseWheelDelta = 0;
                    if (mouseEvent(pMouseEvent->type(), uScreenId,
                                   pMouseEvent->pos(), pMouseEvent->globalPos(),
                                   pMouseEvent->buttons(), 0, Qt::Horizontal))
                        return true;
                    break;
                }
                case QEvent::Wheel:
                {
                    QWheelEvent *pWheelEvent = static_cast<QWheelEvent*>(pEvent);
                    /* There are pointing devices which send smaller values for the delta than 120.
                     * Here we sum them up until we are greater than 120. This allows to have finer control
                     * over the speed acceleration & enables such devices to send a valid wheel event to our
                     * guest mouse device at all: */
                    int iDelta = 0;
                    m_iLastMouseWheelDelta += pWheelEvent->delta();
                    if (qAbs(m_iLastMouseWheelDelta) >= 120)
                    {
                        iDelta = m_iLastMouseWheelDelta;
                        m_iLastMouseWheelDelta = m_iLastMouseWheelDelta % 120;
                    }
                    if (mouseEvent(pWheelEvent->type(), uScreenId,
                                   pWheelEvent->pos(), pWheelEvent->globalPos(),
#ifdef QT_MAC_USE_COCOA
                                   /* Qt Cocoa is buggy. It always reports a left button pressed when the
                                    * mouse wheel event occurs. A workaround is to ask the application which
                                    * buttons are pressed currently: */
                                   QApplication::mouseButtons(),
#else /* QT_MAC_USE_COCOA */
                                   pWheelEvent->buttons(),
#endif /* !QT_MAC_USE_COCOA */
                                   iDelta, pWheelEvent->orientation()))
                        return true;
                    break;
                }
#ifdef Q_WS_MAC
                case QEvent::Leave:
                {
                    /* Enable mouse event compression if we leave the VM view.
                     * This is necessary for having smooth resizing of the VM/other windows: */
                    setMouseCoalescingEnabled(true);
                    break;
                }
                case QEvent::Enter:
                {
                    /* Disable mouse event compression if we enter the VM view.
                     * So all mouse events are registered in the VM.
                     * Only do this if the keyboard/mouse is grabbed
                     * (this is when we have a valid event handler): */
                    if (m_views[uScreenId]->isKeyboardGrabbed())
                        setMouseCoalescingEnabled(false);
                    break;
                }
#endif /* Q_WS_MAC */
#ifdef Q_WS_WIN
                case QEvent::Resize:
                {
                    /* Update mouse clipping: */
                    updateMouseCursorClipping();
                    break;
                }
#endif /* Q_WS_WIN */
                default:
                    break;
            }
        }
        /* If no => check if that widget is of UIMachineView type: */
        else if (UIMachineView *pWatchedMachineView = qobject_cast<UIMachineView*>(pWatchedWidget))
        {
            /* If thats machine-view itself => handle machine-view events: */
            if (m_views.values().contains(pWatchedMachineView))
            {
                switch (pEvent->type())
                {
                    case QEvent::FocusOut:
                        /* Just release the mouse: */
                        releaseMouse();
                        break;
                    default:
                        break;
                }
            }
        }
    }
    return QObject::eventFilter(pWatched, pEvent);
}

/* Separate function to handle most of existing mouse-events: */
bool UIMouseHandler::mouseEvent(int iEventType, ulong uScreenId,
                                const QPoint &relativePos, const QPoint &globalPos,
                                Qt::MouseButtons mouseButtons,
                                int wheelDelta, Qt::Orientation wheelDirection)
{
    /* Check if such view & viewport are registered: */
    if (!m_views.contains(uScreenId) || !m_viewports.contains(uScreenId))
        return true;

    int iMouseButtonsState = 0;
    if (mouseButtons & Qt::LeftButton)
        iMouseButtonsState |= KMouseButtonState_LeftButton;
    if (mouseButtons & Qt::RightButton)
        iMouseButtonsState |= KMouseButtonState_RightButton;
    if (mouseButtons & Qt::MidButton)
        iMouseButtonsState |= KMouseButtonState_MiddleButton;
    if (mouseButtons & Qt::XButton1)
        iMouseButtonsState |= KMouseButtonState_XButton1;
    if (mouseButtons & Qt::XButton2)
        iMouseButtonsState |= KMouseButtonState_XButton2;

#ifdef Q_WS_MAC
    /* Simulate the right click on host-key + left-mouse-button: */
    if (m_views[uScreenId]->isHostKeyPressed() &&
        m_views[uScreenId]->isHostKeyAlone() &&
        iMouseButtonsState == KMouseButtonState_LeftButton)
        iMouseButtonsState = KMouseButtonState_RightButton;
#endif /* Q_WS_MAC */

    int iWheelVertical = 0;
    int iWheelHorizontal = 0;
    if (wheelDirection == Qt::Vertical)
    {
        /* The absolute value of wheel delta is 120 units per every wheel move;
         * positive deltas correspond to counterclockwize rotations (usually up),
         * negative deltas correspond to clockwize (usually down). */
        iWheelVertical = - (wheelDelta / 120);
    }
    else if (wheelDirection == Qt::Horizontal)
        iWheelHorizontal = wheelDelta / 120;

    if (uisession()->isMouseCaptured())
    {
#ifdef Q_WS_WIN
        /* Send pending WM_PAINT events: */
        ::UpdateWindow(m_viewports[uScreenId]->winId());
#endif

        CMouse mouse = session().GetConsole().GetMouse();
        mouse.PutMouseEvent(globalPos.x() - m_lastMousePos.x(),
                            globalPos.y() - m_lastMousePos.y(),
                            iWheelVertical, iWheelHorizontal, iMouseButtonsState);

#ifdef Q_WS_MAC
        /* Keep the mouse from leaving the widget.
         * This is a bit tricky to get right because if it escapes we won't necessarily
         * get mouse events any longer and can warp it back. So, we keep safety zone
         * of up to 300 pixels around the borders of the widget to prevent this from
         * happening. Also, the mouse is warped back to the center of the widget.
         * (Note, relativePos seems to be unreliable, it caused endless recursion here at one points...)
         * (Note, synergy and other remote clients might not like this cursor warping.) */
        QRect rect = m_viewports[uScreenId]->visibleRegion().boundingRect();
        QPoint pw = m_viewports[uScreenId]->mapToGlobal(m_viewports[uScreenId]->pos());
        rect.translate(pw.x(), pw.y());

        QRect dpRect = QApplication::desktop()->screenGeometry(m_viewports[uScreenId]);
        if (rect.intersects(dpRect))
            rect = rect.intersect(dpRect);

        int iWsafe = rect.width() / 6;
        rect.setWidth(rect.width() - iWsafe * 2);
        rect.setLeft(rect.left() + iWsafe);

        int iHsafe = rect.height() / 6;
        rect.setWidth(rect.height() - iHsafe * 2);
        rect.setTop(rect.top() + iHsafe);

        if (rect.contains(globalPos, true))
            m_lastMousePos = globalPos;
        else
        {
            m_lastMousePos = rect.center();
            QCursor::setPos(m_lastMousePos);
        }
#else /* Q_WS_MAC */

        /* Bringing mouse to the opposite side to simulate the endless moving: */

# ifdef Q_WS_WIN
        int iWe = m_viewports[uScreenId]->width() - 1;
        int iHe = m_viewports[uScreenId]->height() - 1;
        QPoint p = relativePos;
        if (relativePos.x() == 0)
            p.setX(iWe - 1);
        else if (relativePos.x() == iWe)
            p.setX(1);
        if (relativePos.y() == 0 )
            p.setY(iHe - 1);
        else if (relativePos.y() == iHe)
            p.setY(1);

        if (p != relativePos)
        {
            m_lastMousePos = m_viewports[uScreenId]->mapToGlobal(p);
            QCursor::setPos(m_lastMousePos);
        }
        else
        {
            m_lastMousePos = globalPos;
        }
# else /* Q_WS_WIN */
        int iWe = QApplication::desktop()->width() - 1;
        int iHe = QApplication::desktop()->height() - 1;
        QPoint p = globalPos;
        if (globalPos.x() == 0)
            p.setX(iWe - 1);
        else if (globalPos.x() == iWe)
            p.setX( 1 );
        if (globalPos.y() == 0)
            p.setY(iHe - 1);
        else if (globalPos.y() == iHe)
            p.setY(1);

        if (p != globalPos)
        {
            m_lastMousePos =  p;
            QCursor::setPos(m_lastMousePos);
        }
        else
        {
            m_lastMousePos = globalPos;
        }
# endif /* !Q_WS_WIN */
#endif /* !Q_WS_MAC */
        return true; /* stop further event handling */
    }
    else /* !uisession()->isMouseCaptured() */
    {
#if 0 // TODO: Move that to fullscreen event-handler:
        if (vboxGlobal().vmRenderMode() != VBoxDefs::SDLMode)
        {
            /* try to automatically scroll the guest canvas if the
             * mouse is on the screen border */
            /// @todo (r=dmik) better use a timer for autoscroll
            QRect scrGeo = QApplication::desktop()->screenGeometry (this);
            int iDx = 0, iDy = 0;
            if (scrGeo.width() < contentsWidth())
            {
                if (scrGeo.left() == globalPos.x()) iDx = -1;
                if (scrGeo.right() == globalPos.x()) iDx = +1;
            }
            if (scrGeo.height() < contentsHeight())
            {
                if (scrGeo.top() == globalPos.y()) iDy = -1;
                if (scrGeo.bottom() == globalPos.y()) iDy = +1;
            }
            if (iDx || iDy)
                scrollBy(iDx, iDy);
        }
#endif

        if (uisession()->isMouseSupportsAbsolute() && uisession()->isMouseIntegrated())
        {
            int iCw = m_views[uScreenId]->contentsWidth(), iCh = m_views[uScreenId]->contentsHeight();
            int iVw = m_views[uScreenId]->visibleWidth(), iVh = m_views[uScreenId]->visibleHeight();

            if (vboxGlobal().vmRenderMode() != VBoxDefs::SDLMode)
            {
                /* Try to automatically scroll the guest canvas if the
                 * mouse goes outside its visible part: */
                int iDx = 0;
                if (relativePos.x() > iVw) iDx = relativePos.x() - iVw;
                else if (relativePos.x() < 0) iDx = relativePos.x();
                int iDy = 0;
                if (relativePos.y() > iVh) iDy = relativePos.y() - iVh;
                else if (relativePos.y() < 0) iDy = relativePos.y();
                if (iDx != 0 || iDy != 0) m_views[uScreenId]->scrollBy(iDx, iDy);
            }

            QPoint cpnt = m_views[uScreenId]->viewportToContents(relativePos);
            if (cpnt.x() < 0) cpnt.setX(0);
            else if (cpnt.x() > iCw - 1) cpnt.setX(iCw - 1);
            if (cpnt.y() < 0) cpnt.setY(0);
            else if (cpnt.y() > iCh - 1) cpnt.setY(iCh - 1);

            /* Get & Setup absolute-event shift: */
            CFramebuffer framebuffer;
            LONG xShift = 0, yShift = 0;
            session().GetConsole().GetDisplay().GetFramebuffer(uScreenId, framebuffer, xShift, yShift);
            cpnt.setX(cpnt.x() + xShift);
            cpnt.setY(cpnt.y() + yShift);

            CMouse mouse = session().GetConsole().GetMouse();
            mouse.PutMouseEventAbsolute(cpnt.x() + 1, cpnt.y() + 1, iWheelVertical, iWheelHorizontal, iMouseButtonsState);
            return true;
        }
        else
        {
            if (m_views[uScreenId]->hasFocus() && (iEventType == QEvent::MouseButtonRelease && mouseButtons == Qt::NoButton))
            {
                if (uisession()->isPaused())
                {
                    vboxProblem().remindAboutPausedVMInput();
                }
                else if (uisession()->isRunning())
                {
                    /* Temporarily disable auto capture that will take place after this dialog is dismissed because
                     * the capture state is to be defined by the dialog result itself: */
                    uisession()->setAutoCaptureDisabled(true);
                    bool autoConfirmed = false;
                    bool ok = vboxProblem().confirmInputCapture(&autoConfirmed);
                    if (autoConfirmed)
                        uisession()->setAutoCaptureDisabled(false);
                    /* Otherwise, the disable flag will be reset in the next console view's foucs in event (since
                     * may happen asynchronously on some platforms, after we return from this code): */
                    if (ok)
                    {
#ifdef Q_WS_X11
                        /* Make sure that pending FocusOut events from the previous message box are handled,
                         * otherwise the mouse is immediately ungrabbed again: */
                        qApp->processEvents();
#endif
                        /* Actually that will be a call to keyboard-handler, not no machine-view,
                         * but now keyboard-handler is not implemented yet. */
                        m_views[uScreenId]->captureKbd(true);

                        captureMouse(uScreenId);
                    }
                }
            }
        }
    }

    return false;
}

#ifdef Q_WS_WIN
/* This method is actually required only because under win-host
 * we do not really grab the mouse in case of capturing it: */
void UIMouseHandler::updateMouseCursorClipping()
{
    /* Check if such viewport is registered: */
    if (!m_viewports.contains(m_iMouseCaptureViewIndex))
        return;

    if (uisession()->isMouseCaptured())
    {
        QWidget *pMachineViewViewport = m_viewports[m_iMouseCaptureViewIndex];
        QRect r = pMachineViewViewport->rect();
        r.moveTopLeft(pMachineViewViewport->mapToGlobal(QPoint(0, 0)));
        RECT rect = { r.left(), r.top(), r.right() + 1, r.bottom() + 1 };
        ::ClipCursor(&rect);
    }
    else
    {
        ::ClipCursor(NULL);
    }
}
#endif /* Q_WS_WIN */

