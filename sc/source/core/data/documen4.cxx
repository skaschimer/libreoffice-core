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

#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>
#include <svl/zformat.hxx>
#include <formula/token.hxx>
#include <sal/log.hxx>
#include <comphelper/configuration.hxx>
#include <osl/diagnose.h>
#include <o3tl/string_view.hxx>

#include <document.hxx>
#include <docsh.hxx>
#include <table.hxx>
#include <globstr.hrc>
#include <scresid.hxx>
#include <subtotal.hxx>
#include <docoptio.hxx>
#include <markdata.hxx>
#include <validat.hxx>
#include <scitems.hxx>
#include <stlpool.hxx>
#include <poolhelp.hxx>
#include <detdata.hxx>
#include <patattr.hxx>
#include <chgtrack.hxx>
#include <progress.hxx>
#include <paramisc.hxx>
#include <compiler.hxx>
#include <externalrefmgr.hxx>
#include <attrib.hxx>
#include <formulacell.hxx>
#include <tokenarray.hxx>
#include <tokenstringcontext.hxx>
#include <memory>

using namespace formula;

/** (Goal Seek) Find a value of x that is a root of f(x)

    This function is used internally for the goal seek operation.  It uses the
    Regula Falsi (aka false position) algorithm to find a root of f(x).  The
    start value and the target value are to be given by the user in the
    goal seek dialog.  The f(x) in this case is defined as the formula in the
    formula cell minus target value.  This function may also perform additional
    search in the horizontal directions when the f(x) is discrete in order to
    ensure a non-zero slope necessary for deriving a subsequent x that is
    reasonably close to the root of interest.

    @change 24.10.2004 by Kohei Yoshida (kohei@openoffice.org)

    @see #i28955#

    @change 6 Aug 2013, fdo37341
*/
bool ScDocument::Solver(SCCOL nFCol, SCROW nFRow, SCTAB nFTab,
                        SCCOL nVCol, SCROW nVRow, SCTAB nVTab,
                        const OUString& sValStr, double& nX)
{
    bool bRet = false;
    nX = 0.0;
    ScFormulaCell* pFormula = nullptr;
    double fTargetVal = 0.0;
    {
        CellType eFType = GetCellType(nFCol, nFRow, nFTab);
        // #i108005# convert target value to number using default format,
        // as previously done in ScInterpreter::GetDouble
        sal_uInt32 nFIndex = 0;
        if ( eFType == CELLTYPE_FORMULA && FetchTable(nVTab) && ValidColRow(nVCol, nVRow) &&
             GetFormatTable()->IsNumberFormat( sValStr, nFIndex, fTargetVal ) )
        {
            ScAddress aFormulaAdr( nFCol, nFRow, nFTab );
            pFormula = GetFormulaCell( aFormulaAdr );
        }
    }
    if (pFormula)
    {
        bool bDoneIteration = false;
        const ScAddress aValueAdr(nVCol, nVRow, nVTab);
        const ScRange aVRange(aValueAdr, aValueAdr); // for SetDirty

        const sal_uInt16 nMaxIter = 100;
        const double fEps = 1E-10;
        const double fDelta = 1E-6;

        double fXPrev = GetValue(aValueAdr);
        double fBestX = fXPrev;

        // Original value to be restored later if necessary
        const ScCellValue aSaveVal(GetRefCellValue(aValueAdr));
        const bool changeCellType = aSaveVal.getType() != CELLTYPE_VALUE;
        if (changeCellType)
            SetValue(aValueAdr, fXPrev);
        double* pVCell = GetValueCell(aValueAdr);

        pFormula->Interpret();
        bool bError = ( pFormula->GetErrCode() != FormulaError::NONE );
        // bError always corresponds with fF

        double fFPrev = pFormula->GetValue() - fTargetVal;

        double fBestF = fabs( fFPrev );
        if ( fBestF < fDelta )
            bDoneIteration = true;

        double fX = fXPrev + fEps;
        double fF = fFPrev;
        double fSlope;

        sal_uInt16 nIter = 0;

        bool bHorMoveError = false;
        // Conform Regula Falsi Method
        while ( !bDoneIteration && ( nIter++ < nMaxIter ) )
        {
            *pVCell = fX;
            SetDirty( aVRange, false );
            pFormula->Interpret();
            bError = ( pFormula->GetErrCode() != FormulaError::NONE );
            fF = pFormula->GetValue() - fTargetVal;

            if ( fF == fFPrev && !bError )
            {
                // HORIZONTAL SEARCH: Keep moving x in both directions until the f(x)
                // becomes different from the previous f(x).  This routine is needed
                // when a given function is discrete, in which case the resulting slope
                // may become zero which ultimately causes the goal seek operation
                // to fail. #i28955#

                sal_uInt16 nHorIter = 0;
                const double fHorStepAngle = 5.0;
                const double fHorMaxAngle = 80.0;
                int const nHorMaxIter = static_cast<int>( fHorMaxAngle / fHorStepAngle );
                bool bDoneHorMove = false;

                while ( !bDoneHorMove && !bHorMoveError && nHorIter++ < nHorMaxIter )
                {
                    double fHorAngle = fHorStepAngle * static_cast<double>( nHorIter );
                    double fHorTangent = std::tan(basegfx::deg2rad(fHorAngle));

                    sal_uInt16 nIdx = 0;
                    while( nIdx++ < 2 && !bDoneHorMove )
                    {
                        double fHorX;
                        if ( nIdx == 1 )
                            fHorX = fX + fabs( fF ) * fHorTangent;
                        else
                            fHorX = fX - fabs( fF ) * fHorTangent;

                        *pVCell = fHorX;
                        SetDirty( aVRange, false );
                        pFormula->Interpret();
                        bHorMoveError = ( pFormula->GetErrCode() != FormulaError::NONE );
                        if ( bHorMoveError )
                            break;

                        fF = pFormula->GetValue() - fTargetVal;
                        if ( fF != fFPrev )
                        {
                            fX = fHorX;
                            bDoneHorMove = true;
                        }
                    }
                }
                if ( !bDoneHorMove )
                    bHorMoveError = true;
            }

            if ( bError )
            {
                // move closer to last valid value (fXPrev), keep fXPrev & fFPrev
                double fDiff = ( fXPrev - fX ) / 2;
                if ( fabs( fDiff ) < fEps )
                    fDiff = ( fDiff < 0.0 ? - fEps : fEps );
                fX += fDiff;
            }
            else if ( bHorMoveError )
                break;
            else if ( fabs(fF) < fDelta )
            {
                // converged to root
                fBestX = fX;
                bDoneIteration = true;
            }
            else
            {
                if ( fabs(fF) + fDelta < fBestF )
                {
                    fBestX = fX;
                    fBestF = fabs( fF );
                }

                if ( ( fXPrev - fX ) != 0 )
                {
                    fSlope = ( fFPrev - fF ) / ( fXPrev - fX );
                    if ( fabs( fSlope ) < fEps )
                        fSlope = fSlope < 0.0 ? -fEps : fEps;
                }
                else
                    fSlope = fEps;

                fXPrev = fX;
                fFPrev = fF;
                fX = fX - ( fF / fSlope );
            }
        }

        // Try a nice rounded input value if possible.
        const double fNiceDelta = ( bDoneIteration && fabs( fBestX ) >= 1e-3 ? 1e-3 : fDelta );
        nX = ::rtl::math::approxFloor( ( fBestX / fNiceDelta ) + 0.5 ) * fNiceDelta;

        if ( bDoneIteration )
        {
            *pVCell = nX;
            SetDirty( aVRange, false );
            pFormula->Interpret();
            if ( fabs( pFormula->GetValue() - fTargetVal ) > fabs( fF ) )
                nX = fBestX;
            bRet = true;
        }
        else if ( bError || bHorMoveError )
        {
            nX = fBestX;
        }
        if (changeCellType)
            aSaveVal.commit(*this, aValueAdr);
        else
            *pVCell = aSaveVal.getDouble();
        SetDirty( aVRange, false );
        pFormula->Interpret();
    }
    return bRet;
}

