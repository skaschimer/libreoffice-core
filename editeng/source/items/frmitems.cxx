/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http: // mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http: // www.apache.org/licenses/LICENSE-2.0 .
 */

#include <memory>
#include <com/sun/star/uno/Any.hxx>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/script/Converter.hpp>
#include <com/sun/star/table/ShadowLocation.hpp>
#include <com/sun/star/table/ShadowFormat.hpp>
#include <com/sun/star/table/BorderLine2.hpp>
#include <com/sun/star/table/BorderLineStyle.hpp>
#include <com/sun/star/style/BreakType.hpp>
#include <com/sun/star/style/GraphicLocation.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/beans/Pair.hpp>
#include <com/sun/star/text/WritingMode2.hpp>
#include <com/sun/star/frame/status/UpperLowerMarginScale.hpp>
#include <com/sun/star/frame/status/LeftRightMarginScale.hpp>
#include <com/sun/star/drawing/ShadingPattern.hpp>
#include <com/sun/star/graphic/XGraphic.hpp>
#include <com/sun/star/util/XComplexColor.hpp>

#include <osl/diagnose.h>
#include <i18nutil/unicode.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <comphelper/processfactory.hxx>
#include <utility>
#include <vcl/GraphicObject.hxx>
#include <tools/urlobj.hxx>
#include <tools/bigint.hxx>
#include <svl/memberid.h>
#include <rtl/math.hxx>
#include <rtl/ustring.hxx>
#include <tools/mapunit.hxx>
#include <tools/UnitConversion.hxx>
#include <vcl/graphicfilter.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <editeng/editrids.hrc>
#include <editeng/pbinitem.hxx>
#include <editeng/sizeitem.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/prntitem.hxx>
#include <editeng/opaqitem.hxx>
#include <editeng/protitem.hxx>
#include <editeng/shaditem.hxx>
#include <editeng/borderline.hxx>
#include <editeng/boxitem.hxx>
#include <editeng/formatbreakitem.hxx>
#include <editeng/keepitem.hxx>
#include <editeng/lineitem.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/frmdiritem.hxx>
#include <editeng/itemtype.hxx>
#include <editeng/eerdll.hxx>
#include <editeng/memberids.h>
#include <libxml/xmlwriter.h>
#include <o3tl/enumrange.hxx>
#include <o3tl/safeint.hxx>
#include <sal/log.hxx>
#include <sax/tools/converter.hxx>
#include <vcl/GraphicLoader.hxx>
#include <unotools/securityoptions.hxx>
#include <docmodel/uno/UnoComplexColor.hxx>

#include <boost/property_tree/ptree.hpp>

using namespace ::editeng;
using namespace ::com::sun::star;
using namespace ::com::sun::star::drawing;
using namespace ::com::sun::star::table::BorderLineStyle;


SfxPoolItem* SvxPaperBinItem::CreateDefault() { return new  SvxPaperBinItem(0);}
SfxPoolItem* SvxSizeItem::CreateDefault() { return new  SvxSizeItem(0);}
SfxPoolItem* SvxLRSpaceItem::CreateDefault() { return new  SvxLRSpaceItem(0);}
SfxPoolItem* SvxULSpaceItem::CreateDefault() { return new  SvxULSpaceItem(0);}
SfxPoolItem* SvxProtectItem::CreateDefault() { return new  SvxProtectItem(0);}
SfxPoolItem* SvxBrushItem::CreateDefault() { return new  SvxBrushItem(0);}
SfxPoolItem* SvxShadowItem::CreateDefault() { return new  SvxShadowItem(0);}
SfxPoolItem* SvxBoxItem::CreateDefault() { return new  SvxBoxItem(0);}
SfxPoolItem* SvxBoxInfoItem::CreateDefault() { return new  SvxBoxInfoItem(0);}
SfxPoolItem* SvxFormatBreakItem::CreateDefault() { return new  SvxFormatBreakItem(SvxBreak::NONE, 0);}
SfxPoolItem* SvxFormatKeepItem::CreateDefault() { return new  SvxFormatKeepItem(false, 0);}
SfxPoolItem* SvxLineItem::CreateDefault() { return new  SvxLineItem(0);}

SvxPaperBinItem* SvxPaperBinItem::Clone( SfxItemPool* ) const
{
    return new SvxPaperBinItem( *this );
}

bool SvxPaperBinItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&
)   const
{
    switch ( ePres )
    {
        case SfxItemPresentation::Nameless:
            rText = OUString::number( GetValue() );
            return true;

        case SfxItemPresentation::Complete:
        {
            sal_uInt8 nValue = GetValue();

            if ( PAPERBIN_PRINTER_SETTINGS == nValue )
                rText = EditResId(RID_SVXSTR_PAPERBIN_SETTINGS);
            else
            {
                rText = EditResId(RID_SVXSTR_PAPERBIN) + " " + OUString::number( nValue );
            }
            return true;
        }
        //no break necessary
        default: ; //prevent warning
    }

    return false;
}


SvxSizeItem::SvxSizeItem( const sal_uInt16 nId, const Size& rSize ) :

    SfxPoolItem( nId ),

    m_aSize( rSize )
{
}


bool SvxSizeItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    awt::Size aTmp(m_aSize.Width(), m_aSize.Height());
    if( bConvert )
    {
        aTmp.Height = convertTwipToMm100(aTmp.Height);
        aTmp.Width = convertTwipToMm100(aTmp.Width);
    }

    switch( nMemberId )
    {
        case MID_SIZE_SIZE:  rVal <<= aTmp; break;
        case MID_SIZE_WIDTH: rVal <<= aTmp.Width; break;
        case MID_SIZE_HEIGHT: rVal <<= aTmp.Height;  break;
        default: OSL_FAIL("Wrong MemberId!"); return false;
    }

    return true;
}


bool SvxSizeItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    switch( nMemberId )
    {
        case MID_SIZE_SIZE:
        {
            awt::Size aTmp;
            if( rVal >>= aTmp )
            {
                if(bConvert)
                {
                    aTmp.Height = o3tl::toTwips(aTmp.Height, o3tl::Length::mm100);
                    aTmp.Width = o3tl::toTwips(aTmp.Width, o3tl::Length::mm100);
                }
                m_aSize = Size( aTmp.Width, aTmp.Height );
            }
            else
            {
                return false;
            }
        }
        break;
        case MID_SIZE_WIDTH:
        {
            sal_Int32 nVal = 0;
            if(!(rVal >>= nVal ))
                return false;

            m_aSize.setWidth( bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal );
        }
        break;
        case MID_SIZE_HEIGHT:
        {
            sal_Int32 nVal = 0;
            if(!(rVal >>= nVal))
                return true;

            m_aSize.setHeight( bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal );
        }
        break;
        default: OSL_FAIL("Wrong MemberId!");
            return false;
    }
    return true;
}


SvxSizeItem::SvxSizeItem( const sal_uInt16 nId ) :

    SfxPoolItem( nId )
{
}


bool SvxSizeItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    return ( m_aSize == static_cast<const SvxSizeItem&>( rAttr ).GetSize() );
}

SvxSizeItem* SvxSizeItem::Clone( SfxItemPool* ) const
{
    return new SvxSizeItem( *this );
}

bool SvxSizeItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    OUString cpDelimTmp(cpDelim);
    switch ( ePres )
    {
        case SfxItemPresentation::Nameless:
            rText = GetMetricText( m_aSize.Width(), eCoreUnit, ePresUnit, &rIntl ) +
                    cpDelimTmp +
                    GetMetricText( m_aSize.Height(), eCoreUnit, ePresUnit, &rIntl );
            return true;

        case SfxItemPresentation::Complete:
            rText = EditResId(RID_SVXITEMS_SIZE_WIDTH) +
                    GetMetricText( m_aSize.Width(), eCoreUnit, ePresUnit, &rIntl ) +
                    " " + EditResId(GetMetricId(ePresUnit)) +
                    cpDelimTmp +
                    EditResId(RID_SVXITEMS_SIZE_HEIGHT) +
                    GetMetricText( m_aSize.Height(), eCoreUnit, ePresUnit, &rIntl ) +
                    " " + EditResId(GetMetricId(ePresUnit));
            return true;
        // no break necessary
        default: ; // prevent warning

    }
    return false;
}


void SvxSizeItem::ScaleMetrics( tools::Long nMult, tools::Long nDiv )
{
    m_aSize.setWidth( BigInt::Scale( m_aSize.Width(), nMult, nDiv ) );
    m_aSize.setHeight( BigInt::Scale( m_aSize.Height(), nMult, nDiv ) );
}


bool SvxSizeItem::HasMetrics() const
{
    return true;
}

double SvxIndentValue::ResolveDouble(const SvxFontUnitMetrics& rMetrics) const
{
    if (m_nUnit == css::util::MeasureUnit::TWIP)
        return m_dValue;

    SAL_WARN_IF(!rMetrics.m_bInitialized, "editeng", "font-relative indentation lost");

    switch (m_nUnit)
    {
        case css::util::MeasureUnit::FONT_EM:
            return m_dValue * rMetrics.m_dEmTwips;

        case css::util::MeasureUnit::FONT_CJK_ADVANCE:
            return m_dValue * rMetrics.m_dIcTwips;

        default:
            SAL_WARN("editeng", "unhandled type conversion");
            return 0.0;
    }
}

sal_Int32 SvxIndentValue::Resolve(const SvxFontUnitMetrics& rMetrics) const
{
    return static_cast<sal_Int32>(std::llround(ResolveDouble(rMetrics)));
}

sal_Int32 SvxIndentValue::ResolveFixedPart() const
{
    if (m_nUnit == css::util::MeasureUnit::TWIP)
        return Resolve({});

    return 0;
}

sal_Int32 SvxIndentValue::ResolveVariablePart(const SvxFontUnitMetrics& rMetrics) const
{
    if (m_nUnit == css::util::MeasureUnit::TWIP)
        return 0;

    return Resolve(rMetrics);
}

void SvxIndentValue::ScaleMetrics(tools::Long const nMult, tools::Long const nDiv)
{
    m_dValue = (m_dValue * static_cast<double>(nMult)) / static_cast<double>(nDiv);
}

size_t SvxIndentValue::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, m_dValue);
    o3tl::hash_combine(seed, m_nUnit);
    return seed;
}

namespace
{

boost::property_tree::ptree lcl_IndentValueToJson(const char* aName, SvxIndentValue stValue)
{
    boost::property_tree::ptree aState;

    switch (stValue.m_nUnit)
    {
        case css::util::MeasureUnit::TWIP:
        {
            OUString sValue
                = GetMetricText(stValue.m_dValue, MapUnit::MapTwip, MapUnit::MapInch, nullptr);
            aState.put(aName, sValue);
            aState.put("unit", "inch");
        }
        break;

        case css::util::MeasureUnit::FONT_EM:
            aState.put(aName, stValue.m_dValue);
            aState.put("unit", "em");
            break;

        case css::util::MeasureUnit::FONT_CJK_ADVANCE:
            aState.put(aName, stValue.m_dValue);
            aState.put("unit", "ic");
            break;

        default:
            SAL_WARN("editeng", "unhandled type conversion");
            break;
    }

    return aState;
}

bool lcl_FillAbsoluteMeasureAny(const SvxIndentValue& rIndent, uno::Any& rVal, bool bConvert)
{
    if (rIndent.m_nUnit == css::util::MeasureUnit::TWIP)
    {
        auto nConvOffset = (bConvert ? convertTwipToMm100(rIndent.m_dValue) : rIndent.m_dValue);
        rVal <<= static_cast<sal_Int32>(std::llround(nConvOffset));
        return true;
    }

    return false;
}

bool lcl_FillRelativeMeasureAny(const SvxIndentValue& rIndent, uno::Any& rVal)
{
    if (rIndent.m_nUnit != css::util::MeasureUnit::TWIP)
    {
        rVal <<= css::beans::Pair<double, sal_Int16>{ rIndent.m_dValue, rIndent.m_nUnit };
        return true;
    }

    return false;
}

}

SvxLRSpaceItem::SvxLRSpaceItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
    , m_nGutterMargin(0)
    , m_nRightGutterMargin(0),
    nPropFirstLineOffset( 100 ),
    nPropLeftMargin( 100 ),
    nPropRightMargin( 100 ),
    bAutoFirst      ( false ),
    bExplicitZeroMarginValRight(false),
    bExplicitZeroMarginValLeft(false)
{
}

SvxLRSpaceItem::SvxLRSpaceItem(SvxIndentValue stLeft, SvxIndentValue stRight,
                               SvxIndentValue stOffset, const sal_uInt16 nId)
    : SfxPoolItem(nId)
    , m_nGutterMargin(0)
    , m_nRightGutterMargin(0)
    , nPropFirstLineOffset(100)
    , nPropLeftMargin(100)
    , nPropRightMargin(100)
    , bAutoFirst(false)
    , bExplicitZeroMarginValRight(false)
    , bExplicitZeroMarginValLeft(false)
{
    SetLeft(stLeft);
    SetRight(stRight);
    SetTextFirstLineOffset(stOffset);
}


bool SvxLRSpaceItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    bool bRet = true;
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    switch( nMemberId )
    {
        // now all signed
        case 0:
        {
            css::frame::status::LeftRightMarginScale aLRSpace;

            auto nLeftTwips = ResolveLeft({});
            aLRSpace.Left
                = static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nLeftTwips) : nLeftTwips);

            auto nTextLeftTwips = ResolveTextLeft({});
            aLRSpace.TextLeft = static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nTextLeftTwips)
                                                                : nTextLeftTwips);

            auto nRightTwips = ResolveRight({});
            aLRSpace.Right
                = static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nRightTwips) : nRightTwips);

            aLRSpace.ScaleLeft = static_cast<sal_Int16>(nPropLeftMargin);
            aLRSpace.ScaleRight = static_cast<sal_Int16>(nPropRightMargin);

            auto nFirstLineOffsetTwips = ResolveTextFirstLineOffset({});
            aLRSpace.FirstLine = static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nFirstLineOffsetTwips) : nFirstLineOffsetTwips);

            aLRSpace.ScaleFirstLine = static_cast<sal_Int16>(nPropFirstLineOffset);
            aLRSpace.AutoFirstLine = IsAutoFirst();
            rVal <<= aLRSpace;
            break;
        }
        case MID_L_MARGIN:
            bRet = lcl_FillAbsoluteMeasureAny(GetLeft(), rVal, bConvert);
            break;

        case MID_TXT_LMARGIN:
            bRet = lcl_FillAbsoluteMeasureAny(GetTextLeft(), rVal, bConvert);
            break;

        case MID_L_UNIT_MARGIN:
            bRet = lcl_FillRelativeMeasureAny(GetTextLeft(), rVal);
            break;

        case MID_R_MARGIN:
            bRet = lcl_FillAbsoluteMeasureAny(m_stRightMargin, rVal, bConvert);
            break;
        case MID_R_UNIT_MARGIN:
            bRet = lcl_FillRelativeMeasureAny(m_stRightMargin, rVal);
            break;

        case MID_L_REL_MARGIN:
            rVal <<= static_cast<sal_Int16>(nPropLeftMargin);
        break;
        case MID_R_REL_MARGIN:
            rVal <<= static_cast<sal_Int16>(nPropRightMargin);
        break;

        case MID_FIRST_LINE_INDENT:
            bRet = lcl_FillAbsoluteMeasureAny(m_stFirstLineOffset, rVal, bConvert);
            break;

        case MID_FIRST_LINE_REL_INDENT:
            rVal <<= static_cast<sal_Int16>(nPropFirstLineOffset);
            break;

        case MID_FIRST_LINE_UNIT_INDENT:
            bRet = lcl_FillRelativeMeasureAny(m_stFirstLineOffset, rVal);
            break;

        case MID_FIRST_AUTO:
            rVal <<= IsAutoFirst();
            break;

        case MID_GUTTER_MARGIN:
            rVal <<= static_cast<sal_Int32>(bConvert ? convertTwipToMm100(m_nGutterMargin)
                                                     : m_nGutterMargin);
            break;

        default:
            bRet = false;
            // SfxDispatchController_Impl::StateChanged calls this with hardcoded 0 triggering this; there used to be a MID_LR_MARGIN 0 but what type would it have?
            OSL_FAIL("unknown MemberId");
    }
    return bRet;
}


bool SvxLRSpaceItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    bool bConvert = 0 != (nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    sal_Int32 nVal = 0;
    if (nMemberId != 0 && nMemberId != MID_FIRST_AUTO && nMemberId != MID_L_REL_MARGIN
        && nMemberId != MID_R_REL_MARGIN && nMemberId != MID_FIRST_LINE_UNIT_INDENT
        && nMemberId != MID_L_UNIT_MARGIN && nMemberId != MID_R_UNIT_MARGIN)
        if (!(rVal >>= nVal))
            return false;

    switch( nMemberId )
    {
        case 0:
        {
            css::frame::status::LeftRightMarginScale aLRSpace;
            if(!(rVal >>= aLRSpace))
                return false;

            SetLeft(SvxIndentValue::twips(
                bConvert ? o3tl::toTwips(aLRSpace.Left, o3tl::Length::mm100) : aLRSpace.Left));
            SetTextLeft(SvxIndentValue::twips(
                bConvert ? o3tl::toTwips(aLRSpace.TextLeft, o3tl::Length::mm100)
                         : aLRSpace.TextLeft));
            SetRight(SvxIndentValue::twips(
                bConvert ? o3tl::toTwips(aLRSpace.Right, o3tl::Length::mm100) : aLRSpace.Right));
            nPropLeftMargin = aLRSpace.ScaleLeft;
            nPropRightMargin = aLRSpace.ScaleRight;
            SetTextFirstLineOffset(SvxIndentValue::twips(
                bConvert ? o3tl::toTwips(aLRSpace.FirstLine, o3tl::Length::mm100)
                         : aLRSpace.FirstLine));
            SetPropTextFirstLineOffset ( aLRSpace.ScaleFirstLine );
            SetAutoFirst( aLRSpace.AutoFirstLine );
            break;
        }
        case MID_L_MARGIN:
            SetLeft(
                SvxIndentValue::twips(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal));
            break;

        case MID_TXT_LMARGIN:
            SetTextLeft(
                SvxIndentValue::twips(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal));
            break;

        case MID_L_UNIT_MARGIN:
        {
            css::beans::Pair<double, sal_Int16> stVal;
            if (!(rVal >>= stVal))
            {
                return false;
            }

            SetTextLeft(SvxIndentValue{ stVal.First, stVal.Second });
            break;
        }

        case MID_R_MARGIN:
            SetRight(
                SvxIndentValue::twips(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal));
            break;

        case MID_R_UNIT_MARGIN:
        {
            css::beans::Pair<double, sal_Int16> stVal;
            if (!(rVal >>= stVal))
            {
                return false;
            }

            SetRight(SvxIndentValue{ stVal.First, stVal.Second });
            break;
        }

        case MID_L_REL_MARGIN:
        case MID_R_REL_MARGIN:
        {
            sal_Int32 nRel = 0;
            if((rVal >>= nRel) && nRel >= 0 && nRel < SAL_MAX_UINT16)
            {
                if(MID_L_REL_MARGIN== nMemberId)
                    nPropLeftMargin = static_cast<sal_uInt16>(nRel);
                else
                    nPropRightMargin = static_cast<sal_uInt16>(nRel);
            }
            else
                return false;
        }
        break;
        case MID_FIRST_LINE_INDENT:
            SetTextFirstLineOffset(
                SvxIndentValue::twips(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal));
            break;

        case MID_FIRST_LINE_REL_INDENT:
            SetPropTextFirstLineOffset ( nVal );
            break;

        case MID_FIRST_LINE_UNIT_INDENT:
        {
            css::beans::Pair<double, sal_Int16> stVal;
            if (!(rVal >>= stVal))
            {
                return false;
            }

            SetTextFirstLineOffset(SvxIndentValue{ stVal.First, stVal.Second });
            break;
        }

        case MID_FIRST_AUTO:
            SetAutoFirst( Any2Bool(rVal) );
            break;

        case MID_GUTTER_MARGIN:
            SetGutterMargin(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal);
            break;

        default:
            OSL_FAIL("unknown MemberId");
            return false;
    }
    return true;
}

void SvxLeftMarginItem::SetLeft(const tools::Long nL, const sal_uInt16 nProp)
{
    m_nLeftMargin = (nL * nProp) / 100;
    m_nPropLeftMargin = nProp;
}

void SvxLRSpaceItem::SetLeft(SvxIndentValue stL, const sal_uInt16 nProp)
{
    SAL_WARN_IF(m_stFirstLineOffset.m_dValue != 0.0, "editeng",
                "probably call SetTextLeft instead? looks inconsistent otherwise");

    m_stLeftMargin = stL;
    nPropLeftMargin = nProp;

    if (nProp != 100)
    {
        m_stLeftMargin.m_dValue = (stL.m_dValue * static_cast<double>(nProp)) / 100.0;
    }
}

SvxIndentValue SvxLRSpaceItem::GetLeft() const { return m_stLeftMargin; }

sal_Int32 SvxLRSpaceItem::ResolveLeft(const SvxFontUnitMetrics& rMetrics) const
{
    return m_stLeftMargin.Resolve(rMetrics);
}

