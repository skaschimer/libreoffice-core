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

#include <config_features.h>

#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/embed/Aspects.hpp>
#include <com/sun/star/embed/XEmbedObjectClipboardCreator.hpp>
#include <com/sun/star/embed/NoVisualAreaSizeException.hpp>
#include <com/sun/star/embed/MSOLEObjectSystemCreator.hpp>
#include <com/sun/star/text/XPasteListener.hpp>

#include <svtools/embedtransfer.hxx>
#include <svtools/insdlg.hxx>
#include <unotools/tempfile.hxx>
#include <comphelper/fileformat.h>
#include <comphelper/processfactory.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/servicehelper.hxx>
#include <comphelper/storagehelper.hxx>
#include <comphelper/string.hxx>
#include <o3tl/deleter.hxx>
#include <o3tl/temporary.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <sot/filelist.hxx>
#include <svx/svxdlg.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <sfx2/linkmgr.hxx>
#include <tools/urlobj.hxx>
#include <vcl/weld.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/viewfrm.hxx>
#include <svl/stritem.hxx>
#include <vcl/imap.hxx>
#include <sot/storage.hxx>
#include <vcl/graph.hxx>
#include <svl/urihelper.hxx>
#include <svx/svdmodel.hxx>
#include <svx/xmlexchg.hxx>
#include <svx/dbaexchange.hxx>
#include <svx/clipfmtitem.hxx>
#include <sfx2/mieclip.hxx>
#include <svl/urlbmk.hxx>
#include <unicode/regex.h>
#include <vcl/inetimg.hxx>
#include <svx/fmview.hxx>
#include <sfx2/docfilt.hxx>
#include <vcl/imapobj.hxx>
#include <sfx2/docfile.hxx>
#include <unotools/transliterationwrapper.hxx>
#include <unotools/streamwrap.hxx>
#include <vcl/graphicfilter.hxx>

#ifdef _WIN32
#include <prewin.h>
#include <postwin.h>
#include <o3tl/char16_t2wchar_t.hxx>
#include <osl/file.hxx>
#include <systools/win32/extended_max_path.hxx>
#endif

#include <svx/unomodel.hxx>
#include <fmturl.hxx>
#include <fmtinfmt.hxx>
#include <swdtflvr.hxx>
#include <shellio.hxx>
#include <ddefld.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentState.hxx>
#include <IMark.hxx>
#include <section.hxx>
#include <ndtxt.hxx>
#include <edtdd.hxx>
#include <edtwin.hxx>
#include <navicont.hxx>
#include <swcont.hxx>
#include <wrtsh.hxx>
#include <swmodule.hxx>
#include <view.hxx>
#include <docsh.hxx>
#include <wdocsh.hxx>
#include <fldbas.hxx>
#include <swundo.hxx>
#include <pam.hxx>
#include <ndole.hxx>
#include <swwait.hxx>
#include <viewopt.hxx>
#include <SwCapObjType.hxx>
#include <cmdid.h>
#include <strings.hrc>
#include <svx/svditer.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/fhgtitem.hxx>
#include <editeng/prntitem.hxx>
#include <svx/svdpage.hxx>
#include <avmedia/mediawindow.hxx>
#include <swcrsr.hxx>
#include <SwRewriter.hxx>
#include <vcl/svapp.hxx>
#include <swserv.hxx>
#include <fmtmeta.hxx>
#include <itabenum.hxx>
#include <iodetect.hxx>
#include <unotextrange.hxx>
#include <unoframe.hxx>
#include <txatbase.hxx>
#include <unoparaframeenum.hxx>
#include <vcl/uitest/logger.hxx>
#include <vcl/uitest/eventdescription.hxx>

#include <vcl/GraphicNativeTransform.hxx>
#include <vcl/GraphicNativeMetadata.hxx>
#include <vcl/TypeSerializer.hxx>
#include <comphelper/lok.hxx>
#include <sfx2/classificationhelper.hxx>
#include <sfx2/sfxdlg.hxx>
#include <comphelper/classids.hxx>
#include <osl/diagnose.h>

#include <memory>

/* default (A4 format) width of 210mm - 2 * border size (border on both sides) */
constexpr tools::Long constOleWidthInMm = 210 - 2 * lMinBorderInMm;

constexpr Size constOleSize100mm(
    constOleWidthInMm * 100, // convert from mm to 100mm
    3000 // 3 cm
);

constexpr Size constOleSizeTwip = o3tl::convert(constOleSize100mm, o3tl::Length::mm100, o3tl::Length::twip);

constexpr sal_uInt32 SWTRANSFER_OBJECTTYPE_DRAWMODEL = 0x00000001;
constexpr sal_uInt32 SWTRANSFER_OBJECTTYPE_HTML      = 0x00000002;
constexpr sal_uInt32 SWTRANSFER_OBJECTTYPE_RTF       = 0x00000004;
constexpr sal_uInt32 SWTRANSFER_OBJECTTYPE_STRING    = 0x00000008;
constexpr sal_uInt32 SWTRANSFER_OBJECTTYPE_SWOLE     = 0x00000010;
constexpr sal_uInt32 SWTRANSFER_OBJECTTYPE_DDE       = 0x00000020;
constexpr sal_uInt32 SWTRANSFER_OBJECTTYPE_RICHTEXT  = 0x00000040;
constexpr sal_uInt32 SWTRANSFER_OBJECTTYPE_MARKDOWN  = 0x00000080;

using namespace ::svx;
using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::datatransfer;
namespace {

void collectUIInformation(const OUString& rAction, const OUString& aParameters)
{
    EventDescription aDescription;
    aDescription.aAction = rAction;
    aDescription.aParameters = {{"parameters", aParameters}};
    aDescription.aID = "writer_edit";
    aDescription.aKeyWord = "SwEditWinUIObject";
    aDescription.aParent = "MainWindow";
    UITestLogger::getInstance().logEvent(aDescription);
}

}

class SwTransferDdeLink : public ::sfx2::SvBaseLink
{
    OUString m_sName;
    ::sfx2::SvLinkSourceRef m_xRefObj;
    SwTransferable& m_rTransfer;
    SwDocShell* m_pDocShell;
    sal_uLong m_nOldTimeOut;
    bool m_bDelBookmark : 1;
    bool m_bInDisconnect : 1;

    bool FindDocShell();

    using sfx2::SvBaseLink::Disconnect;

protected:
    virtual ~SwTransferDdeLink() override;

public:
    SwTransferDdeLink( SwTransferable& rTrans, SwWrtShell& rSh );

    virtual ::sfx2::SvBaseLink::UpdateResult DataChanged(
        const OUString& rMimeType, const css::uno::Any & rValue ) override;
    virtual void Closed() override;

    bool WriteData( SvStream& rStrm );

    void Disconnect( bool bRemoveDataAdvise );
};

/// Tracks the boundaries of pasted content and notifies listeners.
class SwPasteContext
{
public:
    SwPasteContext(SwWrtShell& rWrtShell);
    ~SwPasteContext();

    void remember();
    void forget();

private:
    SwWrtShell& m_rWrtShell;
    std::optional<SwPaM> m_oPaM;
    sal_Int32 m_nStartContent = 0;
};

namespace {

// helper class for Action and Undo enclosing
class SwTrnsfrActionAndUndo
{
    SwWrtShell *pSh;
public:
    SwTrnsfrActionAndUndo( SwWrtShell *pS, bool bDelSel = false, SwPasteContext* pContext = nullptr)
        : pSh( pS )
    {
        pSh->StartUndo( SwUndoId::PASTE_CLIPBOARD );
        if( bDelSel )
        {
            if (pContext)
                pContext->forget();
            pSh->DelRight();
            if (pContext)
                pContext->remember();
        }
        pSh->StartAllAction();
    }
    ~SwTrnsfrActionAndUndo() COVERITY_NOEXCEPT_FALSE
    {
        pSh->EndUndo();
        pSh->EndAllAction();
    }
};

}

SwTransferable::SwTransferable( SwWrtShell& rSh )
    : m_pWrtShell( &rSh ),
    m_pCreatorView( nullptr ),
    m_pOrigGraphic( nullptr ),
    m_eBufferType( TransferBufferType::NONE ),
    m_bOldIdle(false),
    m_bCleanUp(false)
{
    rSh.GetView().AddTransferable(*this);
    SwDocShell* pDShell = rSh.GetDoc()->GetDocShell();
    if( !pDShell )
        return;

    pDShell->FillTransferableObjectDescriptor( m_aObjDesc );
    if( pDShell->GetMedium() )
    {
        const INetURLObject& rURLObj = pDShell->GetMedium()->GetURLObject();
        m_aObjDesc.maDisplayName = URIHelper::removePassword(
                            rURLObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ),
                            INetURLObject::EncodeMechanism::WasEncoded,
                            INetURLObject::DecodeMechanism::Unambiguous );
    }

    PrepareOLE( m_aObjDesc );
}

SwTransferable::~SwTransferable()
{
    SolarMutexGuard aSolarGuard;

    // the DDELink still needs the WrtShell!
    DisconnectDDE();

    m_pWrtShell = nullptr;

    // release reference to the document so that aDocShellRef will delete
    // it (if aDocShellRef is set). Otherwise, the OLE nodes keep references
    // to their sub-storage when the storage is already dead.
    m_pClpDocFac.reset();

    // first close, then the Ref. can be cleared as well, so that
    // the DocShell really gets deleted!
    if( m_aDocShellRef.Is() )
    {
        SfxObjectShell * pObj = m_aDocShellRef;
        SwDocShell* pDocSh = static_cast<SwDocShell*>(pObj);
        pDocSh->DoClose();
    }
    m_aDocShellRef.Clear();

    if (SwModule* pMod = SwModule::get())
    {
        if ( pMod->m_pDragDrop == this )
            pMod->m_pDragDrop = nullptr;
        else if ( pMod->m_pXSelection == this )
            pMod->m_pXSelection = nullptr;
    }

    m_eBufferType = TransferBufferType::NONE;
}

static SwDoc& lcl_GetDoc(SwDocFac & rDocFac)
{
    SwDoc& rDoc = rDocFac.GetDoc();
    rDoc.SetClipBoard( true );
    return rDoc;
}

void SwTransferable::ObjectReleased()
{
    SwModule* pMod = SwModule::get();
    if (!pMod)
        return;
    if( this == pMod->m_pDragDrop )
        pMod->m_pDragDrop = nullptr;
    else if( this == pMod->m_pXSelection )
        pMod->m_pXSelection = nullptr;
}

void SwTransferable::AddSupportedFormats()
{
    // only need if we are the current XSelection Object
    if (this == SwModule::get()->m_pXSelection || comphelper::LibreOfficeKit::isActive())
    {
        SetDataForDragAndDrop( Point( 0,0) );
    }
}

void SwTransferable::InitOle( SfxObjectShell* pDoc )
{
    //set OleVisArea. Upper left corner of the page and size of
    //RealSize in Twips.
    const Size aSz(constOleSizeTwip);
    SwRect aVis( Point( DOCUMENTBORDER, DOCUMENTBORDER ), aSz );
    pDoc->SetVisArea( aVis.SVRect() );
}

uno::Reference < embed::XEmbeddedObject > SwTransferable::FindOLEObj( sal_Int64& nAspect ) const
{
    uno::Reference < embed::XEmbeddedObject > xObj;
    if( m_pClpDocFac )
    {
        SwIterator<SwContentNode,SwFormatColl> aIter( *m_pClpDocFac->GetDoc().GetDfltGrfFormatColl() );
        for( SwContentNode* pNd = aIter.First(); pNd; pNd = aIter.Next() )
            if( SwNodeType::Ole == pNd->GetNodeType() )
            {
                xObj = static_cast<SwOLENode*>(pNd)->GetOLEObj().GetOleRef();
                nAspect = static_cast<SwOLENode*>(pNd)->GetAspect();
                break;
            }
    }
    return xObj;
}

const Graphic* SwTransferable::FindOLEReplacementGraphic() const
{
    if( m_pClpDocFac )
    {
        SwIterator<SwContentNode,SwFormatColl> aIter( *m_pClpDocFac->GetDoc().GetDfltGrfFormatColl() );
        for( SwContentNode* pNd = aIter.First(); pNd; pNd = aIter.Next() )
            if( SwNodeType::Ole == pNd->GetNodeType() )
            {
                return static_cast<SwOLENode*>(pNd)->GetGraphic();
            }
    }

    return nullptr;
}

void SwTransferable::RemoveDDELinkFormat(vcl::Window& rWin)
{
    RemoveFormat( SotClipboardFormatId::LINK );
    if (rWin.GetClipboard()->getContents().get() == this)
        CopyToClipboard(&rWin);
}

void SwTransferable::DisconnectDDE()
{
    if( m_xDdeLink.is() )
    {
        m_xDdeLink->Disconnect( true );
        m_xDdeLink.clear();
    }
}

namespace
{
    //Resolves: fdo#40717 surely when we create a clipboard document we should
    //overwrite the clipboard documents styles and settings with that of the
    //source, so that we can WYSIWYG paste. If we want that the destinations
    //styles are used over the source styles, that's a matter of the
    //destination paste code to handle, not the source paste code.
    void lclOverWriteDoc(SwWrtShell &rSrcWrtShell, SwDoc &rDest, bool bDeleteRedlines = true)
    {
        const SwDoc &rSrc = *rSrcWrtShell.GetDoc();

        rDest.ReplaceCompatibilityOptions(rSrc);
        rDest.ReplaceDefaults(rSrc);

        //It would probably make most sense here to only insert the styles used
        //by the selection, e.g. apply SwDoc::IsUsed on styles ?
        rDest.ReplaceStyles(rSrc, false);

        rSrcWrtShell.Copy(rDest, /*pNewClpText=*/nullptr, bDeleteRedlines);

        rDest.GetMetaFieldManager().copyDocumentProperties(rSrc);
    }

    void lclCheckAndPerformRotation(Graphic& aGraphic)
    {
        GraphicNativeMetadata aMetadata;
        if ( !aMetadata.read(aGraphic) )
            return;

        Degree10 aRotation = aMetadata.getRotation();
        if (aRotation)
        {
            GraphicNativeTransform aTransform( aGraphic );
            aTransform.rotate( aRotation );
        }
    }
}

sal_Bool SAL_CALL SwTransferable::isComplex()
{
    sal_Int32 nTextLength = 0;
    SwNodes& aNodes = m_pWrtShell->GetDoc()->GetNodes();
    for (SwPaM& rPaM : m_pWrtShell->GetCursor()->GetRingContainer())
    {
        for (SwNodeOffset nIndex = rPaM.GetMark()->GetNodeIndex();
             nIndex <= rPaM.GetPoint()->GetNodeIndex(); ++nIndex)
        {
            SwNode& rNd = *aNodes[nIndex];

            SwTextNode* pTextNode = rNd.GetTextNode();
            if (pTextNode)
            {
                if (pTextNode->HasHints())
                {
                    for (size_t nHint = 0; nHint < pTextNode->GetSwpHints().Count(); ++nHint)
                    {
                        SwTextAttr* pHint = pTextNode->GetSwpHints().Get(nHint);
                        if (pHint->Which() == RES_TXTATR_FLYCNT)
                        {
                            return true; // Complex
                        }
                    }
                }

                FrameClientSortList_t vFrames;
                ::CollectFrameAtNode(rNd, vFrames, true);
                if (!vFrames.empty())
                {
                    // There is an at-char anchored object to this node, that's complex.
                    return true;
                }

                nTextLength += pTextNode->GetText().getLength();
                if (nTextLength >= 1024 * 512)
                    return true; // Complex
            }
        }
    }

    if (m_pWrtShell->GetSelectionType() == SelectionType::DrawObject)
        return true; // Complex

    // Simple
    return false;
}

bool SwTransferable::GetData( const DataFlavor& rFlavor, const OUString& rDestDoc )
{
    SotClipboardFormatId nFormat = SotExchange::GetFormat( rFlavor );

    // we can only fulfil the request if
    // 1) we have data for this format
    // 2) we have either a clipboard document (pClpDocFac), or
    //    we have a SwWrtShell (so we can generate a new clipboard document)
    if( !HasFormat( nFormat ) || ( m_pClpDocFac == nullptr && m_pWrtShell == nullptr ) )
        return false;

    if( !m_pClpDocFac )
    {
        SelectionType nSelectionType = m_pWrtShell->GetSelectionType();

        // when pending we will not get the correct type, but SelectionType::Text
        // as fallback. This *happens* during D&D, so we need to check if we are in
        // the fallback and just try to get a graphic
        const bool bPending(m_pWrtShell->ActionPend());

        // SEL_GRF is from ContentType of editsh
        if(bPending || ((SelectionType::Graphic | SelectionType::DrawObject | SelectionType::DbForm) & nSelectionType))
        {
            m_oClpGraphic.emplace();
            if( !m_pWrtShell->GetDrawObjGraphic( SotClipboardFormatId::GDIMETAFILE, *m_oClpGraphic ))
                m_pOrigGraphic = &*m_oClpGraphic;
            m_oClpBitmap.emplace();
            if( !m_pWrtShell->GetDrawObjGraphic( SotClipboardFormatId::BITMAP, *m_oClpBitmap ))
                m_pOrigGraphic = &*m_oClpBitmap;

            // is it a URL-Button ?
            OUString sURL;
            OUString sDesc;
            if( m_pWrtShell->GetURLFromButton( sURL, sDesc ) )
            {
                m_oBookmark.emplace( sURL, sDesc );
                m_eBufferType = TransferBufferType::InetField;
            }
        }

        m_pClpDocFac.reset(new SwDocFac);
        SwDoc& rTmpDoc = lcl_GetDoc(*m_pClpDocFac);

        rTmpDoc.getIDocumentFieldsAccess().LockExpFields();     // never update fields - leave text as it is
        lclOverWriteDoc(*m_pWrtShell, rTmpDoc);

        // in CORE a new one was created (OLE-objects copied!)
        m_aDocShellRef = rTmpDoc.GetTmpDocShell();
        if( m_aDocShellRef.Is() )
            SwTransferable::InitOle( m_aDocShellRef );
        rTmpDoc.SetTmpDocShell( nullptr );

        if( nSelectionType & SelectionType::Text && !m_pWrtShell->HasMark() )
        {
            SwContentAtPos aContentAtPos( IsAttrAtPos::InetAttr );

            Point aPos( SwEditWin::GetDDStartPosX(), SwEditWin::GetDDStartPosY());

            bool bSelect = g_bExecuteDrag &&
                            m_pWrtShell->GetView().GetDocShell() &&
                            !m_pWrtShell->GetView().GetDocShell()->IsReadOnly();
            if( m_pWrtShell->GetContentAtPos( aPos, aContentAtPos, bSelect ) )
            {
                m_oBookmark.emplace(
                        static_cast<const SwFormatINetFormat*>(aContentAtPos.aFnd.pAttr)->GetValue(),
                        aContentAtPos.sStr );
                m_eBufferType = TransferBufferType::InetField;
                if( bSelect )
                    m_pWrtShell->SelectTextAttr( RES_TXTATR_INETFMT );
            }
        }
        if( m_pWrtShell->IsFrameSelected() )
        {
            SfxItemSetFixed<RES_URL, RES_URL> aSet( m_pWrtShell->GetAttrPool() );
            m_pWrtShell->GetFlyFrameAttr( aSet );
            const SwFormatURL& rURL = aSet.Get( RES_URL );
            if( rURL.GetMap() )
                m_pImageMap.reset(new ImageMap( *rURL.GetMap() ));
            else if( !rURL.GetURL().isEmpty() )
                m_pTargetURL.reset(new INetImage(OUString(), rURL.GetURL(),
                                            rURL.GetTargetFrameName() ));
        }
    }

    bool bOK = false;
    if( TransferBufferType::Ole == m_eBufferType )
    {
        //TODO/MBA: testing - is this the "single OLE object" case?!
        // get OLE-Object from ClipDoc and get the data from that.
        sal_Int64 nAspect = embed::Aspects::MSOLE_CONTENT; // will be set in the next statement
        uno::Reference < embed::XEmbeddedObject > xObj = FindOLEObj( nAspect );
        const Graphic* pOLEGraph = FindOLEReplacementGraphic();
        if( xObj.is() )
        {
            TransferableDataHelper aD( new SvEmbedTransferHelper( xObj, pOLEGraph, nAspect ) );
            uno::Any aAny = aD.GetAny(rFlavor, rDestDoc);
            if( aAny.hasValue() )
                bOK = SetAny( aAny );
        }

        // the following solution will be used in the case when the object can not generate the image
        // TODO/LATER: in future the transferhelper must probably be created based on object and the replacement stream
        // TODO: Block not required now, SvEmbedTransferHelper should be able to handle GDIMetaFile format
        if ( nFormat == SotClipboardFormatId::GDIMETAFILE )
        {
            pOLEGraph = FindOLEReplacementGraphic();
            if ( pOLEGraph )
                bOK = SetGDIMetaFile( pOLEGraph->GetGDIMetaFile() );
        }
    }
    else
    {
        switch( nFormat )
        {
        case SotClipboardFormatId::LINK:
            if( m_xDdeLink.is() )
                bOK = SetObject( m_xDdeLink.get(), SWTRANSFER_OBJECTTYPE_DDE, rFlavor );
            break;

        case SotClipboardFormatId::OBJECTDESCRIPTOR:
        case SotClipboardFormatId::LINKSRCDESCRIPTOR:
            bOK = SetTransferableObjectDescriptor( m_aObjDesc );
            break;

        case SotClipboardFormatId::DRAWING:
            {
                SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
                bOK = SetObject( rDoc.getIDocumentDrawModelAccess().GetDrawModel(),
                                SWTRANSFER_OBJECTTYPE_DRAWMODEL, rFlavor );
            }
            break;

        case SotClipboardFormatId::STRING:
        {
            SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
            bOK = SetObject( &rDoc, SWTRANSFER_OBJECTTYPE_STRING, rFlavor );
        }
        break;
        case SotClipboardFormatId::RTF:
        {
            SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
            bOK = SetObject( &rDoc, SWTRANSFER_OBJECTTYPE_RTF, rFlavor );
        }
        break;
        case SotClipboardFormatId::RICHTEXT:
        {
            SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
            bOK = SetObject( &rDoc, SWTRANSFER_OBJECTTYPE_RICHTEXT, rFlavor );
        }
        break;

        case SotClipboardFormatId::MARKDOWN:
        {
            SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
            bOK = SetObject(&rDoc, SWTRANSFER_OBJECTTYPE_MARKDOWN, rFlavor);
            break;
        }

        case SotClipboardFormatId::HTML:
        {
            SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
            bOK = SetObject( &rDoc, SWTRANSFER_OBJECTTYPE_HTML, rFlavor );
        }
            break;

        case SotClipboardFormatId::SVXB:
            if( m_eBufferType & TransferBufferType::Graphic && m_pOrigGraphic )
                bOK = SetGraphic( *m_pOrigGraphic );
            break;

        case SotClipboardFormatId::GDIMETAFILE:
            if( m_eBufferType & TransferBufferType::Graphic )
                bOK = SetGDIMetaFile( m_oClpGraphic->GetGDIMetaFile() );
            break;
        case SotClipboardFormatId::BITMAP:
        case SotClipboardFormatId::PNG:
            // Neither pClpBitmap nor pClpGraphic are necessarily set
            if( (m_eBufferType & TransferBufferType::Graphic) && (m_oClpBitmap || m_oClpGraphic))
                bOK = SetBitmapEx( (m_oClpBitmap ? m_oClpBitmap : m_oClpGraphic)->GetBitmapEx(), rFlavor );
            break;

        case SotClipboardFormatId::SVIM:
            if( m_pImageMap )
                bOK = SetImageMap( *m_pImageMap );
            break;

        case SotClipboardFormatId::INET_IMAGE:
            if( m_pTargetURL )
                bOK = SetINetImage( *m_pTargetURL, rFlavor );
            break;

        case SotClipboardFormatId::SOLK:
        case SotClipboardFormatId::NETSCAPE_BOOKMARK:
        case SotClipboardFormatId::FILEGRPDESCRIPTOR:
        case SotClipboardFormatId::FILECONTENT:
        case SotClipboardFormatId::UNIFORMRESOURCELOCATOR:
        case SotClipboardFormatId::SIMPLE_FILE:
            if( (TransferBufferType::InetField & m_eBufferType) && m_oBookmark )
                bOK = SetINetBookmark( *m_oBookmark, rFlavor );
            break;

        case SotClipboardFormatId::EMBED_SOURCE:
            if( !m_aDocShellRef.Is() )
            {
                SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
                m_aDocShellRef = new SwDocShell(rDoc, SfxObjectCreateMode::EMBEDDED);
                m_aDocShellRef->DoInitNew();
                SwTransferable::InitOle( m_aDocShellRef );
            }
            bOK = SetObject( &m_aDocShellRef, SWTRANSFER_OBJECTTYPE_SWOLE,
                            rFlavor );
            break;
        default: break;
        }
    }
    return bOK;
}