void ScDocument::InsertMatrixFormula(SCCOL nCol1, SCROW nRow1,
                                     SCCOL nCol2, SCROW nRow2,
                                     const ScMarkData& rMark,
                                     const OUString& rFormula,
                                     const ScTokenArray* pArr,
                                     const formula::FormulaGrammar::Grammar eGram )
{
    PutInOrder(nCol1, nCol2);
    PutInOrder(nRow1, nRow2);
    nCol2 = std::min<SCCOL>(nCol2, MaxCol());
    nRow2 = std::min<SCROW>(nRow2, MaxRow());
    if (!rMark.GetSelectCount())
    {
        SAL_WARN("sc", "ScDocument::InsertMatrixFormula: No table marked");
        return;
    }
    if (comphelper::IsFuzzing())
    {
        // just too slow
        if (nCol2 - nCol1 > 64)
            return;
        if (nRow2 - nRow1 > 64)
            return;
    }
    assert( ValidColRow( nCol1, nRow1) && ValidColRow( nCol2, nRow2));

    SCTAB nTab1 = *rMark.begin();

    ScFormulaCell* pCell;
    ScAddress aPos( nCol1, nRow1, nTab1 );
    if (pArr)
        pCell = new ScFormulaCell(*this, aPos, *pArr, eGram, ScMatrixMode::Formula);
    else
        pCell = new ScFormulaCell(*this, aPos, rFormula, eGram, ScMatrixMode::Formula);
    pCell->SetMatColsRows( nCol2 - nCol1 + 1, nRow2 - nRow1 + 1 );
    SCTAB nMax = GetTableCount();
    for (const auto& rTab : rMark)
    {
        if (rTab >= nMax)
            break;

        if (!maTabs[rTab])
            continue;

        if (rTab == nTab1)
        {
            pCell = maTabs[rTab]->SetFormulaCell(nCol1, nRow1, pCell);
            if (!pCell) //NULL if nCol1/nRow1 is invalid, which it can't be here
                break;
        }
        else
            maTabs[rTab]->SetFormulaCell(
                nCol1, nRow1,
                new ScFormulaCell(
                    *pCell, *this, ScAddress(nCol1, nRow1, rTab), ScCloneFlags::StartListening));
    }

    ScSingleRefData aRefData;
    aRefData.InitFlags();
    aRefData.SetRelCol(0);
    aRefData.SetRelRow(0);
    aRefData.SetRelTab(0);  // 2D matrix, always same sheet

    ScTokenArray aArr(*this); // consists only of one single reference token.
    formula::FormulaToken* t = aArr.AddMatrixSingleReference(aRefData);

    for (const SCTAB& nTab : rMark)
    {
        if (nTab >= nMax)
            break;

        ScTable* pTab = FetchTable(nTab);
        if (!pTab)
            continue;

        for (SCCOL nCol : GetWritableColumnsRange(nTab, nCol1, nCol2))
        {
            aRefData.SetRelCol(nCol1 - nCol);
            for (SCROW nRow = nRow1; nRow <= nRow2; ++nRow)
            {
                if (nCol == nCol1 && nRow == nRow1)
                    // Skip the base position.
                    continue;

                // Reference in each cell must point to the origin cell relative to the current cell.
                aRefData.SetRelRow(nRow1 - nRow);
                *t->GetSingleRef() = aRefData;
                // Token array must be cloned so that each formula cell receives its own copy.
                ScTokenArray aTokArr(aArr.CloneValue());
                aPos = ScAddress(nCol, nRow, nTab);
                pCell = new ScFormulaCell(*this, aPos, aTokArr, eGram, ScMatrixMode::Reference);
                pTab->SetFormulaCell(nCol, nRow, pCell);
            }
        }
    }
}

