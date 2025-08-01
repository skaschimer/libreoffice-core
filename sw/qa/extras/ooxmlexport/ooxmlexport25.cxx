/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <swmodeltestbase.hxx>

#include <com/sun/star/beans/XPropertyState.hpp>

#include <comphelper/configuration.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <comphelper/propertyvalue.hxx>
#include <officecfg/Office/Common.hxx>

#include <pam.hxx>
#include <unotxdoc.hxx>
#include <docsh.hxx>
#include <IDocumentSettingAccess.hxx>
#include <wrtsh.hxx>

namespace
{
class Test : public SwModelTestBase
{
public:
    Test()
        : SwModelTestBase(u"/sw/qa/extras/ooxmlexport/data/"_ustr, u"Office Open XML Text"_ustr)
    {
    }
};

DECLARE_OOXMLEXPORT_TEST(testTdf166544_noTopMargin_fields, "tdf166544_noTopMargin_fields.docx")
{
    // given a document with a hyperlink field containing a page break
    auto pXmlDoc = parseLayoutDump();

    // The top margin is applied before the page break - since the page break follows the field end
    sal_Int32 nHeight = getXPath(pXmlDoc, "//page[2]//txt/infos/bounds", "height").toInt32();
    // Without the fix, the text height (showing a large top margin) was 569
    CPPUNIT_ASSERT_EQUAL(sal_Int32(269), nHeight);
}

DECLARE_OOXMLEXPORT_TEST(testTdf166141_linkedStyles, "tdf166141_linkedStyles.docx")
{
    // Given a document with settings.xml containing both linkStyles and attachedTemplate

    // The problem was that there should not be any spacing between the two paragraphs,
    // but there was 200 twips of below spacing.

    uno::Reference<beans::XPropertySet> xStyle(
        getStyles(u"ParagraphStyles"_ustr)->getByName(u"Standard"_ustr), uno::UNO_QUERY);
    // Since the "template" to update the styles from doesn't exist, make no changes
    // This was being forced to 200 twips by linkedStyles...
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), getProperty<sal_Int32>(xStyle, u"ParaBottomMargin"_ustr));
}

DECLARE_OOXMLEXPORT_TEST(testTdf166141_linkedStyles2, "tdf166141_linkedStyles2.docx")
{
    // Given a document with settings.xml containing only linkStyles - no attachedTemplate

    // Since no "template" was provided to update the styles from,
    // the styles must be updated using the application defaults.
    uno::Reference<beans::XPropertySet> xStyle(
        getStyles(u"ParagraphStyles"_ustr)->getByName(u"Standard"_ustr), uno::UNO_QUERY);
    // This must be 200 twips / 0.35cm.
    CPPUNIT_ASSERT_EQUAL(sal_Int32(353), getProperty<sal_Int32>(xStyle, u"ParaBottomMargin"_ustr));
}

DECLARE_OOXMLEXPORT_TEST(testTdf166510_sectPr_bottomSpacing, "tdf166510_sectPr_bottomSpacing.docx")
{
    // given with a sectPr with different bottom spacing (undefined in this case - i.e. zero)
    auto pXmlDoc = parseLayoutDump();

    // The last paragraph (sectPr) has 0 below spacing, so no reduction of page 2's 200pt top margin
    sal_Int32 nHeight = getXPath(pXmlDoc, "//page[2]//body/txt/infos/bounds", "height").toInt32();
    // Without the fix, the text height (showing no top margin at all) was 253
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4253), nHeight);
}

DECLARE_OOXMLEXPORT_TEST(testTdf167657_sectPr_bottomSpacing, "tdf167657_sectPr_bottomSpacing.docx")
{
    // given with a continuous break sectPr with no belowSpacing
    CPPUNIT_ASSERT_EQUAL(1, getPages());

    auto pXmlDoc = parseLayoutDump();

    // Since there is no page break, the prior paragraph's belowSpacing should not be zero'd out.
    // NOTE: apparently layout assigns the belowSpacing to the following section, not body/txt[1],
    sal_Int32 nHeight = getXPath(pXmlDoc, "//section/infos/bounds", "height").toInt32();
    // Without the fix, the section's height (showing no bottom margin for the 1st para) was 309
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1409), nHeight);
}