bool SwTransferable::WriteObject( SvStream& rOStream,
                                    void* pObject, sal_uInt32 nObjectType,
                                    const DataFlavor& /*rFlavor*/ )
{
    bool bRet = false;
    WriterRef xWrt;

    switch( nObjectType )
    {
    case SWTRANSFER_OBJECTTYPE_DRAWMODEL:
        {
            // don't change the sequence of commands
            SdrModel *pModel = static_cast<SdrModel*>(pObject);
            rOStream.SetBufferSize( 16348 );

            // for the changed pool defaults from drawing layer pool set those
            // attributes as hard attributes to preserve them for saving
            const SfxItemPool& rItemPool = pModel->GetItemPool();
            const SvxFontHeightItem& rDefaultFontHeight = rItemPool.GetUserOrPoolDefaultItem(EE_CHAR_FONTHEIGHT);

            // SW should have no MasterPages
            OSL_ENSURE(0 == pModel->GetMasterPageCount(), "SW with MasterPages (!)");

            for(sal_uInt16 a(0); a < pModel->GetPageCount(); a++)
            {
                const SdrPage* pPage = pModel->GetPage(a);
                SdrObjListIter aIter(pPage, SdrIterMode::DeepNoGroups);

                while(aIter.IsMore())
                {
                    SdrObject* pObj = aIter.Next();
                    const SvxFontHeightItem& rItem = pObj->GetMergedItem(EE_CHAR_FONTHEIGHT);

                    if(rItem.GetHeight() == rDefaultFontHeight.GetHeight())
                    {
                        pObj->SetMergedItem(rDefaultFontHeight);
                    }
                }
            }

            {
                uno::Reference<io::XOutputStream> xDocOut( new utl::OOutputStreamWrapper( rOStream ) );
                bRet = SvxDrawingLayerExport( pModel, xDocOut );
            }

            if (bRet)
                bRet = ERRCODE_NONE == rOStream.GetError();
        }
        break;

    case SWTRANSFER_OBJECTTYPE_SWOLE:
        {
            SfxObjectShell*   pEmbObj = static_cast<SfxObjectShell*>(pObject);
            try
            {
                ::utl::TempFileFast aTempFile;
                SvStream* pTempStream = aTempFile.GetStream(StreamMode::READWRITE);
                uno::Reference< embed::XStorage > xWorkStore =
                    ::comphelper::OStorageHelper::GetStorageFromStream( new utl::OStreamWrapper(*pTempStream), embed::ElementModes::READWRITE );

                // write document storage
                pEmbObj->SetupStorage( xWorkStore, SOFFICE_FILEFORMAT_CURRENT, false );
                // mba: no BaseURL for clipboard
                SfxMedium aMedium( xWorkStore, OUString() );
                pEmbObj->DoSaveObjectAs( aMedium, false );
                pEmbObj->DoSaveCompleted();

                uno::Reference< embed::XTransactedObject > xTransact( xWorkStore, uno::UNO_QUERY );
                if ( xTransact.is() )
                    xTransact->commit();

                rOStream.SetBufferSize( 0xff00 );
                rOStream.WriteStream( *pTempStream );

                xWorkStore->dispose();
                xWorkStore.clear();
            }
            catch (const uno::Exception&)
            {
            }

            bRet = ( rOStream.GetError() == ERRCODE_NONE );
        }
        break;

    case SWTRANSFER_OBJECTTYPE_DDE:
        {
            rOStream.SetBufferSize( 1024 );
            SwTransferDdeLink* pDdeLnk = static_cast<SwTransferDdeLink*>(pObject);
            if( pDdeLnk->WriteData( rOStream ) )
            {
                bRet = ERRCODE_NONE == rOStream.GetError();
            }
        }
        break;

    case SWTRANSFER_OBJECTTYPE_HTML:
    {
        // LOK is interested in getting images embedded for copy/paste support.
        GetHTMLWriter( comphelper::LibreOfficeKit::isActive() ? u"EmbedImages;NoPrettyPrint"_ustr : OUString(), OUString(), xWrt );
        break;
    }

    case SWTRANSFER_OBJECTTYPE_RTF:
    case SWTRANSFER_OBJECTTYPE_RICHTEXT:
        GetRTFWriter(std::u16string_view(), OUString(), xWrt);
        break;

    case SWTRANSFER_OBJECTTYPE_MARKDOWN:
    {
        GetMDWriter(std::u16string_view(), OUString(), xWrt);
        break;
    }

    case SWTRANSFER_OBJECTTYPE_STRING:
        GetASCWriter(std::u16string_view(), OUString(), xWrt);
        if( xWrt.is() )
        {
            SwAsciiOptions aAOpt;
            aAOpt.SetCharSet( RTL_TEXTENCODING_UTF8 );
            xWrt->SetAsciiOptions( aAOpt );

            // no start char for clipboard
            xWrt->m_bUCS2_WithStartChar = false;
        }
        break;
    default: break;
    }

    if( xWrt.is() )
    {
        SwDoc* pDoc = static_cast<SwDoc*>(pObject);
        xWrt->m_bWriteClipboardDoc = true;
        xWrt->m_bWriteOnlyFirstTable = bool(TransferBufferType::Table & m_eBufferType);
        xWrt->SetShowProgress(false);

#if defined(DEBUGPASTE)
        SvFileStream aPasteDebug(
            u"PASTEBUFFER.debug"_ustr, StreamMode::WRITE|StreamMode::TRUNC);
        SwWriter aDbgWrt( aPasteDebug, *pDoc );
        aDbgWrt.Write( xWrt );
#endif

        SwWriter aWrt( rOStream, *pDoc );
        if( ! aWrt.Write( xWrt ).IsError() )
        {
            rOStream.WriteChar( '\0' );               // terminate with a zero
            bRet = true;
        }
    }

    return bRet;
}

int SwTransferable::Cut()
{
    int nRet = Copy( true );
    if( nRet )
        DeleteSelection();
    collectUIInformation(u"CUT"_ustr, u"parameter"_ustr);
    return nRet;
}

void SwTransferable::DeleteSelection()
{
    if(!m_pWrtShell)
        return;
    // ask for type of selection before action-bracketing
    const SelectionType nSelection = m_pWrtShell->GetSelectionType();
    // cut rows or columns selected by enhanced table selection and wholly selected tables
    bool bCutMode = ( SelectionType::TableCell & nSelection ) && ( (SelectionType::TableRow | SelectionType::TableCol) & nSelection ||
        m_pWrtShell->HasWholeTabSelection() );

    m_pWrtShell->StartUndo( SwUndoId::START );
    if( bCutMode )
    {
        if( !(SelectionType::TableCol & nSelection) )
            m_pWrtShell->DeleteTable();
        else
        {
            SfxDispatcher* pDispatch = m_pWrtShell->GetView().GetViewFrame().GetDispatcher();
            pDispatch->Execute(FN_TABLE_DELETE_COL, SfxCallMode::SYNCHRON);
        }
    }
    else
    {
        if( ( SelectionType::Text | SelectionType::Table ) & nSelection )
            m_pWrtShell->IntelligentCut( nSelection );
        m_pWrtShell->DelRight();
    }
    m_pWrtShell->EndUndo( SwUndoId::END );
}

static void DeleteDDEAndReminderMarks(SwDoc & rDest)
{
    IDocumentMarkAccess *const pMarkAccess = rDest.getIDocumentMarkAccess();
    std::vector< ::sw::mark::MarkBase* > vMarks;
    // find all DDE-Bookmarks and Navigator-Reminders
    for (auto ppMark = pMarkAccess->getAllMarksBegin();
        ppMark != pMarkAccess->getAllMarksEnd();
        ++ppMark)
    {
        IDocumentMarkAccess::MarkType eMarkType = IDocumentMarkAccess::GetType(**ppMark);
        if (eMarkType == IDocumentMarkAccess::MarkType::DDE_BOOKMARK
            || eMarkType == IDocumentMarkAccess::MarkType::NAVIGATOR_REMINDER)
        {
            vMarks.push_back(*ppMark);
        }
    }
    // remove all DDE-Bookmarks, they are invalid inside the clipdoc!
    // and remove all Navigator-Reminders (tdf#165223)
    for (const auto& rpMark : vMarks)
    {
        pMarkAccess->deleteMark(rpMark);
    }
}

void SwTransferable::PrepareForCopyTextRange(SwPaM & rPaM)
{
    std::optional<SwWait> oWait;
    if (m_pWrtShell->ShouldWait())
    {
        oWait.emplace( *m_pWrtShell->GetView().GetDocShell(), true );
    }

    m_pClpDocFac.reset(new SwDocFac);

    SwDoc& rDest(lcl_GetDoc(*m_pClpDocFac));
    rDest.getIDocumentFieldsAccess().LockExpFields(); // Never update fields - leave text as is
    {
        SwDoc const& rSrc(*m_pWrtShell->GetDoc());
        assert(&rSrc == &rPaM.GetDoc());

        rDest.ReplaceCompatibilityOptions(rSrc);
        rDest.ReplaceDefaults(rSrc);

        //It would probably make most sense here to only insert the styles used
        //by the selection, e.g. apply SwDoc::IsUsed on styles ?
        rDest.ReplaceStyles(rSrc, false);

        // relevant bits of rSrcWrtShell.Copy(rDest);
        rDest.GetIDocumentUndoRedo().DoUndo(false); // always false!
        rDest.getIDocumentRedlineAccess().SetRedlineFlags_intern( RedlineFlags::DeleteRedlines );

        SwNodeIndex const aIdx(rDest.GetNodes().GetEndOfContent(), -1);
        SwContentNode *const pContentNode(aIdx.GetNode().GetContentNode());
        SwPosition aPos(aIdx, pContentNode, pContentNode ? pContentNode->Len() : 0);

        rSrc.getIDocumentContentOperations().CopyRange(rPaM, aPos, SwCopyFlags::CheckPosInFly);

        rDest.getIDocumentRedlineAccess().SetRedlineFlags_intern( RedlineFlags::NONE );

        rDest.GetMetaFieldManager().copyDocumentProperties(rSrc);
    }

    DeleteDDEAndReminderMarks(rDest);

    // a new one was created in core (OLE objects copied!)
    m_aDocShellRef = rDest.GetTmpDocShell();
    if (m_aDocShellRef.Is())
        SwTransferable::InitOle( m_aDocShellRef );
    rDest.SetTmpDocShell( nullptr );

    // let's add some formats
    AddFormat( SotClipboardFormatId::EMBED_SOURCE );
    AddFormat( SotClipboardFormatId::RTF );
#if HAVE_FEATURE_DESKTOP
    AddFormat( SotClipboardFormatId::RICHTEXT );
    AddFormat( SotClipboardFormatId::HTML );
    AddFormat( SotClipboardFormatId::MARKDOWN );
#endif
    AddFormat( SotClipboardFormatId::STRING );
}

int SwTransferable::PrepareForCopy( bool bIsCut, bool bDeleteRedlines )
{
    int nRet = 1;
    if(!m_pWrtShell)
        return 0;

    if ( m_pWrtShell->GetTableInsertMode() != SwTable::SEARCH_NONE )
        m_pWrtShell->SetTableInsertMode( SwTable::SEARCH_NONE );

    if ( m_pWrtShell->GetTableCopied() )
        m_pWrtShell->SetTableCopied( false );

    OUString sGrfNm;
    const SelectionType nSelection = m_pWrtShell->GetSelectionType();
    if( nSelection == SelectionType::Graphic )
    {
        m_oClpGraphic.emplace();
        if( !m_pWrtShell->GetDrawObjGraphic( SotClipboardFormatId::GDIMETAFILE, *m_oClpGraphic ))
            m_pOrigGraphic = &*m_oClpGraphic;
        m_oClpBitmap.emplace();
        if( !m_pWrtShell->GetDrawObjGraphic( SotClipboardFormatId::BITMAP, *m_oClpBitmap ))
            m_pOrigGraphic = &*m_oClpBitmap;

        m_pClpDocFac.reset(new SwDocFac);
        SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
        m_pWrtShell->Copy(rDoc);

#if HAVE_FEATURE_DESKTOP
        if (m_pOrigGraphic && !m_pOrigGraphic->GetBitmapEx().IsEmpty())
          AddFormat( SotClipboardFormatId::SVXB );
#endif

        PrepareOLE( m_aObjDesc );
        AddFormat( SotClipboardFormatId::OBJECTDESCRIPTOR );

        const Graphic* pGrf = m_pWrtShell->GetGraphic();
        if( pGrf && pGrf->IsSupportedGraphic() )
        {
            AddFormat( SotClipboardFormatId::PNG );
#if HAVE_FEATURE_DESKTOP
            AddFormat( SotClipboardFormatId::GDIMETAFILE );
            AddFormat( SotClipboardFormatId::BITMAP );
#endif
        }
        m_eBufferType = TransferBufferType::Graphic;
        m_pWrtShell->GetGrfNms( &sGrfNm, nullptr );
    }
    else if ( nSelection == SelectionType::Ole )
    {
        m_pClpDocFac.reset(new SwDocFac);
        SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
        m_aDocShellRef = new SwDocShell(rDoc, SfxObjectCreateMode::EMBEDDED);
        m_aDocShellRef->DoInitNew();
        m_pWrtShell->Copy(rDoc);

        AddFormat( SotClipboardFormatId::EMBED_SOURCE );

        // --> OD #i98753#
        // set size of embedded object at the object description structure
        m_aObjDesc.maSize = o3tl::convert(m_pWrtShell->GetObjSize(), o3tl::Length::twip, o3tl::Length::mm100);

        // <--
        PrepareOLE( m_aObjDesc );

#if HAVE_FEATURE_DESKTOP
        AddFormat( SotClipboardFormatId::OBJECTDESCRIPTOR );
        AddFormat( SotClipboardFormatId::GDIMETAFILE );

        // Fetch the formats supported via embedtransferhelper as well
        sal_Int64 nAspect = embed::Aspects::MSOLE_CONTENT;
        uno::Reference < embed::XEmbeddedObject > xObj = FindOLEObj( nAspect );
        const Graphic* pOLEGraph = FindOLEReplacementGraphic();
        if( xObj.is() )
        {
            TransferableDataHelper aD( new SvEmbedTransferHelper( xObj, pOLEGraph, nAspect ) );
            if ( aD.GetTransferable().is() )
            {
                DataFlavorExVector              aVector( aD.GetDataFlavorExVector() );

                for( const auto& rItem : aVector )
                    AddFormat( rItem );
            }
        }
#endif
        m_eBufferType = TransferBufferType::Ole;
    }
    // Is there anything to provide anyway?
    else if ( m_pWrtShell->IsSelection() || m_pWrtShell->IsFrameSelected() ||
              m_pWrtShell->GetSelectedObjCount() )
    {
        std::optional<SwWait> oWait;
        if( m_pWrtShell->ShouldWait() )
            oWait.emplace( *m_pWrtShell->GetView().GetDocShell(), true );

        m_pClpDocFac.reset(new SwDocFac);

        // create additional cursor so that equal treatment of keyboard
        // and mouse selection is possible.
        // In AddMode with keyboard selection, the new cursor is not created
        // before the cursor is moved after end of selection.
        if( m_pWrtShell->IsAddMode() && m_pWrtShell->SwCursorShell::HasSelection() )
            m_pWrtShell->CreateCursor();

        SwDoc& rTmpDoc = lcl_GetDoc(*m_pClpDocFac);

        rTmpDoc.getIDocumentFieldsAccess().LockExpFields();     // Never update fields - leave text as is
        lclOverWriteDoc(*m_pWrtShell, rTmpDoc, bDeleteRedlines);

        DeleteDDEAndReminderMarks(rTmpDoc);

        // a new one was created in CORE (OLE objects copied!)
        m_aDocShellRef = rTmpDoc.GetTmpDocShell();
        if( m_aDocShellRef.Is() )
            SwTransferable::InitOle( m_aDocShellRef );
        rTmpDoc.SetTmpDocShell( nullptr );

        if( m_pWrtShell->GetSelectedObjCount() )
            m_eBufferType = TransferBufferType::Drawing;
        else
        {
            m_eBufferType = TransferBufferType::Document;
            if (m_pWrtShell->IntelligentCut(nSelection, false) != SwWrtShell::NO_WORD)
                m_eBufferType = TransferBufferType::DocumentWord | m_eBufferType;
        }

        bool bDDELink = m_pWrtShell->IsSelection();
        if( nSelection & SelectionType::TableCell )
        {
            m_eBufferType = TransferBufferType::Table | m_eBufferType;
            bDDELink = m_pWrtShell->HasWholeTabSelection();

            m_pWrtShell->SetTableCopied(true);

            if ( bIsCut && (SelectionType::TableRow | SelectionType::TableCol) & nSelection )
                m_pWrtShell->SetTableInsertMode( (SelectionType::TableRow & nSelection) ? SwTable::SEARCH_ROW : SwTable::SEARCH_COL );
        }

#if HAVE_FEATURE_DESKTOP
        //When someone needs it, we 'OLE' him something
        AddFormat( SotClipboardFormatId::EMBED_SOURCE );
#endif

        //put RTF ahead of  the OLE's Metafile to have less loss
        if( !m_pWrtShell->GetSelectedObjCount() )
        {
            AddFormat( SotClipboardFormatId::RTF );
#if HAVE_FEATURE_DESKTOP
            AddFormat( SotClipboardFormatId::RICHTEXT );
            AddFormat( SotClipboardFormatId::HTML );
            AddFormat( SotClipboardFormatId::MARKDOWN );
#endif
        }
        if( m_pWrtShell->IsSelection() )
            AddFormat( SotClipboardFormatId::STRING );

        if( nSelection & ( SelectionType::DrawObject | SelectionType::DbForm ))
        {
            AddFormat( SotClipboardFormatId::DRAWING );
            if ( nSelection & SelectionType::DrawObject )
            {
#if HAVE_FEATURE_DESKTOP
                AddFormat( SotClipboardFormatId::GDIMETAFILE );
                AddFormat( SotClipboardFormatId::BITMAP );
#endif
                AddFormat( SotClipboardFormatId::PNG );
            }
            m_eBufferType = static_cast<TransferBufferType>( TransferBufferType::Graphic | m_eBufferType );

            m_oClpGraphic.emplace();
            if( !m_pWrtShell->GetDrawObjGraphic( SotClipboardFormatId::GDIMETAFILE, *m_oClpGraphic ))
                m_pOrigGraphic = &*m_oClpGraphic;
            m_oClpBitmap.emplace();
            if( !m_pWrtShell->GetDrawObjGraphic( SotClipboardFormatId::BITMAP, *m_oClpBitmap ))
                m_pOrigGraphic = &*m_oClpBitmap;

            // is it a URL-Button ?
            OUString sURL;
            OUString sDesc;
            if( m_pWrtShell->GetURLFromButton( sURL, sDesc ) )
            {
                AddFormat( SotClipboardFormatId::STRING );
#if HAVE_FEATURE_DESKTOP
                AddFormat( SotClipboardFormatId::SOLK );
                AddFormat( SotClipboardFormatId::NETSCAPE_BOOKMARK );
                AddFormat( SotClipboardFormatId::FILECONTENT );
                AddFormat( SotClipboardFormatId::FILEGRPDESCRIPTOR );
#endif
                AddFormat( SotClipboardFormatId::UNIFORMRESOURCELOCATOR );
                m_eBufferType = TransferBufferType::InetField | m_eBufferType;
                nRet = 1;
            }
        }

        // at Cut, DDE-Link doesn't make sense!!
        SwDocShell* pDShell;
        if( !bIsCut && bDDELink &&
            nullptr != ( pDShell = m_pWrtShell->GetDoc()->GetDocShell()) &&
            SfxObjectCreateMode::STANDARD == pDShell->GetCreateMode() )
        {
#if HAVE_FEATURE_DESKTOP
            AddFormat( SotClipboardFormatId::LINK );
#endif
            m_xDdeLink = new SwTransferDdeLink( *this, *m_pWrtShell );
        }

        //ObjectDescriptor was already filly from the old DocShell.
        //Now adjust it. Thus in GetData the first query can still
        //be answered with delayed rendering.
        m_aObjDesc.maSize = constOleSize100mm;

        PrepareOLE( m_aObjDesc );
#if HAVE_FEATURE_DESKTOP
        AddFormat( SotClipboardFormatId::OBJECTDESCRIPTOR );
#endif
    }
    else
        nRet = 0;

    if( m_pWrtShell->IsFrameSelected() )
    {
        SfxItemSetFixed<RES_URL, RES_URL> aSet( m_pWrtShell->GetAttrPool() );
        m_pWrtShell->GetFlyFrameAttr( aSet );
        const SwFormatURL& rURL = aSet.Get( RES_URL );
        if( rURL.GetMap() )
        {
            m_pImageMap.reset( new ImageMap( *rURL.GetMap() ) );
            AddFormat( SotClipboardFormatId::SVIM );
        }
        else if( !rURL.GetURL().isEmpty() )
        {
            m_pTargetURL.reset(new INetImage( sGrfNm, rURL.GetURL(),
                                        rURL.GetTargetFrameName() ));
            AddFormat( SotClipboardFormatId::INET_IMAGE );
        }
    }

    return nRet;
}