void SvxRightMarginItem::SetRight(SvxIndentValue stR, const sal_uInt16 nProp)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;

    m_stRightMargin = stR;
    m_nPropRightMargin = nProp;

    if (nProp != 100)
    {
        m_stRightMargin.m_dValue = (stR.m_dValue * static_cast<double>(nProp)) / 100.0;
    }
}

SvxIndentValue SvxRightMarginItem::GetRight() const { return m_stRightMargin; }

SvxIndentValue SvxLRSpaceItem::GetRight() const { return m_stRightMargin; }

sal_Int32 SvxRightMarginItem::ResolveRight(const SvxFontUnitMetrics& rMetrics) const
{
    return m_stRightMargin.Resolve(rMetrics);
}

sal_Int32 SvxLRSpaceItem::ResolveRight(const SvxFontUnitMetrics& rMetrics) const
{
    return m_stRightMargin.Resolve(rMetrics);
}

sal_Int32 SvxRightMarginItem::ResolveRightFixedPart() const
{
    return m_stRightMargin.ResolveFixedPart();
}

sal_Int32 SvxRightMarginItem::ResolveRightVariablePart(const SvxFontUnitMetrics& rMetrics) const
{
    return m_stRightMargin.ResolveVariablePart(rMetrics);
}

sal_uInt16 SvxRightMarginItem::GetPropRight() const { return m_nPropRightMargin; }

void SvxLRSpaceItem::SetRight(SvxIndentValue stR, const sal_uInt16 nProp)
{
    if (0.0 == stR.m_dValue)
    {
        SetExplicitZeroMarginValRight(true);
    }

    m_stRightMargin = stR;
    nPropRightMargin = nProp;

    if (nProp != 100)
    {
        m_stRightMargin.m_dValue = (stR.m_dValue * static_cast<double>(nProp)) / 100.0;
    }
}

void SvxLRSpaceItem::SetTextFirstLineOffset(SvxIndentValue stValue, sal_uInt16 nProp)
{
    // note: left margin contains any negative first line offset - preserve it!
    if (m_stFirstLineOffset.m_dValue < 0.0)
    {
        m_stLeftMargin
            = SvxIndentValue::twips(m_stLeftMargin.Resolve({}) - ResolveTextFirstLineOffset({}));
    }

    m_stFirstLineOffset = stValue;
    nPropFirstLineOffset = nProp;

    if (nProp != 100)
    {
        m_stFirstLineOffset.m_dValue = (stValue.m_dValue * static_cast<double>(nProp)) / 100.0;
    }

    if (m_stFirstLineOffset.m_dValue < 0.0)
    {
        m_stLeftMargin
            = SvxIndentValue::twips(m_stLeftMargin.Resolve({}) + ResolveTextFirstLineOffset({}));
    }
}

void SvxTextLeftMarginItem::SetTextLeft(SvxIndentValue stL, const sal_uInt16 nProp)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;

    m_stTextLeftMargin = stL;
    m_nPropLeftMargin = nProp;

    if (nProp != 100)
    {
        m_stTextLeftMargin.m_dValue = (stL.m_dValue * static_cast<double>(nProp)) / 100.0;
    }
}

void SvxLRSpaceItem::SetTextLeft(SvxIndentValue stL, const sal_uInt16 nProp)
{
    if (0.0 == stL.m_dValue)
    {
        SetExplicitZeroMarginValLeft(true);
    }

    m_stLeftMargin = stL;
    nPropLeftMargin = nProp;

    if (nProp != 100)
    {
        m_stLeftMargin.m_dValue = (stL.m_dValue * static_cast<double>(nProp)) / 100.0;
    }

    // note: left margin contains any negative first line offset
    if (0.0 > m_stFirstLineOffset.m_dValue)
    {
        m_stLeftMargin
            = SvxIndentValue::twips(m_stLeftMargin.Resolve({}) + ResolveTextFirstLineOffset({}));
    }
}

SvxIndentValue SvxLRSpaceItem::GetTextFirstLineOffset() const
{
    return m_stFirstLineOffset;
}

sal_Int32 SvxLRSpaceItem::ResolveTextFirstLineOffset(const SvxFontUnitMetrics& rMetrics) const
{
    return m_stFirstLineOffset.Resolve(rMetrics);
}

SvxIndentValue SvxTextLeftMarginItem::GetTextLeft() const { return m_stTextLeftMargin; }

sal_Int32 SvxTextLeftMarginItem::ResolveTextLeft(const SvxFontUnitMetrics& rMetrics) const
{
    return m_stTextLeftMargin.Resolve(rMetrics);
}

sal_Int32 SvxTextLeftMarginItem::ResolveLeft(const SvxFirstLineIndentItem& rFirstLine,
                                             const SvxFontUnitMetrics& rMetrics) const
{
    auto nLeft = m_stTextLeftMargin.Resolve(rMetrics);

    // add any negative first line offset to text left margin to get left
    auto nFirstLine = rFirstLine.GetTextFirstLineOffset().Resolve(rMetrics);
    if (nFirstLine < 0)
    {
        nLeft += nFirstLine;
    }

    return nLeft;
}

sal_Int32
SvxTextLeftMarginItem::ResolveLeftFixedPart(const SvxFirstLineIndentItem& rFirstLine) const
{
    auto nLeft = m_stTextLeftMargin.ResolveFixedPart();

    // add any negative first line offset to text left margin to get left
    auto nFirstLine = rFirstLine.GetTextFirstLineOffset().ResolveFixedPart();
    if (nFirstLine < 0)
    {
        nLeft += nFirstLine;
    }

    return nLeft;
}

sal_Int32 SvxTextLeftMarginItem::ResolveLeftVariablePart(const SvxFirstLineIndentItem& rFirstLine,
                                                         const SvxFontUnitMetrics& rMetrics) const
{
    auto nLeft = m_stTextLeftMargin.ResolveVariablePart(rMetrics);

    // add any negative first line offset to text left margin to get left
    auto nFirstLine = rFirstLine.GetTextFirstLineOffset().ResolveVariablePart(rMetrics);
    if (nFirstLine < 0)
    {
        nLeft += nFirstLine;
    }

    return nLeft;
}

sal_uInt16 SvxTextLeftMarginItem::GetPropLeft() const { return m_nPropLeftMargin; }

SvxIndentValue SvxLRSpaceItem::GetTextLeft() const
{
    // remove any negative first line offset from left margin to get text-left
    if (m_stFirstLineOffset.m_dValue < 0.0)
    {
        return SvxIndentValue::twips(m_stLeftMargin.Resolve({}) - ResolveTextFirstLineOffset({}));
    }

    return m_stLeftMargin;
}

sal_Int32 SvxLRSpaceItem::ResolveTextLeft(const SvxFontUnitMetrics& rMetrics) const
{
    // remove any negative first line offset from left margin to get text-left
    if (m_stFirstLineOffset.m_dValue < 0.0)
    {
        return m_stLeftMargin.Resolve(rMetrics) - ResolveTextFirstLineOffset(rMetrics);
    }

    return m_stLeftMargin.Resolve(rMetrics);
}

SvxLeftMarginItem::SvxLeftMarginItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
}

SvxLeftMarginItem::SvxLeftMarginItem(const tools::Long nLeft, const sal_uInt16 nId)
    : SfxPoolItem(nId)
    , m_nLeftMargin(nLeft)
{
}

bool SvxLeftMarginItem::QueryValue(uno::Any& rVal, sal_uInt8 nMemberId) const
{
    bool bRet = true;
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    switch (nMemberId)
    {
        case MID_L_MARGIN:
            rVal <<= static_cast<sal_Int32>(bConvert ? convertTwipToMm100(m_nLeftMargin) : m_nLeftMargin);
            break;
        case MID_L_REL_MARGIN:
            rVal <<= static_cast<sal_Int16>(m_nPropLeftMargin);
        break;
        default:
            assert(false);
            bRet = false;
            // SfxDispatchController_Impl::StateChanged calls this with hardcoded 0 triggering this; there used to be a MID_LR_MARGIN 0 but what type would it have?
            OSL_FAIL("unknown MemberId");
    }
    return bRet;
}

bool SvxLeftMarginItem::PutValue(const uno::Any& rVal, sal_uInt8 nMemberId)
{
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    switch (nMemberId)
    {
        case MID_L_MARGIN:
        {
            sal_Int32 nVal = 0;
            if (!(rVal >>= nVal))
            {
                return false;
            }
            SetLeft(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal);
            break;
        }
        case MID_L_REL_MARGIN:
        {
            sal_Int32 nRel = 0;
            if ((rVal >>= nRel) && nRel >= 0 && nRel < SAL_MAX_UINT16)
            {
                m_nPropLeftMargin = static_cast<sal_uInt16>(nRel);
            }
            else
            {
                return false;
            }
        }
        break;
        default:
            assert(false);
            OSL_FAIL("unknown MemberId");
            return false;
    }
    return true;
}

bool SvxLeftMarginItem::operator==(const SfxPoolItem& rAttr) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxLeftMarginItem& rOther = static_cast<const SvxLeftMarginItem&>(rAttr);

    return (m_nLeftMargin == rOther.GetLeft()
        && m_nPropLeftMargin == rOther.GetPropLeft());
}

SvxLeftMarginItem* SvxLeftMarginItem::Clone(SfxItemPool *) const
{
    return new SvxLeftMarginItem(*this);
}

bool SvxLeftMarginItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    switch (ePres)
    {
        case SfxItemPresentation::Nameless:
        {
            if (100 != m_nPropLeftMargin)
            {
                rText = unicode::formatPercent(m_nPropLeftMargin,
                    Application::GetSettings().GetUILanguageTag());
            }
            else
            {
                rText = GetMetricText(m_nLeftMargin,
                                      eCoreUnit, ePresUnit, &rIntl);
            }
            return true;
        }
        case SfxItemPresentation::Complete:
        {
            rText = EditResId(RID_SVXITEMS_LRSPACE_LEFT);
            if (100 != m_nPropLeftMargin)
            {
                rText += unicode::formatPercent(m_nPropLeftMargin,
                    Application::GetSettings().GetUILanguageTag());
            }
            else
            {
                rText += GetMetricText(m_nLeftMargin, eCoreUnit, ePresUnit, &rIntl)
                    + " " + EditResId(GetMetricId(ePresUnit));
            }
            return true;
        }
        default: ; // prevent warning
    }
    return false;
}

void SvxLeftMarginItem::ScaleMetrics(tools::Long const nMult, tools::Long const nDiv)
{
    m_nLeftMargin = BigInt::Scale(m_nLeftMargin, nMult, nDiv);
}

bool SvxLeftMarginItem::HasMetrics() const
{
    return true;
}

void SvxLeftMarginItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxLeftMarginItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nLeftMargin"), BAD_CAST(OString::number(m_nLeftMargin).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nPropLeftMargin"), BAD_CAST(OString::number(m_nPropLeftMargin).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

boost::property_tree::ptree SvxLeftMarginItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree = SfxPoolItem::dumpAsJSON();

    boost::property_tree::ptree aState;

    MapUnit eTargetUnit = MapUnit::MapInch;

    OUString sLeft = GetMetricText(GetLeft(),
                        MapUnit::MapTwip, eTargetUnit, nullptr);

    aState.put("left", sLeft);
    aState.put("unit", "inch");

    aTree.push_back(std::make_pair("state", aState));

    return aTree;
}

SvxTextLeftMarginItem::SvxTextLeftMarginItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
}

SvxTextLeftMarginItem::SvxTextLeftMarginItem(SvxIndentValue stLeft, const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
    SetTextLeft(stLeft);
}

bool SvxTextLeftMarginItem::QueryValue(uno::Any& rVal, sal_uInt8 nMemberId) const
{
    bool bRet = false;
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    switch (nMemberId)
    {
        // tdf#154282 - return both values for the hardcoded 0 in SfxDispatchController_Impl::StateChanged
        case 0:
        {
            css::frame::status::LeftRightMarginScale aLRSpace;

            auto nLeftTwips = m_stTextLeftMargin.Resolve({});
            aLRSpace.TextLeft
                = static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nLeftTwips) : nLeftTwips);
            aLRSpace.ScaleLeft = static_cast<sal_Int16>(m_nPropLeftMargin);
            rVal <<= aLRSpace;
            bRet = true;
            break;
        }
        case MID_TXT_LMARGIN :
            bRet = lcl_FillAbsoluteMeasureAny(m_stTextLeftMargin, rVal, bConvert);
            break;
        case MID_L_REL_MARGIN:
            rVal <<= static_cast<sal_Int16>(m_nPropLeftMargin);
            bRet = true;
            break;
        case MID_L_UNIT_MARGIN:
            bRet = lcl_FillRelativeMeasureAny(m_stTextLeftMargin, rVal);
            break;
        default:
            assert(false);
            bRet = false;
            // SfxDispatchController_Impl::StateChanged calls this with hardcoded 0 triggering this; there used to be a MID_LR_MARGIN 0 but what type would it have?
            OSL_FAIL("unknown MemberId");
    }
    return bRet;
}

bool SvxTextLeftMarginItem::PutValue(const uno::Any& rVal, sal_uInt8 nMemberId)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    switch (nMemberId)
    {
        case MID_TXT_LMARGIN:
        {
            sal_Int32 nVal = 0;
            if (!(rVal >>= nVal))
            {
                return false;
            }
            SetTextLeft(
                SvxIndentValue::twips(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal));
            break;
        }
        case MID_L_REL_MARGIN:
        {
            sal_Int32 nRel = 0;
            if ((rVal >>= nRel) && nRel >= 0 && nRel < SAL_MAX_UINT16)
            {
                m_nPropLeftMargin = static_cast<sal_uInt16>(nRel);
            }
            else
            {
                return false;
            }
            break;
        }
        case MID_L_UNIT_MARGIN:
        {
            css::beans::Pair<double, sal_Int16> stVal;
            if (!(rVal >>= stVal))
            {
                return false;
            }

            SetTextLeft(SvxIndentValue{ stVal.First, stVal.Second });
            break;
        }
        default:
            assert(false);
            OSL_FAIL("unknown MemberId");
            return false;
    }
    return true;
}

bool SvxTextLeftMarginItem::operator==(const SfxPoolItem& rAttr) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxTextLeftMarginItem& rOther = static_cast<const SvxTextLeftMarginItem&>(rAttr);

    return std::tie(m_stTextLeftMargin, m_nPropLeftMargin)
           == std::tie(rOther.m_stTextLeftMargin, rOther.m_nPropLeftMargin);
}

size_t SvxTextLeftMarginItem::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, m_stTextLeftMargin.hashCode());
    o3tl::hash_combine(seed, m_nPropLeftMargin);
    return seed;
}


SvxTextLeftMarginItem* SvxTextLeftMarginItem::Clone(SfxItemPool *) const
{
    return new SvxTextLeftMarginItem(*this);
}

bool SvxTextLeftMarginItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    switch (ePres)
    {
        case SfxItemPresentation::Nameless:
        {
            if (100 != m_nPropLeftMargin)
            {
                rText = unicode::formatPercent(m_nPropLeftMargin,
                    Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stTextLeftMargin.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stTextLeftMargin.m_dValue,
                                                   m_stTextLeftMargin.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
            {
                rText = GetMetricText(m_stTextLeftMargin.m_dValue, eCoreUnit, ePresUnit, &rIntl);
            }
            return true;
        }
        case SfxItemPresentation::Complete:
        {
            rText = EditResId(RID_SVXITEMS_LRSPACE_LEFT);
            if (100 != m_nPropLeftMargin)
            {
                rText += unicode::formatPercent(m_nPropLeftMargin,
                    Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stTextLeftMargin.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stTextLeftMargin.m_dValue,
                                                   m_stTextLeftMargin.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
            {
                rText += GetMetricText(m_stTextLeftMargin.m_dValue, eCoreUnit, ePresUnit, &rIntl)
                    + " " + EditResId(GetMetricId(ePresUnit));
            }
            return true;
        }
        default: ; // prevent warning
    }
    return false;
}

void SvxTextLeftMarginItem::ScaleMetrics(tools::Long const nMult, tools::Long const nDiv)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    m_stTextLeftMargin.ScaleMetrics(nMult, nDiv);
}

bool SvxTextLeftMarginItem::HasMetrics() const
{
    return true;
}

void SvxTextLeftMarginItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxTextLeftMarginItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(
        pWriter, BAD_CAST("m_dTextLeftMargin"),
        BAD_CAST(OString::number(m_stTextLeftMargin.m_dValue).getStr()));
    (void)xmlTextWriterWriteAttribute(
        pWriter, BAD_CAST("m_nUnit"),
        BAD_CAST(OString::number(m_stTextLeftMargin.m_nUnit).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nPropLeftMargin"), BAD_CAST(OString::number(m_nPropLeftMargin).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

boost::property_tree::ptree SvxTextLeftMarginItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree = SfxPoolItem::dumpAsJSON();

    auto aState = lcl_IndentValueToJson("left", m_stTextLeftMargin);
    aTree.push_back(std::make_pair("state", aState));

    return aTree;
}

SvxFirstLineIndentItem::SvxFirstLineIndentItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
}

SvxFirstLineIndentItem::SvxFirstLineIndentItem(SvxIndentValue stValue, const sal_uInt16 nId)
    : SvxFirstLineIndentItem(nId)
{
    SetTextFirstLineOffset(stValue);
}

bool SvxFirstLineIndentItem::IsAutoFirst() const { return m_bAutoFirst; }

void SvxFirstLineIndentItem::SetAutoFirst(bool bNew)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    m_bAutoFirst = bNew;
}

void SvxFirstLineIndentItem::SetPropTextFirstLineOffset(sal_uInt16 nProp)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    m_nPropFirstLineOffset = nProp;
}

sal_uInt16 SvxFirstLineIndentItem::GetPropTextFirstLineOffset() const
{
    return m_nPropFirstLineOffset;
}

void SvxFirstLineIndentItem::SetTextFirstLineOffset(SvxIndentValue stValue, sal_uInt16 nProp)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    m_stFirstLineOffset = stValue;
    m_nPropFirstLineOffset = nProp;

    if (nProp != 100)
    {
        m_stFirstLineOffset.m_dValue = (stValue.m_dValue * static_cast<double>(nProp)) / 100.0;
    }
}

SvxIndentValue SvxFirstLineIndentItem::GetTextFirstLineOffset() const
{
    return m_stFirstLineOffset;
}

sal_Int32
SvxFirstLineIndentItem::ResolveTextFirstLineOffset(const SvxFontUnitMetrics& rMetrics) const
{
    return m_stFirstLineOffset.Resolve(rMetrics);
}

bool SvxFirstLineIndentItem::QueryValue(uno::Any& rVal, sal_uInt8 nMemberId) const
{
    bool bRet = false;
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    switch (nMemberId)
    {
        case MID_FIRST_LINE_INDENT:
            bRet = lcl_FillAbsoluteMeasureAny(m_stFirstLineOffset, rVal, bConvert);
            break;

        case MID_FIRST_LINE_REL_INDENT:
            rVal <<= static_cast<sal_Int16>(m_nPropFirstLineOffset);
            bRet = true;
            break;

        case MID_FIRST_LINE_UNIT_INDENT:
            bRet = lcl_FillRelativeMeasureAny(m_stFirstLineOffset, rVal);
            break;

        case MID_FIRST_AUTO:
            rVal <<= IsAutoFirst();
            bRet = true;
            break;

        default:
            assert(false);
            bRet = false;
            // SfxDispatchController_Impl::StateChanged calls this with hardcoded 0 triggering this; there used to be a MID_LR_MARGIN 0 but what type would it have?
            OSL_FAIL("unknown MemberId");
    }
    return bRet;
}

bool SvxFirstLineIndentItem::PutValue(const uno::Any& rVal, sal_uInt8 nMemberId)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    switch (nMemberId)
    {
        case MID_FIRST_LINE_INDENT:
        {
            sal_Int32 nVal = 0;
            if (!(rVal >>= nVal))
            {
                return false;
            }

            m_stFirstLineOffset
                = SvxIndentValue::twips(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal);
            m_nPropFirstLineOffset = 100;
            break;
        }
        case MID_FIRST_LINE_REL_INDENT:
        {
            sal_Int32 nRel = 0;
            if ((rVal >>= nRel) && nRel >= 0 && nRel < SAL_MAX_UINT16)
            {
                SetPropTextFirstLineOffset(nRel);
            }
            else
            {
                return false;
            }
            break;
        }
        case MID_FIRST_LINE_UNIT_INDENT:
        {
            css::beans::Pair<double, sal_Int16> stVal;
            if (!(rVal >>= stVal))
            {
                return false;
            }

            SetTextFirstLineOffset(SvxIndentValue{ stVal.First, stVal.Second });
            break;
        }
        case MID_FIRST_AUTO:
            SetAutoFirst(Any2Bool(rVal));
            break;
        default:
            assert(false);
            OSL_FAIL("unknown MemberId");
            return false;
    }
    return true;
}

bool SvxFirstLineIndentItem::operator==(const SfxPoolItem& rAttr) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxFirstLineIndentItem& rOther = static_cast<const SvxFirstLineIndentItem&>(rAttr);

    return std::tie(m_stFirstLineOffset, m_nPropFirstLineOffset, m_bAutoFirst)
           == std::tie(rOther.m_stFirstLineOffset, rOther.m_nPropFirstLineOffset,
                       rOther.m_bAutoFirst);
}