void ScDocument::InsertTableOp(const ScTabOpParam& rParam,  // multiple (repeated?) operation
                               SCCOL nCol1, SCROW nRow1, SCCOL nCol2, SCROW nRow2,
                               const ScMarkData& rMark)
{
    PutInOrder(nCol1, nCol2);
    PutInOrder(nRow1, nRow2);
    assert( ValidColRow( nCol1, nRow1) && ValidColRow( nCol2, nRow2));
    SCTAB i, nTab1;
    SCCOL j;
    SCROW k;
    i = 0;
    bool bStop = false;
    SCTAB nMax = GetTableCount();
    for (const auto& rTab : rMark)
    {
        if (rTab >= nMax)
            break;

        if (maTabs[rTab])
        {
            i = rTab;
            bStop = true;
            break;
        }
    }
    nTab1 = i;
    if (!bStop)
    {
        OSL_FAIL("ScDocument::InsertTableOp: No table marked");
        return;
    }

    ScRefAddress aRef;
    OUStringBuffer aForString("="
        + ScCompiler::GetNativeSymbol(ocTableOp)
        + ScCompiler::GetNativeSymbol( ocOpen));

    const OUString& sSep = ScCompiler::GetNativeSymbol( ocSep);
    if (rParam.meMode == ScTabOpParam::Column) // column only
    {
        aRef.Set( rParam.aRefFormulaCell.GetAddress(), true, false, false );
        aForString.append(aRef.GetRefString(*this, nTab1)
            + sSep
            + rParam.aRefColCell.GetRefString(*this, nTab1)
            + sSep);
        aRef.Set( nCol1, nRow1, nTab1, false, true, true );
        aForString.append(aRef.GetRefString(*this, nTab1));
        nCol1++;
        nCol2 = std::min( nCol2, static_cast<SCCOL>(rParam.aRefFormulaEnd.Col() -
                    rParam.aRefFormulaCell.Col() + nCol1 + 1));
    }
    else if (rParam.meMode == ScTabOpParam::Row) // row only
    {
        aRef.Set( rParam.aRefFormulaCell.GetAddress(), false, true, false );
        aForString.append(aRef.GetRefString(*this, nTab1)
            + sSep
            + rParam.aRefRowCell.GetRefString(*this, nTab1)
            + sSep);
        aRef.Set( nCol1, nRow1, nTab1, true, false, true );
        aForString.append(aRef.GetRefString(*this, nTab1));
        nRow1++;
        nRow2 = std::min( nRow2, static_cast<SCROW>(rParam.aRefFormulaEnd.Row() -
                    rParam.aRefFormulaCell.Row() + nRow1 + 1));
    }
    else // both
    {
        aForString.append(rParam.aRefFormulaCell.GetRefString(*this, nTab1)
            + sSep
            + rParam.aRefColCell.GetRefString(*this, nTab1)
            + sSep);
        aRef.Set( nCol1, nRow1 + 1, nTab1, false, true, true );
        aForString.append(aRef.GetRefString(*this, nTab1)
            + sSep
            + rParam.aRefRowCell.GetRefString(*this, nTab1)
            + sSep);
        aRef.Set( nCol1 + 1, nRow1, nTab1, true, false, true );
        aForString.append(aRef.GetRefString(*this, nTab1));
        nCol1++; nRow1++;
    }
    aForString.append(ScCompiler::GetNativeSymbol( ocClose ));

    ScFormulaCell aRefCell( *this, ScAddress( nCol1, nRow1, nTab1 ), aForString.makeStringAndClear(),
           formula::FormulaGrammar::GRAM_NATIVE, ScMatrixMode::NONE );
    for( j = nCol1; j <= nCol2; j++ )
    {
        for( k = nRow1; k <= nRow2; k++ )
        {
            for (const auto& rTab : rMark)
            {
                if (rTab >= nMax)
                    break;
                if( maTabs[rTab] )
                    maTabs[rTab]->SetFormulaCell(
                        j, k, new ScFormulaCell(aRefCell, *this, ScAddress(j, k, rTab), ScCloneFlags::StartListening));
            }
        }
    }
}

namespace {

bool setCacheTableReferenced(const ScDocument& rDoc, formula::FormulaToken& rToken, ScExternalRefManager& rRefMgr, const ScAddress& rPos)
{
    switch (rToken.GetType())
    {
        case svExternalSingleRef:
            return rRefMgr.setCacheTableReferenced(
                rToken.GetIndex(), rToken.GetString().getString(), 1);
        case svExternalDoubleRef:
        {
            const ScComplexRefData& rRef = *rToken.GetDoubleRef();
            ScRange aAbs = rRef.toAbs(rDoc, rPos);
            size_t nSheets = aAbs.aEnd.Tab() - aAbs.aStart.Tab() + 1;
            return rRefMgr.setCacheTableReferenced(
                    rToken.GetIndex(), rToken.GetString().getString(), nSheets);
        }
        case svExternalName:
            /* TODO: external names aren't supported yet, but would
             * have to be marked as well, if so. Mechanism would be
             * different. */
            OSL_FAIL("ScDocument::MarkUsedExternalReferences: implement the svExternalName case!");
            break;
        default:
            break;
    }
    return false;
}

}

bool ScDocument::MarkUsedExternalReferences( const ScTokenArray& rArr, const ScAddress& rPos )
{
    if (!rArr.GetLen())
        return false;

    ScExternalRefManager* pRefMgr = nullptr;
    formula::FormulaTokenArrayPlainIterator aIter( rArr );
    bool bAllMarked = false;
    while (!bAllMarked)
    {
        formula::FormulaToken* t = aIter.GetNextReferenceOrName();
        if (!t)
            break;
        if (t->IsExternalRef())
        {
            if (!pRefMgr)
                pRefMgr = GetExternalRefManager();

            bAllMarked = setCacheTableReferenced(*this, *t, *pRefMgr, rPos);
        }
        else if (t->GetType() == svIndex)
        {
            // this is a named range.  Check if the range contains an external
            // reference.
            ScRangeData* pRangeData = GetRangeName()->findByIndex(t->GetIndex());
            if (!pRangeData)
                continue;

            ScTokenArray* pArray = pRangeData->GetCode();
            formula::FormulaTokenArrayPlainIterator aArrayIter(*pArray);
            for (t = aArrayIter.First(); t; t = aArrayIter.Next())
            {
                if (!t->IsExternalRef())
                    continue;

                if (!pRefMgr)
                    pRefMgr = GetExternalRefManager();

                bAllMarked = setCacheTableReferenced(*this, *t, *pRefMgr, rPos);
            }
        }
    }
    return bAllMarked;
}

bool ScDocument::GetNextSpellingCell(SCCOL& nCol, SCROW& nRow, SCTAB nTab,
                        bool bInSel, const ScMarkData& rMark) const
{
    if (const ScTable* pTable = FetchTable(nTab))
        return pTable->GetNextSpellingCell( nCol, nRow, bInSel, rMark );
    return false;
}

bool ScDocument::GetNextMarkedCell( SCCOL& rCol, SCROW& rRow, SCTAB nTab,
                                        const ScMarkData& rMark )
{
    if (ScTable* pTable = FetchTable(nTab))
        return pTable->GetNextMarkedCell( rCol, rRow, rMark );
    return false;
}

void ScDocument::ReplaceStyle(const SvxSearchItem& rSearchItem,
                              SCCOL nCol, SCROW nRow, SCTAB nTab,
                              const ScMarkData& rMark)
{
    if (ScTable* pTable = FetchTable(nTab))
        pTable->ReplaceStyle(rSearchItem, nCol, nRow, rMark, true/*bIsUndoP*/);
}

void ScDocument::CompileDBFormula()
{
    sc::CompileFormulaContext aCxt(*this);
    for (auto& rxTab : maTabs)
    {
        if (rxTab)
            rxTab->CompileDBFormula(aCxt);
    }
}