int SwTransferable::Copy( bool bIsCut, bool bDeleteRedlines )
{
    if (m_pWrtShell->GetView().GetObjectShell()->isContentExtractionLocked())
        return 0;

    int nRet = PrepareForCopy( bIsCut, bDeleteRedlines );
    if ( nRet )
    {
        CopyToClipboard( &m_pWrtShell->GetView().GetEditWin() );
    }

    if( !bIsCut ){
        collectUIInformation(u"COPY"_ustr, u"parameter"_ustr);
    }

    return nRet;
}

void SwTransferable::CalculateAndCopy()
{
    if(!m_pWrtShell)
        return;
    SwWait aWait( *m_pWrtShell->GetView().GetDocShell(), true );

    OUString aStr( m_pWrtShell->Calculate() );

    m_pClpDocFac.reset(new SwDocFac);
    SwDoc& rDoc = lcl_GetDoc(*m_pClpDocFac);
    m_pWrtShell->Copy(rDoc, &aStr);
    m_eBufferType = TransferBufferType::Document;
    AddFormat( SotClipboardFormatId::STRING );

    CopyToClipboard( &m_pWrtShell->GetView().GetEditWin() );
}

bool SwTransferable::CopyGlossary( SwTextBlocks& rGlossary, const OUString& rStr )
{
    if(!m_pWrtShell)
        return false;
    SwWait aWait( *m_pWrtShell->GetView().GetDocShell(), true );

    m_pClpDocFac.reset(new SwDocFac);
    SwDoc& rCDoc = lcl_GetDoc(*m_pClpDocFac);

    SwNodes& rNds = rCDoc.GetNodes();
    SwNodeIndex aNodeIdx( *rNds.GetEndOfContent().StartOfSectionNode() );
    SwContentNode* pCNd = SwNodes::GoNext(&aNodeIdx); // go to 1st ContentNode
    SwPaM aPam( *pCNd );

    rCDoc.getIDocumentFieldsAccess().LockExpFields();   // never update fields - leave text as it is

    rCDoc.InsertGlossary( rGlossary, rStr, aPam );

    // a new one was created in CORE (OLE-Objects copied!)
    m_aDocShellRef = rCDoc.GetTmpDocShell();
    if( m_aDocShellRef.Is() )
        SwTransferable::InitOle( m_aDocShellRef );
    rCDoc.SetTmpDocShell( nullptr );

    m_eBufferType = TransferBufferType::Document;

    //When someone needs it, we 'OLE' her something.
    AddFormat( SotClipboardFormatId::EMBED_SOURCE );
    AddFormat( SotClipboardFormatId::RTF );
    AddFormat( SotClipboardFormatId::RICHTEXT );
    AddFormat( SotClipboardFormatId::HTML );
    AddFormat( SotClipboardFormatId::STRING );
    AddFormat( SotClipboardFormatId::MARKDOWN );

    //ObjectDescriptor was already filled from the old DocShell.
    //Now adjust it. Thus in GetData the first query can still
    //be answered with delayed rendering.
    m_aObjDesc.maSize = constOleSize100mm;

    PrepareOLE( m_aObjDesc );
    AddFormat( SotClipboardFormatId::OBJECTDESCRIPTOR );

    CopyToClipboard( &m_pWrtShell->GetView().GetEditWin() );

    return true;
}

static uno::Reference < XTransferable > * lcl_getTransferPointer ( uno::Reference < XTransferable > &xRef )
{
    return &xRef;
}

SwPasteContext::SwPasteContext(SwWrtShell& rWrtShell)
    : m_rWrtShell(rWrtShell)
{
    remember();
}

void SwPasteContext::remember()
{
    if (m_rWrtShell.GetPasteListeners().getLength() == 0)
        return;

    SwPaM* pCursor = m_rWrtShell.GetCursor();
    if (!pCursor)
        return;

    // Set point to the previous node, so it is not moved.
    const SwNode& rNode = pCursor->GetPoint()->GetNode();
    m_oPaM.emplace(rNode, rNode, SwNodeOffset(0), SwNodeOffset(-1));
    m_nStartContent = pCursor->GetPoint()->GetContentIndex();
}

void SwPasteContext::forget() { m_oPaM.reset(); }

SwPasteContext::~SwPasteContext()
{
    try
    {
        if (m_rWrtShell.GetPasteListeners().getLength() == 0)
            return;

        beans::PropertyValue aPropertyValue;

        switch (m_rWrtShell.GetView().GetShellMode())
        {
            case ShellMode::Graphic:
            {
                SwFrameFormat* pFormat = m_rWrtShell.GetFlyFrameFormat();
                if (!pFormat)
                    return;

                aPropertyValue.Name = "TextGraphicObject";
                aPropertyValue.Value
                    <<= uno::Reference<text::XTextContent>(SwXTextGraphicObject::CreateXTextGraphicObject(pFormat->GetDoc(), pFormat));
                break;
            }

            default:
            {
                if (!m_oPaM)
                    return;

                SwPaM* pCursor = m_rWrtShell.GetCursor();
                if (!pCursor)
                    return;

                if (!pCursor->GetPoint()->GetNode().IsTextNode())
                    // Non-text was pasted.
                    return;

                // Update mark after paste.
                *m_oPaM->GetMark() = *pCursor->GetPoint();

                // Restore point.
                m_oPaM->GetPoint()->Adjust(SwNodeOffset(1));
                SwNode& rNode = m_oPaM->GetPointNode();
                if (!rNode.IsTextNode())
                    // Starting point is no longer text.
                    return;

                m_oPaM->GetPoint()->SetContent(m_nStartContent);

                aPropertyValue.Name = "TextRange";
                const rtl::Reference<SwXTextRange> xTextRange = SwXTextRange::CreateXTextRange(
                    m_oPaM->GetDoc(), *m_oPaM->GetPoint(), m_oPaM->GetMark());
                aPropertyValue.Value <<= uno::Reference<text::XTextRange>(xTextRange);
                break;
            }
        }

        if (aPropertyValue.Name.isEmpty())
            return;

        // Invoke the listeners.
        uno::Sequence<beans::PropertyValue> aEvent{ std::move(aPropertyValue) };
        m_rWrtShell.GetPasteListeners().notifyEach( &css::text::XPasteListener::notifyPasteEvent, aEvent );
    }
    catch (const uno::Exception& rException)
    {
        SAL_WARN("sw",
                 "SwPasteContext::~SwPasteContext: uncaught exception: " << rException.Message);
    }
}

bool SwTransferable::IsPaste( const SwWrtShell& rSh,
                              const TransferableDataHelper& rData )
{
    // Check the common case first: We can always paste our own data!
    // If _only_ the internal format can be pasted, this check will
    // yield 'true', while the one below would give a (wrong) result 'false'.

    bool bIsPaste = ( GetSwTransferable( rData ) != nullptr );

    // if it's not our own data, we need to have a closer look:
    if( ! bIsPaste )
    {
        // determine the proper paste action, and return true if we find one
        uno::Reference<XTransferable> xTransferable( rData.GetXTransferable() );

        SotExchangeDest nDestination = SwTransferable::GetSotDestination( rSh );
        sal_uInt16 nSourceOptions =
                    (( SotExchangeDest::DOC_TEXTFRAME == nDestination ||
                       SotExchangeDest::SWDOC_FREE_AREA == nDestination ||
                       SotExchangeDest::DOC_TEXTFRAME_WEB == nDestination ||
                       SotExchangeDest::SWDOC_FREE_AREA_WEB == nDestination )
                                    ? EXCHG_IN_ACTION_COPY
                     : EXCHG_IN_ACTION_MOVE);

        SotClipboardFormatId nFormat;          // output param for GetExchangeAction
        sal_uInt8 nEventAction;    // output param for GetExchangeAction
        sal_uInt8 nAction = SotExchange::GetExchangeAction(
                                rData.GetDataFlavorExVector(),
                                nDestination,
                                nSourceOptions,             /* ?? */
                                EXCHG_IN_ACTION_DEFAULT,    /* ?? */
                                nFormat, nEventAction, SotClipboardFormatId::NONE,
                                lcl_getTransferPointer ( xTransferable ) );

        // if we find a suitable action, we can paste!
        bIsPaste = (EXCHG_INOUT_ACTION_NONE != nAction);
    }

    return bIsPaste;
}

void SwTransferable::SelectPasteFormat(const TransferableDataHelper& rData, sal_uInt8& nAction,
                                       SotClipboardFormatId& nFormat)
{
    if (nFormat != SotClipboardFormatId::RICHTEXT)
    {
        return;
    }

    if (!rData.HasFormat(SotClipboardFormatId::EMBED_SOURCE))
    {
        return;
    }

    if (!rData.HasFormat(SotClipboardFormatId::OBJECTDESCRIPTOR))
    {
        return;
    }

    TransferableObjectDescriptor aObjDesc;
    if (!rData.GetTransferableObjectDescriptor(SotClipboardFormatId::OBJECTDESCRIPTOR, aObjDesc))
    {
        return;
    }

    if (aObjDesc.maClassName != SvGlobalName(SO3_SW_CLASSID))
    {
        return;
    }

    // At this point we know that we paste from Writer to Writer and the clipboard has the content
    // in both RTF and ODF formats. Prefer ODF in this case.
    nAction = EXCHG_OUT_ACTION_INSERT_OLE;
    nFormat = SotClipboardFormatId::EMBED_SOURCE;
}

// get HTML indentation level by counting tabulator characters before the index
// (also index value -1 returns with 0)
static sal_Int32 lcl_getLevel(std::u16string_view sText, sal_Int32 nIdx)
{
    sal_Int32 nRet = 0;
    while ( nIdx-- > 0 && sText[nIdx] == '\t' )
    {
        nRet++;
    }
    return nRet;
}

bool SwTransferable::Paste(SwWrtShell& rSh, const TransferableDataHelper& rData, RndStdIds nAnchorType, bool bIgnoreComments, PasteTableType ePasteTable)
{
    SwPasteContext aPasteContext(rSh);

    sal_uInt8 nAction=0;
    SotExchangeDest nDestination = SwTransferable::GetSotDestination( rSh );
    SotClipboardFormatId nFormat = SotClipboardFormatId::NONE;
    SotExchangeActionFlags nActionFlags = SotExchangeActionFlags::NONE;
    bool bSingleCellTable = false;

    if( GetSwTransferable( rData ) )
    {
        nAction = EXCHG_OUT_ACTION_INSERT_PRIVATE;
    }
    else
    {
        sal_uInt16 nSourceOptions =
                    (( SotExchangeDest::DOC_TEXTFRAME == nDestination ||
                    SotExchangeDest::SWDOC_FREE_AREA == nDestination ||
                    SotExchangeDest::DOC_TEXTFRAME_WEB == nDestination ||
                    SotExchangeDest::SWDOC_FREE_AREA_WEB == nDestination )
                                    ? EXCHG_IN_ACTION_COPY
                                    : EXCHG_IN_ACTION_MOVE);
        uno::Reference<XTransferable> xTransferable( rData.GetXTransferable() );
        sal_uInt8 nEventAction;
        nAction = SotExchange::GetExchangeAction(
                                    rData.GetDataFlavorExVector(),
                                    nDestination,
                                    nSourceOptions,             /* ?? */
                                    EXCHG_IN_ACTION_DEFAULT,    /* ?? */
                                    nFormat, nEventAction, SotClipboardFormatId::NONE,
                                    lcl_getTransferPointer ( xTransferable ),
                                    &nActionFlags );
    }

    // when HTML is just an image don't generate new section
    if (rData.HasFormat(SotClipboardFormatId::HTML_SIMPLE) && rData.HasFormat(SotClipboardFormatId::HTML_NO_COMMENT)
        && rData.HasFormat(SotClipboardFormatId::BITMAP) && nFormat == SotClipboardFormatId::FILE_LIST)
        nFormat = SotClipboardFormatId::BITMAP;

    // tdf#37223 avoid non-native insertion of Calc worksheets in the following cases:
    // content of 1-cell worksheets are inserted as simple text using RTF format,
    // bigger worksheets within native (Writer) table cells are inserted as native tables,
    // ie. cell by cell instead of embedding the worksheet in a single cell of the Writer table
    if ( EXCHG_IN_ACTION_COPY == nAction && ( rData.HasFormat( SotClipboardFormatId::SYLK ) ||
                  rData.HasFormat( SotClipboardFormatId::SYLK_BIGCAPS ) ) )
    {
        // is it a 1-cell worksheet?
        OUString aExpand;
        if( rData.GetString( SotClipboardFormatId::STRING, aExpand ))
        {
            const sal_Int32 nNewlines{comphelper::string::getTokenCount(aExpand, '\n')};
            const sal_Int32 nRows = nNewlines ? nNewlines-1 : 0;
            if ( nRows == 1 )
            {
                const sal_Int32 nCols = comphelper::string::getTokenCount(o3tl::getToken(aExpand, 0, '\n'), '\t');
                if (nCols == 1)
                    bSingleCellTable = true;
            }
        }

        // convert the worksheet to a temporary native table using HTML format, and copy that into the original native table
        if (!bSingleCellTable && rData.HasFormat( SotClipboardFormatId::HTML ) &&
                        SwDoc::IsInTable(rSh.GetCursor()->GetPointNode()) != nullptr && rSh.DoesUndo())
        {
            SfxDispatcher* pDispatch = rSh.GetView().GetViewFrame().GetDispatcher();
            sal_uInt32 nLevel = 0;

            // within Writer table cells, inserting worksheets using HTML format results only plain text, not a native table,
            // so remove all outer nested tables temporary to get a working insertion point
            // (RTF format has no such problem, but that inserts the hidden rows of the original Calc worksheet, too)

            // For this, switch off change tracking temporarily, if needed
            RedlineFlags eOld = rSh.GetDoc()->getIDocumentRedlineAccess().GetRedlineFlags();
            if ( eOld & RedlineFlags::On )
                rSh.GetDoc()->getIDocumentRedlineAccess().SetRedlineFlags( eOld & ~RedlineFlags::On );

            UIName sPreviousTableName;
            do
            {
                // tdf#152245 add a limit to the loop, if it's not possible to delete the table
                const SwTableNode* pNode = rSh.GetCursor()->GetPointNode().FindTableNode();
                const UIName sTableName = pNode->GetTable().GetFrameFormat()->GetName();
                if ( sTableName == sPreviousTableName )
                    break;
                sPreviousTableName = sTableName;
                // insert a random character to redo the place of the insertion at the end
                pDispatch->Execute(FN_INSERT_NNBSP, SfxCallMode::SYNCHRON);
                pDispatch->Execute(FN_TABLE_DELETE_TABLE, SfxCallMode::SYNCHRON);
                nLevel++;
            } while (SwDoc::IsInTable(rSh.GetCursor()->GetPointNode()) != nullptr);

            // restore change tracking settings
            if ( eOld & RedlineFlags::On )
                rSh.GetDoc()->getIDocumentRedlineAccess().SetRedlineFlags( eOld );

            if ( SwTransferable::PasteData( rData, rSh, EXCHG_OUT_ACTION_INSERT_STRING, nActionFlags, SotClipboardFormatId::HTML,
                                        nDestination, false, false, nullptr, 0, false, nAnchorType, bIgnoreComments, &aPasteContext, ePasteTable) )
            {
                bool bFoundTemporaryTable = false;
                pDispatch->Execute(FN_LINE_UP, SfxCallMode::SYNCHRON);
                if (SwDoc::IsInTable(rSh.GetCursor()->GetPointNode()) != nullptr)
                {
                    bFoundTemporaryTable = true;
                    pDispatch->Execute(FN_TABLE_SELECT_ALL, SfxCallMode::SYNCHRON);
                    pDispatch->Execute(SID_COPY, SfxCallMode::SYNCHRON);
                }
                for(sal_uInt32 a = 0; a < 1 + (nLevel * 2); a++)
                    pDispatch->Execute(SID_UNDO, SfxCallMode::SYNCHRON);
                // clipboard content hasn't changed (limit potential infinite
                // recursion with the same non-native table, as was in tdf#138688)
                if (!bFoundTemporaryTable)
                    return false;
                if (ePasteTable == PasteTableType::PASTE_TABLE)
                    pDispatch->Execute(FN_PASTE_NESTED_TABLE, SfxCallMode::SYNCHRON);
                else if (ePasteTable == PasteTableType::PASTE_ROW)
                    pDispatch->Execute(FN_TABLE_PASTE_ROW_BEFORE, SfxCallMode::SYNCHRON);
                else if (ePasteTable == PasteTableType::PASTE_COLUMN)
                    pDispatch->Execute(FN_TABLE_PASTE_COL_BEFORE, SfxCallMode::SYNCHRON);
                else
                    pDispatch->Execute(SID_PASTE, SfxCallMode::SYNCHRON);
                return true;
            } else {
                for(sal_uInt32 a = 0; a < (nLevel * 2); a++)
                    pDispatch->Execute(SID_UNDO, SfxCallMode::SYNCHRON);
            }
        }
    }
    // insert clipboard content as new table rows/columns before the actual row/column instead of overwriting it
    else if ( (rSh.GetTableInsertMode() != SwTable::SEARCH_NONE || ePasteTable == PasteTableType::PASTE_ROW || ePasteTable == PasteTableType::PASTE_COLUMN) &&
        rData.HasFormat( SotClipboardFormatId::HTML ) &&
        SwDoc::IsInTable(rSh.GetCursor()->GetPointNode()) != nullptr )
    {
        OUString aExpand;
        sal_Int32 nIdx;
        bool bRowMode = rSh.GetTableInsertMode() == SwTable::SEARCH_ROW || ePasteTable == PasteTableType::PASTE_ROW;
        if( rData.GetString( SotClipboardFormatId::HTML, aExpand ) && (nIdx = aExpand.indexOf("<table")) > -1 )
        {
            // calculate table row/column count by analysing indentation of the HTML table extract

            // calculate indentation level of <table>, which is the base of the next calculations
            // (tdf#148791 table alignment can enlarge it using first level <center>, <div> or <dl>)
            sal_Int32 nTableLevel = lcl_getLevel(aExpand, nIdx);
            // table rows repeated heading use extra indentation, too:
            // <thead> is always used here, and the first table with <thead> is not nested,
            // if its indentation level is greater only by 1, than indentation level of the table
            bool bShifted = lcl_getLevel(aExpand, aExpand.indexOf("<thead")) == nTableLevel + 1;
            // calculate count of selected rows or columns
            sal_Int32 nSelectedRowsOrCols = 0;
            const OUString sSearchRowOrCol = bRowMode ? u"</tr>"_ustr : u"<col "_ustr;
            while((nIdx = aExpand.indexOf(sSearchRowOrCol, nIdx)) > -1)
            {
                // skip rows/columns of nested tables, based on HTML indentation
                if ( lcl_getLevel(aExpand, nIdx) == nTableLevel + (bShifted ? 2 : 1) &&
                    // skip also strange hidden empty rows <tr></tr>
                    !aExpand.match("<tr></tr>", nIdx - 4) )
                {
                    ++nSelectedRowsOrCols;
                }
                ++nIdx;
            }
            // are we at the beginning of the cell?
            bool bStartTableBoxNode =
                // first paragraph of the cell?
                rSh.GetCursor()->GetPointNode().GetIndex() == rSh.GetCursor()->GetPointNode().FindTableBoxStartNode()->GetIndex()+1 &&
                // beginning of the paragraph?
                !rSh.GetCursor()->GetPoint()->GetContentIndex();
            SfxDispatcher* pDispatch = rSh.GetView().GetViewFrame().GetDispatcher();

            // go start of the cell
            if (!bStartTableBoxNode)
                pDispatch->Execute(FN_START_OF_DOCUMENT, SfxCallMode::SYNCHRON);

            // store cursor position in row mode
            ::sw::mark::MarkBase* pMark = (!bRowMode || nSelectedRowsOrCols == 0) ? nullptr : rSh.SetBookmark(
                                    vcl::KeyCode(),
                                    SwMarkName(),
                                    IDocumentMarkAccess::MarkType::UNO_BOOKMARK );

            // add a new empty row/column before the actual table row/column and go there
            const sal_uInt16 nDispatchSlot = bRowMode ? FN_TABLE_INSERT_ROW_BEFORE : FN_TABLE_INSERT_COL_BEFORE;
            pDispatch->Execute(nDispatchSlot, SfxCallMode::SYNCHRON);
            pDispatch->Execute(bRowMode ? FN_LINE_UP : FN_CHAR_LEFT, SfxCallMode::SYNCHRON);

            // add the other new empty rows/columns after the actual table row/column
            if ( nSelectedRowsOrCols > 1 )
            {
                SfxInt16Item aCountItem( nDispatchSlot, nSelectedRowsOrCols-1 );
                SfxBoolItem aAfter( FN_PARAM_INSERT_AFTER, true );
                pDispatch->ExecuteList(nDispatchSlot,
                    SfxCallMode::SYNCHRON|SfxCallMode::RECORD,
                    { &aCountItem, &aAfter });
            }

            // paste rows
            bool bResult = SwTransferable::PasteData( rData, rSh, nAction, nActionFlags, nFormat,
                                        nDestination, false, false, nullptr, 0, false, nAnchorType, bIgnoreComments, &aPasteContext );

            // restore cursor position
            if (pMark != nullptr)
            {
                rSh.GotoMark( pMark );
                rSh.getIDocumentMarkAccess()->deleteMark( pMark );
            }

            return bResult;
        }
    }

    // special case for tables from draw application or 1-cell tables
    if( EXCHG_OUT_ACTION_INSERT_DRAWOBJ == nAction || bSingleCellTable )
    {
        if( rData.HasFormat( SotClipboardFormatId::RTF ) )
        {
            nAction = EXCHG_OUT_ACTION_INSERT_STRING;
            nFormat = SotClipboardFormatId::RTF;
        }
        else if( rData.HasFormat( SotClipboardFormatId::RICHTEXT ) )
        {
            nAction = EXCHG_OUT_ACTION_INSERT_STRING;
            nFormat = SotClipboardFormatId::RICHTEXT;
        }
    }

    // Tweak the format if necessary: the source application can be considered in this context,
    // while not in sot/ code.
    SwTransferable::SelectPasteFormat(rData, nAction, nFormat);

    collectUIInformation(u"PASTE"_ustr, u"parameter"_ustr);

    return EXCHG_INOUT_ACTION_NONE != nAction &&
            SwTransferable::PasteData( rData, rSh, nAction, nActionFlags, nFormat,
                                        nDestination, false, false, nullptr, 0, false, nAnchorType, bIgnoreComments, &aPasteContext, ePasteTable);
}

