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

#include <memory>

#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <vcl/event.hxx>
#include <vcl/i18nhelp.hxx>
#include <vcl/naturalsort.hxx>
#include <vcl/vcllayout.hxx>
#include <vcl/toolkit/lstbox.hxx>
#include <vcl/toolkit/scrbar.hxx>

#include <listbox.hxx>
#include <svdata.hxx>
#include <window.h>

#include <com/sun/star/accessibility/AccessibleRole.hpp>

#include <sal/log.hxx>
#include <o3tl/safeint.hxx>
#include <o3tl/string_view.hxx>
#include <osl/diagnose.h>
#include <comphelper/string.hxx>
#include <comphelper/processfactory.hxx>

#include <limits>

#define MULTILINE_ENTRY_DRAW_FLAGS ( DrawTextFlags::WordBreak | DrawTextFlags::MultiLine | DrawTextFlags::VCenter )

using namespace ::com::sun::star;

constexpr tools::Long gnBorder = 1;

void ImplInitDropDownButton( PushButton* pButton )
{
    pButton->SetSymbol( SymbolType::SPIN_DOWN );

    if ( pButton->IsNativeControlSupported(ControlType::Listbox, ControlPart::Entire)
            && ! pButton->IsNativeControlSupported(ControlType::Listbox, ControlPart::ButtonDown) )
        pButton->SetBackground();
}

ImplEntryList::ImplEntryList( vcl::Window* pWindow )
{
    mpWindow = pWindow;
    mnLastSelected = LISTBOX_ENTRY_NOTFOUND;
    mnSelectionAnchor = LISTBOX_ENTRY_NOTFOUND;
    mnImages = 0;
    mbCallSelectionChangedHdl = true;

    mnMRUCount = 0;
    mnMaxMRUCount = 0;
}

ImplEntryList::~ImplEntryList()
{
    Clear();
}

void ImplEntryList::Clear()
{
    mnImages = 0;
    maEntries.clear();
}

void ImplEntryList::dispose()
{
    Clear();
    mpWindow.reset();
}

void ImplEntryList::SelectEntry( sal_Int32 nPos, bool bSelect )
{
    if (0 <= nPos && o3tl::make_unsigned(nPos) < maEntries.size())
    {
        std::vector<std::unique_ptr<ImplEntryType> >::iterator iter = maEntries.begin()+nPos;

        if ( ( (*iter)->mbIsSelected != bSelect ) &&
           ( ( (*iter)->mnFlags & ListBoxEntryFlags::DisableSelection) == ListBoxEntryFlags::NONE  ) )
        {
            (*iter)->mbIsSelected = bSelect;
            if ( mbCallSelectionChangedHdl )
                maSelectionChangedHdl.Call( nPos );
        }
    }
}

namespace
{
    comphelper::string::NaturalStringSorter& GetSorter()
    {
        static comphelper::string::NaturalStringSorter gSorter(
                    ::comphelper::getProcessComponentContext(),
                    Application::GetSettings().GetLanguageTag().getLocale());
        return gSorter;
    };
}

namespace vcl
{
    sal_Int32 NaturalSortCompare(const OUString &rA, const OUString &rB)
    {
        const comphelper::string::NaturalStringSorter &rSorter = GetSorter();
        return rSorter.compare(rA, rB);
    }
}

sal_Int32 ImplEntryList::InsertEntry( sal_Int32 nPos, ImplEntryType* pNewEntry, bool bSort )
{
    assert(nPos >= 0);
    assert(maEntries.size() < LISTBOX_MAX_ENTRIES);

    if ( !!pNewEntry->maImage )
        mnImages++;

    sal_Int32 insPos = 0;
    const sal_Int32 nEntriesSize = static_cast<sal_Int32>(maEntries.size());

    if ( !bSort || maEntries.empty())
    {
        if (0 <= nPos && nPos < nEntriesSize)
        {
            insPos = nPos;
            maEntries.insert( maEntries.begin() + nPos, std::unique_ptr<ImplEntryType>(pNewEntry) );
        }
        else
        {
            insPos = nEntriesSize;
            maEntries.push_back(std::unique_ptr<ImplEntryType>(pNewEntry));
        }
    }
    else
    {
        const comphelper::string::NaturalStringSorter &rSorter = GetSorter();

        const OUString& rStr = pNewEntry->maStr;

        ImplEntryType* pTemp = GetEntry( nEntriesSize-1 );

        try
        {
            sal_Int32 nComp = rSorter.compare(rStr, pTemp->maStr);

            // fast insert for sorted data
            if ( nComp >= 0 )
            {
                insPos = nEntriesSize;
                maEntries.push_back(std::unique_ptr<ImplEntryType>(pNewEntry));
            }
            else
            {
                pTemp = GetEntry( mnMRUCount );

                nComp = rSorter.compare(rStr, pTemp->maStr);
                if ( nComp <= 0 )
                {
                    insPos = 0;
                    maEntries.insert(maEntries.begin(), std::unique_ptr<ImplEntryType>(pNewEntry));
                }
                else
                {
                    sal_uLong nLow = mnMRUCount;
                    sal_uLong nHigh = maEntries.size()-1;
                    sal_Int32 nMid;

                    // binary search
                    do
                    {
                        nMid = static_cast<sal_Int32>((nLow + nHigh) / 2);
                        pTemp = GetEntry( nMid );

                        nComp = rSorter.compare(rStr, pTemp->maStr);

                        if ( nComp < 0 )
                            nHigh = nMid-1;
                        else
                        {
                            if ( nComp > 0 )
                                nLow = nMid + 1;
                            else
                                break;
                        }
                    }
                    while ( nLow <= nHigh );

                    if ( nComp >= 0 )
                        nMid++;

                    insPos = nMid;
                    maEntries.insert(maEntries.begin()+nMid, std::unique_ptr<ImplEntryType>(pNewEntry));
                }
            }
        }
        catch (uno::RuntimeException& )
        {
            // XXX this is arguable, if the exception occurred because pNewEntry is
            // garbage you wouldn't insert it. If the exception occurred because the
            // Collator implementation is garbage then give the user a chance to see
            // his stuff
            insPos = 0;
            maEntries.insert(maEntries.begin(), std::unique_ptr<ImplEntryType>(pNewEntry));
        }

    }

    return insPos;
}

void ImplEntryList::RemoveEntry( sal_Int32 nPos )
{
    if (0 <= nPos && o3tl::make_unsigned(nPos) < maEntries.size())
    {
        std::vector<std::unique_ptr<ImplEntryType> >::iterator iter = maEntries.begin()+ nPos;

        if ( !!(*iter)->maImage )
            mnImages--;

        maEntries.erase(iter);
    }
}

sal_Int32 ImplEntryList::FindEntry( std::u16string_view rString, bool bSearchMRUArea ) const
{
    const sal_Int32 nEntries = static_cast<sal_Int32>(maEntries.size());
    for ( sal_Int32 n = bSearchMRUArea ? 0 : GetMRUCount(); n < nEntries; n++ )
    {
        OUString aComp( vcl::I18nHelper::filterFormattingChars( maEntries[n]->maStr ) );
        if ( aComp == rString )
            return n;
    }
    return LISTBOX_ENTRY_NOTFOUND;
}

sal_Int32 ImplEntryList::FindMatchingEntry( const OUString& rStr, sal_Int32 nStart, bool bLazy ) const
{
    sal_Int32  nPos = LISTBOX_ENTRY_NOTFOUND;
    sal_Int32  nEntryCount = GetEntryCount();

    const vcl::I18nHelper& rI18nHelper = mpWindow->GetSettings().GetLocaleI18nHelper();
    for ( sal_Int32 n = nStart; n < nEntryCount; )
    {
        ImplEntryType* pImplEntry = GetEntry( n );
        bool bMatch;
        if ( bLazy  )
        {
            bMatch = rI18nHelper.MatchString( rStr, pImplEntry->maStr );
        }
        else
        {
            bMatch = pImplEntry->maStr.startsWith(rStr);
        }
        if ( bMatch )
        {
            nPos = n;
            break;
        }

        n++;
    }

    return nPos;
}

tools::Long ImplEntryList::GetAddedHeight( sal_Int32 i_nEndIndex, sal_Int32 i_nBeginIndex ) const
{
    tools::Long nHeight = 0;
    sal_Int32 nStart = std::min(i_nEndIndex, i_nBeginIndex);
    sal_Int32 nStop  = std::max(i_nEndIndex, i_nBeginIndex);
    sal_Int32 nEntryCount = GetEntryCount();
    if( 0 <= nStop && nStop != LISTBOX_ENTRY_NOTFOUND && nEntryCount != 0 )
    {
        // sanity check
        if( nStop > nEntryCount-1 )
            nStop = nEntryCount-1;
        if (nStart < 0)
            nStart = 0;
        else if( nStart > nEntryCount-1 )
            nStart = nEntryCount-1;

        sal_Int32 nIndex = nStart;
        while( nIndex != LISTBOX_ENTRY_NOTFOUND && nIndex < nStop )
        {
            tools::Long nPosHeight = GetEntryPtr( nIndex )->getHeightWithMargin();
            if (nHeight > ::std::numeric_limits<tools::Long>::max() - nPosHeight)
            {
                SAL_WARN( "vcl", "ImplEntryList::GetAddedHeight: truncated");
                break;
            }
            nHeight += nPosHeight;
            nIndex++;
        }
    }
    else
        nHeight = 0;
    return i_nEndIndex > i_nBeginIndex ? nHeight : -nHeight;
}

tools::Long ImplEntryList::GetEntryHeight( sal_Int32 nPos ) const
{
    ImplEntryType* pImplEntry = GetEntry( nPos );
    return pImplEntry ? pImplEntry->getHeightWithMargin() : 0;
}

OUString ImplEntryList::GetEntryText( sal_Int32 nPos ) const
{
    OUString aEntryText;
    ImplEntryType* pImplEntry = GetEntry( nPos );
    if ( pImplEntry )
        aEntryText = pImplEntry->maStr;
    return aEntryText;
}

bool ImplEntryList::HasEntryImage( sal_Int32 nPos ) const
{
    bool bImage = false;
    ImplEntryType* pImplEntry = GetEntry( nPos );
    if ( pImplEntry )
        bImage = !!pImplEntry->maImage;
    return bImage;
}

Image ImplEntryList::GetEntryImage( sal_Int32 nPos ) const
{
    Image aImage;
    ImplEntryType* pImplEntry = GetEntry( nPos );
    if ( pImplEntry )
        aImage = pImplEntry->maImage;
    return aImage;
}

void ImplEntryList::SetEntryData( sal_Int32 nPos, void* pNewData )
{
    ImplEntryType* pImplEntry = GetEntry( nPos );
    if ( pImplEntry )
        pImplEntry->mpUserData = pNewData;
}

void* ImplEntryList::GetEntryData( sal_Int32 nPos ) const
{
    ImplEntryType* pImplEntry = GetEntry( nPos );
    return pImplEntry ? pImplEntry->mpUserData : nullptr;
}

void ImplEntryList::SetEntryFlags( sal_Int32 nPos, ListBoxEntryFlags nFlags )
{
    ImplEntryType* pImplEntry = GetEntry( nPos );
    if ( pImplEntry )
        pImplEntry->mnFlags = nFlags;
}

sal_Int32 ImplEntryList::GetSelectedEntryCount() const
{
    sal_Int32 nSelCount = 0;
    for ( sal_Int32 n = GetEntryCount(); n; )
    {
        ImplEntryType* pImplEntry = GetEntry( --n );
        if ( pImplEntry->mbIsSelected )
            nSelCount++;
    }
    return nSelCount;
}

OUString ImplEntryList::GetSelectedEntry( sal_Int32 nIndex ) const
{
    return GetEntryText( GetSelectedEntryPos( nIndex ) );
}

sal_Int32 ImplEntryList::GetSelectedEntryPos( sal_Int32 nIndex ) const
{
    sal_Int32 nSelEntryPos = LISTBOX_ENTRY_NOTFOUND;
    sal_Int32 nSel = 0;
    sal_Int32 nEntryCount = GetEntryCount();

    for ( sal_Int32 n = 0; n < nEntryCount; n++ )
    {
        ImplEntryType* pImplEntry = GetEntry( n );
        if ( pImplEntry->mbIsSelected )
        {
            if ( nSel == nIndex )
            {
                nSelEntryPos = n;
                break;
            }
            nSel++;
        }
    }

    return nSelEntryPos;
}

bool ImplEntryList::IsEntryPosSelected( sal_Int32 nIndex ) const
{
    ImplEntryType* pImplEntry = GetEntry( nIndex );
    return pImplEntry && pImplEntry->mbIsSelected;
}

bool ImplEntryList::IsEntrySelectable( sal_Int32 nPos ) const
{
    ImplEntryType* pImplEntry = GetEntry( nPos );
    return pImplEntry == nullptr || ((pImplEntry->mnFlags & ListBoxEntryFlags::DisableSelection) == ListBoxEntryFlags::NONE);
}

sal_Int32 ImplEntryList::FindFirstSelectable( sal_Int32 nPos, bool bForward /* = true */ ) const
{
    if( IsEntrySelectable( nPos ) )
        return nPos;

    if( bForward )
    {
        for( nPos = nPos + 1; nPos < GetEntryCount(); nPos++ )
        {
            if( IsEntrySelectable( nPos ) )
                return nPos;
        }
    }
    else
    {
        while( nPos )
        {
            nPos--;
            if( IsEntrySelectable( nPos ) )
                return nPos;
        }
    }

    return LISTBOX_ENTRY_NOTFOUND;
}

