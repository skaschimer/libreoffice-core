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

#include <sfx2/tabdlg.hxx>
#include <vcl/weld/Button.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/Label.hxx>
#include <vcl/weld/TextView.hxx>
#include <vcl/weld/TreeView.hxx>

class ScUserList;
class ScDocument;
class ScViewData;
class ScRefAddress;

class ScTpUserLists : public SfxTabPage
{
public:
    ScTpUserLists(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& rArgSet);
    static std::unique_ptr<SfxTabPage> Create(weld::Container* pPage, weld::DialogController* pController,
                                          const SfxItemSet* rAttrSet);
    virtual ~ScTpUserLists() override;

    virtual OUString GetAllStrings() override;

    virtual bool FillItemSet(SfxItemSet* pCoreAttrs) override;
    virtual void Reset(const SfxItemSet* pCoreAttrs) override;
    virtual DeactivateRC   DeactivatePage  ( SfxItemSet* pSet ) override;

private:
    std::unique_ptr<weld::Label> mxFtLists;
    std::unique_ptr<weld::TreeView> mxLbLists;
    std::unique_ptr<weld::Label> mxFtEntries;
    std::unique_ptr<weld::TextView> mxEdEntries;
    std::unique_ptr<weld::Label> mxFtCopyFrom;
    std::unique_ptr<weld::Entry> mxEdCopyFrom;
    std::unique_ptr<weld::Button> mxBtnNew;
    std::unique_ptr<weld::Button> mxBtnDiscard;
    std::unique_ptr<weld::Button> mxBtnAdd;
    std::unique_ptr<weld::Button> mxBtnModify;
    std::unique_ptr<weld::Button> mxBtnRemove;
    std::unique_ptr<weld::Button> mxBtnCopy;

    const OUString m_aStrQueryRemove;
    const OUString m_aStrCopyFrom;
    const OUString m_aStrCopyErr;

    const sal_uInt16 m_nWhichUserLists;
    std::unique_ptr<ScUserList> m_pUserLists;

    ScDocument* m_pDoc;
    ScViewData* m_pViewData;
    OUString m_aStrSelectedArea;

    bool m_bModifyMode;
    bool m_bCancelMode;
    bool m_bCopyDone;
    sal_Int32 m_nCancelPos;

    size_t  UpdateUserListBox   ();
    void    UpdateEntries       ( size_t nList );
    static OUString MakeListStr(std::u16string_view sListStr);
    void AddNewList(std::u16string_view sEntriesStr);
    void    RemoveList          ( size_t nList );
    void ModifyList(size_t nSelList, std::u16string_view sEntriesStr);
    void    CopyListFromArea    ( const ScRefAddress& rStartPos,
                                  const ScRefAddress& rEndPos );

    // Handler:
    DECL_LINK(LbSelectHdl, weld::ItemView&, void);
    DECL_LINK( BtnClickHdl, weld::Button&, void );
    DECL_LINK(EdEntriesModHdl, weld::TextWidget&, void);
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
