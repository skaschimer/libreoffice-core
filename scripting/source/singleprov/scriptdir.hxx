/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <com/sun/star/script/browse/XBrowseNode.hpp>
#include <com/sun/star/script/browse/XCreatableBrowseNode.hpp>
#include <cppuhelper/implbase.hxx>
#include <memory>

namespace singleprovider
{
class ProviderContext;

class ScriptDir : public cppu::WeakImplHelper<css::script::browse::XBrowseNode,
                                              css::script::browse::XCreatableBrowseNode>
{
public:
    ScriptDir(const std::shared_ptr<ProviderContext>& pProviderContext);
    ScriptDir(const std::shared_ptr<ProviderContext>& pProviderContext, const OUString& sName,
              const OUString& sBaseUri);

    // XBrowseNode
    OUString SAL_CALL getName() override;
    css::uno::Sequence<css::uno::Reference<css::script::browse::XBrowseNode>>
        SAL_CALL getChildNodes() override;
    sal_Bool SAL_CALL hasChildNodes() override;
    sal_Int16 SAL_CALL getType() override;

    // XCreatableBrowseNode
    sal_Bool SAL_CALL isCreatableNode() override;
    css::uno::Reference<css::script::browse::XBrowseNode>
        SAL_CALL createNode(const OUString& sName) override;

private:
    OUString nameFromUrl(const OUString& sUrl);

    std::shared_ptr<ProviderContext> m_pProviderContext;

    OUString m_sName;
    OUString m_sBaseUri;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
