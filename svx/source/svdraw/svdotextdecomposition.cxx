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

#include <svx/compatflags.hxx>
#include <svx/svdetc.hxx>
#include <svx/svdoutl.hxx>
#include <svx/svdpage.hxx>
#include <svx/svdotext.hxx>
#include <svx/svdmodel.hxx>
#include <svx/sdasitm.hxx>
#include <textchain.hxx>
#include <textchainflow.hxx>
#include <svx/sdtacitm.hxx>
#include <svx/sdtayitm.hxx>
#include <svx/sdtaiitm.hxx>
#include <svx/sdtaaitm.hxx>
#include <svx/xfillit0.hxx>
#include <svx/xbtmpit.hxx>
#include <basegfx/vector/b2dvector.hxx>
#include <sdr/primitive2d/sdrtextprimitive2d.hxx>
#include <drawinglayer/primitive2d/drawinglayer_primitivetypes2d.hxx>
#include <drawinglayer/primitive2d/textbreakuphelper.hxx>
#include <drawinglayer/primitive2d/textprimitive2d.hxx>
#include <drawinglayer/primitive2d/textdecoratedprimitive2d.hxx>
#include <basegfx/range/b2drange.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/editstat.hxx>
#include <editeng/smallcaps.hxx>
#include <tools/helpers.hxx>
#include <svl/itemset.hxx>
#include <drawinglayer/animation/animationtiming.hxx>
#include <basegfx/color/bcolor.hxx>
#include <vcl/svapp.hxx>
#include <editeng/escapementitem.hxx>
#include <editeng/svxenum.hxx>
#include <editeng/flditem.hxx>
#include <editeng/adjustitem.hxx>
#include <drawinglayer/primitive2d/texthierarchyprimitive2d.hxx>
#include <drawinglayer/primitive2d/wrongspellprimitive2d.hxx>
#include <drawinglayer/primitive2d/graphicprimitive2d.hxx>
#include <drawinglayer/primitive2d/textlayoutdevice.hxx>
#include <drawinglayer/primitive2d/unifiedtransparenceprimitive2d.hxx>
#include <svx/unoapi.hxx>
#include <drawinglayer/geometry/viewinformation2d.hxx>
#include <editeng/outlobj.hxx>
#include <basegfx/matrix/b2dhommatrixtools.hxx>
#include <sal/log.hxx>
#include <osl/diagnose.h>
#include <comphelper/lok.hxx>

using namespace com::sun::star;

// helpers
namespace
{
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> buildTextPortionPrimitive(const DrawPortionInfo& rInfo, const OUString& rText,
        const drawinglayer::attribute::FontAttribute& rFontAttribute,
        const std::vector<double>& rDXArray,
        const basegfx::B2DHomMatrix& rNewTransform);

    // static rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> impCheckFieldPrimitive(drawinglayer::primitive2d::BasePrimitive2D* pPrimitive, const DrawPortionInfo& rInfo);
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> CheckFieldPrimitive(drawinglayer::primitive2d::BasePrimitive2D* pPrimitive, const DrawPortionInfo& rInfo)
    {
        rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> xRet = pPrimitive;
        if(rInfo.mpFieldData)
        {
            // Support for FIELD_SEQ_BEGIN, FIELD_SEQ_END. If used, create a TextHierarchyFieldPrimitive2D
            // which holds the field type and, if applicable, the URL
            const SvxURLField* pURLField = dynamic_cast< const SvxURLField* >(rInfo.mpFieldData);
            const SvxPageField* pPageField = dynamic_cast< const SvxPageField* >(rInfo.mpFieldData);

            // embed current primitive to a sequence
            drawinglayer::primitive2d::Primitive2DContainer aSequence;

            if(pPrimitive)
            {
                aSequence.resize(1);
                aSequence[0] = drawinglayer::primitive2d::Primitive2DReference(pPrimitive);
            }

            if(pURLField)
            {
                // extended this to hold more of the contents of the original
                // SvxURLField since that stuff is still used in HitTest and e.g. Calc
                std::vector< std::pair< OUString, OUString>> meValues;
                meValues.emplace_back("URL", pURLField->GetURL());
                meValues.emplace_back("Representation", pURLField->GetRepresentation());
                meValues.emplace_back("TargetFrame", pURLField->GetTargetFrame());
                meValues.emplace_back("AltText", pURLField->GetName());
                meValues.emplace_back("SvxURLFormat", OUString::number(static_cast<sal_uInt16>(pURLField->GetFormat())));
                xRet = new drawinglayer::primitive2d::TextHierarchyFieldPrimitive2D(std::move(aSequence), drawinglayer::primitive2d::FIELD_TYPE_URL, &meValues);
            }
            else if(pPageField)
            {
                xRet = new drawinglayer::primitive2d::TextHierarchyFieldPrimitive2D(std::move(aSequence), drawinglayer::primitive2d::FIELD_TYPE_PAGE);
            }
            else
            {
                xRet = new drawinglayer::primitive2d::TextHierarchyFieldPrimitive2D(std::move(aSequence), drawinglayer::primitive2d::FIELD_TYPE_COMMON);
            }
        }

        return xRet;
    }

    class DoCapitalsDrawPortionInfo : public SvxDoCapitals
    {
    private:
        drawinglayer::primitive2d::Primitive2DContainer& mrTarget;
        const basegfx::B2DHomMatrix& mrNewTransformA;
        const basegfx::B2DHomMatrix& mrNewTransformB;
        const DrawPortionInfo& m_rInfo;
        SvxFont m_aFont;
    public:
        DoCapitalsDrawPortionInfo(
            drawinglayer::primitive2d::Primitive2DContainer& rTarget,
            const basegfx::B2DHomMatrix& rNewTransformA,
            const basegfx::B2DHomMatrix& rNewTransformB,
            const DrawPortionInfo& rInfo)
        : SvxDoCapitals(rInfo.maText, rInfo.mnTextStart, rInfo.mnTextLen)
            , mrTarget(rTarget)
            , mrNewTransformA(rNewTransformA)
            , mrNewTransformB(rNewTransformB)
            , m_rInfo(rInfo)
            , m_aFont(rInfo.mrFont)
        {
            assert(!m_rInfo.mpDXArray.empty());

            /* turn all these off as they are handled outside subportions for the whole portion */
            m_aFont.SetTransparent(false);
            m_aFont.SetUnderline(LINESTYLE_NONE);
            m_aFont.SetOverline(LINESTYLE_NONE);
            m_aFont.SetStrikeout(STRIKEOUT_NONE);

            m_aFont.SetCaseMap(SvxCaseMap::NotMapped); /* otherwise this would call itself */
        }
        virtual void Do( const OUString &rSpanTxt, const sal_Int32 nSpanIdx,
                        const sal_Int32 nSpanLen, const bool bUpper ) override
        {
            sal_uInt8 nProp(0);
            if (!bUpper)
            {
                nProp = m_aFont.GetPropr();
                m_aFont.SetProprRel(SMALL_CAPS_PERCENTAGE);
            }

            sal_Int32 nStartOffset = nSpanIdx - nIdx;
            double nStartX = nStartOffset ? m_rInfo.mpDXArray[nStartOffset - 1] : 0;

            Point aStartPos(m_rInfo.mrStartPos.X() + nStartX, m_rInfo.mrStartPos.Y());

            KernArray aDXArray;
            aDXArray.resize(nSpanLen);
            for (sal_Int32 i = 0; i < nSpanLen; ++i)
                aDXArray[i] = m_rInfo.mpDXArray[nStartOffset + i] - nStartX;

            auto aKashidaArray = !m_rInfo.mpKashidaArray.empty() ?
                std::span<const sal_Bool>(m_rInfo.mpKashidaArray.data() + nStartOffset, nSpanLen) :
                std::span<const sal_Bool>();

            DrawPortionInfo aInfo(aStartPos, rSpanTxt,
                                nSpanIdx, nSpanLen,
                                aDXArray, aKashidaArray,
                                m_aFont, m_rInfo.mnPara,
                                m_rInfo.mnBiDiLevel,
                                nullptr, /* no spelling in subportion, handled outside */
                                nullptr, /* no field in subportion, handled outside */
                                false, false, false,
                                m_rInfo.mpLocale, m_rInfo.maOverlineColor, m_rInfo.maTextLineColor);

            CreateTextPortionPrimitivesFromDrawPortionInfo(
                mrTarget,
                mrNewTransformA,
                mrNewTransformB,
                aInfo);

            if (!bUpper)
                m_aFont.SetPropr(nProp);
        }
    };
} // end of anonymous namespace

