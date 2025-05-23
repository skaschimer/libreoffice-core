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

#include <cmdid.h>
#include <hintids.hxx>
#include <tools/urlobj.hxx>
#include <tools/UnitConversion.hxx>
#include <svl/stritem.hxx>
#include <svl/whiter.hxx>
#include <svl/urihelper.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/objface.hxx>
#include <sfx2/viewfrm.hxx>
#include <editeng/sizeitem.hxx>
#include <sfx2/request.hxx>
#include <vcl/EnumContext.hxx>
#include <sfx2/htmlmode.hxx>
#include <svx/svdview.hxx>
#include <editeng/brushitem.hxx>
#include <svx/grfflt.hxx>
#include <svx/compressgraphicdialog.hxx>
#include <svx/tbxcolor.hxx>
#include <svx/sdangitm.hxx>
#include <osl/diagnose.h>
#include <drawdoc.hxx>
#include <view.hxx>
#include <wrtsh.hxx>
#include <viewopt.hxx>
#include <swmodule.hxx>
#include <swundo.hxx>
#include <uitool.hxx>
#include <docsh.hxx>
#include <grfsh.hxx>
#include <frmmgr.hxx>
#include <frmfmt.hxx>
#include <grfatr.hxx>
#include <swwait.hxx>
#include <svx/extedit.hxx>
#include <svx/graphichelper.hxx>
#include <doc.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <svx/drawitem.hxx>
#define ShellClass_SwGrfShell

#include <sfx2/msg.hxx>
#include <swslots.hxx>
#include <swabstdlg.hxx>
#include <unocrsr.hxx>
#include <flyfrm.hxx>
#include <memory>

constexpr OUString TOOLBOX_NAME = u"colorbar"_ustr;

class SwGrfShell::SwExternalToolEdit
    : public ExternalToolEdit
{
private:
    SwWrtShell *const m_pShell;
    std::shared_ptr<SwUnoCursor> const m_pCursor;

public:
    explicit SwExternalToolEdit(SwWrtShell *const pShell)
        : m_pShell(pShell)
        , m_pCursor( // need only Point, must point to SwGrfNode
            pShell->GetDoc()->CreateUnoCursor(
                *pShell->GetCurrentShellCursor().GetPoint()))
    {
    }

    virtual void Update(Graphic & rGraphic) override
    {
        DBG_TESTSOLARMUTEX();
        m_pShell->Push();
        m_pShell->GetCurrentShellCursor().DeleteMark();
        *m_pShell->GetCurrentShellCursor().GetPoint() = *m_pCursor->GetPoint();
        m_pShell->ReRead(OUString(), OUString(), &rGraphic);
        m_pShell->Pop();
    }
};

SFX_IMPL_INTERFACE(SwGrfShell, SwBaseShell)

void SwGrfShell::InitInterface_Impl()
{
    GetStaticInterface()->RegisterPopupMenu(u"graphic"_ustr);

    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_OBJECT, SfxVisibilityFlags::Invisible, ToolbarId::Grafik_Toolbox);
}

