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

#include <memory>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/util/XCloseable.hpp>
#include <com/sun/star/beans/NamedValue.hpp>
#include <comphelper/string.hxx>
#include <cppuhelper/implbase.hxx>
#include <sal/log.hxx>
#include <svl/itemset.hxx>
#include <tools/debug.hxx>

#include <vcl/svapp.hxx>
#include <sfx2/chalign.hxx>
#include <vcl/weld/Dialog.hxx>
#include <sfx2/childwin.hxx>
#include <sfx2/app.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/module.hxx>
#include <sfx2/dockwin.hxx>
#include <sfx2/dispatch.hxx>
#include <svtools/viewoptions.hxx>
#include <workwin.hxx>

#include <sfx2/sfxsids.hrc>
#include <o3tl/string_view.hxx>

const sal_uInt16 nVersion = 2;

SfxChildWinFactory::SfxChildWinFactory( SfxChildWinCtor pTheCtor, sal_uInt16 nID,
        sal_uInt16 n )
    : pCtor(pTheCtor)
    , nId( nID )
    , nPos(n)
{}

struct SfxChildWindow_Impl
{
    css::uno::Reference< css::frame::XFrame >             xFrame;
    css::uno::Reference< css::lang::XEventListener >      xListener;
    SfxChildWinFactory aFact = { nullptr, 0, 0 };
    bool                bHideNotDelete;
    bool                bVisible;
    bool                bWantsFocus;
    SfxWorkWindow*      pWorkWin;
};

namespace {

class DisposeListener : public ::cppu::WeakImplHelper< css::lang::XEventListener >
{
    public:
        DisposeListener( SfxChildWindow*      pOwner ,
                         SfxChildWindow_Impl* pData  )
            :   m_pOwner( pOwner )
            ,   m_pData ( pData  )
        {}

        virtual void SAL_CALL disposing( const css::lang::EventObject& aSource ) override
        {
            css::uno::Reference< css::lang::XEventListener > xSelfHold( this );

            css::uno::Reference< css::lang::XComponent > xComp( aSource.Source, css::uno::UNO_QUERY );
            if( xComp.is() )
                xComp->removeEventListener( this );

            if( !m_pOwner || !m_pData )
                return;

            m_pData->xListener.clear();

            if ( m_pData->pWorkWin )
            {
                // m_pOwner and m_pData will be killed
                m_pData->xFrame.clear();
                m_pData->pWorkWin->GetBindings().Execute( m_pOwner->GetType() );
            }
            else
            {
                delete m_pOwner;
            }

            m_pOwner = nullptr;
            m_pData  = nullptr;
        }

    private:
        SfxChildWindow*      m_pOwner;
        SfxChildWindow_Impl* m_pData ;
};

}

bool GetPosSizeFromString( std::u16string_view rStr, Point& rPos, Size& rSize )
{
    if ( comphelper::string::getTokenCount(rStr, '/') != 4 )
        return false;

    sal_Int32 nIdx = 0;
    rPos.setX( o3tl::toInt32(o3tl::getToken(rStr, 0, '/', nIdx)) );
    rPos.setY( o3tl::toInt32(o3tl::getToken(rStr, 0, '/', nIdx)) );
    rSize.setWidth( o3tl::toInt32(o3tl::getToken(rStr, 0, '/', nIdx)) );
    rSize.setHeight( o3tl::toInt32(o3tl::getToken(rStr, 0, '/', nIdx)) );

    // negative sizes are invalid
    return rSize.Width() >= 0 && rSize.Height() >= 0;
}

bool GetSplitSizeFromString( std::u16string_view rStr, Size& rSize )
{
    size_t nIndex = rStr.find( ',' );
    if ( nIndex != std::u16string_view::npos )
    {
        std::u16string_view aStr = rStr.substr( nIndex+1 );

        sal_Int32 nCount = comphelper::string::getTokenCount(aStr, ';');
        if ( nCount != 2 )
            return false;

        sal_Int32 nIdx{ 0 };
        rSize.setWidth( o3tl::toInt32(o3tl::getToken(aStr, 0, ';', nIdx )) );
        rSize.setHeight( o3tl::toInt32(o3tl::getToken(aStr, 0, ';', nIdx )) );

        // negative sizes are invalid
        return rSize.Width() >= 0 && rSize.Height() >= 0;
    }

    return false;
}

