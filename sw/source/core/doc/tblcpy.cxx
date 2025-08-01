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

#include <hintids.hxx>

#include <osl/diagnose.h>
#include <svl/numformat.hxx>
#include <frmfmt.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <DocumentContentOperationsManager.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <pam.hxx>
#include <swtable.hxx>
#include <ndtxt.hxx>
#include <tblsel.hxx>
#include <poolfmt.hxx>
#include <cellatr.hxx>
#include <mvsave.hxx>
#include <fmtanchr.hxx>
#include <hints.hxx>
#include <UndoTable.hxx>
#include <fmtfsize.hxx>
#include <frameformats.hxx>
#include <deque>
#include <memory>
#include <numeric>

static void lcl_CpyBox( const SwTable& rCpyTable, const SwTableBox* pCpyBox,
                    SwTable& rDstTable, SwTableBox* pDstBox,
                    bool bDelContent, SwUndoTableCpyTable* pUndo );

// The following type will be used by table copy functions to describe
// the structure of tables (or parts of tables).
// It's for new table model only.

namespace
{
    struct BoxSpanInfo
    {
        SwTableBox* mpBox;
        SwTableBox* mpCopy;
        sal_uInt16 mnColSpan;
        bool mbSelected;
    };

    typedef std::vector< BoxSpanInfo > BoxStructure;
    typedef std::vector< BoxStructure > LineStructure;
    typedef std::deque< sal_uLong > ColumnStructure;

    struct SubBox
    {
        SwTableBox *mpBox;
        bool mbCovered;
    };

    typedef std::vector< SubBox > SubLine;
    typedef std::vector< SubLine > SubTable;

    class TableStructure
    {
    public:
        LineStructure maLines;
        ColumnStructure maCols;
        sal_uInt16 mnStartCol;
        sal_uInt16 mnAddLine;
        void addLine( sal_uInt16 &rLine, const SwTableBoxes&, const SwSelBoxes*,
                      bool bNewModel );
        void addBox( sal_uInt16 nLine, const SwSelBoxes*, SwTableBox *pBox,
                     sal_uLong &rnB, sal_uInt16 &rnC, ColumnStructure::iterator& rpCl,
                     BoxStructure::iterator& rpSel, bool &rbSel, bool bCover );
        void incColSpan( sal_uInt16 nLine, sal_uInt16 nCol );
        explicit TableStructure( const SwTable& rTable );
        TableStructure( const SwTable& rTable, FndBox_ &rFndBox,
                        const SwSelBoxes& rSelBoxes,
                        LineStructure::size_type nMinSize );
        LineStructure::size_type getLineCount() const
            { return maLines.size(); }
        void moreLines( const SwTable& rTable );
        void assignBoxes( const TableStructure &rSource );
        void copyBoxes( const SwTable& rSource, SwTable& rDstTable,
                        SwUndoTableCpyTable* pUndo ) const;
    };

    SubTable::iterator insertSubLine( SubTable& rSubTable, SwTableLine& rLine,
        const SubTable::iterator& pStartLn );

    SubTable::iterator insertSubBox( SubTable& rSubTable, SwTableBox& rBox,
        SubTable::iterator pStartLn, const SubTable::iterator& pEndLn )
    {
        if( !rBox.GetTabLines().empty() )
        {
            SubTable::size_type nSize = static_cast<SubTable::size_type>(std::distance( pStartLn, pEndLn ));
            if( nSize < rBox.GetTabLines().size() )
            {
                SubLine aSubLine;
                for( const auto& rSubBox : *pStartLn )
                {
                    SubBox aSub;
                    aSub.mpBox = rSubBox.mpBox;
                    aSub.mbCovered = true;
                    aSubLine.push_back( aSub );
                }
                do
                {
                    rSubTable.insert( pEndLn, aSubLine );
                } while( ++nSize < rBox.GetTabLines().size() );
            }
            for( auto pLine : rBox.GetTabLines() )
                pStartLn = insertSubLine( rSubTable, *pLine, pStartLn );
            OSL_ENSURE( pStartLn == pEndLn, "Sub line confusion" );
        }
        else
        {
            SubBox aSub;
            aSub.mpBox = &rBox;
            aSub.mbCovered = false;
            while( pStartLn != pEndLn )
            {
                pStartLn->push_back( aSub );
                aSub.mbCovered = true;
                ++pStartLn;
            }
        }
        return pStartLn;
    }

    SubTable::iterator insertSubLine( SubTable& rSubTable, SwTableLine& rLine,
        const SubTable::iterator& pStartLn )
    {
        SubTable::iterator pMax = pStartLn;
        ++pMax;
        SubTable::difference_type nMax = 1;
        for( auto pBox : rLine.GetTabBoxes() )
        {
            SubTable::iterator pTmp = insertSubBox( rSubTable, *pBox, pStartLn, pMax );
            SubTable::difference_type nTmp = std::distance( pStartLn, pTmp );
            if( nTmp > nMax )
            {
                pMax = pTmp;
                nMax = nTmp;
            }
        }
        return pMax;
    }

