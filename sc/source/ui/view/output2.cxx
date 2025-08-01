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

#include <scitems.hxx>
#include <editeng/eeitem.hxx>

#include <editeng/adjustitem.hxx>
#include <svx/algitem.hxx>
#include <editeng/brushitem.hxx>
#include <svtools/colorcfg.hxx>
#include <editeng/colritem.hxx>
#include <editeng/charreliefitem.hxx>
#include <editeng/crossedoutitem.hxx>
#include <editeng/contouritem.hxx>
#include <editeng/editobj.hxx>
#include <editeng/editstat.hxx>
#include <editeng/emphasismarkitem.hxx>
#include <editeng/fhgtitem.hxx>
#include <editeng/forbiddenruleitem.hxx>
#include <editeng/frmdiritem.hxx>
#include <editeng/justifyitem.hxx>
#include <svx/rotmodit.hxx>
#include <editeng/udlnitem.hxx>
#include <editeng/unolingu.hxx>
#include <editeng/fontitem.hxx>
#include <editeng/postitem.hxx>
#include <editeng/shdditem.hxx>
#include <editeng/wghtitem.hxx>
#include <editeng/wrlmitem.hxx>
#include <formula/errorcodes.hxx>
#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>
#include <svl/zformat.hxx>
#include <vcl/kernarray.hxx>
#include <vcl/svapp.hxx>
#include <vcl/metric.hxx>
#include <vcl/outdev.hxx>
#include <vcl/pdfextoutdevdata.hxx>
#include <vcl/settings.hxx>
#include <vcl/glyphitem.hxx>
#include <vcl/glyphitemcache.hxx>
#include <sal/log.hxx>
#include <unotools/charclass.hxx>
#include <osl/diagnose.h>

#include <output.hxx>
#include <document.hxx>
#include <formulacell.hxx>
#include <attrib.hxx>
#include <patattr.hxx>
#include <cellform.hxx>
#include <editutil.hxx>
#include <progress.hxx>
#include <scmod.hxx>
#include <fillinfo.hxx>
#include <stlsheet.hxx>
#include <spellcheckcontext.hxx>
#include <scopetools.hxx>
#include <tabvwsh.hxx>

#include <com/sun/star/i18n/DirectionProperty.hpp>
#include <comphelper/scopeguard.hxx>
#include <comphelper/string.hxx>

#include <memory>
#include <vector>

#include <math.h>

using namespace com::sun::star;

//! Merge Autofilter width with column.cxx
#define DROPDOWN_BITMAP_SIZE        18

#define DRAWTEXT_MAX    32767

const sal_uInt16 SC_SHRINKAGAIN_MAX = 7;
constexpr auto HMM_PER_TWIPS = o3tl::convert(1.0, o3tl::Length::twip, o3tl::Length::mm100);

class ScDrawStringsVars
{
    ScOutputData*       pOutput;                // connection

    const ScPatternAttr* pPattern;              // attribute
    const SfxItemSet*   pCondSet;               // from conditional formatting

    vcl::Font           aFont;                  // created from attributes
    FontMetric          aMetric;
    tools::Long                nAscentPixel;           // always pixels
    SvxCellOrientation  eAttrOrient;
    SvxCellHorJustify   eAttrHorJust;
    SvxCellVerJustify   eAttrVerJust;
    SvxCellJustifyMethod eAttrHorJustMethod;
    const SvxMarginItem* pMargin;
    sal_uInt16          nIndent;
    bool                bRotated;

    OUString            aString;                // contents
    Size                aTextSize;
    tools::Long                nOriginalWidth;
    tools::Long                nMaxDigitWidth;
    tools::Long                nSignWidth;
    tools::Long                nDotWidth;
    tools::Long                nExpWidth;

    ScRefCellValue      maLastCell;
    sal_uLong           nValueFormat;
    bool                bLineBreak;
    bool                bRepeat;
    bool                bShrink;

    bool                bPixelToLogic;
    bool                bCellContrast;

    Color               aBackConfigColor;       // used for ScPatternAttr::GetFont calls
    Color               aTextConfigColor;
    sal_Int32           nRepeatPos;
    sal_Unicode         nRepeatChar;

public:
                ScDrawStringsVars(ScOutputData* pData, bool bPTL);

                //  SetPattern = ex-SetVars
                //  SetPatternSimple: without Font

    void SetPattern(
        const ScPatternAttr* pNew, const SfxItemSet* pSet, const ScRefCellValue& rCell,
        SvtScriptType nScript );

    void        SetPatternSimple( const ScPatternAttr* pNew, const SfxItemSet* pSet );

    bool SetText( const ScRefCellValue& rCell );   // TRUE -> drop pOldPattern
    void        SetHashText();
    bool SetTextToWidthOrHash( ScRefCellValue& rCell, tools::Long nWidth );
    void        SetAutoText( const OUString& rAutoText );

    SvxCellOrientation      GetOrient() const        { return eAttrOrient; }
    SvxCellHorJustify       GetHorJust() const       { return eAttrHorJust; }
    SvxCellVerJustify       GetVerJust() const       { return eAttrVerJust; }
    SvxCellJustifyMethod    GetHorJustMethod() const { return eAttrHorJustMethod; }
    const SvxMarginItem*    GetMargin() const        { return pMargin; }

    sal_uInt16              GetLeftTotal() const     { return pMargin->GetLeftMargin() + nIndent; }
    sal_uInt16              GetRightTotal() const    { return pMargin->GetRightMargin() + nIndent; }

    const OUString&         GetString() const        { return aString; }
    const Size&             GetTextSize() const      { return aTextSize; }
    tools::Long                    GetOriginalWidth() const { return nOriginalWidth; }
    tools::Long             GetFmtTextWidth(const OUString& rString);

    // Get the effective number format, including formula result types.
    // This assumes that a formula cell has already been calculated.
    sal_uLong GetResultValueFormat() const { return nValueFormat;}

    bool    GetLineBreak() const                    { return bLineBreak; }
    bool    IsRepeat() const                        { return bRepeat; }
    bool    IsShrink() const                        { return bShrink; }
    void        RepeatToFill( tools::Long nColWidth );

    tools::Long    GetAscent() const   { return nAscentPixel; }
    bool    IsRotated() const   { return bRotated; }

    void    SetShrinkScale( tools::Long nScale, SvtScriptType nScript );

    bool    HasCondHeight() const   { return pCondSet && SfxItemState::SET ==
                                        pCondSet->GetItemState( ATTR_FONT_HEIGHT ); }

    bool    HasEditCharacters() const;

    // ScOutputData::LayoutStrings() usually triggers a number of calls that require
    // to lay out the text, which is relatively slow, so cache that operation.
    const SalLayoutGlyphs*  GetLayoutGlyphs(const OUString& rString) const
    {
        return SalLayoutGlyphsCache::self()->GetLayoutGlyphs(pOutput->pFmtDevice, rString);
    }

private:
    tools::Long        GetMaxDigitWidth();     // in logic units
    tools::Long        GetSignWidth();
    tools::Long        GetDotWidth();
    tools::Long        GetExpWidth();
    void        TextChanged();
};

ScDrawStringsVars::ScDrawStringsVars(ScOutputData* pData, bool bPTL) :
    pOutput     ( pData ),
    pPattern    ( nullptr ),
    pCondSet    ( nullptr ),
    nAscentPixel(0),
    eAttrOrient ( SvxCellOrientation::Standard ),
    eAttrHorJust( SvxCellHorJustify::Standard ),
    eAttrVerJust( SvxCellVerJustify::Bottom ),
    eAttrHorJustMethod( SvxCellJustifyMethod::Auto ),
    pMargin     ( nullptr ),
    nIndent     ( 0 ),
    bRotated    ( false ),
    nOriginalWidth( 0 ),
    nMaxDigitWidth( 0 ),
    nSignWidth( 0 ),
    nDotWidth( 0 ),
    nExpWidth( 0 ),
    nValueFormat( 0 ),
    bLineBreak  ( false ),
    bRepeat     ( false ),
    bShrink     ( false ),
    bPixelToLogic( bPTL ),
    nRepeatPos( -1 ),
    nRepeatChar( 0x0 )
{
    bCellContrast = pOutput->mbUseStyleColor &&
            Application::GetSettings().GetStyleSettings().GetHighContrastMode();

    const svtools::ColorConfig& rColorConfig = ScModule::get()->GetColorConfig();
    aBackConfigColor = rColorConfig.GetColorValue(svtools::DOCCOLOR).nColor;
    aTextConfigColor = rColorConfig.GetColorValue(svtools::FONTCOLOR).nColor;
}

void ScDrawStringsVars::SetShrinkScale( tools::Long nScale, SvtScriptType nScript )
{
    // text remains valid, size is updated

    OutputDevice* pDev = pOutput->mpDev;
    OutputDevice* pRefDevice = pOutput->mpRefDevice;
    OutputDevice* pFmtDevice = pOutput->pFmtDevice;

    // call GetFont with a modified fraction, use only the height

    Fraction aFraction( nScale, 100 );
    if ( !bPixelToLogic )
        aFraction *= pOutput->maZoomY;
    vcl::Font aTmpFont;
    pPattern->fillFontOnly(aTmpFont, pFmtDevice, &aFraction, pCondSet, nScript);
    // only need font height
    tools::Long nNewHeight = aTmpFont.GetFontHeight();
    if ( nNewHeight > 0 )
        aFont.SetFontHeight( nNewHeight );

    // set font and dependent variables as in SetPattern

    pDev->SetFont( aFont );
    if ( pFmtDevice != pDev )
        pFmtDevice->SetFont( aFont );

    aMetric = pFmtDevice->GetFontMetric();
    if ( pFmtDevice->GetOutDevType() == OUTDEV_PRINTER && aMetric.GetInternalLeading() == 0 )
    {
        OutputDevice* pDefaultDev = Application::GetDefaultDevice();
        MapMode aOld = pDefaultDev->GetMapMode();
        pDefaultDev->SetMapMode( pFmtDevice->GetMapMode() );
        aMetric = pDefaultDev->GetFontMetric( aFont );
        pDefaultDev->SetMapMode( aOld );
    }

    nAscentPixel = aMetric.GetAscent();
    if ( bPixelToLogic )
        nAscentPixel = pRefDevice->LogicToPixel( Size( 0, nAscentPixel ) ).Height();

    SetAutoText( aString );     // same text again, to get text size
}

namespace {

template<typename ItemType, typename EnumType>
EnumType lcl_GetValue(const ScPatternAttr& rPattern, sal_uInt16 nWhich, const SfxItemSet* pCondSet)
{
    const ItemType& rItem = static_cast<const ItemType&>(rPattern.GetItem(nWhich, pCondSet));
    return static_cast<EnumType>(rItem.GetValue());
}

bool lcl_GetBoolValue(const ScPatternAttr& rPattern, sal_uInt16 nWhich, const SfxItemSet* pCondSet)
{
    return lcl_GetValue<SfxBoolItem, bool>(rPattern, nWhich, pCondSet);
}

}

static bool lcl_isNumberFormatText(const ScDocument* pDoc, SCCOL nCellX, SCROW nCellY, SCTAB mnTab )
{
    sal_uInt32 nCurrentNumberFormat = pDoc->GetNumberFormat( nCellX, nCellY, mnTab );
    SvNumberFormatter* pNumberFormatter = pDoc->GetFormatTable();
    return pNumberFormatter->GetType( nCurrentNumberFormat ) == SvNumFormatType::TEXT;
}

void ScDrawStringsVars::SetPattern(
    const ScPatternAttr* pNew, const SfxItemSet* pSet, const ScRefCellValue& rCell,
    SvtScriptType nScript )
{
    nMaxDigitWidth = 0;
    nSignWidth     = 0;
    nDotWidth      = 0;
    nExpWidth      = 0;

    pPattern = pNew;
    pCondSet = pSet;

    // evaluate pPattern

    OutputDevice* pDev = pOutput->mpDev;
    OutputDevice* pRefDevice = pOutput->mpRefDevice;
    OutputDevice* pFmtDevice = pOutput->pFmtDevice;

    // font

    ScAutoFontColorMode eColorMode;
    if ( pOutput->mbUseStyleColor )
    {
        if ( pOutput->mbForceAutoColor )
            eColorMode = bCellContrast ? ScAutoFontColorMode::IgnoreAll : ScAutoFontColorMode::IgnoreFont;
        else
            eColorMode = bCellContrast ? ScAutoFontColorMode::IgnoreBack : ScAutoFontColorMode::Display;
    }
    else
        eColorMode = ScAutoFontColorMode::Print;

    if (bPixelToLogic)
        pPattern->fillFont(aFont, eColorMode, pFmtDevice, nullptr, pCondSet, nScript, &aBackConfigColor, &aTextConfigColor);
    else
        pPattern->fillFont(aFont, eColorMode, pFmtDevice, &pOutput->maZoomY, pCondSet, nScript, &aBackConfigColor, &aTextConfigColor );

    aFont.SetAlignment(ALIGN_BASELINE);

    // orientation

    eAttrOrient = pPattern->GetCellOrientation( pCondSet );

    //  alignment

    eAttrHorJust = pPattern->GetItem( ATTR_HOR_JUSTIFY, pCondSet ).GetValue();

    eAttrVerJust = pPattern->GetItem( ATTR_VER_JUSTIFY, pCondSet ).GetValue();
    if ( eAttrVerJust == SvxCellVerJustify::Standard )
        eAttrVerJust = SvxCellVerJustify::Bottom;

    // justification method

    eAttrHorJustMethod = lcl_GetValue<SvxJustifyMethodItem, SvxCellJustifyMethod>(*pPattern, ATTR_HOR_JUSTIFY_METHOD, pCondSet);

    //  line break

    bLineBreak = pPattern->GetItem( ATTR_LINEBREAK, pCondSet ).GetValue();

    //  handle "repeat" alignment

    bRepeat = ( eAttrHorJust == SvxCellHorJustify::Repeat );
    if ( bRepeat )
    {
        // "repeat" disables rotation (before constructing the font)
        eAttrOrient = SvxCellOrientation::Standard;

        // #i31843# "repeat" with "line breaks" is treated as default alignment (but rotation is still disabled)
        if ( bLineBreak )
            eAttrHorJust = SvxCellHorJustify::Standard;
    }

    sal_Int16 nRot;
    switch (eAttrOrient)
    {
        case SvxCellOrientation::Standard:
            nRot = 0;
            bRotated = pPattern->GetItem( ATTR_ROTATE_VALUE, pCondSet ).GetValue() != 0_deg100 &&
                       !bRepeat;
            break;
        case SvxCellOrientation::Stacked:
            nRot = 0;
            bRotated = false;
            break;
        case SvxCellOrientation::TopBottom:
            nRot = 2700;
            bRotated = false;
            break;
        case SvxCellOrientation::BottomUp:
            nRot = 900;
            bRotated = false;
            break;
        default:
            OSL_FAIL("Invalid SvxCellOrientation value");
            nRot = 0;
            bRotated = false;
            break;
    }
    aFont.SetOrientation( Degree10(nRot) );

    // syntax mode

    if (pOutput->mbSyntaxMode)
        pOutput->SetSyntaxColor(&aFont, rCell);

    // There is no cell attribute for kerning, default is kerning OFF, all
    // kerning is stored at an EditText object that is drawn using EditEngine.
    // See also matching kerning cases in ScColumn::GetNeededSize and
    // ScColumn::GetOptimalColWidth.
    aFont.SetKerning(FontKerning::NONE);

    pDev->SetFont( aFont );
    if ( pFmtDevice != pDev )
        pFmtDevice->SetFont( aFont );

    aMetric = pFmtDevice->GetFontMetric();

    // if there is the leading 0 on a printer device, we have problems
    // -> take metric from the screen (as for EditEngine!)
    if ( pFmtDevice->GetOutDevType() == OUTDEV_PRINTER && aMetric.GetInternalLeading() == 0 )
    {
        OutputDevice* pDefaultDev = Application::GetDefaultDevice();
        MapMode aOld = pDefaultDev->GetMapMode();
        pDefaultDev->SetMapMode( pFmtDevice->GetMapMode() );
        aMetric = pDefaultDev->GetFontMetric( aFont );
        pDefaultDev->SetMapMode( aOld );
    }

    nAscentPixel = aMetric.GetAscent();
    if ( bPixelToLogic )
        nAscentPixel = pRefDevice->LogicToPixel( Size( 0, nAscentPixel ) ).Height();

    Color aULineColor( pPattern->GetItem( ATTR_FONT_UNDERLINE, pCondSet ).GetColor() );
    pDev->SetTextLineColor( aULineColor );

    Color aOLineColor( pPattern->GetItem( ATTR_FONT_OVERLINE, pCondSet ).GetColor() );
    pDev->SetOverlineColor( aOLineColor );

    // number format

    nValueFormat = pPattern->GetNumberFormat( pOutput->mpDoc->GetFormatTable(), pCondSet );

    // margins
    pMargin = &pPattern->GetItem( ATTR_MARGIN, pCondSet );
    if ( eAttrHorJust == SvxCellHorJustify::Left || eAttrHorJust == SvxCellHorJustify::Right )
        nIndent = pPattern->GetItem( ATTR_INDENT, pCondSet ).GetValue();
    else
        nIndent = 0;

    // "Shrink to fit"

    bShrink = pPattern->GetItem( ATTR_SHRINKTOFIT, pCondSet ).GetValue();

    // at least the text size needs to be retrieved again
    //! differentiate and do not get the text again from the number format?
    maLastCell.clear();
}

void ScDrawStringsVars::SetPatternSimple( const ScPatternAttr* pNew, const SfxItemSet* pSet )
{
    nMaxDigitWidth = 0;
    nSignWidth     = 0;
    nDotWidth      = 0;
    nExpWidth      = 0;

    // Is called, when the font variables do not change (!StringDiffer)

    pPattern = pNew;
    pCondSet = pSet;        //! is this needed ???

    // number format

    sal_uLong nOld = nValueFormat;
    nValueFormat = pPattern->GetNumberFormat( pOutput->mpDoc->GetFormatTable(), pCondSet );

    if (nValueFormat != nOld)
        maLastCell.clear();           // always reformat

    // margins

    pMargin = &pPattern->GetItem( ATTR_MARGIN, pCondSet );

    if ( eAttrHorJust == SvxCellHorJustify::Left )
        nIndent = pPattern->GetItem( ATTR_INDENT, pCondSet ).GetValue();
    else
        nIndent = 0;

    // "Shrink to fit"

    bShrink = pPattern->GetItem( ATTR_SHRINKTOFIT, pCondSet ).GetValue();
}

static bool SameValue( const ScRefCellValue& rCell, const ScRefCellValue& rOldCell )
{
    return rOldCell.getType() == CELLTYPE_VALUE && rCell.getType() == CELLTYPE_VALUE &&
        rCell.getDouble() == rOldCell.getDouble();
}

bool ScDrawStringsVars::SetText( const ScRefCellValue& rCell )
{
    bool bChanged = false;

    if (!rCell.isEmpty())
    {
        if (!SameValue(rCell, maLastCell))
        {
            maLastCell = rCell;          // store cell

            const Color* pColor;
            sal_uLong nFormat = nValueFormat;
            aString = ScCellFormat::GetString( rCell,
                                     nFormat, &pColor,
                                     nullptr,
                                     *pOutput->mpDoc,
                                     pOutput->mbShowNullValues,
                                     pOutput->mbShowFormulas,
                                     true );
            if ( nFormat )
            {
                nRepeatPos = aString.indexOf( 0x1B );
                if ( nRepeatPos != -1 )
                {
                    if (nRepeatPos + 1 == aString.getLength())
                        nRepeatPos = -1;
                    else
                    {
                        nRepeatChar = aString[ nRepeatPos + 1 ];
                        // delete placeholder and char to repeat
                        aString = aString.replaceAt( nRepeatPos, 2, u"" );
                        // Do not cache/reuse a repeat-filled string, column
                        // widths or fonts or sizes may differ.
                        maLastCell.clear();
                    }
                }
            }
            else
            {
                nRepeatPos = -1;
                nRepeatChar = 0x0;
            }
            if (aString.getLength() > DRAWTEXT_MAX)
                aString = aString.copy(0, DRAWTEXT_MAX);

            if ( pColor && !pOutput->mbSyntaxMode && !( pOutput->mbUseStyleColor && pOutput->mbForceAutoColor ) )
            {
                OutputDevice* pDev = pOutput->mpDev;
                aFont.SetColor(*pColor);
                pDev->SetFont( aFont );   // only for output
                bChanged = true;
                maLastCell.clear();       // next time return here again
            }

            TextChanged();
        }
        // otherwise keep string/size
    }
    else
    {
        aString.clear();
        maLastCell.clear();
        aTextSize = Size(0,0);
        nOriginalWidth = 0;
    }

    return bChanged;
}

void ScDrawStringsVars::SetHashText()
{
    SetAutoText(u"###"_ustr);
}

void ScDrawStringsVars::RepeatToFill( tools::Long nColWidth )
{
    if ( nRepeatPos == -1 || nRepeatPos > aString.getLength() )
        return;

    // Measuring a string containing a single copy of the repeat char is inaccurate.
    // To increase accuracy, start with a representative sample of a padding sequence.
    constexpr sal_Int32 nSampleSize = 20;
    OUStringBuffer aFill(nSampleSize);
    comphelper::string::padToLength(aFill, nSampleSize, nRepeatChar);

    tools::Long nSampleWidth = GetFmtTextWidth(aFill.makeStringAndClear());
    double nAvgCharWidth = static_cast<double>(nSampleWidth) / static_cast<double>(nSampleSize);

    // Intentionally truncate to round toward zero
    auto nCharWidth = static_cast<tools::Long>(nAvgCharWidth);
    if ( nCharWidth < 1 || (bPixelToLogic && nCharWidth < pOutput->mpRefDevice->PixelToLogic(Size(1,0)).Width()) )
        return;

    // Are there restrictions on the cell type we should filter out here ?
    tools::Long nTextWidth = aTextSize.Width();
    if ( bPixelToLogic )
    {
        nColWidth = pOutput->mpRefDevice->PixelToLogic(Size(nColWidth,0)).Width();
        nTextWidth = pOutput->mpRefDevice->PixelToLogic(Size(nTextWidth,0)).Width();
    }

    tools::Long nSpaceToFill = nColWidth - nTextWidth;
    if ( nSpaceToFill <= nCharWidth )
        return;

    // Intentionally truncate to round toward zero
    auto nCharsToInsert = static_cast<sal_Int32>(static_cast<double>(nSpaceToFill) / nAvgCharWidth);
    aFill.ensureCapacity(nCharsToInsert);
    comphelper::string::padToLength(aFill, nCharsToInsert, nRepeatChar);
    aString = aString.replaceAt( nRepeatPos, 0, aFill );
    TextChanged();
}

bool ScDrawStringsVars::SetTextToWidthOrHash( ScRefCellValue& rCell, tools::Long nWidth )
{
    // #i113045# do the single-character width calculations in logic units
    if (bPixelToLogic)
        nWidth = pOutput->mpRefDevice->PixelToLogic(Size(nWidth,0)).Width();

    CellType eType = rCell.getType();
    if (eType != CELLTYPE_VALUE && eType != CELLTYPE_FORMULA)
        // must be a value or formula cell.
        return false;

    if (eType == CELLTYPE_FORMULA)
    {
        ScFormulaCell* pFCell = rCell.getFormula();
        if (pFCell->GetErrCode() != FormulaError::NONE || pOutput->mbShowFormulas)
        {
            SetHashText();      // If the error string doesn't fit, always use "###". Also for "display formulas" (#i116691#)
            return true;
        }
        // If it's formula, the result must be a value.
        if (!pFCell->IsValue())
            return false;
    }

    sal_uLong nFormat = GetResultValueFormat();
    if ((nFormat % SV_COUNTRY_LANGUAGE_OFFSET) != 0)
    {
        // Not 'General' number format.  Set hash text and bail out.
        SetHashText();
        return true;
    }

    double fVal = rCell.getValue();

    const SvNumberformat* pNumFormat = pOutput->mpDoc->GetFormatTable()->GetEntry(nFormat);
    if (!pNumFormat)
        return false;

    tools::Long nMaxDigit = GetMaxDigitWidth();
    if (!nMaxDigit)
        return false;

    sal_uInt16 nNumDigits = static_cast<sal_uInt16>(nWidth / nMaxDigit);
    {
        OUString sTempOut(aString);
        if (!pNumFormat->GetOutputString(fVal, nNumDigits, sTempOut, pOutput->mpDoc->GetFormatTable()->GetNatNum()))
        {
            aString = sTempOut;
            // Failed to get output string.  Bail out.
            return false;
        }
        aString = sTempOut;
    }
    sal_uInt8 nSignCount = 0, nDecimalCount = 0, nExpCount = 0;
    sal_Int32 nLen = aString.getLength();
    sal_Unicode cDecSep = ScGlobal::getLocaleData().getLocaleItem().decimalSeparator[0];
    for( sal_Int32 i = 0; i < nLen; ++i )
    {
        sal_Unicode c = aString[i];
        if (c == '-')
            ++nSignCount;
        else if (c == cDecSep)
            ++nDecimalCount;
        else if (c == 'E')
            ++nExpCount;
    }

    // #i112250# A small value might be formatted as "0" when only counting the digits,
    // but fit into the column when considering the smaller width of the decimal separator.
    if (aString == "0" && fVal != 0.0)
        nDecimalCount = 1;

    if (nDecimalCount)
        nWidth += (nMaxDigit - GetDotWidth()) * nDecimalCount;
    if (nSignCount)
        nWidth += (nMaxDigit - GetSignWidth()) * nSignCount;
    if (nExpCount)
        nWidth += (nMaxDigit - GetExpWidth()) * nExpCount;

    if (nDecimalCount || nSignCount || nExpCount)
    {
        // Re-calculate.
        nNumDigits = static_cast<sal_uInt16>(nWidth / nMaxDigit);
        OUString sTempOut(aString);
        if (!pNumFormat->GetOutputString(fVal, nNumDigits, sTempOut, pOutput->mpDoc->GetFormatTable()->GetNatNum()))
        {
            aString = sTempOut;
            // Failed to get output string.  Bail out.
            return false;
        }
        aString = sTempOut;
    }

    tools::Long nActualTextWidth = GetFmtTextWidth(aString);
    if (nActualTextWidth > nWidth)
    {
        // Even after the decimal adjustment the text doesn't fit.  Give up.
        SetHashText();
        return true;
    }

    TextChanged();
    maLastCell.clear();   // #i113022# equal cell and format in another column may give different string
    return false;
}

void ScDrawStringsVars::SetAutoText( const OUString& rAutoText )
{
    aString = rAutoText;

    OutputDevice* pRefDevice = pOutput->mpRefDevice;
    OutputDevice* pFmtDevice = pOutput->pFmtDevice;
    aTextSize.setWidth( GetFmtTextWidth( aString ) );
    aTextSize.setHeight( pFmtDevice->GetTextHeight() );

    if ( !pRefDevice->GetConnectMetaFile() || pRefDevice->GetOutDevType() == OUTDEV_PRINTER )
    {
        double fMul = pOutput->GetStretch();
        aTextSize.setWidth( static_cast<tools::Long>(aTextSize.Width() / fMul + 0.5) );
    }

    aTextSize.setHeight( aMetric.GetAscent() + aMetric.GetDescent() );
    if ( GetOrient() != SvxCellOrientation::Standard )
    {
        tools::Long nTemp = aTextSize.Height();
        aTextSize.setHeight( aTextSize.Width() );
        aTextSize.setWidth( nTemp );
    }

    nOriginalWidth = aTextSize.Width();
    if ( bPixelToLogic )
        aTextSize = pRefDevice->LogicToPixel( aTextSize );

    maLastCell.clear();       // the same text may fit in the next cell
}

tools::Long ScDrawStringsVars::GetMaxDigitWidth()
{
    if (nMaxDigitWidth > 0)
        return nMaxDigitWidth;

    for (char i = 0; i < 10; ++i)
    {
        char cDigit = '0' + i;
        // Do not cache this with GetFmtTextWidth(), nMaxDigitWidth is already cached.
        tools::Long n = pOutput->pFmtDevice->GetTextWidth(OUString(cDigit));
        nMaxDigitWidth = ::std::max(nMaxDigitWidth, n);
    }
    return nMaxDigitWidth;
}

tools::Long ScDrawStringsVars::GetSignWidth()
{
    if (nSignWidth > 0)
        return nSignWidth;

    nSignWidth = pOutput->pFmtDevice->GetTextWidth(OUString('-'));
    return nSignWidth;
}

