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

#include <controls/tabpagemodel.hxx>

#include <vcl/svapp.hxx>
#include <helper/property.hxx>
#include <com/sun/star/awt/UnoControlDialogModelProvider.hpp>
#include <com/sun/star/awt/tab/XTabPage.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/supportsservice.hxx>
#include <tools/debug.hxx>
#include <vcl/outdev.hxx>

#include <controls/controlmodelcontainerbase.hxx>
#include <toolkit/controls/unocontrolcontainer.hxx>

#include <helper/unopropertyarrayhelper.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::awt;

UnoControlTabPageModel::UnoControlTabPageModel( Reference< XComponentContext > const & i_factory )
    :ControlModelContainerBase( i_factory )
{
    ImplRegisterProperty( BASEPROPERTY_DEFAULTCONTROL );
    ImplRegisterProperty( BASEPROPERTY_TITLE );
    ImplRegisterProperty( BASEPROPERTY_HELPTEXT );
    ImplRegisterProperty( BASEPROPERTY_HELPURL );
    ImplRegisterProperty( BASEPROPERTY_USERFORMCONTAINEES );
    ImplRegisterProperty( BASEPROPERTY_HSCROLL );
    ImplRegisterProperty( BASEPROPERTY_VSCROLL );
    ImplRegisterProperty( BASEPROPERTY_SCROLLWIDTH );
    ImplRegisterProperty( BASEPROPERTY_SCROLLHEIGHT );
    ImplRegisterProperty( BASEPROPERTY_SCROLLTOP );
    ImplRegisterProperty( BASEPROPERTY_SCROLLLEFT );
    ImplRegisterProperty( BASEPROPERTY_IMAGEURL );
}

OUString SAL_CALL UnoControlTabPageModel::getImplementationName()
{
    return u"stardiv.Toolkit.UnoControlTabPageModel"_ustr;
}

css::uno::Sequence< OUString > SAL_CALL UnoControlTabPageModel::getSupportedServiceNames()
{
    css::uno::Sequence< OUString > aNames = ControlModelContainerBase::getSupportedServiceNames( );
    aNames.realloc( aNames.getLength() + 1 );
    aNames.getArray()[ aNames.getLength() - 1 ] = "com.sun.star.awt.tab.UnoControlTabPageModel";
    return aNames;
}

OUString UnoControlTabPageModel::getServiceName( )
{
    return u"com.sun.star.awt.tab.UnoControlTabPageModel"_ustr;
}

Any UnoControlTabPageModel::ImplGetDefaultValue( sal_uInt16 nPropId ) const
{
    Any aAny;

    switch ( nPropId )
    {
        case BASEPROPERTY_DEFAULTCONTROL:
            aAny <<= u"com.sun.star.awt.tab.UnoControlTabPage"_ustr;
            break;
        case BASEPROPERTY_USERFORMCONTAINEES:
        {
            // We do not have here any usercontainers (yet?), but let's return empty container back
            // so normal properties could be set without triggering UnknownPropertyException
            aAny <<= uno::Reference< XNameContainer >();
            break;
        }
        default:
            aAny = UnoControlModel::ImplGetDefaultValue( nPropId );
    }

    return aAny;
}

::cppu::IPropertyArrayHelper& UnoControlTabPageModel::getInfoHelper()
{
    static UnoPropertyArrayHelper aHelper( ImplGetPropertyIds() );
    return aHelper;
}
// beans::XMultiPropertySet
uno::Reference< beans::XPropertySetInfo > UnoControlTabPageModel::getPropertySetInfo(  )
{
    static uno::Reference< beans::XPropertySetInfo > xInfo( createPropertySetInfo( getInfoHelper() ) );
    return xInfo;
}
////----- XInitialization -------------------------------------------------------------------
void SAL_CALL UnoControlTabPageModel::initialize (const Sequence<Any>& rArguments)
{
    sal_Int16 nPageId = -1;
    if ( rArguments.getLength() == 1 )
    {
        if ( !( rArguments[ 0 ] >>= nPageId ))
             throw lang::IllegalArgumentException();
        m_nTabPageId = nPageId;
    }
    else if ( rArguments.getLength() == 2 )
    {
        if ( !( rArguments[ 0 ] >>= nPageId ))
             throw lang::IllegalArgumentException();
        m_nTabPageId = nPageId;
        OUString sURL;
        if ( !( rArguments[ 1 ] >>= sURL ))
            throw lang::IllegalArgumentException();
        Reference<container::XNameContainer > xDialogModel = awt::UnoControlDialogModelProvider::create( m_xContext, sURL );
        if ( xDialogModel.is() )
        {
            const Sequence< OUString> aNames = xDialogModel->getElementNames();
            for(const OUString& rName : aNames)
            {
                try
                {
                    Any aElement(xDialogModel->getByName(rName));
                    xDialogModel->removeByName(rName);
                    insertByName(rName,aElement);
                }
                catch(const Exception&) {}
            }
            Reference<XPropertySet> xDialogProp(xDialogModel,UNO_QUERY);
            if ( xDialogProp.is() )
            {
                static constexpr OUString s_sResourceResolver = u"ResourceResolver"_ustr;
                setPropertyValue(s_sResourceResolver,xDialogProp->getPropertyValue(s_sResourceResolver));
                setPropertyValue(GetPropertyName(BASEPROPERTY_TITLE),xDialogProp->getPropertyValue(GetPropertyName(BASEPROPERTY_TITLE)));
                setPropertyValue(GetPropertyName(BASEPROPERTY_HELPTEXT),xDialogProp->getPropertyValue(GetPropertyName(BASEPROPERTY_HELPTEXT)));
                setPropertyValue(GetPropertyName(BASEPROPERTY_HELPURL),xDialogProp->getPropertyValue(GetPropertyName(BASEPROPERTY_HELPURL)));
            }
        }
    }
    else
        m_nTabPageId = -1;
}


