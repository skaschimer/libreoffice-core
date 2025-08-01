/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
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

#include <rtl/math.hxx>
#include <svl/numformat.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/string.hxx>
#include <tools/debug.hxx>

#include <vcl/builder.hxx>
#include <vcl/event.hxx>
#include <vcl/commandevent.hxx>
#include <vcl/toolkit/fmtfield.hxx>
#include <vcl/uitest/formattedfielduiobject.hxx>
#include <vcl/weldutils.hxx>

#include "FieldFormatter.hxx"

#include <svl/zformat.hxx>

#include <limits>

// hmm. No support for regular expression. Well, I always (not really :) wanted to write a finite automat
// so here comes a finite automat ...

namespace validation
{
    static void lcl_insertStopTransition( StateTransitions& _rRow )
    {
        _rRow.insert( Transition( '_', END ) );
    }

    static void lcl_insertStartExponentTransition( StateTransitions& _rRow )
    {
        _rRow.insert( Transition( 'e', EXPONENT_START ) );
    }

    static void lcl_insertSignTransitions( StateTransitions& _rRow, const State eNextState )
    {
        _rRow.insert( Transition( '-', eNextState ) );
        _rRow.insert( Transition( '+', eNextState ) );
    }

    static void lcl_insertDigitTransitions( StateTransitions& _rRow, const State eNextState )
    {
        for ( sal_Unicode aChar = '0'; aChar <= '9'; ++aChar )
            _rRow.insert( Transition( aChar, eNextState ) );
    }

    static void lcl_insertCommonPreCommaTransitions( StateTransitions& _rRow, const sal_Unicode _cThSep, const sal_Unicode _cDecSep )
    {
        // digits are allowed
        lcl_insertDigitTransitions( _rRow, DIGIT_PRE_COMMA );

        // the thousand separator is allowed
        _rRow.insert( Transition( _cThSep, DIGIT_PRE_COMMA ) );

        // a comma is allowed
        _rRow.insert( Transition( _cDecSep, DIGIT_POST_COMMA ) );
    }

    NumberValidator::NumberValidator( const sal_Unicode _cThSep, const sal_Unicode _cDecSep )
    {
        // build up our transition table

        // how to proceed from START
        {
            StateTransitions& rRow = m_aTransitions[ START ];
            rRow.insert( Transition( '_', NUM_START ) );
                // if we encounter the normalizing character, we want to proceed with the number
        }

        // how to proceed from NUM_START
        {
            StateTransitions& rRow = m_aTransitions[ NUM_START ];

            // a sign is allowed
            lcl_insertSignTransitions( rRow, DIGIT_PRE_COMMA );

            // common transitions for the two pre-comma states
            lcl_insertCommonPreCommaTransitions( rRow, _cThSep, _cDecSep );

            // the exponent may start here
            // (this would mean string like "_+e10_", but this is a valid fragment, though no valid number)
            lcl_insertStartExponentTransition( rRow );
        }

        // how to proceed from DIGIT_PRE_COMMA
        {
            StateTransitions& rRow = m_aTransitions[ DIGIT_PRE_COMMA ];

            // common transitions for the two pre-comma states
            lcl_insertCommonPreCommaTransitions( rRow, _cThSep, _cDecSep );

            // the exponent may start here
            lcl_insertStartExponentTransition( rRow );

            // the final transition indicating the end of the string
            // (if there is no comma and no post-comma, then the string may end here)
            lcl_insertStopTransition( rRow );
        }

        // how to proceed from DIGIT_POST_COMMA
        {
            StateTransitions& rRow = m_aTransitions[ DIGIT_POST_COMMA ];

            // there might be digits, which would keep the state at DIGIT_POST_COMMA
            lcl_insertDigitTransitions( rRow, DIGIT_POST_COMMA );

            // the exponent may start here
            lcl_insertStartExponentTransition( rRow );

            // the string may end here
            lcl_insertStopTransition( rRow );
        }

        // how to proceed from EXPONENT_START
        {
            StateTransitions& rRow = m_aTransitions[ EXPONENT_START ];

            // there may be a sign
            lcl_insertSignTransitions( rRow, EXPONENT_DIGIT );

            // there may be digits
            lcl_insertDigitTransitions( rRow, EXPONENT_DIGIT );

            // the string may end here
            lcl_insertStopTransition( rRow );
        }

        // how to proceed from EXPONENT_DIGIT
        {
            StateTransitions& rRow = m_aTransitions[ EXPONENT_DIGIT ];

            // there may be digits
            lcl_insertDigitTransitions( rRow, EXPONENT_DIGIT );

            // the string may end here
            lcl_insertStopTransition( rRow );
        }

        // how to proceed from END
        {
            /*StateTransitions& rRow =*/ m_aTransitions[ EXPONENT_DIGIT ];
            // no valid transition to leave this state
            // (note that we, for consistency, nevertheless want to have a row in the table)
        }
    }