void CreateTextPortionPrimitivesFromDrawPortionInfo(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    const basegfx::B2DHomMatrix& rNewTransformA,
    const basegfx::B2DHomMatrix& rNewTransformB,
    const DrawPortionInfo& rInfo)
{
    if(rInfo.maText.isEmpty() || !rInfo.mnTextLen)
        return;

    basegfx::B2DVector aFontScaling;
    drawinglayer::attribute::FontAttribute aFontAttribute(
        drawinglayer::primitive2d::getFontAttributeFromVclFont(
            aFontScaling,
            rInfo.mrFont,
            rInfo.IsRTL(),
            false));
    basegfx::B2DHomMatrix aNewTransform;

    // add font scale to new transform
    aNewTransform.scale(aFontScaling.getX(), aFontScaling.getY());

    // look for proportional font scaling, if necessary, scale accordingly
    sal_Int8 nPropr(rInfo.mrFont.GetPropr());
    const double fPropFontFactor(nPropr / 100.0);
    if (100 != nPropr)
        aNewTransform.scale(fPropFontFactor, fPropFontFactor);

    // apply font rotate
    if(rInfo.mrFont.GetOrientation())
    {
        aNewTransform.rotate(-toRadians(rInfo.mrFont.GetOrientation()));
    }

    // look for escapement, if necessary, translate accordingly
    if(rInfo.mrFont.GetEscapement())
    {
        sal_Int16 nEsc(rInfo.mrFont.GetEscapement());

        if(DFLT_ESC_AUTO_SUPER == nEsc)
        {
            nEsc = .8 * (100 - nPropr);
            assert (nEsc == DFLT_ESC_SUPER && "I'm sure this formula needs to be changed, but how to confirm that???");
            nEsc = DFLT_ESC_SUPER;
        }
        else if(DFLT_ESC_AUTO_SUB == nEsc)
        {
            nEsc = .2 * -(100 - nPropr);
            assert (nEsc == -20 && "I'm sure this formula needs to be changed, but how to confirm that???");
            nEsc = -20;
        }

        if(nEsc > MAX_ESC_POS)
        {
            nEsc = MAX_ESC_POS;
        }
        else if(nEsc < -MAX_ESC_POS)
        {
            nEsc = -MAX_ESC_POS;
        }

        const double fEscapement(nEsc / -100.0);
        aNewTransform.translate(0.0, fEscapement * aFontScaling.getY());
    }

    // apply transformA
    aNewTransform *= rNewTransformA;

    // apply local offset
    aNewTransform.translate(rInfo.mrStartPos.X(), rInfo.mrStartPos.Y());

    // also apply embedding object's transform
    aNewTransform *= rNewTransformB;

    // prepare DXArray content. To make it independent from font size (and such from
    // the text transformation), scale it to unit coordinates
    ::std::vector< double > aDXArray;

    if (!rInfo.mpDXArray.empty())
    {
        aDXArray.reserve(rInfo.mnTextLen);
        for(sal_Int32 a=0; a < rInfo.mnTextLen; a++)
        {
            aDXArray.push_back(rInfo.mpDXArray[a]);
        }
    }

    OUString caseMappedText = rInfo.mrFont.CalcCaseMap(rInfo.maText);
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> pNewPrimitive(buildTextPortionPrimitive(rInfo, caseMappedText,
                                                                                                        aFontAttribute,
                                                                                                        aDXArray, aNewTransform));

    bool bSmallCaps = rInfo.mrFont.IsCapital();
    if (bSmallCaps && rInfo.mpDXArray.empty())
    {
        SAL_WARN("svx", "SmallCaps requested with DXArray, abandoning");
        bSmallCaps = false;
    }
    if (bSmallCaps)
    {
        // rerun with each sub-portion
        DoCapitalsDrawPortionInfo aDoDrawPortionInfo(
            rTarget,
            rNewTransformA,
            rNewTransformB,
            rInfo);
        rInfo.mrFont.DoOnCapitals(aDoDrawPortionInfo);

        // transfer collected primitives from rTarget to a new container
        drawinglayer::primitive2d::Primitive2DContainer aContainer = std::move(rTarget);

        // Take any decoration for the whole formatted portion and keep it to get continuous over/under/strike-through
        if (pNewPrimitive->getPrimitive2DID() == PRIMITIVE2D_ID_TEXTDECORATEDPORTIONPRIMITIVE2D)
        {
            const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D* pTCPP =
                static_cast<const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D*>(pNewPrimitive.get());

            if (pTCPP->getWordLineMode()) // single word mode: 'Individual words' in UI
            {
                // Split to single word primitives using TextBreakupHelper
                drawinglayer::primitive2d::TextBreakupHelper aTextBreakupHelper(*pTCPP);
                drawinglayer::primitive2d::Primitive2DContainer aBroken(aTextBreakupHelper.extractResult(drawinglayer::primitive2d::BreakupUnit::Word));
                for (auto& rPortion : aBroken)
                {
                    assert(rPortion->getPrimitive2DID() == PRIMITIVE2D_ID_TEXTDECORATEDPORTIONPRIMITIVE2D &&
                            "TextBreakupHelper generates same output primitive type as input");

                    const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D* pPortion =
                        static_cast<const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D*>(rPortion.get());

                    // create and add decoration
                    const drawinglayer::primitive2d::Primitive2DContainer& rDecorationGeometryContent(
                        pPortion->getOrCreateDecorationGeometryContent(
                            pPortion->getTextTransform(),
                            caseMappedText,
                            pPortion->getTextPosition(),
                            pPortion->getTextLength(),
                            pPortion->getDXArray()));

                    aContainer.insert(aContainer.end(), rDecorationGeometryContent.begin(), rDecorationGeometryContent.end());
                }
            }
            else
            {
                // create and add decoration
                const drawinglayer::primitive2d::Primitive2DContainer& rDecorationGeometryContent(
                    pTCPP->getOrCreateDecorationGeometryContent(
                        pTCPP->getTextTransform(),
                        caseMappedText,
                        rInfo.mnTextStart,
                        rInfo.mnTextLen,
                        aDXArray));

                aContainer.insert(aContainer.end(), rDecorationGeometryContent.begin(), rDecorationGeometryContent.end());
            }
        }

        pNewPrimitive = new drawinglayer::primitive2d::GroupPrimitive2D(std::move(aContainer));
    }

    const Color aFontColor(rInfo.mrFont.GetColor());
    if (aFontColor.IsTransparent())
    {
        // Handle semi-transparent text for both the decorated and simple case here.
        pNewPrimitive = new drawinglayer::primitive2d::UnifiedTransparencePrimitive2D(
            drawinglayer::primitive2d::Primitive2DContainer{ pNewPrimitive },
            (255 - aFontColor.GetAlpha()) / 255.0);
    }

    if(rInfo.mbEndOfBullet)
    {
        // embed in TextHierarchyBulletPrimitive2D
        drawinglayer::primitive2d::Primitive2DContainer aNewSequence { pNewPrimitive };
        pNewPrimitive = new drawinglayer::primitive2d::TextHierarchyBulletPrimitive2D(std::move(aNewSequence));
    }

    if(rInfo.mpFieldData)
    {
        pNewPrimitive = CheckFieldPrimitive(pNewPrimitive.get(), rInfo);
    }

    rTarget.push_back(pNewPrimitive);

    // support for WrongSpellVector. Create WrongSpellPrimitives as needed
    if(!rInfo.mpWrongSpellVector || aDXArray.empty())
        return;

    const sal_Int32 nSize(rInfo.mpWrongSpellVector->size());
    const sal_Int32 nDXCount(aDXArray.size());
    const basegfx::BColor aSpellColor(1.0, 0.0, 0.0); // red, hard coded

    for(sal_Int32 a(0); a < nSize; a++)
    {
        const EEngineData::WrongSpellClass& rCandidate = (*rInfo.mpWrongSpellVector)[a];

        if(rCandidate.nStart >= rInfo.mnTextStart && rCandidate.nEnd >= rInfo.mnTextStart && rCandidate.nEnd > rCandidate.nStart)
        {
            const sal_Int32 nStart(rCandidate.nStart - rInfo.mnTextStart);
            const sal_Int32 nEnd(rCandidate.nEnd - rInfo.mnTextStart);
            double fStart(0.0);
            double fEnd(0.0);

            if(nStart > 0 && nStart - 1 < nDXCount)
            {
                fStart = aDXArray[nStart - 1];
            }

            if(nEnd > 0 && nEnd - 1 < nDXCount)
            {
                fEnd = aDXArray[nEnd - 1];
            }

            if(!basegfx::fTools::equal(fStart, fEnd))
            {
                if(rInfo.IsRTL())
                {
                    // #i98523#
                    // When the portion is RTL, mirror the redlining using the
                    // full portion width
                    const double fTextWidth(aDXArray[aDXArray.size() - 1]);

                    fStart = fTextWidth - fStart;
                    fEnd = fTextWidth - fEnd;
                }

                // need to take FontScaling out of values; it's already part of
                // aNewTransform and would be double applied
                const double fFontScaleX(aFontScaling.getX() * fPropFontFactor);

                if(!basegfx::fTools::equal(fFontScaleX, 1.0)
                    && !basegfx::fTools::equalZero(fFontScaleX))
                {
                    fStart /= fFontScaleX;
                    fEnd /= fFontScaleX;
                }

                rTarget.push_back(new drawinglayer::primitive2d::WrongSpellPrimitive2D(
                    aNewTransform,
                    fStart,
                    fEnd,
                    aSpellColor));
            }
        }
    }
}

void CreateDrawBulletPrimitivesFromDrawBulletInfo(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    const basegfx::B2DHomMatrix& rNewTransformA,
    const basegfx::B2DHomMatrix& rNewTransformB,
    const DrawBulletInfo& rInfo)
{
    basegfx::B2DHomMatrix aNewTransform;

    // add size to new transform
    aNewTransform.scale(rInfo.maBulletSize.getWidth(), rInfo.maBulletSize.getHeight());

    // apply transformA
    aNewTransform *= rNewTransformA;

    // apply local offset
    aNewTransform.translate(rInfo.maBulletPosition.X(), rInfo.maBulletPosition.Y());

    // also apply embedding object's transform
    aNewTransform *= rNewTransformB;

    // prepare empty GraphicAttr
    const GraphicAttr aGraphicAttr;

    // create GraphicPrimitive2D
    const drawinglayer::primitive2d::Primitive2DReference aNewReference(new drawinglayer::primitive2d::GraphicPrimitive2D(
        aNewTransform,
        rInfo.maBulletGraphicObject,
        aGraphicAttr));

    // embed in TextHierarchyBulletPrimitive2D
    drawinglayer::primitive2d::Primitive2DContainer aNewSequence { aNewReference };
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> pNewPrimitive = new drawinglayer::primitive2d::TextHierarchyBulletPrimitive2D(std::move(aNewSequence));

    // add to output
    rTarget.push_back(pNewPrimitive);
}

namespace
{
    class impTextBreakupHandler
    {
    private:
        drawinglayer::primitive2d::Primitive2DContainer             maTextPortionPrimitives;
        drawinglayer::primitive2d::Primitive2DContainer             maLinePrimitives;
        drawinglayer::primitive2d::Primitive2DContainer             maParagraphPrimitives;

        SdrOutliner&                                                mrOutliner;
        basegfx::B2DHomMatrix                                       maNewTransformA;
        basegfx::B2DHomMatrix                                       maNewTransformB;

        // the visible area for contour text decomposition
        basegfx::B2DVector                                          maScale;

        // ClipRange for BlockText decomposition; only text portions completely
        // inside are to be accepted, so this is different from geometric clipping
        // (which would allow e.g. upper parts of portions to remain). Only used for
        // BlockText (see there)
        basegfx::B2DRange                                           maClipRange;

        void impFlushTextPortionPrimitivesToLinePrimitives();
        void impFlushLinePrimitivesToParagraphPrimitives(sal_Int32 nPara);
        void impHandleDrawPortionInfo(const DrawPortionInfo& rInfo);
        void impHandleDrawBulletInfo(const DrawBulletInfo& rInfo);

    public:
        explicit impTextBreakupHandler(SdrOutliner& rOutliner)
        :   mrOutliner(rOutliner)
        {
        }

        void decomposeContourTextPrimitive(const basegfx::B2DHomMatrix& rNewTransformA, const basegfx::B2DHomMatrix& rNewTransformB, const basegfx::B2DVector& rScale)
        {
            maScale = rScale;
            maNewTransformA = rNewTransformA;
            maNewTransformB = rNewTransformB;

            mrOutliner.StripPortions(
                [this](const DrawPortionInfo& rInfo){
                    // for contour text, ignore (clip away) all portions which are below
                    // the visible area given by maScale
                    if(static_cast<double>(rInfo.mrStartPos.Y()) < maScale.getY())
                    {
                        impHandleDrawPortionInfo(rInfo);
                    }
                },
                [this](const DrawBulletInfo& rInfo){ impHandleDrawBulletInfo(rInfo); });
        }

