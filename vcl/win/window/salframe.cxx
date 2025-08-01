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

#include <com/sun/star/accessibility/MSAAService.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/uno/DeploymentException.hpp>
#include <IconThemeSelector.hxx>

#include <officecfg/Office/Common.hxx>

#include <memory>
#include <string.h>
#include <limits.h>

#include <svsys.h>

#include <comphelper/diagnose_ex.hxx>
#include <comphelper/windowserrorstring.hxx>

#include <rtl/bootstrap.hxx>
#include <rtl/character.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <osl/module.h>
#include <comphelper/scopeguard.hxx>
#include <tools/debug.hxx>
#include <o3tl/enumarray.hxx>
#include <o3tl/char16_t2wchar_t.hxx>

#include <vcl/event.hxx>
#include <vcl/sysdata.hxx>
#include <vcl/timer.hxx>
#include <vcl/settings.hxx>
#include <vcl/themecolors.hxx>
#include <vcl/keycodes.hxx>
#include <vcl/window.hxx>
#include <vcl/wrkwin.hxx>
#include <vcl/svapp.hxx>
#include <vcl/ptrstyle.hxx>

#include <win/wincomp.hxx>
#include <win/salids.hrc>
#include <win/saldata.hxx>
#include <win/salinst.h>
#include <win/salbmp.h>
#include <win/salgdi.h>
#include <win/salsys.h>
#include <win/salframe.h>
#include <win/salvd.h>
#include <win/salmenu.h>
#include <win/salobj.h>
#include <win/saltimer.h>

#include <helpwin.hxx>
#include <window.h>
#include <sallayout.hxx>

#include <vector>

#include <com/sun/star/uno/Exception.hpp>

#include <oleacc.h>
#include <com/sun/star/accessibility/XMSAAService.hpp>

#include <time.h>

#if !defined WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <Vssym32.h>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::container;
using namespace ::com::sun::star::beans;

#ifndef IDC_PEN
# define IDC_PEN MAKEINTRESOURCE(32631)
#endif

const unsigned int WM_USER_SYSTEM_WINDOW_ACTIVATED = RegisterWindowMessageW(L"SYSTEM_WINDOW_ACTIVATED");

bool WinSalFrame::mbInReparent = false;

// Macros for support of WM_UNICHAR & Keyman 6.0
#define Uni_SupplementaryPlanesStart    0x10000

static void SetGeometrySize(vcl::WindowPosSize& rWinPosSize, const Size& rSize)
{
    rWinPosSize.setWidth(rSize.Width() < 0 ? 0 : rSize.Width());
    rWinPosSize.setHeight(rSize.Height() < 0 ? 0 : rSize.Height());
}

// If called with UpdateFrameGeometry, it must be called after it, as UpdateFrameGeometry
// updates the geometry depending on the old state!
void WinSalFrame::UpdateFrameState()
{
    // don't overwrite restore state in fullscreen mode
    if (isFullScreen())
        return;

    const bool bVisible = (GetWindowStyle(mhWnd) & WS_VISIBLE);
    if (IsIconic(mhWnd))
    {
        m_eState &= ~vcl::WindowState(vcl::WindowState::Normal | vcl::WindowState::Maximized);
        m_eState |= vcl::WindowState::Minimized;
        if (bVisible)
            mnShowState = SW_SHOWMINIMIZED;
    }
    else if (IsZoomed(mhWnd))
    {
        m_eState &= ~vcl::WindowState(vcl::WindowState::Minimized | vcl::WindowState::Normal);
        m_eState |= vcl::WindowState::Maximized;
        if (bVisible)
            mnShowState = SW_SHOWMAXIMIZED;
        mbRestoreMaximize = true;
    }
    else
    {
        m_eState &= ~vcl::WindowState(vcl::WindowState::Minimized | vcl::WindowState::Maximized);
        m_eState |= vcl::WindowState::Normal;
        if (bVisible)
            mnShowState = SW_SHOWNORMAL;
        mbRestoreMaximize = false;
    }
}

// if pParentRect is set, the workarea of the monitor that contains pParentRect is returned
void ImplSalGetWorkArea( HWND hWnd, RECT *pRect, const RECT *pParentRect )
{
    if (Application::IsHeadlessModeEnabled()) {
        pRect->left = 0;
        pRect->top = 0;
        pRect->right = VIRTUAL_DESKTOP_WIDTH;
        pRect->bottom = VIRTUAL_DESKTOP_HEIGHT;
        return;
    }
    // check if we or our parent is fullscreen, then the taskbar should be ignored
    bool bIgnoreTaskbar = false;
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if( pFrame )
    {
        vcl::Window *pWin = pFrame->GetWindow();
        while( pWin )
        {
            WorkWindow *pWorkWin = (pWin->GetType() == WindowType::WORKWINDOW) ? static_cast<WorkWindow *>(pWin) : nullptr;
            if( pWorkWin && pWorkWin->ImplGetWindowImpl()->mbReallyVisible && pWorkWin->IsFullScreenMode() )
            {
                bIgnoreTaskbar = true;
                break;
            }
            else
                pWin = pWin->ImplGetWindowImpl()->mpParent;
        }
    }

    // calculates the work area taking multiple monitors into account
    static int nMonitors = GetSystemMetrics( SM_CMONITORS );
    if( nMonitors == 1 )
    {
        if( bIgnoreTaskbar )
        {
            pRect->left = pRect->top = 0;
            pRect->right   = GetSystemMetrics( SM_CXSCREEN );
            pRect->bottom  = GetSystemMetrics( SM_CYSCREEN );
        }
        else
            SystemParametersInfoW( SPI_GETWORKAREA, 0, pRect, 0 );
    }
    else
    {
        if( pParentRect != nullptr )
        {
            // return the size of the monitor where pParentRect lives
            HMONITOR hMonitor;
            MONITORINFO mi;

            // get the nearest monitor to the passed rect.
            hMonitor = MonitorFromRect(pParentRect, MONITOR_DEFAULTTONEAREST);

            // get the work area or entire monitor rect.
            mi.cbSize = sizeof(mi);
            GetMonitorInfo(hMonitor, &mi);
            if( !bIgnoreTaskbar )
                *pRect = mi.rcWork;
            else
                *pRect = mi.rcMonitor;
        }
        else
        {
            // return the union of all monitors
            pRect->left = GetSystemMetrics( SM_XVIRTUALSCREEN );
            pRect->top = GetSystemMetrics( SM_YVIRTUALSCREEN );
            pRect->right = pRect->left + GetSystemMetrics( SM_CXVIRTUALSCREEN );
            pRect->bottom = pRect->top + GetSystemMetrics( SM_CYVIRTUALSCREEN );

            // virtualscreen does not take taskbar into account, so use the corresponding
            // diffs between screen and workarea from the default screen
            // however, this is still not perfect: the taskbar might not be on the primary screen
            if( !bIgnoreTaskbar )
            {
                RECT wRect, scrRect;
                SystemParametersInfoW( SPI_GETWORKAREA, 0, &wRect, 0 );
                scrRect.left = 0;
                scrRect.top = 0;
                scrRect.right = GetSystemMetrics( SM_CXSCREEN );
                scrRect.bottom = GetSystemMetrics( SM_CYSCREEN );

                pRect->left += wRect.left;
                pRect->top += wRect.top;
                pRect->right -= scrRect.right - wRect.right;
                pRect->bottom -= scrRect.bottom - wRect.bottom;
            }
        }
    }
}

namespace {

enum PreferredAppMode
{
    AllowDark = 1,
    ForceDark = 2,
    ForceLight = 3
};

}

static void UpdateDarkMode(HWND hWnd)
{
    static bool bOSSupportsDarkMode = OSSupportsDarkMode();
    if (!bOSSupportsDarkMode)
        return;

    HINSTANCE hUxthemeLib = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hUxthemeLib)
        return;

    typedef PreferredAppMode(WINAPI* SetPreferredAppMode_t)(PreferredAppMode);
    auto SetPreferredAppMode = reinterpret_cast<SetPreferredAppMode_t>(GetProcAddress(hUxthemeLib, MAKEINTRESOURCEA(135)));
    if (SetPreferredAppMode)
    {
        switch (MiscSettings::GetAppColorMode())
        {
            case AppearanceMode::AUTO:
                SetPreferredAppMode(AllowDark);
                break;
            case AppearanceMode::LIGHT:
                SetPreferredAppMode(ForceLight);
                break;
            case AppearanceMode::DARK:
                SetPreferredAppMode(ForceDark);
                break;
        }
    }

    BOOL bDarkMode = UseDarkMode();

    typedef void(WINAPI* AllowDarkModeForWindow_t)(HWND, BOOL);
    auto AllowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindow_t>(GetProcAddress(hUxthemeLib, MAKEINTRESOURCEA(133)));
    if (AllowDarkModeForWindow)
        AllowDarkModeForWindow(hWnd, bDarkMode);

    FreeLibrary(hUxthemeLib);

    if (!AllowDarkModeForWindow)
        return;

    DwmSetWindowAttribute(hWnd, 20, &bDarkMode, sizeof(bDarkMode));
}

static void UpdateAutoAccel()
{
    BOOL bUnderline = FALSE;
    SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &bUnderline, 0);

    ImplSVData* pSVData = ImplGetSVData();
    pSVData->maNWFData.mbAutoAccel = !bUnderline;
}

SalFrame* ImplSalCreateFrame( WinSalInstance* pInst,
                              HWND hWndParent, SalFrameStyleFlags nSalFrameStyle )
{
    WinSalFrame*   pFrame = new WinSalFrame;
    HWND        hWnd;
    DWORD       nSysStyle = 0;
    DWORD       nExSysStyle = 0;
    bool        bSubFrame = false;

    static const char* pEnvSynchronize = getenv("SAL_SYNCHRONIZE");
    if ( pEnvSynchronize )   // no buffering of drawing commands
        GdiSetBatchLimit( 1 );

    static const char* pEnvTransparentFloats = getenv("SAL_TRANSPARENT_FLOATS" );

    // determine creation data
    if ( nSalFrameStyle & (SalFrameStyleFlags::PLUG | SalFrameStyleFlags::SYSTEMCHILD) )
    {
        nSysStyle |= WS_CHILD;
        if( nSalFrameStyle & SalFrameStyleFlags::SYSTEMCHILD )
            nSysStyle |= WS_CLIPSIBLINGS;
    }
    else
    {
        // #i87402# commenting out WS_CLIPCHILDREN
        // this breaks SalFrameStyleFlags::SYSTEMCHILD handling, which is not
        // used currently. Probably SalFrameStyleFlags::SYSTEMCHILD should be
        // removed again.

        // nSysStyle  |= WS_CLIPCHILDREN;
        if ( hWndParent )
        {
            nSysStyle |= WS_POPUP;
            bSubFrame = true;
            pFrame->mbNoIcon = true;
        }
        else
        {
            // Only with WS_OVERLAPPED we get a useful default position/size
            if ( (nSalFrameStyle & (SalFrameStyleFlags::SIZEABLE | SalFrameStyleFlags::MOVEABLE)) ==
                 (SalFrameStyleFlags::SIZEABLE | SalFrameStyleFlags::MOVEABLE) )
                nSysStyle |= WS_OVERLAPPED;
            else
            {
                nSysStyle |= WS_POPUP;
                if ( !(nSalFrameStyle & SalFrameStyleFlags::MOVEABLE) )
                    nExSysStyle |= WS_EX_TOOLWINDOW;    // avoid taskbar appearance, for eg splash screen
            }
        }

        if ( nSalFrameStyle & SalFrameStyleFlags::MOVEABLE )
        {
            pFrame->mbCaption = true;
            nSysStyle |= WS_SYSMENU | WS_CAPTION;
            if ( !hWndParent )
                nSysStyle |= WS_SYSMENU | WS_MINIMIZEBOX;
            else
                nExSysStyle |= WS_EX_DLGMODALFRAME;

            if ( nSalFrameStyle & SalFrameStyleFlags::SIZEABLE )
            {
                pFrame->mbSizeBorder = true;
                nSysStyle |= WS_THICKFRAME;
                if ( !hWndParent )
                    nSysStyle |= WS_MAXIMIZEBOX;
            }
            else
                pFrame->mbFixBorder = true;

            if ( nSalFrameStyle & SalFrameStyleFlags::DEFAULT )
                nExSysStyle |= WS_EX_APPWINDOW;
        }
        if( nSalFrameStyle & SalFrameStyleFlags::TOOLWINDOW
            // #100656# toolwindows lead to bad alt-tab behaviour, if they have the focus
            // you must press it twice to leave the application
            // so toolwindows are only used for non sizeable windows
            // which are typically small, so a small caption makes sense

            // #103578# looked too bad - above changes reverted
            /* && !(nSalFrameStyle & SalFrameStyleFlags::SIZEABLE) */ )
        {
            pFrame->mbNoIcon = true;
            nExSysStyle |= WS_EX_TOOLWINDOW;
            if ( pEnvTransparentFloats /*&& !(nSalFrameStyle & SalFrameStyleFlags::MOVEABLE) */)
                nExSysStyle |= WS_EX_LAYERED;
        }
    }
    if ( nSalFrameStyle & SalFrameStyleFlags::FLOAT )
    {
        nExSysStyle |= WS_EX_TOOLWINDOW;
        pFrame->mbFloatWin = true;

        if (pEnvTransparentFloats)
            nExSysStyle |= WS_EX_LAYERED;

    }
    if (nSalFrameStyle & SalFrameStyleFlags::TOOLTIP)
        nExSysStyle |= WS_EX_TOPMOST;

    // init frame data
    pFrame->mnStyle = nSalFrameStyle;

    // determine show style
    pFrame->mnShowState = SW_SHOWNORMAL;
    if ( (nSysStyle & (WS_POPUP | WS_MAXIMIZEBOX | WS_THICKFRAME)) == (WS_MAXIMIZEBOX | WS_THICKFRAME) )
    {
        if ( GetSystemMetrics( SM_CXSCREEN ) <= 1024 )
            pFrame->mnShowState = SW_SHOWMAXIMIZED;
        else
        {
            if ( nSalFrameStyle & SalFrameStyleFlags::DEFAULT )
            {
                SalData* pSalData = GetSalData();
                pFrame->mnShowState = pSalData->mnCmdShow;
                if ( (pFrame->mnShowState != SW_SHOWMINIMIZED) &&
                     (pFrame->mnShowState != SW_MINIMIZE) &&
                     (pFrame->mnShowState != SW_SHOWMINNOACTIVE) )
                {
                    if ( (pFrame->mnShowState == SW_SHOWMAXIMIZED) ||
                         (pFrame->mnShowState == SW_MAXIMIZE) )
                        pFrame->mbOverwriteState = false;
                    pFrame->mnShowState = SW_SHOWMAXIMIZED;
                }
                else
                    pFrame->mbOverwriteState = false;
            }
            else
            {
                // Document Windows are also maximized, if the current Document Window
                // is also maximized
                HWND hWnd2 = GetForegroundWindow();
                if ( hWnd2 && IsMaximized( hWnd2 ) &&
                     (GetWindowInstance( hWnd2 ) == pInst->mhInst) &&
                     ((GetWindowStyle( hWnd2 ) & (WS_POPUP | WS_MAXIMIZEBOX | WS_THICKFRAME)) == (WS_MAXIMIZEBOX | WS_THICKFRAME)) )
                    pFrame->mnShowState = SW_SHOWMAXIMIZED;
            }
        }
    }

    // create frame
    LPCWSTR pClassName;
    if ( bSubFrame )
    {
        if ( nSalFrameStyle & (SalFrameStyleFlags::MOVEABLE|SalFrameStyleFlags::NOSHADOW) ) // check if shadow not wanted
            pClassName = SAL_SUBFRAME_CLASSNAMEW;
        else
            pClassName = SAL_TMPSUBFRAME_CLASSNAMEW;    // undecorated floaters will get shadow on XP
    }
    else
    {
        if ( nSalFrameStyle & SalFrameStyleFlags::MOVEABLE )
            pClassName = SAL_FRAME_CLASSNAMEW;
        else
            pClassName = SAL_TMPSUBFRAME_CLASSNAMEW;
    }
    hWnd = CreateWindowExW( nExSysStyle, pClassName, L"", nSysStyle,
                            CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                            hWndParent, nullptr, pInst->mhInst, pFrame );
    SAL_WARN_IF(!hWnd, "vcl", "CreateWindowExW failed: " << comphelper::WindowsErrorString(GetLastError()));

#if OSL_DEBUG_LEVEL > 1
    // set transparency value
    if( GetWindowExStyle( hWnd ) & WS_EX_LAYERED )
        SetLayeredWindowAttributes( hWnd, 0, 230, 0x00000002 /*LWA_ALPHA*/ );
#endif
    if ( !hWnd )
    {
        delete pFrame;
        return nullptr;
    }

    // If we have a Window with a Caption Bar and without
    // a MaximizeBox, we change the SystemMenu
    if ( (nSysStyle & (WS_CAPTION | WS_MAXIMIZEBOX)) == (WS_CAPTION) )
    {
        HMENU hSysMenu = GetSystemMenu( hWnd, FALSE );
        if ( hSysMenu )
        {
            if ( !(nSysStyle & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) )
                DeleteMenu( hSysMenu, SC_RESTORE, MF_BYCOMMAND );
            else
                EnableMenuItem( hSysMenu, SC_RESTORE, MF_BYCOMMAND | MF_GRAYED | MF_DISABLED );
            if ( !(nSysStyle & WS_MINIMIZEBOX) )
                DeleteMenu( hSysMenu, SC_MINIMIZE, MF_BYCOMMAND );
            if ( !(nSysStyle & WS_MAXIMIZEBOX) )
                DeleteMenu( hSysMenu, SC_MAXIMIZE, MF_BYCOMMAND );
            if ( !(nSysStyle & WS_THICKFRAME) )
                DeleteMenu( hSysMenu, SC_SIZE, MF_BYCOMMAND );
        }
    }
    if ( (nSysStyle & WS_SYSMENU) && !(nSalFrameStyle & SalFrameStyleFlags::CLOSEABLE) )
    {
        HMENU hSysMenu = GetSystemMenu( hWnd, FALSE );
        if ( hSysMenu )
            EnableMenuItem( hSysMenu, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED | MF_DISABLED );
    }

    // reset input context
    pFrame->mhDefIMEContext = ImmAssociateContext( hWnd, nullptr );

    // determine output size and state
    RECT aRect;
    GetClientRect( hWnd, &aRect );
    pFrame->mbDefPos = true;

    pFrame->UpdateFrameGeometry();
    pFrame->UpdateFrameState();

    if( pFrame->mnShowState == SW_SHOWMAXIMIZED )
    {
        // #96084 set a useful internal window size because
        // the window will not be maximized (and the size updated) before show()

        pFrame->SetMaximizedFrameGeometry(hWnd);
    }

    return pFrame;
}

// helper that only creates the HWND
// to allow for easy reparenting of system windows, (i.e. destroy and create new)
HWND ImplSalReCreateHWND( HWND hWndParent, HWND oldhWnd, bool bAsChild )
{
    HINSTANCE hInstance = GetSalData()->mhInst;
    sal_uLong nSysStyle     = GetWindowLongW( oldhWnd, GWL_STYLE );
    sal_uLong nExSysStyle   = GetWindowLongW( oldhWnd, GWL_EXSTYLE );

    if( bAsChild )
    {
        nSysStyle = WS_CHILD;
        nExSysStyle = 0;
    }

    LPCWSTR pClassName = SAL_SUBFRAME_CLASSNAMEW;
    return CreateWindowExW( nExSysStyle, pClassName, L"", nSysStyle,
                            CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                            hWndParent, nullptr, hInstance, GetWindowPtr( oldhWnd ) );
}

// translation table from System keycodes into StartView keycodes
#define KEY_TAB_SIZE     168

const sal_uInt16 aImplTranslateKeyTab[KEY_TAB_SIZE] =
{
    // StarView-Code      System-Code                         Index
    0,                    //                                  0
    0,                    // VK_LBUTTON                       1
    0,                    // VK_RBUTTON                       2
    0,                    // VK_CANCEL                        3
    0,                    // VK_MBUTTON                       4
    0,                    //                                  5
    0,                    //                                  6
    0,                    //                                  7
    KEY_BACKSPACE,        // VK_BACK                          8
    KEY_TAB,              // VK_TAB                           9
    0,                    //                                  10
    0,                    //                                  11
    0,                    // VK_CLEAR                         12
    KEY_RETURN,           // VK_RETURN                        13
    0,                    //                                  14
    0,                    //                                  15
    0,                    // VK_SHIFT                         16
    0,                    // VK_CONTROL                       17
    0,                    // VK_MENU                          18
    0,                    // VK_PAUSE                         19
    0,                    // VK_CAPITAL                       20
    0,                    // VK_HANGUL                        21
    0,                    //                                  22
    0,                    //                                  23
    0,                    //                                  24
    KEY_HANGUL_HANJA,     // VK_HANJA                         25
    0,                    //                                  26
    KEY_ESCAPE,           // VK_ESCAPE                        27
    0,                    //                                  28
    0,                    //                                  29
    0,                    //                                  30
    0,                    //                                  31
    KEY_SPACE,            // VK_SPACE                         32
    KEY_PAGEUP,           // VK_PRIOR                         33
    KEY_PAGEDOWN,         // VK_NEXT                          34
    KEY_END,              // VK_END                           35
    KEY_HOME,             // VK_HOME                          36
    KEY_LEFT,             // VK_LEFT                          37
    KEY_UP,               // VK_UP                            38
    KEY_RIGHT,            // VK_RIGHT                         39
    KEY_DOWN,             // VK_DOWN                          40
    0,                    // VK_SELECT                        41
    0,                    // VK_PRINT                         42
    0,                    // VK_EXECUTE                       43
    0,                    // VK_SNAPSHOT                      44
    KEY_INSERT,           // VK_INSERT                        45
    KEY_DELETE,           // VK_DELETE                        46
    KEY_HELP,             // VK_HELP                          47
    KEY_0,                //                                  48
    KEY_1,                //                                  49
    KEY_2,                //                                  50
    KEY_3,                //                                  51
    KEY_4,                //                                  52
    KEY_5,                //                                  53
    KEY_6,                //                                  54
    KEY_7,                //                                  55
    KEY_8,                //                                  56
    KEY_9,                //                                  57
    0,                    //                                  58
    0,                    //                                  59
    0,                    //                                  60
    0,                    //                                  61
    0,                    //                                  62
    0,                    //                                  63
    0,                    //                                  64
    KEY_A,                //                                  65
    KEY_B,                //                                  66
    KEY_C,                //                                  67
    KEY_D,                //                                  68
    KEY_E,                //                                  69
    KEY_F,                //                                  70
    KEY_G,                //                                  71
    KEY_H,                //                                  72
    KEY_I,                //                                  73
    KEY_J,                //                                  74
    KEY_K,                //                                  75
    KEY_L,                //                                  76
    KEY_M,                //                                  77
    KEY_N,                //                                  78
    KEY_O,                //                                  79
    KEY_P,                //                                  80
    KEY_Q,                //                                  81
    KEY_R,                //                                  82
    KEY_S,                //                                  83
    KEY_T,                //                                  84
    KEY_U,                //                                  85
    KEY_V,                //                                  86
    KEY_W,                //                                  87
    KEY_X,                //                                  88
    KEY_Y,                //                                  89
    KEY_Z,                //                                  90
    0,                    // VK_LWIN                          91
    0,                    // VK_RWIN                          92
    KEY_CONTEXTMENU,      // VK_APPS                          93
    0,                    //                                  94
    0,                    //                                  95
    KEY_0,                // VK_NUMPAD0                       96
    KEY_1,                // VK_NUMPAD1                       97
    KEY_2,                // VK_NUMPAD2                       98
    KEY_3,                // VK_NUMPAD3                       99
    KEY_4,                // VK_NUMPAD4                      100
    KEY_5,                // VK_NUMPAD5                      101
    KEY_6,                // VK_NUMPAD6                      102
    KEY_7,                // VK_NUMPAD7                      103
    KEY_8,                // VK_NUMPAD8                      104
    KEY_9,                // VK_NUMPAD9                      105
    KEY_MULTIPLY,         // VK_MULTIPLY                     106
    KEY_ADD,              // VK_ADD                          107
    KEY_DECIMAL,          // VK_SEPARATOR                    108
    KEY_SUBTRACT,         // VK_SUBTRACT                     109
    KEY_DECIMAL,          // VK_DECIMAL                      110
    KEY_DIVIDE,           // VK_DIVIDE                       111
    KEY_F1,               // VK_F1                           112
    KEY_F2,               // VK_F2                           113
    KEY_F3,               // VK_F3                           114
    KEY_F4,               // VK_F4                           115
    KEY_F5,               // VK_F5                           116
    KEY_F6,               // VK_F6                           117
    KEY_F7,               // VK_F7                           118
    KEY_F8,               // VK_F8                           119
    KEY_F9,               // VK_F9                           120
    KEY_F10,              // VK_F10                          121
    KEY_F11,              // VK_F11                          122
    KEY_F12,              // VK_F12                          123
    KEY_F13,              // VK_F13                          124
    KEY_F14,              // VK_F14                          125
    KEY_F15,              // VK_F15                          126
    KEY_F16,              // VK_F16                          127
    KEY_F17,              // VK_F17                          128
    KEY_F18,              // VK_F18                          129
    KEY_F19,              // VK_F19                          130
    KEY_F20,              // VK_F20                          131
    KEY_F21,              // VK_F21                          132
    KEY_F22,              // VK_F22                          133
    KEY_F23,              // VK_F23                          134
    KEY_F24,              // VK_F24                          135
    0,                    //                                 136
    0,                    //                                 137
    0,                    //                                 138
    0,                    //                                 139
    0,                    //                                 140
    0,                    //                                 141
    0,                    //                                 142
    0,                    //                                 143
    0,                    // NUMLOCK                         144
    0,                    // SCROLLLOCK                      145
    0,                    //                                 146
    0,                    //                                 147
    0,                    //                                 148
    0,                    //                                 149
    0,                    //                                 150
    0,                    //                                 151
    0,                    //                                 152
    0,                    //                                 153
    0,                    //                                 154
    0,                    //                                 155
    0,                    //                                 156
    0,                    //                                 157
    0,                    //                                 158
    0,                    //                                 159
    0,                    //                                 160
    0,                    //                                 161
    0,                    //                                 162
    0,                    //                                 163
    0,                    //                                 164
    0,                    //                                 165
    KEY_XF86BACK,         // VK_BROWSER_BACK                 166
    KEY_XF86FORWARD       // VK_BROWSER_FORWARD              167
};

static UINT ImplSalGetWheelScrollLines()
{
    UINT nScrLines = 0;
    if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &nScrLines, 0) || !nScrLines)
        nScrLines = 3;

    return nScrLines;
}

static UINT ImplSalGetWheelScrollChars()
{
    UINT nScrChars = 0;
    if( !SystemParametersInfoW( SPI_GETWHEELSCROLLCHARS, 0, &nScrChars, 0 ) )
    {
        return 3;
    }

    // system settings successfully read
    return nScrChars;
}

static void ImplSalAddBorder( const WinSalFrame* pFrame, int& width, int& height )
{
    // transform client size into window size
    RECT    aWinRect;
    aWinRect.left   = 0;
    aWinRect.right  = width-1;
    aWinRect.top    = 0;
    aWinRect.bottom = height-1;
    AdjustWindowRectEx( &aWinRect, GetWindowStyle( pFrame->mhWnd ),
                        FALSE,     GetWindowExStyle( pFrame->mhWnd ) );
    width  = aWinRect.right - aWinRect.left + 1;
    height = aWinRect.bottom - aWinRect.top + 1;
}

static void ImplSalCalcFullScreenSize( const WinSalFrame* pFrame,
                                       int& rX, int& rY, int& rDX, int& rDY )
{
    // set window to screen size
    int nFrameX;
    int nFrameY;
    int nCaptionY;
    int nScreenX = 0;
    int nScreenY = 0;
    int nScreenDX = 0;
    int nScreenDY = 0;

    if ( pFrame->mbSizeBorder )
    {
        nFrameX = GetSystemMetrics( SM_CXSIZEFRAME );
        nFrameY = GetSystemMetrics( SM_CYSIZEFRAME );
    }
    else if ( pFrame->mbFixBorder )
    {
        nFrameX = GetSystemMetrics( SM_CXFIXEDFRAME );
        nFrameY = GetSystemMetrics( SM_CYFIXEDFRAME );
    }
    else if ( pFrame->mbBorder )
    {
        nFrameX = GetSystemMetrics( SM_CXBORDER );
        nFrameY = GetSystemMetrics( SM_CYBORDER );
    }
    else
    {
        nFrameX = 0;
        nFrameY = 0;
    }
    if ( pFrame->mbCaption )
        nCaptionY = GetSystemMetrics( SM_CYCAPTION );
    else
        nCaptionY = 0;

    try
    {
        AbsoluteScreenPixelRectangle aRect;
        sal_Int32 nMonitors = Application::GetScreenCount();
        if( (pFrame->mnDisplay >= 0) && (pFrame->mnDisplay < nMonitors) )
        {
            aRect = Application::GetScreenPosSizePixel(pFrame->mnDisplay);
        }
        else
        {
            for (sal_Int32 i = 0; i < nMonitors; i++)
                aRect.Union(Application::GetScreenPosSizePixel(i));
        }
        nScreenX = aRect.Left();
        nScreenY = aRect.Top();
        nScreenDX = aRect.GetWidth() + 1;
        nScreenDY = aRect.GetHeight() + 1;
    }
    catch( Exception& )
    {
    }

    if( !nScreenDX || !nScreenDY )
    {
        nScreenDX   = GetSystemMetrics( SM_CXSCREEN );
        nScreenDY   = GetSystemMetrics( SM_CYSCREEN );
    }

    rX  = nScreenX -nFrameX;
    rY  = nScreenY -(nFrameY+nCaptionY);
    rDX = nScreenDX+(nFrameX*2);
    rDY = nScreenDY+(nFrameY*2)+nCaptionY;
}

static void ImplSalFrameFullScreenPos( WinSalFrame* pFrame, bool bAlways = false )
{
    if ( bAlways || !IsIconic( pFrame->mhWnd ) )
    {
        // set window to screen size
        int nX;
        int nY;
        int nWidth;
        int nHeight;
        ImplSalCalcFullScreenSize( pFrame, nX, nY, nWidth, nHeight );
        SetWindowPos( pFrame->mhWnd, nullptr,
                      nX, nY, nWidth, nHeight,
                      SWP_NOZORDER | SWP_NOACTIVATE );
    }
}