    TableStructure::TableStructure( const SwTable& rTable ) :
        maLines( rTable.GetTabLines().size() ), mnStartCol(USHRT_MAX),
        mnAddLine(0)
    {
        maCols.push_front(0);
        sal_uInt16 nCnt = 0;
        for( auto pLine : rTable.GetTabLines() )
            addLine( nCnt, pLine->GetTabBoxes(), nullptr, rTable.IsNewModel() );
    }

    TableStructure::TableStructure( const SwTable& rTable,
        FndBox_ &rFndBox, const SwSelBoxes& rSelBoxes,
        LineStructure::size_type nMinSize )
        : mnStartCol(USHRT_MAX), mnAddLine(0)
    {
        if( rFndBox.GetLines().empty() )
            return;

        bool bNoSelection = rSelBoxes.size() < 2;
        FndLines_t &rFndLines = rFndBox.GetLines();
        maCols.push_front(0);
        const SwTableLine* pLine = rFndLines.front()->GetLine();
        const sal_uInt16 nStartLn = rTable.GetTabLines().GetPos( pLine );
        SwTableLines::size_type nEndLn = nStartLn;
        if( rFndLines.size() > 1 )
        {
            pLine = rFndLines.back()->GetLine();
            nEndLn = rTable.GetTabLines().GetPos( pLine );
        }
        if( nStartLn < USHRT_MAX && nEndLn < USHRT_MAX )
        {
            const SwTableLines &rLines = rTable.GetTabLines();
            if( bNoSelection && nMinSize > nEndLn - nStartLn + 1 )
            {
                SwTableLines::size_type nNewEndLn = nStartLn + nMinSize - 1;
                if( nNewEndLn >= rLines.size() )
                {
                    mnAddLine = nNewEndLn - rLines.size() + 1;
                    nNewEndLn = rLines.size() - 1;
                }
                while( nEndLn < nNewEndLn )
                {
                    SwTableLine *pLine2 = rLines[ ++nEndLn ];
                    SwTableBox *pTmpBox = pLine2->GetTabBoxes()[0];
                    FndLine_ *pInsLine = new FndLine_( pLine2, &rFndBox );
                    pInsLine->GetBoxes().insert(pInsLine->GetBoxes().begin(), std::make_unique<FndBox_>(pTmpBox, pInsLine));
                    rFndLines.push_back(std::unique_ptr<FndLine_>(pInsLine));
                }
            }
            maLines.resize( nEndLn - nStartLn + 1 );
            const SwSelBoxes* pSelBoxes = &rSelBoxes;
            sal_uInt16 nCnt = 0;
            for( SwTableLines::size_type nLine = nStartLn; nLine <= nEndLn; ++nLine )
            {
                addLine( nCnt, rLines[nLine]->GetTabBoxes(),
                         pSelBoxes, rTable.IsNewModel() );
                if( bNoSelection )
                    pSelBoxes = nullptr;
            }
        }
        if( bNoSelection && mnStartCol < USHRT_MAX )
        {
            sal_uInt16 nIdx = std::min(mnStartCol, o3tl::narrowing<sal_uInt16>(maLines[0].size()));
            mnStartCol = std::accumulate(maLines[0].begin(), maLines[0].begin() + nIdx, sal_uInt16(0),
                [](sal_uInt16 sum, const BoxSpanInfo& rInfo) { return sum + rInfo.mnColSpan; });
        }
        else
            mnStartCol = USHRT_MAX;
    }

    void TableStructure::addLine( sal_uInt16 &rLine, const SwTableBoxes& rBoxes,
        const SwSelBoxes* pSelBoxes, bool bNewModel )
    {
        bool bComplex = false;
        if( !bNewModel )
            for( SwTableBoxes::size_type nBox = 0; !bComplex && nBox < rBoxes.size(); ++nBox )
                bComplex = !rBoxes[nBox]->GetTabLines().empty();
        if( bComplex )
        {
            SubTable aSubTable;
            aSubTable.emplace_back();
            SubTable::iterator pStartLn = aSubTable.begin();
            SubTable::iterator pEndLn = aSubTable.end();
            for( auto pBox : rBoxes )
                insertSubBox( aSubTable, *pBox, pStartLn, pEndLn );
            SubTable::size_type nSize = aSubTable.size();
            if( nSize )
            {
                maLines.resize( maLines.size() + nSize - 1 );
                while( pStartLn != pEndLn )
                {
                    bool bSelected = false;
                    sal_uLong nBorder = 0;
                    sal_uInt16 nCol = 0;
                    maLines[rLine].reserve( pStartLn->size() );
                    BoxStructure::iterator pSel = maLines[rLine].end();
                    ColumnStructure::iterator pCol = maCols.begin();
                    for( const auto& rBox : *pStartLn )
                    {
                        addBox( rLine, pSelBoxes, rBox.mpBox, nBorder, nCol,
                            pCol, pSel, bSelected, rBox.mbCovered );
                    }
                    ++rLine;
                    ++pStartLn;
                }
            }
        }
        else
        {
            bool bSelected = false;
            sal_uLong nBorder = 0;
            sal_uInt16 nCol = 0;
            maLines[rLine].reserve( rBoxes.size() );
            ColumnStructure::iterator pCol = maCols.begin();
            BoxStructure::iterator pSel = maLines[rLine].end();
            for( auto pBox : rBoxes )
                addBox( rLine, pSelBoxes, pBox, nBorder, nCol,
                        pCol, pSel, bSelected, false );
            ++rLine;
        }
    }

