/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <sal/config.h>

#include <array>
#include <utility>

#include <tools/helpers.hxx>
#include <vcl/BitmapTools.hxx>

#include <sal/log.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/seqstream.hxx>
#include <vcl/canvastools.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>

#include <com/sun/star/graphic/SvgTools.hpp>
#include <com/sun/star/graphic/Primitive2DTools.hpp>

#include <drawinglayer/primitive2d/baseprimitive2d.hxx>

#include <com/sun/star/rendering/XIntegerReadOnlyBitmap.hpp>

#include <vcl/dibtools.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/virdev.hxx>
#if ENABLE_CAIRO_CANVAS
#include <cairo.h>
#endif
#include <comphelper/diagnose_ex.hxx>
#include <tools/fract.hxx>
#include <tools/stream.hxx>
#include <vcl/BitmapWriteAccess.hxx>

using namespace css;

using drawinglayer::primitive2d::Primitive2DSequence;
using drawinglayer::primitive2d::Primitive2DReference;

namespace vcl::bitmap
{

BitmapEx loadFromName(const OUString& rFileName, const ImageLoadFlags eFlags)
{
    bool bSuccess = true;
    OUString aIconTheme;
    BitmapEx aBitmapEx;
    try
    {
        aIconTheme = Application::GetSettings().GetStyleSettings().DetermineIconTheme();
        ImageTree::get().loadImage(rFileName, aIconTheme, aBitmapEx, true, eFlags);
    }
    catch (...)
    {
        bSuccess = false;
    }

    SAL_WARN_IF(!bSuccess, "vcl", "vcl::bitmap::loadFromName : could not load image " << rFileName << " via icon theme " << aIconTheme);

    return aBitmapEx;
}

void loadFromSvg(SvStream& rStream, const OUString& sPath, BitmapEx& rBitmapEx, double fScalingFactor)
{
    const uno::Reference<uno::XComponentContext>& xContext(comphelper::getProcessComponentContext());
    const uno::Reference<graphic::XSvgParser> xSvgParser = graphic::SvgTools::create(xContext);

    std::size_t nSize = rStream.remainingSize();
    std::vector<sal_Int8> aBuffer(nSize + 1);
    rStream.ReadBytes(aBuffer.data(), nSize);
    aBuffer[nSize] = 0;

    uno::Sequence<sal_Int8> aData(aBuffer.data(), nSize + 1);
    uno::Reference<io::XInputStream> aInputStream(new comphelper::SequenceInputStream(aData));

    const Primitive2DSequence aPrimitiveSequence = xSvgParser->getDecomposition(aInputStream, sPath);

    if (!aPrimitiveSequence.hasElements())
        return;

    uno::Sequence<beans::PropertyValue> aViewParameters;

    geometry::RealRectangle2D aRealRect;
    basegfx::B2DRange aRange;
    for (css::uno::Reference<css::graphic::XPrimitive2D> const & xReference : aPrimitiveSequence)
    {
        if (xReference.is())
        {
            aRealRect = xReference->getRange(aViewParameters);
            aRange.expand(basegfx::B2DRange(aRealRect.X1, aRealRect.Y1, aRealRect.X2, aRealRect.Y2));
        }
    }

    aRealRect.X1 = aRange.getMinX();
    aRealRect.Y1 = aRange.getMinY();
    aRealRect.X2 = aRange.getMaxX();
    aRealRect.Y2 = aRange.getMaxY();

    double nDPI = 96 * fScalingFactor;

    const css::uno::Reference<css::graphic::XPrimitive2DRenderer> xPrimitive2DRenderer = css::graphic::Primitive2DTools::create(xContext);
    const css::uno::Reference<css::rendering::XBitmap> xBitmap(
        xPrimitive2DRenderer->rasterize(aPrimitiveSequence, aViewParameters, nDPI, nDPI, aRealRect, 256*256));

    if (xBitmap.is())
    {
        const css::uno::Reference<css::rendering::XIntegerReadOnlyBitmap> xIntBmp(xBitmap, uno::UNO_QUERY_THROW);
        rBitmapEx = vcl::unotools::bitmapExFromXBitmap(xIntBmp);
    }

}

/** Copy block of image data into the bitmap.
    Assumes that the Bitmap has been constructed with the desired size.

    @param pData
    The block of data to copy
    @param nStride
    The number of bytes in a scanline, must >= (width * nBitCount / 8)
    @param bReversColors
    In case the endianness of pData is wrong, you could reverse colors
*/
Bitmap CreateFromData(sal_uInt8 const *pData, sal_Int32 nWidth, sal_Int32 nHeight,
                        sal_Int32 nStride, sal_Int8 nBitCount,
                        bool bReversColors, bool bReverseAlpha)
{
    assert(nStride >= (nWidth * nBitCount / 8));
    assert(nBitCount == 1 || nBitCount == 8 || nBitCount == 24 || nBitCount == 32);

    PixelFormat ePixelFormat;
    if (nBitCount == 1)
        ePixelFormat = PixelFormat::N8_BPP; // we convert 1-bit input data to 8-bit format
    else if (nBitCount == 8)
        ePixelFormat = PixelFormat::N8_BPP;
    else if (nBitCount == 24)
        ePixelFormat = PixelFormat::N24_BPP;
    else if (nBitCount == 32)
        ePixelFormat = PixelFormat::N32_BPP;
    else
        std::abort();
    Bitmap aBmp;
    if (nBitCount == 1)
    {
        BitmapPalette aBiLevelPalette { COL_BLACK, COL_WHITE };
        aBmp = Bitmap(Size(nWidth, nHeight), PixelFormat::N8_BPP, &aBiLevelPalette);
    }
    else
        aBmp = Bitmap(Size(nWidth, nHeight), ePixelFormat);

    BitmapScopedWriteAccess pWrite(aBmp);
    assert(pWrite.get());
    if( !pWrite )
        return Bitmap();
    if (nBitCount == 32)
    {
        for( tools::Long y = 0; y < nHeight; ++y )
        {
            sal_uInt8 const *p = pData + (y * nStride);
            Scanline pScanline = pWrite->GetScanline(y);
            for (tools::Long x = 0; x < nWidth; ++x)
            {
                // FIXME this parameter is badly named
                const sal_uInt8 nAlphaValue = bReverseAlpha ? p[3] : 0xff - p[3];
                BitmapColor col;
                if ( bReversColors )
                    col = BitmapColor( ColorAlpha, p[2], p[1], p[0], nAlphaValue );
                else
                    col = BitmapColor( ColorAlpha, p[0], p[1], p[2], nAlphaValue );
                pWrite->SetPixelOnData(pScanline, x, col);
                p += 4;
            }
        }
    }
    else if (nBitCount == 1)
    {
        for( tools::Long y = 0; y < nHeight; ++y )
        {
            sal_uInt8 const *p = pData + y * nStride / 8;
            Scanline pScanline = pWrite->GetScanline(y);
            for (tools::Long x = 0; x < nWidth; ++x)
            {
                int bitIndex = (y * nStride + x) % 8;

                pWrite->SetPixelOnData(pScanline, x, BitmapColor((*p >> bitIndex) & 1));
            }
        }
    }
    else
    {
        for( tools::Long y = 0; y < nHeight; ++y )
        {
            sal_uInt8 const *p = pData + (y * nStride);
            Scanline pScanline = pWrite->GetScanline(y);
            for (tools::Long x = 0; x < nWidth; ++x)
            {
                BitmapColor col;
                if (nBitCount == 8)
                    col = BitmapColor( *p );
                else if ( bReversColors )
                    col = BitmapColor( p[2], p[1], p[0] );
                else
                    col = BitmapColor( p[0], p[1], p[2] );
                pWrite->SetPixelOnData(pScanline, x, col);
                p += nBitCount/8;
            }
        }
    }
    // Avoid further bitmap use with unfinished write access
    pWrite.reset();
    return aBmp;
}

/** Copy block of image data into the bitmap.
    Assumes that the Bitmap has been constructed with the desired size.
*/
Bitmap CreateFromData( RawBitmap&& rawBitmap )
{
    auto nBitCount = rawBitmap.GetBitCount();
    assert( nBitCount == 24 || nBitCount == 32);

    auto ePixelFormat = vcl::PixelFormat::INVALID;

    if (nBitCount == 24)
        ePixelFormat = vcl::PixelFormat::N24_BPP;
    else if (nBitCount == 32)
        ePixelFormat = vcl::PixelFormat::N32_BPP;

    assert(ePixelFormat != vcl::PixelFormat::INVALID);

    Bitmap aBmp(rawBitmap.maSize, ePixelFormat);

    BitmapScopedWriteAccess pWrite(aBmp);
    assert(pWrite.get());
    if( !pWrite )
        return Bitmap();

    auto nHeight = rawBitmap.maSize.getHeight();
    auto nWidth = rawBitmap.maSize.getWidth();
    auto nStride = nWidth * nBitCount / 8;
    if (nBitCount == 32)
    {
        for( tools::Long y = 0; y < nHeight; ++y )
        {
            sal_uInt8 const *p = rawBitmap.mpData.get() + (y * nStride);
            Scanline pScanline = pWrite->GetScanline(y);
            for (tools::Long x = 0; x < nWidth; ++x)
            {
                BitmapColor col(ColorAlpha, p[0], p[1], p[2], p[3]);
                pWrite->SetPixelOnData(pScanline, x, col);
                p += 4;
            }
        }
    }
    else
    {
        for( tools::Long y = 0; y < nHeight; ++y )
        {
            sal_uInt8 const *p = rawBitmap.mpData.get() + (y * nStride);
            Scanline pScanline = pWrite->GetScanline(y);
            for (tools::Long x = 0; x < nWidth; ++x)
            {
                BitmapColor col(p[0], p[1], p[2]);
                pWrite->SetPixelOnData(pScanline, x, col);
                p += nBitCount/8;
            }
        }
    }
    pWrite.reset();

    return aBmp;
}

void fillWithData(sal_uInt8* pData, BitmapEx const& rBitmapEx)
{
    const Bitmap& aBitmap = rBitmapEx.GetBitmap();
    const AlphaMask& aAlphaMask = rBitmapEx.GetAlphaMask();
    BitmapScopedReadAccess aReadAccessBitmap(aBitmap);
    BitmapScopedReadAccess aReadAccessAlpha(aAlphaMask);

    assert(!aReadAccessAlpha || aReadAccessBitmap->Height() == aReadAccessAlpha->Height());
    assert(!aReadAccessAlpha || aReadAccessBitmap->Width() == aReadAccessAlpha->Width());

    sal_uInt8* p = pData;

    for (tools::Long y = 0, nHeight = aReadAccessBitmap->Height(); y < nHeight; ++y)
    {
        Scanline dataBitmap = aReadAccessBitmap->GetScanline(y);
        Scanline dataAlpha = aReadAccessAlpha ? aReadAccessAlpha->GetScanline(y) : nullptr;

        for (tools::Long x = 0, nWidth = aReadAccessBitmap->Width(); x < nWidth; ++x)
        {
            BitmapColor aColor = aReadAccessBitmap->GetPixelFromData(dataBitmap, x);
            sal_uInt8 aAlpha = dataAlpha ? aReadAccessAlpha->GetPixelFromData(dataAlpha, x).GetBlue() : 255;
            *p++ = aColor.GetBlue();
            *p++ = aColor.GetGreen();
            *p++ = aColor.GetRed();
            *p++ = aAlpha;
        }
    }
}


#if ENABLE_CAIRO_CANVAS
BitmapEx* CreateFromCairoSurface(Size aSize, cairo_surface_t * pSurface)
{
    // FIXME: if we could teach VCL/ about cairo handles, life could
    // be significantly better here perhaps.

    cairo_surface_t *pPixels = cairo_surface_create_similar_image(pSurface,
            CAIRO_FORMAT_ARGB32, aSize.Width(), aSize.Height());
    cairo_t *pCairo = cairo_create( pPixels );
    if( !pPixels || !pCairo || cairo_status(pCairo) != CAIRO_STATUS_SUCCESS )
        return nullptr;

    // suck ourselves from the X server to this buffer so then we can fiddle with
    // Alpha to turn it into the ultra-lame vcl required format and then push it
    // all back again later at vast expense [ urgh ]
    cairo_set_source_surface( pCairo, pSurface, 0, 0 );
    cairo_set_operator( pCairo, CAIRO_OPERATOR_SOURCE );
    cairo_paint( pCairo );

    Bitmap aRGB(aSize, vcl::PixelFormat::N24_BPP);
    ::AlphaMask aMask( aSize );

    BitmapScopedWriteAccess pRGBWrite(aRGB);
    assert(pRGBWrite);
    if (!pRGBWrite)
        return nullptr;

    BitmapScopedWriteAccess pMaskWrite(aMask);
    assert(pMaskWrite);
    if (!pMaskWrite)
        return nullptr;

    cairo_surface_flush(pPixels);
    unsigned char *pSrc = cairo_image_surface_get_data( pPixels );
    unsigned int nStride = cairo_image_surface_get_stride( pPixels );
#if !ENABLE_WASM_STRIP_PREMULTIPLY
    vcl::bitmap::lookup_table const & unpremultiply_table = vcl::bitmap::get_unpremultiply_table();
#endif
    for( tools::Long y = 0; y < aSize.Height(); y++ )
    {
        sal_uInt32 *pPix = reinterpret_cast<sal_uInt32 *>(pSrc + nStride * y);
        for( tools::Long x = 0; x < aSize.Width(); x++ )
        {
#if defined OSL_BIGENDIAN
            sal_uInt8 nB = (*pPix >> 24);
            sal_uInt8 nG = (*pPix >> 16) & 0xff;
            sal_uInt8 nR = (*pPix >> 8) & 0xff;
            sal_uInt8 nAlpha = *pPix & 0xff;
#else
            sal_uInt8 nAlpha = (*pPix >> 24);
            sal_uInt8 nR = (*pPix >> 16) & 0xff;
            sal_uInt8 nG = (*pPix >> 8) & 0xff;
            sal_uInt8 nB = *pPix & 0xff;
#endif
            if( nAlpha != 0 && nAlpha != 255 )
            {
                // Cairo uses pre-multiplied alpha - we do not => re-multiply
#if ENABLE_WASM_STRIP_PREMULTIPLY
                nR = vcl::bitmap::unpremultiply(nR, nAlpha);
                nG = vcl::bitmap::unpremultiply(nG, nAlpha);
                nB = vcl::bitmap::unpremultiply(nB, nAlpha);
#else
                nR = unpremultiply_table[nAlpha][nR];
                nG = unpremultiply_table[nAlpha][nG];
                nB = unpremultiply_table[nAlpha][nB];
#endif
            }
            pRGBWrite->SetPixel( y, x, BitmapColor( nR, nG, nB ) );
            pMaskWrite->SetPixelIndex( y, x, nAlpha );
            pPix++;
        }
    }

    // ignore potential errors above. will get caller a
    // uniformly white bitmap, but not that there would
    // be error handling in calling code ...
    ::BitmapEx *pBitmapEx = new ::BitmapEx( aRGB, aMask );

    cairo_destroy( pCairo );
    cairo_surface_destroy( pPixels );
    return pBitmapEx;
}
#endif

BitmapEx CanvasTransformBitmap( const BitmapEx&                 rBitmap,
                                const ::basegfx::B2DHomMatrix&  rTransform,
                                ::basegfx::B2DRectangle const & rDestRect,
                                ::basegfx::B2DHomMatrix const & rLocalTransform )
{
    const Size aDestBmpSize( ::basegfx::fround<tools::Long>( rDestRect.getWidth() ),
                             ::basegfx::fround<tools::Long>( rDestRect.getHeight() ) );

    if( aDestBmpSize.IsEmpty() )
        return BitmapEx();

    const Size aBmpSize( rBitmap.GetSizePixel() );
    const Bitmap& aSrcBitmap( rBitmap.GetBitmap() );
    Bitmap aSrcAlpha;

    // differentiate mask and alpha channel (on-off
    // vs. multi-level transparency)
    if( rBitmap.IsAlpha() )
    {
        aSrcAlpha = rBitmap.GetAlphaMask().GetBitmap();
    }

    BitmapScopedReadAccess pReadAccess( aSrcBitmap );
    BitmapScopedReadAccess pAlphaReadAccess;
    if (rBitmap.IsAlpha())
        pAlphaReadAccess = aSrcAlpha;

    if( !pReadAccess || (!pAlphaReadAccess && rBitmap.IsAlpha()) )
    {
        // TODO(E2): Error handling!
        ENSURE_OR_THROW( false,
                          "transformBitmap(): could not access source bitmap" );
    }

    // mapping table, to translate pAlphaReadAccess' pixel
    // values into destination alpha values (needed e.g. for
    // paletted 1-bit masks).
    sal_uInt8 aAlphaMap[256];

    if( rBitmap.IsAlpha() )
    {
        // source already has alpha channel - 1:1 mapping,
        // i.e. aAlphaMap[0]=0,...,aAlphaMap[255]=255.
        sal_uInt8  val=0;
        sal_uInt8* pCur=aAlphaMap;
        sal_uInt8* const pEnd=&aAlphaMap[256];
        while(pCur != pEnd)
            *pCur++ = val++;
    }
    // else: mapping table is not used

    Bitmap aDstBitmap(aDestBmpSize, aSrcBitmap.getPixelFormat(), &pReadAccess->GetPalette());
    Bitmap aDstAlpha( AlphaMask( aDestBmpSize ).GetBitmap() );

    {
        // just to be on the safe side: let the
        // ScopedAccessors get destructed before
        // copy-constructing the resulting bitmap. This will
        // rule out the possibility that cached accessor data
        // is not yet written back.
        BitmapScopedWriteAccess pWriteAccess( aDstBitmap );
        BitmapScopedWriteAccess pAlphaWriteAccess( aDstAlpha );


        if( pWriteAccess.get() != nullptr &&
            pAlphaWriteAccess.get() != nullptr &&
            rTransform.isInvertible() )
        {
            // we're doing inverse mapping here, i.e. mapping
            // points from the destination bitmap back to the
            // source
            ::basegfx::B2DHomMatrix aTransform( rLocalTransform );
            aTransform.invert();

            // for the time being, always read as ARGB
            for( tools::Long y=0; y<aDestBmpSize.Height(); ++y )
            {
                // differentiate mask and alpha channel (on-off
                // vs. multi-level transparency)
                if( rBitmap.IsAlpha() )
                {
                    Scanline pScan = pWriteAccess->GetScanline( y );
                    Scanline pScanAlpha = pAlphaWriteAccess->GetScanline( y );
                    // Handling alpha and mask just the same...
                    for( tools::Long x=0; x<aDestBmpSize.Width(); ++x )
                    {
                        ::basegfx::B2DPoint aPoint(x,y);
                        aPoint *= aTransform;

                        const tools::Long nSrcX( ::basegfx::fround<tools::Long>( aPoint.getX() ) );
                        const tools::Long nSrcY( ::basegfx::fround<tools::Long>( aPoint.getY() ) );
                        if( nSrcX < 0 || nSrcX >= aBmpSize.Width() ||
                            nSrcY < 0 || nSrcY >= aBmpSize.Height() )
                        {
                            pAlphaWriteAccess->SetPixelOnData( pScanAlpha, x, BitmapColor(0) );
                        }
                        else
                        {
                            const sal_uInt8 cAlphaIdx = pAlphaReadAccess->GetPixelIndex( nSrcY, nSrcX );
                            pAlphaWriteAccess->SetPixelOnData( pScanAlpha, x, BitmapColor(aAlphaMap[ cAlphaIdx ]) );
                            pWriteAccess->SetPixelOnData( pScan, x, pReadAccess->GetPixel( nSrcY, nSrcX ) );
                        }
                    }
                }
                else
                {
                    Scanline pScan = pWriteAccess->GetScanline( y );
                    Scanline pScanAlpha = pAlphaWriteAccess->GetScanline( y );
                    for( tools::Long x=0; x<aDestBmpSize.Width(); ++x )
                    {
                        ::basegfx::B2DPoint aPoint(x,y);
                        aPoint *= aTransform;

                        const tools::Long nSrcX( ::basegfx::fround<tools::Long>( aPoint.getX() ) );
                        const tools::Long nSrcY( ::basegfx::fround<tools::Long>( aPoint.getY() ) );
                        if( nSrcX < 0 || nSrcX >= aBmpSize.Width() ||
                            nSrcY < 0 || nSrcY >= aBmpSize.Height() )
                        {
                            pAlphaWriteAccess->SetPixelOnData( pScanAlpha, x, BitmapColor(0) );
                        }
                        else
                        {
                            pAlphaWriteAccess->SetPixelOnData( pScanAlpha, x, BitmapColor(255) );
                            pWriteAccess->SetPixelOnData( pScan, x, pReadAccess->GetPixel( nSrcY,
                                                                                 nSrcX ) );
                        }
                    }
                }
            }
        }
        else
        {
            // TODO(E2): Error handling!
            ENSURE_OR_THROW( false,
                              "transformBitmap(): could not access bitmap" );
        }
    }

    return BitmapEx(aDstBitmap, AlphaMask(aDstAlpha));
}

void DrawAlphaBitmapAndAlphaGradient(BitmapEx & rBitmapEx, bool bFixedTransparence, float fTransparence, const AlphaMask & rNewMask)
{
    // mix existing and new alpha mask
    AlphaMask aOldMask;

    if(rBitmapEx.IsAlpha())
    {
        aOldMask = rBitmapEx.GetAlphaMask();
    }

    {

        BitmapScopedWriteAccess pOld(aOldMask);

        assert(pOld && "Got no access to old alpha mask (!)");

        const double fFactor(1.0 / 255.0);

        if(bFixedTransparence)
        {
            const double fOpNew(1.0 - fTransparence);

            for(tools::Long y(0),nHeight(pOld->Height()); y < nHeight; y++)
            {
                Scanline pScanline = pOld->GetScanline( y );
                for(tools::Long x(0),nWidth(pOld->Width()); x < nWidth; x++)
                {
                    const double fOpOld(pOld->GetIndexFromData(pScanline, x) * fFactor);
                    const sal_uInt8 aCol(basegfx::fround((fOpOld * fOpNew) * 255.0));

                    pOld->SetPixelOnData(pScanline, x, BitmapColor(aCol));
                }
            }
        }
        else
        {
            BitmapScopedReadAccess pNew(rNewMask);

            assert(pNew && "Got no access to new alpha mask (!)");

            assert(pOld->Width() == pNew->Width() && pOld->Height() == pNew->Height() &&
                    "Alpha masks have different sizes (!)");

            for(tools::Long y(0),nHeight(pOld->Height()); y < nHeight; y++)
            {
                Scanline pScanline = pOld->GetScanline( y );
                for(tools::Long x(0),nWidth(pOld->Width()); x < nWidth; x++)
                {
                    const double fOpOld(pOld->GetIndexFromData(pScanline, x) * fFactor);
                    const double fOpNew(pNew->GetIndexFromData(pScanline, x) * fFactor);
                    const sal_uInt8 aCol(basegfx::fround((fOpOld * fOpNew) * 255.0));

                    pOld->SetPixelOnData(pScanline, x, BitmapColor(aCol));
                }
            }
        }

    }

    // apply combined bitmap as mask
    rBitmapEx = BitmapEx(rBitmapEx.GetBitmap(), aOldMask);
}


void DrawAndClipBitmap(const Point& rPos, const Size& rSize, const BitmapEx& rBitmap, BitmapEx & aBmpEx, basegfx::B2DPolyPolygon const & rClipPath)
{
    ScopedVclPtrInstance< VirtualDevice > pVDev;
    MapMode aMapMode( MapUnit::Map100thMM );
    aMapMode.SetOrigin( Point( -rPos.X(), -rPos.Y() ) );
    const Size aOutputSizePixel( pVDev->LogicToPixel( rSize, aMapMode ) );
    const Size aSizePixel( rBitmap.GetSizePixel() );
    if ( aOutputSizePixel.Width() && aOutputSizePixel.Height() )
    {
        aMapMode.SetScaleX( Fraction( aSizePixel.Width(), aOutputSizePixel.Width() ) );
        aMapMode.SetScaleY( Fraction( aSizePixel.Height(), aOutputSizePixel.Height() ) );
    }
    pVDev->SetMapMode( aMapMode );
    pVDev->SetOutputSizePixel( aSizePixel );
    pVDev->SetFillColor( COL_BLACK );
    const tools::PolyPolygon aClip( rClipPath );
    pVDev->DrawPolyPolygon( aClip );

    // #i50672# Extract whole VDev content (to match size of rBitmap)
    pVDev->EnableMapMode( false );
    const Bitmap aVDevMask(pVDev->GetBitmap(Point(), aSizePixel));

    if(aBmpEx.IsAlpha())
    {
        // bitmap already uses a Mask or Alpha, we need to blend that with
        // the new masking in pVDev.
        // need to blend in AlphaMask quality (8Bit)
        AlphaMask fromVDev(aVDevMask);
        AlphaMask fromBmpEx(aBmpEx.GetAlphaMask());
        BitmapScopedReadAccess pR(fromVDev);
        BitmapScopedWriteAccess pW(fromBmpEx);

        if(pR && pW)
        {
            const tools::Long nWidth(std::min(pR->Width(), pW->Width()));
            const tools::Long nHeight(std::min(pR->Height(), pW->Height()));

            for(tools::Long nY(0); nY < nHeight; nY++)
            {
                Scanline pScanlineR = pR->GetScanline( nY );
                Scanline pScanlineW = pW->GetScanline( nY );
                for(tools::Long nX(0); nX < nWidth; nX++)
                {
                    const sal_uInt8 nIndR(pR->GetIndexFromData(pScanlineR, nX));
                    const sal_uInt8 nIndW(pW->GetIndexFromData(pScanlineW, nX));

                    // these values represent alpha (255 == no, 0 == fully transparent),
                    // so to blend these we have to multiply
                    const sal_uInt8 nCombined((nIndR * nIndW) >> 8);

                    pW->SetPixelOnData(pScanlineW, nX, BitmapColor(nCombined));
                }
            }
        }

        pR.reset();
        pW.reset();
        aBmpEx = BitmapEx(aBmpEx.GetBitmap(), fromBmpEx);
    }
    else
    {
        // no mask yet, create and add new mask. For better quality, use Alpha,
        // this allows the drawn mask being processed with AntiAliasing (AAed)
        aBmpEx = BitmapEx(rBitmap.GetBitmap(), aVDevMask);
    }
}

css::uno::Sequence< sal_Int8 > GetMaskDIB(BitmapEx const & aBmpEx)
{
    if ( aBmpEx.IsAlpha() )
    {
        SvMemoryStream aMem;
        AlphaMask aMask = aBmpEx.GetAlphaMask();
        // for backwards compatibility for extensions, we need to convert from alpha to transparency
        aMask.Invert();
        WriteDIB(aMask.GetBitmap(), aMem, false, true);
        return css::uno::Sequence< sal_Int8 >( static_cast<sal_Int8 const *>(aMem.GetData()), aMem.Tell() );
    }

    return css::uno::Sequence< sal_Int8 >();
}

static bool readAlpha( BitmapReadAccess const * pAlphaReadAcc, tools::Long nY, const tools::Long nWidth, unsigned char* data, tools::Long nOff )
{
    bool bIsAlpha = false;
    tools::Long nX;
    int nAlpha;
    Scanline pReadScan;

    nOff += 3;

    switch( pAlphaReadAcc->GetScanlineFormat() )
    {
        case ScanlineFormat::N8BitPal:
            pReadScan = pAlphaReadAcc->GetScanline( nY );
            for( nX = 0; nX < nWidth; nX++ )
            {
                BitmapColor const& rColor(
                    pAlphaReadAcc->GetPaletteColor(*pReadScan));
                pReadScan++;
                nAlpha = data[ nOff ] = rColor.GetIndex();
                if( nAlpha != 255 )
                    bIsAlpha = true;
                nOff += 4;
            }
            break;
        default:
            SAL_INFO( "canvas.cairo", "fallback to GetColor for alpha - slow, format: " << static_cast<int>(pAlphaReadAcc->GetScanlineFormat()) );
            for( nX = 0; nX < nWidth; nX++ )
            {
                nAlpha = data[ nOff ] = pAlphaReadAcc->GetColor( nY, nX ).GetIndex();
                if( nAlpha != 255 )
                    bIsAlpha = true;
                nOff += 4;
            }
    }

    return bIsAlpha;
}



/**
 * @param data will be filled with alpha data, if xBitmap is alpha/transparent image
 * @param bHasAlpha will be set to true if resulting surface has alpha
 **/
void CanvasCairoExtractBitmapData( BitmapEx const & aBmpEx, const Bitmap & aBitmap, unsigned char*& data, bool& bHasAlpha, tools::Long& rnWidth, tools::Long& rnHeight )
{
    const AlphaMask& aAlpha = aBmpEx.GetAlphaMask();

    BitmapScopedReadAccess pBitmapReadAcc( aBitmap );
    BitmapScopedReadAccess pAlphaReadAcc;
    const tools::Long      nWidth = rnWidth = pBitmapReadAcc->Width();
    const tools::Long      nHeight = rnHeight = pBitmapReadAcc->Height();
    tools::Long nX, nY;
    bool bIsAlpha = false;

    if( aBmpEx.IsAlpha() )
        pAlphaReadAcc = aAlpha;

    data = static_cast<unsigned char*>(malloc( nWidth*nHeight*4 ));

    tools::Long nOff = 0;
    ::Color aColor;
    unsigned int nAlpha = 255;

#if !ENABLE_WASM_STRIP_PREMULTIPLY
    vcl::bitmap::lookup_table const & premultiply_table = vcl::bitmap::get_premultiply_table();
#endif
    for( nY = 0; nY < nHeight; nY++ )
    {
        ::Scanline pReadScan;

        switch( pBitmapReadAcc->GetScanlineFormat() )
        {
        case ScanlineFormat::N8BitPal:
            pReadScan = pBitmapReadAcc->GetScanline( nY );
            if( pAlphaReadAcc )
                if( readAlpha( pAlphaReadAcc.get(), nY, nWidth, data, nOff ) )
                    bIsAlpha = true;

            for( nX = 0; nX < nWidth; nX++ )
            {
#ifdef OSL_BIGENDIAN
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff++ ];
                else
                    nAlpha = data[ nOff++ ] = 255;
#else
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff + 3 ];
                else
                    nAlpha = data[ nOff + 3 ] = 255;
#endif
                aColor = pBitmapReadAcc->GetPaletteColor(*pReadScan++);

#ifdef OSL_BIGENDIAN
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetRed(), nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetGreen(), nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetBlue(), nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetRed()];
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetGreen()];
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetBlue()];
#endif
#else
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetBlue(), nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetGreen(), nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetRed(), nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetBlue()];
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetGreen()];
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetRed()];
#endif
                nOff++;
