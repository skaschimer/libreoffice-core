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

#include <config_feature_desktop.h>

#ifdef IOS
#include <premac.h>
#include <UIKit/UIKit.h>
#include <postmac.h>
#endif

#include <com/sun/star/frame/theGlobalEventBroadcaster.hpp>
#include <comphelper/lok.hxx>
#include <comphelper/scopeguard.hxx>
#include <comphelper/processfactory.hxx>
#include <o3tl/test_info.hxx>
#include <officecfg/Office/Common.hxx>
#include <osl/diagnose.h>

#include <svdata.hxx>
#include <window.h>
#include <accel.hxx>
#include <brdwin.hxx>
#include <salinst.hxx>

#include <rtl/bootstrap.hxx>
#include <rtl/strbuf.hxx>
#include <sal/log.hxx>

#include <vcl/abstdlg.hxx>
#include <vcl/builder.hxx>
#include <vcl/toolkit/floatwin.hxx>
#include <vcl/layout.hxx>
#include <vcl/svapp.hxx>
#include <vcl/event.hxx>
#include <vcl/locktoplevels.hxx>
#include <vcl/wrkwin.hxx>
#include <vcl/toolkit/button.hxx>
#include <vcl/mnemonic.hxx>
#include <vcl/toolkit/dialog.hxx>
#include <vcl/dialoghelper.hxx>
#include <vcl/settings.hxx>
#include <vcl/virdev.hxx>
#include <vcl/weld.hxx>
#include <vcl/uitest/uiobject.hxx>
#include <vcl/uitest/logger.hxx>
#include <vcl/IDialogRenderable.hxx>
#include <messagedialog.hxx>
#include <salframe.hxx>
#include <tools/json_writer.hxx>

#include <iostream>
#include <stack>
#include <utility>
#include <vector>

static OUString ImplGetDialogText(Dialog* pDialog)
{
    OUString aErrorStr(pDialog->GetText());

    if (MessageDialog* pMessDialog = dynamic_cast<MessageDialog*>(pDialog))
    {
        const OUString sMessage = pMessDialog->get_primary_text();
        if (!sMessage.isEmpty())
            aErrorStr += ", " + sMessage;
    }

    return aErrorStr;
}

static bool ImplIsMnemonicCtrl( vcl::Window* pWindow )
{
    if( ! pWindow->GetSettings().GetStyleSettings().GetAutoMnemonic() )
        return false;

    if ( (pWindow->GetType() == WindowType::RADIOBUTTON) ||
         (pWindow->GetType() == WindowType::CHECKBOX) ||
         (pWindow->GetType() == WindowType::TRISTATEBOX) ||
         (pWindow->GetType() == WindowType::PUSHBUTTON) )
        return true;

    if ( pWindow->GetType() == WindowType::FIXEDTEXT )
    {
        FixedText *pText = static_cast<FixedText*>(pWindow);
        if (pText->get_mnemonic_widget())
            return true;
        //This is the legacy pre-layout logic which we retain
        //until we can be sure we can remove it
        if (pWindow->GetStyle() & WB_NOLABEL)
            return false;
        vcl::Window* pNextWindow = pWindow->GetWindow( GetWindowType::Next );
        if ( !pNextWindow )
            return false;
        pNextWindow = pNextWindow->GetWindow( GetWindowType::Client );
        return !(!(pNextWindow->GetStyle() & WB_TABSTOP) ||
                 (pNextWindow->GetType() == WindowType::FIXEDTEXT) ||
                 (pNextWindow->GetType() == WindowType::GROUPBOX) ||
                 (pNextWindow->GetType() == WindowType::RADIOBUTTON) ||
                 (pNextWindow->GetType() == WindowType::CHECKBOX) ||
                 (pNextWindow->GetType() == WindowType::TRISTATEBOX) ||
                 (pNextWindow->GetType() == WindowType::PUSHBUTTON));
    }

    return false;
}

// Called by native error dialog popup implementations
void ImplHideSplash()
{
    ImplSVData* pSVData = ImplGetSVData();
    if( pSVData->mpIntroWindow )
            pSVData->mpIntroWindow->Hide();
}

vcl::Window * nextLogicalChildOfParent(const vcl::Window *pTopLevel, const vcl::Window *pChild)
{
    const vcl::Window *pLastChild = pChild;

    if (pChild->GetType() == WindowType::SCROLLWINDOW)
        pChild = static_cast<const VclScrolledWindow*>(pChild)->get_child();
    else if (isContainerWindow(*pChild))
        pChild = pChild->GetWindow(GetWindowType::FirstChild);
    else
        pChild = pChild->GetWindow(GetWindowType::Next);

    while (!pChild)
    {
        vcl::Window *pParent = pLastChild->GetParent();
        if (!pParent)
            return nullptr;
        if (pParent == pTopLevel)
            return nullptr;
        pLastChild = pParent;
        pChild = pParent->GetWindow(GetWindowType::Next);
    }

    if (isContainerWindow(*pChild))
        pChild = nextLogicalChildOfParent(pTopLevel, pChild);

    return const_cast<vcl::Window *>(pChild);
}

vcl::Window * prevLogicalChildOfParent(const vcl::Window *pTopLevel, const vcl::Window *pChild)
{
    const vcl::Window *pLastChild = pChild;

    if (pChild->GetType() == WindowType::SCROLLWINDOW)
        pChild = static_cast<const VclScrolledWindow*>(pChild)->get_child();
    else if (isContainerWindow(*pChild))
        pChild = pChild->GetWindow(GetWindowType::LastChild);
    else
        pChild = pChild->GetWindow(GetWindowType::Prev);

    while (!pChild)
    {
        vcl::Window *pParent = pLastChild->GetParent();
        if (!pParent)
            return nullptr;
        if (pParent == pTopLevel)
            return nullptr;
        pLastChild = pParent;
        pChild = pParent->GetWindow(GetWindowType::Prev);
    }

    if (isContainerWindow(*pChild))
        pChild = prevLogicalChildOfParent(pTopLevel, pChild);

    return const_cast<vcl::Window *>(pChild);
}

vcl::Window * firstLogicalChildOfParent(const vcl::Window *pTopLevel)
{
    const vcl::Window *pChild = pTopLevel->GetWindow(GetWindowType::FirstChild);
    if (pChild && isContainerWindow(*pChild))
        pChild = nextLogicalChildOfParent(pTopLevel, pChild);
    return const_cast<vcl::Window *>(pChild);
}

vcl::Window * lastLogicalChildOfParent(const vcl::Window *pTopLevel)
{
    const vcl::Window *pChild = pTopLevel->GetWindow(GetWindowType::LastChild);
    if (pChild && isContainerWindow(*pChild))
        pChild = prevLogicalChildOfParent(pTopLevel, pChild);
    return const_cast<vcl::Window *>(pChild);
}

void GenerateAutoMnemonicsOnHierarchy(const vcl::Window* pWindow)
{
    MnemonicGenerator   aMnemonicGenerator;
    vcl::Window*                 pGetChild;
    vcl::Window*                 pChild;

    // register the assigned mnemonics
    pGetChild = pWindow->GetWindow( GetWindowType::FirstChild );
    while ( pGetChild )
    {
        pChild = pGetChild->ImplGetWindow();
        aMnemonicGenerator.RegisterMnemonic( pChild->GetText() );
        pGetChild = nextLogicalChildOfParent(pWindow, pGetChild);
    }

    // take the Controls of the dialog into account for TabPages
    if ( pWindow->GetType() == WindowType::TABPAGE )
    {
        vcl::Window* pParent = pWindow->GetParent();
        if (pParent && pParent->GetType() == WindowType::TABCONTROL )
            pParent = pParent->GetParent();

        if (pParent && (pParent->GetStyle() & (WB_DIALOGCONTROL | WB_NODIALOGCONTROL)) == WB_DIALOGCONTROL )
        {
            pGetChild = pParent->GetWindow( GetWindowType::FirstChild );
            while ( pGetChild )
            {
                pChild = pGetChild->ImplGetWindow();
                aMnemonicGenerator.RegisterMnemonic( pChild->GetText() );
                pGetChild = nextLogicalChildOfParent(pWindow, pGetChild);
            }
        }
    }

    // assign mnemonics to Controls which have none
    pGetChild = pWindow->GetWindow( GetWindowType::FirstChild );
    while ( pGetChild )
    {
        pChild = pGetChild->ImplGetWindow();
        if ( ImplIsMnemonicCtrl( pChild ) )
        {
            OUString aText = pChild->GetText();
            OUString aNewText = aMnemonicGenerator.CreateMnemonic( aText );
            if ( aText != aNewText )
                pChild->SetText( aNewText );
        }

        pGetChild = nextLogicalChildOfParent(pWindow, pGetChild);
    }
}

