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

#include <accessibility/accessiblelistboxentry.hxx>
#include <accessibility/accessiblelistbox.hxx>
#include <vcl/toolkit/treelistbox.hxx>
#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/accessibility/AccessibleRelationType.hpp>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/datatransfer/clipboard/XClipboard.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <i18nlangtag/languagetag.hxx>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <vcl/unohelp.hxx>
#include <vcl/unohelp2.hxx>
#include <unotools/accessiblerelationsethelper.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <svdata.hxx>
#include <strings.hrc>

using namespace ::com::sun::star::accessibility;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star;
using namespace ::comphelper;

AccessibleListBoxEntry::AccessibleListBoxEntry( SvTreeListBox& _rListBox,
                                                SvTreeListEntry& rEntry,
                                                AccessibleListBox & rListBox)
    : AccessibleListBoxEntry_BASE()

    , m_pTreeListBox( &_rListBox )
    , m_pSvLBoxEntry(&rEntry)
    , m_wListBox(&rListBox)
{
    m_pTreeListBox->AddEventListener( LINK( this, AccessibleListBoxEntry, WindowEventListener ) );
    _rListBox.FillEntryPath( m_pSvLBoxEntry, m_aEntryPath );
}

IMPL_LINK( AccessibleListBoxEntry, WindowEventListener, VclWindowEvent&, rEvent, void )
{
    OSL_ENSURE( rEvent.GetWindow() , "AccessibleListBoxEntry::WindowEventListener: no event window!" );
    OSL_ENSURE( rEvent.GetWindow() == m_pTreeListBox, "AccessibleListBoxEntry::WindowEventListener: where did this come from?" );

    if ( m_pTreeListBox == nullptr )
        return;

    switch ( rEvent.GetId() )
    {
        case VclEventId::CheckboxToggle:
        {
            // assert this object is represented as a checkbox on a11y layer (LABEL role is used for
            // SvButtonState::Tristate, s. AccessibleListBoxEntry::getAccessibleRole)
            assert(getAccessibleRole() == AccessibleRole::CHECK_BOX
                   || getAccessibleRole() == AccessibleRole::LABEL);
            Any aOldValue;
            Any aNewValue;
            if (getAccessibleStateSet() & AccessibleStateType::CHECKED)
                aNewValue <<= AccessibleStateType::CHECKED;
            else
                aOldValue <<= AccessibleStateType::CHECKED;

            NotifyAccessibleEvent(AccessibleEventId::STATE_CHANGED, aOldValue, aNewValue);
            break;
        }
        case  VclEventId::ObjectDying :
        {
            dispose();
            break;
        }
        default: break;
    }
}

tools::Rectangle AccessibleListBoxEntry::GetBoundingBox_Impl() const
{
    tools::Rectangle aRect;
    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if ( pEntry )
    {
        aRect = m_pTreeListBox->GetBoundingRect( pEntry );
        SvTreeListEntry* pParent = m_pTreeListBox->GetParent( pEntry );
        if ( pParent )
        {
            // position relative to parent entry
            Point aTopLeft = aRect.TopLeft();
            aTopLeft -= m_pTreeListBox->GetBoundingRect( pParent ).TopLeft();
            aRect = tools::Rectangle( aTopLeft, aRect.GetSize() );
        }
    }

    return aRect;
}

bool AccessibleListBoxEntry::IsShowing_Impl() const
{
    rtl::Reference<comphelper::OAccessible> pParent = implGetParentAccessible();

    bool bShowing = false;
    if (pParent.is())
    {
        bShowing = GetBoundingBox_Impl().Overlaps(
            vcl::unohelper::ConvertToVCLRect(pParent->getBounds()));
    }

    return bShowing;
}

void AccessibleListBoxEntry::CheckActionIndex(sal_Int32 nIndex)
{
    if (nIndex < 0 || nIndex >= getAccessibleActionCount())
        throw css::lang::IndexOutOfBoundsException();
}

