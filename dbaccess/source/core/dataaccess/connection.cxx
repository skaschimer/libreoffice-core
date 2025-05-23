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

#include <sal/config.h>

#include <iterator>

#include <connection.hxx>
#include "datasource.hxx"
#include <strings.hrc>
#include <strings.hxx>
#include <core_resource.hxx>
#include <statement.hxx>
#include <preparedstatement.hxx>
#include <callablestatement.hxx>
#include <SingleSelectQueryComposer.hxx>
#include <querycomposer.hxx>

#include <com/sun/star/lang/NoSupportException.hpp>
#include <com/sun/star/sdb/CommandType.hpp>
#include <com/sun/star/sdb/tools/ConnectionTools.hpp>
#include <com/sun/star/reflection/ProxyFactory.hpp>
#include <com/sun/star/beans/NamedValue.hpp>
#include <connectivity/dbtools.hxx>
#include <connectivity/dbmetadata.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <osl/diagnose.h>
#include <comphelper/sequence.hxx>
#include <comphelper/types.hxx>
#include <cppuhelper/supportsservice.hxx>

using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::sdb;
using namespace ::com::sun::star::sdb::application;
using namespace ::com::sun::star::sdbc;
using namespace ::com::sun::star::sdbcx;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::reflection;
using namespace ::com::sun::star::container;
using namespace ::com::sun::star::graphic;
using namespace ::osl;
using namespace ::comphelper;
using namespace ::cppu;
using namespace ::dbtools;

using ::com::sun::star::sdb::tools::XTableName;
using ::com::sun::star::sdb::tools::XObjectNames;
using ::com::sun::star::sdb::tools::XDataSourceMetaData;

namespace dbaccess
{

// XServiceInfo
OUString OConnection::getImplementationName(  )
{
    return u"com.sun.star.comp.dbaccess.Connection"_ustr;
}

sal_Bool OConnection::supportsService( const OUString& _rServiceName )
{
    return cppu::supportsService(this, _rServiceName);
}

Sequence< OUString > OConnection::getSupportedServiceNames(  )
{
    Sequence< OUString > aSupported = OConnectionWrapper::getSupportedServiceNames();

    if ( comphelper::findValue( aSupported, SERVICE_SDB_CONNECTION ) == -1 )
    {
        sal_Int32 nLen = aSupported.getLength();
        aSupported.realloc( nLen + 1 );
        aSupported.getArray()[ nLen ] = SERVICE_SDB_CONNECTION;
    }

    return aSupported;
}

// XCloseable
void OConnection::close()
{
    // being closed is the same as being disposed
    dispose();
}

sal_Bool OConnection::isClosed()
{
    MutexGuard aGuard(m_aMutex);
    return !m_xMasterConnection.is();
}

// XConnection
Reference< XStatement >  OConnection::createStatement()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    rtl::Reference< OStatement > xStatement;
    Reference< XStatement > xMasterStatement = m_xMasterConnection->createStatement();
    if ( xMasterStatement.is() )
    {
        xStatement = new OStatement(this, xMasterStatement);
        m_aStatements.emplace_back(xStatement);
    }
    return xStatement;
}

Reference< XPreparedStatement >  OConnection::prepareStatement(const OUString& sql)
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    // TODO convert the SQL to SQL the driver understands
    rtl::Reference< OPreparedStatement > xStatement;
    Reference< XPreparedStatement > xMasterStatement = m_xMasterConnection->prepareStatement(sql);
    if ( xMasterStatement.is() )
    {
        xStatement = new OPreparedStatement(this, xMasterStatement);
        m_aStatements.emplace_back(xStatement);
    }
    return xStatement;
}

Reference< XPreparedStatement >  OConnection::prepareCall(const OUString& sql)
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    rtl::Reference< OCallableStatement > xStatement;
    Reference< XPreparedStatement > xMasterStatement = m_xMasterConnection->prepareCall(sql);
    if ( xMasterStatement.is() )
    {
        xStatement = new OCallableStatement(this, xMasterStatement);
        m_aStatements.emplace_back(xStatement);
    }
    return xStatement;
}

OUString OConnection::nativeSQL(const OUString& sql)
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return m_xMasterConnection->nativeSQL(sql);
}

void OConnection::setAutoCommit(sal_Bool autoCommit)
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    m_xMasterConnection->setAutoCommit(autoCommit);
}

sal_Bool OConnection::getAutoCommit()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return m_xMasterConnection->getAutoCommit();
}

void OConnection::commit()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    m_xMasterConnection->commit();
}

void OConnection::rollback()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    m_xMasterConnection->rollback();
}

