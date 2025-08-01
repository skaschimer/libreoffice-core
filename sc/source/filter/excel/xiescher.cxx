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

#include <xiescher.hxx>

#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/container/XIndexContainer.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/embed/Aspects.hpp>
#include <com/sun/star/embed/XEmbeddedObject.hpp>
#include <com/sun/star/embed/XEmbedPersist.hpp>
#include <com/sun/star/awt/PushButtonType.hpp>
#include <com/sun/star/awt/ScrollBarOrientation.hpp>
#include <com/sun/star/awt/VisualEffect.hpp>
#include <com/sun/star/style/VerticalAlignment.hpp>
#include <com/sun/star/drawing/XControlShape.hpp>
#include <com/sun/star/form/XForm.hpp>
#include <com/sun/star/form/XFormsSupplier.hpp>
#include <com/sun/star/form/binding/XBindableValue.hpp>
#include <com/sun/star/form/binding/XValueBinding.hpp>
#include <com/sun/star/form/binding/XListEntrySink.hpp>
#include <com/sun/star/form/binding/XListEntrySource.hpp>
#include <com/sun/star/script/ScriptEventDescriptor.hpp>
#include <com/sun/star/script/XEventAttacherManager.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/frame/XModel.hpp>

#include <sfx2/objsh.hxx>
#include <officecfg/Office/Common.hxx>
#include <unotools/moduleoptions.hxx>
#include <comphelper/configuration.hxx>
#include <vcl/dibtools.hxx>
#include <vcl/gdimtf.hxx>
#include <vcl/outdev.hxx>
#include <vcl/wmf.hxx>
#include <comphelper/classids.hxx>
#include <comphelper/documentinfo.hxx>
#include <o3tl/safeint.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <basegfx/point/b2dpoint.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <sal/log.hxx>

#include <svx/svdopath.hxx>
#include <svx/svdocirc.hxx>
#include <svx/svdoedge.hxx>
#include <svx/svdogrp.hxx>
#include <svx/svdoashp.hxx>
#include <svx/svdograf.hxx>
#include <svx/svdoole2.hxx>
#include <svx/svdouno.hxx>
#include <svx/svdpage.hxx>
#include <editeng/editobj.hxx>
#include <editeng/outliner.hxx>
#include <editeng/outlobj.hxx>
#include <svx/svditer.hxx>
#include <editeng/writingmodeitem.hxx>
#include <svx/xlnclit.hxx>
#include <svx/xlndsit.hxx>
#include <svx/xlnedcit.hxx>
#include <svx/xlnedit.hxx>
#include <svx/xlnedwit.hxx>
#include <svx/xlnstcit.hxx>
#include <svx/xlnstit.hxx>
#include <svx/xlnstwit.hxx>
#include <svx/xlnwtit.hxx>
#include <svx/sdasitm.hxx>
#include <svx/sdshcitm.hxx>
#include <svx/sdshitm.hxx>
#include <svx/sdsxyitm.hxx>
#include <svx/sdtagitm.hxx>
#include <svx/sdtditm.hxx>

#include <editeng/eeitem.hxx>
#include <svx/xflclit.hxx>
#include <sal/macros.h>
#include <editeng/adjustitem.hxx>
#include <svx/xfillit0.hxx>
#include <svx/xlineit0.hxx>
#include <svx/xlinjoit.hxx>
#include <svx/xlntrit.hxx>
#include <svx/xbtmpit.hxx>
#include <svx/xbitmap.hxx>
#include <svtools/embedhlp.hxx>
#include <sot/storage.hxx>

#include <document.hxx>
#include <drwlayer.hxx>
#include <docsh.hxx>
#include <userdat.hxx>
#include <unonames.hxx>
#include <convuno.hxx>
#include <postit.hxx>
#include <globstr.hrc>
#include <scresid.hxx>

#include <fprogressbar.hxx>
#include <xltracer.hxx>
#include <xistream.hxx>
#include <xihelper.hxx>
#include <xiformula.hxx>
#include <xilink.hxx>
#include <xistyle.hxx>
#include <xipage.hxx>
#include <xichart.hxx>
#include <xicontent.hxx>
#include <scextopt.hxx>

#include <namebuff.hxx>
#include <sfx2/docfile.hxx>
#include <memory>
#include <numeric>
#include <string_view>
#include <utility>

using namespace com::sun::star;
using ::com::sun::star::uno::Any;
using ::com::sun::star::beans::XPropertySet;
using ::com::sun::star::uno::Exception;
using ::com::sun::star::uno::Reference;
using ::com::sun::star::uno::Sequence;
using ::com::sun::star::uno::UNO_QUERY;
using ::com::sun::star::uno::UNO_QUERY_THROW;
using ::com::sun::star::uno::UNO_SET_THROW;
using ::com::sun::star::beans::NamedValue;
using ::com::sun::star::container::XIndexContainer;
using ::com::sun::star::container::XNameContainer;
using ::com::sun::star::frame::XModel;
using ::com::sun::star::awt::XControlModel;
using ::com::sun::star::embed::XEmbeddedObject;
using ::com::sun::star::embed::XEmbedPersist;
using ::com::sun::star::drawing::XControlShape;
using ::com::sun::star::drawing::XShape;
using ::com::sun::star::form::XFormComponent;
using ::com::sun::star::form::XFormsSupplier;
using ::com::sun::star::form::binding::XBindableValue;
using ::com::sun::star::form::binding::XValueBinding;
using ::com::sun::star::form::binding::XListEntrySink;
using ::com::sun::star::form::binding::XListEntrySource;
using ::com::sun::star::script::ScriptEventDescriptor;
using ::com::sun::star::script::XEventAttacherManager;
using ::com::sun::star::table::CellAddress;
using ::com::sun::star::table::CellRangeAddress;

// Drawing objects ============================================================

XclImpDrawObjBase::XclImpDrawObjBase( const XclImpRoot& rRoot ) :
    XclImpRoot( rRoot ),
    mnObjId( EXC_OBJ_INVALID_ID ),
    mnTab( 0 ),
    mnObjType( EXC_OBJTYPE_UNKNOWN ),
    mnDffShapeId( 0 ),
    mnDffFlags( ShapeFlag::NONE ),
    mbHasAnchor( false ),
    mbHidden( false ),
    mbVisible( true ),
    mbPrintable( true ),
    mbAreaObj( false ),
    mbAutoMargin( true ),
    mbSimpleMacro( true ),
    mbProcessSdr( true ),
    mbInsertSdr( true ),
    mbCustomDff( false ),
    mbNotifyMacroEventRead( false )
{
}

XclImpDrawObjBase::~XclImpDrawObjBase()
{
}

XclImpDrawObjRef XclImpDrawObjBase::ReadObj3( const XclImpRoot& rRoot, XclImpStream& rStrm )
{
    XclImpDrawObjRef xDrawObj;

    if( rStrm.GetRecLeft() >= 30 )
    {
        sal_uInt16 nObjType;
        rStrm.Ignore( 4 );
        nObjType = rStrm.ReaduInt16();
        switch( nObjType )
        {
            case EXC_OBJTYPE_GROUP:         xDrawObj= std::make_shared<XclImpGroupObj>( rRoot );          break;
            case EXC_OBJTYPE_LINE:          xDrawObj= std::make_shared<XclImpLineObj>( rRoot );           break;
            case EXC_OBJTYPE_RECTANGLE:     xDrawObj= std::make_shared<XclImpRectObj>( rRoot );           break;
            case EXC_OBJTYPE_OVAL:          xDrawObj= std::make_shared<XclImpOvalObj>( rRoot );           break;
            case EXC_OBJTYPE_ARC:           xDrawObj= std::make_shared<XclImpArcObj>( rRoot );            break;
            case EXC_OBJTYPE_CHART:         xDrawObj= std::make_shared<XclImpChartObj>( rRoot );          break;
            case EXC_OBJTYPE_TEXT:          xDrawObj= std::make_shared<XclImpTextObj>( rRoot );           break;
            case EXC_OBJTYPE_BUTTON:        xDrawObj= std::make_shared<XclImpButtonObj>( rRoot );         break;
            case EXC_OBJTYPE_PICTURE:       xDrawObj= std::make_shared<XclImpPictureObj>( rRoot );        break;
            default:
                SAL_WARN("sc.filter",  "XclImpDrawObjBase::ReadObj3 - unknown object type 0x" << std::hex << nObjType );
                rRoot.GetTracer().TraceUnsupportedObjects();
        }
    }

    if (!xDrawObj)
    {
        xDrawObj = std::make_shared<XclImpPhObj>(rRoot);
    }

    xDrawObj->mnTab = rRoot.GetCurrScTab();
    xDrawObj->ImplReadObj3( rStrm );
    return xDrawObj;
}

XclImpDrawObjRef XclImpDrawObjBase::ReadObj4( const XclImpRoot& rRoot, XclImpStream& rStrm )
{
    XclImpDrawObjRef xDrawObj;

    if( rStrm.GetRecLeft() >= 30 )
    {
        sal_uInt16 nObjType;
        rStrm.Ignore( 4 );
        nObjType = rStrm.ReaduInt16();
        switch( nObjType )
        {
            case EXC_OBJTYPE_GROUP:         xDrawObj = std::make_shared<XclImpGroupObj>( rRoot );          break;
            case EXC_OBJTYPE_LINE:          xDrawObj = std::make_shared<XclImpLineObj>( rRoot );           break;
            case EXC_OBJTYPE_RECTANGLE:     xDrawObj = std::make_shared<XclImpRectObj>( rRoot );           break;
            case EXC_OBJTYPE_OVAL:          xDrawObj = std::make_shared<XclImpOvalObj>( rRoot );           break;
            case EXC_OBJTYPE_ARC:           xDrawObj = std::make_shared<XclImpArcObj>( rRoot );            break;
            case EXC_OBJTYPE_CHART:         xDrawObj = std::make_shared<XclImpChartObj>( rRoot );          break;
            case EXC_OBJTYPE_TEXT:          xDrawObj = std::make_shared<XclImpTextObj>( rRoot );           break;
            case EXC_OBJTYPE_BUTTON:        xDrawObj = std::make_shared<XclImpButtonObj>( rRoot );         break;
            case EXC_OBJTYPE_PICTURE:       xDrawObj = std::make_shared<XclImpPictureObj>( rRoot );        break;
            case EXC_OBJTYPE_POLYGON:       xDrawObj = std::make_shared<XclImpPolygonObj>( rRoot );        break;
            default:
                SAL_WARN("sc.filter",  "XclImpDrawObjBase::ReadObj4 - unknown object type 0x" << std::hex << nObjType );
                rRoot.GetTracer().TraceUnsupportedObjects();
        }
    }

    if (!xDrawObj)
    {
        xDrawObj = std::make_shared<XclImpPhObj>(rRoot);
    }

    xDrawObj->mnTab = rRoot.GetCurrScTab();
    xDrawObj->ImplReadObj4( rStrm );
    return xDrawObj;
}

XclImpDrawObjRef XclImpDrawObjBase::ReadObj5( const XclImpRoot& rRoot, XclImpStream& rStrm )
{
    XclImpDrawObjRef xDrawObj;

    if( rStrm.GetRecLeft() >= 34 )
    {
        sal_uInt16 nObjType(EXC_OBJTYPE_UNKNOWN);
        rStrm.Ignore( 4 );
        nObjType = rStrm.ReaduInt16();
        switch( nObjType )
        {
            case EXC_OBJTYPE_GROUP:         xDrawObj = std::make_shared<XclImpGroupObj>( rRoot );          break;
            case EXC_OBJTYPE_LINE:          xDrawObj = std::make_shared<XclImpLineObj>( rRoot );           break;
            case EXC_OBJTYPE_RECTANGLE:     xDrawObj = std::make_shared<XclImpRectObj>( rRoot );           break;
            case EXC_OBJTYPE_OVAL:          xDrawObj = std::make_shared<XclImpOvalObj>( rRoot );           break;
            case EXC_OBJTYPE_ARC:           xDrawObj = std::make_shared<XclImpArcObj>( rRoot );            break;
            case EXC_OBJTYPE_CHART:         xDrawObj = std::make_shared<XclImpChartObj>( rRoot );          break;
            case EXC_OBJTYPE_TEXT:          xDrawObj = std::make_shared<XclImpTextObj>( rRoot );           break;
            case EXC_OBJTYPE_BUTTON:        xDrawObj = std::make_shared<XclImpButtonObj>( rRoot );         break;
            case EXC_OBJTYPE_PICTURE:       xDrawObj = std::make_shared<XclImpPictureObj>( rRoot );        break;
            case EXC_OBJTYPE_POLYGON:       xDrawObj = std::make_shared<XclImpPolygonObj>( rRoot );        break;
            case EXC_OBJTYPE_CHECKBOX:      xDrawObj = std::make_shared<XclImpCheckBoxObj>( rRoot );       break;
            case EXC_OBJTYPE_OPTIONBUTTON:  xDrawObj = std::make_shared<XclImpOptionButtonObj>( rRoot );   break;
            case EXC_OBJTYPE_EDIT:          xDrawObj = std::make_shared<XclImpEditObj>( rRoot );           break;
            case EXC_OBJTYPE_LABEL:         xDrawObj = std::make_shared<XclImpLabelObj>( rRoot );          break;
            case EXC_OBJTYPE_DIALOG:        xDrawObj = std::make_shared<XclImpDialogObj>( rRoot );         break;
            case EXC_OBJTYPE_SPIN:          xDrawObj = std::make_shared<XclImpSpinButtonObj>( rRoot );     break;
            case EXC_OBJTYPE_SCROLLBAR:     xDrawObj = std::make_shared<XclImpScrollBarObj>( rRoot );      break;
            case EXC_OBJTYPE_LISTBOX:       xDrawObj = std::make_shared<XclImpListBoxObj>( rRoot );        break;
            case EXC_OBJTYPE_GROUPBOX:      xDrawObj = std::make_shared<XclImpGroupBoxObj>( rRoot );       break;
            case EXC_OBJTYPE_DROPDOWN:      xDrawObj = std::make_shared<XclImpDropDownObj>( rRoot );       break;
            default:
                SAL_WARN("sc.filter",  "XclImpDrawObjBase::ReadObj5 - unknown object type 0x" << std::hex << nObjType );
                rRoot.GetTracer().TraceUnsupportedObjects();
                xDrawObj = std::make_shared<XclImpPhObj>( rRoot );
        }
    }

    OSL_ENSURE(xDrawObj, "object import failed");

    if (xDrawObj)
    {
        xDrawObj->mnTab = rRoot.GetCurrScTab();
        xDrawObj->ImplReadObj5( rStrm );
    }
    return xDrawObj;
}

XclImpDrawObjRef XclImpDrawObjBase::ReadObj8( const XclImpRoot& rRoot, XclImpStream& rStrm )
{
    XclImpDrawObjRef xDrawObj;

    if( rStrm.GetRecLeft() >= 10 )
    {
        sal_uInt16 nSubRecId(0), nSubRecSize(0), nObjType(0);
        nSubRecId = rStrm.ReaduInt16();
        nSubRecSize = rStrm.ReaduInt16();
        nObjType = rStrm.ReaduInt16();
        OSL_ENSURE( nSubRecId == EXC_ID_OBJCMO, "XclImpDrawObjBase::ReadObj8 - OBJCMO subrecord expected" );
        if( (nSubRecId == EXC_ID_OBJCMO) && (nSubRecSize >= 6) )
        {
            switch( nObjType )
            {
                // in BIFF8, all simple objects support text
                case EXC_OBJTYPE_LINE:
                case EXC_OBJTYPE_ARC:
                    xDrawObj = std::make_shared<XclImpTextObj>( rRoot );
                    // lines and arcs may be 2-dimensional
                    xDrawObj->SetAreaObj( false );
                break;

                // in BIFF8, all simple objects support text
                case EXC_OBJTYPE_RECTANGLE:
                case EXC_OBJTYPE_OVAL:
                case EXC_OBJTYPE_POLYGON:
                case EXC_OBJTYPE_DRAWING:
                case EXC_OBJTYPE_TEXT:
                    xDrawObj = std::make_shared<XclImpTextObj>( rRoot );
                break;

                case EXC_OBJTYPE_GROUP:         xDrawObj = std::make_shared<XclImpGroupObj>( rRoot );          break;
                case EXC_OBJTYPE_CHART:         xDrawObj = std::make_shared<XclImpChartObj>( rRoot );          break;
                case EXC_OBJTYPE_BUTTON:        xDrawObj = std::make_shared<XclImpButtonObj>( rRoot );         break;
                case EXC_OBJTYPE_PICTURE:       xDrawObj = std::make_shared<XclImpPictureObj>( rRoot );        break;
                case EXC_OBJTYPE_CHECKBOX:      xDrawObj = std::make_shared<XclImpCheckBoxObj>( rRoot );       break;
                case EXC_OBJTYPE_OPTIONBUTTON:  xDrawObj = std::make_shared<XclImpOptionButtonObj>( rRoot );   break;
                case EXC_OBJTYPE_EDIT:          xDrawObj = std::make_shared<XclImpEditObj>( rRoot );           break;
                case EXC_OBJTYPE_LABEL:         xDrawObj = std::make_shared<XclImpLabelObj>( rRoot );          break;
                case EXC_OBJTYPE_DIALOG:        xDrawObj = std::make_shared<XclImpDialogObj>( rRoot );         break;
                case EXC_OBJTYPE_SPIN:          xDrawObj = std::make_shared<XclImpSpinButtonObj>( rRoot );     break;
                case EXC_OBJTYPE_SCROLLBAR:     xDrawObj = std::make_shared<XclImpScrollBarObj>( rRoot );      break;
                case EXC_OBJTYPE_LISTBOX:       xDrawObj = std::make_shared<XclImpListBoxObj>( rRoot );        break;
                case EXC_OBJTYPE_GROUPBOX:      xDrawObj = std::make_shared<XclImpGroupBoxObj>( rRoot );       break;
                case EXC_OBJTYPE_DROPDOWN:      xDrawObj = std::make_shared<XclImpDropDownObj>( rRoot );       break;
                case EXC_OBJTYPE_NOTE:          xDrawObj = std::make_shared<XclImpNoteObj>( rRoot );           break;

                default:
                    SAL_WARN("sc.filter",  "XclImpDrawObjBase::ReadObj8 - unknown object type 0x" << std::hex << nObjType );
                    rRoot.GetTracer().TraceUnsupportedObjects();
            }
        }
    }

    if (!xDrawObj) //ensure placeholder for unknown or broken records
    {
        SAL_WARN( "sc.filter", "XclImpDrawObjBase::ReadObj8 import failed, substituting placeholder");
        xDrawObj = std::make_shared<XclImpPhObj>( rRoot );
    }

    xDrawObj->mnTab = rRoot.GetCurrScTab();
    xDrawObj->ImplReadObj8( rStrm );
    return xDrawObj;
}

void XclImpDrawObjBase::SetAnchor( const XclObjAnchor& rAnchor )
{
    maAnchor = rAnchor;
    mbHasAnchor = true;
}

const tools::Rectangle& XclImpDrawObjBase::GetDffRect() const
{
    return maDffRect;
}

void XclImpDrawObjBase::SetDffData(
    const DffObjData& rDffObjData, const OUString& rObjName, const OUString& rHyperlink,
    bool bVisible, bool bAutoMargin )
{
    mnDffShapeId = rDffObjData.nShapeId;
    mnDffFlags = rDffObjData.nSpFlags;
    maObjName = rObjName;
    maHyperlink = rHyperlink;
    mbVisible = bVisible;
    mbAutoMargin = bAutoMargin;
    maDffRect = rDffObjData.aChildAnchor;
}

OUString XclImpDrawObjBase::GetObjName() const
{
    /*  #i51348# Always return a non-empty name. Create English
        default names depending on the object type. This is not implemented as
        virtual functions in derived classes, as class type and object type may
        not match. */
    return maObjName.isEmpty() ? GetObjectManager().GetDefaultObjName(*this) : maObjName;
}

const XclObjAnchor* XclImpDrawObjBase::GetAnchor() const
{
    return mbHasAnchor ? &maAnchor : nullptr;
}

bool XclImpDrawObjBase::IsValidSize( const tools::Rectangle& rAnchorRect ) const
{
    // XclObjAnchor rounds up the width, width of 3 is the result of an Excel width of 0
    return mbAreaObj ?
        ((rAnchorRect.GetWidth() > 3) && (rAnchorRect.GetHeight() > 1)) :
        ((rAnchorRect.GetWidth() > 3) || (rAnchorRect.GetHeight() > 1));
}

ScRange XclImpDrawObjBase::GetUsedArea( SCTAB nScTab ) const
{
    ScRange aScUsedArea( ScAddress::INITIALIZE_INVALID );
    // #i44077# object inserted -> update used area for OLE object import
    if( mbHasAnchor && GetAddressConverter().ConvertRange( aScUsedArea, maAnchor, nScTab, nScTab, false ) )
    {
        // reduce range, if object ends directly on borders between two columns or rows
        if( (maAnchor.mnRX == 0) && (aScUsedArea.aStart.Col() < aScUsedArea.aEnd.Col()) )
            aScUsedArea.aEnd.IncCol( -1 );
        if( (maAnchor.mnBY == 0) && (aScUsedArea.aStart.Row() < aScUsedArea.aEnd.Row()) )
            aScUsedArea.aEnd.IncRow( -1 );
    }
    return aScUsedArea;
}

std::size_t XclImpDrawObjBase::GetProgressSize() const
{
    return DoGetProgressSize();
}

rtl::Reference<SdrObject> XclImpDrawObjBase::CreateSdrObject( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect, bool bIsDff ) const
{
    rtl::Reference<SdrObject> xSdrObj;
    if( bIsDff && !mbCustomDff )
    {
        rDffConv.Progress( GetProgressSize() );
    }
    else
    {
        xSdrObj = DoCreateSdrObj( rDffConv, rAnchorRect );

        //added for exporting OCX control
        /*  mnObjType value set should be as below table:
                    0x0000      Group               0x0001      Line
                    0x0002      Rectangle           0x0003      Oval
                    0x0004      Arc                 0x0005      Chart
                    0x0006      Text                    0x0009      Polygon
                +-----------------------------------------------------+
        OCX ==>|    0x0008      Picture                                     |
                +-----------------------------------------------------+
                |   0x0007      Button                                      |
                |   0x000B      Checkbox            0x000C      Radio button    |
                |   0x000D      Edit box                0x000E      Label       |
        TBX ==> |   0x000F      Dialog box          0x0010      Spin control    |
                |   0x0011      Scrollbar               0x0012      List            |
                |   0x0013      Group box           0x0014      Dropdown list   |
                +-----------------------------------------------------+
                    0x0019      Note                0x001E      OfficeArt object
        */
        if( xSdrObj && xSdrObj->IsUnoObj() &&
            ( (mnObjType < 25 && mnObjType > 10) || mnObjType == 7 || mnObjType == 8 ) )
        {
            SdrUnoObj* pSdrUnoObj = dynamic_cast< SdrUnoObj* >( xSdrObj.get() );
            if( pSdrUnoObj != nullptr )
            {
                const Reference< XControlModel >& xCtrlModel = pSdrUnoObj->GetUnoControlModel();
                Reference< XPropertySet > xPropSet(xCtrlModel,UNO_QUERY);
                static constexpr OUString sPropertyName(u"ControlTypeinMSO"_ustr);

                enum { eCreateFromOffice = 0, eCreateFromMSTBXControl, eCreateFromMSOCXControl };

                if( mnObjType == 7 || (mnObjType < 25 && mnObjType > 10) )//TBX
                {
                    try
                    {
                        //Need summary type for export. Detail type(checkbox, button ...) has been contained by mnObjType
                        const sal_Int16 nTBXControlType = eCreateFromMSTBXControl ;
                        xPropSet->setPropertyValue(sPropertyName, Any(nTBXControlType));
                    }
                    catch(const Exception&)
                    {
                        SAL_WARN("sc.filter", "XclImpDrawObjBase::CreateSdrObject, this control can't be set the property ControlTypeinMSO!");
                    }
                }
                if( mnObjType == 8 )//OCX
                {
                    //Need summary type for export
                    static constexpr OUStringLiteral sObjIdPropertyName(u"ObjIDinMSO");
                    const XclImpPictureObj* const pObj = dynamic_cast< const XclImpPictureObj* const >(this);
                    if( pObj != nullptr && pObj->IsOcxControl() )
                    {
                        try
                        {
                            const sal_Int16 nOCXControlType =  eCreateFromMSOCXControl;
                            xPropSet->setPropertyValue(sPropertyName, Any(nOCXControlType));
                            //Detail type(checkbox, button ...)
                            xPropSet->setPropertyValue(sObjIdPropertyName, Any(sal_uInt16(mnObjId)));
                        }
                        catch(const Exception&)
                        {
                            SAL_WARN("sc.filter", "XclImpDrawObjBase::CreateSdrObject, this control can't be set the property ObjIDinMSO!");
                        }
                    }
                }

            }
        }
    }
    return xSdrObj;
}