static VclButtonBox* getActionArea(Dialog const *pDialog)
{
    VclButtonBox *pButtonBox = nullptr;
    if (pDialog->isLayoutEnabled())
    {
        vcl::Window *pBox = pDialog->GetWindow(GetWindowType::FirstChild);
        vcl::Window *pChild = pBox->GetWindow(GetWindowType::LastChild);
        while (pChild)
        {
            pButtonBox = dynamic_cast<VclButtonBox*>(pChild);
            if (pButtonBox)
                break;
            pChild = pChild->GetWindow(GetWindowType::Prev);
        }
    }
    return pButtonBox;
}

static vcl::Window* getActionAreaButtonList(Dialog const *pDialog)
{
    VclButtonBox* pButtonBox = getActionArea(pDialog);
    if (pButtonBox)
        return pButtonBox->GetWindow(GetWindowType::FirstChild);
    return pDialog->GetWindow(GetWindowType::FirstChild);
}

static PushButton* ImplGetDefaultButton( Dialog const * pDialog )
{
    vcl::Window* pChild = getActionAreaButtonList(pDialog);
    while ( pChild )
    {
        if ( pChild->ImplIsPushButton() )
        {
            PushButton* pPushButton = static_cast<PushButton*>(pChild);
            if ( pPushButton->ImplIsDefButton() )
                return pPushButton;
        }

        pChild = pChild->GetWindow( GetWindowType::Next );
    }

    return nullptr;
}

static PushButton* ImplGetOKButton( Dialog const * pDialog )
{
    vcl::Window* pChild = getActionAreaButtonList(pDialog);
    while ( pChild )
    {
        if ( pChild->GetType() == WindowType::OKBUTTON )
            return static_cast<PushButton*>(pChild);

        pChild = pChild->GetWindow( GetWindowType::Next );
    }

    return nullptr;
}

static PushButton* ImplGetCancelButton( Dialog const * pDialog )
{
    vcl::Window* pChild = getActionAreaButtonList(pDialog);

    while ( pChild )
    {
        if ( pChild->GetType() == WindowType::CANCELBUTTON )
            return static_cast<PushButton*>(pChild);

        pChild = pChild->GetWindow( GetWindowType::Next );
    }

    return nullptr;
}

static void ImplMouseAutoPos( Dialog* pDialog )
{
    MouseSettingsOptions nMouseOptions = pDialog->GetSettings().GetMouseSettings().GetOptions();
    if ( nMouseOptions & MouseSettingsOptions::AutoCenterPos )
    {
        Size aSize = pDialog->GetOutputSizePixel();
        pDialog->SetPointerPosPixel( Point( aSize.Width()/2, aSize.Height()/2 ) );
    }
    else if ( nMouseOptions & MouseSettingsOptions::AutoDefBtnPos )
    {
        vcl::Window* pWindow = ImplGetDefaultButton( pDialog );
        if ( !pWindow )
            pWindow = ImplGetOKButton( pDialog );
        if ( !pWindow )
            pWindow = ImplGetCancelButton( pDialog );
        if ( !pWindow )
            pWindow = pDialog;
        Size aSize = pWindow->GetOutputSizePixel();
        pWindow->SetPointerPosPixel( Point( aSize.Width()/2, aSize.Height()/2 ) );
    }
}

struct DialogImpl
{
    std::vector<VclPtr<PushButton>> maOwnedButtons;
    std::map<VclPtr<vcl::Window>, short> maResponses;
    tools::Long    mnResult;
    bool    mbStartedModal;
    VclAbstractDialog::AsyncContext maEndCtx;
    Link<const CommandEvent&, bool> m_aPopupMenuHdl;
    Link<void*, vcl::ILibreOfficeKitNotifier*> m_aInstallLOKNotifierHdl;
    bool    m_bLOKTunneling;

    DialogImpl() : mnResult( -1 ), mbStartedModal( false ), m_bLOKTunneling( true ) {}

#ifndef NDEBUG
    short get_response(vcl::Window *pWindow) const
    {
        auto aFind = maResponses.find(pWindow);
        if (aFind != maResponses.end())
            return aFind->second;
        return RET_CANCEL;
    }
#endif

    ~DialogImpl()
    {
        disposeAndClear();
    }

    void disposeAndClear()
    {
        for (VclPtr<PushButton> & pOwnedButton : maOwnedButtons)
            pOwnedButton.disposeAndClear();
    }
};

void Dialog::disposeOwnedButtons()
{
    for (VclPtr<PushButton> & pOwnedButton : mpDialogImpl->maOwnedButtons)
        pOwnedButton.disposeAndClear();
}

void Dialog::ImplInitDialogData()
{
    mpWindowImpl->mbDialog  = true;
    mbInExecute             = false;
    mbInSyncExecute         = false;
    mbInClose               = false;
    mbModalMode             = false;
    mpContentArea.reset();
    mpActionArea.reset();
    mnMousePositioned       = 0;
    mpDialogImpl.reset(new DialogImpl);
}

void Dialog::PixelInvalidate(const tools::Rectangle* pRectangle)
{
    if (!mpDialogImpl->m_bLOKTunneling)
        return;

    Window::PixelInvalidate(pRectangle);
}

vcl::Window* Dialog::GetDefaultParent(WinBits nStyle)
{
    vcl::Window* pParent = Dialog::GetDefDialogParent();
    if (!pParent && !(nStyle & WB_SYSTEMWINDOW))
        pParent = ImplGetSVData()->maFrameData.mpAppWin;

    // If Parent is disabled, then we search for a modal dialog
    // in this frame
    if (pParent && (!pParent->IsInputEnabled() || pParent->IsInModalMode()))
    {
        ImplSVData* pSVData = ImplGetSVData();
        auto& rExecuteDialogs = pSVData->mpWinData->mpExecuteDialogs;
        auto it = std::find_if(rExecuteDialogs.rbegin(), rExecuteDialogs.rend(),
            [&pParent](VclPtr<Dialog>& rDialogPtr) {
                return pParent->ImplGetFirstOverlapWindow() &&
                    pParent->ImplGetFirstOverlapWindow()->IsWindowOrChild(rDialogPtr, true) &&
                    rDialogPtr->IsReallyVisible() && rDialogPtr->IsEnabled() &&
                    rDialogPtr->IsInputEnabled() && !rDialogPtr->IsInModalMode(); });
        if (it != rExecuteDialogs.rend())
            pParent = it->get();
    }

    return pParent;
}

VclPtr<vcl::Window> Dialog::AddBorderWindow(vcl::Window* pParent, WinBits nStyle)
{
    VclPtrInstance<ImplBorderWindow> pBorderWin( pParent, nStyle, BorderWindowStyle::Frame );
    ImplInit( pBorderWin, nStyle & ~WB_BORDER, nullptr );
    pBorderWin->mpWindowImpl->mpClientWindow = this;
    pBorderWin->GetBorder( mpWindowImpl->mnLeftBorder, mpWindowImpl->mnTopBorder, mpWindowImpl->mnRightBorder, mpWindowImpl->mnBottomBorder );
    mpWindowImpl->mpBorderWindow  = pBorderWin;
    mpWindowImpl->mpRealParent    = pParent;

    return pBorderWin;
}