Reference< XDatabaseMetaData >  OConnection::getMetaData()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return m_xMasterConnection->getMetaData();
}

void OConnection::setReadOnly(sal_Bool readOnly)
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    m_xMasterConnection->setReadOnly(readOnly);
}

sal_Bool OConnection::isReadOnly()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return m_xMasterConnection->isReadOnly();
}

void OConnection::setCatalog(const OUString& catalog)
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    m_xMasterConnection->setCatalog(catalog);
}

OUString OConnection::getCatalog()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return m_xMasterConnection->getCatalog();
}

void OConnection::setTransactionIsolation(sal_Int32 level)
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    m_xMasterConnection->setTransactionIsolation(level);
}

sal_Int32 OConnection::getTransactionIsolation()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return m_xMasterConnection->getTransactionIsolation();
}

Reference< XNameAccess >  OConnection::getTypeMap()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return m_xMasterConnection->getTypeMap();
}

void OConnection::setTypeMap(const Reference< XNameAccess > & typeMap)
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    m_xMasterConnection->setTypeMap(typeMap);
}

// OConnection

OConnection::OConnection(ODatabaseSource& _rDB
                         , Reference< XConnection > const & _rxMaster
                         , const Reference< XComponentContext >& _rxORB)
            :m_xParent(&_rDB)
            ,m_aTableFilter(_rDB.m_pImpl->m_aTableFilter)
            ,m_aTableTypeFilter(_rDB.m_pImpl->m_aTableTypeFilter)
            ,m_aContext( _rxORB )
            ,m_xMasterConnection(_rxMaster)
            ,m_aWarnings( Reference< XWarningsSupplier >( _rxMaster, UNO_QUERY ) )
            ,m_nInAppend(0)
            ,m_bSupportsViews(false)
            ,m_bSupportsUsers(false)
            ,m_bSupportsGroups(false)
{
    osl_atomic_increment(&m_refCount);

    try
    {
        Reference< XProxyFactory > xProxyFactory = ProxyFactory::create( m_aContext );
        Reference<XAggregation> xAgg = xProxyFactory->createProxy(_rxMaster);
        setDelegation(xAgg);
        OSL_ENSURE(m_xConnection.is(), "OConnection::OConnection : invalid master connection !");
    }
    catch(const Exception&)
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }

    m_xTableUIProvider.set(m_xMasterConnection, css::uno::UNO_QUERY);

    try
    {
        m_xQueries = OQueryContainer::create(Reference< XNameContainer >(_rDB.getQueryDefinitions(), UNO_QUERY), this, _rxORB, &m_aWarnings).get();

        bool bCase = true;
        Reference<XDatabaseMetaData> xMeta;
        try
        {
            xMeta = getMetaData();
            bCase = xMeta.is() && xMeta->supportsMixedCaseQuotedIdentifiers();
        }
        catch(const SQLException&)
        {
        }
        Reference< XNameContainer > xTableDefinitions(_rDB.getTables(),UNO_QUERY);
        m_pTables.reset( new OTableContainer( *this, m_aMutex, this, bCase, xTableDefinitions, this, m_nInAppend ) );

        // check if we support types
        if ( xMeta.is() )
        {
            Reference<XResultSet> xRes = xMeta->getTableTypes();
            if(xRes.is())
            {
                Reference<XRow> xRow(xRes,UNO_QUERY);
                while(xRes->next())
                {
                    OUString sValue = xRow->getString(1);
                    if( !xRow->wasNull() && sValue == "VIEW")
                    {
                        m_bSupportsViews = true;
                        break;
                    }
                }
            }
            // tdf#130564: getTableTypes retrieves only the table types of the current database and not all the possible table types
            // provided by the DBMS. JDBC would need a new function, something like "supportsViews()"
            // do the same for MySQL/MariaDB since we're on it
            OUString strDbProductName = xMeta->getDatabaseProductName();
            if (!m_bSupportsViews && xMeta->getURL().startsWith("sdbc:odbc:") &&
                 (strDbProductName == "PostgreSQL" || strDbProductName == "MySQL")
               )
            {
                m_bSupportsViews = true;
            }
            // some dbs don't support this type so we should ask if a XViewsSupplier is supported
            if(!m_bSupportsViews)
            {
                Reference< XViewsSupplier > xMaster(getMasterTables(),UNO_QUERY);

                if (xMaster.is() && xMaster->getViews().is())
                    m_bSupportsViews = true;
            }
            if(m_bSupportsViews)
            {
                m_pViews.reset( new OViewContainer(*this, m_aMutex, this, bCase, this, m_nInAppend) );
                m_pViews->addContainerListener(m_pTables.get());
                m_pTables->addContainerListener(m_pViews.get());
            }
            m_bSupportsUsers = Reference< XUsersSupplier> (getMasterTables(),UNO_QUERY).is();
            m_bSupportsGroups = Reference< XGroupsSupplier> (getMasterTables(),UNO_QUERY).is();

            impl_checkTableQueryNames_nothrow();
        }
    }
    catch(const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }
    osl_atomic_decrement( &m_refCount );
}

