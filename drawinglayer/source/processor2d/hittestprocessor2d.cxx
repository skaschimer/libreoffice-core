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

#include <drawinglayer/processor2d/hittestprocessor2d.hxx>
#include <drawinglayer/primitive2d/drawinglayer_primitivetypes2d.hxx>
#include <drawinglayer/primitive2d/transformprimitive2d.hxx>
#include <drawinglayer/primitive2d/PolygonHairlinePrimitive2D.hxx>
#include <drawinglayer/primitive2d/PolygonMarkerPrimitive2D.hxx>
#include <drawinglayer/primitive2d/PolygonWavePrimitive2D.hxx>
#include <drawinglayer/primitive2d/PolyPolygonColorPrimitive2D.hxx>
#include <drawinglayer/primitive2d/BitmapAlphaPrimitive2D.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <basegfx/polygon/b2dpolypolygontools.hxx>
#include <drawinglayer/primitive2d/transparenceprimitive2d.hxx>
#include <drawinglayer/primitive2d/maskprimitive2d.hxx>
#include <drawinglayer/primitive2d/sceneprimitive2d.hxx>
#include <drawinglayer/primitive2d/pointarrayprimitive2d.hxx>
#include <basegfx/matrix/b3dhommatrix.hxx>
#include <drawinglayer/processor3d/cutfindprocessor3d.hxx>
#include <drawinglayer/primitive2d/hiddengeometryprimitive2d.hxx>
#include <drawinglayer/primitive2d/bitmapprimitive2d.hxx>
#include <comphelper/lok.hxx>
#include <toolkit/helper/vclunohelper.hxx>

namespace drawinglayer::processor2d
{
        HitTestProcessor2D::HitTestProcessor2D(const geometry::ViewInformation2D& rViewInformation,
            const basegfx::B2DPoint& rLogicHitPosition,
            const basegfx::B2DVector& rLogicHitTolerancePerAxis,
            bool bHitTextOnly)
        :   BaseProcessor2D(rViewInformation),
            maDiscreteHitTolerancePerAxis(rLogicHitTolerancePerAxis),
            mbCollectHitStack(false),
            mbHit(false),
            mbHitTextOnly(bHitTextOnly)
        {
            // ensure input parameters for hit tolerance is >= 0.0
            if (maDiscreteHitTolerancePerAxis.getX() < 0.0)
                maDiscreteHitTolerancePerAxis.setX(0.0);
            if (maDiscreteHitTolerancePerAxis.getY() < 0.0)
                maDiscreteHitTolerancePerAxis.setY(0.0);

            if (!maDiscreteHitTolerancePerAxis.equalZero())
            {
                // generate discrete hit tolerance
                maDiscreteHitTolerancePerAxis
                    = getViewInformation2D().getObjectToViewTransformation() * rLogicHitTolerancePerAxis;
            }

            // generate discrete hit position
            maDiscreteHitPosition = getViewInformation2D().getObjectToViewTransformation() * rLogicHitPosition;
        }

        HitTestProcessor2D::~HitTestProcessor2D()
        {
        }

        bool HitTestProcessor2D::checkHairlineHitWithTolerance(
            const basegfx::B2DPolygon& rPolygon,
            const basegfx::B2DVector& rDiscreteHitTolerancePerAxis) const
        {
            basegfx::B2DPolygon aLocalPolygon(rPolygon);
            aLocalPolygon.transform(getViewInformation2D().getObjectToViewTransformation());

            // get discrete range
            basegfx::B2DRange aPolygonRange(aLocalPolygon.getB2DRange());

            if(rDiscreteHitTolerancePerAxis.getX() > 0 || rDiscreteHitTolerancePerAxis.getY() > 0)
            {
                aPolygonRange.grow(rDiscreteHitTolerancePerAxis);
            }

            // do rough range test first
            if(aPolygonRange.isInside(getDiscreteHitPosition()))
            {
                // check if a polygon edge is hit
                return basegfx::utils::isInEpsilonRange(
                    aLocalPolygon,
                    getDiscreteHitPosition(),
                    std::max(rDiscreteHitTolerancePerAxis.getX(), rDiscreteHitTolerancePerAxis.getY()));
            }

            return false;
        }

