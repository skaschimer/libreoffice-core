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

#include <sal/types.h>
#include <config_folders.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string.h>
#include <string_view>
#include <win/svsys.h>
#include <vector>

#include <d2d1.h>
#include <dwrite_3.h>
#include <basegfx/matrix/b2dhommatrixtools.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <i18nlangtag/mslangid.hxx>
#include <osl/diagnose.h>
#include <osl/file.hxx>
#include <osl/process.h>
#include <rtl/bootstrap.hxx>
#include <rtl/tencinfo.h>
#include <sal/log.hxx>
#include <o3tl/char16_t2wchar_t.hxx>
#include <tools/helpers.hxx>
#include <tools/stream.hxx>
#include <tools/urlobj.hxx>
#include <unotools/fontcfg.hxx>
#include <unotools/fontdefs.hxx>
#include <vcl/settings.hxx>
#include <vcl/sysdata.hxx>
#include <vcl/metric.hxx>
#include <vcl/fontcharmap.hxx>
#include <comphelper/windowserrorstring.hxx>

#include <font/FontSelectPattern.hxx>
#include <font/PhysicalFontCollection.hxx>
#include <font/PhysicalFontFaceCollection.hxx>
#include <font/PhysicalFontFace.hxx>
#include <font/fontsubstitution.hxx>
#include <font/TrueTypeFont.hxx>
#include <win/WindowsInstance.hxx>
#include <win/saldata.hxx>
#include <win/salgdi.h>
#include <win/winlayout.hxx>
#include <win/wingdiimpl.hxx>
#include <impfontcharmap.hxx>
#include <font/FontMetricData.hxx>
#include <impglyphitem.hxx>

#include <vcl/skia/SkiaHelper.hxx>
#include <skia/win/font.hxx>

using namespace vcl;

// platform specific font substitution hooks for glyph fallback enhancement

namespace {

class WinPreMatchFontSubstititution
:    public vcl::font::PreMatchFontSubstitution
{
public:
    bool FindFontSubstitute(vcl::font::FontSelectPattern&) const override;
};

class WinGlyphFallbackSubstititution
:    public vcl::font::GlyphFallbackFontSubstitution
{
public:
    bool FindFontSubstitute(vcl::font::FontSelectPattern&, LogicalFontInstance* pLogicalFont, OUString& rMissingChars) const override;
};

// does a font face hold the given missing characters?
bool HasMissingChars(vcl::font::PhysicalFontFace* pFace, OUString& rMissingChars)
{
    FontCharMapRef xFontCharMap = pFace->GetFontCharMap();

    // avoid fonts with unknown CMAP subtables for glyph fallback
    if( !xFontCharMap.is() || xFontCharMap->IsDefaultMap() )
        return false;

    int nMatchCount = 0;
    std::vector<sal_UCS4> rRemainingCodes;
    const sal_Int32 nStrLen = rMissingChars.getLength();
    sal_Int32 nStrIdx = 0;
    while (nStrIdx < nStrLen)
    {
        const sal_UCS4 uChar = rMissingChars.iterateCodePoints( &nStrIdx );
        if (xFontCharMap->HasChar(uChar))
            nMatchCount++;
        else
            rRemainingCodes.push_back(uChar);
    }

    xFontCharMap = nullptr;

    if (nMatchCount > 0)
        rMissingChars = OUString(rRemainingCodes.data(), rRemainingCodes.size());

    return nMatchCount > 0;
}

    //used by 2-level font fallback
    vcl::font::PhysicalFontFamily* findDevFontListByLocale(const vcl::font::PhysicalFontCollection &rFontCollection,
                                                const LanguageTag& rLanguageTag )
    {
        // get the default font for a specified locale
        const utl::DefaultFontConfiguration& rDefaults = utl::DefaultFontConfiguration::get();
        const OUString aDefault = rDefaults.getUserInterfaceFont(rLanguageTag);
        return rFontCollection.FindFontFamilyByTokenNames(aDefault);
    }
}

// These are Win 3.1 bitmap fonts using "FON" font format
// which is not supported with DirectWrite so let's substitute them
// with a font that is supported and always available.
// Based on:
// https://dxr.mozilla.org/mozilla-esr10/source/gfx/thebes/gfxDWriteFontList.cpp#1057
const std::map<OUString, OUString> aBitmapFontSubs =
{
    { "MS Sans Serif", "Microsoft Sans Serif" },
    { "MS Serif",      "Times New Roman" },
    { "Small Fonts",   "Arial" },
    { "Courier",       "Courier New" },
    { "Roman",         "Times New Roman" },
    { "Script",        "Mistral" }
};

// TODO: See if Windows have API that we can use here to improve font fallback.
bool WinPreMatchFontSubstititution::FindFontSubstitute(vcl::font::FontSelectPattern& rFontSelData) const
{
    if (rFontSelData.IsMicrosoftSymbolEncoded() || IsOpenSymbol(rFontSelData.maSearchName))
        return false;

    for (const auto& aSub : aBitmapFontSubs)
    {
        if (rFontSelData.maSearchName == GetEnglishSearchFontName(aSub.first))
        {
            rFontSelData.maSearchName = aSub.second;
            return true;
        }
    }

    return false;
}

