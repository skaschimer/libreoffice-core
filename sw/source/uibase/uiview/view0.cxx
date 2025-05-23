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

#include <config_features.h>
#include <config_wasm_strip.h>

#include <SwSpellDialogChildWindow.hxx>
#include <svl/eitem.hxx>
#include <unotools/configmgr.hxx>
#include <unotools/linguprops.hxx>
#include <unotools/lingucfg.hxx>
#include <officecfg/Office/Common.hxx>
#include <viewopt.hxx>
#include <globals.h>
#include <sfx2/infobar.hxx>
#include <sfx2/lokhelper.hxx>
#include <sfx2/request.hxx>
#include <svl/whiter.hxx>
#include <svx/srchdlg.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/sidebar/SidebarChildWindow.hxx>
#include <uivwimp.hxx>
#include <unotxdoc.hxx>
#include <avmedia/mediaplayer.hxx>
#include <swmodule.hxx>
#include <com/sun/star/linguistic2/XLinguProperties.hpp>
#include <comphelper/servicehelper.hxx>
#include <osl/diagnose.h>

#include <sfx2/objface.hxx>
#include <wrtsh.hxx>
#include <edtwin.hxx>
#include <view.hxx>
#include <docsh.hxx>
#include <doc.hxx>
#include <globals.hrc>
#include <cmdid.h>
#include <globdoc.hxx>
#include <wview.hxx>
#include <OnlineAccessibilityCheck.hxx>

#define ShellClass_SwView
#define ShellClass_Text
#define ShellClass_TextDrawText

#include <sfx2/msg.hxx>
#include <swslots.hxx>
#include <PostItMgr.hxx>

#include <unotools/moduleoptions.hxx>
#include <sfx2/viewfac.hxx>

#include <memory>
#include <swabstdlg.hxx>

#include <sfx2/sidebar/SidebarController.hxx>

#include <strings.hrc>

using namespace ::com::sun::star;

SFX_IMPL_NAMED_VIEWFACTORY(SwView, "Default")
{
    if (comphelper::IsFuzzing() || SvtModuleOptions().IsWriterInstalled())
    {
        SFX_VIEW_REGISTRATION(SwDocShell);
        SFX_VIEW_REGISTRATION(SwGlobalDocShell);
    }
}

SFX_IMPL_INTERFACE(SwView, SfxViewShell)

void SwView::InitInterface_Impl()
{
    GetStaticInterface()->RegisterChildWindow(SID_NAVIGATOR, true);

    GetStaticInterface()->RegisterChildWindow(::sfx2::sidebar::SidebarChildWindow::GetChildWindowId());

    GetStaticInterface()->RegisterChildWindow(SfxInfoBarContainerChild::GetChildWindowId());
    GetStaticInterface()->RegisterChildWindow(SvxSearchDialogWrapper::GetChildWindowId());
    GetStaticInterface()->RegisterChildWindow(SwSpellDialogChildWindow::GetChildWindowId());
    GetStaticInterface()->RegisterChildWindow(FN_REDLINE_ACCEPT);
    GetStaticInterface()->RegisterChildWindow(SID_HYPERLINK_DIALOG);
    GetStaticInterface()->RegisterChildWindow(FN_WORDCOUNT_DIALOG);
#if HAVE_FEATURE_AVMEDIA
    GetStaticInterface()->RegisterChildWindow(::avmedia::MediaPlayer::GetChildWindowId());
#endif
    GetStaticInterface()->RegisterChildWindow(FN_INSERT_FIELD_DATA_ONLY);

    GetStaticInterface()->RegisterChildWindow(FN_SYNC_LABELS, false, SfxShellFeature::SwChildWindowLabel);

    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_TOOLS, SfxVisibilityFlags::Standard|SfxVisibilityFlags::Server,
                                            ToolbarId::Tools_Toolbox);
}


ShellMode SwView::GetShellMode() const
{
    return m_pViewImpl->GetShellMode();
}

view::XSelectionSupplier* SwView::GetUNOObject()
{
    return m_pViewImpl->GetUNOObject();
}

void SwView::SetMailMergeConfigItem(std::shared_ptr<SwMailMergeConfigItem> const & rConfigItem)
{
    m_pViewImpl->SetMailMergeConfigItem(rConfigItem);
    UIFeatureChanged();
}

std::shared_ptr<SwMailMergeConfigItem> const & SwView::GetMailMergeConfigItem() const
{
    return m_pViewImpl->GetMailMergeConfigItem();
}