void XclImpDrawObjBase::NotifyMacroEventRead()
{
    if (mbNotifyMacroEventRead)
        return;
    ScDocShell* pDocShell = GetDocShell();
    if (!pDocShell)
        return;
    comphelper::DocumentInfo::notifyMacroEventRead(pDocShell->GetModel());
    mbNotifyMacroEventRead = true;
}

void XclImpDrawObjBase::PreProcessSdrObject( XclImpDffConverter& rDffConv, SdrObject& rSdrObj )
{
    // default: front layer, derived classes may have to set other layer in DoPreProcessSdrObj()
    rSdrObj.NbcSetLayer( SC_LAYER_FRONT );

    // set object name (GetObjName() will always return a non-empty name)
    rSdrObj.SetName( GetObjName() );

    // #i39167# full width for all objects regardless of horizontal alignment
    rSdrObj.SetMergedItem( SdrTextHorzAdjustItem( SDRTEXTHORZADJUST_BLOCK ) );

    // automatic text margin
    if( mbAutoMargin )
    {
        sal_Int32 nMargin = rDffConv.GetDefaultTextMargin();
        rSdrObj.SetMergedItem( makeSdrTextLeftDistItem( nMargin ) );
        rSdrObj.SetMergedItem( makeSdrTextRightDistItem( nMargin ) );
        rSdrObj.SetMergedItem( makeSdrTextUpperDistItem( nMargin ) );
        rSdrObj.SetMergedItem( makeSdrTextLowerDistItem( nMargin ) );
    }

    // macro and hyperlink
    // removed oracle/sun check for mbSimpleMacro ( no idea what its for )
    if (!maMacroName.isEmpty())
    {
        if( ScMacroInfo* pInfo = ScDrawLayer::GetMacroInfo( &rSdrObj, true ) )
        {
            OUString sMacro = XclTools::GetSbMacroUrl(maMacroName, GetDocShell());
            if (!sMacro.isEmpty())
                NotifyMacroEventRead();
            pInfo->SetMacro(sMacro);
        }
    }
    if (!maHyperlink.isEmpty())
        rSdrObj.setHyperlink(maHyperlink);

    // call virtual function for object type specific processing
    DoPreProcessSdrObj( rDffConv, rSdrObj );
}

void XclImpDrawObjBase::PostProcessSdrObject( XclImpDffConverter& rDffConv, SdrObject& rSdrObj ) const
{
    // call virtual function for object type specific processing
    DoPostProcessSdrObj( rDffConv, rSdrObj );
}

// protected ------------------------------------------------------------------

void XclImpDrawObjBase::ReadName5( XclImpStream& rStrm, sal_uInt16 nNameLen )
{
    maObjName.clear();
    if( nNameLen > 0 )
    {
        // name length field is repeated before the name
        maObjName = rStrm.ReadByteString( false );
        // skip padding byte for word boundaries
        if( rStrm.GetRecPos() & 1 ) rStrm.Ignore( 1 );
    }
}

void XclImpDrawObjBase::ReadMacro3( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    maMacroName.clear();
    rStrm.Ignore( nMacroSize );
    // skip padding byte for word boundaries, not contained in nMacroSize
    if( rStrm.GetRecPos() & 1 ) rStrm.Ignore( 1 );
}

void XclImpDrawObjBase::ReadMacro4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    maMacroName.clear();
    rStrm.Ignore( nMacroSize );
}

void XclImpDrawObjBase::ReadMacro5( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    maMacroName.clear();
    rStrm.Ignore( nMacroSize );
}

void XclImpDrawObjBase::ReadMacro8( XclImpStream& rStrm )
{
    maMacroName.clear();
    if( rStrm.GetRecLeft() <= 6 )
        return;

    // macro is stored in a tNameXR token containing a link to a defined name
    sal_uInt16 nFmlaSize;
    nFmlaSize = rStrm.ReaduInt16();
    rStrm.Ignore( 4 );
    OSL_ENSURE( nFmlaSize == 7, "XclImpDrawObjBase::ReadMacro - unexpected formula size" );
    if( nFmlaSize == 7 )
    {
        sal_uInt8 nTokenId;
        sal_uInt16 nExtSheet, nExtName;
        nTokenId = rStrm.ReaduInt8();
        nExtSheet = rStrm.ReaduInt16();
        nExtName = rStrm.ReaduInt16();
        OSL_ENSURE( nTokenId == XclTokenArrayHelper::GetTokenId( EXC_TOKID_NAMEX, EXC_TOKCLASS_REF ),
            "XclImpDrawObjBase::ReadMacro - tNameXR token expected" );
        if( nTokenId == XclTokenArrayHelper::GetTokenId( EXC_TOKID_NAMEX, EXC_TOKCLASS_REF ) )
            maMacroName = GetLinkManager().GetMacroName( nExtSheet, nExtName );
    }
}

void XclImpDrawObjBase::ConvertLineStyle( SdrObject& rSdrObj, const XclObjLineData& rLineData ) const
{
    if( rLineData.IsAuto() )
    {
        XclObjLineData aAutoData;
        aAutoData.mnAuto = 0;
        ConvertLineStyle( rSdrObj, aAutoData );
    }
    else
    {
        tools::Long nLineWidth = 35 * ::std::min( rLineData.mnWidth, EXC_OBJ_LINE_THICK );
        rSdrObj.SetMergedItem( XLineWidthItem( nLineWidth ) );
        rSdrObj.SetMergedItem( XLineColorItem( OUString(), GetPalette().GetColor( rLineData.mnColorIdx ) ) );
        rSdrObj.SetMergedItem( XLineJointItem( css::drawing::LineJoint_MITER ) );

        sal_uLong nDotLen = ::std::max< sal_uLong >( 70 * rLineData.mnWidth, 35 );
        sal_uLong nDashLen = 3 * nDotLen;
        sal_uLong nDist = 2 * nDotLen;

        switch( rLineData.mnStyle )
        {
            default:
            case EXC_OBJ_LINE_SOLID:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_SOLID ) );
            break;
            case EXC_OBJ_LINE_DASH:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_DASH ) );
                rSdrObj.SetMergedItem( XLineDashItem( OUString(), XDash( css::drawing::DashStyle_RECT, 0, nDotLen, 1, nDashLen, nDist ) ) );
            break;
            case EXC_OBJ_LINE_DOT:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_DASH ) );
                rSdrObj.SetMergedItem( XLineDashItem( OUString(), XDash( css::drawing::DashStyle_RECT, 1, nDotLen, 0, nDashLen, nDist ) ) );
            break;
            case EXC_OBJ_LINE_DASHDOT:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_DASH ) );
                rSdrObj.SetMergedItem( XLineDashItem( OUString(), XDash( css::drawing::DashStyle_RECT, 1, nDotLen, 1, nDashLen, nDist ) ) );
            break;
            case EXC_OBJ_LINE_DASHDOTDOT:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_DASH ) );
                rSdrObj.SetMergedItem( XLineDashItem( OUString(), XDash( css::drawing::DashStyle_RECT, 2, nDotLen, 1, nDashLen, nDist ) ) );
            break;
            case EXC_OBJ_LINE_MEDTRANS:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_SOLID ) );
                rSdrObj.SetMergedItem( XLineTransparenceItem( 50 ) );
            break;
            case EXC_OBJ_LINE_DARKTRANS:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_SOLID ) );
                rSdrObj.SetMergedItem( XLineTransparenceItem( 25 ) );
            break;
            case EXC_OBJ_LINE_LIGHTTRANS:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_SOLID ) );
                rSdrObj.SetMergedItem( XLineTransparenceItem( 75 ) );
            break;
            case EXC_OBJ_LINE_NONE:
                rSdrObj.SetMergedItem( XLineStyleItem( drawing::LineStyle_NONE ) );
            break;
        }
    }
}

void XclImpDrawObjBase::ConvertFillStyle( SdrObject& rSdrObj, const XclObjFillData& rFillData ) const
{
    if( rFillData.IsAuto() )
    {
        XclObjFillData aAutoData;
        aAutoData.mnAuto = 0;
        ConvertFillStyle( rSdrObj, aAutoData );
    }
    else if( rFillData.mnPattern == EXC_PATT_NONE )
    {
        rSdrObj.SetMergedItem( XFillStyleItem( drawing::FillStyle_NONE ) );
    }
    else
    {
        Color aPattColor = GetPalette().GetColor( rFillData.mnPattColorIdx );
        Color aBackColor = GetPalette().GetColor( rFillData.mnBackColorIdx );
        if( (rFillData.mnPattern == EXC_PATT_SOLID) || (aPattColor == aBackColor) )
        {
            rSdrObj.SetMergedItem( XFillStyleItem( drawing::FillStyle_SOLID ) );
            rSdrObj.SetMergedItem( XFillColorItem( OUString(), aPattColor ) );
        }
        else
        {
            static const sal_uInt8 sppnPatterns[][ 8 ] =
            {
                { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
                { 0x77, 0xDD, 0x77, 0xDD, 0x77, 0xDD, 0x77, 0xDD },
                { 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22 },
                { 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00 },
                { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC },
                { 0x33, 0x66, 0xCC, 0x99, 0x33, 0x66, 0xCC, 0x99 },
                { 0xCC, 0x66, 0x33, 0x99, 0xCC, 0x66, 0x33, 0x99 },
                { 0xCC, 0xCC, 0x33, 0x33, 0xCC, 0xCC, 0x33, 0x33 },
                { 0xCC, 0xFF, 0x33, 0xFF, 0xCC, 0xFF, 0x33, 0xFF },
                { 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },
                { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 },
                { 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 },
                { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 },
                { 0xFF, 0x11, 0x11, 0x11, 0xFF, 0x11, 0x11, 0x11 },
                { 0xAA, 0x44, 0xAA, 0x11, 0xAA, 0x44, 0xAA, 0x11 },
                { 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 },
                { 0x80, 0x00, 0x08, 0x00, 0x80, 0x00, 0x08, 0x00 }
            };
            const sal_uInt8* const pnPattern = sppnPatterns[std::min<size_t>(rFillData.mnPattern - 2, SAL_N_ELEMENTS(sppnPatterns) - 1)];
            // create 2-colored 8x8 DIB
            SvMemoryStream aMemStrm;
            aMemStrm.WriteUInt32( 12 ).WriteInt16( 8 ).WriteInt16( 8 ).WriteUInt16( 1 ).WriteUInt16( 1 );
            aMemStrm.WriteUChar( 0xFF ).WriteUChar( 0xFF ).WriteUChar( 0xFF );
            aMemStrm.WriteUChar( 0x00 ).WriteUChar( 0x00 ).WriteUChar( 0x00 );
            for( size_t nIdx = 0; nIdx < 8; ++nIdx )
                aMemStrm.WriteUInt32( pnPattern[ nIdx ] ); // 32-bit little-endian
            aMemStrm.Seek( STREAM_SEEK_TO_BEGIN );
            Bitmap aBitmap;
            (void)ReadDIB(aBitmap, aMemStrm, false);

            XOBitmap aXOBitmap( aBitmap );
            aXOBitmap.Bitmap2Array();
            if( aXOBitmap.GetBackgroundColor() == COL_BLACK )
                ::std::swap( aPattColor, aBackColor );
            aXOBitmap.SetPixelColor( aPattColor );
            aXOBitmap.SetBackgroundColor( aBackColor );
            aXOBitmap.Array2Bitmap();
            aBitmap = aXOBitmap.GetBitmap();

            rSdrObj.SetMergedItem(XFillStyleItem(drawing::FillStyle_BITMAP));
            rSdrObj.SetMergedItem(XFillBitmapItem(OUString(), Graphic(BitmapEx(aBitmap))));
        }
    }
}

void XclImpDrawObjBase::ConvertFrameStyle( SdrObject& rSdrObj, sal_uInt16 nFrameFlags ) const
{
    if( ::get_flag( nFrameFlags, EXC_OBJ_FRAME_SHADOW ) )
    {
        rSdrObj.SetMergedItem( makeSdrShadowItem( true ) );
        rSdrObj.SetMergedItem( makeSdrShadowXDistItem( 35 ) );
        rSdrObj.SetMergedItem( makeSdrShadowYDistItem( 35 ) );
        rSdrObj.SetMergedItem( makeSdrShadowColorItem( GetPalette().GetColor( EXC_COLOR_WINDOWTEXT ) ) );
    }
}

Color XclImpDrawObjBase::GetSolidLineColor( const XclObjLineData& rLineData ) const
{
    Color aColor( COL_TRANSPARENT );
    if( rLineData.IsAuto() )
    {
        XclObjLineData aAutoData;
        aAutoData.mnAuto = 0;
        aColor = GetSolidLineColor( aAutoData );
    }
    else if( rLineData.mnStyle != EXC_OBJ_LINE_NONE )
    {
        aColor = GetPalette().GetColor( rLineData.mnColorIdx );
    }
    return aColor;
}

Color XclImpDrawObjBase::GetSolidFillColor( const XclObjFillData& rFillData ) const
{
    Color aColor( COL_TRANSPARENT );
    if( rFillData.IsAuto() )
    {
        XclObjFillData aAutoData;
        aAutoData.mnAuto = 0;
        aColor = GetSolidFillColor( aAutoData );
    }
    else if( rFillData.mnPattern != EXC_PATT_NONE )
    {
        Color aPattColor = GetPalette().GetColor( rFillData.mnPattColorIdx );
        Color aBackColor = GetPalette().GetColor( rFillData.mnBackColorIdx );
        aColor = XclTools::GetPatternColor( aPattColor, aBackColor, rFillData.mnPattern );
    }
    return aColor;
}

void XclImpDrawObjBase::DoReadObj3( XclImpStream&, sal_uInt16 )
{
}

void XclImpDrawObjBase::DoReadObj4( XclImpStream&, sal_uInt16 )
{
}

void XclImpDrawObjBase::DoReadObj5( XclImpStream&, sal_uInt16, sal_uInt16 )
{
}

void XclImpDrawObjBase::DoReadObj8SubRec( XclImpStream&, sal_uInt16, sal_uInt16 )
{
}

std::size_t XclImpDrawObjBase::DoGetProgressSize() const
{
    return 1;
}

rtl::Reference<SdrObject> XclImpDrawObjBase::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& ) const
{
    rDffConv.Progress( GetProgressSize() );
    return nullptr;
}

void XclImpDrawObjBase::DoPreProcessSdrObj( XclImpDffConverter&, SdrObject& ) const
{
    // trace if object is not printable
    if( !IsPrintable() )
        GetTracer().TraceObjectNotPrintable();
}

void XclImpDrawObjBase::DoPostProcessSdrObj( XclImpDffConverter&, SdrObject& ) const
{
}

void XclImpDrawObjBase::ImplReadObj3( XclImpStream& rStrm )
{
    // back to offset 4 (ignore object count field)
    rStrm.Seek( 4 );

    sal_uInt16 nObjFlags, nMacroSize;
    mnObjType = rStrm.ReaduInt16();
    mnObjId = rStrm.ReaduInt16();
    nObjFlags = rStrm.ReaduInt16();
    rStrm >> maAnchor;
    nMacroSize = rStrm.ReaduInt16();
    rStrm.Ignore( 2 );

    mbHasAnchor = true;
    mbHidden = ::get_flag( nObjFlags, EXC_OBJ_HIDDEN );
    mbVisible = ::get_flag( nObjFlags, EXC_OBJ_VISIBLE );
    DoReadObj3( rStrm, nMacroSize );
}

void XclImpDrawObjBase::ImplReadObj4( XclImpStream& rStrm )
{
    // back to offset 4 (ignore object count field)
    rStrm.Seek( 4 );

    sal_uInt16 nObjFlags, nMacroSize;
    mnObjType = rStrm.ReaduInt16();
    mnObjId = rStrm.ReaduInt16();
    nObjFlags = rStrm.ReaduInt16();
    rStrm >> maAnchor;
    nMacroSize = rStrm.ReaduInt16();
    rStrm.Ignore( 2 );

    mbHasAnchor = true;
    mbHidden = ::get_flag( nObjFlags, EXC_OBJ_HIDDEN );
    mbVisible = ::get_flag( nObjFlags, EXC_OBJ_VISIBLE );
    mbPrintable = ::get_flag( nObjFlags, EXC_OBJ_PRINTABLE );
    DoReadObj4( rStrm, nMacroSize );
}

void XclImpDrawObjBase::ImplReadObj5( XclImpStream& rStrm )
{
    // back to offset 4 (ignore object count field)
    rStrm.Seek( 4 );

    sal_uInt16 nObjFlags, nMacroSize, nNameLen;
    mnObjType = rStrm.ReaduInt16();
    mnObjId = rStrm.ReaduInt16();
    nObjFlags = rStrm.ReaduInt16();
    rStrm >> maAnchor;
    nMacroSize = rStrm.ReaduInt16();
    rStrm.Ignore( 2 );
    nNameLen = rStrm.ReaduInt16();
    rStrm.Ignore( 2 );

    mbHasAnchor = true;
    mbHidden = ::get_flag( nObjFlags, EXC_OBJ_HIDDEN );
    mbVisible = ::get_flag( nObjFlags, EXC_OBJ_VISIBLE );
    mbPrintable = ::get_flag( nObjFlags, EXC_OBJ_PRINTABLE );
    DoReadObj5( rStrm, nNameLen, nMacroSize );
}

void XclImpDrawObjBase::ImplReadObj8( XclImpStream& rStrm )
{
    // back to beginning
    rStrm.Seek( EXC_REC_SEEK_TO_BEGIN );

    bool bLoop = true;
    while (bLoop)
    {
        if (rStrm.GetRecLeft() < 4)
            break;

        sal_uInt16 nSubRecId = rStrm.ReaduInt16();
        sal_uInt16 nSubRecSize = rStrm.ReaduInt16();
        rStrm.PushPosition();
        // sometimes the last subrecord has an invalid length (OBJLBSDATA) -> min()
        nSubRecSize = static_cast< sal_uInt16 >( ::std::min< std::size_t >( nSubRecSize, rStrm.GetRecLeft() ) );

        switch( nSubRecId )
        {
            case EXC_ID_OBJCMO:
                OSL_ENSURE( rStrm.GetRecPos() == 4, "XclImpDrawObjBase::ImplReadObj8 - unexpected OBJCMO subrecord" );
                if( (rStrm.GetRecPos() == 4) && (nSubRecSize >= 6) )
                {
                    sal_uInt16 nObjFlags;
                    mnObjType = rStrm.ReaduInt16();
                    mnObjId = rStrm.ReaduInt16(  );
                    nObjFlags = rStrm.ReaduInt16(  );
                    mbPrintable = ::get_flag( nObjFlags, EXC_OBJCMO_PRINTABLE );
                }
            break;
            case EXC_ID_OBJMACRO:
                ReadMacro8( rStrm );
            break;
            case EXC_ID_OBJEND:
                bLoop = false;
            break;
            default:
                DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
        }

        rStrm.PopPosition();
        rStrm.Ignore( nSubRecSize );
    }

    /*  Call DoReadObj8SubRec() with EXC_ID_OBJEND for further stream
        processing (e.g. charts), even if the OBJEND subrecord is missing. */
    DoReadObj8SubRec( rStrm, EXC_ID_OBJEND, 0 );

    /*  Pictures that Excel reads from BIFF5 and writes to BIFF8 still have the
        IMGDATA record following the OBJ record (but they use the image data
        stored in DFF). The IMGDATA record may be continued by several CONTINUE
        records. But the last CONTINUE record may be in fact an MSODRAWING
        record that contains the DFF data of the next drawing object! So we
        have to skip just enough CONTINUE records to look at the next
        MSODRAWING/CONTINUE record. */
    if( !((rStrm.GetNextRecId() == EXC_ID3_IMGDATA) && rStrm.StartNextRecord()) )
        return;

    rStrm.Ignore( 4 );
    sal_uInt32 nDataSize = rStrm.ReaduInt32();
    nDataSize -= rStrm.GetRecLeft();
    // skip following CONTINUE records until IMGDATA ends
    while (true)
    {
        if (!nDataSize)
            break;
        if (rStrm.GetNextRecId() != EXC_ID_CONT)
            break;
        if (!rStrm.StartNextRecord())
            break;
        OSL_ENSURE( nDataSize >= rStrm.GetRecLeft(), "XclImpDrawObjBase::ImplReadObj8 - CONTINUE too long" );
        nDataSize -= ::std::min< sal_uInt32 >( rStrm.GetRecLeft(), nDataSize );
    }
    OSL_ENSURE( nDataSize == 0, "XclImpDrawObjBase::ImplReadObj8 - missing CONTINUE records" );
    // next record may be MSODRAWING or CONTINUE or anything else
}

void XclImpDrawObjVector::InsertGrouped( XclImpDrawObjRef const & xDrawObj )
{
    if( !mObjs.empty() )
        if( XclImpGroupObj* pGroupObj = dynamic_cast< XclImpGroupObj* >( mObjs.back().get() ) )
            if( pGroupObj->TryInsert( xDrawObj ) )
                return;
    mObjs.push_back( xDrawObj );
}

std::size_t XclImpDrawObjVector::GetProgressSize() const
{
    return std::accumulate(mObjs.begin(), mObjs.end(), std::size_t(0),
        [](const std::size_t& rSum, const XclImpDrawObjRef& rxObj) { return rSum + rxObj->GetProgressSize(); });
}

XclImpPhObj::XclImpPhObj( const XclImpRoot& rRoot ) :
    XclImpDrawObjBase( rRoot )
{
    SetProcessSdrObj( false );
}

XclImpGroupObj::XclImpGroupObj( const XclImpRoot& rRoot ) :
    XclImpDrawObjBase( rRoot ),
    mnFirstUngrouped( 0 )
{
}

bool XclImpGroupObj::TryInsert( XclImpDrawObjRef const & xDrawObj )
{
    if( xDrawObj->GetObjId() == mnFirstUngrouped )
        return false;
    // insert into own list or into nested group
    maChildren.InsertGrouped( xDrawObj );
    return true;
}

void XclImpGroupObj::DoReadObj3( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    rStrm.Ignore( 4 );
    mnFirstUngrouped = rStrm.ReaduInt16();
    rStrm.Ignore( 16 );
    ReadMacro3( rStrm, nMacroSize );
}

void XclImpGroupObj::DoReadObj4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    rStrm.Ignore( 4 );
    mnFirstUngrouped = rStrm.ReaduInt16();
    rStrm.Ignore( 16 );
    ReadMacro4( rStrm, nMacroSize );
}

void XclImpGroupObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 nMacroSize )
{
    rStrm.Ignore( 4 );
    mnFirstUngrouped = rStrm.ReaduInt16();
    rStrm.Ignore( 16 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, nMacroSize );
}

std::size_t XclImpGroupObj::DoGetProgressSize() const
{
    return XclImpDrawObjBase::DoGetProgressSize() + maChildren.GetProgressSize();
}