// find a fallback font for missing characters
// TODO: should stylistic matches be searched and preferred?
bool WinGlyphFallbackSubstititution::FindFontSubstitute(vcl::font::FontSelectPattern& rFontSelData, LogicalFontInstance* /*pLogicalFont*/, OUString& rMissingChars) const
{
    // guess a locale matching to the missing chars
    LanguageType eLang = rFontSelData.meLanguage;
    LanguageTag aLanguageTag( eLang);

    // fall back to default UI locale if the font language is inconclusive
    if( eLang == LANGUAGE_DONTKNOW )
        aLanguageTag = Application::GetSettings().GetUILanguageTag();

    // first level fallback:
    // try use the locale specific default fonts defined in VCL.xcu
    const vcl::font::PhysicalFontCollection* pFontCollection = ImplGetSVData()->maGDIData.mxScreenFontList.get();
    vcl::font::PhysicalFontFamily* pFontFamily = findDevFontListByLocale(*pFontCollection, aLanguageTag);
    if( pFontFamily )
    {
        vcl::font::PhysicalFontFace* pFace = pFontFamily->FindBestFontFace( rFontSelData );
        if( HasMissingChars( pFace, rMissingChars ) )
        {
            rFontSelData.maSearchName = pFontFamily->GetSearchName();
            return true;
        }
    }

    // are the missing characters symbols?
    pFontFamily = pFontCollection->FindFontFamilyByAttributes( ImplFontAttrs::Symbol,
                                                     rFontSelData.GetWeight(),
                                                     rFontSelData.GetWidthType(),
                                                     rFontSelData.GetItalic(),
                                                     rFontSelData.maSearchName );
    if( pFontFamily )
    {
        vcl::font::PhysicalFontFace* pFace = pFontFamily->FindBestFontFace( rFontSelData );
        if( HasMissingChars( pFace, rMissingChars ) )
        {
            rFontSelData.maSearchName = pFontFamily->GetSearchName();
            return true;
        }
    }

    // last level fallback, check each font type face one by one
    std::unique_ptr<vcl::font::PhysicalFontFaceCollection> pTestFontList = pFontCollection->GetFontFaceCollection();
    // limit the count of fonts to be checked to prevent hangs
    static const int MAX_GFBFONT_COUNT = 600;
    int nTestFontCount = pTestFontList->Count();
    if( nTestFontCount > MAX_GFBFONT_COUNT )
        nTestFontCount = MAX_GFBFONT_COUNT;

    bool bFound = false;
    for( int i = 0; i < nTestFontCount; ++i )
    {
        vcl::font::PhysicalFontFace* pFace = pTestFontList->Get( i );
        bFound = HasMissingChars( pFace, rMissingChars );
        if( !bFound )
            continue;
        rFontSelData.maSearchName = pFace->GetFamilyName();
        break;
    }

    return bFound;
}

static rtl_TextEncoding ImplCharSetToSal( BYTE nCharSet )
{
    rtl_TextEncoding eTextEncoding;

    if ( nCharSet == OEM_CHARSET )
    {
        UINT nCP = static_cast<sal_uInt16>(GetOEMCP());
        switch ( nCP )
        {
            // It is unclear why these two (undefined?) code page numbers are
            // handled specially here. They are mentioned briefly in
            // https://github.com/osfree-project/osfree/blob/165b89a685030c2b875aa0c7c5db420afc8db5f6/docs/dos/dos.txt
            case 1004:  eTextEncoding = RTL_TEXTENCODING_MS_1252; break;
            case 65400: eTextEncoding = RTL_TEXTENCODING_SYMBOL; break;
            default:
                eTextEncoding = rtl_getTextEncodingFromWindowsCodePage(nCP);
                break;
        }
    }
    else
    {
        if( nCharSet )
            eTextEncoding = rtl_getTextEncodingFromWindowsCharset( nCharSet );
        else
            eTextEncoding = RTL_TEXTENCODING_UNICODE;
    }

    return eTextEncoding;
}

static FontFamily ImplFamilyToSal( BYTE nFamily )
{
    switch ( nFamily & 0xF0 )
    {
        case FF_DECORATIVE:
            return FAMILY_DECORATIVE;

        case FF_MODERN:
            return FAMILY_MODERN;

        case FF_ROMAN:
            return FAMILY_ROMAN;

        case FF_SCRIPT:
            return FAMILY_SCRIPT;

        case FF_SWISS:
            return FAMILY_SWISS;

        default:
            break;
    }

    return FAMILY_DONTKNOW;
}

static FontWeight ImplWeightToSal( int nWeight )
{
    if ( nWeight <= FW_THIN )
        return WEIGHT_THIN;
    else if ( nWeight <= FW_ULTRALIGHT )
        return WEIGHT_ULTRALIGHT;
    else if ( nWeight <= FW_LIGHT )
        return WEIGHT_LIGHT;
    else if ( nWeight < FW_MEDIUM )
        return WEIGHT_NORMAL;
    else if ( nWeight == FW_MEDIUM )
        return WEIGHT_MEDIUM;
    else if ( nWeight <= FW_SEMIBOLD )
        return WEIGHT_SEMIBOLD;
    else if ( nWeight <= FW_BOLD )
        return WEIGHT_BOLD;
    else if ( nWeight <= FW_ULTRABOLD )
        return WEIGHT_ULTRABOLD;
    else
        return WEIGHT_BLACK;
}

static int ImplWeightToWin( FontWeight eWeight )
{
    switch ( eWeight )
    {
        case WEIGHT_THIN:
            return FW_THIN;

        case WEIGHT_ULTRALIGHT:
            return FW_ULTRALIGHT;

        case WEIGHT_LIGHT:
            return FW_LIGHT;

        case WEIGHT_SEMILIGHT:
        case WEIGHT_NORMAL:
            return FW_NORMAL;

        case WEIGHT_MEDIUM:
            return FW_MEDIUM;

        case WEIGHT_SEMIBOLD:
            return FW_SEMIBOLD;

        case WEIGHT_BOLD:
            return FW_BOLD;

        case WEIGHT_ULTRABOLD:
            return FW_ULTRABOLD;

        case WEIGHT_BLACK:
            return FW_BLACK;

        default:
            break;
    }

    return 0;
}

static FontPitch ImplLogPitchToSal( BYTE nPitch )
{
    if ( nPitch & FIXED_PITCH )
        return PITCH_FIXED;
    else
        return PITCH_VARIABLE;
}

