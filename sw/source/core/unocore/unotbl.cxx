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

#include <tuple>
#include <utility>
#include <memory>
#include <vector>
#include <algorithm>
#include <limits>

#include <comphelper/interfacecontainer4.hxx>
#include <o3tl/any.hxx>
#include <o3tl/safeint.hxx>
#include <tools/UnitConversion.hxx>
#include <editeng/memberids.h>
#include <float.h>
#include <swmodule.hxx>
#include <swtypes.hxx>
#include <cmdid.h>
#include <unocoll.hxx>
#include <unomid.h>
#include <unomap.hxx>
#include <unotbl.hxx>
#include <unosection.hxx>
#include <section.hxx>
#include <unocrsr.hxx>
#include <hints.hxx>
#include <swtblfmt.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentContentOperations.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentState.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <shellres.hxx>
#include <docary.hxx>
#include <ndole.hxx>
#include <ndtxt.hxx>
#include <frame.hxx>
#include <vcl/svapp.hxx>
#include <fmtfsize.hxx>
#include <tblafmt.hxx>
#include <tabcol.hxx>
#include <cellatr.hxx>
#include <fmtpdsc.hxx>
#include <pagedesc.hxx>
#include <viewsh.hxx>
#include <rootfrm.hxx>
#include <tabfrm.hxx>
#include <redline.hxx>
#include <unoport.hxx>
#include <unocrsrhelper.hxx>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/text/WrapTextMode.hpp>
#include <com/sun/star/text/TextContentAnchorType.hpp>
#include <com/sun/star/text/TableColumnSeparator.hpp>
#include <com/sun/star/text/VertOrientation.hpp>
#include <com/sun/star/text/XTextSection.hpp>
#include <com/sun/star/table/TableBorder.hpp>
#include <com/sun/star/table/TableBorder2.hpp>
#include <com/sun/star/table/BorderLine2.hpp>
#include <com/sun/star/table/TableBorderDistances.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/chart/XChartDataChangeEventListener.hpp>
#include <com/sun/star/chart/ChartDataChangeEvent.hpp>
#include <com/sun/star/table/CellContentType.hpp>
#include <unotextrange.hxx>
#include <unotextcursor.hxx>
#include <unoparagraph.hxx>
#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>
#include <editeng/formatbreakitem.hxx>
#include <editeng/shaditem.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/ulspitem.hxx>
#include <fmtornt.hxx>
#include <editeng/keepitem.hxx>
#include <fmtlsplt.hxx>
#include <swundo.hxx>
#include <SwStyleNameMapper.hxx>
#include <frmatr.hxx>
#include <sortopt.hxx>
#include <sal/log.hxx>
#include <editeng/frmdiritem.hxx>
#include <comphelper/servicehelper.hxx>
#include <comphelper/string.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <swtable.hxx>
#include <docsh.hxx>
#include <fesh.hxx>
#include <itabenum.hxx>
#include <frameformats.hxx>
#include <names.hxx>
#include <o3tl/string_view.hxx>

using namespace ::com::sun::star;
using ::editeng::SvxBorderLine;

namespace
{
    struct FindUnoCellInstanceHint final : SfxHint
    {
        FindUnoCellInstanceHint(const SwTableBox* pCore) : SfxHint(SfxHintId::SwFindUnoCellInstance), m_pCore(pCore) {};
        const SwTableBox* const m_pCore;
        mutable rtl::Reference<SwXCell> m_pResult;
    };
    struct FindUnoTextTableRowInstanceHint final : SfxHint
    {
        FindUnoTextTableRowInstanceHint(const SwTableLine* pCore) : SfxHint(SfxHintId::SwFindUnoTextTableRowInstance), m_pCore(pCore) {};
        const SwTableLine* const m_pCore;
        mutable rtl::Reference<SwXTextTableRow> m_pResult;
    };
    SwFrameFormat* lcl_EnsureCoreConnected(SwFrameFormat* pFormat, cppu::OWeakObject* pObject)
    {
        if(!pFormat)
            throw uno::RuntimeException(u"Lost connection to core objects"_ustr, pObject);
        return pFormat;
    }
    SwTable* lcl_EnsureTableNotComplex(SwTable* pTable, cppu::OWeakObject* pObject)
    {
        if(pTable->IsTableComplex())
            throw uno::RuntimeException(u"Table too complex"_ustr, pObject);
        return pTable;
    }

    chart::ChartDataChangeEvent createChartEvent(uno::Reference<uno::XInterface> const& xSource)
    {
        //TODO: find appropriate settings of the Event
        chart::ChartDataChangeEvent event;
        event.Source = xSource;
        event.Type = chart::ChartDataChangeType_ALL;
        event.StartColumn = 0;
        event.EndColumn = 1;
        event.StartRow = 0;
        event.EndRow = 1;
        return event;
    }

    void lcl_SendChartEvent(std::unique_lock<std::mutex>& rGuard,
            uno::Reference<uno::XInterface> const& xSource,
            const ::comphelper::OInterfaceContainerHelper4<chart::XChartDataChangeEventListener> & rListeners)
    {
        if (rListeners.getLength(rGuard))
            rListeners.notifyEach(rGuard,
                    &chart::XChartDataChangeEventListener::chartDataChanged,
                    createChartEvent(xSource));
    }

    void lcl_SendChartEvent(std::mutex& rMutex,
            uno::Reference<uno::XInterface> const& xSource,
            const ::comphelper::OInterfaceContainerHelper4<chart::XChartDataChangeEventListener> & rListeners)
    {
        std::unique_lock aGuard(rMutex);
        lcl_SendChartEvent(aGuard, xSource, rListeners);
    }
}

static bool lcl_LineToSvxLine(const table::BorderLine& rLine, SvxBorderLine& rSvxLine)
{
    rSvxLine.SetColor(Color(ColorTransparency, rLine.Color));

    rSvxLine.GuessLinesWidths( SvxBorderLineStyle::NONE,
                                o3tl::toTwips(rLine.OuterLineWidth, o3tl::Length::mm100),
                                o3tl::toTwips(rLine.InnerLineWidth, o3tl::Length::mm100),
                                o3tl::toTwips(rLine.LineDistance, o3tl::Length::mm100) );

    return rLine.InnerLineWidth > 0 || rLine.OuterLineWidth > 0;
}

/// @throws lang::IllegalArgumentException
/// @throws uno::RuntimeException
static void lcl_SetSpecialProperty(SwFrameFormat* pFormat,
                                   const SfxItemPropertyMapEntry* pEntry,
                                   const uno::Any& aValue)
{
    // special treatment for "non-items"
    switch(pEntry->nWID)
    {
        case  FN_TABLE_HEADLINE_REPEAT:
        case  FN_TABLE_HEADLINE_COUNT:
        {
            SwTable* pTable = SwTable::FindTable( pFormat );
            UnoActionContext aAction(&pFormat->GetDoc());
            if( pEntry->nWID == FN_TABLE_HEADLINE_REPEAT)
            {
                pFormat->GetDoc().SetRowsToRepeat( *pTable, aValue.get<bool>() ? 1 : 0 );
            }
            else
            {
                sal_Int32 nRepeat = 0;
                aValue >>= nRepeat;
                if( nRepeat >= 0 && nRepeat < SAL_MAX_UINT16 )
                    pFormat->GetDoc().SetRowsToRepeat( *pTable, o3tl::narrowing<sal_uInt16>(nRepeat) );
            }
        }
        break;

        case  FN_TABLE_IS_RELATIVE_WIDTH:
        case  FN_TABLE_WIDTH:
        case  FN_TABLE_RELATIVE_WIDTH:
        {
            SwFormatFrameSize aSz( pFormat->GetFrameSize() );
            if(FN_TABLE_WIDTH == pEntry->nWID)
            {
                sal_Int32 nWidth = 0;
                aValue >>= nWidth;
                aSz.SetWidthPercent(0);
                aSz.SetWidth ( o3tl::toTwips(nWidth, o3tl::Length::mm100) );
            }
            else if(FN_TABLE_RELATIVE_WIDTH == pEntry->nWID)
            {
                sal_Int16 nSet = 0;
                aValue >>= nSet;
                if(nSet && nSet <=100)
                    aSz.SetWidthPercent( static_cast<sal_uInt8>(nSet) );
            }
            else if(FN_TABLE_IS_RELATIVE_WIDTH == pEntry->nWID)
            {
                if(!aValue.get<bool>())
                    aSz.SetWidthPercent(0);
                else if (aSz.GetWidthPercent() == 0) // If it's already enabled, just ignore it
                {
                    throw lang::IllegalArgumentException(u"relative width cannot be switched on with this property"_ustr, nullptr, 0);
                }
            }
            pFormat->GetDoc().SetAttr(aSz, *pFormat);
        }
        break;

        case RES_PAGEDESC:
        {
            OUString sPageStyle;
            aValue >>= sPageStyle;
            const SwPageDesc* pDesc = nullptr;
            if (!sPageStyle.isEmpty())
            {
                UIName sPageStyleUIName;
                SwStyleNameMapper::FillUIName(ProgName(sPageStyle), sPageStyleUIName, SwGetPoolIdFromName::PageDesc);
                pDesc = SwPageDesc::GetByName(pFormat->GetDoc(), sPageStyleUIName);
            }
            SwFormatPageDesc aDesc( pDesc );
            pFormat->GetDoc().SetAttr(aDesc, *pFormat);
        }
        break;

        default:
            throw lang::IllegalArgumentException();
    }
}

static uno::Any lcl_GetSpecialProperty(SwFrameFormat* pFormat, const SfxItemPropertyMapEntry* pEntry )
{
    switch(pEntry->nWID)
    {
        case  FN_TABLE_HEADLINE_REPEAT:
        case  FN_TABLE_HEADLINE_COUNT:
        {
            SwTable* pTable = SwTable::FindTable( pFormat );
            const sal_uInt16 nRepeat = pTable->GetRowsToRepeat();
            if(pEntry->nWID == FN_TABLE_HEADLINE_REPEAT)
                return uno::Any(nRepeat > 0);
            return uno::Any(sal_Int32(nRepeat));
        }

        case  FN_TABLE_WIDTH:
        case  FN_TABLE_IS_RELATIVE_WIDTH:
        case  FN_TABLE_RELATIVE_WIDTH:
        {
            uno::Any aRet;
            const SwFormatFrameSize& rSz = pFormat->GetFrameSize();
            if(FN_TABLE_WIDTH == pEntry->nWID)
                rSz.QueryValue(aRet, MID_FRMSIZE_WIDTH|CONVERT_TWIPS);
            else if(FN_TABLE_RELATIVE_WIDTH == pEntry->nWID)
                rSz.QueryValue(aRet, MID_FRMSIZE_REL_WIDTH);
            else
                aRet <<= (0 != rSz.GetWidthPercent());
            return aRet;
        }

        case RES_PAGEDESC:
        {
            const SfxItemSet& rSet = pFormat->GetAttrSet();
            if(const SwFormatPageDesc* pItem = rSet.GetItemIfSet(RES_PAGEDESC, false))
            {
                const SwPageDesc* pDsc = pItem->GetPageDesc();
                if(pDsc)
                    return uno::Any(SwStyleNameMapper::GetProgName(pDsc->GetName(), SwGetPoolIdFromName::PageDesc ).toString());
            }
            return uno::Any(OUString());
        }

        case RES_ANCHOR:
            return uno::Any(text::TextContentAnchorType_AT_PARAGRAPH);

        case FN_UNO_ANCHOR_TYPES:
        {
            uno::Sequence<text::TextContentAnchorType> aTypes{text::TextContentAnchorType_AT_PARAGRAPH};
            return uno::Any(aTypes);
        }

        case FN_UNO_WRAP :
            return uno::Any(text::WrapTextMode_NONE);

        case FN_PARAM_LINK_DISPLAY_NAME :
            return uno::Any(pFormat->GetName().toString());

        case FN_UNO_REDLINE_NODE_START:
        case FN_UNO_REDLINE_NODE_END:
        {
            SwTable* pTable = SwTable::FindTable( pFormat );
            SwNode* pTableNode = pTable->GetTableNode();
            if(FN_UNO_REDLINE_NODE_END == pEntry->nWID)
                pTableNode = pTableNode->EndOfSectionNode();
            for(const SwRangeRedline* pRedline : pFormat->GetDoc().getIDocumentRedlineAccess().GetRedlineTable())
            {
                const SwNode& rRedPointNode = pRedline->GetPointNode();
                const SwNode& rRedMarkNode = pRedline->GetMarkNode();
                if(rRedPointNode == *pTableNode || rRedMarkNode == *pTableNode)
                {
                    const SwNode& rStartOfRedline = SwNodeIndex(rRedPointNode) <= SwNodeIndex(rRedMarkNode) ?
                        rRedPointNode : rRedMarkNode;
                    bool bIsStart = &rStartOfRedline == pTableNode;
                    return uno::Any(SwXRedlinePortion::CreateRedlineProperties(*pRedline, bIsStart));
                }
            }
        }
    }
    return uno::Any();
}

/** get position of a cell with a given name
 *
 * If everything was OK, the indices for column and row are changed (both >= 0).
 * In case of errors, at least one of them is < 0.
 *
 * Also since the implementations of tables does not really have columns using
 * this function is appropriate only for tables that are not complex (i.e.
 * where IsTableComplex() returns false).
 *
 * @param rCellName e.g. A1..Z1, a1..z1, AA1..AZ1, Aa1..Az1, BA1..BZ1, Ba1..Bz1, ...
 * @param [IN,OUT] o_rColumn (0-based)
 * @param [IN,OUT] o_rRow (0-based)
 */
//TODO: potential for throwing proper exceptions instead of having every caller to check for errors
void SwXTextTable::GetCellPosition(std::u16string_view aCellName, sal_Int32& o_rColumn, sal_Int32& o_rRow)
{
    o_rColumn = o_rRow = -1;    // default return values indicating failure
    const sal_Int32 nLen = aCellName.size();
    if(!nLen)
    {
        SAL_WARN("sw.uno", "failed to get column or row index");
        return;
    }
    sal_Int32 nRowPos = 0;
    while (nRowPos<nLen)
    {
        if (aCellName[nRowPos]>='0' && aCellName[nRowPos]<='9')
        {
            break;
        }
        ++nRowPos;
    }
    if (nRowPos<=0 || nRowPos>=nLen)
        return;

    sal_Int32 nColIdx = 0;
    for (sal_Int32 i = 0;  i < nRowPos;  ++i)
    {
        nColIdx *= 52;
        if (i < nRowPos - 1)
            ++nColIdx;
        const sal_Unicode cChar = aCellName[i];
        if ('A' <= cChar && cChar <= 'Z')
            nColIdx += cChar - 'A';
        else if ('a' <= cChar && cChar <= 'z')
            nColIdx += 26 + cChar - 'a';
        else
        {
            nColIdx = -1;   // sth failed
            break;
        }
    }

    o_rColumn = nColIdx;
    o_rRow    = o3tl::toInt32(aCellName.substr(nRowPos)) - 1; // - 1 because indices ought to be 0 based
}

/** compare position of two cells (check rows first)
 *
 * @note this function probably also make sense only
 *       for cell names of non-complex tables
 *
 * @param rCellName1 e.g. "A1" (non-empty string with valid cell name)
 * @param rCellName2 e.g. "A1" (non-empty string with valid cell name)
 * @return -1 if cell_1 < cell_2; 0 if both cells are equal; +1 if cell_1 > cell_2
 */
int sw_CompareCellsByRowFirst( std::u16string_view aCellName1, std::u16string_view aCellName2 )
{
    sal_Int32 nCol1 = -1, nRow1 = -1, nCol2 = -1, nRow2 = -1;
    SwXTextTable::GetCellPosition( aCellName1, nCol1, nRow1 );
    SwXTextTable::GetCellPosition( aCellName2, nCol2, nRow2 );

    if (nRow1 < nRow2 || (nRow1 == nRow2 && nCol1 < nCol2))
        return -1;
    else if (nCol1 == nCol2 && nRow1 == nRow2)
        return 0;
    else
        return +1;
}

/** compare position of two cells (check columns first)
 *
 * @note this function probably also make sense only
 *       for cell names of non-complex tables
 *
 * @param rCellName1 e.g. "A1" (non-empty string with valid cell name)
 * @param rCellName2 e.g. "A1" (non-empty string with valid cell name)
 * @return -1 if cell_1 < cell_2; 0 if both cells are equal; +1 if cell_1 > cell_2
 */
int sw_CompareCellsByColFirst( std::u16string_view aCellName1, std::u16string_view aCellName2 )
{
    sal_Int32 nCol1 = -1, nRow1 = -1, nCol2 = -1, nRow2 = -1;
    SwXTextTable::GetCellPosition( aCellName1, nCol1, nRow1 );
    SwXTextTable::GetCellPosition( aCellName2, nCol2, nRow2 );

    if (nCol1 < nCol2 || (nCol1 == nCol2 && nRow1 < nRow2))
        return -1;
    else if (nRow1 == nRow2 && nCol1 == nCol2)
        return 0;
    else
        return +1;
}

/** compare position of two cell ranges
 *
 * @note this function probably also make sense only
 *       for cell names of non-complex tables
 *
 * @param rRange1StartCell e.g. "A1" (non-empty string with valid cell name)
 * @param rRange1EndCell   e.g. "A1" (non-empty string with valid cell name)
 * @param rRange2StartCell e.g. "A1" (non-empty string with valid cell name)
 * @param rRange2EndCell   e.g. "A1" (non-empty string with valid cell name)
 * @param bCmpColsFirst    if <true> position in columns will be compared first before rows
 *
 * @return -1 if cell_range_1 < cell_range_2; 0 if both cell ranges are equal; +1 if cell_range_1 > cell_range_2
 */
int sw_CompareCellRanges(
        std::u16string_view aRange1StartCell, std::u16string_view aRange1EndCell,
        std::u16string_view aRange2StartCell, std::u16string_view aRange2EndCell,
        bool bCmpColsFirst )
{
    int (*pCompareCells)( std::u16string_view, std::u16string_view ) =
            bCmpColsFirst ? &sw_CompareCellsByColFirst : &sw_CompareCellsByRowFirst;

    int nCmpResStartCells = pCompareCells( aRange1StartCell, aRange2StartCell );
    if ((-1 == nCmpResStartCells ) ||
         ( 0 == nCmpResStartCells &&
          -1 == pCompareCells( aRange1EndCell, aRange2EndCell ) ))
        return -1;
    else if (0 == nCmpResStartCells &&
             0 == pCompareCells( aRange1EndCell, aRange2EndCell ))
        return 0;
    else
        return +1;
}

/** get cell name at a specified coordinate
 *
 * @param nColumn column index (0-based)
 * @param nRow row index (0-based)
 * @return the cell name
 */
OUString sw_GetCellName( sal_Int32 nColumn, sal_Int32 nRow )
{
    if (nColumn < 0 || nRow < 0)
        return OUString();
    OUString sCellName;
    sw_GetTableBoxColStr( static_cast< sal_uInt16 >(nColumn), sCellName );
    return sCellName + OUString::number( nRow + 1 );
}

/** Find the top left or bottom right corner box in given table.
  Consider nested lines when finding the box.

  @param rTableLines the table
  @param i_bTopLeft if true, find top left box, otherwise find bottom
         right box
 */
static const SwTableBox* lcl_FindCornerTableBox(const SwTableLines& rTableLines, const bool i_bTopLeft)
{
    const SwTableLines* pLines(&rTableLines);
    while(true)
    {
        assert(!pLines->empty());
        if(pLines->empty())
            return nullptr;
        const SwTableLine* pLine(i_bTopLeft ? pLines->front() : pLines->back());
        assert(pLine);
        const SwTableBoxes& rBoxes(pLine->GetTabBoxes());
        assert(!rBoxes.empty());
        const SwTableBox* pBox = i_bTopLeft ? rBoxes.front() : rBoxes.back();
        assert(pBox);
        if (pBox->GetSttNd())
            return pBox;
        pLines = &pBox->GetTabLines();
    }
}

/** cleanup order in a range
 *
 * Sorts the input to a uniform format. I.e. for the four possible representation
 *      A1:C5, C5:A1, A5:C1, C1:A5
 * the result will be always A1:C5.
 *
 * @param [IN,OUT] rCell1 cell name (will be modified to upper-left corner), e.g. "A1" (non-empty string with valid cell name)
 * @param [IN,OUT] rCell2 cell name (will be modified to lower-right corner), e.g. "A1" (non-empty string with valid cell name)
 */
void sw_NormalizeRange(OUString &rCell1, OUString &rCell2)
{
    sal_Int32 nCol1 = -1, nRow1 = -1, nCol2 = -1, nRow2 = -1;
    SwXTextTable::GetCellPosition( rCell1, nCol1, nRow1 );
    SwXTextTable::GetCellPosition( rCell2, nCol2, nRow2 );
    if (nCol2 < nCol1 || nRow2 < nRow1)
    {
        rCell1  = sw_GetCellName( std::min(nCol1, nCol2), std::min(nRow1, nRow2) );
        rCell2  = sw_GetCellName( std::max(nCol1, nCol2), std::max(nRow1, nRow2) );
    }
}

void SwRangeDescriptor::Normalize()
{
    if (nTop > nBottom)
        std::swap(nBottom, nTop);
    if (nLeft > nRight)
        std::swap(nLeft, nRight);
}

static rtl::Reference<SwXCell> lcl_CreateXCell(SwFrameFormat* pFormat, sal_Int32 nColumn, sal_Int32 nRow)
{
    const OUString sCellName = sw_GetCellName(nColumn, nRow);
    SwTable* pTable = SwTable::FindTable(pFormat);
    SwTableBox* pBox = const_cast<SwTableBox*>(pTable->GetTableBox(sCellName));
    if(!pBox)
        return nullptr;
    return SwXCell::CreateXCell(pFormat, pBox, pTable);
}

static void lcl_InspectLines(SwTableLines& rLines, std::vector<OUString>& rAllNames)
{
    for(auto pLine : rLines)
    {
        for(auto pBox : pLine->GetTabBoxes())
        {
            if(!pBox->GetName().isEmpty() && pBox->getRowSpan() > 0)
                rAllNames.push_back(pBox->GetName());
            SwTableLines& rBoxLines = pBox->GetTabLines();
            if(!rBoxLines.empty())
                lcl_InspectLines(rBoxLines, rAllNames);
        }
    }
}

