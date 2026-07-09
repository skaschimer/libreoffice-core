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

#undef SC_DLLIMPLEMENTATION

#include <comphelper/string.hxx>
#include <officecfg/Office/Calc.hxx>
#include <tools/lineend.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld/MessageDialog.hxx>
#include <osl/diagnose.h>
#include <o3tl/string_view.hxx>

#include <document.hxx>
#include <tabvwsh.hxx>
#include <viewdata.hxx>
#include <uiitems.hxx>
#include <userlist.hxx>
#include <rangeutl.hxx>
#include <crdlg.hxx>
#include <sc.hrc>
#include <globstr.hrc>
#include <scresid.hxx>
#include <tpusrlst.hxx>
#include <scui_def.hxx>

#define CR  u'\x000D'
#define LF  u'\x000A'

const sal_Unicode cDelimiter = ',';

// User-defined lists

ScTpUserLists::ScTpUserLists( weld::Container* pPage, weld::DialogController* pController,
                              const SfxItemSet&     rCoreAttrs )
    : SfxTabPage(pPage, pController, u"modules/scalc/ui/optsortlists.ui"_ustr, u"OptSortLists"_ustr,
                          &rCoreAttrs )
    , mxFtLists(m_xBuilder->weld_label(u"listslabel"_ustr))
    , mxLbLists(m_xBuilder->weld_tree_view(u"lists"_ustr))
    , mxFtEntries(m_xBuilder->weld_label(u"entrieslabel"_ustr))
    , mxEdEntries(m_xBuilder->weld_text_view(u"entries"_ustr))
    , mxFtCopyFrom(m_xBuilder->weld_label(u"copyfromlabel"_ustr))
    , mxEdCopyFrom(m_xBuilder->weld_entry(u"copyfrom"_ustr))
    , mxBtnNew(m_xBuilder->weld_button(u"new"_ustr))
    , mxBtnDiscard(m_xBuilder->weld_button(u"discard"_ustr))
    , mxBtnAdd(m_xBuilder->weld_button(u"add"_ustr))
    , mxBtnModify(m_xBuilder->weld_button(u"modify"_ustr))
    , mxBtnRemove(m_xBuilder->weld_button(u"delete"_ustr))
    , mxBtnCopy(m_xBuilder->weld_button(u"copy"_ustr))
    , m_aStrQueryRemove(ScResId(STR_QUERYREMOVE))
    , m_aStrCopyFrom(ScResId(STR_COPYFROM))
    , m_aStrCopyErr(ScResId(STR_COPYERR))
    , m_nWhichUserLists(GetWhich(SID_SCUSERLISTS))
    , m_pDoc(nullptr)
    , m_pViewData(nullptr)
    , m_bModifyMode(false)
    , m_bCancelMode(false)
    , m_bCopyDone(false)
    , m_nCancelPos(0)
{
    SetExchangeSupport();

    SfxViewShell*   pSh = SfxViewShell::Current();
    ScTabViewShell* pViewSh = dynamic_cast<ScTabViewShell*>( pSh );

    mxLbLists->connect_selection_changed(LINK(this, ScTpUserLists, LbSelectHdl));
    mxBtnNew->connect_clicked     ( LINK( this, ScTpUserLists, BtnClickHdl ) );
    mxBtnDiscard->connect_clicked ( LINK( this, ScTpUserLists, BtnClickHdl ) );
    mxBtnAdd->connect_clicked     ( LINK( this, ScTpUserLists, BtnClickHdl ) );
    mxBtnModify->connect_clicked  ( LINK( this, ScTpUserLists, BtnClickHdl ) );
    mxBtnRemove->connect_clicked  ( LINK( this, ScTpUserLists, BtnClickHdl ) );
    mxEdEntries->connect_changed ( LINK( this, ScTpUserLists, EdEntriesModHdl ) );

    if ( pViewSh )
    {
        SCTAB   nStartTab   = 0;
        SCTAB   nEndTab     = 0;
        SCCOL   nStartCol   = 0;
        SCROW   nStartRow   = 0;
        SCCOL   nEndCol     = 0;
        SCROW   nEndRow     = 0;

        m_pViewData = &pViewSh->GetViewData();
        m_pDoc = &m_pViewData->GetDocument();

        m_pViewData->GetSimpleArea(nStartCol, nStartRow, nStartTab, nEndCol, nEndRow, nEndTab);

        PutInOrder( nStartCol, nEndCol );
        PutInOrder( nStartRow, nEndRow );
        PutInOrder( nStartTab, nEndTab );

        m_aStrSelectedArea = ScRange(nStartCol, nStartRow, nStartTab, nEndCol, nEndRow, nEndTab)
                                 .Format(*m_pDoc, ScRefFlags::RANGE_ABS_3D);

        mxBtnCopy->connect_clicked ( LINK( this, ScTpUserLists, BtnClickHdl ) );
        mxBtnCopy->set_sensitive(true);
    }
    else
    {
        mxBtnCopy->set_sensitive(false);
        mxFtCopyFrom->set_sensitive(false);
        mxEdCopyFrom->set_sensitive(false);
    }

    Reset(&rCoreAttrs);
}

