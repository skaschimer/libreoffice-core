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

#include <dptabres.hxx>

#include <dptabdat.hxx>
#include <dptabsrc.hxx>
#include <global.hxx>
#include <subtotal.hxx>
#include <globstr.hrc>
#include <scresid.hxx>
#include <dpitemdata.hxx>
#include <generalfunction.hxx>

#include <document.hxx>
#include <dpresfilter.hxx>
#include <dputil.hxx>

#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>
#include <rtl/math.hxx>
#include <sal/log.hxx>

#include <math.h>
#include <float.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_map>

#include <com/sun/star/sheet/DataResultFlags.hpp>
#include <com/sun/star/sheet/MemberResultFlags.hpp>
#include <com/sun/star/sheet/DataPilotFieldReferenceType.hpp>
#include <com/sun/star/sheet/DataPilotFieldReferenceItemType.hpp>
#include <com/sun/star/sheet/DataPilotFieldShowItemsMode.hpp>
#include <com/sun/star/sheet/DataPilotFieldSortMode.hpp>
#include <com/sun/star/sheet/GeneralFunction2.hpp>

using namespace com::sun::star;
using ::std::vector;
using ::std::pair;
using ::com::sun::star::uno::Sequence;

namespace {

const TranslateId aFuncStrIds[] =     // matching enum ScSubTotalFunc
{
    {},                             // SUBTOTAL_FUNC_NONE
    STR_FUN_TEXT_AVG,               // SUBTOTAL_FUNC_AVE
    STR_FUN_TEXT_COUNT,             // SUBTOTAL_FUNC_CNT
    STR_FUN_TEXT_COUNT,             // SUBTOTAL_FUNC_CNT2
    STR_FUN_TEXT_MAX,               // SUBTOTAL_FUNC_MAX
    STR_FUN_TEXT_MIN,               // SUBTOTAL_FUNC_MIN
    STR_FUN_TEXT_PRODUCT,           // SUBTOTAL_FUNC_PROD
    STR_FUN_TEXT_STDDEV,            // SUBTOTAL_FUNC_STD
    STR_FUN_TEXT_STDDEV,            // SUBTOTAL_FUNC_STDP
    STR_FUN_TEXT_SUM,               // SUBTOTAL_FUNC_SUM
    STR_FUN_TEXT_VAR,               // SUBTOTAL_FUNC_VAR
    STR_FUN_TEXT_VAR,               // SUBTOTAL_FUNC_VARP
    STR_FUN_TEXT_MEDIAN,            // SUBTOTAL_FUNC_MED
    {}                              // SUBTOTAL_FUNC_SELECTION_COUNT - not used for pivot table
};

bool lcl_SearchMember( const std::vector<std::unique_ptr<ScDPResultMember>>& list, SCROW nOrder, SCROW& rIndex)
{
    bool bFound = false;
    SCROW  nLo = 0;
    SCROW nHi = list.size() - 1;
    SCROW nIndex;
    while (nLo <= nHi)
    {
        nIndex = (nLo + nHi) / 2;
        if ( list[nIndex]->GetOrder() < nOrder )
            nLo = nIndex + 1;
        else
        {
            nHi = nIndex - 1;
            if ( list[nIndex]->GetOrder() == nOrder )
            {
                bFound = true;
                nLo = nIndex;
            }
        }
    }
    rIndex = nLo;
    return bFound;
}

class FilterStack
{
    std::vector<ScDPResultFilter>& mrFilters;
public:
    explicit FilterStack(std::vector<ScDPResultFilter>& rFilters) : mrFilters(rFilters) {}

    void pushDimName(const OUString& rName, bool bDataLayout)
    {
        mrFilters.emplace_back(rName, bDataLayout);
    }

    void pushDimValue(const OUString& rValueName, const OUString& rValue)
    {
        ScDPResultFilter& rFilter = mrFilters.back();
        rFilter.maValueName = rValueName;
        rFilter.maValue = rValue;
        rFilter.mbHasValue = true;
    }

    ~FilterStack()
    {
        ScDPResultFilter& rFilter = mrFilters.back();
        if (rFilter.mbHasValue)
            rFilter.mbHasValue = false;
        else
            mrFilters.pop_back();
    }
};

// function objects for sorting of the column and row members:

class ScDPRowMembersOrder
{
    ScDPResultDimension& rDimension;
    tools::Long                 nMeasure;
    bool                 bAscending;

public:
            ScDPRowMembersOrder( ScDPResultDimension& rDim, tools::Long nM, bool bAsc ) :
                rDimension(rDim),
                nMeasure(nM),
                bAscending(bAsc)
            {}

    bool operator()( sal_Int32 nIndex1, sal_Int32 nIndex2 ) const;
};

class ScDPColMembersOrder
{
    ScDPDataDimension& rDimension;
    tools::Long               nMeasure;
    bool               bAscending;

public:
            ScDPColMembersOrder( ScDPDataDimension& rDim, tools::Long nM, bool bAsc ) :
                rDimension(rDim),
                nMeasure(nM),
                bAscending(bAsc)
            {}

    bool operator()( sal_Int32 nIndex1, sal_Int32 nIndex2 ) const;
};

}

static bool lcl_IsLess( const ScDPDataMember* pDataMember1, const ScDPDataMember* pDataMember2, tools::Long nMeasure, bool bAscending )
{
    // members can be NULL if used for rows

    ScDPSubTotalState aEmptyState;
    const ScDPAggData* pAgg1 = pDataMember1 ? pDataMember1->GetConstAggData( nMeasure, aEmptyState ) : nullptr;
    const ScDPAggData* pAgg2 = pDataMember2 ? pDataMember2->GetConstAggData( nMeasure, aEmptyState ) : nullptr;

    bool bError1 = pAgg1 && pAgg1->HasError();
    bool bError2 = pAgg2 && pAgg2->HasError();
    if ( bError1 )
        return false;       // errors are always sorted at the end
    else if ( bError2 )
        return true;            // errors are always sorted at the end
    else
    {
        double fVal1 = ( pAgg1 && pAgg1->HasData() ) ? pAgg1->GetResult() : 0.0;    // no data is sorted as 0
        double fVal2 = ( pAgg2 && pAgg2->HasData() ) ? pAgg2->GetResult() : 0.0;

        // compare values
        // don't have to check approxEqual, as this is the only sort criterion

        return bAscending ? ( fVal1 < fVal2 ) : ( fVal1 > fVal2 );
    }
}

static bool lcl_IsEqual( const ScDPDataMember* pDataMember1, const ScDPDataMember* pDataMember2, tools::Long nMeasure )
{
    // members can be NULL if used for rows

    ScDPSubTotalState aEmptyState;
    const ScDPAggData* pAgg1 = pDataMember1 ? pDataMember1->GetConstAggData( nMeasure, aEmptyState ) : nullptr;
    const ScDPAggData* pAgg2 = pDataMember2 ? pDataMember2->GetConstAggData( nMeasure, aEmptyState ) : nullptr;

    bool bError1 = pAgg1 && pAgg1->HasError();
    bool bError2 = pAgg2 && pAgg2->HasError();
    if ( bError1 )
    {
        if ( bError2 )
            return true;        // equal
        else
            return false;
    }
    else if ( bError2 )
        return false;
    else
    {
        double fVal1 = ( pAgg1 && pAgg1->HasData() ) ? pAgg1->GetResult() : 0.0;    // no data is sorted as 0
        double fVal2 = ( pAgg2 && pAgg2->HasData() ) ? pAgg2->GetResult() : 0.0;

        // compare values
        // this is used to find equal data at the end of the AutoShow range, so approxEqual must be used

        return rtl::math::approxEqual( fVal1, fVal2 );
    }
}

bool ScDPRowMembersOrder::operator()( sal_Int32 nIndex1, sal_Int32 nIndex2 ) const
{
    const ScDPResultMember* pMember1 = rDimension.GetMember(nIndex1);
    const ScDPResultMember* pMember2 = rDimension.GetMember(nIndex2);

// make the hide item to the largest order.
    if ( !pMember1->IsVisible() || !pMember2->IsVisible() )
        return pMember1->IsVisible();
    const ScDPDataMember* pDataMember1 =  pMember1->GetDataRoot() ;
    const ScDPDataMember* pDataMember2 =  pMember2->GetDataRoot();
    //  GetDataRoot can be NULL if there was no data.
    //  IsVisible == false can happen after AutoShow.
    return lcl_IsLess( pDataMember1, pDataMember2, nMeasure, bAscending );
}

bool ScDPColMembersOrder::operator()( sal_Int32 nIndex1, sal_Int32 nIndex2 ) const
{
    const ScDPDataMember* pDataMember1 = rDimension.GetMember(nIndex1);
    const ScDPDataMember* pDataMember2 = rDimension.GetMember(nIndex2);
    bool bHide1 = pDataMember1 && !pDataMember1->IsVisible();
    bool bHide2 =  pDataMember2 && !pDataMember2->IsVisible();
    if ( bHide1 || bHide2 )
        return !bHide1;
    return lcl_IsLess( pDataMember1, pDataMember2, nMeasure, bAscending );
}

ScDPInitState::Member::Member(tools::Long nSrcIndex, SCROW nNameIndex) :
    mnSrcIndex(nSrcIndex), mnNameIndex(nNameIndex) {}

void ScDPInitState::AddMember( tools::Long nSourceIndex, SCROW nMember )
{
    maMembers.emplace_back(nSourceIndex, nMember);
}

void ScDPInitState::RemoveMember()
{
    OSL_ENSURE(!maMembers.empty(), "ScDPInitState::RemoveMember: Attempt to remove member while empty.");
    if (!maMembers.empty())
        maMembers.pop_back();
}

namespace {

#if DUMP_PIVOT_TABLE
void dumpRow(
    const OUString& rType, const OUString& rName, const ScDPAggData* pAggData,
    ScDocument* pDoc, ScAddress& rPos )
{
    SCCOL nCol = rPos.Col();
    SCROW nRow = rPos.Row();
    SCTAB nTab = rPos.Tab();
    pDoc->SetString( nCol++, nRow, nTab, rType );
    pDoc->SetString( nCol++, nRow, nTab, rName );
    while ( pAggData )
    {
        pDoc->SetValue( nCol++, nRow, nTab, pAggData->GetResult() );
        pAggData = pAggData->GetExistingChild();
    }
    rPos.SetRow( nRow + 1 );
}

void indent( ScDocument* pDoc, SCROW nStartRow, const ScAddress& rPos )
{
    SCCOL nCol = rPos.Col();
    SCTAB nTab = rPos.Tab();

    OUString aString;
    for (SCROW nRow = nStartRow; nRow < rPos.Row(); nRow++)
    {
        aString = pDoc->GetString(nCol, nRow, nTab);
        if (!aString.isEmpty())
        {
            aString = " " + aString;
            pDoc->SetString( nCol, nRow, nTab, aString );
        }
    }
}
#endif

}

ScDPRunningTotalState::ScDPRunningTotalState( ScDPResultMember* pColRoot, ScDPResultMember* pRowRoot ) :
    pColResRoot(pColRoot), pRowResRoot(pRowRoot)
{
    // These arrays should never be empty as the terminating value must be present at all times.
    maColVisible.push_back(-1);
    maColSorted.push_back(-1);
    maRowVisible.push_back(-1);
    maRowSorted.push_back(-1);
}

void ScDPRunningTotalState::AddColIndex( sal_Int32 nVisible, tools::Long nSorted )
{
    maColVisible.back() = nVisible;
    maColVisible.push_back(-1);

    maColSorted.back() = nSorted;
    maColSorted.push_back(-1);
}

void ScDPRunningTotalState::AddRowIndex( sal_Int32 nVisible, tools::Long nSorted )
{
    maRowVisible.back() = nVisible;
    maRowVisible.push_back(-1);

    maRowSorted.back() = nSorted;
    maRowSorted.push_back(-1);
}

void ScDPRunningTotalState::RemoveColIndex()
{
    OSL_ENSURE(!maColVisible.empty() && !maColSorted.empty(), "ScDPRunningTotalState::RemoveColIndex: array is already empty!");
    if (maColVisible.size() >= 2)
    {
        maColVisible.pop_back();
        maColVisible.back() = -1;
    }

    if (maColSorted.size() >= 2)
    {
        maColSorted.pop_back();
        maColSorted.back() = -1;
    }
}

void ScDPRunningTotalState::RemoveRowIndex()
{
    OSL_ENSURE(!maRowVisible.empty() && !maRowSorted.empty(), "ScDPRunningTotalState::RemoveRowIndex: array is already empty!");
    if (maRowVisible.size() >= 2)
    {
        maRowVisible.pop_back();
        maRowVisible.back() = -1;
    }

    if (maRowSorted.size() >= 2)
    {
        maRowSorted.pop_back();
        maRowSorted.back() = -1;
    }
}

ScDPRelativePos::ScDPRelativePos( tools::Long nBase, tools::Long nDir ) :
    nBasePos( nBase ),
    nDirection( nDir )
{
}

void ScDPAggData::Update( const ScDPValue& rNext, ScSubTotalFunc eFunc, const ScDPSubTotalState& rSubState )
{
    if (nCount<0)       // error?
        return;         // nothing more...

    if (rNext.meType == ScDPValue::Empty)
        return;

    if ( rSubState.eColForce != SUBTOTAL_FUNC_NONE && rSubState.eRowForce != SUBTOTAL_FUNC_NONE &&
                                                        rSubState.eColForce != rSubState.eRowForce )
        return;
    if ( rSubState.eColForce != SUBTOTAL_FUNC_NONE ) eFunc = rSubState.eColForce;
    if ( rSubState.eRowForce != SUBTOTAL_FUNC_NONE ) eFunc = rSubState.eRowForce;

    if ( eFunc == SUBTOTAL_FUNC_NONE )
        return;

    if ( eFunc != SUBTOTAL_FUNC_CNT2 )          // CNT2 counts everything, incl. strings and errors
    {
        if (rNext.meType == ScDPValue::Error)
        {
            nCount = -1;        // -1 for error (not for CNT2)
            return;
        }
        if (rNext.meType == ScDPValue::String)
            return;             // ignore
    }

    ++nCount;           // for all functions

    switch (eFunc)
    {
        case SUBTOTAL_FUNC_SUM:
        case SUBTOTAL_FUNC_AVE:
            if ( !SubTotal::SafePlus( fVal, rNext.mfValue ) )
                nCount = -1;                            // -1 for error
            break;
        case SUBTOTAL_FUNC_PROD:
            if ( nCount == 1 )          // copy first value (fVal is initialized to 0)
                fVal = rNext.mfValue;
            else if ( !SubTotal::SafeMult( fVal, rNext.mfValue ) )
                nCount = -1;                            // -1 for error
            break;
        case SUBTOTAL_FUNC_CNT:
        case SUBTOTAL_FUNC_CNT2:
            //  nothing more than incrementing nCount
            break;
        case SUBTOTAL_FUNC_MAX:
            if ( nCount == 1 || rNext.mfValue > fVal )
                fVal = rNext.mfValue;
            break;
        case SUBTOTAL_FUNC_MIN:
            if ( nCount == 1 || rNext.mfValue < fVal )
                fVal = rNext.mfValue;
            break;
        case SUBTOTAL_FUNC_STD:
        case SUBTOTAL_FUNC_STDP:
        case SUBTOTAL_FUNC_VAR:
        case SUBTOTAL_FUNC_VARP:
            maWelford.update( rNext.mfValue);
            break;
        case SUBTOTAL_FUNC_MED:
            {
                auto aIter = std::upper_bound(mSortedValues.begin(), mSortedValues.end(), rNext.mfValue);
                if (aIter == mSortedValues.end())
                    mSortedValues.push_back(rNext.mfValue);
                else
                    mSortedValues.insert(aIter, rNext.mfValue);
            }
            break;
        default:
            OSL_FAIL("invalid function");
    }
}

void ScDPAggData::Calculate( ScSubTotalFunc eFunc, const ScDPSubTotalState& rSubState )
{
    //  calculate the original result
    //  (without reference value, used as the basis for reference value calculation)

    //  called several times at the cross-section of several subtotals - don't calculate twice then
    if ( IsCalculated() )
        return;

    if ( rSubState.eColForce != SUBTOTAL_FUNC_NONE ) eFunc = rSubState.eColForce;
    if ( rSubState.eRowForce != SUBTOTAL_FUNC_NONE ) eFunc = rSubState.eRowForce;

    if ( eFunc == SUBTOTAL_FUNC_NONE )      // this happens when there is no data dimension
    {
        nCount = SC_DPAGG_RESULT_EMPTY;     // make sure there's a valid state for HasData etc.
        return;
    }

    //  check the error conditions for the selected function

    bool bError = false;
    switch (eFunc)
    {
        case SUBTOTAL_FUNC_SUM:
        case SUBTOTAL_FUNC_PROD:
        case SUBTOTAL_FUNC_CNT:
        case SUBTOTAL_FUNC_CNT2:
            bError = ( nCount < 0 );        // only real errors
            break;

        case SUBTOTAL_FUNC_AVE:
        case SUBTOTAL_FUNC_MED:
        case SUBTOTAL_FUNC_MAX:
        case SUBTOTAL_FUNC_MIN:
            bError = ( nCount <= 0 );       // no data is an error
            break;

        case SUBTOTAL_FUNC_STDP:
        case SUBTOTAL_FUNC_VARP:
            bError = ( nCount <= 0 );       // no data is an error
            assert(bError || nCount == static_cast<sal_Int64>(maWelford.getCount()));
            break;

        case SUBTOTAL_FUNC_STD:
        case SUBTOTAL_FUNC_VAR:
            bError = ( nCount < 2 );        // need at least 2 values
            assert(bError || nCount == static_cast<sal_Int64>(maWelford.getCount()));
            break;

        default:
            OSL_FAIL("invalid function");
    }

    //  calculate the selected function

    double fResult = 0.0;
    if ( !bError )
    {
        switch (eFunc)
        {
            case SUBTOTAL_FUNC_MAX:
            case SUBTOTAL_FUNC_MIN:
            case SUBTOTAL_FUNC_SUM:
            case SUBTOTAL_FUNC_PROD:
                //  different error conditions are handled above
                fResult = fVal;
                break;

            case SUBTOTAL_FUNC_CNT:
            case SUBTOTAL_FUNC_CNT2:
                fResult = nCount;
                break;

            case SUBTOTAL_FUNC_AVE:
                if ( nCount > 0 )
                    fResult = fVal / static_cast<double>(nCount);
                break;

            case SUBTOTAL_FUNC_STD:
                if ( nCount >= 2 )
                {
                    fResult = maWelford.getVarianceSample();
                    if (fResult < 0.0)
                        bError = true;
                    else
                        fResult = sqrt( fResult);
                }
                break;
            case SUBTOTAL_FUNC_VAR:
                if ( nCount >= 2 )
                    fResult = maWelford.getVarianceSample();
                break;
            case SUBTOTAL_FUNC_STDP:
                if ( nCount > 0 )
                {
                    fResult = maWelford.getVariancePopulation();
                    if (fResult < 0.0)
                        bError = true;
                    else
                        fResult = sqrt( fResult);
                }
                break;
            case SUBTOTAL_FUNC_VARP:
                if ( nCount > 0 )
                    fResult = maWelford.getVariancePopulation();
                break;
            case SUBTOTAL_FUNC_MED:
                {
                    size_t nSize = mSortedValues.size();
                    if (nSize > 0)
                    {
                        assert(nSize == static_cast<size_t>(nCount));
                        if ((nSize % 2) == 1)
                            fResult = mSortedValues[nSize / 2];
                        else
                            fResult = (mSortedValues[nSize / 2 - 1] + mSortedValues[nSize / 2]) / 2.0;
                    }
                }
                break;
            default:
                OSL_FAIL("invalid function");
        }
    }

    bool bEmpty = ( nCount == 0 );          // no data

    //  store the result
    //  Empty is checked first, so empty results are shown empty even for "average" etc.
    //  If these results should be treated as errors in reference value calculations,
    //  a separate state value (EMPTY_ERROR) is needed.
    //  Now, for compatibility, empty "average" results are counted as 0.

    if ( bEmpty )
        nCount = SC_DPAGG_RESULT_EMPTY;
    else if ( bError )
        nCount = SC_DPAGG_RESULT_ERROR;
    else
        nCount = SC_DPAGG_RESULT_VALID;

    if ( bEmpty || bError )
        fResult = 0.0;      // default, in case the state is later modified

    fVal = fResult;         // used directly from now on
    fAux = 0.0;             // used for running total or original result of reference value
}

