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

#include <tools/debug.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <sal/log.hxx>
#include <com/sun/star/animations/AnimationNodeType.hpp>
#include <com/sun/star/animations/AnimateColor.hpp>
#include <com/sun/star/animations/AnimateMotion.hpp>
#include <com/sun/star/animations/AnimateSet.hpp>
#include <com/sun/star/animations/AnimationFill.hpp>
#include <com/sun/star/animations/Audio.hpp>
#include <com/sun/star/animations/Command.hpp>
#include <com/sun/star/animations/Event.hpp>
#include <com/sun/star/animations/EventTrigger.hpp>
#include <com/sun/star/animations/IterateContainer.hpp>
#include <com/sun/star/animations/ParallelTimeContainer.hpp>
#include <com/sun/star/animations/SequenceTimeContainer.hpp>
#include <com/sun/star/animations/XCommand.hpp>
#include <com/sun/star/animations/XIterateContainer.hpp>
#include <com/sun/star/animations/XAnimateTransform.hpp>
#include <com/sun/star/animations/XAnimateMotion.hpp>
#include <com/sun/star/animations/XAnimate.hpp>
#include <com/sun/star/animations/AnimationRestart.hpp>
#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/presentation/EffectNodeType.hpp>
#include <com/sun/star/presentation/EffectCommands.hpp>
#include <com/sun/star/presentation/EffectPresetClass.hpp>
#include <com/sun/star/presentation/ParagraphTarget.hpp>
#include <com/sun/star/presentation/ShapeAnimationSubType.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/util/XCloneable.hpp>
#include <com/sun/star/util/XChangesNotifier.hpp>
#include <comphelper/processfactory.hxx>
#include <comphelper/sequence.hxx>
#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/i18n/BreakIterator.hpp>
#include <com/sun/star/i18n/CharacterIteratorMode.hpp>
#include <com/sun/star/i18n/WordType.hpp>
#include <com/sun/star/presentation/TextAnimationType.hpp>

#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/polygon/b2dpolypolygontools.hxx>
#include <basegfx/range/b2drange.hxx>
#include <basegfx/matrix/b2dhommatrixtools.hxx>

#include <algorithm>
#include <deque>
#include <numeric>

#include <cppuhelper/implbase.hxx>

#include <drawinglayer/geometry/viewinformation2d.hxx>
#include <o3tl/safeint.hxx>
#include <svx/sdr/contact/viewcontact.hxx>
#include <svx/svdopath.hxx>
#include <svx/svdpage.hxx>
#include <CustomAnimationEffect.hxx>
#include <CustomAnimationPreset.hxx>
#include <animations.hxx>
#include <utility>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::presentation;
using namespace ::com::sun::star::animations;

using ::com::sun::star::container::XEnumerationAccess;
using ::com::sun::star::container::XEnumeration;
using ::com::sun::star::beans::NamedValue;
using ::com::sun::star::container::XChild;
using ::com::sun::star::drawing::XShape;
using ::com::sun::star::lang::XInitialization;
using ::com::sun::star::text::XText;
using ::com::sun::star::text::XTextRange;
using ::com::sun::star::beans::XPropertySet;
using ::com::sun::star::util::XCloneable;
using ::com::sun::star::lang::Locale;
using ::com::sun::star::util::XChangesNotifier;
using ::com::sun::star::util::XChangesListener;

namespace sd
{
class MainSequenceChangeGuard
{
public:
    explicit MainSequenceChangeGuard( EffectSequenceHelper* pSequence )
    {
        mpMainSequence = dynamic_cast< MainSequence* >( pSequence );
        if( mpMainSequence == nullptr )
        {
            InteractiveSequence* pI = dynamic_cast< InteractiveSequence* >( pSequence );
            if( pI )
                mpMainSequence = pI->mpMainSequence;
        }
        DBG_ASSERT( mpMainSequence, "sd::MainSequenceChangeGuard::MainSequenceChangeGuard(), no main sequence to guard!" );

        if( mpMainSequence )
            mpMainSequence->mbIgnoreChanges++;
    }

    ~MainSequenceChangeGuard()
    {
        if( mpMainSequence )
            mpMainSequence->mbIgnoreChanges++;
    }

private:
    MainSequence* mpMainSequence;
};

CustomAnimationEffect::CustomAnimationEffect( const css::uno::Reference< css::animations::XAnimationNode >& xNode )
:   mnNodeType(-1),
    mnPresetClass(-1),
    mnFill(AnimationFill::HOLD),
    mfBegin(-1.0),
    mfDuration(-1.0),
    mfAbsoluteDuration(-1.0),
    mnGroupId(-1),
    mnIterateType(0),
    mfIterateInterval(0.0),
    mnParaDepth( -1 ),
    mbHasText(false),
    mfAcceleration( 1.0 ),
    mfDecelerate( 1.0 ),
    mbAutoReverse(false),
    mnTargetSubItem(0),
    mnCommand(0),
    mpEffectSequence( nullptr ),
    mbHasAfterEffect(false),
    mbAfterEffectOnNextEffect(false)
{
    setNode( xNode );
}

void CustomAnimationEffect::setNode( const css::uno::Reference< css::animations::XAnimationNode >& xNode )
{
    mxNode = xNode;
    mxAudio.clear();
    mnCommand = 0;

    const Sequence< NamedValue > aUserData( mxNode->getUserData() );

    for( const NamedValue& rProp : aUserData )
    {
        if ( rProp.Name == "node-type" )
        {
            rProp.Value >>= mnNodeType;
        }
        else if ( rProp.Name == "preset-id" )
        {
            rProp.Value >>= maPresetId;
        }
        else if ( rProp.Name == "preset-sub-type" )
        {
            rProp.Value >>= maPresetSubType;
        }
        else if ( rProp.Name == "preset-class" )
        {
            rProp.Value >>= mnPresetClass;
        }
        else if ( rProp.Name == "preset-property" )
        {
            rProp.Value >>= maProperty;
        }
        else if ( rProp.Name == "group-id" )
        {
            rProp.Value >>= mnGroupId;
        }
    }

    // get effect start time
    mxNode->getBegin() >>= mfBegin;

    mfAcceleration = mxNode->getAcceleration();
    mfDecelerate = mxNode->getDecelerate();
    mbAutoReverse = mxNode->getAutoReverse();

    mnFill = mxNode->getFill();

    // get iteration data
    Reference< XIterateContainer > xIter( mxNode, UNO_QUERY );
    if( xIter.is() )
    {
        mfIterateInterval = xIter->getIterateInterval();
        mnIterateType = xIter->getIterateType();
        maTarget = xIter->getTarget();
        mnTargetSubItem = xIter->getSubItem();
    }
    else
    {
        mfIterateInterval = 0.0f;
        mnIterateType = 0;
    }

    // calculate effect duration and get target shape
    Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
    if( xEnumerationAccess.is() )
    {
        Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
        if( xEnumeration.is() )
        {
            while( xEnumeration->hasMoreElements() )
            {
                Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY );
                if( !xChildNode.is() )
                    continue;

                if( xChildNode->getType() == AnimationNodeType::AUDIO )
                {
                    mxAudio.set( xChildNode, UNO_QUERY );
                }
                else if( xChildNode->getType() == AnimationNodeType::COMMAND )
                {
                    Reference< XCommand > xCommand( xChildNode, UNO_QUERY );
                    if( xCommand.is() )
                    {
                        mnCommand = xCommand->getCommand();
                        if( !maTarget.hasValue() )
                            maTarget = xCommand->getTarget();
                    }
                }
                else
                {
                    double fBegin = 0.0;
                    double fDuration = 0.0;
                    xChildNode->getBegin() >>= fBegin;
                    xChildNode->getDuration() >>= fDuration;

                    fDuration += fBegin;
                    if( fDuration > mfDuration )
                        mfDuration = fDuration;

                    // no target shape yet?
                    if( !maTarget.hasValue() )
                    {
                        // go get it boys!
                        Reference< XAnimate > xAnimate( xChildNode, UNO_QUERY );
                        if( xAnimate.is() )
                        {
                            maTarget = xAnimate->getTarget();
                            mnTargetSubItem = xAnimate->getSubItem();
                        }
                    }
                }
            }
        }
    }

    mfAbsoluteDuration = mfDuration;
    double fRepeatCount = 1.0;
    if( (mxNode->getRepeatCount()) >>= fRepeatCount )
        mfAbsoluteDuration *= fRepeatCount;

    checkForText();
}

sal_Int32 CustomAnimationEffect::getNumberOfSubitems( const Any& aTarget, sal_Int16 nIterateType )
{
    sal_Int32 nSubItems = 0;

    try
    {
        // first get target text
        sal_Int32 nOnlyPara = -1;

        Reference< XText > xShape;
        aTarget >>= xShape;
        if( !xShape.is() )
        {
            ParagraphTarget aParaTarget;
            if( aTarget >>= aParaTarget )
            {
                xShape.set( aParaTarget.Shape, UNO_QUERY );
                nOnlyPara = aParaTarget.Paragraph;
            }
        }

        // now use the break iterator to iterate over the given text
        // and count the sub items

        if( xShape.is() )
        {
            // TODO/LATER: Optimize this, don't create a break iterator each time
            const Reference< uno::XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );
            Reference < i18n::XBreakIterator > xBI = i18n::BreakIterator::create(xContext);

            Reference< XEnumerationAccess > xEA( xShape, UNO_QUERY_THROW );
            Reference< XEnumeration > xEnumeration( xEA->createEnumeration(), UNO_SET_THROW );
            css::lang::Locale aLocale;
            static constexpr OUStringLiteral aStrLocaleName( u"CharLocale" );
            Reference< XTextRange > xParagraph;

            sal_Int32 nPara = 0;
            while( xEnumeration->hasMoreElements() )
            {
                xEnumeration->nextElement() >>= xParagraph;

                // skip this if it's not the only paragraph we want to count
                if( (nOnlyPara != -1) && (nOnlyPara != nPara ) )
                    continue;

                if( nIterateType == TextAnimationType::BY_PARAGRAPH )
                {
                    nSubItems++;
                }
                else
                {
                    const OUString aText( xParagraph->getString() );
                    Reference< XPropertySet > xSet( xParagraph, UNO_QUERY_THROW );
                    xSet->getPropertyValue( aStrLocaleName ) >>= aLocale;

                    sal_Int32 nPos;
                    const sal_Int32 nEndPos = aText.getLength();

                    if( nIterateType == TextAnimationType::BY_WORD )
                    {
                        for( nPos = 0; nPos < nEndPos; nPos++ )
                        {
                            nPos = xBI->getWordBoundary(aText, nPos, aLocale, i18n::WordType::ANY_WORD, true).endPos;
                            nSubItems++;
                        }
                        break;
                    }
                    else
                    {
                        sal_Int32 nDone;
                        for( nPos = 0; nPos < nEndPos; nPos++ )
                        {
                            nPos = xBI->nextCharacters(aText, nPos, aLocale, i18n::CharacterIteratorMode::SKIPCELL, 0, nDone);
                            nSubItems++;
                        }
                    }
                }

                if( nPara == nOnlyPara )
                    break;

                nPara++;
            }
        }
    }
    catch( Exception& )
    {
        nSubItems = 0;
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::getNumberOfSubitems(), exception caught!" );
    }

    return nSubItems;
}

CustomAnimationEffect::~CustomAnimationEffect()
{
}

CustomAnimationEffectPtr CustomAnimationEffect::clone() const
{
    Reference< XCloneable > xCloneable( mxNode, UNO_QUERY_THROW );
    Reference< XAnimationNode > xNode( xCloneable->createClone(), UNO_QUERY_THROW );
    CustomAnimationEffectPtr pEffect = std::make_shared<CustomAnimationEffect>( xNode );
    pEffect->setEffectSequence( getEffectSequence() );
    return pEffect;
}

sal_Int32 CustomAnimationEffect::get_node_type( const Reference< XAnimationNode >& xNode )
{
    sal_Int16 nNodeType = -1;

    if( xNode.is() )
    {
        const Sequence< NamedValue > aUserData( xNode->getUserData() );
        if( aUserData.hasElements() )
        {
            const NamedValue* pProp = std::find_if(aUserData.begin(), aUserData.end(),
                [](const NamedValue& rProp) { return rProp.Name == "node-type"; });
            if (pProp != aUserData.end())
                pProp->Value >>= nNodeType;
        }
    }

    return nNodeType;
}

void CustomAnimationEffect::setPresetClassAndId( sal_Int16 nPresetClass, const OUString& rPresetId )
{
    if( mnPresetClass == nPresetClass && maPresetId == rPresetId )
        return;

    mnPresetClass = nPresetClass;
    maPresetId = rPresetId;
    if( !mxNode.is() )
        return;

    // first try to find a "preset-class" entry in the user data
    // and change it
    Sequence< NamedValue > aUserData( mxNode->getUserData() );
    sal_Int32 nLength = aUserData.getLength();
    bool bFoundPresetClass = false;
    bool bFoundPresetId = false;
    if( nLength )
    {
        auto [begin, end] = asNonConstRange(aUserData);
        NamedValue* pProp = std::find_if(begin, end,
            [](const NamedValue& rProp) { return rProp.Name == "preset-class"; });
        if (pProp != end)
        {
            pProp->Value <<= mnPresetClass;
            bFoundPresetClass = true;
        }

        pProp = std::find_if(begin, end,
            [](const NamedValue& rProp) { return rProp.Name == "preset-id"; });
        if (pProp != end)
        {
            pProp->Value <<= mnPresetClass;
            bFoundPresetId = true;
        }
    }

    // no "preset-class" entry inside user data, so add it
    if( !bFoundPresetClass )
    {
        aUserData.realloc( nLength + 1);
        auto& el = aUserData.getArray()[nLength];
        el.Name = "preset-class";
        el.Value <<= mnPresetClass;
        ++nLength;
    }

    if( !bFoundPresetId && maPresetId.getLength() > 0 )
    {
        aUserData.realloc( nLength + 1);
        auto& el = aUserData.getArray()[nLength];
        el.Name = "preset-id";
        el.Value <<= maPresetId;
    }

    mxNode->setUserData( aUserData );
}

