/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <memory>
#include <document.hxx>
#include <clipcontext.hxx>
#include <clipparam.hxx>
#include <table.hxx>
#include <tokenarray.hxx>
#include <listenercontext.hxx>
#include <tokenstringcontext.hxx>
#include <poolhelp.hxx>
#include <cellvalues.hxx>
#include <docpool.hxx>
#include <columniterator.hxx>

#include <refupdatecontext.hxx>
#include <sal/log.hxx>
#include <svx/DocumentColorHelper.hxx>
#include <scitems.hxx>
#include <datamapper.hxx>
#include <docsh.hxx>
#include <bcaslot.hxx>
#include <broadcast.hxx>

// Add totally brand-new methods to this source file.

bool ScDocument::IsMerged( const ScAddress& rPos ) const
{
    const ScTable* pTab = FetchTable(rPos.Tab());
    if (!pTab)
        return false;

    return pTab->IsMerged(rPos.Col(), rPos.Row());
}

sc::MultiDataCellState ScDocument::HasMultipleDataCells( const ScRange& rRange ) const
{
    if (rRange.aStart.Tab() != rRange.aEnd.Tab())
        // Currently we only support a single-sheet range.
        return sc::MultiDataCellState();

    const ScTable* pTab = FetchTable(rRange.aStart.Tab());
    if (!pTab)
        return sc::MultiDataCellState(sc::MultiDataCellState::Empty);

    const ScAddress& s = rRange.aStart;
    const ScAddress& e = rRange.aEnd;
    return pTab->HasMultipleDataCells(s.Col(), s.Row(), e.Col(), e.Row());
}

void ScDocument::DeleteBeforeCopyFromClip(
    sc::CopyFromClipContext& rCxt, const ScMarkData& rMark, sc::ColumnSpanSet& rBroadcastSpans )
{
    SCTAB nClipTab = 0;
    std::vector<ScTableUniquePtr> const& rClipTabs = rCxt.getClipDoc()->maTabs;
    SCTAB nClipTabCount = rClipTabs.size();

    for (SCTAB nTab = rCxt.getTabStart(); nTab <= rCxt.getTabEnd(); ++nTab)
    {
        ScTable* pTab = FetchTable(nTab);
        if (!pTab)
            continue;

        if (!rMark.GetTableSelect(nTab))
            continue;

        while (!rClipTabs[nClipTab])
            nClipTab = (nClipTab+1) % nClipTabCount;

        pTab->DeleteBeforeCopyFromClip(rCxt, *rClipTabs[nClipTab], rBroadcastSpans);

        nClipTab = (nClipTab+1) % nClipTabCount;
    }
}

bool ScDocument::CopyOneCellFromClip(
    sc::CopyFromClipContext& rCxt, SCCOL nCol1, SCROW nRow1, SCCOL nCol2, SCROW nRow2 )
{
    ScDocument* pClipDoc = rCxt.getClipDoc();
    if (pClipDoc->GetClipParam().mbCutMode)
        // We don't handle cut and paste or moving of cells here.
        return false;

    ScRange aClipRange = pClipDoc->GetClipParam().getWholeRange();
    if (aClipRange.aStart.Row() != aClipRange.aEnd.Row())
        // The source is not really a single row. Bail out.
        return false;

    SCCOL nSrcColSize = aClipRange.aEnd.Col() - aClipRange.aStart.Col() + 1;
    SCCOL nDestColSize = nCol2 - nCol1 + 1;
    if (nDestColSize < nSrcColSize)
        return false;

    if (pClipDoc->maTabs.size() > 1)
        // Copying from multiple source sheets is not handled here.
        return false;

    ScAddress aSrcPos = aClipRange.aStart;

    for (SCCOL nCol = aClipRange.aStart.Col(); nCol <= aClipRange.aEnd.Col(); ++nCol)
    {
        ScAddress aTestPos = aSrcPos;
        aTestPos.SetCol(nCol);
        if (pClipDoc->IsMerged(aTestPos))
            // We don't handle merged source cell for this.
            return false;
    }

    ScTable* pSrcTab = pClipDoc->FetchTable(aSrcPos.Tab());
    if (!pSrcTab)
        return false;

    rCxt.setSingleCellColumnSize(nSrcColSize);

    for (SCCOL nColOffset = 0; nColOffset < nSrcColSize; ++nColOffset, aSrcPos.IncCol())
    {
        const ScPatternAttr* pAttr = pClipDoc->GetPattern(aSrcPos);
        rCxt.setSingleCellPattern(nColOffset, pAttr);

        if ((rCxt.getInsertFlag() & (InsertDeleteFlags::NOTE | InsertDeleteFlags::ADDNOTES)) != InsertDeleteFlags::NONE)
            rCxt.setSingleCellNote(nColOffset, pClipDoc->GetNote(aSrcPos));

        if ((rCxt.getInsertFlag() & InsertDeleteFlags::SPARKLINES) != InsertDeleteFlags::NONE)
            rCxt.setSingleSparkline(nColOffset, pClipDoc->GetSparkline(aSrcPos));

        ScColumn* pSrcCol = pSrcTab->FetchColumn(aSrcPos.Col());
        assert(pSrcCol);
        // Determine the script type of the copied single cell.
        pSrcCol->UpdateScriptTypes(aSrcPos.Row(), aSrcPos.Row());
        rCxt.setSingleCell(aSrcPos, *pSrcCol);
    }

    // All good. Proceed with the pasting.

    SCTAB nTabEnd = rCxt.getTabEnd();
    for (SCTAB i = rCxt.getTabStart(); i <= nTabEnd && i < GetTableCount(); ++i)
    {
        maTabs[i]->CopyOneCellFromClip(rCxt, nCol1, nRow1, nCol2, nRow2,  aClipRange.aStart.Row(), pSrcTab);
    }

    sc::RefUpdateContext aRefCxt(*this);
    aRefCxt.maRange = ScRange(nCol1, nRow1, rCxt.getTabStart(), nCol2, nRow2, nTabEnd);
    aRefCxt.mnColDelta = nCol1 - aSrcPos.Col();
    aRefCxt.mnRowDelta = nRow1 - aSrcPos.Row();
    aRefCxt.mnTabDelta = rCxt.getTabStart() - aSrcPos.Tab();
    // Only Copy&Paste, for Cut&Paste we already bailed out early.
    aRefCxt.meMode = URM_COPY;
    UpdateReference(aRefCxt, rCxt.getUndoDoc(), false);

    return true;
}