    void TableStructure::addBox( sal_uInt16 nLine, const SwSelBoxes* pSelBoxes,
        SwTableBox *pBox, sal_uLong &rnBorder, sal_uInt16 &rnCol,
        ColumnStructure::iterator& rpCol, BoxStructure::iterator& rpSel,
        bool &rbSelected, bool bCovered )
    {
        BoxSpanInfo aInfo;
        if( pSelBoxes &&
            pSelBoxes->end() != pSelBoxes->find( pBox ) )
        {
            aInfo.mbSelected = true;
            if( mnStartCol == USHRT_MAX )
            {
                mnStartCol = o3tl::narrowing<sal_uInt16>(maLines[nLine].size());
                if( pSelBoxes->size() < 2 )
                {
                    pSelBoxes = nullptr;
                    aInfo.mbSelected = false;
                }
            }
        }
        else
            aInfo.mbSelected = false;
        rnBorder += pBox->GetFrameFormat()->GetFrameSize().GetWidth();
        const sal_uInt16 nLeftCol = rnCol;
        while( rpCol != maCols.end() && *rpCol < rnBorder )
        {
            ++rnCol;
            ++rpCol;
        }
        if( rpCol == maCols.end() || *rpCol > rnBorder )
        {
            rpCol = maCols.insert( rpCol, rnBorder );
            incColSpan( nLine, rnCol );
        }
        aInfo.mnColSpan = rnCol - nLeftCol;
        aInfo.mpCopy = nullptr;
        aInfo.mpBox = bCovered ? nullptr : pBox;
        maLines[nLine].push_back( aInfo );
        if( !aInfo.mbSelected )
            return;

        if( rbSelected )
        {
            while( rpSel != maLines[nLine].end() )
            {
                rpSel->mbSelected = true;
                ++rpSel;
            }
        }
        else
        {
            rpSel = maLines[nLine].end();
            rbSelected = true;
        }
        --rpSel;
    }

    void TableStructure::moreLines( const SwTable& rTable )
    {
        if( !mnAddLine )
            return;

        const SwTableLines &rLines = rTable.GetTabLines();
        const sal_uInt16 nLineCount = rLines.size();
        if( nLineCount < mnAddLine )
            mnAddLine = nLineCount;
        sal_uInt16 nLine = o3tl::narrowing<sal_uInt16>(maLines.size());
        maLines.resize( nLine + mnAddLine );
        while( mnAddLine )
        {
            SwTableLine *pLine = rLines[ nLineCount - mnAddLine ];
            addLine( nLine, pLine->GetTabBoxes(), nullptr, rTable.IsNewModel() );
            --mnAddLine;
        }
    }

    void TableStructure::incColSpan( sal_uInt16 nLineMax, sal_uInt16 nNewCol )
    {
        for( sal_uInt16 nLine = 0; nLine < nLineMax; ++nLine )
        {
            BoxStructure::iterator pInfo = maLines[nLine].begin();
            BoxStructure::iterator pEnd = maLines[nLine].end();
            tools::Long nCol = pInfo->mnColSpan;
            while( nNewCol > nCol && ++pInfo != pEnd )
                nCol += pInfo->mnColSpan;
            if( pInfo != pEnd )
                ++(pInfo->mnColSpan);
        }
    }

    void TableStructure::assignBoxes( const TableStructure &rSource )
    {
        LineStructure::const_iterator pFirstLine = rSource.maLines.begin();
        LineStructure::const_iterator pLastLine = rSource.maLines.end();
        if( pFirstLine == pLastLine )
            return;
        LineStructure::const_iterator pCurrLine = pFirstLine;
        LineStructure::size_type nLineCount = maLines.size();
        sal_uInt16 nFirstStartCol = 0;
        {
            BoxStructure::const_iterator pFirstBox = pFirstLine->begin();
            if( pFirstBox != pFirstLine->end() && pFirstBox->mpBox &&
                pFirstBox->mpBox->getDummyFlag() )
                nFirstStartCol = pFirstBox->mnColSpan;
        }
        for( LineStructure::size_type nLine = 0; nLine < nLineCount; ++nLine )
        {
            BoxStructure::const_iterator pFirstBox = pCurrLine->begin();
            BoxStructure::const_iterator pLastBox = pCurrLine->end();
            sal_uInt16 nCurrStartCol = mnStartCol;
            if( pFirstBox != pLastBox )
            {
                BoxStructure::const_iterator pTmpBox = pLastBox;
                --pTmpBox;
                if( pTmpBox->mpBox && pTmpBox->mpBox->getDummyFlag() )
                    --pLastBox;
                if( pFirstBox != pLastBox && pFirstBox->mpBox &&
                    pFirstBox->mpBox->getDummyFlag() )
                {
                    if( nCurrStartCol < USHRT_MAX )
                    {
                        if( pFirstBox->mnColSpan > nFirstStartCol )
                            nCurrStartCol += pFirstBox->mnColSpan - nFirstStartCol;
                    }
                    ++pFirstBox;
                }
            }
            if( pFirstBox != pLastBox )
            {
                BoxStructure::const_iterator pCurrBox = pFirstBox;
                BoxStructure &rBox = maLines[nLine];
                BoxStructure::size_type nBoxCount = rBox.size();
                sal_uInt16 nCol = 0;
                for( BoxStructure::size_type nBox = 0; nBox < nBoxCount; ++nBox )
                {
                    BoxSpanInfo& rInfo = rBox[nBox];
                    nCol += rInfo.mnColSpan;
                    if( rInfo.mbSelected || nCol > nCurrStartCol )
                    {
                        rInfo.mpCopy = pCurrBox->mpBox;
                        assert(rInfo.mpCopy);
                        if( rInfo.mbSelected && rInfo.mpCopy->getDummyFlag() )
                        {
                            ++pCurrBox;
                            if( pCurrBox == pLastBox )
                            {
                                pCurrBox = pFirstBox;
                                if( pCurrBox->mpBox->getDummyFlag() )
                                    ++pCurrBox;
                            }
                            rInfo.mpCopy = pCurrBox->mpBox;
                        }
                        ++pCurrBox;
                        if( pCurrBox == pLastBox )
                        {
                            if( rInfo.mbSelected )
                                pCurrBox = pFirstBox;
                            else
                            {
                                rInfo.mbSelected = rInfo.mpCopy == nullptr;
                                break;
                            }
                        }
                        rInfo.mbSelected = rInfo.mpCopy == nullptr;
                    }
                }
            }
            ++pCurrLine;
            if( pCurrLine == pLastLine )
                pCurrLine = pFirstLine;
        }
    }