namespace {

void SetForegroundWindow_Impl(HWND hwnd)
{
    if (!Application::IsHeadlessModeEnabled())
        SetForegroundWindow(hwnd);
}

}

WinSalFrame::WinSalFrame()
{
    SalData* pSalData = GetSalData();

    mhWnd               = nullptr;
    mhCursor            = LoadCursor( nullptr, IDC_ARROW );
    mhDefIMEContext     = nullptr;
    mpLocalGraphics     = nullptr;
    mpThreadGraphics    = nullptr;
    m_eState = vcl::WindowState::Normal;
    mnShowState         = SW_SHOWNORMAL;
    mnMinWidth          = 0;
    mnMinHeight         = 0;
    mnMaxWidth          = SHRT_MAX;
    mnMaxHeight         = SHRT_MAX;
    mnInputLang         = 0;
    mnInputCodePage     = 0;
    mbGraphicsAcquired  = false;
    mbCaption           = false;
    mbBorder            = false;
    mbFixBorder         = false;
    mbSizeBorder        = false;
    mbFullScreenCaption = false;
    mbPresentation      = false;
    mbInShow            = false;
    mbRestoreMaximize   = false;
    mbInMoveMsg         = false;
    mbInSizeMsg         = false;
    mbFullScreenToolWin = false;
    mbDefPos            = true;
    mbOverwriteState    = true;
    mbIME               = false;
    mbHandleIME         = false;
    mbSpezIME           = false;
    mbAtCursorIME       = false;
    mbCandidateMode     = false;
    mbFloatWin          = false;
    mbNoIcon            = false;
    mSelectedhMenu      = nullptr;
    mLastActivatedhMenu = nullptr;
    mpClipRgnData       = nullptr;
    mbFirstClipRect     = true;
    mpNextClipRect      = nullptr;
    mnDisplay           = 0;
    mbPropertiesStored  = false;
    m_pTaskbarList3     = nullptr;
    maFirstPanGesturePt = POINT(0,0);

    // get data, when making 1st frame
    if ( !pSalData->mpFirstFrame )
    {
        if ( !aSalShlData.mnWheelScrollLines )
            aSalShlData.mnWheelScrollLines = ImplSalGetWheelScrollLines();
        if ( !aSalShlData.mnWheelScrollChars )
            aSalShlData.mnWheelScrollChars = ImplSalGetWheelScrollChars();
    }

    // insert frame in framelist
    mpNextFrame = pSalData->mpFirstFrame;
    pSalData->mpFirstFrame = this;
}

void WinSalFrame::updateScreenNumber()
{
    if( mnDisplay == -1 ) // spans all monitors
        return;
    WinSalSystem* pSys = static_cast<WinSalSystem*>(ImplGetSalSystem());
    if( pSys )
    {
        const std::vector<WinSalSystem::DisplayMonitor>& rMonitors =
            pSys->getMonitors();
        AbsoluteScreenPixelPoint aPoint(maGeometry.pos());
        size_t nMon = rMonitors.size();
        for( size_t i = 0; i < nMon; i++ )
        {
            if( rMonitors[i].m_aArea.Contains( aPoint ) )
            {
                mnDisplay = static_cast<sal_Int32>(i);
                maGeometry.setScreen(static_cast<unsigned int>(i));
            }
        }
    }
}

WinSalFrame::~WinSalFrame()
{
    SalData* pSalData = GetSalData();

    if( mpClipRgnData )
        delete [] reinterpret_cast<BYTE*>(mpClipRgnData);

    // remove frame from framelist
    WinSalFrame** ppFrame = &pSalData->mpFirstFrame;
    for(; (*ppFrame != this) && *ppFrame; ppFrame = &(*ppFrame)->mpNextFrame );
    if( *ppFrame )
        *ppFrame = mpNextFrame;
    mpNextFrame = nullptr;

    // destroy the thread SalGraphics
    if ( mpThreadGraphics )
    {
        HDC hDC = mpThreadGraphics->getHDC();
        if (hDC)
        {
            mpThreadGraphics->setHDC(nullptr);
            SendMessageW( pSalData->mpInstance->mhComWnd, SAL_MSG_RELEASEDC,
                reinterpret_cast<WPARAM>(mhWnd), reinterpret_cast<LPARAM>(hDC) );
        }
        delete mpThreadGraphics;
        mpThreadGraphics = nullptr;
    }

    // destroy the local SalGraphics
    if ( mpLocalGraphics )
    {
        HDC hDC = mpLocalGraphics->getHDC();
        mpLocalGraphics->setHDC(nullptr);
        ReleaseDC(mhWnd, hDC);
        delete mpLocalGraphics;
        mpLocalGraphics = nullptr;
    }

    if ( m_pTaskbarList3 )
    {
        m_pTaskbarList3->Release();
    }

    if ( mhWnd )
    {
        // reset mouse leave data
        if ( pSalData->mhWantLeaveMsg == mhWnd )
        {
            pSalData->mhWantLeaveMsg = nullptr;
        }

        // remove windows properties
        if ( mbPropertiesStored )
            SetApplicationID( OUString() );

        // destroy system frame
        if ( !DestroyWindow( mhWnd ) )
            SetWindowPtr( mhWnd, nullptr );

        mhWnd = nullptr;
    }
}

SalGraphics* WinSalFrame::AcquireGraphics()
{
    if ( mbGraphicsAcquired || !mhWnd )
        return nullptr;

    SalData* pSalData = GetSalData();

    // Other threads get an own DC, because Windows modify in the
    // other case our DC (changing clip region), when they send a
    // WM_ERASEBACKGROUND message
    if ( !pSalData->mpInstance->IsMainThread() )
    {
        HDC hDC = reinterpret_cast<HDC>(static_cast<sal_IntPtr>(SendMessageW( pSalData->mpInstance->mhComWnd,
                                    SAL_MSG_GETCACHEDDC, reinterpret_cast<WPARAM>(mhWnd), 0 )));
        if ( !hDC )
            return nullptr;

        if ( !mpThreadGraphics )
            mpThreadGraphics = new WinSalGraphics(WinSalGraphics::WINDOW, true, mhWnd, this);

        assert(!mpThreadGraphics->getHDC() && "this is supposed to be zeroed when ReleaseGraphics is called");
        mpThreadGraphics->setHDC( hDC );

        mbGraphicsAcquired = true;
        return mpThreadGraphics;
    }
    else
    {
        if ( !mpLocalGraphics )
        {
            HDC hDC = GetDC( mhWnd );
            if ( !hDC )
                return nullptr;
            mpLocalGraphics = new WinSalGraphics(WinSalGraphics::WINDOW, true, mhWnd, this);
            mpLocalGraphics->setHDC( hDC );
        }
        mbGraphicsAcquired = true;
        return mpLocalGraphics;
    }
}

void WinSalFrame::ReleaseGraphics( SalGraphics* pGraphics )
{
    if ( mpThreadGraphics == pGraphics )
    {
        SalData* pSalData = GetSalData();
        HDC hDC = mpThreadGraphics->getHDC();
        assert(hDC);
        mpThreadGraphics->setHDC(nullptr);
        SendMessageW( pSalData->mpInstance->mhComWnd, SAL_MSG_RELEASEDC,
            reinterpret_cast<WPARAM>(mhWnd), reinterpret_cast<LPARAM>(hDC) );
    }
    mbGraphicsAcquired = false;
}

bool WinSalFrame::PostEvent(std::unique_ptr<ImplSVEvent> pData)
{
    bool const ret = PostMessageW(mhWnd, SAL_MSG_USEREVENT, 0, reinterpret_cast<LPARAM>(pData.get()));
    SAL_WARN_IF(!ret, "vcl", "ERROR: PostMessage() failed!");
    if (ret)
        pData.release();
    return ret;
}

void WinSalFrame::SetTitle( const OUString& rTitle )
{
    static_assert( sizeof( WCHAR ) == sizeof( sal_Unicode ), "must be the same size" );

    SetWindowTextW( mhWnd, o3tl::toW(rTitle.getStr()) );
}

void WinSalFrame::SetIcon( sal_uInt16 nIcon )
{
    // If we have a window without an Icon (for example a dialog), ignore this call
    if ( mbNoIcon )
        return;

    // 0 means default (class) icon
    HICON hIcon = nullptr, hSmIcon = nullptr;
    if ( !nIcon )
        nIcon = 1;

    ImplLoadSalIcon( nIcon, hIcon, hSmIcon );

    SAL_WARN_IF( !hIcon , "vcl",   "WinSalFrame::SetIcon(): Could not load large icon !" );
    SAL_WARN_IF( !hSmIcon , "vcl", "WinSalFrame::SetIcon(): Could not load small icon !" );

    SendMessageW( mhWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon) );
    SendMessageW( mhWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmIcon) );
}

void WinSalFrame::SetMenu( SalMenu* pSalMenu )
{
    WinSalMenu* pWMenu = static_cast<WinSalMenu*>(pSalMenu);
    if( pSalMenu && pWMenu->mbMenuBar )
        ::SetMenu( mhWnd, pWMenu->mhMenu );
}

static HWND ImplGetParentHwnd( HWND hWnd )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if( !pFrame || !pFrame->GetWindow())
        return ::GetParent( hWnd );
    vcl::Window *pRealParent = pFrame->GetWindow()->ImplGetWindowImpl()->mpRealParent;
    if( pRealParent )
        return static_cast<WinSalFrame*>(pRealParent->ImplGetWindowImpl()->mpFrame)->mhWnd;
    else
        return ::GetParent( hWnd );

}

SalFrame* WinSalFrame::GetParent() const
{
    return GetWindowPtr( ImplGetParentHwnd( mhWnd ) );
}

static void ImplSalShow( HWND hWnd, bool bVisible, bool bNoActivate )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return;

    if ( bVisible )
    {
        pFrame->mbDefPos = false;
        pFrame->mbOverwriteState = true;
        pFrame->mbInShow = true;

        // #i4715, save position
        RECT aRectPreMatrox, aRectPostMatrox;
        GetWindowRect( hWnd, &aRectPreMatrox );

        vcl::DeletionListener aDogTag( pFrame );
        if( bNoActivate )
            ShowWindow( hWnd, SW_SHOWNOACTIVATE );
        else
            ShowWindow( hWnd, pFrame->mnShowState );
        if( aDogTag.isDeleted() )
            return;

        if (pFrame->mbFloatWin && !(pFrame->mnStyle & SalFrameStyleFlags::NOSHADOW))
        {
            // erase the window immediately to improve XP shadow effect
            // otherwise the shadow may appears long time before the rest of the window
            // especially when accessibility is on
            HDC dc = GetDC( hWnd );
            RECT aRect;
            GetClientRect( hWnd, &aRect );
            FillRect( dc, &aRect, reinterpret_cast<HBRUSH>(COLOR_MENU+1) ); // choose the menucolor, because its mostly noticeable for menus
            ReleaseDC( hWnd, dc );
        }

        // #i4715, matrox centerpopup might have changed our position
        // reposition popups without caption (menus, dropdowns, tooltips)
        GetWindowRect( hWnd, &aRectPostMatrox );
        if( (GetWindowStyle( hWnd ) & WS_POPUP) &&
            !pFrame->mbCaption &&
            (aRectPreMatrox.left != aRectPostMatrox.left || aRectPreMatrox.top != aRectPostMatrox.top) )
            SetWindowPos( hWnd, nullptr, aRectPreMatrox.left, aRectPreMatrox.top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE  );

        if( aDogTag.isDeleted() )
            return;
        vcl::Window *pClientWin = pFrame->GetWindow()->ImplGetClientWindow();
        if ( pFrame->mbFloatWin || ( pClientWin && (pClientWin->GetStyle() & WB_SYSTEMFLOATWIN) ) )
            pFrame->mnShowState = SW_SHOWNOACTIVATE;
        else
            pFrame->mnShowState = SW_SHOW;
        // hide toolbar for W98
        if ( pFrame->mbPresentation )
        {
            HWND hWndParent = ::GetParent( hWnd );
            if ( hWndParent )
                SetForegroundWindow_Impl( hWndParent );
            SetForegroundWindow_Impl( hWnd );
        }

        pFrame->mbInShow = false;
        pFrame->updateScreenNumber();

        // Direct Paint only, if we get the SolarMutex
        if ( ImplSalYieldMutexTryToAcquire() )
        {
            UpdateWindow( hWnd );
            ImplSalYieldMutexRelease();
        }
    }
    else
    {
        ShowWindow( hWnd, SW_HIDE );
    }
}

void WinSalFrame::SetExtendedFrameStyle( SalExtStyle )
{
}

void WinSalFrame::Show( bool bVisible, bool bNoActivate )
{
    // Post this Message to the window, because this only works
    // in the thread of the window, which has create this window.
    // We post this message to avoid deadlocks
    if ( GetSalData()->mnAppThreadId != GetCurrentThreadId() )
    {
        bool const ret = PostMessageW(mhWnd, SAL_MSG_SHOW, WPARAM(bVisible), LPARAM(bNoActivate));
        SAL_WARN_IF(!ret, "vcl", "ERROR: PostMessage() failed!");
    }
    else
        ImplSalShow( mhWnd, bVisible, bNoActivate );
}

void WinSalFrame::SetMinClientSize( tools::Long nWidth, tools::Long nHeight )
{
    mnMinWidth  = nWidth;
    mnMinHeight = nHeight;
}

void WinSalFrame::SetMaxClientSize( tools::Long nWidth, tools::Long nHeight )
{
    mnMaxWidth  = nWidth;
    mnMaxHeight = nHeight;
}

void WinSalFrame::SetPosSize( tools::Long nX, tools::Long nY, tools::Long nWidth, tools::Long nHeight,
                                                   sal_uInt16 nFlags )
{
    bool bVisible = (GetWindowStyle( mhWnd ) & WS_VISIBLE) != 0;
    if ( !bVisible )
    {
        vcl::Window *pClientWin = GetWindow()->ImplGetClientWindow();
        if ( mbFloatWin || ( pClientWin && (pClientWin->GetStyle() & WB_SYSTEMFLOATWIN) ) )
                mnShowState = SW_SHOWNOACTIVATE;
        else
                mnShowState = SW_SHOWNORMAL;
    }
    else
    {
        if ( IsIconic( mhWnd ) || IsZoomed( mhWnd ) )
                ShowWindow( mhWnd, SW_RESTORE );
    }

    SalEvent nEvent = SalEvent::NONE;
    UINT    nPosSize = 0;
    RECT    aClientRect, aWindowRect;
    GetClientRect( mhWnd, &aClientRect );   // x,y always 0,0, but width and height without border
    GetWindowRect( mhWnd, &aWindowRect );   // x,y in screen coordinates, width and height with border

    if ( !(nFlags & (SAL_FRAME_POSSIZE_X | SAL_FRAME_POSSIZE_Y)) )
        nPosSize |= SWP_NOMOVE;
    else
    {
        //SAL_WARN_IF( !nX || !nY, "vcl", " Windowposition of (0,0) requested!" );
        nEvent = SalEvent::Move;
    }
    if ( !(nFlags & (SAL_FRAME_POSSIZE_WIDTH | SAL_FRAME_POSSIZE_HEIGHT)) )
        nPosSize |= SWP_NOSIZE;
    else
        nEvent = (nEvent == SalEvent::Move) ? SalEvent::MoveResize : SalEvent::Resize;

    if ( !(nFlags & SAL_FRAME_POSSIZE_X) )
        nX = aWindowRect.left;
    if ( !(nFlags & SAL_FRAME_POSSIZE_Y) )
        nY = aWindowRect.top;
    if ( !(nFlags & SAL_FRAME_POSSIZE_WIDTH) )
        nWidth = aClientRect.right-aClientRect.left;
    if ( !(nFlags & SAL_FRAME_POSSIZE_HEIGHT) )
        nHeight = aClientRect.bottom-aClientRect.top;

    // Calculate window size including the border
    RECT    aWinRect;
    aWinRect.left   = 0;
    aWinRect.right  = static_cast<int>(nWidth)-1;
    aWinRect.top    = 0;
    aWinRect.bottom = static_cast<int>(nHeight)-1;
    AdjustWindowRectEx( &aWinRect, GetWindowStyle( mhWnd ),
                        FALSE,     GetWindowExStyle( mhWnd ) );
    nWidth  = aWinRect.right - aWinRect.left + 1;
    nHeight = aWinRect.bottom - aWinRect.top + 1;

    HWND hWndParent = ImplGetParentHwnd(mhWnd);
    // For dialogs (WS_POPUP && WS_DLGFRAME), we need to find the "real" parent,
    // in case multiple dialogs are stacked on each other
    // (we don't want to position the second dialog relative to the first one, but relative to the main window)
    if ( (GetWindowStyle( mhWnd ) & WS_POPUP) &&  (GetWindowStyle( mhWnd ) & WS_DLGFRAME) ) // mhWnd is a dialog
    {
        while ( hWndParent && (GetWindowStyle( hWndParent ) & WS_POPUP) &&  (GetWindowStyle( hWndParent ) & WS_DLGFRAME) )
        {
            hWndParent = ::ImplGetParentHwnd( hWndParent );
        }
    }

    if ( !(nPosSize & SWP_NOMOVE) && hWndParent )
    {
            RECT aParentRect;
            GetClientRect( hWndParent, &aParentRect );
            if( AllSettings::GetLayoutRTL() )
                nX = (aParentRect.right - aParentRect.left) - nWidth-1 - nX;

            //#110386#, do not transform coordinates for system child windows
            if( !(GetWindowStyle( mhWnd ) & WS_CHILD) )
            {
                POINT aPt;
                aPt.x = nX;
                aPt.y = nY;

                WinSalFrame* pParentFrame = GetWindowPtr( hWndParent );
                if ( pParentFrame && pParentFrame->mnShowState == SW_SHOWMAXIMIZED )
                {
                    // #i42485#: parent will be shown maximized in which case
                    // a ClientToScreen uses the wrong coordinates (i.e. those from the restore pos)
                    // so use the (already updated) frame geometry for the transformation
                    aPt.x +=  pParentFrame->maGeometry.x();
                    aPt.y +=  pParentFrame->maGeometry.y();
                }
                else
                    ClientToScreen( hWndParent, &aPt );

                nX = aPt.x;
                nY = aPt.y;

                // the position is set
                mbDefPos = false;
            }
    }

    // #i3338# to be conformant to UNIX we must position the client window, ie without the decoration
    // #i43250# if the position was read from the system (GetWindowRect(), see above), it must not be modified
    if ( nFlags & SAL_FRAME_POSSIZE_X )
        nX += aWinRect.left;
    if ( nFlags & SAL_FRAME_POSSIZE_Y )
        nY += aWinRect.top;

    int     nScreenX;
    int     nScreenY;
    int     nScreenWidth;
    int     nScreenHeight;

    RECT aRect;
    ImplSalGetWorkArea( mhWnd, &aRect, nullptr );
    nScreenX        = aRect.left;
    nScreenY        = aRect.top;
    nScreenWidth    = aRect.right-aRect.left;
    nScreenHeight   = aRect.bottom-aRect.top;

    if ( mbDefPos && (nPosSize & SWP_NOMOVE)) // we got no positioning request, so choose default position
    {
        // center window

        HWND hWndParent2 = ::GetParent( mhWnd );
        // Search for TopLevel Frame
        while ( hWndParent2 && (GetWindowStyle( hWndParent2 ) & WS_CHILD) )
            hWndParent2 = ::GetParent( hWndParent2 );
        // if the Window has a Parent, then center the window to
        // the parent, in the other case to the screen
        if ( hWndParent2 && !IsIconic( hWndParent2 ) &&
             (GetWindowStyle( hWndParent2 ) & WS_VISIBLE) )
        {
            RECT aParentRect;
            GetWindowRect( hWndParent2, &aParentRect );
            int nParentWidth    = aParentRect.right-aParentRect.left;
            int nParentHeight   = aParentRect.bottom-aParentRect.top;

            // We don't center, when Parent is smaller than our window
            if ( (nParentWidth-GetSystemMetrics( SM_CXFIXEDFRAME ) <= nWidth) &&
                 (nParentHeight-GetSystemMetrics( SM_CYFIXEDFRAME ) <= nHeight) )
            {
                int nOff = GetSystemMetrics( SM_CYSIZEFRAME ) + GetSystemMetrics( SM_CYCAPTION );
                nX = aParentRect.left+nOff;
                nY = aParentRect.top+nOff;
            }
            else
            {
                nX = (nParentWidth-nWidth)/2 + aParentRect.left;
                nY = (nParentHeight-nHeight)/2 + aParentRect.top;
            }
        }
        else
        {
            POINT pt;
            GetCursorPos( &pt );
            RECT aRect2;
            aRect2.left = pt.x;
            aRect2.top = pt.y;
            aRect2.right = pt.x+2;
            aRect2.bottom = pt.y+2;

            // dualmonitor support:
            // Get screensize of the monitor with the mouse pointer
            ImplSalGetWorkArea( mhWnd, &aRect2, &aRect2 );

            nX = ((aRect2.right-aRect2.left)-nWidth)/2 + aRect2.left;
            nY = ((aRect2.bottom-aRect2.top)-nHeight)/2 + aRect2.top;
        }

        //if ( bVisible )
        //    mbDefPos = FALSE;

        mbDefPos = false;   // center only once
        nPosSize &= ~SWP_NOMOVE;        // activate positioning
        nEvent = SalEvent::MoveResize;
    }

    // Adjust Window in the screen
    bool bCheckOffScreen = true;

    // but don't do this for floaters or ownerdraw windows that are currently moved interactively
    if( (mnStyle & SalFrameStyleFlags::FLOAT) && !(mnStyle & SalFrameStyleFlags::OWNERDRAWDECORATION) )
        bCheckOffScreen = false;

    if( mnStyle & SalFrameStyleFlags::OWNERDRAWDECORATION )
    {
        // may be the window is currently being moved (mouse is captured), then no check is required
        if( mhWnd == ::GetCapture() )
            bCheckOffScreen = false;
        else
            bCheckOffScreen = true;
    }

    if( bCheckOffScreen )
    {
        if ( nX+nWidth > nScreenX+nScreenWidth )
            nX = (nScreenX+nScreenWidth) - nWidth;
        if ( nY+nHeight > nScreenY+nScreenHeight )
            nY = (nScreenY+nScreenHeight) - nHeight;
        if ( nX < nScreenX )
            nX = nScreenX;
        if ( nY < nScreenY )
            nY = nScreenY;
    }

    UINT nPosFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | nPosSize;
    // bring floating windows always to top
    if( !(mnStyle & SalFrameStyleFlags::FLOAT) )
        nPosFlags |= SWP_NOZORDER; // do not change z-order

    SetWindowPos( mhWnd, HWND_TOP, nX, nY, static_cast<int>(nWidth), static_cast<int>(nHeight), nPosFlags  );

    UpdateFrameGeometry();

    // Notification -- really ???
    if( nEvent != SalEvent::NONE )
        CallCallback( nEvent, nullptr );
}

void WinSalFrame::ImplSetParentFrame( HWND hNewParentWnd, bool bAsChild )
{
    // save hwnd, will be overwritten in WM_CREATE during createwindow
    HWND hWndOld = mhWnd;
    HWND hWndOldParent = ::GetParent( hWndOld );
    SalData* pSalData = GetSalData();

    if( hNewParentWnd == hWndOldParent )
        return;

    ::std::vector< WinSalFrame* > children;
    ::std::vector< WinSalObject* > systemChildren;

    // search child windows
    WinSalFrame *pFrame = pSalData->mpFirstFrame;
    while( pFrame )
    {
        HWND hWndParent = ::GetParent( pFrame->mhWnd );
        if( mhWnd == hWndParent )
            children.push_back( pFrame );
        pFrame = pFrame->mpNextFrame;
    }

    // search system child windows (plugins etc.)
    WinSalObject *pObject = pSalData->mpFirstObject;
    while( pObject )
    {
        HWND hWndParent = ::GetParent( pObject->mhWnd );
        if( mhWnd == hWndParent )
            systemChildren.push_back( pObject );
        pObject = pObject->mpNextObject;
    }

    // to recreate the DCs, if they were destroyed
    bool bHadLocalGraphics = false, bHadThreadGraphics = false;

    HFONT   hFont   = nullptr;
    HPEN    hPen    = nullptr;
    HBRUSH  hBrush  = nullptr;

    // release the thread DC
    if ( mpThreadGraphics )
    {
        // save current gdi objects before hdc is gone
        HDC hDC = mpThreadGraphics->getHDC();
        if (hDC)
        {
            hFont  = static_cast<HFONT>(GetCurrentObject( hDC, OBJ_FONT ));
            hPen   = static_cast<HPEN>(GetCurrentObject( hDC, OBJ_PEN ));
            hBrush = static_cast<HBRUSH>(GetCurrentObject( hDC, OBJ_BRUSH ));

            mpThreadGraphics->setHDC(nullptr);
            SendMessageW( pSalData->mpInstance->mhComWnd, SAL_MSG_RELEASEDC,
                reinterpret_cast<WPARAM>(mhWnd), reinterpret_cast<LPARAM>(hDC) );

            bHadThreadGraphics = true;
        }
    }

    // release the local DC
    if ( mpLocalGraphics )
    {
        bHadLocalGraphics = true;
        HDC hDC = mpLocalGraphics->getHDC();
        mpLocalGraphics->setHDC(nullptr);
        ReleaseDC(mhWnd, hDC);
    }

    // create a new hwnd with the same styles
    HWND hWndParent = hNewParentWnd;
    // forward to main thread
    HWND hWnd = reinterpret_cast<HWND>(static_cast<sal_IntPtr>(SendMessageW( pSalData->mpInstance->mhComWnd,
                                        bAsChild ? SAL_MSG_RECREATECHILDHWND : SAL_MSG_RECREATEHWND,
                                        reinterpret_cast<WPARAM>(hWndParent), reinterpret_cast<LPARAM>(mhWnd) )));

    // succeeded ?
    SAL_WARN_IF( !IsWindow( hWnd ), "vcl", "WinSalFrame::SetParent not successful");

    // re-create thread DC
    if( bHadThreadGraphics )
    {
        mpThreadGraphics->setHWND( hWnd );
        HDC hDC = reinterpret_cast<HDC>(static_cast<sal_IntPtr>(
                    SendMessageW( pSalData->mpInstance->mhComWnd,
                        SAL_MSG_GETCACHEDDC, reinterpret_cast<WPARAM>(hWnd), 0 )));
        if ( hDC )
        {
            mpThreadGraphics->setHDC( hDC );
            // re-select saved gdi objects
            if( hFont )
                SelectObject( hDC, hFont );
            if( hPen )
                SelectObject( hDC, hPen );
            if( hBrush )
                SelectObject( hDC, hBrush );
        }
    }

    // re-create local DC
    if( bHadLocalGraphics )
    {
        mpLocalGraphics->setHWND( hWnd );
        HDC hDC = GetDC( hWnd );
        if (hDC)
            mpLocalGraphics->setHDC( hDC );
    }

    // TODO: add SetParent() call for SalObjects
    SAL_WARN_IF( !systemChildren.empty(), "vcl", "WinSalFrame::SetParent() parent of living system child window will be destroyed!");

    // reparent children before old parent is destroyed
    for (auto & child : children)
        child->ImplSetParentFrame( hWnd, false );

    children.clear();
    systemChildren.clear();

    // Now destroy original HWND in the thread where it was created.
    SendMessageW( GetSalData()->mpInstance->mhComWnd,
                     SAL_MSG_DESTROYHWND, WPARAM(0), reinterpret_cast<LPARAM>(hWndOld));
}

void WinSalFrame::SetParent( SalFrame* pNewParent )
{
    WinSalFrame::mbInReparent = true;
    ImplSetParentFrame( static_cast<WinSalFrame*>(pNewParent)->mhWnd, false );
    WinSalFrame::mbInReparent = false;
}

void WinSalFrame::SetPluginParent( SystemParentData* pNewParent )
{
    if ( pNewParent->hWnd == nullptr )
    {
        pNewParent->hWnd = GetDesktopWindow();
    }

    WinSalFrame::mbInReparent = true;
    ImplSetParentFrame( pNewParent->hWnd, true );
    WinSalFrame::mbInReparent = false;
}

void WinSalFrame::GetWorkArea( AbsoluteScreenPixelRectangle &rRect )
{
    RECT aRect;

    // pass cursor's position to ImplSalGetWorkArea to determine work area on
    // multi monitor setups correctly.
    POINT aPoint;
    GetCursorPos(&aPoint);
    RECT aRect2{ aPoint.x, aPoint.y, aPoint.x + 2, aPoint.y + 2 };

    ImplSalGetWorkArea( mhWnd, &aRect, &aRect2 );
    rRect.SetLeft( aRect.left );
    rRect.SetRight( aRect.right-1 );
    rRect.SetTop( aRect.top );
    rRect.SetBottom( aRect.bottom-1 );
}

void WinSalFrame::GetClientSize( tools::Long& rWidth, tools::Long& rHeight )
{
    rWidth  = maGeometry.width();
    rHeight = maGeometry.height();
}

