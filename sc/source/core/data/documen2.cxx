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

#include <scextopt.hxx>
#include <autonamecache.hxx>

#include <o3tl/test_info.hxx>
#include <osl/thread.h>
#include <svx/xtable.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/objsh.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/printer.hxx>
#include <svl/asiancfg.hxx>
#include <vcl/virdev.hxx>
#include <svl/sharedstringpool.hxx>
#include <tools/urlobj.hxx>
#include <rtl/crc.h>
#include <basic/basmgr.hxx>
#include <comphelper/threadpool.hxx>
#include <sal/log.hxx>
#include <osl/diagnose.h>
#include <comphelper/configuration.hxx>

#include <scmod.hxx>
#include <document.hxx>
#include <table.hxx>
#include <patattr.hxx>
#include <rangenam.hxx>
#include <dbdata.hxx>
#include <chartlock.hxx>
#include <rechead.hxx>
#include <global.hxx>
#include <bcaslot.hxx>
#include <adiasync.hxx>
#include <addinlis.hxx>
#include <chartlis.hxx>
#include <markdata.hxx>
#include <validat.hxx>
#include <detdata.hxx>
#include <defaultsoptions.hxx>
#include <ddelink.hxx>
#include <chgtrack.hxx>
#include <chgviset.hxx>
#include <editutil.hxx>
#include <hints.hxx>
#include <dpobject.hxx>
#include <scrdata.hxx>
#include <poolhelp.hxx>
#include <unoreflist.hxx>
#include <listenercalls.hxx>
#include <recursionhelper.hxx>
#include <lookupcache.hxx>
#include <rangecache.hxx>
#include <externalrefmgr.hxx>
#include <viewdata.hxx>
#include <viewutil.hxx>
#include <tabprotection.hxx>
#include <formulaparserpool.hxx>
#include <clipparam.hxx>
#include <macromgr.hxx>
#include <formulacell.hxx>
#include <clipcontext.hxx>
#include <refupdatecontext.hxx>
#include <refreshtimerprotector.hxx>
#include <scopetools.hxx>
#include <documentlinkmgr.hxx>
#include <interpre.hxx>
#include <tokenstringcontext.hxx>
#include <docsh.hxx>
#include <clipoptions.hxx>
#include <listenercontext.hxx>
#include <datamapper.hxx>
#include <drwlayer.hxx>
#include <sharedstringpoolpurge.hxx>
#include <docpool.hxx>
#include <config_features.h>

using namespace com::sun::star;

const sal_uInt16 ScDocument::nSrcVer = SC_CURRENT_VERSION;

ScSheetLimits ScSheetLimits::CreateDefault()
{
#if HAVE_FEATURE_JUMBO_SHEETS
    bool jumboSheets = false;
    if (ScModule* mod = ScModule::get())
        jumboSheets = mod->GetDefaultsOptions().GetInitJumboSheets();
    else
        assert(o3tl::IsRunningUnitTest());
    if (jumboSheets)
        return ScSheetLimits(MAXCOL_JUMBO, MAXROW_JUMBO);
    else
#endif
        return ScSheetLimits(MAXCOL, MAXROW);
}

CellAttributeHelper& ScDocument::getCellAttributeHelper() const
{
    if (!mpCellAttributeHelper)
    {
        assert(!IsClipOrUndo() && "CellAttributeHelper needs to be shared using SharePooledResources, not created (!)");
        SfxItemPool* pPool(const_cast<ScDocument*>(this)->GetPool());
        assert(nullptr != pPool && "No SfxItemPool for this ScDocument (!)");
        mpCellAttributeHelper.reset(new CellAttributeHelper(*pPool));
    }

    return *mpCellAttributeHelper;
}

ScDocument::ScDocument( ScDocumentMode eMode, ScDocShell* pDocShell ) :
        mpCellAttributeHelper(),
        mpCellStringPool(std::make_shared<svl::SharedStringPool>(ScGlobal::getCharClass())),
        mpDocLinkMgr(new sc::DocumentLinkManager(pDocShell)),
        mbFormulaGroupCxtBlockDiscard(false),
        maCalcConfig( ScInterpreter::GetGlobalConfig()),
        mpUndoManager( nullptr ),
        mpShell( pDocShell ),
        mpPrinter( nullptr ),
        mpVirtualDevice_100th_mm( nullptr ),
        pFormatExchangeList( nullptr ),
        mxSheetLimits(new ScSheetLimits(ScSheetLimits::CreateDefault())),
        pFormulaTree( nullptr ),
        pEOFormulaTree( nullptr ),
        pFormulaTrack( nullptr ),
        pEOFormulaTrack( nullptr ),
        pPreviewCellStyle( nullptr ),
        maPreviewSelection(*mxSheetLimits),
        nUnoObjectId( 0 ),
        nRangeOverflowType( 0 ),
        aCurTextWidthCalcPos(MaxCol(),0,0),
        aTrackIdle("sc ScDocument Track Idle"),
        nFormulaCodeInTree(0),
        nXMLImportedFormulaCount( 0 ),
        nInterpretLevel(0),
        nMacroInterpretLevel(0),
        nInterpreterTableOpLevel(0),
        maInterpreterContext( *this, nullptr ),
        mxScSortedRangeCache(new ScSortedRangeCacheMap),
        nFormulaTrackCount(0),
        eHardRecalcState(HardRecalcState::OFF),
        nVisibleTab( 0 ),
        nPosLeft( 0 ),
        nPosTop( 0 ),
        eLinkMode(LM_UNKNOWN),
        bAutoCalc( eMode == SCDOCMODE_DOCUMENT || eMode == SCDOCMODE_FUNCTIONACCESS ),
        bAutoCalcShellDisabled( false ),
        bForcedFormulaPending( false ),
        bCalculatingFormulaTree( false ),
        bIsClip( eMode == SCDOCMODE_CLIP ),
        bIsUndo( eMode == SCDOCMODE_UNDO ),
        bIsFunctionAccess( eMode == SCDOCMODE_FUNCTIONACCESS ),
        bIsVisible( false ),
        bIsEmbedded( false ),
        bInsertingFromOtherDoc( false ),
        bLoadingMedium( false ),
        bImportingXML( false ),
        mbImportingXLSX( false ),
        bCalcingAfterLoad( false ),
        bNoListening( false ),
        mbIdleEnabled(true),
        bInLinkUpdate( false ),
        bChartListenerCollectionNeedsUpdate( false ),
        bHasForcedFormulas( false ),
        bInDtorClear( false ),
        bExpandRefs( false ),
        bDetectiveDirty( false ),
        bDelayedDeletingBroadcasters( false ),
        bLinkFormulaNeedingCheck( false ),
        nAsianCompression(CharCompressType::Invalid),
        nAsianKerning(SC_ASIANKERNING_INVALID),
        bPastingDrawFromOtherDoc( false ),
        nInDdeLinkUpdate( 0 ),
        bInUnoBroadcast( false ),
        bInUnoListenerCall( false ),
        nAdjustHeightLock(0),
        eGrammar( formula::FormulaGrammar::GRAM_NATIVE ),
        bStyleSheetUsageInvalid( true ),
        mbUndoEnabled( true ),
        mbExecuteLinkEnabled( true ),
        mbChangeReadOnlyEnabled( false ),
        mbStreamValidLocked( false ),
        mbUserInteractionEnabled(true),
        mnNamedRangesLockCount(0),
        mbEmbedFonts(false),
        mbEmbedUsedFontsOnly(false),
        mbEmbedFontScriptLatin(true),
        mbEmbedFontScriptAsian(true),
        mbEmbedFontScriptComplex(true),
        mnImagePreferredDPI(0),
        mbTrackFormulasPending(false),
        mbFinalTrackFormulas(false),
        mbDocShellRecalc(false),
        mbLayoutStrings(false),
        mnMutationGuardFlags(0)
{
    maPreviewSelection = { *mxSheetLimits };
    aCurTextWidthCalcPos = { MaxCol(), 0, 0 };

    SetStorageGrammar( formula::FormulaGrammar::GRAM_STORAGE_DEFAULT);

    eSrcSet = osl_getThreadTextEncoding();

    /* TODO: for SCDOCMODE_FUNCTIONACCESS it might not even be necessary to
     * have all of these available. */
    if ( eMode == SCDOCMODE_DOCUMENT || eMode == SCDOCMODE_FUNCTIONACCESS )
    {
        mxPoolHelper = new ScPoolHelper( *this );
        if (!comphelper::IsFuzzing()) //just too slow
            pBASM.reset( new ScBroadcastAreaSlotMachine( *this ) );
        pChartListenerCollection.reset( new ScChartListenerCollection( *this ) );
        pRefreshTimerControl.reset( new ScRefreshTimerControl );
    }
    else
    {
        pChartListenerCollection = nullptr;
    }

    pDBCollection.reset( new ScDBCollection(*this) );
    pSelectionAttr = nullptr;
    apTemporaryChartLock.reset( new ScTemporaryChartLock(this) );
    xColNameRanges = new ScRangePairList;
    xRowNameRanges = new ScRangePairList;
    ImplCreateOptions();
    // languages for a visible document are set by docshell later (from options)
    SetLanguage( ScGlobal::eLnge, ScGlobal::eLnge, ScGlobal::eLnge );

    aTrackIdle.SetInvokeHandler( LINK( this, ScDocument, TrackTimeHdl ) );
}