size_t SvxFirstLineIndentItem::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, m_stFirstLineOffset.hashCode());
    o3tl::hash_combine(seed, m_nPropFirstLineOffset);
    o3tl::hash_combine(seed, m_bAutoFirst);
    return seed;
}

SvxFirstLineIndentItem* SvxFirstLineIndentItem::Clone(SfxItemPool *) const
{
    return new SvxFirstLineIndentItem(*this);
}

bool SvxFirstLineIndentItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    switch (ePres)
    {
        case SfxItemPresentation::Nameless:
        {
            if (100 != m_nPropFirstLineOffset)
            {
                rText += unicode::formatPercent(m_nPropFirstLineOffset,
                    Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stFirstLineOffset.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stFirstLineOffset.m_dValue,
                                                   m_stFirstLineOffset.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
            {
                rText += GetMetricText(m_stFirstLineOffset.m_dValue, eCoreUnit, ePresUnit, &rIntl);
            }
            return true;
        }
        case SfxItemPresentation::Complete:
        {
            rText += EditResId(RID_SVXITEMS_LRSPACE_FLINE);
            if (100 != m_nPropFirstLineOffset)
            {
                rText += unicode::formatPercent(m_nPropFirstLineOffset,
                            Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stFirstLineOffset.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stFirstLineOffset.m_dValue,
                                                   m_stFirstLineOffset.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
            {
                rText += GetMetricText(m_stFirstLineOffset.m_dValue, eCoreUnit, ePresUnit, &rIntl)
                         + " " + EditResId(GetMetricId(ePresUnit));
            }
            return true;
        }
        default: ; // prevent warning
    }
    return false;
}

void SvxFirstLineIndentItem::ScaleMetrics(tools::Long const nMult, tools::Long const nDiv)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    m_stFirstLineOffset.ScaleMetrics(nMult, nDiv);
}

bool SvxFirstLineIndentItem::HasMetrics() const
{
    return true;
}

void SvxFirstLineIndentItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxFirstLineIndentItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(
        pWriter, BAD_CAST("m_dFirstLineOffset"),
        BAD_CAST(OString::number(m_stFirstLineOffset.m_dValue).getStr()));
    (void)xmlTextWriterWriteAttribute(
        pWriter, BAD_CAST("m_nUnit"),
        BAD_CAST(OString::number(m_stFirstLineOffset.m_nUnit).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nPropFirstLineOffset"), BAD_CAST(OString::number(m_nPropFirstLineOffset).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_bAutoFirst"), BAD_CAST(OString::number(int(m_bAutoFirst)).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

boost::property_tree::ptree SvxFirstLineIndentItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree = SfxPoolItem::dumpAsJSON();

    auto aState = lcl_IndentValueToJson("firstline", m_stFirstLineOffset);

    aTree.push_back(std::make_pair("state", aState));

    return aTree;
}

SvxRightMarginItem::SvxRightMarginItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
}

SvxRightMarginItem::SvxRightMarginItem(SvxIndentValue stRight, const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
    SetRight(stRight);
}

bool SvxRightMarginItem::QueryValue(uno::Any& rVal, sal_uInt8 nMemberId) const
{
    bool bRet = false;
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    switch (nMemberId)
    {
        // tdf#154282 - return both values for the hardcoded 0 in SfxDispatchController_Impl::StateChanged
        case 0:
        {
            css::frame::status::LeftRightMarginScale aLRSpace;

            auto nRightTwips = ResolveRight({});
            aLRSpace.Right
                = static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nRightTwips) : nRightTwips);
            aLRSpace.ScaleRight = static_cast<sal_Int16>(m_nPropRightMargin);
            rVal <<= aLRSpace;
            bRet = true;
            break;
        }
        case MID_R_MARGIN:
            bRet = lcl_FillAbsoluteMeasureAny(m_stRightMargin, rVal, bConvert);
            break;
        case MID_R_REL_MARGIN:
            rVal <<= static_cast<sal_Int16>(m_nPropRightMargin);
            bRet = true;
            break;
        case MID_R_UNIT_MARGIN:
            bRet = lcl_FillRelativeMeasureAny(m_stRightMargin, rVal);
            break;
        default:
            assert(false);
            bRet = false;
            // SfxDispatchController_Impl::StateChanged calls this with hardcoded 0 triggering this; there used to be a MID_LR_MARGIN 0 but what type would it have?
            OSL_FAIL("unknown MemberId");
    }
    return bRet;
}

bool SvxRightMarginItem::PutValue(const uno::Any& rVal, sal_uInt8 nMemberId)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    switch (nMemberId)
    {
        case MID_R_MARGIN:
        {
            sal_Int32 nVal = 0;
            if (!(rVal >>= nVal))
            {
                return false;
            }
            SetRight(
                SvxIndentValue::twips(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal));
            break;
        }
        case MID_R_REL_MARGIN:
        {
            sal_Int32 nRel = 0;
            if ((rVal >>= nRel) && nRel >= 0 && nRel < SAL_MAX_UINT16)
            {
                m_nPropRightMargin = static_cast<sal_uInt16>(nRel);
            }
            else
            {
                return false;
            }
            break;
        }
        case MID_R_UNIT_MARGIN:
        {
            css::beans::Pair<double, sal_Int16> stVal;
            if (!(rVal >>= stVal))
            {
                return false;
            }

            SetRight(SvxIndentValue{ stVal.First, stVal.Second });
            break;
        }
        default:
            assert(false);
            OSL_FAIL("unknown MemberId");
            return false;
    }
    return true;
}

bool SvxRightMarginItem::operator==(const SfxPoolItem& rAttr) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxRightMarginItem& rOther = static_cast<const SvxRightMarginItem&>(rAttr);

    return std::tie(m_stRightMargin, m_nPropRightMargin)
           == std::tie(rOther.m_stRightMargin, rOther.m_nPropRightMargin);
}

size_t SvxRightMarginItem::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, m_stRightMargin.hashCode());
    o3tl::hash_combine(seed, m_nPropRightMargin);
    return seed;
}

SvxRightMarginItem* SvxRightMarginItem::Clone(SfxItemPool *) const
{
    return new SvxRightMarginItem(*this);
}

bool SvxRightMarginItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    switch (ePres)
    {
        case SfxItemPresentation::Nameless:
        {
            if (100 != m_nPropRightMargin)
            {
                rText += unicode::formatPercent(m_nPropRightMargin,
                                                Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stRightMargin.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stRightMargin.m_dValue,
                                                   m_stRightMargin.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
            {
                rText += GetMetricText(m_stRightMargin.m_dValue, eCoreUnit, ePresUnit, &rIntl);
            }
            return true;
        }
        case SfxItemPresentation::Complete:
        {
            rText += EditResId(RID_SVXITEMS_LRSPACE_RIGHT);
            if (100 != m_nPropRightMargin)
            {
                rText += unicode::formatPercent(m_nPropRightMargin,
                    Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stRightMargin.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stRightMargin.m_dValue,
                                                   m_stRightMargin.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
            {
                rText += GetMetricText(m_stRightMargin.m_dValue, eCoreUnit, ePresUnit, &rIntl) + " "
                         + EditResId(GetMetricId(ePresUnit));
            }
            return true;
        }
        default: ; // prevent warning
    }
    return false;
}

void SvxRightMarginItem::ScaleMetrics(tools::Long const nMult, tools::Long const nDiv)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    m_stRightMargin.ScaleMetrics(nMult, nDiv);
}

bool SvxRightMarginItem::HasMetrics() const
{
    return true;
}

void SvxRightMarginItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxRightMarginItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_dRightMargin"),
                                      BAD_CAST(OString::number(m_stRightMargin.m_dValue).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nUnit"),
                                      BAD_CAST(OString::number(m_stRightMargin.m_nUnit).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nPropRightMargin"), BAD_CAST(OString::number(m_nPropRightMargin).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

boost::property_tree::ptree SvxRightMarginItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree = SfxPoolItem::dumpAsJSON();

    auto aState = lcl_IndentValueToJson("right", m_stRightMargin);
    aTree.push_back(std::make_pair("state", aState));

    return aTree;
}

SvxGutterLeftMarginItem::SvxGutterLeftMarginItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
}

bool SvxGutterLeftMarginItem::QueryValue(uno::Any& rVal, sal_uInt8 nMemberId) const
{
    bool bRet = true;
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    switch (nMemberId)
    {
        case MID_GUTTER_MARGIN:
            rVal <<= static_cast<sal_Int32>(bConvert ? convertTwipToMm100(m_nGutterMargin)
                                                     : m_nGutterMargin);
            break;
        default:
            assert(false);
            bRet = false;
            // SfxDispatchController_Impl::StateChanged calls this with hardcoded 0 triggering this; there used to be a MID_LR_MARGIN 0 but what type would it have?
            OSL_FAIL("unknown MemberId");
    }
    return bRet;
}

bool SvxGutterLeftMarginItem::PutValue(const uno::Any& rVal, sal_uInt8 nMemberId)
{
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    switch (nMemberId)
    {
        case MID_GUTTER_MARGIN:
        {
            sal_Int32 nVal = 0;
            if (!(rVal >>= nVal))
            {
                return false;
            }
            SetGutterMargin(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal);
            break;
        }
        default:
            assert(false);
            OSL_FAIL("unknown MemberId");
            return false;
    }
    return true;
}

bool SvxGutterLeftMarginItem::operator==(const SfxPoolItem& rAttr) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxGutterLeftMarginItem& rOther = static_cast<const SvxGutterLeftMarginItem&>(rAttr);

    return (m_nGutterMargin == rOther.GetGutterMargin());
}

SvxGutterLeftMarginItem* SvxGutterLeftMarginItem::Clone(SfxItemPool * ) const
{
    return new SvxGutterLeftMarginItem(*this);
}

bool SvxGutterLeftMarginItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           /*rText*/, const IntlWrapper& /*rIntl*/
)   const
{
    // TODO?
    return false;
}

void SvxGutterLeftMarginItem::ScaleMetrics(tools::Long const /*nMult*/, tools::Long const /*nDiv*/)
{
    // TODO?
}

bool SvxGutterLeftMarginItem::HasMetrics() const
{
    return true;
}

void SvxGutterLeftMarginItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxGutterLeftMarginItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nGutterMargin"),
            BAD_CAST(OString::number(m_nGutterMargin).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

boost::property_tree::ptree SvxGutterLeftMarginItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree = SfxPoolItem::dumpAsJSON();

    boost::property_tree::ptree aState;

    // TODO?
    aState.put("unit", "inch");

    aTree.push_back(std::make_pair("state", aState));

    return aTree;
}

SvxGutterRightMarginItem::SvxGutterRightMarginItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
}

bool SvxGutterRightMarginItem::QueryValue(uno::Any& /*rVal*/, sal_uInt8 nMemberId) const
{
    bool bRet = true;
    //bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
#ifndef _MSC_VER
    switch (nMemberId)
    {
        // TODO?
        default:
            assert(false);
            bRet = false;
            // SfxDispatchController_Impl::StateChanged calls this with hardcoded 0 triggering this; there used to be a MID_LR_MARGIN 0 but what type would it have?
            OSL_FAIL("unknown MemberId");
    }
#else
    (void) nMemberId;
#endif
    return bRet;
}

bool SvxGutterRightMarginItem::PutValue(const uno::Any& /*rVal*/, sal_uInt8 nMemberId)
{
    //bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

#ifndef _MSC_VER
    switch (nMemberId)
    {
        // TODO?
        default:
            assert(false);
            OSL_FAIL("unknown MemberId");
            return false;
    }
#else
    (void) nMemberId;
#endif
    return true;
}


bool SvxGutterRightMarginItem::operator==(const SfxPoolItem& rAttr) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxGutterRightMarginItem& rOther = static_cast<const SvxGutterRightMarginItem&>(rAttr);

    return (m_nRightGutterMargin == rOther.GetRightGutterMargin());
}

SvxGutterRightMarginItem* SvxGutterRightMarginItem::Clone(SfxItemPool *) const
{
    return new SvxGutterRightMarginItem(*this);
}

bool SvxGutterRightMarginItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           /*rText*/, const IntlWrapper& /*rIntl*/
)   const
{
    // TODO?
    return false;
}

void SvxGutterRightMarginItem::ScaleMetrics(tools::Long const /*nMult*/, tools::Long const /*nDiv*/)
{
    // TODO?
}

bool SvxGutterRightMarginItem::HasMetrics() const
{
    return true;
}

void SvxGutterRightMarginItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxGutterRightMarginItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nRightGutterMargin"),
            BAD_CAST(OString::number(m_nRightGutterMargin).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

boost::property_tree::ptree SvxGutterRightMarginItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree = SfxPoolItem::dumpAsJSON();

    boost::property_tree::ptree aState;

    // TODO?
    aState.put("unit", "inch");

    aTree.push_back(std::make_pair("state", aState));

    return aTree;
}


bool SvxLRSpaceItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxLRSpaceItem& rOther = static_cast<const SvxLRSpaceItem&>(rAttr);

    return std::tie(m_stFirstLineOffset, m_nGutterMargin, m_nRightGutterMargin, m_stLeftMargin,
                    m_stRightMargin, nPropFirstLineOffset, nPropLeftMargin, nPropRightMargin,
                    bAutoFirst, bExplicitZeroMarginValRight, bExplicitZeroMarginValLeft)
           == std::tie(rOther.m_stFirstLineOffset, rOther.m_nGutterMargin,
                       rOther.m_nRightGutterMargin, rOther.m_stLeftMargin, rOther.m_stRightMargin,
                       rOther.nPropFirstLineOffset, rOther.nPropLeftMargin, rOther.nPropRightMargin,
                       rOther.bAutoFirst, rOther.bExplicitZeroMarginValRight,
                       rOther.bExplicitZeroMarginValLeft);
}

SvxLRSpaceItem* SvxLRSpaceItem::Clone( SfxItemPool* ) const
{
    return new SvxLRSpaceItem( *this );
}