void WinSalFrame::SetWindowState(const vcl::WindowData* pState)
{
    // Check if the window fits into the screen, in case the screen
    // resolution changed
    int     nX;
    int     nY;
    int     nWidth;
    int     nHeight;
    int     nScreenX;
    int     nScreenY;
    int     nScreenWidth;
    int     nScreenHeight;

    RECT aRect;
    ImplSalGetWorkArea( mhWnd, &aRect, nullptr );
    // #102500# allow some overlap, the window could have been made a little larger than the physical screen
    nScreenX        = aRect.left-10;
    nScreenY        = aRect.top-10;
    nScreenWidth    = aRect.right-aRect.left+20;
    nScreenHeight   = aRect.bottom-aRect.top+20;

    UINT    nPosSize    = 0;
    RECT    aWinRect;
    GetWindowRect( mhWnd, &aWinRect );

    // to be consistent with Unix, the frame state is without(!) decoration
    // ->add the decoration
    RECT aRect2 = aWinRect;
    AdjustWindowRectEx( &aRect2, GetWindowStyle( mhWnd ),
                    FALSE,     GetWindowExStyle( mhWnd ) );
    tools::Long nTopDeco = abs( aWinRect.top - aRect2.top );
    tools::Long nLeftDeco = abs( aWinRect.left - aRect2.left );
    tools::Long nBottomDeco = abs( aWinRect.bottom - aRect2.bottom );
    tools::Long nRightDeco = abs( aWinRect.right - aRect2.right );

    // adjust window position/size to fit the screen
    if ( !(pState->mask() & vcl::WindowDataMask::Pos) )
        nPosSize |= SWP_NOMOVE;
    if ( !(pState->mask() & vcl::WindowDataMask::Size) )
        nPosSize |= SWP_NOSIZE;
    if ( pState->mask() & vcl::WindowDataMask::X )
        nX = static_cast<int>(pState->x()) - nLeftDeco;
    else
        nX = aWinRect.left;
    if ( pState->mask() & vcl::WindowDataMask::Y )
        nY = static_cast<int>(pState->y()) - nTopDeco;
    else
        nY = aWinRect.top;
    if ( pState->mask() & vcl::WindowDataMask::Width )
        nWidth = static_cast<int>(pState->width()) + nLeftDeco + nRightDeco;
    else
        nWidth = aWinRect.right-aWinRect.left;
    if ( pState->mask() & vcl::WindowDataMask::Height )
        nHeight = static_cast<int>(pState->height()) + nTopDeco + nBottomDeco;
    else
        nHeight = aWinRect.bottom-aWinRect.top;

    // Adjust Window in the screen:
    // if it does not fit into the screen do nothing, ie default pos/size will be used
    // if there is an overlap with the screen border move the window while keeping its size

    if( nWidth > nScreenWidth || nHeight > nScreenHeight )
        nPosSize |= (SWP_NOMOVE | SWP_NOSIZE);

    if ( nX+nWidth > nScreenX+nScreenWidth )
        nX = (nScreenX+nScreenWidth) - nWidth;
    if ( nY+nHeight > nScreenY+nScreenHeight )
        nY = (nScreenY+nScreenHeight) - nHeight;
    if ( nX < nScreenX )
        nX = nScreenX;
    if ( nY < nScreenY )
        nY = nScreenY;

    // set Restore-Position
    WINDOWPLACEMENT aPlacement;
    aPlacement.length = sizeof( aPlacement );
    GetWindowPlacement( mhWnd, &aPlacement );

    // set State
    const bool bIsMinimized = IsIconic(mhWnd);
    const bool bIsMaximized = IsZoomed(mhWnd);
    const bool bVisible = (GetWindowStyle(mhWnd) & WS_VISIBLE);
    bool bUpdateHiddenFramePos = false;
    if ( !bVisible )
    {
        aPlacement.showCmd = SW_HIDE;

        if (mbOverwriteState && (pState->mask() & vcl::WindowDataMask::State))
        {
            if (pState->state() & vcl::WindowState::Minimized)
                mnShowState = SW_SHOWMINIMIZED;
            else if (pState->state() & vcl::WindowState::Maximized)
            {
                mnShowState = SW_SHOWMAXIMIZED;
                bUpdateHiddenFramePos = true;
            }
            else if (pState->state() & vcl::WindowState::Normal)
                mnShowState = SW_SHOWNORMAL;
        }
    }
    else
    {
        if ( pState->mask() & vcl::WindowDataMask::State )
        {
            if ( pState->state() & vcl::WindowState::Minimized )
            {
                if ( pState->state() & vcl::WindowState::Maximized )
                    aPlacement.flags |= WPF_RESTORETOMAXIMIZED;
                aPlacement.showCmd = SW_SHOWMINIMIZED;
            }
            else if ( pState->state() & vcl::WindowState::Maximized )
                aPlacement.showCmd = SW_SHOWMAXIMIZED;
            else if ( pState->state() & vcl::WindowState::Normal )
                aPlacement.showCmd = SW_RESTORE;
        }
    }

    // if a window is neither minimized nor maximized or need not be
    // positioned visibly (that is in visible state), do not use
    // SetWindowPlacement since it calculates including the TaskBar
    if (!bIsMinimized && !bIsMaximized && (!bVisible || (aPlacement.showCmd == SW_RESTORE)))
    {
        if( bUpdateHiddenFramePos )
        {
            RECT aStateRect;
            aStateRect.left   = nX;
            aStateRect.top    = nY;
            aStateRect.right  = nX+nWidth;
            aStateRect.bottom = nY+nHeight;
            // #96084 set a useful internal window size because
            // the window will not be maximized (and the size updated) before show()
            SetMaximizedFrameGeometry(mhWnd, &aStateRect);
            SetWindowPos( mhWnd, nullptr,
                          maGeometry.x(), maGeometry.y(), maGeometry.width(), maGeometry.height(),
                          SWP_NOZORDER | SWP_NOACTIVATE | nPosSize );
        }
        else
            SetWindowPos( mhWnd, nullptr,
                      nX, nY, nWidth, nHeight,
                      SWP_NOZORDER | SWP_NOACTIVATE | nPosSize );
    }
    else
    {
        if( !(nPosSize & (SWP_NOMOVE|SWP_NOSIZE)) )
        {
            aPlacement.rcNormalPosition.left    = nX-nScreenX;
            aPlacement.rcNormalPosition.top     = nY-nScreenY;
            aPlacement.rcNormalPosition.right   = nX+nWidth-nScreenX;
            aPlacement.rcNormalPosition.bottom  = nY+nHeight-nScreenY;
        }
        SetWindowPlacement( mhWnd, &aPlacement );
    }

    if( !(nPosSize & SWP_NOMOVE) )
        mbDefPos = false; // window was positioned
}

bool WinSalFrame::GetWindowState(vcl::WindowData* pState)
{
    pState->setPosSize(maGeometry.posSize());
    pState->setState(m_eState);
    pState->setMask(vcl::WindowDataMask::PosSizeState);
    return true;
}

void WinSalFrame::SetScreenNumber( unsigned int nNewScreen )
{
    WinSalSystem* pSys = static_cast<WinSalSystem*>(ImplGetSalSystem());
    if( pSys )
    {
        const std::vector<WinSalSystem::DisplayMonitor>& rMonitors =
            pSys->getMonitors();
        size_t nMon = rMonitors.size();
        if( nNewScreen < nMon )
        {
            AbsoluteScreenPixelPoint aOldMonPos, aNewMonPos( rMonitors[nNewScreen].m_aArea.TopLeft() );
            AbsoluteScreenPixelPoint aCurPos(maGeometry.pos());
            for( size_t i = 0; i < nMon; i++ )
            {
                if( rMonitors[i].m_aArea.Contains( aCurPos ) )
                {
                    aOldMonPos = rMonitors[i].m_aArea.TopLeft();
                    break;
                }
            }
            mnDisplay = nNewScreen;
            maGeometry.setScreen(nNewScreen);
            SetPosSize( aNewMonPos.X() + (maGeometry.x() - aOldMonPos.X()),
                        aNewMonPos.Y() + (maGeometry.y() - aOldMonPos.Y()),
                        0, 0,
                        SAL_FRAME_POSSIZE_X | SAL_FRAME_POSSIZE_Y );
        }
    }
}

void WinSalFrame::SetApplicationID( const OUString &rApplicationID )
{
    // http://msdn.microsoft.com/en-us/library/windows/desktop/dd378430(v=vs.85).aspx
    // A window's properties must be removed before the window is closed.

    IPropertyStore *pps;
    HRESULT hr = SHGetPropertyStoreForWindow(mhWnd, IID_PPV_ARGS(&pps));
    if (SUCCEEDED(hr))
    {
        PROPVARIANT pv;
        if (!rApplicationID.isEmpty())
        {
            hr = InitPropVariantFromString(o3tl::toW(rApplicationID.getStr()), &pv);
            mbPropertiesStored = true;
        }
        else
            // if rApplicationID we remove the property from the window, if present
            PropVariantInit(&pv);

        if (SUCCEEDED(hr))
        {
            hr = pps->SetValue(PKEY_AppUserModel_ID, pv);
            PropVariantClear(&pv);
        }
        pps->Release();
    }
}

void WinSalFrame::ShowFullScreen(const bool bFullScreen, const sal_Int32 nDisplay)
{
    if ((isFullScreen() == bFullScreen) && (!bFullScreen || (mnDisplay == nDisplay)))
        return;

    mnDisplay = nDisplay;

    if ( bFullScreen )
    {
        m_eState |= vcl::WindowState::FullScreen;
        // to hide the Windows taskbar
        DWORD nExStyle = GetWindowExStyle( mhWnd );
        if ( nExStyle & WS_EX_TOOLWINDOW )
        {
            mbFullScreenToolWin = true;
            nExStyle &= ~WS_EX_TOOLWINDOW;
            SetWindowExStyle( mhWnd, nExStyle );
        }
        // save old position
        GetWindowRect( mhWnd, &maFullScreenRect );

        // save show state
        mnFullScreenShowState = mnShowState;
        if ( !(GetWindowStyle( mhWnd ) & WS_VISIBLE) )
            mnShowState = SW_SHOW;

        // Save caption state.
        mbFullScreenCaption = mbCaption;
        if (mbCaption)
        {
            DWORD nStyle = GetWindowStyle(mhWnd);
            SetWindowStyle(mhWnd, nStyle & ~WS_CAPTION);
            mbCaption = false;
        }

        // set window to screen size
        ImplSalFrameFullScreenPos( this, true );
    }
    else
    {
        m_eState &= ~vcl::WindowState::FullScreen;
        // when the ShowState has to be reset, hide the window first to
        // reduce flicker
        bool bVisible = (GetWindowStyle( mhWnd ) & WS_VISIBLE) != 0;
        if ( bVisible && (mnShowState != mnFullScreenShowState) )
            ShowWindow( mhWnd, SW_HIDE );

        if ( mbFullScreenToolWin )
            SetWindowExStyle( mhWnd, GetWindowExStyle( mhWnd ) | WS_EX_TOOLWINDOW );
        mbFullScreenToolWin = false;

        // Restore caption state.
        if (mbFullScreenCaption)
        {
            DWORD nStyle = GetWindowStyle(mhWnd);
            SetWindowStyle(mhWnd, nStyle | WS_CAPTION);
        }
        mbCaption = mbFullScreenCaption;

        SetWindowPos( mhWnd, nullptr,
                      maFullScreenRect.left,
                      maFullScreenRect.top,
                      maFullScreenRect.right-maFullScreenRect.left,
                      maFullScreenRect.bottom-maFullScreenRect.top,
                      SWP_NOZORDER | SWP_NOACTIVATE );

        // restore show state
        if ( mnShowState != mnFullScreenShowState )
        {
            mnShowState = mnFullScreenShowState;
            if ( bVisible )
            {
                mbInShow = true;
                ShowWindow( mhWnd, mnShowState );
                mbInShow = false;
                UpdateWindow( mhWnd );
            }
        }
    }
}

void WinSalFrame::StartPresentation( bool bStart )
{
    if ( mbPresentation == bStart )
        return;

    mbPresentation = bStart;

    if ( bStart )
    {
        // turn off screen-saver / power saving when in Presentation mode
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    }
    else
    {
        // turn on screen-saver / power saving back
        SetThreadExecutionState(ES_CONTINUOUS);
    }
}

void WinSalFrame::SetAlwaysOnTop( bool bOnTop )
{
    HWND hWnd;
    if ( bOnTop )
        hWnd = HWND_TOPMOST;
    else
        hWnd = HWND_NOTOPMOST;
    SetWindowPos( mhWnd, hWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
}

static bool EnableAttachThreadInputHack()
{
    OUString s("$EnableAttachThreadInputHack");
    rtl::Bootstrap::expandMacros(s);
    const bool bEnabled = s == "true";
    SAL_WARN_IF(bEnabled, "vcl", "AttachThreadInput hack is enabled. Watch out for deadlocks!");
    return bEnabled;
}

static void ImplSalToTop( HWND hWnd, SalFrameToTop nFlags )
{
    static const bool bEnableAttachThreadInputHack = EnableAttachThreadInputHack();

    WinSalFrame* pToTopFrame = GetWindowPtr( hWnd );
    if( pToTopFrame && (pToTopFrame->mnStyle & SalFrameStyleFlags::SYSTEMCHILD) )
        BringWindowToTop( hWnd );

    if ( nFlags & SalFrameToTop::ForegroundTask )
    {
        // LO used to always call AttachThreadInput here, which resulted in deadlocks
        // in some installations for unknown reasons!
        if (bEnableAttachThreadInputHack)
        {
            // This magic code is necessary to connect the input focus of the
            // current window thread and the thread which owns the window that
            // should be the new foreground window.
            HWND hCurrWnd = GetForegroundWindow();
            DWORD myThreadID = GetCurrentThreadId();
            DWORD currThreadID = GetWindowThreadProcessId(hCurrWnd,nullptr);
            AttachThreadInput(myThreadID, currThreadID, TRUE);
            SetForegroundWindow_Impl(hWnd);
            AttachThreadInput(myThreadID, currThreadID, FALSE);
        }
        else
            SetForegroundWindow_Impl(hWnd);
    }

    if ( nFlags & SalFrameToTop::RestoreWhenMin )
    {
        HWND hIconicWnd = hWnd;
        while ( hIconicWnd )
        {
            if ( IsIconic( hIconicWnd ) )
            {
                WinSalFrame* pFrame = GetWindowPtr( hIconicWnd );
                if ( pFrame )
                {
                    if ( GetWindowPtr( hWnd )->mbRestoreMaximize )
                        ShowWindow( hIconicWnd, SW_MAXIMIZE );
                    else
                        ShowWindow( hIconicWnd, SW_RESTORE );
                }
                else
                    ShowWindow( hIconicWnd, SW_RESTORE );
            }

            hIconicWnd = ::GetParent( hIconicWnd );
        }
    }

    if ( !IsIconic( hWnd ) && IsWindowVisible( hWnd ) )
    {
        SetFocus( hWnd );

        // Windows sometimes incorrectly reports to have the focus;
        // thus make sure to really get the focus
        if ( ::GetFocus() == hWnd )
            SetForegroundWindow_Impl( hWnd );
    }
}

void WinSalFrame::ToTop( SalFrameToTop nFlags )
{
    nFlags &= ~SalFrameToTop::GrabFocus;   // this flag is not needed on win32
    // Post this Message to the window, because this only works
    // in the thread of the window, which has create this window.
    // We post this message to avoid deadlocks
    if ( GetSalData()->mnAppThreadId != GetCurrentThreadId() )
    {
        bool const ret = PostMessageW( mhWnd, SAL_MSG_TOTOP, static_cast<WPARAM>(nFlags), 0 );
        SAL_WARN_IF(!ret, "vcl", "ERROR: PostMessage() failed!");
    }
    else
        ImplSalToTop( mhWnd, nFlags );
}

void WinSalFrame::SetPointer( PointerStyle ePointerStyle )
{
    struct ImplPtrData
    {
        HCURSOR         mhCursor;
        LPCTSTR         mnSysId;
        UINT            mnOwnId;
    };

    static o3tl::enumarray<PointerStyle, ImplPtrData> aImplPtrTab =
    {
    // [-loplugin:redundantfcast]:
    ImplPtrData{ nullptr, IDC_ARROW, 0 },                       // POINTER_ARROW
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_NULL },    // POINTER_NULL
    ImplPtrData{ nullptr, IDC_WAIT, 0 },                        // POINTER_WAIT
    ImplPtrData{ nullptr, IDC_IBEAM, 0 },                       // POINTER_TEXT
    ImplPtrData{ nullptr, IDC_HELP, 0 },                        // POINTER_HELP
    ImplPtrData{ nullptr, IDC_CROSS, 0 },                       // POINTER_CROSS
    ImplPtrData{ nullptr, IDC_SIZEALL, 0 },                     // POINTER_MOVE
    ImplPtrData{ nullptr, IDC_SIZENS, 0 },                      // POINTER_NSIZE
    ImplPtrData{ nullptr, IDC_SIZENS, 0 },                      // POINTER_SSIZE
    ImplPtrData{ nullptr, IDC_SIZEWE, 0 },                      // POINTER_WSIZE
    ImplPtrData{ nullptr, IDC_SIZEWE, 0 },                      // POINTER_ESIZE
    ImplPtrData{ nullptr, IDC_SIZENWSE, 0 },                    // POINTER_NWSIZE
    ImplPtrData{ nullptr, IDC_SIZENESW, 0 },                    // POINTER_NESIZE
    ImplPtrData{ nullptr, IDC_SIZENESW, 0 },                    // POINTER_SWSIZE
    ImplPtrData{ nullptr, IDC_SIZENWSE, 0 },                    // POINTER_SESIZE
    ImplPtrData{ nullptr, IDC_SIZENS, 0 },                      // POINTER_WINDOW_NSIZE
    ImplPtrData{ nullptr, IDC_SIZENS, 0 },                      // POINTER_WINDOW_SSIZE
    ImplPtrData{ nullptr, IDC_SIZEWE, 0 },                      // POINTER_WINDOW_WSIZE
    ImplPtrData{ nullptr, IDC_SIZEWE, 0 },                      // POINTER_WINDOW_ESIZE
    ImplPtrData{ nullptr, IDC_SIZENWSE, 0 },                    // POINTER_WINDOW_NWSIZE
    ImplPtrData{ nullptr, IDC_SIZENESW, 0 },                    // POINTER_WINDOW_NESIZE
    ImplPtrData{ nullptr, IDC_SIZENESW, 0 },                    // POINTER_WINDOW_SWSIZE
    ImplPtrData{ nullptr, IDC_SIZENWSE, 0 },                    // POINTER_WINDOW_SESIZE
    ImplPtrData{ nullptr, IDC_SIZEWE, 0 },                      // POINTER_HSPLIT
    ImplPtrData{ nullptr, IDC_SIZENS, 0 },                      // POINTER_VSPLIT
    ImplPtrData{ nullptr, IDC_SIZEWE, 0 },                      // POINTER_HSIZEBAR
    ImplPtrData{ nullptr, IDC_SIZENS, 0 },                      // POINTER_VSIZEBAR
    ImplPtrData{ nullptr, IDC_HAND, 0 },                        // POINTER_HAND
    ImplPtrData{ nullptr, IDC_HAND, 0 },                        // POINTER_REFHAND
    ImplPtrData{ nullptr, IDC_PEN, 0 },                         // POINTER_PEN
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MAGNIFY }, // POINTER_MAGNIFY
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_FILL },    // POINTER_FILL
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_ROTATE },  // POINTER_ROTATE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_HSHEAR },  // POINTER_HSHEAR
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_VSHEAR },  // POINTER_VSHEAR
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MIRROR },  // POINTER_MIRROR
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_CROOK },   // POINTER_CROOK
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_CROP },    // POINTER_CROP
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MOVEPOINT }, // POINTER_MOVEPOINT
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MOVEBEZIERWEIGHT }, // POINTER_MOVEBEZIERWEIGHT
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MOVEDATA }, // POINTER_MOVEDATA
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_COPYDATA }, // POINTER_COPYDATA
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_LINKDATA }, // POINTER_LINKDATA
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MOVEDATALINK }, // POINTER_MOVEDATALINK
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_COPYDATALINK }, // POINTER_COPYDATALINK
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MOVEFILE }, // POINTER_MOVEFILE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_COPYFILE }, // POINTER_COPYFILE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_LINKFILE }, // POINTER_LINKFILE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MOVEFILELINK }, // POINTER_MOVEFILELINK
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_COPYFILELINK }, // POINTER_COPYFILELINK
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_MOVEFILES }, // POINTER_MOVEFILES
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_COPYFILES }, // POINTER_COPYFILES
    ImplPtrData{ nullptr, IDC_NO, 0 },                          // POINTER_NOTALLOWED
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_LINE }, // POINTER_DRAW_LINE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_RECT }, // POINTER_DRAW_RECT
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_POLYGON }, // POINTER_DRAW_POLYGON
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_BEZIER }, // POINTER_DRAW_BEZIER
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_ARC }, // POINTER_DRAW_ARC
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_PIE }, // POINTER_DRAW_PIE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_CIRCLECUT }, // POINTER_DRAW_CIRCLECUT
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_ELLIPSE }, // POINTER_DRAW_ELLIPSE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_FREEHAND }, // POINTER_DRAW_FREEHAND
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_CONNECT }, // POINTER_DRAW_CONNECT
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_TEXT }, // POINTER_DRAW_TEXT
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DRAW_CAPTION }, // POINTER_DRAW_CAPTION
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_CHART },   // POINTER_CHART
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_DETECTIVE }, // POINTER_DETECTIVE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_PIVOT_COL }, // POINTER_PIVOT_COL
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_PIVOT_ROW }, // POINTER_PIVOT_ROW
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_PIVOT_FIELD }, // POINTER_PIVOT_FIELD
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_CHAIN },   // POINTER_CHAIN
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_CHAIN_NOTALLOWED }, // POINTER_CHAIN_NOTALLOWED
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_N }, // POINTER_AUTOSCROLL_N
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_S }, // POINTER_AUTOSCROLL_S
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_W }, // POINTER_AUTOSCROLL_W
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_E }, // POINTER_AUTOSCROLL_E
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_NW }, // POINTER_AUTOSCROLL_NW
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_NE }, // POINTER_AUTOSCROLL_NE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_SW }, // POINTER_AUTOSCROLL_SW
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_SE }, // POINTER_AUTOSCROLL_SE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_NS }, // POINTER_AUTOSCROLL_NS
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_WE }, // POINTER_AUTOSCROLL_WE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_AUTOSCROLL_NSWE }, // POINTER_AUTOSCROLL_NSWE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_TEXT_VERTICAL }, // POINTER_TEXT_VERTICAL
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_PIVOT_DELETE }, // POINTER_PIVOT_DELETE

     // #i32329#
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_TAB_SELECT_S }, // POINTER_TAB_SELECT_S
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_TAB_SELECT_E }, // POINTER_TAB_SELECT_E
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_TAB_SELECT_SE }, // POINTER_TAB_SELECT_SE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_TAB_SELECT_W }, // POINTER_TAB_SELECT_W
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_TAB_SELECT_SW }, // POINTER_TAB_SELECT_SW

    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_HIDEWHITESPACE }, // POINTER_HIDEWHITESPACE
    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_SHOWWHITESPACE }, // POINTER_UNHIDEWHITESPACE

    ImplPtrData{ nullptr, nullptr, SAL_RESID_POINTER_FATCROSS } // POINTER_FATCROSS
    };

    // Mousepointer loaded ?
    if ( !aImplPtrTab[ePointerStyle].mhCursor )
    {
        if ( aImplPtrTab[ePointerStyle].mnOwnId )
            aImplPtrTab[ePointerStyle].mhCursor = ImplLoadSalCursor( aImplPtrTab[ePointerStyle].mnOwnId );
        else
            aImplPtrTab[ePointerStyle].mhCursor = LoadCursor( nullptr, aImplPtrTab[ePointerStyle].mnSysId );
    }

    // change the mouse pointer if different
    if ( mhCursor != aImplPtrTab[ePointerStyle].mhCursor )
    {
        mhCursor = aImplPtrTab[ePointerStyle].mhCursor;
        SetCursor( mhCursor );
    }
}

void WinSalFrame::CaptureMouse( bool bCapture )
{
    // Send this Message to the window, because CaptureMouse() only work
    // in the thread of the window, which has create this window
    int nMsg;
    if ( bCapture )
        nMsg = SAL_MSG_CAPTUREMOUSE;
    else
        nMsg = SAL_MSG_RELEASEMOUSE;
    SendMessageW( mhWnd, nMsg, 0, 0 );
}

void WinSalFrame::SetPointerPos( tools::Long nX, tools::Long nY )
{
    POINT aPt;
    aPt.x = static_cast<int>(nX);
    aPt.y = static_cast<int>(nY);
    ClientToScreen( mhWnd, &aPt );
    SetCursorPos( aPt.x, aPt.y );
}

void WinSalFrame::Flush()
{
    if(mpLocalGraphics)
        mpLocalGraphics->Flush();
    if(mpThreadGraphics)
        mpThreadGraphics->Flush();
    GdiFlush();
}

static void ImplSalFrameSetInputContext( HWND hWnd, const SalInputContext* pContext )
{
    WinSalFrame*   pFrame = GetWindowPtr( hWnd );
    bool           bIME(pContext->mnOptions & InputContextFlags::Text);
    if ( bIME )
    {
        if ( !pFrame->mbIME )
        {
            pFrame->mbIME = true;

            if ( pFrame->mhDefIMEContext )
            {
                ImmAssociateContext( pFrame->mhWnd, pFrame->mhDefIMEContext );
                UINT nImeProps = ImmGetProperty( GetKeyboardLayout( 0 ), IGP_PROPERTY );
                pFrame->mbSpezIME = (nImeProps & IME_PROP_SPECIAL_UI) != 0;
                pFrame->mbAtCursorIME = (nImeProps & IME_PROP_AT_CARET) != 0;
                pFrame->mbHandleIME = !pFrame->mbSpezIME;
            }
        }

        // When the application can't handle IME messages, then the
        // System should handle the IME handling
        if ( !(pContext->mnOptions & InputContextFlags::ExtText) )
            pFrame->mbHandleIME = false;

        // Set the Font for IME Handling
        if ( pContext->mpFont )
        {
            HIMC hIMC = ImmGetContext( pFrame->mhWnd );
            if ( hIMC )
            {
                LOGFONTW aLogFont;
                ImplGetLogFontFromFontSelect(pContext->mpFont->GetFontSelectPattern(),
                                             nullptr, aLogFont, true);

                // tdf#147299: To enable vertical input mode, Windows IMEs check the face
                // name string for a leading '@'.
                SalExtTextInputPosEvent aPosEvt;
                pFrame->CallCallback(SalEvent::ExtTextInputPos, &aPosEvt);
                if (aPosEvt.mbVertical)
                {
                    std::array<WCHAR, LF_FACESIZE> aTmpFaceName;
                    std::copy(aLogFont.lfFaceName, aLogFont.lfFaceName + LF_FACESIZE,
                              aTmpFaceName.begin());
                    aLogFont.lfFaceName[0] = L'@';
                    std::copy(aTmpFaceName.begin(), aTmpFaceName.end() - 1,
                              aLogFont.lfFaceName + 1);
                    aLogFont.lfFaceName[LF_FACESIZE - 1] = L'\0';
                }

                ImmSetCompositionFontW( hIMC, &aLogFont );
                ImmReleaseContext( pFrame->mhWnd, hIMC );
            }
        }
    }
    else
    {
        if ( pFrame->mbIME )
        {
            pFrame->mbIME = false;
            pFrame->mbHandleIME = false;
            ImmAssociateContext( pFrame->mhWnd, nullptr );
        }
    }
}

void WinSalFrame::SetInputContext( SalInputContext* pContext )
{
    // Must be called in the main thread!
    SendMessageW( mhWnd, SAL_MSG_SETINPUTCONTEXT, 0, reinterpret_cast<LPARAM>(pContext) );
}

static void ImplSalFrameEndExtTextInput( HWND hWnd, EndExtTextInputFlags nFlags )
{
    HIMC hIMC = ImmGetContext( hWnd );
    if ( hIMC )
    {
        DWORD nIndex;
        if ( nFlags & EndExtTextInputFlags::Complete )
            nIndex = CPS_COMPLETE;
        else
            nIndex = CPS_CANCEL;

        ImmNotifyIME( hIMC, NI_COMPOSITIONSTR, nIndex, 0 );
        ImmReleaseContext( hWnd, hIMC );
    }
}

void WinSalFrame::EndExtTextInput( EndExtTextInputFlags nFlags )
{
    // Must be called in the main thread!
    SendMessageW( mhWnd, SAL_MSG_ENDEXTTEXTINPUT, static_cast<WPARAM>(nFlags), 0 );
}

static void ImplGetKeyNameText(UINT lParam, OUStringBuffer& rBuf, const char* pReplace)
{
    static_assert( sizeof( WCHAR ) == sizeof( sal_Unicode ), "must be the same size" );

    static const int nMaxKeyLen = 350;
    WCHAR aKeyBuf[ nMaxKeyLen ];
    int nKeyLen = 0;
    if ( lParam )
    {
        OUString aLang = Application::GetSettings().GetUILanguageTag().getLanguage();
        OUString aRet = vcl_sal::getKeysReplacementName(aLang, lParam);
        if( aRet.isEmpty() )
        {
            nKeyLen = GetKeyNameTextW( lParam, aKeyBuf, nMaxKeyLen );
            SAL_WARN_IF( nKeyLen > nMaxKeyLen, "vcl", "Invalid key name length!" );
            if( nKeyLen > nMaxKeyLen )
                nKeyLen = 0;
            else if( nKeyLen > 0 )
            {
                // Capitalize just the first letter of key names
                CharLowerBuffW( aKeyBuf, nKeyLen );

                bool bUpper = true;
                for( WCHAR *pW=aKeyBuf, *pE=pW+nKeyLen; pW < pE; ++pW )
                {
                    if( bUpper )
                        CharUpperBuffW( pW, 1 );
                    bUpper = (*pW=='+') || (*pW=='-') || (*pW==' ') || (*pW=='.');
                }
            }
        }
        else
        {
            nKeyLen = aRet.getLength();
            wcscpy( aKeyBuf, o3tl::toW( aRet.getStr() ));
        }
    }

    if ( (nKeyLen > 0) || pReplace )
    {
        if (!rBuf.isEmpty())
            rBuf.append('+');

        if( nKeyLen > 0 )
        {
            rBuf.append(o3tl::toU(aKeyBuf), nKeyLen);
        }
        else // fall back to provided default name
        {
            rBuf.appendAscii(pReplace);
        }
    }
    else
        rBuf.setLength(0);
}

