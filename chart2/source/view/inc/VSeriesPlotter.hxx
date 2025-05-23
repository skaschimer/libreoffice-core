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

#include <memory>
#include "PlotterBase.hxx"
#include "VDataSeries.hxx"
#include "LabelAlignment.hxx"
#include "MinimumAndMaximumSupplier.hxx"
#include "LegendEntryProvider.hxx"
#include <basegfx/range/b2irectangle.hxx>
#include <com/sun/star/drawing/Direction3D.hpp>
#include <rtl/ref.hxx>
#include <svx/unoshape.hxx>

namespace com::sun::star::awt { struct Point; }
namespace com::sun::star::chart2 { class XChartType; }


namespace chart { class ExplicitCategoriesProvider; }
namespace chart { struct ExplicitScaleData; }
namespace chart { class ChartModel; }

namespace com::sun::star {
    namespace util {
        class XNumberFormatsSupplier;
    }
    namespace chart2 {
        class XColorScheme;
        class XRegressionCurveCalculator;
    }
}

namespace chart {

class ChartType;
class NumberFormatterWrapper;

class AxesNumberFormats
{
public:
    AxesNumberFormats() {};

    void setFormat( sal_Int32 nFormatKey, sal_Int32 nDimIndex, sal_Int32 nAxisIndex )
    {
        m_aNumberFormatMap[tFullAxisIndex(nDimIndex,nAxisIndex)] = nFormatKey;
    }

private:
    typedef std::pair< sal_Int32, sal_Int32 > tFullAxisIndex;
    std::map< tFullAxisIndex, sal_Int32 > m_aNumberFormatMap;
};

/**
 * A list of series that have the same CoordinateSystem. They are used to be
 * plotted maybe in a stacked manner by a plotter.
 */
class VDataSeriesGroup final
{
public:
    VDataSeriesGroup() = delete;
    VDataSeriesGroup( std::unique_ptr<VDataSeries> pSeries );
    VDataSeriesGroup(VDataSeriesGroup&&) noexcept;
    ~VDataSeriesGroup();

    void addSeries( std::unique_ptr<VDataSeries> pSeries );//takes ownership of pSeries
    sal_Int32 getSeriesCount() const;
    void deleteSeries();

    sal_Int32    getPointCount() const;
    sal_Int32    getAttachedAxisIndexForFirstSeries() const;

    void getMinimumAndMaximumX( double& rfMinimum, double& rfMaximum ) const;
    void getMinimumAndMaximumYInContinuousXRange( double& rfMinY, double& rfMaxY, double fMinX, double fMaxX, sal_Int32 nAxisIndex ) const;

    void calculateYMinAndMaxForCategory( sal_Int32 nCategoryIndex
                                            , bool bSeparateStackingForDifferentSigns
                                            , double& rfMinimumY, double& rfMaximumY, sal_Int32 nAxisIndex ) const;
    void calculateYMinAndMaxForCategoryRange( sal_Int32 nStartCategoryIndex, sal_Int32 nEndCategoryIndex
                                                , bool bSeparateStackingForDifferentSigns
                                                , double& rfMinimumY, double& rfMaximumY, sal_Int32 nAxisIndex );

    std::vector< std::unique_ptr<VDataSeries> >   m_aSeriesVector;

private:
    //cached values
    struct CachedYValues
    {
        CachedYValues();

        bool    m_bValuesDirty;
        double  m_fMinimumY;
        double  m_fMaximumY;
    };

    mutable bool        m_bMaxPointCountDirty;
    mutable sal_Int32   m_nMaxPointCount;
    typedef std::map< sal_Int32, CachedYValues > tCachedYValuesPerAxisIndexMap;
    mutable std::vector< tCachedYValuesPerAxisIndexMap >   m_aListOfCachedYValues;
};

class VSeriesPlotter : public PlotterBase, public MinimumAndMaximumSupplier, public LegendEntryProvider
{
public:
    VSeriesPlotter() = delete;