void SwGrfShell::Execute(SfxRequest &rReq)
{
    SwWrtShell &rSh = GetShell();

    sal_uInt16 nSlot = rReq.GetSlot();
    switch(nSlot)
    {
        case SID_OBJECT_ROTATE:
        {
            // RotGrfFlyFrame: start rotation when possible
            SdrView* pSdrView = rSh.GetDrawViewWithValidMarkList();

            if(rSh.IsRotationOfSwGrfNodePossible() && pSdrView->IsRotateAllowed())
            {
                if(GetView().IsDrawRotate())
                {
                    rSh.SetDragMode(SdrDragMode::Move);
                }
                else
                {
                    rSh.SetDragMode(SdrDragMode::Rotate);
                }

                GetView().FlipDrawRotate();
            }
        }
        break;

        case SID_TWAIN_TRANSFER:
        {
            GetView().ExecuteScan( rReq );
            break;
        }

        case SID_SAVE_GRAPHIC:
        {
            GraphicAttr aGraphicAttr;
            rSh.GetGraphicAttr(aGraphicAttr);

            short nState = RET_CANCEL;
            if (aGraphicAttr != GraphicAttr()) // the image has been modified
            {
                weld::Window* pWin = GetView().GetFrameWeld();
                if (pWin)
                {
                    nState = GraphicHelper::HasToSaveTransformedImage(pWin);
                }
            }
            else
            {
                nState = RET_NO;
            }

            if (nState == RET_YES)
            {
                const GraphicObject* pGraphicObj = rSh.GetGraphicObj();
                if (pGraphicObj)
                {
                    Graphic aGraphic = pGraphicObj->GetTransformedGraphic(pGraphicObj->GetPrefSize(), pGraphicObj->GetPrefMapMode(), aGraphicAttr);
                    OUString sGrfNm;
                    OUString sFilterNm;
                    rSh.GetGrfNms( &sGrfNm, &sFilterNm );
                    GraphicHelper::ExportGraphic(GetView().GetFrameWeld(), aGraphic, sGrfNm);
                }
            }
            else if (nState == RET_NO)
            {
                const Graphic *pGraphic = rSh.GetGraphic();
                if(nullptr != pGraphic)
                {
                    OUString sGrfNm;
                    OUString sFilterNm;
                    rSh.GetGrfNms( &sGrfNm, &sFilterNm );
                    GraphicHelper::ExportGraphic(GetView().GetFrameWeld(), *pGraphic, sGrfNm);
                }
            }
        }
        break;

        case SID_COMPRESS_GRAPHIC:
        {
            const Graphic* pGraphic = rSh.GetGraphic();
            if( pGraphic )
            {
                Size aSize (
                    convertTwipToMm100(rSh.GetAnyCurRect(CurRectType::FlyEmbedded).SSize()));

                SfxItemSetFixed<RES_GRFATR_MIRRORGRF, RES_GRFATR_CROPGRF> aSet( rSh.GetAttrPool() );
                rSh.GetCurAttr( aSet );
                SwMirrorGrf aMirror( aSet.Get(RES_GRFATR_MIRRORGRF) );
                SwCropGrf aCrop( aSet.Get(RES_GRFATR_CROPGRF) );

                tools::Rectangle aCropRectangle(
                    convertTwipToMm100(aCrop.GetLeft()),
                    convertTwipToMm100(aCrop.GetTop()),
                    convertTwipToMm100(aCrop.GetRight()),
                    convertTwipToMm100(aCrop.GetBottom()) );

                Graphic aGraphic = *pGraphic;

                CompressGraphicsDialog aDialog(GetView().GetFrameWeld(), std::move(aGraphic), aSize, aCropRectangle, GetView().GetViewFrame().GetBindings());
                if (aDialog.run() == RET_OK)
                {
                    rSh.StartAllAction();
                    rSh.StartUndo(SwUndoId::START);
                    tools::Rectangle aScaledCropedRectangle = aDialog.GetScaledCropRectangle();

                    aCrop.SetLeft(   o3tl::toTwips( aScaledCropedRectangle.Left(), o3tl::Length::mm100 ));
                    aCrop.SetTop(    o3tl::toTwips( aScaledCropedRectangle.Top(), o3tl::Length::mm100 ));
                    aCrop.SetRight(  o3tl::toTwips( aScaledCropedRectangle.Right(), o3tl::Length::mm100 ));
                    aCrop.SetBottom( o3tl::toTwips( aScaledCropedRectangle.Bottom(), o3tl::Length::mm100 ));

                    Graphic aCompressedGraphic( aDialog.GetCompressedGraphic() );
                    rSh.ReRead(OUString(), OUString(), const_cast<const Graphic*>(&aCompressedGraphic));

                    rSh.SetAttrItem(aCrop);
                    rSh.SetAttrItem(aMirror);

                    rSh.EndUndo(SwUndoId::END);
                    rSh.EndAllAction();
                }
            }
        }
        break;
        case SID_EXTERNAL_EDIT:
        {
            // When the graphic is selected to be opened via some external tool
            // for advanced editing
            GraphicObject const*const pGraphicObject(rSh.GetGraphicObj());
            if(nullptr != pGraphicObject)
            {
                m_ExternalEdits.push_back(std::make_unique<SwExternalToolEdit>(
                            &rSh));
                m_ExternalEdits.back()->Edit(pGraphicObject);
            }
        }
        break;
        case SID_CHANGE_PICTURE:
        case SID_INSERT_GRAPHIC:
        {
            // #i123922# implement slot independent from the two below to
            // bring up the insert graphic dialog and associated actions
            SwView& rLclView = GetView();
            rReq.SetReturnValue(SfxBoolItem(nSlot, rLclView.InsertGraphicDlg( rReq )));
            break;
        }
        case FN_FORMAT_GRAFIC_DLG:
        case FN_DRAW_WRAP_DLG:
        {
            SwFlyFrameAttrMgr aMgr( false, &rSh, rSh.IsFrameSelected() ?
                                               Frmmgr_Type::NONE : Frmmgr_Type::GRF, nullptr);
            const SwViewOption* pVOpt = rSh.GetViewOptions();
            SwViewOption aUsrPref( *pVOpt );

            SfxItemSetFixed<
                    RES_FRMATR_BEGIN, RES_GRFATR_CROPGRF,
                    // FillAttribute support:
                    XATTR_FILL_FIRST, XATTR_FILL_LAST,
                    SID_DOCFRAME, SID_DOCFRAME,
                    SID_REFERER, SID_REFERER,
                    SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER,
                    SID_ATTR_PAGE_SIZE, SID_ATTR_PAGE_SIZE, // 10051
                    // RotGrfFlyFrame: Need RotationAngle now
                    SID_ATTR_TRANSFORM_ANGLE, SID_ATTR_TRANSFORM_ANGLE, // 10095
                    // Items to hand over XPropertyList things like
                    // XColorList, XHatchList, XGradientList, and XBitmapList to
                    // the Area TabPage:
                    SID_COLOR_TABLE, SID_PATTERN_LIST,  //10179
                    SID_HTML_MODE, SID_HTML_MODE,   //10414
                    SID_ATTR_GRAF_KEEP_ZOOM, SID_ATTR_GRAF_KEEP_ZOOM,   //10882
                    SID_ATTR_GRAF_FRMSIZE, SID_ATTR_GRAF_GRAPHIC,   // 10884
                    // contains SID_ATTR_GRAF_FRMSIZE_PERCENT
                    FN_GET_PRINT_AREA, FN_GET_PRINT_AREA,
                    FN_PARAM_GRF_CONNECT, FN_PARAM_GRF_CONNECT,
                    FN_SET_FRM_NAME, FN_KEEP_ASPECT_RATIO,
                    FN_SET_FRM_ALT_NAME, FN_SET_FRM_ALT_NAME,
                    FN_UNO_DESCRIPTION, FN_UNO_DESCRIPTION>  aSet( GetPool() );

            // create needed items for XPropertyList entries from the DrawModel so that
            // the Area TabPage can access them
            const SwDrawModel* pDrawModel = rSh.GetView().GetDocShell()->GetDoc()->getIDocumentDrawModelAccess().GetDrawModel();

            aSet.Put(SvxColorListItem(pDrawModel->GetColorList(), SID_COLOR_TABLE));
            aSet.Put(SvxGradientListItem(pDrawModel->GetGradientList(), SID_GRADIENT_LIST));
            aSet.Put(SvxHatchListItem(pDrawModel->GetHatchList(), SID_HATCH_LIST));
            aSet.Put(SvxBitmapListItem(pDrawModel->GetBitmapList(), SID_BITMAP_LIST));
            aSet.Put(SvxPatternListItem(pDrawModel->GetPatternList(), SID_PATTERN_LIST));

            sal_uInt16 nHtmlMode = ::GetHtmlMode(GetView().GetDocShell());
            aSet.Put(SfxUInt16Item(SID_HTML_MODE, nHtmlMode));
            FieldUnit eMetric = ::GetDfltMetric(0 != (nHtmlMode&HTMLMODE_ON));
            SwModule* mod = SwModule::get();
            mod->PutItem(SfxUInt16Item(SID_ATTR_METRIC, static_cast<sal_uInt16>(eMetric)));

            const SwRect* pRect = &rSh.GetAnyCurRect(CurRectType::Page);
            SwFormatFrameSize aFrameSize( SwFrameSize::Variable, pRect->Width(), pRect->Height());
            aFrameSize.SetWhich( GetPool().GetWhichIDFromSlotID( SID_ATTR_PAGE_SIZE ) );
            aSet.Put( aFrameSize );

            aSet.Put(SfxStringItem(FN_SET_FRM_NAME, rSh.GetFlyName().toString()));
            aSet.Put(SfxStringItem(FN_UNO_DESCRIPTION, rSh.GetObjDescription()));
            if ( nSlot == FN_FORMAT_GRAFIC_DLG )
            {
                // #i73249#
                aSet.Put( SfxStringItem( FN_SET_FRM_ALT_NAME, rSh.GetObjTitle() ) );
            }

            pRect = &rSh.GetAnyCurRect(CurRectType::PagePrt);
            aFrameSize.SetWidth( pRect->Width() );
            aFrameSize.SetHeight( pRect->Height() );
            aFrameSize.SetWhich( GetPool().GetWhichIDFromSlotID(FN_GET_PRINT_AREA) );
            aSet.Put( aFrameSize );

            aSet.Put( aMgr.GetAttrSet() );
            SwFlyFrame* pFly = rSh.GetSelectedFlyFrame();
            if (pFly)
            {
                // Work with the up to date layout size if possible.
                SwFormatFrameSize aSize = aSet.Get(RES_FRM_SIZE);
                aSize.SetWidth(pFly->getFrameArea().Width());
                aSize.SetHeight(pFly->getFrameArea().Height());
                aSet.Put(aSize);
            }
            aSet.SetParent( aMgr.GetAttrSet().GetParent() );

            // At percentage values initialize size
            SwFormatFrameSize aSizeCopy = aSet.Get(RES_FRM_SIZE);
            if (aSizeCopy.GetWidthPercent() && aSizeCopy.GetWidthPercent() != SwFormatFrameSize::SYNCED)
                aSizeCopy.SetWidth(rSh.GetAnyCurRect(CurRectType::FlyEmbedded).Width());
            if (aSizeCopy.GetHeightPercent() && aSizeCopy.GetHeightPercent() != SwFormatFrameSize::SYNCED)
                aSizeCopy.SetHeight(rSh.GetAnyCurRect(CurRectType::FlyEmbedded).Height());
            // and now set the size for "external" tabpages
            {
                SvxSizeItem aSzItm( SID_ATTR_GRAF_FRMSIZE, aSizeCopy.GetSize() );
                aSet.Put( aSzItm );

                Size aSz( aSizeCopy.GetWidthPercent(), aSizeCopy.GetHeightPercent() );
                if( SwFormatFrameSize::SYNCED == aSz.Width() )   aSz.setWidth( 0 );
                if( SwFormatFrameSize::SYNCED == aSz.Height() )  aSz.setHeight( 0 );

                aSzItm.SetSize( aSz );
                aSzItm.SetWhich( SID_ATTR_GRAF_FRMSIZE_PERCENT );
                aSet.Put( aSzItm );
            }

            OUString sGrfNm;
            OUString sFilterNm;
            rSh.GetGrfNms( &sGrfNm, &sFilterNm );
            if( !sGrfNm.isEmpty() )
            {
                aSet.Put( SvxBrushItem( INetURLObject::decode( sGrfNm,
                                           INetURLObject::DecodeMechanism::Unambiguous ),
                                        sFilterNm, GPOS_LT,
                                        SID_ATTR_GRAF_GRAPHIC ));
            }
            else
            {
                // #119353# - robust
                const GraphicObject* pGrfObj = rSh.GetGraphicObj();
                if ( pGrfObj )
                {
                    aSet.Put( SvxBrushItem( *pGrfObj, GPOS_LT,
                                            SID_ATTR_GRAF_GRAPHIC ) );
                }
            }
            aSet.Put( SfxBoolItem( FN_PARAM_GRF_CONNECT, !sGrfNm.isEmpty() ) );

            // get Mirror and Crop
            {
                SfxItemSetFixed<RES_GRFATR_MIRRORGRF, RES_GRFATR_CROPGRF>  aTmpSet( rSh.GetAttrPool() );

                rSh.GetCurAttr( aTmpSet );
                aSet.Put( aTmpSet );
            }

            aSet.Put(SfxBoolItem(FN_KEEP_ASPECT_RATIO, aUsrPref.IsKeepRatio()));
            aSet.Put(SfxBoolItem( SID_ATTR_GRAF_KEEP_ZOOM, aUsrPref.IsGrfKeepZoom()));

            aSet.Put(SfxFrameItem( SID_DOCFRAME, &GetView().GetViewFrame().GetFrame()));

            SfxObjectShell * sh = rSh.GetDoc()->GetPersist();
            if (sh != nullptr && sh->HasName())
            {
                aSet.Put(
                    SfxStringItem(SID_REFERER, sh->GetMedium()->GetName()));
            }

            Size aUnrotatedSize;
            Degree10 nCurrentRotation;
            {   // RotGrfFlyFrame: Add current RotationAngle value, convert from
                // RES_GRFATR_ROTATION to SID_ATTR_TRANSFORM_ANGLE. Do not forget to
                // convert from 10th degrees to 100th degrees
                SfxItemSetFixed<RES_GRFATR_ROTATION, RES_GRFATR_ROTATION> aTmpSet( rSh.GetAttrPool() );
                rSh.GetCurAttr( aTmpSet );
                const SwRotationGrf& rRotation = aTmpSet.Get(RES_GRFATR_ROTATION);
                nCurrentRotation = rRotation.GetValue();
                aUnrotatedSize = rRotation.GetUnrotatedSize();
                aSet.Put(SdrAngleItem(SID_ATTR_TRANSFORM_ANGLE, to<Degree100>(nCurrentRotation)));
            }

            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            ScopedVclPtr<SfxAbstractTabDialog> pDlg(pFact->CreateFrameTabDialog(u"PictureDialog"_ustr,
                                                    GetView().GetViewFrame(),
                                                    GetView().GetFrameWeld(),
                                                    aSet, false));
            if (nSlot == FN_DRAW_WRAP_DLG)
                pDlg->SetCurPageId(u"wrap"_ustr);

            if (pDlg->Execute() == RET_OK)
            {
                rSh.StartAllAction();
                rSh.StartUndo(SwUndoId::START);
                SfxItemSet* pSet = const_cast<SfxItemSet*>(pDlg->GetOutputItemSet());
                rReq.Done(*pSet);
                // change the 2 frmsize SizeItems to the correct SwFrameSizeItem
                if( const SvxSizeItem* pSizeItem = pSet->GetItemIfSet(
                                SID_ATTR_GRAF_FRMSIZE, false ))
                {
                    SwFormatFrameSize aSize;
                    const Size& rSz = pSizeItem->GetSize();
                    aSize.SetWidth( rSz.Width() );
                    aSize.SetHeight( rSz.Height() );

                    pSizeItem = pSet->GetItemIfSet( SID_ATTR_GRAF_FRMSIZE_PERCENT, false );
                    if( pSizeItem )
                    {
                        const Size& rRelativeSize = pSizeItem->GetSize();
                        aSize.SetWidthPercent( static_cast< sal_uInt8 >( rRelativeSize.Width() ) );
                        aSize.SetHeightPercent( static_cast< sal_uInt8 >( rRelativeSize.Height() ) );
                    }
                    pSet->Put( aSize );
                }

                // Templates AutoUpdate
                SwFrameFormat* pFormat = rSh.GetSelectedFrameFormat();
                if(pFormat && pFormat->IsAutoUpdateOnDirectFormat())
                {
                    pFormat->SetFormatAttr(*pSet);
                    SfxItemSetFixed<
                            RES_FRM_SIZE, RES_FRM_SIZE,
                            RES_SURROUND, RES_ANCHOR>  aShellSet( GetPool() );
                    aShellSet.Put(*pSet);
                    aMgr.SetAttrSet(aShellSet);
                }
                else
                {
                    aMgr.SetAttrSet(*pSet);
                }
                aMgr.UpdateFlyFrame();

                bool bApplyUsrPref = false;
                if (const SfxBoolItem* pRatioItem = pSet->GetItemIfSet(
                    FN_KEEP_ASPECT_RATIO ))
                {
                    aUsrPref.SetKeepRatio( pRatioItem->GetValue() );
                    bApplyUsrPref = true;
                }
                if( const SfxBoolItem* pZoomItem = pSet->GetItemIfSet(
                    SID_ATTR_GRAF_KEEP_ZOOM ))
                {
                    aUsrPref.SetGrfKeepZoom( pZoomItem->GetValue() );
                    bApplyUsrPref = true;
                }

                if( bApplyUsrPref )
                    mod->ApplyUsrPref(aUsrPref, &GetView());

                // and now set all the graphic attributes and other stuff
                if( const SvxBrushItem* pGraphicBrushItem = pSet->GetItemIfSet(
                                        SID_ATTR_GRAF_GRAPHIC ))
                {
                    if( !pGraphicBrushItem->GetGraphicLink().isEmpty() )
                        sGrfNm = pGraphicBrushItem->GetGraphicLink();
                    else
                        sGrfNm.clear();

                    if( !pGraphicBrushItem->GetGraphicFilter().isEmpty() )
                        sFilterNm = pGraphicBrushItem->GetGraphicFilter();
                    else
                        sFilterNm.clear();

                    if( !sGrfNm.isEmpty() )
                    {
                        SwDocShell* pDocSh = GetView().GetDocShell();
                        SwWait aWait( *pDocSh, true );
                        SfxMedium* pMedium = pDocSh->GetMedium();
                        INetURLObject aAbs;
                        if( pMedium )
                            aAbs = pMedium->GetURLObject();
                        rSh.ReRead( URIHelper::SmartRel2Abs(
                                        aAbs, sGrfNm,
                                        URIHelper::GetMaybeFileHdl() ),
                                     sFilterNm );
                    }
                }
                if ( const SfxStringItem* pNameItem = pSet->GetItemIfSet(
                                        FN_SET_FRM_ALT_NAME ))
                {
                    // #i73249#
                    rSh.SetObjTitle( pNameItem->GetValue() );
                }

                if ( const SfxStringItem* pDescriptionItem = pSet->GetItemIfSet(
                                        FN_UNO_DESCRIPTION ))
                    rSh.SetObjDescription( pDescriptionItem->GetValue() );

                // RotGrfFlyFrame: Get and process evtl. changed RotationAngle
                if ( const SdrAngleItem* pAngleItem = pSet->GetItemIfSet(SID_ATTR_TRANSFORM_ANGLE, false ))
                {
                    const Degree10 aNewRotation = to<Degree10>(pAngleItem->GetValue() % 36000_deg100);

                    // RotGrfFlyFrame: Possible rotation change here, SwFlyFrameAttrMgr aMgr is available
                    aMgr.SetRotation(nCurrentRotation, aNewRotation, aUnrotatedSize);
                }

                SfxItemSetFixed<RES_GRFATR_BEGIN, RES_GRFATR_END-1> aGrfSet( rSh.GetAttrPool() );
                aGrfSet.Put( *pSet );
                if( aGrfSet.Count() )
                    rSh.SetAttrSet( aGrfSet );

                rSh.EndUndo(SwUndoId::END);
                rSh.EndAllAction();
            }
        }
        break;

        case FN_GRAPHIC_MIRROR_ON_EVEN_PAGES:
        {
            SfxItemSetFixed<RES_GRFATR_MIRRORGRF, RES_GRFATR_MIRRORGRF> aSet(rSh.GetAttrPool());
            rSh.GetCurAttr( aSet );
            SwMirrorGrf aGrf(aSet.Get(RES_GRFATR_MIRRORGRF));
            aGrf.SetGrfToggle(!aGrf.IsGrfToggle());
            rSh.SetAttrItem(aGrf);
        }
        break;

        case SID_OBJECT_CROP:
        {
            GraphicObject const *pGraphicObject = rSh.GetGraphicObj();
            if (nullptr != pGraphicObject  && SdrDragMode::Crop != rSh.GetDragMode()) {
                rSh.StartCropImage();
            }
        }
        break;

        default:
            OSL_ENSURE(false, "wrong dispatcher");
            return;
    }
}