        void decomposeBlockTextPrimitive(
            const basegfx::B2DHomMatrix& rNewTransformA,
            const basegfx::B2DHomMatrix& rNewTransformB,
            const basegfx::B2DRange& rClipRange)
        {
            maNewTransformA = rNewTransformA;
            maNewTransformB = rNewTransformB;
            maClipRange = rClipRange;

            mrOutliner.StripPortions(
                [this](const DrawPortionInfo& rInfo){
                    // Is clipping wanted? This is text clipping; only accept a portion
                    // if it's completely in the range
                    if(!maClipRange.isEmpty())
                    {
                        // Test start position first; this allows to not get the text range at
                        // all if text is far outside
                        const basegfx::B2DPoint aStartPosition(rInfo.mrStartPos.X(), rInfo.mrStartPos.Y());

                        if(!maClipRange.isInside(aStartPosition))
                        {
                            return;
                        }

                        // Start position is inside. Get TextBoundRect and TopLeft next
                        drawinglayer::primitive2d::TextLayouterDevice aTextLayouterDevice;
                        aTextLayouterDevice.setFont(rInfo.mrFont);

                        const basegfx::B2DRange aTextBoundRect(
                            aTextLayouterDevice.getTextBoundRect(
                                rInfo.maText, rInfo.mnTextStart, rInfo.mnTextLen));
                        const basegfx::B2DPoint aTopLeft(aTextBoundRect.getMinimum() + aStartPosition);

                        if(!maClipRange.isInside(aTopLeft))
                        {
                            return;
                        }

                        // TopLeft is inside. Get BottomRight and check
                        const basegfx::B2DPoint aBottomRight(aTextBoundRect.getMaximum() + aStartPosition);

                        if(!maClipRange.isInside(aBottomRight))
                        {
                            return;
                        }

                        // all inside, clip was successful
                    }
                    impHandleDrawPortionInfo(rInfo);
                },
                [this](const DrawBulletInfo& rInfo){ impHandleDrawBulletInfo(rInfo); });
        }

        void decomposeStretchTextPrimitive(const basegfx::B2DHomMatrix& rNewTransformA, const basegfx::B2DHomMatrix& rNewTransformB)
        {
            maNewTransformA = rNewTransformA;
            maNewTransformB = rNewTransformB;

            mrOutliner.StripPortions(
                [this](const DrawPortionInfo& rInfo){ impHandleDrawPortionInfo(rInfo); },
                [this](const DrawBulletInfo& rInfo){ impHandleDrawBulletInfo(rInfo); });
        }

        drawinglayer::primitive2d::Primitive2DContainer extractPrimitive2DSequence();
    };

    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> buildTextPortionPrimitive(
            const DrawPortionInfo& rInfo, const OUString& rText,
            const drawinglayer::attribute::FontAttribute& rFontAttribute,
            const std::vector<double>& rDXArray,
            const basegfx::B2DHomMatrix& rNewTransform)
    {
        ::std::vector< sal_Bool > aKashidaArray;

        if(!rInfo.mpKashidaArray.empty() && rInfo.mnTextLen)
        {
            aKashidaArray.reserve(rInfo.mnTextLen);

            for(sal_Int32 a=0; a < rInfo.mnTextLen; a++)
            {
                aKashidaArray.push_back(rInfo.mpKashidaArray[a]);
            }
        }

        // create complex text primitive and append
        const Color aFontColor(rInfo.mrFont.GetColor());
        const basegfx::BColor aBFontColor(aFontColor.getBColor());

        const Color aTextFillColor(rInfo.mrFont.GetFillColor());

        // prepare wordLineMode (for underline and strikeout)
        // NOT for bullet texts. It is set (this may be an error by itself), but needs to be suppressed to hinder e.g. '1)'
        // to be split which would not look like the original
        const bool bWordLineMode(rInfo.mrFont.IsWordLineMode() && !rInfo.mbEndOfBullet);

        // prepare new primitive
        rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> pNewPrimitive;
        const bool bDecoratedIsNeeded(
               LINESTYLE_NONE != rInfo.mrFont.GetOverline()
            || LINESTYLE_NONE != rInfo.mrFont.GetUnderline()
            || STRIKEOUT_NONE != rInfo.mrFont.GetStrikeout()
            || FontEmphasisMark::NONE != (rInfo.mrFont.GetEmphasisMark() & FontEmphasisMark::Style)
            || FontRelief::NONE != rInfo.mrFont.GetRelief()
            || rInfo.mrFont.IsShadow()
            || bWordLineMode);

        if(bDecoratedIsNeeded)
        {
            // TextDecoratedPortionPrimitive2D needed, prepare some more data
            // get overline and underline color. If it's on automatic (0xffffffff) use FontColor instead
            const Color aUnderlineColor(rInfo.maTextLineColor);
            const basegfx::BColor aBUnderlineColor((aUnderlineColor == COL_AUTO) ? aBFontColor : aUnderlineColor.getBColor());
            const Color aOverlineColor(rInfo.maOverlineColor);
            const basegfx::BColor aBOverlineColor((aOverlineColor == COL_AUTO) ? aBFontColor : aOverlineColor.getBColor());

            // prepare overline and underline data
            const drawinglayer::primitive2d::TextLine eFontOverline(
                drawinglayer::primitive2d::mapFontLineStyleToTextLine(rInfo.mrFont.GetOverline()));
            const drawinglayer::primitive2d::TextLine eFontLineStyle(
                drawinglayer::primitive2d::mapFontLineStyleToTextLine(rInfo.mrFont.GetUnderline()));

            // check UnderlineAbove
            const bool bUnderlineAbove(
                drawinglayer::primitive2d::TEXT_LINE_NONE != eFontLineStyle && rInfo.mrFont.IsUnderlineAbove());

            // prepare strikeout data
            const drawinglayer::primitive2d::TextStrikeout eTextStrikeout(
                drawinglayer::primitive2d::mapFontStrikeoutToTextStrikeout(rInfo.mrFont.GetStrikeout()));

            // prepare emphasis mark data
            drawinglayer::primitive2d::TextEmphasisMark eTextEmphasisMark(drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_NONE);

            switch(rInfo.mrFont.GetEmphasisMark() & FontEmphasisMark::Style)
            {
                case FontEmphasisMark::Dot : eTextEmphasisMark = drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_DOT; break;
                case FontEmphasisMark::Circle : eTextEmphasisMark = drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_CIRCLE; break;
                case FontEmphasisMark::Disc : eTextEmphasisMark = drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_DISC; break;
                case FontEmphasisMark::Accent : eTextEmphasisMark = drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_ACCENT; break;
                default: break;
            }

            const bool bEmphasisMarkAbove(rInfo.mrFont.GetEmphasisMark() & FontEmphasisMark::PosAbove);
            const bool bEmphasisMarkBelow(rInfo.mrFont.GetEmphasisMark() & FontEmphasisMark::PosBelow);

            // prepare font relief data
            drawinglayer::primitive2d::TextRelief eTextRelief(drawinglayer::primitive2d::TEXT_RELIEF_NONE);

            switch(rInfo.mrFont.GetRelief())
            {
                case FontRelief::Embossed : eTextRelief = drawinglayer::primitive2d::TEXT_RELIEF_EMBOSSED; break;
                case FontRelief::Engraved : eTextRelief = drawinglayer::primitive2d::TEXT_RELIEF_ENGRAVED; break;
                default : break; // RELIEF_NONE, FontRelief_FORCE_EQUAL_SIZE
            }

            // prepare shadow/outline data
            const bool bShadow(rInfo.mrFont.IsShadow());

            // TextDecoratedPortionPrimitive2D is needed, create one
            pNewPrimitive = new drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D(

                // attributes for TextSimplePortionPrimitive2D
                rNewTransform,
                rText,
                rInfo.mnTextStart,
                rInfo.mnTextLen,
                std::vector(rDXArray),
                std::move(aKashidaArray),
                rFontAttribute,
                rInfo.mpLocale ? *rInfo.mpLocale : css::lang::Locale(),
                aBFontColor,
                aTextFillColor,

                // attributes for TextDecoratedPortionPrimitive2D
                aBOverlineColor,
                aBUnderlineColor,
                eFontOverline,
                eFontLineStyle,
                bUnderlineAbove,
                eTextStrikeout,
                bWordLineMode,
                eTextEmphasisMark,
                bEmphasisMarkAbove,
                bEmphasisMarkBelow,
                eTextRelief,
                bShadow);
        }
        else
        {
            // TextSimplePortionPrimitive2D is enough
            pNewPrimitive = new drawinglayer::primitive2d::TextSimplePortionPrimitive2D(
                rNewTransform,
                rText,
                rInfo.mnTextStart,
                rInfo.mnTextLen,
                std::vector(rDXArray),
                std::move(aKashidaArray),
                rFontAttribute,
                rInfo.mpLocale ? *rInfo.mpLocale : css::lang::Locale(),
                aBFontColor,
                aTextFillColor);
        }

        return pNewPrimitive;
    }

    void impTextBreakupHandler::impFlushTextPortionPrimitivesToLinePrimitives()
    {
        // only create a line primitive when we had content; there is no need for
        // empty line primitives (contrary to paragraphs, see below).
        if(!maTextPortionPrimitives.empty())
        {
            maLinePrimitives.push_back(new drawinglayer::primitive2d::TextHierarchyLinePrimitive2D(std::move(maTextPortionPrimitives)));
        }
    }

    void impTextBreakupHandler::impFlushLinePrimitivesToParagraphPrimitives(sal_Int32 nPara)
    {
        sal_Int16 nDepth = mrOutliner.GetDepth(nPara);
        EBulletInfo eInfo = mrOutliner.GetBulletInfo(nPara);
        // Pass -1 to signal VclMetafileProcessor2D that there is no active
        // bullets/numbering in this paragraph (i.e. this is normal text)
        const sal_Int16 nOutlineLevel( eInfo.bVisible ?  nDepth : -1);

        // ALWAYS create a paragraph primitive, even when no content was added. This is done to
        // have the correct paragraph count even with empty paragraphs. Those paragraphs will
        // have an empty sub-PrimitiveSequence.
        maParagraphPrimitives.push_back(
            new drawinglayer::primitive2d::TextHierarchyParagraphPrimitive2D(
                std::move(maLinePrimitives),
                nOutlineLevel));
    }

    void impTextBreakupHandler::impHandleDrawPortionInfo(const DrawPortionInfo& rInfo)
    {
        CreateTextPortionPrimitivesFromDrawPortionInfo(
            maTextPortionPrimitives,
            maNewTransformA,
            maNewTransformB,
            rInfo);

        if(rInfo.mbEndOfLine || rInfo.mbEndOfParagraph)
        {
            impFlushTextPortionPrimitivesToLinePrimitives();
        }

        if(rInfo.mbEndOfParagraph)
        {
            impFlushLinePrimitivesToParagraphPrimitives(rInfo.mnPara);
        }
    }

    void impTextBreakupHandler::impHandleDrawBulletInfo(const DrawBulletInfo& rInfo)
    {
        CreateDrawBulletPrimitivesFromDrawBulletInfo(
            maTextPortionPrimitives,
            maNewTransformA,
            maNewTransformB,
            rInfo);
        basegfx::B2DHomMatrix aNewTransform;

        // add size to new transform
        aNewTransform.scale(rInfo.maBulletSize.getWidth(), rInfo.maBulletSize.getHeight());

        // apply transformA
        aNewTransform *= maNewTransformA;

        // apply local offset
        aNewTransform.translate(rInfo.maBulletPosition.X(), rInfo.maBulletPosition.Y());

        // also apply embedding object's transform
        aNewTransform *= maNewTransformB;

        // prepare empty GraphicAttr
        const GraphicAttr aGraphicAttr;

        // create GraphicPrimitive2D
        const drawinglayer::primitive2d::Primitive2DReference aNewReference(new drawinglayer::primitive2d::GraphicPrimitive2D(
            aNewTransform,
            rInfo.maBulletGraphicObject,
            aGraphicAttr));

        // embed in TextHierarchyBulletPrimitive2D
        drawinglayer::primitive2d::Primitive2DContainer aNewSequence { aNewReference };
        rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> pNewPrimitive = new drawinglayer::primitive2d::TextHierarchyBulletPrimitive2D(std::move(aNewSequence));

        // add to output
        maTextPortionPrimitives.push_back(pNewPrimitive);
    }