    virtual ~VSeriesPlotter() override;

    /**
    * A new series can be positioned relative to other series in a chart.
    * This positioning has two dimensions. First a series can be placed
    * next to each other on the category axis. This position is indicated by xSlot.
    * Second a series can be stacked on top of another. This position is indicated by ySlot.
    * The positions are counted from 0 on.
    * xSlot < 0                     : append the series to already existing x series
    * xSlot > occupied              : append the series to already existing x series
    *
    * If the xSlot is already occupied the given ySlot decides what should happen:
    * ySlot < -1                    : move all existing series in the xSlot to next slot
    * ySlot == -1                   : stack on top at given x position
    * ySlot == already occupied     : insert at given y and x position
    * ySlot > occupied              : stack on top at given x position
    */
    virtual void addSeries( std::unique_ptr<VDataSeries> pSeries, sal_Int32 zSlot, sal_Int32 xSlot, sal_Int32 ySlot );

    /** a value <= 0 for a directions means that this direction can be stretched arbitrary
    */
    virtual css::drawing::Direction3D  getPreferredDiagramAspectRatio() const;

    /** this enables you to handle series on the same x axis with different y axis
    the property AttachedAxisIndex at a dataseries indicates which value scale is to use
    (0==AttachedAxisIndex or a not set AttachedAxisIndex property indicates that this series should be scaled at the main y-axis;
    1==AttachedAxisIndex indicates that the series should be scaled at the first secondary axis if there is any otherwise at the main y axis
    and so on.
    The parameter nAxisIndex matches this DataSeries property 'AttachedAxisIndex'.
    nAxisIndex must be greater than 0. nAxisIndex==1 refers to the first secondary axis.
    )

    @throws css::uno::RuntimeException
    */

    void addSecondaryValueScale( const ExplicitScaleData& rScale, sal_Int32 nAxisIndex );

    // MinimumAndMaximumSupplier

    virtual double getMinimumX() override;
    virtual double getMaximumX() override;

    virtual std::pair<double, double> getMinimumAndMaximumYInRange( double fMinimumX, double fMaximumX, sal_Int32 nAxisIndex ) override;

    virtual double getMinimumZ() override;
    virtual double getMaximumZ() override;

    virtual bool isExpandBorderToIncrementRhythm( sal_Int32 nDimensionIndex ) override;
    virtual bool isExpandIfValuesCloseToBorder( sal_Int32 nDimensionIndex ) override;
    virtual bool isExpandWideValuesToZero( sal_Int32 nDimensionIndex ) override;
    virtual bool isExpandNarrowValuesTowardZero( sal_Int32 nDimensionIndex ) override;
    virtual bool isSeparateStackingForDifferentSigns( sal_Int32 nDimensionIndex ) override;

    virtual tools::Long calculateTimeResolutionOnXAxis() override;
    virtual void setTimeResolutionOnXAxis( tools::Long nTimeResolution, const Date& rNullDate ) override;

    void getMinimumAndMaximumX( double& rfMinimum, double& rfMaximum ) const;
    void getMinimumAndMaximumYInContinuousXRange( double& rfMinY, double& rfMaxY, double fMinX, double fMaxX, sal_Int32 nAxisIndex ) const;


    // Methods for handling legends and legend entries.

    virtual std::vector< ViewLegendEntry > createLegendEntries(
            const css::awt::Size& rEntryKeyAspectRatio,
            css::chart2::LegendPosition eLegendPosition,
            const css::uno::Reference< css::beans::XPropertySet >& xTextProperties,
            const rtl::Reference<SvxShapeGroupAnyD>& xTarget,
            const css::uno::Reference< css::uno::XComponentContext >& xContext,
            ChartModel& rModel
                ) override;

    virtual LegendSymbolStyle getLegendSymbolStyle();
    virtual css::awt::Size getPreferredLegendKeyAspectRatio() override;