#endif
            }
            break;
        case ScanlineFormat::N24BitTcBgr:
            pReadScan = pBitmapReadAcc->GetScanline( nY );
            if( pAlphaReadAcc )
                if( readAlpha( pAlphaReadAcc.get(), nY, nWidth, data, nOff ) )
                    bIsAlpha = true;

            for( nX = 0; nX < nWidth; nX++ )
            {
#ifdef OSL_BIGENDIAN
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff ];
                else
                    nAlpha = data[ nOff ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff + 3 ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff + 2 ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff + 1 ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
#else
                data[ nOff + 3 ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff + 2 ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff + 1 ] = premultiply_table[nAlpha][*pReadScan++];
#endif
                nOff += 4;
#else
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff + 3 ];
                else
                    nAlpha = data[ nOff + 3 ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
#endif
                nOff++;
#endif
            }
            break;
        case ScanlineFormat::N24BitTcRgb:
            pReadScan = pBitmapReadAcc->GetScanline( nY );
            if( pAlphaReadAcc )
                if( readAlpha( pAlphaReadAcc.get(), nY, nWidth, data, nOff ) )
                    bIsAlpha = true;

            for( nX = 0; nX < nWidth; nX++ )
            {
#ifdef OSL_BIGENDIAN
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff++ ];
                else
                    nAlpha = data[ nOff++ ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
#endif
#else
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff + 3 ];
                else
                    nAlpha = data[ nOff + 3 ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 2 ], nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 1 ], nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 0 ], nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 2 ]];
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 1 ]];
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 0 ]];
#endif
                pReadScan += 3;
                nOff++;