tools::Long ScDrawStringsVars::GetDotWidth()
{
    if (nDotWidth > 0)
        return nDotWidth;

    const OUString& sep = ScGlobal::getLocaleData().getLocaleItem().decimalSeparator;
    nDotWidth = pOutput->pFmtDevice->GetTextWidth(sep);
    return nDotWidth;
}

tools::Long ScDrawStringsVars::GetExpWidth()
{
    if (nExpWidth > 0)
        return nExpWidth;

    nExpWidth = pOutput->pFmtDevice->GetTextWidth(OUString('E'));
    return nExpWidth;
}

tools::Long ScDrawStringsVars::GetFmtTextWidth( const OUString& rString )
{
    return pOutput->pFmtDevice->GetTextWidth( rString, 0, -1, nullptr, GetLayoutGlyphs( rString ));
}

void ScDrawStringsVars::TextChanged()
{
    OutputDevice* pRefDevice = pOutput->mpRefDevice;
    OutputDevice* pFmtDevice = pOutput->pFmtDevice;
    aTextSize.setWidth( GetFmtTextWidth( aString ) );
    aTextSize.setHeight( pFmtDevice->GetTextHeight() );

    if ( !pRefDevice->GetConnectMetaFile() || pRefDevice->GetOutDevType() == OUTDEV_PRINTER )
    {
        double fMul = pOutput->GetStretch();
        aTextSize.setWidth( static_cast<tools::Long>(aTextSize.Width() / fMul + 0.5) );
    }

    aTextSize.setHeight( aMetric.GetAscent() + aMetric.GetDescent() );
    if ( GetOrient() != SvxCellOrientation::Standard )
    {
        tools::Long nTemp = aTextSize.Height();
        aTextSize.setHeight( aTextSize.Width() );
        aTextSize.setWidth( nTemp );
    }

    nOriginalWidth = aTextSize.Width();
    if ( bPixelToLogic )
        aTextSize = pRefDevice->LogicToPixel( aTextSize );
}

bool ScDrawStringsVars::HasEditCharacters() const
{
    for (sal_Int32 nIdx = 0; nIdx < aString.getLength(); ++nIdx)
    {
        switch(aString[nIdx])
        {
            case CHAR_NBSP:
                // tdf#122676: Ignore CHAR_NBSP (this is thousand separator in any number)
                // if repeat character is set
                if (nRepeatPos < 0)
                    return true;
                break;
            case CHAR_SHY:
            case CHAR_ZWSP:
            case CHAR_LRM:
            case CHAR_RLM:
            case CHAR_NBHY:
            case CHAR_WJ:
                return true;
            default:
                break;
        }
    }

    return false;
}

double ScOutputData::GetStretch() const
{
    if ( mpRefDevice->IsMapModeEnabled() )
    {
        //  If a non-trivial MapMode is set, its scale is now already
        //  taken into account in the OutputDevice's font handling
        //  (OutputDevice::ImplNewFont, see #95414#).
        //  The old handling below is only needed for pixel output.
        return 1.0;
    }

    // calculation in double is faster than Fraction multiplication
    // and doesn't overflow

    if ( mpRefDevice == pFmtDevice )
    {
        MapMode aOld = mpRefDevice->GetMapMode();
        return static_cast<double>(aOld.GetScaleY()) / static_cast<double>(aOld.GetScaleX()) * static_cast<double>(maZoomY) / static_cast<double>(maZoomX);
    }
    else
    {
        // when formatting for printer, device map mode has already been taken care of
        return static_cast<double>(maZoomY) / static_cast<double>(maZoomX);
    }
}

//  output strings

static void lcl_DoHyperlinkResult( const OutputDevice* pDev, const tools::Rectangle& rRect, const ScRefCellValue& rCell )
{
    vcl::PDFExtOutDevData* pPDFData = dynamic_cast< vcl::PDFExtOutDevData* >( pDev->GetExtOutDevData() );

    OUString aURL;
    OUString aCellText;
    if (rCell.getType() == CELLTYPE_FORMULA)
    {
        ScFormulaCell* pFCell = rCell.getFormula();
        if ( pFCell->IsHyperLinkCell() )
            pFCell->GetURLResult( aURL, aCellText );
    }

    if ( !aURL.isEmpty() && pPDFData )
    {
        vcl::PDFExtOutDevBookmarkEntry aBookmark;
        aBookmark.nLinkId = pPDFData->CreateLink(rRect, u""_ustr);
        aBookmark.aBookmark = aURL;
        std::vector< vcl::PDFExtOutDevBookmarkEntry >& rBookmarks = pPDFData->GetBookmarks();
        rBookmarks.push_back( aBookmark );
    }
}

void ScOutputData::SetSyntaxColor( vcl::Font* pFont, const ScRefCellValue& rCell )
{
    switch (rCell.getType())
    {
        case CELLTYPE_VALUE:
            pFont->SetColor(*mxValueColor);
        break;
        case CELLTYPE_STRING:
            pFont->SetColor(*mxTextColor);
        break;
        case CELLTYPE_FORMULA:
            pFont->SetColor(*mxFormulaColor);
        break;
        default:
        {
            // added to avoid warnings
        }
    }
}

static void lcl_SetEditColor( EditEngine& rEngine, const Color& rColor )
{
    ESelection aSel( 0, 0, rEngine.GetParagraphCount(), 0 );
    SfxItemSet aSet( rEngine.GetEmptyItemSet() );
    aSet.Put( SvxColorItem( rColor, EE_CHAR_COLOR ) );
    rEngine.QuickSetAttribs( aSet, aSel );
    // function is called with update mode set to FALSE
}

void ScOutputData::SetEditSyntaxColor( EditEngine& rEngine, const ScRefCellValue& rCell )
{
    Color aColor;
    switch (rCell.getType())
    {
        case CELLTYPE_VALUE:
            aColor = *mxValueColor;
            break;
        case CELLTYPE_STRING:
        case CELLTYPE_EDIT:
            aColor = *mxTextColor;
            break;
        case CELLTYPE_FORMULA:
            aColor = *mxFormulaColor;
            break;
        default:
        {
            // added to avoid warnings
        }
    }
    lcl_SetEditColor( rEngine, aColor );
}

bool ScOutputData::GetMergeOrigin( SCCOL nX, SCROW nY, SCSIZE nArrY,
                                    SCCOL& rOverX, SCROW& rOverY,
                                    bool bVisRowChanged )
{
    bool bDoMerge = false;
    bool bIsLeft = ( nX == mnVisX1 );
    bool bIsTop  = ( nY == mnVisY1 ) || bVisRowChanged;

    bool bHOver;
    bool bVOver;
    bool bHidden;

    if (!mpDoc->ColHidden(nX, mnTab) && nX >= mnX1 && nX <= mnX2
            && !mpDoc->RowHidden(nY, mnTab) && nY >= mnY1 && nY <= mnY2)
    {
        ScCellInfo* pInfo = &mpRowInfo[nArrY].cellInfo(nX);
        bHOver = pInfo->bHOverlapped;
        bVOver = pInfo->bVOverlapped;
    }
    else
    {
        ScMF nOverlap2 = mpDoc->GetAttr(nX, nY, mnTab, ATTR_MERGE_FLAG)->GetValue();
        bHOver = bool(nOverlap2 & ScMF::Hor);
        bVOver = bool(nOverlap2 & ScMF::Ver);
    }

    if ( bHOver && bVOver )
        bDoMerge = bIsLeft && bIsTop;
    else if ( bHOver )
        bDoMerge = bIsLeft;
    else if ( bVOver )
        bDoMerge = bIsTop;

    rOverX = nX;
    rOverY = nY;

    while (bHOver)              // nY constant
    {
        --rOverX;
        bHidden = mpDoc->ColHidden(rOverX, mnTab);
        if ( !bDoMerge && !bHidden )
            return false;

        if (rOverX >= mnX1 && !bHidden)
        {
            bHOver = mpRowInfo[nArrY].cellInfo(rOverX).bHOverlapped;
            bVOver = mpRowInfo[nArrY].cellInfo(rOverX).bVOverlapped;
        }
        else
        {
            ScMF nOverlap = mpDoc->GetAttr(rOverX, rOverY, mnTab, ATTR_MERGE_FLAG)->GetValue();
            bHOver = bool(nOverlap & ScMF::Hor);
            bVOver = bool(nOverlap & ScMF::Ver);
        }
    }

    while (bVOver)
    {
        --rOverY;
        bHidden = mpDoc->RowHidden(rOverY, mnTab);
        if ( !bDoMerge && !bHidden )
            return false;

        if (nArrY>0)
            --nArrY;                        // local copy !

        if (rOverX >= mnX1 && rOverY >= mnY1 &&
            !mpDoc->ColHidden(rOverX, mnTab) &&
            !mpDoc->RowHidden(rOverY, mnTab) &&
            mpRowInfo[nArrY].nRowNo == rOverY)
        {
            bVOver = mpRowInfo[nArrY].cellInfo(rOverX).bVOverlapped;
        }
        else
        {
            ScMF nOverlap = mpDoc->GetAttr( rOverX, rOverY, mnTab, ATTR_MERGE_FLAG )->GetValue();
            bVOver = bool(nOverlap & ScMF::Ver);
        }
    }

    return true;
}

static bool StringDiffer( const ScPatternAttr*& rpOldPattern, const ScPatternAttr* pNewPattern )
{
    assert(pNewPattern && "pNewPattern");

    if ( ScPatternAttr::areSame( pNewPattern, rpOldPattern ) )
        return false;
    else if ( !rpOldPattern )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT ), rpOldPattern->GetItem( ATTR_FONT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_CJK_FONT ), rpOldPattern->GetItem( ATTR_CJK_FONT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_CTL_FONT ), rpOldPattern->GetItem( ATTR_CTL_FONT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_HEIGHT ), rpOldPattern->GetItem( ATTR_FONT_HEIGHT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_CJK_FONT_HEIGHT ), rpOldPattern->GetItem( ATTR_CJK_FONT_HEIGHT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_CTL_FONT_HEIGHT ), rpOldPattern->GetItem( ATTR_CTL_FONT_HEIGHT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_WEIGHT ), rpOldPattern->GetItem( ATTR_FONT_WEIGHT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_CJK_FONT_WEIGHT ), rpOldPattern->GetItem( ATTR_CJK_FONT_WEIGHT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_CTL_FONT_WEIGHT ), rpOldPattern->GetItem( ATTR_CTL_FONT_WEIGHT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_POSTURE ), rpOldPattern->GetItem( ATTR_FONT_POSTURE ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_CJK_FONT_POSTURE ), rpOldPattern->GetItem( ATTR_CJK_FONT_POSTURE ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_CTL_FONT_POSTURE ), rpOldPattern->GetItem( ATTR_CTL_FONT_POSTURE ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_UNDERLINE ), rpOldPattern->GetItem( ATTR_FONT_UNDERLINE ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_OVERLINE ), rpOldPattern->GetItem( ATTR_FONT_OVERLINE ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_WORDLINE ), rpOldPattern->GetItem( ATTR_FONT_WORDLINE ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_CROSSEDOUT ), rpOldPattern->GetItem( ATTR_FONT_CROSSEDOUT ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_CONTOUR ), rpOldPattern->GetItem( ATTR_FONT_CONTOUR ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_SHADOWED ), rpOldPattern->GetItem( ATTR_FONT_SHADOWED ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_COLOR ), rpOldPattern->GetItem( ATTR_FONT_COLOR ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_HOR_JUSTIFY ), rpOldPattern->GetItem( ATTR_HOR_JUSTIFY ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_HOR_JUSTIFY_METHOD ), rpOldPattern->GetItem( ATTR_HOR_JUSTIFY_METHOD ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_VER_JUSTIFY ), rpOldPattern->GetItem( ATTR_VER_JUSTIFY ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_VER_JUSTIFY_METHOD ), rpOldPattern->GetItem( ATTR_VER_JUSTIFY_METHOD ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_STACKED ), rpOldPattern->GetItem( ATTR_STACKED ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_LINEBREAK ), rpOldPattern->GetItem( ATTR_LINEBREAK ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_MARGIN ), rpOldPattern->GetItem( ATTR_MARGIN ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_ROTATE_VALUE ), rpOldPattern->GetItem( ATTR_ROTATE_VALUE ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FORBIDDEN_RULES ), rpOldPattern->GetItem( ATTR_FORBIDDEN_RULES ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_EMPHASISMARK ), rpOldPattern->GetItem( ATTR_FONT_EMPHASISMARK ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_FONT_RELIEF ), rpOldPattern->GetItem( ATTR_FONT_RELIEF ) ) )
        return true;
    else if ( !SfxPoolItem::areSame( pNewPattern->GetItem( ATTR_BACKGROUND ), rpOldPattern->GetItem( ATTR_BACKGROUND ) ) )
        return true;    // needed with automatic text color
    else
    {
        rpOldPattern = pNewPattern;
        return false;
    }
}

static void lcl_CreateInterpretProgress( bool& bProgress, ScDocument* pDoc,
        const ScFormulaCell* pFCell )
{
    if ( !bProgress && pFCell->GetDirty() )
    {
        ScProgress::CreateInterpretProgress( pDoc );
        bProgress = true;
    }
}

static bool IsAmbiguousScript( SvtScriptType nScript )
{
    return ( nScript != SvtScriptType::LATIN &&
             nScript != SvtScriptType::ASIAN &&
             nScript != SvtScriptType::COMPLEX );
}

bool ScOutputData::IsEmptyCellText( const RowInfo* pThisRowInfo, SCCOL nX, SCROW nY )
{
    // pThisRowInfo may be NULL

    bool bEmpty;
    if ( pThisRowInfo && nX <= mnX2 )
        bEmpty = pThisRowInfo->basicCellInfo(nX).bEmptyCellText;
    else
    {
        ScRefCellValue aCell(*mpDoc, ScAddress(nX, nY, mnTab));
        bEmpty = aCell.isEmpty();
    }

    if ( !bEmpty && ( nX < mnX1 || nX > mnX2 || !pThisRowInfo ) )
    {
        //  for the range mnX1..mnX2 in RowInfo, cell protection attribute is already evaluated
        //  into bEmptyCellText in ScDocument::FillInfo / lcl_HidePrint (printfun)

        bool bIsPrint = ( meType == OUTTYPE_PRINTER );

        if ( bIsPrint || mbTabProtected )
        {
            const ScProtectionAttr* pAttr =
                    mpDoc->GetEffItem( nX, nY, mnTab, ATTR_PROTECTION );
            if ( bIsPrint && pAttr->GetHidePrint() )
                bEmpty = true;
            else if ( mbTabProtected )
            {
                if ( pAttr->GetHideCell() )
                    bEmpty = true;
                else if ( mbShowFormulas && pAttr->GetHideFormula() )
                {
                    if (mpDoc->GetCellType(ScAddress(nX, nY, mnTab)) == CELLTYPE_FORMULA)
                        bEmpty = true;
                }
            }
        }
    }
    return bEmpty;
}

void ScOutputData::GetVisibleCell( SCCOL nCol, SCROW nRow, SCTAB nTabP, ScRefCellValue& rCell )
{
    rCell.assign(*mpDoc, ScAddress(nCol, nRow, nTabP));
    if (!rCell.isEmpty() && IsEmptyCellText(nullptr, nCol, nRow))
        rCell.clear();
}

bool ScOutputData::IsAvailable( SCCOL nX, SCROW nY )
{
    //  apply the same logic here as in DrawStrings/DrawEdit:
    //  Stop at non-empty or merged or overlapped cell,
    //  where a note is empty as well as a cell that's hidden by protection settings

    ScRefCellValue aCell(*mpDoc, ScAddress(nX, nY, mnTab));
    if (!aCell.isEmpty() && !IsEmptyCellText(nullptr, nX, nY))
        return false;

    const ScPatternAttr* pPattern = mpDoc->GetPattern( nX, nY, mnTab );
    return !(pPattern->GetItem(ATTR_MERGE).IsMerged() ||
         pPattern->GetItem(ATTR_MERGE_FLAG).IsOverlapped());
}

// nX, nArrY:       loop variables from DrawStrings / DrawEdit
// nPosX, nPosY:    corresponding positions for nX, nArrY
// nCellX, nCellY:  position of the cell that contains the text
// nNeeded:         Text width, including margin
// rPattern:        cell format at nCellX, nCellY
// nHorJustify:     horizontal alignment (visual) to determine which cells to use for long strings
// bCellIsValue:    if set, don't extend into empty cells
// bBreak:          if set, don't extend, and don't set clip marks (but rLeftClip/rRightClip is set)
// bOverwrite:      if set, also extend into non-empty cells (for rotated text)
// rParam           output: various area parameters.

void ScOutputData::GetOutputArea( SCCOL nX, SCSIZE nArrY, tools::Long nPosX, tools::Long nPosY,
                                  SCCOL nCellX, SCROW nCellY, tools::Long nNeeded,
                                  const ScPatternAttr& rPattern,
                                  sal_uInt16 nHorJustify, bool bCellIsValue,
                                  bool bBreak, bool bOverwrite,
                                  OutputAreaParam& rParam )
{
    //  rThisRowInfo may be for a different row than nCellY, is still used for clip marks
    RowInfo& rThisRowInfo = mpRowInfo[nArrY];

    tools::Long nLayoutSign = mbLayoutRTL ? -1 : 1;

    tools::Long nCellPosX = nPosX;         // find nCellX position, starting at nX/nPosX
    SCCOL nCompCol = nX;
    while ( nCellX > nCompCol )
    {
        //! extra member function for width?
        tools::Long nColWidth = ( nCompCol <= mnX2 ) ?
                mpRowInfo[0].basicCellInfo(nCompCol).nWidth :
                static_cast<tools::Long>( mpDoc->GetColWidth( nCompCol, mnTab ) * mnPPTX );
        nCellPosX += nColWidth * nLayoutSign;
        ++nCompCol;
    }
    while ( nCellX < nCompCol )
    {
        --nCompCol;
        tools::Long nColWidth = ( nCompCol <= mnX2 ) ?
                mpRowInfo[0].basicCellInfo(nCompCol).nWidth :
                static_cast<tools::Long>( mpDoc->GetColWidth( nCompCol, mnTab ) * mnPPTX );
        nCellPosX -= nColWidth * nLayoutSign;
    }

    tools::Long nCellPosY = nPosY;         // find nCellY position, starting at nArrY/nPosY
    SCSIZE nCompArr = nArrY;
    SCROW nCompRow = mpRowInfo[nCompArr].nRowNo;
    while ( nCellY > nCompRow )
    {
        if ( nCompArr + 1 < mnArrCount )
        {
            nCellPosY += mpRowInfo[nCompArr].nHeight;
            ++nCompArr;
            nCompRow = mpRowInfo[nCompArr].nRowNo;
        }
        else
        {
            sal_uInt16 nDocHeight = mpDoc->GetRowHeight( nCompRow, mnTab );
            if ( nDocHeight )
                nCellPosY += static_cast<tools::Long>( nDocHeight * mnPPTY );
            ++nCompRow;
        }
    }
    nCellPosY -= mpDoc->GetScaledRowHeight( nCellY, nCompRow-1, mnTab, mnPPTY );

    const ScMergeAttr* pMerge = &rPattern.GetItem( ATTR_MERGE );
    bool bMerged = pMerge->IsMerged();
    tools::Long nMergeCols = pMerge->GetColMerge();
    if ( nMergeCols == 0 )
        nMergeCols = 1;
    tools::Long nMergeRows = pMerge->GetRowMerge();
    if ( nMergeRows == 0 )
        nMergeRows = 1;

    tools::Long nMergeSizeX = 0;
    for ( tools::Long i=0; i<nMergeCols; i++ )
    {
        tools::Long nColWidth = ( nCellX+i <= mnX2 ) ?
                mpRowInfo[0].basicCellInfo(nCellX+i).nWidth :
                static_cast<tools::Long>( mpDoc->GetColWidth( sal::static_int_cast<SCCOL>(nCellX+i), mnTab ) * mnPPTX );
        nMergeSizeX += nColWidth;
    }
    tools::Long nMergeSizeY = 0;
    short nDirect = 0;
    if ( rThisRowInfo.nRowNo == nCellY )
    {
        // take first row's height from row info
        nMergeSizeY += rThisRowInfo.nHeight;
        nDirect = 1;        // skip in loop
    }
    // following rows always from document
    nMergeSizeY += mpDoc->GetScaledRowHeight( nCellY+nDirect, nCellY+nMergeRows-1, mnTab, mnPPTY);

    --nMergeSizeX;      // leave out the grid horizontally, also for alignment (align between grid lines)

    rParam.mnColWidth = nMergeSizeX; // store the actual column width.
    rParam.mnLeftClipLength = rParam.mnRightClipLength = 0;

    // construct the rectangles using logical left/right values (justify is called at the end)

    // rAlignRect is the single cell or merged area, used for alignment.

    rParam.maAlignRect.SetLeft( nCellPosX );
    rParam.maAlignRect.SetRight( nCellPosX + ( nMergeSizeX - 1 ) * nLayoutSign );
    rParam.maAlignRect.SetTop( nCellPosY );
    rParam.maAlignRect.SetBottom( nCellPosY + nMergeSizeY - 1 );

    //  rClipRect is all cells that are used for output.
    //  For merged cells this is the same as rAlignRect, otherwise neighboring cells can also be used.

    rParam.maClipRect = rParam.maAlignRect;
    if ( nNeeded > nMergeSizeX )
    {
        SvxCellHorJustify eHorJust = static_cast<SvxCellHorJustify>(nHorJustify);

        tools::Long nMissing = nNeeded - nMergeSizeX;
        tools::Long nLeftMissing = 0;
        tools::Long nRightMissing = 0;
        switch ( eHorJust )
        {
            case SvxCellHorJustify::Left:
                nRightMissing = nMissing;
                break;
            case SvxCellHorJustify::Right:
                nLeftMissing = nMissing;
                break;
            case SvxCellHorJustify::Center:
                nLeftMissing = nMissing / 2;
                nRightMissing = nMissing - nLeftMissing;
                break;
            default:
            {
                // added to avoid warnings
            }
        }

        // nLeftMissing, nRightMissing are logical, eHorJust values are visual
        if ( mbLayoutRTL )
            ::std::swap( nLeftMissing, nRightMissing );

        SCCOL nRightX = nCellX;
        SCCOL nLeftX = nCellX;
        if ( !bMerged && !bCellIsValue && !bBreak )
        {
            //  look for empty cells into which the text can be extended

            while ( nRightMissing > 0 && nRightX < mpDoc->MaxCol() && ( bOverwrite || IsAvailable( nRightX+1, nCellY ) ) )
            {
                ++nRightX;
                tools::Long nAdd = static_cast<tools::Long>( mpDoc->GetColWidth( nRightX, mnTab ) * mnPPTX );
                nRightMissing -= nAdd;
                rParam.maClipRect.AdjustRight(nAdd * nLayoutSign );

                if ( rThisRowInfo.nRowNo == nCellY && nRightX >= mnX1 && nRightX <= mnX2 )
                    rThisRowInfo.cellInfo(nRightX-1).bHideGrid = true;
            }

            while ( nLeftMissing > 0 && nLeftX > 0 && ( bOverwrite || IsAvailable( nLeftX-1, nCellY ) ) )
            {
                if ( rThisRowInfo.nRowNo == nCellY && nLeftX >= mnX1 && nLeftX <= mnX2 )
                    rThisRowInfo.cellInfo(nLeftX-1).bHideGrid = true;

                --nLeftX;
                tools::Long nAdd = static_cast<tools::Long>( mpDoc->GetColWidth( nLeftX, mnTab ) * mnPPTX );
                nLeftMissing -= nAdd;
                rParam.maClipRect.AdjustLeft( -(nAdd * nLayoutSign) );
            }
        }

        //  Set flag and reserve space for clipping mark triangle,
        //  even if rThisRowInfo isn't for nCellY (merged cells).
        if ( nRightMissing > 0 && mbMarkClipped && nRightX >= mnX1 && nRightX <= mnX2 && !bBreak && !bCellIsValue )
        {
            rThisRowInfo.cellInfo(nRightX).nClipMark |= ScClipMark::Right;
            mbAnyClipped = true;
            tools::Long nMarkPixel = static_cast<tools::Long>( SC_CLIPMARK_SIZE * mnPPTX );
            rParam.maClipRect.AdjustRight( -(nMarkPixel * nLayoutSign) );
        }
        if ( nLeftMissing > 0 && mbMarkClipped && nLeftX >= mnX1 && nLeftX <= mnX2 && !bBreak && !bCellIsValue )
        {
            rThisRowInfo.cellInfo(nLeftX).nClipMark |= ScClipMark::Left;
            mbAnyClipped = true;
            tools::Long nMarkPixel = static_cast<tools::Long>( SC_CLIPMARK_SIZE * mnPPTX );
            rParam.maClipRect.AdjustLeft(nMarkPixel * nLayoutSign );
        }

        rParam.mbLeftClip = ( nLeftMissing > 0 );
        rParam.mbRightClip = ( nRightMissing > 0 );
        rParam.mnLeftClipLength = nLeftMissing;
        rParam.mnRightClipLength = nRightMissing;
    }
    else
    {
        rParam.mbLeftClip = rParam.mbRightClip = false;

        // leave space for AutoFilter on screen
        // (for automatic line break: only if not formatting for printer, as in ScColumn::GetNeededSize)

        if ( meType==OUTTYPE_WINDOW &&
             ( rPattern.GetItem(ATTR_MERGE_FLAG).GetValue() & (ScMF::Auto|ScMF::Button|ScMF::ButtonPopup) ) &&
             ( !bBreak || mpRefDevice == pFmtDevice ) )
        {
            // filter drop-down width depends on row height
            double fZoom = mpRefDevice ? static_cast<double>(mpRefDevice->GetMapMode().GetScaleY()) : 1.0;
            fZoom = fZoom > 1.0 ? fZoom : 1.0;
            const tools::Long nFilter = fZoom * DROPDOWN_BITMAP_SIZE;
            bool bFit = ( nNeeded + nFilter <= nMergeSizeX );
            if ( bFit )
            {
                // content fits even in the remaining area without the filter button
                // -> align within that remaining area

                rParam.maAlignRect.AdjustRight( -(nFilter * nLayoutSign) );
                rParam.maClipRect.AdjustRight( -(nFilter * nLayoutSign) );
            }
        }
    }

    //  justify both rectangles for alignment calculation, use with DrawText etc.

    rParam.maAlignRect.Normalize();
    rParam.maClipRect.Normalize();
}

namespace {

bool beginsWithRTLCharacter(const OUString& rStr)
{
    if (rStr.isEmpty())
        return false;

    switch (ScGlobal::getCharClass().getCharacterDirection(rStr, 0))
    {
        case i18n::DirectionProperty_RIGHT_TO_LEFT:
        case i18n::DirectionProperty_RIGHT_TO_LEFT_ARABIC:
        case i18n::DirectionProperty_RIGHT_TO_LEFT_EMBEDDING:
        case i18n::DirectionProperty_RIGHT_TO_LEFT_OVERRIDE:
            return true;
        default:
            ;
    }

    return false;
}

}

/** Get left, right or centered alignment from RTL context.

    Does not return standard, block or repeat, for these the contextual left or
    right alignment is returned.
 */