void CustomAnimationEffect::setNodeType( sal_Int16 nNodeType )
{
    if( mnNodeType == nNodeType )
        return;

    mnNodeType = nNodeType;
    if( !mxNode.is() )
        return;

    // first try to find a "node-type" entry in the user data
    // and change it
    Sequence< NamedValue > aUserData( mxNode->getUserData() );
    sal_Int32 nLength = aUserData.getLength();
    bool bFound = false;
    if( nLength )
    {
        auto [begin, end] = asNonConstRange(aUserData);
        NamedValue* pProp = std::find_if(begin, end,
            [](const NamedValue& rProp) { return rProp.Name == "node-type"; });
        if (pProp != end)
        {
            pProp->Value <<= mnNodeType;
            bFound = true;
        }
    }

    // no "node-type" entry inside user data, so add it
    if( !bFound )
    {
        aUserData.realloc( nLength + 1);
        auto& el = aUserData.getArray()[nLength];
        el.Name = "node-type";
        el.Value <<= mnNodeType;
    }

    mxNode->setUserData( aUserData );
}

void CustomAnimationEffect::setGroupId( sal_Int32 nGroupId )
{
    mnGroupId = nGroupId;
    if( !mxNode.is() )
        return;

    // first try to find a "group-id" entry in the user data
    // and change it
    Sequence< NamedValue > aUserData( mxNode->getUserData() );
    sal_Int32 nLength = aUserData.getLength();
    bool bFound = false;
    if( nLength )
    {
        auto [begin, end] = asNonConstRange(aUserData);
        NamedValue* pProp = std::find_if(begin, end,
            [](const NamedValue& rProp) { return rProp.Name == "group-id"; });
        if (pProp != end)
        {
            pProp->Value <<= mnGroupId;
            bFound = true;
        }
    }

    // no "group-id" entry inside user data, so add it
    if( !bFound )
    {
        aUserData.realloc( nLength + 1);
        auto& el = aUserData.getArray()[nLength];
        el.Name = "group-id";
        el.Value <<= mnGroupId;
    }

    mxNode->setUserData( aUserData );
}

/** checks if the text for this effect has changed and updates internal flags.
    returns true if something changed.
*/
bool CustomAnimationEffect::checkForText( const std::vector<sal_Int32>* paragraphNumberingLevel )
{
    bool bChange = false;

    Reference< XText > xText;

    if( maTarget.getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
    {
        // calc para depth
        ParagraphTarget aParaTarget;
        maTarget >>= aParaTarget;

        xText.set( aParaTarget.Shape, UNO_QUERY );

        // get paragraph
        if( xText.is() )
        {
            sal_Int32 nPara = aParaTarget.Paragraph;

            bool bHasText = false;
            sal_Int32 nParaDepth = 0;

            if ( paragraphNumberingLevel )
            {
                bHasText = !paragraphNumberingLevel->empty();
                if (nPara >= 0 && o3tl::make_unsigned(nPara) < paragraphNumberingLevel->size())
                    nParaDepth = paragraphNumberingLevel->at(nPara);
            }
            else
            {
                Reference< XEnumerationAccess > xEA( xText, UNO_QUERY );
                if( xEA.is() )
                {
                    Reference< XEnumeration > xEnumeration = xEA->createEnumeration();
                    if( xEnumeration.is() )
                    {
                        bHasText = xEnumeration->hasMoreElements();

                        while( xEnumeration->hasMoreElements() && nPara-- )
                            xEnumeration->nextElement();

                        if( xEnumeration->hasMoreElements() )
                        {
                            Reference< XPropertySet > xParaSet;
                            xEnumeration->nextElement() >>= xParaSet;
                            if( xParaSet.is() )
                            {
                                xParaSet->getPropertyValue( u"NumberingLevel"_ustr ) >>= nParaDepth;
                            }
                        }
                    }
                }
            }

            if( bHasText )
            {
                bChange |= bHasText != mbHasText;
                mbHasText = bHasText;

                bChange |= nParaDepth != mnParaDepth;
                mnParaDepth = nParaDepth;
            }
        }
    }
    else
    {
        maTarget >>= xText;
        bool bHasText = xText.is() && !xText->getString().isEmpty();
        bChange |= bHasText != mbHasText;
        mbHasText = bHasText;
    }

    bChange |= calculateIterateDuration();
    return bChange;
}

bool CustomAnimationEffect::calculateIterateDuration()
{
    bool bChange = false;

    // if we have an iteration, we must also calculate the
    // 'true' container duration, that is
    // ( ( is form animated ) ? [contained effects duration] : 0 ) +
    // ( [number of animated children] - 1 ) * [interval-delay] + [contained effects duration]
    Reference< XIterateContainer > xIter( mxNode, UNO_QUERY );
    if( xIter.is() )
    {
        double fDuration = mfDuration;
        const double fSubEffectDuration = mfDuration;

        if( mnTargetSubItem != ShapeAnimationSubType::ONLY_BACKGROUND ) // does not make sense for iterate container but better check
        {
            const sal_Int32 nSubItems = getNumberOfSubitems( maTarget, mnIterateType );
            if( nSubItems )
            {
                const double f = (nSubItems-1) * mfIterateInterval;
                fDuration += f;
            }
        }

        // if we also animate the form first, we have to add the
        // sub effect duration to the whole effect duration
        if( mnTargetSubItem == ShapeAnimationSubType::AS_WHOLE )
            fDuration += fSubEffectDuration;

        bChange |= fDuration != mfAbsoluteDuration;
        mfAbsoluteDuration = fDuration;
    }

    return bChange;
}

void CustomAnimationEffect::setTarget( const css::uno::Any& rTarget )
{
    try
    {
        maTarget = rTarget;

        // first, check special case for random node
        Reference< XInitialization > xInit( mxNode, UNO_QUERY );
        if( xInit.is() )
        {
            const Sequence< Any > aArgs( &maTarget, 1 );
            xInit->initialize( aArgs );
        }
        else
        {
            Reference< XIterateContainer > xIter( mxNode, UNO_QUERY );
            if( xIter.is() )
            {
                xIter->setTarget(maTarget);
            }
            else
            {
                Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
                if( xEnumerationAccess.is() )
                {
                    Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
                    if( xEnumeration.is() )
                    {
                        while( xEnumeration->hasMoreElements() )
                        {
                            const Any aElem( xEnumeration->nextElement() );
                            Reference< XAnimate > xAnimate( aElem, UNO_QUERY );
                            if( xAnimate.is() )
                                xAnimate->setTarget( rTarget );
                            else
                            {
                                Reference< XCommand > xCommand( aElem, UNO_QUERY );
                                if( xCommand.is() )
                                    xCommand->setTarget( rTarget );
                            }
                        }
                    }
                }
            }
        }
        checkForText();
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setTarget()" );
    }
}

void CustomAnimationEffect::setTargetSubItem( sal_Int16 nSubItem )
{
    try
    {
        mnTargetSubItem = nSubItem;

        Reference< XIterateContainer > xIter( mxNode, UNO_QUERY );
        if( xIter.is() )
        {
            xIter->setSubItem(mnTargetSubItem);
        }
        else
        {
            Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
            if( xEnumerationAccess.is() )
            {
                Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
                if( xEnumeration.is() )
                {
                    while( xEnumeration->hasMoreElements() )
                    {
                        Reference< XAnimate > xAnimate( xEnumeration->nextElement(), UNO_QUERY );
                        if( xAnimate.is() )
                            xAnimate->setSubItem( mnTargetSubItem );
                    }
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setTargetSubItem()" );
    }
}

void CustomAnimationEffect::setDuration( double fDuration )
{
    if( (mfDuration == -1.0) || (mfDuration == fDuration) )
        return;

    try
    {
        double fScale = fDuration / mfDuration;
        mfDuration = fDuration;
        double fRepeatCount = 1.0;
        getRepeatCount() >>= fRepeatCount;
        mfAbsoluteDuration = mfDuration * fRepeatCount;

        // calculate effect duration and get target shape
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
        if( xEnumerationAccess.is() )
        {
            Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
            if( xEnumeration.is() )
            {
                while( xEnumeration->hasMoreElements() )
                {
                    Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY );
                    if( !xChildNode.is() )
                        continue;

                    double fChildBegin = 0.0;
                    xChildNode->getBegin() >>= fChildBegin;
                    if(  fChildBegin != 0.0 )
                    {
                        fChildBegin *= fScale;
                        xChildNode->setBegin( Any( fChildBegin ) );
                    }

                    double fChildDuration = 0.0;
                    xChildNode->getDuration() >>= fChildDuration;
                    if( fChildDuration != 0.0 )
                    {
                        fChildDuration *= fScale;
                        xChildNode->setDuration( Any( fChildDuration ) );
                    }
                }
            }
        }
        calculateIterateDuration();
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setDuration()" );
    }
}

void CustomAnimationEffect::setBegin( double fBegin )
{
    if( mxNode.is() ) try
    {
        mfBegin = fBegin;
        mxNode->setBegin( Any( fBegin ) );
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setBegin()" );
    }
}

void CustomAnimationEffect::setAcceleration( double fAcceleration )
{
    if( mxNode.is() ) try
    {
        mfAcceleration = fAcceleration;
        mxNode->setAcceleration( fAcceleration );
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setAcceleration()" );
    }
}

void CustomAnimationEffect::setDecelerate( double fDecelerate )
{
    if( mxNode.is() ) try
    {
        mfDecelerate = fDecelerate;
        mxNode->setDecelerate( fDecelerate );
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setDecelerate()" );
    }
}

void CustomAnimationEffect::setAutoReverse( bool bAutoReverse )
{
    if( mxNode.is() ) try
    {
        mbAutoReverse = bAutoReverse;
        mxNode->setAutoReverse( bAutoReverse );
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setAutoReverse()" );
    }
}

void CustomAnimationEffect::replaceNode( const css::uno::Reference< css::animations::XAnimationNode >& xNode )
{
    sal_Int16 nNodeType = mnNodeType;
    Any aTarget = maTarget;

    sal_Int16 nFill = mnFill;
    double fBegin = mfBegin;
    double fDuration = mfDuration;
    double fAcceleration = mfAcceleration;
    double fDecelerate = mfDecelerate ;
    bool bAutoReverse = mbAutoReverse;
    Reference< XAudio > xAudio( mxAudio );
    sal_Int16 nIterateType = mnIterateType;
    double fIterateInterval = mfIterateInterval;
    sal_Int16 nSubItem = mnTargetSubItem;

    setNode( xNode );

    setAudio( xAudio );
    setNodeType( nNodeType );
    setTarget( aTarget );
    setTargetSubItem( nSubItem );
    setDuration( fDuration );
    setBegin( fBegin );
    setFill( nFill );

    setAcceleration( fAcceleration );
    setDecelerate( fDecelerate );
    setAutoReverse( bAutoReverse );

    if( nIterateType != mnIterateType )
        setIterateType( nIterateType );

    if( mnIterateType && ( fIterateInterval != mfIterateInterval ) )
        setIterateInterval( fIterateInterval );
}

Reference< XShape > CustomAnimationEffect::getTargetShape() const
{
    Reference< XShape > xShape;
    maTarget >>= xShape;
    if( !xShape.is() )
    {
        ParagraphTarget aParaTarget;
        if( maTarget >>= aParaTarget )
            xShape = aParaTarget.Shape;
    }

    return xShape;
}

Any CustomAnimationEffect::getRepeatCount() const
{
    if( mxNode.is() )
    {
        return mxNode->getRepeatCount();
    }
    else
    {
        Any aAny;
        return aAny;
    }
}

Any CustomAnimationEffect::getEnd() const
{
    if( mxNode.is() )
    {
        return mxNode->getEnd();
    }
    else
    {
        Any aAny;
        return aAny;
    }
}

void CustomAnimationEffect::setRepeatCount( const Any& rRepeatCount )
{
    if( mxNode.is() )
    {
        mxNode->setRepeatCount( rRepeatCount );
        double fRepeatCount = 1.0;
        rRepeatCount >>= fRepeatCount;
        mfAbsoluteDuration = mfDuration * fRepeatCount;
    }
}

void CustomAnimationEffect::setEnd( const Any& rEnd )
{
    if( mxNode.is() )
        mxNode->setEnd( rEnd );
}

void CustomAnimationEffect::setFill( sal_Int16 nFill )
{
    if (mxNode.is())
    {
        mnFill = nFill;
        mxNode->setFill( nFill );
    }
}

Reference< XAnimationNode > CustomAnimationEffect::createAfterEffectNode() const
{
    DBG_ASSERT( mbHasAfterEffect, "sd::CustomAnimationEffect::createAfterEffectNode(), this node has no after effect!" );

    const Reference< XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );

    Reference< XAnimate > xAnimate;
    if( maDimColor.hasValue() )
        xAnimate = AnimateColor::create( xContext );
    else
        xAnimate = AnimateSet::create( xContext );

    Any aTo;
    OUString aAttributeName;

    if( maDimColor.hasValue() )
    {
        aTo = maDimColor;
        aAttributeName = "DimColor";
    }
    else
    {
        aTo <<= false;
        aAttributeName = "Visibility";
    }

    Any aBegin;
    if( !mbAfterEffectOnNextEffect ) // sameClick
    {
        Event aEvent;

        aEvent.Source <<= getNode();
        aEvent.Trigger = EventTrigger::END_EVENT;
        aEvent.Repeat = 0;

        aBegin <<= aEvent;
    }
    else
    {
        aBegin <<= 0.0;
    }

    xAnimate->setBegin( aBegin );
    xAnimate->setTo( aTo );
    xAnimate->setAttributeName( aAttributeName );

    xAnimate->setDuration( Any( 0.001 ) );
    xAnimate->setFill( AnimationFill::HOLD );
    xAnimate->setTarget( maTarget );

    return xAnimate;
}

void CustomAnimationEffect::setIterateType( sal_Int16 nIterateType )
{
    if( mnIterateType == nIterateType )
        return;

    try
    {
        // do we need to exchange the container node?
        if( (mnIterateType == 0) || (nIterateType == 0) )
        {
            sal_Int16 nTargetSubItem = mnTargetSubItem;

            const Reference< XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );
            Reference< XTimeContainer > xNewContainer;
            if(nIterateType)
            {
                xNewContainer.set( IterateContainer::create( xContext ) );
            }
            else
                xNewContainer.set( ParallelTimeContainer::create( xContext ), UNO_QUERY_THROW );

            Reference< XTimeContainer > xOldContainer( mxNode, UNO_QUERY_THROW );
            Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY_THROW );
            Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
            while( xEnumeration->hasMoreElements() )
            {
                Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY_THROW );
                xOldContainer->removeChild( xChildNode );
                xNewContainer->appendChild( xChildNode );
            }

            xNewContainer->setBegin( mxNode->getBegin() );
            xNewContainer->setDuration( mxNode->getDuration() );
            xNewContainer->setEnd( mxNode->getEnd() );
            xNewContainer->setEndSync( mxNode->getEndSync() );
            xNewContainer->setRepeatCount( mxNode->getRepeatCount() );
            xNewContainer->setFill( mxNode->getFill() );
            xNewContainer->setFillDefault( mxNode->getFillDefault() );
            xNewContainer->setRestart( mxNode->getRestart() );
            xNewContainer->setRestartDefault( mxNode->getRestartDefault() );
            xNewContainer->setAcceleration( mxNode->getAcceleration() );
            xNewContainer->setDecelerate( mxNode->getDecelerate() );
            xNewContainer->setAutoReverse( mxNode->getAutoReverse() );
            xNewContainer->setRepeatDuration( mxNode->getRepeatDuration() );
            xNewContainer->setEndSync( mxNode->getEndSync() );
            xNewContainer->setRepeatCount( mxNode->getRepeatCount() );
            xNewContainer->setUserData( mxNode->getUserData() );

            mxNode = xNewContainer;

            Any aTarget;
            if( nIterateType )
            {
                Reference< XIterateContainer > xIter( mxNode, UNO_QUERY_THROW );
                xIter->setTarget(maTarget);
                xIter->setSubItem( nTargetSubItem );
            }
            else
            {
                aTarget = maTarget;
            }

            Reference< XEnumerationAccess > xEA( mxNode, UNO_QUERY_THROW );
            Reference< XEnumeration > xE( xEA->createEnumeration(), UNO_SET_THROW );
            while( xE->hasMoreElements() )
            {
                Reference< XAnimate > xAnimate( xE->nextElement(), UNO_QUERY );
                if( xAnimate.is() )
                {
                    xAnimate->setTarget( aTarget );
                    xAnimate->setSubItem( nTargetSubItem );
                }
            }
        }

        mnIterateType = nIterateType;

        // if we have an iteration container, we must set its type
        if( mnIterateType )
        {
            Reference< XIterateContainer > xIter( mxNode, UNO_QUERY_THROW );
            xIter->setIterateType( nIterateType );
        }

        checkForText();
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setIterateType()" );
    }
}