ImplListBoxWindow::ImplListBoxWindow( vcl::Window* pParent, WinBits nWinStyle ) :
    Control( pParent, 0 ),
    maEntryList( this ),
    maQuickSelectionEngine( *this )
{

    mnTop               = 0;
    mnLeft              = 0;
    mnSelectModifier    = 0;
    mnUserDrawEntry     = LISTBOX_ENTRY_NOTFOUND;
    mbTrack             = false;
    mbTravelSelect      = false;
    mbTrackingSelect    = false;
    mbSelectionChanged  = false;
    mbMouseMoveSelect   = false;
    mbMulti             = false;
    mbGrabFocus         = false;
    mbUserDrawEnabled   = false;
    mbInUserDraw        = false;
    mbReadOnly          = false;
    mbHasFocusRect      = false;
    mbRight             = ( nWinStyle & WB_RIGHT );
    mbCenter            = ( nWinStyle & WB_CENTER );
    mbSimpleMode        = ( nWinStyle & WB_SIMPLEMODE );
    mbSort              = ( nWinStyle & WB_SORT );
    mbIsDropdown        = ( nWinStyle & WB_DROPDOWN );
    mbEdgeBlending      = false;

    mnCurrentPos            = LISTBOX_ENTRY_NOTFOUND;
    mnTrackingSaveSelection = LISTBOX_ENTRY_NOTFOUND;

    GetOutDev()->SetLineColor();
    SetTextFillColor();
    SetBackground( Wallpaper( GetSettings().GetStyleSettings().GetFieldColor() ) );

    ApplySettings(*GetOutDev());
    ImplCalcMetrics();
}

ImplListBoxWindow::~ImplListBoxWindow()
{
    disposeOnce();
}

void ImplListBoxWindow::dispose()
{
    maEntryList.dispose();
    Control::dispose();
}

void ImplListBoxWindow::ApplySettings(vcl::RenderContext& rRenderContext)
{
    const StyleSettings& rStyleSettings = rRenderContext.GetSettings().GetStyleSettings();

    ApplyControlFont(rRenderContext, rStyleSettings.GetFieldFont());
    ApplyControlForeground(rRenderContext, rStyleSettings.GetListBoxWindowTextColor());

    if (IsControlBackground())
        rRenderContext.SetBackground(GetControlBackground());
    else
        rRenderContext.SetBackground(rStyleSettings.GetListBoxWindowBackgroundColor());
}

void ImplListBoxWindow::ImplCalcMetrics()
{
    mnMaxWidth      = 0;
    mnMaxTxtWidth   = 0;
    mnMaxImgWidth   = 0;
    mnMaxImgTxtWidth= 0;
    mnMaxImgHeight  = 0;

    mnTextHeight = static_cast<sal_uInt16>(GetTextHeight());
    mnMaxTxtHeight = mnTextHeight + gnBorder;
    mnMaxHeight = mnMaxTxtHeight;

    if ( maUserItemSize.Height() > mnMaxHeight )
        mnMaxHeight = static_cast<sal_uInt16>(maUserItemSize.Height());
    if ( maUserItemSize.Width() > mnMaxWidth )
        mnMaxWidth= static_cast<sal_uInt16>(maUserItemSize.Width());

    for ( sal_Int32 n = maEntryList.GetEntryCount(); n; )
    {
        ImplEntryType* pEntry = maEntryList.GetMutableEntryPtr( --n );
        ImplUpdateEntryMetrics( *pEntry );
    }

    if( mnCurrentPos != LISTBOX_ENTRY_NOTFOUND )
    {
        Size aSz( GetOutputSizePixel().Width(), maEntryList.GetEntryPtr( mnCurrentPos )->getHeightWithMargin() );
        maFocusRect.SetSize( aSz );
    }
}

void ImplListBoxWindow::Clear()
{
    maEntryList.Clear();

    mnMaxHeight     = mnMaxTxtHeight;
    mnMaxWidth      = 0;
    mnMaxTxtWidth   = 0;
    mnMaxImgTxtWidth= 0;
    mnMaxImgWidth   = 0;
    mnMaxImgHeight  = 0;
    mnTop           = 0;
    mnLeft          = 0;
    ImplClearLayoutData();

    mnCurrentPos = LISTBOX_ENTRY_NOTFOUND;
    maQuickSelectionEngine.Reset();

    Invalidate();
}

void ImplListBoxWindow::SetUserItemSize( const Size& rSz )
{
    ImplClearLayoutData();
    maUserItemSize = rSz;
    ImplCalcMetrics();
}

namespace {

struct ImplEntryMetrics
{
    bool    bText;
    bool    bImage;
    tools::Long    nEntryWidth;
    tools::Long    nEntryHeight;
    tools::Long    nTextWidth;
    tools::Long    nImgWidth;
    tools::Long    nImgHeight;
};

}

tools::Long ImplEntryType::getHeightWithMargin() const
{
    return mnHeight + ImplGetSVData()->maNWFData.mnListBoxEntryMargin;
}

SalLayoutGlyphs* ImplEntryType::GetTextGlyphs(const OutputDevice* pOutputDevice)
{
    if (maStrGlyphs.IsValid())
        // Use pre-calculated result.
        return &maStrGlyphs;

    std::unique_ptr<SalLayout> pLayout = pOutputDevice->ImplLayout(
        maStr, 0, maStr.getLength(), Point(0, 0), 0, {}, {}, SalLayoutFlags::GlyphItemsOnly);
    if (!pLayout)
        return nullptr;

    // Remember the calculation result.
    maStrGlyphs = pLayout->GetGlyphs();

    return &maStrGlyphs;
}

void ImplListBoxWindow::ImplUpdateEntryMetrics( ImplEntryType& rEntry )
{
    const bool bIsUserDrawEnabled = IsUserDrawEnabled();

    ImplEntryMetrics aMetrics;
    aMetrics.bText = !rEntry.maStr.isEmpty();
    aMetrics.bImage = !!rEntry.maImage;
    aMetrics.nEntryWidth = 0;
    aMetrics.nEntryHeight = 0;
    aMetrics.nTextWidth = 0;
    aMetrics.nImgWidth = 0;
    aMetrics.nImgHeight = 0;

    if (aMetrics.bText && !bIsUserDrawEnabled)
    {
        if( rEntry.mnFlags & ListBoxEntryFlags::MultiLine )
        {
            // multiline case
            Size aCurSize( PixelToLogic( GetSizePixel() ) );
            // set the current size to a large number
            // GetTextRect should shrink it to the actual size
            aCurSize.setHeight( 0x7fffff );
            tools::Rectangle aTextRect( Point( 0, 0 ), aCurSize );
            aTextRect = GetTextRect( aTextRect, rEntry.maStr, DrawTextFlags::WordBreak | DrawTextFlags::MultiLine );
            aMetrics.nTextWidth = aTextRect.GetWidth();
            if( aMetrics.nTextWidth > mnMaxTxtWidth )
                mnMaxTxtWidth = aMetrics.nTextWidth;
            aMetrics.nEntryWidth = mnMaxTxtWidth;
            aMetrics.nEntryHeight = aTextRect.GetHeight() + gnBorder;
        }
        else
        {
            // normal single line case
            const SalLayoutGlyphs* pGlyphs = rEntry.GetTextGlyphs(GetOutDev());
            aMetrics.nTextWidth
                = static_cast<sal_uInt16>(GetTextWidth(rEntry.maStr, 0, -1, nullptr, pGlyphs));
            if( aMetrics.nTextWidth > mnMaxTxtWidth )
                mnMaxTxtWidth = aMetrics.nTextWidth;
            aMetrics.nEntryWidth = mnMaxTxtWidth;
            aMetrics.nEntryHeight = mnTextHeight + gnBorder;
        }
    }
    if ( aMetrics.bImage )
    {
        Size aImgSz = rEntry.maImage.GetSizePixel();
        aMetrics.nImgWidth  = static_cast<sal_uInt16>(CalcZoom( aImgSz.Width() ));
        aMetrics.nImgHeight = static_cast<sal_uInt16>(CalcZoom( aImgSz.Height() ));

        if( aMetrics.nImgWidth > mnMaxImgWidth )
            mnMaxImgWidth = aMetrics.nImgWidth;
        if( aMetrics.nImgHeight > mnMaxImgHeight )
            mnMaxImgHeight = aMetrics.nImgHeight;

        mnMaxImgTxtWidth = std::max( mnMaxImgTxtWidth, aMetrics.nTextWidth );
        aMetrics.nEntryHeight = std::max( aMetrics.nImgHeight, aMetrics.nEntryHeight );

    }

    if (bIsUserDrawEnabled || aMetrics.bImage)
    {
        aMetrics.nEntryWidth = std::max( aMetrics.nImgWidth, maUserItemSize.Width() );
        if (!bIsUserDrawEnabled && aMetrics.bText)
            aMetrics.nEntryWidth += aMetrics.nTextWidth + IMG_TXT_DISTANCE;
        aMetrics.nEntryHeight = std::max( std::max( mnMaxImgHeight, maUserItemSize.Height() ) + 2,
                                     aMetrics.nEntryHeight );
    }

    if (!aMetrics.bText && !aMetrics.bImage && !bIsUserDrawEnabled)
    {
        // entries which have no (aka an empty) text, and no image,
        // and are not user-drawn, should be shown nonetheless
        aMetrics.nEntryHeight = mnTextHeight + gnBorder;
    }

    if ( aMetrics.nEntryWidth > mnMaxWidth )
        mnMaxWidth = aMetrics.nEntryWidth;
    if ( aMetrics.nEntryHeight > mnMaxHeight )
        mnMaxHeight = aMetrics.nEntryHeight;

    rEntry.mnHeight = aMetrics.nEntryHeight;
}

void ImplListBoxWindow::ImplCallSelect()
{
    if ( !IsTravelSelect() && GetEntryList().GetMaxMRUCount() )
    {
        // Insert the selected entry as MRU, if not already first MRU
        sal_Int32 nSelected = GetEntryList().GetSelectedEntryPos( 0 );
        sal_Int32 nMRUCount = GetEntryList().GetMRUCount();
        OUString aSelected = GetEntryList().GetEntryText( nSelected );
        sal_Int32 nFirstMatchingEntryPos = GetEntryList().FindEntry( aSelected, true );
        if ( nFirstMatchingEntryPos || !nMRUCount )
        {
            bool bSelectNewEntry = false;
            if ( nFirstMatchingEntryPos < nMRUCount )
            {
                RemoveEntry( nFirstMatchingEntryPos );
                nMRUCount--;
                if ( nFirstMatchingEntryPos == nSelected )
                    bSelectNewEntry = true;
            }
            else if ( nMRUCount == GetEntryList().GetMaxMRUCount() )
            {
                RemoveEntry( nMRUCount - 1 );
                nMRUCount--;
            }

            ImplClearLayoutData();

            ImplEntryType* pNewEntry = new ImplEntryType( aSelected );
            pNewEntry->mbIsSelected = bSelectNewEntry;
            GetEntryList().InsertEntry( 0, pNewEntry, false );
            ImplUpdateEntryMetrics( *pNewEntry );
            GetEntryList().SetMRUCount( ++nMRUCount );
            SetSeparatorPos( nMRUCount ? nMRUCount-1 : 0 );
            maMRUChangedHdl.Call( nullptr );
        }
    }

    maSelectHdl.Call( nullptr );
    mbSelectionChanged = false;
}

sal_Int32 ImplListBoxWindow::InsertEntry(sal_Int32 nPos, ImplEntryType* pNewEntry, bool bSort)
{
    assert(nPos >= 0);
    assert(maEntryList.GetEntryCount() < LISTBOX_MAX_ENTRIES);

    ImplClearLayoutData();
    sal_Int32 nNewPos = maEntryList.InsertEntry( nPos, pNewEntry, bSort );

    if( GetStyle() & WB_WORDBREAK )
        pNewEntry->mnFlags |= ListBoxEntryFlags::MultiLine;

    ImplUpdateEntryMetrics( *pNewEntry );
    return nNewPos;
}

sal_Int32 ImplListBoxWindow::InsertEntry( sal_Int32 nPos, ImplEntryType* pNewEntry )
{
    return InsertEntry(nPos, pNewEntry, mbSort);
}

void ImplListBoxWindow::RemoveEntry( sal_Int32 nPos )
{
    ImplClearLayoutData();
    maEntryList.RemoveEntry( nPos );
    if( mnCurrentPos >= maEntryList.GetEntryCount() )
        mnCurrentPos = LISTBOX_ENTRY_NOTFOUND;
    ImplCalcMetrics();
}

void ImplListBoxWindow::SetEntryFlags( sal_Int32 nPos, ListBoxEntryFlags nFlags )
{
    maEntryList.SetEntryFlags( nPos, nFlags );
    ImplEntryType* pEntry = maEntryList.GetMutableEntryPtr( nPos );
    if( pEntry )
        ImplUpdateEntryMetrics( *pEntry );
}

void ImplListBoxWindow::ImplShowFocusRect()
{
    if ( mbHasFocusRect )
        HideFocus();
    ShowFocus( maFocusRect );
    mbHasFocusRect = true;
}

void ImplListBoxWindow::ImplHideFocusRect()
{
    if ( mbHasFocusRect )
    {
        HideFocus();
        mbHasFocusRect = false;
    }
}

sal_Int32 ImplListBoxWindow::GetEntryPosForPoint( const Point& rPoint ) const
{
    tools::Long nY = gnBorder;

    sal_Int32 nSelect = mnTop;
    const ImplEntryType* pEntry = maEntryList.GetEntryPtr( nSelect );
    while (pEntry)
    {
        tools::Long nEntryHeight = pEntry->getHeightWithMargin();
        if (rPoint.Y() <= nEntryHeight + nY)
            break;
        nY += nEntryHeight;
        pEntry = maEntryList.GetEntryPtr( ++nSelect );
    }
    if( pEntry == nullptr )
        nSelect = LISTBOX_ENTRY_NOTFOUND;

    return nSelect;
}