UnoControlTabPage::UnoControlTabPage( const uno::Reference< uno::XComponentContext >& rxContext )
    :UnoControlTabPage_Base(rxContext)
    ,m_bWindowListener(false)
{
    maComponentInfos.nWidth = 280;
    maComponentInfos.nHeight = 400;
}
UnoControlTabPage::~UnoControlTabPage()
{
}

OUString UnoControlTabPage::GetComponentServiceName() const
{
    return u"TabPageModel"_ustr;
}

OUString SAL_CALL UnoControlTabPage::getImplementationName()
{
    return u"stardiv.Toolkit.UnoControlTabPage"_ustr;
}

sal_Bool SAL_CALL UnoControlTabPage::supportsService(OUString const & ServiceName)
{
    return cppu::supportsService(this, ServiceName);
}

css::uno::Sequence<OUString> SAL_CALL UnoControlTabPage::getSupportedServiceNames()
{
    return { u"com.sun.star.awt.tab.UnoControlTabPage"_ustr };
}

void SAL_CALL UnoControlTabPage::disposing( const lang::EventObject& Source )
{
     ControlContainerBase::disposing( Source );
}

void UnoControlTabPage::createPeer( const Reference< XToolkit > & rxToolkit, const Reference< XWindowPeer >  & rParentPeer )
{
    SolarMutexGuard aSolarGuard;
    ImplUpdateResourceResolver();

    UnoControlContainer::createPeer( rxToolkit, rParentPeer );

    Reference < tab::XTabPage > xTabPage( getPeer(), UNO_QUERY );
    if ( xTabPage.is() )
    {
        if ( !m_bWindowListener )
        {
            Reference< XWindowListener > xWL(this);
            addWindowListener( xWL );
            m_bWindowListener = true;
        }
    }
}

static ::Size ImplMapPixelToAppFont( OutputDevice const * pOutDev, const ::Size& aSize )
{
    ::Size aTmp = pOutDev->PixelToLogic(aSize, MapMode(MapUnit::MapAppFont));
    return aTmp;
}
// css::awt::XWindowListener
void SAL_CALL UnoControlTabPage::windowResized( const css::awt::WindowEvent& e )
{
    OutputDevice*pOutDev = Application::GetDefaultDevice();
    DBG_ASSERT( pOutDev, "Missing Default Device!" );
    if ( !pOutDev || mbSizeModified )
        return;

    // Currently we are simply using MapUnit::MapAppFont
    ::Size aAppFontSize( e.Width, e.Height );

    Reference< XControl > xDialogControl( *this, UNO_QUERY_THROW );
    Reference< XDevice > xDialogDevice( xDialogControl->getPeer(), UNO_QUERY );
    OSL_ENSURE( xDialogDevice.is(), "UnoDialogControl::windowResized: no peer, but a windowResized event?" );
    if ( xDialogDevice.is() )
    {
        DeviceInfo aDeviceInfo( xDialogDevice->getInfo() );
        aAppFontSize.AdjustWidth( -(aDeviceInfo.LeftInset + aDeviceInfo.RightInset) );
        aAppFontSize.AdjustHeight( -(aDeviceInfo.TopInset + aDeviceInfo.BottomInset) );
    }

    aAppFontSize = ImplMapPixelToAppFont( pOutDev, aAppFontSize );

    // Remember that changes have been done by listener. No need to
    // update the position because of property change event.
    mbSizeModified = true;
    // Properties in a sequence must be sorted!
    Sequence< OUString > aProps{ u"Height"_ustr, u"Width"_ustr };
    Sequence< Any > aValues{ Any(aAppFontSize.Height()), Any(aAppFontSize.Width()) };

    ImplSetPropertyValues( aProps, aValues, true );
    mbSizeModified = false;

}

void SAL_CALL UnoControlTabPage::windowMoved( const css::awt::WindowEvent& e )
{
    OutputDevice*pOutDev = Application::GetDefaultDevice();
    DBG_ASSERT( pOutDev, "Missing Default Device!" );
    if ( !pOutDev || mbPosModified )
        return;

    // Currently we are simply using MapUnit::MapAppFont
    ::Size aTmp( e.X, e.Y );
    aTmp = ImplMapPixelToAppFont( pOutDev, aTmp );

    // Remember that changes have been done by listener. No need to
    // update the position because of property change event.
    mbPosModified = true;
    Sequence< OUString > aProps{ u"PositionX"_ustr, u"PositionY"_ustr };
    Sequence< Any > aValues{ Any(aTmp.Width()), Any(aTmp.Height()) };

    ImplSetPropertyValues( aProps, aValues, true );
    mbPosModified = false;

}

void SAL_CALL UnoControlTabPage::windowShown( const css::lang::EventObject& ) {}

void SAL_CALL UnoControlTabPage::windowHidden( const css::lang::EventObject& ) {}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
stardiv_Toolkit_UnoControlTabPageModel_get_implementation(
    css::uno::XComponentContext *context,
    css::uno::Sequence<css::uno::Any> const &)
{
    return cppu::acquire(new UnoControlTabPageModel(context));
}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
stardiv_Toolkit_UnoControlTabPage_get_implementation(
    css::uno::XComponentContext *context,
    css::uno::Sequence<css::uno::Any> const &)
{
    return cppu::acquire(new UnoControlTabPage(context));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