SfxChildWindow::SfxChildWindow(vcl::Window* const pParent, sal_uInt16 nId)
    : m_pParent(pParent)
    , m_pImpl(new SfxChildWindow_Impl)
    , m_eChildAlignment(SfxChildAlignment::NOALIGNMENT)
    , m_nType(nId)
{
    m_pImpl->bHideNotDelete = false;
    m_pImpl->bWantsFocus = true;
    m_pImpl->bVisible = true;
    m_pImpl->pWorkWin = nullptr;
}

void SfxChildWindow::Destroy()
{
    if ( GetFrame().is() )
    {
        ClearWorkwin();
        try
        {
            css::uno::Reference < css::util::XCloseable > xClose( GetFrame(), css::uno::UNO_QUERY );
            if ( xClose.is() )
                xClose->close( true );
            else
                GetFrame()->dispose();
        }
        catch (const css::uno::Exception&)
        {
        }
    }
    else
        delete this;
}

void SfxChildWindow::ClearWorkwin()
{
    if (m_pImpl->pWorkWin)
    {
        if (m_pImpl->pWorkWin->GetActiveChild_Impl() == m_pWindow)
            m_pImpl->pWorkWin->SetActiveChild_Impl(nullptr);
        m_pImpl->pWorkWin = nullptr;
    }
}

SfxChildWindow::~SfxChildWindow()
{
    ClearWorkwin();
    if (m_xController)
    {
        m_xController->ChildWinDispose();
        m_xController.reset();
    }
    m_pWindow.disposeAndClear();
}

std::unique_ptr<SfxChildWindow> SfxChildWindow::CreateChildWindow(sal_uInt16 nId,
                                                                  vcl::Window* pParent,
                                                                  SfxBindings& rBindings,
                                                                  SfxChildWinInfo& rInfo)
{
    std::unique_ptr<SfxChildWindow> pChild;
    SfxChildWinFactory* pFact=nullptr;
    SystemWindowFlags nOldMode = Application::GetSystemWindowMode();

    // First search for ChildWindow in SDT; Overlay windows are realized
    // by using ChildWindowContext
    SfxApplication *pApp = SfxGetpApp();
    {
        pFact = pApp->GetChildWinFactoryById(nId);
        if ( pFact )
        {
            if ( rInfo.bVisible )
            {
                rBindings.ENTERREGISTRATIONS();
                SfxChildWinInfo aInfo = rInfo;
                Application::SetSystemWindowMode( SystemWindowFlags::NOAUTOMODE );
                pChild = pFact->pCtor(pParent, nId, &rBindings, &aInfo);
                pChild->Initialize();
                Application::SetSystemWindowMode( nOldMode );
                rBindings.LEAVEREGISTRATIONS();
            }
        }
    }

    SfxDispatcher* pDisp = rBindings.GetDispatcher_Impl();
    SfxModule *pMod = pDisp ? SfxModule::GetActiveModule( pDisp->GetFrame() ) : nullptr;
    if (!pChild && pMod)
    {
        pFact = pMod->GetChildWinFactoryById(nId);
        if ( pFact )
        {
            if ( rInfo.bVisible )
            {
                rBindings.ENTERREGISTRATIONS();
                SfxChildWinInfo aInfo = rInfo;
                Application::SetSystemWindowMode( SystemWindowFlags::NOAUTOMODE );
                pChild = pFact->pCtor(pParent, nId, &rBindings, &aInfo);
                pChild->Initialize();
                rInfo.nFlags |= aInfo.nFlags;
                Application::SetSystemWindowMode( nOldMode );
                rBindings.LEAVEREGISTRATIONS();
            }
        }
    }

    if (pChild)
    {
        assert(pFact && "pChild is returned by a call on pFact, so pFact cannot be null");
        pChild->SetFactory_Impl( pFact );
    }

    DBG_ASSERT(pFact && (pChild || !rInfo.bVisible), "ChildWindow-Typ not registered!");

    if (pChild && (!pChild->m_pWindow && !pChild->m_xController))
    {
        pChild.reset();
        SAL_INFO("sfx.appl", "ChildWindow has no Window!");
    }

    return pChild;
}