bool ScDPAggData::IsCalculated() const
{
    return ( nCount <= SC_DPAGG_RESULT_EMPTY );
}

double ScDPAggData::GetResult() const
{
    assert( IsCalculated() && "ScDPAggData not calculated" );

    return fVal;        // use calculated value
}

bool ScDPAggData::HasError() const
{
    assert( IsCalculated() && "ScDPAggData not calculated" );

    return ( nCount == SC_DPAGG_RESULT_ERROR );
}

bool ScDPAggData::HasData() const
{
    assert( IsCalculated() && "ScDPAggData not calculated" );

    return ( nCount != SC_DPAGG_RESULT_EMPTY );     // values or error
}

void ScDPAggData::SetResult( double fNew )
{
    assert( IsCalculated() && "ScDPAggData not calculated" );

    fVal = fNew;        // don't reset error flag
}

void ScDPAggData::SetError()
{
    assert( IsCalculated() && "ScDPAggData not calculated" );

    nCount = SC_DPAGG_RESULT_ERROR;
}

void ScDPAggData::SetEmpty( bool bSet )
{
    assert( IsCalculated() && "ScDPAggData not calculated" );

    if ( bSet )
        nCount = SC_DPAGG_RESULT_EMPTY;
    else
        nCount = SC_DPAGG_RESULT_VALID;
}

double ScDPAggData::GetAuxiliary() const
{
    // after Calculate, fAux is used as auxiliary value for running totals and reference values
    assert( IsCalculated() && "ScDPAggData not calculated" );

    return fAux;
}

void ScDPAggData::SetAuxiliary( double fNew )
{
    // after Calculate, fAux is used as auxiliary value for running totals and reference values
    assert( IsCalculated() && "ScDPAggData not calculated" );

    fAux = fNew;
}

ScDPAggData* ScDPAggData::GetChild()
{
    if (!pChild)
        pChild.reset( new ScDPAggData );
    return pChild.get();
}

void ScDPAggData::Reset()
{
    maWelford = WelfordRunner();
    fVal = 0.0;
    fAux = 0.0;
    nCount = SC_DPAGG_EMPTY;
    pChild.reset();
}

#if DUMP_PIVOT_TABLE
void ScDPAggData::Dump(int nIndent) const
{
    std::string aIndent(nIndent*2, ' ');
    std::cout << aIndent << "* ";
    if (IsCalculated())
        std::cout << GetResult();
    else
        std::cout << "not calculated";

    std::cout << "  [val=" << fVal << "; aux=" << fAux << "; count=" << nCount << "]" << std::endl;
}
#endif

ScDPRowTotals::ScDPRowTotals() :
    bIsInColRoot( false )
{
}

ScDPRowTotals::~ScDPRowTotals()
{
}

static ScDPAggData* lcl_GetChildTotal( ScDPAggData* pFirst, tools::Long nMeasure )
{
    OSL_ENSURE( nMeasure >= 0, "GetColTotal: no measure" );

    ScDPAggData* pAgg = pFirst;
    tools::Long nSkip = nMeasure;

    // subtotal settings are ignored - column/row totals exist once per measure

    for ( tools::Long nPos=0; nPos<nSkip; nPos++ )
        pAgg = pAgg->GetChild();    // column total is constructed empty - children need to be created

    if ( !pAgg->IsCalculated() )
    {
        // for first use, simulate an empty calculation
        ScDPSubTotalState aEmptyState;
        pAgg->Calculate( SUBTOTAL_FUNC_SUM, aEmptyState );
    }

    return pAgg;
}

ScDPAggData* ScDPRowTotals::GetRowTotal( tools::Long nMeasure )
{
    return lcl_GetChildTotal( &aRowTotal, nMeasure );
}

ScDPAggData* ScDPRowTotals::GetGrandTotal( tools::Long nMeasure )
{
    return lcl_GetChildTotal( &aGrandTotal, nMeasure );
}

static ScSubTotalFunc lcl_GetForceFunc( const ScDPLevel* pLevel, tools::Long nFuncNo )
{
    ScSubTotalFunc eRet = SUBTOTAL_FUNC_NONE;
    if ( pLevel )
    {
        //TODO: direct access via ScDPLevel

        uno::Sequence<sal_Int16> aSeq = pLevel->getSubTotals();
        tools::Long nSequence = aSeq.getLength();
        if ( nSequence && aSeq[0] != sheet::GeneralFunction2::AUTO )
        {
            // For manual subtotals, "automatic" is added as first function.
            // ScDPResultMember::GetSubTotalCount adds to the count, here NONE has to be
            // returned as the first function then.

            --nFuncNo;      // keep NONE for first (check below), move the other entries
        }

        if ( nFuncNo >= 0 && nFuncNo < nSequence )
        {
            ScGeneralFunction eUser = static_cast<ScGeneralFunction>(aSeq.getConstArray()[nFuncNo]);
            if (eUser != ScGeneralFunction::AUTO)
                eRet = ScDPUtil::toSubTotalFunc(eUser);
        }
    }
    return eRet;
}

ScDPResultData::ScDPResultData( ScDPSource& rSrc ) :
    mrSource(rSrc),
    bLateInit( false ),
    bDataAtCol( false ),
    bDataAtRow( false )
{
}

ScDPResultData::~ScDPResultData()
{
}

void ScDPResultData::SetMeasureData(
    std::vector<ScSubTotalFunc>& rFunctions, std::vector<sheet::DataPilotFieldReference>& rRefs,
    std::vector<sheet::DataPilotFieldOrientation>& rRefOrient, std::vector<OUString>& rNames )
{
    // We need to have at least one measure data at all times.

    maMeasureFuncs.swap(rFunctions);
    if (maMeasureFuncs.empty())
        maMeasureFuncs.push_back(SUBTOTAL_FUNC_NONE);

    maMeasureRefs.swap(rRefs);
    if (maMeasureRefs.empty())
        maMeasureRefs.emplace_back(); // default ctor is ok.

    maMeasureRefOrients.swap(rRefOrient);
    if (maMeasureRefOrients.empty())
        maMeasureRefOrients.push_back(sheet::DataPilotFieldOrientation_HIDDEN);

    maMeasureNames.swap(rNames);
    if (maMeasureNames.empty())
        maMeasureNames.push_back(ScResId(STR_EMPTYDATA));
}

void ScDPResultData::SetDataLayoutOrientation( sheet::DataPilotFieldOrientation nOrient )
{
    bDataAtCol = ( nOrient == sheet::DataPilotFieldOrientation_COLUMN );
    bDataAtRow = ( nOrient == sheet::DataPilotFieldOrientation_ROW );
}

void ScDPResultData::SetLateInit( bool bSet )
{
    bLateInit = bSet;
}

tools::Long ScDPResultData::GetColStartMeasure() const
{
    if (maMeasureFuncs.size() == 1)
        return 0;

    return bDataAtCol ? SC_DPMEASURE_ALL : SC_DPMEASURE_ANY;
}

tools::Long ScDPResultData::GetRowStartMeasure() const
{
    if (maMeasureFuncs.size() == 1)
        return 0;

    return bDataAtRow ? SC_DPMEASURE_ALL : SC_DPMEASURE_ANY;
}

ScSubTotalFunc ScDPResultData::GetMeasureFunction(tools::Long nMeasure) const
{
    OSL_ENSURE(o3tl::make_unsigned(nMeasure) < maMeasureFuncs.size(), "bumm");
    return maMeasureFuncs[nMeasure];
}

const sheet::DataPilotFieldReference& ScDPResultData::GetMeasureRefVal(tools::Long nMeasure) const
{
    OSL_ENSURE(o3tl::make_unsigned(nMeasure) < maMeasureRefs.size(), "bumm");
    return maMeasureRefs[nMeasure];
}

sheet::DataPilotFieldOrientation ScDPResultData::GetMeasureRefOrient(tools::Long nMeasure) const
{
    OSL_ENSURE(o3tl::make_unsigned(nMeasure) < maMeasureRefOrients.size(), "bumm");
    return maMeasureRefOrients[nMeasure];
}

OUString ScDPResultData::GetMeasureString(tools::Long nMeasure, bool bForce, ScSubTotalFunc eForceFunc, bool& rbTotalResult) const
{
    //  with bForce==true, return function instead of "result" for single measure
    //  with eForceFunc != SUBTOTAL_FUNC_NONE, always use eForceFunc
    rbTotalResult = false;
    if ( nMeasure < 0 || (maMeasureFuncs.size() == 1 && !bForce && eForceFunc == SUBTOTAL_FUNC_NONE) )
    {
        //  for user-specified subtotal function with all measures,
        //  display only function name
        assert(unsigned(eForceFunc) < SAL_N_ELEMENTS(aFuncStrIds));
        if ( eForceFunc != SUBTOTAL_FUNC_NONE )
            return ScResId(aFuncStrIds[eForceFunc]);

        rbTotalResult = true;
        return ScResId(STR_TABLE_ERGEBNIS);
    }
    else
    {
        OSL_ENSURE(o3tl::make_unsigned(nMeasure) < maMeasureFuncs.size(), "bumm");
        const ScDPDimension* pDataDim = mrSource.GetDataDimension(nMeasure);
        if (pDataDim)
        {
            const std::optional<OUString> & pLayoutName = pDataDim->GetLayoutName();
            if (pLayoutName)
                return *pLayoutName;
        }

        ScSubTotalFunc eFunc = ( eForceFunc == SUBTOTAL_FUNC_NONE ) ?
                                    GetMeasureFunction(nMeasure) : eForceFunc;

        return ScDPUtil::getDisplayedMeasureName(maMeasureNames[nMeasure], eFunc);
    }
}

OUString ScDPResultData::GetMeasureDimensionName(tools::Long nMeasure) const
{
    if ( nMeasure < 0 )
    {
        OSL_FAIL("GetMeasureDimensionName: negative");
        return u"***"_ustr;
    }

    return mrSource.GetDataDimName(nMeasure);
}

bool ScDPResultData::IsBaseForGroup( tools::Long nDim ) const
{
    return mrSource.GetData()->IsBaseForGroup(nDim);
}

tools::Long ScDPResultData::GetGroupBase( tools::Long nGroupDim ) const
{
    return mrSource.GetData()->GetGroupBase(nGroupDim);
}

bool ScDPResultData::IsNumOrDateGroup( tools::Long nDim ) const
{
    return mrSource.GetData()->IsNumOrDateGroup(nDim);
}

bool ScDPResultData::IsInGroup( SCROW nGroupDataId, tools::Long nGroupIndex,
                                const ScDPItemData& rBaseData, tools::Long nBaseIndex ) const
{
    const ScDPItemData* pGroupData = mrSource.GetItemDataById(nGroupIndex , nGroupDataId);
    if ( pGroupData )
        return mrSource.GetData()->IsInGroup(*pGroupData, nGroupIndex, rBaseData, nBaseIndex);
    else
        return false;
}

bool ScDPResultData::HasCommonElement( SCROW nFirstDataId, tools::Long nFirstIndex,
                                       const ScDPItemData& rSecondData, tools::Long nSecondIndex ) const
{
    const ScDPItemData* pFirstData = mrSource.GetItemDataById(nFirstIndex , nFirstDataId);
    if ( pFirstData )
        return mrSource.GetData()->HasCommonElement(*pFirstData, nFirstIndex, rSecondData, nSecondIndex);
    else
        return false;
}

ResultMembers& ScDPResultData::GetDimResultMembers(tools::Long nDim, const ScDPDimension* pDim, ScDPLevel* pLevel) const
{
    if (nDim < static_cast<tools::Long>(maDimMembers.size()) && maDimMembers[nDim])
        return *maDimMembers[nDim];

    if (nDim >= static_cast<tools::Long>(maDimMembers.size()))
        maDimMembers.resize(nDim+1);

    std::unique_ptr<ResultMembers> pResultMembers(new ResultMembers());
    // global order is used to initialize aMembers, so it doesn't have to be looked at later
    const ScMemberSortOrder& rGlobalOrder = pLevel->GetGlobalOrder();

    ScDPMembers* pMembers = pLevel->GetMembersObject();
    tools::Long nMembCount = pMembers->getCount();
    for (tools::Long i = 0; i < nMembCount; ++i)
    {
        tools::Long nSorted = rGlobalOrder.empty() ? i : rGlobalOrder[i];
        ScDPMember* pMember = pMembers->getByIndex(nSorted);
        if (!pResultMembers->FindMember(pMember->GetItemDataId()))
        {
            ScDPParentDimData aNew(i, pDim, pLevel, pMember);
            pResultMembers->InsertMember(aNew);
        }
    }

    maDimMembers[nDim] = std::move(pResultMembers);
    return *maDimMembers[nDim];
}

ScDPResultMember::ScDPResultMember(
    const ScDPResultData* pData, const ScDPParentDimData& rParentDimData ) :
    pResultData( pData ),
       aParentDimData( rParentDimData ),
    bHasElements( false ),
    bForceSubTotal( false ),
    bHasHiddenDetails( false ),
    bInitialized( false ),
    bAutoHidden( false ),
    nMemberStep( 1 )
{
    // pParentLevel/pMemberDesc is 0 for root members
}

ScDPResultMember::ScDPResultMember(
    const ScDPResultData* pData, bool bForceSub ) :
    pResultData( pData ),
    bHasElements( false ),
    bForceSubTotal( bForceSub ),
    bHasHiddenDetails( false ),
    bInitialized( false ),
    bAutoHidden( false ),
    nMemberStep( 1 )
{
}
ScDPResultMember::~ScDPResultMember()
{
}

OUString ScDPResultMember::GetName() const
{
    const ScDPMember* pMemberDesc = GetDPMember();
    if (pMemberDesc)
        return pMemberDesc->GetNameStr( false );
    else
        return ScResId(STR_PIVOT_TOTAL);         // root member
}

OUString ScDPResultMember::GetDisplayName( bool bLocaleIndependent ) const
{
    const ScDPMember* pDPMember = GetDPMember();
    if (!pDPMember)
        return OUString();

    ScDPItemData aItem(pDPMember->FillItemData());
    if (aParentDimData.mpParentDim)
    {
        tools::Long nDim = aParentDimData.mpParentDim->GetDimension();
        return pResultData->GetSource().GetData()->GetFormattedString(nDim, aItem, bLocaleIndependent);
    }

    return aItem.GetString();
}

ScDPItemData ScDPResultMember::FillItemData() const
{
    const ScDPMember* pMemberDesc = GetDPMember();
    if (pMemberDesc)
        return pMemberDesc->FillItemData();
    return ScDPItemData(ScResId(STR_PIVOT_TOTAL));     // root member
}

bool ScDPResultMember::IsNamedItem( SCROW nIndex ) const
{
    //TODO: store ScDPMember pointer instead of ScDPMember ???
    const ScDPMember* pMemberDesc = GetDPMember();
    if (pMemberDesc)
        return pMemberDesc->IsNamedItem(nIndex);
    return false;
}

bool ScDPResultMember::IsValidEntry( const vector< SCROW >& aMembers ) const
{
    if ( !IsValid() )
        return false;

    const ScDPResultDimension* pChildDim = GetChildDimension();
    if (pChildDim)
    {
        if (aMembers.size() < 2)
            return false;

        vector<SCROW>::const_iterator itr = aMembers.begin();
        vector<SCROW> aChildMembers(++itr, aMembers.end());
        return pChildDim->IsValidEntry(aChildMembers);
    }
    else
        return true;
}

void ScDPResultMember::InitFrom( const vector<ScDPDimension*>& ppDim, const vector<ScDPLevel*>& ppLev,
                                 size_t nPos, ScDPInitState& rInitState ,
                                 bool bInitChild )
{
    //  with LateInit, initialize only those members that have data
    if ( pResultData->IsLateInit() )
        return;

    bInitialized = true;

    if (nPos >= ppDim.size())
        return;

    //  skip child dimension if details are not shown
    if ( GetDPMember() && !GetDPMember()->getShowDetails() )
    {
        // Show DataLayout dimension
        nMemberStep = 1;
        while ( nPos < ppDim.size() )
        {
            if (  ppDim[nPos]->getIsDataLayoutDimension() )
            {
                if ( !pChildDimension )
                    pChildDimension.reset( new ScDPResultDimension( pResultData ) );
                pChildDimension->InitFrom( ppDim, ppLev, nPos, rInitState , false );
                return;
            }
            else
            { //find next dim
                nPos ++;
                nMemberStep ++;
            }
        }
        bHasHiddenDetails = true;   // only if there is a next dimension
        return;
    }

    if ( bInitChild )
    {
        pChildDimension.reset( new ScDPResultDimension( pResultData ) );
        pChildDimension->InitFrom(ppDim, ppLev, nPos, rInitState);
    }
}

void ScDPResultMember::LateInitFrom(
    LateInitParams& rParams, const vector<SCROW>& pItemData, size_t nPos, ScDPInitState& rInitState)
{
    //  without LateInit, everything has already been initialized
    if ( !pResultData->IsLateInit() )
        return;

    bInitialized = true;

    if ( rParams.IsEnd( nPos )  /*nPos >= ppDim.size()*/)
        // No next dimension.  Bail out.
        return;

    //  skip child dimension if details are not shown
    if ( GetDPMember() && !GetDPMember()->getShowDetails() )
    {
        // Show DataLayout dimension
        nMemberStep = 1;
        while ( !rParams.IsEnd( nPos ) )
        {
            if (  rParams.GetDim( nPos )->getIsDataLayoutDimension() )
            {
                if ( !pChildDimension )
                    pChildDimension.reset( new ScDPResultDimension( pResultData ) );

                // #i111462# reset InitChild flag only for this child dimension's LateInitFrom call,
                // not for following members of parent dimensions
                bool bWasInitChild = rParams.GetInitChild();
                rParams.SetInitChild( false );
                pChildDimension->LateInitFrom( rParams, pItemData, nPos, rInitState );
                rParams.SetInitChild( bWasInitChild );
                return;
            }
            else
            { //find next dim
                nPos ++;
                nMemberStep ++;
            }
        }
        bHasHiddenDetails = true;   // only if there is a next dimension
        return;
    }

    //  LateInitFrom is called several times...
    if ( rParams.GetInitChild() )
    {
        if ( !pChildDimension )
            pChildDimension.reset( new ScDPResultDimension( pResultData ) );
        pChildDimension->LateInitFrom( rParams, pItemData, nPos, rInitState );
    }
}

