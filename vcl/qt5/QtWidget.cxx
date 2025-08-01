/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <QtWidget.hxx>
#include <QtWidget.moc>

#include <QtFrame.hxx>
#include <QtInstance.hxx>
#include <QtTransferable.hxx>
#include <QtTools.hxx>

#include <QtCore/QMimeData>
#include <QtGui/QDrag>
#include <QtGui/QFocusEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QResizeEvent>
#include <QtGui/QShowEvent>
#include <QtGui/QTextCharFormat>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QWidget>

#include <vcl/commandevent.hxx>
#include <vcl/event.hxx>
#include <vcl/qt/QtUtils.hxx>
#include <vcl/toolkit/floatwin.hxx>
#include <window.h>
#include <comphelper/OAccessible.hxx>
#include <comphelper/diagnose_ex.hxx>

#include <com/sun/star/accessibility/XAccessible.hpp>
#include <com/sun/star/accessibility/XAccessibleEditableText.hpp>

#if CHECK_ANY_QT_USING_X11
#define XK_MISCELLANY
#include <X11/keysymdef.h>
#endif

using namespace com::sun::star;

void QtWidget::paintEvent(QPaintEvent* pEvent) { m_rFrame.handlePaintEvent(pEvent, this); }

void QtWidget::resizeEvent(QResizeEvent* pEvent) { m_rFrame.handleResizeEvent(pEvent); }

void QtWidget::fakeResize()
{
    QResizeEvent aEvent(size(), QSize());
    resizeEvent(&aEvent);
}

void QtWidget::fillSalAbstractMouseEvent(const QInputEvent* pQEvent, const QPoint& rPos,
                                         Qt::MouseButtons eButtons,
                                         SalAbstractMouseEvent& aSalEvent) const
{
    const qreal fRatio = m_rFrame.devicePixelRatioF();
    const Point aPos = toPoint(rPos * fRatio);

    aSalEvent.mnX
        = QGuiApplication::isLeftToRight() ? aPos.X() : round(width() * fRatio) - aPos.X();
    aSalEvent.mnY = aPos.Y();
    aSalEvent.mnTime = pQEvent->timestamp();
    aSalEvent.mnCode = toVclKeyboardModifiers(pQEvent->modifiers()) | toVclMouseButtons(eButtons);
}

void QtWidget::handleMouseButtonEvent(const QMouseEvent* pEvent) const
{
    SalMouseEvent aEvent;
    fillSalAbstractMouseEvent(pEvent, pEvent->pos(), pEvent->buttons(), aEvent);

    switch (pEvent->button())
    {
        case Qt::LeftButton:
            aEvent.mnButton = MOUSE_LEFT;
            break;
        case Qt::MiddleButton:
            aEvent.mnButton = MOUSE_MIDDLE;
            break;
        case Qt::RightButton:
            aEvent.mnButton = MOUSE_RIGHT;
            break;
        default:
            return;
    }

    SalEvent nEventType;
    if (pEvent->type() == QEvent::MouseButtonPress || pEvent->type() == QEvent::MouseButtonDblClick)
        nEventType = SalEvent::MouseButtonDown;
    else
        nEventType = SalEvent::MouseButtonUp;
    m_rFrame.CallCallback(nEventType, &aEvent);
}

void QtWidget::mousePressEvent(QMouseEvent* pEvent)
{
    handleMouseButtonEvent(pEvent);
    if (m_rFrame.isPopup()
        && !geometry().translated(geometry().topLeft() * -1).contains(pEvent->pos()))
        closePopup();
}

void QtWidget::mouseReleaseEvent(QMouseEvent* pEvent) { handleMouseButtonEvent(pEvent); }

void QtWidget::mouseMoveEvent(QMouseEvent* pEvent)
{
    SalMouseEvent aEvent;
    fillSalAbstractMouseEvent(pEvent, pEvent->pos(), pEvent->buttons(), aEvent);

    aEvent.mnButton = 0;

    m_rFrame.CallCallback(SalEvent::MouseMove, &aEvent);
    pEvent->accept();
}