static FontWidth ImplStretchToSal(DWRITE_FONT_STRETCH eStretch)
{
    switch (eStretch)
    {
        case DWRITE_FONT_STRETCH_ULTRA_CONDENSED:
            return WIDTH_ULTRA_CONDENSED;
        case DWRITE_FONT_STRETCH_EXTRA_CONDENSED:
            return WIDTH_EXTRA_CONDENSED;
        case DWRITE_FONT_STRETCH_CONDENSED:
            return WIDTH_CONDENSED;
        case DWRITE_FONT_STRETCH_SEMI_CONDENSED:
            return WIDTH_SEMI_CONDENSED;
        case DWRITE_FONT_STRETCH_NORMAL:
            return WIDTH_NORMAL;
        case DWRITE_FONT_STRETCH_SEMI_EXPANDED:
            return WIDTH_SEMI_EXPANDED;
        case DWRITE_FONT_STRETCH_EXPANDED:
            return WIDTH_EXPANDED;
        case DWRITE_FONT_STRETCH_EXTRA_EXPANDED:
            return WIDTH_EXTRA_EXPANDED;
        case DWRITE_FONT_STRETCH_ULTRA_EXPANDED:
            return WIDTH_ULTRA_EXPANDED;
        default:
            return WIDTH_DONTKNOW;
    }
}

static OUString ImplGetDWriteName(IDWriteLocalizedStrings* pStrings)
{
    // Prefer the en-us name, matching the name-table reading code used for
    // font matching and legacy-name resolution.
    UINT32 nIndex = 0;
    BOOL bExists = FALSE;
    if (FAILED(pStrings->FindLocaleName(L"en-us", &nIndex, &bExists)) || !bExists)
        nIndex = 0;
    UINT32 nLength = 0;
    if (FAILED(pStrings->GetStringLength(nIndex, &nLength)) || nLength == 0)
        return OUString();
    std::vector<wchar_t> aBuf(nLength + 1);
    if (FAILED(pStrings->GetString(nIndex, aBuf.data(), static_cast<UINT32>(aBuf.size()))))
        return OUString();
    return OUString(o3tl::toU(aBuf.data()), static_cast<sal_Int32>(nLength));
}

static DWRITE_FONT_FACE_TYPE ImplGetDWriteFaceType(IDWriteFont* pFont)
{
    auto xFont = sal::systools::COMReference<IDWriteFont>(pFont).QueryInterface<IDWriteFont3>();
    if (!xFont)
        return DWRITE_FONT_FACE_TYPE_UNKNOWN;
    sal::systools::COMReference<IDWriteFontFaceReference> xFaceRef;
    if (FAILED(xFont->GetFontFaceReference(&xFaceRef)))
        return DWRITE_FONT_FACE_TYPE_UNKNOWN;
    sal::systools::COMReference<IDWriteFontFile> xFile;
    if (FAILED(xFaceRef->GetFontFile(&xFile)))
        return DWRITE_FONT_FACE_TYPE_UNKNOWN;
    BOOL bSupported = FALSE;
    DWRITE_FONT_FILE_TYPE eFileType;
    DWRITE_FONT_FACE_TYPE eFaceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 nFaces = 0;
    if (FAILED(xFile->Analyze(&bSupported, &eFileType, &eFaceType, &nFaces)))
        return DWRITE_FONT_FACE_TYPE_UNKNOWN;
    return eFaceType;
}

static FontAttributes WinFont2DevFontAttributes(IDWriteFont* pFont, const OUString& rFamilyName,
                                                const LOGFONTW& rLogFont)
{
    FontAttributes aDFA;

    // get font face attributes
    aDFA.SetFamilyType(ImplFamilyToSal(rLogFont.lfPitchAndFamily));
    aDFA.SetWidthType(ImplStretchToSal(pFont->GetStretch()));
    aDFA.SetWeight(ImplWeightToSal(pFont->GetWeight()));
    switch (pFont->GetStyle())
    {
        case DWRITE_FONT_STYLE_ITALIC:
            aDFA.SetItalic(ITALIC_NORMAL);
            break;
        case DWRITE_FONT_STYLE_OBLIQUE:
            aDFA.SetItalic(ITALIC_OBLIQUE);
            break;
        default:
            aDFA.SetItalic(ITALIC_NONE);
            break;
    }
    // Get the pitch from DirectWrite; the LOGFONT from ConvertFontToLOGFONT()
    // doesn't carry it (unlike the one GDI enumeration provides).
    if (auto xFont1 = sal::systools::COMReference<IDWriteFont>(pFont).QueryInterface<IDWriteFont1>())
        aDFA.SetPitch(xFont1->IsMonospacedFont() ? PITCH_FIXED : PITCH_VARIABLE);
    else
        aDFA.SetPitch(ImplLogPitchToSal(rLogFont.lfPitchAndFamily));
    aDFA.SetMicrosoftSymbolEncoded(rLogFont.lfCharSet == SYMBOL_CHARSET);

    // get the font face name
    aDFA.SetFamilyName(rFamilyName);
    sal::systools::COMReference<IDWriteLocalizedStrings> xFaceNames;
    if (SUCCEEDED(pFont->GetFaceNames(&xFaceNames)))
        aDFA.SetStyleName(ImplGetDWriteName(xFaceNames.get()));

    // heuristics for font quality
    // -   opentypeTT > truetype
    aDFA.SetQuality(0);
    const DWRITE_FONT_FACE_TYPE eFaceType = ImplGetDWriteFaceType(pFont);
    if (eFaceType == DWRITE_FONT_FACE_TYPE_TRUETYPE
        || eFaceType == DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION)
        aDFA.IncreaseQualityBy(50);
    // everything DirectWrite serves is OpenType
    aDFA.IncreaseQualityBy(10);

    return aDFA;
}