static bool lcl_IsViewMarks( const SwViewOption& rVOpt )
{
    return  rVOpt.IsFieldShadings();
}
static void lcl_SetViewMarks(SwViewOption& rVOpt, bool bOn )
{
    rVOpt.SetAppearanceFlag(
            ViewOptFlags::FieldShadings, bOn, true);
}

static void lcl_SetViewMetaChars( SwViewOption& rVOpt, bool bOn)
{
    rVOpt.SetViewMetaChars( bOn );
    if(bOn && !(rVOpt.IsParagraph()     ||
            rVOpt.IsTab()       ||
            rVOpt.IsLineBreak() ||
            rVOpt.IsShowHiddenChar() ||
            rVOpt.IsShowBookmarks() ||
            rVOpt.IsBlank()))
    {
        rVOpt.SetParagraph(bOn);
        rVOpt.SetTab(bOn);
        rVOpt.SetLineBreak(bOn);
        rVOpt.SetBlank(bOn);
        rVOpt.SetShowHiddenChar(bOn);
        rVOpt.SetShowBookmarks(bOn);
    }
}

void SwView::RecheckBrowseMode()
{
    // OS: pay attention to numerical order!
    static sal_uInt16 const aInva[] =
        {
            //SID_NEWWINDOW,/*5620*/
            SID_BROWSER_MODE, /*6313*/
            SID_RULER_BORDERS, SID_RULER_PAGE_POS,
            //SID_ATTR_LONG_LRSPACE,
            SID_HTML_MODE,
            SID_RULER_PROTECT, /* 10915 */
            //SID_AUTOSPELL_CHECK,
            //SID_AUTOSPELL_MARKOFF,
            SID_TOGGLE_RESOLVED_NOTES, /* 11672*/
            FN_RULER,       /*20211*/
            FN_VIEW_BOUNDARIES,  /*20212*/
            FN_VIEW_GRAPHIC,    /*20213*/
            FN_VIEW_BOUNDS,     /**/
            FN_VIEW_FIELDS,     /*20215*/
            FN_VLINEAL,             /*20216*/
            FN_VSCROLLBAR,      /*20217*/
            FN_HSCROLLBAR,      /*20218*/
            FN_VIEW_SECTION_BOUNDARIES, /*20219*/
            FN_VIEW_META_CHARS, /**/
            FN_VIEW_MARKS,      /**/
            //FN_VIEW_FIELDNAME,    /**/
            FN_VIEW_TABLEGRID,  /*20227*/
            FN_PRINT_LAYOUT, /*20237*/
            FN_QRY_MERGE,   /*20364*/
            FN_SHADOWCURSOR, /**/
            FN_SINGLE_PAGE_PER_ROW, /**/
            FN_MULTIPLE_PAGES_PER_ROW, /**/
            FN_BOOKVIEW, /**/
            0
        };
    // the view must not exist!
    GetViewFrame().GetBindings().Invalidate(aInva);
    CheckVisArea();

    SvxZoomType eType;
    if( GetWrtShell().GetViewOptions()->getBrowseMode() && SvxZoomType::PERCENT != (eType =
        GetWrtShell().GetViewOptions()->GetZoomType()) )
        SetZoom( eType );
    InvalidateBorder();
}

// State of view options

