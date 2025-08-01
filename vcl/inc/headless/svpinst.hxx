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

#include <osl/thread.hxx>
#include <osl/conditn.hxx>
#include <salinst.hxx>
#include <saltimer.hxx>
#include <salusereventlist.hxx>
#include <unx/geninst.h>
#include <unx/genprn.h>

#include <condition_variable>
#include <mutex>
#include <queue>

#include <sys/time.h>

#ifdef IOS
#define SvpSalInstance AquaSalInstance
#endif

class SvpSalInstance;
class SvpSalTimer final : public SalTimer
{
    SvpSalInstance* m_pInstance;
public:
    SvpSalTimer( SvpSalInstance* pInstance ) : m_pInstance( pInstance ) {}
    virtual ~SvpSalTimer() override;

    // override all pure virtual methods
    virtual void Start( sal_uInt64 nMS ) override;
    virtual void Stop() override;
};

enum class SvpRequest
{
    NONE,
    MainThreadDispatchOneEvent,
    MainThreadDispatchAllEvents,
};

class SvpSalYieldMutex final : public SalYieldMutex
{
private:
    // note: these members might as well live in SvpSalInstance, but there is
    // at least one subclass of SvpSalInstance (GTK3) that doesn't use them.
    friend class SvpSalInstance;
    // members for communication from main thread to non-main thread
    std::mutex              m_FeedbackMutex;
    std::queue<bool>        m_FeedbackPipe;
    std::condition_variable m_FeedbackCV;
    osl::Condition          m_NonMainWaitingYieldCond;
    // members for communication from non-main thread to main thread
    bool                    m_bNoYieldLock = false; // accessed only on main thread
    std::mutex              m_WakeUpMainMutex; // guard m_wakeUpMain & m_Request
    std::condition_variable m_WakeUpMainCond;
    bool                    m_wakeUpMain = false;
    SvpRequest              m_Request = SvpRequest::NONE;

    virtual void            doAcquire( sal_uInt32 nLockCount ) override;
    virtual sal_uInt32      doRelease( bool bUnlockAll ) override;

public:
    SvpSalYieldMutex();
    virtual ~SvpSalYieldMutex() override;

    virtual bool IsCurrentThread() const override;
};

// NOTE: the functions IsMainThread, DoYield and Wakeup *require* the use of
// SvpSalYieldMutex; if a subclass uses something else it must override these
// (Wakeup is only called by SvpSalTimer and SvpSalFrame)
class VCL_DLLPUBLIC SvpSalInstance : public SalGenericInstance, public SalUserEventList
{
    timeval                 m_aTimeout;
    sal_uLong               m_nTimeoutMS;
    oslThreadIdentifier     m_MainThread;

    virtual void            TriggerUserEventProcessing() override;
    virtual void            ProcessEvent( SalUserEvent aEvent ) override;

#if defined EMSCRIPTEN
    bool DoExecute(int &nExitCode) override;
    void DoQuit() override;
#endif

public:
    static SvpSalInstance*  s_pDefaultInstance;

    SvpSalInstance( std::unique_ptr<SalYieldMutex> pMutex );
    virtual ~SvpSalInstance() override;

    SAL_DLLPRIVATE bool ImplYield(bool bWait, bool bHandleAllCurrentEvents);

    SAL_DLLPRIVATE void     CloseWakeupPipe();
    SAL_DLLPRIVATE void     Wakeup(SvpRequest request = SvpRequest::NONE);

    SAL_DLLPRIVATE void     StartTimer( sal_uInt64 nMS );
    SAL_DLLPRIVATE void     StopTimer();

    inline void             registerFrame( SalFrame* pFrame );
    inline void             deregisterFrame( SalFrame* pFrame );

    SAL_DLLPRIVATE bool     CheckTimeout( bool bExecuteTimers = true );

    // Frame
    SAL_DLLPRIVATE virtual SalFrame* CreateChildFrame( SystemParentData* pParent, SalFrameStyleFlags nStyle ) override;
    SAL_DLLPRIVATE virtual SalFrame* CreateFrame( SalFrame* pParent, SalFrameStyleFlags nStyle ) override;
    virtual void            DestroyFrame( SalFrame* pFrame ) override;

    // Object (System Child Window)
    SAL_DLLPRIVATE virtual SalObject* CreateObject( SalFrame* pParent, SystemWindowData* pWindowData, bool bShow ) override;
    virtual void            DestroyObject( SalObject* pObject ) override;

    // VirtualDevice
    // nDX and nDY in Pixel
    SAL_DLLPRIVATE virtual std::unique_ptr<SalVirtualDevice>
                            CreateVirtualDevice( SalGraphics& rGraphics,
                                                     tools::Long nDX, tools::Long nDY,
                                                     DeviceFormat eFormat,
                                                     bool bAlphaMaskTransparent = false ) override;

    // VirtualDevice
    // nDX and nDY in Pixel
    // pData allows for using a system dependent graphics or device context
    SAL_DLLPRIVATE virtual std::unique_ptr<SalVirtualDevice>
                            CreateVirtualDevice( SalGraphics& rGraphics,
                                                     tools::Long &nDX, tools::Long &nDY,
                                                     DeviceFormat eFormat,
                                                     const SystemGraphicsData& rData ) override;

    // Printer
    // pSetupData->mpDriverData can be 0
    // pSetupData must be updated with the current
    // JobSetup
    SAL_DLLPRIVATE virtual SalInfoPrinter* CreateInfoPrinter( SalPrinterQueueInfo* pQueueInfo,
                                               ImplJobSetup* pSetupData ) override;
    virtual void            DestroyInfoPrinter( SalInfoPrinter* pPrinter ) override;
    SAL_DLLPRIVATE virtual std::unique_ptr<SalPrinter> CreatePrinter( SalInfoPrinter* pInfoPrinter ) override;

    virtual void            GetPrinterQueueInfo( ImplPrnQueueList* pList ) override;
    virtual void            GetPrinterQueueState( SalPrinterQueueInfo* pInfo ) override;
    virtual OUString        GetDefaultPrinter() override;
    virtual void            PostPrintersChanged() override;

    // SalTimer
    SAL_DLLPRIVATE virtual SalTimer* CreateSalTimer() override;
    // SalSystem
    SAL_DLLPRIVATE virtual SalSystem* CreateSalSystem() override;
    // SalBitmap
    virtual std::shared_ptr<SalBitmap> CreateSalBitmap() override;

    // wait next event and dispatch
    // must returned by UserEvent (SalFrame::PostEvent)
    // and timer
    SAL_DLLPRIVATE virtual bool DoYield(bool bWait, bool bHandleAllCurrentEvents) override;
    SAL_DLLPRIVATE virtual bool AnyInput( VclInputFlags nType ) override;
    SAL_DLLPRIVATE virtual bool IsMainThread() const override;
    virtual void            updateMainThread() override;

    SAL_DLLPRIVATE virtual void AddToRecentDocumentList(const OUString& rFileUrl, const OUString& rMimeType, const OUString& rDocumentService) override;

    SAL_DLLPRIVATE virtual std::unique_ptr<GenPspGraphics> CreatePrintGraphics() override;

    SAL_DLLPRIVATE virtual const cairo_font_options_t* GetCairoFontOptions() override;
};

inline void SvpSalInstance::registerFrame( SalFrame* pFrame )
{
    insertFrame( pFrame );
}

inline void SvpSalInstance::deregisterFrame( SalFrame* pFrame )
{
    eraseFrame( pFrame );
}

VCL_DLLPUBLIC cairo_surface_t* get_underlying_cairo_surface(const VirtualDevice& rDevice);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