OUString AccessibleListBoxEntry::implGetText()
{
    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if (pEntry)
        return SvTreeListBox::SearchEntryTextWithHeadTitle(pEntry);
    return OUString();
}

Locale AccessibleListBoxEntry::implGetLocale()
{
    return Application::GetSettings().GetUILanguageTag().getLocale();
}
void AccessibleListBoxEntry::implGetSelection( sal_Int32& nStartIndex, sal_Int32& nEndIndex )
{
    nStartIndex = 0;
    nEndIndex = 0;
}

css::awt::Rectangle AccessibleListBoxEntry::implGetBounds()
{
    return vcl::unohelper::ConvertToAWTRect(GetBoundingBox_Impl());
}


// XComponent

void SAL_CALL AccessibleListBoxEntry::disposing()
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    Reference< XAccessible > xKeepAlive( this );

    OAccessible::disposing();

    // clean up
    m_wListBox.clear();

    if ( m_pTreeListBox )
        m_pTreeListBox->RemoveEventListener( LINK( this, AccessibleListBoxEntry, WindowEventListener ) );
    m_pTreeListBox = nullptr;
}

uno::Any SAL_CALL AccessibleListBoxEntry::queryInterface(const uno::Type& rType)
{
    if (rType == cppu::UnoType<XAccessibleText>::get())
    {
        // don't support XAccessibleText interface if there's no text item
        // (e.g. for IconView items), as screen readers may give text a higher
        // priority than accessible name and just say something like "blank" otherwise
        SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath(m_aEntryPath);
        if (pEntry && !pEntry->GetFirstItem(SvLBoxItemType::String))
            return uno::Any();
    }

    return AccessibleListBoxEntry_BASE::queryInterface(rType);
}

// XServiceInfo

OUString SAL_CALL AccessibleListBoxEntry::getImplementationName()
{
    return u"com.sun.star.comp.svtools.AccessibleTreeListBoxEntry"_ustr;
}

Sequence< OUString > SAL_CALL AccessibleListBoxEntry::getSupportedServiceNames()
{
    return {u"com.sun.star.accessibility.AccessibleContext"_ustr};
}

sal_Bool SAL_CALL AccessibleListBoxEntry::supportsService( const OUString& _rServiceName )
{
    return cppu::supportsService(this, _rServiceName);
}

// XAccessibleContext

sal_Int64 SAL_CALL AccessibleListBoxEntry::getAccessibleChildCount(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();
    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    sal_Int32 nCount = 0;
    if ( pEntry )
        nCount = m_pTreeListBox->GetLevelChildCount( pEntry );

    return nCount;
}

Reference< XAccessible > SAL_CALL AccessibleListBoxEntry::getAccessibleChild( sal_Int64 i )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();

    SvTreeListEntry* pEntry = GetRealChild(i);
    if ( !pEntry )
        throw IndexOutOfBoundsException();

    rtl::Reference<AccessibleListBox> xListBox(m_wListBox);
    assert(xListBox.is());

    return xListBox->implGetAccessible(*pEntry);
}

rtl::Reference<comphelper::OAccessible> AccessibleListBoxEntry::implGetParentAccessible() const
{
    rtl::Reference<comphelper::OAccessible> pParent;
    assert( m_aEntryPath.size() ); // invalid path
    if ( m_aEntryPath.size() == 1 )
    {   // we're a top level entry
        // -> our parent is the tree listbox itself
        if ( m_pTreeListBox )
            pParent = m_pTreeListBox->GetAccessible();
    }
    else
    {   // we have an entry as parent -> get its accessible

        // shorten our access path by one
        std::deque< sal_Int32 > aParentPath( m_aEntryPath );
        aParentPath.pop_back();

        // get the entry for this shortened access path
        SvTreeListEntry* pParentEntry = m_pTreeListBox->GetEntryFromPath( aParentPath );
        assert(pParentEntry && "AccessibleListBoxEntry::implGetParentAccessible: could not obtain a parent entry!");
        if ( pParentEntry )
        {
            rtl::Reference<AccessibleListBox> xListBox(m_wListBox);
            assert(xListBox.is());
            return xListBox->implGetAccessible(*pParentEntry);
            // the AccessibleListBoxEntry class will create its parent
            // when needed
        }
    }

    return pParent;
}