rtl::Reference<SdrObject> XclImpGroupObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& /*rAnchorRect*/ ) const
{
    rtl::Reference<SdrObjGroup> xSdrObj(
        new SdrObjGroup(
            *GetDoc().GetDrawLayer()));
    // child objects in BIFF2-BIFF5 have absolute size, not needed to pass own anchor rectangle
    SdrObjList& rObjList = *xSdrObj->GetSubList();  // SdrObjGroup always returns existing sublist
    for( const auto& rxChild : maChildren )
        rDffConv.ProcessObject( rObjList, *rxChild );
    rDffConv.Progress();
    return xSdrObj;
}

XclImpLineObj::XclImpLineObj( const XclImpRoot& rRoot ) :
    XclImpDrawObjBase( rRoot ),
    mnArrows( 0 ),
    mnStartPoint( EXC_OBJ_LINE_TL )
{
    SetAreaObj( false );
}

void XclImpLineObj::DoReadObj3( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    rStrm >> maLineData;
    mnArrows = rStrm.ReaduInt16();
    mnStartPoint = rStrm.ReaduInt8();
    rStrm.Ignore( 1 );
    ReadMacro3( rStrm, nMacroSize );
}

void XclImpLineObj::DoReadObj4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    rStrm >> maLineData;
    mnArrows = rStrm.ReaduInt16();
    mnStartPoint = rStrm.ReaduInt8();
    rStrm.Ignore( 1 );
    ReadMacro4( rStrm, nMacroSize );
}

void XclImpLineObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 nMacroSize )
{
    rStrm >> maLineData;
    mnArrows = rStrm.ReaduInt16();
    mnStartPoint = rStrm.ReaduInt8();
    rStrm.Ignore( 1 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, nMacroSize );
}

rtl::Reference<SdrObject> XclImpLineObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    ::basegfx::B2DPolygon aB2DPolygon;
    switch( mnStartPoint )
    {
        default:
        case EXC_OBJ_LINE_TL:
            aB2DPolygon.append( ::basegfx::B2DPoint( rAnchorRect.Left(), rAnchorRect.Top() ) );
            aB2DPolygon.append( ::basegfx::B2DPoint( rAnchorRect.Right(), rAnchorRect.Bottom() ) );
        break;
        case EXC_OBJ_LINE_TR:
            aB2DPolygon.append( ::basegfx::B2DPoint( rAnchorRect.Right(), rAnchorRect.Top() ) );
            aB2DPolygon.append( ::basegfx::B2DPoint( rAnchorRect.Left(), rAnchorRect.Bottom() ) );
        break;
        case EXC_OBJ_LINE_BR:
            aB2DPolygon.append( ::basegfx::B2DPoint( rAnchorRect.Right(), rAnchorRect.Bottom() ) );
            aB2DPolygon.append( ::basegfx::B2DPoint( rAnchorRect.Left(), rAnchorRect.Top() ) );
        break;
        case EXC_OBJ_LINE_BL:
            aB2DPolygon.append( ::basegfx::B2DPoint( rAnchorRect.Left(), rAnchorRect.Bottom() ) );
            aB2DPolygon.append( ::basegfx::B2DPoint( rAnchorRect.Right(), rAnchorRect.Top() ) );
        break;
    }
    rtl::Reference<SdrObject> xSdrObj(
        new SdrPathObj(
            *GetDoc().GetDrawLayer(),
            SdrObjKind::Line,
            ::basegfx::B2DPolyPolygon(aB2DPolygon)));
    ConvertLineStyle( *xSdrObj, maLineData );

    // line ends
    sal_uInt8 nArrowType = ::extract_value< sal_uInt8 >( mnArrows, 0, 4 );
    bool bLineStart = false;
    bool bLineEnd = false;
    bool bFilled = false;
    switch( nArrowType )
    {
        case EXC_OBJ_ARROW_OPEN:        bLineStart = false; bLineEnd = true;  bFilled = false;  break;
        case EXC_OBJ_ARROW_OPENBOTH:    bLineStart = true;  bLineEnd = true;  bFilled = false;  break;
        case EXC_OBJ_ARROW_FILLED:      bLineStart = false; bLineEnd = true;  bFilled = true;   break;
        case EXC_OBJ_ARROW_FILLEDBOTH:  bLineStart = true;  bLineEnd = true;  bFilled = true;   break;
    }
    if( bLineStart || bLineEnd )
    {
        sal_uInt8 nArrowWidth = ::extract_value< sal_uInt8 >( mnArrows, 4, 4 );
        double fArrowWidth = 3.0;
        switch( nArrowWidth )
        {
            case EXC_OBJ_ARROW_NARROW:  fArrowWidth = 2.0;  break;
            case EXC_OBJ_ARROW_MEDIUM:  fArrowWidth = 3.0;  break;
            case EXC_OBJ_ARROW_WIDE:    fArrowWidth = 5.0;  break;
        }

        sal_uInt8 nArrowLength = ::extract_value< sal_uInt8 >( mnArrows, 8, 4 );
        double fArrowLength = 3.0;
        switch( nArrowLength )
        {
            case EXC_OBJ_ARROW_NARROW:  fArrowLength = 2.5; break;
            case EXC_OBJ_ARROW_MEDIUM:  fArrowLength = 3.5; break;
            case EXC_OBJ_ARROW_WIDE:    fArrowLength = 6.0; break;
        }

        ::basegfx::B2DPolygon aArrowPoly;
#define EXC_ARROW_POINT( x, y ) ::basegfx::B2DPoint( fArrowWidth * (x), fArrowLength * (y) )
        if( bFilled )
        {
            aArrowPoly.append( EXC_ARROW_POINT(   0, 100 ) );
            aArrowPoly.append( EXC_ARROW_POINT(  50,   0 ) );
            aArrowPoly.append( EXC_ARROW_POINT( 100, 100 ) );
        }
        else
        {
            sal_uInt8 nLineWidth = ::limit_cast< sal_uInt8 >( maLineData.mnWidth, EXC_OBJ_LINE_THIN, EXC_OBJ_LINE_THICK );
            aArrowPoly.append( EXC_ARROW_POINT( 50, 0 ) );
            aArrowPoly.append( EXC_ARROW_POINT( 100, 100 - 3 * nLineWidth ) );
            aArrowPoly.append( EXC_ARROW_POINT( 100 - 5 * nLineWidth, 100 ) );
            aArrowPoly.append( EXC_ARROW_POINT( 50, 12 * nLineWidth ) );
            aArrowPoly.append( EXC_ARROW_POINT( 5 * nLineWidth, 100 ) );
            aArrowPoly.append( EXC_ARROW_POINT( 0, 100 - 3 * nLineWidth ) );
        }
#undef EXC_ARROW_POINT

        tools::Long nWidth = static_cast< tools::Long >( 125 * fArrowWidth );
        if( bLineStart )
        {
            xSdrObj->SetMergedItem( XLineStartItem( OUString(), basegfx::B2DPolyPolygon(aArrowPoly) ) );
            xSdrObj->SetMergedItem( XLineStartWidthItem( nWidth ) );
            xSdrObj->SetMergedItem( XLineStartCenterItem( false ) );
        }
        if( bLineEnd )
        {
            xSdrObj->SetMergedItem( XLineEndItem( OUString(), basegfx::B2DPolyPolygon(aArrowPoly) ) );
            xSdrObj->SetMergedItem( XLineEndWidthItem( nWidth ) );
            xSdrObj->SetMergedItem( XLineEndCenterItem( false ) );
        }
    }
    rDffConv.Progress();
    return xSdrObj;
}

XclImpRectObj::XclImpRectObj( const XclImpRoot& rRoot ) :
    XclImpDrawObjBase( rRoot ),
    mnFrameFlags( 0 )
{
    SetAreaObj( true );
}

void XclImpRectObj::ReadFrameData( XclImpStream& rStrm )
{
    rStrm >> maFillData >> maLineData;
    mnFrameFlags = rStrm.ReaduInt16();
}

void XclImpRectObj::ConvertRectStyle( SdrObject& rSdrObj ) const
{
    ConvertLineStyle( rSdrObj, maLineData );
    ConvertFillStyle( rSdrObj, maFillData );
    ConvertFrameStyle( rSdrObj, mnFrameFlags );
}

void XclImpRectObj::DoReadObj3( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    ReadFrameData( rStrm );
    ReadMacro3( rStrm, nMacroSize );
}

void XclImpRectObj::DoReadObj4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    ReadFrameData( rStrm );
    ReadMacro4( rStrm, nMacroSize );
}

void XclImpRectObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 nMacroSize )
{
    ReadFrameData( rStrm );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, nMacroSize );
}

rtl::Reference<SdrObject> XclImpRectObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    rtl::Reference<SdrObject> xSdrObj(
        new SdrRectObj(
            *GetDoc().GetDrawLayer(),
            rAnchorRect));
    ConvertRectStyle( *xSdrObj );
    rDffConv.Progress();
    return xSdrObj;
}

XclImpOvalObj::XclImpOvalObj( const XclImpRoot& rRoot ) :
    XclImpRectObj( rRoot )
{
}

rtl::Reference<SdrObject> XclImpOvalObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    rtl::Reference<SdrObject> xSdrObj(
        new SdrCircObj(
            *GetDoc().GetDrawLayer(),
            SdrCircKind::Full,
            rAnchorRect));
    ConvertRectStyle( *xSdrObj );
    rDffConv.Progress();
    return xSdrObj;
}

XclImpArcObj::XclImpArcObj( const XclImpRoot& rRoot ) :
    XclImpDrawObjBase( rRoot ),
    mnQuadrant( EXC_OBJ_ARC_TR )
{
    SetAreaObj( false );    // arc may be 2-dimensional
}

void XclImpArcObj::DoReadObj3( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    rStrm >> maFillData >> maLineData;
    mnQuadrant = rStrm.ReaduInt8();
    rStrm.Ignore( 1 );
    ReadMacro3( rStrm, nMacroSize );
}

void XclImpArcObj::DoReadObj4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    rStrm >> maFillData >> maLineData;
    mnQuadrant  = rStrm.ReaduInt8();
    rStrm.Ignore( 1 );
    ReadMacro4( rStrm, nMacroSize );
}

void XclImpArcObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 nMacroSize )
{
    rStrm >> maFillData >> maLineData;
    mnQuadrant = rStrm.ReaduInt8();
    rStrm.Ignore( 1 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, nMacroSize );
}

rtl::Reference<SdrObject> XclImpArcObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    tools::Rectangle aNewRect = rAnchorRect;
    Degree100 nStartAngle;
    Degree100 nEndAngle;
    switch( mnQuadrant )
    {
        default:
        case EXC_OBJ_ARC_TR:
            nStartAngle = 0_deg100;
            nEndAngle = 9000_deg100;
            aNewRect.AdjustLeft( -(rAnchorRect.GetWidth()) );
            aNewRect.AdjustBottom(rAnchorRect.GetHeight() );
        break;
        case EXC_OBJ_ARC_TL:
            nStartAngle = 9000_deg100;
            nEndAngle = 18000_deg100;
            aNewRect.AdjustRight(rAnchorRect.GetWidth() );
            aNewRect.AdjustBottom(rAnchorRect.GetHeight() );
        break;
        case EXC_OBJ_ARC_BL:
            nStartAngle = 18000_deg100;
            nEndAngle = 27000_deg100;
            aNewRect.AdjustRight(rAnchorRect.GetWidth() );
            aNewRect.AdjustTop( -(rAnchorRect.GetHeight()) );
        break;
        case EXC_OBJ_ARC_BR:
            nStartAngle = 27000_deg100;
            nEndAngle = 0_deg100;
            aNewRect.AdjustLeft( -(rAnchorRect.GetWidth()) );
            aNewRect.AdjustTop( -(rAnchorRect.GetHeight()) );
        break;
    }
    SdrCircKind eObjKind = maFillData.IsFilled() ? SdrCircKind::Section : SdrCircKind::Arc;
    rtl::Reference<SdrObject> xSdrObj(
        new SdrCircObj(
            *GetDoc().GetDrawLayer(),
            eObjKind,
            aNewRect,
            nStartAngle,
            nEndAngle));
    ConvertFillStyle( *xSdrObj, maFillData );
    ConvertLineStyle( *xSdrObj, maLineData );
    rDffConv.Progress();
    return xSdrObj;
}

XclImpPolygonObj::XclImpPolygonObj( const XclImpRoot& rRoot ) :
    XclImpRectObj( rRoot ),
    mnPolyFlags( 0 ),
    mnPointCount( 0 )
{
    SetAreaObj( false );    // polygon may be 2-dimensional
}

void XclImpPolygonObj::ReadCoordList( XclImpStream& rStrm )
{
    if( (rStrm.GetNextRecId() == EXC_ID_COORDLIST) && rStrm.StartNextRecord() )
    {
        OSL_ENSURE( rStrm.GetRecLeft() / 4 == mnPointCount, "XclImpPolygonObj::ReadCoordList - wrong polygon point count" );
        while (true)
        {
            if (rStrm.GetRecLeft() < 4)
                break;
            sal_uInt16 nX = rStrm.ReaduInt16();
            sal_uInt16 nY = rStrm.ReaduInt16();
            maCoords.emplace_back( nX, nY );
        }
    }
}

void XclImpPolygonObj::DoReadObj4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    ReadFrameData( rStrm );
    mnPolyFlags = rStrm.ReaduInt16();
    rStrm.Ignore( 10 );
    mnPointCount = rStrm.ReaduInt16();
    rStrm.Ignore( 8 );
    ReadMacro4( rStrm, nMacroSize );
    ReadCoordList( rStrm );
}

void XclImpPolygonObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 nMacroSize )
{
    ReadFrameData( rStrm );
    mnPolyFlags = rStrm.ReaduInt16();
    rStrm.Ignore( 10 );
    mnPointCount = rStrm.ReaduInt16();
    rStrm.Ignore( 8 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, nMacroSize );
    ReadCoordList( rStrm );
}

namespace {

::basegfx::B2DPoint lclGetPolyPoint( const tools::Rectangle& rAnchorRect, const Point& rPoint )
{
    return ::basegfx::B2DPoint(
        rAnchorRect.Left() + static_cast< sal_Int32 >( ::std::min< double >( rPoint.X(), 16384.0 ) / 16384.0 * rAnchorRect.GetWidth() + 0.5 ),
        rAnchorRect.Top() + static_cast< sal_Int32 >( ::std::min< double >( rPoint.Y(), 16384.0 ) / 16384.0 * rAnchorRect.GetHeight() + 0.5 ) );
}

} // namespace

rtl::Reference<SdrObject> XclImpPolygonObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    rtl::Reference<SdrObject> xSdrObj;
    if( maCoords.size() >= 2 )
    {
        // create the polygon
        ::basegfx::B2DPolygon aB2DPolygon;
        for( const auto& rCoord : maCoords )
            aB2DPolygon.append( lclGetPolyPoint( rAnchorRect, rCoord ) );
        // close polygon if specified
        if( ::get_flag( mnPolyFlags, EXC_OBJ_POLY_CLOSED ) && (maCoords.front() != maCoords.back()) )
            aB2DPolygon.append( lclGetPolyPoint( rAnchorRect, maCoords.front() ) );
        // create the SdrObject
        SdrObjKind eObjKind = maFillData.IsFilled() ? SdrObjKind::PathPoly : SdrObjKind::PathPolyLine;
        xSdrObj =
            new SdrPathObj(
                *GetDoc().GetDrawLayer(),
                eObjKind,
                ::basegfx::B2DPolyPolygon(aB2DPolygon));
        ConvertRectStyle( *xSdrObj );
    }
    rDffConv.Progress();
    return xSdrObj;
}

void XclImpObjTextData::ReadByteString( XclImpStream& rStrm )
{
    mxString.reset();
    if( maData.mnTextLen > 0 )
    {
        mxString = std::make_shared<XclImpString>( rStrm.ReadRawByteString( maData.mnTextLen ) );
        // skip padding byte for word boundaries
        if( rStrm.GetRecPos() & 1 ) rStrm.Ignore( 1 );
    }
}

void XclImpObjTextData::ReadFormats( XclImpStream& rStrm )
{
    if( mxString )
        mxString->ReadObjFormats( rStrm, maData.mnFormatSize );
    else
        rStrm.Ignore( maData.mnFormatSize );
}

XclImpTextObj::XclImpTextObj( const XclImpRoot& rRoot ) :
    XclImpRectObj( rRoot )
{
}

void XclImpTextObj::DoReadObj3( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    ReadFrameData( rStrm );
    maTextData.maData.ReadObj3( rStrm );
    ReadMacro3( rStrm, nMacroSize );
    maTextData.ReadByteString( rStrm );
    maTextData.ReadFormats( rStrm );
}

void XclImpTextObj::DoReadObj4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    ReadFrameData( rStrm );
    maTextData.maData.ReadObj3( rStrm );
    ReadMacro4( rStrm, nMacroSize );
    maTextData.ReadByteString( rStrm );
    maTextData.ReadFormats( rStrm );
}

void XclImpTextObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 nMacroSize )
{
    ReadFrameData( rStrm );
    maTextData.maData.ReadObj5( rStrm );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, nMacroSize );
    maTextData.ReadByteString( rStrm );
    rStrm.Ignore( maTextData.maData.mnLinkSize );   // ignore text link formula
    maTextData.ReadFormats( rStrm );
}

rtl::Reference<SdrObject> XclImpTextObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    rtl::Reference<SdrObjCustomShape> xSdrObj(
        new SdrObjCustomShape(
            *GetDoc().GetDrawLayer()));
    xSdrObj->NbcSetSnapRect( rAnchorRect );
    OUString aRectType = u"rectangle"_ustr;
    xSdrObj->MergeDefaultAttributes( &aRectType );
    ConvertRectStyle( *xSdrObj );
    bool bAutoSize = ::get_flag( maTextData.maData.mnFlags, EXC_OBJ_TEXT_AUTOSIZE );
    xSdrObj->SetMergedItem( makeSdrTextAutoGrowWidthItem( bAutoSize ) );
    xSdrObj->SetMergedItem( makeSdrTextAutoGrowHeightItem( bAutoSize ) );
    xSdrObj->SetMergedItem( makeSdrTextWordWrapItem( true ) );
    rDffConv.Progress();
    return xSdrObj;
}

void XclImpTextObj::DoPreProcessSdrObj( XclImpDffConverter& rDffConv, SdrObject& rSdrObj ) const
{
    // set text data
    if( SdrTextObj* pTextObj = DynCastSdrTextObj( &rSdrObj ) )
    {
        if( maTextData.mxString )
        {
            if( maTextData.mxString->IsRich() )
            {
                if (maTextData.mxString->GetText().getLength() > 1024 && comphelper::IsFuzzing())
                {
                    SAL_WARN("sc.filter", "truncating slow long rich text for fuzzing performance");
                    maTextData.mxString->SetText(maTextData.mxString->GetText().copy(0, 1024));
                }

                // rich text
                std::unique_ptr< EditTextObject > xEditObj(
                    XclImpStringHelper::CreateTextObject( GetRoot(), *maTextData.mxString ) );
                OutlinerParaObject aOutlineObj(std::move(xEditObj));
                aOutlineObj.SetOutlinerMode( OutlinerMode::TextObject );
                pTextObj->NbcSetOutlinerParaObject( std::move(aOutlineObj) );
            }
            else
            {
                // plain text
                pTextObj->NbcSetText( maTextData.mxString->GetText() );
            }

            /*  #i96858# Do not apply any formatting if there is no text.
                SdrObjCustomShape::SetVerticalWriting (initiated from
                SetMergedItem) calls SdrTextObj::ForceOutlinerParaObject which
                ensures that we can erroneously write a ClientTextbox record
                (with no content) while exporting to XLS, which can cause a
                corrupted exported document. */

            SvxAdjust eHorAlign = SvxAdjust::Left;
            SdrTextVertAdjust eVerAlign = SDRTEXTVERTADJUST_TOP;

            // orientation (this is only a fake, drawing does not support real text orientation)
            namespace csst = css::text;
            csst::WritingMode eWriteMode = csst::WritingMode_LR_TB;
            switch( maTextData.maData.mnOrient )
            {
                default:
                case EXC_OBJ_ORIENT_NONE:
                {
                    eWriteMode = csst::WritingMode_LR_TB;
                    switch( maTextData.maData.GetHorAlign() )
                    {
                        case EXC_OBJ_HOR_LEFT:      eHorAlign = SvxAdjust::Left;    break;
                        case EXC_OBJ_HOR_CENTER:    eHorAlign = SvxAdjust::Center;  break;
                        case EXC_OBJ_HOR_RIGHT:     eHorAlign = SvxAdjust::Right;   break;
                        case EXC_OBJ_HOR_JUSTIFY:   eHorAlign = SvxAdjust::Block;   break;
                    }
                    switch( maTextData.maData.GetVerAlign() )
                    {
                        case EXC_OBJ_VER_TOP:       eVerAlign = SDRTEXTVERTADJUST_TOP;      break;
                        case EXC_OBJ_VER_CENTER:    eVerAlign = SDRTEXTVERTADJUST_CENTER;   break;
                        case EXC_OBJ_VER_BOTTOM:    eVerAlign = SDRTEXTVERTADJUST_BOTTOM;   break;
                        case EXC_OBJ_VER_JUSTIFY:   eVerAlign = SDRTEXTVERTADJUST_BLOCK;    break;
                    }
                }
                break;

                case EXC_OBJ_ORIENT_90CCW:
                {
                    if( SdrObjCustomShape* pObjCustomShape = dynamic_cast< SdrObjCustomShape* >( &rSdrObj ) )
                    {
                        css::beans::PropertyValue aTextRotateAngle;
                        aTextRotateAngle.Name = "TextRotateAngle";
                        aTextRotateAngle.Value <<= 180.0;
                        SdrCustomShapeGeometryItem aGeometryItem(pObjCustomShape->GetMergedItem( SDRATTR_CUSTOMSHAPE_GEOMETRY ));
                        aGeometryItem.SetPropertyValue( aTextRotateAngle );
                        pObjCustomShape->SetMergedItem( aGeometryItem );
                    }
                    eWriteMode = csst::WritingMode_TB_RL;
                    switch( maTextData.maData.GetHorAlign() )
                    {
                        case EXC_OBJ_HOR_LEFT:      eVerAlign = SDRTEXTVERTADJUST_TOP;      break;
                        case EXC_OBJ_HOR_CENTER:    eVerAlign = SDRTEXTVERTADJUST_CENTER;   break;
                        case EXC_OBJ_HOR_RIGHT:     eVerAlign = SDRTEXTVERTADJUST_BOTTOM;   break;
                        case EXC_OBJ_HOR_JUSTIFY:   eVerAlign = SDRTEXTVERTADJUST_BLOCK;    break;
                    }
                    MSO_Anchor eTextAnchor = static_cast<MSO_Anchor>(rDffConv.GetPropertyValue( DFF_Prop_anchorText, mso_anchorTop ));
                    switch( eTextAnchor )
                    {
                        case mso_anchorTopCentered :
                        case mso_anchorMiddleCentered :
                        case mso_anchorBottomCentered :
                        {
                            eHorAlign = SvxAdjust::Center;
                        }
                        break;

                        default:
                        {
                            switch( maTextData.maData.GetVerAlign() )
                            {
                                case EXC_OBJ_VER_TOP:       eHorAlign = SvxAdjust::Right;   break;
                                case EXC_OBJ_VER_CENTER:    eHorAlign = SvxAdjust::Center;  break;
                                case EXC_OBJ_VER_BOTTOM:    eHorAlign = SvxAdjust::Left;    break;
                                case EXC_OBJ_VER_JUSTIFY:   eHorAlign = SvxAdjust::Block;   break;
                            }
                        }
                    }
                }
                break;

                case EXC_OBJ_ORIENT_STACKED:
                {
                    // sj: STACKED is not supported, maybe it can be optimized here a bit
                    [[fallthrough]];
                }
                case EXC_OBJ_ORIENT_90CW:
                {
                    eWriteMode = csst::WritingMode_TB_RL;
                    switch( maTextData.maData.GetHorAlign() )
                    {
                        case EXC_OBJ_HOR_LEFT:      eVerAlign = SDRTEXTVERTADJUST_BOTTOM;   break;
                        case EXC_OBJ_HOR_CENTER:    eVerAlign = SDRTEXTVERTADJUST_CENTER;   break;
                        case EXC_OBJ_HOR_RIGHT:     eVerAlign = SDRTEXTVERTADJUST_TOP;      break;
                        case EXC_OBJ_HOR_JUSTIFY:   eVerAlign = SDRTEXTVERTADJUST_BLOCK;    break;
                    }
                    MSO_Anchor eTextAnchor = static_cast<MSO_Anchor>(rDffConv.GetPropertyValue( DFF_Prop_anchorText, mso_anchorTop ));
                    switch ( eTextAnchor )
                    {
                        case mso_anchorTopCentered :
                        case mso_anchorMiddleCentered :
                        case mso_anchorBottomCentered :
                        {
                            eHorAlign = SvxAdjust::Center;
                        }
                        break;

                        default:
                        {
                            switch( maTextData.maData.GetVerAlign() )
                            {
                                case EXC_OBJ_VER_TOP:       eHorAlign = SvxAdjust::Left;   break;
                                case EXC_OBJ_VER_CENTER:    eHorAlign = SvxAdjust::Center;  break;
                                case EXC_OBJ_VER_BOTTOM:    eHorAlign = SvxAdjust::Right;   break;
                                case EXC_OBJ_VER_JUSTIFY:   eHorAlign = SvxAdjust::Block;   break;
                            }
                        }
                    }
                }
                break;
            }
            rSdrObj.SetMergedItem( SvxAdjustItem( eHorAlign, EE_PARA_JUST ) );
            rSdrObj.SetMergedItem( SdrTextVertAdjustItem( eVerAlign ) );
            rSdrObj.SetMergedItem( SvxWritingModeItem( eWriteMode, SDRATTR_TEXTDIRECTION ) );
        }
    }
    // base class processing
    XclImpRectObj::DoPreProcessSdrObj( rDffConv, rSdrObj );
}