bool SwTransferable::PasteData( const TransferableDataHelper& rData,
                            SwWrtShell& rSh, sal_uInt8 nAction, SotExchangeActionFlags nActionFlags,
                            SotClipboardFormatId nFormat,
                            SotExchangeDest nDestination, bool bIsPasteFormat,
                            [[maybe_unused]] bool bIsDefault,
                            const Point* pPt, sal_Int8 nDropAction,
                            bool bPasteSelection, RndStdIds nAnchorType,
                            bool bIgnoreComments,
                            SwPasteContext* pContext,
                            PasteTableType ePasteTable )
{
    SwWait aWait( *rSh.GetView().GetDocShell(), false );
    std::unique_ptr<SwTrnsfrActionAndUndo, o3tl::default_delete<SwTrnsfrActionAndUndo>> pAction;
    SwModule* pMod = SwModule::get();

    bool bRet = false;
    bool bCallAutoCaption = false;

    if( pPt )
    {
        // external Drop
        if ((bPasteSelection ? !pMod->m_pXSelection : !pMod->m_pDragDrop) &&
                // The following condition is used for tdf#156111 to prevent a selection from being
                // cleared by the default case of the nDestination switch.
                !(rSh.GetCursorCnt() == 1 && rSh.TestCurrPam(*pPt) &&
                nDestination == SotExchangeDest::SWDOC_FREE_AREA &&
                nFormat == SotClipboardFormatId::SONLK))
        {
            switch( nDestination )
            {
            case SotExchangeDest::DOC_LNKD_GRAPH_W_IMAP:
            case SotExchangeDest::DOC_LNKD_GRAPHOBJ:
            case SotExchangeDest::DOC_GRAPH_W_IMAP:
            case SotExchangeDest::DOC_GRAPHOBJ:
            case SotExchangeDest::DOC_OLEOBJ:
            case SotExchangeDest::DOC_DRAWOBJ:
            case SotExchangeDest::DOC_URLBUTTON:
            case SotExchangeDest::DOC_GROUPOBJ:
                // select frames/objects
                SwTransferable::SetSelInShell( rSh, true, pPt );
                break;

            default:
                bool bLockView = rSh.IsViewLocked();
                if (nFormat == SotClipboardFormatId::SONLK)
                    rSh.LockView(true); // prevent view jump
                SwTransferable::SetSelInShell( rSh, false, pPt );
                rSh.LockView(bLockView);
                break;
            }
        }
    }
    else if( ( !GetSwTransferable( rData ) || bIsPasteFormat ) &&
            !rSh.IsTableMode() && rSh.HasSelection() )
    {
        // then delete the selections

        //don't delete selected content
        // - at table-selection
        // - at ReRead of a graphic/DDEData
        // - at D&D, for the right selection was taken care of
        //      in Drop-Handler
        bool bDelSel = false;
        switch( nDestination )
        {
        case SotExchangeDest::DOC_TEXTFRAME:
        case SotExchangeDest::SWDOC_FREE_AREA:
        case SotExchangeDest::DOC_TEXTFRAME_WEB:
        case SotExchangeDest::SWDOC_FREE_AREA_WEB:
            bDelSel = true;
            break;
        default:
            break;
        }

        if( bDelSel )
            // #i34830#
            pAction.reset(new SwTrnsfrActionAndUndo(&rSh, true, pContext));
    }

    SwTransferable *pTrans=nullptr, *pTunneledTrans=GetSwTransferable( rData );

    // check for private drop
    bool bPrivateDrop(pPt);
    if (bPrivateDrop)
    {
        if (bPasteSelection)
            pTrans = pMod->m_pXSelection;
        else
            pTrans = pMod->m_pDragDrop;
        bPrivateDrop = nullptr != pTrans;
    }
    bool bNeedToSelectBeforePaste(false);

    if(bPrivateDrop && DND_ACTION_LINK == nDropAction)
    {
        // internal drop on object, suppress bPrivateDrop to change internal fill
        bPrivateDrop = false;
        bNeedToSelectBeforePaste = true;
    }

    if(bPrivateDrop && pPt && DND_ACTION_MOVE == nDropAction)
    {
        // check if dragged over a useful target. If yes, use as content exchange
        // drop as if from external
        const SwFrameFormat* pSwFrameFormat = rSh.GetFormatFromObj(*pPt);

        if(dynamic_cast< const SwDrawFrameFormat* >(pSwFrameFormat))
        {
            bPrivateDrop = false;
            bNeedToSelectBeforePaste = true;
        }
    }

    if(bPrivateDrop)
    {
        // then internal Drag & Drop or XSelection
        bRet = pTrans->PrivateDrop( rSh, *pPt, DND_ACTION_MOVE == nDropAction,
                                    bPasteSelection );
    }
    else if( !pPt && pTunneledTrans &&
            EXCHG_OUT_ACTION_INSERT_PRIVATE == nAction )
    {
        // then internal paste
        bRet = pTunneledTrans->PrivatePaste(rSh, pContext, ePasteTable);
    }
    else if( EXCHG_INOUT_ACTION_NONE != nAction )
    {
        if( !pAction )
        {
            pAction.reset(new SwTrnsfrActionAndUndo( &rSh ));
        }

        // in Drag&Drop MessageBoxes must not be showed
        bool bMsg = nullptr == pPt;

        // delete selections

        switch( nAction )
        {
        case EXCHG_OUT_ACTION_INSERT_PRIVATE:
            OSL_ENSURE( pPt, "EXCHG_OUT_ACTION_INSERT_PRIVATE: what should happen here?" );
            break;

        case EXCHG_OUT_ACTION_MOVE_PRIVATE:
            OSL_ENSURE( pPt, "EXCHG_OUT_ACTION_MOVE_PRIVATE: what should happen here?" );
            break;

        case EXCHG_IN_ACTION_MOVE:
        case EXCHG_IN_ACTION_COPY:
        case EXCHG_IN_ACTION_LINK:
        case EXCHG_OUT_ACTION_INSERT_HTML:
        case EXCHG_OUT_ACTION_INSERT_STRING:
        case EXCHG_OUT_ACTION_INSERT_IMAGEMAP:
        case EXCHG_OUT_ACTION_REPLACE_IMAGEMAP:

            // then we have to use the format
            switch( nFormat )
            {
            case SotClipboardFormatId::DRAWING:
                bRet = SwTransferable::PasteSdrFormat( rData, rSh,
                                                SwPasteSdr::Insert, pPt,
                                                nActionFlags, bNeedToSelectBeforePaste);
                break;

            case SotClipboardFormatId::HTML:
            case SotClipboardFormatId::HTML_SIMPLE:
            case SotClipboardFormatId::HTML_NO_COMMENT:
            case SotClipboardFormatId::RTF:
            case SotClipboardFormatId::RICHTEXT:
            case SotClipboardFormatId::STRING:
            case SotClipboardFormatId::MARKDOWN:
                bRet = SwTransferable::PasteFileContent( rData, rSh,
                                                            nFormat, bMsg, bIgnoreComments );
                break;

            case SotClipboardFormatId::NETSCAPE_BOOKMARK:
                {
                    INetBookmark aBkmk;
                    if( rData.GetINetBookmark( nFormat, aBkmk ) )
                    {
                        SwFormatINetFormat aFormat( aBkmk.GetURL(), OUString() );
                        rSh.InsertURL( aFormat, aBkmk.GetDescription() );
                        bRet = true;
                    }
                }
                break;

            case SotClipboardFormatId::SD_OLE:
                bRet = SwTransferable::PasteOLE( rData, rSh, nFormat,
                                                    nActionFlags, bMsg );
                break;

            case SotClipboardFormatId::SVIM:
                bRet = SwTransferable::PasteImageMap( rData, rSh );
                break;

            case SotClipboardFormatId::SVXB:
            case SotClipboardFormatId::BITMAP:
            case SotClipboardFormatId::PNG:
            case SotClipboardFormatId::GDIMETAFILE:
                bRet = SwTransferable::PasteGrf( rData, rSh, nFormat,
                                                SwPasteSdr::Insert,pPt,
                                                nActionFlags, nDropAction, bNeedToSelectBeforePaste);
                break;

            case SotClipboardFormatId::XFORMS:
            case SotClipboardFormatId::SBA_FIELDDATAEXCHANGE:
            case SotClipboardFormatId::SBA_DATAEXCHANGE:
            case SotClipboardFormatId::SBA_CTRLDATAEXCHANGE:
                bRet = SwTransferable::PasteDBData( rData, rSh, nFormat,
                                            EXCHG_IN_ACTION_LINK == nAction,
                                            pPt, bMsg );
                break;

            case SotClipboardFormatId::SIMPLE_FILE:
                bRet = SwTransferable::PasteFileName( rData, rSh, nFormat,
                                ( EXCHG_IN_ACTION_MOVE == nAction
                                    ? SwPasteSdr::Replace
                                    : EXCHG_IN_ACTION_LINK == nAction
                                        ? SwPasteSdr::SetAttr
                                        : SwPasteSdr::Insert),
                                pPt, nActionFlags, nullptr );
                break;

            case SotClipboardFormatId::FILE_LIST:
                // then insert as graphics only
                bRet = SwTransferable::PasteFileList( rData, rSh,
                                    EXCHG_IN_ACTION_LINK == nAction,
                                    pPt, bMsg );
                break;

            case SotClipboardFormatId::SONLK:
                if( pPt )
                {
                    NaviContentBookmark aBkmk;
                    if (aBkmk.Paste(rData, rSh.GetSelText()))
                    {
                        aWait.~SwWait(); // end the wait pointer, X11 only annoyance
                        rSh.NavigatorPaste(aBkmk);
                        bRet = true;
                    }
                }
                break;

            case SotClipboardFormatId::INET_IMAGE:
            case SotClipboardFormatId::NETSCAPE_IMAGE:
                bRet = SwTransferable::PasteTargetURL( rData, rSh,
                                                        SwPasteSdr::Insert,
                                                        pPt, true );
                break;

            default:
                OSL_ENSURE( pPt, "unknown format" );
            }
            break;

        case EXCHG_OUT_ACTION_INSERT_FILE:
            {
                bool graphicInserted;
                bRet = SwTransferable::PasteFileName( rData, rSh, nFormat,
                                            SwPasteSdr::Insert, pPt,
                                            nActionFlags,
                                            &graphicInserted );
                if( graphicInserted )
                    bCallAutoCaption = true;
            }
            break;

        case EXCHG_OUT_ACTION_INSERT_OLE:
            bRet = SwTransferable::PasteOLE( rData, rSh, nFormat,
                                                nActionFlags,bMsg );
            break;

        case EXCHG_OUT_ACTION_INSERT_DDE:
            {
                bool bReRead = 0 != CNT_HasGrf( rSh.GetCntType() );
                bRet = SwTransferable::PasteDDE( rData, rSh, bReRead, bMsg );
            }
            break;

        case EXCHG_OUT_ACTION_INSERT_HYPERLINK:
            {
                OUString sURL, sDesc;
                if( SotClipboardFormatId::SIMPLE_FILE == nFormat )
                {
                    if( rData.GetString( nFormat, sURL ) && !sURL.isEmpty() )
                    {
                        SwTransferable::CheckForURLOrLNKFile( rData, sURL, &sDesc );
                        if( sDesc.isEmpty() )
                            sDesc = sURL;
                        bRet = true;
                    }
                }
                else
                {
                    INetBookmark aBkmk;
                    if( rData.GetINetBookmark( nFormat, aBkmk ) )
                    {
                        sURL = aBkmk.GetURL();
                        sDesc = aBkmk.GetDescription();
                        bRet = true;
                    }
                }

                if( bRet )
                {
                    SwFormatINetFormat aFormat( sURL, OUString() );
                    rSh.InsertURL( aFormat, sDesc );
                }
            }
            break;

        case EXCHG_OUT_ACTION_GET_ATTRIBUTES:
            switch( nFormat )
            {
            case SotClipboardFormatId::DRAWING:
                bRet = SwTransferable::PasteSdrFormat( rData, rSh,
                                                SwPasteSdr::SetAttr, pPt,
                                                nActionFlags, bNeedToSelectBeforePaste);
                break;
            case SotClipboardFormatId::SVXB:
            case SotClipboardFormatId::GDIMETAFILE:
            case SotClipboardFormatId::BITMAP:
            case SotClipboardFormatId::PNG:
            case SotClipboardFormatId::NETSCAPE_BOOKMARK:
            case SotClipboardFormatId::SIMPLE_FILE:
            case SotClipboardFormatId::FILEGRPDESCRIPTOR:
            case SotClipboardFormatId::UNIFORMRESOURCELOCATOR:
                bRet = SwTransferable::PasteGrf( rData, rSh, nFormat,
                                                SwPasteSdr::SetAttr, pPt,
                                                nActionFlags, nDropAction, bNeedToSelectBeforePaste);
                break;
            default:
                OSL_FAIL( "unknown format" );
            }

            break;

        case EXCHG_OUT_ACTION_INSERT_DRAWOBJ:
            bRet = SwTransferable::PasteSdrFormat( rData, rSh,
                                                SwPasteSdr::Insert, pPt,
                                                nActionFlags, bNeedToSelectBeforePaste);
            break;
        case EXCHG_OUT_ACTION_INSERT_SVXB:
        case EXCHG_OUT_ACTION_INSERT_GDIMETAFILE:
        case EXCHG_OUT_ACTION_INSERT_BITMAP:
        case EXCHG_OUT_ACTION_INSERT_GRAPH:
            bRet = SwTransferable::PasteGrf( rData, rSh, nFormat,
                                                SwPasteSdr::Insert, pPt,
                                                nActionFlags, nDropAction, bNeedToSelectBeforePaste, nAnchorType );
            break;

        case EXCHG_OUT_ACTION_REPLACE_DRAWOBJ:
            bRet = SwTransferable::PasteSdrFormat( rData, rSh,
                                                SwPasteSdr::Replace, pPt,
                                                nActionFlags, bNeedToSelectBeforePaste);
            break;

        case EXCHG_OUT_ACTION_REPLACE_SVXB:
        case EXCHG_OUT_ACTION_REPLACE_GDIMETAFILE:
        case EXCHG_OUT_ACTION_REPLACE_BITMAP:
        case EXCHG_OUT_ACTION_REPLACE_GRAPH:
            bRet = SwTransferable::PasteGrf( rData, rSh, nFormat,
                                                SwPasteSdr::Replace,pPt,
                                                nActionFlags, nDropAction, bNeedToSelectBeforePaste);
            break;

        case EXCHG_OUT_ACTION_INSERT_INTERACTIVE:
            bRet = SwTransferable::PasteAsHyperlink( rData, rSh, nFormat );
            break;

        default:
            OSL_FAIL("unknown action" );
        }
    }

    if( !bPasteSelection && rSh.IsFrameSelected() )
    {
        rSh.EnterSelFrameMode();
        //force ::SelectShell
        rSh.GetView().StopShellTimer();
    }

    pAction.reset();
    if( bCallAutoCaption )
        rSh.GetView().AutoCaption( GRAPHIC_CAP );

    return bRet;
}

SotExchangeDest SwTransferable::GetSotDestination( const SwWrtShell& rSh )
{
    SotExchangeDest nRet = SotExchangeDest::NONE;

    ObjCntType eOType = rSh.GetObjCntTypeOfSelection();

    switch( eOType )
    {
    case OBJCNT_GRF:
        {
            bool bIMap, bLink;
            bIMap = nullptr != rSh.GetFlyFrameFormat()->GetURL().GetMap();
            OUString aDummy;
            rSh.GetGrfNms( &aDummy, nullptr );
            bLink = !aDummy.isEmpty();

            if( bLink && bIMap )
                nRet = SotExchangeDest::DOC_LNKD_GRAPH_W_IMAP;
            else if( bLink )
                nRet = SotExchangeDest::DOC_LNKD_GRAPHOBJ;
            else if( bIMap )
                nRet = SotExchangeDest::DOC_GRAPH_W_IMAP;
            else
                nRet = SotExchangeDest::DOC_GRAPHOBJ;
        }
        break;

    case OBJCNT_FLY:
        if( dynamic_cast< const SwWebDocShell *>( rSh.GetView().GetDocShell() ) != nullptr  )
            nRet = SotExchangeDest::DOC_TEXTFRAME_WEB;
        else
            nRet = SotExchangeDest::DOC_TEXTFRAME;
        break;
    case OBJCNT_OLE:        nRet = SotExchangeDest::DOC_OLEOBJ;       break;

    case OBJCNT_CONTROL:    /* no Action avail */
    case OBJCNT_SIMPLE:     nRet = SotExchangeDest::DOC_DRAWOBJ;      break;
    case OBJCNT_URLBUTTON:  nRet = SotExchangeDest::DOC_URLBUTTON;    break;
    case OBJCNT_GROUPOBJ:   nRet = SotExchangeDest::DOC_GROUPOBJ;     break;

    // what do we do at multiple selections???
    default:
        {
            if( dynamic_cast< const SwWebDocShell *>( rSh.GetView().GetDocShell() ) != nullptr  )
                nRet = SotExchangeDest::SWDOC_FREE_AREA_WEB;
            else
                nRet = SotExchangeDest::SWDOC_FREE_AREA;
        }
    }

    return nRet;
}

namespace
{
bool CanSkipInvalidateNumRules(const SwPosition& rInsertPosition)
{
    SwTextNode* pTextNode = rInsertPosition.GetNode().GetTextNode();
    if (!pTextNode)
    {
        return false;
    }

    const SwNodeNum* pNum = pTextNode->GetNum();
    if (pNum)
    {
        SwNumRule* pNumRule = pNum->GetNumRule();
        if (pNumRule)
        {
            const SvxNumberType rType = pNumRule->Get(pTextNode->GetActualListLevel());
            if (rType.GetNumberingType() == SVX_NUM_CHAR_SPECIAL)
            {
                // Bullet list, skip invalidation.
                return true;
            }
        }

        // Numbered list, invalidate.
        return false;
    }

    // Not a list, skip invalidation.
    return true;
}
}

bool SwTransferable::PasteFileContent( const TransferableDataHelper& rData,
                                    SwWrtShell& rSh, SotClipboardFormatId nFormat, bool bMsg, bool bIgnoreComments )
{
    bool bRet = false;

    MSE40HTMLClipFormatObj aMSE40ClpObj;

    std::unique_ptr<SvStream> xStrm;
    SvStream* pStream = nullptr;
    Reader* pRead = nullptr;
    OUString sData;
    bool bSkipInvalidateNumRules = false;
    switch( nFormat )
    {
    case SotClipboardFormatId::STRING:
        {
            pRead = ReadAscii;

            const SwPosition& rInsertPosition = *rSh.GetCursor()->Start();
            if (CanSkipInvalidateNumRules(rInsertPosition))
            {
                // Insertion point is not a numbering and we paste plain text: then no need to
                // invalidate all numberings.
                bSkipInvalidateNumRules = true;
            }

            if( rData.GetString( nFormat, sData ) )
            {
                pStream = new SvMemoryStream( const_cast<sal_Unicode *>(sData.getStr()),
                            sData.getLength() * sizeof( sal_Unicode ),
                            StreamMode::READ );
                pStream->ResetEndianSwap();

                SwAsciiOptions aAOpt;
                aAOpt.SetCharSet( RTL_TEXTENCODING_UCS2 );
                pRead->GetReaderOpt().SetASCIIOpts( aAOpt );
                break;
            }
        }
        [[fallthrough]]; // because then test if we get a stream

    default:
        if( (xStrm = rData.GetSotStorageStream( nFormat )) )
        {
            if( ( SotClipboardFormatId::HTML_SIMPLE == nFormat ) ||
                ( SotClipboardFormatId::HTML_NO_COMMENT == nFormat ) )
            {
                pStream = aMSE40ClpObj.IsValid( *xStrm );
                pRead = ReadHTML;
                pRead->SetReadUTF8( true );

                bool bNoComments =
                    ( nFormat == SotClipboardFormatId::HTML_NO_COMMENT );
                pRead->SetIgnoreHTMLComments( bNoComments );
            }
            else
            {
                pStream = xStrm.get();
                if( SotClipboardFormatId::RTF == nFormat || SotClipboardFormatId::RICHTEXT == nFormat)
                    pRead = SwReaderWriter::GetRtfReader();
                else if( SotClipboardFormatId::MARKDOWN == nFormat )
                {
                    pRead = ReadMarkdown;
                }
                else if( !pRead )
                {
                    pRead = ReadHTML;
                    pRead->SetReadUTF8( true );
                }
            }
        }
        break;
    }

    TranslateId pResId;
    if( pStream && pRead )
    {
        Link<LinkParamNone*,void> aOldLink( rSh.GetChgLnk() );
        rSh.SetChgLnk( Link<LinkParamNone*,void>() );

        const SwPosition& rInsPos = *rSh.GetCursor()->Start();
        SwReader aReader(*pStream, OUString(), OUString(), *rSh.GetCursor());
        rSh.SaveTableBoxContent( &rInsPos );

        if (bIgnoreComments)
            pRead->SetIgnoreHTMLComments(true);
        if (bSkipInvalidateNumRules)
        {
            aReader.SetSkipInvalidateNumRules(bSkipInvalidateNumRules);
        }

        if( aReader.Read( *pRead ).IsError() )
            pResId = STR_ERROR_CLPBRD_READ;
        else
        {
            pResId = TranslateId();
            bRet = true;
        }

        rSh.SetChgLnk( aOldLink );
        if( bRet )
            rSh.CallChgLnk();
    }
    else
        pResId = STR_CLPBRD_FORMAT_ERROR;

    // Exist a SvMemoryStream? (data in the OUString and xStrm is empty)
    if( pStream && !xStrm )
        delete pStream;

    if (bMsg && pResId)
    {
        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(nullptr,
                                                  VclMessageType::Info, VclButtonsType::Ok,
                                                  SwResId(pResId)));
        xBox->run();
    }
    return bRet;
}

