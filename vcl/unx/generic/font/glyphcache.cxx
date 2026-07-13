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

#include <stdlib.h>
#include <unx/font/freetype_glyphcache.hxx>
#include <unx/font/fontmanager.hxx>
#include <unx/gendata.hxx>

#include <font/LogicalFontInstance.hxx>

#include <rtl/ustring.hxx>
#include <sal/log.hxx>

FreetypeFontList::FreetypeFontList()
{
    InitFreetype();
}

void FreetypeFontList::Init()
{
    for (const auto& rFont : psp::PrintFontManager::get().takeSystemFonts())
    {
        FontAttributes aDFA = rFont.m_aFontAttributes;
        aDFA.IncreaseQualityBy(4096);
        AddFontFace(aDFA, rFont.m_aFontFile, rFont.m_nCollectionEntry, rFont.m_nVariationEntry);
    }

    SAL_INFO("vcl.fonts", "have " << m_aFontFaceList.size() << " fonts");
}

FreetypeFontList::~FreetypeFontList()
{
    m_aFontFaceList.clear();
}

FreetypeFontList& FreetypeFontList::get()
{
    GenericUnixSalData* const pSalData(GetGenericUnixSalData());
    assert(pSalData);
    return *pSalData->GetFreetypeFontList();
}

FreetypeFontFile* FreetypeFontList::FindFontFile(const OString& rNativeFileName)
{
    // font file already known? (e.g. for ttc, synthetic, aliased fonts)
    const char* pFileName = rNativeFileName.getStr();
    FontFileList::const_iterator it = m_aFontFileList.find(pFileName);
    if (it != m_aFontFileList.end())
        return it->second.get();

    // no => create new one
    FreetypeFontFile* pFontFile = new FreetypeFontFile(rNativeFileName);
    pFileName = pFontFile->maNativeFileName.getStr();
    m_aFontFileList[pFileName].reset(pFontFile);
    return pFontFile;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
