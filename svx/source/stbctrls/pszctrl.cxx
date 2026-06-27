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

#include <comphelper/propertyvalue.hxx>
#include <vcl/commandevent.hxx>
#include <vcl/event.hxx>
#include <vcl/fieldvalues.hxx>
#include <vcl/status.hxx>
#include <vcl/image.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Menu.hxx>
#include <vcl/weld/Window.hxx>
#include <vcl/weld/weldutils.hxx>
#include <svl/stritem.hxx>
#include <svl/ptitem.hxx>
#include <sfx2/module.hxx>
#include <svx/dialmgr.hxx>
#include <svx/statusitem.hxx>
#include <svx/strings.hrc>
#include <svl/intitem.hxx>
#include <sal/log.hxx>
#include <tools/fldunit.hxx>
#include <svtools/stringtransfer.hxx>
#include <rtl/ref.hxx>
#include <rtl/ustrbuf.hxx>

#include <svx/pszctrl.hxx>

#define PAINT_OFFSET    5

#include <editeng/sizeitem.hxx>


#include <global.hxx>

#include <svx/svxids.hrc>
#include <bitmaps.hlst>
#include <unotools/localedatawrapper.hxx>
#include <globstr.hrc>
#include <com/sun/star/beans/PropertyValue.hpp>

// None sentinel bit (no ScSubTotalFunc counterpart)
constexpr sal_uInt16 PSZ_FUNC_NONE_BIT = 16;

/*  [Description]

    Function used to create a text representation of
    a metric value

    nVal is the metric value in the unit eUnit.

    [cross reference]

    <SvxPosSizeStatusBarControl::Paint(const UserDrawEvent&)>
*/

OUString SvxPosSizeStatusBarControl::GetMetricStr_Impl( tools::Long nVal ) const
{
    // deliver and set the Metric of the application
    FieldUnit eOutUnit = SfxModule::GetModuleFieldUnit( getFrameInterface() );

    OUString sMetric;
    const sal_Unicode cSep = Application::GetSettings().GetLocaleDataWrapper().getNumDecimalSep()[0];
    sal_Int64 nConvVal = vcl::ConvertValue( nVal * 100, 0, 0, FieldUnit::MM_100TH, eOutUnit );

    if ( nConvVal < 0 && ( nConvVal / 100 == 0 ) )
        sMetric += "-";
    sMetric += OUString::number(nConvVal / 100);

    if( FieldUnit::NONE != eOutUnit )
    {
        sMetric += OUStringChar(cSep);
        sal_Int64 nFract = nConvVal % 100;

        if ( nFract < 0 )
            nFract *= -1;
        if ( nFract < 10 )
            sMetric += "0";
        sMetric += OUString::number(nFract);
    }

    return sMetric;
}


SFX_IMPL_STATUSBAR_CONTROL(SvxPosSizeStatusBarControl, SvxSizeItem);

namespace {

class FunctionPopup_Impl
{
    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Menu> m_xMenu;
    sal_uInt32        m_nSelected;
    OUString          m_sFuncValuesStr;
    static sal_uInt16 id_to_function(std::u16string_view rIdent);
    static OUString function_to_id(sal_uInt16 nFunc);
    OUString GetValueTextForFunction(sal_uInt16 nFunc) const;
    void UpdateMenuLabels();
public:
    explicit FunctionPopup_Impl(sal_uInt32 nCheckEncoded, const OUString& rFuncValuesStr);
    OUString Execute(weld::Window* pParent, const tools::Rectangle& rRect)
    {
        return m_xMenu->popup_at_rect(pParent, rRect);
    }
    sal_uInt32 GetSelected(std::u16string_view curident);
    OUString GetClipboardText();
};

}