void ScDocument::SetValues( const ScAddress& rPos, const std::vector<double>& rVals )
{
    ScTable* pTab = FetchTable(rPos.Tab());
    if (!pTab)
        return;

    pTab->SetValues(rPos.Col(), rPos.Row(), rVals);
}

void ScDocument::TransferCellValuesTo( const ScAddress& rTopPos, size_t nLen, sc::CellValues& rDest )
{
    ScTable* pTab = FetchTable(rTopPos.Tab());
    if (!pTab)
        return;

    pTab->TransferCellValuesTo(rTopPos.Col(), rTopPos.Row(), nLen, rDest);
}

void ScDocument::CopyCellValuesFrom( const ScAddress& rTopPos, const sc::CellValues& rSrc )
{
    ScTable* pTab = FetchTable(rTopPos.Tab());
    if (!pTab)
        return;

    pTab->CopyCellValuesFrom(rTopPos.Col(), rTopPos.Row(), rSrc);
}

std::set<Color> ScDocument::GetDocColors()
{
    std::set<Color> aDocColors;
    ScDocumentPool *pPool = GetPool();

    svx::DocumentColorHelper::queryColors<SvxBrushItem>(ATTR_BACKGROUND, pPool, aDocColors);
    svx::DocumentColorHelper::queryColors<SvxColorItem>(ATTR_FONT_COLOR, pPool, aDocColors);

    return aDocColors;
}

void ScDocument::SetCalcConfig( const ScCalcConfig& rConfig )
{
    ScMutationGuard aGuard(*this, ScMutationGuardFlags::CORE);
    maCalcConfig = rConfig;
}

void ScDocument::ConvertFormulaToValue( const ScRange& rRange, sc::TableValues* pUndo )
{
    sc::EndListeningContext aCxt(*this);

    for (SCTAB nTab = rRange.aStart.Tab(); nTab <= rRange.aEnd.Tab(); ++nTab)
    {
        ScTable* pTab = FetchTable(nTab);
        if (!pTab)
            continue;

        pTab->ConvertFormulaToValue(
            aCxt, rRange.aStart.Col(), rRange.aStart.Row(), rRange.aEnd.Col(), rRange.aEnd.Row(),
            pUndo);
    }

    aCxt.purgeEmptyBroadcasters();
}

void ScDocument::SwapNonEmpty( sc::TableValues& rValues )
{
    const ScRange& rRange = rValues.getRange();
    if (!rRange.IsValid())
        return;

    const auto pPosSet = std::make_shared<sc::ColumnBlockPositionSet>(*this);
    sc::StartListeningContext aStartCxt(*this, pPosSet);
    sc::EndListeningContext aEndCxt(*this, pPosSet);

    for (SCTAB nTab = rRange.aStart.Tab(); nTab <= rRange.aEnd.Tab(); ++nTab)
    {
        ScTable* pTab = FetchTable(nTab);
        if (!pTab)
            continue;

        pTab->SwapNonEmpty(rValues, aStartCxt, aEndCxt);
    }

    aEndCxt.purgeEmptyBroadcasters();
}

void ScDocument::PreprocessAllRangeNamesUpdate( const std::map<OUString, ScRangeName>& rRangeMap )
{
    // Update all existing names with new names.
    // The prerequisites are that the name dialog preserves ScRangeData index
    // for changes and does not reuse free index slots for new names.
    // ScDocument::SetAllRangeNames() hereafter then will replace the
    // ScRangeName containers of ScRangeData instances with empty
    // ScRangeData::maNewName.
    std::map<OUString, ScRangeName*> aRangeNameMap;
    GetRangeNameMap( aRangeNameMap);
    for (const auto& itTab : aRangeNameMap)
    {
        ScRangeName* pOldRangeNames = itTab.second;
        if (!pOldRangeNames)
            continue;

        const auto itNewTab( rRangeMap.find( itTab.first));
        if (itNewTab == rRangeMap.end())
            continue;

        const ScRangeName& rNewRangeNames = itNewTab->second;

        for (const auto& rEntry : *pOldRangeNames)
        {
            ScRangeData* pOldData = rEntry.second.get();
            if (!pOldData)
                continue;

            const ScRangeData* pNewData = rNewRangeNames.findByIndex( pOldData->GetIndex());
            if (pNewData)
                pOldData->SetNewName( pNewData->GetName());
        }
    }

    sc::EndListeningContext aEndListenCxt(*this);
    sc::CompileFormulaContext aCompileCxt(*this);

    for (const auto& rxTab : maTabs)
    {
        ScTable* p = rxTab.get();
        p->PreprocessRangeNameUpdate(aEndListenCxt, aCompileCxt);
    }
}

