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

#include <vcl/window.hxx>
#include <swtypes.hxx>

#include <com/sun/star/accessibility/XAccessible.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <vcl/unohelp.hxx>
#include <i18nlangtag/languagetag.hxx>
#include <unotools/accessiblerelationsethelper.hxx>
#include <viewsh.hxx>
#include <crsrsh.hxx>
#include <fesh.hxx>
#include <wrtsh.hxx>
#include <txtfrm.hxx>
#include <ndtxt.hxx>
#include <pagefrm.hxx>
#include <flyfrm.hxx>
#include <dflyobj.hxx>
#include <pam.hxx>
#include <accmap.hxx>
#include "accfrmobjslist.hxx"
#include "acccontext.hxx"
#include <svx/AccessibleShape.hxx>
#include <comphelper/accessibleeventnotifier.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <PostItMgr.hxx>

using namespace sw::access;
using namespace ::com::sun::star;
using namespace ::com::sun::star::accessibility;

void SwAccessibleContext::SetParent( SwAccessibleContext *pParent )
{
    std::scoped_lock aGuard( m_Mutex );

    m_xWeakParent = pParent;
}

rtl::Reference< SwAccessibleContext > SwAccessibleContext::GetWeakParent() const
{
    std::scoped_lock aGuard( m_Mutex );

    return m_xWeakParent.get();
}

vcl::Window *SwAccessibleContext::GetWindow()
{
    vcl::Window *pWin = nullptr;

    if( GetMap() )
    {
        pWin = GetMap()->GetShell().GetWin();
        OSL_ENSURE( pWin, "no window" );
    }

    return pWin;
}

// get SwViewShell from accessibility map, and cast to cursor shell
SwCursorShell* SwAccessibleContext::GetCursorShell()
{
    SwViewShell* pViewShell = GetMap() ? &GetMap()->GetShell() : nullptr;
    OSL_ENSURE( pViewShell, "no view shell" );
    return dynamic_cast<SwCursorShell*>( pViewShell);
}

const SwCursorShell* SwAccessibleContext::GetCursorShell() const
{
    // just like non-const GetCursorShell
    const SwViewShell* pViewShell = GetMap() ? &GetMap()->GetShell() : nullptr;
    OSL_ENSURE( pViewShell, "no view shell" );
    return dynamic_cast<const SwCursorShell*>( pViewShell);
}

namespace {

enum class Action { NONE, SCROLLED, SCROLLED_WITHIN,
                          SCROLLED_IN, SCROLLED_OUT };

}

void SwAccessibleContext::ChildrenScrolled( const SwFrame *pFrame,
                                            const SwRect& rOldVisArea )
{
    const SwRect& rNewVisArea = GetVisArea();
    const bool bVisibleChildrenOnly = SwAccessibleChild( pFrame ).IsVisibleChildrenOnly();

    const SwAccessibleChildSList aList( *pFrame, *(GetMap()) );
    for (const SwAccessibleChild& rLower : aList)
    {
        const SwRect aBox( rLower.GetBox( *(GetMap()) ) );
        if (rLower.IsAccessible(GetShell().IsPreview()))
        {
            Action eAction = Action::NONE;
            if( aBox.Overlaps( rNewVisArea ) )
            {
                if( aBox.Overlaps( rOldVisArea ) )
                    eAction = Action::SCROLLED_WITHIN;
                else if (bVisibleChildrenOnly && !rLower.AlwaysIncludeAsChild())
                    eAction = Action::SCROLLED_IN;
                else
                    eAction = Action::SCROLLED;
            }
            else if( aBox.Overlaps( rOldVisArea ) )
            {
                if ( bVisibleChildrenOnly &&
                     !rLower.AlwaysIncludeAsChild() )
                {
                    eAction = Action::SCROLLED_OUT;
                }
                else
                {
                    eAction = Action::SCROLLED;
                }
            }
            else if( !bVisibleChildrenOnly ||
                     rLower.AlwaysIncludeAsChild() )
            {
                // This wouldn't be required if the SwAccessibleFrame,
                // wouldn't know about the visible area.
                eAction = Action::SCROLLED;
            }
            if( Action::NONE != eAction )
            {
                if ( rLower.GetSwFrame() )
                {
                    OSL_ENSURE( !rLower.AlwaysIncludeAsChild(),
                            "<SwAccessibleContext::ChildrenScrolled(..)> - always included child not considered!" );
                    const SwFrame* pLower( rLower.GetSwFrame() );
                    ::rtl::Reference< SwAccessibleContext > xAccImpl =
                        GetMap()->GetContextImpl( pLower );
                    if( xAccImpl.is() )
                    {
                        switch( eAction )
                        {
                        case Action::SCROLLED:
                            xAccImpl->Scrolled( rOldVisArea );
                            break;
                        case Action::SCROLLED_WITHIN:
                            xAccImpl->ScrolledWithin( rOldVisArea );
                            break;
                        case Action::SCROLLED_IN:
                            xAccImpl->ScrolledIn();
                            break;
                        case Action::SCROLLED_OUT:
                            xAccImpl->ScrolledOut( rOldVisArea );
                            break;
                        case Action::NONE:
                            break;
                        }
                    }
                    else
                    {
                        ChildrenScrolled( pLower, rOldVisArea );
                    }
                }
                else if ( rLower.GetDrawObject() )
                {
                    OSL_ENSURE( !rLower.AlwaysIncludeAsChild(),
                            "<SwAccessibleContext::ChildrenScrolled(..)> - always included child not considered!" );
                    ::rtl::Reference< ::accessibility::AccessibleShape > xAccImpl =
                        GetMap()->GetContextImpl( rLower.GetDrawObject(),
                                                  this );
                    if( xAccImpl.is() )
                    {
                        switch( eAction )
                        {
                        case Action::SCROLLED:
                        case Action::SCROLLED_WITHIN:
                            xAccImpl->ViewForwarderChanged();
                            break;
                        case Action::SCROLLED_IN:
                            ScrolledInShape( xAccImpl.get() );
                            break;
                        case Action::SCROLLED_OUT:
                            {
                                xAccImpl->ViewForwarderChanged();
                                // this DisposeShape call was removed by
                                // IAccessibility2 implementation
                                // without giving any reason why
                                DisposeShape( rLower.GetDrawObject(),
                                          xAccImpl.get() );
                            }
                            break;
                        // coverity[dead_error_begin] - following conditions exist to avoid compiler warning
                        case Action::NONE:
                            break;
                        }
                    }
                }
                else if ( rLower.GetWindow() )
                {
                    // nothing to do - as such children are always included as children.
                    OSL_ENSURE( rLower.AlwaysIncludeAsChild(),
                            "<SwAccessibleContext::ChildrenScrolled(..)> - not always included child not considered!" );
                }
            }
        }
        else if ( rLower.GetSwFrame() &&
                  ( !bVisibleChildrenOnly ||
                    aBox.Overlaps( rOldVisArea ) ||
                    aBox.Overlaps( rNewVisArea ) ) )
        {
            // There are no unaccessible SdrObjects that need to be notified
            ChildrenScrolled( rLower.GetSwFrame(), rOldVisArea );
        }
    }
}