ScTpUserLists::~ScTpUserLists()
{
}

std::unique_ptr<SfxTabPage> ScTpUserLists::Create( weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* rAttrSet )
{
    return std::make_unique<ScTpUserLists>(pPage, pController, *rAttrSet);
}

void ScTpUserLists::Reset(const SfxItemSet* pCoreAttrs)
{
    const ScUserListItem& rUserListItem
        = static_cast<const ScUserListItem&>(pCoreAttrs->Get(m_nWhichUserLists));
    const ScUserList*     pCoreList     = rUserListItem.GetUserList();

    OSL_ENSURE( pCoreList, "UserList not found :-/" );

    if ( pCoreList )
    {
        if (!m_pUserLists)
            m_pUserLists.reset(new ScUserList(*pCoreList));
        else
            *m_pUserLists = *pCoreList;

        if ( UpdateUserListBox() > 0 )
        {
            mxLbLists->select( 0 );
            UpdateEntries( 0 );
        }
    }
    else if (!m_pUserLists)
        m_pUserLists.reset(new ScUserList);

    mxEdCopyFrom->set_text(m_aStrSelectedArea);

    if ( mxLbLists->n_children() == 0 || officecfg::Office::Calc::SortList::List::isReadOnly() )
    {
        mxFtLists->set_sensitive(false);
        mxLbLists->set_sensitive(false);
        mxFtEntries->set_sensitive(false);
        mxEdEntries->set_sensitive(false);
        mxBtnRemove->set_sensitive(false);
    }

    mxBtnNew->show();
    mxBtnDiscard->hide();
    mxBtnAdd->show();
    mxBtnModify->hide();
    mxBtnNew->set_sensitive(!officecfg::Office::Calc::SortList::List::isReadOnly());
    mxBtnAdd->set_sensitive(false);
    mxBtnModify->set_sensitive(false);

    if (!m_bCopyDone && m_pViewData)
    {
        mxFtCopyFrom->set_sensitive(true);
        mxEdCopyFrom->set_sensitive(true);
        mxBtnCopy->set_sensitive(true);
    }
}

OUString ScTpUserLists::GetAllStrings()
{
    OUStringBuffer sAllStrings;
    OUString labels[] = { u"listslabel"_ustr, u"entrieslabel"_ustr, u"copyfromlabel"_ustr };

    for (const auto& label : labels)
    {
        if (const auto pString = m_xBuilder->weld_label(label))
            sAllStrings.append(pString->get_label() + " ");
    }

    OUString buttons[] = { u"new"_ustr, u"discard"_ustr, u"add"_ustr, u"modify"_ustr, u"delete"_ustr, u"copy"_ustr };

    for (const auto& btn : buttons)
    {
        if (const auto pString = m_xBuilder->weld_button(btn))
            sAllStrings.append(pString->get_label() + " ");
    }

    return sAllStrings.toString().replaceAll("_", "");
}