    void TableStructure::copyBoxes( const SwTable& rSource, SwTable& rDstTable,
                                    SwUndoTableCpyTable* pUndo ) const
    {
        LineStructure::size_type nLineCount = maLines.size();
        for( LineStructure::size_type nLine = 0; nLine < nLineCount; ++nLine )
        {
            const BoxStructure &rBox = maLines[nLine];
            BoxStructure::size_type nBoxCount = rBox.size();
            for( BoxStructure::size_type nBox = 0; nBox < nBoxCount; ++nBox )
            {
                const BoxSpanInfo& rInfo = rBox[nBox];
                if( ( rInfo.mpCopy && !rInfo.mpCopy->getDummyFlag() )
                    || rInfo.mbSelected )
                {
                    SwTableBox *pBox = rInfo.mpBox;
                    if( pBox && pBox->getRowSpan() > 0 )
                        lcl_CpyBox( rSource, rInfo.mpCopy, rDstTable, pBox,
                                    true, pUndo );
                }
            }
        }
    }
}

/** Copy Table into this Box.
    Copy all Boxes of a Line into the corresponding Boxes. The old content
    is deleted by doing this.
    If no Box is left the remaining content goes to the Box of a "BaseLine".
    If there's no Line anymore, put it also into the last Box of a "BaseLine". */
