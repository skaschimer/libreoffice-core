/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>
#include <com/sun/star/script/browse/XBrowseNode.hpp>
#include <com/sun/star/script/browse/XEditableBrowseNode.hpp>
#include <cppuhelper/implbase.hxx>
#include <memory>

namespace singleprovider
{
class ProviderContext;

class ScriptMacro
    : public cppu::WeakImplHelper<css::script::browse::XBrowseNode,
                                  css::script::browse::XEditableBrowseNode,
                                  css::beans::XPropertySet, css::beans::XPropertySetInfo>
{
public:
    ScriptMacro(const std::shared_ptr<ProviderContext>& pProviderContext, const OUString& sName,
                const OUString& sUri);

    // XBrowseNode
    OUString SAL_CALL getName() override;
    css::uno::Sequence<css::uno::Reference<css::script::browse::XBrowseNode>>
        SAL_CALL getChildNodes() override;
    sal_Bool SAL_CALL hasChildNodes() override;
    sal_Int16 SAL_CALL getType() override;

    // XEditableBrowseNode
    sal_Bool SAL_CALL isEditableNode() override;
    sal_Bool SAL_CALL editNode() override;

    // XPropertySet
    css::uno::Reference<css::beans::XPropertySetInfo> SAL_CALL getPropertySetInfo() override;
    void SAL_CALL setPropertyValue(const OUString& aPropertyName,
                                   const css::uno::Any& aValue) override;
    css::uno::Any SAL_CALL getPropertyValue(const OUString& sPropertyName) override;
    void SAL_CALL addPropertyChangeListener(
        const OUString& aPropertyName,
        const css::uno::Reference<css::beans::XPropertyChangeListener>& xListener) override;
    void SAL_CALL removePropertyChangeListener(
        const OUString& aPropertyName,
        const css::uno::Reference<css::beans::XPropertyChangeListener>& aListener) override;
    void SAL_CALL addVetoableChangeListener(
        const OUString& PropertyName,
        const css::uno::Reference<css::beans::XVetoableChangeListener>& aListener) override;
    void SAL_CALL removeVetoableChangeListener(
        const OUString& PropertyName,
        const css::uno::Reference<css::beans::XVetoableChangeListener>& aListener) override;

    // XPropertySetInfo
    css::uno::Sequence<css::beans::Property> SAL_CALL getProperties() override;
    css::beans::Property SAL_CALL getPropertyByName(const OUString& sName) override;
    sal_Bool SAL_CALL hasPropertyByName(const OUString& sName) override;

private:
    static css::beans::Property getUriProperty();

    std::shared_ptr<ProviderContext> m_pProviderContext;

    OUString m_sName;
    OUString m_sBaseUri;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