sal_uInt16 FunctionPopup_Impl::id_to_function(std::u16string_view rIdent)
{
    if (rIdent == u"avg")
        return SUBTOTAL_FUNC_AVE;
    else if (rIdent == u"counta")
        return SUBTOTAL_FUNC_CNT2;
    else if (rIdent == u"count")
        return SUBTOTAL_FUNC_CNT;
    else if (rIdent == u"max")
        return SUBTOTAL_FUNC_MAX;
    else if (rIdent == u"min")
        return SUBTOTAL_FUNC_MIN;
    else if (rIdent == u"sum")
        return SUBTOTAL_FUNC_SUM;
    else if (rIdent == u"selection")
        return SUBTOTAL_FUNC_SELECTION_COUNT;
    else if (rIdent == u"none")
        return PSZ_FUNC_NONE_BIT;
    return 0;
}

OUString FunctionPopup_Impl::function_to_id(sal_uInt16 nFunc)
{
    switch (nFunc)
    {
        case SUBTOTAL_FUNC_AVE:
            return u"avg"_ustr;
        case SUBTOTAL_FUNC_CNT2:
            return u"counta"_ustr;
        case SUBTOTAL_FUNC_CNT:
            return u"count"_ustr;
        case SUBTOTAL_FUNC_MAX:
            return u"max"_ustr;
        case SUBTOTAL_FUNC_MIN:
            return u"min"_ustr;
        case SUBTOTAL_FUNC_SUM:
            return u"sum"_ustr;
        case SUBTOTAL_FUNC_SELECTION_COUNT:
            return u"selection"_ustr;
        case PSZ_FUNC_NONE_BIT:
            return u"none"_ustr;
    }
    return {};
}

FunctionPopup_Impl::FunctionPopup_Impl(sal_uInt32 nCheckEncoded, const OUString& rFuncValuesStr)
    : m_xBuilder(Application::CreateBuilder(nullptr, u"svx/ui/functionmenu.ui"_ustr))
    , m_xMenu(m_xBuilder->weld_menu(u"menu"_ustr))
    , m_nSelected(nCheckEncoded)
    , m_sFuncValuesStr(rFuncValuesStr)
{
    for ( sal_uInt16 nCheck = SUBTOTAL_FUNC_AVE; nCheck < PSZ_FUNC_NONE_BIT; ++nCheck )
        if ( nCheckEncoded & (1u << nCheck) )
            m_xMenu->set_active(function_to_id(nCheck), true);

    if (!m_sFuncValuesStr.isEmpty())
        UpdateMenuLabels();

    m_xMenu->append_separator(u"sep_copy"_ustr);
    m_xMenu->append(u"copy_all"_ustr, SvxResId(RID_SVXSTR_COPY_VALUES));
}

sal_uInt32 FunctionPopup_Impl::GetSelected(std::u16string_view curident)
{
    sal_uInt16 nCurItemId = id_to_function(curident);
    if ( nCurItemId == PSZ_FUNC_NONE_BIT )
        m_nSelected = ( 1 << PSZ_FUNC_NONE_BIT );
    else
    {
        m_nSelected &= (~( 1u << PSZ_FUNC_NONE_BIT )); // Clear the "None" bit
        m_nSelected ^= ( 1u << nCurItemId ); // Toggle the bit corresponding to nCurItemId
        if ( !m_nSelected )
            m_nSelected = ( 1u << PSZ_FUNC_NONE_BIT );
    }
    return m_nSelected;
}

OUString FunctionPopup_Impl::GetValueTextForFunction(sal_uInt16 nFunc) const
{
    // Search by numeric function ID prefix (locale-independent)
    OUString sSearch = OUString::number(nFunc) + ":";
    sal_Int32 nIdx = m_sFuncValuesStr.indexOf(sSearch);

    if (nIdx < 0)
        return {};

    // Locate where the value starts by finding the second ": "
    sal_Int32 nNameEnd = m_sFuncValuesStr.indexOf(": ", nIdx + sSearch.getLength());
    if (nNameEnd < 0)
        return {};

    // Return "Name: Value" without the ID prefix
    sal_Int32 nNameStart = nIdx + sSearch.getLength();
    sal_Int32 nEnd = m_sFuncValuesStr.indexOf("; ", nNameEnd + 2);
    if (nEnd >= 0)
        return m_sFuncValuesStr.copy(nNameStart, nEnd - nNameStart);
    return m_sFuncValuesStr.copy(nNameStart);
}

