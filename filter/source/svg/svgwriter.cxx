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

#include "svgfilter.hxx"
#include "svgfontexport.hxx"
#include "svgwriter.hxx"

#include <comphelper/base64.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <sal/log.hxx>
#include <vcl/unohelp.hxx>
#include <vcl/cvtgrf.hxx>
#include <vcl/metric.hxx>
#include <vcl/outdev.hxx>
#include <vcl/settings.hxx>
#include <vcl/filter/SvmReader.hxx>
#include <vcl/filter/SvmWriter.hxx>
#include <tools/fract.hxx>
#include <tools/helpers.hxx>
#include <tools/stream.hxx>
#include <xmloff/namespacemap.hxx>
#include <xmloff/unointerfacetouniqueidentifiermapper.hxx>
#include <i18nlangtag/languagetag.hxx>
#include <o3tl/string_view.hxx>
#include <svx/svdomedia.hxx>
#include <basegfx/utils/bgradient.hxx>
#include <tools/vcompat.hxx>

#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XIndexReplace.hpp>
#include <com/sun/star/graphic/XGraphic.hpp>
#include <com/sun/star/i18n/CharacterIteratorMode.hpp>
#include <com/sun/star/i18n/XBreakIterator.hpp>
#include <com/sun/star/style/NumberingType.hpp>
#include <com/sun/star/text/XTextField.hpp>

#include <memory>

using namespace ::com::sun::star::style;

constexpr OUString aPrefixClipPathId = u"clip_path_"_ustr;

constexpr OUString aXMLElemG = u"g"_ustr;
constexpr OUString aXMLElemDefs = u"defs"_ustr;
constexpr OUString aXMLElemText = u"text"_ustr;
constexpr OUString aXMLElemTspan = u"tspan"_ustr;
constexpr OUString aXMLElemLinearGradient = u"linearGradient"_ustr;
constexpr OUString aXMLElemStop = u"stop"_ustr;

constexpr OUString aXMLAttrTransform = u"transform"_ustr;
constexpr OUString aXMLAttrStyle = u"style"_ustr;
constexpr OUString aXMLAttrId = u"id"_ustr;
constexpr OUString aXMLAttrX = u"x"_ustr;
constexpr OUString aXMLAttrY = u"y"_ustr;
constexpr OUString aXMLAttrX1 = u"x1"_ustr;
constexpr OUString aXMLAttrY1 = u"y1"_ustr;
constexpr OUString aXMLAttrX2 = u"x2"_ustr;
constexpr OUString aXMLAttrY2 = u"y2"_ustr;
constexpr OUString aXMLAttrCX = u"cx"_ustr;
constexpr OUString aXMLAttrCY = u"cy"_ustr;
constexpr OUString aXMLAttrRX = u"rx"_ustr;
constexpr OUString aXMLAttrRY = u"ry"_ustr;
constexpr OUString aXMLAttrWidth = u"width"_ustr;
constexpr OUString aXMLAttrHeight = u"height"_ustr;
constexpr OUString aXMLAttrStrokeWidth = u"stroke-width"_ustr;
constexpr OUString aXMLAttrFill = u"fill"_ustr;
constexpr OUString aXMLAttrFontFamily = u"font-family"_ustr;
constexpr OUString aXMLAttrFontSize = u"font-size"_ustr;
constexpr OUString aXMLAttrFontStyle = u"font-style"_ustr;
constexpr OUString aXMLAttrFontWeight = u"font-weight"_ustr;
constexpr OUString aXMLAttrTextDecoration = u"text-decoration"_ustr;
constexpr OUString aXMLAttrXLinkHRef = u"xlink:href"_ustr;
constexpr OUString aXMLAttrGradientUnits = u"gradientUnits"_ustr;
constexpr OUString aXMLAttrOffset = u"offset"_ustr;
constexpr OUString aXMLAttrStopColor = u"stop-color"_ustr;
constexpr OUString aXMLAttrStrokeLinejoin = u"stroke-linejoin"_ustr;
constexpr OUString aXMLAttrStrokeLinecap = u"stroke-linecap"_ustr;
constexpr OUString aXMLAttrTextDirection = u"direction"_ustr;


vcl::PushFlags SVGContextHandler::getPushFlags() const
{
    if (maStateStack.empty())
        return vcl::PushFlags::NONE;

    const PartialState& rPartialState = maStateStack.top();
    return rPartialState.meFlags;
}

SVGState& SVGContextHandler::getCurrentState()
{
    return maCurrentState;
}

void SVGContextHandler::pushState( vcl::PushFlags eFlags )
{
    PartialState aPartialState;
    aPartialState.meFlags = eFlags;

    if (eFlags & vcl::PushFlags::FONT)
    {
        aPartialState.setFont( maCurrentState.aFont );
    }

    if (eFlags & vcl::PushFlags::CLIPREGION)
    {
        aPartialState.mnRegionClipPathId = maCurrentState.nRegionClipPathId;
    }

    maStateStack.push( std::move(aPartialState) );
}

void SVGContextHandler::popState()
{
    if (maStateStack.empty())
        return;

    const PartialState& rPartialState = maStateStack.top();
    vcl::PushFlags eFlags = rPartialState.meFlags;

    if (eFlags & vcl::PushFlags::FONT)
    {
        maCurrentState.aFont = rPartialState.getFont( vcl::Font() );
    }

    if (eFlags & vcl::PushFlags::CLIPREGION)
    {
        maCurrentState.nRegionClipPathId = rPartialState.mnRegionClipPathId;
    }

    maStateStack.pop();
}

SVGAttributeWriter::SVGAttributeWriter( SVGExport& rExport, SVGFontExport& rFontExport, SVGState& rCurState )
    : mrExport( rExport )
    , mrFontExport( rFontExport )
    , mrCurrentState( rCurState )
{
}


SVGAttributeWriter::~SVGAttributeWriter()
{
}


double SVGAttributeWriter::ImplRound( double fValue )
{
      return floor( fValue * pow( 10.0, 3 ) + 0.5 ) / pow( 10.0, 3 );
}


void SVGAttributeWriter::ImplGetColorStr( const Color& rColor, OUString& rColorStr )
{
    if( rColor.GetAlpha() == 0 )
        rColorStr = "none";
    else
    {
        rColorStr = "rgb(" + OUString::number(rColor.GetRed()) + "," + OUString::number(rColor.GetGreen()) +
                    "," + OUString::number(rColor.GetBlue()) + ")";
    }
}


void SVGAttributeWriter::AddColorAttr( const OUString& pColorAttrName,
                                       const OUString& pColorOpacityAttrName,
                                       const Color& rColor )
{
    OUString aColor, aColorOpacity;

    ImplGetColorStr( rColor, aColor );

    if( rColor.GetAlpha() < 255 && rColor.GetAlpha() > 0 )
        aColorOpacity = OUString::number( ImplRound( rColor.GetAlpha() / 255.0 ) );

    mrExport.AddAttribute(pColorAttrName, aColor);

    if( !aColorOpacity.isEmpty() && mrExport.IsUseOpacity() )
        mrExport.AddAttribute(pColorOpacityAttrName, aColorOpacity);
}


void SVGAttributeWriter::AddPaintAttr( const Color& rLineColor, const Color& rFillColor,
                                       const tools::Rectangle* pObjBoundRect, const Gradient* pFillGradient )
{
    // Fill
    if( pObjBoundRect && pFillGradient )
    {
        OUString aGradientId;

        AddGradientDef( *pObjBoundRect, *pFillGradient, aGradientId );

        if( !aGradientId.isEmpty() )
        {
            OUString aGradientURL = "url(#" + aGradientId + ")";
            mrExport.AddAttribute(aXMLAttrFill, aGradientURL);
        }
    }
    else
        AddColorAttr( aXMLAttrFill, u"fill-opacity"_ustr, rFillColor );

    // Stroke
    AddColorAttr( u"stroke"_ustr, u"stroke-opacity"_ustr, rLineColor );
}


void SVGAttributeWriter::AddGradientDef( const tools::Rectangle& rObjRect, const Gradient& rGradient, OUString& rGradientId )
{
    if( rObjRect.GetWidth() && rObjRect.GetHeight() &&
        ( rGradient.GetStyle() == css::awt::GradientStyle_LINEAR || rGradient.GetStyle() == css::awt::GradientStyle_AXIAL ||
          rGradient.GetStyle() == css::awt::GradientStyle_RADIAL || rGradient.GetStyle() == css::awt::GradientStyle_ELLIPTICAL ) )
    {
        SvXMLElementExport aDesc(mrExport, aXMLElemDefs, true, true);
        Color aStartColor( rGradient.GetStartColor() ), aEndColor( rGradient.GetEndColor() );
        Degree10 nAngle = rGradient.GetAngle() % 3600_deg10;
        Point aObjRectCenter( rObjRect.Center() );
        tools::Polygon aPoly( rObjRect );
        static sal_Int32 nCurGradientId = 1;

        aPoly.Rotate( aObjRectCenter, nAngle );
        tools::Rectangle aRect( aPoly.GetBoundRect() );

        // adjust start/end colors with intensities
        aStartColor.SetRed( static_cast<sal_uInt8>( ( aStartColor.GetRed() * rGradient.GetStartIntensity() ) / 100 ) );
        aStartColor.SetGreen( static_cast<sal_uInt8>( ( aStartColor.GetGreen() * rGradient.GetStartIntensity() ) / 100 ) );
        aStartColor.SetBlue( static_cast<sal_uInt8>( ( aStartColor.GetBlue() * rGradient.GetStartIntensity() ) / 100 ) );

        aEndColor.SetRed( static_cast<sal_uInt8>( ( aEndColor.GetRed() * rGradient.GetEndIntensity() ) / 100 ) );
        aEndColor.SetGreen( static_cast<sal_uInt8>( ( aEndColor.GetGreen() * rGradient.GetEndIntensity() ) / 100 ) );
        aEndColor.SetBlue( static_cast<sal_uInt8>( ( aEndColor.GetBlue() * rGradient.GetEndIntensity() ) / 100 ) );

        rGradientId = "Gradient_" + OUString::number( nCurGradientId++ );
        mrExport.AddAttribute(aXMLAttrId, rGradientId);

        {
            std::unique_ptr< SvXMLElementExport >   apGradient;
            OUString                         aColorStr;

            if( rGradient.GetStyle() == css::awt::GradientStyle_LINEAR || rGradient.GetStyle() == css::awt::GradientStyle_AXIAL )
            {
                tools::Polygon aLinePoly( 2 );

                aLinePoly[ 0 ] = Point( aObjRectCenter.X(), aRect.Top() );
                aLinePoly[ 1 ] = Point( aObjRectCenter.X(), aRect.Bottom() );

                aLinePoly.Rotate( aObjRectCenter, nAngle );

                mrExport.AddAttribute(aXMLAttrGradientUnits, u"userSpaceOnUse"_ustr);
                mrExport.AddAttribute(aXMLAttrX1, OUString::number(aLinePoly[0].X()));
                mrExport.AddAttribute(aXMLAttrY1, OUString::number(aLinePoly[0].Y()));
                mrExport.AddAttribute(aXMLAttrX2, OUString::number(aLinePoly[1].X()));
                mrExport.AddAttribute(aXMLAttrY2, OUString::number(aLinePoly[1].Y()));

                apGradient.reset( new SvXMLElementExport( mrExport, aXMLElemLinearGradient, true, true ) );

                // write stop values
                double fBorder = static_cast< double >( rGradient.GetBorder() ) *
                                ( ( rGradient.GetStyle() == css::awt::GradientStyle_AXIAL ) ? 0.005 : 0.01 );

                ImplGetColorStr( ( rGradient.GetStyle() == css::awt::GradientStyle_AXIAL ) ? aEndColor : aStartColor, aColorStr );
                mrExport.AddAttribute(aXMLAttrOffset, OUString::number(fBorder));
                mrExport.AddAttribute(aXMLAttrStopColor, aColorStr);

                {
                    SvXMLElementExport aDesc2(mrExport, aXMLElemStop, true, true);
                }

                if( rGradient.GetStyle() == css::awt::GradientStyle_AXIAL )
                {
                    ImplGetColorStr( aStartColor, aColorStr );
                    mrExport.AddAttribute(aXMLAttrOffset, u"0.5"_ustr);
                    mrExport.AddAttribute(aXMLAttrStopColor, aColorStr);

                    {
                        SvXMLElementExport aDesc3(mrExport, aXMLElemStop, true, true);
                    }
                }

                if( rGradient.GetStyle() != css::awt::GradientStyle_AXIAL )
                    fBorder = 0.0;

                ImplGetColorStr( aEndColor, aColorStr );
                mrExport.AddAttribute(aXMLAttrOffset, OUString::number(ImplRound(1.0 - fBorder)));
                mrExport.AddAttribute(aXMLAttrStopColor, aColorStr);

                {
                    SvXMLElementExport aDesc4(mrExport, aXMLElemStop, true, true);
                }
            }
            else
            {
                const double    fCenterX = rObjRect.Left() + rObjRect.GetWidth() * rGradient.GetOfsX() * 0.01;
                const double    fCenterY = rObjRect.Top() + rObjRect.GetHeight() * rGradient.GetOfsY() * 0.01;
                const double    fRadius = std::hypot(rObjRect.GetWidth(), rObjRect.GetHeight()) * 0.5;

                mrExport.AddAttribute(aXMLAttrGradientUnits, u"userSpaceOnUse"_ustr);
                mrExport.AddAttribute(aXMLAttrCX, OUString::number(ImplRound(fCenterX)));
                mrExport.AddAttribute(aXMLAttrCY, OUString::number(ImplRound(fCenterY)));
                mrExport.AddAttribute(u"r"_ustr, OUString::number(ImplRound(fRadius)));

                apGradient.reset( new SvXMLElementExport( mrExport, u"radialGradient"_ustr, true, true ) );

                // write stop values
                ImplGetColorStr( aEndColor, aColorStr );
                mrExport.AddAttribute(aXMLAttrOffset, u"0"_ustr);
                mrExport.AddAttribute(aXMLAttrStopColor, aColorStr);

                {
                    SvXMLElementExport aDesc5(mrExport, aXMLElemStop, true, true);
                }

                ImplGetColorStr( aStartColor, aColorStr );
                mrExport.AddAttribute( aXMLAttrOffset,
                                       OUString::number( ImplRound( 1.0 - rGradient.GetBorder() * 0.01 ) ) );
                mrExport.AddAttribute(aXMLAttrStopColor, aColorStr);

                {
                    SvXMLElementExport aDesc6(mrExport, aXMLElemStop, true, true);
                }
            }
        }
    }
    else
        rGradientId.clear();
}


void SVGAttributeWriter::SetFontAttr( const vcl::Font& rFont )
{
    vcl::Font& rCurFont = mrCurrentState.aFont;

    if( rFont == rCurFont )
        return;

    OUString  aFontStyle;
    sal_Int32        nFontWeight;

    rCurFont = rFont;

    // Font Family
    setFontFamily();

    // Font Size
    mrExport.AddAttribute(aXMLAttrFontSize, OUString::number(rFont.GetFontHeight()) + "px");

    // Font Style
    if( rFont.GetItalic() != ITALIC_NONE )
    {
        if( rFont.GetItalic() == ITALIC_OBLIQUE )
            aFontStyle = "oblique";
        else
            aFontStyle = "italic";
    }
    else
        aFontStyle = "normal";

    mrExport.AddAttribute(aXMLAttrFontStyle, aFontStyle);

    // Font Weight
    switch( rFont.GetWeight() )
    {
        case WEIGHT_THIN:           nFontWeight = 100; break;
        case WEIGHT_ULTRALIGHT:     nFontWeight = 200; break;
        case WEIGHT_LIGHT:          nFontWeight = 300; break;
        case WEIGHT_SEMILIGHT:      nFontWeight = 400; break;
        case WEIGHT_NORMAL:         nFontWeight = 400; break;
        case WEIGHT_MEDIUM:         nFontWeight = 500; break;
        case WEIGHT_SEMIBOLD:       nFontWeight = 600; break;
        case WEIGHT_BOLD:           nFontWeight = 700; break;
        case WEIGHT_ULTRABOLD:      nFontWeight = 800; break;
        case WEIGHT_BLACK:          nFontWeight = 900; break;
        default:                    nFontWeight = 400; break;
    }

    mrExport.AddAttribute(aXMLAttrFontWeight, OUString::number(nFontWeight));

    if( mrExport.IsUseNativeTextDecoration() )
    {
        OUString aTextDecoration;
        if( rFont.GetUnderline() != LINESTYLE_NONE || rFont.GetStrikeout() != STRIKEOUT_NONE )
        {
            if( rFont.GetUnderline() != LINESTYLE_NONE )
                aTextDecoration = "underline ";

            if( rFont.GetStrikeout() != STRIKEOUT_NONE )
                aTextDecoration += "line-through ";
        }
        else
            aTextDecoration = "none";

        mrExport.AddAttribute(aXMLAttrTextDecoration, aTextDecoration);
    }

    startFontSettings();
}


void SVGAttributeWriter::startFontSettings()
{
    endFontSettings();
    if( mrExport.IsUsePositionedCharacters() )
    {
        mpElemFont.reset(new SvXMLElementExport(mrExport, aXMLElemG, true, true));
    }
    else
    {
        mpElemFont.reset(new SvXMLElementExport(mrExport, aXMLElemTspan, true, true));
    }
}


void SVGAttributeWriter::endFontSettings()
{
    mpElemFont.reset();
}


void SVGAttributeWriter::setFontFamily()
{
    vcl::Font& rCurFont = mrCurrentState.aFont;

    if( mrExport.IsUsePositionedCharacters() )
    {
        mrExport.AddAttribute( aXMLAttrFontFamily, mrFontExport.GetMappedFontName( rCurFont.GetFamilyName() ) );
    }
    else
    {
        const OUString& rsFontName = rCurFont.GetFamilyName();
        OUString sFontFamily( rsFontName.getToken( 0, ';' ) );
        FontPitch ePitch = rCurFont.GetPitchMaybeAskConfig();
        if( ePitch == PITCH_FIXED )
        {
            sFontFamily += ", monospace";
        }
        else
        {
            FontFamily eFamily = rCurFont.GetFamilyTypeMaybeAskConfig();
            if( eFamily == FAMILY_ROMAN )
                sFontFamily += ", serif";
            else if( eFamily == FAMILY_SWISS )
                sFontFamily += ", sans-serif";
        }
        mrExport.AddAttribute(aXMLAttrFontFamily, sFontFamily);
    }
}

SVGTextWriter::SVGTextWriter(SVGExport& rExport, SVGAttributeWriter& rAttributeWriter,
                             SVGActionWriter& rActionWriter)
: mrExport( rExport ),
  mrAttributeWriter( rAttributeWriter ),
  mrActionWriter(rActionWriter),
  mpVDev( nullptr ),
  mbIsTextShapeStarted( false ),
  mpTextEmbeddedBitmapMtf( nullptr ),
  mpTargetMapMode( nullptr ),
  mnLeftTextPortionLength( 0 ),
  maTextPos(0,0),
  mnTextWidth(0),
  mbPositioningNeeded( false ),
  mbIsNewListItem( false ),
  meNumberingType(0),
  mcBulletChar(0),
  mbIsListLevelStyleImage( false ),
  mbLineBreak( false ),
  mbIsURLField( false ),
  mbIsPlaceholderShape( false )
{
}


SVGTextWriter::~SVGTextWriter()
{
    endTextParagraph();
}