bool ScTpUserLists::FillItemSet(SfxItemSet* pCoreAttrs)
{
    // Changes aren't saved?
    // -> simulate click of Add-Button

    if (m_bModifyMode || m_bCancelMode)
        BtnClickHdl(*mxBtnAdd);

    const ScUserListItem& rUserListItem
        = static_cast<const ScUserListItem&>(GetItemSet().Get(m_nWhichUserLists));

    ScUserList* pCoreList       = rUserListItem.GetUserList();
    bool        bDataModified   = false;

    if ((m_pUserLists == nullptr) && (pCoreList == nullptr))
    {
        bDataModified = false;
    }
    else if (m_pUserLists != nullptr)
    {
        if ( pCoreList != nullptr )
            bDataModified = (*m_pUserLists != *pCoreList);
        else
            bDataModified = true;
    }

    if ( bDataModified )
    {
        ScUserListItem aULItem(m_nWhichUserLists);

        if (m_pUserLists)
            aULItem.SetUserList(*m_pUserLists);

        pCoreAttrs->Put(aULItem);
    }

    return bDataModified;
}

DeactivateRC ScTpUserLists::DeactivatePage( SfxItemSet* pSetP )
{
    if ( pSetP )
        FillItemSet( pSetP );

    return DeactivateRC::LeavePage;
}

size_t ScTpUserLists::UpdateUserListBox()
{
    mxLbLists->clear();

    if (!m_pUserLists)
        return 0;

    size_t nCount = m_pUserLists->size();
    for ( size_t i=0; i<nCount; ++i )
    {
        const OUString sEntry = (*m_pUserLists)[i].GetString();
        OSL_ENSURE(!sEntry.isEmpty(), "Empty UserList-entry :-/");
        mxLbLists->append_text(sEntry);
    }

    return nCount;
}

void ScTpUserLists::UpdateEntries( size_t nList )
{
    if (!m_pUserLists)
        return;

    if (nList < m_pUserLists->size())
    {
        const ScUserListData& rList = (*m_pUserLists)[nList];
        std::size_t nSubCount = rList.GetSubCount();
        OUStringBuffer aEntryListStr;

        for ( size_t i=0; i<nSubCount; i++ )
        {
            if ( i!=0 )
                aEntryListStr.append(CR);
            aEntryListStr.append(rList.GetSubStr(i));
        }

        mxEdEntries->set_text(convertLineEnd(aEntryListStr.makeStringAndClear(), GetSystemLineEnd()));
    }
    else
    {
        OSL_FAIL( "Invalid ListIndex :-/" );
    }
}

OUString ScTpUserLists::MakeListStr(std::u16string_view sListStr)
{
    if (sListStr.empty())
        return OUString();

    OUStringBuffer aStr;

    for(sal_Int32 nIdx=0; nIdx>=0;)
    {
        aStr.append(comphelper::string::strip(o3tl::getToken(sListStr, 0, LF, nIdx), ' '));
        aStr.append(cDelimiter);
    }

    aStr.strip(cDelimiter);
    sal_Int32 nLen = aStr.getLength();

    OUString sResult;

    // delete all duplicates of cDelimiter
    sal_Int32 c = 0;
    while ( c < nLen )
    {
        sResult += OUStringChar(aStr[c]);
        ++c;

        if ((c < nLen) && (aStr[c] == cDelimiter))
        {
            sResult += OUStringChar(aStr[c]);

            while ((c < nLen) && (aStr[c] == cDelimiter))
                ++c;
        }
    }

    return sResult;
}

void ScTpUserLists::AddNewList(std::u16string_view sEntriesStr)
{
    if (!m_pUserLists)
        m_pUserLists.reset(new ScUserList);

    m_pUserLists->emplace_back(MakeListStr(sEntriesStr));
}