OConnection::~OConnection()
{
}

// XWarningsSupplier
Any SAL_CALL OConnection::getWarnings()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return m_aWarnings.getWarnings();
}

void SAL_CALL OConnection::clearWarnings(  )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    m_aWarnings.clearWarnings();
}

namespace
{
    struct CompareTypeByName
    {
        bool operator() ( const Type& _rLHS, const Type& _rRHS ) const
        {
            return _rLHS.getTypeName() < _rRHS.getTypeName();
        }
    };
    typedef std::set< Type, CompareTypeByName > TypeBag;

    void lcl_copyTypes( TypeBag& _out_rTypes, const Sequence< Type >& _rTypes )
    {
        std::copy( _rTypes.begin(), _rTypes.end(),
            std::insert_iterator< TypeBag >( _out_rTypes, _out_rTypes.begin() ) );
    }
}

// css::lang::XTypeProvider
Sequence< Type > OConnection::getTypes()
{
    TypeBag aNormalizedTypes;

    lcl_copyTypes( aNormalizedTypes, OConnection_Base::getTypes() );

    if ( !m_bSupportsViews )
        aNormalizedTypes.erase( cppu::UnoType<XViewsSupplier>::get() );
    if ( !m_bSupportsUsers )
        aNormalizedTypes.erase( cppu::UnoType<XUsersSupplier>::get() );
    if ( !m_bSupportsGroups )
        aNormalizedTypes.erase( cppu::UnoType<XGroupsSupplier>::get() );

    return comphelper::containerToSequence(aNormalizedTypes);
}

// css::uno::XInterface
Any OConnection::queryInterface( const Type & rType )
{
    if ( !m_bSupportsViews && rType.equals( cppu::UnoType<XViewsSupplier>::get() ) )
        return Any();
    else if ( !m_bSupportsUsers && rType.equals( cppu::UnoType<XUsersSupplier>::get() ) )
        return Any();
    else if ( !m_bSupportsGroups && rType.equals( cppu::UnoType<XGroupsSupplier>::get() ) )
        return Any();
    return OConnection_Base::queryInterface( rType );
}

// OConnection_Base
void OConnection::disposing()
{
    MutexGuard aGuard(m_aMutex);

    OConnection_Base::disposing();

    for (auto const& statement : m_aStatements)
    {
        rtl::Reference<OStatementBase> xComp(statement.get());
        ::comphelper::disposeComponent(xComp);
    }
    m_aStatements.clear();
    m_xMasterTables = nullptr;

    if(m_pTables)
        m_pTables->dispose();
    if(m_pViews)
        m_pViews->dispose();

    ::comphelper::disposeComponent(m_xQueries);

    for (auto const& composer : m_aComposers)
    {
        Reference<XComponent> xComp(composer.get(),UNO_QUERY);
        ::comphelper::disposeComponent(xComp);
    }

    m_aComposers.clear();

    try
    {
        if (m_xMasterConnection.is())
            m_xMasterConnection->close();
    }
    catch(const Exception&)
    {
    }
    m_xMasterConnection = nullptr;
}

// XChild
Reference< XInterface >  OConnection::getParent()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    return static_cast<OWeakObject*>(m_xParent.get().get());
}

void OConnection::setParent(const Reference< XInterface > & /*Parent*/)
{
    throw NoSupportException();
}

// XSQLQueryComposerFactory
Reference< XSQLQueryComposer >  OConnection::createQueryComposer()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    //  Reference< XNumberFormatsSupplier >  xSupplier = pParent->getNumberFormatsSupplier();
    Reference< XSQLQueryComposer >  xComposer( new OQueryComposer( this ) );
    m_aComposers.emplace_back(xComposer);
    return xComposer;
}

void OConnection::impl_fillTableFilter()
{
    Reference<XPropertySet> xProp(getParent(),UNO_QUERY);
    if ( xProp.is() )
    {
        xProp->getPropertyValue(PROPERTY_TABLEFILTER)       >>= m_aTableFilter;
        xProp->getPropertyValue(PROPERTY_TABLETYPEFILTER)   >>= m_aTableTypeFilter;
    }
}