static SvxCellHorJustify getAlignmentFromContext( SvxCellHorJustify eInHorJust,
        bool bCellIsValue, const OUString& rText,
        const ScPatternAttr& rPattern, const SfxItemSet* pCondSet,
        const ScDocument* pDoc, SCTAB mnTab, const bool  bNumberFormatIsText )
{
    SvxCellHorJustify eHorJustContext = eInHorJust;
    bool bUseWritingDirection = false;
    if (eInHorJust == SvxCellHorJustify::Standard)
    {
        // fdo#32530: Default alignment depends on value vs
        // string, and the direction of the 1st letter.
        if (beginsWithRTLCharacter( rText)) //If language is RTL
        {
            if (bCellIsValue)
               eHorJustContext = bNumberFormatIsText ? SvxCellHorJustify::Right : SvxCellHorJustify::Left;
            else
                eHorJustContext = SvxCellHorJustify::Right;
        }
        else if (bCellIsValue) //If language is not RTL
            eHorJustContext = bNumberFormatIsText ? SvxCellHorJustify::Left : SvxCellHorJustify::Right;
        else
            bUseWritingDirection = true;
    }

    if (bUseWritingDirection ||
            eInHorJust == SvxCellHorJustify::Block || eInHorJust == SvxCellHorJustify::Repeat)
    {
        SvxFrameDirection nDirection = lcl_GetValue<SvxFrameDirectionItem, SvxFrameDirection>(rPattern, ATTR_WRITINGDIR, pCondSet);
        if (nDirection == SvxFrameDirection::Horizontal_LR_TB || nDirection == SvxFrameDirection::Vertical_LR_TB)
            eHorJustContext = SvxCellHorJustify::Left;
        else if (nDirection == SvxFrameDirection::Environment)
        {
            SAL_WARN_IF( !pDoc, "sc.ui", "getAlignmentFromContext - pDoc==NULL");
            // fdo#73588: The content of the cell must also
            // begin with a RTL character to be right
            // aligned; otherwise, it should be left aligned.
            eHorJustContext = (pDoc && pDoc->IsLayoutRTL(mnTab) && (beginsWithRTLCharacter( rText))) ? SvxCellHorJustify::Right : SvxCellHorJustify::Left;
        }
        else
            eHorJustContext = SvxCellHorJustify::Right;
    }
    return eHorJustContext;
}

void ScOutputData::DrawStrings( bool bPixelToLogic )
{
    LayoutStrings(bPixelToLogic);
}

void ScOutputData::LayoutStrings(bool bPixelToLogic)
{
    vcl::PDFExtOutDevData* pPDF = dynamic_cast<vcl::PDFExtOutDevData*>(mpDev->GetExtOutDevData());
    bool bTaggedPDF = pPDF && pPDF->GetIsExportTaggedPDF();
    if (bTaggedPDF)
    {
        pPDF->WrapBeginStructureElement(vcl::pdf::StructElement::Table, u"Table"_ustr);
        pPDF->GetScPDFState()->m_TableRowMap.clear();
    }

    bool bOrigIsInLayoutStrings = mpDoc->IsInLayoutStrings();
    mpDoc->SetLayoutStrings(true);
    OSL_ENSURE( mpDev == mpRefDevice ||
                mpDev->GetMapMode().GetMapUnit() == mpRefDevice->GetMapMode().GetMapUnit(),
                "LayoutStrings: different MapUnits ?!?!" );
    vcl::text::ComplexTextLayoutFlags eTextLayout = mpDev->GetLayoutMode();
    mpDev->SetLayoutMode(vcl::text::ComplexTextLayoutFlags::Default);

    comphelper::ScopeGuard g([this, bOrigIsInLayoutStrings, eTextLayout] {
        mpDoc->SetLayoutStrings(bOrigIsInLayoutStrings);
        mpDev->SetLayoutMode(eTextLayout);
    });

    sc::IdleSwitch aIdleSwitch(*mpDoc, false);

    // Try to limit interpreting to only visible cells. Calling e.g. IsValue()
    // on a formula cell that needs interpreting would call Interpret()
    // for the entire formula group, which could be large.
    mpDoc->InterpretCellsIfNeeded( ScRange( mnX1, mnY1, mnTab, mnX2, mnY2, mnTab ));

    ScDrawStringsVars aVars( this, bPixelToLogic );

    bool bProgress = false;

    tools::Long nInitPosX = mnScrX;
    if ( mbLayoutRTL )
        nInitPosX += mnMirrorW - 1;              // pixels
    tools::Long nLayoutSign = mbLayoutRTL ? -1 : 1;

    SCCOL nLastContentCol = mpDoc->MaxCol();
    if ( mnX2 < mpDoc->MaxCol() )
    {
        SCROW nEndRow;
        mpDoc->GetCellArea(mnTab, nLastContentCol, nEndRow);
    }

    SCCOL nLoopStartX = mnX1;
    if ( mnX1 > 0  && !bTaggedPDF )
        --nLoopStartX;          // start before mnX1 for rest of long text to the left

    // variables for GetOutputArea
    const ScPatternAttr* pOldPattern = nullptr;
    const SfxItemSet* pOldCondSet = nullptr;
    SvtScriptType nOldScript = SvtScriptType::NONE;

    // alternative pattern instances in case we need to modify the pattern
    // before processing the cell value.
    std::vector<std::unique_ptr<ScPatternAttr> > aAltPatterns;

    KernArray aDX;
    tools::Long nPosY = mnScrY;
    for (SCSIZE nArrY=1; nArrY+1<mnArrCount; nArrY++)
    {
        RowInfo* pThisRowInfo = &mpRowInfo[nArrY];
        SCROW nY = pThisRowInfo->nRowNo;
        if (pThisRowInfo->bChanged)
        {
            bool bReopenRowTag = false;
            tools::Long nPosX = nInitPosX;
            if ( nLoopStartX < mnX1 )
                nPosX -= mpRowInfo[0].basicCellInfo(nLoopStartX).nWidth * nLayoutSign;
            std::optional<SCCOL> oFirstNonEmptyCellX;
            std::optional<SCCOL> oLastEmptyCellX;
            for (SCCOL nX=nLoopStartX; nX<=mnX2; nX++)
            {
                LayoutStringsImpl(bPixelToLogic, pThisRowInfo, nX, nY, nArrY, oFirstNonEmptyCellX, oLastEmptyCellX, nLastContentCol,
                                  aAltPatterns, pOldPattern, pOldCondSet, nOldScript, aVars,
                                  bProgress, nPosX, nPosY, bTaggedPDF, bReopenRowTag, pPDF, nLayoutSign, aDX);
                nPosX += mpRowInfo[0].basicCellInfo(nX).nWidth * nLayoutSign;
            }
        }
        nPosY += mpRowInfo[nArrY].nHeight;
    }
    if (bTaggedPDF)
        pPDF->EndStructureElement(); // Table

    if ( bProgress )
        ScProgress::DeleteInterpretProgress();
}

/// inner loop of LayoutStrings
void ScOutputData::LayoutStringsImpl(bool const bPixelToLogic, RowInfo* const pThisRowInfo,
            SCCOL const nX, SCROW const nY, SCSIZE const nArrY,
            std::optional<SCCOL>& oFirstNonEmptyCellX,
            std::optional<SCCOL>& oLastEmptyCellX,
            SCCOL const nLastContentCol,
            std::vector<std::unique_ptr<ScPatternAttr> >& aAltPatterns,
            const ScPatternAttr*& pOldPattern,
            const SfxItemSet*& pOldCondSet,
            SvtScriptType& nOldScript,
            ScDrawStringsVars& aVars,
            bool& bProgress, tools::Long const nPosX, tools::Long const nPosY,
            bool const bTaggedPDF, bool& bReopenRowTag, vcl::PDFExtOutDevData* const pPDF,
            tools::Long const nLayoutSign, KernArray& aDX)
{
    const ScPatternAttr* pPattern = nullptr;
    const SfxItemSet* pCondSet = nullptr;
    bool bCellIsValue = false;
    tools::Long nNeededWidth = 0;
    OutputAreaParam aAreaParam;
    bool bMergeEmpty = false;
    const ScCellInfo* pInfo = &pThisRowInfo->cellInfo(nX);
    bool bEmpty = nX < mnX1 || pThisRowInfo->basicCellInfo(nX).bEmptyCellText;

    SCCOL nCellX = nX;                  // position where the cell really starts
    SCROW nCellY = nY;
    bool bDoCell = false;
    bool bUseEditEngine = false;

    //  Part of a merged cell?

    bool bOverlapped = (pInfo->bHOverlapped || pInfo->bVOverlapped);
    if ( bOverlapped )
    {
        bEmpty = true;

        SCCOL nOverX;                   // start of the merged cells
        SCROW nOverY;
        bool bVisChanged = !mpRowInfo[nArrY-1].bChanged;
        if (GetMergeOrigin( nX,nY, nArrY, nOverX,nOverY, bVisChanged ))
        {
            nCellX = nOverX;
            nCellY = nOverY;
            bDoCell = true;
        }
        else
            bMergeEmpty = true;
    }

    //  Is there a long text further to the left, that overlaps this region?

    if ( bEmpty && !bMergeEmpty && nX < mnX1 && !bOverlapped )
    {
        if (!oFirstNonEmptyCellX)
        {
            SCCOL nTempX=mnX1;
            while (nTempX > 0 && IsEmptyCellText( pThisRowInfo, nTempX, nY ))
                --nTempX;
            oFirstNonEmptyCellX = nTempX;
        }

        if ( *oFirstNonEmptyCellX < mnX1 &&
             !IsEmptyCellText( pThisRowInfo, *oFirstNonEmptyCellX, nY ) &&
             !mpDoc->HasAttrib( *oFirstNonEmptyCellX,nY,mnTab, mnX1,nY,mnTab, HasAttrFlags::Merged | HasAttrFlags::Overlapped ) )
        {
            nCellX = *oFirstNonEmptyCellX;
            bDoCell = true;
        }
    }

    //  Is there a long text further to the right, that overlaps this region?

    if ( bEmpty && !bMergeEmpty && nX == mnX2 && !bOverlapped )
    {
        if (!oLastEmptyCellX)
        {
            //  don't have to look further than nLastContentCol
            SCCOL nTempX=nX;
            while (nTempX < nLastContentCol && IsEmptyCellText( pThisRowInfo, nTempX, nY ))
                ++nTempX;
            oLastEmptyCellX = nTempX;
        }

        if ( *oLastEmptyCellX > nX &&
             !IsEmptyCellText( pThisRowInfo, *oLastEmptyCellX, nY ) &&
             !mpDoc->HasAttrib( *oLastEmptyCellX, nY,mnTab, nX,nY,mnTab, HasAttrFlags::Merged | HasAttrFlags::Overlapped ) )
        {
            nCellX = *oLastEmptyCellX;
            bDoCell = true;
        }
    }

    //  normal visible cell

    if (!bEmpty)
        bDoCell = true;

    //  don't output the cell that's being edited

    if ( bDoCell && mbEditMode && nCellX == mnEditCol && nCellY == mnEditRow )
        bDoCell = false;

    // skip text in cell if data bar/icon set is set and only value selected
    if ( bDoCell )
    {
        if(pInfo->pDataBar && !pInfo->pDataBar->mbShowValue)
            bDoCell = false;
        if(pInfo->pIconSet && !pInfo->pIconSet->mbShowValue)
            bDoCell = false;
    }

    //  output the cell text

    ScRefCellValue aCell;
    if (bDoCell)
    {
        if ( nCellY == nY && nCellX == nX && nCellX >= mnX1 && nCellX <= mnX2 )
            aCell = pThisRowInfo->cellInfo(nCellX).maCell;
        else
            GetVisibleCell( nCellX, nCellY, mnTab, aCell );      // get from document
        if (aCell.isEmpty())
            bDoCell = false;
        else if (aCell.getType() == CELLTYPE_EDIT)
            bUseEditEngine = true;
    }

    // Check if this cell is mis-spelled.
    if (bDoCell && !bUseEditEngine && aCell.getType() == CELLTYPE_STRING)
    {
        if (mpSpellCheckCxt && mpSpellCheckCxt->isMisspelled(nCellX, nCellY))
            bUseEditEngine = true;
    }

    if (bDoCell && !bUseEditEngine)
    {
        if ( nCellY == nY && nCellX >= mnX1 && nCellX <= mnX2 )
        {
            ScCellInfo& rCellInfo = pThisRowInfo->cellInfo(nCellX);
            pPattern = rCellInfo.pPatternAttr;
            pCondSet = rCellInfo.pConditionSet;

            if ( !pPattern )
            {
                // #i68085# pattern from cell info for hidden columns is null,
                // test for null is quicker than using column flags
                pPattern = mpDoc->GetPattern( nCellX, nCellY, mnTab );
                pCondSet = mpDoc->GetCondResult( nCellX, nCellY, mnTab );
            }
        }
        else        // get from document
        {
            pPattern = mpDoc->GetPattern( nCellX, nCellY, mnTab );
            pCondSet = mpDoc->GetCondResult( nCellX, nCellY, mnTab );
        }
        if ( mpDoc->GetPreviewFont() || mpDoc->GetPreviewCellStyle() )
        {
            aAltPatterns.push_back(std::make_unique<ScPatternAttr>(*pPattern));
            ScPatternAttr* pAltPattern = aAltPatterns.back().get();
            if (  ScStyleSheet* pPreviewStyle = mpDoc->GetPreviewCellStyle( nCellX, nCellY, mnTab ) )
            {
                pAltPattern->SetStyleSheet(pPreviewStyle);
            }
            else if ( SfxItemSet* pFontSet = mpDoc->GetPreviewFont( nCellX, nCellY, mnTab ) )
            {
                if ( const SvxFontItem* pItem = pFontSet->GetItemIfSet( ATTR_FONT ) )
                    pAltPattern->GetItemSet().Put( *pItem );
                if ( const SvxFontItem* pItem = pFontSet->GetItemIfSet( ATTR_CJK_FONT ) )
                    pAltPattern->GetItemSet().Put( *pItem );
                if ( const SvxFontItem* pItem = pFontSet->GetItemIfSet( ATTR_CTL_FONT ) )
                    pAltPattern->GetItemSet().Put( *pItem );
            }
            pPattern = pAltPattern;
        }

        if (aCell.hasNumeric() &&
                pPattern->GetItem(ATTR_LINEBREAK, pCondSet).GetValue())
        {
            // Disable line break when the cell content is numeric.
            aAltPatterns.push_back(std::make_unique<ScPatternAttr>(*pPattern));
            ScPatternAttr* pAltPattern = aAltPatterns.back().get();
            ScLineBreakCell aLineBreak(false);
            pAltPattern->GetItemSet().Put(aLineBreak);
            pPattern = pAltPattern;
        }

        SvtScriptType nScript = mpDoc->GetCellScriptType(
            ScAddress(nCellX, nCellY, mnTab),
            pPattern->GetNumberFormat(mpDoc->GetFormatTable(), pCondSet));

        if (nScript == SvtScriptType::NONE)
            nScript = ScGlobal::GetDefaultScriptType();

        if ( !ScPatternAttr::areSame(pPattern, pOldPattern) || pCondSet != pOldCondSet ||
             nScript != nOldScript || mbSyntaxMode )
        {
            if ( StringDiffer(pOldPattern,pPattern) ||
                 pCondSet != pOldCondSet || nScript != nOldScript || mbSyntaxMode )
            {
                aVars.SetPattern(pPattern, pCondSet, aCell, nScript);
            }
            else
                aVars.SetPatternSimple( pPattern, pCondSet );
            pOldPattern = pPattern;
            pOldCondSet = pCondSet;
            nOldScript = nScript;
        }

        //  use edit engine for rotated, stacked or mixed-script text
        if ( aVars.GetOrient() == SvxCellOrientation::Stacked ||
             aVars.IsRotated() || IsAmbiguousScript(nScript) )
            bUseEditEngine = true;
    }
    if (bDoCell && !bUseEditEngine)
    {
        bool bFormulaCell = (aCell.getType() == CELLTYPE_FORMULA);
        if ( bFormulaCell )
            lcl_CreateInterpretProgress(bProgress, mpDoc, aCell.getFormula());
        if ( aVars.SetText(aCell) )
            pOldPattern = nullptr;
        bUseEditEngine = aVars.HasEditCharacters() || (bFormulaCell && aCell.getFormula()->IsMultilineResult());
    }
    tools::Long nTotalMargin = 0;
    SvxCellHorJustify eOutHorJust = SvxCellHorJustify::Standard;
    if (bDoCell && !bUseEditEngine)
    {
        CellType eCellType = aCell.getType();
        bCellIsValue = ( eCellType == CELLTYPE_VALUE );
        if ( eCellType == CELLTYPE_FORMULA )
        {
            ScFormulaCell* pFCell = aCell.getFormula();
            bCellIsValue = pFCell->IsRunning() || pFCell->IsValue();
        }

        const bool bNumberFormatIsText = lcl_isNumberFormatText( mpDoc, nCellX, nCellY, mnTab );
        eOutHorJust = getAlignmentFromContext( aVars.GetHorJust(), bCellIsValue, aVars.GetString(),
                *pPattern, pCondSet, mpDoc, mnTab, bNumberFormatIsText );

        bool bBreak = ( aVars.GetLineBreak() || aVars.GetHorJust() == SvxCellHorJustify::Block );
        // #i111387# #o11817313# tdf#121040 disable automatic line breaks for all number formats
        // Must be synchronized with ScColumn::GetNeededSize()
        SvNumberFormatter* pFormatter = mpDoc->GetFormatTable();
        if (bBreak && bCellIsValue && (pFormatter->GetType(aVars.GetResultValueFormat()) == SvNumFormatType::NUMBER))
            bBreak = false;

        bool bRepeat = aVars.IsRepeat() && !bBreak;
        bool bShrink = aVars.IsShrink() && !bBreak && !bRepeat;

        nTotalMargin =
            static_cast<tools::Long>(aVars.GetLeftTotal() * mnPPTX) +
            static_cast<tools::Long>(aVars.GetMargin()->GetRightMargin() * mnPPTX);

        nNeededWidth = aVars.GetTextSize().Width() + nTotalMargin;

        // GetOutputArea gives justified rectangles
        GetOutputArea( nX, nArrY, nPosX, nPosY, nCellX, nCellY, nNeededWidth,
                       *pPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                       bCellIsValue || bRepeat || bShrink, bBreak, false,
                       aAreaParam );

        aVars.RepeatToFill( aAreaParam.mnColWidth - nTotalMargin );
        if ( bShrink )
        {
            if ( aVars.GetOrient() != SvxCellOrientation::Standard )
            {
                // Only horizontal scaling is handled here.
                // DrawEdit is used to vertically scale 90 deg rotated text.
                bUseEditEngine = true;
            }
            else if ( aAreaParam.mbLeftClip || aAreaParam.mbRightClip )     // horizontal
            {
                tools::Long nAvailable = aAreaParam.maAlignRect.GetWidth() - nTotalMargin;
                tools::Long nScaleSize = aVars.GetTextSize().Width();         // without margin

                if ( nAvailable > 0 && nScaleSize > 0 )       // 0 if the text is empty (formulas, number formats)
                {
                    tools::Long nScale = ( nAvailable * 100 ) / nScaleSize;

                    aVars.SetShrinkScale( nScale, nOldScript );
                    tools::Long nNewSize = aVars.GetTextSize().Width();

                    sal_uInt16 nShrinkAgain = 0;
                    while ( nNewSize > nAvailable && nShrinkAgain < SC_SHRINKAGAIN_MAX )
                    {
                        // If the text is still too large, reduce the scale again by 10%, until it fits,
                        // at most 7 times (it's less than 50% of the calculated scale then).

                        nScale = ( nScale * 9 ) / 10;
                        aVars.SetShrinkScale( nScale, nOldScript );
                        nNewSize = aVars.GetTextSize().Width();
                        ++nShrinkAgain;
                    }
                    // If even at half the size the font still isn't rendered smaller,
                    // fall back to normal clipping (showing ### for numbers).
                    if ( nNewSize <= nAvailable )
                    {
                        // Reset relevant parameters.
                        aAreaParam.mbLeftClip = aAreaParam.mbRightClip = false;
                        aAreaParam.mnLeftClipLength = aAreaParam.mnRightClipLength = 0;
                    }

                    pOldPattern = nullptr;
                }
            }
        }

        if ( bRepeat && !aAreaParam.mbLeftClip && !aAreaParam.mbRightClip )
        {
            tools::Long nAvailable = aAreaParam.maAlignRect.GetWidth() - nTotalMargin;
            tools::Long nRepeatSize = aVars.GetTextSize().Width();         // without margin
            // When formatting for the printer, the text sizes don't always add up.
            // Round down (too few repetitions) rather than exceeding the cell size then:
            if ( pFmtDevice != mpRefDevice )
                ++nRepeatSize;
            if ( nRepeatSize > 0 )
            {
                tools::Long nRepeatCount = nAvailable / nRepeatSize;
                if ( nRepeatCount > 1 )
                {
                    OUString aCellStr = aVars.GetString();
                    OUStringBuffer aRepeated(aCellStr);
                    for ( tools::Long nRepeat = 1; nRepeat < nRepeatCount; nRepeat++ )
                        aRepeated.append(aCellStr);
                    aVars.SetAutoText( aRepeated.makeStringAndClear() );
                }
            }
        }

        //  use edit engine if automatic line breaks are needed
        if ( bBreak )
        {
            if ( aVars.GetOrient() == SvxCellOrientation::Standard )
                bUseEditEngine = ( aAreaParam.mbLeftClip || aAreaParam.mbRightClip );
            else
            {
                tools::Long nHeight = aVars.GetTextSize().Height() +
                                static_cast<tools::Long>(aVars.GetMargin()->GetTopMargin()*mnPPTY) +
                                static_cast<tools::Long>(aVars.GetMargin()->GetBottomMargin()*mnPPTY);
                bUseEditEngine = ( nHeight > aAreaParam.maClipRect.GetHeight() );
            }
        }
        if (!bUseEditEngine)
        {
            bUseEditEngine =
                aVars.GetHorJust() == SvxCellHorJustify::Block &&
                aVars.GetHorJustMethod() == SvxCellJustifyMethod::Distribute;
        }
    }
    if (bUseEditEngine)
    {
        //  mark the cell in ScCellInfo to be drawn in DrawEdit:
        //  Cells to the left are marked directly, cells to the
        //  right are handled by the flag for mnX2
        SCCOL nMarkX = ( nCellX <= mnX2 ) ? nCellX : mnX2;
        pThisRowInfo->basicCellInfo(nMarkX).bEditEngine = true;
        bDoCell = false;    // don't draw here

        // Mark the tagged "TD" structure element to be drawn in DrawEdit
        if (bTaggedPDF)
        {
            if (bReopenRowTag)
                ReopenPDFStructureElement(vcl::pdf::StructElement::TableRow, nY);
            else
            {
                sal_Int32 nId = pPDF->EnsureStructureElement(nullptr);
                pPDF->InitStructureElement(nId, vcl::pdf::StructElement::TableRow,
                                           u"TR"_ustr);
                pPDF->BeginStructureElement(nId);
                pPDF->GetScPDFState()->m_TableRowMap.emplace(nY, nId);
                bReopenRowTag = true;
            }

            pPDF->WrapBeginStructureElement(vcl::pdf::StructElement::TableData,
                                            u"TD"_ustr);

            sal_Int32 nId = pPDF->GetCurrentStructureElement();
            pPDF->GetScPDFState()->m_TableDataMap[{ nY, nX }] = nId;

            pPDF->EndStructureElement(); // TableData
            pPDF->EndStructureElement(); // TableRow
        }
    }
    if ( bDoCell )
    {
        if ( bCellIsValue && ( aAreaParam.mbLeftClip || aAreaParam.mbRightClip ) )
        {
            bool bHasHashText = false;
            if (mbShowFormulas)
            {
                aVars.SetHashText();
                bHasHashText = true;
            }
            else
                // Adjust the decimals to fit the available column width.
                bHasHashText = aVars.SetTextToWidthOrHash( aCell, aAreaParam.mnColWidth - nTotalMargin );

            if ( bHasHashText )
            {
                tools::Long nMarkPixel = SC_CLIPMARK_SIZE * mnPPTX;

                if ( eOutHorJust == SvxCellHorJustify::Left )
                {
                    if ( nCellY == nY && nCellX >= mnX1 && nCellX <= mnX2 )
                        mpRowInfo[nArrY].cellInfo(nCellX).nClipMark |= ScClipMark::Right;
                    mbAnyClipped = true;
                    aAreaParam.maClipRect.AdjustRight( -(nMarkPixel * nLayoutSign) );
                }
                else if ( eOutHorJust == SvxCellHorJustify::Right )
                {
                    if ( nCellY == nY && nCellX >= mnX1 && nCellX <= mnX2 )
                        mpRowInfo[nArrY].cellInfo(nCellX).nClipMark |= ScClipMark::Left;
                    mbAnyClipped = true;
                    aAreaParam.maClipRect.AdjustLeft(nMarkPixel * nLayoutSign);
                }
                else
                {
                    if ( nCellY == nY && nCellX >= mnX1 && nCellX <= mnX2 )
                    {
                        mpRowInfo[nArrY].cellInfo(nCellX).nClipMark |= ScClipMark::Right;
                        mpRowInfo[nArrY].cellInfo(nCellX).nClipMark |= ScClipMark::Left;
                    }
                    mbAnyClipped = true;
                    aAreaParam.maClipRect.AdjustRight( -(nMarkPixel * nLayoutSign) );
                    aAreaParam.maClipRect.AdjustLeft(nMarkPixel * nLayoutSign);
                }
            }

            nNeededWidth = aVars.GetTextSize().Width() +
                        static_cast<tools::Long>( aVars.GetLeftTotal() * mnPPTX ) +
                        static_cast<tools::Long>( aVars.GetMargin()->GetRightMargin() * mnPPTX );
            if ( nNeededWidth <= aAreaParam.maClipRect.GetWidth() )
            {
                // Cell value is no longer clipped.  Reset relevant parameters.
                aAreaParam.mbLeftClip = aAreaParam.mbRightClip = false;
                aAreaParam.mnLeftClipLength = aAreaParam.mnRightClipLength = 0;
            }
        }

        tools::Long nJustPosX = aAreaParam.maAlignRect.Left();     // "justified" - effect of alignment will be added
        tools::Long nJustPosY = aAreaParam.maAlignRect.Top();
        tools::Long nAvailWidth = aAreaParam.maAlignRect.GetWidth();
        tools::Long nOutHeight = aAreaParam.maAlignRect.GetHeight();

        bool bOutside = ( aAreaParam.maClipRect.Right() < mnScrX || aAreaParam.maClipRect.Left() >= mnScrX + mnScrW );
        // Take adjusted values of aAreaParam.mbLeftClip and aAreaParam.mbRightClip
        bool bVClip = AdjustAreaParamClipRect(aAreaParam);
        bool bHClip = aAreaParam.mbLeftClip || aAreaParam.mbRightClip;

        // check horizontal space

        if ( !bOutside )
        {
            bool bRightAdjusted = false;        // to correct text width calculation later
            switch (eOutHorJust)
            {
                case SvxCellHorJustify::Left:
                    nJustPosX += static_cast<tools::Long>( aVars.GetLeftTotal() * mnPPTX );
                    break;
                case SvxCellHorJustify::Right:
                    nJustPosX += nAvailWidth - aVars.GetTextSize().Width() -
                                static_cast<tools::Long>( aVars.GetRightTotal() * mnPPTX );
                    bRightAdjusted = true;
                    break;
                case SvxCellHorJustify::Center:
                    nJustPosX += ( nAvailWidth - aVars.GetTextSize().Width() +
                                static_cast<tools::Long>( aVars.GetLeftTotal() * mnPPTX ) -
                                static_cast<tools::Long>( aVars.GetMargin()->GetRightMargin() * mnPPTX ) ) / 2;
                    break;
                default:
                {
                    // added to avoid warnings
                }
            }

            tools::Long nTestClipHeight = aVars.GetTextSize().Height();
            switch (aVars.GetVerJust())
            {
                case SvxCellVerJustify::Top:
                case SvxCellVerJustify::Block:
                    {
                        tools::Long nTop = static_cast<tools::Long>( aVars.GetMargin()->GetTopMargin() * mnPPTY );
                        nJustPosY += nTop;
                        nTestClipHeight += nTop;
                    }
                    break;
                case SvxCellVerJustify::Bottom:
                    {
                        tools::Long nBot = static_cast<tools::Long>( aVars.GetMargin()->GetBottomMargin() * mnPPTY );
                        nJustPosY += nOutHeight - aVars.GetTextSize().Height() - nBot;
                        nTestClipHeight += nBot;
                    }
                    break;
                case SvxCellVerJustify::Center:
                    {
                        tools::Long nTop = static_cast<tools::Long>( aVars.GetMargin()->GetTopMargin() * mnPPTY );
                        tools::Long nBot = static_cast<tools::Long>( aVars.GetMargin()->GetBottomMargin() * mnPPTY );
                        nJustPosY += ( nOutHeight + nTop -
                                        aVars.GetTextSize().Height() - nBot ) / 2;
                        nTestClipHeight += std::abs( nTop - nBot );
                    }
                    break;
                default:
                {
                    // added to avoid warnings
                }
            }

            if ( nTestClipHeight > nOutHeight )
            {
                // no vertical clipping when printing cells with optimal height,
                // except when font size is from conditional formatting.
                if ( meType != OUTTYPE_PRINTER ||
                        ( mpDoc->GetRowFlags( nCellY, mnTab ) & CRFlags::ManualSize ) ||
                        ( aVars.HasCondHeight() ) )
                    bVClip = true;
            }

            if ( bHClip || bVClip )
            {
                // only clip the affected dimension so that not all right-aligned
                // columns are cut off when performing a non-proportional resize
                if (!bHClip)
                {
                    aAreaParam.maClipRect.SetLeft( mnScrX );
                    aAreaParam.maClipRect.SetRight( mnScrX+mnScrW );
                }
                if (!bVClip)
                {
                    aAreaParam.maClipRect.SetTop( mnScrY );
                    aAreaParam.maClipRect.SetBottom( mnScrY+mnScrH );
                }

                //  aClipRect is not used after SetClipRegion/IntersectClipRegion,
                //  so it can be modified here
                if (bPixelToLogic)
                    aAreaParam.maClipRect = mpRefDevice->PixelToLogic( aAreaParam.maClipRect );

                if (mbMetaFile)
                {
                    mpDev->Push();
                    mpDev->IntersectClipRegion( aAreaParam.maClipRect );
                }
                else
                    mpDev->SetClipRegion( vcl::Region( aAreaParam.maClipRect ) );
            }

            Point aURLStart( nJustPosX, nJustPosY );    // copy before modifying for orientation

            switch (aVars.GetOrient())
            {
                case SvxCellOrientation::Standard:
                    nJustPosY += aVars.GetAscent();
                    break;
                case SvxCellOrientation::TopBottom:
                    nJustPosX += aVars.GetTextSize().Width() - aVars.GetAscent();
                    break;
                case SvxCellOrientation::BottomUp:
                    nJustPosY += aVars.GetTextSize().Height();
                    nJustPosX += aVars.GetAscent();
                    break;
                default:
                {
                    // added to avoid warnings
                }
            }

            // When clipping, the visible part is now completely defined by the alignment,
            // there's no more special handling to show the right part of RTL text.

            Point aDrawTextPos( nJustPosX, nJustPosY );
            if ( bPixelToLogic )
            {
                //  undo text width adjustment in pixels
                if (bRightAdjusted)
                    aDrawTextPos.AdjustX(aVars.GetTextSize().Width() );

                aDrawTextPos = mpRefDevice->PixelToLogic( aDrawTextPos );

                //  redo text width adjustment in logic units
                if (bRightAdjusted)
                    aDrawTextPos.AdjustX( -(aVars.GetOriginalWidth()) );
            }

            // in Metafiles always use DrawTextArray to ensure that positions are
            // recorded (for non-proportional resize):

            const OUString& aString = aVars.GetString();
            if (!aString.isEmpty())
            {
                if (bTaggedPDF)
                {
                    if (bReopenRowTag)
                        ReopenPDFStructureElement(vcl::pdf::StructElement::TableRow,
                                                  nY);
                    else
                    {
                        sal_Int32 nId = pPDF->EnsureStructureElement(nullptr);
                        pPDF->InitStructureElement(
                            nId, vcl::pdf::StructElement::TableRow, u"TR"_ustr);
                        pPDF->BeginStructureElement(nId);
                        pPDF->GetScPDFState()->m_TableRowMap.emplace(nY, nId);
                        bReopenRowTag = true;
                    }

                    pPDF->WrapBeginStructureElement(vcl::pdf::StructElement::TableData,
                                                    u"TD"_ustr);
                    pPDF->WrapBeginStructureElement(vcl::pdf::StructElement::Paragraph,
                                                    u"P"_ustr);
                }

                // If the string is clipped, make it shorter for
                // better performance since drawing by HarfBuzz is
                // quite expensive especially for long string.

                OUString aShort = aString;

                // But never fiddle with numeric values.
                // (Which was the cause of tdf#86024).
                // The General automatic format output takes
                // care of this, or fixed width numbers either fit
                // or display as ###.
                if (!bCellIsValue)
                {
                    double fVisibleRatio = 1.0;
                    double fTextWidth = aVars.GetTextSize().Width();
                    sal_Int32 nTextLen = aString.getLength();
                    if (eOutHorJust == SvxCellHorJustify::Left && aAreaParam.mnRightClipLength > 0)
                    {
                        fVisibleRatio = (fTextWidth - aAreaParam.mnRightClipLength) / fTextWidth;
                        if (0.0 < fVisibleRatio && fVisibleRatio < 1.0)
                        {
                            // Only show the left-end segment.
                            sal_Int32 nShortLen = fVisibleRatio*nTextLen + 1;
                            aShort = aShort.copy(0, nShortLen);
                        }
                    }
                    else if (eOutHorJust == SvxCellHorJustify::Right && aAreaParam.mnLeftClipLength > 0)
                    {
                        fVisibleRatio = (fTextWidth - aAreaParam.mnLeftClipLength) / fTextWidth;
                        if (0.0 < fVisibleRatio && fVisibleRatio < 1.0)
                        {
                            // Only show the right-end segment.
                            sal_Int32 nShortLen = fVisibleRatio*nTextLen + 1;
                            aShort = aShort.copy(nTextLen-nShortLen);

                            // Adjust the text position after shortening of the string.
                            double fShortWidth = aVars.GetFmtTextWidth(aShort);
                            double fOffset = fTextWidth - fShortWidth;
                            aDrawTextPos.Move(fOffset, 0);
                        }
                    }
                }

                if (mbMetaFile || pFmtDevice != mpDev || maZoomX != maZoomY)
                {
                    size_t nLen = aShort.getLength();
                    if (aDX.size() < nLen)
                        aDX.resize(nLen, 0);

                    pFmtDevice->GetTextArray(aShort, &aDX);

                    if ( !mpRefDevice->GetConnectMetaFile() ||
                            mpRefDevice->GetOutDevType() == OUTDEV_PRINTER )
                    {
                        double fMul = GetStretch();
                        for (size_t i = 0; i < nLen; ++i)
                            aDX[i] /= fMul;
                    }

                    mpDev->DrawTextArray(aDrawTextPos, aShort, aDX, {}, 0, nLen);
                }
                else
                {
                    mpDev->DrawText(aDrawTextPos, aShort, 0, -1, nullptr, nullptr,
                        aVars.GetLayoutGlyphs(aShort));
                }
                if (bTaggedPDF)
                {
                    pPDF->EndStructureElement(); // Paragraph
                    pPDF->EndStructureElement(); // TableData
                    pPDF->EndStructureElement(); // TableRow
                }
            }

            if ( bHClip || bVClip )
            {
                if (mbMetaFile)
                    mpDev->Pop();
                else
                    mpDev->SetClipRegion();
            }

            // PDF: whole-cell hyperlink from formula?
            bool bHasURL = pPDF && aCell.getType() == CELLTYPE_FORMULA && aCell.getFormula()->IsHyperLinkCell();
            if (bHasURL)
            {
                tools::Rectangle aURLRect( aURLStart, aVars.GetTextSize() );
                lcl_DoHyperlinkResult(mpDev, aURLRect, aCell);
            }
        }
    }
}