void Dialog::ImplInitDialog( vcl::Window* pParent, WinBits nStyle, InitFlag eFlag )
{
    SystemWindowFlags nSysWinMode = Application::GetSystemWindowMode();

    if ( !(nStyle & WB_NODIALOGCONTROL) )
        nStyle |= WB_DIALOGCONTROL;

    // Now, all Dialogs are per default system windows !!!
    nStyle |= WB_SYSTEMWINDOW;

    if (InitFlag::NoParent == eFlag)
    {
        pParent = nullptr;
    }
    else if (!pParent) // parent is NULL: get the default Dialog parent
    {
        pParent = Dialog::GetDefaultParent(nStyle);
    }

    if ( !pParent || (nStyle & WB_SYSTEMWINDOW) ||
         (pParent->mpWindowImpl->mpFrameData->mbNeedSysWindow && !(nSysWinMode & SystemWindowFlags::NOAUTOMODE)) ||
         (nSysWinMode & SystemWindowFlags::DIALOG) )
    {
        // create window with a small border ?
        if ((nStyle & WB_ALLOWMENUBAR) || ((nStyle & (WB_BORDER | WB_NOBORDER | WB_MOVEABLE | WB_SIZEABLE | WB_CLOSEABLE)) == WB_BORDER))
        {
            AddBorderWindow(pParent, nStyle);
        }
        else
        {
            mpWindowImpl->mbFrame         = true;
            mpWindowImpl->mbOverlapWin    = true;
            ImplInit( pParent, (nStyle & (WB_MOVEABLE | WB_SIZEABLE | WB_STANDALONE)) | WB_CLOSEABLE, nullptr );
            // Now set all style bits
            mpWindowImpl->mnStyle = nStyle;
        }
    }
    else
    {
        VclPtrInstance<ImplBorderWindow> pBorderWin( pParent, nStyle, BorderWindowStyle::Overlap );
        ImplInit( pBorderWin, nStyle & ~WB_BORDER, nullptr );
        pBorderWin->mpWindowImpl->mpClientWindow = this;
        pBorderWin->GetBorder( mpWindowImpl->mnLeftBorder, mpWindowImpl->mnTopBorder, mpWindowImpl->mnRightBorder, mpWindowImpl->mnBottomBorder );
        mpWindowImpl->mpBorderWindow  = pBorderWin;
        mpWindowImpl->mpRealParent    = pParent;
    }

    SetActivateMode( ActivateModeFlags::GrabFocus );

    ImplInitSettings();
}

void Dialog::ApplySettings(vcl::RenderContext& rRenderContext)
{
    if (IsControlBackground())
    {
        // user override
        SetBackground(GetControlBackground());
    }
    else if (rRenderContext.IsNativeControlSupported(ControlType::WindowBackground, ControlPart::BackgroundDialog))
    {
        // NWF background
        mpWindowImpl->mnNativeBackground = ControlPart::BackgroundDialog;
        EnableChildTransparentMode();
    }
    else
    {
        // fallback to settings color
        rRenderContext.SetBackground(GetSettings().GetStyleSettings().GetDialogColor());
    }
}

void Dialog::ImplInitSettings()
{
    // user override
    if (IsControlBackground())
        SetBackground(GetControlBackground());
    // NWF background
    else if( IsNativeControlSupported(ControlType::WindowBackground, ControlPart::BackgroundDialog))
    {
        mpWindowImpl->mnNativeBackground = ControlPart::BackgroundDialog;
        EnableChildTransparentMode();
    }
    // fallback to settings color
    else
        SetBackground(GetSettings().GetStyleSettings().GetDialogColor());
}

void Dialog::ImplLOKNotifier(vcl::Window* pParent)
{
    if (comphelper::LibreOfficeKit::isActive() && pParent)
    {
        if (VclPtr<vcl::Window> pWin = pParent->GetParentWithLOKNotifier())
        {
            SetLOKNotifier(pWin->GetLOKNotifier());
        }
    }
}

Dialog::Dialog( WindowType eType )
    : SystemWindow( eType, "vcl::Dialog maLayoutIdle", true )
    , mnInitFlag(InitFlag::Default)
{
    ImplInitDialogData();
}

void VclBuilderContainer::disposeBuilder()
{
    if (m_pUIBuilder)
        m_pUIBuilder->disposeBuilder();
}

OUString AllSettings::GetUIRootDir()
{
    OUString sShareLayer(u"$BRAND_BASE_DIR/$BRAND_SHARE_SUBDIR/config/soffice.cfg/"_ustr);
    rtl::Bootstrap::expandMacros(sShareLayer);
    return sShareLayer;
}

//we can't change sizeable after the fact, so need to defer until we know and then do the init.
void Dialog::ImplDeferredInit(vcl::Window* pParent, WinBits nBits)
{
    ImplInitDialog(pParent, nBits | WB_BORDER, mnInitFlag);
}

Dialog::Dialog(vcl::Window* pParent, const OUString& rID, const OUString& rUIXMLDescription)
    : SystemWindow(WindowType::DIALOG, "vcl::Dialog maLayoutIdle", true)
    , mnInitFlag(InitFlag::Default)
{
    ImplLOKNotifier(pParent);
    ImplInitDialogData();
    loadUI(pParent, rID, rUIXMLDescription);
}

Dialog::Dialog(vcl::Window* pParent, WinBits nStyle, InitFlag eFlag)
    : SystemWindow(WindowType::DIALOG, "vcl::Dialog maLayoutIdle", true)
    , mnInitFlag(eFlag)
{
    ImplLOKNotifier(pParent);
    ImplInitDialogData();
    ImplInitDialog( pParent, nStyle, eFlag );
}

void Dialog::set_action_area(VclButtonBox* pBox)
{
    mpActionArea.reset(pBox);
    if (pBox)
    {
        const DialogStyle& rDialogStyle =
            GetSettings().GetStyleSettings().GetDialogStyle();
        pBox->set_border_width(rDialogStyle.action_area_border);
    }
}

void Dialog::set_content_area(VclBox* pBox)
{
    mpContentArea.reset(pBox);
}

void Dialog::settingOptimalLayoutSize(Window *pBox)
{
    const DialogStyle& rDialogStyle =
        GetSettings().GetStyleSettings().GetDialogStyle();
    VclBox * pBox2 = static_cast<VclBox*>(pBox);
    pBox2->set_border_width(rDialogStyle.content_area_border);
}

Dialog::~Dialog()
{
    disposeOnce();
}

void Dialog::dispose()
{
    bool bTunnelingEnabled = mpDialogImpl->m_bLOKTunneling;

    mpDialogImpl->disposeAndClear();
    RemoveFromDlgList();
    mpActionArea.reset();
    mpContentArea.reset();

    const css::uno::Reference< css::uno::XComponentContext >& xContext(
            comphelper::getProcessComponentContext() );
    css::uno::Reference<css::frame::XGlobalEventBroadcaster> xEventBroadcaster(css::frame::theGlobalEventBroadcaster::get(xContext), css::uno::UNO_SET_THROW);
    css::document::DocumentEvent aObject;
    aObject.EventName = "DialogClosed";
    aObject.Supplement <<= GetText(); // title
    xEventBroadcaster->documentEventOccured(aObject);
    UITestLogger::getInstance().log(u"Close Dialog");

    if (comphelper::LibreOfficeKit::isActive())
    {
        if(const vcl::ILibreOfficeKitNotifier* pNotifier = GetLOKNotifier())
        {
            if (bTunnelingEnabled)
                pNotifier->notifyWindow(GetLOKWindowId(), u"close"_ustr);
            ReleaseLOKNotifier();
        }
    }

    SystemWindow::dispose();
}