void SVGTextWriter::implRegisterInterface( const Reference< XInterface >& rxIf )
{
    if( rxIf.is() )
        mrExport.getInterfaceToIdentifierMapper().registerReference( rxIf );
}


const OUString & SVGTextWriter::implGetValidIDFromInterface( const Reference< XInterface >& rxIf )
{
   return mrExport.getInterfaceToIdentifierMapper().getIdentifier( rxIf );
}


void SVGTextWriter::implMap( const Size& rSz, Size& rDstSz ) const
{
    if( mpVDev && mpTargetMapMode )
        rDstSz = OutputDevice::LogicToLogic( rSz, mpVDev->GetMapMode(), *mpTargetMapMode );
    else
        OSL_FAIL( "SVGTextWriter::implMap: invalid virtual device or map mode." );
}


void SVGTextWriter::implMap( const Point& rPt, Point& rDstPt ) const
{
    if( mpVDev && mpTargetMapMode )
        rDstPt = OutputDevice::LogicToLogic( rPt, mpVDev->GetMapMode(), *mpTargetMapMode );
    else
        OSL_FAIL( "SVGTextWriter::implMap: invalid virtual device or map mode." );
}


void SVGTextWriter::implSetCurrentFont()
{
    if( mpVDev )
    {
        maCurrentFont = mpVDev->GetFont();
        Size aSz;

        implMap( Size( 0, maCurrentFont.GetFontHeight() ), aSz );

        maCurrentFont.SetFontHeight( aSz.Height() );
    }
    else
    {
        OSL_FAIL( "SVGTextWriter::implSetCorrectFontHeight: invalid virtual device." );
    }
}


template< typename SubType >
bool SVGTextWriter::implGetTextPosition( const MetaAction* pAction, Point& raPos, bool& rbEmpty )
{
    const SubType* pA = static_cast<const SubType*>(pAction);
    sal_uInt16 nLength = pA->GetLen();
    rbEmpty = ( nLength == 0 );
    if( !rbEmpty )
    {
        raPos = pA->GetPoint();
        return true;
    }
    return false;
}


template<>
bool SVGTextWriter::implGetTextPosition<MetaTextRectAction>( const MetaAction* pAction, Point& raPos, bool& rbEmpty )
{
    const MetaTextRectAction* pA = static_cast<const MetaTextRectAction*>(pAction);
    sal_uInt16 nLength = pA->GetText().getLength();
    rbEmpty = ( nLength == 0 );
    if( !rbEmpty )
    {
        raPos = pA->GetRect().TopLeft();
        return true;
    }
    return false;
}


template< typename SubType >
bool SVGTextWriter::implGetTextPositionFromBitmap( const MetaAction* pAction, Point& raPos, bool& rbEmpty )
{
    const SubType* pA = static_cast<const SubType*>(pAction);
    raPos = pA->GetPoint();
    rbEmpty = false;
    return true;
}


/** setTextPosition
 *  Set the start position of the next line of text. In case no text is found
 *  the current action index is updated to the index value we reached while
 *  searching for text.
 *
 *  @returns {sal_Int32}
 *    -2 if no text found and end of line is reached
 *    -1 if no text found and end of paragraph is reached
 *     0 if no text found and end of text shape is reached
 *     1 if text found!
 */
sal_Int32 SVGTextWriter::setTextPosition(const GDIMetaFile& rMtf, size_t& nCurAction,
                                         sal_uInt32 nWriteFlags)
{
    Point aPos;
    size_t nCount = rMtf.GetActionSize();
    bool bEOL = false;
    bool bEOP = false;
    bool bETS = false;
    bool bConfigured = false;
    bool bEmpty = true;

    // similar to OutputDevice::Push, but we may conditionally not restore these
    MapMode aOrigMapMode = mpVDev->GetMapMode();
    bool bOrigMapMapModeEnabled = mpVDev->IsMapModeEnabled();
    int nPopsNeeded = 0;

    size_t nActionIndex = nCurAction + 1;
    for( ; nActionIndex < nCount; ++nActionIndex )
    {
        const MetaAction*    pAction = rMtf.GetAction( nActionIndex );
        const MetaActionType nType = pAction->GetType();

        switch( nType )
        {
            case MetaActionType::TEXT:
            {
                bConfigured = implGetTextPosition<MetaTextAction>( pAction, aPos, bEmpty );
            }
            break;

            case MetaActionType::TEXTRECT:
            {
                bConfigured = implGetTextPosition<MetaTextRectAction>( pAction, aPos, bEmpty );
            }
            break;

            case MetaActionType::TEXTARRAY:
            {
                bConfigured = implGetTextPosition<MetaTextArrayAction>( pAction, aPos, bEmpty );
            }
            break;

            case MetaActionType::FLOATTRANSPARENT:
            {
                const MetaFloatTransparentAction* pA
                    = static_cast<const MetaFloatTransparentAction*>(pAction);
                GDIMetaFile aTmpMtf(pA->GetGDIMetaFile());
                size_t nTmpAction = 0;
                if (setTextPosition(aTmpMtf, nTmpAction, nWriteFlags) == 1)
                {
                    // Text is found in the inner metafile.
                    bConfigured = true;

                    // nTextFound == 1 is only possible if the inner setTextPosition() had bEmpty ==
                    // false, adjust our bEmpty accordingly.
                    bEmpty = false;

                    mrActionWriter.StartMask(pA->GetPoint(), pA->GetSize(), pA->GetGradient(),
                                             nWriteFlags, pA->getSVGTransparencyColorStops(), &maTextOpacity);
                }
            }
            break;

            case MetaActionType::STRETCHTEXT:
            {
                bConfigured = implGetTextPosition<MetaStretchTextAction>( pAction, aPos, bEmpty );
            }
            break;

            case MetaActionType::BMPSCALE:
            {
                bConfigured = implGetTextPositionFromBitmap<MetaBmpScaleAction>( pAction, aPos, bEmpty );
            }
            break;

            case MetaActionType::BMPEXSCALE:
            {
                bConfigured = implGetTextPositionFromBitmap<MetaBmpExScaleAction>( pAction, aPos, bEmpty );
            }
            break;

            // If we reach the end of the current line, paragraph or text shape
            // without finding any text we stop searching
            case MetaActionType::COMMENT:
            {
                const MetaCommentAction* pA = static_cast<const MetaCommentAction*>(pAction);
                const OString& rsComment = pA->GetComment();
                if( rsComment.equalsIgnoreAsciiCase( "XTEXT_EOL" ) )
                {
                    bEOL = true;
                }
                else if( rsComment.equalsIgnoreAsciiCase( "XTEXT_EOP" ) )
                {
                    bEOP = true;

                    OUString sContent;
                    while( nextTextPortion() )
                    {
                        sContent = mrCurrentTextPortion->getString();
                        if( sContent.isEmpty() )
                        {
                            continue;
                        }
                        else
                        {
                            if( sContent == "\n" )
                                mbLineBreak = true;
                        }
                    }
                    if( nextParagraph() )
                    {
                        while( nextTextPortion() )
                        {
                            sContent = mrCurrentTextPortion->getString();
                            if( sContent.isEmpty() )
                            {
                                continue;
                            }
                            else
                            {
                                if( sContent == "\n" )
                                    mbLineBreak = true;
                            }
                        }
                    }
                }
                else if( rsComment.equalsIgnoreAsciiCase( "XTEXT_PAINTSHAPE_END" ) )
                {
                    bETS = true;
                }
            }
            break;

            case MetaActionType::PUSH:
                const_cast<MetaAction*>(pAction)->Execute(mpVDev);
                ++nPopsNeeded;
                break;
            case MetaActionType::POP:
                const_cast<MetaAction*>(pAction)->Execute(mpVDev);
                --nPopsNeeded;
                break;
            case MetaActionType::MAPMODE:
            {
                // keep MapMode up to date
                const_cast<MetaAction*>(pAction)->Execute(mpVDev);
                break;
            }
            break;

            default: break;
        }
        if( bConfigured || bEOL || bEOP || bETS ) break;
    }
    implMap( aPos, maTextPos );

    if( bEmpty )
    {
        // If we fast-forward to this nActionIndex, then leave
        // the OutputDevice state as it is.
        nCurAction = nActionIndex;
        return ( bEOL ? -2 : ( bEOP ? -1 : 0 ) );
    }

    // If we are leaving nCurAction untouched, then restore the OutputDevice
    // to its original state
    while (nPopsNeeded > 0)
    {
        mpVDev->Pop();
        --nPopsNeeded;
    }

    mpVDev->SetMapMode(aOrigMapMode);
    mpVDev->EnableMapMode(bOrigMapMapModeEnabled);
    return 1;
}


void SVGTextWriter::setTextProperties( const GDIMetaFile& rMtf, size_t nCurAction )
{
    size_t nCount = rMtf.GetActionSize();
    bool bEOP = false;
    bool bConfigured = false;
    for( size_t nActionIndex = nCurAction + 1; nActionIndex < nCount; ++nActionIndex )
    {
        const MetaAction*    pAction = rMtf.GetAction( nActionIndex );
        const MetaActionType nType = pAction->GetType();
        switch( nType )
        {
            case MetaActionType::TEXTLINECOLOR:
            case MetaActionType::TEXTFILLCOLOR:
            case MetaActionType::TEXTCOLOR:
            case MetaActionType::TEXTALIGN:
            case MetaActionType::FONT:
            case MetaActionType::LAYOUTMODE:
            {
                const_cast<MetaAction*>(pAction)->Execute( mpVDev );
            }
            break;

            case MetaActionType::TEXT:
            {
                const MetaTextAction* pA = static_cast<const MetaTextAction*>(pAction);
                if( pA->GetLen() > 2 )
                    bConfigured = true;
            }
            break;
            case MetaActionType::TEXTRECT:
            {
                const MetaTextRectAction* pA = static_cast<const MetaTextRectAction*>(pAction);
                if( pA->GetText().getLength() > 2 )
                    bConfigured = true;
            }
            break;
            case MetaActionType::TEXTARRAY:
            {
                const MetaTextArrayAction* pA = static_cast<const MetaTextArrayAction*>(pAction);
                if( pA->GetLen() > 2 )
                    bConfigured = true;
            }
            break;
            case MetaActionType::STRETCHTEXT:
            {
                const MetaStretchTextAction* pA = static_cast<const MetaStretchTextAction*>(pAction);
                if( pA->GetLen() > 2 )
                    bConfigured = true;
            }
            break;
            // If we reach the end of the paragraph without finding any text
            // we stop searching
            case MetaActionType::COMMENT:
            {
                const MetaCommentAction* pA = static_cast<const MetaCommentAction*>(pAction);
                const OString& rsComment = pA->GetComment();
                if( rsComment.equalsIgnoreAsciiCase( "XTEXT_EOP" ) )
                {
                    bEOP = true;
                }
            }
            break;
            default: break;
        }
        if( bConfigured || bEOP ) break;
    }
}


void SVGTextWriter::addFontAttributes( bool bIsTextContainer )
{
    implSetCurrentFont();

    if( maCurrentFont ==  maParentFont )
        return;

    const OUString& rsCurFontName               = maCurrentFont.GetFamilyName();
    tools::Long nCurFontSize                       = maCurrentFont.GetFontHeight();
    FontItalic eCurFontItalic                   = maCurrentFont.GetItalicMaybeAskConfig();
    FontWeight eCurFontWeight                   = maCurrentFont.GetWeightMaybeAskConfig();

    const OUString& rsParFontName               = maParentFont.GetFamilyName();
    tools::Long nParFontSize                       = maParentFont.GetFontHeight();
    FontItalic eParFontItalic                   = maParentFont.GetItalicMaybeAskConfig();
    FontWeight eParFontWeight                   = maParentFont.GetWeightMaybeAskConfig();


    // Font Family
    if( rsCurFontName != rsParFontName )
    {
        implSetFontFamily();
    }

    // Font Size
    if( nCurFontSize != nParFontSize )
    {
        mrExport.AddAttribute(aXMLAttrFontSize, OUString::number(nCurFontSize) + "px");
    }

    // Font Style
    if( eCurFontItalic != eParFontItalic )
    {
        OUString sFontStyle;
        if( eCurFontItalic != ITALIC_NONE )
        {
            if( eCurFontItalic == ITALIC_OBLIQUE )
                sFontStyle = "oblique";
            else
                sFontStyle = "italic";
        }
        else
        {
            sFontStyle = "normal";
        }
        mrExport.AddAttribute(aXMLAttrFontStyle, sFontStyle);
    }

    // Font Weight
    if( eCurFontWeight != eParFontWeight )
    {
        sal_Int32 nFontWeight;
        switch( eCurFontWeight )
        {
            case WEIGHT_THIN:           nFontWeight = 100; break;
            case WEIGHT_ULTRALIGHT:     nFontWeight = 200; break;
            case WEIGHT_LIGHT:          nFontWeight = 300; break;
            case WEIGHT_SEMILIGHT:      nFontWeight = 400; break;
            case WEIGHT_NORMAL:         nFontWeight = 400; break;
            case WEIGHT_MEDIUM:         nFontWeight = 500; break;
            case WEIGHT_SEMIBOLD:       nFontWeight = 600; break;
            case WEIGHT_BOLD:           nFontWeight = 700; break;
            case WEIGHT_ULTRABOLD:      nFontWeight = 800; break;
            case WEIGHT_BLACK:          nFontWeight = 900; break;
            default:                    nFontWeight = 400; break;
        }
        mrExport.AddAttribute(aXMLAttrFontWeight, OUString::number(nFontWeight));
    }


    if( mrExport.IsUseNativeTextDecoration() )
    {
        FontLineStyle eCurFontLineStyle         = maCurrentFont.GetUnderline();
        FontStrikeout eCurFontStrikeout         = maCurrentFont.GetStrikeout();

        FontLineStyle eParFontLineStyle         = maParentFont.GetUnderline();
        FontStrikeout eParFontStrikeout         = maParentFont.GetStrikeout();

        OUString sTextDecoration;
        bool bIsDecorationChanged = false;
        if( eCurFontLineStyle != eParFontLineStyle )
        {
            if( eCurFontLineStyle != LINESTYLE_NONE )
                sTextDecoration = "underline";
            bIsDecorationChanged = true;
        }
        if( eCurFontStrikeout != eParFontStrikeout )
        {
            if( eCurFontStrikeout != STRIKEOUT_NONE )
            {
                if( !sTextDecoration.isEmpty() )
                    sTextDecoration += " ";
                sTextDecoration += "line-through";
            }
            bIsDecorationChanged = true;
        }

        if( !sTextDecoration.isEmpty() )
        {
            mrExport.AddAttribute(aXMLAttrTextDecoration, sTextDecoration);
        }
        else if( bIsDecorationChanged )
        {
            sTextDecoration = "none";
            mrExport.AddAttribute(aXMLAttrTextDecoration, sTextDecoration);
        }
    }

    if( bIsTextContainer )
        maParentFont = maCurrentFont;
}


void SVGTextWriter::implSetFontFamily()
{
    const OUString& rsFontName = maCurrentFont.GetFamilyName();
    OUString sFontFamily( rsFontName.getToken( 0, ';' ) );
    FontPitch ePitch = maCurrentFont.GetPitchMaybeAskConfig();
    if( ePitch == PITCH_FIXED )
    {
        sFontFamily += ", monospace";
    }
    else
    {
        FontFamily eFamily = maCurrentFont.GetFamilyTypeMaybeAskConfig();
        if( eFamily == FAMILY_ROMAN )
            sFontFamily += ", serif";
        else if( eFamily == FAMILY_SWISS )
            sFontFamily += ", sans-serif";
    }
    mrExport.AddAttribute(aXMLAttrFontFamily, sFontFamily);
}