void QtWidget::handleMouseEnterLeaveEvent(QEvent* pQEvent) const
{
    const qreal fRatio = m_rFrame.devicePixelRatioF();
    const Point aPos = toPoint(mapFromGlobal(QCursor::pos()) * fRatio);

    SalMouseEvent aEvent;
    aEvent.mnX = QGuiApplication::isLeftToRight() ? aPos.X() : round(width() * fRatio) - aPos.X();
    aEvent.mnY = aPos.Y();
    aEvent.mnTime = 0;
    aEvent.mnButton = 0;
    aEvent.mnCode = toVclKeyboardModifiers(QGuiApplication::keyboardModifiers())
                    | toVclMouseButtons(QGuiApplication::mouseButtons());

    SalEvent nEventType;
    if (pQEvent->type() == QEvent::Enter)
        nEventType = SalEvent::MouseMove;
    else
        nEventType = SalEvent::MouseLeave;
    m_rFrame.CallCallback(nEventType, &aEvent);
    pQEvent->accept();
}

void QtWidget::leaveEvent(QEvent* pEvent) { handleMouseEnterLeaveEvent(pEvent); }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void QtWidget::enterEvent(QEnterEvent* pEvent)
#else
void QtWidget::enterEvent(QEvent* pEvent)
#endif
{
    handleMouseEnterLeaveEvent(pEvent);
}

void QtWidget::wheelEvent(QWheelEvent* pEvent)
{
    SalWheelMouseEvent aEvent;
    fillSalAbstractMouseEvent(pEvent, pEvent->position().toPoint(), pEvent->buttons(), aEvent);

    // mouse wheel ticks are 120, which we map to 3 lines.
    // we have to accumulate for touch scroll to keep track of the absolute delta.

    int nDelta = pEvent->angleDelta().y(), lines;
    aEvent.mbHorz = nDelta == 0;
    if (aEvent.mbHorz)
    {
        nDelta = (QGuiApplication::isLeftToRight() ? 1 : -1) * pEvent->angleDelta().x();
        if (!nDelta)
            return;

        m_nDeltaX += nDelta;
        lines = m_nDeltaX / 40;
        m_nDeltaX = m_nDeltaX % 40;
    }
    else
    {
        m_nDeltaY += nDelta;
        lines = m_nDeltaY / 40;
        m_nDeltaY = m_nDeltaY % 40;
    }

    aEvent.mnDelta = nDelta;
    aEvent.mnNotchDelta = nDelta < 0 ? -1 : 1;
    aEvent.mnScrollLines = std::abs(lines);

    m_rFrame.CallCallback(SalEvent::WheelMouse, &aEvent);
    pEvent->accept();
}

void QtWidget::dragEnterEvent(QDragEnterEvent* pEvent) { m_rFrame.handleDragEnter(pEvent); }

// also called when a drop is rejected
void QtWidget::dragLeaveEvent(QDragLeaveEvent*) { m_rFrame.handleDragLeave(); }

void QtWidget::dragMoveEvent(QDragMoveEvent* pEvent) { m_rFrame.handleDragMove(pEvent); }

void QtWidget::dropEvent(QDropEvent* pEvent) { m_rFrame.handleDrop(pEvent); }

void QtWidget::moveEvent(QMoveEvent* pEvent)
{
    // already handled by QtMainWindow::moveEvent
    if (m_rFrame.m_pTopLevel)
        return;

    m_rFrame.handleMoveEvent(pEvent);
}

void QtWidget::showEvent(QShowEvent*)
{
    QSize aSize(size() * m_rFrame.devicePixelRatioF());
    // forcing an immediate update somehow interferes with the hide + show
    // sequence from QtFrame::SetModal, if the frame was already set visible,
    // resulting in a hidden / unmapped window
    SalPaintEvent aPaintEvt(0, 0, aSize.width(), aSize.height());
    if (m_rFrame.isPopup())
        GetQtInstance().setActivePopup(&m_rFrame);
    m_rFrame.CallCallback(SalEvent::Paint, &aPaintEvt);
}

void QtWidget::hideEvent(QHideEvent*)
{
    if (m_rFrame.isPopup() && GetQtInstance().activePopup() == &m_rFrame)
        GetQtInstance().setActivePopup(nullptr);
}

void QtWidget::closeEvent(QCloseEvent* /*pEvent*/)
{
    m_rFrame.CallCallback(SalEvent::Close, nullptr);
}