    bool NumberValidator::implValidateNormalized( const OUString& _rText )
    {
        const sal_Unicode* pCheckPos = _rText.getStr();
        State eCurrentState = START;

        while ( END != eCurrentState )
        {
            // look up the transition row for the current state
            TransitionTable::const_iterator aRow = m_aTransitions.find( eCurrentState );
            DBG_ASSERT( m_aTransitions.end() != aRow,
                "NumberValidator::implValidateNormalized: invalid transition table (row not found)!" );

            if ( m_aTransitions.end() != aRow )
            {
                // look up the current character in this row
                StateTransitions::const_iterator aTransition = aRow->second.find( *pCheckPos );
                if ( aRow->second.end() != aTransition )
                {
                    // there is a valid transition for this character
                    eCurrentState = aTransition->second;
                    ++pCheckPos;
                    continue;
                }
            }

            // if we're here, there is no valid transition
            break;
        }

        DBG_ASSERT( ( END != eCurrentState ) || ( 0 == *pCheckPos ),
            "NumberValidator::implValidateNormalized: inconsistency!" );
            // if we're at END, then the string should be done, too - the string should be normalized, means ending
            // a "_" and not containing any other "_" (except at the start), and "_" is the only possibility
            // to reach the END state

        // the string is valid if and only if we reached the final state
        return ( END == eCurrentState );
    }

    bool NumberValidator::isValidNumericFragment( std::u16string_view _rText )
    {
        if ( _rText.empty() )
            // empty strings are always allowed
            return true;

        // normalize the string
        OUString sNormalized = OUString::Concat("_") + _rText + "_";

        return implValidateNormalized( sNormalized );
    }
}

SvNumberFormatter* Formatter::StaticFormatter::s_cFormatter = nullptr;
sal_uLong Formatter::StaticFormatter::s_nReferences = 0;

SvNumberFormatter* Formatter::StaticFormatter::GetFormatter()
{
    if (!s_cFormatter)
    {
        // get the Office's locale and translate
        LanguageType eSysLanguage = SvtSysLocale().GetLanguageTag().getLanguageType( false);
        s_cFormatter = new SvNumberFormatter(
            ::comphelper::getProcessComponentContext(),
            eSysLanguage);
    }
    return s_cFormatter;
}

Formatter::StaticFormatter::StaticFormatter()
{
    ++s_nReferences;
}

Formatter::StaticFormatter::~StaticFormatter()
{
    if (--s_nReferences == 0)
    {
        delete s_cFormatter;
        s_cFormatter = nullptr;
    }
}

Formatter::Formatter()
    :m_aLastSelection(0,0)
    ,m_dMinValue(0)
    ,m_dMaxValue(0)
    ,m_bHasMin(false)
    ,m_bHasMax(false)
    ,m_bWrapOnLimits(false)
    ,m_bStrictFormat(true)
    ,m_bEnableEmptyField(true)
    ,m_bAutoColor(false)
    ,m_bEnableNaN(false)
    ,m_bDisableRemainderFactor(false)
    ,m_bDefaultValueSet(false)
    ,m_ValueState(valueDirty)
    ,m_dCurrentValue(0)
    ,m_dDefaultValue(0)
    ,m_nFormatKey(0)
    ,m_pFormatter(nullptr)
    ,m_dSpinSize(1)
    ,m_dSpinFirst(-1000000)
    ,m_dSpinLast(1000000)
    ,m_bTreatAsNumber(true)
    ,m_pLastOutputColor(nullptr)
    ,m_bUseInputStringForFormatting(false)
{
}

Formatter::~Formatter()
{
}

void Formatter::SetFieldText(const OUString& rStr, const Selection& rNewSelection)
{
    SetEntryText(rStr, rNewSelection);
    m_ValueState = valueDirty;
}

void Formatter::SetTextFormatted(const OUString& rStr)
{
    SAL_INFO_IF(GetOrCreateFormatter().IsTextFormat(m_nFormatKey), "svtools",
        "FormattedField::SetTextFormatted : valid only with text formats !");

    m_sCurrentTextValue = rStr;

    OUString sFormatted;
    double dNumber = 0.0;
    // IsNumberFormat changes the format key parameter
    sal_uInt32 nTempFormatKey = static_cast< sal_uInt32 >( m_nFormatKey );
    if( IsUsingInputStringForFormatting() &&
        GetOrCreateFormatter().IsNumberFormat(m_sCurrentTextValue, nTempFormatKey, dNumber) )
    {
        sFormatted = GetOrCreateFormatter().GetInputLineString(dNumber, m_nFormatKey);
    }
    else
    {
        GetOrCreateFormatter().GetOutputString(m_sCurrentTextValue,
                                            m_nFormatKey,
                                            sFormatted,
                                            &m_pLastOutputColor);
    }

    // calculate the new selection
    Selection aSel(GetEntrySelection());
    Selection aNewSel(aSel);
    aNewSel.Normalize();
    sal_Int32 nNewLen = sFormatted.getLength();
    sal_Int32 nCurrentLen = GetEntryText().getLength();
    if ((nNewLen > nCurrentLen) && (aNewSel.Max() == nCurrentLen))
    {   // the new text is longer and the cursor was behind the last char (of the old text)
        if (aNewSel.Min() == 0)
        {   // the whole text was selected -> select the new text on the whole, too
            aNewSel.Max() = nNewLen;
            if (!nCurrentLen)
            {   // there wasn't really a previous selection (as there was no previous text), we're setting a new one -> check the selection options
                SelectionOptions nSelOptions = GetEntrySelectionOptions();
                if (nSelOptions & SelectionOptions::ShowFirst)
                {   // selection should be from right to left -> swap min and max
                    aNewSel.Min() = aNewSel.Max();
                    aNewSel.Max() = 0;
                }
            }
        }
        else if (aNewSel.Max() == aNewSel.Min())
        {   // there was no selection -> set the cursor behind the new last char
            aNewSel.Max() = nNewLen;
            aNewSel.Min() = nNewLen;
        }
    }
    else if (aNewSel.Max() > nNewLen)
        aNewSel.Max() = nNewLen;
    else
        aNewSel = aSel; // don't use the justified version
    SetEntryText(sFormatted, aNewSel);
    m_ValueState = valueString;
}