sfx2::LinkManager* ScDocument::GetLinkManager()
{
    return GetDocLinkManager().getLinkManager();
}

const sfx2::LinkManager* ScDocument::GetLinkManager() const
{
    return GetDocLinkManager().getExistingLinkManager();
}

sc::DocumentLinkManager& ScDocument::GetDocLinkManager()
{
    return *mpDocLinkMgr;
}

const sc::DocumentLinkManager& ScDocument::GetDocLinkManager() const
{
    return const_cast<ScDocument*>(this)->GetDocLinkManager();
}

void ScDocument::SetStorageGrammar( formula::FormulaGrammar::Grammar eGram )
{
    OSL_PRECOND(
        eGram == formula::FormulaGrammar::GRAM_ODFF ||
            eGram == formula::FormulaGrammar::GRAM_PODF,
            "ScDocument::SetStorageGrammar: wrong storage grammar");

    eStorageGrammar = eGram;
}

void ScDocument::SetDocVisible( bool bSet )
{
    //  called from view ctor - only for a visible document,
    //  each new sheet's RTL flag is initialized from the locale
    bIsVisible = bSet;
}

sal_uInt32 ScDocument::GetDocumentID() const
{
    const ScDocument* pThis = this;
    sal_uInt32 nCrc = rtl_crc32( 0, &pThis, sizeof(ScDocument*) );
    // the this pointer only might not be sufficient
    nCrc = rtl_crc32( nCrc, &mpShell, sizeof(SfxObjectShell*) );
    return nCrc;
}

void ScDocument::StartChangeTracking()
{
    if (!pChangeTrack)
    {
        pChangeTrack.reset( new ScChangeTrack( *this ) );
        if (mpShell)
            mpShell->SetModified();
    }
}

void ScDocument::EndChangeTracking()
{
    if (pChangeTrack && mpShell)
        mpShell->SetModified();
    pChangeTrack.reset();
}

void ScDocument::SetChangeTrack( std::unique_ptr<ScChangeTrack> pTrack )
{
    OSL_ENSURE( &pTrack->GetDocument() == this, "SetChangeTrack: different documents" );
    if ( !pTrack || pTrack == pChangeTrack || &pTrack->GetDocument() != this )
        return ;
    EndChangeTracking();
    pChangeTrack = std::move(pTrack);
}

IMPL_LINK_NOARG(ScDocument, TrackTimeHdl, Timer *, void)
{
    if ( ScDdeLink::IsInUpdate() )      // do not nest
    {
        aTrackIdle.Start();            // try again later
    }
    else if (mpShell)                    // execute
    {
        TrackFormulas();
        mpShell->Broadcast( SfxHint( SfxHintId::ScDataChanged ) );

        if (!mpShell->IsModified())
        {
            mpShell->SetModified();
            SfxBindings* pBindings = GetViewBindings();
            if (pBindings)
            {
                pBindings->Invalidate( SID_SAVEDOC );
                pBindings->Invalidate( SID_DOC_MODIFIED );
            }
        }
    }
}

void ScDocument::SetExpandRefs( bool bVal )
{
    bExpandRefs = bVal;
}

void ScDocument::StartTrackTimer()
{
    if (!aTrackIdle.IsActive())        // do not postpone for forever
        aTrackIdle.Start();
}

void ScDocument::ClosingClipboardSource()
{
    if (!bIsClip)
        return;

    ForgetNoteCaptions( ScRangeList( ScRange( 0,0,0, MaxCol(), MaxRow(), GetTableCount()-1)), true);
}

ScDocument::~ScDocument()
{
    OSL_PRECOND( !bInLinkUpdate, "bInLinkUpdate in dtor" );

    // Join any pending(recalc) threads in global threadpool
    comphelper::ThreadPool::getSharedOptimalPool().joinThreadsIfIdle();

    bInDtorClear = true;

    // first of all disable all refresh timers by deleting the control
    if ( pRefreshTimerControl )
    {   // To be sure there isn't anything running do it with a protector,
        // this ensures also that nothing needs the control anymore.
        ScRefreshTimerProtector aProt( GetRefreshTimerControlAddress() );
        pRefreshTimerControl.reset();
    }

    mxFormulaParserPool.reset();
    // Destroy the external ref mgr instance here because it has a timer
    // which needs to be stopped before the app closes.
    pExternalRefMgr.reset();

    ScAddInAsync::RemoveDocument( this );
    ScAddInListener::RemoveDocument( this );
    pChartListenerCollection.reset();   // before pBASM because of potential Listener!

    ClearLookupCaches(); // before pBASM because of listeners

    // destroy BroadcastAreas first to avoid un-needed Single-EndListenings of Formula-Cells
    pBASM.reset();       // BroadcastAreaSlotMachine

    pUnoBroadcaster.reset();     // broadcasts SfxHintId::Dying again

    pUnoRefUndoList.reset();
    pUnoListenerCalls.reset();

    Clear( true );              // true = from destructor (needed for SdrModel::ClearModel)

    pValidationList.reset();
    pRangeName.reset();
    pDBCollection.reset();
    pSelectionAttr.reset();
    apTemporaryChartLock.reset();
    mpDrawLayer.reset();
    mpPrinter.disposeAndClear();
    ImplDeleteOptions();
    pConsolidateDlgData.reset();
    pClipData.reset();
    pDetOpList.reset();                  // also deletes entries
    pChangeTrack.reset();
    mpEditEngine.reset();
    mpNoteEngine.reset();
    pChangeViewSettings.reset();         // and delete
    mpVirtualDevice_100th_mm.disposeAndClear();

    pDPCollection.reset();
    mpAnonymousDBData.reset();

    // delete the EditEngine before destroying the mxPoolHelper
    pCacheFieldEditEngine.reset();

    if ( mxPoolHelper.is() && !bIsClip && !bIsUndo)
        mxPoolHelper->SourceDocumentGone();
    mxPoolHelper.clear();

    pScriptTypeData.reset();
    maNonThreaded.xRecursionHelper.reset();
    assert(!maThreadSpecific.xRecursionHelper);

    pPreviewFont.reset();
    SAL_WARN_IF( pAutoNameCache, "sc.core", "AutoNameCache still set in dtor" );

    mpFormulaGroupCxt.reset();
    // Purge unused items if the string pool will be still used (e.g. by undo history).
    if(mpCellStringPool.use_count() > 1)
    {
        // Calling purge() may be somewhat expensive with large documents, so
        // try to delay and compress it for temporary documents.
        if(IsClipOrUndo())
            ScGlobal::GetSharedStringPoolPurge().delayedPurge(mpCellStringPool);
        else
            mpCellStringPool->purge();
    }
    mpCellStringPool.reset();

    assert( pDelayedFormulaGrouping == nullptr );
    assert( pDelayedStartListeningFormulaCells.empty());
}