DECLARE_OOXMLEXPORT_TEST(testTdf165478_bottomAligned, "tdf165478_bottomAligned.docx")
{
    // given a layoutInCell, wrap-through image, paragraph-anchored to a bottom-aligned cell
    auto pXmlDoc = parseLayoutDump();

    // The text in the cell should be at the bottom of the cell
    assertXPathContent(pXmlDoc, "//cell[2]/txt", u"Bottom aligned");
    sal_Int32 nTextBottom = getXPath(pXmlDoc, "//cell[2]/txt/infos/bounds", "bottom").toInt32();
    sal_Int32 nCellBottom = getXPath(pXmlDoc, "//cell[2]/infos/bounds", "bottom").toInt32();

    // Without the fix, the text was at the top of the cell (2002) instead of at the bottom (4423)
    CPPUNIT_ASSERT_EQUAL(nCellBottom, nTextBottom);

    // The image is inside of the table
    sal_Int32 nFlyTop = getXPath(pXmlDoc, "//cell[2]//fly/infos/bounds", "top").toInt32();
    sal_Int32 nCellTop = getXPath(pXmlDoc, "//cell[2]/infos/bounds", "top").toInt32();
    // Without the fix, the image was above the table (284) instead of inside the cell (1887)
    CPPUNIT_ASSERT_GREATER(nCellTop, nFlyTop); // image is below the cell top
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1887), nFlyTop);
}

CPPUNIT_TEST_FIXTURE(Test, testTdf166620)
{
    createSwDoc();
    {
        SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
        pWrtShell->Insert(u"Body text"_ustr);
        pWrtShell->InsertFootnote({}, /*bEndNote=*/true, /*bEdit=*/true);
        pWrtShell->Insert(u"Endnote text"_ustr);
    }

    // Exporting to a Word format, a tab is prepended to the endnote text. When imported, the
    // NoGapAfterNoteNumber compatibility flag is enabled; and the exported tab is the only thing
    // that separates the number and the text. The tab must not be stripped away on import.
    saveAndReload(mpFilter);
    {
        auto xFactory = mxComponent.queryThrow<lang::XMultiServiceFactory>();
        auto xSettings = xFactory->createInstance(u"com.sun.star.document.Settings"_ustr);
        CPPUNIT_ASSERT(getProperty<bool>(xSettings, u"NoGapAfterNoteNumber"_ustr));

        auto xSupplier = mxComponent.queryThrow<text::XEndnotesSupplier>();
        auto xEndnotes = xSupplier->getEndnotes();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xEndnotes->getCount());
        auto xEndnoteText = xEndnotes->getByIndex(0).queryThrow<text::XText>();
        CPPUNIT_ASSERT_EQUAL(u"\tEndnote text"_ustr, xEndnoteText->getString());
    }
    // Do a second round-trip. It must not duplicate the tab.
    saveAndReload(mpFilter);
    {
        auto xFactory = mxComponent.queryThrow<lang::XMultiServiceFactory>();
        auto xSettings = xFactory->createInstance(u"com.sun.star.document.Settings"_ustr);
        CPPUNIT_ASSERT(getProperty<bool>(xSettings, u"NoGapAfterNoteNumber"_ustr));

        auto xSupplier = mxComponent.queryThrow<text::XEndnotesSupplier>();
        auto xEndnotes = xSupplier->getEndnotes();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xEndnotes->getCount());
        auto xEndnoteText = xEndnotes->getByIndex(0).queryThrow<text::XText>();
        CPPUNIT_ASSERT_EQUAL(u"\tEndnote text"_ustr, xEndnoteText->getString());

        // Remove the tab
        xEndnoteText->setString(u"Endnote text"_ustr);
    }
    // Do a third round-trip. It must not introduce the tab, because of the compatibility flag.
    saveAndReload(mpFilter);
    {
        auto xFactory = mxComponent.queryThrow<lang::XMultiServiceFactory>();
        auto xSettings = xFactory->createInstance(u"com.sun.star.document.Settings"_ustr);
        CPPUNIT_ASSERT(getProperty<bool>(xSettings, u"NoGapAfterNoteNumber"_ustr));

        auto xSupplier = mxComponent.queryThrow<text::XEndnotesSupplier>();
        auto xEndnotes = xSupplier->getEndnotes();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xEndnotes->getCount());
        auto xEndnoteText = xEndnotes->getByIndex(0).queryThrow<text::XText>();
        CPPUNIT_ASSERT_EQUAL(u"Endnote text"_ustr, xEndnoteText->getString());
    }
}