bool ScDPResultMember::IsSubTotalInTitle(tools::Long nMeasure) const
{
    bool bRet = false;
    if ( pChildDimension && /*pParentLevel*/GetParentLevel() &&
         /*pParentLevel*/GetParentLevel()->IsOutlineLayout() && /*pParentLevel*/GetParentLevel()->IsSubtotalsAtTop() )
    {
        tools::Long nUserSubStart;
        tools::Long nSubTotals = GetSubTotalCount( &nUserSubStart );
        nSubTotals -= nUserSubStart;            // visible count
        if ( nSubTotals )
        {
            if ( nMeasure == SC_DPMEASURE_ALL )
                nSubTotals *= pResultData->GetMeasureCount();   // number of subtotals that will be inserted

            // only a single subtotal row will be shown in the outline title row
            if ( nSubTotals == 1 )
                bRet = true;
        }
    }
    return bRet;
}

tools::Long ScDPResultMember::GetSize(tools::Long nMeasure) const
{
    if ( !IsVisible() )
        return 0;
    const ScDPLevel*       pParentLevel = GetParentLevel();
    tools::Long nExtraSpace = 0;
    if ( pParentLevel && pParentLevel->IsAddEmpty() )
        ++nExtraSpace;

    if ( pChildDimension )
    {
        //  outline layout takes up an extra row for the title only if subtotals aren't shown in that row
        if ( pParentLevel && pParentLevel->IsOutlineLayout() && !IsSubTotalInTitle( nMeasure ) )
            ++nExtraSpace;

        tools::Long nSize = pChildDimension->GetSize(nMeasure);
        tools::Long nUserSubStart;
        tools::Long nUserSubCount = GetSubTotalCount( &nUserSubStart );
        nUserSubCount -= nUserSubStart;     // for output size, use visible count
        if ( nUserSubCount )
        {
            if ( nMeasure == SC_DPMEASURE_ALL )
                nSize += pResultData->GetMeasureCount() * nUserSubCount;
            else
                nSize += nUserSubCount;
        }
        return nSize + nExtraSpace;
    }
    else
    {
        if ( nMeasure == SC_DPMEASURE_ALL )
            return pResultData->GetMeasureCount() + nExtraSpace;
        else
            return 1 + nExtraSpace;
    }
}

bool ScDPResultMember::IsVisible() const
{
    if (!bInitialized)
        return false;

    if (!IsValid())
        return false;

    if (bHasElements)
        return true;

    //  not initialized -> shouldn't be there at all
    //  (allocated only to preserve ordering)
    const ScDPLevel* pParentLevel = GetParentLevel();

    return (pParentLevel && pParentLevel->getShowEmpty());
}

bool ScDPResultMember::IsValid() const
{
    //  non-Valid members are left out of calculation

    //  was member set no invisible at the DataPilotSource?
    const ScDPMember* pMemberDesc = GetDPMember();
    if ( pMemberDesc && !pMemberDesc->isVisible() )
        return false;

    if ( bAutoHidden )
        return false;

    return true;
}

tools::Long ScDPResultMember::GetSubTotalCount( tools::Long* pUserSubStart ) const
{
    if ( pUserSubStart )
        *pUserSubStart = 0;     // default

    const ScDPLevel* pParentLevel = GetParentLevel();

    if ( bForceSubTotal )       // set if needed for root members
        return 1;               // grand total is always "automatic"
    else if ( pParentLevel )
    {
        //TODO: direct access via ScDPLevel

        uno::Sequence<sal_Int16> aSeq = pParentLevel->getSubTotals();
        tools::Long nSequence = aSeq.getLength();
        if ( nSequence && aSeq[0] != sheet::GeneralFunction2::AUTO )
        {
            // For manual subtotals, always add "automatic" as first function
            // (used for calculation, but not for display, needed for sorting, see lcl_GetForceFunc)

            ++nSequence;
            if ( pUserSubStart )
                *pUserSubStart = 1;     // visible subtotals start at 1
        }
        return nSequence;
    }
    else
        return 0;
}

void ScDPResultMember::ProcessData( const vector< SCROW >& aChildMembers, const ScDPResultDimension* pDataDim,
                                    const vector< SCROW >& aDataMembers, const vector<ScDPValue>& aValues )
{
    SetHasElements();

    if (pChildDimension)
        pChildDimension->ProcessData( aChildMembers, pDataDim, aDataMembers, aValues );

    if ( !pDataRoot )
    {
        pDataRoot.reset( new ScDPDataMember( pResultData, nullptr ) );
        if ( pDataDim )
            pDataRoot->InitFrom( pDataDim );            // recursive
    }

    ScDPSubTotalState aSubState;        // initial state

    tools::Long nUserSubCount = GetSubTotalCount();

    // Calculate at least automatic if no subtotals are selected,
    // show only own values if there's no child dimension (innermost).
    if ( !nUserSubCount || !pChildDimension )
        nUserSubCount = 1;

    const ScDPLevel*    pParentLevel = GetParentLevel();

    for (tools::Long nUserPos=0; nUserPos<nUserSubCount; nUserPos++)   // including hidden "automatic"
    {
        // #i68338# if nUserSubCount is 1 (automatic only), don't set nRowSubTotalFunc
        if ( pChildDimension && nUserSubCount > 1 )
        {
            aSubState.nRowSubTotalFunc = nUserPos;
            aSubState.eRowForce = lcl_GetForceFunc( pParentLevel, nUserPos );
        }

        pDataRoot->ProcessData( aDataMembers, aValues, aSubState );
    }
}

/**
 * Parse subtotal string and replace all occurrences of '?' with the caption
 * string.  Do ensure that escaped characters are not translated.
 */
static OUString lcl_parseSubtotalName(std::u16string_view rSubStr, std::u16string_view rCaption)
{
    OUStringBuffer aNewStr;
    sal_Int32 n = rSubStr.size();
    bool bEscaped = false;
    for (sal_Int32 i = 0; i < n; ++i)
    {
        sal_Unicode c = rSubStr[i];
        if (!bEscaped && c == '\\')
        {
            bEscaped = true;
            continue;
        }

        if (!bEscaped && c == '?')
            aNewStr.append(rCaption);
        else
            aNewStr.append(c);
        bEscaped = false;
    }
    return aNewStr.makeStringAndClear();
}

void ScDPResultMember::FillMemberResults(
    uno::Sequence<sheet::MemberResult>* pSequences, tools::Long& rPos, tools::Long nMeasure, bool bRoot,
    const OUString* pMemberName, const OUString* pMemberCaption )
{
    //  IsVisible() test is in ScDPResultDimension::FillMemberResults
    //  (not on data layout dimension)

    if (!pSequences->hasElements())
        // empty sequence.  Bail out.
        return;

    tools::Long nSize = GetSize(nMeasure);
    sheet::MemberResult* pArray = pSequences->getArray();
    OSL_ENSURE( rPos+nSize <= pSequences->getLength(), "bumm" );

    bool bIsNumeric = false;
    double fValue = std::numeric_limits<double>::quiet_NaN();
    OUString aName;
    if ( pMemberName )          // if pMemberName != NULL, use instead of real member name
    {
        aName = *pMemberName;
    }
    else
    {
        ScDPItemData aItemData(FillItemData());
        if (aParentDimData.mpParentDim)
        {
            tools::Long nDim = aParentDimData.mpParentDim->GetDimension();
            aName = pResultData->GetSource().GetData()->GetFormattedString(nDim, aItemData, false);
        }
        else
        {
            tools::Long nDim = -1;
            const ScDPMember* pMem = GetDPMember();
            if (pMem)
                nDim = pMem->GetDim();
            aName = pResultData->GetSource().GetData()->GetFormattedString(nDim, aItemData, false);
        }

        ScDPItemData::Type eType = aItemData.GetType();
        bIsNumeric = eType == ScDPItemData::Value || eType == ScDPItemData::GroupValue;
        // IsValue() is not identical to bIsNumeric, i.e.
        // ScDPItemData::GroupValue is excluded and not stored in the double,
        // so even if the item is numeric the Value may be NaN.
        if (aItemData.IsValue())
            fValue = aItemData.GetValue();
    }

    const ScDPDimension*        pParentDim = GetParentDim();
    if ( bIsNumeric && pParentDim && pResultData->IsNumOrDateGroup( pParentDim->GetDimension() ) )
    {
        // Numeric group dimensions use numeric entries for proper sorting,
        // but the group titles must be output as text.
        bIsNumeric = false;
    }

    OUString aCaption = aName;
    const ScDPMember* pMemberDesc = GetDPMember();
    if (pMemberDesc)
    {
        const std::optional<OUString> & pLayoutName = pMemberDesc->GetLayoutName();
        if (pLayoutName)
        {
            aCaption = *pLayoutName;
            bIsNumeric = false; // layout name is always non-numeric.
        }
    }

    if ( pMemberCaption )                   // use pMemberCaption if != NULL
        aCaption = *pMemberCaption;
    if (aCaption.isEmpty())
        aCaption = ScResId(STR_EMPTYDATA);

    if (bIsNumeric)
        pArray[rPos].Flags |= sheet::MemberResultFlags::NUMERIC;
    else
        pArray[rPos].Flags &= ~sheet::MemberResultFlags::NUMERIC;

    const ScDPLevel*    pParentLevel = GetParentLevel();
    if ( nSize && !bRoot )                  // root is overwritten by first dimension
    {
        pArray[rPos].Name    = aName;
        pArray[rPos].Caption = aCaption;
        pArray[rPos].Flags  |= sheet::MemberResultFlags::HASMEMBER;
        pArray[rPos].Value   = fValue;

        //  set "continue" flag (removed for subtotals later)
        for (tools::Long i=1; i<nSize; i++)
        {
            pArray[rPos+i].Flags |= sheet::MemberResultFlags::CONTINUE;
            // tdf#113002 - add numeric flag to recurring data fields
            if (bIsNumeric)
                pArray[rPos + i].Flags |= sheet::MemberResultFlags::NUMERIC;
        }

        if ( pParentLevel && pParentLevel->getRepeatItemLabels() )
        {
            tools::Long nSizeNonEmpty = nSize;
            if ( pParentLevel->IsAddEmpty() )
                --nSizeNonEmpty;
            for (tools::Long i=1; i<nSizeNonEmpty; i++)
            {
                pArray[rPos+i].Name = aName;
                pArray[rPos+i].Caption = aCaption;
                pArray[rPos+i].Flags  |= sheet::MemberResultFlags::HASMEMBER;
                pArray[rPos+i].Value   = fValue;
            }
        }
    }

    tools::Long nExtraSpace = 0;
    if ( pParentLevel && pParentLevel->IsAddEmpty() )
        ++nExtraSpace;

    bool bTitleLine = false;
    if ( pParentLevel && pParentLevel->IsOutlineLayout() )
        bTitleLine = true;

    // if the subtotals are shown at the top (title row) in outline layout,
    // no extra row for the subtotals is needed
    bool bSubTotalInTitle = IsSubTotalInTitle( nMeasure );

    bool bHasChild = ( pChildDimension != nullptr );
    if (bHasChild)
    {
        if ( bTitleLine )           // in tabular layout the title is on a separate row
            ++rPos;                 // -> fill child dimension one row below

        if (bRoot)      // same sequence for root member
            pChildDimension->FillMemberResults( pSequences, rPos, nMeasure );
        else
            pChildDimension->FillMemberResults( pSequences + nMemberStep/*1*/, rPos, nMeasure );

        if ( bTitleLine )           // title row is included in GetSize, so the following
            --rPos;                 // positions are calculated with the normal values
    }

    rPos += nSize;

    tools::Long nUserSubStart;
    tools::Long nUserSubCount = GetSubTotalCount(&nUserSubStart);
    if ( !nUserSubCount || !pChildDimension || bSubTotalInTitle )
        return;

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nSubSize = pResultData->GetCountForMeasure(nMeasure);

    rPos -= nSubSize * (nUserSubCount - nUserSubStart);     // GetSize includes space for SubTotal
    rPos -= nExtraSpace;                                    // GetSize includes the empty line

    for (tools::Long nUserPos=nUserSubStart; nUserPos<nUserSubCount; nUserPos++)
    {
        for ( tools::Long nSubCount=0; nSubCount<nSubSize; nSubCount++ )
        {
            if ( nMeasure == SC_DPMEASURE_ALL )
                nMemberMeasure = nSubCount;

            ScSubTotalFunc eForce = SUBTOTAL_FUNC_NONE;
            if (bHasChild)
                eForce = lcl_GetForceFunc( pParentLevel, nUserPos );

            bool bTotalResult = false;
            OUString aSubStr = aCaption + " " + pResultData->GetMeasureString(nMemberMeasure, false, eForce, bTotalResult);

            if (bTotalResult)
            {
                if (pMemberDesc)
                {
                    // single data field layout.
                    const std::optional<OUString> & pSubtotalName = pParentDim->GetSubtotalName();
                    if (pSubtotalName)
                        aSubStr = lcl_parseSubtotalName(*pSubtotalName, aCaption);
                    pArray[rPos].Flags &= ~sheet::MemberResultFlags::GRANDTOTAL;
                }
                else
                {
                    // root member - subtotal (grand total?) for multi-data field layout.
                    const std::optional<OUString> & pGrandTotalName = pResultData->GetSource().GetGrandTotalName();
                    if (pGrandTotalName)
                        aSubStr = *pGrandTotalName;
                    pArray[rPos].Flags |= sheet::MemberResultFlags::GRANDTOTAL;
                }
            }

            fValue = std::numeric_limits<double>::quiet_NaN(); /* TODO: any numeric value to obtain? */
            pArray[rPos].Name    = aName;
            pArray[rPos].Caption = aSubStr;
            pArray[rPos].Flags = ( pArray[rPos].Flags |
                                ( sheet::MemberResultFlags::HASMEMBER | sheet::MemberResultFlags::SUBTOTAL) ) &
                                ~sheet::MemberResultFlags::CONTINUE;
            pArray[rPos].Value   = fValue;

            if ( nMeasure == SC_DPMEASURE_ALL )
            {
                //  data layout dimension is (direct/indirect) child of this.
                //  data layout dimension must have name for all entries.

                uno::Sequence<sheet::MemberResult>* pLayoutSeq = pSequences;
                if (!bRoot)
                    ++pLayoutSeq;
                ScDPResultDimension* pLayoutDim = pChildDimension.get();
                while ( pLayoutDim && !pLayoutDim->IsDataLayout() )
                {
                    pLayoutDim = pLayoutDim->GetFirstChildDimension();
                    ++pLayoutSeq;
                }
                if ( pLayoutDim )
                {
                    sheet::MemberResult* pLayoutArray = pLayoutSeq->getArray();
                    pLayoutArray[rPos].Name = pResultData->GetMeasureDimensionName(nMemberMeasure);
                }
            }

            rPos += 1;
        }
    }

    rPos += nExtraSpace;                                    // add again (subtracted above)
}

void ScDPResultMember::FillDataResults(
    const ScDPResultMember* pRefMember,
    ScDPResultFilterContext& rFilterCxt, uno::Sequence<uno::Sequence<sheet::DataResult> >& rSequence,
    tools::Long nMeasure) const
{
    std::unique_ptr<FilterStack> pFilterStack;
    const ScDPMember* pDPMember = GetDPMember();
    if (pDPMember)
    {
        // Root result has no corresponding DP member. Only take the non-root results.
        pFilterStack.reset(new FilterStack(rFilterCxt.maFilters));
        pFilterStack->pushDimValue( GetDisplayName( false), GetDisplayName( true));
    }

    //  IsVisible() test is in ScDPResultDimension::FillDataResults
    //  (not on data layout dimension)
    const ScDPLevel*     pParentLevel = GetParentLevel();
    sal_Int32 nStartRow = rFilterCxt.mnRow;

    tools::Long nExtraSpace = 0;
    if ( pParentLevel && pParentLevel->IsAddEmpty() )
        ++nExtraSpace;

    bool bTitleLine = false;
    if ( pParentLevel && pParentLevel->IsOutlineLayout() )
        bTitleLine = true;

    bool bSubTotalInTitle = IsSubTotalInTitle( nMeasure );

    bool bHasChild = ( pChildDimension != nullptr );
    if (bHasChild)
    {
        if ( bTitleLine )           // in tabular layout the title is on a separate row
            ++rFilterCxt.mnRow;                 // -> fill child dimension one row below

        sal_Int32 nOldRow = rFilterCxt.mnRow;
        pChildDimension->FillDataResults(pRefMember, rFilterCxt, rSequence, nMeasure);
        rFilterCxt.mnRow = nOldRow; // Revert to the original row before the call.

        rFilterCxt.mnRow += GetSize( nMeasure );

        if ( bTitleLine )           // title row is included in GetSize, so the following
            --rFilterCxt.mnRow;                 // positions are calculated with the normal values
    }

    tools::Long nUserSubStart;
    tools::Long nUserSubCount = GetSubTotalCount(&nUserSubStart);
    if ( !nUserSubCount && bHasChild )
        return;

    // Calculate at least automatic if no subtotals are selected,
    // show only own values if there's no child dimension (innermost).
    if ( !nUserSubCount || !bHasChild )
    {
        nUserSubCount = 1;
        nUserSubStart = 0;
    }

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nSubSize = pResultData->GetCountForMeasure(nMeasure);
    if (bHasChild)
    {
        rFilterCxt.mnRow -= nSubSize * ( nUserSubCount - nUserSubStart );   // GetSize includes space for SubTotal
        rFilterCxt.mnRow -= nExtraSpace;                                    // GetSize includes the empty line
    }

    tools::Long nMoveSubTotal = 0;
    if ( bSubTotalInTitle )
    {
        nMoveSubTotal = rFilterCxt.mnRow - nStartRow;   // force to first (title) row
        rFilterCxt.mnRow = nStartRow;
    }

    if ( pDataRoot )
    {
        ScDPSubTotalState aSubState;        // initial state

        for (tools::Long nUserPos=nUserSubStart; nUserPos<nUserSubCount; nUserPos++)
        {
            if ( bHasChild && nUserSubCount > 1 )
            {
                aSubState.nRowSubTotalFunc = nUserPos;
                aSubState.eRowForce = lcl_GetForceFunc( /*pParentLevel*/GetParentLevel() , nUserPos );
            }

            for ( tools::Long nSubCount=0; nSubCount<nSubSize; nSubCount++ )
            {
                if ( nMeasure == SC_DPMEASURE_ALL )
                    nMemberMeasure = nSubCount;
                else if ( pResultData->GetColStartMeasure() == SC_DPMEASURE_ALL )
                    nMemberMeasure = SC_DPMEASURE_ALL;

                OSL_ENSURE( rFilterCxt.mnRow < rSequence.getLength(), "bumm" );
                rFilterCxt.mnCol = 0;
                if (pRefMember->IsVisible())
                {
                    uno::Sequence<sheet::DataResult>& rSubSeq = rSequence.getArray()[rFilterCxt.mnRow];
                    pDataRoot->FillDataRow(pRefMember, rFilterCxt, rSubSeq, nMemberMeasure, bHasChild, aSubState);
                }
                rFilterCxt.mnRow += 1;
            }
        }
    }
    else
        rFilterCxt.mnRow += nSubSize * ( nUserSubCount - nUserSubStart );   // empty rows occur when ShowEmpty is true

    // add extra space again if subtracted from GetSize above,
    // add to own size if no children
    rFilterCxt.mnRow += nExtraSpace;
    rFilterCxt.mnRow += nMoveSubTotal;
}

