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

#ifndef INCLUDED_VCL_GDIMTF_HXX
#define INCLUDED_VCL_GDIMTF_HXX

#include <vcl/dllapi.h>
#include <tools/gen.hxx>
#include <tools/solar.h>
#include <vcl/mapmod.hxx>
#include <vcl/bitmap.hxx>
#include <vcl/vclptr.hxx>
#include <vector>

class OutputDevice;
class MetaAction;
class Color;
class BitmapEx;
namespace tools {
    class Polygon;
    class PolyPolygon;
}
class Gradient;

#define GDI_METAFILE_END                (size_t(0xFFFFFFFF))

enum class MtfConversion
{
    N1BitThreshold,
    N8BitGreys
};


typedef Color (*ColorExchangeFnc)( const Color& rColor, const void* pColParam );
typedef BitmapEx (*BmpExchangeFnc)( const BitmapEx& rBmpEx, const void* pBmpParam );

class VCL_DLLPUBLIC GDIMetaFile final
{
private:
    ::std::vector< rtl::Reference<MetaAction> > m_aList;
    size_t          m_nCurrentActionElement;

    MapMode         m_aPrefMapMode;
    Size            m_aPrefSize;
    GDIMetaFile*    m_pPrev;
    GDIMetaFile*    m_pNext;
    VclPtr<OutputDevice> m_pOutDev;
    bool            m_bPause;
    bool            m_bRecord;
    bool            m_bUseCanvas;

    // tdf#155479 need to know if it's SVG export
    bool            m_bSVG;


    SAL_DLLPRIVATE static Color         ImplColAdjustFnc( const Color& rColor, const void* pColParam );
    SAL_DLLPRIVATE static BitmapEx      ImplBmpAdjustFnc( const BitmapEx& rBmpEx, const void* pBmpParam );

    SAL_DLLPRIVATE static Color         ImplColConvertFnc( const Color& rColor, const void* pColParam );
    SAL_DLLPRIVATE static BitmapEx      ImplBmpConvertFnc( const BitmapEx& rBmpEx, const void* pBmpParam );

    SAL_DLLPRIVATE static Color         ImplColMonoFnc( const Color& rColor, const void* pColParam );
    SAL_DLLPRIVATE static BitmapEx      ImplBmpMonoFnc( const BitmapEx& rBmpEx, const void* pBmpParam );

    SAL_DLLPRIVATE static Color         ImplColReplaceFnc( const Color& rColor, const void* pColParam );
    SAL_DLLPRIVATE static BitmapEx      ImplBmpReplaceFnc( const BitmapEx& rBmpEx, const void* pBmpParam );

    SAL_DLLPRIVATE void                 ImplExchangeColors( ColorExchangeFnc pFncCol, const void* pColParam,
                                                            BmpExchangeFnc pFncBmp, const void* pBmpParam );

    SAL_DLLPRIVATE static Point         ImplGetRotatedPoint( const Point& rPt, const Point& rRotatePt,
                                                             const Size& rOffset, double fSin, double fCos );
    SAL_DLLPRIVATE static tools::Polygon ImplGetRotatedPolygon( const tools::Polygon& rPoly, const Point& rRotatePt,
                                                               const Size& rOffset, double fSin, double fCos );
    SAL_DLLPRIVATE static tools::PolyPolygon ImplGetRotatedPolyPolygon( const tools::PolyPolygon& rPoly, const Point& rRotatePt,
                                                                   const Size& rOffset, double fSin, double fCos );
    SAL_DLLPRIVATE static void          ImplAddGradientEx( GDIMetaFile& rMtf,
                                                           const OutputDevice& rMapDev,
                                                           const tools::PolyPolygon& rPolyPoly,
                                                           const Gradient& rGrad );

    SAL_DLLPRIVATE bool                 ImplPlayWithRenderer(OutputDevice& rOut, const Point& rPos, Size rLogicDestSize);

    SAL_DLLPRIVATE void                 Linker( OutputDevice* pOut, bool bLink );

public:
                    GDIMetaFile();
                    GDIMetaFile( const GDIMetaFile& rMtf );
                    ~GDIMetaFile();