void ScDocument::PreprocessRangeNameUpdate()
{
    sc::EndListeningContext aEndListenCxt(*this);
    sc::CompileFormulaContext aCompileCxt(*this);

    for (const auto& rxTab : maTabs)
    {
        ScTable* p = rxTab.get();
        p->PreprocessRangeNameUpdate(aEndListenCxt, aCompileCxt);
    }
}

void ScDocument::PreprocessDBDataUpdate()
{
    sc::EndListeningContext aEndListenCxt(*this);
    sc::CompileFormulaContext aCompileCxt(*this);

    for (const auto& rxTab : maTabs)
    {
        ScTable* p = rxTab.get();
        p->PreprocessDBDataUpdate(aEndListenCxt, aCompileCxt);
    }
}

void ScDocument::CompileHybridFormula()
{
    sc::StartListeningContext aStartListenCxt(*this);
    sc::CompileFormulaContext aCompileCxt(*this);
    for (const auto& rxTab : maTabs)
    {
        ScTable* p = rxTab.get();
        p->CompileHybridFormula(aStartListenCxt, aCompileCxt);
    }
}

void ScDocument::SharePooledResources( const ScDocument* pSrcDoc )
{
    ScMutationGuard aGuard(*this, ScMutationGuardFlags::CORE);
    mxPoolHelper = pSrcDoc->mxPoolHelper;
    mpCellStringPool = pSrcDoc->mpCellStringPool;

    // force lazy creation/existence in source document *before* sharing
    pSrcDoc->getCellAttributeHelper();
    mpCellAttributeHelper = pSrcDoc->mpCellAttributeHelper;
}

void ScDocument::UpdateScriptTypes( const ScAddress& rPos, SCCOL nColSize, SCROW nRowSize )
{
    ScTable* pTab = FetchTable(rPos.Tab());
    if (!pTab)
        return;

    pTab->UpdateScriptTypes(rPos.Col(), rPos.Row(), rPos.Col()+nColSize-1, rPos.Row()+nRowSize-1);
}

bool ScDocument::HasUniformRowHeight( SCTAB nTab, SCROW nRow1, SCROW nRow2 ) const
{
    const ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return false;

    return pTab->HasUniformRowHeight(nRow1, nRow2);
}

void ScDocument::UnshareFormulaCells( SCTAB nTab, SCCOL nCol, std::vector<SCROW>& rRows )
{
    ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return;

    pTab->UnshareFormulaCells(nCol, rRows);
}

void ScDocument::RegroupFormulaCells( SCTAB nTab, SCCOL nCol )
{
    ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return;

    pTab->RegroupFormulaCells(nCol);
}

void ScDocument::RegroupFormulaCells( const ScRange& rRange )
{
    for( SCTAB tab = rRange.aStart.Tab(); tab <= rRange.aEnd.Tab(); ++tab )
        for( SCCOL col = rRange.aStart.Col(); col <= rRange.aEnd.Col(); ++col )
            RegroupFormulaCells( tab, col );
}

void ScDocument::DelayFormulaGrouping( bool delay )
{
    if( delay )
    {
        if( !pDelayedFormulaGrouping )
            pDelayedFormulaGrouping.reset( new ScRange( ScAddress::INITIALIZE_INVALID ));
    }
    else
    {
        if( pDelayedFormulaGrouping && pDelayedFormulaGrouping->IsValid())
            RegroupFormulaCells( *pDelayedFormulaGrouping );
        pDelayedFormulaGrouping.reset();
    }
}

void ScDocument::AddDelayedFormulaGroupingCell( const ScFormulaCell* cell )
{
    if( !pDelayedFormulaGrouping->Contains( cell->aPos ))
        pDelayedFormulaGrouping->ExtendTo( ScRange(cell->aPos) );
}

void ScDocument::EnableDelayStartListeningFormulaCells( ScColumn* column, bool delay )
{
    if( delay )
    {
        if( pDelayedStartListeningFormulaCells.find( column ) == pDelayedStartListeningFormulaCells.end())
            pDelayedStartListeningFormulaCells[ column ] = std::pair<SCROW, SCROW>( -1, -1 );
    }
    else
    {
        auto it = pDelayedStartListeningFormulaCells.find( column );
        if( it != pDelayedStartListeningFormulaCells.end())
        {
            if( it->second.first != -1 )
            {
                auto xPosSet = std::make_shared<sc::ColumnBlockPositionSet>(*this);
                sc::StartListeningContext aStartCxt(*this, xPosSet);
                sc::EndListeningContext aEndCxt(*this, std::move(xPosSet));
                column->StartListeningFormulaCells(aStartCxt, aEndCxt, it->second.first, it->second.second);
            }
            pDelayedStartListeningFormulaCells.erase( it );
        }
    }
}

bool ScDocument::IsEnabledDelayStartListeningFormulaCells( ScColumn* column ) const
{
    return pDelayedStartListeningFormulaCells.find( column ) != pDelayedStartListeningFormulaCells.end();
}