static bool lcl_FormatTable(SwFrameFormat const * pTableFormat)
{
    bool bHasFrames = false;
    SwIterator<SwFrame,SwFormat> aIter( *pTableFormat );
    for(SwFrame* pFrame = aIter.First(); pFrame; pFrame = aIter.Next())
    {
        vcl::RenderContext* pRenderContext = pFrame->getRootFrame()->GetCurrShell()->GetOut();
        // mba: no TYPEINFO for SwTabFrame
        if(!pFrame->IsTabFrame())
            continue;
        DisableCallbackAction a(*pFrame->getRootFrame());
        SwTabFrame* pTabFrame = static_cast<SwTabFrame*>(pFrame);
        if(pTabFrame->isFrameAreaDefinitionValid())
            pTabFrame->InvalidatePos();
        pTabFrame->SetONECalcLowers();
        pTabFrame->Calc(pRenderContext);
        bHasFrames = true;
    }
    return bHasFrames;
}

static void lcl_CursorSelect(SwPaM& rCursor, bool bExpand)
{
    if(bExpand)
    {
        if(!rCursor.HasMark())
            rCursor.SetMark();
    }
    else if(rCursor.HasMark())
        rCursor.DeleteMark();
}

static void lcl_GetTableSeparators(uno::Any& rRet, SwTable const * pTable, SwTableBox const * pBox, bool bRow)
{
    SwTabCols aCols;
    aCols.SetLeftMin ( 0 );
    aCols.SetLeft    ( 0 );
    aCols.SetRight   ( UNO_TABLE_COLUMN_SUM );
    aCols.SetRightMax( UNO_TABLE_COLUMN_SUM );

    pTable->GetTabCols( aCols, pBox, false, bRow );

    const size_t nSepCount = aCols.Count();
    uno::Sequence< text::TableColumnSeparator> aColSeq(nSepCount);
    text::TableColumnSeparator* pArray = aColSeq.getArray();
    bool bError = false;
    for(size_t i = 0; i < nSepCount; ++i)
    {
        pArray[i].Position = static_cast< sal_Int16 >(aCols[i]);
        pArray[i].IsVisible = !aCols.IsHidden(i);
        if(!bRow && !pArray[i].IsVisible)
        {
            bError = true;
            break;
        }
    }
    if(!bError)
        rRet <<= aColSeq;

}

static void lcl_SetTableSeparators(const uno::Any& rVal, SwTable* pTable, SwTableBox const * pBox, bool bRow, SwDoc& rDoc)
{
    SwTabCols aOldCols;

    aOldCols.SetLeftMin ( 0 );
    aOldCols.SetLeft    ( 0 );
    aOldCols.SetRight   ( UNO_TABLE_COLUMN_SUM );
    aOldCols.SetRightMax( UNO_TABLE_COLUMN_SUM );

    pTable->GetTabCols( aOldCols, pBox, false, bRow );
    const size_t nOldCount = aOldCols.Count();
    // there is no use in setting tab cols if there is only one column
    if( !nOldCount )
        return;

    auto pSepSeq =
                o3tl::tryAccess<uno::Sequence<text::TableColumnSeparator>>(rVal);
    if(!pSepSeq || static_cast<size_t>(pSepSeq->getLength()) != nOldCount)
        return;
    SwTabCols aCols(aOldCols);
    const text::TableColumnSeparator* pArray = pSepSeq->getConstArray();
    tools::Long nLastValue = 0;
    //sal_Int32 nTableWidth = aCols.GetRight() - aCols.GetLeft();
    for(size_t i = 0; i < nOldCount; ++i)
    {
        aCols[i] = pArray[i].Position;
        if(bool(pArray[i].IsVisible) == aCols.IsHidden(i) ||
                (!bRow && aCols.IsHidden(i)) ||
                aCols[i] < nLastValue ||
                UNO_TABLE_COLUMN_SUM < aCols[i] )
            return; // probably this should assert()
        nLastValue = aCols[i];
    }
    rDoc.SetTabCols(*pTable, aCols, aOldCols, pBox, bRow );
}

/* non UNO function call to set string in SwXCell */
void sw_setString( SwXCell &rCell, const OUString &rText,
        bool bKeepNumberFormat = false )
{
    if(rCell.IsValid())
    {
        SwFrameFormat* pBoxFormat = rCell.m_pBox->ClaimFrameFormat();
        pBoxFormat->LockModify();
        pBoxFormat->ResetFormatAttr( RES_BOXATR_FORMULA );
        pBoxFormat->ResetFormatAttr( RES_BOXATR_VALUE );
        if (!bKeepNumberFormat)
            pBoxFormat->SetFormatAttr( SwTableBoxNumFormat(/*default Text*/) );
        pBoxFormat->UnlockModify();
    }
    rCell.SwXText::setString(rText);
}


/* non UNO function call to set value in SwXCell */
void sw_setValue( SwXCell &rCell, double nVal )
{
    if(!rCell.IsValid())
        return;
    // first this text (maybe) needs to be deleted
    SwNodeOffset nNdPos = rCell.m_pBox->IsValidNumTextNd();
    if(NODE_OFFSET_MAX != nNdPos)
        sw_setString( rCell, OUString(), true );   // true == keep number format
    SwDoc* pDoc = rCell.GetDoc();
    UnoActionContext aAction(pDoc);
    SwFrameFormat* pBoxFormat = rCell.m_pBox->ClaimFrameFormat();
    SfxItemSetFixed<RES_BOXATR_FORMAT, RES_BOXATR_VALUE> aSet(pDoc->GetAttrPool());

    //!! do we need to set a new number format? Yes, if
    // - there is no current number format
    // - the current number format is not a number format according to the number formatter, but rather a text format
    const SwTableBoxNumFormat* pNumFormat = pBoxFormat->GetAttrSet().GetItemIfSet(RES_BOXATR_FORMAT);
    if(!pNumFormat
        ||  pDoc->GetNumberFormatter()->IsTextFormat(pNumFormat->GetValue()))
    {
        aSet.Put(SwTableBoxNumFormat(0));
    }

    SwTableBoxValue aVal(nVal);
    aSet.Put(aVal);
    pDoc->SetTableBoxFormulaAttrs( *rCell.m_pBox, aSet );
    // update table
    pDoc->getIDocumentFieldsAccess().UpdateTableFields(SwTable::FindTable(rCell.GetFrameFormat()));
}


SwXCell::SwXCell(SwFrameFormat* pTableFormat, SwTableBox* pBx, size_t const nPos) :
    SwXText(&pTableFormat->GetDoc(), CursorType::TableText),
    m_pPropSet(aSwMapProvider.GetPropertySet(PROPERTY_MAP_TABLE_CELL)),
    m_pBox(pBx),
    m_pStartNode(nullptr),
    m_pTableFormat(pTableFormat),
    m_nFndPos(nPos)
{
    StartListening(pTableFormat->GetNotifier());
}

SwXCell::SwXCell(SwFrameFormat* pTableFormat, const SwStartNode& rStartNode) :
    SwXText(&pTableFormat->GetDoc(), CursorType::TableText),
    m_pPropSet(aSwMapProvider.GetPropertySet(PROPERTY_MAP_TABLE_CELL)),
    m_pBox(nullptr),
    m_pStartNode(&rStartNode),
    m_pTableFormat(pTableFormat),
    m_nFndPos(NOTFOUND)
{
    StartListening(pTableFormat->GetNotifier());
}

SwXCell::~SwXCell()
{
    SolarMutexGuard aGuard;
    EndListeningAll();
}

uno::Sequence< uno::Type > SAL_CALL SwXCell::getTypes(  )
{
    return comphelper::concatSequences(
            SwXCellBaseClass::getTypes(),
            SwXText::getTypes()
        );
}

uno::Sequence< sal_Int8 > SAL_CALL SwXCell::getImplementationId(  )
{
    return css::uno::Sequence<sal_Int8>();
}

void SAL_CALL SwXCell::acquire(  ) noexcept
{
    SwXCellBaseClass::acquire();
}

void SAL_CALL SwXCell::release(  ) noexcept
{
    SolarMutexGuard aGuard;

    SwXCellBaseClass::release();
}

uno::Any SAL_CALL SwXCell::queryInterface( const uno::Type& aType )
{
    uno::Any aRet = SwXText::queryInterface(aType);
    if(aRet.getValueType() == cppu::UnoType<void>::get())
        aRet = SwXCellBaseClass::queryInterface(aType);
    return aRet;
}

const SwStartNode *SwXCell::GetStartNode() const
{
    const SwStartNode* pSttNd = nullptr;

    if( m_pStartNode || IsValid() )
        pSttNd = m_pStartNode ? m_pStartNode : m_pBox->GetSttNd();

    return pSttNd;
}

bool SwXCell::IsValid() const
{
    // FIXME: this is now a const method, to make SwXText::IsValid invisible
    // but the const_cast here are still ridiculous. TODO: find a better way.
    SwFrameFormat* pTableFormat = m_pBox ? GetFrameFormat() : nullptr;
    if(!pTableFormat)
    {
        const_cast<SwXCell*>(this)->m_pBox = nullptr;
    }
    else
    {
        SwTable* pTable = SwTable::FindTable( pTableFormat );
        SwTableBox const*const pFoundBox =
            const_cast<SwXCell*>(this)->FindBox(pTable, m_pBox);
        if (!pFoundBox)
        {
            const_cast<SwXCell*>(this)->m_pBox = nullptr;
        }
    }
    return nullptr != m_pBox;
}

OUString SwXCell::getFormula()
{
    SolarMutexGuard aGuard;
    if(!IsValid())
        return OUString();
    SwTableBoxFormula aFormula( m_pBox->GetFrameFormat()->GetTableBoxFormula() );
    SwTable* pTable = SwTable::FindTable( GetFrameFormat() );
    aFormula.PtrToBoxNm( pTable );
    return aFormula.GetFormula();
}

///@see sw_setValue (TODO: seems to be copy and paste programming here)
void SwXCell::setFormula(const OUString& rFormula)
{
    SolarMutexGuard aGuard;
    if(!IsValid())
        return;
    // first this text (maybe) needs to be deleted
    SwNodeOffset nNdPos = m_pBox->IsValidNumTextNd();
    if(SwNodeOffset(USHRT_MAX) == nNdPos)
        sw_setString( *this, OUString(), true );
    OUString sFormula(comphelper::string::stripStart(rFormula, ' '));
    if( !sFormula.isEmpty() && '=' == sFormula[0] )
                sFormula = sFormula.copy( 1 );
    SwTableBoxFormula aFormula( sFormula );
    SwDoc* pMyDoc = GetDoc();
    UnoActionContext aAction(pMyDoc);
    SfxItemSetFixed<RES_BOXATR_FORMAT, RES_BOXATR_FORMULA> aSet(pMyDoc->GetAttrPool());
    SwFrameFormat* pBoxFormat = m_pBox->GetFrameFormat();
    const SwTableBoxNumFormat* pNumFormat =
        pBoxFormat->GetAttrSet().GetItemIfSet(RES_BOXATR_FORMAT);
    if(!pNumFormat
        ||  pMyDoc->GetNumberFormatter()->IsTextFormat(pNumFormat->GetValue()))
    {
        aSet.Put(SwTableBoxNumFormat(0));
    }
    aSet.Put(aFormula);
    GetDoc()->SetTableBoxFormulaAttrs( *m_pBox, aSet );
    // update table
    pMyDoc->getIDocumentFieldsAccess().UpdateTableFields(SwTable::FindTable(GetFrameFormat()));
}

double SwXCell::getValue()
{
    SolarMutexGuard aGuard;
    // #i112652# a table cell may contain NaN as a value, do not filter that
    if(IsValid() && !getString().isEmpty())
        return m_pBox->GetFrameFormat()->GetTableBoxValue().GetValue();
    return std::numeric_limits<double>::quiet_NaN();
}

void SwXCell::setValue(double rValue)
{
    SolarMutexGuard aGuard;
    sw_setValue( *this, rValue );
}

table::CellContentType SwXCell::getType()
{
    SolarMutexGuard aGuard;

    table::CellContentType nRes = table::CellContentType_EMPTY;
    sal_uInt32 nNdPos = m_pBox->IsFormulaOrValueBox();
    switch (nNdPos)
    {
        case 0 :                    nRes = table::CellContentType_TEXT; break;
        case USHRT_MAX :            nRes = table::CellContentType_EMPTY; break;
        case RES_BOXATR_VALUE :     nRes = table::CellContentType_VALUE; break;
        case RES_BOXATR_FORMULA :   nRes = table::CellContentType_FORMULA; break;
        default :
            OSL_FAIL( "unexpected case" );
    }
    return  nRes;
}

void SwXCell::setString(const OUString& aString)
{
    SolarMutexGuard aGuard;
    sw_setString( *this, aString );
}

sal_Int32 SwXCell::getError()
{
    SolarMutexGuard aGuard;
    OUString sContent = getString();
    return sal_Int32(sContent == SwViewShell::GetShellRes()->aCalc_Error);
}

rtl::Reference< SwXTextCursor > SwXCell::createXTextCursor()
{
    if(!m_pStartNode && !IsValid())
        throw uno::RuntimeException();
    const SwStartNode* pSttNd = m_pStartNode ? m_pStartNode : m_pBox->GetSttNd();
    SwPosition aPos(*pSttNd);
    rtl::Reference<SwXTextCursor> const pXCursor =
        new SwXTextCursor(*GetDoc(), this, CursorType::TableText, aPos);
    auto& rUnoCursor(pXCursor->GetCursor());
    rUnoCursor.Move(fnMoveForward, GoInNode);
    return pXCursor;
}

rtl::Reference<SwXTextCursor> SwXCell::createXTextCursorByRange(const uno::Reference< text::XTextRange > & xTextPosition)
{
    SwUnoInternalPaM aPam(*GetDoc());
    if(!::sw::XTextRangeToSwPaM(aPam, xTextPosition))
        throw uno::RuntimeException();
    return createXTextCursorByRangeImpl(aPam);
}

rtl::Reference< SwXTextCursor > SwXCell::createXTextCursorByRangeImpl(
        SwUnoInternalPaM& rPam)
{
    if(!m_pStartNode && !IsValid())
        throw uno::RuntimeException();
    const SwStartNode* pSttNd = m_pStartNode ? m_pStartNode : m_pBox->GetSttNd();
    // skip sections
    SwStartNode* p1 = rPam.GetPointNode().StartOfSectionNode();
    while(p1->IsSectionNode())
        p1 = p1->StartOfSectionNode();
    if( p1 != pSttNd )
        return nullptr;
    return new SwXTextCursor(*GetDoc(), this, CursorType::TableText,
                *rPam.GetPoint(), rPam.GetMark());
}

uno::Reference< beans::XPropertySetInfo >  SwXCell::getPropertySetInfo()
{
    static uno::Reference< beans::XPropertySetInfo >  xRef = m_pPropSet->getPropertySetInfo();
    return xRef;
}

void SwXCell::setPropertyValue(const OUString& rPropertyName, const uno::Any& aValue)
{
    SolarMutexGuard aGuard;
    if(!IsValid())
        return;
    // Hack to support hidden property to transfer textDirection
    if(rPropertyName == "FRMDirection")
    {
        SvxFrameDirectionItem aItem(SvxFrameDirection::Environment, RES_FRAMEDIR);
        aItem.PutValue(aValue, 0);
        m_pBox->GetFrameFormat()->SetFormatAttr(aItem);
    }
    else if(rPropertyName == "TableRedlineParams")
    {
        // Get the table row properties
        uno::Sequence<beans::PropertyValue> tableCellProperties = aValue.get< uno::Sequence< beans::PropertyValue > >();
        comphelper::SequenceAsHashMap aPropMap(tableCellProperties);
        OUString sRedlineType;
        if(!(aPropMap.getValue(u"RedlineType"_ustr) >>= sRedlineType))
            throw beans::UnknownPropertyException(u"No redline type property: "_ustr, getXWeak());

        // Create a 'Table Cell Redline' object
        SwUnoCursorHelper::makeTableCellRedline(*m_pBox, sRedlineType, tableCellProperties);


    }
    else if (rPropertyName == "VerticalMerge")
    {
        //Hack to allow clearing of numbering from the paragraphs in the merged cells.
        SwNodeIndex aIdx(*GetStartNode(), 1);
        const SwNode* pEndNd = aIdx.GetNode().EndOfSectionNode();
        while (&aIdx.GetNode() != pEndNd)
        {
            SwTextNode* pNd = aIdx.GetNode().GetTextNode();
            if (pNd)
                pNd->SetCountedInList(false);
            ++aIdx;
        }
    }
    else
    {
        auto pEntry(m_pPropSet->getPropertyMap().getByName(rPropertyName));
        if ( !pEntry )
        {
            // not a table property: ignore it, if it is a paragraph/character property
            const SfxItemPropertySet& rParaPropSet = *aSwMapProvider.GetPropertySet(PROPERTY_MAP_PARAGRAPH);
            pEntry = rParaPropSet.getPropertyMap().getByName(rPropertyName);

            if ( pEntry )
                return;
        }

        if(!pEntry)
            throw beans::UnknownPropertyException(rPropertyName, getXWeak());
        if(pEntry->nWID != FN_UNO_CELL_ROW_SPAN)
        {
            SwFrameFormat* pBoxFormat = m_pBox->ClaimFrameFormat();
            SwAttrSet aSet(pBoxFormat->GetAttrSet());
            m_pPropSet->setPropertyValue(rPropertyName, aValue, aSet);
            pBoxFormat->GetDoc().SetAttr(aSet, *pBoxFormat);
        }
        else if(aValue.isExtractableTo(cppu::UnoType<sal_Int32>::get()))
            m_pBox->setRowSpan(aValue.get<sal_Int32>());
    }
}

uno::Any SwXCell::getPropertyValue(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;
    if(!IsValid())
        return uno::Any();
    auto pEntry(m_pPropSet->getPropertyMap().getByName(rPropertyName));
    if(!pEntry)
        throw beans::UnknownPropertyException(rPropertyName, getXWeak());
    switch(pEntry->nWID)
    {
        case FN_UNO_CELL_ROW_SPAN:
            return uno::Any(m_pBox->getRowSpan());
        case FN_UNO_TEXT_SECTION:
        {
            SwFrameFormat* pTableFormat = GetFrameFormat();
            SwTable* pTable = SwTable::FindTable(pTableFormat);
            SwTableNode* pTableNode = pTable->GetTableNode();
            SwSectionNode* pSectionNode = pTableNode->FindSectionNode();
            if(!pSectionNode)
                return uno::Any();
            SwSection& rSect = pSectionNode->GetSection();
            rtl::Reference<SwXTextSection> xSect = SwXTextSection::CreateXTextSection(rSect.GetFormat());
            return uno::Any(uno::Reference< text::XTextSection >(xSect));
        }
        break;
        case FN_UNO_CELL_NAME:
            return uno::Any(m_pBox->GetName());
        case FN_UNO_REDLINE_NODE_START:
        case FN_UNO_REDLINE_NODE_END:
        {
            //redline can only be returned if it's a living object
            return SwXText::getPropertyValue(rPropertyName);
        }
        break;
        case FN_UNO_PARENT_TEXT:
        {
            if (!m_xParentText.is())
            {
                const SwStartNode* pSttNd = m_pBox->GetSttNd();
                if (!pSttNd)
                    return uno::Any();

                const SwTableNode* pTableNode = pSttNd->FindTableNode();
                if (!pTableNode)
                    return uno::Any();

                SwPosition aPos(*pTableNode);
                SwDoc& rDoc = aPos.GetDoc();
                m_xParentText = sw::CreateParentXText(rDoc, aPos);
            }

            return uno::Any(m_xParentText);
        }
        break;
        default:
        {
            const SwAttrSet& rSet = m_pBox->GetFrameFormat()->GetAttrSet();
            uno::Any aResult;
            m_pPropSet->getPropertyValue(rPropertyName, rSet, aResult);
            return aResult;
        }
    }
}

void SwXCell::addPropertyChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXCell::removePropertyChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXCell::addVetoableChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXCell::removeVetoableChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

uno::Reference<container::XEnumeration> SwXCell::createEnumeration()
{
    return createSwEnumeration();
}

rtl::Reference<SwXParagraphEnumeration> SwXCell::createSwEnumeration()
{
    SolarMutexGuard aGuard;
    if(!IsValid())
        return {};
    const SwStartNode* pSttNd = m_pBox->GetSttNd();
    SwPosition aPos(*pSttNd);
    auto pUnoCursor(GetDoc()->CreateUnoCursor(aPos));
    pUnoCursor->Move(fnMoveForward, GoInNode);
    // remember table and start node for later travelling
    // (used in export of tables in tables)
    return SwXParagraphEnumeration::Create(this, pUnoCursor, CursorType::TableText, m_pBox);
}

uno::Type SAL_CALL SwXCell::getElementType()
{
    return cppu::UnoType<text::XTextRange>::get();
}

sal_Bool SwXCell::hasElements()
{
    return true;
}

void SwXCell::Notify(const SfxHint& rHint)
{
    if(rHint.GetId() == SfxHintId::Dying)
    {
        m_pTableFormat = nullptr;
    }
    else if(rHint.GetId() == SfxHintId::SwFindUnoCellInstance)
    {
        auto pFindHint = static_cast<const FindUnoCellInstanceHint*>(&rHint);
        if(!pFindHint->m_pResult && pFindHint->m_pCore == GetTableBox())
            pFindHint->m_pResult = this;
    }
}

rtl::Reference<SwXCell> SwXCell::CreateXCell(SwFrameFormat* pTableFormat, SwTableBox* pBox, SwTable *pTable )
{
    if(!pTableFormat || !pBox)
        return nullptr;
    if(!pTable)
        pTable = SwTable::FindTable(pTableFormat);
    SwTableSortBoxes::const_iterator it = pTable->GetTabSortBoxes().find(pBox);
    if(it == pTable->GetTabSortBoxes().end())
        return nullptr;
    size_t const nPos = it - pTable->GetTabSortBoxes().begin();
    FindUnoCellInstanceHint aHint{pBox};
    pTableFormat->GetNotifier().Broadcast(aHint);
    return aHint.m_pResult ? aHint.m_pResult.get() : new SwXCell(pTableFormat, pBox, nPos);
}