bool SvxLRSpaceItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    switch ( ePres )
    {
        case SfxItemPresentation::Nameless:
        {
            if ( 100 != nPropLeftMargin )
            {
                rText = unicode::formatPercent(nPropLeftMargin,
                    Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stLeftMargin.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stLeftMargin.m_dValue,
                                                   m_stLeftMargin.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
                rText = GetMetricText(static_cast<tools::Long>(m_stLeftMargin.m_dValue), eCoreUnit,
                                      ePresUnit, &rIntl);
            rText += cpDelim;
            if ( 100 != nPropFirstLineOffset )
            {
                rText += unicode::formatPercent(nPropFirstLineOffset,
                    Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stFirstLineOffset.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stFirstLineOffset.m_dValue,
                                                   m_stFirstLineOffset.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
                rText += GetMetricText(static_cast<tools::Long>(m_stFirstLineOffset.m_dValue),
                                       eCoreUnit, ePresUnit, &rIntl);
            rText += cpDelim;
            if (100 != nPropRightMargin)
            {
                rText += unicode::formatPercent(nPropRightMargin,
                                                Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stRightMargin.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stRightMargin.m_dValue,
                                                   m_stRightMargin.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
                rText += GetMetricText(static_cast<tools::Long>(m_stRightMargin.m_dValue),
                                       eCoreUnit, ePresUnit, &rIntl);
            return true;
        }
        case SfxItemPresentation::Complete:
        {
            rText = EditResId(RID_SVXITEMS_LRSPACE_LEFT);
            if ( 100 != nPropLeftMargin )
                rText += unicode::formatPercent(nPropLeftMargin,
                    Application::GetSettings().GetUILanguageTag());
            else if (m_stLeftMargin.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stLeftMargin.m_dValue,
                                                   m_stLeftMargin.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
            {
                rText += GetMetricText(static_cast<tools::Long>(m_stLeftMargin.m_dValue), eCoreUnit,
                                       ePresUnit, &rIntl)
                         + " " + EditResId(GetMetricId(ePresUnit));
            }
            rText += cpDelim;
            if (100 != nPropFirstLineOffset || m_stFirstLineOffset.m_dValue != 0.0)
            {
                rText += EditResId(RID_SVXITEMS_LRSPACE_FLINE);
                if ( 100 != nPropFirstLineOffset )
                    rText += unicode::formatPercent(nPropFirstLineOffset,
                                Application::GetSettings().GetUILanguageTag());
                else if (m_stFirstLineOffset.m_nUnit != css::util::MeasureUnit::TWIP)
                {
                    OUStringBuffer stBuf;
                    sax::Converter::convertMeasureUnit(stBuf, m_stFirstLineOffset.m_dValue,
                                                       m_stFirstLineOffset.m_nUnit);
                    rText += stBuf.makeStringAndClear();
                }
                else
                {
                    rText += GetMetricText(static_cast<tools::Long>(m_stFirstLineOffset.m_dValue),
                                           eCoreUnit, ePresUnit, &rIntl)
                             + " " + EditResId(GetMetricId(ePresUnit));
                }
                rText += cpDelim;
            }
            rText += EditResId(RID_SVXITEMS_LRSPACE_RIGHT);
            if (100 != nPropRightMargin)
            {
                rText += unicode::formatPercent(nPropRightMargin,
                                                Application::GetSettings().GetUILanguageTag());
            }
            else if (m_stRightMargin.m_nUnit != css::util::MeasureUnit::TWIP)
            {
                OUStringBuffer stBuf;
                sax::Converter::convertMeasureUnit(stBuf, m_stRightMargin.m_dValue,
                                                   m_stRightMargin.m_nUnit);
                rText += stBuf.makeStringAndClear();
            }
            else
            {
                rText += GetMetricText(static_cast<tools::Long>(m_stRightMargin.m_dValue),
                                       eCoreUnit, ePresUnit, &rIntl)
                         + " " + EditResId(GetMetricId(ePresUnit));
            }
            return true;
        }
        default: ; // prevent warning
    }
    return false;
}


void SvxLRSpaceItem::ScaleMetrics( tools::Long nMult, tools::Long nDiv )
{
    m_stFirstLineOffset.ScaleMetrics(nMult, nDiv);
    m_stLeftMargin.ScaleMetrics(nMult, nDiv);
    m_stRightMargin.ScaleMetrics(nMult, nDiv);
}


bool SvxLRSpaceItem::HasMetrics() const
{
    return true;
}


void SvxLRSpaceItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxLRSpaceItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(
        pWriter, BAD_CAST("m_dFirstLineOffset"),
        BAD_CAST(OString::number(m_stFirstLineOffset.m_dValue).getStr()));
    (void)xmlTextWriterWriteAttribute(
        pWriter, BAD_CAST("m_nFirstLineUnit"),
        BAD_CAST(OString::number(m_stFirstLineOffset.m_nUnit).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_dLeftMargin"),
                                      BAD_CAST(OString::number(m_stLeftMargin.m_dValue).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nLeftMarginUnit"),
                                      BAD_CAST(OString::number(m_stLeftMargin.m_nUnit).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_dRightMargin"),
                                      BAD_CAST(OString::number(m_stRightMargin.m_dValue).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nRightMarginUnit"),
                                      BAD_CAST(OString::number(m_stRightMargin.m_nUnit).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nGutterMargin"),
                                BAD_CAST(OString::number(m_nGutterMargin).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nRightGutterMargin"),
                                BAD_CAST(OString::number(m_nRightGutterMargin).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("nPropFirstLineOffset"), BAD_CAST(OString::number(nPropFirstLineOffset).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("nPropLeftMargin"), BAD_CAST(OString::number(nPropLeftMargin).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("nPropRightMargin"), BAD_CAST(OString::number(nPropRightMargin).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("bAutoFirst"), BAD_CAST(OString::number(int(bAutoFirst)).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("bExplicitZeroMarginValRight"), BAD_CAST(OString::number(int(bExplicitZeroMarginValRight)).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("bExplicitZeroMarginValLeft"), BAD_CAST(OString::number(int(bExplicitZeroMarginValLeft)).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}


boost::property_tree::ptree SvxLRSpaceItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree = SfxPoolItem::dumpAsJSON();

    boost::property_tree::ptree aState;

    MapUnit eTargetUnit = MapUnit::MapInch;

    OUString sLeft = GetMetricText(ResolveLeft({}), MapUnit::MapTwip, eTargetUnit, nullptr);

    OUString sRight = GetMetricText(ResolveRight({}), MapUnit::MapTwip, eTargetUnit, nullptr);

    OUString sFirstline
        = GetMetricText(ResolveTextFirstLineOffset({}), MapUnit::MapTwip, eTargetUnit, nullptr);

    aState.put("left", sLeft);
    aState.put("right", sRight);
    aState.put("firstline", sFirstline);
    aState.put("unit", "inch");

    aTree.push_back(std::make_pair("state", aState));

    return aTree;
}


SvxULSpaceItem::SvxULSpaceItem( const sal_uInt16 nId )
    : SfxPoolItem(nId)
    , nUpper(0)
    , nLower(0)
    , bContext(false)
    , nPropUpper(100)
    , nPropLower(100)
{
}


SvxULSpaceItem::SvxULSpaceItem( const sal_uInt16 nUp, const sal_uInt16 nLow,
                                const sal_uInt16 nId )
    : SfxPoolItem(nId)
    , nUpper(nUp)
    , nLower(nLow)
    , bContext(false)
    , nPropUpper(100)
    , nPropLower(100)
{
}


bool SvxULSpaceItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    switch( nMemberId )
    {
        // now all signed
        case 0:
        {
            css::frame::status::UpperLowerMarginScale aUpperLowerMarginScale;
            aUpperLowerMarginScale.Upper = static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nUpper) : nUpper);
            aUpperLowerMarginScale.Lower = static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nLower) : nPropUpper);
            aUpperLowerMarginScale.ScaleUpper = static_cast<sal_Int16>(nPropUpper);
            aUpperLowerMarginScale.ScaleLower = static_cast<sal_Int16>(nPropLower);
            rVal <<= aUpperLowerMarginScale;
            break;
        }
        case MID_UP_MARGIN: rVal <<= static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nUpper) : nUpper); break;
        case MID_LO_MARGIN: rVal <<= static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nLower) : nLower); break;
        case MID_CTX_MARGIN: rVal <<= bContext; break;
        case MID_UP_REL_MARGIN: rVal <<= static_cast<sal_Int16>(nPropUpper); break;
        case MID_LO_REL_MARGIN: rVal <<= static_cast<sal_Int16>(nPropLower); break;
    }
    return true;
}


bool SvxULSpaceItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    sal_Int32 nVal = 0;
    bool bVal = false;
    switch( nMemberId )
    {
        case 0:
        {
            css::frame::status::UpperLowerMarginScale aUpperLowerMarginScale;
            if ( !(rVal >>= aUpperLowerMarginScale ))
                return false;
            {
                SetUpper(bConvert ? o3tl::toTwips(aUpperLowerMarginScale.Upper, o3tl::Length::mm100) : aUpperLowerMarginScale.Upper);
                SetLower(bConvert ? o3tl::toTwips(aUpperLowerMarginScale.Lower, o3tl::Length::mm100) : aUpperLowerMarginScale.Lower);
                if( aUpperLowerMarginScale.ScaleUpper > 1 )
                    nPropUpper = aUpperLowerMarginScale.ScaleUpper;
                if( aUpperLowerMarginScale.ScaleLower > 1 )
                    nPropUpper = aUpperLowerMarginScale.ScaleLower;
            }
        }
        break;
        case MID_UP_MARGIN :
            if(!(rVal >>= nVal))
                return false;
            SetUpper(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal);
            break;
        case MID_LO_MARGIN :
            if(!(rVal >>= nVal) || nVal < 0)
                return false;
            SetLower(bConvert ? o3tl::toTwips(nVal, o3tl::Length::mm100) : nVal);
            break;
        case MID_CTX_MARGIN :
            if (!(rVal >>= bVal))
                return false;
            SetContextValue(bVal);
            break;
        case MID_UP_REL_MARGIN:
        case MID_LO_REL_MARGIN:
        {
            sal_Int32 nRel = 0;
            if((rVal >>= nRel) && nRel > 1 )
            {
                if(MID_UP_REL_MARGIN == nMemberId)
                    nPropUpper = static_cast<sal_uInt16>(nRel);
                else
                    nPropLower = static_cast<sal_uInt16>(nRel);
            }
            else
                return false;
        }
        break;

        default:
            OSL_FAIL("unknown MemberId");
            return false;
    }
    return true;
}


bool SvxULSpaceItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxULSpaceItem& rSpaceItem = static_cast<const SvxULSpaceItem&>( rAttr );
    return ( nUpper == rSpaceItem.nUpper &&
             nLower == rSpaceItem.nLower &&
             bContext == rSpaceItem.bContext &&
             nPropUpper == rSpaceItem.nPropUpper &&
             nPropLower == rSpaceItem.nPropLower );
}

size_t SvxULSpaceItem::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, nUpper);
    o3tl::hash_combine(seed, nLower);
    o3tl::hash_combine(seed, bContext);
    o3tl::hash_combine(seed, nPropUpper);
    o3tl::hash_combine(seed, nPropLower);
    return seed;
}

SvxULSpaceItem* SvxULSpaceItem::Clone( SfxItemPool* ) const
{
    return new SvxULSpaceItem( *this );
}

bool SvxULSpaceItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText,
    const IntlWrapper&  rIntl
)   const
{
    switch ( ePres )
    {
        case SfxItemPresentation::Nameless:
        {
            if ( 100 != nPropUpper )
            {
                rText = unicode::formatPercent(nPropUpper,
                    Application::GetSettings().GetUILanguageTag());
            }
            else
                rText = GetMetricText( static_cast<tools::Long>(nUpper), eCoreUnit, ePresUnit, &rIntl );
            rText += cpDelim;
            if ( 100 != nPropLower )
            {
                rText += unicode::formatPercent(nPropLower,
                    Application::GetSettings().GetUILanguageTag());
            }
            else
                rText += GetMetricText( static_cast<tools::Long>(nLower), eCoreUnit, ePresUnit, &rIntl );
            return true;
        }
        case SfxItemPresentation::Complete:
        {
            rText = EditResId(RID_SVXITEMS_ULSPACE_UPPER);
            if ( 100 != nPropUpper )
            {
                rText += unicode::formatPercent(nPropUpper,
                    Application::GetSettings().GetUILanguageTag());
            }
            else
            {
                rText += GetMetricText( static_cast<tools::Long>(nUpper), eCoreUnit, ePresUnit, &rIntl ) +
                        " " + EditResId(GetMetricId(ePresUnit));
            }
            rText += cpDelim + EditResId(RID_SVXITEMS_ULSPACE_LOWER);
            if ( 100 != nPropLower )
            {
                rText += unicode::formatPercent(nPropLower,
                    Application::GetSettings().GetUILanguageTag());
            }
            else
            {
                rText += GetMetricText( static_cast<tools::Long>(nLower), eCoreUnit, ePresUnit, &rIntl ) +
                        " " + EditResId(GetMetricId(ePresUnit));
            }
            return true;
        }
        default: ; // prevent warning
    }
    return false;
}


void SvxULSpaceItem::ScaleMetrics( tools::Long nMult, tools::Long nDiv )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    nUpper = static_cast<sal_uInt16>(BigInt::Scale( nUpper, nMult, nDiv ));
    nLower = static_cast<sal_uInt16>(BigInt::Scale( nLower, nMult, nDiv ));
}


bool SvxULSpaceItem::HasMetrics() const
{
    return true;
}


void SvxULSpaceItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxULSpaceItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("nUpper"), BAD_CAST(OString::number(nUpper).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("nLower"), BAD_CAST(OString::number(nLower).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("bContext"), BAD_CAST(OString::boolean(bContext).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("nPropUpper"), BAD_CAST(OString::number(nPropUpper).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("nPropLower"), BAD_CAST(OString::number(nPropLower).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

boost::property_tree::ptree SvxULSpaceItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree = SfxPoolItem::dumpAsJSON();

    boost::property_tree::ptree aState;

    MapUnit eTargetUnit = MapUnit::MapInch;

    OUString sUpper = GetMetricText(GetUpper(),
                        MapUnit::MapTwip, eTargetUnit, nullptr);

    OUString sLower = GetMetricText(GetLower(),
                        MapUnit::MapTwip, eTargetUnit, nullptr);

    aState.put("upper", sUpper);
    aState.put("lower", sLower);
    aState.put("unit", "inch");

    aTree.push_back(std::make_pair("state", aState));

    return aTree;
}

SvxPrintItem* SvxPrintItem::Clone( SfxItemPool* ) const
{
    return new SvxPrintItem( *this );
}

bool SvxPrintItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&
)   const
{
    TranslateId pId = RID_SVXITEMS_PRINT_FALSE;

    if ( GetValue() )
        pId = RID_SVXITEMS_PRINT_TRUE;
    rText = EditResId(pId);
    return true;
}

SvxOpaqueItem* SvxOpaqueItem::Clone( SfxItemPool* ) const
{
    return new SvxOpaqueItem( *this );
}

bool SvxOpaqueItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&
)   const
{
    TranslateId pId = RID_SVXITEMS_OPAQUE_FALSE;

    if ( GetValue() )
        pId = RID_SVXITEMS_OPAQUE_TRUE;
    rText = EditResId(pId);
    return true;
}


bool SvxProtectItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxProtectItem& rItem = static_cast<const SvxProtectItem&>(rAttr);
    return ( bCntnt == rItem.bCntnt &&
             bSize  == rItem.bSize  &&
             bPos   == rItem.bPos );
}


bool SvxProtectItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    nMemberId &= ~CONVERT_TWIPS;
    bool bValue;
    switch(nMemberId)
    {
        case MID_PROTECT_CONTENT :  bValue = bCntnt; break;
        case MID_PROTECT_SIZE    :  bValue = bSize; break;
        case MID_PROTECT_POSITION:  bValue = bPos; break;
        default:
            OSL_FAIL("Wrong MemberId");
            return false;
    }

    rVal <<= bValue;
    return true;
}


bool SvxProtectItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    nMemberId &= ~CONVERT_TWIPS;
    bool bVal( Any2Bool(rVal) );
    switch(nMemberId)
    {
        case MID_PROTECT_CONTENT :  bCntnt = bVal;  break;
        case MID_PROTECT_SIZE    :  bSize  = bVal;  break;
        case MID_PROTECT_POSITION:  bPos   = bVal;  break;
        default:
            OSL_FAIL("Wrong MemberId");
            return false;
    }
    return true;
}

SvxProtectItem* SvxProtectItem::Clone( SfxItemPool* ) const
{
    return new SvxProtectItem( *this );
}

bool SvxProtectItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&
)   const
{
    TranslateId pId = RID_SVXITEMS_PROT_CONTENT_FALSE;

    if ( bCntnt )
        pId = RID_SVXITEMS_PROT_CONTENT_TRUE;
    rText = EditResId(pId) + cpDelim;
    pId = RID_SVXITEMS_PROT_SIZE_FALSE;

    if ( bSize )
        pId = RID_SVXITEMS_PROT_SIZE_TRUE;
    rText += EditResId(pId) + cpDelim;
    pId = RID_SVXITEMS_PROT_POS_FALSE;

    if ( bPos )
        pId = RID_SVXITEMS_PROT_POS_TRUE;
    rText += EditResId(pId);
    return true;
}


void SvxProtectItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxProtectItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("content"), BAD_CAST(OString::boolean(bCntnt).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("size"), BAD_CAST(OString::boolean(bSize).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("position"), BAD_CAST(OString::boolean(bPos).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}


SvxShadowItem::SvxShadowItem( const sal_uInt16 nId,
                 const Color *pColor, const sal_uInt16 nW,
                 const SvxShadowLocation eLoc ) :
    SfxPoolItem( nId ),
    aShadowColor(COL_GRAY),
    nWidth      ( nW ),
    eLocation   ( eLoc )
{
    if ( pColor )
        aShadowColor = *pColor;
}


bool SvxShadowItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    table::ShadowFormat aShadow;
    table::ShadowLocation eSet = table::ShadowLocation_NONE;
    switch( eLocation )
    {
        case SvxShadowLocation::TopLeft    : eSet = table::ShadowLocation_TOP_LEFT    ; break;
        case SvxShadowLocation::TopRight   : eSet = table::ShadowLocation_TOP_RIGHT   ; break;
        case SvxShadowLocation::BottomLeft : eSet = table::ShadowLocation_BOTTOM_LEFT ; break;
        case SvxShadowLocation::BottomRight: eSet = table::ShadowLocation_BOTTOM_RIGHT; break;
        default: ; // prevent warning
    }
    aShadow.Location = eSet;
    aShadow.ShadowWidth =   bConvert ? convertTwipToMm100(nWidth) : nWidth;
    aShadow.IsTransparent = aShadowColor.IsTransparent();
    aShadow.Color = sal_Int32(aShadowColor);

    sal_Int8 nTransparence = rtl::math::round((float(255 - aShadowColor.GetAlpha()) * 100) / 255);

    switch ( nMemberId )
    {
        case MID_LOCATION: rVal <<= aShadow.Location; break;
        case MID_WIDTH: rVal <<= aShadow.ShadowWidth; break;
        case MID_TRANSPARENT: rVal <<= aShadow.IsTransparent; break;
        case MID_BG_COLOR: rVal <<= aShadow.Color; break;
        case 0: rVal <<= aShadow; break;
        case MID_SHADOW_TRANSPARENCE: rVal <<= nTransparence; break;
        default: OSL_FAIL("Wrong MemberId!"); return false;
    }

    return true;
}

bool SvxShadowItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;

    table::ShadowFormat aShadow;
    uno::Any aAny;
    bool bRet = QueryValue( aAny, bConvert ? CONVERT_TWIPS : 0 ) && ( aAny >>= aShadow );
    switch ( nMemberId )
    {
        case MID_LOCATION:
        {
            bRet = (rVal >>= aShadow.Location);
            if ( !bRet )
            {
                sal_Int16 nVal = 0;
                bRet = (rVal >>= nVal);
                aShadow.Location = static_cast<table::ShadowLocation>(nVal);
            }

            break;
        }

        case MID_WIDTH: rVal >>= aShadow.ShadowWidth; break;
        case MID_TRANSPARENT: rVal >>= aShadow.IsTransparent; break;
        case MID_BG_COLOR: rVal >>= aShadow.Color; break;
        case 0: rVal >>= aShadow; break;
        case MID_SHADOW_TRANSPARENCE:
        {
            sal_Int32 nTransparence = 0;
            if ((rVal >>= nTransparence) && !o3tl::checked_multiply<sal_Int32>(nTransparence, 255, nTransparence))
            {
                Color aColor(ColorTransparency, aShadow.Color);
                aColor.SetAlpha(255 - rtl::math::round(float(nTransparence) / 100));
                aShadow.Color = sal_Int32(aColor);
            }
            break;
        }
        default: OSL_FAIL("Wrong MemberId!"); return false;
    }

    if ( bRet )
    {
        switch( aShadow.Location )
        {
            case table::ShadowLocation_NONE        : eLocation = SvxShadowLocation::NONE; break;
            case table::ShadowLocation_TOP_LEFT    : eLocation = SvxShadowLocation::TopLeft; break;
            case table::ShadowLocation_TOP_RIGHT   : eLocation = SvxShadowLocation::TopRight; break;
            case table::ShadowLocation_BOTTOM_LEFT : eLocation = SvxShadowLocation::BottomLeft ; break;
            case table::ShadowLocation_BOTTOM_RIGHT: eLocation = SvxShadowLocation::BottomRight; break;
            default: ; // prevent warning
        }

        nWidth = bConvert ? o3tl::toTwips(aShadow.ShadowWidth, o3tl::Length::mm100) : aShadow.ShadowWidth;
        Color aSet(ColorTransparency, aShadow.Color);
        aShadowColor = aSet;
    }

    return bRet;
}


bool SvxShadowItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxShadowItem& rItem = static_cast<const SvxShadowItem&>(rAttr);
    return ( ( aShadowColor == rItem.aShadowColor ) &&
             ( nWidth    == rItem.GetWidth() ) &&
             ( eLocation == rItem.GetLocation() ) );
}

size_t SvxShadowItem::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, sal_Int32(aShadowColor));
    o3tl::hash_combine(seed, nWidth);
    o3tl::hash_combine(seed, static_cast<int>(eLocation));
    return seed;
}

SvxShadowItem* SvxShadowItem::Clone( SfxItemPool* ) const
{
    return new SvxShadowItem( *this );
}

sal_uInt16 SvxShadowItem::CalcShadowSpace( SvxShadowItemSide nShadow ) const
{
    sal_uInt16 nSpace = 0;

    switch ( nShadow )
    {
        case SvxShadowItemSide::TOP:
            if ( eLocation == SvxShadowLocation::TopLeft ||
                 eLocation == SvxShadowLocation::TopRight  )
                nSpace = nWidth;
            break;

        case SvxShadowItemSide::BOTTOM:
            if ( eLocation == SvxShadowLocation::BottomLeft ||
                 eLocation == SvxShadowLocation::BottomRight  )
                nSpace = nWidth;
            break;

        case SvxShadowItemSide::LEFT:
            if ( eLocation == SvxShadowLocation::TopLeft ||
                 eLocation == SvxShadowLocation::BottomLeft )
                nSpace = nWidth;
            break;

        case SvxShadowItemSide::RIGHT:
            if ( eLocation == SvxShadowLocation::TopRight ||
                 eLocation == SvxShadowLocation::BottomRight )
                nSpace = nWidth;
            break;

        default:
            OSL_FAIL( "wrong shadow" );
    }
    return nSpace;
}

const TranslateId RID_SVXITEMS_SHADOW[] =
{
    RID_SVXITEMS_SHADOW_NONE,
    RID_SVXITEMS_SHADOW_TOPLEFT,
    RID_SVXITEMS_SHADOW_TOPRIGHT,
    RID_SVXITEMS_SHADOW_BOTTOMLEFT,
    RID_SVXITEMS_SHADOW_BOTTOMRIGHT
};

bool SvxShadowItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    switch ( ePres )
    {
        case SfxItemPresentation::Nameless:
        {
            rText = ::GetColorString( aShadowColor ) + cpDelim;
            TranslateId pId = RID_SVXITEMS_TRANSPARENT_FALSE;

            if ( aShadowColor.IsTransparent() )
                pId = RID_SVXITEMS_TRANSPARENT_TRUE;
            rText += EditResId(pId) +
                    cpDelim +
                    GetMetricText( static_cast<tools::Long>(nWidth), eCoreUnit, ePresUnit, &rIntl ) +
                    cpDelim +
                    EditResId(RID_SVXITEMS_SHADOW[static_cast<int>(eLocation)]);
            return true;
        }
        case SfxItemPresentation::Complete:
        {
            rText = EditResId(RID_SVXITEMS_SHADOW_COMPLETE) +
                    ::GetColorString( aShadowColor ) +
                    cpDelim;

            TranslateId pId = RID_SVXITEMS_TRANSPARENT_FALSE;
            if ( aShadowColor.IsTransparent() )
                pId = RID_SVXITEMS_TRANSPARENT_TRUE;
            rText += EditResId(pId) +
                    cpDelim +
                    GetMetricText( static_cast<tools::Long>(nWidth), eCoreUnit, ePresUnit, &rIntl ) +
                    " " + EditResId(GetMetricId(ePresUnit)) +
                    cpDelim +
                    EditResId(RID_SVXITEMS_SHADOW[static_cast<int>(eLocation)]);
            return true;
        }
        default: ; // prevent warning
    }
    return false;
}


void SvxShadowItem::ScaleMetrics( tools::Long nMult, tools::Long nDiv )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    nWidth = static_cast<sal_uInt16>(BigInt::Scale( nWidth, nMult, nDiv ));
}


bool SvxShadowItem::HasMetrics() const
{
    return true;
}


void SvxShadowItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxShadowItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("aShadowColor"), BAD_CAST(aShadowColor.AsRGBHexString().toUtf8().getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("nWidth"), BAD_CAST(OString::number(nWidth).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("eLocation"), BAD_CAST(OString::number(static_cast<int>(eLocation)).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("presentation"), BAD_CAST(EditResId(RID_SVXITEMS_SHADOW[static_cast<int>(eLocation)]).toUtf8().getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

// class SvxBoxItem ------------------------------------------------------

SvxBoxItem::SvxBoxItem(const SvxBoxItem& rCopy)
    : SfxPoolItem (rCopy)
    , mpTopBorderLine(rCopy.mpTopBorderLine ? new SvxBorderLine(*rCopy.mpTopBorderLine) : nullptr)
    , mpBottomBorderLine(rCopy.mpBottomBorderLine ? new SvxBorderLine(*rCopy.mpBottomBorderLine) : nullptr)
    , mpLeftBorderLine(rCopy.mpLeftBorderLine ? new SvxBorderLine(*rCopy.mpLeftBorderLine) : nullptr)
    , mpRightBorderLine(rCopy.mpRightBorderLine ? new SvxBorderLine(*rCopy.mpRightBorderLine) : nullptr)
    , mnTopDistance(rCopy.mnTopDistance)
    , mnBottomDistance(rCopy.mnBottomDistance)
    , mnLeftDistance(rCopy.mnLeftDistance)
    , mnRightDistance(rCopy.mnRightDistance)
    , maTempComplexColors(rCopy.maTempComplexColors)
    , mbRemoveAdjCellBorder(rCopy.mbRemoveAdjCellBorder)
{
}


SvxBoxItem::SvxBoxItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
{
}


SvxBoxItem::~SvxBoxItem()
{
}

void SvxBoxItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxBoxItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("top-dist"),
                                      BAD_CAST(OString::number(mnTopDistance).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("bottom-dist"),
                                      BAD_CAST(OString::number(mnBottomDistance).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("left-dist"),
                                      BAD_CAST(OString::number(mnLeftDistance).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("right-dist"),
                                      BAD_CAST(OString::number(mnRightDistance).getStr()));
    SfxPoolItem::dumpAsXml(pWriter);
    (void)xmlTextWriterEndElement(pWriter);
}

boost::property_tree::ptree SvxBoxItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree;

    boost::property_tree::ptree aState;
    aState.put("top", GetTop() && !GetTop()->isEmpty());
    aState.put("bottom", GetBottom() && !GetBottom()->isEmpty());
    aState.put("left", GetLeft() && !GetLeft()->isEmpty());
    aState.put("right", GetRight() && !GetRight()->isEmpty());

    aTree.push_back(std::make_pair("state", aState));
    aTree.put("commandName", ".uno:BorderOuter");

    return aTree;
}


static bool CompareBorderLine(const std::unique_ptr<SvxBorderLine> & pBrd1, const SvxBorderLine* pBrd2)
{
    if( pBrd1.get() == pBrd2 )
        return true;
    if( pBrd1 == nullptr || pBrd2 == nullptr)
        return false;
    return *pBrd1 == *pBrd2;
}


bool SvxBoxItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxBoxItem& rBoxItem = static_cast<const SvxBoxItem&>(rAttr);
    return (
        (mnTopDistance == rBoxItem.mnTopDistance) &&
        (mnBottomDistance == rBoxItem.mnBottomDistance) &&
        (mnLeftDistance == rBoxItem.mnLeftDistance) &&
        (mnRightDistance == rBoxItem.mnRightDistance) &&
        (mbRemoveAdjCellBorder == rBoxItem.mbRemoveAdjCellBorder ) &&
        (maTempComplexColors == rBoxItem.maTempComplexColors) &&
        CompareBorderLine(mpTopBorderLine, rBoxItem.GetTop()) &&
        CompareBorderLine(mpBottomBorderLine, rBoxItem.GetBottom()) &&
        CompareBorderLine(mpLeftBorderLine, rBoxItem.GetLeft()) &&
        CompareBorderLine(mpRightBorderLine, rBoxItem.GetRight()));
}

size_t SvxBoxItem::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, mnTopDistance);
    o3tl::hash_combine(seed, mnBottomDistance);
    o3tl::hash_combine(seed, mnLeftDistance);
    o3tl::hash_combine(seed, mnRightDistance);
    return seed;
}

table::BorderLine2 SvxBoxItem::SvxLineToLine(const SvxBorderLine* pLine, bool bConvert)
{
    table::BorderLine2 aLine;
    if(pLine)
    {
        aLine.Color          = sal_Int32(pLine->GetColor());
        aLine.InnerLineWidth = sal_uInt16( bConvert ? convertTwipToMm100(pLine->GetInWidth() ): pLine->GetInWidth() );
        aLine.OuterLineWidth = sal_uInt16( bConvert ? convertTwipToMm100(pLine->GetOutWidth()): pLine->GetOutWidth() );
        aLine.LineDistance   = sal_uInt16( bConvert ? convertTwipToMm100(pLine->GetDistance()): pLine->GetDistance() );
        aLine.LineStyle      = sal_Int16(pLine->GetBorderLineStyle());
        aLine.LineWidth      = sal_uInt32( bConvert ? convertTwipToMm100( pLine->GetWidth( ) ) : pLine->GetWidth( ) );
    }
    else
    {
        aLine.Color          = aLine.InnerLineWidth = aLine.OuterLineWidth = aLine.LineDistance  = 0;
        aLine.LineStyle = table::BorderLineStyle::NONE; // 0 is SOLID!
    }
    return aLine;
}

bool SvxBoxItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    table::BorderLine2 aRetLine;
    sal_Int16 nDist = 0;
    bool bDistMember = false;
    nMemberId &= ~CONVERT_TWIPS;
    switch(nMemberId)
    {
        case 0:
        {
            // 4 Borders and 5 distances
            uno::Sequence< uno::Any > aSeq{
                uno::Any(SvxBoxItem::SvxLineToLine(GetLeft(), bConvert)),
                uno::Any(SvxBoxItem::SvxLineToLine(GetRight(), bConvert)),
                uno::Any(SvxBoxItem::SvxLineToLine(GetBottom(), bConvert)),
                uno::Any(SvxBoxItem::SvxLineToLine(GetTop(), bConvert)),
                uno::Any(static_cast<sal_Int32>(bConvert ? convertTwipToMm100(GetSmallestDistance()) : GetSmallestDistance())),
                uno::Any(static_cast<sal_Int32>(bConvert ? convertTwipToMm100(mnTopDistance) : mnTopDistance)),
                uno::Any(static_cast<sal_Int32>(bConvert ? convertTwipToMm100(mnBottomDistance) : mnBottomDistance)),
                uno::Any(static_cast<sal_Int32>(bConvert ? convertTwipToMm100(mnLeftDistance) : mnLeftDistance)),
                uno::Any(static_cast<sal_Int32>(bConvert ? convertTwipToMm100(mnRightDistance) : mnRightDistance))
            };
            rVal <<= aSeq;
            return true;
        }
        case MID_LEFT_BORDER:
        case LEFT_BORDER:
            aRetLine = SvxBoxItem::SvxLineToLine(GetLeft(), bConvert);
            break;
        case MID_RIGHT_BORDER:
        case RIGHT_BORDER:
            aRetLine = SvxBoxItem::SvxLineToLine(GetRight(), bConvert);
            break;
        case MID_BOTTOM_BORDER:
        case BOTTOM_BORDER:
            aRetLine = SvxBoxItem::SvxLineToLine(GetBottom(), bConvert);
            break;
        case MID_TOP_BORDER:
        case TOP_BORDER:
            aRetLine = SvxBoxItem::SvxLineToLine(GetTop(), bConvert);
            break;
        case BORDER_DISTANCE:
            nDist = GetSmallestDistance();
            bDistMember = true;
            break;
        case TOP_BORDER_DISTANCE:
            nDist = mnTopDistance;
            bDistMember = true;
            break;
        case BOTTOM_BORDER_DISTANCE:
            nDist = mnBottomDistance;
            bDistMember = true;
            break;
        case LEFT_BORDER_DISTANCE:
            nDist = mnLeftDistance;
            bDistMember = true;
            break;
        case RIGHT_BORDER_DISTANCE:
            nDist = mnRightDistance;
            bDistMember = true;
            break;
        case MID_BORDER_BOTTOM_COLOR:
        {
            if (mpBottomBorderLine)
            {
                rVal <<= model::color::createXComplexColor(mpBottomBorderLine->getComplexColor());
            }
            else if (maTempComplexColors[size_t(SvxBoxItemLine::BOTTOM)].getType() != model::ColorType::Unused)
            {
                rVal <<= model::color::createXComplexColor(maTempComplexColors[size_t(SvxBoxItemLine::BOTTOM)]);
            }
            return true;
        }
        case MID_BORDER_LEFT_COLOR:
        {
            if (mpLeftBorderLine)
            {
                rVal <<= model::color::createXComplexColor(mpLeftBorderLine->getComplexColor());
            }
            else if (maTempComplexColors[size_t(SvxBoxItemLine::LEFT)].getType() != model::ColorType::Unused)
            {
                rVal <<= model::color::createXComplexColor(maTempComplexColors[size_t(SvxBoxItemLine::LEFT)]);
            }
            return true;
        }
        case MID_BORDER_RIGHT_COLOR:
        {
            if (mpRightBorderLine)
            {
                rVal <<= model::color::createXComplexColor(mpRightBorderLine->getComplexColor());
            }
            else if (maTempComplexColors[size_t(SvxBoxItemLine::RIGHT)].getType() != model::ColorType::Unused)
            {
                rVal <<= model::color::createXComplexColor(maTempComplexColors[size_t(SvxBoxItemLine::RIGHT)]);
            }
            return true;
        }
        case MID_BORDER_TOP_COLOR:
        {
            if (mpTopBorderLine)
            {
                rVal <<= model::color::createXComplexColor(mpTopBorderLine->getComplexColor());
            }
            else if (maTempComplexColors[size_t(SvxBoxItemLine::TOP)].getType() != model::ColorType::Unused)
            {
                rVal <<= model::color::createXComplexColor(maTempComplexColors[size_t(SvxBoxItemLine::TOP)]);
            }
            return true;
        }
        case LINE_STYLE:
        case LINE_WIDTH:
            // it doesn't make sense to return a value for these since it's
            // probably ambiguous
            return true;
    }

    if( bDistMember )
        rVal <<= static_cast<sal_Int32>(bConvert ? convertTwipToMm100(nDist) : nDist);
    else
        rVal <<= aRetLine;

    return true;
}

namespace
{

bool
lcl_lineToSvxLine(const table::BorderLine& rLine, SvxBorderLine& rSvxLine, bool bConvert, bool bGuessWidth)
{
    rSvxLine.SetColor( Color(ColorTransparency, rLine.Color));
    if ( bGuessWidth )
    {
        rSvxLine.GuessLinesWidths( rSvxLine.GetBorderLineStyle(),
                bConvert ? o3tl::toTwips(rLine.OuterLineWidth, o3tl::Length::mm100) : rLine.OuterLineWidth,
                bConvert ? o3tl::toTwips(rLine.InnerLineWidth, o3tl::Length::mm100) : rLine.InnerLineWidth,
                bConvert ? o3tl::toTwips(rLine.LineDistance, o3tl::Length::mm100) : rLine.LineDistance );
    }

    bool bRet = !rSvxLine.isEmpty();
    return bRet;
}

}


bool SvxBoxItem::LineToSvxLine(const css::table::BorderLine& rLine, SvxBorderLine& rSvxLine, bool bConvert)
{
    return lcl_lineToSvxLine(rLine, rSvxLine, bConvert, true);
}

bool
SvxBoxItem::LineToSvxLine(const css::table::BorderLine2& rLine, SvxBorderLine& rSvxLine, bool bConvert)
{
    SvxBorderLineStyle const nStyle =
        (rLine.LineStyle < 0 || BORDER_LINE_STYLE_MAX < rLine.LineStyle)
        ? SvxBorderLineStyle::SOLID     // default
        : static_cast<SvxBorderLineStyle>(rLine.LineStyle);

    rSvxLine.SetBorderLineStyle( nStyle );

    bool bGuessWidth = true;
    if ( rLine.LineWidth )
    {
        rSvxLine.SetWidth( bConvert? o3tl::toTwips(rLine.LineWidth, o3tl::Length::mm100) : rLine.LineWidth );
        // fdo#46112: double does not necessarily mean symmetric
        // for backwards compatibility
        bGuessWidth = (SvxBorderLineStyle::DOUBLE == nStyle || SvxBorderLineStyle::DOUBLE_THIN == nStyle) &&
            (rLine.InnerLineWidth > 0) && (rLine.OuterLineWidth > 0);
    }

    return lcl_lineToSvxLine(rLine, rSvxLine, bConvert, bGuessWidth);
}


namespace
{

bool
lcl_extractBorderLine(const uno::Any& rAny, table::BorderLine2& rLine)
{
    if (rAny >>= rLine)
        return true;

    table::BorderLine aBorderLine;
    if (rAny >>= aBorderLine)
    {
        rLine.Color = aBorderLine.Color;
        rLine.InnerLineWidth = aBorderLine.InnerLineWidth;
        rLine.OuterLineWidth = aBorderLine.OuterLineWidth;
        rLine.LineDistance = aBorderLine.LineDistance;
        rLine.LineStyle = table::BorderLineStyle::SOLID;
        return true;
    }

    return false;
}

template<typename Item, typename Line>
bool
lcl_setLine(const uno::Any& rAny, Item& rItem, Line nLine, const bool bConvert)
{
    bool bDone = false;
    table::BorderLine2 aBorderLine;
    if (lcl_extractBorderLine(rAny, aBorderLine))
    {
        SvxBorderLine aLine;
        bool bSet = SvxBoxItem::LineToSvxLine(aBorderLine, aLine, bConvert);
        rItem.SetLine( bSet ? &aLine : nullptr, nLine);
        bDone = true;
    }
    return bDone;
}

}

bool SvxBoxItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    SvxBoxItemLine nLine = SvxBoxItemLine::TOP;
    bool bDistMember = false;
    nMemberId &= ~CONVERT_TWIPS;
    switch(nMemberId)
    {
        case 0:
        {
            uno::Sequence< uno::Any > aSeq;
            if (( rVal >>= aSeq ) && ( aSeq.getLength() == 9 ))
            {
                // 4 Borders and 5 distances
                const SvxBoxItemLine aBorders[] = { SvxBoxItemLine::LEFT, SvxBoxItemLine::RIGHT, SvxBoxItemLine::BOTTOM, SvxBoxItemLine::TOP };
                for (size_t n(0); n != std::size(aBorders); ++n)
                {
                    if (!lcl_setLine(aSeq[n], *this, aBorders[n], bConvert))
                        return false;
                    tryMigrateComplexColor(aBorders[n]);
                }

                // WTH are the borders and the distances saved in different order?
                SvxBoxItemLine const nLines[4] = { SvxBoxItemLine::TOP, SvxBoxItemLine::BOTTOM, SvxBoxItemLine::LEFT, SvxBoxItemLine::RIGHT };
                for ( sal_Int32 n = 4; n < 9; n++ )
                {
                    sal_Int32 nDist = 0;
                    if ( aSeq[n] >>= nDist )
                    {
                        if( bConvert )
                            nDist = o3tl::toTwips(nDist, o3tl::Length::mm100);
                        if ( n == 4 )
                            SetAllDistances(nDist);
                        else
                            SetDistance( nDist, nLines[n-5] );
                    }
                    else
                        return false;
                }

                return true;
            }
            else
                return false;
        }
        case LEFT_BORDER_DISTANCE:
            bDistMember = true;
            [[fallthrough]];
        case LEFT_BORDER:
        case MID_LEFT_BORDER:
            nLine = SvxBoxItemLine::LEFT;
            break;
        case RIGHT_BORDER_DISTANCE:
            bDistMember = true;
            [[fallthrough]];
        case RIGHT_BORDER:
        case MID_RIGHT_BORDER:
            nLine = SvxBoxItemLine::RIGHT;
            break;
        case BOTTOM_BORDER_DISTANCE:
            bDistMember = true;
            [[fallthrough]];
        case BOTTOM_BORDER:
        case MID_BOTTOM_BORDER:
            nLine = SvxBoxItemLine::BOTTOM;
            break;
        case TOP_BORDER_DISTANCE:
            bDistMember = true;
            [[fallthrough]];
        case TOP_BORDER:
        case MID_TOP_BORDER:
            nLine = SvxBoxItemLine::TOP;
            break;
        case LINE_STYLE:
            {
                drawing::LineStyle eDrawingStyle;
                rVal >>= eDrawingStyle;
                SvxBorderLineStyle eBorderStyle = SvxBorderLineStyle::NONE;
                switch ( eDrawingStyle )
                {
                    default:
                    case drawing::LineStyle_NONE:
                        break;
                    case drawing::LineStyle_SOLID:
                        eBorderStyle = SvxBorderLineStyle::SOLID;
                        break;
                    case drawing::LineStyle_DASH:
                        eBorderStyle = SvxBorderLineStyle::DASHED;
                        break;
                }

                // Set the line style on all borders
                for( SvxBoxItemLine n : o3tl::enumrange<SvxBoxItemLine>() )
                {
                    editeng::SvxBorderLine* pLine = const_cast< editeng::SvxBorderLine* >( GetLine( n ) );
                    if( pLine )
                        pLine->SetBorderLineStyle( eBorderStyle );
                }
                return true;
            }
            break;
        case LINE_WIDTH:
            {
                // Set the line width on all borders
                tools::Long nWidth(0);
                rVal >>= nWidth;
                if( bConvert )
                    nWidth = o3tl::toTwips(nWidth, o3tl::Length::mm100);

                // Set the line Width on all borders
                for( SvxBoxItemLine n : o3tl::enumrange<SvxBoxItemLine>() )
                {
                    editeng::SvxBorderLine* pLine = const_cast< editeng::SvxBorderLine* >( GetLine( n ) );
                    if( pLine )
                        pLine->SetWidth( nWidth );
                }
            }
            return true;
        case MID_BORDER_BOTTOM_COLOR:
        {
            if (mpBottomBorderLine)
                return mpBottomBorderLine->setComplexColorFromAny(rVal);
            else
            {
                css::uno::Reference<css::util::XComplexColor> xComplexColor;
                if (!(rVal >>= xComplexColor))
                    return false;

                if (xComplexColor.is())
                    maTempComplexColors[size_t(SvxBoxItemLine::BOTTOM)] = model::color::getFromXComplexColor(xComplexColor);
            }
            return true;
        }
        case MID_BORDER_LEFT_COLOR:
        {
            if (mpLeftBorderLine)
                return mpLeftBorderLine->setComplexColorFromAny(rVal);
            else
            {
                css::uno::Reference<css::util::XComplexColor> xComplexColor;
                if (!(rVal >>= xComplexColor))
                    return false;

                if (xComplexColor.is())
                    maTempComplexColors[size_t(SvxBoxItemLine::LEFT)] = model::color::getFromXComplexColor(xComplexColor);
            }
            return true;
        }
        case MID_BORDER_RIGHT_COLOR:
        {
            if (mpRightBorderLine)
                return mpRightBorderLine->setComplexColorFromAny(rVal);
            else
            {
                css::uno::Reference<css::util::XComplexColor> xComplexColor;
                if (!(rVal >>= xComplexColor))
                    return false;

                if (xComplexColor.is())
                    maTempComplexColors[size_t(SvxBoxItemLine::RIGHT)] = model::color::getFromXComplexColor(xComplexColor);
            }
            return true;
        }
        case MID_BORDER_TOP_COLOR:
        {
            if (mpTopBorderLine)
                return mpTopBorderLine->setComplexColorFromAny(rVal);
            else
            {
                css::uno::Reference<css::util::XComplexColor> xComplexColor;
                if (!(rVal >>= xComplexColor))
                    return false;

                if (xComplexColor.is())
                    maTempComplexColors[size_t(SvxBoxItemLine::TOP)] = model::color::getFromXComplexColor(xComplexColor);
            }
            return true;
        }
    }

    if( bDistMember || nMemberId == BORDER_DISTANCE )
    {
        sal_Int32 nDist = 0;
        if(!(rVal >>= nDist))
            return false;

        {
            if( bConvert )
                nDist = o3tl::toTwips(nDist, o3tl::Length::mm100);
            if( nMemberId == BORDER_DISTANCE )
                SetAllDistances(nDist);
            else
                SetDistance( nDist, nLine );
        }
    }
    else
    {
        SvxBorderLine aLine;
        if( !rVal.hasValue() )
            return false;

        table::BorderLine2 aBorderLine;
        if( lcl_extractBorderLine(rVal, aBorderLine) )
        {
            // usual struct
        }
        else if (rVal.getValueTypeClass() == uno::TypeClass_SEQUENCE )
        {
            // serialization for basic macro recording
            uno::Reference < script::XTypeConverter > xConverter
                    ( script::Converter::create(::comphelper::getProcessComponentContext()) );
            uno::Sequence < uno::Any > aSeq;
            uno::Any aNew;
            try { aNew = xConverter->convertTo( rVal, cppu::UnoType<uno::Sequence < uno::Any >>::get() ); }
            catch (const uno::Exception&) {}

            aNew >>= aSeq;
            if (aSeq.getLength() >= 4 && aSeq.getLength() <= 6)
            {
                sal_Int32 nVal = 0;
                if ( aSeq[0] >>= nVal )
                    aBorderLine.Color = nVal;
                if ( aSeq[1] >>= nVal )
                    aBorderLine.InnerLineWidth = static_cast<sal_Int16>(nVal);
                if ( aSeq[2] >>= nVal )
                    aBorderLine.OuterLineWidth = static_cast<sal_Int16>(nVal);
                if ( aSeq[3] >>= nVal )
                    aBorderLine.LineDistance = static_cast<sal_Int16>(nVal);
                if (aSeq.getLength() >= 5) // fdo#40874 added fields
                {
                    if (aSeq[4] >>= nVal)
                    {
                        aBorderLine.LineStyle = nVal;
                    }
                    if (aSeq.getLength() >= 6)
                    {
                        if (aSeq[5] >>= nVal)
                        {
                            aBorderLine.LineWidth = nVal;
                        }
                    }
                }
            }
            else
                return false;
        }
        else
            return false;

        bool bSet = SvxBoxItem::LineToSvxLine(aBorderLine, aLine, bConvert);
        SetLine(bSet ? &aLine : nullptr, nLine);
        tryMigrateComplexColor(nLine);
    }

    return true;
}

SvxBoxItem* SvxBoxItem::Clone( SfxItemPool* ) const
{
    return new SvxBoxItem( *this );
}

