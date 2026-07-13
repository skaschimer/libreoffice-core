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

#pragma once

#include <sal/config.h>

#include <o3tl/sorted_vector.hxx>
#include <tools/fontenum.hxx>
#include <vcl/dllapi.h>
#include <vcl/timer.hxx>
#include <com/sun/star/lang/Locale.hpp>
#include <unx/font/fc_fontoptions.hxx>

#include <font/PhysicalFontFace.hxx>

#include <optional>
#include <set>
#include <memory>
#include <string_view>
#include <vector>
#include <unordered_map>

class FontConfigFontOptions;
namespace vcl::font
{
class FontSelectPattern;
}
class GenericUnixSalData;

namespace psp {

struct FontconfigFont
{
    FontAttributes    m_aFontAttributes;

    OString           m_aFontFile;        // system dependent path
    int               m_nCollectionEntry = 0; // 0 for regular fonts, 0 to ... for fonts stemming from collections
    int               m_nVariationEntry = 0;  // 0 for regular fonts, 0 to ... for fonts stemming from font variations
};

class VCL_PLUGIN_PUBLIC PrintFontManager
{
    std::vector<FontconfigFont> m_aSystemFonts;

    std::optional<FontconfigFont> fontFromFcPattern(FcPattern* pPattern);
    std::vector<FontconfigFont> fontsFromFontconfigFile(std::string_view rFilePath);

    void collectSystemFonts();

    /* try to initialize fonts from libfontconfig

    called from <code>initialize()</code>
    */
    static void initFontconfig();
    /* deinitialize fontconfig
     */
    static void deinitFontconfig();

    /* register an application specific font directory for libfontconfig

    since fontconfig is asked for font substitutes before OOo will check for font availability
    and fontconfig will happily substitute fonts it doesn't know (e.g. "Arial Narrow" -> "DejaVu Sans Book"!)
    it becomes necessary to tell the library about all the hidden font treasures
    */
    static void addFontconfigDir(const OString& rDirectory);

    /* register an application specific font file for libfontconfig */
    static void addFontconfigFile(const OString& rFile);

    /* deregister an application specific font file from libfontconfig */
    static void removeFontconfigFile(std::string_view aFile);

    std::set<OString> m_aPreviousLangSupportRequests;
    std::vector<OUString> m_aCurrentRequests;
    Timer m_aFontInstallerTimer;

    DECL_DLLPRIVATE_LINK( autoInstallFontLangSupport, Timer*, void );
    PrintFontManager();
public:
    ~PrintFontManager();
    friend class ::GenericUnixSalData;
    static PrintFontManager& get(); // one instance only

    std::vector<FontconfigFont> takeSystemFonts() { return std::move(m_aSystemFonts); }

    std::vector<FontconfigFont> addFontFile(const OString& rFile);

    static void removeFontFile(std::string_view rFile);

    // font administration functions

    /*  system dependent font matching

    <p>
    <code>matchFont</code> matches a pattern of font characteristics
    and returns the closest match if possible. If a match was found
    it will update rDFA to the found matching font.
    </p>
    <p>
    implementation note: currently the function is only implemented
    for fontconfig.
    </p>

    @param rDFA
    out of the FontAttributes structure the following
    fields will be used for the match:
    <ul>
    <li>family name</li>
    <li>italic</li>
    <li>width</li>
    <li>weight</li>
    <li>pitch</li>
    </ul>

    @param rLocale
    if <code>rLocal</code> contains non empty strings the corresponding
    locale will be used for font matching also; e.g. "Sans" can result
    in different fonts in e.g. english and japanese
     */
    static bool matchFont(FontAttributes& rDFA, const css::lang::Locale& rLocale);

    static std::unique_ptr<FontConfigFontOptions> getFontOptions(const FontAttributes& rFontAttributes, int nSize);

    void Substitute(vcl::font::FontSelectPattern &rPattern, OUString& rMissingCodes);

};

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