bool SwTransferable::PasteOLE( const TransferableDataHelper& rData, SwWrtShell& rSh,
                                SotClipboardFormatId nFormat, SotExchangeActionFlags nActionFlags, bool bMsg )
{
    bool bRet = false;
    TransferableObjectDescriptor aObjDesc;
    uno::Reference < io::XInputStream > xStrm;
    uno::Reference < embed::XStorage > xStore;
    Reader* pRead = nullptr;

    // Get the preferred format
    SotClipboardFormatId nId;
    if( rData.HasFormat( SotClipboardFormatId::EMBEDDED_OBJ ) )
        nId = SotClipboardFormatId::EMBEDDED_OBJ;
    else if( rData.HasFormat( SotClipboardFormatId::EMBED_SOURCE ) &&
             rData.HasFormat( SotClipboardFormatId::OBJECTDESCRIPTOR ))
        nId = SotClipboardFormatId::EMBED_SOURCE;
    else
        nId = SotClipboardFormatId::NONE;

    if (nId != SotClipboardFormatId::NONE)
    {
        SwDocShell* pDocSh = rSh.GetDoc()->GetDocShell();
        xStrm = rData.GetInputStream(nId, SfxObjectShell::CreateShellID(pDocSh));
    }

    if (xStrm.is())
    {
        // if there is an embedded object, first try if it's a writer object
        // this will be inserted into the document by using a Reader
        try
        {
            xStore = comphelper::OStorageHelper::GetStorageFromInputStream( xStrm );
            switch( SotStorage::GetFormatID( xStore ) )
            {
                case SotClipboardFormatId::STARWRITER_60:
                case SotClipboardFormatId::STARWRITERWEB_60:
                case SotClipboardFormatId::STARWRITERGLOB_60:
                case SotClipboardFormatId::STARWRITER_8:
                case SotClipboardFormatId::STARWRITERWEB_8:
                case SotClipboardFormatId::STARWRITERGLOB_8:
                    pRead = ReadXML;
                    break;
                default:
                    try
                    {
                        xStore->dispose();
                        xStore = nullptr;
                    }
                    catch (const uno::Exception&)
                    {
                    }

                    break;
            }
        }
        catch (const uno::Exception&)
        {
            // it wasn't a storage, but maybe it's a useful stream
        }
    }

    if( pRead )
    {
        SwPaM &rPAM = *rSh.GetCursor();
        SwReader aReader(xStore, OUString(), rPAM);
        aReader.SetInPaste(true);
        if( ! aReader.Read( *pRead ).IsError() )
            bRet = true;
        else if( bMsg )
        {
            std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(nullptr,
                                                      VclMessageType::Info, VclButtonsType::Ok,
                                                      SwResId(STR_ERROR_CLPBRD_READ)));
            xBox->run();
        }
    }
    else
    {
        // temporary storage until the object is inserted
        uno::Reference< embed::XStorage > xTmpStor;
        uno::Reference < embed::XEmbeddedObject > xObj;
        OUString aName;
        comphelper::EmbeddedObjectContainer aCnt;

        if ( xStrm.is() )
        {
            if ( !rData.GetTransferableObjectDescriptor( SotClipboardFormatId::OBJECTDESCRIPTOR, aObjDesc ) )
            {
                OSL_ENSURE( !xStrm.is(), "An object without descriptor in clipboard!");
            }
        }
        else
        {
            if( rData.HasFormat( SotClipboardFormatId::OBJECTDESCRIPTOR_OLE ) && rData.GetTransferableObjectDescriptor( nFormat, aObjDesc ) )
            {
                xStrm = rData.GetInputStream(SotClipboardFormatId::EMBED_SOURCE_OLE, OUString());
                if (!xStrm.is())
                    xStrm = rData.GetInputStream(SotClipboardFormatId::EMBEDDED_OBJ_OLE, OUString());

                if ( !xStrm.is() )
                {
                    // This is MSOLE object that should be created by direct using of system clipboard
                    try
                    {
                        xTmpStor = ::comphelper::OStorageHelper::GetTemporaryStorage();
                        uno::Reference < embed::XEmbedObjectClipboardCreator > xClipboardCreator =
                            embed::MSOLEObjectSystemCreator::create( ::comphelper::getProcessComponentContext() );

                        embed::InsertedObjectInfo aInfo = xClipboardCreator->createInstanceInitFromClipboard(
                                                            xTmpStor,
                                                            u"DummyName"_ustr,
                                                            uno::Sequence< beans::PropertyValue >() );

                        // TODO/LATER: in future InsertedObjectInfo will be used to get container related information
                        // for example whether the object should be an iconified one
                        xObj = aInfo.Object;
                    }
                    catch (const uno::Exception&)
                    {
                    }
                }
            }
            else if (rData.HasFormat(SotClipboardFormatId::SIMPLE_FILE))
            {
                OUString sFile;
                if (rData.GetString(nFormat, sFile) && !sFile.isEmpty())
                {
                    // Copied from sd::View::DropInsertFileHdl
                    uno::Sequence< beans::PropertyValue > aMedium{ comphelper::makePropertyValue(
                        u"URL"_ustr, sFile) };
                    SwDocShell* pDocSh = rSh.GetDoc()->GetDocShell();
                    xObj = pDocSh->GetEmbeddedObjectContainer().InsertEmbeddedObject(aMedium, aName);
                }
            }
        }

        if ( xStrm.is() && !xObj.is() )
            xObj = aCnt.InsertEmbeddedObject( xStrm, aName );

        if( xObj.is() )
        {
            svt::EmbeddedObjectRef xObjRef( xObj, aObjDesc.mnViewAspect );

            // try to get the replacement image from the clipboard
            Graphic aGraphic;
            SotClipboardFormatId nGrFormat = SotClipboardFormatId::NONE;

            // limit the size of the preview metafile to 100000 actions
            GDIMetaFile aMetafile;
            if (rData.GetGDIMetaFile(SotClipboardFormatId::GDIMETAFILE, aMetafile, 100000))
            {
                nGrFormat = SotClipboardFormatId::GDIMETAFILE;
                aGraphic = aMetafile;
            }

            // insert replacement image ( if there is one ) into the object helper
            if ( nGrFormat != SotClipboardFormatId::NONE )
            {
                DataFlavor aDataFlavor;
                SotExchange::GetFormatDataFlavor( nGrFormat, aDataFlavor );
                xObjRef.SetGraphic( aGraphic, aDataFlavor.MimeType );
            }
            else if ( aObjDesc.mnViewAspect == embed::Aspects::MSOLE_ICON )
            {
                // it is important to have an icon, let an empty graphic be used
                // if no other graphic is provided
                // TODO/LATER: in future a default bitmap could be used
                MapMode aMapMode( MapUnit::Map100thMM );
                aGraphic.SetPrefSize( Size( 2500, 2500 ) );
                aGraphic.SetPrefMapMode( aMapMode );
                xObjRef.SetGraphic( aGraphic, OUString() );
            }

            //set size. This is a hack because of handing over, size should be
            //passed to the InsertOle!!!!!!!!!!
            Size aSize;
            if ( aObjDesc.mnViewAspect == embed::Aspects::MSOLE_ICON )
            {
                if( aObjDesc.maSize.Width() && aObjDesc.maSize.Height() )
                    aSize = aObjDesc.maSize;
                else
                {
                    MapMode aMapMode( MapUnit::Map100thMM );
                    aSize = xObjRef.GetSize( &aMapMode );
                }
            }
            else if( aObjDesc.maSize.Width() && aObjDesc.maSize.Height() )
            {
                aSize = aObjDesc.maSize;    //always 100TH_MM
                MapUnit aUnit = VCLUnoHelper::UnoEmbed2VCLMapUnit( xObj->getMapUnit( aObjDesc.mnViewAspect ) );
                aSize = OutputDevice::LogicToLogic(aSize, MapMode(MapUnit::Map100thMM), MapMode(aUnit));
                awt::Size aSz;
                try
                {
                    aSz = xObj->getVisualAreaSize( aObjDesc.mnViewAspect );
                }
                catch (const embed::NoVisualAreaSizeException&)
                {
                    // in this case the provided size is used
                }

                if ( aSz.Width != aSize.Width() || aSz.Height != aSize.Height() )
                {
                    aSz.Width = aSize.Width();
                    aSz.Height = aSize.Height();
                    xObj->setVisualAreaSize( aObjDesc.mnViewAspect, aSz );
                }
            }
            else
            {
                // the descriptor contains the wrong object size
                // the following call will let the MSOLE objects cache the size if it is possible
                // it should be done while the object is running
                try
                {
                    xObj->getVisualAreaSize( aObjDesc.mnViewAspect );
                }
                catch (const uno::Exception&)
                {
                }
            }
            //End of Hack!

            rSh.InsertOleObject( xObjRef );
            bRet = true;

            if( bRet && ( nActionFlags & SotExchangeActionFlags::InsertTargetUrl) )
                SwTransferable::PasteTargetURL( rData, rSh, SwPasteSdr::NONE, nullptr, false );

            // let the object be unloaded if possible
            SwOLEObj::UnloadObject( xObj, rSh.GetDoc(), embed::Aspects::MSOLE_CONTENT );
        }
    }
    return bRet;
}

bool SwTransferable::PasteTargetURL( const TransferableDataHelper& rData,
                                    SwWrtShell& rSh, SwPasteSdr nAction,
                                    const Point* pPt, bool bInsertGRF )
{
    bool bRet = false;
    INetImage aINetImg;
    if( ( rData.HasFormat( SotClipboardFormatId::INET_IMAGE ) &&
          rData.GetINetImage( SotClipboardFormatId::INET_IMAGE, aINetImg )) ||
        ( rData.HasFormat( SotClipboardFormatId::NETSCAPE_IMAGE ) &&
          rData.GetINetImage( SotClipboardFormatId::NETSCAPE_IMAGE, aINetImg )) )
    {
        if( !aINetImg.GetImageURL().isEmpty() && bInsertGRF )
        {
            OUString sURL( aINetImg.GetImageURL() );
            SwTransferable::CheckForURLOrLNKFile( rData, sURL );

            //!!! check at FileSystem - only then it makes sense to test graphics !!!
            Graphic aGraphic;
            GraphicFilter &rFlt = GraphicFilter::GetGraphicFilter();
            bRet = ERRCODE_NONE == GraphicFilter::LoadGraphic(sURL, OUString(), aGraphic, &rFlt);

            if( bRet )
            {
                //Check and Perform rotation if needed
                lclCheckAndPerformRotation(aGraphic);

                switch( nAction )
                {
                case SwPasteSdr::Insert:
                    SwTransferable::SetSelInShell( rSh, false, pPt );
                    rSh.InsertGraphic(sURL, OUString(), aGraphic);
                    break;

                case SwPasteSdr::Replace:
                    if( rSh.GetSelectedObjCount() )
                    {
                        rSh.ReplaceSdrObj( sURL, &aGraphic );
                        Point aPt( pPt ? *pPt : rSh.GetCursorDocPos() );
                        SwTransferable::SetSelInShell( rSh, true, &aPt );
                    }
                    else
                        rSh.ReRead(sURL, OUString(), &aGraphic);
                    break;

                case SwPasteSdr::SetAttr:
                    if( rSh.GetSelectedObjCount() )
                        rSh.Paste( aGraphic, OUString() );
                    else if( OBJCNT_GRF == rSh.GetObjCntTypeOfSelection() )
                        rSh.ReRead(sURL, OUString(), &aGraphic);
                    else
                    {
                        SwTransferable::SetSelInShell( rSh, false, pPt );
                        rSh.InsertGraphic(sURL, OUString(), aGraphic);
                    }
                    break;
                default:
                    bRet = false;
                }
            }
        }
        else
            bRet = true;
    }

    if( bRet )
    {
        SfxItemSetFixed<RES_URL, RES_URL> aSet( rSh.GetAttrPool() );
        rSh.GetFlyFrameAttr( aSet );
        SwFormatURL aURL( aSet.Get( RES_URL ) );

        if( aURL.GetURL() != aINetImg.GetTargetURL() ||
            aURL.GetTargetFrameName() != aINetImg.GetTargetFrame() )
        {
            aURL.SetURL( aINetImg.GetTargetURL(), false );
            aURL.SetTargetFrameName( aINetImg.GetTargetFrame() );
            aSet.Put( aURL );
            rSh.SetFlyFrameAttr( aSet );
        }
    }
    return bRet;
}

void SwTransferable::SetSelInShell( SwWrtShell& rSh, bool bSelectFrame,
                                        const Point* pPt )
{
    if( bSelectFrame )
    {
        // select frames/objects
        if( pPt && !rSh.GetView().GetViewFrame().GetDispatcher()->IsLocked() )
        {
            rSh.GetView().NoRotate();
            if( rSh.SelectObj( *pPt ))
            {
                rSh.HideCursor();
                rSh.EnterSelFrameMode( pPt );
                g_bFrameDrag = true;
            }
        }
    }
    else
    {
        if( rSh.IsFrameSelected() || rSh.GetSelectedObjCount() )
        {
            rSh.UnSelectFrame();
            rSh.LeaveSelFrameMode();
            rSh.GetView().GetEditWin().StopInsFrame();
            g_bFrameDrag = false;
        }
        else if( rSh.GetView().GetDrawFuncPtr() )
            rSh.GetView().GetEditWin().StopInsFrame();

        rSh.EnterStdMode();
        if( pPt )
            rSh.SwCursorShell::SetCursor( *pPt, true );
    }
}

bool SwTransferable::PasteDDE( const TransferableDataHelper& rData,
                                SwWrtShell& rWrtShell, bool bReReadGrf,
                                bool bMsg )
{
    // data from Clipboardformat
    OUString aApp, aTopic, aItem;

    if (!rData.ReadDDELink(aApp, aTopic, aItem, o3tl::temporary(OUString())))
    {
        return false;
    }   // report useful error!!

    OUString aCmd;
    sfx2::MakeLnkName( aCmd, &aApp, aTopic, aItem );

    // do we want to read in a graphic now?
    SotClipboardFormatId nFormat;
    if( !rData.HasFormat( SotClipboardFormatId::RTF ) &&
        !rData.HasFormat( SotClipboardFormatId::RICHTEXT ) &&
        !rData.HasFormat( SotClipboardFormatId::HTML ) &&
        !rData.HasFormat( SotClipboardFormatId::STRING ) &&
        !rData.HasFormat( SotClipboardFormatId::MARKDOWN ) &&
        (rData.HasFormat( nFormat = SotClipboardFormatId::GDIMETAFILE ) ||
         rData.HasFormat( nFormat = SotClipboardFormatId::BITMAP )) )
    {
        Graphic aGrf;
        bool bRet = rData.GetGraphic( nFormat, aGrf );
        if( bRet )
        {
            OUString sLnkTyp(u"DDE"_ustr);
            if ( bReReadGrf )
                rWrtShell.ReRead( aCmd, sLnkTyp, &aGrf );
            else
                rWrtShell.InsertGraphic( aCmd, sLnkTyp, aGrf );
        }
        return bRet;
    }

    SwFieldType* pTyp = nullptr;
    size_t i = 1;
    size_t j;
    UIName aName;
    bool bDoublePaste = false;
    const size_t nSize = rWrtShell.GetFieldTypeCount();
    const ::utl::TransliterationWrapper& rColl = ::GetAppCmpStrIgnore();

    do {
        aName = UIName(aApp + OUString::number( i ));
        for( j = INIT_FLDTYPES; j < nSize; j++ )
        {
            pTyp = rWrtShell.GetFieldType( j );
            if( SwFieldIds::Dde == pTyp->Which() )
            {
                if( rColl.isEqual( static_cast<SwDDEFieldType*>(pTyp)->GetCmd(), aCmd ) &&
                    SfxLinkUpdateMode::ALWAYS == static_cast<SwDDEFieldType*>(pTyp)->GetType() )
                {
                    aName = pTyp->GetName();
                    bDoublePaste = true;
                    break;
                }
                else if( rColl.isEqual( aName.toString(), pTyp->GetName().toString() ) )
                    break;
            }
        }
        if( j == nSize )
            break;
        ++i;
    }
    while( !bDoublePaste );

    if( !bDoublePaste )
    {
        SwDDEFieldType aType( aName, aCmd, SfxLinkUpdateMode::ALWAYS );
        pTyp = rWrtShell.InsertFieldType( aType );
    }

    SwDDEFieldType* pDDETyp = static_cast<SwDDEFieldType*>(pTyp);

    OUString aExpand;
    if( rData.GetString( SotClipboardFormatId::STRING, aExpand ))
    {
        do {            // middle checked loop

            const sal_Int32 nNewlines{comphelper::string::getTokenCount(aExpand, '\n')};
            // When data comes from a spreadsheet, we add a DDE-table
            if( !aExpand.isEmpty() &&
                ( rData.HasFormat( SotClipboardFormatId::SYLK ) ||
                  rData.HasFormat( SotClipboardFormatId::SYLK_BIGCAPS ) ) )
            {
                sal_Int32 nRows = nNewlines ? nNewlines-1 : 0;
                if (!aExpand.endsWith("\n"))
                    ++nRows;    // last row has no newline, e.g. one single cell
                const sal_Int32 nCols = comphelper::string::getTokenCount(o3tl::getToken(aExpand, 0, '\n'), '\t');

                // don't try to insert tables that are too large for writer
                if (nRows > SAL_MAX_UINT16 || nCols > SAL_MAX_UINT16)
                {
                    if( bMsg )
                    {
                        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(nullptr,
                                                                  VclMessageType::Info, VclButtonsType::Ok,
                                                                  SwResId(STR_TABLE_TOO_LARGE)));
                        xBox->run();
                    }
                    pDDETyp = nullptr;
                    break;
                }

                // at least one column & row must be there
                if( !nRows || !nCols )
                {
                    if( bMsg )
                    {
                        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(nullptr,
                                                                  VclMessageType::Info, VclButtonsType::Ok,
                                                                  SwResId(STR_NO_TABLE)));
                        xBox->run();
                    }
                    pDDETyp = nullptr;
                    break;
                }

                rWrtShell.InsertDDETable(
                    SwInsertTableOptions( SwInsertTableFlags::SplitLayout, 1 ), // TODO MULTIHEADER
                    pDDETyp, nRows, nCols );
            }
            else if( nNewlines > 1 )
            {
                // multiple paragraphs -> insert a protected section
                if( rWrtShell.HasSelection() )
                    rWrtShell.DelRight();

                SwSectionData aSect( SectionType::DdeLink, aName );
                aSect.SetLinkFileName( aCmd );
                aSect.SetProtectFlag(true);
                rWrtShell.InsertSection( aSect );

                pDDETyp = nullptr;                // remove FieldTypes again
            }
            else
            {
                // insert
                SwDDEField aSwDDEField( pDDETyp );
                rWrtShell.InsertField2( aSwDDEField );
            }

        } while( false );
    }
    else
        pDDETyp = nullptr;                        // remove FieldTypes again

    if( !pDDETyp && !bDoublePaste )
    {
        // remove FieldType again - error occurred!
        for( j = nSize; j >= INIT_FLDTYPES; --j )
            if( pTyp == rWrtShell.GetFieldType( j ) )
            {
                rWrtShell.RemoveFieldType( j );
                break;
            }
    }

    return true;
}

bool SwTransferable::PasteSdrFormat(  const TransferableDataHelper& rData,
                                    SwWrtShell& rSh, SwPasteSdr nAction,
                                    const Point* pPt, SotExchangeActionFlags nActionFlags, bool bNeedToSelectBeforePaste)
{
    bool bRet = false;
    if( std::unique_ptr<SvStream> xStrm = rData.GetSotStorageStream( SotClipboardFormatId::DRAWING ))
    {
        xStrm->SetVersion( SOFFICE_FILEFORMAT_50 );

        if(bNeedToSelectBeforePaste && pPt)
        {
            // if this is an internal drag, need to set the target right (select it), else
            // still the source will be selected
            SwTransferable::SetSelInShell( rSh, true, pPt );
        }

        rSh.Paste( *xStrm, nAction, pPt );
        bRet = true;

        if( bRet && ( nActionFlags & SotExchangeActionFlags::InsertTargetUrl ))
            SwTransferable::PasteTargetURL( rData, rSh, SwPasteSdr::NONE, nullptr, false );
    }
    return bRet;
}

