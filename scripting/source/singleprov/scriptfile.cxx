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

#include <singleprov/directorynode.hxx>
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

sal_Bool SAL_CALL ScriptFile::isCopyableNode() { return true; }

std::optional<OUString> ScriptFile::getCopyDestinationUri(
    const css::uno::Reference<css::script::browse::XBrowseNode>& xDest) const
{
    const DirectoryNode* pDirectoryNode = dynamic_cast<const DirectoryNode*>(xDest.get());

    if (!pDirectoryNode)
        return std::nullopt;

    // Don’t allow copying into providers for other languages
    if (pDirectoryNode->getScriptFactory()->getLanguageName()
        != m_pProviderContext->m_pSingleScriptFactory->getLanguageName())
    {
        return std::nullopt;
    }

    std::optional<OUString> sDirectoryUri = pDirectoryNode->getDirectoryUri();

    // Don’t allow copying into the same directory
    if (sDirectoryUri)
    {
        sal_Int32 nDirEnd = m_sBaseUri.lastIndexOf(u'/');

        if (sDirectoryUri.value() == (nDirEnd == -1 ? m_sBaseUri : m_sBaseUri.copy(0, nDirEnd)))
            return std::nullopt;
    }

    return sDirectoryUri;
}

sal_Bool SAL_CALL ScriptFile::nodeCanBeCopiedTo(
    const css::uno::Reference<css::script::browse::XBrowseNode>& xParentNode)
{
    return getCopyDestinationUri(xParentNode).has_value();
}

css::uno::Reference<css::script::browse::XBrowseNode> SAL_CALL
ScriptFile::copyNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xParentNode)
{
    std::optional<OUString> sDirectoryUri = getCopyDestinationUri(xParentNode);

    if (!sDirectoryUri)
    {
        throw css::lang::IllegalArgumentException(u"Invalid parent node passed to copyNode"_ustr,
                                                  getXWeak(), 0);
    }

    const css::uno::Reference<css::ucb::XSimpleFileAccess3>& xFileAccess
        = m_pProviderContext->m_xFileAccess;

    xFileAccess->createFolder(sDirectoryUri.value());

    std::u16string_view sBaseName = m_sBaseUri.subView(m_sBaseUri.lastIndexOf(u'/') + 1);
    OUString sTargetUri = sDirectoryUri.value() + OUStringChar('/') + sBaseName;

    xFileAccess->copy(m_sBaseUri, sTargetUri);

    return new ScriptFile(m_pProviderContext, m_sName, sTargetUri);
}

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