Reference< XAccessible > SAL_CALL AccessibleListBoxEntry::getAccessibleParent(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();

    return implGetParentAccessible( );
}

sal_Int64 SAL_CALL AccessibleListBoxEntry::getAccessibleIndexInParent(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );

    OSL_ENSURE( !m_aEntryPath.empty(), "empty path" );
    return m_aEntryPath.empty() ? -1 : m_aEntryPath.back();
}

sal_Int32 AccessibleListBoxEntry::GetRoleType() const
{
    sal_Int32 nCase = 0;
    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntry(0);
    if ( pEntry )
    {
        if( pEntry->HasChildrenOnDemand() || m_pTreeListBox->GetChildCount(pEntry) > 0  )
        {
            nCase = 1;
            return nCase;
        }
    }

    bool bHasButtons = (m_pTreeListBox->GetStyle() & WB_HASBUTTONS)!=0;
    if( !(m_pTreeListBox->GetTreeFlags() & SvTreeFlags::CHKBTN) )
    {
        if( bHasButtons )
            nCase = 1;
    }
    else
    {
        if( bHasButtons )
            nCase = 2;
        else
            nCase = 3;
    }
    return nCase;
}

sal_Int16 SAL_CALL AccessibleListBoxEntry::getAccessibleRole(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    SvTreeListBox* pBox = m_pTreeListBox;
    if(!pBox)
        return AccessibleRole::UNKNOWN;

    SvTreeFlags treeFlag = pBox->GetTreeFlags();
    if(treeFlag & SvTreeFlags::CHKBTN )
    {
        SvTreeListEntry* pEntry = pBox->GetEntryFromPath( m_aEntryPath );
        SvButtonState eState = pBox->GetCheckButtonState( pEntry );
        switch( eState )
        {
        case SvButtonState::Checked:
        case SvButtonState::Unchecked:
            return AccessibleRole::CHECK_BOX;
        case SvButtonState::Tristate:
        default:
            return AccessibleRole::LABEL;
        }
    }
    if (GetRoleType() == 0)
        return AccessibleRole::LIST_ITEM;
    else
        //o is: return AccessibleRole::LABEL;
        return AccessibleRole::TREE_ITEM;
}

OUString SAL_CALL AccessibleListBoxEntry::getAccessibleDescription(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    return OUString();
}

OUString SAL_CALL AccessibleListBoxEntry::getAccessibleName(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    if (SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath(m_aEntryPath))
    {
        OUString sName = pEntry->GetAccessibleName();
        if (!sName.isEmpty())
            return sName;
    }

    return implGetText();
}

Reference< XAccessibleRelationSet > SAL_CALL AccessibleListBoxEntry::getAccessibleRelationSet(  )
{
    rtl::Reference<comphelper::OAccessible> pParent;
    if ( m_aEntryPath.size() > 1 ) // not a root entry
        pParent = implGetParentAccessible();
    if (!pParent.is())
        return nullptr;

    rtl::Reference<utl::AccessibleRelationSetHelper> pRelationSetHelper = new utl::AccessibleRelationSetHelper;
    Sequence<Reference<XAccessible>> aSequence{ pParent };
    pRelationSetHelper->AddRelation(
        AccessibleRelation( AccessibleRelationType_NODE_CHILD_OF, aSequence ) );
    return pRelationSetHelper;
}