void ScDocument::CompileColRowNameFormula()
{
    sc::CompileFormulaContext aCxt(*this);
    for (auto& rxTab : maTabs)
    {
        if (rxTab)
            rxTab->CompileColRowNameFormula(aCxt);
    }
}

void ScDocument::InvalidateTableArea()
{
    for (auto& rxTab : maTabs)
    {
        if (!rxTab)
            break;
        rxTab->InvalidateTableArea();
        if ( rxTab->IsScenario() )
            rxTab->InvalidateScenarioRanges();
    }
}

sal_Int32 ScDocument::GetMaxStringLen( SCTAB nTab, SCCOL nCol,
        SCROW nRowStart, SCROW nRowEnd, rtl_TextEncoding eCharSet ) const
{
    if (const ScTable* pTable = FetchTable(nTab))
        return pTable->GetMaxStringLen(nCol, nRowStart, nRowEnd, eCharSet);
    return 0;
}

sal_Int32 ScDocument::GetMaxNumberStringLen( sal_uInt16& nPrecision, SCTAB nTab,
                                    SCCOL nCol, SCROW nRowStart, SCROW nRowEnd ) const
{
    if (const ScTable* pTable = FetchTable(nTab))
        return pTable->GetMaxNumberStringLen(nPrecision, nCol, nRowStart, nRowEnd);
    return 0;
}

bool ScDocument::GetSelectionFunction( ScSubTotalFunc eFunc,
                                        const ScAddress& rCursor, const ScMarkData& rMark,
                                        double& rResult )
{
    ScFunctionData aData(eFunc);

    ScMarkData aMark(rMark);
    aMark.MarkToMulti();
    if (!aMark.IsMultiMarked() && !aMark.IsCellMarked(rCursor.Col(), rCursor.Row()))
        aMark.SetMarkArea(ScRange(rCursor));

    SCTAB nMax = GetTableCount();
    ScMarkData::const_iterator itr = aMark.begin(), itrEnd = aMark.end();

    for (; itr != itrEnd && *itr < nMax && !aData.getError(); ++itr)
        if (maTabs[*itr])
            maTabs[*itr]->UpdateSelectionFunction(aData, aMark);

    rResult = aData.getResult();
    if (aData.getError())
        rResult = 0.0;

    return !aData.getError();
}

double ScDocument::RoundValueAsShown( double fVal, sal_uInt32 nFormat, const ScInterpreterContext* pContext ) const
{
    const SvNumberFormatter* pFormatter = pContext ? pContext->GetFormatTable() : GetFormatTable();
    const SvNumberformat* pFormat = pFormatter->GetEntry( nFormat );
    if (!pFormat)
        return fVal;
    SvNumFormatType nType = pFormat->GetMaskedType();
    if (nType != SvNumFormatType::DATE && nType != SvNumFormatType::TIME && nType != SvNumFormatType::DATETIME )
    {
        // MSVC doesn't recognize all paths init nPrecision and wails about
        // "potentially uninitialized local variable 'nPrecision' used"
        // so init to some random sensible value preserving all decimals.
        short nPrecision = 20;
        bool bStdPrecision = ((nFormat % SV_COUNTRY_LANGUAGE_OFFSET) == 0);
        if (!bStdPrecision)
        {
            sal_uInt16 nIdx = pFormat->GetSubformatIndex( fVal );
            nPrecision = static_cast<short>(pFormat->GetFormatPrecision( nIdx ));
            switch ( nType )
            {
                case SvNumFormatType::PERCENT:      // 0.41% == 0.0041
                    nPrecision += 2;
                    break;
                case SvNumFormatType::SCIENTIFIC:   // 1.23e-3 == 0.00123
                {
                    short nExp = 0;
                    if ( fVal > 0.0 )
                        nExp = static_cast<short>(floor( log10( fVal ) ));
                    else if ( fVal < 0.0 )
                        nExp = static_cast<short>(floor( log10( -fVal ) ));
                    nPrecision -= nExp;
                    short nInteger = static_cast<short>(pFormat->GetFormatIntegerDigits( nIdx ));
                    if ( nInteger > 1 ) // Engineering notation
                    {
                        short nIncrement = nExp % nInteger;
                        if ( nIncrement != 0 )
                        {
                            nPrecision += nIncrement;
                            if (nExp < 0 )
                                nPrecision += nInteger;
                        }
                    }
                    break;
                }
                case SvNumFormatType::FRACTION:     // get value of fraction representation
                {
                    return pFormat->GetRoundFractionValue( fVal );
                }
                case SvNumFormatType::NUMBER:
                case SvNumFormatType::CURRENCY:
                {   // tdf#106253 Thousands divisors for format "0,"
                    const sal_uInt16 nTD = pFormat->GetThousandDivisorPrecision( nIdx );
                    if (nTD == SvNumberFormatter::UNLIMITED_PRECISION)
                        // Format contains General keyword, handled below.
                        bStdPrecision = true;
                    else
                        nPrecision -= nTD;
                    break;
                }
                default: break;
            }
        }
        if (bStdPrecision)
        {
            nPrecision = static_cast<short>(GetDocOptions().GetStdPrecision());
            // #i115512# no rounding for automatic decimals
            if (nPrecision == static_cast<short>(SvNumberFormatter::UNLIMITED_PRECISION))
                return fVal;
        }
        return ::rtl::math::round( fVal, nPrecision );
    }
    else
        return fVal;
}

// conditional formats and validation ranges

sal_uInt32 ScDocument::AddCondFormat( std::unique_ptr<ScConditionalFormat> pNew, SCTAB nTab )
{
    if(!pNew)
        return 0;

    if (ScTable* pTable = FetchTable(nTab))
        return pTable->AddCondFormat(std::move(pNew));

    return 0;
}