bool SwTransferable::PasteGrf( const TransferableDataHelper& rData, SwWrtShell& rSh,
                                SotClipboardFormatId nFormat, SwPasteSdr nAction, const Point* pPt,
                                SotExchangeActionFlags nActionFlags, sal_Int8 nDropAction, bool bNeedToSelectBeforePaste, RndStdIds nAnchorType )
{
    bool bRet = false;

    Graphic aGraphic;
    INetBookmark aBkmk;
    bool bCheckForGrf = false, bCheckForImageMap = false;

    switch( nFormat )
    {
    case SotClipboardFormatId::BITMAP:
    case SotClipboardFormatId::PNG:
    case SotClipboardFormatId::GDIMETAFILE:
        bRet = rData.GetGraphic( nFormat, aGraphic );
        break;

    case SotClipboardFormatId::SVXB:
    {
        if (std::unique_ptr<SvStream> xStm = rData.GetSotStorageStream(SotClipboardFormatId::SVXB))
        {
            TypeSerializer aSerializer(*xStm);
            aSerializer.readGraphic(aGraphic);
            bRet = (GraphicType::NONE != aGraphic.GetType() && GraphicType::Default != aGraphic.GetType());
        }

        break;
    }

    case SotClipboardFormatId::NETSCAPE_BOOKMARK:
    case SotClipboardFormatId::FILEGRPDESCRIPTOR:
    case SotClipboardFormatId::UNIFORMRESOURCELOCATOR:
        bRet = rData.GetINetBookmark( nFormat, aBkmk );
        if( bRet )
        {
            if( SwPasteSdr::SetAttr == nAction )
                nFormat = SotClipboardFormatId::NETSCAPE_BOOKMARK;
            else
                bCheckForGrf = true;
        }
        break;

    case SotClipboardFormatId::SIMPLE_FILE:
        {
            OUString sText;
            bRet = rData.GetString( nFormat, sText );
            if( bRet )
            {
                OUString sDesc;
                SwTransferable::CheckForURLOrLNKFile( rData, sText, &sDesc );

                sText = URIHelper::SmartRel2Abs(INetURLObject(), sText, Link<OUString*, bool>(),
                    false);

#ifdef _WIN32
                // Now that the path could be modified after SwTransferable::CheckForURLOrLNKFile,
                // where it could have been converted to URL, and made sure it's actually converted
                // to URL in URIHelper::SmartRel2Abs, we can finally convert file: URL back to
                // system path to make sure we don't use short path.
                // It looks not optimal, when we could apply GetLongPathNameW right to the original
                // pasted filename. But I don't know if (1) all arriving strings are system paths;
                // and (2) if SwTransferable::CheckForURLOrLNKFile could result in a different short
                // path, so taking a safe route.
                if (sText.startsWithIgnoreAsciiCase("file:"))
                {
                    // tdf#124500: Convert short path to long path which should be used in links
                    OUString sSysPath;
                    osl::FileBase::getSystemPathFromFileURL(sText, sSysPath);
                    std::unique_ptr<sal_Unicode[]> aBuf(new sal_Unicode[EXTENDED_MAX_PATH]);
                    DWORD nCopied = GetLongPathNameW(o3tl::toW(sSysPath.getStr()),
                                                     o3tl::toW(aBuf.get()), EXTENDED_MAX_PATH);
                    if (nCopied && nCopied < EXTENDED_MAX_PATH)
                        sText = URIHelper::SmartRel2Abs(INetURLObject(), OUString(aBuf.get()),
                                                        Link<OUString*, bool>(), false);
                }
#endif

                aBkmk = INetBookmark(sText, sDesc);
                bCheckForGrf = true;
                bCheckForImageMap = SwPasteSdr::Replace == nAction;
            }
        }
        break;

    default:
        bRet = rData.GetGraphic( nFormat, aGraphic );
        break;
    }

    if( bCheckForGrf )
    {
        //!!! check at FileSystem - only then it makes sense to test the graphics !!!
        GraphicFilter &rFlt = GraphicFilter::GetGraphicFilter();
        bRet = ERRCODE_NONE == GraphicFilter::LoadGraphic(aBkmk.GetURL(), OUString(),
                                            aGraphic, &rFlt );

        if( !bRet && SwPasteSdr::SetAttr == nAction &&
            SotClipboardFormatId::SIMPLE_FILE == nFormat &&
            // only at frame selection
            rSh.IsFrameSelected() )
        {
            // then set as hyperlink after the graphic
            nFormat = SotClipboardFormatId::NETSCAPE_BOOKMARK;
            bRet = true;
        }
    }

    if(pPt && bNeedToSelectBeforePaste)
    {
        // when using internal D&Ds, still the source object is selected and
        // this is necessary to get the correct source data which is also
        // dependent from selection. After receiving the drag data it is
        // now time to select the correct target object
        SwTransferable::SetSelInShell( rSh, true, pPt );
    }

    if( bRet )
    {
        //Check and Perform rotation if needed
        lclCheckAndPerformRotation(aGraphic);

        OUString sURL;
        if( dynamic_cast< const SwWebDocShell *>( rSh.GetView().GetDocShell() ) != nullptr
            // #i123922# if link action is noted, also take URL
            || DND_ACTION_LINK == nDropAction)
        {
            sURL = aBkmk.GetURL();
        }

        switch( nAction )
        {
            case SwPasteSdr::Insert:
            {
                SwTransferable::SetSelInShell( rSh, false, pPt );
                rSh.InsertGraphic(sURL, OUString(), aGraphic, nullptr, nAnchorType);
                break;
            }

            case SwPasteSdr::Replace:
            {
                if( rSh.GetSelectedObjCount() )
                {
                    // #i123922# for D&D on draw objects, do for now the same for
                    // SwPasteSdr::Replace (D&D) as for SwPasteSdr::SetAttr (D&D and
                    // CTRL+SHIFT). The code below replaces the draw object with
                    // a writer graphic; maybe this is an option later again if wanted
                    rSh.Paste( aGraphic, sURL );

                    // rSh.ReplaceSdrObj(sURL, OUString(), &aGraphic);
                    // Point aPt( pPt ? *pPt : rSh.GetCursorDocPos() );
                    // SwTransferable::SetSelInShell( rSh, true, &aPt );
                }
                else
                {
                    // set graphic at writer graphic without link
                    rSh.ReRead(sURL, OUString(), &aGraphic);
                }

                break;
            }

            case SwPasteSdr::SetAttr:
            {
                if( SotClipboardFormatId::NETSCAPE_BOOKMARK == nFormat )
                {
                    if( rSh.IsFrameSelected() )
                    {
                        SfxItemSetFixed<RES_URL, RES_URL> aSet( rSh.GetAttrPool() );
                        rSh.GetFlyFrameAttr( aSet );
                        SwFormatURL aURL( aSet.Get( RES_URL ) );
                        aURL.SetURL( aBkmk.GetURL(), false );
                        aSet.Put( aURL );
                        rSh.SetFlyFrameAttr( aSet );
                    }
                }
                else if( rSh.GetSelectedObjCount() )
                {
                    // set as attribute at DrawObject
                    rSh.Paste( aGraphic, sURL );
                }
                else if( OBJCNT_GRF == rSh.GetObjCntTypeOfSelection() )
                {
                    // set as linked graphic at writer graphic frame
                    rSh.ReRead(sURL, OUString(), &aGraphic);
                }
                else
                {
                    SwTransferable::SetSelInShell( rSh, false, pPt );
                    rSh.InsertGraphic(aBkmk.GetURL(), OUString(), aGraphic);
                }
                break;
            }
            default:
            {
                bRet = false;
                break;
            }
        }
    }

    if( bRet )
    {

        if( nActionFlags &
            ( SotExchangeActionFlags::InsertImageMap | SotExchangeActionFlags::ReplaceImageMap ) )
            SwTransferable::PasteImageMap( rData, rSh );

        if( nActionFlags & SotExchangeActionFlags::InsertTargetUrl )
            SwTransferable::PasteTargetURL( rData, rSh, SwPasteSdr::NONE, nullptr, false );
    }
    else if( bCheckForImageMap )
    {
        // or should the file be an ImageMap-File?
        ImageMap aMap;
        SfxMedium aMed( INetURLObject(aBkmk.GetURL()).GetFull(),
                            StreamMode::STD_READ );
        SvStream* pStream = aMed.GetInStream();
        if( pStream != nullptr  &&
            !pStream->GetError()  &&
            // mba: no BaseURL for clipboard functionality
            aMap.Read( *pStream, IMapFormat::Detect ) == IMAP_ERR_OK &&
            aMap.GetIMapObjectCount() )
        {
            SfxItemSetFixed<RES_URL, RES_URL> aSet( rSh.GetAttrPool() );
            rSh.GetFlyFrameAttr( aSet );
            SwFormatURL aURL( aSet.Get( RES_URL ) );
            aURL.SetMap( &aMap );
            aSet.Put( aURL );
            rSh.SetFlyFrameAttr( aSet );
            bRet = true;
        }
    }

    return bRet;
}

bool SwTransferable::PasteImageMap( const TransferableDataHelper& rData,
                                    SwWrtShell& rSh )
{
    bool bRet = false;
    if( rData.HasFormat( SotClipboardFormatId::SVIM ))
    {
        SfxItemSetFixed<RES_URL, RES_URL> aSet( rSh.GetAttrPool() );
        rSh.GetFlyFrameAttr( aSet );
        SwFormatURL aURL( aSet.Get( RES_URL ) );
        const ImageMap* pOld = aURL.GetMap();

        // set or replace, that is the question
        ImageMap aImageMap;
        if( rData.GetImageMap( SotClipboardFormatId::SVIM, aImageMap ) &&
            ( !pOld || aImageMap != *pOld ))
        {
            aURL.SetMap( &aImageMap );
            aSet.Put( aURL );
            rSh.SetFlyFrameAttr( aSet );
        }
        bRet = true;
    }
    return bRet;
}

bool SwTransferable::PasteAsHyperlink( const TransferableDataHelper& rData,
                                        SwWrtShell& rSh, SotClipboardFormatId nFormat )
{
    bool bRet = false;
    OUString sFile;
    if( rData.GetString( nFormat, sFile ) && !sFile.isEmpty() )
    {
        OUString sDesc;
        SwTransferable::CheckForURLOrLNKFile( rData, sFile, &sDesc );

        // first, make the URL absolute
        INetURLObject aURL;
        aURL.SetSmartProtocol( INetProtocol::File );
        aURL.SetSmartURL( sFile );
        sFile = aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE );

        switch( rSh.GetObjCntTypeOfSelection() )
        {
        case OBJCNT_FLY:
        case OBJCNT_GRF:
        case OBJCNT_OLE:
            {
                SfxItemSetFixed<RES_URL, RES_URL> aSet( rSh.GetAttrPool() );
                rSh.GetFlyFrameAttr( aSet );
                SwFormatURL aURL2( aSet.Get( RES_URL ) );
                aURL2.SetURL( sFile, false );
                if( aURL2.GetName().isEmpty() )
                    aURL2.SetName( sFile );
                aSet.Put( aURL2 );
                rSh.SetFlyFrameAttr( aSet );
            }
            break;

        default:
            {
                rSh.InsertURL( SwFormatINetFormat( sFile, OUString() ),
                                sDesc.isEmpty() ? sFile : sDesc);
            }
        }
        bRet = true;
    }
    return bRet;
}

bool SwTransferable::PasteFileName( const TransferableDataHelper& rData,
                                    SwWrtShell& rSh, SotClipboardFormatId nFormat,
                                    SwPasteSdr nAction, const Point* pPt,
                                    SotExchangeActionFlags nActionFlags,
                                    bool * graphicInserted)
{
    bool bRet = SwTransferable::PasteGrf( rData, rSh, nFormat, nAction,
                                            pPt, nActionFlags, 0, false);
    if (graphicInserted != nullptr) {
        *graphicInserted = bRet;
    }
    if( !bRet )
    {
        OUString sFile, sDesc;
        if( rData.GetString( nFormat, sFile ) && !sFile.isEmpty() )
        {
#if HAVE_FEATURE_AVMEDIA
            INetURLObject aMediaURL;

            aMediaURL.SetSmartURL( sFile );
            const OUString aMediaURLStr( aMediaURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );

            if( ::avmedia::MediaWindow::isMediaURL( aMediaURLStr, u""_ustr/*TODO?*/ ) )
            {
                const SfxStringItem aMediaURLItem( SID_INSERT_AVMEDIA, aMediaURLStr );
                rSh.GetView().GetViewFrame().GetDispatcher()->ExecuteList(
                                SID_INSERT_AVMEDIA, SfxCallMode::SYNCHRON,
                                { &aMediaURLItem });
            }
#else
            if (false)
            {
            }
#endif
            else
            {
                bool bIsURLFile = SwTransferable::CheckForURLOrLNKFile( rData, sFile, &sDesc );

                //Own FileFormat? --> insert, not for StarWriter/Web
                OUString sFileURL = URIHelper::SmartRel2Abs(INetURLObject(), sFile, Link<OUString *, bool>(), false );
                std::shared_ptr<const SfxFilter> pFlt = SwPasteSdr::SetAttr == nAction
                        ? nullptr : SwIoSystem::GetFileFilter(sFileURL);
                if( pFlt && dynamic_cast< const SwWebDocShell *>( rSh.GetView().GetDocShell() ) == nullptr )
                {
                    // and then pull up the insert-region-dialog
                    SwSectionData aSect(
                                    SectionType::FileLink,
                                    UIName(rSh.GetDoc()->GetUniqueSectionName()) );
                    aSect.SetLinkFileName( sFileURL );
                    aSect.SetProtectFlag( true );

                    rSh.StartInsertRegionDialog( aSect ); // starts dialog asynchronously
                    bRet = true;
                }
                else if (SwPasteSdr::Insert == nAction && rData.HasFormat(SotClipboardFormatId::SIMPLE_FILE))
                {
                    // insert file as OLE
                    PasteOLE(rData, rSh, nFormat, nActionFlags, nullptr == pPt);
                }
                else if( SwPasteSdr::SetAttr == nAction ||
                        ( bIsURLFile && SwPasteSdr::Insert == nAction ))
                {
                    //we can insert foreign files as links after all

                    // first, make the URL absolute
                    INetURLObject aURL;
                    aURL.SetSmartProtocol( INetProtocol::File );
                    aURL.SetSmartURL( sFile );
                    sFile = aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE );

                    switch( rSh.GetObjCntTypeOfSelection() )
                    {
                    case OBJCNT_FLY:
                    case OBJCNT_GRF:
                    case OBJCNT_OLE:
                        {
                            SfxItemSetFixed<RES_URL, RES_URL> aSet( rSh.GetAttrPool() );
                            rSh.GetFlyFrameAttr( aSet );
                            SwFormatURL aURL2( aSet.Get( RES_URL ) );
                            aURL2.SetURL( sFile, false );
                            if( aURL2.GetName().isEmpty() )
                                aURL2.SetName( sFile );
                            aSet.Put( aURL2 );
                            rSh.SetFlyFrameAttr( aSet );
                        }
                        break;

                    default:
                        {
                            rSh.InsertURL( SwFormatINetFormat( sFile, OUString() ),
                                            sDesc.isEmpty() ? sFile : sDesc );
                        }
                    }
                    bRet = true;
                }
            }
        }
    }
    return bRet;
}

bool SwTransferable::PasteDBData( const TransferableDataHelper& rData,
                                    SwWrtShell& rSh, SotClipboardFormatId nFormat, bool bLink,
                                    const Point* pDragPt, bool bMsg )
{
    bool bRet = false;
    OUString sText;
    if( rData.GetString( nFormat, sText ) && !sText.isEmpty() )
    {
        sal_uInt16 nWh = SotClipboardFormatId::SBA_CTRLDATAEXCHANGE == nFormat
                    ? 0
                    : SotClipboardFormatId::SBA_DATAEXCHANGE == nFormat
                                ? (bLink
                                    ? FN_QRY_MERGE_FIELD
                                    : FN_QRY_INSERT)
                                : (bLink
                                    ? 0
                                    : FN_QRY_INSERT_FIELD );
        const DataFlavorExVector& rVector = rData.GetDataFlavorExVector();
        bool bHaveColumnDescriptor = OColumnTransferable::canExtractColumnDescriptor(rVector, ColumnTransferFormatFlags::COLUMN_DESCRIPTOR | ColumnTransferFormatFlags::CONTROL_EXCHANGE);
        if ( SotClipboardFormatId::XFORMS == nFormat )
        {
            rSh.MakeDrawView();
            FmFormView* pFmView = dynamic_cast<FmFormView*>( rSh.GetDrawView()  );
            if (pFmView && pDragPt)
            {
                OXFormsDescriptor aDesc = OXFormsTransferable::extractDescriptor(rData);
                rtl::Reference<SdrObject> pObj = pFmView->CreateXFormsControl(aDesc);
                if(pObj)
                {
                    rSh.SwFEShell::InsertDrawObj( *pObj, *pDragPt );
                }
            }
        }
        else if( nWh )
        {
            std::unique_ptr<SfxUnoAnyItem> pConnectionItem;
            std::unique_ptr<SfxUnoAnyItem> pCursorItem;
            std::unique_ptr<SfxUnoAnyItem> pColumnItem;
            std::unique_ptr<SfxUnoAnyItem> pSourceItem;
            std::unique_ptr<SfxUnoAnyItem> pCommandItem;
            std::unique_ptr<SfxUnoAnyItem> pCommandTypeItem;
            std::unique_ptr<SfxUnoAnyItem> pColumnNameItem;
            std::unique_ptr<SfxUnoAnyItem> pSelectionItem;

            bool bDataAvailable = true;
            ODataAccessDescriptor aDesc;
            if(bHaveColumnDescriptor)
                aDesc = OColumnTransferable::extractColumnDescriptor(rData);
            else if(ODataAccessObjectTransferable::canExtractObjectDescriptor(rVector) )
                aDesc = ODataAccessObjectTransferable::extractObjectDescriptor(rData);
            else
                bDataAvailable = false;

            if ( bDataAvailable )
            {
                pConnectionItem.reset(new SfxUnoAnyItem(FN_DB_CONNECTION_ANY, aDesc[DataAccessDescriptorProperty::Connection]));
                pColumnItem.reset(new SfxUnoAnyItem(FN_DB_COLUMN_ANY, aDesc[DataAccessDescriptorProperty::ColumnObject]));
                pSourceItem.reset(new SfxUnoAnyItem(FN_DB_DATA_SOURCE_ANY, Any(aDesc.getDataSource())));
                pCommandItem.reset(new SfxUnoAnyItem(FN_DB_DATA_COMMAND_ANY, aDesc[DataAccessDescriptorProperty::Command]));
                pCommandTypeItem.reset(new SfxUnoAnyItem(FN_DB_DATA_COMMAND_TYPE_ANY, aDesc[DataAccessDescriptorProperty::CommandType]));
                pColumnNameItem.reset(new SfxUnoAnyItem(FN_DB_DATA_COLUMN_NAME_ANY, aDesc[DataAccessDescriptorProperty::ColumnName]));
                pSelectionItem.reset(new SfxUnoAnyItem(FN_DB_DATA_SELECTION_ANY, aDesc[DataAccessDescriptorProperty::Selection]));
                pCursorItem.reset(new SfxUnoAnyItem(FN_DB_DATA_CURSOR_ANY, aDesc[DataAccessDescriptorProperty::Cursor]));
            }

            SwView& rView = rSh.GetView();
            //force ::SelectShell
            rView.StopShellTimer();

            SfxStringItem aDataDesc( nWh, sText );
            rView.GetViewFrame().GetDispatcher()->ExecuteList(
                nWh, SfxCallMode::ASYNCHRON,
                { &aDataDesc, pConnectionItem.get(), pColumnItem.get(),
                  pSourceItem.get(), pCommandItem.get(), pCommandTypeItem.get(),
                  pColumnNameItem.get(), pSelectionItem.get(),
                  pCursorItem.get() });
        }
        else
        {
            rSh.MakeDrawView();
            FmFormView* pFmView = dynamic_cast<FmFormView*>( rSh.GetDrawView()  );
            if (pFmView && bHaveColumnDescriptor && pDragPt)
            {
                rtl::Reference<SdrObject> pObj = pFmView->CreateFieldControl( OColumnTransferable::extractColumnDescriptor(rData) );
                if (pObj)
                    rSh.SwFEShell::InsertDrawObj( *pObj, *pDragPt );
            }
        }
        bRet = true;
    }
    else if( bMsg )
    {
        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(nullptr,
                                                  VclMessageType::Info, VclButtonsType::Ok,
                                                  SwResId(STR_CLPBRD_FORMAT_ERROR)));
        xBox->run();
    }
    return bRet;
}

bool SwTransferable::PasteFileList( const TransferableDataHelper& rData,
                                    SwWrtShell& rSh, bool bLink,
                                    const Point* pPt, bool bMsg )
{
    bool bRet = false;
    FileList aFileList;
    if( rData.GetFileList( SotClipboardFormatId::FILE_LIST, aFileList ) &&
        aFileList.Count() )
    {
        SwPasteSdr nAct = bLink ? SwPasteSdr::SetAttr : SwPasteSdr::Insert;
        UIName sFlyNm;
        // iterate over the filelist
        for( sal_uLong n = 0, nEnd = aFileList.Count(); n < nEnd; ++n )
        {
            rtl::Reference<TransferDataContainer> pHlp = new TransferDataContainer;
            pHlp->CopyString( SotClipboardFormatId::SIMPLE_FILE, aFileList.GetFile( n ));
            TransferableDataHelper aData( pHlp );

            if( SwTransferable::PasteFileName( aData, rSh, SotClipboardFormatId::SIMPLE_FILE, nAct,
                                            pPt, SotExchangeActionFlags::NONE, nullptr ))
            {
                if( bLink )
                {
                    sFlyNm = rSh.GetFlyName();
                    SwTransferable::SetSelInShell( rSh, false, pPt );
                }
                bRet = true;
            }
        }
        if( !sFlyNm.isEmpty() )
            rSh.GotoFly( sFlyNm );
    }
    else if( bMsg )
    {
        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(nullptr,
                                                  VclMessageType::Info, VclButtonsType::Ok,
                                                  SwResId(STR_CLPBRD_FORMAT_ERROR)));
        xBox->run();
    }
    return bRet;
}

bool SwTransferable::CheckForURLOrLNKFile( const TransferableDataHelper& rData,
                                        OUString& rFileName, OUString* pTitle )
{
    bool bIsURLFile = false;
    INetBookmark aBkmk;
    if( rData.GetINetBookmark( SotClipboardFormatId::SOLK, aBkmk ) )
    {
        rFileName = aBkmk.GetURL();
        if( pTitle )
            *pTitle = aBkmk.GetDescription();
        bIsURLFile = true;
    }
    else
    {
        if( rFileName.getLength()>4 && rFileName.endsWithIgnoreAsciiCase(".url") )
        {
            OSL_ENSURE( false, "how do we read today .URL - Files?" );
        }
    }
    return bIsURLFile;
}

bool SwTransferable::IsPasteSpecial( const SwWrtShell& rWrtShell,
                                     const TransferableDataHelper& rData )
{
    // we can paste-special if there's an entry in the paste-special-format list
    SvxClipboardFormatItem aClipboardFormatItem(TypedWhichId<SvxClipboardFormatItem>(0));
    FillClipFormatItem( rWrtShell, rData, aClipboardFormatItem);
    return aClipboardFormatItem.Count() > 0;
}

bool SwTransferable::IsPasteOwnFormat( const TransferableDataHelper& rData )
{
    return ( GetSwTransferable( rData ) != nullptr );
}

bool SwTransferable::PasteFormat( SwWrtShell& rSh,
                                    const TransferableDataHelper& rData,
                                    SotClipboardFormatId nFormat )
{
    SwWait aWait( *rSh.GetView().GetDocShell(), false );
    bool bRet = false;

    SotClipboardFormatId nPrivateFormat = SotClipboardFormatId::PRIVATE;
    SwTransferable *pClipboard = GetSwTransferable( rData );
    if( pClipboard &&
        ((TransferBufferType::Document|TransferBufferType::Graphic|TransferBufferType::Ole) & pClipboard->m_eBufferType ))
        nPrivateFormat = SotClipboardFormatId::EMBED_SOURCE;

    if( pClipboard && nPrivateFormat == nFormat )
        bRet = pClipboard->PrivatePaste( rSh );
    else if( rData.HasFormat( nFormat ) )
    {
        uno::Reference<XTransferable> xTransferable( rData.GetXTransferable() );
        sal_uInt8       nEventAction;
        SotExchangeDest nDestination = SwTransferable::GetSotDestination( rSh );
        sal_uInt16      nSourceOptions =
                    (( SotExchangeDest::DOC_TEXTFRAME == nDestination ||
                       SotExchangeDest::SWDOC_FREE_AREA == nDestination ||
                       SotExchangeDest::DOC_TEXTFRAME_WEB == nDestination ||
                       SotExchangeDest::SWDOC_FREE_AREA_WEB == nDestination )
                                        ? EXCHG_IN_ACTION_COPY
                                        : EXCHG_IN_ACTION_MOVE);
        SotExchangeActionFlags nActionFlags;
        sal_uInt8      nAction = SotExchange::GetExchangeAction(
                                    rData.GetDataFlavorExVector(),
                                    nDestination,
                                    nSourceOptions,             /* ?? */
                                    EXCHG_IN_ACTION_DEFAULT,    /* ?? */
                                    nFormat, nEventAction, nFormat,
                                    lcl_getTransferPointer ( xTransferable ),
                                    &nActionFlags );

        if( EXCHG_INOUT_ACTION_NONE != nAction )
            bRet = SwTransferable::PasteData( rData, rSh, nAction, nActionFlags, nFormat,
                                                nDestination, true, false );
    }
    return bRet;
}