void SwAccessibleContext::Scrolled( const SwRect& rOldVisArea )
{
    SetVisArea( GetMap()->GetVisArea() );

    ChildrenScrolled( GetFrame(), rOldVisArea );

    bool bIsOldShowingState;
    bool bIsNewShowingState = IsShowing( *(GetMap()) );
    {
        std::scoped_lock aGuard( m_Mutex );
        bIsOldShowingState = m_isShowingState;
        m_isShowingState = bIsNewShowingState;
    }

    if( bIsOldShowingState != bIsNewShowingState )
        FireStateChangedEvent( AccessibleStateType::SHOWING,
                               bIsNewShowingState  );
}

void SwAccessibleContext::ScrolledWithin( const SwRect& rOldVisArea )
{
    SetVisArea( GetMap()->GetVisArea() );

    ChildrenScrolled( GetFrame(), rOldVisArea );

    FireVisibleDataEvent();
}

void SwAccessibleContext::ScrolledIn()
{
    // This accessible should be freshly created, because it
    // was not visible before. Therefore, its visible area must already
    // reflect the scrolling.
    OSL_ENSURE( GetVisArea() == GetMap()->GetVisArea(),
            "Visible area of child is wrong. Did it exist already?" );

    // Send child event at parent. That's all we have to do here.
    const SwFrame* pParent = GetParent();
    ::rtl::Reference< SwAccessibleContext > xParentImpl(
         GetMap()->GetContextImpl( pParent, false ) );
    if( !xParentImpl.is() )
        return;

    SetParent( xParentImpl.get() );

    uno::Reference<XAccessibleContext> xThis(this);
    xParentImpl->FireAccessibleEvent(AccessibleEventId::CHILD, uno::Any(), uno::Any(xThis));

    if( HasCursor() )
    {
        vcl::Window *pWin = GetWindow();
        if( pWin && pWin->HasFocus() )
        {
            FireStateChangedEvent( AccessibleStateType::FOCUSED, true );
        }
    }
}

void SwAccessibleContext::ScrolledOut( const SwRect& rOldVisArea )
{
    SetVisArea( GetMap()->GetVisArea() );

    // First of all, update the children. That's required to dispose
    // all children that are existing only if they are visible. They
    // are not disposed by the recursive Dispose call that follows later on,
    // because this call will only dispose children that are in the
    // new visible area. The children we want to dispose however are in the
    // old visible area all.
    ChildrenScrolled( GetFrame(), rOldVisArea );

    // Broadcast a state changed event for the showing state.
    // It might be that the child is freshly created just to send
    // the child event. In this case no listener will exist.
    FireStateChangedEvent( AccessibleStateType::SHOWING, false );

    // this Dispose call was removed by IAccessibility2 implementation
    // without giving any reason why - without it we get stale
    // entries in SwAccessibleMap::mpFrameMap.
    Dispose(true);
}

// #i27301# - use new type definition for <_nStates>
void SwAccessibleContext::InvalidateChildrenStates( const SwFrame* _pFrame,
                                                    AccessibleStates _nStates )
{
    const SwAccessibleChildSList aVisList( GetVisArea(), *_pFrame, *(GetMap()) );

    for (const SwAccessibleChild& rLower : aVisList)
    {
        if (const SwFrame* pLower = rLower.GetSwFrame())
        {
            ::rtl::Reference< SwAccessibleContext > xAccImpl;
            if (rLower.IsAccessible(GetShell().IsPreview()))
                xAccImpl = GetMap()->GetContextImpl( pLower, false );
            if( xAccImpl.is() )
                xAccImpl->InvalidateStates( _nStates );
            else
                InvalidateChildrenStates( pLower, _nStates );
        }
        else if ( rLower.GetDrawObject() )
        {
            // TODO: SdrObjects
        }
        else if ( rLower.GetWindow() )
        {
            // nothing to do ?
        }
    }
}