CPPUNIT_TEST_FIXTURE(Test, testTdf167082)
{
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: Heading 1
    // - Actual  : Standard

    createSwDoc("tdf167082.docx");
    saveAndReload(mpFilter);
    OUString aStyleName = getProperty<OUString>(getParagraph(3), u"ParaStyleName"_ustr);

    CPPUNIT_ASSERT_EQUAL(OUString("Heading 1"), aStyleName);
}

CPPUNIT_TEST_FIXTURE(Test, testFloatingTableAnchorPosExport)
{
    // Given a document with two floating tables after each other:
    // When saving that document to DOCX:
    loadAndSave("floattable-anchorpos.docx");

    // Then make sure that the dummy anchor of the first floating table is not written to the export
    // result:
    xmlDocUniquePtr pXmlDoc = parseExport(u"word/document.xml"_ustr);
    // Check the order of the floating tables: C is from the previous node, A is normal floating
    // table.
    CPPUNIT_ASSERT_EQUAL(u"C"_ustr,
                         getXPathContent(pXmlDoc, "//w:body/w:tbl[1]/w:tr/w:tc/w:p/w:r/w:t"));
    CPPUNIT_ASSERT_EQUAL(u"A"_ustr,
                         getXPathContent(pXmlDoc, "//w:body/w:tbl[2]/w:tr/w:tc/w:p/w:r/w:t"));
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 1
    // - Actual  : 2
    // i.e. the dummy anchor node was written to DOCX, leading to a Writer vs Word layout
    // difference.
    CPPUNIT_ASSERT_EQUAL(1, countXPathNodes(pXmlDoc, "//w:body/w:p"));
    CPPUNIT_ASSERT_EQUAL(u"D"_ustr, getXPathContent(pXmlDoc, "//w:body/w:p/w:r/w:t"));
}

CPPUNIT_TEST_FIXTURE(Test, testTdf167297)
{
    createSwDoc("tdf167297.fodt");

    auto fnVerify = [this] {
        auto pXmlDoc = parseLayoutDump();

        // The test document uses style:script-type to replace 12pt characters
        // with 96pt characters. Round-trip is confirmed by checking height.
        sal_Int32 nHeight = getXPath(pXmlDoc, "//txt/infos/bounds", "height").toInt32();
        CPPUNIT_ASSERT_GREATER(sal_Int32(2000), nHeight);
    };

    fnVerify();
    saveAndReload(mpFilter);
    fnVerify();
}

CPPUNIT_TEST_FIXTURE(Test, testTdf167583)
{
    createSwDoc();

    auto fnVerify = [&](bool bExpected) {
        auto xFactory = mxComponent.queryThrow<lang::XMultiServiceFactory>();
        auto xSettings = xFactory->createInstance(u"com.sun.star.document.Settings"_ustr);
        CPPUNIT_ASSERT_EQUAL(
            bExpected, getProperty<bool>(xSettings, u"AdjustTableLineHeightsToGridHeight"_ustr));
    };

    // By default, a Writer doc has the compat option set
    fnVerify(true);

    // Check that the value is persisted across save-reload
    saveAndReload(mpFilter);
    fnVerify(true);

    // Unset the compat flag
    {
        auto xFactory = mxComponent.queryThrow<lang::XMultiServiceFactory>();
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY);
        xSettings->setPropertyValue(u"AdjustTableLineHeightsToGridHeight"_ustr, uno::Any(false));
    }

    fnVerify(false);
    saveAndReload(mpFilter);
    fnVerify(false);
}

CPPUNIT_TEST_FIXTURE(Test, testTdf150822)
{
    createSwDoc("tdf150822.docx");

    auto fnVerify = [this] {
        auto pXmlDoc = parseLayoutDump();

        // Without the fix, vertical layout won't be parsed
        assertXPath(pXmlDoc, "//txt[@WritingMode='Vertical']", 3);
    };

    fnVerify();
    saveAndReload(mpFilter);
    fnVerify();
}

} // end of anonymous namespace
CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