static void lcl_CpyBox( const SwTable& rCpyTable, const SwTableBox* pCpyBox,
                    SwTable& rDstTable, SwTableBox* pDstBox,
                    bool bDelContent, SwUndoTableCpyTable* pUndo )
{
    OSL_ENSURE( ( !pCpyBox || pCpyBox->GetSttNd() ) && pDstBox->GetSttNd(),
            "No content in this Box" );

    SwDoc& rCpyDoc = rCpyTable.GetFrameFormat()->GetDoc();
    SwDoc& rDoc = rDstTable.GetFrameFormat()->GetDoc();

    // First copy the new content and then delete the old one.
    // Do not create empty Sections, otherwise they will be deleted!
    std::unique_ptr< SwNodeRange > pRg( pCpyBox ?
        new SwNodeRange ( *pCpyBox->GetSttNd(), SwNodeOffset(1),
        *pCpyBox->GetSttNd()->EndOfSectionNode() ) : nullptr );

    SwNodeIndex aInsIdx( *pDstBox->GetSttNd(), bDelContent ? SwNodeOffset(1) :
                        pDstBox->GetSttNd()->EndOfSectionIndex() -
                        pDstBox->GetSttIdx() );

    if( pUndo )
        pUndo->AddBoxBefore( *pDstBox, bDelContent );

    bool bUndoRedline = pUndo && rDoc.getIDocumentRedlineAccess().IsRedlineOn();
    ::sw::UndoGuard const undoGuard(rDoc.GetIDocumentUndoRedo());

    SwNodeIndex aSavePos( aInsIdx, -1 );
    if (pRg)
        rCpyDoc.GetDocumentContentOperationsManager().CopyWithFlyInFly(*pRg, aInsIdx.GetNode(), nullptr, false);
    else
        rDoc.GetNodes().MakeTextNode( aInsIdx.GetNode(), rDoc.GetDfltTextFormatColl() );
    ++aSavePos;

    SwTableLine* pLine = pDstBox->GetUpper();
    while( pLine->GetUpper() )
        pLine = pLine->GetUpper()->GetUpper();

    bool bReplaceColl = true;
    if( bDelContent && !bUndoRedline )
    {
        // Delete the Fly first, then the corresponding Nodes
        SwNodeIndex aEndNdIdx( *aInsIdx.GetNode().EndOfSectionNode() );

        // Move Bookmarks
        {
            SwPosition aMvPos( aInsIdx );
            SwContentNode* pCNd = SwNodes::GoPrevious( &aMvPos );
            assert(pCNd); // keep coverity happy
            aMvPos.SetContent( pCNd->Len() );
            SwDoc::CorrAbs( aInsIdx, aEndNdIdx, aMvPos );
        }

        // If we still have FlyFrames hanging around, delete them too
        for(sw::SpzFrameFormat* pFly: *rDoc.GetSpzFrameFormats())
        {
            SwFormatAnchor const*const pAnchor = &pFly->GetAnchor();
            SwNode const*const pAnchorNode = pAnchor->GetAnchorNode();
            if (pAnchorNode &&
                ((RndStdIds::FLY_AT_PARA == pAnchor->GetAnchorId()) ||
                 (RndStdIds::FLY_AT_CHAR == pAnchor->GetAnchorId())) &&
                aInsIdx <= *pAnchorNode && *pAnchorNode <= aEndNdIdx.GetNode() )
            {
                rDoc.getIDocumentLayoutAccess().DelLayoutFormat( pFly );
            }
        }

        // If DestBox is a Headline Box and has Table style set, then
        // DO NOT automatically set the TableHeadline style!
        if( 1 < rDstTable.GetTabLines().size() &&
            pLine == rDstTable.GetTabLines().front() )
        {
            SwContentNode* pCNd = aInsIdx.GetNode().GetContentNode();
            if( !pCNd )
            {
                SwNodeIndex aTmp( aInsIdx );
                pCNd = SwNodes::GoNext(&aTmp);
            }

            if( pCNd &&
                RES_POOLCOLL_TABLE_HDLN !=
                    pCNd->GetFormatColl()->GetPoolFormatId() )
                bReplaceColl = false;
        }

        rDoc.GetNodes().Delete( aInsIdx, aEndNdIdx.GetIndex() - aInsIdx.GetIndex() );
    }

    //b6341295: Table copy redlining will be managed by AddBoxAfter()
    if( pUndo )
        pUndo->AddBoxAfter( *pDstBox, aInsIdx, bDelContent );

    // heading
    SwTextNode *const pTextNd = aSavePos.GetNode().GetTextNode();
    if( !pTextNd )
        return;

    const sal_uInt16 nPoolId = pTextNd->GetTextColl()->GetPoolFormatId();
    if( bReplaceColl &&
        (( 1 < rDstTable.GetTabLines().size() &&
            pLine == rDstTable.GetTabLines().front() )
            // Is the Table's content still valid?
            ? RES_POOLCOLL_TABLE == nPoolId
            : RES_POOLCOLL_TABLE_HDLN == nPoolId ) )
    {
        SwTextFormatColl* pColl = rDoc.getIDocumentStylePoolAccess().GetTextCollFromPool(
            o3tl::narrowing<sal_uInt16>(
                                RES_POOLCOLL_TABLE == nPoolId
                                    ? RES_POOLCOLL_TABLE_HDLN
                                    : RES_POOLCOLL_TABLE ) );
        if( pColl )         // Apply style
        {
            SwPaM aPam( aSavePos );
            aPam.SetMark();
            aPam.Move( fnMoveForward, GoInSection );
            rDoc.SetTextFormatColl( aPam, pColl );
        }
    }

    // Delete the current Formula/Format/Value values
    if( SfxItemState::SET == pDstBox->GetFrameFormat()->GetItemState( RES_BOXATR_FORMAT ) ||
        SfxItemState::SET == pDstBox->GetFrameFormat()->GetItemState( RES_BOXATR_FORMULA ) ||
        SfxItemState::SET == pDstBox->GetFrameFormat()->GetItemState( RES_BOXATR_VALUE ) )
    {
        pDstBox->ClaimFrameFormat()->ResetFormatAttr( RES_BOXATR_FORMAT,
                                             RES_BOXATR_VALUE );
    }

    // Copy the TableBoxAttributes - Formula/Format/Value
    if( !pCpyBox )
        return;

    SfxItemSetFixed<RES_BOXATR_FORMAT, RES_BOXATR_VALUE> aBoxAttrSet( rCpyDoc.GetAttrPool() );
    aBoxAttrSet.Put( pCpyBox->GetFrameFormat()->GetAttrSet() );
    if( !aBoxAttrSet.Count() )
        return;

    const SwTableBoxNumFormat* pItem;
    SvNumberFormatter* pN = rDoc.GetNumberFormatter( false );
    if( pN && pN->HasMergeFormatTable() &&
        (pItem = aBoxAttrSet.GetItemIfSet( RES_BOXATR_FORMAT, false )) )
    {
        sal_uLong nOldIdx = pItem->GetValue();
        sal_uLong nNewIdx = pN->GetMergeFormatIndex( nOldIdx );
        if( nNewIdx != nOldIdx )
            aBoxAttrSet.Put( SwTableBoxNumFormat( nNewIdx ));
    }
    pDstBox->ClaimFrameFormat()->SetFormatAttr( aBoxAttrSet );
}