void SwView::StateViewOptions(SfxItemSet &rSet)
{
    SfxWhichIter aIter(rSet);
    sal_uInt16 nWhich = aIter.FirstWhich();
    SfxBoolItem aBool;
    const SwViewOption* pOpt = GetWrtShell().GetViewOptions();

    while(nWhich)
    {
        bool bReadonly = GetDocShell()->IsReadOnly();
        if (bReadonly && nWhich != FN_VIEW_GRAPHIC && nWhich != FN_HIGHLIGHT_CHAR_DF
            && nWhich != SID_AUTOSPELL_CHECK)
        {
            rSet.DisableItem(nWhich);
            nWhich = 0;
        }
        switch(nWhich)
        {
            case FN_RULER:
            {
                if(!pOpt->IsViewHRuler(true) && !pOpt->IsViewVRuler(true))
                {
                    rSet.DisableItem(nWhich);
                    nWhich = 0;
                }
                else
                    aBool.SetValue( pOpt->IsViewAnyRuler());
            }
            break;
            case SID_BROWSER_MODE:
            case FN_PRINT_LAYOUT:
            {
                bool bState = pOpt->getBrowseMode();
                if(FN_PRINT_LAYOUT == nWhich)
                    bState = !bState;
                aBool.SetValue( bState );
            }
            break;
            case FN_VIEW_BOUNDARIES:
                aBool.SetValue( pOpt->IsShowBoundaries()); break;
            case FN_VIEW_BOUNDS:
                aBool.SetValue( pOpt->IsTextBoundaries()); break;
            case FN_VIEW_SECTION_BOUNDARIES:
                aBool.SetValue(pOpt->IsSectionBoundaries()); break;
            case FN_VIEW_GRAPHIC:
                aBool.SetValue( pOpt->IsGraphic() ); break;
            case FN_VIEW_FIELDS:
                aBool.SetValue( pOpt->IsFieldShadings() ); break;
            case FN_VIEW_FIELDNAME:
                aBool.SetValue( pOpt->IsFieldName() ); break;
            case FN_VIEW_MARKS:
                aBool.SetValue( lcl_IsViewMarks(*pOpt) ); break;
            case FN_VIEW_META_CHARS:
                aBool.SetValue( pOpt->IsViewMetaChars() ); break;
            case FN_VIEW_TABLEGRID:
                aBool.SetValue( pOpt->IsTableBoundaries() ); break;
            case SID_TOGGLE_NOTES:
            {
                if (!GetPostItMgr()->HasNotes())
                {
                    rSet.DisableItem(nWhich);
                    nWhich = 0;
                }
                else
                    aBool.SetValue( pOpt->IsPostIts());
                break;
            }
            case SID_TOGGLE_RESOLVED_NOTES:
            {
                if (!GetPostItMgr()->HasNotes())
                {
                    rSet.DisableItem(nWhich);
                    nWhich = 0;
                }
                else
                    aBool.SetValue( pOpt->IsResolvedPostIts());
                break;
            }
            case FN_VIEW_HIDDEN_PARA:
                aBool.SetValue( pOpt->IsShowHiddenPara()); break;
            case FN_VIEW_HIDE_WHITESPACE:
            {
                if (pOpt->getBrowseMode() || !pOpt->CanHideWhitespace())
                {
                    rSet.DisableItem(nWhich);
                    nWhich = 0;
                }
                else
                    aBool.SetValue(pOpt->IsHideWhitespaceMode());
                break;
            }
            case FN_VIEW_SHOW_WHITESPACE:
            {
                aBool.SetValue(!pOpt->IsHideWhitespaceMode());
                break;
            }
            case SID_GRID_VISIBLE:
                aBool.SetValue( pOpt->IsGridVisible() ); break;
            case SID_GRID_USE:
                aBool.SetValue( pOpt->IsSnap() ); break;
            case SID_HELPLINES_MOVE:
                aBool.SetValue( pOpt->IsCrossHair() ); break;
            case FN_VIEW_SMOOTH_SCROLL:
                aBool.SetValue( pOpt->IsSmoothScroll()); break;
            case FN_VLINEAL:
                aBool.SetValue( StatVRuler() ); break;
            case FN_HSCROLLBAR:
                if( pOpt->getBrowseMode() )
                {
                    rSet.DisableItem(nWhich);
                    nWhich = 0;
                }
                else
                    aBool.SetValue( IsHScrollbarVisible() );
                break;
            case FN_VSCROLLBAR:
                aBool.SetValue( IsVScrollbarVisible() ); break;
            case SID_AUTOSPELL_CHECK:
                aBool.SetValue( pOpt->IsOnlineSpell() );
            break;
            case SID_ACCESSIBILITY_CHECK_ONLINE:
            {
                bool bOnlineAccessibilityCheck = officecfg::Office::Common::Accessibility::OnlineAccessibilityCheck::get();
                aBool.SetValue(bOnlineAccessibilityCheck);
            }
            break;
            case FN_SHADOWCURSOR:
                if ( pOpt->getBrowseMode() )
                {
                    rSet.DisableItem( nWhich );
                    nWhich = 0;
                }
                else
                    aBool.SetValue( pOpt->IsShadowCursor() );
            break;
            case FN_SHOW_INLINETOOLTIPS:
              aBool.SetValue( pOpt->IsShowInlineTooltips() );
            break;
            case FN_USE_HEADERFOOTERMENU:
              aBool.SetValue( pOpt->IsUseHeaderFooterMenu() );
            break;
            case FN_SHOW_OUTLINECONTENTVISIBILITYBUTTON:
              aBool.SetValue( pOpt->IsShowOutlineContentVisibilityButton() );
            break;
            case FN_SHOW_CHANGES_IN_MARGIN:
              aBool.SetValue( pOpt->IsShowChangesInMargin() );
            break;
            case FN_HIGHLIGHT_CHAR_DF:
              aBool.SetValue(m_bIsHighlightCharDF);
            break;
            case SID_SPOTLIGHT_PARASTYLES:
                aBool.SetValue(m_bIsSpotlightParaStyles);
            break;
            case SID_SPOTLIGHT_CHARSTYLES:
                aBool.SetValue(m_bIsSpotlightCharStyles);
            break;
            case FN_SINGLE_PAGE_PER_ROW:
                aBool.SetValue( !pOpt->IsMultipageView());
            break;
            case FN_MULTIPLE_PAGES_PER_ROW:
                aBool.SetValue( pOpt->GetViewLayoutColumns() == 0);
            break;
            case FN_BOOKVIEW:
                aBool.SetValue( pOpt->IsViewLayoutBookMode());
            break;
            case SID_CLICK_CHANGE_ROTATION:
                aBool.SetValue( pOpt->IsClickChangeRotation());
            break;
        }

        if( nWhich )
        {
            aBool.SetWhich( nWhich );
            rSet.Put( aBool );
        }
        nWhich = aIter.NextWhich();
    }
}

