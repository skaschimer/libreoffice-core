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

#pragma once

#include <config_vclplug.h>

#include <QtCore/QDate>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QMessageBox>

#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <tools/color.hxx>
#include <tools/date.hxx>
#include <tools/gen.hxx>
#include <vcl/bitmap/BitmapTypes.hxx>
#include <vcl/event.hxx>
#include <vcl/qt/QtUtils.hxx>
#include <vcl/vclenum.hxx>

#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/uno/Sequence.hxx>
#include <com/sun/star/datatransfer/dnd/DNDConstants.hpp>

#include <memory>

class Image;
class QImage;

inline QRect toQRect(const tools::Rectangle& rRect)
{
    return QRect(rRect.Left(), rRect.Top(), rRect.GetWidth(), rRect.GetHeight());
}

inline QRect toQRect(const AbsoluteScreenPixelRectangle& rRect, const qreal fScale)
{
    return QRect(floor(rRect.Left() * fScale), floor(rRect.Top() * fScale),
                 ceil(rRect.GetWidth() * fScale), ceil(rRect.GetHeight() * fScale));
}

inline QRect scaledQRect(const QRect& rRect, const qreal fScale)
{
    return QRect(floor(rRect.x() * fScale), floor(rRect.y() * fScale), ceil(rRect.width() * fScale),
                 ceil(rRect.height() * fScale));
}

inline tools::Rectangle toRectangle(const QRect& rRect)
{
    return tools::Rectangle(rRect.left(), rRect.top(), rRect.right(), rRect.bottom());
}

inline QSize toQSize(const Size& rSize) { return QSize(rSize.Width(), rSize.Height()); }

inline Size toSize(const QSize& rSize) { return Size(rSize.width(), rSize.height()); }

inline QPoint toQPoint(const Point& rPoint) { return QPoint(rPoint.X(), rPoint.Y()); }

inline Point toPoint(const QPoint& rPoint) { return Point(rPoint.x(), rPoint.y()); }

inline QColor toQColor(const Color& rColor)
{
    return QColor(rColor.GetRed(), rColor.GetGreen(), rColor.GetBlue(), rColor.GetAlpha());
}

inline Color toColor(const QColor& rColor)
{
    return Color(rColor.red(), rColor.green(), rColor.blue());
}

inline QDate toQDate(const Date& rDate)
{
    return QDate(rDate.GetYear(), rDate.GetMonth(), rDate.GetDay());
}

inline Date toDate(const QDate& rDate) { return Date(rDate.day(), rDate.month(), rDate.year()); }

Qt::CheckState toQtCheckState(TriState eTristate);
TriState toVclTriState(Qt::CheckState eTristate);

Qt::DropActions toQtDropActions(sal_Int8 dragOperation);
sal_Int8 toVclDropActions(Qt::DropActions dragOperation);
sal_Int8 toVclDropAction(Qt::DropAction dragOperation);

inline QList<int> toQList(const css::uno::Sequence<sal_Int32>& aSequence)
{
    QList<int> aList;
    for (sal_Int32 i : aSequence)
    {
        aList.append(i);
    }
    return aList;
}

constexpr QImage::Format Qt_DefaultFormat32 = QImage::Format_ARGB32;

inline QImage::Format getBitFormat(vcl::PixelFormat ePixelFormat)
{
    switch (ePixelFormat)
    {
        case vcl::PixelFormat::N8_BPP:
            return QImage::Format_Indexed8;
        case vcl::PixelFormat::N24_BPP:
            return QImage::Format_RGB888;
        case vcl::PixelFormat::N32_BPP:
            return Qt_DefaultFormat32;
        default:
            std::abort();
            break;
    }
    return QImage::Format_Invalid;
}

inline sal_uInt16 getFormatBits(QImage::Format eFormat)
{
    switch (eFormat)
    {
        case QImage::Format_Mono:
            return 1;
        case QImage::Format_Indexed8:
            return 8;
        case QImage::Format_RGB888:
            return 24;
        case Qt_DefaultFormat32:
        case QImage::Format_ARGB32_Premultiplied:
            return 32;
        default:
            std::abort();
            return 0;
    }
}

typedef struct _cairo_surface cairo_surface_t;
struct CairoDeleter
{
    void operator()(cairo_surface_t* pSurface) const;
};

typedef std::unique_ptr<cairo_surface_t, CairoDeleter> UniqueCairoSurface;

sal_uInt16 toVclKeyboardModifiers(Qt::KeyboardModifiers eKeyModifiers);
sal_uInt16 toVclKeyCode(int nKeyval, Qt::KeyboardModifiers eModifiers);
KeyEvent toVclKeyEvent(QKeyEvent& rEvent);
sal_uInt16 toVclMouseButtons(Qt::MouseButtons eButtons);
MouseEvent toVclMouseEvent(QMouseEvent& rEvent);

QImage toQImage(const Image& rImage);

QFont toQtFont(const vcl::Font& rVclFont);

bool toVclFont(const QFont& rQFont, const css::lang::Locale& rLocale, vcl::Font& rVclFont);

QMessageBox::Icon vclMessageTypeToQtIcon(VclMessageType eType);
QString vclMessageTypeToQtTitle(VclMessageType eType);

/** Converts a string potentially containing a '~' character to indicate an accelerator
 *  to the Qt variant using '&' for the accelerator.
 */
QString vclToQtStringWithAccelerator(const OUString& rText);

/** Converts a string potentially containing a '&' character to indicate an accelerator
 *  to the VCL variant using '~' for the accelerator.
 */
OUString qtToVclStringWithAccelerator(const QString& rText);

template <typename charT, typename traits>
inline std::basic_ostream<charT, traits>& operator<<(std::basic_ostream<charT, traits>& stream,
                                                     const QString& rString)
{
    return stream << toOUString(rString);
}

template <typename charT, typename traits>
inline std::basic_ostream<charT, traits>& operator<<(std::basic_ostream<charT, traits>& stream,
                                                     const QRect& rRect)
{
    return stream << toRectangle(rRect);
}

template <typename charT, typename traits>
inline std::basic_ostream<charT, traits>& operator<<(std::basic_ostream<charT, traits>& stream,
                                                     const QSize& rSize)
{
    return stream << toSize(rSize);
}

template <typename charT, typename traits>
inline std::basic_ostream<charT, traits>& operator<<(std::basic_ostream<charT, traits>& stream,
                                                     const QPoint& rPoint)
{
    return stream << toPoint(rPoint);
}

#define CHECK_QT5_USING_X11 (QT_VERSION < QT_VERSION_CHECK(6, 0, 0) && QT5_USING_X11)

#define CHECK_QT6_USING_X11 (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) && QT6_USING_X11)

#define CHECK_ANY_QT_USING_X11 CHECK_QT5_USING_X11 || CHECK_QT6_USING_X11

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