/** search if a box exists in a table
 *
 * @param pTable the table to search in
 * @param pBox2 box model to find
 * @return the box if existent in pTable, 0 (!!!) if not found
 */
SwTableBox* SwXCell::FindBox(SwTable* pTable, SwTableBox* pBox2)
{
    // check if nFndPos happens to point to the right table box
    if( m_nFndPos < pTable->GetTabSortBoxes().size() &&
        pBox2 == pTable->GetTabSortBoxes()[ m_nFndPos ] )
        return pBox2;

    // if not, seek the entry (and return, if successful)
    SwTableSortBoxes::const_iterator it = pTable->GetTabSortBoxes().find( pBox2 );
    if( it != pTable->GetTabSortBoxes().end() )
    {
        m_nFndPos = it - pTable->GetTabSortBoxes().begin();
        return pBox2;
    }

    // box not found: reset nFndPos pointer
    m_nFndPos = NOTFOUND;
    return nullptr;
}

double SwXCell::GetForcedNumericalValue() const
{
    if(table::CellContentType_TEXT != const_cast<SwXCell*>(this)->getType())
        return getValue();
    // now we'll try to get a useful numerical value
    // from the text in the cell...
    sal_uInt32 nFIndex;
    SvNumberFormatter* pNumFormatter(const_cast<SvNumberFormatter*>(GetDoc()->GetNumberFormatter()));
    // look for SwTableBoxNumFormat value in parents as well
    auto pBoxFormat(GetTableBox()->GetFrameFormat());
    const SwTableBoxNumFormat* pNumFormat = pBoxFormat->GetAttrSet().GetItemIfSet(RES_BOXATR_FORMAT);

    if (pNumFormat)
    {
        // please note that the language of the numberformat
        // is implicitly coded into the below value as well
        nFIndex = pNumFormat->GetValue();

        // since the current value indicates a text format but the call
        // to 'IsNumberFormat' below won't work for text formats
        // we need to get rid of the part that indicates the text format.
        // According to ER this can be done like this:
        nFIndex -= (nFIndex % SV_COUNTRY_LANGUAGE_OFFSET);
    }
    else
    {
        // system language is probably not the best possible choice
        // but since we have to guess anyway (because the language of at
        // the text is NOT the one used for the number format!)
        // it is at least conform to what is used in
        // SwTableShell::Execute when
        // SID_ATTR_NUMBERFORMAT_VALUE is set...
        LanguageType eLang = LANGUAGE_SYSTEM;
        nFIndex = pNumFormatter->GetStandardIndex( eLang );
    }
    double fTmp;
    if (!const_cast<SwDoc*>(GetDoc())->IsNumberFormat(const_cast<SwXCell*>(this)->getString(), nFIndex, fTmp))
        return std::numeric_limits<double>::quiet_NaN();
    return fTmp;
}

uno::Any SwXCell::GetAny() const
{
    if(!m_pBox)
        throw uno::RuntimeException();
    // check if table box value item is set
    auto pBoxFormat(m_pBox->GetFrameFormat());
    const bool bIsNum = pBoxFormat->GetItemState(RES_BOXATR_VALUE, false) == SfxItemState::SET;
    return bIsNum ? uno::Any(getValue()) : uno::Any(const_cast<SwXCell*>(this)->getString());
}

OUString SwXCell::getImplementationName()
    { return u"SwXCell"_ustr; }

sal_Bool SwXCell::supportsService(const OUString& rServiceName)
    { return cppu::supportsService(this, rServiceName); }

uno::Sequence< OUString > SwXCell::getSupportedServiceNames()
    { return {u"com.sun.star.text.CellProperties"_ustr}; }

OUString SwXTextTableRow::getImplementationName()
    { return u"SwXTextTableRow"_ustr; }

sal_Bool SwXTextTableRow::supportsService(const OUString& rServiceName)
    { return cppu::supportsService(this, rServiceName); }

uno::Sequence< OUString > SwXTextTableRow::getSupportedServiceNames()
    { return {u"com.sun.star.text.TextTableRow"_ustr}; }


SwXTextTableRow::SwXTextTableRow(SwFrameFormat* pFormat, SwTableLine* pLn) :
    m_pFormat(pFormat),
    m_pLine(pLn),
    m_pPropSet(aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_TABLE_ROW))
{
    StartListening(m_pFormat->GetNotifier());
}

SwXTextTableRow::~SwXTextTableRow()
{
    SolarMutexGuard aGuard;
    EndListeningAll();
}

uno::Reference< beans::XPropertySetInfo > SwXTextTableRow::getPropertySetInfo()
{
    static uno::Reference<beans::XPropertySetInfo> xRef = m_pPropSet->getPropertySetInfo();
    return xRef;
}

void SwXTextTableRow::setPropertyValue(const OUString& rPropertyName, const uno::Any& aValue)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = lcl_EnsureCoreConnected(GetFrameFormat(), this);
    SwTable* pTable = SwTable::FindTable( pFormat );
    SwTableLine* pLn = SwXTextTableRow::FindLine(pTable, m_pLine);
    if(!pLn)
        return;

    // Check for a specific property
    if  ( rPropertyName == "TableRedlineParams" )
    {
        // Get the table row properties
        uno::Sequence< beans::PropertyValue > tableRowProperties = aValue.get< uno::Sequence< beans::PropertyValue > >();
        comphelper::SequenceAsHashMap aPropMap( tableRowProperties );
        OUString sRedlineType;
        if( !(aPropMap.getValue(u"RedlineType"_ustr) >>= sRedlineType) )
        {
            throw beans::UnknownPropertyException(u"No redline type property: "_ustr, getXWeak() );
        }

        // Create a 'Table Row Redline' object
        SwUnoCursorHelper::makeTableRowRedline( *pLn, sRedlineType, tableRowProperties);

    }
    else
    {
        const SfxItemPropertyMapEntry* pEntry =
            m_pPropSet->getPropertyMap().getByName(rPropertyName);
        SwDoc& rDoc = pFormat->GetDoc();
        if (!pEntry)
            throw beans::UnknownPropertyException("Unknown property: " + rPropertyName, getXWeak() );
        if ( pEntry->nFlags & beans::PropertyAttribute::READONLY)
            throw beans::PropertyVetoException("Property is read-only: " + rPropertyName, getXWeak() );

        switch(pEntry->nWID)
        {
            case FN_UNO_ROW_HEIGHT:
            case FN_UNO_ROW_AUTO_HEIGHT:
            {
                SwFormatFrameSize aFrameSize(pLn->GetFrameFormat()->GetFrameSize());
                if(FN_UNO_ROW_AUTO_HEIGHT== pEntry->nWID)
                {
                    bool bSet = *o3tl::doAccess<bool>(aValue);
                    aFrameSize.SetHeightSizeType(bSet ? SwFrameSize::Variable : SwFrameSize::Fixed);
                }
                else
                {
                    sal_Int32 nHeight = 0;
                    aValue >>= nHeight;
                    Size aSz(aFrameSize.GetSize());
                    aSz.setHeight( o3tl::toTwips(nHeight, o3tl::Length::mm100) );
                    aFrameSize.SetSize(aSz);
                }
                rDoc.SetAttr(aFrameSize, *pLn->ClaimFrameFormat());
            }
            break;

            case FN_UNO_TABLE_COLUMN_SEPARATORS:
            {
                UnoActionContext aContext(&rDoc);
                SwTable* pTable2 = SwTable::FindTable( pFormat );
                lcl_SetTableSeparators(aValue, pTable2, m_pLine->GetTabBoxes()[0], true, rDoc);
            }
            break;

            default:
            {
                SwFrameFormat* pLnFormat = pLn->ClaimFrameFormat();
                SwAttrSet aSet(pLnFormat->GetAttrSet());
                SfxItemPropertySet::setPropertyValue(*pEntry, aValue, aSet);
                rDoc.SetAttr(aSet, *pLnFormat);
            }
        }
    }
}

uno::Any SwXTextTableRow::getPropertyValue(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;
    uno::Any aRet;
    SwFrameFormat* pFormat = lcl_EnsureCoreConnected(GetFrameFormat(), this);
    SwTable* pTable = SwTable::FindTable( pFormat );
    SwTableLine* pLn = SwXTextTableRow::FindLine(pTable, m_pLine);
    if(pLn)
    {
        const SfxItemPropertyMapEntry* pEntry =
                                m_pPropSet->getPropertyMap().getByName(rPropertyName);
        if (!pEntry)
            throw beans::UnknownPropertyException("Unknown property: " + rPropertyName, getXWeak() );

        switch(pEntry->nWID)
        {
            case FN_UNO_ROW_HEIGHT:
            case FN_UNO_ROW_AUTO_HEIGHT:
            {
                const SwFormatFrameSize& rSize = pLn->GetFrameFormat()->GetFrameSize();
                if(FN_UNO_ROW_AUTO_HEIGHT== pEntry->nWID)
                {
                    aRet <<= SwFrameSize::Variable == rSize.GetHeightSizeType();
                }
                else
                    aRet <<= static_cast<sal_Int32>(convertTwipToMm100(rSize.GetSize().Height()));
            }
            break;

            case FN_UNO_TABLE_COLUMN_SEPARATORS:
            {
                lcl_GetTableSeparators(aRet, pTable, m_pLine->GetTabBoxes()[0], true);
            }
            break;

            default:
            {
                const SwAttrSet& rSet = pLn->GetFrameFormat()->GetAttrSet();
                SfxItemPropertySet::getPropertyValue(*pEntry, rSet, aRet);
            }
        }
    }
    return aRet;
}

void SwXTextTableRow::addPropertyChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXTextTableRow::removePropertyChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXTextTableRow::addVetoableChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXTextTableRow::removeVetoableChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXTextTableRow::Notify(const SfxHint& rHint)
{
    if(rHint.GetId() == SfxHintId::Dying)
    {
        m_pFormat = nullptr;
    }
    else if(rHint.GetId() == SfxHintId::SwFindUnoTextTableRowInstance)
    {
        auto pFindHint = static_cast<const FindUnoTextTableRowInstanceHint*>(&rHint);
        if(!pFindHint->m_pCore && pFindHint->m_pCore == m_pLine)
            pFindHint->m_pResult = this;
    }
}

SwTableLine* SwXTextTableRow::FindLine(SwTable* pTable, SwTableLine const * pLine)
{
    for(const auto& pCurrentLine : pTable->GetTabLines())
        if(pCurrentLine == pLine)
            return pCurrentLine;
    return nullptr;
}

// SwXTextTableCursor

OUString SwXTextTableCursor::getImplementationName()
    { return u"SwXTextTableCursor"_ustr; }

sal_Bool SwXTextTableCursor::supportsService(const OUString& rServiceName)
    { return cppu::supportsService(this, rServiceName); }

void SwXTextTableCursor::release() noexcept
{
    SolarMutexGuard aGuard;
    SwXTextTableCursor_Base::release();
}

const SwPaM*        SwXTextTableCursor::GetPaM() const  { return &GetCursor(); }
SwPaM*              SwXTextTableCursor::GetPaM()        { return &GetCursor(); }
const SwDoc*        SwXTextTableCursor::GetDoc() const  { return &GetFrameFormat()->GetDoc(); }
SwDoc*              SwXTextTableCursor::GetDoc()        { return &GetFrameFormat()->GetDoc(); }
const SwUnoCursor&    SwXTextTableCursor::GetCursor() const { return *m_pUnoCursor; }
SwUnoCursor&          SwXTextTableCursor::GetCursor()       { return *m_pUnoCursor; }

uno::Sequence<OUString> SwXTextTableCursor::getSupportedServiceNames()
    { return {u"com.sun.star.text.TextTableCursor"_ustr}; }

SwXTextTableCursor::SwXTextTableCursor(SwFrameFormat* pFrameFormat, SwTableBox const* pBox)
    : m_pFrameFormat(pFrameFormat)
    , m_pPropSet(aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_TABLE_CURSOR))
{
    StartListening(m_pFrameFormat->GetNotifier());
    SwDoc& rDoc = m_pFrameFormat->GetDoc();
    const SwStartNode* pSttNd = pBox->GetSttNd();
    SwPosition aPos(*pSttNd);
    m_pUnoCursor.reset(rDoc.CreateUnoCursor(aPos, true));
    m_pUnoCursor->Move( fnMoveForward, GoInNode );
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(*m_pUnoCursor);
    rTableCursor.MakeBoxSels();
}

SwXTextTableCursor::SwXTextTableCursor(SwFrameFormat& rTableFormat, const SwTableCursor* pTableSelection)
    : m_pFrameFormat(&rTableFormat)
    , m_pPropSet(aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_TABLE_CURSOR))
{
    StartListening(m_pFrameFormat->GetNotifier());
    m_pUnoCursor.reset(pTableSelection->GetDoc().CreateUnoCursor(*pTableSelection->GetPoint(), true));
    if(pTableSelection->HasMark())
    {
        m_pUnoCursor->SetMark();
        *m_pUnoCursor->GetMark() = *pTableSelection->GetMark();
    }
    const SwSelBoxes& rBoxes = pTableSelection->GetSelectedBoxes();
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(*m_pUnoCursor);
    for(auto pBox : rBoxes)
        rTableCursor.InsertBox(*pBox);
    rTableCursor.MakeBoxSels();
}

OUString SwXTextTableCursor::getRangeName()
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    SwUnoTableCursor* pTableCursor = dynamic_cast<SwUnoTableCursor*>(&rUnoCursor);
    //!! see also SwChartDataSequence::getSourceRangeRepresentation
    if(!pTableCursor)
        return OUString();
    pTableCursor->MakeBoxSels();
    const SwStartNode* pNode = pTableCursor->GetPoint()->GetNode().FindTableBoxStartNode();
    const SwTable* pTable = SwTable::FindTable(GetFrameFormat());
    const SwTableBox* pEndBox = pTable->GetTableBox(pNode->GetIndex());
    if(pTableCursor->HasMark())
    {
        pNode = pTableCursor->GetMark()->GetNode().FindTableBoxStartNode();
        const SwTableBox* pStartBox = pTable->GetTableBox(pNode->GetIndex());
        if(pEndBox != pStartBox)
        {
            // need to switch start and end?
            if(*pTableCursor->GetPoint() < *pTableCursor->GetMark())
                std::swap(pStartBox, pEndBox);
            return pStartBox->GetName() + ":" + pEndBox->GetName();
        }
    }
    return pEndBox->GetName();
}

sal_Bool SwXTextTableCursor::gotoCellByName(const OUString& sCellName, sal_Bool bExpand)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    auto& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    lcl_CursorSelect(rTableCursor, bExpand);
    return rTableCursor.GotoTableBox(sCellName);
}

sal_Bool SwXTextTableCursor::goLeft(sal_Int16 Count, sal_Bool bExpand)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    lcl_CursorSelect(rTableCursor, bExpand);
    return rTableCursor.Left(Count);
}

sal_Bool SwXTextTableCursor::goRight(sal_Int16 Count, sal_Bool bExpand)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    lcl_CursorSelect(rTableCursor, bExpand);
    return rTableCursor.Right(Count);
}

sal_Bool SwXTextTableCursor::goUp(sal_Int16 Count, sal_Bool bExpand)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    lcl_CursorSelect(rTableCursor, bExpand);
    return rTableCursor.UpDown(true, Count, nullptr, 0,
        *rUnoCursor.GetDoc().getIDocumentLayoutAccess().GetCurrentLayout());
}

sal_Bool SwXTextTableCursor::goDown(sal_Int16 Count, sal_Bool bExpand)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    lcl_CursorSelect(rTableCursor, bExpand);
    return rTableCursor.UpDown(false, Count, nullptr, 0,
        *rUnoCursor.GetDoc().getIDocumentLayoutAccess().GetCurrentLayout());
}

void SwXTextTableCursor::gotoStart(sal_Bool bExpand)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    lcl_CursorSelect(rTableCursor, bExpand);
    rTableCursor.MoveTable(GotoCurrTable, fnTableStart);
}

void SwXTextTableCursor::gotoEnd(sal_Bool bExpand)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    lcl_CursorSelect(rTableCursor, bExpand);
    rTableCursor.MoveTable(GotoCurrTable, fnTableEnd);
}

sal_Bool SwXTextTableCursor::mergeRange()
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();

    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    {
        // HACK: remove pending actions for selecting old style tables
        UnoActionRemoveContext aRemoveContext(rTableCursor);
    }
    rTableCursor.MakeBoxSels();
    bool bResult;
    {
        UnoActionContext aContext(&rUnoCursor.GetDoc());
        bResult = TableMergeErr::Ok == rTableCursor.GetDoc().MergeTable(rTableCursor);
    }
    if(bResult)
    {
        size_t nCount = rTableCursor.GetSelectedBoxesCount();
        while (nCount--)
            rTableCursor.DeleteBox(nCount);
    }
    rTableCursor.MakeBoxSels();
    return bResult;
}

sal_Bool SwXTextTableCursor::splitRange(sal_Int16 Count, sal_Bool Horizontal)
{
    SolarMutexGuard aGuard;
    if (Count <= 0)
        throw uno::RuntimeException(u"Illegal first argument: needs to be > 0"_ustr, getXWeak());
    SwUnoCursor& rUnoCursor = GetCursor();
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    {
        // HACK: remove pending actions for selecting old style tables
        UnoActionRemoveContext aRemoveContext(rTableCursor);
    }
    rTableCursor.MakeBoxSels();
    bool bResult;
    {
        UnoActionContext aContext(&rUnoCursor.GetDoc());
        bResult = rTableCursor.GetDoc().SplitTable(rTableCursor.GetSelectedBoxes(), !Horizontal, Count);
    }
    rTableCursor.MakeBoxSels();
    return bResult;
}

uno::Reference< beans::XPropertySetInfo >  SwXTextTableCursor::getPropertySetInfo()
{
    static uno::Reference< beans::XPropertySetInfo >  xRef = m_pPropSet->getPropertySetInfo();
    return xRef;
}

void SwXTextTableCursor::setPropertyValue(const OUString& rPropertyName, const uno::Any& aValue)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    auto pEntry(m_pPropSet->getPropertyMap().getByName(rPropertyName));
    if(!pEntry)
        throw beans::UnknownPropertyException("Unknown property: " + rPropertyName, getXWeak());
    if(pEntry->nFlags & beans::PropertyAttribute::READONLY)
        throw beans::PropertyVetoException("Property is read-only: " + rPropertyName, getXWeak());
    {
        auto pSttNode = rUnoCursor.GetPointNode().StartOfSectionNode();
        const SwTableNode* pTableNode = pSttNode->FindTableNode();
        lcl_FormatTable(pTableNode->GetTable().GetFrameFormat());
    }
    auto& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    rTableCursor.MakeBoxSels();
    SwDoc& rDoc = rUnoCursor.GetDoc();
    switch(pEntry->nWID)
    {
        case FN_UNO_TABLE_CELL_BACKGROUND:
        {
            std::unique_ptr<SfxPoolItem> aBrush(std::make_unique<SvxBrushItem>(RES_BACKGROUND));
            SwDoc::GetBoxAttr(rUnoCursor, aBrush);
            aBrush->PutValue(aValue, pEntry->nMemberId);
            rDoc.SetBoxAttr(rUnoCursor, *aBrush);

        }
        break;
        case RES_BOXATR_FORMAT:
        {
            SfxUInt32Item aNumberFormat(RES_BOXATR_FORMAT);
            aNumberFormat.PutValue(aValue, 0);
            rDoc.SetBoxAttr(rUnoCursor, aNumberFormat);
        }
        break;
        case FN_UNO_PARA_STYLE:
            SwUnoCursorHelper::SetTextFormatColl(aValue, rUnoCursor);
        break;
        default:
        {
            SfxItemSet aItemSet(rDoc.GetAttrPool(), pEntry->nWID, pEntry->nWID);
            SwUnoCursorHelper::GetCursorAttr(rTableCursor.GetSelRing(),
                    aItemSet);

            if (!SwUnoCursorHelper::SetCursorPropertyValue(
                    *pEntry, aValue, rTableCursor.GetSelRing(), aItemSet))
            {
                SfxItemPropertySet::setPropertyValue(*pEntry, aValue, aItemSet);
            }
            SwUnoCursorHelper::SetCursorAttr(rTableCursor.GetSelRing(),
                    aItemSet, SetAttrMode::DEFAULT, true);
        }
    }
}

uno::Any SwXTextTableCursor::getPropertyValue(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;
    SwUnoCursor& rUnoCursor = GetCursor();
    {
        auto pSttNode = rUnoCursor.GetPointNode().StartOfSectionNode();
        const SwTableNode* pTableNode = pSttNode->FindTableNode();
        lcl_FormatTable(pTableNode->GetTable().GetFrameFormat());
    }
    SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(rUnoCursor);
    auto pEntry(m_pPropSet->getPropertyMap().getByName(rPropertyName));
    if(!pEntry)
        throw beans::UnknownPropertyException("Unknown property: " + rPropertyName, getXWeak());
    rTableCursor.MakeBoxSels();
    uno::Any aResult;
    switch(pEntry->nWID)
    {
        case FN_UNO_TABLE_CELL_BACKGROUND:
        {
            std::unique_ptr<SfxPoolItem> aBrush(std::make_unique<SvxBrushItem>(RES_BACKGROUND));
            if (SwDoc::GetBoxAttr(rUnoCursor, aBrush))
                aBrush->QueryValue(aResult, pEntry->nMemberId);
        }
        break;
        case RES_BOXATR_FORMAT:
            // TODO: GetAttr for table selections in a Doc is missing
            throw uno::RuntimeException("Unknown property: " + rPropertyName, getXWeak());
        break;
        case FN_UNO_PARA_STYLE:
        {
            auto pFormat(SwUnoCursorHelper::GetCurTextFormatColl(rUnoCursor, false));
            if(pFormat)
            {
                ProgName ret;
                SwStyleNameMapper::FillProgName(pFormat->GetName(), ret, SwGetPoolIdFromName::TxtColl);
                aResult <<= ret.toString();
            }
        }
        break;
        default:
        {
            SfxItemSetFixed
                <RES_CHRATR_BEGIN, RES_FRMATR_END-1,
                RES_UNKNOWNATR_CONTAINER, RES_UNKNOWNATR_CONTAINER>
                    aSet(rTableCursor.GetDoc().GetAttrPool());
            SwUnoCursorHelper::GetCursorAttr(rTableCursor.GetSelRing(), aSet);
            SfxItemPropertySet::getPropertyValue(*pEntry, aSet, aResult);
        }
    }
    return aResult;
}