void QtWidget::commitText(const QString& aText) const
{
    SalExtTextInputEvent aInputEvent;
    aInputEvent.mpTextAttr = nullptr;
    aInputEvent.mnCursorFlags = 0;
    aInputEvent.maText = toOUString(aText);
    aInputEvent.mnCursorPos = aInputEvent.maText.getLength();

    SolarMutexGuard aGuard;
    vcl::DeletionListener aDel(&m_rFrame);
    m_rFrame.CallCallback(SalEvent::ExtTextInput, &aInputEvent);
    if (!aDel.isDeleted())
        m_rFrame.CallCallback(SalEvent::EndExtTextInput, nullptr);
}

void QtWidget::deleteReplacementText(int nReplacementStart, int nReplacementLength) const
{
    // get the surrounding text
    SolarMutexGuard aGuard;
    SalSurroundingTextRequestEvent aSurroundingTextEvt;
    aSurroundingTextEvt.maText.clear();
    aSurroundingTextEvt.mnStart = aSurroundingTextEvt.mnEnd = 0;
    m_rFrame.CallCallback(SalEvent::SurroundingTextRequest, &aSurroundingTextEvt);

    // Turn nReplacementStart, nReplacementLength into a UTF-16 selection
    const Selection aSelection = SalFrame::CalcDeleteSurroundingSelection(
        aSurroundingTextEvt.maText, aSurroundingTextEvt.mnStart, nReplacementStart,
        nReplacementLength);

    const Selection aInvalid(SAL_MAX_UINT32, SAL_MAX_UINT32);
    if (aSelection == aInvalid)
    {
        SAL_WARN("vcl.qt", "Invalid selection when deleting IM replacement text");
        return;
    }

    SalSurroundingTextSelectionChangeEvent aEvt;
    aEvt.mnStart = aSelection.Min();
    aEvt.mnEnd = aSelection.Max();
    m_rFrame.CallCallback(SalEvent::DeleteSurroundingTextRequest, &aEvt);
}

bool QtWidget::handleGestureEvent(QGestureEvent* pGestureEvent) const
{
    if (QGesture* pGesture = pGestureEvent->gesture(Qt::PinchGesture))
    {
        if (!pGesture->hasHotSpot())
        {
            pGestureEvent->ignore();
            return false;
        }

        GestureEventZoomType eType = GestureEventZoomType::Begin;
        switch (pGesture->state())
        {
            case Qt::GestureStarted:
                eType = GestureEventZoomType::Begin;
                break;
            case Qt::GestureUpdated:
                eType = GestureEventZoomType::Update;
                break;
            case Qt::GestureFinished:
                eType = GestureEventZoomType::End;
                break;
            case Qt::NoGesture:
            case Qt::GestureCanceled:
            default:
                SAL_WARN("vcl.qt", "Unhandled pinch gesture state: " << pGesture->state());
                pGestureEvent->ignore();
                return false;
        }

        QPinchGesture* pPinchGesture = static_cast<QPinchGesture*>(pGesture);
        const QPointF aHotspot = pGesture->hotSpot();
        SalGestureZoomEvent aEvent;
        aEvent.meEventType = eType;
        aEvent.mnX = aHotspot.x();
        aEvent.mnY = aHotspot.y();
        aEvent.mfScaleDelta = 1 + pPinchGesture->totalScaleFactor();
        m_rFrame.CallCallback(SalEvent::GestureZoom, &aEvent);
        pGestureEvent->accept();
        return true;
    }

    pGestureEvent->ignore();
    return false;
}