#endif
            }
            break;
        case ScanlineFormat::N32BitTcBgra:
        case ScanlineFormat::N32BitTcBgrx:
            pReadScan = pBitmapReadAcc->GetScanline( nY );
            if( pAlphaReadAcc )
                if( readAlpha( pAlphaReadAcc.get(), nY, nWidth, data, nOff ) )
                    bIsAlpha = true;

            for( nX = 0; nX < nWidth; nX++ )
            {
#ifdef OSL_BIGENDIAN
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff++ ];
                else
                    nAlpha = data[ nOff++ ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 2 ], nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 1 ], nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 0 ], nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 2 ]];
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 1 ]];
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 0 ]];
#endif
                pReadScan += 4;
#else
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff + 3 ];
                else
                    nAlpha = data[ nOff + 3 ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
#endif
                pReadScan++;
                nOff++;
#endif
            }
            break;
        case ScanlineFormat::N32BitTcRgba:
        case ScanlineFormat::N32BitTcRgbx:
            pReadScan = pBitmapReadAcc->GetScanline( nY );
            if( pAlphaReadAcc )
                if( readAlpha( pAlphaReadAcc.get(), nY, nWidth, data, nOff ) )
                    bIsAlpha = true;

            for( nX = 0; nX < nWidth; nX++ )
            {
#ifdef OSL_BIGENDIAN
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff ++ ];
                else
                    nAlpha = data[ nOff ++ ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(*pReadScan++, nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
                data[ nOff++ ] = premultiply_table[nAlpha][*pReadScan++];
#endif
                pReadScan++;
#else
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff + 3 ];
                else
                    nAlpha = data[ nOff + 3 ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 2 ], nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 1 ], nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(pReadScan[ 0 ], nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 2 ]];
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 1 ]];
                data[ nOff++ ] = premultiply_table[nAlpha][pReadScan[ 0 ]];