void SwXTextTableCursor::addPropertyChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXTextTableCursor::removePropertyChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXTextTableCursor::addVetoableChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXTextTableCursor::removeVetoableChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"not implemented"_ustr, getXWeak()); };

void SwXTextTableCursor::Notify( const SfxHint& rHint )
{
    if(rHint.GetId() == SfxHintId::Dying)
        m_pFrameFormat = nullptr;
}


// SwXTextTable ===========================================================

namespace {

class SwTableProperties_Impl
{
    SwUnoCursorHelper::SwAnyMapHelper m_aAnyMap;

public:
    SwTableProperties_Impl();

    void SetProperty(sal_uInt16 nWhichId, sal_uInt16 nMemberId, const uno::Any& aVal);
    const uno::Any* GetProperty(sal_uInt16 nWhichId, sal_uInt16 nMemberId);
    void AddItemToSet(SfxItemSet& rSet, std::function<std::unique_ptr<SfxPoolItem>()> aItemFactory,
                        sal_uInt16 nWhich, std::initializer_list<sal_uInt16> vMember, bool bAddTwips = false);
    void ApplyTableAttr(const SwTable& rTable, SwDoc& rDoc);
};

}

SwTableProperties_Impl::SwTableProperties_Impl()
    { }

void SwTableProperties_Impl::SetProperty(sal_uInt16 nWhichId, sal_uInt16 nMemberId, const uno::Any& rVal)
    {
        m_aAnyMap.SetValue(nWhichId, nMemberId, rVal);
    }

const uno::Any* SwTableProperties_Impl::GetProperty(sal_uInt16 nWhichId, sal_uInt16 nMemberId)
    {
        const uno::Any* pAny = nullptr;
        m_aAnyMap.FillValue(nWhichId, nMemberId, pAny);
        return pAny;
    }

void SwTableProperties_Impl::AddItemToSet(SfxItemSet& rSet,
        std::function<std::unique_ptr<SfxPoolItem>()> aItemFactory,
        sal_uInt16 nWhich, std::initializer_list<sal_uInt16> vMember, bool bAddTwips)
{
    std::vector< std::pair<sal_uInt16, const uno::Any* > > vMemberAndAny;
    for(sal_uInt16 nMember : vMember)
    {
        if (const uno::Any* pAny = GetProperty(nWhich, nMember))
            vMemberAndAny.emplace_back(nMember, pAny);
    }
    if(!vMemberAndAny.empty())
    {
        std::unique_ptr<SfxPoolItem> aItem(aItemFactory());
        for(const auto& aMemberAndAny : vMemberAndAny)
            aItem->PutValue(*aMemberAndAny.second, aMemberAndAny.first | (bAddTwips ? CONVERT_TWIPS : 0) );
        rSet.Put(std::move(aItem));
    }
}
void SwTableProperties_Impl::ApplyTableAttr(const SwTable& rTable, SwDoc& rDoc)
{
    SfxItemSetFixed<
            RES_FRM_SIZE, RES_BREAK,
            RES_HORI_ORIENT, RES_HORI_ORIENT,
            RES_BACKGROUND, RES_BACKGROUND,
            RES_SHADOW, RES_SHADOW,
            RES_KEEP, RES_KEEP,
            RES_LAYOUT_SPLIT, RES_LAYOUT_SPLIT>
        aSet(rDoc.GetAttrPool());
    const SwFrameFormat &rFrameFormat = *rTable.GetFrameFormat();
    if (const uno::Any* pRepHead = GetProperty(FN_TABLE_HEADLINE_REPEAT, 0xff))
    {
        bool bVal(pRepHead->get<bool>());
        const_cast<SwTable&>(rTable).SetRowsToRepeat( bVal ? 1 : 0 );  // TODO: MULTIHEADER
    }

    AddItemToSet(aSet, [&rFrameFormat]() { return rFrameFormat.makeBackgroundBrushItem(); }, RES_BACKGROUND, {
        MID_BACK_COLOR,
        MID_GRAPHIC_TRANSPARENT,
        MID_GRAPHIC_POSITION,
        MID_GRAPHIC,
        MID_GRAPHIC_FILTER });

    bool bPutBreak = true;
    const uno::Any* pPage = GetProperty(FN_UNO_PAGE_STYLE, 0);
    if (!pPage)
        pPage = GetProperty(RES_PAGEDESC, 0xff);
    if (pPage)
    {
        OUString sPageStyleProgName = pPage->get<OUString>();
        if(!sPageStyleProgName.isEmpty())
        {
            UIName sPageStyle;
            SwStyleNameMapper::FillUIName(ProgName(sPageStyleProgName), sPageStyle, SwGetPoolIdFromName::PageDesc);
            const SwPageDesc* pDesc = SwPageDesc::GetByName(rDoc, sPageStyle);
            if(pDesc)
            {
                SwFormatPageDesc aDesc(pDesc);
                if (const uno::Any* pPgNo = GetProperty(RES_PAGEDESC, MID_PAGEDESC_PAGENUMOFFSET))
                {
                    aDesc.SetNumOffset(pPgNo->get<sal_Int16>());
                }
                aSet.Put(aDesc);
                bPutBreak = false;
            }

        }
    }

    if(bPutBreak)
        AddItemToSet(aSet, [&rFrameFormat]() { return std::unique_ptr<SfxPoolItem>(rFrameFormat.GetBreak().Clone()); }, RES_BREAK, {0});
    AddItemToSet(aSet, [&rFrameFormat]() { return std::unique_ptr<SfxPoolItem>(rFrameFormat.GetShadow().Clone()); }, RES_SHADOW, {0}, true);
    AddItemToSet(aSet, [&rFrameFormat]() { return std::unique_ptr<SfxPoolItem>(rFrameFormat.GetKeep().Clone()); }, RES_KEEP, {0});
    AddItemToSet(aSet, [&rFrameFormat]() { return std::unique_ptr<SfxPoolItem>(rFrameFormat.GetHoriOrient().Clone()); }, RES_HORI_ORIENT, {MID_HORIORIENT_ORIENT}, true);

    const uno::Any* pSzRel = GetProperty(FN_TABLE_IS_RELATIVE_WIDTH, 0xff);
    const uno::Any* pRelWidth = GetProperty(FN_TABLE_RELATIVE_WIDTH, 0xff);

    bool bPutSize = false;
    SwFormatFrameSize aSz(SwFrameSize::Variable);
    if (const uno::Any* pWidth = GetProperty(FN_TABLE_WIDTH, 0xff))
    {
        aSz.PutValue(*pWidth, MID_FRMSIZE_WIDTH);
        bPutSize = true;
    }
    if(pSzRel && pSzRel->get<bool>() && pRelWidth)
    {
        aSz.PutValue(*pRelWidth, MID_FRMSIZE_REL_WIDTH|CONVERT_TWIPS); // CONVERT_TWIPS here???
        bPutSize = true;
    }
    if(bPutSize)
    {
        if(!aSz.GetWidth())
            aSz.SetWidth(MINLAY);
        aSet.Put(aSz);
    }
    AddItemToSet(aSet, [&rFrameFormat]() { return std::unique_ptr<SfxPoolItem>(rFrameFormat.GetLRSpace().Clone()); }, RES_LR_SPACE, {
        MID_L_MARGIN|CONVERT_TWIPS,
        MID_R_MARGIN|CONVERT_TWIPS });
    AddItemToSet(aSet, [&rFrameFormat]() { return std::unique_ptr<SfxPoolItem>(rFrameFormat.GetULSpace().Clone()); }, RES_UL_SPACE, {
        MID_UP_MARGIN|CONVERT_TWIPS,
        MID_LO_MARGIN|CONVERT_TWIPS });
    if (const ::uno::Any* pSplit = GetProperty(RES_LAYOUT_SPLIT, 0))
    {
        SwFormatLayoutSplit aSp(pSplit->get<bool>());
        aSet.Put(aSp);
    }
    if(aSet.Count())
    {
        rDoc.SetAttr(aSet, *rTable.GetFrameFormat());
    }
}

class SwXTextTable::Impl
    : public SvtListener
{
private:
    SwFrameFormat* m_pFrameFormat;

public:
    unotools::WeakReference<SwXTextTable> m_wThis;
    std::mutex m_Mutex; // just for OInterfaceContainerHelper4
    ::comphelper::OInterfaceContainerHelper4<css::lang::XEventListener> m_EventListeners;
    ::comphelper::OInterfaceContainerHelper4<chart::XChartDataChangeEventListener> m_ChartListeners;

    const SfxItemPropertySet * m_pPropSet;

    unotools::WeakReference<SwXTableRows> m_xRows;
    unotools::WeakReference<SwXTableColumns> m_xColumns;

    bool m_bFirstRowAsLabel;
    bool m_bFirstColumnAsLabel;

    // Descriptor-interface
    std::unique_ptr<SwTableProperties_Impl> m_pTableProps;
    OUString       m_sTableName;
    unsigned short m_nRows;
    unsigned short m_nColumns;

    explicit Impl(SwFrameFormat* const pFrameFormat)
        : m_pFrameFormat(pFrameFormat)
        , m_pPropSet(aSwMapProvider.GetPropertySet(PROPERTY_MAP_TEXT_TABLE))
        , m_bFirstRowAsLabel(false)
        , m_bFirstColumnAsLabel(false)
        , m_pTableProps(pFrameFormat ? nullptr : new SwTableProperties_Impl)
        , m_nRows(pFrameFormat ? 0 : 2)
        , m_nColumns(pFrameFormat ? 0 : 2)
    {
        if(m_pFrameFormat)
            StartListening(m_pFrameFormat->GetNotifier());
    }

    SwFrameFormat* GetFrameFormat() { return m_pFrameFormat; }
    void SetFrameFormat(SwFrameFormat& rFrameFormat)
    {
        EndListeningAll();
        m_pFrameFormat = &rFrameFormat;
        StartListening(m_pFrameFormat->GetNotifier());
    }

    bool IsDescriptor() const { return m_pTableProps != nullptr; }

    // note: lock mutex before calling this to avoid concurrent update
    static std::pair<sal_uInt16, sal_uInt16> ThrowIfComplex(SwXTextTable &rThis)
    {
        sal_uInt16 const nRowCount(rThis.m_pImpl->GetRowCount());
        sal_uInt16 const nColCount(rThis.m_pImpl->GetColumnCount());
        if (!nRowCount || !nColCount)
        {
            throw uno::RuntimeException(u"Table too complex"_ustr, rThis.getXWeak());
        }
        return std::make_pair(nRowCount, nColCount);
    }

    sal_uInt16 GetRowCount();
    sal_uInt16 GetColumnCount();

    virtual void Notify(const SfxHint&) override;

};

SwXTextTable::SwXTextTable()
    : m_pImpl(new Impl(nullptr))
{
}

SwXTextTable::SwXTextTable(SwFrameFormat& rFrameFormat)
    : m_pImpl(new Impl(&rFrameFormat))
{
}

SwXTextTable::~SwXTextTable()
{
}

rtl::Reference<SwXTextTable> SwXTextTable::CreateXTextTable(SwFrameFormat* const pFrameFormat)
{
    rtl::Reference<SwXTextTable> xTable;
    if (pFrameFormat)
        xTable = dynamic_cast<SwXTextTable*>(pFrameFormat->GetXObject().get().get()); // cached?
    if (xTable.is())
        return xTable;
    if (pFrameFormat)
    {
        xTable = new SwXTextTable(*pFrameFormat);
        pFrameFormat->SetXObject(xTable->getXWeak());
    }
    else
        xTable = new SwXTextTable();

    // need a permanent Reference to initialize m_wThis
    xTable->m_pImpl->m_wThis = xTable.get();
    return xTable;
}

SwFrameFormat* SwXTextTable::GetFrameFormat()
{
    return m_pImpl->GetFrameFormat();
}

void SwXTextTable::initialize(sal_Int32 nR, sal_Int32 nC)
{
    if (!m_pImpl->IsDescriptor() || nR <= 0 || nC <= 0 || nR >= SAL_MAX_UINT16 || nC >= SAL_MAX_UINT16)
        throw uno::RuntimeException();
    m_pImpl->m_nRows = o3tl::narrowing<sal_uInt16>(nR);
    m_pImpl->m_nColumns = o3tl::narrowing<sal_uInt16>(nC);
}

uno::Reference<table::XTableRows> SAL_CALL SwXTextTable::getRows()
{
    return getSwRows();
}

rtl::Reference<SwXTableRows> SwXTextTable::getSwRows()
{
    SolarMutexGuard aGuard;
    rtl::Reference<SwXTableRows> xResult(m_pImpl->m_xRows);
    if(xResult)
        return xResult;
    if(SwFrameFormat* pFormat = GetFrameFormat())
    {
        xResult = new SwXTableRows(*pFormat);
        m_pImpl->m_xRows = xResult.get();
    }
    if(!xResult.is())
        throw uno::RuntimeException();
    return xResult;
}

uno::Reference<table::XTableColumns> SAL_CALL SwXTextTable::getColumns()
{
    SolarMutexGuard aGuard;
    rtl::Reference<SwXTableColumns> xResult(m_pImpl->m_xColumns.get());
    if(xResult.is())
        return xResult;
    if(SwFrameFormat* pFormat = GetFrameFormat())
    {
        xResult = new SwXTableColumns(*pFormat);
        m_pImpl->m_xColumns = xResult.get();
    }
    if(!xResult.is())
        throw uno::RuntimeException();
    return xResult;
}

uno::Reference<table::XCell> SwXTextTable::getCellByName(const OUString& sCellName)
{
    return uno::Reference<table::XCell>(getSwCellByName(sCellName));
}

rtl::Reference<SwXCell> SwXTextTable::getSwCellByName(const OUString& sCellName)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = lcl_EnsureCoreConnected(GetFrameFormat(), this);
    SwTable* pTable = SwTable::FindTable(pFormat);
    SwTableBox* pBox = const_cast<SwTableBox*>(pTable->GetTableBox(sCellName));
    if(!pBox)
        return nullptr;
    return SwXCell::CreateXCell(pFormat, pBox);
}

uno::Sequence<OUString> SwXTextTable::getCellNames()
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat(GetFrameFormat());
    if(!pFormat)
        return {};
    SwTable* pTable = SwTable::FindTable(pFormat);
    // exists at the table and at all boxes
    SwTableLines& rTableLines = pTable->GetTabLines();
    std::vector<OUString> aAllNames;
    lcl_InspectLines(rTableLines, aAllNames);
    return comphelper::containerToSequence(aAllNames);
}

uno::Reference<text::XTextTableCursor> SwXTextTable::createCursorByCellName(const OUString& sCellName)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = lcl_EnsureCoreConnected(GetFrameFormat(), this);
    SwTable* pTable = SwTable::FindTable(pFormat);
    SwTableBox* pBox = const_cast<SwTableBox*>(pTable->GetTableBox(sCellName));
    if(!pBox || pBox->getRowSpan() == 0)
        throw uno::RuntimeException();
    return new SwXTextTableCursor(pFormat, pBox);
}

void SAL_CALL
SwXTextTable::attach(const uno::Reference<text::XTextRange> & xTextRange)
{
    SolarMutexGuard aGuard;

    // attach() must only be called once
    if (!m_pImpl->IsDescriptor())  /* already attached ? */
        throw uno::RuntimeException(u"SwXTextTable: already attached to range."_ustr, getXWeak());

    SwXTextRange* pRange(dynamic_cast<SwXTextRange*>(xTextRange.get()));
    OTextCursorHelper* pCursor(dynamic_cast<OTextCursorHelper*>(xTextRange.get()));
    SwDoc* pDoc = pRange ? &pRange->GetDoc() : pCursor ? pCursor->GetDoc() : nullptr;
    if (!pDoc || !m_pImpl->m_nRows || !m_pImpl->m_nColumns)
        throw lang::IllegalArgumentException();
    SwUnoInternalPaM aPam(*pDoc);
    // this now needs to return TRUE
    ::sw::XTextRangeToSwPaM(aPam, xTextRange);
    {
        UnoActionContext aCont(pDoc);

        pDoc->GetIDocumentUndoRedo().StartUndo(SwUndoId::EMPTY, nullptr);
        const SwTable* pTable(nullptr);
        if( 0 != aPam.Start()->GetContentIndex() )
        {
            pDoc->getIDocumentContentOperations().SplitNode(*aPam.Start(), false);
        }
        //TODO: if it is the last paragraph than add another one!
        if(aPam.HasMark())
        {
            pDoc->getIDocumentContentOperations().DeleteAndJoin(aPam);
            aPam.DeleteMark();
        }

        OUString tableName;
        if (const uno::Any* pName = m_pImpl->m_pTableProps->GetProperty(FN_UNO_TABLE_NAME, 0))
        {
            tableName = pName->get<OUString>();
        }
        else if (!m_pImpl->m_sTableName.isEmpty())
        {
            sal_uInt16 nIndex = 1;
            tableName = m_pImpl->m_sTableName;
            while (pDoc->FindTableFormatByName(UIName(tableName), true) && nIndex < USHRT_MAX)
                tableName = m_pImpl->m_sTableName + OUString::number(nIndex++);
        }

        pTable = pDoc->InsertTable(SwInsertTableOptions( SwInsertTableFlags::Headline | SwInsertTableFlags::DefaultBorder | SwInsertTableFlags::SplitLayout, 0 ),
                *aPam.GetPoint(),
                m_pImpl->m_nRows,
                m_pImpl->m_nColumns,
                text::HoriOrientation::FULL,
                nullptr, nullptr, false, true,
                tableName);
        if(pTable)
        {
            // here, the properties of the descriptor need to be analyzed
            m_pImpl->m_pTableProps->ApplyTableAttr(*pTable, *pDoc);
            SwFrameFormat* pTableFormat(pTable->GetFrameFormat());
            lcl_FormatTable(pTableFormat);

            m_pImpl->SetFrameFormat(*pTableFormat);

            m_pImpl->m_pTableProps.reset();
        }
        pDoc->GetIDocumentUndoRedo().EndUndo( SwUndoId::END, nullptr );
    }
}

uno::Reference<text::XTextRange>  SwXTextTable::getAnchor()
{
    SolarMutexGuard aGuard;
    SwTableFormat *const pFormat = static_cast<SwTableFormat*>(
        lcl_EnsureCoreConnected(GetFrameFormat(), this));
    return new SwXTextRange(*pFormat);
}

void SwXTextTable::dispose()
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = lcl_EnsureCoreConnected(GetFrameFormat(), this);
    SwTable* pTable = SwTable::FindTable(pFormat);
    SwSelBoxes aSelBoxes;
    for(auto& rBox : pTable->GetTabSortBoxes() )
        aSelBoxes.insert(rBox);
    pFormat->GetDoc().DeleteRowCol(aSelBoxes, SwDoc::RowColMode::DeleteProtected);
}

void SAL_CALL SwXTextTable::addEventListener(
        const uno::Reference<lang::XEventListener> & xListener)
{
    // no need to lock here as m_pImpl is const and container threadsafe
    std::unique_lock aGuard(m_pImpl->m_Mutex);
    m_pImpl->m_EventListeners.addInterface(aGuard, xListener);
}

void SAL_CALL SwXTextTable::removeEventListener(
        const uno::Reference< lang::XEventListener > & xListener)
{
    // no need to lock here as m_pImpl is const and container threadsafe
    std::unique_lock aGuard(m_pImpl->m_Mutex);
    m_pImpl->m_EventListeners.removeInterface(aGuard, xListener);
}

uno::Reference<table::XCell> SwXTextTable::getCellByPosition(sal_Int32 nColumn, sal_Int32 nRow)
{
    return uno::Reference<table::XCell>(getSwCellByPosition(nColumn, nRow));
}

rtl::Reference<SwXCell>  SwXTextTable::getSwCellByPosition(sal_Int32 nColumn, sal_Int32 nRow)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat(GetFrameFormat());
    // sheet is unimportant
    if(nColumn >= 0 && nRow >= 0 && pFormat)
    {
        auto pXCell = lcl_CreateXCell(pFormat, nColumn, nRow);
        if(pXCell)
            return pXCell;
    }
    throw lang::IndexOutOfBoundsException();
}

namespace {

rtl::Reference<SwXCellRange> GetRangeByName(
        SwFrameFormat* pFormat, SwTable const * pTable,
        const OUString& rTLName, const OUString& rBRName,
        SwRangeDescriptor const & rDesc)
{
    const SwTableBox* pTLBox = pTable->GetTableBox(rTLName);
    if(!pTLBox)
        return nullptr;
    const SwStartNode* pSttNd = pTLBox->GetSttNd();
    SwPosition aPos(*pSttNd);
    // set cursor to the upper-left cell of the range
    auto pUnoCursor(pFormat->GetDoc().CreateUnoCursor(aPos, true));
    pUnoCursor->Move(fnMoveForward, GoInNode);
    pUnoCursor->SetRemainInSection(false);
    const SwTableBox* pBRBox(pTable->GetTableBox(rBRName));
    if(!pBRBox)
        return nullptr;
    pUnoCursor->SetMark();
    pUnoCursor->GetPoint()->Assign( *pBRBox->GetSttNd() );
    pUnoCursor->Move( fnMoveForward, GoInNode );
    SwUnoTableCursor& rCursor = dynamic_cast<SwUnoTableCursor&>(*pUnoCursor);
    // HACK: remove pending actions for selecting old style tables
    UnoActionRemoveContext aRemoveContext(rCursor);
    rCursor.MakeBoxSels();
    // pUnoCursor will be provided and will not be deleted
    return SwXCellRange::CreateXCellRange(std::move(pUnoCursor), *pFormat, rDesc);
}

} // namespace