        bool HitTestProcessor2D::checkFillHitWithTolerance(
            const basegfx::B2DPolyPolygon& rPolyPolygon,
            const basegfx::B2DVector& rDiscreteHitTolerancePerAxis) const
        {
            bool bRetval(false);
            basegfx::B2DPolyPolygon aLocalPolyPolygon(rPolyPolygon);
            aLocalPolyPolygon.transform(getViewInformation2D().getObjectToViewTransformation());

            // get discrete range
            basegfx::B2DRange aPolygonRange(aLocalPolyPolygon.getB2DRange());

            const bool bDiscreteHitToleranceUsed(rDiscreteHitTolerancePerAxis.getX() > 0
                                                 || rDiscreteHitTolerancePerAxis.getY() > 0);

            if (bDiscreteHitToleranceUsed)
            {
                aPolygonRange.grow(rDiscreteHitTolerancePerAxis);
            }

            // do rough range test first
            if(aPolygonRange.isInside(getDiscreteHitPosition()))
            {
                // if a HitTolerance is given, check for polygon edge hit in epsilon first
                if(bDiscreteHitToleranceUsed &&
                    basegfx::utils::isInEpsilonRange(
                        aLocalPolyPolygon,
                        getDiscreteHitPosition(),
                        std::max(rDiscreteHitTolerancePerAxis.getX(), rDiscreteHitTolerancePerAxis.getY())))
                {
                    bRetval = true;
                }

                // check for hit in filled polyPolygon
                if(!bRetval && basegfx::utils::isInside(
                    aLocalPolyPolygon,
                    getDiscreteHitPosition(),
                    true))
                {
                    bRetval = true;
                }
            }

            return bRetval;
        }

        void HitTestProcessor2D::checkBitmapHit(basegfx::B2DRange aRange, const BitmapEx& rBitmapEx, const basegfx::B2DHomMatrix& rTransform)
        {
            if(!getHitTextOnly())
            {
                // The recently added BitmapEx::GetTransparency() makes it easy to extend
                // the BitmapPrimitive2D HitTest to take the contained BitmapEx and it's
                // transparency into account
                if(!aRange.isEmpty())
                {
                    const Size& rSizePixel(rBitmapEx.GetSizePixel());

                    // When tiled rendering, don't bother with the pixel size of the candidate.
                    if(rSizePixel.Width() && rSizePixel.Height() && !comphelper::LibreOfficeKit::isActive())
                    {
                        basegfx::B2DHomMatrix aBackTransform(
                            getViewInformation2D().getObjectToViewTransformation() *
                            rTransform);
                        aBackTransform.invert();

                        const basegfx::B2DPoint aRelativePoint(aBackTransform * getDiscreteHitPosition());
                        const basegfx::B2DRange aUnitRange(0.0, 0.0, 1.0, 1.0);

                        if(aUnitRange.isInside(aRelativePoint))
                        {
                            const sal_Int32 nX(basegfx::fround(aRelativePoint.getX() * rSizePixel.Width()));
                            const sal_Int32 nY(basegfx::fround(aRelativePoint.getY() * rSizePixel.Height()));

                            mbHit = (0 != rBitmapEx.GetAlpha(nX, nY));
                        }
                    }
                    else
                    {
                        // fallback to standard HitTest
                        const basegfx::B2DPolygon aOutline(basegfx::utils::createPolygonFromRect(aRange));
                        mbHit = checkFillHitWithTolerance(basegfx::B2DPolyPolygon(aOutline), getDiscreteHitTolerance());
                    }
                }
            }
        }

