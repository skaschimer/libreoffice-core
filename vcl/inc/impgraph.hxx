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

#pragma once

#include <vcl/dllapi.h>
#include <vcl/GraphicExternalLink.hxx>
#include <vcl/gdimtf.hxx>
#include <vcl/graph.hxx>
#include "graphic/Manager.hxx"
#include "graphic/MemoryManaged.hxx"
#include "graphic/GraphicID.hxx"
#include "graphic/BitmapContainer.hxx"
#include "graphic/AnimationContainer.hxx"
#include "graphic/SwapInfo.hxx"
#include <optional>

class OutputDevice;
class GfxLink;
class ImpSwapFile;

enum class GraphicContentType : sal_Int32
{
    Bitmap,
    Animation,
    Vector
};

class SAL_DLLPUBLIC_RTTI ImpGraphic final : public vcl::graphic::MemoryManaged
{
    friend class Graphic;
    friend class GraphicID;

private:
    BitmapEx maCachedBitmap;
    GDIMetaFile maMetaFile;
    std::shared_ptr<BitmapContainer> mpBitmapContainer;

    /// If maBitmapEx is empty, this preferred size will be set on it when it gets initialized.
    Size                         maExPrefSize;
    SwapInfo maSwapInfo;
    std::shared_ptr<AnimationContainer> mpAnimationContainer;
    std::shared_ptr<ImpSwapFile> mpSwapFile;
    std::shared_ptr<GfxLink>     mpGfxLink;
    std::shared_ptr<VectorGraphicData> maVectorGraphicData;

    GraphicType                  meType = GraphicType::NONE;
    mutable sal_Int64 mnSizeBytes = 0;
    bool                         mbSwapOut = false;
    bool                         mbDummyContext = false;
    // cache checksum computation
    mutable BitmapChecksum       mnChecksum = 0;

    std::optional<GraphicID>     mxGraphicID;
    GraphicExternalLink          maGraphicExternalLink;

    // atomic because it is touched in parallel from the drawinglayer parallel rendering stuff in ScenePrimitive2D::create2DDecomposition
    mutable std::atomic<std::chrono::high_resolution_clock::time_point> maLastUsed = std::chrono::high_resolution_clock::now();
    bool mbPrepared = false;

public:
    ImpGraphic(bool bDefault = false);
    ImpGraphic( const ImpGraphic& rImpGraphic );
    ImpGraphic( ImpGraphic&& rImpGraphic ) noexcept;
    ImpGraphic( GraphicExternalLink aExternalLink);
    ImpGraphic(std::shared_ptr<GfxLink> xGfxLink, sal_Int32 nPageIndex = 0);
    ImpGraphic( const BitmapEx& rBmpEx );
    ImpGraphic(const std::shared_ptr<VectorGraphicData>& rVectorGraphicDataPtr);
    ImpGraphic( const Animation& rAnimation );
    ImpGraphic( const GDIMetaFile& rMtf );
    ~ImpGraphic();

    void setPrepared(bool bAnimated, const Size* pSizeHint);

private:

    ImpGraphic&         operator=( const ImpGraphic& rImpGraphic );
    ImpGraphic&         operator=( ImpGraphic&& rImpGraphic );
    bool                operator==( const ImpGraphic& rImpGraphic ) const;
    bool                operator!=( const ImpGraphic& rImpGraphic ) const { return !( *this == rImpGraphic ); }

    OUString const & getOriginURL() const
    {
        return maGraphicExternalLink.msURL;
    }

    void setOriginURL(OUString const & rOriginURL)
    {
        maGraphicExternalLink.msURL = rOriginURL;
    }

    OString getUniqueID()
    {
        if (!mxGraphicID)
            mxGraphicID.emplace(*this);
        return mxGraphicID->getIDString();
    }

    void createSwapInfo();
    void restoreFromSwapInfo();

    void                clearGraphics();
    void                clear();

    GraphicType         getType() const { return meType;}
    bool                isSupportedGraphic() const;

    bool                isTransparent() const;
    bool                isAlpha() const;
    bool                isAnimated() const;
    bool                isEPS() const;

    bool isAvailable() const;
    bool makeAvailable();

    BitmapEx            getBitmapEx(const GraphicConversionParameters& rParameters) const;
    /// Gives direct access to the contained BitmapEx.
    const BitmapEx&     getBitmapExRef() const;
    Animation           getAnimation() const;
    const GDIMetaFile&  getGDIMetaFile() const;

    Size                getSizePixel() const;

    Size                getPrefSize() const;
    void                setPrefSize( const Size& rPrefSize );

    MapMode             getPrefMapMode() const;
    void                setPrefMapMode( const MapMode& rPrefMapMode );

    sal_Int64 getSizeBytes() const;

    void ensureCurrentSizeInBytes();

    void                draw(OutputDevice& rOutDev, const Point& rDestPt,
                             const Size& rDestSize) const;

    void                startAnimation(OutputDevice& rOutDev,
                                       const Point& rDestPt,
                                       const Size& rDestSize,
                                       tools::Long nRendererId,
                                       OutputDevice* pFirstFrameOutDev);
    void                stopAnimation( const OutputDevice* pOutputDevice,
                                           tools::Long nRendererId );

    void                setAnimationNotifyHdl( const Link<Animation*,void>& rLink );
    Link<Animation*,void> getAnimationNotifyHdl() const;

    sal_uInt32          getAnimationLoopCount() const;

private:
    // swapping methods
    bool swapInFromStream(SvStream& rStream);
    bool swapInGraphic(SvStream& rStream);

    bool swapInContent(SvStream& rStream);
    bool swapOutContent(SvStream& rStream);
    bool swapOutGraphic(SvStream& rStream);
    // end swapping

    Bitmap getBitmap(const GraphicConversionParameters& rParameters) const;

    void                setDummyContext( bool value ) { mbDummyContext = value; }
    bool                isDummyContext() const { return mbDummyContext; }
    void                setGfxLink( const std::shared_ptr<GfxLink>& );
    const std::shared_ptr<GfxLink> & getSharedGfxLink() const;
    GfxLink             getGfxLink() const;
    bool                isGfxLink() const;

    BitmapChecksum      getChecksum() const;

    const std::shared_ptr<VectorGraphicData>& getVectorGraphicData() const;

    /// Gets the bitmap replacement for a vector graphic.
    // Hide volatile state of maBitmapEx when using maVectorGraphicData into this method
    void updateBitmapFromVectorGraphic(const Size& pixelSize = {}) const;

    bool ensureAvailable () const;

    sal_Int32 getPageNumber() const;

    // Set the pref size, but don't force swap-in
    void setValuesForPrefSize(const Size& rPrefSize);
    // Set the pref map mode, but don't force swap-in
    void setValuesForPrefMapMod(const MapMode& rPrefMapMode);

    bool canReduceMemory() const override;
    bool reduceMemory() override;
    std::chrono::high_resolution_clock::time_point getLastUsed() const override;
    void resetLastUsed() const;

public:
    void resetChecksum() { mnChecksum = 0; }
    bool swapIn();
    VCL_DLLPUBLIC bool swapOut();
    bool isSwappedOut() const { return mbSwapOut; }
    VCL_DLLPUBLIC SvStream* getSwapFileStream() const;
    // public only because of use in GraphicFilter
    void updateFromLoadedGraphic(const ImpGraphic* graphic);
    void dumpState(rtl::OStringBuffer &rState) override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