sal_Int64 SAL_CALL AccessibleListBoxEntry::getAccessibleStateSet(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );

    sal_Int64 nStateSet = 0;

    if (isAlive())
    {
        switch(getAccessibleRole())
        {
            case AccessibleRole::LABEL:
                nStateSet |= AccessibleStateType::TRANSIENT;
                nStateSet |= AccessibleStateType::SELECTABLE;
                nStateSet |= AccessibleStateType::ENABLED;
                if (m_pTreeListBox->IsInplaceEditingEnabled())
                    nStateSet |= AccessibleStateType::EDITABLE;
                if (IsShowing_Impl())
                    nStateSet |= AccessibleStateType::SHOWING;
                break;
            case AccessibleRole::CHECK_BOX:
                nStateSet |= AccessibleStateType::TRANSIENT;
                nStateSet |= AccessibleStateType::SELECTABLE;
                nStateSet |= AccessibleStateType::ENABLED;
                if (IsShowing_Impl())
                    nStateSet |= AccessibleStateType::SHOWING;
                break;
        }
        SvTreeListEntry *pEntry = m_pTreeListBox->GetEntryFromPath(m_aEntryPath);
        if (pEntry)
            m_pTreeListBox->FillAccessibleEntryStateSet(pEntry, nStateSet);
    }
    else
        nStateSet |= AccessibleStateType::DEFUNC;

    return nStateSet;
}

Locale SAL_CALL AccessibleListBoxEntry::getLocale(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    return implGetLocale();
}

// XAccessibleComponent

Reference< XAccessible > SAL_CALL AccessibleListBoxEntry::getAccessibleAtPoint( const awt::Point& _aPoint )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();
    SvTreeListEntry* pEntry
        = m_pTreeListBox->GetEntry(vcl::unohelper::ConvertToVCLPoint(_aPoint));
    if ( !pEntry )
        throw RuntimeException(u"AccessibleListBoxEntry::getAccessibleAtPoint - pEntry cannot be empty!"_ustr);

    rtl::Reference<AccessibleListBox> xListBox(m_wListBox);
    assert(xListBox.is());
    rtl::Reference<AccessibleListBoxEntry> pAccEntry = xListBox->implGetAccessible(*pEntry);
    tools::Rectangle aRect = pAccEntry->GetBoundingBox_Impl();
    if (aRect.Contains(vcl::unohelper::ConvertToVCLPoint(_aPoint))
        && pAccEntry->implGetParentAccessible().get() == this)
        return pAccEntry;

    return {};
}

void SAL_CALL AccessibleListBoxEntry::grabFocus(  )
{
    // do nothing, because no focus for each item
}

sal_Int32 AccessibleListBoxEntry::getForeground(    )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    sal_Int32 nColor = 0;
    Reference< XAccessible > xParent = getAccessibleParent();
    if ( xParent.is() )
    {
        Reference< XAccessibleComponent > xParentComp( xParent->getAccessibleContext(), UNO_QUERY );
        if ( xParentComp.is() )
            nColor = xParentComp->getForeground();
    }

    return nColor;
}

sal_Int32 AccessibleListBoxEntry::getBackground(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    sal_Int32 nColor = 0;
    Reference< XAccessible > xParent = getAccessibleParent();
    if ( xParent.is() )
    {
        Reference< XAccessibleComponent > xParentComp( xParent->getAccessibleContext(), UNO_QUERY );
        if ( xParentComp.is() )
            nColor = xParentComp->getBackground();
    }

    return nColor;
}

// XAccessibleText


awt::Rectangle SAL_CALL AccessibleListBoxEntry::getCharacterBounds( sal_Int32 nIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    if ( !implIsValidIndex( nIndex, implGetText().getLength() ) )
        throw IndexOutOfBoundsException();

    awt::Rectangle aBounds( 0, 0, 0, 0 );
    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if ( pEntry )
    {
        vcl::ControlLayoutData aLayoutData;
        tools::Rectangle aItemRect = GetBoundingBox_Impl();
        m_pTreeListBox->RecordLayoutData( &aLayoutData, aItemRect );
        tools::Rectangle aCharRect = aLayoutData.GetCharacterBounds( nIndex );
        aCharRect.Move( -aItemRect.Left(), -aItemRect.Top() );
        aBounds = vcl::unohelper::ConvertToAWTRect(aCharRect);
    }

    return aBounds;
}