void SwGrfShell::ExecAttr( SfxRequest const &rReq )
{
    GraphicType nGrfType = GraphicType::NONE;
    if (CNT_GRF == GetShell().GetCntType())
        nGrfType = GetShell().GetGraphicType();
    if (GraphicType::Bitmap == nGrfType ||
        GraphicType::GdiMetafile == nGrfType)
    {
        SfxItemSetFixed<RES_GRFATR_BEGIN, RES_GRFATR_END -1> aGrfSet( GetShell().GetAttrPool() );
        const SfxItemSet *pArgs = rReq.GetArgs();
        const SfxPoolItem* pItem;
        sal_uInt16 nSlot = rReq.GetSlot();
        if( !pArgs || SfxItemState::SET != pArgs->GetItemState( nSlot, false, &pItem ))
            pItem = nullptr;

        switch( nSlot )
        {
            case SID_FLIP_VERTICAL:
            case SID_FLIP_HORIZONTAL:
            {
                GetShell().GetCurAttr( aGrfSet );
                SwMirrorGrf aMirror( aGrfSet.Get( RES_GRFATR_MIRRORGRF ) );
                MirrorGraph nMirror = aMirror.GetValue();
                if ( nSlot==SID_FLIP_HORIZONTAL )
                    switch( nMirror )
                    {
                    case MirrorGraph::Dont: nMirror = MirrorGraph::Vertical;
                                                break;
                    case MirrorGraph::Horizontal:  nMirror = MirrorGraph::Both;
                                                break;
                    case MirrorGraph::Vertical:   nMirror = MirrorGraph::Dont;
                                                break;
                    case MirrorGraph::Both: nMirror = MirrorGraph::Horizontal;
                                                break;
                    }
                else
                    switch( nMirror )
                    {
                    case MirrorGraph::Dont: nMirror = MirrorGraph::Horizontal;
                                                break;
                    case MirrorGraph::Vertical: nMirror = MirrorGraph::Both;
                                                break;
                    case MirrorGraph::Horizontal:    nMirror = MirrorGraph::Dont;
                                                break;
                    case MirrorGraph::Both: nMirror = MirrorGraph::Vertical;
                                                break;
                    }
                aMirror.SetValue( nMirror );
                aGrfSet.ClearItem();
                aGrfSet.Put( aMirror );
            }
            break;

        case SID_ATTR_GRAF_LUMINANCE:
            if( pItem )
                aGrfSet.Put( SwLuminanceGrf(
                            static_cast<const SfxInt16Item*>(pItem)->GetValue() ));
            break;

        case SID_ATTR_GRAF_CONTRAST:
            if( pItem )
                aGrfSet.Put( SwContrastGrf(
                            static_cast<const SfxInt16Item*>(pItem)->GetValue() ));
            break;

        case SID_ATTR_GRAF_RED:
            if( pItem )
                aGrfSet.Put( SwChannelRGrf(
                            static_cast<const SfxInt16Item*>(pItem)->GetValue() ));
            break;

        case SID_ATTR_GRAF_GREEN:
            if( pItem )
                aGrfSet.Put( SwChannelGGrf(
                            static_cast<const SfxInt16Item*>(pItem)->GetValue() ));
            break;

        case SID_ATTR_GRAF_BLUE:
            if( pItem )
                aGrfSet.Put( SwChannelBGrf(
                            static_cast<const SfxInt16Item*>(pItem)->GetValue() ));
            break;

        case SID_ATTR_GRAF_GAMMA:
            if( pItem )
            {
                double fVal = static_cast<const SfxUInt32Item*>(pItem)->GetValue();
                aGrfSet.Put( SwGammaGrf(fVal/100. ));
            }
            break;

        case SID_ATTR_GRAF_TRANSPARENCE:
            if( pItem )
                aGrfSet.Put( SwTransparencyGrf(
                    static_cast< sal_Int8 >( static_cast<const SfxUInt16Item*>(pItem )->GetValue() ) ) );
            break;

        case SID_ATTR_GRAF_INVERT:
            if( pItem )
                aGrfSet.Put( SwInvertGrf(
                            static_cast<const SfxBoolItem*>(pItem)->GetValue() ));
            break;

        case SID_ATTR_GRAF_MODE:
            if( pItem )
                aGrfSet.Put( SwDrawModeGrf(
                            static_cast<GraphicDrawMode>(static_cast<const SfxUInt16Item*>(pItem)->GetValue()) ));
            break;

        case SID_COLOR_SETTINGS:
        {
            svx::ToolboxAccess aToolboxAccess( TOOLBOX_NAME );
            aToolboxAccess.toggleToolbox();
            break;
        }

        case SID_GRFFILTER:
        case SID_GRFFILTER_INVERT:
        case SID_GRFFILTER_SMOOTH:
        case SID_GRFFILTER_SHARPEN:
        case SID_GRFFILTER_REMOVENOISE:
        case SID_GRFFILTER_SOBEL:
        case SID_GRFFILTER_MOSAIC:
        case SID_GRFFILTER_EMBOSS:
        case SID_GRFFILTER_POSTER:
        case SID_GRFFILTER_POPART:
        case SID_GRFFILTER_SEPIA:
        case SID_GRFFILTER_SOLARIZE:
            if( GraphicType::Bitmap == nGrfType )
            {
                // #119353# - robust
                const GraphicObject* pFilterObj( GetShell().GetGraphicObj() );
                if ( pFilterObj )
                {
                    SvxGraphicFilter::ExecuteGrfFilterSlot( rReq, *pFilterObj,
                        [this] (GraphicObject aFilteredObject) -> void
                        {
                            GetShell().ReRead( OUString(), OUString(),
                                               &aFilteredObject.GetGraphic() );
                        });
                }
            }
            break;

        default:
            OSL_ENSURE(false, "wrong dispatcher");
        }

        if( aGrfSet.Count() )
            GetShell().SetAttrSet( aGrfSet );
    }
    GetView().GetViewFrame().GetBindings().Invalidate(rReq.GetSlot());
}