void SfxChildWindow::SaveStatus(const SfxChildWinInfo& rInfo)
{
    sal_uInt16 nID = GetType();

    OUString aInfoVisible = rInfo.bVisible ? u"V"_ustr : u"H"_ustr;

    OUString aWinData = "V"
                      + OUString::number(static_cast<sal_Int32>(nVersion))
                      + ","
                      + aInfoVisible
                      + ","
                      + OUString::number(static_cast<sal_Int32>(rInfo.nFlags));

    if ( !rInfo.aExtraString.isEmpty() )
        aWinData += "," + rInfo.aExtraString;

    OUString sName(OUString::number(nID));
    //Try and save window state per-module, e.g. sidebar on in one application
    //but off in another
    if (!rInfo.aModule.isEmpty())
        sName = rInfo.aModule + "/" + sName;
    SvtViewOptions aWinOpt(EViewType::Window, sName);
    aWinOpt.SetWindowState(rInfo.aWinState);

    css::uno::Sequence < css::beans::NamedValue > aSeq
        { { u"Data"_ustr, css::uno::Any(aWinData) } };
    aWinOpt.SetUserData( aSeq );

    // ... but save status at runtime!
    m_pImpl->aFact.aInfo = rInfo;
}

void SfxChildWindow::SetAlignment(SfxChildAlignment eAlign) { m_eChildAlignment = eAlign; }

SfxChildWinInfo SfxChildWindow::GetInfo() const
{
    SfxChildWinInfo aInfo(m_pImpl->aFact.aInfo);
    if (m_xController)
    {
        weld::Dialog* pDialog = m_xController->getDialog();
        aInfo.aPos  = pDialog->get_position();
        aInfo.aSize = pDialog->get_size();
        vcl::WindowDataMask nMask = vcl::WindowDataMask::Pos | vcl::WindowDataMask::State;
        if (pDialog->get_resizable())
            nMask |= vcl::WindowDataMask::Size;
        aInfo.aWinState = pDialog->get_window_state(nMask);
    }
    else if (m_pWindow)
    {
        aInfo.aPos = m_pWindow->GetPosPixel();
        aInfo.aSize = m_pWindow->GetSizePixel();
        if (m_pWindow->IsSystemWindow())
        {
            vcl::WindowDataMask nMask = vcl::WindowDataMask::Pos | vcl::WindowDataMask::State;
            if (m_pWindow->GetStyle() & WB_SIZEABLE)
                nMask |= vcl::WindowDataMask::Size;
            aInfo.aWinState = static_cast<SystemWindow*>(m_pWindow.get())->GetWindowState(nMask);
        }
        else if (DockingWindow* pDockingWindow = dynamic_cast<DockingWindow*>(m_pWindow.get()))
        {
            if (pDockingWindow->GetFloatingWindow())
                aInfo.aWinState = pDockingWindow->GetFloatingWindow()->GetWindowState();
            else if (SfxDockingWindow* pSfxDockingWindow = dynamic_cast<SfxDockingWindow*>(pDockingWindow))
            {
                SfxChildWinInfo aTmpInfo;
                pSfxDockingWindow->FillInfo( aTmpInfo );
                aInfo.aExtraString = aTmpInfo.aExtraString;
            }
        }
    }

    aInfo.bVisible = m_pImpl->bVisible;
    aInfo.nFlags = SfxChildWindowFlags::NONE;
    return aInfo;
}

sal_uInt16 SfxChildWindow::GetPosition() const { return m_pImpl->aFact.nPos; }