        void HitTestProcessor2D::check3DHit(const primitive2d::ScenePrimitive2D& rCandidate)
        {
            // calculate relative point in unified 2D scene
            const basegfx::B2DPoint aLogicHitPosition(getViewInformation2D().getInverseObjectToViewTransformation() * getDiscreteHitPosition());

            // use bitmap check in ScenePrimitive2D
            bool bTryFastResult(false);

            if(rCandidate.tryToCheckLastVisualisationDirectHit(aLogicHitPosition, bTryFastResult))
            {
                mbHit = bTryFastResult;
            }
            else
            {
                basegfx::B2DHomMatrix aInverseSceneTransform(rCandidate.getObjectTransformation());
                aInverseSceneTransform.invert();
                const basegfx::B2DPoint aRelativePoint(aInverseSceneTransform * aLogicHitPosition);

                // check if test point is inside scene's unified area at all
                if(aRelativePoint.getX() >= 0.0 && aRelativePoint.getX() <= 1.0
                    && aRelativePoint.getY() >= 0.0 && aRelativePoint.getY() <= 1.0)
                {
                    // get 3D view information
                    const geometry::ViewInformation3D& rObjectViewInformation3D = rCandidate.getViewInformation3D();

                    // create HitPoint Front and Back, transform to object coordinates
                    basegfx::B3DHomMatrix aViewToObject(rObjectViewInformation3D.getObjectToView());
                    aViewToObject.invert();
                    const basegfx::B3DPoint aFront(aViewToObject * basegfx::B3DPoint(aRelativePoint.getX(), aRelativePoint.getY(), 0.0));
                    const basegfx::B3DPoint aBack(aViewToObject * basegfx::B3DPoint(aRelativePoint.getX(), aRelativePoint.getY(), 1.0));

                    if(!aFront.equal(aBack))
                    {
                        const primitive3d::Primitive3DContainer& rPrimitives = rCandidate.getChildren3D();

                        if(!rPrimitives.empty())
                        {
                            // make BoundVolume empty and overlapping test for speedup
                            const basegfx::B3DRange aObjectRange(
                                    rPrimitives.getB3DRange(rObjectViewInformation3D));

                            if(!aObjectRange.isEmpty())
                            {
                                const basegfx::B3DRange aFrontBackRange(aFront, aBack);

                                if(aObjectRange.overlaps(aFrontBackRange))
                                {
                                    // bound volumes hit, geometric cut tests needed
                                    drawinglayer::processor3d::CutFindProcessor aCutFindProcessor(
                                        rObjectViewInformation3D,
                                        aFront,
                                        aBack,
                                        true);
                                    aCutFindProcessor.process(rPrimitives);

                                    mbHit = (!aCutFindProcessor.getCutPoints().empty());
                                }
                            }
                        }
                    }
                }

                if(!getHit())
                {
                    // empty 3D scene; Check for border hit
                    basegfx::B2DPolygon aOutline(basegfx::utils::createUnitPolygon());
                    aOutline.transform(rCandidate.getObjectTransformation());

                    mbHit = checkHairlineHitWithTolerance(aOutline, getDiscreteHitTolerance());
                }
            }
        }