void SwAccessibleContext::DisposeChildren(const SwFrame *pFrame,
                                          bool bRecursive,
                                          bool bCanSkipInvisible)
{
    const SwAccessibleChildSList aVisList( GetVisArea(), *pFrame, *(GetMap()) );
    SwAccessibleChildSList::const_iterator aIter( aVisList.begin() );
    while( aIter != aVisList.end() )
    {
        const SwAccessibleChild& rLower = *aIter;
        const SwFrame* pLower = rLower.GetSwFrame();
        if( pLower )
        {
            // tdf#117601 dispose the darn thing if it ever was accessible
            ::rtl::Reference<SwAccessibleContext> xAccImpl = GetMap()->GetContextImpl(pLower, false);
            if( xAccImpl.is() )
                xAccImpl->Dispose( bRecursive );
            else
            {
                // it's possible that the xAccImpl *does* exist with a
                // ref-count of 0 and blocked in its dtor in another thread -
                // this call here could be from SwAccessibleMap dtor so
                // remove it from any maps now!
                GetMap()->RemoveContext(pLower);
                // in this case the context will check with a weak_ptr
                // that the map is still alive so it's not necessary
                // to clear its m_pMap here.
                if (bRecursive)
                {
                    DisposeChildren(pLower, bRecursive, bCanSkipInvisible);
                }
            }
        }
        else if ( rLower.GetDrawObject() )
        {
            ::rtl::Reference< ::accessibility::AccessibleShape > xAccImpl(
                    GetMap()->GetContextImpl( rLower.GetDrawObject(),
                                          this, false )  );
            if( xAccImpl.is() )
                DisposeShape( rLower.GetDrawObject(), xAccImpl.get() );
        }
        else if ( rLower.GetWindow() )
        {
            DisposeChild(rLower, false, bCanSkipInvisible);
        }
        ++aIter;
    }
}

void SwAccessibleContext::InvalidateContent_( bool )
{
}

void SwAccessibleContext::InvalidateCursorPos_()
{
}

void SwAccessibleContext::InvalidateFocus_()
{
}

void SwAccessibleContext::FireAccessibleEvent(const sal_Int16 nEventId,
                                              const css::uno::Any& rOldValue,
                                              const css::uno::Any& rNewValue, sal_Int32 nIndexHint)
{
    if( !GetFrame() )
    {
        SAL_INFO("sw.a11y", "SwAccessibleContext::FireAccessibleEvent called for already disposed frame?");
        return;
    }

    NotifyAccessibleEvent(nEventId, rOldValue, rNewValue, nIndexHint);
}

void SwAccessibleContext::FireVisibleDataEvent()
{
    FireAccessibleEvent(AccessibleEventId::VISIBLE_DATA_CHANGED, uno::Any(), uno::Any());
}

void SwAccessibleContext::FireStateChangedEvent( sal_Int64 nState,
                                                 bool bNewState )
{
    uno::Any aOldValue;
    uno::Any aNewValue;

    if( bNewState )
        aNewValue <<= nState;
    else
        aOldValue <<= nState;

    FireAccessibleEvent(AccessibleEventId::STATE_CHANGED, aOldValue, aNewValue);
}

void SwAccessibleContext::GetStates( sal_Int64& rStateSet )
{
    std::scoped_lock aGuard( m_Mutex );

    // SHOWING
    if (m_isShowingState)
        rStateSet |= AccessibleStateType::SHOWING;

    // EDITABLE
    if (m_isEditableState)
    //Set editable state to graphic and other object when the document is editable
    {
        rStateSet |= AccessibleStateType::EDITABLE;
        rStateSet |= AccessibleStateType::RESIZABLE;
        rStateSet |= AccessibleStateType::MOVEABLE;
    }
    // ENABLED
    rStateSet |= AccessibleStateType::ENABLED;

    // OPAQUE
    if (m_isOpaqueState)
        rStateSet |= AccessibleStateType::OPAQUE;

    // VISIBLE
    rStateSet |= AccessibleStateType::VISIBLE;

    if (m_isDefuncState)
        rStateSet |= AccessibleStateType::DEFUNC;
}

bool SwAccessibleContext::IsEditableState()
{
    bool bRet;
    {
        std::scoped_lock aGuard( m_Mutex );
        bRet = m_isEditableState;
    }

    return bRet;
}

bool SwAccessibleContext::IsDisposed() const
{
    return !(GetFrame() && GetMap());
}

void SwAccessibleContext::ThrowIfDisposed()
{
    if (IsDisposed())
    {
        throw lang::DisposedException(u"object is nonfunctional"_ustr,
                getXWeak());
    }
}

SwAccessibleContext::SwAccessibleContext(std::shared_ptr<SwAccessibleMap> const& pMap,
                                          sal_Int16 const nRole,
                                          const SwFrame *pF )
    : SwAccessibleFrame( pMap->GetVisArea(), pF,
                         pMap->GetShell().IsPreview() )
    , m_pMap(pMap.get())
    , m_wMap(pMap)
    , m_nRole(nRole)
    , m_isDisposing( false )
    , m_isRegisteredAtAccessibleMap( true )
    , m_isSelectedInDoc(false)
{
    m_isShowingState = IsShowing( *(GetMap()) );

    SwViewShell& rVSh = GetMap()->GetShell();
    m_isEditableState = IsEditable(rVSh);
    m_isOpaqueState = IsOpaque(rVSh);
    m_isDefuncState = false;
}

SwAccessibleContext::~SwAccessibleContext()
{
    // must have for 2 reasons: 2. as long as this thread has SolarMutex
    // another thread cannot destroy the SwAccessibleMap so our temporary
    // taking a hard ref to SwAccessibleMap won't delay its destruction
    SolarMutexGuard aGuard;
    // must check with weak_ptr that m_pMap is still alive
    std::shared_ptr<SwAccessibleMap> pMap(m_wMap.lock());
    if (m_isRegisteredAtAccessibleMap && GetFrame() && pMap)
    {
        pMap->RemoveContext( GetFrame() );
    }
}