bool SwTransferable::TestAllowedFormat( const TransferableDataHelper& rData,
                                        SotClipboardFormatId nFormat, SotExchangeDest nDestination )
{
    sal_uInt8 nAction = EXCHG_INOUT_ACTION_NONE;
    if( rData.HasFormat( nFormat )) {
        uno::Reference<XTransferable> xTransferable( rData.GetXTransferable() );
        sal_uInt8 nEventAction;
        nAction = SotExchange::GetExchangeAction(
                        rData.GetDataFlavorExVector(),
                        nDestination, EXCHG_IN_ACTION_COPY,
                        EXCHG_IN_ACTION_COPY, nFormat,
                        nEventAction, nFormat,
                        lcl_getTransferPointer ( xTransferable ) );
    }
    return EXCHG_INOUT_ACTION_NONE != nAction;
}

/**
 * the list of formats which will be offered to the user in the 'Paste
 * Special...' dialog and the paste button menu
 */
const SotClipboardFormatId aPasteSpecialIds[] =
{
    SotClipboardFormatId::HTML,
    SotClipboardFormatId::HTML_SIMPLE,
    SotClipboardFormatId::HTML_NO_COMMENT,
    SotClipboardFormatId::RTF,
    SotClipboardFormatId::RICHTEXT,
    SotClipboardFormatId::STRING,
    SotClipboardFormatId::SONLK,
    SotClipboardFormatId::NETSCAPE_BOOKMARK,
    SotClipboardFormatId::DRAWING,
    SotClipboardFormatId::SVXB,
    SotClipboardFormatId::GDIMETAFILE,
    SotClipboardFormatId::BITMAP,
    SotClipboardFormatId::SVIM,
    SotClipboardFormatId::FILEGRPDESCRIPTOR,
    SotClipboardFormatId::MARKDOWN,
    SotClipboardFormatId::NONE
};

bool SwTransferable::PasteUnformatted( SwWrtShell& rSh, const TransferableDataHelper& rData )
{
    // Plain text == unformatted
    return SwTransferable::PasteFormat( rSh, rData, SotClipboardFormatId::STRING );
}

void SwTransferable::PrePasteSpecial( const SwWrtShell& rSh, const TransferableDataHelper& rData, const VclPtr<SfxAbstractPasteDialog>& pDlg )
{
    DataFlavorExVector aFormats( rData.GetDataFlavorExVector() );
    TransferableObjectDescriptor aDesc;

    SotExchangeDest nDest = SwTransferable::GetSotDestination( rSh );

    SwTransferable *pClipboard = GetSwTransferable( rData );
    if( pClipboard )
    {
        aDesc = pClipboard->m_aObjDesc;
        TranslateId pResId;
        if( pClipboard->m_eBufferType & TransferBufferType::Document )
            pResId = STR_PRIVATETEXT;
        else if( pClipboard->m_eBufferType & TransferBufferType::Graphic )
            pResId = STR_PRIVATEGRAPHIC;
        else if( pClipboard->m_eBufferType == TransferBufferType::Ole )
            pResId = STR_PRIVATEOLE;

        if (pResId)
        {
            if (STR_PRIVATEOLE == pResId || STR_PRIVATEGRAPHIC == pResId)
            {
                // add SotClipboardFormatId::EMBED_SOURCE to the formats. This
                // format display then the private format name.
                DataFlavorEx aFlavorEx;
                aFlavorEx.mnSotId = SotClipboardFormatId::EMBED_SOURCE;
                aFormats.insert( aFormats.begin(), aFlavorEx );
            }
            pDlg->SetObjName( pClipboard->m_aObjDesc.maClassName,
                                SwResId(pResId) );
            pDlg->Insert( SotClipboardFormatId::EMBED_SOURCE, OUString() );
        }
    }
    else
    {
        if( rData.HasFormat( SotClipboardFormatId::OBJECTDESCRIPTOR ) )
        {
            (void)rData.GetTransferableObjectDescriptor(
                                SotClipboardFormatId::OBJECTDESCRIPTOR, aDesc );
        }

        if( SwTransferable::TestAllowedFormat( rData, SotClipboardFormatId::EMBED_SOURCE, nDest ))
            pDlg->Insert( SotClipboardFormatId::EMBED_SOURCE, OUString() );
        if( SwTransferable::TestAllowedFormat( rData, SotClipboardFormatId::LINK_SOURCE, nDest ))
            pDlg->Insert( SotClipboardFormatId::LINK_SOURCE, OUString() );
    }

    if( SwTransferable::TestAllowedFormat( rData, SotClipboardFormatId::LINK, nDest ))
        pDlg->Insert( SotClipboardFormatId::LINK, SwResId(STR_DDEFORMAT) );

    for( const SotClipboardFormatId* pIds = aPasteSpecialIds; *pIds != SotClipboardFormatId::NONE; ++pIds )
        if( SwTransferable::TestAllowedFormat( rData, *pIds, nDest ))
            pDlg->Insert( *pIds, OUString() );
}

void SwTransferable::FillClipFormatItem( const SwWrtShell& rSh,
                                const TransferableDataHelper& rData,
                                SvxClipboardFormatItem & rToFill )
{
    SotExchangeDest nDest = SwTransferable::GetSotDestination( rSh );

    SwTransferable *pClipboard = GetSwTransferable( rData );
    if( pClipboard )
    {
        TranslateId pResId;
        if( pClipboard->m_eBufferType & TransferBufferType::Document )
            pResId = STR_PRIVATETEXT;
        else if( pClipboard->m_eBufferType & TransferBufferType::Graphic )
            pResId = STR_PRIVATEGRAPHIC;
        else if( pClipboard->m_eBufferType == TransferBufferType::Ole )
            pResId = STR_PRIVATEOLE;

        if (pResId)
            rToFill.AddClipbrdFormat(SotClipboardFormatId::EMBED_SOURCE,
                                       SwResId(pResId));
    }
    else
    {
        TransferableObjectDescriptor aDesc;
        if (rData.HasFormat(SotClipboardFormatId::OBJECTDESCRIPTOR))
        {
            (void)rData.GetTransferableObjectDescriptor(
                                SotClipboardFormatId::OBJECTDESCRIPTOR, aDesc);
        }

        if( SwTransferable::TestAllowedFormat( rData, SotClipboardFormatId::EMBED_SOURCE, nDest ))
            rToFill.AddClipbrdFormat( SotClipboardFormatId::EMBED_SOURCE,
                                            aDesc.maTypeName );
        if( SwTransferable::TestAllowedFormat( rData, SotClipboardFormatId::LINK_SOURCE, nDest ))
            rToFill.AddClipbrdFormat( SotClipboardFormatId::LINK_SOURCE );

        SotClipboardFormatId nFormat;
        if ( rData.HasFormat(nFormat = SotClipboardFormatId::EMBED_SOURCE_OLE) || rData.HasFormat(nFormat = SotClipboardFormatId::EMBEDDED_OBJ_OLE) )
        {
            OUString sName,sSource;
            if ( SvPasteObjectHelper::GetEmbeddedName(rData,sName,sSource,nFormat) )
                rToFill.AddClipbrdFormat( nFormat, sName );
        }
    }

    if( SwTransferable::TestAllowedFormat( rData, SotClipboardFormatId::LINK, nDest ))
        rToFill.AddClipbrdFormat( SotClipboardFormatId::LINK, SwResId(STR_DDEFORMAT) );

    for( const SotClipboardFormatId* pIds = aPasteSpecialIds; *pIds != SotClipboardFormatId::NONE; ++pIds )
        if( SwTransferable::TestAllowedFormat( rData, *pIds, nDest ))
            rToFill.AddClipbrdFormat(*pIds, OUString());
}

void SwTransferable::SetDataForDragAndDrop( const Point& rSttPos )
{
    if(!m_pWrtShell)
        return;
    OUString sGrfNm;
    const SelectionType nSelection = m_pWrtShell->GetSelectionType();
    if( SelectionType::Graphic == nSelection)
    {
        AddFormat( SotClipboardFormatId::SVXB );
        const Graphic* pGrf = m_pWrtShell->GetGraphic();
        if ( pGrf && pGrf->IsSupportedGraphic() )
        {
            AddFormat( SotClipboardFormatId::GDIMETAFILE );
            AddFormat( SotClipboardFormatId::PNG );
            AddFormat( SotClipboardFormatId::BITMAP );
        }
        m_eBufferType = TransferBufferType::Graphic;
        m_pWrtShell->GetGrfNms( &sGrfNm, nullptr );
    }
    else if( SelectionType::Ole == nSelection )
    {
        AddFormat( SotClipboardFormatId::EMBED_SOURCE );
        PrepareOLE( m_aObjDesc );
        AddFormat( SotClipboardFormatId::OBJECTDESCRIPTOR );
        AddFormat( SotClipboardFormatId::GDIMETAFILE );
        m_eBufferType = TransferBufferType::Ole;
    }
    //Is there anything to provide anyway?
    else if ( m_pWrtShell->IsSelection() || m_pWrtShell->IsFrameSelected() ||
              m_pWrtShell->GetSelectedObjCount() )
    {
        if( m_pWrtShell->GetSelectedObjCount() )
            m_eBufferType = TransferBufferType::Drawing;
        else
        {
            m_eBufferType = TransferBufferType::Document;
            if( SwWrtShell::NO_WORD !=
                m_pWrtShell->IntelligentCut( nSelection, false ))
                m_eBufferType = TransferBufferType::DocumentWord | m_eBufferType;
        }

        if( nSelection & SelectionType::TableCell )
            m_eBufferType = TransferBufferType::Table | m_eBufferType;

        AddFormat( SotClipboardFormatId::EMBED_SOURCE );

        //put RTF ahead of the OLE's Metafile for less loss
        if( !m_pWrtShell->GetSelectedObjCount() )
        {
            AddFormat( SotClipboardFormatId::RTF );
            AddFormat( SotClipboardFormatId::RICHTEXT );
            AddFormat( SotClipboardFormatId::HTML );
            AddFormat( SotClipboardFormatId::MARKDOWN );
        }
        if( m_pWrtShell->IsSelection() )
            AddFormat( SotClipboardFormatId::STRING );

        if( nSelection & ( SelectionType::DrawObject | SelectionType::DbForm ))
        {
            AddFormat( SotClipboardFormatId::DRAWING );
            if ( nSelection & SelectionType::DrawObject )
            {
                AddFormat( SotClipboardFormatId::GDIMETAFILE );
                AddFormat( SotClipboardFormatId::PNG );
                AddFormat( SotClipboardFormatId::BITMAP );
            }
            m_eBufferType = TransferBufferType::Graphic | m_eBufferType;

            // is it a URL-Button ?
            OUString sURL;
            OUString sDesc;
            if( m_pWrtShell->GetURLFromButton( sURL, sDesc ) )
            {
                AddFormat( SotClipboardFormatId::STRING );
                AddFormat( SotClipboardFormatId::SOLK );
                AddFormat( SotClipboardFormatId::NETSCAPE_BOOKMARK );
                AddFormat( SotClipboardFormatId::FILECONTENT );
                AddFormat( SotClipboardFormatId::FILEGRPDESCRIPTOR );
                AddFormat( SotClipboardFormatId::UNIFORMRESOURCELOCATOR );
                m_eBufferType = TransferBufferType::InetField | m_eBufferType;
            }
        }

        //ObjectDescriptor was already filled from the old DocShell.
        //Now adjust it. Thus in GetData the first query can still
        //be answered with delayed rendering.
        m_aObjDesc.maDragStartPos = rSttPos;
        m_aObjDesc.maSize = constOleSize100mm;

        PrepareOLE( m_aObjDesc );
        AddFormat( SotClipboardFormatId::OBJECTDESCRIPTOR );
    }
    else if( nSelection & SelectionType::Text && !m_pWrtShell->HasMark() )
    {
        // is only one field - selected?
        SwContentAtPos aContentAtPos( IsAttrAtPos::InetAttr );
        Point aPos( SwEditWin::GetDDStartPosX(), SwEditWin::GetDDStartPosY());

        if( m_pWrtShell->GetContentAtPos( aPos, aContentAtPos ) )
        {
            AddFormat( SotClipboardFormatId::STRING );
            AddFormat( SotClipboardFormatId::SOLK );
            AddFormat( SotClipboardFormatId::NETSCAPE_BOOKMARK );
            AddFormat( SotClipboardFormatId::FILECONTENT );
            AddFormat( SotClipboardFormatId::FILEGRPDESCRIPTOR );
            AddFormat( SotClipboardFormatId::UNIFORMRESOURCELOCATOR );
            m_eBufferType = TransferBufferType::InetField;
        }
    }

    if( !m_pWrtShell->IsFrameSelected() )
        return;

    SfxItemSetFixed<RES_URL, RES_URL> aSet( m_pWrtShell->GetAttrPool() );
    m_pWrtShell->GetFlyFrameAttr( aSet );
    const SwFormatURL& rURL = aSet.Get( RES_URL );
    if( rURL.GetMap() )
    {
        m_pImageMap.reset( new ImageMap( *rURL.GetMap() ) );
        AddFormat( SotClipboardFormatId::SVIM );
    }
    else if( !rURL.GetURL().isEmpty() )
    {
        m_pTargetURL.reset(new INetImage( sGrfNm, rURL.GetURL(),
                                    rURL.GetTargetFrameName() ));
        AddFormat( SotClipboardFormatId::INET_IMAGE );
    }
}

void SwTransferable::StartDrag( vcl::Window* pWin, const Point& rPos )
{
    if(!m_pWrtShell)
        return;
    m_bOldIdle = m_pWrtShell->GetViewOptions()->IsIdle();
    m_bCleanUp = true;

    m_pWrtShell->GetViewOptions()->SetIdle( false );

    if( m_pWrtShell->IsSelFrameMode() )
        m_pWrtShell->ShowCursor();

    SwModule::get()->m_pDragDrop = this;

    SetDataForDragAndDrop( rPos );

    sal_Int8 nDragOptions = DND_ACTION_COPYMOVE | DND_ACTION_LINK;
    SwDocShell* pDShell = m_pWrtShell->GetView().GetDocShell();
    if( ( pDShell && pDShell->IsReadOnly() ) || m_pWrtShell->HasReadonlySel() )
        nDragOptions &= ~DND_ACTION_MOVE;

    TransferableHelper::StartDrag( pWin, nDragOptions );
}

void SwTransferable::DragFinished( sal_Int8 nAction )
{
    //And the last finishing work so that all statuses are right
    if( DND_ACTION_MOVE == nAction  )
    {
        if( m_bCleanUp )
        {
            //It was dropped outside of Writer. We still have to
            //delete.

            m_pWrtShell->StartAllAction();
            m_pWrtShell->StartUndo( SwUndoId::UI_DRAG_AND_MOVE );
            if ( m_pWrtShell->IsTableMode() )
                m_pWrtShell->DeleteTableSel();
            else
            {
                if ( !(m_pWrtShell->IsSelFrameMode() || m_pWrtShell->GetSelectedObjCount()) )
                    //SmartCut, take one of the blanks along
                    m_pWrtShell->IntelligentCut( m_pWrtShell->GetSelectionType() );
                m_pWrtShell->DelRight();
            }
            m_pWrtShell->EndUndo( SwUndoId::UI_DRAG_AND_MOVE );
            m_pWrtShell->EndAllAction();
        }
        else
        {
            const SelectionType nSelection = m_pWrtShell->GetSelectionType();
            if( ( SelectionType::Frame | SelectionType::Graphic |
                 SelectionType::Ole | SelectionType::DrawObject ) & nSelection )
            {
                m_pWrtShell->EnterSelFrameMode();
            }
        }
    }
    m_pWrtShell->GetView().GetEditWin().DragFinished();

    if( m_pWrtShell->IsSelFrameMode() )
        m_pWrtShell->HideCursor();
    else
        m_pWrtShell->ShowCursor();

    m_pWrtShell->GetViewOptions()->SetIdle( m_bOldIdle );
}

namespace
{

bool lcl_checkClassification(SwDoc* pSourceDoc, SwDoc* pDestinationDoc)
{
    if (!pSourceDoc || !pDestinationDoc)
        return true;

    SwDocShell* pSourceShell = pSourceDoc->GetDocShell();
    SwDocShell* pDestinationShell = pDestinationDoc->GetDocShell();
    if (!pSourceShell || !pDestinationShell)
        return true;

    SfxClassificationCheckPasteResult eResult = SfxClassificationHelper::CheckPaste(pSourceShell->getDocProperties(), pDestinationShell->getDocProperties());
    return SfxClassificationHelper::ShowPasteInfo(eResult);
}

}

bool SwTransferable::PrivatePaste(SwWrtShell& rShell, SwPasteContext* pContext, PasteTableType ePasteTable)
{
    // first, ask for the SelectionType, then action-bracketing !!!!
    // (otherwise it's not pasted into a TableSelection!!!)
    OSL_ENSURE( !rShell.ActionPend(), "Paste must never have an ActionPend" );
    if ( !m_pClpDocFac )
        return false; // the return value of the SwFEShell::Paste also is bool!

    const SelectionType nSelection = rShell.GetSelectionType();

    SwTrnsfrActionAndUndo aAction( &rShell );

    bool bKillPaMs = false;

    //Delete selected content, not at table-selection and table in Clipboard, and don't delete hovering graphics.
    if( rShell.HasSelection() && !( nSelection & SelectionType::TableCell) && !( nSelection & SelectionType::DrawObject))
    {
        if (!(nSelection & SelectionType::NumberList))
        {
            bKillPaMs = true;
            rShell.SetRetainSelection( true );
        }
        if (pContext)
            pContext->forget();
        rShell.DelRight();
        if (pContext)
            pContext->remember();
        // when a Fly was selected, a valid cursor position has to be found now
        // (parked Cursor!)
        if( ( SelectionType::Frame | SelectionType::Graphic |
            SelectionType::Ole | SelectionType::DrawObject |
            SelectionType::DbForm ) & nSelection )
        {
            // position the cursor again
            Point aPt( rShell.GetCharRect().Pos() );
            rShell.SwCursorShell::SetCursor( aPt, true );
        }
        if (!(nSelection & SelectionType::NumberList))
        {
            rShell.SetRetainSelection( false );
        }
    }
    if ( nSelection & SelectionType::DrawObject) //unselect hovering graphics
    {
        rShell.ResetSelect(nullptr, false, ScrollSizeMode::ScrollSizeDefault);
    }

    bool bInWrd = false, bEndWrd = false, bSttWrd = false,
         bSmart(TransferBufferType::DocumentWord & m_eBufferType);
    if( bSmart )
    {
            // Why not for other Scripts? If TransferBufferType::DocumentWord is set, we have a word
            // in the buffer, word in this context means 'something with spaces at beginning
            // and end'. In this case we definitely want these spaces to be inserted here.
            bInWrd = rShell.IsInWord();
            bEndWrd = rShell.IsEndWrd();
            bSmart = bInWrd || bEndWrd;
            if( bSmart )
            {
                bSttWrd = rShell.IsStartWord();
                if (!bSttWrd && (bInWrd || bEndWrd))
                    rShell.SwEditShell::Insert(' ');
            }
    }

    bool bRet = true;
    // m_pWrtShell is nullptr when the source document is closed already.
    if (!m_pWrtShell || lcl_checkClassification(m_pWrtShell->GetDoc(), rShell.GetDoc()))
        bRet = rShell.Paste(m_pClpDocFac->GetDoc(), ePasteTable == PasteTableType::PASTE_TABLE);

    if( bKillPaMs )
        rShell.KillPams();

    // If Smart Paste then insert blank
    if( bRet && bSmart && ((bInWrd && !bEndWrd )|| bSttWrd) )
        rShell.SwEditShell::Insert(' ');

    return bRet;
}