uno::Reference<table::XCellRange>  SwXTextTable::getCellRangeByPosition(sal_Int32 nLeft, sal_Int32 nTop, sal_Int32 nRight, sal_Int32 nBottom)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat(GetFrameFormat());
    if(pFormat &&
            nLeft <= nRight && nTop <= nBottom &&
            nLeft >= 0 && nRight >= 0 && nTop >= 0 && nBottom >= 0 )
    {
        SwTable* pTable = SwTable::FindTable(pFormat);
        if(!pTable->IsTableComplex())
        {
            SwRangeDescriptor aDesc;
            aDesc.nTop    = nTop;
            aDesc.nBottom = nBottom;
            aDesc.nLeft   = nLeft;
            aDesc.nRight  = nRight;
            const OUString sTLName = sw_GetCellName(aDesc.nLeft, aDesc.nTop);
            const OUString sBRName = sw_GetCellName(aDesc.nRight, aDesc.nBottom);
            // please note that according to the 'if' statement at the begin
            // sTLName:sBRName already denotes the normalized range string
            return GetRangeByName(pFormat, pTable, sTLName, sBRName, aDesc);
        }
    }
    throw lang::IndexOutOfBoundsException();
}

uno::Reference<table::XCellRange> SwXTextTable::getCellRangeByName(const OUString& sRange)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = lcl_EnsureCoreConnected(GetFrameFormat(), this);
    SwTable* pTable = lcl_EnsureTableNotComplex(SwTable::FindTable(pFormat), this);
    sal_Int32 nPos = 0;
    const OUString sTLName(sRange.getToken(0, ':', nPos));
    const OUString sBRName(sRange.getToken(0, ':', nPos));
    if(sTLName.isEmpty() || sBRName.isEmpty())
        throw uno::RuntimeException();
    SwRangeDescriptor aDesc;
    aDesc.nTop = aDesc.nLeft = aDesc.nBottom = aDesc.nRight = -1;
    SwXTextTable::GetCellPosition(sTLName, aDesc.nLeft, aDesc.nTop );
    SwXTextTable::GetCellPosition(sBRName, aDesc.nRight, aDesc.nBottom );

    // we should normalize the range now (e.g. A5:C1 will become A1:C5)
    // since (depending on what is done later) it will be troublesome
    // elsewhere when the cursor in the implementation does not
    // point to the top-left and bottom-right cells
    aDesc.Normalize();
    return GetRangeByName(pFormat, pTable, sTLName, sBRName, aDesc);
}

uno::Sequence< uno::Sequence< uno::Any > > SAL_CALL SwXTextTable::getDataArray()
{
    SolarMutexGuard aGuard;
    std::pair<sal_uInt16, sal_uInt16> const RowsAndColumns(SwXTextTable::Impl::ThrowIfComplex(*this));
    uno::Reference<sheet::XCellRangeData> const xAllRange(
        getCellRangeByPosition(0, 0, RowsAndColumns.second-1, RowsAndColumns.first-1),
        uno::UNO_QUERY_THROW);
    return xAllRange->getDataArray();
}

void SAL_CALL SwXTextTable::setDataArray(const uno::Sequence< uno::Sequence< uno::Any > >& rArray)
{
    SolarMutexGuard aGuard;
    std::pair<sal_uInt16, sal_uInt16> const RowsAndColumns(SwXTextTable::Impl::ThrowIfComplex(*this));
    uno::Reference<sheet::XCellRangeData> const xAllRange(
        getCellRangeByPosition(0, 0, RowsAndColumns.second-1, RowsAndColumns.first-1),
        uno::UNO_QUERY_THROW);
    return xAllRange->setDataArray(rArray);
}

uno::Sequence< uno::Sequence< double > > SwXTextTable::getData()
{
    SolarMutexGuard aGuard;
    std::pair<sal_uInt16, sal_uInt16> const RowsAndColumns(SwXTextTable::Impl::ThrowIfComplex(*this));
    uno::Reference<chart::XChartDataArray> const xAllRange(
        getCellRangeByPosition(0, 0, RowsAndColumns.second-1, RowsAndColumns.first-1),
        uno::UNO_QUERY_THROW);
    static_cast<SwXCellRange*>(xAllRange.get())->SetLabels(
            m_pImpl->m_bFirstRowAsLabel, m_pImpl->m_bFirstColumnAsLabel);
    return xAllRange->getData();
}

void SwXTextTable::setData(const uno::Sequence< uno::Sequence< double > >& rData)
{
    SolarMutexGuard aGuard;
    std::pair<sal_uInt16, sal_uInt16> const RowsAndColumns(SwXTextTable::Impl::ThrowIfComplex(*this));
    uno::Reference<chart::XChartDataArray> const xAllRange(
        getCellRangeByPosition(0, 0, RowsAndColumns.second-1, RowsAndColumns.first-1),
        uno::UNO_QUERY_THROW);
    static_cast<SwXCellRange*>(xAllRange.get())->SetLabels(
            m_pImpl->m_bFirstRowAsLabel, m_pImpl->m_bFirstColumnAsLabel);
    xAllRange->setData(rData);
    // this is rather inconsistent: setData on XTextTable sends events, but e.g. CellRanges do not
    lcl_SendChartEvent(m_pImpl->m_Mutex, *this, m_pImpl->m_ChartListeners);
}

uno::Sequence<OUString> SwXTextTable::getRowDescriptions()
{
    SolarMutexGuard aGuard;
    std::pair<sal_uInt16, sal_uInt16> const RowsAndColumns(SwXTextTable::Impl::ThrowIfComplex(*this));
    uno::Reference<chart::XChartDataArray> const xAllRange(
        getCellRangeByPosition(0, 0, RowsAndColumns.second-1, RowsAndColumns.first-1),
        uno::UNO_QUERY_THROW);
    static_cast<SwXCellRange*>(xAllRange.get())->SetLabels(
            m_pImpl->m_bFirstRowAsLabel, m_pImpl->m_bFirstColumnAsLabel);
    return xAllRange->getRowDescriptions();
}

void SwXTextTable::setRowDescriptions(const uno::Sequence<OUString>& rRowDesc)
{
    SolarMutexGuard aGuard;
    std::pair<sal_uInt16, sal_uInt16> const RowsAndColumns(SwXTextTable::Impl::ThrowIfComplex(*this));
    uno::Reference<chart::XChartDataArray> const xAllRange(
        getCellRangeByPosition(0, 0, RowsAndColumns.second-1, RowsAndColumns.first-1),
        uno::UNO_QUERY_THROW);
    static_cast<SwXCellRange*>(xAllRange.get())->SetLabels(
            m_pImpl->m_bFirstRowAsLabel, m_pImpl->m_bFirstColumnAsLabel);
    xAllRange->setRowDescriptions(rRowDesc);
}

uno::Sequence<OUString> SwXTextTable::getColumnDescriptions()
{
    SolarMutexGuard aGuard;
    std::pair<sal_uInt16, sal_uInt16> const RowsAndColumns(SwXTextTable::Impl::ThrowIfComplex(*this));
    uno::Reference<chart::XChartDataArray> const xAllRange(
        getCellRangeByPosition(0, 0, RowsAndColumns.second-1, RowsAndColumns.first-1),
        uno::UNO_QUERY_THROW);
    static_cast<SwXCellRange*>(xAllRange.get())->SetLabels(
            m_pImpl->m_bFirstRowAsLabel, m_pImpl->m_bFirstColumnAsLabel);
    return xAllRange->getColumnDescriptions();
}

void SwXTextTable::setColumnDescriptions(const uno::Sequence<OUString>& rColumnDesc)
{
    SolarMutexGuard aGuard;
    std::pair<sal_uInt16, sal_uInt16> const RowsAndColumns(SwXTextTable::Impl::ThrowIfComplex(*this));
    uno::Reference<chart::XChartDataArray> const xAllRange(
        getCellRangeByPosition(0, 0, RowsAndColumns.second-1, RowsAndColumns.first-1),
        uno::UNO_QUERY_THROW);
    static_cast<SwXCellRange*>(xAllRange.get())->SetLabels(
            m_pImpl->m_bFirstRowAsLabel, m_pImpl->m_bFirstColumnAsLabel);
    return xAllRange->setColumnDescriptions(rColumnDesc);
}

void SAL_CALL SwXTextTable::addChartDataChangeEventListener(
    const uno::Reference<chart::XChartDataChangeEventListener> & xListener)
{
    // no need to lock here as m_pImpl is const and container threadsafe
    std::unique_lock aGuard(m_pImpl->m_Mutex);
    m_pImpl->m_ChartListeners.addInterface(aGuard, xListener);
}

void SAL_CALL SwXTextTable::removeChartDataChangeEventListener(
    const uno::Reference<chart::XChartDataChangeEventListener> & xListener)
{
    // no need to lock here as m_pImpl is const and container threadsafe
    std::unique_lock aGuard(m_pImpl->m_Mutex);
    m_pImpl->m_ChartListeners.removeInterface(aGuard, xListener);
}

sal_Bool SwXTextTable::isNotANumber(double nNumber)
{
    // We use DBL_MIN because starcalc does (which uses it because chart
    // wants it that way!)
    return ( nNumber == DBL_MIN );
}

double SwXTextTable::getNotANumber()
{
    // We use DBL_MIN because starcalc does (which uses it because chart
    // wants it that way!)
    return DBL_MIN;
}

uno::Sequence< beans::PropertyValue > SwXTextTable::createSortDescriptor()
{
    SolarMutexGuard aGuard;

    return SwUnoCursorHelper::CreateSortDescriptor(true);
}

void SwXTextTable::sort(const uno::Sequence< beans::PropertyValue >& rDescriptor)
{
    SolarMutexGuard aGuard;
    SwSortOptions aSortOpt;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(!(pFormat &&
        SwUnoCursorHelper::ConvertSortProperties(rDescriptor, aSortOpt)))
        return;

    SwTable* pTable = SwTable::FindTable( pFormat );
    SwSelBoxes aBoxes;
    const SwTableSortBoxes& rTBoxes = pTable->GetTabSortBoxes();
    for (size_t n = 0; n < rTBoxes.size(); ++n)
    {
        SwTableBox* pBox = rTBoxes[ n ];
        aBoxes.insert( pBox );
    }
    UnoActionContext aContext( &pFormat->GetDoc() );
    pFormat->GetDoc().SortTable(aBoxes, aSortOpt);
}

void SwXTextTable::autoFormat(const OUString& sAutoFormatName)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = lcl_EnsureCoreConnected(GetFrameFormat(), this);
    SwTable* pTable = lcl_EnsureTableNotComplex(SwTable::FindTable(pFormat), this);
    const SwTableAutoFormatTable& rAutoFormatTable = SwModule::get()->GetAutoFormatTable();
    for (size_t i = rAutoFormatTable.size(); i;)
        if( sAutoFormatName == rAutoFormatTable[ --i ].GetName() )
        {
            SwSelBoxes aBoxes;
            const SwTableSortBoxes& rTBoxes = pTable->GetTabSortBoxes();
            for (size_t n = 0; n < rTBoxes.size(); ++n)
            {
                SwTableBox* pBox = rTBoxes[ n ];
                aBoxes.insert( pBox );
            }
            UnoActionContext aContext( &pFormat->GetDoc() );
            pFormat->GetDoc().SetTableAutoFormat( aBoxes, rAutoFormatTable[i] );
            break;
        }
}

uno::Reference< beans::XPropertySetInfo >  SwXTextTable::getPropertySetInfo()
{
    static uno::Reference<beans::XPropertySetInfo> xRef = m_pImpl->m_pPropSet->getPropertySetInfo();
    return xRef;
}

void SwXTextTable::setPropertyValue(const OUString& rPropertyName, const uno::Any& aValue)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(!aValue.hasValue())
        throw lang::IllegalArgumentException();
    const SfxItemPropertyMapEntry* pEntry =
            m_pImpl->m_pPropSet->getPropertyMap().getByName(rPropertyName);
    if( !pEntry )
        throw lang::IllegalArgumentException();
    if(pFormat)
    {
        if ( pEntry->nFlags & beans::PropertyAttribute::READONLY)
            throw beans::PropertyVetoException("Property is read-only: " + rPropertyName, getXWeak() );

        if(0xBF == pEntry->nMemberId)
        {
            lcl_SetSpecialProperty(pFormat, pEntry, aValue);
        }
        else
        {
            switch(pEntry->nWID)
            {
                case FN_UNO_TABLE_NAME :
                {
                    OUString sName;
                    aValue >>= sName;
                    setName( sName );
                }
                break;

                case FN_UNO_RANGE_ROW_LABEL:
                {
                    bool bTmp = *o3tl::doAccess<bool>(aValue);
                    if (m_pImpl->m_bFirstRowAsLabel != bTmp)
                    {
                        lcl_SendChartEvent(m_pImpl->m_Mutex, *this, m_pImpl->m_ChartListeners);
                        m_pImpl->m_bFirstRowAsLabel = bTmp;
                    }
                }
                break;

                case FN_UNO_RANGE_COL_LABEL:
                {
                    bool bTmp = *o3tl::doAccess<bool>(aValue);
                    if (m_pImpl->m_bFirstColumnAsLabel != bTmp)
                    {
                        lcl_SendChartEvent(m_pImpl->m_Mutex, *this, m_pImpl->m_ChartListeners);
                        m_pImpl->m_bFirstColumnAsLabel = bTmp;
                    }
                }
                break;

                case FN_UNO_TABLE_BORDER:
                case FN_UNO_TABLE_BORDER2:
                {
                    table::TableBorder oldBorder;
                    table::TableBorder2 aBorder;
                    SvxBorderLine aTopLine;
                    SvxBorderLine aBottomLine;
                    SvxBorderLine aLeftLine;
                    SvxBorderLine aRightLine;
                    SvxBorderLine aHoriLine;
                    SvxBorderLine aVertLine;
                    if (aValue >>= oldBorder)
                    {
                        aBorder.IsTopLineValid = oldBorder.IsTopLineValid;
                        aBorder.IsBottomLineValid = oldBorder.IsBottomLineValid;
                        aBorder.IsLeftLineValid = oldBorder.IsLeftLineValid;
                        aBorder.IsRightLineValid = oldBorder.IsRightLineValid;
                        aBorder.IsHorizontalLineValid = oldBorder.IsHorizontalLineValid;
                        aBorder.IsVerticalLineValid = oldBorder.IsVerticalLineValid;
                        aBorder.Distance = oldBorder.Distance;
                        aBorder.IsDistanceValid = oldBorder.IsDistanceValid;
                        lcl_LineToSvxLine(
                                oldBorder.TopLine, aTopLine);
                        lcl_LineToSvxLine(
                                oldBorder.BottomLine, aBottomLine);
                        lcl_LineToSvxLine(
                                oldBorder.LeftLine, aLeftLine);
                        lcl_LineToSvxLine(
                                oldBorder.RightLine, aRightLine);
                        lcl_LineToSvxLine(
                                oldBorder.HorizontalLine, aHoriLine);
                        lcl_LineToSvxLine(
                                oldBorder.VerticalLine, aVertLine);
                    }
                    else if (aValue >>= aBorder)
                    {
                        SvxBoxItem::LineToSvxLine(
                                aBorder.TopLine, aTopLine, true);
                        SvxBoxItem::LineToSvxLine(
                                aBorder.BottomLine, aBottomLine, true);
                        SvxBoxItem::LineToSvxLine(
                                aBorder.LeftLine, aLeftLine, true);
                        SvxBoxItem::LineToSvxLine(
                                aBorder.RightLine, aRightLine, true);
                        SvxBoxItem::LineToSvxLine(
                                aBorder.HorizontalLine, aHoriLine, true);
                        SvxBoxItem::LineToSvxLine(
                                aBorder.VerticalLine, aVertLine, true);
                    }
                    else
                    {
                        break; // something else
                    }
                    SwDoc& rDoc = pFormat->GetDoc();
                    if(!lcl_FormatTable(pFormat))
                        break;
                    SwTable* pTable = SwTable::FindTable( pFormat );
                    SwTableLines &rLines = pTable->GetTabLines();

                    const SwTableBox* pTLBox = lcl_FindCornerTableBox(rLines, true);
                    const SwStartNode* pSttNd = pTLBox->GetSttNd();
                    SwPosition aPos(*pSttNd);
                    // set cursor to top left cell
                    auto pUnoCursor(rDoc.CreateUnoCursor(aPos, true));
                    pUnoCursor->Move( fnMoveForward, GoInNode );
                    pUnoCursor->SetRemainInSection( false );

                    const SwTableBox* pBRBox = lcl_FindCornerTableBox(rLines, false);
                    pUnoCursor->SetMark();
                    pUnoCursor->GetPoint()->Assign( *pBRBox->GetSttNd() );
                    pUnoCursor->Move( fnMoveForward, GoInNode );
                    SwUnoTableCursor& rCursor = dynamic_cast<SwUnoTableCursor&>(*pUnoCursor);
                    // HACK: remove pending actions for selecting old style tables
                    UnoActionRemoveContext aRemoveContext(rCursor);
                    rCursor.MakeBoxSels();

                    SfxItemSetFixed<RES_BOX, RES_BOX,
                                    SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER>
                         aSet(rDoc.GetAttrPool());

                    SvxBoxItem aBox( RES_BOX );
                    SvxBoxInfoItem aBoxInfo( SID_ATTR_BORDER_INNER );

                    aBox.SetLine(aTopLine.isEmpty() ? nullptr : &aTopLine, SvxBoxItemLine::TOP);
                    aBoxInfo.SetValid(SvxBoxInfoItemValidFlags::TOP, aBorder.IsTopLineValid);

                    aBox.SetLine(aBottomLine.isEmpty() ? nullptr : &aBottomLine, SvxBoxItemLine::BOTTOM);
                    aBoxInfo.SetValid(SvxBoxInfoItemValidFlags::BOTTOM, aBorder.IsBottomLineValid);

                    aBox.SetLine(aLeftLine.isEmpty() ? nullptr : &aLeftLine, SvxBoxItemLine::LEFT);
                    aBoxInfo.SetValid(SvxBoxInfoItemValidFlags::LEFT, aBorder.IsLeftLineValid);

                    aBox.SetLine(aRightLine.isEmpty() ? nullptr : &aRightLine, SvxBoxItemLine::RIGHT);
                    aBoxInfo.SetValid(SvxBoxInfoItemValidFlags::RIGHT, aBorder.IsRightLineValid);

                    aBoxInfo.SetLine(aHoriLine.isEmpty() ? nullptr : &aHoriLine, SvxBoxInfoItemLine::HORI);
                    aBoxInfo.SetValid(SvxBoxInfoItemValidFlags::HORI, aBorder.IsHorizontalLineValid);

                    aBoxInfo.SetLine(aVertLine.isEmpty() ? nullptr : &aVertLine, SvxBoxInfoItemLine::VERT);
                    aBoxInfo.SetValid(SvxBoxInfoItemValidFlags::VERT, aBorder.IsVerticalLineValid);

                    aBox.SetAllDistances(o3tl::toTwips(aBorder.Distance, o3tl::Length::mm100));
                    aBoxInfo.SetValid(SvxBoxInfoItemValidFlags::DISTANCE, aBorder.IsDistanceValid);

                    aSet.Put(aBox);
                    aSet.Put(aBoxInfo);

                    rDoc.SetTabBorders(rCursor, aSet);
                }
                break;

                case FN_UNO_TABLE_BORDER_DISTANCES:
                {
                    table::TableBorderDistances aTableBorderDistances;
                    if( !(aValue >>= aTableBorderDistances) ||
                        (!aTableBorderDistances.IsLeftDistanceValid &&
                        !aTableBorderDistances.IsRightDistanceValid &&
                        !aTableBorderDistances.IsTopDistanceValid &&
                        !aTableBorderDistances.IsBottomDistanceValid ))
                        break;

                    const sal_uInt16 nLeftDistance =   o3tl::toTwips(aTableBorderDistances.LeftDistance, o3tl::Length::mm100);
                    const sal_uInt16 nRightDistance =  o3tl::toTwips(aTableBorderDistances.RightDistance, o3tl::Length::mm100);
                    const sal_uInt16 nTopDistance =    o3tl::toTwips(aTableBorderDistances.TopDistance, o3tl::Length::mm100);
                    const sal_uInt16 nBottomDistance = o3tl::toTwips(aTableBorderDistances.BottomDistance, o3tl::Length::mm100);
                    SwDoc& rDoc = pFormat->GetDoc();
                    SwTable* pTable = SwTable::FindTable( pFormat );
                    SwTableLines &rLines = pTable->GetTabLines();
                    rDoc.GetIDocumentUndoRedo().StartUndo(SwUndoId::START, nullptr);
                    for(size_t i = 0; i < rLines.size(); ++i)
                    {
                        SwTableLine* pLine = rLines[i];
                        SwTableBoxes& rBoxes = pLine->GetTabBoxes();
                        for(size_t k = 0; k < rBoxes.size(); ++k)
                        {
                            SwTableBox* pBox = rBoxes[k];
                            const SwFrameFormat* pBoxFormat = pBox->GetFrameFormat();
                            const SvxBoxItem& rBox = pBoxFormat->GetBox();
                            if(
                                (aTableBorderDistances.IsLeftDistanceValid && nLeftDistance !=   rBox.GetDistance( SvxBoxItemLine::LEFT )) ||
                                (aTableBorderDistances.IsRightDistanceValid && nRightDistance !=  rBox.GetDistance( SvxBoxItemLine::RIGHT )) ||
                                (aTableBorderDistances.IsTopDistanceValid && nTopDistance !=    rBox.GetDistance( SvxBoxItemLine::TOP )) ||
                                (aTableBorderDistances.IsBottomDistanceValid && nBottomDistance != rBox.GetDistance( SvxBoxItemLine::BOTTOM )))
                            {
                                SvxBoxItem aSetBox( rBox );
                                SwFrameFormat* pSetBoxFormat = pBox->ClaimFrameFormat();
                                if( aTableBorderDistances.IsLeftDistanceValid )
                                    aSetBox.SetDistance( nLeftDistance, SvxBoxItemLine::LEFT );
                                if( aTableBorderDistances.IsRightDistanceValid )
                                    aSetBox.SetDistance( nRightDistance, SvxBoxItemLine::RIGHT );
                                if( aTableBorderDistances.IsTopDistanceValid )
                                    aSetBox.SetDistance( nTopDistance, SvxBoxItemLine::TOP );
                                if( aTableBorderDistances.IsBottomDistanceValid )
                                    aSetBox.SetDistance( nBottomDistance, SvxBoxItemLine::BOTTOM );
                                rDoc.SetAttr( aSetBox, *pSetBoxFormat );
                            }
                        }
                    }
                    rDoc.GetIDocumentUndoRedo().EndUndo(SwUndoId::END, nullptr);
                }
                break;

                case FN_UNO_TABLE_COLUMN_SEPARATORS:
                {
                    UnoActionContext aContext(&pFormat->GetDoc());
                    SwTable* pTable = SwTable::FindTable( pFormat );
                    lcl_SetTableSeparators(aValue, pTable, pTable->GetTabLines()[0]->GetTabBoxes()[0], false, pFormat->GetDoc());
                }
                break;

                case FN_UNO_TABLE_COLUMN_RELATIVE_SUM:/*_readonly_*/ break;

                case FN_UNO_TABLE_TEMPLATE_NAME:
                {
                    SwTable* pTable = SwTable::FindTable(pFormat);
                    OUString sProgName;
                    if (!(aValue >>= sProgName))
                        break;
                    UIName sName;
                    SwStyleNameMapper::FillUIName(ProgName(sProgName), sName, SwGetPoolIdFromName::TableStyle);
                    pTable->SetTableStyleName(TableStyleName(sName.toString()));
                    SwDoc& rDoc = pFormat->GetDoc();
                    if (SwFEShell* pFEShell = rDoc.GetDocShell()->GetFEShell())
                        pFEShell->UpdateTableStyleFormatting(pTable->GetTableNode());
                }
                break;

                default:
                {
                    SwAttrSet aSet(pFormat->GetAttrSet());
                    SfxItemPropertySet::setPropertyValue(*pEntry, aValue, aSet);
                    pFormat->GetDoc().SetAttr(aSet, *pFormat);
                }
            }
        }
    }
    else if (m_pImpl->IsDescriptor())
    {
        m_pImpl->m_pTableProps->SetProperty(pEntry->nWID, pEntry->nMemberId, aValue);
    }
    else
        throw uno::RuntimeException();
}