bool ImplListBoxWindow::IsVisible( sal_Int32 i_nEntry ) const
{
    bool bRet = false;

    if( i_nEntry >= mnTop )
    {
        if( maEntryList.GetAddedHeight( i_nEntry, mnTop ) <
            PixelToLogic( GetSizePixel() ).Height() )
        {
            bRet = true;
        }
    }

    return bRet;
}

tools::Long ImplListBoxWindow::GetEntryHeightWithMargin() const
{
    tools::Long nMargin = ImplGetSVData()->maNWFData.mnListBoxEntryMargin;
    return mnMaxHeight + nMargin;
}

sal_Int32 ImplListBoxWindow::GetLastVisibleEntry() const
{
    sal_Int32 nPos = mnTop;
    tools::Long nWindowHeight = GetSizePixel().Height();
    sal_Int32 nCount = maEntryList.GetEntryCount();
    tools::Long nDiff;
    for( nDiff = 0; nDiff < nWindowHeight && nPos < nCount; nDiff = maEntryList.GetAddedHeight( nPos, mnTop ) )
        nPos++;

    if( nDiff > nWindowHeight && nPos > mnTop )
        nPos--;

    if( nPos >= nCount )
        nPos = nCount-1;

    return nPos;
}

void ImplListBoxWindow::MouseButtonDown( const MouseEvent& rMEvt )
{
    mbMouseMoveSelect = false;  // only till the first MouseButtonDown
    maQuickSelectionEngine.Reset();

    if ( !IsReadOnly() )
    {
        if( rMEvt.GetClicks() == 1 )
        {
            sal_Int32 nSelect = GetEntryPosForPoint( rMEvt.GetPosPixel() );
            if( nSelect != LISTBOX_ENTRY_NOTFOUND )
            {
                if ( !mbMulti && GetEntryList().GetSelectedEntryCount() )
                    mnTrackingSaveSelection = GetEntryList().GetSelectedEntryPos( 0 );
                else
                    mnTrackingSaveSelection = LISTBOX_ENTRY_NOTFOUND;

                mnCurrentPos = nSelect;
                mbTrackingSelect = true;
                bool bCurPosChange = (mnCurrentPos != nSelect);
                (void)SelectEntries( nSelect, LET_MBDOWN, rMEvt.IsShift(), rMEvt.IsMod1() ,bCurPosChange);
                mbTrackingSelect = false;
                if ( mbGrabFocus )
                    GrabFocus();

                StartTracking( StartTrackingFlags::ScrollRepeat );
            }
        }
        if( rMEvt.GetClicks() == 2 )
        {
            maDoubleClickHdl.Call( this );
        }
    }
    else // if ( mbGrabFocus )
    {
        GrabFocus();
    }
}

void ImplListBoxWindow::MouseMove( const MouseEvent& rMEvt )
{
    if (rMEvt.IsLeaveWindow() || mbMulti || !IsMouseMoveSelect() || !maEntryList.GetEntryCount())
        return;

    tools::Rectangle aRect( Point(), GetOutputSizePixel() );
    if( !aRect.Contains( rMEvt.GetPosPixel() ) )
        return;

    // prevent initial mouse move event from selecting an entry when the listbox is started
    if (mnLastPosPixel == Point())
    {
        mnLastPosPixel = rMEvt.GetPosPixel();
        return;
    }
    if (mnLastPosPixel == rMEvt.GetPosPixel())
        return;

    if ( IsMouseMoveSelect() )
    {
        sal_Int32 nSelect = GetEntryPosForPoint( rMEvt.GetPosPixel() );
        if( nSelect == LISTBOX_ENTRY_NOTFOUND )
            nSelect = maEntryList.GetEntryCount() - 1;
        nSelect = std::min( nSelect, GetLastVisibleEntry() );
        nSelect = std::min( nSelect, static_cast<sal_Int32>( maEntryList.GetEntryCount() - 1 ) );
        // Select only visible Entries with MouseMove, otherwise Tracking...
        if ( IsVisible( nSelect ) &&
            maEntryList.IsEntrySelectable( nSelect ) &&
            ( ( nSelect != mnCurrentPos ) || !GetEntryList().GetSelectedEntryCount() || ( nSelect != GetEntryList().GetSelectedEntryPos( 0 ) ) ) )
        {
            mbTrackingSelect = true;
            if ( SelectEntries( nSelect, LET_TRACKING ) )
            {
                // When list box selection change by mouse move, notify
                // VclEventId::ListboxSelect vcl event.
                maListItemSelectHdl.Call(nullptr);
            }
            mbTrackingSelect = false;
        }
    }

    // if the DD button was pressed and someone moved into the ListBox
    // with the mouse button pressed...
    if ( rMEvt.IsLeft() && !rMEvt.IsSynthetic() )
    {
        if ( !mbMulti && GetEntryList().GetSelectedEntryCount() )
            mnTrackingSaveSelection = GetEntryList().GetSelectedEntryPos( 0 );
        else
            mnTrackingSaveSelection = LISTBOX_ENTRY_NOTFOUND;

        StartTracking( StartTrackingFlags::ScrollRepeat );
    }
}

void ImplListBoxWindow::DeselectAll()
{
    while ( GetEntryList().GetSelectedEntryCount() )
    {
        sal_Int32 nS = GetEntryList().GetSelectedEntryPos( 0 );
        SelectEntry( nS, false );
    }
}

void ImplListBoxWindow::SelectEntry( sal_Int32 nPos, bool bSelect )
{
    if( (maEntryList.IsEntryPosSelected( nPos ) == bSelect) || !maEntryList.IsEntrySelectable( nPos ) )
        return;

    ImplHideFocusRect();
    if( bSelect )
    {
        if( !mbMulti )
        {
            // deselect the selected entry
            sal_Int32 nDeselect = GetEntryList().GetSelectedEntryPos( 0 );
            if( nDeselect != LISTBOX_ENTRY_NOTFOUND )
            {
                //SelectEntryPos( nDeselect, false );
                GetEntryList().SelectEntry( nDeselect, false );
                if (IsUpdateMode() && IsReallyVisible())
                    Invalidate();
            }
        }
        maEntryList.SelectEntry( nPos, true );
        mnCurrentPos = nPos;
        if ( ( nPos != LISTBOX_ENTRY_NOTFOUND ) && IsUpdateMode() )
        {
            Invalidate();
            if ( !IsVisible( nPos ) )
            {
                ImplClearLayoutData();
                sal_Int32 nVisibleEntries = GetLastVisibleEntry()-mnTop;
                if ( !nVisibleEntries || !IsReallyVisible() || ( nPos < GetTopEntry() ) )
                {
                    Resize();
                    ShowProminentEntry( nPos );
                }
                else
                {
                    ShowProminentEntry( nPos );
                }
            }
        }
    }
    else
    {
        maEntryList.SelectEntry( nPos, false );
        Invalidate();
    }
    mbSelectionChanged = true;
}

bool ImplListBoxWindow::SelectEntries( sal_Int32 nSelect, LB_EVENT_TYPE eLET, bool bShift, bool bCtrl, bool bSelectPosChange /*=FALSE*/ )
{
    bool bSelectionChanged = false;

    if( IsEnabled() && maEntryList.IsEntrySelectable( nSelect ) )
    {
        bool bFocusChanged = false;

        // here (Single-ListBox) only one entry can be deselected
        if( !mbMulti )
        {
            sal_Int32 nDeselect = maEntryList.GetSelectedEntryPos( 0 );
            if( nSelect != nDeselect )
            {
                SelectEntry( nSelect, true );
                maEntryList.SetLastSelected( nSelect );
                bFocusChanged = true;
                bSelectionChanged = true;
            }
        }
        // MultiListBox without Modifier
        else if( mbSimpleMode && !bCtrl && !bShift )
        {
            sal_Int32 nEntryCount = maEntryList.GetEntryCount();
            for ( sal_Int32 nPos = 0; nPos < nEntryCount; nPos++ )
            {
                bool bSelect = nPos == nSelect;
                if ( maEntryList.IsEntryPosSelected( nPos ) != bSelect )
                {
                    SelectEntry( nPos, bSelect );
                    bFocusChanged = true;
                    bSelectionChanged = true;
                }
            }
            maEntryList.SetLastSelected( nSelect );
            maEntryList.SetSelectionAnchor( nSelect );
        }
        // MultiListBox only with CTRL/SHIFT or not in SimpleMode
        else if( ( !mbSimpleMode /* && !bShift */ ) || ( mbSimpleMode && ( bCtrl || bShift ) ) )
        {
            // Space for selection change
            if( !bShift && ( ( eLET == LET_KEYSPACE ) || ( eLET == LET_MBDOWN ) ) )
            {
                bool bSelect = !maEntryList.IsEntryPosSelected( nSelect );
                SelectEntry( nSelect, bSelect );
                maEntryList.SetLastSelected( nSelect );
                maEntryList.SetSelectionAnchor( nSelect );
                if ( !maEntryList.IsEntryPosSelected( nSelect ) )
                    maEntryList.SetSelectionAnchor( LISTBOX_ENTRY_NOTFOUND );
                bFocusChanged = true;
                bSelectionChanged = true;
            }
            else if( ( ( eLET == LET_TRACKING ) && ( nSelect != mnCurrentPos ) ) ||
                     ( bShift && ( ( eLET == LET_KEYMOVE ) || ( eLET == LET_MBDOWN ) ) ) )
            {
                mnCurrentPos = nSelect;
                bFocusChanged = true;

                sal_Int32 nAnchor = maEntryList.GetSelectionAnchor();
                if( ( nAnchor == LISTBOX_ENTRY_NOTFOUND ) && maEntryList.GetSelectedEntryCount() )
                {
                    nAnchor = maEntryList.GetSelectedEntryPos( maEntryList.GetSelectedEntryCount() - 1 );
                }
                if( nAnchor != LISTBOX_ENTRY_NOTFOUND )
                {
                    // All entries from Anchor to nSelect have to be selected
                    sal_Int32 nStart = std::min( nSelect, nAnchor );
                    sal_Int32 nEnd = std::max( nSelect, nAnchor );
                    for ( sal_Int32 n = nStart; n <= nEnd; n++ )
                    {
                        if ( !maEntryList.IsEntryPosSelected( n ) )
                        {
                            SelectEntry( n, true );
                            bSelectionChanged = true;
                        }
                    }

                    // if appropriate some more has to be deselected...
                    sal_Int32 nLast = maEntryList.GetLastSelected();
                    if ( nLast != LISTBOX_ENTRY_NOTFOUND )
                    {
                        if ( ( nLast > nSelect ) && ( nLast > nAnchor ) )
                        {
                            for ( sal_Int32 n = nSelect+1; n <= nLast; n++ )
                            {
                                if ( maEntryList.IsEntryPosSelected( n ) )
                                {
                                    SelectEntry( n, false );
                                    bSelectionChanged = true;
                                }
                            }
                        }
                        else if ( ( nLast < nSelect ) && ( nLast < nAnchor ) )
                        {
                            for ( sal_Int32 n = nLast; n < nSelect; n++ )
                            {
                                if ( maEntryList.IsEntryPosSelected( n ) )
                                {
                                    SelectEntry( n, false );
                                    bSelectionChanged = true;
                                }
                            }
                        }
                    }
                    maEntryList.SetLastSelected( nSelect );
                }
            }
            else if( eLET != LET_TRACKING )
            {
                ImplHideFocusRect();
                Invalidate();
                bFocusChanged = true;
            }
        }
        else if( bShift )
        {
            bFocusChanged = true;
        }

        if( bSelectionChanged )
            mbSelectionChanged = true;

        if( bFocusChanged )
        {
            tools::Long nHeightDiff = maEntryList.GetAddedHeight( nSelect, mnTop );
            maFocusRect.SetPos( Point( 0, nHeightDiff ) );
            Size aSz( maFocusRect.GetWidth(),
                      maEntryList.GetEntryHeight( nSelect ) );
            maFocusRect.SetSize( aSz );
            if( HasFocus() )
                ImplShowFocusRect();
            if (bSelectPosChange)
            {
                maFocusHdl.Call(nSelect);
            }
        }
        ImplClearLayoutData();
    }
    return bSelectionChanged;
}

