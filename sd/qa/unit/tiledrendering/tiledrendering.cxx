/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sdtiledrenderingtest.hxx>

#include <app.hrc>
#include <test/helper/transferable.hxx>
#include <boost/property_tree/json_parser.hpp>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <sal/log.hxx>
#include <comphelper/propertysequence.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/string.hxx>
#include <comphelper/hash.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/editids.hrc>
#include <editeng/editobj.hxx>
#include <editeng/editview.hxx>
#include <editeng/numitem.hxx>
#include <editeng/outliner.hxx>
#include <editeng/fhgtitem.hxx>
#include <editeng/outlobj.hxx>
#include <osl/conditn.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/viewfrm.hxx>
#include <svl/stritem.hxx>
#include <svl/intitem.hxx>
#include <svx/unoapi.hxx>
#include <svx/svdotable.hxx>
#include <svx/svdoutl.hxx>
#include <unotools/datetime.hxx>

#include <DrawDocShell.hxx>
#include <ViewShellBase.hxx>
#include <ViewShell.hxx>
#include <SlideshowLayerRenderer.hxx>
#include <sdpage.hxx>
#include <unomodel.hxx>
#include <drawdoc.hxx>
#include <undo/undomanager.hxx>
#include <sfx2/request.hxx>

#include <svx/svxids.hrc>
#include <pres.hxx>
#include <navigatr.hxx>
#include <vcl/cursor.hxx>
#include <vcl/scheduler.hxx>
#include <vcl/vclevent.hxx>
#include <vcl/BitmapReadAccess.hxx>
#include <vcl/virdev.hxx>
#include <sfx2/sidebar/Sidebar.hxx>
#include <vcl/BitmapTools.hxx>
#include <vcl/filter/PngImageWriter.hxx>
#include <sfx2/lokhelper.hxx>

#include <chrono>
#include <cstdlib>
#include <string_view>

using namespace css;

static std::ostream& operator<<(std::ostream& os, ViewShellId id)
{
    os << static_cast<sal_Int32>(id);
    return os;
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCreateDestroy)
{
    createDoc("dummy.odp");
    // Nothing to do, the tearDown call should cleanup.
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCreateView)
{
    createDoc("dummy.odp");

    SfxLokHelper::createView();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testRegisterCallback)
{
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    setupLibreOfficeKitViewCallback(pViewShell->GetViewShellBase());

    // Start text edit of the empty title shape.
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    pView->SdrBeginTextEdit(pObject);
    CPPUNIT_ASSERT(pView->GetTextEditObject());

    // Check that the top left 256x256px tile would be invalidated.
    CPPUNIT_ASSERT(!m_aInvalidation.IsEmpty());
    ::tools::Rectangle aTopLeft(0, 0, 256*15, 256*15); // 1 px = 15 twips, assuming 96 DPI.
    CPPUNIT_ASSERT(m_aInvalidation.Overlaps(aTopLeft));
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testPostKeyEvent)
{
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    CPPUNIT_ASSERT_EQUAL(SdrObjKind::TitleText, pObject->GetObjIdentifier());
    SdrTextObj* pTextObj = static_cast<SdrTextObj*>(pObject);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pTextObj, pView->GetSdrPageView());
    SfxStringItem aInputString(SID_ATTR_CHAR, u"x"_ustr);
    pViewShell->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR,
            SfxCallMode::SYNCHRON, { &aInputString });

    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'x', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'x', 0);
    Scheduler::ProcessEventsToIdle();

    CPPUNIT_ASSERT(pView->GetTextEditObject());
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    // Did we manage to enter a second character?
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(2), rEditView.GetSelection().start.nIndex);
    ESelection aWordSelection(0, 0, 0, 2); // start para, start char, end para, end char.
    rEditView.SetSelection(aWordSelection);
    // Did we enter the expected character?
    CPPUNIT_ASSERT_EQUAL(u"xx"_ustr, rEditView.GetSelected());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testPostMouseEvent)
{
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    CPPUNIT_ASSERT_EQUAL(SdrObjKind::TitleText, pObject->GetObjIdentifier());
    SdrTextObj* pTextObj = static_cast<SdrTextObj*>(pObject);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pTextObj, pView->GetSdrPageView());
    SfxStringItem aInputString(SID_ATTR_CHAR, u"x"_ustr);
    pViewShell->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR,
            SfxCallMode::SYNCHRON, { &aInputString });
    CPPUNIT_ASSERT(pView->GetTextEditObject());
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    // Did we manage to go after the first character?
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), rEditView.GetSelection().start.nIndex);

    vcl::Cursor* pCursor = rEditView.GetCursor();
    Point aPosition(pCursor->GetPos().getX(), pCursor->GetPos().getY() + pCursor->GetSize().Height() / 2);
    aPosition.setX(aPosition.getX() - 1000);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      o3tl::toTwips(aPosition.getX(), o3tl::Length::mm100), o3tl::toTwips(aPosition.getY(), o3tl::Length::mm100),
                                      1, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      o3tl::toTwips(aPosition.getX(), o3tl::Length::mm100), o3tl::toTwips(aPosition.getY(), o3tl::Length::mm100),
                                      1, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(pView->GetTextEditObject());
    // The new cursor position must be before the first word.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), rEditView.GetSelection().start.nIndex);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSetTextSelection)
{
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    uno::Reference<container::XIndexAccess> xDrawPage(pXImpressDocument->getDrawPages()->getByIndex(0), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xShape(xDrawPage->getByIndex(0), uno::UNO_QUERY);
    xShape->setString(u"Aaa bbb."_ustr);
    // Create a selection on the second word.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    pView->SdrBeginTextEdit(pObject);
    CPPUNIT_ASSERT(pView->GetTextEditObject());
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    ESelection aWordSelection(0, 4, 0, 7);
    rEditView.SetSelection(aWordSelection);
    // Did we indeed manage to select the second word?
    CPPUNIT_ASSERT_EQUAL(u"bbb"_ustr, rEditView.GetSelected());

    // Now use setTextSelection() to move the end of the selection 1000 twips right.
    vcl::Cursor* pCursor = rEditView.GetCursor();
    Point aEnd = pCursor->GetPos();
    aEnd.setX(aEnd.getX() + 1000);
    pXImpressDocument->setTextSelection(LOK_SETTEXTSELECTION_END, aEnd.getX(), aEnd.getY());
    // The new selection must include the ending dot, too -- but not the first word.
    CPPUNIT_ASSERT_EQUAL(u"bbb."_ustr, rEditView.GetSelected());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testGetTextSelection)
{
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    uno::Reference<container::XIndexAccess> xDrawPage(pXImpressDocument->getDrawPages()->getByIndex(0), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xShape(xDrawPage->getByIndex(0), uno::UNO_QUERY);
    xShape->setString(u"Shape"_ustr);
    // Create a selection on the shape text.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    pView->SdrBeginTextEdit(pObject);
    CPPUNIT_ASSERT(pView->GetTextEditObject());
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    ESelection aWordSelection(0, 0, 0, 5);
    rEditView.SetSelection(aWordSelection);
    // Did we indeed manage to copy the selected text?
    CPPUNIT_ASSERT_EQUAL("Shape"_ostr, apitest::helper::transferable::getTextSelection(pXImpressDocument->getSelection(), "text/plain;charset=utf-8"_ostr));

    // Make sure returned RTF is not empty.
    CPPUNIT_ASSERT(!apitest::helper::transferable::getTextSelection(pXImpressDocument->getSelection(), "text/rtf"_ostr).isEmpty());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSetGraphicSelection)
{
    SdXImpressDocument* pXImpressDocument = createDoc("shape.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pPage = pViewShell->GetActualPage();
    SdrObject* pObject = pPage->GetObj(0);
    SdrHdlList handleList(nullptr);
    pObject->AddToHdlList(handleList);
    // Make sure the rectangle has 8 handles: at each corner and at the center of each edge.
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(8), handleList.GetHdlCount());
    // Take the bottom center one.
    SdrHdl* pHdl = handleList.GetHdl(6);
    CPPUNIT_ASSERT_EQUAL(int(SdrHdlKind::Lower), static_cast<int>(pHdl->GetKind()));
    ::tools::Rectangle aShapeBefore = pObject->GetSnapRect();
    // Resize.
    pXImpressDocument->setGraphicSelection(LOK_SETGRAPHICSELECTION_START, o3tl::toTwips(pHdl->GetPos().getX(), o3tl::Length::mm100), o3tl::toTwips(pHdl->GetPos().getY(), o3tl::Length::mm100));
    pXImpressDocument->setGraphicSelection(LOK_SETGRAPHICSELECTION_END, o3tl::toTwips(pHdl->GetPos().getX(), o3tl::Length::mm100), o3tl::toTwips(pHdl->GetPos().getY() + 1000, o3tl::Length::mm100));

    // Assert that view shell ID tracking works.
    sal_Int32 nView1 = SfxLokHelper::getCurrentView();
    SdDrawDocument* pDocument = pXImpressDocument->GetDoc();
    sd::UndoManager* pUndoManager = pDocument->GetUndoManager();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), pUndoManager->GetUndoActionCount());
    auto pListAction = dynamic_cast<SfxListUndoAction*>(pUndoManager->GetUndoAction());
    CPPUNIT_ASSERT(pListAction);
    for (size_t i = 0; i < pListAction->maUndoActions.size(); ++i)
        // The second item was -1 here, view shell ID wasn't known.
        CPPUNIT_ASSERT_EQUAL(ViewShellId(nView1), pListAction->GetUndoAction(i)->GetViewShellId());

    ::tools::Rectangle aShapeAfter = pObject->GetSnapRect();
    // Check that a resize happened, but aspect ratio is not kept.
    CPPUNIT_ASSERT_EQUAL(aShapeBefore.getOpenWidth(), aShapeAfter.getOpenWidth());
    CPPUNIT_ASSERT(aShapeBefore.getOpenHeight() < aShapeAfter.getOpenHeight());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testUndoShells)
{
    // Load a document and set the page size.
    SdXImpressDocument* pXImpressDocument = createDoc("shape.odp");
    uno::Sequence<beans::PropertyValue> aPropertyValues(comphelper::InitPropertySequence(
    {
        {"AttributePageSize.Width", uno::Any(static_cast<sal_Int32>(10000))},
        {"AttributePageSize.Height", uno::Any(static_cast<sal_Int32>(10000))},
    }));
    dispatchCommand(mxComponent, u".uno:AttributePageSize"_ustr, aPropertyValues);

    // Assert that view shell ID tracking works for SdUndoAction subclasses.
    SdDrawDocument* pDocument = pXImpressDocument->GetDoc();
    sd::UndoManager* pUndoManager = pDocument->GetUndoManager();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), pUndoManager->GetUndoActionCount());
    sal_Int32 nView1 = SfxLokHelper::getCurrentView();
    // This was -1, SdUndoGroup did not track what view shell created it.
    CPPUNIT_ASSERT_EQUAL(ViewShellId(nView1), pUndoManager->GetUndoAction()->GetViewShellId());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testResetSelection)
{
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    uno::Reference<container::XIndexAccess> xDrawPage(pXImpressDocument->getDrawPages()->getByIndex(0), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xShape(xDrawPage->getByIndex(0), uno::UNO_QUERY);
    xShape->setString(u"Aaa bbb."_ustr);
    // Create a selection on the second word.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    pView->SdrBeginTextEdit(pObject);
    CPPUNIT_ASSERT(pView->GetTextEditObject());
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    ESelection aWordSelection(0, 4, 0, 7);
    rEditView.SetSelection(aWordSelection);
    // Did we indeed manage to select the second word?
    CPPUNIT_ASSERT_EQUAL(u"bbb"_ustr, rEditView.GetSelected());

    // Now use resetSelection() to reset the selection.
    pXImpressDocument->resetSelection();
    CPPUNIT_ASSERT(!pView->GetTextEditObject());
}

namespace
{

std::vector<OUString> getCurrentParts(SdXImpressDocument* pDocument)
{
    int parts = pDocument->getParts();
    std::vector<OUString> result;

    result.reserve(parts);
    for (int i = 0; i < parts; i++)
    {
        result.push_back(pDocument->getPartName(i));
    }

    return result;
}

}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testInsertDeletePage)
{
    SdXImpressDocument* pXImpressDocument = createDoc("insert-delete.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    setupLibreOfficeKitViewCallback(pViewShell->GetViewShellBase());

    SdDrawDocument* pDoc = pXImpressDocument->GetDocShell()->GetDoc();
    CPPUNIT_ASSERT(pDoc);

    std::vector<OUString> aInserted =
    {
        u"Slide 1"_ustr, u"Slide 2"_ustr, u"Slide 3"_ustr, u"Slide 4"_ustr, u"Slide 5"_ustr,
        u"Slide 6"_ustr, u"Slide 7"_ustr, u"Slide 8"_ustr, u"Slide 9"_ustr, u"Slide 10"_ustr, u"Slide 11"_ustr
    };

    std::vector<OUString> aDeleted =
    {
        u"Slide 1"_ustr
    };

    // the document has 1 slide
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(1), pDoc->GetSdPageCount(PageKind::Standard));

    uno::Sequence<beans::PropertyValue> aArgs;

    // Insert slides
    m_aDocumentSizeCondition.reset();
    for (unsigned it = 1; it <= 10; it++)
        dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, aArgs);

    osl::Condition::Result aResult = m_aDocumentSizeCondition.wait(std::chrono::seconds(2));
    CPPUNIT_ASSERT_EQUAL(osl::Condition::result_ok, aResult);

    // Verify inserted slides
    std::vector<OUString> aPageList(getCurrentParts(pXImpressDocument));
    CPPUNIT_ASSERT_EQUAL(aPageList.size(), aInserted.size());

    for (auto it1 = aPageList.begin(), it2 = aInserted.begin(); it1 != aPageList.end(); ++it1, ++it2)
    {
        CPPUNIT_ASSERT_EQUAL(*it1, *it2);
    }

    // Delete slides
    m_aDocumentSizeCondition.reset();
    for (unsigned it = 1; it <= 10; it++)
        dispatchCommand(mxComponent, u".uno:DeletePage"_ustr, aArgs);

    aResult = m_aDocumentSizeCondition.wait(std::chrono::seconds(2));
    CPPUNIT_ASSERT_EQUAL(osl::Condition::result_ok, aResult);

    // Verify deleted slides
    aPageList = getCurrentParts(pXImpressDocument);
    CPPUNIT_ASSERT_EQUAL(aPageList.size(), aDeleted.size());
    for (auto it1 = aPageList.begin(), it2 = aDeleted.begin(); it1 != aPageList.end(); ++it1, ++it2)
    {
        CPPUNIT_ASSERT_EQUAL(*it1, *it2);
    }

    // Undo deleted slides
    m_aDocumentSizeCondition.reset();
    for (unsigned it = 1; it <= 10; it++)
        dispatchCommand(mxComponent, u".uno:Undo"_ustr, aArgs);

    aResult = m_aDocumentSizeCondition.wait(std::chrono::seconds(2));
    CPPUNIT_ASSERT_EQUAL(osl::Condition::result_ok, aResult);

    // Verify inserted slides
    aPageList = getCurrentParts(pXImpressDocument);
    CPPUNIT_ASSERT_EQUAL(aPageList.size(), aInserted.size());
    for (auto it1 = aPageList.begin(), it2 = aInserted.begin(); it1 != aPageList.end(); ++it1, ++it2)
    {
        CPPUNIT_ASSERT_EQUAL(*it1, *it2);
    }

    // Redo deleted slides
    m_aDocumentSizeCondition.reset();
    for (unsigned it = 1; it <= 10; it++)
        dispatchCommand(mxComponent, u".uno:Redo"_ustr, aArgs);

    aResult = m_aDocumentSizeCondition.wait(std::chrono::seconds(2));
    CPPUNIT_ASSERT_EQUAL(osl::Condition::result_ok, aResult);

    // Verify deleted slides
    aPageList = getCurrentParts(pXImpressDocument);
    CPPUNIT_ASSERT_EQUAL(aPageList.size(), aDeleted.size());
    for (auto it1 = aPageList.begin(), it2 = aDeleted.begin(); it1 != aPageList.end(); ++it1, ++it2)
    {
        CPPUNIT_ASSERT_EQUAL(*it1, *it2);
    }

    // the document has 1 slide
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(1), pDoc->GetSdPageCount(PageKind::Standard));
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testInsertTable)
{
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");

    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence(
    {
        { "Rows", uno::Any(sal_Int32(3)) },
        { "Columns", uno::Any(sal_Int32(5)) }
    }));

    dispatchCommand(mxComponent, u".uno:InsertTable"_ustr, aArgs);

    // get the table
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(1);
    CPPUNIT_ASSERT(pObject);

    // check that the table is not in the top left corner
    Point aPos(pObject->GetRelativePos());

    CPPUNIT_ASSERT(aPos.X() != 0);
    CPPUNIT_ASSERT(aPos.Y() != 0);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testDeleteTable)
{
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");

    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence(
    {
        { "Rows", uno::Any(sal_Int32(3)) },
        { "Columns", uno::Any(sal_Int32(5)) }
    }));

    dispatchCommand(mxComponent, u".uno:InsertTable"_ustr, aArgs);
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pSdrView = pViewShell->GetView();
    const SdrMarkList& rMarkList = pSdrView->GetMarkedObjectList();
    CPPUNIT_ASSERT(rMarkList.GetMarkCount());
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_MOD1 | awt::Key::A);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_MOD1 | awt::Key::A);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_DELETE);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_DELETE);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(!rMarkList.GetMarkCount());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testPartHash)
{
    SdXImpressDocument* pDoc = createDoc("dummy.odp");

    int nParts = pDoc->getParts();
    for (int it = 0; it < nParts; it++)
    {
        CPPUNIT_ASSERT(!pDoc->getPartHash(it).isEmpty());
    }

    // check part that it does not exists
    CPPUNIT_ASSERT(pDoc->getPartHash(100).isEmpty());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testResizeTable)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("table.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pObject);
    CPPUNIT_ASSERT(pTableObject);

    // Select the table by marking it + starting and ending text edit.
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pObject, pView->GetSdrPageView());
    pView->SdrBeginTextEdit(pObject);
    pView->SdrEndTextEdit();

    // Remember the original row heights.
    uno::Reference<table::XColumnRowRange> xTable = pTableObject->getTable();
    uno::Reference<container::XIndexAccess> xRows = xTable->getRows();
    uno::Reference<beans::XPropertySet> xRow1(xRows->getByIndex(0), uno::UNO_QUERY);
    sal_Int32 nExpectedRow1 = xRow1->getPropertyValue(u"Size"_ustr).get<sal_Int32>();
    uno::Reference<beans::XPropertySet> xRow2(xRows->getByIndex(1), uno::UNO_QUERY);
    sal_Int32 nExpectedRow2 = xRow2->getPropertyValue(u"Size"_ustr).get<sal_Int32>();

    // Resize the upper row, decrease its height by 1 cm.
    Point aInnerRowEdge = pObject->GetSnapRect().Center();
    pXImpressDocument->setGraphicSelection(LOK_SETGRAPHICSELECTION_START, o3tl::toTwips(aInnerRowEdge.getX(), o3tl::Length::mm100), o3tl::toTwips(aInnerRowEdge.getY(), o3tl::Length::mm100));
    pXImpressDocument->setGraphicSelection(LOK_SETGRAPHICSELECTION_END, o3tl::toTwips(aInnerRowEdge.getX(), o3tl::Length::mm100), o3tl::toTwips(aInnerRowEdge.getY() - 1000, o3tl::Length::mm100));

    // Remember the resized row heights.
    sal_Int32 nResizedRow1 = xRow1->getPropertyValue(u"Size"_ustr).get<sal_Int32>();
    CPPUNIT_ASSERT(nResizedRow1 < nExpectedRow1);
    sal_Int32 nResizedRow2 = xRow2->getPropertyValue(u"Size"_ustr).get<sal_Int32>();
    CPPUNIT_ASSERT_EQUAL(nExpectedRow2, nResizedRow2);

    // Now undo the resize.
    pXImpressDocument->GetDocShell()->GetUndoManager()->Undo();

    // Check the undo result.
    sal_Int32 nActualRow1 = xRow1->getPropertyValue(u"Size"_ustr).get<sal_Int32>();
    CPPUNIT_ASSERT_EQUAL(nExpectedRow1, nActualRow1);
    sal_Int32 nActualRow2 = xRow2->getPropertyValue(u"Size"_ustr).get<sal_Int32>();
    // Expected was 4000, actual was 4572, i.e. the second row after undo was larger than expected.
    CPPUNIT_ASSERT_EQUAL(nExpectedRow2, nActualRow2);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testResizeTableColumn)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("table-column.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pObject);
    CPPUNIT_ASSERT(pTableObject);

    // Select the table by marking it + starting and ending text edit.
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pObject, pView->GetSdrPageView());
    pView->SdrBeginTextEdit(pObject);
    pView->SdrEndTextEdit();

    // Remember the original cell widths.
    xmlDocUniquePtr pXmlDoc = parseXmlDump();
    OString aPrefix = "/SdDrawDocument/SdrModel/maPages/SdPage/SdrPage/SdrObjList/SdrTableObj/SdrTableObjImpl/TableLayouter/columns/"_ostr;
    sal_Int32 nExpectedColumn1 = getXPath(pXmlDoc, aPrefix + "TableLayouter_Layout[1]", "size").toInt32();
    sal_Int32 nExpectedColumn2 = getXPath(pXmlDoc, aPrefix + "TableLayouter_Layout[2]", "size").toInt32();
    pXmlDoc = nullptr;

    // Resize the left column, decrease its width by 1 cm.
    Point aInnerRowEdge = pObject->GetSnapRect().Center();
    pXImpressDocument->setGraphicSelection(LOK_SETGRAPHICSELECTION_START, o3tl::toTwips(aInnerRowEdge.getX(), o3tl::Length::mm100), o3tl::toTwips(aInnerRowEdge.getY(), o3tl::Length::mm100));
    pXImpressDocument->setGraphicSelection(LOK_SETGRAPHICSELECTION_END, o3tl::toTwips(aInnerRowEdge.getX() - 1000, o3tl::Length::mm100), o3tl::toTwips(aInnerRowEdge.getY(), o3tl::Length::mm100));

    // Remember the resized column widths.
    pXmlDoc = parseXmlDump();
    sal_Int32 nResizedColumn1 = getXPath(pXmlDoc, aPrefix + "TableLayouter_Layout[1]", "size").toInt32();
    CPPUNIT_ASSERT(nResizedColumn1 < nExpectedColumn1);
    sal_Int32 nResizedColumn2 = getXPath(pXmlDoc, aPrefix + "TableLayouter_Layout[2]", "size").toInt32();
    CPPUNIT_ASSERT(nResizedColumn2 > nExpectedColumn2);
    pXmlDoc = nullptr;

    // Now undo the resize.
    pXImpressDocument->GetDocShell()->GetUndoManager()->Undo();

    // Check the undo result.
    pXmlDoc = parseXmlDump();
    sal_Int32 nActualColumn1 = getXPath(pXmlDoc, aPrefix + "TableLayouter_Layout[1]", "size").toInt32();
    // Expected was 7049, actual was 6048, i.e. the first column width after undo was 1cm smaller than expected.
    CPPUNIT_ASSERT_EQUAL(nExpectedColumn1, nActualColumn1);
    sal_Int32 nActualColumn2 = getXPath(pXmlDoc, aPrefix + "TableLayouter_Layout[2]", "size").toInt32();
    CPPUNIT_ASSERT_EQUAL(nExpectedColumn2, nActualColumn2);
    pXmlDoc = nullptr;
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testViewCursors)
{
    // Create two views.
    SdXImpressDocument* pXImpressDocument = createDoc("shape.odp");
    SdTestViewCallback aView1;
    SfxLokHelper::createView();
    SdTestViewCallback aView2;

    // Select the shape in the second view.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pObject, pView->GetSdrPageView());
    Scheduler::ProcessEventsToIdle();

    // First view notices that there was a selection change in the other view.
    CPPUNIT_ASSERT(aView1.m_bGraphicViewSelectionInvalidated);
    // Second view notices that there was a selection change in its own view.
    CPPUNIT_ASSERT(aView2.m_bGraphicSelectionInvalidated);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testViewCursorParts)
{
    // Create two views.
    SdXImpressDocument* pXImpressDocument = createDoc("shape.odp");
    SdTestViewCallback aView1;
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SdTestViewCallback aView2;

    // Select the shape in the second view.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pObject, pView->GetSdrPageView());
    Scheduler::ProcessEventsToIdle();
    // First view notices that there was a selection change in the other view.
    CPPUNIT_ASSERT(aView1.m_bGraphicViewSelectionInvalidated);
    pView->UnmarkAllObj(pView->GetSdrPageView());

    // Now switch to the second part in the second view.
    pXImpressDocument->setPart(1);
    aView2.m_nPart = 1;
    aView1.m_bGraphicViewSelectionInvalidated = false;
    pActualPage = pViewShell->GetActualPage();
    pObject = pActualPage->GetObj(0);
    pView->MarkObj(pObject, pView->GetSdrPageView());
    Scheduler::ProcessEventsToIdle();
    // First view ignores view selection, as it would be for part 1, and it's in part 0.
    // This failed when the "part" was always 0 in the callback.
    CPPUNIT_ASSERT(!aView1.m_bGraphicViewSelectionInvalidated);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCursorViews)
{
    // Create the first view.
    SdXImpressDocument* pXImpressDocument = createDoc("title-shape.odp");
    int nView1 = SfxLokHelper::getCurrentView();
    SdTestViewCallback aView1;

    // Begin text edit on the only object on the slide.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pView = pViewShell->GetView();
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'x', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'x', 0);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(pView->IsTextEdit());

    // Make sure that cursor state is not changed just because we create a second view.
    aView1.m_bCursorVisibleChanged = false;
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(!aView1.m_bCursorVisibleChanged);

    // Make sure that typing in the first view causes an invalidation in the
    // second view as well, even if the second view was created after begin
    // text edit in the first view.
    SdTestViewCallback aView2;
    // This failed: the second view didn't get a lock notification, even if the
    // first view already started text edit.
    CPPUNIT_ASSERT(aView2.m_bViewLock);
    SfxLokHelper::setView(nView1);
    aView2.m_bTilesInvalidated = false;
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'x', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'x', 0);
    Scheduler::ProcessEventsToIdle();
    // This failed: the second view was not invalidated when pressing a key in
    // the first view.
    CPPUNIT_ASSERT(aView2.m_bTilesInvalidated);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCursorVisibility_SingleClick)
{
    // Single-clicking in a text box enters editing only
    // when it's on the text, even if it's the default text.

    // Load doc.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    SdTestViewCallback aView1;

    // Begin text edit on the only object on the slide.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject1 = pActualPage->GetObj(0);
    CPPUNIT_ASSERT(pObject1 != nullptr);
    CPPUNIT_ASSERT_EQUAL(SdrObjKind::TitleText, pObject1->GetObjIdentifier());
    SdrTextObj* pTextObject = static_cast<SdrTextObj*>(pObject1);

    // Click once outside of the text (in the first quartile) => no editing.
    const ::tools::Rectangle aRect = pTextObject->GetCurrentBoundRect();
    const auto cornerX = o3tl::toTwips(aRect.Left() + (aRect.getOpenWidth() / 4), o3tl::Length::mm100);
    const auto cornerY = o3tl::toTwips(aRect.Top() + (aRect.getOpenHeight() / 4), o3tl::Length::mm100);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      cornerX, cornerY,
                                      1, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      cornerX, cornerY,
                                      1, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();

    // No editing.
    CPPUNIT_ASSERT(!pViewShell->GetView()->IsTextEdit());
    CPPUNIT_ASSERT(!aView1.m_bCursorVisible);

    // Click again, now on the text, in the center, to start editing.
    const auto centerX = o3tl::toTwips(aRect.Left() + (aRect.getOpenWidth() / 2), o3tl::Length::mm100);
    const auto centerY = o3tl::toTwips(aRect.Top() + (aRect.getOpenHeight() / 2), o3tl::Length::mm100);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      centerX, centerY,
                                      1, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      centerX, centerY,
                                      1, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();

    // We must be in text editing mode and have cursor visible.
    CPPUNIT_ASSERT(pViewShell->GetView()->IsTextEdit());
    CPPUNIT_ASSERT(aView1.m_bCursorVisible);
}


CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCursorVisibility_DoubleClick)
{
    // Double-clicking anywhere in the TextBox should start editing.

    // Create the first view.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    SdTestViewCallback aView1;

    // Begin text edit on the only object on the slide.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject1 = pActualPage->GetObj(0);
    CPPUNIT_ASSERT(pObject1 != nullptr);
    CPPUNIT_ASSERT_EQUAL(SdrObjKind::TitleText, pObject1->GetObjIdentifier());
    SdrTextObj* pTextObject = static_cast<SdrTextObj*>(pObject1);

    // Double-click outside the text to enter edit mode.
    const ::tools::Rectangle aRect = pTextObject->GetCurrentBoundRect();
    const auto cornerX = o3tl::toTwips(aRect.Left() + (aRect.getOpenWidth() / 4), o3tl::Length::mm100);
    const auto cornerY = o3tl::toTwips(aRect.Top() + (aRect.getOpenHeight() / 4), o3tl::Length::mm100);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      cornerX, cornerY,
                                      2, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      cornerX, cornerY,
                                      2, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();

    // We must be in text editing mode and have cursor visible.
    CPPUNIT_ASSERT(pViewShell->GetView()->IsTextEdit());
    CPPUNIT_ASSERT(aView1.m_bCursorVisible);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCursorVisibility_MultiView)
{
    // Create the first view.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    const int nView1 = SfxLokHelper::getCurrentView();
    SdTestViewCallback aView1;

    // Begin text edit on the only object on the slide.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject1 = pActualPage->GetObj(0);
    CPPUNIT_ASSERT(pObject1);
    CPPUNIT_ASSERT_EQUAL(SdrObjKind::TitleText, pObject1->GetObjIdentifier());
    SdrTextObj* pTextObject = static_cast<SdrTextObj*>(pObject1);

    // Make sure that cursor state is not changed just because we create a second view.
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    const int nView2 = SfxLokHelper::getCurrentView();
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT_EQUAL(false, aView1.m_bCursorVisibleChanged);
    CPPUNIT_ASSERT_EQUAL(false, aView1.m_aViewCursorVisibilities[nView2]);

    // Also check that the second view gets the notifications.
    SdTestViewCallback aView2;

    SfxLokHelper::setView(nView1);

    ::tools::Rectangle aRect = pTextObject->GetCurrentBoundRect();
    const auto centerX = o3tl::toTwips(aRect.Left() + (aRect.getOpenWidth() / 2), o3tl::Length::mm100);
    const auto centerY = o3tl::toTwips(aRect.Top() + (aRect.getOpenHeight() / 2), o3tl::Length::mm100);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      centerX, centerY,
                                      2, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      centerX, centerY,
                                      2, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();

    // We must be in text editing mode and have cursor visible.
    CPPUNIT_ASSERT(pViewShell->GetView()->IsTextEdit());
    CPPUNIT_ASSERT(aView1.m_bCursorVisible);
    CPPUNIT_ASSERT_EQUAL(false, aView1.m_aViewCursorVisibilities[nView2]);

    CPPUNIT_ASSERT_EQUAL(false, aView2.m_bCursorVisible);
    CPPUNIT_ASSERT_EQUAL(false, aView2.m_aViewCursorVisibilities[nView1]);
    CPPUNIT_ASSERT_EQUAL(false, aView2.m_aViewCursorVisibilities[nView2]);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCursorVisibility_Escape)
{
    // Load doc.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    SdTestViewCallback aView1;

    // Begin text edit on the only object on the slide.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject1 = pActualPage->GetObj(0);
    CPPUNIT_ASSERT(pObject1 != nullptr);
    CPPUNIT_ASSERT_EQUAL(SdrObjKind::TitleText, pObject1->GetObjIdentifier());
    SdrTextObj* pTextObject = static_cast<SdrTextObj*>(pObject1);

    // Click once on the text to start editing.
    const ::tools::Rectangle aRect = pTextObject->GetCurrentBoundRect();
    const auto centerX = o3tl::toTwips(aRect.Left() + (aRect.getOpenWidth() / 2), o3tl::Length::mm100);
    const auto centerY = o3tl::toTwips(aRect.Top() + (aRect.getOpenHeight() / 2), o3tl::Length::mm100);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      centerX, centerY,
                                      1, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      centerX, centerY,
                                      1, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();

    // We must be in text editing mode and have cursor visible.
    CPPUNIT_ASSERT(pViewShell->GetView()->IsTextEdit());
    CPPUNIT_ASSERT(aView1.m_bCursorVisible);

    // End editing by pressing the escape key.
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::ESCAPE);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::ESCAPE);
    Scheduler::ProcessEventsToIdle();

    // We must be in text editing mode and have cursor visible.
    CPPUNIT_ASSERT(!pViewShell->GetView()->IsTextEdit());
    CPPUNIT_ASSERT_EQUAL(false, aView1.m_bCursorVisible);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testViewLock)
{
    // Load a document that has a shape and create two views.
    SdXImpressDocument* pXImpressDocument = createDoc("shape.odp");
    SdTestViewCallback aView1;
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());

    // Begin text edit in the second view and assert that the first gets a lock
    // notification.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    aView1.m_bViewLock = false;
    pView->SdrBeginTextEdit(pObject);
    CPPUNIT_ASSERT(aView1.m_bViewLock);

    // End text edit in the second view, and assert that the lock is removed in
    // the first view.
    pView->SdrEndTextEdit();
    CPPUNIT_ASSERT(!aView1.m_bViewLock);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testUndoLimiting)
{
    // Create the first view.
    SdXImpressDocument* pXImpressDocument = createDoc("title-shape.odp");
    sd::ViewShell* pViewShell1 = pXImpressDocument->GetDocShell()->GetViewShell();
    int nView1 = SfxLokHelper::getCurrentView();
    SfxLokHelper::createView();
    sd::ViewShell* pViewShell2 = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell1 != pViewShell2);

    // Begin text edit on the only object on the slide.
    SfxLokHelper::setView(nView1);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'x', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'x', 0);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(pViewShell1->GetView()->IsTextEdit());

    // View2 UNDO stack should be empty
    {
        SfxRequest aReq2(SID_UNDO, SfxCallMode::SLOT, pXImpressDocument->GetDocShell()->GetDoc()->GetPool());
        aReq2.AppendItem(SfxUInt16Item(SID_UNDO, 1));
        pViewShell2->ExecuteSlot(aReq2);
        const auto* pReturnValue = aReq2.GetReturnValue().getItem();
        CPPUNIT_ASSERT(!pReturnValue);
    }

    // View1 can UNDO
    {
        SfxRequest aReq1(SID_UNDO, SfxCallMode::SLOT, pXImpressDocument->GetDocShell()->GetDoc()->GetPool());
        aReq1.AppendItem(SfxUInt16Item(SID_UNDO, 1));
        pViewShell1->ExecuteSlot(aReq1);
        CPPUNIT_ASSERT(aReq1.IsDone());
    }

    // View1 can REDO
    {
        SfxRequest aReq1(SID_REDO, SfxCallMode::SLOT, pXImpressDocument->GetDocShell()->GetDoc()->GetPool());
        aReq1.AppendItem(SfxUInt16Item(SID_REDO, 1));
        pViewShell1->ExecuteSlot(aReq1);
        CPPUNIT_ASSERT(aReq1.IsDone());
    }

    // Exit text edit mode
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::ESCAPE);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::ESCAPE);
    Scheduler::ProcessEventsToIdle();

    CPPUNIT_ASSERT(!pViewShell1->GetView()->IsTextEdit());

    // Now check view2 cannot undo actions.
    {
        SfxRequest aReq2(SID_UNDO, SfxCallMode::SLOT, pXImpressDocument->GetDocShell()->GetDoc()->GetPool());
        aReq2.AppendItem(SfxUInt16Item(SID_UNDO, 1));
        pViewShell2->ExecuteSlot(aReq2);
        const SfxUInt32Item* pUInt32Item = dynamic_cast<const SfxUInt32Item*>(aReq2.GetReturnValue().getItem());
        CPPUNIT_ASSERT(pUInt32Item);
        CPPUNIT_ASSERT_EQUAL(static_cast< sal_uInt32 >(SID_REPAIRPACKAGE), pUInt32Item->GetValue());
    }

    // Now check view1 can undo action
    {
        SfxRequest aReq1(SID_UNDO, SfxCallMode::SLOT, pXImpressDocument->GetDocShell()->GetDoc()->GetPool());
        aReq1.AppendItem(SfxUInt16Item(SID_UNDO, 1));
        pViewShell1->ExecuteSlot(aReq1);
        CPPUNIT_ASSERT(aReq1.IsDone());
    }
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCreateViewGraphicSelection)
{
    // Load a document and register a callback.
    SdXImpressDocument* pXImpressDocument = createDoc("shape.odp");
    SdTestViewCallback aView1;

    // Select the only shape in the document and assert that the graphic selection is changed.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    aView1.m_bGraphicSelectionInvalidated = false;
    pView->MarkObj(pObject, pView->GetSdrPageView());
    CPPUNIT_ASSERT(aView1.m_bGraphicSelectionInvalidated);

    // Now create a new view.
    aView1.m_bGraphicSelectionInvalidated = false;
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering({});
    // This failed, creating a new view affected the graphic selection of an
    // existing view.
    CPPUNIT_ASSERT(!aView1.m_bGraphicSelectionInvalidated);

    // Check that when the first view has a shape selected and we register a
    // callback on the second view, then it gets a "graphic view selection".
    SdTestViewCallback aView2;
    // This failed, the created new view had no "view selection" of the first
    // view's selected shape.
    CPPUNIT_ASSERT(aView2.m_bGraphicViewSelectionInvalidated);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCreateViewTextCursor)
{
    // Load a document and register a callback.
    SdXImpressDocument* pXImpressDocument = createDoc("title-shape.odp");
    SdTestViewCallback aView1;

    // Begin text edit.
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'x', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'x', 0);
    Scheduler::ProcessEventsToIdle();
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pSdrView = pViewShell->GetView();
    CPPUNIT_ASSERT(pSdrView->IsTextEdit());

    // Create an editeng text selection.
    EditView& rEditView = pSdrView->GetTextEditOutlinerView()->GetEditView();
    // 0th para, 0th char -> 0th para, 1st char.
    ESelection aWordSelection(0, 0, 0, 1);
    rEditView.SetSelection(aWordSelection);

    // Make sure that creating a new view either doesn't affect the previous
    // one, or at least the effect is not visible at the end.
    aView1.m_aViewCursorInvalidations.clear();
    aView1.m_aViewCursorVisibilities.clear();
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering({});
    SdTestViewCallback aView2;
    bool bFoundCursor = false;
    for (const auto& rInvalidation : aView1.m_aViewCursorInvalidations)
    {
        auto itVisibility = aView1.m_aViewCursorVisibilities.find(rInvalidation.first);
        // For each cursor invalidation: if there is no visibility or the visibility is true, that's a problem.
        if (itVisibility == aView1.m_aViewCursorVisibilities.end() || itVisibility->second)
        {
            bFoundCursor = true;
            break;
        }
    }
    // This failed: the second view created an unexpected view cursor in the
    // first view.
    CPPUNIT_ASSERT(!bFoundCursor);
    // This failed: the text view selection of the first view wasn't seen by
    // the second view.
    CPPUNIT_ASSERT(aView2.m_bViewSelectionSet);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testTdf102223)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("tdf102223.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pActualPage->GetObj(2));
    CPPUNIT_ASSERT(pTableObject);
    SdrView* pView = pViewShell->GetView();

    // select contents of cell
    ::tools::Rectangle aRect = pTableObject->GetCurrentBoundRect();
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      o3tl::toTwips(aRect.Left() + 2, o3tl::Length::mm100), o3tl::toTwips(aRect.Top() + 2, o3tl::Length::mm100),
                                      1, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      o3tl::toTwips(aRect.Left() + 2, o3tl::Length::mm100), o3tl::toTwips(aRect.Top() + 2, o3tl::Length::mm100),
                                      1, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();
    pView->SdrBeginTextEdit(pTableObject);
    CPPUNIT_ASSERT(pView->GetTextEditObject());
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    rEditView.SetSelection(ESelection(0, 0, 0, 3)); // start para, start char, end para, end char.
    CPPUNIT_ASSERT_EQUAL(u"Red"_ustr, rEditView.GetSelected());
    CPPUNIT_ASSERT_EQUAL(
        int(1411), static_cast<int>(rEditView.GetAttribs().Get(EE_CHAR_FONTHEIGHT).GetHeight()));

    // cut contents of cell
    uno::Sequence<beans::PropertyValue> aArgs;
    dispatchCommand(mxComponent, u".uno:Cut"_ustr, aArgs);

    pView->SdrEndTextEdit(false);
    pView->SdrBeginTextEdit(pTableObject);
    CPPUNIT_ASSERT(pView->GetTextEditObject());
    EditView& rEditView2 = pView->GetTextEditOutlinerView()->GetEditView();
    rEditView2.SetSelection(ESelection(0, 0, 0, 1)); // start para, start char, end para, end char.
    CPPUNIT_ASSERT_EQUAL(
        int(1411), static_cast<int>(rEditView2.GetAttribs().Get(EE_CHAR_FONTHEIGHT).GetHeight()));
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testTdf118354)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("tdf118354.odp");

    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), pActualPage->GetObjCount());

    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pActualPage->GetObj(0));
    CPPUNIT_ASSERT(pTableObject);

    // Without the fix, it would crash here
    ::tools::Rectangle aRect = pTableObject->GetCurrentBoundRect();
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      o3tl::toTwips(aRect.Left() + 2, o3tl::Length::mm100), o3tl::toTwips(aRect.Top() + 2, o3tl::Length::mm100),
                                      1, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      o3tl::toTwips(aRect.Left() + 2, o3tl::Length::mm100), o3tl::toTwips(aRect.Top() + 2, o3tl::Length::mm100),
                                      1, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();

    SdrView* pView = pViewShell->GetView();
    auto pMarkedObj = dynamic_cast<sdr::table::SdrTableObj*>(pView->GetMarkedObjectList().GetMark(0)->GetMarkedSdrObj());
    CPPUNIT_ASSERT_EQUAL(pMarkedObj, pTableObject);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testPostKeyEventInvalidation)
{
    // Load a document and begin text edit on the first slide.
    SdXImpressDocument* pXImpressDocument = createDoc("2slides.odp");
    CPPUNIT_ASSERT_EQUAL(0, pXImpressDocument->getPart());
    SdTestViewCallback aView1;
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pView = pViewShell->GetView();
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_F2);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_F2);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(pView->GetTextEditObject());

    // Create a second view and begin text edit there as well, in parallel.
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering({});
    SdTestViewCallback aView2;
    pXImpressDocument->setPart(1);
    sd::ViewShell* pViewShell2 = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pView2 = pViewShell2->GetView();
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_F2);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_F2);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(pView2->GetTextEditObject());

    // Now go left with the cursor in the second view and watch for
    // invalidations.
    aView2.m_bTilesInvalidated = false;
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT);
    Scheduler::ProcessEventsToIdle();
    // This failed: moving the cursor caused unexpected invalidation.
    CPPUNIT_ASSERT(!aView2.m_bTilesInvalidated);
}