    virtual css::uno::Any getExplicitSymbol( const VDataSeries& rSeries, sal_Int32 nPointIndex/*-1 for series symbol*/ );

    rtl::Reference<SvxShapeGroup> createLegendSymbolForSeries(
                  const css::awt::Size& rEntryKeyAspectRatio
                , const VDataSeries& rSeries
                , const rtl::Reference<SvxShapeGroupAnyD>& xTarget );

    rtl::Reference< SvxShapeGroup > createLegendSymbolForPoint(
                  const css::awt::Size& rEntryKeyAspectRatio
                , const VDataSeries& rSeries
                , sal_Int32 nPointIndex
                , const rtl::Reference<SvxShapeGroupAnyD>& xTarget );

    std::vector< ViewLegendEntry > createLegendEntriesForSeries(
            const css::awt::Size& rEntryKeyAspectRatio,
            const VDataSeries& rSeries,
            const css::uno::Reference< css::beans::XPropertySet >& xTextProperties,
            const rtl::Reference<SvxShapeGroupAnyD>& xTarget,
            const css::uno::Reference< css::uno::XComponentContext >& xContext
                );

    std::vector<ViewLegendSymbol> createSymbols(
              const css::awt::Size& rEntryKeyAspectRatio
            , const rtl::Reference<SvxShapeGroupAnyD>& xTarget
            , const css::uno::Reference<css::uno::XComponentContext>& xContext);

    std::vector<ViewLegendSymbol> createSymbolsForSeries(
              const css::awt::Size& rEntryKeyAspectRatio
            , const VDataSeries& rSeries
            , const rtl::Reference<SvxShapeGroupAnyD>& xTarget
            , const css::uno::Reference<css::uno::XComponentContext>& xContext);

    std::vector<VDataSeries*> getAllSeries();
    std::vector<VDataSeries const*> getAllSeries() const;

    // This method creates a series plotter of the requested type; e.g. : return new PieChart...
    static VSeriesPlotter* createSeriesPlotter( const rtl::Reference< ::chart::ChartType >& xChartTypeModel
                                , sal_Int32 nDimensionCount
                                , bool bExcludingPositioning /*for pie and donut charts labels and exploded segments are excluded from the given size*/);

    sal_Int32 getPointCount() const;

    // Methods for number formats and color schemes

    void setNumberFormatsSupplier( const css::uno::Reference< css::util::XNumberFormatsSupplier > & xNumFmtSupplier );

    void setColorScheme( const css::uno::Reference< css::chart2::XColorScheme >& xColorScheme );

    void setExplicitCategoriesProvider( ExplicitCategoriesProvider* pExplicitCategoriesProvider );

    ExplicitCategoriesProvider* getExplicitCategoriesProvider() { return m_pExplicitCategoriesProvider; }

    //get series names for the z axis labels
    css::uno::Sequence<OUString> getSeriesNames() const;

    //get all series names
    css::uno::Sequence<OUString> getAllSeriesNames() const;

    void setPageReferenceSize( const css::awt::Size & rPageRefSize );
    //better performance for big data
    void setCoordinateSystemResolution( const css::uno::Sequence< sal_Int32 >& rCoordinateSystemResolution );
    bool PointsWereSkipped() const { return m_bPointsWereSkipped;}
    void setPieLabelsAllowToMove( bool bIsPieOrDonut ) { m_bPieLabelsAllowToMove = bIsPieOrDonut; };
    void setAvailableOuterRect( const basegfx::B2IRectangle& aAvailableOuterRect ) { m_aAvailableOuterRect = aAvailableOuterRect; };

    //return the depth for a logic 1
    double  getTransformedDepth() const;

    void    releaseShapes();

    virtual void rearrangeLabelToAvoidOverlapIfRequested( const css::awt::Size& rPageSize );