bool SwTransferable::PrivateDrop( SwWrtShell& rSh, const Point& rDragPt,
                                bool bMove, bool bIsXSelection )
{
    int cWord    = 0;
    bool bInWrd  = false;
    bool bEndWrd = false;
    bool bSttWrd = false;
    bool bSttPara = false;
    bool bTableSel = false;
    bool bTableMove = false;
    bool bFrameSel = false;

    SwWrtShell& rSrcSh = *GetShell();

    rSh.UnSetVisibleCursor();

    if( TransferBufferType::InetField == m_eBufferType )
    {
        if( rSh.GetFormatFromObj( rDragPt ) )
        {
            INetBookmark aTmp;
            if( (TransferBufferType::InetField & m_eBufferType) && m_oBookmark )
                aTmp = *m_oBookmark;

            // select target graphic
            if( rSh.SelectObj( rDragPt ) )
            {
                rSh.HideCursor();
                rSh.EnterSelFrameMode( &rDragPt );
                g_bFrameDrag = true;
            }

            const SelectionType nSelection = rSh.GetSelectionType();

            // not yet consider Draw objects
            if( SelectionType::Graphic & nSelection )
            {
                SfxItemSetFixed<RES_URL, RES_URL> aSet( rSh.GetAttrPool() );
                rSh.GetFlyFrameAttr( aSet );
                SwFormatURL aURL( aSet.Get( RES_URL ) );
                aURL.SetURL( aTmp.GetURL(), false );
                aSet.Put( aURL );
                rSh.SetFlyFrameAttr( aSet );
                return true;
            }

            if( SelectionType::DrawObject & nSelection )
            {
                rSh.LeaveSelFrameMode();
                rSh.UnSelectFrame();
                rSh.ShowCursor();
                g_bFrameDrag = false;
            }
        }
    }

    if( &rSh != &rSrcSh && (SelectionType::Graphic & rSh.GetSelectionType()) &&
        TransferBufferType::Graphic == m_eBufferType )
    {
        // ReRead the graphic
        OUString sGrfNm;
        OUString sFltNm;
        rSrcSh.GetGrfNms( &sGrfNm, &sFltNm );
        rSh.ReRead( sGrfNm, sFltNm, rSrcSh.GetGraphic() );
        return true;
    }

    //not in selections or selected frames
    if( rSh.TestCurrPam( rDragPt ) ||
        ( rSh.IsSelFrameMode() && rSh.IsInsideSelectedObj( rDragPt )) )
        return false;

    if( rSrcSh.IsTableMode() )
    {
        bTableSel = true;
        const SelectionType nSelection = rSrcSh.GetSelectionType();
        // at enhanced table row/column selection or wholly selected tables,
        // paste rows above or columns before, and in the case of moving, remove the selection
        // (limit only to the single document case temporarily)
        if( rSrcSh.GetDoc() == rSh.GetDoc() &&
                ( (( SelectionType::TableRow | SelectionType::TableCol) & nSelection ) || rSrcSh.HasWholeTabSelection() ) )
        {
            bool bTableCol(SelectionType::TableCol & nSelection);

            ::sw::mark::MarkBase* pMarkMoveFrom = bMove
                    ? rSh.SetBookmark(
                                    vcl::KeyCode(),
                                    SwMarkName(),
                                    IDocumentMarkAccess::MarkType::UNO_BOOKMARK )
                    : nullptr;

            // row count and direction of the table selection:
            // up to down, if the cursor is there in its last table row
            const SwSelBoxes& rBoxes = rSrcSh.GetTableCursor()->GetSelectedBoxes();
            const SwTableNode* pTableNd = rSh.IsCursorInTable();
            if (!pTableNd)
            {
                SAL_WARN("sw", "presumably this case can't arise in practice");
                return false;
            }
            const SwTableLines& rLines = pTableNd->GetTable().GetTabLines();
            const SwStartNode& rDelPos = rBoxes.back()
                    ? *rBoxes.front()->GetSttNd()
                    : *pTableNd->GetStartNode();

            // count selected rows or columns
            sal_Int32 nSelRowOrCols = 0;
            if ( rBoxes.back() )
            {
                if ( bTableCol )
                {
                    // selected column count is the count of the cells
                    // in the first row of the selection
                    auto nLine = rLines.GetPos( rBoxes.front()->GetUpper() );
                    for (auto pBox : rBoxes)
                    {
                        // cell is in the next row
                        if ( nLine != rLines.GetPos( pBox->GetUpper() ) )
                            break;
                        ++nSelRowOrCols;
                    }
                }
                else
                {
                   // selected row count is the difference of the row number of the
                   // first and the last cell of the selection
                   nSelRowOrCols = rLines.GetPos( rBoxes.back()->GetUpper() ) -
                                   rLines.GetPos( rBoxes.front()->GetUpper() ) + 1;
                }
            }

            bool bSelUpToDown = rBoxes.back() && rBoxes.back()->GetUpper() ==
                           rSh.GetCursor()->GetPointNode().GetTableBox()->GetUpper();

            SwUndoId eUndoId = bMove ? SwUndoId::UI_DRAG_AND_MOVE : SwUndoId::UI_DRAG_AND_COPY;

            SwRewriter aRewriter;

            aRewriter.AddRule(UndoArg1, rSrcSh.GetSelDescr());

            if(rSrcSh.GetDoc() != rSh.GetDoc())
                rSrcSh.StartUndo( eUndoId, &aRewriter );
            rSh.StartUndo( eUndoId, &aRewriter );

            rSh.StartAction();
            rSrcSh.StartAction();

            SfxDispatcher* pDispatch = rSrcSh.GetView().GetViewFrame().GetDispatcher();
            pDispatch->Execute(SID_COPY, SfxCallMode::SYNCHRON);

            rSrcSh.Push(); // save selection for later restoration
            rSh.EnterStdMode();
            rSh.SwCursorShell::SetCursor(rDragPt, false);

            bool bPasteIntoTable = rSh.GetCursor()->GetPointNode().GetTableBox() != nullptr;

            // store cursor
            ::sw::mark::MarkBase* pMark = rSh.SetBookmark(
                                    vcl::KeyCode(),
                                    SwMarkName(),
                                    IDocumentMarkAccess::MarkType::UNO_BOOKMARK );

            // paste rows above/columns before
            pDispatch->Execute(bTableCol ? FN_TABLE_PASTE_COL_BEFORE : FN_TABLE_PASTE_ROW_BEFORE, SfxCallMode::SYNCHRON);

            // go to the previously inserted table rows and set them to tracked insertion, if needed
            bool bNeedTrack = !bTableCol && rSh.getIDocumentRedlineAccess().IsRedlineOn();

            // restore cursor position
            if (bNeedTrack && pMark != nullptr)
                rSh.GotoMark( pMark );

            if ( !bNeedTrack && !bPasteIntoTable )
            {
                rSrcSh.Pop(SwCursorShell::PopMode::DeleteCurrent); // restore selection...

                // delete source rows/columns
                if (bMove)
                    pDispatch->Execute(bTableCol
                        ? FN_TABLE_DELETE_COL
                        : FN_TABLE_DELETE_ROW, SfxCallMode::SYNCHRON);
            }
            else
            {
                const SwTableBox* pBoxStt = rSh.GetCursor()->GetPointNode().GetTableBox();
                SwTableLine* pLine = pBoxStt ? const_cast<SwTableLine*>( pBoxStt->GetUpper()): nullptr;

                for (sal_Int32 nDeleted = 0; bNeedTrack && nDeleted < nSelRowOrCols;)
                {
                    // move up text cursor (note: "true" is important for the layout level)
                    if ( !rSh.Up(false) )
                        break;

                    const SwTableBox* pBox = rSh.GetCursor()->GetPointNode().GetTableBox();

                    if ( !pBox )
                        break;

                    // Up() reaches a new row
                    if ( pBox->GetUpper() != pLine )
                    {
                        //rSh.SelTableRow();
                        SvxPrintItem aTracked(RES_PRINT, false);
                        rSh.GetDoc()->SetRowNotTracked( *rSh.GetCursor(), aTracked );
                        ++nDeleted;
                        pLine = const_cast<SwTableLine*>(pBox->GetUpper());
                    }
                }

                rSrcSh.Pop(SwCursorShell::PopMode::DeleteCurrent); // restore selection...

                // delete source rows/columns
                if (bMove)
                {
                    // restore cursor position
                    if (pMarkMoveFrom != nullptr)
                    {
                        rSh.GotoMark( pMarkMoveFrom );
                        rSh.getIDocumentMarkAccess()->deleteMark( pMarkMoveFrom );
                    }

                    // tracked table row moving: set original rows as tracked deletion,
                    // otherwise delete original rows/columns (tracking column deletion
                    // and insertion is not supported yet)
                    if ( !bTableCol && bNeedTrack )
                    {
                        pLine = nullptr;

                        for (sal_Int32 nDeleted = 0; nDeleted < nSelRowOrCols;)
                        {
                            const SwTableBox* pBox = rSh.GetCursor()->GetPointNode().GetTableBox();

                            if ( !pBox )
                                break;

                            if ( pBox->GetUpper() != pLine )
                            {
                                pLine = const_cast<SwTableLine*>(pBox->GetUpper());
                                pDispatch->Execute(FN_TABLE_DELETE_ROW, SfxCallMode::SYNCHRON);
                                ++nDeleted;
                            }

                            bool bMoved = false;
                            if (bSelUpToDown)
                                bMoved = rSh.Up(false);
                            else
                                bMoved = rSh.Down(false);
                            if (!bMoved)
                                break;
                        }
                    }
                    else
                    {
                        // set cursor in the first cell of the original selection
                        rSh.GetCursor()->DeleteMark();
                        rSh.GetCursor()->GetPoint()->Assign( rDelPos.GetIndex() + 1);

                        for (sal_Int32 nDeleted = 0; nDeleted < nSelRowOrCols; ++nDeleted)
                        {
                            pDispatch->Execute(bTableCol
                                ? FN_TABLE_DELETE_COL
                                : FN_TABLE_DELETE_ROW, SfxCallMode::SYNCHRON);
                        }
                    }
                }
            }

            // restore cursor position
            if (pMark != nullptr)
            {
                rSh.GotoMark( pMark );
                rSh.getIDocumentMarkAccess()->deleteMark( pMark );
            }

            rSh.DestroyCursor();
            rSh.EndUndo();
            rSh.EndAction();
            rSh.EndAction();
            return true;
        }

        if ( bMove && rSrcSh.HasWholeTabSelection() )
            bTableMove = true;
    }
    else if( rSrcSh.IsSelFrameMode() || rSrcSh.GetSelectedObjCount() )
    {
        // don't move position-protected objects!
        if( bMove && rSrcSh.IsSelObjProtected( FlyProtectFlags::Pos ) != FlyProtectFlags::NONE )
            return false;

        bFrameSel = true;
    }

    const SelectionType nSel = rSrcSh.GetSelectionType();

    SwUndoId eUndoId = bMove ? SwUndoId::UI_DRAG_AND_MOVE : SwUndoId::UI_DRAG_AND_COPY;

    SwRewriter aRewriter;

    aRewriter.AddRule(UndoArg1, rSrcSh.GetSelDescr());

    if(rSrcSh.GetDoc() != rSh.GetDoc())
        rSrcSh.StartUndo( eUndoId, &aRewriter );
    rSh.StartUndo( eUndoId, &aRewriter );

    rSh.StartAction();
    rSrcSh.StartAction();

    if( &rSrcSh != &rSh )
    {
        rSh.EnterStdMode();
        rSh.SwCursorShell::SetCursor( rDragPt, true );
        cWord = rSrcSh.IntelligentCut( nSel, false );
    }
    else if( !bTableSel && !bFrameSel )
    {
        if( !rSh.IsAddMode() )
        {
            // #i87233#
            if ( rSh.IsBlockMode() )
            {
                // preserve order of cursors for block mode
                rSh.GoPrevCursor();
            }

            rSh.SwCursorShell::CreateCursor();
        }
        rSh.SwCursorShell::SetCursor( rDragPt, true, false );
        rSh.GoPrevCursor();
        cWord = rSh.IntelligentCut( rSh.GetSelectionType(), false );
        rSh.GoNextCursor();
    }

    bInWrd  = rSh.IsInWord();
    bEndWrd = rSh.IsEndWrd();
    bSttWrd = !bEndWrd && rSh.IsStartWord();
    bSttPara= rSh.IsSttPara();

    Point aSttPt( SwEditWin::GetDDStartPosX(), SwEditWin::GetDDStartPosY() );

    // at first, select InetFields!
    if( TransferBufferType::InetField == m_eBufferType )
    {
        if( &rSrcSh == &rSh )
        {
            rSh.GoPrevCursor();
            rSh.SwCursorShell::SetCursor( aSttPt, true );
            rSh.SelectTextAttr( RES_TXTATR_INETFMT );
            if( rSh.TestCurrPam( rDragPt ) )
            {
                // don't copy/move inside of yourself
                rSh.DestroyCursor();
                rSh.EndUndo();
                rSh.EndAction();
                rSh.EndAction();
                return false;
            }
            rSh.GoNextCursor();
        }
        else
        {
            rSrcSh.SwCursorShell::SetCursor( aSttPt, true );
            rSrcSh.SelectTextAttr( RES_TXTATR_INETFMT );
        }

        // is there a URL attribute at the insert point? Then replace that,
        // so simply put up a selection?
        rSh.DelINetAttrWithText();
        g_bDDINetAttr = true;
    }

    if ( rSrcSh.IsSelFrameMode() )
    {
        //Hack: fool the special treatment
        aSttPt = rSrcSh.GetObjRect().Pos();
    }

    bool bRet = rSrcSh.SwFEShell::Copy(rSh, aSttPt, rDragPt, bMove,
                                       !bIsXSelection);

    if( !bIsXSelection )
    {
        rSrcSh.Push();
        if ( bRet && bMove && !bFrameSel )
        {
            if ( bTableSel )
            {
                /* delete table contents not cells */
                rSrcSh.Delete(false);
            }
            else
            {
                //SmartCut, take one of the blanks along.
                rSh.SwCursorShell::DestroyCursor();
                if ( cWord == SwWrtShell::WORD_SPACE_BEFORE )
                    rSh.ExtendSelection( false );
                else if ( cWord == SwWrtShell::WORD_SPACE_AFTER )
                    rSh.ExtendSelection();
                rSrcSh.DelRight();
            }
        }
        rSrcSh.KillPams();
        rSrcSh.Pop(SwCursorShell::PopMode::DeleteCurrent);

        /* after dragging a table selection inside one shell
            set cursor to the drop position. */
        if( &rSh == &rSrcSh && ( bTableSel || rSh.IsBlockMode() ) )
        {
            rSrcSh.CalcLayout();
            rSrcSh.SwCursorShell::SetCursor(rDragPt);
            rSrcSh.GetCursor()->SetMark();
        }
    }

    if( bRet && !bTableSel && !bFrameSel )
    {
        if( (bInWrd || bEndWrd) &&
            (cWord == SwWrtShell::WORD_SPACE_AFTER ||
                cWord == SwWrtShell::WORD_SPACE_BEFORE) )
        {
            if ( bSttWrd || (bInWrd && !bEndWrd))
                rSh.SwEditShell::Insert(' ', bIsXSelection);
            if ( !bSttWrd || (bInWrd && !bSttPara) )
            {
                rSh.SwapPam();
                if ( !bSttWrd )
                    rSh.SwEditShell::Insert(' ', bIsXSelection);
                rSh.SwapPam();
            }
        }

        if( bIsXSelection )
        {
            if( &rSrcSh == &rSh && !rSh.IsAddMode() )
            {
                rSh.SwCursorShell::DestroyCursor();
                rSh.GoPrevCursor();
            }
            else
            {
                rSh.SwapPam();
                rSh.SwCursorShell::ClearMark();
            }
        }
        else
        {
            if( rSh.IsAddMode() )
                rSh.SwCursorShell::CreateCursor();
            else
            {
                // turn on selection mode
                rSh.SttSelect();
                rSh.EndSelect();
            }
        }
    }
    else if ( bRet && bTableMove )
    {
        SfxDispatcher* pDispatch = rSrcSh.GetView().GetViewFrame().GetDispatcher();
        pDispatch->Execute(FN_TABLE_DELETE_TABLE, SfxCallMode::SYNCHRON);
    }

    if( bRet && bMove && bFrameSel )
        rSrcSh.LeaveSelFrameMode();

    if( rSrcSh.GetDoc() != rSh.GetDoc() )
        rSrcSh.EndUndo();
    rSh.EndUndo();

        // put the shell in the right state
    if( &rSrcSh != &rSh && ( rSh.IsFrameSelected() || rSh.GetSelectedObjCount() ))
        rSh.EnterSelFrameMode();

    rSrcSh.EndAction();
    rSh.EndAction();
    return true;
}

// Interfaces for Selection
void SwTransferable::CreateSelection( SwWrtShell& rSh,
                                      const SwFrameShell * _pCreatorView )
{
    rtl::Reference<SwTransferable> pNew = new SwTransferable( rSh );

    pNew->m_pCreatorView = _pCreatorView;

    SwModule::get()->m_pXSelection = pNew.get();
    pNew->CopyToPrimarySelection();
}

void SwTransferable::ClearSelection( const SwWrtShell& rSh,
                                     const SwFrameShell * _pCreatorView)
{
    SwModule* pMod = SwModule::get();
    if( pMod->m_pXSelection &&
        ((!pMod->m_pXSelection->m_pWrtShell) || (pMod->m_pXSelection->m_pWrtShell == &rSh)) &&
        (!_pCreatorView || (pMod->m_pXSelection->m_pCreatorView == _pCreatorView)) )
    {
        TransferableHelper::ClearPrimarySelection();
    }
}

SwTransferable* SwTransferable::GetSwTransferable( const TransferableDataHelper& rData )
{
    return dynamic_cast<SwTransferable*>(rData.GetTransferable().get());
}

SwTransferDdeLink::SwTransferDdeLink( SwTransferable& rTrans, SwWrtShell& rSh )
    : m_rTransfer(rTrans)
    , m_pDocShell(nullptr)
    , m_nOldTimeOut(0)
    , m_bDelBookmark(false)
    , m_bInDisconnect(false)
{
    // we only end up here with table- or text selection
    if( SelectionType::TableCell & rSh.GetSelectionType() )
    {
        SwFrameFormat* pFormat = rSh.GetTableFormat();
        if( pFormat )
            m_sName = pFormat->GetName().toString();
    }
    else
    {
        // creating a temp. bookmark without undo
        bool bUndo = rSh.DoesUndo();
        rSh.DoUndo( false );
        bool bIsModified = rSh.IsModified();

        ::sw::mark::MarkBase* pMark = rSh.SetBookmark(
            vcl::KeyCode(),
            SwMarkName(),
            IDocumentMarkAccess::MarkType::DDE_BOOKMARK);
        if(pMark)
        {
            m_sName = pMark->GetName().toString();
            m_bDelBookmark = true;
            if( !bIsModified )
                rSh.ResetModified();
        }
        else
            m_sName.clear();
        rSh.DoUndo( bUndo );
    }

    if( m_sName.isEmpty() ||
        nullptr == ( m_pDocShell = rSh.GetDoc()->GetDocShell() ))
        return;

    // then we create our "server" and connect to it
    m_xRefObj = m_pDocShell->DdeCreateLinkSource( m_sName );
    if( m_xRefObj.is() )
    {
        m_xRefObj->AddConnectAdvise( this );
        m_xRefObj->AddDataAdvise( this,
                        OUString(),
                        ADVISEMODE_NODATA | ADVISEMODE_ONLYONCE );
        m_nOldTimeOut = m_xRefObj->GetUpdateTimeout();
        m_xRefObj->SetUpdateTimeout( 0 );
    }
}

SwTransferDdeLink::~SwTransferDdeLink()
{
    if( m_xRefObj.is() )
        Disconnect( true );
}

::sfx2::SvBaseLink::UpdateResult SwTransferDdeLink::DataChanged( const OUString& ,
                                    const uno::Any& )
{
    // well, that's it with the link
    if( !m_bInDisconnect )
    {
        if( FindDocShell() && m_pDocShell->GetView() )
            m_rTransfer.RemoveDDELinkFormat( m_pDocShell->GetView()->GetEditWin() );
        Disconnect( false );
    }
    return SUCCESS;
}

bool SwTransferDdeLink::WriteData( SvStream& rStrm )
{
    if( !m_xRefObj.is() || !FindDocShell() )
        return false;

    TransferableDataHelper::WriteDDELink(rStrm, Application::GetAppName(),
                                         m_pDocShell->GetTitle(SFX_TITLE_FULLNAME), m_sName);

    IDocumentMarkAccess* const pMarkAccess = m_pDocShell->GetDoc()->getIDocumentMarkAccess();
    auto ppMark = pMarkAccess->findMark(SwMarkName(m_sName));
    if(ppMark != pMarkAccess->getAllMarksEnd()
        && IDocumentMarkAccess::GetType(**ppMark) != IDocumentMarkAccess::MarkType::BOOKMARK)
    {
        // the mark is still a DdeBookmark
        // we replace it with a Bookmark, so it will get saved etc.
        ::sw::mark::MarkBase* const pMark = *ppMark;
        ::sfx2::SvLinkSource* p = m_xRefObj.get();
        SwServerObject& rServerObject = dynamic_cast<SwServerObject&>(*p);

        // collecting state of old mark
        SwPaM aPaM(pMark->GetMarkStart());
        *aPaM.GetPoint() = pMark->GetMarkStart();
        if(pMark->IsExpanded())
        {
            aPaM.SetMark();
            *aPaM.GetMark() = pMark->GetMarkEnd();
        }
        SwMarkName sMarkName = pMark->GetName();

        // remove mark
        rServerObject.SetNoServer(); // this removes the connection between SwServerObject and mark
        // N.B. ppMark was not loaded from file and cannot have xml:id
        pMarkAccess->deleteMark(ppMark, false);

        // recreate as Bookmark
        ::sw::mark::MarkBase* const pNewMark = pMarkAccess->makeMark(
            aPaM,
            sMarkName,
            IDocumentMarkAccess::MarkType::BOOKMARK,
            ::sw::mark::InsertMode::New);
        rServerObject.SetDdeBookmark(*pNewMark);
    }

    m_bDelBookmark = false;
    return true;
}

void SwTransferDdeLink::Disconnect( bool bRemoveDataAdvise )
{
    //  don't accept DataChanged anymore, when already in Disconnect!
    //  (DTOR from Bookmark sends a DataChanged!)
    bool bOldDisconnect = m_bInDisconnect;
    m_bInDisconnect = true;

    // destroy the unused bookmark again (without Undo!)?
    if( m_bDelBookmark && m_xRefObj.is() && FindDocShell() )
    {
        SwDoc* pDoc = m_pDocShell->GetDoc();
        ::sw::UndoGuard const undoGuard(pDoc->GetIDocumentUndoRedo());

        // #i58448#
        Link<bool,void> aSavedOle2Link( pDoc->GetOle2Link() );
        pDoc->SetOle2Link( Link<bool,void>() );

        bool bIsModified = pDoc->getIDocumentState().IsModified();

        IDocumentMarkAccess* const pMarkAccess = pDoc->getIDocumentMarkAccess();
        pMarkAccess->deleteMark(pMarkAccess->findMark(SwMarkName(m_sName)), false);

        if( !bIsModified )
            pDoc->getIDocumentState().ResetModified();
        // #i58448#
        pDoc->SetOle2Link( aSavedOle2Link );

        m_bDelBookmark = false;
    }

    if( m_xRefObj.is() )
    {
        m_xRefObj->SetUpdateTimeout( m_nOldTimeOut );
        m_xRefObj->RemoveConnectAdvise( this );
        if( bRemoveDataAdvise )
            // in a DataChanged the SelectionObject must NEVER be deleted
            // is already handled by the base class
            // (ADVISEMODE_ONLYONCE!!!!)
            // but always in normal Disconnect!
            m_xRefObj->RemoveAllDataAdvise( this );
        m_xRefObj.clear();
    }
    m_bInDisconnect = bOldDisconnect;
}

bool SwTransferDdeLink::FindDocShell()
{
    SfxObjectShell* pTmpSh = SfxObjectShell::GetFirst( checkSfxObjectShell<SwDocShell> );
    while( pTmpSh )
    {
        if( pTmpSh == m_pDocShell )       // that's what we want to have
        {
            if( m_pDocShell->GetDoc() )
                return true;
            break;      // the Doc is not there anymore, so leave!
        }
        pTmpSh = SfxObjectShell::GetNext( *pTmpSh, checkSfxObjectShell<SwDocShell> );
    }

    m_pDocShell = nullptr;
    return false;
}

void SwTransferDdeLink::Closed()
{
    if( !m_bInDisconnect && m_xRefObj.is() )
    {
        m_xRefObj->RemoveAllDataAdvise( this );
        m_xRefObj->RemoveConnectAdvise( this );
        m_xRefObj.clear();
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