bool SvxBoxItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    OUString cpDelimTmp(cpDelim);
    switch ( ePres )
    {
        case SfxItemPresentation::Nameless:
        {
            rText.clear();

            if (mpTopBorderLine)
            {
                rText = mpTopBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl ) + cpDelimTmp;
            }
            if ( !(mpTopBorderLine && mpBottomBorderLine && mpLeftBorderLine && mpRightBorderLine &&
                  *mpTopBorderLine == *mpBottomBorderLine &&
                  *mpTopBorderLine == *mpLeftBorderLine &&
                  *mpTopBorderLine == *mpRightBorderLine))
            {
                if (mpBottomBorderLine)
                {
                    rText += mpBottomBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl ) + cpDelimTmp;
                }
                if (mpLeftBorderLine)
                {
                    rText += mpLeftBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl ) + cpDelimTmp;
                }
                if (mpRightBorderLine)
                {
                    rText += mpRightBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl ) + cpDelimTmp;
                }
            }
            rText += GetMetricText( static_cast<tools::Long>(mnTopDistance), eCoreUnit, ePresUnit, &rIntl );
            if (mnTopDistance != mnBottomDistance ||
                mnTopDistance != mnLeftDistance ||
                mnTopDistance != mnRightDistance)
            {
                rText += cpDelimTmp +
                        GetMetricText( tools::Long(mnBottomDistance), eCoreUnit, ePresUnit, &rIntl ) +
                        cpDelimTmp +
                        GetMetricText( tools::Long(mnLeftDistance), eCoreUnit, ePresUnit, &rIntl ) +
                        cpDelimTmp +
                        GetMetricText( tools::Long(mnRightDistance), eCoreUnit, ePresUnit, &rIntl );
            }
            return true;
        }
        case SfxItemPresentation::Complete:
        {
            if (!(mpTopBorderLine || mpBottomBorderLine || mpLeftBorderLine || mpRightBorderLine))
            {
                rText = EditResId(RID_SVXITEMS_BORDER_NONE) + cpDelimTmp;
            }
            else
            {
                rText = EditResId(RID_SVXITEMS_BORDER_COMPLETE);
                if (mpTopBorderLine && mpBottomBorderLine && mpLeftBorderLine && mpRightBorderLine &&
                    *mpTopBorderLine == *mpBottomBorderLine &&
                    *mpTopBorderLine == *mpLeftBorderLine &&
                    *mpTopBorderLine == *mpRightBorderLine)
                {
                    rText += mpTopBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl, true ) + cpDelimTmp;
                }
                else
                {
                    if (mpTopBorderLine)
                    {
                        rText += EditResId(RID_SVXITEMS_BORDER_TOP) +
                                mpTopBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl, true ) +
                                cpDelimTmp;
                    }
                    if (mpBottomBorderLine)
                    {
                        rText += EditResId(RID_SVXITEMS_BORDER_BOTTOM) +
                                mpBottomBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl, true ) +
                                cpDelimTmp;
                    }
                    if (mpLeftBorderLine)
                    {
                        rText += EditResId(RID_SVXITEMS_BORDER_LEFT) +
                                mpLeftBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl, true ) +
                                cpDelimTmp;
                    }
                    if (mpRightBorderLine)
                    {
                        rText += EditResId(RID_SVXITEMS_BORDER_RIGHT) +
                                mpRightBorderLine->GetValueString( eCoreUnit, ePresUnit, &rIntl, true ) +
                                cpDelimTmp;
                    }
                }
            }

            rText += EditResId(RID_SVXITEMS_BORDER_DISTANCE);
            if (mnTopDistance == mnBottomDistance &&
                mnTopDistance == mnLeftDistance &&
                mnTopDistance == mnRightDistance)
            {
                rText += GetMetricText(tools::Long(mnTopDistance), eCoreUnit, ePresUnit, &rIntl ) +
                        " " + EditResId(GetMetricId(ePresUnit));
            }
            else
            {
                rText += EditResId(RID_SVXITEMS_BORDER_TOP) +
                        GetMetricText(tools::Long(mnTopDistance), eCoreUnit, ePresUnit, &rIntl) +
                        " " + EditResId(GetMetricId(ePresUnit)) +
                        cpDelimTmp +
                        EditResId(RID_SVXITEMS_BORDER_BOTTOM) +
                        GetMetricText(tools::Long(mnBottomDistance), eCoreUnit, ePresUnit, &rIntl) +
                        " " + EditResId(GetMetricId(ePresUnit)) +
                        cpDelimTmp +
                        EditResId(RID_SVXITEMS_BORDER_LEFT) +
                        GetMetricText(tools::Long(mnLeftDistance), eCoreUnit, ePresUnit, &rIntl) +
                        " " + EditResId(GetMetricId(ePresUnit)) +
                        cpDelimTmp +
                        EditResId(RID_SVXITEMS_BORDER_RIGHT) +
                        GetMetricText(tools::Long(mnRightDistance), eCoreUnit, ePresUnit, &rIntl) +
                        " " + EditResId(GetMetricId(ePresUnit));
            }
            return true;
        }
        default: ; // prevent warning
    }
    return false;
}


void SvxBoxItem::ScaleMetrics( tools::Long nMult, tools::Long nDiv )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    if (mpTopBorderLine)
        mpTopBorderLine->ScaleMetrics( nMult, nDiv );
    if (mpBottomBorderLine)
        mpBottomBorderLine->ScaleMetrics( nMult, nDiv );
    if (mpLeftBorderLine)
        mpLeftBorderLine->ScaleMetrics( nMult, nDiv );
    if (mpRightBorderLine)
        mpRightBorderLine->ScaleMetrics( nMult, nDiv );

    mnTopDistance = static_cast<sal_Int16>(BigInt::Scale(mnTopDistance, nMult, nDiv));
    mnBottomDistance = static_cast<sal_Int16>(BigInt::Scale(mnBottomDistance, nMult, nDiv));
    mnLeftDistance = static_cast<sal_Int16>(BigInt::Scale(mnLeftDistance, nMult, nDiv));
    mnRightDistance = static_cast<sal_Int16>(BigInt::Scale(mnRightDistance, nMult, nDiv));
}


bool SvxBoxItem::HasMetrics() const
{
    return true;
}


const SvxBorderLine *SvxBoxItem::GetLine( SvxBoxItemLine nLine ) const
{
    const SvxBorderLine *pRet = nullptr;

    switch ( nLine )
    {
        case SvxBoxItemLine::TOP:
            pRet = mpTopBorderLine.get();
            break;
        case SvxBoxItemLine::BOTTOM:
            pRet = mpBottomBorderLine.get();
            break;
        case SvxBoxItemLine::LEFT:
            pRet = mpLeftBorderLine.get();
            break;
        case SvxBoxItemLine::RIGHT:
            pRet = mpRightBorderLine.get();
            break;
        default:
            OSL_FAIL( "wrong line" );
            break;
    }

    return pRet;
}


void SvxBoxItem::SetLine( const SvxBorderLine* pNew, SvxBoxItemLine nLine )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    std::unique_ptr<SvxBorderLine> pTmp( pNew ? new SvxBorderLine( *pNew ) : nullptr );

    switch ( nLine )
    {
        case SvxBoxItemLine::TOP:
            mpTopBorderLine = std::move(pTmp);
            break;
        case SvxBoxItemLine::BOTTOM:
            mpBottomBorderLine = std::move(pTmp);
            break;
        case SvxBoxItemLine::LEFT:
            mpLeftBorderLine = std::move(pTmp);
            break;
        case SvxBoxItemLine::RIGHT:
            mpRightBorderLine = std::move(pTmp);
            break;
        default:
            OSL_FAIL( "wrong line" );
    }
}


sal_uInt16 SvxBoxItem::GetSmallestDistance() const
{
    // The smallest distance that is not 0 will be returned.
    sal_uInt16 nDist = mnTopDistance;
    if (mnBottomDistance && (!nDist || mnBottomDistance < nDist))
        nDist = mnBottomDistance;
    if (mnLeftDistance && (!nDist || mnLeftDistance < nDist))
        nDist = mnLeftDistance;
    if (mnRightDistance && (!nDist || mnRightDistance < nDist))
        nDist = mnRightDistance;

    return nDist;
}


sal_Int16 SvxBoxItem::GetDistance( SvxBoxItemLine nLine, bool bAllowNegative ) const
{
    sal_Int16 nDist = 0;
    switch ( nLine )
    {
        case SvxBoxItemLine::TOP:
            nDist = mnTopDistance;
            break;
        case SvxBoxItemLine::BOTTOM:
            nDist = mnBottomDistance;
            break;
        case SvxBoxItemLine::LEFT:
            nDist = mnLeftDistance;
            break;
        case SvxBoxItemLine::RIGHT:
            nDist = mnRightDistance;
            break;
        default:
            OSL_FAIL( "wrong line" );
    }

    if (!bAllowNegative && nDist < 0)
    {
        nDist = 0;
    }
    return nDist;
}


void SvxBoxItem::SetDistance( sal_Int16 nNew, SvxBoxItemLine nLine )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    switch ( nLine )
    {
        case SvxBoxItemLine::TOP:
            mnTopDistance = nNew;
            break;
        case SvxBoxItemLine::BOTTOM:
            mnBottomDistance = nNew;
            break;
        case SvxBoxItemLine::LEFT:
            mnLeftDistance = nNew;
            break;
        case SvxBoxItemLine::RIGHT:
            mnRightDistance = nNew;
            break;
        default:
            OSL_FAIL( "wrong line" );
    }
}

sal_uInt16 SvxBoxItem::CalcLineWidth( SvxBoxItemLine nLine ) const
{
    SvxBorderLine* pTmp = nullptr;
    sal_uInt16 nWidth = 0;
    switch ( nLine )
    {
    case SvxBoxItemLine::TOP:
        pTmp = mpTopBorderLine.get();
        break;
    case SvxBoxItemLine::BOTTOM:
        pTmp = mpBottomBorderLine.get();
        break;
    case SvxBoxItemLine::LEFT:
        pTmp = mpLeftBorderLine.get();
        break;
    case SvxBoxItemLine::RIGHT:
        pTmp = mpRightBorderLine.get();
        break;
    default:
        OSL_FAIL( "wrong line" );
    }

    if( pTmp )
        nWidth = pTmp->GetScaledWidth();

    return nWidth;
}

sal_Int16 SvxBoxItem::CalcLineSpace( SvxBoxItemLine nLine, bool bEvenIfNoLine, bool bAllowNegative ) const
{
    SvxBorderLine* pTmp = nullptr;
    sal_Int16 nDist = 0;
    switch ( nLine )
    {
    case SvxBoxItemLine::TOP:
        pTmp = mpTopBorderLine.get();
        nDist = mnTopDistance;
        break;
    case SvxBoxItemLine::BOTTOM:
        pTmp = mpBottomBorderLine.get();
        nDist = mnBottomDistance;
        break;
    case SvxBoxItemLine::LEFT:
        pTmp = mpLeftBorderLine.get();
        nDist = mnLeftDistance;
        break;
    case SvxBoxItemLine::RIGHT:
        pTmp = mpRightBorderLine.get();
        nDist = mnRightDistance;
        break;
    default:
        OSL_FAIL( "wrong line" );
    }

    if( pTmp )
    {
        nDist = nDist + pTmp->GetScaledWidth();
    }
    else if( !bEvenIfNoLine )
        nDist = 0;

    if (!bAllowNegative && nDist < 0)
    {
        nDist = 0;
    }

    return nDist;
}

void SvxBoxItem::tryMigrateComplexColor(SvxBoxItemLine eLine)
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    if (!GetLine(eLine))
        return;

    auto nIndex = size_t(eLine);

    if (maTempComplexColors[nIndex].getType() == model::ColorType::Unused)
        return;

    switch (eLine)
    {
    case SvxBoxItemLine::TOP:
        mpTopBorderLine->setComplexColor(maTempComplexColors[nIndex]);
        break;
    case SvxBoxItemLine::BOTTOM:
        mpBottomBorderLine->setComplexColor(maTempComplexColors[nIndex]);
        break;
    case SvxBoxItemLine::LEFT:
        mpLeftBorderLine->setComplexColor(maTempComplexColors[nIndex]);
        break;
    case SvxBoxItemLine::RIGHT:
        mpRightBorderLine->setComplexColor(maTempComplexColors[nIndex]);
        break;
    }

    maTempComplexColors[nIndex] = model::ComplexColor();
}

bool SvxBoxItem::HasBorder( bool bTreatPaddingAsBorder ) const
{
    return  CalcLineSpace( SvxBoxItemLine::BOTTOM,   bTreatPaddingAsBorder )
            || CalcLineSpace( SvxBoxItemLine::RIGHT, bTreatPaddingAsBorder )
            || CalcLineSpace( SvxBoxItemLine::TOP,   bTreatPaddingAsBorder )
            || CalcLineSpace( SvxBoxItemLine::LEFT,  bTreatPaddingAsBorder );
}

// class SvxBoxInfoItem --------------------------------------------------

SvxBoxInfoItem::SvxBoxInfoItem(const sal_uInt16 nId)
    : SfxPoolItem(nId)
    , mbDistance(false)
    , mbMinimumDistance(false)
{
    ResetFlags();
}

SvxBoxInfoItem::SvxBoxInfoItem( const SvxBoxInfoItem& rCopy )
    : SfxPoolItem(rCopy)
    , mpHorizontalLine(rCopy.mpHorizontalLine ? new SvxBorderLine(*rCopy.mpHorizontalLine) : nullptr)
    , mpVerticalLine(rCopy.mpVerticalLine ? new SvxBorderLine(*rCopy.mpVerticalLine) : nullptr)
    , mbEnableHorizontalLine(rCopy.mbEnableHorizontalLine)
    , mbEnableVerticalLine(rCopy.mbEnableVerticalLine)
    , mbDistance(rCopy.mbDistance)
    , mbMinimumDistance (rCopy.mbMinimumDistance)
    , mnValidFlags(rCopy.mnValidFlags)
    , mnDefaultMinimumDistance(rCopy.mnDefaultMinimumDistance)
{
}

SvxBoxInfoItem::~SvxBoxInfoItem()
{
}


boost::property_tree::ptree SvxBoxInfoItem::dumpAsJSON() const
{
    boost::property_tree::ptree aTree;

    boost::property_tree::ptree aState;
    aState.put("vertical", GetVert() && !GetVert()->isEmpty());
    aState.put("horizontal", GetHori() && !GetHori()->isEmpty());

    aTree.push_back(std::make_pair("state", aState));
    aTree.put("commandName", ".uno:BorderInner");

    return aTree;
}


bool SvxBoxInfoItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxBoxInfoItem& rBoxInfo = static_cast<const SvxBoxInfoItem&>(rAttr);

    return (mbEnableHorizontalLine == rBoxInfo.mbEnableHorizontalLine
            && mbEnableVerticalLine == rBoxInfo.mbEnableVerticalLine
            && mbDistance == rBoxInfo.mbDistance
            && mbMinimumDistance == rBoxInfo.mbMinimumDistance
            && mnValidFlags == rBoxInfo.mnValidFlags
            && mnDefaultMinimumDistance == rBoxInfo.mnDefaultMinimumDistance
            && CompareBorderLine(mpHorizontalLine, rBoxInfo.GetHori())
            && CompareBorderLine(mpVerticalLine, rBoxInfo.GetVert()));
}


void SvxBoxInfoItem::SetLine( const SvxBorderLine* pNew, SvxBoxInfoItemLine nLine )
{
    std::unique_ptr<SvxBorderLine> pCopy(pNew ? new SvxBorderLine(*pNew) : nullptr);

    if ( SvxBoxInfoItemLine::HORI == nLine )
    {
        mpHorizontalLine = std::move(pCopy);
    }
    else if ( SvxBoxInfoItemLine::VERT == nLine )
    {
        mpVerticalLine = std::move(pCopy);
    }
    else
    {
        OSL_FAIL( "wrong line" );
    }
}

SvxBoxInfoItem* SvxBoxInfoItem::Clone( SfxItemPool* ) const
{
    return new SvxBoxInfoItem( *this );
}

bool SvxBoxInfoItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&
)   const
{
    rText.clear();
    return false;
}


void SvxBoxInfoItem::ScaleMetrics( tools::Long nMult, tools::Long nDiv )
{
    if (mpHorizontalLine)
        mpHorizontalLine->ScaleMetrics(nMult, nDiv);
    if (mpVerticalLine)
        mpVerticalLine->ScaleMetrics(nMult, nDiv);
    mnDefaultMinimumDistance = sal_uInt16(BigInt::Scale(mnDefaultMinimumDistance, nMult, nDiv));
}


bool SvxBoxInfoItem::HasMetrics() const
{
    return true;
}


void SvxBoxInfoItem::ResetFlags()
{
    mnValidFlags = static_cast<SvxBoxInfoItemValidFlags>(0x7F); // all valid except Disable
}

bool SvxBoxInfoItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    bool bConvert = 0 != (nMemberId & CONVERT_TWIPS);
    table::BorderLine2 aRetLine;
    sal_Int16 nVal=0;
    bool bIntMember = false;
    nMemberId &= ~CONVERT_TWIPS;
    switch(nMemberId)
    {
        case 0:
        {
            // 2 BorderLines, flags, valid flags and distance
            if ( IsTable() )
                nVal |= 0x01;
            if ( IsDist() )
                nVal |= 0x02;
            if ( IsMinDist() )
                nVal |= 0x04;
            css::uno::Sequence< css::uno::Any > aSeq{
                uno::Any(SvxBoxItem::SvxLineToLine(mpHorizontalLine.get(), bConvert)),
                uno::Any(SvxBoxItem::SvxLineToLine(mpVerticalLine.get(), bConvert)),
                uno::Any(nVal),
                uno::Any(static_cast<sal_Int16>(mnValidFlags)),
                uno::Any(static_cast<sal_Int32>(bConvert ? convertTwipToMm100(GetDefDist()) : GetDefDist()))
            };
            rVal <<= aSeq;
            return true;
        }

        case MID_HORIZONTAL:
            aRetLine = SvxBoxItem::SvxLineToLine(mpHorizontalLine.get(), bConvert);
            break;
        case MID_VERTICAL:
            aRetLine = SvxBoxItem::SvxLineToLine(mpVerticalLine.get(), bConvert);
            break;
        case MID_FLAGS:
            bIntMember = true;
            if ( IsTable() )
                nVal |= 0x01;
            if ( IsDist() )
                nVal |= 0x02;
            if ( IsMinDist() )
                nVal |= 0x04;
            rVal <<= nVal;
            break;
        case MID_VALIDFLAGS:
            bIntMember = true;
            rVal <<= static_cast<sal_Int16>(mnValidFlags);
            break;
        case MID_DISTANCE:
            bIntMember = true;
            rVal <<= static_cast<sal_Int32>(bConvert ? convertTwipToMm100(GetDefDist()) : GetDefDist());
            break;
        default: OSL_FAIL("Wrong MemberId!"); return false;
    }

    if( !bIntMember )
        rVal <<= aRetLine;

    return true;
}


bool SvxBoxInfoItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    bool bConvert = 0!=(nMemberId&CONVERT_TWIPS);
    nMemberId &= ~CONVERT_TWIPS;
    bool bRet;
    switch(nMemberId)
    {
        case 0:
        {
            css::uno::Sequence< css::uno::Any > aSeq;
            if (( rVal >>= aSeq ) && ( aSeq.getLength() == 5 ))
            {
                // 2 BorderLines, flags, valid flags and distance
                if (!lcl_setLine(aSeq[0], *this, SvxBoxInfoItemLine::HORI, bConvert))
                    return false;
                if (!lcl_setLine(aSeq[1], *this, SvxBoxInfoItemLine::VERT, bConvert))
                    return false;

                sal_Int16 nFlags( 0 );
                sal_Int32 nVal( 0 );
                if ( aSeq[2] >>= nFlags )
                {
                    SetTable  ( ( nFlags & 0x01 ) != 0 );
                    SetDist   ( ( nFlags & 0x02 ) != 0 );
                    SetMinDist( ( nFlags & 0x04 ) != 0 );
                }
                else
                    return false;
                if ( aSeq[3] >>= nFlags )
                    mnValidFlags = static_cast<SvxBoxInfoItemValidFlags>(nFlags);
                else
                    return false;
                if (( aSeq[4] >>= nVal ) && ( nVal >= 0 ))
                {
                    if( bConvert )
                        nVal = o3tl::toTwips(nVal, o3tl::Length::mm100);
                    SetDefDist( nVal );
                }
            }
            return true;
        }

        case MID_HORIZONTAL:
        case MID_VERTICAL:
        {
            if( !rVal.hasValue() )
                return false;

            table::BorderLine2 aBorderLine;
            if( lcl_extractBorderLine(rVal, aBorderLine) )
            {
                // usual struct
            }
            else if (rVal.getValueTypeClass() == uno::TypeClass_SEQUENCE )
            {
                // serialization for basic macro recording
                uno::Reference < script::XTypeConverter > xConverter( script::Converter::create(::comphelper::getProcessComponentContext()) );
                uno::Any aNew;
                uno::Sequence < uno::Any > aSeq;
                try { aNew = xConverter->convertTo( rVal, cppu::UnoType<uno::Sequence < uno::Any >>::get() ); }
                catch (const uno::Exception&) {}

                if ((aNew >>= aSeq) &&
                    aSeq.getLength() >= 4  && aSeq.getLength() <= 6)
                {
                    sal_Int32 nVal = 0;
                    if ( aSeq[0] >>= nVal )
                        aBorderLine.Color = nVal;
                    if ( aSeq[1] >>= nVal )
                        aBorderLine.InnerLineWidth = static_cast<sal_Int16>(nVal);
                    if ( aSeq[2] >>= nVal )
                        aBorderLine.OuterLineWidth = static_cast<sal_Int16>(nVal);
                    if ( aSeq[3] >>= nVal )
                        aBorderLine.LineDistance = static_cast<sal_Int16>(nVal);
                    if (aSeq.getLength() >= 5) // fdo#40874 added fields
                    {
                        if (aSeq[4] >>= nVal)
                        {
                            aBorderLine.LineStyle = nVal;
                        }
                        if (aSeq.getLength() >= 6)
                        {
                            if (aSeq[5] >>= nVal)
                            {
                                aBorderLine.LineWidth = nVal;
                            }
                        }
                    }
                }
                else
                    return false;
            }
            else if (rVal.getValueType() == cppu::UnoType<css::uno::Sequence < sal_Int16 >>::get() )
            {
                // serialization for basic macro recording
                css::uno::Sequence < sal_Int16 > aSeq;
                rVal >>= aSeq;
                if (aSeq.getLength() >= 4 && aSeq.getLength() <= 6)
                {
                    aBorderLine.Color = aSeq[0];
                    aBorderLine.InnerLineWidth = aSeq[1];
                    aBorderLine.OuterLineWidth = aSeq[2];
                    aBorderLine.LineDistance = aSeq[3];
                    if (aSeq.getLength() >= 5) // fdo#40874 added fields
                    {
                        aBorderLine.LineStyle = aSeq[4];
                        if (aSeq.getLength() >= 6)
                        {
                            aBorderLine.LineWidth = aSeq[5];
                        }
                    }
                }
                else
                    return false;
            }
            else
                return false;

            SvxBorderLine aLine;
            bool bSet = SvxBoxItem::LineToSvxLine(aBorderLine, aLine, bConvert);
            if ( bSet )
                SetLine( &aLine, nMemberId == MID_HORIZONTAL ? SvxBoxInfoItemLine::HORI : SvxBoxInfoItemLine::VERT );
            break;
        }
        case MID_FLAGS:
        {
            sal_Int16 nFlags = sal_Int16();
            bRet = (rVal >>= nFlags);
            if ( bRet )
            {
                SetTable  ( ( nFlags & 0x01 ) != 0 );
                SetDist   ( ( nFlags & 0x02 ) != 0 );
                SetMinDist( ( nFlags & 0x04 ) != 0 );
            }

            break;
        }
        case MID_VALIDFLAGS:
        {
            sal_Int16 nFlags = sal_Int16();
            bRet = (rVal >>= nFlags);
            if ( bRet )
                mnValidFlags = static_cast<SvxBoxInfoItemValidFlags>(nFlags);
            break;
        }
        case MID_DISTANCE:
        {
            sal_Int32 nVal = 0;
            bRet = (rVal >>= nVal);
            if ( bRet && nVal>=0 )
            {
                if( bConvert )
                    nVal = o3tl::toTwips(nVal, o3tl::Length::mm100);
                SetDefDist( static_cast<sal_uInt16>(nVal) );
            }
            break;
        }
        default: OSL_FAIL("Wrong MemberId!"); return false;
    }

    return true;
}