#endif
                pReadScan += 4;
                nOff++;
#endif
            }
            break;
        default:
            SAL_INFO( "canvas.cairo", "fallback to GetColor - slow, format: " << static_cast<int>(pBitmapReadAcc->GetScanlineFormat()) );

            if( pAlphaReadAcc )
                if( readAlpha( pAlphaReadAcc.get(), nY, nWidth, data, nOff ) )
                    bIsAlpha = true;

            for( nX = 0; nX < nWidth; nX++ )
            {
                aColor = pBitmapReadAcc->GetColor( nY, nX );

                // cairo need premultiplied color values
                // TODO(rodo) handle endianness
#ifdef OSL_BIGENDIAN
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff++ ];
                else
                    nAlpha = data[ nOff++ ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetRed(), nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetGreen(), nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetBlue(), nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetRed()];
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetGreen()];
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetBlue()];
#endif
#else
                if( pAlphaReadAcc )
                    nAlpha = data[ nOff + 3 ];
                else
                    nAlpha = data[ nOff + 3 ] = 255;
#if ENABLE_WASM_STRIP_PREMULTIPLY
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetBlue(), nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetGreen(), nAlpha);
                data[ nOff++ ] = vcl::bitmap::premultiply(aColor.GetRed(), nAlpha);
#else
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetBlue()];
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetGreen()];
                data[ nOff++ ] = premultiply_table[nAlpha][aColor.GetRed()];