    drawinglayer::primitive2d::Primitive2DContainer impTextBreakupHandler::extractPrimitive2DSequence()
    {
        if(!maTextPortionPrimitives.empty())
        {
            // collect non-closed lines
            impFlushTextPortionPrimitivesToLinePrimitives();
        }

        if(!maLinePrimitives.empty())
        {
            // collect non-closed paragraphs
            impFlushLinePrimitivesToParagraphPrimitives(mrOutliner.GetParagraphCount() - 1);
        }

        return std::move(maParagraphPrimitives);
    }
} // end of anonymous namespace

// primitive decompositions
void SdrTextObj::impDecomposeContourTextPrimitive(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    const drawinglayer::primitive2d::SdrContourTextPrimitive2D& rSdrContourTextPrimitive,
    const drawinglayer::geometry::ViewInformation2D& aViewInformation) const
{
    basegfx::B2DHomMatrix aObjectMatrix = rSdrContourTextPrimitive.getObjectTransform();
    basegfx::B2DPolyPolygon aPolyPolygon(rSdrContourTextPrimitive.getUnitPolyPolygon());

    // decompose aObjectMatrix
    basegfx::B2DTuple aScale, aTranslate;
    double fRotate, fShearX;
    aObjectMatrix.decompose(aScale, aTranslate, fRotate, fShearX);

    // tdf#84507 The aPolyPolygon is not suitable for the text in case of rotate or shear.
    if (!basegfx::fTools::equalZero(fRotate) || !basegfx::fTools::equalZero(fShearX))
    {
        // unitPolyPolygon was build by inverse(aObjectMatrix) * PolyPolygon.
        // Restore to PolyPolygon.
        aPolyPolygon.transform(aObjectMatrix);

        // outliner expects an unrotated, unsheared polypolygon with top-left in origin.
        // Remember top-left of aPolyPolygon.
        basegfx::B2DTuple aTargetLeftTop = aPolyPolygon.getB2DRange().getMinimum();
        // Remove rotation if any
        basegfx::B2DHomMatrix aRemoveRotShear;
        if (!basegfx::fTools::equalZero(fRotate))
            aRemoveRotShear *= basegfx::utils::createRotateB2DHomMatrix(-fRotate);
        // Remove shear if any
        if (!basegfx::fTools::equalZero(fShearX))
            aRemoveRotShear *= basegfx::utils::createShearXB2DHomMatrix(-fShearX);
        aPolyPolygon.transform(aRemoveRotShear);
        // Move Top/Left to origin
        basegfx::B2DRange aBoundRange = aPolyPolygon.getB2DRange();
        aPolyPolygon.translate(-aBoundRange.getMinimum());

        // Calculate the translation needed to bring the text to the original position of
        // aPolyPolygon.
        basegfx::B2DPolyPolygon aTemp(aPolyPolygon);
        aTemp.transform(
            basegfx::utils::createShearXRotateTranslateB2DHomMatrix(fShearX, fRotate, 0.0, 0.0));
        basegfx::B2DTuple aTempLeftTop = aTemp.getB2DRange().getMinimum();
        aTranslate = aTargetLeftTop - aTempLeftTop;
    }
    else
    {
        // scale up to original size
        aPolyPolygon.transform(
            basegfx::utils::createScaleB2DHomMatrix(fabs(aScale.getX()), fabs(aScale.getY())));
    }

    // prepare outliner
    SolarMutexGuard aSolarGuard;
    SdrOutliner& rOutliner = ImpGetDrawOutliner();
    const Size aNullSize;
    rOutliner.SetPaperSize(aNullSize);
    rOutliner.SetPolygon(aPolyPolygon);
    rOutliner.SetUpdateLayout(true);
    rOutliner.SetText(rSdrContourTextPrimitive.getOutlinerParaObject());

    // set visualizing page at Outliner; needed e.g. for PageNumberField decomposition
    rOutliner.setVisualizedPage(GetSdrPageFromXDrawPage(aViewInformation.getVisualizedPage()));

    // prepare matrices to apply to newly created primitives
    basegfx::B2DHomMatrix aNewTransformA;
    // East Asian vertical writing mode needs text start at TopRight.
    const OutlinerParaObject& rOutlinerParaObject
        = rSdrContourTextPrimitive.getOutlinerParaObject();
    const bool bVerticalWriting(rOutlinerParaObject.IsEffectivelyVertical());
    const bool bTopToBottom(rOutlinerParaObject.IsTopToBottom());
    if (bVerticalWriting && bTopToBottom)
    {
        const double fStartInX = aPolyPolygon.getB2DRange().getMaximum().getX();
        aNewTransformA *= basegfx::utils::createTranslateB2DHomMatrix(fStartInX, 0.0);
    }

    // mirroring. We are now in the polygon sizes. When mirroring in X and Y,
    // move the null point which was top left to bottom right.
    const bool bMirrorX(aScale.getX() < 0.0);
    const bool bMirrorY(aScale.getY() < 0.0);

    // in-between the translations of the single primitives will take place. Afterwards,
    // the object's transformations need to be applied
    const basegfx::B2DHomMatrix aNewTransformB(basegfx::utils::createScaleShearXRotateTranslateB2DHomMatrix(
        bMirrorX ? -1.0 : 1.0, bMirrorY ? -1.0 : 1.0,
        fShearX, fRotate, aTranslate.getX(), aTranslate.getY()));

    // now break up text primitives. If it has a fat stroke, createTextPrimitive() has created a
    // ScaledUnitPolyPolygon. Thus aPolyPolygon might be smaller than aScale from aObjectMatrix. We
    // use this smaller size for the text area, otherwise the text will reach into the stroke.
    impTextBreakupHandler aConverter(rOutliner);
    aConverter.decomposeContourTextPrimitive(aNewTransformA, aNewTransformB,
                                             aPolyPolygon.getB2DRange().getRange());

    // cleanup outliner
    rOutliner.Clear();
    rOutliner.setVisualizedPage(nullptr);

    rTarget = aConverter.extractPrimitive2DSequence();
}

void SdrTextObj::impDecomposeAutoFitTextPrimitive(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    const drawinglayer::primitive2d::SdrAutoFitTextPrimitive2D& rSdrAutofitTextPrimitive,
    const drawinglayer::geometry::ViewInformation2D& aViewInformation) const
{
    // decompose matrix to have position and size of text
    basegfx::B2DVector aScale, aTranslate;
    double fRotate, fShearX;
    rSdrAutofitTextPrimitive.getTextRangeTransform().decompose(aScale, aTranslate, fRotate, fShearX);

    // use B2DRange aAnchorTextRange for calculations
    basegfx::B2DRange aAnchorTextRange(aTranslate);
    aAnchorTextRange.expand(aTranslate + aScale);

    // prepare outliner
    const SfxItemSet& rTextItemSet = rSdrAutofitTextPrimitive.getSdrText()->GetItemSet();
    SolarMutexGuard aSolarGuard;
    SdrOutliner& rOutliner = ImpGetDrawOutliner();
    SdrTextVertAdjust eVAdj = GetTextVerticalAdjust(rTextItemSet);
    SdrTextHorzAdjust eHAdj = GetTextHorizontalAdjust(rTextItemSet);
    const EEControlBits nOriginalControlWord(rOutliner.GetControlWord());
    const Size aNullSize;

    // set visualizing page at Outliner; needed e.g. for PageNumberField decomposition
    rOutliner.setVisualizedPage(GetSdrPageFromXDrawPage(aViewInformation.getVisualizedPage()));

    rOutliner.SetControlWord(nOriginalControlWord|EEControlBits::AUTOPAGESIZE|EEControlBits::STRETCHING);
    rOutliner.SetMinAutoPaperSize(aNullSize);
    rOutliner.SetMaxAutoPaperSize(Size(1000000,1000000));

    // That color needs to be restored on leaving this method
    Color aOriginalBackColor(rOutliner.GetBackgroundColor());
    setSuitableOutlinerBg(rOutliner);

    // add one to range sizes to get back to the old Rectangle and outliner measurements
    const sal_uInt32 nAnchorTextWidth(basegfx::fround<sal_uInt32>(aAnchorTextRange.getWidth() + 1));
    const sal_uInt32 nAnchorTextHeight(basegfx::fround<sal_uInt32>(aAnchorTextRange.getHeight() + 1));
    const OutlinerParaObject& rOutlinerParaObject(rSdrAutofitTextPrimitive.getOutlinerParaObject());
    const bool bVerticalWriting(rOutlinerParaObject.IsEffectivelyVertical());
    const bool bTopToBottom(rOutlinerParaObject.IsTopToBottom());
    const Size aAnchorTextSize(Size(nAnchorTextWidth, nAnchorTextHeight));

    if(rSdrAutofitTextPrimitive.getWordWrap() || IsTextFrame())
    {
        rOutliner.SetMaxAutoPaperSize(aAnchorTextSize);
    }

    if(SDRTEXTHORZADJUST_BLOCK == eHAdj && !bVerticalWriting)
    {
        rOutliner.SetMinAutoPaperSize(Size(nAnchorTextWidth, 0));
        rOutliner.SetMinColumnWrapHeight(nAnchorTextHeight);
    }

    if(SDRTEXTVERTADJUST_BLOCK == eVAdj && bVerticalWriting)
    {
        rOutliner.SetMinAutoPaperSize(Size(0, nAnchorTextHeight));
        rOutliner.SetMinColumnWrapHeight(nAnchorTextWidth);
    }

    rOutliner.SetPaperSize(aAnchorTextSize);
    rOutliner.SetUpdateLayout(true);
    rOutliner.SetText(rOutlinerParaObject);

    setupAutoFitText(rOutliner, aAnchorTextSize);
    // set visualizing page at Outliner; needed e.g. for PageNumberField decomposition
    rOutliner.setVisualizedPage(GetSdrPageFromXDrawPage(aViewInformation.getVisualizedPage()));

    // Do not forget to set FixedCellHeight, else the line heights will not be correct,
    // see impDecomposeBlockTextPrimitive or impDecomposeStretchTextPrimitive in this
    // file. The visualization for paint and in EditMode would be different. Since
    // SetFixedCellHeight *is* used/done in ::BegTextEdit the error is that it is *not*
    // done here (in contrast to BlockText and StretchText)
    rOutliner.SetFixedCellHeight(rSdrAutofitTextPrimitive.isFixedCellHeight());

    // now get back the layouted text size from outliner
    const Size aOutlinerTextSize(rOutliner.GetPaperSize());
    const basegfx::B2DVector aOutlinerScale(aOutlinerTextSize.Width(), aOutlinerTextSize.Height());
    basegfx::B2DVector aAdjustTranslate(0.0, 0.0);

    // correct horizontal translation using the now known text size
    if(SDRTEXTHORZADJUST_CENTER == eHAdj || SDRTEXTHORZADJUST_RIGHT == eHAdj)
    {
        const double fFree(aAnchorTextRange.getWidth() - aOutlinerScale.getX());

        if(SDRTEXTHORZADJUST_CENTER == eHAdj)
        {
            aAdjustTranslate.setX(fFree / 2.0);
        }

        if(SDRTEXTHORZADJUST_RIGHT == eHAdj)
        {
            aAdjustTranslate.setX(fFree);
        }
    }

    // correct vertical translation using the now known text size
    if(SDRTEXTVERTADJUST_CENTER == eVAdj || SDRTEXTVERTADJUST_BOTTOM == eVAdj)
    {
        const double fFree(aAnchorTextRange.getHeight() - aOutlinerScale.getY());

        if(SDRTEXTVERTADJUST_CENTER == eVAdj)
        {
            aAdjustTranslate.setY(fFree / 2.0);
        }

        if(SDRTEXTVERTADJUST_BOTTOM == eVAdj)
        {
            aAdjustTranslate.setY(fFree);
        }
    }

    // prepare matrices to apply to newly created primitives. aNewTransformA
    // will get coordinates in aOutlinerScale size and positive in X, Y.
    basegfx::B2DHomMatrix aNewTransformA;
    basegfx::B2DHomMatrix aNewTransformB;

    // translate relative to given primitive to get same rotation and shear
    // as the master shape we are working on. For vertical, use the top-right
    // corner
    const double fStartInX(bVerticalWriting && bTopToBottom ? aAdjustTranslate.getX() + aOutlinerScale.getX() : aAdjustTranslate.getX());
    const double fStartInY(bVerticalWriting && !bTopToBottom ? aAdjustTranslate.getY() + aOutlinerScale.getY() : aAdjustTranslate.getY());
    aNewTransformA.translate(fStartInX, fStartInY);

    // mirroring. We are now in aAnchorTextRange sizes. When mirroring in X and Y,
    // move the null point which was top left to bottom right.
    const bool bMirrorX(aScale.getX() < 0.0);
    const bool bMirrorY(aScale.getY() < 0.0);
    aNewTransformB.scale(bMirrorX ? -1.0 : 1.0, bMirrorY ? -1.0 : 1.0);

    // in-between the translations of the single primitives will take place. Afterwards,
    // the object's transformations need to be applied
    aNewTransformB.shearX(fShearX);
    aNewTransformB.rotate(fRotate);
    aNewTransformB.translate(aTranslate.getX(), aTranslate.getY());

    basegfx::B2DRange aClipRange;

    // now break up text primitives.
    impTextBreakupHandler aConverter(rOutliner);
    aConverter.decomposeBlockTextPrimitive(aNewTransformA, aNewTransformB, aClipRange);

    // cleanup outliner
    rOutliner.SetBackgroundColor(aOriginalBackColor);
    rOutliner.Clear();
    rOutliner.setVisualizedPage(nullptr);
    rOutliner.SetControlWord(nOriginalControlWord);

    rTarget = aConverter.extractPrimitive2DSequence();
}