sal_Int32 SAL_CALL AccessibleListBoxEntry::getIndexAtPoint( const awt::Point& aPoint )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    if(aPoint.X==0 && aPoint.Y==0) return 0;

    sal_Int32 nIndex = -1;
    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if ( pEntry )
    {
        vcl::ControlLayoutData aLayoutData;
        tools::Rectangle aItemRect = GetBoundingBox_Impl();
        m_pTreeListBox->RecordLayoutData( &aLayoutData, aItemRect );
        Point aPnt(vcl::unohelper::ConvertToVCLPoint(aPoint));
        aPnt += aItemRect.TopLeft();
        nIndex = aLayoutData.GetIndexForPoint( aPnt );
    }

    return nIndex;
}

sal_Bool SAL_CALL AccessibleListBoxEntry::copyText( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();

    OUString sText = implGetText();
    if  ( ( 0 > nStartIndex ) || ( sText.getLength() <= nStartIndex )
        || ( 0 > nEndIndex ) || ( sText.getLength() <= nEndIndex ) )
        throw IndexOutOfBoundsException();

    if (!m_pTreeListBox)
        return false;

    sal_Int32 nLen = nEndIndex - nStartIndex + 1;
    css::uno::Reference<css::datatransfer::clipboard::XClipboard> xClipBoard = m_pTreeListBox->GetClipboard();
    vcl::unohelper::TextDataObject::CopyStringTo(sText.copy(nStartIndex, nLen), xClipBoard);

    return true;
}

sal_Bool SAL_CALL AccessibleListBoxEntry::scrollSubstringTo( sal_Int32, sal_Int32, AccessibleScrollType )
{
    return false;
}

// XAccessibleAction

sal_Int32 SAL_CALL AccessibleListBoxEntry::getAccessibleActionCount(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );

    // three actions supported
    SvTreeFlags treeFlag = m_pTreeListBox->GetTreeFlags();
    bool bHasButtons = (m_pTreeListBox->GetStyle() & WB_HASBUTTONS)!=0;
    if( (treeFlag & SvTreeFlags::CHKBTN) && !bHasButtons)
    {
        sal_Int16 role = getAccessibleRole();
        if ( role == AccessibleRole::CHECK_BOX )
            return 2;
        else if ( role == AccessibleRole::LABEL )
            return 0;
    }
    else
        return 1;
    return 0;
}

sal_Bool SAL_CALL AccessibleListBoxEntry::doAccessibleAction( sal_Int32 nIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    bool bRet = false;
    CheckActionIndex(nIndex);
    ensureAlive();

    SvTreeFlags treeFlag = m_pTreeListBox->GetTreeFlags();
    if( nIndex == 0 && (treeFlag & SvTreeFlags::CHKBTN) )
    {
        if(getAccessibleRole() == AccessibleRole::CHECK_BOX)
        {
            SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
            SvButtonState state = m_pTreeListBox->GetCheckButtonState( pEntry );
            if ( state == SvButtonState::Checked )
                m_pTreeListBox->SetCheckButtonState(pEntry, SvButtonState::Unchecked);
            else if (state == SvButtonState::Unchecked)
                m_pTreeListBox->SetCheckButtonState(pEntry, SvButtonState::Checked);
        }
    }
    else if( (nIndex == 1 && (treeFlag & SvTreeFlags::CHKBTN) ) || (nIndex == 0) )
    {
        SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
        if ( pEntry )
        {
            if ( m_pTreeListBox->IsExpanded( pEntry ) )
                m_pTreeListBox->Collapse( pEntry );
            else
                m_pTreeListBox->Expand( pEntry );
            bRet = true;
        }
    }

    return bRet;
}