#endif
                nOff ++;
#endif
            }
        }
    }

    bHasAlpha = bIsAlpha;

}

    uno::Sequence< sal_Int8 > CanvasExtractBitmapData(BitmapEx const & rBitmapEx, const geometry::IntegerRectangle2D& rect)
    {
        const Bitmap& aBitmap( rBitmapEx.GetBitmap() );
        Bitmap aAlpha( rBitmapEx.GetAlphaMask().GetBitmap() );

        BitmapScopedReadAccess pReadAccess( aBitmap );
        BitmapScopedReadAccess pAlphaReadAccess;
        if (!aAlpha.IsEmpty())
            pAlphaReadAccess = aAlpha;

        assert( pReadAccess );

        // TODO(F1): Support more formats.
        const Size aBmpSize( aBitmap.GetSizePixel() );

        // for the time being, always return as BGRA
        uno::Sequence< sal_Int8 > aRes( 4*aBmpSize.Width()*aBmpSize.Height() );
        sal_Int8* pRes = aRes.getArray();

        int nCurrPos(0);
        for( tools::Long y=rect.Y1;
             y<aBmpSize.Height() && y<rect.Y2;
             ++y )
        {
            if( pAlphaReadAccess.get() != nullptr )
            {
                Scanline pScanlineReadAlpha = pAlphaReadAccess->GetScanline( y );
                for( tools::Long x=rect.X1;
                     x<aBmpSize.Width() && x<rect.X2;
                     ++x )
                {
                    pRes[ nCurrPos++ ] = pReadAccess->GetColor( y, x ).GetRed();
                    pRes[ nCurrPos++ ] = pReadAccess->GetColor( y, x ).GetGreen();
                    pRes[ nCurrPos++ ] = pReadAccess->GetColor( y, x ).GetBlue();
                    pRes[ nCurrPos++ ] = 255 - pAlphaReadAccess->GetIndexFromData( pScanlineReadAlpha, x );
                }
            }
            else
            {
                for( tools::Long x=rect.X1;
                     x<aBmpSize.Width() && x<rect.X2;
                     ++x )
                {
                    pRes[ nCurrPos++ ] = pReadAccess->GetColor( y, x ).GetRed();
                    pRes[ nCurrPos++ ] = pReadAccess->GetColor( y, x ).GetGreen();
                    pRes[ nCurrPos++ ] = pReadAccess->GetColor( y, x ).GetBlue();
                    pRes[ nCurrPos++ ] = sal_uInt8(255);
                }
            }
        }
        return aRes;
    }

    Bitmap createHistorical8x8FromArray(std::array<sal_uInt8,64> const & pArray, Color aColorPix, Color aColorBack)
    {
        BitmapPalette aPalette(2);

        aPalette[0] = BitmapColor(aColorBack);
        aPalette[1] = BitmapColor(aColorPix);

        Bitmap aBitmap(Size(8, 8), vcl::PixelFormat::N8_BPP, &aPalette);
        BitmapScopedWriteAccess pContent(aBitmap);

        for(sal_uInt16 a(0); a < 8; a++)
        {
            for(sal_uInt16 b(0); b < 8; b++)
            {
                if(pArray[(a * 8) + b])
                {
                    pContent->SetPixelIndex(a, b, 1);
                }
                else
                {
                    pContent->SetPixelIndex(a, b, 0);
                }
            }
        }

        return aBitmap;
    }

    bool isHistorical8x8(const Bitmap& rBitmap, Color& o_rBack, Color& o_rFront)
    {
        bool bRet(false);

        if(!rBitmap.HasAlpha())
        {
            if(8 == rBitmap.GetSizePixel().Width() && 8 == rBitmap.GetSizePixel().Height())
            {
                // Historical 1bpp images are getting really historical,
                // even to the point that e.g. the png loader actually loads
                // them as RGB. But the pattern code in svx relies on this
                // assumption that any 2-color 1bpp bitmap is a pattern, and so it would
                // get confused by RGB. Try to detect if this image is really
                // just two colors and say it's a pattern bitmap if so.
                BitmapScopedReadAccess access(rBitmap);
                o_rBack = access->GetColor(0,0);
                bool foundSecondColor = false;;
                for(tools::Long y = 0, nHeight = access->Height(); y < nHeight; ++y)
                    for(tools::Long x = 0, nWidth = access->Width(); x < nWidth; ++x)
                    {
                        if(!foundSecondColor)
                        {
                            if( access->GetColor(y,x) != o_rBack )
                            {
                                o_rFront = access->GetColor(y,x);
                                foundSecondColor = true;
                                // Hard to know which of the two colors is the background,
                                // select the lighter one.
                                if( o_rFront.GetLuminance() > o_rBack.GetLuminance())
                                    std::swap( o_rFront, o_rBack );
                            }
                        }
                        else
                        {
                            if( access->GetColor(y,x) != o_rBack && access->GetColor(y,x) != o_rFront)
                                return false;
                        }
                    }
                return true;
            }
        }

        return bRet;
    }