IMPL_LINK_NOARG(Dialog, ImplAsyncCloseHdl, void*, void)
{
    Close();
}

bool Dialog::EventNotify( NotifyEvent& rNEvt )
{
    // first call the base class due to Tab control
    bool bRet = SystemWindow::EventNotify( rNEvt );
    if ( !bRet )
    {
        if ( rNEvt.GetType() == NotifyEventType::KEYINPUT )
        {
            const KeyEvent* pKEvt = rNEvt.GetKeyEvent();
            vcl::KeyCode    aKeyCode = pKEvt->GetKeyCode();
            sal_uInt16      nKeyCode = aKeyCode.GetCode();

            if ( (nKeyCode == KEY_ESCAPE) &&
                 ((GetStyle() & WB_CLOSEABLE) || ImplGetCancelButton( this ) || ImplGetOKButton( this )) )
            {
                // #i89505# for the benefit of slightly mentally challenged implementations
                // like e.g. SfxModelessDialog which destroy themselves inside Close()
                // post this Close asynchronous so we can leave our key handler before
                // we get destroyed
                PostUserEvent( LINK( this, Dialog, ImplAsyncCloseHdl ), nullptr, true);
                return true;
            }
        }
        else if ( rNEvt.GetType() == NotifyEventType::GETFOCUS )
        {
            // make sure the dialog is still modal
            // changing focus between application frames may
            // have re-enabled input for our parent
            if( mbInExecute && mbModalMode )
            {
                ImplSetModalInputMode( false );
                ImplSetModalInputMode( true );

                // #93022# def-button might have changed after show
                if( !mnMousePositioned )
                {
                    mnMousePositioned = 1;
                    ImplMouseAutoPos( this );
                }

            }
        }
    }

    return bRet;
}

//What we really want here is something that gives the available width and
//height of a users screen, taking away the space taken up the OS
//taskbar, menus, etc.
Size bestmaxFrameSizeForScreenSize(const Size &rScreenSize)
{
#ifndef IOS
    tools::Long w = rScreenSize.Width();
    if (w <= 800)
        w -= 15;
    else if (w <= 1024)
        w -= 65;
    else
        w -= 115;

    tools::Long h = rScreenSize.Height();
    if (h <= 768)
        h -= 50;
    else
        h -= 100;

    return Size(std::max<tools::Long>(w, 640 - 15),
                std::max<tools::Long>(h, 480 - 50));
#else
    // Don't bother with ancient magic numbers of unclear relevance on non-desktop apps anyway. It
    // seems that at least currently in the iOS app, this function is called just once per dialog,
    // with a rScreenSize parameter of 1x1 (!). This would lead to always returning 625x430 which is
    // a bit random and needlessly small on an iPad at least. We want something that closely will
    // just fit on the display in either orientation.

    // We ignore the rScreenSize as it will be the dummy 1x1 from iosinst.cxx (see "Totally wrong of course").
    (void) rScreenSize;

    const int n = std::min<CGFloat>([[UIScreen mainScreen] bounds].size.width, [[UIScreen mainScreen] bounds].size.height);
    return Size(n-10, n-10);
#endif
}

void Dialog::SetPopupMenuHdl(const Link<const CommandEvent&, bool>& rLink)
{
    mpDialogImpl->m_aPopupMenuHdl = rLink;
}

void Dialog::SetInstallLOKNotifierHdl(const Link<void*, vcl::ILibreOfficeKitNotifier*>& rLink)
{
    mpDialogImpl->m_aInstallLOKNotifierHdl = rLink;
}

void Dialog::SetLOKTunnelingState(bool bEnabled)
{
    mpDialogImpl->m_bLOKTunneling = bEnabled;
}

void Dialog::StateChanged( StateChangedType nType )
{
    bool bTunnelingEnabled = mpDialogImpl->m_bLOKTunneling;

    if (nType == StateChangedType::InitShow)
    {
        DoInitialLayout();

        const bool bKitActive = comphelper::LibreOfficeKit::isActive();
        if (bKitActive && bTunnelingEnabled)
        {
            std::vector<vcl::LOKPayloadItem> aItems;
            aItems.emplace_back("type", "dialog");
            aItems.emplace_back("size", GetSizePixel().toString());
            aItems.emplace_back("unique_id", this->get_id().toUtf8());
            if (!GetText().isEmpty())
                aItems.emplace_back("title", GetText().toUtf8());

            if (const vcl::ILibreOfficeKitNotifier* pNotifier = GetLOKNotifier())
            {
                pNotifier->notifyWindow(GetLOKWindowId(), u"created"_ustr, aItems);
                pNotifier->notifyWindow(GetLOKWindowId(), u"created"_ustr, aItems);
            }
            else
            {
                vcl::ILibreOfficeKitNotifier* pViewShell = mpDialogImpl->m_aInstallLOKNotifierHdl.Call(nullptr);
                if (pViewShell)
                {
                    SetLOKNotifier(pViewShell);
                    pViewShell->notifyWindow(GetLOKWindowId(), u"created"_ustr, aItems);
                }
            }
        }

        if ( !HasChildPathFocus() || HasFocus() )
            GrabFocusToFirstControl();
        if ( !(GetStyle() & WB_CLOSEABLE) )
        {
            if ( ImplGetCancelButton( this ) || ImplGetOKButton( this ) )
            {
                if ( ImplGetBorderWindow() )
                    static_cast<ImplBorderWindow*>(ImplGetBorderWindow())->SetCloseButton();
            }
        }

        ImplMouseAutoPos( this );
    }
    else if (nType == StateChangedType::Text)
    {
        const vcl::ILibreOfficeKitNotifier* pNotifier = GetLOKNotifier();
        if (pNotifier && bTunnelingEnabled)
        {
            std::vector<vcl::LOKPayloadItem> aPayload;
            aPayload.emplace_back("title", GetText().toUtf8());
            pNotifier->notifyWindow(GetLOKWindowId(), u"title_changed"_ustr, aPayload);
        }
    }

    SystemWindow::StateChanged( nType );

    if (nType == StateChangedType::ControlBackground)
    {
        ImplInitSettings();
        Invalidate();
    }

    if (!mbModalMode && nType == StateChangedType::Visible)
    {
        const vcl::ILibreOfficeKitNotifier* pNotifier = GetLOKNotifier();
        if (pNotifier && bTunnelingEnabled)
        {
            std::vector<vcl::LOKPayloadItem> aPayload;
            aPayload.emplace_back("title", GetText().toUtf8());
            pNotifier->notifyWindow(GetLOKWindowId(), IsVisible()? u"show"_ustr: u"hide"_ustr, aPayload);
        }
    }
}

void Dialog::DataChanged( const DataChangedEvent& rDCEvt )
{
    SystemWindow::DataChanged( rDCEvt );

    if ( (rDCEvt.GetType() == DataChangedEventType::SETTINGS) &&
         (rDCEvt.GetFlags() & AllSettingsFlags::STYLE) )
    {
        ImplInitSettings();
        Invalidate();
    }
}

