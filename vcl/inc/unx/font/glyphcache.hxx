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

#include <memory>
#include <freetype/config/ftheader.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <rtl/ref.hxx>
#include <vcl/dllapi.h>
#include <vcl/outdev.hxx>

#include <fontattributes.hxx>
#include <font/FontMetricData.hxx>
#include <glyphid.hxx>

#include <unordered_map>

class FreetypeFontFace;
class FreetypeFontFile;
namespace vcl::font
{
class PhysicalFontCollection;
}

 /**
  * The FreetypeManager holds the known Freetype fonts.
  *
  * It maps a font id to the FreetypeFontFace for it, and keeps the mmapped font
  * files those faces read from. The faces it hands to a PhysicalFontCollection
  * are the very same objects, shared by every collection.
  *
  * The resources are:
  *   FreetypeFontFile = holds the mmapped font file, as long as it's used by any face.
  *   FreetypeFontFace = holds the FT_FaceRec_ object, as long as it's used by any FreetypeFont.
  *   FreetypeFont     = holds the FT_SizeRec_; it is the Freetype LogicalFontInstance.
  **/
class VCL_DLLPUBLIC FreetypeManager final
{
public:
    FreetypeManager(const FreetypeManager&) = delete;
    FreetypeManager& operator=(const FreetypeManager&) = delete;

    SAL_DLLPRIVATE ~FreetypeManager();

    static FreetypeManager& get();

    void                    AddFontFile(const OString& rNormalizedName,
                                int nFaceNum, int nVariantNum,
                                sal_IntPtr nFontId,
                                const FontAttributes&);
    void                    RemoveFontFile(sal_IntPtr nFontId);

    SAL_DLLPRIVATE void     AnnounceFonts( vcl::font::PhysicalFontCollection* ) const;

    void                    ClearFontCache();

private:
    // to access the constructor
    friend class GenericUnixSalData;
    SAL_DLLPRIVATE explicit FreetypeManager();

    SAL_DLLPRIVATE static void InitFreetype();
    SAL_DLLPRIVATE FreetypeFontFile* FindFontFile(const OString& rNativeFileName);

    typedef std::unordered_map<sal_IntPtr, rtl::Reference<FreetypeFontFace>> FontFaceList;
    typedef std::unordered_map<const char*, std::unique_ptr<FreetypeFontFile>, rtl::CStringHash, rtl::CStringEqual> FontFileList;

    FontFaceList            m_aFontFaceList;

    FontFileList            m_aFontFileList;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