void ScDocument::InitClipPtrs( ScDocument* pSourceDoc )
{
    OSL_ENSURE(bIsClip, "InitClipPtrs and not bIsClip");

    ScMutationGuard aGuard(*this, ScMutationGuardFlags::CORE);

    pValidationList.reset();

    Clear();

    SharePooledResources(pSourceDoc);

    //  conditional Formats / validations
    // TODO: Copy Templates?
    const ScValidationDataList* pSourceValid = pSourceDoc->pValidationList.get();
    if ( pSourceValid )
        pValidationList.reset(new ScValidationDataList(*this, *pSourceValid));

    // store Links in Stream
    pClipData.reset();
    if (pSourceDoc->GetDocLinkManager().hasDdeLinks())
    {
        pClipData.reset( new SvMemoryStream );
        pSourceDoc->SaveDdeLinks(*pClipData);
    }

    // Options pointers exist (ImplCreateOptions) for any document.
    // Must be copied for correct results in OLE objects (#i42666#).
    SetDocOptions( pSourceDoc->GetDocOptions() );
    SetViewOptions( pSourceDoc->GetViewOptions() );
}

SvNumberFormatter* ScDocument::GetFormatTable() const
{
    assert(!IsThreadedGroupCalcInProgress());
    return mxPoolHelper->GetFormTable();
}

SfxItemPool* ScDocument::GetEditEnginePool() const
{
    return mxPoolHelper->GetEditEnginePool();
}

ScFieldEditEngine& ScDocument::GetEditEngine()
{
    if ( !mpEditEngine )
    {
        mpEditEngine.reset( new ScFieldEditEngine(this, GetEditEnginePool()) );
        mpEditEngine->SetUpdateLayout( false );
        mpEditEngine->EnableUndo( false );
        mpEditEngine->SetRefMapMode(MapMode(MapUnit::Map100thMM));
        ApplyAsianEditSettings( *mpEditEngine );
    }
    return *mpEditEngine;
}

ScNoteEditEngine& ScDocument::GetNoteEngine()
{
    if ( !mpNoteEngine )
    {
        ScMutationGuard aGuard(*this, ScMutationGuardFlags::CORE);
        mpNoteEngine.reset( new ScNoteEditEngine( GetEditEnginePool() ) );
        mpNoteEngine->SetUpdateLayout( false );
        mpNoteEngine->EnableUndo( false );
        mpNoteEngine->SetRefMapMode(MapMode(MapUnit::Map100thMM));
        ApplyAsianEditSettings( *mpNoteEngine );
        const SfxItemSet& rItemSet(getCellAttributeHelper().getDefaultCellAttribute().GetItemSet());
        SfxItemSet aEEItemSet(mpNoteEngine->GetEmptyItemSet());
        ScPatternAttr::FillToEditItemSet(aEEItemSet, rItemSet);
        mpNoteEngine->SetDefaults(std::move(aEEItemSet)); // edit engine takes ownership
    }
    return *mpNoteEngine;
}

std::unique_ptr<EditTextObject> ScDocument::CreateSharedStringTextObject( const svl::SharedString& rSS )
{
    /* TODO: Add shared string support to the edit engine to make this process
     * simpler. */
    ScFieldEditEngine& rEngine = GetEditEngine();
    rEngine.SetTextCurrentDefaults( rSS.getString());
    std::unique_ptr<EditTextObject> pObj( rEngine.CreateTextObject());
    pObj->NormalizeString( GetSharedStringPool());
    return pObj;
}

void ScDocument::ResetClip( ScDocument* pSourceDoc, const ScMarkData* pMarks )
{
    if (bIsClip)
    {
        InitClipPtrs(pSourceDoc);

        for (SCTAB i = 0; i < pSourceDoc->GetTableCount(); i++)
            if (pSourceDoc->maTabs[i])
                if (!pMarks || pMarks->GetTableSelect(i))
                {
                    OUString aString = pSourceDoc->maTabs[i]->GetName();
                    if (i < GetTableCount())
                    {
                        maTabs[i].reset( new ScTable(*this, i, aString) );

                    }
                    else
                    {
                        if (i > GetTableCount())
                        {
                            maTabs.resize(i);
                        }
                        maTabs.emplace_back(new ScTable(*this, i, aString));
                    }
                    maTabs[i]->SetLayoutRTL( pSourceDoc->maTabs[i]->IsLayoutRTL() );
                }
    }
    else
    {
        OSL_FAIL("ResetClip");
    }
}

void ScDocument::ResetClip( ScDocument* pSourceDoc, SCTAB nTab )
{
    if (bIsClip)
    {
        InitClipPtrs(pSourceDoc);
        if (nTab >= GetTableCount())
        {
            maTabs.resize(nTab+1);
        }
        maTabs[nTab].reset( new ScTable(*this, nTab, u"baeh"_ustr) );
        if (nTab < pSourceDoc->GetTableCount() && pSourceDoc->maTabs[nTab])
            maTabs[nTab]->SetLayoutRTL( pSourceDoc->maTabs[nTab]->IsLayoutRTL() );
    }
    else
    {
        OSL_FAIL("ResetClip");
    }
}

void ScDocument::EnsureTable( SCTAB nTab )
{
    bool bExtras = !bIsUndo;        // Column-Widths, Row-Heights, Flags
    if (nTab >= GetTableCount())
        maTabs.resize(nTab+1);

    if (!maTabs[nTab])
        maTabs[nTab].reset( new ScTable(*this, nTab, u"temp"_ustr, bExtras, bExtras) );
}

ScRefCellValue ScDocument::GetRefCellValue( const ScAddress& rPos )
{
    if (ScTable* pTable = FetchTable(rPos.Tab()))
        return pTable->GetRefCellValue(rPos.Col(), rPos.Row());
    return ScRefCellValue(); // empty
}

ScRefCellValue ScDocument::GetRefCellValue( const ScAddress& rPos, sc::ColumnBlockPosition& rBlockPos )
{
    if (ScTable* pTable = FetchTable(rPos.Tab()))
        return pTable->GetRefCellValue(rPos.Col(), rPos.Row(), rBlockPos);
    return ScRefCellValue(); // empty
}

svl::SharedStringPool& ScDocument::GetSharedStringPool()
{
    return *mpCellStringPool;
}

const svl::SharedStringPool& ScDocument::GetSharedStringPool() const
{
    return *mpCellStringPool;
}

