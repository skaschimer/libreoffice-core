/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <test/unoapi_test.hxx>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/script/browse/XBrowseNode.hpp>
#include <com/sun/star/script/browse/XCreatableBrowseNode.hpp>
#include <com/sun/star/script/browse/XRenamableBrowseNode.hpp>
#include <com/sun/star/ucb/UniversalContentBroker.hpp>

namespace
{
class BeanShellTest : public UnoApiTest
{
public:
    BeanShellTest()
        : UnoApiTest(u"/scripting/qa/extras"_ustr)
    {
    }

    void setUp() override;

private:
    void testRenameEmptyLibrary();
    void testRenameNonEmptyLibrary();

    CPPUNIT_TEST_SUITE(BeanShellTest);
    CPPUNIT_TEST(testRenameEmptyLibrary);
    CPPUNIT_TEST(testRenameNonEmptyLibrary);
    CPPUNIT_TEST_SUITE_END();
};

void BeanShellTest::setUp()
{
    UnoApiTest::setUp();

    // The ExpandContentProvider is needed for ScriptURIHelper to work
    css::uno::Reference<css::ucb::XUniversalContentBroker> xUcb
        = css::ucb::UniversalContentBroker::create(m_xContext);
    css::uno::Reference<css::ucb::XContentProvider> xExpandProvider(
        m_xSFactory->createInstance(u"com.sun.star.ucb.ExpandContentProvider"_ustr),
        css::uno::UNO_QUERY_THROW);
    xUcb->registerContentProvider(xExpandProvider, u"vnd.sun.star.expand"_ustr, true);
}

void createChild(css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
                 const OUString& sName)
{
    css::uno::Reference<css::script::browse::XCreatableBrowseNode> xCreatable(
        xNode, css::uno::UNO_QUERY_THROW);
    css::uno::Reference<css::script::browse::XBrowseNode> xResult = xCreatable->createNode(sName);
    CPPUNIT_ASSERT(xResult.is());
}

void renameLibrary(css::uno::Reference<css::script::browse::XBrowseNode>& xNode,
                   const OUString& sName)
{
    css::uno::Reference<css::script::browse::XRenamableBrowseNode> xRenamable(
        xNode, css::uno::UNO_QUERY_THROW);
    css::uno::Reference<css::script::browse::XBrowseNode> xResult = xRenamable->renameNode(sName);

    CPPUNIT_ASSERT_EQUAL(xResult, xNode);
}

css::uno::Reference<css::script::browse::XBrowseNode>
findChild(const css::uno::Reference<css::script::browse::XBrowseNode>& xParent,
          std::u16string_view sName)
{
    for (const auto& xChild : xParent->getChildNodes())
    {
        if (xChild->getName() == sName)
            return xChild;
    }

    OString sError
        = OUStringToOString(Concat2View(u"Couldn’t find "_ustr + sName), RTL_TEXTENCODING_UTF8);
    CPPUNIT_FAIL(sError.getStr());
}

void BeanShellTest::testRenameEmptyLibrary()
{
    css::uno::Sequence<css::uno::Any> aArgs = { css::uno::Any(u"user"_ustr) };

    css::uno::Reference<css::script::browse::XBrowseNode> xRoot(
        m_xFactory->createInstanceWithArgumentsAndContext(
            u"com.sun.star.script.provider.ScriptProviderForBeanShell"_ustr, aArgs, m_xContext),
        css::uno::UNO_QUERY_THROW);

    // Create an empty library
    createChild(xRoot, "EmptyLibrary");

    // Make sure the library we just created exists in the hierarchy
    css::uno::Reference<css::script::browse::XBrowseNode> xEmptyLibrary
        = findChild(xRoot, u"EmptyLibrary");

    // Try renaming it
    renameLibrary(xEmptyLibrary, "RenamedEmptyLibrary");

    // Make sure it now exists in the hierarchy with the new name
    findChild(xRoot, u"RenamedEmptyLibrary");
}

void BeanShellTest::testRenameNonEmptyLibrary()
{
    css::uno::Sequence<css::uno::Any> aArgs = { css::uno::Any(u"user"_ustr) };

    css::uno::Reference<css::script::browse::XBrowseNode> xRoot(
        m_xFactory->createInstanceWithArgumentsAndContext(
            u"com.sun.star.script.provider.ScriptProviderForBeanShell"_ustr, aArgs, m_xContext),
        css::uno::UNO_QUERY_THROW);

    // Create an empty library
    createChild(xRoot, "NonEmptyLibrary");

    // Make sure the library we just created exists in the hierarchy
    css::uno::Reference<css::script::browse::XBrowseNode> xNewLibrary
        = findChild(xRoot, u"NonEmptyLibrary");

    // Create a script within the new library
    createChild(xNewLibrary, "SomeScript");

    // Try renaming the library
    renameLibrary(xNewLibrary, "RenamedNonEmptyLibrary");

    // Make sure it now exists in the hierarchy with the new name
    css::uno::Reference<css::script::browse::XBrowseNode> xNewNode
        = findChild(xRoot, u"RenamedNonEmptyLibrary");

    // Make sure it still has the child
    css::uno::Sequence<css::uno::Reference<css::script::browse::XBrowseNode>> xChildren
        = xNewNode->getChildNodes();
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xChildren.getLength());
    CPPUNIT_ASSERT_EQUAL(u"SomeScript.bsh"_ustr, xChildren[0]->getName());

    // Make sure the child has an updated URI
    css::uno::Reference<css::beans::XPropertySet> xChildProps(xChildren[0],
                                                              css::uno::UNO_QUERY_THROW);
    OUString sUri;
    CPPUNIT_ASSERT(xChildProps->getPropertyValue("URI") >>= sUri);
    CPPUNIT_ASSERT(sUri.indexOf("RenamedNonEmptyLibrary") != -1);
}

CPPUNIT_TEST_SUITE_REGISTRATION(BeanShellTest);

} // namespace
CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