void SfxChildWindow::InitializeChildWinFactory_Impl(sal_uInt16 nId, SfxChildWinInfo& rInfo)
{
    // load configuration

    std::optional<SvtViewOptions> xWinOpt;
    // first see if a module specific id exists
    if (rInfo.aModule.getLength())
        xWinOpt.emplace(EViewType::Window, rInfo.aModule + "/" + OUString::number(nId));

    // if not then try the generic id
    if (!xWinOpt || !xWinOpt->Exists())
        xWinOpt.emplace(EViewType::Window, OUString::number(nId));

    if (xWinOpt->Exists() && xWinOpt->HasVisible() )
        rInfo.bVisible  = xWinOpt->IsVisible(); // set state from configuration. Can be overwritten by UserData, see below

    css::uno::Sequence < css::beans::NamedValue > aSeq = xWinOpt->GetUserData();

    OUString aTmp;
    if ( aSeq.hasElements() )
        aSeq[0].Value >>= aTmp;

    OUString aWinData( aTmp );
    rInfo.aWinState = xWinOpt->GetWindowState();

    if ( aWinData.isEmpty() )
        return;

    // Search for version ID
    if ( aWinData[0] != 0x0056 ) // 'V' = 56h
        // A version ID, so do not use
        return;

    // Delete 'V'
    aWinData = aWinData.copy(1);

    // Read version
    char cToken = ',';
    sal_Int32 nPos = aWinData.indexOf( cToken );
    sal_uInt16 nActVersion = static_cast<sal_uInt16>(o3tl::toInt32(aWinData.subView( 0, nPos + 1 )));
    if ( nActVersion != nVersion )
        return;

    aWinData = aWinData.copy(nPos+1);

    // Load Visibility: is coded as a char
    rInfo.bVisible = (aWinData[0] == 0x0056); // 'V' = 56h
    aWinData = aWinData.copy(1);
    nPos = aWinData.indexOf( cToken );
    if (nPos == -1)
        return;

    sal_Int32 nNextPos = aWinData.indexOf( cToken, 2 );
    if ( nNextPos != -1 )
    {
        // there is extra information
        rInfo.nFlags = static_cast<SfxChildWindowFlags>(static_cast<sal_uInt16>(o3tl::toInt32(aWinData.subView( nPos+1, nNextPos - nPos - 1 ))));
        aWinData = aWinData.replaceAt( nPos, nNextPos-nPos+1, u"" );
        rInfo.aExtraString = aWinData;
    }
    else
        rInfo.nFlags = static_cast<SfxChildWindowFlags>(static_cast<sal_uInt16>(o3tl::toInt32(aWinData.subView( nPos+1 ))));
}

bool ParentIsFloatingWindow(const vcl::Window *pParent)
{
    if (!pParent)
        return false;
    if (pParent->GetType() == WindowType::DOCKINGWINDOW || pParent->GetType() == WindowType::TOOLBOX)
        return static_cast<const DockingWindow*>(pParent)->GetFloatingWindow() != nullptr;
    if (pParent->GetType() == WindowType::FLOATINGWINDOW)
        return true;
    return false;
}

void SfxChildWindow::SetFactory_Impl(const SfxChildWinFactory* pF) { m_pImpl->aFact = *pF; }

void SfxChildWindow::SetHideNotDelete(bool bOn) { m_pImpl->bHideNotDelete = bOn; }

bool SfxChildWindow::IsHideNotDelete() const { return m_pImpl->bHideNotDelete; }

void SfxChildWindow::SetWantsFocus(bool bSet) { m_pImpl->bWantsFocus = bSet; }

bool SfxChildWindow::WantsFocus() const { return m_pImpl->bWantsFocus; }

bool SfxChildWinInfo::GetExtraData_Impl
(
    SfxChildAlignment   *pAlign
)   const
{
    // invalid?
    if ( aExtraString.isEmpty() )
        return false;
    OUString aStr;
    sal_Int32 nPos = aExtraString.indexOf("AL:");
    if ( nPos == -1 )
        return false;

    // Try to read the alignment string "ALIGN :(...)", but if
    // it is not present, then use an older version
    sal_Int32 n1 = aExtraString.indexOf('(', nPos);
    if ( n1 != -1 )
    {
        sal_Int32 n2 = aExtraString.indexOf(')', n1);
        if ( n2 != -1 )
        {
            // Cut out Alignment string
            aStr = aExtraString.copy(nPos, n2 - nPos + 1);
            aStr = aStr.replaceAt(nPos, n1-nPos+1, u"");
        }
    }

    // First extract the Alignment
    if ( aStr.isEmpty() )
        return false;
    if ( pAlign )
        *pAlign = static_cast<SfxChildAlignment>(static_cast<sal_uInt16>(aStr.toInt32()));

    // then the LastAlignment
    nPos = aStr.indexOf(',');
    if ( nPos == -1 )
        return false;
    aStr = aStr.copy(nPos+1);

    // Then the splitting information
    nPos = aStr.indexOf(',');
    if ( nPos == -1 )
        // No docking in a Splitwindow
        return true;
    aStr = aStr.copy(nPos+1);
    Point aChildPos;
    Size aChildSize;
    return GetPosSizeFromString( aStr, aChildPos, aChildSize );
}