void FunctionPopup_Impl::UpdateMenuLabels()
{
    for (sal_uInt16 nFunc = SUBTOTAL_FUNC_AVE; nFunc < PSZ_FUNC_NONE_BIT; ++nFunc)
    {
        OUString sFullText = GetValueTextForFunction(nFunc);
        if (sFullText.isEmpty())
            continue;

        OUString sMenuId = function_to_id(nFunc);
        if (sMenuId.isEmpty())
            continue;

        m_xMenu->set_label(sMenuId, sFullText);
    }
}

OUString FunctionPopup_Impl::GetClipboardText()
{
    OUStringBuffer aBuf;
    for (sal_uInt16 nCheck = SUBTOTAL_FUNC_AVE; nCheck < PSZ_FUNC_NONE_BIT; ++nCheck)
    {
        if ( !(m_nSelected & (1u << nCheck)) )
            continue;

        OUString sFullText = GetValueTextForFunction(nCheck);
        if (sFullText.isEmpty())
        {
            // Fallback: use the menu label
            OUString sId = function_to_id(nCheck);
            sFullText = m_xMenu->get_label(sId);
        }

        if ( !sFullText.isEmpty() )
        {
            if ( !aBuf.isEmpty() )
                aBuf.append("\n");
            aBuf.append(sFullText);
        }
    }
    return aBuf.makeStringAndClear();
}

struct SvxPosSizeStatusBarControl_Impl

/*  [Description]

    This implementation-structure of the class SvxPosSizeStatusBarControl
    is done for the un-linking of the changes of the exported interface such as
    the toning down of symbols that are visible externally.

    One instance exists for each SvxPosSizeStatusBarControl-instance
    during its life time
*/

{
    Point     aPos;       // valid when a position is shown
    Size      aSize;      // valid when a size is shown
    OUString  aStr;       // valid when a text is shown
    OUString  aFuncValuesStr;  // function values like "9:Sum: 123; 4:Max: 456"
    bool      bPos;       // show position ?
    bool      bSize;      // set size ?
    bool      bTable;     // set table index ?
    bool      bHasMenu;   // set StarCalc popup menu ?
    sal_uInt32  nFunctionSet;  // the selected StarCalc functions encoded in 32 bits
    Image     aPosImage;  // Image to show the position
    Image     aSizeImage; // Image to show the size
};

/*  [Description]

    Ctor():
    Create an instance of the implementation class,
    load the images for the position and size
*/

#define STR_POSITION ".uno:Position"
#define STR_TABLECELL ".uno:StateTableCell"
#define STR_FUNC ".uno:StatusBarFunc"

SvxPosSizeStatusBarControl::SvxPosSizeStatusBarControl( sal_uInt16 _nSlotId,
                                                        sal_uInt16 _nId,
                                                        StatusBar& rStb ) :
    SfxStatusBarControl( _nSlotId, _nId, rStb ),
    pImpl( new SvxPosSizeStatusBarControl_Impl )
{
    pImpl->bPos = false;
    pImpl->bSize = false;
    pImpl->bTable = false;
    pImpl->bHasMenu = false;
    pImpl->nFunctionSet = 0;
    pImpl->aPosImage = Image(StockImage::Yes, RID_SVXBMP_POSITION);
    pImpl->aSizeImage = Image(StockImage::Yes, RID_SVXBMP_SIZE);

    addStatusListener( u"" STR_POSITION ""_ustr);         // SID_ATTR_POSITION
    addStatusListener( u"" STR_TABLECELL ""_ustr);   // SID_TABLE_CELL
    addStatusListener( u"" STR_FUNC ""_ustr);    // SID_PSZ_FUNCTION
    ImplUpdateItemText();
}

/*  [Description]

    Dtor():
    remove the pointer to the implementation class, so that the timer is stopped

*/

SvxPosSizeStatusBarControl::~SvxPosSizeStatusBarControl()
{
}


/*  [Description]

    SID_PSZ_FUNCTION activates the popup menu for Calc:

    Status overview
    Depending on the type of the item, a special setting is enabled, the others disabled.

                NULL/Void   SfxPointItem    SvxSizeItem     SfxStringItem
    ------------------------------------------------------------------------
    Position    sal_False                                       FALSE
    Size        FALSE                       TRUE            FALSE
    Text        sal_False                       sal_False           TRUE

*/