void SVGTextWriter::createParagraphEnumeration()
{
    if( mrTextShape.is() )
    {
        msShapeId = implGetValidIDFromInterface( Reference<XInterface>(mrTextShape, UNO_QUERY) );

        Reference< XEnumerationAccess > xEnumerationAccess( mrTextShape, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        if( xEnumeration.is() )
        {
            mrParagraphEnumeration.set( xEnumeration );
        }
        else
        {
            OSL_FAIL( "SVGTextWriter::createParagraphEnumeration: no valid xEnumeration interface found." );
        }
    }
    else
    {
        OSL_FAIL( "SVGTextWriter::createParagraphEnumeration: no valid XText interface found." );
    }
}


bool SVGTextWriter::nextParagraph()
{
    mrTextPortionEnumeration.clear();
    mrCurrentTextParagraph.clear();
    mbIsNewListItem = false;
    mbIsListLevelStyleImage = false;

    if( !mrParagraphEnumeration || !mrParagraphEnumeration->hasMoreElements() )
        return false;

    Reference < XTextContent >  xTextContent( mrParagraphEnumeration->nextElement(), UNO_QUERY_THROW );
    if( xTextContent.is() )
    {
        Reference< XServiceInfo > xServiceInfo( xTextContent, UNO_QUERY_THROW );
#if OSL_DEBUG_LEVEL > 0
        OUString sInfo;
#endif
        if( xServiceInfo->supportsService( u"com.sun.star.text.Paragraph"_ustr ) )
        {
            mrCurrentTextParagraph.set( xTextContent );
            Reference< XPropertySet > xPropSet( xTextContent, UNO_QUERY_THROW );
            Reference< XPropertySetInfo > xPropSetInfo = xPropSet->getPropertySetInfo();
            if( xPropSetInfo->hasPropertyByName( u"NumberingLevel"_ustr ) )
            {
                sal_Int16 nListLevel = 0;
                if( xPropSet->getPropertyValue( u"NumberingLevel"_ustr ) >>= nListLevel )
                {
                    mbIsNewListItem = true;
#if OSL_DEBUG_LEVEL > 0
                    sInfo = "NumberingLevel: " + OUString::number( nListLevel );
                    mrExport.AddAttribute(u"style"_ustr, sInfo);
#endif
                    Reference< XIndexReplace > xNumRules;
                    if( xPropSetInfo->hasPropertyByName( u"NumberingRules"_ustr ) )
                    {
                        xPropSet->getPropertyValue( u"NumberingRules"_ustr ) >>= xNumRules;
                    }
                    if( xNumRules.is() && ( nListLevel < xNumRules->getCount() ) )
                    {
                        bool bIsNumbered = true;
                        OUString sNumberingIsNumber(u"NumberingIsNumber"_ustr);
                        if( xPropSetInfo->hasPropertyByName( sNumberingIsNumber ) )
                        {
                            if( !(xPropSet->getPropertyValue( sNumberingIsNumber ) >>= bIsNumbered ) )
                            {
                                OSL_FAIL( "numbered paragraph without number info" );
                                bIsNumbered = false;
                            }
#if OSL_DEBUG_LEVEL > 0
                            if( bIsNumbered )
                            {
                                sInfo = "true";
                                mrExport.AddAttribute(u"is-numbered"_ustr, sInfo);
                            }
#endif
                        }
                        mbIsNewListItem = bIsNumbered;

                        if( bIsNumbered )
                        {
                            Sequence<PropertyValue> aProps;
                            if( xNumRules->getByIndex( nListLevel ) >>= aProps )
                            {
                                sal_Int16 eType = NumberingType::CHAR_SPECIAL;
                                sal_Unicode cBullet = 0xf095;
                                const sal_Int32 nCount = aProps.getLength();
                                const PropertyValue* pPropArray = aProps.getConstArray();
                                for( sal_Int32 i = 0; i < nCount; ++i )
                                {
                                    const PropertyValue& rProp = pPropArray[i];
                                    if( rProp.Name == "NumberingType" )
                                    {
                                        rProp.Value >>= eType;
                                    }
                                    else if( rProp.Name == "BulletChar" )
                                    {
                                        OUString sValue;
                                        rProp.Value >>= sValue;
                                        if( !sValue.isEmpty() )
                                        {
                                            cBullet = sValue[0];
                                        }
                                    }
                                }
                                meNumberingType = eType;
                                mbIsListLevelStyleImage = ( NumberingType::BITMAP == meNumberingType );
                                if( NumberingType::CHAR_SPECIAL == meNumberingType )
                                {
                                    if( cBullet )
                                    {
                                        if( cBullet < ' ' )
                                        {
                                            cBullet = 0xF000 + 149;
                                        }
                                        mcBulletChar = cBullet;
#if OSL_DEBUG_LEVEL > 0
                                        sInfo = OUString::number( static_cast<sal_Int32>(cBullet) );
                                        mrExport.AddAttribute(u"bullet-char"_ustr, sInfo);
#endif
                                    }

                                }
                            }
                        }
                    }

                }
            }

            Reference< XEnumerationAccess > xEnumerationAccess( xTextContent, UNO_QUERY_THROW );
            Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
            if( xEnumeration.is() && xEnumeration->hasMoreElements() )
            {
                mrTextPortionEnumeration.set( xEnumeration );
            }
#if OSL_DEBUG_LEVEL > 0
            sInfo = "Paragraph";
#endif
        }
        else if( xServiceInfo->supportsService( u"com.sun.star.text.Table"_ustr ) )
        {
            OSL_FAIL( "SVGTextWriter::nextParagraph: text tables are not handled." );
#if OSL_DEBUG_LEVEL > 0
            sInfo = "Table";
#endif
        }
        else
        {
            OSL_FAIL( "SVGTextWriter::nextParagraph: Unknown text content." );
            return false;
        }
#if OSL_DEBUG_LEVEL > 0
        mrExport.AddAttribute(u"class"_ustr, sInfo);
        SvXMLElementExport aParaElem(mrExport, u"desc"_ustr, mbIWS, mbIWS);
#endif
    }
    else
    {
        OSL_FAIL( "SVGTextWriter::nextParagraph: no XServiceInfo interface available for text content." );
        return false;
    }

    const OUString& rParagraphId = implGetValidIDFromInterface( Reference<XInterface>(xTextContent, UNO_QUERY) );
    if( !rParagraphId.isEmpty() )
    {
            // if there is id for empty paragraph we need to create a empty text paragraph
            Reference < XTextRange > xRange( xTextContent, UNO_QUERY_THROW );
            if ( xRange.is() && xRange->getString().isEmpty() )
            {
                endTextParagraph();
                mrExport.AddAttribute(u"class"_ustr, u"TextParagraph"_ustr);
                mrExport.AddAttribute(u"id"_ustr, rParagraphId);
                mpTextParagraphElem.reset(new SvXMLElementExport( mrExport, aXMLElemTspan, mbIWS, mbIWS ));
            }
            else
            {
                mrExport.AddAttribute(u"id"_ustr, rParagraphId);
            }
    }
    return true;
}


bool SVGTextWriter::nextTextPortion()
{
    mrCurrentTextPortion.clear();
    mbIsURLField = false;
    if( !mrTextPortionEnumeration || !mrTextPortionEnumeration->hasMoreElements() )
        return false;

    mbIsPlaceholderShape = false;
    Reference< XPropertySet > xPortionPropSet( mrTextPortionEnumeration->nextElement(), UNO_QUERY );
    Reference< XPropertySetInfo > xPortionPropInfo( xPortionPropSet->getPropertySetInfo() );
    Reference < XTextRange > xPortionTextRange( xPortionPropSet, UNO_QUERY);
    if( !xPortionPropSet || !xPortionPropInfo
            || !xPortionPropInfo->hasPropertyByName( u"TextPortionType"_ustr ) )
        return true;

#if OSL_DEBUG_LEVEL > 0
    OUString sInfo;
    OUString sPortionType;
    if( xPortionPropSet->getPropertyValue( u"TextPortionType"_ustr ) >>= sPortionType )
    {
        sInfo = "type: " + sPortionType + "; ";
    }
#endif
    msPageCount = "";
    msDateTimeType = "";
    msTextFieldType = "";
    if( xPortionTextRange.is() )
    {
#if OSL_DEBUG_LEVEL > 0
        sInfo += "content: " + xPortionTextRange->getString() + "; ";
#endif
        mrCurrentTextPortion.set( xPortionTextRange );

        Reference < XPropertySet > xRangePropSet( xPortionTextRange, UNO_QUERY );
        if( xRangePropSet.is() && xRangePropSet->getPropertySetInfo()->hasPropertyByName( u"TextField"_ustr ) )
        {
            Reference < XTextField > xTextField( xRangePropSet->getPropertyValue( u"TextField"_ustr ), UNO_QUERY );
            if( xTextField.is() )
            {
                static constexpr OUString sServicePrefix(u"com.sun.star.text.textfield."_ustr);
                static constexpr OUString sPresentationServicePrefix(u"com.sun.star.presentation.TextField."_ustr);

                Reference< XServiceInfo > xService( xTextField, UNO_QUERY );
                const Sequence< OUString > aServices = xService->getSupportedServiceNames();

                const OUString* pNames = aServices.getConstArray();
                sal_Int32 nCount = aServices.getLength();

                OUString sFieldName;    // service name postfix of current field

                // search for TextField service name
                while( nCount-- )
                {
                    if ( pNames->matchIgnoreAsciiCase( sServicePrefix ) )
                    {
                        // TextField found => postfix is field type!
                        sFieldName = pNames->copy( sServicePrefix.getLength() );
                        break;
                    }
                    else if( pNames->startsWith( sPresentationServicePrefix ) )
                    {
                        // TextField found => postfix is field type!
                        sFieldName = pNames->copy( sPresentationServicePrefix.getLength() );
                        break;
                    }

                    ++pNames;
                }

                msTextFieldType = sFieldName;
#if OSL_DEBUG_LEVEL > 0
                sInfo += "text field type: " + sFieldName + "; content: " + xTextField->getPresentation( /* show command: */ false ) + "; ";
#endif
                // This case handles Date or Time text field inserted by the user
                // on both page/master page. It doesn't handle the standard DateTime field.
                if( sFieldName == "DateTime" )
                {
                    Reference<XPropertySet> xTextFieldPropSet(xTextField, UNO_QUERY);
                    if( xTextFieldPropSet.is() )
                    {
                        Reference<XPropertySetInfo> xPropSetInfo = xTextFieldPropSet->getPropertySetInfo();
                        if( xPropSetInfo.is() )
                        {
                            // The standard DateTime field has no property.
                            // Trying to get a property value on such field would cause a runtime exception.
                            // So the hasPropertyByName check is needed.
                            bool bIsFixed = true;
                            if( xPropSetInfo->hasPropertyByName(u"IsFixed"_ustr) && ( ( xTextFieldPropSet->getPropertyValue( u"IsFixed"_ustr ) ) >>= bIsFixed ) && !bIsFixed )
                            {
                                bool bIsDate = true;
                                if( xPropSetInfo->hasPropertyByName(u"IsDate"_ustr) && ( ( xTextFieldPropSet->getPropertyValue( u"IsDate"_ustr ) ) >>= bIsDate ) )
                                {
                                    msDateTimeType = bIsDate ? u"Date"_ustr : u"Time"_ustr;
                                }
                            }
                        }
                    }
                }
                if( sFieldName == "DateTime" || sFieldName == "Header"
                        || sFieldName == "Footer" || sFieldName == "PageNumber"
                        || sFieldName == "PageName" )
                {
                    mbIsPlaceholderShape = true;
                }
                else if (sFieldName == "PageCount")
                {
                    msPageCount = xTextField->getPresentation( /* show command: */ false );
                }
                else
                {
                    mbIsURLField = sFieldName == "URL";

                    if( mbIsURLField )
                    {
                        Reference<XPropertySet> xTextFieldPropSet(xTextField, UNO_QUERY);
                        if( xTextFieldPropSet.is() )
                        {
                            OUString sURL;
                            if( ( xTextFieldPropSet->getPropertyValue( sFieldName ) ) >>= sURL )
                            {
#if OSL_DEBUG_LEVEL > 0
                                sInfo += "url: " + mrExport.GetRelativeReference( sURL );
#endif
                                msUrl = mrExport.GetRelativeReference( sURL );
                                if( !msUrl.isEmpty() )
                                {
                                    implRegisterInterface( xPortionTextRange );

                                    const OUString& rTextPortionId = implGetValidIDFromInterface( Reference<XInterface>(xPortionTextRange, UNO_QUERY) );
                                    if( !rTextPortionId.isEmpty() )
                                    {
                                        msHyperlinkIdList += rTextPortionId + " ";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
#if OSL_DEBUG_LEVEL > 0
    mrExport.AddAttribute(u"class"_ustr, u"TextPortion"_ustr);
    SvXMLElementExport aPortionElem(mrExport, u"desc"_ustr, mbIWS, mbIWS);
    mrExport.GetDocHandler()->characters( sInfo );
#endif
    return true;
}


void SVGTextWriter::startTextShape()
{
    if( mpTextShapeElem )
    {
        OSL_FAIL( "SVGTextWriter::startTextShape: text shape already defined." );
    }

    {
        mbIsTextShapeStarted = true;
        maParentFont = vcl::Font();
        mrExport.AddAttribute(u"class"_ustr, u"SVGTextShape"_ustr);

        // if text is rotated, set transform matrix at text element
        const vcl::Font& rFont = mpVDev->GetFont();
        if( rFont.GetOrientation() )
        {
            Point   aRot( maTextPos );
            OUString aTransform = "rotate(" +
                OUString::number( rFont.GetOrientation().get() * -0.1 ) + " " +
                OUString::number( aRot.X() ) + " " +
                OUString::number( aRot.Y() ) + ")";
            mrExport.AddAttribute(aXMLAttrTransform, aTransform);
        }

        // tdf#91315: Set text direction
        auto nLayoutMode = mpVDev->GetLayoutMode();
        if (nLayoutMode
            & (vcl::text::ComplexTextLayoutFlags::BiDiRtl
               | vcl::text::ComplexTextLayoutFlags::BiDiStrong))
        {
            mrExport.AddAttribute(aXMLAttrTextDirection, u"rtl"_ustr);
        }

        mpTextShapeElem.reset(new SvXMLElementExport(mrExport, aXMLElemText, true, mbIWS));
        startTextParagraph();
    }
}


void SVGTextWriter::endTextShape()
{
    endTextParagraph();
    mrTextShape.clear();
    mrParagraphEnumeration.clear();
    mrCurrentTextParagraph.clear();
    mpTextShapeElem.reset();
    maTextOpacity.clear();
    mbIsTextShapeStarted = false;
    // these need to be invoked after the <text> element has been closed
    implExportHyperlinkIds();
    implWriteBulletChars();
    implWriteEmbeddedBitmaps();

}


void SVGTextWriter::startTextParagraph()
{
    endTextParagraph();
    nextParagraph();
    if( mbIsNewListItem )
    {
        OUString sNumberingType;
        switch( meNumberingType )
        {
            case NumberingType::CHAR_SPECIAL:
                    sNumberingType = "bullet-style";
                    break;
            case NumberingType::BITMAP:
                    sNumberingType = "image-style";
                    break;
            default:
                    sNumberingType = "number-style";
                    break;
        }
        mrExport.AddAttribute(u"ooo:numbering-type"_ustr, sNumberingType);
        mrExport.AddAttribute(u"class"_ustr, u"ListItem"_ustr);
    }
    else
    {
        mrExport.AddAttribute(u"class"_ustr, u"TextParagraph"_ustr);
    }
    maParentFont = vcl::Font();
    mpTextParagraphElem.reset(new SvXMLElementExport(mrExport, aXMLElemTspan, mbIWS, mbIWS));

    if( !mbIsListLevelStyleImage )
    {
        mbPositioningNeeded = true;
    }
}


void SVGTextWriter::endTextParagraph()
{
    mrCurrentTextPortion.clear();
    endTextPosition();
    mbIsNewListItem = false;
    mbIsListLevelStyleImage = false;
    mbPositioningNeeded = false;
    mpTextParagraphElem.reset();
}


void SVGTextWriter::startTextPosition( bool bExportX, bool bExportY )
{
    endTextPosition();
    mnTextWidth = 0;
    mrExport.AddAttribute(u"class"_ustr, u"TextPosition"_ustr);
    if( bExportX )
        mrExport.AddAttribute(aXMLAttrX, OUString::number(maTextPos.X()));
    if( bExportY )
        mrExport.AddAttribute(aXMLAttrY, OUString::number(maTextPos.Y()));

    mpTextPositionElem.reset(new SvXMLElementExport(mrExport, aXMLElemTspan, mbIWS, mbIWS));
}


void SVGTextWriter::endTextPosition()
{
    mpTextPositionElem.reset();
}

bool SVGTextWriter::hasTextOpacity() const { return !maTextOpacity.isEmpty(); }

OUString& SVGTextWriter::getTextOpacity() { return maTextOpacity; }

void SVGTextWriter::implExportHyperlinkIds()
{
    if( !msHyperlinkIdList.isEmpty() )
    {
        mrExport.AddAttribute(u"class"_ustr, u"HyperlinkIdList"_ustr);
        SvXMLElementExport aDescElem(mrExport, u"desc"_ustr, true, false);
        mrExport.GetDocHandler()->characters( msHyperlinkIdList.trim() );
        msHyperlinkIdList.clear();
    }
}


void SVGTextWriter::implWriteBulletChars()
{
    if( maBulletListItemMap.empty() )
        return;

    mrExport.AddAttribute(u"class"_ustr, u"BulletChars"_ustr);
    SvXMLElementExport aGroupElem(mrExport, aXMLElemG, true, true);

    OUString sId, sPosition, sScaling, sRefId;
    for (auto const& bulletListItem : maBulletListItemMap)
    {
        // <g id="?" > (used by animations)
        // As id we use the id of the text portion placeholder with prefix
        // bullet-char-*
        sId = "bullet-char-" + bulletListItem.first;
        mrExport.AddAttribute(u"id"_ustr, sId);
        mrExport.AddAttribute(u"class"_ustr, u"BulletChar"_ustr);
        SvXMLElementExport aBulletCharElem(mrExport, aXMLElemG, true, true);

        // <g transform="translate(x,y)" >
        {
            const BulletListItemInfo& rInfo = bulletListItem.second;

            // Add positioning attribute through a translation
            sPosition = "translate(" +
                        OUString::number( rInfo.aPos.X() ) +
                        "," + OUString::number( rInfo.aPos.Y() ) + ")";
            mrExport.AddAttribute(u"transform"_ustr, sPosition);

            mrAttributeWriter.AddPaintAttr( COL_TRANSPARENT, rInfo.aColor );

            SvXMLElementExport aPositioningElem(mrExport, aXMLElemG, true, true);

            if (mrExport.IsEmbeddedBulletGlyph(rInfo.cBulletChar))
            {
                // <use transform="scale(font-size)" xlink:ref="/" >
                // Add size attribute through a scaling
                sScaling = "scale(" + OUString::number( rInfo.aFont.GetFontHeight() ) +
                           "," + OUString::number( rInfo.aFont.GetFontHeight() )+ ")";
                mrExport.AddAttribute(u"transform"_ustr, sScaling);

                // Add ref attribute
                sRefId = "#bullet-char-template-" +
                         OUString::number( rInfo.cBulletChar );
                mrExport.AddAttribute(aXMLAttrXLinkHRef, sRefId);

                SvXMLElementExport aRefElem(mrExport, u"use"_ustr, true, true);
            }
            else
            {
                // <path d="...">
                tools::PolyPolygon aPolyPolygon;
                OUString aStr(rInfo.cBulletChar);
                mpVDev->Push(vcl::PushFlags::FONT);
                mpVDev->SetFont(rInfo.aFont);
                if (mpVDev->GetTextOutline(aPolyPolygon, aStr))
                {
                    OUString aPathString(SVGActionWriter::GetPathString(aPolyPolygon, false));
                    mrExport.AddAttribute(u"d"_ustr, aPathString);
                    SvXMLElementExport aPath(mrExport, u"path"_ustr, true, true);
                }
                mpVDev->Pop();
            }
        } // close aPositioningElem
    }

    // clear the map
    maBulletListItemMap.clear();
}


template< typename MetaBitmapActionType >
void SVGTextWriter::writeBitmapPlaceholder( const MetaBitmapActionType* pAction )
{
    // text position element
    const Point& rPos = pAction->GetPoint();
    implMap( rPos, maTextPos );
    startTextPosition();
    mbPositioningNeeded = true;
    if( mbIsNewListItem )
    {
        mbIsNewListItem = false;
        mbIsListLevelStyleImage = false;
    }

    // bitmap placeholder element
    BitmapChecksum nId = SVGActionWriter::GetChecksum( pAction );
    OUString sId = "bitmap-placeholder("  + msShapeId + "." +
                   OUString::number( nId ) + ")";

    {
        mrExport.AddAttribute(u"id"_ustr, sId);
        mrExport.AddAttribute(u"class"_ustr, u"BitmapPlaceholder"_ustr);
        SvXMLElementExport aSVGTspanElem(mrExport, aXMLElemTspan, mbIWS, mbIWS);
    }
    endTextPosition();
}


void SVGTextWriter::implWriteEmbeddedBitmaps()
{
    if( !(mpTextEmbeddedBitmapMtf && mpTextEmbeddedBitmapMtf->GetActionSize()) )
        return;

    mrExport.AddAttribute(u"class"_ustr, u"EmbeddedBitmaps"_ustr);
    SvXMLElementExport aEmbBitmapGroupElem(mrExport, aXMLElemG, true, true);

    const GDIMetaFile& rMtf = *mpTextEmbeddedBitmapMtf;

    BitmapChecksum nId, nChecksum = 0;
    Point aPt;
    Size  aSz;
    size_t nCount = rMtf.GetActionSize();
    for( size_t nCurAction = 0; nCurAction < nCount; nCurAction++ )
    {

        const MetaAction*    pAction = rMtf.GetAction( nCurAction );
        const MetaActionType nType = pAction->GetType();

        switch( nType )
        {
            case MetaActionType::BMPSCALE:
            {
                const MetaBmpScaleAction* pA = static_cast<const MetaBmpScaleAction*>(pAction);
                // The conversion to BitmapEx is needed since at the point
                // where the bitmap is actually exported a Bitmap object is
                // converted to BitmapEx before computing the checksum used
                // to generate the <image> element id.
                // (See GetBitmapChecksum in svgexport.cxx)
                nChecksum = BitmapEx( pA->GetBitmap() ).GetChecksum();
                aPt = pA->GetPoint();
                aSz = pA->GetSize();
            }
            break;
            case MetaActionType::BMPEXSCALE:
            {
                const MetaBmpExScaleAction* pA = static_cast<const MetaBmpExScaleAction*>(pAction);
                nChecksum = pA->GetBitmapEx().GetChecksum();
                aPt = pA->GetPoint();
                aSz = pA->GetSize();
            }
            break;
            default: break;
        }

        // <g id="?" > (used by animations)
        {
            // embedded bitmap id
            nId = SVGActionWriter::GetChecksum( pAction );
            OUString sId = "embedded-bitmap(" + msShapeId + "." + OUString::number( nId ) + ")";
            mrExport.AddAttribute(u"id"_ustr, sId);
            mrExport.AddAttribute(u"class"_ustr, u"EmbeddedBitmap"_ustr);

            SvXMLElementExport aEmbBitmapElem(mrExport, aXMLElemG, true, true);

            // <use x="?" y="?" xlink:ref="?" >
            {
                // referenced bitmap template
                OUString sRefId = "#bitmap(" + OUString::number( nChecksum ) + ")";

                Point aPoint;
                Size  aSize;
                implMap( aPt, aPoint );
                implMap( aSz, aSize );

                mrExport.AddAttribute(aXMLAttrX, OUString::number(aPoint.X()));
                mrExport.AddAttribute(aXMLAttrY, OUString::number(aPoint.Y()));
                mrExport.AddAttribute(aXMLAttrXLinkHRef, sRefId);

                SvXMLElementExport aRefElem(mrExport, u"use"_ustr, true, true);
            }
        } // close aEmbBitmapElem
    }
}


void SVGTextWriter::writeTextPortion(const Point& rPos, const OUString& rText, tools::Long nWidth)
{
    if( rText.isEmpty() )
        return;

    bool bStandAloneTextPortion = false;
    if( !isTextShapeStarted() )
    {
        bStandAloneTextPortion = true;
        startTextShape();
    }

    mbLineBreak = false;

    if( !mbIsNewListItem || mbIsListLevelStyleImage )
    {
        bool bNotSync = true;
        OUString sContent;
        sal_Int32 nStartPos;
        while( bNotSync )
        {
            if( mnLeftTextPortionLength <= 0  || !mrCurrentTextPortion.is() )
            {
                if( !nextTextPortion() )
                    break;
                else
                {
                    sContent = mrCurrentTextPortion->getString();
                    if( mbIsURLField && sContent.isEmpty() )
                    {
                        Reference < XPropertySet > xPropSet( mrCurrentTextPortion, UNO_QUERY );
                        Reference < XTextField > xTextField( xPropSet->getPropertyValue( u"TextField"_ustr ), UNO_QUERY );
                        sContent = xTextField->getPresentation( /* show command: */ false );
                        if( sContent.isEmpty() )
                            OSL_FAIL( "SVGTextWriter::writeTextPortion: content of URL TextField is empty." );
                    }
                    mnLeftTextPortionLength = sContent.getLength();
                }
            }
            else
            {
                sContent = mrCurrentTextPortion->getString();
            }

            nStartPos = sContent.getLength() - mnLeftTextPortionLength;
            if( nStartPos < 0 ) nStartPos = 0;
            mnLeftTextPortionLength -= rText.getLength();

            if( sContent.isEmpty() )
                continue;
            if( sContent == "\n" )
                mbLineBreak = true;
            else if (sContent == "\t")
            {
                // Need to emit position for the next text portion after a tab, otherwise the tab
                // would appear as if it has 0 width.
                mbPositioningNeeded = true;
            }
            if( sContent.match( rText, nStartPos ) )
                bNotSync = false;
        }
    }

    assert(mpVDev); //invalid virtual device

#if 0
    const FontMetric aMetric( mpVDev->GetFontMetric() );

    bool bTextSpecial = aMetric.IsShadow() || aMetric.IsOutline() || (aMetric.GetRelief() != FontRelief::NONE);

    if( true || !bTextSpecial )
    {
        implWriteTextPortion( rPos, rText, mpVDev->GetTextColor() );
    }
    else
    {
        // to be implemented
    }
#else
    implWriteTextPortion( rPos, rText, mpVDev->GetTextColor(), nWidth );
#endif

    if( bStandAloneTextPortion )
    {
        endTextShape();
    }
}


void SVGTextWriter::implWriteTextPortion(const Point& rPos, const OUString& rText, Color aTextColor,
                                         tools::Long nWidth)
{
    Point                                   aPos;
    Point                                   aBaseLinePos( rPos );
    const FontMetric                        aMetric( mpVDev->GetFontMetric() );
    const vcl::Font&                        rFont = mpVDev->GetFont();

    if( rFont.GetAlignment() == ALIGN_TOP )
        aBaseLinePos.AdjustY(aMetric.GetAscent() );
    else if( rFont.GetAlignment() == ALIGN_BOTTOM )
        aBaseLinePos.AdjustY( -(aMetric.GetDescent()) );

    implMap( rPos, aPos );

    if( mbPositioningNeeded )
    {
        mbPositioningNeeded = false;
        maTextPos.setX( aPos.X() );
        maTextPos.setY( aPos.Y() );
        startTextPosition();
    }
    else if( maTextPos.Y() != aPos.Y() )
    {
        // In case the text position moved backward we could have a line break
        // so we end the current line and start a new one.
        if( mbLineBreak || ( ( maTextPos.X() + mnTextWidth ) > aPos.X() ) )
        {
            mbLineBreak = false;
            maTextPos.setX( aPos.X() );
            maTextPos.setY( aPos.Y() );
            startTextPosition();
        }
        else // superscript, subscript, list item numbering
        {
            maTextPos.setY( aPos.Y() );
            startTextPosition( false /* do not export x attribute */ );
        }
    }
    // we are dealing with a bullet, so set up this for the next text portion
    if( mbIsNewListItem )
    {
        mbIsNewListItem = false;
        mbPositioningNeeded = true;

        if( meNumberingType == NumberingType::CHAR_SPECIAL )
        {
            // Create an id for the current text portion
            implRegisterInterface( mrCurrentTextParagraph );

            // Add the needed info to the BulletListItemMap
            OUString sId = implGetValidIDFromInterface( Reference<XInterface>(mrCurrentTextParagraph, UNO_QUERY) );
            if( !sId.isEmpty() )
            {
                sId += ".bp";
                BulletListItemInfo& aBulletListItemInfo = maBulletListItemMap[ sId ];
                aBulletListItemInfo.aFont = rFont;
                aBulletListItemInfo.aColor = aTextColor;
                aBulletListItemInfo.aPos = maTextPos;
                aBulletListItemInfo.cBulletChar = mcBulletChar;

                // Make this text portion a bullet placeholder
                mrExport.AddAttribute(u"id"_ustr, sId);
                mrExport.AddAttribute(u"class"_ustr, u"BulletPlaceholder"_ustr);
                SvXMLElementExport aSVGTspanElem(mrExport, aXMLElemTspan, mbIWS, mbIWS);
                return;
            }
        }
    }

    const OUString& rTextPortionId = implGetValidIDFromInterface( Reference<XInterface>(mrCurrentTextPortion, UNO_QUERY) );
    if( !rTextPortionId.isEmpty() )
    {
        mrExport.AddAttribute(u"id"_ustr, rTextPortionId);
    }

    if( mbIsPlaceholderShape )
    {
        OUString sClass = u"PlaceholderText"_ustr;
        // This case handles Date or Time text field inserted by the user
        // on both page/master page. It doesn't handle the standard DateTime field.
        if( !msDateTimeType.isEmpty() )
        {
            sClass += " " + msDateTimeType;
        }
        else if( !msTextFieldType.isEmpty() )
        {
            sClass += " " + msTextFieldType;
        }
        mrExport.AddAttribute(u"class"_ustr, sClass);
    }

    addFontAttributes( /* isTexTContainer: */ false );

    tools::Long nTextWidth;
    if (nWidth)
    {
        Size size;
        implMap(Size(nWidth, 0), size);
        nTextWidth = size.Width();
        mrExport.AddAttribute(u"lengthAdjust"_ustr, u"spacingAndGlyphs"_ustr);
        mrExport.AddAttribute(u"textLength"_ustr, OUString::number(nTextWidth));
    }
    else
        nTextWidth = mpVDev->GetTextWidth(rText);

    if (!maTextOpacity.isEmpty())
    {
        mrExport.AddAttribute(u"fill-opacity"_ustr, maTextOpacity);
    }

    mrAttributeWriter.AddPaintAttr( COL_TRANSPARENT, aTextColor );

    // <a> tag for link should be the innermost tag, inside <tspan>
    if( !mbIsPlaceholderShape && mbIsURLField && !msUrl.isEmpty() )
    {
        mrExport.AddAttribute(u"class"_ustr, u"UrlField"_ustr);
        mrExport.AddAttribute(aXMLAttrXLinkHRef, msUrl);

        SvXMLElementExport aSVGTspanElem(mrExport, aXMLElemTspan, mbIWS, mbIWS);
        mrExport.AddAttribute(aXMLAttrXLinkHRef, msUrl);
        {
            SvXMLElementExport aSVGAElem(mrExport, u"a"_ustr, mbIWS, mbIWS);
            mrExport.GetDocHandler()->characters( rText );
        }
    }
    else if ( !msPageCount.isEmpty() )
    {
        mrExport.AddAttribute(u"class"_ustr, u"PageCount"_ustr);
        SvXMLElementExport aSVGTspanElem(mrExport, aXMLElemTspan, mbIWS, mbIWS);
        mrExport.GetDocHandler()->characters( msPageCount );
    }
    else
    {
        // Without the following attribute Google Chrome does not render leading spaces
        mrExport.AddAttribute(u"style"_ustr, u"white-space: pre"_ustr);

        SvXMLElementExport aSVGTspanElem(mrExport, aXMLElemTspan, mbIWS, mbIWS);
        mrExport.GetDocHandler()->characters( rText );
    }

    mnTextWidth += nTextWidth;
}


SVGActionWriter::SVGActionWriter( SVGExport& rExport, SVGFontExport& rFontExport ) :
    mnCurGradientId( 1 ),
    mnCurMaskId( 1 ),
    mnCurPatternId( 1 ),
    mnCurClipPathId( 1 ),
    mrExport( rExport ),
    maContextHandler(),
    mrCurrentState( maContextHandler.getCurrentState() ),
    maAttributeWriter( rExport, rFontExport, mrCurrentState ),
    maTextWriter(rExport, maAttributeWriter, *this),
    mpVDev(VclPtr<VirtualDevice>::Create()),
    mbIsPlaceholderShape( false ),
    mpEmbeddedBitmapsMap( nullptr ),
    mbIsPreview( false )
{
    mpVDev->EnableOutput( false );
    maTargetMapMode = MapMode(MapUnit::Map100thMM);
    maTextWriter.setVirtualDevice( mpVDev, maTargetMapMode );
}


SVGActionWriter::~SVGActionWriter()
{
    mpVDev.disposeAndClear();
}


tools::Long SVGActionWriter::ImplMap( sal_Int32 nVal ) const
{
    Size aSz( nVal, nVal );

    return ImplMap( aSz, aSz ).Width();
}


Point& SVGActionWriter::ImplMap( const Point& rPt, Point& rDstPt ) const
{
    rDstPt = OutputDevice::LogicToLogic( rPt, mpVDev->GetMapMode(), maTargetMapMode );
    return rDstPt;
}


Size& SVGActionWriter::ImplMap( const Size& rSz, Size& rDstSz ) const
{
    rDstSz = OutputDevice::LogicToLogic( rSz, mpVDev->GetMapMode(), maTargetMapMode );
    return rDstSz;
}


void SVGActionWriter::ImplMap( const tools::Rectangle& rRect, tools::Rectangle& rDstRect ) const
{
    Point   aTL( rRect.TopLeft() );
    Size    aSz( rRect.GetSize() );

    rDstRect = tools::Rectangle( ImplMap( aTL, aTL ), ImplMap( aSz, aSz ) );
}


tools::Polygon& SVGActionWriter::ImplMap( const tools::Polygon& rPoly, tools::Polygon& rDstPoly ) const
{
    rDstPoly = tools::Polygon( rPoly.GetSize() );

    for( sal_uInt16 i = 0, nSize = rPoly.GetSize(); i < nSize; ++i )
    {
        ImplMap( rPoly[ i ], rDstPoly[ i ] );
        rDstPoly.SetFlags( i, rPoly.GetFlags( i ) );
    }

    return rDstPoly;
}


tools::PolyPolygon& SVGActionWriter::ImplMap( const tools::PolyPolygon& rPolyPoly, tools::PolyPolygon& rDstPolyPoly ) const
{
    tools::Polygon aPoly;

    rDstPolyPoly = tools::PolyPolygon();

    for( auto const& poly : rPolyPoly )
    {
        rDstPolyPoly.Insert( ImplMap( poly, aPoly ) );
    }

    return rDstPolyPoly;
}


OUString SVGActionWriter::GetPathString( const tools::PolyPolygon& rPolyPoly, bool bLine )
{
    OUStringBuffer   aPathData;
    static constexpr OUString   aBlank( u" "_ustr );
    static constexpr OUString   aComma( u","_ustr );
    Point                      aPolyPoint;


    for( auto rPolyIter = rPolyPoly.begin(); rPolyIter != rPolyPoly.end(); ++rPolyIter )
    {
        auto const& rPoly = *rPolyIter;
        sal_uInt16 n = 1, nSize = rPoly.GetSize();

        if( nSize > 1 )
        {
            aPolyPoint = rPoly[ 0 ];
            aPathData.append("M " +
                    OUString::number( aPolyPoint.X() ) +
                    aComma +
                    OUString::number( aPolyPoint.Y() ));

            char nCurrentMode = 0;
            const bool bClose(!bLine || rPoly[0] == rPoly[nSize - 1]);
            while( n < nSize )
            {
                aPathData.append(aBlank);

                if ( ( rPoly.GetFlags( n ) == PolyFlags::Control ) && ( ( n + 2 ) < nSize ) )
                {
                    if ( nCurrentMode != 'C' )
                    {
                        nCurrentMode = 'C';
                        aPathData.append("C ");
                    }
                    for ( int j = 0; j < 3; j++ )
                    {
                        if ( j )
                            aPathData.append(aBlank);

                        aPolyPoint = rPoly[ n++ ];
                        aPathData.append( OUString::number(aPolyPoint.X()) +
                                aComma +
                                OUString::number( aPolyPoint.Y() ) );
                    }
                }
                else
                {
                    if ( nCurrentMode != 'L' )
                    {
                        nCurrentMode = 'L';
                        aPathData.append("L ");
                    }

                    aPolyPoint = rPoly[ n++ ];
                    aPathData.append( OUString::number(aPolyPoint.X()) +
                            aComma +
                            OUString::number(aPolyPoint.Y()) );
                }
            }

            if( bClose )
                aPathData.append(" Z");

            if( rPolyIter != rPolyPoly.end() )
                aPathData.append(aBlank);
        }
    }

    return aPathData.makeStringAndClear();
}

BitmapChecksum SVGActionWriter::GetChecksum( const MetaAction* pAction )
{
    GDIMetaFile aMtf;
    MetaAction* pA = const_cast<MetaAction*>(pAction);
    aMtf.AddAction( pA );
    return SvmWriter::GetChecksum( aMtf );
}

void SVGActionWriter::SetEmbeddedBitmapRefs( const MetaBitmapActionMap* pEmbeddedBitmapsMap )
{
    if (pEmbeddedBitmapsMap)
        mpEmbeddedBitmapsMap = pEmbeddedBitmapsMap;
    else
        OSL_FAIL( "SVGActionWriter::SetEmbeddedBitmapRefs: passed pointer is null" );
}

void SVGActionWriter::ImplWriteLine( const Point& rPt1, const Point& rPt2,
                                     const Color* pLineColor )
{
    Point aPt1, aPt2;

    ImplMap( rPt1, aPt1 );
    ImplMap( rPt2, aPt2 );

    mrExport.AddAttribute(aXMLAttrX1, OUString::number(aPt1.X()));
    mrExport.AddAttribute(aXMLAttrY1, OUString::number(aPt1.Y()));
    mrExport.AddAttribute(aXMLAttrX2, OUString::number(aPt2.X()));
    mrExport.AddAttribute(aXMLAttrY2, OUString::number(aPt2.Y()));

    if( pLineColor )
    {
        // !!! mrExport.AddAttribute( ... )
        OSL_FAIL( "SVGActionWriter::ImplWriteLine: Line color not implemented" );
    }

    {
        SvXMLElementExport aElem(mrExport, u"line"_ustr, true, true);
    }
}


void SVGActionWriter::ImplWriteRect( const tools::Rectangle& rRect, tools::Long nRadX, tools::Long nRadY )
{
    tools::Rectangle aRect;

    ImplMap( rRect, aRect );

    mrExport.AddAttribute(aXMLAttrX, OUString::number(aRect.Left()));
    mrExport.AddAttribute(aXMLAttrY, OUString::number(aRect.Top()));
    mrExport.AddAttribute(aXMLAttrWidth, OUString::number(aRect.GetWidth()));
    mrExport.AddAttribute(aXMLAttrHeight, OUString::number(aRect.GetHeight()));

    if( nRadX )
        mrExport.AddAttribute(aXMLAttrRX, OUString::number(ImplMap(nRadX)));

    if( nRadY )
        mrExport.AddAttribute(aXMLAttrRY, OUString::number(ImplMap(nRadY)));

    SvXMLElementExport aElem(mrExport, u"rect"_ustr, true, true);
}


void SVGActionWriter::ImplWriteEllipse( const Point& rCenter, tools::Long nRadX, tools::Long nRadY )
{
    Point aCenter;

    ImplMap( rCenter, aCenter );

    mrExport.AddAttribute(aXMLAttrCX, OUString::number(aCenter.X()));
    mrExport.AddAttribute(aXMLAttrCY, OUString::number(aCenter.Y()));
    mrExport.AddAttribute(aXMLAttrRX, OUString::number(ImplMap(nRadX)));
    mrExport.AddAttribute(aXMLAttrRY, OUString::number(ImplMap(nRadY)));

    {
        SvXMLElementExport aElem(mrExport, u"ellipse"_ustr, true, true);
    }
}


void SVGActionWriter::ImplAddLineAttr( const LineInfo &rAttrs )
{
    if ( rAttrs.IsDefault() )
        return;

    sal_Int32 nStrokeWidth = ImplMap( rAttrs.GetWidth() );
    mrExport.AddAttribute(aXMLAttrStrokeWidth, OUString::number(nStrokeWidth));
    // support for LineJoint
    switch(rAttrs.GetLineJoin())
    {
        case basegfx::B2DLineJoin::NONE:
        case basegfx::B2DLineJoin::Miter:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinejoin, u"miter"_ustr);
            break;
        }
        case basegfx::B2DLineJoin::Bevel:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinejoin, u"bevel"_ustr);
            break;
        }
        case basegfx::B2DLineJoin::Round:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinejoin, u"round"_ustr);
            break;
        }
    }

    // support for LineCap
    switch(rAttrs.GetLineCap())
    {
        default: /* css::drawing::LineCap_BUTT */
        {
            // butt is Svg default, so no need to write until the exporter might write styles.
            // If this happens, activate here
            // mrExport.AddAttribute(aXMLAttrStrokeLinecap, "butt");
            break;
        }
        case css::drawing::LineCap_ROUND:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinecap, u"round"_ustr);
            break;
        }
        case css::drawing::LineCap_SQUARE:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinecap, u"square"_ustr);
            break;
        }
    }

}


