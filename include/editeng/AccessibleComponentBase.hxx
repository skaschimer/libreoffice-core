/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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

#ifndef INCLUDED_EDITENG_ACCESSIBLECOMPONENTBASE_HXX
#define INCLUDED_EDITENG_ACCESSIBLECOMPONENTBASE_HXX

#include <com/sun/star/accessibility/XAccessibleExtendedComponent.hpp>
#include <editeng/editengdllapi.h>

namespace accessibility
{
/** @descr
        This base class provides (will provide) a base implementation of the
        XAccessibleComponent and the
        XAccessibleExtendedComponent for all shapes.  For
        more detailed documentation about the methods refer to the interface
        descriptions of XAccessible,
        XAccessibleContext, <type>XInterface</type>,
        XServiceInfo, and <type>XTypeProvider</type>.
*/
class EDITENG_DLLPUBLIC AccessibleComponentBase
    : public css::accessibility::XAccessibleExtendedComponent
{
public:
    //=====  internal  ========================================================
    AccessibleComponentBase();
    virtual ~AccessibleComponentBase();

    //=====  XAccessibleComponent  ================================================

    /** The default implementation uses the result of
        <member>getBounds</member> to determine whether the given point lies
        inside this object.
    */
    virtual sal_Bool SAL_CALL containsPoint(const css::awt::Point& aPoint) override;

    /** The default implementation returns an empty reference.
    */
    virtual css::uno::Reference<css::accessibility::XAccessible>
        SAL_CALL getAccessibleAtPoint(const css::awt::Point& aPoint) override;

    /** The default implementation returns an empty rectangle.
    */
    virtual css::awt::Rectangle SAL_CALL getBounds() override;

    /** The default implementation uses the result of
        <member>getBounds</member> to determine the location.
    */
    virtual css::awt::Point SAL_CALL getLocation() override;

    /** The default implementation returns an empty position, i.e. the
    * result of the default constructor of css::awt::Point.
    */
    virtual css::awt::Point SAL_CALL getLocationOnScreen() override;

    /** The default implementation uses the result of
        <member>getBounds</member> to determine the size.
    */
    virtual css::awt::Size SAL_CALL getSize() override;

    /** The default implementation does nothing.
    */
    virtual void SAL_CALL grabFocus() override;

    /** Returns black as the default foreground color.
    */
    virtual sal_Int32 SAL_CALL getForeground() override;

    /** Returns white as the default background color.
    */
    virtual sal_Int32 SAL_CALL getBackground() override;

    //=====  XAccessibleExtendedComponent  ====================================
    virtual OUString SAL_CALL getTitledBorderText() override;
    virtual OUString SAL_CALL getToolTipText() override;

    //=====  XTypeProvider  ===================================================

    /// @throws css::uno::RuntimeException
    virtual css::uno::Sequence<css::uno::Type> SAL_CALL getTypes();
};

} // end of namespace accessibility

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
