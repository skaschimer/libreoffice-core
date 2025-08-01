/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include "SlsListener.hxx"

#include <SlideSorter.hxx>
#include <ViewShell.hxx>
#include <ViewShellHint.hxx>
#include <unomodel.hxx>
#include <controller/SlideSorterController.hxx>
#include <controller/SlsPageSelector.hxx>
#include <controller/SlsCurrentSlideManager.hxx>
#include <controller/SlsSelectionManager.hxx>
#include <controller/SlsSelectionObserver.hxx>
#include <model/SlideSorterModel.hxx>
#include <view/SlideSorterView.hxx>
#include <cache/SlsPageCache.hxx>
#include <cache/SlsPageCacheManager.hxx>
#include <drawdoc.hxx>
#include <sdpage.hxx>
#include <DrawDocShell.hxx>
#include <svx/svdpage.hxx>

#include <ViewShellBase.hxx>
#include <EventMultiplexer.hxx>
#include <com/sun/star/document/XEventBroadcaster.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/frame/FrameActionEvent.hpp>
#include <com/sun/star/frame/FrameAction.hpp>
#include <tools/debug.hxx>
#include <comphelper/diagnose_ex.hxx>

using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star;

namespace sd::slidesorter::controller {

Listener::Listener (
    SlideSorter& rSlideSorter)
    : mrSlideSorter(rSlideSorter),
      mrController(mrSlideSorter.GetController()),
      mpBase(mrSlideSorter.GetViewShellBase()),
      mbListeningToDocument (false),
      mbListeningToUNODocument (false),
      mbListeningToController (false),
      mbListeningToFrame (false),
      mbIsMainViewChangePending(false)
{
    StartListening(*mrSlideSorter.GetModel().GetDocument());
    StartListening(*mrSlideSorter.GetModel().GetDocument()->GetDocSh());
    mbListeningToDocument = true;

    // Connect to the UNO document.
    rtl::Reference<SdXImpressDocument> xBroadcaster(
        mrSlideSorter.GetModel().GetDocument()->getUnoModel());
    if (xBroadcaster.is())
    {
        xBroadcaster->addEventListener(css::uno::Reference< css::document::XEventListener >(this));
        mbListeningToUNODocument = true;
    }

    // Listen for disposing events from the document.
    if (xBroadcaster.is())
        xBroadcaster->addEventListener (
            Reference<lang::XEventListener>(
                static_cast<XWeak*>(this), UNO_QUERY));

    // Connect to the frame to listen for controllers being exchanged.
    bool bIsMainViewShell (false);
    ViewShell& rViewShell = mrSlideSorter.GetViewShell();
    bIsMainViewShell = rViewShell.IsMainViewShell();
    if ( ! bIsMainViewShell)
    {
        // Listen to changes of certain properties.
        Reference<frame::XFrame> xFrame;
        Reference<frame::XController> xController (mrSlideSorter.GetXController());
        if (xController.is())
            xFrame = xController->getFrame();
        mxFrameWeak = xFrame;
        if (xFrame.is())
        {
            xFrame->addFrameActionListener(Reference<frame::XFrameActionListener>(this));
            mbListeningToFrame = true;
        }

        // Connect to the current controller.
        ConnectToController ();
    }

    // Listen for hints of the MainViewShell as well.  If that is not yet
    // present then the EventMultiplexer will tell us when it is available.
    if (mpBase != nullptr)
    {
        ViewShell* pMainViewShell = mpBase->GetMainViewShell().get();
        if (pMainViewShell != nullptr
            && pMainViewShell!=&rViewShell)
        {
            StartListening(*pMainViewShell);
        }

        Link<tools::EventMultiplexerEvent&,void> aLink (LINK(this, Listener, EventMultiplexerCallback));
        mpBase->GetEventMultiplexer()->AddEventListener(aLink);
    }
}

Listener::~Listener()
{
    DBG_ASSERT( !mbListeningToDocument && !mbListeningToUNODocument && !mbListeningToFrame,
        "sd::Listener::~Listener(), disposing() was not called, ask DBO!" );
}

void Listener::ReleaseListeners()
{
    if (mbListeningToDocument)
    {
        EndListening(*mrSlideSorter.GetModel().GetDocument()->GetDocSh());
        EndListening(*mrSlideSorter.GetModel().GetDocument());
        mbListeningToDocument = false;
    }

    if (mbListeningToUNODocument)
    {
        rtl::Reference<SdXImpressDocument> xBroadcaster(
            mrSlideSorter.GetModel().GetDocument()->getUnoModel());
        if (xBroadcaster.is())
            xBroadcaster->removeEventListener(css::uno::Reference< css::document::XEventListener >(this));

        // Remove the dispose listener.
        if (xBroadcaster.is())
            xBroadcaster->removeEventListener (
                Reference<lang::XEventListener>(
                    static_cast<XWeak*>(this), UNO_QUERY));

        mbListeningToUNODocument = false;
    }

    if (mbListeningToFrame)
    {
        // Listen to changes of certain properties.
        Reference<frame::XFrame> xFrame (mxFrameWeak);
        if (xFrame.is())
        {
            xFrame->removeFrameActionListener(Reference<frame::XFrameActionListener>(this));
            mbListeningToFrame = false;
        }
    }

    DisconnectFromController ();

    if (mpBase != nullptr)
    {
        Link<sd::tools::EventMultiplexerEvent&,void> aLink (LINK(this, Listener, EventMultiplexerCallback));
        mpBase->GetEventMultiplexer()->RemoveEventListener(aLink);
    }
}

void Listener::ConnectToController()
{
    ViewShell& rShell = mrSlideSorter.GetViewShell();

    // Register at the controller of the main view shell (if we are that not
    // ourself).
    if (rShell.IsMainViewShell())
        return;

    Reference<frame::XController> xController (mrSlideSorter.GetXController());

    // Listen to changes of certain properties.
    Reference<beans::XPropertySet> xSet (xController, UNO_QUERY);
    if (xSet.is())
    {
        try
        {
            xSet->addPropertyChangeListener(u"CurrentPage"_ustr, this);
        }
        catch (beans::UnknownPropertyException&)
        {
            DBG_UNHANDLED_EXCEPTION("sd");
        }
        try
        {
            xSet->addPropertyChangeListener(u"IsMasterPageMode"_ustr, this);
        }
        catch (beans::UnknownPropertyException&)
        {
            DBG_UNHANDLED_EXCEPTION("sd");
        }
    }

    // Listen for disposing events.
    if (xController.is())
    {
        xController->addEventListener (
            Reference<lang::XEventListener>(static_cast<XWeak*>(this), UNO_QUERY));

        mxControllerWeak = xController;
        mbListeningToController = true;
    }
}

void Listener::DisconnectFromController()
{
    if (!mbListeningToController)
        return;

    Reference<frame::XController> xController = mxControllerWeak;
    Reference<beans::XPropertySet> xSet (xController, UNO_QUERY);
    try
    {
        // Remove the property listener.
        if (xSet.is())
        {
            xSet->removePropertyChangeListener( u"CurrentPage"_ustr, this );
            xSet->removePropertyChangeListener( u"IsMasterPageMode"_ustr, this);
        }

        // Remove the dispose listener.
        if (xController.is())
            xController->removeEventListener (
                Reference<lang::XEventListener>(
                    static_cast<XWeak*>(this), UNO_QUERY));
    }
    catch (beans::UnknownPropertyException&)
    {
        DBG_UNHANDLED_EXCEPTION("sd");
    }

    mbListeningToController = false;
    mxControllerWeak = Reference<frame::XController>();
}

void Listener::Notify (
    SfxBroadcaster& rBroadcaster,
    const SfxHint& rHint)
{
    if (rHint.GetId() == SfxHintId::ThisIsAnSdrHint)
    {
        const SdrHint* pSdrHint = static_cast<const SdrHint*>(&rHint);
        switch (pSdrHint->GetKind())
        {
            case SdrHintKind::ModelCleared:
                if (&rBroadcaster == mrSlideSorter.GetModel().GetDocument())
                {   // rhbz#965646 stop listening to dying document
                    EndListening(rBroadcaster);
                    return;
                }
                break;
            case SdrHintKind::PageOrderChange:
                if (&rBroadcaster == mrSlideSorter.GetModel().GetDocument())
                    HandleModelChange(pSdrHint->GetPage());
                break;

            default:
                break;
        }
    }
    else if (rHint.GetId() == SfxHintId::DocChanged)
    {
        mrController.CheckForMasterPageAssignment();
        mrController.CheckForSlideTransitionAssignment();
    }
    else if (rHint.GetId() == SfxHintId::SdViewShell)
    {
        auto pViewShellHint = static_cast<const ViewShellHint*>(&rHint);
        switch (pViewShellHint->GetHintId())
        {
            case ViewShellHint::HINT_PAGE_RESIZE_START:
                // Initiate a model change but do nothing (well, not much)
                // until we are told that all slides have been resized.
                mpModelChangeLock.reset(new SlideSorterController::ModelChangeLock(mrController),
                                        o3tl::default_delete<SlideSorterController::ModelChangeLock>());
                mrController.HandleModelChange();
                break;

            case ViewShellHint::HINT_PAGE_RESIZE_END:
                // All slides have been resized.  The model has to be updated.
                mpModelChangeLock.reset();
                break;

            case ViewShellHint::HINT_CHANGE_EDIT_MODE_START:
                mrController.PrepareEditModeChange();
                break;

            case ViewShellHint::HINT_CHANGE_EDIT_MODE_END:
                mrController.FinishEditModeChange();
                break;

            case ViewShellHint::HINT_COMPLEX_MODEL_CHANGE_START:
                mpModelChangeLock.reset(new SlideSorterController::ModelChangeLock(mrController),
                                        o3tl::default_delete<SlideSorterController::ModelChangeLock>());
                break;

            case ViewShellHint::HINT_COMPLEX_MODEL_CHANGE_END:
                mpModelChangeLock.reset();
                break;
        }
    }
}

IMPL_LINK(Listener, EventMultiplexerCallback, ::sd::tools::EventMultiplexerEvent&, rEvent, void)
{
    switch (rEvent.meEventId)
    {
        case EventMultiplexerEventId::MainViewRemoved:
        {
            if (mpBase != nullptr)
            {
                ViewShell* pMainViewShell = mpBase->GetMainViewShell().get();
                if (pMainViewShell != nullptr)
                    EndListening(*pMainViewShell);
            }
        }
        break;

        case EventMultiplexerEventId::MainViewAdded:
            mbIsMainViewChangePending = true;
            break;

        case EventMultiplexerEventId::ConfigurationUpdated:
            if (mbIsMainViewChangePending && mpBase != nullptr)
            {
                mbIsMainViewChangePending = false;
                ViewShell* pMainViewShell = mpBase->GetMainViewShell().get();
                if (pMainViewShell != nullptr
                    && pMainViewShell!=&mrSlideSorter.GetViewShell())
                {
                    StartListening (*pMainViewShell);
                }
            }
            break;

        case EventMultiplexerEventId::ControllerAttached:
        {
            ConnectToController();
            //            mrController.GetPageSelector().GetCoreSelection();
            UpdateEditMode();
        }
        break;

        case EventMultiplexerEventId::ControllerDetached:
            DisconnectFromController();
            break;

        case EventMultiplexerEventId::ShapeChanged:
        case EventMultiplexerEventId::ShapeInserted:
        case EventMultiplexerEventId::ShapeRemoved:
            HandleShapeModification(static_cast<const SdrPage*>(rEvent.mpUserData));
            break;

        case EventMultiplexerEventId::EndTextEdit:
            if (rEvent.mpUserData != nullptr)
            {
                const SdrObject* pObject = static_cast<const SdrObject*>(rEvent.mpUserData);
                HandleShapeModification(pObject->getSdrPageFromSdrObject());
            }
            break;

        default:
            break;
    }
}

//=====  lang::XEventListener  ================================================

void SAL_CALL Listener::disposing (
    const lang::EventObject& rEventObject)
{
    if ((mbListeningToDocument || mbListeningToUNODocument)
        && mrSlideSorter.GetModel().GetDocument()!=nullptr
        && rEventObject.Source
           == uno::Reference<XInterface>(cppu::getXWeak(mrSlideSorter.GetModel().GetDocument()->getUnoModel())))
    {
        mbListeningToDocument = false;
        mbListeningToUNODocument = false;
    }
    else if (mbListeningToController)
    {
        Reference<frame::XController> xController (mxControllerWeak);
        if (rEventObject.Source == xController)
        {
            mbListeningToController = false;
        }
    }
}

//=====  document::XEventListener  ============================================

void SAL_CALL Listener::notifyEvent (
    const document::EventObject& )
{
}

//=====  beans::XPropertySetListener  =========================================

void SAL_CALL Listener::propertyChange (
    const PropertyChangeEvent& rEvent)
{
    if (m_bDisposed)
    {
        throw lang::DisposedException (u"SlideSorterController object has already been disposed"_ustr,
            static_cast<uno::XWeak*>(this));
    }

    if (rEvent.PropertyName == "CurrentPage")
    {
        Any aCurrentPage = rEvent.NewValue;
        Reference<beans::XPropertySet> xPageSet (aCurrentPage, UNO_QUERY);
        if (xPageSet.is())
        {
            try
            {
                Any aPageNumber = xPageSet->getPropertyValue (u"Number"_ustr);
                sal_Int32 nCurrentPage = 0;
                aPageNumber >>= nCurrentPage;
                // The selection is already set but we call SelectPage()
                // nevertheless in order to make the new current page the
                // last recently selected page of the PageSelector.  This is
                // used when making the selection visible.
                mrController.GetCurrentSlideManager().NotifyCurrentSlideChange(nCurrentPage-1);
                mrController.GetPageSelector().SelectPage(nCurrentPage-1);
            }
            catch (beans::UnknownPropertyException&)
            {
                DBG_UNHANDLED_EXCEPTION("sd");
            }
            catch (lang::DisposedException&)
            {
                // Something is already disposed.  There is not much we can
                // do, except not to crash.
            }
        }
    }
    else if (rEvent.PropertyName == "IsMasterPageMode")
    {
        bool bIsMasterPageMode = false;
        rEvent.NewValue >>= bIsMasterPageMode;
        mrController.ChangeEditMode (
            bIsMasterPageMode ? EditMode::MasterPage : EditMode::Page);
    }
}

//===== frame::XFrameActionListener  ==========================================

void SAL_CALL Listener::frameAction (const frame::FrameActionEvent& rEvent)
{
    switch (rEvent.Action)
    {
        case frame::FrameAction_COMPONENT_DETACHING:
            DisconnectFromController();
            break;

        case frame::FrameAction_COMPONENT_REATTACHED:
        {
            ConnectToController();
            mrController.GetPageSelector().GetCoreSelection();
            UpdateEditMode();
        }
        break;

        default:
            break;
    }
}

void Listener::disposing(std::unique_lock<std::mutex>&)
{
    ReleaseListeners();
}

void Listener::UpdateEditMode()
{
    // When there is a new controller then the edit mode may have changed at
    // the same time.
    Reference<frame::XController> xController (mxControllerWeak);
    Reference<beans::XPropertySet> xSet (xController, UNO_QUERY);
    bool bIsMasterPageMode = false;
    if (xSet != nullptr)
    {
        try
        {
            Any aValue (xSet->getPropertyValue( u"IsMasterPageMode"_ustr ));
            aValue >>= bIsMasterPageMode;
        }
        catch (beans::UnknownPropertyException&)
        {
            // When the property is not supported then the master page mode
            // is not supported, too.
            bIsMasterPageMode = false;
        }
    }
    mrController.ChangeEditMode (
        bIsMasterPageMode ? EditMode::MasterPage : EditMode::Page);
}

void Listener::HandleModelChange (const SdrPage* pPage)
{
    // Notify model and selection observer about the page.  The return value
    // of the model call acts as filter as to which events to pass to the
    // selection observer.
    if (mrSlideSorter.GetModel().NotifyPageEvent(pPage))
    {
        // The page of the hint belongs (or belonged) to the model.

        // Tell the cache manager that the preview bitmaps for a deleted
        // page can be removed from all caches.
        if (pPage!=nullptr && ! pPage->IsInserted())
            cache::PageCacheManager::Instance()->ReleasePreviewBitmap(pPage);

        mrController.GetSelectionManager()->GetSelectionObserver()->NotifyPageEvent(pPage);
    }

    // Tell the controller about the model change only when the document is
    // in a sane state, not just in the middle of a larger change.
    SdDrawDocument* pDocument (mrSlideSorter.GetModel().GetDocument());
    if (pDocument != nullptr
        && pDocument->GetMasterSdPageCount(PageKind::Standard) == pDocument->GetMasterSdPageCount(PageKind::Notes))
    {
        // A model change can make updates of some text fields necessary
        // (like page numbers and page count.)  Invalidate all previews in
        // the cache to cope with this.  Doing this on demand would be a
        // nice optimization.
        cache::PageCacheManager::Instance()->InvalidateAllPreviewBitmaps(pDocument->getUnoModel());

        mrController.HandleModelChange();
    }
}

void Listener::HandleShapeModification (const SdrPage* pPage)
{
    if (pPage == nullptr)
        return;

    // Invalidate the preview of the page (in all slide sorters that display
    // it.)
    std::shared_ptr<cache::PageCacheManager> pCacheManager (cache::PageCacheManager::Instance());
    if ( ! pCacheManager)
        return;
    SdDrawDocument* pDocument = mrSlideSorter.GetModel().GetDocument();
    if (pDocument == nullptr)
    {
        OSL_ASSERT(pDocument!=nullptr);
        return;
    }
    pCacheManager->InvalidatePreviewBitmap(pDocument->getUnoModel(), pPage);
    mrSlideSorter.GetView().GetPreviewCache()->RequestPreviewBitmap(pPage);

    // When the page is a master page then invalidate the previews of all
    // pages that are linked to this master page.
    if (!pPage->IsMasterPage())
        return;

    for (sal_uInt16 nIndex=0,nCount=pDocument->GetSdPageCount(PageKind::Standard);
         nIndex<nCount;
         ++nIndex)
    {
        const SdPage* pCandidate = pDocument->GetSdPage(nIndex, PageKind::Standard);
        if (pCandidate!=nullptr && pCandidate->TRG_HasMasterPage())
        {
            if (&pCandidate->TRG_GetMasterPage() == pPage)
                pCacheManager->InvalidatePreviewBitmap(pDocument->getUnoModel(), pCandidate);
        }
        else
        {
            OSL_ASSERT(pCandidate!=nullptr && pCandidate->TRG_HasMasterPage());
        }
    }
}

} // end of namespace ::sd::slidesorter::controller

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