void ScOutputData::SetRefDevice( OutputDevice* pRDev )
{
    mpRefDevice = pFmtDevice = pRDev;
    // reset EditEngine because it depends on pFmtDevice and mpRefDevice
    mxOutputEditEngine.reset();
}

void ScOutputData::SetFmtDevice( OutputDevice* pRDev )
{
    pFmtDevice = pRDev;
    // reset EditEngine because it depends on pFmtDevice
    mxOutputEditEngine.reset();
}

void ScOutputData::SetUseStyleColor( bool bSet )
{
    mbUseStyleColor = bSet;
    // reset EditEngine because it depends on mbUseStyleColor
    mxOutputEditEngine.reset();
}

void ScOutputData::InitOutputEditEngine()
{
    if (!mxOutputEditEngine)
    {
        mxOutputEditEngine = std::make_unique<ScFieldEditEngine>(mpDoc, mpDoc->GetEditEnginePool());
        mxOutputEditEngine->SetUpdateLayout( false );
        mxOutputEditEngine->EnableUndo( false ); // don't need undo for painting purposes
        // a RefDevice always has to be set, otherwise EditEngine would create a VirtualDevice
        mxOutputEditEngine->SetRefDevice( pFmtDevice );
        EEControlBits nCtrl = mxOutputEditEngine->GetControlWord();
        if ( mbShowSpellErrors )
            nCtrl |= EEControlBits::ONLINESPELLING;
        if ( meType == OUTTYPE_PRINTER )
            nCtrl &= ~EEControlBits::MARKFIELDS;
        else
            nCtrl &= ~EEControlBits::MARKURLFIELDS;   // URLs not shaded for output
        mxOutputEditEngine->SetControlWord( nCtrl );
        mxOutputEditEngine->EnableAutoColor( mbUseStyleColor );
    }
    else
    {
        // just in case someone turned it on during the last paint cycle
        mxOutputEditEngine->SetUpdateLayout( false );
    }
    // we don't track changes to these settings, so we have to apply them every time
    mpDoc->ApplyAsianEditSettings( *mxOutputEditEngine );
    mxOutputEditEngine->SetDefaultHorizontalTextDirection( mpDoc->GetEditTextDirection( mnTab ) );
}

static void lcl_ClearEdit( EditEngine& rEngine )       // text and attributes
{
    rEngine.SetUpdateLayout( false );

    rEngine.SetText(OUString());
    // do not keep any para-attributes
    const SfxItemSet& rPara = rEngine.GetParaAttribs(0);
    if (rPara.Count())
        rEngine.SetParaAttribs( 0,
                    SfxItemSet( *rPara.GetPool(), rPara.GetRanges() ) );
    rEngine.EnableSkipOutsideFormat(false);
}

static bool lcl_SafeIsValue( const ScRefCellValue& rCell )
{
    switch (rCell.getType())
    {
        case CELLTYPE_VALUE:
            return true;
        case CELLTYPE_FORMULA:
        {
            ScFormulaCell* pFCell = rCell.getFormula();
            if (pFCell->IsRunning() || pFCell->IsValue())
                return true;
        }
        break;
        default:
        {
            // added to avoid warnings
        }
    }
    return false;
}

static void lcl_ScaleFonts( EditEngine& rEngine, tools::Long nPercent )
{
    bool bUpdateMode = rEngine.SetUpdateLayout( false );

    sal_Int32 nParCount = rEngine.GetParagraphCount();
    for (sal_Int32 nPar=0; nPar<nParCount; nPar++)
    {
        std::vector<sal_Int32> aPortions;
        rEngine.GetPortions( nPar, aPortions );

        sal_Int32 nStart = 0;
        for ( const sal_Int32 nEnd : aPortions )
        {
            ESelection aSel( nPar, nStart, nPar, nEnd );
            SfxItemSet aAttribs = rEngine.GetAttribs( aSel );

            tools::Long nWestern = aAttribs.Get(EE_CHAR_FONTHEIGHT).GetHeight();
            tools::Long nCJK = aAttribs.Get(EE_CHAR_FONTHEIGHT_CJK).GetHeight();
            tools::Long nCTL = aAttribs.Get(EE_CHAR_FONTHEIGHT_CTL).GetHeight();

            nWestern = ( nWestern * nPercent ) / 100;
            nCJK     = ( nCJK     * nPercent ) / 100;
            nCTL     = ( nCTL     * nPercent ) / 100;

            aAttribs.Put( SvxFontHeightItem( nWestern, 100, EE_CHAR_FONTHEIGHT ) );
            aAttribs.Put( SvxFontHeightItem( nCJK, 100, EE_CHAR_FONTHEIGHT_CJK ) );
            aAttribs.Put( SvxFontHeightItem( nCTL, 100, EE_CHAR_FONTHEIGHT_CTL ) );

            rEngine.QuickSetAttribs( aAttribs, aSel );      //! remove paragraph attributes from aAttribs?

            nStart = nEnd;
        }
    }

    if ( bUpdateMode )
        rEngine.SetUpdateLayout( true );
}

static tools::Long lcl_GetEditSize( EditEngine& rEngine, bool bWidth, bool bSwap, Degree100 nAttrRotate )
{
    if ( bSwap )
        bWidth = !bWidth;

    if ( nAttrRotate )
    {
        tools::Long nRealWidth  = static_cast<tools::Long>(rEngine.CalcTextWidth());
        tools::Long nRealHeight = rEngine.GetTextHeight();

        // assuming standard mode, otherwise width isn't used

        double nRealOrient = toRadians(nAttrRotate);   // 1/100th degrees
        double nAbsCos = fabs( cos( nRealOrient ) );
        double nAbsSin = fabs( sin( nRealOrient ) );
        if ( bWidth )
            return static_cast<tools::Long>( nRealWidth * nAbsCos + nRealHeight * nAbsSin );
        else
            return static_cast<tools::Long>( nRealHeight * nAbsCos + nRealWidth * nAbsSin );
    }
    else if ( bWidth )
        return static_cast<tools::Long>(rEngine.CalcTextWidth());
    else
        return rEngine.GetTextHeight();
}

void ScOutputData::ShrinkEditEngine( EditEngine& rEngine, const tools::Rectangle& rAlignRect,
            tools::Long nLeftM, tools::Long nTopM, tools::Long nRightM, tools::Long nBottomM,
            bool bWidth, SvxCellOrientation nOrient, Degree100 nAttrRotate, bool bPixelToLogic,
            tools::Long& rEngineWidth, tools::Long& rEngineHeight, tools::Long& rNeededPixel, bool& rLeftClip, bool& rRightClip )
{
    if ( !bWidth )
    {
        // vertical

        tools::Long nScaleSize = bPixelToLogic ?
            mpRefDevice->LogicToPixel(Size(0,rEngineHeight)).Height() : rEngineHeight;

        // Don't scale if it fits already.
        // Allowing to extend into the margin, to avoid scaling at optimal height.
        if ( nScaleSize <= rAlignRect.GetHeight() )
            return;

        bool bSwap = ( nOrient == SvxCellOrientation::TopBottom || nOrient == SvxCellOrientation::BottomUp );
        tools::Long nAvailable = rAlignRect.GetHeight() - nTopM - nBottomM;
        tools::Long nScale = ( nAvailable * 100 ) / nScaleSize;

        lcl_ScaleFonts( rEngine, nScale );
        rEngineHeight = lcl_GetEditSize( rEngine, false, bSwap, nAttrRotate );
        tools::Long nNewSize = bPixelToLogic ?
            mpRefDevice->LogicToPixel(Size(0,rEngineHeight)).Height() : rEngineHeight;

        sal_uInt16 nShrinkAgain = 0;
        while ( nNewSize > nAvailable && nShrinkAgain < SC_SHRINKAGAIN_MAX )
        {
            // further reduce, like in DrawStrings
            lcl_ScaleFonts( rEngine, 90 );     // reduce by 10%
            rEngineHeight = lcl_GetEditSize( rEngine, false, bSwap, nAttrRotate );
            nNewSize = bPixelToLogic ?
                mpRefDevice->LogicToPixel(Size(0,rEngineHeight)).Height() : rEngineHeight;
            ++nShrinkAgain;
        }

        // sizes for further processing (alignment etc):
        rEngineWidth = lcl_GetEditSize( rEngine, true, bSwap, nAttrRotate );
        tools::Long nPixelWidth = bPixelToLogic ?
            mpRefDevice->LogicToPixel(Size(rEngineWidth,0)).Width() : rEngineWidth;
        rNeededPixel = nPixelWidth + nLeftM + nRightM;
    }
    else if ( rLeftClip || rRightClip )
    {
        // horizontal

        tools::Long nAvailable = rAlignRect.GetWidth() - nLeftM - nRightM;
        tools::Long nScaleSize = rNeededPixel - nLeftM - nRightM;      // without margin

        if ( nScaleSize <= nAvailable )
            return;

        tools::Long nScale = ( nAvailable * 100 ) / nScaleSize;

        lcl_ScaleFonts( rEngine, nScale );
        rEngineWidth = lcl_GetEditSize( rEngine, true, false, nAttrRotate );
        tools::Long nNewSize = bPixelToLogic ?
            mpRefDevice->LogicToPixel(Size(rEngineWidth,0)).Width() : rEngineWidth;

        sal_uInt16 nShrinkAgain = 0;
        while ( nNewSize > nAvailable && nShrinkAgain < SC_SHRINKAGAIN_MAX )
        {
            // further reduce, like in DrawStrings
            lcl_ScaleFonts( rEngine, 90 );     // reduce by 10%
            rEngineWidth = lcl_GetEditSize( rEngine, true, false, nAttrRotate );
            nNewSize = bPixelToLogic ?
                mpRefDevice->LogicToPixel(Size(rEngineWidth,0)).Width() : rEngineWidth;
            ++nShrinkAgain;
        }
        if ( nNewSize <= nAvailable )
            rLeftClip = rRightClip = false;

        // sizes for further processing (alignment etc):
        rNeededPixel = nNewSize + nLeftM + nRightM;
        rEngineHeight = lcl_GetEditSize( rEngine, false, false, nAttrRotate );
    }
}

ScOutputData::DrawEditParam::DrawEditParam(const ScPatternAttr* pPattern, const SfxItemSet* pCondSet, bool bCellIsValue) :
    meHorJustAttr( lcl_GetValue<SvxHorJustifyItem, SvxCellHorJustify>(*pPattern, ATTR_HOR_JUSTIFY, pCondSet) ),
    meHorJustContext( meHorJustAttr ),
    meHorJustResult( meHorJustAttr ),
    meVerJust( lcl_GetValue<SvxVerJustifyItem, SvxCellVerJustify>(*pPattern, ATTR_VER_JUSTIFY, pCondSet) ),
    meHorJustMethod( lcl_GetValue<SvxJustifyMethodItem, SvxCellJustifyMethod>(*pPattern, ATTR_HOR_JUSTIFY_METHOD, pCondSet) ),
    meVerJustMethod( lcl_GetValue<SvxJustifyMethodItem, SvxCellJustifyMethod>(*pPattern, ATTR_VER_JUSTIFY_METHOD, pCondSet) ),
    meOrient( pPattern->GetCellOrientation(pCondSet) ),
    mnArrY(0),
    mnX(0), mnCellX(0), mnCellY(0),
    mnPosX(0), mnPosY(0), mnInitPosX(0),
    mbBreak( (meHorJustAttr == SvxCellHorJustify::Block) || lcl_GetBoolValue(*pPattern, ATTR_LINEBREAK, pCondSet) ),
    mbCellIsValue(bCellIsValue),
    mbAsianVertical(false),
    mbPixelToLogic(false),
    mbHyphenatorSet(false),
    mpEngine(nullptr),
    mpPattern(pPattern),
    mpCondSet(pCondSet),
    mpPreviewFontSet(nullptr),
    mpOldPattern(nullptr),
    mpOldCondSet(nullptr),
    mpOldPreviewFontSet(nullptr),
    mpThisRowInfo(nullptr)
{}

bool ScOutputData::DrawEditParam::readCellContent(
    const ScDocument* pDoc, bool bShowNullValues, bool bShowFormulas, bool bSyntaxMode, bool bUseStyleColor, bool bForceAutoColor, bool& rWrapFields)
{
    if (maCell.getType() == CELLTYPE_EDIT)
    {
        const EditTextObject* pData = maCell.getEditText();
        if (pData)
        {
            mpEngine->SetTextCurrentDefaults(*pData);

            if ( mbBreak && !mbAsianVertical && pData->HasField() )
            {
                //  Fields aren't wrapped, so clipping is enabled to prevent
                //  a field from being drawn beyond the cell size

                rWrapFields = true;
            }
        }
        else
        {
            OSL_FAIL("pData == 0");
            return false;
        }
    }
    else
    {
        sal_uInt32 nFormat = mpPattern->GetNumberFormat(
                                    pDoc->GetFormatTable(), mpCondSet );
        const Color* pColor;
        OUString aString = ScCellFormat::GetString( maCell,
                                 nFormat, &pColor,
                                 nullptr,
                                 *pDoc,
                                 bShowNullValues,
                                 bShowFormulas);

        mpEngine->SetTextCurrentDefaults(aString);
        if ( pColor && !bSyntaxMode && !( bUseStyleColor && bForceAutoColor ) )
            lcl_SetEditColor( *mpEngine, *pColor );
    }

    if (maMisspellRanges.mpRanges)
        mpEngine->SetAllMisspellRanges(*maMisspellRanges.mpRanges);

    return true;
}

static Color GetConfBackgroundColor()
{
    if (const ScTabViewShell* pTabViewShellBg = ScTabViewShell::GetActiveViewShell())
        return pTabViewShellBg->GetViewRenderingData().GetDocColor();
    return ScModule::get()->GetColorConfig().GetColorValue(svtools::DOCCOLOR).nColor;
}

void ScOutputData::DrawEditParam::setPatternToEngine(bool bUseStyleColor)
{
    // syntax highlighting mode is ignored here
    // StringDiffer doesn't look at hyphenate, language items

    if (ScPatternAttr::areSame(mpPattern, mpOldPattern) && mpCondSet == mpOldCondSet && mpPreviewFontSet == mpOldPreviewFontSet )
        return;

    Color nConfBackColor = GetConfBackgroundColor();
    bool bCellContrast = bUseStyleColor &&
            Application::GetSettings().GetStyleSettings().GetHighContrastMode();

    SfxItemSet aSet( mpEngine->GetEmptyItemSet() );
    mpPattern->FillEditItemSet( &aSet, mpCondSet );
    if ( mpPreviewFontSet )
    {
        if ( const SvxFontItem* pItem = mpPreviewFontSet->GetItemIfSet( ATTR_FONT ) )
        {
            // tdf#125054 adapt WhichID
            aSet.PutAsTargetWhich(*pItem, EE_CHAR_FONTINFO);
        }
        if ( const SvxFontItem* pItem = mpPreviewFontSet->GetItemIfSet( ATTR_CJK_FONT ) )
        {
            // tdf#125054 adapt WhichID
            aSet.PutAsTargetWhich(*pItem, EE_CHAR_FONTINFO_CJK);
        }
        if ( const SvxFontItem* pItem = mpPreviewFontSet->GetItemIfSet( ATTR_CTL_FONT ) )
        {
            // tdf#125054 adapt WhichID
            aSet.PutAsTargetWhich(*pItem, EE_CHAR_FONTINFO_CTL);
        }
    }
    bool bParaHyphenate = aSet.Get(EE_PARA_HYPHENATE).GetValue();
    mpEngine->SetDefaults( std::move(aSet) );
    mpOldPattern = mpPattern;
    mpOldCondSet = mpCondSet;
    mpOldPreviewFontSet = mpPreviewFontSet;

    EEControlBits nControl = mpEngine->GetControlWord();
    if (meOrient == SvxCellOrientation::Stacked)
        nControl |= EEControlBits::ONECHARPERLINE;
    else
        nControl &= ~EEControlBits::ONECHARPERLINE;
    mpEngine->SetControlWord( nControl );

    if ( !mbHyphenatorSet && bParaHyphenate )
    {
        //  set hyphenator the first time it is needed
        css::uno::Reference<css::linguistic2::XHyphenator> xXHyphenator( LinguMgr::GetHyphenator() );
        mpEngine->SetHyphenator( xXHyphenator );
        mbHyphenatorSet = true;
    }

    Color aBackCol = mpPattern->GetItem( ATTR_BACKGROUND, mpCondSet ).GetColor();
    if ( bUseStyleColor && ( aBackCol.IsTransparent() || bCellContrast ) )
        aBackCol = nConfBackColor;
    mpEngine->SetBackgroundColor( aBackCol );
}

void ScOutputData::DrawEditParam::calcMargins(tools::Long& rTopM, tools::Long& rLeftM, tools::Long& rBottomM, tools::Long& rRightM, double nPPTX, double nPPTY) const
{
    const SvxMarginItem& rMargin = mpPattern->GetItem(ATTR_MARGIN, mpCondSet);

    sal_uInt16 nIndent = 0;
    if (meHorJustAttr == SvxCellHorJustify::Left || meHorJustAttr == SvxCellHorJustify::Right)
        nIndent = lcl_GetValue<ScIndentItem, sal_uInt16>(*mpPattern, ATTR_INDENT, mpCondSet);

    rLeftM   = static_cast<tools::Long>(((rMargin.GetLeftMargin() + nIndent) * nPPTX));
    rTopM    = static_cast<tools::Long>((rMargin.GetTopMargin() * nPPTY));
    rRightM  = static_cast<tools::Long>((rMargin.GetRightMargin() * nPPTX));
    rBottomM = static_cast<tools::Long>((rMargin.GetBottomMargin() * nPPTY));
    if(meHorJustAttr == SvxCellHorJustify::Right)
    {
        rLeftM   = static_cast<tools::Long>((rMargin.GetLeftMargin()  * nPPTX));
        rRightM  = static_cast<tools::Long>(((rMargin.GetRightMargin() + nIndent) * nPPTX));
    }
}

void ScOutputData::DrawEditParam::calcPaperSize(
    Size& rPaperSize, const tools::Rectangle& rAlignRect, double nPPTX, double nPPTY) const
{
    tools::Long nTopM, nLeftM, nBottomM, nRightM;
    calcMargins(nTopM, nLeftM, nBottomM, nRightM, nPPTX, nPPTY);

    if (isVerticallyOriented())
    {
        rPaperSize.setWidth( rAlignRect.GetHeight() - nTopM - nBottomM );
        rPaperSize.setHeight( rAlignRect.GetWidth() - nLeftM - nRightM );
    }
    else
    {
        rPaperSize.setWidth( rAlignRect.GetWidth() - nLeftM - nRightM );
        rPaperSize.setHeight( rAlignRect.GetHeight() - nTopM - nBottomM );
    }

    if (mbAsianVertical)
    {
        rPaperSize.setHeight( rAlignRect.GetHeight() - nTopM - nBottomM );
        // Subtract some extra value from the height or else the text would go
        // outside the cell area.  The value of 5 is arbitrary, and is based
        // entirely on heuristics.
        rPaperSize.AdjustHeight( -5 );
    }
}

void ScOutputData::DrawEditParam::getEngineSize(ScFieldEditEngine* pEngine, tools::Long& rWidth, tools::Long& rHeight) const
{
    tools::Long nEngineWidth = 0;
    if (!mbBreak || meOrient == SvxCellOrientation::Stacked || mbAsianVertical)
        nEngineWidth = static_cast<tools::Long>(pEngine->CalcTextWidth());

    tools::Long nEngineHeight = pEngine->GetTextHeight();

    if (isVerticallyOriented())
        std::swap( nEngineWidth, nEngineHeight );

    if (meOrient == SvxCellOrientation::Stacked)
        nEngineWidth = nEngineWidth * 11 / 10;

    rWidth = nEngineWidth;
    rHeight = nEngineHeight;
}

bool ScOutputData::DrawEditParam::hasLineBreak() const
{
    return (mbBreak || (meOrient == SvxCellOrientation::Stacked) || mbAsianVertical);
}

bool ScOutputData::DrawEditParam::isHyperlinkCell() const
{
    if (maCell.getType() != CELLTYPE_FORMULA)
        return false;

    return maCell.getFormula()->IsHyperLinkCell();
}

bool ScOutputData::DrawEditParam::isVerticallyOriented() const
{
    return (meOrient == SvxCellOrientation::TopBottom || meOrient == SvxCellOrientation::BottomUp);
}

void ScOutputData::DrawEditParam::calcStartPosForVertical(
    Point& rLogicStart, tools::Long nCellWidth, tools::Long nEngineWidth, tools::Long nTopM, const OutputDevice* pRefDevice)
{
    OSL_ENSURE(isVerticallyOriented(), "Use this only for vertically oriented cell!");

    if (mbPixelToLogic)
        rLogicStart = pRefDevice->PixelToLogic(rLogicStart);

    if (!mbBreak)
        return;

    // vertical adjustment is within the EditEngine
    if (mbPixelToLogic)
        rLogicStart.AdjustY(pRefDevice->PixelToLogic(Size(0,nTopM)).Height() );
    else
        rLogicStart.AdjustY(nTopM );

    switch (meHorJustResult)
    {
        case SvxCellHorJustify::Center:
            rLogicStart.AdjustX((nCellWidth - nEngineWidth) / 2 );
        break;
        case SvxCellHorJustify::Right:
            rLogicStart.AdjustX(nCellWidth - nEngineWidth );
        break;
        default:
            ; // do nothing
    }
}