sal_Int64 SAL_CALL SwAccessibleContext::getAccessibleChildCount()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    return m_isDisposing ? 0 : GetChildCount( *(GetMap()) );
}

uno::Reference< XAccessible> SAL_CALL
    SwAccessibleContext::getAccessibleChild( sal_Int64 nIndex )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    if (nIndex < 0 || nIndex >= getAccessibleChildCount())
        throw lang::IndexOutOfBoundsException();

    const SwAccessibleChild aChild( GetChild( *(GetMap()), nIndex ) );
    if( !aChild.IsValid() )
    {
        uno::Reference < XAccessibleContext > xThis( this );
        lang::IndexOutOfBoundsException aExcept(
                u"index out of bounds"_ustr,
                xThis );
        throw aExcept;
    }

    rtl::Reference<comphelper::OAccessible> pChild;
    if( aChild.GetSwFrame() )
    {
        ::rtl::Reference < SwAccessibleContext > xChildImpl(
                GetMap()->GetContextImpl( aChild.GetSwFrame(), !m_isDisposing )  );
        if( xChildImpl.is() )
        {
            xChildImpl->SetParent( this );
            pChild = xChildImpl;
        }
    }
    else if ( aChild.GetDrawObject() )
    {
        ::rtl::Reference < ::accessibility::AccessibleShape > xChildImpl(
                GetMap()->GetContextImpl( aChild.GetDrawObject(),
                                          this, !m_isDisposing) );
        if( xChildImpl.is() )
            pChild = xChildImpl;
    }
    else if ( aChild.GetWindow() )
    {
        pChild = aChild.GetWindow()->GetAccessible();
    }

    return pChild;
}

css::uno::Sequence<uno::Reference<XAccessible>> SAL_CALL
    SwAccessibleContext::getAccessibleChildren()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    std::list<sw::access::SwAccessibleChild> aChildren = GetChildren(*GetMap());

    std::vector<uno::Reference<XAccessible>> aRet;
    aRet.reserve(aChildren.size());
    for (const auto & rSwChild : aChildren)
    {
        uno::Reference< XAccessible > xChild;
        if( rSwChild.GetSwFrame() )
        {
            ::rtl::Reference < SwAccessibleContext > xChildImpl(
                    GetMap()->GetContextImpl( rSwChild.GetSwFrame(), !m_isDisposing )  );
            if( xChildImpl.is() )
            {
                xChildImpl->SetParent( this );
                xChild = xChildImpl.get();
            }
        }
        else if ( rSwChild.GetDrawObject() )
        {
            ::rtl::Reference < ::accessibility::AccessibleShape > xChildImpl(
                    GetMap()->GetContextImpl( rSwChild.GetDrawObject(),
                                              this, !m_isDisposing) );
            if( xChildImpl.is() )
                xChild = xChildImpl.get();
        }
        else if ( rSwChild.GetWindow() )
        {
            xChild = rSwChild.GetWindow()->GetAccessible();
        }
        aRet.push_back(xChild);
    }
    return comphelper::containerToSequence(aRet);
}

rtl::Reference< SwAccessibleContext> SwAccessibleContext::getAccessibleParentImpl()
{
    SolarMutexGuard aGuard;

    const SwFrame *pUpper = GetParent();
    OSL_ENSURE( pUpper != nullptr || m_isDisposing, "no upper found" );

    rtl::Reference< SwAccessibleContext > xAcc;
    if( pUpper )
        xAcc = GetMap()->GetContextImpl( pUpper, !m_isDisposing );

    OSL_ENSURE( xAcc.is() || m_isDisposing, "no parent found" );

    // Remember the parent as weak ref.
    {
        std::scoped_lock aWeakParentGuard( m_Mutex );
        m_xWeakParent = xAcc.get();
    }

    return xAcc;
}

uno::Reference< XAccessible> SAL_CALL SwAccessibleContext::getAccessibleParent()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    return getAccessibleParentImpl();
}

sal_Int64 SAL_CALL SwAccessibleContext::getAccessibleIndexInParent()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    const SwFrame *pUpper = GetParent();
    OSL_ENSURE( pUpper != nullptr || m_isDisposing, "no upper found" );

    sal_Int64 nIndex = -1;
    if( pUpper )
    {
        ::rtl::Reference < SwAccessibleContext > xAccImpl(
            GetMap()->GetContextImpl(pUpper, !m_isDisposing) );
        OSL_ENSURE( xAccImpl.is() || m_isDisposing, "no parent found" );
        if( xAccImpl.is() )
            nIndex = xAccImpl->GetChildIndex( *(GetMap()), SwAccessibleChild(GetFrame()) );
    }

    return nIndex;
}

sal_Int16 SAL_CALL SwAccessibleContext::getAccessibleRole()
{
    return m_nRole;
}

OUString SAL_CALL SwAccessibleContext::getAccessibleName()
{
    return m_sName;
}

uno::Reference< XAccessibleRelationSet> SAL_CALL
    SwAccessibleContext::getAccessibleRelationSet()
{
    // by default there are no relations
    uno::Reference< XAccessibleRelationSet> xRet( new utl::AccessibleRelationSetHelper() );
    return xRet;
}

sal_Int64 SAL_CALL SwAccessibleContext::getAccessibleStateSet()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    sal_Int64 nStateSet = 0;

    if (m_isSelectedInDoc)
        nStateSet |= AccessibleStateType::SELECTED;

    GetStates( nStateSet );

    return nStateSet;
}

lang::Locale SAL_CALL SwAccessibleContext::getLocale()
{
    SolarMutexGuard aGuard;

    lang::Locale aLoc( Application::GetSettings().GetLanguageTag().getLocale() );
    return aLoc;
}

css::awt::Rectangle SwAccessibleContext::implGetBounds() { return getBoundsImpl(true); }