    bool WantToPlotInFrontOfAxisLine();
    virtual bool shouldSnapRectToUsedArea();

    /// This method returns a text string representation of the passed numeric
    /// value by exploiting a NumberFormatterWrapper object.
    OUString getLabelTextForValue(VDataSeries const & rDataSeries, sal_Int32 nPointIndex,
                                  double fValue, bool bAsPercentage);

    sal_Int32 getRenderOrder() const;

protected:

    VSeriesPlotter( rtl::Reference< ::chart::ChartType > xChartTypeModel
                , sal_Int32 nDimensionCount
                , bool bCategoryXAxis=true );

    // Methods for group shapes.

    rtl::Reference<SvxShapeGroupAnyD>
        getSeriesGroupShape( VDataSeries* pDataSeries
            , const rtl::Reference<SvxShapeGroupAnyD>& xTarget );

    //the following group shapes will be created as children of SeriesGroupShape on demand
    //they can be used to assure that some parts of a series shape are always in front of others (e.g. symbols in front of lines)
    //parameter xTarget will be used as parent for the series group shape
    rtl::Reference<SvxShapeGroupAnyD>
        getSeriesGroupShapeFrontChild( VDataSeries* pDataSeries
            , const rtl::Reference<SvxShapeGroupAnyD>& xTarget );
    rtl::Reference<SvxShapeGroupAnyD>
        getSeriesGroupShapeBackChild( VDataSeries* pDataSeries
            , const rtl::Reference<SvxShapeGroupAnyD>& xTarget );

    /// This method creates a 2D group shape for containing all text shapes
    /// needed for this series; the group is added to the text target;
    static rtl::Reference<SvxShapeGroup>
        getLabelsGroupShape( VDataSeries& rDataSeries
            , const rtl::Reference<SvxShapeGroupAnyD>& xTarget );

    rtl::Reference<SvxShapeGroupAnyD>
        getErrorBarsGroupShape( VDataSeries& rDataSeries
            , const rtl::Reference<SvxShapeGroupAnyD>& xTarget, bool bYError );

    /** This method creates a text shape for a label related to a data point
     *  and append it to the root text shape group (xTarget).
     *
     *  @param xTarget
     *      the main root text shape group.
     *  @param rDataSeries
     *      the data series, the data point belongs to.
     *  @param nPointIndex
     *      the index of the data point the label is related to.
     *  @param fValue
     *      the value of the data point.
     *  @param fSumValue
     *      the sum of all data point values in the data series.
     *  @param rScreenPosition2D
     *      the anchor point position for the label.
     *  @param eAlignment
     *      the required alignment of the label.
     *  @param offset
     *      an optional offset depending on the label alignment.
     *  @param nTextWidth
     *      the maximum width of a text label (used for text wrapping).
     *
     *  @return
     *      a reference to the created text shape.
     */
    rtl::Reference<SvxShapeText>
        createDataLabel( const rtl::Reference<SvxShapeGroupAnyD>& xTarget
                , VDataSeries& rDataSeries
                , sal_Int32 nPointIndex
                , double fValue
                , double fSumValue
                , const css::awt::Point& rScreenPosition2D
                , LabelAlignment eAlignment
                , sal_Int32 nOffset=0
                , sal_Int32 nTextWidth = 0 );

    /** creates two T-shaped error bars in both directions (up/down or
        left/right depending on the bVertical parameter)

        @param rPos
            logic coordinates

        @param xErrorBarProperties
            the XPropertySet returned by the DataPoint-property "ErrorBarX" or
            "ErrorBarY".

        @param nIndex
            the index of the data point in rData for which the calculation is
            done.

        @param bVertical
            for y-error bars this is true, for x-error-bars it is false.
     */
    void createErrorBar(
          const rtl::Reference<SvxShapeGroupAnyD>& xTarget
        , const css::drawing::Position3D & rPos
        , const css::uno::Reference< css::beans::XPropertySet > & xErrorBarProperties
        , const VDataSeries& rVDataSeries
        , sal_Int32 nIndex
        , bool bVertical
        , const double* pfScaledLogicX
        );