void SwGrfShell::GetAttrState(SfxItemSet &rSet)
{
    SwWrtShell &rSh = GetShell();
    SfxItemSet aCoreSet( GetPool(), aNoTextNodeSetRange );
    rSh.GetCurAttr( aCoreSet );
    bool bParentCntProt = FlyProtectFlags::NONE != rSh.IsSelObjProtected( FlyProtectFlags::Content|FlyProtectFlags::Parent );
    bool bIsGrfContent = CNT_GRF == GetShell().GetCntType();

    SetGetStateSet( &rSet );

    SfxWhichIter aIter( rSet );
    sal_uInt16 nWhich = aIter.FirstWhich();
    while( nWhich )
    {
        bool bDisable = bParentCntProt;
        switch( nWhich )
        {
        case SID_OBJECT_ROTATE:
        {
            // RotGrfFlyFrame: steer rotation state
            const bool bIsRotate(GetView().IsDrawRotate());
            SdrView* pSdrView = rSh.GetDrawViewWithValidMarkList();

            if(!bIsRotate && !pSdrView->IsRotateAllowed())
            {
                rSet.DisableItem(nWhich);
            }
            else
            {
                rSet.Put(SfxBoolItem(nWhich, bIsRotate));
            }

            break;
        }
        case SID_INSERT_GRAPHIC:
        case FN_FORMAT_GRAFIC_DLG:
        case SID_TWAIN_TRANSFER:
            if( bParentCntProt || !bIsGrfContent )
                bDisable = true;
            else if ( nWhich == SID_INSERT_GRAPHIC
                      && rSh.CursorInsideInputField() )
            {
                bDisable = true;
            }
            break;

        case SID_SAVE_GRAPHIC:
        case SID_EXTERNAL_EDIT:
            if( rSh.GetGraphicType() == GraphicType::NONE || GetObjectShell()->isExportLocked())
                bDisable = true;
            break;

        case SID_COLOR_SETTINGS:
        {
            if ( bParentCntProt || !bIsGrfContent )
                bDisable = true;
            else
            {
                svx::ToolboxAccess aToolboxAccess( TOOLBOX_NAME );
                rSet.Put( SfxBoolItem( nWhich, aToolboxAccess.isToolboxVisible() ) );
            }
            break;
        }

        case SID_FLIP_HORIZONTAL:
            if( !bParentCntProt )
            {
                MirrorGraph nState = aCoreSet.Get(
                                        RES_GRFATR_MIRRORGRF ).GetValue();

                rSet.Put(SfxBoolItem( nWhich, nState == MirrorGraph::Vertical ||
                                              nState == MirrorGraph::Both));
            }
            break;

        case SID_FLIP_VERTICAL:
            if( !bParentCntProt )
            {
                MirrorGraph nState = aCoreSet.GetItem<SwMirrorGrf>( RES_GRFATR_MIRRORGRF )->GetValue();
                rSet.Put(SfxBoolItem( nWhich, nState == MirrorGraph::Horizontal ||
                                              nState == MirrorGraph::Both));
            }
            break;

        case SID_ATTR_GRAF_LUMINANCE:
            if( !bParentCntProt )
                rSet.Put( SfxInt16Item( nWhich,
                        aCoreSet.Get(RES_GRFATR_LUMINANCE).GetValue() ));
            break;

        case SID_ATTR_GRAF_CONTRAST:
            if( !bParentCntProt )
                rSet.Put( SfxInt16Item( nWhich,
                        aCoreSet.Get(RES_GRFATR_CONTRAST).GetValue() ));
            break;

        case SID_ATTR_GRAF_RED:
            if( !bParentCntProt )
                rSet.Put( SfxInt16Item( nWhich,
                        aCoreSet.Get(RES_GRFATR_CHANNELR).GetValue() ));
            break;

        case SID_ATTR_GRAF_GREEN:
            if( !bParentCntProt )
                rSet.Put( SfxInt16Item( nWhich,
                        aCoreSet.Get(RES_GRFATR_CHANNELG).GetValue() ));
            break;

        case SID_ATTR_GRAF_BLUE:
            if( !bParentCntProt )
                rSet.Put( SfxInt16Item( nWhich,
                        aCoreSet.Get(RES_GRFATR_CHANNELB).GetValue() ));
            break;

        case SID_ATTR_GRAF_GAMMA:
            if( !bParentCntProt )
                rSet.Put( SfxUInt32Item( nWhich, static_cast< sal_uInt32 >(
                    aCoreSet.Get( RES_GRFATR_GAMMA ).GetValue() * 100 ) ) );
            break;

        case SID_ATTR_GRAF_TRANSPARENCE:
            if( !bParentCntProt )
            {
                // #119353# - robust
                const GraphicObject* pGrafObj = rSh.GetGraphicObj();
                if ( pGrafObj )
                {
                    if( pGrafObj->IsAnimated() ||
                        GraphicType::GdiMetafile == pGrafObj->GetType() )
                        bDisable = true;
                    else
                        rSet.Put( SfxUInt16Item( nWhich,
                            aCoreSet.Get(RES_GRFATR_TRANSPARENCY).GetValue() ));
                }
            }
            break;

        case SID_ATTR_GRAF_INVERT:
            if( !bParentCntProt )
                rSet.Put( SfxBoolItem( nWhich,
                        aCoreSet.Get(RES_GRFATR_INVERT).GetValue() ));
            break;

        case SID_ATTR_GRAF_MODE:
            if( !bParentCntProt )
                rSet.Put( SfxUInt16Item( nWhich, static_cast<sal_uInt16>(aCoreSet.Get(RES_GRFATR_DRAWMODE).GetValue()) ));
            break;

        case SID_GRFFILTER:
        case SID_GRFFILTER_INVERT:
        case SID_GRFFILTER_SMOOTH:
        case SID_GRFFILTER_SHARPEN:
        case SID_GRFFILTER_REMOVENOISE:
        case SID_GRFFILTER_SOBEL:
        case SID_GRFFILTER_MOSAIC:
        case SID_GRFFILTER_EMBOSS:
        case SID_GRFFILTER_POSTER:
        case SID_GRFFILTER_POPART:
        case SID_GRFFILTER_SEPIA:
        case SID_GRFFILTER_SOLARIZE:
            {
                if( bParentCntProt || !bIsGrfContent )
                    bDisable = true;
                // #i59688# load graphic only if type is unknown
                else
                {
                    const GraphicType eGraphicType( rSh.GetGraphicType() );
                    if ( ( eGraphicType == GraphicType::NONE ||
                           eGraphicType == GraphicType::Default ) &&
                         rSh.IsLinkedGrfSwapOut() )
                    {
                        rSet.DisableItem( nWhich );
                        if( AddGrfUpdateSlot( nWhich ))
                            rSh.GetGraphic(false);  // start the loading
                    }
                    else
                    {
                        bDisable = eGraphicType != GraphicType::Bitmap;
                    }
                }
            }
            break;

        case SID_OBJECT_CROP:
            {
                bDisable = FlyProtectFlags::NONE != rSh.IsSelObjProtected( FlyProtectFlags::Content|FlyProtectFlags::Parent );
                if( rSh.GetGraphicType() == GraphicType::NONE )
                    bDisable = true;
            }
            break;

        default:
            bDisable = false;
        }

        if( bDisable )
            rSet.DisableItem( nWhich );
        nWhich = aIter.NextWhich();
    }
    SetGetStateSet( nullptr );
}