// Resolves: fdo#35779 set background color of this shape as the editeng background if there
// is one. Check the shape itself, then the host page, then that page's master page.
bool SdrObject::setSuitableOutlinerBg(::Outliner& rOutliner) const
{
    const SfxItemSet* pBackgroundFillSet = getBackgroundFillSet();
    if (drawing::FillStyle_NONE != pBackgroundFillSet->Get(XATTR_FILLSTYLE).GetValue())
    {
        Color aColor(GetDraftFillColor(*pBackgroundFillSet).value_or(rOutliner.GetBackgroundColor()));
        rOutliner.SetBackgroundColor(aColor);
        return true;
    }
    return false;
}

const SfxItemSet* SdrObject::getBackgroundFillSet() const
{
    const SfxItemSet* pBackgroundFillSet = &GetObjectItemSet();

    if (drawing::FillStyle_NONE == pBackgroundFillSet->Get(XATTR_FILLSTYLE).GetValue())
    {
        SdrPage* pOwnerPage(getSdrPageFromSdrObject());
        if (pOwnerPage)
        {
            pBackgroundFillSet = &pOwnerPage->getSdrPageProperties().GetItemSet();

            if (drawing::FillStyle_NONE == pBackgroundFillSet->Get(XATTR_FILLSTYLE).GetValue())
            {
                if (!pOwnerPage->IsMasterPage() && pOwnerPage->TRG_HasMasterPage())
                {
                    // See unomodel.cxx: "It is guaranteed, that after a standard page the corresponding notes page follows."
                    bool notesPage = pOwnerPage->GetPageNum() % 2 == 0;

                    if (!comphelper::LibreOfficeKit::isActive() || !notesPage || !pOwnerPage->getSdrModelFromSdrPage().IsImpress())
                        pBackgroundFillSet = &pOwnerPage->TRG_GetMasterPage().getSdrPageProperties().GetItemSet();
                    else {
                        // See sdrmasterpagedescriptor.cxx: e.g. the Notes MasterPage has no StyleSheet set (and there maybe others).
                        if (pOwnerPage->getSdrModelFromSdrPage().GetPage(pOwnerPage->GetPageNum() - 1))
                            pBackgroundFillSet = &pOwnerPage->getSdrModelFromSdrPage().GetPage(pOwnerPage->GetPageNum() - 1)->TRG_GetMasterPage().getSdrPageProperties().GetItemSet();
                        else
                            pBackgroundFillSet = &pOwnerPage->TRG_GetMasterPage().getSdrPageProperties().GetItemSet();
                    }
                }
            }
        }
    }
    return pBackgroundFillSet;
}

const Graphic* SdrObject::getFillGraphic() const
{
    if(IsGroupObject()) // Doesn't make sense, and GetObjectItemSet() asserts.
        return nullptr;
    const SfxItemSet* pBackgroundFillSet = getBackgroundFillSet();
    if (drawing::FillStyle_BITMAP != pBackgroundFillSet->Get(XATTR_FILLSTYLE).GetValue())
        return nullptr;
    return &pBackgroundFillSet->Get(XATTR_FILLBITMAP).GetGraphicObject().GetGraphic();
}