void ScOutputData::DrawEditParam::setAlignmentToEngine()
{
    if (isVerticallyOriented() || mbAsianVertical)
    {
        SvxAdjust eSvxAdjust = SvxAdjust::Left;
        switch (meVerJust)
        {
            case SvxCellVerJustify::Top:
                eSvxAdjust = (meOrient == SvxCellOrientation::TopBottom || mbAsianVertical) ?
                            SvxAdjust::Left : SvxAdjust::Right;
                break;
            case SvxCellVerJustify::Center:
                eSvxAdjust = SvxAdjust::Center;
                break;
            case SvxCellVerJustify::Bottom:
            case SvxCellVerJustify::Standard:
                eSvxAdjust = (meOrient == SvxCellOrientation::TopBottom || mbAsianVertical) ?
                            SvxAdjust::Right : SvxAdjust::Left;
                break;
            case SvxCellVerJustify::Block:
                eSvxAdjust = SvxAdjust::Block;
                break;
        }

        mpEngine->SetDefaultItem( SvxAdjustItem(eSvxAdjust, EE_PARA_JUST) );
        mpEngine->SetDefaultItem( SvxJustifyMethodItem(meVerJustMethod, EE_PARA_JUST_METHOD) );

        if (meHorJustResult == SvxCellHorJustify::Block)
            mpEngine->SetDefaultItem( SvxVerJustifyItem(SvxCellVerJustify::Block, EE_PARA_VER_JUST) );
    }
    else
    {
        //  horizontal alignment now may depend on cell content
        //  (for values with number formats with mixed script types)
        //  -> always set adjustment

        SvxAdjust eSvxAdjust = SvxAdjust::Left;
        if (meOrient == SvxCellOrientation::Stacked)
            eSvxAdjust = SvxAdjust::Center;
        else if (mbBreak)
        {
            if (meOrient == SvxCellOrientation::Standard)
                switch (meHorJustResult)
                {
                    case SvxCellHorJustify::Repeat:            // repeat is not yet implemented
                    case SvxCellHorJustify::Standard:
                        SAL_WARN("sc.ui","meHorJustResult does not match getAlignmentFromContext()");
                        [[fallthrough]];
                    case SvxCellHorJustify::Left:
                        eSvxAdjust = SvxAdjust::Left;
                        break;
                    case SvxCellHorJustify::Center:
                        eSvxAdjust = SvxAdjust::Center;
                        break;
                    case SvxCellHorJustify::Right:
                        eSvxAdjust = SvxAdjust::Right;
                        break;
                    case SvxCellHorJustify::Block:
                        eSvxAdjust = SvxAdjust::Block;
                        break;
                }
            else
                switch (meVerJust)
                {
                    case SvxCellVerJustify::Top:
                        eSvxAdjust = SvxAdjust::Right;
                        break;
                    case SvxCellVerJustify::Center:
                        eSvxAdjust = SvxAdjust::Center;
                        break;
                    case SvxCellVerJustify::Bottom:
                    case SvxCellVerJustify::Standard:
                        eSvxAdjust = SvxAdjust::Left;
                        break;
                    case SvxCellVerJustify::Block:
                        eSvxAdjust = SvxAdjust::Block;
                        break;
                }
        }

        mpEngine->SetDefaultItem( SvxAdjustItem(eSvxAdjust, EE_PARA_JUST) );

        if (mbAsianVertical)
        {
            mpEngine->SetDefaultItem( SvxJustifyMethodItem(meVerJustMethod, EE_PARA_JUST_METHOD) );
            if (meHorJustResult == SvxCellHorJustify::Block)
                mpEngine->SetDefaultItem( SvxVerJustifyItem(SvxCellVerJustify::Block, EE_PARA_VER_JUST) );
        }
        else
        {
            mpEngine->SetDefaultItem( SvxJustifyMethodItem(meHorJustMethod, EE_PARA_JUST_METHOD) );
            if (meVerJust == SvxCellVerJustify::Block)
                mpEngine->SetDefaultItem( SvxVerJustifyItem(SvxCellVerJustify::Block, EE_PARA_VER_JUST) );
        }
    }

    mpEngine->SetVertical(mbAsianVertical);
    if (maCell.getType() == CELLTYPE_EDIT)
    {
        // We need to synchronize the vertical mode in the EditTextObject
        // instance too.  No idea why we keep this state in two separate
        // instances.
        const EditTextObject* pData = maCell.getEditText();
        if (pData)
            const_cast<EditTextObject*>(pData)->SetVertical(mbAsianVertical);
    }
}

bool ScOutputData::DrawEditParam::adjustHorAlignment(ScFieldEditEngine* pEngine)
{
    if (meHorJustResult == SvxCellHorJustify::Right || meHorJustResult == SvxCellHorJustify::Center)
    {
        SvxAdjust eEditAdjust = (meHorJustResult == SvxCellHorJustify::Center) ?
            SvxAdjust::Center : SvxAdjust::Right;

        const bool bPrevUpdateLayout = pEngine->SetUpdateLayout(false);
        pEngine->SetDefaultItem( SvxAdjustItem(eEditAdjust, EE_PARA_JUST) );
        pEngine->SetUpdateLayout(bPrevUpdateLayout);
        return true;
    }
    return false;
}

void ScOutputData::DrawEditParam::adjustForHyperlinkInPDF(Point aURLStart, const OutputDevice* pDev)
{
    // PDF: whole-cell hyperlink from formula?
    vcl::PDFExtOutDevData* pPDFData = dynamic_cast<vcl::PDFExtOutDevData* >( pDev->GetExtOutDevData() );
    bool bHasURL = pPDFData && isHyperlinkCell();
    if (!bHasURL)
        return;

    tools::Long nURLWidth = static_cast<tools::Long>(mpEngine->CalcTextWidth());
    tools::Long nURLHeight = mpEngine->GetTextHeight();
    if (mbBreak)
    {
        Size aPaper = mpEngine->GetPaperSize();
        if ( mbAsianVertical )
            nURLHeight = aPaper.Height();
        else
            nURLWidth = aPaper.Width();
    }
    if (isVerticallyOriented())
        std::swap( nURLWidth, nURLHeight );
    else if (mbAsianVertical)
        aURLStart.AdjustX( -nURLWidth );

    tools::Rectangle aURLRect( aURLStart, Size( nURLWidth, nURLHeight ) );
    lcl_DoHyperlinkResult(pDev, aURLRect, maCell);
}

// Returns true if the rect is clipped vertically
bool ScOutputData::AdjustAreaParamClipRect(OutputAreaParam& rAreaParam)
{
    if( rAreaParam.maClipRect.Left() < mnScrX )
    {
        rAreaParam.maClipRect.SetLeft( mnScrX );
        rAreaParam.mbLeftClip = true;
    }
    if( rAreaParam.maClipRect.Right() > mnScrX + mnScrW )
    {
        rAreaParam.maClipRect.SetRight( mnScrX + mnScrW );          //! minus one?
        rAreaParam.mbRightClip = true;
    }

    bool bVClip = false;

    if( rAreaParam.maClipRect.Top() < mnScrY )
    {
        rAreaParam.maClipRect.SetTop( mnScrY );
        bVClip = true;
    }
    if( rAreaParam.maClipRect.Bottom() > mnScrY + mnScrH )
    {
        rAreaParam.maClipRect.SetBottom( mnScrY + mnScrH );     //! minus one?
        bVClip = true;
    }
    return bVClip;
}

// Doesn't handle clip marks - should be handled in advance using GetOutputArea
class ClearableClipRegion
{
public:
    ClearableClipRegion( const tools::Rectangle& rRect, bool bClip, bool bSimClip,
                        const VclPtr<OutputDevice>& pDev, bool bMetaFile )
        :mbMetaFile(bMetaFile)
    {
        if (!(bClip || bSimClip))
            return;

        maRect = rRect;
        if (bClip)  // for bSimClip only initialize aClipRect
        {
            mpDev.reset(pDev);
            if (mbMetaFile)
            {
                mpDev->Push();
                mpDev->IntersectClipRegion(maRect);
            }
            else
                mpDev->SetClipRegion(vcl::Region(maRect));
        }
    }

    ~ClearableClipRegion() COVERITY_NOEXCEPT_FALSE
    {
        // Pop() or SetClipRegion() must only be called in case bClip was true
        // in the ctor, and only then mpDev is set.
        if (mpDev)
        {
            if (mbMetaFile)
                mpDev->Pop();
            else
                mpDev->SetClipRegion();
        }
    }

    const tools::Rectangle& getRect() const { return maRect; }

private:
    tools::Rectangle        maRect;
    VclPtr<OutputDevice>    mpDev;
    bool                    mbMetaFile;
};

// Returns needed width in current units; sets rNeededPixel to needed width in pixels
tools::Long ScOutputData::SetEngineTextAndGetWidth( DrawEditParam& rParam, const OUString& rSetString,
                                             tools::Long& rNeededPixel, tools::Long nAddWidthPixels )
{
    rParam.mpEngine->SetTextCurrentDefaults( rSetString );
    tools::Long nEngineWidth = static_cast<tools::Long>( rParam.mpEngine->CalcTextWidth() );
    if ( rParam.mbPixelToLogic )
        rNeededPixel = mpRefDevice->LogicToPixel( Size( nEngineWidth, 0 ) ).Width();
    else
        rNeededPixel = nEngineWidth;

    rNeededPixel += nAddWidthPixels;

    return nEngineWidth;
}

void ScOutputData::DrawEditStandard(DrawEditParam& rParam)
{
    OSL_ASSERT(rParam.meOrient == SvxCellOrientation::Standard);
    OSL_ASSERT(!rParam.mbAsianVertical);

    Size aRefOne = mpRefDevice->PixelToLogic(Size(1,1));

    bool bRepeat = (rParam.meHorJustAttr == SvxCellHorJustify::Repeat && !rParam.mbBreak);
    bool bShrink = !rParam.mbBreak && !bRepeat && lcl_GetBoolValue(*rParam.mpPattern, ATTR_SHRINKTOFIT, rParam.mpCondSet);
    Degree100 nAttrRotate = lcl_GetValue<ScRotateValueItem, Degree100>(*rParam.mpPattern, ATTR_ROTATE_VALUE, rParam.mpCondSet);

    if ( rParam.meHorJustAttr == SvxCellHorJustify::Repeat )
    {
        // ignore orientation/rotation if "repeat" is active
        rParam.meOrient = SvxCellOrientation::Standard;
        nAttrRotate = 0_deg100;

        // #i31843# "repeat" with "line breaks" is treated as default alignment
        // (but rotation is still disabled).
        // Default again leads to context dependent alignment instead of
        // SvxCellHorJustify::Standard.
        if ( rParam.mbBreak )
            rParam.meHorJustResult = rParam.meHorJustContext;
    }

    if (nAttrRotate)
    {
        //! set flag to find the cell in DrawRotated again ?
        //! (or flag already set during DrawBackground, then no query here)
        return;     // rotated is outputted separately
    }

    SvxCellHorJustify eOutHorJust = rParam.meHorJustContext;

    //! mirror margin values for RTL?
    //! move margin down to after final GetOutputArea call
    tools::Long nTopM, nLeftM, nBottomM, nRightM;
    rParam.calcMargins(nTopM, nLeftM, nBottomM, nRightM, mnPPTX, mnPPTY);

    SCCOL nXForPos = rParam.mnX;
    if ( nXForPos < mnX1 )
    {
        nXForPos = mnX1;
        rParam.mnPosX = rParam.mnInitPosX;
    }
    SCSIZE nArrYForPos = rParam.mnArrY;
    if ( nArrYForPos < 1 )
    {
        nArrYForPos = 1;
        rParam.mnPosY = mnScrY;
    }

    OutputAreaParam aAreaParam;

    //  Initial page size - large for normal text, cell size for automatic line breaks

    Size aPaperSize( 1000000, 1000000 );
    if (rParam.mbBreak)
    {
        //  call GetOutputArea with nNeeded=0, to get only the cell width

        //! handle nArrY == 0
        GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, 0,
                       *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                       rParam.mbCellIsValue, true, false, aAreaParam );

        //! special ScEditUtil handling if formatting for printer
        rParam.calcPaperSize(aPaperSize, aAreaParam.maAlignRect, mnPPTX, mnPPTY);
    }
    if (rParam.mbPixelToLogic)
    {
        Size aLogicSize = mpRefDevice->PixelToLogic(aPaperSize);
        if ( rParam.mbBreak && !rParam.mbAsianVertical && mpRefDevice != pFmtDevice )
        {
            // #i85342# screen display and formatting for printer,
            // use same GetEditArea call as in ScViewData::SetEditEngine

            Fraction aFract(1,1);
            tools::Rectangle aUtilRect = ScEditUtil( *mpDoc, rParam.mnCellX, rParam.mnCellY, mnTab, Point(0,0), pFmtDevice,
                HMM_PER_TWIPS, HMM_PER_TWIPS, aFract, aFract ).GetEditArea( rParam.mpPattern, false );
            aLogicSize.setWidth( aUtilRect.GetWidth() );
        }
        rParam.mpEngine->SetPaperSize(aLogicSize);
    }
    else
        rParam.mpEngine->SetPaperSize(aPaperSize);

    //  Fill the EditEngine (cell attributes and text)

    // default alignment for asian vertical mode is top-right
    if ( rParam.mbAsianVertical && rParam.meVerJust == SvxCellVerJustify::Standard )
        rParam.meVerJust = SvxCellVerJustify::Top;

    rParam.setPatternToEngine(mbUseStyleColor);
    rParam.setAlignmentToEngine();
    // Don't format unnecessary parts if the text will be drawn from top (Standard will
    // act that way if text doesn't fit, see below).
    rParam.mpEngine->EnableSkipOutsideFormat(rParam.meVerJust==SvxCellVerJustify::Top
        || rParam.meVerJust==SvxCellVerJustify::Standard);

    //  Read content from cell

    bool bWrapFields = false;
    if (!rParam.readCellContent(mpDoc, mbShowNullValues, mbShowFormulas, mbSyntaxMode, mbUseStyleColor, mbForceAutoColor, bWrapFields))
        // Failed to read cell content.  Bail out.
        return;

    if ( mbSyntaxMode )
        SetEditSyntaxColor(*rParam.mpEngine, rParam.maCell);
    else if ( mbUseStyleColor && mbForceAutoColor )
        lcl_SetEditColor( *rParam.mpEngine, COL_AUTO );     //! or have a flag at EditEngine

    rParam.mpEngine->SetUpdateLayout( true );     // after SetText, before CalcTextWidth/GetTextHeight

    //  Get final output area using the calculated width

    tools::Long nEngineWidth, nEngineHeight;
    rParam.getEngineSize(rParam.mpEngine, nEngineWidth, nEngineHeight);

    tools::Long nNeededPixel = nEngineWidth;
    if (rParam.mbPixelToLogic)
        nNeededPixel = mpRefDevice->LogicToPixel(Size(nNeededPixel,0)).Width();
    nNeededPixel += nLeftM + nRightM;

    if (!rParam.mbBreak || bShrink)
    {
        // for break, the first GetOutputArea call is sufficient
        GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, nNeededPixel,
                       *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                       rParam.mbCellIsValue || bRepeat || bShrink, false, false, aAreaParam );

        if ( bShrink )
        {
            ShrinkEditEngine( *rParam.mpEngine, aAreaParam.maAlignRect,
                nLeftM, nTopM, nRightM, nBottomM, true,
                rParam.meOrient, 0_deg100, rParam.mbPixelToLogic,
                nEngineWidth, nEngineHeight, nNeededPixel,
                aAreaParam.mbLeftClip, aAreaParam.mbRightClip );
        }
        if ( bRepeat && !aAreaParam.mbLeftClip && !aAreaParam.mbRightClip && rParam.mpEngine->GetParagraphCount() == 1 )
        {
            // First check if twice the space for the formatted text is available
            // (otherwise just keep it unchanged).

            tools::Long nFormatted = nNeededPixel - nLeftM - nRightM;      // without margin
            tools::Long nAvailable = aAreaParam.maAlignRect.GetWidth() - nLeftM - nRightM;
            if ( nAvailable >= 2 * nFormatted )
            {
                // "repeat" is handled with unformatted text (for performance reasons)
                OUString aCellStr = rParam.mpEngine->GetText();

                tools::Long nRepeatSize = 0;
                SetEngineTextAndGetWidth( rParam, aCellStr, nRepeatSize, 0 );
                if ( pFmtDevice != mpRefDevice )
                    ++nRepeatSize;
                if ( nRepeatSize > 0 )
                {
                    tools::Long nRepeatCount = nAvailable / nRepeatSize;
                    if ( nRepeatCount > 1 )
                    {
                        OUStringBuffer aRepeated(aCellStr);
                        for ( tools::Long nRepeat = 1; nRepeat < nRepeatCount; nRepeat++ )
                            aRepeated.append(aCellStr);

                        SetEngineTextAndGetWidth( rParam, aRepeated.makeStringAndClear(),
                                                  nNeededPixel, (nLeftM + nRightM ) );

                        nEngineHeight = rParam.mpEngine->GetTextHeight();
                    }
                }
            }
        }

        if (rParam.mnX >= mnX1 && rParam.mbCellIsValue
            && (aAreaParam.mbLeftClip || aAreaParam.mbRightClip))
        {
            SetEngineTextAndGetWidth( rParam, u"###"_ustr, nNeededPixel, ( nLeftM + nRightM ) );
            tools::Long nLayoutSign = mbLayoutRTL ? -1 : 1;
            ScCellInfo* pClipMarkCell = &rParam.mpThisRowInfo->cellInfo(rParam.mnX);
            SetClipMarks( aAreaParam, pClipMarkCell, eOutHorJust, nLayoutSign );
        }

        if (eOutHorJust != SvxCellHorJustify::Left)
        {
            aPaperSize.setWidth( nNeededPixel + 1 );
            if (rParam.mbPixelToLogic)
                rParam.mpEngine->SetPaperSize(mpRefDevice->PixelToLogic(aPaperSize));
            else
                rParam.mpEngine->SetPaperSize(aPaperSize);
        }
    }

    tools::Long nStartX = aAreaParam.maAlignRect.Left();
    tools::Long nStartY = aAreaParam.maAlignRect.Top();
    tools::Long nCellWidth = aAreaParam.maAlignRect.GetWidth();
    tools::Long nOutWidth = nCellWidth - 1 - nLeftM - nRightM;
    tools::Long nOutHeight = aAreaParam.maAlignRect.GetHeight() - nTopM - nBottomM;

    if (rParam.mbBreak)
    {
        //  text with automatic breaks is aligned only within the
        //  edit engine's paper size, the output of the whole area
        //  is always left-aligned

        nStartX += nLeftM;
    }
    else
    {
        if ( eOutHorJust == SvxCellHorJustify::Right )
            nStartX -= nNeededPixel - nCellWidth + nRightM + 1;
        else if ( eOutHorJust == SvxCellHorJustify::Center )
            nStartX -= ( nNeededPixel - nCellWidth + nRightM + 1 - nLeftM ) / 2;
        else
            nStartX += nLeftM;
    }

    bool bOutside = (aAreaParam.maClipRect.Right() < mnScrX || aAreaParam.maClipRect.Left() >= mnScrX + mnScrW);
    if (bOutside)
        return;

    // Also take fields in a cell with automatic breaks into account: clip to cell width
    bool bClip = AdjustAreaParamClipRect(aAreaParam) || aAreaParam.mbLeftClip || aAreaParam.mbRightClip || bWrapFields;
    bool bSimClip = false;

    Size aCellSize;         // output area, excluding margins, in logical units
    if (rParam.mbPixelToLogic)
        aCellSize = mpRefDevice->PixelToLogic( Size( nOutWidth, nOutHeight ) );
    else
        aCellSize = Size( nOutWidth, nOutHeight );

    if ( nEngineHeight >= aCellSize.Height() + aRefOne.Height() )
    {
        const ScMergeAttr* pMerge = &rParam.mpPattern->GetItem(ATTR_MERGE);
        bool bMerged = pMerge->GetColMerge() > 1 || pMerge->GetRowMerge() > 1;

        //  Don't clip for text height when printing rows with optimal height,
        //  except when font size is from conditional formatting.
        //! Allow clipping when vertically merged?
        if ( meType != OUTTYPE_PRINTER ||
            ( mpDoc->GetRowFlags( rParam.mnCellY, mnTab ) & CRFlags::ManualSize ) ||
            ( rParam.mpCondSet && SfxItemState::SET ==
                rParam.mpCondSet->GetItemState(ATTR_FONT_HEIGHT) ) )
            bClip = true;
        else
            bSimClip = true;

        //  Show clip marks if height is at least 5pt too small and
        //  there are several lines of text.
        //  Not for asian vertical text, because that would interfere
        //  with the default right position of the text.
        //  Only with automatic line breaks, to avoid having to find
        //  the cells with the horizontal end of the text again.
        if ( nEngineHeight - aCellSize.Height() > 100 &&
             rParam.mbBreak && mbMarkClipped &&
             ( rParam.mpEngine->GetParagraphCount() > 1 || rParam.mpEngine->GetLineCount(0) > 1 ) )
        {
            ScCellInfo* pClipMarkCell = nullptr;
            if ( bMerged )
            {
                //  anywhere in the merged area...
                SCCOL nClipX = ( rParam.mnX < mnX1 ) ? mnX1 : rParam.mnX;
                pClipMarkCell = &mpRowInfo[(rParam.mnArrY != 0) ? rParam.mnArrY : 1].cellInfo(nClipX);
            }
            else
                pClipMarkCell = &rParam.mpThisRowInfo->cellInfo(rParam.mnX);

            pClipMarkCell->nClipMark |= ScClipMark::Right;      //! also allow left?
            mbAnyClipped = true;

            tools::Long nMarkPixel = static_cast<tools::Long>( SC_CLIPMARK_SIZE * mnPPTX );
            if ( aAreaParam.maClipRect.Right() - nMarkPixel > aAreaParam.maClipRect.Left() )
                aAreaParam.maClipRect.AdjustRight( -nMarkPixel );

            // Standard is normally treated as Bottom, but if text height is clipped, then
            // Top looks better and also allows using EditEngine::EnableSkipOutsideFormat().
            if (rParam.meVerJust==SvxCellVerJustify::Standard)
                rParam.meVerJust=SvxCellVerJustify::Top;
        }
    }

    Point aURLStart;

    {   // Clip marks are already handled in GetOutputArea
        ClearableClipRegion aClip(rParam.mbPixelToLogic ? mpRefDevice->PixelToLogic(aAreaParam.maClipRect)
                                : aAreaParam.maClipRect, bClip, bSimClip, mpDev, mbMetaFile);

        Point aLogicStart;
        if (rParam.mbPixelToLogic)
            aLogicStart = mpRefDevice->PixelToLogic( Point(nStartX,nStartY) );
        else
            aLogicStart = Point(nStartX, nStartY);

        if (!rParam.mbBreak)
        {
            //  horizontal alignment
            if (rParam.adjustHorAlignment(rParam.mpEngine))
                // reset adjustment for the next cell
                rParam.mpOldPattern = nullptr;
        }

        if (rParam.meVerJust==SvxCellVerJustify::Bottom ||
            rParam.meVerJust==SvxCellVerJustify::Standard)
        {
            //! if pRefDevice != pFmtDevice, keep heights in logic units,
            //! only converting margin?

            if (rParam.mbPixelToLogic)
                aLogicStart.AdjustY(mpRefDevice->PixelToLogic( Size(0, nTopM +
                                mpRefDevice->LogicToPixel(aCellSize).Height() -
                                mpRefDevice->LogicToPixel(Size(0,nEngineHeight)).Height()
                                )).Height() );
            else
                aLogicStart.AdjustY(nTopM + aCellSize.Height() - nEngineHeight );
        }
        else if (rParam.meVerJust==SvxCellVerJustify::Center)
        {
            if (rParam.mbPixelToLogic)
                aLogicStart.AdjustY(mpRefDevice->PixelToLogic( Size(0, nTopM + (
                                mpRefDevice->LogicToPixel(aCellSize).Height() -
                                mpRefDevice->LogicToPixel(Size(0,nEngineHeight)).Height() )
                                / 2)).Height() );
            else
                aLogicStart.AdjustY(nTopM + (aCellSize.Height() - nEngineHeight) / 2 );
        }
        else        // top
        {
            if (rParam.mbPixelToLogic)
                aLogicStart.AdjustY(mpRefDevice->PixelToLogic(Size(0,nTopM)).Height() );
            else
                aLogicStart.AdjustY(nTopM );
        }

        aURLStart = aLogicStart;      // copy before modifying for orientation

        // bMoveClipped handling has been replaced by complete alignment
        // handling (also extending to the left).

        if (bSimClip)
        {
            // no hard clip, only draw the affected rows
            Point aDocStart = aClip.getRect().TopLeft();
            aDocStart -= aLogicStart;
            rParam.mpEngine->DrawText_ToRectangle(*mpDev, aClip.getRect(), aDocStart, false);
        }
        else
        {
            rParam.mpEngine->DrawText_ToPosition(*mpDev, aLogicStart);
        }
    }

    rParam.adjustForHyperlinkInPDF(aURLStart, mpDev);
}

void ScOutputData::SetClipMarks( OutputAreaParam &aAreaParam, ScCellInfo* pClipMarkCell,
                                 SvxCellHorJustify eOutHorJust,
                                 tools::Long nLayoutSign )
{
    tools::Long nMarkPixel = SC_CLIPMARK_SIZE * mnPPTX;

    if ( eOutHorJust == SvxCellHorJustify::Left )
    {
        pClipMarkCell->nClipMark |= ScClipMark::Right;
        mbAnyClipped = true;
        aAreaParam.maClipRect.AdjustRight( -( nMarkPixel * nLayoutSign ) );
    }
    else if ( eOutHorJust == SvxCellHorJustify::Right )
    {
        pClipMarkCell->nClipMark |= ScClipMark::Left;
        mbAnyClipped = true;
        aAreaParam.maClipRect.AdjustLeft( nMarkPixel * nLayoutSign );
    }
    else
    {
        pClipMarkCell->nClipMark |= ScClipMark::Right;
        pClipMarkCell->nClipMark |= ScClipMark::Left;
        mbAnyClipped = true;
        aAreaParam.maClipRect.AdjustRight( -( nMarkPixel * nLayoutSign ) );
        aAreaParam.maClipRect.AdjustLeft( nMarkPixel * nLayoutSign );
    }

}

void ScOutputData::ShowClipMarks( DrawEditParam& rParam, tools::Long nEngineWidth, const Size& aCellSize,
                                  bool bMerged, OutputAreaParam& aAreaParam, bool bTop)
{
    //  Show clip marks if width is at least 5pt too small and
    //  there are several lines of text.
    //  Not for asian vertical text, because that would interfere
    //  with the default right position of the text.
    //  Only with automatic line breaks, to avoid having to find
    //  the cells with the horizontal end of the text again.
    if (nEngineWidth - aCellSize.Width() <= 100 || !rParam.mbBreak || !mbMarkClipped
        || (rParam.mpEngine->GetParagraphCount() <= 1 && rParam.mpEngine->GetLineCount(0) <= 1))
        return;

    ScCellInfo* pClipMarkCell = nullptr;
    if (bMerged)
    {
        //  anywhere in the merged area...
        SCCOL nClipX = (rParam.mnX < mnX1) ? mnX1 : rParam.mnX;
        pClipMarkCell = &mpRowInfo[(rParam.mnArrY != 0) ? rParam.mnArrY : 1].cellInfo(nClipX);
    }
    else
        pClipMarkCell = &rParam.mpThisRowInfo->cellInfo(rParam.mnX);

    mbAnyClipped = true;
    mbVertical = true;
    const tools::Long nMarkPixel = static_cast<tools::Long>(SC_CLIPMARK_SIZE * mnPPTX);
    if (bTop)
    {
        pClipMarkCell->nClipMark |= ScClipMark::Top;
        if (aAreaParam.maClipRect.Top() - nMarkPixel < aAreaParam.maClipRect.Bottom())
            aAreaParam.maClipRect.AdjustTop(+nMarkPixel);
    }
    else
    {
        pClipMarkCell->nClipMark |= ScClipMark::Bottom;
        if (aAreaParam.maClipRect.Top() - nMarkPixel < aAreaParam.maClipRect.Bottom())
            aAreaParam.maClipRect.AdjustBottom(-nMarkPixel);
    }
}