#if ENABLE_WASM_STRIP_PREMULTIPLY
    sal_uInt8 unpremultiply(sal_uInt8 c, sal_uInt8 a)
    {
        return (a == 0) ? 0 : (c * 255 + a / 2) / a;
    }

    sal_uInt8 premultiply(sal_uInt8 c, sal_uInt8 a)
    {
        return (c * a + 127) / 255;
    }
#else
    sal_uInt8 unpremultiply(sal_uInt8 c, sal_uInt8 a)
    {
        return get_unpremultiply_table()[a][c];
    }

    static constexpr sal_uInt8 unpremultiplyImpl(sal_uInt8 c, sal_uInt8 a)
    {
        return (a == 0) ? 0 : (c * 255 + a / 2) / a;
    }

    sal_uInt8 premultiply(sal_uInt8 c, sal_uInt8 a)
    {
        return get_premultiply_table()[a][c];
    }

    static constexpr sal_uInt8 premultiplyImpl(sal_uInt8 c, sal_uInt8 a)
    {
        return (c * a + 127) / 255;
    }

    template<int... Is> static constexpr std::array<sal_uInt8, 256> make_unpremultiply_table_row_(
        int a, std::integer_sequence<int, Is...>)
    {
        return {unpremultiplyImpl(Is, a)...};
    }

    template<int... Is> static constexpr lookup_table make_unpremultiply_table_(
        std::integer_sequence<int, Is...>)
    {
        return {make_unpremultiply_table_row_(Is, std::make_integer_sequence<int, 256>{})...};
    }

    lookup_table const & get_unpremultiply_table()
    {
        static constexpr auto unpremultiply_table = make_unpremultiply_table_(
            std::make_integer_sequence<int, 256>{});
        return unpremultiply_table;
    }

    template<int... Is> static constexpr std::array<sal_uInt8, 256> make_premultiply_table_row_(
        int a, std::integer_sequence<int, Is...>)
    {
        return {premultiplyImpl(Is, a)...};
    }

    template<int... Is> static constexpr lookup_table make_premultiply_table_(
        std::integer_sequence<int, Is...>)
    {
        return {make_premultiply_table_row_(Is, std::make_integer_sequence<int, 256>{})...};
    }

    lookup_table const & get_premultiply_table()
    {
        static constexpr auto premultiply_table = make_premultiply_table_(
            std::make_integer_sequence<int, 256>{});
        return premultiply_table;
    }