bool SwTable::InsNewTable( const SwTable& rCpyTable, const SwSelBoxes& rSelBoxes,
                        SwUndoTableCpyTable* pUndo )
{
    SwDoc& rDoc = GetFrameFormat()->GetDoc();
    SwDoc& rCpyDoc = rCpyTable.GetFrameFormat()->GetDoc();

    SwTableNumFormatMerge aTNFM( rCpyDoc, rDoc );

    // Analyze source structure
    TableStructure aCopyStruct( rCpyTable );

    // Analyze target structure (from start box) and selected substructure
    FndBox_ aFndBox( nullptr, nullptr );
    {   // get all boxes/lines
        FndPara aPara( rSelBoxes, &aFndBox );
        ForEach_FndLineCopyCol( GetTabLines(), &aPara );
    }
    TableStructure aTarget( *this, aFndBox, rSelBoxes, aCopyStruct.getLineCount() );

    bool bClear = false;
    if( aTarget.mnAddLine && IsNewModel() )
    {
        SwSelBoxes aBoxes;
        aBoxes.insert( GetTabLines().back()->GetTabBoxes().front() );
        if( pUndo )
            pUndo->InsertRow( *this, aBoxes, aTarget.mnAddLine );
        else
            InsertRow( rDoc, aBoxes, aTarget.mnAddLine, /*bBehind*/true );

        aTarget.moreLines( *this );
        bClear = true;
    }

    // Find mapping, if needed extend target table and/or selection
    aTarget.assignBoxes( aCopyStruct );

    {
        const_cast<SwTable*>(&rCpyTable)->SwitchFormulasToRelativeRepresentation();
    }

    // delete frames
    aFndBox.SetTableLines( *this );
    if( bClear )
        aFndBox.ClearLineBehind();
    aFndBox.DelFrames( *this );

    // copy boxes
    aTarget.copyBoxes( rCpyTable, *this, pUndo );

    // adjust row span attributes accordingly

    // make frames
    aFndBox.MakeFrames( *this );

    return true;
}

/** Copy Table into this Box.
    Copy all Boxes of a Line into the corresponding Boxes. The old content is
    deleted by doing this.
    If no Box is left the remaining content goes to the Box of a "BaseLine".
    If there's no Line anymore, put it also into the last Box of a "BaseLine". */
bool SwTable::InsTable( const SwTable& rCpyTable, const SwNodeIndex& rSttBox,
                        SwUndoTableCpyTable* pUndo )
{
    SetHTMLTableLayout(std::shared_ptr<SwHTMLTableLayout>());    // Delete HTML Layout

    SwDoc& rDoc = GetFrameFormat()->GetDoc();

    SwTableNode* pTableNd = SwDoc::IsIdxInTable( rSttBox );

    // Find the Box, to which should be copied:
    SwTableBox* pMyBox = GetTableBox(
            rSttBox.GetNode().FindTableBoxStartNode()->GetIndex() );

    assert(pMyBox && "Index is not in a Box in this Table");

    // First delete the Table's Frames
    FndBox_ aFndBox( nullptr, nullptr );
    aFndBox.DelFrames( pTableNd->GetTable() );

    SwDoc& rCpyDoc = rCpyTable.GetFrameFormat()->GetDoc();

    const_cast<SwTable*>(&rCpyTable)->SwitchFormulasToRelativeRepresentation();

    SwTableNumFormatMerge aTNFM( rCpyDoc, rDoc );

    bool bDelContent = true;
    const SwTableBox* pTmp;

    for( auto pLine : rCpyTable.GetTabLines() )
    {
        // Get the first from the CopyLine
        const SwTableBox* pCpyBox = pLine->GetTabBoxes().front();
        while( !pCpyBox->GetTabLines().empty() )
            pCpyBox = pCpyBox->GetTabLines().front()->GetTabBoxes().front();

        do {
            // First copy the new content and then delete the old one.
            // Do not create empty Sections, otherwise they will be deleted!
            lcl_CpyBox( rCpyTable, pCpyBox, *this, pMyBox, bDelContent, pUndo );

            pTmp = pCpyBox->FindNextBox( rCpyTable, pCpyBox, false );
            if( !pTmp )
                break;      // no more Boxes
            pCpyBox = pTmp;

            pTmp = pMyBox->FindNextBox( *this, pMyBox, false );
            if( !pTmp )
                bDelContent = false;  // No space left?
            else
                pMyBox = const_cast<SwTableBox*>(pTmp);

        } while( true );

        // Find the topmost Line
        SwTableLine* pNxtLine = pMyBox->GetUpper();
        while( pNxtLine->GetUpper() )
            pNxtLine = pNxtLine->GetUpper()->GetUpper();
        const SwTableLines::size_type nPos = GetTabLines().GetPos( pNxtLine ) + 1;
        // Is there a next?
        if( nPos >= GetTabLines().size() )
            bDelContent = false;      // there is none, all goes into the last Box
        else
        {
            // Find the next Box with content
            pNxtLine = GetTabLines()[ nPos ];
            pMyBox = pNxtLine->GetTabBoxes().front();
            while( !pMyBox->GetTabLines().empty() )
                pMyBox = pMyBox->GetTabLines().front()->GetTabBoxes().front();
            bDelContent = true;
        }
    }

    aFndBox.MakeFrames( pTableNd->GetTable() );     // Create the Frames anew
    return true;
}