uno::Reference< XAccessible > SAL_CALL SwAccessibleContext::getAccessibleAtPoint(
                const awt::Point& aPoint )
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    uno::Reference< XAccessible > xAcc;

    vcl::Window *pWin = GetWindow();
    if (!pWin)
    {
        throw uno::RuntimeException(u"no Window"_ustr, getXWeak());
    }

    Point aPixPoint( aPoint.X, aPoint.Y ); // px rel to parent
    if( !GetFrame()->IsRootFrame() )
    {
        SwRect aLogBounds( GetBounds( *(GetMap()), GetFrame() ) ); // twip rel to doc root
        Point aPixPos( GetMap()->CoreToPixel( aLogBounds ).TopLeft() );
        aPixPoint.setX(aPixPoint.getX() + aPixPos.getX());
        aPixPoint.setY(aPixPoint.getY() + aPixPos.getY());
    }

    const SwAccessibleChild aChild( GetChildAtPixel( aPixPoint, *(GetMap()) ) );
    if( aChild.GetSwFrame() )
    {
        xAcc = GetMap()->GetContext( aChild.GetSwFrame() );
    }
    else if( aChild.GetDrawObject() )
    {
        xAcc = GetMap()->GetContext( aChild.GetDrawObject(), this );
    }
    else if ( aChild.GetWindow() )
    {
        xAcc = aChild.GetWindow()->GetAccessible();
    }

    return xAcc;
}

/**
   Get bounding box.

   There are two modes.

   - relative

     Return bounding box relative to parent if parent is no root
     frame. Otherwise return the absolute bounding box.

   - absolute

     Return the absolute bounding box.

   @param bRelative
   true: Use relative mode.
   false: Use absolute mode.
*/
awt::Rectangle SwAccessibleContext::getBoundsImpl(bool bRelative)
{
    ThrowIfDisposed();

    const SwFrame *pParent = GetParent();
    OSL_ENSURE( pParent, "no Parent found" );
    vcl::Window *pWin = GetWindow();

    if (!pParent)
    {
        throw uno::RuntimeException(u"no Parent"_ustr, getXWeak());
    }
    if (!pWin)
    {
        throw uno::RuntimeException(u"no Window"_ustr, getXWeak());
    }

    SwRect aLogBounds( GetBounds( *(GetMap()), GetFrame() ) ); // twip relative to document root
    tools::Rectangle aPixBounds( 0, 0, 0, 0 );
    if( GetFrame()->IsPageFrame() &&
        static_cast < const SwPageFrame * >( GetFrame() )->IsEmptyPage() )
    {
        OSL_ENSURE(GetShell().IsPreview(), "empty page accessible?");
        if (GetShell().IsPreview())
        {
            // adjust method call <GetMap()->GetPreviewPageSize()>
            sal_uInt16 nPageNum =
                static_cast < const SwPageFrame * >( GetFrame() )->GetPhyPageNum();
            aLogBounds.SSize( GetMap()->GetPreviewPageSize( nPageNum ) );
        }
    }
    if( !aLogBounds.IsEmpty() )
    {
        aPixBounds = GetMap()->CoreToPixel( aLogBounds );
        if( !pParent->IsRootFrame() && bRelative)
        {
            SwRect aParentLogBounds( GetBounds( *(GetMap()), pParent ) ); // twip rel to doc root
            Point aParentPixPos( GetMap()->CoreToPixel( aParentLogBounds ).TopLeft() );
            aPixBounds.Move( -aParentPixPos.getX(), -aParentPixPos.getY() );
        }
    }

    return vcl::unohelper::ConvertToAWTRect(aPixBounds);
}

awt::Point SAL_CALL SwAccessibleContext::getLocationOnScreen()
{
    SolarMutexGuard aGuard;

    awt::Rectangle aRect = getBoundsImpl(false);

    Point aPixPos(aRect.X, aRect.Y);

    vcl::Window *pWin = GetWindow();
    if (!pWin)
    {
        throw uno::RuntimeException(u"no Window"_ustr, getXWeak());
    }

    AbsoluteScreenPixelPoint aPixPosAbs = pWin->OutputToAbsoluteScreenPixel(aPixPos);
    awt::Point aPoint(aPixPosAbs.getX(), aPixPosAbs.getY());

    return aPoint;
}

void SAL_CALL SwAccessibleContext::grabFocus()
{
    SolarMutexGuard aGuard;

    ThrowIfDisposed();

    if( GetFrame()->IsFlyFrame() )
    {
        const SdrObject *pObj =
            static_cast < const SwFlyFrame * >( GetFrame() )->GetVirtDrawObj();
        if( pObj )
            Select( const_cast < SdrObject * >( pObj ), false );
    }
    else
    {
        const SwContentFrame *pCFrame = nullptr;
        if( GetFrame()->IsContentFrame() )
            pCFrame = static_cast< const SwContentFrame * >( GetFrame() );
        else if( GetFrame()->IsLayoutFrame() )
            pCFrame = static_cast< const SwLayoutFrame * >( GetFrame() )->ContainsContent();

        if( pCFrame && pCFrame->IsTextFrame() )
        {
            const SwTextFrame *pTextFrame = static_cast< const SwTextFrame * >( pCFrame );
            const SwTextNode *pTextNd = pTextFrame->GetTextNodeFirst();
            assert(pTextNd); // can it actually be null? probably not=>simplify
            if( pTextNd )
            {
                // create pam for selection
                SwPosition const aStartPos(pTextFrame->MapViewToModelPos(pTextFrame->GetOffset()));
                SwPaM aPaM( aStartPos );

                // set PaM at cursor shell
                Select( aPaM );
            }
        }
    }
}

sal_Int32 SAL_CALL SwAccessibleContext::getForeground()
{
    return sal_Int32(COL_BLACK);
}