void SdrTextObj::impDecomposeBlockTextPrimitive(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    const drawinglayer::primitive2d::SdrBlockTextPrimitive2D& rSdrBlockTextPrimitive,
    const drawinglayer::geometry::ViewInformation2D& aViewInformation) const
{
    // decompose matrix to have position and size of text
    basegfx::B2DVector aScale, aTranslate;
    double fRotate, fShearX;
    rSdrBlockTextPrimitive.getTextRangeTransform().decompose(aScale, aTranslate, fRotate, fShearX);

    // use B2DRange aAnchorTextRange for calculations
    basegfx::B2DRange aAnchorTextRange(aTranslate);
    aAnchorTextRange.expand(aTranslate + aScale);

    // prepare outliner
    const bool bIsCell(rSdrBlockTextPrimitive.getCellText());
    SolarMutexGuard aSolarGuard;
    SdrOutliner& rOutliner = ImpGetDrawOutliner();
    SdrTextHorzAdjust eHAdj = rSdrBlockTextPrimitive.getSdrTextHorzAdjust();
    SdrTextVertAdjust eVAdj = rSdrBlockTextPrimitive.getSdrTextVertAdjust();
    const EEControlBits nOriginalControlWord(rOutliner.GetControlWord());
    const Size aNullSize;

    // set visualizing page at Outliner; needed e.g. for PageNumberField decomposition
    rOutliner.setVisualizedPage(GetSdrPageFromXDrawPage(aViewInformation.getVisualizedPage()));
    rOutliner.SetFixedCellHeight(rSdrBlockTextPrimitive.isFixedCellHeight());
    rOutliner.SetControlWord(nOriginalControlWord|EEControlBits::AUTOPAGESIZE);
    rOutliner.SetMinAutoPaperSize(aNullSize);
    rOutliner.SetMaxAutoPaperSize(Size(1000000,1000000));

    // That color needs to be restored on leaving this method
    Color aOriginalBackColor(rOutliner.GetBackgroundColor());
    setSuitableOutlinerBg(rOutliner);

    // add one to range sizes to get back to the old Rectangle and outliner measurements
    const sal_uInt32 nAnchorTextWidth(basegfx::fround<sal_uInt32>(aAnchorTextRange.getWidth() + 1));
    const sal_uInt32 nAnchorTextHeight(basegfx::fround<sal_uInt32>(aAnchorTextRange.getHeight() + 1));
    const bool bVerticalWriting(rSdrBlockTextPrimitive.getOutlinerParaObject().IsEffectivelyVertical());
    const bool bTopToBottom(rSdrBlockTextPrimitive.getOutlinerParaObject().IsTopToBottom());
    const Size aAnchorTextSize(Size(nAnchorTextWidth, nAnchorTextHeight));

    if(bIsCell)
    {
        // cell text is formatted neither like a text object nor like an object
        // text, so use a special setup here
        rOutliner.SetMaxAutoPaperSize(aAnchorTextSize);

        // #i106214# To work with an unchangeable PaperSize (CellSize in
        // this case) Set(Min|Max)AutoPaperSize and SetPaperSize have to be used.
        // #i106214# This was not completely correct; to still measure the real
        // text height to allow vertical adjust (and vice versa for VerticalWritintg)
        // only one aspect has to be set, but the other one to zero
        if(bVerticalWriting)
        {
            // measure the horizontal text size
            rOutliner.SetMinAutoPaperSize(Size(0, aAnchorTextSize.Height()));
        }
        else
        {
            // measure the vertical text size
            rOutliner.SetMinAutoPaperSize(Size(aAnchorTextSize.Width(), 0));
        }

        rOutliner.SetPaperSize(aAnchorTextSize);
        rOutliner.SetUpdateLayout(true);
        rOutliner.SetText(rSdrBlockTextPrimitive.getOutlinerParaObject());
    }
    else
    {
        // check if block text is used (only one of them can be true)
        const bool bHorizontalIsBlock(SDRTEXTHORZADJUST_BLOCK == eHAdj && !bVerticalWriting);
        const bool bVerticalIsBlock(SDRTEXTVERTADJUST_BLOCK == eVAdj && bVerticalWriting);

        // set minimal paper size horizontally/vertically if needed
        if(bHorizontalIsBlock)
        {
            rOutliner.SetMinAutoPaperSize(Size(nAnchorTextWidth, 0));
            rOutliner.SetMinColumnWrapHeight(nAnchorTextHeight);
        }
        else if(bVerticalIsBlock)
        {
            rOutliner.SetMinAutoPaperSize(Size(0, nAnchorTextHeight));
            rOutliner.SetMinColumnWrapHeight(nAnchorTextWidth);
        }

        if((rSdrBlockTextPrimitive.getWordWrap() || IsTextFrame()) && !rSdrBlockTextPrimitive.getUnlimitedPage())
        {
            // #i103454# maximal paper size hor/ver needs to be limited to text
            // frame size. If it's block text, still allow the 'other' direction
            // to grow to get a correct real text size when using GetPaperSize().
            // When just using aAnchorTextSize as maximum, GetPaperSize()
            // would just return aAnchorTextSize again: this means, the wanted
            // 'measurement' of the real size of block text would not work
            Size aMaxAutoPaperSize(aAnchorTextSize);

            // Usual processing - always grow in one of directions
            bool bAllowGrowVertical = !bVerticalWriting;
            bool bAllowGrowHorizontal = bVerticalWriting;

            // Compatibility mode for tdf#99729
            if (getSdrModelFromSdrObject().GetCompatibilityFlag(
                    SdrCompatibilityFlag::AnchoredTextOverflowLegacy))
            {
                bAllowGrowVertical = bHorizontalIsBlock;
                bAllowGrowHorizontal = bVerticalIsBlock;
            }

            if (bAllowGrowVertical)
            {
                // allow to grow vertical for horizontal texts
                aMaxAutoPaperSize.setHeight(1000000);
            }
            else if (bAllowGrowHorizontal)
            {
                // allow to grow horizontal for vertical texts
                aMaxAutoPaperSize.setWidth(1000000);
            }

            rOutliner.SetMaxAutoPaperSize(aMaxAutoPaperSize);
        }

        rOutliner.SetPaperSize(aNullSize);
        rOutliner.SetUpdateLayout(true);
        rOutliner.SetText(rSdrBlockTextPrimitive.getOutlinerParaObject());
    }

    rOutliner.SetControlWord(nOriginalControlWord);

    // now get back the layouted text size from outliner
    const Size aOutlinerTextSize(rOutliner.GetPaperSize());
    const basegfx::B2DVector aOutlinerScale(aOutlinerTextSize.Width(), aOutlinerTextSize.Height());
    basegfx::B2DVector aAdjustTranslate(0.0, 0.0);

    // For draw objects containing text correct hor/ver alignment if text is bigger
    // than the object itself. Without that correction, the text would always be
    // formatted to the left edge (or top edge when vertical) of the draw object.
    if(!IsTextFrame() && !bIsCell)
    {
        if(aAnchorTextRange.getWidth() < aOutlinerScale.getX() && !bVerticalWriting)
        {
            // Horizontal case here. Correct only if eHAdj == SDRTEXTHORZADJUST_BLOCK,
            // else the alignment is wanted.
            if(SDRTEXTHORZADJUST_BLOCK == eHAdj)
            {
                SvxAdjust eAdjust = GetObjectItemSet().Get(EE_PARA_JUST).GetAdjust();
                switch(eAdjust)
                {
                    case SvxAdjust::Left:   eHAdj = SDRTEXTHORZADJUST_LEFT; break;
                    case SvxAdjust::Right:  eHAdj = SDRTEXTHORZADJUST_RIGHT; break;
                    case SvxAdjust::Center: eHAdj = SDRTEXTHORZADJUST_CENTER; break;
                    default: break;
                }
            }
        }

        if(aAnchorTextRange.getHeight() < aOutlinerScale.getY() && bVerticalWriting)
        {
            // Vertical case here. Correct only if eHAdj == SDRTEXTVERTADJUST_BLOCK,
            // else the alignment is wanted.
            if(SDRTEXTVERTADJUST_BLOCK == eVAdj)
            {
                eVAdj = SDRTEXTVERTADJUST_CENTER;
            }
        }
    }

    // correct horizontal translation using the now known text size
    if(SDRTEXTHORZADJUST_CENTER == eHAdj || SDRTEXTHORZADJUST_RIGHT == eHAdj)
    {
        const double fFree(aAnchorTextRange.getWidth() - aOutlinerScale.getX());

        if(SDRTEXTHORZADJUST_CENTER == eHAdj)
        {
            aAdjustTranslate.setX(fFree / 2.0);
        }

        if(SDRTEXTHORZADJUST_RIGHT == eHAdj)
        {
            aAdjustTranslate.setX(fFree);
        }
    }

    const double fFreeVerticalSpace(aAnchorTextRange.getHeight() - aOutlinerScale.getY());
    bool bClipVerticalTextOverflow = fFreeVerticalSpace < 0
                                     && GetObjectItemSet().Get(SDRATTR_TEXT_CLIPVERTOVERFLOW).GetValue();
    // correct vertical translation using the now known text size
    if((SDRTEXTVERTADJUST_CENTER == eVAdj || SDRTEXTVERTADJUST_BOTTOM == eVAdj)
       && !bClipVerticalTextOverflow)
    {
        if(SDRTEXTVERTADJUST_CENTER == eVAdj)
        {
            aAdjustTranslate.setY(fFreeVerticalSpace / 2.0);
        }

        if(SDRTEXTVERTADJUST_BOTTOM == eVAdj)
        {
            aAdjustTranslate.setY(fFreeVerticalSpace);
        }
    }

    // prepare matrices to apply to newly created primitives. aNewTransformA
    // will get coordinates in aOutlinerScale size and positive in X, Y.
    // Translate relative to given primitive to get same rotation and shear
    // as the master shape we are working on. For vertical, use the top-right
    // corner
    const double fStartInX(bVerticalWriting && bTopToBottom ? aAdjustTranslate.getX() + aOutlinerScale.getX() : aAdjustTranslate.getX());
    const double fStartInY(bVerticalWriting && !bTopToBottom ? aAdjustTranslate.getY() + aOutlinerScale.getY() : aAdjustTranslate.getY());
    basegfx::B2DHomMatrix aNewTransformA(basegfx::utils::createTranslateB2DHomMatrix(fStartInX, fStartInY));

    // Apply the camera rotation. It have to be applied after adjustment of
    // the text (top, bottom, center, left, right).
    if(GetCameraZRotation() != 0)
    {
        // First find the text rect.
        basegfx::B2DRange aTextRectangle(/*x1=*/aTranslate.getX() + aAdjustTranslate.getX(),
                                         /*y1=*/aTranslate.getY() + aAdjustTranslate.getY(),
                                         /*x2=*/aTranslate.getX() + aOutlinerScale.getX() - aAdjustTranslate.getX(),
                                         /*y2=*/aTranslate.getY() + aOutlinerScale.getY() - aAdjustTranslate.getY());

        // Rotate the text from the center point.
        basegfx::B2DVector aTranslateToCenter(aTextRectangle.getWidth() / 2, aTextRectangle.getHeight() / 2);
        aNewTransformA.translate(-aTranslateToCenter.getX(), -aTranslateToCenter.getY());
        aNewTransformA.rotate(basegfx::deg2rad(360.0 - GetCameraZRotation() ));
        aNewTransformA.translate(aTranslateToCenter.getX(), aTranslateToCenter.getY());
    }

    // mirroring. We are now in aAnchorTextRange sizes. When mirroring in X and Y,
    // move the null point which was top left to bottom right.
    const bool bMirrorX(aScale.getX() < 0.0);
    const bool bMirrorY(aScale.getY() < 0.0);

    // in-between the translations of the single primitives will take place. Afterwards,
    // the object's transformations need to be applied
    const basegfx::B2DHomMatrix aNewTransformB(basegfx::utils::createScaleShearXRotateTranslateB2DHomMatrix(
        bMirrorX ? -1.0 : 1.0, bMirrorY ? -1.0 : 1.0,
        fShearX, fRotate, aTranslate.getX(), aTranslate.getY()));


    // create ClipRange (if needed)
    basegfx::B2DRange aClipRange;
    if(bClipVerticalTextOverflow)
        aClipRange = {0, 0, std::numeric_limits<double>::max(), aAnchorTextRange.getHeight()};

    // now break up text primitives.
    impTextBreakupHandler aConverter(rOutliner);
    aConverter.decomposeBlockTextPrimitive(aNewTransformA, aNewTransformB, aClipRange);

    // cleanup outliner
    rOutliner.SetBackgroundColor(aOriginalBackColor);
    rOutliner.Clear();
    rOutliner.setVisualizedPage(nullptr);

    rTarget = aConverter.extractPrimitive2DSequence();
}

void SdrTextObj::impDecomposeStretchTextPrimitive(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    const drawinglayer::primitive2d::SdrStretchTextPrimitive2D& rSdrStretchTextPrimitive,
    const drawinglayer::geometry::ViewInformation2D& aViewInformation) const
{
    // decompose matrix to have position and size of text
    basegfx::B2DVector aScale, aTranslate;
    double fRotate, fShearX;
    rSdrStretchTextPrimitive.getTextRangeTransform().decompose(aScale, aTranslate, fRotate, fShearX);

    // prepare outliner
    SolarMutexGuard aSolarGuard;
    SdrOutliner& rOutliner = ImpGetDrawOutliner();
    const EEControlBits nOriginalControlWord(rOutliner.GetControlWord());
    const Size aNullSize;

    rOutliner.SetControlWord(nOriginalControlWord|EEControlBits::STRETCHING|EEControlBits::AUTOPAGESIZE);
    rOutliner.SetFixedCellHeight(rSdrStretchTextPrimitive.isFixedCellHeight());
    rOutliner.SetMinAutoPaperSize(aNullSize);
    rOutliner.SetMaxAutoPaperSize(Size(1000000,1000000));
    rOutliner.SetPaperSize(aNullSize);
    rOutliner.SetUpdateLayout(true);
    rOutliner.SetText(rSdrStretchTextPrimitive.getOutlinerParaObject());

    // set visualizing page at Outliner; needed e.g. for PageNumberField decomposition
    rOutliner.setVisualizedPage(GetSdrPageFromXDrawPage(aViewInformation.getVisualizedPage()));

    // now get back the laid out text size from outliner
    const Size aOutlinerTextSize(rOutliner.CalcTextSize());
    const basegfx::B2DVector aOutlinerScale(
        aOutlinerTextSize.Width() == tools::Long(0) ? 1.0 : aOutlinerTextSize.Width(),
        aOutlinerTextSize.Height()  == tools::Long(0) ? 1.0 : aOutlinerTextSize.Height());

    // prepare matrices to apply to newly created primitives
    basegfx::B2DHomMatrix aNewTransformA;

    // #i101957# Check for vertical text. If used, aNewTransformA
    // needs to translate the text initially around object width to orient
    // it relative to the topper right instead of the topper left
    const bool bVertical(rSdrStretchTextPrimitive.getOutlinerParaObject().IsEffectivelyVertical());
    const bool bTopToBottom(rSdrStretchTextPrimitive.getOutlinerParaObject().IsTopToBottom());

    if(bVertical)
    {
        if(bTopToBottom)
            aNewTransformA.translate(aScale.getX(), 0.0);
        else
            aNewTransformA.translate(0.0, aScale.getY());
    }

    // calculate global char stretching scale parameters. Use non-mirrored sizes
    // to layout without mirroring
    const double fScaleX(fabs(aScale.getX()) / aOutlinerScale.getX());
    const double fScaleY(fabs(aScale.getY()) / aOutlinerScale.getY());
    ScalingParameters aScalingParameters{fScaleX, fScaleY};

    rOutliner.setScalingParameters(aScalingParameters);

    // When mirroring in X and Y,
    // move the null point which was top left to bottom right.
    const bool bMirrorX(aScale.getX() < 0.0);
    const bool bMirrorY(aScale.getY() < 0.0);

    // in-between the translations of the single primitives will take place. Afterwards,
    // the object's transformations need to be applied
    const basegfx::B2DHomMatrix aNewTransformB(basegfx::utils::createScaleShearXRotateTranslateB2DHomMatrix(
        bMirrorX ? -1.0 : 1.0, bMirrorY ? -1.0 : 1.0,
        fShearX, fRotate, aTranslate.getX(), aTranslate.getY()));

    // now break up text primitives.
    impTextBreakupHandler aConverter(rOutliner);
    aConverter.decomposeStretchTextPrimitive(aNewTransformA, aNewTransformB);

    // cleanup outliner
    rOutliner.SetControlWord(nOriginalControlWord);
    rOutliner.Clear();
    rOutliner.setVisualizedPage(nullptr);

    rTarget = aConverter.extractPrimitive2DSequence();
}