void SwGrfShell::ExecuteRotation(SfxRequest const &rReq)
{
    // RotGrfFlyFrame: Modify rotation attribute instead of manipulating the graphic
    Degree10 aRotation;

    if (rReq.GetSlot() == SID_ROTATE_GRAPHIC_LEFT)
    {
        aRotation = 900_deg10;
    }
    else if (rReq.GetSlot() == SID_ROTATE_GRAPHIC_RIGHT)
    {
        aRotation = 2700_deg10;
    }
    else if (rReq.GetSlot() == SID_ROTATE_GRAPHIC_180)
    {
        aRotation = 1800_deg10;
    }

    if (rReq.GetSlot() != SID_ROTATE_GRAPHIC_RESET && 0_deg10 == aRotation)
        return;

    SwWrtShell& rShell = GetShell();
    SfxItemSetFixed<RES_GRFATR_ROTATION, RES_GRFATR_ROTATION> aSet( rShell.GetAttrPool() );
    rShell.GetCurAttr( aSet );
    const SwRotationGrf& rRotation = aSet.Get(RES_GRFATR_ROTATION);
    SwFlyFrameAttrMgr aMgr(false, &rShell, rShell.IsFrameSelected() ? Frmmgr_Type::NONE : Frmmgr_Type::GRF, nullptr);

    // RotGrfFlyFrame: Possible rotation change here, SwFlyFrameAttrMgr aMgr is available
    if (rReq.GetSlot() == SID_ROTATE_GRAPHIC_RESET)
    {
        aMgr.SetRotation(rRotation.GetValue(), 0_deg10, rRotation.GetUnrotatedSize());
    }
    else if(0_deg10 != aRotation)
    {
        const Degree10 aNewRotation((aRotation + rRotation.GetValue()) % 3600_deg10);

        aMgr.SetRotation(rRotation.GetValue(), aNewRotation, rRotation.GetUnrotatedSize());
    }
}