OUString WinSalFrame::GetKeyName( sal_uInt16 nKeyCode )
{
    OUStringBuffer aKeyBuf;
    UINT        nSysCode = 0;

    if ( nKeyCode & KEY_MOD1 )
    {
        nSysCode = MapVirtualKeyW( VK_CONTROL, 0 );
        nSysCode = (nSysCode << 16) | ((sal_uLong(1)) << 25);
        ImplGetKeyNameText( nSysCode, aKeyBuf, "Ctrl" );
    }

    if ( nKeyCode & KEY_MOD2 )
    {
        nSysCode = MapVirtualKeyW( VK_MENU, 0 );
        nSysCode = (nSysCode << 16) | ((sal_uLong(1)) << 25);
        ImplGetKeyNameText( nSysCode, aKeyBuf, "Alt" );
    }

    if ( nKeyCode & KEY_SHIFT )
    {
        nSysCode = MapVirtualKeyW( VK_SHIFT, 0 );
        nSysCode = (nSysCode << 16) | ((sal_uLong(1)) << 25);
        ImplGetKeyNameText( nSysCode, aKeyBuf, "Shift" );
    }

    sal_uInt16      nCode = nKeyCode & 0x0FFF;
    sal_uLong       nSysCode2 = 0;
    const char*   pReplace = nullptr;
    sal_Unicode cSVCode = 0;
    char    aFBuf[4];
    nSysCode = 0;

    if ( (nCode >= KEY_0) && (nCode <= KEY_9) )
        cSVCode = '0' + (nCode - KEY_0);
    else if ( (nCode >= KEY_A) && (nCode <= KEY_Z) )
        cSVCode = 'A' + (nCode - KEY_A);
    else if ( (nCode >= KEY_F1) && (nCode <= KEY_F26) )
    {
        nSysCode = VK_F1 + (nCode - KEY_F1);
        aFBuf[0] = 'F';
        if (nCode <= KEY_F9)
        {
            aFBuf[1] = sal::static_int_cast<char>('1' + (nCode - KEY_F1));
            aFBuf[2] = 0;
        }
        else if (nCode <= KEY_F19)
        {
            aFBuf[1] = '1';
            aFBuf[2] = sal::static_int_cast<char>('0' + (nCode - KEY_F10));
            aFBuf[3] = 0;
        }
        else
        {
            aFBuf[1] = '2';
            aFBuf[2] = sal::static_int_cast<char>('0' + (nCode - KEY_F20));
            aFBuf[3] = 0;
        }
        pReplace = aFBuf;
    }
    else
    {
        switch ( nCode )
        {
            case KEY_DOWN:
                nSysCode = VK_DOWN;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Down";
                break;
            case KEY_UP:
                nSysCode = VK_UP;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Up";
                break;
            case KEY_LEFT:
                nSysCode = VK_LEFT;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Left";
                break;
            case KEY_RIGHT:
                nSysCode = VK_RIGHT;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Right";
                break;
            case KEY_HOME:
                nSysCode = VK_HOME;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Home";
                break;
            case KEY_END:
                nSysCode = VK_END;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "End";
                break;
            case KEY_PAGEUP:
                nSysCode = VK_PRIOR;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Page Up";
                break;
            case KEY_PAGEDOWN:
                nSysCode = VK_NEXT;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Page Down";
                break;
            case KEY_RETURN:
                nSysCode = VK_RETURN;
                pReplace = "Enter";
                break;
            case KEY_ESCAPE:
                nSysCode = VK_ESCAPE;
                pReplace = "Escape";
                break;
            case KEY_TAB:
                nSysCode = VK_TAB;
                pReplace = "Tab";
                break;
            case KEY_BACKSPACE:
                nSysCode = VK_BACK;
                pReplace = "Backspace";
                break;
            case KEY_SPACE:
                nSysCode = VK_SPACE;
                pReplace = "Space";
                break;
            case KEY_INSERT:
                nSysCode = VK_INSERT;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Insert";
                break;
            case KEY_DELETE:
                nSysCode = VK_DELETE;
                nSysCode2 = ((sal_uLong(1)) << 24);
                pReplace = "Delete";
                break;

            case KEY_ADD:
                cSVCode  = '+';
                break;
            case KEY_SUBTRACT:
                cSVCode  = '-';
                break;
            case KEY_MULTIPLY:
                cSVCode  = '*';
                break;
            case KEY_DIVIDE:
                cSVCode  = '/';
                break;
            case KEY_POINT:
                cSVCode  = '.';
                break;
            case KEY_COMMA:
                cSVCode  = ',';
                break;
            case KEY_LESS:
                cSVCode  = '<';
                break;
            case KEY_GREATER:
                cSVCode  = '>';
                break;
            case KEY_EQUAL:
                cSVCode  = '=';
                break;
            case KEY_NUMBERSIGN:
                cSVCode = '#';
                break;
            case KEY_XF86FORWARD:
                cSVCode = VK_BROWSER_FORWARD;
                break;
            case KEY_XF86BACK:
                cSVCode = VK_BROWSER_BACK;
                break;
            case KEY_COLON:
                cSVCode = ':';
                break;
            case KEY_SEMICOLON:
                cSVCode = ';';
                break;
            case KEY_QUOTERIGHT:
                cSVCode = '\'';
                break;
            case KEY_BRACKETLEFT:
                cSVCode = '[';
                break;
            case KEY_BRACKETRIGHT:
                cSVCode = ']';
                break;
            case KEY_QUOTELEFT:
                cSVCode = '`';
                break;
            case KEY_RIGHTCURLYBRACKET:
                cSVCode = '}';
                break;
        }
    }

    if ( nSysCode )
    {
        nSysCode = MapVirtualKeyW( nSysCode, 0 );
        if ( nSysCode )
            nSysCode = (nSysCode << 16) | nSysCode2;
        ImplGetKeyNameText( nSysCode, aKeyBuf, pReplace );
    }
    else
    {
        if ( cSVCode )
        {
            if (!aKeyBuf.isEmpty())
                aKeyBuf.append('+');
            aKeyBuf.append(cSVCode);
        }
    }

    return aKeyBuf.makeStringAndClear();
}

static Color ImplWinColorToSal( COLORREF nColor )
{
    return Color( GetRValue( nColor ), GetGValue( nColor ), GetBValue( nColor ) );
}

static void ImplSalUpdateStyleFontW( HDC hDC, const LOGFONTW& rLogFont, vcl::Font& rFont )
{
    ImplSalLogFontToFontW( hDC, rLogFont, rFont );

    // On Windows 9x, Windows NT we get sometimes very small sizes
    // (for example for the small Caption height).
    // So if it is MS Sans Serif, a none scalable font we use
    // 8 Point as the minimum control height, in all other cases
    // 6 Point is the smallest one
    if ( rFont.GetFontHeight() < 8 )
    {
        if ( rtl_ustr_compareIgnoreAsciiCase( o3tl::toU(rLogFont.lfFaceName), o3tl::toU(L"MS Sans Serif") ) == 0 )
            rFont.SetFontHeight( 8 );
        else if ( rFont.GetFontHeight() < 6 )
            rFont.SetFontHeight( 6 );
    }
}

static tools::Long ImplW2I( const wchar_t* pStr )
{
    tools::Long n = 0;
    int     nSign = 1;

    if ( *pStr == '-' )
    {
        nSign = -1;
        pStr++;
    }

    while( (*pStr >= 48) && (*pStr <= 57) )
    {
        n *= 10;
        n += ((*pStr) - 48);
        pStr++;
    }

    n *= nSign;

    return n;
}

static void lcl_LoadColorsFromTheme(StyleSettings& rStyleSet)
{
    const ThemeColors& rThemeColors = ThemeColors::GetThemeColors();

    rStyleSet.SetWindowColor(rThemeColors.GetWindowColor());
    rStyleSet.BatchSetBackgrounds(rThemeColors.GetWindowColor());

    rStyleSet.SetActiveTabColor(rThemeColors.GetWindowColor());
    rStyleSet.SetInactiveTabColor(rThemeColors.GetBaseColor());
    rStyleSet.SetDisableColor(rThemeColors.GetDisabledColor()); // tab outline

    // Highlight related colors
    rStyleSet.SetAccentColor(rThemeColors.GetAccentColor());
    rStyleSet.SetHighlightColor(rThemeColors.GetAccentColor());

    rStyleSet.SetListBoxWindowHighlightColor(rThemeColors.GetAccentColor());
    rStyleSet.SetListBoxWindowTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetListBoxWindowBackgroundColor(rThemeColors.GetBaseColor());
    rStyleSet.SetListBoxWindowHighlightTextColor(rThemeColors.GetMenuHighlightTextColor());
    rStyleSet.SetWindowTextColor(rThemeColors.GetWindowTextColor()); // Treeview Lists

    rStyleSet.SetRadioCheckTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetLabelTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetFieldTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetTabTextColor(rThemeColors.GetWindowTextColor());
    rStyleSet.SetFieldColor(rThemeColors.GetBaseColor());
    rStyleSet.SetMenuBarTextColor(rThemeColors.GetMenuBarTextColor());
    rStyleSet.SetMenuTextColor(rThemeColors.GetMenuTextColor());

    rStyleSet.SetDefaultActionButtonTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetActionButtonTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetShadowColor(rThemeColors.GetShadeColor());

    rStyleSet.SetDefaultButtonTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetDefaultButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetDefaultButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());

    rStyleSet.SetFlatButtonTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetFlatButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetFlatButtonRolloverTextColor(rThemeColors.GetButtonTextColor());

    rStyleSet.SetButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetDefaultActionButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetDefaultActionButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetActionButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetActionButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetFieldRolloverTextColor(rThemeColors.GetButtonTextColor());

    rStyleSet.SetButtonRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetButtonPressedRolloverTextColor(rThemeColors.GetButtonTextColor());
    rStyleSet.SetHelpColor(rThemeColors.GetWindowColor());
    rStyleSet.SetHelpTextColor(rThemeColors.GetWindowTextColor());

    // rStyleSet.SetHighlightTextColor(aThemeColors.GetActiveTextColor());
    // rStyleSet.SetActiveColor(aThemeColors.GetActiveColor());
    rStyleSet.SetActiveTextColor(rThemeColors.GetWindowTextColor());

    // rStyleSet.SetLinkColor(aThemeColors.GetAccentColor());
    // Color aVisitedLinkColor = aThemeColors.GetActiveColor();
    // aVisitedLinkColor.Merge(aThemeColors.GetWindowColor(), 100);
    // rStyleSet.SetVisitedLinkColor(aVisitedLinkColor);
    // rStyleSet.SetToolTextColor(Color(255, 0, 0));

    rStyleSet.SetTabRolloverTextColor(rThemeColors.GetMenuBarHighlightTextColor());
}

void WinSalFrame::UpdateSettings( AllSettings& rSettings )
{
    MouseSettings aMouseSettings = rSettings.GetMouseSettings();
    aMouseSettings.SetDoubleClickTime( GetDoubleClickTime() );
    aMouseSettings.SetDoubleClickWidth( GetSystemMetrics( SM_CXDOUBLECLK ) );
    aMouseSettings.SetDoubleClickHeight( GetSystemMetrics( SM_CYDOUBLECLK ) );
    tools::Long nDragWidth = GetSystemMetrics( SM_CXDRAG );
    tools::Long nDragHeight = GetSystemMetrics( SM_CYDRAG );
    if ( nDragWidth )
        aMouseSettings.SetStartDragWidth( nDragWidth );
    if ( nDragHeight )
        aMouseSettings.SetStartDragHeight( nDragHeight );
    {
        wchar_t aValueBuf[10];
        DWORD   nValueSize = sizeof( aValueBuf );
        if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"MenuShowDelay",
                         RRF_RT_REG_SZ, nullptr, aValueBuf, &nValueSize)
            == ERROR_SUCCESS)
        {
            aMouseSettings.SetMenuDelay( static_cast<sal_uLong>(ImplW2I( aValueBuf )) );
        }
    }

    StyleSettings aStyleSettings = rSettings.GetStyleSettings();

    // High contrast
    HIGHCONTRAST hc;
    hc.cbSize = sizeof( HIGHCONTRAST );
    if( SystemParametersInfoW( SPI_GETHIGHCONTRAST, hc.cbSize, &hc, 0 )
            && (hc.dwFlags & HCF_HIGHCONTRASTON) )
        aStyleSettings.SetHighContrastMode( true );
    else
        aStyleSettings.SetHighContrastMode( false );

    aStyleSettings.SetScrollBarSize( GetSystemMetrics( SM_CXVSCROLL ) );
    aStyleSettings.SetSpinSize( GetSystemMetrics( SM_CXVSCROLL ) );
    UINT blinkTime = GetCaretBlinkTime();
    aStyleSettings.SetCursorBlinkTime(
        blinkTime == 0 || blinkTime == INFINITE // 0 indicates error
        ? STYLE_CURSOR_NOBLINKTIME : blinkTime );
    aStyleSettings.SetFloatTitleHeight( GetSystemMetrics( SM_CYSMCAPTION ) );
    aStyleSettings.SetTitleHeight( GetSystemMetrics( SM_CYCAPTION ) );
    aStyleSettings.SetActiveBorderColor( ImplWinColorToSal( GetSysColor( COLOR_ACTIVEBORDER ) ) );
    aStyleSettings.SetDeactiveBorderColor( ImplWinColorToSal( GetSysColor( COLOR_INACTIVEBORDER ) ) );
    aStyleSettings.SetDeactiveColor( ImplWinColorToSal( GetSysColor( COLOR_GRADIENTINACTIVECAPTION ) ) );

    Color aControlTextColor;
    Color aMenuBarTextColor;
    Color aMenuBarRolloverTextColor;
    Color aHighlightTextColor = ImplWinColorToSal(GetSysColor(COLOR_HIGHLIGHTTEXT));

    BOOL bFlatMenus = FALSE;
    SystemParametersInfoW( SPI_GETFLATMENU, 0, &bFlatMenus, 0);
    if( bFlatMenus )
    {
        aStyleSettings.SetUseFlatMenus( true );
        // flat borders for our controls etc. as well in this mode (ie, no 3d borders)
        // this is not active in the classic style appearance
        aStyleSettings.SetUseFlatBorders( true );
    }
    else
    {
        aStyleSettings.SetUseFlatMenus( false );
        aStyleSettings.SetUseFlatBorders( false );
    }

    if( bFlatMenus )
    {
        aStyleSettings.SetMenuHighlightColor( ImplWinColorToSal( GetSysColor( COLOR_MENUHILIGHT ) ) );
        aStyleSettings.SetMenuBarRolloverColor( ImplWinColorToSal( GetSysColor( COLOR_MENUHILIGHT ) ) );
        aStyleSettings.SetMenuBorderColor( ImplWinColorToSal( GetSysColor( COLOR_3DSHADOW ) ) );
    }
    else
    {
        aStyleSettings.SetMenuHighlightColor( ImplWinColorToSal( GetSysColor( COLOR_HIGHLIGHT ) ) );
        aStyleSettings.SetMenuBarRolloverColor( ImplWinColorToSal( GetSysColor( COLOR_HIGHLIGHT ) ) );
        aStyleSettings.SetMenuBorderColor( ImplWinColorToSal( GetSysColor( COLOR_3DLIGHT ) ) );
    }

    const bool bUseDarkMode(UseDarkMode());
    if (!ThemeColors::VclPluginCanUseThemeColors())
    {
        OUString sThemeName(!bUseDarkMode ? u"colibre" : u"colibre_dark");
        aStyleSettings.SetPreferredIconTheme(sThemeName, bUseDarkMode);
    }
    else
    {
        aStyleSettings.SetPreferredIconTheme(vcl::IconThemeSelector::GetIconThemeForDesktopEnvironment(
            Application::GetDesktopEnvironment(), ThemeColors::GetThemeColors().GetWindowColor().IsDark()));
    }

    if (bUseDarkMode)
    {
        SetWindowTheme(mhWnd, L"Explorer", nullptr);

        HTHEME hTheme = OpenThemeData(nullptr, L"ItemsView");
        COLORREF color;
        GetThemeColor(hTheme, 0, 0, TMT_FILLCOLOR, &color);
        aStyleSettings.SetFaceColor( ImplWinColorToSal( color ) );
        aStyleSettings.SetWindowColor( ImplWinColorToSal( color ) );

        // tdf#156040 in the absence of a better idea, do like
        // StyleSettings::Set3DColors does
        Color aLightColor(ImplWinColorToSal(color));
        aLightColor.DecreaseLuminance(64);
        aStyleSettings.SetLightColor(aLightColor);

        GetThemeColor(hTheme, 0, 0, TMT_TEXTCOLOR, &color);
        aStyleSettings.SetWindowTextColor( ImplWinColorToSal( color ) );
        aStyleSettings.SetToolTextColor( ImplWinColorToSal( color ) );
        GetThemeColor(hTheme, 0, 0, TMT_SHADOWCOLOR, &color);
        aStyleSettings.SetShadowColor( ImplWinColorToSal( color ) );
        GetThemeColor(hTheme, 0, 0, TMT_DKSHADOW3D, &color);
        aStyleSettings.SetDarkShadowColor( ImplWinColorToSal( color ) );
        CloseThemeData(hTheme);

        hTheme = OpenThemeData(mhWnd, L"Button");
        GetThemeColor(hTheme, BP_PUSHBUTTON, PBS_NORMAL, TMT_TEXTCOLOR, &color);
        aControlTextColor = ImplWinColorToSal(color);
        GetThemeColor(hTheme, BP_CHECKBOX, CBS_CHECKEDNORMAL, TMT_TEXTCOLOR, &color);
        aStyleSettings.SetRadioCheckTextColor( ImplWinColorToSal( color ) );
        CloseThemeData(hTheme);

        SetWindowTheme(mhWnd, nullptr, nullptr);

        hTheme = OpenThemeData(mhWnd, L"Menu");
        GetThemeColor(hTheme, MENU_POPUPITEM, MBI_NORMAL, TMT_TEXTCOLOR, &color);
        aStyleSettings.SetMenuTextColor( ImplWinColorToSal( color ) );
        aMenuBarTextColor = ImplWinColorToSal( color );
        aMenuBarRolloverTextColor = ImplWinColorToSal( color );
        CloseThemeData(hTheme);

        aStyleSettings.SetActiveTabColor( aStyleSettings.GetWindowColor() );
        hTheme = OpenThemeData(mhWnd, L"Toolbar");
        GetThemeColor(hTheme, 0, 0, TMT_FILLCOLOR, &color);
        aStyleSettings.SetInactiveTabColor( ImplWinColorToSal( color ) );
        // see ImplDrawNativeControl for dark mode
        aStyleSettings.SetMenuBarColor( aStyleSettings.GetWindowColor() );
        CloseThemeData(hTheme);

        hTheme = OpenThemeData(mhWnd, L"Textstyle");
        if (hTheme)
        {
            GetThemeColor(hTheme, TEXT_HYPERLINKTEXT, TS_HYPERLINK_NORMAL, TMT_TEXTCOLOR, &color);
            aStyleSettings.SetLinkColor(ImplWinColorToSal(color));
            CloseThemeData(hTheme);
        }

        // tdf#148448 pick a warning color more likely to be readable as a
        // background in a dark theme
        aStyleSettings.SetWarningColor(Color(0xf5, 0x79, 0x00));
    }
    else
    {
        aStyleSettings.SetFaceColor( ImplWinColorToSal( GetSysColor( COLOR_3DFACE ) ) );
        aStyleSettings.SetWindowColor( ImplWinColorToSal( GetSysColor( COLOR_WINDOW ) ) );
        aStyleSettings.SetWindowTextColor( ImplWinColorToSal( GetSysColor( COLOR_WINDOWTEXT ) ) );
        aStyleSettings.SetToolTextColor( ImplWinColorToSal( GetSysColor( COLOR_WINDOWTEXT ) ) );
        aStyleSettings.SetLightColor( ImplWinColorToSal( GetSysColor( COLOR_3DHILIGHT ) ) );
        aStyleSettings.SetShadowColor( ImplWinColorToSal( GetSysColor( COLOR_3DSHADOW ) ) );
        aStyleSettings.SetDarkShadowColor( ImplWinColorToSal( GetSysColor( COLOR_3DDKSHADOW ) ) );
        aControlTextColor = ImplWinColorToSal(GetSysColor(COLOR_BTNTEXT));
        aStyleSettings.SetRadioCheckTextColor( ImplWinColorToSal( GetSysColor( COLOR_WINDOWTEXT ) ) );
        aStyleSettings.SetMenuTextColor( ImplWinColorToSal( GetSysColor( COLOR_MENUTEXT ) ) );
        aMenuBarTextColor = ImplWinColorToSal( GetSysColor( COLOR_MENUTEXT ) );
        aMenuBarRolloverTextColor = aHighlightTextColor;
        if( bFlatMenus )
            aStyleSettings.SetMenuBarColor( ImplWinColorToSal( GetSysColor( COLOR_MENUBAR ) ) );
        else
            aStyleSettings.SetMenuBarColor( ImplWinColorToSal( GetSysColor( COLOR_MENU ) ) );
        aStyleSettings.SetActiveTabColor( aStyleSettings.GetWindowColor() );
        aStyleSettings.SetInactiveTabColor( aStyleSettings.GetFaceColor() );
    }

    aStyleSettings.SetMenuBarTextColor( aMenuBarTextColor );
    aStyleSettings.SetMenuBarRolloverTextColor( aMenuBarRolloverTextColor );

    aStyleSettings.SetLightBorderColor( ImplWinColorToSal( GetSysColor( COLOR_3DLIGHT ) ) );
    aStyleSettings.SetHelpColor( ImplWinColorToSal( GetSysColor( COLOR_INFOBK ) ) );
    aStyleSettings.SetHelpTextColor( ImplWinColorToSal( GetSysColor( COLOR_INFOTEXT ) ) );

    aStyleSettings.SetWorkspaceColor(aStyleSettings.GetFaceColor());
    aStyleSettings.SetDialogColor(aStyleSettings.GetFaceColor());
    aStyleSettings.SetDialogTextColor(aControlTextColor);

    Color aHighlightButtonTextColor = aStyleSettings.GetHighContrastMode() ?
        aHighlightTextColor : aControlTextColor;

    if (aStyleSettings.GetHighContrastMode())
    {
        Color aLinkColor(ImplWinColorToSal(GetSysColor(COLOR_HOTLIGHT)));
        aStyleSettings.SetLinkColor(aLinkColor);
        aStyleSettings.SetVisitedLinkColor(aLinkColor);
    }

    aStyleSettings.SetDefaultButtonTextColor(aHighlightButtonTextColor);
    aStyleSettings.SetButtonTextColor(aControlTextColor);
    aStyleSettings.SetDefaultActionButtonTextColor(aHighlightButtonTextColor);
    aStyleSettings.SetActionButtonTextColor(aControlTextColor);
    aStyleSettings.SetFlatButtonTextColor(aControlTextColor);
    aStyleSettings.SetDefaultButtonRolloverTextColor(aHighlightButtonTextColor);
    aStyleSettings.SetButtonRolloverTextColor(aHighlightButtonTextColor);
    aStyleSettings.SetDefaultActionButtonRolloverTextColor(aHighlightButtonTextColor);
    aStyleSettings.SetActionButtonRolloverTextColor(aHighlightButtonTextColor);
    aStyleSettings.SetFlatButtonRolloverTextColor(aHighlightButtonTextColor);
    aStyleSettings.SetDefaultButtonPressedRolloverTextColor(aControlTextColor);
    aStyleSettings.SetButtonPressedRolloverTextColor(aControlTextColor);
    aStyleSettings.SetDefaultActionButtonPressedRolloverTextColor(aControlTextColor);
    aStyleSettings.SetActionButtonPressedRolloverTextColor(aControlTextColor);
    aStyleSettings.SetFlatButtonPressedRolloverTextColor(aControlTextColor);

    aStyleSettings.SetTabTextColor(aControlTextColor);
    aStyleSettings.SetTabRolloverTextColor(aControlTextColor);
    aStyleSettings.SetTabHighlightTextColor(aControlTextColor);

    aStyleSettings.SetGroupTextColor( aStyleSettings.GetRadioCheckTextColor() );
    aStyleSettings.SetLabelTextColor( aStyleSettings.GetRadioCheckTextColor() );
    aStyleSettings.SetFieldColor( aStyleSettings.GetWindowColor() );
    aStyleSettings.SetListBoxWindowBackgroundColor( aStyleSettings.GetWindowColor() );
    aStyleSettings.SetFieldTextColor( aStyleSettings.GetWindowTextColor() );
    aStyleSettings.SetFieldRolloverTextColor( aStyleSettings.GetFieldTextColor() );
    aStyleSettings.SetListBoxWindowTextColor( aStyleSettings.GetFieldTextColor() );

    aStyleSettings.SetAccentColor( ImplWinColorToSal( GetSysColor( COLOR_HIGHLIGHT ) ) );
    // https://devblogs.microsoft.com/oldnewthing/20170405-00/?p=95905

    aStyleSettings.SetHighlightColor( ImplWinColorToSal( GetSysColor( COLOR_HIGHLIGHT ) ) );
    aStyleSettings.SetHighlightTextColor(aHighlightTextColor);
    aStyleSettings.SetListBoxWindowHighlightColor( aStyleSettings.GetHighlightColor() );
    aStyleSettings.SetListBoxWindowHighlightTextColor( aStyleSettings.GetHighlightTextColor() );
    aStyleSettings.SetMenuHighlightTextColor( aStyleSettings.GetHighlightTextColor() );

    ImplSVData* pSVData = ImplGetSVData();
    pSVData->maNWFData.mnMenuFormatBorderX = 0;
    pSVData->maNWFData.mnMenuFormatBorderY = 0;
    pSVData->maNWFData.maMenuBarHighlightTextColor = COL_TRANSPARENT;
    GetSalData()->mbThemeMenuSupport = false;
    aStyleSettings.SetMenuColor( ImplWinColorToSal( GetSysColor( COLOR_MENU ) ) );
    aStyleSettings.SetMenuBarHighlightTextColor(aStyleSettings.GetMenuHighlightTextColor());
    aStyleSettings.SetActiveColor( ImplWinColorToSal( GetSysColor( COLOR_ACTIVECAPTION ) ) );
    aStyleSettings.SetActiveTextColor( ImplWinColorToSal( GetSysColor( COLOR_CAPTIONTEXT ) ) );
    aStyleSettings.SetDeactiveColor( ImplWinColorToSal( GetSysColor( COLOR_INACTIVECAPTION ) ) );
    aStyleSettings.SetDeactiveTextColor( ImplWinColorToSal( GetSysColor( COLOR_INACTIVECAPTIONTEXT ) ) );

    aStyleSettings.SetCheckedColorSpecialCase( );

    // caret width
    DWORD nCaretWidth = 2;
    if( SystemParametersInfoW( SPI_GETCARETWIDTH, 0, &nCaretWidth, 0 ) )
        aStyleSettings.SetCursorSize( nCaretWidth );

    // Query Fonts
    vcl::Font    aMenuFont = aStyleSettings.GetMenuFont();
    vcl::Font    aTitleFont = aStyleSettings.GetTitleFont();
    vcl::Font    aFloatTitleFont = aStyleSettings.GetFloatTitleFont();
    vcl::Font    aHelpFont = aStyleSettings.GetHelpFont();
    vcl::Font    aAppFont = aStyleSettings.GetAppFont();
    vcl::Font    aIconFont = aStyleSettings.GetIconFont();
    HDC     hDC = GetDC( nullptr );
    NONCLIENTMETRICSW aNonClientMetrics;
    aNonClientMetrics.cbSize = sizeof( aNonClientMetrics );
    if ( SystemParametersInfoW( SPI_GETNONCLIENTMETRICS, sizeof( aNonClientMetrics ), &aNonClientMetrics, 0 ) )
    {
        ImplSalUpdateStyleFontW( hDC, aNonClientMetrics.lfMenuFont, aMenuFont );
        ImplSalUpdateStyleFontW( hDC, aNonClientMetrics.lfCaptionFont, aTitleFont );
        ImplSalUpdateStyleFontW( hDC, aNonClientMetrics.lfSmCaptionFont, aFloatTitleFont );
        ImplSalUpdateStyleFontW( hDC, aNonClientMetrics.lfStatusFont, aHelpFont );
        ImplSalUpdateStyleFontW( hDC, aNonClientMetrics.lfMessageFont, aAppFont );

        LOGFONTW aLogFont;
        if ( SystemParametersInfoW( SPI_GETICONTITLELOGFONT, 0, &aLogFont, 0 ) )
            ImplSalUpdateStyleFontW( hDC, aLogFont, aIconFont );
    }

    ReleaseDC( nullptr, hDC );

    aStyleSettings.SetToolbarIconSize(ToolbarIconSize::Large);

    aStyleSettings.BatchSetFonts( aAppFont, aAppFont );

    aStyleSettings.SetMenuFont( aMenuFont );
    aStyleSettings.SetTitleFont( aTitleFont );
    aStyleSettings.SetFloatTitleFont( aFloatTitleFont );
    aStyleSettings.SetHelpFont( aHelpFont );
    aStyleSettings.SetIconFont( aIconFont );

    if ( aAppFont.GetWeightMaybeAskConfig() > WEIGHT_NORMAL )
        aAppFont.SetWeight( WEIGHT_NORMAL );
    aStyleSettings.SetToolFont( aAppFont );
    aStyleSettings.SetTabFont( aAppFont );

    BOOL bDragFull;
    if ( SystemParametersInfoW( SPI_GETDRAGFULLWINDOWS, 0, &bDragFull, 0 ) )
    {
        DragFullOptions nDragFullOptions = aStyleSettings.GetDragFullOptions();
        if ( bDragFull )
            nDragFullOptions |= DragFullOptions::WindowMove | DragFullOptions::WindowSize | DragFullOptions::Docking | DragFullOptions::Split;
        else
            nDragFullOptions &= ~DragFullOptions(DragFullOptions::WindowMove | DragFullOptions::WindowSize | DragFullOptions::Docking | DragFullOptions::Split);
        aStyleSettings.SetDragFullOptions( nDragFullOptions );
    }

    {
        wchar_t aValueBuf[10];
        DWORD   nValueSize = sizeof( aValueBuf );
        if (RegGetValueW(HKEY_CURRENT_USER,
                         L"Control Panel\\International\\Calendars\\TwoDigitYearMax", L"1",
                         RRF_RT_REG_SZ, nullptr, aValueBuf, &nValueSize)
            == ERROR_SUCCESS)
        {
            DWORD nValue = static_cast<sal_uLong>(ImplW2I(aValueBuf));
            if ((nValue > 1000) && (nValue < 10000))
            {
                std::shared_ptr<comphelper::ConfigurationChanges> batch(comphelper::ConfigurationChanges::create());
                officecfg::Office::Common::DateFormat::TwoDigitYear::set(static_cast<sal_Int32>(nValue-99), batch);
                batch->commit();
            }
        }
    }

    // otherwise, menu shows up as white in dark mode
    aStyleSettings.SetMenuColor(aStyleSettings.GetWindowColor());
    if (ThemeColors::VclPluginCanUseThemeColors())
        lcl_LoadColorsFromTheme(aStyleSettings);
    aStyleSettings.SetSystemColorsLoaded(true);

    rSettings.SetMouseSettings( aMouseSettings );
    rSettings.SetStyleSettings( aStyleSettings );

    // now apply the values from theming, if available
    WinSalGraphics::updateSettingsNative( rSettings );
}

const SystemEnvData& WinSalFrame::GetSystemData() const
{
    return maSysData;
}

void WinSalFrame::Beep()
{
    // a simple beep
    MessageBeep( 0 );
}

void WinSalFrame::FlashWindow() const
{
    if (GetForegroundWindow() != mhWnd)
    {
        ::FlashWindow(mhWnd, TRUE);
    }
}