bool Dialog::Close()
{
    VclPtr<vcl::Window> xWindow = this;
    CallEventListeners( VclEventId::WindowClose );
    if ( xWindow->isDisposed() )
        return false;

    if ( mpWindowImpl->mxWindowPeer.is() && IsCreatedWithToolkit() && !IsInExecute() )
        return false;

    // If there's a cancel button with a custom handler, then always give it a chance to
    // handle Dialog::Close
    PushButton* pCustomCancelButton;
    PushButton* pCancelButton = dynamic_cast<PushButton*>(get_widget_for_response(RET_CANCEL));
    if (!mbInClose && pCancelButton && pCancelButton->GetClickHdl().IsSet())
        pCustomCancelButton = pCancelButton;
    else
        pCustomCancelButton = nullptr;

    mbInClose = true;

    if (pCustomCancelButton)
    {
        pCustomCancelButton->Click();
        if (xWindow->isDisposed())
            return true;
        mbInClose = false;
        return false;
    }

    if ( !(GetStyle() & WB_CLOSEABLE) )
    {
        bool bRet = true;
        PushButton* pButton = ImplGetCancelButton( this );
        if ( pButton )
            pButton->Click();
        else
        {
            pButton = ImplGetOKButton( this );
            if ( pButton )
                pButton->Click();
            else
                bRet = false;
        }
        if ( xWindow->isDisposed() )
            return true;
        return bRet;
    }

    if (IsInExecute() || mpDialogImpl->maEndCtx.isSet())
    {
        EndDialog();
        mbInClose = false;
        return true;
    }
    else
    {
        mbInClose = false;
        return SystemWindow::Close();
    }
}

bool Dialog::ImplStartExecute(bool async)
{
    setDeferredProperties();

    if (IsInExecute() || mpDialogImpl->maEndCtx.isSet())
    {
#ifdef DBG_UTIL
        SAL_WARN( "vcl", "Dialog::StartExecuteModal() is called in Dialog::StartExecuteModal(): "
                    << ImplGetDialogText(this) );
#endif
        return false;
    }

    ImplSVData* pSVData = ImplGetSVData();

    const bool bKitActive = comphelper::LibreOfficeKit::isActive();

    const bool bModal = GetType() != WindowType::MODELESSDIALOG;

    if (bModal)
    {
        if (bKitActive && !GetLOKNotifier())
        {
            if (auto pNotifier = mpDialogImpl->m_aInstallLOKNotifierHdl.Call(nullptr))
                SetLOKNotifier(pNotifier);
            else
            {
                // gh#5908 handle pasting disallowed clipboard contents on iOS
                // When another app owns the current clipboard contents, pasting
                // will display a "allow or disallow" dialog. If the disallow
                // option is selected, the data from the UIPasteboard will be
                // garbage and we will find ourselves here. Since calling
                // SetLOKNotifier() with a nullptr aborts in an assert(), fix
                // the crash by failing gracefully.

                // Also pNotifier may be nullptr when a dialog (e.g., "update
                // links?") is to be shown when loading a document.
                // Never crash in release builds (assume "cancel"), but allow
                // to see the not yet async / not properly set up dialogs in
                // debug builds.
                assert(!"A dialog without a notifier: make me async / properly set up");
                return false;
            }
        }
        if (!async && Application::IsUseSystemEventLoop())
        {
            // As long as Application::Yield deliberately calls std::abort when trying to
            // synchronously execute modal dialogs, better cancel them here for now as a hack:
            SAL_WARN(
                "vcl", "cancel synchronous execution of modal dialog " << ImplGetDialogText(this));
            return false;
        }

        switch ( Application::GetDialogCancelMode() )
        {
        case DialogCancelMode::Off:
            break;
        case DialogCancelMode::Silent:
            if (bModal && GetLOKNotifier())
            {
                // check if there's already some dialog being ::Execute()d
                const bool bDialogExecuting = std::any_of(pSVData->mpWinData->mpExecuteDialogs.begin(),
                                                          pSVData->mpWinData->mpExecuteDialogs.end(),
                                                          [](const Dialog* pDialog) {
                                                              return pDialog->IsInSyncExecute();
                                                          });
                if (!(bDialogExecuting && IsInSyncExecute()))
                    break;
                else
                    SAL_WARN("lok.dialog", "Dialog \"" << ImplGetDialogText(this) << "\" is being synchronously executed over an existing synchronously executing dialog.");
            }

            if (o3tl::IsRunningUnitTest())
            { // helps starbasic unit tests show their errors
                std::cerr << "Dialog \"" << ImplGetDialogText(this)
                          << "\" cancelled in silent mode\n";
            }

            SAL_INFO(
                "vcl",
                "Dialog \"" << ImplGetDialogText(this)
                    << "\" cancelled in silent mode");
            return false;

        case DialogCancelMode::LOKSilent:
            return false;

        default: // default cannot happen
        case DialogCancelMode::Fatal:
            std::abort();
        }

#ifdef DBG_UTIL
        vcl::Window* pParent = GetParent();
        if ( pParent )
        {
            pParent = pParent->ImplGetFirstOverlapWindow();
            if (pParent)
            {
                SAL_WARN_IF( !pParent->IsReallyVisible(), "vcl",
                            "Dialog::StartExecuteModal() - Parent not visible" );
                SAL_WARN_IF( !pParent->IsInputEnabled(), "vcl",
                            "Dialog::StartExecuteModal() - Parent input disabled, use another parent to ensure modality!" );
                SAL_WARN_IF(  pParent->IsInModalMode(), "vcl",
                            "Dialog::StartExecuteModal() - Parent already modally disabled, use another parent to ensure modality!" );
            }
        }
#endif

        // link all dialogs which are being executed
        pSVData->mpWinData->mpExecuteDialogs.push_back(this);

        // stop capturing, in order to have control over the dialog
        if (pSVData->mpWinData->mpTrackWin)
            pSVData->mpWinData->mpTrackWin->EndTracking(TrackingEventFlags::Cancel);
        if (pSVData->mpWinData->mpCaptureWin)
            pSVData->mpWinData->mpCaptureWin->ReleaseMouse();
        EnableInput();
    }

    mbInExecute = true;
    // no real modality in LibreOfficeKit
    if (!bKitActive && bModal)
        SetModalInputMode(true);

    // FIXME: no layouting, workaround some clipping issues
    ImplAdjustNWFSizes();

    const css::uno::Reference< css::uno::XComponentContext >& xContext(
        comphelper::getProcessComponentContext());
    bool bForceFocusAndToFront(officecfg::Office::Common::View::NewDocumentHandling::ForceFocusAndToFront::get());
    ShowFlags showFlags = bForceFocusAndToFront ? ShowFlags::ForegroundTask : ShowFlags::NONE;
    Show(true, showFlags);

    if (bModal)
        pSVData->maAppData.mnModalMode++;

    css::uno::Reference<css::frame::XGlobalEventBroadcaster> xEventBroadcaster(
        css::frame::theGlobalEventBroadcaster::get(xContext), css::uno::UNO_SET_THROW);
    css::document::DocumentEvent aObject;
    aObject.EventName = "DialogExecute";
    aObject.Supplement <<= GetText(); // title
    xEventBroadcaster->documentEventOccured(aObject);
    if (bModal)
        UITestLogger::getInstance().log(Concat2View("Open Modal " + get_id()));
    else
        UITestLogger::getInstance().log(Concat2View("Open Modeless " + get_id()));

    bool bTunnelingEnabled = mpDialogImpl->m_bLOKTunneling;
    if (comphelper::LibreOfficeKit::isActive() && bTunnelingEnabled)
    {
        if (const vcl::ILibreOfficeKitNotifier* pNotifier = GetLOKNotifier())
        {
            // Dialog boxes don't get the Resize call and they
            // can have invalid size at 'created' message above.
            // If there is no difference, the client should detect it and ignore us,
            // otherwise, this should make sure that the window has the correct size.
            std::vector<vcl::LOKPayloadItem> aItems;
            aItems.emplace_back("size", GetSizePixel().toString());
            aItems.emplace_back("unique_id", this->get_id().toUtf8());
            pNotifier->notifyWindow(GetLOKWindowId(), u"size_changed"_ustr, aItems);
        }
    }

    return true;
}

void Dialog::ImplEndExecuteModal()
{
    ImplSVData* pSVData = ImplGetSVData();
    pSVData->maAppData.mnModalMode--;
}