#endif

bool convertBitmap32To24Plus8(BitmapEx const & rInput, BitmapEx & rResult)
{
    const Bitmap& aBitmap(rInput.GetBitmap());
    if (aBitmap.getPixelFormat() != vcl::PixelFormat::N32_BPP)
        return false;

    Size aSize = aBitmap.GetSizePixel();
    Bitmap aResultBitmap(aSize, vcl::PixelFormat::N24_BPP);
    AlphaMask aResultAlpha(aSize);
    {
        BitmapScopedWriteAccess pResultBitmapAccess(aResultBitmap);
        BitmapScopedWriteAccess pResultAlphaAccess(aResultAlpha);

        BitmapScopedReadAccess pReadAccess(aBitmap);

        for (tools::Long nY = 0; nY < aSize.Height(); ++nY)
        {
            Scanline aResultScan = pResultBitmapAccess->GetScanline(nY);
            Scanline aResultScanAlpha = pResultAlphaAccess->GetScanline(nY);

            Scanline aReadScan = pReadAccess->GetScanline(nY);

            for (tools::Long nX = 0; nX < aSize.Width(); ++nX)
            {
                const BitmapColor aColor = pReadAccess->GetPixelFromData(aReadScan, nX);
                BitmapColor aResultColor(aColor.GetRed(), aColor.GetGreen(), aColor.GetBlue());
                BitmapColor aResultColorAlpha(aColor.GetAlpha(), aColor.GetAlpha(), aColor.GetAlpha());

                pResultBitmapAccess->SetPixelOnData(aResultScan, nX, aResultColor);
                pResultAlphaAccess->SetPixelOnData(aResultScanAlpha, nX, aResultColorAlpha);
            }
        }
    }
    if (rInput.IsAlpha())
        rResult = BitmapEx(aResultBitmap, rInput.GetAlphaMask());
    else
        rResult = BitmapEx(aResultBitmap, aResultAlpha);
    return true;
}