void ScDPResultMember::UpdateDataResults( const ScDPResultMember* pRefMember, tools::Long nMeasure ) const
{
    //  IsVisible() test is in ScDPResultDimension::FillDataResults
    //  (not on data layout dimension)

    bool bHasChild = ( pChildDimension != nullptr );

    tools::Long nUserSubCount = GetSubTotalCount();

    // process subtotals even if not shown

    // Calculate at least automatic if no subtotals are selected,
    // show only own values if there's no child dimension (innermost).
    if (!nUserSubCount || !bHasChild)
        nUserSubCount = 1;

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nSubSize = pResultData->GetCountForMeasure(nMeasure);

    if (pDataRoot)
    {
        ScDPSubTotalState aSubState;        // initial state

        for (tools::Long nUserPos = 0; nUserPos < nUserSubCount; ++nUserPos)   // including hidden "automatic"
        {
            if (bHasChild && nUserSubCount > 1)
            {
                aSubState.nRowSubTotalFunc = nUserPos;
                aSubState.eRowForce = lcl_GetForceFunc(GetParentLevel(), nUserPos);
            }

            for (tools::Long nSubCount = 0; nSubCount < nSubSize; ++nSubCount)
            {
                if (nMeasure == SC_DPMEASURE_ALL)
                    nMemberMeasure = nSubCount;
                else if (pResultData->GetColStartMeasure() == SC_DPMEASURE_ALL)
                    nMemberMeasure = SC_DPMEASURE_ALL;

                pDataRoot->UpdateDataRow(pRefMember, nMemberMeasure, bHasChild, aSubState);
            }
        }
    }

    if (bHasChild)  // child dimension must be processed last, so the column total is known
    {
        pChildDimension->UpdateDataResults( pRefMember, nMeasure );
    }
}

void ScDPResultMember::SortMembers( ScDPResultMember* pRefMember )
{
    bool bHasChild = ( pChildDimension != nullptr );
    if (bHasChild)
        pChildDimension->SortMembers( pRefMember );     // sorting is done at the dimension

    if ( IsRoot() && pDataRoot )
    {
        // use the row root member to sort columns
        // sub total count is always 1

        pDataRoot->SortMembers( pRefMember );
    }
}

void ScDPResultMember::DoAutoShow( ScDPResultMember* pRefMember )
{
    bool bHasChild = ( pChildDimension != nullptr );
    if (bHasChild)
        pChildDimension->DoAutoShow( pRefMember );     // sorting is done at the dimension

    if ( IsRoot()&& pDataRoot )
    {
        // use the row root member to sort columns
        // sub total count is always 1

        pDataRoot->DoAutoShow( pRefMember );
    }
}

void ScDPResultMember::ResetResults()
{
    if (pDataRoot)
        pDataRoot->ResetResults();

    if (pChildDimension)
        pChildDimension->ResetResults();
}

void ScDPResultMember::UpdateRunningTotals( const ScDPResultMember* pRefMember, tools::Long nMeasure,
                                            ScDPRunningTotalState& rRunning, ScDPRowTotals& rTotals ) const
{
    //  IsVisible() test is in ScDPResultDimension::FillDataResults
    //  (not on data layout dimension)

    rTotals.SetInColRoot( IsRoot() );

    bool bHasChild = ( pChildDimension != nullptr );

    tools::Long nUserSubCount = GetSubTotalCount();
    //if ( nUserSubCount || !bHasChild )
    {
        // Calculate at least automatic if no subtotals are selected,
        // show only own values if there's no child dimension (innermost).
        if ( !nUserSubCount || !bHasChild )
            nUserSubCount = 1;

        tools::Long nMemberMeasure = nMeasure;
        tools::Long nSubSize = pResultData->GetCountForMeasure(nMeasure);

        if ( pDataRoot )
        {
            ScDPSubTotalState aSubState;        // initial state

            for (tools::Long nUserPos=0; nUserPos<nUserSubCount; nUserPos++)   // including hidden "automatic"
            {
                if ( bHasChild && nUserSubCount > 1 )
                {
                    aSubState.nRowSubTotalFunc = nUserPos;
                    aSubState.eRowForce = lcl_GetForceFunc(GetParentLevel(), nUserPos);
                }

                for ( tools::Long nSubCount=0; nSubCount<nSubSize; nSubCount++ )
                {
                    if ( nMeasure == SC_DPMEASURE_ALL )
                        nMemberMeasure = nSubCount;
                    else if ( pResultData->GetColStartMeasure() == SC_DPMEASURE_ALL )
                        nMemberMeasure = SC_DPMEASURE_ALL;

                    if (pRefMember->IsVisible())
                        pDataRoot->UpdateRunningTotals(
                            pRefMember, nMemberMeasure, bHasChild, aSubState, rRunning, rTotals, *this);
                }
            }
        }
    }

    if (bHasChild)  // child dimension must be processed last, so the column total is known
    {
        pChildDimension->UpdateRunningTotals( pRefMember, nMeasure, rRunning, rTotals );
    }
}

#if DUMP_PIVOT_TABLE
void ScDPResultMember::DumpState( const ScDPResultMember* pRefMember, ScDocument* pDoc, ScAddress& rPos ) const
{
    dumpRow(u"ScDPResultMember"_ustr, GetName(), nullptr, pDoc, rPos);
    SCROW nStartRow = rPos.Row();

    if (pDataRoot)
        pDataRoot->DumpState( pRefMember, pDoc, rPos );

    if (pChildDimension)
        pChildDimension->DumpState( pRefMember, pDoc, rPos );

    indent(pDoc, nStartRow, rPos);
}

void ScDPResultMember::Dump(int nIndent) const
{
    std::string aIndent(nIndent*2, ' ');
    std::cout << aIndent << "-- result member '" << GetName() << "'" << std::endl;

    std::cout << aIndent << " column totals" << std::endl;
    for (const ScDPAggData* p = &aColTotal; p; p = p->GetExistingChild())
        p->Dump(nIndent+1);

    if (pChildDimension)
        pChildDimension->Dump(nIndent+1);

    if (pDataRoot)
    {
        std::cout << aIndent << " data root" << std::endl;
        pDataRoot->Dump(nIndent+1);
    }
}
#endif

ScDPAggData* ScDPResultMember::GetColTotal( tools::Long nMeasure ) const
{
    return lcl_GetChildTotal( const_cast<ScDPAggData*>(&aColTotal), nMeasure );
}

void ScDPResultMember::FillVisibilityData(ScDPResultVisibilityData& rData) const
{
    if (pChildDimension)
        pChildDimension->FillVisibilityData(rData);
}

ScDPDataMember::ScDPDataMember( const ScDPResultData* pData, const ScDPResultMember* pRes ) :
    pResultData( pData ),
    pResultMember( pRes )
{
    // pResultMember is 0 for root members
}

ScDPDataMember::~ScDPDataMember()
{
}

OUString ScDPDataMember::GetName() const
{
    if (pResultMember)
        return pResultMember->GetName();
    else
        return OUString();
}

bool ScDPDataMember::IsVisible() const
{
    if (pResultMember)
        return pResultMember->IsVisible();
    else
        return false;
}

bool ScDPDataMember::IsNamedItem( SCROW nRow ) const
{
    if (pResultMember)
        return pResultMember->IsNamedItem(nRow);
    else
        return false;
}

bool ScDPDataMember::HasHiddenDetails() const
{
    if (pResultMember)
        return pResultMember->HasHiddenDetails();
    else
        return false;
}

void ScDPDataMember::InitFrom( const ScDPResultDimension* pDim )
{
    if ( !pChildDimension )
        pChildDimension.reset( new ScDPDataDimension(pResultData) );
    pChildDimension->InitFrom(pDim);
}

const tools::Long SC_SUBTOTALPOS_AUTO = -1;    // default
const tools::Long SC_SUBTOTALPOS_SKIP = -2;    // don't use

static tools::Long lcl_GetSubTotalPos( const ScDPSubTotalState& rSubState )
{
    if ( rSubState.nColSubTotalFunc >= 0 && rSubState.nRowSubTotalFunc >= 0 &&
         rSubState.nColSubTotalFunc != rSubState.nRowSubTotalFunc )
    {
        // #i68338# don't return the same index for different combinations (leading to repeated updates),
        // return a "don't use" value instead

        return SC_SUBTOTALPOS_SKIP;
    }

    tools::Long nRet = SC_SUBTOTALPOS_AUTO;
    if ( rSubState.nColSubTotalFunc >= 0 ) nRet = rSubState.nColSubTotalFunc;
    if ( rSubState.nRowSubTotalFunc >= 0 ) nRet = rSubState.nRowSubTotalFunc;
    return nRet;
}

void ScDPDataMember::UpdateValues( const vector<ScDPValue>& aValues, const ScDPSubTotalState& rSubState )
{
    //TODO: find out how many and which subtotals are used

    ScDPAggData* pAgg = &aAggregate;

    tools::Long nSubPos = lcl_GetSubTotalPos(rSubState);
    if (nSubPos == SC_SUBTOTALPOS_SKIP)
        return;
    if (nSubPos > 0)
    {
        tools::Long nSkip = nSubPos * pResultData->GetMeasureCount();
        for (tools::Long i=0; i<nSkip; i++)
            pAgg = pAgg->GetChild();        // created if not there
    }

    size_t nCount = aValues.size();
    for (size_t nPos = 0; nPos < nCount; ++nPos)
    {
        pAgg->Update(aValues[nPos], pResultData->GetMeasureFunction(nPos), rSubState);
        pAgg = pAgg->GetChild();
    }
}

void ScDPDataMember::ProcessData( const vector< SCROW >& aChildMembers, const vector<ScDPValue>& aValues,
                                    const ScDPSubTotalState& rSubState )
{
    if ( pResultData->IsLateInit() && !pChildDimension && pResultMember && pResultMember->GetChildDimension() )
    {
        //  if this DataMember doesn't have a child dimension because the ResultMember's
        //  child dimension wasn't there yet during this DataMembers's creation,
        //  create the child dimension now
        InitFrom( pResultMember->GetChildDimension() );
    }

    tools::Long nUserSubCount = pResultMember ? pResultMember->GetSubTotalCount() : 0;

    // Calculate at least automatic if no subtotals are selected,
    // show only own values if there's no child dimension (innermost).
    if ( !nUserSubCount || !pChildDimension )
        nUserSubCount = 1;

    ScDPSubTotalState aLocalSubState = rSubState;        // keep row state, modify column
    for (tools::Long nUserPos=0; nUserPos<nUserSubCount; nUserPos++)   // including hidden "automatic"
    {
        if ( pChildDimension && nUserSubCount > 1 )
        {
            const ScDPLevel* pForceLevel = pResultMember ? pResultMember->GetParentLevel() : nullptr;
            aLocalSubState.nColSubTotalFunc = nUserPos;
            aLocalSubState.eColForce = lcl_GetForceFunc( pForceLevel, nUserPos );
        }

        UpdateValues( aValues, aLocalSubState );
    }

    if (pChildDimension)
        pChildDimension->ProcessData( aChildMembers, aValues, rSubState );      // with unmodified subtotal state
}

bool ScDPDataMember::HasData( tools::Long nMeasure, const ScDPSubTotalState& rSubState ) const
{
    if ( rSubState.eColForce != SUBTOTAL_FUNC_NONE && rSubState.eRowForce != SUBTOTAL_FUNC_NONE &&
                                                        rSubState.eColForce != rSubState.eRowForce )
        return false;

    //  HasData can be different between measures!

    const ScDPAggData* pAgg = GetConstAggData( nMeasure, rSubState );
    if (!pAgg)
        return false;           //TODO: error?

    return pAgg->HasData();
}

bool ScDPDataMember::HasError( tools::Long nMeasure, const ScDPSubTotalState& rSubState ) const
{
    const ScDPAggData* pAgg = GetConstAggData( nMeasure, rSubState );
    if (!pAgg)
        return true;

    return pAgg->HasError();
}

double ScDPDataMember::GetAggregate( tools::Long nMeasure, const ScDPSubTotalState& rSubState ) const
{
    const ScDPAggData* pAgg = GetConstAggData( nMeasure, rSubState );
    if (!pAgg)
        return DBL_MAX;         //TODO: error?

    return pAgg->GetResult();
}

ScDPAggData* ScDPDataMember::GetAggData( tools::Long nMeasure, const ScDPSubTotalState& rSubState )
{
    OSL_ENSURE( nMeasure >= 0, "GetAggData: no measure" );

    ScDPAggData* pAgg = &aAggregate;
    tools::Long nSkip = nMeasure;
    tools::Long nSubPos = lcl_GetSubTotalPos(rSubState);
    if (nSubPos == SC_SUBTOTALPOS_SKIP)
        return nullptr;
    if (nSubPos > 0)
        nSkip += nSubPos * pResultData->GetMeasureCount();

    for ( tools::Long nPos=0; nPos<nSkip; nPos++ )
        pAgg = pAgg->GetChild();        //TODO: need to create children here?

    return pAgg;
}

const ScDPAggData* ScDPDataMember::GetConstAggData( tools::Long nMeasure, const ScDPSubTotalState& rSubState ) const
{
    OSL_ENSURE( nMeasure >= 0, "GetConstAggData: no measure" );

    const ScDPAggData* pAgg = &aAggregate;
    tools::Long nSkip = nMeasure;
    tools::Long nSubPos = lcl_GetSubTotalPos(rSubState);
    if (nSubPos == SC_SUBTOTALPOS_SKIP)
        return nullptr;
    if (nSubPos > 0)
        nSkip += nSubPos * pResultData->GetMeasureCount();

    for ( tools::Long nPos=0; nPos<nSkip; nPos++ )
    {
        pAgg = pAgg->GetExistingChild();
        if (!pAgg)
            return nullptr;
    }

    return pAgg;
}

void ScDPDataMember::FillDataRow(
    const ScDPResultMember* pRefMember, ScDPResultFilterContext& rFilterCxt,
    uno::Sequence<sheet::DataResult>& rSequence, tools::Long nMeasure, bool bIsSubTotalRow,
    const ScDPSubTotalState& rSubState) const
{
    std::unique_ptr<FilterStack> pFilterStack;
    if (pResultMember)
    {
        // Topmost data member (pResultMember=NULL) doesn't need to be handled
        // since its immediate parent result member is linked to the same
        // dimension member.
        pFilterStack.reset(new FilterStack(rFilterCxt.maFilters));
        pFilterStack->pushDimValue( pResultMember->GetDisplayName( false), pResultMember->GetDisplayName( true));
    }

    OSL_ENSURE( pRefMember == pResultMember || !pResultMember, "bla" );

    tools::Long nStartCol = rFilterCxt.mnCol;

    const ScDPDataDimension* pDataChild = GetChildDimension();
    const ScDPResultDimension* pRefChild = pRefMember->GetChildDimension();

    const ScDPLevel* pRefParentLevel = pRefMember->GetParentLevel();

    tools::Long nExtraSpace = 0;
    if ( pRefParentLevel && pRefParentLevel->IsAddEmpty() )
        ++nExtraSpace;

    bool bTitleLine = false;
    if ( pRefParentLevel && pRefParentLevel->IsOutlineLayout() )
        bTitleLine = true;

    bool bSubTotalInTitle = pRefMember->IsSubTotalInTitle( nMeasure );

    //  leave space for children even if the DataMember hasn't been initialized
    //  (pDataChild is null then, this happens when no values for it are in this row)
    bool bHasChild = ( pRefChild != nullptr );

    if ( bHasChild )
    {
        if ( bTitleLine )           // in tabular layout the title is on a separate column
            ++rFilterCxt.mnCol;                 // -> fill child dimension one column below

        if ( pDataChild )
        {
            tools::Long nOldCol = rFilterCxt.mnCol;
            pDataChild->FillDataRow(pRefChild, rFilterCxt, rSequence, nMeasure, bIsSubTotalRow, rSubState);
            rFilterCxt.mnCol = nOldCol; // Revert to the old column value before the call.
        }
        rFilterCxt.mnCol += static_cast<sal_uInt16>(pRefMember->GetSize( nMeasure ));

        if ( bTitleLine )           // title column is included in GetSize, so the following
            --rFilterCxt.mnCol;                 // positions are calculated with the normal values
    }

    tools::Long nUserSubStart;
    tools::Long nUserSubCount = pRefMember->GetSubTotalCount(&nUserSubStart);
    if ( !nUserSubCount && bHasChild )
        return;

    // Calculate at least automatic if no subtotals are selected,
    // show only own values if there's no child dimension (innermost).
    if ( !nUserSubCount || !bHasChild )
    {
        nUserSubCount = 1;
        nUserSubStart = 0;
    }

    ScDPSubTotalState aLocalSubState(rSubState);        // keep row state, modify column

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nSubSize = pResultData->GetCountForMeasure(nMeasure);
    if (bHasChild)
    {
        rFilterCxt.mnCol -= nSubSize * ( nUserSubCount - nUserSubStart );   // GetSize includes space for SubTotal
        rFilterCxt.mnCol -= nExtraSpace;                                    // GetSize includes the empty line
    }

    tools::Long nMoveSubTotal = 0;
    if ( bSubTotalInTitle )
    {
        nMoveSubTotal = rFilterCxt.mnCol - nStartCol;   // force to first (title) column
        rFilterCxt.mnCol = nStartCol;
    }

    for (tools::Long nUserPos=nUserSubStart; nUserPos<nUserSubCount; nUserPos++)
    {
        if ( pChildDimension && nUserSubCount > 1 )
        {
            const ScDPLevel* pForceLevel = pResultMember ? pResultMember->GetParentLevel() : nullptr;
            aLocalSubState.nColSubTotalFunc = nUserPos;
            aLocalSubState.eColForce = lcl_GetForceFunc( pForceLevel, nUserPos );
        }

        for ( tools::Long nSubCount=0; nSubCount<nSubSize; nSubCount++ )
        {
            if ( nMeasure == SC_DPMEASURE_ALL )
                nMemberMeasure = nSubCount;

            OSL_ENSURE( rFilterCxt.mnCol < rSequence.getLength(), "bumm" );
            sheet::DataResult& rRes = rSequence.getArray()[rFilterCxt.mnCol];

            if ( HasData( nMemberMeasure, aLocalSubState ) )
            {
                if ( HasError( nMemberMeasure, aLocalSubState ) )
                {
                    rRes.Value = 0;
                    rRes.Flags |= sheet::DataResultFlags::ERROR;
                }
                else
                {
                    rRes.Value = GetAggregate( nMemberMeasure, aLocalSubState );
                    rRes.Flags |= sheet::DataResultFlags::HASDATA;
                }
            }

            if ( bHasChild || bIsSubTotalRow )
                rRes.Flags |= sheet::DataResultFlags::SUBTOTAL;

            rFilterCxt.maFilterSet.add(rFilterCxt.maFilters, rRes.Value);
            rFilterCxt.mnCol += 1;
        }
    }

    // add extra space again if subtracted from GetSize above,
    // add to own size if no children
    rFilterCxt.mnCol += nExtraSpace;
    rFilterCxt.mnCol += nMoveSubTotal;
}