sal_Int32 SAL_CALL SwAccessibleContext::getBackground()
{
    return sal_Int32(COL_WHITE);
}

void SwAccessibleContext::DisposeShape( const SdrObject *pObj,
                                ::accessibility::AccessibleShape *pAccImpl )
{
    ::rtl::Reference< ::accessibility::AccessibleShape > xAccImpl( pAccImpl );
    if( !xAccImpl.is() )
        xAccImpl = GetMap()->GetContextImpl( pObj, this );

    FireAccessibleEvent(AccessibleEventId::CHILD, uno::Any(uno::Reference<XAccessible>(xAccImpl)),
                        uno::Any());

    GetMap()->RemoveContext( pObj );
    xAccImpl->dispose();
}

void SwAccessibleContext::ScrolledInShape( ::accessibility::AccessibleShape *pAccImpl )
{
    if(nullptr == pAccImpl)
    {
        return ;
    }
    uno::Reference< XAccessible > xAcc( pAccImpl );
    FireAccessibleEvent(AccessibleEventId::CHILD, uno::Any(), uno::Any(xAcc));

    if( !pAccImpl->GetState( AccessibleStateType::FOCUSED ) )
        return;

    vcl::Window *pWin = GetWindow();
    if( pWin && pWin->HasFocus() )
    {
        pAccImpl->CommitChange(AccessibleEventId::STATE_CHANGED,
                               uno::Any(AccessibleStateType::FOCUSED), uno::Any(), -1);
    }
}

void SwAccessibleContext::Dispose(bool bRecursive, bool bCanSkipInvisible)
{
    SolarMutexGuard aGuard;

    OSL_ENSURE( GetFrame() && GetMap(), "already disposed" );
    OSL_ENSURE( GetMap()->GetVisArea() == GetVisArea(),
                "invalid visible area for dispose" );

    m_isDisposing = true;

    // dispose children
    if( bRecursive )
        DisposeChildren(GetFrame(), bRecursive, bCanSkipInvisible);

    // get parent
    rtl::Reference<SwAccessibleContext> xParent(GetWeakParent());
    uno::Reference < XAccessibleContext > xThis( this );

    // send child event at parent
    if( xParent.is() )
    {
        xParent->FireAccessibleEvent(AccessibleEventId::CHILD, uno::Any(xThis), uno::Any());
    }

    // set defunc state (it's not required to broadcast a state changed
    // event if the object is disposed afterwards)
    {
        std::scoped_lock aDefuncStateGuard( m_Mutex );
        m_isDefuncState = true;
    }

    OAccessible::dispose();

    RemoveFrameFromAccessibleMap();
    ClearFrame();
    m_pMap = nullptr;
    m_wMap.reset();

    m_isDisposing = false;
}

void SwAccessibleContext::DisposeChild( const SwAccessibleChild& rChildFrameOrObj,
                                        bool bRecursive, bool bCanSkipInvisible )
{
    SolarMutexGuard aGuard;

    if ( !bCanSkipInvisible ||
         rChildFrameOrObj.AlwaysIncludeAsChild() ||
         IsShowing( *(GetMap()), rChildFrameOrObj ) ||
         !SwAccessibleChild( GetFrame() ).IsVisibleChildrenOnly() )
    {
        // If the object could have existed before, then there is nothing to do,
        // because no wrapper exists now and therefore no one is interested to
        // get notified of the movement.
        if( rChildFrameOrObj.GetSwFrame() )
        {
            ::rtl::Reference< SwAccessibleContext > xAccImpl =
                    GetMap()->GetContextImpl( rChildFrameOrObj.GetSwFrame(), false );
            if (xAccImpl)
                xAccImpl->Dispose( bRecursive );
        }
        else if ( rChildFrameOrObj.GetDrawObject() )
        {
            ::rtl::Reference< ::accessibility::AccessibleShape > xAccImpl =
                    GetMap()->GetContextImpl( rChildFrameOrObj.GetDrawObject(),
                                              this, false );
            if (xAccImpl)
                DisposeShape( rChildFrameOrObj.GetDrawObject(),
                              xAccImpl.get() );
        }
        else if ( rChildFrameOrObj.GetWindow() )
        {
            uno::Reference< XAccessible > xAcc =
                                    rChildFrameOrObj.GetWindow()->GetAccessible();
            FireAccessibleEvent(AccessibleEventId::CHILD, uno::Any(xAcc), uno::Any());
        }
    }
    else if( bRecursive && rChildFrameOrObj.GetSwFrame() )
        DisposeChildren(rChildFrameOrObj.GetSwFrame(), bRecursive, bCanSkipInvisible);
}

void SwAccessibleContext::InvalidatePosOrSize( const SwRect& )
{
    SolarMutexGuard aGuard;

    OSL_ENSURE( GetFrame() && !GetFrame()->getFrameArea().IsEmpty(), "context should have a size" );

    bool bIsOldShowingState;
    bool bIsNewShowingState = IsShowing( *(GetMap()) );
    {
        std::scoped_lock aShowingStateGuard( m_Mutex );
        bIsOldShowingState = m_isShowingState;
        m_isShowingState = bIsNewShowingState;
    }

    if( bIsOldShowingState != bIsNewShowingState )
    {
        FireStateChangedEvent( AccessibleStateType::SHOWING,
                               bIsNewShowingState  );
    }
    else if( bIsNewShowingState )
    {
        // The frame stays visible -> broadcast event
        FireVisibleDataEvent();
    }

    // note: InvalidatePosOrSize must call InvalidateContent_ so that
    // SwAccessibleParagraph updates its portions, or dispose it
    // (see accmap.cxx: INVALID_CONTENT is contained in POS_CHANGED)
    if( !bIsNewShowingState &&
        SwAccessibleChild( GetParent() ).IsVisibleChildrenOnly() )
    {
        // this Dispose call was removed by IAccessibility2 implementation
        // without giving any reason why - without it we get stale
        // entries in SwAccessibleMap::mpFrameMap.
        Dispose(true);
    }
    else
    {
        InvalidateContent_( true );
    }
}

