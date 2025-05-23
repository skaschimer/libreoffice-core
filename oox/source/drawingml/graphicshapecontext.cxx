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

#include <sal/config.h>

#include <string_view>

#include <oox/drawingml/graphicshapecontext.hxx>

#include <osl/diagnose.h>
#include <sal/log.hxx>

#include <drawingml/embeddedwavaudiofile.hxx>
#include <drawingml/misccontexts.hxx>
#include <drawingml/graphicproperties.hxx>
#include <drawingml/customshapeproperties.hxx>
#include <oox/drawingml/diagram/diagram.hxx>
#include <drawingml/table/tablecontext.hxx>
#include <oox/core/xmlfilterbase.hxx>
#include <oox/helper/attributelist.hxx>
#include <oox/vml/vmldrawing.hxx>
#include <drawingml/transform2dcontext.hxx>
#include <oox/ppt/pptshapegroupcontext.hxx>
#include <oox/token/namespaces.hxx>
#include <oox/token/tokens.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::io;
using namespace ::com::sun::star::uno;
using namespace ::oox::core;

static uno::Reference<io::XInputStream>
lcl_GetMediaStream(const OUString& rStream, const oox::core::XmlFilterBase& rFilter)
{
    if (rStream.isEmpty())
        return nullptr;

    Reference< XInputStream > xInStrm( rFilter.openInputStream(rStream), UNO_SET_THROW );
    return xInStrm;
}

static OUString lcl_GetMediaReference(std::u16string_view rStream)
{
    return rStream.empty() ? OUString() : OUString::Concat("vnd.sun.star.Package:") + rStream;
}