void CustomAnimationEffect::setIterateInterval( double fIterateInterval )
{
    if( mfIterateInterval == fIterateInterval )
        return;

    Reference< XIterateContainer > xIter( mxNode, UNO_QUERY );

    DBG_ASSERT( xIter.is(), "sd::CustomAnimationEffect::setIterateInterval(), not an iteration node" );
    if( xIter.is() )
    {
        mfIterateInterval = fIterateInterval;
        xIter->setIterateInterval( fIterateInterval );
    }

    calculateIterateDuration();
}

OUString CustomAnimationEffect::getPath() const
{
    OUString aPath;

    if( mxNode.is() ) try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        while( xEnumeration->hasMoreElements() )
        {
            Reference< XAnimateMotion > xMotion( xEnumeration->nextElement(), UNO_QUERY );
            if( xMotion.is() )
            {
                xMotion->getPath() >>= aPath;
                break;
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::getPath()" );
    }

    return aPath;
}

void CustomAnimationEffect::setPath( const OUString& rPath )
{
    if( !mxNode.is() )
        return;

    try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        while( xEnumeration->hasMoreElements() )
        {
            Reference< XAnimateMotion > xMotion( xEnumeration->nextElement(), UNO_QUERY );
            if( xMotion.is() )
            {

                MainSequenceChangeGuard aGuard( mpEffectSequence );
                xMotion->setPath( Any( rPath ) );
                break;
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setPath()" );
    }
}

Any CustomAnimationEffect::getProperty( sal_Int32 nNodeType, std::u16string_view rAttributeName, EValue eValue )
{
    Any aProperty;
    if( mxNode.is() ) try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
        if( xEnumerationAccess.is() )
        {
            Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
            if( xEnumeration.is() )
            {
                while( xEnumeration->hasMoreElements() && !aProperty.hasValue() )
                {
                    Reference< XAnimate > xAnimate( xEnumeration->nextElement(), UNO_QUERY );
                    if( !xAnimate.is() )
                        continue;

                    if( xAnimate->getType() == nNodeType )
                    {
                        if( xAnimate->getAttributeName() == rAttributeName )
                        {
                            switch( eValue )
                            {
                            case EValue::To:   aProperty = xAnimate->getTo(); break;
                            case EValue::By:   aProperty = xAnimate->getBy(); break;
                            }
                        }
                    }
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::getProperty()" );
    }

    return aProperty;
}

bool CustomAnimationEffect::setProperty( sal_Int32 nNodeType, std::u16string_view rAttributeName, EValue eValue, const Any& rValue )
{
    bool bChanged = false;
    if( mxNode.is() ) try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
        if( xEnumerationAccess.is() )
        {
            Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
            if( xEnumeration.is() )
            {
                while( xEnumeration->hasMoreElements() )
                {
                    Reference< XAnimate > xAnimate( xEnumeration->nextElement(), UNO_QUERY );
                    if( !xAnimate.is() )
                        continue;

                    if( xAnimate->getType() == nNodeType )
                    {
                        if( xAnimate->getAttributeName() == rAttributeName )
                        {
                            switch( eValue )
                            {
                            case EValue::To:
                                if( xAnimate->getTo() != rValue )
                                {
                                    xAnimate->setTo( rValue );
                                    bChanged = true;
                                }
                                break;
                            case EValue::By:
                                if( xAnimate->getTo() != rValue )
                                {
                                    xAnimate->setBy( rValue );
                                    bChanged = true;
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setProperty()" );
    }

    return bChanged;
}

static bool implIsColorAttribute( std::u16string_view rAttributeName )
{
    return rAttributeName == u"FillColor" || rAttributeName == u"LineColor" || rAttributeName == u"CharColor";
}

Any CustomAnimationEffect::getColor( sal_Int32 nIndex )
{
    Any aColor;
    if( mxNode.is() ) try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
        if( xEnumerationAccess.is() )
        {
            Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
            if( xEnumeration.is() )
            {
                while( xEnumeration->hasMoreElements() && !aColor.hasValue() )
                {
                    Reference< XAnimate > xAnimate( xEnumeration->nextElement(), UNO_QUERY );
                    if( !xAnimate.is() )
                        continue;

                    switch( xAnimate->getType() )
                    {
                    case AnimationNodeType::SET:
                    case AnimationNodeType::ANIMATE:
                        if( !implIsColorAttribute( xAnimate->getAttributeName() ) )
                            break;
                        [[fallthrough]];
                    case AnimationNodeType::ANIMATECOLOR:
                        Sequence<Any> aValues( xAnimate->getValues() );
                        if( aValues.hasElements() )
                        {
                            if( aValues.getLength() > nIndex )
                                aColor = aValues[nIndex];
                        }
                        else if( nIndex == 0 )
                            aColor = xAnimate->getFrom();
                        else
                            aColor = xAnimate->getTo();
                    }
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::getColor()" );
    }

    return aColor;
}

void CustomAnimationEffect::setColor( sal_Int32 nIndex, const Any& rColor )
{
    if( !mxNode.is() )
        return;

    try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
        if( xEnumerationAccess.is() )
        {
            Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
            if( xEnumeration.is() )
            {
                while( xEnumeration->hasMoreElements() )
                {
                    Reference< XAnimate > xAnimate( xEnumeration->nextElement(), UNO_QUERY );
                    if( !xAnimate.is() )
                        continue;

                    switch( xAnimate->getType() )
                    {
                    case AnimationNodeType::SET:
                    case AnimationNodeType::ANIMATE:
                        if( !implIsColorAttribute( xAnimate->getAttributeName() ) )
                            break;
                        [[fallthrough]];
                    case AnimationNodeType::ANIMATECOLOR:
                    {
                        Sequence<Any> aValues( xAnimate->getValues() );
                        if( aValues.hasElements() )
                        {
                            if( aValues.getLength() > nIndex )
                            {
                                aValues.getArray()[nIndex] = rColor;
                                xAnimate->setValues( aValues );
                            }
                        }
                        else if( (nIndex == 0) && xAnimate->getFrom().hasValue() )
                            xAnimate->setFrom(rColor);
                        else if( (nIndex == 1) && xAnimate->getTo().hasValue() )
                            xAnimate->setTo(rColor);
                    }
                    break;

                    }
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setColor()" );
    }
}

Any CustomAnimationEffect::getTransformationProperty( sal_Int32 nTransformType, EValue eValue )
{
    Any aProperty;
    if( mxNode.is() ) try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
        if( xEnumerationAccess.is() )
        {
            Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
            if( xEnumeration.is() )
            {
                while( xEnumeration->hasMoreElements() && !aProperty.hasValue() )
                {
                    Reference< XAnimateTransform > xTransform( xEnumeration->nextElement(), UNO_QUERY );
                    if( !xTransform.is() )
                        continue;

                    if( xTransform->getTransformType() == nTransformType )
                    {
                        switch( eValue )
                        {
                        case EValue::To:   aProperty = xTransform->getTo(); break;
                        case EValue::By:   aProperty = xTransform->getBy(); break;
                        }
                    }
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::getTransformationProperty()" );
    }

    return aProperty;
}

bool CustomAnimationEffect::setTransformationProperty( sal_Int32 nTransformType, EValue eValue, const Any& rValue )
{
    bool bChanged = false;
    if( mxNode.is() ) try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxNode, UNO_QUERY );
        if( xEnumerationAccess.is() )
        {
            Reference< XEnumeration > xEnumeration = xEnumerationAccess->createEnumeration();
            if( xEnumeration.is() )
            {
                while( xEnumeration->hasMoreElements() )
                {
                    Reference< XAnimateTransform > xTransform( xEnumeration->nextElement(), UNO_QUERY );
                    if( !xTransform.is() )
                        continue;

                    if( xTransform->getTransformType() == nTransformType )
                    {
                        switch( eValue )
                        {
                        case EValue::To:
                            if( xTransform->getTo() != rValue )
                            {
                                xTransform->setTo( rValue );
                                bChanged = true;
                            }
                            break;
                        case EValue::By:
                            if( xTransform->getBy() != rValue )
                            {
                                xTransform->setBy( rValue );
                                bChanged = true;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setTransformationProperty()" );
    }

    return bChanged;
}

void CustomAnimationEffect::createAudio( const css::uno::Any& rSource )
{
    DBG_ASSERT( !mxAudio.is(), "sd::CustomAnimationEffect::createAudio(), node already has an audio!" );

    if( mxAudio.is() )
        return;

    try
    {
        const Reference< XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );
        Reference< XAudio > xAudio( Audio::create( xContext ) );
        xAudio->setSource( rSource );
        xAudio->setVolume( 1.0 );
        setAudio( xAudio );
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::createAudio()" );
    }
}

static Reference< XCommand > findCommandNode( const Reference< XAnimationNode >& xRootNode )
{
    Reference< XCommand > xCommand;

    if( xRootNode.is() ) try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( xRootNode, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        while( !xCommand.is() && xEnumeration->hasMoreElements() )
        {
            Reference< XAnimationNode > xNode( xEnumeration->nextElement(), UNO_QUERY );
            if( xNode.is() && (xNode->getType() == AnimationNodeType::COMMAND) )
                xCommand.set( xNode, UNO_QUERY_THROW );
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::findCommandNode()" );
    }

    return xCommand;
}

void CustomAnimationEffect::removeAudio()
{
    try
    {
        Reference< XAnimationNode > xChild;

        if( mxAudio.is() )
        {
            xChild = mxAudio;
            mxAudio.clear();
        }
        else if( mnCommand == EffectCommands::STOPAUDIO )
        {
            xChild = findCommandNode( mxNode );
            mnCommand = 0;
        }

        if( xChild.is() )
        {
            Reference< XTimeContainer > xContainer( mxNode, UNO_QUERY );
            if( xContainer.is() )
                xContainer->removeChild( xChild );
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::removeAudio()" );
    }

}

void CustomAnimationEffect::setAudio( const Reference< css::animations::XAudio >& xAudio )
{
    if( mxAudio == xAudio )
        return;

    try
    {
        removeAudio();
        mxAudio = xAudio;
        Reference< XTimeContainer > xContainer( mxNode, UNO_QUERY );
        if( xContainer.is() && mxAudio.is() )
            xContainer->appendChild( mxAudio );
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setAudio()" );
    }
}

void CustomAnimationEffect::setStopAudio()
{
    if( mnCommand == EffectCommands::STOPAUDIO )
        return;

    try
    {
        if( mxAudio.is() )
            removeAudio();

        const Reference< XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );
        Reference< XCommand > xCommand( Command::create( xContext ) );

        xCommand->setCommand( EffectCommands::STOPAUDIO );

        Reference< XTimeContainer > xContainer( mxNode, UNO_QUERY_THROW );
        xContainer->appendChild( xCommand );

        mnCommand = EffectCommands::STOPAUDIO;
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::CustomAnimationEffect::setStopAudio()" );
    }
}

bool CustomAnimationEffect::getStopAudio() const
{
    return mnCommand == EffectCommands::STOPAUDIO;
}

rtl::Reference<SdrPathObj> CustomAnimationEffect::createSdrPathObjFromPath(SdrModel& rTargetModel)
{
    rtl::Reference<SdrPathObj> pPathObj = new SdrPathObj(rTargetModel, SdrObjKind::PathLine);
    updateSdrPathObjFromPath( *pPathObj );
    return pPathObj;
}

void CustomAnimationEffect::updateSdrPathObjFromPath( SdrPathObj& rPathObj )
{
    ::basegfx::B2DPolyPolygon aPolyPoly;
    if( ::basegfx::utils::importFromSvgD( aPolyPoly, getPath(), true, nullptr ) )
    {
        SdrObject* pObj = SdrObject::getSdrObjectFromXShape(getTargetShape());
        if( pObj )
        {
            SdrPage* pPage = pObj->getSdrPageFromSdrObject();
            if( pPage )
            {
                const Size aPageSize( pPage->GetSize() );
                aPolyPoly.transform(basegfx::utils::createScaleB2DHomMatrix(static_cast<double>(aPageSize.Width()), static_cast<double>(aPageSize.Height())));
            }

            const ::tools::Rectangle aBoundRect( pObj->GetCurrentBoundRect() );
            const Point aCenter( aBoundRect.Center() );
            aPolyPoly.translate(aCenter.X(), aCenter.Y());
        }
    }

    rPathObj.SetPathPoly( aPolyPoly );
}

void CustomAnimationEffect::updatePathFromSdrPathObj( const SdrPathObj& rPathObj )
{
    ::basegfx::B2DPolyPolygon aPolyPoly( rPathObj.GetPathPoly() );

    SdrObject* pObj = SdrObject::getSdrObjectFromXShape(getTargetShape());
    if( pObj )
    {
        ::tools::Rectangle aBoundRect(0,0,0,0);

        drawinglayer::primitive2d::Primitive2DContainer xPrimitives;
        pObj->GetViewContact().getViewIndependentPrimitive2DContainer(xPrimitives);
        const drawinglayer::geometry::ViewInformation2D aViewInformation2D;
        const basegfx::B2DRange aRange(xPrimitives.getB2DRange(aViewInformation2D));

        if(!aRange.isEmpty())
        {
            aBoundRect = ::tools::Rectangle(
                    static_cast<sal_Int32>(floor(aRange.getMinX())), static_cast<sal_Int32>(floor(aRange.getMinY())),
                    static_cast<sal_Int32>(ceil(aRange.getMaxX())), static_cast<sal_Int32>(ceil(aRange.getMaxY())));
        }

        const Point aCenter( aBoundRect.Center() );

        aPolyPoly.translate(-aCenter.X(), -aCenter.Y());

        SdrPage* pPage = pObj->getSdrPageFromSdrObject();
        if( pPage )
        {
            const Size aPageSize( pPage->GetSize() );
            aPolyPoly.transform(basegfx::utils::createScaleB2DHomMatrix(
                1.0 / static_cast<double>(aPageSize.Width()), 1.0 / static_cast<double>(aPageSize.Height())));
        }
    }

    setPath( ::basegfx::utils::exportToSvgD( aPolyPoly, true, true, true) );
}

EffectSequenceHelper::EffectSequenceHelper()
: mnSequenceType( EffectNodeType::DEFAULT )
{
}

EffectSequenceHelper::EffectSequenceHelper( css::uno::Reference< css::animations::XTimeContainer > xSequenceRoot )
: mxSequenceRoot(std::move( xSequenceRoot )), mnSequenceType( EffectNodeType::DEFAULT )
{
    Reference< XAnimationNode > xNode( mxSequenceRoot, UNO_QUERY_THROW );
    create( xNode );
}

EffectSequenceHelper::~EffectSequenceHelper()
{
    reset();
}

void EffectSequenceHelper::reset()
{
    for( CustomAnimationEffectPtr& pEffect : maEffects )
    {
        pEffect->setEffectSequence(nullptr);
    }
    maEffects.clear();
}

Reference< XAnimationNode > EffectSequenceHelper::getRootNode()
{
    return mxSequenceRoot;
}

void EffectSequenceHelper::append( const CustomAnimationEffectPtr& pEffect )
{
    pEffect->setEffectSequence( this );
    maEffects.push_back(pEffect);
    rebuild();
}

CustomAnimationEffectPtr EffectSequenceHelper::append( const CustomAnimationPresetPtr& pPreset, const Any& rTarget, double fDuration /* = -1.0 */ )
{
    CustomAnimationEffectPtr pEffect;

    if( pPreset )
    {
        Reference< XAnimationNode > xNode( pPreset->create( u""_ustr ) );
        if( xNode.is() )
        {
            // first, filter all only ui relevant user data
            std::vector< NamedValue > aNewUserData;
            Sequence< NamedValue > aUserData( xNode->getUserData() );

            std::copy_if(std::cbegin(aUserData), std::cend(aUserData), std::back_inserter(aNewUserData),
                [](const NamedValue& rProp) { return rProp.Name != "text-only" && rProp.Name != "preset-property"; });

            if( !aNewUserData.empty() )
            {
                aUserData = ::comphelper::containerToSequence( aNewUserData );
                xNode->setUserData( aUserData );
            }

            // check target, maybe we need to force it to text
            sal_Int16 nSubItem = ShapeAnimationSubType::AS_WHOLE;

            if( rTarget.getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
            {
                nSubItem = ShapeAnimationSubType::ONLY_TEXT;
            }
            else if( pPreset->isTextOnly() )
            {
                Reference< XShape > xShape;
                rTarget >>= xShape;
                if( xShape.is() )
                {
                    // that's bad, we target a shape here but the effect is only for text
                    // so change subitem
                    nSubItem = ShapeAnimationSubType::ONLY_TEXT;
                }
            }

            // now create effect from preset
            pEffect = std::make_shared<CustomAnimationEffect>( xNode );
            pEffect->setEffectSequence( this );
            pEffect->setTarget( rTarget );
            pEffect->setTargetSubItem( nSubItem );
            if( fDuration != -1.0 )
                pEffect->setDuration( fDuration );

            maEffects.push_back(pEffect);

            rebuild();
        }
    }

    DBG_ASSERT( pEffect, "sd::EffectSequenceHelper::append(), failed!" );
    return pEffect;
}

CustomAnimationEffectPtr EffectSequenceHelper::append( const SdrPathObj& rPathObj, const Any& rTarget, double fDuration /* = -1.0 */, const OUString& rPresetId )
{
    CustomAnimationEffectPtr pEffect;

    if( fDuration <= 0.0 )
        fDuration = 2.0;

    try
    {
        Reference< XTimeContainer > xEffectContainer( ParallelTimeContainer::create( ::comphelper::getProcessComponentContext() ), UNO_QUERY_THROW );
        Reference< XAnimationNode > xAnimateMotion( AnimateMotion::create( ::comphelper::getProcessComponentContext() ) );

        xAnimateMotion->setDuration( Any( fDuration ) );
        xAnimateMotion->setFill( AnimationFill::HOLD );
        xEffectContainer->appendChild( xAnimateMotion );

        sal_Int16 nSubItem = ShapeAnimationSubType::AS_WHOLE;

        if( rTarget.getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
            nSubItem = ShapeAnimationSubType::ONLY_TEXT;

        pEffect = std::make_shared<CustomAnimationEffect>( xEffectContainer );
        pEffect->setEffectSequence( this );
        pEffect->setTarget( rTarget );
        pEffect->setTargetSubItem( nSubItem );
        pEffect->setNodeType( css::presentation::EffectNodeType::ON_CLICK );
        pEffect->setPresetClassAndId( css::presentation::EffectPresetClass::MOTIONPATH, rPresetId );
        pEffect->setAcceleration( 0.5 );
        pEffect->setDecelerate( 0.5 );
        pEffect->setFill( AnimationFill::HOLD );
        pEffect->setBegin( 0.0 );
        pEffect->updatePathFromSdrPathObj( rPathObj );
        if( fDuration != -1.0 )
            pEffect->setDuration( fDuration );

        maEffects.push_back(pEffect);

        rebuild();
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::EffectSequenceHelper::append()" );
    }

    return pEffect;
}

void EffectSequenceHelper::replace( const CustomAnimationEffectPtr& pEffect, const CustomAnimationPresetPtr& pPreset, const OUString& rPresetSubType, double fDuration /* = -1.0 */ )
{
    if( !(pEffect && pPreset) )
        return;

    try
    {
        Reference< XAnimationNode > xNewNode( pPreset->create( rPresetSubType ) );
        if( xNewNode.is() )
        {
            pEffect->replaceNode( xNewNode );
            if( fDuration != -1.0 )
                pEffect->setDuration( fDuration );
        }

        rebuild();
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::EffectSequenceHelper::replace()" );
    }
}

void EffectSequenceHelper::replace( const CustomAnimationEffectPtr& pEffect, const CustomAnimationPresetPtr& pPreset, double fDuration /* = -1.0 */ )
{
    replace( pEffect, pPreset, u""_ustr, fDuration );
}

void EffectSequenceHelper::remove( const CustomAnimationEffectPtr& pEffect )
{
    if( pEffect )
    {
        pEffect->setEffectSequence( nullptr );
        maEffects.remove( pEffect );
    }

    rebuild();
}

void EffectSequenceHelper::moveToBeforeEffect( const CustomAnimationEffectPtr& pEffect, const CustomAnimationEffectPtr& pInsertBefore)
{
    if ( pEffect )
    {
        maEffects.remove( pEffect );
        EffectSequence::iterator aInsertIter( find( pInsertBefore ) );

        // aInsertIter being end() is OK: pInsertBefore could be null, so put at end.
        maEffects.insert( aInsertIter, pEffect );

        rebuild();
    }
}

void EffectSequenceHelper::rebuild()
{
    implRebuild();
}

void EffectSequenceHelper::implRebuild()
{
    try
    {
        // first we delete all time containers on the first two levels
        Reference< XEnumerationAccess > xEnumerationAccess( mxSequenceRoot, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        while( xEnumeration->hasMoreElements() )
        {
            Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY_THROW );
            Reference< XTimeContainer > xChildContainer( xChildNode, UNO_QUERY_THROW );

            Reference< XEnumerationAccess > xChildEnumerationAccess( xChildNode, UNO_QUERY_THROW );
            Reference< XEnumeration > xChildEnumeration( xChildEnumerationAccess->createEnumeration(), UNO_SET_THROW );
            while( xChildEnumeration->hasMoreElements() )
            {
                Reference< XAnimationNode > xNode( xChildEnumeration->nextElement(), UNO_QUERY_THROW );
                xChildContainer->removeChild( xNode );
            }

            mxSequenceRoot->removeChild( xChildNode );
        }

        // second, rebuild main sequence
        EffectSequence::iterator aIter( maEffects.begin() );
        EffectSequence::iterator aEnd( maEffects.end() );
        if( aIter != aEnd )
        {
            std::vector< sd::AfterEffectNode > aAfterEffects;

            CustomAnimationEffectPtr pEffect = *aIter++;

            bool bFirst = true;
            do
            {
                // create a par container for the next click node and all following with and after effects
                Reference< XTimeContainer > xOnClickContainer( ParallelTimeContainer::create( ::comphelper::getProcessComponentContext() ), UNO_QUERY_THROW );

                Event aEvent;
                if( mxEventSource.is() )
                {
                    aEvent.Source <<= mxEventSource;
                    aEvent.Trigger = EventTrigger::ON_CLICK;
                }
                else
                {
                    aEvent.Trigger = EventTrigger::ON_NEXT;
                }
                aEvent.Repeat = 0;

                Any aBegin( aEvent );
                if( bFirst )
                {
                    // if the first node is not a click action, this click container
                    // must not have INDEFINITE begin but start at 0s
                    bFirst = false;
                    if( pEffect->getNodeType() != EffectNodeType::ON_CLICK )
                        aBegin <<= 0.0;
                }

                xOnClickContainer->setBegin( aBegin );

                mxSequenceRoot->appendChild( xOnClickContainer );

                double fBegin = 0.0;

                do
                {
                    // create a par container for the current click or after effect node and all following with effects
                    Reference< XTimeContainer > xWithContainer( ParallelTimeContainer::create( ::comphelper::getProcessComponentContext() ), UNO_QUERY_THROW );
                    xWithContainer->setBegin( Any( fBegin ) );
                    xOnClickContainer->appendChild( xWithContainer );

                    double fDuration = 0.0;
                    do
                    {
                        Reference< XAnimationNode > xEffectNode( pEffect->getNode() );
                        xWithContainer->appendChild( xEffectNode );

                        if( pEffect->hasAfterEffect() )
                        {
                            Reference< XAnimationNode > xAfterEffect( pEffect->createAfterEffectNode() );
                            AfterEffectNode a( xAfterEffect, xEffectNode, pEffect->IsAfterEffectOnNext() );
                            aAfterEffects.push_back( a );
                        }

                        double fTemp = pEffect->getBegin() + pEffect->getAbsoluteDuration();
                        if( fTemp > fDuration )
                            fDuration = fTemp;

                        if( aIter != aEnd )
                            pEffect = *aIter++;
                        else
                            pEffect.reset();
                    }
                    while( pEffect && (pEffect->getNodeType() == EffectNodeType::WITH_PREVIOUS) );

                    fBegin += fDuration;
                }
                while( pEffect && (pEffect->getNodeType() != EffectNodeType::ON_CLICK) );
            }
            while( pEffect );

            // process after effect nodes
            std::for_each( aAfterEffects.begin(), aAfterEffects.end(), stl_process_after_effect_node_func );

            updateTextGroups();

            // reset duration, might have been altered (see below)
            mxSequenceRoot->setDuration( Any() );
        }
        else
        {
            // empty sequence, set duration to 0.0 explicitly
            // (otherwise, this sequence will never end)
            mxSequenceRoot->setDuration( Any(0.0) );
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::EffectSequenceHelper::rebuild()" );
    }
}

stl_CustomAnimationEffect_search_node_predict::stl_CustomAnimationEffect_search_node_predict( const css::uno::Reference< css::animations::XAnimationNode >& xSearchNode )
: mxSearchNode( xSearchNode )
{
}

bool stl_CustomAnimationEffect_search_node_predict::operator()( const CustomAnimationEffectPtr& pEffect ) const
{
    return pEffect->getNode() == mxSearchNode;
}

/// @throws Exception
static bool implFindNextContainer( Reference< XTimeContainer > const & xParent, Reference< XTimeContainer > const & xCurrent, Reference< XTimeContainer >& xNext )
{
    Reference< XEnumerationAccess > xEnumerationAccess( xParent, UNO_QUERY_THROW );
    Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration() );
    if( xEnumeration.is() )
    {
        Reference< XInterface > x;
        while( xEnumeration->hasMoreElements() && !xNext.is() )
        {
            if( (xEnumeration->nextElement() >>= x) && (x == xCurrent) )
            {
                if( xEnumeration->hasMoreElements() )
                    xEnumeration->nextElement() >>= xNext;
            }
        }
    }
    return xNext.is();
}

void stl_process_after_effect_node_func(AfterEffectNode const & rNode)
{
    try
    {
        if( rNode.mxNode.is() && rNode.mxMaster.is() )
        {
            // set master node
            Reference< XAnimationNode > xMasterNode( rNode.mxMaster, UNO_SET_THROW );
            Sequence< NamedValue > aUserData( rNode.mxNode->getUserData() );
            sal_Int32 nSize = aUserData.getLength();
            aUserData.realloc(nSize+1);
            auto pUserData = aUserData.getArray();
            pUserData[nSize].Name = "master-element";
            pUserData[nSize].Value <<= xMasterNode;
            rNode.mxNode->setUserData( aUserData );

            // insert after effect node into timeline
            Reference< XTimeContainer > xContainer( rNode.mxMaster->getParent(), UNO_QUERY_THROW );

            if( !rNode.mbOnNextEffect ) // sameClick
            {
                // insert the aftereffect after its effect is animated
                xContainer->insertAfter( rNode.mxNode, rNode.mxMaster );
            }
            else // nextClick
            {
                const Reference< XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );
                // insert the aftereffect in the next group

                Reference< XTimeContainer > xClickContainer( xContainer->getParent(), UNO_QUERY_THROW );
                Reference< XTimeContainer > xSequenceContainer( xClickContainer->getParent(), UNO_QUERY_THROW );

                Reference< XTimeContainer > xNextContainer;

                // first try if we have an after effect container
                if( !implFindNextContainer( xClickContainer, xContainer, xNextContainer ) )
                {
                    Reference< XTimeContainer > xNextClickContainer;
                    // if not, try to find the next click effect container
                    if( implFindNextContainer( xSequenceContainer, xClickContainer, xNextClickContainer ) )
                    {
                        Reference< XEnumerationAccess > xEnumerationAccess( xNextClickContainer, UNO_QUERY_THROW );
                        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
                        if( xEnumeration->hasMoreElements() )
                        {
                            // the next container is the first child container
                            xEnumeration->nextElement() >>= xNextContainer;
                        }
                        else
                        {
                            // this does not yet have a child container, create one
                            xNextContainer.set( ParallelTimeContainer::create(xContext), UNO_QUERY_THROW );

                            xNextContainer->setBegin( Any( 0.0 ) );
                            xNextClickContainer->appendChild( xNextContainer );
                        }
                        DBG_ASSERT( xNextContainer.is(), "ppt::stl_process_after_effect_node_func::operator(), could not find/create container!" );
                    }
                }

                // if we don't have a next container, we add one to the sequence container
                if( !xNextContainer.is() )
                {
                    Reference< XTimeContainer > xNewClickContainer( ParallelTimeContainer::create( xContext ), UNO_QUERY_THROW );

                    Event aEvent;
                    aEvent.Trigger = EventTrigger::ON_NEXT;
                    aEvent.Repeat = 0;
                    xNewClickContainer->setBegin( Any( aEvent ) );

                    xSequenceContainer->insertAfter( xNewClickContainer, xClickContainer );

                    xNextContainer.set( ParallelTimeContainer::create( xContext ), UNO_QUERY_THROW );

                    xNextContainer->setBegin( Any( 0.0 ) );
                    xNewClickContainer->appendChild( xNextContainer );
                }

                if( xNextContainer.is() )
                {
                    // find begin time of first element
                    Reference< XEnumerationAccess > xEnumerationAccess( xNextContainer, UNO_QUERY_THROW );
                    Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
                    if( xEnumeration->hasMoreElements() )
                    {
                        Reference< XAnimationNode > xChild;
                        // the next container is the first child container
                        xEnumeration->nextElement() >>= xChild;
                        if( xChild.is() )
                        {
                            Any aBegin( xChild->getBegin() );
                            double fBegin = 0.0;
                            if( (aBegin >>= fBegin) && (fBegin >= 0.0))
                                rNode.mxNode->setBegin( aBegin );
                        }
                    }

                    xNextContainer->appendChild( rNode.mxNode );
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "ppt::stl_process_after_effect_node_func::operator()" );
    }
}

EffectSequence::iterator EffectSequenceHelper::find( const CustomAnimationEffectPtr& pEffect )
{
    return std::find( maEffects.begin(), maEffects.end(), pEffect );
}

CustomAnimationEffectPtr EffectSequenceHelper::findEffect( const css::uno::Reference< css::animations::XAnimationNode >& xNode ) const
{
    CustomAnimationEffectPtr pEffect;

    EffectSequence::const_iterator aIter = std::find_if(maEffects.begin(), maEffects.end(),
        [&xNode](const CustomAnimationEffectPtr& rxEffect) { return rxEffect->getNode() == xNode; });
    if (aIter != maEffects.end())
        pEffect = *aIter;

    return pEffect;
}

sal_Int32 EffectSequenceHelper::getOffsetFromEffect( const CustomAnimationEffectPtr& xEffect ) const
{
    auto aIter = std::find(maEffects.begin(), maEffects.end(), xEffect);
    if (aIter != maEffects.end())
        return static_cast<sal_Int32>(std::distance(maEffects.begin(), aIter));

    return -1;
}

CustomAnimationEffectPtr EffectSequenceHelper::getEffectFromOffset( sal_Int32 nOffset ) const
{
    EffectSequence::const_iterator aIter( maEffects.begin() );
    nOffset = std::min(nOffset, static_cast<sal_Int32>(maEffects.size()));
    std::advance(aIter, nOffset);

    CustomAnimationEffectPtr pEffect;
    if( aIter != maEffects.end() )
        pEffect = *aIter;

    return pEffect;
}

bool EffectSequenceHelper::disposeShape( const Reference< XShape >& xShape )
{
    bool bChanges = false;

    EffectSequence::iterator aIter( maEffects.begin() );
    while( aIter != maEffects.end() )
    {
        if( (*aIter)->getTargetShape() == xShape )
        {
            (*aIter)->setEffectSequence( nullptr );
            bChanges = true;
            aIter = maEffects.erase( aIter );
        }
        else
        {
            ++aIter;
        }
    }

    return bChanges;
}

bool EffectSequenceHelper::hasEffect( const css::uno::Reference< css::drawing::XShape >& xShape )
{
    return std::any_of(maEffects.begin(), maEffects.end(),
        [&xShape](const CustomAnimationEffectPtr& rxEffect) { return rxEffect->getTargetShape() == xShape; });
}

bool EffectSequenceHelper::getParagraphNumberingLevels( const Reference< XShape >& xShape, std::vector< sal_Int32 >& rParagraphNumberingLevel )
{
    rParagraphNumberingLevel.clear();

    if( !hasEffect( xShape ) )
        return false;

    Reference< XText > xText( xShape, UNO_QUERY );
    if( xText.is() )
    {
        Reference< XEnumerationAccess > xEA( xText, UNO_QUERY );
        if( xEA.is() )
        {
            Reference< XEnumeration > xEnumeration = xEA->createEnumeration();

            if( xEnumeration.is() )
            {
                while( xEnumeration->hasMoreElements() )
                {
                    Reference< XPropertySet > xParaSet;
                    xEnumeration->nextElement() >>= xParaSet;

                    sal_Int32 nParaDepth = 0;
                    if( xParaSet.is() )
                    {
                        xParaSet->getPropertyValue( u"NumberingLevel"_ustr ) >>= nParaDepth;
                    }

                    rParagraphNumberingLevel.push_back( nParaDepth );
                }
            }
        }
    }

    return true;
}

void EffectSequenceHelper::insertTextRange( const css::uno::Any& aTarget )
{
    ParagraphTarget aParaTarget;
    if( !(aTarget >>= aParaTarget ) )
        return;

    // get map [paragraph index] -> [NumberingLevel]
    // for following reusage inside all animation effects
    std::vector< sal_Int32 > paragraphNumberingLevel;
    std::vector< sal_Int32 >* paragraphNumberingLevelParam = nullptr;
    if ( getParagraphNumberingLevels( aParaTarget.Shape, paragraphNumberingLevel ) )
        paragraphNumberingLevelParam = &paragraphNumberingLevel;

    // update internal flags for each animation effect
    const bool bChanges = std::accumulate(maEffects.begin(), maEffects.end(), false,
        [&aParaTarget, &paragraphNumberingLevelParam](const bool bCheck, const CustomAnimationEffectPtr& rxEffect) {
            bool bRes = bCheck;
            if (rxEffect->getTargetShape() == aParaTarget.Shape)
                bRes |= rxEffect->checkForText( paragraphNumberingLevelParam );
            return bRes;
        });

    if( bChanges )
        rebuild();
}

static bool isParagraphTargetTextEmpty( ParagraphTarget aParaTarget )
{
    // get paragraph
    Reference< XText > xText ( aParaTarget.Shape, UNO_QUERY );
    if( xText.is() )
    {
        Reference< XEnumerationAccess > xEA( xText, UNO_QUERY );
        if( xEA.is() )
        {
            Reference< XEnumeration > xEnumeration = xEA->createEnumeration();
            if( xEnumeration.is() )
            {
                // advance to the Nth paragraph
                sal_Int32 nPara = aParaTarget.Paragraph;
                while( xEnumeration->hasMoreElements() && nPara-- )
                    xEnumeration->nextElement();

                // get Nth paragraph's text and check if it's empty
                if( xEnumeration->hasMoreElements() )
                {
                    Reference< XTextRange > xRange( xEnumeration->nextElement(), UNO_QUERY );
                    if( xRange.is() )
                    {
                        OUString text = xRange->getString();
                        return text.isEmpty();
                    }
                }
            }
        }
    }
    return false;
}

void EffectSequenceHelper::disposeTextRange( const css::uno::Any& aTarget )
{
    ParagraphTarget aParaTarget;
    if( !(aTarget >>= aParaTarget ) )
        return;

    bool bChanges = false;

    // building list of effects for target shape; process effects not on target shape
    EffectSequence aTargetParagraphEffects;
    for( const auto &pEffect : maEffects )
    {
        Any aIterTarget( pEffect->getTarget() );
        if( aIterTarget.getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
        {
            ParagraphTarget aIterParaTarget;
            if( (aIterTarget >>= aIterParaTarget) && (aIterParaTarget.Shape == aParaTarget.Shape) )
            {
                aTargetParagraphEffects.push_back(pEffect);
            }
        }
        else if( pEffect->getTargetShape() == aParaTarget.Shape )
        {
            bChanges |= pEffect->checkForText();
        }
    }

    // select effect to delete:
    // if paragraph before target is blank, then delete its animation effect (if any) instead
    ParagraphTarget aPreviousParagraph = aParaTarget;
    --aPreviousParagraph.Paragraph;
    bool bIsPreviousParagraphEmpty = isParagraphTargetTextEmpty( aPreviousParagraph );
    sal_Int16 anParaNumToDelete = bIsPreviousParagraphEmpty ? aPreviousParagraph.Paragraph : aParaTarget.Paragraph;

    // update effects
    for( const auto &pEffect : aTargetParagraphEffects )
    {
        Any aIterTarget( pEffect->getTarget() );

        ParagraphTarget aIterParaTarget;
        aIterTarget >>= aIterParaTarget;

        // delete effect for target paragraph (may have effects in more than one text group)
        if( aIterParaTarget.Paragraph == anParaNumToDelete )
        {
            auto aItr = find( pEffect );
            DBG_ASSERT( aItr != maEffects.end(), "sd::EffectSequenceHelper::disposeTextRange(), Expected effect missing.");
            if( aItr != maEffects.end() )
            {
                (*aItr)->setEffectSequence( nullptr );
                maEffects.erase(aItr);
                bChanges = true;
            }
        }

        // shift all paragraphs after disposed paragraph
        if( aIterParaTarget.Paragraph > anParaNumToDelete )
        {
            --aIterParaTarget.Paragraph;
            pEffect->setTarget( Any( aIterParaTarget ) );
            bChanges = true;
        }
    }

    if( bChanges )
    {
        rebuild();
    }
}

CustomAnimationTextGroup::CustomAnimationTextGroup( const Reference< XShape >& rTarget, sal_Int32 nGroupId )
:   maTarget( rTarget ),
    mnGroupId( nGroupId )
{
    reset();
}

void CustomAnimationTextGroup::reset()
{
    mnTextGrouping = -1;
    mbAnimateForm = false;
    mbTextReverse = false;
    mfGroupingAuto = -1.0;
    mnLastPara = -1; // used to check for TextReverse

    for (sal_Int8 & rn : mnDepthFlags)
    {
        rn = 0;
    }

    maEffects.clear();
}

void CustomAnimationTextGroup::addEffect( CustomAnimationEffectPtr const & pEffect )
{
    maEffects.push_back( pEffect );

    Any aTarget( pEffect->getTarget() );
    if( aTarget.getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
    {
        // now look at the paragraph
        ParagraphTarget aParaTarget;
        aTarget >>= aParaTarget;

        if( mnLastPara != -1 )
            mbTextReverse = mnLastPara > aParaTarget.Paragraph;

        mnLastPara = aParaTarget.Paragraph;

        const sal_Int32 nParaDepth = pEffect->getParaDepth();

        // only look at the first PARA_LEVELS levels
        if( nParaDepth < PARA_LEVELS )
        {
            // our first paragraph with this level?
            if( mnDepthFlags[nParaDepth] == 0 )
            {
                // so set it to the first found
                mnDepthFlags[nParaDepth] = static_cast<sal_Int8>(pEffect->getNodeType());
            }
            else if( mnDepthFlags[nParaDepth] != pEffect->getNodeType() )
            {
                mnDepthFlags[nParaDepth] = -1;
            }

            if( pEffect->getNodeType() == EffectNodeType::AFTER_PREVIOUS )
                mfGroupingAuto = pEffect->getBegin();

            mnTextGrouping = PARA_LEVELS;
            while( (mnTextGrouping > 0)
                   && (mnDepthFlags[mnTextGrouping - 1] <= 0) )
                --mnTextGrouping;
        }
    }
    else
    {
        // if we have an effect with the shape as a target, we animate the background
        mbAnimateForm = pEffect->getTargetSubItem() != ShapeAnimationSubType::ONLY_TEXT;
    }
}

CustomAnimationTextGroupPtr EffectSequenceHelper::findGroup( sal_Int32 nGroupId )
{
    CustomAnimationTextGroupPtr aPtr;

    CustomAnimationTextGroupMap::iterator aIter( maGroupMap.find( nGroupId ) );
    if( aIter != maGroupMap.end() )
        aPtr = (*aIter).second;

    return aPtr;
}

void EffectSequenceHelper::updateTextGroups()
{
    maGroupMap.clear();

    // first create all the groups
    for( const CustomAnimationEffectPtr& pEffect : maEffects )
    {
        const sal_Int32 nGroupId = pEffect->getGroupId();

        if( nGroupId == -1 )
            continue; // trivial case, no group

        CustomAnimationTextGroupPtr pGroup = findGroup( nGroupId );
        if( !pGroup )
        {
            pGroup = std::make_shared<CustomAnimationTextGroup>( pEffect->getTargetShape(), nGroupId );
            maGroupMap[nGroupId] = pGroup;
        }

        pGroup->addEffect( pEffect );
    }

    // Now that all the text groups have been cleared up and rebuilt, we need to update its
    // text grouping. addEffect() already make mnTextGrouping the last possible level,
    // so just continue to find the last level that is not EffectNodeType::WITH_PREVIOUS.
    for(const auto &rGroupMapItem: maGroupMap)
    {
        const CustomAnimationTextGroupPtr &pGroup = rGroupMapItem.second;
        while(pGroup->mnTextGrouping > 0 && pGroup->mnDepthFlags[pGroup->mnTextGrouping - 1] == EffectNodeType::WITH_PREVIOUS)
            --pGroup->mnTextGrouping;
    }
}

CustomAnimationTextGroupPtr
EffectSequenceHelper::createTextGroup(const CustomAnimationEffectPtr& pEffect,
                                      sal_Int32 nTextGrouping, double fTextGroupingAuto,
                                      bool bAnimateForm, bool bTextReverse)
{
    // first find a free group-id
    sal_Int32 nGroupId = 0;

    CustomAnimationTextGroupMap::iterator aIter( maGroupMap.begin() );
    const CustomAnimationTextGroupMap::iterator aEnd( maGroupMap.end() );
    while( aIter != aEnd )
    {
        if( (*aIter).first == nGroupId )
        {
            nGroupId++;
            aIter = maGroupMap.begin();
        }
        else
        {
            ++aIter;
        }
    }

    Reference< XShape > xTarget( pEffect->getTargetShape() );

    CustomAnimationTextGroupPtr pTextGroup = std::make_shared<CustomAnimationTextGroup>( xTarget, nGroupId );
    maGroupMap[nGroupId] = pTextGroup;

    bool bUsed = false;

    // do we need to target the shape?
    if( (nTextGrouping == 0) || bAnimateForm )
    {
        sal_Int16 nSubItem;
        if( nTextGrouping == 0)
            nSubItem = bAnimateForm ? ShapeAnimationSubType::AS_WHOLE : ShapeAnimationSubType::ONLY_TEXT;
        else
            nSubItem = ShapeAnimationSubType::ONLY_BACKGROUND;

        pEffect->setTarget( Any( xTarget ) );
        pEffect->setTargetSubItem( nSubItem );
        pEffect->setEffectSequence( this );
        pEffect->setGroupId( nGroupId );

        pTextGroup->addEffect( pEffect );
        bUsed = true;
    }

    pTextGroup->mnTextGrouping = nTextGrouping;
    pTextGroup->mfGroupingAuto = fTextGroupingAuto;
    pTextGroup->mbTextReverse = bTextReverse;

    // now add an effect for each paragraph
    createTextGroupParagraphEffects( pTextGroup, pEffect, bUsed );

    notify_listeners();

    return pTextGroup;
}

void EffectSequenceHelper::createTextGroupParagraphEffects( const CustomAnimationTextGroupPtr& pTextGroup, const CustomAnimationEffectPtr& pEffect, bool bUsed )
{
    Reference< XShape > xTarget( pTextGroup->maTarget );

    sal_Int32 nTextGrouping = pTextGroup->mnTextGrouping;
    double fTextGroupingAuto = pTextGroup->mfGroupingAuto;
    bool bTextReverse = pTextGroup->mbTextReverse;

    // now add an effect for each paragraph
    if( nTextGrouping < 0 )
        return;

    try
    {
        EffectSequence::iterator aInsertIter( find( pEffect ) );

        Reference< XEnumerationAccess > xText( xTarget, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xText->createEnumeration(), UNO_SET_THROW );

        std::deque< sal_Int16 > aParaList;
        sal_Int16 nPara;

        // fill the list with all valid paragraphs
        for( nPara = 0; xEnumeration->hasMoreElements(); nPara++ )
        {
            Reference< XTextRange > xRange( xEnumeration->nextElement(), UNO_QUERY );
            if( xRange.is() && !xRange->getString().isEmpty() )
            {
                if( bTextReverse ) // sort them
                    aParaList.push_front( nPara );
                else
                    aParaList.push_back( nPara );
            }
        }

        ParagraphTarget aTarget;
        aTarget.Shape = std::move(xTarget);

        for( const auto i : aParaList )
        {
            aTarget.Paragraph = i;

            CustomAnimationEffectPtr pNewEffect;
            if( bUsed )
            {
                // clone a new effect from first effect
                pNewEffect = pEffect->clone();
                ++aInsertIter;
                aInsertIter = maEffects.insert( aInsertIter, pNewEffect );
            }
            else
            {
                // reuse first effect if it's not yet used
                pNewEffect = pEffect;
                bUsed = true;
                aInsertIter = find( pNewEffect );
            }

            // set target and group-id
            pNewEffect->setTarget( Any( aTarget ) );
            pNewEffect->setTargetSubItem( ShapeAnimationSubType::ONLY_TEXT );
            pNewEffect->setGroupId( pTextGroup->mnGroupId );
            pNewEffect->setEffectSequence( this );

            // set correct node type
            if( pNewEffect->getParaDepth() < nTextGrouping )
            {
                if( fTextGroupingAuto == -1.0 )
                {
                    pNewEffect->setNodeType( EffectNodeType::ON_CLICK );
                    pNewEffect->setBegin( 0.0 );
                }
                else
                {
                    pNewEffect->setNodeType( EffectNodeType::AFTER_PREVIOUS );
                    pNewEffect->setBegin( fTextGroupingAuto );
                }
            }
            else
            {
                pNewEffect->setNodeType( EffectNodeType::WITH_PREVIOUS );
                pNewEffect->setBegin( 0.0 );
            }

            pTextGroup->addEffect( pNewEffect );
        }
        notify_listeners();
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::EffectSequenceHelper::createTextGroup()" );
    }
}

void EffectSequenceHelper::setTextGrouping( const CustomAnimationTextGroupPtr& pTextGroup, sal_Int32 nTextGrouping )
{
    if( pTextGroup->mnTextGrouping == nTextGrouping )
    {
        // first case, trivial case, do nothing
    }
    else if( (pTextGroup->mnTextGrouping == -1) && (nTextGrouping >= 0) )
    {
        // second case, we need to add new effects for each paragraph

        CustomAnimationEffectPtr pEffect( pTextGroup->maEffects.front() );

        pTextGroup->mnTextGrouping = nTextGrouping;
        createTextGroupParagraphEffects( pTextGroup, pEffect, true );
        notify_listeners();
    }
    else if( (pTextGroup->mnTextGrouping >= 0) && (nTextGrouping == -1 ) )
    {
        // third case, we need to remove effects for each paragraph

        EffectSequence aEffects( pTextGroup->maEffects );
        pTextGroup->reset();

        for( const CustomAnimationEffectPtr& pEffect : aEffects )
        {
            if( pEffect->getTarget().getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
                remove( pEffect );
            else
                pTextGroup->addEffect( pEffect );
        }
        notify_listeners();
    }
    else
    {
        // fourth case, we need to change the node types for the text nodes
        double fTextGroupingAuto = pTextGroup->mfGroupingAuto;

        EffectSequence aEffects( pTextGroup->maEffects );
        pTextGroup->reset();

        for( CustomAnimationEffectPtr& pEffect : aEffects )
        {
            if( pEffect->getTarget().getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
            {
                // set correct node type
                if( pEffect->getParaDepth() < nTextGrouping )
                {
                    if( fTextGroupingAuto == -1.0 )
                    {
                        pEffect->setNodeType( EffectNodeType::ON_CLICK );
                        pEffect->setBegin( 0.0 );
                    }
                    else
                    {
                        pEffect->setNodeType( EffectNodeType::AFTER_PREVIOUS );
                        pEffect->setBegin( fTextGroupingAuto );
                    }
                }
                else
                {
                    pEffect->setNodeType( EffectNodeType::WITH_PREVIOUS );
                    pEffect->setBegin( 0.0 );
                }
            }

            pTextGroup->addEffect( pEffect );

        }
        notify_listeners();
    }
}

void EffectSequenceHelper::setAnimateForm( const CustomAnimationTextGroupPtr& pTextGroup, bool bAnimateForm )
{
    if( pTextGroup->mbAnimateForm == bAnimateForm )
    {
        // trivial case, do nothing
    }
    else
    {
        EffectSequence aEffects( pTextGroup->maEffects );
        pTextGroup->reset();

        SAL_WARN_IF(aEffects.empty(), "sd", "EffectSequenceHelper::setAnimateForm effects empty" );

        if (aEffects.empty())
            return;

        EffectSequence::iterator aIter( aEffects.begin() );
        const EffectSequence::iterator aEnd( aEffects.end() );

        // first insert if we have to
        if( bAnimateForm )
        {
            EffectSequence::iterator aInsertIter( find( *aIter ) );

            CustomAnimationEffectPtr pEffect;
            if( (aEffects.size() == 1) && ((*aIter)->getTarget().getValueType() != ::cppu::UnoType<ParagraphTarget>::get() ) )
            {
                // special case, only one effect and that targets whole text,
                // convert this to target whole shape
                pEffect = *aIter++;
                pEffect->setTargetSubItem( ShapeAnimationSubType::AS_WHOLE );
            }
            else
            {
                pEffect = (*aIter)->clone();
                pEffect->setTarget( Any( (*aIter)->getTargetShape() ) );
                pEffect->setTargetSubItem( ShapeAnimationSubType::ONLY_BACKGROUND );
                maEffects.insert( aInsertIter, pEffect );
            }

            pTextGroup->addEffect( pEffect );
        }

        if( !bAnimateForm && (aEffects.size() == 1) )
        {
            const CustomAnimationEffectPtr& pEffect( *aIter );
            pEffect->setTarget( Any( (*aIter)->getTargetShape() ) );
            pEffect->setTargetSubItem( ShapeAnimationSubType::ONLY_TEXT );
            pTextGroup->addEffect( pEffect );
        }
        else
        {
            // read the rest to the group again
            while( aIter != aEnd )
            {
                CustomAnimationEffectPtr pEffect( *aIter++ );

                if( pEffect->getTarget().getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
                {
                    pTextGroup->addEffect( pEffect );
                }
                else
                {
                    DBG_ASSERT( !bAnimateForm, "sd::EffectSequenceHelper::setAnimateForm(), something is wrong here!" );
                    remove( pEffect );
                }
            }
        }
        notify_listeners();
    }
}

void EffectSequenceHelper::setTextGroupingAuto( const CustomAnimationTextGroupPtr& pTextGroup, double fTextGroupingAuto )
{
    sal_Int32 nTextGrouping = pTextGroup->mnTextGrouping;

    EffectSequence aEffects( pTextGroup->maEffects );
    pTextGroup->reset();

    for( CustomAnimationEffectPtr& pEffect : aEffects )
    {
        if( pEffect->getTarget().getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
        {
            // set correct node type
            if( pEffect->getParaDepth() < nTextGrouping )
            {
                if( fTextGroupingAuto == -1.0 )
                {
                    pEffect->setNodeType( EffectNodeType::ON_CLICK );
                    pEffect->setBegin( 0.0 );
                }
                else
                {
                    pEffect->setNodeType( EffectNodeType::AFTER_PREVIOUS );
                    pEffect->setBegin( fTextGroupingAuto );
                }
            }
            else
            {
                pEffect->setNodeType( EffectNodeType::WITH_PREVIOUS );
                pEffect->setBegin( 0.0 );
            }
        }

        pTextGroup->addEffect( pEffect );

    }
    notify_listeners();
}

namespace {

struct ImplStlTextGroupSortHelper
{
    explicit ImplStlTextGroupSortHelper( bool bReverse ) : mbReverse( bReverse ) {};
    bool operator()( const CustomAnimationEffectPtr& p1, const CustomAnimationEffectPtr& p2 );
    bool mbReverse;
    sal_Int32 getTargetParagraph( const CustomAnimationEffectPtr& p1 );
};

}

sal_Int32 ImplStlTextGroupSortHelper::getTargetParagraph( const CustomAnimationEffectPtr& p1 )
{
    const Any aTarget(p1->getTarget());
    if( aTarget.hasValue() && aTarget.getValueType() == ::cppu::UnoType<ParagraphTarget>::get() )
    {
        ParagraphTarget aParaTarget;
        aTarget >>= aParaTarget;
        return aParaTarget.Paragraph;
    }
    else
    {
        return mbReverse ? 0x7fffffff : -1;
    }
}

bool ImplStlTextGroupSortHelper::operator()( const CustomAnimationEffectPtr& p1, const CustomAnimationEffectPtr& p2 )
{
    if( mbReverse )
    {
        return getTargetParagraph( p2 ) < getTargetParagraph( p1 );
    }
    else
    {
        return getTargetParagraph( p1 ) < getTargetParagraph( p2 );
    }
}

void EffectSequenceHelper::setTextReverse( const CustomAnimationTextGroupPtr& pTextGroup, bool bTextReverse )
{
    if( pTextGroup->mbTextReverse == bTextReverse )
    {
        // do nothing
    }
    else
    {
        std::vector< CustomAnimationEffectPtr > aSortedVector( pTextGroup->maEffects.begin(), pTextGroup->maEffects.end() );
        ImplStlTextGroupSortHelper aSortHelper( bTextReverse );
        std::sort( aSortedVector.begin(), aSortedVector.end(), aSortHelper );

        pTextGroup->reset();

        std::vector< CustomAnimationEffectPtr >::iterator aIter( aSortedVector.begin() );
        const std::vector< CustomAnimationEffectPtr >::iterator aEnd( aSortedVector.end() );

        if( aIter != aEnd )
        {
            pTextGroup->addEffect( *aIter );
            EffectSequence::iterator aInsertIter( find( *aIter++ ) );
            while( aIter != aEnd )
            {
                CustomAnimationEffectPtr pEffect( *aIter++ );
                maEffects.erase( find( pEffect ) );
                aInsertIter = maEffects.insert( ++aInsertIter, pEffect );
                pTextGroup->addEffect( pEffect );
            }
        }
        notify_listeners();
    }
}

void EffectSequenceHelper::addListener( ISequenceListener* pListener )
{
    if( std::find( maListeners.begin(), maListeners.end(), pListener ) == maListeners.end() )
        maListeners.push_back( pListener );
}

void EffectSequenceHelper::removeListener( ISequenceListener* pListener )
{
    maListeners.remove( pListener );
}

namespace {

struct stl_notify_listeners_func
{
    stl_notify_listeners_func() {}
    void operator()(ISequenceListener* pListener) { pListener->notify_change(); }
};

}

void EffectSequenceHelper::notify_listeners()
{
    stl_notify_listeners_func aFunc;
    std::for_each( maListeners.begin(), maListeners.end(), aFunc );
}

void EffectSequenceHelper::create( const css::uno::Reference< css::animations::XAnimationNode >& xNode )
{
    DBG_ASSERT( xNode.is(), "sd::EffectSequenceHelper::create(), illegal argument" );

    if( !xNode.is() )
        return;

    try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( xNode, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        while( xEnumeration->hasMoreElements() )
        {
            Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY_THROW );
            createEffectsequence( xChildNode );
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::EffectSequenceHelper::create()" );
    }
}

void EffectSequenceHelper::createEffectsequence( const Reference< XAnimationNode >& xNode )
{
    DBG_ASSERT( xNode.is(), "sd::EffectSequenceHelper::createEffectsequence(), illegal argument" );

    if( !xNode.is() )
        return;

    try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( xNode, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        while( xEnumeration->hasMoreElements() )
        {
            Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY_THROW );

            createEffects( xChildNode );
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::EffectSequenceHelper::createEffectsequence()" );
    }
}

void EffectSequenceHelper::createEffects( const Reference< XAnimationNode >& xNode )
{
    DBG_ASSERT( xNode.is(), "sd::EffectSequenceHelper::createEffects(), illegal argument" );

    if( !xNode.is() )
        return;

    try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( xNode, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        while( xEnumeration->hasMoreElements() )
        {
            Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY_THROW );

            switch( xChildNode->getType() )
            {
            // found an effect
            case AnimationNodeType::PAR:
            case AnimationNodeType::ITERATE:
                {
                    CustomAnimationEffectPtr pEffect = std::make_shared<CustomAnimationEffect>( xChildNode );

                    if( pEffect->mnNodeType != -1 )
                    {
                        pEffect->setEffectSequence( this );
                        maEffects.push_back(std::move(pEffect));
                    }
                }
                break;

            // found an after effect
            case AnimationNodeType::SET:
            case AnimationNodeType::ANIMATECOLOR:
                {
                    processAfterEffect( xChildNode );
                }
                break;
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::EffectSequenceHelper::createEffects()" );
    }
}

void EffectSequenceHelper::processAfterEffect( const Reference< XAnimationNode >& xNode )
{
    try
    {
        Reference< XAnimationNode > xMaster;

        const Sequence< NamedValue > aUserData( xNode->getUserData() );
        const NamedValue* pProp = std::find_if(aUserData.begin(), aUserData.end(),
            [](const NamedValue& rProp) { return rProp.Name == "master-element"; });

        if (pProp != aUserData.end())
            pProp->Value >>= xMaster;

        // only process if this is a valid after effect
        if( xMaster.is() )
        {
            CustomAnimationEffectPtr pMasterEffect;

            // find the master effect
            stl_CustomAnimationEffect_search_node_predict aSearchPredict( xMaster );
            EffectSequence::iterator aIter( std::find_if( maEffects.begin(), maEffects.end(), aSearchPredict ) );
            if( aIter != maEffects.end() )
                pMasterEffect = *aIter;

            if( pMasterEffect )
            {
                pMasterEffect->setHasAfterEffect( true );

                // find out what kind of after effect this is
                if( xNode->getType() == AnimationNodeType::ANIMATECOLOR )
                {
                    // it's a dim
                    Reference< XAnimate > xAnimate( xNode, UNO_QUERY_THROW );
                    pMasterEffect->setDimColor( xAnimate->getTo() );
                    pMasterEffect->setAfterEffectOnNext( true );
                }
                else
                {
                    // it's a hide
                    pMasterEffect->setAfterEffectOnNext( xNode->getParent() != xMaster->getParent() );
                }
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::EffectSequenceHelper::processAfterEffect()" );
    }
}

namespace {

class AnimationChangeListener : public cppu::WeakImplHelper< XChangesListener >
{
public:
    explicit AnimationChangeListener( MainSequence* pMainSequence ) : mpMainSequence( pMainSequence ) {}

    virtual void SAL_CALL changesOccurred( const css::util::ChangesEvent& Event ) override;
    virtual void SAL_CALL disposing( const css::lang::EventObject& Source ) override;
private:
    MainSequence* mpMainSequence;
};

}

void SAL_CALL AnimationChangeListener::changesOccurred( const css::util::ChangesEvent& )
{
    if( mpMainSequence )
        mpMainSequence->startRecreateTimer();
}

void SAL_CALL AnimationChangeListener::disposing( const css::lang::EventObject& )
{
}

MainSequence::MainSequence()
    : mxTimingRootNode(SequenceTimeContainer::create(::comphelper::getProcessComponentContext()))
    , maTimer("sd MainSequence maTimer")
    , mbTimerMode(false)
    , mbRebuilding( false )
    , mnRebuildLockGuard( 0 )
    , mbPendingRebuildRequest( false )
    , mbIgnoreChanges( 0 )
{
    if( mxTimingRootNode.is() )
    {
        Sequence< css::beans::NamedValue > aUserData
            { { u"node-type"_ustr, css::uno::Any(css::presentation::EffectNodeType::MAIN_SEQUENCE) } };
        mxTimingRootNode->setUserData( aUserData );
    }
    init();
}

MainSequence::MainSequence( const css::uno::Reference< css::animations::XAnimationNode >& xNode )
    : mxTimingRootNode( xNode, UNO_QUERY )
    , maTimer("sd MainSequence maTimer")
    , mbTimerMode( false )
    , mbRebuilding( false )
    , mnRebuildLockGuard( 0 )
    , mbPendingRebuildRequest( false )
    , mbIgnoreChanges( 0 )
{
    init();
}

MainSequence::~MainSequence()
{
    reset();
}

void MainSequence::init()
{
    mnSequenceType = EffectNodeType::MAIN_SEQUENCE;

    maTimer.SetInvokeHandler( LINK(this, MainSequence, onTimerHdl) );
    maTimer.SetTimeout(50);

    mxChangesListener.set( new AnimationChangeListener( this ) );

    createMainSequence();
}

void MainSequence::reset( const css::uno::Reference< css::animations::XAnimationNode >& xTimingRootNode )
{
    reset();

    mxTimingRootNode.set( xTimingRootNode, UNO_QUERY );

    createMainSequence();
}

Reference< css::animations::XAnimationNode > MainSequence::getRootNode()
{
    DBG_ASSERT( mnRebuildLockGuard == 0, "MainSequence::getRootNode(), rebuild is locked, is this really what you want?" );

    if( maTimer.IsActive() && mbTimerMode )
    {
        // force a rebuild NOW if one is pending
        maTimer.Stop();
        implRebuild();
    }

    return EffectSequenceHelper::getRootNode();
}

void MainSequence::createMainSequence()
{
    if( mxTimingRootNode.is() ) try
    {
        Reference< XEnumerationAccess > xEnumerationAccess( mxTimingRootNode, UNO_QUERY_THROW );
        Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
        while( xEnumeration->hasMoreElements() )
        {
            Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY_THROW );
            sal_Int32 nNodeType = CustomAnimationEffect::get_node_type( xChildNode );
            if( nNodeType == EffectNodeType::MAIN_SEQUENCE )
            {
                mxSequenceRoot.set( xChildNode, UNO_QUERY );
                EffectSequenceHelper::create( xChildNode );
            }
            else if( nNodeType == EffectNodeType::INTERACTIVE_SEQUENCE )
            {
                Reference< XTimeContainer > xInteractiveRoot( xChildNode, UNO_QUERY_THROW );
                InteractiveSequencePtr pIS = std::make_shared<InteractiveSequence>( xInteractiveRoot, this );
                pIS->addListener( this );
                maInteractiveSequenceVector.push_back( pIS );
            }
        }

        // see if we have a mainsequence at all. if not, create one...
        if( !mxSequenceRoot.is() )
        {
            mxSequenceRoot = SequenceTimeContainer::create( ::comphelper::getProcessComponentContext() );

            uno::Sequence< css::beans::NamedValue > aUserData
                { { u"node-type"_ustr, css::uno::Any(css::presentation::EffectNodeType::MAIN_SEQUENCE) } };
            mxSequenceRoot->setUserData( aUserData );

            // empty sequence until now, set duration to 0.0
            // explicitly (otherwise, this sequence will never
            // end)
            mxSequenceRoot->setDuration( Any(0.0) );

            Reference< XAnimationNode > xMainSequenceNode( mxSequenceRoot, UNO_QUERY_THROW );
            mxTimingRootNode->appendChild( xMainSequenceNode );
        }

        updateTextGroups();

        notify_listeners();

        Reference< XChangesNotifier > xNotifier( mxTimingRootNode, UNO_QUERY );
        if( xNotifier.is() )
            xNotifier->addChangesListener( mxChangesListener );
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::MainSequence::create()" );
        return;
    }

    DBG_ASSERT( mxSequenceRoot.is(), "sd::MainSequence::create(), found no main sequence!" );
}

void MainSequence::reset()
{
    EffectSequenceHelper::reset();

    for (auto const& interactiveSequence : maInteractiveSequenceVector)
        interactiveSequence->reset();
    maInteractiveSequenceVector.clear();

    try
    {
        Reference< XChangesNotifier > xNotifier( mxTimingRootNode, UNO_QUERY );
        if( xNotifier.is() )
            xNotifier->removeChangesListener( mxChangesListener );
    }
    catch( Exception& )
    {

    }
}

InteractiveSequencePtr MainSequence::createInteractiveSequence( const css::uno::Reference< css::drawing::XShape >& xShape )
{
    InteractiveSequencePtr pIS;

    // create a new interactive sequence container
    Reference< XTimeContainer > xISRoot = SequenceTimeContainer::create( ::comphelper::getProcessComponentContext() );

    uno::Sequence< css::beans::NamedValue > aUserData
        { { u"node-type"_ustr, css::uno::Any(css::presentation::EffectNodeType::INTERACTIVE_SEQUENCE) } };
    xISRoot->setUserData( aUserData );
    xISRoot->setRestart( css::animations::AnimationRestart::WHEN_NOT_ACTIVE );

    Reference< XChild > xChild( mxSequenceRoot, UNO_QUERY_THROW );
    Reference< XTimeContainer > xParent( xChild->getParent(), UNO_QUERY_THROW );
    xParent->appendChild( xISRoot );

    pIS = std::make_shared<InteractiveSequence>( xISRoot, this);
    pIS->setTriggerShape( xShape );
    pIS->addListener( this );
    maInteractiveSequenceVector.push_back( pIS );
    return pIS;
}

CustomAnimationEffectPtr MainSequence::findEffect( const css::uno::Reference< css::animations::XAnimationNode >& xNode ) const
{
    CustomAnimationEffectPtr pEffect = EffectSequenceHelper::findEffect( xNode );

    if( !pEffect )
    {
        for (auto const& interactiveSequence : maInteractiveSequenceVector)
        {
            pEffect = interactiveSequence->findEffect( xNode );
            if (pEffect)
                break;
        }
    }
    return pEffect;
}

sal_Int32 MainSequence::getOffsetFromEffect( const CustomAnimationEffectPtr& pEffect ) const
{
    sal_Int32 nOffset = EffectSequenceHelper::getOffsetFromEffect( pEffect );

    if( nOffset != -1 )
        return nOffset;

    nOffset = EffectSequenceHelper::getCount();

    for (auto const& interactiveSequence : maInteractiveSequenceVector)
    {
        sal_Int32 nTemp = interactiveSequence->getOffsetFromEffect( pEffect );
        if( nTemp != -1 )
            return nOffset + nTemp;

        nOffset += interactiveSequence->getCount();
    }

    return -1;
}

CustomAnimationEffectPtr MainSequence::getEffectFromOffset( sal_Int32 nOffset ) const
{
    if( nOffset >= 0 )
    {
        if( nOffset < getCount() )
            return EffectSequenceHelper::getEffectFromOffset( nOffset );

        nOffset -= getCount();

        auto aIter( maInteractiveSequenceVector.begin() );

        while( (aIter != maInteractiveSequenceVector.end()) && (nOffset > (*aIter)->getCount()) )
            nOffset -= (*aIter++)->getCount();

        if( (aIter != maInteractiveSequenceVector.end()) && (nOffset >= 0) )
            return (*aIter)->getEffectFromOffset( nOffset );
    }

    return CustomAnimationEffectPtr();
}

bool MainSequence::disposeShape( const Reference< XShape >& xShape )
{
    bool bChanges = EffectSequenceHelper::disposeShape( xShape );

    for (auto const& iterativeSequence : maInteractiveSequenceVector)
    {
            bChanges |= iterativeSequence->disposeShape( xShape );
    }

    if( bChanges )
        startRebuildTimer();

    return bChanges;
}

bool MainSequence::hasEffect( const css::uno::Reference< css::drawing::XShape >& xShape )
{
    if( EffectSequenceHelper::hasEffect( xShape ) )
        return true;

    for (auto const& iterativeSequence : maInteractiveSequenceVector)
    {
        if( iterativeSequence->getTriggerShape() == xShape )
            return true;

        if( iterativeSequence->hasEffect( xShape ) )
            return true;
    }

    return false;
}

void MainSequence::insertTextRange( const css::uno::Any& aTarget )
{
    EffectSequenceHelper::insertTextRange( aTarget );

    for (auto const& iterativeSequence : maInteractiveSequenceVector)
    {
        iterativeSequence->insertTextRange( aTarget );
    }
}

void MainSequence::disposeTextRange( const css::uno::Any& aTarget )
{
    EffectSequenceHelper::disposeTextRange( aTarget );

    for (auto const& iterativeSequence : maInteractiveSequenceVector)
    {
        iterativeSequence->disposeTextRange( aTarget );
    }
}

/** callback from the sd::View when an object just left text edit mode */
void MainSequence::onTextChanged( const Reference< XShape >& xShape )
{
    EffectSequenceHelper::onTextChanged( xShape );

    for (auto const& iterativeSequence : maInteractiveSequenceVector)
    {
        iterativeSequence->onTextChanged( xShape );
    }
}

void EffectSequenceHelper::onTextChanged( const Reference< XShape >& xShape )
{
    // get map [paragraph index] -> [NumberingLevel]
    // for following reusage inside all animation effects
    std::vector< sal_Int32 > paragraphNumberingLevel;
    std::vector< sal_Int32 >* paragraphNumberingLevelParam = nullptr;
    if ( getParagraphNumberingLevels( xShape, paragraphNumberingLevel ) )
        paragraphNumberingLevelParam = &paragraphNumberingLevel;

    // update internal flags for each animation effect
    const bool bChanges = std::accumulate(maEffects.begin(), maEffects.end(), false,
        [&xShape, &paragraphNumberingLevelParam](const bool bCheck, const CustomAnimationEffectPtr& rxEffect) {
            bool bRes = bCheck;
            if (rxEffect->getTargetShape() == xShape)
                bRes |= rxEffect->checkForText( paragraphNumberingLevelParam );
            return bRes;
        });

    if( bChanges )
        rebuild();
}

void MainSequence::rebuild()
{
    startRebuildTimer();
}

void MainSequence::lockRebuilds()
{
    mnRebuildLockGuard++;
}

void MainSequence::unlockRebuilds()
{
    DBG_ASSERT( mnRebuildLockGuard, "sd::MainSequence::unlockRebuilds(), no corresponding lockRebuilds() call!" );
    if( mnRebuildLockGuard )
        mnRebuildLockGuard--;

    if( (mnRebuildLockGuard == 0) && mbPendingRebuildRequest )
    {
        mbPendingRebuildRequest = false;
        startRebuildTimer();
    }
}

void MainSequence::implRebuild()
{
    if( mnRebuildLockGuard )
    {
        mbPendingRebuildRequest = true;
        return;
    }

    mbRebuilding = true;

    EffectSequenceHelper::implRebuild();

    auto aIter( maInteractiveSequenceVector.begin() );
    while( aIter != maInteractiveSequenceVector.end() )
    {
        InteractiveSequencePtr pIS( *aIter );
        if( pIS->maEffects.empty() )
        {
            // remove empty interactive sequences
            aIter = maInteractiveSequenceVector.erase( aIter );

            Reference< XChild > xChild( mxSequenceRoot, UNO_QUERY_THROW );
            Reference< XTimeContainer > xParent( xChild->getParent(), UNO_QUERY_THROW );
            Reference< XAnimationNode > xISNode( pIS->mxSequenceRoot, UNO_QUERY_THROW );
            xParent->removeChild( xISNode );
        }
        else
        {
            pIS->implRebuild();
            ++aIter;
        }
    }

    notify_listeners();
    mbRebuilding = false;
}

void MainSequence::notify_change()
{
    notify_listeners();
}

bool MainSequence::setTrigger( const CustomAnimationEffectPtr& pEffect, const css::uno::Reference< css::drawing::XShape >& xTriggerShape )
{
    EffectSequenceHelper* pOldSequence = pEffect->getEffectSequence();

    EffectSequenceHelper* pNewSequence = nullptr;
    if( xTriggerShape.is() )
    {
        for (InteractiveSequencePtr const& pIS : maInteractiveSequenceVector)
        {
            if( pIS->getTriggerShape() == xTriggerShape )
            {
                pNewSequence = pIS.get();
                break;
            }
        }

        if( !pNewSequence )
            pNewSequence = createInteractiveSequence( xTriggerShape ).get();
    }
    else
    {
        pNewSequence = this;
    }

    if( pOldSequence != pNewSequence )
    {
        if( pOldSequence )
            pOldSequence->maEffects.remove( pEffect );
        if( pNewSequence )
            pNewSequence->maEffects.push_back( pEffect );
        pEffect->setEffectSequence( pNewSequence );
        return true;
    }
    else
    {
        return false;
    }

}

IMPL_LINK_NOARG(MainSequence, onTimerHdl, Timer *, void)
{
    if( mbTimerMode )
    {
        implRebuild();
    }
    else
    {
        reset();
        createMainSequence();
    }
}

/** starts a timer that recreates the internal structure from the API core */
void MainSequence::startRecreateTimer()
{
    if( !mbRebuilding && (mbIgnoreChanges == 0) )
    {
        mbTimerMode = false;
        maTimer.Start();
    }
}

/**
 * starts a timer that rebuilds the API core from the internal structure
 * This is used to reduce the number of screen redraws due to animation changes.
*/
void MainSequence::startRebuildTimer()
{
    mbTimerMode = true;
    maTimer.Start();
}

InteractiveSequence::InteractiveSequence( const Reference< XTimeContainer >& xSequenceRoot, MainSequence* pMainSequence )
: EffectSequenceHelper( xSequenceRoot ), mpMainSequence( pMainSequence )
{
    mnSequenceType = EffectNodeType::INTERACTIVE_SEQUENCE;

    try
    {
        if( mxSequenceRoot.is() )
        {
            Reference< XEnumerationAccess > xEnumerationAccess( mxSequenceRoot, UNO_QUERY_THROW );
            Reference< XEnumeration > xEnumeration( xEnumerationAccess->createEnumeration(), UNO_SET_THROW );
            while( !mxEventSource.is() && xEnumeration->hasMoreElements() )
            {
                Reference< XAnimationNode > xChildNode( xEnumeration->nextElement(), UNO_QUERY_THROW );

                Event aEvent;
                if( (xChildNode->getBegin() >>= aEvent) && (aEvent.Trigger == EventTrigger::ON_CLICK) )
                    aEvent.Source >>= mxEventSource;
            }
        }
    }
    catch( Exception& )
    {
        TOOLS_WARN_EXCEPTION( "sd", "sd::InteractiveSequence::InteractiveSequence()" );
        return;
    }
}

void InteractiveSequence::rebuild()
{
    mpMainSequence->rebuild();
}

void InteractiveSequence::implRebuild()
{
    EffectSequenceHelper::implRebuild();
}

MainSequenceRebuildGuard::MainSequenceRebuildGuard( MainSequencePtr pMainSequence )
: mpMainSequence(std::move( pMainSequence ))
{
    if( mpMainSequence )
        mpMainSequence->lockRebuilds();
}

MainSequenceRebuildGuard::~MainSequenceRebuildGuard()
{
    if( mpMainSequence )
        mpMainSequence->unlockRebuilds();
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