bool QtWidget::handleKeyEvent(QKeyEvent* pEvent) const
{
    const bool bIsKeyPressed
        = pEvent->type() == QEvent::KeyPress || pEvent->type() == QEvent::ShortcutOverride;
    sal_uInt16 nCode = toVclKeyCode(pEvent->key(), pEvent->modifiers());
    if (bIsKeyPressed && nCode == 0 && pEvent->text().length() > 1
        && testAttribute(Qt::WA_InputMethodEnabled))
    {
        commitText(pEvent->text());
        pEvent->accept();
        return true;
    }

    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);

    if (nCode == 0 && pEvent->text().isEmpty())
    {
        sal_uInt16 nModCode = toVclKeyboardModifiers(pEvent->modifiers());
        SalKeyModEvent aModEvt;
        aModEvt.mbDown = bIsKeyPressed;
        aModEvt.mnModKeyCode = ModKeyFlags::NONE;

#if CHECK_ANY_QT_USING_X11
        if (QGuiApplication::platformName() == "xcb")
        {
            // pressing just the ctrl key leads to a keysym of XK_Control but
            // the event state does not contain ControlMask. In the release
            // event it's the other way round: it does contain the Control mask.
            // The modifier mode therefore has to be adapted manually.
            ModKeyFlags nExtModMask = ModKeyFlags::NONE;
            sal_uInt16 nModMask = 0;
            switch (pEvent->nativeVirtualKey())
            {
                case XK_Control_L:
                    nExtModMask = ModKeyFlags::LeftMod1;
                    nModMask = KEY_MOD1;
                    break;
                case XK_Control_R:
                    nExtModMask = ModKeyFlags::RightMod1;
                    nModMask = KEY_MOD1;
                    break;
                case XK_Alt_L:
                    nExtModMask = ModKeyFlags::LeftMod2;
                    nModMask = KEY_MOD2;
                    break;
                case XK_Alt_R:
                    nExtModMask = ModKeyFlags::RightMod2;
                    nModMask = KEY_MOD2;
                    break;
                case XK_Shift_L:
                    nExtModMask = ModKeyFlags::LeftShift;
                    nModMask = KEY_SHIFT;
                    break;
                case XK_Shift_R:
                    nExtModMask = ModKeyFlags::RightShift;
                    nModMask = KEY_SHIFT;
                    break;
                // Map Meta/Super keys to MOD3 modifier on all Unix systems
                // except macOS
                case XK_Meta_L:
                case XK_Super_L:
                    nExtModMask = ModKeyFlags::LeftMod3;
                    nModMask = KEY_MOD3;
                    break;
                case XK_Meta_R:
                case XK_Super_R:
                    nExtModMask = ModKeyFlags::RightMod3;
                    nModMask = KEY_MOD3;
                    break;
            }

            if (!bIsKeyPressed)
            {
                // sending the old mnModKeyCode mask on release is needed to
                // implement the writing direction switch with Ctrl + L/R-Shift
                aModEvt.mnModKeyCode = m_rFrame.m_nKeyModifiers;
                nModCode &= ~nModMask;
                m_rFrame.m_nKeyModifiers &= ~nExtModMask;
            }
            else
            {
                nModCode |= nModMask;
                m_rFrame.m_nKeyModifiers |= nExtModMask;
                aModEvt.mnModKeyCode = m_rFrame.m_nKeyModifiers;
            }
        }
#endif
        aModEvt.mnCode = nModCode;

        m_rFrame.CallCallback(SalEvent::KeyModChange, &aModEvt);
        return false;
    }

#if CHECK_ANY_QT_USING_X11
    // prevent interference of writing direction switch (Ctrl + L/R-Shift) with "normal" shortcuts
    m_rFrame.m_nKeyModifiers = ModKeyFlags::NONE;
#endif

    SalKeyEvent aEvent;
    aEvent.mnCharCode = (pEvent->text().isEmpty() ? 0 : pEvent->text().at(0).unicode());
    aEvent.mnRepeat = 0;
    aEvent.mnCode = nCode;
    aEvent.mnCode |= toVclKeyboardModifiers(pEvent->modifiers());

    bool bStopProcessingKey;
    if (bIsKeyPressed)
        bStopProcessingKey = m_rFrame.CallCallback(SalEvent::KeyInput, &aEvent);
    else
        bStopProcessingKey = m_rFrame.CallCallback(SalEvent::KeyUp, &aEvent);
    if (bStopProcessingKey)
        pEvent->accept();
    return bStopProcessingKey;
}