SalFrame::SalPointerState WinSalFrame::GetPointerState()
{
    SalPointerState aState;
    aState.mnState = 0;

    if ( GetKeyState( VK_LBUTTON ) & 0x8000 )
        aState.mnState |= MOUSE_LEFT;
    if ( GetKeyState( VK_MBUTTON ) & 0x8000 )
        aState.mnState |= MOUSE_MIDDLE;
    if ( GetKeyState( VK_RBUTTON ) & 0x8000 )
        aState.mnState |= MOUSE_RIGHT;
    if ( GetKeyState( VK_SHIFT ) & 0x8000 )
        aState.mnState |= KEY_SHIFT;
    if ( GetKeyState( VK_CONTROL ) & 0x8000 )
        aState.mnState |= KEY_MOD1;
    if ( GetKeyState( VK_MENU ) & 0x8000 )
        aState.mnState |= KEY_MOD2;

    POINT pt;
    GetCursorPos( &pt );

    aState.maPos = Point(pt.x - maGeometry.x(), pt.y - maGeometry.y());
    return aState;
}

KeyIndicatorState WinSalFrame::GetIndicatorState()
{
    KeyIndicatorState aState = KeyIndicatorState::NONE;
    if (::GetKeyState(VK_CAPITAL))
        aState |= KeyIndicatorState::CAPSLOCK;

    if (::GetKeyState(VK_NUMLOCK))
        aState |= KeyIndicatorState::NUMLOCK;

    if (::GetKeyState(VK_SCROLL))
        aState |= KeyIndicatorState::SCROLLLOCK;

    return aState;
}

void WinSalFrame::SimulateKeyPress( sal_uInt16 nKeyCode )
{
    BYTE nVKey = 0;
    switch (nKeyCode)
    {
        case KEY_CAPSLOCK:
            nVKey = VK_CAPITAL;
        break;
    }

    if (nVKey > 0 && nVKey < 255)
    {
        ::keybd_event(nVKey, 0x45, KEYEVENTF_EXTENDEDKEY, 0);
        ::keybd_event(nVKey, 0x45, KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP, 0);
    }
}

void WinSalFrame::ResetClipRegion()
{
    SetWindowRgn( mhWnd, nullptr, TRUE );
}

void WinSalFrame::BeginSetClipRegion( sal_uInt32 nRects )
{
    if( mpClipRgnData )
        delete [] reinterpret_cast<BYTE*>(mpClipRgnData);
    sal_uLong nRectBufSize = sizeof(RECT)*nRects;
    mpClipRgnData = reinterpret_cast<RGNDATA*>(new BYTE[sizeof(RGNDATA)-1+nRectBufSize]);
    mpClipRgnData->rdh.dwSize     = sizeof( RGNDATAHEADER );
    mpClipRgnData->rdh.iType      = RDH_RECTANGLES;
    mpClipRgnData->rdh.nCount     = nRects;
    mpClipRgnData->rdh.nRgnSize  = nRectBufSize;
    SetRectEmpty( &(mpClipRgnData->rdh.rcBound) );
    mpNextClipRect        = reinterpret_cast<RECT*>(&(mpClipRgnData->Buffer));
    mbFirstClipRect       = true;
}

void WinSalFrame::UnionClipRegion( tools::Long nX, tools::Long nY, tools::Long nWidth, tools::Long nHeight )
{
    if( ! mpClipRgnData )
        return;

    RECT*       pRect = mpNextClipRect;
    RECT*       pBoundRect = &(mpClipRgnData->rdh.rcBound);
    tools::Long        nRight = nX + nWidth;
    tools::Long        nBottom = nY + nHeight;

    if ( mbFirstClipRect )
    {
        pBoundRect->left    = nX;
        pBoundRect->top     = nY;
        pBoundRect->right   = nRight;
        pBoundRect->bottom  = nBottom;
        mbFirstClipRect = false;
    }
    else
    {
        if ( nX < pBoundRect->left )
            pBoundRect->left = static_cast<int>(nX);

        if ( nY < pBoundRect->top )
            pBoundRect->top = static_cast<int>(nY);

        if ( nRight > pBoundRect->right )
            pBoundRect->right = static_cast<int>(nRight);

        if ( nBottom > pBoundRect->bottom )
            pBoundRect->bottom = static_cast<int>(nBottom);
    }

    pRect->left     = static_cast<int>(nX);
    pRect->top      = static_cast<int>(nY);
    pRect->right    = static_cast<int>(nRight);
    pRect->bottom   = static_cast<int>(nBottom);
    if( (mpNextClipRect  - reinterpret_cast<RECT*>(&mpClipRgnData->Buffer)) < static_cast<int>(mpClipRgnData->rdh.nCount) )
        mpNextClipRect++;
}

void WinSalFrame::EndSetClipRegion()
{
    if( ! mpClipRgnData )
        return;

    HRGN hRegion;

    // create region from accumulated rectangles
    if ( mpClipRgnData->rdh.nCount == 1 )
    {
        RECT* pRect = &(mpClipRgnData->rdh.rcBound);
        hRegion = CreateRectRgn( pRect->left, pRect->top,
                                 pRect->right, pRect->bottom );
    }
    else
    {
        sal_uLong nSize = mpClipRgnData->rdh.nRgnSize+sizeof(RGNDATAHEADER);
        hRegion = ExtCreateRegion( nullptr, nSize, mpClipRgnData );
    }
    delete [] reinterpret_cast<BYTE*>(mpClipRgnData);
    mpClipRgnData = nullptr;

    SAL_WARN_IF( !hRegion, "vcl", "WinSalFrame::EndSetClipRegion() - Can't create ClipRegion" );
    if( hRegion )
    {
        RECT aWindowRect;
        GetWindowRect( mhWnd, &aWindowRect );
        POINT aPt;
        aPt.x=0;
        aPt.y=0;
        ClientToScreen( mhWnd, &aPt );
        OffsetRgn( hRegion, aPt.x - aWindowRect.left, aPt.y - aWindowRect.top );

        if( SetWindowRgn( mhWnd, hRegion, TRUE ) == 0 )
            DeleteObject( hRegion );
    }
}

void WinSalFrame::UpdateDarkMode()
{
    ::UpdateDarkMode(mhWnd);
}

bool WinSalFrame::GetUseDarkMode() const
{
    return UseDarkMode();
}

bool WinSalFrame::GetUseReducedAnimation() const
{
    BOOL bEnableAnimation = FALSE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &bEnableAnimation, 0);
    return !bEnableAnimation;
}

void WinSalFrame::SetTaskBarProgress(int nCurrentProgress)
{
    if (!m_pTaskbarList3)
    {
        HRESULT hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pTaskbarList3));
        if (!SUCCEEDED(hr) || !m_pTaskbarList3)
            return;
    }

    m_pTaskbarList3->SetProgressValue(mhWnd, nCurrentProgress, 100);
}

void WinSalFrame::SetTaskBarState(VclTaskBarStates eTaskBarState)
{
    if (!m_pTaskbarList3)
    {
        HRESULT hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pTaskbarList3));
        if (!SUCCEEDED(hr) || !m_pTaskbarList3)
            return;
    }

    TBPFLAG nFlag;
    switch (eTaskBarState)
    {
        case VclTaskBarStates::Progress:
            nFlag = TBPF_NORMAL;
            break;
        case VclTaskBarStates::ProgressUnknown:
            nFlag = TBPF_INDETERMINATE;
            break;
        case VclTaskBarStates::Paused:
            nFlag = TBPF_PAUSED;
            SetTaskBarProgress(100);
            break;
        case VclTaskBarStates::Error:
            nFlag = TBPF_ERROR;
            SetTaskBarProgress(100);
            break;
        case VclTaskBarStates::Normal:
        default:
            nFlag = TBPF_NOPROGRESS;
            break;
    }

    m_pTaskbarList3->SetProgressState(mhWnd, nFlag);
}

static bool ImplHandleMouseMsg( HWND hWnd, UINT nMsg,
                                WPARAM wParam, LPARAM lParam )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return false;

    if( nMsg == WM_LBUTTONDOWN || nMsg == WM_MBUTTONDOWN || nMsg == WM_RBUTTONDOWN )
    {
        // #103168# post again if async focus has not arrived yet
        // hopefully we will not receive the corresponding button up before this
        // button down arrives again
        vcl::Window *pWin = pFrame->GetWindow();
        if( pWin && pWin->ImplGetWindowImpl()->mpFrameData->mnFocusId )
        {
            bool const ret = PostMessageW( hWnd, nMsg, wParam, lParam );
            SAL_WARN_IF(!ret, "vcl", "ERROR: PostMessage() failed!");
            return true;
        }
    }
    SalMouseEvent   aMouseEvt;
    bool            nRet;
    SalEvent        nEvent = SalEvent::NONE;
    bool            bCall = true;

    aMouseEvt.mnX       = static_cast<short>(LOWORD( lParam ));
    aMouseEvt.mnY       = static_cast<short>(HIWORD( lParam ));
    aMouseEvt.mnCode    = 0;
    aMouseEvt.mnTime    = GetMessageTime();

    // Use GetKeyState(), as some Logitech mouse drivers do not check
    // KeyState when simulating double-click with center mouse button

    if ( GetKeyState( VK_LBUTTON ) & 0x8000 )
        aMouseEvt.mnCode |= MOUSE_LEFT;
    if ( GetKeyState( VK_MBUTTON ) & 0x8000 )
        aMouseEvt.mnCode |= MOUSE_MIDDLE;
    if ( GetKeyState( VK_RBUTTON ) & 0x8000 )
        aMouseEvt.mnCode |= MOUSE_RIGHT;
    if ( GetKeyState( VK_SHIFT ) & 0x8000 )
        aMouseEvt.mnCode |= KEY_SHIFT;
    if ( GetKeyState( VK_CONTROL ) & 0x8000 )
        aMouseEvt.mnCode |= KEY_MOD1;
    if ( GetKeyState( VK_MENU ) & 0x8000 )
        aMouseEvt.mnCode |= KEY_MOD2;

    switch ( nMsg )
    {
        case WM_MOUSEMOVE:
            {
            // As the mouse events are not collected correctly when
            // pressing modifier keys (as interrupted by KeyEvents)
            // we do this here ourselves
            if ( aMouseEvt.mnCode & (KEY_SHIFT | KEY_MOD1 | KEY_MOD2) )
            {
                MSG aTempMsg;
                if ( PeekMessageW( &aTempMsg, hWnd, WM_MOUSEFIRST, WM_MOUSELAST, PM_NOREMOVE | PM_NOYIELD ) )
                {
                    if ( (aTempMsg.message == WM_MOUSEMOVE) &&
                         (aTempMsg.wParam == wParam) )
                        return true;
                }
            }

            SalData* pSalData = GetSalData();
            // Test for MouseLeave
            if ( pSalData->mhWantLeaveMsg && (pSalData->mhWantLeaveMsg != hWnd) )
                SendMessageW( pSalData->mhWantLeaveMsg, SAL_MSG_MOUSELEAVE, 0, GetMessagePos() );

            pSalData->mhWantLeaveMsg = hWnd;
            aMouseEvt.mnButton = 0;
            nEvent = SalEvent::MouseMove;
            }
            break;

        case WM_NCMOUSEMOVE:
        case SAL_MSG_MOUSELEAVE:
            {
            SalData* pSalData = GetSalData();
            if ( pSalData->mhWantLeaveMsg == hWnd )
            {
                // Mouse-Coordinates are relative to the screen
                POINT aPt;
                aPt.x = static_cast<short>(LOWORD(lParam));
                aPt.y = static_cast<short>(HIWORD(lParam));
                ScreenToClient(hWnd, &aPt);
                if (const auto& pHelpWin = ImplGetSVHelpData().mpHelpWin)
                {
                    const tools::Rectangle& rHelpRect = pHelpWin->GetHelpArea();
                    if (rHelpRect.Contains(Point(aPt.x, aPt.y)))
                    {
                        // We have entered a tooltip (help window). Don't call the handler here; it
                        // would launch the sequence "Mouse leaves the Control->Control redraws->
                        // Help window gets destroyed->Mouse enters the Control->Control redraws",
                        // which takes CPU and may flicker. Just destroy the help window and pretend
                        // we are still over the original window.
                        ImplDestroyHelpWindow(true);
                        bCall = false;
                        break;
                    }
                }
                pSalData->mhWantLeaveMsg = nullptr;
                aMouseEvt.mnX = aPt.x;
                aMouseEvt.mnY = aPt.y;
                aMouseEvt.mnButton = 0;
                nEvent = SalEvent::MouseLeave;
            }
            else
                bCall = false;
            }
            break;

        case WM_LBUTTONDOWN:
            aMouseEvt.mnButton = MOUSE_LEFT;
            nEvent = SalEvent::MouseButtonDown;
            break;

        case WM_MBUTTONDOWN:
            aMouseEvt.mnButton = MOUSE_MIDDLE;
            nEvent = SalEvent::MouseButtonDown;
            break;

        case WM_RBUTTONDOWN:
            aMouseEvt.mnButton = MOUSE_RIGHT;
            nEvent = SalEvent::MouseButtonDown;
            break;

        case WM_LBUTTONUP:
            aMouseEvt.mnButton = MOUSE_LEFT;
            nEvent = SalEvent::MouseButtonUp;
            break;

        case WM_MBUTTONUP:
            aMouseEvt.mnButton = MOUSE_MIDDLE;
            nEvent = SalEvent::MouseButtonUp;
            break;

        case WM_RBUTTONUP:
            aMouseEvt.mnButton = MOUSE_RIGHT;
            nEvent = SalEvent::MouseButtonUp;
            break;
    }

    // check if this window was destroyed - this might happen if we are the help window
    // and sent a mouse leave message to the application which killed the help window, ie ourselves
    if( !IsWindow( hWnd ) )
        return false;

    if ( bCall )
    {
        if ( nEvent == SalEvent::MouseButtonDown )
            UpdateWindow( hWnd );

        if( AllSettings::GetLayoutRTL() )
            aMouseEvt.mnX = pFrame->GetWidth() - 1 - aMouseEvt.mnX;

        nRet = pFrame->CallCallback( nEvent, &aMouseEvt );
        if ( nMsg == WM_MOUSEMOVE )
            SetCursor( pFrame->mhCursor );
    }
    else
        nRet = false;

    return nRet;
}

static bool ImplHandleMouseActivateMsg( HWND hWnd )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return false;

    if ( pFrame->mbFloatWin )
        return true;

    return pFrame->CallCallback( SalEvent::MouseActivate, nullptr );
}

static bool ImplHandleWheelMsg( HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam )
{
    DBG_ASSERT( nMsg == WM_MOUSEWHEEL ||
                nMsg == WM_MOUSEHWHEEL,
                "ImplHandleWheelMsg() called with no wheel mouse event" );

    ImplSalYieldMutexAcquireWithWait();

    bool nRet = false;
    WinSalFrame*   pFrame = GetWindowPtr( hWnd );
    if ( pFrame )
    {
        WORD    nWinModCode = GET_KEYSTATE_WPARAM( wParam );     // Key modifiers
        POINT   aWinPt;
        aWinPt.x    = static_cast<short>(LOWORD( lParam ));
        aWinPt.y    = static_cast<short>(HIWORD( lParam ));
        ScreenToClient( hWnd, &aWinPt );

        SalWheelMouseEvent aWheelEvt;
        aWheelEvt.mnTime        = GetMessageTime();
        aWheelEvt.mnX           = aWinPt.x;
        aWheelEvt.mnY           = aWinPt.y;
        aWheelEvt.mnCode        = 0;
        aWheelEvt.mnDelta       = GET_WHEEL_DELTA_WPARAM( wParam );     // Distance scrolled passed in message param
        aWheelEvt.mnNotchDelta  = aWheelEvt.mnDelta/WHEEL_DELTA;        // Number of mouse notches/detents scrolled
        if( aWheelEvt.mnNotchDelta == 0 )
        {
            // Keep mnNotchDelta nonzero unless distance scrolled was exactly zero.
            // Many places use its sign to indicate direction scrolled.
            if( aWheelEvt.mnDelta > 0 )
                aWheelEvt.mnNotchDelta = 1;
            else if( aWheelEvt.mnDelta < 0 )
                aWheelEvt.mnNotchDelta = -1;
        }

        if( nMsg == WM_MOUSEWHEEL )     // Vertical scroll
        {
            if ( aSalShlData.mnWheelScrollLines == WHEEL_PAGESCROLL )  // Mouse wheel set to "One screen at a time"
            {
                // Note: mnDelta may hit a multiple of WHEEL_DELTA via touchpad scrolling. That's the tradeoff to keep
                //  smooth touchpad scrolling with mouse wheel set to screen.

                if ( (aWheelEvt.mnDelta % WHEEL_DELTA) == 0 )   // Mouse wheel sends WHEEL_DELTA (or multiple of it)
                    aWheelEvt.mnScrollLines = SAL_WHEELMOUSE_EVENT_PAGESCROLL;          // "Magic" page scroll value
                else    // Touchpad can send smaller values. Use default 3 lines to scroll at a time.
                    aWheelEvt.mnScrollLines = aWheelEvt.mnDelta / double(WHEEL_DELTA) * 3.0;  // Calculate actual lines using distance
            }
            else    // Mouse wheel set to "Multiple lines at a time"
            {
                // Windows legacy touchpad support sends touchpad scroll gesture as multiple mouse wheel messages.
                // Calculate number of mouse notches scrolled using distance from Windows.
                aWheelEvt.mnScrollLines = aWheelEvt.mnDelta / double(WHEEL_DELTA);
                // Multiply by user setting for number of lines to scroll at a time.
                aWheelEvt.mnScrollLines *= aSalShlData.mnWheelScrollLines;
            }
            aWheelEvt.mbHorz        = false;
        }
        else    // Horizontal scroll
        {
            // Windows legacy touchpad support sends touchpad scroll gesture as multiple mouse wheel messages.
            // Calculate number of mouse notches scrolled using distance from Windows.
            aWheelEvt.mnScrollLines = aWheelEvt.mnDelta / double(WHEEL_DELTA);
            // Multiply by user setting for number of characters to scroll at a time.
            aWheelEvt.mnScrollLines *= aSalShlData.mnWheelScrollChars;
            aWheelEvt.mbHorz        = true;

            // fdo#36380 - seems horiz scrolling has swapped direction
            aWheelEvt.mnDelta *= -1;
            aWheelEvt.mnNotchDelta *= -1;
            aWheelEvt.mnScrollLines *= -1.0;
        }

        // Do not change magic value for page scrolling
        if (aWheelEvt.mnScrollLines != SAL_WHEELMOUSE_EVENT_PAGESCROLL)
        {
            // Scrolling code multiplies (scroll lines * number of notches), so pull # notches out to prevent double multiply.
            if (aWheelEvt.mnNotchDelta != 0)    // No divide by zero!
                aWheelEvt.mnScrollLines /= aWheelEvt.mnNotchDelta;
            else
                aWheelEvt.mnScrollLines = abs(aWheelEvt.mnScrollLines);     // Just ensure (+) value
        }

        if ( nWinModCode & MK_SHIFT )
            aWheelEvt.mnCode |= KEY_SHIFT;
        if ( nWinModCode & MK_CONTROL )
            aWheelEvt.mnCode |= KEY_MOD1;
        if ( GetKeyState( VK_MENU ) & 0x8000 )
            aWheelEvt.mnCode |= KEY_MOD2;

        if( AllSettings::GetLayoutRTL() )
            aWheelEvt.mnX = pFrame->GetWidth() - 1 - aWheelEvt.mnX;

        nRet = pFrame->CallCallback( SalEvent::WheelMouse, &aWheelEvt );
    }

    ImplSalYieldMutexRelease();

    return nRet;
}

static sal_uInt16 ImplSalGetKeyCode( WPARAM wParam )
{
    sal_uInt16 nKeyCode;

    // convert KeyCode
    if ( wParam < KEY_TAB_SIZE )
        nKeyCode = aImplTranslateKeyTab[wParam];
    else
    {
        SalData* pSalData = GetSalData();
        std::map< UINT, sal_uInt16 >::const_iterator it = pSalData->maVKMap.find( static_cast<UINT>(wParam) );
        if( it != pSalData->maVKMap.end() )
            nKeyCode = it->second;
        else
            nKeyCode = 0;
    }

    return nKeyCode;
}

static void ImplUpdateInputLang( WinSalFrame* pFrame )
{
    UINT nLang = LOWORD( GetKeyboardLayout( 0 ) );
    if ( nLang && nLang != pFrame->mnInputLang )
    {
        // keep input lang up-to-date
        pFrame->mnInputLang = nLang;
    }

    // We are on Windows NT so we use Unicode FrameProcs and get
    // Unicode charcodes directly from Windows no need to set up a
    // code page
    return;
}

static sal_Unicode ImplGetCharCode( WinSalFrame* pFrame, WPARAM nCharCode )
{
    ImplUpdateInputLang( pFrame );

    // We are on Windows NT so we use Unicode FrameProcs and we
    // get Unicode charcodes directly from Windows
    return static_cast<sal_Unicode>(nCharCode);
}

LanguageType WinSalFrame::GetInputLanguage()
{
    if( !mnInputLang )
        ImplUpdateInputLang( this );

    if( !mnInputLang )
        return LANGUAGE_DONTKNOW;
    else
        return LanguageType(mnInputLang);
}

bool WinSalFrame::MapUnicodeToKeyCode( sal_Unicode aUnicode, LanguageType aLangType, vcl::KeyCode& rKeyCode )
{
    bool bRet = false;
    sal_IntPtr nLangType = static_cast<sal_uInt16>(aLangType);
    // just use the passed language identifier, do not try to load additional keyboard support
    HKL hkl = reinterpret_cast<HKL>(nLangType);

    if( hkl )
    {
        SHORT scan = VkKeyScanExW( aUnicode, hkl );
        if( LOWORD(scan) == 0xFFFF )
            // keyboard not loaded or key cannot be mapped
            bRet = false;
        else
        {
            BYTE vkeycode   = LOBYTE(scan);
            BYTE shiftstate = HIBYTE(scan);

            // Last argument is set to false, because there's no decision made
            // yet which key should be assigned to MOD3 modifier on Windows.
            // Windows key - user's can be confused, because it should display
            //               Windows menu (applies to both left/right key)
            // Menu key    - this key is used to display context menu
            // AltGr key   - probably it has no sense
            rKeyCode = vcl::KeyCode( ImplSalGetKeyCode( vkeycode ),
                (shiftstate & 0x01) != 0,     // shift
                (shiftstate & 0x02) != 0,     // ctrl
                (shiftstate & 0x04) != 0,     // alt
                false );
            bRet = true;
        }
    }

    return bRet;
}

static void UnsetAltIfAltGr(SalKeyEvent& rKeyEvt, sal_uInt16 nModCode)
{
    if ((nModCode & (KEY_MOD1 | KEY_MOD2)) == (KEY_MOD1 | KEY_MOD2) &&
        rKeyEvt.mnCharCode)
    {
        // this is actually AltGr and should not be handled as Alt
        rKeyEvt.mnCode &= ~(KEY_MOD1 | KEY_MOD2);
    }
}

// tdf#152404 Commit uncommitted text before dispatching key shortcuts. In
// certain cases such as pressing Control-Alt-C in a Writer document while
// there is uncommitted text will call WinSalFrame::EndExtTextInput() which
// will dispatch a SalEvent::EndExtTextInput event. Writer's handler for that
// event will delete the uncommitted text and then insert the committed text
// but LibreOffice will crash when deleting the uncommitted text because
// deletion of the text also removes and deletes the newly inserted comment.
static void FlushIMBeforeShortCut(WinSalFrame* pFrame, SalEvent nEvent, sal_uInt16 nModCode)
{
    if (pFrame->mbCandidateMode && nEvent == SalEvent::KeyInput
        && (nModCode & (KEY_MOD1 | KEY_MOD2)))
    {
        pFrame->EndExtTextInput(EndExtTextInputFlags::Complete);
    }
}

// When Num Lock is off, the key codes from NumPag come as arrows, PgUp/PgDn, etc.
static WORD NumPadFromArrows(WORD vk)
{
    switch (vk)
    {
        case VK_CLEAR:
            return VK_NUMPAD5;
        case VK_PRIOR:
            return VK_NUMPAD9;
        case VK_NEXT:
            return VK_NUMPAD3;
        case VK_END:
            return VK_NUMPAD1;
        case VK_HOME:
            return VK_NUMPAD7;
        case VK_LEFT:
            return VK_NUMPAD4;
        case VK_UP:
            return VK_NUMPAD8;
        case VK_RIGHT:
            return VK_NUMPAD6;
        case VK_DOWN:
            return VK_NUMPAD2;
        case VK_INSERT:
            return VK_NUMPAD0;
        default:
            return vk;
    }
}

static bool HandleAltNumPadCode(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
    struct
    {
        bool started = false;
        //static bool hex = false; // TODO: support HKEY_CURRENT_USER\Control Panel\Input Method\EnableHexNumpad
        sal_UCS4 ch = 0;
        bool wait_WM_CHAR = false;
        void clear()
        {
            started = false;
            ch = 0;
            wait_WM_CHAR = false;
        }
    } static state;

    WORD vk = LOWORD(wParam);
    WORD keyFlags = HIWORD(lParam);

    switch (nMsg)
    {
        case WM_CHAR:
            if (state.wait_WM_CHAR && MapVirtualKeyW(LOBYTE(keyFlags), MAPVK_VSC_TO_VK) == VK_MENU)
            {
                state.clear();
                // Ignore it - it is synthetized (incorrect, truncated) character from system
                return true;
            }

            break;

        case WM_SYSKEYDOWN:
            if (vk == VK_MENU)
            {
                if (!(keyFlags & KF_REPEAT))
                    state.clear();
                state.started = true;
                return false; // This must be processed further - e.g., to show accelerators
            }

            if (!state.started)
                break;

            if (keyFlags & KF_EXTENDED)
                break; // NUMPAD numeric keys are *not* considered extended

            vk = NumPadFromArrows(vk);
            if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
                return true;

            break;

        case WM_SYSKEYUP:
            if (!state.started)
                break;

            if (keyFlags & KF_EXTENDED)
                break; // NUMPAD numeric keys are *not* considered extended

            vk = NumPadFromArrows(vk);
            if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
            {
                state.ch *= 10;
                state.ch += vk - VK_NUMPAD0;
                return true;
            }

            break;

        case WM_KEYUP:
            if (vk == VK_MENU && state.started && state.ch)
            {
                sal_UCS4 ch = state.ch;
                state.clear();
                // Let system provide codes for values below 256
                if (ch >= 256 && rtl::isUnicodeCodePoint(ch))
                {
                    PostMessageW(hWnd, WM_UNICHAR, ch, 0);
                    state.wait_WM_CHAR = true;
                }
                return true;
            }
            break;
    }

    state.clear();
    return false;
}