void ScDPDataMember::UpdateDataRow(
    const ScDPResultMember* pRefMember, tools::Long nMeasure, bool bIsSubTotalRow,
    const ScDPSubTotalState& rSubState )
{
    OSL_ENSURE( pRefMember == pResultMember || !pResultMember, "bla" );

    // Calculate must be called even if not visible (for use as reference value)
    const ScDPDataDimension* pDataChild = GetChildDimension();
    const ScDPResultDimension* pRefChild = pRefMember->GetChildDimension();

    //  leave space for children even if the DataMember hasn't been initialized
    //  (pDataChild is null then, this happens when no values for it are in this row)
    bool bHasChild = ( pRefChild != nullptr );

    // process subtotals even if not shown
    tools::Long nUserSubCount = pRefMember->GetSubTotalCount();

    // Calculate at least automatic if no subtotals are selected,
    // show only own values if there's no child dimension (innermost).
    if ( !nUserSubCount || !bHasChild )
        nUserSubCount = 1;

    ScDPSubTotalState aLocalSubState(rSubState);        // keep row state, modify column

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nSubSize = pResultData->GetCountForMeasure(nMeasure);

    for (tools::Long nUserPos=0; nUserPos<nUserSubCount; nUserPos++)   // including hidden "automatic"
    {
        if ( pChildDimension && nUserSubCount > 1 )
        {
            const ScDPLevel* pForceLevel = pResultMember ? pResultMember->GetParentLevel() : nullptr;
            aLocalSubState.nColSubTotalFunc = nUserPos;
            aLocalSubState.eColForce = lcl_GetForceFunc( pForceLevel, nUserPos );
        }

        for ( tools::Long nSubCount=0; nSubCount<nSubSize; nSubCount++ )
        {
            if ( nMeasure == SC_DPMEASURE_ALL )
                nMemberMeasure = nSubCount;

            // update data...
            ScDPAggData* pAggData = GetAggData( nMemberMeasure, aLocalSubState );
            if (pAggData)
            {
                //TODO: aLocalSubState?
                ScSubTotalFunc eFunc = pResultData->GetMeasureFunction( nMemberMeasure );
                sheet::DataPilotFieldReference aReferenceValue = pResultData->GetMeasureRefVal( nMemberMeasure );
                sal_Int32 eRefType = aReferenceValue.ReferenceType;

                // calculate the result first - for all members, regardless of reference value
                pAggData->Calculate( eFunc, aLocalSubState );

                if ( eRefType == sheet::DataPilotFieldReferenceType::ITEM_DIFFERENCE ||
                     eRefType == sheet::DataPilotFieldReferenceType::ITEM_PERCENTAGE ||
                     eRefType == sheet::DataPilotFieldReferenceType::ITEM_PERCENTAGE_DIFFERENCE )
                {
                    // copy the result into auxiliary value, so differences can be
                    // calculated in any order
                    pAggData->SetAuxiliary( pAggData->GetResult() );
                }
                // column/row percentage/index is now in UpdateRunningTotals, so it doesn't disturb sorting
            }
        }
    }

    if ( bHasChild )    // child dimension must be processed last, so the row total is known
    {
        if ( pDataChild )
            pDataChild->UpdateDataRow( pRefChild, nMeasure, bIsSubTotalRow, rSubState );
    }
}

void ScDPDataMember::SortMembers( ScDPResultMember* pRefMember )
{
    OSL_ENSURE( pRefMember == pResultMember || !pResultMember, "bla" );

    if ( pRefMember->IsVisible() )  //TODO: here or in ScDPDataDimension ???
    {
        ScDPDataDimension* pDataChild = GetChildDimension();
        ScDPResultDimension* pRefChild = pRefMember->GetChildDimension();
        if ( pRefChild && pDataChild )
            pDataChild->SortMembers( pRefChild );       // sorting is done at the dimension
    }
}

void ScDPDataMember::DoAutoShow( ScDPResultMember* pRefMember )
{
    OSL_ENSURE( pRefMember == pResultMember || !pResultMember, "bla" );

    if ( pRefMember->IsVisible() )  //TODO: here or in ScDPDataDimension ???
    {
        ScDPDataDimension* pDataChild = GetChildDimension();
        ScDPResultDimension* pRefChild = pRefMember->GetChildDimension();
        if ( pRefChild && pDataChild )
            pDataChild->DoAutoShow( pRefChild );       // sorting is done at the dimension
    }
}

void ScDPDataMember::ResetResults()
{
    aAggregate.Reset();

    ScDPDataDimension* pDataChild = GetChildDimension();
    if ( pDataChild )
        pDataChild->ResetResults();
}

void ScDPDataMember::UpdateRunningTotals(
    const ScDPResultMember* pRefMember, tools::Long nMeasure, bool bIsSubTotalRow,
    const ScDPSubTotalState& rSubState, ScDPRunningTotalState& rRunning,
    ScDPRowTotals& rTotals, const ScDPResultMember& rRowParent )
{
    OSL_ENSURE( pRefMember == pResultMember || !pResultMember, "bla" );

    const ScDPDataDimension* pDataChild = GetChildDimension();
    const ScDPResultDimension* pRefChild = pRefMember->GetChildDimension();

    bool bIsRoot = ( pResultMember == nullptr || pResultMember->GetParentLevel() == nullptr );

    //  leave space for children even if the DataMember hasn't been initialized
    //  (pDataChild is null then, this happens when no values for it are in this row)
    bool bHasChild = ( pRefChild != nullptr );

    tools::Long nUserSubCount = pRefMember->GetSubTotalCount();
    {
        // Calculate at least automatic if no subtotals are selected,
        // show only own values if there's no child dimension (innermost).
        if ( !nUserSubCount || !bHasChild )
            nUserSubCount = 1;

        ScDPSubTotalState aLocalSubState(rSubState);        // keep row state, modify column

        tools::Long nMemberMeasure = nMeasure;
        tools::Long nSubSize = pResultData->GetCountForMeasure(nMeasure);

        for (tools::Long nUserPos=0; nUserPos<nUserSubCount; nUserPos++)   // including hidden "automatic"
        {
            if ( pChildDimension && nUserSubCount > 1 )
            {
                const ScDPLevel* pForceLevel = pResultMember ? pResultMember->GetParentLevel() : nullptr;
                aLocalSubState.nColSubTotalFunc = nUserPos;
                aLocalSubState.eColForce = lcl_GetForceFunc( pForceLevel, nUserPos );
            }

            for ( tools::Long nSubCount=0; nSubCount<nSubSize; nSubCount++ )
            {
                if ( nMeasure == SC_DPMEASURE_ALL )
                    nMemberMeasure = nSubCount;

                // update data...
                ScDPAggData* pAggData = GetAggData( nMemberMeasure, aLocalSubState );
                if (pAggData)
                {
                    //TODO: aLocalSubState?
                    sheet::DataPilotFieldReference aReferenceValue = pResultData->GetMeasureRefVal( nMemberMeasure );
                    sal_Int32 eRefType = aReferenceValue.ReferenceType;

                    if ( eRefType == sheet::DataPilotFieldReferenceType::RUNNING_TOTAL ||
                         eRefType == sheet::DataPilotFieldReferenceType::ITEM_DIFFERENCE ||
                         eRefType == sheet::DataPilotFieldReferenceType::ITEM_PERCENTAGE ||
                         eRefType == sheet::DataPilotFieldReferenceType::ITEM_PERCENTAGE_DIFFERENCE )
                    {
                        bool bRunningTotal = ( eRefType == sheet::DataPilotFieldReferenceType::RUNNING_TOTAL );
                        bool bRelative =
                            ( aReferenceValue.ReferenceItemType != sheet::DataPilotFieldReferenceItemType::NAMED && !bRunningTotal );
                        tools::Long nRelativeDir = bRelative ?
                            ( ( aReferenceValue.ReferenceItemType == sheet::DataPilotFieldReferenceItemType::PREVIOUS ) ? -1 : 1 ) : 0;

                        const ScDPRunningTotalState::IndexArray& rColVisible = rRunning.GetColVisible();
                        const ScDPRunningTotalState::IndexArray& rColSorted = rRunning.GetColSorted();
                        const ScDPRunningTotalState::IndexArray& rRowVisible = rRunning.GetRowVisible();
                        const ScDPRunningTotalState::IndexArray& rRowSorted = rRunning.GetRowSorted();

                        OUString aRefFieldName = aReferenceValue.ReferenceField;

                        //TODO: aLocalSubState?
                        sheet::DataPilotFieldOrientation nRefOrient = pResultData->GetMeasureRefOrient( nMemberMeasure );
                        bool bRefDimInCol = ( nRefOrient == sheet::DataPilotFieldOrientation_COLUMN );
                        bool bRefDimInRow = ( nRefOrient == sheet::DataPilotFieldOrientation_ROW );

                        ScDPResultDimension* pSelectDim = nullptr;
                        sal_Int32 nRowPos = 0;
                        sal_Int32 nColPos = 0;

                        //  find the reference field in column or row dimensions

                        if ( bRefDimInRow )     //  look in row dimensions
                        {
                            pSelectDim = rRunning.GetRowResRoot()->GetChildDimension();
                            while ( pSelectDim && pSelectDim->GetName() != aRefFieldName )
                            {
                                tools::Long nIndex = rRowSorted[nRowPos];
                                if ( nIndex >= 0 && nIndex < pSelectDim->GetMemberCount() )
                                    pSelectDim = pSelectDim->GetMember(nIndex)->GetChildDimension();
                                else
                                    pSelectDim = nullptr;
                                ++nRowPos;
                            }
                            // child dimension of innermost member?
                            if ( pSelectDim && rRowSorted[nRowPos] < 0 )
                                pSelectDim = nullptr;
                        }

                        if ( bRefDimInCol )     //  look in column dimensions
                        {
                            pSelectDim = rRunning.GetColResRoot()->GetChildDimension();
                            while ( pSelectDim && pSelectDim->GetName() != aRefFieldName )
                            {
                                tools::Long nIndex = rColSorted[nColPos];
                                if ( nIndex >= 0 && nIndex < pSelectDim->GetMemberCount() )
                                    pSelectDim = pSelectDim->GetMember(nIndex)->GetChildDimension();
                                else
                                    pSelectDim = nullptr;
                                ++nColPos;
                            }
                            // child dimension of innermost member?
                            if ( pSelectDim && rColSorted[nColPos] < 0 )
                                pSelectDim = nullptr;
                        }

                        bool bNoDetailsInRef = false;
                        if ( pSelectDim && bRunningTotal )
                        {
                            //  Running totals:
                            //  If details are hidden for this member in the reference dimension,
                            //  don't show or sum up the value. Otherwise, for following members,
                            //  the running totals of details and subtotals wouldn't match.

                            tools::Long nMyIndex = bRefDimInCol ? rColSorted[nColPos] : rRowSorted[nRowPos];
                            if ( nMyIndex >= 0 && nMyIndex < pSelectDim->GetMemberCount() )
                            {
                                const ScDPResultMember* pMyRefMember = pSelectDim->GetMember(nMyIndex);
                                if ( pMyRefMember && pMyRefMember->HasHiddenDetails() )
                                {
                                    pSelectDim = nullptr;          // don't calculate
                                    bNoDetailsInRef = true;     // show error, not empty
                                }
                            }
                        }

                        if ( bRelative )
                        {
                            //  Difference/Percentage from previous/next:
                            //  If details are hidden for this member in the innermost column/row
                            //  dimension (the orientation of the reference dimension), show an
                            //  error value.
                            //  - If the no-details dimension is the reference dimension, its
                            //    members will be skipped when finding the previous/next member,
                            //    so there must be no results for its members.
                            //  - If the no-details dimension is outside of the reference dimension,
                            //    no calculation in the reference dimension is possible.
                            //  - Otherwise, the error isn't strictly necessary, but shown for
                            //    consistency.

                            bool bInnerNoDetails = bRefDimInCol ? HasHiddenDetails() :
                                                 ( !bRefDimInRow || rRowParent.HasHiddenDetails() );
                            if ( bInnerNoDetails )
                            {
                                pSelectDim = nullptr;
                                bNoDetailsInRef = true;         // show error, not empty
                            }
                        }

                        if ( !bRefDimInCol && !bRefDimInRow )   // invalid dimension specified
                            bNoDetailsInRef = true;             // pSelectDim is then already NULL

                        //  get the member for the reference item and do the calculation

                        if ( bRunningTotal )
                        {
                            // running total in (dimension) -> find first existing member

                            if ( pSelectDim )
                            {
                                ScDPDataMember* pSelectMember;
                                if ( bRefDimInCol )
                                    pSelectMember = ScDPResultDimension::GetColReferenceMember( nullptr, nullptr,
                                                                    nColPos, rRunning );
                                else
                                {
                                    const sal_Int32* pRowSorted = rRowSorted.data();
                                    const sal_Int32* pColSorted = rColSorted.data();
                                    pRowSorted += nRowPos + 1; // including the reference dimension
                                    pSelectMember = pSelectDim->GetRowReferenceMember(
                                        nullptr, nullptr, pRowSorted, pColSorted);
                                }

                                if ( pSelectMember )
                                {
                                    // The running total is kept as the auxiliary value in
                                    // the first available member for the reference dimension.
                                    // Members are visited in final order, so each one's result
                                    // can be used and then modified.

                                    ScDPAggData* pSelectData = pSelectMember->
                                                    GetAggData( nMemberMeasure, aLocalSubState );
                                    if ( pSelectData )
                                    {
                                        double fTotal = pSelectData->GetAuxiliary();
                                        fTotal += pAggData->GetResult();
                                        pSelectData->SetAuxiliary( fTotal );
                                        pAggData->SetResult( fTotal );
                                        pAggData->SetEmpty(false);              // always display
                                    }
                                }
                                else
                                    pAggData->SetError();
                            }
                            else if (bNoDetailsInRef)
                                pAggData->SetError();
                            else
                                pAggData->SetEmpty(true);                       // empty (dim set to 0 above)
                        }
                        else
                        {
                            // difference/percentage -> find specified member

                            if ( pSelectDim )
                            {
                                OUString aRefItemName = aReferenceValue.ReferenceItemName;
                                ScDPRelativePos aRefItemPos( 0, nRelativeDir );     // nBasePos is modified later

                                const OUString* pRefName = nullptr;
                                const ScDPRelativePos* pRefPos = nullptr;
                                if ( bRelative )
                                    pRefPos = &aRefItemPos;
                                else
                                    pRefName = &aRefItemName;

                                ScDPDataMember* pSelectMember;
                                if ( bRefDimInCol )
                                {
                                    aRefItemPos.nBasePos = rColVisible[nColPos];    // without sort order applied
                                    pSelectMember = ScDPResultDimension::GetColReferenceMember( pRefPos, pRefName,
                                                                    nColPos, rRunning );
                                }
                                else
                                {
                                    aRefItemPos.nBasePos = rRowVisible[nRowPos];    // without sort order applied
                                    const sal_Int32* pRowSorted = rRowSorted.data();
                                    const sal_Int32* pColSorted = rColSorted.data();
                                    pRowSorted += nRowPos + 1; // including the reference dimension
                                    pSelectMember = pSelectDim->GetRowReferenceMember(
                                        pRefPos, pRefName, pRowSorted, pColSorted);
                                }

                                // difference or perc.difference is empty for the reference item itself
                                if ( pSelectMember == this &&
                                     eRefType != sheet::DataPilotFieldReferenceType::ITEM_PERCENTAGE )
                                {
                                    pAggData->SetEmpty(true);
                                }
                                else if ( pSelectMember )
                                {
                                    const ScDPAggData* pOtherAggData = pSelectMember->
                                                        GetConstAggData( nMemberMeasure, aLocalSubState );
                                    OSL_ENSURE( pOtherAggData, "no agg data" );
                                    if ( pOtherAggData )
                                    {
                                        // Reference member may be visited before or after this one,
                                        // so the auxiliary value is used for the original result.

                                        double fOtherResult = pOtherAggData->GetAuxiliary();
                                        double fThisResult = pAggData->GetResult();
                                        bool bError = false;
                                        switch ( eRefType )
                                        {
                                            case sheet::DataPilotFieldReferenceType::ITEM_DIFFERENCE:
                                                fThisResult = fThisResult - fOtherResult;
                                                break;
                                            case sheet::DataPilotFieldReferenceType::ITEM_PERCENTAGE:
                                                if ( fOtherResult == 0.0 )
                                                    bError = true;
                                                else
                                                    fThisResult = fThisResult / fOtherResult;
                                                break;
                                            case sheet::DataPilotFieldReferenceType::ITEM_PERCENTAGE_DIFFERENCE:
                                                if ( fOtherResult == 0.0 )
                                                    bError = true;
                                                else
                                                    fThisResult = ( fThisResult - fOtherResult ) / fOtherResult;
                                                break;
                                            default:
                                                OSL_FAIL("invalid calculation type");
                                        }
                                        if ( bError )
                                        {
                                            pAggData->SetError();
                                        }
                                        else
                                        {
                                            pAggData->SetResult(fThisResult);
                                            pAggData->SetEmpty(false);              // always display
                                        }
                                        //TODO: errors in data?
                                    }
                                }
                                else if (bRelative && !bNoDetailsInRef)
                                    pAggData->SetEmpty(true);                   // empty
                                else
                                    pAggData->SetError();                       // error
                            }
                            else if (bNoDetailsInRef)
                                pAggData->SetError();                           // error
                            else
                                pAggData->SetEmpty(true);                       // empty
                        }
                    }
                    else if ( eRefType == sheet::DataPilotFieldReferenceType::ROW_PERCENTAGE ||
                              eRefType == sheet::DataPilotFieldReferenceType::COLUMN_PERCENTAGE ||
                              eRefType == sheet::DataPilotFieldReferenceType::TOTAL_PERCENTAGE ||
                              eRefType == sheet::DataPilotFieldReferenceType::INDEX )
                    {

                        //  set total values when they are encountered (always before their use)

                        ScDPAggData* pColTotalData = pRefMember->GetColTotal( nMemberMeasure );
                        ScDPAggData* pRowTotalData = rTotals.GetRowTotal( nMemberMeasure );
                        ScDPAggData* pGrandTotalData = rTotals.GetGrandTotal( nMemberMeasure );

                        double fTotalValue = pAggData->HasError() ? 0 : pAggData->GetResult();

                        if ( bIsRoot && rTotals.IsInColRoot() && pGrandTotalData )
                            pGrandTotalData->SetAuxiliary( fTotalValue );

                        if ( bIsRoot && pRowTotalData )
                            pRowTotalData->SetAuxiliary( fTotalValue );

                        if ( rTotals.IsInColRoot() && pColTotalData )
                            pColTotalData->SetAuxiliary( fTotalValue );

                        //  find relation to total values

                        switch ( eRefType )
                        {
                            case sheet::DataPilotFieldReferenceType::ROW_PERCENTAGE:
                            case sheet::DataPilotFieldReferenceType::COLUMN_PERCENTAGE:
                            case sheet::DataPilotFieldReferenceType::TOTAL_PERCENTAGE:
                                {
                                    double nTotal;
                                    if ( eRefType == sheet::DataPilotFieldReferenceType::ROW_PERCENTAGE )
                                        nTotal = pRowTotalData ? pRowTotalData->GetAuxiliary() : 0.0;
                                    else if ( eRefType == sheet::DataPilotFieldReferenceType::COLUMN_PERCENTAGE )
                                        nTotal = pColTotalData ? pColTotalData->GetAuxiliary() : 0.0;
                                    else
                                        nTotal = pGrandTotalData ? pGrandTotalData->GetAuxiliary() : 0.0;

                                    if ( nTotal == 0.0 )
                                        pAggData->SetError();
                                    else
                                        pAggData->SetResult( pAggData->GetResult() / nTotal );
                                }
                                break;
                            case sheet::DataPilotFieldReferenceType::INDEX:
                                {
                                    double nColTotal = pColTotalData ? pColTotalData->GetAuxiliary() : 0.0;
                                    double nRowTotal = pRowTotalData ? pRowTotalData->GetAuxiliary() : 0.0;
                                    double nGrandTotal = pGrandTotalData ? pGrandTotalData->GetAuxiliary() : 0.0;
                                    if ( nRowTotal == 0.0 || nColTotal == 0.0 )
                                        pAggData->SetError();
                                    else
                                        pAggData->SetResult(
                                            ( pAggData->GetResult() * nGrandTotal ) /
                                            ( nRowTotal * nColTotal ) );
                                }
                                break;
                        }
                    }
                }
            }
        }
    }

    if ( bHasChild )    // child dimension must be processed last, so the row total is known
    {
        if ( pDataChild )
            pDataChild->UpdateRunningTotals( pRefChild, nMeasure,
                                            bIsSubTotalRow, rSubState, rRunning, rTotals, rRowParent );
    }
}