void ImplListBoxWindow::Tracking( const TrackingEvent& rTEvt )
{
    tools::Rectangle aRect( Point(), GetOutputSizePixel() );
    bool bInside = aRect.Contains( rTEvt.GetMouseEvent().GetPosPixel() );

    if( rTEvt.IsTrackingCanceled() || rTEvt.IsTrackingEnded() ) // MouseButtonUp
    {
        if ( bInside && !rTEvt.IsTrackingCanceled() )
        {
            mnSelectModifier = rTEvt.GetMouseEvent().GetModifier();
            ImplCallSelect();
        }
        else
        {
            maCancelHdl.Call( nullptr );
            if ( !mbMulti )
            {
                mbTrackingSelect = true;
                SelectEntry( mnTrackingSaveSelection, true );
                mbTrackingSelect = false;
                if ( mnTrackingSaveSelection != LISTBOX_ENTRY_NOTFOUND )
                {
                    tools::Long nHeightDiff = maEntryList.GetAddedHeight( mnCurrentPos, mnTop );
                    maFocusRect.SetPos( Point( 0, nHeightDiff ) );
                    Size aSz( maFocusRect.GetWidth(),
                              maEntryList.GetEntryHeight( mnCurrentPos ) );
                    maFocusRect.SetSize( aSz );
                    ImplShowFocusRect();
                }
            }
        }

        mbTrack = false;
    }
    else
    {
        bool bTrackOrQuickClick = mbTrack;
        if( !mbTrack )
        {
            if ( bInside )
            {
                mbTrack = true;
            }

            // this case only happens, if the mouse button is pressed very briefly
            if( rTEvt.IsTrackingEnded() && mbTrack )
            {
                bTrackOrQuickClick = true;
                mbTrack = false;
            }
        }

        if( bTrackOrQuickClick )
        {
            MouseEvent aMEvt = rTEvt.GetMouseEvent();
            Point aPt( aMEvt.GetPosPixel() );
            bool bShift = aMEvt.IsShift();
            bool bCtrl  = aMEvt.IsMod1();

            sal_Int32 nSelect = LISTBOX_ENTRY_NOTFOUND;
            if( aPt.Y() < 0 )
            {
                if ( mnCurrentPos != LISTBOX_ENTRY_NOTFOUND )
                {
                    nSelect = mnCurrentPos ? ( mnCurrentPos - 1 ) : 0;
                    if( nSelect < mnTop )
                        SetTopEntry( mnTop-1 );
                }
            }
            else if( aPt.Y() > GetOutputSizePixel().Height() )
            {
                if ( mnCurrentPos != LISTBOX_ENTRY_NOTFOUND )
                {
                    nSelect = std::min(  static_cast<sal_Int32>(mnCurrentPos+1), static_cast<sal_Int32>(maEntryList.GetEntryCount()-1) );
                    if( nSelect >= GetLastVisibleEntry() )
                        SetTopEntry( mnTop+1 );
                }
            }
            else
            {
                nSelect = static_cast<sal_Int32>( ( aPt.Y() + gnBorder ) / mnMaxHeight ) + mnTop;
                nSelect = std::min( nSelect, GetLastVisibleEntry() );
                nSelect = std::min( nSelect, static_cast<sal_Int32>( maEntryList.GetEntryCount() - 1 ) );
            }

            if ( bInside )
            {
                if ( ( nSelect != mnCurrentPos ) || !GetEntryList().GetSelectedEntryCount() )
                {
                    mbTrackingSelect = true;
                    SelectEntries(nSelect, LET_TRACKING, bShift, bCtrl);
                    mbTrackingSelect = false;
                }
            }
            else
            {
                if ( !mbMulti && GetEntryList().GetSelectedEntryCount() )
                {
                    mbTrackingSelect = true;
                    SelectEntry( GetEntryList().GetSelectedEntryPos( 0 ), false );
                    mbTrackingSelect = false;
                }
            }
            mnCurrentPos = nSelect;
            if ( mnCurrentPos == LISTBOX_ENTRY_NOTFOUND )
            {
                ImplHideFocusRect();
            }
            else
            {
                tools::Long nHeightDiff = maEntryList.GetAddedHeight( mnCurrentPos, mnTop );
                maFocusRect.SetPos( Point( 0, nHeightDiff ) );
                Size aSz( maFocusRect.GetWidth(), maEntryList.GetEntryHeight( mnCurrentPos ) );
                maFocusRect.SetSize( aSz );
                ImplShowFocusRect();
            }
        }
    }
}

void ImplListBoxWindow::KeyInput( const KeyEvent& rKEvt )
{
    if( !ProcessKeyInput( rKEvt ) )
        Control::KeyInput( rKEvt );
}