void ScTpUserLists::CopyListFromArea( const ScRefAddress& rStartPos,
                                      const ScRefAddress& rEndPos )
{
    if (m_bCopyDone)
        return;

    SCTAB   nTab            = rStartPos.Tab();
    SCCOL   nStartCol       = rStartPos.Col();
    SCROW   nStartRow       = rStartPos.Row();
    SCCOL   nEndCol         = rEndPos.Col();
    SCROW   nEndRow         = rEndPos.Row();
    sal_uInt16  nCellDir        = SCRET_COLS;

    if ( (nStartCol != nEndCol) && (nStartRow != nEndRow) )
    {
        ScColOrRowDlg aDialog(GetFrameWeld(), ScResId(STR_COPYLIST), m_aStrCopyFrom);
        nCellDir = aDialog.run();
    }
    else if ( nStartCol != nEndCol )
        nCellDir = SCRET_ROWS;
    else
        nCellDir = SCRET_COLS;

    if ( nCellDir != RET_CANCEL )
    {
        bool bValueIgnored = false;

        if ( nCellDir == SCRET_COLS )
        {
            for ( SCCOL col=nStartCol; col<=nEndCol; col++ )
            {
                OUStringBuffer aStrList;
                for ( SCROW row=nStartRow; row<=nEndRow; row++ )
                {
                    if (m_pDoc->HasStringData(col, row, nTab))
                    {
                        OUString aStrField = m_pDoc->GetString(col, row, nTab);

                        if ( !aStrField.isEmpty() )
                        {
                            aStrList.append(aStrField + "\n");
                        }
                    }
                    else
                        bValueIgnored = true;
                }
                if ( !aStrList.isEmpty() )
                    AddNewList( aStrList.makeStringAndClear() );
            }
        }
        else
        {
            for ( SCROW row=nStartRow; row<=nEndRow; row++ )
            {
                OUStringBuffer aStrList;
                for ( SCCOL col=nStartCol; col<=nEndCol; col++ )
                {
                    if (m_pDoc->HasStringData(col, row, nTab))
                    {
                        OUString aStrField = m_pDoc->GetString(col, row, nTab);

                        if ( !aStrField.isEmpty() )
                        {
                            aStrList.append(aStrField + "\n");
                        }
                    }
                    else
                        bValueIgnored = true;
                }
                if ( !aStrList.isEmpty() )
                    AddNewList( aStrList.makeStringAndClear() );
            }
        }

        if ( bValueIgnored )
        {
            std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(
                GetFrameWeld(), VclMessageType::Info, VclButtonsType::Ok, m_aStrCopyErr));
            xInfoBox->run();
        }
    }

    m_bCopyDone = true;
}

void ScTpUserLists::ModifyList(size_t nSelList, std::u16string_view sEntriesStr)
{
    if (!m_pUserLists)
        return;

    (*m_pUserLists)[nSelList].SetString(MakeListStr(sEntriesStr));
}

void ScTpUserLists::RemoveList( size_t nList )
{
    if (m_pUserLists && nList < m_pUserLists->size())
        m_pUserLists->EraseData(nList);
}

// Handler:

IMPL_LINK(ScTpUserLists, LbSelectHdl, weld::ItemView&, rLb, void)
{
    if ( &rLb != mxLbLists.get() )
        return;

    sal_Int32 nSelPos = mxLbLists->get_selected_index();
    if ( nSelPos == -1 )
        return;

    if ( !mxFtEntries->get_sensitive() )  mxFtEntries->set_sensitive(true);
    if ( !mxEdEntries->get_sensitive() )  mxEdEntries->set_sensitive(true);
    if ( !mxBtnRemove->get_sensitive() )  mxBtnRemove->set_sensitive(true);
    if ( mxBtnAdd->get_sensitive() )
    {
        mxBtnAdd->set_sensitive(false);
        mxBtnModify->set_sensitive(false);
    }

    UpdateEntries( nSelPos );
}

