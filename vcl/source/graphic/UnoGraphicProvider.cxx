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

#include <o3tl/string_view.hxx>
#include <o3tl/temporary.hxx>
#include <vcl/svapp.hxx>
#include <vcl/image.hxx>
#include <vcl/metaact.hxx>
#include <imagerepository.hxx>
#include <tools/fract.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <vcl/graphic/BitmapHelper.hxx>
#include <vcl/graphicfilter.hxx>
#include <vcl/stdtext.hxx>
#include <vcl/wmfexternal.hxx>
#include <vcl/virdev.hxx>
#include <com/sun/star/awt/XBitmap.hpp>
#include <com/sun/star/graphic/XGraphicProvider2.hpp>
#include <com/sun/star/io/XStream.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/text/GraphicCrop.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/fileformat.h>
#include <comphelper/servicehelper.hxx>
#include <cppuhelper/implbase.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <sal/log.hxx>

#include <graphic/UnoGraphicDescriptor.hxx>
#include <graphic/UnoGraphic.hxx>
#include <rtl/ref.hxx>
#include <vcl/dibtools.hxx>
#include <comphelper/sequence.hxx>
#include <memory>
#include <string_view>

#include <vcl/TypeSerializer.hxx>

using namespace com::sun::star;

namespace
{
SvMemoryStream AsStream(const css::uno::Sequence<sal_Int8>& s)
{
    return SvMemoryStream(const_cast<sal_Int8*>(s.getConstArray()), s.getLength(),
                          StreamMode::READ);
}

Bitmap BitmapFromDIB(const css::uno::Sequence<sal_Int8>& dib)
{
    Bitmap bmp;
    if (dib.hasElements())
        ReadDIB(bmp, o3tl::temporary(AsStream(dib)), true);
    return bmp;
}
}

namespace vcl
{
BitmapEx GetBitmap(const css::uno::Reference<css::awt::XBitmap>& xBitmap)
{
    if (!xBitmap)
        return {};

    if (auto xGraphic = xBitmap.query<css::graphic::XGraphic>())
        return Graphic(xGraphic).GetBitmapEx();

    // This is an unknown implementation of a XBitmap interface
    if (Bitmap aMask = BitmapFromDIB(xBitmap->getMaskDIB()); !aMask.IsEmpty())
    {
        aMask.Invert(); // Convert from transparency to alpha
        return BitmapEx(BitmapFromDIB(xBitmap->getDIB()), aMask);
    }

    BitmapEx aBmp;
    ReadDIBBitmapEx(aBmp, o3tl::temporary(AsStream(xBitmap->getDIB())), true);
    return aBmp;
}

css::uno::Reference<css::graphic::XGraphic> GetGraphic(const css::uno::Any& any)
{
    if (auto xRet = any.query<css::graphic::XGraphic>())
        return xRet;

    if (BitmapEx aBmpEx = GetBitmap(any.query<css::awt::XBitmap>()); !aBmpEx.IsEmpty())
    {
        return Graphic(aBmpEx).GetXGraphic();
    }

    return {};
}
}