void ImplSalLogFontToFontW( HDC hDC, const LOGFONTW& rLogFont, Font& rFont )
{
    OUString aFontName( o3tl::toU(rLogFont.lfFaceName) );
    if (aFontName.isEmpty())
        return;

    rFont.SetFamilyName( aFontName );
    rFont.SetCharSet( ImplCharSetToSal( rLogFont.lfCharSet ) );
    rFont.SetFamily( ImplFamilyToSal( rLogFont.lfPitchAndFamily ) );
    rFont.SetPitch( ImplLogPitchToSal( rLogFont.lfPitchAndFamily ) );
    rFont.SetWeight( ImplWeightToSal( rLogFont.lfWeight ) );

    tools::Long nFontHeight = rLogFont.lfHeight;
    if ( nFontHeight < 0 )
        nFontHeight = -nFontHeight;
    tools::Long nDPIY = GetDeviceCaps( hDC, LOGPIXELSY );
    if( !nDPIY )
        nDPIY = 600;
    nFontHeight *= 72;
    nFontHeight += nDPIY/2;
    nFontHeight /= nDPIY;
    rFont.SetFontSize( Size( 0, nFontHeight ) );
    rFont.SetOrientation( Degree10(static_cast<sal_Int16>(rLogFont.lfEscapement)) );
    if ( rLogFont.lfItalic )
        rFont.SetItalic( ITALIC_NORMAL );
    else
        rFont.SetItalic( ITALIC_NONE );
    if ( rLogFont.lfUnderline )
        rFont.SetUnderline( LINESTYLE_SINGLE );
    else
        rFont.SetUnderline( LINESTYLE_NONE );
    if ( rLogFont.lfStrikeOut )
        rFont.SetStrikeout( STRIKEOUT_SINGLE );
    else
        rFont.SetStrikeout( STRIKEOUT_NONE );
}

static sal_IntPtr getNextFontId()
{
    static sal_IntPtr id;
    return ++id;
}

WinFontFace::WinFontFace(const FontAttributes& rDFA, const LOGFONTW& rLogFont,
                         IDWriteFont* pDWFont)
:   vcl::font::PhysicalFontFace(rDFA),
    mnId(getNextFontId()),
    maLogFont(rLogFont),
    mxDWFont(pDWFont)
{
}

WinFontFace::~WinFontFace()
{
}

IDWriteFontFace* WinFontFace::GetDWFontFace() const
{
    if (!mxDWFontFace && mxDWFont)
    {
        HRESULT hr = mxDWFont->CreateFontFace(&mxDWFontFace);
        if (FAILED(hr))
        {
            SAL_WARN("vcl.fonts", "HRESULT 0x" << OUString::number(hr, 16) << ": "
                                               << comphelper::WindowsErrorStringFromHRESULT(hr));
        }
    }
    return mxDWFontFace.get();
}

sal_IntPtr WinFontFace::GetFontId() const
{
    return mnId;
}

rtl::Reference<LogicalFontInstance> WinFontFace::CreateFontInstance(const vcl::font::FontSelectPattern& rFSD) const
{
    assert(SkiaHelper::isVCLSkiaEnabled() && "Windows requires skia");
    return new SkiaWinFontInstance(*this, rFSD);
}

const std::vector<vcl::font::Variation>&
WinFontFace::GetVariations(const LogicalFontInstance& rFont) const
{
    if (!mxVariations)
    {
        mxVariations.emplace();
        auto xDWFontFace5(static_cast<const WinFontInstance&>(rFont).GetDWFontFace()
                              .QueryInterface<IDWriteFontFace5>());
        if (xDWFontFace5 && xDWFontFace5->HasVariations())
        {
            std::vector<DWRITE_FONT_AXIS_VALUE> aAxisValues(xDWFontFace5->GetFontAxisValueCount());
            auto hr = xDWFontFace5->GetFontAxisValues(aAxisValues.data(), aAxisValues.size());
            if (SUCCEEDED(hr))
            {
                mxVariations->reserve(aAxisValues.size());
                for (auto& rAxisValue : aAxisValues)
                    mxVariations->push_back({ OSL_NETDWORD(rAxisValue.axisTag), rAxisValue.value });
            }
        }
    }

    return *mxVariations;
}

static hb_blob_t* createBlob(const void* pData, unsigned int nSize)
{
    if (!pData || nSize == 0)
        return nullptr;
    char* pBuffer = new char[nSize];
    memcpy(pBuffer, pData, nSize);
    return hb_blob_create(pBuffer, nSize, HB_MEMORY_MODE_READONLY, pBuffer,
                          [](void* pUserData) { delete[] static_cast<char*>(pUserData); });
}

// The whole font file, read only when requested.
static hb_blob_t* getFontFile(IDWriteFontFace* pFontFace)
{
    UINT32 nFiles = 1;
    sal::systools::COMReference<IDWriteFontFile> xFile;
    sal::systools::COMReference<IDWriteFontFileLoader> xLoader;
    sal::systools::COMReference<IDWriteFontFileStream> xStream;
    const void* pKey = nullptr;
    UINT32 nKeySize = 0;
    UINT64 nSize = 0;
    const void* pData = nullptr;
    void* pContext = nullptr;
    if (FAILED(pFontFace->GetFiles(&nFiles, &xFile))
        || FAILED(xFile->GetReferenceKey(&pKey, &nKeySize)) || FAILED(xFile->GetLoader(&xLoader))
        || FAILED(xLoader->CreateStreamFromKey(pKey, nKeySize, &xStream))
        || FAILED(xStream->GetFileSize(&nSize))
        || FAILED(xStream->ReadFileFragment(&pData, 0, nSize, &pContext)))
        return nullptr;

    hb_blob_t* pBlob = createBlob(pData, static_cast<unsigned int>(nSize));
    xStream->ReleaseFileFragment(pContext);
    return pBlob;
}