void SvxPosSizeStatusBarControl::StateChangedAtStatusBarControl( sal_uInt16 nSID, SfxItemState eState,
                                               const SfxPoolItem* pState )
{
    // Because the combi-controller, always sets the current Id as HelpId
    // first clean the cached HelpText
    GetStatusBar().SetHelpText( GetId(), u""_ustr );

    switch ( nSID )
    {
        case SID_ATTR_POSITION : GetStatusBar().SetHelpId( GetId(), u"" STR_POSITION ""_ustr ); break;
        case SID_TABLE_CELL: GetStatusBar().SetHelpId( GetId(), u"" STR_TABLECELL ""_ustr ); break;
        case SID_PSZ_FUNCTION: GetStatusBar().SetHelpId( GetId(), u"" STR_FUNC ""_ustr ); break;
        default: break;
    }

    if ( nSID == SID_PSZ_FUNCTION )
    {
        if ( eState == SfxItemState::DEFAULT )
        {
            pImpl->bHasMenu = true;
            if ( auto pUInt32Item = dynamic_cast< const SfxUInt32Item* >(pState) )
                pImpl->nFunctionSet = pUInt32Item->GetValue();
        }
        else
            pImpl->bHasMenu = false;
    }
    else if ( SfxItemState::DEFAULT != eState )
    {
        // don't switch to empty display before an empty state was
        // notified for all display types

        if ( nSID == SID_TABLE_CELL )
            pImpl->bTable = false;
        else if ( nSID == SID_ATTR_POSITION )
            pImpl->bPos = false;
        else if ( nSID == GetSlotId() )     // controller is registered for SID_ATTR_SIZE
            pImpl->bSize = false;
        else
        {
            SAL_WARN( "svx.stbcrtls","unknown slot id");
        }
    }
    else if ( auto pPointItem = dynamic_cast<const SfxPointItem*>( pState) )
    {
        // show position
        pImpl->aPos = pPointItem->GetValue();
        pImpl->bPos = true;
        pImpl->bTable = false;
    }
    else if ( auto pSizeItem = dynamic_cast<const SvxSizeItem*>( pState) )
    {
        // show size
        pImpl->aSize = pSizeItem->GetSize();
        pImpl->bSize = true;
        pImpl->bTable = false;
    }
    else if ( auto pStatusItem = dynamic_cast<const SvxStatusItem*>( pState) )
    {
        // show string (table cell or different)
        pImpl->aStr = pStatusItem->GetValue();
        pImpl->bTable = true;
        pImpl->bPos = false;
        pImpl->bSize = false;
        if (!pImpl->aStr.isEmpty())
        {
            OUString sTip;
            switch (pStatusItem->GetCategory())
            {
                case StatusCategory::TableCell:
                    sTip = SvxResId(RID_SVXSTR_TABLECELL_HINT);
                    break;
                case StatusCategory::Section:
                    sTip = SvxResId(RID_SVXSTR_SECTION_HINT);
                    break;
                case StatusCategory::TableOfContents:
                    sTip = SvxResId(RID_SVXSTR_TOC_HINT);
                    break;
                case StatusCategory::Numbering:
                    sTip = SvxResId(RID_SVXSTR_NUMBERING_HINT);
                    break;
                case StatusCategory::ListStyle:
                    sTip = SvxResId(RID_SVXSTR_LIST_STYLE_HINT);
                    break;
                case StatusCategory::Formula:
                    pImpl->aFuncValuesStr = pStatusItem->GetValue();
                    {
                        // Strip numeric ID prefixes ("9:Sum: 123" → "Sum: 123") for display
                        OUString sRaw = pStatusItem->GetValue();
                        OUStringBuffer aBuf;
                        sal_Int32 nStart = 0;
                        while (nStart < sRaw.getLength())
                        {
                            sal_Int32 nSep = sRaw.indexOf("; ", nStart);
                            OUString sSegment;
                            if (nSep >= 0)
                            {
                                sSegment = sRaw.copy(nStart, nSep - nStart);
                                nStart = nSep + 2;
                            }
                            else
                            {
                                sSegment = sRaw.copy(nStart);
                                nStart = sRaw.getLength();
                            }
                            // Remove leading ID prefix like "9:"
                            sal_Int32 nColon = sSegment.indexOf(':');
                            if (nColon > 0 && nColon < 3) // ID is 1-2 digits
                                sSegment = sSegment.copy(nColon + 1);
                            if (!aBuf.isEmpty())
                                aBuf.append("; ");
                            aBuf.append(sSegment);
                        }
                        pImpl->aStr = aBuf.makeStringAndClear();
                    }
                    sTip = SvxResId(RID_SVXSTR_FORMULA_HINT);
                    break;
                case StatusCategory::RowColumn:
                    sTip = SvxResId(RID_SVXSTR_ROW_COLUMN_HINT);
                    break;
                case StatusCategory::NONE:
                    break;
            }
            GetStatusBar().SetQuickHelpText(GetId(), sTip);
        }
    }
    else if ( auto pStringItem = dynamic_cast<const SfxStringItem*>( pState) )
    {
        SAL_WARN( "svx.stbcrtls", "this should be a SvxStatusItem not a SfxStringItem" );
        // show string (table cell or different)
        pImpl->aStr = pStringItem->GetValue();
        pImpl->bTable = true;
        pImpl->bPos = false;
        pImpl->bSize = false;
    }
    else
    {
        SAL_WARN( "svx.stbcrtls", "invalid item type" );
        pImpl->bPos = false;
        pImpl->bSize = false;
        pImpl->bTable = false;
    }

    GetStatusBar().SetItemData( GetId(), nullptr );

    ImplUpdateItemText();
}