bool ImplListBoxWindow::ProcessKeyInput( const KeyEvent& rKEvt )
{
    // entry to be selected
    sal_Int32 nSelect = LISTBOX_ENTRY_NOTFOUND;
    LB_EVENT_TYPE eLET = LET_KEYMOVE;

    vcl::KeyCode aKeyCode = rKEvt.GetKeyCode();

    bool bShift = aKeyCode.IsShift();
    bool bCtrl  = aKeyCode.IsMod1() || aKeyCode.IsMod3();
    bool bMod2 = aKeyCode.IsMod2();
    bool bDone = false;
    bool bHandleKey = false;

    switch( aKeyCode.GetCode() )
    {
        case KEY_UP:
        {
            if ( IsReadOnly() )
            {
                if ( GetTopEntry() )
                    SetTopEntry( GetTopEntry()-1 );
            }
            else if ( !bMod2 )
            {
                if( mnCurrentPos == LISTBOX_ENTRY_NOTFOUND )
                {
                    nSelect = maEntryList.FindFirstSelectable( 0 );
                }
                else if ( mnCurrentPos )
                {
                    // search first selectable above the current position
                    nSelect = maEntryList.FindFirstSelectable( mnCurrentPos - 1, false );
                }

                if( ( nSelect != LISTBOX_ENTRY_NOTFOUND ) && ( nSelect < mnTop ) )
                    SetTopEntry( mnTop-1 );

                bDone = true;
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_DOWN:
        {
            if ( IsReadOnly() )
            {
                SetTopEntry( GetTopEntry()+1 );
            }
            else if ( !bMod2 )
            {
                if( mnCurrentPos == LISTBOX_ENTRY_NOTFOUND )
                {
                    nSelect = maEntryList.FindFirstSelectable( 0 );
                }
                else if ( (mnCurrentPos+1) < maEntryList.GetEntryCount() )
                {
                    // search first selectable below the current position
                    nSelect = maEntryList.FindFirstSelectable( mnCurrentPos + 1 );
                }

                if( ( nSelect != LISTBOX_ENTRY_NOTFOUND ) && ( nSelect >= GetLastVisibleEntry() ) )
                    SetTopEntry( mnTop+1 );

                bDone = true;
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_PAGEUP:
        {
            if ( IsReadOnly() )
            {
                sal_Int32 nCurVis = GetLastVisibleEntry() - mnTop +1;
                SetTopEntry( ( mnTop > nCurVis ) ?
                                (mnTop-nCurVis) : 0 );
            }
            else if ( !bCtrl && !bMod2 )
            {
                if( mnCurrentPos == LISTBOX_ENTRY_NOTFOUND )
                {
                    nSelect = maEntryList.FindFirstSelectable( 0 );
                }
                else if ( mnCurrentPos )
                {
                    if( mnCurrentPos == mnTop )
                    {
                        sal_Int32 nCurVis = GetLastVisibleEntry() - mnTop +1;
                        SetTopEntry( ( mnTop > nCurVis ) ? ( mnTop-nCurVis+1 ) : 0 );
                    }

                    // find first selectable starting from mnTop looking forward
                    nSelect = maEntryList.FindFirstSelectable( mnTop );
                }
                bDone = true;
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_PAGEDOWN:
        {
            if ( IsReadOnly() )
            {
                SetTopEntry( GetLastVisibleEntry() );
            }
            else if ( !bCtrl && !bMod2 )
            {
                if( mnCurrentPos == LISTBOX_ENTRY_NOTFOUND )
                {
                    nSelect = maEntryList.FindFirstSelectable( 0 );
                }
                else if ( (mnCurrentPos+1) < maEntryList.GetEntryCount() )
                {
                    sal_Int32 nCount = maEntryList.GetEntryCount();
                    sal_Int32 nCurVis = GetLastVisibleEntry() - mnTop;
                    sal_Int32 nTmp = std::min( nCurVis, nCount );
                    nTmp += mnTop - 1;
                    if( mnCurrentPos == nTmp && mnCurrentPos != nCount - 1 )
                    {
                        tools::Long nTmp2 = std::min( static_cast<tools::Long>(nCount-nCurVis), static_cast<tools::Long>(static_cast<tools::Long>(mnTop)+static_cast<tools::Long>(nCurVis)-1) );
                        nTmp2 = std::max( tools::Long(0) , nTmp2 );
                        nTmp = static_cast<sal_Int32>(nTmp2+(nCurVis-1) );
                        SetTopEntry( static_cast<sal_Int32>(nTmp2) );
                    }
                    // find first selectable starting from nTmp looking backwards
                    nSelect = maEntryList.FindFirstSelectable( nTmp, false );
                }
                bDone = true;
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_HOME:
        {
            if ( IsReadOnly() )
            {
                SetTopEntry( 0 );
            }
            else if ( !bCtrl && !bMod2 &&  mnCurrentPos )
            {
                nSelect = maEntryList.FindFirstSelectable( maEntryList.GetEntryCount() ? 0 : LISTBOX_ENTRY_NOTFOUND );
                if( mnTop != 0 )
                    SetTopEntry( 0 );

                bDone = true;
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_END:
        {
            if ( IsReadOnly() )
            {
                SetTopEntry( 0xFFFF );
            }
            else if ( !bCtrl && !bMod2 )
            {
                if( mnCurrentPos == LISTBOX_ENTRY_NOTFOUND )
                {
                    nSelect = maEntryList.FindFirstSelectable( 0 );
                }
                else if ( (mnCurrentPos+1) < maEntryList.GetEntryCount() )
                {
                    sal_Int32 nCount = maEntryList.GetEntryCount();
                    nSelect = maEntryList.FindFirstSelectable( nCount - 1, false );
                    sal_Int32 nCurVis = GetLastVisibleEntry() - mnTop + 1;
                    if( nCount > nCurVis )
                        SetTopEntry( nCount - nCurVis );
                }
                bDone = true;
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_LEFT:
        {
            if ( !bCtrl && !bMod2 )
            {
                ScrollHorz( -HORZ_SCROLL );
                bDone = true;
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_RIGHT:
        {
            if ( !bCtrl && !bMod2 )
            {
                ScrollHorz( HORZ_SCROLL );
                bDone = true;
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_RETURN:
        {
            if ( !bMod2 && !IsReadOnly() )
            {
                mnSelectModifier = rKEvt.GetKeyCode().GetModifier();
                ImplCallSelect();
                bDone = false;  // do not catch RETURN
            }
            maQuickSelectionEngine.Reset();
        }
        break;

        case KEY_SPACE:
        {
            if ( !bMod2 && !IsReadOnly() )
            {
                if( mbMulti && ( !mbSimpleMode || ( mbSimpleMode && bCtrl && !bShift ) ) )
                {
                    nSelect = mnCurrentPos;
                    eLET = LET_KEYSPACE;
                }
                bDone = true;
            }
            bHandleKey = true;
        }
        break;

        case KEY_A:
        {
            if( bCtrl && mbMulti )
            {
                // paint only once
                bool bUpdates = IsUpdateMode();
                SetUpdateMode( false );

                sal_Int32 nEntryCount = maEntryList.GetEntryCount();
                for( sal_Int32 i = 0; i < nEntryCount; i++ )
                    SelectEntry( i, true );

                // tdf#97066 - Update selected items
                ImplCallSelect();

                // restore update mode
                SetUpdateMode( bUpdates );
                Invalidate();

                maQuickSelectionEngine.Reset();

                bDone = true;
            }
            else
            {
                bHandleKey = true;
            }
        }
        break;

        default:
            bHandleKey = true;
            break;
    }
    if (bHandleKey && !IsReadOnly())
    {
        bDone = maQuickSelectionEngine.HandleKeyEvent( rKEvt );
    }

    if  (   ( nSelect != LISTBOX_ENTRY_NOTFOUND )
        &&  (   ( !maEntryList.IsEntryPosSelected( nSelect ) )
            ||  ( eLET == LET_KEYSPACE )
            )
        )
    {
        SAL_WARN_IF( maEntryList.IsEntryPosSelected( nSelect ) && !mbMulti, "vcl", "ImplListBox: Selecting same Entry" );
        sal_Int32 nCount = maEntryList.GetEntryCount();
        if (nSelect >= nCount)
            nSelect = nCount ? nCount-1 : LISTBOX_ENTRY_NOTFOUND;
        bool bCurPosChange = (mnCurrentPos != nSelect);
        mnCurrentPos = nSelect;
        if(SelectEntries( nSelect, eLET, bShift, bCtrl, bCurPosChange))
        {
            // tdf#129043 Correctly deliver events when changing values with arrow keys in combobox
            if (mbIsDropdown && IsReallyVisible())
                mbTravelSelect = true;
            mnSelectModifier = rKEvt.GetKeyCode().GetModifier();
            ImplCallSelect();
            mbTravelSelect = false;
        }
    }

    return bDone;
}

namespace
{
    vcl::StringEntryIdentifier lcl_getEntry( const ImplEntryList& _rList, sal_Int32 _nPos, OUString& _out_entryText )
    {
        OSL_PRECOND( ( _nPos != LISTBOX_ENTRY_NOTFOUND ), "lcl_getEntry: invalid position!" );
        sal_Int32 nEntryCount( _rList.GetEntryCount() );
        if ( _nPos >= nEntryCount )
            _nPos = 0;
        _out_entryText = _rList.GetEntryText( _nPos );

        // vcl::StringEntryIdentifier does not allow for 0 values, but our position is 0-based
        // => normalize
        return reinterpret_cast< vcl::StringEntryIdentifier >( _nPos + sal_IntPtr(1) );
    }

    sal_Int32 lcl_getEntryPos( vcl::StringEntryIdentifier _entry )
    {
        // our pos is 0-based, but StringEntryIdentifier does not allow for a NULL
        return static_cast< sal_Int32 >( reinterpret_cast< sal_Int64 >( _entry ) ) - 1;
    }
}

vcl::StringEntryIdentifier ImplListBoxWindow::CurrentEntry( OUString& _out_entryText ) const
{
    return lcl_getEntry( GetEntryList(), ( mnCurrentPos == LISTBOX_ENTRY_NOTFOUND ) ? 0 : mnCurrentPos, _out_entryText );
}

vcl::StringEntryIdentifier ImplListBoxWindow::NextEntry( vcl::StringEntryIdentifier _currentEntry, OUString& _out_entryText ) const
{
    sal_Int32 nNextPos = lcl_getEntryPos( _currentEntry ) + 1;
    return lcl_getEntry( GetEntryList(), nNextPos, _out_entryText );
}

void ImplListBoxWindow::SelectEntry( vcl::StringEntryIdentifier _entry )
{
    sal_Int32 nSelect = lcl_getEntryPos( _entry );
    if ( maEntryList.IsEntryPosSelected( nSelect ) )
    {
        // ignore that. This method is a callback from the QuickSelectionEngine, which means the user attempted
        // to select the given entry by typing its starting letters. No need to act.
        return;
    }

    // normalize
    OSL_ENSURE( nSelect < maEntryList.GetEntryCount(), "ImplListBoxWindow::SelectEntry: how that?" );
    sal_Int32 nCount = maEntryList.GetEntryCount();
    if (nSelect >= nCount)
        nSelect = nCount ? nCount-1 : LISTBOX_ENTRY_NOTFOUND;

    // make visible
    ShowProminentEntry( nSelect );

    // actually select
    mnCurrentPos = nSelect;
    if ( SelectEntries( nSelect, LET_KEYMOVE ) )
    {
        mbTravelSelect = true;
        mnSelectModifier = 0;
        ImplCallSelect();
        mbTravelSelect = false;
    }
}

void ImplListBoxWindow::ImplPaint(vcl::RenderContext& rRenderContext, sal_Int32 nPos)
{
    const StyleSettings& rStyleSettings = rRenderContext.GetSettings().GetStyleSettings();

    const ImplEntryType* pEntry = maEntryList.GetEntryPtr( nPos );
    if (!pEntry)
        return;

    tools::Long nWidth = GetOutputSizePixel().Width();
    tools::Long nY = maEntryList.GetAddedHeight(nPos, mnTop);
    tools::Rectangle aRect(Point(0, nY), Size(nWidth, pEntry->getHeightWithMargin()));

    bool bSelected = maEntryList.IsEntryPosSelected(nPos);
    if (bSelected)
    {
        rRenderContext.SetTextColor(!IsEnabled() ? rStyleSettings.GetDisableColor() : rStyleSettings.GetListBoxWindowHighlightTextColor());
        rRenderContext.SetFillColor(rStyleSettings.GetListBoxWindowHighlightColor());
        rRenderContext.SetLineColor();
        rRenderContext.DrawRect(aRect);
    }
    else
    {
        ApplySettings(rRenderContext);
        if (!IsEnabled())
            rRenderContext.SetTextColor(rStyleSettings.GetDisableColor());
    }
    rRenderContext.SetTextFillColor();

    if (IsUserDrawEnabled())
    {
        mbInUserDraw = true;
        mnUserDrawEntry = nPos;
        aRect.AdjustLeft( -mnLeft );
        if (nPos < GetEntryList().GetMRUCount())
            nPos = GetEntryList().FindEntry(GetEntryList().GetEntryText(nPos));
        nPos = nPos - GetEntryList().GetMRUCount();

        UserDrawEvent aUDEvt(&rRenderContext, aRect, nPos, bSelected);
        maUserDrawHdl.Call( &aUDEvt );
        mbInUserDraw = false;
    }
    else
    {
        DrawEntry(rRenderContext, nPos, true, true);
    }
}

void ImplListBoxWindow::DrawEntry(vcl::RenderContext& rRenderContext, sal_Int32 nPos, bool bDrawImage, bool bDrawText)
{
    const ImplEntryType* pEntry = maEntryList.GetEntryPtr(nPos);
    if (!pEntry)
        return;

    tools::Long nEntryHeight = pEntry->getHeightWithMargin();

    // when changing this function don't forget to adjust ImplWin::DrawEntry()

    if (mbInUserDraw)
        nPos = mnUserDrawEntry; // real entry, not the matching entry from MRU

    tools::Long nY = maEntryList.GetAddedHeight(nPos, mnTop);

    if (bDrawImage && maEntryList.HasImages())
    {
        Image aImage = maEntryList.GetEntryImage(nPos);
        if (!!aImage)
        {
            Size aImgSz = aImage.GetSizePixel();
            Point aPtImg(gnBorder - mnLeft, nY + ((nEntryHeight - aImgSz.Height()) / 2));

            if (!IsZoom())
            {
                rRenderContext.DrawImage(aPtImg, aImage);
            }
            else
            {
                aImgSz.setWidth( CalcZoom(aImgSz.Width()) );
                aImgSz.setHeight( CalcZoom(aImgSz.Height()) );
                rRenderContext.DrawImage(aPtImg, aImgSz, aImage);
            }

            const StyleSettings& rStyleSettings = Application::GetSettings().GetStyleSettings();
            const sal_uInt16 nEdgeBlendingPercent(GetEdgeBlending() ? rStyleSettings.GetEdgeBlending() : 0);

            if (nEdgeBlendingPercent && aImgSz.Width() && aImgSz.Height())
            {
                const Color& rTopLeft(rStyleSettings.GetEdgeBlendingTopLeftColor());
                const Color& rBottomRight(rStyleSettings.GetEdgeBlendingBottomRightColor());
                const sal_uInt8 nAlpha(255 - ((nEdgeBlendingPercent * 255) / 100));
                const BitmapEx aBlendFrame(createAlphaBlendFrame(aImgSz, nAlpha, rTopLeft, rBottomRight));

                if (!aBlendFrame.IsEmpty())
                {
                    rRenderContext.DrawBitmapEx(aPtImg, aBlendFrame);
                }
            }
        }
    }

    if (bDrawText)
    {
        OUString aStr(maEntryList.GetEntryText(nPos));
        if (!aStr.isEmpty())
        {
            tools::Long nMaxWidth = std::max(mnMaxWidth, GetOutputSizePixel().Width() - 2 * gnBorder);
            // a multiline entry should only be as wide as the window
            if (pEntry->mnFlags & ListBoxEntryFlags::MultiLine)
                nMaxWidth = GetOutputSizePixel().Width() - 2 * gnBorder;

            tools::Rectangle aTextRect(Point(gnBorder - mnLeft, nY),
                                Size(nMaxWidth, nEntryHeight));

            if (maEntryList.HasEntryImage(nPos) || IsUserDrawEnabled())
            {
                tools::Long nImageWidth = std::max(mnMaxImgWidth, maUserItemSize.Width());
                aTextRect.AdjustLeft(nImageWidth + IMG_TXT_DISTANCE );
            }

            DrawTextFlags nDrawStyle = ImplGetTextStyle();
            if (pEntry->mnFlags & ListBoxEntryFlags::MultiLine)
                nDrawStyle |= MULTILINE_ENTRY_DRAW_FLAGS;
            if (pEntry->mnFlags & ListBoxEntryFlags::DrawDisabled)
                nDrawStyle |= DrawTextFlags::Disable;

            rRenderContext.DrawText(aTextRect, aStr, nDrawStyle);
        }
    }

    if ( !maSeparators.empty() && ( isSeparator(nPos) || isSeparator(nPos-1) ) )
    {
        Color aOldLineColor(rRenderContext.GetLineColor());
        rRenderContext.SetLineColor((GetBackground() != COL_LIGHTGRAY) ? COL_LIGHTGRAY : COL_GRAY);
        Point aStartPos(0, nY);
        if (isSeparator(nPos))
            aStartPos.AdjustY(pEntry->getHeightWithMargin() - 1 );
        Point aEndPos(aStartPos);
        aEndPos.setX( GetOutputSizePixel().Width() );
        rRenderContext.DrawLine(aStartPos, aEndPos);
        rRenderContext.SetLineColor(aOldLineColor);
    }
}

void ImplListBoxWindow::FillLayoutData() const
{
    mxLayoutData.emplace();
    const_cast<ImplListBoxWindow*>(this)->Invalidate(tools::Rectangle(Point(0, 0), GetOutDev()->GetOutputSize()));
}

void ImplListBoxWindow::ImplDoPaint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect)
{
    sal_Int32 nCount = maEntryList.GetEntryCount();

    bool bShowFocusRect = mbHasFocusRect;
    if (mbHasFocusRect)
        ImplHideFocusRect();

    tools::Long nY = 0; // + gnBorder;
    tools::Long nHeight = GetOutputSizePixel().Height();// - mnMaxHeight + gnBorder;

    for (sal_Int32 i = mnTop; i < nCount && nY < nHeight + mnMaxHeight; i++)
    {
        const ImplEntryType* pEntry = maEntryList.GetEntryPtr(i);
        tools::Long nEntryHeight = pEntry->getHeightWithMargin();
        if (nY + nEntryHeight >= rRect.Top() &&
            nY <= rRect.Bottom() + mnMaxHeight)
        {
            ImplPaint(rRenderContext, i);
        }
        nY += nEntryHeight;
    }

    tools::Long nHeightDiff = maEntryList.GetAddedHeight(mnCurrentPos, mnTop);
    maFocusRect.SetPos(Point(0, nHeightDiff));
    Size aSz(maFocusRect.GetWidth(), maEntryList.GetEntryHeight(mnCurrentPos));
    maFocusRect.SetSize(aSz);
    if (HasFocus() && bShowFocusRect)
        ImplShowFocusRect();
}

void ImplListBoxWindow::Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect)
{
    if (SupportsDoubleBuffering())
    {
        // This widget is explicitly double-buffered, so avoid partial paints.
        tools::Rectangle aRect(Point(0, 0), GetOutputSizePixel());
        ImplDoPaint(rRenderContext, aRect);
    }
    else
        ImplDoPaint(rRenderContext, rRect);
}

sal_uInt16 ImplListBoxWindow::GetDisplayLineCount() const
{
    // FIXME: ListBoxEntryFlags::MultiLine

    const sal_Int32 nCount = maEntryList.GetEntryCount()-mnTop;
    tools::Long nHeight = GetOutputSizePixel().Height();// - mnMaxHeight + gnBorder;
    sal_uInt16 nEntries = static_cast< sal_uInt16 >( ( nHeight + mnMaxHeight - 1 ) / mnMaxHeight );
    if( nEntries > nCount )
        nEntries = static_cast<sal_uInt16>(nCount);

    return nEntries;
}

void ImplListBoxWindow::Resize()
{
    Control::Resize();

    bool bShowFocusRect = mbHasFocusRect;
    if ( bShowFocusRect )
        ImplHideFocusRect();

    if( mnCurrentPos != LISTBOX_ENTRY_NOTFOUND )
    {
        Size aSz( GetOutputSizePixel().Width(), maEntryList.GetEntryHeight( mnCurrentPos ) );
        maFocusRect.SetSize( aSz );
    }

    if ( bShowFocusRect )
        ImplShowFocusRect();

    ImplClearLayoutData();
}

void ImplListBoxWindow::GetFocus()
{
    sal_Int32 nPos = mnCurrentPos;
    if ( nPos == LISTBOX_ENTRY_NOTFOUND )
        nPos = 0;
    tools::Long nHeightDiff = maEntryList.GetAddedHeight( nPos, mnTop );
    maFocusRect.SetPos( Point( 0, nHeightDiff ) );
    Size aSz( maFocusRect.GetWidth(), maEntryList.GetEntryHeight( nPos ) );
    maFocusRect.SetSize( aSz );
    ImplShowFocusRect();
    Control::GetFocus();
}

void ImplListBoxWindow::LoseFocus()
{
    ImplHideFocusRect();
    Control::LoseFocus();
}

void ImplListBoxWindow::SetTopEntry( sal_Int32 nTop )
{
    if( maEntryList.GetEntryCount() == 0 )
        return;

    tools::Long nWHeight = PixelToLogic( GetSizePixel() ).Height();

    sal_Int32 nLastEntry = maEntryList.GetEntryCount()-1;
    if( nTop > nLastEntry )
        nTop = nLastEntry;
    const ImplEntryType* pLast = maEntryList.GetEntryPtr( nLastEntry );
    while( nTop > 0 && maEntryList.GetAddedHeight( nLastEntry, nTop-1 ) + pLast->getHeightWithMargin() <= nWHeight )
        nTop--;

    if ( nTop == mnTop )
        return;

    ImplClearLayoutData();
    tools::Long nDiff = maEntryList.GetAddedHeight( mnTop, nTop );
    PaintImmediately();
    ImplHideFocusRect();
    mnTop = nTop;
    Scroll( 0, nDiff );
    PaintImmediately();
    if( HasFocus() )
        ImplShowFocusRect();
    maScrollHdl.Call( this );
}

void ImplListBoxWindow::ShowProminentEntry( sal_Int32 nEntryPos )
{
    sal_Int32 nPos = nEntryPos;
    auto nWHeight = PixelToLogic( GetSizePixel() ).Height();
    while( nEntryPos > 0 && maEntryList.GetAddedHeight( nPos+1, nEntryPos ) < nWHeight/2 )
        nEntryPos--;

    SetTopEntry( nEntryPos );
}

void ImplListBoxWindow::SetLeftIndent( tools::Long n )
{
    ScrollHorz( n - mnLeft );
}

void ImplListBoxWindow::ScrollHorz( tools::Long n )
{
    tools::Long nDiff = 0;
    if ( n > 0 )
    {
        tools::Long nWidth = GetOutputSizePixel().Width();
        if( ( mnMaxWidth - mnLeft + n ) > nWidth )
            nDiff = n;
    }
    else if ( n < 0 )
    {
        if( mnLeft )
        {
            tools::Long nAbs = -n;
            nDiff = - std::min( mnLeft, nAbs );
        }
    }

    if ( nDiff )
    {
        ImplClearLayoutData();
        mnLeft = sal::static_int_cast<sal_uInt16>(mnLeft + nDiff);
        PaintImmediately();
        ImplHideFocusRect();
        Scroll( -nDiff, 0 );
        PaintImmediately();
        if( HasFocus() )
            ImplShowFocusRect();
        maScrollHdl.Call( this );
    }
}

void ImplListBoxWindow::SetSeparatorPos( sal_Int32 n )
{
    maSeparators.clear();

    if ( n != LISTBOX_ENTRY_NOTFOUND )
    {
        maSeparators.insert( n );
    }
}

sal_Int32 ImplListBoxWindow::GetSeparatorPos() const
{
    if (!maSeparators.empty())
        return *(maSeparators.begin());
    else
        return LISTBOX_ENTRY_NOTFOUND;
}

bool ImplListBoxWindow::isSeparator( const sal_Int32 &n) const
{
    return maSeparators.find(n) != maSeparators.end();
}

Size ImplListBoxWindow::CalcSize(sal_Int32 nMaxLines) const
{
    // FIXME: ListBoxEntryFlags::MultiLine

    Size aSz;
    aSz.setHeight(nMaxLines * GetEntryHeightWithMargin());
    aSz.setWidth( mnMaxWidth + 2*gnBorder );
    return aSz;
}

tools::Rectangle ImplListBoxWindow::GetBoundingRectangle( sal_Int32 nItem ) const
{
    const ImplEntryType* pEntry = maEntryList.GetEntryPtr( nItem );
    Size aSz( GetSizePixel().Width(), pEntry ? pEntry->getHeightWithMargin() : GetEntryHeightWithMargin() );
    tools::Long nY = maEntryList.GetAddedHeight( nItem, GetTopEntry() ) + GetEntryList().GetMRUCount()*GetEntryHeightWithMargin();
    tools::Rectangle aRect( Point( 0, nY ), aSz );
    return aRect;
}

void ImplListBoxWindow::StateChanged( StateChangedType nType )
{
    Control::StateChanged( nType );

    if ( nType == StateChangedType::Zoom )
    {
        ApplySettings(*GetOutDev());
        ImplCalcMetrics();
        Invalidate();
    }
    else if ( nType == StateChangedType::UpdateMode )
    {
        if ( IsUpdateMode() && IsReallyVisible() )
            Invalidate();
    }
    else if ( nType == StateChangedType::ControlFont )
    {
        ApplySettings(*GetOutDev());
        ImplCalcMetrics();
        Invalidate();
    }
    else if ( nType == StateChangedType::ControlForeground )
    {
        ApplySettings(*GetOutDev());
        Invalidate();
    }
    else if ( nType == StateChangedType::ControlBackground )
    {
        ApplySettings(*GetOutDev());
        Invalidate();
    }
    else if( nType == StateChangedType::Enable )
    {
        Invalidate();
    }

    ImplClearLayoutData();
}

void ImplListBoxWindow::DataChanged( const DataChangedEvent& rDCEvt )
{
    Control::DataChanged( rDCEvt );

    if ( (rDCEvt.GetType() == DataChangedEventType::FONTS) ||
         (rDCEvt.GetType() == DataChangedEventType::FONTSUBSTITUTION) ||
         ((rDCEvt.GetType() == DataChangedEventType::SETTINGS) &&
          (rDCEvt.GetFlags() & AllSettingsFlags::STYLE)) )
    {
        ImplClearLayoutData();
        ApplySettings(*GetOutDev());
        ImplCalcMetrics();
        Invalidate();
    }
}

DrawTextFlags ImplListBoxWindow::ImplGetTextStyle() const
{
    DrawTextFlags nTextStyle = DrawTextFlags::VCenter;

    if (maEntryList.HasImages())
        nTextStyle |= DrawTextFlags::Left;
    else if (mbCenter)
        nTextStyle |= DrawTextFlags::Center;
    else if (mbRight)
        nTextStyle |= DrawTextFlags::Right;
    else
        nTextStyle |= DrawTextFlags::Left;

    return nTextStyle;
}

ImplListBox::ImplListBox( vcl::Window* pParent, WinBits nWinStyle ) :
    Control( pParent, nWinStyle ),
    maLBWindow(VclPtr<ImplListBoxWindow>::Create( this, nWinStyle&(~WB_BORDER) ))
{
    // for native widget rendering we must be able to detect this window type
    SetType( WindowType::LISTBOXWINDOW );

    mpVScrollBar    = VclPtr<ScrollBar>::Create( this, WB_VSCROLL | WB_DRAG );
    mpHScrollBar    = VclPtr<ScrollBar>::Create( this, WB_HSCROLL | WB_DRAG );
    mpScrollBarBox  = VclPtr<ScrollBarBox>::Create( this );

    Link<ScrollBar*,void> aLink( LINK( this, ImplListBox, ScrollBarHdl ) );
    mpVScrollBar->SetScrollHdl( aLink );
    mpHScrollBar->SetScrollHdl( aLink );

    mbVScroll       = false;
    mbHScroll       = false;
    mbAutoHScroll   = ( nWinStyle & WB_AUTOHSCROLL );
    mbEdgeBlending  = false;

    maLBWindow->SetScrollHdl( LINK( this, ImplListBox, LBWindowScrolled ) );
    maLBWindow->SetMRUChangedHdl( LINK( this, ImplListBox, MRUChanged ) );
    maLBWindow->SetEdgeBlending(GetEdgeBlending());
    maLBWindow->Show();
}

ImplListBox::~ImplListBox()
{
    disposeOnce();
}

void ImplListBox::dispose()
{
    mpHScrollBar.disposeAndClear();
    mpVScrollBar.disposeAndClear();
    mpScrollBarBox.disposeAndClear();
    maLBWindow.disposeAndClear();
    Control::dispose();
}

void ImplListBox::Clear()
{
    maLBWindow->Clear();
    if ( GetEntryList().GetMRUCount() )
    {
        maLBWindow->GetEntryList().SetMRUCount( 0 );
        maLBWindow->SetSeparatorPos( LISTBOX_ENTRY_NOTFOUND );
    }
    mpVScrollBar->SetThumbPos( 0 );
    mpHScrollBar->SetThumbPos( 0 );
    CompatStateChanged( StateChangedType::Data );
}

sal_Int32 ImplListBox::InsertEntry( sal_Int32 nPos, const OUString& rStr )
{
    ImplEntryType* pNewEntry = new ImplEntryType( rStr );
    sal_Int32 nNewPos = maLBWindow->InsertEntry( nPos, pNewEntry );
    CompatStateChanged( StateChangedType::Data );
    return nNewPos;
}

sal_Int32 ImplListBox::InsertEntry( sal_Int32 nPos, const OUString& rStr, const Image& rImage )
{
    ImplEntryType* pNewEntry = new ImplEntryType( rStr, rImage );
    sal_Int32 nNewPos = maLBWindow->InsertEntry( nPos, pNewEntry );
    CompatStateChanged( StateChangedType::Data );
    return nNewPos;
}

void ImplListBox::RemoveEntry( sal_Int32 nPos )
{
    maLBWindow->RemoveEntry( nPos );
    CompatStateChanged( StateChangedType::Data );
}

void ImplListBox::SetEntryFlags( sal_Int32 nPos, ListBoxEntryFlags nFlags )
{
    maLBWindow->SetEntryFlags( nPos, nFlags );
}

void ImplListBox::SelectEntry( sal_Int32 nPos, bool bSelect )
{
    maLBWindow->SelectEntry( nPos, bSelect );
}

void ImplListBox::SetNoSelection()
{
    maLBWindow->DeselectAll();
}

void ImplListBox::GetFocus()
{
    if (maLBWindow)
        maLBWindow->GrabFocus();
    else
        Control::GetFocus();
}

void ImplListBox::Resize()
{
    Control::Resize();
    ImplResizeControls();
    ImplCheckScrollBars();
}

IMPL_LINK_NOARG(ImplListBox, MRUChanged, LinkParamNone*, void)
{
    CompatStateChanged( StateChangedType::Data );
}

IMPL_LINK_NOARG(ImplListBox, LBWindowScrolled, ImplListBoxWindow*, void)
{
    tools::Long nSet = GetTopEntry();
    if( nSet > mpVScrollBar->GetRangeMax() )
        mpVScrollBar->SetRangeMax( GetEntryList().GetEntryCount() );
    mpVScrollBar->SetThumbPos( GetTopEntry() );

    mpHScrollBar->SetThumbPos( GetLeftIndent() );

    maScrollHdl.Call( this );
}

IMPL_LINK( ImplListBox, ScrollBarHdl, ScrollBar*, pSB, void )
{
    sal_uInt16 nPos = static_cast<sal_uInt16>(pSB->GetThumbPos());
    if( pSB == mpVScrollBar )
        SetTopEntry( nPos );
    else if( pSB == mpHScrollBar )
        SetLeftIndent( nPos );
    if( GetParent() )
        GetParent()->Invalidate( InvalidateFlags::Update );
}

void ImplListBox::ImplCheckScrollBars()
{
    bool bArrange = false;

    Size aOutSz = GetOutputSizePixel();
    sal_Int32 nEntries = GetEntryList().GetEntryCount();
    sal_uInt16 nMaxVisEntries = static_cast<sal_uInt16>(aOutSz.Height() / GetEntryHeightWithMargin());

    // vertical ScrollBar
    if( nEntries > nMaxVisEntries )
    {
        if( !mbVScroll )
            bArrange = true;
        mbVScroll = true;

        // check of the scrolled-out region
        if( GetEntryList().GetSelectedEntryCount() == 1 &&
            GetEntryList().GetSelectedEntryPos( 0 ) != LISTBOX_ENTRY_NOTFOUND )
            ShowProminentEntry( GetEntryList().GetSelectedEntryPos( 0 ) );
        else
            SetTopEntry( GetTopEntry() );   // MaxTop is being checked...
    }
    else
    {
        if( mbVScroll )
            bArrange = true;
        mbVScroll = false;
        SetTopEntry( 0 );
    }

    // horizontal ScrollBar
    if( mbAutoHScroll )
    {
        tools::Long nWidth = static_cast<sal_uInt16>(aOutSz.Width());
        if ( mbVScroll )
            nWidth -= mpVScrollBar->GetSizePixel().Width();

        tools::Long nMaxWidth = GetMaxEntryWidth();
        if( nWidth < nMaxWidth )
        {
            if( !mbHScroll )
                bArrange = true;
            mbHScroll = true;

            if ( !mbVScroll )   // maybe we do need one now
            {
                nMaxVisEntries = static_cast<sal_uInt16>( ( aOutSz.Height() - mpHScrollBar->GetSizePixel().Height() ) / GetEntryHeightWithMargin() );
                if( nEntries > nMaxVisEntries )
                {
                    bArrange = true;
                    mbVScroll = true;

                    // check of the scrolled-out region
                    if( GetEntryList().GetSelectedEntryCount() == 1 &&
                        GetEntryList().GetSelectedEntryPos( 0 ) != LISTBOX_ENTRY_NOTFOUND )
                        ShowProminentEntry( GetEntryList().GetSelectedEntryPos( 0 ) );
                    else
                        SetTopEntry( GetTopEntry() );   // MaxTop is being checked...
                }
            }

            // check of the scrolled-out region
            sal_uInt16 nMaxLI = static_cast<sal_uInt16>(nMaxWidth - nWidth);
            if ( nMaxLI < GetLeftIndent() )
                SetLeftIndent( nMaxLI );
        }
        else
        {
            if( mbHScroll )
                bArrange = true;
            mbHScroll = false;
            SetLeftIndent( 0 );
        }
    }

    if( bArrange )
        ImplResizeControls();

    ImplInitScrollBars();
}

void ImplListBox::ImplInitScrollBars()
{
    Size aOutSz = maLBWindow->GetOutputSizePixel();

    if ( mbVScroll )
    {
        sal_Int32 nEntries = GetEntryList().GetEntryCount();
        sal_uInt16 nVisEntries = static_cast<sal_uInt16>(aOutSz.Height() / GetEntryHeightWithMargin());
        mpVScrollBar->SetRangeMax( nEntries );
        mpVScrollBar->SetVisibleSize( nVisEntries );
        mpVScrollBar->SetPageSize( nVisEntries - 1 );
    }

    if ( mbHScroll )
    {
        mpHScrollBar->SetRangeMax( GetMaxEntryWidth() + HORZ_SCROLL );
        mpHScrollBar->SetVisibleSize( static_cast<sal_uInt16>(aOutSz.Width()) );
        mpHScrollBar->SetLineSize( HORZ_SCROLL );
        mpHScrollBar->SetPageSize( aOutSz.Width() - HORZ_SCROLL );
    }
}

void ImplListBox::ImplResizeControls()
{
    // Here we only position the Controls; if the Scrollbars are to be
    // visible is already determined in ImplCheckScrollBars

    Size aOutSz = GetOutputSizePixel();
    tools::Long nSBWidth = GetSettings().GetStyleSettings().GetScrollBarSize();
    nSBWidth = CalcZoom( nSBWidth );

    Size aInnerSz( aOutSz );
    if ( mbVScroll )
        aInnerSz.AdjustWidth( -nSBWidth );
    if ( mbHScroll )
        aInnerSz.AdjustHeight( -nSBWidth );

    Point aWinPos( 0, 0 );
    maLBWindow->SetPosSizePixel( aWinPos, aInnerSz );

    // ScrollBarBox
    if( mbVScroll && mbHScroll )
    {
        Point aBoxPos( aInnerSz.Width(), aInnerSz.Height() );
        mpScrollBarBox->SetPosSizePixel( aBoxPos, Size( nSBWidth, nSBWidth ) );
        mpScrollBarBox->Show();
    }
    else
    {
        mpScrollBarBox->Hide();
    }

    // vertical ScrollBar
    if( mbVScroll )
    {
        // Scrollbar on left or right side?
        Point aVPos( aOutSz.Width() - nSBWidth, 0 );
        mpVScrollBar->SetPosSizePixel( aVPos, Size( nSBWidth, aInnerSz.Height() ) );
        mpVScrollBar->Show();
    }
    else
    {
        mpVScrollBar->Hide();
        // #107254# Don't reset top entry after resize, but check for max top entry
        SetTopEntry( GetTopEntry() );
    }

    // horizontal ScrollBar
    if( mbHScroll )
    {
        Point aHPos( 0, aOutSz.Height() - nSBWidth );
        mpHScrollBar->SetPosSizePixel( aHPos, Size( aInnerSz.Width(), nSBWidth ) );
        mpHScrollBar->Show();
    }
    else
    {
        mpHScrollBar->Hide();
        SetLeftIndent( 0 );
    }
}

void ImplListBox::StateChanged( StateChangedType nType )
{
    if ( nType == StateChangedType::InitShow )
    {
        ImplCheckScrollBars();
    }
    else if ( ( nType == StateChangedType::UpdateMode ) || ( nType == StateChangedType::Data ) )
    {
        bool bUpdate = IsUpdateMode();
        maLBWindow->SetUpdateMode( bUpdate );
        if ( bUpdate && IsReallyVisible() )
            ImplCheckScrollBars();
    }
    else if( nType == StateChangedType::Enable )
    {
        mpHScrollBar->Enable( IsEnabled() );
        mpVScrollBar->Enable( IsEnabled() );
        mpScrollBarBox->Enable( IsEnabled() );
        maLBWindow->Enable( IsEnabled() );

        Invalidate();
    }
    else if ( nType == StateChangedType::Zoom )
    {
        maLBWindow->SetZoom( GetZoom() );
        Resize();
    }
    else if ( nType == StateChangedType::ControlFont )
    {
        maLBWindow->SetControlFont( GetControlFont() );
    }
    else if ( nType == StateChangedType::ControlForeground )
    {
        maLBWindow->SetControlForeground( GetControlForeground() );
    }
    else if ( nType == StateChangedType::ControlBackground )
    {
        maLBWindow->SetControlBackground( GetControlBackground() );
    }
    else if( nType == StateChangedType::Mirroring )
    {
        maLBWindow->EnableRTL( IsRTLEnabled() );
        mpHScrollBar->EnableRTL( IsRTLEnabled() );
        mpVScrollBar->EnableRTL( IsRTLEnabled() );
        ImplResizeControls();
    }

    Control::StateChanged( nType );
}

bool ImplListBox::EventNotify( NotifyEvent& rNEvt )
{
    bool bDone = false;
    if ( rNEvt.GetType() == NotifyEventType::COMMAND )
    {
        const CommandEvent& rCEvt = *rNEvt.GetCommandEvent();
        if ( rCEvt.GetCommand() == CommandEventId::Wheel )
        {
            const CommandWheelData* pData = rCEvt.GetWheelData();
            if( !pData->GetModifier() && ( pData->GetMode() == CommandWheelMode::SCROLL ) )
            {
                bDone = HandleScrollCommand( rCEvt, mpHScrollBar, mpVScrollBar );
            }
        }
        else if (rCEvt.GetCommand() == CommandEventId::GesturePan)
        {
            bDone = HandleScrollCommand(rCEvt, mpHScrollBar, mpVScrollBar);
        }
    }

    return bDone || Window::EventNotify( rNEvt );
}

const Wallpaper& ImplListBox::GetDisplayBackground() const
{
    return maLBWindow->GetDisplayBackground();
}

bool ImplListBox::HandleWheelAsCursorTravel(const CommandEvent& rCEvt, Control& rControl)
{
    bool bDone = false;
    if ( rCEvt.GetCommand() == CommandEventId::Wheel )
    {
        const CommandWheelData* pData = rCEvt.GetWheelData();
        if( !pData->GetModifier() && ( pData->GetMode() == CommandWheelMode::SCROLL ) )
        {
            if (!rControl.HasChildPathFocus())
                rControl.GrabFocus();
            sal_uInt16 nKey = ( pData->GetDelta() < 0 ) ? KEY_DOWN : KEY_UP;
            KeyEvent aKeyEvent( 0, vcl::KeyCode( nKey ) );
            bDone = ProcessKeyInput( aKeyEvent );
        }
    }
    return bDone;
}

void ImplListBox::SetMRUEntries( std::u16string_view rEntries, sal_Unicode cSep )
{
    bool bChanges = GetEntryList().GetMRUCount() != 0;

    // Remove old MRU entries
    for ( sal_Int32 n = GetEntryList().GetMRUCount();n; )
        maLBWindow->RemoveEntry( --n );

    sal_Int32 nMRUCount = 0;
    sal_Int32 nIndex = 0;
    do
    {
        OUString aEntry( o3tl::getToken(rEntries, 0, cSep, nIndex ) );
        // Accept only existing entries
        if ( GetEntryList().FindEntry( aEntry ) != LISTBOX_ENTRY_NOTFOUND )
        {
            ImplEntryType* pNewEntry = new ImplEntryType( aEntry );
            maLBWindow->InsertEntry(nMRUCount++, pNewEntry, false);
            bChanges = true;
        }
    }
    while ( nIndex >= 0 );

    if ( bChanges )
    {
        maLBWindow->GetEntryList().SetMRUCount( nMRUCount );
        SetSeparatorPos( nMRUCount ? nMRUCount-1 : 0 );
        CompatStateChanged( StateChangedType::Data );
    }
}

OUString ImplListBox::GetMRUEntries( sal_Unicode cSep ) const
{
    OUStringBuffer aEntries;
    for ( sal_Int32 n = 0; n < GetEntryList().GetMRUCount(); n++ )
    {
        aEntries.append(GetEntryList().GetEntryText( n ));
        if( n < ( GetEntryList().GetMRUCount() - 1 ) )
            aEntries.append(cSep);
    }
    return aEntries.makeStringAndClear();
}

void ImplListBox::SetEdgeBlending(bool bNew)
{
    if(mbEdgeBlending != bNew)
    {
        mbEdgeBlending = bNew;
        maLBWindow->SetEdgeBlending(GetEdgeBlending());
    }
}

void ImplListBox::SetHighlightColor(const Color& rColor)
{
    AllSettings aSettings(GetSettings());
    StyleSettings aStyle(aSettings.GetStyleSettings());
    aStyle.SetHighlightColor(rColor);
    aSettings.SetStyleSettings(aStyle);
    SetSettings(aSettings);

    AllSettings aSettingsLB(maLBWindow->GetSettings());
    StyleSettings aStyleLB(aSettingsLB.GetStyleSettings());
    aStyleLB.SetListBoxWindowHighlightColor(rColor);
    aSettingsLB.SetStyleSettings(aStyleLB);
    maLBWindow->SetSettings(aSettingsLB);
}

void ImplListBox::SetHighlightTextColor(const Color& rColor)
{
    AllSettings aSettings(GetSettings());
    StyleSettings aStyle(aSettings.GetStyleSettings());
    aStyle.SetHighlightTextColor(rColor);
    aSettings.SetStyleSettings(aStyle);
    SetSettings(aSettings);

    AllSettings aSettingsLB(maLBWindow->GetSettings());
    StyleSettings aStyleLB(aSettingsLB.GetStyleSettings());
    aStyleLB.SetListBoxWindowHighlightTextColor(rColor);
    aSettingsLB.SetStyleSettings(aStyleLB);
    maLBWindow->SetSettings(aSettingsLB);
}

ImplWin::ImplWin( vcl::Window* pParent, WinBits nWinStyle ) :
    Control ( pParent, nWinStyle )
{
    if ( IsNativeControlSupported(ControlType::Listbox, ControlPart::Entire)
            && ! IsNativeControlSupported(ControlType::Listbox, ControlPart::ButtonDown) )
        SetBackground();
    else
        SetBackground( Wallpaper( GetSettings().GetStyleSettings().GetFieldColor() ) );

    ImplGetWindowImpl()->mbUseNativeFocus = ImplGetSVData()->maNWFData.mbNoFocusRects;

    mbEdgeBlending = false;
    mnItemPos = LISTBOX_ENTRY_NOTFOUND;
}

void ImplWin::MouseButtonDown( const MouseEvent& )
{
    if( IsEnabled() )
    {
        maMBDownHdl.Call(this);
    }
}

void ImplWin::FillLayoutData() const
{
    mxLayoutData.emplace();
    ImplWin* pThis = const_cast<ImplWin*>(this);
    pThis->ImplDraw(*pThis->GetOutDev(), true);
}

void ImplWin::ImplDraw(vcl::RenderContext& rRenderContext, bool bLayout)
{
    const StyleSettings& rStyleSettings = rRenderContext.GetSettings().GetStyleSettings();

    if (!bLayout)
    {
        bool bNativeOK = false;
        bool bHasFocus = HasFocus();
        bool bIsEnabled = IsEnabled();

        ControlState nState = ControlState::ENABLED;
        if (rRenderContext.IsNativeControlSupported(ControlType::Listbox, ControlPart::Entire)
            && rRenderContext.IsNativeControlSupported(ControlType::Listbox, ControlPart::HasBackgroundTexture) )
        {
            // Repaint the (focused) area similarly to
            // ImplSmallBorderWindowView::DrawWindow() in
            // vcl/source/window/brdwin.cxx
            vcl::Window *pWin = GetParent();

            ImplControlValue aControlValue;
            bIsEnabled &= pWin->IsEnabled();
            if ( !bIsEnabled )
                nState &= ~ControlState::ENABLED;
            bHasFocus |= pWin->HasFocus();
            if ( bHasFocus )
                nState |= ControlState::FOCUSED;

            // The listbox is painted over the entire control including the
            // border, but ImplWin does not contain the border => correction
            // needed.
            sal_Int32 nLeft, nTop, nRight, nBottom;
            pWin->GetBorder( nLeft, nTop, nRight, nBottom );
            Point aPoint( -nLeft, -nTop );
            tools::Rectangle aCtrlRegion( aPoint - GetPosPixel(), pWin->GetSizePixel() );

            bool bMouseOver = pWin->IsMouseOver();
            if (!bMouseOver)
            {
                vcl::Window *pChild = pWin->GetWindow( GetWindowType::FirstChild );
                while( pChild )
                {
                    bMouseOver = pChild->IsMouseOver();
                    if (bMouseOver)
                        break;
                    pChild = pChild->GetWindow( GetWindowType::Next );
                }
            }
            if( bMouseOver )
                nState |= ControlState::ROLLOVER;

            Color aBackgroundColor = COL_AUTO;
            if (IsControlBackground())
                aBackgroundColor = GetControlBackground();

            // if parent has no border, then nobody has drawn the background
            // since no border window exists. so draw it here.
            WinBits nParentStyle = pWin->GetStyle();
            if( ! (nParentStyle & WB_BORDER) || (nParentStyle & WB_NOBORDER) )
            {
                tools::Rectangle aParentRect( Point( 0, 0 ), pWin->GetSizePixel() );
                pWin->GetOutDev()->DrawNativeControl( ControlType::Listbox, ControlPart::Entire, aParentRect,
                                         nState, aControlValue, OUString(), aBackgroundColor);
            }

            bNativeOK = rRenderContext.DrawNativeControl(ControlType::Listbox, ControlPart::Entire, aCtrlRegion,
                                                         nState, aControlValue, OUString(), aBackgroundColor);
        }

        if (bIsEnabled)
        {
            if (bHasFocus && !ImplGetSVData()->maNWFData.mbDDListBoxNoTextArea)
            {
                if ( !ImplGetSVData()->maNWFData.mbNoFocusRects )
                {
                    rRenderContext.SetFillColor( rStyleSettings.GetHighlightColor() );
                    rRenderContext.SetTextColor( rStyleSettings.GetHighlightTextColor() );
                }
                else
                {
                    rRenderContext.SetLineColor();
                    rRenderContext.SetFillColor();
                    rRenderContext.SetTextColor( rStyleSettings.GetFieldTextColor() );
                }
                rRenderContext.DrawRect( maFocusRect );
            }
            else
            {
                Color aColor;
                if (IsControlForeground())
                    aColor = GetControlForeground();
                else if (ImplGetSVData()->maNWFData.mbDDListBoxNoTextArea)
                {
                    if( bNativeOK && (nState & ControlState::ROLLOVER) )
                        aColor = rStyleSettings.GetButtonRolloverTextColor();
                    else
                        aColor = rStyleSettings.GetButtonTextColor();
                }
                else
                {
                    if( bNativeOK && (nState & ControlState::ROLLOVER) )
                        aColor = rStyleSettings.GetFieldRolloverTextColor();
                    else
                        aColor = rStyleSettings.GetFieldTextColor();
                }
                rRenderContext.SetTextColor(aColor);
                if (!bNativeOK)
                    rRenderContext.Erase(maFocusRect);
            }
        }
        else // Disabled
        {
            rRenderContext.SetTextColor(rStyleSettings.GetDisableColor());
            if (!bNativeOK)
                rRenderContext.Erase(maFocusRect);
        }
    }

    DrawEntry(rRenderContext, bLayout);
}

void ImplWin::ApplySettings(vcl::RenderContext& rRenderContext)
{
    const StyleSettings& rStyleSettings = rRenderContext.GetSettings().GetStyleSettings();

    ApplyControlFont(rRenderContext, rStyleSettings.GetFieldFont());
    ApplyControlForeground(rRenderContext, rStyleSettings.GetFieldTextColor());

    if (IsControlBackground())
        rRenderContext.SetBackground(GetControlBackground());
    else
        rRenderContext.SetBackground(rStyleSettings.GetFieldColor());
}

void ImplWin::Paint( vcl::RenderContext& rRenderContext, const tools::Rectangle& )
{
    ImplDraw(rRenderContext);
}

void ImplWin::DrawEntry(vcl::RenderContext& rRenderContext, bool bLayout)
{
    tools::Long nBorder = 1;
    Size aOutSz(GetOutputSizePixel());

    bool bImage = !!maImage;
    if (bImage && !bLayout)
    {
        DrawImageFlags nStyle = DrawImageFlags::NONE;
        Size aImgSz = maImage.GetSizePixel();
        Point aPtImg( nBorder, ( ( aOutSz.Height() - aImgSz.Height() ) / 2 ) );
        const StyleSettings& rStyleSettings = Application::GetSettings().GetStyleSettings();

        // check for HC mode
        Image *pImage = &maImage;

        if ( !IsZoom() )
        {
            rRenderContext.DrawImage( aPtImg, *pImage, nStyle );
        }
        else
        {
            aImgSz.setWidth( CalcZoom( aImgSz.Width() ) );
            aImgSz.setHeight( CalcZoom( aImgSz.Height() ) );
            rRenderContext.DrawImage( aPtImg, aImgSz, *pImage, nStyle );
        }

        const sal_uInt16 nEdgeBlendingPercent(GetEdgeBlending() ? rStyleSettings.GetEdgeBlending() : 0);

        if(nEdgeBlendingPercent)
        {
            const Color& rTopLeft(rStyleSettings.GetEdgeBlendingTopLeftColor());
            const Color& rBottomRight(rStyleSettings.GetEdgeBlendingBottomRightColor());
            const sal_uInt8 nAlpha(255 - ((nEdgeBlendingPercent * 255) / 100));
            const BitmapEx aBlendFrame(createAlphaBlendFrame(aImgSz, nAlpha, rTopLeft, rBottomRight));

            if(!aBlendFrame.IsEmpty())
            {
                rRenderContext.DrawBitmapEx(aPtImg, aBlendFrame);
            }
        }
    }

    if( !maString.isEmpty() )
    {
        DrawTextFlags nTextStyle = DrawTextFlags::VCenter;

        if ( bImage && !bLayout )
            nTextStyle |= DrawTextFlags::Left;
        else if ( GetStyle() & WB_CENTER )
            nTextStyle |= DrawTextFlags::Center;
        else if ( GetStyle() & WB_RIGHT )
            nTextStyle |= DrawTextFlags::Right;
        else
            nTextStyle |= DrawTextFlags::Left;

        tools::Rectangle aTextRect( Point( nBorder, 0 ), Size( aOutSz.Width()-2*nBorder, aOutSz.Height() ) );

        if ( bImage )
        {
            aTextRect.AdjustLeft(maImage.GetSizePixel().Width() + IMG_TXT_DISTANCE );
        }

        std::vector< tools::Rectangle >* pVector = bLayout ? &mxLayoutData->m_aUnicodeBoundRects : nullptr;
        OUString* pDisplayText = bLayout ? &mxLayoutData->m_aDisplayText : nullptr;
        rRenderContext.DrawText( aTextRect, maString, nTextStyle, pVector, pDisplayText );
    }

    if( HasFocus() && !bLayout )
        ShowFocus( maFocusRect );
}

void ImplWin::Resize()
{
    Control::Resize();
    maFocusRect.SetSize( GetOutputSizePixel() );
    Invalidate();
}

void ImplWin::GetFocus()
{
    ShowFocus( maFocusRect );
    if (IsNativeWidgetEnabled() &&
        IsNativeControlSupported(ControlType::Listbox, ControlPart::Entire))
    {
        vcl::Window* pWin = GetParent()->GetWindow( GetWindowType::Border );
        if( ! pWin )
            pWin = GetParent();
        pWin->Invalidate();
    }
    else
        Invalidate();
    Control::GetFocus();
}

void ImplWin::LoseFocus()
{
    HideFocus();
    if (IsNativeWidgetEnabled() &&
        IsNativeControlSupported( ControlType::Listbox, ControlPart::Entire))
    {
        vcl::Window* pWin = GetParent()->GetWindow( GetWindowType::Border );
        if( ! pWin )
            pWin = GetParent();
        pWin->Invalidate();
    }
    else
        Invalidate();
    Control::LoseFocus();
}

void ImplWin::ShowFocus(const tools::Rectangle& rRect)
{
    if (IsNativeControlSupported(ControlType::Listbox, ControlPart::Focus))
    {
        ImplControlValue aControlValue;

        vcl::Window *pWin = GetParent();
        tools::Rectangle aParentRect(Point(0, 0), pWin->GetSizePixel());
        pWin->GetOutDev()->DrawNativeControl(ControlType::Listbox, ControlPart::Focus, aParentRect,
                                ControlState::FOCUSED, aControlValue, OUString());
    }
    Control::ShowFocus(rRect);
}

ImplBtn::ImplBtn( vcl::Window* pParent, WinBits nWinStyle ) :
    PushButton(  pParent, nWinStyle )
{
}

void ImplBtn::MouseButtonDown( const MouseEvent& )
{
    if( IsEnabled() )
        maMBDownHdl.Call(this);
}

ImplListBoxFloatingWindow::ImplListBoxFloatingWindow( vcl::Window* pParent ) :
    FloatingWindow( pParent, WB_BORDER | WB_SYSTEMWINDOW | WB_NOSHADOW )    // no drop shadow for list boxes
{
    // for native widget rendering we must be able to detect this window type
    SetType( WindowType::LISTBOXWINDOW );

    mpImplLB = nullptr;
    mnDDLineCount = 0;
    mbAutoWidth = false;

    mnPopupModeStartSaveSelection = LISTBOX_ENTRY_NOTFOUND;

    vcl::Window * pBorderWindow = ImplGetBorderWindow();
    if( pBorderWindow )
    {
        SetAccessibleRole(accessibility::AccessibleRole::PANEL);
        pBorderWindow->SetAccessibleRole(accessibility::AccessibleRole::WINDOW);
    }
    else
    {
        SetAccessibleRole(accessibility::AccessibleRole::WINDOW);
    }

}

ImplListBoxFloatingWindow::~ImplListBoxFloatingWindow()
{
    disposeOnce();
}

void ImplListBoxFloatingWindow::dispose()
{
    mpImplLB.reset();
    FloatingWindow::dispose();
}


bool ImplListBoxFloatingWindow::PreNotify( NotifyEvent& rNEvt )
{
    if( rNEvt.GetType() == NotifyEventType::LOSEFOCUS )
    {
        if( !GetParent()->HasChildPathFocus( true ) )
            EndPopupMode();
    }

    return FloatingWindow::PreNotify( rNEvt );
}

void ImplListBoxFloatingWindow::setPosSizePixel( tools::Long nX, tools::Long nY, tools::Long nWidth, tools::Long nHeight, PosSizeFlags nFlags )
{
    FloatingWindow::setPosSizePixel( nX, nY, nWidth, nHeight, nFlags );

    // Fix #60890# ( MBA ): to be able to resize the Listbox even in its open state
    // after a call to Resize(), we adjust its position if necessary
    if ( IsReallyVisible() && ( nFlags & PosSizeFlags::Height ) )
    {
        Point aPos = GetParent()->GetPosPixel();
        aPos = GetParent()->GetParent()->OutputToScreenPixel( aPos );

        if ( nFlags & PosSizeFlags::X )
            aPos.setX( nX );

        if ( nFlags & PosSizeFlags::Y )
            aPos.setY( nY );

        sal_uInt16 nIndex;
        SetPosPixel( ImplCalcPos( this, tools::Rectangle( aPos, GetParent()->GetSizePixel() ), FloatWinPopupFlags::Down, nIndex ) );
    }

//  if( !IsReallyVisible() )
    {
        // The ImplListBox does not get a Resize() as not visible.
        // But the windows must get a Resize(), so that the number of
        // visible entries is correct for PgUp/PgDown.
        // The number also cannot be calculated by List/Combobox, as for
        // this the presence of the vertical Scrollbar has to be known.
        mpImplLB->SetSizePixel( GetOutputSizePixel() );
        static_cast<vcl::Window*>(mpImplLB)->Resize();
        static_cast<vcl::Window*>(mpImplLB->GetMainWindow())->Resize();
    }
}

void ImplListBoxFloatingWindow::Resize()
{
    mpImplLB->GetMainWindow()->ImplClearLayoutData();
    FloatingWindow::Resize();
}

Size ImplListBoxFloatingWindow::CalcFloatSize(const tools::Rectangle& rParentRect) const
{
    Size aFloatSz( maPrefSz );

    sal_Int32 nLeft, nTop, nRight, nBottom;
    GetBorder( nLeft, nTop, nRight, nBottom );

    sal_Int32 nLines = mpImplLB->GetEntryList().GetEntryCount();
    if ( mnDDLineCount && ( nLines > mnDDLineCount ) )
        nLines = mnDDLineCount;

    Size aSz = mpImplLB->CalcSize( nLines );
    tools::Long nMaxHeight = aSz.Height() + nTop + nBottom;

    if ( mnDDLineCount )
        aFloatSz.setHeight( nMaxHeight );

    AbsoluteScreenPixelRectangle aDesktopRect(GetDesktopRectPixel());

    if( mbAutoWidth )
    {
        // AutoSize first only for width...

        aFloatSz.setWidth( aSz.Width() + nLeft + nRight );
        aFloatSz.AdjustWidth(nRight ); // adding some space looks better...

        if ( ( aFloatSz.Height() < nMaxHeight ) || ( mnDDLineCount && ( mnDDLineCount < mpImplLB->GetEntryList().GetEntryCount() ) ) )
        {
            // then we also need the vertical Scrollbar
            tools::Long nSBWidth = GetSettings().GetStyleSettings().GetScrollBarSize();
            aFloatSz.AdjustWidth(nSBWidth );
        }

        tools::Long nDesktopWidth = aDesktopRect.getOpenWidth();
        if (aFloatSz.Width() > nDesktopWidth)
            // Don't exceed the desktop width.
            aFloatSz.setWidth( nDesktopWidth );
    }

    tools::Long nDesktopHeight = aDesktopRect.getOpenHeight();

    //tdf#136943. If the popup won't fit either above/below then force the menu
    //to scroll if it won't fit
    {
        const vcl::Window* pRef = this;
        if ( pRef->GetParent() )
            pRef = pRef->GetParent();

        tools::Rectangle normRect( rParentRect );  // rRect is already relative to top-level window
        normRect.SetPos( pRef->ScreenToOutputPixel( normRect.TopLeft() ) );

        AbsoluteScreenPixelRectangle devRect(pRef->OutputToAbsoluteScreenPixel(normRect.TopLeft()),
                                             pRef->OutputToAbsoluteScreenPixel(normRect.BottomRight()));

        tools::Long nHeightAbove = devRect.Top() - aDesktopRect.Top();
        tools::Long nHeightBelow = aDesktopRect.Bottom() - devRect.Bottom();
        nDesktopHeight = std::min(nDesktopHeight, std::max(nHeightAbove, nHeightBelow));
    }

    if (aFloatSz.Height() > nDesktopHeight)
        aFloatSz.setHeight(nDesktopHeight);

    // Minimal height, in case height is not set to Float height.
    // The parent of FloatWin must be DropDown-Combo/Listbox.
    Size aParentSz = GetParent()->GetSizePixel();
    if( (!mnDDLineCount || !nLines) && ( aFloatSz.Height() < aParentSz.Height() ) )
        aFloatSz.setHeight( aParentSz.Height() );

    // do not get narrower than the parent...
    if( aFloatSz.Width() < aParentSz.Width() )
        aFloatSz.setWidth( aParentSz.Width() );

    // align height to entries...
    tools::Long nInnerHeight = aFloatSz.Height() - nTop - nBottom;
    tools::Long nEntryHeight = mpImplLB->GetEntryHeightWithMargin();
    if ( nInnerHeight % nEntryHeight )
    {
        nInnerHeight /= nEntryHeight;
        nInnerHeight *= nEntryHeight;
        aFloatSz.setHeight( nInnerHeight + nTop + nBottom );
    }

    if (aFloatSz.Width() < aSz.Width())
    {
        // The max width of list box entries exceeds the window width.
        // Account for the scroll bar height.
        tools::Long nSBWidth = GetSettings().GetStyleSettings().GetScrollBarSize();
        aFloatSz.AdjustHeight(nSBWidth );
    }

    return aFloatSz;
}

tools::Rectangle ImplListBoxFloatingWindow::GetParentRect() const
{
    // Get Rectangle at which popup will appear
    Size aSz = GetParent()->GetSizePixel();
    Point aPos = GetParent()->GetPosPixel();
    aPos = GetParent()->GetParent()->OutputToScreenPixel( aPos );
    // FIXME: this ugly hack is for Mac/Aqua
    // should be replaced by a real mechanism to place the float rectangle
    if( ImplGetSVData()->maNWFData.mbNoFocusRects &&
        GetParent()->IsNativeWidgetEnabled() )
    {
        const sal_Int32 nLeft = 4, nTop = 4, nRight = 4, nBottom = 4;
        aPos.AdjustX(nLeft );
        aPos.AdjustY(nTop );
        aSz.AdjustWidth( -(nLeft + nRight) );
        aSz.AdjustHeight( -(nTop + nBottom) );
    }
    tools::Rectangle aRect( aPos, aSz );

    // check if the control's parent is un-mirrored which is the case for form controls in a mirrored UI
    // where the document is unmirrored
    // because StartPopupMode() expects a rectangle in mirrored coordinates we have to re-mirror
    vcl::Window *pGrandparent = GetParent()->GetParent();
    const OutputDevice *pGrandparentOutDev = pGrandparent->GetOutDev();

    if( pGrandparent->GetOutDev()->ImplIsAntiparallel() )
        pGrandparentOutDev->ReMirror( aRect );

    return aRect;
}

void ImplListBoxFloatingWindow::StartFloat( bool bStartTracking )
{
    if( IsInPopupMode() )
        return;

    tools::Rectangle aRect = GetParentRect();

    Size aFloatSz = CalcFloatSize(aRect);

    SetSizePixel( aFloatSz );
    mpImplLB->SetSizePixel( GetOutputSizePixel() );

    sal_Int32 nPos = mpImplLB->GetEntryList().GetSelectedEntryPos( 0 );
    mnPopupModeStartSaveSelection = nPos;

    mpImplLB->GetMainWindow()->ResetLastPosPixel();
    // mouse-button right: close the List-Box-Float-win and don't stop the handling fdo#84795
    StartPopupMode( aRect, LISTBOX_FLOATWINPOPUPFLAGS );

    if( nPos != LISTBOX_ENTRY_NOTFOUND )
        mpImplLB->ShowProminentEntry( nPos );

    mpImplLB->GetMainWindow()->EnableMouseMoveSelect(bStartTracking);

    if ( mpImplLB->GetMainWindow()->IsGrabFocusAllowed() )
        mpImplLB->GetMainWindow()->GrabFocus();

    mpImplLB->GetMainWindow()->ImplClearLayoutData();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