#if DUMP_PIVOT_TABLE
void ScDPDataMember::DumpState( const ScDPResultMember* pRefMember, ScDocument* pDoc, ScAddress& rPos ) const
{
    dumpRow(u"ScDPDataMember"_ustr, GetName(), &aAggregate, pDoc, rPos);
    SCROW nStartRow = rPos.Row();

    const ScDPDataDimension* pDataChild = GetChildDimension();
    const ScDPResultDimension* pRefChild = pRefMember->GetChildDimension();
    if ( pDataChild && pRefChild )
        pDataChild->DumpState( pRefChild, pDoc, rPos );

    indent(pDoc, nStartRow, rPos);
}

void ScDPDataMember::Dump(int nIndent) const
{
    std::string aIndent(nIndent*2, ' ');
    std::cout << aIndent << "-- data member '"
        << (pResultMember ? pResultMember->GetName() : OUString()) << "'" << std::endl;
    for (const ScDPAggData* pAgg = &aAggregate; pAgg; pAgg = pAgg->GetExistingChild())
        pAgg->Dump(nIndent+1);

    if (pChildDimension)
        pChildDimension->Dump(nIndent+1);
}
#endif

//  Helper class to select the members to include in
//  ScDPResultDimension::InitFrom or LateInitFrom if groups are used

namespace {

class ScDPGroupCompare
{
private:
    const ScDPResultData* pResultData;
    const ScDPInitState& rInitState;
    tools::Long                 nDimSource;
    bool                 bIncludeAll;
    bool                 bIsBase;
    tools::Long                 nGroupBase;
public:
            ScDPGroupCompare( const ScDPResultData* pData, const ScDPInitState& rState, tools::Long nDimension );

    bool    IsIncluded( const ScDPMember& rMember )     { return bIncludeAll || TestIncluded( rMember ); }
    bool    TestIncluded( const ScDPMember& rMember );
};

}

ScDPGroupCompare::ScDPGroupCompare( const ScDPResultData* pData, const ScDPInitState& rState, tools::Long nDimension ) :
    pResultData( pData ),
    rInitState( rState ),
    nDimSource( nDimension )
{
    bIsBase = pResultData->IsBaseForGroup( nDimSource );
    nGroupBase = pResultData->GetGroupBase( nDimSource );      //TODO: get together in one call?

    // if bIncludeAll is set, TestIncluded doesn't need to be called
    bIncludeAll = !( bIsBase || nGroupBase >= 0 );
}

bool ScDPGroupCompare::TestIncluded( const ScDPMember& rMember )
{
    bool bInclude = true;
    if ( bIsBase )
    {
        // need to check all previous groups
        //TODO: get array of groups (or indexes) before loop?
        ScDPItemData aMemberData(rMember.FillItemData());

        const std::vector<ScDPInitState::Member>& rMemStates = rInitState.GetMembers();
        bInclude = std::all_of(rMemStates.begin(), rMemStates.end(),
            [this, &aMemberData](const ScDPInitState::Member& rMem) {
                return (pResultData->GetGroupBase(rMem.mnSrcIndex) != nDimSource)
                    || pResultData->IsInGroup(rMem.mnNameIndex, rMem.mnSrcIndex, aMemberData, nDimSource);
            });
    }
    else if ( nGroupBase >= 0 )
    {
        // base isn't used in preceding fields
        // -> look for other groups using the same base

        //TODO: get array of groups (or indexes) before loop?
        ScDPItemData aMemberData(rMember.FillItemData());
        const std::vector<ScDPInitState::Member>& rMemStates = rInitState.GetMembers();
        bInclude = std::all_of(rMemStates.begin(), rMemStates.end(),
            [this, &aMemberData](const ScDPInitState::Member& rMem) {
                // coverity[copy_paste_error : FALSE] - same base (hierarchy between
                // the two groups is irrelevant)
                return (pResultData->GetGroupBase(rMem.mnSrcIndex) != nGroupBase)
                    || pResultData->HasCommonElement(rMem.mnNameIndex, rMem.mnSrcIndex, aMemberData, nDimSource);
            });
    }

    return bInclude;
}

ScDPResultDimension::ScDPResultDimension( const ScDPResultData* pData ) :
    pResultData( pData ),
    nSortMeasure( 0 ),
    bIsDataLayout( false ),
    bSortByData( false ),
    bSortAscending( false ),
    bAutoShow( false ),
    bAutoTopItems( false ),
    bInitialized( false ),
    nAutoMeasure( 0 ),
    nAutoCount( 0 )
{
}

ScDPResultDimension::~ScDPResultDimension()
{
}

ScDPResultMember *ScDPResultDimension::FindMember(  SCROW  iData ) const
{
    if( bIsDataLayout )
    {
        SAL_WARN_IF(maMemberArray.empty(), "sc.core", "MemberArray is empty");
        return !maMemberArray.empty() ? maMemberArray[0].get() : nullptr;
    }

    MemberHash::const_iterator aRes = maMemberHash.find( iData );
    if( aRes != maMemberHash.end()) {
        if ( aRes->second->IsNamedItem( iData ) )
            return aRes->second;
        OSL_FAIL("problem!  hash result is not the same as IsNamedItem");
    }

    unsigned int i;
    unsigned int nCount = maMemberArray.size();
    for( i = 0; i < nCount ; i++ )
    {
        ScDPResultMember* pResultMember = maMemberArray[i].get();
        if ( pResultMember->IsNamedItem( iData ) )
            return pResultMember;
    }
    return nullptr;
}

void ScDPResultDimension::InitFrom(
    const vector<ScDPDimension*>& ppDim, const vector<ScDPLevel*>& ppLev,
    size_t nPos, ScDPInitState& rInitState,  bool bInitChild )
{
    if (nPos >= ppDim.size() || nPos >= ppLev.size())
    {
        bInitialized = true;
        return;
    }

    ScDPDimension* pThisDim = ppDim[nPos];
    ScDPLevel* pThisLevel = ppLev[nPos];

    if (!pThisDim || !pThisLevel)
    {
        bInitialized = true;
        return;
    }

    bIsDataLayout = pThisDim->getIsDataLayoutDimension();   // member
    aDimensionName = pThisDim->getName();                   // member

    // Check the autoshow setting.  If it's enabled, store the settings.
    const sheet::DataPilotFieldAutoShowInfo& rAutoInfo = pThisLevel->GetAutoShow();
    if ( rAutoInfo.IsEnabled )
    {
        bAutoShow     = true;
        bAutoTopItems = ( rAutoInfo.ShowItemsMode == sheet::DataPilotFieldShowItemsMode::FROM_TOP );
        nAutoMeasure  = pThisLevel->GetAutoMeasure();
        nAutoCount    = rAutoInfo.ItemCount;
    }

    // Check the sort info, and store the settings if appropriate.
    const sheet::DataPilotFieldSortInfo& rSortInfo = pThisLevel->GetSortInfo();
    if ( rSortInfo.Mode == sheet::DataPilotFieldSortMode::DATA )
    {
        bSortByData = true;
        bSortAscending = rSortInfo.IsAscending;
        nSortMeasure = pThisLevel->GetSortMeasure();
    }

    // global order is used to initialize aMembers, so it doesn't have to be looked at later
    const ScMemberSortOrder& rGlobalOrder = pThisLevel->GetGlobalOrder();

    tools::Long nDimSource = pThisDim->GetDimension();     //TODO: check GetSourceDim?
    ScDPGroupCompare aCompare( pResultData, rInitState, nDimSource );

    // Now, go through all members and initialize them.
    ScDPMembers* pMembers = pThisLevel->GetMembersObject();
    tools::Long nMembCount = pMembers->getCount();
    for ( tools::Long i=0; i<nMembCount; i++ )
    {
        tools::Long nSorted = rGlobalOrder.empty() ? i : rGlobalOrder[i];

        ScDPMember* pMember = pMembers->getByIndex(nSorted);
        if ( aCompare.IsIncluded( *pMember ) )
        {
            ScDPParentDimData aData( i, pThisDim, pThisLevel, pMember);
            ScDPResultMember* pNew = AddMember( aData );

            rInitState.AddMember(nDimSource, pNew->GetDataId());
            pNew->InitFrom( ppDim, ppLev, nPos+1, rInitState, bInitChild  );
            rInitState.RemoveMember();
        }
    }
    bInitialized = true;
}

void ScDPResultDimension::LateInitFrom(
    LateInitParams& rParams, const vector<SCROW>& pItemData, size_t nPos, ScDPInitState& rInitState)
{
    if ( rParams.IsEnd( nPos ) )
        return;
    if (nPos >= pItemData.size())
    {
        SAL_WARN("sc.core", "pos " << nPos << ", but vector size is " << pItemData.size());
        return;
    }
    SCROW rThisData = pItemData[nPos];
    ScDPDimension* pThisDim = rParams.GetDim( nPos );
    ScDPLevel* pThisLevel = rParams.GetLevel( nPos );

    if (!pThisDim || !pThisLevel)
        return;

    tools::Long nDimSource = pThisDim->GetDimension();     //TODO: check GetSourceDim?

    bool bShowEmpty = pThisLevel->getShowEmpty();

    if ( !bInitialized )
    { // init some values
        //  create all members at the first call (preserve order)
        bIsDataLayout = pThisDim->getIsDataLayoutDimension();
        aDimensionName = pThisDim->getName();

        const sheet::DataPilotFieldAutoShowInfo& rAutoInfo = pThisLevel->GetAutoShow();
        if ( rAutoInfo.IsEnabled )
        {
            bAutoShow     = true;
            bAutoTopItems = ( rAutoInfo.ShowItemsMode == sheet::DataPilotFieldShowItemsMode::FROM_TOP );
            nAutoMeasure  = pThisLevel->GetAutoMeasure();
            nAutoCount    = rAutoInfo.ItemCount;
        }

        const sheet::DataPilotFieldSortInfo& rSortInfo = pThisLevel->GetSortInfo();
        if ( rSortInfo.Mode == sheet::DataPilotFieldSortMode::DATA )
        {
            bSortByData = true;
            bSortAscending = rSortInfo.IsAscending;
            nSortMeasure = pThisLevel->GetSortMeasure();
        }
    }

    bool bLateInitAllMembers=  bIsDataLayout || rParams.GetInitAllChild() || bShowEmpty;

    if ( !bLateInitAllMembers )
    {
        ResultMembers& rMembers = pResultData->GetDimResultMembers(nDimSource, pThisDim, pThisLevel);
        bLateInitAllMembers = rMembers.IsHasHideDetailsMembers();

        SAL_INFO("sc.core", aDimensionName << (rMembers.IsHasHideDetailsMembers() ? " HasHideDetailsMembers" : ""));

        rMembers.SetHasHideDetailsMembers( false );
    }

    bool bNewAllMembers = (!rParams.IsRow()) ||  nPos == 0 || bLateInitAllMembers;

    if (bNewAllMembers )
    {
      // global order is used to initialize aMembers, so it doesn't have to be looked at later
        if ( !bInitialized )
        { //init all members
            const ScMemberSortOrder& rGlobalOrder = pThisLevel->GetGlobalOrder();

            ScDPGroupCompare aCompare( pResultData, rInitState, nDimSource );
            ScDPMembers* pMembers = pThisLevel->GetMembersObject();
            tools::Long nMembCount = pMembers->getCount();
            for ( tools::Long i=0; i<nMembCount; i++ )
            {
                tools::Long nSorted = rGlobalOrder.empty() ? i : rGlobalOrder[i];

                ScDPMember* pMember = pMembers->getByIndex(nSorted);
                if ( aCompare.IsIncluded( *pMember ) )
                { // add all members
                    ScDPParentDimData aData( i, pThisDim, pThisLevel, pMember );
                    AddMember( aData );
                }
            }
            bInitialized = true;    // don't call again, even if no members were included
        }
        //  initialize only specific member (or all if "show empty" flag is set)
        if ( bLateInitAllMembers  )
        {
            tools::Long nCount = maMemberArray.size();
            for (tools::Long i=0; i<nCount; i++)
            {
                ScDPResultMember* pResultMember = maMemberArray[i].get();

                // check show empty
                bool bAllChildren = false;
                if( bShowEmpty )
                {
                    bAllChildren = !pResultMember->IsNamedItem( rThisData );
                }
                rParams.SetInitAllChildren( bAllChildren );
                rInitState.AddMember( nDimSource,  pResultMember->GetDataId() );
                pResultMember->LateInitFrom( rParams, pItemData, nPos+1, rInitState );
                rInitState.RemoveMember();
            }
        }
        else
        {
            ScDPResultMember* pResultMember = FindMember( rThisData );
            if( nullptr != pResultMember )
            {
                rInitState.AddMember( nDimSource,  pResultMember->GetDataId() );
                pResultMember->LateInitFrom( rParams, pItemData, nPos+1, rInitState );
                rInitState.RemoveMember();
            }
        }
    }
    else
        InitWithMembers( rParams, pItemData, nPos, rInitState );
}

tools::Long ScDPResultDimension::GetSize(tools::Long nMeasure) const
{
    tools::Long nMemberCount = maMemberArray.size();
    if (!nMemberCount)
        return 0;

    tools::Long nTotal = 0;
    if (bIsDataLayout)
    {
        OSL_ENSURE(nMeasure == SC_DPMEASURE_ALL || pResultData->GetMeasureCount() == 1,
                    "DataLayout dimension twice?");
        //  repeat first member...
        nTotal = nMemberCount * maMemberArray[0]->GetSize(0);   // all measures have equal size
    }
    else
    {
        //  add all members
        for (tools::Long nMem=0; nMem<nMemberCount; nMem++)
            nTotal += maMemberArray[nMem]->GetSize(nMeasure);
    }
    return nTotal;
}

bool ScDPResultDimension::IsValidEntry( const vector< SCROW >& aMembers ) const
{
    if (aMembers.empty())
        return false;

    const ScDPResultMember* pMember = FindMember( aMembers[0] );
    if ( nullptr != pMember )
        return pMember->IsValidEntry( aMembers );
#if OSL_DEBUG_LEVEL > 0
    SAL_INFO("sc.core", "IsValidEntry: Member not found, DimNam = "  << GetName());
#endif
    return false;
}

void ScDPResultDimension::ProcessData( const vector< SCROW >& aMembers,
                                       const ScDPResultDimension* pDataDim,
                                       const vector< SCROW >& aDataMembers,
                                       const vector<ScDPValue>& aValues ) const
{
    if (aMembers.empty())
        return;

    ScDPResultMember* pMember = FindMember( aMembers[0] );
    if ( nullptr != pMember )
    {
        vector<SCROW> aChildMembers;
        if (aMembers.size() > 1)
        {
            vector<SCROW>::const_iterator itr = aMembers.begin();
            aChildMembers.insert(aChildMembers.begin(), ++itr, aMembers.end());
        }
        pMember->ProcessData( aChildMembers, pDataDim, aDataMembers, aValues );
        return;
    }

    OSL_FAIL("ProcessData: Member not found");
}

void ScDPResultDimension::FillMemberResults( uno::Sequence<sheet::MemberResult>* pSequences,
                                                tools::Long nStart, tools::Long nMeasure )
{
    tools::Long nPos = nStart;
    tools::Long nCount = maMemberArray.size();

    for (tools::Long i=0; i<nCount; i++)
    {
        tools::Long nSorted = aMemberOrder.empty() ? i : aMemberOrder[i];

        ScDPResultMember* pMember = maMemberArray[nSorted].get();
        //  in data layout dimension, use first member with different measures/names
        if ( bIsDataLayout )
        {
            bool bTotalResult = false;
            OUString aMbrName = pResultData->GetMeasureDimensionName( nSorted );
            OUString aMbrCapt = pResultData->GetMeasureString( nSorted, false, SUBTOTAL_FUNC_NONE, bTotalResult );
            maMemberArray[0]->FillMemberResults( pSequences, nPos, nSorted, false, &aMbrName, &aMbrCapt );
        }
        else if ( pMember->IsVisible() )
        {
            pMember->FillMemberResults( pSequences, nPos, nMeasure, false, nullptr, nullptr );
        }
        // nPos is modified
    }
}