/**
 * tests a cut/paste bug around bullet items in a list and
 * graphic (bitmap) bullet items in a list (Tdf103083, Tdf166882)
 */
CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testClipNumRules)
{
    std::vector<const char*> aFileNames = { "tdf103083.fodp", "tdf166882.odp" };
    for (const char* pFileName : aFileNames)
    {
        // Load the document.
        SdXImpressDocument* pXImpressDocument = createDoc(pFileName);
        CPPUNIT_ASSERT(pXImpressDocument);

        sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
        SdPage* pActualPage = pViewShell->GetActualPage();

        SdrObject* pObject1 = pActualPage->GetObj(1);
        CPPUNIT_ASSERT_EQUAL(SdrObjKind::OutlineText, pObject1->GetObjIdentifier());
        SdrTextObj* pTextObject = static_cast<SdrTextObj*>(pObject1);

        SdrView* pView = pViewShell->GetView();

        // select contents of bullet item
        ::tools::Rectangle aRect = pTextObject->GetCurrentBoundRect();
        pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
            o3tl::toTwips(aRect.Left() + 2, o3tl::Length::mm100), o3tl::toTwips(aRect.Top() + 2, o3tl::Length::mm100),
            1, MOUSE_LEFT, 0);
        pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
            o3tl::toTwips(aRect.Left() + 2, o3tl::Length::mm100), o3tl::toTwips(aRect.Top() + 2, o3tl::Length::mm100),
            1, MOUSE_LEFT, 0);
        Scheduler::ProcessEventsToIdle();
        pView->SdrBeginTextEdit(pTextObject);
        CPPUNIT_ASSERT(pView->GetTextEditObject());
        EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
        rEditView.SetSelection(ESelection(2, 0, 2, 33)); // start para, start char, end para, end char.
        CPPUNIT_ASSERT_EQUAL(u"They have all the same formatting"_ustr, rEditView.GetSelected());
        SdrOutliner* pOutliner = pView->GetTextEditOutliner();
        CPPUNIT_ASSERT_EQUAL(u"No-Logo Content~LT~Gliederung 2"_ustr,
            pOutliner->GetStyleSheet(2)->GetName());
        const EditTextObject& aEdit = pTextObject->GetOutlinerParaObject()->GetTextObject();
        const SvxNumBulletItem* pNumFmt = aEdit.GetParaAttribs(2).GetItem(EE_PARA_NUMBULLET);
        SvxNumberFormat aNumFmt(pNumFmt->GetNumRule().GetLevel(2));

        // cut contents of bullet item
        dispatchCommand(mxComponent, u".uno:Cut"_ustr, uno::Sequence<beans::PropertyValue>());

        CPPUNIT_ASSERT(pView->GetTextEditObject());
        EditView& rEditView2 = pView->GetTextEditOutlinerView()->GetEditView();
        rEditView2.SetSelection(ESelection(2, 0, 2, 10)); // start para, start char, end para, end char.
        CPPUNIT_ASSERT_EQUAL(OUString(), rEditView2.GetSelected());

        // paste contents of bullet item
        dispatchCommand(mxComponent, u".uno:Paste"_ustr, uno::Sequence<beans::PropertyValue>());

        // send an ESC key to trigger the commit of the edit to the main model
        pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::ESCAPE);
        pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::ESCAPE);
        Scheduler::ProcessEventsToIdle();

        pView->SdrBeginTextEdit(pTextObject);
        CPPUNIT_ASSERT(pView->GetTextEditObject());
        pOutliner = pView->GetTextEditOutliner();
        EditView& rEditView3 = pView->GetTextEditOutlinerView()->GetEditView();
        rEditView3.SetSelection(ESelection(2, 0, 2, 33)); // start para, start char, end para, end char.
        CPPUNIT_ASSERT_EQUAL(u"They have all the same formatting"_ustr, rEditView3.GetSelected());
        CPPUNIT_ASSERT_EQUAL(u"No-Logo Content~LT~Gliederung 2"_ustr,
            pOutliner->GetStyleSheet(2)->GetName());

        const EditTextObject& aEdit2 = pTextObject->GetOutlinerParaObject()->GetTextObject();
        const SvxNumBulletItem* pNumFmt2 = aEdit2.GetParaAttribs(2).GetItem(EE_PARA_NUMBULLET);
        SvxNumberFormat aNumFmt2(pNumFmt2->GetNumRule().GetLevel(2));

        bool bEqual(aNumFmt2 == aNumFmt);
        CPPUNIT_ASSERT_MESSAGE("Bullet properties changed after paste", bEqual);
    }
}

/**
 * tests a clone-formatting bug around table cell attributes
 */
CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testTdf104405)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("tdf104405.fodp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(2);
    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pObject);
    CPPUNIT_ASSERT(pTableObject);

    // select the middle cell
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pTableObject, pView->GetSdrPageView());
    pTableObject->setActiveCell(sdr::table::CellPos(2,1));
    pView->SdrBeginTextEdit(pTableObject);
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    rEditView.SetSelection(ESelection(0, 0, 0, 3)); // start para, start char, end para, end char.

    // trigger the clone-formatting/paintbrush command to copy formatting contents of cell
    uno::Sequence aArgs{ comphelper::makePropertyValue(u"PersistentCopy"_ustr, true) };
    dispatchCommand(mxComponent, u".uno:FormatPaintbrush"_ustr, aArgs);

    // now click on the table
    pView->MarkObj(pTableObject, pView->GetSdrPageView());
    pTableObject->setActiveCell(sdr::table::CellPos(0,0));
    pView->SdrEndTextEdit(false);
    pView->SdrBeginTextEdit(pTableObject);
    EditView& rEditView2 = pView->GetTextEditOutlinerView()->GetEditView();
    rEditView2.SetSelection(ESelection(0, 0, 0, 3)); // start para, start char, end para, end char.
    ::tools::Rectangle aRect = pTableObject->GetCurrentBoundRect();
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      o3tl::toTwips(aRect.Left(), o3tl::Length::mm100), o3tl::toTwips(aRect.Top(), o3tl::Length::mm100),
                                      1, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      o3tl::toTwips(aRect.Left(), o3tl::Length::mm100), o3tl::toTwips(aRect.Top(), o3tl::Length::mm100),
                                      1, MOUSE_LEFT, 0);

    Scheduler::ProcessEventsToIdle();

    // check that the first cell has acquired the resulting vertical style
    xmlDocUniquePtr pXmlDoc = parseXmlDump();
    // the following name has a compiler-dependent part
    CPPUNIT_ASSERT_EQUAL(
        u"2"_ustr,
        getXPath(
            pXmlDoc,
            "/SdDrawDocument/SdrModel/maPages/SdPage/SdrPage/SdrObjList/SdrTableObj/SdrTableObjImpl"
                "/TableModel/Cell[1]/DefaultProperties/SfxItemSet/SdrTextVertAdjustItem",
            "value"));
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testTdf81754)
{
    SdXImpressDocument* pXImpressDocument = createDoc("tdf81754.pptx");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(1);

    SdrTextObj* pTextObj = static_cast<SdrTextObj*>(pObject);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pTextObj, pView->GetSdrPageView());
    SfxStringItem aInputString(SID_ATTR_CHAR, u"x"_ustr);
    pViewShell->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR,
            SfxCallMode::SYNCHRON, { &aInputString });

    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'x', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'x', 0);
    Scheduler::ProcessEventsToIdle();

    // now save, reload, and assert that we did not lose the edit
    saveAndReload(u"Impress Office Open XML"_ustr);

    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xPage(xDrawPagesSupplier->getDrawPages()->getByIndex(0),
                                             uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xShape(xPage->getByIndex(1), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"Somethingxx"_ustr, xShape->getString());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testTdf105502)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("tdf105502.odp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    sd::Window* pWindow = pViewShell->GetActiveWindow();
    CPPUNIT_ASSERT(pWindow);
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pObject);
    CPPUNIT_ASSERT(pTableObject);

    // Select the first row.
    sd::View* pView = pViewShell->GetView();
    pView->MarkObj(pObject, pView->GetSdrPageView());
    pView->SdrBeginTextEdit(pObject);
    rtl::Reference<sdr::SelectionController> xSelectionController(pView->getSelectionController());
    CPPUNIT_ASSERT(xSelectionController.is());
    SfxRequest aRequest(*pViewShell->GetViewFrame(), SID_TABLE_SELECT_ROW);
    xSelectionController->Execute(aRequest);

    // Assert that the A1:B1 selection succeeded.
    CPPUNIT_ASSERT(xSelectionController->hasSelectedCells());
    sdr::table::CellPos aFirstCell;
    sdr::table::CellPos aLastCell;
    xSelectionController->getSelectedCells(aFirstCell, aLastCell);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), aFirstCell.mnCol);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), aFirstCell.mnRow);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), aLastCell.mnCol);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), aLastCell.mnRow);

    // Grow font size for the selection.
    dispatchCommand(mxComponent, u".uno:Grow"_ustr, {});

    // Assert that the selected A1 has now a larger font than the unselected
    // A2.
    xmlDocUniquePtr pXmlDoc = parseXmlDump();
    sal_Int32 nA1Height = getXPath(pXmlDoc, "//Cell[1]/SdrText/OutlinerParaObject/EditTextObject/ContentInfo/SfxItemSet/SvxFontHeightItem[1]", "height").toInt32();
    sal_Int32 nA2Height = getXPath(pXmlDoc, "//Cell[3]/SdrText/OutlinerParaObject/EditTextObject/ContentInfo/attribs[1]/SvxFontHeightItem", "height").toInt32();
    // This failed when FuText::ChangeFontSize() never did "continue" in the
    // text loop, instead of doing so depending on what IsInSelection() returns.
    CPPUNIT_ASSERT(nA1Height > nA2Height);

    // Check that selection remains the same
    CPPUNIT_ASSERT(xSelectionController->hasSelectedCells());
    xSelectionController->getSelectedCells(aFirstCell, aLastCell);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), aFirstCell.mnCol);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), aFirstCell.mnRow);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), aLastCell.mnCol);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), aLastCell.mnRow);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testShowAndUseGridCallbacks)
{
    /*
        Showing and hiding grid is done via one command. Command toggles the grid state.
        Also "snapping the objects to grid" feature is toggled via one command.
        Here we switch on and off these 2 features and check the callbacks.
    */

    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());

    SdTestViewCallback aView;

    dispatchCommand(mxComponent, ".uno:GridVisible", uno::Sequence<beans::PropertyValue>());
    CPPUNIT_ASSERT(std::find(aView.m_aStateChanged.begin(), aView.m_aStateChanged.end(), ".uno:GridVisible=true") != aView.m_aStateChanged.end());

    dispatchCommand(mxComponent, ".uno:GridVisible", uno::Sequence<beans::PropertyValue>());
    CPPUNIT_ASSERT(std::find(aView.m_aStateChanged.begin(), aView.m_aStateChanged.end(), ".uno:GridVisible=false") != aView.m_aStateChanged.end());

    dispatchCommand(mxComponent, ".uno:GridUse", uno::Sequence<beans::PropertyValue>());
    CPPUNIT_ASSERT(std::find(aView.m_aStateChanged.begin(), aView.m_aStateChanged.end(), ".uno:GridUse=false") != aView.m_aStateChanged.end());

    dispatchCommand(mxComponent, ".uno:GridUse", uno::Sequence<beans::PropertyValue>());
    CPPUNIT_ASSERT(std::find(aView.m_aStateChanged.begin(), aView.m_aStateChanged.end(), ".uno:GridUse=true") != aView.m_aStateChanged.end());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCommentCallbacks)
{
    // Load the document.
    // Set the tiled annotations off
    comphelper::LibreOfficeKit::setTiledAnnotations(false);

    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp", comphelper::InitPropertySequence(
    {
        {".uno:Author", uno::Any(u"LOK User1"_ustr)},
    }));
    SdTestViewCallback aView1;
    int nView1 = SfxLokHelper::getCurrentView();

    SfxLokHelper::createView();
    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence(
    {
        {".uno:Author", uno::Any(u"LOK User2"_ustr)},
    }));
    pXImpressDocument->initializeForTiledRendering(aArgs);
    SdTestViewCallback aView2;
    int nView2 = SfxLokHelper::getCurrentView();

    SfxLokHelper::setView(nView1);

    // Add a new comment
    aArgs = comphelper::InitPropertySequence(
    {
        {"Text", uno::Any(u"Comment"_ustr)},
    });
    dispatchCommand(mxComponent, u".uno:InsertAnnotation"_ustr, aArgs);

    // We received a LOK_CALLBACK_COMMENT callback with comment 'Add' action
    CPPUNIT_ASSERT_EQUAL(std::string("Add"), aView1.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(std::string("Add"), aView2.m_aCommentCallbackResult.get<std::string>("action"));
    int nComment1 = aView1.m_aCommentCallbackResult.get<int>("id");
    CPPUNIT_ASSERT_EQUAL(nComment1, aView2.m_aCommentCallbackResult.get<int>("id"));
    css::util::DateTime aDateTime;
    OUString aDateTimeString = OUString::createFromAscii(aView1.m_aCommentCallbackResult.get<std::string>("dateTime"));
    CPPUNIT_ASSERT(utl::ISO8601parseDateTime(aDateTimeString, aDateTime));
    CPPUNIT_ASSERT_EQUAL(std::string("LOK User1"), aView1.m_aCommentCallbackResult.get<std::string>("author"));
    CPPUNIT_ASSERT_EQUAL(std::string("LOK User1"), aView2.m_aCommentCallbackResult.get<std::string>("author"));
    CPPUNIT_ASSERT_EQUAL(std::string("Comment"), aView1.m_aCommentCallbackResult.get<std::string>("text"));
    CPPUNIT_ASSERT_EQUAL(std::string("Comment"), aView2.m_aCommentCallbackResult.get<std::string>("text"));
    CPPUNIT_ASSERT(!aView1.m_aCommentCallbackResult.get<std::string>("parthash").empty());
    CPPUNIT_ASSERT(!aView2.m_aCommentCallbackResult.get<std::string>("parthash").empty());

    // Reply to a just added comment
    SfxLokHelper::setView(nView2);
    aArgs = comphelper::InitPropertySequence(
    {
        {"Id", uno::Any(OUString::number(nComment1))},
        {"Text", uno::Any(u"Reply to comment"_ustr)},
    });
    dispatchCommand(mxComponent, u".uno:ReplyToAnnotation"_ustr, aArgs);

    // We received a LOK_CALLBACK_COMMENT callback with comment 'Modify' action
    CPPUNIT_ASSERT_EQUAL(std::string("Modify"), aView1.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(std::string("Modify"), aView2.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(nComment1, aView1.m_aCommentCallbackResult.get<int>("id"));
    CPPUNIT_ASSERT_EQUAL(nComment1, aView2.m_aCommentCallbackResult.get<int>("id"));
    CPPUNIT_ASSERT_EQUAL(std::string("LOK User2"), aView1.m_aCommentCallbackResult.get<std::string>("author"));
    CPPUNIT_ASSERT_EQUAL(std::string("LOK User2"), aView2.m_aCommentCallbackResult.get<std::string>("author"));
    OUString aReplyTextView1 = OUString::createFromAscii(aView1.m_aCommentCallbackResult.get<std::string>("text"));
    OUString aReplyTextView2 = OUString::createFromAscii(aView2.m_aCommentCallbackResult.get<std::string>("text"));
    CPPUNIT_ASSERT(aReplyTextView1.startsWith("Reply to LOK User1"));
    CPPUNIT_ASSERT(aReplyTextView1.endsWith("Reply to comment"));
    CPPUNIT_ASSERT(aReplyTextView2.startsWith("Reply to LOK User1"));
    CPPUNIT_ASSERT(aReplyTextView2.endsWith("Reply to comment"));
    CPPUNIT_ASSERT(!aView1.m_aCommentCallbackResult.get<std::string>("parthash").empty());
    CPPUNIT_ASSERT(!aView2.m_aCommentCallbackResult.get<std::string>("parthash").empty());

    // Edit this annotation now
    aArgs = comphelper::InitPropertySequence(
    {
        {"Id", uno::Any(OUString::number(nComment1))},
        {"Text", uno::Any(u"Edited comment"_ustr)},
    });
    dispatchCommand(mxComponent, u".uno:EditAnnotation"_ustr, aArgs);

    // We received a LOK_CALLBACK_COMMENT callback with comment 'Modify' action
    CPPUNIT_ASSERT_EQUAL(std::string("Modify"), aView1.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(std::string("Modify"), aView2.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(nComment1, aView1.m_aCommentCallbackResult.get<int>("id"));
    CPPUNIT_ASSERT_EQUAL(nComment1, aView2.m_aCommentCallbackResult.get<int>("id"));
    CPPUNIT_ASSERT(!aView1.m_aCommentCallbackResult.get<std::string>("parthash").empty());
    CPPUNIT_ASSERT(!aView2.m_aCommentCallbackResult.get<std::string>("parthash").empty());
    CPPUNIT_ASSERT_EQUAL(std::string("Edited comment"), aView1.m_aCommentCallbackResult.get<std::string>("text"));
    CPPUNIT_ASSERT_EQUAL(std::string("Edited comment"), aView2.m_aCommentCallbackResult.get<std::string>("text"));

    // Delete the comment
    aArgs = comphelper::InitPropertySequence(
    {
        {"Id", uno::Any(OUString::number(nComment1))},
    });
    dispatchCommand(mxComponent, u".uno:DeleteAnnotation"_ustr, aArgs);

    // We received a LOK_CALLBACK_COMMENT callback with comment 'Remove' action
    CPPUNIT_ASSERT_EQUAL(std::string("Remove"), aView1.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(std::string("Remove"), aView2.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(nComment1, aView1.m_aCommentCallbackResult.get<int>("id"));
    CPPUNIT_ASSERT_EQUAL(nComment1, aView2.m_aCommentCallbackResult.get<int>("id"));

    comphelper::LibreOfficeKit::setTiledAnnotations(true);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCommentChangeImpress)
{
    uno::Sequence<beans::PropertyValue> aArgs;

    // Load the document.
    // Set the tiled annotations off
    comphelper::LibreOfficeKit::setTiledAnnotations(false);

    createDoc("dummy.odp", comphelper::InitPropertySequence(
    {
        {".uno:Author", uno::Any(u"LOK User1"_ustr)},
    }));

    SdTestViewCallback aView1;

    // Add a new comment
    aArgs = comphelper::InitPropertySequence(
    {
        {"Text", uno::Any(u"Comment"_ustr)},
    });
    dispatchCommand(mxComponent, u".uno:InsertAnnotation"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(std::string("Add"), aView1.m_aCommentCallbackResult.get<std::string>("action"));

    int nComment1 = aView1.m_aCommentCallbackResult.get<int>("id");

    CPPUNIT_ASSERT(!aView1.m_aCommentCallbackResult.get<std::string>("parthash").empty());
    CPPUNIT_ASSERT_EQUAL(std::string("Comment"), aView1.m_aCommentCallbackResult.get<std::string>("text"));
    CPPUNIT_ASSERT_EQUAL(std::string("0, 0, 478, 280"), aView1.m_aCommentCallbackResult.get<std::string>("rectangle"));

    // Edit this annotation now
    aArgs = comphelper::InitPropertySequence(
    {
        {"Id", uno::Any(OUString::number(nComment1))},
        {"PositionX", uno::Any(sal_Int32(10))},
        {"PositionY", uno::Any(sal_Int32(20))}
    });
    dispatchCommand(mxComponent, u".uno:EditAnnotation"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(std::string("Modify"), aView1.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(std::string("Comment"), aView1.m_aCommentCallbackResult.get<std::string>("text"));
    CPPUNIT_ASSERT_EQUAL(std::string("10, 20, 478, 280"), aView1.m_aCommentCallbackResult.get<std::string>("rectangle"));

    comphelper::LibreOfficeKit::setTiledAnnotations(true);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCommentChangeDraw)
{
    uno::Sequence<beans::PropertyValue> aArgs;

    // Load the document.
    // Set the tiled annotations off
    comphelper::LibreOfficeKit::setTiledAnnotations(false);

    createDoc("dummy.odg", comphelper::InitPropertySequence(
    {
        {".uno:Author", uno::Any(u"LOK User1"_ustr)},
    }));

    SdTestViewCallback aView1;

    // Add a new comment
    aArgs = comphelper::InitPropertySequence(
    {
        {"Text", uno::Any(u"Comment"_ustr)},
    });
    dispatchCommand(mxComponent, u".uno:InsertAnnotation"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(std::string("Add"), aView1.m_aCommentCallbackResult.get<std::string>("action"));

    int nComment1 = aView1.m_aCommentCallbackResult.get<int>("id");

    CPPUNIT_ASSERT(!aView1.m_aCommentCallbackResult.get<std::string>("parthash").empty());
    CPPUNIT_ASSERT_EQUAL(std::string("Comment"), aView1.m_aCommentCallbackResult.get<std::string>("text"));
    CPPUNIT_ASSERT_EQUAL(std::string("0, 0, 478, 280"), aView1.m_aCommentCallbackResult.get<std::string>("rectangle"));

    // Edit this annotation now
    aArgs = comphelper::InitPropertySequence(
    {
        {"Id", uno::Any(OUString::number(nComment1))},
        {"PositionX", uno::Any(sal_Int32(10))},
        {"PositionY", uno::Any(sal_Int32(20))}
    });
    dispatchCommand(mxComponent, u".uno:EditAnnotation"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(std::string("Modify"), aView1.m_aCommentCallbackResult.get<std::string>("action"));
    CPPUNIT_ASSERT_EQUAL(std::string("Comment"), aView1.m_aCommentCallbackResult.get<std::string>("text"));
    CPPUNIT_ASSERT_EQUAL(std::string("10, 20, 478, 280"), aView1.m_aCommentCallbackResult.get<std::string>("rectangle"));

    comphelper::LibreOfficeKit::setTiledAnnotations(true);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testMultiViewInsertDeletePage)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    SdTestViewCallback aView1;
    int nView1 = SfxLokHelper::getCurrentView();
    uno::Sequence<beans::PropertyValue> aArgs;
    SdDrawDocument* pDoc = pXImpressDocument->GetDocShell()->GetDoc();

    // Create second view
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering(aArgs);
    SdTestViewCallback aView2;
    int nView2 = SfxLokHelper::getCurrentView();

    // the document has 8 slides
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(8), pDoc->GetSdPageCount(PageKind::Standard));

    // Switch to 5th page in 2nd view
    pXImpressDocument->setPart(4);

    // Insert slide in 1st view
    SfxLokHelper::setView(nView1);
    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, aArgs);

    // See if the current slide number changed in 2nd view too
    SfxLokHelper::setView(nView2);
    CPPUNIT_ASSERT_EQUAL(5, pXImpressDocument->getPart());

    // Delete the page in 1st view now
    SfxLokHelper::setView(nView1);
    dispatchCommand(mxComponent, u".uno:DeletePage"_ustr, aArgs);

    // See if current slide number changed in 2nd view too
    SfxLokHelper::setView(nView2);
    CPPUNIT_ASSERT_EQUAL(4, pXImpressDocument->getPart());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testMultiViewInsertDeletePage2)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    SdTestViewCallback aView1;
    int nView1 = SfxLokHelper::getCurrentView();
    uno::Sequence<beans::PropertyValue> aArgs;
    SdDrawDocument* pDoc = pXImpressDocument->GetDocShell()->GetDoc();

    // Create second view
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering(aArgs);
    SdTestViewCallback aView2;
    int nView2 = SfxLokHelper::getCurrentView();

    // the document has 8 slides
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(8), pDoc->GetSdPageCount(PageKind::Standard));

    // Switch to 5th page in 2nd view
    pXImpressDocument->setPart(4);

    // Begin text edit on the only object on the slide.
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject1 = pActualPage->GetObj(0);
    CPPUNIT_ASSERT(pObject1 != nullptr);
    CPPUNIT_ASSERT_EQUAL(SdrObjKind::TitleText, pObject1->GetObjIdentifier());
    SdrTextObj* pTextObject = static_cast<SdrTextObj*>(pObject1);

    // Double-click outside the text to enter edit mode.
    const ::tools::Rectangle aRect = pTextObject->GetCurrentBoundRect();
    const auto cornerX = o3tl::toTwips(aRect.Left() + (aRect.getOpenWidth() / 4), o3tl::Length::mm100);
    const auto cornerY = o3tl::toTwips(aRect.Top() + (aRect.getOpenHeight() / 4), o3tl::Length::mm100);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONDOWN,
                                      cornerX, cornerY,
                                      2, MOUSE_LEFT, 0);
    pXImpressDocument->postMouseEvent(LOK_MOUSEEVENT_MOUSEBUTTONUP,
                                      cornerX, cornerY,
                                      2, MOUSE_LEFT, 0);
    Scheduler::ProcessEventsToIdle();

    // We must be in text editing mode and have cursor visible.
    CPPUNIT_ASSERT(pViewShell->GetView()->IsTextEdit());

    // Insert slide in 1st view
    SfxLokHelper::setView(nView1);
    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, aArgs);

    // See if the current slide number changed in 2nd view too
    SfxLokHelper::setView(nView2);
    CPPUNIT_ASSERT_EQUAL(5, pXImpressDocument->getPart());

    // Delete the page in 1st view now
    SfxLokHelper::setView(nView1);
    dispatchCommand(mxComponent, u".uno:DeletePage"_ustr, aArgs);

    // See if current slide number changed in 2nd view too
    SfxLokHelper::setView(nView2);
    CPPUNIT_ASSERT_EQUAL(4, pXImpressDocument->getPart());

    // We must be still in text editing mode and have cursor visible.
    CPPUNIT_ASSERT(pViewShell->GetView()->IsTextEdit());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testDisableUndoRepair)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");

    // Create View 1
    SfxViewShell* pView1 = SfxViewShell::Current();
    sd::ViewShell* pViewShell1 = pXImpressDocument->GetDocShell()->GetViewShell();
    int nView1 = SfxLokHelper::getCurrentView();

    // Create View 2
    SfxLokHelper::createView();
    SfxViewShell* pView2 = SfxViewShell::Current();
    sd::ViewShell* pViewShell2 = pXImpressDocument->GetDocShell()->GetViewShell();
    int nView2 = SfxLokHelper::getCurrentView();

    // Check UNDO is disabled
    {
        std::unique_ptr<SfxPoolItem> pItem1;
        std::unique_ptr<SfxPoolItem> pItem2;
        CPPUNIT_ASSERT_EQUAL(SfxItemState::DISABLED, pView1->GetViewFrame().GetBindings().QueryState(SID_UNDO, pItem1));
        CPPUNIT_ASSERT_EQUAL(SfxItemState::DISABLED, pView2->GetViewFrame().GetBindings().QueryState(SID_UNDO, pItem2));
    }

    // Insert a character in the first view.
    SfxLokHelper::setView(nView1);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'h', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'h', 0);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(pViewShell1->GetView()->IsTextEdit());
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::ESCAPE);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::ESCAPE);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(!pViewShell1->GetView()->IsTextEdit());

    // Check
    {
        std::unique_ptr<SfxPoolItem> xItem1;
        pView1->GetViewFrame().GetBindings().QueryState(SID_UNDO, xItem1);
        const auto* pUInt32Item1 = dynamic_cast<const SfxUInt32Item*>(xItem1.get());
        CPPUNIT_ASSERT(!pUInt32Item1);

        std::unique_ptr<SfxPoolItem> xItem2;
        pView2->GetViewFrame().GetBindings().QueryState(SID_UNDO, xItem2);
        const auto* pUInt32Item2 = dynamic_cast<const SfxUInt32Item*>(xItem2.get());
        CPPUNIT_ASSERT(pUInt32Item2);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt32>(SID_REPAIRPACKAGE), pUInt32Item2->GetValue());
    }

    // Insert a character in the second view.
    SfxLokHelper::setView(nView2);
    pXImpressDocument->setPart(1);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'c', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'c', 0);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(pViewShell2->GetView()->IsTextEdit());
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::ESCAPE);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::ESCAPE);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(!pViewShell2->GetView()->IsTextEdit());

    // Check
    {
        std::unique_ptr<SfxPoolItem> xItem1;
        pView1->GetViewFrame().GetBindings().QueryState(SID_UNDO, xItem1);
        const SfxUInt32Item* pUInt32Item = dynamic_cast<const SfxUInt32Item*>(xItem1.get());
        CPPUNIT_ASSERT(pUInt32Item);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt32>(SID_REPAIRPACKAGE), pUInt32Item->GetValue());

        std::unique_ptr<SfxPoolItem> xItem2;
        pView2->GetViewFrame().GetBindings().QueryState(SID_UNDO, xItem2);
        CPPUNIT_ASSERT(!dynamic_cast< const SfxUInt32Item* >(xItem2.get()));
    }
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testDocumentRepair)
{
    // Create two views.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    CPPUNIT_ASSERT(pXImpressDocument);

    // view #1
    SfxViewShell* pView1 = SfxViewShell::Current();

    // view #2
    SfxLokHelper::createView();
    SfxViewShell* pView2 = SfxViewShell::Current();
    int nView2 = SfxLokHelper::getCurrentView();
    sd::ViewShell* pViewShell2 = pXImpressDocument->GetDocShell()->GetViewShell();

    CPPUNIT_ASSERT(pView1 != pView2);
    {
        std::unique_ptr<SfxBoolItem> pItem1;
        pView1->GetViewFrame().GetBindings().QueryState(SID_DOC_REPAIR, pItem1);
        CPPUNIT_ASSERT(pItem1);
        CPPUNIT_ASSERT_EQUAL(false, pItem1->GetValue());

        std::unique_ptr<SfxBoolItem> pItem2;
        pView2->GetViewFrame().GetBindings().QueryState(SID_DOC_REPAIR, pItem2);
        CPPUNIT_ASSERT(pItem2);
        CPPUNIT_ASSERT_EQUAL(false, pItem2->GetValue());
    }

    // Insert a character in the second view.
    SfxLokHelper::setView(nView2);
    pXImpressDocument->setPart(1);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 'c', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 'c', 0);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(pViewShell2->GetView()->IsTextEdit());
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::ESCAPE);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::ESCAPE);
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT(!pViewShell2->GetView()->IsTextEdit());

    {
        std::unique_ptr<SfxBoolItem> pItem1;
        pView1->GetViewFrame().GetBindings().QueryState(SID_DOC_REPAIR, pItem1);
        CPPUNIT_ASSERT(pItem1);
        CPPUNIT_ASSERT_EQUAL(true, pItem1->GetValue());

        std::unique_ptr<SfxBoolItem> pItem2;
        pView2->GetViewFrame().GetBindings().QueryState(SID_DOC_REPAIR, pItem2);
        CPPUNIT_ASSERT(pItem2);
        CPPUNIT_ASSERT_EQUAL(true, pItem2->GetValue());
    }
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testLanguageStatus)
{
    // Load the document.
    createDoc("dummy.odp");
    SfxViewShell* pView1 = SfxViewShell::Current();
    SfxLokHelper::createView();
    SfxViewShell* pView2 = SfxViewShell::Current();
    {
        std::unique_ptr<SfxPoolItem> xItem1;
        std::unique_ptr<SfxPoolItem> xItem2;
        pView1->GetViewFrame().GetBindings().QueryState(SID_LANGUAGE_STATUS, xItem1);
        pView2->GetViewFrame().GetBindings().QueryState(SID_LANGUAGE_STATUS, xItem2);
        auto pStringItem = dynamic_cast<const SfxStringItem*>(xItem1.get());
        CPPUNIT_ASSERT(pStringItem);

        CPPUNIT_ASSERT_EQUAL(u"English (USA);en-US"_ustr, pStringItem->GetValue());

        CPPUNIT_ASSERT(dynamic_cast< const SfxStringItem* >(xItem2.get()));
    }
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testLanguageAllText)
{
    // Load the document, which has a single shape, with Hungarian text.
    createDoc("language-all-text.odp");

    // Set the language to English for all text.
    uno::Sequence<beans::PropertyValue> aArgs = comphelper::InitPropertySequence({
        { "Language", uno::Any(u"Default_English (USA)"_ustr) },
    });
    dispatchCommand(mxComponent, u".uno:LanguageStatus"_ustr, aArgs);

    // Assert that the shape text language was changed.
    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xPage(xDrawPagesSupplier->getDrawPages()->getByIndex(0),
                                             uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xShape(xPage->getByIndex(0), uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xRun(xShape, uno::UNO_QUERY_THROW);
    lang::Locale aLocale;
    xRun->getPropertyValue(u"CharLocale"_ustr) >>= aLocale;
    // Without the accompanying fix in place, this test would have failed with 'Expected: en;
    // Actual: hu', as the shape text language was not set.
    CPPUNIT_ASSERT_EQUAL(u"en"_ustr, aLocale.Language);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testDefaultView)
{
    // Load the document with notes view.
    SdXImpressDocument* pXImpressDocument = createDoc("notes-view.odp");
    sd::ViewShell* pView = pXImpressDocument->GetDocShell()->GetViewShell();
    {
        std::unique_ptr<SfxBoolItem> pImpressView;
        std::unique_ptr<SfxBoolItem> pNotesView;
        pView->GetViewFrame()->GetBindings().QueryState(SID_NORMAL_MULTI_PANE_GUI, pImpressView);
        pView->GetViewFrame()->GetBindings().QueryState(SID_NOTES_MODE, pNotesView);
        CPPUNIT_ASSERT(pImpressView);
        CPPUNIT_ASSERT(pNotesView);
        CPPUNIT_ASSERT_EQUAL(true, pImpressView->GetValue());
        CPPUNIT_ASSERT_EQUAL(false, pNotesView->GetValue());
    }
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testIMESupport)
{
    // Load the document with notes view.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    VclPtr<vcl::Window> pDocWindow = pXImpressDocument->getDocWindow();
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrObject* pObject = pViewShell->GetActualPage()->GetObj(0);
    SdrTextObj* pTextObj = static_cast<SdrTextObj*>(pObject);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pTextObj, pView->GetSdrPageView());
    SfxStringItem aInputString(SID_ATTR_CHAR, u"x"_ustr);
    pViewShell->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR,
                                                             SfxCallMode::SYNCHRON, { &aInputString });

    // sequence of chinese IME compositions when 'nihao' is typed in an IME
    const std::vector<OString> aUtf8Inputs{ "年"_ostr, "你"_ostr, "你好"_ostr, "你哈"_ostr, "你好"_ostr, "你好"_ostr };
    std::vector<OUString> aInputs;
    std::transform(aUtf8Inputs.begin(), aUtf8Inputs.end(),
                   std::back_inserter(aInputs), [](OString aInput) {
                       return OUString::fromUtf8(aInput);
                   });
    for (const auto& aInput: aInputs)
    {
        pDocWindow->PostExtTextInputEvent(VclEventId::ExtTextInput, aInput);
    }
    pDocWindow->PostExtTextInputEvent(VclEventId::EndExtTextInput, u""_ustr);

    // the cursor should be at position 3rd
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(3), rEditView.GetSelection().start.nIndex);

    ESelection aWordSelection(0, 0, 0, 3); // start para, start char, end para, end char.
    rEditView.SetSelection(aWordSelection);
    // content contains only the last IME composition, not all
    CPPUNIT_ASSERT_EQUAL(OUString("x" + aInputs[aInputs.size() - 1]), rEditView.GetSelected());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testTdf115783)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("tdf115783.fodp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pObject);
    CPPUNIT_ASSERT(pTableObject);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pTableObject, pView->GetSdrPageView());

    // Create a cell selection and set font height.
    // Go to the end of the B1 cell.
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT);
    // Create a B1->C1 cell selection.
    const int nShiftRight = KEY_SHIFT + KEY_RIGHT;
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, nShiftRight);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, nShiftRight);
    uno::Sequence<beans::PropertyValue> aArgs = comphelper::InitPropertySequence({
        { "FontHeight.Height", uno::Any(static_cast<float>(12)) },
    });
    dispatchCommand(mxComponent, u".uno:FontHeight"_ustr, aArgs);

    // Create a text selection on the B1 cell.
    pTableObject->setActiveCell(sdr::table::CellPos(1, 0));
    pView->SdrBeginTextEdit(pTableObject);
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    // Start para, start char, end para, end char.
    rEditView.SetSelection(ESelection(0, 0, 0, 5));
    CPPUNIT_ASSERT_EQUAL(u"hello"_ustr, rEditView.GetSelected());

    // Copy selection, paste at the start of the cell.
    aArgs = {};
    dispatchCommand(mxComponent, u".uno:Copy"_ustr, aArgs);
    rEditView.SetSelection(ESelection(0, 0, 0, 0));
    aArgs = {};
    dispatchCommand(mxComponent, u".uno:Paste"_ustr, aArgs);
    pView->SdrEndTextEdit();

    // And now verify that the cell has the correct font size.
    uno::Reference<table::XCellRange> xTable = pTableObject->getTable();
    CPPUNIT_ASSERT(xTable.is());
    uno::Reference<text::XTextRange> xCell(xTable->getCellByPosition(1, 0), uno::UNO_QUERY);
    CPPUNIT_ASSERT(xCell.is());
    uno::Reference<container::XEnumerationAccess> xText(xCell->getText(), uno::UNO_QUERY);
    CPPUNIT_ASSERT(xText.is());
    uno::Reference<container::XEnumerationAccess> xParagraph(
        xText->createEnumeration()->nextElement(), uno::UNO_QUERY);
    CPPUNIT_ASSERT(xParagraph.is());
    uno::Reference<text::XTextRange> xPortion(xParagraph->createEnumeration()->nextElement(),
                                              uno::UNO_QUERY);
    CPPUNIT_ASSERT(xPortion.is());
    // This failed, it was only "hello" as the paragraph had 2 portions: a
    // "hello" with 12pt size and a "hello" with 18pt.
    CPPUNIT_ASSERT_EQUAL(u"hellohello"_ustr, xPortion->getString());
    uno::Reference<beans::XPropertySet> xPropertySet(xPortion, uno::UNO_QUERY);
    int nHeight = xPropertySet->getPropertyValue(u"CharHeight"_ustr).get<float>();
    // Make sure that the single font size for the cell is the expected one.
    CPPUNIT_ASSERT_EQUAL(12, nHeight);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testPasteTextOnSlide)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("paste_text_onslide.odp");
    CPPUNIT_ASSERT(pXImpressDocument);

    // select second text object
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    Scheduler::ProcessEventsToIdle();

    // step into text editing
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, '1', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, '1', 0);
    Scheduler::ProcessEventsToIdle();

    // select full text
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT | KEY_SHIFT);
    Scheduler::ProcessEventsToIdle();

    // Copy some text
    dispatchCommand(mxComponent, u".uno:Copy"_ustr, uno::Sequence<beans::PropertyValue>());

    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::ESCAPE);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::ESCAPE);
    Scheduler::ProcessEventsToIdle();

    // Paste onto the slide
    dispatchCommand(mxComponent, u".uno:Paste"_ustr, uno::Sequence<beans::PropertyValue>());

    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::ESCAPE);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::ESCAPE);
    Scheduler::ProcessEventsToIdle();

    // Check the position of the newly added text shape, created for pasted text
    SdPage* pActualPage = pXImpressDocument->GetDocShell()->GetViewShell()->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());
    SdrObject* pObject = pActualPage->GetObj(2);
    CPPUNIT_ASSERT(pObject);
    SdrTextObj* pTextObj = DynCastSdrTextObj(pObject);
    CPPUNIT_ASSERT(pTextObj);
    CPPUNIT_ASSERT_EQUAL(SdrObjKind::Text, pTextObj->GetObjIdentifier());
    const Point aPos = pTextObj->GetLastBoundRect().TopLeft();
    CPPUNIT_ASSERT_EQUAL(static_cast<tools::Long>(0), aPos.getX());
    CPPUNIT_ASSERT_EQUAL(static_cast<tools::Long>(0), aPos.getY());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testTdf115873)
{
    // Initialize the navigator.
    SdXImpressDocument* pXImpressDocument = createDoc("tdf115873.fodp");
    SfxViewShell* pViewShell = SfxViewShell::Current();
    CPPUNIT_ASSERT(pViewShell);
    SfxBindings& rBindings = pViewShell->GetViewFrame().GetBindings();
    auto xNavigator = std::make_unique<SdNavigatorWin>(nullptr, &rBindings, nullptr);
    xNavigator->InitTreeLB(pXImpressDocument->GetDoc());
    SdPageObjsTLV& rObjects = xNavigator->GetObjects();
    rObjects.SelectEntry(u"Slide 1");
    rObjects.Select();
    sd::ViewShell* pSdViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pSdrView = pSdViewShell->GetView();
    pSdrView->UnmarkAllObj(pSdrView->GetSdrPageView());

    // Make sure that no shapes are selected.
    const SdrMarkList& rMarkList = pSdrView->GetMarkedObjectList();
    Scheduler::ProcessEventsToIdle();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), rMarkList.GetMarkCount());

    // Single-click with the mouse.
    MouseEvent aMouseEvent(Point(0, 0), /*nClicks=*/1, MouseEventModifiers::NONE, MOUSE_LEFT);
    rObjects.MousePressHdl(aMouseEvent);
    rObjects.SelectEntry(u"Rectangle");
    rObjects.Select();
    rObjects.MouseReleaseHdl(aMouseEvent);
    Scheduler::ProcessEventsToIdle();
    // This failed, single-click did not result in a shape selection (only
    // double-click did).
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), rMarkList.GetMarkCount());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testTdf115873Group)
{
    // Initialize the navigator.
    SdXImpressDocument* pXImpressDocument = createDoc("tdf115873-group.fodp");
    SfxViewShell* pViewShell = SfxViewShell::Current();
    CPPUNIT_ASSERT(pViewShell);
    SfxBindings& rBindings = pViewShell->GetViewFrame().GetBindings();
    auto xNavigator = std::make_unique<SdNavigatorWin>(nullptr, &rBindings, nullptr);
    xNavigator->InitTreeLB(pXImpressDocument->GetDoc());
    SdPageObjsTLV& rObjects = xNavigator->GetObjects();
    // This failed, Fill() and IsEqualToDoc() were out of sync for group
    // shapes.
    CPPUNIT_ASSERT(rObjects.IsEqualToDoc(pXImpressDocument->GetDoc()));
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testCutSelectionChange)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("cut_selection_change.odp");
    CPPUNIT_ASSERT(pXImpressDocument);

    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    setupLibreOfficeKitViewCallback(pViewShell->GetViewShellBase());
    Scheduler::ProcessEventsToIdle();

    // Select first text object
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    Scheduler::ProcessEventsToIdle();

    // step into text editing
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, '1', 0);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, '1', 0);
    Scheduler::ProcessEventsToIdle();

    // select some text
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_LEFT | KEY_SHIFT);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_LEFT | KEY_SHIFT);
    Scheduler::ProcessEventsToIdle();

    // Check that we have a selection before cutting
    CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(1), m_aSelection.size());

    // Cut the selected text
    dispatchCommand(mxComponent, u".uno:Cut"_ustr, uno::Sequence<beans::PropertyValue>());

    // Selection is removed
    CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(0), m_aSelection.size());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testGetViewRenderState)
{
    // Add a pair of schemes, last added is the default
    svtools::EditableColorConfig aColorConfig;
    aColorConfig.AddScheme(u"Dark"_ustr);
    aColorConfig.AddScheme(u"Light"_ustr);

    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    int nFirstViewId = SfxLokHelper::getCurrentView();
    SdTestViewCallback aView1;
    CPPUNIT_ASSERT_EQUAL("S;Default"_ostr, pXImpressDocument->getViewRenderState());
    // Create a second view
    SfxLokHelper::createView();
    SdTestViewCallback aView2;
    CPPUNIT_ASSERT_EQUAL("S;Default"_ostr, pXImpressDocument->getViewRenderState());
    // Set to dark scheme
    {
        uno::Sequence<beans::PropertyValue> aPropertyValues = comphelper::InitPropertySequence(
            {
                { "NewTheme", uno::Any(u"Dark"_ustr) },
            }
        );
        dispatchCommand(mxComponent, u".uno:ChangeTheme"_ustr, aPropertyValues);
    }
    CPPUNIT_ASSERT_EQUAL("S;Dark"_ostr, pXImpressDocument->getViewRenderState());
    // Switch back to the first view, and check that the options string is the same
    SfxLokHelper::setView(nFirstViewId);
    CPPUNIT_ASSERT_EQUAL("S;Default"_ostr, pXImpressDocument->getViewRenderState());
}