bool QtWidget::handleEvent(QEvent* pEvent)
{
    if (pEvent->type() == QEvent::Gesture)
    {
        QGestureEvent* pGestureEvent = static_cast<QGestureEvent*>(pEvent);
        return handleGestureEvent(pGestureEvent);
    }
    else if (pEvent->type() == QEvent::ShortcutOverride)
    {
        // ignore non-spontaneous QEvent::ShortcutOverride events,
        // since such an extra event is sent e.g. with Orca screen reader enabled,
        // so that two events of that kind (the "real one" and a non-spontaneous one)
        // would otherwise be processed, resulting in duplicate input as 'handleKeyEvent'
        // is called below (s. tdf#122053)
        if (!pEvent->spontaneous())
        {
            // accept event so shortcut action (from menu) isn't triggered in addition
            // to the processing for the spontaneous event further below
            pEvent->accept();
            return false;
        }

        // Accepted event disables shortcut activation,
        // but enables keypress event.
        // If event is not accepted and shortcut is successfully activated,
        // KeyPress event is omitted.
        //
        // Instead of processing keyPressEvent, handle ShortcutOverride event,
        // and if it's handled - disable the shortcut, it should have been activated.
        // Don't process keyPressEvent generated after disabling shortcut since it was handled here.
        // If event is not handled, don't accept it and let Qt activate related shortcut.
        if (handleKeyEvent(static_cast<QKeyEvent*>(pEvent)))
            return true;
    }
    else if (pEvent->type() == QEvent::ToolTip)
    {
        // Qt's POV on the active popup is wrong due to our fake popup, so check LO's state.
        // Otherwise Qt will continue handling ToolTip events from the "parent" window.
        const QtFrame* pPopupFrame = GetQtInstance().activePopup();
        if (!m_rFrame.m_aTooltipText.isEmpty() && (!pPopupFrame || pPopupFrame == &m_rFrame))
        {
            // tdf#162297 and tdf#166805: Use an html tag to ensure the tooltip is wrapped
            QString sTooltipText("<html>");
            sTooltipText += toQString(m_rFrame.m_aTooltipText).toHtmlEscaped();
            sTooltipText += "</html>";
            QToolTip::showText(QCursor::pos(), sTooltipText, this, m_rFrame.m_aTooltipArea);
        }
        else
        {
            QToolTip::hideText();
            pEvent->ignore();
        }
        return true;
    }
    return false;
}

bool QtWidget::event(QEvent* pEvent) { return handleEvent(pEvent) || QWidget::event(pEvent); }

void QtWidget::keyReleaseEvent(QKeyEvent* pEvent)
{
    if (!handleKeyEvent(pEvent))
        QWidget::keyReleaseEvent(pEvent);
}

void QtWidget::focusInEvent(QFocusEvent*) { m_rFrame.CallCallback(SalEvent::GetFocus, nullptr); }

void QtWidget::closePopup()
{
    VclPtr<FloatingWindow> pFirstFloat = ImplGetSVData()->mpWinData->mpFirstFloat;
    if (pFirstFloat && !(pFirstFloat->GetPopupModeFlags() & FloatWinPopupFlags::NoAppFocusClose))
    {
        SolarMutexGuard aGuard;
        pFirstFloat->EndPopupMode(FloatWinPopupEndFlags::Cancel | FloatWinPopupEndFlags::CloseAll);
    }
}

void QtWidget::focusOutEvent(QFocusEvent*)
{
#if CHECK_ANY_QT_USING_X11
    m_rFrame.m_nKeyModifiers = ModKeyFlags::NONE;
#endif
    endExtTextInput();
    m_rFrame.CallCallback(SalEvent::LoseFocus, nullptr);
    closePopup();
}

QtWidget::QtWidget(QtFrame& rFrame, Qt::WindowFlags f)
    // if you try to set the QWidget parent via the QtFrame, instead of using nullptr, at
    // least test Wayland popups; these horribly broke last time doing this (read commits)!
    : QWidget(nullptr, f)
    , m_rFrame(rFrame)
    , m_bNonEmptyIMPreeditSeen(false)
    , m_bInInputMethodQueryCursorRectangle(false)
    , m_nDeltaX(0)
    , m_nDeltaY(0)
{
    // make floating windows translucent, needed at least by BubbleWindow
    // (used for update notifications) on X11
    if (f & Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_NoSystemBackground);
    }

    setMouseTracking(true);
    if (!rFrame.isPopup())
        setFocusPolicy(Qt::StrongFocus);
    else
        setFocusPolicy(Qt::ClickFocus);

    grabGesture(Qt::PinchGesture);
}