// timing generators
#define ENDLESS_LOOP    (0xffffffff)
#define ENDLESS_TIME    (double(0xffffffff))

void SdrTextObj::impGetBlinkTextTiming(drawinglayer::animation::AnimationEntryList& rAnimList) const
{
    if(SdrTextAniKind::Blink != GetTextAniKind())
        return;

    // get values
    const SfxItemSet& rSet = GetObjectItemSet();
    const sal_uInt32 nRepeat(static_cast<sal_uInt32>(rSet.Get(SDRATTR_TEXT_ANICOUNT).GetValue()));
    double fDelay(static_cast<double>(rSet.Get(SDRATTR_TEXT_ANIDELAY).GetValue()));

    if(0.0 == fDelay)
    {
        // use default
        fDelay = 250.0;
    }

    // prepare loop and add
    drawinglayer::animation::AnimationEntryLoop  aLoop(nRepeat ? nRepeat : ENDLESS_LOOP);
    drawinglayer::animation::AnimationEntryFixed aStart(fDelay, 0.0);
    aLoop.append(aStart);
    drawinglayer::animation::AnimationEntryFixed aEnd(fDelay, 1.0);
    aLoop.append(aEnd);
    rAnimList.append(aLoop);

    // add stopped state if loop is not endless
    if(0 != nRepeat)
    {
        bool bVisibleWhenStopped(rSet.Get(SDRATTR_TEXT_ANISTOPINSIDE).GetValue());
        drawinglayer::animation::AnimationEntryFixed aStop(ENDLESS_TIME, bVisibleWhenStopped ? 0.0 : 1.0);
        rAnimList.append(aStop);
    }
}

static void impCreateScrollTiming(const SfxItemSet& rSet, drawinglayer::animation::AnimationEntryList& rAnimList, bool bForward, double fTimeFullPath, double fFrequency)
{
    bool bVisibleWhenStopped(rSet.Get(SDRATTR_TEXT_ANISTOPINSIDE).GetValue());
    bool bVisibleWhenStarted(rSet.Get(SDRATTR_TEXT_ANISTARTINSIDE).GetValue());
    const sal_uInt32 nRepeat(rSet.Get(SDRATTR_TEXT_ANICOUNT).GetValue());

    if(bVisibleWhenStarted)
    {
        // move from center to outside
        drawinglayer::animation::AnimationEntryLinear aInOut(fTimeFullPath * 0.5, fFrequency, 0.5, bForward ? 1.0 : 0.0);
        rAnimList.append(aInOut);
    }

    // loop. In loop, move through
    drawinglayer::animation::AnimationEntryLoop aLoop(nRepeat ? nRepeat : ENDLESS_LOOP);
    drawinglayer::animation::AnimationEntryLinear aThrough(fTimeFullPath, fFrequency, bForward ? 0.0 : 1.0, bForward ? 1.0 : 0.0);
    aLoop.append(aThrough);
    rAnimList.append(aLoop);

    if(0 != nRepeat && bVisibleWhenStopped)
    {
        // move from outside to center
        drawinglayer::animation::AnimationEntryLinear aOutIn(fTimeFullPath * 0.5, fFrequency, bForward ? 0.0 : 1.0, 0.5);
        rAnimList.append(aOutIn);

        // add timing for staying at the end
        drawinglayer::animation::AnimationEntryFixed aEnd(ENDLESS_TIME, 0.5);
        rAnimList.append(aEnd);
    }
}

static void impCreateAlternateTiming(const SfxItemSet& rSet, drawinglayer::animation::AnimationEntryList& rAnimList, double fRelativeTextLength, bool bForward, double fTimeFullPath, double fFrequency)
{
    if(basegfx::fTools::more(fRelativeTextLength, 0.5))
    {
        // this is the case when fTextLength > fFrameLength, text is bigger than animation frame.
        // In that case, correct direction
        bForward = !bForward;
    }

    const double fStartPosition(bForward ? fRelativeTextLength : 1.0 - fRelativeTextLength);
    const double fEndPosition(bForward ? 1.0 - fRelativeTextLength : fRelativeTextLength);
    bool bVisibleWhenStarted(rSet.Get(SDRATTR_TEXT_ANISTARTINSIDE).GetValue());
    const sal_uInt32 nRepeat(rSet.Get(SDRATTR_TEXT_ANICOUNT).GetValue());

    if(!bVisibleWhenStarted)
    {
        // move from outside to center
        drawinglayer::animation::AnimationEntryLinear aOutIn(fTimeFullPath * 0.5, fFrequency, bForward ? 0.0 : 1.0, 0.5);
        rAnimList.append(aOutIn);
    }

    // loop. In loop, move out and in again. fInnerMovePath may be negative when text is bigger then frame,
    // so use absolute value
    const double fInnerMovePath(fabs(1.0 - (fRelativeTextLength * 2.0)));
    const double fTimeForInnerPath(fTimeFullPath * fInnerMovePath);
    const double fHalfInnerPath(fTimeForInnerPath * 0.5);
    const sal_uInt32 nDoubleRepeat(nRepeat / 2L);

    if(nDoubleRepeat || 0 == nRepeat)
    {
        // double forth and back loop
        drawinglayer::animation::AnimationEntryLoop aLoop(nDoubleRepeat ? nDoubleRepeat : ENDLESS_LOOP);
        drawinglayer::animation::AnimationEntryLinear aTime0(fHalfInnerPath, fFrequency, 0.5, fEndPosition);
        aLoop.append(aTime0);
        drawinglayer::animation::AnimationEntryLinear aTime1(fTimeForInnerPath, fFrequency, fEndPosition, fStartPosition);
        aLoop.append(aTime1);
        drawinglayer::animation::AnimationEntryLinear aTime2(fHalfInnerPath, fFrequency, fStartPosition, 0.5);
        aLoop.append(aTime2);
        rAnimList.append(aLoop);
    }

    if(nRepeat % 2L)
    {
        // repeat is uneven, so we need one more forth and back to center
        drawinglayer::animation::AnimationEntryLinear aTime0(fHalfInnerPath, fFrequency, 0.5, fEndPosition);
        rAnimList.append(aTime0);
        drawinglayer::animation::AnimationEntryLinear aTime1(fHalfInnerPath, fFrequency, fEndPosition, 0.5);
        rAnimList.append(aTime1);
    }

    if(0 == nRepeat)
        return;

    bool bVisibleWhenStopped(rSet.Get(SDRATTR_TEXT_ANISTOPINSIDE).GetValue());
    if(bVisibleWhenStopped)
    {
        // add timing for staying at the end
        drawinglayer::animation::AnimationEntryFixed aEnd(ENDLESS_TIME, 0.5);
        rAnimList.append(aEnd);
    }
    else
    {
        // move from center to outside
        drawinglayer::animation::AnimationEntryLinear aInOut(fTimeFullPath * 0.5, fFrequency, 0.5, bForward ? 1.0 : 0.0);
        rAnimList.append(aInOut);
    }
}

static void impCreateSlideTiming(const SfxItemSet& rSet, drawinglayer::animation::AnimationEntryList& rAnimList, bool bForward, double fTimeFullPath, double fFrequency)
{
    // move in from outside, start outside
    const double fStartPosition(bForward ? 0.0 : 1.0);
    const sal_uInt32 nRepeat(rSet.Get(SDRATTR_TEXT_ANICOUNT).GetValue());

    // move from outside to center
    drawinglayer::animation::AnimationEntryLinear aOutIn(fTimeFullPath * 0.5, fFrequency, fStartPosition, 0.5);
    rAnimList.append(aOutIn);

    // loop. In loop, move out and in again
    if(nRepeat > 1 || 0 == nRepeat)
    {
        drawinglayer::animation::AnimationEntryLoop aLoop(nRepeat ? nRepeat - 1 : ENDLESS_LOOP);
        drawinglayer::animation::AnimationEntryLinear aTime0(fTimeFullPath * 0.5, fFrequency, 0.5, fStartPosition);
        aLoop.append(aTime0);
        drawinglayer::animation::AnimationEntryLinear aTime1(fTimeFullPath * 0.5, fFrequency, fStartPosition, 0.5);
        aLoop.append(aTime1);
        rAnimList.append(aLoop);
    }

    // always visible when stopped, so add timing for staying at the end when not endless
    if(0 != nRepeat)
    {
        drawinglayer::animation::AnimationEntryFixed aEnd(ENDLESS_TIME, 0.5);
        rAnimList.append(aEnd);
    }
}

void SdrTextObj::impGetScrollTextTiming(drawinglayer::animation::AnimationEntryList& rAnimList, double fFrameLength, double fTextLength) const
{
    const SdrTextAniKind eAniKind(GetTextAniKind());

    if(SdrTextAniKind::Scroll != eAniKind && SdrTextAniKind::Alternate != eAniKind && SdrTextAniKind::Slide != eAniKind)
        return;

    // get data. Goal is to calculate fTimeFullPath which is the time needed to
    // move animation from (0.0) to (1.0) state
    const SfxItemSet& rSet = GetObjectItemSet();
    double fAnimationDelay(static_cast<double>(rSet.Get(SDRATTR_TEXT_ANIDELAY).GetValue()));
    double fSingleStepWidth(static_cast<double>(rSet.Get(SDRATTR_TEXT_ANIAMOUNT).GetValue()));
    const SdrTextAniDirection eDirection(GetTextAniDirection());
    const bool bForward(SdrTextAniDirection::Right == eDirection || SdrTextAniDirection::Down == eDirection);

    if(basegfx::fTools::equalZero(fAnimationDelay))
    {
        // default to 1/20 second
        fAnimationDelay = 50.0;
    }

    if (fSingleStepWidth < 0.0)
    {
        // data is in pixels, convert to logic. Imply 96 dpi.
        // It makes no sense to keep the view-transformation centered
        // definitions, so get rid of them here.
        fSingleStepWidth = o3tl::convert(-fSingleStepWidth, o3tl::Length::px, o3tl::Length::mm100);
    }

    if(basegfx::fTools::equalZero(fSingleStepWidth))
    {
        // default to 1 millimeter
        fSingleStepWidth = 100.0;
    }

    // use the length of the full animation path and the number of steps
    // to get the full path time
    const double fFullPathLength(fFrameLength + fTextLength);
    const double fNumberOfSteps(fFullPathLength / fSingleStepWidth);
    double fTimeFullPath(fNumberOfSteps * fAnimationDelay);

    if(fTimeFullPath < fAnimationDelay)
    {
        fTimeFullPath = fAnimationDelay;
    }

    switch(eAniKind)
    {
        case SdrTextAniKind::Scroll :
        {
            impCreateScrollTiming(rSet, rAnimList, bForward, fTimeFullPath, fAnimationDelay);
            break;
        }
        case SdrTextAniKind::Alternate :
        {
            double fRelativeTextLength(fTextLength / (fFrameLength + fTextLength));
            impCreateAlternateTiming(rSet, rAnimList, fRelativeTextLength, bForward, fTimeFullPath, fAnimationDelay);
            break;
        }
        case SdrTextAniKind::Slide :
        {
            impCreateSlideTiming(rSet, rAnimList, bForward, fTimeFullPath, fAnimationDelay);
            break;
        }
        default : break; // SdrTextAniKind::NONE, SdrTextAniKind::Blink
    }
}