// execute view options

void SwView::ExecViewOptions(SfxRequest &rReq)
{
    std::optional<SwViewOption> pOpt( *GetWrtShell().GetViewOptions() );
    bool bModified = GetWrtShell().IsModified();

    int eState = STATE_TOGGLE;
    bool bSet = false;
    bool bBrowseModeChanged = false;

    const SfxItemSet *pArgs = rReq.GetArgs();
    sal_uInt16 nSlot = rReq.GetSlot();
    const SfxPoolItem* pAttr=nullptr;

    if( pArgs && SfxItemState::SET == pArgs->GetItemState( nSlot , false, &pAttr ))
    {
        bSet = static_cast<const SfxBoolItem*>(pAttr)->GetValue();
        eState = bSet ? STATE_ON : STATE_OFF;
    }

    bool bFlag = STATE_ON == eState;
    uno::Reference< linguistic2::XLinguProperties >  xLngProp( ::GetLinguPropertySet() );

    switch ( nSlot )
    {
    case FN_VIEW_GRAPHIC:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsGraphic();
        pOpt->SetGraphic( bFlag );
        break;

    case FN_VIEW_FIELDS:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsFieldShadings() ;
        pOpt->SetAppearanceFlag(ViewOptFlags::FieldShadings, bFlag, true );
        break;

    case FN_VIEW_BOUNDS:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsTextBoundaries();
        pOpt->SetTextBoundaries( bFlag );
        break;

    case FN_VIEW_SECTION_BOUNDARIES:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsSectionBoundaries();
        pOpt->SetSectionBoundaries( bFlag );
        break;

    case FN_VIEW_TABLEGRID:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsTableBoundaries();
        pOpt->SetTableBoundaries( bFlag );
        break;

    case FN_VIEW_BOUNDARIES:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsShowBoundaries();
        pOpt->SetShowBoundaries( bFlag );
        break;

    case SID_GRID_VISIBLE:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsGridVisible();

        pOpt->SetGridVisible( bFlag );
        break;

    case SID_GRID_USE:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsSnap();

        pOpt->SetSnap( bFlag );
        break;

    case SID_HELPLINES_MOVE:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsCrossHair();

        pOpt->SetCrossHair( bFlag );
        break;

    case SID_BROWSER_MODE:
        bBrowseModeChanged = !pOpt->getBrowseMode();
        pOpt->setBrowseMode(true );
        break;

    case FN_PRINT_LAYOUT:
        bBrowseModeChanged = pOpt->getBrowseMode();
        pOpt->setBrowseMode( false );
        break;

    case SID_TOGGLE_NOTES:
        if ( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsPostIts();

        GetPostItMgr()->SetLayout();
        pOpt->SetPostIts( bFlag );
        if (pOpt->IsPostIts())
            GetPostItMgr()->CheckMetaText();
        break;

    case SID_TOGGLE_RESOLVED_NOTES:
        if ( STATE_TOGGLE == eState )
            bFlag = pOpt->IsResolvedPostIts();

        GetPostItMgr()->ShowHideResolvedNotes(!bFlag);

        GetPostItMgr()->SetLayout();
        pOpt->SetResolvedPostIts( !bFlag );

        break;

    case FN_VIEW_HIDDEN_PARA:
        if ( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsShowHiddenPara();

        pOpt->SetShowHiddenPara( bFlag );
        break;

    case FN_VIEW_HIDE_WHITESPACE:
        if ( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsHideWhitespaceMode();

        pOpt->SetHideWhitespaceMode(bFlag);
        break;

    case FN_VIEW_SHOW_WHITESPACE:
        if ( STATE_TOGGLE == eState )
            bFlag = pOpt->IsHideWhitespaceMode();

        pOpt->SetHideWhitespaceMode(!bFlag);
        break;

    case FN_VIEW_SMOOTH_SCROLL:

        if ( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsSmoothScroll();

        pOpt->SetSmoothScroll( bFlag );
        break;

    case FN_VLINEAL:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsViewVRuler();

        pOpt->SetViewVRuler( bFlag );
        break;

    case FN_VSCROLLBAR:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsViewVScrollBar();

        pOpt->SetViewVScrollBar( bFlag );
        break;

    case FN_HSCROLLBAR:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsViewHScrollBar();

        pOpt->SetViewHScrollBar( bFlag );
        break;

    case FN_RULER:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsViewAnyRuler();

        pOpt->SetViewAnyRuler( bFlag );
        break;
    case FN_VIEW_FIELDNAME:
    {
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsFieldName() ;
        const bool bDoAsk = officecfg::Office::Common::Misc::QueryShowFieldName::get();
        short nresult = RET_YES;
        if (bFlag && bDoAsk)
        {
            VclAbstractDialogFactory* pFact = VclAbstractDialogFactory::Create();
            auto pDlg = pFact->CreateQueryDialog(
                GetWrtShell().GetView().GetFrameWeld(), SwResId(STR_QUERY_FIELDNAME_TITLE),
                SwResId(STR_QUERY_FIELDNAME_TEXT), SwResId(STR_QUERY_FIELDNAME_QUESTION), true);
            nresult = pDlg->Execute();
            if (pDlg->ShowAgain() == false)
            {
                std::shared_ptr<comphelper::ConfigurationChanges> xChanges(
                    comphelper::ConfigurationChanges::create());
                officecfg::Office::Common::Misc::QueryShowFieldName::set(false, xChanges);
                xChanges->commit();
            }
            pDlg->disposeOnce();
        }
        if (nresult == RET_YES)
            pOpt->SetFieldName(bFlag);
        break;
    }
    case FN_VIEW_MARKS:
        if( STATE_TOGGLE == eState )
            bFlag = !lcl_IsViewMarks(*pOpt) ;

        lcl_SetViewMarks( *pOpt, bFlag );
        break;

    case FN_HIGHLIGHT_CHAR_DF:
        if (STATE_TOGGLE == eState)
            bFlag = !m_bIsHighlightCharDF;
        m_bIsHighlightCharDF = bFlag;
        break;

    case SID_SPOTLIGHT_PARASTYLES:
    {
        if (STATE_TOGGLE == eState)
            bFlag = !m_bIsSpotlightParaStyles;
        m_bIsSpotlightParaStyles = bFlag;

        if (!comphelper::LibreOfficeKit::isActive() && m_bIsSpotlightParaStyles)
        {
            if (!pArgs || !pArgs->HasItem(FN_PARAM_1))
            {
                // If the sidebar isn't open, open it to the styles deck.
                sfx2::sidebar::SidebarController* pController
                    = sfx2::sidebar::SidebarController::GetSidebarControllerForFrame(
                        GetViewFrame().GetFrame().GetFrameInterface());
                if (!pController)
                {
                    const SfxStringItem sDeckName(SID_SIDEBAR_DECK, u"StyleListDeck"_ustr);
                    GetDispatcher().ExecuteList(SID_SIDEBAR_DECK, SfxCallMode::SYNCHRON,
                                                { &sDeckName });
                }
                else
                {
                    // assure the styles panel is filled
                    pController->CreateDeck(u"StyleListDeck");
                }
            }
        }
    }
    break;

    case SID_SPOTLIGHT_CHARSTYLES:
    {
        if (STATE_TOGGLE == eState)
            bFlag = !m_bIsSpotlightCharStyles;
        m_bIsSpotlightCharStyles = bFlag;

        if (!comphelper::LibreOfficeKit::isActive() && m_bIsSpotlightCharStyles)
        {
            if (!pArgs || !pArgs->HasItem(FN_PARAM_1))
            {
                // If the sidebar isn't open, open it to the styles deck.
                sfx2::sidebar::SidebarController* pController
                    = sfx2::sidebar::SidebarController::GetSidebarControllerForFrame(
                        GetViewFrame().GetFrame().GetFrameInterface());
                if (!pController)
                {
                    const SfxStringItem sDeckName(SID_SIDEBAR_DECK, u"StyleListDeck"_ustr);
                    GetDispatcher().ExecuteList(SID_SIDEBAR_DECK, SfxCallMode::SYNCHRON,
                                                { &sDeckName });
                }
                else
                {
                    // assure the styles panel is filled
                    pController->CreateDeck(u"StyleListDeck");
                }
            }
        }
    }
    break;

    case FN_VIEW_META_CHARS:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsViewMetaChars();

        lcl_SetViewMetaChars( *pOpt, bFlag );
        break;

    case SID_AUTOSPELL_CHECK:
        const SfxPoolItem* pItem;

        if (pArgs && pArgs->HasItem(FN_PARAM_1, &pItem))
            bSet = static_cast<const SfxBoolItem*>(pItem)->GetValue();
        else if( STATE_TOGGLE == eState )
        {
            bFlag = !pOpt->IsOnlineSpell();
            bSet = bFlag;
        }

        pOpt->SetOnlineSpell(bSet);
        {
            SvtLinguConfig  aCfg;
            aCfg.SetProperty( UPN_IS_SPELL_AUTO, uno::Any( bSet ) );

            if (xLngProp.is())
                xLngProp->setIsSpellAuto( bSet );

            // for the time being we do not have a specific option for grammarchecking.
            // thus we'll use the one for spell checking...
            if (bSet)
            {
                SwDocShell *pDocSh = GetDocShell();
                SwDoc *pDoc = pDocSh? pDocSh->GetDoc() : nullptr;

                // right now we don't have view options for automatic grammar checking. Thus...
                bool bIsAutoGrammar = false;
                aCfg.GetProperty( UPN_IS_GRAMMAR_AUTO ) >>= bIsAutoGrammar;

                if (pDoc && bIsAutoGrammar)
                    pDoc->StartGrammarChecking();
            }
        }
        break;

    case SID_ACCESSIBILITY_CHECK_ONLINE:
    {
        if (pArgs && pArgs->HasItem(FN_PARAM_1, &pItem))
        {
            bSet = static_cast<const SfxBoolItem*>(pItem)->GetValue();
        }
        else if (STATE_TOGGLE == eState)
        {
            bool bOnlineCheck = officecfg::Office::Common::Accessibility::OnlineAccessibilityCheck::get();
            bSet = !bOnlineCheck;
        }
        std::shared_ptr<comphelper::ConfigurationChanges> batch(comphelper::ConfigurationChanges::create());
        officecfg::Office::Common::Accessibility::OnlineAccessibilityCheck::set(bSet, batch);
        batch->commit();

        SwDocShell *pDocSh = GetDocShell();
        SwDoc* pDocument = pDocSh? pDocSh->GetDoc() : nullptr;
        if (pDocument)
            pDocument->getOnlineAccessibilityCheck()->updateCheckerActivity();
    }
    break;

    case FN_SHADOWCURSOR:
        if( STATE_TOGGLE == eState )
        {
            bFlag = !pOpt->IsShadowCursor();
            bSet = bFlag;
        }

        pOpt->SetShadowCursor(bSet);
        break;

    case FN_SHOW_INLINETOOLTIPS:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsShowInlineTooltips();

        pOpt->SetShowInlineTooltips( bFlag );
        break;

    case FN_USE_HEADERFOOTERMENU:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsUseHeaderFooterMenu();

        pOpt->SetUseHeaderFooterMenu( bFlag );
        break;

    case FN_SHOW_OUTLINECONTENTVISIBILITYBUTTON:
    {
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsShowOutlineContentVisibilityButton();

        SwWrtShell &rSh = GetWrtShell();

        if (!bFlag) // Outline folding is being turned ON!
            rSh.MakeAllFoldedOutlineContentVisible();

        pOpt->SetShowOutlineContentVisibilityButton(bFlag);

        // Apply option change here so if toggling the outline folding feature ON
        // the invalidate function will see this.
        rSh.StartAction();
        rSh.ApplyViewOptions(*pOpt);
        rSh.EndAction();

        if (bFlag) // Outline folding is being turned OFF!
            rSh.MakeAllFoldedOutlineContentVisible(false);

        break;
    }

    case FN_SHOW_CHANGES_IN_MARGIN:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsShowChangesInMargin();

        pOpt->SetShowChangesInMargin( bFlag );
        break;

    case FN_SINGLE_PAGE_PER_ROW:
        pOpt->SetViewLayoutBookMode( false );
        pOpt->SetViewLayoutColumns( 1 );
        break;

    case FN_MULTIPLE_PAGES_PER_ROW:
        pOpt->SetViewLayoutBookMode( false );
        pOpt->SetViewLayoutColumns( 0 );
        break;

    case FN_BOOKVIEW:
        pOpt->SetViewLayoutColumns( 2 );
        pOpt->SetViewLayoutBookMode( true );
        break;
    case SID_CLICK_CHANGE_ROTATION:
        if( STATE_TOGGLE == eState )
            bFlag = !pOpt->IsClickChangeRotation();
        pOpt->SetClickChangeRotation(bFlag);
    break;

    default:
        OSL_FAIL("wrong request method");
        return;
    }

    // Set UserPrefs, mark request as modified
    bool bWebView =  dynamic_cast<const SwWebView*>(this) !=  nullptr;
    SwWrtShell &rSh = GetWrtShell();
    rSh.StartAction();
    SwModule* pModule = SwModule::get();
    if( *rSh.GetViewOptions() != *pOpt )
    {
        rSh.ApplyViewOptions( *pOpt );
        if( bBrowseModeChanged )
        {
            GetDocShell()->ToggleLayoutMode(this);
        }

        // The UsrPref must be marked as modified.
        // call for initialization
        pModule->GetUsrPref(bWebView);
        SwModule::CheckSpellChanges( pOpt->IsOnlineSpell(), false, false, false );
    }
    //OS: Set back modified again, because view/fields sets the Doc modified.
    if( !bModified )
        rSh.ResetModified();

    pModule->ApplyUsrPref( *pOpt, this, bWebView ? SvViewOpt::DestWeb : SvViewOpt::DestText );

    if (nSlot == SID_SPOTLIGHT_CHARSTYLES || nSlot == SID_SPOTLIGHT_PARASTYLES)
    {
        SwXTextDocument* pModel = comphelper::getFromUnoTunnel<SwXTextDocument>(GetCurrentDocument());
        SfxLokHelper::notifyViewRenderState(this, pModel);
        if (vcl::Window *pMyWin = rSh.GetWin())
            pMyWin->Invalidate();
    }

    // #i6193# let postits know about new spellcheck setting
    if ( nSlot == SID_AUTOSPELL_CHECK )
        GetPostItMgr()->SetSpellChecking();

    const bool bLockedView = rSh.IsViewLocked();
    rSh.LockView( true );    //lock visible section
    GetWrtShell().EndAction();
    if( bBrowseModeChanged && !bFlag )
        CalcVisArea( GetEditWin().GetOutputSizePixel() );
    rSh.LockView( bLockedView );

    pOpt.reset();
    Invalidate(rReq.GetSlot());
    if(!pArgs)
        rReq.AppendItem(SfxBoolItem(nSlot, bFlag));
    rReq.Done();
}

void SwView::ExecFormatFootnote()
{
    SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
    VclPtr<VclAbstractDialog> pDlg(pFact->CreateSwFootNoteOptionDlg(GetFrameWeld(), GetWrtShell()));
    pDlg->StartExecuteAsync(
        [pDlg] (sal_Int32 /*nResult*/)->void
        {
            pDlg->disposeOnce();
        }
    );
}

void SwView::ExecNumberingOutline(SfxItemPool & rPool)
{
    SfxItemSet aTmp(SfxItemSet::makeFixedSfxItemSet<FN_PARAM_1, FN_PARAM_1>(rPool));
    SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
    VclPtr<SfxAbstractTabDialog> pDlg(pFact->CreateOutlineTabDialog(GetFrameWeld(), &aTmp, GetWrtShell()));
    pDlg->StartExecuteAsync(
        [pDlg] (sal_Int32 /*nResult*/)->void
        {
            pDlg->disposeOnce();
        }
    );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