// Helper function to get a tile to a bitmap and check the pixel color
static void assertTilePixelColor(SdXImpressDocument* pXImpressDocument, int nPixelX, int nPixelY, Color aColor)
{
    size_t nCanvasSize = 1024;
    size_t nTileSize = 256;
    std::vector<unsigned char> aPixmap(nCanvasSize * nCanvasSize * 4, 0);
    ScopedVclPtrInstance<VirtualDevice> pDevice(DeviceFormat::WITHOUT_ALPHA);
    pDevice->SetBackground(Wallpaper(COL_TRANSPARENT));
    pDevice->SetOutputSizePixelScaleOffsetAndLOKBuffer(Size(nCanvasSize, nCanvasSize),
            Fraction(1.0), Point(), aPixmap.data());
    pXImpressDocument->paintTile(*pDevice, nCanvasSize, nCanvasSize, 0, 0, 15360, 7680);
    pDevice->EnableMapMode(false);
    Bitmap aBitmap = pDevice->GetBitmap(Point(0, 0), Size(nTileSize, nTileSize));
    BitmapScopedReadAccess pAccess(aBitmap);
    Color aActualColor(pAccess->GetPixel(nPixelX, nPixelY));
    CPPUNIT_ASSERT_EQUAL(aColor, aActualColor);
}