namespace oox::drawingml {

// CT_Picture

GraphicShapeContext::GraphicShapeContext( ContextHandler2Helper const & rParent, const ShapePtr& pMasterShapePtr, const ShapePtr& pShapePtr )
: ShapeContext( rParent, pMasterShapePtr, pShapePtr )
{
}

ContextHandlerRef GraphicShapeContext::onCreateContext( sal_Int32 aElementToken, const AttributeList& rAttribs )
{
    switch( getBaseToken( aElementToken ) )
    {
    // CT_ShapeProperties
    case XML_xfrm:
        return new Transform2DContext( *this, rAttribs, *mpShapePtr );
    case XML_blipFill:
        return new BlipFillContext(*this, rAttribs, mpShapePtr->getGraphicProperties().maBlipProps, nullptr);
    case XML_wavAudioFile:
        {
            OUString const path(getEmbeddedWAVAudioFile(getRelations(), rAttribs));
            Reference<XInputStream> xMediaStream = lcl_GetMediaStream(path, getFilter());
            if (xMediaStream.is())
            {
                mpShapePtr->getGraphicProperties().m_xMediaStream = std::move(xMediaStream);
                mpShapePtr->getGraphicProperties().m_sMediaPackageURL = lcl_GetMediaReference(path);
            }
        }
        break;
    case XML_audioFile:
    case XML_videoFile:
        {
            OUString rPath = getRelations().getFragmentPathFromRelId(
                    rAttribs.getStringDefaulted(R_TOKEN(link)) );
            if (!rPath.isEmpty())
            {
                Reference<XInputStream> xMediaStream = lcl_GetMediaStream(rPath, getFilter());
                if (xMediaStream.is()) // embedded media file
                {
                    mpShapePtr->getGraphicProperties().m_xMediaStream = std::move(xMediaStream);
                    mpShapePtr->getGraphicProperties().m_sMediaPackageURL
                        = lcl_GetMediaReference(rPath);
                }
            }
            else
            {
                rPath = getRelations().getExternalTargetFromRelId(
                    rAttribs.getStringDefaulted(R_TOKEN(link)));
                if (!rPath.isEmpty()) // linked media file
                    mpShapePtr->getGraphicProperties().m_sMediaPackageURL
                        = getFilter().getAbsoluteUrl(rPath);
            }
        }
        break;
    }

    if ((getNamespace( aElementToken ) == NMSP_vml) && mpShapePtr)
    {
        mpShapePtr->setServiceName(u"com.sun.star.drawing.CustomShape"_ustr);
        CustomShapePropertiesPtr pCstmShpProps
            (mpShapePtr->getCustomShapeProperties());

        pCstmShpProps->setShapePresetType( getBaseToken( aElementToken ) );
    }

    return ShapeContext::onCreateContext( aElementToken, rAttribs );
}

// CT_GraphicalObjectFrameContext

GraphicalObjectFrameContext::GraphicalObjectFrameContext( ContextHandler2Helper& rParent, const ShapePtr& pMasterShapePtr, const ShapePtr& pShapePtr, bool bEmbedShapesInChart ) :
    ShapeContext( rParent, pMasterShapePtr, pShapePtr ),
    mbEmbedShapesInChart( bEmbedShapesInChart ),
    mpParent(&rParent)
{
}

ContextHandlerRef GraphicalObjectFrameContext::onCreateContext( sal_Int32 aElementToken, const AttributeList& rAttribs )
{
    switch( getBaseToken( aElementToken ) )
    {
    // CT_ShapeProperties
    case XML_nvGraphicFramePr:      // CT_GraphicalObjectFrameNonVisual
        break;
    case XML_xfrm:                  // CT_Transform2D
        return new Transform2DContext( *this, rAttribs, *mpShapePtr );
    case XML_graphic:               // CT_GraphicalObject
        return this;

        case XML_graphicData :          // CT_GraphicalObjectData
        {
            OUString sUri( rAttribs.getStringDefaulted( XML_uri ) );
            if ( sUri == "http://schemas.openxmlformats.org/presentationml/2006/ole" ||
                    sUri == "http://purl.oclc.org/ooxml/presentationml/ole" )
                return new OleObjectGraphicDataContext( *this, mpShapePtr );
            else if ( sUri == "http://schemas.openxmlformats.org/drawingml/2006/diagram" ||
                    sUri == "http://purl.oclc.org/ooxml/drawingml/diagram" )
                return new DiagramGraphicDataContext( *this, mpShapePtr );
            else if ( sUri == "http://schemas.openxmlformats.org/drawingml/2006/chart" ||
                    sUri == "http://purl.oclc.org/ooxml/drawingml/chart" )
                return new ChartGraphicDataContext( *this, mpShapePtr, mbEmbedShapesInChart );
            else if ( sUri == "http://schemas.microsoft.com/office/drawing/2014/chartex" )
                // Is there a corresponding purl.oclc.org URL? At this time
                // (2025) those don't seem to be active.
                return new ChartGraphicDataContext( *this, mpShapePtr, mbEmbedShapesInChart );
            else if ( sUri == "http://schemas.openxmlformats.org/drawingml/2006/table" ||
                    sUri == "http://purl.oclc.org/ooxml/drawingml/table" )
                return new table::TableContext( *this, mpShapePtr );
            else
            {
                SAL_WARN("oox.drawingml", "OOX: Ignore graphicsData of :" << sUri );
                return nullptr;
            }
        }
        break;
    }

    return ShapeContext::onCreateContext( aElementToken, rAttribs );
}

void GraphicalObjectFrameContext::onEndElement()
{
    if( getCurrentElement() == PPT_TOKEN( graphicFrame ) && mpParent )
    {
        oox::ppt::PPTShapeGroupContext* pParent = dynamic_cast<oox::ppt::PPTShapeGroupContext*>(mpParent);
        if( pParent )
            pParent->importExtDrawings();
    }
}

OleObjectGraphicDataContext::OleObjectGraphicDataContext( ContextHandler2Helper const & rParent, const ShapePtr& xShape ) :
    ShapeContext( rParent, ShapePtr(), xShape ),
    mrOleObjectInfo( xShape->setOleObjectType() )
{
}

OleObjectGraphicDataContext::~OleObjectGraphicDataContext()
{
    /*  Register the OLE shape at the VML drawing, this prevents that the
        related VML shape converts the OLE object by itself. */
    if( !mrOleObjectInfo.maShapeId.isEmpty() )
        if( ::oox::vml::Drawing* pVmlDrawing = getFilter().getVmlDrawing() )
            pVmlDrawing->registerOleObject( mrOleObjectInfo );
}

ContextHandlerRef OleObjectGraphicDataContext::onCreateContext( sal_Int32 nElement, const AttributeList& rAttribs )
{
    switch( nElement )
    {
        case PPT_TOKEN( oleObj ):
        {
            mrOleObjectInfo.maShapeId = rAttribs.getXString( XML_spid, OUString() );
            const Relation* pRelation = getRelations().getRelationFromRelId( rAttribs.getStringDefaulted( R_TOKEN( id )) );
            OSL_ENSURE( pRelation, "OleObjectGraphicDataContext::createFastChildContext - missing relation for OLE object" );
            if( pRelation )
            {
                mrOleObjectInfo.mbLinked = pRelation->mbExternal;
                if( pRelation->mbExternal )
                {
                    mrOleObjectInfo.maTargetLink = getFilter().getAbsoluteUrl( pRelation->maTarget );
                }
                else
                {
                    OUString aFragmentPath = getFragmentPathFromRelation( *pRelation );
                    if( !aFragmentPath.isEmpty() )
                        getFilter().importBinaryData( mrOleObjectInfo.maEmbeddedData, aFragmentPath );
                }
            }
            mrOleObjectInfo.maName = rAttribs.getXString( XML_name, OUString() );
            mrOleObjectInfo.maProgId = rAttribs.getXString( XML_progId, OUString() );
            mrOleObjectInfo.mbShowAsIcon = rAttribs.getBool( XML_showAsIcon, false );
            mrOleObjectInfo.mbHasPicture = false; // Initialize as false
            return this;
        }
        break;

        case PPT_TOKEN( embed ):
            OSL_ENSURE( !mrOleObjectInfo.mbLinked, "OleObjectGraphicDataContext::createFastChildContext - unexpected child element" );
        break;

        case PPT_TOKEN( link ):
            OSL_ENSURE( mrOleObjectInfo.mbLinked, "OleObjectGraphicDataContext::createFastChildContext - unexpected child element" );
            mrOleObjectInfo.mbAutoUpdate = rAttribs.getBool( XML_updateAutomatic, false );
        break;
        case PPT_TOKEN( pic ):
            mrOleObjectInfo.mbHasPicture = true; // Set true if ole object has picture element.
            return new GraphicShapeContext( *this, mpMasterShapePtr, mpShapePtr );
    }
    SAL_WARN("oox", "OleObjectGraphicDataContext::onCreateContext: unhandled element: "
                        << getBaseToken(nElement));
    return nullptr;
}

void OleObjectGraphicDataContext::onEndElement()
{
    if( getCurrentElement() == PPT_TOKEN( oleObj ) && !isMCEStateEmpty() )
    {
        if (getMCEState() == MCE_STATE::FoundChoice && !mrOleObjectInfo.mbHasPicture
            && mrOleObjectInfo.maShapeId.isEmpty())
            setMCEState( MCE_STATE::Started );
    }
}

DiagramGraphicDataContext::DiagramGraphicDataContext( ContextHandler2Helper const & rParent, const ShapePtr& pShapePtr )
: ShapeContext( rParent, ShapePtr(), pShapePtr )
{
    pShapePtr->setDiagramType();
}

DiagramGraphicDataContext::~DiagramGraphicDataContext()
{
}

ContextHandlerRef DiagramGraphicDataContext::onCreateContext( ::sal_Int32 aElementToken, const AttributeList& rAttribs )
{
    switch( aElementToken )
    {
    case DGM_TOKEN( relIds ):
    {
        msDm = rAttribs.getStringDefaulted( R_TOKEN( dm ) );
        msLo = rAttribs.getStringDefaulted( R_TOKEN( lo ) );
        msQs = rAttribs.getStringDefaulted( R_TOKEN( qs ) );
        msCs = rAttribs.getStringDefaulted( R_TOKEN( cs ) );
        loadDiagram(mpShapePtr,
                    getFilter(),
                    getFragmentPathFromRelId( msDm ),
                    getFragmentPathFromRelId( msLo ),
                    getFragmentPathFromRelId( msQs ),
                    getFragmentPathFromRelId( msCs ),
                    getRelations());
        SAL_INFO("oox.drawingml", "DiagramGraphicDataContext::onCreateContext: added shape " << mpShapePtr->getName()
                 << " of type " << mpShapePtr->getServiceName()
                 << ", position: " << mpShapePtr->getPosition().X
                 << "," << mpShapePtr->getPosition().Y
                 << ", size: " << mpShapePtr->getSize().Width
                 << "x" << mpShapePtr->getSize().Height);

        // No DrawingML fallback, need to warn the user at the end.
        if (mpShapePtr->getExtDrawings().empty())
            getFilter().setMissingExtDrawing();
        else
        {
            for (const auto& rRelId : mpShapePtr->getExtDrawings())
            {
                // An invalid fallback reference is as bad as a missing one.
                if (getFragmentPathFromRelId(rRelId).isEmpty())
                {
                    getFilter().setMissingExtDrawing();
                    break;
                }
            }
        }

        break;
    }
    default:
        break;
    }

    return ShapeContext::onCreateContext( aElementToken, rAttribs );
}

ChartGraphicDataContext::ChartGraphicDataContext( ContextHandler2Helper const & rParent, const ShapePtr& rxShape, bool bEmbedShapes ) :
    ShapeContext( rParent, ShapePtr(), rxShape ),
    mrChartShapeInfo( rxShape->setChartType( bEmbedShapes ) )
{
}

ContextHandlerRef ChartGraphicDataContext::onCreateContext( ::sal_Int32 nElement, const AttributeList& rAttribs )
{
    if( nElement == C_TOKEN( chart ) || nElement == CX_TOKEN( chart ))
    {
        mrChartShapeInfo.maFragmentPath = getFragmentPathFromRelId( rAttribs.getStringDefaulted( R_TOKEN( id )) );
    }
    return nullptr;
}

} // namespace oox::drawingml

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