XclImpChartObj::XclImpChartObj( const XclImpRoot& rRoot, bool bOwnTab ) :
    XclImpRectObj( rRoot ),
    mbOwnTab( bOwnTab )
{
    SetSimpleMacro( false );
    SetCustomDffObj( true );
}

void XclImpChartObj::ReadChartSubStream( XclImpStream& rStrm )
{
    /*  If chart is read from a chartsheet (mbOwnTab == true), the BOF record
        has already been read. If chart is embedded as object, the next record
        has to be the BOF record. */
    if( mbOwnTab )
    {
        /*  #i109800# The input stream may point somewhere inside the chart
            substream and not exactly to the leading BOF record. To read this
            record correctly in the following, the stream has to rewind it, so
            that the next call to StartNextRecord() will find it correctly. */
        if( rStrm.GetRecId() != EXC_ID5_BOF )
            rStrm.RewindRecord();
    }
    else
    {
        if( (rStrm.GetNextRecId() == EXC_ID5_BOF) && rStrm.StartNextRecord() )
        {
            sal_uInt16 nBofType;
            rStrm.Seek( 2 );
            nBofType = rStrm.ReaduInt16();
            SAL_WARN_IF( nBofType != EXC_BOF_CHART, "sc.filter", "XclImpChartObj::ReadChartSubStream - no chart BOF record" );
        }
        else
        {
            SAL_INFO("sc.filter", "XclImpChartObj::ReadChartSubStream - missing chart substream");
            return;
        }
    }

    // read chart, even if BOF record contains wrong substream identifier
    mxChart = std::make_shared<XclImpChart>( GetRoot(), mbOwnTab );
    mxChart->ReadChartSubStream( rStrm );
    if( mbOwnTab )
        FinalizeTabChart();
}

void XclImpChartObj::DoReadObj3( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    // read OBJ record and the following chart substream
    ReadFrameData( rStrm );
    rStrm.Ignore( 18 );
    ReadMacro3( rStrm, nMacroSize );
    // set frame format from OBJ record, it is used if chart itself is transparent
    if( mxChart )
        mxChart->UpdateObjFrame( maLineData, maFillData );
}

void XclImpChartObj::DoReadObj4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    // read OBJ record and the following chart substream
    ReadFrameData( rStrm );
    rStrm.Ignore( 18 );
    ReadMacro4( rStrm, nMacroSize );
    // set frame format from OBJ record, it is used if chart itself is transparent
    if( mxChart )
        mxChart->UpdateObjFrame( maLineData, maFillData );
}

void XclImpChartObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 nMacroSize )
{
    // read OBJ record and the following chart substream
    ReadFrameData( rStrm );
    rStrm.Ignore( 18 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, nMacroSize );
    ReadChartSubStream( rStrm );
    // set frame format from OBJ record, it is used if chart itself is transparent
    if( mxChart )
        mxChart->UpdateObjFrame( maLineData, maFillData );
}

void XclImpChartObj::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 /*nSubRecSize*/ )
{
    // read the following chart substream
    if( nSubRecId == EXC_ID_OBJEND )
    {
        // enable CONTINUE handling for the entire chart substream
        rStrm.ResetRecord( true );
        ReadChartSubStream( rStrm );
        /*  disable CONTINUE handling again to be able to read
            following CONTINUE records as MSODRAWING records. */
        rStrm.ResetRecord( false );
    }
}

std::size_t XclImpChartObj::DoGetProgressSize() const
{
    return mxChart ? mxChart->GetProgressSize() : 1;
}

rtl::Reference<SdrObject> XclImpChartObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    rtl::Reference<SdrObject> xSdrObj;
    ScDocShell* pDocShell = GetDocShell();
    if( rDffConv.SupportsOleObjects() && SvtModuleOptions().IsChartInstalled() && pDocShell && mxChart && !mxChart->IsPivotChart() )
    {
        // create embedded chart object
        OUString aEmbObjName;
        OUString sBaseURL(GetRoot().GetMedium().GetBaseURL());
        Reference< XEmbeddedObject > xEmbObj = pDocShell->GetEmbeddedObjectContainer().
                CreateEmbeddedObject( SvGlobalName( SO3_SCH_CLASSID ).GetByteSequence(), aEmbObjName, &sBaseURL );

        if (!xEmbObj)
            return xSdrObj;

        /*  Set the size to the embedded object, this prevents that font sizes
            of text objects are changed in the chart when the object is
            inserted into the draw page. */
        sal_Int64 nAspect = css::embed::Aspects::MSOLE_CONTENT;
        MapUnit aUnit = VCLUnoHelper::UnoEmbed2VCLMapUnit( xEmbObj->getMapUnit( nAspect ) );
        Size aSize( OutputDevice::LogicToLogic( rAnchorRect.GetSize(), MapMode( MapUnit::Map100thMM ), MapMode( aUnit ) ) );
        css::awt::Size aAwtSize( aSize.Width(), aSize.Height() );
        xEmbObj->setVisualAreaSize( nAspect, aAwtSize );

        // #i121334# This call will change the chart's default background fill from white to transparent.
        // Add here again if this is wanted (see task description for details)
        // ChartHelper::AdaptDefaultsForChart( xEmbObj );

        // create the container OLE object
        xSdrObj =
            new SdrOle2Obj(
                *GetDoc().GetDrawLayer(),
                svt::EmbeddedObjectRef(xEmbObj, nAspect),
                aEmbObjName,
                rAnchorRect);
    }

    return xSdrObj;
}

void XclImpChartObj::DoPostProcessSdrObj( XclImpDffConverter& rDffConv, SdrObject& rSdrObj ) const
{
    const SdrOle2Obj* pSdrOleObj = dynamic_cast< const SdrOle2Obj* >( &rSdrObj );
    if( !(mxChart && pSdrOleObj) )
        return;

    const Reference< XEmbeddedObject >& xEmbObj = pSdrOleObj->GetObjRef();
    if( xEmbObj.is() && ::svt::EmbeddedObjectRef::TryRunningState( xEmbObj ) ) try
    {
        Reference< XEmbedPersist > xPersist( xEmbObj, UNO_QUERY_THROW );
        Reference< XModel > xModel( xEmbObj->getComponent(), UNO_QUERY_THROW );
        mxChart->Convert( xModel, rDffConv, xPersist->getEntryName(), rSdrObj.GetLogicRect() );
    }
    catch( const Exception& )
    {
    }
}

void XclImpChartObj::FinalizeTabChart()
{
    /*  #i44077# Calculate and store DFF anchor for sheet charts.
        Needed to get used area if this chart is inserted as OLE object. */
    OSL_ENSURE( mbOwnTab, "XclImpChartObj::FinalizeTabChart - not allowed for embedded chart objects" );

    // set uninitialized page to landscape
    if( !GetPageSettings().GetPageData().mbValid )
        GetPageSettings().SetPaperSize( EXC_PAPERSIZE_DEFAULT, false );

    // calculate size of the chart object
    const XclPageData& rPageData = GetPageSettings().GetPageData();
    Size aPaperSize = rPageData.GetScPaperSize();

    tools::Long nWidth = XclTools::GetHmmFromTwips( aPaperSize.Width() );
    tools::Long nHeight = XclTools::GetHmmFromTwips( aPaperSize.Height() );

    // subtract page margins, give some more extra space
    nWidth -= o3tl::saturating_add(XclTools::GetHmmFromInch(rPageData.mfLeftMargin + rPageData.mfRightMargin), static_cast<sal_Int32>(2000));
    nHeight -= o3tl::saturating_add(XclTools::GetHmmFromInch(rPageData.mfTopMargin + rPageData.mfBottomMargin), static_cast<sal_Int32>(1000));

    // print column/row headers?
    if( rPageData.mbPrintHeadings )
    {
        nWidth -= 2000;
        nHeight -= 1000;
    }

    // create the object anchor
    XclObjAnchor aAnchor;
    aAnchor.SetRect( GetRoot(), GetCurrScTab(), tools::Rectangle( 1000, 500, nWidth, nHeight ), MapUnit::Map100thMM );
    SetAnchor( aAnchor );
}

XclImpNoteObj::XclImpNoteObj( const XclImpRoot& rRoot ) :
    XclImpTextObj( rRoot ),
    maScPos( ScAddress::INITIALIZE_INVALID ),
    mnNoteFlags( 0 )
{
    SetSimpleMacro( false );
    // caption object will be created manually
    SetInsertSdrObj( false );
}

void XclImpNoteObj::SetNoteData( const ScAddress& rScPos, sal_uInt16 nNoteFlags )
{
    maScPos = rScPos;
    mnNoteFlags = nNoteFlags;
}

void XclImpNoteObj::DoPreProcessSdrObj( XclImpDffConverter& rDffConv, SdrObject& rSdrObj ) const
{
    // create formatted text
    XclImpTextObj::DoPreProcessSdrObj( rDffConv, rSdrObj );
    OutlinerParaObject* pOutlinerObj = rSdrObj.GetOutlinerParaObject();
    if( maScPos.IsValid() && pOutlinerObj )
    {
        // create cell note with all data from drawing object
        ScNoteUtil::CreateNoteFromObjectData(
            GetDoc(), maScPos,
            rSdrObj.GetMergedItemSet(),
            OUString(), *pOutlinerObj,
            rSdrObj.GetLogicRect(),
            ::get_flag( mnNoteFlags, EXC_NOTE_VISIBLE ) );
    }
}

XclImpControlHelper::XclImpControlHelper( const XclImpRoot& rRoot, XclCtrlBindMode eBindMode ) :
    mrRoot( rRoot ),
    meBindMode( eBindMode )
{
}

XclImpControlHelper::~XclImpControlHelper()
{
}

rtl::Reference<SdrObject> XclImpControlHelper::CreateSdrObjectFromShape(
        const Reference< XShape >& rxShape, const tools::Rectangle& rAnchorRect ) const
{
    mxShape = rxShape;
    rtl::Reference<SdrObject> xSdrObj( SdrObject::getSdrObjectFromXShape( rxShape ) );
    if( xSdrObj )
    {
        xSdrObj->NbcSetSnapRect( rAnchorRect );
        // #i30543# insert into control layer
        xSdrObj->NbcSetLayer( SC_LAYER_CONTROLS );
    }
    return xSdrObj;
}

void XclImpControlHelper::ApplySheetLinkProps() const
{

    Reference< XControlModel > xCtrlModel = XclControlHelper::GetControlModel( mxShape );
    if( !xCtrlModel.is() )
        return;

   // sheet links
    ScDocShell* pDocShell = mrRoot.GetDocShell();
    if(!pDocShell)
        return;

    ScModelObj* pModelObj = pDocShell->GetModel();
    if( !pModelObj )
        return;

    // cell link
    if( mxCellLink ) try
    {
        Reference< XBindableValue > xBindable( xCtrlModel, UNO_QUERY_THROW );

        // create argument sequence for createInstanceWithArguments()
        CellAddress aApiAddress;
        ScUnoConversion::FillApiAddress( aApiAddress, *mxCellLink );

        NamedValue aValue;
        aValue.Name = SC_UNONAME_BOUNDCELL;
        aValue.Value <<= aApiAddress;

        Sequence< Any > aArgs{ Any(aValue) };

        // create the CellValueBinding instance and set at the control model
        OUString aServiceName;
        switch( meBindMode )
        {
            case EXC_CTRL_BINDCONTENT:  aServiceName = SC_SERVICENAME_VALBIND;       break;
            case EXC_CTRL_BINDPOSITION: aServiceName = SC_SERVICENAME_LISTCELLBIND;  break;
        }
        Reference< XValueBinding > xBinding(
            pModelObj->createInstanceWithArguments( aServiceName, aArgs ), UNO_QUERY_THROW );
        xBindable->setValueBinding( xBinding );
    }
    catch( const Exception& )
    {
    }

    // source range
    if( !mxSrcRange )
        return;

    try
    {
        Reference< XListEntrySink > xEntrySink( xCtrlModel, UNO_QUERY_THROW );

        // create argument sequence for createInstanceWithArguments()
        CellRangeAddress aApiRange;
        ScUnoConversion::FillApiRange( aApiRange, *mxSrcRange );

        NamedValue aValue;
        aValue.Name = SC_UNONAME_CELLRANGE;
        aValue.Value <<= aApiRange;

        Sequence< Any > aArgs{ Any(aValue) };

        // create the EntrySource instance and set at the control model
        Reference< XListEntrySource > xEntrySource( pModelObj->createInstanceWithArguments(
            SC_SERVICENAME_LISTSOURCE, aArgs ), UNO_QUERY_THROW );
        xEntrySink->setListEntrySource( xEntrySource );
    }
    catch( const Exception& )
    {
    }
}

void XclImpControlHelper::ProcessControl( const XclImpDrawObjBase& rDrawObj ) const
{
    Reference< XControlModel > xCtrlModel = XclControlHelper::GetControlModel( mxShape );
    if( !xCtrlModel.is() )
        return;

    ApplySheetLinkProps();

    ScfPropertySet aPropSet( xCtrlModel );

    // #i51348# set object name at control model
    aPropSet.SetStringProperty( u"Name"_ustr, rDrawObj.GetObjName() );

    // control visible and printable?
    aPropSet.SetBoolProperty( u"EnableVisible"_ustr, rDrawObj.IsVisible() );
    aPropSet.SetBoolProperty( u"Printable"_ustr, rDrawObj.IsPrintable() );

    // virtual call for type specific processing
    DoProcessControl( aPropSet );
}

void XclImpControlHelper::ReadCellLinkFormula( XclImpStream& rStrm, bool bWithBoundSize )
{
    ScRangeList aScRanges;
    ReadRangeList( aScRanges, rStrm, bWithBoundSize );
    // Use first cell of first range
    if ( !aScRanges.empty() )
    {
        const ScRange & rScRange = aScRanges.front();
        mxCellLink = std::make_shared<ScAddress>( rScRange.aStart );
    }
}

void XclImpControlHelper::ReadSourceRangeFormula( XclImpStream& rStrm, bool bWithBoundSize )
{
    ScRangeList aScRanges;
    ReadRangeList( aScRanges, rStrm, bWithBoundSize );
    // Use first range
    if ( !aScRanges.empty() )
    {
        const ScRange & rScRange = aScRanges.front();
        mxSrcRange = std::make_shared<ScRange>( rScRange );
    }
}

void XclImpControlHelper::DoProcessControl( ScfPropertySet& ) const
{
}

void XclImpControlHelper::ReadRangeList( ScRangeList& rScRanges, XclImpStream& rStrm )
{
    XclTokenArray aXclTokArr;
    sal_uInt16 nSize = XclTokenArray::ReadSize(rStrm);
    rStrm.Ignore( 4 );
    aXclTokArr.ReadArray(nSize, rStrm);
    mrRoot.GetFormulaCompiler().CreateRangeList( rScRanges, EXC_FMLATYPE_CONTROL, aXclTokArr, rStrm );
}

void XclImpControlHelper::ReadRangeList( ScRangeList& rScRanges, XclImpStream& rStrm, bool bWithBoundSize )
{
    if( bWithBoundSize )
    {
        sal_uInt16 nSize;
        nSize = rStrm.ReaduInt16();
        if( nSize > 0 )
        {
            rStrm.PushPosition();
            ReadRangeList( rScRanges, rStrm );
            rStrm.PopPosition();
            rStrm.Ignore( nSize );
        }
    }
    else
    {
        ReadRangeList( rScRanges, rStrm );
    }
}

XclImpTbxObjBase::XclImpTbxObjBase( const XclImpRoot& rRoot ) :
    XclImpTextObj( rRoot ),
    XclImpControlHelper( rRoot, EXC_CTRL_BINDPOSITION )
{
    SetSimpleMacro( false );
    SetCustomDffObj( true );
}

namespace {

void lclExtractColor( sal_uInt8& rnColorIdx, const DffPropSet& rDffPropSet, sal_uInt32 nPropId )
{
    if( rDffPropSet.IsProperty( nPropId ) )
    {
        sal_uInt32 nColor = rDffPropSet.GetPropertyValue( nPropId, 0 );
        if( (nColor & 0xFF000000) == 0x08000000 )
            rnColorIdx = ::extract_value< sal_uInt8 >( nColor, 0, 8 );
    }
}

} // namespace

void XclImpTbxObjBase::SetDffProperties( const DffPropSet& rDffPropSet )
{
    maFillData.mnPattern = rDffPropSet.GetPropertyBool( DFF_Prop_fFilled ) ? EXC_PATT_SOLID : EXC_PATT_NONE;
    lclExtractColor( maFillData.mnBackColorIdx, rDffPropSet, DFF_Prop_fillBackColor );
    lclExtractColor( maFillData.mnPattColorIdx, rDffPropSet, DFF_Prop_fillColor );
    ::set_flag( maFillData.mnAuto, EXC_OBJ_LINE_AUTO, false );

    maLineData.mnStyle = rDffPropSet.GetPropertyBool( DFF_Prop_fLine ) ? EXC_OBJ_LINE_SOLID : EXC_OBJ_LINE_NONE;
    lclExtractColor( maLineData.mnColorIdx, rDffPropSet, DFF_Prop_lineColor );
    ::set_flag( maLineData.mnAuto, EXC_OBJ_FILL_AUTO, false );
}

void XclImpControlHelper::SetStringProperty(const OUString& sName, const OUString& sVal)
{
    Reference<XControlModel> xCtrlModel = XclControlHelper::GetControlModel(mxShape);
    if (!xCtrlModel.is())
        return;

    ScfPropertySet aProps(xCtrlModel);
    aProps.SetStringProperty(sName, sVal);
}

bool XclImpTbxObjBase::FillMacroDescriptor( ScriptEventDescriptor& rDescriptor ) const
{
    return XclControlHelper::FillMacroDescriptor( rDescriptor, DoGetEventType(), GetMacroName(), GetDocShell() );
}

void XclImpTbxObjBase::ConvertFont( ScfPropertySet& rPropSet ) const
{
    if( maTextData.mxString )
    {
        const XclFormatRunVec& rFormatRuns = maTextData.mxString->GetFormats();
        if( rFormatRuns.empty() )
            GetFontBuffer().WriteDefaultCtrlFontProperties( rPropSet );
        else
            GetFontBuffer().WriteFontProperties( rPropSet, EXC_FONTPROPSET_CONTROL, rFormatRuns.front().mnFontIdx );
    }
}

void XclImpTbxObjBase::ConvertLabel( ScfPropertySet& rPropSet ) const
{
    if( maTextData.mxString )
    {
        OUString aLabel = maTextData.mxString->GetText();
        if( maTextData.maData.mnShortcut > 0 )
        {
            sal_Int32 nPos = aLabel.indexOf( static_cast< sal_Unicode >( maTextData.maData.mnShortcut ) );
            if( nPos != -1 )
                aLabel = aLabel.replaceAt( nPos, 0, u"~" );
        }
        rPropSet.SetStringProperty( u"Label"_ustr, aLabel );

        //Excel Alt text <==> Aoo description
        //For TBX control, if user does not operate alt text, alt text will be set label text as default value in Excel.
        //In this case, DFF_Prop_wzDescription will not be set in excel file.
        //So In the end of SvxMSDffManager::ImportShape, description will not be set. But actually in excel,
        //the alt text is the label value. So here set description as label text first which is called before ImportShape.
        Reference< css::beans::XPropertySet > xPropset( mxShape, UNO_QUERY );
        try{
        if(xPropset.is())
            xPropset->setPropertyValue( u"Description"_ustr, Any(aLabel) );
        }catch( ... )
        {
            SAL_WARN("sc.filter", "Can't set a default text for TBX Control ");
        }
    }
    ConvertFont( rPropSet );
}

rtl::Reference<SdrObject> XclImpTbxObjBase::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    rtl::Reference<SdrObject> xSdrObj( rDffConv.CreateSdrObject( *this, rAnchorRect ) );
    rDffConv.Progress();
    return xSdrObj;
}

void XclImpTbxObjBase::DoPreProcessSdrObj( XclImpDffConverter& /*rDffConv*/, SdrObject& /*rSdrObj*/ ) const
{
    // do not call DoPreProcessSdrObj() from base class (to skip text processing)
    ProcessControl( *this );
}

XclImpButtonObj::XclImpButtonObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjBase( rRoot )
{
}

void XclImpButtonObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // label and text formatting
    ConvertLabel( rPropSet );

    /*  Horizontal text alignment. For unknown reason, the property type is a
        simple sal_Int16 and not a com.sun.star.style.HorizontalAlignment. */
    sal_Int16 nHorAlign = 1;
    switch( maTextData.maData.GetHorAlign() )
    {
        case EXC_OBJ_HOR_LEFT:      nHorAlign = 0;  break;
        case EXC_OBJ_HOR_CENTER:    nHorAlign = 1;  break;
        case EXC_OBJ_HOR_RIGHT:     nHorAlign = 2;  break;
    }
    rPropSet.SetProperty( u"Align"_ustr, nHorAlign );

    // vertical text alignment
    namespace csss = css::style;
    csss::VerticalAlignment eVerAlign = csss::VerticalAlignment_MIDDLE;
    switch( maTextData.maData.GetVerAlign() )
    {
        case EXC_OBJ_VER_TOP:       eVerAlign = csss::VerticalAlignment_TOP;    break;
        case EXC_OBJ_VER_CENTER:    eVerAlign = csss::VerticalAlignment_MIDDLE; break;
        case EXC_OBJ_VER_BOTTOM:    eVerAlign = csss::VerticalAlignment_BOTTOM; break;
    }
    rPropSet.SetProperty( u"VerticalAlign"_ustr, eVerAlign );

    // always wrap text automatically
    rPropSet.SetBoolProperty( u"MultiLine"_ustr, true );

    // default button
    bool bDefButton = ::get_flag( maTextData.maData.mnButtonFlags, EXC_OBJ_BUTTON_DEFAULT );
    rPropSet.SetBoolProperty( u"DefaultButton"_ustr, bDefButton );

    // button type (flags cannot be combined in OOo)
    namespace cssa = css::awt;
    cssa::PushButtonType eButtonType = cssa::PushButtonType_STANDARD;
    if( ::get_flag( maTextData.maData.mnButtonFlags, EXC_OBJ_BUTTON_CLOSE ) )
        eButtonType = cssa::PushButtonType_OK;
    else if( ::get_flag( maTextData.maData.mnButtonFlags, EXC_OBJ_BUTTON_CANCEL ) )
        eButtonType = cssa::PushButtonType_CANCEL;
    else if( ::get_flag( maTextData.maData.mnButtonFlags, EXC_OBJ_BUTTON_HELP ) )
        eButtonType = cssa::PushButtonType_HELP;
    // property type is short, not enum
    rPropSet.SetProperty( u"PushButtonType"_ustr, sal_Int16( eButtonType ) );
}