void SwAccessibleContext::InvalidateChildPosOrSize(
                    const SwAccessibleChild& rChildFrameOrObj,
                    const SwRect& rOldFrame )
{
    SolarMutexGuard aGuard;

    // this happens during layout, e.g. when a page is deleted and next page's
    // header/footer moves backward such an event is generated
    SAL_INFO_IF(rChildFrameOrObj.GetSwFrame() &&
            rChildFrameOrObj.GetSwFrame()->getFrameArea().IsEmpty(),
            "sw.a11y", "child context should have a size");

    if ( rChildFrameOrObj.AlwaysIncludeAsChild() )
    {
        // nothing to do;
        return;
    }

    const bool bVisibleChildrenOnly = SwAccessibleChild( GetFrame() ).IsVisibleChildrenOnly();
    const bool bNew = rOldFrame.IsEmpty() ||
                     ( rOldFrame.Left() == 0 && rOldFrame.Top() == 0 );
    if( IsShowing( *(GetMap()), rChildFrameOrObj ) )
    {
        // If the object could have existed before, then there is nothing to do,
        // because no wrapper exists now and therefore no one is interested to
        // get notified of the movement.
        if( bNew || (bVisibleChildrenOnly && !IsShowing( rOldFrame )) )
        {
            if( rChildFrameOrObj.GetSwFrame() )
            {
                // The frame becomes visible. A child event must be send.
                ::rtl::Reference< SwAccessibleContext > xAccImpl =
                    GetMap()->GetContextImpl( rChildFrameOrObj.GetSwFrame() );
                xAccImpl->ScrolledIn();
            }
            else if ( rChildFrameOrObj.GetDrawObject() )
            {
                ::rtl::Reference< ::accessibility::AccessibleShape > xAccImpl =
                        GetMap()->GetContextImpl( rChildFrameOrObj.GetDrawObject(),
                                                  this );
                // #i37790#
                if ( xAccImpl.is() )
                {
                    ScrolledInShape( xAccImpl.get() );
                }
                else
                {
                    OSL_FAIL( "<SwAccessibleContext::InvalidateChildPosOrSize(..)> - no accessible shape found." );
                }
            }
            else if ( rChildFrameOrObj.GetWindow() )
            {
                FireAccessibleEvent(AccessibleEventId::CHILD, uno::Any(),
                                    uno::Any(uno::Reference<XAccessible>(
                                        rChildFrameOrObj.GetWindow()->GetAccessible())));
            }
        }
    }
    else
    {
        // If the frame was visible before, then a child event for the parent
        // needs to be send. However, there is no wrapper existing, and so
        // no notifications for grandchildren are required. If the are
        // grandgrandchildren, they would be notified by the layout.
        if( bVisibleChildrenOnly &&
            !bNew && IsShowing( rOldFrame ) )
        {
            if( rChildFrameOrObj.GetSwFrame() )
            {
                ::rtl::Reference< SwAccessibleContext > xAccImpl =
                    GetMap()->GetContextImpl( rChildFrameOrObj.GetSwFrame() );
                xAccImpl->SetParent( this );
                xAccImpl->Dispose( true );
            }
            else if ( rChildFrameOrObj.GetDrawObject() )
            {
                ::rtl::Reference< ::accessibility::AccessibleShape > xAccImpl =
                        GetMap()->GetContextImpl( rChildFrameOrObj.GetDrawObject(),
                                                  this );
                DisposeShape( rChildFrameOrObj.GetDrawObject(),
                          xAccImpl.get() );
            }
            else if ( rChildFrameOrObj.GetWindow() )
            {
                OSL_FAIL( "<SwAccessibleContext::InvalidateChildPosOrSize(..)> - not expected to handle dispose of child of type <vcl::Window>." );
            }
        }
    }
}

void SwAccessibleContext::InvalidateContent()
{
    SolarMutexGuard aGuard;

    InvalidateContent_( false );
}

void SwAccessibleContext::InvalidateCursorPos()
{
    SolarMutexGuard aGuard;

    InvalidateCursorPos_();
}

void SwAccessibleContext::InvalidateFocus()
{
    SolarMutexGuard aGuard;

    InvalidateFocus_();
}

// #i27301# - use new type definition for <_nStates>
void SwAccessibleContext::InvalidateStates( AccessibleStates _nStates )
{
    if( !GetMap() )
        return;

    SwViewShell& rVSh = GetMap()->GetShell();
    if( _nStates & AccessibleStates::EDITABLE )
    {
        bool bIsOldEditableState;
        bool bIsNewEditableState = IsEditable(rVSh);
        {
            std::scoped_lock aGuard( m_Mutex );
            bIsOldEditableState = m_isEditableState;
            m_isEditableState = bIsNewEditableState;
        }

        if( bIsOldEditableState != bIsNewEditableState )
            FireStateChangedEvent( AccessibleStateType::EDITABLE,
                                   bIsNewEditableState  );
    }
    if( _nStates & AccessibleStates::OPAQUE )
    {
        bool bIsOldOpaqueState;
        bool bIsNewOpaqueState = IsOpaque(rVSh);
        {
            std::scoped_lock aGuard( m_Mutex );
            bIsOldOpaqueState = m_isOpaqueState;
            m_isOpaqueState = bIsNewOpaqueState;
        }

        if( bIsOldOpaqueState != bIsNewOpaqueState )
            FireStateChangedEvent( AccessibleStateType::OPAQUE,
                                   bIsNewOpaqueState  );
    }

    InvalidateChildrenStates( GetFrame(), _nStates );
}