static hb_blob_t* getFontTable(hb_face_t*, hb_tag_t nTag, void* pUserData)
{
    auto pFontFace = static_cast<IDWriteFontFace*>(pUserData);

    // nTag == 0 references the whole font file.
    if (nTag == 0)
        return getFontFile(pFontFace);

    const void* pData = nullptr;
    UINT32 nSize = 0;
    void* pContext = nullptr;
    BOOL bExists = FALSE;
    if (FAILED(
            pFontFace->TryGetFontTable(OSL_NETDWORD(nTag), &pData, &nSize, &pContext, &bExists)))
        return nullptr;

    hb_blob_t* pBlob = nullptr;
    if (bExists)
    {
        pBlob = createBlob(pData, nSize);
        pFontFace->ReleaseFontTable(pContext);
    }
    return pBlob;
}

hb_face_t* WinFontFace::GetHbFace() const
{
    if (!mpHbFace)
    {
        if (IDWriteFontFace* pFontFace = GetDWFontFace())
        {
            pFontFace->AddRef();
            mpHbFace = hb_face_create_for_tables(getFontTable, pFontFace, [](void* pUserData) {
                static_cast<IDWriteFontFace*>(pUserData)->Release();
            });
        }
        else
            mpHbFace = hb_face_get_empty();
    }
    return mpHbFace;
}

hb_blob_t* WinFontFace::GetHbTable(hb_tag_t nTag) const
{
    return hb_face_reference_table(GetHbFace(), nTag);
}

void WinSalGraphics::SetTextColor( Color nColor )
{
    COLORREF aCol = RGB( nColor.GetRed(),
                         nColor.GetGreen(),
                         nColor.GetBlue() );

    ::SetTextColor( getHDC(), aCol );
}

void ImplGetLogFontFromFontSelect( const vcl::font::FontSelectPattern& rFont,
                                   const vcl::font::PhysicalFontFace& rFontFace,
                                   LOGFONTW& rLogFont, bool bAntiAliased)
{
    // start from the LOGFONT the face was enumerated with
    rLogFont = static_cast<const WinFontFace&>(rFontFace).GetLogFont();

    rLogFont.lfWeight          = ImplWeightToWin( rFont.GetWeight() );
    rLogFont.lfHeight          = static_cast<LONG>(-rFont.mnHeight);
    // mnWidth is relative to the font height, unlike GDI's lfWidth which is
    // relative to the average character width, so it can't be used directly.
    rLogFont.lfWidth           = 0;
    rLogFont.lfUnderline       = 0;
    rLogFont.lfStrikeOut       = 0;
    rLogFont.lfItalic          = BYTE(rFont.GetItalic() != ITALIC_NONE);
    rLogFont.lfEscapement      = rFont.mnOrientation.get();
    rLogFont.lfOrientation     = rLogFont.lfEscapement;
    rLogFont.lfClipPrecision   = CLIP_DEFAULT_PRECIS;
    rLogFont.lfOutPrecision    = OUT_TT_PRECIS;
    if ( rFont.mnOrientation )
        rLogFont.lfClipPrecision |= CLIP_LH_ANGLES;

    // disable antialiasing if requested
    if ( rFont.mbNonAntialiased )
        rLogFont.lfQuality = NONANTIALIASED_QUALITY;
    else if (Application::GetSettings().GetStyleSettings().GetUseFontAAFromSystem())
        rLogFont.lfQuality = DEFAULT_QUALITY; // for display on screen
    else
        rLogFont.lfQuality = bAntiAliased ? ANTIALIASED_QUALITY : NONANTIALIASED_QUALITY;
}

std::tuple<HFONT, HFONT>
WinSalGraphics::ImplDoSetFont(HDC hDC, vcl::font::FontSelectPattern const& i_rFont,
                              const vcl::font::PhysicalFontFace& i_rFontFace, HFONT& o_rOldFont)
{
    HFONT hNewFont = nullptr;

    LOGFONTW aLogFont;
    ImplGetLogFontFromFontSelect( i_rFont, i_rFontFace, aLogFont, getAntiAlias());

    hNewFont = ::CreateFontIndirectW( &aLogFont );
    o_rOldFont = ::SelectFont(hDC, hNewFont);

    TEXTMETRICW aTextMetricW;
    if (!::GetTextMetricsW(hDC, &aTextMetricW))
    {
        // the selected font doesn't work => try a replacement
        // TODO: use its font fallback instead
        lstrcpynW( aLogFont.lfFaceName, L"Courier New", 12 );
        aLogFont.lfPitchAndFamily = FIXED_PITCH;
        HFONT hNewFont2 = CreateFontIndirectW( &aLogFont );
        SelectFont(hDC, hNewFont2);
        DeleteFont( hNewFont );
        hNewFont = hNewFont2;
        ::GetTextMetricsW(hDC, &aTextMetricW);
    }

    // Stretch the glyphs horizontally like the layout does (mnWidth relative to the
    // font height), expressed in GDI's average character width terms.
    if (i_rFont.mnWidth && i_rFont.mnHeight && aTextMetricW.tmAveCharWidth > 0)
    {
        aLogFont.lfWidth = basegfx::fround(double(i_rFont.mnWidth) * aTextMetricW.tmAveCharWidth
                                           / i_rFont.mnHeight);
        HFONT hStretchedFont = ::CreateFontIndirectW(&aLogFont);
        ::SelectFont(hDC, hStretchedFont);
        ::DeleteFont(hNewFont);
        hNewFont = hStretchedFont;
    }

    // Optionally create a secondary font for non-rotated CJK glyphs in vertical context
    HFONT hNewVerticalFont = nullptr;
    if (i_rFont.mbVertical && mbPrinter)
    {
        aLogFont.lfEscapement = 0;
        aLogFont.lfOrientation = 0;
        hNewVerticalFont = ::CreateFontIndirectW(&aLogFont);
    }

    return std::make_tuple(hNewFont, hNewVerticalFont);
}