void SVGActionWriter::ImplWritePolyPolygon( const tools::PolyPolygon& rPolyPoly, bool bLineOnly,
                                            bool bApplyMapping )
{
    tools::PolyPolygon aPolyPoly;

    if( bApplyMapping )
        ImplMap( rPolyPoly, aPolyPoly );
    else
        aPolyPoly = rPolyPoly;

    // add path data attribute
    mrExport.AddAttribute(u"d"_ustr, GetPathString(aPolyPoly, bLineOnly));

    {
        // write polyline/polygon element
        SvXMLElementExport aElem(mrExport, u"path"_ustr, true, true);
    }
}


void SVGActionWriter::ImplWriteShape( const SVGShapeDescriptor& rShape )
{
    tools::PolyPolygon aPolyPoly;

    ImplMap( rShape.maShapePolyPoly, aPolyPoly );

    const bool bLineOnly
        = (rShape.maShapeFillColor == COL_TRANSPARENT) && (!rShape.moShapeGradient);
    tools::Rectangle   aBoundRect( aPolyPoly.GetBoundRect() );

    maAttributeWriter.AddPaintAttr( rShape.maShapeLineColor, rShape.maShapeFillColor, &aBoundRect,
                                   rShape.moShapeGradient ? &*rShape.moShapeGradient : nullptr );

    if( !rShape.maId.isEmpty() )
        mrExport.AddAttribute(aXMLAttrId, rShape.maId);

    if( rShape.mnStrokeWidth )
    {
        sal_Int32 nStrokeWidth = ImplMap( rShape.mnStrokeWidth );
        mrExport.AddAttribute(aXMLAttrStrokeWidth, OUString::number(nStrokeWidth));
    }

    // support for LineJoin
    switch(rShape.maLineJoin)
    {
        case basegfx::B2DLineJoin::NONE:
        case basegfx::B2DLineJoin::Miter:
        {
            // miter is Svg default, so no need to write until the exporter might write styles.
            // If this happens, activate here
            // mrExport.AddAttribute(aXMLAttrStrokeLinejoin, "miter");
            break;
        }
        case basegfx::B2DLineJoin::Bevel:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinejoin, u"bevel"_ustr);
            break;
        }
        case basegfx::B2DLineJoin::Round:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinejoin, u"round"_ustr);
            break;
        }
    }

    // support for LineCap
    switch(rShape.maLineCap)
    {
        default: /* css::drawing::LineCap_BUTT */
        {
            // butt is Svg default, so no need to write until the exporter might write styles.
            // If this happens, activate here
            // mrExport.AddAttribute(aXMLAttrStrokeLinecap, "butt");
            break;
        }
        case css::drawing::LineCap_ROUND:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinecap, u"round"_ustr);
            break;
        }
        case css::drawing::LineCap_SQUARE:
        {
            mrExport.AddAttribute(aXMLAttrStrokeLinecap, u"square"_ustr);
            break;
        }
    }

    if( !rShape.maDashArray.empty() )
    {
        OUStringBuffer   aDashArrayStr;

        for( size_t k = 0; k < rShape.maDashArray.size(); ++k )
        {
            const sal_Int32 nDash = ImplMap(basegfx::fround(rShape.maDashArray[k]));

            if( k )
                aDashArrayStr.append(",");

            aDashArrayStr.append( nDash );
        }

        mrExport.AddAttribute(u"stroke-dasharray"_ustr, aDashArrayStr.makeStringAndClear());
    }

    ImplWritePolyPolygon( aPolyPoly, bLineOnly, false );
}



