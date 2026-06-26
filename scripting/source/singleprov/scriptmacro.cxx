/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "scriptmacro.hxx"

#include <com/sun/star/script/browse/BrowseNodeTypes.hpp>
#include <com/sun/star/script/provider/XScriptURIHelper.hpp>
#include <singleprov/singlescriptfactory.hxx>

#include "externaledit.hxx"
#include "provcontext.hxx"

namespace singleprovider
{
ScriptMacro::ScriptMacro(const std::shared_ptr<ProviderContext>& pProviderContext,
                         const OUString& sName, const OUString& sBaseUri)
    : m_pProviderContext(pProviderContext)
    , m_sName(sName)
    , m_sBaseUri(sBaseUri)
{
}

OUString SAL_CALL ScriptMacro::getName() { return m_sName; }

css::uno::Sequence<css::uno::Reference<css::script::browse::XBrowseNode>>
    SAL_CALL ScriptMacro::getChildNodes()
{
    return css::uno::Sequence<css::uno::Reference<css::script::browse::XBrowseNode>>();
}

sal_Bool SAL_CALL ScriptMacro::hasChildNodes() { return false; }

sal_Int16 SAL_CALL ScriptMacro::getType() { return css::script::browse::BrowseNodeTypes::SCRIPT; }

sal_Bool SAL_CALL ScriptMacro::isEditableNode()
{
    return isEditable(m_pProviderContext, m_sBaseUri);
}

sal_Bool SAL_CALL ScriptMacro::editNode()
{
    externalEdit(m_pProviderContext, m_sBaseUri);
    return true;
}

css::uno::Reference<css::beans::XPropertySetInfo> SAL_CALL ScriptMacro::getPropertySetInfo()
{
    return this;
}

void SAL_CALL ScriptMacro::setPropertyValue(const OUString&, const css::uno::Any&) {}

css::uno::Any SAL_CALL ScriptMacro::getPropertyValue(const OUString& sPropertyName)
{
    css::uno::Any xRet;

    if (sPropertyName == "URI")
        xRet <<= m_pProviderContext->m_xUriHelper->getScriptURI(m_sBaseUri);
    else
    {
        throw css::beans::UnknownPropertyException("Tried to get unknown property "
                                                   + sPropertyName);
    }

    return xRet;
}

void SAL_CALL ScriptMacro::addPropertyChangeListener(
    const OUString&, const css::uno::Reference<css::beans::XPropertyChangeListener>&)
{
}

void SAL_CALL ScriptMacro::removePropertyChangeListener(
    const OUString&, const css::uno::Reference<css::beans::XPropertyChangeListener>&)
{
}

void SAL_CALL ScriptMacro::addVetoableChangeListener(
    const OUString&, const css::uno::Reference<css::beans::XVetoableChangeListener>&)
{
}

void SAL_CALL ScriptMacro::removeVetoableChangeListener(
    const OUString&, const css::uno::Reference<css::beans::XVetoableChangeListener>&)
{
}

css::uno::Sequence<css::beans::Property> SAL_CALL ScriptMacro::getProperties()
{
    css::uno::Sequence<css::beans::Property> aProperties(1);
    aProperties.getArray()[0] = getUriProperty();
    return aProperties;
}

css::beans::Property SAL_CALL ScriptMacro::getPropertyByName(const OUString& sName)
{
    if (sName == "URI")
        return getUriProperty();
    else
        throw css::beans::UnknownPropertyException("Tried to retrieve unknown property " + sName);
}

sal_Bool SAL_CALL ScriptMacro::hasPropertyByName(const OUString& sName) { return sName == "URI"; }

css::beans::Property ScriptMacro::getUriProperty()
{
    return css::beans::Property("URI", 0, cppu::UnoType<OUString>::get(), 0);
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