IMPL_LINK( ScTpUserLists, BtnClickHdl, weld::Button&, rBtn, void )
{
    if (&rBtn == mxBtnNew.get() || &rBtn == mxBtnDiscard.get())
    {
        if (!m_bCancelMode)
        {
            m_nCancelPos = (mxLbLists->n_children() > 0) ? mxLbLists->get_selected_index() : 0;
            mxLbLists->unselect_all();
            mxFtLists->set_sensitive(false);
            mxLbLists->set_sensitive(false);
            mxFtEntries->set_sensitive(true);
            mxEdEntries->set_sensitive(true);
            mxEdEntries->set_text( OUString() );
            mxEdEntries->grab_focus();
            mxBtnAdd->set_sensitive(false);
            mxBtnModify->set_sensitive(false);
            mxBtnRemove->set_sensitive(false);

            if ( mxBtnCopy->get_sensitive() )
            {
                mxBtnCopy->set_sensitive(false);
                mxFtCopyFrom->set_sensitive(false);
                mxEdCopyFrom->set_sensitive(false);
            }
            mxBtnNew->hide();
            mxBtnDiscard->show();
            m_bCancelMode = true;
        }
        else // if ( bCancelMode )
        {
            if ( mxLbLists->n_children() > 0 )
            {
                mxLbLists->select(m_nCancelPos);
                LbSelectHdl( *mxLbLists );
                mxFtLists->set_sensitive(true);
                mxLbLists->set_sensitive(true);
            }
            else
            {
                mxFtEntries->set_sensitive(false);
                mxEdEntries->set_sensitive(false);
                mxEdEntries->set_text( OUString() );
                mxBtnRemove->set_sensitive(false);
            }
            mxBtnAdd->set_sensitive(false);
            mxBtnModify->set_sensitive(false);

            if (m_pViewData && !m_bCopyDone)
            {
                mxBtnCopy->set_sensitive(true);
                mxFtCopyFrom->set_sensitive(true);
                mxEdCopyFrom->set_sensitive(true);
            }
            mxBtnNew->show();
            mxBtnDiscard->hide();
            m_bCancelMode = false;
            m_bModifyMode = false;
        }
    }
    else if (&rBtn == mxBtnAdd.get() || &rBtn == mxBtnModify.get())
    {
        OUString theEntriesStr( mxEdEntries->get_text() );

        if (!m_bModifyMode)
        {
            if ( !theEntriesStr.isEmpty() )
            {
                AddNewList( theEntriesStr );
                UpdateUserListBox();
                mxLbLists->select( mxLbLists->n_children()-1 );
                LbSelectHdl( *mxLbLists );
                mxFtLists->set_sensitive(true);
                mxLbLists->set_sensitive(true);
            }
            else
            {
                if ( mxLbLists->n_children() > 0 )
                {
                    mxLbLists->select(m_nCancelPos);
                    LbSelectHdl( *mxLbLists );
                    mxLbLists->set_sensitive(true);
                    mxLbLists->set_sensitive(true);
                }
            }

            mxBtnAdd->set_sensitive(false);
            mxBtnModify->set_sensitive(false);
            mxBtnRemove->set_sensitive(true);
            mxBtnNew->show();
            mxBtnDiscard->hide();
            m_bCancelMode = false;
        }
        else // if ( bModifyMode )
        {
            sal_Int32 nSelList = mxLbLists->get_selected_index();

            assert(nSelList != -1 && "Modify without List :-/");

            if ( !theEntriesStr.isEmpty() )
            {
                ModifyList( nSelList, theEntriesStr );
                UpdateUserListBox();
                mxLbLists->select( nSelList );
            }
            else
            {
                mxLbLists->select( 0 );
                LbSelectHdl( *mxLbLists );
            }

            mxBtnNew->show();
            mxBtnDiscard->hide();
            m_bCancelMode = false;
            mxBtnAdd->show();
            mxBtnModify->show();
            mxBtnAdd->set_sensitive(false);
            mxBtnModify->set_sensitive(false);
            m_bModifyMode = false;
            mxBtnRemove->set_sensitive(true);
            mxFtLists->set_sensitive(true);
            mxLbLists->set_sensitive(true);
        }

        if (m_pViewData && !m_bCopyDone)
        {
            mxBtnCopy->set_sensitive(true);
            mxFtCopyFrom->set_sensitive(true);
            mxEdCopyFrom->set_sensitive(true);
        }
    }
    else if ( &rBtn == mxBtnRemove.get() )
    {
        if ( mxLbLists->n_children() > 0 )
        {
            sal_Int32 nRemovePos   = mxLbLists->get_selected_index();
            OUString aMsg = o3tl::getToken(m_aStrQueryRemove, 0, '#')
                            + mxLbLists->get_text(nRemovePos)
                            + o3tl::getToken(m_aStrQueryRemove, 1, '#');

            std::unique_ptr<weld::MessageDialog> xQueryBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                           VclMessageType::Question, VclButtonsType::YesNo,
                                                           aMsg));
            xQueryBox->set_default_response(RET_YES);

            if (RET_YES == xQueryBox->run())
            {
                RemoveList( nRemovePos );
                UpdateUserListBox();

                if ( mxLbLists->n_children() > 0 )
                {
                    mxLbLists->select(
                        ( nRemovePos >= mxLbLists->n_children() )
                            ? mxLbLists->n_children()-1
                            : nRemovePos );
                    LbSelectHdl( *mxLbLists );
                }
                else
                {
                    mxFtLists->set_sensitive(false);
                    mxLbLists->set_sensitive(false);
                    mxFtEntries->set_sensitive(false);
                    mxEdEntries->set_sensitive(false);
                    mxEdEntries->set_text( OUString() );
                    mxBtnRemove->set_sensitive(false);
                }
            }

            if (m_pViewData && !m_bCopyDone && !mxBtnCopy->get_sensitive())
            {
                mxBtnCopy->set_sensitive(true);
                mxFtCopyFrom->set_sensitive(true);
                mxEdCopyFrom->set_sensitive(true);
            }
        }
    }
    else if (m_pViewData && (&rBtn == mxBtnCopy.get()))
    {
        if (m_bCopyDone)
            return;

        ScRefAddress theStartPos;
        ScRefAddress theEndPos;
        OUString     theAreaStr( mxEdCopyFrom->get_text() );
        bool     bAreaOk = false;

        if ( !theAreaStr.isEmpty() )
        {
            bAreaOk = ScRangeUtil::IsAbsArea(theAreaStr, *m_pDoc, m_pViewData->CurrentTabForData(),
                                             &theAreaStr, &theStartPos, &theEndPos,
                                             m_pDoc->GetAddressConvention());
            if ( !bAreaOk )
            {
                bAreaOk = ScRangeUtil::IsAbsPos(theAreaStr, *m_pDoc,
                                                m_pViewData->CurrentTabForData(), &theAreaStr,
                                                &theStartPos, m_pDoc->GetAddressConvention());
                theEndPos = theStartPos;
            }
        }

        if ( bAreaOk )
        {
            CopyListFromArea( theStartPos, theEndPos );
            UpdateUserListBox();
            mxLbLists->select( mxLbLists->n_children()-1 );
            LbSelectHdl( *mxLbLists );
            mxEdCopyFrom->set_text( theAreaStr );
            mxEdCopyFrom->set_sensitive(false);
            mxBtnCopy->set_sensitive(false);
            mxFtCopyFrom->set_sensitive(false);
        }
        else
        {
            std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(GetFrameWeld(),
                        VclMessageType::Warning, VclButtonsType::Ok,
                        ScResId(STR_INVALID_TABREF)));

            xBox->run();
            mxEdCopyFrom->grab_focus();
            mxEdCopyFrom->select_region(0, -1);
        }
    }
}

