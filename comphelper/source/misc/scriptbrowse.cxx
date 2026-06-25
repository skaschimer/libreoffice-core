/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <comphelper/scriptbrowse.hxx>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/script/XInvocation.hpp>
#include <com/sun/star/script/browse/XBrowseNode.hpp>
#include <com/sun/star/script/browse/XCreatableBrowseNode.hpp>
#include <com/sun/star/script/browse/XDeletableBrowseNode.hpp>
#include <com/sun/star/script/browse/XEditableBrowseNode.hpp>
#include <com/sun/star/script/browse/XRenamableBrowseNode.hpp>

namespace
{
bool getBoolProperty(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
                     const OUString& sName)
{
    css::uno::Reference<css::beans::XPropertySet> xPropertySet(xNode, css::uno::UNO_QUERY);

    if (!xPropertySet.is())
        return false;

    css::uno::Any xResult;

    try
    {
        xResult = xPropertySet->getPropertyValue(sName);
    }
    catch (css::beans::UnknownPropertyException&)
    {
        return false;
    }

    bool bResult;

    if (!(xResult >>= bResult))
        return false;

    return bResult;
}

css::uno::Any invokeMethod(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
                           const OUString& sName, const css::uno::Sequence<css::uno::Any>& aParams)
{
    css::uno::Reference<css::script::XInvocation> xInvocation(xNode, css::uno::UNO_QUERY_THROW);

    css::uno::Sequence<sal_Int16> aOutParamIndex;
    css::uno::Sequence<css::uno::Any> aOutParam;

    return xInvocation->invoke(sName, aParams, aOutParamIndex, aOutParam);
}

css::uno::Any invokeMethod(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
                           const OUString& sName)
{
    return invokeMethod(xNode, sName, css::uno::Sequence<css::uno::Any>());
}

css::uno::Any invokeMethod(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
                           const OUString& sName, const OUString& sOtherName)
{
    css::uno::Sequence<css::uno::Any> aParams{ css::uno::Any(sOtherName) };

    return invokeMethod(xNode, sName, aParams);
}
}

namespace comphelper::scriptbrowse
{
bool isEditable(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode)
{
    css::uno::Reference<css::script::browse::XEditableBrowseNode> xEditable(xNode,
                                                                            css::uno::UNO_QUERY);

    if (xEditable.is())
        return xEditable->isEditableNode();
    else
        return getBoolProperty(xNode, u"Editable"_ustr);
}

bool editNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode)
{
    css::uno::Reference<css::script::browse::XEditableBrowseNode> xEditable(xNode,
                                                                            css::uno::UNO_QUERY);

    if (xEditable.is())
    {
        return xEditable->editNode();
    }
    else
    {
        bool bResult = false;

        invokeMethod(xNode, u"Editable"_ustr) >>= bResult;

        return bResult;
    }
}

bool isDeletable(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode)
{
    css::uno::Reference<css::script::browse::XDeletableBrowseNode> xDeletable(xNode,
                                                                              css::uno::UNO_QUERY);

    if (xDeletable.is())
        return xDeletable->isDeletableNode();
    else
        return getBoolProperty(xNode, u"Deletable"_ustr);
}

bool deleteNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode)
{
    css::uno::Reference<css::script::browse::XDeletableBrowseNode> xDeletable(xNode,
                                                                              css::uno::UNO_QUERY);

    if (xDeletable.is())
    {
        return xDeletable->deleteNode();
    }
    else
    {
        bool bResult = false;

        invokeMethod(xNode, u"Deletable"_ustr) >>= bResult;

        return bResult;
    }
}

bool isCreatable(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode)
{
    css::uno::Reference<css::script::browse::XCreatableBrowseNode> xCreatable(xNode,
                                                                              css::uno::UNO_QUERY);

    if (xCreatable.is())
        return xCreatable->isCreatableNode();
    else
        return getBoolProperty(xNode, u"Creatable"_ustr);
}

css::uno::Reference<css::script::browse::XBrowseNode>
createNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
           const OUString& sName)
{
    css::uno::Reference<css::script::browse::XCreatableBrowseNode> xCreatable(xNode,
                                                                              css::uno::UNO_QUERY);

    if (xCreatable.is())
    {
        return xCreatable->createNode(sName);
    }
    else
    {
        css::uno::Reference<css::script::browse::XBrowseNode> xResult;

        invokeMethod(xNode, u"Creatable"_ustr, sName) >>= xResult;

        return xResult;
    }
}

bool isRenamable(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode)
{
    css::uno::Reference<css::script::browse::XRenamableBrowseNode> xRenamable(xNode,
                                                                              css::uno::UNO_QUERY);

    if (xRenamable.is())
        return xRenamable->isRenamableNode();
    else
        return getBoolProperty(xNode, u"Renamable"_ustr);
}

css::uno::Reference<css::script::browse::XBrowseNode>
renameNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
           const OUString& sName)
{
    css::uno::Reference<css::script::browse::XRenamableBrowseNode> xRenamable(xNode,
                                                                              css::uno::UNO_QUERY);

    if (xRenamable.is())
    {
        return xRenamable->renameNode(sName);
    }
    else
    {
        css::uno::Reference<css::script::browse::XBrowseNode> xResult;

        invokeMethod(xNode, u"Renamable"_ustr, sName) >>= xResult;

        return xResult;
    }
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