ClearableClipRegionPtr ScOutputData::Clip( DrawEditParam& rParam, const Size& aCellSize,
                                                        OutputAreaParam& aAreaParam, tools::Long nEngineWidth,
                                                        bool bWrapFields, bool bTop)
{
    // Also take fields in a cell with automatic breaks into account: clip to cell width
    bool bClip = AdjustAreaParamClipRect(aAreaParam) || aAreaParam.mbLeftClip || aAreaParam.mbRightClip || bWrapFields;
    bool bSimClip = false;

    const Size aRefOne = mpRefDevice->PixelToLogic(Size(1,1));
    if ( nEngineWidth >= aCellSize.Width() + aRefOne.Width() )
    {
        const ScMergeAttr* pMerge = &rParam.mpPattern->GetItem(ATTR_MERGE);
        const bool bMerged = pMerge->GetColMerge() > 1 || pMerge->GetRowMerge() > 1;

        //  Don't clip for text height when printing rows with optimal height,
        //  except when font size is from conditional formatting.
        //! Allow clipping when vertically merged?
        if ( meType != OUTTYPE_PRINTER ||
            ( mpDoc->GetRowFlags( rParam.mnCellY, mnTab ) & CRFlags::ManualSize ) ||
            ( rParam.mpCondSet && SfxItemState::SET ==
                rParam.mpCondSet->GetItemState(ATTR_FONT_HEIGHT) ) )
            bClip = true;
        else
            bSimClip = true;

        ShowClipMarks( rParam, nEngineWidth, aCellSize, bMerged, aAreaParam, bTop);
    }

        // Clip marks are already handled in GetOutputArea
    return ClearableClipRegionPtr(new ClearableClipRegion(rParam.mbPixelToLogic ?
                                                mpRefDevice->PixelToLogic(aAreaParam.maClipRect)
                                              : aAreaParam.maClipRect, bClip, bSimClip, mpDev, mbMetaFile));
}

void ScOutputData::DrawEditBottomTop(DrawEditParam& rParam)
{
    OSL_ASSERT(rParam.meHorJustAttr != SvxCellHorJustify::Repeat);

    const bool bRepeat = (rParam.meHorJustAttr == SvxCellHorJustify::Repeat && !rParam.mbBreak);
    const bool bShrink = !rParam.mbBreak && !bRepeat && lcl_GetBoolValue(*rParam.mpPattern, ATTR_SHRINKTOFIT, rParam.mpCondSet);

    SvxCellHorJustify eOutHorJust = rParam.meHorJustContext;

    //! mirror margin values for RTL?
    //! move margin down to after final GetOutputArea call
    tools::Long nTopM, nLeftM, nBottomM, nRightM;
    rParam.calcMargins(nTopM, nLeftM, nBottomM, nRightM, mnPPTX, mnPPTY);

    SCCOL nXForPos = rParam.mnX;
    if ( nXForPos < mnX1 )
    {
        nXForPos = mnX1;
        rParam.mnPosX = rParam.mnInitPosX;
    }
    SCSIZE nArrYForPos = rParam.mnArrY;
    if ( nArrYForPos < 1 )
    {
        nArrYForPos = 1;
        rParam.mnPosY = mnScrY;
    }

    OutputAreaParam aAreaParam;

    //  Initial page size - large for normal text, cell size for automatic line breaks

    Size aPaperSize( 1000000, 1000000 );
    if (rParam.mbBreak)
    {
        //  call GetOutputArea with nNeeded=0, to get only the cell width

        //! handle nArrY == 0
        GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, 0,
                       *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                       rParam.mbCellIsValue, true, false, aAreaParam );

        //! special ScEditUtil handling if formatting for printer
        rParam.calcPaperSize(aPaperSize, aAreaParam.maAlignRect, mnPPTX, mnPPTY);
    }
    if (rParam.mbPixelToLogic)
    {
        Size aLogicSize = mpRefDevice->PixelToLogic(aPaperSize);
        rParam.mpEngine->SetPaperSize(aLogicSize);
    }
    else
        rParam.mpEngine->SetPaperSize(aPaperSize);

    //  Fill the EditEngine (cell attributes and text)

    rParam.setPatternToEngine(mbUseStyleColor);
    rParam.setAlignmentToEngine();

    //  Read content from cell

    bool bWrapFields = false;
    if (!rParam.readCellContent(mpDoc, mbShowNullValues, mbShowFormulas, mbSyntaxMode, mbUseStyleColor, mbForceAutoColor, bWrapFields))
        // Failed to read cell content.  Bail out.
        return;

    if ( mbSyntaxMode )
        SetEditSyntaxColor( *rParam.mpEngine, rParam.maCell );
    else if ( mbUseStyleColor && mbForceAutoColor )
        lcl_SetEditColor( *rParam.mpEngine, COL_AUTO );     //! or have a flag at EditEngine

    rParam.mpEngine->SetUpdateLayout( true );     // after SetText, before CalcTextWidth/GetTextHeight

    //  Get final output area using the calculated width

    tools::Long nEngineWidth, nEngineHeight;
    rParam.getEngineSize(rParam.mpEngine, nEngineWidth, nEngineHeight);

    tools::Long nNeededPixel = nEngineWidth;
    if (rParam.mbPixelToLogic)
        nNeededPixel = mpRefDevice->LogicToPixel(Size(nNeededPixel,0)).Width();
    nNeededPixel += nLeftM + nRightM;

    if (!rParam.mbBreak || bShrink)
    {
        // for break, the first GetOutputArea call is sufficient
        GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, nNeededPixel,
                       *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                       rParam.mbCellIsValue || bRepeat || bShrink, false, false, aAreaParam );

        if ( bShrink )
        {
            ShrinkEditEngine( *rParam.mpEngine, aAreaParam.maAlignRect,
                nLeftM, nTopM, nRightM, nBottomM, false,
                (rParam.meOrient), 0_deg100, rParam.mbPixelToLogic,
                nEngineWidth, nEngineHeight, nNeededPixel,
                aAreaParam.mbLeftClip, aAreaParam.mbRightClip );
        }
        if ( bRepeat && !aAreaParam.mbLeftClip && !aAreaParam.mbRightClip && rParam.mpEngine->GetParagraphCount() == 1 )
        {
            // First check if twice the space for the formatted text is available
            // (otherwise just keep it unchanged).

            const tools::Long nFormatted = nNeededPixel - nLeftM - nRightM;      // without margin
            const tools::Long nAvailable = aAreaParam.maAlignRect.GetWidth() - nLeftM - nRightM;
            if ( nAvailable >= 2 * nFormatted )
            {
                // "repeat" is handled with unformatted text (for performance reasons)
                OUString aCellStr = rParam.mpEngine->GetText();

                tools::Long nRepeatSize = 0;
                SetEngineTextAndGetWidth( rParam, aCellStr, nRepeatSize, 0 );
                if ( pFmtDevice != mpRefDevice )
                    ++nRepeatSize;
                if ( nRepeatSize > 0 )
                {
                    const tools::Long nRepeatCount = nAvailable / nRepeatSize;
                    if ( nRepeatCount > 1 )
                    {
                        OUStringBuffer aRepeated(aCellStr);
                        for ( tools::Long nRepeat = 1; nRepeat < nRepeatCount; nRepeat++ )
                            aRepeated.append(aCellStr);

                        nEngineWidth = SetEngineTextAndGetWidth( rParam, aRepeated.makeStringAndClear(),
                                                            nNeededPixel, (nLeftM + nRightM ) );

                        nEngineHeight = rParam.mpEngine->GetTextHeight();
                    }
                }
            }
        }
        if ( rParam.mbCellIsValue && ( aAreaParam.mbLeftClip || aAreaParam.mbRightClip ) )
        {
            nEngineWidth = SetEngineTextAndGetWidth( rParam, u"###"_ustr, nNeededPixel, ( nLeftM + nRightM ) );

            //  No clip marks if "###" doesn't fit (same as in DrawStrings)
        }
    }

    tools::Long nStartX = aAreaParam.maAlignRect.Left();
    const tools::Long nStartY = aAreaParam.maAlignRect.Top();
    const tools::Long nCellWidth = aAreaParam.maAlignRect.GetWidth();
    const tools::Long nOutWidth = nCellWidth - 1 - nLeftM - nRightM;
    const tools::Long nOutHeight = aAreaParam.maAlignRect.GetHeight() - nTopM - nBottomM;

    if (rParam.mbBreak)
    {
        //  text with automatic breaks is aligned only within the
        //  edit engine's paper size, the output of the whole area
        //  is always left-aligned

        nStartX += nLeftM;
    }
    else
    {
        if ( eOutHorJust == SvxCellHorJustify::Right )
            nStartX -= nNeededPixel - nCellWidth + nRightM + 1;
        else if ( eOutHorJust == SvxCellHorJustify::Center )
            nStartX -= ( nNeededPixel - nCellWidth + nRightM + 1 - nLeftM ) / 2;
        else
            nStartX += nLeftM;
    }

    const bool bOutside = (aAreaParam.maClipRect.Right() < mnScrX || aAreaParam.maClipRect.Left() >= mnScrX + mnScrW);
    if (bOutside)
        return;

    // output area, excluding margins, in logical units
    const Size aCellSize = rParam.mbPixelToLogic
        ? mpRefDevice->PixelToLogic( Size( nOutWidth, nOutHeight ) )
        : Size( nOutWidth, nOutHeight );

    Point aURLStart;

    {
        const auto pClipRegion = Clip( rParam, aCellSize, aAreaParam, nEngineWidth, bWrapFields, true );

        Point aLogicStart(nStartX, nStartY);
        rParam.calcStartPosForVertical(aLogicStart, aCellSize.Width(), nEngineWidth, nTopM, mpRefDevice);

        aURLStart = aLogicStart;      // copy before modifying for orientation

        if (rParam.meHorJustResult == SvxCellHorJustify::Block || rParam.mbBreak)
        {
            Size aPSize = rParam.mpEngine->GetPaperSize();
            aPSize.setWidth( aCellSize.Height() );
            rParam.mpEngine->SetPaperSize(aPSize);
            aLogicStart.AdjustY(
                rParam.mbBreak ? aPSize.Width() : nEngineHeight );
        }
        else
        {
            // Note that the "paper" is rotated 90 degrees to the left, so
            // paper's width is in vertical direction.  Also, the whole text
            // is on a single line, as text wrap is not in effect.

            // Set the paper width to be the width of the text.
            Size aPSize = rParam.mpEngine->GetPaperSize();
            aPSize.setWidth( rParam.mpEngine->CalcTextWidth() );
            rParam.mpEngine->SetPaperSize(aPSize);

            tools::Long nGap = 0;
            tools::Long nTopOffset = 0;
            if (rParam.mbPixelToLogic)
            {
                nGap = mpRefDevice->LogicToPixel(aCellSize).Height() - mpRefDevice->LogicToPixel(aPSize).Width();
                nGap = mpRefDevice->PixelToLogic(Size(0, nGap)).Height();
                nTopOffset = mpRefDevice->PixelToLogic(Size(0,nTopM)).Height();
            }
            else
            {
                nGap = aCellSize.Height() - aPSize.Width();
                nTopOffset = nTopM;
            }

            // First, align text to bottom.
            aLogicStart.AdjustY(aCellSize.Height() );
            aLogicStart.AdjustY(nTopOffset );

            switch (rParam.meVerJust)
            {
                case SvxCellVerJustify::Standard:
                case SvxCellVerJustify::Bottom:
                    // align to bottom (do nothing).
                break;
                case SvxCellVerJustify::Center:
                    // center it.
                    aLogicStart.AdjustY( -(nGap / 2) );
                break;
                case SvxCellVerJustify::Block:
                case SvxCellVerJustify::Top:
                    // align to top
                    aLogicStart.AdjustY( -nGap );
                break;
                default:
                    ;
            }
        }

        rParam.mpEngine->DrawText_ToPosition(*mpDev, aLogicStart, 900_deg10);
    }

    rParam.adjustForHyperlinkInPDF(aURLStart, mpDev);
}

void ScOutputData::DrawEditTopBottom(DrawEditParam& rParam)
{
    OSL_ASSERT(rParam.meHorJustAttr != SvxCellHorJustify::Repeat);

    const bool bRepeat = (rParam.meHorJustAttr == SvxCellHorJustify::Repeat && !rParam.mbBreak);
    const bool bShrink = !rParam.mbBreak && !bRepeat && lcl_GetBoolValue(*rParam.mpPattern, ATTR_SHRINKTOFIT, rParam.mpCondSet);

    SvxCellHorJustify eOutHorJust = rParam.meHorJustContext;

    //! mirror margin values for RTL?
    //! move margin down to after final GetOutputArea call
    tools::Long nTopM, nLeftM, nBottomM, nRightM;
    rParam.calcMargins(nTopM, nLeftM, nBottomM, nRightM, mnPPTX, mnPPTY);

    SCCOL nXForPos = rParam.mnX;
    if ( nXForPos < mnX1 )
    {
        nXForPos = mnX1;
        rParam.mnPosX = rParam.mnInitPosX;
    }
    SCSIZE nArrYForPos = rParam.mnArrY;
    if ( nArrYForPos < 1 )
    {
        nArrYForPos = 1;
        rParam.mnPosY = mnScrY;
    }

    OutputAreaParam aAreaParam;

    //  Initial page size - large for normal text, cell size for automatic line breaks

    Size aPaperSize( 1000000, 1000000 );
    if (rParam.hasLineBreak())
    {
        //  call GetOutputArea with nNeeded=0, to get only the cell width

        //! handle nArrY == 0
        GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, 0,
                       *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                       rParam.mbCellIsValue, true, false, aAreaParam );

        //! special ScEditUtil handling if formatting for printer
        rParam.calcPaperSize(aPaperSize, aAreaParam.maAlignRect, mnPPTX, mnPPTY);
    }
    if (rParam.mbPixelToLogic)
    {
        Size aLogicSize = mpRefDevice->PixelToLogic(aPaperSize);
        rParam.mpEngine->SetPaperSize(aLogicSize);
    }
    else
        rParam.mpEngine->SetPaperSize(aPaperSize);

    //  Fill the EditEngine (cell attributes and text)

    rParam.setPatternToEngine(mbUseStyleColor);
    rParam.setAlignmentToEngine();

    //  Read content from cell

    bool bWrapFields = false;
    if (!rParam.readCellContent(mpDoc, mbShowNullValues, mbShowFormulas, mbSyntaxMode, mbUseStyleColor, mbForceAutoColor, bWrapFields))
        // Failed to read cell content.  Bail out.
        return;

    if ( mbSyntaxMode )
        SetEditSyntaxColor( *rParam.mpEngine, rParam.maCell );
    else if ( mbUseStyleColor && mbForceAutoColor )
        lcl_SetEditColor( *rParam.mpEngine, COL_AUTO );     //! or have a flag at EditEngine

    rParam.mpEngine->SetUpdateLayout( true );     // after SetText, before CalcTextWidth/GetTextHeight

    //  Get final output area using the calculated width

    tools::Long nEngineWidth, nEngineHeight;
    rParam.getEngineSize(rParam.mpEngine, nEngineWidth, nEngineHeight);

    tools::Long nNeededPixel = nEngineWidth;
    if (rParam.mbPixelToLogic)
        nNeededPixel = mpRefDevice->LogicToPixel(Size(nNeededPixel,0)).Width();
    nNeededPixel += nLeftM + nRightM;

    if (!rParam.mbBreak || bShrink)
    {
        // for break, the first GetOutputArea call is sufficient
        GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, nNeededPixel,
                       *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                       rParam.mbCellIsValue || bRepeat || bShrink, false, false, aAreaParam );

        if ( bShrink )
        {
            ShrinkEditEngine( *rParam.mpEngine, aAreaParam.maAlignRect,
                nLeftM, nTopM, nRightM, nBottomM, false,
                rParam.meOrient, 0_deg100, rParam.mbPixelToLogic,
                nEngineWidth, nEngineHeight, nNeededPixel,
                aAreaParam.mbLeftClip, aAreaParam.mbRightClip );
        }
        if ( bRepeat && !aAreaParam.mbLeftClip && !aAreaParam.mbRightClip && rParam.mpEngine->GetParagraphCount() == 1 )
        {
            // First check if twice the space for the formatted text is available
            // (otherwise just keep it unchanged).

            const tools::Long nFormatted = nNeededPixel - nLeftM - nRightM;      // without margin
            const tools::Long nAvailable = aAreaParam.maAlignRect.GetWidth() - nLeftM - nRightM;
            if ( nAvailable >= 2 * nFormatted )
            {
                // "repeat" is handled with unformatted text (for performance reasons)
                OUString aCellStr = rParam.mpEngine->GetText();

                tools::Long nRepeatSize = 0;
                SetEngineTextAndGetWidth( rParam, aCellStr, nRepeatSize, 0 );

                if ( pFmtDevice != mpRefDevice )
                    ++nRepeatSize;
                if ( nRepeatSize > 0 )
                {
                    const tools::Long nRepeatCount = nAvailable / nRepeatSize;
                    if ( nRepeatCount > 1 )
                    {
                        OUStringBuffer aRepeated(aCellStr);
                        for ( tools::Long nRepeat = 1; nRepeat < nRepeatCount; nRepeat++ )
                            aRepeated.append(aCellStr);

                        nEngineWidth = SetEngineTextAndGetWidth( rParam, aRepeated.makeStringAndClear(),
                                                            nNeededPixel, (nLeftM + nRightM ) );

                        nEngineHeight = rParam.mpEngine->GetTextHeight();
                    }
                }
            }
        }
        if ( rParam.mbCellIsValue && ( aAreaParam.mbLeftClip || aAreaParam.mbRightClip ) )
        {
            nEngineWidth = SetEngineTextAndGetWidth( rParam, u"###"_ustr, nNeededPixel, ( nLeftM + nRightM ) );

            //  No clip marks if "###" doesn't fit (same as in DrawStrings)
        }
    }

    tools::Long nStartX = aAreaParam.maAlignRect.Left();
    const tools::Long nStartY = aAreaParam.maAlignRect.Top();
    const tools::Long nCellWidth = aAreaParam.maAlignRect.GetWidth();
    const tools::Long nOutWidth = nCellWidth - 1 - nLeftM - nRightM;
    const tools::Long nOutHeight = aAreaParam.maAlignRect.GetHeight() - nTopM - nBottomM;

    if (rParam.mbBreak)
    {
        //  text with automatic breaks is aligned only within the
        //  edit engine's paper size, the output of the whole area
        //  is always left-aligned

        nStartX += nLeftM;
        if (rParam.meHorJustResult == SvxCellHorJustify::Block)
            nStartX += aPaperSize.Height();
    }
    else
    {
        if ( eOutHorJust == SvxCellHorJustify::Right )
            nStartX -= nNeededPixel - nCellWidth + nRightM + 1;
        else if ( eOutHorJust == SvxCellHorJustify::Center )
            nStartX -= ( nNeededPixel - nCellWidth + nRightM + 1 - nLeftM ) / 2;
        else
            nStartX += nLeftM;
    }

    const bool bOutside = (aAreaParam.maClipRect.Right() < mnScrX || aAreaParam.maClipRect.Left() >= mnScrX + mnScrW);
    if (bOutside)
        return;

    // output area, excluding margins, in logical units
    const Size aCellSize = rParam.mbPixelToLogic
        ? mpRefDevice->PixelToLogic( Size( nOutWidth, nOutHeight ) )
        : Size( nOutWidth, nOutHeight );

    Point aURLStart;

    {
        const auto pClipRegion = Clip( rParam, aCellSize, aAreaParam, nEngineWidth, bWrapFields, false );

        Point aLogicStart(nStartX, nStartY);
        rParam.calcStartPosForVertical(aLogicStart, aCellSize.Width(), nEngineWidth, nTopM, mpRefDevice);

        aURLStart = aLogicStart;      // copy before modifying for orientation

        if (rParam.meHorJustResult != SvxCellHorJustify::Block)
        {
            aLogicStart.AdjustX(nEngineWidth );
            if (!rParam.mbBreak)
            {
                // Set the paper width to text size.
                Size aPSize = rParam.mpEngine->GetPaperSize();
                aPSize.setWidth( rParam.mpEngine->CalcTextWidth() );
                rParam.mpEngine->SetPaperSize(aPSize);

                tools::Long nGap = 0;
                tools::Long nTopOffset = 0; // offset by top margin
                if (rParam.mbPixelToLogic)
                {
                    nGap = mpRefDevice->LogicToPixel(aPSize).Width() - mpRefDevice->LogicToPixel(aCellSize).Height();
                    nGap = mpRefDevice->PixelToLogic(Size(0, nGap)).Height();
                    nTopOffset = mpRefDevice->PixelToLogic(Size(0,nTopM)).Height();
                }
                else
                {
                    nGap = aPSize.Width() - aCellSize.Height();
                    nTopOffset = nTopM;
                }
                aLogicStart.AdjustY(nTopOffset );

                switch (rParam.meVerJust)
                {
                    case SvxCellVerJustify::Standard:
                    case SvxCellVerJustify::Bottom:
                        // align to bottom
                        aLogicStart.AdjustY( -nGap );
                    break;
                    case SvxCellVerJustify::Center:
                        // center it.
                        aLogicStart.AdjustY( -(nGap / 2) );
                    break;
                    case SvxCellVerJustify::Block:
                    case SvxCellVerJustify::Top:
                        // align to top (do nothing)
                    default:
                        ;
                }
            }
        }

        // bMoveClipped handling has been replaced by complete alignment
        // handling (also extending to the left).

        rParam.mpEngine->DrawText_ToPosition(*mpDev, aLogicStart, 2700_deg10);
    }

    rParam.adjustForHyperlinkInPDF(aURLStart, mpDev);
}

void ScOutputData::DrawEditStacked(DrawEditParam& rParam)
{
    OSL_ASSERT(rParam.meHorJustAttr != SvxCellHorJustify::Repeat);
    Size aRefOne = mpRefDevice->PixelToLogic(Size(1,1));

    bool bRepeat = (rParam.meHorJustAttr == SvxCellHorJustify::Repeat && !rParam.mbBreak);
    bool bShrink = !rParam.mbBreak && !bRepeat && lcl_GetBoolValue(*rParam.mpPattern, ATTR_SHRINKTOFIT, rParam.mpCondSet);

    rParam.mbAsianVertical =
        lcl_GetBoolValue(*rParam.mpPattern, ATTR_VERTICAL_ASIAN, rParam.mpCondSet);

    if ( rParam.mbAsianVertical )
    {
        // in asian mode, use EditEngine::SetVertical instead of EEControlBits::ONECHARPERLINE
        rParam.meOrient = SvxCellOrientation::Standard;
        DrawEditAsianVertical(rParam);
        return;
    }

    SvxCellHorJustify eOutHorJust = rParam.meHorJustContext;

    //! mirror margin values for RTL?
    //! move margin down to after final GetOutputArea call
    tools::Long nTopM, nLeftM, nBottomM, nRightM;
    rParam.calcMargins(nTopM, nLeftM, nBottomM, nRightM, mnPPTX, mnPPTY);

    SCCOL nXForPos = rParam.mnX;
    if ( nXForPos < mnX1 )
    {
        nXForPos = mnX1;
        rParam.mnPosX = rParam.mnInitPosX;
    }
    SCSIZE nArrYForPos = rParam.mnArrY;
    if ( nArrYForPos < 1 )
    {
        nArrYForPos = 1;
        rParam.mnPosY = mnScrY;
    }

    OutputAreaParam aAreaParam;

    //  Initial page size - large for normal text, cell size for automatic line breaks

    Size aPaperSize( 1000000, 1000000 );
    //  call GetOutputArea with nNeeded=0, to get only the cell width

    //! handle nArrY == 0
    GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, 0,
                   *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                   rParam.mbCellIsValue, true, false, aAreaParam );

    //! special ScEditUtil handling if formatting for printer
    rParam.calcPaperSize(aPaperSize, aAreaParam.maAlignRect, mnPPTX, mnPPTY);

    if (rParam.mbPixelToLogic)
    {
        Size aLogicSize = mpRefDevice->PixelToLogic(aPaperSize);
        if ( rParam.mbBreak && mpRefDevice != pFmtDevice )
        {
            // #i85342# screen display and formatting for printer,
            // use same GetEditArea call as in ScViewData::SetEditEngine

            Fraction aFract(1,1);
            tools::Rectangle aUtilRect = ScEditUtil( *mpDoc, rParam.mnCellX, rParam.mnCellY, mnTab, Point(0,0), pFmtDevice,
                HMM_PER_TWIPS, HMM_PER_TWIPS, aFract, aFract ).GetEditArea( rParam.mpPattern, false );
            aLogicSize.setWidth( aUtilRect.GetWidth() );
        }
        rParam.mpEngine->SetPaperSize(aLogicSize);
    }
    else
        rParam.mpEngine->SetPaperSize(aPaperSize);

    //  Fill the EditEngine (cell attributes and text)

    rParam.setPatternToEngine(mbUseStyleColor);
    rParam.setAlignmentToEngine();

    //  Read content from cell

    bool bWrapFields = false;
    if (!rParam.readCellContent(mpDoc, mbShowNullValues, mbShowFormulas, mbSyntaxMode, mbUseStyleColor, mbForceAutoColor, bWrapFields))
        // Failed to read cell content.  Bail out.
        return;

    if ( mbSyntaxMode )
        SetEditSyntaxColor( *rParam.mpEngine, rParam.maCell );
    else if ( mbUseStyleColor && mbForceAutoColor )
        lcl_SetEditColor( *rParam.mpEngine, COL_AUTO );     //! or have a flag at EditEngine

    rParam.mpEngine->SetUpdateLayout( true );     // after SetText, before CalcTextWidth/GetTextHeight

    //  Get final output area using the calculated width

    tools::Long nEngineWidth, nEngineHeight;
    rParam.getEngineSize(rParam.mpEngine, nEngineWidth, nEngineHeight);

    tools::Long nNeededPixel = nEngineWidth;
    if (rParam.mbPixelToLogic)
        nNeededPixel = mpRefDevice->LogicToPixel(Size(nNeededPixel,0)).Width();
    nNeededPixel += nLeftM + nRightM;

    if (bShrink)
    {
        // for break, the first GetOutputArea call is sufficient
        GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, nNeededPixel,
                       *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                       true, false, false, aAreaParam );

        ShrinkEditEngine( *rParam.mpEngine, aAreaParam.maAlignRect,
            nLeftM, nTopM, nRightM, nBottomM, true,
            rParam.meOrient, 0_deg100, rParam.mbPixelToLogic,
            nEngineWidth, nEngineHeight, nNeededPixel,
            aAreaParam.mbLeftClip, aAreaParam.mbRightClip );

        if ( rParam.mbCellIsValue && ( aAreaParam.mbLeftClip || aAreaParam.mbRightClip ) )
        {
            nEngineWidth = SetEngineTextAndGetWidth( rParam, u"###"_ustr, nNeededPixel, ( nLeftM + nRightM ) );
            tools::Long nLayoutSign = mbLayoutRTL ? -1 : 1;
            ScCellInfo* pClipMarkCell = &rParam.mpThisRowInfo->cellInfo(rParam.mnX);
            SetClipMarks( aAreaParam, pClipMarkCell, eOutHorJust, nLayoutSign );
        }

        if ( eOutHorJust != SvxCellHorJustify::Left )
        {
            aPaperSize.setWidth( nNeededPixel + 1 );
            if (rParam.mbPixelToLogic)
                rParam.mpEngine->SetPaperSize(mpRefDevice->PixelToLogic(aPaperSize));
            else
                rParam.mpEngine->SetPaperSize(aPaperSize);
        }
    }

    tools::Long nStartX = aAreaParam.maAlignRect.Left();
    tools::Long nStartY = aAreaParam.maAlignRect.Top();
    tools::Long nCellWidth = aAreaParam.maAlignRect.GetWidth();
    tools::Long nOutWidth = nCellWidth - 1 - nLeftM - nRightM;
    tools::Long nOutHeight = aAreaParam.maAlignRect.GetHeight() - nTopM - nBottomM;

    if (rParam.mbBreak)
    {
        //  text with automatic breaks is aligned only within the
        //  edit engine's paper size, the output of the whole area
        //  is always left-aligned

        nStartX += nLeftM;
    }
    else
    {
        if ( eOutHorJust == SvxCellHorJustify::Right )
            nStartX -= nNeededPixel - nCellWidth + nRightM + 1;
        else if ( eOutHorJust == SvxCellHorJustify::Center )
            nStartX -= ( nNeededPixel - nCellWidth + nRightM + 1 - nLeftM ) / 2;
        else
            nStartX += nLeftM;
    }

    bool bOutside = (aAreaParam.maClipRect.Right() < mnScrX || aAreaParam.maClipRect.Left() >= mnScrX + mnScrW);
    if (bOutside)
        return;

    // Also take fields in a cell with automatic breaks into account: clip to cell width
    bool bClip = AdjustAreaParamClipRect(aAreaParam) || aAreaParam.mbLeftClip || aAreaParam.mbRightClip || bWrapFields;
    bool bSimClip = false;

    Size aCellSize;         // output area, excluding margins, in logical units
    if (rParam.mbPixelToLogic)
        aCellSize = mpRefDevice->PixelToLogic( Size( nOutWidth, nOutHeight ) );
    else
        aCellSize = Size( nOutWidth, nOutHeight );

    if ( nEngineHeight >= aCellSize.Height() + aRefOne.Height() )
    {
        const ScMergeAttr* pMerge = &rParam.mpPattern->GetItem(ATTR_MERGE);
        bool bMerged = pMerge->GetColMerge() > 1 || pMerge->GetRowMerge() > 1;

        //  Don't clip for text height when printing rows with optimal height,
        //  except when font size is from conditional formatting.
        //! Allow clipping when vertically merged?
        if ( meType != OUTTYPE_PRINTER ||
            ( mpDoc->GetRowFlags( rParam.mnCellY, mnTab ) & CRFlags::ManualSize ) ||
            ( rParam.mpCondSet && SfxItemState::SET ==
                rParam.mpCondSet->GetItemState(ATTR_FONT_HEIGHT) ) )
            bClip = true;
        else
            bSimClip = true;

        //  Show clip marks if height is at least 5pt too small and
        //  there are several lines of text.
        //  Not for asian vertical text, because that would interfere
        //  with the default right position of the text.
        //  Only with automatic line breaks, to avoid having to find
        //  the cells with the horizontal end of the text again.
        if ( nEngineHeight - aCellSize.Height() > 100 &&
             rParam.mbBreak && mbMarkClipped &&
             ( rParam.mpEngine->GetParagraphCount() > 1 || rParam.mpEngine->GetLineCount(0) > 1 ) )
        {
            ScCellInfo* pClipMarkCell = nullptr;
            if ( bMerged )
            {
                //  anywhere in the merged area...
                SCCOL nClipX = ( rParam.mnX < mnX1 ) ? mnX1 : rParam.mnX;
                pClipMarkCell = &mpRowInfo[(rParam.mnArrY != 0) ? rParam.mnArrY : 1].cellInfo(nClipX);
            }
            else
                pClipMarkCell = &rParam.mpThisRowInfo->cellInfo(rParam.mnX);

            pClipMarkCell->nClipMark |= ScClipMark::Right;      //! also allow left?
            mbAnyClipped = true;

            tools::Long nMarkPixel = static_cast<tools::Long>( SC_CLIPMARK_SIZE * mnPPTX );
            if ( aAreaParam.maClipRect.Right() - nMarkPixel > aAreaParam.maClipRect.Left() )
                aAreaParam.maClipRect.AdjustRight( -nMarkPixel );
        }
    }

    Point aURLStart;

    {   // Clip marks are already handled in GetOutputArea
        ClearableClipRegion aClip(rParam.mbPixelToLogic ? mpRefDevice->PixelToLogic(aAreaParam.maClipRect)
                                : aAreaParam.maClipRect, bClip, bSimClip, mpDev, mbMetaFile);

        Point aLogicStart;
        if (rParam.mbPixelToLogic)
            aLogicStart = mpRefDevice->PixelToLogic( Point(nStartX,nStartY) );
        else
            aLogicStart = Point(nStartX, nStartY);

        if (rParam.meVerJust==SvxCellVerJustify::Bottom ||
            rParam.meVerJust==SvxCellVerJustify::Standard)
        {
            //! if pRefDevice != pFmtDevice, keep heights in logic units,
            //! only converting margin?

            if (rParam.mbPixelToLogic)
                aLogicStart.AdjustY(mpRefDevice->PixelToLogic( Size(0, nTopM +
                                mpRefDevice->LogicToPixel(aCellSize).Height() -
                                mpRefDevice->LogicToPixel(Size(0,nEngineHeight)).Height()
                                )).Height() );
            else
                aLogicStart.AdjustY(nTopM + aCellSize.Height() - nEngineHeight );
        }
        else if (rParam.meVerJust==SvxCellVerJustify::Center)
        {
            if (rParam.mbPixelToLogic)
                aLogicStart.AdjustY(mpRefDevice->PixelToLogic( Size(0, nTopM + (
                                mpRefDevice->LogicToPixel(aCellSize).Height() -
                                mpRefDevice->LogicToPixel(Size(0,nEngineHeight)).Height() )
                                / 2)).Height() );
            else
                aLogicStart.AdjustY(nTopM + (aCellSize.Height() - nEngineHeight) / 2 );
        }
        else        // top
        {
            if (rParam.mbPixelToLogic)
                aLogicStart.AdjustY(mpRefDevice->PixelToLogic(Size(0,nTopM)).Height() );
            else
                aLogicStart.AdjustY(nTopM );
        }

        aURLStart = aLogicStart;      // copy before modifying for orientation

        Size aPaperLogic = rParam.mpEngine->GetPaperSize();
        aPaperLogic.setWidth( nEngineWidth );
        rParam.mpEngine->SetPaperSize(aPaperLogic);

        // bMoveClipped handling has been replaced by complete alignment
        // handling (also extending to the left).

        if (bSimClip)
        {
            // no hard clip, only draw the affected rows
            Point aDocStart = aClip.getRect().TopLeft();
            aDocStart -= aLogicStart;
            rParam.mpEngine->DrawText_ToRectangle(*mpDev, aClip.getRect(), aDocStart, false);
        }
        else
        {
            rParam.mpEngine->DrawText_ToPosition(*mpDev, aLogicStart);
        }
    }

    rParam.adjustForHyperlinkInPDF(aURLStart, mpDev);
}

