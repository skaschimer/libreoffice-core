/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <comphelper/scopeguard.hxx>
#include <test/unoapi_test.hxx>
#include <vcl/svapp.hxx>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/script/browse/XBrowseNode.hpp>
#include <com/sun/star/script/provider/XScript.hpp>
#include <com/sun/star/script/provider/XScriptProvider.hpp>

namespace
{
class AcceleratorKeyCheckerTest : public UnoApiTest
{
public:
    AcceleratorKeyCheckerTest()
        : UnoApiTest(u"/officecfg/util"_ustr)
    {
    }

private:
    void testAcceleratorKeyChecker();

    CPPUNIT_TEST_SUITE(AcceleratorKeyCheckerTest);
    CPPUNIT_TEST(testAcceleratorKeyChecker);
    CPPUNIT_TEST_SUITE_END();
};

css::uno::Reference<css::script::browse::XBrowseNode>
findChildNode(const css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
              const std::u16string_view sName)
{
    for (const auto& xChildNode : xNode->getChildNodes())
    {
        if (xChildNode->getName() == sName)
            return xChildNode;
    }

    CPPUNIT_FAIL(OUString(u"Couldn’t find child node "_ustr + sName).toUtf8().getStr());
}

// Tries running the script contained in the AcceleratorKeyChecker utility document
void AcceleratorKeyCheckerTest::testAcceleratorKeyChecker()
{
    // Make sure that dialogs are fatal so that if the script reports
    // an error then the test will fail
    DialogCancelMode nOldDialogCancelMode = Application::GetDialogCancelMode();
    comphelper::ScopeGuard g(
        [nOldDialogCancelMode]() { Application::SetDialogCancelMode(nOldDialogCancelMode); });
    Application::SetDialogCancelMode(DialogCancelMode::Fatal);

    loadFromFile(u"AcceleratorKeyChecker.fodt");

    // Get the script provider for BASIC macros in the document
    css::uno::Sequence<css::uno::Any> aArgs(1);
    aArgs.getArray()[0] <<= u"vnd.sun.star.tdoc:/1"_ustr;

    css::uno::Reference<css::script::browse::XBrowseNode> xRootNode(
        m_xFactory->createInstanceWithArgumentsAndContext(
            "com.sun.star.script.provider.ScriptProviderForBasic", aArgs, m_xContext),
        css::uno::UNO_QUERY_THROW);

    // Get the browse node for the testmenus macro
    css::uno::Reference<css::script::browse::XBrowseNode> xBrowseNode
        = findChildNode(xRootNode, u"AcceleratorKeyCheck");
    xBrowseNode = findChildNode(xBrowseNode, u"__testMenu");
    xBrowseNode = findChildNode(xBrowseNode, u"testmenus");

    // Get the URI for the macro
    css::uno::Reference<css::beans::XPropertySet> xNodeProperties(xBrowseNode,
                                                                  css::uno::UNO_QUERY_THROW);
    css::uno::Any value = xNodeProperties->getPropertyValue(u"URI"_ustr);
    OUString sURI;
    CPPUNIT_ASSERT(value >>= sURI);

    // Get the XScript for the macro from the script provider
    css::uno::Reference<css::script::provider::XScriptProvider> xScriptProvider(
        xRootNode, css::uno::UNO_QUERY_THROW);
    css::uno::Reference<css::script::provider::XScript> xScript = xScriptProvider->getScript(sURI);

    // Run the macro
    css::uno::Sequence<css::uno::Any> aParams;
    css::uno::Sequence<sal_Int16> aOutParamIndex;
    css::uno::Sequence<css::uno::Any> aOutParam;
    xScript->invoke(aParams, aOutParamIndex, aOutParam);
}

CPPUNIT_TEST_SUITE_REGISTRATION(AcceleratorKeyCheckerTest);

} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