void ScDPResultDimension::FillDataResults(
    const ScDPResultMember* pRefMember, ScDPResultFilterContext& rFilterCxt,
    uno::Sequence< uno::Sequence<sheet::DataResult> >& rSequence, tools::Long nMeasure) const
{
    FilterStack aFilterStack(rFilterCxt.maFilters);
    aFilterStack.pushDimName(GetName(), bIsDataLayout);

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nCount = maMemberArray.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        tools::Long nSorted = aMemberOrder.empty() ? i : aMemberOrder[i];

        const ScDPResultMember* pMember;
        if (bIsDataLayout)
        {
            OSL_ENSURE(nMeasure == SC_DPMEASURE_ALL || pResultData->GetMeasureCount() == 1,
                        "DataLayout dimension twice?");
            pMember = maMemberArray[0].get();
            nMemberMeasure = nSorted;
        }
        else
            pMember = maMemberArray[nSorted].get();

        if ( pMember->IsVisible() )
            pMember->FillDataResults(pRefMember, rFilterCxt, rSequence, nMemberMeasure);
    }
}

void ScDPResultDimension::UpdateDataResults( const ScDPResultMember* pRefMember, tools::Long nMeasure ) const
{
    tools::Long nMemberMeasure = nMeasure;
    tools::Long nCount = maMemberArray.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        const ScDPResultMember* pMember;
        if (bIsDataLayout)
        {
            OSL_ENSURE(nMeasure == SC_DPMEASURE_ALL || pResultData->GetMeasureCount() == 1,
                        "DataLayout dimension twice?");
            pMember = maMemberArray[0].get();
            nMemberMeasure = i;
        }
        else
            pMember = maMemberArray[i].get();

        if ( pMember->IsVisible() )
            pMember->UpdateDataResults( pRefMember, nMemberMeasure );
    }
}

void ScDPResultDimension::SortMembers( ScDPResultMember* pRefMember )
{
    tools::Long nCount = maMemberArray.size();

    if ( bSortByData )
    {
        // sort members

        OSL_ENSURE( aMemberOrder.empty(), "sort twice?" );
        aMemberOrder.resize( nCount );
        for (tools::Long nPos=0; nPos<nCount; nPos++)
            aMemberOrder[nPos] = nPos;

        ScDPRowMembersOrder aComp( *this, nSortMeasure, bSortAscending );
        ::std::sort( aMemberOrder.begin(), aMemberOrder.end(), aComp );
    }

    // handle children

    // for data layout, call only once - sorting measure is always taken from settings
    tools::Long nLoopCount = bIsDataLayout ? std::min<tools::Long>(1, nCount) : nCount;
    for (tools::Long i=0; i<nLoopCount; i++)
    {
        ScDPResultMember* pMember = maMemberArray[i].get();
        if ( pMember->IsVisible() )
            pMember->SortMembers( pRefMember );
    }
}

void ScDPResultDimension::DoAutoShow( ScDPResultMember* pRefMember )
{
    tools::Long nCount = maMemberArray.size();

    // handle children first, before changing the visible state

    // for data layout, call only once - sorting measure is always taken from settings
    tools::Long nLoopCount = bIsDataLayout ? 1 : nCount;
    for (tools::Long i=0; i<nLoopCount; i++)
    {
        ScDPResultMember* pMember = maMemberArray[i].get();
        if ( pMember->IsVisible() )
            pMember->DoAutoShow( pRefMember );
    }

    if ( !(bAutoShow && nAutoCount > 0 && nAutoCount < nCount) )
        return;

    // establish temporary order, hide remaining members

    ScMemberSortOrder aAutoOrder;
    aAutoOrder.resize( nCount );
    tools::Long nPos;
    for (nPos=0; nPos<nCount; nPos++)
        aAutoOrder[nPos] = nPos;

    ScDPRowMembersOrder aComp( *this, nAutoMeasure, !bAutoTopItems );
    ::std::sort( aAutoOrder.begin(), aAutoOrder.end(), aComp );

    // look for equal values to the last included one

    tools::Long nIncluded = nAutoCount;
    const ScDPResultMember* pMember1 = maMemberArray[aAutoOrder[nIncluded - 1]].get();
    const ScDPDataMember* pDataMember1 = pMember1->IsVisible() ? pMember1->GetDataRoot() : nullptr;
    bool bContinue = true;
    while ( bContinue )
    {
        bContinue = false;
        if ( nIncluded < nCount )
        {
            const ScDPResultMember* pMember2 = maMemberArray[aAutoOrder[nIncluded]].get();
            const ScDPDataMember* pDataMember2 = pMember2->IsVisible() ? pMember2->GetDataRoot() : nullptr;

            if ( lcl_IsEqual( pDataMember1, pDataMember2, nAutoMeasure ) )
            {
                ++nIncluded;                // include more members if values are equal
                bContinue = true;
            }
        }
    }

    // hide the remaining members

    for (nPos = nIncluded; nPos < nCount; nPos++)
    {
        ScDPResultMember* pMember = maMemberArray[aAutoOrder[nPos]].get();
        pMember->SetAutoHidden();
    }
}

void ScDPResultDimension::ResetResults()
{
    tools::Long nCount = maMemberArray.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        // sort order doesn't matter
        ScDPResultMember* pMember = maMemberArray[bIsDataLayout ? 0 : i].get();
        pMember->ResetResults();
    }
}

tools::Long ScDPResultDimension::GetSortedIndex( tools::Long nUnsorted ) const
{
    return aMemberOrder.empty() ? nUnsorted : aMemberOrder[nUnsorted];
}

void ScDPResultDimension::UpdateRunningTotals( const ScDPResultMember* pRefMember, tools::Long nMeasure,
                                                ScDPRunningTotalState& rRunning, ScDPRowTotals& rTotals ) const
{
    const ScDPResultMember* pMember;
    tools::Long nMemberMeasure = nMeasure;
    tools::Long nCount = maMemberArray.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        tools::Long nSorted = aMemberOrder.empty() ? i : aMemberOrder[i];

        if (bIsDataLayout)
        {
            OSL_ENSURE(nMeasure == SC_DPMEASURE_ALL || pResultData->GetMeasureCount() == 1,
                        "DataLayout dimension twice?");
            pMember = maMemberArray[0].get();
            nMemberMeasure = nSorted;
        }
        else
            pMember = maMemberArray[nSorted].get();

        if ( pMember->IsVisible() )
        {
            if ( bIsDataLayout )
                rRunning.AddRowIndex( 0, 0 );
            else
                rRunning.AddRowIndex( i, nSorted );
            pMember->UpdateRunningTotals( pRefMember, nMemberMeasure, rRunning, rTotals );
            rRunning.RemoveRowIndex();
        }
    }
}

ScDPDataMember* ScDPResultDimension::GetRowReferenceMember(
    const ScDPRelativePos* pRelativePos, const OUString* pName,
    const sal_Int32* pRowIndexes, const sal_Int32* pColIndexes ) const
{
    // get named, previous/next, or first member of this dimension (first existing if pRelativePos and pName are NULL)

    OSL_ENSURE( pRelativePos == nullptr || pName == nullptr, "can't use position and name" );

    ScDPDataMember* pColMember = nullptr;

    bool bFirstExisting = ( pRelativePos == nullptr && pName == nullptr );
    tools::Long nMemberCount = maMemberArray.size();
    tools::Long nMemberIndex = 0;      // unsorted
    tools::Long nDirection = 1;        // forward if no relative position is used
    if ( pRelativePos )
    {
        nDirection = pRelativePos->nDirection;
        nMemberIndex = pRelativePos->nBasePos + nDirection;     // bounds are handled below

        OSL_ENSURE( nDirection == 1 || nDirection == -1, "Direction must be 1 or -1" );
    }
    else if ( pName )
    {
        // search for named member

        const ScDPResultMember* pRowMember = maMemberArray[GetSortedIndex(nMemberIndex)].get();

        //TODO: use ScDPItemData, as in ScDPDimension::IsValidPage?
        while ( pRowMember && pRowMember->GetName() != *pName )
        {
            ++nMemberIndex;
            if ( nMemberIndex < nMemberCount )
                pRowMember = maMemberArray[GetSortedIndex(nMemberIndex)].get();
            else
                pRowMember = nullptr;
        }
    }

    bool bContinue = true;
    while ( bContinue && nMemberIndex >= 0 && nMemberIndex < nMemberCount )
    {
        const ScDPResultMember* pRowMember = maMemberArray[GetSortedIndex(nMemberIndex)].get();

        // get child members by given indexes

        const sal_Int32* pNextRowIndex = pRowIndexes;
        while ( *pNextRowIndex >= 0 && pRowMember )
        {
            const ScDPResultDimension* pRowChild = pRowMember->GetChildDimension();
            if ( pRowChild && *pNextRowIndex < pRowChild->GetMemberCount() )
                pRowMember = pRowChild->GetMember( *pNextRowIndex );
            else
                pRowMember = nullptr;
            ++pNextRowIndex;
        }

        if ( pRowMember && pRelativePos )
        {
            //  Skip the member if it has hidden details
            //  (because when looking for the details, it is skipped, too).
            //  Also skip if the member is invisible because it has no data,
            //  for consistent ordering.
            if ( pRowMember->HasHiddenDetails() || !pRowMember->IsVisible() )
                pRowMember = nullptr;
        }

        if ( pRowMember )
        {
            pColMember = pRowMember->GetDataRoot();

            const sal_Int32* pNextColIndex = pColIndexes;
            while ( *pNextColIndex >= 0 && pColMember )
            {
                ScDPDataDimension* pColChild = pColMember->GetChildDimension();
                if ( pColChild && *pNextColIndex < pColChild->GetMemberCount() )
                    pColMember = pColChild->GetMember( *pNextColIndex );
                else
                    pColMember = nullptr;
                ++pNextColIndex;
            }
        }

        // continue searching only if looking for first existing or relative position
        bContinue = ( pColMember == nullptr && ( bFirstExisting || pRelativePos ) );
        nMemberIndex += nDirection;
    }

    return pColMember;
}

ScDPDataMember* ScDPResultDimension::GetColReferenceMember(
    const ScDPRelativePos* pRelativePos, const OUString* pName,
    sal_Int32 nRefDimPos, const ScDPRunningTotalState& rRunning )
{
    OSL_ENSURE( pRelativePos == nullptr || pName == nullptr, "can't use position and name" );

    const sal_Int32* pColIndexes = rRunning.GetColSorted().data();
    const sal_Int32* pRowIndexes = rRunning.GetRowSorted().data();

    // get own row member using all indexes

    const ScDPResultMember* pRowMember = rRunning.GetRowResRoot();
    ScDPDataMember* pColMember = nullptr;

    const sal_Int32* pNextRowIndex = pRowIndexes;
    while ( *pNextRowIndex >= 0 && pRowMember )
    {
        const ScDPResultDimension* pRowChild = pRowMember->GetChildDimension();
        if ( pRowChild && *pNextRowIndex < pRowChild->GetMemberCount() )
            pRowMember = pRowChild->GetMember( *pNextRowIndex );
        else
            pRowMember = nullptr;
        ++pNextRowIndex;
    }

    // get column (data) members before the reference field
    //TODO: pass rRowParent from ScDPDataMember::UpdateRunningTotals instead

    if ( pRowMember )
    {
        pColMember = pRowMember->GetDataRoot();

        const sal_Int32* pNextColIndex = pColIndexes;
        sal_Int32 nColSkipped = 0;
        while ( *pNextColIndex >= 0 && pColMember && nColSkipped < nRefDimPos )
        {
            ScDPDataDimension* pColChild = pColMember->GetChildDimension();
            if ( pColChild && *pNextColIndex < pColChild->GetMemberCount() )
                pColMember = pColChild->GetMember( *pNextColIndex );
            else
                pColMember = nullptr;
            ++pNextColIndex;
            ++nColSkipped;
        }
    }

    // get column member for the reference field

    if ( pColMember )
    {
        ScDPDataDimension* pReferenceDim = pColMember->GetChildDimension();
        if ( pReferenceDim )
        {
            tools::Long nReferenceCount = pReferenceDim->GetMemberCount();

            bool bFirstExisting = ( pRelativePos == nullptr && pName == nullptr );
            tools::Long nMemberIndex = 0;      // unsorted
            tools::Long nDirection = 1;        // forward if no relative position is used
            pColMember = nullptr;          // don't use parent dimension's member if none found
            if ( pRelativePos )
            {
                nDirection = pRelativePos->nDirection;
                nMemberIndex = pRelativePos->nBasePos + nDirection;     // bounds are handled below
            }
            else if ( pName )
            {
                // search for named member

                pColMember = pReferenceDim->GetMember( pReferenceDim->GetSortedIndex( nMemberIndex ) );

                //TODO: use ScDPItemData, as in ScDPDimension::IsValidPage?
                while ( pColMember && pColMember->GetName() != *pName )
                {
                    ++nMemberIndex;
                    if ( nMemberIndex < nReferenceCount )
                        pColMember = pReferenceDim->GetMember( pReferenceDim->GetSortedIndex( nMemberIndex ) );
                    else
                        pColMember = nullptr;
                }
            }

            bool bContinue = true;
            while ( bContinue && nMemberIndex >= 0 && nMemberIndex < nReferenceCount )
            {
                pColMember = pReferenceDim->GetMember( pReferenceDim->GetSortedIndex( nMemberIndex ) );

                // get column members below the reference field

                const sal_Int32* pNextColIndex = pColIndexes + nRefDimPos + 1;
                while ( *pNextColIndex >= 0 && pColMember )
                {
                    ScDPDataDimension* pColChild = pColMember->GetChildDimension();
                    if ( pColChild && *pNextColIndex < pColChild->GetMemberCount() )
                        pColMember = pColChild->GetMember( *pNextColIndex );
                    else
                        pColMember = nullptr;
                    ++pNextColIndex;
                }

                if ( pColMember && pRelativePos )
                {
                    //  Skip the member if it has hidden details
                    //  (because when looking for the details, it is skipped, too).
                    //  Also skip if the member is invisible because it has no data,
                    //  for consistent ordering.
                    if ( pColMember->HasHiddenDetails() || !pColMember->IsVisible() )
                        pColMember = nullptr;
                }

                // continue searching only if looking for first existing or relative position
                bContinue = ( pColMember == nullptr && ( bFirstExisting || pRelativePos ) );
                nMemberIndex += nDirection;
            }
        }
        else
            pColMember = nullptr;
    }

    return pColMember;
}

#if DUMP_PIVOT_TABLE
void ScDPResultDimension::DumpState( const ScDPResultMember* pRefMember, ScDocument* pDoc, ScAddress& rPos ) const
{
    OUString aDimName = bIsDataLayout ? u"(data layout)"_ustr : GetName();
    dumpRow(u"ScDPResultDimension"_ustr, aDimName, nullptr, pDoc, rPos);

    SCROW nStartRow = rPos.Row();

    tools::Long nCount = bIsDataLayout ? 1 : maMemberArray.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        const ScDPResultMember* pMember = maMemberArray[i].get();
        pMember->DumpState( pRefMember, pDoc, rPos );
    }

    indent(pDoc, nStartRow, rPos);
}

void ScDPResultDimension::Dump(int nIndent) const
{
    std::string aIndent(nIndent*2, ' ');
    std::cout << aIndent << "-- dimension '" << GetName() << "'" << std::endl;
    for (const auto& rxMember : maMemberArray)
    {
        const ScDPResultMember* p = rxMember.get();
        p->Dump(nIndent+1);
    }
}
#endif

tools::Long ScDPResultDimension::GetMemberCount() const
{
    return maMemberArray.size();
}

const ScDPResultMember* ScDPResultDimension::GetMember(tools::Long n) const
{
    return maMemberArray[n].get();
}
ScDPResultMember* ScDPResultDimension::GetMember(tools::Long n)
{
    return maMemberArray[n].get();
}

ScDPResultDimension* ScDPResultDimension::GetFirstChildDimension() const
{
    if ( !maMemberArray.empty() )
        return maMemberArray[0]->GetChildDimension();
    else
        return nullptr;
}

void ScDPResultDimension::FillVisibilityData(ScDPResultVisibilityData& rData) const
{
    if (IsDataLayout())
        return;

    for (const auto& rxMember : maMemberArray)
    {
        ScDPResultMember* pMember = rxMember.get();
        if (pMember->IsValid())
        {
            ScDPItemData aItem(pMember->FillItemData());
            rData.addVisibleMember(GetName(), aItem);
            pMember->FillVisibilityData(rData);
        }
    }
}

ScDPDataDimension::ScDPDataDimension( const ScDPResultData* pData ) :
    pResultData( pData ),
    pResultDimension( nullptr ),
    bIsDataLayout( false )
{
}

ScDPDataDimension::~ScDPDataDimension()
{
}

void ScDPDataDimension::InitFrom( const ScDPResultDimension* pDim )
{
    if (!pDim)
        return;

    pResultDimension = pDim;
    bIsDataLayout = pDim->IsDataLayout();

    // Go through all result members under the given result dimension, and
    // create a new data member instance for each result member.
    tools::Long nCount = pDim->GetMemberCount();
    for (tools::Long i=0; i<nCount; i++)
    {
        const ScDPResultMember* pResMem = pDim->GetMember(i);

        ScDPDataMember* pNew = new ScDPDataMember( pResultData, pResMem );
        maMembers.emplace_back( pNew);

        if ( !pResultData->IsLateInit() )
        {
            //  with LateInit, pResMem hasn't necessarily been initialized yet,
            //  so InitFrom for the new result member is called from its ProcessData method

            const ScDPResultDimension* pChildDim = pResMem->GetChildDimension();
            if ( pChildDim )
                pNew->InitFrom( pChildDim );
        }
    }
}

void ScDPDataDimension::ProcessData( const vector< SCROW >& aDataMembers, const vector<ScDPValue>& aValues,
                                     const ScDPSubTotalState& rSubState )
{
    // the ScDPItemData array must contain enough entries for all dimensions - this isn't checked

    tools::Long nCount = maMembers.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        ScDPDataMember* pMember = maMembers[static_cast<sal_uInt16>(i)].get();

        // always first member for data layout dim
        if ( bIsDataLayout || ( !aDataMembers.empty() && pMember->IsNamedItem(aDataMembers[0]) ) )
        {
            vector<SCROW> aChildDataMembers;
            if (aDataMembers.size() > 1)
            {
                vector<SCROW>::const_iterator itr = aDataMembers.begin();
                aChildDataMembers.insert(aChildDataMembers.begin(), ++itr, aDataMembers.end());
            }
            pMember->ProcessData( aChildDataMembers, aValues, rSubState );
            return;
        }
    }

    OSL_FAIL("ProcessData: Member not found");
}

