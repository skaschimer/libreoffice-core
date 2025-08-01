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

#include <vector>
#include <vcl/window.hxx>
#include <com/sun/star/uno/Reference.h>
#include "TableConnectionData.hxx"

class Point;
namespace tools { class Rectangle; }

namespace dbaui
{
    class OTableWindow;
    class OJoinTableView;
    class OConnectionLine;

    class OTableConnection : public vcl::Window
    {
        std::vector<std::unique_ptr<OConnectionLine>> m_vConnLine;
        TTableConnectionData::value_type
                                        m_pData;
        VclPtr<OJoinTableView>          m_pParent;

        bool                            m_bSelected;

        void Init();
        /** loops through the vector and deletes all lines */
        void clearLineData();

    public:
        OTableConnection( OJoinTableView* pContainer, TTableConnectionData::value_type aTabConnData );
        OTableConnection( const OTableConnection& rConn );
        /** destructor

            @attention Normally a pointer to OTableConnectionData is given but
                       here, however, one has to create an instance (with
                       OTableConnectionDate::NewInstance) which is never deleted
                       (same like in other cases). Thus, the caller is
                       responsible to check and save the data for deleting it
                       eventually.
         */
        virtual ~OTableConnection() override;
        virtual void dispose() override;

        OTableConnection& operator=( const OTableConnection& rConn );

        void        Select();
        void        Deselect();
        bool        IsSelected() const { return m_bSelected; }
        bool        CheckHit( const Point& rMousePos ) const;
        void        InvalidateConnection();
        void        UpdateLineList();

        OTableWindow* GetSourceWin() const;
        OTableWindow* GetDestWin() const;

        void RecalcLines();
        /** isTableConnection

            @param  _pTable the table where we should check if we belong to it
            @return true when the source or the destination window are equal
        */
        bool isTableConnection(const OTableWindow* _pTable)
        {
            return (_pTable == GetSourceWin() || _pTable == GetDestWin());
        }

        tools::Rectangle   GetBoundingRect() const;

        const TTableConnectionData::value_type& GetData() const { return m_pData; }
        const std::vector<std::unique_ptr<OConnectionLine>>&  GetConnLineList() const { return m_vConnLine; }
        OJoinTableView*                  GetParent() const { return m_pParent; }
        virtual void Draw(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect);
        using Window::Draw;
        virtual rtl::Reference<comphelper::OAccessible> CreateAccessible() override;
    };
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