void SVGActionWriter::ImplCreateClipPathDef( const tools::PolyPolygon& rPolyPoly )
{
    OUString aClipPathId = aPrefixClipPathId + OUString::number( mnCurClipPathId++ );

    SvXMLElementExport aElemDefs(mrExport, aXMLElemDefs, true, true);

    {
        mrExport.AddAttribute(aXMLAttrId, aClipPathId);
        mrExport.AddAttribute(u"clipPathUnits"_ustr, u"userSpaceOnUse"_ustr);
        SvXMLElementExport aElemClipPath(mrExport, u"clipPath"_ustr, true, true);

        ImplWritePolyPolygon(rPolyPoly, false);
    }
}

void SVGActionWriter::ImplStartClipRegion(sal_Int32 nClipPathId)
{
    assert(!mpCurrentClipRegionElem);

    if (nClipPathId == 0)
        return;

    OUString aUrl = OUString::Concat("url(#") + aPrefixClipPathId + OUString::number( nClipPathId ) + ")";
    mrExport.AddAttribute(u"clip-path"_ustr, aUrl);
    mpCurrentClipRegionElem.reset(new SvXMLElementExport(mrExport, aXMLElemG, true, true));
}

void SVGActionWriter::ImplEndClipRegion()
{
    if (mpCurrentClipRegionElem)
    {
        mpCurrentClipRegionElem.reset();
    }
}

void SVGActionWriter::ImplWriteClipPath( const tools::PolyPolygon& rPolyPoly )
{
    ImplEndClipRegion();

    if( rPolyPoly.Count() == 0 )
        return;

    ImplCreateClipPathDef(rPolyPoly);
    mrCurrentState.nRegionClipPathId = mnCurClipPathId - 1;
    ImplStartClipRegion( mrCurrentState.nRegionClipPathId );
}

void SVGActionWriter::ImplWritePattern( const tools::PolyPolygon& rPolyPoly,
                                        const Hatch* pHatch,
                                        const Gradient* pGradient,
                                        sal_uInt32 nWriteFlags )
{
    if( !rPolyPoly.Count() )
        return;

    SvXMLElementExport aElemG(mrExport, aXMLElemG, true, true);

    OUString aPatternId = "pattern" + OUString::number( mnCurPatternId++ );

    {
        SvXMLElementExport aElemDefs(mrExport, aXMLElemDefs, true, true);

        mrExport.AddAttribute(aXMLAttrId, aPatternId);

        tools::Rectangle aRect;
        ImplMap( rPolyPoly.GetBoundRect(), aRect );

        mrExport.AddAttribute(aXMLAttrX, OUString::number(aRect.Left()));
        mrExport.AddAttribute(aXMLAttrY, OUString::number(aRect.Top()));
        mrExport.AddAttribute(aXMLAttrWidth, OUString::number(aRect.GetWidth()));
        mrExport.AddAttribute(aXMLAttrHeight, OUString::number(aRect.GetHeight()));

        mrExport.AddAttribute(u"patternUnits"_ustr, u"userSpaceOnUse"_ustr);

        {
            SvXMLElementExport aElemPattern(mrExport, u"pattern"_ustr, true, true);

            // The origin of a pattern is positioned at (aRect.Left(), aRect.Top()).
            // So we need to adjust the pattern coordinate.
            OUString aTransform = "translate(" +
                                  OUString::number( -aRect.Left() ) +
                                  "," + OUString::number( -aRect.Top() ) +
                                  ")";
            mrExport.AddAttribute(aXMLAttrTransform, aTransform);

            {
                SvXMLElementExport aElemG2(mrExport, aXMLElemG, true, true);

                GDIMetaFile aTmpMtf;
                if( pHatch )
                {
                    mpVDev->AddHatchActions( rPolyPoly, *pHatch, aTmpMtf );
                }
                else if ( pGradient )
                {
                    Gradient aGradient(*pGradient);
                    aGradient.AddGradientActions( rPolyPoly.GetBoundRect(), aTmpMtf );
                }

                ImplWriteActions( aTmpMtf, nWriteFlags, u""_ustr );
            }
        }
    }

    OUString aPatternStyle = "fill:url(#" + aPatternId + ")";

    mrExport.AddAttribute(aXMLAttrStyle, aPatternStyle);
    ImplWritePolyPolygon( rPolyPoly, false );
}


void SVGActionWriter::ImplWriteGradientEx( const tools::PolyPolygon& rPolyPoly, const Gradient& rGradient,
                                           sal_uInt32 nWriteFlags, const basegfx::BColorStops* pColorStops)
{
    if ( rGradient.GetStyle() == css::awt::GradientStyle_LINEAR ||
         rGradient.GetStyle() == css::awt::GradientStyle_AXIAL )
    {
        ImplWriteGradientLinear( rPolyPoly, rGradient, pColorStops );
    }
    else
    {
        ImplWritePattern( rPolyPoly, nullptr, &rGradient, nWriteFlags );
    }
}


void SVGActionWriter::ImplWriteGradientLinear( const tools::PolyPolygon& rPolyPoly,
                                               const Gradient& rGradient, const basegfx::BColorStops* pColorStops )
{
    if( !rPolyPoly.Count() )
        return;

    SvXMLElementExport aElemG(mrExport, aXMLElemG, true, true);

    OUString aGradientId = "gradient" + OUString::number( mnCurGradientId++ );

    {
        SvXMLElementExport aElemDefs(mrExport, aXMLElemDefs, true, true);

        mrExport.AddAttribute(aXMLAttrId, aGradientId);
        {
            tools::Rectangle aTmpRect, aRect;
            Point aTmpCenter, aCenter;

            rGradient.GetBoundRect( rPolyPoly.GetBoundRect(), aTmpRect, aTmpCenter );
            ImplMap( aTmpRect, aRect );
            ImplMap( aTmpCenter, aCenter );
            const Degree10 nAngle = rGradient.GetAngle() % 3600_deg10;

            tools::Polygon aPoly( 2 );
            // Setting x value of a gradient vector to rotation center to
            // place a gradient vector in a target polygon.
            // This would help editing it in SVG editors like inkscape.
            aPoly[ 0 ].setX( aCenter.X() );
            aPoly[ 1 ].setX( aCenter.X() );
            aPoly[ 0 ].setY( aRect.Top() );
            aPoly[ 1 ].setY( aRect.Bottom() );
            aPoly.Rotate( aCenter, nAngle );

            mrExport.AddAttribute(aXMLAttrX1, OUString::number(aPoly[0].X()));
            mrExport.AddAttribute(aXMLAttrY1, OUString::number(aPoly[0].Y()));
            mrExport.AddAttribute(aXMLAttrX2, OUString::number(aPoly[1].X()));
            mrExport.AddAttribute(aXMLAttrY2, OUString::number(aPoly[1].Y()));

            mrExport.AddAttribute(aXMLAttrGradientUnits, u"userSpaceOnUse"_ustr);
        }

        {
            SvXMLElementExport aElemLinearGradient(mrExport, aXMLElemLinearGradient, true, true);
            basegfx::BColorStops aColorStops;

            if (nullptr != pColorStops && pColorStops->size() > 1)
            {
                // if we got the real colr stops, use them. That way we are
                // now capable in the SVG export to export real multi color gradients
                aColorStops = *pColorStops;
            }
            else
            {
                // else create color stops with 'old' start/endColor
                aColorStops.emplace_back(0.0, rGradient.GetStartColor().getBColor());
                aColorStops.emplace_back(1.0, rGradient.GetEndColor().getBColor());
            }

            // create a basegfx::BGradient with the info to be able to directly
            // use the tooling it offers
            basegfx::BGradient aGradient(
                aColorStops,
                rGradient.GetStyle(),
                rGradient.GetAngle(),
                rGradient.GetOfsX(),
                rGradient.GetOfsY(),
                rGradient.GetBorder(),
                rGradient.GetStartIntensity(),
                rGradient.GetEndIntensity(),
                rGradient.GetSteps());

            // apply Start/EndIntensity to the whole color stops - if used
            aGradient.tryToApplyStartEndIntensity();

            // apply border to color stops - if used
            aGradient.tryToApplyBorder();

            // convert from 'axial' to linear - if needed and used
            aGradient.tryToApplyAxial();

            // apply 'Steps' as hard gradient stops - if used
            aGradient.tryToApplySteps();

            // write prepared gradient stops
            for (const auto& rCand : aGradient.GetColorStops())
            {
                ImplWriteGradientStop(
                    Color(rCand.getStopColor()),
                    rCand.getStopOffset());
                    //  aStartColor, fBorderOffset );
            }
        }
    }

    OUString aGradientStyle = "fill:url(#" + aGradientId + ")";

    mrExport.AddAttribute(aXMLAttrStyle, aGradientStyle);
    ImplWritePolyPolygon( rPolyPoly, false );
}


void SVGActionWriter::ImplWriteGradientStop( const Color& rColor, double fOffset )
{
    mrExport.AddAttribute(aXMLAttrOffset, OUString::number(fOffset));

    OUString aStyle, aColor;
    aStyle += "stop-color:";
    SVGAttributeWriter::ImplGetColorStr ( rColor, aColor );
    aStyle += aColor;

    mrExport.AddAttribute(aXMLAttrStyle, aStyle);
    {
        SvXMLElementExport aElemStartStop(mrExport, aXMLElemStop, true, true);
    }
}


Color SVGActionWriter::ImplGetColorWithIntensity( const Color& rColor,
                                                  sal_uInt16 nIntensity )
{
     sal_uInt8 nNewRed = static_cast<sal_uInt8>( static_cast<tools::Long>(rColor.GetRed()) * nIntensity / 100 );
     sal_uInt8 nNewGreen = static_cast<sal_uInt8>( static_cast<tools::Long>(rColor.GetGreen()) * nIntensity / 100 );
     sal_uInt8 nNewBlue = static_cast<sal_uInt8>( static_cast<tools::Long>(rColor.GetBlue()) * nIntensity / 100 );
     return Color( nNewRed, nNewGreen, nNewBlue);
}


void SVGActionWriter::StartMask(const Point& rDestPt, const Size& rDestSize,
                                const Gradient& rGradient, sal_uInt32 nWriteFlags,
                                const basegfx::BColorStops* pColorStops, OUString* pTextFillOpacity)
{
    OUString aStyle;
    if (rGradient.GetStartColor() == rGradient.GetEndColor())
    {
        // Special case: constant alpha value.
        const Color& rColor = rGradient.GetStartColor();
        const double fOpacity = 1.0 - static_cast<double>(rColor.GetLuminance()) / 255;
        if (pTextFillOpacity)
        {
            // Don't write anything, return what is a value suitable for <tspan fill-opacity="...">.
            *pTextFillOpacity = OUString::number(fOpacity);
            return;
        }
        else
        {
            aStyle = "opacity: " + OUString::number(fOpacity);
        }
    }
    else
    {
        OUString aMaskId = "mask" + OUString::number(mnCurMaskId++);

        {
            SvXMLElementExport aElemDefs(mrExport, aXMLElemDefs, true, true);

            mrExport.AddAttribute(aXMLAttrId, aMaskId);
            {
                SvXMLElementExport aElemMask(mrExport, u"mask"_ustr, true, true);

                const tools::PolyPolygon aPolyPolygon(tools::PolyPolygon(tools::Rectangle(rDestPt, rDestSize)));
                Gradient aGradient(rGradient);

                // swap gradient stops to adopt SVG mask
                Color aTmpColor(aGradient.GetStartColor());
                sal_uInt16 nTmpIntensity(aGradient.GetStartIntensity());
                aGradient.SetStartColor(aGradient.GetEndColor());
                aGradient.SetStartIntensity(aGradient.GetEndIntensity());
                aGradient.SetEndColor(aTmpColor);
                aGradient.SetEndIntensity(nTmpIntensity);

                // tdf#155479 prep local ColorStops. The code above
                // implies that the ColorStops need to be reversed,
                // so do so & use change of local ptr to represent this
                basegfx::BColorStops aLocalColorStops;

                if (nullptr != pColorStops)
                {
                    aLocalColorStops = *pColorStops;
                    aLocalColorStops.reverseColorStops();
                    pColorStops = &aLocalColorStops;
                }

                ImplWriteGradientEx(aPolyPolygon, aGradient, nWriteFlags, pColorStops);
            }
        }

        aStyle = "mask:url(#" + aMaskId + ")";
    }
    mrExport.AddAttribute(aXMLAttrStyle, aStyle);
}

void SVGActionWriter::ImplWriteMask(GDIMetaFile& rMtf, const Point& rDestPt, const Size& rDestSize,
                                    const Gradient& rGradient, sal_uInt32 nWriteFlags, const basegfx::BColorStops* pColorStops)
{
    Point aSrcPt(rMtf.GetPrefMapMode().GetOrigin());
    const Size aSrcSize(rMtf.GetPrefSize());
    const double fScaleX
        = aSrcSize.Width() ? static_cast<double>(rDestSize.Width()) / aSrcSize.Width() : 1.0;
    const double fScaleY
        = aSrcSize.Height() ? static_cast<double>(rDestSize.Height()) / aSrcSize.Height() : 1.0;
    tools::Long nMoveX, nMoveY;

    if (fScaleX != 1.0 || fScaleY != 1.0)
    {
        rMtf.Scale(fScaleX, fScaleY);
        aSrcPt.setX(basegfx::fround<tools::Long>(aSrcPt.X() * fScaleX));
        aSrcPt.setY(basegfx::fround<tools::Long>(aSrcPt.Y() * fScaleY));
    }

    nMoveX = rDestPt.X() - aSrcPt.X();
    nMoveY = rDestPt.Y() - aSrcPt.Y();

    if (nMoveX || nMoveY)
        rMtf.Move(nMoveX, nMoveY);

    std::optional<OUString> oTextOpacity;
    if (maTextWriter.isTextShapeStarted())
    {
        // We're inside <text>, then try to use the fill-opacity attribute instead of a <g> element
        // to express transparency to ensure well-formed output.
        oTextOpacity = maTextWriter.getTextOpacity();
        StartMask(rDestPt, rDestSize, rGradient, nWriteFlags, pColorStops, &maTextWriter.getTextOpacity());
    }

    {
        std::unique_ptr<SvXMLElementExport> pElemG;
        if (!maTextWriter.hasTextOpacity())
        {
            StartMask(rDestPt, rDestSize, rGradient, nWriteFlags, pColorStops);
            pElemG.reset(new SvXMLElementExport(mrExport, aXMLElemG, true, true));
        }

        mpVDev->Push();
        ImplWriteActions( rMtf, nWriteFlags, u""_ustr );
        mpVDev->Pop();
    }

    if (oTextOpacity)
    {
        maTextWriter.getTextOpacity() = *oTextOpacity;
    }
}


void SVGActionWriter::ImplWriteText( const Point& rPos, const OUString& rText,
                                     KernArraySpan pDXArray, tools::Long nWidth )
{
    const FontMetric aMetric( mpVDev->GetFontMetric() );

    bool bTextSpecial = aMetric.IsShadow() || aMetric.IsOutline() || (aMetric.GetRelief() != FontRelief::NONE);

    if( !bTextSpecial )
    {
        ImplWriteText( rPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );
    }
    else
    {
        if( aMetric.GetRelief() != FontRelief::NONE )
        {
            Color aReliefColor( COL_LIGHTGRAY );
            Color aTextColor( mpVDev->GetTextColor() );

            if ( aTextColor == COL_BLACK )
                aTextColor = COL_WHITE;

            // coverity[copy_paste_error : FALSE] - aReliefColor depending on aTextColor is correct
            if (aTextColor == COL_WHITE)
                aReliefColor = COL_BLACK;


            Point aPos( rPos );
            Point aOffset( 6, 6 );

            if ( aMetric.GetRelief() == FontRelief::Engraved )
            {
                aPos -= aOffset;
            }
            else
            {
                aPos += aOffset;
            }

            ImplWriteText( aPos, rText, pDXArray, nWidth, aReliefColor );
            ImplWriteText( rPos, rText, pDXArray, nWidth, aTextColor );
        }
        else
        {
            if( aMetric.IsShadow() )
            {
                tools::Long nOff = 1 + ((aMetric.GetLineHeight()-24)/24);
                if ( aMetric.IsOutline() )
                    nOff += 6;

                Color aTextColor( mpVDev->GetTextColor() );
                Color aShadowColor( COL_BLACK );

                if ( (aTextColor == COL_BLACK) || (aTextColor.GetLuminance() < 8) )
                    aShadowColor = COL_LIGHTGRAY;

                Point aPos( rPos );
                aPos += Point( nOff, nOff );
                ImplWriteText( aPos, rText, pDXArray, nWidth, aShadowColor );

                if( !aMetric.IsOutline() )
                {
                    ImplWriteText( rPos, rText, pDXArray, nWidth, aTextColor );
                }
            }

            if( aMetric.IsOutline() )
            {
                Point aPos = rPos + Point( -6, -6 );
                ImplWriteText( aPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );
                aPos = rPos + Point( +6, +6);
                ImplWriteText( aPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );
                aPos = rPos + Point( -6, +0);
                ImplWriteText( aPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );
                aPos = rPos + Point( -6, +6);
                ImplWriteText( aPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );
                aPos = rPos + Point( +0, +6);
                ImplWriteText( aPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );
                aPos = rPos + Point( +0, -6);
                ImplWriteText( aPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );
                aPos = rPos + Point( +6, -1);
                ImplWriteText( aPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );
                aPos = rPos + Point( +6, +0);
                ImplWriteText( aPos, rText, pDXArray, nWidth, mpVDev->GetTextColor() );

                ImplWriteText( rPos, rText, pDXArray, nWidth, COL_WHITE );
            }
        }
    }
}


