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

#include <databasecontext.hxx>
#include "databasedocument.hxx"
#include "datasource.hxx"
#include <definitioncontainer.hxx>
#include <ModelImpl.hxx>
#include <sdbcoretools.hxx>

#include <com/sun/star/beans/PropertyBag.hpp>
#include <com/sun/star/container/XSet.hpp>
#include <com/sun/star/document/MacroExecMode.hpp>
#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/embed/XTransactionBroadcaster.hpp>
#include <com/sun/star/embed/StorageFactory.hpp>
#include <com/sun/star/frame/theGlobalEventBroadcaster.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/lang/WrappedTargetRuntimeException.hpp>
#include <com/sun/star/sdb/BooleanComparisonMode.hpp>
#include <com/sun/star/script/DocumentScriptLibraryContainer.hpp>
#include <com/sun/star/script/DocumentDialogLibraryContainer.hpp>
#include <com/sun/star/util/NumberFormatsSupplier.hpp>
#include <com/sun/star/security/DocumentDigitalSignatures.hpp>
#include <com/sun/star/security/XDocumentDigitalSignatures.hpp>
#include <com/sun/star/task/DocumentMacroConfirmationRequest.hpp>

#include <cppuhelper/exc_hlp.hxx>
#include <cppuhelper/implbase.hxx>
#include <comphelper/storagehelper.hxx>
#include <comphelper/types.hxx>
#include <comphelper/processfactory.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/signaturestate.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <osl/file.hxx>
#include <osl/diagnose.h>
#include <sal/log.hxx>
#include <tools/urlobj.hxx>
#include <unotools/configmgr.hxx>
#include <unotools/tempfile.hxx>
#include <i18nlangtag/languagetag.hxx>

#include <algorithm>
#include <utility>

using namespace css;
using namespace ::com::sun::star::document;
using namespace ::com::sun::star::sdbc;
using namespace ::com::sun::star::sdb;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::embed;
using namespace ::com::sun::star::container;
using namespace ::com::sun::star::util;
using namespace ::com::sun::star::io;
using namespace ::com::sun::star::frame;
using namespace ::com::sun::star::task;
using namespace ::com::sun::star::script;
using namespace ::cppu;
using namespace ::osl;
using namespace ::comphelper;