void ScOutputData::DrawEditAsianVertical(DrawEditParam& rParam)
{
    // When in asian vertical orientation, the orientation value is STANDARD,
    // and the asian vertical boolean is true.
    OSL_ASSERT(rParam.meOrient == SvxCellOrientation::Standard);
    OSL_ASSERT(rParam.mbAsianVertical);
    OSL_ASSERT(rParam.meHorJustAttr != SvxCellHorJustify::Repeat);

    Size aRefOne = mpRefDevice->PixelToLogic(Size(1,1));

    bool bHidden = false;
    bool bShrink = !rParam.mbBreak && lcl_GetBoolValue(*rParam.mpPattern, ATTR_SHRINKTOFIT, rParam.mpCondSet);
    Degree100 nAttrRotate = lcl_GetValue<ScRotateValueItem, Degree100>(*rParam.mpPattern, ATTR_ROTATE_VALUE, rParam.mpCondSet);

    if (nAttrRotate)
    {
        //! set flag to find the cell in DrawRotated again ?
        //! (or flag already set during DrawBackground, then no query here)
        bHidden = true;     // rotated is outputted separately
    }

    // default alignment for asian vertical mode is top-right
    /* TODO: is setting meHorJustContext and meHorJustResult unconditionally to
     * SvxCellHorJustify::Right really wanted? Seems this was done all the time,
     * also before context was introduced and everything was attr only. */
    if ( rParam.meHorJustAttr == SvxCellHorJustify::Standard )
        rParam.meHorJustResult = rParam.meHorJustContext = SvxCellHorJustify::Right;

    if (bHidden)
        return;

    SvxCellHorJustify eOutHorJust = rParam.meHorJustContext;

    //! mirror margin values for RTL?
    //! move margin down to after final GetOutputArea call
    tools::Long nTopM, nLeftM, nBottomM, nRightM;
    rParam.calcMargins(nTopM, nLeftM, nBottomM, nRightM, mnPPTX, mnPPTY);

    SCCOL nXForPos = rParam.mnX;
    if ( nXForPos < mnX1 )
    {
        nXForPos = mnX1;
        rParam.mnPosX = rParam.mnInitPosX;
    }
    SCSIZE nArrYForPos = rParam.mnArrY;
    if ( nArrYForPos < 1 )
    {
        nArrYForPos = 1;
        rParam.mnPosY = mnScrY;
    }

    OutputAreaParam aAreaParam;

    //  Initial page size - large for normal text, cell size for automatic line breaks

    Size aPaperSize( 1000000, 1000000 );
    //  call GetOutputArea with nNeeded=0, to get only the cell width

    //! handle nArrY == 0
    GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, 0,
                   *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                   rParam.mbCellIsValue, true, false, aAreaParam );

    //! special ScEditUtil handling if formatting for printer
    rParam.calcPaperSize(aPaperSize, aAreaParam.maAlignRect, mnPPTX, mnPPTY);

    if (rParam.mbPixelToLogic)
    {
        Size aLogicSize = mpRefDevice->PixelToLogic(aPaperSize);
        if ( rParam.mbBreak && !rParam.mbAsianVertical && mpRefDevice != pFmtDevice )
        {
            // #i85342# screen display and formatting for printer,
            // use same GetEditArea call as in ScViewData::SetEditEngine

            Fraction aFract(1,1);
            tools::Rectangle aUtilRect = ScEditUtil( *mpDoc, rParam.mnCellX, rParam.mnCellY, mnTab, Point(0,0), pFmtDevice,
                HMM_PER_TWIPS, HMM_PER_TWIPS, aFract, aFract ).GetEditArea( rParam.mpPattern, false );
            aLogicSize.setWidth( aUtilRect.GetWidth() );
        }
        rParam.mpEngine->SetPaperSize(aLogicSize);
    }
    else
        rParam.mpEngine->SetPaperSize(aPaperSize);

    //  Fill the EditEngine (cell attributes and text)

    // default alignment for asian vertical mode is top-right
    if ( rParam.meVerJust == SvxCellVerJustify::Standard )
        rParam.meVerJust = SvxCellVerJustify::Top;

    rParam.setPatternToEngine(mbUseStyleColor);
    rParam.setAlignmentToEngine();

    //  Read content from cell

    bool bWrapFields = false;
    if (!rParam.readCellContent(mpDoc, mbShowNullValues, mbShowFormulas, mbSyntaxMode, mbUseStyleColor, mbForceAutoColor, bWrapFields))
        // Failed to read cell content.  Bail out.
        return;

    if ( mbSyntaxMode )
        SetEditSyntaxColor( *rParam.mpEngine, rParam.maCell );
    else if ( mbUseStyleColor && mbForceAutoColor )
        lcl_SetEditColor( *rParam.mpEngine, COL_AUTO );     //! or have a flag at EditEngine

    rParam.mpEngine->SetUpdateLayout( true );     // after SetText, before CalcTextWidth/GetTextHeight

    //  Get final output area using the calculated width

    tools::Long nEngineWidth, nEngineHeight;
    rParam.getEngineSize(rParam.mpEngine, nEngineWidth, nEngineHeight);

    tools::Long nNeededPixel = nEngineWidth;
    if (rParam.mbPixelToLogic)
        nNeededPixel = mpRefDevice->LogicToPixel(Size(nNeededPixel,0)).Width();
    nNeededPixel += nLeftM + nRightM;

    // for break, the first GetOutputArea call is sufficient
    GetOutputArea( nXForPos, nArrYForPos, rParam.mnPosX, rParam.mnPosY, rParam.mnCellX, rParam.mnCellY, nNeededPixel,
                   *rParam.mpPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                   rParam.mbCellIsValue || bShrink, false, false, aAreaParam );

    if ( bShrink )
    {
        ShrinkEditEngine( *rParam.mpEngine, aAreaParam.maAlignRect,
            nLeftM, nTopM, nRightM, nBottomM, false,
            rParam.meOrient, 0_deg100, rParam.mbPixelToLogic,
            nEngineWidth, nEngineHeight, nNeededPixel,
            aAreaParam.mbLeftClip, aAreaParam.mbRightClip );
    }
    if ( rParam.mbCellIsValue && ( aAreaParam.mbLeftClip || aAreaParam.mbRightClip ) )
    {
        nEngineWidth = SetEngineTextAndGetWidth( rParam, u"###"_ustr, nNeededPixel, ( nLeftM + nRightM ) );
        tools::Long nLayoutSign = mbLayoutRTL ? -1 : 1;
        ScCellInfo* pClipMarkCell = &rParam.mpThisRowInfo->cellInfo(rParam.mnX);
        SetClipMarks( aAreaParam, pClipMarkCell, eOutHorJust, nLayoutSign );
    }

    if (eOutHorJust != SvxCellHorJustify::Left)
    {
        aPaperSize.setWidth( nNeededPixel + 1 );
        if (rParam.mbPixelToLogic)
            rParam.mpEngine->SetPaperSize(mpRefDevice->PixelToLogic(aPaperSize));
        else
            rParam.mpEngine->SetPaperSize(aPaperSize);
    }

    tools::Long nStartX = aAreaParam.maAlignRect.Left();
    tools::Long nStartY = aAreaParam.maAlignRect.Top();
    tools::Long nCellWidth = aAreaParam.maAlignRect.GetWidth();
    tools::Long nOutWidth = nCellWidth - 1 - nLeftM - nRightM;
    tools::Long nOutHeight = aAreaParam.maAlignRect.GetHeight() - nTopM - nBottomM;

    //  text with automatic breaks is aligned only within the
    //  edit engine's paper size, the output of the whole area
    //  is always left-aligned

    nStartX += nLeftM;

    bool bOutside = (aAreaParam.maClipRect.Right() < mnScrX || aAreaParam.maClipRect.Left() >= mnScrX + mnScrW);
    if (bOutside)
        return;

    // Also take fields in a cell with automatic breaks into account: clip to cell width
    bool bClip = AdjustAreaParamClipRect(aAreaParam) || aAreaParam.mbLeftClip || aAreaParam.mbRightClip || bWrapFields;
    bool bSimClip = false;

    Size aCellSize;         // output area, excluding margins, in logical units
    if (rParam.mbPixelToLogic)
        aCellSize = mpRefDevice->PixelToLogic( Size( nOutWidth, nOutHeight ) );
    else
        aCellSize = Size( nOutWidth, nOutHeight );

    if ( nEngineHeight >= aCellSize.Height() + aRefOne.Height() )
    {
        const ScMergeAttr* pMerge = &rParam.mpPattern->GetItem(ATTR_MERGE);
        bool bMerged = pMerge->GetColMerge() > 1 || pMerge->GetRowMerge() > 1;

        //  Don't clip for text height when printing rows with optimal height,
        //  except when font size is from conditional formatting.
        //! Allow clipping when vertically merged?
        if ( meType != OUTTYPE_PRINTER ||
            ( mpDoc->GetRowFlags( rParam.mnCellY, mnTab ) & CRFlags::ManualSize ) ||
            ( rParam.mpCondSet && SfxItemState::SET ==
                rParam.mpCondSet->GetItemState(ATTR_FONT_HEIGHT) ) )
            bClip = true;
        else
            bSimClip = true;

        //  Show clip marks if height is at least 5pt too small and
        //  there are several lines of text.
        //  Not for asian vertical text, because that would interfere
        //  with the default right position of the text.
        //  Only with automatic line breaks, to avoid having to find
        //  the cells with the horizontal end of the text again.
        if ( nEngineHeight - aCellSize.Height() > 100 &&
             ( rParam.mbBreak || rParam.meOrient == SvxCellOrientation::Stacked ) &&
             !rParam.mbAsianVertical && mbMarkClipped &&
             ( rParam.mpEngine->GetParagraphCount() > 1 || rParam.mpEngine->GetLineCount(0) > 1 ) )
        {
            ScCellInfo* pClipMarkCell = nullptr;
            if ( bMerged )
            {
                //  anywhere in the merged area...
                SCCOL nClipX = ( rParam.mnX < mnX1 ) ? mnX1 : rParam.mnX;
                pClipMarkCell = &mpRowInfo[(rParam.mnArrY != 0) ? rParam.mnArrY : 1].cellInfo(nClipX);
            }
            else
                pClipMarkCell = &rParam.mpThisRowInfo->cellInfo(rParam.mnX);

            pClipMarkCell->nClipMark |= ScClipMark::Right;      //! also allow left?
            mbAnyClipped = true;

            tools::Long nMarkPixel = static_cast<tools::Long>( SC_CLIPMARK_SIZE * mnPPTX );
            if ( aAreaParam.maClipRect.Right() - nMarkPixel > aAreaParam.maClipRect.Left() )
                aAreaParam.maClipRect.AdjustRight( -nMarkPixel );
        }
    }

    Point aURLStart;

    {   // Clip marks are already handled in GetOutputArea
        ClearableClipRegion aClip(rParam.mbPixelToLogic ? mpRefDevice->PixelToLogic(aAreaParam.maClipRect)
                                : aAreaParam.maClipRect, bClip, bSimClip, mpDev, mbMetaFile);

        Point aLogicStart;
        if (rParam.mbPixelToLogic)
            aLogicStart = mpRefDevice->PixelToLogic( Point(nStartX,nStartY) );
        else
            aLogicStart = Point(nStartX, nStartY);

        tools::Long nAvailWidth = aCellSize.Width();
        // space for AutoFilter is already handled in GetOutputArea

        //  horizontal alignment

        if (rParam.meHorJustResult==SvxCellHorJustify::Right)
            aLogicStart.AdjustX(nAvailWidth - nEngineWidth );
        else if (rParam.meHorJustResult==SvxCellHorJustify::Center)
            aLogicStart.AdjustX((nAvailWidth - nEngineWidth) / 2 );

        // paper size is subtracted below
        aLogicStart.AdjustX(nEngineWidth );

        // vertical adjustment is within the EditEngine
        if (rParam.mbPixelToLogic)
            aLogicStart.AdjustY(mpRefDevice->PixelToLogic(Size(0,nTopM)).Height() );
        else
            aLogicStart.AdjustY(nTopM );

        aURLStart = aLogicStart;      // copy before modifying for orientation

        // bMoveClipped handling has been replaced by complete alignment
        // handling (also extending to the left).

        // with SetVertical, the start position is top left of
        // the whole output area, not the text itself
        aLogicStart.AdjustX( -(rParam.mpEngine->GetPaperSize().Width()) );

        rParam.mpEngine->DrawText_ToPosition(*mpDev, aLogicStart);
    }

    rParam.adjustForHyperlinkInPDF(aURLStart, mpDev);
}

void ScOutputData::DrawEdit(bool bPixelToLogic)
{
    vcl::PDFExtOutDevData* pPDF = dynamic_cast<vcl::PDFExtOutDevData*>(mpDev->GetExtOutDevData());
    bool bTaggedPDF = pPDF && pPDF->GetIsExportTaggedPDF();

    InitOutputEditEngine();

    bool bHyphenatorSet = false;
    const ScPatternAttr* pOldPattern = nullptr;
    const SfxItemSet*    pOldCondSet = nullptr;
    const SfxItemSet*    pOldPreviewFontSet = nullptr;
    ScRefCellValue aCell;

    tools::Long nInitPosX = mnScrX;
    if ( mbLayoutRTL )
    {
        nInitPosX += mnMirrorW - 1;
    }
    tools::Long nLayoutSign = mbLayoutRTL ? -1 : 1;

    SCCOL nLastContentCol = mpDoc->MaxCol();
    if ( mnX2 < mpDoc->MaxCol() )
    {
        SCROW nEndRow;
        mpDoc->GetCellArea(mnTab, nLastContentCol, nEndRow);
    }

    tools::Long nRowPosY = mnScrY;
    for (SCSIZE nArrY=0; nArrY+1<mnArrCount; nArrY++)            // 0 of the rest of the merged
    {
        RowInfo* pThisRowInfo = &mpRowInfo[nArrY];

        if (nArrY==1) nRowPosY = mnScrY;                         // positions before are calculated individually

        if ( pThisRowInfo->bChanged || nArrY==0 )
        {
            tools::Long nPosX = 0;
            for (SCCOL nX=0; nX<=mnX2; nX++)                     // due to overflow
            {
                std::unique_ptr< ScPatternAttr > pPreviewPattr;
                if (nX==mnX1) nPosX = nInitPosX;                 // positions before mnX1 are calculated individually

                if (pThisRowInfo->basicCellInfo(nX).bEditEngine)
                {
                    SCROW nY = pThisRowInfo->nRowNo;

                    bool bReopenTag = false;
                    if (bTaggedPDF)
                        bReopenTag = ReopenPDFStructureElement(vcl::pdf::StructElement::TableData, nY, nX);

                    SCCOL nCellX = nX;                  // position where the cell really starts
                    SCROW nCellY = nY;
                    bool bDoCell = false;

                    // if merged cell contains hidden row or column or both
                    const ScMergeFlagAttr* pMergeFlag = mpDoc->GetAttr(nX, nY, mnTab, ATTR_MERGE_FLAG);
                    bool bOverlapped = (pMergeFlag->IsHorOverlapped() || pMergeFlag->IsVerOverlapped());

                    tools::Long nPosY = nRowPosY;
                    if (bOverlapped)
                    {
                        nY = mpRowInfo[nArrY].nRowNo;
                        SCCOL nOverX;                   // start of the merged cells
                        SCROW nOverY;
                        if (GetMergeOrigin( nX,nY, nArrY, nOverX,nOverY, true ))
                        {
                            nCellX = nOverX;
                            nCellY = nOverY;
                            bDoCell = true;
                        }
                    }
                    else if ( nX == mnX2 && pThisRowInfo->cellInfo(nX).maCell.isEmpty() )
                    {
                        //  Rest of a long text further to the right?

                        SCCOL nTempX=nX;
                        while (nTempX < nLastContentCol && IsEmptyCellText( pThisRowInfo, nTempX, nY ))
                            ++nTempX;

                        if ( nTempX > nX &&
                             !IsEmptyCellText( pThisRowInfo, nTempX, nY ) &&
                             !mpDoc->HasAttrib( nTempX,nY,mnTab, nX,nY,mnTab, HasAttrFlags::Merged | HasAttrFlags::Overlapped ) )
                        {
                            nCellX = nTempX;
                            bDoCell = true;
                        }
                    }
                    else
                    {
                        bDoCell = true;
                    }

                    if ( bDoCell && mbEditMode && nCellX == mnEditCol && nCellY == mnEditRow )
                        bDoCell = false;

                    const ScPatternAttr* pPattern = nullptr;
                    const SfxItemSet* pCondSet = nullptr;
                    if (bDoCell)
                    {
                        if ( nCellY == nY && nCellX >= mnX1 && nCellX <= mnX2 &&
                             !mpDoc->ColHidden(nCellX, mnTab) )
                        {
                            ScCellInfo& rCellInfo = pThisRowInfo->cellInfo(nCellX);
                            pPattern = rCellInfo.pPatternAttr;
                            pCondSet = rCellInfo.pConditionSet;
                            aCell = rCellInfo.maCell;
                        }
                        else        // get from document
                        {
                            pPattern = mpDoc->GetPattern( nCellX, nCellY, mnTab );
                            pCondSet = mpDoc->GetCondResult( nCellX, nCellY, mnTab );
                            GetVisibleCell( nCellX, nCellY, mnTab, aCell );
                        }
                        if (aCell.isEmpty())
                            bDoCell = false;
                    }
                    if (bDoCell)
                    {
                        if ( mpDoc->GetPreviewCellStyle() )
                        {
                            if ( ScStyleSheet* pPreviewStyle = mpDoc->GetPreviewCellStyle( nCellX, nCellY, mnTab ) )
                            {
                                pPreviewPattr.reset( new ScPatternAttr(*pPattern) );
                                pPreviewPattr->SetStyleSheet(pPreviewStyle);
                                pPattern = pPreviewPattr.get();
                            }
                        }
                        SfxItemSet* pPreviewFontSet = mpDoc->GetPreviewFont( nCellX, nCellY, mnTab );
                        lcl_ClearEdit( *mxOutputEditEngine );      // also calls SetUpdateMode(sal_False)

                        // fdo#32530: Check if the first character is RTL.
                        OUString aStr = mpDoc->GetString(nCellX, nCellY, mnTab);

                        DrawEditParam aParam(pPattern, pCondSet, lcl_SafeIsValue(aCell));
                        const bool bNumberFormatIsText = lcl_isNumberFormatText( mpDoc, nCellX, nCellY, mnTab );
                        aParam.meHorJustContext = getAlignmentFromContext( aParam.meHorJustAttr,
                                aParam.mbCellIsValue, aStr, *pPattern, pCondSet, mpDoc, mnTab, bNumberFormatIsText);
                        aParam.meHorJustResult = (aParam.meHorJustAttr == SvxCellHorJustify::Block) ?
                                SvxCellHorJustify::Block : aParam.meHorJustContext;
                        aParam.mbPixelToLogic = bPixelToLogic;
                        aParam.mbHyphenatorSet = bHyphenatorSet;
                        aParam.mpEngine = mxOutputEditEngine.get();
                        aParam.maCell = aCell;
                        aParam.mnArrY = nArrY;
                        aParam.mnX = nX;
                        aParam.mnCellX = nCellX;
                        aParam.mnCellY = nCellY;
                        aParam.mnPosX = nPosX;
                        aParam.mnPosY = nPosY;
                        aParam.mnInitPosX = nInitPosX;
                        aParam.mpPreviewFontSet = pPreviewFontSet;
                        aParam.mpOldPattern = pOldPattern;
                        aParam.mpOldCondSet = pOldCondSet;
                        aParam.mpOldPreviewFontSet = pOldPreviewFontSet;
                        aParam.mpThisRowInfo = pThisRowInfo;
                        if (mpSpellCheckCxt)
                            aParam.maMisspellRanges = mpSpellCheckCxt->getMisspellRanges(nCellX, nCellY);

                        if (aParam.meHorJustAttr == SvxCellHorJustify::Repeat)
                        {
                            // ignore orientation/rotation if "repeat" is active
                            aParam.meOrient = SvxCellOrientation::Standard;
                        }
                        switch (aParam.meOrient)
                        {
                            case SvxCellOrientation::BottomUp:
                                DrawEditBottomTop(aParam);
                            break;
                            case SvxCellOrientation::TopBottom:
                                DrawEditTopBottom(aParam);
                            break;
                            case SvxCellOrientation::Stacked:
                                // this can be vertically stacked or asian vertical.
                                DrawEditStacked(aParam);
                            break;
                            default:
                                DrawEditStandard(aParam);
                        }

                        // Retrieve parameters for next iteration.
                        pOldPattern = aParam.mpOldPattern;
                        pOldCondSet = aParam.mpOldCondSet;
                        pOldPreviewFontSet = aParam.mpOldPreviewFontSet;
                        bHyphenatorSet = aParam.mbHyphenatorSet;
                    }
                    if (bReopenTag)
                        pPDF->EndStructureElement();
                }
                nPosX += mpRowInfo[0].basicCellInfo(nX).nWidth * nLayoutSign;
            }
        }
        nRowPosY += mpRowInfo[nArrY].nHeight;
    }

    if (mrTabInfo.maArray.HasCellRotation())
    {
        DrawRotated(bPixelToLogic);     //! call from outside ?
    }
}