void OConnection::refresh(const Reference< XNameAccess >& _rToBeRefreshed)
{
    if ( _rToBeRefreshed == Reference< XNameAccess >(m_pTables.get()) )
    {
        if (m_pTables && !m_pTables->isInitialized())
        {
            impl_fillTableFilter();
            // check if our "master connection" can supply tables
            getMasterTables();

            if (m_xMasterTables.is() && m_xMasterTables->getTables().is())
            {   // yes -> wrap them
                m_pTables->construct(m_xMasterTables->getTables(),m_aTableFilter, m_aTableTypeFilter);
            }
            else
            {   // no -> use an own container
                m_pTables->construct(m_aTableFilter, m_aTableTypeFilter);
            }
        }
    }
    else if ( _rToBeRefreshed == Reference< XNameAccess >(m_pViews.get()) )
    {
        if (m_pViews && !m_pViews->isInitialized())
        {
            impl_fillTableFilter();
            // check if our "master connection" can supply tables
            Reference< XViewsSupplier > xMaster(getMasterTables(),UNO_QUERY);

            if (xMaster.is() && xMaster->getViews().is())
                m_pViews->construct(xMaster->getViews(),m_aTableFilter, m_aTableTypeFilter);
            else
                m_pViews->construct(m_aTableFilter, m_aTableTypeFilter);
        }
    }
}

// XTablesSupplier
Reference< XNameAccess >  OConnection::getTables()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    refresh(m_pTables.get());

    return m_pTables.get();
}

Reference< XNameAccess > SAL_CALL OConnection::getViews(  )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    refresh(m_pViews.get());

    return m_pViews.get();
}

// XQueriesSupplier
Reference< XNameAccess >  OConnection::getQueries()
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    return m_xQueries;
}

// css::sdb::XCommandPreparation
Reference< XPreparedStatement >  SAL_CALL OConnection::prepareCommand( const OUString& command, sal_Int32 commandType )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    OUString aStatement;
    switch (commandType)
    {
        case CommandType::TABLE:
            {
                aStatement = "SELECT * FROM ";

                OUString sCatalog, sSchema, sTable;
                ::dbtools::qualifiedNameComponents( getMetaData(), command, sCatalog, sSchema, sTable, ::dbtools::EComposeRule::InDataManipulation );
                aStatement += ::dbtools::composeTableNameForSelect( this, sCatalog, sSchema, sTable );
            }
            break;
        case CommandType::QUERY:
            if ( m_xQueries->hasByName(command) )
            {
                Reference< XPropertySet > xQuery(m_xQueries->getByName(command),UNO_QUERY);
                xQuery->getPropertyValue(PROPERTY_COMMAND) >>= aStatement;
            }
            break;
        default:
            aStatement = command;
    }
    // TODO EscapeProcessing
    return prepareStatement(aStatement);
}

Reference< XInterface > SAL_CALL OConnection::createInstance( const OUString& _sServiceSpecifier )
{
    Reference< XServiceInfo > xRet;
    if ( SERVICE_NAME_SINGLESELECTQUERYCOMPOSER == _sServiceSpecifier || _sServiceSpecifier == "com.sun.star.sdb.SingleSelectQueryAnalyzer" )
    {
        xRet = new OSingleSelectQueryComposer( getTables(),this, m_aContext );
        m_aComposers.emplace_back(xRet);
    }
    else
    {
        if ( !_sServiceSpecifier.isEmpty() )
        {
            TSupportServices::const_iterator aFind = m_aSupportServices.find(_sServiceSpecifier);
            if ( aFind == m_aSupportServices.end() )
            {
                Reference<XConnection> xMy(this);
                Sequence<Any> aArgs{ Any(NamedValue(u"ActiveConnection"_ustr,Any(xMy))) };
                aFind = m_aSupportServices.emplace(
                               _sServiceSpecifier,
                               m_aContext->getServiceManager()->createInstanceWithArgumentsAndContext(_sServiceSpecifier, aArgs, m_aContext)
                           ).first;
            }
            return aFind->second;
        }
    }
    return Reference<XInterface>(xRet, UNO_QUERY);
}

Reference< XInterface > SAL_CALL OConnection::createInstanceWithArguments( const OUString& _sServiceSpecifier, const Sequence< Any >& /*Arguments*/ )
{
    return createInstance(_sServiceSpecifier);
}

Sequence< OUString > SAL_CALL OConnection::getAvailableServiceNames(  )
{
    Sequence< OUString > aRet { SERVICE_NAME_SINGLESELECTQUERYCOMPOSER };
    return aRet;
}

