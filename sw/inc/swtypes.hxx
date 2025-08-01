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
#ifndef INCLUDED_SW_INC_SWTYPES_HXX
#define INCLUDED_SW_INC_SWTYPES_HXX
#include <rtl/ustring.hxx>

#include <limits.h>
#include <com/sun/star/uno/Reference.h>
#include <com/sun/star/i18n/CollatorOptions.hpp>
#include "swdllapi.h"
#include <o3tl/typed_flags_set.hxx>
#include <o3tl/unit_conversion.hxx>
#include <i18nlangtag/lang.h>
#include <vcl/outdev.hxx>
#include <unotools/resmgr.hxx>

namespace com::sun::star {
    namespace linguistic2{
        class XLinguProperties;
        class XSpellChecker1;
        class XHyphenator;
        class XThesaurus;
    }
}
namespace utl{
    class TransliterationWrapper;
}

class Size;
class Graphic;
class CharClass;
class CollatorWrapper;
class LanguageTag;

typedef tools::Long SwTwips;
#define INVALID_TWIPS   LONG_MAX
#define TWIPS_MAX       (LONG_MAX - 1)

constexpr sal_Int32 COMPLETE_STRING = SAL_MAX_INT32;

constexpr SwTwips cMinHdFtHeight = 56; // ~1mm

#define MINFLY 23   // Minimal size for FlyFrames.
#define MINLAY 23   // Minimal size for other Frames.

/// hard-coded value of read-only TableColumnRelativeSum property
constexpr int UNO_TABLE_COLUMN_SUM{10000};

// Default column distance of two text columns corresponds to 0.3 cm.
constexpr SwTwips DEF_GUTTER_WIDTH = o3tl::toTwips(3, o3tl::Length::mm);

// Minimal distance (distance to text) for border attribute
// in order not to crock up aligned lines.
// 28 Twips == 0,5mm
constexpr SwTwips MIN_BORDER_DIST = 28; // ~0.5mm

// Minimal document border: 20mm.
constexpr tools::Long lMinBorderInMm(20);
constexpr SwTwips lMinBorder = o3tl::toTwips(lMinBorderInMm, o3tl::Length::mm);

// Margin left and above document.
// Half of it is gap between the pages.
//TODO: Replace with SwViewOption::defDocumentBorder
constexpr SwTwips DOCUMENTBORDER = 284; // ~5mm

// For inserting of captions (what and where to insert).
// It's here because it is not big enough to justify its own hxx
// and does not seem to fit somewhere else.
enum class SwLabelType
{
    Table,    // Caption for a table.
    Object,   // Caption for a graphic or OLE.
    Fly,      // Caption for a text frame.
    Draw      // Caption for a draw object.
};

constexpr sal_uInt8 MAXLEVEL = 10;

//  Values for indents at numbering and bullet lists.
//  (For more levels the values have to be multiplied with the levels+1;
//  levels 0 ..4!)

constexpr short lBulletIndent = o3tl::toTwips(25, o3tl::Length::in100); // 0.25 inch
constexpr short lBulletFirstLineOffset = -lBulletIndent;
constexpr sal_uInt16 lNumberIndent = o3tl::toTwips(25, o3tl::Length::in100); // 0.25 inch
constexpr short lNumberFirstLineOffset = -lNumberIndent;
constexpr short lOutlineMinTextDistance = o3tl::toTwips(15, o3tl::Length::in100); // 0.15 inch = 0.38 cm

// Count of SystemField-types of SwDoc.
#define INIT_FLDTYPES   33

// Count of predefined Seq-field types. It is always the last
// fields before INIT_FLDTYPES.
#define INIT_SEQ_FLDTYPES   5

// defined in sw/source/uibase/app/swmodule.cxx
SW_DLLPUBLIC OUString SwResId(TranslateId aId);
OUString SwResId(TranslateNId aContextSingularPlural, int nCardinality);

css::uno::Reference< css::linguistic2::XSpellChecker1 > GetSpellChecker();
css::uno::Reference< css::linguistic2::XHyphenator >    GetHyphenator();
css::uno::Reference< css::linguistic2::XThesaurus >     GetThesaurus();
css::uno::Reference< css::linguistic2::XLinguProperties > GetLinguPropertySet();

// Returns the twip size of this graphic.
SW_DLLPUBLIC Size GetGraphicSizeTwip( const Graphic&, vcl::RenderContext* pOutDev );

// Separator for jumps to different content types in document.
const sal_Unicode cMarkSeparator = '|';
// Sequences names for jumps are <name of sequence>!<no>
const char cSequenceMarkSeparator = '!';
/// separator for toxmarks: #<no>%19<text>%19<type><typename>|toxmark
sal_Unicode const toxMarkSeparator = '\u0019';

#define DB_DELIM u'\x00ff'        // Database <-> table separator.

enum class SetAttrMode
{
    DEFAULT         = 0x0000,  // Default.
    /// @attention: DONTEXPAND does not work very well for CHARATR
    ///             because it can expand only the whole AUTOFMT or nothing
    DONTEXPAND      = 0x0001,  // Don't expand text attribute any further.
    DONTREPLACE     = 0x0002,  // Don't replace another text attribute.