bool ScDocument::GetPrintArea( SCTAB nTab, SCCOL& rEndCol, SCROW& rEndRow,
                                bool bNotes) const
{
    if (const ScTable* pTable = FetchTable(nTab))
    {
        bool bAny = pTable->GetPrintArea( rEndCol, rEndRow, bNotes, /*bCalcHiddens*/false);
        if (mpDrawLayer)
        {
            ScRange aDrawRange(0,0,nTab, MaxCol(),MaxRow(),nTab);
            if (DrawGetPrintArea( aDrawRange, true, true ))
            {
                if (aDrawRange.aEnd.Col()>rEndCol) rEndCol=aDrawRange.aEnd.Col();
                if (aDrawRange.aEnd.Row()>rEndRow) rEndRow=aDrawRange.aEnd.Row();
                bAny = true;
            }
        }
        return bAny;
    }

    rEndCol = 0;
    rEndRow = 0;
    return false;
}

bool ScDocument::GetPrintAreaHor( SCTAB nTab, SCROW nStartRow, SCROW nEndRow,
                                        SCCOL& rEndCol ) const
{
    if (const ScTable* pTable = FetchTable(nTab))
    {
        bool bAny = pTable->GetPrintAreaHor( nStartRow, nEndRow, rEndCol );
        if (mpDrawLayer)
        {
            ScRange aDrawRange(0,nStartRow,nTab, MaxCol(),nEndRow,nTab);
            if (DrawGetPrintArea( aDrawRange, true, false ))
            {
                if (aDrawRange.aEnd.Col()>rEndCol) rEndCol=aDrawRange.aEnd.Col();
                bAny = true;
            }
        }
        return bAny;
    }

    rEndCol = 0;
    return false;
}

bool ScDocument::GetPrintAreaVer( SCTAB nTab, SCCOL nStartCol, SCCOL nEndCol,
                                        SCROW& rEndRow, bool bNotes ) const
{
    if (const ScTable* pTable = FetchTable(nTab))
    {
        bool bAny = pTable->GetPrintAreaVer( nStartCol, nEndCol, rEndRow, bNotes );
        if (mpDrawLayer)
        {
            ScRange aDrawRange(nStartCol,0,nTab, nEndCol,MaxRow(),nTab);
            if (DrawGetPrintArea( aDrawRange, false, true ))
            {
                if (aDrawRange.aEnd.Row()>rEndRow) rEndRow=aDrawRange.aEnd.Row();
                bAny = true;
            }
        }
        return bAny;
    }

    rEndRow = 0;
    return false;
}

bool ScDocument::GetDataStart( SCTAB nTab, SCCOL& rStartCol, SCROW& rStartRow ) const
{
    if (const ScTable* pTable = FetchTable(nTab))
    {
        bool bAny = pTable->GetDataStart( rStartCol, rStartRow );
        if (mpDrawLayer)
        {
            ScRange aDrawRange(0,0,nTab, MaxCol(),MaxRow(),nTab);
            if (DrawGetPrintArea( aDrawRange, true, true ))
            {
                if (aDrawRange.aStart.Col()<rStartCol) rStartCol=aDrawRange.aStart.Col();
                if (aDrawRange.aStart.Row()<rStartRow) rStartRow=aDrawRange.aStart.Row();
                bAny = true;
            }
        }
        return bAny;
    }

    rStartCol = 0;
    rStartRow = 0;
    return false;
}

void ScDocument::GetTiledRenderingArea(SCTAB nTab, SCCOL& rEndCol, SCROW& rEndRow) const
{
    bool bHasPrintArea = GetCellArea(nTab, rEndCol, rEndRow);

    // we need some reasonable minimal document size
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
    {
        if (!bHasPrintArea)
        {
            rEndCol = 20;
            rEndRow = 50;
        }
        else
        {
            rEndCol += 20;
            rEndRow += 50;
        }
    }
    else if (!bHasPrintArea)
    {
        rEndCol = pViewData->GetMaxTiledCol();
        rEndRow = pViewData->GetMaxTiledRow();
    }
    else
    {
        rEndCol = std::max(rEndCol, pViewData->GetMaxTiledCol());
        rEndRow = std::max(rEndRow, pViewData->GetMaxTiledRow());
    }
}

bool ScDocument::MoveTab( SCTAB nOldPos, SCTAB nNewPos, ScProgress* pProgress )
{
    if (nOldPos == nNewPos)
        return false;

    SCTAB nTabCount = GetTableCount();
    if(nTabCount < 2)
        return false;

    bool bValid = false;
    if (ValidTab(nOldPos) && nOldPos < nTabCount )
    {
        if (maTabs[nOldPos])
        {
            sc::AutoCalcSwitch aACSwitch(*this, false);
            sc::DelayDeletingBroadcasters delayDeletingBroadcasters(*this);

            SetNoListening( true );
            if (nNewPos == SC_TAB_APPEND || nNewPos >= nTabCount)
                nNewPos = nTabCount-1;

            // Update Reference
            // TODO: combine with UpdateReference!

            sc::RefUpdateMoveTabContext aCxt( *this, nOldPos, nNewPos);

            SCTAB nDz = nNewPos - nOldPos;
            ScRange aSourceRange( 0,0,nOldPos, MaxCol(),MaxRow(),nOldPos );
            if (pRangeName)
                pRangeName->UpdateMoveTab(aCxt);

            pDBCollection->UpdateMoveTab( nOldPos, nNewPos );
            xColNameRanges->UpdateReference( URM_REORDER, *this, aSourceRange, 0,0,nDz );
            xRowNameRanges->UpdateReference( URM_REORDER, *this, aSourceRange, 0,0,nDz );
            if (pDPCollection)
                pDPCollection->UpdateReference( URM_REORDER, aSourceRange, 0,0,nDz );
            if (pDetOpList)
                pDetOpList->UpdateReference( *this, URM_REORDER, aSourceRange, 0,0,nDz );
            UpdateChartRef( URM_REORDER,
                    0,0,nOldPos, MaxCol(),MaxRow(),nOldPos, 0,0,nDz );
            UpdateRefAreaLinks( URM_REORDER, aSourceRange, 0,0,nDz );
            if ( pValidationList )
                pValidationList->UpdateMoveTab(aCxt);
            if ( pUnoBroadcaster )
                pUnoBroadcaster->Broadcast( ScUpdateRefHint( URM_REORDER,
                            aSourceRange, 0,0,nDz ) );

            ScTableUniquePtr pSaveTab = std::move(maTabs[nOldPos]);
            maTabs.erase(maTabs.begin()+nOldPos);
            maTabs.insert(maTabs.begin()+nNewPos, std::move(pSaveTab));
            for (SCTAB i = 0; i < nTabCount; i++)
                if (maTabs[i])
                    maTabs[i]->UpdateMoveTab(aCxt, i, pProgress);
            for (auto& rxTab : maTabs)
                if (rxTab)
                    rxTab->UpdateCompile();
            SetNoListening( false );
            StartAllListeners();

            sc::SetFormulaDirtyContext aFormulaDirtyCxt;
            SetAllFormulasDirty(aFormulaDirtyCxt);

            if (mpDrawLayer)
                mpDrawLayer->ScMovePage( static_cast<sal_uInt16>(nOldPos), static_cast<sal_uInt16>(nNewPos) );

            bValid = true;
        }
    }
    return bValid;
}