IMPL_LINK_NOARG(ScTpUserLists, EdEntriesModHdl, weld::TextWidget&, void)
{
    if ( mxBtnCopy->get_sensitive() )
    {
        mxBtnCopy->set_sensitive(false);
        mxFtCopyFrom->set_sensitive(false);
        mxEdCopyFrom->set_sensitive(false);
    }

    if ( !mxEdEntries->get_text().isEmpty() )
    {
        if (!m_bCancelMode && !m_bModifyMode)
        {
            mxBtnNew->hide();
            mxBtnDiscard->show();
            m_bCancelMode = true;
            mxBtnAdd->hide();
            mxBtnAdd->set_sensitive(true);
            mxBtnModify->show();
            mxBtnModify->set_sensitive(true);
            m_bModifyMode = true;
            mxBtnRemove->set_sensitive(false);
            mxFtLists->set_sensitive(false);
            mxLbLists->set_sensitive(false);
        }
        else // if ( bCancelMode || bModifyMode )
        {
            if ( !mxBtnAdd->get_sensitive() )
            {
                mxBtnAdd->set_sensitive(true);
                mxBtnModify->set_sensitive(true);
            }
        }
    }
    else
    {
        if ( mxBtnAdd->get_sensitive() )
        {
            mxBtnAdd->set_sensitive(false);
            mxBtnModify->set_sensitive(false);
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
