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

#pragma once

#include <svl/numformat.hxx>
#include <xmloff/xmlictxt.hxx>
#include <xmloff/xmlimppr.hxx>
#include <xmloff/xmlnumfi.hxx>
#include "sdxmlimp_impl.hxx"
#include "ximppage.hxx"
#include <xmloff/xmlstyle.hxx>
#include <com/sun/star/view/PaperOrientation.hpp>
#include <com/sun/star/drawing/XDrawPages2.hpp>
#include <memory>
#include <vector>

// special style:style context inside style:page-master context

class SdXMLPageMasterStyleContext: public SvXMLStyleContext
{
    sal_Int32                   mnBorderBottom;
    sal_Int32                   mnBorderLeft;
    sal_Int32                   mnBorderRight;
    sal_Int32                   mnBorderTop;
    sal_Int32                   mnWidth;
    sal_Int32                   mnHeight;
    css::view::PaperOrientation meOrientation;

    const SdXMLImport& GetSdImport() const { return static_cast<const SdXMLImport&>(GetImport()); }
    SdXMLImport& GetSdImport() { return static_cast<SdXMLImport&>(GetImport()); }

public:

    SdXMLPageMasterStyleContext(
        SdXMLImport& rImport,
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList);
    virtual ~SdXMLPageMasterStyleContext() override;

    sal_Int32 GetBorderBottom() const { return mnBorderBottom; }
    sal_Int32 GetBorderLeft() const { return mnBorderLeft; }
    sal_Int32 GetBorderRight() const { return mnBorderRight; }
    sal_Int32 GetBorderTop() const { return mnBorderTop; }
    sal_Int32 GetWidth() const { return mnWidth; }
    sal_Int32 GetHeight() const { return mnHeight; }
    css::view::PaperOrientation GetOrientation() const { return meOrientation; }
};

// style:page-master context

class SdXMLPageMasterContext: public SvXMLStyleContext
{
    rtl::Reference<SdXMLPageMasterStyleContext> mxPageMasterStyle;

    const SdXMLImport& GetSdImport() const { return static_cast<const SdXMLImport&>(GetImport()); }
    SdXMLImport& GetSdImport() { return static_cast<SdXMLImport&>(GetImport()); }

public:

    SdXMLPageMasterContext(
        SdXMLImport& rImport,
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList);

    virtual css::uno::Reference< css::xml::sax::XFastContextHandler > SAL_CALL createFastChildContext(
        sal_Int32 nElement, const css::uno::Reference< css::xml::sax::XFastAttributeList >& AttrList ) override;

    const SdXMLPageMasterStyleContext* GetPageMasterStyle() const { return mxPageMasterStyle.get(); }
};

// style:masterpage context

class SdXMLMasterPageContext: public SdXMLGenericPageContext
{
    OUString               msName;
    OUString               msDisplayName;

public:

    // called for normal master page
    SdXMLMasterPageContext(
        SdXMLImport& rImport,
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList,
        css::uno::Reference< css::drawing::XDrawPages2 > const & xMasterPages);
    // Called for handout master page
    SdXMLMasterPageContext(
        SdXMLImport& rImport,
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList,
        css::uno::Reference< css::drawing::XShapes > const & rShapes);
    virtual ~SdXMLMasterPageContext() override;

    virtual css::uno::Reference< css::xml::sax::XFastContextHandler > SAL_CALL createFastChildContext(
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& AttrList ) override;

    virtual void SAL_CALL endFastElement(sal_Int32 nElement) override;

    const OUString& GetDisplayName() const { return msDisplayName; }

};

// presentation:placeholder context

class SdXMLPresentationPlaceholderContext: public SvXMLImportContext
{
    OUString               msName;
    sal_Int32                   mnX;

    const SdXMLImport& GetSdImport() const { return static_cast<const SdXMLImport&>(GetImport()); }
    SdXMLImport& GetSdImport() { return static_cast<SdXMLImport&>(GetImport()); }

public:
    SdXMLPresentationPlaceholderContext(
        SdXMLImport& rImport,
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList);
    virtual ~SdXMLPresentationPlaceholderContext() override;

    const OUString& GetName() const { return msName; }
    sal_Int32 GetX() const { return mnX; }
};