namespace {

class GraphicProvider : public ::cppu::WeakImplHelper< css::graphic::XGraphicProvider2,
                                                        css::lang::XServiceInfo >
{
public:

    GraphicProvider();

protected:

    // XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService( const OUString& ServiceName ) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getSupportedServiceNames() override;

    // XTypeProvider
    virtual css::uno::Sequence< css::uno::Type > SAL_CALL getTypes(  ) override;
    virtual css::uno::Sequence< sal_Int8 > SAL_CALL getImplementationId(  ) override;

    // XGraphicProvider
    virtual css::uno::Reference< css::beans::XPropertySet > SAL_CALL queryGraphicDescriptor( const css::uno::Sequence< css::beans::PropertyValue >& MediaProperties ) override;
    virtual css::uno::Reference< css::graphic::XGraphic > SAL_CALL queryGraphic( const css::uno::Sequence< css::beans::PropertyValue >& MediaProperties ) override;
    virtual void SAL_CALL storeGraphic( const css::uno::Reference< css::graphic::XGraphic >& Graphic, const css::uno::Sequence< css::beans::PropertyValue >& MediaProperties ) override;

    // XGraphicProvider2
    uno::Sequence< uno::Reference<graphic::XGraphic> > SAL_CALL queryGraphics(const uno::Sequence< uno::Sequence<beans::PropertyValue> >& MediaPropertiesSeq ) override;

private:

    static css::uno::Reference< css::graphic::XGraphic > implLoadMemory( std::u16string_view rResourceURL );
    static css::uno::Reference< css::graphic::XGraphic > implLoadRepositoryImage( std::u16string_view rResourceURL );
    static css::uno::Reference< css::graphic::XGraphic > implLoadStandardImage( std::u16string_view rResourceURL );
};

GraphicProvider::GraphicProvider()
{
}

OUString SAL_CALL GraphicProvider::getImplementationName()
{
    return u"com.sun.star.comp.graphic.GraphicProvider"_ustr;
}

sal_Bool SAL_CALL GraphicProvider::supportsService( const OUString& ServiceName )
{
    return cppu::supportsService( this, ServiceName );
}

uno::Sequence< OUString > SAL_CALL GraphicProvider::getSupportedServiceNames()
{
    return { u"com.sun.star.graphic.GraphicProvider"_ustr };
}

uno::Sequence< uno::Type > SAL_CALL GraphicProvider::getTypes()
{
    static const uno::Sequence< uno::Type > aTypes {
        cppu::UnoType<lang::XServiceInfo>::get(),
        cppu::UnoType<lang::XTypeProvider>::get(),
        cppu::UnoType<graphic::XGraphicProvider>::get()
    };
    return aTypes;
}

uno::Sequence< sal_Int8 > SAL_CALL GraphicProvider::getImplementationId()
{
    return css::uno::Sequence<sal_Int8>();
}

uno::Reference< ::graphic::XGraphic > GraphicProvider::implLoadMemory( std::u16string_view rResourceURL )
{
    sal_Int32                               nIndex = 0;

    if( o3tl::getToken(rResourceURL, 0, '/', nIndex ) != u"private:memorygraphic" )
        return nullptr;

    sal_Int64 nGraphicAddress = o3tl::toInt64(o3tl::getToken(rResourceURL, 0, '/', nIndex ));
    if( nGraphicAddress == 0 )
        return nullptr;

    rtl::Reference<::unographic::Graphic> pUnoGraphic = new ::unographic::Graphic;

    pUnoGraphic->init( *reinterpret_cast< ::Graphic* >( nGraphicAddress ) );
    return pUnoGraphic;
}

uno::Reference< ::graphic::XGraphic > GraphicProvider::implLoadRepositoryImage( std::u16string_view rResourceURL )
{
    uno::Reference< ::graphic::XGraphic >   xRet;

    std::u16string_view sPathName;
    if( o3tl::starts_with(rResourceURL, u"private:graphicrepository/", &sPathName) )
    {
        BitmapEx aBitmap;
        if ( vcl::ImageRepository::loadImage( OUString(sPathName), aBitmap ) )
        {
            Graphic aGraphic(aBitmap);
            aGraphic.setOriginURL(OUString(rResourceURL));
            xRet = aGraphic.GetXGraphic();
        }
    }
    return xRet;
}

uno::Reference< ::graphic::XGraphic > GraphicProvider::implLoadStandardImage( std::u16string_view rResourceURL )
{
    uno::Reference< ::graphic::XGraphic >   xRet;

    std::u16string_view sImageName;
    if( o3tl::starts_with(rResourceURL, u"private:standardimage/", &sImageName) )
    {
        if ( sImageName == u"info" )
        {
            xRet = Graphic(GetStandardInfoBoxImage().GetBitmap()).GetXGraphic();
        }
        else if ( sImageName == u"warning" )
        {
            xRet = Graphic(GetStandardWarningBoxImage().GetBitmap()).GetXGraphic();
        }
        else if ( sImageName == u"error" )
        {
            xRet = Graphic(GetStandardErrorBoxImage().GetBitmap()).GetXGraphic();
        }
        else if ( sImageName == u"query" )
        {
            xRet = Graphic(GetStandardQueryBoxImage().GetBitmap()).GetXGraphic();
        }
    }
    return xRet;
}


uno::Reference< beans::XPropertySet > SAL_CALL GraphicProvider::queryGraphicDescriptor( const uno::Sequence< beans::PropertyValue >& rMediaProperties )
{
    OUString aURL;
    uno::Reference< io::XInputStream > xIStm;
    uno::Any aBtm;

    for( const auto& rMediaProperty : rMediaProperties )
    {
        const OUString   aName( rMediaProperty.Name );
        const uno::Any          aValue( rMediaProperty.Value );

        if (aName == "URL")
        {
            aValue >>= aURL;
        }
        else if (aName == "InputStream")
        {
            aValue >>= xIStm;
        }
        else if (aName == "Bitmap")
        {
            aBtm = aValue;
        }
    }

    SolarMutexGuard g;

    uno::Reference<beans::XPropertySet> xRet;
    if( xIStm.is() )
    {
        rtl::Reference<unographic::GraphicDescriptor> pDescriptor = new unographic::GraphicDescriptor;
        pDescriptor->init( xIStm, aURL );
        xRet = pDescriptor;
    }
    else if( !aURL.isEmpty() )
    {
        uno::Reference< ::graphic::XGraphic > xGraphic( implLoadMemory( aURL ) );

        if ( !xGraphic.is() )
            xGraphic = implLoadRepositoryImage( aURL );

        if ( !xGraphic.is() )
            xGraphic = implLoadStandardImage( aURL );

        if( xGraphic.is() )
        {
            xRet.set( xGraphic, uno::UNO_QUERY );
        }
        else
        {
            rtl::Reference<unographic::GraphicDescriptor> pDescriptor = new unographic::GraphicDescriptor;
            pDescriptor->init( aURL );
            xRet = pDescriptor;
        }
    }
    else if (aBtm.hasValue())
    {
        xRet.set(vcl::GetGraphic(aBtm), uno::UNO_QUERY);
    }

    return xRet;
}


uno::Reference< ::graphic::XGraphic > SAL_CALL GraphicProvider::queryGraphic( const uno::Sequence< ::beans::PropertyValue >& rMediaProperties )
{
    OUString                                aPath;

    uno::Reference< io::XInputStream > xIStm;
    uno::Any aBtm;

    uno::Sequence< ::beans::PropertyValue > aFilterData;

    bool bLazyRead = false;
    bool bLoadAsLink = false;

    for (const auto& rMediaProperty : rMediaProperties)
    {
        const OUString   aName( rMediaProperty.Name );
        const uno::Any          aValue( rMediaProperty.Value );

        if (aName == "URL")
        {
            aValue >>= aPath;
        }
        else if (aName == "InputStream")
        {
            aValue >>= xIStm;
        }
        else if (aName == "Bitmap")
        {
            aBtm = aValue;
        }
        else if (aName == "FilterData")
        {
            aValue >>= aFilterData;
        }
        else if (aName == "LazyRead")
        {
            aValue >>= bLazyRead;
        }
        else if (aName == "LoadAsLink")
        {
            aValue >>= bLoadAsLink;
        }
    }

    sal_uInt16 nExtMapMode = 0;
    for (const auto& rProp : aFilterData)
    {
        const OUString   aName( rProp.Name );
        const uno::Any          aValue( rProp.Value );

        if (aName == "ExternalMapMode")
        {
            aValue >>= nExtMapMode;
        }
    }

    SolarMutexGuard g;

    uno::Reference<::graphic::XGraphic> xRet;
    std::unique_ptr<SvStream> pIStm;

    if( xIStm.is() )
    {
        pIStm = ::utl::UcbStreamHelper::CreateStream( xIStm );
    }
    else if( !aPath.isEmpty() )
    {
        xRet = implLoadMemory( aPath );

        if ( !xRet.is() )
            xRet = implLoadRepositoryImage( aPath );

        if ( !xRet.is() )
            xRet = implLoadStandardImage( aPath );

        if( !xRet.is() )
            pIStm = ::utl::UcbStreamHelper::CreateStream( aPath, StreamMode::READ );
    }
    else if (aBtm.hasValue())
    {
        xRet = vcl::GetGraphic(aBtm);
    }

    if( pIStm )
    {
        ::GraphicFilter& rFilter = ::GraphicFilter::GetGraphicFilter();

        if ( nExtMapMode > 0 )
        {
            bLazyRead = false;
        }

        Graphic aVCLGraphic;
        ErrCode error = ERRCODE_NONE;
        if (bLazyRead)
        {
            aVCLGraphic = rFilter.ImportUnloadedGraphic(*pIStm);
        }
        if (aVCLGraphic.IsNone())
            error = rFilter.ImportGraphic(aVCLGraphic, aPath, *pIStm, GRFILTER_FORMAT_DONTKNOW, nullptr, GraphicFilterImportFlags::NONE);

        if (error == ERRCODE_NONE && !aVCLGraphic.IsNone())
        {
            if (!aPath.isEmpty() && bLoadAsLink)
                aVCLGraphic.setOriginURL(aPath);

            rtl::Reference<::unographic::Graphic> pUnoGraphic = new ::unographic::Graphic;

            pUnoGraphic->init( aVCLGraphic );
            xRet = pUnoGraphic;
        }
        else{
            SAL_WARN("svtools", "Could not create graphic for:" << aPath << " error: " << error);
        }
    }

    return xRet;
}

uno::Sequence< uno::Reference<graphic::XGraphic> > SAL_CALL GraphicProvider::queryGraphics(const uno::Sequence< uno::Sequence<beans::PropertyValue> >& rMediaPropertiesSeq)
{
    // Turn properties into streams.
    std::vector< std::unique_ptr<SvStream> > aStreams;
    for (const auto& rMediaProperties : rMediaPropertiesSeq)
    {
        std::unique_ptr<SvStream> pStream;
        uno::Reference<io::XInputStream> xStream;

        auto pProp = std::find_if(rMediaProperties.begin(), rMediaProperties.end(),
            [](const beans::PropertyValue& rProp) { return rProp.Name == "InputStream"; });
        if (pProp != rMediaProperties.end())
        {
            pProp->Value >>= xStream;
            if (xStream.is())
                pStream = utl::UcbStreamHelper::CreateStream(xStream);
        }

        aStreams.push_back(std::move(pStream));
    }

    // Import: streams to graphics.
    std::vector< std::shared_ptr<Graphic> > aGraphics;
    GraphicFilter& rFilter = GraphicFilter::GetGraphicFilter();
    rFilter.ImportGraphics(aGraphics, std::move(aStreams));

    // Returning: graphics to UNO objects.
    std::vector< uno::Reference<graphic::XGraphic> > aRet;
    for (const auto& pGraphic : aGraphics)
    {
        uno::Reference<graphic::XGraphic> xGraphic;

        if (pGraphic)
        {
            rtl::Reference<unographic::Graphic> pUnoGraphic = new unographic::Graphic();
            pUnoGraphic->init(*pGraphic);
            xGraphic = pUnoGraphic;
        }

        aRet.push_back(xGraphic);
    }

    return comphelper::containerToSequence(aRet);
}

void ImplCalculateCropRect( ::Graphic const & rGraphic, const text::GraphicCrop& rGraphicCropLogic, tools::Rectangle& rGraphicCropPixel )
{
    if ( !(rGraphicCropLogic.Left || rGraphicCropLogic.Top || rGraphicCropLogic.Right || rGraphicCropLogic.Bottom) )
        return;

    Size aSourceSizePixel( rGraphic.GetSizePixel() );
    if ( !(aSourceSizePixel.Width() && aSourceSizePixel.Height()) )
        return;

    if ( !(rGraphicCropLogic.Left || rGraphicCropLogic.Top || rGraphicCropLogic.Right || rGraphicCropLogic.Bottom) )
        return;

    Size aSize100thMM( 0, 0 );
    if( rGraphic.GetPrefMapMode().GetMapUnit() != MapUnit::MapPixel )
    {
        aSize100thMM = OutputDevice::LogicToLogic(rGraphic.GetPrefSize(), rGraphic.GetPrefMapMode(), MapMode(MapUnit::Map100thMM));
    }
    else
    {
        aSize100thMM = Application::GetDefaultDevice()->PixelToLogic(rGraphic.GetPrefSize(), MapMode(MapUnit::Map100thMM));
    }
    if ( aSize100thMM.Width() && aSize100thMM.Height() )
    {
        double fSourceSizePixelWidth = static_cast<double>(aSourceSizePixel.Width());
        double fSourceSizePixelHeight= static_cast<double>(aSourceSizePixel.Height());
        rGraphicCropPixel.SetLeft( static_cast< sal_Int32 >((fSourceSizePixelWidth * rGraphicCropLogic.Left ) / aSize100thMM.Width()) );
        rGraphicCropPixel.SetTop( static_cast< sal_Int32 >((fSourceSizePixelHeight * rGraphicCropLogic.Top ) / aSize100thMM.Height()) );
        rGraphicCropPixel.SetRight( static_cast< sal_Int32 >(( fSourceSizePixelWidth * ( aSize100thMM.Width() - rGraphicCropLogic.Right ) ) / aSize100thMM.Width() ) );
        rGraphicCropPixel.SetBottom( static_cast< sal_Int32 >(( fSourceSizePixelHeight * ( aSize100thMM.Height() - rGraphicCropLogic.Bottom ) ) / aSize100thMM.Height() ) );
    }
}

void ImplApplyBitmapScaling( ::Graphic& rGraphic, sal_Int32 nPixelWidth, sal_Int32 nPixelHeight )
{
    if ( nPixelWidth && nPixelHeight )
    {
        BitmapEx aBmpEx( rGraphic.GetBitmapEx() );
        MapMode aPrefMapMode( aBmpEx.GetPrefMapMode() );
        Size    aPrefSize( aBmpEx.GetPrefSize() );
        aBmpEx.Scale( Size( nPixelWidth, nPixelHeight ) );
        aBmpEx.SetPrefMapMode( aPrefMapMode );
        aBmpEx.SetPrefSize( aPrefSize );
        rGraphic = aBmpEx;
    }
}

void ImplApplyBitmapResolution( ::Graphic& rGraphic, sal_Int32 nImageResolution, const Size& rVisiblePixelSize, const awt::Size& rLogicalSize )
{
    if ( !(nImageResolution && rLogicalSize.Width && rLogicalSize.Height) )
        return;

    const double fImageResolution = static_cast<double>( nImageResolution );
    const double fSourceDPIX = ( static_cast<double>(rVisiblePixelSize.Width()) * 2540.0 ) / static_cast<double>(rLogicalSize.Width);
    const double fSourceDPIY = ( static_cast<double>(rVisiblePixelSize.Height()) * 2540.0 ) / static_cast<double>(rLogicalSize.Height);
    const sal_Int32 nSourcePixelWidth( rGraphic.GetSizePixel().Width() );
    const sal_Int32 nSourcePixelHeight( rGraphic.GetSizePixel().Height() );
    const double fSourcePixelWidth = static_cast<double>( nSourcePixelWidth );
    const double fSourcePixelHeight= static_cast<double>( nSourcePixelHeight );

    sal_Int32 nDestPixelWidth = nSourcePixelWidth;
    sal_Int32 nDestPixelHeight = nSourcePixelHeight;

    // check, if the bitmap DPI exceeds the maximum DPI
    if( fSourceDPIX > fImageResolution )
    {
        nDestPixelWidth = static_cast<sal_Int32>(( fSourcePixelWidth * fImageResolution ) / fSourceDPIX);
        if ( !nDestPixelWidth || ( nDestPixelWidth > nSourcePixelWidth ) )
            nDestPixelWidth = nSourcePixelWidth;
    }
    if ( fSourceDPIY > fImageResolution )
    {
        nDestPixelHeight= static_cast<sal_Int32>(( fSourcePixelHeight* fImageResolution ) / fSourceDPIY);
        if ( !nDestPixelHeight || ( nDestPixelHeight > nSourcePixelHeight ) )
            nDestPixelHeight = nSourcePixelHeight;
    }
    if ( ( nDestPixelWidth != nSourcePixelWidth ) || ( nDestPixelHeight != nSourcePixelHeight ) )
        ImplApplyBitmapScaling( rGraphic, nDestPixelWidth, nDestPixelHeight );
}

void ImplApplyFilterData( ::Graphic& rGraphic, const uno::Sequence< beans::PropertyValue >& rFilterData )
{
    /* this method applies following attributes to the graphic, in the first step the
       cropping area (logical size in 100thmm) is applied, in the second step the resolution
       is applied, in the third step the graphic is scaled to the corresponding pixelsize.
       if a parameter value is zero or not available the corresponding step will be skipped */

    sal_Int32 nPixelWidth = 0;
    sal_Int32 nPixelHeight= 0;
    sal_Int32 nImageResolution = 0;
    awt::Size aLogicalSize( 0, 0 );
    text::GraphicCrop aCropLogic( 0, 0, 0, 0 );
    bool bRemoveCropArea = true;

    for( const auto& rProp : rFilterData )
    {
        const OUString   aName(  rProp.Name );
        const uno::Any          aValue( rProp.Value );

        if (aName == "PixelWidth")
            aValue >>= nPixelWidth;
        else if (aName == "PixelHeight")
            aValue >>= nPixelHeight;
        else if (aName == "LogicalSize")
            aValue >>= aLogicalSize;
        else if (aName == "GraphicCropLogic")
            aValue >>= aCropLogic;
        else if (aName == "RemoveCropArea")
            aValue >>= bRemoveCropArea;
        else if (aName == "ImageResolution")
            aValue >>= nImageResolution;
    }
    if ( rGraphic.GetType() == GraphicType::Bitmap )
    {
        if(rGraphic.getVectorGraphicData())
        {
            // embedded Vector Graphic Data, no need to scale. Also no method to apply crop data currently
        }
        else
        {
            tools::Rectangle aCropPixel( Point( 0, 0 ), rGraphic.GetSizePixel() );
            ImplCalculateCropRect( rGraphic, aCropLogic, aCropPixel );
            if ( bRemoveCropArea )
            {
                BitmapEx aBmpEx( rGraphic.GetBitmapEx() );
                aBmpEx.Crop( aCropPixel );
                rGraphic = aBmpEx;
            }
            Size aVisiblePixelSize( bRemoveCropArea ? rGraphic.GetSizePixel() : aCropPixel.GetSize() );
            ImplApplyBitmapResolution( rGraphic, nImageResolution, aVisiblePixelSize, aLogicalSize );
            ImplApplyBitmapScaling( rGraphic, nPixelWidth, nPixelHeight );
        }
    }
    else if ( ( rGraphic.GetType() == GraphicType::GdiMetafile ) && nImageResolution )
    {
        ScopedVclPtrInstance< VirtualDevice > aDummyVDev;
        GDIMetaFile aMtf( rGraphic.GetGDIMetaFile() );
        Size aMtfSize( OutputDevice::LogicToLogic(aMtf.GetPrefSize(), aMtf.GetPrefMapMode(), MapMode(MapUnit::Map100thMM)) );
        if ( aMtfSize.Width() && aMtfSize.Height() )
        {
            MapMode aNewMapMode( MapUnit::Map100thMM );
            aNewMapMode.SetScaleX( Fraction( aLogicalSize.Width, aMtfSize.Width() ) );
            aNewMapMode.SetScaleY( Fraction( aLogicalSize.Height, aMtfSize.Height() ) );
            aDummyVDev->EnableOutput( false );
            aDummyVDev->SetMapMode( aNewMapMode );

            for( size_t i = 0, nObjCount = aMtf.GetActionSize(); i < nObjCount; i++ )
            {
                MetaAction* pAction = aMtf.GetAction( i );
                switch( pAction->GetType() )
                {
                    // only optimizing common bitmap actions:
                    case MetaActionType::MAPMODE:
                    {
                        pAction->Execute( aDummyVDev.get() );
                        break;
                    }
                    case MetaActionType::PUSH:
                    {
                        const MetaPushAction* pA = static_cast<const MetaPushAction*>(pAction);
                        aDummyVDev->Push( pA->GetFlags() );
                        break;
                    }
                    case MetaActionType::POP:
                    {
                        aDummyVDev->Pop();
                        break;
                    }
                    case MetaActionType::BMPSCALE:
                    case MetaActionType::BMPEXSCALE:
                    {
                        BitmapEx aBmpEx;
                        Point aPos;
                        Size aSize;
                        if ( pAction->GetType() == MetaActionType::BMPSCALE )
                        {
                            MetaBmpScaleAction* pScaleAction = dynamic_cast< MetaBmpScaleAction* >( pAction );
                            assert(pScaleAction);
                            aBmpEx = pScaleAction->GetBitmap();
                            aPos = pScaleAction->GetPoint();
                            aSize = pScaleAction->GetSize();
                        }
                        else
                        {
                            MetaBmpExScaleAction* pScaleAction = dynamic_cast< MetaBmpExScaleAction* >( pAction );
                            assert(pScaleAction);
                            aBmpEx = pScaleAction->GetBitmapEx();
                            aPos = pScaleAction->GetPoint();
                            aSize = pScaleAction->GetSize();
                        }
                        ::Graphic aGraphic( aBmpEx );
                        const Size aSize100thmm( aDummyVDev->LogicToPixel( aSize ) );
                        Size aSize100thmm2( aDummyVDev->PixelToLogic(aSize100thmm, MapMode(MapUnit::Map100thMM)) );

                        ImplApplyBitmapResolution( aGraphic, nImageResolution,
                            aGraphic.GetSizePixel(), awt::Size( aSize100thmm2.Width(), aSize100thmm2.Height() ) );

                        rtl::Reference<MetaAction> pNewAction = new MetaBmpExScaleAction( aPos, aSize, aGraphic.GetBitmapEx() );
                        aMtf.ReplaceAction( pNewAction, i );
                        break;
                    }
                    default:
                    case MetaActionType::BMP:
                    case MetaActionType::BMPSCALEPART:
                    case MetaActionType::BMPEX:
                    case MetaActionType::BMPEXSCALEPART:
                    case MetaActionType::MASK:
                    case MetaActionType::MASKSCALE:
                    break;
                }
            }
            rGraphic = aMtf;
        }
    }
}


void SAL_CALL GraphicProvider::storeGraphic( const uno::Reference< ::graphic::XGraphic >& rxGraphic, const uno::Sequence< beans::PropertyValue >& rMediaProperties )
{
    std::unique_ptr<SvStream> pOStm;
    OUString    aPath;

    for( const auto& rMediaProperty : rMediaProperties )
    {
        const OUString   aName( rMediaProperty.Name );
        const uno::Any          aValue( rMediaProperty.Value );

        if (aName == "URL")
        {
            OUString aURL;

            aValue >>= aURL;
            pOStm = ::utl::UcbStreamHelper::CreateStream( aURL, StreamMode::WRITE | StreamMode::TRUNC );
            aPath = aURL;
        }
        else if (aName == "OutputStream")
        {
            uno::Reference< io::XStream > xOStm;

            aValue >>= xOStm;

            if( xOStm.is() )
                pOStm = ::utl::UcbStreamHelper::CreateStream( xOStm );
        }

        if( pOStm )
            break;
    }

    if( !pOStm )
        return;

    uno::Sequence< beans::PropertyValue >   aFilterDataSeq;
    OUString sFilterShortName;

    for( const auto& rMediaProperty : rMediaProperties )
    {
        const OUString   aName( rMediaProperty.Name );
        const uno::Any          aValue( rMediaProperty.Value );

        if (aName == "FilterData")
        {
            aValue >>= aFilterDataSeq;
        }
        else if (aName == "MimeType")
        {
            OUString aMimeType;

            aValue >>= aMimeType;

            if (aMimeType == MIMETYPE_BMP)
                sFilterShortName = "bmp";
            else if (aMimeType == MIMETYPE_EPS)
                sFilterShortName = "eps";
            else if (aMimeType == MIMETYPE_GIF)
                sFilterShortName = "gif";
            else if (aMimeType == MIMETYPE_JPG)
                sFilterShortName = "jpg";
            else if (aMimeType == MIMETYPE_MET)
                sFilterShortName = "met";
            else if (aMimeType == MIMETYPE_PNG)
                sFilterShortName = "png";
            else if (aMimeType == MIMETYPE_PCT)
                sFilterShortName = "pct";
            else if (aMimeType == MIMETYPE_PBM)
                sFilterShortName = "pbm";
            else if (aMimeType == MIMETYPE_PGM)
                sFilterShortName = "pgm";
            else if (aMimeType == MIMETYPE_PPM)
                sFilterShortName = "ppm";
            else if (aMimeType == MIMETYPE_RAS)
                sFilterShortName = "ras";
            else if (aMimeType == MIMETYPE_SVM)
                sFilterShortName = "svm";
            else if (aMimeType == MIMETYPE_TIF)
                sFilterShortName = "tif";
            else if (aMimeType == MIMETYPE_EMF)
                sFilterShortName = "emf";
            else if (aMimeType == MIMETYPE_WMF)
                sFilterShortName = "wmf";
            else if (aMimeType == MIMETYPE_XPM)
                sFilterShortName = "xpm";
            else if (aMimeType == MIMETYPE_SVG)
                sFilterShortName = "svg";
            else if (aMimeType == MIMETYPE_VCLGRAPHIC)
                sFilterShortName = MIMETYPE_VCLGRAPHIC;
        }
    }

    if( sFilterShortName.isEmpty() )
        return;

    ::GraphicFilter& rFilter = ::GraphicFilter::GetGraphicFilter();

    {
        const uno::Reference< XInterface >  xIFace( rxGraphic, uno::UNO_QUERY );
        const ::unographic::Graphic* pUnoGraphic = dynamic_cast<::unographic::Graphic*>(xIFace.get());
        const ::Graphic* pGraphic = pUnoGraphic ? &pUnoGraphic->GetGraphic() : nullptr;

        if( pGraphic && ( pGraphic->GetType() != GraphicType::NONE ) )
        {
            ::Graphic aGraphic( *pGraphic );
            ImplApplyFilterData( aGraphic, aFilterDataSeq );

            /* sj: using a temporary memory stream, because some graphic filters are seeking behind
               stream end (which leads to an invalid argument exception then). */
            SvMemoryStream aMemStrm;
            aMemStrm.SetVersion( SOFFICE_FILEFORMAT_CURRENT );
            if( sFilterShortName == MIMETYPE_VCLGRAPHIC )
            {
                TypeSerializer aSerializer(aMemStrm);
                aSerializer.writeGraphic(aGraphic);
            }
            else
            {
                rFilter.ExportGraphic( aGraphic, aPath, aMemStrm,
                                        rFilter.GetExportFormatNumberForShortName( sFilterShortName ),
                                            ( aFilterDataSeq.hasElements() ? &aFilterDataSeq : nullptr ) );
            }
            pOStm->WriteBytes( aMemStrm.GetData(), aMemStrm.TellEnd() );
        }
    }
}

}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
com_sun_star_comp_graphic_GraphicProvider_get_implementation(
    css::uno::XComponentContext *,
    css::uno::Sequence<css::uno::Any> const &)
{
    return cppu::acquire(new GraphicProvider);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