sal_uInt32 ScDocument::AddValidationEntry( const ScValidationData& rNew )
{
    if (rNew.IsEmpty())
        return 0;                   // empty is always 0

    if (!pValidationList)
    {
        ScMutationGuard aGuard(*this, ScMutationGuardFlags::CORE);
        pValidationList.reset(new ScValidationDataList);
        mnLastValidationListMax = 0;
    }

    sal_uInt32 nMax = 0;
    if (IsImportingXLSX())
    {
        // During import, the search becomes an O(n^2) problem.
        // Just ignore any duplicates we might find, which seems to be rare.
        nMax = mnLastValidationListMax;
        ++mnLastValidationListMax;
    }
    else
    {
        for( const auto& rxData : *pValidationList )
        {
            const ScValidationData* pData = rxData.get();
            sal_uInt32 nKey = pData->GetKey();
            if ( pData->EqualEntries( rNew ) )
            {
                return nKey;
            }
            if ( nKey > nMax )
                nMax = nKey;
        }
    }

    // might be called from ScPatternAttr::MigrateToDocument; thus clone (real copy)
    sal_uInt32 nNewKey = nMax + 1;
    std::unique_ptr<ScValidationData> pInsert(rNew.Clone(*this));
    pInsert->SetKey( nNewKey );
    ScMutationGuard aGuard(*this, ScMutationGuardFlags::CORE);
    pValidationList->InsertNew( std::move(pInsert) );
    return nNewKey;
}

const SfxPoolItem* ScDocument::GetEffItem(
                        SCCOL nCol, SCROW nRow, SCTAB nTab, sal_uInt16 nWhich ) const
{
    const ScPatternAttr* pPattern = GetPattern( nCol, nRow, nTab );
    if ( pPattern )
    {
        const SfxItemSet& rSet = pPattern->GetItemSet();
        const ScCondFormatItem* pConditionalItem = nullptr;
        if ( rSet.GetItemState( ATTR_CONDITIONAL, true, &pConditionalItem ) == SfxItemState::SET )
        {
            const ScCondFormatIndexes& rIndex = pConditionalItem->GetCondFormatData();
            ScConditionalFormatList* pCondFormList = GetCondFormList( nTab );
            if (!rIndex.empty() && pCondFormList)
            {
                for(const auto& rItem : rIndex)
                {
                    const ScConditionalFormat* pForm = pCondFormList->GetFormat( rItem );
                    if ( pForm )
                    {
                        ScAddress aPos(nCol, nRow, nTab);
                        ScRefCellValue aCell(const_cast<ScDocument&>(*this), aPos);
                        const OUString aStyle = pForm->GetCellStyle(aCell, aPos);
                        if (!aStyle.isEmpty())
                        {
                            SfxStyleSheetBase* pStyleSheet = mxPoolHelper->GetStylePool()->Find(
                                    aStyle, SfxStyleFamily::Para );
                            const SfxPoolItem* pItem = nullptr;
                            if ( pStyleSheet && pStyleSheet->GetItemSet().GetItemState(
                                        nWhich, true, &pItem ) == SfxItemState::SET )
                                return pItem;
                        }
                    }
                }
            }
        }
        return &rSet.Get( nWhich );
    }
    OSL_FAIL("no pattern");
    return nullptr;
}

const SfxItemSet* ScDocument::GetCondResult( SCCOL nCol, SCROW nRow, SCTAB nTab, ScRefCellValue* pCell ) const
{
    ScConditionalFormatList* pFormatList = GetCondFormList(nTab);
    if (!pFormatList)
        return nullptr;

    ScAddress aPos(nCol, nRow, nTab);
    ScRefCellValue aCell;
    if( pCell == nullptr )
    {
        aCell.assign(const_cast<ScDocument&>(*this), aPos);
        pCell = &aCell;
    }
    // if the underlying cell needs evaluation, ScPatternAttr
    // and ScCondFormatIndexes might end up being deleted under
    // us, so we need to trigger evaluation before accessing them.
    if (pCell->getType() == CELLTYPE_FORMULA)
        (void)pCell->getFormula()->IsValue();
    const ScPatternAttr* pPattern = GetPattern( nCol, nRow, nTab );
    const ScCondFormatIndexes& rIndex =
        pPattern->GetItem(ATTR_CONDITIONAL).GetCondFormatData();

    return GetCondResult(*pCell, aPos, *pFormatList, rIndex);
}

const SfxItemSet* ScDocument::GetCondResult(
    const ScRefCellValue& rCell, const ScAddress& rPos, const ScConditionalFormatList& rList,
    const ScCondFormatIndexes& rIndex ) const
{
    for (const auto& rItem : rIndex)
    {
        const ScConditionalFormat* pForm = rList.GetFormat(rItem);
        if (!pForm)
            continue;

        const OUString aStyle = pForm->GetCellStyle(rCell, rPos);
        if (!aStyle.isEmpty())
        {
            SfxStyleSheetBase* pStyleSheet =
                mxPoolHelper->GetStylePool()->Find(aStyle, SfxStyleFamily::Para);

            if (pStyleSheet)
                return &pStyleSheet->GetItemSet();

            // if style is not there, treat like no condition
        }
    }

    return nullptr;
}

ScConditionalFormat* ScDocument::GetCondFormat(
                            SCCOL nCol, SCROW nRow, SCTAB nTab ) const
{
    sal_uInt32 nIndex = 0;
    const ScCondFormatIndexes& rCondFormats = GetAttr(nCol, nRow, nTab, ATTR_CONDITIONAL)->GetCondFormatData();

    if(!rCondFormats.empty())
        nIndex = rCondFormats[0];

    if (nIndex)
    {
        ScConditionalFormatList* pCondFormList = GetCondFormList(nTab);
        if (pCondFormList)
            return pCondFormList->GetFormat( nIndex );
        else
        {
            OSL_FAIL("pCondFormList is 0");
        }
    }

    return nullptr;
}

ScConditionalFormatList* ScDocument::GetCondFormList(SCTAB nTab) const
{
    if (HasTable(nTab))
        return maTabs[nTab]->GetCondFormList();
    return nullptr;
}

void ScDocument::SetCondFormList( ScConditionalFormatList* pList, SCTAB nTab )
{
    if (ScTable* pTable = FetchTable(nTab))
        pTable->SetCondFormList(pList);
}

const ScValidationData* ScDocument::GetValidationEntry( sal_uInt32 nIndex ) const
{
    if ( pValidationList )
        return pValidationList->GetData( nIndex );
    else
        return nullptr;
}

void ScDocument::DeleteConditionalFormat(sal_uLong nOldIndex, SCTAB nTab)
{
    if (ScTable* pTable = FetchTable(nTab))
        pTable->DeleteConditionalFormat(nOldIndex);
}

bool ScDocument::HasDetectiveOperations() const
{
    return pDetOpList && pDetOpList->Count();
}

void ScDocument::AddDetectiveOperation( const ScDetOpData& rData )
{
    if (!pDetOpList)
        pDetOpList.reset(new ScDetOpList);

    pDetOpList->Append( rData );
}

void ScDocument::ClearDetectiveOperations()
{
    pDetOpList.reset();      // deletes also the entries
}