void WinSalGraphics::SetFont(LogicalFontInstance* pFont, int nFallbackLevel)
{
    assert(nFallbackLevel >= 0 && nFallbackLevel < MAX_FALLBACK);

    // return early if there is no new font
    if( !pFont )
    {
        if (!mpWinFontEntry[nFallbackLevel].is())
            return;

        // DeInitGraphics doesn't free the cached fonts, so mhDefFont might be nullptr
        if (mhDefFont)
        {
            ::SelectFont(getHDC(), mhDefFont);
            mhDefFont = nullptr;
        }

        // release no longer referenced font handles
        for( int i = nFallbackLevel; i < MAX_FALLBACK; ++i )
            mpWinFontEntry[i] = nullptr;
        return;
    }

    WinFontInstance *pFontInstance = static_cast<WinFontInstance*>(pFont);
    mpWinFontEntry[ nFallbackLevel ] = pFontInstance;

    HFONT hOldFont = nullptr;
    HFONT hNewFont = pFontInstance->GetHFONT();
    if (!hNewFont)
    {
        pFontInstance->SetGraphics(this);
        hNewFont = pFontInstance->GetHFONT();
    }
    hOldFont = ::SelectFont(getHDC(), hNewFont);

    // keep default font
    if( !mhDefFont )
        mhDefFont = hOldFont;
    else
    {
        // release no longer referenced font handles
        for( int i = nFallbackLevel + 1; i < MAX_FALLBACK && mpWinFontEntry[i].is(); ++i )
            mpWinFontEntry[i] = nullptr;
    }
}

void WinSalGraphics::GetFontMetric( FontMetricDataRef& rxFontMetric, int nFallbackLevel )
{
    rtl::Reference<WinFontInstance> pFontInstance = mpWinFontEntry[nFallbackLevel];
    const WinFontFace* pFace = pFontInstance->GetFontFace();

    // device independent font attributes
    rxFontMetric->FontAttributes::operator=(*pFace);
    rxFontMetric->SetSlant( 0 );

    rxFontMetric->SetMinKashida(pFontInstance->GetKashidaWidth());
    rxFontMetric->ImplCalcLineSpacing(pFontInstance.get());
    rxFontMetric->ImplInitBaselines(pFontInstance.get());

    // transformation dependent font metrics, mnWidth is only used for
    // stretching/squeezing fonts
    const vcl::font::FontSelectPattern& rFSP = pFontInstance->GetFontSelectPattern();
    rxFontMetric->SetWidth(rFSP.mnWidth ? rFSP.mnWidth : rFSP.mnHeight);
}

FontCharMapRef WinSalGraphics::GetFontCharMap() const
{
    if (!mpWinFontEntry[0])
    {
        return FontCharMapRef( new FontCharMap() );
    }
    return mpWinFontEntry[0]->GetFontFace()->GetFontCharMap();
}

bool WinSalGraphics::GetFontCapabilities(vcl::FontCapabilities &rFontCapabilities) const
{
    if (!mpWinFontEntry[0])
        return false;
    return mpWinFontEntry[0]->GetFontFace()->GetFontCapabilities(rFontCapabilities);
}

static void ImplEnumDWriteCollection(IDWriteFontCollection* pCollection,
                                     vcl::font::PhysicalFontCollection* pList)
{
    const UINT32 nFamilies = pCollection->GetFontFamilyCount();
    for (UINT32 i = 0; i < nFamilies; ++i)
    {
        sal::systools::COMReference<IDWriteFontFamily> xFamily;
        if (FAILED(pCollection->GetFontFamily(i, &xFamily)))
            continue;
        sal::systools::COMReference<IDWriteLocalizedStrings> xFamilyNames;
        if (FAILED(xFamily->GetFamilyNames(&xFamilyNames)))
            continue;
        const OUString aFamilyName = ImplGetDWriteName(xFamilyNames.get());
        if (aFamilyName.isEmpty())
            continue;
        const UINT32 nFonts = xFamily->GetFontCount();
        for (UINT32 j = 0; j < nFonts; ++j)
        {
            sal::systools::COMReference<IDWriteFont> xFont;
            if (FAILED(xFamily->GetFont(j, &xFont)))
                continue;

            // Skip synthetic bold/oblique entries, the unsimulated face is
            // enumerated too.
            if (xFont->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE)
                continue;

            // The LOGFONT GDI needed to select exactly this face.
            LOGFONTW aLogFont{};
            BOOL bIsSystemFont = FALSE;
            if (FAILED(WinSalGraphics::getDWriteGdiInterop()->ConvertFontToLOGFONT(
                    xFont.get(), &aLogFont, &bIsSystemFont)))
                continue;

            rtl::Reference<WinFontFace> pData = new WinFontFace(
                WinFont2DevFontAttributes(xFont.get(), aFamilyName, aLogFont), aLogFont,
                xFont.get());
            pList->Add(pData.get());
            SAL_INFO("vcl.fonts", "ImplEnumDWriteCollection: font added: "
                                      << pData->GetFamilyName() << " " << pData->GetStyleName());
        }
    }
}

static void ImplEnumDWriteSystemFonts(vcl::font::PhysicalFontCollection* pList)
{
    sal::systools::COMReference<IDWriteFontCollection2> xCollection;
    HRESULT hr = WinSalGraphics::getDWriteFactory()->GetSystemFontCollection(
        FALSE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &xCollection);
    if (FAILED(hr))
    {
        SAL_WARN("vcl.fonts", "Enumerating system fonts failed: "
                                  << comphelper::WindowsErrorStringFromHRESULT(hr));
        return;
    }
    ImplEnumDWriteCollection(xCollection.get(), pList);
}