OUString const & Formatter::GetTextValue() const
{
    if (m_ValueState != valueString )
    {
        const_cast<Formatter*>(this)->m_sCurrentTextValue = GetEntryText();
        const_cast<Formatter*>(this)->m_ValueState = valueString;
    }
    return m_sCurrentTextValue;
}

void Formatter::EnableNotANumber(bool _bEnable)
{
    if ( m_bEnableNaN == _bEnable )
        return;

    m_bEnableNaN = _bEnable;
}

void Formatter::SetAutoColor(bool _bAutomatic)
{
    if (_bAutomatic == m_bAutoColor)
        return;

    m_bAutoColor = _bAutomatic;
    if (m_bAutoColor)
    {
        // if auto color is switched on, adjust the current text color, too
        SetEntryTextColor(m_pLastOutputColor);
    }
}

void Formatter::Modify(bool makeValueDirty)
{
    if (!IsStrictFormat())
    {
        if(makeValueDirty)
            m_ValueState = valueDirty;
        FieldModified();
        return;
    }

    OUString sCheck = GetEntryText();
    if (CheckText(sCheck))
    {
        m_sLastValidText = sCheck;
        m_aLastSelection = GetEntrySelection();
        if(makeValueDirty)
            m_ValueState = valueDirty;
    }
    else
    {
        ImplSetTextImpl(m_sLastValidText, &m_aLastSelection);
    }

    FieldModified();
}

void Formatter::ImplSetTextImpl(const OUString& rNew, Selection const * pNewSel)
{
    if (m_bAutoColor)
        SetEntryTextColor(m_pLastOutputColor);

    if (pNewSel)
        SetEntryText(rNew, *pNewSel);
    else
    {
        Selection aSel(GetEntrySelection());
        aSel.Normalize();

        sal_Int32 nNewLen = rNew.getLength();
        sal_Int32 nCurrentLen = GetEntryText().getLength();

        if ((nNewLen > nCurrentLen) && (aSel.Max() == nCurrentLen))
        {   // new text is longer and the cursor is behind the last char
            if (aSel.Min() == 0)
            {
                if (!nCurrentLen)
                {   // there wasn't really a previous selection (as there was no previous text)
                    aSel.Max() = 0;
                }
                else
                {   // the whole text was selected -> select the new text on the whole, too
                    aSel.Max() = nNewLen;
                }
            }
            else if (aSel.Max() == aSel.Min())
            {   // there was no selection -> set the cursor behind the new last char
                aSel.Max() = nNewLen;
                aSel.Min() = nNewLen;
            }
        }
        else if (aSel.Max() > nNewLen)
            aSel.Max() = nNewLen;
        SetEntryText(rNew, aSel);
    }

    m_ValueState = valueDirty; // not always necessary, but better re-evaluate for safety reasons
}

void Formatter::ImplSetFormatKey(sal_uLong nFormatKey)
{
    m_nFormatKey = nFormatKey;
    bool bNeedFormatter = (m_pFormatter == nullptr) && (nFormatKey != 0);
    if (bNeedFormatter)
    {
        GetOrCreateFormatter(); // this creates a standard formatter
        assert(m_pFormatter);

        // It might happen that the standard formatter makes no sense here, but it takes a default
        // format. Thus, it is possible to set one of the other standard keys (which are spanning
        // across multiple formatters).
        m_nFormatKey = nFormatKey;
        // When calling SetFormatKey without a formatter, the key must be one of the standard values
        // that is available for all formatters (and, thus, also in this new one).
        DBG_ASSERT(m_pFormatter->GetEntry(nFormatKey) != nullptr, "FormattedField::ImplSetFormatKey : invalid format key !");
    }
}