bool ScDocument::CanDelayStartListeningFormulaCells( ScColumn* column, SCROW row1, SCROW row2 )
{
    auto it = pDelayedStartListeningFormulaCells.find( column );
    if( it == pDelayedStartListeningFormulaCells.end())
        return false; // not enabled
    if( it->second.first == -1 && it->second.second == -1 ) // uninitialized
        pDelayedStartListeningFormulaCells[ column ] = std::make_pair( row1, row2 );
    else
    {
        if( row1 > it->second.second + 1 || row2 < it->second.first - 1 )
        { // two non-adjacent ranges, just bail out
            return false;
        }
        it->second.first = std::min( it->second.first, row1 );
        it->second.second = std::max( it->second.second, row2 );
    }
    return true;
}

void ScDocument::EnableDelayDeletingBroadcasters( bool set )
{
    if( bDelayedDeletingBroadcasters == set )
        return;
    bDelayedDeletingBroadcasters = set;
    if( !bDelayedDeletingBroadcasters )
    {
        for (auto& rxTab : maTabs)
            if (rxTab)
                rxTab->DeleteEmptyBroadcasters();
    }
}

bool ScDocument::HasFormulaCell( const ScRange& rRange ) const
{
    if (!rRange.IsValid())
        return false;

    for (SCTAB nTab = rRange.aStart.Tab(); nTab <= rRange.aEnd.Tab(); ++nTab)
    {
        const ScTable* pTab = FetchTable(nTab);
        if (!pTab)
            continue;

        if (pTab->HasFormulaCell(rRange.aStart.Col(), rRange.aStart.Row(), rRange.aEnd.Col(), rRange.aEnd.Row()))
            return true;
    }

    return false;
}

void ScDocument::EndListeningIntersectedGroup(
    sc::EndListeningContext& rCxt, const ScAddress& rPos, std::vector<ScAddress>* pGroupPos )
{
    ScTable* pTab = FetchTable(rPos.Tab());
    if (!pTab)
        return;

    pTab->EndListeningIntersectedGroup(rCxt, rPos.Col(), rPos.Row(), pGroupPos);
}

void ScDocument::EndListeningIntersectedGroups(
    sc::EndListeningContext& rCxt, const ScRange& rRange, std::vector<ScAddress>* pGroupPos )
{
    for (SCTAB nTab = rRange.aStart.Tab(); nTab <= rRange.aEnd.Tab(); ++nTab)
    {
        ScTable* pTab = FetchTable(nTab);
        if (!pTab)
            continue;

        pTab->EndListeningIntersectedGroups(
            rCxt, rRange.aStart.Col(), rRange.aStart.Row(), rRange.aEnd.Col(), rRange.aEnd.Row(),
            pGroupPos);
    }
}

void ScDocument::EndListeningGroups( const std::vector<ScAddress>& rPosArray )
{
    sc::EndListeningContext aCxt(*this);
    for (const ScAddress& rPos : rPosArray)
    {
        ScTable* pTab = FetchTable(rPos.Tab());
        if (!pTab)
            return;

        pTab->EndListeningGroup(aCxt, rPos.Col(), rPos.Row());
    }

    aCxt.purgeEmptyBroadcasters();
}

void ScDocument::SetNeedsListeningGroups( const std::vector<ScAddress>& rPosArray )
{
    for (const ScAddress& rPos : rPosArray)
    {
        ScTable* pTab = FetchTable(rPos.Tab());
        if (!pTab)
            return;

        pTab->SetNeedsListeningGroup(rPos.Col(), rPos.Row());
    }
}

namespace {

class StartNeededListenersHandler
{
    std::shared_ptr<sc::StartListeningContext> mpCxt;
public:
    explicit StartNeededListenersHandler( ScDocument& rDoc ) : mpCxt(std::make_shared<sc::StartListeningContext>(rDoc)) {}
    explicit StartNeededListenersHandler( ScDocument& rDoc, const std::shared_ptr<const sc::ColumnSet>& rpColSet ) :
        mpCxt(std::make_shared<sc::StartListeningContext>(rDoc))
    {
        mpCxt->setColumnSet( rpColSet);
    }

    void operator() (const ScTableUniquePtr & p)
    {
        if (p)
            p->StartListeners(*mpCxt, false);
    }
};

}

void ScDocument::StartNeededListeners()
{
    std::for_each(maTabs.begin(), maTabs.end(), StartNeededListenersHandler(*this));
}

void ScDocument::StartNeededListeners( const std::shared_ptr<const sc::ColumnSet>& rpColSet )
{
    std::for_each(maTabs.begin(), maTabs.end(), StartNeededListenersHandler(*this, rpColSet));
}

void ScDocument::StartAllListeners( const ScRange& rRange )
{
    if (IsClipOrUndo() || GetNoListening())
        return;

    const auto pPosSet = std::make_shared<sc::ColumnBlockPositionSet>(*this);
    sc::StartListeningContext aStartCxt(*this, pPosSet);
    sc::EndListeningContext aEndCxt(*this, pPosSet);

    for (SCTAB nTab = rRange.aStart.Tab(); nTab <= rRange.aEnd.Tab(); ++nTab)
    {
        ScTable* pTab = FetchTable(nTab);
        if (!pTab)
            continue;

        pTab->StartListeningFormulaCells(
            aStartCxt, aEndCxt,
            rRange.aStart.Col(), rRange.aStart.Row(), rRange.aEnd.Col(), rRange.aEnd.Row());
    }
}

