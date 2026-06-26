/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "scriptfile.hxx"

#include <com/sun/star/script/browse/BrowseNodeTypes.hpp>
#include <com/sun/star/ucb/SimpleFileAccess.hpp>

#include <singleprov/singlescriptfactory.hxx>

#include "externaledit.hxx"
#include "provcontext.hxx"
#include "scriptmacro.hxx"

namespace singleprovider
{
ScriptFile::ScriptFile(const std::shared_ptr<ProviderContext>& pProviderContext,
                       const OUString& sName, const OUString& sBaseUri)
    : m_pProviderContext(pProviderContext)
    , m_sName(sName)
    , m_sBaseUri(sBaseUri)
{
}

OUString SAL_CALL ScriptFile::getName() { return m_sName; }

css::uno::Sequence<css::uno::Reference<css::script::browse::XBrowseNode>>
    SAL_CALL ScriptFile::getChildNodes()
{
    // Each file is treated as a container with a single macro inside it
    css::uno::Sequence<css::uno::Reference<css::script::browse::XBrowseNode>> aChild(1);

    aChild.getArray()[0].set(new ScriptMacro(m_pProviderContext, m_sName, m_sBaseUri));

    return aChild;
}

sal_Bool SAL_CALL ScriptFile::hasChildNodes() { return true; }

sal_Int16 SAL_CALL ScriptFile::getType() { return css::script::browse::BrowseNodeTypes::CONTAINER; }

sal_Bool SAL_CALL ScriptFile::isEditableNode()
{
    return isEditable(m_pProviderContext, m_sBaseUri);
}

sal_Bool SAL_CALL ScriptFile::editNode()
{
    externalEdit(m_pProviderContext, m_sBaseUri);
    return true;
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