OUString XclImpButtonObj::DoGetServiceName() const
{
    return u"com.sun.star.form.component.CommandButton"_ustr;
}

XclTbxEventType XclImpButtonObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_ACTION;
}

XclImpCheckBoxObj::XclImpCheckBoxObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjBase( rRoot ),
    mnState( EXC_OBJ_CHECKBOX_UNCHECKED ),
    mnCheckBoxFlags( 0 )
{
}

void XclImpCheckBoxObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 /*nMacroSize*/ )
{
    ReadFrameData( rStrm );
    rStrm.Ignore( 10 );
    maTextData.maData.mnFlags = rStrm.ReaduInt16();
    rStrm.Ignore( 20 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, rStrm.ReaduInt16() );   // first macro size invalid and unused
    ReadCellLinkFormula( rStrm, true );
    maTextData.maData.mnTextLen = rStrm.ReaduInt16();
    maTextData.ReadByteString( rStrm );
    mnState = rStrm.ReaduInt16();
    maTextData.maData.mnShortcut = rStrm.ReaduInt16();
    maTextData.maData.mnShortcutEA = rStrm.ReaduInt16();
    mnCheckBoxFlags = rStrm.ReaduInt16();
}

void XclImpCheckBoxObj::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 nSubRecSize )
{
    switch( nSubRecId )
    {
        case EXC_ID_OBJCBLS:
            // do not read EXC_ID_OBJCBLSDATA, not written by OOo Excel export
            mnState = rStrm.ReaduInt16();
            rStrm.Ignore( 4 );
            maTextData.maData.mnShortcut = rStrm.ReaduInt16();
            maTextData.maData.mnShortcutEA = rStrm.ReaduInt16();
            mnCheckBoxFlags = rStrm.ReaduInt16();
        break;
        case EXC_ID_OBJCBLSFMLA:
            ReadCellLinkFormula( rStrm, false );
        break;
        default:
            XclImpTbxObjBase::DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
    }
}

void XclImpCheckBoxObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // label and text formatting
    ConvertLabel( rPropSet );

    // state
    bool bSupportsTristate = GetObjType() == EXC_OBJTYPE_CHECKBOX;
    sal_Int16 nApiState = 0;
    switch( mnState )
    {
        case EXC_OBJ_CHECKBOX_UNCHECKED:    nApiState = 0;                          break;
        case EXC_OBJ_CHECKBOX_CHECKED:      nApiState = 1;                          break;
        case EXC_OBJ_CHECKBOX_TRISTATE:     nApiState = bSupportsTristate ? 2 : 1;  break;
    }
    if( bSupportsTristate )
        rPropSet.SetBoolProperty( u"TriState"_ustr, nApiState == 2 );
    rPropSet.SetProperty( u"DefaultState"_ustr, nApiState );

    // box style
    namespace AwtVisualEffect = css::awt::VisualEffect;
    sal_Int16 nEffect = ::get_flagvalue( mnCheckBoxFlags, EXC_OBJ_CHECKBOX_FLAT, AwtVisualEffect::FLAT, AwtVisualEffect::LOOK3D );
    rPropSet.SetProperty( u"VisualEffect"_ustr, nEffect );

    // do not wrap text automatically
    rPropSet.SetBoolProperty( u"MultiLine"_ustr, false );

    // #i40279# always centered vertically
    namespace csss = css::style;
    rPropSet.SetProperty( u"VerticalAlign"_ustr, csss::VerticalAlignment_MIDDLE );

    // background color
    if( maFillData.IsFilled() )
    {
        sal_Int32 nColor = static_cast< sal_Int32 >( GetSolidFillColor( maFillData ) );
        rPropSet.SetProperty( u"BackgroundColor"_ustr, nColor );
    }
}

OUString XclImpCheckBoxObj::DoGetServiceName() const
{
    return u"com.sun.star.form.component.CheckBox"_ustr;
}

XclTbxEventType XclImpCheckBoxObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_ACTION;
}

XclImpOptionButtonObj::XclImpOptionButtonObj( const XclImpRoot& rRoot ) :
    XclImpCheckBoxObj( rRoot ),
    mnNextInGroup( 0 ),
    mnFirstInGroup( 1 )
{
}

void XclImpOptionButtonObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 /*nMacroSize*/ )
{
    ReadFrameData( rStrm );
    rStrm.Ignore( 10 );
    maTextData.maData.mnFlags = rStrm.ReaduInt16();
    rStrm.Ignore( 32 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, rStrm.ReaduInt16() );   // first macro size invalid and unused
    ReadCellLinkFormula( rStrm, true );
    maTextData.maData.mnTextLen = rStrm.ReaduInt16();
    maTextData.ReadByteString( rStrm );
    mnState = rStrm.ReaduInt16();
    maTextData.maData.mnShortcut = rStrm.ReaduInt16();
    maTextData.maData.mnShortcutEA = rStrm.ReaduInt16();
    mnCheckBoxFlags = rStrm.ReaduInt16();
    mnNextInGroup = rStrm.ReaduInt16();
    mnFirstInGroup = rStrm.ReaduInt16();
}

void XclImpOptionButtonObj::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 nSubRecSize )
{
    switch( nSubRecId )
    {
        case EXC_ID_OBJRBODATA:
            mnNextInGroup = rStrm.ReaduInt16();
            mnFirstInGroup = rStrm.ReaduInt16();
        break;
        default:
            XclImpCheckBoxObj::DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
    }
}

void XclImpOptionButtonObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    XclImpCheckBoxObj::DoProcessControl( rPropSet );
    // TODO: grouping
    XclImpOptionButtonObj* pTbxObj = dynamic_cast< XclImpOptionButtonObj* >( GetObjectManager().GetSheetDrawing( GetTab() ).FindDrawObj( mnNextInGroup ).get() );
    if ( pTbxObj && pTbxObj->mnFirstInGroup )
    {
        // Group has terminated
        // traverse each RadioButton in group and
        //     a) apply the groupname
        //     b) propagate the linked cell from the lead radiobutton
        //     c) apply the correct Ref value
        XclImpOptionButtonObj* pLeader = pTbxObj;

        sal_Int32 nRefVal = 1;
        do
        {

            Reference< XControlModel > xCtrlModel = XclControlHelper::GetControlModel( pTbxObj->mxShape );
            if ( xCtrlModel.is() )
            {
                ScfPropertySet aProps( xCtrlModel );
                OUString sGroupName = OUString::number( pLeader->GetDffShapeId() );

                aProps.SetStringProperty( u"GroupName"_ustr, sGroupName );
                aProps.SetStringProperty( u"RefValue"_ustr, OUString::number( nRefVal++ ) );
                if ( pLeader->HasCellLink() && !pTbxObj->HasCellLink() )
                {
                    // propagate cell link info
                    pTbxObj->mxCellLink = std::make_shared<ScAddress>( *pLeader->mxCellLink );
                    pTbxObj->ApplySheetLinkProps();
                }
                pTbxObj = dynamic_cast< XclImpOptionButtonObj* >( GetObjectManager().GetSheetDrawing( GetTab() ).FindDrawObj( pTbxObj->mnNextInGroup ).get() );
            }
            else
                pTbxObj = nullptr;
        } while ( pTbxObj && ( pTbxObj->mnFirstInGroup != 1 ) );
    }
    else
    {
        // not the leader? try and find it
    }
}

OUString XclImpOptionButtonObj::DoGetServiceName() const
{
    return u"com.sun.star.form.component.RadioButton"_ustr;
}

XclTbxEventType XclImpOptionButtonObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_ACTION;
}

bool XclImpOptionButtonObj::IsInGroup() const
{
    return mnNextInGroup;
}

XclImpLabelObj::XclImpLabelObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjBase( rRoot )
{
}

void XclImpLabelObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // label and text formatting
    ConvertLabel( rPropSet );

    // text alignment (always top/left aligned)
    rPropSet.SetProperty( u"Align"_ustr, sal_Int16( 0 ) );
    namespace csss = css::style;
    rPropSet.SetProperty( u"VerticalAlign"_ustr, csss::VerticalAlignment_TOP );

    // always wrap text automatically
    rPropSet.SetBoolProperty( u"MultiLine"_ustr, true );
}

OUString XclImpLabelObj::DoGetServiceName() const
{
    return u"com.sun.star.form.component.FixedText"_ustr;
}

XclTbxEventType XclImpLabelObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_MOUSE;
}

XclImpGroupBoxObj::XclImpGroupBoxObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjBase( rRoot ),
    mnGroupBoxFlags( 0 )
{
}

void XclImpGroupBoxObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 /*nMacroSize*/ )
{
    ReadFrameData( rStrm );
    rStrm.Ignore( 10 );
    maTextData.maData.mnFlags = rStrm.ReaduInt16();
    rStrm.Ignore( 26 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, rStrm.ReaduInt16() );   // first macro size invalid and unused
    maTextData.maData.mnTextLen = rStrm.ReaduInt16();
    maTextData.ReadByteString( rStrm );
    maTextData.maData.mnShortcut = rStrm.ReaduInt16();
    maTextData.maData.mnShortcutEA = rStrm.ReaduInt16(  );
    mnGroupBoxFlags = rStrm.ReaduInt16();
}

void XclImpGroupBoxObj::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 nSubRecSize )
{
    switch( nSubRecId )
    {
        case EXC_ID_OBJGBODATA:
            maTextData.maData.mnShortcut = rStrm.ReaduInt16();
            maTextData.maData.mnShortcutEA = rStrm.ReaduInt16();
            mnGroupBoxFlags = rStrm.ReaduInt16();
        break;
        default:
            XclImpTbxObjBase::DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
    }
}

void XclImpGroupBoxObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // label and text formatting
    ConvertLabel( rPropSet );
}

OUString XclImpGroupBoxObj::DoGetServiceName() const
{
    return u"com.sun.star.form.component.GroupBox"_ustr;
}

XclTbxEventType XclImpGroupBoxObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_MOUSE;
}

XclImpDialogObj::XclImpDialogObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjBase( rRoot )
{
}

void XclImpDialogObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // label and text formatting
    ConvertLabel( rPropSet );
}

OUString XclImpDialogObj::DoGetServiceName() const
{
    // dialog frame faked by a groupbox
    return u"com.sun.star.form.component.GroupBox"_ustr;
}

XclTbxEventType XclImpDialogObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_MOUSE;
}

XclImpEditObj::XclImpEditObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjBase( rRoot ),
    mnContentType( EXC_OBJ_EDIT_TEXT ),
    mnMultiLine( 0 ),
    mnScrollBar( 0 ),
    mnListBoxObjId( 0 )
{
}

bool XclImpEditObj::IsNumeric() const
{
    return (mnContentType == EXC_OBJ_EDIT_INTEGER) || (mnContentType == EXC_OBJ_EDIT_DOUBLE);
}

void XclImpEditObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 /*nMacroSize*/ )
{
    ReadFrameData( rStrm );
    rStrm.Ignore( 10 );
    maTextData.maData.mnFlags = rStrm.ReaduInt16();
    rStrm.Ignore( 14 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, rStrm.ReaduInt16() );   // first macro size invalid and unused
    maTextData.maData.mnTextLen = rStrm.ReaduInt16();
    maTextData.ReadByteString( rStrm );
    mnContentType = rStrm.ReaduInt16();
    mnMultiLine = rStrm.ReaduInt16();
    mnScrollBar = rStrm.ReaduInt16();
    mnListBoxObjId = rStrm.ReaduInt16();
}

void XclImpEditObj::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 nSubRecSize )
{
    switch( nSubRecId )
    {
        case EXC_ID_OBJEDODATA:
            mnContentType = rStrm.ReaduInt16();
            mnMultiLine = rStrm.ReaduInt16();
            mnScrollBar = rStrm.ReaduInt16();
            mnListBoxObjId = rStrm.ReaduInt16();
        break;
        default:
            XclImpTbxObjBase::DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
    }
}

void XclImpEditObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    if( maTextData.mxString )
    {
        OUString aText = maTextData.mxString->GetText();
        if( IsNumeric() )
        {
            // TODO: OUString::toDouble() does not handle local decimal separator
            rPropSet.SetProperty( u"DefaultValue"_ustr, aText.toDouble() );
            rPropSet.SetBoolProperty( u"Spin"_ustr, mnScrollBar != 0 );
        }
        else
        {
            rPropSet.SetProperty( u"DefaultText"_ustr, aText );
            rPropSet.SetBoolProperty( u"MultiLine"_ustr, mnMultiLine != 0 );
            rPropSet.SetBoolProperty( u"VScroll"_ustr, mnScrollBar != 0 );
        }
    }
    ConvertFont( rPropSet );
}

OUString XclImpEditObj::DoGetServiceName() const
{
    return IsNumeric() ?
        u"com.sun.star.form.component.NumericField"_ustr :
        u"com.sun.star.form.component.TextField"_ustr;
}

XclTbxEventType XclImpEditObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_TEXT;
}

XclImpTbxObjScrollableBase::XclImpTbxObjScrollableBase( const XclImpRoot& rRoot ) :
    XclImpTbxObjBase( rRoot ),
    mnValue( 0 ),
    mnMin( 0 ),
    mnMax( 100 ),
    mnStep( 1 ),
    mnPageStep( 10 ),
    mnOrient( 0 ),
    mnThumbWidth( 1 ),
    mnScrollFlags( 0 )
{
}

void XclImpTbxObjScrollableBase::ReadSbs( XclImpStream& rStrm )
{
    rStrm.Ignore( 4 );
    mnValue = rStrm.ReaduInt16();
    mnMin = rStrm.ReaduInt16();
    mnMax = rStrm.ReaduInt16();
    mnStep = rStrm.ReaduInt16();
    mnPageStep = rStrm.ReaduInt16();
    mnOrient = rStrm.ReaduInt16();
    mnThumbWidth = rStrm.ReaduInt16();
    mnScrollFlags = rStrm.ReaduInt16();
}

void XclImpTbxObjScrollableBase::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 nSubRecSize )
{
    switch( nSubRecId )
    {
        case EXC_ID_OBJSBS:
            ReadSbs( rStrm );
        break;
        case EXC_ID_OBJSBSFMLA:
            ReadCellLinkFormula( rStrm, false );
        break;
        default:
            XclImpTbxObjBase::DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
    }
}

XclImpSpinButtonObj::XclImpSpinButtonObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjScrollableBase( rRoot )
{
}

void XclImpSpinButtonObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 /*nMacroSize*/ )
{
    ReadFrameData( rStrm );
    ReadSbs( rStrm );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, rStrm.ReaduInt16() );   // first macro size invalid and unused
    ReadCellLinkFormula( rStrm, true );
}

void XclImpSpinButtonObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // Calc's "Border" property is not the 3D/flat style effect in Excel (#i34712#)
    rPropSet.SetProperty( u"Border"_ustr, css::awt::VisualEffect::NONE );
    rPropSet.SetProperty< sal_Int32 >( u"DefaultSpinValue"_ustr, mnValue );
    rPropSet.SetProperty< sal_Int32 >( u"SpinValueMin"_ustr, mnMin );
    rPropSet.SetProperty< sal_Int32 >( u"SpinValueMax"_ustr, mnMax );
    rPropSet.SetProperty< sal_Int32 >( u"SpinIncrement"_ustr, mnStep );

    // Excel spin buttons always vertical
    rPropSet.SetProperty( u"Orientation"_ustr, css::awt::ScrollBarOrientation::VERTICAL );
}

OUString XclImpSpinButtonObj::DoGetServiceName() const
{
    return u"com.sun.star.form.component.SpinButton"_ustr;
}

XclTbxEventType XclImpSpinButtonObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_VALUE;
}

XclImpScrollBarObj::XclImpScrollBarObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjScrollableBase( rRoot )
{
}

void XclImpScrollBarObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 /*nMacroSize*/ )
{
    ReadFrameData( rStrm );
    ReadSbs( rStrm );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, rStrm.ReaduInt16() );   // first macro size invalid and unused
    ReadCellLinkFormula( rStrm, true );
}

void XclImpScrollBarObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // Calc's "Border" property is not the 3D/flat style effect in Excel (#i34712#)
    rPropSet.SetProperty( u"Border"_ustr, css::awt::VisualEffect::NONE );
    rPropSet.SetProperty< sal_Int32 >( u"DefaultScrollValue"_ustr, mnValue );
    rPropSet.SetProperty< sal_Int32 >( u"ScrollValueMin"_ustr, mnMin );
    rPropSet.SetProperty< sal_Int32 >( u"ScrollValueMax"_ustr, mnMax );
    rPropSet.SetProperty< sal_Int32 >( u"LineIncrement"_ustr, mnStep );
    rPropSet.SetProperty< sal_Int32 >( u"BlockIncrement"_ustr, mnPageStep );
    rPropSet.SetProperty( u"VisibleSize"_ustr, ::std::min< sal_Int32 >( mnPageStep, 1 ) );

    namespace AwtScrollOrient = css::awt::ScrollBarOrientation;
    sal_Int32 nApiOrient = ::get_flagvalue( mnOrient, EXC_OBJ_SCROLLBAR_HOR, AwtScrollOrient::HORIZONTAL, AwtScrollOrient::VERTICAL );
    rPropSet.SetProperty( u"Orientation"_ustr, nApiOrient );
}

OUString XclImpScrollBarObj::DoGetServiceName() const
{
    return u"com.sun.star.form.component.ScrollBar"_ustr;
}

XclTbxEventType XclImpScrollBarObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_VALUE;
}

XclImpTbxObjListBase::XclImpTbxObjListBase( const XclImpRoot& rRoot ) :
    XclImpTbxObjScrollableBase( rRoot ),
    mnEntryCount( 0 ),
    mnSelEntry( 0 ),
    mnListFlags( 0 ),
    mnEditObjId( 0 ),
    mbHasDefFontIdx( false )
{
}

void XclImpTbxObjListBase::ReadLbsData( XclImpStream& rStrm )
{
    ReadSourceRangeFormula( rStrm, true );
    mnEntryCount = rStrm.ReaduInt16();
    mnSelEntry = rStrm.ReaduInt16();
    mnListFlags = rStrm.ReaduInt16();
    mnEditObjId = rStrm.ReaduInt16();
}

void XclImpTbxObjListBase::SetBoxFormatting( ScfPropertySet& rPropSet ) const
{
    // border style
    namespace AwtVisualEffect = css::awt::VisualEffect;
    sal_Int16 nApiBorder = ::get_flagvalue( mnListFlags, EXC_OBJ_LISTBOX_FLAT, AwtVisualEffect::FLAT, AwtVisualEffect::LOOK3D );
    rPropSet.SetProperty( u"Border"_ustr, nApiBorder );

    // font formatting
    if( mbHasDefFontIdx )
        GetFontBuffer().WriteFontProperties( rPropSet, EXC_FONTPROPSET_CONTROL, maTextData.maData.mnDefFontIdx );
    else
        GetFontBuffer().WriteDefaultCtrlFontProperties( rPropSet );
}

XclImpListBoxObj::XclImpListBoxObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjListBase( rRoot )
{
}

void XclImpListBoxObj::ReadFullLbsData( XclImpStream& rStrm, std::size_t nRecLeft )
{
    std::size_t nRecEnd = rStrm.GetRecPos() + nRecLeft;
    ReadLbsData( rStrm );
    OSL_ENSURE( (rStrm.GetRecPos() == nRecEnd) || (rStrm.GetRecPos() + mnEntryCount == nRecEnd),
        "XclImpListBoxObj::ReadFullLbsData - invalid size of OBJLBSDATA record" );
    while (rStrm.IsValid())
    {
        if (rStrm.GetRecPos() >= nRecEnd)
            break;
        maSelection.push_back( rStrm.ReaduInt8() );
    }
}

void XclImpListBoxObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 /*nMacroSize*/ )
{
    ReadFrameData( rStrm );
    ReadSbs( rStrm );
    rStrm.Ignore( 18 );
    maTextData.maData.mnDefFontIdx = rStrm.ReaduInt16();
    rStrm.Ignore( 4 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, rStrm.ReaduInt16() );   // first macro size invalid and unused
    ReadCellLinkFormula( rStrm, true );
    ReadFullLbsData( rStrm, rStrm.GetRecLeft() );
    mbHasDefFontIdx = true;
}

void XclImpListBoxObj::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 nSubRecSize )
{
    switch( nSubRecId )
    {
        case EXC_ID_OBJLBSDATA:
            ReadFullLbsData( rStrm, nSubRecSize );
        break;
        default:
            XclImpTbxObjListBase::DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
    }
}

void XclImpListBoxObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // listbox formatting
    SetBoxFormatting( rPropSet );

    // selection type
    sal_uInt8 nSelType = ::extract_value< sal_uInt8 >( mnListFlags, 4, 2 );
    bool bMultiSel = nSelType != EXC_OBJ_LISTBOX_SINGLE;
    rPropSet.SetBoolProperty( u"MultiSelection"_ustr, bMultiSel );

    // selection (do not set, if listbox is linked to a cell)
    if( HasCellLink() )
        return;

    ScfInt16Vec aSelVec;

    // multi selection: API expects sequence of list entry indexes
    if( bMultiSel )
    {
        sal_Int16 nIndex = 0;
        for( const auto& rItem : maSelection )
        {
            if( rItem != 0 )
                aSelVec.push_back( nIndex );
            ++nIndex;
        }
    }
    // single selection: mnSelEntry is one-based, API expects zero-based
    else if( mnSelEntry > 0 )
        aSelVec.push_back( static_cast< sal_Int16 >( mnSelEntry - 1 ) );

    if( !aSelVec.empty() )
    {
        Sequence<sal_Int16> aSelSeq(aSelVec.data(), static_cast<sal_Int32>(aSelVec.size()));
        rPropSet.SetProperty( u"DefaultSelection"_ustr, aSelSeq );
    }
}

OUString XclImpListBoxObj::DoGetServiceName() const
{
    return u"com.sun.star.form.component.ListBox"_ustr;
}

XclTbxEventType XclImpListBoxObj::DoGetEventType() const
{
    return EXC_TBX_EVENT_CHANGE;
}

XclImpDropDownObj::XclImpDropDownObj( const XclImpRoot& rRoot ) :
    XclImpTbxObjListBase( rRoot ),
    mnLeft( 0 ),
    mnTop( 0 ),
    mnRight( 0 ),
    mnBottom( 0 ),
    mnDropDownFlags( 0 ),
    mnLineCount( 0 ),
    mnMinWidth( 0 )
{
}

sal_uInt16 XclImpDropDownObj::GetDropDownType() const
{
    return ::extract_value< sal_uInt8 >( mnDropDownFlags, 0, 2 );
}

void XclImpDropDownObj::ReadFullLbsData( XclImpStream& rStrm )
{
    ReadLbsData( rStrm );
    mnDropDownFlags = rStrm.ReaduInt16();
    mnLineCount = rStrm.ReaduInt16();
    mnMinWidth = rStrm.ReaduInt16();
    maTextData.maData.mnTextLen = rStrm.ReaduInt16();
    maTextData.ReadByteString( rStrm );
    // dropdowns of auto-filters have 'simple' style, they don't have a text area
    if( GetDropDownType() == EXC_OBJ_DROPDOWN_SIMPLE )
        SetProcessSdrObj( false );
}

void XclImpDropDownObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 /*nMacroSize*/ )
{
    ReadFrameData( rStrm );
    ReadSbs( rStrm );
    rStrm.Ignore( 18 );
    maTextData.maData.mnDefFontIdx = rStrm.ReaduInt16();
    rStrm.Ignore( 14 );
    mnLeft = rStrm.ReaduInt16();
    mnTop = rStrm.ReaduInt16();
    mnRight = rStrm.ReaduInt16();
    mnBottom = rStrm.ReaduInt16();
    rStrm.Ignore( 4 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, rStrm.ReaduInt16() );   // first macro size invalid and unused
    ReadCellLinkFormula( rStrm, true );
    ReadFullLbsData( rStrm );
    mbHasDefFontIdx = true;
}

void XclImpDropDownObj::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 nSubRecSize )
{
    switch( nSubRecId )
    {
        case EXC_ID_OBJLBSDATA:
            ReadFullLbsData( rStrm );
        break;
        default:
            XclImpTbxObjListBase::DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
    }
}

void XclImpDropDownObj::DoProcessControl( ScfPropertySet& rPropSet ) const
{
    // dropdown listbox formatting
    SetBoxFormatting( rPropSet );
    // enable dropdown button
    rPropSet.SetBoolProperty( u"Dropdown"_ustr, true );
    // dropdown line count
    rPropSet.SetProperty( u"LineCount"_ustr, mnLineCount );

    if( GetDropDownType() == EXC_OBJ_DROPDOWN_COMBOBOX )
    {
        // text of editable combobox
        if( maTextData.mxString )
            rPropSet.SetStringProperty( u"DefaultText"_ustr, maTextData.mxString->GetText() );
    }
    else
    {
        // selection (do not set, if dropdown is linked to a cell)
        if( !HasCellLink() && (mnSelEntry > 0) )
        {
            Sequence< sal_Int16 > aSelSeq{ o3tl::narrowing<sal_Int16>(mnSelEntry - 1) };
            rPropSet.SetProperty( u"DefaultSelection"_ustr, aSelSeq );
        }
    }
}

OUString XclImpDropDownObj::DoGetServiceName() const
{
    return (GetDropDownType() == EXC_OBJ_DROPDOWN_COMBOBOX) ?
        u"com.sun.star.form.component.ComboBox"_ustr :
        u"com.sun.star.form.component.ListBox"_ustr;
}

XclTbxEventType XclImpDropDownObj::DoGetEventType() const
{
    return (GetDropDownType() == EXC_OBJ_DROPDOWN_COMBOBOX) ? EXC_TBX_EVENT_TEXT : EXC_TBX_EVENT_CHANGE;
}

XclImpPictureObj::XclImpPictureObj( const XclImpRoot& rRoot ) :
    XclImpRectObj( rRoot ),
    XclImpControlHelper( rRoot, EXC_CTRL_BINDCONTENT ),
    mnStorageId( 0 ),
    mnCtlsStrmPos( 0 ),
    mnCtlsStrmSize( 0 ),
    mbEmbedded( false ),
    mbLinked( false ),
    mbSymbol( false ),
    mbControl( false ),
    mbUseCtlsStrm( false )
{
    SetAreaObj( true );
    SetSimpleMacro( true );
    SetCustomDffObj( true );
}

OUString XclImpPictureObj::GetOleStorageName() const
{
    OUStringBuffer aStrgName;
    if( (mbEmbedded || mbLinked) && !mbControl && (mnStorageId > 0) )
    {
        aStrgName = mbEmbedded ? std::u16string_view(u"" EXC_STORAGE_OLE_EMBEDDED) : std::u16string_view(u"" EXC_STORAGE_OLE_LINKED);
        static const char spcHexChars[] = "0123456789ABCDEF";
        for( sal_uInt8 nIndex = 32; nIndex > 0; nIndex -= 4 )
            aStrgName.append(OUStringChar( spcHexChars[ ::extract_value< sal_uInt8 >( mnStorageId, nIndex - 4, 4 ) ] ));
    }
    return aStrgName.makeStringAndClear();
}

void XclImpPictureObj::DoReadObj3( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    sal_uInt16 nLinkSize;
    ReadFrameData( rStrm );
    rStrm.Ignore( 6 );
    nLinkSize = rStrm.ReaduInt16();
    rStrm.Ignore( 2 );
    ReadFlags3( rStrm );
    ReadMacro3( rStrm, nMacroSize );
    ReadPictFmla( rStrm, nLinkSize );

    if( (rStrm.GetNextRecId() == EXC_ID3_IMGDATA) && rStrm.StartNextRecord() )
        maGraphic = XclImpDrawing::ReadImgData( GetRoot(), rStrm );
}

void XclImpPictureObj::DoReadObj4( XclImpStream& rStrm, sal_uInt16 nMacroSize )
{
    sal_uInt16 nLinkSize;
    ReadFrameData( rStrm );
    rStrm.Ignore( 6 );
    nLinkSize = rStrm.ReaduInt16();
    rStrm.Ignore( 2 );
    ReadFlags3( rStrm );
    ReadMacro4( rStrm, nMacroSize );
    ReadPictFmla( rStrm, nLinkSize );

    if( (rStrm.GetNextRecId() == EXC_ID3_IMGDATA) && rStrm.StartNextRecord() )
        maGraphic = XclImpDrawing::ReadImgData( GetRoot(), rStrm );
}

void XclImpPictureObj::DoReadObj5( XclImpStream& rStrm, sal_uInt16 nNameLen, sal_uInt16 nMacroSize )
{
    sal_uInt16 nLinkSize;
    ReadFrameData( rStrm );
    rStrm.Ignore( 6 );
    nLinkSize = rStrm.ReaduInt16();
    rStrm.Ignore( 2 );
    ReadFlags3( rStrm );
    rStrm.Ignore( 4 );
    ReadName5( rStrm, nNameLen );
    ReadMacro5( rStrm, nMacroSize );
    ReadPictFmla( rStrm, nLinkSize );

    if( (rStrm.GetNextRecId() == EXC_ID3_IMGDATA) && rStrm.StartNextRecord() )
    {
        // page background is stored as hidden picture with name "__BkgndObj"
        if ( IsHidden() && (GetObjName() == "__BkgndObj") )
            GetPageSettings().ReadImgData( rStrm );
        else
            maGraphic = XclImpDrawing::ReadImgData( GetRoot(), rStrm );
    }
}

void XclImpPictureObj::DoReadObj8SubRec( XclImpStream& rStrm, sal_uInt16 nSubRecId, sal_uInt16 nSubRecSize )
{
    switch( nSubRecId )
    {
        case EXC_ID_OBJFLAGS:
            ReadFlags8( rStrm );
        break;
        case EXC_ID_OBJPICTFMLA:
            ReadPictFmla( rStrm, rStrm.ReaduInt16() );
        break;
        default:
            XclImpDrawObjBase::DoReadObj8SubRec( rStrm, nSubRecId, nSubRecSize );
    }
}

rtl::Reference<SdrObject> XclImpPictureObj::DoCreateSdrObj( XclImpDffConverter& rDffConv, const tools::Rectangle& rAnchorRect ) const
{
    // try to create an OLE object or form control
    rtl::Reference<SdrObject> xSdrObj( rDffConv.CreateSdrObject( *this, rAnchorRect ) );

    // insert a graphic replacement for unsupported ole object ( if none already
    // exists ) Hmm ok, it's possibly that there has been some imported
    // graphic at a base level  but unlikely, normally controls have a valid
    // preview in the IMGDATA record ( see below )
    // It might be possible to push such an imported graphic up to this
    // XclImpPictureObj instance but there are so many layers of indirection I
    // don't see an easy way. This way at least ensures that we can
    // avoid a 'blank' shape that can result from a failed control import
    if ( !xSdrObj && IsOcxControl() && maGraphic.GetType() == GraphicType::NONE )
    {
        const_cast< XclImpPictureObj* >( this )->maGraphic =
                SdrOle2Obj::GetEmptyOLEReplacementGraphic();
    }
    // no OLE - create a plain picture from IMGDATA record data
    if( !xSdrObj && (maGraphic.GetType() != GraphicType::NONE) )
    {
        xSdrObj =
            new SdrGrafObj(
                *GetDoc().GetDrawLayer(),
                maGraphic,
                rAnchorRect);
        ConvertRectStyle( *xSdrObj );
    }

    rDffConv.Progress();
    return xSdrObj;
}

OUString XclImpPictureObj::GetObjName() const
{
    if( IsOcxControl() )
    {
        OUString sName( GetObjectManager().GetOleNameOverride( GetTab(), GetObjId() ) );
        if (!sName.isEmpty())
            return sName;
    }
    return XclImpDrawObjBase::GetObjName();
}

void XclImpPictureObj::DoPreProcessSdrObj( XclImpDffConverter& rDffConv, SdrObject& rSdrObj ) const
{
    if( IsOcxControl() )
    {
        // do not call XclImpRectObj::DoPreProcessSdrObj(), it would trace missing "printable" feature
        ProcessControl( *this );
    }
    else if( mbEmbedded || mbLinked )
    {
        // trace missing "printable" feature
        XclImpRectObj::DoPreProcessSdrObj( rDffConv, rSdrObj );

        SfxObjectShell* pDocShell = GetDocShell();
        SdrOle2Obj* pOleSdrObj = dynamic_cast< SdrOle2Obj* >( &rSdrObj );
        if( pOleSdrObj && pDocShell )
        {
            comphelper::EmbeddedObjectContainer& rEmbObjCont = pDocShell->GetEmbeddedObjectContainer();
            Reference< XEmbeddedObject > xEmbObj = pOleSdrObj->GetObjRef();
            OUString aOldName( pOleSdrObj->GetPersistName() );

            /*  The object persistence should be already in the storage, but
                the object still might not be inserted into the container. */
            if( rEmbObjCont.HasEmbeddedObject( aOldName ) )
            {
                if( !rEmbObjCont.HasEmbeddedObject( xEmbObj ) )
                    // filter code is allowed to call the following method
                    rEmbObjCont.AddEmbeddedObject( xEmbObj, aOldName );
            }
            else
            {
                /*  If the object is still not in container it must be inserted
                    there, the name must be generated in this case. */
                OUString aNewName;
                rEmbObjCont.InsertEmbeddedObject( xEmbObj, aNewName );
                if( aOldName != aNewName )
                    // SetPersistName, not SetName
                    pOleSdrObj->SetPersistName( aNewName );
            }
        }
    }
}

void XclImpPictureObj::ReadFlags3( XclImpStream& rStrm )
{
    sal_uInt16 nFlags;
    nFlags = rStrm.ReaduInt16();
    mbSymbol = ::get_flag( nFlags, EXC_OBJ_PIC_SYMBOL );
}

void XclImpPictureObj::ReadFlags8( XclImpStream& rStrm )
{
    sal_uInt16 nFlags;
    nFlags = rStrm.ReaduInt16();
    mbSymbol      = ::get_flag( nFlags, EXC_OBJ_PIC_SYMBOL );
    mbControl     = ::get_flag( nFlags, EXC_OBJ_PIC_CONTROL );
    mbUseCtlsStrm = ::get_flag( nFlags, EXC_OBJ_PIC_CTLSSTREAM );
    OSL_ENSURE( mbControl || !mbUseCtlsStrm, "XclImpPictureObj::ReadFlags8 - CTLS stream for controls only" );
    SetProcessSdrObj( mbControl || !mbUseCtlsStrm );
}

void XclImpPictureObj::ReadPictFmla( XclImpStream& rStrm, sal_uInt16 nLinkSize )
{
    std::size_t nLinkEnd = rStrm.GetRecPos() + nLinkSize;
    if( nLinkSize >= 6 )
    {
        sal_uInt16 nFmlaSize;
        nFmlaSize = rStrm.ReaduInt16();
        OSL_ENSURE( nFmlaSize > 0, "XclImpPictureObj::ReadPictFmla - missing link formula" );
        // BIFF3/BIFF4 do not support storages, nothing to do here
        if( (nFmlaSize > 0) && (GetBiff() >= EXC_BIFF5) )
        {
            rStrm.Ignore( 4 );
            sal_uInt8 nToken;
            nToken = rStrm.ReaduInt8();

            // different processing for linked vs. embedded OLE objects
            if( nToken == XclTokenArrayHelper::GetTokenId( EXC_TOKID_NAMEX, EXC_TOKCLASS_REF ) )
            {
                mbLinked = true;
                switch( GetBiff() )
                {
                    case EXC_BIFF5:
                    {
                        sal_Int16 nRefIdx;
                        sal_uInt16 nNameIdx;
                        nRefIdx = rStrm.ReadInt16();
                        rStrm.Ignore( 8 );
                        nNameIdx = rStrm.ReaduInt16();
                        rStrm.Ignore( 12 );
                        const ExtName* pExtName = GetOldRoot().pExtNameBuff->GetNameByIndex( nRefIdx, nNameIdx );
                        if( pExtName && pExtName->IsOLE() )
                            mnStorageId = pExtName->nStorageId;
                    }
                    break;
                    case EXC_BIFF8:
                    {
                        sal_uInt16 nXti, nExtName;
                        nXti = rStrm.ReaduInt16();
                        nExtName = rStrm.ReaduInt16();
                        const XclImpExtName* pExtName = GetLinkManager().GetExternName( nXti, nExtName );
                        if( pExtName && (pExtName->GetType() == xlExtOLE) )
                            mnStorageId = pExtName->GetStorageId();
                    }
                    break;
                    default:
                        DBG_ERROR_BIFF();
                }
            }
            else if( nToken == XclTokenArrayHelper::GetTokenId( EXC_TOKID_TBL, EXC_TOKCLASS_NONE ) )
            {
                mbEmbedded = true;
                OSL_ENSURE( nFmlaSize == 5, "XclImpPictureObj::ReadPictFmla - unexpected formula size" );
                rStrm.Ignore( nFmlaSize - 1 );      // token ID already read
                if( nFmlaSize & 1 )
                    rStrm.Ignore( 1 );              // padding byte

                // a class name may follow inside the picture link
                if( rStrm.GetRecPos() + 2 <= nLinkEnd )
                {
                    sal_uInt16 nLen = rStrm.ReaduInt16();
                    if( nLen > 0 )
                        maClassName = (GetBiff() == EXC_BIFF8) ? rStrm.ReadUniString( nLen ) : rStrm.ReadRawByteString( nLen );
                }
            }
            // else: ignore other formulas, e.g. pictures linked to cell ranges
        }
    }

    // seek behind picture link data
    rStrm.Seek( nLinkEnd );

    // read additional data for embedded OLE objects following the picture link
    if( IsOcxControl() )
    {
        // #i26521# form controls to be ignored
        if( maClassName == "Forms.HTML:Hidden.1"  )
        {
            SetProcessSdrObj( false );
            return;
        }

        if( rStrm.GetRecLeft() <= 8 ) return;

        // position and size of control data in 'Ctls' stream
        mnCtlsStrmPos = static_cast< std::size_t >( rStrm.ReaduInt32() );
        mnCtlsStrmSize = static_cast< std::size_t >( rStrm.ReaduInt32() );

        if( rStrm.GetRecLeft() <= 8 ) return;

        // additional string (16-bit characters), e.g. for progress bar control
        sal_uInt32 nAddStrSize;
        nAddStrSize = rStrm.ReaduInt32();
        OSL_ENSURE( rStrm.GetRecLeft() >= nAddStrSize + 4, "XclImpPictureObj::ReadPictFmla - missing data" );
        if( rStrm.GetRecLeft() >= nAddStrSize + 4 )
        {
            rStrm.Ignore( nAddStrSize );
            // cell link and source range
            ReadCellLinkFormula( rStrm, true );
            ReadSourceRangeFormula( rStrm, true );
        }
    }
    else if( mbEmbedded && (rStrm.GetRecLeft() >= 4) )
    {
        mnStorageId = rStrm.ReaduInt32();
    }
}

// DFF stream conversion ======================================================

void XclImpSolverContainer::InsertSdrObjectInfo( SdrObject& rSdrObj, sal_uInt32 nDffShapeId, ShapeFlag nDffFlags )
{
    if( nDffShapeId > 0 )
    {
        maSdrInfoMap[ nDffShapeId ].Set( &rSdrObj, nDffFlags );
        maSdrObjMap[ &rSdrObj ] = nDffShapeId;
    }
}

void XclImpSolverContainer::RemoveSdrObjectInfo( SdrObject& rSdrObj )
{
    // remove info of passed object from the maps
    XclImpSdrObjMap::iterator aIt = maSdrObjMap.find( &rSdrObj );
    if( aIt != maSdrObjMap.end() )
    {
        maSdrInfoMap.erase( aIt->second );
        maSdrObjMap.erase( aIt );
    }

    // remove info of all child objects of a group object
    if( SdrObjGroup* pGroupObj = dynamic_cast< SdrObjGroup* >( &rSdrObj ) )
    {
        if( SdrObjList* pSubList = pGroupObj->GetSubList() )
        {
            // iterate flat over the list because this function already works recursively
            SdrObjListIter aObjIt( pSubList, SdrIterMode::Flat );
            for( SdrObject* pChildObj = aObjIt.Next(); pChildObj; pChildObj = aObjIt.Next() )
                RemoveSdrObjectInfo( *pChildObj );
        }
    }
}

void XclImpSolverContainer::UpdateConnectorRules()
{
    for (auto const & pRule : aCList)
    {
        UpdateConnection( pRule->nShapeA, pRule->pAObj, &pRule->nSpFlagsA );
        UpdateConnection( pRule->nShapeB, pRule->pBObj, &pRule->nSpFlagsB );
        UpdateConnection( pRule->nShapeC, pRule->pCObj );
    }
}

void XclImpSolverContainer::RemoveConnectorRules()
{
    aCList.clear();
    maSdrInfoMap.clear();
    maSdrObjMap.clear();
}

void XclImpSolverContainer::UpdateConnection( sal_uInt32 nDffShapeId, SdrObject*& rpSdrObj, ShapeFlag* pnDffFlags )
{
    XclImpSdrInfoMap::const_iterator aIt = maSdrInfoMap.find( nDffShapeId );
    if( aIt != maSdrInfoMap.end() )
    {
        rpSdrObj = aIt->second.mpSdrObj;
        if( pnDffFlags )
            *pnDffFlags = aIt->second.mnDffFlags;
    }
}

XclImpSimpleDffConverter::XclImpSimpleDffConverter( const XclImpRoot& rRoot, SvStream& rDffStrm ) :
    SvxMSDffManager( rDffStrm, rRoot.GetBasePath(), 0, nullptr, rRoot.GetDoc().GetDrawLayer(), 1440, COL_DEFAULT, nullptr ),
    XclImpRoot( rRoot )
{
    SetSvxMSDffSettings( SVXMSDFF_SETTINGS_CROP_BITMAPS | SVXMSDFF_SETTINGS_IMPORT_EXCEL );
}

XclImpSimpleDffConverter::~XclImpSimpleDffConverter()
{
}

bool XclImpSimpleDffConverter::GetColorFromPalette( sal_uInt16 nIndex, Color& rColor ) const
{
    Color nColor = GetPalette().GetColor( nIndex );

    if( nColor == COL_AUTO )
        return false;

    rColor = nColor;
    return true;
}

XclImpDffConverter::XclImpDffConvData::XclImpDffConvData(
        XclImpDrawing& rDrawing, SdrModel& rSdrModel, SdrPage& rSdrPage ) :
    mrDrawing( rDrawing ),
    mrSdrModel( rSdrModel ),
    mrSdrPage( rSdrPage ),
    mnLastCtrlIndex( -1 ),
    mbHasCtrlForm( false )
{
}

constexpr OUString gaStdFormName( u"Standard"_ustr ); /// Standard name of control forms.

XclImpDffConverter::XclImpDffConverter( const XclImpRoot& rRoot, SvStream& rDffStrm ) :
    XclImpSimpleDffConverter( rRoot, rDffStrm ),
    oox::ole::MSConvertOCXControls( rRoot.GetDocShell()->GetModel() ),
    mnOleImpFlags( 0 ),
    mbNotifyMacroEventRead(false)
{
    if( officecfg::Office::Common::Filter::Microsoft::Import::MathTypeToMath::get() )
        mnOleImpFlags |= OLE_MATHTYPE_2_STARMATH;
    if( officecfg::Office::Common::Filter::Microsoft::Import::WinWordToWriter::get() )
        mnOleImpFlags |= OLE_WINWORD_2_STARWRITER;
    if( officecfg::Office::Common::Filter::Microsoft::Import::PowerPointToImpress::get() )
        mnOleImpFlags |= OLE_POWERPOINT_2_STARIMPRESS;

    // try to open the 'Ctls' storage stream containing OCX control properties
    mxCtlsStrm = OpenStream( EXC_STREAM_CTLS );

    // default text margin (convert EMU to drawing layer units)
    mnDefTextMargin = EXC_OBJ_TEXT_MARGIN;
    ScaleEmu( mnDefTextMargin );
}

XclImpDffConverter::~XclImpDffConverter()
{
}

OUString XclImpObjectManager::GetOleNameOverride( SCTAB nTab, sal_uInt16 nObjId )
{
    OUString sOleName;
    OUString sCodeName = GetExtDocOptions().GetCodeName( nTab );

    if (mxOleCtrlNameOverride.is() && mxOleCtrlNameOverride->hasByName(sCodeName))
    {
        Reference< XIndexContainer > xIdToOleName;
        mxOleCtrlNameOverride->getByName( sCodeName ) >>= xIdToOleName;
        xIdToOleName->getByIndex( nObjId ) >>= sOleName;
    }

    return sOleName;
}

void XclImpDffConverter::StartProgressBar( std::size_t nProgressSize )
{
    mxProgress = std::make_shared<ScfProgressBar>( GetDocShell(), STR_PROGRESS_CALCULATING );
    mxProgress->AddSegment( nProgressSize );
    mxProgress->Activate();
}

void XclImpDffConverter::Progress( std::size_t nDelta )
{
    OSL_ENSURE( mxProgress, "XclImpDffConverter::Progress - invalid call, no progress bar" );
    mxProgress->Progress( nDelta );
}

void XclImpDffConverter::InitializeDrawing( XclImpDrawing& rDrawing, SdrModel& rSdrModel, SdrPage& rSdrPage )
{
    XclImpDffConvDataRef xConvData = std::make_shared<XclImpDffConvData>( rDrawing, rSdrModel, rSdrPage );
    maDataStack.push_back( xConvData );
    SetModel( &xConvData->mrSdrModel, 1440 );
}

void XclImpDffConverter::ProcessObject( SdrObjList& rObjList, XclImpDrawObjBase& rDrawObj )
{
    if( !rDrawObj.IsProcessSdrObj() )
        return;

    const XclObjAnchor* pAnchor = rDrawObj.GetAnchor();
    if(!pAnchor)
        return;

    tools::Rectangle aAnchorRect = GetConvData().mrDrawing.CalcAnchorRect( *pAnchor, false );
    if( rDrawObj.IsValidSize( aAnchorRect ) )
    {
        // CreateSdrObject() recursively creates embedded child objects
        rtl::Reference<SdrObject> xSdrObj( rDrawObj.CreateSdrObject( *this, aAnchorRect, false ) );
        if( xSdrObj )
            rDrawObj.PreProcessSdrObject( *this, *xSdrObj );
        // call InsertSdrObject() also, if SdrObject is missing
        InsertSdrObject( rObjList, rDrawObj, xSdrObj.get() );
    }
}

void XclImpDffConverter::ProcessDrawing( const XclImpDrawObjVector& rDrawObjs )
{
    SdrPage& rSdrPage = GetConvData().mrSdrPage;
    for( const auto& rxDrawObj : rDrawObjs )
        ProcessObject( rSdrPage, *rxDrawObj );
}

void XclImpDffConverter::ProcessDrawing( SvStream& rDffStrm )
{
    if( rDffStrm.TellEnd() > 0 )
    {
        rDffStrm.Seek( STREAM_SEEK_TO_BEGIN );
        DffRecordHeader aHeader;
        ReadDffRecordHeader( rDffStrm, aHeader );
        OSL_ENSURE( aHeader.nRecType == DFF_msofbtDgContainer, "XclImpDffConverter::ProcessDrawing - unexpected record" );
        if( aHeader.nRecType == DFF_msofbtDgContainer )
            ProcessDgContainer( rDffStrm, aHeader );
    }
}