Bitmap GetDownsampledBitmap(Size const& rDstSizeTwip, Point const& rSrcPt, Size const& rSrcSz,
                            Bitmap const& rBmp, tools::Long nMaxBmpDPIX, tools::Long nMaxBmpDPIY)
{
    Bitmap aBmp(rBmp);

    if (!aBmp.IsEmpty())
    {
        const tools::Rectangle aBmpRect( Point(), aBmp.GetSizePixel() );
        tools::Rectangle       aSrcRect( rSrcPt, rSrcSz );

        // do cropping if necessary
        if( aSrcRect.Intersection( aBmpRect ) != aBmpRect )
        {
            if( !aSrcRect.IsEmpty() )
                aBmp.Crop( aSrcRect );
            else
                aBmp.SetEmpty();
        }

        if( !aBmp.IsEmpty() )
        {
            // do downsampling if necessary
            // #103209# Normalize size (mirroring has to happen outside of this method)
            Size aDstSizeTwip(std::abs(rDstSizeTwip.Width()), std::abs(rDstSizeTwip.Height()));

            const Size aBmpSize( aBmp.GetSizePixel() );
            const double fBmpPixelX = aBmpSize.Width();
            const double fBmpPixelY = aBmpSize.Height();
            const double fMaxPixelX
                = o3tl::convert<double>(aDstSizeTwip.Width(), o3tl::Length::twip, o3tl::Length::in)
                  * nMaxBmpDPIX;
            const double fMaxPixelY
                = o3tl::convert<double>(aDstSizeTwip.Height(), o3tl::Length::twip, o3tl::Length::in)
                  * nMaxBmpDPIY;

            // check, if the bitmap DPI exceeds the maximum DPI (allow 4 pixel rounding tolerance)
            if (((fBmpPixelX > (fMaxPixelX + 4)) ||
                  (fBmpPixelY > (fMaxPixelY + 4))) &&
                (fBmpPixelY > 0.0) && (fMaxPixelY > 0.0))
            {
                // do scaling
                Size aNewBmpSize;
                const double fBmpWH = fBmpPixelX / fBmpPixelY;
                const double fMaxWH = fMaxPixelX / fMaxPixelY;

                if (fBmpWH < fMaxWH)
                {
                    aNewBmpSize.setWidth(basegfx::fround<tools::Long>(fMaxPixelY * fBmpWH));
                    aNewBmpSize.setHeight(basegfx::fround<tools::Long>(fMaxPixelY));
                }
                else if (fBmpWH > 0.0)
                {
                    aNewBmpSize.setWidth(basegfx::fround<tools::Long>(fMaxPixelX));
                    aNewBmpSize.setHeight(basegfx::fround<tools::Long>(fMaxPixelX / fBmpWH));
                }

                if( aNewBmpSize.Width() && aNewBmpSize.Height() )
                    aBmp.Scale(aNewBmpSize);
                else
                    aBmp.SetEmpty();
            }
        }
    }

    return aBmp;
}

BitmapColor premultiply(const BitmapColor c)
{
    return BitmapColor(ColorAlpha, premultiply(c.GetRed(), c.GetAlpha()),
                       premultiply(c.GetGreen(), c.GetAlpha()),
                       premultiply(c.GetBlue(), c.GetAlpha()), c.GetAlpha());
}

BitmapColor unpremultiply(const BitmapColor c)
{
    return BitmapColor(ColorAlpha, unpremultiply(c.GetRed(), c.GetAlpha()),
                       unpremultiply(c.GetGreen(), c.GetAlpha()),
                       unpremultiply(c.GetBlue(), c.GetAlpha()), c.GetAlpha());
}

} // end vcl::bitmap

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