void ScDocument::finalizeOutlineImport()
{
    for (const auto& rxTab : maTabs)
    {
        ScTable* p = rxTab.get();
        p->finalizeOutlineImport();
    }
}

bool ScDocument::FindRangeNamesReferencingSheet( sc::UpdatedRangeNames& rIndexes,
        SCTAB nTokenTab, const sal_uInt16 nTokenIndex,
        SCTAB nGlobalRefTab, SCTAB nLocalRefTab, SCTAB nOldTokenTab, SCTAB nOldTokenTabReplacement,
        bool bSameDoc, int nRecursion) const
{
    if (nTokenTab < -1)
    {
        SAL_WARN("sc.core", "ScDocument::FindRangeNamesReferencingSheet - nTokenTab < -1 : " <<
                nTokenTab << ", nTokenIndex " << nTokenIndex << " Fix the creator!");
#if OSL_DEBUG_LEVEL > 0
        const ScRangeData* pData = FindRangeNameBySheetAndIndex( nTokenTab, nTokenIndex);
        SAL_WARN_IF( pData, "sc.core", "ScDocument::FindRangeNamesReferencingSheet - named expression is: " << pData->GetName());
#endif
        nTokenTab = -1;
    }
    SCTAB nRefTab = nGlobalRefTab;
    if (nTokenTab == nOldTokenTab)
    {
        nTokenTab = nOldTokenTabReplacement;
        nRefTab = nLocalRefTab;
    }
    else if (nTokenTab == nOldTokenTabReplacement)
    {
        nRefTab = nLocalRefTab;
    }

    if (rIndexes.isNameUpdated( nTokenTab, nTokenIndex))
        return true;

    ScRangeData* pData = FindRangeNameBySheetAndIndex( nTokenTab, nTokenIndex);
    if (!pData)
        return false;

    ScTokenArray* pCode = pData->GetCode();
    if (!pCode)
        return false;

    bool bRef = !bSameDoc;  // include every name used when copying to other doc
    if (nRecursion < 126)   // whatever... 42*3
    {
        formula::FormulaTokenArrayPlainIterator aIter(*pCode);
        for (const formula::FormulaToken* p = aIter.First(); p; p = aIter.Next())
        {
            if (p->GetOpCode() == ocName)
            {
                bRef |= FindRangeNamesReferencingSheet( rIndexes, p->GetSheet(), p->GetIndex(),
                        nGlobalRefTab, nLocalRefTab, nOldTokenTab, nOldTokenTabReplacement, bSameDoc, nRecursion+1);
            }
        }
    }

    if (!bRef)
    {
        SCTAB nPosTab = pData->GetPos().Tab();
        if (nPosTab == nOldTokenTab)
            nPosTab = nOldTokenTabReplacement;
        bRef = pCode->ReferencesSheet( nRefTab, nPosTab);
    }
    if (bRef)
        rIndexes.setUpdatedName( nTokenTab, nTokenIndex);

    return bRef;
}

namespace {

enum MightReferenceSheet
{
    UNKNOWN,
    NONE,
    CODE,
    NAME
};

MightReferenceSheet mightRangeNameReferenceSheet( ScRangeData* pData, SCTAB nRefTab)
{
    ScTokenArray* pCode = pData->GetCode();
    if (!pCode)
        return MightReferenceSheet::NONE;

    formula::FormulaTokenArrayPlainIterator aIter(*pCode);
    for (const formula::FormulaToken* p = aIter.First(); p; p = aIter.Next())
    {
        if (p->GetOpCode() == ocName)
            return MightReferenceSheet::NAME;
    }

    return pCode->ReferencesSheet( nRefTab, pData->GetPos().Tab()) ?
        MightReferenceSheet::CODE : MightReferenceSheet::NONE;
}

ScRangeData* copyRangeName( const ScRangeData* pOldRangeData, ScDocument& rNewDoc, const ScDocument& rOldDoc,
        const ScAddress& rNewPos, const ScAddress& rOldPos, bool bGlobalNamesToLocal,
        SCTAB nOldSheet, const SCTAB nNewSheet, bool bSameDoc)
{
    ScAddress aRangePos( pOldRangeData->GetPos());
    if (nNewSheet >= 0)
        aRangePos.SetTab( nNewSheet);
    ScRangeData* pRangeData = new ScRangeData(*pOldRangeData, &rNewDoc, &aRangePos);
    pRangeData->SetIndex(0);    // needed for insert to assign a new index
    ScTokenArray* pRangeNameToken = pRangeData->GetCode();
    if (bSameDoc && nNewSheet >= 0)
    {
        if (bGlobalNamesToLocal && nOldSheet < 0)
        {
            nOldSheet = rOldPos.Tab();
            if (rNewPos.Tab() <= nOldSheet)
                // Sheet was inserted before and references already updated.
                ++nOldSheet;
        }
        pRangeNameToken->AdjustSheetLocalNameReferences( nOldSheet, nNewSheet);
    }
    if (!bSameDoc)
    {
        pRangeNameToken->ReadjustAbsolute3DReferences(rOldDoc, rNewDoc, pRangeData->GetPos(), true);
        pRangeNameToken->AdjustAbsoluteRefs(rOldDoc, rOldPos, rNewPos, true);
    }

    bool bInserted;
    if (nNewSheet < 0)
        bInserted = rNewDoc.GetRangeName()->insert(pRangeData);
    else
        bInserted = rNewDoc.GetRangeName(nNewSheet)->insert(pRangeData);

    return bInserted ? pRangeData : nullptr;
}

struct SheetIndex
{
    SCTAB       mnSheet;
    sal_uInt16  mnIndex;