void ScDocument::SetDetOpList(std::unique_ptr<ScDetOpList> pNew)
{
    pDetOpList = std::move(pNew);
}

// Comparison of Documents

//  Pfriemel-Factors
#define SC_DOCCOMP_MAXDIFF  256
#define SC_DOCCOMP_MINGOOD  128
#define SC_DOCCOMP_COLUMNS  10
#define SC_DOCCOMP_ROWS     100

sal_uInt16 ScDocument::RowDifferences( SCROW nThisRow, SCTAB nThisTab,
                                    ScDocument& rOtherDoc, SCROW nOtherRow, SCTAB nOtherTab,
                                    SCCOL nMaxCol, const SCCOLROW* pOtherCols )
{
    sal_uLong nDif = 0;
    sal_uLong nUsed = 0;
    for (SCCOL nThisCol=0; nThisCol<=nMaxCol; nThisCol++)
    {
        SCCOL nOtherCol;
        if ( pOtherCols )
            nOtherCol = static_cast<SCCOL>(pOtherCols[nThisCol]);
        else
            nOtherCol = nThisCol;

        if (ValidCol(nOtherCol))    // only compare columns that are common to both docs
        {
            ScRefCellValue aThisCell(*this, ScAddress(nThisCol, nThisRow, nThisTab));
            ScRefCellValue aOtherCell(rOtherDoc, ScAddress(nOtherCol, nOtherRow, nOtherTab));
            if (!aThisCell.equalsWithoutFormat(aOtherCell))
            {
                if (!aThisCell.isEmpty() && !aOtherCell.isEmpty())
                    nDif += 3;
                else
                    nDif += 4;      // content <-> empty counts more
            }

            if (!aThisCell.isEmpty() || !aOtherCell.isEmpty())
                ++nUsed;
        }
    }

    if (nUsed > 0)
        return static_cast<sal_uInt16>((nDif*64)/nUsed);            // max.256 (SC_DOCCOMP_MAXDIFF)

    OSL_ENSURE(!nDif,"Diff without Used");
    return 0;
}

sal_uInt16 ScDocument::ColDifferences( SCCOL nThisCol, SCTAB nThisTab,
                                    ScDocument& rOtherDoc, SCCOL nOtherCol, SCTAB nOtherTab,
                                    SCROW nMaxRow, const SCCOLROW* pOtherRows )
{

    //TODO: optimize e.g. with iterator?

    sal_uInt64 nDif = 0;
    sal_uInt64 nUsed = 0;
    for (SCROW nThisRow=0; nThisRow<=nMaxRow; nThisRow++)
    {
        SCROW nOtherRow;
        if ( pOtherRows )
            nOtherRow = pOtherRows[nThisRow];
        else
            nOtherRow = nThisRow;

        if (ValidRow(nOtherRow))    // only compare rows that are common to both docs
        {
            ScRefCellValue aThisCell(*this, ScAddress(nThisCol, nThisRow, nThisTab));
            ScRefCellValue aOtherCell(rOtherDoc, ScAddress(nOtherCol, nOtherRow, nOtherTab));
            if (!aThisCell.equalsWithoutFormat(aOtherCell))
            {
                if (!aThisCell.isEmpty() && !aOtherCell.isEmpty())
                    nDif += 3;
                else
                    nDif += 4;      // content <-> empty counts more
            }

            if (!aThisCell.isEmpty() || !aOtherCell.isEmpty())
                ++nUsed;
        }
    }

    if (nUsed > 0)
        return static_cast<sal_uInt16>((nDif*64)/nUsed);    // max.256

    OSL_ENSURE(!nDif,"Diff without Used");
    return 0;
}

void ScDocument::FindOrder( SCCOLROW* pOtherRows, SCCOLROW nThisEndRow, SCCOLROW nOtherEndRow,
                            bool bColumns, ScDocument& rOtherDoc, SCTAB nThisTab, SCTAB nOtherTab,
                            SCCOLROW nEndCol, const SCCOLROW* pTranslate, ScProgress* pProgress, sal_uInt64 nProAdd )
{
    //  bColumns=true: rows are columns and vice versa

    SCCOLROW nMaxCont;                      // continue by how much
    SCCOLROW nMinGood;                      // what is a hit (incl.)
    if ( bColumns )
    {
        nMaxCont = SC_DOCCOMP_COLUMNS;      // 10 columns
        nMinGood = SC_DOCCOMP_MINGOOD;

        //TODO: additional pass with nMinGood = 0 ????

    }
    else
    {
        nMaxCont = SC_DOCCOMP_ROWS;         // 100 rows
        nMinGood = SC_DOCCOMP_MINGOOD;
    }
    bool bUseTotal = bColumns && !pTranslate;       // only for the 1st pass

    SCCOLROW nOtherRow = 0;
    sal_uInt16 nComp;
    SCCOLROW nThisRow;
    bool bTotal = false;        // hold for several nThisRow
    SCCOLROW nUnknown = 0;
    for (nThisRow = 0; nThisRow <= nThisEndRow; nThisRow++)
    {
        SCCOLROW nTempOther = nOtherRow;
        bool bFound = false;
        sal_uInt16 nBest = SC_DOCCOMP_MAXDIFF;
        SCCOLROW nMax = std::min( nOtherEndRow, static_cast<SCCOLROW>(( nTempOther + nMaxCont + nUnknown )) );
        for (SCCOLROW i=nTempOther; i<=nMax && nBest>0; i++)    // stop at 0
        {
            if (bColumns)
                nComp = ColDifferences( static_cast<SCCOL>(nThisRow), nThisTab, rOtherDoc, static_cast<SCCOL>(i), nOtherTab, nEndCol, pTranslate );
            else
                nComp = RowDifferences( nThisRow, nThisTab, rOtherDoc, i, nOtherTab, static_cast<SCCOL>(nEndCol), pTranslate );
            if ( nComp < nBest && ( nComp <= nMinGood || bTotal ) )
            {
                nTempOther = i;
                nBest = nComp;
                bFound = true;
            }
            if ( nComp < SC_DOCCOMP_MAXDIFF || bFound )
                bTotal = false;
            else if ( i == nTempOther && bUseTotal )
                bTotal = true;                          // only at the very top
        }
        if ( bFound )
        {
            pOtherRows[nThisRow] = nTempOther;
            nOtherRow = nTempOther + 1;
            nUnknown = 0;
        }
        else
        {
            pOtherRows[nThisRow] = SCROW_MAX;
            ++nUnknown;
        }

        if (pProgress)
            pProgress->SetStateOnPercent(nProAdd+static_cast<sal_uLong>(nThisRow));
    }

    // fill in blocks that don't match

    SCROW nFillStart = 0;
    SCROW nFillPos = 0;
    bool bInFill = false;
    for (nThisRow = 0; nThisRow <= nThisEndRow+1; nThisRow++)
    {
        SCROW nThisOther = ( nThisRow <= nThisEndRow ) ? pOtherRows[nThisRow] : (nOtherEndRow+1);
        if ( ValidRow(nThisOther) )
        {
            if ( bInFill )
            {
                if ( nThisOther > nFillStart )      // is there something to distribute?
                {
                    SCROW nDiff1 = nThisOther - nFillStart;
                    SCROW nDiff2 = nThisRow   - nFillPos;
                    SCROW nMinDiff = std::min(nDiff1, nDiff2);
                    for (SCROW i=0; i<nMinDiff; i++)
                        pOtherRows[nFillPos+i] = nFillStart+i;
                }

                bInFill = false;
            }
            nFillStart = nThisOther + 1;
            nFillPos = nThisRow + 1;
        }
        else
            bInFill = true;
    }
}