bool SwTable::InsTable( const SwTable& rCpyTable, const SwSelBoxes& rSelBoxes,
                        SwUndoTableCpyTable* pUndo )
{
    OSL_ENSURE( !rSelBoxes.empty(), "Missing selection" );

    SetHTMLTableLayout(std::shared_ptr<SwHTMLTableLayout>());    // Delete HTML Layout

    if( IsNewModel() || rCpyTable.IsNewModel() )
        return InsNewTable( rCpyTable, rSelBoxes, pUndo );

    OSL_ENSURE( !rCpyTable.IsTableComplex(), "Table too complex" );

    SwDoc& rDoc = GetFrameFormat()->GetDoc();
    SwDoc& rCpyDoc = rCpyTable.GetFrameFormat()->GetDoc();

    SwTableNumFormatMerge aTNFM( rCpyDoc, rDoc );

    FndLine_ *pFLine;
    FndBox_ aFndBox( nullptr, nullptr );
    // Find all Boxes/Lines
    {
        FndPara aPara( rSelBoxes, &aFndBox );
        ForEach_FndLineCopyCol( GetTabLines(), &aPara );
    }

    // Special case: If a Box is located in a Table, copy it to all selected
    // Boxes!
    if( 1 != rCpyTable.GetTabSortBoxes().size() )
    {
        FndBox_* pFndBox;

        const FndLines_t::size_type nFndCnt = aFndBox.GetLines().size();
        if( !nFndCnt )
            return false;

        // Check if we have enough space for all Lines and Boxes
        SwTableLines::size_type nTstLns = 0;
        pFLine = aFndBox.GetLines().front().get();
        sal_uInt16 nSttLine = GetTabLines().GetPos( pFLine->GetLine() );
        // Do we have as many rows, actually?
        if( 1 == nFndCnt )
        {
            // Is there still enough space in the Table?
            if( (GetTabLines().size() - nSttLine ) <
                rCpyTable.GetTabLines().size() )
            {
                // If we don't have enough Lines, then see if we can insert
                // new ones to reach our goal. But only if the SSelection
                // contains a Box!
                if( 1 < rSelBoxes.size() )
                    return false;

                const sal_uInt16 nNewLns = rCpyTable.GetTabLines().size() -
                                (GetTabLines().size() - nSttLine );

                // See if the Box count is high enough for the Lines
                SwTableLine* pLastLn = GetTabLines().back();

                SwTableBox* pSttBox = pFLine->GetBoxes()[0]->GetBox();
                const SwTableBoxes::size_type nSttBox = pFLine->GetLine()->GetBoxPos( pSttBox );
                for( SwTableLines::size_type n = rCpyTable.GetTabLines().size() - nNewLns;
                        n < rCpyTable.GetTabLines().size(); ++n )
                {
                    SwTableLine* pCpyLn = rCpyTable.GetTabLines()[ n ];

                    if( pLastLn->GetTabBoxes().size() < nSttBox ||
                        ( pLastLn->GetTabBoxes().size() - nSttBox ) <
                            pCpyLn->GetTabBoxes().size() )
                        return false;

                    // Test for nesting
                    for( SwTableBoxes::size_type nBx = 0; nBx < pCpyLn->GetTabBoxes().size(); ++nBx )
                        if( !pLastLn->GetTabBoxes()[ nSttBox + nBx ]->GetSttNd() )
                            return false;
                }
                // We have enough space for the to-be-copied, so insert new
                // rows accordingly.
                SwTableBox* pInsBox = pLastLn->GetTabBoxes()[ nSttBox ];
                OSL_ENSURE( pInsBox && pInsBox->GetSttNd(),
                    "no ContentBox or it's not in this Table" );
                SwSelBoxes aBoxes;

                if( pUndo
                    ? !pUndo->InsertRow( *this, SelLineFromBox( pInsBox,
                                aBoxes ), nNewLns )
                    : !InsertRow( rDoc, SelLineFromBox( pInsBox,
                                aBoxes ), nNewLns, /*bBehind*/true ) )
                    return false;
            }

            nTstLns = rCpyTable.GetTabLines().size();        // copy this many
        }
        else if( 0 == (nFndCnt % rCpyTable.GetTabLines().size()) )
            nTstLns = nFndCnt;
        else
            return false;       // not enough space for the rows

        for( SwTableLines::size_type nLn = 0; nLn < nTstLns; ++nLn )
        {
            // We have enough rows, so check the Boxes per row
            pFLine = aFndBox.GetLines()[ nLn % nFndCnt ].get();
            SwTableLine* pLine = pFLine->GetLine();
            SwTableBox* pSttBox = pFLine->GetBoxes()[0]->GetBox();
            const SwTableBoxes::size_type nSttBox = pLine->GetBoxPos( pSttBox );
            std::unique_ptr<FndLine_> pInsFLine;
            if( nLn >= nFndCnt )
            {
                // We have more rows in the ClipBoard than we have selected
                pInsFLine.reset(new FndLine_( GetTabLines()[ nSttLine + nLn ],
                                        &aFndBox ));
                pLine = pInsFLine->GetLine();
            }
            SwTableLine* pCpyLn = rCpyTable.GetTabLines()[ nLn %
                                        rCpyTable.GetTabLines().size() ];

            // Selected too few rows?
            if( pInsFLine )
            {
                // We insert a new row into the FndBox
                if( pLine->GetTabBoxes().size() < nSttBox ||
                    pLine->GetTabBoxes().size() - nSttBox < pFLine->GetBoxes().size() )
                {
                    return false;
                }

                // Test for nesting
                for (FndBoxes_t::size_type nBx = 0; nBx < pFLine->GetBoxes().size(); ++nBx)
                {
                    SwTableBox *pTmpBox = pLine->GetTabBoxes()[ nSttBox + nBx ];
                    if( !pTmpBox->GetSttNd() )
                    {
                        return false;
                    }
                    // if Ok, insert the Box into the FndLine
                    pFndBox = new FndBox_( pTmpBox, pInsFLine.get() );
                    pInsFLine->GetBoxes().insert( pInsFLine->GetBoxes().begin() + nBx,
                            std::unique_ptr<FndBox_>(pFndBox));
                }
                aFndBox.GetLines().insert( aFndBox.GetLines().begin() + nLn, std::move(pInsFLine));
            }
            else if( pFLine->GetBoxes().size() == 1 )
            {
                if( pLine->GetTabBoxes().size() < nSttBox  ||
                    ( pLine->GetTabBoxes().size() - nSttBox ) <
                    pCpyLn->GetTabBoxes().size() )
                    return false;

                // Test for nesting
                for( SwTableBoxes::size_type nBx = 0; nBx < pCpyLn->GetTabBoxes().size(); ++nBx )
                {
                    SwTableBox *pTmpBox = pLine->GetTabBoxes()[ nSttBox + nBx ];
                    if( !pTmpBox->GetSttNd() )
                        return false;
                    // if Ok, insert the Box into the FndLine
                    if( nBx == pFLine->GetBoxes().size() )
                    {
                        pFndBox = new FndBox_( pTmpBox, pFLine );
                        pFLine->GetBoxes().insert(pFLine->GetBoxes().begin() + nBx,
                                std::unique_ptr<FndBox_>(pFndBox));
                    }
                }
            }
            else
            {
                // Match the selected Boxes with the ones in the Clipboard
                // (n times)
                if( 0 != ( pFLine->GetBoxes().size() %
                            pCpyLn->GetTabBoxes().size() ))
                    return false;

                // Test for nesting
                for (auto &rpBox : pFLine->GetBoxes())
                {
                    if (!rpBox->GetBox()->GetSttNd())
                        return false;
                }
            }
        }

        if( aFndBox.GetLines().empty() )
            return false;
    }

    {
        const_cast<SwTable*>(&rCpyTable)->SwitchFormulasToRelativeRepresentation();
    }

    // Delete the Frames
    aFndBox.SetTableLines( *this );
    //Not dispose accessible table
    aFndBox.DelFrames( *this );

    if( 1 == rCpyTable.GetTabSortBoxes().size() )
    {
        SwTableBox *pTmpBx = rCpyTable.GetTabSortBoxes()[0];
        for (size_t n = 0; n < rSelBoxes.size(); ++n)
        {
            lcl_CpyBox( rCpyTable, pTmpBx, *this,
                        rSelBoxes[n], true, pUndo );
        }
    }
    else
        for (FndLines_t::size_type nLn = 0; nLn < aFndBox.GetLines().size(); ++nLn)
        {
            pFLine = aFndBox.GetLines()[ nLn ].get();
            SwTableLine* pCpyLn = rCpyTable.GetTabLines()[
                                nLn % rCpyTable.GetTabLines().size() ];
            for (FndBoxes_t::size_type nBx = 0; nBx < pFLine->GetBoxes().size(); ++nBx)
            {
                // Copy the pCpyBox into pMyBox
                lcl_CpyBox( rCpyTable, pCpyLn->GetTabBoxes()[
                            nBx % pCpyLn->GetTabBoxes().size() ],
                    *this, pFLine->GetBoxes()[nBx]->GetBox(), true, pUndo );
            }
        }

    aFndBox.MakeFrames( *this );
    return true;
}

static void FndContentLine( const SwTableLine* pLine, SwSelBoxes* pPara );

static void FndContentBox( const SwTableBox* pBox, SwSelBoxes* pPara )
{
    if( !pBox->GetTabLines().empty() )
    {
        for( const SwTableLine* pLine : pBox->GetTabLines() )
            FndContentLine( pLine, pPara );
    }
    else
        pPara->insert( const_cast<SwTableBox*>(pBox) );
}

static void FndContentLine( const SwTableLine* pLine, SwSelBoxes* pPara )
{
    for( const SwTableBox* pBox : pLine->GetTabBoxes() )
        FndContentBox(pBox, pPara );
}

// Find all Boxes with content in this Box
SwSelBoxes& SwTable::SelLineFromBox( const SwTableBox* pBox,
                                    SwSelBoxes& rBoxes, bool bToTop )
{
    SwTableLine* pLine = const_cast<SwTableLine*>(pBox->GetUpper());
    if( bToTop )
        while( pLine->GetUpper() )
            pLine = pLine->GetUpper()->GetUpper();

    // Delete all old ones
    rBoxes.clear();
    for( const auto& rpBox : pLine->GetTabBoxes() )
        FndContentBox(rpBox, &rBoxes );
    return rBoxes;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