OUString SAL_CALL AccessibleListBoxEntry::getAccessibleActionDescription( sal_Int32 nIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    CheckActionIndex(nIndex);
    ensureAlive();

    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    SvButtonState state = m_pTreeListBox->GetCheckButtonState( pEntry );
    SvTreeFlags treeFlag = m_pTreeListBox->GetTreeFlags();
    if(nIndex == 0 && (treeFlag & SvTreeFlags::CHKBTN))
    {
        if(getAccessibleRole() == AccessibleRole::CHECK_BOX)
        {
            if ( state == SvButtonState::Checked )
                return u"UnCheck"_ustr;
            else if (state == SvButtonState::Unchecked)
                return u"Check"_ustr;
        }
        else
        {
            //Sometimes, a List or Tree may have both checkbox and label at the same time
            return OUString();
        }
    }
    else if( (nIndex == 1 && (treeFlag & SvTreeFlags::CHKBTN)) || nIndex == 0 )
    {
        if( pEntry && (pEntry->HasChildren() || pEntry->HasChildrenOnDemand()) )
            return m_pTreeListBox->IsExpanded( pEntry ) ?
            VclResId(STR_SVT_ACC_ACTION_COLLAPSE) :
            VclResId(STR_SVT_ACC_ACTION_EXPAND);
        return OUString();

    }
    throw IndexOutOfBoundsException();
}

Reference< XAccessibleKeyBinding > AccessibleListBoxEntry::getAccessibleActionKeyBinding( sal_Int32 nIndex )
{
    Reference< XAccessibleKeyBinding > xRet;
    CheckActionIndex(nIndex);
    // ... which key?
    return xRet;
}

// XAccessibleSelection

void SAL_CALL AccessibleListBoxEntry::selectAccessibleChild( sal_Int64 nChildIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    if (nChildIndex < 0 || nChildIndex >= getAccessibleChildCount())
        throw IndexOutOfBoundsException();

    SvTreeListEntry* pEntry = GetRealChild(nChildIndex);
    if ( !pEntry )
        throw IndexOutOfBoundsException();

    m_pTreeListBox->Select( pEntry );
}

sal_Bool SAL_CALL AccessibleListBoxEntry::isAccessibleChildSelected( sal_Int64 nChildIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    if (nChildIndex < 0 || nChildIndex >= getAccessibleChildCount())
        throw IndexOutOfBoundsException();

    SvTreeListEntry* pParent = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntry( pParent, nChildIndex );
    if ( !pEntry )
        throw IndexOutOfBoundsException();

    return m_pTreeListBox->IsSelected( pEntry );
}

void SAL_CALL AccessibleListBoxEntry::clearAccessibleSelection(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    SvTreeListEntry* pParent = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if ( !pParent )
        throw RuntimeException(u"AccessibleListBoxEntry::clearAccessibleSelection - pParent cannot be empty!"_ustr);
    sal_Int32 nCount = m_pTreeListBox->GetLevelChildCount( pParent );
    for ( sal_Int32 i = 0; i < nCount; ++i )
    {
        SvTreeListEntry* pEntry = m_pTreeListBox->GetEntry( pParent, i );
        if ( m_pTreeListBox->IsSelected( pEntry ) )
            m_pTreeListBox->Select( pEntry, false );
    }
}

void SAL_CALL AccessibleListBoxEntry::selectAllAccessibleChildren(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    SvTreeListEntry* pParent = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if ( !pParent )
        throw RuntimeException(u"AccessibleListBoxEntry::selectAllAccessibleChildren - pParent cannot be empty!"_ustr);
    sal_Int32 nCount = m_pTreeListBox->GetLevelChildCount( pParent );
    for ( sal_Int32 i = 0; i < nCount; ++i )
    {
        SvTreeListEntry* pEntry = m_pTreeListBox->GetEntry( pParent, i );
        if ( !m_pTreeListBox->IsSelected( pEntry ) )
            m_pTreeListBox->Select( pEntry );
    }
}

sal_Int64 SAL_CALL AccessibleListBoxEntry::getSelectedAccessibleChildCount(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    sal_Int64 nSelCount = 0;

    SvTreeListEntry* pParent = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if ( !pParent )
        throw RuntimeException(u"AccessibleListBoxEntry::getSelectedAccessibleChildCount - pParent cannot be empty!"_ustr);
    sal_Int32 nCount = m_pTreeListBox->GetLevelChildCount( pParent );
    for (sal_Int32 i = 0; i < nCount; ++i )
    {
        SvTreeListEntry* pEntry = m_pTreeListBox->GetEntry( pParent, i );
        if ( m_pTreeListBox->IsSelected( pEntry ) )
            ++nSelCount;
    }

    return nSelCount;
}