void XclImpDffConverter::FinalizeDrawing()
{
    OSL_ENSURE( !maDataStack.empty(), "XclImpDffConverter::FinalizeDrawing - no drawing manager on stack" );
    maDataStack.pop_back();
    // restore previous model at core DFF converter
    if( !maDataStack.empty() )
        SetModel( &maDataStack.back()->mrSdrModel, 1440 );
}

void XclImpDffConverter::NotifyMacroEventRead()
{
    if (mbNotifyMacroEventRead)
        return;
    comphelper::DocumentInfo::notifyMacroEventRead(mxModel);
    mbNotifyMacroEventRead = true;
}

rtl::Reference<SdrObject> XclImpDffConverter::CreateSdrObject( const XclImpTbxObjBase& rTbxObj, const tools::Rectangle& rAnchorRect )
{
    rtl::Reference<SdrObject> xSdrObj;

    OUString aServiceName = rTbxObj.GetServiceName();
    if( SupportsOleObjects() && !aServiceName.isEmpty() ) try
    {
        // create the form control from scratch
        Reference< XFormComponent > xFormComp( ScfApiHelper::CreateInstance( GetDocShell(), aServiceName ), UNO_QUERY_THROW );
        // set controls form, needed in virtual function InsertControl()
        InitControlForm();
        // try to insert the control into the form
        css::awt::Size aDummySize;
        Reference< XShape > xShape;
        XclImpDffConvData& rConvData = GetConvData();
        if( rConvData.mxCtrlForm.is() && InsertControl( xFormComp, aDummySize, &xShape, true ) )
        {
            xSdrObj = rTbxObj.CreateSdrObjectFromShape( xShape, rAnchorRect );
            // try to attach a macro to the control
            ScriptEventDescriptor aDescriptor;
            if( (rConvData.mnLastCtrlIndex >= 0) && rTbxObj.FillMacroDescriptor( aDescriptor ) )
            {
                NotifyMacroEventRead();
                Reference< XEventAttacherManager > xEventMgr( rConvData.mxCtrlForm, UNO_QUERY_THROW );
                xEventMgr->registerScriptEvent( rConvData.mnLastCtrlIndex, aDescriptor );
            }
        }
    }
    catch( const Exception& )
    {
    }

    return xSdrObj;
}

rtl::Reference<SdrObject> XclImpDffConverter::CreateSdrObject( const XclImpPictureObj& rPicObj, const tools::Rectangle& rAnchorRect )
{
    rtl::Reference<SdrObject> xSdrObj;

    if( SupportsOleObjects() )
    {
        if( rPicObj.IsOcxControl() )
        {
            if( mxCtlsStrm.is() ) try
            {
                /*  set controls form, needed in virtual function InsertControl()
                    called from ReadOCXExcelKludgeStream() */
                InitControlForm();

                // read from mxCtlsStrm into xShape, insert the control model into the form
                Reference< XShape > xShape;
                if( GetConvData().mxCtrlForm.is() )
                {
                     Reference< XFormComponent >  xFComp;
                     ReadOCXCtlsStream( mxCtlsStrm, xFComp, rPicObj.GetCtlsStreamPos(),  rPicObj.GetCtlsStreamSize() );
                     // recreate the method formerly known as ReadOCXExcelKludgeStream()
                     if ( xFComp.is() )
                     {
                         css::awt::Size aSz;  // not used in import
                         ScfPropertySet aPropSet( xFComp );
                         aPropSet.SetStringProperty( u"Name"_ustr, rPicObj.GetObjName() );
                         InsertControl( xFComp, aSz,&xShape,true);
                         xSdrObj = rPicObj.CreateSdrObjectFromShape( xShape, rAnchorRect );
                     }
                }
            }
            catch( const Exception& )
            {
            }
        }
        else
        {
            SfxObjectShell* pDocShell = GetDocShell();
            rtl::Reference<SotStorage> xSrcStrg = GetRootStorage();
            OUString aStrgName = rPicObj.GetOleStorageName();
            if( pDocShell && xSrcStrg.is() && (!aStrgName.isEmpty()) )
            {
                // first try to resolve graphic from DFF storage
                Graphic aGraphic;
                tools::Rectangle aVisArea;
                if( !GetBLIP( GetPropertyValue( DFF_Prop_pib, 0 ), aGraphic, &aVisArea ) )
                {
                    // if not found, use graphic from object (imported from IMGDATA record)
                    aGraphic = rPicObj.GetGraphic();
                }
                if( aGraphic.GetType() != GraphicType::NONE )
                {
                    ErrCode nError = ERRCODE_NONE;
                    namespace cssea = css::embed::Aspects;
                    sal_Int64 nAspects = rPicObj.IsSymbol() ? cssea::MSOLE_ICON : cssea::MSOLE_CONTENT;
                    xSdrObj =
                        CreateSdrOLEFromStorage(
                            GetConvData().mrSdrModel,
                            aStrgName,
                            xSrcStrg,
                            pDocShell->GetStorage(),
                            aGraphic,
                            rAnchorRect,
                            aVisArea,
                            nullptr,
                            nError,
                            mnOleImpFlags,
                            nAspects,
                            GetRoot().GetMedium().GetBaseURL());
                }
            }
        }
    }

    return xSdrObj;
}

bool XclImpDffConverter::SupportsOleObjects() const
{
    return GetConvData().mrDrawing.SupportsOleObjects();
}

// virtual functions ----------------------------------------------------------

void XclImpDffConverter::ProcessClientAnchor2( SvStream& rDffStrm,
        DffRecordHeader& rHeader, DffObjData& rObjData )
{
    // find the OBJ record data related to the processed shape
    XclImpDffConvData& rConvData = GetConvData();
    XclImpDrawObjBase* pDrawObj = rConvData.mrDrawing.FindDrawObj( rObjData.rSpHd ).get();
    if(!pDrawObj)
        return;

    OSL_ENSURE( rHeader.nRecType == DFF_msofbtClientAnchor, "XclImpDffConverter::ProcessClientAnchor2 - no client anchor record" );
    XclObjAnchor aAnchor;
    rHeader.SeekToContent( rDffStrm );
    sal_uInt8 nFlags(0);
    rDffStrm.ReadUChar( nFlags );
    rDffStrm.SeekRel( 1 );  // flags
    rDffStrm >> aAnchor;    // anchor format equal to BIFF5 OBJ records

    if (!rDffStrm.good())
    {
        SAL_WARN("sc.filter", "ProcessClientAnchor2 short read");
        return;
    }

    pDrawObj->SetAnchor( aAnchor );
    rObjData.aChildAnchor = rConvData.mrDrawing.CalcAnchorRect( aAnchor, true );
    rObjData.bChildAnchor = true;
    // page anchoring is the best approximation we have if mbMove
    // is set
    rObjData.bPageAnchor = ( nFlags & 0x1 );
}

namespace {

struct XclImpDrawObjClientData : public SvxMSDffClientData
{
    const XclImpDrawObjBase* m_pTopLevelObj;

    XclImpDrawObjClientData()
        : m_pTopLevelObj(nullptr)
    {
    }
    virtual void NotifyFreeObj(SdrObject*) override {}
};

}

rtl::Reference<SdrObject> XclImpDffConverter::ProcessObj( SvStream& rDffStrm, DffObjData& rDffObjData,
        SvxMSDffClientData& rClientData, tools::Rectangle& /*rTextRect*/, SdrObject* pOldSdrObj )
{
    XclImpDffConvData& rConvData = GetConvData();

    /*  pOldSdrObj passes a generated SdrObject. This function owns this object
        and can modify it. The function has either to return it back to caller
        or to delete it by itself. */
    rtl::Reference<SdrObject> xSdrObj( pOldSdrObj );

    // find the OBJ record data related to the processed shape
    XclImpDrawObjRef xDrawObj = rConvData.mrDrawing.FindDrawObj( rDffObjData.rSpHd );
    const tools::Rectangle& rAnchorRect = rDffObjData.aChildAnchor;

    // Do not process the global page group shape
    bool bGlobalPageGroup( rDffObjData.nSpFlags & ShapeFlag::Patriarch );
    if( !xDrawObj || !xDrawObj->IsProcessSdrObj() || bGlobalPageGroup )
        return nullptr;   // simply return, xSdrObj will be destroyed

    /*  Pass pointer to top-level object back to caller. If the processed
        object is embedded in a group, the pointer is already set to the
        top-level parent object. */
    XclImpDrawObjClientData& rDrawObjClientData = static_cast<XclImpDrawObjClientData&>(rClientData);
    const bool bIsTopLevel = !rDrawObjClientData.m_pTopLevelObj;
    if (bIsTopLevel )
        rDrawObjClientData.m_pTopLevelObj = xDrawObj.get();

    // connectors don't have to be area objects
    if( dynamic_cast< SdrEdgeObj* >( xSdrObj.get() ) )
        xDrawObj->SetAreaObj( false );

    /*  Check for valid size for all objects. Needed to ignore lots of invisible
        phantom objects from deleted rows or columns (for performance reasons).
        #i30816# Include objects embedded in groups.
        #i58780# Ignore group shapes, size is not initialized. */
    bool bEmbeddedGroup = !bIsTopLevel && dynamic_cast< SdrObjGroup* >( xSdrObj.get() );
    if( !bEmbeddedGroup && !xDrawObj->IsValidSize( rAnchorRect ) )
        return nullptr;   // simply return, xSdrObj will be destroyed

    // set shape information from DFF stream
    OUString aObjName = GetPropertyString( DFF_Prop_wzName, rDffStrm );
    OUString aHyperlink = ReadHlinkProperty( rDffStrm );
    bool bVisible = !GetPropertyBool( DFF_Prop_fHidden );
    bool bAutoMargin = GetPropertyBool( DFF_Prop_AutoTextMargin );
    xDrawObj->SetDffData( rDffObjData, aObjName, aHyperlink, bVisible, bAutoMargin );

    /*  Connect textbox data (string, alignment, text orientation) to object.
        don't ask for a text-ID, DFF export doesn't set one. */
    if( XclImpTextObj* pTextObj = dynamic_cast< XclImpTextObj* >( xDrawObj.get() ) )
        if( const XclImpObjTextData* pTextData = rConvData.mrDrawing.FindTextData( rDffObjData.rSpHd ) )
            pTextObj->SetTextData( *pTextData );

    // copy line and fill formatting of TBX form controls from DFF properties
    if( XclImpTbxObjBase* pTbxObj = dynamic_cast< XclImpTbxObjBase* >( xDrawObj.get() ) )
        pTbxObj->SetDffProperties( *this );

    // try to create a custom SdrObject that overwrites the passed object
    rtl::Reference<SdrObject> xNewSdrObj( xDrawObj->CreateSdrObject( *this, rAnchorRect, true ) );
    if( xNewSdrObj )
        xSdrObj = std::move( xNewSdrObj );

    // process the SdrObject
    if( xSdrObj )
    {
        // filled without color -> set system window color
        if( GetPropertyBool( DFF_Prop_fFilled ) && !IsProperty( DFF_Prop_fillColor ) )
            xSdrObj->SetMergedItem( XFillColorItem( OUString(), GetPalette().GetColor( EXC_COLOR_WINDOWBACK ) ) );

        // additional processing on the SdrObject
        xDrawObj->PreProcessSdrObject( *this, *xSdrObj );

        /*  If the SdrObject will not be inserted into the draw page, delete it
            here. Happens e.g. for notes: The PreProcessSdrObject() call above
            has inserted the note into the document, and the SdrObject is not
            needed anymore. */
        if( !xDrawObj->IsInsertSdrObj() )
            xSdrObj.clear();
    }

    if( xSdrObj )
    {
        /*  Store the relation between shape ID and SdrObject for connectors.
            Must be done here (and not in InsertSdrObject() function),
            otherwise all SdrObjects embedded in groups would be lost. */
        rConvData.maSolverCont.InsertSdrObjectInfo( *xSdrObj, xDrawObj->GetDffShapeId(), xDrawObj->GetDffFlags() );

        /*  If the drawing object is embedded in a group object, call
            PostProcessSdrObject() here. For top-level objects this will be
            done automatically in InsertSdrObject() but grouped shapes are
            inserted into their groups somewhere in the SvxMSDffManager base
            class without chance of notification. Unfortunately, now this is
            called before the object is really inserted into its group object,
            but that should not have any effect for grouped objects. */
        if( !bIsTopLevel )
            xDrawObj->PostProcessSdrObject( *this, *xSdrObj );
    }

    return xSdrObj;
}

SdrObject* XclImpDffConverter::FinalizeObj(DffObjData& rDffObjData, SdrObject* pOldSdrObj )
{
    XclImpDffConvData& rConvData = GetConvData();

    /*  pOldSdrObj passes a generated SdrObject. This function owns this object
        and can modify it. The function has either to return it back to caller
        or to delete it by itself. */
    rtl::Reference<SdrObject> xSdrObj( pOldSdrObj );

    // find the OBJ record data related to the processed shape
    XclImpDrawObjRef xDrawObj = rConvData.mrDrawing.FindDrawObj( rDffObjData.rSpHd );

    if( xSdrObj && xDrawObj )
    {
        // cell anchoring
        if ( !rDffObjData.bPageAnchor )
            ScDrawLayer::SetCellAnchoredFromPosition( *xSdrObj,  GetDoc(), xDrawObj->GetTab(), false );
    }

    return xSdrObj.get();
}

bool XclImpDffConverter::InsertControl( const Reference< XFormComponent >& rxFormComp,
        const css::awt::Size& /*rSize*/, Reference< XShape >* pxShape,
        bool /*bFloatingCtrl*/ )
{
    if( GetDocShell() ) try
    {
        XclImpDffConvData& rConvData = GetConvData();
        Reference< XIndexContainer > xFormIC( rConvData.mxCtrlForm, UNO_QUERY_THROW );
        Reference< XControlModel > xCtrlModel( rxFormComp, UNO_QUERY_THROW );

        // create the control shape
        Reference< XShape > xShape( ScfApiHelper::CreateInstance( GetDocShell(), u"com.sun.star.drawing.ControlShape"_ustr ), UNO_QUERY_THROW );
        Reference< XControlShape > xCtrlShape( xShape, UNO_QUERY_THROW );

        // insert the new control into the form
        sal_Int32 nNewIndex = xFormIC->getCount();
        xFormIC->insertByIndex( nNewIndex, Any( rxFormComp ) );
        // on success: store new index of the control for later use (macro events)
        rConvData.mnLastCtrlIndex = nNewIndex;

        // set control model at control shape and pass back shape to caller
        xCtrlShape->setControl( xCtrlModel );
        if( pxShape ) *pxShape = std::move(xShape);
        return true;
    }
    catch( const Exception& )
    {
        OSL_FAIL( "XclImpDffConverter::InsertControl - cannot create form control" );
    }

    return false;
}

// private --------------------------------------------------------------------

XclImpDffConverter::XclImpDffConvData& XclImpDffConverter::GetConvData()
{
    OSL_ENSURE( !maDataStack.empty(), "XclImpDffConverter::GetConvData - no drawing manager on stack" );
    return *maDataStack.back();
}

const XclImpDffConverter::XclImpDffConvData& XclImpDffConverter::GetConvData() const
{
    OSL_ENSURE( !maDataStack.empty(), "XclImpDffConverter::GetConvData - no drawing manager on stack" );
    return *maDataStack.back();
}

OUString XclImpDffConverter::ReadHlinkProperty( SvStream& rDffStrm ) const
{
    /*  Reads hyperlink data from a complex DFF property. Contents of this
        property are equal to the HLINK record, import of this record is
        implemented in class XclImpHyperlink. This function has to create an
        instance of the XclImpStream class to be able to reuse the
        functionality of XclImpHyperlink. */
    OUString aString;
    sal_uInt32 nBufferSize = GetPropertyValue( DFF_Prop_pihlShape, 0 );
    if( (0 < nBufferSize) && (nBufferSize <= 0xFFFF) && SeekToContent( DFF_Prop_pihlShape, rDffStrm ) )
    {
        // create a faked BIFF record that can be read by XclImpStream class
        SvMemoryStream aMemStream;
        aMemStream.WriteUInt16( 0 ).WriteUInt16( nBufferSize );

        // copy from DFF stream to memory stream
        ::std::vector< sal_uInt8 > aBuffer( nBufferSize );
        sal_uInt8* pnData = aBuffer.data();
        if (rDffStrm.ReadBytes(pnData, nBufferSize) == nBufferSize)
        {
            aMemStream.WriteBytes(pnData, nBufferSize);

            // create BIFF import stream to be able to use XclImpHyperlink class
            XclImpStream aXclStrm( aMemStream, GetRoot() );
            if( aXclStrm.StartNextRecord() )
                aString = XclImpHyperlink::ReadEmbeddedData( aXclStrm );
        }
    }
    return aString;
}

bool XclImpDffConverter::ProcessDgContainer( SvStream& rDffStrm, const DffRecordHeader& rDgHeader )
{
    std::size_t nEndPos = rDgHeader.GetRecEndFilePos();
    bool isBreak(false);
    while (!isBreak && rDffStrm.good() && rDffStrm.Tell() < nEndPos)
    {
        DffRecordHeader aHeader;
        ReadDffRecordHeader( rDffStrm, aHeader );
        switch( aHeader.nRecType )
        {
            case DFF_msofbtSolverContainer:
                isBreak = !ProcessSolverContainer( rDffStrm, aHeader );
            break;
            case DFF_msofbtSpgrContainer:
                isBreak = !ProcessShGrContainer( rDffStrm, aHeader );
            break;
            default:
                isBreak = !aHeader.SeekToEndOfRecord( rDffStrm );
        }
    }
    // seek to end of drawing page container
    isBreak = !rDgHeader.SeekToEndOfRecord( rDffStrm );

    // #i12638# #i37900# connector rules
    XclImpSolverContainer& rSolverCont = GetConvData().maSolverCont;
    rSolverCont.UpdateConnectorRules();
    SolveSolver( rSolverCont );
    rSolverCont.RemoveConnectorRules();
    return !isBreak;
}

bool XclImpDffConverter::ProcessShGrContainer( SvStream& rDffStrm, const DffRecordHeader& rShGrHeader )
{
    std::size_t nEndPos = rShGrHeader.GetRecEndFilePos();
    bool isBreak(false);
    while (!isBreak && rDffStrm.good() && rDffStrm.Tell() < nEndPos)
    {
        DffRecordHeader aHeader;
        ReadDffRecordHeader( rDffStrm, aHeader );
        switch( aHeader.nRecType )
        {
            case DFF_msofbtSpgrContainer:
            case DFF_msofbtSpContainer:
                isBreak = !ProcessShContainer( rDffStrm, aHeader );
            break;
            default:
                isBreak = !aHeader.SeekToEndOfRecord( rDffStrm );
        }
    }
    // seek to end of shape group container
    return rShGrHeader.SeekToEndOfRecord( rDffStrm ) && !isBreak;
}

bool XclImpDffConverter::ProcessSolverContainer( SvStream& rDffStrm, const DffRecordHeader& rSolverHeader )
{
    // solver container wants to read the solver container header again
    rSolverHeader.SeekToBegOfRecord( rDffStrm );
    // read the entire solver container
    ReadSvxMSDffSolverContainer( rDffStrm, GetConvData().maSolverCont );
    // seek to end of solver container
    return rSolverHeader.SeekToEndOfRecord( rDffStrm );
}

bool XclImpDffConverter::ProcessShContainer( SvStream& rDffStrm, const DffRecordHeader& rShHeader )
{
    rShHeader.SeekToBegOfRecord( rDffStrm );
    tools::Rectangle aDummy;
    XclImpDrawObjClientData aDrawObjClientData;
    /*  The call to ImportObj() creates and returns a new SdrObject for the
        processed shape. We take ownership of the returned object here. If the
        shape is a group object, all embedded objects are created recursively,
        and the returned group object contains them all. ImportObj() calls the
        virtual functions ProcessClientAnchor2() and ProcessObj() and writes
        the pointer to the related draw object data (OBJ record) into aDrawObjClientData. */
    rtl::Reference<SdrObject> xSdrObj( ImportObj( rDffStrm, aDrawObjClientData, aDummy, aDummy, /*nCalledByGroup*/0, /*pShapeId*/nullptr ) );
    if (aDrawObjClientData.m_pTopLevelObj && xSdrObj )
        InsertSdrObject( GetConvData().mrSdrPage, *aDrawObjClientData.m_pTopLevelObj, xSdrObj.get() );
    return rShHeader.SeekToEndOfRecord( rDffStrm );
}

void XclImpDffConverter::InsertSdrObject( SdrObjList& rObjList, const XclImpDrawObjBase& rDrawObj, SdrObject* pSdrObj )
{
    XclImpDffConvData& rConvData = GetConvData();
    /*  Take ownership of the passed object. If insertion fails (e.g. rDrawObj
        states to skip insertion), the object is automatically deleted. */
    rtl::Reference<SdrObject> xSdrObj( pSdrObj );
    if( xSdrObj && rDrawObj.IsInsertSdrObj() )
    {
        rObjList.NbcInsertObject( xSdrObj.get() );
        // callback to drawing manager for e.g. tracking of used sheet area
        rConvData.mrDrawing.OnObjectInserted( rDrawObj );
        // callback to drawing object for post processing (use pSdrObj, xSdrObj already released)
        rDrawObj.PostProcessSdrObject( *this, *pSdrObj );
    }
    /*  SdrObject still here? Insertion failed, remove data from shape ID map.
        The SdrObject will be destructed then. */
    if( xSdrObj )
        rConvData.maSolverCont.RemoveSdrObjectInfo( *xSdrObj );
}

void XclImpDffConverter::InitControlForm()
{
    XclImpDffConvData& rConvData = GetConvData();
    if( rConvData.mbHasCtrlForm )
        return;

    rConvData.mbHasCtrlForm = true;
    if( !SupportsOleObjects() )
        return;

    try
    {
        Reference< XFormsSupplier > xFormsSupplier( rConvData.mrSdrPage.getUnoPage(), UNO_QUERY_THROW );
        Reference< XNameContainer > xFormsNC( xFormsSupplier->getForms(), UNO_SET_THROW );
        // find or create the Standard form used to insert the imported controls
        if( xFormsNC->hasByName( gaStdFormName ) )
        {
            xFormsNC->getByName( gaStdFormName ) >>= rConvData.mxCtrlForm;
        }
        else if( SfxObjectShell* pDocShell = GetDocShell() )
        {
            rConvData.mxCtrlForm.set( ScfApiHelper::CreateInstance( pDocShell, u"com.sun.star.form.component.Form"_ustr ), UNO_QUERY_THROW );
            xFormsNC->insertByName( gaStdFormName, Any( rConvData.mxCtrlForm ) );
        }
    }
    catch( const Exception& )
    {
    }
}

// Drawing manager ============================================================

XclImpDrawing::XclImpDrawing( const XclImpRoot& rRoot, bool bOleObjects ) :
    XclImpRoot( rRoot ),
    mbOleObjs( bOleObjects )
{
}

XclImpDrawing::~XclImpDrawing()
{
}

Graphic XclImpDrawing::ReadImgData( const XclImpRoot& rRoot, XclImpStream& rStrm )
{
    Graphic aGraphic;
    sal_uInt16 nFormat = rStrm.ReaduInt16();
    rStrm.Ignore( 2 );//nEnv
    sal_uInt32 nDataSize = rStrm.ReaduInt32();
    if( nDataSize <= rStrm.GetRecLeft() )
    {
        switch( nFormat )
        {
            case EXC_IMGDATA_WMF:   ReadWmf( aGraphic, rStrm );  break;
            case EXC_IMGDATA_BMP:   ReadBmp( aGraphic, rRoot, rStrm );  break;
            default:    OSL_FAIL( "XclImpDrawing::ReadImgData - unknown image format" );
        }
    }
    return aGraphic;
}