void SwAccessibleContext::InvalidateRelation( sal_uInt16 nType )
{
    FireAccessibleEvent(nType, uno::Any(), uno::Any());
}

/** #i27301# - text selection has changed */
void SwAccessibleContext::InvalidateTextSelection()
{
    FireAccessibleEvent(AccessibleEventId::TEXT_SELECTION_CHANGED, uno::Any(), uno::Any());
}

/** #i88069# - attributes has changed */
void SwAccessibleContext::InvalidateAttr()
{
    FireAccessibleEvent(AccessibleEventId::TEXT_ATTRIBUTE_CHANGED, uno::Any(), uno::Any());
}

bool SwAccessibleContext::HasCursor()
{
    return false;
}

bool SwAccessibleContext::Select( SwPaM *pPaM, SdrObject *pObj,
                                  bool bAdd )
{
    SwCursorShell* pCursorShell = GetCursorShell();
    if( !pCursorShell )
        return false;

    SwFEShell* pFEShell = dynamic_cast<SwFEShell*>(pCursorShell);
    // Get rid of activated OLE object
    if( pFEShell )
        pFEShell->FinishOLEObj();

    SwWrtShell* pWrtShell = dynamic_cast<SwWrtShell*>(pCursorShell);

    bool bRet = false;
    if( pObj )
    {
        if( pFEShell )
        {
            sal_uInt8 nFlags = bAdd ? SW_ADD_SELECT : 0;
            pFEShell->SelectObj( Point(), nFlags, pObj );
            bRet = true;
        }
    }
    else if( pPaM )
    {
        // Get rid of frame selection. If there is one, make text cursor
        // visible again.
        bool bCallShowCursor = false;
        if( pFEShell && (pFEShell->IsFrameSelected() ||
                         pFEShell->GetSelectedObjCount()) )
        {
            Point aPt( LONG_MIN, LONG_MIN );
            pFEShell->SelectObj( aPt );
            bCallShowCursor = true;
        }
        pCursorShell->KillPams();
        if( pWrtShell && pPaM->HasMark() )
            // We have to do this or SwWrtShell can't figure out that it needs
            // to kill the selection later, when the user moves the cursor.
            pWrtShell->SttSelect();
        pCursorShell->SetSelection( *pPaM );
        if( pPaM->HasMark() && *pPaM->GetPoint() == *pPaM->GetMark())
            // Setting a "Selection" that starts and ends at the same spot
            // should remove the selection rather than create an empty one, so
            // that we get defined behavior if accessibility sets the cursor
            // later.
            pCursorShell->ClearMark();
        if( bCallShowCursor )
            pCursorShell->ShowCursor();
        bRet = true;
    }

    return bRet;
}

OUString SwAccessibleContext::GetResource(TranslateId pResId,
                                          const OUString *pArg1,
                                          const OUString *pArg2)
{
    OUString sStr = SwResId(pResId);

    if( pArg1 )
    {
        sStr = sStr.replaceFirst( "$(ARG1)", *pArg1 );
    }
    if( pArg2 )
    {
        sStr = sStr.replaceFirst( "$(ARG2)", *pArg2 );
    }

    return sStr;
}

void SwAccessibleContext::RemoveFrameFromAccessibleMap()
{
    assert(m_refCount > 0); // must be alive to do this without using m_wMap
    if (m_isRegisteredAtAccessibleMap && GetFrame() && GetMap())
        GetMap()->RemoveContext( GetFrame() );
}

bool SwAccessibleContext::HasAdditionalAccessibleChildren()
{
    bool bRet( false );

    if ( GetFrame()->IsTextFrame() )
    {
        SwPostItMgr* pPostItMgr = GetMap()->GetShell().GetPostItMgr();
        if ( pPostItMgr && pPostItMgr->HasNotes() && pPostItMgr->ShowNotes() )
        {
            bRet = pPostItMgr->HasFrameConnectedSidebarWins( *(GetFrame()) );
        }
    }

    return bRet;
}

/** #i88070# - get additional accessible child by index */
vcl::Window* SwAccessibleContext::GetAdditionalAccessibleChild( const sal_Int32 nIndex )
{
    vcl::Window* pAdditionalAccessibleChild( nullptr );

    if ( GetFrame()->IsTextFrame() )
    {
        SwPostItMgr* pPostItMgr = GetMap()->GetShell().GetPostItMgr();
        if ( pPostItMgr && pPostItMgr->HasNotes() && pPostItMgr->ShowNotes() )
        {
            pAdditionalAccessibleChild =
                    pPostItMgr->GetSidebarWinForFrameByIndex( *(GetFrame()), nIndex );
        }
    }

    return pAdditionalAccessibleChild;
}

/** #i88070# - get all additional accessible children */
std::vector<vcl::Window*> SwAccessibleContext::GetAdditionalAccessibleChildren()
{
    if ( GetFrame()->IsTextFrame() )
    {
        SwPostItMgr* pPostItMgr = GetMap()->GetShell().GetPostItMgr();
        if ( pPostItMgr && pPostItMgr->HasNotes() && pPostItMgr->ShowNotes() )
        {
            return pPostItMgr->GetAllSidebarWinForFrame(*(GetFrame()));
        }
    }

    return {};
}

bool SwAccessibleContext::SetSelectedState(bool const bSelected)
{
    if (m_isSelectedInDoc != bSelected)
    {
        m_isSelectedInDoc = bSelected;
        FireStateChangedEvent( AccessibleStateType::SELECTED, bSelected );
        return true;
    }
    return false;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