bool ScDocument::CopyTab( SCTAB nOldPos, SCTAB nNewPos, const ScMarkData* pOnlyMarked )
{
    if (SC_TAB_APPEND == nNewPos  || nNewPos >= GetTableCount())
        nNewPos = GetTableCount();
    OUString aName;
    GetName(nOldPos, aName);

    //  check first if Prefix is valid; if not, then only avoid duplicates
    bool bPrefix = ValidTabName( aName );
    OSL_ENSURE(bPrefix, "invalid table name");
    SCTAB nDummy;

    CreateValidTabName(aName);

    bool bValid;
    if (bPrefix)
        bValid = ValidNewTabName(aName);
    else
        bValid = !GetTable( aName, nDummy );

    sc::AutoCalcSwitch aACSwitch(*this, false);
    sc::RefUpdateInsertTabContext aCxt( *this, nNewPos, 1);

    if (bValid)
    {
        if (nNewPos >= GetTableCount())
        {
            nNewPos = GetTableCount();
            maTabs.emplace_back(new ScTable(*this, nNewPos, aName));
        }
        else
        {
            if (ValidTab(nNewPos) && nNewPos < GetTableCount())
            {
                SetNoListening( true );

                ScRange aRange( 0,0,nNewPos, MaxCol(),MaxRow(),MAXTAB );
                xColNameRanges->UpdateReference( URM_INSDEL, *this, aRange, 0,0,1 );
                xRowNameRanges->UpdateReference( URM_INSDEL, *this, aRange, 0,0,1 );
                if (pRangeName)
                    pRangeName->UpdateInsertTab(aCxt);

                pDBCollection->UpdateReference(
                                    URM_INSDEL, 0,0,nNewPos, MaxCol(),MaxRow(),MAXTAB, 0,0,1 );
                if (pDPCollection)
                    pDPCollection->UpdateReference( URM_INSDEL, aRange, 0,0,1 );
                if (pDetOpList)
                    pDetOpList->UpdateReference( *this, URM_INSDEL, aRange, 0,0,1 );
                UpdateChartRef( URM_INSDEL, 0,0,nNewPos, MaxCol(),MaxRow(),MAXTAB, 0,0,1 );
                UpdateRefAreaLinks( URM_INSDEL, aRange, 0,0,1 );
                if ( pUnoBroadcaster )
                    pUnoBroadcaster->Broadcast( ScUpdateRefHint( URM_INSDEL, aRange, 0,0,1 ) );

                for (auto it = maTabs.begin(); it != maTabs.end(); ++it)
                {
                    if (*it && it != (maTabs.begin() + nOldPos))
                        (*it)->UpdateInsertTab(aCxt);
                }
                if (nNewPos <= nOldPos)
                    nOldPos++;
                maTabs.emplace(maTabs.begin() + nNewPos, new ScTable(*this, nNewPos, aName));
                bValid = true;
                for (auto it = maTabs.begin(); it != maTabs.end(); ++it)
                {
                    if (*it && it != maTabs.begin()+nOldPos && it != maTabs.begin() + nNewPos)
                        (*it)->UpdateCompile();
                }
                SetNoListening( false );
                sc::StartListeningContext aSLCxt(*this);
                for (auto it = maTabs.begin(); it != maTabs.end(); ++it)
                {
                    if (*it && it != maTabs.begin()+nOldPos && it != maTabs.begin()+nNewPos)
                        (*it)->StartListeners(aSLCxt, true);
                }

                if (pValidationList)
                    pValidationList->UpdateInsertTab(aCxt);
            }
            else
                bValid = false;
        }
    }

    if (bValid)
    {
        SetNoListening( true );     // not yet at CopyToTable/Insert

        const bool bGlobalNamesToLocal = true;
        const SCTAB nRealOldPos = (nNewPos < nOldPos) ? nOldPos - 1 : nOldPos;
        const ScRangeName* pNames = GetRangeName( nOldPos);
        if (pNames)
            pNames->CopyUsedNames( nOldPos, nRealOldPos, nNewPos, *this, *this, bGlobalNamesToLocal);
        GetRangeName()->CopyUsedNames( -1, nRealOldPos, nNewPos, *this, *this, bGlobalNamesToLocal);

        sc::CopyToDocContext aCopyDocCxt(*this);
        pDBCollection->CopyToTable(nOldPos, nNewPos);
        maTabs[nOldPos]->CopyToTable(aCopyDocCxt, 0, 0, MaxCol(), MaxRow(), InsertDeleteFlags::ALL,
                (pOnlyMarked != nullptr), maTabs[nNewPos].get(), pOnlyMarked,
                false /*bAsLink*/, true /*bColRowFlags*/, bGlobalNamesToLocal, false /*bCopyCaptions*/ );
        maTabs[nNewPos]->SetTabBgColor(maTabs[nOldPos]->GetTabBgColor());

        SCTAB nDz = nNewPos - nOldPos;
        sc::RefUpdateContext aRefCxt(*this);
        aRefCxt.meMode = URM_COPY;
        aRefCxt.maRange = ScRange(0, 0, nNewPos, MaxCol(), MaxRow(), nNewPos);
        aRefCxt.mnTabDelta = nDz;
        maTabs[nNewPos]->UpdateReference(aRefCxt);

        maTabs[nNewPos]->UpdateInsertTabAbs(nNewPos); // move all paragraphs up by one!!
        maTabs[nOldPos]->UpdateInsertTab(aCxt);

        maTabs[nOldPos]->UpdateCompile();
        maTabs[nNewPos]->UpdateCompile( true ); //  maybe already compiled in Clone, but used names need recompilation
        SetNoListening( false );
        sc::StartListeningContext aSLCxt(*this);
        maTabs[nOldPos]->StartListeners(aSLCxt, true);
        maTabs[nNewPos]->StartListeners(aSLCxt, true);

        sc::SetFormulaDirtyContext aFormulaDirtyCxt;
        SetAllFormulasDirty(aFormulaDirtyCxt);

        if (mpDrawLayer) //  Skip cloning Note caption object
            // page is already created in ScTable ctor
            mpDrawLayer->ScCopyPage( static_cast<sal_uInt16>(nOldPos), static_cast<sal_uInt16>(nNewPos) );

        if (pDPCollection)
            pDPCollection->CopyToTab(nOldPos, nNewPos);

        maTabs[nNewPos]->SetPageStyle( maTabs[nOldPos]->GetPageStyle() );
        maTabs[nNewPos]->SetPendingRowHeights( maTabs[nOldPos]->IsPendingRowHeights() );

        // Copy the custom print range if exists.
        maTabs[nNewPos]->CopyPrintRange(*maTabs[nOldPos]);

        // Copy the RTL settings
        maTabs[nNewPos]->SetLayoutRTL(maTabs[nOldPos]->IsLayoutRTL());
        maTabs[nNewPos]->SetLoadingRTL(maTabs[nOldPos]->IsLoadingRTL());

        // Finally copy the note captions, which need
        // 1. the updated source ScColumn::nTab members if nNewPos <= nOldPos
        // 2. row heights and column widths of the destination
        // 3. RTL settings of the destination
        maTabs[nOldPos]->CopyCaptionsToTable( 0, 0, MaxCol(), MaxRow(), maTabs[nNewPos].get(), true /*bCloneCaption*/);
    }

    return bValid;
}