namespace dbaccess
{

// DocumentStorageAccess
class DocumentStorageAccess : public ::cppu::WeakImplHelper<   XDocumentSubStorageSupplier
                                                           ,   XTransactionListener >
{
    typedef std::map< OUString, Reference< XStorage > >    NamedStorages;

    ::osl::Mutex        m_aMutex;
    /// all sub storages which we ever gave to the outer world
    NamedStorages       m_aExposedStorages;
    ODatabaseModelImpl* m_pModelImplementation;
    bool                m_bPropagateCommitToRoot;
    bool                m_bDisposingSubStorages;

public:
    explicit DocumentStorageAccess( ODatabaseModelImpl& _rModelImplementation )
        :m_pModelImplementation( &_rModelImplementation )
        ,m_bPropagateCommitToRoot( true )
        ,m_bDisposingSubStorages( false )
    {
    }

protected:
    virtual ~DocumentStorageAccess() override
    {
    }

public:
    void dispose();

    // XDocumentSubStorageSupplier
    virtual Reference< XStorage > SAL_CALL getDocumentSubStorage( const OUString& aStorageName, ::sal_Int32 _nMode ) override;
    virtual Sequence< OUString > SAL_CALL getDocumentSubStoragesNames(  ) override;

    // XTransactionListener
    virtual void SAL_CALL preCommit( const css::lang::EventObject& aEvent ) override;
    virtual void SAL_CALL commited( const css::lang::EventObject& aEvent ) override;
    virtual void SAL_CALL preRevert( const css::lang::EventObject& aEvent ) override;
    virtual void SAL_CALL reverted( const css::lang::EventObject& aEvent ) override;

    // XEventListener
    virtual void SAL_CALL disposing( const css::lang::EventObject& Source ) override;

    /// disposes all storages managed by this instance
    void disposeStorages();

    /// disposes all known sub storages
    void commitStorages();

    /// commits the dedicated "database" storage
    bool commitEmbeddedStorage( bool _bPreventRootCommits );

private:
    /** opens the sub storage with the given name, in the given mode
    */
    Reference< XStorage > impl_openSubStorage_nothrow( const OUString& _rStorageName, sal_Int32 _nMode );

    void impl_suspendCommitPropagation()
    {
        OSL_ENSURE( m_bPropagateCommitToRoot, "DocumentStorageAccess::impl_suspendCommitPropagation: already suspended" );
        m_bPropagateCommitToRoot = false;
    }
    void impl_resumeCommitPropagation()
    {
        OSL_ENSURE( !m_bPropagateCommitToRoot, "DocumentStorageAccess::impl_resumeCommitPropagation: not suspended" );
        m_bPropagateCommitToRoot = true;
    }

};

void DocumentStorageAccess::dispose()
{
    ::osl::MutexGuard aGuard( m_aMutex );

    for (auto const& exposedStorage : m_aExposedStorages)
    {
        try
        {
            Reference< XTransactionBroadcaster > xBroadcaster(exposedStorage.second, UNO_QUERY);
            if ( xBroadcaster.is() )
                xBroadcaster->removeTransactionListener( this );
        }
        catch( const Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("dbaccess");
        }
    }

    m_aExposedStorages.clear();

    m_pModelImplementation = nullptr;
}

Reference< XStorage > DocumentStorageAccess::impl_openSubStorage_nothrow( const OUString& _rStorageName, sal_Int32 _nDesiredMode )
{
    OSL_ENSURE( !_rStorageName.isEmpty(),"ODatabaseModelImpl::impl_openSubStorage_nothrow: Invalid storage name!" );

    Reference< XStorage > xStorage;
    try
    {
        Reference< XStorage > xRootStorage( m_pModelImplementation->getOrCreateRootStorage() );
        if ( xRootStorage.is() )
        {
            sal_Int32 nRealMode = m_pModelImplementation->m_bDocumentReadOnly ? ElementModes::READ : _nDesiredMode;
            if ( nRealMode == ElementModes::READ )
            {
                if ( xRootStorage.is() && !xRootStorage->hasByName( _rStorageName ) )
                    return xStorage;
            }

            xStorage = xRootStorage->openStorageElement( _rStorageName, nRealMode );

            Reference< XTransactionBroadcaster > xBroad( xStorage, UNO_QUERY );
            if ( xBroad.is() )
                xBroad->addTransactionListener( this );
        }
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }

    return xStorage;
}

void DocumentStorageAccess::disposeStorages()
{
    m_bDisposingSubStorages = true;

    for (auto & exposedStorage : m_aExposedStorages)
    {
        try
        {
            ::comphelper::disposeComponent( exposedStorage.second );
        }
        catch( const Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("dbaccess");
        }
    }
    m_aExposedStorages.clear();

    m_bDisposingSubStorages = false;
}

void DocumentStorageAccess::commitStorages()
{
    try
    {
        for (auto const& exposedStorage : m_aExposedStorages)
        {
            tools::stor::commitStorageIfWriteable( exposedStorage.second );
        }
    }
    catch(const WrappedTargetException&)
    {
        // WrappedTargetException not allowed to leave
        throw IOException();
    }
}

bool DocumentStorageAccess::commitEmbeddedStorage( bool _bPreventRootCommits )
{
    if ( _bPreventRootCommits )
        impl_suspendCommitPropagation();

    bool bSuccess = false;
    try
    {
        NamedStorages::const_iterator pos = m_aExposedStorages.find( u"database"_ustr );
        if ( pos != m_aExposedStorages.end() )
            bSuccess = tools::stor::commitStorageIfWriteable( pos->second );
    }
    catch( Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }

    if ( _bPreventRootCommits )
        impl_resumeCommitPropagation();

    return bSuccess;

}

Reference< XStorage > SAL_CALL DocumentStorageAccess::getDocumentSubStorage( const OUString& aStorageName, ::sal_Int32 _nDesiredMode )
{
    ::osl::MutexGuard aGuard( m_aMutex );
    NamedStorages::const_iterator pos = m_aExposedStorages.find( aStorageName );
    if ( pos == m_aExposedStorages.end() )
    {
        Reference< XStorage > xResult = impl_openSubStorage_nothrow( aStorageName, _nDesiredMode );
        pos = m_aExposedStorages.emplace( aStorageName, xResult ).first;
    }

    return pos->second;
}

Sequence< OUString > SAL_CALL DocumentStorageAccess::getDocumentSubStoragesNames(  )
{
    Reference< XStorage > xRootStor( m_pModelImplementation->getRootStorage() );
    if ( !xRootStor.is() )
        return Sequence< OUString >();

    std::vector< OUString > aNames;

    const Sequence< OUString > aElementNames( xRootStor->getElementNames() );
    for ( OUString const & name : aElementNames )
    {
        if ( xRootStor->isStorageElement( name ) )
            aNames.push_back( name );
    }
    return aNames.empty()
        ?  Sequence< OUString >()
        :  Sequence< OUString >( aNames.data(), aNames.size() );
}

void SAL_CALL DocumentStorageAccess::preCommit( const css::lang::EventObject& /*aEvent*/ )
{
    // not interested in
}

void SAL_CALL DocumentStorageAccess::commited( const css::lang::EventObject& aEvent )
{
    ::osl::MutexGuard aGuard( m_aMutex );

    if ( m_pModelImplementation )
        m_pModelImplementation->setModified( true );

    if ( !(m_pModelImplementation && m_bPropagateCommitToRoot) )
        return;

    Reference< XStorage > xStorage( aEvent.Source, UNO_QUERY );

    // check if this is the dedicated "database" sub storage
    NamedStorages::const_iterator pos = m_aExposedStorages.find( u"database"_ustr );
    if  (   ( pos != m_aExposedStorages.end() )
        &&  ( pos->second == xStorage )
        )
    {
        // if so, also commit the root storage
        m_pModelImplementation->commitRootStorage();
    }
}

void SAL_CALL DocumentStorageAccess::preRevert( const css::lang::EventObject& /*aEvent*/ )
{
    // not interested in
}

void SAL_CALL DocumentStorageAccess::reverted( const css::lang::EventObject& /*aEvent*/ )
{
    // not interested in
}

void SAL_CALL DocumentStorageAccess::disposing( const css::lang::EventObject& Source )
{
    OSL_ENSURE( Reference< XStorage >( Source.Source, UNO_QUERY ).is(), "DocumentStorageAccess::disposing: No storage? What's this?" );

    if ( m_bDisposingSubStorages )
        return;

    auto find = std::find_if(m_aExposedStorages.begin(), m_aExposedStorages.end(),
        [&Source](const NamedStorages::value_type& rEntry) { return rEntry.second == Source.Source; });
    if (find != m_aExposedStorages.end())
        m_aExposedStorages.erase( find );
}

// ODatabaseModelImpl

ODatabaseModelImpl::ODatabaseModelImpl( const Reference< XComponentContext >& _rxContext, ODatabaseContext& _rDBContext )
            :m_aContainer()
            ,m_aMacroMode( *this )
            ,m_nImposedMacroExecMode( MacroExecMode::NEVER_EXECUTE )
            ,m_rDBContext( _rDBContext )
            ,m_refCount(0)
            ,m_bModificationLock( false )
            ,m_bDocumentInitialized( false )
            ,m_nScriptingSignatureState(SignatureState::UNKNOWN)
            ,m_aContext( _rxContext )
            ,m_nLoginTimeout(0)
            ,m_bReadOnly(false)
            ,m_bPasswordRequired(false)
            ,m_bSuppressVersionColumns(true)
            ,m_bModified(false)
            ,m_bDocumentReadOnly(false)
            ,m_bMacroCallsSeenWhileLoading(false)
            ,m_nControllerLockCount(0)
{
    // some kind of default
    m_sConnectURL = "jdbc:";
    m_aTableFilter = { u"%"_ustr };
    impl_construct_nothrow();
}

ODatabaseModelImpl::ODatabaseModelImpl(
                    OUString _sRegistrationName,
                    const Reference< XComponentContext >& _rxContext,
                    ODatabaseContext& _rDBContext
                    )
            :m_aContainer()
            ,m_aMacroMode( *this )
            ,m_nImposedMacroExecMode( MacroExecMode::NEVER_EXECUTE )
            ,m_rDBContext( _rDBContext )
            ,m_refCount(0)
            ,m_bModificationLock( false )
            ,m_bDocumentInitialized( false )
            ,m_nScriptingSignatureState(SignatureState::UNKNOWN)
            ,m_aContext( _rxContext )
            ,m_sName(std::move(_sRegistrationName))
            ,m_nLoginTimeout(0)
            ,m_bReadOnly(false)
            ,m_bPasswordRequired(false)
            ,m_bSuppressVersionColumns(true)
            ,m_bModified(false)
            ,m_bDocumentReadOnly(false)
            ,m_bMacroCallsSeenWhileLoading(false)
            ,m_nControllerLockCount(0)
{
    impl_construct_nothrow();
}

ODatabaseModelImpl::~ODatabaseModelImpl()
{
}

void ODatabaseModelImpl::impl_construct_nothrow()
{
    // create the property bag to hold the settings (also known as "Info" property)
    try
    {
        // the set of property value types in the bag is limited:
        Sequence< Type > aAllowedTypes({
             cppu::UnoType<sal_Bool>::get(),
             cppu::UnoType<double>::get(),
             cppu::UnoType<OUString>::get(),
             cppu::UnoType<sal_Int32>::get(),
             cppu::UnoType<sal_Int16>::get(),
             cppu::UnoType<Sequence< Any >>::get(),
        });

        m_xSettings = PropertyBag::createWithTypes( m_aContext, aAllowedTypes, false/*AllowEmptyPropertyName*/, true/*AutomaticAddition*/ );

        // insert the default settings
        Reference< XPropertyContainer > xContainer( m_xSettings, UNO_QUERY_THROW );
        Reference< XSet > xSettingsSet( m_xSettings, UNO_QUERY_THROW );
        for (auto& setting : getDefaultDataSourceSettings())
        {
            if (!setting.DefaultValue.hasValue())
            {
                Property aProperty(setting.Name,
                    -1,
                    setting.ValueType,
                    PropertyAttribute::BOUND | PropertyAttribute::MAYBEDEFAULT | PropertyAttribute::MAYBEVOID
                );
                xSettingsSet->insert( Any( aProperty ) );
            }
            else
            {
                xContainer->addProperty(setting.Name,
                    PropertyAttribute::BOUND | PropertyAttribute::MAYBEDEFAULT,
                    setting.DefaultValue
                );
            }
        }
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }
    m_rDBContext.appendAtTerminateListener(*this);
}

namespace
{
    OUString lcl_getContainerStorageName_throw( ODatabaseModelImpl::ObjectType _eType )
    {
        OUString sName;
        switch ( _eType )
        {
        case ODatabaseModelImpl::ObjectType::Form:   sName = u"forms"_ustr; break;
        case ODatabaseModelImpl::ObjectType::Report: sName = u"reports"_ustr; break;
        case ODatabaseModelImpl::ObjectType::Query:  sName = u"queries"_ustr; break;
        case ODatabaseModelImpl::ObjectType::Table:  sName = u"tables"_ustr; break;
        default:
            throw RuntimeException();
        }
        return sName;
    }

