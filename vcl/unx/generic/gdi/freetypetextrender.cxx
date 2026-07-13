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

#include <sal/config.h>

#include <unx/freetypetextrender.hxx>

#include <unotools/configmgr.hxx>
#include <vcl/settings.hxx>
#include <vcl/sysdata.hxx>
#include <vcl/svapp.hxx>
#include <vcl/fontcharmap.hxx>
#include <sal/log.hxx>

#include <unx/font/fontmanager.hxx>
#include <unx/geninst.h>
#include <unx/font/glyphcache.hxx>
#include <unx/font/fc_fontoptions.hxx>
#include <unx/font/freetype_glyphcache.hxx>
#include <font/PhysicalFontFace.hxx>
#include <font/FontMetricData.hxx>

#include <sallayout.hxx>

FreeTypeTextRenderImpl::FreeTypeTextRenderImpl()
    : mnTextColor(Color(0x00, 0x00, 0x00)) //black
{
}

FreeTypeTextRenderImpl::~FreeTypeTextRenderImpl()
{
    ReleaseFonts();
}

void FreeTypeTextRenderImpl::SetFont(LogicalFontInstance *pEntry, int nFallbackLevel)
{
    // release all no longer needed font resources
    for( int i = nFallbackLevel; i < MAX_FALLBACK; ++i )
    {
        // old server side font is no longer referenced
        mpFreetypeFont[i] = nullptr;
    }

    // return early if there is no new font
    if( !pEntry )
        return;

    FreetypeFont* pFreetypeFont = static_cast<FreetypeFont*>(pEntry);
    mpFreetypeFont[ nFallbackLevel ] = pFreetypeFont;

    // ignore fonts with e.g. corrupted font files
    if (!mpFreetypeFont[nFallbackLevel]->TestFont())
        mpFreetypeFont[nFallbackLevel] = nullptr;
}

FontCharMapRef FreeTypeTextRenderImpl::GetFontCharMap() const
{
    if (!mpFreetypeFont[0])
        return nullptr;
    return mpFreetypeFont[0]->GetFontFace()->GetFontCharMap();
}

bool FreeTypeTextRenderImpl::GetFontCapabilities(vcl::FontCapabilities &rGetImplFontCapabilities) const
{
    if (!mpFreetypeFont[0])
        return false;
    return mpFreetypeFont[0]->GetFontFace()->GetFontCapabilities(rGetImplFontCapabilities);
}

// SalGraphics
void
FreeTypeTextRenderImpl::SetTextColor( Color nColor )
{
    if( mnTextColor != nColor )
    {
        mnTextColor = nColor;
    }
}

bool FreeTypeTextRenderImpl::AddTempDevFont(vcl::font::PhysicalFontCollection* pFontCollection,
                                            const OUString& rFileURL, const OUString& rFontName)
{
    FreetypeFontList& rFontList = FreetypeFontList::get();
    if (!rFontList.AddFontFile(rFileURL, rFontName))
        return false;

    // announce new font to device's font list
    rFontList.AnnounceFonts(pFontCollection);
    return true;
}

bool FreeTypeTextRenderImpl::RemoveTempDevFont(const OUString& rFileURL, const OUString& /*rFontName*/)
{
    FreetypeFontList::get().RemoveFontFile(rFileURL);
    return true;
}

void FreeTypeTextRenderImpl::ClearDevFontCache()
{
}

void FreeTypeTextRenderImpl::GetDevFontList(vcl::font::PhysicalFontCollection* pFontCollection)
{
    FreetypeFontList::get().AnnounceFonts(pFontCollection);

    // register platform specific font substitutions if available
    SalGenericInstance::RegisterFontSubstitutors(pFontCollection);
}

void FreeTypeTextRenderImpl::GetFontMetric( FontMetricDataRef& rxFontMetric, int nFallbackLevel )
{
    if( nFallbackLevel >= MAX_FALLBACK )
        return;

    if (mpFreetypeFont[nFallbackLevel])
        mpFreetypeFont[nFallbackLevel]->GetFontMetric(rxFontMetric);
}

std::unique_ptr<GenericSalLayout> FreeTypeTextRenderImpl::GetTextLayout(int nFallbackLevel)
{
    if (!mpFreetypeFont[nFallbackLevel])
        return nullptr;
    return std::make_unique<GenericSalLayout>(*mpFreetypeFont[nFallbackLevel]);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