void XclImpDrawing::ReadObj( XclImpStream& rStrm )
{
    XclImpDrawObjRef xDrawObj;

    /*  #i61786# In BIFF8 streams, OBJ records may occur without MSODRAWING
        records. In this case, the OBJ records are in BIFF5 format. Do a sanity
        check here that there is no DFF data loaded before. */
    OSL_ENSURE( maDffStrm.Tell() == 0, "XclImpDrawing::ReadObj - unexpected DFF stream data, OBJ will be ignored" );
    if( maDffStrm.Tell() == 0 ) switch( GetBiff() )
    {
        case EXC_BIFF3:
            xDrawObj = XclImpDrawObjBase::ReadObj3( GetRoot(), rStrm );
        break;
        case EXC_BIFF4:
            xDrawObj = XclImpDrawObjBase::ReadObj4( GetRoot(), rStrm );
        break;
        case EXC_BIFF5:
        case EXC_BIFF8:
            xDrawObj = XclImpDrawObjBase::ReadObj5( GetRoot(), rStrm );
        break;
        default:
            DBG_ERROR_BIFF();
    }

    if( xDrawObj )
    {
        // insert into maRawObjs or into the last open group object
        maRawObjs.InsertGrouped( xDrawObj );
        // to be able to find objects by ID
        maObjMapId[ xDrawObj->GetObjId() ] = xDrawObj;
    }
}

void XclImpDrawing::ReadMsoDrawing( XclImpStream& rStrm )
{
    OSL_ENSURE_BIFF( GetBiff() == EXC_BIFF8 );
    // disable internal CONTINUE handling
    rStrm.ResetRecord( false );
    // read leading MSODRAWING record
    ReadDffRecord( rStrm );

    // read following drawing records, but do not start following unrelated record
    bool bLoop = true;
    while( bLoop ) switch( rStrm.GetNextRecId() )
    {
        case EXC_ID_MSODRAWING:
        case EXC_ID_MSODRAWINGSEL:
        case EXC_ID_CONT:
            rStrm.StartNextRecord();
            ReadDffRecord( rStrm );
        break;
        case EXC_ID_OBJ:
            rStrm.StartNextRecord();
            ReadObj8( rStrm );
        break;
        case EXC_ID_TXO:
            rStrm.StartNextRecord();
            ReadTxo( rStrm );
        break;
        default:
            bLoop = false;
    }

    // re-enable internal CONTINUE handling
    rStrm.ResetRecord( true );
}

XclImpDrawObjRef XclImpDrawing::FindDrawObj( const DffRecordHeader& rHeader ) const
{
    /*  maObjMap stores objects by position of the client data (OBJ record) in
        the DFF stream, which is always behind shape start position of the
        passed header. The function upper_bound() finds the first element in
        the map whose key is greater than the start position of the header. Its
        end position is used to test whether the found object is really related
        to the shape. */
    XclImpDrawObjRef xDrawObj;
    XclImpObjMap::const_iterator aIt = maObjMap.upper_bound( rHeader.GetRecBegFilePos() );
    if( (aIt != maObjMap.end()) && (aIt->first <= rHeader.GetRecEndFilePos()) )
        xDrawObj = aIt->second;
    return xDrawObj;
}

XclImpDrawObjRef XclImpDrawing::FindDrawObj( sal_uInt16 nObjId ) const
{
    XclImpDrawObjRef xDrawObj;
    XclImpObjMapById::const_iterator aIt = maObjMapId.find( nObjId );
    if( aIt != maObjMapId.end() )
        xDrawObj = aIt->second;
    return xDrawObj;
}

const XclImpObjTextData* XclImpDrawing::FindTextData( const DffRecordHeader& rHeader ) const
{
    /*  maTextMap stores textbox data by position of the client data (TXO
        record) in the DFF stream, which is always behind shape start position
        of the passed header. The function upper_bound() finds the first
        element in the map whose key is greater than the start position of the
        header. Its end position is used to test whether the found object is
        really related to the shape. */
    XclImpObjTextMap::const_iterator aIt = maTextMap.upper_bound( rHeader.GetRecBegFilePos() );
    if( (aIt != maTextMap.end()) && (aIt->first <= rHeader.GetRecEndFilePos()) )
        return aIt->second.get();
    return nullptr;
}

void XclImpDrawing::ApplyGroupBoxes()
{
    // sorted: smallest to largest - looking for smallest contained-in GroupBox
    // multimap: allows duplicate key values - may have identical areas.
    std::multimap<double, XclImpDrawObjRef> aGroupBoxAreaMap;
    for (auto& rGroupBox : maObjMapId)
    {
        if (rGroupBox.second->GetObjType() != EXC_OBJTYPE_GROUPBOX)
            continue;
        const tools::Rectangle& rRect = rGroupBox.second->GetDffRect();
        const double fArea = double(rRect.GetWidth()) * rRect.GetHeight();
        aGroupBoxAreaMap.insert(std::pair<double, XclImpDrawObjRef>(fArea, rGroupBox.second));
    }

    for (auto& rGroupedObj : maObjMapId)
    {
        auto pRadioButton = dynamic_cast<XclImpOptionButtonObj*>(rGroupedObj.second.get());
        if (!pRadioButton || pRadioButton->IsInGroup())
            continue;

        OUString sGroupName(u"autoGroup_"_ustr);
        for (auto& rGroupBox : aGroupBoxAreaMap)
        {
            assert(pRadioButton->GetTab() == rGroupBox.second->GetTab() && "impossible right?");
            if (!rGroupBox.second->GetDffRect().Contains(pRadioButton->GetDffRect()))
                continue;

            sGroupName = rGroupBox.second->GetObjName();
            if (sGroupName.isEmpty())
                sGroupName += "autoGroup_" + OUString::number(rGroupBox.second->GetObjId());
            // I ASSUME the smallest box wins in MS Word. (otherwise first? last?)
            break;
        }
        pRadioButton->SetStringProperty(u"GroupName"_ustr, sGroupName);
    }
}

void XclImpDrawing::SetSkipObj( sal_uInt16 nObjId )
{
    maSkipObjs.push_back( nObjId );
}

std::size_t XclImpDrawing::GetProgressSize() const
{
    return std::accumulate(maObjMap.begin(), maObjMap.end(), maRawObjs.GetProgressSize(),
        [](const std::size_t& rSum, const XclImpObjMap::value_type& rEntry) { return rSum + rEntry.second->GetProgressSize(); });
}

void XclImpDrawing::ImplConvertObjects( XclImpDffConverter& rDffConv, SdrModel& rSdrModel, SdrPage& rSdrPage )
{
    //rhbz#636521, disable undo during conversion. faster, smaller and stops
    //temp objects being inserted into the undo list
    bool bOrigUndoStatus = rSdrModel.IsUndoEnabled();
    rSdrModel.EnableUndo(false);
    // register this drawing manager at the passed (global) DFF manager
    rDffConv.InitializeDrawing( *this, rSdrModel, rSdrPage );
    // process list of objects to be skipped
    for( const auto& rSkipObj : maSkipObjs )
        if( XclImpDrawObjBase* pDrawObj = FindDrawObj( rSkipObj ).get() )
            pDrawObj->SetProcessSdrObj( false );
    // process drawing objects without DFF data
    rDffConv.ProcessDrawing( maRawObjs );
    // process all objects in the DFF stream
    rDffConv.ProcessDrawing( maDffStrm );
    // assign groups based on being contained in the same GroupBox/sheet
    ApplyGroupBoxes();
    // unregister this drawing manager at the passed (global) DFF manager
    rDffConv.FinalizeDrawing();
    rSdrModel.EnableUndo(bOrigUndoStatus);
}

// protected ------------------------------------------------------------------

void XclImpDrawing::AppendRawObject( const XclImpDrawObjRef& rxDrawObj )
{
    OSL_ENSURE( rxDrawObj, "XclImpDrawing::AppendRawObject - unexpected empty reference" );
    maRawObjs.push_back( rxDrawObj );
}

// private --------------------------------------------------------------------

void XclImpDrawing::ReadWmf( Graphic& rGraphic, XclImpStream& rStrm ) // static helper
{
    // extract graphic data from IMGDATA and following CONTINUE records
    rStrm.Ignore( 8 );
    SvMemoryStream aMemStrm;
    rStrm.CopyToStream( aMemStrm, rStrm.GetRecLeft() );
    aMemStrm.Seek( STREAM_SEEK_TO_BEGIN );
    // import the graphic from memory stream
    GDIMetaFile aGDIMetaFile;
    if( ::ReadWindowMetafile( aMemStrm, aGDIMetaFile ) )
        rGraphic = aGDIMetaFile;
}

void XclImpDrawing::ReadBmp( Graphic& rGraphic, const XclImpRoot& rRoot, XclImpStream& rStrm ) // static helper
{
    // extract graphic data from IMGDATA and following CONTINUE records
    SvMemoryStream aMemStrm;

    /*  Excel 3 and 4 seem to write broken BMP data. Usually they write a
        DIBCOREHEADER (12 bytes) containing width, height, planes = 1, and
        pixel depth = 32 bit. After that, 3 unused bytes are added before the
        actual pixel data. This does even confuse Excel 5 and later, which
        cannot read the image data correctly. */
    if( rRoot.GetBiff() <= EXC_BIFF4 )
    {
        rStrm.PushPosition();
        sal_uInt32 nHdrSize;
        sal_uInt16 nWidth, nHeight, nPlanes, nDepth;
        nHdrSize = rStrm.ReaduInt32();
        nWidth = rStrm.ReaduInt16();
        nHeight = rStrm.ReaduInt16();
        nPlanes = rStrm.ReaduInt16();
        nDepth = rStrm.ReaduInt16();
        if( (nHdrSize == 12) && (nPlanes == 1) && (nDepth == 32) )
        {
            rStrm.Ignore( 3 );
            aMemStrm.SetEndian( SvStreamEndian::LITTLE );
            aMemStrm.WriteUInt32( nHdrSize ).WriteUInt16( nWidth ).WriteUInt16( nHeight ).WriteUInt16( nPlanes ).WriteUInt16( nDepth );
            rStrm.CopyToStream( aMemStrm, rStrm.GetRecLeft() );
        }
        rStrm.PopPosition();
    }

    // no special handling above -> just copy the remaining record data
    if( aMemStrm.Tell() == 0 )
        rStrm.CopyToStream( aMemStrm, rStrm.GetRecLeft() );

    // import the graphic from memory stream
    aMemStrm.Seek( STREAM_SEEK_TO_BEGIN );
    Bitmap aBitmap;
    if( ReadDIB(aBitmap, aMemStrm, false) )   // read DIB without file header
        rGraphic = BitmapEx(aBitmap);
}

void XclImpDrawing::ReadDffRecord( XclImpStream& rStrm )
{
    maDffStrm.Seek( STREAM_SEEK_TO_END );
    rStrm.CopyRecordToStream( maDffStrm );
}

void XclImpDrawing::ReadObj8( XclImpStream& rStrm )
{
    XclImpDrawObjRef xDrawObj = XclImpDrawObjBase::ReadObj8( GetRoot(), rStrm );
    // store the new object in the internal containers
    maObjMap[ maDffStrm.Tell() ] = xDrawObj;
    maObjMapId[ xDrawObj->GetObjId() ] = xDrawObj;
}

void XclImpDrawing::ReadTxo( XclImpStream& rStrm )
{
    XclImpObjTextRef xTextData = std::make_shared<XclImpObjTextData>();
    maTextMap[ maDffStrm.Tell() ] = xTextData;

    // 1) read the TXO record
    xTextData->maData.ReadTxo8( rStrm );

    // 2) first CONTINUE with string
    xTextData->mxString.reset();
    bool bValid = true;
    if( xTextData->maData.mnTextLen > 0 )
    {
        bValid = (rStrm.GetNextRecId() == EXC_ID_CONT) && rStrm.StartNextRecord();
        OSL_ENSURE( bValid, "XclImpDrawing::ReadTxo - missing CONTINUE record" );
        if( bValid )
            xTextData->mxString = std::make_shared<XclImpString>( rStrm.ReadUniString( xTextData->maData.mnTextLen ) );
    }

    // 3) second CONTINUE with formatting runs
    if( xTextData->maData.mnFormatSize > 0 )
    {
        bValid = (rStrm.GetNextRecId() == EXC_ID_CONT) && rStrm.StartNextRecord();
        OSL_ENSURE( bValid, "XclImpDrawing::ReadTxo - missing CONTINUE record" );
        if( bValid )
            xTextData->ReadFormats( rStrm );
    }
}

XclImpSheetDrawing::XclImpSheetDrawing( const XclImpRoot& rRoot, SCTAB nScTab ) :
    XclImpDrawing( rRoot, true ),
    maScUsedArea( ScAddress::INITIALIZE_INVALID )
{
    maScUsedArea.aStart.SetTab( nScTab );
    maScUsedArea.aEnd.SetTab( nScTab );
}

void XclImpSheetDrawing::ReadNote( XclImpStream& rStrm )
{
    switch( GetBiff() )
    {
        case EXC_BIFF2:
        case EXC_BIFF3:
        case EXC_BIFF4:
        case EXC_BIFF5:
            ReadNote3( rStrm );
        break;
        case EXC_BIFF8:
            ReadNote8( rStrm );
        break;
        default:
            DBG_ERROR_BIFF();
    }
}

void XclImpSheetDrawing::ReadTabChart( XclImpStream& rStrm )
{
    OSL_ENSURE_BIFF( GetBiff() >= EXC_BIFF5 );
    auto xChartObj = std::make_shared<XclImpChartObj>( GetRoot(), true );
    xChartObj->ReadChartSubStream( rStrm );
    // insert the chart as raw object without connected DFF data
    AppendRawObject( xChartObj );
}

void XclImpSheetDrawing::ConvertObjects( XclImpDffConverter& rDffConv )
{
    if( SdrModel* pSdrModel = GetDoc().GetDrawLayer() )
        if( SdrPage* pSdrPage = GetSdrPage( maScUsedArea.aStart.Tab() ) )
            ImplConvertObjects( rDffConv, *pSdrModel, *pSdrPage );
}

tools::Rectangle XclImpSheetDrawing::CalcAnchorRect( const XclObjAnchor& rAnchor, bool /*bDffAnchor*/ ) const
{
    return rAnchor.GetRect( GetRoot(), maScUsedArea.aStart.Tab(), MapUnit::Map100thMM );
}

void XclImpSheetDrawing::OnObjectInserted( const XclImpDrawObjBase& rDrawObj )
{
    ScRange aScObjArea = rDrawObj.GetUsedArea( maScUsedArea.aStart.Tab() );
    if( aScObjArea.IsValid() )
        maScUsedArea.ExtendTo( aScObjArea );
}

// private --------------------------------------------------------------------

void XclImpSheetDrawing::ReadNote3( XclImpStream& rStrm )
{
    XclAddress aXclPos;
    rStrm >> aXclPos;
    sal_uInt16 nTotalLen = rStrm.ReaduInt16();

    ScAddress aScNotePos( ScAddress::UNINITIALIZED );
    if( !GetAddressConverter().ConvertAddress( aScNotePos, aXclPos, maScUsedArea.aStart.Tab(), true ) )
        return;

    sal_uInt16 nPartLen = ::std::min( nTotalLen, static_cast< sal_uInt16 >( rStrm.GetRecLeft() ) );
    OUStringBuffer aNoteText(rStrm.ReadRawByteString( nPartLen ));
    nTotalLen = nTotalLen - nPartLen;
    while (true)
    {
        if (!nTotalLen)
            break;
        if (rStrm.GetNextRecId() != EXC_ID_NOTE)
            break;
        if (!rStrm.StartNextRecord())
            break;
        rStrm >> aXclPos;
        nPartLen = rStrm.ReaduInt16();
        OSL_ENSURE( aXclPos.mnRow == 0xFFFF, "XclImpObjectManager::ReadNote3 - missing continuation NOTE record" );
        if( aXclPos.mnRow == 0xFFFF )
        {
            OSL_ENSURE( nPartLen <= nTotalLen, "XclImpObjectManager::ReadNote3 - string too long" );
            aNoteText.append(rStrm.ReadRawByteString( nPartLen ));
            nTotalLen = nTotalLen - ::std::min( nTotalLen, nPartLen );
        }
        else
        {
            // seems to be a new note, record already started -> load the note
            rStrm.Seek( EXC_REC_SEEK_TO_BEGIN );
            ReadNote( rStrm );
            nTotalLen = 0;
        }
    }
    ScNoteUtil::CreateNoteFromString( GetDoc(), aScNotePos, aNoteText.makeStringAndClear(), false, false );
}

void XclImpSheetDrawing::ReadNote8( XclImpStream& rStrm )
{
    XclAddress aXclPos;
    sal_uInt16 nFlags, nObjId;
    rStrm >> aXclPos;
    nFlags = rStrm.ReaduInt16();
    nObjId = rStrm.ReaduInt16();

    ScAddress aScNotePos( ScAddress::UNINITIALIZED );
    if( GetAddressConverter().ConvertAddress( aScNotePos, aXclPos, maScUsedArea.aStart.Tab(), true ) )
        if( nObjId != EXC_OBJ_INVALID_ID )
            if( XclImpNoteObj* pNoteObj = dynamic_cast< XclImpNoteObj* >( FindDrawObj( nObjId ).get() ) )
                pNoteObj->SetNoteData( aScNotePos, nFlags );
}

// The object manager =========================================================

XclImpObjectManager::XclImpObjectManager( const XclImpRoot& rRoot ) :
    XclImpRoot( rRoot )
{
    maDefObjNames[ EXC_OBJTYPE_GROUP ]          = "Group";
    maDefObjNames[ EXC_OBJTYPE_LINE ]           = ScResId( STR_SHAPE_LINE );
    maDefObjNames[ EXC_OBJTYPE_RECTANGLE ]      = ScResId( STR_SHAPE_RECTANGLE );
    maDefObjNames[ EXC_OBJTYPE_OVAL ]           = ScResId( STR_SHAPE_OVAL );
    maDefObjNames[ EXC_OBJTYPE_ARC ]            = "Arc";
    maDefObjNames[ EXC_OBJTYPE_CHART ]          = "Chart";
    maDefObjNames[ EXC_OBJTYPE_TEXT ]           = "Text";
    maDefObjNames[ EXC_OBJTYPE_BUTTON ]         =  ScResId( STR_FORM_BUTTON );
    maDefObjNames[ EXC_OBJTYPE_PICTURE ]        = "Picture";
    maDefObjNames[ EXC_OBJTYPE_POLYGON ]        = "Freeform";
    maDefObjNames[ EXC_OBJTYPE_CHECKBOX ]       = ScResId( STR_FORM_CHECKBOX );
    maDefObjNames[ EXC_OBJTYPE_OPTIONBUTTON ]   = ScResId( STR_FORM_OPTIONBUTTON );
    maDefObjNames[ EXC_OBJTYPE_EDIT ]           = "Edit Box";
    maDefObjNames[ EXC_OBJTYPE_LABEL ]          = ScResId( STR_FORM_LABEL );
    maDefObjNames[ EXC_OBJTYPE_DIALOG ]         = "Dialog Frame";
    maDefObjNames[ EXC_OBJTYPE_SPIN ]           = ScResId( STR_FORM_SPINNER );
    maDefObjNames[ EXC_OBJTYPE_SCROLLBAR ]      = ScResId( STR_FORM_SCROLLBAR );
    maDefObjNames[ EXC_OBJTYPE_LISTBOX ]        = ScResId( STR_FORM_LISTBOX );
    maDefObjNames[ EXC_OBJTYPE_GROUPBOX ]       = ScResId( STR_FORM_GROUPBOX );
    maDefObjNames[ EXC_OBJTYPE_DROPDOWN ]       = ScResId( STR_FORM_DROPDOWN );
    maDefObjNames[ EXC_OBJTYPE_NOTE ]           = "Comment";
    maDefObjNames[ EXC_OBJTYPE_DRAWING ]        = ScResId( STR_SHAPE_AUTOSHAPE );
}

XclImpObjectManager::~XclImpObjectManager()
{
}

void XclImpObjectManager::ReadMsoDrawingGroup( XclImpStream& rStrm )
{
    OSL_ENSURE_BIFF( GetBiff() == EXC_BIFF8 );
    // Excel continues this record with MSODRAWINGGROUP and CONTINUE records, hmm.
    rStrm.ResetRecord( true, EXC_ID_MSODRAWINGGROUP );
    maDggStrm.Seek( STREAM_SEEK_TO_END );
    rStrm.CopyRecordToStream( maDggStrm );
}

XclImpSheetDrawing& XclImpObjectManager::GetSheetDrawing( SCTAB nScTab )
{
    XclImpSheetDrawingRef& rxDrawing = maSheetDrawings[ nScTab ];
    if( !rxDrawing )
        rxDrawing = std::make_shared<XclImpSheetDrawing>( GetRoot(), nScTab );
    return *rxDrawing;
}

void XclImpObjectManager::ConvertObjects()
{
    // do nothing if the document does not contain a drawing layer
    if( !GetDoc().GetDrawLayer() )
        return;

    // get total progress bar size for all sheet drawing managers
    std::size_t nProgressSize = std::accumulate(maSheetDrawings.begin(), maSheetDrawings.end(), std::size_t(0),
        [](const std::size_t& rSum, const XclImpSheetDrawingMap::value_type& rEntry) { return rSum + rEntry.second->GetProgressSize(); });
    // nothing to do if progress bar is zero (no objects present)
    if( nProgressSize == 0 )
        return;

    XclImpDffConverter aDffConv( GetRoot(), maDggStrm );
    aDffConv.StartProgressBar( nProgressSize );
    for( auto& rEntry : maSheetDrawings )
        rEntry.second->ConvertObjects( aDffConv );

    // #i112436# don't call ScChartListenerCollection::SetDirty here,
    // instead use InterpretDirtyCells in ScDocument::CalcAfterLoad.
}

OUString XclImpObjectManager::GetDefaultObjName( const XclImpDrawObjBase& rDrawObj ) const
{
    OUString aDefName;
    DefObjNameMap::const_iterator aIt = maDefObjNames.find( rDrawObj.GetObjType() );
    if( aIt != maDefObjNames.end() )
        aDefName = aIt->second;
    return aDefName + " " + OUString::number(static_cast<sal_Int32>(rDrawObj.GetObjId()));
}

ScRange XclImpObjectManager::GetUsedArea( SCTAB nScTab ) const
{
    XclImpSheetDrawingMap::const_iterator aIt = maSheetDrawings.find( nScTab );
    if( aIt != maSheetDrawings.end() )
        return aIt->second->GetUsedArea();
    return ScRange( ScAddress::INITIALIZE_INVALID );
}

// DFF property set helper ====================================================

XclImpDffPropSet::XclImpDffPropSet( const XclImpRoot& rRoot ) :
    XclImpRoot( rRoot ),
    maDffConv( rRoot, maDummyStrm )
{
}

void XclImpDffPropSet::Read( XclImpStream& rStrm )
{
    sal_uInt32 nPropSetSize;

    rStrm.PushPosition();
    rStrm.Ignore( 4 );
    nPropSetSize = rStrm.ReaduInt32();
    rStrm.PopPosition();

    mxMemStrm.reset( new SvMemoryStream );
    rStrm.CopyToStream( *mxMemStrm, 8 + nPropSetSize );
    mxMemStrm->Seek( STREAM_SEEK_TO_BEGIN );
    maDffConv.ReadPropSet( *mxMemStrm, nullptr );
}

sal_uInt32 XclImpDffPropSet::GetPropertyValue( sal_uInt16 nPropId ) const
{
    return maDffConv.GetPropertyValue( nPropId, 0 );
}

void XclImpDffPropSet::FillToItemSet( SfxItemSet& rItemSet ) const
{
    if( mxMemStrm )
        maDffConv.ApplyAttributes( *mxMemStrm, rItemSet );
}

XclImpStream& operator>>( XclImpStream& rStrm, XclImpDffPropSet& rPropSet )
{
    rPropSet.Read( rStrm );
    return rStrm;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