void SwGrfShell::GetAttrStateForRotation(SfxItemSet &rSet)
{
    SwWrtShell& rShell = GetShell();
    bool bIsParentContentProtected = FlyProtectFlags::NONE != rShell.IsSelObjProtected( FlyProtectFlags::Content|FlyProtectFlags::Parent );

    SetGetStateSet( &rSet );

    SfxWhichIter aIterator( rSet );
    sal_uInt16 nWhich = aIterator.FirstWhich();
    while( nWhich )
    {
        bool bDisable = bIsParentContentProtected;
        switch( nWhich )
        {
            case SID_ROTATE_GRAPHIC_LEFT:
            case SID_ROTATE_GRAPHIC_RIGHT:
            case SID_ROTATE_GRAPHIC_180:
            {
                if( rShell.GetGraphicType() == GraphicType::NONE )
                {
                    bDisable = true;
                }
                break;
            }
            case SID_ROTATE_GRAPHIC_RESET:
            {
                // RotGrfFlyFrame: disable when already no rotation
                SfxItemSetFixed<RES_GRFATR_ROTATION, RES_GRFATR_ROTATION> aSet( rShell.GetAttrPool() );
                rShell.GetCurAttr( aSet );
                const SwRotationGrf& rRotation = aSet.Get(RES_GRFATR_ROTATION);
                bDisable = (0_deg10 == rRotation.GetValue());
                break;
            }
            case SID_ATTR_TRANSFORM_ANGLE:
            {
                // RotGrfFlyFrame: get rotation value from RES_GRFATR_ROTATION and copy to rSet as
                // SID_ATTR_TRANSFORM_ANGLE, convert from 10th degrees to 100th degrees
                SfxItemSetFixed<RES_GRFATR_ROTATION, RES_GRFATR_ROTATION> aSet( rShell.GetAttrPool() );
                rShell.GetCurAttr( aSet );
                const SwRotationGrf& rRotation = aSet.Get(RES_GRFATR_ROTATION);
                rSet.Put(SdrAngleItem(SID_ATTR_TRANSFORM_ANGLE, to<Degree100>(rRotation.GetValue())));
                break;
            }
            default:
                bDisable = false;
        }

        if( bDisable )
            rSet.DisableItem( nWhich );
        nWhich = aIterator.NextWhich();
    }
    SetGetStateSet( nullptr );
}

SwGrfShell::~SwGrfShell()
{
}

SwGrfShell::SwGrfShell(SwView &_rView) :
    SwBaseShell(_rView)
{
    SetName(u"Graphic"_ustr);
    SfxShell::SetContextName(vcl::EnumContext::GetContextName(vcl::EnumContext::Context::Graphic));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