    NOTXTATRCHR     = 0x0004,  // Don't insert 0xFF at attributes with no end.
    /// attention: NOHINTADJUST prevents MergePortions!
    /// when using this need to pay attention to ignore start/end flags of hint
    NOHINTADJUST    = 0x0008,  // No merging of ranges.
    NOFORMATATTR    = 0x0010,  // Do not change into format attribute.
    APICALL         = 0x0020,  // Called from API (all UI related
                               // functionality will be disabled).
    /// Force hint expand (only matters for hints with CH_TXTATR).
    FORCEHINTEXPAND = 0x0040,
    /// The inserted item is a copy -- intended for use in ndtxt.cxx.
    IS_COPY         = 0x0080,
    /// for Undo, translated to SwInsertFlags::NOHINTEXPAND
    NOHINTEXPAND    = 0x0100,
    /// don't change the cursor position
    NO_CURSOR_CHANGE = 0x0200,
    // remove all char attributes and char styles when para/char styles are applied
    REMOVE_ALL_ATTR = 0x0400
};
namespace o3tl
{
    template<> struct typed_flags<SetAttrMode> : is_typed_flags<SetAttrMode, 0x7ff> {};
}

namespace sw {

enum class GetTextAttrMode {
    Default,    /// DEFAULT: (Start <= nIndex <  End)
    Expand,     /// EXPAND : (Start <  nIndex <= End)
    Parent,     /// PARENT : (Start <  nIndex <  End)
};

} // namespace sw

constexpr bool SW_ISPRINTABLE(sal_Unicode c) { return c >= ' ' && 127 != c; }

#define CHAR_HARDBLANK      u'\x00A0'
#define CHAR_HARDHYPHEN     u'\x2011'
#define CHAR_SOFTHYPHEN     u'\x00AD'
#define CHAR_RLM            u'\x200F'
#define CHAR_LRM            u'\x200E'
#define CHAR_ZWSP           u'\x200B'
#define CHAR_WJ             u'\x2060'
#define CHAR_NNBSP          u'\x202F' //NARROW NO-BREAK SPACE

inline constexpr OUString vEnSpaces = u"\u2002\u2002\u2002\u2002\u2002"_ustr;

// Returns the APP - CharClass instance - used for all ToUpper/ToLower/...
SW_DLLPUBLIC CharClass& GetAppCharClass();
SW_DLLPUBLIC LanguageType GetAppLanguage();
SW_DLLPUBLIC const LanguageTag& GetAppLanguageTag();

#if 0
// I18N doesn't get this right, can't specify more than one to ignore
#define SW_COLLATOR_IGNORES ( \
    css::i18n::CollatorOptions::CollatorOptions_IGNORE_CASE | \
    css::i18n::CollatorOptions::CollatorOptions_IGNORE_KANA | \
    css::i18n::CollatorOptions::CollatorOptions_IGNORE_WIDTH )
#else
#define SW_COLLATOR_IGNORES ( \
    css::i18n::CollatorOptions::CollatorOptions_IGNORE_CASE )
#endif

SW_DLLPUBLIC CollatorWrapper& GetAppCollator();
CollatorWrapper& GetAppCaseCollator();

SW_DLLPUBLIC const ::utl::TransliterationWrapper& GetAppCmpStrIgnore();

// Official shortcut for Prepare() regarding notification of Content by the Layout.
// Content provides for calculation of minimal requirements at the next call of ::Format().
enum class PrepareHint
{
    Clear,                     // Reformat completely.
    WidowsOrphans,             // Only check for widows and orphans and split in case of need.
    FixSizeChanged,            // FixSize has changed.
    FollowFollows,             // Follow is now possibly adjacent.
    AdjustSizeWithoutFormatting,  // Adjust size via grow/shrink without formatting.
    FlyFrameSizeChanged,       // A FlyFrame has changed its size.
    FlyFrameAttributesChanged, // A FlyFrame has changed its attributes (e. g. wrap).
    FlyFrameArrive,            // A FlyFrame now overlaps range.
    FlyFrameLeave,             // A FlyFrame has left range.
    FootnoteInvalidation,      // Invalidation of footnotes.
    FramePositionChanged,      // Position of Frame has changed.
                               // (Check for Fly-break). In void* of Prepare()
                               // a sal_Bool& is passed. If this is sal_True,
                               // it indicates that a format has been executed.
    ULSpaceChanged,            // UL-Space has changed, TextFrames have to
                               // re-calculate line space.
    MustFit,                   // Make frm fit (split) even if the attributes do
                               // not allow that (e.g. "keep together").
    Widows,                    // A follow realizes that the orphan rule will be applied
                               // for it and sends a Widows to its predecessor
                               // (Master/Follow).
    QuoVadis,                  // If a footnote has to be split between two paragraphs
                               // the last on the page has to receive a QUOVADIS in
                               // order to format the text into it.
    BossChanged,               // If a Frame changes its column/page this additional
                               // Prepare is sent to POS_CHGD in MoveFwd/Bwd
                               // (join Footnote-numbers etc.)
                               // Direction is communicated via pVoid:
                               //     MoveFwd: pVoid == 0
                               //     MoveBwd: pVoid == pOldPage
    Register,                  // Invalidate frames with registers.
    FootnoteInvalidationGone,  // A Follow loses its footnote, possibly its first line can move up.
    FootnoteMove,              // A footnote changes its page. Its contents receives at first a
                               // height of zero in order to avoid too much noise. At formatting
                               // it checks whether it fits and if necessary changes its page again.
    ErgoSum,                   // Needed because of movement in FootnoteFrames. Check QuoVadis/ErgoSum.
};

enum class FrameControlType
{
    PageBreak,
    Header,
    Footer,
    FloatingTable,
    ContentControl,
    Outline
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