Reference< XAccessible > SAL_CALL AccessibleListBoxEntry::getSelectedAccessibleChild( sal_Int64 nSelectedChildIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    if ( nSelectedChildIndex < 0 || nSelectedChildIndex >= getSelectedAccessibleChildCount() )
        throw IndexOutOfBoundsException();

    Reference< XAccessible > xChild;
    sal_Int64 nSelCount = 0;

    SvTreeListEntry* pParent = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if ( !pParent )
        throw RuntimeException(u"AccessibleListBoxEntry::getSelectedAccessibleChild - pParent cannot be empty!"_ustr);
    sal_Int32 nCount = m_pTreeListBox->GetLevelChildCount( pParent );
    for (sal_Int32 i = 0; i < nCount; ++i )
    {
        SvTreeListEntry* pEntry = m_pTreeListBox->GetEntry( pParent, i );
        if ( m_pTreeListBox->IsSelected( pEntry ) )
            ++nSelCount;

        if ( nSelCount == ( nSelectedChildIndex + 1 ) )
        {
            rtl::Reference<AccessibleListBox> xListBox(m_wListBox);
            assert(xListBox.is());
            xChild = xListBox->implGetAccessible(*pEntry).get();
            break;
        }
    }

    return xChild;
}

void SAL_CALL AccessibleListBoxEntry::deselectAccessibleChild( sal_Int64 nSelectedChildIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );

    ensureAlive();

    if (nSelectedChildIndex < 0 || nSelectedChildIndex >= getAccessibleChildCount())
        throw IndexOutOfBoundsException();

    SvTreeListEntry* pParent = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    SvTreeListEntry* pEntry = m_pTreeListBox->GetEntry( pParent, nSelectedChildIndex );
    if ( !pEntry )
        throw IndexOutOfBoundsException();

    m_pTreeListBox->Select( pEntry, false );
}
sal_Int32 SAL_CALL AccessibleListBoxEntry::getCaretPosition(  )
{
    return -1;
}
sal_Bool SAL_CALL AccessibleListBoxEntry::setCaretPosition ( sal_Int32 nIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();

    if ( !implIsValidRange( nIndex, nIndex, implGetText().getLength() ) )
        throw IndexOutOfBoundsException();

    return false;
}
sal_Unicode SAL_CALL AccessibleListBoxEntry::getCharacter( sal_Int32 nIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return OCommonAccessibleText::implGetCharacter( implGetText(), nIndex );
}
css::uno::Sequence< css::beans::PropertyValue > SAL_CALL AccessibleListBoxEntry::getCharacterAttributes( sal_Int32 nIndex, const css::uno::Sequence< OUString >& )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();

    OUString sText( implGetText() );

    if ( !implIsValidIndex( nIndex, sText.getLength() ) )
        throw IndexOutOfBoundsException();

    return css::uno::Sequence< css::beans::PropertyValue >();
}
sal_Int32 SAL_CALL AccessibleListBoxEntry::getCharacterCount(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return implGetText().getLength();
}

