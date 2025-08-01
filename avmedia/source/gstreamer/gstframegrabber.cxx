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

#include "gstframegrabber.hxx"

#include <cppuhelper/supportsservice.hxx>

#include <gst/gstbuffer.h>
#include <o3tl/safeint.hxx>
#include <vcl/graph.hxx>
#include <vcl/BitmapTools.hxx>

constexpr OUString AVMEDIA_GST_FRAMEGRABBER_SERVICENAME = u"com.sun.star.media.FrameGrabber_GStreamer"_ustr;

using namespace ::com::sun::star;

namespace avmedia::gstreamer {

void FrameGrabber::disposePipeline()
{
    if( mpPipeline != nullptr )
    {
        gst_element_set_state( mpPipeline, GST_STATE_NULL );
        g_object_unref( G_OBJECT( mpPipeline ) );
        mpPipeline = nullptr;
    }
}

FrameGrabber::FrameGrabber( std::u16string_view rURL )
{
    const char pPipelineStr[] =
        "uridecodebin name=source ! videoconvert ! videoscale ! appsink "
        "name=sink caps=\"video/x-raw,format=RGB,pixel-aspect-ratio=1/1\"";

    GError *pError = nullptr;
    mpPipeline = gst_parse_launch( pPipelineStr, &pError );
    if( pError != nullptr) {
        g_warning( "Failed to construct frame-grabber pipeline '%s'\n", pError->message );
        g_error_free( pError );
        disposePipeline();
    }

    if( mpPipeline ) {

        if (GstElement *pUriDecode = gst_bin_get_by_name(GST_BIN(mpPipeline), "source"))
            g_object_set(pUriDecode, "uri", OUStringToOString(rURL, RTL_TEXTENCODING_UTF8).getStr(), nullptr);
        else
            g_warning("Missing 'source' element in gstreamer pipeline");

        // pre-roll
        switch( gst_element_set_state( mpPipeline, GST_STATE_PAUSED ) ) {
        case GST_STATE_CHANGE_FAILURE:
        case GST_STATE_CHANGE_NO_PREROLL:
            g_warning( "failure pre-rolling media" );
            disposePipeline();
            break;
        default:
            break;
        }
    }
    if( mpPipeline &&
        gst_element_get_state( mpPipeline, nullptr, nullptr, 5 * GST_SECOND ) == GST_STATE_CHANGE_FAILURE )
        disposePipeline();
}

FrameGrabber::~FrameGrabber()
{
    disposePipeline();
}

rtl::Reference<FrameGrabber> FrameGrabber::create( std::u16string_view rURL )
{
    return new FrameGrabber( rURL );
}

uno::Reference< graphic::XGraphic > SAL_CALL FrameGrabber::grabFrame( double fMediaTime )
{
    uno::Reference< graphic::XGraphic > xRet;

    if( !mpPipeline )
        return xRet;

    gint64 gst_position = llround( fMediaTime * GST_SECOND );
    gst_element_seek_simple(
        mpPipeline, GST_FORMAT_TIME,
        GstSeekFlags(GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH),
        gst_position );

    GstElement *pSink = gst_bin_get_by_name( GST_BIN( mpPipeline ), "sink" );
    if( !pSink )
        return xRet;

    GstBuffer *pBuf = nullptr;
    GstCaps *pCaps = nullptr;

    // synchronously fetch the frame
    GstSample *pSample = nullptr;
    g_signal_emit_by_name( pSink, "pull-preroll", &pSample, nullptr );

    if( pSample )
    {
        pBuf = gst_sample_get_buffer( pSample );
        pCaps = gst_sample_get_caps( pSample );
    }

    // get geometry
    int nWidth = 0, nHeight = 0;
    if( !pCaps )
        g_warning( "could not get snapshot format\n" );
    else
    {
        GstStructure *pStruct = gst_caps_get_structure( pCaps, 0 );

        /* we need to get the final caps on the buffer to get the size */
        if( !gst_structure_get_int( pStruct, "width", &nWidth ) ||
            !gst_structure_get_int( pStruct, "height", &nHeight ) )
            nWidth = nHeight = 0;
    }

    if( pBuf && nWidth > 0 && nHeight > 0 &&
        // sanity check the size
        gst_buffer_get_size( pBuf ) >= o3tl::make_unsigned( nWidth * nHeight * 3 )
        )
    {
        sal_uInt8 *pData = nullptr;
        GstMapInfo aMapInfo;
        gst_buffer_map( pBuf, &aMapInfo, GST_MAP_READ );
        pData = aMapInfo.data;

        int nStride = GST_ROUND_UP_4( nWidth * 3 );
        Bitmap aBmp = vcl::bitmap::CreateFromData(pData, nWidth, nHeight, nStride, /*nBitsPerPixel*/24);

        gst_buffer_unmap( pBuf, &aMapInfo );
        xRet = Graphic( aBmp ).GetXGraphic();
    }

    return xRet;
}

OUString SAL_CALL FrameGrabber::getImplementationName(  )
{
    return u"com.sun.star.comp.avmedia.FrameGrabber_GStreamer"_ustr;
}

sal_Bool SAL_CALL FrameGrabber::supportsService( const OUString& ServiceName )
{
    return cppu::supportsService(this, ServiceName);
}

uno::Sequence< OUString > SAL_CALL FrameGrabber::getSupportedServiceNames()
{
    return { AVMEDIA_GST_FRAMEGRABBER_SERVICENAME };
}

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