static ExtTextInputAttr lcl_MapUnderlineStyle(QTextCharFormat::UnderlineStyle us)
{
    switch (us)
    {
        case QTextCharFormat::NoUnderline:
            return ExtTextInputAttr::NONE;
        case QTextCharFormat::DotLine:
            return ExtTextInputAttr::DottedUnderline;
        case QTextCharFormat::DashDotDotLine:
        case QTextCharFormat::DashDotLine:
            return ExtTextInputAttr::DashDotUnderline;
        case QTextCharFormat::WaveUnderline:
            return ExtTextInputAttr::GrayWaveline;
        default:
            return ExtTextInputAttr::Underline;
    }
}

void QtWidget::inputMethodEvent(QInputMethodEvent* pEvent)
{
    const bool bHasCommitText = !pEvent->commitString().isEmpty();
    const int nReplacementLength = pEvent->replacementLength();

    if (nReplacementLength > 0 || bHasCommitText)
    {
        if (nReplacementLength > 0)
            deleteReplacementText(pEvent->replacementStart(), nReplacementLength);
        if (bHasCommitText)
            commitText(pEvent->commitString());
    }
    else
    {
        SalExtTextInputEvent aInputEvent;
        aInputEvent.mpTextAttr = nullptr;
        aInputEvent.mnCursorFlags = 0;
        aInputEvent.maText = toOUString(pEvent->preeditString());
        aInputEvent.mnCursorPos = 0;

        const sal_Int32 nLength = aInputEvent.maText.getLength();
        const QList<QInputMethodEvent::Attribute>& rAttrList = pEvent->attributes();
        std::vector<ExtTextInputAttr> aTextAttrs(std::max(sal_Int32(1), nLength),
                                                 ExtTextInputAttr::NONE);
        aInputEvent.mpTextAttr = aTextAttrs.data();

        for (const QInputMethodEvent::Attribute& rAttr : rAttrList)
        {
            switch (rAttr.type)
            {
                case QInputMethodEvent::TextFormat:
                {
                    QTextCharFormat aCharFormat
                        = qvariant_cast<QTextFormat>(rAttr.value).toCharFormat();
                    if (aCharFormat.isValid())
                    {
                        ExtTextInputAttr aETIP
                            = lcl_MapUnderlineStyle(aCharFormat.underlineStyle());
                        if (aCharFormat.hasProperty(QTextFormat::BackgroundBrush))
                            aETIP |= ExtTextInputAttr::Highlight;
                        if (aCharFormat.fontStrikeOut())
                            aETIP |= ExtTextInputAttr::RedText;
                        for (int j = rAttr.start; j < rAttr.start + rAttr.length; j++)
                        {
                            SAL_WARN_IF(j >= static_cast<int>(aTextAttrs.size()), "vcl.qt",
                                        "QInputMethodEvent::Attribute out of range. Broken range: "
                                            << rAttr.start << "," << rAttr.start + rAttr.length
                                            << " Legal range: 0," << aTextAttrs.size());
                            if (j >= static_cast<int>(aTextAttrs.size()))
                                break;
                            aTextAttrs[j] = aETIP;
                        }
                    }
                    break;
                }
                case QInputMethodEvent::Cursor:
                {
                    aInputEvent.mnCursorPos = rAttr.start;
                    if (rAttr.length == 0)
                        aInputEvent.mnCursorFlags |= EXTTEXTINPUT_CURSOR_INVISIBLE;
                    break;
                }
                default:
                    SAL_WARN("vcl.qt", "Unhandled QInputMethodEvent attribute: "
                                           << static_cast<int>(rAttr.type));
                    break;
            }
        }

        const bool bIsEmpty = aInputEvent.maText.isEmpty();
        if (m_bNonEmptyIMPreeditSeen || !bIsEmpty)
        {
            SolarMutexGuard aGuard;
            vcl::DeletionListener aDel(&m_rFrame);
            m_rFrame.CallCallback(SalEvent::ExtTextInput, &aInputEvent);
            if (!aDel.isDeleted() && bIsEmpty)
                m_rFrame.CallCallback(SalEvent::EndExtTextInput, nullptr);
            m_bNonEmptyIMPreeditSeen = !bIsEmpty;
        }
    }

    pEvent->accept();
}