short Dialog::Execute()
{
    VclPtr<vcl::Window> xWindow = this;

    mbInSyncExecute = true;
    comphelper::ScopeGuard aGuard([&]() {
            mbInSyncExecute = false;
        });

    if ( !ImplStartExecute(false) )
        return 0;

    // Yield util EndDialog is called or dialog gets destroyed
    // (the latter should not happen, but better safe than sorry
    while ( !xWindow->isDisposed() && mbInExecute && !Application::IsQuit() )
        Application::Yield();

    ImplEndExecuteModal();
#ifdef DBG_UTIL
    assert (!mpDialogParent || !mpDialogParent->isDisposed());
#endif
    if ( !xWindow->isDisposed() )
        xWindow.reset();
    else
    {
        OSL_FAIL( "Dialog::Execute() - Dialog destroyed in Execute()" );
    }


    tools::Long nRet = mpDialogImpl->mnResult;
    mpDialogImpl->mnResult = -1;

    return static_cast<short>(nRet);
}

// virtual
bool Dialog::StartExecuteAsync( VclAbstractDialog::AsyncContext &rCtx )
{
    const bool bModal = GetType() != WindowType::MODELESSDIALOG;
    if (!ImplStartExecute(true))
    {
        rCtx.mxOwner.disposeAndClear();
        rCtx.mxOwnerDialogController.reset();
        rCtx.mxOwnerSelf.reset();
        return false;
    }

    mpDialogImpl->maEndCtx = rCtx;
    mpDialogImpl->mbStartedModal = bModal;

    return true;
}

void Dialog::RemoveFromDlgList()
{
    ImplSVData* pSVData = ImplGetSVData();
    auto& rExecuteDialogs = pSVData->mpWinData->mpExecuteDialogs;

    // remove dialog from the list of dialogs which are being executed
    std::erase_if(rExecuteDialogs, [this](VclPtr<Dialog>& dialog){ return dialog.get() == this; });
}

void Dialog::EndDialog( tools::Long nResult )
{
    if (!mbInExecute || isDisposed())
        return;

    // do not let this object be destroyed from underneath us
    VclPtr<vcl::Window> xWindow = this;

    const bool bModal = GetType() != WindowType::MODELESSDIALOG;

    Hide();

    if (comphelper::LibreOfficeKit::isActive())
    {
        if(const vcl::ILibreOfficeKitNotifier* pNotifier = GetLOKNotifier())
        {
            if (mpDialogImpl->m_bLOKTunneling)
                pNotifier->notifyWindow(GetLOKWindowId(), u"close"_ustr);
            ReleaseLOKNotifier();
        }
    }

    if (bModal)
    {
        SetModalInputMode(false);

        RemoveFromDlgList();

        // set focus to previous modal dialog if it is modal for
        // the same frame parent (or NULL)
        ImplSVData* pSVData = ImplGetSVData();
        if (!pSVData->mpWinData->mpExecuteDialogs.empty())
        {
            VclPtr<Dialog> pPrevious = pSVData->mpWinData->mpExecuteDialogs.back();

            vcl::Window* pFrameParent = ImplGetFrameWindow()->ImplGetParent();
            vcl::Window* pPrevFrameParent = pPrevious->ImplGetFrameWindow()? pPrevious->ImplGetFrameWindow()->ImplGetParent(): nullptr;
            if( ( !pFrameParent && !pPrevFrameParent ) ||
                ( pFrameParent && pPrevFrameParent && pFrameParent->ImplGetFrame() == pPrevFrameParent->ImplGetFrame() )
                )
            {
                pPrevious->GrabFocus();
            }
        }
    }

    mpDialogImpl->mnResult = nResult;

    if ( mpDialogImpl->mbStartedModal )
        ImplEndExecuteModal();

    if ( mpDialogImpl->maEndCtx.isSet() )
    {
        // We have a special case with async-dialogs that re-execute themselves.
        // In order to prevent overwriting state we need here, we need to extract
        // all the state we need before calling maEndDialogFn, because
        // maEndDialogFn might itself call StartExecuteAsync and store new state.
        std::shared_ptr<weld::DialogController> xOwnerDialogController = std::move(mpDialogImpl->maEndCtx.mxOwnerDialogController);
        std::shared_ptr<weld::Dialog> xOwnerSelf = std::move(mpDialogImpl->maEndCtx.mxOwnerSelf);
        auto xOwner = std::move(mpDialogImpl->maEndCtx.mxOwner);
        if ( mpDialogImpl->mbStartedModal )
        {
            mpDialogImpl->mbStartedModal = false;
            mpDialogImpl->mnResult = -1;
        }
        mbInExecute = false;
        auto fn = std::move(mpDialogImpl->maEndCtx.maEndDialogFn);

        // std::move leaves maEndDialogFn in a valid state with unspecified
        // value. For the SwSyncBtnDlg case gcc and msvc left maEndDialogFn
        // unset, but clang left maEndDialogFn at its original value, keeping
        // an extra reference to the DialogController in its lambda giving
        // an inconsistent lifecycle for the dialog. Force it to be unset.
        mpDialogImpl->maEndCtx.maEndDialogFn = nullptr;
        fn(nResult);

        // Destroy ourselves, if we have a context with VclPtr owner, and
        // we have not been re-executed.
        if (!mpDialogImpl->maEndCtx.isSet())
            xOwner.disposeAndClear();
        xOwnerDialogController.reset();
        xOwnerSelf.reset();
    }
    else
    {
        if ( mpDialogImpl->mbStartedModal )
        {
            mpDialogImpl->mbStartedModal = false;
            mpDialogImpl->mnResult = -1;
        }
        mbInExecute = false;
        // Destroy ourselves (if we have a context with VclPtr owner)
        std::shared_ptr<weld::DialogController> xOwnerDialogController = std::move(mpDialogImpl->maEndCtx.mxOwnerDialogController);
        std::shared_ptr<weld::Dialog> xOwnerSelf = std::move(mpDialogImpl->maEndCtx.mxOwnerSelf);
        mpDialogImpl->maEndCtx.mxOwner.disposeAndClear();
    }
}

namespace vcl
{
    void EndAllDialogs( vcl::Window const * pParent )
    {
        ImplSVData* pSVData = ImplGetSVData();
        auto& rExecuteDialogs = pSVData->mpWinData->mpExecuteDialogs;

        for (auto it = rExecuteDialogs.rbegin(); it != rExecuteDialogs.rend(); ++it)
        {
            if (!pParent || pParent->IsWindowOrChild(*it, true))
            {
                (*it)->EndDialog();
                (*it)->PostUserEvent(Link<void*, void>());
            }
        }
    }

    void EnableDialogInput(vcl::Window* pWindow)
    {
        if (Dialog* pDialog = dynamic_cast<Dialog*>(pWindow))
        {
            pDialog->EnableInput();
        }
    }

    void CloseTopLevel(vcl::Window* pWindow)
    {
        if (Dialog* pDialog = dynamic_cast<Dialog*>(pWindow))
            pDialog->Close();
        else if (FloatingWindow* pFloatWin = dynamic_cast<FloatingWindow*>(pWindow))
            pFloatWin->EndPopupMode(FloatWinPopupEndFlags::Cancel | FloatWinPopupEndFlags::CloseAll);
    }
}

void Dialog::SetModalInputMode( bool bModal )
{
    if ( bModal == mbModalMode )
        return;

    ImplGetFrame()->SetModal(bModal);

    if (GetParent())
    {
        SalFrame* pFrame = GetParent()->ImplGetFrame();
        pFrame->NotifyModalHierarchy(bModal);
    }

    ImplSetModalInputMode(bModal);
}