// style:presentation-page-layout context

class SdXMLPresentationPageLayoutContext: public SvXMLStyleContext
{
    std::vector< rtl::Reference< SdXMLPresentationPlaceholderContext > >
                           maList;
    sal_uInt16             mnTypeId;

    const SdXMLImport& GetSdImport() const { return static_cast<const SdXMLImport&>(GetImport()); }
    SdXMLImport& GetSdImport() { return static_cast<SdXMLImport&>(GetImport()); }

public:

    SdXMLPresentationPageLayoutContext(
        SdXMLImport& rImport,
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList);

    virtual css::uno::Reference< css::xml::sax::XFastContextHandler > SAL_CALL createFastChildContext(
        sal_Int32 nElement, const css::uno::Reference< css::xml::sax::XFastAttributeList >& AttrList ) override;

    virtual void SAL_CALL endFastElement(sal_Int32 nElement) override;

    sal_uInt16 GetTypeId() const { return mnTypeId; }
};

// office:styles context

class SdXMLStylesContext : public SvXMLStylesContext
{
    bool                    mbIsAutoStyle;
    std::unique_ptr<SvXMLNumFmtHelper> mpNumFmtHelper;
    std::unique_ptr<SvNumberFormatter> mpNumFormatter;

    const SdXMLImport& GetSdImport() const { return static_cast<const SdXMLImport&>(GetImport()); }
    SdXMLImport& GetSdImport() { return static_cast<SdXMLImport&>(GetImport()); }

    void ImpSetGraphicStyles() const;
    void ImpSetCellStyles() const;
    void ImpSetGraphicStyles( css::uno::Reference< css::container::XNameAccess > const & xPageStyles,
        XmlStyleFamily nFamily, const OUString& rPrefix) const;

protected:
    using SvXMLStylesContext::CreateStyleChildContext;
    virtual SvXMLStyleContext* CreateStyleChildContext(
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList) override;

    using SvXMLStylesContext::CreateStyleStyleChildContext;
    virtual SvXMLStyleContext *CreateStyleStyleChildContext(
        XmlStyleFamily nFamily,
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList) override;

    using SvXMLStylesContext::CreateDefaultStyleStyleChildContext;
    virtual SvXMLStyleContext *CreateDefaultStyleStyleChildContext(
        XmlStyleFamily nFamily,
        sal_Int32 nElement,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList) override;
public:

    SdXMLStylesContext(
        SdXMLImport& rImport,
        bool bIsAutoStyle);

    virtual void SAL_CALL endFastElement(sal_Int32 nElement) override;
    virtual SvXMLImportPropertyMapper* GetImportPropertyMapper(XmlStyleFamily nFamily) const override;

    void SetMasterPageStyles(SdXMLMasterPageContext const & rMaster) const;

    css::uno::Reference< css::container::XNameAccess > getPageLayouts() const;
};

// office:master-styles context

class SdXMLMasterStylesContext : public SvXMLImportContext
{
    std::vector< rtl::Reference< SdXMLMasterPageContext > > maMasterPageList;

    const SdXMLImport& GetSdImport() const { return static_cast<const SdXMLImport&>(GetImport()); }
    SdXMLImport& GetSdImport() { return static_cast<SdXMLImport&>(GetImport()); }

public:

    SdXMLMasterStylesContext(SdXMLImport& rImport);

    virtual css::uno::Reference< css::xml::sax::XFastContextHandler > SAL_CALL createFastChildContext(
                sal_Int32 nElement, const css::uno::Reference< css::xml::sax::XFastAttributeList >& AttrList ) override;
};

// <pres:header-decl>, <pres:footer-decl> and <pres:date-time-decl>

class SdXMLHeaderFooterDeclContext : public SvXMLStyleContext
{
public:
    SdXMLHeaderFooterDeclContext( SvXMLImport& rImport,
        const css::uno::Reference< css::xml::sax::XFastAttributeList >& xAttrList );

    virtual bool IsTransient() const override;
    virtual void SAL_CALL endFastElement(sal_Int32 ) override;
    virtual void SAL_CALL characters( const OUString& rChars ) override;

private:
    OUString maStrName;
    OUString maStrText;
    OUString maStrDateTimeFormat;
    bool        mbFixed;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