    SheetIndex( SCTAB nSheet, sal_uInt16 nIndex ) : mnSheet(nSheet < -1 ? -1 : nSheet), mnIndex(nIndex) {}
    bool operator<( const SheetIndex& r ) const
    {
        // Ascending order sheet, index
        if (mnSheet < r.mnSheet)
            return true;
        if (mnSheet == r.mnSheet)
            return mnIndex < r.mnIndex;
        return false;
    }
};
typedef std::map< SheetIndex, SheetIndex > SheetIndexMap;

ScRangeData* copyRangeNames( SheetIndexMap& rSheetIndexMap, std::vector<ScRangeData*>& rRangeDataVec,
        const sc::UpdatedRangeNames& rReferencingNames, SCTAB nTab,
        const ScRangeData* pOldRangeData, ScDocument& rNewDoc, const ScDocument& rOldDoc,
        const ScAddress& rNewPos, const ScAddress& rOldPos, bool bGlobalNamesToLocal,
        const SCTAB nOldSheet, const SCTAB nNewSheet, bool bSameDoc)
{
    ScRangeData* pRangeData = nullptr;
    const ScRangeName* pOldRangeName = (nTab < 0 ? rOldDoc.GetRangeName() : rOldDoc.GetRangeName(nTab));
    if (pOldRangeName)
    {
        const ScRangeName* pNewRangeName = (nNewSheet < 0 ? rNewDoc.GetRangeName() : rNewDoc.GetRangeName(nNewSheet));
        sc::UpdatedRangeNames::NameIndicesType aSet( rReferencingNames.getUpdatedNames(nTab));
        for (auto const & rIndex : aSet)
        {
            const ScRangeData* pCopyData = pOldRangeName->findByIndex(rIndex);
            if (pCopyData)
            {
                // Match the original pOldRangeData to adapt the current
                // token's values later. For that no check for an already
                // copied name is needed as we only enter here if there was
                // none.
                if (pCopyData == pOldRangeData)
                {
                    pRangeData = copyRangeName( pCopyData, rNewDoc, rOldDoc, rNewPos, rOldPos,
                            bGlobalNamesToLocal, nOldSheet, nNewSheet, bSameDoc);
                    if (pRangeData)
                    {
                        rRangeDataVec.push_back(pRangeData);
                        rSheetIndexMap.insert( std::make_pair( SheetIndex( nOldSheet, pCopyData->GetIndex()),
                                    SheetIndex( nNewSheet, pRangeData->GetIndex())));
                    }
                }
                else
                {
                    // First check if the name is already available as copy.
                    const ScRangeData* pFoundData = pNewRangeName->findByUpperName( pCopyData->GetUpperName());
                    if (pFoundData)
                    {
                        // Just add the resulting sheet/index mapping.
                        rSheetIndexMap.insert( std::make_pair( SheetIndex( nOldSheet, pCopyData->GetIndex()),
                                    SheetIndex( nNewSheet, pFoundData->GetIndex())));
                    }
                    else
                    {
                        ScRangeData* pTmpData = copyRangeName( pCopyData, rNewDoc, rOldDoc, rNewPos, rOldPos,
                                bGlobalNamesToLocal, nOldSheet, nNewSheet, bSameDoc);
                        if (pTmpData)
                        {
                            rRangeDataVec.push_back(pTmpData);
                            rSheetIndexMap.insert( std::make_pair( SheetIndex( nOldSheet, pCopyData->GetIndex()),
                                        SheetIndex( nNewSheet, pTmpData->GetIndex())));
                        }
                    }
                }
            }
        }
    }
    return pRangeData;
}

}   // namespace