// Test that changing the theme in one view doesn't change it in the other view
CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testThemeViewSeparation)
{
    Color aDarkColor(0x1c, 0x1c, 0x1c);
    // Add a minimal dark scheme
    {
        svtools::EditableColorConfig aColorConfig;
        svtools::ColorConfigValue aValue;
        aValue.bIsVisible = true;
        aValue.nColor = aDarkColor;
        aColorConfig.SetColorValue(svtools::DOCCOLOR, aValue);
        aColorConfig.AddScheme(u"Dark"_ustr);
    }
    // Add a minimal light scheme
    {
        svtools::EditableColorConfig aColorConfig;
        svtools::ColorConfigValue aValue;
        aValue.bIsVisible = true;
        aValue.nColor = COL_WHITE;
        aColorConfig.SetColorValue(svtools::DOCCOLOR, aValue);
        aColorConfig.AddScheme(u"Light"_ustr);
    }
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    int nFirstViewId = SfxLokHelper::getCurrentView();
    SdTestViewCallback aView1;
    // Switch first view to light scheme
    {
        uno::Sequence<beans::PropertyValue> aPropertyValues = comphelper::InitPropertySequence(
            {
                { "NewTheme", uno::Any(u"Light"_ustr) },
            }
        );
        dispatchCommand(mxComponent, u".uno:ChangeTheme"_ustr, aPropertyValues);
    }
    // First view is at light scheme
    assertTilePixelColor(pXImpressDocument, 255, 255, COL_WHITE);
    // Create second view
    SfxLokHelper::createView();
    int nSecondViewId = SfxLokHelper::getCurrentView();
    SdTestViewCallback aView2;
    // Set second view to dark scheme
    {
        uno::Sequence<beans::PropertyValue> aPropertyValues = comphelper::InitPropertySequence(
            {
                { "NewTheme", uno::Any(u"Dark"_ustr) },
            }
        );
        dispatchCommand(mxComponent, u".uno:ChangeTheme"_ustr, aPropertyValues);
    }
    assertTilePixelColor(pXImpressDocument, 255, 255, aDarkColor);
    // First view still in light scheme
    SfxLokHelper::setView(nFirstViewId);
    assertTilePixelColor(pXImpressDocument, 255, 255, COL_WHITE);
    // Second view still in dark scheme
    SfxLokHelper::setView(nSecondViewId);
    assertTilePixelColor(pXImpressDocument, 255, 255, Color(0x1c, 0x1c, 0x1c));
    // Switch second view back to light scheme
    {
        uno::Sequence<beans::PropertyValue> aPropertyValues = comphelper::InitPropertySequence(
            {
                { "NewTheme", uno::Any(u"Light"_ustr) },
            }
        );
        dispatchCommand(mxComponent, u".uno:ChangeTheme"_ustr, aPropertyValues);
    }
    // Now in light scheme
    assertTilePixelColor(pXImpressDocument, 255, 255, COL_WHITE);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testRegenerateDiagram)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("regenerate-diagram.pptx");
    CPPUNIT_ASSERT(pXImpressDocument);

    SdPage* pActualPage = pXImpressDocument->GetDocShell()->GetViewShell()->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(4), pActualPage->GetObj(0)->GetSubList()->GetObjCount());

    // For new Diagram functionality entering group using UI is not allowed as long
    // as the group shape is a diagram. Do the same as before done by triggering UI
    // events directly in the model
    // Remove and free top-left entry (Box showing "A")
    pActualPage->GetObj(0)->GetSubList()->RemoveObject(1);

    // select diagram
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, awt::Key::TAB);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, awt::Key::TAB);
    Scheduler::ProcessEventsToIdle();

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObj(0)->GetSubList()->GetObjCount());

    // regenerate diagram
    dispatchCommand(mxComponent, u".uno:RegenerateDiagram"_ustr, uno::Sequence<beans::PropertyValue>());

    // diagram content (child shape count) should be the same as in the beginning
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(4), pActualPage->GetObj(0)->GetSubList()->GetObjCount());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testInsertDeletePageInvalidation)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    SdTestViewCallback aView1;
    CPPUNIT_ASSERT_EQUAL(8, pXImpressDocument->getParts());

    // Insert slide
    aView1.m_bTilesInvalidated = false;
    aView1.m_aInvalidations.clear();
    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, uno::Sequence<beans::PropertyValue>());
    CPPUNIT_ASSERT(aView1.m_bTilesInvalidated);
    CPPUNIT_ASSERT_EQUAL(9, pXImpressDocument->getParts());
    CPPUNIT_ASSERT_EQUAL(size_t(9), aView1.m_aInvalidations.size());

    // Delete slide
    aView1.m_bTilesInvalidated = false;
    aView1.m_aInvalidations.clear();
    dispatchCommand(mxComponent, u".uno:DeletePage"_ustr, uno::Sequence<beans::PropertyValue>());
    CPPUNIT_ASSERT(aView1.m_bTilesInvalidated);
    CPPUNIT_ASSERT_EQUAL(8, pXImpressDocument->getParts());
    CPPUNIT_ASSERT_EQUAL(size_t(8), aView1.m_aInvalidations.size());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSpellOnlineRenderParameter)
{
    // Load the document.
    SdXImpressDocument* pXImpressDocument = createDoc("dummy.odp");
    bool bSet = pXImpressDocument->GetDoc()->GetOnlineSpell();

    uno::Sequence<beans::PropertyValue> aPropertyValues =
    {
        comphelper::InitPropertySequence({ { ".uno:SpellOnline", uno::Any(!bSet) } }),
    };
    pXImpressDocument->initializeForTiledRendering(aPropertyValues);
    CPPUNIT_ASSERT_EQUAL(!bSet, pXImpressDocument->GetDoc()->GetOnlineSpell());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideDuplicateUndo)
{
    // Create two views.
    SdXImpressDocument* pXImpressDocument = createDoc("duplicate-undo.odp");
    int nView0 = SfxLokHelper::getCurrentView();
    SfxLokHelper::createView();
    pXImpressDocument->initializeForTiledRendering({});
    int nView1 = SfxLokHelper::getCurrentView();
    SfxLokHelper::setView(nView0);

    // Switch to the 3rd slide on view 0, and start text editing.
    {
        pXImpressDocument->setPart(2);
        sd::ViewShell* pViewShell0 = pXImpressDocument->GetDocShell()->GetViewShell();
        SdrView* pView = pViewShell0->GetView();
        SdPage* pActualPage = pViewShell0->GetActualPage();
        SdrObject* pObject = pActualPage->GetObj(1);
        SdrTextObj* pTextObj = static_cast<SdrTextObj*>(pObject);
        pView->MarkObj(pTextObj, pView->GetSdrPageView());
        SfxStringItem aInputString(SID_ATTR_CHAR, u"x"_ustr);
        pViewShell0->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR,
                SfxCallMode::SYNCHRON, { &aInputString });
        CPPUNIT_ASSERT(pView->IsTextEdit());
        CPPUNIT_ASSERT(pView->GetTextEditPageView());
    }

    // Duplicate the first slide on view 1 and undo it.
    SfxLokHelper::setView(nView1);
    dispatchCommand(mxComponent, u".uno:DuplicatePage"_ustr, {});
    pXImpressDocument->setPart(0, /*bAllowChangeFocus=*/false);
    pXImpressDocument->setPart(1, /*bAllowChangeFocus=*/false);
    SfxLokHelper::setView(nView0);
    pXImpressDocument->setPart(0, /*bAllowChangeFocus=*/false);
    pXImpressDocument->setPart(3, /*bAllowChangeFocus=*/false);
    SfxLokHelper::setView(nView1);
    pXImpressDocument->getUndoManager()->undo();
    // Without the accompanying fix in place, this would have tried to access the outdated page view
    // pointer, potentially leading to a crash.
    pXImpressDocument->setPart(2, /*bAllowChangeFocus=*/false);

    // Make sure that view 0 now doesn't have an outdated page view pointer.
    SfxLokHelper::setView(nView0);
    sd::ViewShell* pViewShell0 = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pView0 = pViewShell0->GetView();
    CPPUNIT_ASSERT(!pView0->GetTextEditPageView());
}

namespace
{

void lcl_extractHandleParameters(std::string_view selection, sal_uInt32& id, sal_uInt32& x, sal_uInt32& y)
{
    OString extraInfo( selection.substr(selection.find("{")) );
    std::stringstream aStream((std::string(extraInfo)));
    boost::property_tree::ptree aTree;
    boost::property_tree::read_json(aStream, aTree);
    boost::property_tree::ptree
        handle0 = aTree
            .get_child("handles")
            .get_child("kinds")
            .get_child("rectangle")
            .get_child("1")
            .begin()->second;
    id = handle0.get_child("id").get_value<int>();
    x = handle0.get_child("point").get_child("x").get_value<int>();
    y = handle0.get_child("point").get_child("y").get_value<int>();

    CPPUNIT_ASSERT(aTree.get_child("OrdNum").get_value<int>() >= 0);
    CPPUNIT_ASSERT(aTree.get_child("ObjectRectangles").size() >= 1);
}

}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testMoveShapeHandle)
{
    SdXImpressDocument* pXImpressDocument = createDoc("shape.odp");
    SdTestViewCallback aView1;
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pPage = pViewShell->GetActualPage();
    SdrObject* pObject = pPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pObject, pView->GetSdrPageView());
    Scheduler::ProcessEventsToIdle();

    CPPUNIT_ASSERT(!aView1.m_ShapeSelection.isEmpty());
    {
        sal_uInt32 id, x, y;
        lcl_extractHandleParameters(aView1.m_ShapeSelection, id, x ,y);
        sal_uInt32 oldX = x;
        sal_uInt32 oldY = y;
        uno::Sequence<beans::PropertyValue> aPropertyValues(comphelper::InitPropertySequence(
        {
            {"HandleNum", uno::Any(id)},
            {"NewPosX", uno::Any(x+1)},
            {"NewPosY", uno::Any(y+1)}
        }));
        dispatchCommand(mxComponent, u".uno:MoveShapeHandle"_ustr, aPropertyValues);
        CPPUNIT_ASSERT(!aView1.m_ShapeSelection.isEmpty());
        lcl_extractHandleParameters(aView1.m_ShapeSelection, id, x ,y);
        CPPUNIT_ASSERT_EQUAL(x-1, oldX);
        CPPUNIT_ASSERT_EQUAL(y-1, oldY);
    }
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testPasteUndo)
{
    // Given a document with a textbox, containing "world":
    SdXImpressDocument* pXImpressDocument = createDoc("paste-undo.fodp");
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pObject, pView->GetSdrPageView());
    pView->SdrBeginTextEdit(pObject);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_HOME);
    pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_HOME);
    EditView& rEditView = pView->GetTextEditOutlinerView()->GetEditView();
    ESelection aWordSelection(0, 0, 0, 1); // "w" of "world"
    rEditView.SetSelection(aWordSelection);
    dispatchCommand(mxComponent, u".uno:Cut"_ustr, {});

    // When undoing a paste:
    dispatchCommand(mxComponent, u".uno:Paste"_ustr, {});
    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    // Then make sure the cursor position is still at the beginning:
    ESelection aSelection = rEditView.GetSelection();
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 0
    // - Actual  : 4
    // i.e. the cursor position after undo was at the end of the line, not at the start, as
    // expected.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), aSelection.start.nIndex);
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testShapeEditInMultipleViews)
{
    SdXImpressDocument* pXImpressDocument = createDoc("TextBoxAndRect.odg");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SdDrawDocument* pDocument = pXImpressDocument->GetDoc();

    // Create view 1
    const int nView1 = SfxLokHelper::getCurrentView();
    sd::ViewShell* pViewShell1 = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pView1 = pViewShell1->GetView();
    Scheduler::ProcessEventsToIdle();

    // Create view 2
    SfxLokHelper::createView();
    const int nView2 = SfxLokHelper::getCurrentView();
    CPPUNIT_ASSERT(nView1 != nView2);

    sd::ViewShell* pViewShell2 = pXImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pView2 = pViewShell2->GetView();
    Scheduler::ProcessEventsToIdle();

    // Switch to view 1
    SfxLokHelper::setView(nView1);

    SdPage* pPage1 = pViewShell1->GetActualPage();

    SdrObject* pTextBoxObject = pPage1->GetObj(0);
    CPPUNIT_ASSERT_EQUAL(u"Text Box"_ustr, pTextBoxObject->GetName());

    SdrObject* pRectangleObject = pPage1->GetObj(1);
    CPPUNIT_ASSERT_EQUAL(u"Rect"_ustr, pRectangleObject->GetName());

    SdrObject* pTableObject = pPage1->GetObj(2);
    CPPUNIT_ASSERT_EQUAL(u"Table1"_ustr, pTableObject->GetName());

    // Scenario 1
    // 2 shapes - "Text Box" and "Rect"
    // View1 - "Text Box" enters text edit mode, View 2 - moves the "Rect" around
    {
        sd::UndoManager* pUndoManager = pDocument->GetUndoManager();
        CPPUNIT_ASSERT_EQUAL(size_t(0), pUndoManager->GetUndoActionCount());

        pView1->SdrBeginTextEdit(pTextBoxObject);
        CPPUNIT_ASSERT_EQUAL(true, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());

        // Local undo count for View1 is 0
        CPPUNIT_ASSERT_EQUAL(size_t(0), pView1->getViewLocalUndoManager()->GetUndoActionCount());
        // Write 'test' in View1
        SfxStringItem aInputString(SID_ATTR_CHAR, u"test"_ustr);
        pViewShell1->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR, SfxCallMode::SYNCHRON, { &aInputString });
        // Local undo count for View1 is now 1
        CPPUNIT_ASSERT_EQUAL(size_t(1), pView1->getViewLocalUndoManager()->GetUndoActionCount());

        // Mark rectangle object
        pView2->MarkObj(pRectangleObject, pView2->GetSdrPageView());

        // Check the initial position of the object
        tools::Rectangle aRectangle = pRectangleObject->GetLogicRect();
        CPPUNIT_ASSERT_EQUAL(6250L, aRectangle.TopLeft().X());
        CPPUNIT_ASSERT_EQUAL(7000L, aRectangle.TopLeft().Y());
        CPPUNIT_ASSERT_EQUAL(6501L, aRectangle.GetWidth());
        CPPUNIT_ASSERT_EQUAL(4501L, aRectangle.GetHeight());

        // On View2 - Move handle 0 on the shape to a new position - resize
        Point aNewPosition = aRectangle.TopLeft() + Point(-1250, -1000);
        pView2->MoveShapeHandle(0, aNewPosition, -1);
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT_EQUAL(size_t(1), pUndoManager->GetUndoActionCount());

        // Check the object has a new size
        aRectangle = pRectangleObject->GetLogicRect();
        CPPUNIT_ASSERT_EQUAL(5000L, aRectangle.TopLeft().X());
        CPPUNIT_ASSERT_EQUAL(6000L, aRectangle.TopLeft().Y());
        CPPUNIT_ASSERT_EQUAL(7751L, aRectangle.GetWidth());
        CPPUNIT_ASSERT_EQUAL(5501L, aRectangle.GetHeight());

        // View1 is still in text edit mode...
        CPPUNIT_ASSERT_EQUAL(true, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());

        // On View2 - relative move the shape to a different position
        pView2->MoveMarkedObj(Size(1000, 2000), /*bCopy=*/false);
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT_EQUAL(size_t(2), pUndoManager->GetUndoActionCount());

        // Check the object is at a different position
        aRectangle = pRectangleObject->GetLogicRect();
        CPPUNIT_ASSERT_EQUAL(6000L, aRectangle.TopLeft().X());
        CPPUNIT_ASSERT_EQUAL(8000L, aRectangle.TopLeft().Y());
        CPPUNIT_ASSERT_EQUAL(7751L, aRectangle.GetWidth());
        CPPUNIT_ASSERT_EQUAL(5501L, aRectangle.GetHeight());

        // View1 is still in text edit mode...
        CPPUNIT_ASSERT_EQUAL(true, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());

        // End Text edit - check undo count increase from 2 -> 3
        CPPUNIT_ASSERT_EQUAL(size_t(2), pUndoManager->GetUndoActionCount());
        pView1->SdrEndTextEdit();
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT_EQUAL(size_t(3), pUndoManager->GetUndoActionCount());

        // Check that both views exited the text edit mode
        CPPUNIT_ASSERT_EQUAL(false, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());
    }

    // Scenario 2
    // 1 shapes - "Text Box"
    // View1 - "Text Box" enters text edit mode, View 2 - moves the "Text Box" around
    {
        sd::UndoManager* pUndoManager = pDocument->GetUndoManager();
        CPPUNIT_ASSERT_EQUAL(size_t(3), pUndoManager->GetUndoActionCount());

        pView1->SdrBeginTextEdit(pTextBoxObject);
        CPPUNIT_ASSERT_EQUAL(true, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());

        // Local undo count for View1 is 0
        CPPUNIT_ASSERT_EQUAL(size_t(0), pView1->getViewLocalUndoManager()->GetUndoActionCount());
        // Write 'test' in View1
        SfxStringItem aInputString(SID_ATTR_CHAR, u"test"_ustr);
        pViewShell1->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR, SfxCallMode::SYNCHRON, { &aInputString });
        // Local undo count for View1 is now 1
        CPPUNIT_ASSERT_EQUAL(size_t(1), pView1->getViewLocalUndoManager()->GetUndoActionCount());

        // Mark rectangle object
        pView2->MarkObj(pTextBoxObject, pView2->GetSdrPageView());

        // Check the initial position of the object
        tools::Rectangle aRectangle = pTextBoxObject->GetLogicRect();
        CPPUNIT_ASSERT_EQUAL(2250L, aRectangle.TopLeft().X());
        CPPUNIT_ASSERT_EQUAL(2000L, aRectangle.TopLeft().Y());
        CPPUNIT_ASSERT_EQUAL(4501L, aRectangle.GetWidth());
        CPPUNIT_ASSERT_EQUAL(2001L, aRectangle.GetHeight());

        // On View2 - Move handle 0 on the shape to a new position - resize
        Point aNewPosition = aRectangle.TopLeft() + Point(-1250, -1000);
        pView2->MoveShapeHandle(0, aNewPosition, -1);
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT_EQUAL(size_t(4), pUndoManager->GetUndoActionCount());

        // Check the object has a new size
        aRectangle = pTextBoxObject->GetLogicRect();
        CPPUNIT_ASSERT_EQUAL(1000L, aRectangle.TopLeft().X());
        CPPUNIT_ASSERT_EQUAL(1000L, aRectangle.TopLeft().Y());
        CPPUNIT_ASSERT_EQUAL(4990L, aRectangle.GetWidth());
        CPPUNIT_ASSERT_EQUAL(2175L, aRectangle.GetHeight());

        // View1 is still in text edit mode...
        CPPUNIT_ASSERT_EQUAL(true, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());

        // On View2 - relative move the shape to a different position
        pView2->MoveMarkedObj(Size(1000, 2000), /*bCopy=*/false);
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT_EQUAL(size_t(5), pUndoManager->GetUndoActionCount());

        // Check the object is at a different position
        aRectangle = pTextBoxObject->GetLogicRect();
        CPPUNIT_ASSERT_EQUAL(2000L, aRectangle.TopLeft().X());
        CPPUNIT_ASSERT_EQUAL(3000L, aRectangle.TopLeft().Y());
        CPPUNIT_ASSERT_EQUAL(4990L, aRectangle.GetWidth());
        CPPUNIT_ASSERT_EQUAL(2175L, aRectangle.GetHeight());

        // View1 is still in text edit mode...
        CPPUNIT_ASSERT_EQUAL(true, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());

        // End Text edit - check undo count increase from 5 -> 6
        CPPUNIT_ASSERT_EQUAL(size_t(5), pUndoManager->GetUndoActionCount());
        pView1->SdrEndTextEdit();
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT_EQUAL(size_t(6), pUndoManager->GetUndoActionCount());

        // Check that both views exited the text edit mode
        CPPUNIT_ASSERT_EQUAL(false, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());
    }

    // Scenario 3
    // 1 shapes - "Table1"
    // View1 - "Table1" enters text edit mode, View 2 - moves the "Table1" around
    {
        sd::UndoManager* pUndoManager = pDocument->GetUndoManager();
        CPPUNIT_ASSERT_EQUAL(size_t(6), pUndoManager->GetUndoActionCount());

        pView1->SdrBeginTextEdit(pTableObject);
        CPPUNIT_ASSERT_EQUAL(true, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());

        // Local undo count for View1 is 0
        CPPUNIT_ASSERT_EQUAL(size_t(0), pView1->getViewLocalUndoManager()->GetUndoActionCount());
        // Write 'test' in View1
        SfxStringItem aInputString(SID_ATTR_CHAR, u"test"_ustr);
        pViewShell1->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR, SfxCallMode::SYNCHRON, { &aInputString });
        // Local undo count for View1 is now 1
        CPPUNIT_ASSERT_EQUAL(size_t(1), pView1->getViewLocalUndoManager()->GetUndoActionCount());

        // Mark rectangle object
        pView2->MarkObj(pTableObject, pView2->GetSdrPageView());

        // Check the initial position of the table
        tools::Rectangle aRectangle = pTableObject->GetLogicRect();
        CPPUNIT_ASSERT_EQUAL(2919L, aRectangle.TopLeft().X());
        CPPUNIT_ASSERT_EQUAL(18063L, aRectangle.TopLeft().Y());
        CPPUNIT_ASSERT_EQUAL(14099L, aRectangle.GetWidth());
        CPPUNIT_ASSERT_EQUAL(5999L, aRectangle.GetHeight());

        // On View2 - relative move the shape to a different position
        pView2->MoveMarkedObj(Size(1000, 2000), /*bCopy=*/false);
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT_EQUAL(size_t(7), pUndoManager->GetUndoActionCount());

        // Check the object is at a different position
        aRectangle = pTableObject->GetLogicRect();
        CPPUNIT_ASSERT_EQUAL(3919L, aRectangle.TopLeft().X());
        CPPUNIT_ASSERT_EQUAL(20063L, aRectangle.TopLeft().Y());
        CPPUNIT_ASSERT_EQUAL(14099L, aRectangle.GetWidth());
        CPPUNIT_ASSERT_EQUAL(5999L, aRectangle.GetHeight());

        // View1 is still in text edit mode...
        CPPUNIT_ASSERT_EQUAL(true, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());

        // End Text edit - check undo count increase from 7 -> 8
        CPPUNIT_ASSERT_EQUAL(size_t(7), pUndoManager->GetUndoActionCount());
        pView1->SdrEndTextEdit();
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT_EQUAL(size_t(8), pUndoManager->GetUndoActionCount());

        // Check that both views exited the text edit mode
        CPPUNIT_ASSERT_EQUAL(false, pView1->IsTextEdit());
        CPPUNIT_ASSERT_EQUAL(false, pView2->IsTextEdit());
    }
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSidebarHide)
{
    // Given an impress document, with a visible sidebar:
    createDoc("dummy.odp");
    SdTestViewCallback aView;
    sfx2::sidebar::Sidebar::Setup(u"");
    Scheduler::ProcessEventsToIdle();
    aView.m_aStateChanges.clear();

    // When hiding the slide layout deck:
    dispatchCommand(mxComponent, u".uno:ModifyPage"_ustr, {});

    // Then make sure we get a state change for this, in JSON format:
    auto it = aView.m_aStateChanges.find(".uno:ModifyPage");
    // Without the accompanying fix in place, this test would have failed, we got the state change
    // in plain text, which was inconsistent (show was JSON, hide was plain text).
    CPPUNIT_ASSERT(it != aView.m_aStateChanges.end());
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testStartPresentation)
{
    SdXImpressDocument* pXImpressDocument = createDoc("test.ppsx");
    SdTestViewCallback aView;
    CPPUNIT_ASSERT(pXImpressDocument->GetDoc()->GetStartWithPresentation());
    Scheduler::ProcessEventsToIdle();

    const auto it = aView.m_aStateChanges.find(".uno:StartWithPresentation");
    CPPUNIT_ASSERT(it != aView.m_aStateChanges.end());

    const auto value = it->second;
    CPPUNIT_ASSERT(value.get_child_optional("state").has_value());
    CPPUNIT_ASSERT_EQUAL(std::string("true"), value.get_child("state").get_value<std::string>());
}