/*  [Description]

    execute popup menu, when the status enables this
*/

void SvxPosSizeStatusBarControl::Command( const CommandEvent& rCEvt )
{
    if ( rCEvt.GetCommand() == CommandEventId::ContextMenu && pImpl->bHasMenu )
    {
        sal_uInt32 nSelect = pImpl->nFunctionSet;
        if (!nSelect)
            nSelect = ( 1 << PSZ_FUNC_NONE_BIT );
        tools::Rectangle aRect(rCEvt.GetMousePosPixel(), Size(1,1));
        weld::Window* pParent = weld::GetPopupParent(GetStatusBar(), aRect);
        FunctionPopup_Impl aMenu(nSelect, pImpl->aFuncValuesStr);
        OUString sIdent = aMenu.Execute(pParent, aRect);
        if (!sIdent.isEmpty())
        {
            if (sIdent == u"copy_all")
            {
                OUString aClipText = aMenu.GetClipboardText();
                rtl::Reference<svt::OStringTransferable> pTransfer
                    = new svt::OStringTransferable(aClipText);
                pTransfer->CopyToClipboard(pParent->get_clipboard());
            }
            else
            {
                nSelect = aMenu.GetSelected(sIdent);
                if (nSelect)
                {
                    if (nSelect == (1 << PSZ_FUNC_NONE_BIT))
                        nSelect = 0;

                    css::uno::Any a;
                    SfxUInt32Item aItem( SID_PSZ_FUNCTION, nSelect );
                    aItem.QueryValue( a );
                    css::uno::Sequence< css::beans::PropertyValue > aArgs{ comphelper::makePropertyValue(
                        u"StatusBarFunc"_ustr, a) };
                    execute( u".uno:StatusBarFunc"_ustr, aArgs );
                }
            }
        }
    }
    else
        SfxStatusBarControl::Command( rCEvt );
}


/*  [Description]

    Depending on the type to be shown, the value us shown. First the
    rectangle is repainted (removed).
*/