namespace editeng
{

void BorderDistanceFromWord(bool bFromEdge, sal_Int32& nMargin, sal_Int32& nBorderDistance,
    sal_Int32 nBorderWidth)
{
    // See https://wiki.openoffice.org/wiki/Writer/MSInteroperability/PageBorder

    sal_Int32 nNewMargin = nMargin;
    sal_Int32 nNewBorderDistance = nBorderDistance;

    if (bFromEdge)
    {
        nNewMargin = nBorderDistance;
        nNewBorderDistance = nMargin - nBorderDistance - nBorderWidth;
    }
    else
    {
        nNewMargin -= nBorderDistance + nBorderWidth;
    }

    // Ensure correct distance from page edge to text in cases not supported by us:
    // when border is outside entire page area (!bFromEdge && BorderDistance > Margin),
    // and when border is inside page body area (bFromEdge && BorderDistance > Margin)
    if (nNewMargin < 0)
    {
        nNewMargin = 0;
        nNewBorderDistance = std::max<sal_Int32>(nMargin - nBorderWidth, 0);
    }
    else if (nNewBorderDistance < 0)
    {
        nNewMargin = nMargin;
    }

    nMargin = nNewMargin;
    nBorderDistance = nNewBorderDistance;
}

// Heuristics to decide if we need to use "from edge" offset of borders
//
// There are two cases when we can safely use "from text" or "from edge" offset without distorting
// border position (modulo rounding errors):
// 1. When distance of all borders from text is no greater than 31 pt, we use "from text"
// 2. Otherwise, if distance of all borders from edge is no greater than 31 pt, we use "from edge"
// In all other cases, the position of borders would be distorted on export, because Word doesn't
// support the offset of >31 pts (https://msdn.microsoft.com/en-us/library/ff533820), and we need
// to decide which type of offset would provide less wrong result (i.e., the result would look
// closer to original). Here, we just check sum of distances from text to borders, and if it is
// less than sum of distances from borders to edges. The alternative would be to compare total areas
// between text-and-borders and between borders-and-edges (taking into account different lengths of
// borders, and visual impact of that).
void BorderDistancesToWord(const SvxBoxItem& rBox, const WordPageMargins& rMargins,
    WordBorderDistances& rDistances)
{
    // Use signed sal_Int32 that can hold sal_uInt16, to prevent overflow at subtraction below
    const sal_Int32 nT = rBox.GetDistance(SvxBoxItemLine::TOP, /*bAllowNegative=*/true);
    const sal_Int32 nL = rBox.GetDistance(SvxBoxItemLine::LEFT, /*bAllowNegative=*/true);
    const sal_Int32 nB = rBox.GetDistance(SvxBoxItemLine::BOTTOM, /*bAllowNegative=*/true);
    const sal_Int32 nR = rBox.GetDistance(SvxBoxItemLine::RIGHT, /*bAllowNegative=*/true);

    // Only take into account existing borders
    const SvxBorderLine* pLnT = rBox.GetLine(SvxBoxItemLine::TOP);
    const SvxBorderLine* pLnL = rBox.GetLine(SvxBoxItemLine::LEFT);
    const SvxBorderLine* pLnB = rBox.GetLine(SvxBoxItemLine::BOTTOM);
    const SvxBorderLine* pLnR = rBox.GetLine(SvxBoxItemLine::RIGHT);

    // We need to take border widths into account
    const tools::Long nWidthT = pLnT ? pLnT->GetScaledWidth() : 0;
    const tools::Long nWidthL = pLnL ? pLnL->GetScaledWidth() : 0;
    const tools::Long nWidthB = pLnB ? pLnB->GetScaledWidth() : 0;
    const tools::Long nWidthR = pLnR ? pLnR->GetScaledWidth() : 0;

    // Resulting distances from text to borders
    const sal_Int32 nT2BT = pLnT ? nT : 0;
    const sal_Int32 nT2BL = pLnL ? nL : 0;
    const sal_Int32 nT2BB = pLnB ? nB : 0;
    const sal_Int32 nT2BR = pLnR ? nR : 0;

    // Resulting distances from edge to borders
    const sal_Int32 nE2BT = pLnT ? std::max<sal_Int32>(rMargins.nTop - nT - nWidthT, 0) : 0;
    const sal_Int32 nE2BL = pLnL ? std::max<sal_Int32>(rMargins.nLeft - nL - nWidthL, 0) : 0;
    const sal_Int32 nE2BB = pLnB ? std::max<sal_Int32>(rMargins.nBottom - nB - nWidthB, 0) : 0;
    const sal_Int32 nE2BR = pLnR ? std::max<sal_Int32>(rMargins.nRight - nR - nWidthR, 0) : 0;

    const sal_Int32 n32pt = 32 * 20;
    // 1. If all borders are in range of 31 pts from text
    if (nT2BT >= 0 && nT2BT < n32pt && nT2BL >= 0 && nT2BL < n32pt && nT2BB >= 0 && nT2BB < n32pt && nT2BR >= 0 && nT2BR < n32pt)
    {
        rDistances.bFromEdge = false;
    }
    else
    {
        // 2. If all borders are in range of 31 pts from edge
        if (nE2BT < n32pt && nE2BL < n32pt && nE2BB < n32pt && nE2BR < n32pt)
        {
            rDistances.bFromEdge = true;
        }
        else
        {
            // Let's try to guess which would be the best approximation
            rDistances.bFromEdge =
                (nT2BT + nT2BL + nT2BB + nT2BR) > (nE2BT + nE2BL + nE2BB + nE2BR);
        }
    }

    if (rDistances.bFromEdge)
    {
        rDistances.nTop = sal::static_int_cast<sal_uInt16>(nE2BT);
        rDistances.nLeft = sal::static_int_cast<sal_uInt16>(nE2BL);
        rDistances.nBottom = sal::static_int_cast<sal_uInt16>(nE2BB);
        rDistances.nRight = sal::static_int_cast<sal_uInt16>(nE2BR);
    }
    else
    {
        rDistances.nTop = sal::static_int_cast<sal_uInt16>(nT2BT);
        rDistances.nLeft = sal::static_int_cast<sal_uInt16>(nT2BL);
        rDistances.nBottom = sal::static_int_cast<sal_uInt16>(nT2BB);
        rDistances.nRight = sal::static_int_cast<sal_uInt16>(nT2BR);
    }
}

}

// class SvxFormatBreakItem -------------------------------------------------

bool SvxFormatBreakItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    return GetValue() == static_cast<const SvxFormatBreakItem&>( rAttr ).GetValue();
}


bool SvxFormatBreakItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&
)   const
{
    rText = GetValueTextByPos( GetEnumValue() );
    return true;
}

OUString SvxFormatBreakItem::GetValueTextByPos( sal_uInt16 nPos )
{
    static const TranslateId RID_SVXITEMS_BREAK[] =
    {
        RID_SVXITEMS_BREAK_NONE,
        RID_SVXITEMS_BREAK_COLUMN_BEFORE,
        RID_SVXITEMS_BREAK_COLUMN_AFTER,
        RID_SVXITEMS_BREAK_COLUMN_BOTH,
        RID_SVXITEMS_BREAK_PAGE_BEFORE,
        RID_SVXITEMS_BREAK_PAGE_AFTER,
        RID_SVXITEMS_BREAK_PAGE_BOTH
    };
    static_assert(std::size(RID_SVXITEMS_BREAK) == size_t(SvxBreak::End), "unexpected size");
    assert(nPos < sal_uInt16(SvxBreak::End) && "enum overflow!");
    return EditResId(RID_SVXITEMS_BREAK[nPos]);
}

bool SvxFormatBreakItem::QueryValue( uno::Any& rVal, sal_uInt8 /*nMemberId*/ ) const
{
    style::BreakType eBreak = style::BreakType_NONE;
    switch ( GetBreak() )
    {
        case SvxBreak::ColumnBefore:   eBreak = style::BreakType_COLUMN_BEFORE; break;
        case SvxBreak::ColumnAfter:    eBreak = style::BreakType_COLUMN_AFTER ; break;
        case SvxBreak::ColumnBoth:     eBreak = style::BreakType_COLUMN_BOTH  ; break;
        case SvxBreak::PageBefore:     eBreak = style::BreakType_PAGE_BEFORE  ; break;
        case SvxBreak::PageAfter:      eBreak = style::BreakType_PAGE_AFTER   ; break;
        case SvxBreak::PageBoth:       eBreak = style::BreakType_PAGE_BOTH    ; break;
        default: ; // prevent warning
    }
    rVal <<= eBreak;
    return true;
}

bool SvxFormatBreakItem::PutValue( const uno::Any& rVal, sal_uInt8 /*nMemberId*/ )
{
    style::BreakType nBreak;

    if(!(rVal >>= nBreak))
    {
        sal_Int32 nValue = 0;
        if(!(rVal >>= nValue))
            return false;

        nBreak = static_cast<style::BreakType>(nValue);
    }

    SvxBreak eBreak = SvxBreak::NONE;
    switch( nBreak )
    {
        case style::BreakType_COLUMN_BEFORE:    eBreak = SvxBreak::ColumnBefore; break;
        case style::BreakType_COLUMN_AFTER: eBreak = SvxBreak::ColumnAfter;  break;
        case style::BreakType_COLUMN_BOTH:      eBreak = SvxBreak::ColumnBoth;   break;
        case style::BreakType_PAGE_BEFORE:      eBreak = SvxBreak::PageBefore;   break;
        case style::BreakType_PAGE_AFTER:       eBreak = SvxBreak::PageAfter;    break;
        case style::BreakType_PAGE_BOTH:        eBreak = SvxBreak::PageBoth;     break;
        default: ; // prevent warning
    }
    SetValue(eBreak);

    return true;
}

SvxFormatBreakItem* SvxFormatBreakItem::Clone( SfxItemPool* ) const
{
    return new SvxFormatBreakItem( *this );
}

SvxFormatKeepItem* SvxFormatKeepItem::Clone( SfxItemPool* ) const
{
    return new SvxFormatKeepItem( *this );
}

bool SvxFormatKeepItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&
    ) const
{
    TranslateId pId = RID_SVXITEMS_FMTKEEP_FALSE;

    if ( GetValue() )
        pId = RID_SVXITEMS_FMTKEEP_TRUE;
    rText = EditResId(pId);
    return true;
}

void SvxFormatKeepItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxFormatKeepItem"));

    SfxBoolItem::dumpAsXml(pWriter);

    (void)xmlTextWriterEndElement(pWriter);
}

SvxLineItem::SvxLineItem( const sal_uInt16 nId ) :
    SfxPoolItem ( nId )
{
}


SvxLineItem::SvxLineItem( const SvxLineItem& rCpy ) :
    SfxPoolItem ( rCpy ),
    pLine(rCpy.pLine ? new SvxBorderLine( *rCpy.pLine ) : nullptr)
{
}


SvxLineItem::~SvxLineItem()
{
}


bool SvxLineItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    return CompareBorderLine(pLine, static_cast<const SvxLineItem&>(rAttr).GetLine());
}

SvxLineItem* SvxLineItem::Clone( SfxItemPool* ) const
{
    return new SvxLineItem( *this );
}

bool SvxLineItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemId ) const
{
    bool bConvert = 0!=(nMemId&CONVERT_TWIPS);
    nMemId &= ~CONVERT_TWIPS;
    if ( nMemId == 0 )
    {
        rVal <<= SvxBoxItem::SvxLineToLine(pLine.get(), bConvert);
        return true;
    }
    else if ( pLine )
    {
        switch ( nMemId )
        {
            case MID_FG_COLOR:      rVal <<= pLine->GetColor(); break;
            case MID_OUTER_WIDTH:   rVal <<= sal_Int32(pLine->GetOutWidth());   break;
            case MID_INNER_WIDTH:   rVal <<= sal_Int32(pLine->GetInWidth( ));   break;
            case MID_DISTANCE:      rVal <<= sal_Int32(pLine->GetDistance());   break;
            default:
                OSL_FAIL( "Wrong MemberId" );
                return false;
        }
    }

    return true;
}


bool SvxLineItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemId )
{
    bool bConvert = 0!=(nMemId&CONVERT_TWIPS);
    nMemId &= ~CONVERT_TWIPS;
    sal_Int32 nVal = 0;
    if ( nMemId == 0 )
    {
        table::BorderLine2 aLine;
        if ( lcl_extractBorderLine(rVal, aLine) )
        {
            if ( !pLine )
                pLine.reset( new SvxBorderLine );
            if( !SvxBoxItem::LineToSvxLine(aLine, *pLine, bConvert) )
                pLine.reset();
            return true;
        }
        return false;
    }
    else if ( rVal >>= nVal )
    {
        if ( !pLine )
            pLine.reset( new SvxBorderLine );

        switch ( nMemId )
        {
            case MID_FG_COLOR:      pLine->SetColor( Color(ColorTransparency, nVal) ); break;
            case MID_LINE_STYLE:
                pLine->SetBorderLineStyle(static_cast<SvxBorderLineStyle>(nVal));
            break;
            default:
                OSL_FAIL( "Wrong MemberId" );
                return false;
        }

        return true;
    }

    return false;
}


bool SvxLineItem::GetPresentation
(
    SfxItemPresentation ePres,
    MapUnit             eCoreUnit,
    MapUnit             ePresUnit,
    OUString&           rText, const IntlWrapper& rIntl
)   const
{
    rText.clear();

    if ( pLine )
        rText = pLine->GetValueString( eCoreUnit, ePresUnit, &rIntl,
            (SfxItemPresentation::Complete == ePres) );
    return true;
}


void SvxLineItem::ScaleMetrics( tools::Long nMult, tools::Long nDiv )
{
    if ( pLine ) pLine->ScaleMetrics( nMult, nDiv );
}


bool SvxLineItem::HasMetrics() const
{
    return true;
}


void SvxLineItem::SetLine( const SvxBorderLine* pNew )
{
    pLine.reset( pNew ? new SvxBorderLine( *pNew ) : nullptr );
}

ItemInstanceManager* SvxBrushItem::getItemInstanceManager() const
{
    static DefaultItemInstanceManager aInstanceManager(ItemType());
    return &aInstanceManager;
}

SvxBrushItem::SvxBrushItem(sal_uInt16 _nWhich)
    : SfxPoolItem(_nWhich )
    , aColor(COL_TRANSPARENT)
    , aFilterColor(COL_TRANSPARENT)
    , nShadingValue(ShadingPattern::CLEAR)
    , nGraphicTransparency(0)
    , eGraphicPos(GPOS_NONE)
    , bLoadAgain(true)
{
}

SvxBrushItem::SvxBrushItem(const Color& rColor, sal_uInt16 _nWhich)
    : SfxPoolItem(_nWhich )
    , aColor(rColor)
    , aFilterColor(COL_TRANSPARENT)
    , nShadingValue(ShadingPattern::CLEAR)
    , nGraphicTransparency(0)
    , eGraphicPos(GPOS_NONE)
    , bLoadAgain(true)
{
}

SvxBrushItem::SvxBrushItem(Color const& rColor, model::ComplexColor const& rComplexColor, sal_uInt16 nWhich)
    : SfxPoolItem(nWhich)
    , aColor(rColor)
    , maComplexColor(rComplexColor)
    , aFilterColor(COL_TRANSPARENT)
    , nShadingValue(ShadingPattern::CLEAR)
    , nGraphicTransparency(0)
    , eGraphicPos(GPOS_NONE)
    , bLoadAgain(true)
{
}

SvxBrushItem::SvxBrushItem(const Graphic& rGraphic, SvxGraphicPosition ePos, sal_uInt16 _nWhich)
    : SfxPoolItem(_nWhich)
    , aColor(COL_TRANSPARENT)
    , aFilterColor(COL_TRANSPARENT)
    , nShadingValue(ShadingPattern::CLEAR)
    , xGraphicObject(new GraphicObject(rGraphic))
    , nGraphicTransparency(0)
    , eGraphicPos((GPOS_NONE != ePos) ? ePos : GPOS_MM)
    , bLoadAgain(true)
{
    DBG_ASSERT( GPOS_NONE != ePos, "SvxBrushItem-Ctor with GPOS_NONE == ePos" );
}

SvxBrushItem::SvxBrushItem(const GraphicObject& rGraphicObj, SvxGraphicPosition ePos, sal_uInt16 _nWhich)
    : SfxPoolItem(_nWhich)
    , aColor(COL_TRANSPARENT)
    , aFilterColor(COL_TRANSPARENT)
    , nShadingValue(ShadingPattern::CLEAR)
    , xGraphicObject(new GraphicObject(rGraphicObj))
    , nGraphicTransparency(0)
    , eGraphicPos((GPOS_NONE != ePos) ? ePos : GPOS_MM)
    , bLoadAgain(true)
{
    DBG_ASSERT( GPOS_NONE != ePos, "SvxBrushItem-Ctor with GPOS_NONE == ePos" );
}

SvxBrushItem::SvxBrushItem(OUString aLink, OUString aFilter,
                           SvxGraphicPosition ePos, sal_uInt16 _nWhich)
    : SfxPoolItem(_nWhich)
    , aColor(COL_TRANSPARENT)
    , aFilterColor(COL_TRANSPARENT)
    , nShadingValue(ShadingPattern::CLEAR)
    , nGraphicTransparency(0)
    , maStrLink(std::move(aLink))
    , maStrFilter(std::move(aFilter))
    , eGraphicPos((GPOS_NONE != ePos) ? ePos : GPOS_MM)
    , bLoadAgain(true)
{
    DBG_ASSERT( GPOS_NONE != ePos, "SvxBrushItem-Ctor with GPOS_NONE == ePos" );
}

SvxBrushItem::SvxBrushItem(const SvxBrushItem& rItem)
    : SfxPoolItem(rItem)
    , aColor(rItem.aColor)
    , maComplexColor(rItem.maComplexColor)
    , aFilterColor(rItem.aFilterColor)
    , nShadingValue(rItem.nShadingValue)
    , xGraphicObject(rItem.xGraphicObject ? new GraphicObject(*rItem.xGraphicObject) : nullptr)
    , nGraphicTransparency(rItem.nGraphicTransparency)
    , maStrLink(rItem.maStrLink)
    , maStrFilter(rItem.maStrFilter)
    , eGraphicPos(rItem.eGraphicPos)
    , bLoadAgain(rItem.bLoadAgain)
{
}

SvxBrushItem::SvxBrushItem(SvxBrushItem&& rItem)
    : SfxPoolItem(std::move(rItem))
    , aColor(std::move(rItem.aColor))
    , maComplexColor(std::move(rItem.maComplexColor))
    , aFilterColor(std::move(rItem.aFilterColor))
    , nShadingValue(std::move(rItem.nShadingValue))
    , xGraphicObject(std::move(rItem.xGraphicObject))
    , nGraphicTransparency(std::move(rItem.nGraphicTransparency))
    , maStrLink(std::move(rItem.maStrLink))
    , maStrFilter(std::move(rItem.maStrFilter))
    , eGraphicPos(std::move(rItem.eGraphicPos))
    , bLoadAgain(std::move(rItem.bLoadAgain))
{
}

