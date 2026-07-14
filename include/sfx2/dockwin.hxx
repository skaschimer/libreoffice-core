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
#ifndef INCLUDED_SFX2_DOCKWIN_HXX
#define INCLUDED_SFX2_DOCKWIN_HXX

#include <memory>
#include <sal/config.h>
#include <sal/types.h>
#include <vcl/dockwin.hxx>
#include <vcl/weld/Box.hxx>
#include <sfx2/dllapi.h>
#include <sfx2/childwin.hxx>

namespace com::sun::star::frame { class XFrame; }
namespace com::sun::star::uno { template <class interface_type> class Reference; }
namespace weld { class Builder; }

class SfxDockingWindow_Impl;

void SfxDockingWindowFactory( const css::uno::Reference< css::frame::XFrame >& rFrame, std::u16string_view rDockingWindowName );
bool IsDockingWindowVisible( const css::uno::Reference< css::frame::XFrame >& rFrame, std::u16string_view rDockingWindowName );

class SFX2_DLLPUBLIC SfxDockingWindow : public ResizableDockingWindow
{
protected:
    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Box> m_xContainer;

private:
    tools::Rectangle m_aInnerRect;
    tools::Rectangle m_aOuterRect;
    SfxBindings* m_pBindings;
    Size m_aFloatSize;
    SfxChildWindow* m_pMgr;
    std::unique_ptr<SfxDockingWindow_Impl> m_pImpl;

    SfxDockingWindow(SfxDockingWindow const &) = delete;
    SfxDockingWindow& operator =(SfxDockingWindow const &) = delete;

protected:
    SfxChildAlignment   CalcAlignment(const Point& rPos, tools::Rectangle& rRect );
    virtual Size        CalcDockingSize(SfxChildAlignment);
    virtual SfxChildAlignment
                        CheckAlignment(SfxChildAlignment,SfxChildAlignment);

    virtual void        Resize() override;
    virtual bool        PrepareToggleFloatingMode() override;
    virtual void        ToggleFloatingMode() override;
    virtual void        StartDocking() override;
    virtual bool        Docking( const Point& rPos, tools::Rectangle& rRect ) override;
    virtual void        EndDocking( const tools::Rectangle& rRect, bool bFloatMode ) override;
    virtual void        Resizing( Size& rSize ) override;
    virtual void        Paint( vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect ) override;
    virtual bool        Close() override;
    virtual void        Move() override;

    SAL_DLLPRIVATE SfxChildWindow* GetChildWindow_Impl() { return m_pMgr; }

public:
                        SfxDockingWindow( SfxBindings *pBindings,
                                          SfxChildWindow *pCW,
                                          vcl::Window* pParent,
                                          WinBits nWinBits);
                        SfxDockingWindow( SfxBindings *pBindings,
                                          SfxChildWindow *pCW,
                                          vcl::Window* pParent,
                                          const OUString& rID, const OUString& rUIXMLDescription );
                        virtual ~SfxDockingWindow() override;
    virtual void        dispose() override;

    void                Initialize (SfxChildWinInfo* pInfo);
    virtual void        FillInfo(SfxChildWinInfo&) const;
    virtual void        StateChanged( StateChangedType nStateChange ) override;

    void                SetDockingRects(const tools::Rectangle& rOuter, const tools::Rectangle& rInner)
    {
        m_aInnerRect = rInner;
        m_aOuterRect = rOuter;
    }
    const tools::Rectangle& GetInnerRect() const { return m_aInnerRect; }
    const tools::Rectangle& GetOuterRect() const { return m_aOuterRect; }
    SfxBindings& GetBindings() const { return *m_pBindings; }
    sal_uInt16 GetType() const { return m_pMgr->GetType(); }
    SfxChildAlignment GetAlignment() const { return m_pMgr->GetAlignment(); }
    void SetAlignment(SfxChildAlignment eAlign) { m_pMgr->SetAlignment(eAlign); }
    const Size& GetFloatingSize() const { return m_aFloatSize; }
    void SetFloatingSize(const Size& rSize) { m_aFloatSize = rSize; }

    void                SetMinOutputSizePixel( const Size& rSize );
    const Size&         GetMinOutputSizePixel() const;
    virtual bool        EventNotify( NotifyEvent& rNEvt ) override;
    DECL_DLLPRIVATE_LINK(TimerHdl, Timer *, void);

    SAL_DLLPRIVATE void Initialize_Impl();
    SAL_DLLPRIVATE void SetItemSize_Impl( const Size& rSize );
    SAL_DLLPRIVATE void Disappear_Impl();
    SAL_DLLPRIVATE void Reappear_Impl();
    SAL_DLLPRIVATE bool IsAutoHide_Impl() const;
    SAL_DLLPRIVATE void AutoShow_Impl();
    SAL_DLLPRIVATE void ReleaseChildWindow_Impl();
};

class SfxDockingWrapper final : public SfxChildWindow
{
    public:
        SfxDockingWrapper( vcl::Window* pParent ,
                           sal_uInt16 nId ,
                           SfxBindings* pBindings ,
                           SfxChildWinInfo* pInfo );

        SFX_DECL_CHILDWINDOW(SfxDockingWrapper);
};

#endif // INCLUDED_SFX2_DOCKWIN_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