static bool ImplHandleKeyMsg( HWND hWnd, UINT nMsg,
                              WPARAM wParam, LPARAM lParam, LRESULT& rResult )
{
    static bool         bIgnoreCharMsg  = false;
    static WPARAM       nDeadChar       = 0;
    static WPARAM       nLastVKChar     = 0;
    static sal_uInt16   nLastChar       = 0;
    sal_uInt16          nRepeat         = LOWORD( lParam );
    if (nRepeat)
        --nRepeat;
    sal_uInt16          nModCode        = 0;

    // this key might have been relayed by SysChild and thus
    // may not be processed twice
    GetSalData()->mnSalObjWantKeyEvt = 0;

    if ( nMsg == WM_DEADCHAR )
    {
        nDeadChar = wParam;
        return false;
    }

    if (HandleAltNumPadCode(hWnd, nMsg, wParam, lParam))
        return true; // no default processing

    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return false;

    // reset the background mode for each text input,
    // as some tools such as RichWin may have changed it
    if ( pFrame->mpLocalGraphics &&
         pFrame->mpLocalGraphics->getHDC() )
        SetBkMode( pFrame->mpLocalGraphics->getHDC(), TRANSPARENT );

    // determine modifiers
    if ( GetKeyState( VK_SHIFT ) & 0x8000 )
        nModCode |= KEY_SHIFT;
    if ( GetKeyState( VK_CONTROL ) & 0x8000 )
        nModCode |= KEY_MOD1;
    if (GetKeyState(VK_MENU) & 0x8000)
        nModCode |= KEY_MOD2;

    if ( (nMsg == WM_CHAR) || (nMsg == WM_SYSCHAR) )
    {
        nDeadChar = 0;

        if ( bIgnoreCharMsg )
        {
            bIgnoreCharMsg = false;
            // #101635# if zero is returned here for WM_SYSCHAR (ALT+<key>) Windows will beep
            // because this 'hotkey' was not processed -> better return 1
            // except for Alt-SPACE which should always open the sysmenu (#104616#)

            // also return zero if a system menubar is available that might process this hotkey
            // this also applies to the OLE inplace embedding where we are a child window
            if( (GetWindowStyle( hWnd ) & WS_CHILD) || GetMenu( hWnd ) || (wParam == 0x20) )
                return false;
            else
                return true;
        }

        // ignore backspace as a single key, so that
        // we do not get problems for combinations w/ a DeadKey
        if ( wParam == 0x08 )    // BACKSPACE
            return false;

        // only "free flying" WM_CHAR messages arrive here, that are
        // created by typing an ALT-NUMPAD combination
        SalKeyEvent aKeyEvt;

        if ( (wParam >= '0') && (wParam <= '9') )
            aKeyEvt.mnCode = sal::static_int_cast<sal_uInt16>(KEYGROUP_NUM + wParam - '0');
        else if ( (wParam >= 'A') && (wParam <= 'Z') )
            aKeyEvt.mnCode = sal::static_int_cast<sal_uInt16>(KEYGROUP_ALPHA + wParam - 'A');
        else if ( (wParam >= 'a') && (wParam <= 'z') )
            aKeyEvt.mnCode = sal::static_int_cast<sal_uInt16>(KEYGROUP_ALPHA + wParam - 'a');
        else if ( wParam == 0x0D )    // RETURN
            aKeyEvt.mnCode = KEY_RETURN;
        else if ( wParam == 0x1B )    // ESCAPE
            aKeyEvt.mnCode = KEY_ESCAPE;
        else if ( wParam == 0x09 )    // TAB
            aKeyEvt.mnCode = KEY_TAB;
        else if ( wParam == 0x20 )    // SPACE
            aKeyEvt.mnCode = KEY_SPACE;
        else
            aKeyEvt.mnCode = 0;

        aKeyEvt.mnCode     |= nModCode;
        aKeyEvt.mnCharCode  = ImplGetCharCode( pFrame, wParam );
        aKeyEvt.mnRepeat    = nRepeat;

        UnsetAltIfAltGr(aKeyEvt, nModCode);
        FlushIMBeforeShortCut(pFrame, SalEvent::KeyInput, nModCode);

        nLastChar = 0;
        nLastVKChar = 0;

        bool nRet = pFrame->CallCallback( SalEvent::KeyInput, &aKeyEvt );
        pFrame->CallCallback( SalEvent::KeyUp, &aKeyEvt );
        return nRet;
    }
    // #i11583#, MCD, 2003-01-13, Support for WM_UNICHAR & Keyman 6.0; addition begins
    else if( nMsg == WM_UNICHAR )
    {
        // If Windows is asking if we accept WM_UNICHAR, return TRUE
        if(wParam == UNICODE_NOCHAR)
        {
            rResult = TRUE; // ssa: this will actually return TRUE to windows
            return true;    // ...but this will only avoid calling the defwindowproc
        }

        if (!rtl::isUnicodeCodePoint(wParam))
            return false;

        SalKeyEvent aKeyEvt;
        aKeyEvt.mnCode     = nModCode; // Or should it be 0? - as this is always a character returned
        aKeyEvt.mnRepeat   = 0;

        if( wParam >= Uni_SupplementaryPlanesStart )
        {
            // character is supplementary char in UTF-32 format - must be converted to UTF-16 supplementary pair
            aKeyEvt.mnCharCode = rtl::getHighSurrogate(wParam);
            nLastChar = 0;
            nLastVKChar = 0;
            pFrame->CallCallback(SalEvent::KeyInput, &aKeyEvt);
            pFrame->CallCallback(SalEvent::KeyUp, &aKeyEvt);
            wParam = rtl::getLowSurrogate(wParam);
        }

        aKeyEvt.mnCharCode = static_cast<sal_Unicode>(wParam);

        nLastChar = 0;
        nLastVKChar = 0;
        bool nRet = pFrame->CallCallback( SalEvent::KeyInput, &aKeyEvt );
        pFrame->CallCallback( SalEvent::KeyUp, &aKeyEvt );

        return nRet;
    }
    // MCD, 2003-01-13, Support for WM_UNICHAR & Keyman 6.0; addition ends
    else
    {
        static ModKeyFlags nLastModKeyCode = ModKeyFlags::NONE;

        // for shift, control and menu we issue a KeyModChange event
        if ( (wParam == VK_SHIFT) || (wParam == VK_CONTROL) || (wParam == VK_MENU) )
        {
            ModKeyFlags tmpCode = ModKeyFlags::NONE;
            if( GetKeyState( VK_LSHIFT )  & 0x8000 )
                tmpCode |= ModKeyFlags::LeftShift;
            if( GetKeyState( VK_RSHIFT )  & 0x8000 )
                tmpCode |= ModKeyFlags::RightShift;
            if( GetKeyState( VK_LCONTROL ) & 0x8000 )
                tmpCode |= ModKeyFlags::LeftMod1;
            if( GetKeyState( VK_RCONTROL ) & 0x8000 )
                tmpCode |= ModKeyFlags::RightMod1;
            if( GetKeyState( VK_LMENU )  & 0x8000 )
                tmpCode |= ModKeyFlags::LeftMod2;
            if( GetKeyState( VK_RMENU )  & 0x8000 )
                tmpCode |= ModKeyFlags::RightMod2;

            if (tmpCode != nLastModKeyCode)
            {
                SalKeyModEvent aModEvt;
                aModEvt.mbDown = nMsg == WM_KEYDOWN || nMsg == WM_SYSKEYDOWN;
                aModEvt.mnCode = nModCode;
                aModEvt.mnModKeyCode = tmpCode < nLastModKeyCode ? nLastModKeyCode : tmpCode;
                nLastModKeyCode = tmpCode;
                return pFrame->CallCallback(SalEvent::KeyModChange, &aModEvt);
            }
            return false;
        }
        else
        {
            SalKeyEvent     aKeyEvt;
            SalEvent        nEvent;
            MSG             aCharMsg;
            bool            bCharPeek = false;
            UINT            nCharMsg = WM_CHAR;
            bool            bKeyUp = (nMsg == WM_KEYUP) || (nMsg == WM_SYSKEYUP);

            comphelper::ScopeGuard aEndModKeyCodes(
                [nModCode, pFrame, listener = vcl::DeletionListener(pFrame)]
                {
                    if (listener.isDeleted())
                        return;
                    // Send SalEvent::KeyModChange, to make sure that this window ends special mode
                    // (e.g., hides mnemonics if auto-accelerator feature is active)
                    SalKeyModEvent aModEvt;
                    aModEvt.mbDown = false;
                    aModEvt.mnCode = nModCode;
                    aModEvt.mnModKeyCode = nLastModKeyCode;
                    pFrame->CallCallback(SalEvent::KeyModChange, &aModEvt);
                });
            if (nLastModKeyCode == ModKeyFlags::NONE)
                aEndModKeyCodes.dismiss();
            nLastModKeyCode = ModKeyFlags::NONE; // make sure no modkey messages are sent if they belong to a hotkey (see above)
            aKeyEvt.mnCharCode = 0;
            aKeyEvt.mnCode = ImplSalGetKeyCode( wParam );
            if ( !bKeyUp )
            {
                // check for charcode
                // Get the related WM_CHAR message using PeekMessage, if available.
                // The WM_CHAR message is always at the beginning of the
                // message queue. Also it is made certain that there is always only
                // one WM_CHAR message in the queue.
                bCharPeek = PeekMessageW( &aCharMsg, hWnd,
                                             WM_CHAR, WM_CHAR, PM_NOREMOVE | PM_NOYIELD );
                if ( bCharPeek && (nDeadChar == aCharMsg.wParam) )
                {
                    bCharPeek = false;
                    nDeadChar = 0;

                    if ( wParam == VK_BACK )
                    {
                        PeekMessageW( &aCharMsg, hWnd,
                                         nCharMsg, nCharMsg, PM_REMOVE | PM_NOYIELD );
                        return false;
                    }
                }
                else
                {
                    if ( !bCharPeek )
                    {
                        bCharPeek = PeekMessageW( &aCharMsg, hWnd,
                                                    WM_SYSCHAR, WM_SYSCHAR, PM_NOREMOVE | PM_NOYIELD );
                        nCharMsg = WM_SYSCHAR;
                    }
                }
                if ( bCharPeek )
                    aKeyEvt.mnCharCode = ImplGetCharCode( pFrame, aCharMsg.wParam );
                else
                    aKeyEvt.mnCharCode = 0;

                nLastChar = aKeyEvt.mnCharCode;
                nLastVKChar = wParam;
            }
            else
            {
                if ( wParam == nLastVKChar )
                {
                    aKeyEvt.mnCharCode = nLastChar;
                    nLastChar = 0;
                    nLastVKChar = 0;
                }
            }

            if ( aKeyEvt.mnCode || aKeyEvt.mnCharCode )
            {
                if ( bKeyUp )
                    nEvent = SalEvent::KeyUp;
                else
                    nEvent = SalEvent::KeyInput;

                aKeyEvt.mnCode     |= nModCode;
                aKeyEvt.mnRepeat    = nRepeat;

                UnsetAltIfAltGr(aKeyEvt, nModCode);
                FlushIMBeforeShortCut(pFrame, nEvent, nModCode);

                bIgnoreCharMsg = bCharPeek;
                bool nRet = pFrame->CallCallback( nEvent, &aKeyEvt );
                // independent part only reacts on keyup but Windows does not send
                // keyup for VK_HANJA
                if( aKeyEvt.mnCode == KEY_HANGUL_HANJA )
                    nRet = pFrame->CallCallback( SalEvent::KeyUp, &aKeyEvt );

                bIgnoreCharMsg = false;

                // char-message, then remove or ignore
                if ( bCharPeek )
                {
                    nDeadChar = 0;
                    if ( nRet )
                    {
                        PeekMessageW( &aCharMsg, hWnd,
                                         nCharMsg, nCharMsg, PM_REMOVE | PM_NOYIELD );
                    }
                    else
                        bIgnoreCharMsg = true;
                }

                return nRet;
            }
            else
                return false;
        }
    }
}

bool ImplHandleSalObjKeyMsg( HWND hWnd, UINT nMsg,
                             WPARAM wParam, LPARAM lParam )
{
    if ( (nMsg == WM_KEYDOWN) || (nMsg == WM_KEYUP) )
    {
        WinSalFrame* pFrame = GetWindowPtr( hWnd );
        if ( !pFrame )
            return false;

        sal_uInt16  nRepeat     = LOWORD( lParam )-1;
        sal_uInt16  nModCode    = 0;

        // determine modifiers
        if ( GetKeyState( VK_SHIFT ) & 0x8000 )
            nModCode |= KEY_SHIFT;
        if ( GetKeyState( VK_CONTROL ) & 0x8000 )
            nModCode |= KEY_MOD1;
        if ( GetKeyState( VK_MENU ) & 0x8000 )
            nModCode |= KEY_MOD2;

        if ( (wParam != VK_SHIFT) && (wParam != VK_CONTROL) && (wParam != VK_MENU) )
        {
            SalKeyEvent     aKeyEvt;
            SalEvent        nEvent;

            // convert KeyCode
            aKeyEvt.mnCode      = ImplSalGetKeyCode( wParam );
            aKeyEvt.mnCharCode  = 0;

            if ( aKeyEvt.mnCode )
            {
                if (nMsg == WM_KEYUP)
                    nEvent = SalEvent::KeyUp;
                else
                    nEvent = SalEvent::KeyInput;

                aKeyEvt.mnCode     |= nModCode;
                aKeyEvt.mnRepeat    = nRepeat;
                bool nRet = pFrame->CallCallback( nEvent, &aKeyEvt );
                return nRet;
            }
            else
                return false;
        }
    }

    return false;
}

bool ImplHandleSalObjSysCharMsg( HWND hWnd, WPARAM wParam, LPARAM lParam )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return false;

    sal_uInt16  nRepeat     = LOWORD( lParam )-1;
    sal_uInt16  nModCode    = 0;
    sal_uInt16  cKeyCode    = static_cast<sal_uInt16>(wParam);

    // determine modifiers
    if ( GetKeyState( VK_SHIFT ) & 0x8000 )
        nModCode |= KEY_SHIFT;
    if ( GetKeyState( VK_CONTROL ) & 0x8000 )
        nModCode |= KEY_MOD1;
    nModCode |= KEY_MOD2;

    // assemble KeyEvent
    SalKeyEvent aKeyEvt;
    if ( (cKeyCode >= 48) && (cKeyCode <= 57) )
        aKeyEvt.mnCode = KEY_0+(cKeyCode-48);
    else if ( (cKeyCode >= 65) && (cKeyCode <= 90) )
        aKeyEvt.mnCode = KEY_A+(cKeyCode-65);
    else  if ( (cKeyCode >= 97) && (cKeyCode <= 122) )
        aKeyEvt.mnCode = KEY_A+(cKeyCode-97);
    else
        aKeyEvt.mnCode = 0;
    aKeyEvt.mnCode     |= nModCode;
    aKeyEvt.mnCharCode  = ImplGetCharCode( pFrame, cKeyCode );
    aKeyEvt.mnRepeat    = nRepeat;
    bool nRet = pFrame->CallCallback( SalEvent::KeyInput, &aKeyEvt );
    pFrame->CallCallback( SalEvent::KeyUp, &aKeyEvt );
    return nRet;
}

namespace {

enum class DeferPolicy
{
    Blocked,
    Allowed
};

}

// Remember to release the solar mutex on success!
static WinSalFrame* ProcessOrDeferMessage( HWND hWnd, INT nMsg, WPARAM pWParam = 0,
                                                  DeferPolicy eCanDefer = DeferPolicy::Allowed )
{
    bool bFailedCondition = false, bGotMutex = false;
    WinSalFrame* pFrame = nullptr;

    if ( DeferPolicy::Blocked == eCanDefer )
        assert( (DeferPolicy::Blocked == eCanDefer) && (nMsg == 0) && (pWParam == 0) );
    else
        assert( (DeferPolicy::Allowed == eCanDefer) && (nMsg != 0) );

    if ( DeferPolicy::Blocked == eCanDefer )
    {
        ImplSalYieldMutexAcquireWithWait();
        bGotMutex = true;
    }
    else if ( !(bGotMutex = ImplSalYieldMutexTryToAcquire()) )
        bFailedCondition = true;

    if ( !bFailedCondition )
    {
        pFrame = GetWindowPtr( hWnd );
        bFailedCondition = pFrame == nullptr;
    }

    if ( bFailedCondition )
    {
        if ( bGotMutex )
            ImplSalYieldMutexRelease();
        if ( DeferPolicy::Allowed == eCanDefer )
        {
            bool const ret = PostMessageW(hWnd, nMsg, pWParam, 0);
            SAL_WARN_IF(!ret, "vcl", "ERROR: PostMessage() failed!");
        }
    }

    return pFrame;
}

namespace {

enum class PostedState
{
    IsPosted,
    IsInitial
};

}

static bool ImplHandlePostPaintMsg( HWND hWnd, RECT* pRect,
                                    PostedState eProcessed = PostedState::IsPosted )
{
    RECT* pMsgRect;
    if ( PostedState::IsInitial == eProcessed )
    {
        pMsgRect = new RECT;
        CopyRect( pMsgRect, pRect );
    }
    else
        pMsgRect = pRect;

    WinSalFrame* pFrame = ProcessOrDeferMessage( hWnd, SAL_MSG_POSTPAINT,
                                                 reinterpret_cast<WPARAM>(pMsgRect) );
    if ( pFrame )
    {
        SalPaintEvent aPEvt( pRect->left, pRect->top, pRect->right-pRect->left, pRect->bottom-pRect->top );
        pFrame->CallCallback( SalEvent::Paint, &aPEvt );
        ImplSalYieldMutexRelease();
        if ( PostedState::IsPosted == eProcessed )
            delete pRect;
    }

    return (pFrame != nullptr);
}

static bool ImplHandlePaintMsg( HWND hWnd )
{
    bool bPaintSuccessful = false;

    // even without the Yield mutex, we can still change the clip region,
    // because other threads don't use the Yield mutex
    // --> see AcquireGraphics()

    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( pFrame )
    {
        // clip region must be set, as we don't get a proper
        // bounding rectangle otherwise
        WinSalGraphics *pGraphics = pFrame->mpLocalGraphics;
        bool bHasClipRegion = pGraphics &&
            pGraphics->getHDC() && pGraphics->getRegion();
        if ( bHasClipRegion )
            SelectClipRgn( pGraphics->getHDC(), nullptr );

        // according to Windows documentation one shall check first if
        // there really is a paint-region
        RECT aUpdateRect;
        PAINTSTRUCT aPs;
        bool bHasPaintRegion = GetUpdateRect( hWnd, nullptr, FALSE );
        if ( bHasPaintRegion )
        {
            // call BeginPaint/EndPaint to query the paint rect and use
            // this information in the (deferred) paint
            BeginPaint( hWnd, &aPs );
            CopyRect( &aUpdateRect, &aPs.rcPaint );
        }

        // reset clip region
        if ( bHasClipRegion )
            SelectClipRgn( pGraphics->getHDC(), pGraphics->getRegion() );

        // try painting
        if ( bHasPaintRegion )
        {
            bPaintSuccessful = ImplHandlePostPaintMsg(
                hWnd, &aUpdateRect, PostedState::IsInitial );
            EndPaint( hWnd, &aPs );
        }
        else // if there is nothing to paint, the paint is successful
            bPaintSuccessful = true;
    }

    return bPaintSuccessful;
}

void WinSalFrame::SetMaximizedFrameGeometry(HWND hWnd, RECT* pParentRect )
{
    // calculate and set frame geometry of a maximized window - useful if the window is still hidden

    // dualmonitor support:
    // Get screensize of the monitor with the mouse pointer

    RECT aRectMouse;
    if( ! pParentRect )
    {
        POINT pt;
        GetCursorPos( &pt );
        aRectMouse.left = pt.x;
        aRectMouse.top = pt.y;
        aRectMouse.right = pt.x+2;
        aRectMouse.bottom = pt.y+2;
        pParentRect = &aRectMouse;
    }

    RECT aRect;
    ImplSalGetWorkArea( hWnd, &aRect, pParentRect );

    // a maximized window has no other borders than the caption
    maGeometry.setDecorations(0, mbCaption ? GetSystemMetrics(SM_CYCAPTION) : 0, 0, 0);

    aRect.top += maGeometry.topDecoration();
    maGeometry.setPos({ aRect.left, aRect.top });
    SetGeometrySize(maGeometry, { aRect.right - aRect.left, aRect.bottom - aRect.top });
}

void WinSalFrame::UpdateFrameGeometry()
{
    RECT aRect;
    GetWindowRect(mhWnd, &aRect);
    maGeometry.setPosSize({ 0, 0 }, { 0, 0 });
    maGeometry.setDecorations(0, 0, 0, 0);
    maGeometry.setScreen(0);

    if (IsIconic(mhWnd))
        return;

    POINT aPt;
    aPt.x=0;
    aPt.y=0;
    ClientToScreen(mhWnd, &aPt);
    int cx = aPt.x - aRect.left;

    maGeometry.setDecorations(cx, aPt.y - aRect.top, cx, 0);
    maGeometry.setPos({ aPt.x, aPt.y });

    RECT aInnerRect;
    GetClientRect(mhWnd, &aInnerRect);
    if( aInnerRect.right )
    {
        // improve right decoration
        aPt.x=aInnerRect.right;
        aPt.y=aInnerRect.top;
        ClientToScreen(mhWnd, &aPt);
        maGeometry.setRightDecoration(aRect.right - aPt.x);
    }
    if( aInnerRect.bottom ) // may be zero if window was not shown yet
        maGeometry.setBottomDecoration(aRect.bottom - aPt.y - aInnerRect.bottom);
    else
        // bottom border is typically the same as left/right
        maGeometry.setBottomDecoration(maGeometry.leftDecoration());

    int nWidth  = aRect.right - aRect.left
        - maGeometry.rightDecoration() - maGeometry.leftDecoration();
    int nHeight = aRect.bottom - aRect.top
        - maGeometry.bottomDecoration() - maGeometry.topDecoration();
    SetGeometrySize(maGeometry, { nWidth, nHeight });
    updateScreenNumber();
}

static void ImplCallClosePopupsHdl( HWND hWnd )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( pFrame )
    {
        pFrame->CallCallback( SalEvent::ClosePopups, nullptr );
    }
}

static void ImplCallMoveHdl(HWND hWnd)
{
    WinSalFrame* pFrame = ProcessOrDeferMessage(hWnd, SAL_MSG_POSTMOVE);
    if (!pFrame)
        return;

    pFrame->CallCallback(SalEvent::Move, nullptr);

    ImplSalYieldMutexRelease();
}

static void ImplHandleMoveMsg(HWND hWnd, LPARAM lParam)
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if (!pFrame)
        return;

    pFrame->UpdateFrameGeometry();

#ifdef NDEBUG
    (void) lParam;
#endif
    SAL_WARN_IF(!IsIconic(hWnd) && pFrame->GetUnmirroredGeometry().x() != static_cast<sal_Int16>(LOWORD(lParam)),
                "vcl",
                "Unexpected X: " << pFrame->GetUnmirroredGeometry().x() << " instead of "
                                 << static_cast<sal_Int16>(LOWORD(lParam)));
    SAL_WARN_IF(!IsIconic(hWnd) && pFrame->GetUnmirroredGeometry().y() != static_cast<sal_Int16>(HIWORD(lParam)),
                "vcl",
                "Unexpected Y: " << pFrame->GetUnmirroredGeometry().y() << " instead of "
                                 << static_cast<sal_Int16>(HIWORD(lParam)));

    if (GetWindowStyle(hWnd) & WS_VISIBLE)
        pFrame->mbDefPos = false;

    // protect against recursion
    if (!pFrame->mbInMoveMsg)
    {
        // adjust window again for FullScreenMode
        pFrame->mbInMoveMsg = true;
        if (pFrame->isFullScreen())
            ImplSalFrameFullScreenPos(pFrame);
        pFrame->mbInMoveMsg = false;
    }

    pFrame->UpdateFrameState();

    ImplCallMoveHdl(hWnd);
}

static void ImplCallSizeHdl( HWND hWnd )
{
    WinSalFrame* pFrame = ProcessOrDeferMessage( hWnd, SAL_MSG_POSTCALLSIZE );
    if (!pFrame)
        return;

    pFrame->CallCallback(SalEvent::Resize, nullptr);
    // to avoid double Paints by VCL and SAL
    if (IsWindowVisible(hWnd) && !pFrame->mbInShow)
        UpdateWindow(hWnd);

    ImplSalYieldMutexRelease();
}

static void ImplHandleSizeMsg(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    if ((wParam == SIZE_MAXSHOW) || (wParam == SIZE_MAXHIDE))
        return;

    WinSalFrame* pFrame = GetWindowPtr(hWnd);
    if (!pFrame)
        return;

    pFrame->UpdateFrameGeometry();

#ifdef NDEBUG
    (void) lParam;
#endif
    assert(pFrame->GetWidth() == static_cast<sal_Int16>(LOWORD(lParam)));
    assert(pFrame->GetHeight() == static_cast<sal_Int16>(HIWORD(lParam)));

    pFrame->UpdateFrameState();

    ImplCallSizeHdl(hWnd);

    WinSalTimer* pTimer = static_cast<WinSalTimer*>(ImplGetSVData()->maSchedCtx.mpSalTimer);
    if (pTimer)
        pTimer->SetForceRealTimer(true);
}

static void ImplHandleFocusMsg( HWND hWnd )
{
    WinSalFrame* pFrame = ProcessOrDeferMessage( hWnd, SAL_MSG_POSTFOCUS );
    if (!pFrame)
        return;
    const ::comphelper::ScopeGuard aScopeGuard([](){ ImplSalYieldMutexRelease(); });

    if (WinSalFrame::mbInReparent)
        return;

    const bool bGotFocus = ::GetFocus() == hWnd;
    if (bGotFocus)
    {
        if (IsWindowVisible(hWnd) && !pFrame->mbInShow)
            UpdateWindow(hWnd);

        // do we support IME?
        if (pFrame->mbIME && pFrame->mhDefIMEContext)
        {
            UINT nImeProps = ImmGetProperty(GetKeyboardLayout(0), IGP_PROPERTY);
            pFrame->mbSpezIME = (nImeProps & IME_PROP_SPECIAL_UI) != 0;
            pFrame->mbAtCursorIME = (nImeProps & IME_PROP_AT_CARET) != 0;
            pFrame->mbHandleIME = !pFrame->mbSpezIME;
        }
    }

    pFrame->CallCallback(bGotFocus ? SalEvent::GetFocus : SalEvent::LoseFocus, nullptr);
}

static void ImplHandleCloseMsg( HWND hWnd )
{
    WinSalFrame* pFrame = ProcessOrDeferMessage( hWnd, WM_CLOSE );
    if ( pFrame )
    {
        pFrame->CallCallback( SalEvent::Close, nullptr );
        ImplSalYieldMutexRelease();
    }
}

static bool ImplHandleShutDownMsg( HWND hWnd )
{
    bool nRet = false;
    WinSalFrame* pFrame = ProcessOrDeferMessage( hWnd, 0, 0, DeferPolicy::Blocked );
    if ( pFrame )
    {
        nRet = pFrame->CallCallback( SalEvent::Shutdown, nullptr );
        ImplSalYieldMutexRelease();
    }
    return nRet;
}

static void ImplHandleSettingsChangeMsg(HWND hWnd, UINT nMsg, WPARAM /*wParam*/, LPARAM lParam)
{
    SalEvent nSalEvent = SalEvent::SettingsChanged;

    if ( nMsg == WM_DEVMODECHANGE )
        nSalEvent = SalEvent::PrinterChanged;
    else if ( nMsg == WM_DISPLAYCHANGE )
        nSalEvent = SalEvent::DisplayChanged;
    else if ( nMsg == WM_FONTCHANGE )
        nSalEvent = SalEvent::FontChanged;
    else if ( nMsg == WM_SETTINGCHANGE )
    {
        if (const auto* paramStr = o3tl::toU(reinterpret_cast<const wchar_t*>(lParam)))
        {
            if (rtl_ustr_ascii_compareIgnoreAsciiCase(paramStr, "devices") == 0)
                nSalEvent = SalEvent::PrinterChanged;
        }
        aSalShlData.mnWheelScrollLines = ImplSalGetWheelScrollLines();
        aSalShlData.mnWheelScrollChars = ImplSalGetWheelScrollChars();
        UpdateAutoAccel();
        UpdateDarkMode(hWnd);
        GetSalData()->mbThemeChanged = true;
    }

    WinSalFrame* pFrame = ProcessOrDeferMessage( hWnd, 0, 0, DeferPolicy::Blocked );
    if (!pFrame)
        return;

    if (((nMsg == WM_DISPLAYCHANGE) || (nMsg == WM_SETTINGCHANGE)) && pFrame->isFullScreen())
        ImplSalFrameFullScreenPos(pFrame);

    pFrame->CallCallback(nSalEvent, nullptr);

    ImplSalYieldMutexRelease();
}

static void ImplHandleUserEvent( HWND hWnd, LPARAM lParam )
{
    WinSalFrame* pFrame = ProcessOrDeferMessage( hWnd, 0, 0, DeferPolicy::Blocked );
    if ( pFrame )
    {
        pFrame->CallCallback( SalEvent::UserEvent, reinterpret_cast<void*>(lParam) );
        ImplSalYieldMutexRelease();
    }
}

static bool ImplHandleMinMax( HWND hWnd, LPARAM lParam )
{
    bool bRet = false;

    if ( ImplSalYieldMutexTryToAcquire() )
    {
        WinSalFrame* pFrame = GetWindowPtr( hWnd );
        if ( pFrame )
        {
            MINMAXINFO* pMinMax = reinterpret_cast<MINMAXINFO*>(lParam);

            if (pFrame->isFullScreen())
            {
                int         nX;
                int         nY;
                int         nDX;
                int         nDY;
                ImplSalCalcFullScreenSize( pFrame, nX, nY, nDX, nDY );

                if ( pMinMax->ptMaxPosition.x > nX )
                    pMinMax->ptMaxPosition.x = nX;
                if ( pMinMax->ptMaxPosition.y > nY )
                    pMinMax->ptMaxPosition.y = nY;

                if ( pMinMax->ptMaxSize.x < nDX )
                    pMinMax->ptMaxSize.x = nDX;
                if ( pMinMax->ptMaxSize.y < nDY )
                    pMinMax->ptMaxSize.y = nDY;
                if ( pMinMax->ptMaxTrackSize.x < nDX )
                    pMinMax->ptMaxTrackSize.x = nDX;
                if ( pMinMax->ptMaxTrackSize.y < nDY )
                    pMinMax->ptMaxTrackSize.y = nDY;

                pMinMax->ptMinTrackSize.x = nDX;
                pMinMax->ptMinTrackSize.y = nDY;

                bRet = true;
            }

            if ( pFrame->mnMinWidth || pFrame->mnMinHeight )
            {
                int nWidth   = pFrame->mnMinWidth;
                int nHeight  = pFrame->mnMinHeight;

                ImplSalAddBorder( pFrame, nWidth, nHeight );

                if ( pMinMax->ptMinTrackSize.x < nWidth )
                     pMinMax->ptMinTrackSize.x = nWidth;
                if ( pMinMax->ptMinTrackSize.y < nHeight )
                     pMinMax->ptMinTrackSize.y = nHeight;
            }

            if ( pFrame->mnMaxWidth || pFrame->mnMaxHeight )
            {
                int nWidth   = pFrame->mnMaxWidth;
                int nHeight  = pFrame->mnMaxHeight;

                ImplSalAddBorder( pFrame, nWidth, nHeight );

                if( nWidth > 0 && nHeight > 0 ) // protect against int overflow due to INT_MAX initialisation
                {
                    if ( pMinMax->ptMaxTrackSize.x > nWidth )
                        pMinMax->ptMaxTrackSize.x = nWidth;
                    if ( pMinMax->ptMaxTrackSize.y > nHeight )
                        pMinMax->ptMaxTrackSize.y = nHeight;
                }
            }
        }

        ImplSalYieldMutexRelease();
    }

    return bRet;
}

// retrieves the SalMenuItem pointer from a hMenu
// the pointer is stored in every item, so if no position
// is specified we just use the first item (ie, pos=0)
// if bByPosition is false then nPos denotes a menu id instead of a position
static WinSalMenuItem* ImplGetSalMenuItem( HMENU hMenu, UINT nPos, bool bByPosition=true )
{
    MENUITEMINFOW mi = {};
    mi.cbSize = sizeof( mi );
    mi.fMask = MIIM_DATA;
    if( !GetMenuItemInfoW( hMenu, nPos, bByPosition, &mi) )
        SAL_WARN("vcl", "GetMenuItemInfoW failed: " << comphelper::WindowsErrorString(GetLastError()));

    return reinterpret_cast<WinSalMenuItem *>(mi.dwItemData);
}

// returns the index of the currently selected item if any or -1
static int ImplGetSelectedIndex( HMENU hMenu )
{
    MENUITEMINFOW mi = {};
    mi.cbSize = sizeof( mi );
    mi.fMask = MIIM_STATE;
    int n = GetMenuItemCount( hMenu );
    if( n != -1 )
    {
        for(int i=0; i<n; i++ )
        {
            if( !GetMenuItemInfoW( hMenu, i, TRUE, &mi) )
                SAL_WARN( "vcl", "GetMenuItemInfoW failed: " << comphelper::WindowsErrorString( GetLastError() ) );
            else
            {
                if( mi.fState & MFS_HILITE )
                    return i;
            }
        }
    }
    return -1;
}

