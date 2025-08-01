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

#ifndef INCLUDED_UNOTOOLS_CHARCLASS_HXX
#define INCLUDED_UNOTOOLS_CHARCLASS_HXX

#include <unotools/unotoolsdllapi.h>
#include <i18nlangtag/languagetag.hxx>
#include <com/sun/star/i18n/DirectionProperty.hpp>
#include <com/sun/star/i18n/KCharacterType.hpp>
#include <com/sun/star/i18n/ParseResult.hpp>
#include <com/sun/star/i18n/UnicodeScript.hpp>
#include <com/sun/star/uno/Reference.hxx>

namespace com::sun::star::uno { class XComponentContext; }
namespace com::sun::star::i18n { class XCharacterClassification; }

inline constexpr sal_Int32 nCharClassAlphaType =
    css::i18n::KCharacterType::UPPER |
    css::i18n::KCharacterType::LOWER |
    css::i18n::KCharacterType::TITLE_CASE;

inline constexpr sal_Int32 nCharClassAlphaTypeMask =
    nCharClassAlphaType |
    css::i18n::KCharacterType::LETTER |     // Alpha is also always a LETTER
    css::i18n::KCharacterType::PRINTABLE |
    css::i18n::KCharacterType::BASE_FORM;

inline constexpr sal_Int32 nCharClassLetterType =
    nCharClassAlphaType |
    css::i18n::KCharacterType::LETTER;

inline constexpr sal_Int32 nCharClassLetterTypeMask =
    nCharClassAlphaTypeMask |
    css::i18n::KCharacterType::LETTER;

inline constexpr sal_Int32 nCharClassNumericType =
    css::i18n::KCharacterType::DIGIT;

inline constexpr sal_Int32 nCharClassNumericTypeMask =
    nCharClassNumericType |
    css::i18n::KCharacterType::PRINTABLE |
    css::i18n::KCharacterType::BASE_FORM;

inline constexpr sal_Int32 nCharClassBaseType =
    css::i18n::KCharacterType::BASE_FORM;

class UNOTOOLS_DLLPUBLIC CharClass
{
    LanguageTag                 maLanguageTag;
    css::uno::Reference< css::i18n::XCharacterClassification >    xCC;

    CharClass(const CharClass&) = delete;
    CharClass& operator=(const CharClass&) = delete;

public:
    /// Preferred ctor with service manager specified
    CharClass(
        const css::uno::Reference< css::uno::XComponentContext > & rxContext,
        LanguageTag aLanguageTag );

    /// Deprecated ctor, tries to get a process service manager or to load the
    /// library directly.
    CharClass( LanguageTag aLanguageTag );

    ~CharClass();

    /// get current Locale
    const LanguageTag& getLanguageTag() const;

    /// isdigit() on ascii values of entire string
    static bool isAsciiNumeric( std::u16string_view rStr );

    /// isalpha() on ascii values of entire string
    static bool isAsciiAlpha( std::u16string_view rStr );

    /// whether type is pure numeric or not, e.g. return of getCharacterType()
    static bool isNumericType( sal_Int32 nType )
    {
        return ((nType & nCharClassNumericType) != 0) &&
            ((nType & ~nCharClassNumericTypeMask) == 0);
    }

    /// whether type is pure alphanumeric or not, e.g. return of getCharacterType()
    static bool isAlphaNumericType( sal_Int32 nType )
    {
        return ((nType & (nCharClassAlphaType |
            nCharClassNumericType)) != 0) &&
            ((nType & ~(nCharClassAlphaTypeMask |
            nCharClassNumericTypeMask)) == 0);
    }

    /// whether type is pure letter or not, e.g. return of getCharacterType()
    static bool isLetterType( sal_Int32 nType )
    {
        return ((nType & nCharClassLetterType) != 0) &&
            ((nType & ~nCharClassLetterTypeMask) == 0);
    }

    /// whether type is pure letternumeric or not, e.g. return of getCharacterType()
    static bool isLetterNumericType( sal_Int32 nType )
    {
        return ((nType & (nCharClassLetterType |
            nCharClassNumericType)) != 0) &&
            ((nType & ~(nCharClassLetterTypeMask |
            nCharClassNumericTypeMask)) == 0);
    }

    // Wrapper implementations of class CharacterClassification

    OUString uppercase( const OUString& rStr, sal_Int32 nPos, sal_Int32 nCount ) const;
    OUString lowercase( const OUString& rStr, sal_Int32 nPos, sal_Int32 nCount ) const;
    OUString titlecase( const OUString& rStr, sal_Int32 nPos, sal_Int32 nCount ) const;

    OUString uppercase( const OUString& _rStr ) const
    {
        return uppercase(_rStr, 0, _rStr.getLength());
    }
    OUString lowercase( const OUString& _rStr ) const
    {
        return lowercase(_rStr, 0, _rStr.getLength());
    }
    OUString titlecase( const OUString& _rStr ) const
    {
        return titlecase(_rStr, 0, _rStr.getLength());
    }

    sal_Int16 getType( const OUString& rStr, sal_Int32 nPos ) const;
    css::i18n::DirectionProperty getCharacterDirection( const OUString& rStr, sal_Int32 nPos ) const;
    css::i18n::UnicodeScript getScript( const OUString& rStr, sal_Int32 nPos ) const;
    sal_Int32 getCharacterType( const OUString& rStr, sal_Int32 nPos ) const;

    css::i18n::ParseResult parseAnyToken(
                                    const OUString& rStr,
                                    sal_Int32 nPos,
                                    sal_Int32 nStartCharFlags,
                                    const OUString& userDefinedCharactersStart,
                                    sal_Int32 nContCharFlags,
                                    const OUString& userDefinedCharactersCont ) const;

    css::i18n::ParseResult parsePredefinedToken(
                                    sal_Int32 nTokenType,
                                    const OUString& rStr,
                                    sal_Int32 nPos,
                                    sal_Int32 nStartCharFlags,
                                    const OUString& userDefinedCharactersStart,
                                    sal_Int32 nContCharFlags,
                                    const OUString& userDefinedCharactersCont ) const;

    // Functionality of class International methods

    bool isAlpha( const OUString& rStr, sal_Int32 nPos ) const;
    bool isLetter( const OUString& rStr, sal_Int32 nPos ) const;
    bool isDigit( const OUString& rStr, sal_Int32 nPos ) const;
    bool isAlphaNumeric( const OUString& rStr, sal_Int32 nPos ) const;
    bool isLetterNumeric( const OUString& rStr, sal_Int32 nPos ) const;
    bool isBase( const OUString& rStr, sal_Int32 nPos ) const;
    bool isUpper( const OUString& rStr, sal_Int32 nPos ) const;
    bool isLetter( const OUString& rStr ) const;
    bool isNumeric( const OUString& rStr ) const;
    bool isLetterNumeric( const OUString& rStr ) const;

private:

    const css::lang::Locale &  getMyLocale() const;
};

#endif // INCLUDED_UNOTOOLS_CHARCLASS_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