void ScOutputData::DrawRotated(bool bPixelToLogic)
{
    InitOutputEditEngine();
    //! store nRotMax
    SCCOL nRotMax = mnX2;
    for (SCSIZE nRotY=0; nRotY<mnArrCount; nRotY++)
        if (mpRowInfo[nRotY].nRotMaxCol != SC_ROTMAX_NONE && mpRowInfo[nRotY].nRotMaxCol > nRotMax)
            nRotMax = mpRowInfo[nRotY].nRotMaxCol;

    Color nConfBackColor = GetConfBackgroundColor();
    bool bCellContrast = mbUseStyleColor &&
            Application::GetSettings().GetStyleSettings().GetHighContrastMode();

    bool bHyphenatorSet = false;
    const ScPatternAttr* pPattern;
    const SfxItemSet*    pCondSet;
    const ScPatternAttr* pOldPattern = nullptr;
    const SfxItemSet*    pOldCondSet = nullptr;
    ScRefCellValue aCell;

    tools::Long nInitPosX = mnScrX;
    if ( mbLayoutRTL )
    {
        nInitPosX += mnMirrorW - 1;
    }
    tools::Long nLayoutSign = mbLayoutRTL ? -1 : 1;

    tools::Long nRowPosY = mnScrY;
    for (SCSIZE nArrY=0; nArrY+1<mnArrCount; nArrY++)            // 0 for the rest of the merged
    {
        RowInfo* pThisRowInfo = &mpRowInfo[nArrY];
        tools::Long nCellHeight = static_cast<tools::Long>(pThisRowInfo->nHeight);
        if (nArrY==1) nRowPosY = mnScrY;                         // positions before are calculated individually

        if ( ( pThisRowInfo->bChanged || nArrY==0 ) && pThisRowInfo->nRotMaxCol != SC_ROTMAX_NONE )
        {
            tools::Long nPosX = 0;
            for (SCCOL nX=0; nX<=nRotMax; nX++)
            {
                if (nX==mnX1) nPosX = nInitPosX;                 // positions before mnX1 are calculated individually

                const ScCellInfo* pInfo = &pThisRowInfo->cellInfo(nX);
                if ( pInfo->nRotateDir != ScRotateDir::NONE )
                {
                    SCROW nY = pThisRowInfo->nRowNo;

                    bool bHidden = false;
                    if (mbEditMode)
                        if ( nX == mnEditCol && nY == mnEditRow )
                            bHidden = true;

                    if (!bHidden)
                    {
                        lcl_ClearEdit( *mxOutputEditEngine );      // also calls SetUpdateMode(sal_False)

                        tools::Long nPosY = nRowPosY;

                        //! rest from merged cells further up do not work!

                        bool bFromDoc = false;
                        pPattern = pInfo->pPatternAttr;
                        pCondSet = pInfo->pConditionSet;
                        if (!pPattern)
                        {
                            pPattern = mpDoc->GetPattern( nX, nY, mnTab );
                            bFromDoc = true;
                        }
                        aCell = pInfo->maCell;
                        if (bFromDoc)
                            pCondSet = mpDoc->GetCondResult( nX, nY, mnTab );

                        if (aCell.isEmpty() && nX>mnX2)
                            GetVisibleCell( nX, nY, mnTab, aCell );

                        if (aCell.isEmpty() || IsEmptyCellText(pThisRowInfo, nX, nY))
                            bHidden = true;     // nRotateDir is also set without a cell

                        tools::Long nCellWidth = static_cast<tools::Long>(mpRowInfo[0].basicCellInfo(nX).nWidth);

                        SvxCellHorJustify eHorJust =
                                            pPattern->GetItem(ATTR_HOR_JUSTIFY, pCondSet).GetValue();
                        bool bBreak = ( eHorJust == SvxCellHorJustify::Block ) ||
                                    pPattern->GetItem(ATTR_LINEBREAK, pCondSet).GetValue();
                        bool bRepeat = ( eHorJust == SvxCellHorJustify::Repeat && !bBreak );
                        bool bShrink = !bBreak && !bRepeat &&
                                        pPattern->GetItem( ATTR_SHRINKTOFIT, pCondSet ).GetValue();
                        SvxCellOrientation eOrient = pPattern->GetCellOrientation( pCondSet );

                        const ScMergeAttr* pMerge = &pPattern->GetItem(ATTR_MERGE);
                        bool bMerged = pMerge->GetColMerge() > 1 || pMerge->GetRowMerge() > 1;

                        tools::Long nStartX = nPosX;
                        tools::Long nStartY = nPosY;
                        if (nX<mnX1)
                        {
                            if ((bBreak || eOrient!=SvxCellOrientation::Standard) && !bMerged)
                                bHidden = true;
                            else
                            {
                                nStartX = nInitPosX;
                                SCCOL nCol = mnX1;
                                while (nCol > nX)
                                {
                                    --nCol;
                                    nStartX -= nLayoutSign * static_cast<tools::Long>(mpRowInfo[0].basicCellInfo(nCol).nWidth);
                                }
                            }
                        }
                        tools::Long nCellStartX = nStartX;

                        // omit substitute representation of small text

                        if (!bHidden)
                        {
                            tools::Long nOutWidth = nCellWidth - 1;
                            tools::Long nOutHeight = nCellHeight;

                            if ( bMerged )
                            {
                                SCCOL nCountX = pMerge->GetColMerge();
                                for (SCCOL i=1; i<nCountX; i++)
                                    nOutWidth += mpDoc->GetColWidth(nX+i,mnTab) * mnPPTX;
                                SCROW nCountY = pMerge->GetRowMerge();
                                nOutHeight += mpDoc->GetScaledRowHeight( nY+1, nY+nCountY-1, mnTab, mnPPTY);
                            }

                            SvxCellVerJustify eVerJust =
                                                pPattern->GetItem(ATTR_VER_JUSTIFY, pCondSet).GetValue();

                            // syntax mode is ignored here...

                            // StringDiffer doesn't look at hyphenate, language items
                            if ( !ScPatternAttr::areSame(pPattern, pOldPattern) || pCondSet != pOldCondSet )
                            {
                                SfxItemSet aSet( mxOutputEditEngine->GetEmptyItemSet() );
                                pPattern->FillEditItemSet( &aSet, pCondSet );

                                                                    // adjustment for EditEngine
                                SvxAdjust eSvxAdjust = SvxAdjust::Left;
                                if (eOrient==SvxCellOrientation::Stacked)
                                    eSvxAdjust = SvxAdjust::Center;
                                // adjustment for bBreak is omitted here
                                aSet.Put( SvxAdjustItem( eSvxAdjust, EE_PARA_JUST ) );

                                bool bParaHyphenate = aSet.Get(EE_PARA_HYPHENATE).GetValue();
                                mxOutputEditEngine->SetDefaults( std::move(aSet) );
                                pOldPattern = pPattern;
                                pOldCondSet = pCondSet;

                                EEControlBits nControl = mxOutputEditEngine->GetControlWord();
                                if (eOrient==SvxCellOrientation::Stacked)
                                    nControl |= EEControlBits::ONECHARPERLINE;
                                else
                                    nControl &= ~EEControlBits::ONECHARPERLINE;
                                mxOutputEditEngine->SetControlWord( nControl );

                                if ( !bHyphenatorSet && bParaHyphenate )
                                {
                                    //  set hyphenator the first time it is needed
                                    css::uno::Reference<css::linguistic2::XHyphenator> xXHyphenator( LinguMgr::GetHyphenator() );
                                    mxOutputEditEngine->SetHyphenator( xXHyphenator );
                                    bHyphenatorSet = true;
                                }

                                Color aBackCol =
                                    pPattern->GetItem( ATTR_BACKGROUND, pCondSet ).GetColor();
                                if ( mbUseStyleColor && ( aBackCol.IsTransparent() || bCellContrast ) )
                                    aBackCol = nConfBackColor;
                                mxOutputEditEngine->SetBackgroundColor( aBackCol );
                            }

                            // margins

                            //! change position and paper size to EditUtil !!!

                            const SvxMarginItem* pMargin =
                                                    &pPattern->GetItem(ATTR_MARGIN, pCondSet);
                            sal_uInt16 nIndent = 0;
                            if ( eHorJust == SvxCellHorJustify::Left )
                                nIndent = pPattern->GetItem(ATTR_INDENT, pCondSet).GetValue();

                            tools::Long nTotalHeight = nOutHeight; // without subtracting the margin
                            if ( bPixelToLogic )
                                nTotalHeight = mpRefDevice->PixelToLogic(Size(0,nTotalHeight)).Height();

                            tools::Long nLeftM = static_cast<tools::Long>( (pMargin->GetLeftMargin() + nIndent) * mnPPTX );
                            tools::Long nTopM  = static_cast<tools::Long>( pMargin->GetTopMargin() * mnPPTY );
                            tools::Long nRightM  = static_cast<tools::Long>( pMargin->GetRightMargin() * mnPPTX );
                            tools::Long nBottomM = static_cast<tools::Long>( pMargin->GetBottomMargin() * mnPPTY );
                            nStartX += nLeftM;
                            nStartY += nTopM;
                            nOutWidth -= nLeftM + nRightM;
                            nOutHeight -= nTopM + nBottomM;

                            // rotate here already, to adjust paper size for page breaks
                            Degree100 nAttrRotate;
                            double nSin = 0.0;
                            double nCos = 1.0;
                            SvxRotateMode eRotMode = SVX_ROTATE_MODE_STANDARD;
                            if ( eOrient == SvxCellOrientation::Standard )
                            {
                                nAttrRotate = pPattern->
                                                    GetItem(ATTR_ROTATE_VALUE, pCondSet).GetValue();
                                if ( nAttrRotate )
                                {
                                    eRotMode = pPattern->GetItem(ATTR_ROTATE_MODE, pCondSet).GetValue();

                                    // tdf#143377 To use the same limits to avoid too big Skew
                                    // with TextOrientation in Calc, use 1/2 degree here, too.
                                    // This equals '50' in the notation here (100th degree)
                                    static const sal_Int32 nMinRad(50);

                                    // bring nAttrRotate to the range [0..36000[
                                    nAttrRotate = Degree100(((nAttrRotate.get() % 36000) + 36000) % 36000);

                                    // check for to be avoided extreme values and correct
                                    if (nAttrRotate < Degree100(nMinRad))
                                    {
                                        // range [0..50]
                                        nAttrRotate = Degree100(nMinRad);
                                        eRotMode = SVX_ROTATE_MODE_STANDARD;    // no overflow
                                    }
                                    else if (nAttrRotate > Degree100(36000 - nMinRad))
                                    {
                                        // range [35950..36000[
                                        nAttrRotate = Degree100(36000 - nMinRad);
                                        eRotMode = SVX_ROTATE_MODE_STANDARD;    // no overflow
                                    }
                                    else if (nAttrRotate > Degree100(18000 - nMinRad) && (nAttrRotate < Degree100(18000 + nMinRad)))
                                    {
                                        // range 50 around 18000, [17950..18050]
                                        nAttrRotate = (nAttrRotate > Degree100(18000))
                                            ? Degree100(18000 + nMinRad)
                                            : Degree100(18000 - nMinRad);
                                        eRotMode = SVX_ROTATE_MODE_STANDARD;    // no overflow
                                    }

                                    if ( mbLayoutRTL )
                                    {
                                        // keep in range [0..36000[
                                        nAttrRotate = Degree100(36000 - nAttrRotate.get());
                                    }

                                    double nRealOrient = toRadians(nAttrRotate);   // 1/100 degree
                                    nCos = cos( nRealOrient );

                                    // tdf#143377 new strategy: instead of using zero for nSin, which
                                    // would be the *correct* value, continue with the corrected maximum
                                    // allowed value which is then *not* zero. This is similar to
                                    // the behaviour before where (just due to numerical unprecisions)
                                    // nSin was also not zero (pure coincidence), but very close to it.
                                    // I checked and tried to make safe all places below that use
                                    // nSin and divide by it, but there is too much going on and that
                                    // would not be safe, so rely on the same values as before, but
                                    // now numerically limited to not get the Skew go havoc
                                    nSin = sin( nRealOrient );
                                }
                            }

                            Size aPaperSize( 1000000, 1000000 );
                            if (eOrient==SvxCellOrientation::Stacked)
                                aPaperSize.setWidth( nOutWidth );             // to center
                            else if (bBreak)
                            {
                                if (nAttrRotate)
                                {
                                    //! the correct paper size for break depends on the number
                                    //! of rows, as long as the rows can not be outputted individually
                                    //! offsetted -> therefore unlimited, so no wrapping.
                                    //! With offset rows the following would be correct:
                                    aPaperSize.setWidth( static_cast<tools::Long>(nOutHeight / fabs(nSin)) );
                                }
                                else if (eOrient == SvxCellOrientation::Standard)
                                    aPaperSize.setWidth( nOutWidth );
                                else
                                    aPaperSize.setWidth( nOutHeight - 1 );
                            }
                            if (bPixelToLogic)
                                mxOutputEditEngine->SetPaperSize(mpRefDevice->PixelToLogic(aPaperSize));
                            else
                                mxOutputEditEngine->SetPaperSize(aPaperSize);  // scale is always 1

                            // read data from cell

                            if (aCell.getType() == CELLTYPE_EDIT)
                            {
                                if (aCell.getEditText())
                                    mxOutputEditEngine->SetTextCurrentDefaults(*aCell.getEditText());
                                else
                                {
                                    OSL_FAIL("pData == 0");
                                }
                            }
                            else
                            {
                                sal_uInt32 nFormat = pPattern->GetNumberFormat(
                                                            mpDoc->GetFormatTable(), pCondSet );
                                const Color* pColor;
                                OUString aString = ScCellFormat::GetString( aCell,
                                                         nFormat, &pColor,
                                                         nullptr,
                                                         *mpDoc,
                                                         mbShowNullValues,
                                                         mbShowFormulas);

                                mxOutputEditEngine->SetTextCurrentDefaults(aString);
                                if ( pColor && !mbSyntaxMode && !( mbUseStyleColor && mbForceAutoColor ) )
                                    lcl_SetEditColor( *mxOutputEditEngine, *pColor );
                            }

                            if ( mbSyntaxMode )
                            {
                                SetEditSyntaxColor(*mxOutputEditEngine, aCell);
                            }
                            else if ( mbUseStyleColor && mbForceAutoColor )
                                lcl_SetEditColor( *mxOutputEditEngine, COL_AUTO );     //! or have a flag at EditEngine

                            mxOutputEditEngine->SetUpdateLayout( true );     // after SetText, before CalcTextWidth/GetTextHeight

                            tools::Long nEngineWidth  = static_cast<tools::Long>(mxOutputEditEngine->CalcTextWidth());
                            tools::Long nEngineHeight = mxOutputEditEngine->GetTextHeight();

                            if (nAttrRotate && bBreak)
                            {
                                double nAbsCos = fabs( nCos );
                                double nAbsSin = fabs( nSin );

                                // adjust width of papersize for height of text
                                int nSteps = 5;
                                while (nSteps > 0)
                                {
                                    // everything is in pixels
                                    tools::Long nEnginePixel = mpRefDevice->LogicToPixel(
                                                            Size(0,nEngineHeight)).Height();
                                    tools::Long nEffHeight = nOutHeight - static_cast<tools::Long>(nEnginePixel * nAbsCos) + 2;
                                    tools::Long nNewWidth = static_cast<tools::Long>(nEffHeight / nAbsSin) + 2;
                                    bool bFits = ( nNewWidth >= aPaperSize.Width() );
                                    if ( bFits )
                                        nSteps = 0;
                                    else
                                    {
                                        if ( nNewWidth < 4 )
                                        {
                                            // can't fit -> fall back to using half height
                                            nEffHeight = nOutHeight / 2;
                                            nNewWidth = static_cast<tools::Long>(nEffHeight / nAbsSin) + 2;
                                            nSteps = 0;
                                        }
                                        else
                                            --nSteps;

                                        // set paper width and get new text height
                                        aPaperSize.setWidth( nNewWidth );
                                        if (bPixelToLogic)
                                            mxOutputEditEngine->SetPaperSize(mpRefDevice->PixelToLogic(aPaperSize));
                                        else
                                            mxOutputEditEngine->SetPaperSize(aPaperSize);  // Scale is always 1
                                        //mxOutputEditEngine->QuickFormatDoc( sal_True );

                                        nEngineWidth  = static_cast<tools::Long>(mxOutputEditEngine->CalcTextWidth());
                                        nEngineHeight = mxOutputEditEngine->GetTextHeight();
                                    }
                                }
                            }

                            tools::Long nRealWidth  = nEngineWidth;
                            tools::Long nRealHeight = nEngineHeight;

                            // when rotated, adjust size
                            if (nAttrRotate)
                            {
                                double nAbsCos = fabs( nCos );
                                double nAbsSin = fabs( nSin );

                                if ( eRotMode == SVX_ROTATE_MODE_STANDARD )
                                    nEngineWidth = static_cast<tools::Long>( nRealWidth * nAbsCos +
                                                            nRealHeight * nAbsSin );
                                else
                                    nEngineWidth = static_cast<tools::Long>( nRealHeight / nAbsSin );
                                //! limit !!!

                                nEngineHeight = static_cast<tools::Long>( nRealHeight * nAbsCos +
                                                         nRealWidth * nAbsSin );
                            }

                            if (!nAttrRotate)           //  only rotated text here
                                bHidden = true;         //! check first !!!

                            //! omit which doesn't stick out

                            if (!bHidden)
                            {
                                Size aClipSize( mnScrX+mnScrW-nStartX, mnScrY+mnScrH-nStartY );

                                // go on writing

                                Size aCellSize;
                                if (bPixelToLogic)
                                    aCellSize = mpRefDevice->PixelToLogic( Size( nOutWidth, nOutHeight ) );
                                else
                                    aCellSize = Size( nOutWidth, nOutHeight );  // scale is one

                                tools::Long nGridWidth = nEngineWidth;
                                bool bNegative = false;
                                if ( eRotMode != SVX_ROTATE_MODE_STANDARD )
                                {
                                    nGridWidth = aCellSize.Width() +
                                            std::abs(static_cast<tools::Long>( aCellSize.Height() * nCos / nSin ));
                                    bNegative = ( pInfo->nRotateDir == ScRotateDir::Left );
                                    if ( mbLayoutRTL )
                                        bNegative = !bNegative;
                                }

                                // use GetOutputArea to hide the grid
                                // (clip region is done manually below)
                                OutputAreaParam aAreaParam;

                                SCCOL nCellX = nX;
                                SCROW nCellY = nY;
                                SvxCellHorJustify eOutHorJust = eHorJust;
                                if ( eRotMode != SVX_ROTATE_MODE_STANDARD )
                                    eOutHorJust = bNegative ? SvxCellHorJustify::Right : SvxCellHorJustify::Left;
                                tools::Long nNeededWidth = nGridWidth;     // in pixel for GetOutputArea
                                if ( bPixelToLogic )
                                    nNeededWidth =  mpRefDevice->LogicToPixel(Size(nNeededWidth,0)).Width();

                                GetOutputArea( nX, nArrY, nCellStartX, nPosY, nCellX, nCellY, nNeededWidth,
                                                *pPattern, sal::static_int_cast<sal_uInt16>(eOutHorJust),
                                                false, false, true, aAreaParam );

                                if ( bShrink )
                                {
                                    tools::Long nPixelWidth = bPixelToLogic ?
                                        mpRefDevice->LogicToPixel(Size(nEngineWidth,0)).Width() : nEngineWidth;
                                    tools::Long nNeededPixel = nPixelWidth + nLeftM + nRightM;

                                    aAreaParam.mbLeftClip = aAreaParam.mbRightClip = true;

                                    // always do height
                                    ShrinkEditEngine( *mxOutputEditEngine, aAreaParam.maAlignRect, nLeftM, nTopM, nRightM, nBottomM,
                                        false, eOrient, nAttrRotate, bPixelToLogic,
                                        nEngineWidth, nEngineHeight, nNeededPixel, aAreaParam.mbLeftClip, aAreaParam.mbRightClip );

                                    if ( eRotMode == SVX_ROTATE_MODE_STANDARD )
                                    {
                                        // do width only if rotating within the cell (standard mode)
                                        ShrinkEditEngine( *mxOutputEditEngine, aAreaParam.maAlignRect, nLeftM, nTopM, nRightM, nBottomM,
                                            true, eOrient, nAttrRotate, bPixelToLogic,
                                            nEngineWidth, nEngineHeight, nNeededPixel, aAreaParam.mbLeftClip, aAreaParam.mbRightClip );
                                    }

                                    // nEngineWidth/nEngineHeight is updated in ShrinkEditEngine
                                    // (but width is only valid for standard mode)
                                    nRealWidth  = static_cast<tools::Long>(mxOutputEditEngine->CalcTextWidth());
                                    nRealHeight = mxOutputEditEngine->GetTextHeight();

                                    if ( eRotMode != SVX_ROTATE_MODE_STANDARD )
                                        nEngineWidth = static_cast<tools::Long>( nRealHeight / fabs( nSin ) );
                                }

                                tools::Long nClipStartX = nStartX;
                                if (nX<mnX1)
                                {
                                    //! clipping is not needed when on the left side of the window

                                    if (nStartX<mnScrX)
                                    {
                                        tools::Long nDif = mnScrX - nStartX;
                                        nClipStartX = mnScrX;
                                        aClipSize.AdjustWidth( -nDif );
                                    }
                                }

                                tools::Long nClipStartY = nStartY;
                                if (nArrY==0 && nClipStartY < nRowPosY )
                                {
                                    tools::Long nDif = nRowPosY - nClipStartY;
                                    nClipStartY = nRowPosY;
                                    aClipSize.AdjustHeight( -nDif );
                                }

                                if ( nAttrRotate /* && eRotMode != SVX_ROTATE_MODE_STANDARD */ )
                                {
                                    // only clip rotated output text at the page border
                                    nClipStartX = mnScrX;
                                    aClipSize.setWidth( mnScrW );
                                }

                                if (bPixelToLogic)
                                    aAreaParam.maClipRect = mpRefDevice->PixelToLogic( tools::Rectangle(
                                                    Point(nClipStartX,nClipStartY), aClipSize ) );
                                else
                                    aAreaParam.maClipRect = tools::Rectangle(Point(nClipStartX, nClipStartY),
                                                            aClipSize );    // Scale = 1

                                if (mbMetaFile)
                                {
                                    mpDev->Push();
                                    mpDev->IntersectClipRegion( aAreaParam.maClipRect );
                                }
                                else
                                    mpDev->SetClipRegion( vcl::Region( aAreaParam.maClipRect ) );

                                Point aLogicStart;
                                if (bPixelToLogic)
                                    aLogicStart = mpRefDevice->PixelToLogic( Point(nStartX,nStartY) );
                                else
                                    aLogicStart = Point(nStartX, nStartY);
                                if ( eOrient!=SvxCellOrientation::Standard || !bBreak )
                                {
                                    tools::Long nAvailWidth = aCellSize.Width();
                                    if (meType==OUTTYPE_WINDOW &&
                                            eOrient!=SvxCellOrientation::Stacked &&
                                            pInfo->bAutoFilter)
                                    {
                                        // filter drop-down width depends on row height
                                        double fZoom = mpRefDevice ? static_cast<double>(mpRefDevice->GetMapMode().GetScaleY()) : 1.0;
                                        fZoom = fZoom > 1.0 ? fZoom : 1.0;
                                        if (bPixelToLogic)
                                            nAvailWidth -= mpRefDevice->PixelToLogic(Size(0,fZoom * DROPDOWN_BITMAP_SIZE)).Height();
                                        else
                                            nAvailWidth -= fZoom * DROPDOWN_BITMAP_SIZE;
                                        tools::Long nComp = nEngineWidth;
                                        if (nAvailWidth<nComp) nAvailWidth=nComp;
                                    }

                                    // horizontal orientation

                                    if (eOrient==SvxCellOrientation::Standard && !nAttrRotate)
                                    {
                                        if (eHorJust==SvxCellHorJustify::Right ||
                                            eHorJust==SvxCellHorJustify::Center)
                                        {
                                            mxOutputEditEngine->SetUpdateLayout( false );

                                            SvxAdjust eSvxAdjust =
                                                (eHorJust==SvxCellHorJustify::Right) ?
                                                    SvxAdjust::Right : SvxAdjust::Center;
                                            mxOutputEditEngine->SetDefaultItem(
                                                SvxAdjustItem( eSvxAdjust, EE_PARA_JUST ) );

                                            aPaperSize.setWidth( nOutWidth );
                                            if (bPixelToLogic)
                                                mxOutputEditEngine->SetPaperSize(mpRefDevice->PixelToLogic(aPaperSize));
                                            else
                                                mxOutputEditEngine->SetPaperSize(aPaperSize);

                                            mxOutputEditEngine->SetUpdateLayout( true );
                                        }
                                    }
                                    else
                                    {
                                        // rotated text is centered by default
                                        if (eHorJust==SvxCellHorJustify::Right)
                                            aLogicStart.AdjustX(nAvailWidth - nEngineWidth );
                                        else if (eHorJust==SvxCellHorJustify::Center ||
                                                 eHorJust==SvxCellHorJustify::Standard)
                                            aLogicStart.AdjustX((nAvailWidth - nEngineWidth) / 2 );
                                    }
                                }

                                if ( mbLayoutRTL )
                                {
                                    if (bPixelToLogic)
                                        aLogicStart.AdjustX( -(mpRefDevice->PixelToLogic(
                                                        Size( nCellWidth, 0 ) ).Width()) );
                                    else
                                        aLogicStart.AdjustX( -nCellWidth );
                                }

                                if ( eOrient==SvxCellOrientation::Standard ||
                                     eOrient==SvxCellOrientation::Stacked || !bBreak )
                                {
                                    if (eVerJust==SvxCellVerJustify::Bottom ||
                                        eVerJust==SvxCellVerJustify::Standard)
                                    {
                                        if (bPixelToLogic)
                                            aLogicStart.AdjustY(mpRefDevice->PixelToLogic( Size(0,
                                                            mpRefDevice->LogicToPixel(aCellSize).Height() -
                                                            mpRefDevice->LogicToPixel(Size(0,nEngineHeight)).Height()
                                                            )).Height() );
                                        else
                                            aLogicStart.AdjustY(aCellSize.Height() - nEngineHeight );
                                    }

                                    else if (eVerJust==SvxCellVerJustify::Center)
                                    {
                                        if (bPixelToLogic)
                                            aLogicStart.AdjustY(mpRefDevice->PixelToLogic( Size(0,(
                                                            mpRefDevice->LogicToPixel(aCellSize).Height() -
                                                            mpRefDevice->LogicToPixel(Size(0,nEngineHeight)).Height())
                                                            / 2)).Height() );
                                        else
                                            aLogicStart.AdjustY((aCellSize.Height() - nEngineHeight) / 2 );
                                    }
                                }

                                // TOPBOTTOM and BOTTOMTOP are handled in DrawStrings/DrawEdit
                                OSL_ENSURE( eOrient == SvxCellOrientation::Standard && nAttrRotate,
                                            "DrawRotated: no rotation" );

                                Degree10 nOriVal = 0_deg10;
                                if ( nAttrRotate )
                                {
                                    // attribute is 1/100, Font 1/10 degrees
                                    nOriVal = to<Degree10>(nAttrRotate);

                                    double nAddX = 0.0;
                                    double nAddY = 0.0;
                                    if ( nCos > 0.0 && eRotMode != SVX_ROTATE_MODE_STANDARD )
                                    {
                                        //! limit !!!
                                        double nH = nRealHeight * nCos;
                                        nAddX += nH * ( nCos / fabs(nSin) );
                                    }
                                    if ( nCos < 0.0 && eRotMode == SVX_ROTATE_MODE_STANDARD )
                                        nAddX -= nRealWidth * nCos;
                                    if ( nSin < 0.0 )
                                        nAddX -= nRealHeight * nSin;
                                    if ( nSin > 0.0 )
                                        nAddY += nRealWidth * nSin;
                                    if ( nCos < 0.0 )
                                        nAddY -= nRealHeight * nCos;

                                    if ( eRotMode != SVX_ROTATE_MODE_STANDARD )
                                    {
                                        //! limit !!!
                                        double nSkew = nTotalHeight * nCos / fabs(nSin);
                                        if ( eRotMode == SVX_ROTATE_MODE_CENTER )
                                            nAddX -= nSkew * 0.5;
                                        if ( ( eRotMode == SVX_ROTATE_MODE_TOP && nSin > 0.0 ) ||
                                             ( eRotMode == SVX_ROTATE_MODE_BOTTOM && nSin < 0.0 ) )
                                            nAddX -= nSkew;

                                        tools::Long nUp = 0;
                                        if ( eVerJust == SvxCellVerJustify::Center )
                                            nUp = ( aCellSize.Height() - nEngineHeight ) / 2;
                                        else if ( eVerJust == SvxCellVerJustify::Top )
                                        {
                                            if ( nSin > 0.0 )
                                                nUp = aCellSize.Height() - nEngineHeight;
                                        }
                                        else    // BOTTOM / STANDARD
                                        {
                                            if ( nSin < 0.0 )
                                                nUp = aCellSize.Height() - nEngineHeight;
                                        }
                                        if ( nUp )
                                            nAddX += ( nUp * nCos / fabs(nSin) );
                                    }

                                    aLogicStart.AdjustX(static_cast<tools::Long>(nAddX) );
                                    aLogicStart.AdjustY(static_cast<tools::Long>(nAddY) );
                                }

                                //  bSimClip is not used here (because nOriVal is set)

                                mxOutputEditEngine->DrawText_ToPosition(*mpDev, aLogicStart, nOriVal);

                                if (mbMetaFile)
                                    mpDev->Pop();
                                else
                                    mpDev->SetClipRegion();
                            }
                        }
                    }
                }
                nPosX += mpRowInfo[0].basicCellInfo(nX).nWidth * nLayoutSign;
            }
        }
        nRowPosY += mpRowInfo[nArrY].nHeight;
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