static LRESULT ImplMenuChar( HWND, WPARAM wParam, LPARAM lParam )
{
    LRESULT nRet = MNC_IGNORE;
    HMENU hMenu = reinterpret_cast<HMENU>(lParam);
    OUString aMnemonic( "&" + OUStringChar(static_cast<sal_Unicode>(LOWORD(wParam))) );
    aMnemonic = aMnemonic.toAsciiLowerCase();   // we only have ascii mnemonics

    // search the mnemonic in the current menu
    int nItemCount = GetMenuItemCount( hMenu );
    int nFound = 0;
    int idxFound = -1;
    int idxSelected = ImplGetSelectedIndex( hMenu );
    int idx = idxSelected != -1 ? idxSelected+1 : 0;    // if duplicate mnemonics cycle through menu
    for( int i=0; i< nItemCount; i++, idx++ )
    {
        WinSalMenuItem* pSalMenuItem = ImplGetSalMenuItem( hMenu, idx % nItemCount );
        if( !pSalMenuItem )
            continue;
        OUString aStr = pSalMenuItem->mText;
        aStr = aStr.toAsciiLowerCase();
        if( aStr.indexOf( aMnemonic ) != -1 )
        {
            if( idxFound == -1 )
                idxFound = idx % nItemCount;
            if( nFound++ )
                break;  // duplicate found
        }
    }
    if( nFound == 1 )
        nRet = MAKELRESULT( idxFound, MNC_EXECUTE );
    else
        // duplicate mnemonics, just select the next occurrence
        nRet = MAKELRESULT( idxFound, MNC_SELECT );

    return nRet;
}

static LRESULT ImplMeasureItem( HWND hWnd, WPARAM wParam, LPARAM lParam )
{
    LRESULT nRet = 0;
    if( !wParam )
    {
        // request was sent by a menu
        nRet = 1;
        MEASUREITEMSTRUCT *pMI = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if( pMI->CtlType != ODT_MENU )
            return 0;

        WinSalMenuItem *pSalMenuItem = reinterpret_cast<WinSalMenuItem *>(pMI->itemData);
        if( !pSalMenuItem )
            return 0;

        HDC hdc = GetDC( hWnd );
        SIZE strSize;

        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof( ncm );
        SystemParametersInfoW( SPI_GETNONCLIENTMETRICS, 0, &ncm, 0 );

        // Assume every menu item can be default and printed bold
        //ncm.lfMenuFont.lfWeight = FW_BOLD;

        HFONT hfntOld = static_cast<HFONT>(SelectObject(hdc, CreateFontIndirectW( &ncm.lfMenuFont )));

        // menu text and accelerator
        OUString aStr(pSalMenuItem->mText);
        if( pSalMenuItem->mAccelText.getLength() )
        {
            aStr += " " + pSalMenuItem->mAccelText;
        }
        GetTextExtentPoint32W( hdc, o3tl::toW(aStr.getStr()),
                                aStr.getLength(), &strSize );

        // image
        Size bmpSize( 16, 16 );
        //if( pSalMenuItem->maBitmap )
        //    bmpSize = pSalMenuItem->maBitmap.GetSizePixel();

        // checkmark
        Size checkSize( GetSystemMetrics( SM_CXMENUCHECK ), GetSystemMetrics( SM_CYMENUCHECK ) );

        pMI->itemWidth = checkSize.Width() + 3 + bmpSize.Width() + 3 + strSize.cx;
        pMI->itemHeight = std::max( std::max( checkSize.Height(), bmpSize.Height() ), tools::Long(strSize.cy) );
        pMI->itemHeight += 4;

        DeleteObject( SelectObject(hdc, hfntOld) );
        ReleaseDC( hWnd, hdc );
    }

    return nRet;
}

static LRESULT ImplDrawItem(HWND, WPARAM wParam, LPARAM lParam )
{
    LRESULT nRet = 0;
    if( !wParam )
    {
        // request was sent by a menu
        nRet = 1;
        DRAWITEMSTRUCT *pDI = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if( pDI->CtlType != ODT_MENU )
            return 0;

        WinSalMenuItem *pSalMenuItem = reinterpret_cast<WinSalMenuItem *>(pDI->itemData);
        if( !pSalMenuItem )
            return 0;

        COLORREF clrPrevText, clrPrevBkgnd;
        HFONT hfntOld;
        HBRUSH hbrOld;
        bool    fChecked = (pDI->itemState & ODS_CHECKED);
        bool    fSelected = (pDI->itemState & ODS_SELECTED);
        bool    fDisabled = (pDI->itemState & (ODS_DISABLED | ODS_GRAYED));

        // Set the appropriate foreground and background colors.
        RECT aRect = pDI->rcItem;

        if ( fDisabled )
            clrPrevText = SetTextColor( pDI->hDC, GetSysColor( COLOR_GRAYTEXT ) );
        else
            clrPrevText = SetTextColor( pDI->hDC, GetSysColor( fSelected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT ) );

        DWORD colBackground = GetSysColor( fSelected ? COLOR_HIGHLIGHT : COLOR_MENU );
        clrPrevBkgnd = SetBkColor( pDI->hDC, colBackground );

        hbrOld = static_cast<HBRUSH>(SelectObject( pDI->hDC, CreateSolidBrush( GetBkColor( pDI->hDC ) ) ));

        // Fill background
        if(!PatBlt( pDI->hDC, aRect.left, aRect.top, aRect.right-aRect.left, aRect.bottom-aRect.top, PATCOPY ))
            SAL_WARN("vcl", "PatBlt failed: " << comphelper::WindowsErrorString(GetLastError()));

        int lineHeight = aRect.bottom-aRect.top;

        int x = aRect.left;
        int y = aRect.top;

        int checkWidth  = GetSystemMetrics( SM_CXMENUCHECK );
        int checkHeight = GetSystemMetrics( SM_CYMENUCHECK );
        if( fChecked )
        {
            RECT r;
            r.left = 0;
            r.top = 0;
            r.right = checkWidth;
            r.bottom = checkWidth;
            HDC memDC = CreateCompatibleDC( pDI->hDC );
            HBITMAP memBmp = CreateCompatibleBitmap( pDI->hDC, checkWidth, checkHeight );
            HBITMAP hOldBmp = static_cast<HBITMAP>(SelectObject( memDC, memBmp ));
            DrawFrameControl( memDC, &r, DFC_MENU, DFCS_MENUCHECK );
            BitBlt( pDI->hDC, x, y+(lineHeight-checkHeight)/2, checkWidth, checkHeight, memDC, 0, 0, SRCAND );
            DeleteObject( SelectObject( memDC, hOldBmp ) );
            DeleteDC( memDC );
        }
        x += checkWidth+3;

        //Size bmpSize = aBitmap.GetSizePixel();
        Size bmpSize(16, 16);
        if( !pSalMenuItem->maBitmap.IsEmpty() )
        {
            Bitmap aBitmap( pSalMenuItem->maBitmap );

            // set transparent pixels to background color
            if( fDisabled )
                colBackground = RGB(255,255,255);
            aBitmap.Replace( COL_LIGHTMAGENTA,
                Color( GetRValue(colBackground),GetGValue(colBackground),GetBValue(colBackground) ));

            WinSalBitmap* pSalBmp = static_cast<WinSalBitmap*>(aBitmap.ImplGetSalBitmap().get());
            HGLOBAL hDrawDIB = pSalBmp->ImplGethDIB();

            if( hDrawDIB )
            {
                if (PBITMAPINFO pBI = static_cast<PBITMAPINFO>(GlobalLock( hDrawDIB )))
                {
                    PBYTE               pBits = reinterpret_cast<PBYTE>(pBI) + pBI->bmiHeader.biSize +
                                                WinSalBitmap::ImplGetDIBColorCount( hDrawDIB ) * sizeof( RGBQUAD );

                    HBITMAP hBmp = CreateDIBitmap( pDI->hDC, &pBI->bmiHeader, CBM_INIT, pBits, pBI, DIB_RGB_COLORS );
                    GlobalUnlock( hDrawDIB );

                    HBRUSH hbrIcon = CreateSolidBrush( GetSysColor( COLOR_GRAYTEXT ) );
                    DrawStateW( pDI->hDC, hbrIcon, nullptr, reinterpret_cast<LPARAM>(hBmp), WPARAM(0),
                        x, y+(lineHeight-bmpSize.Height())/2, bmpSize.Width(), bmpSize.Height(),
                         DST_BITMAP | (fDisabled ? (fSelected ? DSS_MONO : DSS_DISABLED) : DSS_NORMAL) );

                    DeleteObject( hbrIcon );
                    DeleteObject( hBmp );
                }
            }

        }
        x += bmpSize.Width() + 3;
        aRect.left = x;

        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof( ncm );
        SystemParametersInfoW( SPI_GETNONCLIENTMETRICS, 0, &ncm, 0 );

        // Print default menu entry with bold font
        //if ( pDI->itemState & ODS_DEFAULT )
        //    ncm.lfMenuFont.lfWeight = FW_BOLD;

        hfntOld = static_cast<HFONT>(SelectObject(pDI->hDC, CreateFontIndirectW( &ncm.lfMenuFont )));

        SIZE strSize;
        OUString aStr( pSalMenuItem->mText );
        GetTextExtentPoint32W( pDI->hDC, o3tl::toW(aStr.getStr()),
                                aStr.getLength(), &strSize );

        if(!DrawStateW( pDI->hDC, nullptr, nullptr,
            reinterpret_cast<LPARAM>(aStr.getStr()),
            WPARAM(0), aRect.left, aRect.top + (lineHeight - strSize.cy)/2, 0, 0,
            DST_PREFIXTEXT | (fDisabled && !fSelected ? DSS_DISABLED : DSS_NORMAL) ) )
            SAL_WARN("vcl", "DrawStateW failed: " << comphelper::WindowsErrorString(GetLastError()));

        if( pSalMenuItem->mAccelText.getLength() )
        {
            SIZE strSizeA;
            aStr = pSalMenuItem->mAccelText;
            GetTextExtentPoint32W( pDI->hDC, o3tl::toW(aStr.getStr()),
                                    aStr.getLength(), &strSizeA );
            TEXTMETRICW tm;
            GetTextMetricsW( pDI->hDC, &tm );

            // position the accelerator string to the right but leave space for the
            // (potential) submenu arrow (tm.tmMaxCharWidth)
            if(!DrawStateW( pDI->hDC, nullptr, nullptr,
                reinterpret_cast<LPARAM>(aStr.getStr()),
                WPARAM(0), aRect.right-strSizeA.cx-tm.tmMaxCharWidth, aRect.top + (lineHeight - strSizeA.cy)/2, 0, 0,
                DST_TEXT | (fDisabled && !fSelected ? DSS_DISABLED : DSS_NORMAL) ) )
                SAL_WARN("vcl", "DrawStateW failed: " << comphelper::WindowsErrorString(GetLastError()));
        }

        // Restore the original font and colors.
        DeleteObject( SelectObject( pDI->hDC, hbrOld ) );
        DeleteObject( SelectObject( pDI->hDC, hfntOld) );
        SetTextColor(pDI->hDC, clrPrevText);
        SetBkColor(pDI->hDC, clrPrevBkgnd);
    }
    return nRet;
}

static bool ImplHandleMenuActivate( HWND hWnd, WPARAM wParam, LPARAM )
{
    // Menu activation
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return false;

    HMENU hMenu = reinterpret_cast<HMENU>(wParam);
    // WORD nPos = LOWORD (lParam);
    // bool bWindowMenu = (bool) HIWORD(lParam);

    // Send activate and deactivate together, so we have not keep track of opened menus
    // this will be enough to have the menus updated correctly
    SalMenuEvent aMenuEvt;
    WinSalMenuItem *pSalMenuItem = ImplGetSalMenuItem( hMenu, 0 );
    if( pSalMenuItem )
        aMenuEvt.mpMenu = pSalMenuItem->mpMenu;
    else
        aMenuEvt.mpMenu = nullptr;

    bool nRet = pFrame->CallCallback( SalEvent::MenuActivate, &aMenuEvt );
    if( nRet )
        nRet = pFrame->CallCallback( SalEvent::MenuDeactivate, &aMenuEvt );
    if( nRet )
        pFrame->mLastActivatedhMenu = hMenu;

    return nRet;
}

static bool ImplHandleMenuSelect( HWND hWnd, WPARAM wParam, LPARAM lParam )
{
    // Menu selection
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return false;

    WORD nId = LOWORD(wParam);      // menu item or submenu index
    WORD nFlags = HIWORD(wParam);
    HMENU hMenu = reinterpret_cast<HMENU>(lParam);

    // check if we have to process the message
    if( !GetSalData()->IsKnownMenuHandle( hMenu ) )
        return false;

    bool bByPosition = false;
    if( nFlags & MF_POPUP )
        bByPosition = true;

    bool nRet = false;
    if ( hMenu && !pFrame->mLastActivatedhMenu )
    {
        // we never activated a menu (ie, no WM_INITMENUPOPUP has occurred yet)
        // which means this must be the menubar -> send activation/deactivation
        SalMenuEvent aMenuEvt;
        WinSalMenuItem *pSalMenuItem = ImplGetSalMenuItem( hMenu, nId, bByPosition );
        if( pSalMenuItem )
            aMenuEvt.mpMenu = pSalMenuItem->mpMenu;
        else
            aMenuEvt.mpMenu = nullptr;

        nRet = pFrame->CallCallback( SalEvent::MenuActivate, &aMenuEvt );
        if( nRet )
            nRet = pFrame->CallCallback( SalEvent::MenuDeactivate, &aMenuEvt );
        if( nRet )
            pFrame->mLastActivatedhMenu = hMenu;
    }

    if( !hMenu && nFlags == 0xFFFF )
    {
        // all menus are closed, reset activation logic
        pFrame->mLastActivatedhMenu = nullptr;
    }

    if( hMenu )
    {
        // hMenu must be saved, as it is not passed in WM_COMMAND which always occurs after a selection
        // if a menu is closed due to a command selection then hMenu is NULL, but WM_COMMAND comes later
        // so we must not overwrite it in this case
        pFrame->mSelectedhMenu = hMenu;

        // send highlight event
        if( nFlags & MF_POPUP )
        {
            // submenu selected
            // wParam now carries an index instead of an id -> retrieve id
            MENUITEMINFOW mi = {};
            mi.cbSize = sizeof( mi );
            mi.fMask = MIIM_ID;
            if( GetMenuItemInfoW( hMenu, LOWORD(wParam), TRUE, &mi) )
                nId = sal::static_int_cast<WORD>(mi.wID);
        }

        SalMenuEvent aMenuEvt;
        aMenuEvt.mnId   = nId;
        WinSalMenuItem *pSalMenuItem = ImplGetSalMenuItem( hMenu, nId, false );
        if( pSalMenuItem )
            aMenuEvt.mpMenu = pSalMenuItem->mpMenu;
        else
            aMenuEvt.mpMenu = nullptr;

        nRet = pFrame->CallCallback( SalEvent::MenuHighlight, &aMenuEvt );
    }

    return nRet;
}

static bool ImplHandleCommand( HWND hWnd, WPARAM wParam, LPARAM )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return false;

    bool nRet = false;
    if( !HIWORD(wParam) )
    {
        // Menu command
        WORD nId = LOWORD(wParam);
        if( nId )   // zero for separators
        {
            SalMenuEvent aMenuEvt;
            aMenuEvt.mnId   = nId;
            WinSalMenuItem *pSalMenuItem = ImplGetSalMenuItem( pFrame->mSelectedhMenu, nId, false );
            if( pSalMenuItem )
                aMenuEvt.mpMenu = pSalMenuItem->mpMenu;
            else
                aMenuEvt.mpMenu = nullptr;

            nRet = pFrame->CallCallback( SalEvent::MenuCommand, &aMenuEvt );
        }
    }
    return nRet;
}

static bool ImplHandleSysCommand( HWND hWnd, WPARAM wParam, LPARAM lParam )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( !pFrame )
        return false;

    WPARAM nCommand = wParam & 0xFFF0;

    if (pFrame->isFullScreen())
    {
        bool    bMaximize = IsZoomed( pFrame->mhWnd );
        bool    bMinimize = IsIconic( pFrame->mhWnd );
        if ( (nCommand == SC_SIZE) ||
             (!bMinimize && (nCommand == SC_MOVE)) ||
             (!bMaximize && (nCommand == SC_MAXIMIZE)) ||
             (bMaximize && (nCommand == SC_RESTORE)) )
        {
            return true;
        }
    }

    if ( nCommand == SC_MOVE )
    {
        WinSalTimer* pTimer = static_cast<WinSalTimer*>( ImplGetSVData()->maSchedCtx.mpSalTimer );
        if ( pTimer )
            pTimer->SetForceRealTimer( true );
    }

    if ( nCommand == SC_KEYMENU )
    {
        // do not process SC_KEYMENU if we have a native menu
        // Windows should handle this
        if( GetMenu( hWnd ) )
            return false;

        // Process here KeyMenu events only for Alt to activate the MenuBar,
        // or if a SysChild window is in focus, as Alt-key-combinations are
        // only processed via this event
        if ( !LOWORD( lParam ) )
        {
            // Only trigger if no other key is pressed.
            // Contrary to Docu the CharCode is delivered with the x-coordinate
            // that is pressed in addition.
            // Also 32 for space, 99 for c, 100 for d, ...
            // As this is not documented, we check the state of the space-bar
            if ( GetKeyState( VK_SPACE ) & 0x8000 )
                return false;

            // to avoid activating the MenuBar for Alt+MouseKey
            if ( (GetKeyState( VK_LBUTTON ) & 0x8000) ||
                 (GetKeyState( VK_RBUTTON ) & 0x8000) ||
                 (GetKeyState( VK_MBUTTON ) & 0x8000) ||
                 (GetKeyState( VK_SHIFT )   & 0x8000) )
                return true;

            SalKeyEvent aKeyEvt;
            aKeyEvt.mnCode      = KEY_MENU;
            aKeyEvt.mnCharCode  = 0;
            aKeyEvt.mnRepeat    = 0;
            bool nRet = pFrame->CallCallback( SalEvent::KeyInput, &aKeyEvt );
            pFrame->CallCallback( SalEvent::KeyUp, &aKeyEvt );
            return nRet;
        }
        else
        {
            // check if a SysChild is in focus
            HWND hFocusWnd = ::GetFocus();
            if ( hFocusWnd && ImplFindSalObject( hFocusWnd ) )
            {
                char cKeyCode = static_cast<char>(static_cast<unsigned char>(LOWORD( lParam )));
                // LowerCase
                if ( (cKeyCode >= 65) && (cKeyCode <= 90) )
                    cKeyCode += 32;
                // We only accept 0-9 and A-Z; all other keys have to be
                // processed by the SalObj hook
                if ( ((cKeyCode >= 48) && (cKeyCode <= 57)) ||
                     ((cKeyCode >= 97) && (cKeyCode <= 122)) )
                {
                    sal_uInt16 nModCode = 0;
                    if ( GetKeyState( VK_SHIFT ) & 0x8000 )
                        nModCode |= KEY_SHIFT;
                    if ( GetKeyState( VK_CONTROL ) & 0x8000 )
                        nModCode |= KEY_MOD1;
                    nModCode |= KEY_MOD2;

                    SalKeyEvent aKeyEvt;
                    if ( (cKeyCode >= 48) && (cKeyCode <= 57) )
                        aKeyEvt.mnCode = KEY_0+(cKeyCode-48);
                    else
                        aKeyEvt.mnCode = KEY_A+(cKeyCode-97);
                    aKeyEvt.mnCode     |= nModCode;
                    aKeyEvt.mnCharCode  = cKeyCode;
                    aKeyEvt.mnRepeat    = 0;
                    bool nRet = pFrame->CallCallback( SalEvent::KeyInput, &aKeyEvt );
                    pFrame->CallCallback( SalEvent::KeyUp, &aKeyEvt );
                    return nRet;
                }
            }
        }
    }

    return false;
}

static void ImplHandleInputLangChange( HWND hWnd, WPARAM, LPARAM lParam )
{
    ImplSalYieldMutexAcquireWithWait();

    // check if we support IME
    WinSalFrame* pFrame = GetWindowPtr( hWnd );

    if ( !pFrame )
        return;

    if ( pFrame->mbIME && pFrame->mhDefIMEContext )
    {
        HKL     hKL = reinterpret_cast<HKL>(lParam);
        UINT    nImeProps = ImmGetProperty( hKL, IGP_PROPERTY );

        pFrame->mbSpezIME = (nImeProps & IME_PROP_SPECIAL_UI) != 0;
        pFrame->mbAtCursorIME = (nImeProps & IME_PROP_AT_CARET) != 0;
        pFrame->mbHandleIME = !pFrame->mbSpezIME;
    }

    // trigger input language and codepage update
    UINT nLang = pFrame->mnInputLang;
    ImplUpdateInputLang( pFrame );

    // notify change
    if( nLang != pFrame->mnInputLang )
        pFrame->CallCallback( SalEvent::InputLanguageChange, nullptr );

    // reinit spec. keys
    GetSalData()->initKeyCodeMap();

    ImplSalYieldMutexRelease();
}

static void ImplUpdateIMECursorPos( WinSalFrame* pFrame, HIMC hIMC )
{
    COMPOSITIONFORM aForm = {};

    // get cursor position and from it calculate default position
    // for the composition window
    SalExtTextInputPosEvent aPosEvt;
    pFrame->CallCallback( SalEvent::ExtTextInputPos, &aPosEvt );
    if ( (aPosEvt.mnX == -1) && (aPosEvt.mnY == -1) )
        aForm.dwStyle |= CFS_DEFAULT;
    else
    {
        aForm.dwStyle          |= CFS_POINT;
        aForm.ptCurrentPos.x    = aPosEvt.mnX;
        aForm.ptCurrentPos.y    = aPosEvt.mnY;
    }
    ImmSetCompositionWindow( hIMC, &aForm );

    // Because not all IME's use this values, we create
    // a Windows caret to force the Position from the IME
    if ( GetFocus() == pFrame->mhWnd )
    {
        CreateCaret( pFrame->mhWnd, nullptr,
                     aPosEvt.mnWidth, aPosEvt.mnHeight );
        SetCaretPos( aPosEvt.mnX, aPosEvt.mnY );
    }
}

static bool ImplHandleIMEStartComposition( HWND hWnd )
{
    bool bDef = true;

    ImplSalYieldMutexAcquireWithWait();

    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( pFrame )
    {
        HIMC hIMC = ImmGetContext( hWnd );
        if ( hIMC )
        {
            ImplUpdateIMECursorPos( pFrame, hIMC );
            ImmReleaseContext( hWnd, hIMC );
        }

        if ( pFrame->mbHandleIME )
        {
            if ( pFrame->mbAtCursorIME )
                bDef = false;
        }
    }

    ImplSalYieldMutexRelease();

    return bDef;
}

static bool ImplHandleIMECompositionInput( WinSalFrame* pFrame,
                                               HIMC hIMC, LPARAM lParam )
{
    bool bDef = true;

    // Init Event
    SalExtTextInputEvent    aEvt;
    aEvt.mpTextAttr         = nullptr;
    aEvt.mnCursorPos        = 0;
    aEvt.mnCursorFlags      = 0;

    // If we get a result string, then we handle this input
    if ( lParam & GCS_RESULTSTR )
    {
        bDef = false;

        LONG nTextLen = ImmGetCompositionStringW( hIMC, GCS_RESULTSTR, nullptr, 0 ) / sizeof( WCHAR );
        if ( nTextLen >= 0 )
        {
            auto pTextBuf = std::make_unique<WCHAR[]>(nTextLen);
            ImmGetCompositionStringW( hIMC, GCS_RESULTSTR, pTextBuf.get(), nTextLen*sizeof( WCHAR ) );
            aEvt.maText = OUString( o3tl::toU(pTextBuf.get()), static_cast<sal_Int32>(nTextLen) );
        }

        aEvt.mnCursorPos = aEvt.maText.getLength();
        pFrame->CallCallback( SalEvent::ExtTextInput, &aEvt );
        pFrame->CallCallback( SalEvent::EndExtTextInput, nullptr );
        ImplUpdateIMECursorPos( pFrame, hIMC );
    }

    // If the IME doesn't support OnSpot input, then there is nothing to do
    if ( !pFrame->mbAtCursorIME )
        return !bDef;

    // If we get new Composition data, then we handle this new input
    if ( (lParam & (GCS_COMPSTR | GCS_COMPATTR)) ||
         ((lParam & GCS_CURSORPOS) && !(lParam & GCS_RESULTSTR)) )
    {
        bDef = false;

        std::unique_ptr<ExtTextInputAttr[]> pSalAttrAry;
        LONG    nTextLen = ImmGetCompositionStringW( hIMC, GCS_COMPSTR, nullptr, 0 ) / sizeof( WCHAR );
        if ( nTextLen > 0 )
        {
            {
                auto pTextBuf = std::make_unique<WCHAR[]>(nTextLen);
                ImmGetCompositionStringW( hIMC, GCS_COMPSTR, pTextBuf.get(), nTextLen*sizeof( WCHAR ) );
                aEvt.maText = OUString( o3tl::toU(pTextBuf.get()), static_cast<sal_Int32>(nTextLen) );
            }

            std::unique_ptr<BYTE[]> pAttrBuf;
            LONG        nAttrLen = ImmGetCompositionStringW( hIMC, GCS_COMPATTR, nullptr, 0 );
            if ( nAttrLen > 0 )
            {
                pAttrBuf.reset(new BYTE[nAttrLen]);
                ImmGetCompositionStringW( hIMC, GCS_COMPATTR, pAttrBuf.get(), nAttrLen );
            }

            if ( pAttrBuf )
            {
                sal_Int32 nTextLen2 = aEvt.maText.getLength();
                pSalAttrAry.reset(new ExtTextInputAttr[nTextLen2]);
                memset( pSalAttrAry.get(), 0, nTextLen2*sizeof( sal_uInt16 ) );
                for( sal_Int32 i = 0; (i < nTextLen2) && (i < nAttrLen); i++ )
                {
                    BYTE nWinAttr = pAttrBuf.get()[i];
                    ExtTextInputAttr   nSalAttr;
                    if ( nWinAttr == ATTR_TARGET_CONVERTED )
                    {
                        nSalAttr = ExtTextInputAttr::BoldUnderline;
                        aEvt.mnCursorFlags |= EXTTEXTINPUT_CURSOR_INVISIBLE;
                    }
                    else if ( nWinAttr == ATTR_CONVERTED )
                        nSalAttr = ExtTextInputAttr::DashDotUnderline;
                    else if ( nWinAttr == ATTR_TARGET_NOTCONVERTED )
                        nSalAttr = ExtTextInputAttr::Highlight;
                    else if ( nWinAttr == ATTR_INPUT_ERROR )
                        nSalAttr = ExtTextInputAttr::RedText | ExtTextInputAttr::DottedUnderline;
                    else /* ( nWinAttr == ATTR_INPUT ) */
                        nSalAttr = ExtTextInputAttr::DottedUnderline;
                    pSalAttrAry[i] = nSalAttr;
                }

                aEvt.mpTextAttr = pSalAttrAry.get();
            }
        }

        // Only when we get new composition data, we must send this event
        if ( (nTextLen > 0) || !(lParam & GCS_RESULTSTR) )
        {
            // End the mode, if the last character is deleted
            if ( !nTextLen )
            {
                pFrame->CallCallback( SalEvent::ExtTextInput, &aEvt );
                pFrame->CallCallback( SalEvent::EndExtTextInput, nullptr );
            }
            else
            {
                // Because Cursor-Position and DeltaStart never updated
                // from the korean input engine, we must handle this here
                if ( lParam & CS_INSERTCHAR )
                {
                    aEvt.mnCursorPos = nTextLen;
                    if ( aEvt.mnCursorPos && (lParam & CS_NOMOVECARET) )
                        aEvt.mnCursorPos--;
                }
                else
                    aEvt.mnCursorPos = LOWORD( ImmGetCompositionStringW( hIMC, GCS_CURSORPOS, nullptr, 0 ) );

                if ( pFrame->mbCandidateMode )
                    aEvt.mnCursorFlags |= EXTTEXTINPUT_CURSOR_INVISIBLE;
                if ( lParam & CS_NOMOVECARET )
                    aEvt.mnCursorFlags |= EXTTEXTINPUT_CURSOR_OVERWRITE;

                pFrame->CallCallback( SalEvent::ExtTextInput, &aEvt );
            }
            ImplUpdateIMECursorPos( pFrame, hIMC );
        }
    }

    return !bDef;
}

static bool ImplHandleIMEComposition( HWND hWnd, LPARAM lParam )
{
    bool bDef = true;
    ImplSalYieldMutexAcquireWithWait();

    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( pFrame && (!lParam || (lParam & GCS_RESULTSTR)) )
    {
        // reset the background mode for each text input,
        // as some tools such as RichWin may have changed it
        if ( pFrame->mpLocalGraphics &&
             pFrame->mpLocalGraphics->getHDC() )
            SetBkMode( pFrame->mpLocalGraphics->getHDC(), TRANSPARENT );
    }

    if ( pFrame && pFrame->mbHandleIME )
    {
        if ( !lParam )
        {
            SalExtTextInputEvent aEvt;
            aEvt.mpTextAttr         = nullptr;
            aEvt.mnCursorPos        = 0;
            aEvt.mnCursorFlags      = 0;
            pFrame->CallCallback( SalEvent::ExtTextInput, &aEvt );
            pFrame->CallCallback( SalEvent::EndExtTextInput, nullptr );
        }
        else if ( lParam & (GCS_RESULTSTR | GCS_COMPSTR | GCS_COMPATTR | GCS_CURSORPOS) )
        {
            HIMC hIMC = ImmGetContext( hWnd );
            if ( hIMC )
            {
                if ( ImplHandleIMECompositionInput( pFrame, hIMC, lParam ) )
                    bDef = false;

                ImmReleaseContext( hWnd, hIMC );
            }
        }
    }

    ImplSalYieldMutexRelease();
    return bDef;
}

static bool ImplHandleIMEEndComposition( HWND hWnd )
{
    bool bDef = true;

    ImplSalYieldMutexAcquireWithWait();

    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    if ( pFrame && pFrame->mbHandleIME )
    {
        if ( pFrame->mbAtCursorIME )
        {
            pFrame->mbCandidateMode = false;
            bDef = false;
        }

        // tdf#155158: Windows IMEs do not necessarily send a composition message if they are
        // dismissed during composition (for example, by an input method/language change).
        // tdf#167740: Also clear the candidate text when the IME is dismissed.
        SalExtTextInputEvent aEvt;
        aEvt.mpTextAttr = nullptr;
        aEvt.mnCursorPos = 0;
        aEvt.mnCursorFlags = 0;
        pFrame->CallCallback(SalEvent::ExtTextInput, &aEvt);
        pFrame->CallCallback(SalEvent::EndExtTextInput, nullptr);
    }

    ImplSalYieldMutexRelease();

    return bDef;
}