void Formatter::SetFormatKey(sal_uLong nFormatKey)
{
    bool bNoFormatter = (m_pFormatter == nullptr);
    ImplSetFormatKey(nFormatKey);
    FormatChanged((bNoFormatter && (m_pFormatter != nullptr)) ? FORMAT_CHANGE_TYPE::FORMATTER : FORMAT_CHANGE_TYPE::KEYONLY);
}

void Formatter::SetFormatter(SvNumberFormatter* pFormatter, bool bResetFormat)
{

    if (bResetFormat)
    {
        m_pFormatter = pFormatter;

        // calc the default format key from the Office's UI locale
        if ( m_pFormatter )
        {
            // get the Office's locale and translate
            LanguageType eSysLanguage = SvtSysLocale().GetLanguageTag().getLanguageType( false);
            // get the standard numeric format for this language
            m_nFormatKey = m_pFormatter->GetStandardFormat( SvNumFormatType::NUMBER, eSysLanguage );
        }
        else
            m_nFormatKey = 0;
    }
    else
    {
        LanguageType aOldLang;
        OUString sOldFormat = GetFormat(aOldLang);

        sal_uInt32 nDestKey = pFormatter->TestNewString(sOldFormat);
        if (nDestKey == NUMBERFORMAT_ENTRY_NOT_FOUND)
        {
            // language of the new formatter
            const SvNumberformat* pDefaultEntry = pFormatter->GetEntry(0);
            LanguageType aNewLang = pDefaultEntry ? pDefaultEntry->GetLanguage() : LANGUAGE_DONTKNOW;

            // convert the old format string into the new language
            sal_Int32 nCheckPos;
            SvNumFormatType nType;
            pFormatter->PutandConvertEntry(sOldFormat, nCheckPos, nType, nDestKey, aOldLang, aNewLang, true);
            m_nFormatKey = nDestKey;
        }
        m_pFormatter = pFormatter;
    }

    FormatChanged(FORMAT_CHANGE_TYPE::FORMATTER);
}

OUString Formatter::GetFormat(LanguageType& eLang) const
{
    const SvNumberformat* pFormatEntry = GetOrCreateFormatter().GetEntry(m_nFormatKey);
    DBG_ASSERT(pFormatEntry != nullptr, "FormattedField::GetFormat: no number format for the given format key.");
    OUString sFormatString = pFormatEntry ? pFormatEntry->GetFormatstring() : OUString();
    eLang = pFormatEntry ? pFormatEntry->GetLanguage() : LANGUAGE_DONTKNOW;

    return sFormatString;
}

bool Formatter::SetFormat(const OUString& rFormatString, LanguageType eLang)
{
    sal_uInt32 nNewKey = GetOrCreateFormatter().TestNewString(rFormatString, eLang);
    if (nNewKey == NUMBERFORMAT_ENTRY_NOT_FOUND)
    {
        sal_Int32 nCheckPos;
        SvNumFormatType nType;
        OUString rFormat(rFormatString);
        if (!GetOrCreateFormatter().PutEntry(rFormat, nCheckPos, nType, nNewKey, eLang))
            return false;
        DBG_ASSERT(nNewKey != NUMBERFORMAT_ENTRY_NOT_FOUND, "FormattedField::SetFormatString : PutEntry returned an invalid key !");
    }

    if (nNewKey != m_nFormatKey)
        SetFormatKey(nNewKey);
    return true;
}

OUString Formatter::FormatValue(double fValue)
{
    if (m_aFormatValueHdl.IsSet())
    {
        std::optional<OUString> aText = m_aFormatValueHdl.Call(fValue);
        if (aText.has_value())
            return aText.value();
    }

    OUString sNewText;
    if (GetOrCreateFormatter().IsTextFormat(m_nFormatKey))
    {
        // first convert the number as string in standard format
        OUString sTemp;
        GetOrCreateFormatter().GetOutputString(fValue, 0, sTemp, &m_pLastOutputColor);
        // then encode the string in the corresponding text format
        GetOrCreateFormatter().GetOutputString(sTemp, m_nFormatKey, sNewText, &m_pLastOutputColor);
    }
    else
    {
        if( IsUsingInputStringForFormatting())
        {
            sNewText = GetOrCreateFormatter().GetInputLineString(fValue, m_nFormatKey);
        }
        else
        {
            GetOrCreateFormatter().GetOutputString(fValue, m_nFormatKey, sNewText, &m_pLastOutputColor);
        }
    }

    return sNewText;
}

bool Formatter::GetThousandsSep() const
{
    DBG_ASSERT(!GetOrCreateFormatter().IsTextFormat(m_nFormatKey),
        "FormattedField::GetThousandsSep : Are you sure what you are doing when setting the precision of a text format?");

    bool bThousand, IsRed;
    sal_uInt16 nPrecision, nLeadingCnt;
    GetOrCreateFormatter().GetFormatSpecialInfo(m_nFormatKey, bThousand, IsRed, nPrecision, nLeadingCnt);

    return bThousand;
}