bool ScDocument::TransferTab( ScDocument& rSrcDoc, SCTAB nSrcPos,
                                SCTAB nDestPos, bool bInsertNew,
                                bool bResultsOnly )
{
    bool bRetVal = true;

    if (rSrcDoc.mpShell->GetMedium())
    {
        rSrcDoc.maFileURL = rSrcDoc.mpShell->GetMedium()->GetURLObject().GetMainURL(INetURLObject::DecodeMechanism::ToIUri);
        // for unsaved files use the title name and adjust during save of file
        if (rSrcDoc.maFileURL.isEmpty())
            rSrcDoc.maFileURL = rSrcDoc.mpShell->GetName();
    }
    else
    {
        rSrcDoc.maFileURL = rSrcDoc.mpShell->GetName();
    }

    bool bValid = true;
    if (bInsertNew)             // re-insert
    {
        OUString aName;
        rSrcDoc.GetName(nSrcPos, aName);
        CreateValidTabName(aName);
        bValid = InsertTab(nDestPos, aName);

        // Copy the RTL settings
        maTabs[nDestPos]->SetLayoutRTL(rSrcDoc.maTabs[nSrcPos]->IsLayoutRTL());
        maTabs[nDestPos]->SetLoadingRTL(rSrcDoc.maTabs[nSrcPos]->IsLoadingRTL());
    }
    else                        // replace existing tables
    {
        if (ScTable* pTable = FetchTable(nDestPos))
        {
            pTable->DeleteArea(0, 0, MaxCol(), MaxRow(), InsertDeleteFlags::ALL);
        }
        else
            bValid = false;
    }

    if (bValid)
    {
        bool bOldAutoCalcSrc = false;
        bool bOldAutoCalc = GetAutoCalc();
        SetAutoCalc( false );   // avoid repeated calculations
        SetNoListening( true );
        if ( bResultsOnly )
        {
            bOldAutoCalcSrc = rSrcDoc.GetAutoCalc();
            rSrcDoc.SetAutoCalc( true );   // in case something needs calculation
        }

        {
            NumFmtMergeHandler aNumFmtMergeHdl(*this, rSrcDoc);

            sc::CopyToDocContext aCxt(*this);
            nDestPos = std::min(nDestPos, static_cast<SCTAB>(GetTableCount() - 1));
            {   // scope for bulk broadcast
                ScBulkBroadcast aBulkBroadcast( pBASM.get(), SfxHintId::ScDataChanged);
                if (!bResultsOnly)
                {
                    const bool bGlobalNamesToLocal = false;
                    const ScRangeName* pNames = rSrcDoc.GetRangeName( nSrcPos);
                    if (pNames)
                        pNames->CopyUsedNames( nSrcPos, nSrcPos, nDestPos, rSrcDoc, *this, bGlobalNamesToLocal);
                    rSrcDoc.GetRangeName()->CopyUsedNames( -1, nSrcPos, nDestPos, rSrcDoc, *this, bGlobalNamesToLocal);
                }
                rSrcDoc.maTabs[nSrcPos]->CopyToTable(aCxt, 0, 0, MaxCol(), MaxRow(),
                        ( bResultsOnly ? InsertDeleteFlags::ALL & ~InsertDeleteFlags::FORMULA : InsertDeleteFlags::ALL),
                        false, maTabs[nDestPos].get(), /*pMarkData*/nullptr, /*bAsLink*/false, /*bColRowFlags*/true,
                        /*bGlobalNamesToLocal*/false, /*bCopyCaptions*/true );
            }
        }
        maTabs[nDestPos]->SetTabNo(nDestPos);
        maTabs[nDestPos]->SetTabBgColor(rSrcDoc.maTabs[nSrcPos]->GetTabBgColor());

        // tdf#66613 - copy existing print ranges and col/row repetitions
        if (auto aRepeatColRange = rSrcDoc.maTabs[nSrcPos]->GetRepeatColRange())
        {
            aRepeatColRange->aStart.SetTab(nDestPos);
            aRepeatColRange->aEnd.SetTab(nDestPos);
            maTabs[nDestPos]->SetRepeatColRange(std::move(aRepeatColRange));
        }

        if (auto aRepeatRowRange = rSrcDoc.maTabs[nSrcPos]->GetRepeatRowRange())
        {
            aRepeatRowRange->aStart.SetTab(nDestPos);
            aRepeatRowRange->aEnd.SetTab(nDestPos);
            maTabs[nDestPos]->SetRepeatRowRange(std::move(aRepeatRowRange));
        }

        if (rSrcDoc.IsPrintEntireSheet(nSrcPos))
            maTabs[nDestPos]->SetPrintEntireSheet();
        else
        {
            // tdf#157897 - clear print ranges before adding additional ones
            maTabs[nDestPos]->ClearPrintRanges();
            const auto nPrintRangeCount = rSrcDoc.maTabs[nSrcPos]->GetPrintRangeCount();
            for (auto nPos = 0; nPos < nPrintRangeCount; nPos++)
            {
                // Adjust the tab for the print range at the new position
                ScRange aSrcPrintRange(*rSrcDoc.maTabs[nSrcPos]->GetPrintRange(nPos));
                aSrcPrintRange.aStart.SetTab(nDestPos);
                aSrcPrintRange.aEnd.SetTab(nDestPos);
                maTabs[nDestPos]->AddPrintRange(aSrcPrintRange);
            }
        }

        if ( !bResultsOnly )
        {
            sc::RefUpdateContext aRefCxt(*this);
            aRefCxt.meMode = URM_COPY;
            aRefCxt.maRange = ScRange(0, 0, nDestPos, MaxCol(), MaxRow(), nDestPos);
            aRefCxt.mnTabDelta = nDestPos - nSrcPos;
            maTabs[nDestPos]->UpdateReference(aRefCxt);

            // Readjust self-contained absolute references to this sheet
            maTabs[nDestPos]->TestTabRefAbs(nSrcPos);
            sc::CompileFormulaContext aFormulaCxt(*this);
            maTabs[nDestPos]->CompileAll(aFormulaCxt);
        }

        SetNoListening( false );
        if ( !bResultsOnly )
        {
            sc::StartListeningContext aSLCxt(*this);
            maTabs[nDestPos]->StartListeners(aSLCxt, true);
        }
        SetDirty( ScRange( 0, 0, nDestPos, MaxCol(), MaxRow(), nDestPos), false);

        if ( bResultsOnly )
            rSrcDoc.SetAutoCalc( bOldAutoCalcSrc );
        SetAutoCalc( bOldAutoCalc );

        //  copy Drawing

        if (bInsertNew)
            TransferDrawPage( rSrcDoc, nSrcPos, nDestPos );

        maTabs[nDestPos]->SetPendingRowHeights( rSrcDoc.maTabs[nSrcPos]->IsPendingRowHeights() );
    }
    if (!bValid)
        bRetVal = false;
    bool bVbaEnabled = IsInVBAMode();

    if ( bVbaEnabled  )
    {
        ScDocShell* pSrcShell = rSrcDoc.GetDocumentShell();
        if ( pSrcShell )
        {
            OUString aLibName(u"Standard"_ustr);
#if HAVE_FEATURE_SCRIPTING
            const BasicManager *pBasicManager = pSrcShell->GetBasicManager();
            if (pBasicManager && !pBasicManager->GetName().isEmpty())
            {
                aLibName = pSrcShell->GetBasicManager()->GetName();
            }
#endif
            OUString sSource;
            uno::Reference< script::XLibraryContainer > xLibContainer = pSrcShell->GetBasicContainer();
            uno::Reference< container::XNameContainer > xLib;
            if( xLibContainer.is() )
            {
                uno::Any aLibAny = xLibContainer->getByName(aLibName);
                aLibAny >>= xLib;
            }

            if( xLib.is() )
            {
                OUString sSrcCodeName;
                rSrcDoc.GetCodeName( nSrcPos, sSrcCodeName );
                OUString sRTLSource;
                if (xLib->hasByName( sSrcCodeName ))
                    xLib->getByName( sSrcCodeName ) >>= sRTLSource;
                sSource = sRTLSource;
            }
            VBA_InsertModule( *this, nDestPos, sSource );
        }
    }

    return bRetVal;
}