static bool ImplHandleAppCommand( HWND hWnd, LPARAM lParam, LRESULT & nRet )
{
    MediaCommand nCommand;
    switch( GET_APPCOMMAND_LPARAM(lParam) )
    {
    case APPCOMMAND_MEDIA_CHANNEL_DOWN:         nCommand = MediaCommand::ChannelDown; break;
    case APPCOMMAND_MEDIA_CHANNEL_UP:           nCommand = MediaCommand::ChannelUp; break;
    case APPCOMMAND_MEDIA_NEXTTRACK:            nCommand = MediaCommand::NextTrack; break;
    case APPCOMMAND_MEDIA_PAUSE:                nCommand = MediaCommand::Pause; break;
    case APPCOMMAND_MEDIA_PLAY:                 nCommand = MediaCommand::Play; break;
    case APPCOMMAND_MEDIA_PLAY_PAUSE:           nCommand = MediaCommand::PlayPause; break;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK:        nCommand = MediaCommand::PreviousTrack; break;
    case APPCOMMAND_MEDIA_RECORD:               nCommand = MediaCommand::Record; break;
    case APPCOMMAND_MEDIA_REWIND:               nCommand = MediaCommand::Rewind; break;
    case APPCOMMAND_MEDIA_STOP:                 nCommand = MediaCommand::Stop; break;
    case APPCOMMAND_MIC_ON_OFF_TOGGLE:          nCommand = MediaCommand::MicOnOffToggle; break;
    case APPCOMMAND_MICROPHONE_VOLUME_DOWN:     nCommand = MediaCommand::MicrophoneVolumeDown; break;
    case APPCOMMAND_MICROPHONE_VOLUME_MUTE:     nCommand = MediaCommand::MicrophoneVolumeMute; break;
    case APPCOMMAND_MICROPHONE_VOLUME_UP:       nCommand = MediaCommand::MicrophoneVolumeUp; break;
    case APPCOMMAND_VOLUME_DOWN:                nCommand = MediaCommand::VolumeDown; break;
    case APPCOMMAND_VOLUME_MUTE:                nCommand = MediaCommand::VolumeMute; break;
    case APPCOMMAND_VOLUME_UP:                  nCommand = MediaCommand::VolumeUp; break;
    default:
        return false;
    }

    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    vcl::Window *pWindow = pFrame ? pFrame->GetWindow() : nullptr;

    if( pWindow )
    {
        const Point aPoint;
        CommandMediaData aMediaData(nCommand);
        CommandEvent aCEvt( aPoint, CommandEventId::Media, false, &aMediaData );
        NotifyEvent aNCmdEvt( NotifyEventType::COMMAND, pWindow, &aCEvt );

        if ( !ImplCallPreNotify( aNCmdEvt ) )
        {
            pWindow->Command( aCEvt );
            nRet = 1;
            return !aMediaData.GetPassThroughToOS();
        }
    }

    return false;
}

static void ImplHandleIMENotify( HWND hWnd, WPARAM wParam )
{
    if ( wParam == WPARAM(IMN_OPENCANDIDATE) )
    {
        ImplSalYieldMutexAcquireWithWait();

        WinSalFrame* pFrame = GetWindowPtr( hWnd );
        if ( pFrame && pFrame->mbHandleIME &&
             pFrame->mbAtCursorIME )
        {
            // we want to hide the cursor
            pFrame->mbCandidateMode = true;
            ImplHandleIMEComposition( hWnd, GCS_CURSORPOS );

            HWND hWnd2 = pFrame->mhWnd;
            HIMC hIMC = ImmGetContext( hWnd2 );
            if ( hIMC )
            {
                LONG nBufLen = ImmGetCompositionStringW( hIMC, GCS_COMPSTR, nullptr, 0 );
                if ( nBufLen >= 1 )
                {
                    SalExtTextInputPosEvent aPosEvt;
                    pFrame->CallCallback( SalEvent::ExtTextInputPos, &aPosEvt );

                    // Vertical !!!
                    CANDIDATEFORM aForm;
                    aForm.dwIndex           = 0;
                    aForm.dwStyle           = CFS_EXCLUDE;
                    aForm.ptCurrentPos.x    = aPosEvt.mnX;
                    aForm.ptCurrentPos.y    = aPosEvt.mnY+1;
                    aForm.rcArea.left       = aPosEvt.mnX;
                    aForm.rcArea.top        = aPosEvt.mnY;
                    aForm.rcArea.right      = aForm.rcArea.left+aPosEvt.mnExtWidth+1;
                    aForm.rcArea.bottom     = aForm.rcArea.top+aPosEvt.mnHeight+1;
                    ImmSetCandidateWindow( hIMC, &aForm );
                }

                ImmReleaseContext( hWnd2, hIMC );
            }
        }

        ImplSalYieldMutexRelease();
    }
    else if ( wParam == WPARAM(IMN_CLOSECANDIDATE) )
    {
        ImplSalYieldMutexAcquireWithWait();
        WinSalFrame* pFrame = GetWindowPtr( hWnd );
        if ( pFrame )
            pFrame->mbCandidateMode = false;
        ImplSalYieldMutexRelease();
    }
}

static bool
ImplHandleGetObject(HWND hWnd, LPARAM lParam, WPARAM wParam, LRESULT & nRet)
{
    static const bool disable = []
    {
        const char* pEnv = getenv("SAL_ACCESSIBILITY_ENABLED");
        return pEnv && pEnv[0] == '0';
    }();
    if (disable)
        return false;
    uno::Reference<accessibility::XMSAAService> xMSAA;
    if (ImplSalYieldMutexTryToAcquire())
    {
        ImplSVData* pSVData = ImplGetSVData();

        // Make sure to launch Accessibility only the following criteria are satisfied
        // to avoid RFT interrupts regular accessibility processing
        if ( !pSVData->mxAccessBridge.is() )
        {
            css::uno::Reference<XComponentContext> xContext(comphelper::getProcessComponentContext());
            try
            {
                pSVData->mxAccessBridge = css::accessibility::MSAAService::create(xContext);
                SAL_INFO("vcl", "got IAccessible2 bridge");
            }
            catch (css::uno::DeploymentException&)
            {
                TOOLS_WARN_EXCEPTION("vcl", "got no IAccessible2 bridge");
                assert(false && "failed to create IAccessible2 bridge");
            }
        }
        xMSAA.set(pSVData->mxAccessBridge, uno::UNO_QUERY);
        ImplSalYieldMutexRelease();
    }
    else
    {   // tdf#155794: access without locking: hopefully this should be fine
        // as WM_GETOBJECT is received only on the main thread and by the time in
        // VCL shutdown when ImplSvData dies there should not be Windows any
        // more that could receive messages.
        xMSAA.set(ImplGetSVData()->mxAccessBridge, uno::UNO_QUERY);
    }

    if ( xMSAA.is() )
    {
        sal_Int32 lParam32 = static_cast<sal_Int32>(lParam);
        sal_uInt32 wParam32 = static_cast<sal_uInt32>(wParam);

        // mhOnSetTitleWnd not set to reasonable value anywhere...
        if ( lParam32 == OBJID_CLIENT )
        {
            nRet = xMSAA->getAccObjectPtr(
                    reinterpret_cast<sal_Int64>(hWnd), lParam32, wParam32);
            if (nRet != 0)
                return true;
        }
    }
    return false;
}

static LRESULT ImplHandleIMEReconvertString( HWND hWnd, LPARAM lParam )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    LPRECONVERTSTRING pReconvertString = reinterpret_cast<LPRECONVERTSTRING>(lParam);
    LRESULT nRet = 0;
    SalSurroundingTextRequestEvent aEvt;
    aEvt.maText.clear();
    aEvt.mnStart = aEvt.mnEnd = 0;

    UINT nImeProps = ImmGetProperty( GetKeyboardLayout( 0 ), IGP_SETCOMPSTR );
    if( (nImeProps & SCS_CAP_SETRECONVERTSTRING) == 0 )
    {
        // This IME does not support reconversion.
        return 0;
    }

    if( !pReconvertString )
    {
        // The first call for reconversion.
        pFrame->CallCallback( SalEvent::StartReconversion, nullptr );

        // Retrieve the surrounding text from the focused control.
        pFrame->CallCallback( SalEvent::SurroundingTextRequest, &aEvt );

        if( aEvt.maText.isEmpty())
        {
            return 0;
        }

        nRet = sizeof(RECONVERTSTRING) + (aEvt.maText.getLength() + 1) * sizeof(WCHAR);
    }
    else
    {
        // The second call for reconversion.

        // Retrieve the surrounding text from the focused control.
        pFrame->CallCallback( SalEvent::SurroundingTextRequest, &aEvt );
        nRet = sizeof(RECONVERTSTRING) + (aEvt.maText.getLength() + 1) * sizeof(WCHAR);

        pReconvertString->dwStrOffset = sizeof(RECONVERTSTRING);
        pReconvertString->dwStrLen = aEvt.maText.getLength();
        pReconvertString->dwCompStrOffset = aEvt.mnStart * sizeof(WCHAR);
        pReconvertString->dwCompStrLen = aEvt.mnEnd - aEvt.mnStart;
        pReconvertString->dwTargetStrOffset = pReconvertString->dwCompStrOffset;
        pReconvertString->dwTargetStrLen = pReconvertString->dwCompStrLen;

        memcpy( pReconvertString + 1, aEvt.maText.getStr(), (aEvt.maText.getLength() + 1) * sizeof(WCHAR) );
    }

    // just return the required size of buffer to reconvert.
    return nRet;
}

static LRESULT ImplHandleIMEConfirmReconvertString( HWND hWnd, LPARAM lParam )
{
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    LPRECONVERTSTRING pReconvertString = reinterpret_cast<LPRECONVERTSTRING>(lParam);
    SalSurroundingTextRequestEvent aEvt;
    aEvt.maText.clear();
    aEvt.mnStart = aEvt.mnEnd = 0;

    pFrame->CallCallback( SalEvent::SurroundingTextRequest, &aEvt );

    sal_uLong nTmpStart = pReconvertString->dwCompStrOffset / sizeof(WCHAR);
    sal_uLong nTmpEnd = nTmpStart + pReconvertString->dwCompStrLen;

    if( nTmpStart != aEvt.mnStart || nTmpEnd != aEvt.mnEnd )
    {
        SalSurroundingTextSelectionChangeEvent aSelEvt { nTmpStart, nTmpEnd };
        pFrame->CallCallback( SalEvent::SurroundingTextSelectionChange, &aSelEvt );
    }

    return TRUE;
}

static LRESULT ImplHandleIMEQueryCharPosition( HWND hWnd, LPARAM lParam ) {
    WinSalFrame* pFrame = GetWindowPtr( hWnd );
    PIMECHARPOSITION pQueryCharPosition = reinterpret_cast<PIMECHARPOSITION>(lParam);
    if ( pQueryCharPosition->dwSize < sizeof(IMECHARPOSITION) )
        return FALSE;

    SalQueryCharPositionEvent aEvt;
    aEvt.mbValid = false;
    aEvt.mnCharPos = pQueryCharPosition->dwCharPos;

    pFrame->CallCallback( SalEvent::QueryCharPosition, &aEvt );

    if ( !aEvt.mbValid )
        return FALSE;

    if ( aEvt.mbVertical )
    {
        // For vertical writing, the base line is left edge of the rectangle
        // and the target position is top-right corner.
        pQueryCharPosition->pt.x = aEvt.maCursorBound.getX() + aEvt.maCursorBound.GetWidth();
        pQueryCharPosition->pt.y = aEvt.maCursorBound.getY();
        pQueryCharPosition->cLineHeight = aEvt.maCursorBound.GetWidth();
    }
    else
    {
        // For horizontal writing, the base line is the bottom edge of the rectangle.
        // and the target position is top-left corner.
        pQueryCharPosition->pt.x = aEvt.maCursorBound.getX();
        pQueryCharPosition->pt.y = aEvt.maCursorBound.getY();
        pQueryCharPosition->cLineHeight = aEvt.maCursorBound.GetHeight();
    }

    // Currently not supported but many IMEs usually ignore them.
    pQueryCharPosition->rcDocument.left = 0;
    pQueryCharPosition->rcDocument.top = 0;
    pQueryCharPosition->rcDocument.right = 0;
    pQueryCharPosition->rcDocument.bottom = 0;

    return TRUE;
}

void SalTestMouseLeave()
{
    SalData* pSalData = GetSalData();

    if ( pSalData->mhWantLeaveMsg && !::GetCapture() )
    {
        POINT aPt;
        GetCursorPos( &aPt );

        // a one item cache, because this function is sometimes hot - if the cursor has not moved, then
        // no need to call WindowFromPoint
        static POINT cachedPoint;
        if (cachedPoint.x == aPt.x && cachedPoint.y == aPt.y)
            return;
        cachedPoint = aPt;

        if ( pSalData->mhWantLeaveMsg != WindowFromPoint( aPt ) )
            SendMessageW( pSalData->mhWantLeaveMsg, SAL_MSG_MOUSELEAVE, 0, MAKELPARAM( aPt.x, aPt.y ) );
    }
}

static bool ImplSalWheelMousePos( HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam ,
                                 LRESULT& rResult )
{
    POINT aPt;
    POINT aScreenPt;
    aScreenPt.x = static_cast<short>(LOWORD( lParam ));
    aScreenPt.y = static_cast<short>(HIWORD( lParam ));
    // find child window that is at this position
    HWND hChildWnd;
    HWND hWheelWnd = hWnd;
    do
    {
        hChildWnd = hWheelWnd;
        aPt = aScreenPt;
        ScreenToClient( hChildWnd, &aPt );
        hWheelWnd = ChildWindowFromPointEx( hChildWnd, aPt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT );
    }
    while ( hWheelWnd && (hWheelWnd != hChildWnd) );
    if ( hWheelWnd && (hWheelWnd != hWnd) &&
         (hWheelWnd != ::GetFocus()) && IsWindowEnabled( hWheelWnd ) )
    {
        rResult = SendMessageW( hWheelWnd, nMsg, wParam, lParam );
        return false;
    }

    return true;
}

static bool ImplHandleGestureMsg(HWND hWnd, LPARAM lParam)
{
    ImplSalYieldMutexAcquireWithWait();

    bool nRet = false;

    WinSalFrame* pFrame = GetWindowPtr(hWnd);
    if (pFrame)
    {
        GESTUREINFO gi;
        ZeroMemory(&gi, sizeof(GESTUREINFO));
        gi.cbSize = sizeof(GESTUREINFO);

        BOOL result = GetGestureInfo((HGESTUREINFO)lParam, &gi);
        if (!result)
            return nRet;

        switch (gi.dwID)
        {
            case GID_PAN:
            {
                SalGestureEvent aEvent;
                POINT aPt(gi.ptsLocation.x, gi.ptsLocation.y);
                ScreenToClient(hWnd, &aPt);
                aEvent.mnX = aPt.x;
                aEvent.mnY = aPt.y;

                if (gi.dwFlags & GF_BEGIN)
                {
                    pFrame->maFirstPanGesturePt.x = gi.ptsLocation.x;
                    pFrame->maFirstPanGesturePt.y = gi.ptsLocation.y;
                    aEvent.meEventType = GestureEventPanType::Begin;
                }
                else if (gi.dwFlags & GF_END)
                    aEvent.meEventType = GestureEventPanType::End;
                else
                {
                    POINT aFirstPt(pFrame->maFirstPanGesturePt.x, pFrame->maFirstPanGesturePt.y);
                    POINT aSecondPt(gi.ptsLocation.x, gi.ptsLocation.y);
                    tools::Long deltaX = (aSecondPt.x - aFirstPt.x);
                    tools::Long deltaY = (aSecondPt.y - aFirstPt.y);

                    if (std::abs(deltaX) > std::abs(deltaY))
                    {
                        aEvent.mfOffset = aSecondPt.x - aFirstPt.x;
                        aEvent.meOrientation = PanningOrientation::Horizontal;
                    }
                    else
                    {
                        aEvent.mfOffset = aSecondPt.y - aFirstPt.y;
                        aEvent.meOrientation = PanningOrientation::Vertical;
                    }

                    aEvent.meEventType = GestureEventPanType::Update;
                }
                nRet = pFrame->CallCallback(SalEvent::GesturePan, &aEvent);
            }
            break;

            case GID_ZOOM:
            {
                SalGestureZoomEvent aEvent;
                POINT aPt(gi.ptsLocation.x, gi.ptsLocation.y);
                ScreenToClient(hWnd, &aPt);
                aEvent.mnX = aPt.x;
                aEvent.mnY = aPt.y;
                aEvent.mfScaleDelta = gi.ullArguments;

                if (gi.dwFlags & GF_BEGIN)
                    aEvent.meEventType = GestureEventZoomType::Begin;
                else if (gi.dwFlags & GF_END)
                    aEvent.meEventType = GestureEventZoomType::End;
                else
                    aEvent.meEventType = GestureEventZoomType::Update;

                nRet = pFrame->CallCallback(SalEvent::GestureZoom, &aEvent);
            }
            break;
        }
        CloseGestureInfoHandle((HGESTUREINFO)lParam);
    }

    ImplSalYieldMutexRelease();

    return nRet;
}

static LRESULT CALLBACK SalFrameWndProc( HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam, bool& rDef )
{
    LRESULT     nRet = 0;
    static bool  bInWheelMsg = false;
    static bool  bInQueryEnd = false;

    SAL_INFO("vcl.gdi.wndproc", "SalFrameWndProc(nMsg=" << nMsg << ", wParam=" << wParam << ", lParam=" << lParam << ")");

    // By WM_CREATE we connect the frame with the window handle
    if ( nMsg == WM_CREATE )
    {
        // Save Window-Instance in Windowhandle
        // Can also be used for the A-Version, because the struct
        // to access lpCreateParams is the same structure
        CREATESTRUCTW* pStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        WinSalFrame* pFrame = static_cast<WinSalFrame*>(pStruct->lpCreateParams);
        if ( pFrame != nullptr )
        {
            SetWindowPtr( hWnd, pFrame );

            UpdateAutoAccel();
            UpdateDarkMode(hWnd);

            // Set HWND already here, as data might be used already
            // when messages are being sent by CreateWindow()
            pFrame->mhWnd = hWnd;
            pFrame->maSysData.hWnd = hWnd;

            DWORD dwPanWant = GC_PAN | GC_PAN_WITH_SINGLE_FINGER_VERTICALLY
                              | GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY;
            DWORD dwPanBlock = GC_PAN_WITH_GUTTER;
            GESTURECONFIG gc[] = { { GID_ZOOM, GC_ZOOM, 0 },
                                   { GID_ROTATE, GC_ROTATE, 0 },
                                   { GID_PAN, dwPanWant, dwPanBlock } };
            UINT uiGcs = 3;
            if (!SetGestureConfig(hWnd, 0, uiGcs, gc, sizeof(GESTURECONFIG)))
            {
                SAL_WARN("vcl", "SetGestureConfig failed: " << comphelper::WindowsErrorString(GetLastError()));
            }
        }
        return 0;
    }

    ImplSVData* pSVData = ImplGetSVData();
    // #i72707# TODO: the mbDeInit check will not be needed
    // once all windows that are not properly closed on exit got fixed
    if( pSVData->mbDeInit )
        return 0;

    if ( WM_USER_SYSTEM_WINDOW_ACTIVATED == nMsg )
    {
        ImplHideSplash();
        return 0;
    }

    switch( nMsg )
    {
        case WM_GESTURE:
            rDef = !ImplHandleGestureMsg(hWnd, lParam);
            break;

        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_NCMOUSEMOVE:
        case SAL_MSG_MOUSELEAVE:
            ImplSalYieldMutexAcquireWithWait();
            rDef = !ImplHandleMouseMsg( hWnd, nMsg, wParam, lParam );
            ImplSalYieldMutexRelease();
            break;

        case WM_NCLBUTTONDOWN:
        case WM_NCMBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
            ImplSalYieldMutexAcquireWithWait();
            ImplCallClosePopupsHdl( hWnd );   // close popups...
            ImplSalYieldMutexRelease();
            break;

        case WM_MOUSEACTIVATE:
            if ( LOWORD( lParam ) == HTCLIENT )
            {
                ImplSalYieldMutexAcquireWithWait();
                nRet = LRESULT(ImplHandleMouseActivateMsg( hWnd ));
                ImplSalYieldMutexRelease();
                if ( nRet )
                {
                    nRet = MA_NOACTIVATE;
                    rDef = false;
                }
            }
            break;

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_DEADCHAR:
        case WM_CHAR:
        case WM_UNICHAR:    // MCD, 2003-01-13, Support for WM_UNICHAR & Keyman 6.0
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_SYSCHAR:
            ImplSalYieldMutexAcquireWithWait();
            rDef = !ImplHandleKeyMsg( hWnd, nMsg, wParam, lParam, nRet );
            ImplSalYieldMutexRelease();
            break;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            // protect against recursion, in case the message is returned
            // by IE or the external window
            if ( !bInWheelMsg )
            {
                bInWheelMsg = true;
                rDef = !ImplHandleWheelMsg( hWnd, nMsg, wParam, lParam );
                // If we did not process the message, re-check if there is a
                // connected (?) window that we have to notify.
                if ( rDef )
                    rDef = ImplSalWheelMousePos( hWnd, nMsg, wParam, lParam, nRet );
                bInWheelMsg = false;
            }
            break;

        case WM_COMMAND:
            ImplSalYieldMutexAcquireWithWait();
            rDef = !ImplHandleCommand( hWnd, wParam, lParam );
            ImplSalYieldMutexRelease();
            break;

        case WM_INITMENUPOPUP:
            ImplSalYieldMutexAcquireWithWait();
            rDef = !ImplHandleMenuActivate( hWnd, wParam, lParam );
            ImplSalYieldMutexRelease();
            break;

        case WM_MENUSELECT:
            ImplSalYieldMutexAcquireWithWait();
            rDef = !ImplHandleMenuSelect( hWnd, wParam, lParam );
            ImplSalYieldMutexRelease();
            break;

        case WM_SYSCOMMAND:
            ImplSalYieldMutexAcquireWithWait();
            nRet = LRESULT(ImplHandleSysCommand( hWnd, wParam, lParam ));
            ImplSalYieldMutexRelease();
            if ( nRet )
                rDef = false;
            break;

        case WM_MENUCHAR:
            nRet = ImplMenuChar( hWnd, wParam, lParam );
            if( nRet )
                rDef = false;
            break;

        case WM_MEASUREITEM:
            nRet = ImplMeasureItem(hWnd, wParam, lParam);
            if( nRet )
                rDef = false;
            break;

        case WM_DRAWITEM:
            nRet = ImplDrawItem(hWnd, wParam, lParam);
            if( nRet )
                rDef = false;
            break;

        case WM_MOVE:
            ImplHandleMoveMsg(hWnd, lParam);
            rDef = false;
            break;
        case SAL_MSG_POSTMOVE:
            ImplCallMoveHdl(hWnd);
            rDef = false;
            break;
        case WM_SIZE:
            ImplHandleSizeMsg(hWnd, wParam, lParam);
            rDef = false;
            break;
        case SAL_MSG_POSTCALLSIZE:
            ImplCallSizeHdl( hWnd );
            rDef = false;
            break;

        case WM_GETMINMAXINFO:
            if ( ImplHandleMinMax( hWnd, lParam ) )
                rDef = false;
            break;

        case WM_ERASEBKGND:
            nRet = 1;
            rDef = false;
            break;
        case WM_PAINT:
            ImplHandlePaintMsg( hWnd );
            rDef = false;
            break;
        case SAL_MSG_POSTPAINT:
            ImplHandlePostPaintMsg( hWnd, reinterpret_cast<RECT*>(wParam) );
            rDef = false;
            break;

        case WM_ENABLE:
            // #95133# a system dialog is opened/closed, using our app window as parent
            {
                WinSalFrame* pFrame = GetWindowPtr( hWnd );
                vcl::Window *pWin = nullptr;
                if( pFrame )
                    pWin = pFrame->GetWindow();

                if( !wParam )
                {
                    pSVData->maAppData.mnModalMode++;

                    ImplHideSplash();
                    if( pWin )
                    {
                        pWin->EnableInput( false, nullptr );
                        pWin->IncModalCount();  // #106303# support frame based modal count
                    }
                }
                else
                {
                    ImplGetSVData()->maAppData.mnModalMode--;
                    if( pWin )
                    {
                        pWin->EnableInput( true, nullptr );
                        pWin->DecModalCount();  // #106303# support frame based modal count
                    }
                }
            }
            break;

        case WM_KILLFOCUS:
            DestroyCaret();
            [[fallthrough]];
        case WM_SETFOCUS:
        case SAL_MSG_POSTFOCUS:
            ImplHandleFocusMsg( hWnd );
            rDef = false;
            break;

        case WM_CLOSE:
            ImplHandleCloseMsg( hWnd );
            rDef = false;
            break;

        case WM_QUERYENDSESSION:
            if( !bInQueryEnd )
            {
                // handle queryendsession only once
                bInQueryEnd = true;
                nRet = LRESULT(!ImplHandleShutDownMsg( hWnd ));
                rDef = false;

                // Issue #16314#: ImplHandleShutDownMsg causes a PostMessage in case of allowing shutdown.
                // This posted message was never processed and cause Windows XP to hang after log off
                // if there are multiple sessions and the current session wasn't the first one started.
                // So if shutdown is allowed we assume that a post message was done and retrieve all
                // messages in the message queue and dispatch them before we return control to the system.

                if ( nRet )
                {
                    SolarMutexGuard aGuard;
                    while ( Application::Reschedule( true ) );
                }
            }
            else
            {
                ImplSalYieldMutexAcquireWithWait();
                ImplSalYieldMutexRelease();
                rDef = true;
            }
            break;

        case WM_ENDSESSION:
            if( !wParam )
                bInQueryEnd = false; // no shutdown: allow query again
            nRet = FALSE;
            rDef = false;
            break;

        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
        case WM_DEVMODECHANGE:
        case WM_FONTCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_TIMECHANGE:
            ImplHandleSettingsChangeMsg( hWnd, nMsg, wParam, lParam );
            break;

        case WM_THEMECHANGED:
            UpdateDarkMode(hWnd);
            GetSalData()->mbThemeChanged = true;
            break;

        case SAL_MSG_USEREVENT:
            ImplHandleUserEvent( hWnd, lParam );
            rDef = false;
            break;

        case SAL_MSG_CAPTUREMOUSE:
            SetCapture( hWnd );
            rDef = false;
            break;
        case SAL_MSG_RELEASEMOUSE:
            if ( ::GetCapture() == hWnd )
                ReleaseCapture();
            rDef = false;
            break;
        case SAL_MSG_TOTOP:
            ImplSalToTop( hWnd, static_cast<SalFrameToTop>(wParam) );
            rDef = false;
            break;
        case SAL_MSG_SHOW:
            ImplSalShow( hWnd, static_cast<bool>(wParam), static_cast<bool>(lParam) );
            rDef = false;
            break;
        case SAL_MSG_SETINPUTCONTEXT:
            ImplSalFrameSetInputContext( hWnd, reinterpret_cast<const SalInputContext*>(lParam) );
            rDef = false;
            break;
        case SAL_MSG_ENDEXTTEXTINPUT:
            ImplSalFrameEndExtTextInput( hWnd, static_cast<EndExtTextInputFlags>(wParam) );
            rDef = false;
            break;

        case WM_INPUTLANGCHANGE:
            ImplHandleInputLangChange( hWnd, wParam, lParam );
            break;

        case WM_IME_CHAR:
            // #103487#, some IMEs (eg, those that do not work onspot)
            //           may send WM_IME_CHAR instead of WM_IME_COMPOSITION
            // we just handle it like a WM_CHAR message - seems to work fine
            ImplSalYieldMutexAcquireWithWait();
            rDef = !ImplHandleKeyMsg( hWnd, WM_CHAR, wParam, lParam, nRet );
            ImplSalYieldMutexRelease();
            break;

         case WM_IME_STARTCOMPOSITION:
            rDef = ImplHandleIMEStartComposition( hWnd );
            break;

        case WM_IME_COMPOSITION:
            rDef = ImplHandleIMEComposition( hWnd, lParam );
            break;

        case WM_IME_ENDCOMPOSITION:
            rDef = ImplHandleIMEEndComposition( hWnd );
            break;

        case WM_IME_NOTIFY:
            ImplHandleIMENotify( hWnd, wParam );
            break;

        case WM_GETOBJECT:
            // tdf#155794: this must complete without taking SolarMutex
            if ( ImplHandleGetObject( hWnd, lParam, wParam, nRet ) )
            {
                rDef = false;
            }
            break;

        case WM_APPCOMMAND:
            if( ImplHandleAppCommand( hWnd, lParam, nRet ) )
            {
                rDef = false;
            }
            break;
        case WM_IME_REQUEST:
            if ( static_cast<sal_uIntPtr>(wParam) == IMR_RECONVERTSTRING )
            {
                nRet = ImplHandleIMEReconvertString( hWnd, lParam );
                rDef = false;
            }
            else if( static_cast<sal_uIntPtr>(wParam) == IMR_CONFIRMRECONVERTSTRING )
            {
                nRet = ImplHandleIMEConfirmReconvertString( hWnd, lParam );
                rDef = false;
            }
            else if ( static_cast<sal_uIntPtr>(wParam) == IMR_QUERYCHARPOSITION )
            {
                if ( ImplSalYieldMutexTryToAcquire() )
                {
                    nRet = ImplHandleIMEQueryCharPosition( hWnd, lParam );
                    ImplSalYieldMutexRelease();
                }
                else
                    nRet = FALSE;
                rDef = false;
            }
            break;
    }

    return nRet;
}

LRESULT CALLBACK SalFrameWndProcW( HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam )
{
    bool bDef = true;
    LRESULT nRet = 0;
    __try
    {
        nRet = SalFrameWndProc( hWnd, nMsg, wParam, lParam, bDef );
    }
    __except(WinSalInstance::WorkaroundExceptionHandlingInUSER32Lib(GetExceptionCode(), GetExceptionInformation()))
    {
    }

    if ( bDef )
        nRet = DefWindowProcW( hWnd, nMsg, wParam, lParam );
    return nRet;
}

bool ImplHandleGlobalMsg( HWND /*hWnd*/, UINT nMsg, WPARAM /*wParam*/, LPARAM /*lParam*/, LRESULT& /*rlResult*/ )
{
    // handle all messages concerning all frames so they get processed only once
    // Must work for Unicode and none Unicode
    bool bResult = false;
    if( nMsg == WM_DISPLAYCHANGE )
    {
        WinSalSystem* pSys = static_cast<WinSalSystem*>(ImplGetSalSystem());
        if( pSys )
            pSys->clearMonitors();
        bResult = (pSys != nullptr);
    }
    return bResult;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