void Formatter::SetThousandsSep(bool _bUseSeparator)
{
    DBG_ASSERT(!GetOrCreateFormatter().IsTextFormat(m_nFormatKey),
        "FormattedField::SetThousandsSep : Are you sure what you are doing when setting the precision of a text format?");

    // get the current settings
    bool bThousand, IsRed;
    sal_uInt16 nPrecision, nLeadingCnt;
    GetOrCreateFormatter().GetFormatSpecialInfo(m_nFormatKey, bThousand, IsRed, nPrecision, nLeadingCnt);
    if (bThousand == _bUseSeparator)
        return;

    // we need the language for the following
    LanguageType eLang;
    GetFormat(eLang);

    // generate a new format ...
    OUString sFmtDescription = GetOrCreateFormatter().GenerateFormat(m_nFormatKey, eLang, _bUseSeparator, IsRed, nPrecision, nLeadingCnt);
    // ... and introduce it to the formatter
    sal_Int32 nCheckPos = 0;
    sal_uInt32 nNewKey;
    SvNumFormatType nType;
    GetOrCreateFormatter().PutEntry(sFmtDescription, nCheckPos, nType, nNewKey, eLang);

    // set the new key
    ImplSetFormatKey(nNewKey);
    FormatChanged(FORMAT_CHANGE_TYPE::THOUSANDSSEP);
}

sal_uInt16 Formatter::GetDecimalDigits() const
{
    DBG_ASSERT(!GetOrCreateFormatter().IsTextFormat(m_nFormatKey),
        "FormattedField::GetDecimalDigits : Are you sure what you are doing when setting the precision of a text format?");

    bool bThousand, IsRed;
    sal_uInt16 nPrecision, nLeadingCnt;
    GetOrCreateFormatter().GetFormatSpecialInfo(m_nFormatKey, bThousand, IsRed, nPrecision, nLeadingCnt);

    return nPrecision;
}

void Formatter::SetDecimalDigits(sal_uInt16 _nPrecision)
{
    DBG_ASSERT(!GetOrCreateFormatter().IsTextFormat(m_nFormatKey),
        "FormattedField::SetDecimalDigits : Are you sure what you are doing when setting the precision of a text format?");

    // get the current settings
    bool bThousand, IsRed;
    sal_uInt16 nPrecision, nLeadingCnt;
    GetOrCreateFormatter().GetFormatSpecialInfo(m_nFormatKey, bThousand, IsRed, nPrecision, nLeadingCnt);
    if (nPrecision == _nPrecision)
        return;

    // we need the language for the following
    LanguageType eLang;
    GetFormat(eLang);

    // generate a new format ...
    OUString sFmtDescription = GetOrCreateFormatter().GenerateFormat(m_nFormatKey, eLang, bThousand, IsRed, _nPrecision, nLeadingCnt);
    // ... and introduce it to the formatter
    sal_Int32 nCheckPos = 0;
    sal_uInt32 nNewKey;
    SvNumFormatType nType;
    GetOrCreateFormatter().PutEntry(sFmtDescription, nCheckPos, nType, nNewKey, eLang);

    // set the new key
    ImplSetFormatKey(nNewKey);
    FormatChanged(FORMAT_CHANGE_TYPE::PRECISION);
}

void Formatter::FormatChanged(FORMAT_CHANGE_TYPE _nWhat)
{
    m_pLastOutputColor = nullptr;

    if ( (_nWhat == FORMAT_CHANGE_TYPE::FORMATTER) && m_pFormatter )
        m_pFormatter->SetEvalDateFormat( NfEvalDateFormat::FormatThenInternational );

    ReFormat();
}

void Formatter::EntryLostFocus()
{
    // special treatment for empty texts
    if (GetEntryText().isEmpty())
    {
        if (!IsEmptyFieldEnabled())
        {
            if (TreatingAsNumber())
            {
                ImplSetValue(m_dCurrentValue, true);
                Modify();
                m_ValueState = valueDouble;
            }
            else
            {
                OUString sNew = GetTextValue();
                if (!sNew.isEmpty())
                    SetTextFormatted(sNew);
                else
                    SetTextFormatted(m_sDefaultText);
                m_ValueState = valueString;
            }
        }
    }
    else
    {
        Commit();
    }
}

void Formatter::Commit()
{
    // remember the old text
    OUString sOld(GetEntryText());

    // do the reformat
    ReFormat();

    // did the text change?
    if (GetEntryText() != sOld)
    {   // consider the field as modified,
        // but we already have the most recent value;
        // don't reparse it from the text
        // (can lead to data loss when the format is lossy,
        //  as is e.g. our default date format: 2-digit year!)
        Modify(false);
    }
}

void Formatter::ReFormat()
{
    if (!IsEmptyFieldEnabled() || !GetEntryText().isEmpty())
    {
        if (TreatingAsNumber())
        {
            double dValue = GetValue();
            if ( m_bEnableNaN && std::isnan( dValue ) )
                return;
            ImplSetValue( dValue, true );
        }
        else
            SetTextFormatted(GetTextValue());
    }
}