void ScDocument::SetError( SCCOL nCol, SCROW nRow, SCTAB nTab, const FormulaError nError)
{
    if (ScTable* pTable = FetchTable(nTab))
        pTable->SetError(nCol, nRow, nError);
}

void ScDocument::SetFormula(
    const ScAddress& rPos, const ScTokenArray& rArray )
{
    if (ScTable* pTable = FetchTable(rPos.Tab()))
        pTable->SetFormula(rPos.Col(), rPos.Row(), rArray, formula::FormulaGrammar::GRAM_DEFAULT);
}

void ScDocument::SetFormula(
    const ScAddress& rPos, const OUString& rFormula, formula::FormulaGrammar::Grammar eGram )
{
    if (ScTable* pTable = FetchTable(rPos.Tab()))
        pTable->SetFormula(rPos.Col(), rPos.Row(), rFormula, eGram);
}

ScFormulaCell* ScDocument::SetFormulaCell( const ScAddress& rPos, ScFormulaCell* pCell )
{
    if (ScTable* pTable = FetchTable(rPos.Tab()))
        return pTable->SetFormulaCell(rPos.Col(), rPos.Row(), pCell);

    delete pCell;
    return nullptr;
}

bool ScDocument::SetFormulaCells( const ScAddress& rPos, std::vector<ScFormulaCell*>& rCells )
{
    if (rCells.empty())
        return false;

    if (ScTable* pTable = FetchTable(rPos.Tab()))
        return pTable->SetFormulaCells(rPos.Col(), rPos.Row(), rCells);
    return false;
}

void ScDocument::SetConsolidateDlgData( std::unique_ptr<ScConsolidateParam> pData )
{
    pConsolidateDlgData = std::move(pData);
}

void ScDocument::SetEasyConditionalFormatDialogData(std::unique_ptr<ScConditionMode> pMode)
{
    pConditionalFormatDialogMode = std::move(pMode);
}

void ScDocument::SetChangeViewSettings(const ScChangeViewSettings& rNew)
{
    if (pChangeViewSettings==nullptr)
        pChangeViewSettings.reset( new ScChangeViewSettings );

    *pChangeViewSettings=rNew;
}

std::unique_ptr<ScFieldEditEngine> ScDocument::CreateFieldEditEngine()
{
    std::unique_ptr<ScFieldEditEngine> pNewEditEngine;
    if (!pCacheFieldEditEngine)
    {
        pNewEditEngine.reset( new ScFieldEditEngine(
            this, GetEditEnginePool(), false) );
    }
    else
    {
        if ( !bImportingXML )
        {
            // #i66209# previous use might not have restored update mode,
            // ensure same state as for a new EditEngine (UpdateMode = true)
            pCacheFieldEditEngine->SetUpdateLayout(true);
        }

        pNewEditEngine = std::move(pCacheFieldEditEngine);
    }
    return pNewEditEngine;
}

void ScDocument::DisposeFieldEditEngine(std::unique_ptr<ScFieldEditEngine>& rpEditEngine)
{
    if (!pCacheFieldEditEngine && rpEditEngine)
    {
        pCacheFieldEditEngine = std::move( rpEditEngine );
        pCacheFieldEditEngine->Clear();
    }
    else
        rpEditEngine.reset();
}

ScLookupCache & ScDocument::GetLookupCache( const ScRange & rRange, ScInterpreterContext* pContext )
{
    ScLookupCache* pCache = nullptr;
    if (!pContext->mxScLookupCache)
        pContext->mxScLookupCache.reset(new ScLookupCacheMap);
    ScLookupCacheMap* pCacheMap = pContext->mxScLookupCache.get();
    // insert with temporary value to avoid doing two lookups
    auto [findIt, bInserted] = pCacheMap->aCacheMap.emplace(rRange, nullptr);
    if (bInserted)
    {
        findIt->second = std::make_unique<ScLookupCache>(this, rRange, *pCacheMap);
        pCache = findIt->second.get();
        // The StartListeningArea() call is not thread-safe, as all threads
        // would access the same SvtBroadcaster.
        std::unique_lock guard( mScLookupMutex );
        StartListeningArea(rRange, false, pCache);
    }
    else
        pCache = (*findIt).second.get();

    return *pCache;
}

ScSortedRangeCache& ScDocument::GetSortedRangeCache( const ScRange & rRange, const ScQueryParam& param,
                                                     ScInterpreterContext* pContext, bool bNewSearchFunction,
                                                     sal_uInt8 nSortedBinarySearch )
{
    assert(mxScSortedRangeCache);
    ScSortedRangeCache::HashKey key = ScSortedRangeCache::makeHashKey(rRange, param);
    // This should be created just once for one range, and repeated calls should reuse it, even
    // between threads (it doesn't make sense to use ScInterpreterContext and have all threads
    // build their own copy of the same data). So first try read-only access, which should
    // in most cases be enough.
    {
      std::shared_lock guard(mScLookupMutex);
      auto findIt = mxScSortedRangeCache->aCacheMap.find(key);
      if( findIt != mxScSortedRangeCache->aCacheMap.end())
        return *findIt->second;
    }
    // Avoid recursive calls because of some cells in the range being dirty and triggering
    // interpreting, which may call into this again. Threaded calculation makes sure
    // no cells are dirty. If some cells in the range cannot be interpreted and remain
    // dirty e.g. because of circular dependencies, create only an invalid empty cache to prevent
    // a possible recursive deadlock.
    bool invalid = false;
    if(!IsThreadedGroupCalcInProgress())
        if(!InterpretCellsIfNeeded(rRange))
            invalid = true;
    std::unique_lock guard(mScLookupMutex);
    auto [findIt, bInserted] = mxScSortedRangeCache->aCacheMap.emplace(key, nullptr);
    if (bInserted)
    {
        findIt->second = std::make_unique<ScSortedRangeCache>(*this, rRange, param, pContext, invalid, bNewSearchFunction, nSortedBinarySearch);
        StartListeningArea(rRange, false, findIt->second.get());
    }
    return *findIt->second;
}

void ScDocument::RemoveLookupCache( ScLookupCache & rCache )
{
    // Data changes leading to this should never happen during calculation (they are either
    // a result of user input or recalc). If it turns out this can be the case, locking is needed
    // here and also in ScLookupCache::Notify().
    assert(!IsThreadedGroupCalcInProgress());
    auto & cacheMap = rCache.getCacheMap();
    auto it(cacheMap.aCacheMap.find(rCache.getRange()));
    if (it != cacheMap.aCacheMap.end())
    {
        std::unique_ptr<ScLookupCache> xCache = std::move(it->second);
        cacheMap.aCacheMap.erase(it);
        assert(!IsThreadedGroupCalcInProgress()); // EndListeningArea() is not thread-safe
        EndListeningArea(xCache->getRange(), false, &rCache);
        return;
    }
    OSL_FAIL( "ScDocument::RemoveLookupCache: range not found in hash map");
}

void ScDocument::RemoveSortedRangeCache( ScSortedRangeCache & rCache )
{
    // Data changes leading to this should never happen during calculation (they are either
    // a result of user input or recalc). If it turns out this can be the case, locking is needed
    // here and also in ScSortedRangeCache::Notify().
    assert(!IsThreadedGroupCalcInProgress());
    auto it(mxScSortedRangeCache->aCacheMap.find(rCache.getHashKey()));
    if (it != mxScSortedRangeCache->aCacheMap.end())
    {
        std::unique_ptr<ScSortedRangeCache> xCache = std::move(it->second);
        mxScSortedRangeCache->aCacheMap.erase(it);
        assert(!IsThreadedGroupCalcInProgress()); // EndListeningArea() is not thread-safe
        EndListeningArea(xCache->getRange(), false, &rCache);
        return;
    }
    OSL_FAIL( "ScDocument::RemoveSortedRangeCache: range not found in hash map");
}