void SdrTextObj::impHandleChainingEventsDuringDecomposition(SdrOutliner &rOutliner) const
{
    if (GetTextChain()->GetNilChainingEvent(this))
        return;

    GetTextChain()->SetNilChainingEvent(this, true);

    TextChainFlow aTxtChainFlow(const_cast<SdrTextObj*>(this));
    bool bIsOverflow;

#ifdef DBG_UTIL
    // Some debug output
    size_t nObjCount(getSdrPageFromSdrObject()->GetObjCount());
    for (size_t i = 0; i < nObjCount; i++)
    {
        SdrTextObj* pCurObj(DynCastSdrTextObj(getSdrPageFromSdrObject()->GetObj(i)));
        if(pCurObj == this)
        {
            SAL_INFO("svx.chaining", "Working on TextBox " << i);
            break;
        }
    }
#endif

    aTxtChainFlow.CheckForFlowEvents(&rOutliner);

    if (aTxtChainFlow.IsUnderflow() && !IsInEditMode())
    {
        // underflow-induced overflow
        aTxtChainFlow.ExecuteUnderflow(&rOutliner);
        bIsOverflow = aTxtChainFlow.IsOverflow();
    } else {
        // standard overflow (no underflow before)
        bIsOverflow = aTxtChainFlow.IsOverflow();
    }

    if (bIsOverflow && !IsInEditMode()) {
        // Initialize Chaining Outliner
        SdrOutliner &rChainingOutl(getSdrModelFromSdrObject().GetChainingOutliner(this));
        ImpInitDrawOutliner( rChainingOutl );
        rChainingOutl.SetUpdateLayout(true);
        // We must pass the chaining outliner otherwise we would mess up decomposition
        aTxtChainFlow.ExecuteOverflow(&rOutliner, &rChainingOutl);
    }

    GetTextChain()->SetNilChainingEvent(this, false);
}

void SdrTextObj::impDecomposeChainedTextPrimitive(
        drawinglayer::primitive2d::Primitive2DContainer& rTarget,
        const drawinglayer::primitive2d::SdrChainedTextPrimitive2D& rSdrChainedTextPrimitive,
        const drawinglayer::geometry::ViewInformation2D& aViewInformation) const
{
    // decompose matrix to have position and size of text
    basegfx::B2DVector aScale, aTranslate;
    double fRotate, fShearX;
    rSdrChainedTextPrimitive.getTextRangeTransform().decompose(aScale, aTranslate, fRotate, fShearX);

    // use B2DRange aAnchorTextRange for calculations
    basegfx::B2DRange aAnchorTextRange(aTranslate);
    aAnchorTextRange.expand(aTranslate + aScale);

    // prepare outliner
    const SfxItemSet& rTextItemSet = rSdrChainedTextPrimitive.getSdrText()->GetItemSet();
    SolarMutexGuard aSolarGuard;
    SdrOutliner& rOutliner = ImpGetDrawOutliner();

    SdrTextVertAdjust eVAdj = GetTextVerticalAdjust(rTextItemSet);
    SdrTextHorzAdjust eHAdj = GetTextHorizontalAdjust(rTextItemSet);
    const EEControlBits nOriginalControlWord(rOutliner.GetControlWord());
    const Size aNullSize;

    // set visualizing page at Outliner; needed e.g. for PageNumberField decomposition
    rOutliner.setVisualizedPage(GetSdrPageFromXDrawPage(aViewInformation.getVisualizedPage()));

    rOutliner.SetControlWord(nOriginalControlWord|EEControlBits::AUTOPAGESIZE|EEControlBits::STRETCHING);
    rOutliner.SetMinAutoPaperSize(aNullSize);
    rOutliner.SetMaxAutoPaperSize(Size(1000000,1000000));

    // add one to range sizes to get back to the old Rectangle and outliner measurements
    const sal_uInt32 nAnchorTextWidth(basegfx::fround<sal_uInt32>(aAnchorTextRange.getWidth() + 1));
    const sal_uInt32 nAnchorTextHeight(basegfx::fround<sal_uInt32>(aAnchorTextRange.getHeight() + 1));

    // Text
    const OutlinerParaObject& rOutlinerParaObject(rSdrChainedTextPrimitive.getOutlinerParaObject());
    const bool bVerticalWriting(rOutlinerParaObject.IsEffectivelyVertical());
    const bool bTopToBottom(rOutlinerParaObject.IsTopToBottom());
    const Size aAnchorTextSize(Size(nAnchorTextWidth, nAnchorTextHeight));

    if(IsTextFrame())
    {
        rOutliner.SetMaxAutoPaperSize(aAnchorTextSize);
    }

    if(SDRTEXTHORZADJUST_BLOCK == eHAdj && !bVerticalWriting)
    {
        rOutliner.SetMinAutoPaperSize(Size(nAnchorTextWidth, 0));
    }

    if(SDRTEXTVERTADJUST_BLOCK == eVAdj && bVerticalWriting)
    {
        rOutliner.SetMinAutoPaperSize(Size(0, nAnchorTextHeight));
    }

    rOutliner.SetPaperSize(aNullSize);
    rOutliner.SetUpdateLayout(true);
    // Sets original text
    rOutliner.SetText(rOutlinerParaObject);

    /* Begin overflow/underflow handling */

    impHandleChainingEventsDuringDecomposition(rOutliner);

    /* End overflow/underflow handling */

    // set visualizing page at Outliner; needed e.g. for PageNumberField decomposition
    rOutliner.setVisualizedPage(GetSdrPageFromXDrawPage(aViewInformation.getVisualizedPage()));

    // now get back the layouted text size from outliner
    const Size aOutlinerTextSize(rOutliner.GetPaperSize());
    const basegfx::B2DVector aOutlinerScale(aOutlinerTextSize.Width(), aOutlinerTextSize.Height());
    basegfx::B2DVector aAdjustTranslate(0.0, 0.0);

    // correct horizontal translation using the now known text size
    if(SDRTEXTHORZADJUST_CENTER == eHAdj || SDRTEXTHORZADJUST_RIGHT == eHAdj)
    {
        const double fFree(aAnchorTextRange.getWidth() - aOutlinerScale.getX());

        if(SDRTEXTHORZADJUST_CENTER == eHAdj)
        {
            aAdjustTranslate.setX(fFree / 2.0);
        }

        if(SDRTEXTHORZADJUST_RIGHT == eHAdj)
        {
            aAdjustTranslate.setX(fFree);
        }
    }

    // correct vertical translation using the now known text size
    if(SDRTEXTVERTADJUST_CENTER == eVAdj || SDRTEXTVERTADJUST_BOTTOM == eVAdj)
    {
        const double fFree(aAnchorTextRange.getHeight() - aOutlinerScale.getY());

        if(SDRTEXTVERTADJUST_CENTER == eVAdj)
        {
            aAdjustTranslate.setY(fFree / 2.0);
        }

        if(SDRTEXTVERTADJUST_BOTTOM == eVAdj)
        {
            aAdjustTranslate.setY(fFree);
        }
    }

    // prepare matrices to apply to newly created primitives. aNewTransformA
    // will get coordinates in aOutlinerScale size and positive in X, Y.
    basegfx::B2DHomMatrix aNewTransformA;
    basegfx::B2DHomMatrix aNewTransformB;

    // translate relative to given primitive to get same rotation and shear
    // as the master shape we are working on. For vertical, use the top-right
    // corner
    const double fStartInX(bVerticalWriting && bTopToBottom ? aAdjustTranslate.getX() + aOutlinerScale.getX() : aAdjustTranslate.getX());
    const double fStartInY(bVerticalWriting && !bTopToBottom ? aAdjustTranslate.getY() + aOutlinerScale.getY() : aAdjustTranslate.getY());
    aNewTransformA.translate(fStartInX, fStartInY);

    // mirroring. We are now in aAnchorTextRange sizes. When mirroring in X and Y,
    // move the null point which was top left to bottom right.
    const bool bMirrorX(aScale.getX() < 0.0);
    const bool bMirrorY(aScale.getY() < 0.0);
    aNewTransformB.scale(bMirrorX ? -1.0 : 1.0, bMirrorY ? -1.0 : 1.0);

    // in-between the translations of the single primitives will take place. Afterwards,
    // the object's transformations need to be applied
    aNewTransformB.shearX(fShearX);
    aNewTransformB.rotate(fRotate);
    aNewTransformB.translate(aTranslate.getX(), aTranslate.getY());

    basegfx::B2DRange aClipRange;

    // now break up text primitives.
    impTextBreakupHandler aConverter(rOutliner);
    aConverter.decomposeBlockTextPrimitive(aNewTransformA, aNewTransformB, aClipRange);

    // cleanup outliner
    rOutliner.Clear();
    rOutliner.setVisualizedPage(nullptr);
    rOutliner.SetControlWord(nOriginalControlWord);

    rTarget = aConverter.extractPrimitive2DSequence();
}

// Direct decomposer for text visualization when you already have a prepared
// Outliner containing all the needed information
void SdrTextObj::impDecomposeBlockTextPrimitiveDirect(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    SdrOutliner& rOutliner,
    const basegfx::B2DHomMatrix& rNewTransformA,
    const basegfx::B2DHomMatrix& rNewTransformB,
    const basegfx::B2DRange& rClipRange)
{
    impTextBreakupHandler aConverter(rOutliner);
    aConverter.decomposeBlockTextPrimitive(rNewTransformA, rNewTransformB, rClipRange);
    rTarget.append(aConverter.extractPrimitive2DSequence());
}

double SdrTextObj::GetCameraZRotation() const
{
    const css::uno::Any* pAny;
    double fTextCameraZRotateAngle = 0.0;
    const SfxItemSet& rSet = GetObjectItemSet();
    const SdrCustomShapeGeometryItem& rGeometryItem(rSet.Get(SDRATTR_CUSTOMSHAPE_GEOMETRY));

    pAny = rGeometryItem.GetPropertyValueByName(u"TextCameraZRotateAngle"_ustr);

    if ( pAny )
        *pAny >>= fTextCameraZRotateAngle;

    return fTextCameraZRotateAngle;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