bool ScDocument::CopyAdjustRangeName( SCTAB& rSheet, sal_uInt16& rIndex, ScRangeData*& rpRangeData,
        ScDocument& rNewDoc, const ScAddress& rNewPos, const ScAddress& rOldPos, const bool bGlobalNamesToLocal,
        const bool bUsedByFormula ) const
{
    ScDocument* pThis = const_cast<ScDocument*>(this);
    const bool bSameDoc = (rNewDoc.GetPool() == pThis->GetPool());
    if (bSameDoc && ((rSheet < 0 && !bGlobalNamesToLocal) || (rSheet >= 0
                    && (rSheet != rOldPos.Tab() || (IsClipboard() && pThis->IsCutMode())))))
        // Same doc and global name, if not copied to local name, or
        // sheet-local name on other sheet stays the same. Sheet-local on
        // same sheet also in a clipboard cut&paste / move operation.
        return false;

    // Ensure we don't fiddle with the references until exit.
    const SCTAB nOldSheet = rSheet;
    const sal_uInt16 nOldIndex = rIndex;

    SAL_WARN_IF( !bSameDoc && nOldSheet >= 0 && nOldSheet != rOldPos.Tab(),
            "sc.core", "adjustCopyRangeName - sheet-local name was on other sheet in other document");
    /* TODO: can we do something about that? e.g. loop over sheets? */

    OUString aRangeName;
    ScRangeData* pOldRangeData = nullptr;

    // XXX bGlobalNamesToLocal is also a synonym for copied sheet.
    bool bInsertingBefore = (bGlobalNamesToLocal && bSameDoc && rNewPos.Tab() <= rOldPos.Tab());

    // The Tab where an old local name is to be found or that a global name
    // references. May differ below from nOldSheet if a sheet was inserted
    // before the old position. Global names and local names other than on the
    // old sheet or new sheet are already updated, local names on the old sheet
    // or inserted sheet will be updated later. Confusing stuff. Watch out.
    SCTAB nOldTab = (nOldSheet < 0 ? rOldPos.Tab() : nOldSheet);
    if (bInsertingBefore)
        // Sheet was already inserted before old position.
        ++nOldTab;

    // Search the name of the RangeName.
    if (nOldSheet >= 0)
    {
        const ScRangeName* pNames = GetRangeName(nOldTab);
        pOldRangeData = pNames ? pNames->findByIndex(nOldIndex) : nullptr;
        if (!pOldRangeData)
            return false;     // might be an error in the formula array
        aRangeName = pOldRangeData->GetUpperName();
    }
    else
    {
        pOldRangeData = GetRangeName()->findByIndex(nOldIndex);
        if (!pOldRangeData)
            return false;     // might be an error in the formula array
        aRangeName = pOldRangeData->GetUpperName();
    }

    // Find corresponding range name in new document.
    // First search for local range name then global range names.
    SCTAB nNewSheet = rNewPos.Tab();
    ScRangeName* pNewNames = rNewDoc.GetRangeName(nNewSheet);
    // Search local range names.
    if (pNewNames)
    {
        rpRangeData = pNewNames->findByUpperName(aRangeName);
    }
    // Search global range names.
    if (!rpRangeData && !bGlobalNamesToLocal)
    {
        nNewSheet = -1;
        pNewNames = rNewDoc.GetRangeName();
        if (pNewNames)
            rpRangeData = pNewNames->findByUpperName(aRangeName);
    }
    // If no range name was found copy it.
    if (!rpRangeData)
    {
        // Do not copy global name if it doesn't reference sheet or is not used
        // by a formula copied to another document.
        bool bEarlyBailOut = (nOldSheet < 0 && (bSameDoc || !bUsedByFormula));
        MightReferenceSheet eMightReference = mightRangeNameReferenceSheet( pOldRangeData, nOldTab);
        if (bEarlyBailOut && eMightReference == MightReferenceSheet::NONE)
            return false;

        if (eMightReference == MightReferenceSheet::NAME)
        {
            // Name these to clarify what is passed where.
            const SCTAB nGlobalRefTab = nOldTab;
            const SCTAB nLocalRefTab = (bInsertingBefore ? nOldTab-1 : nOldTab);
            const SCTAB nOldTokenTab = (nOldSheet < 0 ? (bInsertingBefore ? nOldTab-1 : nOldTab) : nOldSheet);
            const SCTAB nOldTokenTabReplacement = nOldTab;
            sc::UpdatedRangeNames aReferencingNames;
            FindRangeNamesReferencingSheet( aReferencingNames, nOldSheet, nOldIndex,
                    nGlobalRefTab, nLocalRefTab, nOldTokenTab, nOldTokenTabReplacement, bSameDoc, 0);
            if (bEarlyBailOut && aReferencingNames.isEmpty(-1) && aReferencingNames.isEmpty(nOldTokenTabReplacement))
                return false;

            SheetIndexMap aSheetIndexMap;
            std::vector<ScRangeData*> aRangeDataVec;
            if (!aReferencingNames.isEmpty(nOldTokenTabReplacement))
            {
                const SCTAB nTmpOldSheet = (nOldSheet < 0 ? nOldTab : nOldSheet);
                nNewSheet = rNewPos.Tab();
                rpRangeData = copyRangeNames( aSheetIndexMap, aRangeDataVec, aReferencingNames, nOldTab,
                        pOldRangeData, rNewDoc, *this, rNewPos, rOldPos,
                        bGlobalNamesToLocal, nTmpOldSheet, nNewSheet, bSameDoc);
            }
            if ((bGlobalNamesToLocal || !bSameDoc) && !aReferencingNames.isEmpty(-1))
            {
                const SCTAB nTmpOldSheet = -1;
                const SCTAB nTmpNewSheet = (bGlobalNamesToLocal ? rNewPos.Tab() : -1);
                ScRangeData* pTmpData = copyRangeNames( aSheetIndexMap, aRangeDataVec, aReferencingNames, -1,
                        pOldRangeData, rNewDoc, *this, rNewPos, rOldPos,
                        bGlobalNamesToLocal, nTmpOldSheet, nTmpNewSheet, bSameDoc);
                if (!rpRangeData)
                {
                    rpRangeData = pTmpData;
                    nNewSheet = nTmpNewSheet;
                }
            }

            // Adjust copied nested names to new sheet/index.
            for (auto & iRD : aRangeDataVec)
            {
                ScTokenArray* pCode = iRD->GetCode();
                if (pCode)
                {
                    formula::FormulaTokenArrayPlainIterator aIter(*pCode);
                    for (formula::FormulaToken* p = aIter.First(); p; p = aIter.Next())
                    {
                        if (p->GetOpCode() == ocName)
                        {
                            auto it = aSheetIndexMap.find( SheetIndex( p->GetSheet(), p->GetIndex()));
                            if (it != aSheetIndexMap.end())
                            {
                                p->SetSheet( it->second.mnSheet);
                                p->SetIndex( it->second.mnIndex);
                            }
                            else if (!bSameDoc)
                            {
                                SAL_WARN("sc.core","adjustCopyRangeName - mapping to new name in other doc missing");
                                p->SetIndex(0);     // #NAME? error instead of arbitrary name.
                            }
                        }
                    }
                }
            }
        }
        else
        {
            nNewSheet = ((nOldSheet < 0 && !bGlobalNamesToLocal) ? -1 : rNewPos.Tab());
            rpRangeData = copyRangeName( pOldRangeData, rNewDoc, *this, rNewPos, rOldPos, bGlobalNamesToLocal,
                    nOldSheet, nNewSheet, bSameDoc);
        }

        if (rpRangeData && !rNewDoc.IsClipOrUndo())
        {
            ScDocShell* pDocSh = rNewDoc.GetDocumentShell();
            if (pDocSh)
                pDocSh->SetAreasChangedNeedBroadcast();
        }
    }

    rSheet = nNewSheet;
    rIndex = rpRangeData ? rpRangeData->GetIndex() : 0;     // 0 means not inserted
    return true;
}