void ScDocument::ClearLookupCaches()
{
    assert(!IsThreadedGroupCalcInProgress());
    GetNonThreadedContext().mxScLookupCache.reset();
    mxScSortedRangeCache->aCacheMap.clear();
    // Clear lookup cache in all interpreter-contexts in the (threaded/non-threaded) pools.
    ScInterpreterContextPool::ClearLookupCaches(this);
}

bool ScDocument::IsCellInChangeTrack(const ScAddress &cell,Color *pColCellBorder)
{
    ScChangeTrack* pTrack = GetChangeTrack();
    ScChangeViewSettings* pSettings = GetChangeViewSettings();
    if ( !pTrack || !pTrack->GetFirst() || !pSettings || !pSettings->ShowChanges() )
        return false;           // missing or turned-off
    ScActionColorChanger aColorChanger(*pTrack);
    //  Clipping happens from outside
    //! TODO: without Clipping; only paint affected cells ??!??!?
    const ScChangeAction* pAction = pTrack->GetFirst();
    while (pAction)
    {
        if ( pAction->IsVisible() )
        {
            ScChangeActionType eType = pAction->GetType();
            const ScBigRange& rBig = pAction->GetBigRange();
            if ( rBig.aStart.Tab() == cell.Tab())
            {
                ScRange aRange = rBig.MakeRange( *this );
                if ( eType == SC_CAT_DELETE_ROWS )
                    aRange.aEnd.SetRow( aRange.aStart.Row() );
                else if ( eType == SC_CAT_DELETE_COLS )
                    aRange.aEnd.SetCol( aRange.aStart.Col() );
                if (ScViewUtil::IsActionShown( *pAction, *pSettings, *this ) )
                {
                    if (aRange.Contains(cell))
                    {
                        if (pColCellBorder != nullptr)
                        {
                            aColorChanger.Update( *pAction );
                            Color aColor( aColorChanger.GetColor() );
                            *pColCellBorder = aColor;
                        }
                        return true;
                    }
                }
            }
            if ( eType == SC_CAT_MOVE &&
                static_cast<const ScChangeActionMove*>(pAction)->
                GetFromRange().aStart.Tab() == cell.Col() )
            {
                ScRange aRange = static_cast<const ScChangeActionMove*>(pAction)->
                    GetFromRange().MakeRange( *this );
                if (ScViewUtil::IsActionShown( *pAction, *pSettings, *this ) )
                {
                    if (aRange.Contains(cell))
                    {
                        if (pColCellBorder != nullptr)
                        {
                            aColorChanger.Update( *pAction );
                            Color aColor( aColorChanger.GetColor() );
                            *pColCellBorder = aColor;
                        }
                        return true;
                    }
                }
            }
        }
        pAction = pAction->GetNext();
    }
    return false;
}

void ScDocument::GetCellChangeTrackNote( const ScAddress &aCellPos, OUString &aTrackText,bool &bLeftEdge)
{
    aTrackText.clear();
    //  Change-Tracking
    ScChangeTrack* pTrack = GetChangeTrack();
    ScChangeViewSettings* pSettings = GetChangeViewSettings();
    if ( !(pTrack && pTrack->GetFirst() && pSettings && pSettings->ShowChanges()))
        return;

    const ScChangeAction* pFound = nullptr;
    const ScChangeAction* pFoundContent = nullptr;
    const ScChangeAction* pFoundMove = nullptr;
    const ScChangeAction* pAction = pTrack->GetFirst();
    while (pAction)
    {
        if ( pAction->IsVisible() &&
             ScViewUtil::IsActionShown( *pAction, *pSettings, *this ) )
        {
            ScChangeActionType eType = pAction->GetType();
            const ScBigRange& rBig = pAction->GetBigRange();
            if ( rBig.aStart.Tab() == aCellPos.Tab())
            {
                ScRange aRange = rBig.MakeRange( *this );
                if ( eType == SC_CAT_DELETE_ROWS )
                    aRange.aEnd.SetRow( aRange.aStart.Row() );
                else if ( eType == SC_CAT_DELETE_COLS )
                    aRange.aEnd.SetCol( aRange.aStart.Col() );
                if ( aRange.Contains( aCellPos ) )
                {
                    pFound = pAction;       // the last wins
                    switch ( eType )
                    {
                        case SC_CAT_CONTENT :
                            pFoundContent = pAction;
                        break;
                        case SC_CAT_MOVE :
                            pFoundMove = pAction;
                        break;
                        default:
                            break;
                    }
                }
            }
            if ( eType == SC_CAT_MOVE )
            {
                ScRange aRange =
                    static_cast<const ScChangeActionMove*>(pAction)->
                    GetFromRange().MakeRange( *this );
                if ( aRange.Contains( aCellPos ) )
                {
                    pFound = pAction;
                }
            }
        }
        pAction = pAction->GetNext();
    }
    if ( !pFound )
        return;

    if ( pFoundContent && pFound->GetType() != SC_CAT_CONTENT )
        pFound = pFoundContent;     // Content wins
    if ( pFoundMove && pFound->GetType() != SC_CAT_MOVE &&
            pFoundMove->GetActionNumber() >
            pFound->GetActionNumber() )
        pFound = pFoundMove;        // Move wins
    //  for deleted columns: arrow on left side of row
    if ( pFound->GetType() == SC_CAT_DELETE_COLS )
        bLeftEdge = true;
    DateTime aDT = pFound->GetDateTime();
    aTrackText  = pFound->GetUser();
    aTrackText += ", ";
    aTrackText += ScGlobal::getLocaleData().getDate(aDT);
    aTrackText += " ";
    aTrackText += ScGlobal::getLocaleData().getTime(aDT);
    aTrackText += ":\n";
    OUString aComStr = pFound->GetComment();
    if(!aComStr.isEmpty())
    {
        aTrackText += aComStr;
        aTrackText += "\n( ";
    }
    aTrackText = pFound->GetDescription( *this );
    if (!aComStr.isEmpty())
    {
        aTrackText += ")";
    }
}

void ScDocument::SetPreviewFont( std::unique_ptr<SfxItemSet> pFont )
{
    pPreviewFont = std::move(pFont);
}

void  ScDocument::SetPreviewSelection( const ScMarkData& rSel )
{
    maPreviewSelection = rSel;
}

SfxItemSet* ScDocument::GetPreviewFont( SCCOL nCol, SCROW nRow, SCTAB nTab )
{
    SfxItemSet* pRet = nullptr;
    if ( pPreviewFont )
    {
        ScMarkData aSel = GetPreviewSelection();
        if ( aSel.IsCellMarked( nCol, nRow ) && aSel.GetFirstSelected() == nTab )
            pRet = pPreviewFont.get();
    }
    return pRet;
}

ScStyleSheet* ScDocument::GetPreviewCellStyle( SCCOL nCol, SCROW nRow, SCTAB nTab )
{
    ScStyleSheet* pRet = nullptr;
    ScMarkData aSel = GetPreviewSelection();
    if ( pPreviewCellStyle && aSel.IsCellMarked( nCol, nRow ) && aSel.GetFirstSelected() == nTab  )
        pRet = pPreviewCellStyle;
    return pRet;
}

sc::IconSetBitmapMap& ScDocument::GetIconSetBitmapMap()
{
    if (!m_pIconSetBitmapMap)
    {
        m_pIconSetBitmapMap.reset(new sc::IconSetBitmapMap);
    }
    return *m_pIconSetBitmapMap;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