static bool ImplEnumDWriteFontFiles(vcl::font::PhysicalFontCollection* pList,
                                    const std::unordered_set<OUString>& rFontPaths)
{
    if (rFontPaths.empty())
        return true;
    const auto& xFactory = WinSalGraphics::getDWriteFactory();
    sal::systools::COMReference<IDWriteFontSetBuilder1> xBuilder;
    sal::systools::COMReference<IDWriteFontSet> xFontSet;
    sal::systools::COMReference<IDWriteFontCollection2> xCollection;
    HRESULT hr = xFactory->CreateFontSetBuilder(&xBuilder);
    if (SUCCEEDED(hr))
    {
        for (const OUString& rPath : rFontPaths)
        {
            sal::systools::COMReference<IDWriteFontFile> xFile;
            if (SUCCEEDED(
                    xFactory->CreateFontFileReference(o3tl::toW(rPath.getStr()), nullptr, &xFile)))
                xBuilder->AddFontFile(xFile.get());
        }
        hr = xBuilder->CreateFontSet(&xFontSet);
    }
    if (SUCCEEDED(hr))
        hr = xFactory->CreateFontCollectionFromFontSet(
            xFontSet.get(), DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &xCollection);
    if (FAILED(hr))
    {
        SAL_WARN("vcl.fonts", "Enumerating font files failed: "
                                  << comphelper::WindowsErrorStringFromHRESULT(hr));
        return false;
    }
    ImplEnumDWriteCollection(xCollection.get(), pList);
    return true;
}

static int lcl_AddFontResource(WindowsInstance& rWinInstance, const OUString& rFontFileURL)
{
    OUString aFontSystemPath;
    OSL_VERIFY(!osl::FileBase::getSystemPathFromFileURL(rFontFileURL, aFontSystemPath));

    int nRet = AddFontResourceExW(o3tl::toW(aFontSystemPath.getStr()), FR_PRIVATE, nullptr);
    SAL_WARN_IF(nRet <= 0, "vcl.fonts", "AddFontResourceExW failed for " << rFontFileURL);
    if (nRet <= 0)
        return nRet;

    rWinInstance.GetData().m_aTempFontPaths.insert(aFontSystemPath);

    return nRet;
}

void ImplReleaseTempFonts()
{
    std::unordered_set<OUString>& rTempFontPaths = GetWindowsInstance().GetData().m_aTempFontPaths;
    for (const OUString& rFontPath : rTempFontPaths)
        RemoveFontResourceExW(o3tl::toW(rFontPath.getStr()), FR_PRIVATE, nullptr);
    rTempFontPaths.clear();
}

bool WinSalGraphics::AddTempDevFont(vcl::font::PhysicalFontCollection* pFontCollection,
                                    const OUString& rFontFileURL, const OUString& rFontName)
{
    // The font is enumerated from its file under its own names, but GDI registration
    // is still needed for font selection.
    int nFonts = lcl_AddFontResource(GetWindowsInstance(), rFontFileURL);
    if (nFonts <= 0)
        return false;

    OUString aFontSystemPath;
    OSL_VERIFY(!osl::FileBase::getSystemPathFromFileURL(rFontFileURL, aFontSystemPath));
    // Fonts registered with GDI are not in the DirectWrite system collection.
    if (!ImplEnumDWriteFontFiles(pFontCollection, { aFontSystemPath }))
        return false;

    SAL_WARN_IF(!pFontCollection->FindFontFamily(rFontName), "vcl.fonts",
                "temp font was registered but is not in enumeration: " << rFontFileURL);
    return true;
}

bool WinSalGraphics::RemoveTempDevFont(const OUString& rFileURL, const OUString& /*rFontName*/)
{
    OUString path;
    osl::FileBase::getSystemPathFromFileURL(rFileURL, path);
    std::unordered_set<OUString>& rTempFontPaths = GetWindowsInstance().GetData().m_aTempFontPaths;
    auto aIt = rTempFontPaths.find(path);
    if (aIt != rTempFontPaths.end())
    {
        RemoveFontResourceExW(o3tl::toW(path.getStr()), FR_PRIVATE, nullptr);
        rTempFontPaths.erase(aIt);
        return true;
    }

    SAL_WARN("vcl.fonts", "Trying to unregister an embedded font that wasn't registered?");
    return true; // It's still safe to delete the font file: we don't use it
}

void WinSalGraphics::GetDevFontList( vcl::font::PhysicalFontCollection* pFontCollection )
{
    // make sure all LO shared fonts are registered temporarily
    static std::once_flag init;
    std::call_once(init, []()
    {
        auto registerFontsIn = [](const OUString& dir) {
            // collect fonts in font path that could not be registered
            osl::Directory aFontDir(dir);
            osl::FileBase::RC rcOSL = aFontDir.open();
            if (rcOSL == osl::FileBase::E_None)
            {
                WindowsInstance& rWinInstance = GetWindowsInstance();
                osl::DirectoryItem aDirItem;
                while (aFontDir.getNextItem(aDirItem, 10) == osl::FileBase::E_None)
                {
                    osl::FileStatus aFileStatus(osl_FileStatus_Mask_FileURL);
                    rcOSL = aDirItem.getFileStatus(aFileStatus);
                    if (rcOSL == osl::FileBase::E_None)
                        lcl_AddFontResource(rWinInstance, aFileStatus.getFileURL());
                }
            }
        };

        // determine font path
        // since we are only interested in fonts that could not be
        // registered before because of missing administration rights
        // only the font path of the user installation is needed
        OUString aPath("$BRAND_BASE_DIR");
        rtl_bootstrap_expandMacros(&aPath.pData);

        // internal font resources, required for normal operation, like OpenSymbol
        registerFontsIn(aPath + "/" LIBO_SHARE_RESOURCE_FOLDER "/common/fonts");

        // collect fonts in font path that could not be registered
        registerFontsIn(aPath + "/" LIBO_SHARE_FOLDER "/fonts/truetype");

        return true;
    });

    ImplEnumDWriteSystemFonts(pFontCollection);
    ImplEnumDWriteFontFiles(pFontCollection, GetWindowsInstance().GetData().m_aTempFontPaths);

    // set glyph fallback hook
    static WinGlyphFallbackSubstititution aSubstFallback;
    static WinPreMatchFontSubstititution aPreMatchFont;
    pFontCollection->SetFallbackHook( &aSubstFallback );
    pFontCollection->SetPreMatchHook(&aPreMatchFont);
}