    void createErrorRectangle(
          const css::drawing::Position3D& rUnscaledLogicPosition
        , VDataSeries& rVDataSeries
        , sal_Int32 nIndex
        , const rtl::Reference<SvxShapeGroupAnyD>& rTarget
        , bool bUseXErrorData
        , bool bUseYErrorData
    );

    static void addErrorBorder(
          const css::drawing::Position3D& rPos0
        , const css::drawing::Position3D& rPos1
        , const rtl::Reference<SvxShapeGroupAnyD>& rTarget
        , const css::uno::Reference< css::beans::XPropertySet >& rErrorBorderProp );

    void createErrorBar_X( const css::drawing::Position3D& rUnscaledLogicPosition
        , VDataSeries& rVDataSeries, sal_Int32 nPointIndex
        , const rtl::Reference<SvxShapeGroupAnyD>& xTarget );

    void createErrorBar_Y( const css::drawing::Position3D& rUnscaledLogicPosition
        , VDataSeries& rVDataSeries, sal_Int32 nPointIndex
        , const rtl::Reference<SvxShapeGroupAnyD>& xTarget
        , double const * pfScaledLogicX );

    void createRegressionCurvesShapes( VDataSeries const & rVDataSeries
        , const rtl::Reference<SvxShapeGroupAnyD>& xTarget
        , const rtl::Reference<SvxShapeGroupAnyD>& xEquationTarget
        , bool bMaySkipPointsInRegressionCalculation );

    void createRegressionCurveEquationShapes( const OUString & rEquationCID
        , const css::uno::Reference< css::beans::XPropertySet > & xEquationProperties
        , const rtl::Reference<SvxShapeGroupAnyD>& xEquationTarget
        , const css::uno::Reference< css::chart2::XRegressionCurveCalculator > & xRegressionCurveCalculator
        , css::awt::Point aDefaultPos );

    virtual PlottingPositionHelper& getPlottingPositionHelper( sal_Int32 nAxisIndex ) const;//nAxisIndex indicates whether the position belongs to the main axis ( nAxisIndex==0 ) or secondary axis ( nAxisIndex==1 )

    VDataSeries* getFirstSeries() const;

    OUString getCategoryName( sal_Int32 nPointIndex ) const;

protected:
    PlottingPositionHelper*    m_pMainPosHelper;

    rtl::Reference< ::chart::ChartType > m_xChartTypeModel;

    std::vector< std::vector< VDataSeriesGroup > >  m_aZSlots;

    bool                                m_bCategoryXAxis;//true->xvalues are indices (this would not be necessary if series for category chart wouldn't have x-values)
    tools::Long m_nTimeResolution;
    Date m_aNullDate;

    std::unique_ptr< NumberFormatterWrapper > m_apNumberFormatterWrapper;

    css::uno::Reference< css::chart2::XColorScheme >    m_xColorScheme;

    ExplicitCategoriesProvider*    m_pExplicitCategoriesProvider;

    //better performance for big data
    css::uno::Sequence< sal_Int32 >    m_aCoordinateSystemResolution;
    bool m_bPointsWereSkipped;
    bool m_bPieLabelsAllowToMove;
    basegfx::B2IRectangle m_aAvailableOuterRect;
    css::awt::Size m_aPageReferenceSize;

private:
    typedef std::map< sal_Int32 , ExplicitScaleData > tSecondaryValueScales;
    tSecondaryValueScales   m_aSecondaryValueScales;

    typedef std::map< sal_Int32 , std::unique_ptr<PlottingPositionHelper> > tSecondaryPosHelperMap;
    mutable tSecondaryPosHelperMap   m_aSecondaryPosHelperMap;
};

} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