    GDIMetaFile&    operator=( const GDIMetaFile& rMtf );
    bool            operator==( const GDIMetaFile& rMtf ) const;
    bool            operator!=( const GDIMetaFile& rMtf ) const { return !( *this == rMtf ); }

    void            Clear();
    SAL_DLLPRIVATE void Mirror( BmpMirrorFlags nMirrorFlags );
    void            Move( tools::Long nX, tools::Long nY );
    // additional Move method getting specifics how to handle MapMode( MapUnit::MapPixel )
    void            Move( tools::Long nX, tools::Long nY, tools::Long nDPIX, tools::Long nDPIY );
    void            ScaleActions(double fScaleX, double fScaleY);
    void            Scale( double fScaleX, double fScaleY );
    void            Scale( const Fraction& rScaleX, const Fraction& rScaleY );
    SAL_DLLPRIVATE void Rotate( Degree10 nAngle10 );
    void            Clip( const tools::Rectangle& );
    bool            HasTransparentActions() const;

    /* get the bound rect of the contained actions
     * caveats:
     * - clip actions will limit the contained actions,
     *   but the current clipregion of the passed OutputDevice will not
     * - coordinates of actions will be transformed to preferred mapmode
     * - the returned rectangle is relative to the preferred mapmode of the metafile
    */
    tools::Rectangle       GetBoundRect( OutputDevice& i_rReference ) const;

    void            Adjust( short nLuminancePercent, short nContrastPercent,
                            short nChannelRPercent = 0,  short nChannelGPercent = 0,
                            short nChannelBPercent = 0,  double fGamma = 1.0,
                            bool bInvert = false, bool msoBrightness = false );

    void            Convert( MtfConversion eConversion );
    void            ReplaceColors( const Color* pSearchColors, const Color* rReplaceColors,
                                   sal_uLong nColorCount );

    GDIMetaFile     GetMonochromeMtf( const Color& rCol ) const;

    void            Record( OutputDevice* pOutDev );
    bool            IsRecord() const { return m_bRecord; }

    void            Play(GDIMetaFile& rMtf);
    void            Play(OutputDevice& rOutDev, size_t nPos = GDI_METAFILE_END);
    void            Play(OutputDevice& rOutDev, const Point& rPos, const Size& rSize);

    void            Pause( bool bPause );
    bool            IsPause() const { return m_bPause; }

    void            Stop();

    void            WindStart();
    void            WindPrev();

    size_t          GetActionSize() const;

    void            AddAction(const rtl::Reference<MetaAction>& pAction);
    void            AddAction(const rtl::Reference<MetaAction>& pAction, size_t nPos);
    SAL_DLLPRIVATE void push_back(const rtl::Reference<MetaAction>& pAction);
    /**
     * @param nAction the action to replace
     */
    SAL_DLLPRIVATE void ReplaceAction( rtl::Reference<MetaAction> pAction, size_t nAction );

    MetaAction*     FirstAction();
    MetaAction*     NextAction();
    MetaAction*     GetAction( size_t nAction ) const;
    MetaAction*     GetCurAction() const { return GetAction( m_nCurrentActionElement ); }

    const Size&     GetPrefSize() const { return m_aPrefSize; }
    void            SetPrefSize( const Size& rSize ) { m_aPrefSize = rSize; }

    const MapMode&  GetPrefMapMode() const { return m_aPrefMapMode; }
    void            SetPrefMapMode( const MapMode& rMapMode ) { m_aPrefMapMode = rMapMode; }


    SAL_DLLPRIVATE sal_uLong GetSizeBytes() const;

    /// Creates an antialiased thumbnail
    bool            CreateThumbnail(Bitmap& rBitmapEx,
                                    BmpConversion nColorConversion = BmpConversion::N24Bit,
                                    BmpScaleFlag nScaleFlag = BmpScaleFlag::BestQuality) const;

    void            UseCanvas( bool _bUseCanvas );
    bool            GetUseCanvas() const { return m_bUseCanvas; }

    // tdf#155479
    bool getSVG() const { return m_bSVG; }
    void setSVG(bool bNew) { m_bSVG = bNew; }

    /// Dumps the meta actions as XML in metafile.xml.
    void dumpAsXml(const char* pFileName = nullptr) const;
};

#endif // INCLUDED_VCL_GDIMTF_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
