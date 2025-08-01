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

#include <vcl/idle.hxx>
#include <sdr/contact/viewobjectcontactofpageobj.hxx>
#include <sdr/contact/viewcontactofpageobj.hxx>
#include <svx/svdopage.hxx>
#include <svx/sdr/contact/displayinfo.hxx>
#include <svtools/colorcfg.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <drawinglayer/primitive2d/PolygonHairlinePrimitive2D.hxx>
#include <drawinglayer/primitive2d/PolyPolygonColorPrimitive2D.hxx>
#include <sdr/contact/objectcontactofobjlistpainter.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <svx/svdpage.hxx>
#include <svx/svdmodel.hxx>
#include <svx/unoapi.hxx>
#include <drawinglayer/primitive2d/pagepreviewprimitive2d.hxx>
#include <drawinglayer/primitive2d/sdrdecompositiontools2d.hxx>
#include <vcl/canvastools.hxx>

using namespace com::sun::star;

namespace sdr::contact {

class PagePrimitiveExtractor : public ObjectContactOfPagePainter, public Idle
{
private:
    // the ViewObjectContactOfPageObj using this painter
    ViewObjectContactOfPageObj&         mrViewObjectContactOfPageObj;

public:
    // basic constructor/destructor
    explicit PagePrimitiveExtractor(ViewObjectContactOfPageObj& rVOC);
    virtual ~PagePrimitiveExtractor() override;

    // LazyInvalidate request. Supported here to not automatically
    // invalidate the second interaction state all the time at the
    // original OC
    virtual void setLazyInvalidate(ViewObjectContact& rVOC) override;

    // From baseclass Timer, the timeout call triggered by the LazyInvalidate mechanism
    virtual void Invoke() final override;

    // get primitive visualization
    drawinglayer::primitive2d::Primitive2DContainer createPrimitive2DSequenceForPage();

    // Own reaction on changes which will be forwarded to the OC of the owner-VOC
    virtual void InvalidatePartOfView(const basegfx::B2DRange& rRange) const override;