void WinSalGraphics::ClearDevFontCache()
{
    mWinSalGraphicsImplBase->ClearDevFontCache();
}

namespace
{
// Builds a B2DPolyPolygon from the glyph outline
class B2DGeometrySink : public IDWriteGeometrySink
{
    basegfx::B2DPolyPolygon& mrPolyPoly;
    basegfx::B2DPolygon maPolygon;

public:
    B2DGeometrySink(basegfx::B2DPolyPolygon& rPolyPoly)
        : mrPolyPoly(rPolyPoly)
    {
    }

    // IUnknown, for a stack-allocated sink
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID rIId, void** ppObject) override
    {
        if (rIId == __uuidof(IDWriteGeometrySink) || rIId == __uuidof(IUnknown))
        {
            *ppObject = this;
            return S_OK;
        }
        *ppObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}

    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F aStartPoint, D2D1_FIGURE_BEGIN) override
    {
        maPolygon.append(basegfx::B2DPoint(aStartPoint.x, aStartPoint.y));
    }

    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* pPoints, UINT32 nCount) override
    {
        for (UINT32 i = 0; i < nCount; ++i)
            maPolygon.append(basegfx::B2DPoint(pPoints[i].x, pPoints[i].y));
    }

    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* pBeziers, UINT32 nCount) override
    {
        for (UINT32 i = 0; i < nCount; ++i)
            maPolygon.appendBezierSegment(
                basegfx::B2DPoint(pBeziers[i].point1.x, pBeziers[i].point1.y),
                basegfx::B2DPoint(pBeziers[i].point2.x, pBeziers[i].point2.y),
                basegfx::B2DPoint(pBeziers[i].point3.x, pBeziers[i].point3.y));
    }

    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override
    {
        maPolygon.setClosed(true);
        mrPolyPoly.append(maPolygon);
        maPolygon.clear();
    }

    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
}

bool WinFontInstance::GetGlyphOutline(sal_GlyphId nId, basegfx::B2DPolyPolygon& rB2DPolyPoly,
                                      bool) const
{
    rB2DPolyPoly.clear();

    IDWriteFontFace* pFontFace = GetDWFontFace().get();
    if (!pFontFace)
        return false;

    const vcl::font::FontSelectPattern& rFSP = GetFontSelectPattern();
    const UINT16 nIndex = nId;
    B2DGeometrySink aSink(rB2DPolyPoly);
    if (FAILED(pFontFace->GetGlyphRunOutline(rFSP.mnHeight, &nIndex, nullptr, nullptr, 1, FALSE,
                                             FALSE, &aSink)))
        return false;

    const float fHScale = getHScale();
    if (fHScale != 1.0f)
        rB2DPolyPoly.transform(basegfx::utils::createScaleB2DHomMatrix(fHScale, 1.0));

    return true;
}

const sal::systools::COMReference<IDWriteFontFace>& WinFontInstance::GetDWFontFace() const
{
    if (!mxDWFontFace)
    {
        IDWriteFont* pDWFont = GetFontFace()->GetDWFont();
        if (!pDWFont)
            return mxDWFontFace;

        // Simulate bold and italic when they are requested but the face does not
        // provide them, like GDI font selection does.
        DWRITE_FONT_SIMULATIONS eSimulations = DWRITE_FONT_SIMULATIONS_NONE;
        const vcl::font::FontSelectPattern& rFSD = GetFontSelectPattern();
        if (rFSD.GetWeight() > WEIGHT_MEDIUM && GetFontFace()->GetWeight() <= WEIGHT_MEDIUM)
            eSimulations |= DWRITE_FONT_SIMULATIONS_BOLD;
        if (rFSD.GetItalic() != ITALIC_NONE && GetFontFace()->GetItalic() == ITALIC_NONE)
            eSimulations |= DWRITE_FONT_SIMULATIONS_OBLIQUE;

        HRESULT hr = S_OK;
        if (eSimulations == DWRITE_FONT_SIMULATIONS_NONE)
            mxDWFontFace
                = sal::systools::COMReference<IDWriteFontFace>(GetFontFace()->GetDWFontFace());
        else
        {
            auto xDWFont = sal::systools::COMReference<IDWriteFont>(pDWFont)
                               .QueryInterface<IDWriteFont3>();
            sal::systools::COMReference<IDWriteFontFaceReference> xFaceRef;
            hr = xDWFont ? xDWFont->GetFontFaceReference(&xFaceRef) : E_NOINTERFACE;
            if (SUCCEEDED(hr))
            {
                sal::systools::COMReference<IDWriteFontFace3> xFontFace;
                hr = xFaceRef->CreateFontFaceWithSimulations(eSimulations, &xFontFace);
                if (SUCCEEDED(hr))
                    mxDWFontFace = sal::systools::COMReference<IDWriteFontFace>(xFontFace.get());
            }
        }
        if (FAILED(hr))
        {
            SAL_WARN("vcl.fonts", "HRESULT 0x" << OUString::number(hr, 16) << ": "
                                               << comphelper::WindowsErrorStringFromHRESULT(hr));
            mxDWFontFace = nullptr;
        }
    }

    return mxDWFontFace;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