SvxBrushItem::~SvxBrushItem()
{
}

bool SvxBrushItem::isUsed() const
{
    if (GPOS_NONE != GetGraphicPos())
    {
        // graphic used
        return true;
    }
    else if (!GetColor().IsFullyTransparent())
    {
        // color used
        return true;
    }

    return false;
}


static sal_Int8 lcl_PercentToTransparency(tools::Long nPercent)
{
    // 0xff must not be returned!
    return sal_Int8(nPercent ? (50 + 0xfe * nPercent) / 100 : 0);
}


sal_Int8 SvxBrushItem::TransparencyToPercent(sal_Int32 nTrans)
{
    return static_cast<sal_Int8>((nTrans * 100 + 127) / 254);
}


bool SvxBrushItem::QueryValue( uno::Any& rVal, sal_uInt8 nMemberId ) const
{
    nMemberId &= ~CONVERT_TWIPS;
    switch( nMemberId)
    {
        case MID_BACK_COLOR:
            rVal <<= aColor;
        break;
        case MID_BACK_COLOR_R_G_B:
            rVal <<= aColor.GetRGBColor();
        break;
        case MID_BACK_COLOR_TRANSPARENCY:
            rVal <<= SvxBrushItem::TransparencyToPercent(255 - aColor.GetAlpha());
        break;

        case MID_BACKGROUND_COMPLEX_COLOR:
        {
            auto xComplexColor = model::color::createXComplexColor(maComplexColor);
            rVal <<= xComplexColor;
            break;
        }
        break;

        case MID_GRAPHIC_POSITION:
            rVal <<= static_cast<style::GraphicLocation>(static_cast<sal_Int16>(eGraphicPos));
        break;

        case MID_GRAPHIC_TRANSPARENT:
            rVal <<= ( aColor.GetAlpha() == 0 );
        break;

        case MID_GRAPHIC_URL:
        case MID_GRAPHIC:
        {
            uno::Reference<graphic::XGraphic> xGraphic;
            if (!maStrLink.isEmpty())
            {
                Graphic aGraphic(vcl::graphic::loadFromURL(maStrLink));
                xGraphic = aGraphic.GetXGraphic();
            }
            else if (xGraphicObject)
            {
                xGraphic = xGraphicObject->GetGraphic().GetXGraphic();
            }
            rVal <<= xGraphic;
        }
        break;

        case MID_GRAPHIC_FILTER:
        {
            rVal <<= maStrFilter;
        }
        break;

        case MID_GRAPHIC_TRANSPARENCY:
            rVal <<= nGraphicTransparency;
        break;

        case MID_SHADING_VALUE:
        {
            rVal <<= nShadingValue;
        }
        break;
    }

    return true;
}


bool SvxBrushItem::PutValue( const uno::Any& rVal, sal_uInt8 nMemberId )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    nMemberId &= ~CONVERT_TWIPS;
    switch( nMemberId)
    {
        case MID_BACK_COLOR:
        case MID_BACK_COLOR_R_G_B:
        {
            Color aNewCol;
            if ( !( rVal >>= aNewCol ) )
                return false;
            if(MID_BACK_COLOR_R_G_B == nMemberId)
            {
                aNewCol.SetAlpha(aColor.GetAlpha());
            }
            ASSERT_CHANGE_REFCOUNTED_ITEM;
            aColor = aNewCol;
        }
        break;
        case MID_BACK_COLOR_TRANSPARENCY:
        {
            sal_Int32 nTrans = 0;
            if ( !( rVal >>= nTrans ) || nTrans < 0 || nTrans > 100 )
                return false;
            ASSERT_CHANGE_REFCOUNTED_ITEM;
            aColor.SetAlpha(255 - lcl_PercentToTransparency(nTrans));
        }
        break;

        case MID_BACKGROUND_COMPLEX_COLOR:
        {
            css::uno::Reference<css::util::XComplexColor> xComplexColor;
            if (!(rVal >>= xComplexColor))
                return false;

            if (xComplexColor.is())
            {
                ASSERT_CHANGE_REFCOUNTED_ITEM;
                maComplexColor = model::color::getFromXComplexColor(xComplexColor);
            }
        }
        break;

        case MID_GRAPHIC_POSITION:
        {
            style::GraphicLocation eLocation;
            if ( !( rVal>>=eLocation ) )
            {
                sal_Int32 nValue = 0;
                if ( !( rVal >>= nValue ) )
                    return false;
                eLocation = static_cast<style::GraphicLocation>(nValue);
            }
            ASSERT_CHANGE_REFCOUNTED_ITEM;
            SetGraphicPos( static_cast<SvxGraphicPosition>(static_cast<sal_uInt16>(eLocation)) );
        }
        break;

        case MID_GRAPHIC_TRANSPARENT:
            ASSERT_CHANGE_REFCOUNTED_ITEM;
            aColor.SetAlpha( Any2Bool( rVal ) ? 0 : 255 );
        break;

        case MID_GRAPHIC_URL:
        case MID_GRAPHIC:
        {
            Graphic aGraphic;

            if (OUString aURL; rVal >>= aURL)
            {
                aGraphic = vcl::graphic::loadFromURL(aURL);
            }
            else if (uno::Reference<graphic::XGraphic> xGraphic; rVal >>= xGraphic)
            {
                aGraphic = Graphic(xGraphic);
            }

            if (!aGraphic.IsNone())
            {
                maStrLink.clear();

                ASSERT_CHANGE_REFCOUNTED_ITEM;
                std::unique_ptr<GraphicObject> xOldGrfObj(std::move(xGraphicObject));
                xGraphicObject.reset(new GraphicObject(std::move(aGraphic)));
                ApplyGraphicTransparency_Impl();
                xOldGrfObj.reset();

                if (eGraphicPos == GPOS_NONE)
                {
                    eGraphicPos = GPOS_MM;
                }
            }
            else
            {
                eGraphicPos = GPOS_NONE;
            }
        }
        break;

        case MID_GRAPHIC_FILTER:
        {
            if( rVal.getValueType() == ::cppu::UnoType<OUString>::get() )
            {
                OUString sLink;
                rVal >>= sLink;
                ASSERT_CHANGE_REFCOUNTED_ITEM;
                SetGraphicFilter( sLink );
            }
        }
        break;
        case MID_GRAPHIC_TRANSPARENCY :
        {
            sal_Int32 nTmp = 0;
            rVal >>= nTmp;
            if(nTmp >= 0 && nTmp <= 100)
            {
                ASSERT_CHANGE_REFCOUNTED_ITEM;
                nGraphicTransparency = sal_Int8(nTmp);
                if (xGraphicObject)
                    ApplyGraphicTransparency_Impl();
            }
        }
        break;

        case MID_SHADING_VALUE:
        {
            sal_Int32 nVal = 0;
            if (!(rVal >>= nVal))
                return false;

            ASSERT_CHANGE_REFCOUNTED_ITEM;
            nShadingValue = nVal;
        }
        break;
    }

    return true;
}


bool SvxBrushItem::GetPresentation
(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&
    ) const
{
    if ( GPOS_NONE  == eGraphicPos )
    {
        rText = ::GetColorString( aColor ) + cpDelim;
        TranslateId pId = RID_SVXITEMS_TRANSPARENT_FALSE;

        if ( aColor.IsTransparent() )
            pId = RID_SVXITEMS_TRANSPARENT_TRUE;
        rText += EditResId(pId);
    }
    else
    {
        rText = EditResId(RID_SVXITEMS_GRAPHIC);
    }

    return true;
}

bool SvxBrushItem::operator==( const SfxPoolItem& rAttr ) const
{
    assert(SfxPoolItem::operator==(rAttr));

    const SvxBrushItem& rCmp = static_cast<const SvxBrushItem&>(rAttr);
    bool bEqual =
        aColor == rCmp.aColor &&
        maComplexColor == rCmp.maComplexColor &&
        aFilterColor == rCmp.aFilterColor &&
        eGraphicPos == rCmp.eGraphicPos &&
        nGraphicTransparency == rCmp.nGraphicTransparency;

    if ( bEqual )
    {
        if ( GPOS_NONE != eGraphicPos )
        {
            bEqual = maStrLink == rCmp.maStrLink;

            if ( bEqual )
            {
                bEqual = maStrFilter == rCmp.maStrFilter;
            }

            if ( bEqual )
            {
                if (!rCmp.xGraphicObject)
                    bEqual = !xGraphicObject;
                else
                    bEqual = xGraphicObject &&
                             (*xGraphicObject == *rCmp.xGraphicObject);
            }
        }

        if (bEqual)
        {
            bEqual = nShadingValue == rCmp.nShadingValue;
        }
    }

    return bEqual;
}

size_t SvxBrushItem::hashCode() const
{
    std::size_t seed(0);
    o3tl::hash_combine(seed, static_cast<sal_Int32>(aColor));
    o3tl::hash_combine(seed, maComplexColor);
    o3tl::hash_combine(seed, static_cast<sal_Int32>(aFilterColor));
    o3tl::hash_combine(seed, eGraphicPos);
    o3tl::hash_combine(seed, nGraphicTransparency);
    if ( GPOS_NONE != eGraphicPos )
    {
        o3tl::hash_combine(seed, maStrLink);
        o3tl::hash_combine(seed, maStrFilter);
    }
    o3tl::hash_combine(seed, nShadingValue);
    return seed;
}

SvxBrushItem* SvxBrushItem::Clone( SfxItemPool* ) const
{
    return new SvxBrushItem( *this );
}

const GraphicObject* SvxBrushItem::GetGraphicObject(OUString const & referer) const
{
    if (bLoadAgain && !maStrLink.isEmpty() && !xGraphicObject)
    // when graphics already loaded, use as a cache
    {
        if (SvtSecurityOptions::isUntrustedReferer(referer)) {
            return nullptr;
        }

        INetURLObject aGraphicURL( maStrLink );
        if (aGraphicURL.IsExoticProtocol())
        {
            SAL_WARN("editeng", "Ignore exotic protocol: " << maStrLink);
            return nullptr;
        }

        // tdf#94088 prepare graphic and state
        Graphic aGraphic;
        bool bGraphicLoaded = false;

        // try to create stream directly from given URL
        std::unique_ptr<SvStream> xStream(utl::UcbStreamHelper::CreateStream(maStrLink, StreamMode::STD_READ));
        // tdf#94088 if we have a stream, try to load it directly as graphic
        if (xStream && !xStream->GetError())
        {
            if (ERRCODE_NONE == GraphicFilter::GetGraphicFilter().ImportGraphic(aGraphic, maStrLink, *xStream,
                GRFILTER_FORMAT_DONTKNOW, nullptr, GraphicFilterImportFlags::DontSetLogsizeForJpeg))
            {
                bGraphicLoaded = true;
            }
        }

        // tdf#94088 if no succeeded, try if the string (which is not empty) contains
        // a 'data:' scheme url and try to load that (embedded graphics)
        if(!bGraphicLoaded)
        {
            if( INetProtocol::Data == aGraphicURL.GetProtocol() )
            {
                std::unique_ptr<SvMemoryStream> const xMemStream(aGraphicURL.getData());
                if (xMemStream)
                {
                    if (ERRCODE_NONE == GraphicFilter::GetGraphicFilter().ImportGraphic(aGraphic, u"", *xMemStream))
                    {
                        bGraphicLoaded = true;

                        // tdf#94088 delete the no longer needed data scheme URL which
                        // is potentially pretty // large, containing a base64 encoded copy of the graphic
                        const_cast< SvxBrushItem* >(this)->maStrLink.clear();
                    }
                }
            }
        }

        // tdf#94088 when we got a graphic, set it
        if(bGraphicLoaded && GraphicType::NONE != aGraphic.GetType())
        {
            xGraphicObject.reset(new GraphicObject);
            xGraphicObject->SetGraphic(aGraphic);
            const_cast < SvxBrushItem*> (this)->ApplyGraphicTransparency_Impl();
        }
        else
        {
            bLoadAgain = false;
        }
    }

    return xGraphicObject.get();
}

void SvxBrushItem::setGraphicTransparency(sal_Int8 nNew)
{
    if (nNew != nGraphicTransparency)
    {
        ASSERT_CHANGE_REFCOUNTED_ITEM;
        nGraphicTransparency = nNew;
        ApplyGraphicTransparency_Impl();
    }
}

const Graphic* SvxBrushItem::GetGraphic(OUString const & referer) const
{
    const GraphicObject* pGrafObj = GetGraphicObject(referer);
    return( pGrafObj ? &( pGrafObj->GetGraphic() ) : nullptr );
}

void SvxBrushItem::SetGraphicPos( SvxGraphicPosition eNew )
{
    if (eGraphicPos == eNew)
        return;

    ASSERT_CHANGE_REFCOUNTED_ITEM;
    eGraphicPos = eNew;

    if ( GPOS_NONE == eGraphicPos )
    {
        xGraphicObject.reset();
        maStrLink.clear();
        maStrFilter.clear();
    }
    else
    {
        if (!xGraphicObject && maStrLink.isEmpty())
        {
            xGraphicObject.reset(new GraphicObject); // Creating a dummy
        }
    }
}

void SvxBrushItem::SetGraphic( const Graphic& rNew )
{
    if ( maStrLink.isEmpty() )
    {
        ASSERT_CHANGE_REFCOUNTED_ITEM;
        if (xGraphicObject)
            xGraphicObject->SetGraphic(rNew);
        else
            xGraphicObject.reset(new GraphicObject(rNew));

        ApplyGraphicTransparency_Impl();

        if ( GPOS_NONE == eGraphicPos )
            eGraphicPos = GPOS_MM; // None would be brush, then Default: middle
    }
    else
    {
        OSL_FAIL( "SetGraphic() on linked graphic! :-/" );
    }
}

void SvxBrushItem::SetGraphicObject( const GraphicObject& rNewObj )
{
    if ( maStrLink.isEmpty() )
    {
        ASSERT_CHANGE_REFCOUNTED_ITEM;
        if (xGraphicObject)
            *xGraphicObject = rNewObj;
        else
            xGraphicObject.reset(new GraphicObject(rNewObj));

        ApplyGraphicTransparency_Impl();

        if ( GPOS_NONE == eGraphicPos )
            eGraphicPos = GPOS_MM; // None would be brush, then Default: middle
    }
    else
    {
        OSL_FAIL( "SetGraphic() on linked graphic! :-/" );
    }
}

void SvxBrushItem::SetGraphicLink( const OUString& rNew )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    if ( rNew.isEmpty() )
        maStrLink.clear();
    else
    {
        maStrLink = rNew;
        xGraphicObject.reset();
    }
}

void SvxBrushItem::SetGraphicFilter( const OUString& rNew )
{
    ASSERT_CHANGE_REFCOUNTED_ITEM;
    maStrFilter = rNew;
}

void SvxBrushItem::ApplyGraphicTransparency_Impl()
{
    DBG_ASSERT(xGraphicObject, "no GraphicObject available" );
    if (xGraphicObject)
    {
        GraphicAttr aAttr(xGraphicObject->GetAttr());
        aAttr.SetAlpha(255 - lcl_PercentToTransparency(
                            nGraphicTransparency));
        xGraphicObject->SetAttr(aAttr);
    }
}

void SvxBrushItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxBrushItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("whichId"), BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("color"), BAD_CAST(aColor.AsRGBHexString().toUtf8().getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("filtercolor"), BAD_CAST(aFilterColor.AsRGBHexString().toUtf8().getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("shadingValue"), BAD_CAST(OString::number(nShadingValue).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("link"), BAD_CAST(maStrLink.toUtf8().getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("filter"), BAD_CAST(maStrFilter.toUtf8().getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("graphicPos"), BAD_CAST(OString::number(eGraphicPos).getStr()));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("loadAgain"), BAD_CAST(OString::boolean(bLoadAgain).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

ItemInstanceManager* SvxFrameDirectionItem::getItemInstanceManager() const
{
    static DefaultItemInstanceManager aInstanceManager(ItemType());
    return &aInstanceManager;
}

SvxFrameDirectionItem::SvxFrameDirectionItem( SvxFrameDirection nValue ,
                                            sal_uInt16 _nWhich )
    : SfxEnumItem<SvxFrameDirection>( _nWhich, nValue )
{
}


SvxFrameDirectionItem::~SvxFrameDirectionItem()
{
}

SvxFrameDirectionItem* SvxFrameDirectionItem::Clone( SfxItemPool * ) const
{
    return new SvxFrameDirectionItem( *this );
}

TranslateId getFrmDirResId(size_t nIndex)
{
    TranslateId const RID_SVXITEMS_FRMDIR[] =
    {
        RID_SVXITEMS_FRMDIR_HORI_LEFT_TOP,
        RID_SVXITEMS_FRMDIR_HORI_RIGHT_TOP,
        RID_SVXITEMS_FRMDIR_VERT_TOP_RIGHT,
        RID_SVXITEMS_FRMDIR_VERT_TOP_LEFT,
        RID_SVXITEMS_FRMDIR_ENVIRONMENT,
        RID_SVXITEMS_FRMDIR_VERT_BOT_LEFT,
        RID_SVXITEMS_FRMDIR_VERT_TOP_RIGHT90
    };
    return RID_SVXITEMS_FRMDIR[nIndex];
}

bool SvxFrameDirectionItem::GetPresentation(
    SfxItemPresentation /*ePres*/,
    MapUnit             /*eCoreUnit*/,
    MapUnit             /*ePresUnit*/,
    OUString&           rText, const IntlWrapper&) const
{
    rText = EditResId(getFrmDirResId(GetEnumValue()));
    return true;
}

bool SvxFrameDirectionItem::PutValue( const css::uno::Any& rVal,
                                             sal_uInt8 )
{
    sal_Int16 nVal = sal_Int16();
    bool bRet = ( rVal >>= nVal );
    if( bRet )
    {
        ASSERT_CHANGE_REFCOUNTED_ITEM;
        // translate WritingDirection2 constants into SvxFrameDirection
        switch( nVal )
        {
            case text::WritingMode2::LR_TB:
                SetValue( SvxFrameDirection::Horizontal_LR_TB );
                break;
            case text::WritingMode2::RL_TB:
                SetValue( SvxFrameDirection::Horizontal_RL_TB );
                break;
            case text::WritingMode2::TB_RL:
                SetValue( SvxFrameDirection::Vertical_RL_TB );
                break;
            case text::WritingMode2::TB_LR:
                SetValue( SvxFrameDirection::Vertical_LR_TB );
                break;
            case text::WritingMode2::BT_LR:
                SetValue( SvxFrameDirection::Vertical_LR_BT );
                break;
            case text::WritingMode2::TB_RL90:
                SetValue(SvxFrameDirection::Vertical_RL_TB90);
                break;
            case text::WritingMode2::PAGE:
                SetValue( SvxFrameDirection::Environment );
                break;
            case text::WritingMode2::STACKED:
                SetValue(SvxFrameDirection::Stacked);
                break;
            default:
                bRet = false;
                break;
        }
    }

    return bRet;
}


bool SvxFrameDirectionItem::QueryValue( css::uno::Any& rVal,
                                            sal_uInt8 ) const
{
    // translate SvxFrameDirection into WritingDirection2
    sal_Int16 nVal;
    bool bRet = true;
    switch( GetValue() )
    {
        case SvxFrameDirection::Horizontal_LR_TB:
            nVal = text::WritingMode2::LR_TB;
            break;
        case SvxFrameDirection::Horizontal_RL_TB:
            nVal = text::WritingMode2::RL_TB;
            break;
        case SvxFrameDirection::Vertical_RL_TB:
            nVal = text::WritingMode2::TB_RL;
            break;
        case SvxFrameDirection::Vertical_LR_TB:
            nVal = text::WritingMode2::TB_LR;
            break;
        case SvxFrameDirection::Vertical_LR_BT:
            nVal = text::WritingMode2::BT_LR;
            break;
        case SvxFrameDirection::Vertical_RL_TB90:
            nVal = text::WritingMode2::TB_RL90;
            break;
        case SvxFrameDirection::Environment:
            nVal = text::WritingMode2::PAGE;
            break;
        case SvxFrameDirection::Stacked:
            nVal = text::WritingMode2::STACKED;
            break;
        default:
            OSL_FAIL("Unknown SvxFrameDirection value!");
            bRet = false;
            break;
    }

    // return value + error state
    if( bRet )
    {
        rVal <<= nVal;
    }
    return bRet;
}

void SvxFrameDirectionItem::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    (void)xmlTextWriterStartElement(pWriter, BAD_CAST("SvxFrameDirectionItem"));
    (void)xmlTextWriterWriteAttribute(pWriter, BAD_CAST("m_nWhich"),
                                BAD_CAST(OString::number(Which()).getStr()));
    (void)xmlTextWriterWriteAttribute(
        pWriter, BAD_CAST("m_nValue"),
        BAD_CAST(OString::number(static_cast<sal_Int16>(GetValue())).getStr()));
    (void)xmlTextWriterEndElement(pWriter);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