uno::Any SwXTextTable::getPropertyValue(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;
    uno::Any aRet;
    SwFrameFormat* pFormat = GetFrameFormat();
    const SfxItemPropertyMapEntry* pEntry =
            m_pImpl->m_pPropSet->getPropertyMap().getByName(rPropertyName);

    if (!pEntry)
        throw beans::UnknownPropertyException("Unknown property: " + rPropertyName, getXWeak() );

    if(pFormat)
    {
        if(0xBF == pEntry->nMemberId)
        {
            aRet = lcl_GetSpecialProperty(pFormat, pEntry );
        }
        else
        {
            switch(pEntry->nWID)
            {
                case FN_UNO_TABLE_NAME:
                {
                    aRet <<= getName();
                }
                break;

                case  FN_UNO_ANCHOR_TYPES:
                case  FN_UNO_TEXT_WRAP:
                case  FN_UNO_ANCHOR_TYPE:
                    (void)::sw::GetDefaultTextContentValue(
                            aRet, u"", pEntry->nWID);
                break;

                case FN_UNO_RANGE_ROW_LABEL:
                {
                    aRet <<= m_pImpl->m_bFirstRowAsLabel;
                }
                break;

                case FN_UNO_RANGE_COL_LABEL:
                    aRet <<= m_pImpl->m_bFirstColumnAsLabel;
                break;

                case FN_UNO_TABLE_BORDER:
                case FN_UNO_TABLE_BORDER2:
                {
                    SwDoc& rDoc = pFormat->GetDoc();
                    // tables without layout (invisible header/footer?)
                    if(!lcl_FormatTable(pFormat))
                        break;
                    SwTable* pTable = SwTable::FindTable( pFormat );
                    SwTableLines &rLines = pTable->GetTabLines();

                    const SwTableBox* pTLBox = lcl_FindCornerTableBox(rLines, true);
                    const SwStartNode* pSttNd = pTLBox->GetSttNd();
                    SwPosition aPos(*pSttNd);
                    // set cursor to top left cell
                    auto pUnoCursor(rDoc.CreateUnoCursor(aPos, true));
                    pUnoCursor->Move( fnMoveForward, GoInNode );
                    pUnoCursor->SetRemainInSection( false );

                    const SwTableBox* pBRBox = lcl_FindCornerTableBox(rLines, false);
                    pUnoCursor->SetMark();
                    const SwStartNode* pLastNd = pBRBox->GetSttNd();
                    pUnoCursor->GetPoint()->Assign( *pLastNd );

                    pUnoCursor->Move( fnMoveForward, GoInNode );
                    SwUnoTableCursor& rCursor = dynamic_cast<SwUnoTableCursor&>(*pUnoCursor);
                    // HACK: remove pending actions for selecting old style tables
                    UnoActionRemoveContext aRemoveContext(rCursor);
                    rCursor.MakeBoxSels();

                    SfxItemSetFixed<RES_BOX, RES_BOX,
                                    SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER>
                        aSet(rDoc.GetAttrPool());
                    aSet.Put(SvxBoxInfoItem( SID_ATTR_BORDER_INNER ));
                    SwDoc::GetTabBorders(rCursor, aSet);
                    const SvxBoxInfoItem& rBoxInfoItem = aSet.Get(SID_ATTR_BORDER_INNER);
                    const SvxBoxItem& rBox = aSet.Get(RES_BOX);

                    if (FN_UNO_TABLE_BORDER == pEntry->nWID)
                    {
                        table::TableBorder aTableBorder;
                        aTableBorder.TopLine                = SvxBoxItem::SvxLineToLine(rBox.GetTop(), true);
                        aTableBorder.IsTopLineValid         = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::TOP);
                        aTableBorder.BottomLine             = SvxBoxItem::SvxLineToLine(rBox.GetBottom(), true);
                        aTableBorder.IsBottomLineValid      = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::BOTTOM);
                        aTableBorder.LeftLine               = SvxBoxItem::SvxLineToLine(rBox.GetLeft(), true);
                        aTableBorder.IsLeftLineValid        = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::LEFT);
                        aTableBorder.RightLine              = SvxBoxItem::SvxLineToLine(rBox.GetRight(), true);
                        aTableBorder.IsRightLineValid       = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::RIGHT );
                        aTableBorder.HorizontalLine         = SvxBoxItem::SvxLineToLine(rBoxInfoItem.GetHori(), true);
                        aTableBorder.IsHorizontalLineValid  = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::HORI);
                        aTableBorder.VerticalLine           = SvxBoxItem::SvxLineToLine(rBoxInfoItem.GetVert(), true);
                        aTableBorder.IsVerticalLineValid    = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::VERT);
                        aTableBorder.Distance               = convertTwipToMm100(rBox.GetSmallestDistance());
                        aTableBorder.IsDistanceValid        = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::DISTANCE);
                        aRet <<= aTableBorder;
                    }
                    else
                    {
                        table::TableBorder2 aTableBorder;
                        aTableBorder.TopLine                = SvxBoxItem::SvxLineToLine(rBox.GetTop(), true);
                        aTableBorder.IsTopLineValid         = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::TOP);
                        aTableBorder.BottomLine             = SvxBoxItem::SvxLineToLine(rBox.GetBottom(), true);
                        aTableBorder.IsBottomLineValid      = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::BOTTOM);
                        aTableBorder.LeftLine               = SvxBoxItem::SvxLineToLine(rBox.GetLeft(), true);
                        aTableBorder.IsLeftLineValid        = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::LEFT);
                        aTableBorder.RightLine              = SvxBoxItem::SvxLineToLine(rBox.GetRight(), true);
                        aTableBorder.IsRightLineValid       = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::RIGHT );
                        aTableBorder.HorizontalLine         = SvxBoxItem::SvxLineToLine(rBoxInfoItem.GetHori(), true);
                        aTableBorder.IsHorizontalLineValid  = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::HORI);
                        aTableBorder.VerticalLine           = SvxBoxItem::SvxLineToLine(rBoxInfoItem.GetVert(), true);
                        aTableBorder.IsVerticalLineValid    = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::VERT);
                        aTableBorder.Distance               = convertTwipToMm100(rBox.GetSmallestDistance());
                        aTableBorder.IsDistanceValid        = rBoxInfoItem.IsValid(SvxBoxInfoItemValidFlags::DISTANCE);
                        aRet <<= aTableBorder;
                    }
                }
                break;

                case FN_UNO_TABLE_BORDER_DISTANCES :
                {
                    table::TableBorderDistances aTableBorderDistances( 0, true, 0, true, 0, true, 0, true ) ;
                    SwTable* pTable = SwTable::FindTable( pFormat );
                    const SwTableLines &rLines = pTable->GetTabLines();
                    bool bFirst = true;
                    sal_uInt16 nLeftDistance = 0;
                    sal_uInt16 nRightDistance = 0;
                    sal_uInt16 nTopDistance = 0;
                    sal_uInt16 nBottomDistance = 0;

                    for(size_t i = 0; i < rLines.size(); ++i)
                    {
                        const SwTableLine* pLine = rLines[i];
                        const SwTableBoxes& rBoxes = pLine->GetTabBoxes();
                        for(size_t k = 0; k < rBoxes.size(); ++k)
                        {
                            const SwTableBox* pBox = rBoxes[k];
                            SwFrameFormat* pBoxFormat = pBox->GetFrameFormat();
                            const SvxBoxItem& rBox = pBoxFormat->GetBox();
                            if( bFirst )
                            {
                                nLeftDistance =     convertTwipToMm100( rBox.GetDistance( SvxBoxItemLine::LEFT   ));
                                nRightDistance =    convertTwipToMm100( rBox.GetDistance( SvxBoxItemLine::RIGHT  ));
                                nTopDistance =      convertTwipToMm100( rBox.GetDistance( SvxBoxItemLine::TOP    ));
                                nBottomDistance =   convertTwipToMm100( rBox.GetDistance( SvxBoxItemLine::BOTTOM ));
                                bFirst = false;
                            }
                            else
                            {
                                if( aTableBorderDistances.IsLeftDistanceValid &&
                                    nLeftDistance != convertTwipToMm100( rBox.GetDistance( SvxBoxItemLine::LEFT   )))
                                    aTableBorderDistances.IsLeftDistanceValid = false;
                                if( aTableBorderDistances.IsRightDistanceValid &&
                                    nRightDistance != convertTwipToMm100( rBox.GetDistance( SvxBoxItemLine::RIGHT   )))
                                    aTableBorderDistances.IsRightDistanceValid = false;
                                if( aTableBorderDistances.IsTopDistanceValid &&
                                    nTopDistance != convertTwipToMm100( rBox.GetDistance( SvxBoxItemLine::TOP   )))
                                    aTableBorderDistances.IsTopDistanceValid = false;
                                if( aTableBorderDistances.IsBottomDistanceValid &&
                                    nBottomDistance != convertTwipToMm100( rBox.GetDistance( SvxBoxItemLine::BOTTOM   )))
                                    aTableBorderDistances.IsBottomDistanceValid = false;
                            }

                        }
                        if( !aTableBorderDistances.IsLeftDistanceValid &&
                                !aTableBorderDistances.IsRightDistanceValid &&
                                !aTableBorderDistances.IsTopDistanceValid &&
                                !aTableBorderDistances.IsBottomDistanceValid )
                            break;
                    }
                    if( aTableBorderDistances.IsLeftDistanceValid)
                        aTableBorderDistances.LeftDistance = nLeftDistance;
                    if( aTableBorderDistances.IsRightDistanceValid)
                        aTableBorderDistances.RightDistance  = nRightDistance;
                    if( aTableBorderDistances.IsTopDistanceValid)
                        aTableBorderDistances.TopDistance    = nTopDistance;
                    if( aTableBorderDistances.IsBottomDistanceValid)
                        aTableBorderDistances.BottomDistance = nBottomDistance;

                    aRet <<= aTableBorderDistances;
                }
                break;

                case FN_UNO_TABLE_COLUMN_SEPARATORS:
                {
                    SwTable* pTable = SwTable::FindTable( pFormat );
                    lcl_GetTableSeparators(aRet, pTable, pTable->GetTabLines()[0]->GetTabBoxes()[0], false);
                }
                break;

                case FN_UNO_TABLE_COLUMN_RELATIVE_SUM:
                    aRet <<= sal_Int16(UNO_TABLE_COLUMN_SUM);
                break;

                case RES_ANCHOR:
                    // AnchorType is readonly and might be void (no return value)
                break;

                case FN_UNO_TEXT_SECTION:
                {
                    SwTable* pTable = SwTable::FindTable( pFormat );
                    SwTableNode* pTableNode = pTable->GetTableNode();
                    SwSectionNode* pSectionNode =  pTableNode->FindSectionNode();
                    if(pSectionNode)
                    {
                        SwSection& rSect = pSectionNode->GetSection();
                        rtl::Reference< SwXTextSection > xSect =
                                        SwXTextSection::CreateXTextSection(rSect.GetFormat());
                        aRet <<= uno::Reference< text::XTextSection >(xSect);
                    }
                }
                break;

                case FN_UNO_TABLE_TEMPLATE_NAME:
                {
                    SwTable* pTable = SwTable::FindTable(pFormat);
                    ProgName sName;
                    SwStyleNameMapper::FillProgName(UIName(pTable->GetTableStyleName().toString()), sName, SwGetPoolIdFromName::TableStyle);
                    aRet <<= sName.toString();
                }
                break;

                default:
                {
                    const SwAttrSet& rSet = pFormat->GetAttrSet();
                    SfxItemPropertySet::getPropertyValue(*pEntry, rSet, aRet);
                }
            }
        }
    }
    else if (m_pImpl->IsDescriptor())
    {
        if (const uno::Any* pAny = m_pImpl->m_pTableProps->GetProperty(pEntry->nWID, pEntry->nMemberId))
            aRet = *pAny;
        else
            throw lang::IllegalArgumentException();
    }
    else
        throw uno::RuntimeException();
    return aRet;
}

void SwXTextTable::addPropertyChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

void SwXTextTable::removePropertyChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

void SwXTextTable::addVetoableChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