bool SfxChildWindow::IsVisible() const { return m_pImpl->bVisible; }

void SfxChildWindow::SetVisible_Impl(bool bVis) { m_pImpl->bVisible = bVis; }

void SfxChildWindow::Hide()
{
    if (m_xController)
        m_xController->EndDialog(nCloseResponseToJustHide);
    else
        m_pWindow->Hide();
}

void SfxChildWindow::Show( ShowFlags nFlags )
{
    if (m_xController)
    {
        if (!m_xController->getDialog()->get_visible())
        {
            if (!m_xController->CloseOnHide())
            {
                // tdf#155708 - do not run a new (Async) validation window,
                // because we already have one in sync mode, just show the running one
                m_xController->getDialog()->show();
            }
            else
            {
                weld::DialogController::runAsync(m_xController,
                                                 [this](sal_Int32 nResult)
                                                 {
                                                     if (nResult == nCloseResponseToJustHide)
                                                         return;
                                                     m_xController->Close();
                                                 });
            }
        }
    }
    else
        m_pWindow->Show(true, nFlags);
}

void SfxChildWindow::SetWorkWindow_Impl( SfxWorkWindow* pWin )
{
    m_pImpl->pWorkWin = pWin;
    if (pWin)
    {
        if ((m_xController && m_xController->getDialog()->has_toplevel_focus())
            || (m_pWindow && m_pWindow->HasChildPathFocus()))
        {
            m_pImpl->pWorkWin->SetActiveChild_Impl(m_pWindow);
        }
    }
}

void SfxChildWindow::Activate_Impl()
{
    if (m_pImpl->pWorkWin != nullptr)
        m_pImpl->pWorkWin->SetActiveChild_Impl(m_pWindow);
}

bool SfxChildWindow::QueryClose()
{
    bool bAllow = true;

    if (m_pImpl->xFrame.is())
    {
        css::uno::Reference<css::frame::XController> xCtrl = m_pImpl->xFrame->getController();
        if ( xCtrl.is() )
            bAllow = xCtrl->suspend( true );
    }

    if ( bAllow )
    {
        if (GetController())
        {
            weld::Dialog* pDialog = GetController()->getDialog();
            bAllow = !pDialog->get_visible() || !pDialog->get_modal();
        }
        else if (GetWindow())
            bAllow = !GetWindow()->IsInModalMode();
    }

    return bAllow;
}

const css::uno::Reference< css::frame::XFrame >&  SfxChildWindow::GetFrame() const
{
    return m_pImpl->xFrame;
}

void SfxChildWindow::SetFrame( const css::uno::Reference< css::frame::XFrame > & rFrame )
{
    // Do nothing if nothing will be changed ...
    if (m_pImpl->xFrame == rFrame)
        return;

    // ... but stop listening on old frame, if connection exist!
    if (m_pImpl->xFrame.is())
        m_pImpl->xFrame->removeEventListener(m_pImpl->xListener);

    // If new frame is not NULL -> we must guarantee valid listener for disposing events.
    // Use already existing or create new one.
    if( rFrame.is() )
        if (!m_pImpl->xListener.is())
            m_pImpl->xListener.set(new DisposeListener(this, m_pImpl.get()));

    // Set new frame in data container
    // and build new listener connection, if necessary.
    m_pImpl->xFrame = rFrame;
    if (m_pImpl->xFrame.is())
        m_pImpl->xFrame->addEventListener(m_pImpl->xListener);
}

void SfxChildWindow::RegisterChildWindow(SfxModule* pMod, const SfxChildWinFactory& rFact)
{
    SfxGetpApp()->RegisterChildWindow_Impl( pMod, rFact );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