static bool lcl_retrieveSurrounding(sal_Int32& rPosition, sal_Int32& rAnchor, QString* pText,
                                    QString* pSelection)
{
    SolarMutexGuard aGuard;
    vcl::Window* pFocusWin = Application::GetFocusWindow();
    if (!pFocusWin)
        return false;

    uno::Reference<accessibility::XAccessibleEditableText> xText;
    try
    {
        xText = FindFocusedEditableText(pFocusWin->GetAccessible());
    }
    catch (const uno::Exception&)
    {
        TOOLS_WARN_EXCEPTION("vcl.qt", "Exception in getting input method surrounding text");
    }

    if (xText.is())
    {
        rPosition = xText->getCaretPosition();
        if (rPosition != -1)
        {
            if (pText)
                *pText = toQString(xText->getText());

            sal_Int32 nSelStart = xText->getSelectionStart();
            sal_Int32 nSelEnd = xText->getSelectionEnd();
            if (nSelStart == nSelEnd)
            {
                rAnchor = rPosition;
            }
            else
            {
                if (rPosition == nSelStart)
                    rAnchor = nSelEnd;
                else
                    rAnchor = nSelStart;
                if (pSelection)
                    *pSelection = toQString(xText->getSelectedText());
            }
            return true;
        }
    }

    return false;
}

QVariant QtWidget::inputMethodQuery(Qt::InputMethodQuery property) const
{
    switch (property)
    {
        case Qt::ImSurroundingText:
        {
            QString aText;
            sal_Int32 nCursorPos, nAnchor;
            if (lcl_retrieveSurrounding(nCursorPos, nAnchor, &aText, nullptr))
                return QVariant(aText);
            return QVariant();
        }
        case Qt::ImCursorPosition:
        {
            sal_Int32 nCursorPos, nAnchor;
            if (lcl_retrieveSurrounding(nCursorPos, nAnchor, nullptr, nullptr))
                return QVariant(static_cast<int>(nCursorPos));
            return QVariant();
        }
        case Qt::ImCursorRectangle:
        {
            if (!m_bInInputMethodQueryCursorRectangle)
            {
                m_bInInputMethodQueryCursorRectangle = true;
                SalExtTextInputPosEvent aPosEvent;
                m_rFrame.CallCallback(SalEvent::ExtTextInputPos, &aPosEvent);
                const qreal fRatio = m_rFrame.devicePixelRatioF();
                m_aImCursorRectangle.setRect(aPosEvent.mnX / fRatio, aPosEvent.mnY / fRatio,
                                             aPosEvent.mnWidth / fRatio,
                                             aPosEvent.mnHeight / fRatio);
                m_bInInputMethodQueryCursorRectangle = false;
            }
            return QVariant(m_aImCursorRectangle);
        }
        case Qt::ImAnchorPosition:
        {
            sal_Int32 nCursorPos, nAnchor;
            if (lcl_retrieveSurrounding(nCursorPos, nAnchor, nullptr, nullptr))
                return QVariant(static_cast<int>(nAnchor));
            return QVariant();
        }
        case Qt::ImCurrentSelection:
        {
            QString aSelection;
            sal_Int32 nCursorPos, nAnchor;
            if (lcl_retrieveSurrounding(nCursorPos, nAnchor, nullptr, &aSelection))
                return QVariant(aSelection);
            return QVariant();
        }
        default:
            return QWidget::inputMethodQuery(property);
    }
}

void QtWidget::endExtTextInput()
{
    if (m_bNonEmptyIMPreeditSeen)
    {
        m_rFrame.CallCallback(SalEvent::EndExtTextInput, nullptr);
        m_bNonEmptyIMPreeditSeen = false;
    }
}

void QtWidget::changeEvent(QEvent* pEvent)
{
    switch (pEvent->type())
    {
        case QEvent::FontChange:
            [[fallthrough]];
        case QEvent::PaletteChange:
            [[fallthrough]];
        case QEvent::StyleChange:
        {
            GetQtInstance().UpdateStyle(QEvent::FontChange == pEvent->type());
            break;
        }
        default:
            break;
    }
    QWidget::changeEvent(pEvent);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