void SwXTextTable::removeVetoableChangeListener(const OUString& /*rPropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*xListener*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

OUString SwXTextTable::getName()
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = GetFrameFormat();
    if (!pFormat && !m_pImpl->IsDescriptor())
        throw uno::RuntimeException();
    if(pFormat)
    {
        return pFormat->GetName().toString();
    }
    return m_pImpl->m_sTableName;
}

void SwXTextTable::setName(const OUString& rName)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFormat = GetFrameFormat();
    if ((!pFormat && !m_pImpl->IsDescriptor()) ||
       rName.isEmpty() ||
       rName.indexOf('.')>=0 ||
       rName.indexOf(' ')>=0 )
        throw uno::RuntimeException();

    if(pFormat)
    {
        const UIName aOldName( pFormat->GetName() );
        const sw::TableFrameFormats* pFrameFormats = pFormat->GetDoc().GetTableFrameFormats();
        for (size_t i = pFrameFormats->size(); i;)
        {
            const SwTableFormat* pTmpFormat = (*pFrameFormats)[--i];
            if( !pTmpFormat->IsDefault() &&
                pTmpFormat->GetName() == rName &&
                            pFormat->GetDoc().IsUsed( *pTmpFormat ))
            {
                throw uno::RuntimeException();
            }
        }

        pFormat->SetFormatName( UIName(rName) );

        SwStartNode *pStNd;
        SwNodeIndex aIdx( *pFormat->GetDoc().GetNodes().GetEndOfAutotext().StartOfSectionNode(), 1 );
        while ( nullptr != (pStNd = aIdx.GetNode().GetStartNode()) )
        {
            ++aIdx;
            SwNode *const pNd = & aIdx.GetNode();
            if ( pNd->IsOLENode() &&
                aOldName == static_cast<const SwOLENode*>(pNd)->GetChartTableName() )
            {
                static_cast<SwOLENode*>(pNd)->SetChartTableName( UIName(rName) );

                SwTable* pTable = SwTable::FindTable( pFormat );
                //TL_CHART2: chart needs to be notified about name changes
                pFormat->GetDoc().UpdateCharts( pTable->GetFrameFormat()->GetName() );
            }
            aIdx.Assign( *pStNd->EndOfSectionNode(), + 1 );
        }
        pFormat->GetDoc().getIDocumentState().SetModified();
    }
    else
        m_pImpl->m_sTableName = rName;
}

sal_uInt16 SwXTextTable::Impl::GetRowCount()
{
    sal_uInt16 nRet = 0;
    SwFrameFormat* pFormat = GetFrameFormat();
    if(pFormat)
    {
        SwTable* pTable = SwTable::FindTable( pFormat );
        if(!pTable->IsTableComplex())
        {
            nRet = pTable->GetTabLines().size();
        }
    }
    return nRet;
}

sal_uInt16 SwXTextTable::Impl::GetColumnCount()
{
    SwFrameFormat* pFormat = GetFrameFormat();
    sal_uInt16 nRet = 0;
    if(pFormat)
    {
        SwTable* pTable = SwTable::FindTable( pFormat );
        if(!pTable->IsTableComplex())
        {
            SwTableLines& rLines = pTable->GetTabLines();
            SwTableLine* pLine = rLines.front();
            nRet = pLine->GetTabBoxes().size();
        }
    }
    return nRet;
}

void SwXTextTable::Impl::Notify(const SfxHint& rHint)
{
    if(rHint.GetId() == SfxHintId::Dying)
    {
        m_pFrameFormat = nullptr;
        EndListeningAll();
    }
    std::unique_lock aGuard(m_Mutex);
    if (m_EventListeners.getLength(aGuard) == 0 && m_ChartListeners.getLength(aGuard) == 0)
        return;
    uno::Reference<uno::XInterface> const xThis(m_wThis);
    // fdo#72695: if UNO object is already dead, don't revive it with event
    if (!xThis)
        return;
    if(!m_pFrameFormat)
    {
        lang::EventObject const ev(xThis);
        m_EventListeners.disposeAndClear(aGuard, ev);
        m_ChartListeners.disposeAndClear(aGuard, ev);
    }
    else
    {
        lcl_SendChartEvent(aGuard, xThis, m_ChartListeners);
    }
}

OUString SAL_CALL SwXTextTable::getImplementationName()
    { return u"SwXTextTable"_ustr; }

sal_Bool SwXTextTable::supportsService(const OUString& rServiceName)
    { return cppu::supportsService(this, rServiceName); }

uno::Sequence<OUString> SwXTextTable::getSupportedServiceNames()
{
    return {
        u"com.sun.star.document.LinkTarget"_ustr,
        u"com.sun.star.text.TextTable"_ustr,
        u"com.sun.star.text.TextContent"_ustr,
        u"com.sun.star.text.TextSortable"_ustr };
}


class SwXCellRange::Impl
    : public SvtListener
{
private:
    SwFrameFormat* m_pFrameFormat;

public:
    unotools::WeakReference<SwXCellRange> m_wThis;
    std::mutex m_Mutex; // just for OInterfaceContainerHelper4
    ::comphelper::OInterfaceContainerHelper4<chart::XChartDataChangeEventListener> m_ChartListeners;

    sw::UnoCursorPointer m_pTableCursor;

    SwRangeDescriptor           m_RangeDescriptor;
    const SfxItemPropertySet*   m_pPropSet;

    bool m_bFirstRowAsLabel;
    bool m_bFirstColumnAsLabel;

    Impl(sw::UnoCursorPointer const& pCursor, SwFrameFormat& rFrameFormat, SwRangeDescriptor const& rDesc)
        : m_pFrameFormat(&rFrameFormat)
        , m_pTableCursor(pCursor)
        , m_RangeDescriptor(rDesc)
        , m_pPropSet(aSwMapProvider.GetPropertySet(PROPERTY_MAP_TABLE_RANGE))
        , m_bFirstRowAsLabel(false)
        , m_bFirstColumnAsLabel(false)
    {
        StartListening(rFrameFormat.GetNotifier());
        m_RangeDescriptor.Normalize();
    }

    SwFrameFormat* GetFrameFormat()
    {
        return m_pFrameFormat;
    }

    std::tuple<sal_uInt32, sal_uInt32, sal_uInt32, sal_uInt32> GetLabelCoordinates(bool bRow);

    uno::Sequence<OUString> GetLabelDescriptions(SwXCellRange & rThis, bool bRow);

    void SetLabelDescriptions(SwXCellRange & rThis,
            const css::uno::Sequence<OUString>& rDesc, bool bRow);

    sal_Int32 GetRowCount() const;
    sal_Int32 GetColumnCount() const;

    virtual void Notify(const SfxHint& ) override;

};

OUString SwXCellRange::getImplementationName()
    { return u"SwXCellRange"_ustr; }

sal_Bool SwXCellRange::supportsService(const OUString& rServiceName)
    { return cppu::supportsService(this, rServiceName); }

uno::Sequence<OUString> SwXCellRange::getSupportedServiceNames()
{
    return {
        u"com.sun.star.text.CellRange"_ustr,
        u"com.sun.star.style.CharacterProperties"_ustr,
        u"com.sun.star.style.CharacterPropertiesAsian"_ustr,
        u"com.sun.star.style.CharacterPropertiesComplex"_ustr,
        u"com.sun.star.style.ParagraphProperties"_ustr,
        u"com.sun.star.style.ParagraphPropertiesAsian"_ustr,
        u"com.sun.star.style.ParagraphPropertiesComplex"_ustr };
}

SwXCellRange::SwXCellRange(sw::UnoCursorPointer const& pCursor,
        SwFrameFormat& rFrameFormat, SwRangeDescriptor const & rDesc)
    : m_pImpl(new Impl(pCursor, rFrameFormat, rDesc))
{
}

SwXCellRange::~SwXCellRange()
{
}

rtl::Reference<SwXCellRange> SwXCellRange::CreateXCellRange(
        sw::UnoCursorPointer const& pCursor, SwFrameFormat& rFrameFormat,
        SwRangeDescriptor const & rDesc)
{
    rtl::Reference<SwXCellRange> pCellRange(new SwXCellRange(pCursor, rFrameFormat, rDesc));
    // need a permanent Reference to initialize m_wThis
    pCellRange->m_pImpl->m_wThis = pCellRange.get();
    return pCellRange;
}

void SwXCellRange::SetLabels(bool bFirstRowAsLabel, bool bFirstColumnAsLabel)
{
    m_pImpl->m_bFirstRowAsLabel = bFirstRowAsLabel;
    m_pImpl->m_bFirstColumnAsLabel = bFirstColumnAsLabel;
}

std::vector< uno::Reference< table::XCell > > SwXCellRange::GetCells()
{
    SwFrameFormat *const pFormat = m_pImpl->GetFrameFormat();
    const sal_Int32 nRowCount(m_pImpl->GetRowCount());
    const sal_Int32 nColCount(m_pImpl->GetColumnCount());
    std::vector< uno::Reference< table::XCell > > vResult;
    vResult.reserve(static_cast<size_t>(nRowCount)*static_cast<size_t>(nColCount));
    for(sal_Int32 nRow = 0; nRow < nRowCount; ++nRow)
        for(sal_Int32 nCol = 0; nCol < nColCount; ++nCol)
            vResult.emplace_back(lcl_CreateXCell(pFormat, m_pImpl->m_RangeDescriptor.nLeft + nCol, m_pImpl->m_RangeDescriptor.nTop + nRow));
    return vResult;
}

uno::Reference<table::XCell> SAL_CALL
SwXCellRange::getCellByPosition(sal_Int32 nColumn, sal_Int32 nRow)
{
    SolarMutexGuard aGuard;
    rtl::Reference< SwXCell >  pXCell;
    SwFrameFormat *const pFormat = m_pImpl->GetFrameFormat();
    if(pFormat)
    {
        if(nColumn >= 0 && nRow >= 0 &&
             m_pImpl->GetColumnCount() > nColumn && m_pImpl->GetRowCount() > nRow )
        {
            pXCell = lcl_CreateXCell(pFormat,
                    m_pImpl->m_RangeDescriptor.nLeft + nColumn,
                    m_pImpl->m_RangeDescriptor.nTop + nRow);
        }
    }
    if(!pXCell.is())
        throw lang::IndexOutOfBoundsException();
    return pXCell;
}

uno::Reference<table::XCellRange> SAL_CALL
SwXCellRange::getCellRangeByPosition(
        sal_Int32 nLeft, sal_Int32 nTop, sal_Int32 nRight, sal_Int32 nBottom)
{
    SolarMutexGuard aGuard;
    rtl::Reference< SwXCellRange >  aRet;
    SwFrameFormat *const pFormat = m_pImpl->GetFrameFormat();
    if  (pFormat && m_pImpl->GetColumnCount() > nRight
        && m_pImpl->GetRowCount() > nBottom &&
        nLeft <= nRight && nTop <= nBottom
        && nLeft >= 0 && nRight >= 0 && nTop >= 0 && nBottom >= 0 )
    {
        SwTable* pTable = SwTable::FindTable( pFormat );
        if(!pTable->IsTableComplex())
        {
            SwRangeDescriptor aNewDesc;
            aNewDesc.nTop    = nTop + m_pImpl->m_RangeDescriptor.nTop;
            aNewDesc.nBottom = nBottom + m_pImpl->m_RangeDescriptor.nTop;
            aNewDesc.nLeft   = nLeft + m_pImpl->m_RangeDescriptor.nLeft;
            aNewDesc.nRight  = nRight + m_pImpl->m_RangeDescriptor.nLeft;
            aNewDesc.Normalize();
            const OUString sTLName = sw_GetCellName(aNewDesc.nLeft, aNewDesc.nTop);
            const OUString sBRName = sw_GetCellName(aNewDesc.nRight, aNewDesc.nBottom);
            const SwTableBox* pTLBox = pTable->GetTableBox( sTLName );
            if(pTLBox)
            {
                const SwStartNode* pSttNd = pTLBox->GetSttNd();
                SwPosition aPos(*pSttNd);
                // set cursor in the upper-left cell of the range
                std::shared_ptr<SwUnoCursor> xUnoCursor(pFormat->GetDoc().CreateUnoCursor(aPos, true));
                xUnoCursor->Move( fnMoveForward, GoInNode );
                xUnoCursor->SetRemainInSection( false );
                const SwTableBox* pBRBox = pTable->GetTableBox( sBRName );
                if(pBRBox)
                {
                    xUnoCursor->SetMark();
                    xUnoCursor->GetPoint()->Assign( *pBRBox->GetSttNd() );
                    xUnoCursor->Move( fnMoveForward, GoInNode );
                    SwUnoTableCursor& rCursor = dynamic_cast<SwUnoTableCursor&>(*xUnoCursor);
                    // HACK: remove pending actions for selecting old style tables
                    UnoActionRemoveContext aRemoveContext(rCursor);
                    rCursor.MakeBoxSels();
                    // xUnoCursor will be provided and will not be deleted
                    aRet = SwXCellRange::CreateXCellRange(std::move(xUnoCursor), *pFormat, aNewDesc).get();
                }
            }
        }
    }
    if(!aRet.is())
        throw lang::IndexOutOfBoundsException();
    return aRet;
}

uno::Reference<table::XCellRange> SAL_CALL
SwXCellRange::getCellRangeByName(const OUString& rRange)
{
    SolarMutexGuard aGuard;
    sal_Int32 nPos = 0;
    const OUString sTLName(rRange.getToken(0, ':', nPos));
    const OUString sBRName(rRange.getToken(0, ':', nPos));
    if(sTLName.isEmpty() || sBRName.isEmpty())
        throw uno::RuntimeException();
    SwRangeDescriptor aDesc;
    aDesc.nTop = aDesc.nLeft = aDesc.nBottom = aDesc.nRight = -1;
    SwXTextTable::GetCellPosition( sTLName, aDesc.nLeft, aDesc.nTop );
    SwXTextTable::GetCellPosition( sBRName, aDesc.nRight, aDesc.nBottom );
    aDesc.Normalize();
    return getCellRangeByPosition(
                aDesc.nLeft - m_pImpl->m_RangeDescriptor.nLeft,
                aDesc.nTop - m_pImpl->m_RangeDescriptor.nTop,
                aDesc.nRight - m_pImpl->m_RangeDescriptor.nLeft,
                aDesc.nBottom - m_pImpl->m_RangeDescriptor.nTop);
}

uno::Reference< beans::XPropertySetInfo >  SwXCellRange::getPropertySetInfo()
{
    static uno::Reference<beans::XPropertySetInfo> xRef = m_pImpl->m_pPropSet->getPropertySetInfo();
    return xRef;
}

void SAL_CALL
SwXCellRange::setPropertyValue(const OUString& rPropertyName, const uno::Any& aValue)
{
    SolarMutexGuard aGuard;
    SwFrameFormat *const pFormat = m_pImpl->GetFrameFormat();
    if(!pFormat)
        return;

    const SfxItemPropertyMapEntry *const pEntry =
            m_pImpl->m_pPropSet->getPropertyMap().getByName(rPropertyName);
    if(!pEntry)
        throw beans::UnknownPropertyException("Unknown property: " + rPropertyName, getXWeak() );

    if ( pEntry->nFlags & beans::PropertyAttribute::READONLY)
        throw beans::PropertyVetoException("Property is read-only: " + rPropertyName, getXWeak() );

    SwDoc& rDoc = m_pImpl->m_pTableCursor->GetDoc();
    SwUnoTableCursor& rCursor(dynamic_cast<SwUnoTableCursor&>(*m_pImpl->m_pTableCursor));
    {
        // HACK: remove pending actions for selecting old style tables
        UnoActionRemoveContext aRemoveContext(rCursor);
    }
    rCursor.MakeBoxSels();
    switch(pEntry->nWID )
    {
        case FN_UNO_TABLE_CELL_BACKGROUND:
        {
            std::unique_ptr<SfxPoolItem> aBrush(std::make_unique<SvxBrushItem>(RES_BACKGROUND));
            SwDoc::GetBoxAttr(*m_pImpl->m_pTableCursor, aBrush);
            aBrush->PutValue(aValue, pEntry->nMemberId);
            rDoc.SetBoxAttr(*m_pImpl->m_pTableCursor, *aBrush);

        }
        break;
        case RES_BOX :
        {
            SfxItemSetFixed<RES_BOX, RES_BOX,
                            SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER>
                aSet(rDoc.GetAttrPool());
            SvxBoxInfoItem aBoxInfo( SID_ATTR_BORDER_INNER );
            aBoxInfo.SetValid(SvxBoxInfoItemValidFlags::ALL, false);
            SvxBoxInfoItemValidFlags nValid = SvxBoxInfoItemValidFlags::NONE;
            switch(pEntry->nMemberId & ~CONVERT_TWIPS)
            {
                case  LEFT_BORDER :             nValid = SvxBoxInfoItemValidFlags::LEFT; break;
                case  RIGHT_BORDER:             nValid = SvxBoxInfoItemValidFlags::RIGHT; break;
                case  TOP_BORDER  :             nValid = SvxBoxInfoItemValidFlags::TOP; break;
                case  BOTTOM_BORDER:            nValid = SvxBoxInfoItemValidFlags::BOTTOM; break;
                case  LEFT_BORDER_DISTANCE :
                case  RIGHT_BORDER_DISTANCE:
                case  TOP_BORDER_DISTANCE  :
                case  BOTTOM_BORDER_DISTANCE:
                    nValid = SvxBoxInfoItemValidFlags::DISTANCE;
                break;
            }
            aBoxInfo.SetValid(nValid);

            aSet.Put(aBoxInfo);
            SwDoc::GetTabBorders(rCursor, aSet);

            aSet.Put(aBoxInfo);
            SvxBoxItem aBoxItem(aSet.Get(RES_BOX));
            static_cast<SfxPoolItem&>(aBoxItem).PutValue(aValue, pEntry->nMemberId);
            aSet.Put(aBoxItem);
            rDoc.SetTabBorders(*m_pImpl->m_pTableCursor, aSet);
        }
        break;
        case RES_BOXATR_FORMAT:
        {
            SfxUInt32Item aNumberFormat(RES_BOXATR_FORMAT);
            static_cast<SfxPoolItem&>(aNumberFormat).PutValue(aValue, 0);
            rDoc.SetBoxAttr(rCursor, aNumberFormat);
        }
        break;
        case FN_UNO_RANGE_ROW_LABEL:
        {
            bool bTmp = *o3tl::doAccess<bool>(aValue);
            if (m_pImpl->m_bFirstRowAsLabel != bTmp)
            {
                lcl_SendChartEvent(m_pImpl->m_Mutex, *this, m_pImpl->m_ChartListeners);
                m_pImpl->m_bFirstRowAsLabel = bTmp;
            }
        }
        break;
        case FN_UNO_RANGE_COL_LABEL:
        {
            bool bTmp = *o3tl::doAccess<bool>(aValue);
            if (m_pImpl->m_bFirstColumnAsLabel != bTmp)
            {
                lcl_SendChartEvent(m_pImpl->m_Mutex, *this, m_pImpl->m_ChartListeners);
                m_pImpl->m_bFirstColumnAsLabel = bTmp;
            }
        }
        break;
        case RES_VERT_ORIENT:
        {
            sal_Int16 nAlign = -1;
            aValue >>= nAlign;
            if( nAlign >= text::VertOrientation::NONE && nAlign <= text::VertOrientation::BOTTOM)
                rDoc.SetBoxAlign( rCursor, nAlign );
        }
        break;
        default:
        {
            SfxItemSet aItemSet( rDoc.GetAttrPool(), pEntry->nWID, pEntry->nWID );
            SwUnoCursorHelper::GetCursorAttr(rCursor.GetSelRing(),
                    aItemSet);

            if (!SwUnoCursorHelper::SetCursorPropertyValue(
                    *pEntry, aValue, rCursor.GetSelRing(), aItemSet))
            {
                SfxItemPropertySet::setPropertyValue(*pEntry, aValue, aItemSet);
            }
            SwUnoCursorHelper::SetCursorAttr(rCursor.GetSelRing(),
                    aItemSet, SetAttrMode::DEFAULT, true);
        }
    }
}

uno::Any SAL_CALL SwXCellRange::getPropertyValue(const OUString& rPropertyName)
{
    SolarMutexGuard aGuard;
    uno::Any aRet;
    SwFrameFormat *const pFormat = m_pImpl->GetFrameFormat();
    if(pFormat)
    {
        const SfxItemPropertyMapEntry *const pEntry =
            m_pImpl->m_pPropSet->getPropertyMap().getByName(rPropertyName);
        if(!pEntry)
           throw beans::UnknownPropertyException("Unknown property: " + rPropertyName, getXWeak() );

        switch(pEntry->nWID )
        {
            case FN_UNO_TABLE_CELL_BACKGROUND:
            {
                std::unique_ptr<SfxPoolItem> aBrush(std::make_unique<SvxBrushItem>(RES_BACKGROUND));
                if (SwDoc::GetBoxAttr(*m_pImpl->m_pTableCursor, aBrush))
                    aBrush->QueryValue(aRet, pEntry->nMemberId);

            }
            break;
            case RES_BOX :
            {
                SwDoc& rDoc = m_pImpl->m_pTableCursor->GetDoc();
                SfxItemSetFixed<RES_BOX, RES_BOX,
                                SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER>
                    aSet(rDoc.GetAttrPool());
                aSet.Put(SvxBoxInfoItem( SID_ATTR_BORDER_INNER ));
                SwDoc::GetTabBorders(*m_pImpl->m_pTableCursor, aSet);
                const SvxBoxItem& rBoxItem = aSet.Get(RES_BOX);
                rBoxItem.QueryValue(aRet, pEntry->nMemberId);
            }
            break;
            case RES_BOXATR_FORMAT:
                OSL_FAIL("not implemented");
            break;
            case FN_UNO_PARA_STYLE:
            {
                SwFormatColl *const pTmpFormat =
                    SwUnoCursorHelper::GetCurTextFormatColl(*m_pImpl->m_pTableCursor, false);
                ProgName sRet;
                if (pTmpFormat)
                {
                    SwStyleNameMapper::FillProgName(pTmpFormat->GetName(), sRet, SwGetPoolIdFromName::TxtColl);
                }
                aRet <<= sRet.toString();
            }
            break;
            case FN_UNO_RANGE_ROW_LABEL:
                aRet <<= m_pImpl->m_bFirstRowAsLabel;
            break;
            case FN_UNO_RANGE_COL_LABEL:
                aRet <<= m_pImpl->m_bFirstColumnAsLabel;
            break;
            case RES_VERT_ORIENT:
            {
                std::unique_ptr<SfxPoolItem> aVertOrient(
                    std::make_unique<SwFormatVertOrient>(RES_VERT_ORIENT));
                if (SwDoc::GetBoxAttr(*m_pImpl->m_pTableCursor, aVertOrient))
                {
                    aVertOrient->QueryValue( aRet, pEntry->nMemberId );
                }
            }
            break;
            default:
            {
                SfxItemSetFixed<
                        RES_CHRATR_BEGIN, RES_FRMATR_END - 1,
                        RES_UNKNOWNATR_CONTAINER,
                            RES_UNKNOWNATR_CONTAINER>
                    aSet(m_pImpl->m_pTableCursor->GetDoc().GetAttrPool());
                // first look at the attributes of the cursor
                SwUnoTableCursor& rCursor =
                    dynamic_cast<SwUnoTableCursor&>(*m_pImpl->m_pTableCursor);
                SwUnoCursorHelper::GetCursorAttr(rCursor.GetSelRing(), aSet);
                SfxItemPropertySet::getPropertyValue(*pEntry, aSet, aRet);
            }
        }

    }
    return aRet;
}

void SwXCellRange::addPropertyChangeListener(const OUString& /*PropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*aListener*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

void SwXCellRange::removePropertyChangeListener(const OUString& /*PropertyName*/, const uno::Reference< beans::XPropertyChangeListener > & /*aListener*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

void SwXCellRange::addVetoableChangeListener(const OUString& /*PropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*aListener*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

void SwXCellRange::removeVetoableChangeListener(const OUString& /*PropertyName*/, const uno::Reference< beans::XVetoableChangeListener > & /*aListener*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

///@see SwXCellRange::getData
uno::Sequence<uno::Sequence<uno::Any>> SAL_CALL SwXCellRange::getDataArray()
{
    SolarMutexGuard aGuard;
    const sal_Int32 nRowCount = m_pImpl->GetRowCount();
    const sal_Int32 nColCount = m_pImpl->GetColumnCount();
    if(!nRowCount || !nColCount)
        throw uno::RuntimeException(u"Table too complex"_ustr, getXWeak());
    lcl_EnsureCoreConnected(m_pImpl->GetFrameFormat(), this);
    uno::Sequence< uno::Sequence< uno::Any > > aRowSeq(nRowCount);
    auto vCells(GetCells());
    auto pCurrentCell(vCells.begin());
    for(auto& rRow : asNonConstRange(aRowSeq))
    {
        rRow = uno::Sequence< uno::Any >(nColCount);
        for(auto& rCellAny : asNonConstRange(rRow))
        {
            auto pCell(static_cast<SwXCell*>(pCurrentCell->get()));
            if(!pCell)
                throw uno::RuntimeException(u"Table too complex"_ustr, getXWeak());
            rCellAny = pCell->GetAny();
            ++pCurrentCell;
        }
    }
    return aRowSeq;
}

///@see SwXCellRange::setData
void SAL_CALL SwXCellRange::setDataArray(const uno::Sequence< uno::Sequence< uno::Any > >& rArray)
{
    SolarMutexGuard aGuard;
    const sal_Int32 nRowCount = m_pImpl->GetRowCount();
    const sal_Int32 nColCount = m_pImpl->GetColumnCount();
    if(!nRowCount || !nColCount)
        throw uno::RuntimeException(u"Table too complex"_ustr, getXWeak());
    SwFrameFormat *const pFormat = m_pImpl->GetFrameFormat();
    if(!pFormat)
        return;
    if(rArray.getLength() != nRowCount)
        throw uno::RuntimeException("Row count mismatch. expected: " + OUString::number(nRowCount) + " got: " + OUString::number(rArray.getLength()), getXWeak());
    auto vCells(GetCells());
    auto pCurrentCell(vCells.begin());
    for(const auto& rColSeq : rArray)
    {
        if(rColSeq.getLength() != nColCount)
            throw uno::RuntimeException("Column count mismatch. expected: " + OUString::number(nColCount) + " got: " + OUString::number(rColSeq.getLength()), getXWeak());
        for(const auto& aValue : rColSeq)
        {
            auto pCell(static_cast<SwXCell*>(pCurrentCell->get()));
            if(!pCell || !pCell->GetTableBox())
                throw uno::RuntimeException(u"Box for cell missing"_ustr, getXWeak());
            if(aValue.isExtractableTo(cppu::UnoType<OUString>::get()))
                sw_setString(*pCell, aValue.get<OUString>());
            else if(aValue.isExtractableTo(cppu::UnoType<double>::get()))
                sw_setValue(*pCell, aValue.get<double>());
            else
                sw_setString(*pCell, OUString(), true);
            ++pCurrentCell;
        }
    }
}

uno::Sequence<uno::Sequence<double>> SAL_CALL
SwXCellRange::getData()
{
    SolarMutexGuard aGuard;
    const sal_Int32 nRowCount = m_pImpl->GetRowCount();
    const sal_Int32 nColCount = m_pImpl->GetColumnCount();
    if(!nRowCount || !nColCount)
        throw uno::RuntimeException(u"Table too complex"_ustr, getXWeak());
    if (m_pImpl->m_bFirstColumnAsLabel || m_pImpl->m_bFirstRowAsLabel)
    {
        uno::Reference<chart::XChartDataArray> const xDataRange(
                getCellRangeByPosition((m_pImpl->m_bFirstColumnAsLabel) ? 1 : 0,
                                       (m_pImpl->m_bFirstRowAsLabel) ? 1 : 0,
            nColCount-1, nRowCount-1), uno::UNO_QUERY_THROW);
        return xDataRange->getData();
    }
    uno::Sequence< uno::Sequence< double > > vRows(nRowCount);
    auto vCells(GetCells());
    auto pCurrentCell(vCells.begin());
    for(auto& rRow : asNonConstRange(vRows))
    {
        rRow = uno::Sequence<double>(nColCount);
        for(auto& rValue : asNonConstRange(rRow))
        {
            if(!(*pCurrentCell))
                throw uno::RuntimeException(u"Table too complex"_ustr, getXWeak());
            rValue = (*pCurrentCell)->getValue();
            ++pCurrentCell;
        }
    }
    return vRows;
}

void SAL_CALL
SwXCellRange::setData(const uno::Sequence< uno::Sequence<double> >& rData)
{
    SolarMutexGuard aGuard;
    const sal_Int32 nRowCount = m_pImpl->GetRowCount();
    const sal_Int32 nColCount = m_pImpl->GetColumnCount();
    if(!nRowCount || !nColCount)
        throw uno::RuntimeException(u"Table too complex"_ustr, getXWeak());
    if (m_pImpl->m_bFirstColumnAsLabel || m_pImpl->m_bFirstRowAsLabel)
    {
        uno::Reference<chart::XChartDataArray> const xDataRange(
                getCellRangeByPosition((m_pImpl->m_bFirstColumnAsLabel) ? 1 : 0,
                                       (m_pImpl->m_bFirstRowAsLabel) ? 1 : 0,
            nColCount-1, nRowCount-1), uno::UNO_QUERY_THROW);
        return xDataRange->setData(rData);
    }
    lcl_EnsureCoreConnected(m_pImpl->GetFrameFormat(), this);
    if(rData.getLength() != nRowCount)
        throw uno::RuntimeException("Row count mismatch. expected: " + OUString::number(nRowCount) + " got: " + OUString::number(rData.getLength()), getXWeak());
    auto vCells(GetCells());
    auto pCurrentCell(vCells.begin());
    for(const auto& rRow : rData)
    {
        if(rRow.getLength() != nColCount)
            throw uno::RuntimeException("Column count mismatch. expected: " + OUString::number(nColCount) + " got: " + OUString::number(rRow.getLength()), getXWeak());
        for(const auto& rValue : rRow)
        {
            uno::Reference<table::XCell>(*pCurrentCell, uno::UNO_SET_THROW)->setValue(rValue);
            ++pCurrentCell;
        }
    }
}

std::tuple<sal_uInt32, sal_uInt32, sal_uInt32, sal_uInt32>
SwXCellRange::Impl::GetLabelCoordinates(bool bRow)
{
    sal_uInt32 nLeft, nTop, nRight, nBottom;
    nLeft = nTop = nRight = nBottom = 0;
    if(bRow)
    {
        nTop = m_bFirstRowAsLabel ? 1 : 0;
        nBottom = GetRowCount() - 1;
    }
    else
    {
        nLeft = m_bFirstColumnAsLabel ? 1 : 0;
        nRight = GetColumnCount() - 1;
    }
    return std::make_tuple(nLeft, nTop, nRight, nBottom);
}

uno::Sequence<OUString>
SwXCellRange::Impl::GetLabelDescriptions(SwXCellRange & rThis, bool bRow)
{
    SolarMutexGuard aGuard;
    sal_uInt32 nLeft, nTop, nRight, nBottom;
    std::tie(nLeft, nTop, nRight, nBottom) = GetLabelCoordinates(bRow);
    if(!nRight && !nBottom)
        throw uno::RuntimeException(u"Table too complex"_ustr, rThis.getXWeak());
    lcl_EnsureCoreConnected(GetFrameFormat(), &rThis);
    if (!(bRow ? m_bFirstColumnAsLabel : m_bFirstRowAsLabel))
        return {};  // without labels we have no descriptions
    auto xLabelRange(rThis.getCellRangeByPosition(nLeft, nTop, nRight, nBottom));
    auto vCells(static_cast<SwXCellRange*>(xLabelRange.get())->GetCells());
    uno::Sequence<OUString> vResult(vCells.size());
    std::transform(vCells.begin(), vCells.end(), vResult.getArray(),
        [](uno::Reference<table::XCell> xCell) -> OUString { return uno::Reference<text::XText>(xCell, uno::UNO_QUERY_THROW)->getString(); });
    return vResult;
}

uno::Sequence<OUString> SAL_CALL SwXCellRange::getRowDescriptions()
{
    return m_pImpl->GetLabelDescriptions(*this, true);
}

uno::Sequence<OUString> SAL_CALL SwXCellRange::getColumnDescriptions()
{
    return m_pImpl->GetLabelDescriptions(*this, false);
}

void SwXCellRange::Impl::SetLabelDescriptions(SwXCellRange & rThis,
        const uno::Sequence<OUString>& rDesc, bool bRow)
{
    SolarMutexGuard aGuard;
    lcl_EnsureCoreConnected(GetFrameFormat(), &rThis);
    if (!(bRow ? m_bFirstColumnAsLabel : m_bFirstRowAsLabel))
        return; // if there are no labels we cannot set descriptions
    sal_uInt32 nLeft, nTop, nRight, nBottom;
    std::tie(nLeft, nTop, nRight, nBottom) = GetLabelCoordinates(bRow);
    if(!nRight && !nBottom)
        throw uno::RuntimeException(u"Table too complex"_ustr, rThis.getXWeak());
    auto xLabelRange(rThis.getCellRangeByPosition(nLeft, nTop, nRight, nBottom));
    if (!xLabelRange.is())
        throw uno::RuntimeException(u"Missing Cell Range"_ustr, rThis.getXWeak());
    auto vCells(static_cast<SwXCellRange*>(xLabelRange.get())->GetCells());
    if (sal::static_int_cast<sal_uInt32>(rDesc.getLength()) != vCells.size())
        throw uno::RuntimeException(u"Too few or too many descriptions"_ustr, rThis.getXWeak());
    auto pDescIterator(rDesc.begin());
    for(auto& xCell : vCells)
        uno::Reference<text::XText>(xCell, uno::UNO_QUERY_THROW)->setString(*pDescIterator++);
}

void SAL_CALL SwXCellRange::setRowDescriptions(
        const uno::Sequence<OUString>& rRowDesc)
{
    m_pImpl->SetLabelDescriptions(*this, rRowDesc, true);
}

void SAL_CALL SwXCellRange::setColumnDescriptions(
        const uno::Sequence<OUString>& rColumnDesc)
{
    m_pImpl->SetLabelDescriptions(*this, rColumnDesc, false);
}

void SAL_CALL SwXCellRange::addChartDataChangeEventListener(
        const uno::Reference<chart::XChartDataChangeEventListener> & xListener)
{
    // no need to lock here as m_pImpl is const and container threadsafe
    std::unique_lock aGuard(m_pImpl->m_Mutex);
    m_pImpl->m_ChartListeners.addInterface(aGuard, xListener);
}

void SAL_CALL SwXCellRange::removeChartDataChangeEventListener(
        const uno::Reference<chart::XChartDataChangeEventListener> & xListener)
{
    // no need to lock here as m_pImpl is const and container threadsafe
    std::unique_lock aGuard(m_pImpl->m_Mutex);
    m_pImpl->m_ChartListeners.removeInterface(aGuard, xListener);
}

sal_Bool SwXCellRange::isNotANumber(double /*fNumber*/)
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