namespace
{

void debugWriteImageToFile(unsigned step, const std::vector<sal_uInt8>& pBuffer,
    unsigned nViewWidth, unsigned nViewHeight, const char* sJSON)
{
    (void)step;
    (void)pBuffer;
    (void)nViewWidth;
    (void)nViewHeight;
    (void)sJSON;

#if false
    printf ("%u %s\n\n", step, sJSON);

    BitmapEx aBitmapEx = vcl::bitmap::CreateFromData(pBuffer.data(), nViewWidth, nViewHeight, nViewWidth * 4, /*nBitsPerPixel*/32, true, true);

    OUString sFileName = "/tmp/slideshow_" + OUString::number(step) + ".png";
    SvFileStream aStream(sFileName, StreamMode::WRITE | StreamMode::TRUNC);
    vcl::PngImageWriter aPNGWriter(aStream);
    aPNGWriter.write(aBitmapEx);
#endif
}

template <typename T>
bool is_unique(std::vector<T> vec)
{
    std::sort(vec.begin(), vec.end());
    return std::unique(vec.begin(), vec.end()) == vec.end();
}

boost::property_tree::ptree const& child_at(boost::property_tree::ptree const& rTree, std::string_view aName, size_t nIndex)
{
    return std::next(rTree.get_child(std::string(aName)).find(""), nIndex)->second;
}

bool has_child(boost::property_tree::ptree const& rTree, std::string_view aName)
{
    return rTree.count(std::string(aName)) > 0;
}

void readJSON(boost::property_tree::ptree& rTree, OUString const& rJson)
{
    std::stringstream aStream((std::string(rJson.toUtf8())));
    boost::property_tree::read_json(aStream, rTree);
}

} // end anonymous ns

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testPresentationInfo)
{
    SdXImpressDocument* pXImpressDocument = createDoc("PresentationInfoTest.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());

    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);

    Scheduler::ProcessEventsToIdle();

    SdPage* pPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT(pPage);

    OString aString = pXImpressDocument->getPresentationInfo();

    boost::property_tree::ptree aTree;
    std::stringstream aStream((std::string(aString)));
    boost::property_tree::read_json(aStream, aTree);

    CPPUNIT_ASSERT_EQUAL(15875, aTree.get_child("docWidth").get_value<int>());
    CPPUNIT_ASSERT_EQUAL(8930,  aTree.get_child("docHeight").get_value<int>());

    CPPUNIT_ASSERT_EQUAL(size_t(5),  aTree.get_child("slides").size());

    // Slide Index 0
    {
        const boost::property_tree::ptree& rChild = child_at(aTree, "slides", 0);
        CPPUNIT_ASSERT_EQUAL(0, rChild.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(false, rChild.get_child("empty").get_value<bool>());
        // Check only that these exist
        CPPUNIT_ASSERT(has_child(rChild, "hash"));
        CPPUNIT_ASSERT(has_child(rChild, "masterPage"));
        CPPUNIT_ASSERT(has_child(rChild, "masterPageObjectsVisibility"));

        // Transitions
        CPPUNIT_ASSERT_EQUAL(std::string("BarWipe"), rChild.get_child("transitionType").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("TopToBottom"), rChild.get_child("transitionSubtype").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(false, rChild.get_child("transitionDirection").get_value<bool>());
    }

    // Slide Index 1
    {
        const boost::property_tree::ptree& rChild = child_at(aTree, "slides", 1);
        CPPUNIT_ASSERT_EQUAL(1, rChild.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(false, rChild.get_child("empty").get_value<bool>());
        // Check only that these exist
        CPPUNIT_ASSERT(has_child(rChild, "hash"));
        CPPUNIT_ASSERT(has_child(rChild, "masterPage"));
        CPPUNIT_ASSERT(has_child(rChild, "masterPageObjectsVisibility"));

        // Transitions
        CPPUNIT_ASSERT_EQUAL(std::string("FanWipe"), rChild.get_child("transitionType").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("CenterTop"), rChild.get_child("transitionSubtype").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, rChild.get_child("transitionDirection").get_value<bool>());

    }

    // Slide Index 2
    {
        const boost::property_tree::ptree& rChild = child_at(aTree, "slides", 2);
        CPPUNIT_ASSERT_EQUAL(2, rChild.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(false, rChild.get_child("empty").get_value<bool>());
        // Check only that these exist
        CPPUNIT_ASSERT(has_child(rChild, "hash"));
        CPPUNIT_ASSERT(has_child(rChild, "masterPage"));
        CPPUNIT_ASSERT(has_child(rChild, "masterPageObjectsVisibility"));

        // Transitions
        CPPUNIT_ASSERT_EQUAL(std::string("BlindWipe"), rChild.get_child("transitionType").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("Vertical"), rChild.get_child("transitionSubtype").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, rChild.get_child("transitionDirection").get_value<bool>());
    }

    // Slide Index 3 - Hidden
    {
        const boost::property_tree::ptree& rChild = child_at(aTree, "slides", 3);
        CPPUNIT_ASSERT_EQUAL(3, rChild.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(true, rChild.get_child("hidden").get_value<bool>());
    }

    // Slide Index 4
    {
        const boost::property_tree::ptree& rChild = child_at(aTree, "slides", 4);
        CPPUNIT_ASSERT_EQUAL(4, rChild.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(false, rChild.get_child("empty").get_value<bool>());
        // Check only that these exist
        CPPUNIT_ASSERT(has_child(rChild, "hash"));
        CPPUNIT_ASSERT(has_child(rChild, "masterPage"));
        CPPUNIT_ASSERT(has_child(rChild, "masterPageObjectsVisibility"));

        // Transitions
        CPPUNIT_ASSERT_EQUAL(std::string("Fade"), rChild.get_child("transitionType").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("FadeOverColor"), rChild.get_child("transitionSubtype").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, rChild.get_child("transitionDirection").get_value<bool>());
    }
}

namespace
{
class SlideRendererChecker
{
private:
    SdXImpressDocument* mpXImpressDocument;
    sal_Int32 mnViewWidth;
    sal_Int32 mnViewHeight;
    sal_Int32 mnSlideNumber;
    int mnImageFileStep;

public:
    SlideRendererChecker(SdXImpressDocument* pXImpressDocument,
                         sal_Int32 nSlideNumber, sal_Int32 nViewWidth, sal_Int32 nViewHeight,
                         bool bRenderBackground = true, bool bRenderMasterPage = true)
        : mpXImpressDocument(pXImpressDocument)
        , mnViewWidth(nViewWidth)
        , mnViewHeight(nViewHeight)
        , mnSlideNumber(nSlideNumber)
        , mnImageFileStep(0)
    {
        CPPUNIT_ASSERT(mpXImpressDocument);
        SdDrawDocument* pDoc = mpXImpressDocument->GetDoc();
        CPPUNIT_ASSERT(pDoc);
        SdPage* pPage = pDoc->GetSdPage(sal_uInt16(mnSlideNumber), PageKind::Standard);
        CPPUNIT_ASSERT(pPage);

        std::string sHash = GetInterfaceHash(GetXDrawPageForSdrPage(pPage));
        CPPUNIT_ASSERT(mpXImpressDocument->createSlideRenderer(
            sHash.c_str(), mnSlideNumber, mnViewWidth, mnViewHeight, bRenderBackground,
            bRenderMasterPage));
    }

    void checkSlideSize(sal_Int32 nViewWidth, sal_Int32 nViewHeight) const
    {
        CPPUNIT_ASSERT_EQUAL(nViewWidth, mnViewWidth);
        CPPUNIT_ASSERT_EQUAL(nViewHeight, mnViewHeight);
    }

    void checkBackgroundLayer()
    {
        std::vector<sal_uInt8> pBuffer(mnViewWidth * mnViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !mpXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        debugWriteImageToFile(mnImageFileStep++, pBuffer, mnViewWidth, mnViewHeight,
                              aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("Background"),
                             aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"),
                             aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
    }

    void checkTextFieldLayer(int nIndex, const std::string& rTextFieldType)
    {
        const std::string sMsg
            = "TextField Layer Type: " + rTextFieldType + " Index: " + std::to_string(nIndex);

        std::vector<sal_uInt8> pBuffer(mnViewWidth * mnViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT_MESSAGE(
            sMsg, !mpXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT_MESSAGE(sMsg, bIsBitmapLayer);

        debugWriteImageToFile(mnImageFileStep++, pBuffer, mnViewWidth, mnViewHeight,
                              aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, std::string("TextFields"),
                                     aTree.get_child("group").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, std::string("TextFields"),
                                     aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, nIndex, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, true, has_child(aTree, "content"));
        {
            auto aContentChild = aTree.get_child("content");
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, rTextFieldType,
                                         aContentChild.get_child("type").get_value<std::string>());
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, true, has_child(aContentChild, "content"));
            auto aContentChildChild = aContentChild.get_child("content");
            CPPUNIT_ASSERT_EQUAL_MESSAGE(
                sMsg, std::string("%IMAGETYPE%"),
                aContentChildChild.get_child("type").get_value<std::string>());
            CPPUNIT_ASSERT_EQUAL_MESSAGE(
                sMsg, std::string("%IMAGECHECKSUM%"),
                aContentChildChild.get_child("checksum").get_value<std::string>());
        }
    }

    void checkPlaceholderLayer(int nIndex, const std::string& rTextFieldType)
    {
        const std::string sMsg
            = "Placeholder Layer Type: " + rTextFieldType + " Index: " + std::to_string(nIndex);

        std::vector<sal_uInt8> pBuffer(mnViewWidth * mnViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT_MESSAGE(
            sMsg, !mpXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT_MESSAGE(sMsg, !bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, std::string("MasterPage"),
                                     aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, nIndex, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, std::string("placeholder"),
                                     aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, true, has_child(aTree, "content"));
        {
            auto aContentChild = aTree.get_child("content");
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, rTextFieldType,
                                         aContentChild.get_child("type").get_value<std::string>());
        }
    }

    void checkPageLayer(int nIndex, const std::string& rGroup, bool bIsAnimated = false)
    {
        const std::string sMsg = rGroup + " Layer Index: " + std::to_string(nIndex);

        std::vector<sal_uInt8> pBuffer(mnViewWidth * mnViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT_MESSAGE(
            sMsg, !mpXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT_MESSAGE(sMsg, bIsBitmapLayer);

        debugWriteImageToFile(mnImageFileStep++, pBuffer, mnViewWidth, mnViewHeight,
                              aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, rGroup,
                                     aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, nIndex, aTree.get_child("index").get_value<int>());

        if (!bIsAnimated)
        {
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, std::string("bitmap"),
                                         aTree.get_child("type").get_value<std::string>());
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, true, has_child(aTree, "content"));
        }
        else
        {
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, std::string("animated"),
                                         aTree.get_child("type").get_value<std::string>());
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, true, has_child(aTree, "content"));

            auto aContentChild = aTree.get_child("content");
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, true, has_child(aContentChild, "hash"));
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, true, has_child(aContentChild, "initVisible"));
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, std::string("bitmap"),
                                         aContentChild.get_child("type").get_value<std::string>());
            CPPUNIT_ASSERT_EQUAL_MESSAGE(sMsg, true, has_child(aContentChild, "content"));

            auto aContentChildChild = aContentChild.get_child("content");
            CPPUNIT_ASSERT_EQUAL_MESSAGE(
                sMsg, std::string("%IMAGETYPE%"),
                aContentChildChild.get_child("type").get_value<std::string>());
            CPPUNIT_ASSERT_EQUAL_MESSAGE(
                sMsg, std::string("%IMAGECHECKSUM%"),
                aContentChildChild.get_child("checksum").get_value<std::string>());
        }
    }

    void checkFinalEmptyLayer()
    {
        std::vector<sal_uInt8> pBuffer(mnViewWidth * mnViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            mpXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(aJson.isEmpty());
    }
};
} // end anonymous ns

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering)
{
    // Check rendering of slideshow layers (as in the document):
    // - master slide layer
    // - main slide layer

    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);
    SdPage* pPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT(pPage);
    std::string sHash = GetInterfaceHash(GetXDrawPageForSdrPage(pPage));
    sal_Int32 nViewWidth = 2000;
    sal_Int32 nViewHeight = 2000;
    CPPUNIT_ASSERT(pXImpressDocument->createSlideRenderer(sHash.c_str(), 0, nViewWidth, nViewHeight, true, true));
    CPPUNIT_ASSERT_EQUAL(2000, nViewWidth);
    CPPUNIT_ASSERT_EQUAL(1125, nViewHeight);

    const Color aTransparentColor(ColorAlpha, 0x00000000);
    const tools::Long nLeft = 20;
    const tools::Long nRight = nViewWidth - 20;
    const tools::Long nTop = 20;
    const tools::Long nBottom = nViewHeight - 20;

    // Background Layer - TODO
    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(0, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("Background"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);
        // TODO - check JSON content

        Bitmap aBitmapEx = vcl::bitmap::CreateFromData(pBuffer.data(), nViewWidth, nViewHeight, nViewWidth * 4, /*nBitsPerPixel*/32, true, true);
        debugWriteImageToFile(1, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        // top-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nTop));

        // bottom-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nBottom));

        // bottom-right corner
        CPPUNIT_ASSERT_EQUAL(Color(0xff, 0xd0, 0x40), aBitmapEx.GetPixelColor(nRight, nBottom));

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);
        // TODO - check JSON content
        debugWriteImageToFile(2, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        Bitmap aBitmapEx = vcl::bitmap::CreateFromData(pBuffer.data(), nViewWidth, nViewHeight, nViewWidth * 4, /*nBitsPerPixel*/32, true, true);

        // top-left corner
        CPPUNIT_ASSERT_EQUAL(Color(0x00, 0x50, 0x90), aBitmapEx.GetPixelColor(nLeft, nTop));

        // bottom-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nBottom));

        // bottom-right corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nRight, nBottom));

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(aJson.isEmpty());
    }

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_WithFields)
{
    // Check rendering of fields - each in own layer

    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_WithFields.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);
    SdPage* pPage = pViewShell->GetActualPage();

    CPPUNIT_ASSERT(pPage);
    std::string sHash = GetInterfaceHash(GetXDrawPageForSdrPage(pPage));
    sal_Int32 nViewWidth = 2000;
    sal_Int32 nViewHeight = 2000;
    CPPUNIT_ASSERT(pXImpressDocument->createSlideRenderer(sHash.c_str(), 0, nViewWidth, nViewHeight, true, true));
    CPPUNIT_ASSERT_EQUAL(2000, nViewWidth);
    CPPUNIT_ASSERT_EQUAL(1125, nViewHeight);

    const Color aTransparentColor(ColorAlpha, 0x00000000);
    const tools::Long nLeft = 20;
    const tools::Long nRight = nViewWidth - 20;
    const tools::Long nTop = 20;
    const tools::Long nBottom = nViewHeight - 20;
    const tools::Long nCenterX = nViewWidth / 2;

    // Background Layer
    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        debugWriteImageToFile(0, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("Background"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(2, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        Bitmap aBitmapEx = vcl::bitmap::CreateFromData(pBuffer.data(), nViewWidth, nViewHeight, nViewWidth * 4, /*nBitsPerPixel*/32, true, true);

        // top-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nTop));

        // bottom-left corner
        CPPUNIT_ASSERT_EQUAL(Color(0x90, 0x80, 0xff), aBitmapEx.GetPixelColor(nLeft, nBottom));

        // bottom-right corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nRight, nBottom));

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("TextFields"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);
        // TODO - check JSON content
        debugWriteImageToFile(3, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        Bitmap aBitmapEx = vcl::bitmap::CreateFromData(pBuffer.data(), nViewWidth, nViewHeight, nViewWidth * 4, /*nBitsPerPixel*/32, true, true);

        // top-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nTop));

        // bottom-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nBottom));

        // bottom-right corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nRight, nBottom));

        // bottom-center
        CPPUNIT_ASSERT_EQUAL(Color(0x20, 0xaa, 0x00), aBitmapEx.GetPixelColor(nCenterX, nBottom));

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("TextFields"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(4, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        Bitmap aBitmapEx = vcl::bitmap::CreateFromData(pBuffer.data(), nViewWidth, nViewHeight, nViewWidth * 4, /*nBitsPerPixel*/32, true, true);

        // top-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nTop));

        // bottom-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nBottom));

        // bottom-right corner
        CPPUNIT_ASSERT_EQUAL(Color(0xff, 0xd0, 0x40), aBitmapEx.GetPixelColor(nRight, nBottom));

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(!bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("placeholder"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(!bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(2, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("placeholder"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(5, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        Bitmap aBitmapEx = vcl::bitmap::CreateFromData(pBuffer.data(), nViewWidth, nViewHeight, nViewWidth * 4, /*nBitsPerPixel*/32, true, true);

        // top-left corner
        CPPUNIT_ASSERT_EQUAL(Color(0x00, 0x50, 0x90), aBitmapEx.GetPixelColor(nLeft, nTop));

        // bottom-left corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nLeft, nBottom));

        // bottom-right corner
        CPPUNIT_ASSERT_EQUAL(aTransparentColor, aBitmapEx.GetPixelColor(nRight, nBottom));

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(aJson.isEmpty());
    }

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_SlideNumber_Header_DateTime)
{
    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_SlideNumber_Header_DateTime.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SlideRendererChecker aSlideRendererChecker(pXImpressDocument, 0, 2000, 2000);
    aSlideRendererChecker.checkSlideSize(2000, 1125);

    aSlideRendererChecker.checkBackgroundLayer();

    aSlideRendererChecker.checkTextFieldLayer(0, "SlideNumber");
    aSlideRendererChecker.checkTextFieldLayer(1, "Footer");
    aSlideRendererChecker.checkTextFieldLayer(2, "DateTime");

    aSlideRendererChecker.checkPlaceholderLayer(0, "DateTime");
    aSlideRendererChecker.checkPlaceholderLayer(1, "Footer");
    aSlideRendererChecker.checkPlaceholderLayer(2, "SlideNumber");

    aSlideRendererChecker.checkPageLayer(0, "DrawPage");

    aSlideRendererChecker.checkFinalEmptyLayer();

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Skip_Background)
{
    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_SlideNumber_Header_DateTime.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SlideRendererChecker aSlideRendererChecker(pXImpressDocument, 0, 2000, 2000, false, true);
    aSlideRendererChecker.checkSlideSize(2000, 1125);

    aSlideRendererChecker.checkTextFieldLayer(0, "SlideNumber");
    aSlideRendererChecker.checkTextFieldLayer(1, "Footer");
    aSlideRendererChecker.checkTextFieldLayer(2, "DateTime");

    aSlideRendererChecker.checkPlaceholderLayer(0, "DateTime");
    aSlideRendererChecker.checkPlaceholderLayer(1, "Footer");
    aSlideRendererChecker.checkPlaceholderLayer(2, "SlideNumber");

    aSlideRendererChecker.checkPageLayer(0, "DrawPage");

    aSlideRendererChecker.checkFinalEmptyLayer();

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Skip_MasterPage)
{
    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_SlideNumber_Header_DateTime.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SlideRendererChecker aSlideRendererChecker(pXImpressDocument, 0, 2000, 2000, true, false);
    aSlideRendererChecker.checkSlideSize(2000, 1125);

    aSlideRendererChecker.checkBackgroundLayer();

    aSlideRendererChecker.checkTextFieldLayer(0, "SlideNumber");
    aSlideRendererChecker.checkTextFieldLayer(1, "Footer");
    aSlideRendererChecker.checkTextFieldLayer(2, "DateTime");

    aSlideRendererChecker.checkPageLayer(0, "DrawPage");

    aSlideRendererChecker.checkFinalEmptyLayer();

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Skip_Background_And_MasterPage)
{
    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_SlideNumber_Header_DateTime.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SlideRendererChecker aSlideRendererChecker(pXImpressDocument, 0, 2000, 2000, false, false);
    aSlideRendererChecker.checkSlideSize(2000, 1125);

    aSlideRendererChecker.checkTextFieldLayer(0, "SlideNumber");
    aSlideRendererChecker.checkTextFieldLayer(1, "Footer");
    aSlideRendererChecker.checkTextFieldLayer(2, "DateTime");

    aSlideRendererChecker.checkPageLayer(0, "DrawPage");

    aSlideRendererChecker.checkFinalEmptyLayer();

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animated_Shape_Inside_A_Group)
{
    // 1 not animated groups made up by 2 shapes
    // one of the 2 shapes is animated

    SdXImpressDocument* pXImpressDocument
        = createDoc("SlideRenderingTest_Animated_Shape_Inside_A_Group.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SlideRendererChecker aSlideRendererChecker(pXImpressDocument, 0, 2000, 2000, false, false);
    aSlideRendererChecker.checkSlideSize(2000, 1125);

    // not animated group, no layer should be created for the animated shape
    aSlideRendererChecker.checkPageLayer(0, "DrawPage", /*bIsAnimated=*/ false);

    aSlideRendererChecker.checkFinalEmptyLayer();

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animated_Group)
{
    // 1 animated groups made up by 2 not animated shapes
    // a single not animated shape

    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_Animated_Group.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SlideRendererChecker aSlideRendererChecker(pXImpressDocument, 0, 2000, 2000, false, false);
    aSlideRendererChecker.checkSlideSize(2000, 1125);

    // animated group
    aSlideRendererChecker.checkPageLayer(0, "DrawPage", /*bIsAnimated=*/ true);

    // not animated shape
    aSlideRendererChecker.checkPageLayer(1, "DrawPage", /*bIsAnimated=*/ false);

    aSlideRendererChecker.checkFinalEmptyLayer();

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animated_Groups)
{
    // 2 animated groups made up by 2 shapes each

    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_Animated_Groups.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SlideRendererChecker aSlideRendererChecker(pXImpressDocument, 0, 2000, 2000, false, false);
    aSlideRendererChecker.checkSlideSize(2000, 1125);

    // 1st group
    aSlideRendererChecker.checkPageLayer(0, "DrawPage", /*bIsAnimated=*/ true);

    // 2nd group
    aSlideRendererChecker.checkPageLayer(1, "DrawPage", /*bIsAnimated=*/ true);

    aSlideRendererChecker.checkFinalEmptyLayer();

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animated_MultiLevel_Group)
{
    // 3 1st level groups made up by 2 shapes each
    // the 1st group is not animated but one of its shape is
    // the 2nd group is animated, none of its shapes is
    // the 3rd group is animated with a color based effect
    // 1st and 2nd group are grouped together and the 2nd level group is animated

    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_Animated_MultiLevel_Group.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    SlideRendererChecker aSlideRendererChecker(pXImpressDocument, 0, 2000, 2000, false, false);
    aSlideRendererChecker.checkSlideSize(2000, 1125);

    // a single layer should be created for the highest level group,
    // embedded animated groups or animated shapes should be ignored
    aSlideRendererChecker.checkPageLayer(0, "DrawPage", /*bIsAnimated=*/ true);

    // a group with applied an effect based on color animations should not be animated
    aSlideRendererChecker.checkPageLayer(1, "DrawPage", /*bIsAnimated=*/ false);

    aSlideRendererChecker.checkFinalEmptyLayer();

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animations)
{
    // Check rendering of animated objects - each in own layer

    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_Animations.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);
    SdPage* pPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT(pPage);
    std::string sHash = GetInterfaceHash(GetXDrawPageForSdrPage(pPage));
    sal_Int32 nViewWidth = 2000;
    sal_Int32 nViewHeight = 2000;
    CPPUNIT_ASSERT(pXImpressDocument->createSlideRenderer(sHash.c_str(), 0, nViewWidth, nViewHeight, true, true));
    CPPUNIT_ASSERT_EQUAL(2000, nViewWidth);
    CPPUNIT_ASSERT_EQUAL(1125, nViewHeight);

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);
        debugWriteImageToFile(0, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("Background"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "initVisible"));
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(!bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("placeholder"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("DateTime"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(!bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("placeholder"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("Footer"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(!bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(2, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("placeholder"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("SlideNumber"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);
        debugWriteImageToFile(1, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("true"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);
        debugWriteImageToFile(2, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("false"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(aJson.isEmpty());
    }

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animations2)
{
    SdXImpressDocument* pXImpressDocument = createDoc("anim.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);
    SdPage* pPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT(pPage);
    sal_Int32 nViewWidth = 2000;
    sal_Int32 nViewHeight = 2000;
    std::string sHash = GetInterfaceHash(GetXDrawPageForSdrPage(pPage));
    CPPUNIT_ASSERT(pXImpressDocument->createSlideRenderer(sHash.c_str(), 0, nViewWidth, nViewHeight, true, true));
    CPPUNIT_ASSERT_EQUAL(2000, nViewWidth);
    CPPUNIT_ASSERT_EQUAL(1125, nViewHeight);

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(0, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("Background"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "initVisible"));
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "bounds"));
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(!bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("placeholder"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("DateTime"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(!bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("placeholder"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("Footer"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = true;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(
            !pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(!bIsBitmapLayer);

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("MasterPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(2, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("placeholder"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("SlideNumber"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(1, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "initVisible"));
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "bounds"));
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(2, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("false"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "bounds"));
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(3, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(2, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("true"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "bounds"));
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(4, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(3, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "initVisible"));
        CPPUNIT_ASSERT_EQUAL(false, has_child(aContentChild, "bounds"));
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(bIsBitmapLayer);

        debugWriteImageToFile(5, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(4, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("false"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "bounds"));
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(aJson.isEmpty());
    }

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animation_TextBox)
{
    // Check rendering of animated paragraphs in a text box

    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_TextBox.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);
    SdPage* pPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT(pPage);
    sal_Int32 nViewWidth = 2000;
    sal_Int32 nViewHeight = 2000;

    std::string sHash = GetInterfaceHash(GetXDrawPageForSdrPage(pPage));
    CPPUNIT_ASSERT(pXImpressDocument->createSlideRenderer(sHash.c_str(), 0, nViewWidth, nViewHeight, true, true));
    CPPUNIT_ASSERT_EQUAL(2000, nViewWidth);
    CPPUNIT_ASSERT_EQUAL(1125, nViewHeight);

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(0, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("Background"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(21, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(2, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("false"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "bounds"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(3, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(2, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("false"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "bounds"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(4, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(3, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("false"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "bounds"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    // Check we are done
    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(aJson.isEmpty());
    }

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animation_TextBox_SecondParagraphMultipleEffects)
{
    // Check rendering of animated paragraphs in a text box when there are multiple effects for one paragraph

    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_Animations_SecondParagraphMultipleEffects.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);
    SdPage* pPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT(pPage);
    sal_Int32 nViewWidth = 2000;
    sal_Int32 nViewHeight = 2000;

    std::string sHash = GetInterfaceHash(GetXDrawPageForSdrPage(pPage));
    CPPUNIT_ASSERT(pXImpressDocument->createSlideRenderer(sHash.c_str(), 0, nViewWidth, nViewHeight, true, true));
    CPPUNIT_ASSERT_EQUAL(2000, nViewWidth);
    CPPUNIT_ASSERT_EQUAL(1125, nViewHeight);

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(1, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("Background"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(2, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        debugWriteImageToFile(3, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("false"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "bounds"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    // Check we are done
    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(aJson.isEmpty());
    }

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_TEST_FIXTURE(SdTiledRenderingTest, testSlideshowLayeredRendering_Animation_DifferentKindOfTextBox)
{
    SdXImpressDocument* pXImpressDocument = createDoc("SlideRenderingTest_Animations_DifferentKindOfTextBox.odp");
    pXImpressDocument->initializeForTiledRendering(uno::Sequence<beans::PropertyValue>());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);
    SdPage* pPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT(pPage);
    sal_Int32 nViewWidth = 2000;
    sal_Int32 nViewHeight = 2000;

    std::string sHash = GetInterfaceHash(GetXDrawPageForSdrPage(pPage));
    CPPUNIT_ASSERT(pXImpressDocument->createSlideRenderer(sHash.c_str(), 0, nViewWidth, nViewHeight, true, true));
    CPPUNIT_ASSERT_EQUAL(2000, nViewWidth);
    CPPUNIT_ASSERT_EQUAL(1500, nViewHeight);

    std::vector<std::string> aBufferHashes;

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        // Remember the hash of the buffer for uniqueness check
        auto aBufferHash = comphelper::Hash::calculateHash(pBuffer.data(), pBuffer.size(), comphelper::HashType::SHA1);
        aBufferHashes.push_back(comphelper::hashToString(aBufferHash));

        debugWriteImageToFile(1, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("Background"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        // Remember the hash of the buffer for uniqueness check
        auto aBufferHash = comphelper::Hash::calculateHash(pBuffer.data(), pBuffer.size(), comphelper::HashType::SHA1);
        aBufferHashes.push_back(comphelper::hashToString(aBufferHash));

        debugWriteImageToFile(2, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(0, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChild.get_child("checksum").get_value<std::string>());
    }

    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(!pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));

        // Remember the hash of the buffer for uniqueness check
        auto aBufferHash = comphelper::Hash::calculateHash(pBuffer.data(), pBuffer.size(), comphelper::HashType::SHA1);
        aBufferHashes.push_back(comphelper::hashToString(aBufferHash));

        debugWriteImageToFile(3, pBuffer, nViewWidth, nViewHeight, aJson.toUtf8().getStr());

        boost::property_tree::ptree aTree;
        readJSON(aTree, aJson);

        CPPUNIT_ASSERT_EQUAL(std::string("DrawPage"), aTree.get_child("group").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(1, aTree.get_child("index").get_value<int>());
        CPPUNIT_ASSERT_EQUAL(std::string("animated"), aTree.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "slideHash"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aTree, "content"));
        auto aContentChild = aTree.get_child("content");
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "hash"));
        CPPUNIT_ASSERT_EQUAL(std::string("false"), aContentChild.get_child("initVisible").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("bitmap"), aContentChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "bounds"));

        CPPUNIT_ASSERT_EQUAL(true, has_child(aContentChild, "content"));
        auto aContentChildChild = aContentChild.get_child("content");
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGETYPE%"), aContentChildChild.get_child("type").get_value<std::string>());
        CPPUNIT_ASSERT_EQUAL(std::string("%IMAGECHECKSUM%"), aContentChildChild.get_child("checksum").get_value<std::string>());
    }

    // check if all hashes are unique
    CPPUNIT_ASSERT(is_unique(aBufferHashes));

    // Check we are done
    {
        std::vector<sal_uInt8> pBuffer(nViewWidth * nViewHeight * 4);
        bool bIsBitmapLayer = false;
        double dScale = 1.0;
        OUString aJson;
        CPPUNIT_ASSERT(pXImpressDocument->renderNextSlideLayer(pBuffer.data(), bIsBitmapLayer, dScale, aJson));
        CPPUNIT_ASSERT(aJson.isEmpty());
    }

    pXImpressDocument->postSlideshowCleanup();
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