void Formatter::SetMinValue(double dMin)
{
    DBG_ASSERT(m_bTreatAsNumber, "FormattedField::SetMinValue : only to be used in numeric mode !");

    m_dMinValue = dMin;
    m_bHasMin = true;
    // for checking the current value at the new border -> ImplSetValue
    ReFormat();
}

void Formatter::SetMaxValue(double dMax)
{
    DBG_ASSERT(m_bTreatAsNumber, "FormattedField::SetMaxValue : only to be used in numeric mode !");

    m_dMaxValue = dMax;
    m_bHasMax = true;
    // for checking the current value at the new border -> ImplSetValue
    ReFormat();
}

void Formatter::SetTextValue(const OUString& rText)
{
    SetFieldText(rText, Selection(0, 0));
    ReFormat();
}

void Formatter::EnableEmptyField(bool bEnable)
{
    if (bEnable == m_bEnableEmptyField)
        return;

    m_bEnableEmptyField = bEnable;
    if (!m_bEnableEmptyField && GetEntryText().isEmpty())
        ImplSetValue(m_dCurrentValue, true);
}

void Formatter::ImplSetValue(double dVal, bool bForce)
{
    if (m_bHasMin && (dVal<m_dMinValue))
    {
        dVal = m_bWrapOnLimits ? fmod(dVal + m_dMaxValue + 1 - m_dMinValue, m_dMaxValue + 1) + m_dMinValue
                               : m_dMinValue;
    }
    if (m_bHasMax && (dVal>m_dMaxValue))
    {
        dVal = m_bWrapOnLimits ? fmod(dVal - m_dMinValue, m_dMaxValue + 1) + m_dMinValue
                               : m_dMaxValue;
    }
    if (!bForce && (dVal == GetValue()))
        return;

    m_ValueState = valueDouble;
    UpdateCurrentValue(dVal);

    const OUString sNewText = FormatValue(dVal);
    ImplSetTextImpl(sNewText, nullptr);
    DBG_ASSERT(CheckText(sNewText), "FormattedField::ImplSetValue : formatted string doesn't match the criteria !");

    m_ValueState = valueDouble;
}

std::optional<double> Formatter::ParseText(const OUString& rText)
{
    double fValue = 0.0;
    bool bUseExternalFormatterValue = false;
    if (m_aParseTextHdl.IsSet())
    {
        ParseResult aResult = m_aParseTextHdl.Call(rText);
        bUseExternalFormatterValue = aResult.m_eState != TRISTATE_INDET;
        if (bUseExternalFormatterValue)
        {
            if (aResult.m_eState == TRISTATE_TRUE)
                fValue = aResult.m_fValue;
            else
                fValue = m_dCurrentValue;
        }
    }

    if (!bUseExternalFormatterValue)
    {
        sal_uInt32 nFormatKey = m_nFormatKey; // IsNumberFormat changes the FormatKey!

        if (GetOrCreateFormatter().IsTextFormat(nFormatKey) && m_bTreatAsNumber)
            // for detection of values like "1,1" in fields that are formatted as text
            nFormatKey = 0;

        // special treatment for percentage formatting
        OUString sText = rText;
        if (GetOrCreateFormatter().GetType(m_nFormatKey) == SvNumFormatType::PERCENT)
        {
            // the language of our format
            const SvNumberformat* pFormatEntry = m_pFormatter->GetEntry(m_nFormatKey);
            assert(pFormatEntry && "due to GetType");
            LanguageType eLanguage = pFormatEntry->GetLanguage();
            // the default number format for this language
            sal_uLong nStandardNumericFormat = m_pFormatter->GetStandardFormat(SvNumFormatType::NUMBER, eLanguage);

            sal_uInt32 nTempFormat = nStandardNumericFormat;
            double dTemp;
            if (m_pFormatter->IsNumberFormat(sText, nTempFormat, dTemp) &&
                SvNumFormatType::NUMBER == m_pFormatter->GetType(nTempFormat))
                // the string is equivalent to a number formatted one (has no % sign) -> append it
                sText += "%";
            // (with this, an input of '3' becomes '3%', which then by the formatter is translated
            // into 0.03. Without this, the formatter would give us the double 3 for an input '3',
            // which equals 300 percent.
        }
        if (!GetOrCreateFormatter().IsNumberFormat(sText, nFormatKey, fValue))
            return std::optional<double>();
    }

    if (m_bHasMin && (fValue < m_dMinValue))
        fValue = m_dMinValue;
    if (m_bHasMax && (fValue > m_dMaxValue))
        fValue = m_dMaxValue;
    return std::optional<double>(fValue);
}

bool Formatter::ImplGetValue(double& dNewVal)
{
    dNewVal = m_dCurrentValue;
    if (m_ValueState == valueDouble)
        return true;

    // tdf#155241 default to m_dDefaultValue only if explicitly set
    // otherwise default to m_dCurrentValue
    if (m_bDefaultValueSet)
        dNewVal = m_dDefaultValue;

    OUString sText(GetEntryText());
    if (sText.isEmpty())
        return true;

    std::optional<double> aValue = ParseText(sText);
    if (!aValue.has_value())
        return false;

    dNewVal = aValue.value();
    return true;
}