double SwXCellRange::getNotANumber()
    { throw uno::RuntimeException(u"Not implemented"_ustr, getXWeak()); }

uno::Sequence< beans::PropertyValue > SwXCellRange::createSortDescriptor()
{
    SolarMutexGuard aGuard;
    return SwUnoCursorHelper::CreateSortDescriptor(true);
}

void SAL_CALL SwXCellRange::sort(const uno::Sequence< beans::PropertyValue >& rDescriptor)
{
    SolarMutexGuard aGuard;
    SwSortOptions aSortOpt;
    SwFrameFormat *const pFormat = m_pImpl->GetFrameFormat();
    if(pFormat && SwUnoCursorHelper::ConvertSortProperties(rDescriptor, aSortOpt))
    {
        SwUnoTableCursor& rTableCursor = dynamic_cast<SwUnoTableCursor&>(*m_pImpl->m_pTableCursor);
        rTableCursor.MakeBoxSels();
        UnoActionContext aContext(&pFormat->GetDoc());
        pFormat->GetDoc().SortTable(rTableCursor.GetSelectedBoxes(), aSortOpt);
    }
}

sal_Int32 SwXCellRange::Impl::GetColumnCount() const
{
    return m_RangeDescriptor.nRight - m_RangeDescriptor.nLeft + 1;
}

sal_Int32 SwXCellRange::Impl::GetRowCount() const
{
    return m_RangeDescriptor.nBottom - m_RangeDescriptor.nTop + 1;
}

const SwUnoCursor* SwXCellRange::GetTableCursor() const
{
    SwFrameFormat *const pFormat = m_pImpl->GetFrameFormat();
    return pFormat ? &(*m_pImpl->m_pTableCursor) : nullptr;
}

void SwXCellRange::Impl::Notify( const SfxHint& rHint )
{
    uno::Reference<uno::XInterface> const xThis(m_wThis);
    if(rHint.GetId() == SfxHintId::Dying)
    {
        m_pFrameFormat = nullptr;
        m_pTableCursor.reset(nullptr);
    }
    if (xThis.is())
    {   // fdo#72695: if UNO object is already dead, don't revive it with event
        if(m_pFrameFormat)
        {
            std::unique_lock aGuard(m_Mutex);
            lcl_SendChartEvent(aGuard, xThis, m_ChartListeners);
        }
        else
        {
            std::unique_lock aGuard(m_Mutex);
            m_ChartListeners.disposeAndClear(aGuard, lang::EventObject(xThis));
        }
    }
}

class SwXTableRows::Impl : public SvtListener
{
private:
    SwFrameFormat* m_pFrameFormat;

public:
    explicit Impl(SwFrameFormat& rFrameFormat) : m_pFrameFormat(&rFrameFormat)
    {
        StartListening(rFrameFormat.GetNotifier());
    }
    SwFrameFormat* GetFrameFormat() { return m_pFrameFormat; }
    virtual void Notify(const SfxHint&) override;
};

//  SwXTableRows

OUString SwXTableRows::getImplementationName()
    { return u"SwXTableRows"_ustr; }

sal_Bool SwXTableRows::supportsService(const OUString& rServiceName)
    { return cppu::supportsService(this, rServiceName); }

uno::Sequence< OUString > SwXTableRows::getSupportedServiceNames()
    { return { u"com.sun.star.text.TableRows"_ustr }; }


SwXTableRows::SwXTableRows(SwFrameFormat& rFrameFormat) :
    m_pImpl(new SwXTableRows::Impl(rFrameFormat))
{ }

SwXTableRows::~SwXTableRows()
{ }

SwFrameFormat* SwXTableRows::GetFrameFormat()
{
    return m_pImpl->GetFrameFormat();
}

sal_Int32 SwXTableRows::getCount()
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFrameFormat = GetFrameFormat();
    if(!pFrameFormat)
        throw uno::RuntimeException();
    SwTable* pTable = SwTable::FindTable(pFrameFormat);
    return pTable->GetTabLines().size();
}

///@see SwXCell::CreateXCell (TODO: seems to be copy and paste programming here)
uno::Any SwXTableRows::getByIndex(sal_Int32 nIndex)
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFrameFormat(lcl_EnsureCoreConnected(GetFrameFormat(), this));
    if(nIndex < 0)
        throw lang::IndexOutOfBoundsException();
    SwTable* pTable = SwTable::FindTable( pFrameFormat );
    if(o3tl::make_unsigned(nIndex) >= pTable->GetTabLines().size())
        throw lang::IndexOutOfBoundsException();
    SwTableLine* pLine = pTable->GetTabLines()[nIndex];
    FindUnoTextTableRowInstanceHint aHint{pLine};
    pFrameFormat->GetNotifier().Broadcast(aHint);
    if(!aHint.m_pResult)
        aHint.m_pResult = new SwXTextTableRow(pFrameFormat, pLine);
    uno::Reference<beans::XPropertySet> xRet = static_cast<beans::XPropertySet*>(aHint.m_pResult.get());
    return uno::Any(xRet);
}

uno::Type SAL_CALL SwXTableRows::getElementType()
{
    return cppu::UnoType<beans::XPropertySet>::get();
}

sal_Bool SwXTableRows::hasElements()
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFrameFormat = GetFrameFormat();
    if(!pFrameFormat)
        throw uno::RuntimeException();
    // a table always has rows
    return true;
}

void SwXTableRows::insertByIndex(sal_Int32 nIndex, sal_Int32 nCount)
{
    SolarMutexGuard aGuard;
    if (nCount == 0)
        return;
    SwFrameFormat* pFrameFormat(lcl_EnsureCoreConnected(GetFrameFormat(), this));
    SwTable* pTable = lcl_EnsureTableNotComplex(SwTable::FindTable(pFrameFormat), this);
    const size_t nRowCount = pTable->GetTabLines().size();
    if (nCount <= 0 || 0 > nIndex || o3tl::make_unsigned(nIndex) > nRowCount)
        throw uno::RuntimeException(u"Illegal arguments"_ustr, getXWeak());
    const OUString sTLName = sw_GetCellName(0, nIndex);
    const SwTableBox* pTLBox = pTable->GetTableBox(sTLName);
    bool bAppend = false;
    if(!pTLBox)
    {
        bAppend = true;
        // to append at the end the cursor must be in the last line
        SwTableLines& rLines = pTable->GetTabLines();
        SwTableLine* pLine = rLines.back();
        SwTableBoxes& rBoxes = pLine->GetTabBoxes();
        pTLBox = rBoxes.front();
    }
    if(!pTLBox)
        throw uno::RuntimeException(u"Illegal arguments"_ustr, getXWeak());
    const SwStartNode* pSttNd = pTLBox->GetSttNd();
    SwPosition aPos(*pSttNd);
    // set cursor to the upper-left cell of the range
    UnoActionContext aAction(&pFrameFormat->GetDoc());
    std::shared_ptr<SwUnoTableCursor> const pUnoCursor(
            std::dynamic_pointer_cast<SwUnoTableCursor>(
                pFrameFormat->GetDoc().CreateUnoCursor(aPos, true)));
    pUnoCursor->Move( fnMoveForward, GoInNode );
    {
        // remove actions - TODO: why?
        UnoActionRemoveContext aRemoveContext(&pUnoCursor->GetDoc());
    }
    pFrameFormat->GetDoc().InsertRow(*pUnoCursor, o3tl::narrowing<sal_uInt16>(nCount), bAppend);
}

void SwXTableRows::removeByIndex(sal_Int32 nIndex, sal_Int32 nCount)
{
    SolarMutexGuard aGuard;
    if (nCount == 0)
        return;
    SwFrameFormat* pFrameFormat(lcl_EnsureCoreConnected(GetFrameFormat(), this));
    if(nIndex < 0 || nCount <=0 )
        throw uno::RuntimeException();
    SwTable* pTable = lcl_EnsureTableNotComplex(SwTable::FindTable(pFrameFormat), this);
    OUString sTLName = sw_GetCellName(0, nIndex);
    const SwTableBox* pTLBox = pTable->GetTableBox(sTLName);
    if(!pTLBox)
        throw uno::RuntimeException(u"Illegal arguments"_ustr, getXWeak());
    const SwStartNode* pSttNd = pTLBox->GetSttNd();
    SwPosition aPos(*pSttNd);
    // set cursor to the upper-left cell of the range
    auto pUnoCursor(pFrameFormat->GetDoc().CreateUnoCursor(aPos, true));
    pUnoCursor->Move(fnMoveForward, GoInNode);
    pUnoCursor->SetRemainInSection( false );
    const OUString sBLName = sw_GetCellName(0, nIndex + nCount - 1);
    const SwTableBox* pBLBox = pTable->GetTableBox( sBLName );
    if(!pBLBox)
        throw uno::RuntimeException(u"Illegal arguments"_ustr, getXWeak());
    pUnoCursor->SetMark();
    pUnoCursor->GetPoint()->Assign( *pBLBox->GetSttNd() );
    pUnoCursor->Move(fnMoveForward, GoInNode);
    SwUnoTableCursor& rCursor = dynamic_cast<SwUnoTableCursor&>(*pUnoCursor);
    {
        // HACK: remove pending actions for selecting old style tables
        UnoActionRemoveContext aRemoveContext(rCursor);
    }
    rCursor.MakeBoxSels();
    {   // these braces are important
        UnoActionContext aAction(&pFrameFormat->GetDoc());
        pFrameFormat->GetDoc().DeleteRow(*pUnoCursor);
        pUnoCursor.reset();
    }
    {
        // invalidate all actions - TODO: why?
        UnoActionRemoveContext aRemoveContext(&pFrameFormat->GetDoc());
    }
}

void SwXTableRows::Impl::Notify( const SfxHint& rHint)
{
    if(rHint.GetId() == SfxHintId::Dying)
        m_pFrameFormat = nullptr;
}

// SwXTableColumns

class SwXTableColumns::Impl : public SvtListener
{
    SwFrameFormat* m_pFrameFormat;
    public:
        explicit Impl(SwFrameFormat& rFrameFormat) : m_pFrameFormat(&rFrameFormat)
        {
            StartListening(rFrameFormat.GetNotifier());
        }
        SwFrameFormat* GetFrameFormat() { return m_pFrameFormat; }
        virtual void Notify(const SfxHint&) override;
};

OUString SwXTableColumns::getImplementationName()
    { return u"SwXTableColumns"_ustr; }

sal_Bool SwXTableColumns::supportsService(const OUString& rServiceName)
    { return cppu::supportsService(this, rServiceName); }

uno::Sequence< OUString > SwXTableColumns::getSupportedServiceNames()
    { return { u"com.sun.star.text.TableColumns"_ustr}; }


SwXTableColumns::SwXTableColumns(SwFrameFormat& rFrameFormat) :
    m_pImpl(new SwXTableColumns::Impl(rFrameFormat))
{ }

SwXTableColumns::~SwXTableColumns()
{ }

SwFrameFormat* SwXTableColumns::GetFrameFormat() const
{
    return m_pImpl->GetFrameFormat();
}

sal_Int32 SwXTableColumns::getCount()
{
    SolarMutexGuard aGuard;
    SwFrameFormat* pFrameFormat(lcl_EnsureCoreConnected(GetFrameFormat(), this));
    SwTable* pTable = SwTable::FindTable( pFrameFormat );
//    if(!pTable->IsTableComplex())
//        throw uno::RuntimeException("Table too complex", getXWeak());
    SwTableLines& rLines = pTable->GetTabLines();
    SwTableLine* pLine = rLines.front();
    return pLine->GetTabBoxes().size();
}

uno::Any SwXTableColumns::getByIndex(sal_Int32 nIndex)
{
    SolarMutexGuard aGuard;
    if(nIndex < 0 || getCount() <= nIndex)
        throw lang::IndexOutOfBoundsException();
    return uno::Any(uno::Reference<uno::XInterface>()); // i#21699 not supported
}

uno::Type SAL_CALL SwXTableColumns::getElementType()
{
    return cppu::UnoType<uno::XInterface>::get();
}

sal_Bool SwXTableColumns::hasElements()
{
    SolarMutexGuard aGuard;
    lcl_EnsureCoreConnected(GetFrameFormat(), this);
    return true;
}

///@see SwXTableRows::insertByIndex (TODO: seems to be copy and paste programming here)
void SwXTableColumns::insertByIndex(sal_Int32 nIndex, sal_Int32 nCount)
{
    SolarMutexGuard aGuard;
    if (nCount == 0)
        return;
    SwFrameFormat* pFrameFormat(lcl_EnsureCoreConnected(GetFrameFormat(), this));
    SwTable* pTable = lcl_EnsureTableNotComplex(SwTable::FindTable(pFrameFormat), this);
    SwTableLines& rLines = pTable->GetTabLines();
    SwTableLine* pLine = rLines.front();
    const size_t nColCount = pLine->GetTabBoxes().size();
    if (nCount <= 0 || 0 > nIndex || o3tl::make_unsigned(nIndex) > nColCount)
        throw uno::RuntimeException(u"Illegal arguments"_ustr, getXWeak());
    const OUString sTLName = sw_GetCellName(nIndex, 0);
    const SwTableBox* pTLBox = pTable->GetTableBox( sTLName );
    bool bAppend = false;
    if(!pTLBox)
    {
        bAppend = true;
        // to append at the end the cursor must be in the last line
        SwTableBoxes& rBoxes = pLine->GetTabBoxes();
        pTLBox = rBoxes.back();
    }
    if(!pTLBox)
        throw uno::RuntimeException(u"Illegal arguments"_ustr, getXWeak());
    const SwStartNode* pSttNd = pTLBox->GetSttNd();
    SwPosition aPos(*pSttNd);
    UnoActionContext aAction(&pFrameFormat->GetDoc());
    auto pUnoCursor(pFrameFormat->GetDoc().CreateUnoCursor(aPos, true));
    pUnoCursor->Move(fnMoveForward, GoInNode);

    {
        // remove actions - TODO: why?
        UnoActionRemoveContext aRemoveContext(&pUnoCursor->GetDoc());
    }

    pFrameFormat->GetDoc().InsertCol(*pUnoCursor, o3tl::narrowing<sal_uInt16>(nCount), bAppend);
}

///@see SwXTableRows::removeByIndex (TODO: seems to be copy and paste programming here)
void SwXTableColumns::removeByIndex(sal_Int32 nIndex, sal_Int32 nCount)
{
    SolarMutexGuard aGuard;
    if (nCount == 0)
        return;
    SwFrameFormat* pFrameFormat(lcl_EnsureCoreConnected(GetFrameFormat(), this));
    if(nIndex < 0 || nCount <=0 )
        throw uno::RuntimeException();
    SwTable* pTable = lcl_EnsureTableNotComplex(SwTable::FindTable(pFrameFormat), this);
    const OUString sTLName = sw_GetCellName(nIndex, 0);
    const SwTableBox* pTLBox = pTable->GetTableBox( sTLName );
    if(!pTLBox)
        throw uno::RuntimeException(u"Cell not found"_ustr, getXWeak());
    const SwStartNode* pSttNd = pTLBox->GetSttNd();
    SwPosition aPos(*pSttNd);
    // set cursor to the upper-left cell of the range
    auto pUnoCursor(pFrameFormat->GetDoc().CreateUnoCursor(aPos, true));
    pUnoCursor->Move(fnMoveForward, GoInNode);
    pUnoCursor->SetRemainInSection(false);
    const OUString sTRName = sw_GetCellName(nIndex + nCount - 1, 0);
    const SwTableBox* pTRBox = pTable->GetTableBox(sTRName);
    if(!pTRBox)
        throw uno::RuntimeException(u"Cell not found"_ustr, getXWeak());
    pUnoCursor->SetMark();
    pUnoCursor->GetPoint()->Assign( *pTRBox->GetSttNd() );
    pUnoCursor->Move(fnMoveForward, GoInNode);
    SwUnoTableCursor& rCursor = dynamic_cast<SwUnoTableCursor&>(*pUnoCursor);
    {
        // HACK: remove pending actions for selecting old style tables
        UnoActionRemoveContext aRemoveContext(rCursor);
    }
    rCursor.MakeBoxSels();
    {   // these braces are important
        UnoActionContext aAction(&pFrameFormat->GetDoc());
        pFrameFormat->GetDoc().DeleteCol(*pUnoCursor);
        pUnoCursor.reset();
    }
    {
        // invalidate all actions - TODO: why?
        UnoActionRemoveContext aRemoveContext(&pFrameFormat->GetDoc());
    }
}

void SwXTableColumns::Impl::Notify(const SfxHint& rHint)
{
    if(rHint.GetId() == SfxHintId::Dying)
        m_pFrameFormat = nullptr;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