void ScDocument::CompareDocument( ScDocument& rOtherDoc )
{
    if (!pChangeTrack)
        return;

    SCTAB nThisCount = GetTableCount();
    SCTAB nOtherCount = rOtherDoc.GetTableCount();
    std::unique_ptr<SCTAB[]> pOtherTabs(new SCTAB[nThisCount]);
    SCTAB nThisTab;

    //  compare tables with identical names
    OUString aThisName;
    OUString aOtherName;
    for (nThisTab=0; nThisTab<nThisCount; nThisTab++)
    {
        SCTAB nOtherTab = SCTAB_MAX;
        if (!IsScenario(nThisTab))  // skip scenarios
        {
            GetName( nThisTab, aThisName );
            for (SCTAB nTemp=0; nTemp<nOtherCount && nOtherTab>MAXTAB; nTemp++)
                if (!rOtherDoc.IsScenario(nTemp))
                {
                    rOtherDoc.GetName( nTemp, aOtherName );
                    if ( aThisName == aOtherName )
                        nOtherTab = nTemp;
                }
        }
        pOtherTabs[nThisTab] = nOtherTab;
    }
    //  fill in, so that un-named tables don't get lost
    SCTAB nFillStart = 0;
    SCTAB nFillPos = 0;
    bool bInFill = false;
    for (nThisTab = 0; nThisTab <= nThisCount; nThisTab++)
    {
        SCTAB nThisOther = ( nThisTab < nThisCount ) ? pOtherTabs[nThisTab] : nOtherCount;
        if ( ValidTab(nThisOther) )
        {
            if ( bInFill )
            {
                if ( nThisOther > nFillStart )      // is there something to distribute?
                {
                    SCTAB nDiff1 = nThisOther - nFillStart;
                    SCTAB nDiff2 = nThisTab   - nFillPos;
                    SCTAB nMinDiff = std::min(nDiff1, nDiff2);
                    for (SCTAB i=0; i<nMinDiff; i++)
                        if ( !IsScenario(nFillPos+i) && !rOtherDoc.IsScenario(nFillStart+i) )
                            pOtherTabs[nFillPos+i] = nFillStart+i;
                }

                bInFill = false;
            }
            nFillStart = nThisOther + 1;
            nFillPos = nThisTab + 1;
        }
        else
            bInFill = true;
    }

    //  compare tables in the original order

    for (nThisTab=0; nThisTab<nThisCount; nThisTab++)
    {
        SCTAB nOtherTab = pOtherTabs[nThisTab];
        if ( ValidTab(nOtherTab) )
        {
            SCCOL nThisEndCol = 0;
            SCROW nThisEndRow = 0;
            SCCOL nOtherEndCol = 0;
            SCROW nOtherEndRow = 0;
            GetCellArea( nThisTab, nThisEndCol, nThisEndRow );
            rOtherDoc.GetCellArea( nOtherTab, nOtherEndCol, nOtherEndRow );
            SCCOL nEndCol = std::max(nThisEndCol, nOtherEndCol);
            SCROW nEndRow = std::max(nThisEndRow, nOtherEndRow);
            SCCOL nThisCol;
            SCROW nThisRow;
            sal_uLong n1,n2;    // for AppendDeleteRange

            //TODO: one Progress over all tables ???

            OUString aTabName;
            GetName( nThisTab, aTabName );
            OUString aTemplate = ScResId(STR_PROGRESS_COMPARING);
            sal_Int32 nIndex = 0;
            OUString aProText = o3tl::getToken(aTemplate, 0, '#', nIndex ) +
                aTabName +
                o3tl::getToken(aTemplate, 0, '#', nIndex );
            ScProgress aProgress( GetDocumentShell(), aProText, 3*nThisEndRow, true );  // 2x FindOrder, 1x here
            tools::Long nProgressStart = 2*nThisEndRow;                    // start for here

            std::unique_ptr<SCCOLROW[]> pTempRows(new SCCOLROW[nThisEndRow+1]);
            std::unique_ptr<SCCOLROW[]> pOtherRows(new SCCOLROW[nThisEndRow+1]);
            std::unique_ptr<SCCOLROW[]> pOtherCols(new SCCOLROW[nThisEndCol+1]);

            //  find inserted/deleted columns/rows:
            //  Two attempts:
            //  1) compare original rows                    (pTempRows)
            //  2) compare original columns                 (pOtherCols)
            //     with this column order compare rows      (pOtherRows)

            //TODO: compare columns twice with different nMinGood ???

            // 1
            FindOrder( pTempRows.get(), nThisEndRow, nOtherEndRow, false,
                        rOtherDoc, nThisTab, nOtherTab, nEndCol, nullptr, &aProgress, 0 );
            // 2
            FindOrder( pOtherCols.get(), nThisEndCol, nOtherEndCol, true,
                        rOtherDoc, nThisTab, nOtherTab, nEndRow, nullptr, nullptr, 0 );
            FindOrder( pOtherRows.get(), nThisEndRow, nOtherEndRow, false,
                        rOtherDoc, nThisTab, nOtherTab, nThisEndCol,
                       pOtherCols.get(), &aProgress, nThisEndRow );

            sal_uLong nMatch1 = 0;  // pTempRows, no columns
            for (nThisRow = 0; nThisRow<=nThisEndRow; nThisRow++)
                if (ValidRow(pTempRows[nThisRow]))
                    nMatch1 += SC_DOCCOMP_MAXDIFF -
                               RowDifferences( nThisRow, nThisTab, rOtherDoc, pTempRows[nThisRow],
                                                nOtherTab, nEndCol, nullptr );

            sal_uLong nMatch2 = 0;  // pOtherRows, pOtherCols
            for (nThisRow = 0; nThisRow<=nThisEndRow; nThisRow++)
                if (ValidRow(pOtherRows[nThisRow]))
                    nMatch2 += SC_DOCCOMP_MAXDIFF -
                               RowDifferences( nThisRow, nThisTab, rOtherDoc, pOtherRows[nThisRow],
                                               nOtherTab, nThisEndCol, pOtherCols.get() );

            if ( nMatch1 >= nMatch2 )           // without columns ?
            {
                //  reset columns
                for (nThisCol = 0; nThisCol<=nThisEndCol; nThisCol++)
                    pOtherCols[nThisCol] = nThisCol;

                //  swap row-arrays (they get both deleted anyway)
                pTempRows.swap(pOtherRows);
            }
            else
            {
                //  remains for pOtherCols, pOtherRows
            }

            //  Generate Change-Actions
            //  1) columns from the right
            //  2) rows from below
            //  3) single cells in normal order

            //  Actions for inserted/deleted columns

            SCCOL nLastOtherCol = static_cast<SCCOL>(nOtherEndCol + 1);
            //  nThisEndCol ... 0
            for ( nThisCol = nThisEndCol+1; nThisCol > 0; )
            {
                --nThisCol;
                SCCOL nOtherCol = static_cast<SCCOL>(pOtherCols[nThisCol]);
                if ( ValidCol(nOtherCol) && nOtherCol+1 < nLastOtherCol )
                {
                    // gap -> deleted
                    ScRange aDelRange( nOtherCol+1, 0, nOtherTab,
                                        nLastOtherCol-1, MaxRow(), nOtherTab );
                    pChangeTrack->AppendDeleteRange( aDelRange, &rOtherDoc, n1, n2 );
                }
                if ( nOtherCol > MaxCol() )                       // inserted
                {
                    //  combine
                    if ( nThisCol == nThisEndCol || ValidCol(static_cast<SCCOL>(pOtherCols[nThisCol+1])) )
                    {
                        SCCOL nFirstNew = nThisCol;
                        while ( nFirstNew > 0 && pOtherCols[nFirstNew-1] > MaxCol() )
                            --nFirstNew;
                        SCCOL nDiff = nThisCol - nFirstNew;
                        ScRange aRange( nLastOtherCol, 0, nOtherTab,
                                        nLastOtherCol+nDiff, MaxRow(), nOtherTab );
                        pChangeTrack->AppendInsert( aRange );
                    }
                }
                else
                    nLastOtherCol = nOtherCol;
            }
            if ( nLastOtherCol > 0 )                            // deleted at the very top
            {
                ScRange aDelRange( 0, 0, nOtherTab,
                                    nLastOtherCol-1, MaxRow(), nOtherTab );
                pChangeTrack->AppendDeleteRange( aDelRange, &rOtherDoc, n1, n2 );
            }

            //  Actions for inserted/deleted rows

            SCROW nLastOtherRow = nOtherEndRow + 1;
            //  nThisEndRow ... 0
            for ( nThisRow = nThisEndRow+1; nThisRow > 0; )
            {
                --nThisRow;
                SCROW nOtherRow = pOtherRows[nThisRow];
                if ( ValidRow(nOtherRow) && nOtherRow+1 < nLastOtherRow )
                {
                    // gap -> deleted
                    ScRange aDelRange( 0, nOtherRow+1, nOtherTab,
                                        MaxCol(), nLastOtherRow-1, nOtherTab );
                    pChangeTrack->AppendDeleteRange( aDelRange, &rOtherDoc, n1, n2 );
                }
                if ( nOtherRow > MaxRow() )                       // inserted
                {
                    //  combine
                    if ( nThisRow == nThisEndRow || ValidRow(pOtherRows[nThisRow+1]) )
                    {
                        SCROW nFirstNew = nThisRow;
                        while ( nFirstNew > 0 && pOtherRows[nFirstNew-1] > MaxRow() )
                            --nFirstNew;
                        SCROW nDiff = nThisRow - nFirstNew;
                        ScRange aRange( 0, nLastOtherRow, nOtherTab,
                                        MaxCol(), nLastOtherRow+nDiff, nOtherTab );
                        pChangeTrack->AppendInsert( aRange );
                    }
                }
                else
                    nLastOtherRow = nOtherRow;
            }
            if ( nLastOtherRow > 0 )                            // deleted at the very top
            {
                ScRange aDelRange( 0, 0, nOtherTab,
                                    MaxCol(), nLastOtherRow-1, nOtherTab );
                pChangeTrack->AppendDeleteRange( aDelRange, &rOtherDoc, n1, n2 );
            }

             //  walk rows to find single cells

            for (nThisRow = 0; nThisRow <= nThisEndRow; nThisRow++)
            {
                SCROW nOtherRow = pOtherRows[nThisRow];
                for (nThisCol = 0; nThisCol <= nThisEndCol; nThisCol++)
                {
                    SCCOL nOtherCol = static_cast<SCCOL>(pOtherCols[nThisCol]);
                    ScAddress aThisPos( nThisCol, nThisRow, nThisTab );
                    ScCellValue aThisCell;
                    aThisCell.assign(*this, aThisPos);
                    ScCellValue aOtherCell; // start empty
                    if ( ValidCol(nOtherCol) && ValidRow(nOtherRow) )
                    {
                        ScAddress aOtherPos( nOtherCol, nOtherRow, nOtherTab );
                        aOtherCell.assign(rOtherDoc, aOtherPos);
                    }

                    if (!aThisCell.equalsWithoutFormat(aOtherCell))
                    {
                        ScRange aRange( aThisPos );
                        ScChangeActionContent* pAction = new ScChangeActionContent( aRange );
                        pAction->SetOldValue(aOtherCell, &rOtherDoc, this);
                        pAction->SetNewValue(aThisCell, this);
                        pChangeTrack->Append( pAction );
                    }
                }
                aProgress.SetStateOnPercent(nProgressStart+nThisRow);
            }
        }
    }
}

sal_Unicode ScDocument::GetSheetSeparator() const
{
    const ScCompiler::Convention* pConv = ScCompiler::GetRefConvention(
            FormulaGrammar::extractRefConvention( GetGrammar()));
    assert(pConv);
    return pConv ? pConv->getSpecialSymbol( ScCompiler::Convention::SHEET_SEPARATOR) : '.';
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