void SVGActionWriter::ImplWriteText( const Point& rPos, const OUString& rText,
                                     KernArraySpan pDXArray, tools::Long nWidth,
                                     Color aTextColor )
{
    sal_Int32                               nLen = rText.getLength();
    Size                                    aNormSize;
    Point                                   aPos;
    Point                                   aBaseLinePos( rPos );
    const FontMetric                        aMetric( mpVDev->GetFontMetric() );
    const vcl::Font&                        rFont = mpVDev->GetFont();

    if( rFont.GetAlignment() == ALIGN_TOP )
        aBaseLinePos.AdjustY(aMetric.GetAscent() );
    else if( rFont.GetAlignment() == ALIGN_BOTTOM )
        aBaseLinePos.AdjustY( -(aMetric.GetDescent()) );

    ImplMap( rPos, aPos );

    KernArray aTmpArray;
    // get text sizes
    if( !pDXArray.empty() )
    {
        aNormSize = Size( mpVDev->GetTextWidth( rText ), 0 );
        aTmpArray.assign(pDXArray.begin(), pDXArray.end());
    }
    else
    {
        aNormSize
            = Size(basegfx::fround<tools::Long>(mpVDev->GetTextArray(rText, &aTmpArray)), 0);
    }

    // if text is rotated, set transform matrix at new g element
    if( rFont.GetOrientation() )
    {
        Point   aRot( aPos );
        OUString  aTransform = "rotate(" +
                    OUString::number( rFont.GetOrientation().get() * -0.1 ) + " " +
                    OUString::number( aRot.X() ) + " " +
                    OUString::number( aRot.Y() ) + ")";
        mrExport.AddAttribute(aXMLAttrTransform, aTransform);
    }


    maAttributeWriter.AddPaintAttr( COL_TRANSPARENT, aTextColor );

    // for each line of text there should be at least one group element
    SvXMLElementExport aSVGGElem(mrExport, aXMLElemG, true, false);

    bool bIsPlaceholderField = false;

    if( mbIsPlaceholderShape )
    {
        bIsPlaceholderField = rText.match( sPlaceholderTag );
        // for a placeholder text field we export only one <text> svg element
        if( bIsPlaceholderField )
        {
            OUString sCleanTextContent;
            static const sal_Int32 nFrom = sPlaceholderTag.getLength();
            if( rText.getLength() > nFrom )
            {
                sCleanTextContent = rText.copy( nFrom );
            }
            mrExport.AddAttribute(u"class"_ustr, u"PlaceholderText"_ustr);
            mrExport.AddAttribute(aXMLAttrX, OUString::number(aPos.X()));
            mrExport.AddAttribute(aXMLAttrY, OUString::number(aPos.Y()));
            {
                SvXMLElementExport aElem(mrExport, aXMLElemText, true, false);
                // At least for the single slide case we need really to  export placeholder text
                mrExport.GetDocHandler()->characters( sCleanTextContent );
            }
        }
    }

    if( !bIsPlaceholderField )
    {
        if( nLen > 1 )
        {
            aNormSize.setWidth( aTmpArray[ nLen - 2 ] + mpVDev->GetTextWidth( OUString(rText[nLen - 1]) ) );

            if( nWidth && aNormSize.Width() && ( nWidth != aNormSize.Width() ) )
            {
                tools::Long i;
                const double fFactor = static_cast<double>(nWidth) / aNormSize.Width();

                for( i = 0; i < ( nLen - 1 ); i++ )
                    aTmpArray[i] *= fFactor;
            }
            else
            {
                css::uno::Reference< css::i18n::XBreakIterator > xBI( vcl::unohelper::CreateBreakIterator() );
                const css::lang::Locale& rLocale = Application::GetSettings().GetLanguageTag().getLocale();
                sal_Int32 nCurPos = 0, nLastPos = 0, nX = aPos.X();

                // write single glyphs at absolute text positions
                for( bool bCont = true; bCont; )
                {
                    sal_Int32 nCount = 1;

                    nLastPos = nCurPos;
                    nCurPos = xBI->nextCharacters( rText, nCurPos, rLocale,
                                                css::i18n::CharacterIteratorMode::SKIPCELL,
                                                nCount, nCount );

                    nCount = nCurPos - nLastPos;
                    bCont = ( nCurPos < rText.getLength() ) && nCount;

                    if( nCount )
                    {
                        const OUString aGlyph( rText.copy( nLastPos, nCount ) );

                        mrExport.AddAttribute(aXMLAttrX, OUString::number(nX));
                        mrExport.AddAttribute(aXMLAttrY, OUString::number(aPos.Y()));

                        {
                            SvXMLElementExport aElem(mrExport, aXMLElemText, true, false);
                            mrExport.GetDocHandler()->characters( aGlyph );
                        }

                        if( bCont )
                        {
                            // #118796# do NOT access pDXArray, it may be zero (!)
                            sal_Int32 nDXWidth = aTmpArray[ nCurPos - 1 ];
                            nDXWidth = ImplMap( nDXWidth );
                            nX = aPos.X() + nDXWidth;
                        }
                    }
                }
            }
        }
        else
        {
            mrExport.AddAttribute(aXMLAttrX, OUString::number(aPos.X()));
            mrExport.AddAttribute(aXMLAttrY, OUString::number(aPos.Y()));

            {
                SvXMLElementExport aElem(mrExport, aXMLElemText, true, false);
                mrExport.GetDocHandler()->characters( rText );
            }
        }
    }


    if( mrExport.IsUseNativeTextDecoration() )
        return;

    if( rFont.GetStrikeout() == STRIKEOUT_NONE && rFont.GetUnderline() == LINESTYLE_NONE )
        return;

    tools::Polygon aPoly( 4 );
    const tools::Long  nLineHeight = std::max<tools::Long>( basegfx::fround<tools::Long>( aMetric.GetLineHeight() * 0.05 ), 1 );

    if( rFont.GetStrikeout() )
    {
        const tools::Long nYLinePos = aBaseLinePos.Y() - basegfx::fround<tools::Long>( aMetric.GetAscent() * 0.26 );

        aPoly[ 0 ].setX( aBaseLinePos.X() ); aPoly[ 0 ].setY( nYLinePos - ( nLineHeight >> 1 ) );
        aPoly[ 1 ].setX( aBaseLinePos.X() + aNormSize.Width() - 1 ); aPoly[ 1 ].setY( aPoly[ 0 ].Y() );
        aPoly[ 2 ].setX( aPoly[ 1 ].X() ); aPoly[ 2 ].setY( aPoly[ 0 ].Y() + nLineHeight - 1 );
        aPoly[ 3 ].setX( aPoly[ 0 ].X() ); aPoly[ 3 ].setY( aPoly[ 2 ].Y() );

        ImplWritePolyPolygon( tools::PolyPolygon(aPoly), false );
    }

    if( rFont.GetUnderline() )
    {
        const tools::Long  nYLinePos = aBaseLinePos.Y() + ( nLineHeight << 1 );

        aPoly[ 0 ].setX( aBaseLinePos.X() ); aPoly[ 0 ].setY( nYLinePos - ( nLineHeight >> 1 ) );
        aPoly[ 1 ].setX( aBaseLinePos.X() + aNormSize.Width() - 1 ); aPoly[ 1 ].setY( aPoly[ 0 ].Y() );
        aPoly[ 2 ].setX( aPoly[ 1 ].X() ); aPoly[ 2 ].setY( aPoly[ 0 ].Y() + nLineHeight - 1 );
        aPoly[ 3 ].setX( aPoly[ 0 ].X() ); aPoly[ 3 ].setY( aPoly[ 2 ].Y() );

        ImplWritePolyPolygon( tools::PolyPolygon(aPoly), false );
    }
}

namespace
{
void GetGraphicFromXShape(const css::uno::Reference<css::drawing::XShape>* pShape, Graphic& rGraphic)
{
    if (!pShape)
    {
        return;
    }

    uno::Reference<beans::XPropertySet> xPropertySet(*pShape, uno::UNO_QUERY);
    if (!xPropertySet.is())
    {
        return;
    }

    uno::Reference<graphic::XGraphic> xGraphic;
    if (xPropertySet->getPropertySetInfo()->hasPropertyByName(u"Graphic"_ustr))
    {
        xPropertySet->getPropertyValue(u"Graphic"_ustr) >>= xGraphic;
    }
    rGraphic= Graphic(xGraphic);
}
}

void SVGActionWriter::ImplWriteBmp( const BitmapEx& rBmpEx,
                                    const Point& rPt, const Size& rSz,
                                    const Point& rSrcPt, const Size& rSrcSz,
                                    const css::uno::Reference<css::drawing::XShape>* pShape )
{
    if( rBmpEx.IsEmpty() )
        return;
    if( mpEmbeddedBitmapsMap && !mpEmbeddedBitmapsMap->empty())
    {
        BitmapChecksum nChecksum = rBmpEx.GetChecksum();
        if( mpEmbeddedBitmapsMap->find( nChecksum ) != mpEmbeddedBitmapsMap->end() )
        {
            // <use transform="translate(?) scale(?)" xlink:ref="?" >
            OUString sTransform;

            Point aPoint;
            ImplMap( rPt, aPoint );
            if( aPoint.X() != 0 || aPoint.Y() != 0 )
                sTransform = "translate(" + OUString::number( aPoint.X() ) + ", " + OUString::number( aPoint.Y() ) + ")";

            Size  aSize;
            ImplMap( rSz, aSize );

            MapMode aSourceMode( MapUnit::MapPixel );
            Size aPrefSize = OutputDevice::LogicToLogic( rSrcSz, aSourceMode, maTargetMapMode );
            Fraction aFractionX( aSize.Width(), aPrefSize.Width() );
            Fraction aFractionY( aSize.Height(), aPrefSize.Height() );
            double scaleX = rtl_math_round( double(aFractionX), 3, rtl_math_RoundingMode::rtl_math_RoundingMode_Corrected );
            double scaleY = rtl_math_round( double(aFractionY), 3, rtl_math_RoundingMode::rtl_math_RoundingMode_Corrected );
            if( !rtl_math_approxEqual( scaleX, 1.0 ) || !rtl_math_approxEqual( scaleY, 1.0 ) )
                sTransform += " scale(" + OUString::number( double(aFractionX) ) + ", " + OUString::number( double(aFractionY) ) + ")";

            if( !sTransform.isEmpty() )
                mrExport.AddAttribute(aXMLAttrTransform, sTransform);

            // referenced bitmap template
            OUString sRefId = "#bitmap(" + OUString::number( nChecksum ) + ")";
            mrExport.AddAttribute(aXMLAttrXLinkHRef, sRefId);

            SvXMLElementExport aRefElem(mrExport, u"use"_ustr, true, true);
            return;
        }
    }

    BitmapEx aBmpEx( rBmpEx );
    const tools::Rectangle aBmpRect( Point(), rBmpEx.GetSizePixel() );
    const tools::Rectangle aSrcRect( rSrcPt, rSrcSz );

    if( aSrcRect != aBmpRect )
        aBmpEx.Crop( aSrcRect );

    if( aBmpEx.IsEmpty() )
        return;

    SvMemoryStream aOStm( 65535, 65535 );

    bool bCached = false;
    Graphic aGraphic;
    bool bJPG = false;
    if (pShape)
    {
        GetGraphicFromXShape(pShape, aGraphic);
        if (aGraphic.GetType() == GraphicType::Bitmap)
        {
            const BitmapEx& rGraphicBitmap = aGraphic.GetBitmapExRef();
            if (rGraphicBitmap == rBmpEx)
            {
                bool bPNG = false;
                GfxLink aGfxLink = aGraphic.GetGfxLink();
                if (aGfxLink.GetType() == GfxLinkType::NativePng)
                {
                    bPNG = true;
                }
                else if (aGfxLink.GetType() == GfxLinkType::NativeJpg)
                {
                    bJPG = true;
                }
                if (bPNG || bJPG)
                {
                    aOStm.WriteBytes(aGfxLink.GetData(), aGfxLink.GetDataSize());
                    bCached = true;
                }
            }
        }
    }

    const BitmapEx* pBitmap = &rBmpEx;
    std::unique_ptr<BitmapEx> pNewBitmap;

    // for preview we generate downscaled images (1280x720 max)
    if (mbIsPreview)
    {
        Size aSize = rBmpEx.GetSizePixel();
        double fX = static_cast<double>(aSize.getWidth()) / 1280;
        double fY = static_cast<double>(aSize.getHeight()) / 720;
        double fFactor = fX > fY ? fX : fY;
        if (fFactor > 1.0)
        {
            aSize.setWidth(aSize.getWidth() / fFactor);
            aSize.setHeight(aSize.getHeight() / fFactor);
            pNewBitmap = std::make_unique<BitmapEx>(rBmpEx);
            pNewBitmap->Scale(aSize);
            pBitmap = pNewBitmap.get();
        }
    }

    if( !(bCached || GraphicConverter::Export( aOStm, *pBitmap, ConvertDataFormat::PNG ) == ERRCODE_NONE) )
        return;

    Point                    aPt;
    Size                     aSz;
    Sequence< sal_Int8 >     aSeq( static_cast<sal_Int8 const *>(aOStm.GetData()), aOStm.Tell() );
    OUStringBuffer aBuffer;
    if (bJPG)
    {
        aBuffer.append("data:image/jpeg;base64,");
    }
    else
    {
        aBuffer.append("data:image/png;base64,");
    }
    ::comphelper::Base64::encode( aBuffer, aSeq );

    ImplMap( rPt, aPt );
    ImplMap( rSz, aSz );

    mrExport.AddAttribute(aXMLAttrX, OUString::number(aPt.X()));
    mrExport.AddAttribute(aXMLAttrY, OUString::number(aPt.Y()));
    mrExport.AddAttribute(aXMLAttrWidth, OUString::number(aSz.Width()));
    mrExport.AddAttribute(aXMLAttrHeight, OUString::number(aSz.Height()));

    // If we have a media object (a video), export the video.
    // Also, use the image generated above as the video poster (thumbnail).
    SdrMediaObj* pMediaObj
        = pShape ? dynamic_cast<SdrMediaObj*>(SdrObject::getSdrObjectFromXShape(*pShape)) : nullptr;
    const bool embedVideo = (pMediaObj && !pMediaObj->getTempURL().isEmpty());

    if (!embedVideo)
    {
        // the image must be scaled to aSz in a non-uniform way
        mrExport.AddAttribute(u"preserveAspectRatio"_ustr, u"none"_ustr);

        mrExport.AddAttribute(aXMLAttrXLinkHRef, aBuffer.makeStringAndClear());

        SvXMLElementExport aElem(mrExport, u"image"_ustr, true, true);
    }
    else
    {
        // <foreignObject xmlns="http://www.w3.org/2000/svg" overflow="visible" width="499.6" height="374.33333333333337" x="705" y="333">
        //     <body xmlns="http://www.w3.org/1999/xhtml">
        //         <video controls="controls" width="499.6" height="374.33333333333337">
        //             <source src="file:///tmp/abcdef.mp4" type="video/mp4">
        //         </video>
        //     </body>
        // </foreignObject>
        mrExport.AddAttribute(u"xmlns"_ustr, u"http://www.w3.org/2000/svg"_ustr);
        mrExport.AddAttribute(u"overflow"_ustr, u"visible"_ustr);
        SvXMLElementExport aForeignObject(mrExport, u"foreignObject"_ustr, true, true);
        mrExport.AddAttribute(u"xmlns"_ustr, u"http://www.w3.org/1999/xhtml"_ustr);
        SvXMLElementExport aBody(mrExport, u"body"_ustr, true, true);

        mrExport.AddAttribute(aXMLAttrWidth, OUString::number(aSz.Width()));
        mrExport.AddAttribute(aXMLAttrHeight, OUString::number(aSz.Height()));
        mrExport.AddAttribute(u"autoplay"_ustr, u"autoplay"_ustr);
        mrExport.AddAttribute(u"controls"_ustr, u"controls"_ustr);
        mrExport.AddAttribute(u"loop"_ustr, u"loop"_ustr);
        mrExport.AddAttribute(u"preload"_ustr, u"auto"_ustr);
        mrExport.AddAttribute(u"poster"_ustr, aBuffer.makeStringAndClear());
        SvXMLElementExport aVideo(mrExport, u"video"_ustr, true, true);

        mrExport.AddAttribute(u"src"_ustr, pMediaObj->getTempURL());
        mrExport.AddAttribute(u"type"_ustr, u"video/mp4"_ustr); //FIXME: set mime type.
        SvXMLElementExport aSource(mrExport, u"source"_ustr, true, true);
    }
}