void Formatter::SetValue(double dVal)
{
    ImplSetValue(dVal, m_ValueState != valueDouble);
}

double Formatter::GetValue()
{
    if ( !ImplGetValue( m_dCurrentValue ) )
        UpdateCurrentValue(m_bEnableNaN ? std::numeric_limits<double>::quiet_NaN() : m_dDefaultValue);

    m_ValueState = valueDouble;
    return m_dCurrentValue;
}

void Formatter::DisableRemainderFactor()
{
    m_bDisableRemainderFactor = true;
}

void Formatter::UseInputStringForFormatting()
{
    m_bUseInputStringForFormatting = true;
}

namespace
{
    class FieldFormatter : public Formatter
    {
    private:
        FormattedField& m_rSpinButton;
    public:
        FieldFormatter(FormattedField& rSpinButton)
            : m_rSpinButton(rSpinButton)
        {
        }

        // Formatter overrides
        virtual Selection GetEntrySelection() const override
        {
            return m_rSpinButton.GetSelection();
        }

        virtual OUString GetEntryText() const override
        {
            return m_rSpinButton.GetText();
        }

        void SetEntryText(const OUString& rText, const Selection& rSel) override
        {
            m_rSpinButton.SpinField::SetText(rText, rSel);
        }

        virtual void SetEntryTextColor(const ::Color* pColor) override
        {
            if (pColor)
                m_rSpinButton.SetControlForeground(*pColor);
            else
                m_rSpinButton.SetControlForeground();
        }

        virtual SelectionOptions GetEntrySelectionOptions() const override
        {
            return m_rSpinButton.GetSettings().GetStyleSettings().GetSelectionOptions();
        }

        virtual void FieldModified() override
        {
            m_rSpinButton.SpinField::Modify();
        }

        virtual void UpdateCurrentValue(double dCurrentValue) override
        {
            Formatter::UpdateCurrentValue(dCurrentValue);
            m_rSpinButton.SetUpperEnabled(!m_bHasMax || dCurrentValue < m_dMaxValue);
            m_rSpinButton.SetLowerEnabled(!m_bHasMin || dCurrentValue > m_dMinValue);
        }
    };

    class DoubleNumericFormatter : public FieldFormatter
    {
    private:
        DoubleNumericField& m_rNumericSpinButton;
    public:
        DoubleNumericFormatter(DoubleNumericField& rNumericSpinButton)
            : FieldFormatter(rNumericSpinButton)
            , m_rNumericSpinButton(rNumericSpinButton)
        {
        }

        virtual bool CheckText(const OUString& sText) const override
        {
            // We'd like to implement this using the NumberFormatter::IsNumberFormat, but unfortunately, this doesn't
            // recognize fragments of numbers (like, for instance "1e", which happens during entering e.g. "1e10")
            // Thus, the roundabout way via a regular expression
            return m_rNumericSpinButton.GetNumberValidator().isValidNumericFragment(sText);
        }

        virtual void FormatChanged(FORMAT_CHANGE_TYPE nWhat) override
        {
            m_rNumericSpinButton.ResetConformanceTester();
            FieldFormatter::FormatChanged(nWhat);
        }
    };

    class DoubleCurrencyFormatter : public FieldFormatter
    {
    private:
        DoubleCurrencyField& m_rCurrencySpinButton;
        bool m_bChangingFormat;
    public:
        DoubleCurrencyFormatter(DoubleCurrencyField& rNumericSpinButton)
            : FieldFormatter(rNumericSpinButton)
            , m_rCurrencySpinButton(rNumericSpinButton)
            , m_bChangingFormat(false)
        {
        }

        virtual void FormatChanged(FORMAT_CHANGE_TYPE nWhat) override
        {
            if (m_bChangingFormat)
            {
                FieldFormatter::FormatChanged(nWhat);
                return;
            }

            switch (nWhat)
            {
                case FORMAT_CHANGE_TYPE::FORMATTER:
                case FORMAT_CHANGE_TYPE::PRECISION:
                case FORMAT_CHANGE_TYPE::THOUSANDSSEP:
                    // the aspects which changed don't take our currency settings into account (in fact, they most probably
                    // destroyed them)
                    m_rCurrencySpinButton.UpdateCurrencyFormat();
                    break;
                case FORMAT_CHANGE_TYPE::KEYONLY:
                    OSL_FAIL("DoubleCurrencyField::FormatChanged : somebody modified my key !");
                    // We always build our own format from the settings we get via special methods (setCurrencySymbol etc.).
                    // Nobody but ourself should modify the format key directly!
                    break;
                default: break;
            }

            FieldFormatter::FormatChanged(nWhat);
        }