void Dialog::ImplSetModalInputMode( bool bModal )
{
    if ( bModal == mbModalMode )
        return;

    // previously Execute()'d dialog - the one below the top-most one
    VclPtr<Dialog> pPrevious;
    ImplSVData* pSVData = ImplGetSVData();
    auto& rExecuteDialogs = pSVData->mpWinData->mpExecuteDialogs;
    if (rExecuteDialogs.size() > 1)
        pPrevious = rExecuteDialogs[rExecuteDialogs.size() - 2];

    mbModalMode = bModal;
    if ( bModal )
    {
        // Disable the prev Modal Dialog, because our dialog must close at first,
        // before the other dialog can be closed (because the other dialog
        // is on stack since our dialog returns)
        if (pPrevious && !pPrevious->IsWindowOrChild(this, true))
            pPrevious->EnableInput(false, this);

        // determine next overlap dialog parent
        vcl::Window* pParent = GetParent();
        if ( pParent )
        {
            // #103716# dialogs should always be modal to the whole frame window
            // #115933# disable the whole frame hierarchy, useful if our parent
            // is a modeless dialog
            mpDialogParent = pParent->mpWindowImpl->mpFrameWindow;
            mpDialogParent->IncModalCount();
        }
    }
    else
    {
        if ( mpDialogParent )
        {
            // #115933# re-enable the whole frame hierarchy again (see above)
            // note that code in getfocus assures that we do not accidentally enable
            // windows that were disabled before
            mpDialogParent->DecModalCount();
        }

        // Enable the prev Modal Dialog
        if (pPrevious && !pPrevious->IsWindowOrChild(this, true))
        {
            pPrevious->EnableInput(true, this);

            // ensure continued modality of prev dialog
            // do not change modality counter

            // #i119994# need find the last modal dialog before reactive it
            if (pPrevious->IsModalInputMode() || !pPrevious->IsWindowOrChild(this, true))
            {
                pPrevious->ImplSetModalInputMode(false);
                pPrevious->ImplSetModalInputMode(true);
            }
        }
    }
}

vcl::Window* Dialog::GetFirstControlForFocus()
{
    vcl::Window* pFocusControl = nullptr;
    vcl::Window* pFirstOverlapWindow = ImplGetFirstOverlapWindow();

    // find focus control, even if the dialog has focus
    if (!HasFocus() && pFirstOverlapWindow && pFirstOverlapWindow->mpWindowImpl)
    {
        // prefer a child window which had focus before
        pFocusControl = ImplGetFirstOverlapWindow()->mpWindowImpl->mpLastFocusWindow;
        // find the control out of the dialog control
        if ( pFocusControl )
            pFocusControl = ImplFindDlgCtrlWindow( pFocusControl );
    }
    // no control had the focus before or the control is not
    // part of the tab-control, now give focus to the
    // first control in the tab-control
    if ( !pFocusControl ||
         !(pFocusControl->GetStyle() & WB_TABSTOP) ||
         !isVisibleInLayout(pFocusControl) ||
         !isEnabledInLayout(pFocusControl) || !pFocusControl->IsInputEnabled() )
    {
        pFocusControl = ImplGetDlgWindow( 0, GetDlgWindowType::First );
    }

    return pFocusControl;
}

void Dialog::GrabFocusToFirstControl()
{
    vcl::Window* pFocusControl = GetFirstControlForFocus();
    if ( pFocusControl )
        pFocusControl->ImplControlFocus( GetFocusFlags::Init );
}

void Dialog::GetDrawWindowBorder( sal_Int32& rLeftBorder, sal_Int32& rTopBorder, sal_Int32& rRightBorder, sal_Int32& rBottomBorder ) const
{
    ScopedVclPtrInstance<ImplBorderWindow> aImplWin( static_cast<vcl::Window*>(const_cast<Dialog *>(this)), WB_BORDER|WB_STDWORK, BorderWindowStyle::Overlap );
    aImplWin->GetBorder( rLeftBorder, rTopBorder, rRightBorder, rBottomBorder );
}

void Dialog::Draw( OutputDevice* pDev, const Point& rPos, SystemTextColorFlags )
{
    Point aPos = pDev->LogicToPixel( rPos );
    Size aSize = GetSizePixel();

    Wallpaper aWallpaper = GetBackground();
    if ( !aWallpaper.IsBitmap() )
        ImplInitSettings();

    pDev->Push();
    pDev->SetMapMode();
    pDev->SetLineColor();

    if ( aWallpaper.IsBitmap() )
        pDev->DrawBitmapEx( aPos, aSize, aWallpaper.GetBitmap() );
    else
    {
        pDev->SetFillColor( aWallpaper.GetColor() );
        pDev->DrawRect( tools::Rectangle( aPos, aSize ) );
    }

    if (!( GetStyle() & WB_NOBORDER ))
    {
        ScopedVclPtrInstance< ImplBorderWindow > aImplWin( this, WB_BORDER|WB_STDWORK, BorderWindowStyle::Overlap );
        aImplWin->SetText( GetText() );
        aImplWin->setPosSizePixel( aPos.X(), aPos.Y(), aSize.Width(), aSize.Height() );
        aImplWin->SetDisplayActive( true );
        aImplWin->InitView();

        aImplWin->Draw( pDev, aPos );
    }

    pDev->Pop();
}

void Dialog::queue_resize(StateChangedType eReason)
{
    if (IsInClose())
        return;
    SystemWindow::queue_resize(eReason);
}

void Dialog::Resize()
{
    SystemWindow::Resize();

    if (comphelper::LibreOfficeKit::isDialogPainting())
        return;

    bool bTunnelingEnabled = mpDialogImpl->m_bLOKTunneling;
    const vcl::ILibreOfficeKitNotifier* pNotifier = GetLOKNotifier();
    if (pNotifier && bTunnelingEnabled)
    {
        std::vector<vcl::LOKPayloadItem> aItems;
        aItems.emplace_back("size", GetSizePixel().toString());
        aItems.emplace_back("unique_id", this->get_id().toUtf8());
        pNotifier->notifyWindow(GetLOKWindowId(), u"size_changed"_ustr, aItems);
    }
}

bool Dialog::set_property(const OUString &rKey, const OUString &rValue)
{
    if (rKey == "border-width")
        set_border_width(rValue.toInt32());
    else
        return SystemWindow::set_property(rKey, rValue);
    return true;
}

FactoryFunction Dialog::GetUITestFactory() const
{
    return DialogUIObject::create;
}

IMPL_LINK(Dialog, ResponseHdl, Button*, pButton, void)
{
    auto aFind = mpDialogImpl->maResponses.find(pButton);
    if (aFind == mpDialogImpl->maResponses.end())
        return;
    short nResponse = aFind->second;
    if (nResponse == RET_HELP)
    {
        vcl::Window* pFocusWin = Application::GetFocusWindow();
        if (!pFocusWin || comphelper::LibreOfficeKit::isActive())
            pFocusWin = pButton;
        HelpEvent aEvt(pFocusWin->GetPointerPosPixel(), HelpEventMode::CONTEXT);
        pFocusWin->RequestHelp(aEvt);
        return;
    }
    EndDialog(nResponse);
}

void Dialog::add_button(PushButton* pButton, int response, bool bTransferOwnership)
{
    if (bTransferOwnership)
        mpDialogImpl->maOwnedButtons.push_back(pButton);
    mpDialogImpl->maResponses[pButton] = response;
    switch (pButton->GetType())
    {
        case WindowType::PUSHBUTTON:
        {
            if (!pButton->GetClickHdl().IsSet())
                pButton->SetClickHdl(LINK(this, Dialog, ResponseHdl));
            break;
        }
        //insist that the response ids match the default actions for those
        //widgets, and leave their default handlers in place
        case WindowType::OKBUTTON:
            assert(mpDialogImpl->get_response(pButton) == RET_OK);
            break;
        case WindowType::CANCELBUTTON:
            assert(mpDialogImpl->get_response(pButton) == RET_CANCEL || mpDialogImpl->get_response(pButton) == RET_CLOSE);
            break;
        case WindowType::HELPBUTTON:
            assert(mpDialogImpl->get_response(pButton) == RET_HELP);
            break;
        default:
            SAL_WARN("vcl.layout", "The type of widget " <<
                pButton->GetHelpId() << " is currently not handled");
            break;
    }
}