void SVGActionWriter::ImplWriteActions( const GDIMetaFile& rMtf,
                                        sal_uInt32 nWriteFlags,
                                        const OUString& aElementId,
                                        const Reference< css::drawing::XShape >* pxShape,
                                        const GDIMetaFile* pTextEmbeddedBitmapMtf )
{
    // need a counter for the actions written per shape to avoid double ID
    // generation
    sal_Int32 nEntryCount(0);

    bool bUseElementId = !aElementId.isEmpty();

#if OSL_DEBUG_LEVEL > 0
    bool bIsTextShape = false;
    if( !mrExport.IsUsePositionedCharacters() && pxShape
            && Reference< XText >( *pxShape, UNO_QUERY ).is() )
    {
        bIsTextShape = true;
    }
#endif
    mbIsPlaceholderShape = false;
    if( bUseElementId && ( aElementId == sPlaceholderTag ) )
    {
        mbIsPlaceholderShape = true;
        // since we utilize aElementId in an improper way we reset the boolean
        // control variable bUseElementId to false before to go on
        bUseElementId = false;
    }

    for( size_t nCurAction = 0, nCount = rMtf.GetActionSize(); nCurAction < nCount; nCurAction++ )
    {
        const MetaAction*    pAction = rMtf.GetAction( nCurAction );
        const MetaActionType nType = pAction->GetType();

#if OSL_DEBUG_LEVEL > 0
        if( bIsTextShape )
        {
            try
            {
                SvXMLElementExport aElem(mrExport, u"desc"_ustr, false, false);
                OUStringBuffer sType(OUString::number(static_cast<sal_uInt16>(nType)));
                if (pAction && (nType == MetaActionType::COMMENT))
                {
                    sType.append(": ");
                    const MetaCommentAction* pA = static_cast<const MetaCommentAction*>(pAction);
                    OString sComment = pA->GetComment();
                    if (!sComment.isEmpty())
                    {
                        sType.append(OStringToOUString(
                                        sComment, RTL_TEXTENCODING_UTF8));
                    }
                    if (sComment.equalsIgnoreAsciiCase("FIELD_SEQ_BEGIN"))
                    {
                        sal_uInt8 const*const pData = pA->GetData();
                        if (pData && (pA->GetDataSize()))
                        {
                            sal_uInt16 sz = static_cast<sal_uInt16>((pA->GetDataSize()) / 2);
                            if (sz)
                            {
                                sType.append(OUString::Concat("; ")
                                    + std::u16string_view(
                                        reinterpret_cast<sal_Unicode const*>(pData),
                                        sz));
                            }
                        }
                    }
                }
                if (sType.getLength())
                {
                    mrExport.GetDocHandler()->characters(
                            sType.makeStringAndClear());
                }
            }
            catch( ... )
            {
                const MetaCommentAction* pA = static_cast<const MetaCommentAction*>(pAction);
                SAL_WARN( "filter.svg", pA->GetComment() );
            }

        }
#endif
        switch( nType )
        {
            case MetaActionType::PIXEL:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaPixelAction* pA = static_cast<const MetaPixelAction*>(pAction);

                    maAttributeWriter.AddPaintAttr( pA->GetColor(), pA->GetColor() );
                    ImplWriteLine( pA->GetPoint(), pA->GetPoint(), &pA->GetColor() );
                }
            }
            break;

            case MetaActionType::POINT:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaPointAction* pA = static_cast<const MetaPointAction*>(pAction);

                    maAttributeWriter.AddPaintAttr( mpVDev->GetLineColor(), mpVDev->GetLineColor() );
                    ImplWriteLine( pA->GetPoint(), pA->GetPoint() );
                }
            }
            break;

            case MetaActionType::LINE:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaLineAction* pA = static_cast<const MetaLineAction*>(pAction);

                    maAttributeWriter.AddPaintAttr( mpVDev->GetLineColor(), mpVDev->GetLineColor() );
                    ImplWriteLine( pA->GetStartPoint(), pA->GetEndPoint() );
                }
            }
            break;

            case MetaActionType::RECT:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    maAttributeWriter.AddPaintAttr( mpVDev->GetLineColor(), mpVDev->GetFillColor() );
                    ImplWriteRect( static_cast<const MetaRectAction*>(pAction)->GetRect() );
                }
            }
            break;

            case MetaActionType::ROUNDRECT:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaRoundRectAction* pA = static_cast<const MetaRoundRectAction*>(pAction);

                    maAttributeWriter.AddPaintAttr( mpVDev->GetLineColor(), mpVDev->GetFillColor() );
                    ImplWriteRect( pA->GetRect(), pA->GetHorzRound(), pA->GetVertRound() );
                }
            }
            break;

            case MetaActionType::ELLIPSE:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaEllipseAction*    pA = static_cast<const MetaEllipseAction*>(pAction);
                    const tools::Rectangle&            rRect = pA->GetRect();

                    maAttributeWriter.AddPaintAttr( mpVDev->GetLineColor(), mpVDev->GetFillColor() );
                    ImplWriteEllipse( rRect.Center(), rRect.GetWidth() >> 1, rRect.GetHeight() >> 1 );
                }
            }
            break;

            case MetaActionType::ARC:
            case MetaActionType::PIE:
            case MetaActionType::CHORD:
            case MetaActionType::POLYGON:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    tools::Polygon aPoly;

                    switch( nType )
                    {
                        case MetaActionType::ARC:
                        {
                            const MetaArcAction* pA = static_cast<const MetaArcAction*>(pAction);
                            aPoly = tools::Polygon( pA->GetRect(), pA->GetStartPoint(), pA->GetEndPoint(), PolyStyle::Arc );
                        }
                        break;

                        case MetaActionType::PIE:
                        {
                            const MetaPieAction* pA = static_cast<const MetaPieAction*>(pAction);
                            aPoly = tools::Polygon( pA->GetRect(), pA->GetStartPoint(), pA->GetEndPoint(), PolyStyle::Pie );
                        }
                        break;

                        case MetaActionType::CHORD:
                        {
                            const MetaChordAction* pA = static_cast<const MetaChordAction*>(pAction);
                            aPoly = tools::Polygon( pA->GetRect(), pA->GetStartPoint(), pA->GetEndPoint(), PolyStyle::Chord );
                        }
                        break;

                        case MetaActionType::POLYGON:
                            aPoly = static_cast<const MetaPolygonAction*>(pAction)->GetPolygon();
                        break;
                        default: break;
                    }

                    if( aPoly.GetSize() )
                    {
                        maAttributeWriter.AddPaintAttr( mpVDev->GetLineColor(), mpVDev->GetFillColor() );
                        ImplWritePolyPolygon( tools::PolyPolygon(aPoly), false );
                    }
                }
            }
            break;

            case MetaActionType::POLYLINE:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaPolyLineAction* pA = static_cast<const MetaPolyLineAction*>(pAction);
                    const tools::Polygon& rPoly = pA->GetPolygon();

                    if( rPoly.GetSize() )
                    {
                        maAttributeWriter.AddPaintAttr( mpVDev->GetLineColor(), COL_TRANSPARENT );
                        ImplAddLineAttr( pA->GetLineInfo() );
                        ImplWritePolyPolygon( tools::PolyPolygon(rPoly), true );
                    }
                }
            }
            break;

            case MetaActionType::POLYPOLYGON:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaPolyPolygonAction*    pA = static_cast<const MetaPolyPolygonAction*>(pAction);
                    const tools::PolyPolygon&              rPolyPoly = pA->GetPolyPolygon();

                    if( rPolyPoly.Count() )
                    {
                        maAttributeWriter.AddPaintAttr( mpVDev->GetLineColor(), mpVDev->GetFillColor() );
                        ImplWritePolyPolygon( rPolyPoly, false );
                    }
                }
            }
            break;

            case MetaActionType::GRADIENT:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaGradientAction* pA = static_cast<const MetaGradientAction*>(pAction);
                    const tools::Polygon aRectPoly( pA->GetRect() );
                    const tools::PolyPolygon aRectPolyPoly( aRectPoly );

                    ImplWriteGradientEx( aRectPolyPoly, pA->GetGradient(), nWriteFlags, nullptr );
                }
            }
            break;

            case MetaActionType::GRADIENTEX:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaGradientExAction* pA = static_cast<const MetaGradientExAction*>(pAction);
                    ImplWriteGradientEx( pA->GetPolyPolygon(), pA->GetGradient(), nWriteFlags, nullptr );
                }
            }
            break;

            case MetaActionType::HATCH:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaHatchAction*  pA = static_cast<const MetaHatchAction*>(pAction);
                    ImplWritePattern( pA->GetPolyPolygon(), &pA->GetHatch(), nullptr, nWriteFlags );
                }
            }
            break;

            case MetaActionType::Transparent:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaTransparentAction*    pA = static_cast<const MetaTransparentAction*>(pAction);
                    const tools::PolyPolygon& rPolyPoly = pA->GetPolyPolygon();

                    if( rPolyPoly.Count() )
                    {
                        Color aNewLineColor( mpVDev->GetLineColor() ), aNewFillColor( mpVDev->GetFillColor() );

                        // tdf#149800 do not change transparency of fully transparent
                        // i.e. invisible line, because it makes it visible,
                        // resulting an extra line behind the normal shape line
                        if ( aNewLineColor.GetAlpha() > 0 )
                            aNewLineColor.SetAlpha( 255 - basegfx::fround<sal_uInt8>( pA->GetTransparence() * 2.55 ) );
                        aNewFillColor.SetAlpha( 255 - basegfx::fround<sal_uInt8>( pA->GetTransparence() * 2.55 ) );

                        maAttributeWriter.AddPaintAttr( aNewLineColor, aNewFillColor );
                        ImplWritePolyPolygon( rPolyPoly, false );
                    }
                }
            }
            break;

            case MetaActionType::FLOATTRANSPARENT:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaFloatTransparentAction*   pA = static_cast<const MetaFloatTransparentAction*>(pAction);
                    GDIMetaFile                         aTmpMtf( pA->GetGDIMetaFile() );
                    ImplWriteMask( aTmpMtf, pA->GetPoint(), pA->GetSize(),
                                   pA->GetGradient(), nWriteFlags, pA->getSVGTransparencyColorStops()  );
                }
            }
            break;

            case MetaActionType::EPS:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaEPSAction*    pA = static_cast<const MetaEPSAction*>(pAction);
                    const GDIMetaFile&      aGDIMetaFile( pA->GetSubstitute() );
                    bool                bFound = false;

                    for( sal_uInt32 k = 0, nCount2 = aGDIMetaFile.GetActionSize(); ( k < nCount2 ) && !bFound; ++k )
                    {
                        const MetaAction* pSubstAct = aGDIMetaFile.GetAction( k );

                        if( pSubstAct->GetType() == MetaActionType::BMPSCALE )
                        {
                            bFound = true;
                            const MetaBmpScaleAction* pBmpScaleAction = static_cast<const MetaBmpScaleAction*>(pSubstAct);
                            ImplWriteBmp( BitmapEx(pBmpScaleAction->GetBitmap()),
                                          pA->GetPoint(), pA->GetSize(),
                                          Point(), pBmpScaleAction->GetBitmap().GetSizePixel(), pxShape );
                        }
                    }
                }
            }
            break;

            case MetaActionType::COMMENT:
            {
                const MetaCommentAction*    pA = static_cast<const MetaCommentAction*>(pAction);

                if (pA->GetComment().equalsIgnoreAsciiCase("BGRAD_SEQ_BEGIN"))
                {
                    // detect and use the new BGRAD_SEQ_* metafile comment actions
                    const MetaGradientExAction* pGradAction(nullptr);
                    bool bDone(false);

                    while (!bDone && (++nCurAction < nCount))
                    {
                        pAction = rMtf.GetAction(nCurAction);

                        if (MetaActionType::GRADIENTEX == pAction->GetType())
                        {
                            // remember the 'paint' data action
                            pGradAction = static_cast<const MetaGradientExAction*>(pAction);
                        }
                        else if (MetaActionType::COMMENT == pAction->GetType()
                            && static_cast<const MetaCommentAction*>(pAction)->GetComment().equalsIgnoreAsciiCase("BGRAD_SEQ_END"))
                        {
                            // end action found
                            bDone = true;
                        }
                    }

                    if (nullptr != pGradAction)
                    {
                        // we have a complete actions sequence of BGRAD_SEQ_*, so we can now
                        // read the correct color stops here
                        basegfx::BColorStops aColorStops;
                        SvMemoryStream aMemStm(const_cast<sal_uInt8 *>(pA->GetData()), pA->GetDataSize(), StreamMode::READ);
                        VersionCompatRead aCompat(aMemStm);
                        sal_uInt16 nTmp(0);
                        double fOff, fR, fG, fB;
                        aMemStm.ReadUInt16( nTmp );

                        const size_t nMaxPossibleEntries = aMemStm.remainingSize() / 4 * sizeof(double);
                        if (nTmp > nMaxPossibleEntries)
                        {
                            SAL_WARN("filter.svg", "gradient record claims to have: " << nTmp << " entries, but only " << nMaxPossibleEntries << " possible, clamping");
                            nTmp = nMaxPossibleEntries;
                        }

                        for (sal_uInt16 a(0); a < nTmp; a++)
                        {
                            aMemStm.ReadDouble(fOff);
                            aMemStm.ReadDouble(fR);
                            aMemStm.ReadDouble(fG);
                            aMemStm.ReadDouble(fB);

                            aColorStops.emplace_back(fOff, basegfx::BColor(fR, fG, fB));
                        }

                        // export with real Color Stops
                        ImplWriteGradientEx(pGradAction->GetPolyPolygon(), pGradAction->GetGradient(), nWriteFlags, &aColorStops);
                    }
                }
                else if( ( pA->GetComment().equalsIgnoreAsciiCase("XGRAD_SEQ_BEGIN") ) &&
                    ( nWriteFlags & SVGWRITER_WRITE_FILL ) )
                {
                    const MetaGradientExAction* pGradAction = nullptr;
                    bool                    bDone = false;

                    while( !bDone && ( ++nCurAction < nCount ) )
                    {
                        pAction = rMtf.GetAction( nCurAction );

                        if( pAction->GetType() == MetaActionType::GRADIENTEX )
                            pGradAction = static_cast<const MetaGradientExAction*>(pAction);
                        else if( ( pAction->GetType() == MetaActionType::COMMENT ) &&
                                 ( static_cast<const MetaCommentAction*>( pAction )->GetComment().
                                        equalsIgnoreAsciiCase("XGRAD_SEQ_END") ) )
                        {
                            bDone = true;
                        }
                    }

                    if( pGradAction )
                        ImplWriteGradientEx( pGradAction->GetPolyPolygon(), pGradAction->GetGradient(), nWriteFlags, nullptr );
                }
                else if( ( pA->GetComment().equalsIgnoreAsciiCase("XPATHFILL_SEQ_BEGIN") ) &&
                         ( nWriteFlags & SVGWRITER_WRITE_FILL ) && !( nWriteFlags & SVGWRITER_NO_SHAPE_COMMENTS ) &&
                         pA->GetDataSize() )
                {
                    // write open shape in every case
                    if (mapCurShape)
                    {
                        ImplWriteShape( *mapCurShape );
                        mapCurShape.reset();
                    }

                    SvMemoryStream  aMemStm( const_cast<sal_uInt8 *>(pA->GetData()), pA->GetDataSize(), StreamMode::READ );
                    SvtGraphicFill  aFill;

                    ReadSvtGraphicFill( aMemStm, aFill );

                    bool bGradient = SvtGraphicFill::fillGradient == aFill.getFillType() &&
                                     ( SvtGraphicFill::GradientType::Linear == aFill.getGradientType() ||
                                       SvtGraphicFill::GradientType::Radial == aFill.getGradientType() );
                    bool bSkip = ( SvtGraphicFill::fillSolid == aFill.getFillType() || bGradient );

                    if( bSkip )
                    {
                        tools::PolyPolygon aShapePolyPoly;

                        aFill.getPath( aShapePolyPoly );

                        if( aShapePolyPoly.Count() )
                        {
                            mapCurShape.reset( new SVGShapeDescriptor );

                            if( bUseElementId )
                            {
                                mapCurShape->maId = aElementId + "_" + OUString::number(nEntryCount++);
                            }

                            mapCurShape->maShapePolyPoly = std::move(aShapePolyPoly);
                            mapCurShape->maShapeFillColor = aFill.getFillColor();
                            mapCurShape->maShapeFillColor.SetAlpha( 255 - basegfx::fround<sal_uInt8>( 255.0 * aFill.getTransparency() ) );

                            if( bGradient )
                            {
                                // step through following actions until the first Gradient/GradientEx action is found
                                while (!mapCurShape->moShapeGradient && bSkip
                                       && (++nCurAction < nCount))
                                {
                                    pAction = rMtf.GetAction( nCurAction );

                                    if( ( pAction->GetType() == MetaActionType::COMMENT ) &&
                                        ( static_cast<const MetaCommentAction*>(pAction)->GetComment().
                                               equalsIgnoreAsciiCase("XPATHFILL_SEQ_END") ) )
                                    {
                                        bSkip = false;
                                    }
                                    else if( pAction->GetType() == MetaActionType::GRADIENTEX )
                                    {
                                        mapCurShape->moShapeGradient.emplace(
                                            static_cast< const MetaGradientExAction* >( pAction )->GetGradient() );
                                    }
                                    else if( pAction->GetType() == MetaActionType::GRADIENT )
                                    {
                                        mapCurShape->moShapeGradient.emplace(
                                            static_cast< const MetaGradientAction* >( pAction )->GetGradient() );
                                    }
                                }
                            }
                        }
                        else
                            bSkip = false;
                    }

                    // skip rest of comment
                    while( bSkip && ( ++nCurAction < nCount ) )
                    {
                        pAction = rMtf.GetAction( nCurAction );

                        if( ( pAction->GetType() == MetaActionType::COMMENT ) &&
                                    ( static_cast<const MetaCommentAction*>( pAction )->GetComment().
                                            equalsIgnoreAsciiCase("XPATHFILL_SEQ_END") ) )
                        {
                            bSkip = false;
                        }
                    }
                }
                else if( ( pA->GetComment().equalsIgnoreAsciiCase("XPATHSTROKE_SEQ_BEGIN") ) &&
                         ( nWriteFlags & SVGWRITER_WRITE_FILL ) && !( nWriteFlags & SVGWRITER_NO_SHAPE_COMMENTS ) &&
                         pA->GetDataSize() )
                {
                    SvMemoryStream aMemStm( const_cast<sal_uInt8 *>(pA->GetData()), pA->GetDataSize(), StreamMode::READ );
                    SvtGraphicStroke aStroke;
                    tools::PolyPolygon aStartArrow, aEndArrow;

                    ReadSvtGraphicStroke( aMemStm, aStroke );
                    aStroke.getStartArrow( aStartArrow );
                    aStroke.getEndArrow( aEndArrow );

                    // Currently no support for strokes with start/end arrow(s)
                    // added that support
                    tools::Polygon aPoly;

                    aStroke.getPath(aPoly);

                    if (mapCurShape)
                    {
                        if(1 != mapCurShape->maShapePolyPoly.Count()
                            || !mapCurShape->maShapePolyPoly[0].IsEqual(aPoly))
                        {
                            // this path action is not covering the same path than the already existing
                            // fill polypolygon, so write out the fill polygon
                            ImplWriteShape( *mapCurShape );
                            mapCurShape.reset();
                        }
                    }

                    if (!mapCurShape)
                    {

                        mapCurShape.reset( new SVGShapeDescriptor );

                        if( bUseElementId )
                        {
                            mapCurShape->maId = aElementId + "_" + OUString::number(nEntryCount++);
                        }

                        mapCurShape->maShapePolyPoly = tools::PolyPolygon(aPoly);
                    }

                    mapCurShape->maShapeLineColor = mpVDev->GetLineColor();
                    mapCurShape->maShapeLineColor.SetAlpha( 255 - basegfx::fround<sal_uInt8>( aStroke.getTransparency() * 255.0 ) );
                    mapCurShape->mnStrokeWidth = basegfx::fround(aStroke.getStrokeWidth());
                    aStroke.getDashArray( mapCurShape->maDashArray );

                    // added support for LineJoin
                    switch(aStroke.getJoinType())
                    {
                        default: /* SvtGraphicStroke::joinMiter,  SvtGraphicStroke::joinNone */
                        {
                            mapCurShape->maLineJoin = basegfx::B2DLineJoin::Miter;
                            break;
                        }
                        case SvtGraphicStroke::joinRound:
                        {
                            mapCurShape->maLineJoin = basegfx::B2DLineJoin::Round;
                            break;
                        }
                        case SvtGraphicStroke::joinBevel:
                        {
                            mapCurShape->maLineJoin = basegfx::B2DLineJoin::Bevel;
                            break;
                        }
                    }

                    // added support for LineCap
                    switch(aStroke.getCapType())
                    {
                        default: /* SvtGraphicStroke::capButt */
                        {
                            mapCurShape->maLineCap = css::drawing::LineCap_BUTT;
                            break;
                        }
                        case SvtGraphicStroke::capRound:
                        {
                            mapCurShape->maLineCap = css::drawing::LineCap_ROUND;
                            break;
                        }
                        case SvtGraphicStroke::capSquare:
                        {
                            mapCurShape->maLineCap = css::drawing::LineCap_SQUARE;
                            break;
                        }
                    }

                    if (mapCurShape->maShapePolyPoly.Count() && (aStartArrow.Count() || aEndArrow.Count()))
                    {
                        ImplWriteShape( *mapCurShape );

                        mapCurShape->maShapeFillColor = mapCurShape->maShapeLineColor;
                        mapCurShape->maShapeLineColor = COL_TRANSPARENT;
                        mapCurShape->mnStrokeWidth = 0;
                        mapCurShape->maDashArray.clear();
                        mapCurShape->maLineJoin = basegfx::B2DLineJoin::Miter;
                        mapCurShape->maLineCap = css::drawing::LineCap_BUTT;

                        if(aStartArrow.Count())
                        {
                            mapCurShape->maShapePolyPoly = std::move(aStartArrow);

                            if( bUseElementId ) // #i124825# aElementId is optional, may be zero
                            {
                                mapCurShape->maId = aElementId + "_" + OUString::number(nEntryCount++);
                            }

                            ImplWriteShape( *mapCurShape );
                        }

                        if(aEndArrow.Count())
                        {
                            mapCurShape->maShapePolyPoly = std::move(aEndArrow);

                            if( bUseElementId ) // #i124825# aElementId is optional, may be zero
                            {
                                mapCurShape->maId = aElementId + "_" + OUString::number(nEntryCount++);
                            }

                            ImplWriteShape( *mapCurShape );
                        }

                        mapCurShape.reset();
                    }

                    // write open shape in every case
                    if (mapCurShape)
                    {
                        ImplWriteShape( *mapCurShape );
                        mapCurShape.reset();
                    }

                    // skip rest of comment
                    bool bSkip = true;

                    while( bSkip && ( ++nCurAction < nCount ) )
                    {
                        pAction = rMtf.GetAction( nCurAction );

                        if( ( pAction->GetType() == MetaActionType::COMMENT ) &&
                                    ( static_cast<const MetaCommentAction*>(pAction)->GetComment().
                                    equalsIgnoreAsciiCase("XPATHSTROKE_SEQ_END") ) )
                        {
                            bSkip = false;
                        }
                    }
                }
                else if( !mrExport.IsUsePositionedCharacters() && ( nWriteFlags & SVGWRITER_WRITE_TEXT ) )
                {
                    if( pA->GetComment().equalsIgnoreAsciiCase( "XTEXT_PAINTSHAPE_BEGIN" ) )
                    {
                        if( pxShape )
                        {
                            Reference< XText > xText( *pxShape, UNO_QUERY );
                            if( xText.is() )
                                maTextWriter.setTextShape( xText, pTextEmbeddedBitmapMtf );
                        }
                        maTextWriter.createParagraphEnumeration();
                        {
                            // nTextFound == -1 => no text found
                            // nTextFound ==  0 => no text found and end of text shape reached
                            // nTextFound ==  1 => text found!
                            sal_Int32 nTextFound = -1;
                            while( ( nTextFound < 0 ) && ( nCurAction < nCount ) )
                            {
                                nTextFound
                                    = maTextWriter.setTextPosition(rMtf, nCurAction, nWriteFlags);
                            }
                            // We found some text in the current text shape.
                            if( nTextFound > 0 )
                            {
                                maTextWriter.setTextProperties( rMtf, nCurAction );
                                maTextWriter.startTextShape();
                            }
                            // We reached the end of the current text shape
                            // without finding any text. So we need to go back
                            // by one action in order to handle the
                            // XTEXT_PAINTSHAPE_END action because on the next
                            // loop the nCurAction is incremented by one.
                            else
                            {
                                --nCurAction;
                            }
                        }
                    }
                    else if( pA->GetComment().equalsIgnoreAsciiCase( "XTEXT_PAINTSHAPE_END" ) )
                    {
                        maTextWriter.endTextShape();
                    }
                    else if( pA->GetComment().equalsIgnoreAsciiCase( "XTEXT_EOP" ) )
                    {
                        const MetaAction* pNextAction = rMtf.GetAction( nCurAction + 1 );
                        if( !( ( pNextAction->GetType() == MetaActionType::COMMENT ) &&
                               ( static_cast<const MetaCommentAction*>(pNextAction)->GetComment().equalsIgnoreAsciiCase("XTEXT_PAINTSHAPE_END") )  ))
                        {
                            // nTextFound == -1 => no text found and end of paragraph reached
                            // nTextFound ==  0 => no text found and end of text shape reached
                            // nTextFound ==  1 => text found!
                            sal_Int32 nTextFound = -1;
                            while( ( nTextFound < 0 ) && ( nCurAction < nCount ) )
                            {
                                nTextFound
                                    = maTextWriter.setTextPosition(rMtf, nCurAction, nWriteFlags);
                            }
                            // We found a paragraph with some text in the
                            // current text shape.
                            if( nTextFound > 0 )
                            {
                                maTextWriter.setTextProperties( rMtf, nCurAction );
                                maTextWriter.startTextParagraph();
                            }
                            // We reached the end of the current text shape
                            // without finding any text. So we need to go back
                            // by one action in order to handle the
                            // XTEXT_PAINTSHAPE_END action because on the next
                            // loop the nCurAction is incremented by one.
                            else
                            {
                                --nCurAction;
                            }

                        }
                    }
                    else if( pA->GetComment().equalsIgnoreAsciiCase( "XTEXT_EOL" ) )
                    {
                        const MetaAction* pNextAction = rMtf.GetAction( nCurAction + 1 );
                        if( !( ( pNextAction->GetType() == MetaActionType::COMMENT ) &&
                               ( static_cast<const MetaCommentAction*>(pNextAction)->GetComment().equalsIgnoreAsciiCase("XTEXT_EOP") ) ) )
                        {
                            // nTextFound == -2 => no text found and end of line reached
                            // nTextFound == -1 => no text found and end of paragraph reached
                            // nTextFound ==  1 => text found!
                            sal_Int32 nTextFound = -2;
                            while( ( nTextFound < -1 ) && ( nCurAction < nCount ) )
                            {
                                nTextFound
                                    = maTextWriter.setTextPosition(rMtf, nCurAction, nWriteFlags);
                            }
                            // We found a line with some text in the current
                            // paragraph.
                            if( nTextFound > 0 )
                            {
                                maTextWriter.startTextPosition();
                            }
                            // We reached the end of the current paragraph
                            // without finding any text. So we need to go back
                            // by one action in order to handle the XTEXT_EOP
                            // action because on the next loop the nCurAction is
                            // incremented by one.
                            else
                            {
                                --nCurAction;
                            }
                        }
                    }
                }
                else if( pA->GetComment().startsWithIgnoreAsciiCase( sTiledBackgroundTag ) )
                {
                    // In the tile case the background is rendered through a rectangle
                    // filled by exploiting an exported pattern element.
                    // Both the pattern and the rectangle are embedded in a <defs> element.
                    // The comment content has the following format: "SLIDE_BACKGROUND <background-id>"
                    const OString& sComment = pA->GetComment();
                    OUString sRefId = "#" + OUString::fromUtf8( o3tl::getToken(sComment, 1, ' ') );
                    mrExport.AddAttribute(aXMLAttrXLinkHRef, sRefId);

                    SvXMLElementExport aRefElem(mrExport, u"use"_ustr, true, true);
                }
            }
            break;

            case MetaActionType::BMP:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaBmpAction* pA = static_cast<const MetaBmpAction*>(pAction);

                    ImplWriteBmp( BitmapEx(pA->GetBitmap()),
                                  pA->GetPoint(), mpVDev->PixelToLogic( pA->GetBitmap().GetSizePixel() ),
                                  Point(), pA->GetBitmap().GetSizePixel(), pxShape );
                }
            }
            break;

            case MetaActionType::BMPSCALE:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaBmpScaleAction* pA = static_cast<const MetaBmpScaleAction*>(pAction);

                    // Bitmaps embedded into text shapes are collected and exported elsewhere.
                    if( maTextWriter.isTextShapeStarted() )
                    {
                        maTextWriter.writeBitmapPlaceholder( pA );
                    }
                    else
                    {
                        ImplWriteBmp( BitmapEx(pA->GetBitmap()),
                                      pA->GetPoint(), pA->GetSize(),
                                      Point(), pA->GetBitmap().GetSizePixel(), pxShape );
                    }
                }
            }
            break;

            case MetaActionType::BMPSCALEPART:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaBmpScalePartAction* pA = static_cast<const MetaBmpScalePartAction*>(pAction);

                    ImplWriteBmp( BitmapEx(pA->GetBitmap()),
                                  pA->GetDestPoint(), pA->GetDestSize(),
                                  pA->GetSrcPoint(), pA->GetSrcSize(), pxShape );
                }
            }
            break;

            case MetaActionType::BMPEX:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaBmpExAction*  pA = static_cast<const MetaBmpExAction*>(pAction);

                    ImplWriteBmp( pA->GetBitmapEx(),
                                  pA->GetPoint(), mpVDev->PixelToLogic( pA->GetBitmapEx().GetSizePixel() ),
                                  Point(), pA->GetBitmapEx().GetSizePixel(), pxShape );
                }
            }
            break;

            case MetaActionType::BMPEXSCALE:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaBmpExScaleAction* pA = static_cast<const MetaBmpExScaleAction*>(pAction);

                    // Bitmaps embedded into text shapes are collected and exported elsewhere.
                    if( maTextWriter.isTextShapeStarted() )
                    {
                        maTextWriter.writeBitmapPlaceholder( pA );
                    }
                    else
                    {
                        ImplWriteBmp( pA->GetBitmapEx(),
                                      pA->GetPoint(), pA->GetSize(),
                                      Point(), pA->GetBitmapEx().GetSizePixel(), pxShape );
                    }
                }
            }
            break;

            case MetaActionType::BMPEXSCALEPART:
            {
                if( nWriteFlags & SVGWRITER_WRITE_FILL )
                {
                    const MetaBmpExScalePartAction* pA = static_cast<const MetaBmpExScalePartAction*>(pAction);

                    ImplWriteBmp( pA->GetBitmapEx(),
                                  pA->GetDestPoint(), pA->GetDestSize(),
                                  pA->GetSrcPoint(), pA->GetSrcSize(), pxShape );
                }
            }
            break;

            case MetaActionType::TEXT:
            {
                if( nWriteFlags & SVGWRITER_WRITE_TEXT )
                {
                    const MetaTextAction*   pA = static_cast<const MetaTextAction*>(pAction);
                    sal_Int32               aLength = std::min( pA->GetText().getLength(), pA->GetLen() );
                    const OUString          aText = pA->GetText().copy( pA->GetIndex(), aLength );

                    if( !aText.isEmpty() )
                    {
                        if( mrExport.IsUsePositionedCharacters() )
                        {
                            vcl::Font aFont = ImplSetCorrectFontHeight();
                            maAttributeWriter.SetFontAttr( aFont );
                            ImplWriteText( pA->GetPoint(), aText, {}, 0 );
                        }
                        else
                        {
                            maTextWriter.writeTextPortion( pA->GetPoint(), aText );
                        }
                    }
                }
            }
            break;

            case MetaActionType::TEXTRECT:
            {
                if( nWriteFlags & SVGWRITER_WRITE_TEXT )
                {
                    const MetaTextRectAction* pA = static_cast<const MetaTextRectAction*>(pAction);

                    if (!pA->GetText().isEmpty())
                    {
                        if( mrExport.IsUsePositionedCharacters() )
                        {
                            vcl::Font aFont = ImplSetCorrectFontHeight();
                            maAttributeWriter.SetFontAttr( aFont );
                            ImplWriteText( pA->GetRect().TopLeft(), pA->GetText(), {}, 0 );
                        }
                        maTextWriter.writeTextPortion( pA->GetRect().TopLeft(), pA->GetText() );
                    }
                }
            }
            break;

            case MetaActionType::TEXTARRAY:
            {
                if( nWriteFlags & SVGWRITER_WRITE_TEXT )
                {
                    const MetaTextArrayAction*  pA = static_cast<const MetaTextArrayAction*>(pAction);
                    sal_Int32                   aLength = std::min( pA->GetText().getLength(), pA->GetLen() );
                    const OUString              aText = pA->GetText().copy( pA->GetIndex(), aLength );

                    if( !aText.isEmpty() )
                    {
                        if( mrExport.IsUsePositionedCharacters() )
                        {
                            vcl::Font aFont = ImplSetCorrectFontHeight();
                            maAttributeWriter.SetFontAttr( aFont );
                            ImplWriteText( pA->GetPoint(), aText, pA->GetDXArray(), 0 );
                        }
                        else
                        {
                            tools::Long nWidth = 0;
                            if (pA->GetDXArray().size() >= o3tl::make_unsigned(aText.getLength()))
                                nWidth = std::round(pA->GetDXArray()[aText.getLength() - 1]);
                            maTextWriter.writeTextPortion(pA->GetPoint(), aText, nWidth);
                        }
                    }
                }
            }
            break;

            case MetaActionType::STRETCHTEXT:
            {
                if( nWriteFlags & SVGWRITER_WRITE_TEXT )
                {
                    const MetaStretchTextAction*    pA = static_cast<const MetaStretchTextAction*>(pAction);
                    sal_Int32                       aLength = std::min( pA->GetText().getLength(), pA->GetLen() );
                    const OUString                  aText = pA->GetText().copy( pA->GetIndex(), aLength );

                    if( !aText.isEmpty() )
                    {
                        if( mrExport.IsUsePositionedCharacters() )
                        {
                            vcl::Font aFont = ImplSetCorrectFontHeight();
                            maAttributeWriter.SetFontAttr( aFont );
                            ImplWriteText( pA->GetPoint(), aText, {}, pA->GetWidth() );
                        }
                        else
                        {
                            maTextWriter.writeTextPortion( pA->GetPoint(), aText );
                        }
                    }
                }
            }
            break;

            case MetaActionType::CLIPREGION:
            case MetaActionType::ISECTRECTCLIPREGION:
            case MetaActionType::ISECTREGIONCLIPREGION:
            case MetaActionType::MOVECLIPREGION:
            {
                const_cast<MetaAction*>(pAction)->Execute( mpVDev );
                const vcl::Region aClipRegion = mpVDev->GetActiveClipRegion();
                ImplWriteClipPath( aClipRegion.GetAsPolyPolygon() );
            }
            break;

            case MetaActionType::PUSH:
            {
                const MetaPushAction*  pA = static_cast<const MetaPushAction*>(pAction);
                vcl::PushFlags mnFlags = pA->GetFlags();

                const_cast<MetaAction*>(pAction)->Execute( mpVDev );

                maContextHandler.pushState( mnFlags );
            }
            break;

            case MetaActionType::POP:
            {
                const_cast<MetaAction*>(pAction)->Execute( mpVDev );

                vcl::PushFlags mnFlags = maContextHandler.getPushFlags();

                maContextHandler.popState();

                if( mnFlags & vcl::PushFlags::CLIPREGION )
                {
                    ImplEndClipRegion();
                    ImplStartClipRegion( mrCurrentState.nRegionClipPathId );
                }
            }
            break;

            case MetaActionType::REFPOINT:
            case MetaActionType::MAPMODE:
            case MetaActionType::LINECOLOR:
            case MetaActionType::FILLCOLOR:
            case MetaActionType::TEXTLINECOLOR:
            case MetaActionType::TEXTFILLCOLOR:
            case MetaActionType::TEXTCOLOR:
            case MetaActionType::TEXTALIGN:
            case MetaActionType::FONT:
            case MetaActionType::LAYOUTMODE:
            {
                const_cast<MetaAction*>(pAction)->Execute( mpVDev );
            }
            break;

            case MetaActionType::RASTEROP:
            case MetaActionType::MASK:
            case MetaActionType::MASKSCALE:
            case MetaActionType::MASKSCALEPART:
            case MetaActionType::WALLPAPER:
            case MetaActionType::TEXTLINE:
            case MetaActionType::TEXTLANGUAGE:
            {
                // !!! >>> we don't want to support these actions
            }
            break;

            default:
                SAL_WARN("filter.svg", "SVGActionWriter::ImplWriteActions: unsupported MetaAction # "  << sal_Int32(nType));
            break;
        }
    }
}