void ScDPDataDimension::FillDataRow(
    const ScDPResultDimension* pRefDim, ScDPResultFilterContext& rFilterCxt,
    uno::Sequence<sheet::DataResult>& rSequence, tools::Long nMeasure, bool bIsSubTotalRow,
    const ScDPSubTotalState& rSubState) const
{
    OUString aDimName;
    bool bDataLayout = false;
    if (pResultDimension)
    {
        aDimName = pResultDimension->GetName();
        bDataLayout = pResultDimension->IsDataLayout();
    }

    FilterStack aFilterStack(rFilterCxt.maFilters);
    aFilterStack.pushDimName(aDimName, bDataLayout);

    assert(pRefDim);
    OSL_ENSURE( static_cast<size_t>(pRefDim->GetMemberCount()) == maMembers.size(), "dimensions don't match" );
    OSL_ENSURE( pRefDim == pResultDimension, "wrong dim" );

    const ScMemberSortOrder& rMemberOrder = pRefDim->GetMemberOrder();

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nCount = maMembers.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        tools::Long nSorted = rMemberOrder.empty() ? i : rMemberOrder[i];

        tools::Long nMemberPos = nSorted;
        if (bIsDataLayout)
        {
            OSL_ENSURE(nMeasure == SC_DPMEASURE_ALL || pResultData->GetMeasureCount() == 1,
                        "DataLayout dimension twice?");
            nMemberPos = 0;
            nMemberMeasure = nSorted;
        }

        const ScDPResultMember* pRefMember = pRefDim->GetMember(nMemberPos);
        if ( pRefMember->IsVisible() )  //TODO: here or in ScDPDataMember::FillDataRow ???
        {
            const ScDPDataMember* pDataMember = maMembers[static_cast<sal_uInt16>(nMemberPos)].get();
            pDataMember->FillDataRow(pRefMember, rFilterCxt, rSequence, nMemberMeasure, bIsSubTotalRow, rSubState);
        }
    }
}

void ScDPDataDimension::UpdateDataRow( const ScDPResultDimension* pRefDim,
                                    tools::Long nMeasure, bool bIsSubTotalRow,
                                    const ScDPSubTotalState& rSubState ) const
{
    assert(pRefDim);
    OSL_ENSURE( static_cast<size_t>(pRefDim->GetMemberCount()) == maMembers.size(), "dimensions don't match" );
    OSL_ENSURE( pRefDim == pResultDimension, "wrong dim" );

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nCount = maMembers.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        tools::Long nMemberPos = i;
        if (bIsDataLayout)
        {
            OSL_ENSURE(nMeasure == SC_DPMEASURE_ALL || pResultData->GetMeasureCount() == 1,
                        "DataLayout dimension twice?");
            nMemberPos = 0;
            nMemberMeasure = i;
        }

        // Calculate must be called even if the member is not visible (for use as reference value)
        const ScDPResultMember* pRefMember = pRefDim->GetMember(nMemberPos);
        ScDPDataMember* pDataMember = maMembers[static_cast<sal_uInt16>(nMemberPos)].get();
        pDataMember->UpdateDataRow( pRefMember, nMemberMeasure, bIsSubTotalRow, rSubState );
    }
}

void ScDPDataDimension::SortMembers( ScDPResultDimension* pRefDim )
{
    tools::Long nCount = maMembers.size();

    if ( pRefDim->IsSortByData() )
    {
        // sort members

        ScMemberSortOrder& rMemberOrder = pRefDim->GetMemberOrder();
        OSL_ENSURE( rMemberOrder.empty(), "sort twice?" );
        rMemberOrder.resize( nCount );
        for (tools::Long nPos=0; nPos<nCount; nPos++)
            rMemberOrder[nPos] = nPos;

        ScDPColMembersOrder aComp( *this, pRefDim->GetSortMeasure(), pRefDim->IsSortAscending() );
        ::std::sort( rMemberOrder.begin(), rMemberOrder.end(), aComp );
    }

    // handle children

    OSL_ENSURE( pRefDim && static_cast<size_t>(pRefDim->GetMemberCount()) == maMembers.size(), "dimensions don't match" );
    OSL_ENSURE( pRefDim == pResultDimension, "wrong dim" );

    // for data layout, call only once - sorting measure is always taken from settings
    tools::Long nLoopCount = bIsDataLayout ? 1 : nCount;
    for (tools::Long i=0; i<nLoopCount; i++)
    {
        ScDPResultMember* pRefMember = pRefDim->GetMember(i);
        if ( pRefMember->IsVisible() )  //TODO: here or in ScDPDataMember ???
        {
            ScDPDataMember* pDataMember = maMembers[static_cast<sal_uInt16>(i)].get();
            pDataMember->SortMembers( pRefMember );
        }
    }
}

void ScDPDataDimension::DoAutoShow( ScDPResultDimension* pRefDim )
{
    tools::Long nCount = maMembers.size();

    // handle children first, before changing the visible state

    assert(pRefDim);
    OSL_ENSURE( static_cast<size_t>(pRefDim->GetMemberCount()) == maMembers.size(), "dimensions don't match" );
    OSL_ENSURE( pRefDim == pResultDimension, "wrong dim" );

    // for data layout, call only once - sorting measure is always taken from settings
    tools::Long nLoopCount = bIsDataLayout ? 1 : nCount;
    for (tools::Long i=0; i<nLoopCount; i++)
    {
        ScDPResultMember* pRefMember = pRefDim->GetMember(i);
        if ( pRefMember->IsVisible() )  //TODO: here or in ScDPDataMember ???
        {
            ScDPDataMember* pDataMember = maMembers[i].get();
            pDataMember->DoAutoShow( pRefMember );
        }
    }

    if ( !(pRefDim->IsAutoShow() && pRefDim->GetAutoCount() > 0 && pRefDim->GetAutoCount() < nCount) )
        return;

    // establish temporary order, hide remaining members

    ScMemberSortOrder aAutoOrder;
    aAutoOrder.resize( nCount );
    tools::Long nPos;
    for (nPos=0; nPos<nCount; nPos++)
        aAutoOrder[nPos] = nPos;

    ScDPColMembersOrder aComp( *this, pRefDim->GetAutoMeasure(), !pRefDim->IsAutoTopItems() );
    ::std::sort( aAutoOrder.begin(), aAutoOrder.end(), aComp );

    // look for equal values to the last included one

    tools::Long nIncluded = pRefDim->GetAutoCount();
    ScDPDataMember* pDataMember1 = maMembers[aAutoOrder[nIncluded - 1]].get();
    if ( !pDataMember1->IsVisible() )
        pDataMember1 = nullptr;
    bool bContinue = true;
    while ( bContinue )
    {
        bContinue = false;
        if ( nIncluded < nCount )
        {
            ScDPDataMember* pDataMember2 = maMembers[aAutoOrder[nIncluded]].get();
            if ( !pDataMember2->IsVisible() )
                pDataMember2 = nullptr;

            if ( lcl_IsEqual( pDataMember1, pDataMember2, pRefDim->GetAutoMeasure() ) )
            {
                ++nIncluded;                // include more members if values are equal
                bContinue = true;
            }
        }
    }

    // hide the remaining members

    for (nPos = nIncluded; nPos < nCount; nPos++)
    {
        ScDPResultMember* pMember = pRefDim->GetMember(aAutoOrder[nPos]);
        pMember->SetAutoHidden();
    }
}

void ScDPDataDimension::ResetResults()
{
    tools::Long nCount = maMembers.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        //  sort order doesn't matter

        tools::Long nMemberPos = bIsDataLayout ? 0 : i;
        ScDPDataMember* pDataMember = maMembers[nMemberPos].get();
        pDataMember->ResetResults();
    }
}

tools::Long ScDPDataDimension::GetSortedIndex( tools::Long nUnsorted ) const
{
    if (!pResultDimension)
       return nUnsorted;

    const ScMemberSortOrder& rMemberOrder = pResultDimension->GetMemberOrder();
    return rMemberOrder.empty() ? nUnsorted : rMemberOrder[nUnsorted];
}

void ScDPDataDimension::UpdateRunningTotals( const ScDPResultDimension* pRefDim,
                                    tools::Long nMeasure, bool bIsSubTotalRow,
                                    const ScDPSubTotalState& rSubState, ScDPRunningTotalState& rRunning,
                                    ScDPRowTotals& rTotals, const ScDPResultMember& rRowParent ) const
{
    assert(pRefDim);
    OSL_ENSURE( static_cast<size_t>(pRefDim->GetMemberCount()) == maMembers.size(), "dimensions don't match" );
    OSL_ENSURE( pRefDim == pResultDimension, "wrong dim" );

    tools::Long nMemberMeasure = nMeasure;
    tools::Long nCount = maMembers.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        const ScMemberSortOrder& rMemberOrder = pRefDim->GetMemberOrder();
        tools::Long nSorted = rMemberOrder.empty() ? i : rMemberOrder[i];

        tools::Long nMemberPos = nSorted;
        if (bIsDataLayout)
        {
            OSL_ENSURE(nMeasure == SC_DPMEASURE_ALL || pResultData->GetMeasureCount() == 1,
                        "DataLayout dimension twice?");
            nMemberPos = 0;
            nMemberMeasure = nSorted;
        }

        const ScDPResultMember* pRefMember = pRefDim->GetMember(nMemberPos);
        if ( pRefMember->IsVisible() )
        {
            if ( bIsDataLayout )
                rRunning.AddColIndex( 0, 0 );
            else
                rRunning.AddColIndex( i, nSorted );

            ScDPDataMember* pDataMember = maMembers[nMemberPos].get();
            pDataMember->UpdateRunningTotals(
                pRefMember, nMemberMeasure, bIsSubTotalRow, rSubState, rRunning, rTotals, rRowParent);

            rRunning.RemoveColIndex();
        }
    }
}

#if DUMP_PIVOT_TABLE
void ScDPDataDimension::DumpState( const ScDPResultDimension* pRefDim, ScDocument* pDoc, ScAddress& rPos ) const
{
    OUString aDimName = bIsDataLayout ? u"(data layout)"_ustr : u"(unknown)"_ustr;
    dumpRow(u"ScDPDataDimension"_ustr, aDimName, nullptr, pDoc, rPos);

    SCROW nStartRow = rPos.Row();

    tools::Long nCount = bIsDataLayout ? 1 : maMembers.size();
    for (tools::Long i=0; i<nCount; i++)
    {
        const ScDPResultMember* pRefMember = pRefDim->GetMember(i);
        const ScDPDataMember* pDataMember = maMembers[i].get();
        pDataMember->DumpState( pRefMember, pDoc, rPos );
    }

    indent(pDoc, nStartRow, rPos);
}

void ScDPDataDimension::Dump(int nIndent) const
{
    std::string aIndent(nIndent*2, ' ');
    std::cout << aIndent << "-- data dimension '"
        << (pResultDimension ? pResultDimension->GetName() : OUString()) << "'" << std::endl;
    for (auto& rxMember : maMembers)
        rxMember->Dump(nIndent+1);
}
#endif

tools::Long ScDPDataDimension::GetMemberCount() const
{
    return maMembers.size();
}

const ScDPDataMember* ScDPDataDimension::GetMember(tools::Long n) const
{
    return maMembers[n].get();
}

ScDPDataMember* ScDPDataDimension::GetMember(tools::Long n)
{
    return maMembers[n].get();
}

ScDPResultVisibilityData::ScDPResultVisibilityData(
 ScDPSource* pSource) :
    mpSource(pSource)
{
}

ScDPResultVisibilityData::~ScDPResultVisibilityData()
{
}

void ScDPResultVisibilityData::addVisibleMember(const OUString& rDimName, const ScDPItemData& rMemberItem)
{
    DimMemberType::iterator itr = maDimensions.find(rDimName);
    if (itr == maDimensions.end())
    {
        pair<DimMemberType::iterator, bool> r = maDimensions.emplace(
            rDimName, VisibleMemberType());

        if (!r.second)
            // insertion failed.
            return;

        itr = r.first;
    }
    VisibleMemberType& rMem = itr->second;
    rMem.insert(rMemberItem);
}

void ScDPResultVisibilityData::fillFieldFilters(vector<ScDPFilteredCache::Criterion>& rFilters) const
{
    typedef std::unordered_map<OUString, tools::Long> FieldNameMapType;
    FieldNameMapType aFieldNames;
    ScDPTableData* pData = mpSource->GetData();
    sal_Int32 nColumnCount = pData->GetColumnCount();
    for (sal_Int32 i = 0; i < nColumnCount; ++i)
    {
        aFieldNames.emplace(pData->getDimensionName(i), i);
    }

    const ScDPDimensions* pDims = mpSource->GetDimensionsObject();
    for (const auto& [rDimName, rMem] : maDimensions)
    {
        ScDPFilteredCache::Criterion aCri;
        FieldNameMapType::const_iterator itrField = aFieldNames.find(rDimName);
        if (itrField == aFieldNames.end())
            // This should never happen!
            continue;

        tools::Long nDimIndex = itrField->second;
        aCri.mnFieldIndex = static_cast<sal_Int32>(nDimIndex);
        aCri.mpFilter = std::make_shared<ScDPFilteredCache::GroupFilter>();

        ScDPFilteredCache::GroupFilter* pGrpFilter =
            static_cast<ScDPFilteredCache::GroupFilter*>(aCri.mpFilter.get());

        for (const ScDPItemData& rMemItem : rMem)
        {
            pGrpFilter->addMatchItem(rMemItem);
        }

        ScDPDimension* pDim = pDims->getByIndex(nDimIndex);
        ScDPMembers* pMembers = pDim->GetHierarchiesObject()->getByIndex(0)->
            GetLevelsObject()->getByIndex(0)->GetMembersObject();
        if (pGrpFilter->getMatchItemCount() < o3tl::make_unsigned(pMembers->getCount()))
            rFilters.push_back(std::move(aCri));
    }
}

size_t ScDPResultVisibilityData::MemberHash::operator() (const ScDPItemData& r) const
{
    if (r.IsValue())
        return static_cast<size_t>(::rtl::math::approxFloor(r.GetValue()));
    else
        return r.GetString().hashCode();
}
SCROW ScDPResultMember::GetDataId( ) const
{
    const ScDPMember*   pMemberDesc = GetDPMember();
    if (pMemberDesc)
        return  pMemberDesc->GetItemDataId();
    return -1;
}

ScDPResultMember* ScDPResultDimension::AddMember(const ScDPParentDimData &aData )
{
    ScDPResultMember* pMember = new ScDPResultMember( pResultData, aData );
    SCROW   nDataIndex = pMember->GetDataId();
    maMemberArray.emplace_back( pMember );

    maMemberHash.emplace( nDataIndex, pMember );
    return pMember;
}

ScDPResultMember* ScDPResultDimension::InsertMember(const ScDPParentDimData *pMemberData)
{
    SCROW  nInsert = 0;
    if ( !lcl_SearchMember( maMemberArray, pMemberData->mnOrder , nInsert ) )
    {
        ScDPResultMember* pNew = new ScDPResultMember( pResultData, *pMemberData );
        maMemberArray.emplace( maMemberArray.begin()+nInsert, pNew );

        SCROW   nDataIndex = pMemberData->mpMemberDesc->GetItemDataId();
        maMemberHash.emplace( nDataIndex, pNew );
        return pNew;
    }
    return maMemberArray[ nInsert ].get();
}

void ScDPResultDimension::InitWithMembers(
    LateInitParams& rParams, const std::vector<SCROW>& pItemData, size_t nPos,
    ScDPInitState& rInitState)
{
    if ( rParams.IsEnd( nPos ) )
        return;
    ScDPDimension* pThisDim        = rParams.GetDim( nPos );
    ScDPLevel*        pThisLevel      = rParams.GetLevel( nPos );
    SCROW             nDataID         = pItemData[nPos];

    if (!(pThisDim && pThisLevel))
        return;

    tools::Long nDimSource = pThisDim->GetDimension();     //TODO: check GetSourceDim?

    //  create all members at the first call (preserve order)
    ResultMembers& rMembers = pResultData->GetDimResultMembers(nDimSource, pThisDim, pThisLevel);
    ScDPGroupCompare aCompare( pResultData, rInitState, nDimSource );
    //  initialize only specific member (or all if "show empty" flag is set)
    ScDPResultMember* pResultMember = nullptr;
    if ( bInitialized  )
        pResultMember = FindMember( nDataID );
    else
        bInitialized = true;

    if ( pResultMember == nullptr )
    { //only insert found item
        const ScDPParentDimData* pMemberData = rMembers.FindMember( nDataID );
        if ( pMemberData && aCompare.IsIncluded( *( pMemberData->mpMemberDesc ) ) )
            pResultMember = InsertMember( pMemberData );
    }
    if ( pResultMember )
    {
        rInitState.AddMember( nDimSource, pResultMember->GetDataId()  );
        pResultMember->LateInitFrom(rParams, pItemData, nPos+1, rInitState);
        rInitState.RemoveMember();
    }
}

ScDPParentDimData::ScDPParentDimData() :
    mnOrder(-1), mpParentDim(nullptr), mpParentLevel(nullptr), mpMemberDesc(nullptr) {}

ScDPParentDimData::ScDPParentDimData(
    SCROW nIndex, const ScDPDimension* pDim, const ScDPLevel* pLev, const ScDPMember* pMember) :
    mnOrder(nIndex), mpParentDim(pDim), mpParentLevel(pLev), mpMemberDesc(pMember) {}

const ScDPParentDimData* ResultMembers::FindMember( SCROW nIndex ) const
{
    auto aRes = maMemberHash.find( nIndex );
    if( aRes != maMemberHash.end()) {
        if ( aRes->second.mpMemberDesc && aRes->second.mpMemberDesc->GetItemDataId()==nIndex )
            return &aRes->second;
    }
    return nullptr;
}
void  ResultMembers::InsertMember(  const ScDPParentDimData& rNew )
{
    if ( !rNew.mpMemberDesc->getShowDetails() )
        mbHasHideDetailsMember = true;
    maMemberHash.emplace( rNew.mpMemberDesc->GetItemDataId(), rNew );
}

ResultMembers::ResultMembers():
    mbHasHideDetailsMember( false )
{
}
ResultMembers::~ResultMembers()
{
}

LateInitParams::LateInitParams(
    const vector<ScDPDimension*>& ppDim, const vector<ScDPLevel*>& ppLev, bool bRow ) :
    mppDim( ppDim ),
    mppLev( ppLev ),
    mbRow( bRow ),
    mbInitChild( true ),
    mbAllChildren( false )
{
}

bool LateInitParams::IsEnd( size_t nPos ) const
{
    return nPos >= mppDim.size();
}

void ScDPResultDimension::CheckShowEmpty( bool bShow )
{
    tools::Long nCount = maMemberArray.size();

    for (tools::Long i=0; i<nCount; i++)
    {
        ScDPResultMember* pMember = maMemberArray.at(i).get();
        pMember->CheckShowEmpty(bShow);
    }

}

void ScDPResultMember::CheckShowEmpty( bool bShow )
{
    if (bHasElements)
    {
        ScDPResultDimension* pChildDim = GetChildDimension();
        if (pChildDim)
            pChildDim->CheckShowEmpty();
    }
    else if (IsValid() && bInitialized)
    {
        bShow = bShow || (GetParentLevel() && GetParentLevel()->getShowEmpty());
        if (bShow)
        {
            SetHasElements();
            ScDPResultDimension* pChildDim = GetChildDimension();
            if (pChildDim)
                pChildDim->CheckShowEmpty(true);
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