OUString SAL_CALL AccessibleListBoxEntry::getSelectedText(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return OUString();
}
sal_Int32 SAL_CALL AccessibleListBoxEntry::getSelectionStart(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return 0;
}
sal_Int32 SAL_CALL AccessibleListBoxEntry::getSelectionEnd(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return 0;
}
sal_Bool SAL_CALL AccessibleListBoxEntry::setSelection( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();

    if ( !implIsValidRange( nStartIndex, nEndIndex, implGetText().getLength() ) )
        throw IndexOutOfBoundsException();

    return false;
}
OUString SAL_CALL AccessibleListBoxEntry::getText(  )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return implGetText(  );
}
OUString SAL_CALL AccessibleListBoxEntry::getTextRange( sal_Int32 nStartIndex, sal_Int32 nEndIndex )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return OCommonAccessibleText::implGetTextRange( implGetText(), nStartIndex, nEndIndex );
}
css::accessibility::TextSegment SAL_CALL AccessibleListBoxEntry::getTextAtIndex( sal_Int32 nIndex, sal_Int16 aTextType )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return OCommonAccessibleText::getTextAtIndex( nIndex ,aTextType);
}
css::accessibility::TextSegment SAL_CALL AccessibleListBoxEntry::getTextBeforeIndex( sal_Int32 nIndex, sal_Int16 aTextType )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();
    return OCommonAccessibleText::getTextBeforeIndex( nIndex ,aTextType);
}
css::accessibility::TextSegment SAL_CALL AccessibleListBoxEntry::getTextBehindIndex( sal_Int32 nIndex, sal_Int16 aTextType )
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard( m_aMutex );
    ensureAlive();

    return OCommonAccessibleText::getTextBehindIndex( nIndex ,aTextType);
}

// XAccessibleValue


Any AccessibleListBoxEntry::getCurrentValue(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );
    Any aValue;
    sal_Int32 level = static_cast<sal_Int32>(m_aEntryPath.size()) - 1;
    level = level < 0 ?  0: level;
    aValue <<= level;
    return aValue;
}


sal_Bool AccessibleListBoxEntry::setCurrentValue( const Any& aNumber )
{
    ::osl::MutexGuard aGuard( m_aMutex );


    bool bReturn = false;
    SvTreeListBox* pBox = m_pTreeListBox;
    if(getAccessibleRole() == AccessibleRole::CHECK_BOX)
    {
        SvTreeListEntry* pEntry = pBox->GetEntryFromPath( m_aEntryPath );
        if ( pEntry )
        {
            sal_Int32 nValue(0), nValueMin(0), nValueMax(0);
            aNumber >>= nValue;
            getMinimumValue() >>= nValueMin;
            getMaximumValue() >>= nValueMax;

            if ( nValue < nValueMin )
                nValue = nValueMin;
            else if ( nValue > nValueMax )
                nValue = nValueMax;

            pBox->SetCheckButtonState(pEntry,  static_cast<SvButtonState>(nValue) );
            bReturn = true;
        }
    }

    return bReturn;
}


Any AccessibleListBoxEntry::getMaximumValue(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );

    Any aValue;
    switch(getAccessibleRole())
    {
        case AccessibleRole::CHECK_BOX:
            aValue <<= sal_Int32(1);
            break;
        case AccessibleRole::LABEL:
        default:
            break;
    }

    return aValue;
}


Any AccessibleListBoxEntry::getMinimumValue(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );

    Any aValue;
    switch(getAccessibleRole())
    {
        case AccessibleRole::CHECK_BOX:
            aValue <<= sal_Int32(0);
            break;
        case AccessibleRole::LABEL:
        default:
            break;
    }

    return aValue;
}

Any AccessibleListBoxEntry::getMinimumIncrement(  )
{
    ::osl::MutexGuard aGuard( m_aMutex );

    Any aValue;
    switch(getAccessibleRole())
    {
        case AccessibleRole::CHECK_BOX:
            aValue <<= sal_Int32(1);
            break;
        case AccessibleRole::LABEL:
        default:
            break;
    }

    return aValue;
}

SvTreeListEntry* AccessibleListBoxEntry::GetRealChild(sal_Int32 nIndex)
{
    SvTreeListEntry* pEntry = nullptr;
    SvTreeListEntry* pParent = m_pTreeListBox->GetEntryFromPath( m_aEntryPath );
    if (pParent)
    {
        pEntry = m_pTreeListBox->GetEntry( pParent, nIndex );
        if ( !pEntry && getAccessibleChildCount() > 0 )
        {
            m_pTreeListBox->RequestingChildren(pParent);
            pEntry = m_pTreeListBox->GetEntry( pParent, nIndex );
        }
    }
    return pEntry;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