vcl::Font SVGActionWriter::ImplSetCorrectFontHeight() const
{
    vcl::Font aFont( mpVDev->GetFont() );
    Size      aSz;

    ImplMap( Size( 0, aFont.GetFontHeight() ), aSz );

    aFont.SetFontHeight( aSz.Height() );

    return aFont;
}


void SVGActionWriter::WriteMetaFile( const Point& rPos100thmm,
                                     const Size& rSize100thmm,
                                     const GDIMetaFile& rMtf,
                                     sal_uInt32 nWriteFlags,
                                     const OUString& aElementId,
                                     const Reference< css::drawing::XShape >* pXShape,
                                     const GDIMetaFile* pTextEmbeddedBitmapMtf )
{
    MapMode     aMapMode( rMtf.GetPrefMapMode() );
    Size        aPrefSize( rMtf.GetPrefSize() );
    Fraction    aFractionX( aMapMode.GetScaleX() );
    Fraction    aFractionY( aMapMode.GetScaleY() );

    mpVDev->Push();

    Size aSize( OutputDevice::LogicToLogic(rSize100thmm, MapMode(MapUnit::Map100thMM), aMapMode) );
    aFractionX *= Fraction( aSize.Width(), aPrefSize.Width() );
    aMapMode.SetScaleX( aFractionX );
    aFractionY *= Fraction( aSize.Height(), aPrefSize.Height() );
    aMapMode.SetScaleY( aFractionY );

    Point aOffset( OutputDevice::LogicToLogic(rPos100thmm, MapMode(MapUnit::Map100thMM), aMapMode ) );
    aOffset += aMapMode.GetOrigin();
    aMapMode.SetOrigin( aOffset );

    mpVDev->SetMapMode( aMapMode );

    mapCurShape.reset();

    ImplWriteActions( rMtf, nWriteFlags, aElementId, pXShape, pTextEmbeddedBitmapMtf );
    maTextWriter.endTextParagraph();
    ImplEndClipRegion();

    // draw open shape that doesn't have a border
    if (mapCurShape)
    {
        ImplWriteShape( *mapCurShape );
        mapCurShape.reset();
    }

    mpVDev->Pop();
}


SVGWriter::SVGWriter( const Sequence<Any>& args, const Reference< XComponentContext >& rxCtx )
    : mxContext(rxCtx)
{
    if(args.getLength()==1)
        args[0]>>=maFilterData;
}


SVGWriter::~SVGWriter()
{
}


void SAL_CALL SVGWriter::write( const Reference<XDocumentHandler>& rxDocHandler,
                                const Sequence<sal_Int8>& rMtfSeq )
{
    SvMemoryStream  aMemStm( const_cast<sal_Int8 *>(rMtfSeq.getConstArray()), rMtfSeq.getLength(), StreamMode::READ );
    GDIMetaFile     aMtf;

    SvmReader aReader( aMemStm );
    aReader.Read( aMtf );

    rtl::Reference<SVGExport> pWriter(new SVGExport( mxContext, rxDocHandler, maFilterData ));
    pWriter->writeMtf( aMtf );
}

//  XServiceInfo
sal_Bool SVGWriter::supportsService(const OUString& sServiceName)
{
    return cppu::supportsService(this, sServiceName);
}
OUString SVGWriter::getImplementationName()
{
    return u"com.sun.star.comp.Draw.SVGWriter"_ustr;
}
css::uno::Sequence< OUString > SVGWriter::getSupportedServiceNames()
{
    return { u"com.sun.star.svg.SVGWriter"_ustr };
}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
filter_SVGWriter_get_implementation(
    css::uno::XComponentContext* context, css::uno::Sequence<css::uno::Any> const& args)
{
    return cppu::acquire(new SVGWriter(args, context));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