    bool lcl_hasObjectWithMacros_throw( const ODefinitionContainer_Impl& _rObjectDefinitions, const Reference< XStorage >& _rxContainerStorage )
    {
        bool bSomeDocHasMacros = false;

        for (auto const& objectDefinition : _rObjectDefinitions)
        {
            const TContentPtr& rDefinition( objectDefinition.second );
            const OUString& rPersistentName( rDefinition->m_aProps.sPersistentName );

            if ( rPersistentName.isEmpty() )
            {   // it's a logical sub folder used to organize the real objects
                const ODefinitionContainer_Impl& rSubFoldersObjectDefinitions( dynamic_cast< const ODefinitionContainer_Impl& >( *rDefinition ) );
                bSomeDocHasMacros = lcl_hasObjectWithMacros_throw( rSubFoldersObjectDefinitions, _rxContainerStorage );
                if (bSomeDocHasMacros)
                    break;
                continue;
            }

            bSomeDocHasMacros = ODatabaseModelImpl::objectHasMacros( _rxContainerStorage, rPersistentName );
            if (bSomeDocHasMacros)
                break;
        }
        return bSomeDocHasMacros;
    }

    bool lcl_hasObjectsWithMacros_nothrow( ODatabaseModelImpl& _rModel, const ODatabaseModelImpl::ObjectType _eType )
    {
        bool bSomeDocHasMacros = false;

        const OContentHelper_Impl& rContainerData( *_rModel.getObjectContainer( _eType ) );
        const ODefinitionContainer_Impl& rObjectDefinitions = dynamic_cast< const ODefinitionContainer_Impl& >( rContainerData );

        try
        {
            Reference< XStorage > xContainerStorage( _rModel.getStorage( _eType ) );
            // note the READWRITE here: If the storage already existed before, then the OpenMode will
            // be ignored, anyway.
            // If the storage did not yet exist, then it will be created. If the database document
            // is read-only, the OpenMode will be automatically downgraded to READ. Otherwise,
            // the storage will in fact be created as READWRITE. While this is not strictly necessary
            // for this particular use case here, it is required since the storage is *cached*, and
            // later use cases will need the READWRITE mode.

            if ( xContainerStorage.is() )
                bSomeDocHasMacros = lcl_hasObjectWithMacros_throw( rObjectDefinitions, xContainerStorage );
        }
        catch( const Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("dbaccess");
            // be on the safe side: If we can't reliably determine whether there are macros,
            // assume there actually are. Better this way, than the other way round.
            bSomeDocHasMacros = true;
        }

        return bSomeDocHasMacros;
    }
}

bool ODatabaseModelImpl::objectHasMacros( const Reference< XStorage >& _rxContainerStorage, const OUString& _rPersistentName )
{
    OSL_PRECOND( _rxContainerStorage.is(), "ODatabaseModelImpl::objectHasMacros: this will crash!" );

    bool bHasMacros = true;
    try
    {
        if ( !_rxContainerStorage->hasByName( _rPersistentName ) )
            return false;

        Reference< XStorage > xObjectStor( _rxContainerStorage->openStorageElement(
            _rPersistentName, ElementModes::READ ) );

        bHasMacros = ::sfx2::DocumentMacroMode::storageHasMacros( xObjectStor );
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }
    return bHasMacros;
}

void ODatabaseModelImpl::reset()
{
    m_bReadOnly = false;
    for (auto & i : m_aContainer)
        i.reset();

    if ( m_pStorageAccess.is() )
    {
        m_pStorageAccess->dispose();
        m_pStorageAccess.clear();
    }
}

void ODatabaseModelImpl::disposing( const css::lang::EventObject& Source )
{
    Reference<XConnection> xCon(Source.Source,UNO_QUERY);
    if ( xCon.is() )
    {
        bool bStore = false;
        for (OWeakConnectionArray::iterator i = m_aConnections.begin(); i != m_aConnections.end(); )
        {
            css::uno::Reference< css::sdbc::XConnection > xIterConn ( *i );
            if ( !xIterConn.is())
            {
                i = m_aConnections.erase(i);
            }
            else if ( xCon == xIterConn )
            {
                *i = css::uno::WeakReference< css::sdbc::XConnection >();
                bStore = true;
                break;
            } else
                ++i;
        }

        if ( bStore )
            commitRootStorage();
    }
    else
    {
        OSL_FAIL( "ODatabaseModelImpl::disposing: where does this come from?" );
    }
}

void ODatabaseModelImpl::clearConnections()
{
    OWeakConnectionArray aConnections;
    aConnections.swap( m_aConnections );

    Reference< XConnection > xConn;
    for (auto const& connection : aConnections)
    {
        xConn = connection;
        if ( xConn.is() )
        {
            try
            {
                xConn->close();
            }
            catch(const Exception&)
            {
                DBG_UNHANDLED_EXCEPTION("dbaccess");
            }
        }
    }

    m_xSharedConnectionManager = nullptr;
}

void ODatabaseModelImpl::dispose()
{
    // dispose the data source and the model
    try
    {
        rtl::Reference< ODatabaseSource > xDS( m_xDataSource );
        if (xDS)
        {
            xDS->dispose();
            m_xDataSource.clear();
        }

        rtl::Reference< ODatabaseDocument > xModel( m_xModel );
        if (xModel)
        {
            xModel->dispose();
            m_xModel.clear();
        }
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }
    m_xDataSource.clear();
    m_xModel.clear();

    for (auto const& elem : m_aContainer)
    {
        if ( elem )
            elem->m_pDataSource = nullptr;
    }
    for (auto & i : m_aContainer)
        i.reset();

    clearConnections();

    m_xNumberFormatsSupplier = nullptr;

    try
    {
        bool bCouldStore = commitEmbeddedStorage( true );
            // "true" means that committing the embedded storage should not trigger committing the root
            // storage. This is because we are going to commit the root storage ourself, anyway
        disposeStorages();
        if ( bCouldStore )
            commitRootStorage();

        impl_switchToStorage_throw( nullptr );
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }

    if ( m_pStorageAccess.is() )
    {
        m_pStorageAccess->dispose();
        m_pStorageAccess.clear();
    }
}

const Reference< XNumberFormatsSupplier > & ODatabaseModelImpl::getNumberFormatsSupplier()
{
    if (!m_xNumberFormatsSupplier.is())
    {
        // the arguments : the work locale of the current user
        Locale aLocale( LanguageTag::convertToLocale( utl::ConfigManager::getWorkLocale(), false));

        m_xNumberFormatsSupplier.set( NumberFormatsSupplier::createWithLocale( m_aContext, aLocale ) );
    }
    return m_xNumberFormatsSupplier;
}

void ODatabaseModelImpl::setDocFileLocation( const OUString& i_rLoadedFrom )
{
    ENSURE_OR_THROW( !i_rLoadedFrom.isEmpty(), "invalid URL" );
    m_sDocFileLocation = i_rLoadedFrom;
}

void ODatabaseModelImpl::setResource( const OUString& i_rDocumentURL, const Sequence< PropertyValue >& _rArgs )
{
    ENSURE_OR_THROW( !i_rDocumentURL.isEmpty(), "invalid URL" );

    ::comphelper::NamedValueCollection aMediaDescriptor( _rArgs );
#if OSL_DEBUG_LEVEL > 0
    if ( aMediaDescriptor.has( u"SalvagedFile"_ustr ) )
    {
        OUString sSalvagedFile( aMediaDescriptor.getOrDefault( u"SalvagedFile"_ustr, OUString() ) );
        // If SalvagedFile is an empty string, this indicates "the document is being recovered, but i_rDocumentURL already
        // is the real document URL, not the temporary document location"
        if ( sSalvagedFile.isEmpty() )
            sSalvagedFile = i_rDocumentURL;

        OSL_ENSURE( sSalvagedFile == i_rDocumentURL, "ODatabaseModelImpl::setResource: inconsistency!" );
            // nowadays, setResource should only be called with the logical URL of the document
    }
#endif

    m_aMediaDescriptor = stripLoadArguments( aMediaDescriptor );

    impl_switchToLogicalURL( i_rDocumentURL );
}

::comphelper::NamedValueCollection ODatabaseModelImpl::stripLoadArguments( const ::comphelper::NamedValueCollection& _rArguments )
{
    OSL_ENSURE( !_rArguments.has( u"Model"_ustr ), "ODatabaseModelImpl::stripLoadArguments: this is suspicious (1)!" );
    OSL_ENSURE( !_rArguments.has( u"ViewName"_ustr ), "ODatabaseModelImpl::stripLoadArguments: this is suspicious (2)!" );

    ::comphelper::NamedValueCollection aMutableArgs( _rArguments );
    aMutableArgs.remove( u"Model"_ustr );
    aMutableArgs.remove( u"ViewName"_ustr );
    return aMutableArgs;
}

void ODatabaseModelImpl::disposeStorages()
{
    getDocumentStorageAccess()->disposeStorages();
}

Reference< XSingleServiceFactory > ODatabaseModelImpl::createStorageFactory() const
{
    return StorageFactory::create( m_aContext );
}

void ODatabaseModelImpl::commitRootStorage()
{
    Reference< XStorage > xStorage( getOrCreateRootStorage() );
    bool bSuccess = commitStorageIfWriteable_ignoreErrors( xStorage );
    SAL_WARN_IF(!bSuccess && xStorage.is(), "dbaccess",
        "ODatabaseModelImpl::commitRootStorage: could not commit the storage!");
}

Reference< XStorage > const & ODatabaseModelImpl::getOrCreateRootStorage()
{
    if ( !m_xDocumentStorage.is() )
    {
        Reference< XSingleServiceFactory> xStorageFactory = StorageFactory::create( m_aContext );
        Any aSource = m_aMediaDescriptor.get( u"Stream"_ustr );
        if ( !aSource.hasValue() )
            aSource = m_aMediaDescriptor.get( u"InputStream"_ustr );
        if ( !aSource.hasValue() && !m_sDocFileLocation.isEmpty() )
            aSource <<= m_sDocFileLocation;
        // TODO: shouldn't we also check URL?

        OSL_ENSURE( aSource.hasValue(), "ODatabaseModelImpl::getOrCreateRootStorage: no source to create the storage from!" );

        if ( aSource.hasValue() )
        {
            Sequence< Any > aStorageCreationArgs{ aSource, Any(ElementModes::READWRITE) };

            Reference< XStorage > xDocumentStorage;
            OUString sURL;
            aSource >>= sURL;
            // Don't try to load a meta-URL as-is.
            if (!sURL.startsWithIgnoreAsciiCase("vnd.sun.star.pkg:"))
            {
                try
                {
                    xDocumentStorage.set( xStorageFactory->createInstanceWithArguments( aStorageCreationArgs ), UNO_QUERY_THROW );
                }
                catch( const Exception& )
                {
                    m_bDocumentReadOnly = true;
                    aStorageCreationArgs.getArray()[1] <<= ElementModes::READ;
                    try
                    {
                        xDocumentStorage.set( xStorageFactory->createInstanceWithArguments( aStorageCreationArgs ), UNO_QUERY_THROW );
                    }
                    catch( const Exception& )
                    {
                        DBG_UNHANDLED_EXCEPTION("dbaccess");
                    }
                }
            }

            impl_switchToStorage_throw( xDocumentStorage );
        }
    }
    return m_xDocumentStorage.getTyped();
}

DocumentStorageAccess* ODatabaseModelImpl::getDocumentStorageAccess()
{
    if ( !m_pStorageAccess.is() )
    {
        m_pStorageAccess = new DocumentStorageAccess( *this );
    }
    return m_pStorageAccess.get();
}

void ODatabaseModelImpl::modelIsDisposing( const bool _wasInitialized, ResetModelAccess )
{
    m_xModel.clear();

    // Basic libraries and Dialog libraries are a model facet, though held at this impl class.
    // They automatically dispose themself when the model they belong to is being disposed.
    // So, to not be tempted to do anything with them, again, we reset them.
    m_xBasicLibraries.clear();
    m_xDialogLibraries.clear();

    m_bDocumentInitialized = _wasInitialized;
}

Reference< XDocumentSubStorageSupplier > ODatabaseModelImpl::getDocumentSubStorageSupplier()
{
    return getDocumentStorageAccess();
}

bool ODatabaseModelImpl::commitEmbeddedStorage( bool _bPreventRootCommits )
{
    return getDocumentStorageAccess()->commitEmbeddedStorage( _bPreventRootCommits );
}

bool ODatabaseModelImpl::commitStorageIfWriteable_ignoreErrors( const Reference< XStorage >& _rxStorage )
{
    bool bTryToPreserveScriptSignature = false;
    utl::TempFileNamed aTempFile;
    aTempFile.EnableKillingFile();
    OUString sTmpFileUrl = aTempFile.GetURL();
    SignatureState aSignatureState = getScriptingSignatureState();
    OUString sLocation = getDocFileLocation();
    bool bIsEmbedded = sLocation.startsWith("vnd.sun.star.pkg:") && sLocation.endsWith("/EmbeddedDatabase");
    if (!bIsEmbedded && !sLocation.isEmpty()
        && (aSignatureState == SignatureState::OK || aSignatureState == SignatureState::NOTVALIDATED
            || aSignatureState == SignatureState::INVALID
            || aSignatureState == SignatureState::UNKNOWN))
    {
        bTryToPreserveScriptSignature = true;
        // We need to first save the file (which removes the macro signature), then add the macro signature again.
        // For that, we need a temporary copy of the original file.
        osl::File::RC rc = osl::File::copy(sLocation, sTmpFileUrl);
        if (rc != osl::FileBase::E_None)
            throw uno::RuntimeException(u"Could not create temp file"_ustr);
    }

    bool bSuccess = false;
    try
    {
        bSuccess = tools::stor::commitStorageIfWriteable( _rxStorage );
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }

    // Preserve script signature if the script has not changed
    if (bTryToPreserveScriptSignature)
    {
        OUString aODFVersion(comphelper::OStorageHelper::GetODFVersionFromStorage(_rxStorage));
        uno::Reference<security::XDocumentDigitalSignatures> xDDSigns;
        try
        {
            xDDSigns = security::DocumentDigitalSignatures::createWithVersion(
                comphelper::getProcessComponentContext(), aODFVersion);

            const OUString aScriptSignName
                = xDDSigns->getScriptingContentSignatureDefaultStreamName();

            if (!aScriptSignName.isEmpty())
            {
                Reference<XStorage> xReadOrig
                    = comphelper::OStorageHelper::GetStorageOfFormatFromURL(
                        ZIP_STORAGE_FORMAT_STRING, sTmpFileUrl, ElementModes::READ);
                if (!xReadOrig.is())
                    throw uno::RuntimeException("Could not read " + sTmpFileUrl);
                uno::Reference<embed::XStorage> xMetaInf
                    = xReadOrig->openStorageElement(u"META-INF"_ustr, embed::ElementModes::READ);

                uno::Reference<embed::XStorage> xTargetMetaInf
                    = _rxStorage->openStorageElement(u"META-INF"_ustr, embed::ElementModes::READWRITE);
                if (xMetaInf.is() && xTargetMetaInf.is() && xMetaInf->hasByName(aScriptSignName))
                {
                    xMetaInf->copyElementTo(aScriptSignName, xTargetMetaInf, aScriptSignName);

                    uno::Reference<embed::XTransactedObject> xTransact(xTargetMetaInf,
                                                                        uno::UNO_QUERY);
                    if (xTransact.is())
                        xTransact->commit();

                    xTargetMetaInf->dispose();

                    // now check the copied signature
                    uno::Sequence<security::DocumentSignatureInformation> aInfos
                        = xDDSigns->verifyScriptingContentSignatures(
                            _rxStorage, uno::Reference<io::XInputStream>());
                    SignatureState nState = DocumentSignatures::getSignatureState(aInfos);
                    if (nState == SignatureState::OK || nState == SignatureState::NOTVALIDATED
                        || nState == SignatureState::PARTIAL_OK)
                    {
                        // commit the ZipStorage from target medium
                        xTransact.set(_rxStorage, uno::UNO_QUERY);
                        if (xTransact.is())
                            xTransact->commit();
                    }
                    else
                    {
                        SAL_WARN("dbaccess", "An invalid signature was copied!");
                    }
                }
            }
        }
        catch (uno::Exception&)
        {
            TOOLS_WARN_EXCEPTION("dbaccess", "");
        }
    }

    return bSuccess;
}

void ODatabaseModelImpl::setModified( bool _bModified )
{
    if ( isModifyLocked() )
        return;

    try
    {
        rtl::Reference< ODatabaseDocument > xModi( m_xModel );
        if ( xModi.is() )
            xModi->setModified( _bModified );
        else
            m_bModified = _bModified;
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }
}

Reference<XDataSource> ODatabaseModelImpl::getOrCreateDataSource()
{
    rtl::Reference<ODatabaseSource> xDs = m_xDataSource;
    if ( !xDs.is() )
    {
        xDs = new ODatabaseSource(this);
        m_xDataSource = xDs.get();
    }
    return xDs;
}

rtl::Reference<ODatabaseDocument> ODatabaseModelImpl::getModel_noCreate() const
{
    return m_xModel.get();
}

rtl::Reference< ODatabaseDocument > ODatabaseModelImpl::createNewModel_deliverOwnership()
{
    rtl::Reference< ODatabaseDocument > xModel( m_xModel );
    OSL_PRECOND( !xModel.is(), "ODatabaseModelImpl::createNewModel_deliverOwnership: not to be called if there already is a model!" );
    if ( !xModel.is() )
    {
        bool bHadModelBefore = m_bDocumentInitialized;

        xModel = ODatabaseDocument::createDatabaseDocument( this, ODatabaseDocument::FactoryAccess() );
        m_xModel = xModel.get();

        try
        {
            Reference< XGlobalEventBroadcaster > xModelCollection = theGlobalEventBroadcaster::get( m_aContext );
            xModelCollection->insert( Any( Reference< XModel >(xModel) ) );
        }
        catch( const Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("dbaccess");
        }

        if ( bHadModelBefore )
        {
            // do an attachResources
            // In case the document is loaded regularly, this is not necessary, as our loader will do it.
            // However, in case that the document is implicitly created by asking the data source for the document,
            // then nobody would call the doc's attachResource. So, we do it here, to ensure it's in a proper
            // state, fires all events, and so on.
            // #i105505#
            xModel->attachResource( xModel->getURL(), m_aMediaDescriptor.getPropertyValues() );
        }
    }
    return xModel;
}

void ODatabaseModelImpl::acquire()
{
    osl_atomic_increment(&m_refCount);
}

void ODatabaseModelImpl::release()
{
    if ( osl_atomic_decrement(&m_refCount) == 0 )
    {
        acquire();  // prevent multiple releases
        m_rDBContext.removeFromTerminateListener(*this);
        dispose();
        m_rDBContext.storeTransientProperties(*this);
        if (!m_sDocumentURL.isEmpty())
            m_rDBContext.revokeDatabaseDocument(*this);
        delete this;
    }
}

void ODatabaseModelImpl::commitStorages()
{
    getDocumentStorageAccess()->commitStorages();
}

Reference< XStorage > ODatabaseModelImpl::getStorage( const ObjectType _eType )
{
    return getDocumentStorageAccess()->getDocumentSubStorage( getObjectContainerStorageName( _eType ),
                    css::embed::ElementModes::READWRITE );
}

// static
std::span<const DefaultPropertyValue> ODatabaseModelImpl::getDefaultDataSourceSettings()
{
    static const DefaultPropertyValue aKnownSettings[] =
    {
        // known JDBC settings
        { u"JavaDriverClass"_ustr,            Any( OUString() ) },
        { u"JavaDriverClassPath"_ustr,        Any( OUString() ) },
        { u"IgnoreCurrency"_ustr,             Any( false ) },
        // known settings for file-based drivers
        { u"Extension"_ustr,                  Any( OUString() ) },
        { u"CharSet"_ustr,                    Any( OUString() ) },
        { u"HeaderLine"_ustr,                 Any( true ) },
        { u"FieldDelimiter"_ustr,             Any( u","_ustr ) },
        { u"StringDelimiter"_ustr,            Any( u"\""_ustr ) },
        { u"DecimalDelimiter"_ustr,           Any( u"."_ustr ) },
        { u"ThousandDelimiter"_ustr,          Any( OUString() ) },
        { u"ShowDeleted"_ustr,                Any( false ) },
        // known ODBC settings
        { u"SystemDriverSettings"_ustr,       Any( OUString() ) },
        { u"UseCatalog"_ustr,                 Any( false ) },
        { u"TypeInfoSettings"_ustr,           Any( Sequence< Any >()) },
        // settings related to auto increment handling
        { u"AutoIncrementCreation"_ustr,      Any( OUString() ) },
        { u"AutoRetrievingStatement"_ustr,    Any( OUString() ) },
        { u"IsAutoRetrievingEnabled"_ustr,    Any( false ) },
        // known LDAP driver settings
        { u"HostName"_ustr,                   Any( OUString() ) },
        { u"PortNumber"_ustr,                 Any( sal_Int32(389) ) },
        { u"BaseDN"_ustr,                     Any( OUString() ) },
        { u"MaxRowCount"_ustr,                Any( sal_Int32(100) ) },
        // known MySQLNative driver settings
        { u"LocalSocket"_ustr,                Any( OUString() ) },
        { u"NamedPipe"_ustr,                  Any( OUString() ) },
        // misc known driver settings
        { u"ParameterNameSubstitution"_ustr,  Any( false ) },
        { u"AddIndexAppendix"_ustr,           Any( true ) },
        { u"IgnoreDriverPrivileges"_ustr,     Any( true ) },
        { u"ImplicitCatalogRestriction"_ustr, ::cppu::UnoType< OUString >::get() },
        { u"ImplicitSchemaRestriction"_ustr,  ::cppu::UnoType< OUString >::get() },
        { u"PrimaryKeySupport"_ustr,          ::cppu::UnoType< sal_Bool >::get() },
        { u"ShowColumnDescription"_ustr,      Any( false ) },
        // known SDB level settings
        { u"NoNameLengthLimit"_ustr,          Any( false ) },
        { u"AppendTableAliasName"_ustr,       Any( false ) },
        { u"GenerateASBeforeCorrelationName"_ustr,  Any( false ) },
        { u"ColumnAliasInOrderBy"_ustr,       Any( true ) },
        { u"EnableSQL92Check"_ustr,           Any( false ) },
        { u"BooleanComparisonMode"_ustr,      Any( BooleanComparisonMode::EQUAL_INTEGER ) },
        { u"TableTypeFilterMode"_ustr,        Any( sal_Int32(3) ) },
        { u"RespectDriverResultSetType"_ustr, Any( false ) },
        { u"UseSchemaInSelect"_ustr,          Any( true ) },
        { u"UseCatalogInSelect"_ustr,         Any( true ) },
        { u"EnableOuterJoinEscape"_ustr,      Any( true ) },
        { u"PreferDosLikeLineEnds"_ustr,      Any( false ) },
        { u"FormsCheckRequiredFields"_ustr,   Any( true ) },
        { u"EscapeDateTime"_ustr,             Any( true ) },

        // known services to handle database tasks
        { u"TableAlterationServiceName"_ustr, Any( OUString() ) },
        { u"TableRenameServiceName"_ustr,     Any( OUString() ) },
        { u"ViewAlterationServiceName"_ustr,  Any( OUString() ) },
        { u"ViewAccessServiceName"_ustr,      Any( OUString() ) },
        { u"CommandDefinitions"_ustr,         Any( OUString() ) },
        { u"Forms"_ustr,                      Any( OUString() ) },
        { u"Reports"_ustr,                    Any( OUString() ) },
        { u"KeyAlterationServiceName"_ustr,   Any( OUString() ) },
        { u"IndexAlterationServiceName"_ustr, Any( OUString() ) },
    };
    return aKnownSettings;
}

TContentPtr& ODatabaseModelImpl::getObjectContainer( ObjectType _eType )
{
    TContentPtr& rContentPtr = m_aContainer[ _eType ];

    if ( !rContentPtr )
    {
        rContentPtr = std::make_shared<ODefinitionContainer_Impl>();
        rContentPtr->m_pDataSource = this;
        rContentPtr->m_aProps.aTitle = lcl_getContainerStorageName_throw( _eType );
    }
    return rContentPtr;
}

bool ODatabaseModelImpl::adjustMacroMode_AutoReject()
{
    return m_aMacroMode.adjustMacroMode( nullptr );
}

bool ODatabaseModelImpl::checkMacrosOnLoading()
{
    Reference< XInteractionHandler > xInteraction;
    xInteraction = m_aMediaDescriptor.getOrDefault( u"InteractionHandler"_ustr, xInteraction );
    const bool bHasMacros = m_aMacroMode.hasMacros();
    // Since Base does not support document signatures, we always assume that the content signature is valid.
    return m_aMacroMode.checkMacrosOnLoading(xInteraction, true /*HasValidContentSignature*/, bHasMacros);
}

void ODatabaseModelImpl::resetMacroExecutionMode()
{
    m_aMacroMode = ::sfx2::DocumentMacroMode( *this );
}

Reference< XStorageBasedLibraryContainer > ODatabaseModelImpl::getLibraryContainer( bool _bScript )
{
    Reference< XStorageBasedLibraryContainer >& rxContainer( _bScript ? m_xBasicLibraries : m_xDialogLibraries );
    if ( rxContainer.is() )
        return rxContainer;

    rtl::Reference< ODatabaseDocument > xDocument( getModel_noCreate() );
    if (!xDocument)
        throw uno::RuntimeException();
        // this is only to be called if there already exists a document model - in fact, it is
        // to be called by the document model only

    try
    {
        Reference< XStorageBasedLibraryContainer > (*Factory)( const Reference< XComponentContext >&, const Reference< XStorageBasedDocument >&)
            = _bScript ? &DocumentScriptLibraryContainer::create : &DocumentDialogLibraryContainer::create;

        rxContainer.set(
            (*Factory)( m_aContext, xDocument ),
            UNO_SET_THROW
        );
    }
    catch( const RuntimeException& )
    {
        throw;
    }
    catch( const Exception& )
    {
        throw WrappedTargetRuntimeException(
            OUString(),
            cppu::getXWeak(xDocument.get()),
            ::cppu::getCaughtException()
        );
    }
    return rxContainer;
}

void ODatabaseModelImpl::storeLibraryContainersTo( const Reference< XStorage >& _rxToRootStorage )
{
    if ( m_xBasicLibraries.is() )
        m_xBasicLibraries->storeLibrariesToStorage( _rxToRootStorage );

    if ( m_xDialogLibraries.is() )
        m_xDialogLibraries->storeLibrariesToStorage( _rxToRootStorage );
}

Reference< XStorage > ODatabaseModelImpl::switchToStorage( const Reference< XStorage >& _rxNewRootStorage )
{
    if ( !_rxNewRootStorage.is() )
        throw IllegalArgumentException();

    return impl_switchToStorage_throw( _rxNewRootStorage );
}

namespace
{
    void lcl_modifyListening( ::sfx2::IModifiableDocument& _rDocument,
        const Reference< XStorage >& _rxStorage, ::rtl::Reference< ::sfx2::DocumentStorageModifyListener >& _inout_rListener,
        comphelper::SolarMutex& _rMutex, bool _bListen )
    {
        Reference< XModifiable > xModify( _rxStorage, UNO_QUERY );
        OSL_ENSURE( xModify.is() || !_rxStorage.is(), "lcl_modifyListening: storage can't notify us!" );

        if ( xModify.is() && !_bListen && _inout_rListener.is() )
        {
            xModify->removeModifyListener( _inout_rListener );
        }

        if ( _inout_rListener.is() )
        {
            _inout_rListener->dispose();
            _inout_rListener = nullptr;
        }

        if ( xModify.is() && _bListen )
        {
            _inout_rListener = new ::sfx2::DocumentStorageModifyListener( _rDocument, _rMutex );
            xModify->addModifyListener( _inout_rListener );
        }
    }
}

namespace
{
    void lcl_rebaseScriptStorage_throw( const Reference< XStorageBasedLibraryContainer >& _rxContainer,
        const Reference< XStorage >& _rxNewRootStorage )
    {
        if ( _rxContainer.is() )
        {
            if ( _rxNewRootStorage.is() )
                _rxContainer->setRootStorage( _rxNewRootStorage );
//            else
                   // TODO: what to do here? dispose the container?
        }
    }
}

Reference< XStorage > const & ODatabaseModelImpl::impl_switchToStorage_throw( const Reference< XStorage >& _rxNewRootStorage )
{
    // stop listening for modifications at the old storage
    lcl_modifyListening( *this, m_xDocumentStorage.getTyped(), m_pStorageModifyListener, Application::GetSolarMutex(), false );

    // set new storage
    m_xDocumentStorage.reset( _rxNewRootStorage, SharedStorage::TakeOwnership );

    // start listening for modifications
    lcl_modifyListening( *this, m_xDocumentStorage.getTyped(), m_pStorageModifyListener, Application::GetSolarMutex(), true );

    // forward new storage to Basic and Dialog library containers
    lcl_rebaseScriptStorage_throw( m_xBasicLibraries, m_xDocumentStorage.getTyped() );
    lcl_rebaseScriptStorage_throw( m_xDialogLibraries, m_xDocumentStorage.getTyped() );

    m_bReadOnly = !tools::stor::storageIsWritable_nothrow( m_xDocumentStorage.getTyped() );
    // TODO: our data source, if it exists, must broadcast the change of its ReadOnly property

    return m_xDocumentStorage.getTyped();
}

void ODatabaseModelImpl::impl_switchToLogicalURL( const OUString& i_rDocumentURL )
{
    if ( i_rDocumentURL == m_sDocumentURL )
        return;

    const OUString sOldURL( m_sDocumentURL );
    // update our name, if necessary
    if  (   ( m_sName == m_sDocumentURL )   // our name is our old URL
        ||  ( m_sName.isEmpty() )        // we do not have a name, yet (i.e. are not registered at the database context)
        )
    {
        INetURLObject aURL( i_rDocumentURL );
        if ( aURL.GetProtocol() != INetProtocol::NotValid )
        {
            m_sName = i_rDocumentURL;
            // TODO: our data source must broadcast the change of the Name property
        }
    }

    // remember URL
    m_sDocumentURL = i_rDocumentURL;

    // update our location, if necessary
    if  ( m_sDocFileLocation.isEmpty() )
        m_sDocFileLocation = m_sDocumentURL;

    // register at the database context, or change registration
    if (!sOldURL.isEmpty())
        m_rDBContext.databaseDocumentURLChange( sOldURL, m_sDocumentURL );
    else
        m_rDBContext.registerDatabaseDocument( *this );
}

OUString ODatabaseModelImpl::getObjectContainerStorageName( const ObjectType _eType )
{
    return lcl_getContainerStorageName_throw( _eType );
}

sal_Int16 ODatabaseModelImpl::getCurrentMacroExecMode() const
{
    sal_Int16 nCurrentMode = MacroExecMode::NEVER_EXECUTE;
    try
    {
        nCurrentMode = m_aMediaDescriptor.getOrDefault( u"MacroExecutionMode"_ustr, nCurrentMode );
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }
    return nCurrentMode;
}

void ODatabaseModelImpl::setCurrentMacroExecMode( sal_uInt16 nMacroMode )
{
    m_aMediaDescriptor.put( u"MacroExecutionMode"_ustr, nMacroMode );
}

OUString ODatabaseModelImpl::getDocumentLocation() const
{
    return getURL();
    // formerly, we returned getDocFileLocation here, which is the location of the file from which we
    // recovered the "real" document.
    // However, during CWS autorecovery evolving, we clarified (with MAV/MT) the role of XModel::getURL and
    // XStorable::getLocation. In this course, we agreed that for a macro security check, the *document URL*
    // (not the recovery file URL) is to be used: The recovery file lies in the backup folder, and by definition,
    // this folder is considered to be secure. So, the document URL needs to be used to decide about the security.
}

ODatabaseModelImpl::EmbeddedMacros ODatabaseModelImpl::determineEmbeddedMacros()
{
    if ( !m_aEmbeddedMacros )
    {
        if ( ::sfx2::DocumentMacroMode::storageHasMacros( getOrCreateRootStorage() ) )
        {
            m_aEmbeddedMacros = EmbeddedMacros::DocumentWide;
        }
        else if (   lcl_hasObjectsWithMacros_nothrow( *this, ObjectType::Form )
                ||  lcl_hasObjectsWithMacros_nothrow( *this, ObjectType::Report )
                )
        {
            m_aEmbeddedMacros = EmbeddedMacros::SubDocument;
        }
        else
        {
            m_aEmbeddedMacros = EmbeddedMacros::NONE;
        }
    }
    return *m_aEmbeddedMacros;
}

bool ODatabaseModelImpl::documentStorageHasMacros() const
{
    const_cast< ODatabaseModelImpl* >( this )->determineEmbeddedMacros();
    return ( *m_aEmbeddedMacros != EmbeddedMacros::NONE );
}

bool ODatabaseModelImpl::macroCallsSeenWhileLoading() const
{
    return m_bMacroCallsSeenWhileLoading;
}

Reference< XEmbeddedScripts > ODatabaseModelImpl::getEmbeddedDocumentScripts() const
{
    return getModel_noCreate();
}

SignatureState ODatabaseModelImpl::getScriptingSignatureState()
{
    return m_nScriptingSignatureState;
}

bool ODatabaseModelImpl::hasTrustedScriptingSignature(
    const css::uno::Reference<css::task::XInteractionHandler>& _rxInteraction)
{
    bool bResult = false;

    try
    {
        // Don't use m_xDocumentStorage, that somehow has an incomplete storage representation
        // which leads to signatures not being found
        Reference<XStorage> xStorage = comphelper::OStorageHelper::GetStorageOfFormatFromURL(
            ZIP_STORAGE_FORMAT_STRING, m_sDocFileLocation, ElementModes::READ);

        OUString aODFVersion(comphelper::OStorageHelper::GetODFVersionFromStorage(getOrCreateRootStorage()));
        uno::Reference<security::XDocumentDigitalSignatures> xSigner(
            security::DocumentDigitalSignatures::createWithVersion(
                comphelper::getProcessComponentContext(), aODFVersion));
        const uno::Sequence<security::DocumentSignatureInformation> aInfo
            = xSigner->verifyScriptingContentSignatures(xStorage,
                                                        uno::Reference<io::XInputStream>());

        if (!aInfo.hasElements())
            return false;

        m_nScriptingSignatureState = DocumentSignatures::getSignatureState(aInfo);
        if (m_nScriptingSignatureState == SignatureState::OK
            || m_nScriptingSignatureState == SignatureState::NOTVALIDATED)
        {
            bResult = std::any_of(aInfo.begin(), aInfo.end(),
                                  [&xSigner](const security::DocumentSignatureInformation& rInfo) {
                                      return xSigner->isAuthorTrusted(rInfo.Signer);
                                  });
        }

        if (!bResult && _rxInteraction)
        {
            task::DocumentMacroConfirmationRequest aRequest;
            aRequest.DocumentURL = m_sDocFileLocation;
            aRequest.DocumentStorage = std::move(xStorage);
            aRequest.DocumentSignatureInformation = aInfo;
            aRequest.DocumentVersion = aODFVersion;
            aRequest.Classification = task::InteractionClassification_QUERY;
            bResult = SfxMedium::CallApproveHandler(_rxInteraction, uno::Any(aRequest), true);
        }
    }
    catch (uno::Exception&)
    {
    }

    return bResult;
}

void ODatabaseModelImpl::storageIsModified()
{
    setModified( true );
}

ModelDependentComponent::ModelDependentComponent( ::rtl::Reference< ODatabaseModelImpl > _model )
    :m_pImpl(std::move( _model ))
{
}

ModelDependentComponent::~ModelDependentComponent()
{
}

}   // namespace dbaccess

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
