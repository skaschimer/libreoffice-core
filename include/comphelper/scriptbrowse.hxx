/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sal/config.h>

#include <comphelper/comphelperdllapi.h>

#include <com/sun/star/uno/Reference.hxx>

namespace com::sun::star::script::browse
{
class XBrowseNode;
}

namespace comphelper::scriptbrowse
{
COMPHELPER_DLLPUBLIC bool
isEditable(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode);
COMPHELPER_DLLPUBLIC bool
editNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode);

COMPHELPER_DLLPUBLIC bool
isDeletable(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode);
COMPHELPER_DLLPUBLIC bool
deleteNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode);

COMPHELPER_DLLPUBLIC bool
isCreatable(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode);
COMPHELPER_DLLPUBLIC css::uno::Reference<css::script::browse::XBrowseNode>
createNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
           const OUString& sName);

COMPHELPER_DLLPUBLIC bool
isRenamable(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode);
COMPHELPER_DLLPUBLIC css::uno::Reference<css::script::browse::XBrowseNode>
renameNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
           const OUString& sName);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