bool ScDocument::IsEditActionAllowed(
    sc::EditAction eAction, SCTAB nTab, SCCOL nStartCol, SCROW nStartRow, SCCOL nEndCol, SCROW nEndRow ) const
{
    const ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return false;

    return pTab->IsEditActionAllowed(eAction, nStartCol, nStartRow, nEndCol, nEndRow);
}

bool ScDocument::IsEditActionAllowed(
    sc::EditAction eAction, const ScMarkData& rMark, SCCOL nStartCol, SCROW nStartRow, SCCOL nEndCol, SCROW nEndRow ) const
{
    return std::all_of(rMark.begin(), rMark.end(),
        [this, &eAction, &nStartCol, &nStartRow, &nEndCol, &nEndRow](const SCTAB& rTab)
        { return IsEditActionAllowed(eAction, rTab, nStartCol, nStartRow, nEndCol, nEndRow); });
}

std::optional<sc::ColumnIterator> ScDocument::GetColumnIterator( SCTAB nTab, SCCOL nCol, SCROW nRow1, SCROW nRow2 ) const
{
    const ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return {};

    return pTab->GetColumnIterator(nCol, nRow1, nRow2);
}

void ScDocument::CreateColumnIfNotExists( SCTAB nTab, SCCOL nCol )
{
    ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return;

    pTab->CreateColumnIfNotExists(nCol);
}

bool ScDocument::EnsureFormulaCellResults( const ScRange& rRange, bool bSkipRunning )
{
    bool bAnyDirty = false;
    for (SCTAB nTab = rRange.aStart.Tab(); nTab <= rRange.aEnd.Tab(); ++nTab)
    {
        ScTable* pTab = FetchTable(nTab);
        if (!pTab)
            continue;

        bool bRet = pTab->EnsureFormulaCellResults(
            rRange.aStart.Col(), rRange.aStart.Row(), rRange.aEnd.Col(), rRange.aEnd.Row(), bSkipRunning);
        bAnyDirty = bAnyDirty || bRet;
    }

    return bAnyDirty;
}

sc::ExternalDataMapper& ScDocument::GetExternalDataMapper()
{
    if (!mpDataMapper)
        mpDataMapper.reset(new sc::ExternalDataMapper(*this));

    return *mpDataMapper;
}

void ScDocument::StoreTabToCache(SCTAB nTab, SvStream& rStrm) const
{
    const ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return;

    pTab->StoreToCache(rStrm);
}

void ScDocument::RestoreTabFromCache(SCTAB nTab, SvStream& rStrm)
{
    ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return;

    pTab->RestoreFromCache(rStrm);
}

OString ScDocument::dumpSheetGeomData(SCTAB nTab, bool bColumns, SheetGeomType eGeomType)
{
    ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return ""_ostr;

    return pTab->dumpSheetGeomData(bColumns, eGeomType);
}

SCCOL ScDocument::GetLOKFreezeCol(SCTAB nTab) const
{
    const ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return -1;

    return pTab->GetLOKFreezeCol();
}
SCROW ScDocument::GetLOKFreezeRow(SCTAB nTab) const
{
    const ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return -1;

    return pTab->GetLOKFreezeRow();
}

bool ScDocument::SetLOKFreezeCol(SCCOL nFreezeCol, SCTAB nTab)
{
    ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return false;

    return pTab->SetLOKFreezeCol(nFreezeCol);
}

bool ScDocument::SetLOKFreezeRow(SCROW nFreezeRow, SCTAB nTab)
{
    ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return false;

    return pTab->SetLOKFreezeRow(nFreezeRow);
}

std::set<SCCOL> ScDocument::QueryColumnsWithFormulaCells( SCTAB nTab ) const
{
    const ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return std::set<SCCOL>{};

    return pTab->QueryColumnsWithFormulaCells();
}

void ScDocument::CheckIntegrity( SCTAB nTab ) const
{
    const ScTable* pTab = FetchTable(nTab);
    if (!pTab)
        return;

    pTab->CheckIntegrity();
}

sc::BroadcasterState ScDocument::GetBroadcasterState() const
{
    sc::BroadcasterState aState;

    for (const auto& xTab : maTabs)
        xTab->CollectBroadcasterState(aState);

    if (pBASM)
        pBASM->CollectBroadcasterState(aState);

    return aState;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