void SvxPosSizeStatusBarControl::Paint( const UserDrawEvent& rUsrEvt )
{
    vcl::RenderContext* pDev = rUsrEvt.GetRenderContext();

    const tools::Rectangle& rRect = rUsrEvt.GetRect();
    StatusBar& rBar = GetStatusBar();
    Point aItemPos = rBar.GetItemTextPos( GetId() );
    auto popIt = pDev->ScopedPush(vcl::PushFlags::LINECOLOR | vcl::PushFlags::FILLCOLOR);
    pDev->SetLineColor();
    pDev->SetFillColor( pDev->GetBackground().GetColor() );

    if ( pImpl->bPos || pImpl->bSize )
    {
        // count the position for showing the size
        tools::Long nSizePosX =
            rRect.Left() + rRect.GetWidth() / 2 + PAINT_OFFSET;
        // draw position
        Point aPnt = rRect.TopLeft();
        aPnt.AdjustX(PAINT_OFFSET );
        // vertically centered
        const tools::Long nSizePosY =
            (rRect.GetHeight() - pImpl->aPosImage.GetSizePixel().Height()) / 2;
        aPnt.AdjustY( nSizePosY );

        pDev->DrawImage( aPnt, pImpl->aPosImage );
        aPnt.AdjustX(pImpl->aPosImage.GetSizePixel().Width() );
        aPnt.AdjustX(PAINT_OFFSET );
        OUString aStr = GetMetricStr_Impl( pImpl->aPos.X()) + " / " +
            GetMetricStr_Impl( pImpl->aPos.Y());
        tools::Rectangle aRect(aPnt, Point(nSizePosX, rRect.Bottom()));
        pDev->DrawRect(aRect);
        vcl::Region aOrigRegion(pDev->GetClipRegion());
        pDev->SetClipRegion(vcl::Region(aRect));
        pDev->DrawText(aPnt, aStr);
        pDev->SetClipRegion(aOrigRegion);

        // draw the size, when available
        aPnt.setX( nSizePosX );

        if ( pImpl->bSize )
        {
            pDev->DrawImage( aPnt, pImpl->aSizeImage );
            aPnt.AdjustX(pImpl->aSizeImage.GetSizePixel().Width() );
            Point aDrwPnt = aPnt;
            aPnt.AdjustX(PAINT_OFFSET );
            aStr = GetMetricStr_Impl( pImpl->aSize.Width() ) + " x " +
                GetMetricStr_Impl( pImpl->aSize.Height() );
            aRect = tools::Rectangle(aDrwPnt, rRect.BottomRight());
            pDev->DrawRect(aRect);
            aOrigRegion = pDev->GetClipRegion();
            pDev->SetClipRegion(vcl::Region(aRect));
            pDev->DrawText(aPnt, aStr);
            pDev->SetClipRegion(aOrigRegion);
        }
        else
            pDev->DrawRect( tools::Rectangle( aPnt, rRect.BottomRight() ) );
    }
    else if ( pImpl->bTable )
    {
        pDev->DrawRect( rRect );
        pDev->DrawText( Point(
            rRect.Left() + rRect.GetWidth() / 2 - pDev->GetTextWidth( pImpl->aStr ) / 2,
            aItemPos.Y() ), pImpl->aStr );
    }
    else
    {
        // Empty display if neither size nor table position are available.
        // Date/Time are no longer used (#65302#).
        pDev->DrawRect( rRect );
    }
}

void SvxPosSizeStatusBarControl::ImplUpdateItemText()
{
    //  set only strings as text at the statusBar, so that the Help-Tips
    //  can work with the text, when it is too long for the statusBar
    OUString aText;
    int nCharsWidth = -1;
    if ( pImpl->bPos || pImpl->bSize )
    {
        aText = GetMetricStr_Impl( pImpl->aPos.X()) + " / " +
            GetMetricStr_Impl( pImpl->aPos.Y());
        // widest X/Y string looks like "-999,99"
        nCharsWidth = 1 + 6 + 3 + 6; // icon + x + slash + y
        if ( pImpl->bSize )
        {
            aText += " " + GetMetricStr_Impl( pImpl->aSize.Width() ) + " x " +
                GetMetricStr_Impl( pImpl->aSize.Height() );
            nCharsWidth += 1 + 1 + 4 + 3 + 4; // icon + space + w + x + h
        }
    }
    else if ( pImpl->bTable )
       aText = pImpl->aStr;

    GetStatusBar().SetItemText( GetId(), aText, nCharsWidth );
}
/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