    // forward access to SdrPageView of ViewObjectContactOfPageObj
    virtual bool isOutputToPrinter() const override;
    virtual bool isPageDecorationActive() const override;
    virtual bool isMasterPageActive() const override;
    virtual bool isOutputToRecordingMetaFile() const override;
    virtual bool isOutputToPDFFile() const override;
    virtual bool isExportTaggedPDF() const override;
    virtual ::vcl::PDFExtOutDevData const* GetPDFExtOutDevData() const override;
    virtual bool isDrawModeGray() const override;
    virtual bool isDrawModeHighContrast() const override;
    virtual SdrPageView* TryToGetSdrPageView() const override;
    virtual OutputDevice* TryToGetOutputDevice() const override;
};

PagePrimitiveExtractor::PagePrimitiveExtractor(
    ViewObjectContactOfPageObj& rVOC)
:   ObjectContactOfPagePainter(rVOC.GetObjectContact()), Idle("svx PagePrimitiveExtractor"),
    mrViewObjectContactOfPageObj(rVOC)
{
    // make this renderer a preview renderer
    setPreviewRenderer(true);

    // init timer
    SetPriority(TaskPriority::HIGH_IDLE);
    Stop();
}

PagePrimitiveExtractor::~PagePrimitiveExtractor()
{
    // execute missing LazyInvalidates and stop timer
    Invoke();
}

void PagePrimitiveExtractor::setLazyInvalidate(ViewObjectContact& /*rVOC*/)
{
    // do NOT call parent, but remember that something is to do by
    // starting the LazyInvalidateTimer
    Start();
}

// From baseclass Timer, the timeout call triggered by the LazyInvalidate mechanism
void PagePrimitiveExtractor::Invoke()
{
    // stop the timer
    Stop();

    // invalidate all LazyInvalidate VOCs new situations
    const sal_uInt32 nVOCCount(getViewObjectContactCount());

    for(sal_uInt32 a(0); a < nVOCCount; a++)
    {
        ViewObjectContact* pCandidate = getViewObjectContact(a);
        pCandidate->triggerLazyInvalidate();
    }
}

drawinglayer::primitive2d::Primitive2DContainer PagePrimitiveExtractor::createPrimitive2DSequenceForPage()
{
    drawinglayer::primitive2d::Primitive2DContainer xRetval;
    SdrPage* pStartPage = GetStartPage();

    if(pStartPage)
    {
        // update own ViewInformation2D for visualized page
        const drawinglayer::geometry::ViewInformation2D& rOriginalViewInformation = mrViewObjectContactOfPageObj.GetObjectContact().getViewInformation2D();
        drawinglayer::geometry::ViewInformation2D aNewViewInformation2D(rOriginalViewInformation);

        // #i101075# use empty range for page content here to force
        // the content not to be physically clipped in any way. This
        // would be possible, but would require the internal transformation
        // which maps between the page visualisation object and the page
        // content, including the aspect ratios (for details see in
        // PagePreviewPrimitive2D::create2DDecomposition)
        aNewViewInformation2D.setViewport(basegfx::B2DRange());

        aNewViewInformation2D.setVisualizedPage(GetXDrawPageForSdrPage(pStartPage));

        // no time; page previews are not animated
        aNewViewInformation2D.setViewTime(0.0);

        setViewInformation2D2D(aNewViewInformation2D);

        // create copy of DisplayInfo to set PagePainting
        DisplayInfo aDisplayInfo;

        // get page's VOC
        ViewObjectContact& rDrawPageVOContact = pStartPage->GetViewContact().GetViewObjectContact(*this);

        // get whole Primitive2DContainer
        rDrawPageVOContact.getPrimitive2DSequenceHierarchy(aDisplayInfo, xRetval);
    }

    return xRetval;
}

void PagePrimitiveExtractor::InvalidatePartOfView(const basegfx::B2DRange& rRange) const
{
    // an invalidate is called at this view, this needs to be translated to an invalidate
    // for the using VOC. Coordinates are in page coordinate system.
    const SdrPage* pStartPage = GetStartPage();

    if(pStartPage && !rRange.isEmpty())
    {
        const basegfx::B2DRange aPageRange(0.0, 0.0, static_cast<double>(pStartPage->GetWidth()), static_cast<double>(pStartPage->GetHeight()));

        if(rRange.overlaps(aPageRange))
        {
            // if object on the page is inside or overlapping with page, create ActionChanged() for
            // involved VOC
            mrViewObjectContactOfPageObj.ActionChanged();
        }
    }
}

// forward access to SdrPageView to VOCOfPageObj
bool PagePrimitiveExtractor::isOutputToPrinter() const { return mrViewObjectContactOfPageObj.GetObjectContact().isOutputToPrinter(); }
bool PagePrimitiveExtractor::isPageDecorationActive() const { return mrViewObjectContactOfPageObj.GetObjectContact().isPageDecorationActive(); }
bool PagePrimitiveExtractor::isMasterPageActive() const { return mrViewObjectContactOfPageObj.GetObjectContact().isMasterPageActive(); }
bool PagePrimitiveExtractor::isOutputToRecordingMetaFile() const { return mrViewObjectContactOfPageObj.GetObjectContact().isOutputToRecordingMetaFile(); }
bool PagePrimitiveExtractor::isOutputToPDFFile() const { return mrViewObjectContactOfPageObj.GetObjectContact().isOutputToPDFFile(); }
bool PagePrimitiveExtractor::isExportTaggedPDF() const { return mrViewObjectContactOfPageObj.GetObjectContact().isExportTaggedPDF(); }
::vcl::PDFExtOutDevData const* PagePrimitiveExtractor::GetPDFExtOutDevData() const { return mrViewObjectContactOfPageObj.GetObjectContact().GetPDFExtOutDevData(); }
bool PagePrimitiveExtractor::isDrawModeGray() const { return mrViewObjectContactOfPageObj.GetObjectContact().isDrawModeGray(); }
bool PagePrimitiveExtractor::isDrawModeHighContrast() const { return mrViewObjectContactOfPageObj.GetObjectContact().isDrawModeHighContrast(); }
SdrPageView* PagePrimitiveExtractor::TryToGetSdrPageView() const { return mrViewObjectContactOfPageObj.GetObjectContact().TryToGetSdrPageView(); }
OutputDevice* PagePrimitiveExtractor::TryToGetOutputDevice() const { return mrViewObjectContactOfPageObj.GetObjectContact().TryToGetOutputDevice(); }

void ViewObjectContactOfPageObj::createPrimitive2DSequence(const DisplayInfo& /*rDisplayInfo*/, drawinglayer::primitive2d::Primitive2DDecompositionVisitor& rVisitor) const
{
    const SdrPageObj& rPageObject(static_cast< ViewContactOfPageObj& >(GetViewContact()).GetPageObj());
    const SdrPage* pPage = rPageObject.GetReferencedPage();
    const svtools::ColorConfig aColorConfig;

    // get PageObject's geometry
    basegfx::B2DHomMatrix aPageObjectTransform;
    {
        const tools::Rectangle aPageObjectModelData(rPageObject.GetLastBoundRect());
        const basegfx::B2DRange aPageObjectBound = vcl::unotools::b2DRectangleFromRectangle(aPageObjectModelData);

        aPageObjectTransform.set(0, 0, aPageObjectBound.getWidth());
        aPageObjectTransform.set(1, 1, aPageObjectBound.getHeight());
        aPageObjectTransform.set(0, 2, aPageObjectBound.getMinX());
        aPageObjectTransform.set(1, 2, aPageObjectBound.getMinY());
    }

    // #i102637# add gray frame also when printing and page exists (handout pages)
    const bool bCreateGrayFrame(!GetObjectContact().isOutputToPrinter() || pPage);

    // get displayed page's content. This is the unscaled page content
    if(mpExtractor && pPage)
    {
        // get displayed page's geometry
        drawinglayer::primitive2d::Primitive2DContainer xPageContent;
        const Size aPageSize(pPage->GetSize());
        const double fPageWidth(aPageSize.getWidth());
        const double fPageHeight(aPageSize.getHeight());

        // The case that a PageObject contains another PageObject which visualizes the
        // same page again would lead to a recursion. Limit that recursion depth to one
        // by using a local static bool
        static bool bInCreatePrimitive2D(false);

        if(bInCreatePrimitive2D)
        {
            // Recursion is possible. Create a replacement primitive
            xPageContent.resize(2);
            const Color aDocColor(aColorConfig.GetColorValue(svtools::DOCCOLOR).nColor);
            const bool bShowMargin = pPage->getSdrModelFromSdrPage().IsShowMargin();
            const Color aBorderColor = bShowMargin ? aColorConfig.GetColorValue(svtools::DOCBOUNDARIES).nColor : aDocColor;
            const basegfx::B2DRange aPageBound(0.0, 0.0, fPageWidth, fPageHeight);
            basegfx::B2DPolygon aOutline(basegfx::utils::createPolygonFromRect(aPageBound));

            // add replacement fill
            xPageContent[0] = drawinglayer::primitive2d::Primitive2DReference(
                new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(basegfx::B2DPolyPolygon(aOutline), aDocColor.getBColor()));

            // add replacement border
            xPageContent[1] = drawinglayer::primitive2d::Primitive2DReference(
                new drawinglayer::primitive2d::PolygonHairlinePrimitive2D(std::move(aOutline), aBorderColor.getBColor()));
        }
        else
        {
            // set recursion flag
            bInCreatePrimitive2D = true;

            // init extractor, guarantee existence, set page there
            mpExtractor->SetStartPage(pPage);

            // #i105548# also need to copy the VOCRedirector for sub-content creation
            mpExtractor->SetViewObjectContactRedirector(GetObjectContact().GetViewObjectContactRedirector());

            // create page content
            xPageContent = mpExtractor->createPrimitive2DSequenceForPage();

            // #i105548# reset VOCRedirector to not accidentally have a pointer to a
            // temporary class, so calls to it are avoided safely
            mpExtractor->SetViewObjectContactRedirector(nullptr);

            // reset recursion flag
            bInCreatePrimitive2D = false;
        }

        // prepare retval
        if(!xPageContent.empty())
        {
            const uno::Reference< drawing::XDrawPage > xDrawPage(GetXDrawPageForSdrPage(const_cast< SdrPage*>(pPage)));
            const drawinglayer::primitive2d::Primitive2DReference xPagePreview(new drawinglayer::primitive2d::PagePreviewPrimitive2D(
                xDrawPage, aPageObjectTransform, fPageWidth, fPageHeight, std::move(xPageContent)));
            rVisitor.visit(xPagePreview);
        }
    }
    else if(bCreateGrayFrame)
    {
        // #i105146# no content, but frame display. To make hitting the page preview objects
        // on the handout page more simple, add hidden fill geometry
        const drawinglayer::primitive2d::Primitive2DReference xFrameHit(
            drawinglayer::primitive2d::createHiddenGeometryPrimitives2D(
                aPageObjectTransform));
        rVisitor.visit(xFrameHit);
    }

    // add a gray outline frame, except not when printing
    if(bCreateGrayFrame)
    {
        const Color aFrameColor(aColorConfig.GetColorValue(svtools::DOCBOUNDARIES).nColor);
        basegfx::B2DPolygon aOwnOutline(basegfx::utils::createUnitPolygon());
        aOwnOutline.transform(aPageObjectTransform);

        const drawinglayer::primitive2d::Primitive2DReference xGrayFrame(
            new drawinglayer::primitive2d::PolygonHairlinePrimitive2D(std::move(aOwnOutline), aFrameColor.getBColor()));

        rVisitor.visit(xGrayFrame);
    }
}

ViewObjectContactOfPageObj::ViewObjectContactOfPageObj(ObjectContact& rObjectContact, ViewContact& rViewContact)
:   ViewObjectContactOfSdrObj(rObjectContact, rViewContact),
    mpExtractor(new PagePrimitiveExtractor(*this))
{
}

ViewObjectContactOfPageObj::~ViewObjectContactOfPageObj()
{
    // delete the helper OC
    if(mpExtractor)
    {
        // remember candidate and reset own pointer to avoid action when createPrimitive2DSequence()
        // would be called for any reason
        std::unique_ptr<PagePrimitiveExtractor> pCandidate = std::move(mpExtractor);

        // also reset the StartPage to avoid ActionChanged() forwardings in the
        // PagePrimitiveExtractor::InvalidatePartOfView() implementation
        pCandidate->SetStartPage(nullptr);
    }
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