        void GuardSetFormat(const OUString& rString, LanguageType eLanguage)
        {
            // set this new basic format
            m_bChangingFormat = true;
            SetFormat(rString, eLanguage);
            m_bChangingFormat = false;
        }

    };
}

DoubleNumericField::DoubleNumericField(vcl::Window* pParent, WinBits nStyle)
    : FormattedField(pParent, nStyle)
{
    m_xOwnFormatter.reset(new DoubleNumericFormatter(*this));
    m_pFormatter = m_xOwnFormatter.get();
    ResetConformanceTester();
}

DoubleNumericField::~DoubleNumericField() = default;

void DoubleNumericField::ResetConformanceTester()
{
    // the thousands and the decimal separator are language dependent
    Formatter& rFormatter = GetFormatter();
    const SvNumberformat* pFormatEntry = rFormatter.GetOrCreateFormatter().GetEntry(rFormatter.GetFormatKey());

    sal_Unicode cSeparatorThousand = ',';
    sal_Unicode cSeparatorDecimal = '.';
    if (pFormatEntry)
    {
        LocaleDataWrapper aLocaleInfo( LanguageTag( pFormatEntry->GetLanguage()) );

        OUString sSeparator = aLocaleInfo.getNumThousandSep();
        if (!sSeparator.isEmpty())
            cSeparatorThousand = sSeparator[0];

        sSeparator = aLocaleInfo.getNumDecimalSep();
        if (!sSeparator.isEmpty())
            cSeparatorDecimal = sSeparator[0];
    }

    m_pNumberValidator.reset(new validation::NumberValidator( cSeparatorThousand, cSeparatorDecimal ));
}


DoubleCurrencyField::DoubleCurrencyField(vcl::Window* pParent, WinBits nStyle)
    :FormattedField(pParent, nStyle)
{
    m_xOwnFormatter.reset(new DoubleCurrencyFormatter(*this));
    m_pFormatter = m_xOwnFormatter.get();

    m_bPrependCurrSym = false;

    // initialize with a system currency format
    m_sCurrencySymbol = SvtSysLocale().GetLocaleData().getCurrSymbol();
    UpdateCurrencyFormat();
}

void DoubleCurrencyField::setCurrencySymbol(const OUString& rSymbol)
{
    if (m_sCurrencySymbol == rSymbol)
        return;

    m_sCurrencySymbol = rSymbol;
    UpdateCurrencyFormat();
    m_pFormatter->FormatChanged(FORMAT_CHANGE_TYPE::CURRENCY_SYMBOL);
}

void DoubleCurrencyField::setPrependCurrSym(bool _bPrepend)
{
    if (m_bPrependCurrSym == _bPrepend)
         return;

    m_bPrependCurrSym = _bPrepend;
    UpdateCurrencyFormat();
    m_pFormatter->FormatChanged(FORMAT_CHANGE_TYPE::CURRSYM_POSITION);
}

void DoubleCurrencyField::UpdateCurrencyFormat()
{
    // the old settings
    LanguageType eLanguage;
    m_pFormatter->GetFormat(eLanguage);
    bool bThSep = m_pFormatter->GetThousandsSep();
    sal_uInt16 nDigits = m_pFormatter->GetDecimalDigits();

    // build a new format string with the base class' and my own settings

    /* Strangely with gcc 4.6.3 this needs a temporary LanguageTag, otherwise
     * there's
     * error: request for member 'getNumThousandSep' in 'aLocaleInfo', which is
     * of non-class type 'LocaleDataWrapper(LanguageTag)' */
    LocaleDataWrapper aLocaleInfo(( LanguageTag(eLanguage) ));

    OUStringBuffer sNewFormat;
    if (bThSep)
    {
        sNewFormat.append("#" + aLocaleInfo.getNumThousandSep() + "##0");
    }
    else
        sNewFormat.append('0');

    if (nDigits)
    {
        sNewFormat.append(aLocaleInfo.getNumDecimalSep());
        comphelper::string::padToLength(sNewFormat, sNewFormat.getLength() + nDigits, '0');
    }

    if (getPrependCurrSym())
    {
        OUString sSymbol = getCurrencySymbol();
        sSymbol = comphelper::string::strip(sSymbol, ' ');

        OUString sTemp =
            "[$" + sSymbol + "] "
            + sNewFormat
        // for negative values : $ -0.00, not -$ 0.00...
        // (the real solution would be a possibility to choose a "positive currency format" and a "negative currency format"...
        // But not now... (and hey, you could take a formatted field for this...))
        // FS - 31.03.00 74642
            + ";[$"
            + sSymbol
            + "] -"
            + sNewFormat;

        sNewFormat = sTemp;
    }
    else
    {
        OUString sTemp = getCurrencySymbol();
        sTemp = comphelper::string::strip(sTemp, ' ');

        sNewFormat.append(" [$" + sTemp + "]");
    }

    // set this new basic format
    static_cast<DoubleCurrencyFormatter*>(m_pFormatter)->GuardSetFormat(sNewFormat.makeStringAndClear(), eLanguage);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