Reference< XTablesSupplier > const & OConnection::getMasterTables()
{
// check if out "master connection" can supply tables
    if(!m_xMasterTables.is())
    {
        try
        {
            Reference<XDatabaseMetaData> xMeta = getMetaData();
            if ( xMeta.is() )
                m_xMasterTables = ::dbtools::getDataDefinitionByURLAndConnection( xMeta->getURL(), m_xMasterConnection, m_aContext );
        }
        catch(const SQLException&)
        {
        }
    }
    return m_xMasterTables;
}

// XUsersSupplier
Reference< XNameAccess > SAL_CALL OConnection::getUsers(  )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();

    Reference<XUsersSupplier> xUsr(getMasterTables(),UNO_QUERY);
    return xUsr.is() ? xUsr->getUsers() : Reference< XNameAccess >();
}

// XGroupsSupplier
Reference< XNameAccess > SAL_CALL OConnection::getGroups(  )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    Reference<XGroupsSupplier> xGrp(getMasterTables(),UNO_QUERY);
    return xGrp.is() ? xGrp->getGroups() : Reference< XNameAccess >();
}

void OConnection::impl_loadConnectionTools_throw()
{
    m_xConnectionTools = css::sdb::tools::ConnectionTools::createWithConnection( m_aContext, this );
}

Reference< XTableName > SAL_CALL OConnection::createTableName(  )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    impl_loadConnectionTools_throw();

    return m_xConnectionTools->createTableName();
}

Reference< XObjectNames > SAL_CALL OConnection::getObjectNames(  )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    impl_loadConnectionTools_throw();

    return m_xConnectionTools->getObjectNames();
}

Reference< XDataSourceMetaData > SAL_CALL OConnection::getDataSourceMetaData(  )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    impl_loadConnectionTools_throw();

    return m_xConnectionTools->getDataSourceMetaData();
}

Reference< css::container::XNameAccess > SAL_CALL OConnection::getFieldsByCommandDescriptor( ::sal_Int32 commandType, const OUString& command, css::uno::Reference< css::lang::XComponent >& keepFieldsAlive )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    impl_loadConnectionTools_throw();

    return m_xConnectionTools->getFieldsByCommandDescriptor(commandType,command,keepFieldsAlive);
}

Reference< XSingleSelectQueryComposer > SAL_CALL OConnection::getComposer( ::sal_Int32 commandType, const OUString& command )
{
    MutexGuard aGuard(m_aMutex);
    checkDisposed();
    impl_loadConnectionTools_throw();

    return m_xConnectionTools->getComposer(commandType,command);
}

void OConnection::impl_checkTableQueryNames_nothrow()
{
    DatabaseMetaData aMeta( static_cast< XConnection* >( this ) );
    if ( !aMeta.supportsSubqueriesInFrom() )
        // nothing to do
        return;

    try
    {
        Reference< XNameAccess > xTables( getTables() );
        const Sequence< OUString > aTableNames( xTables->getElementNames() );
        std::set< OUString > aSortedTableNames( aTableNames.begin(), aTableNames.end() );

        Reference< XNameAccess > xQueries( getQueries() );
        const Sequence< OUString > aQueryNames( xQueries->getElementNames() );

        for ( auto const & queryName : aQueryNames )
        {
            if ( aSortedTableNames.contains(queryName) )
            {
                OUString sConflictWarning( DBA_RES( RID_STR_CONFLICTING_NAMES ) );
                m_aWarnings.appendWarning( sConflictWarning, "01SB0", *this );
            }
        }
    }
    catch( const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }
}

Reference< XGraphic > SAL_CALL OConnection::getTableIcon( const OUString& TableName, ::sal_Int32 ColorMode )
{
    Reference< XGraphic > xReturn;

    // ask our aggregate
    if ( m_xTableUIProvider.is() )
        xReturn = m_xTableUIProvider->getTableIcon( TableName, ColorMode );

    // ask ourself
    // well, we don't have own functionality here ...
    // In the future, we might decide to delegate the complete handling to this interface.
    // In this case, we would need to load the icon here.

    return xReturn;
}

Reference< XInterface > SAL_CALL OConnection::getTableEditor( const Reference< XDatabaseDocumentUI >& DocumentUI, const OUString& TableName )
{
    Reference< XInterface > xReturn;

    // ask our aggregate
    if ( m_xTableUIProvider.is() )
        xReturn = m_xTableUIProvider->getTableEditor( DocumentUI, TableName );

    // ask ourself
    // well, we don't have own functionality here ...
    // In the future, we might decide to delegate the complete handling to this interface.
    // In this case, we would need to instantiate a css.sdb.TableDesign here.

    return xReturn;
}

}   // namespace dbaccess

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