vcl::Window* Dialog::get_widget_for_response(int response)
{
    //copy explicit responses
    std::map<VclPtr<vcl::Window>, short> aResponses(mpDialogImpl->maResponses);

    if (mpActionArea)
    {
        //add implicit responses
        for (vcl::Window* pChild = mpActionArea->GetWindow(GetWindowType::FirstChild); pChild;
             pChild = pChild->GetWindow(GetWindowType::Next))
        {
            if (aResponses.find(pChild) != aResponses.end())
                continue;
            switch (pChild->GetType())
            {
                case WindowType::OKBUTTON:
                    aResponses[pChild] = RET_OK;
                    break;
                case WindowType::CANCELBUTTON:
                    aResponses[pChild] = RET_CANCEL;
                    break;
                case WindowType::HELPBUTTON:
                    aResponses[pChild] = RET_HELP;
                    break;
                default:
                    break;
            }
        }
    }

    for (const auto& a : aResponses)
    {
        if (a.second == response)
           return a.first;
    }

    return nullptr;
}

int Dialog::get_default_response() const
{
    //copy explicit responses
    std::map<VclPtr<vcl::Window>, short> aResponses(mpDialogImpl->maResponses);

    if (mpActionArea)
    {
        //add implicit responses
        for (vcl::Window* pChild = mpActionArea->GetWindow(GetWindowType::FirstChild); pChild;
             pChild = pChild->GetWindow(GetWindowType::Next))
        {
            if (aResponses.find(pChild) != aResponses.end())
                continue;
            switch (pChild->GetType())
            {
                case WindowType::OKBUTTON:
                    aResponses[pChild] = RET_OK;
                    break;
                case WindowType::CANCELBUTTON:
                    aResponses[pChild] = RET_CANCEL;
                    break;
                case WindowType::HELPBUTTON:
                    aResponses[pChild] = RET_HELP;
                    break;
                default:
                    break;
            }
        }
    }

    for (const auto& a : aResponses)
    {
        if (a.first->GetStyle() & WB_DEFBUTTON)
        {
            return a.second;
        }
    }
    return RET_CANCEL;
}

void Dialog::set_default_response(int response)
{
    //copy explicit responses
    std::map<VclPtr<vcl::Window>, short> aResponses(mpDialogImpl->maResponses);

    if (mpActionArea)
    {
        //add implicit responses
        for (vcl::Window* pChild = mpActionArea->GetWindow(GetWindowType::FirstChild); pChild;
             pChild = pChild->GetWindow(GetWindowType::Next))
        {
            if (aResponses.find(pChild) != aResponses.end())
                continue;
            switch (pChild->GetType())
            {
                case WindowType::OKBUTTON:
                    aResponses[pChild] = RET_OK;
                    break;
                case WindowType::CANCELBUTTON:
                    aResponses[pChild] = RET_CANCEL;
                    break;
                case WindowType::HELPBUTTON:
                    aResponses[pChild] = RET_HELP;
                    break;
                default:
                    break;
            }
        }
    }

    for (auto& a : aResponses)
    {
        if (a.second == response)
        {
            a.first->SetStyle(a.first->GetStyle() | WB_DEFBUTTON);
            a.first->GrabFocus();
        }
        else
        {
            a.first->SetStyle(a.first->GetStyle() & ~WB_DEFBUTTON);
        }
    }
}

VclBuilderContainer::VclBuilderContainer()
{
}

void VclBuilderContainer::setDeferredProperties()
{
    if (!m_pUIBuilder)
        return;
    m_pUIBuilder->setDeferredProperties();
}

VclBuilderContainer::~VclBuilderContainer()
{
}

void Dialog::Activate()
{
    if (GetType() == WindowType::MODELESSDIALOG)
    {
        const css::uno::Reference< css::uno::XComponentContext >& xContext(
                comphelper::getProcessComponentContext() );
        css::uno::Reference<css::frame::XGlobalEventBroadcaster> xEventBroadcaster(css::frame::theGlobalEventBroadcaster::get(xContext), css::uno::UNO_SET_THROW);
        css::document::DocumentEvent aObject;
        aObject.EventName = "ModelessDialogVisible";
        aObject.Supplement <<= GetText(); // title
        xEventBroadcaster->documentEventOccured(aObject);
    }
    SystemWindow::Activate();
}

void Dialog::Command(const CommandEvent& rCEvt)
{
    if (mpDialogImpl->m_aPopupMenuHdl.Call(rCEvt))
        return;
    SystemWindow::Command(rCEvt);
}

struct TopLevelWindowLockerImpl
{
    std::stack<std::vector<VclPtr<vcl::Window>>> m_aBusyStack;
};

TopLevelWindowLocker::TopLevelWindowLocker()
    : m_xImpl(std::make_unique<TopLevelWindowLockerImpl>())
{
}

void TopLevelWindowLocker::incBusy(const weld::Widget* pIgnore)
{
    // lock any toplevel windows from being closed until busy is over
    std::vector<VclPtr<vcl::Window>> aTopLevels;
    vcl::Window *pTopWin = Application::GetFirstTopLevelWindow();
    while (pTopWin)
    {
        vcl::Window* pCandidate = pTopWin;
        if (pCandidate->GetType() == WindowType::BORDERWINDOW)
            pCandidate = pCandidate->GetWindow(GetWindowType::FirstChild);
        // tdf#125266 ignore HelpTextWindows
        if (pCandidate &&
            pCandidate->GetType() != WindowType::HELPTEXTWINDOW &&
            pCandidate->GetType() != WindowType::FLOATINGWINDOW &&
            pCandidate->GetFrameWeld() != pIgnore)
        {
            aTopLevels.push_back(pCandidate);
        }
        pTopWin = Application::GetNextTopLevelWindow(pTopWin);
    }
    for (auto& a : aTopLevels)
    {
        a->IncModalCount();
        a->ImplGetFrame()->NotifyModalHierarchy(true);
    }
    m_xImpl->m_aBusyStack.push(aTopLevels);
}

void TopLevelWindowLocker::decBusy()
{
    // unlock locked toplevel windows from being closed now busy is over
    for (auto& a : m_xImpl->m_aBusyStack.top())
    {
        if (a->isDisposed())
            continue;
        a->DecModalCount();
        a->ImplGetFrame()->NotifyModalHierarchy(false);
    }
    m_xImpl->m_aBusyStack.pop();
}

bool TopLevelWindowLocker::isBusy() const
{
    return !m_xImpl->m_aBusyStack.empty();
}

TopLevelWindowLocker::~TopLevelWindowLocker()
{
}

void Dialog::DumpAsPropertyTree(tools::JsonWriter& rJsonWriter)
{
    SystemWindow::DumpAsPropertyTree(rJsonWriter);
    rJsonWriter.put("title", GetText());
    if (vcl::Window* pActionArea = get_action_area())
    {
        if (!pActionArea->IsVisible())
            rJsonWriter.put("collapsed", true);
    }

    OUString sDialogId = GetHelpId();
    sal_Int32 nStartPos = sDialogId.lastIndexOf('/');
    nStartPos = nStartPos >= 0 ? nStartPos + 1 : 0;
    rJsonWriter.put("dialogid", sDialogId.copy(nStartPos));

    {
        auto aResponses = rJsonWriter.startArray("responses");
        for (const auto& rResponse : mpDialogImpl->maResponses)
        {
            auto aResponse = rJsonWriter.startStruct();
            rJsonWriter.put("id", rResponse.first->get_id());
            rJsonWriter.put("response", rResponse.second);
        }
    }

    vcl::Window* pFocusControl = GetFirstControlForFocus();
    if (pFocusControl)
        rJsonWriter.put("init_focus_id", pFocusControl->get_id());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