        void HitTestProcessor2D::processBasePrimitive2D(const primitive2d::BasePrimitive2D& rCandidate)
        {
            if(getHit())
            {
                // stop processing as soon as a hit was recognized
                return;
            }

            switch(rCandidate.getPrimitive2DID())
            {
                case PRIMITIVE2D_ID_TRANSFORMPRIMITIVE2D :
                {
                    // remember current ViewInformation2D
                    const primitive2d::TransformPrimitive2D& rTransformCandidate(static_cast< const primitive2d::TransformPrimitive2D& >(rCandidate));
                    const geometry::ViewInformation2D aLastViewInformation2D(getViewInformation2D());

                    // create new local ViewInformation2D containing transformation
                    geometry::ViewInformation2D aViewInformation2D(getViewInformation2D());
                    aViewInformation2D.setObjectTransformation(getViewInformation2D().getObjectTransformation() * rTransformCandidate.getTransformation());
                    setViewInformation2D(aViewInformation2D);

                    // process child content recursively
                    process(rTransformCandidate.getChildren());

                    // restore transformations
                    setViewInformation2D(aLastViewInformation2D);

                    break;
                }
                case PRIMITIVE2D_ID_POLYGONHAIRLINEPRIMITIVE2D :
                {
                    if(!getHitTextOnly())
                    {
                        // create hairline in discrete coordinates
                        const primitive2d::PolygonHairlinePrimitive2D& rPolygonCandidate(static_cast< const primitive2d::PolygonHairlinePrimitive2D& >(rCandidate));

                        // use hairline test
                        mbHit = checkHairlineHitWithTolerance(rPolygonCandidate.getB2DPolygon(), getDiscreteHitTolerance());
                    }

                    break;
                }
                case PRIMITIVE2D_ID_POLYGONMARKERPRIMITIVE2D :
                {
                    if(!getHitTextOnly())
                    {
                        // handle marker like hairline; no need to decompose in dashes
                        const primitive2d::PolygonMarkerPrimitive2D& rPolygonCandidate(static_cast< const primitive2d::PolygonMarkerPrimitive2D& >(rCandidate));

                        // use hairline test
                        mbHit = checkHairlineHitWithTolerance(rPolygonCandidate.getB2DPolygon(), getDiscreteHitTolerance());
                    }

                    break;
                }
                case PRIMITIVE2D_ID_POLYGONSTROKEPRIMITIVE2D :
                {
                    if(!getHitTextOnly())
                    {
                        // handle stroke evtl. directly; no need to decompose to filled polygon outlines
                        const primitive2d::PolygonStrokePrimitive2D& rPolygonCandidate(static_cast< const primitive2d::PolygonStrokePrimitive2D& >(rCandidate));
                        const attribute::LineAttribute& rLineAttribute = rPolygonCandidate.getLineAttribute();

                        if(rLineAttribute.getWidth() > 0.0)
                        {
                            if(basegfx::B2DLineJoin::Miter == rLineAttribute.getLineJoin())
                            {
                                // if line is mitered, use decomposition since mitered line
                                // geometry may use more space than the geometry grown by half line width
                                process(rCandidate);
                            }
                            else
                            {
                                // for all other B2DLINEJOIN_* do a hairline HitTest with expanded tolerance
                                const basegfx::B2DVector aDiscreteHalfLineVector(getViewInformation2D().getObjectToViewTransformation()
                                    * basegfx::B2DVector(rLineAttribute.getWidth() * 0.5, rLineAttribute.getWidth() * 0.5));
                                mbHit = checkHairlineHitWithTolerance(
                                    rPolygonCandidate.getB2DPolygon(),
                                    getDiscreteHitTolerance() + aDiscreteHalfLineVector);
                            }
                        }
                        else
                        {
                            // hairline; fallback to hairline test. Do not decompose
                            // since this may decompose the hairline to dashes
                            mbHit = checkHairlineHitWithTolerance(rPolygonCandidate.getB2DPolygon(), getDiscreteHitTolerance());
                        }
                    }

                    break;
                }
                case PRIMITIVE2D_ID_POLYGONWAVEPRIMITIVE2D :
                {
                    if(!getHitTextOnly())
                    {
                        // do not use decompose; just handle like a line with width
                        const primitive2d::PolygonWavePrimitive2D& rPolygonCandidate(static_cast< const primitive2d::PolygonWavePrimitive2D& >(rCandidate));
                        double fLogicHitTolerance(0.0);

                        // if WaveHeight, grow by it
                        if(rPolygonCandidate.getWaveHeight() > 0.0)
                        {
                            fLogicHitTolerance += rPolygonCandidate.getWaveHeight();
                        }

                        // if line width, grow by it
                        if(rPolygonCandidate.getLineAttribute().getWidth() > 0.0)
                        {
                            fLogicHitTolerance += rPolygonCandidate.getLineAttribute().getWidth() * 0.5;
                        }

                        const basegfx::B2DVector aDiscreteHalfLineVector(getViewInformation2D().getObjectToViewTransformation()
                            * basegfx::B2DVector(fLogicHitTolerance, fLogicHitTolerance));

                        mbHit = checkHairlineHitWithTolerance(
                            rPolygonCandidate.getB2DPolygon(),
                            getDiscreteHitTolerance() + aDiscreteHalfLineVector);
                    }

                    break;
                }
                case PRIMITIVE2D_ID_POLYPOLYGONCOLORPRIMITIVE2D :
                {
                    if(!getHitTextOnly())
                    {
                        // create filled polyPolygon in discrete coordinates
                        const primitive2d::PolyPolygonColorPrimitive2D& rPolygonCandidate(static_cast< const primitive2d::PolyPolygonColorPrimitive2D& >(rCandidate));

                        // use fill hit test
                        mbHit = checkFillHitWithTolerance(rPolygonCandidate.getB2DPolyPolygon(), getDiscreteHitTolerance());
                    }

                    break;
                }
                case PRIMITIVE2D_ID_TRANSPARENCEPRIMITIVE2D :
                {
                    // sub-transparence group
                    const primitive2d::TransparencePrimitive2D& rTransCandidate(static_cast< const primitive2d::TransparencePrimitive2D& >(rCandidate));

                    // Currently the transparence content is not taken into account; only
                    // the children are recursively checked for hit. This may be refined for
                    // parts where the content is completely transparent if needed.
                    process(rTransCandidate.getChildren());

                    break;
                }
                case PRIMITIVE2D_ID_MASKPRIMITIVE2D :
                {
                    // create mask in discrete coordinates; only recursively continue
                    // with content when HitTest position is inside the mask
                    const primitive2d::MaskPrimitive2D& rMaskCandidate(static_cast< const primitive2d::MaskPrimitive2D& >(rCandidate));

                    // use fill hit test
                    if(checkFillHitWithTolerance(rMaskCandidate.getMask(), getDiscreteHitTolerance()))
                    {
                        // recursively HitTest children
                        process(rMaskCandidate.getChildren());
                    }

                    break;
                }
                case PRIMITIVE2D_ID_SCENEPRIMITIVE2D :
                {
                    if(!getHitTextOnly())
                    {
                        const primitive2d::ScenePrimitive2D& rScenePrimitive2D(
                            static_cast< const primitive2d::ScenePrimitive2D& >(rCandidate));
                        check3DHit(rScenePrimitive2D);
                    }

                    break;
                }
                case PRIMITIVE2D_ID_WRONGSPELLPRIMITIVE2D :
                case PRIMITIVE2D_ID_MARKERARRAYPRIMITIVE2D :
                case PRIMITIVE2D_ID_GRIDPRIMITIVE2D :
                case PRIMITIVE2D_ID_HELPLINEPRIMITIVE2D :
                {
                    // ignorable primitives
                    break;
                }
                case PRIMITIVE2D_ID_SHADOWPRIMITIVE2D :
                {
                    // Ignore shadows; we do not want to have shadows hittable.
                    // Remove this one to make shadows hittable on demand.
                    break;
                }
                case PRIMITIVE2D_ID_TEXTSIMPLEPORTIONPRIMITIVE2D :
                case PRIMITIVE2D_ID_TEXTDECORATEDPORTIONPRIMITIVE2D :
                {
                    // for text use the BoundRect of the primitive itself
                    const basegfx::B2DRange aRange(rCandidate.getB2DRange(getViewInformation2D()));

                    if(!aRange.isEmpty())
                    {
                        const basegfx::B2DPolygon aOutline(basegfx::utils::createPolygonFromRect(aRange));
                        mbHit = checkFillHitWithTolerance(basegfx::B2DPolyPolygon(aOutline), getDiscreteHitTolerance());
                    }

                    break;
                }
                case PRIMITIVE2D_ID_BITMAPALPHAPRIMITIVE2D :
                {
                    // avoid decompose of this primitive by handling directly
                    const primitive2d::BitmapAlphaPrimitive2D& rBitmapAlphaCandidate(static_cast< const primitive2d::BitmapAlphaPrimitive2D& >(rCandidate));

                    if (!basegfx::fTools::equal(rBitmapAlphaCandidate.getTransparency(), 1.0))
                    {
                        checkBitmapHit(
                            rCandidate.getB2DRange(getViewInformation2D()),
                            rBitmapAlphaCandidate.getBitmap(),
                            rBitmapAlphaCandidate.getTransform());
                    }
                    break;
                }
                case PRIMITIVE2D_ID_BITMAPPRIMITIVE2D :
                {
                    // use common tooling
                    const primitive2d::BitmapPrimitive2D& rBitmapCandidate(static_cast< const primitive2d::BitmapPrimitive2D& >(rCandidate));
                    checkBitmapHit(
                        rCandidate.getB2DRange(getViewInformation2D()),
                        rBitmapCandidate.getBitmap(),
                        rBitmapCandidate.getTransform());
                    break;
                }
                case PRIMITIVE2D_ID_METAFILEPRIMITIVE2D :
                case PRIMITIVE2D_ID_CONTROLPRIMITIVE2D :
                case PRIMITIVE2D_ID_FILLGRADIENTPRIMITIVE2D :
                case PRIMITIVE2D_ID_FILLGRAPHICPRIMITIVE2D :
                case PRIMITIVE2D_ID_FILLHATCHPRIMITIVE2D :
                case PRIMITIVE2D_ID_PAGEPREVIEWPRIMITIVE2D :
                case PRIMITIVE2D_ID_MEDIAPRIMITIVE2D:
                case PRIMITIVE2D_ID_ANIMATEDGRAPHICPRIMITIVE2D:
                {
                    if(!getHitTextOnly())
                    {
                        // Class of primitives for which just the BoundRect of the primitive itself
                        // will be used for HitTest currently.
                        //
                        // This may be refined in the future, e.g:
                        // - For Bitmaps, the mask and/or transparence information may be used
                        // - For MetaFiles, the MetaFile content may be used
                        const basegfx::B2DRange aRange(rCandidate.getB2DRange(getViewInformation2D()));

                        if(!aRange.isEmpty())
                        {
                            const basegfx::B2DPolygon aOutline(basegfx::utils::createPolygonFromRect(aRange));
                            mbHit = checkFillHitWithTolerance(basegfx::B2DPolyPolygon(aOutline), getDiscreteHitTolerance());
                        }
                    }

                    break;
                }
                case PRIMITIVE2D_ID_HIDDENGEOMETRYPRIMITIVE2D :
                {
                    // HiddenGeometryPrimitive2D; the default decomposition would return an empty sequence,
                    // so force this primitive to process its children directly if the switch is set
                    // (which is the default). Else, ignore invisible content
                    const primitive2d::HiddenGeometryPrimitive2D& rHiddenGeometry(static_cast< const primitive2d::HiddenGeometryPrimitive2D& >(rCandidate));
                    const primitive2d::Primitive2DContainer& rChildren = rHiddenGeometry.getChildren();

                    if(!rChildren.empty())
                    {
                        process(rChildren);
                    }

                    break;
                }
                case PRIMITIVE2D_ID_POINTARRAYPRIMITIVE2D :
                {
                    if(!getHitTextOnly())
                    {
                        const primitive2d::PointArrayPrimitive2D& rPointArrayCandidate(static_cast< const primitive2d::PointArrayPrimitive2D& >(rCandidate));
                        const std::vector< basegfx::B2DPoint >& rPositions = rPointArrayCandidate.getPositions();
                        const sal_uInt32 nCount(rPositions.size());

                        for(sal_uInt32 a(0); !getHit() && a < nCount; a++)
                        {
                            const basegfx::B2DPoint aPosition(getViewInformation2D().getObjectToViewTransformation() * rPositions[a]);
                            const basegfx::B2DVector aDistance(aPosition - getDiscreteHitPosition());

                            if (aDistance.getLength() <= std::max(getDiscreteHitTolerance().getX(),
                                                                  getDiscreteHitTolerance().getY()))
                            {
                                mbHit = true;
                            }
                        }
                    }

                    break;
                }
                default :
                {
                    // process recursively
                    process(rCandidate);

                    break;
                }
            }

            if (getHit() && getCollectHitStack())
            {
                /// push candidate to HitStack to create it. This only happens when a hit is found and
                /// creating the HitStack was requested (see collectHitStack)
                maHitStack.append(const_cast< primitive2d::BasePrimitive2D* >(&rCandidate));
            }
        }

} // end of namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
