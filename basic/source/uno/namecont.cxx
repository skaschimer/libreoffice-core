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

#include <config_extensions.h>
#include <config_folders.h>

#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/container/XContainer.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/lang/NoSupportException.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/ucb/ContentCreationException.hpp>
#include <com/sun/star/xml/sax/SAXException.hpp>
#include <utility>
#include <vcl/svapp.hxx>
#include <o3tl/string_view.hxx>
#include <o3tl/temporary.hxx>
#include <osl/mutex.hxx>
#include <vcl/errinf.hxx>
#include <rtl/ustring.hxx>
#include <sal/log.hxx>
#include <sot/storage.hxx>
#include <comphelper/getexpandeduri.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/sequence.hxx>

#include <namecont.hxx>
#include <basic/basicmanagerrepository.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <tools/urlobj.hxx>
#include <unotools/pathoptions.hxx>
#include <svtools/sfxecode.hxx>
#include <svtools/ehdl.hxx>
#include <basic/basmgr.hxx>
#include <com/sun/star/xml/sax/Parser.hpp>
#include <com/sun/star/xml/sax/InputSource.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/xml/sax/Writer.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/uno/DeploymentException.hpp>
#include <com/sun/star/lang/DisposedException.hpp>
#include <com/sun/star/script/LibraryNotLoadedException.hpp>
#include <com/sun/star/script/vba/VBAScriptEventId.hpp>
#include <com/sun/star/ucb/SimpleFileAccess.hpp>
#include <com/sun/star/util/PathSubstitution.hpp>
#include <com/sun/star/deployment/ExtensionManager.hpp>
#include <comphelper/storagehelper.hxx>
#include <cppuhelper/exc_hlp.hxx>
#include <cppuhelper/queryinterface.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <cppuhelper/typeprovider.hxx>
#include <memory>
#include <string_view>

namespace basic
{

using namespace com::sun::star::document;
using namespace com::sun::star::container;
using namespace com::sun::star::uno;
using namespace com::sun::star::lang;
using namespace com::sun::star::io;
using namespace com::sun::star::ucb;
using namespace com::sun::star::script;
using namespace com::sun::star::beans;
using namespace com::sun::star::xml::sax;
using namespace com::sun::star::util;
using namespace com::sun::star::task;
using namespace com::sun::star::embed;
using namespace com::sun::star::frame;
using namespace com::sun::star::deployment;
using namespace com::sun::star;
using namespace cppu;

using com::sun::star::uno::Reference;

// #i34411: Flag for error handling during migration
static bool GbMigrationSuppressErrors = false;


// Implementation class NameContainer

// Methods XElementAccess
Type NameContainer::getElementType()
{
    return mType;
}

sal_Bool NameContainer::hasElements()
{
    return !maMap.empty();
}

// Methods XNameAccess
Any NameContainer::getByName( const OUString& aName )
{
    auto aIt = maMap.find(aName);
    if (aIt == maMap.end())
    {
        throw NoSuchElementException(aName);
    }
    return aIt->second;
}

Sequence< OUString > NameContainer::getElementNames()
{
    return comphelper::mapKeysToSequence(maMap);
}

sal_Bool NameContainer::hasByName( const OUString& aName )
{
    return maMap.contains(aName);
}


// Methods XNameReplace
void NameContainer::replaceByName(const OUString& aName, const Any& aElement,
                                  std::unique_lock<std::mutex>& guard)
{
    const Type& aAnyType = aElement.getValueType();
    if( mType != aAnyType )
    {
        throw IllegalArgumentException(u"types do not match"_ustr, rOwner, 2);
    }
    auto aIt = maMap.find(aName);
    if (aIt == maMap.end())
    {
        throw NoSuchElementException(aName);
    }
    Any aOldElement = aIt->second;
    aIt->second = aElement;

    // Fire event
    if (maContainerListeners.getLength(guard) > 0)
    {
        ContainerEvent aEvent;
        aEvent.Source = mpxEventSource;
        aEvent.Accessor <<= aName;
        aEvent.Element = aElement;
        aEvent.ReplacedElement = aOldElement;
        maContainerListeners.notifyEach(guard, &XContainerListener::elementReplaced, aEvent);
    }

    /*  After the container event has been fired (one listener will update the
        core Basic manager), fire change event. Listeners can rely on that the
        Basic source code of the core Basic manager is up-to-date. */
    if (maChangesListeners.getLength(guard) > 0)
    {
        ChangesEvent aEvent;
        aEvent.Source = mpxEventSource;
        aEvent.Base <<= aEvent.Source;
        aEvent.Changes = { { Any(aName), aElement, aOldElement } };
        maChangesListeners.notifyEach(guard, &XChangesListener::changesOccurred, aEvent);
    }
}

void NameContainer::insertNoCheck(const OUString& aName, const Any& aElement,
                                  std::unique_lock<std::mutex>& guard)
{
    const Type& aAnyType = aElement.getValueType();
    if( mType != aAnyType )
    {
        throw IllegalArgumentException(u"types do not match"_ustr, rOwner, 2);
    }

    maMap[aName] = aElement;

    // Fire event
    if (maContainerListeners.getLength(guard) > 0)
    {
        ContainerEvent aEvent;
        aEvent.Source = mpxEventSource;
        aEvent.Accessor <<= aName;
        aEvent.Element = aElement;
        maContainerListeners.notifyEach(guard, &XContainerListener::elementInserted, aEvent);
    }

    /*  After the container event has been fired (one listener will update the
        core Basic manager), fire change event. Listeners can rely on that the
        Basic source code of the core Basic manager is up-to-date. */
    if (maChangesListeners.getLength(guard) > 0)
    {
        ChangesEvent aEvent;
        aEvent.Source = mpxEventSource;
        aEvent.Base <<= aEvent.Source;
        aEvent.Changes = { { Any(aName), aElement, {} } };
        maChangesListeners.notifyEach(guard, &XChangesListener::changesOccurred, aEvent);
    }
}

// Methods XNameContainer
void NameContainer::insertByName(const OUString& aName, const Any& aElement,
                                 std::unique_lock<std::mutex>& guard)
{
    if (hasByName(aName))
        throw ElementExistException(aName);
    insertNoCheck(aName, aElement, guard);
}

void NameContainer::removeByName(const OUString& aName, std::unique_lock<std::mutex>& guard)
{
    auto aIt = maMap.find(aName);
    if (aIt == maMap.end())
    {
        throw NoSuchElementException("\"" + aName + "\" not found");
    }

    Any aOldElement = aIt->second;
    maMap.erase(aIt);

    // Fire event
    if (maContainerListeners.getLength(guard) > 0)
    {
        ContainerEvent aEvent;
        aEvent.Source = mpxEventSource;
        aEvent.Accessor <<= aName;
        aEvent.Element = aOldElement;
        maContainerListeners.notifyEach(guard, &XContainerListener::elementRemoved, aEvent);
    }

    /*  After the container event has been fired (one listener will update the
        core Basic manager), fire change event. Listeners can rely on that the
        Basic source code of the core Basic manager is up-to-date. */
    if (maChangesListeners.getLength(guard) > 0)
    {
        ChangesEvent aEvent;
        aEvent.Source = mpxEventSource;
        aEvent.Base <<= aEvent.Source;
        aEvent.Changes = { { Any(aName),
                             {}, // Element remains empty (meaning "replaced with nothing")
                             aOldElement } };
        maChangesListeners.notifyEach(guard, &XChangesListener::changesOccurred, aEvent);
    }
}


// Methods XContainer
void NameContainer::addContainerListener(const Reference<XContainerListener>& xListener,
                                         std::unique_lock<std::mutex>& guard)
{
    if( !xListener.is() )
    {
        throw RuntimeException(u"addContainerListener called with null xListener"_ustr,rOwner);
    }
    maContainerListeners.addInterface(guard, xListener);
}

void NameContainer::removeContainerListener(const Reference<XContainerListener>& xListener,
                                            std::unique_lock<std::mutex>& guard)
{
    if( !xListener.is() )
    {
        throw RuntimeException(u"removeContainerListener called with null xListener"_ustr,rOwner);
    }
    maContainerListeners.removeInterface(guard, xListener);
}

// Methods XChangesNotifier
void NameContainer::addChangesListener(const Reference<XChangesListener>& xListener,
                                       std::unique_lock<std::mutex>& guard)
{
    if( !xListener.is() )
    {
        throw RuntimeException(u"addChangesListener called with null xListener"_ustr,rOwner);
    }
    maChangesListeners.addInterface(guard, xListener);
}

void NameContainer::removeChangesListener(const Reference<XChangesListener>& xListener,
                                          std::unique_lock<std::mutex>& guard)
{
    if( !xListener.is() )
    {
        throw RuntimeException(u"removeChangesListener called with null xListener"_ustr,rOwner);
    }
    maChangesListeners.removeInterface(guard, xListener);
}


// ModifiableHelper

void ModifiableHelper::setModified(bool _bModified, std::unique_lock<std::mutex>& guard)
{
    if ( _bModified == mbModified )
    {
        return;
    }
    mbModified = _bModified;

    if (m_aModifyListeners.getLength(guard) == 0)
    {
        return;
    }
    EventObject aModifyEvent( m_rEventSource );
    m_aModifyListeners.notifyEach(guard, &XModifyListener::modified, aModifyEvent);
}


// Ctor
SfxLibraryContainer::SfxLibraryContainer()
    : mnRunningVBAScripts( 0 )
    , mbVBACompat( false )
    , meVBATextEncoding( RTL_TEXTENCODING_DONTKNOW )
    , maModifiable( *this )
    , maNameContainer( cppu::UnoType<XNameAccess>::get(), *this )
    , mpBasMgr( nullptr )
    , mbOwnBasMgr( false )
    , mbOldInfoFormat( false )
    , mbOasis2OOoFormat( false )
    , meInitMode(DEFAULT)
{
    mxContext = comphelper::getProcessComponentContext();

    mxSFI = ucb::SimpleFileAccess::create( mxContext );

    mxStringSubstitution = util::PathSubstitution::create( mxContext );
}

SfxLibraryContainer::~SfxLibraryContainer()
{
    if( mbOwnBasMgr )
    {
        delete mpBasMgr;
    }
}

void SfxLibraryContainer::enterMethod()
{
    Application::GetSolarMutex().acquire();
    if (m_bDisposed)
    {
        throw DisposedException( OUString(), *this );
    }
}

void SfxLibraryContainer::leaveMethod()
{
    Application::GetSolarMutex().release();
}

BasicManager* SfxLibraryContainer::getBasicManager()
{
    try
    {
        if ( mpBasMgr )
        {
            return mpBasMgr;
        }
        Reference< XModel > xDocument( mxOwnerDocument.get(), UNO_QUERY );
        SAL_WARN_IF(
            !xDocument.is(), "basic",
            ("SfxLibraryContainer::getBasicManager: cannot obtain a BasicManager"
             " without document!"));
        if ( xDocument.is() )
        {
            mpBasMgr = BasicManagerRepository::getDocumentBasicManager( xDocument );
        }
    }
    catch (const css::ucb::ContentCreationException&)
    {
        TOOLS_WARN_EXCEPTION( "basic", "SfxLibraryContainer::getBasicManager:" );
    }
    return mpBasMgr;
}

// Methods XStorageBasedLibraryContainer
Reference< XStorage > SAL_CALL SfxLibraryContainer::getRootStorage()
{
    LibraryContainerMethodGuard aGuard( *this );
    return mxStorage;
}

void SAL_CALL SfxLibraryContainer::setRootStorage( const Reference< XStorage >& _rxRootStorage )
{
    LibraryContainerMethodGuard aGuard( *this );
    if ( !_rxRootStorage.is() )
    {
        throw IllegalArgumentException(u"no root storage"_ustr, getXWeak(), 1);
    }
    mxStorage = _rxRootStorage;
    onNewRootStorage();
}

void SAL_CALL SfxLibraryContainer::storeLibrariesToStorage( const Reference< XStorage >& _rxRootStorage )
{
    LibraryContainerMethodGuard aGuard( *this );
    if ( !_rxRootStorage.is() )
    {
        throw IllegalArgumentException(u"no root storage"_ustr, getXWeak(), 1);
    }
    try
    {
        storeLibraries_Impl(_rxRootStorage, true, o3tl::temporary(std::unique_lock(m_aMutex)));
    }
    catch( const Exception& )
    {
        throw WrappedTargetException( OUString(),
                                      *this, ::cppu::getCaughtException() );
    }
}


// Methods XModifiable
sal_Bool SfxLibraryContainer::isModified()
{
    LibraryContainerMethodGuard aGuard( *this );
    if ( maModifiable.isModified() )
    {
        return true;
    }
    // the library container is not modified, go through the libraries and check whether they are modified
    for (auto& aName : maNameContainer.getElementNames())
    {
        try
        {
            SfxLibrary* pImplLib = getImplLib( aName );
            if( pImplLib->isModified() )
            {
                if ( aName == "Standard" )
                {
                    // this is a workaround that has to be implemented because
                    // empty standard library should stay marked as modified
                    // but should not be treated as modified while it is empty
                    if ( pImplLib->hasElements() )
                        return true;
                }
                else
                {
                    return true;
                }
            }
        }
        catch(const css::container::NoSuchElementException&)
        {
        }
    }

    return false;
}

void SAL_CALL SfxLibraryContainer::setModified( sal_Bool _bModified )
{
    LibraryContainerMethodGuard aGuard( *this );
    maModifiable.setModified(_bModified, o3tl::temporary(std::unique_lock(m_aMutex)));
}

void SAL_CALL SfxLibraryContainer::addModifyListener( const Reference< XModifyListener >& _rxListener )
{
    LibraryContainerMethodGuard aGuard( *this );
    maModifiable.addModifyListener(_rxListener, o3tl::temporary(std::unique_lock(m_aMutex)));
}

void SAL_CALL SfxLibraryContainer::removeModifyListener( const Reference< XModifyListener >& _rxListener )
{
    LibraryContainerMethodGuard aGuard( *this );
    maModifiable.removeModifyListener(_rxListener, o3tl::temporary(std::unique_lock(m_aMutex)));
}

// Methods XPersistentLibraryContainer
Any SAL_CALL SfxLibraryContainer::getRootLocation()
{
    LibraryContainerMethodGuard aGuard( *this );
    return Any( getRootStorage() );
}

OUString SAL_CALL SfxLibraryContainer::getContainerLocationName()
{
    LibraryContainerMethodGuard aGuard( *this );
    return maLibrariesDir;
}

void SAL_CALL SfxLibraryContainer::storeLibraries(  )
{
    LibraryContainerMethodGuard aGuard( *this );
    try
    {
        storeLibraries_Impl(mxStorage, mxStorage.is(), o3tl::temporary(std::unique_lock(m_aMutex)));
        // we need to store *all* libraries if and only if we are based on a storage:
        // in this case, storeLibraries_Impl will remove the source storage, after loading
        // all libraries, so we need to force them to be stored, again
    }
    catch( const Exception& )
    {
        throw WrappedTargetException( OUString(), *this, ::cppu::getCaughtException() );
    }
}

namespace
{
void checkAndCopyFileImpl( const INetURLObject& rSourceFolderInetObj,
                                  const INetURLObject& rTargetFolderInetObj,
                                  std::u16string_view rCheckFileName,
                                  std::u16string_view rCheckExtension,
                                  const Reference< XSimpleFileAccess3 >& xSFI )
{
    INetURLObject aTargetFolderInetObj( rTargetFolderInetObj );
    aTargetFolderInetObj.insertName( rCheckFileName, true, INetURLObject::LAST_SEGMENT,
                                     INetURLObject::EncodeMechanism::All );
    aTargetFolderInetObj.setExtension( rCheckExtension );
    OUString aTargetFile = aTargetFolderInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
    if( !xSFI->exists( aTargetFile ) )
    {
        INetURLObject aSourceFolderInetObj( rSourceFolderInetObj );
        aSourceFolderInetObj.insertName( rCheckFileName, true, INetURLObject::LAST_SEGMENT,
                                         INetURLObject::EncodeMechanism::All );
        aSourceFolderInetObj.setExtension( rCheckExtension );
        OUString aSourceFile = aSourceFolderInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
        xSFI->copy( aSourceFile, aTargetFile );
    }
}

constexpr OUString sUserBasicVariablePrefix = u"$(USER)/basic/"_ustr;
constexpr OUString sInstBasicVariablePrefix = u"$(INST)/" LIBO_SHARE_FOLDER "/basic/"_ustr;

void createVariableURL( OUString& rStr, std::u16string_view rLibName,
                               std::u16string_view rInfoFileName, bool bUser )
{
    if( bUser )
    {
        rStr = sUserBasicVariablePrefix;
    }
    else
    {
        rStr = sInstBasicVariablePrefix;
    }
    rStr += OUString::Concat(rLibName) + "/" + rInfoFileName + ".xlb/";
}
}

void SfxLibraryContainer::init(const OUString& rInitialDocumentURL,
                               const uno::Reference<embed::XStorage>& rxInitialStorage,
                               std::unique_lock<std::mutex>& guard)
{
    // this might be called from within the ctor, and the impl_init might (indirectly) create
    // a UNO reference to ourself.
    // Ensure that we're not destroyed while we're in here
    osl_atomic_increment( &m_refCount );
    init_Impl(rInitialDocumentURL, rxInitialStorage, guard);
    osl_atomic_decrement( &m_refCount );
}

void SfxLibraryContainer::init_Impl( const OUString& rInitialDocumentURL,
                                     const uno::Reference< embed::XStorage >& rxInitialStorage,
                                     std::unique_lock<std::mutex>& guard )
{
    uno::Reference< embed::XStorage > xStorage = rxInitialStorage;

    maInitialDocumentURL = rInitialDocumentURL;
    maInfoFileName = getInfoFileName();
    maOldInfoFileName = getOldInfoFileName();
    maLibElementFileExtension = getLibElementFileExtension();
    maLibrariesDir = getLibrariesDir();

    meInitMode = DEFAULT;
    INetURLObject aInitUrlInetObj( maInitialDocumentURL );
    OUString aInitFileName = aInitUrlInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
    if( !aInitFileName.isEmpty() )
    {
        // We need a BasicManager to avoid problems
        StarBASIC* pBas = new StarBASIC();
        mpBasMgr = new BasicManager( pBas );
        mbOwnBasMgr = true;

        OUString aExtension = aInitUrlInetObj.getExtension();
        if( aExtension == "xlc" )
        {
            meInitMode = CONTAINER_INIT_FILE;
            INetURLObject aLibPathInetObj( std::move(aInitUrlInetObj) );
            aLibPathInetObj.removeSegment();
            maLibraryPath = aLibPathInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
        }
        else if( aExtension == "xlb" )
        {
            meInitMode = LIBRARY_INIT_FILE;
            uno::Reference< embed::XStorage > xDummyStor;
            ::xmlscript::LibDescriptor aLibDesc;
            implLoadLibraryIndexFile(nullptr, aLibDesc, xDummyStor, aInitFileName, guard);
            return;
        }
        else
        {
            // Decide between old and new document
            bool bOldStorage = SotStorage::IsOLEStorage( aInitFileName );
            if ( bOldStorage )
            {
                meInitMode = OLD_BASIC_STORAGE;
                importFromOldStorage( aInitFileName );
                return;
            }
            else
            {
                meInitMode = OFFICE_DOCUMENT;
                try
                {
                    xStorage = ::comphelper::OStorageHelper::GetStorageFromURL( aInitFileName, embed::ElementModes::READ );
                }
                catch (const uno::Exception& )
                {
                    // TODO: error handling
                }
            }
        }
    }
    else
    {
        // Default paths
        maLibraryPath = SvtPathOptions().GetBasicPath();
    }

    uno::Reference< io::XInputStream > xInput;

    mxStorage = xStorage;
    bool bStorage = mxStorage.is();


    // #110009: Scope to force the StorageRefs to be destructed and
    // so the streams to be closed before the preload operation
    {

    uno::Reference< embed::XStorage > xLibrariesStor;
    OUString aFileName;

    int nPassCount = 1;
    if( !bStorage && meInitMode == DEFAULT )
    {
        nPassCount = 2;
    }
    for( int nPass = 0 ; nPass < nPassCount ; nPass++ )
    {
        if( bStorage )
        {
            SAL_WARN_IF(
                meInitMode != DEFAULT && meInitMode != OFFICE_DOCUMENT, "basic",
                "Wrong InitMode for document");
            try
            {
                uno::Reference< io::XStream > xStream;
                xLibrariesStor = xStorage->openStorageElement( maLibrariesDir, embed::ElementModes::READ );

                if ( xLibrariesStor.is() )
                {
                    aFileName = maInfoFileName + "-lc.xml";
                    try
                    {
                        xStream = xLibrariesStor->openStreamElement( aFileName, embed::ElementModes::READ );
                    }
                    catch(const uno::Exception& )
                    {}

                    if( !xStream.is() )
                    {
                        mbOldInfoFormat = true;

                        // Check old version
                        aFileName = maOldInfoFileName + ".xml";
                        try
                        {
                            xStream = xLibrariesStor->openStreamElement( aFileName, embed::ElementModes::READ );
                        }
                        catch(const uno::Exception& )
                        {}

                        if( !xStream.is() )
                        {
                            // Check for EA2 document version with wrong extensions
                            aFileName = maOldInfoFileName + ".xli";
                            xStream = xLibrariesStor->openStreamElement( aFileName, embed::ElementModes::READ );
                        }
                    }
                }

                if ( xStream.is() )
                {
                    xInput = xStream->getInputStream();
                }
            }
            catch(const uno::Exception& )
            {
                // TODO: error handling?
            }
        }
        else
        {
            std::unique_ptr<INetURLObject> pLibInfoInetObj;
            if( meInitMode == CONTAINER_INIT_FILE )
            {
                aFileName = aInitFileName;
            }
            else
            {
                if( nPass == 1 )
                {
                    pLibInfoInetObj.reset(new INetURLObject( o3tl::getToken(maLibraryPath, 0, ';') ));
                }
                else
                {
                    pLibInfoInetObj.reset(new INetURLObject( o3tl::getToken(maLibraryPath, 1, ';') ));
                }
                pLibInfoInetObj->insertName( maInfoFileName, false, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
                pLibInfoInetObj->setExtension( u"xlc" );
                aFileName = pLibInfoInetObj->GetMainURL( INetURLObject::DecodeMechanism::NONE );
            }

            try
            {
                xInput = mxSFI->openFileRead( aFileName );
            }
            catch(const Exception& )
            {
                // Silently tolerate empty or missing files
                xInput.clear();
            }

            // Old variant?
            if( !xInput.is() && nPass == 0 )
            {
                INetURLObject aLibInfoInetObj( o3tl::getToken(maLibraryPath, 1, ';') );
                aLibInfoInetObj.insertName( maOldInfoFileName, false, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
                aLibInfoInetObj.setExtension( u"xli" );
                aFileName = aLibInfoInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );

                try
                {
                    xInput = mxSFI->openFileRead( aFileName );
                    mbOldInfoFormat = true;
                }
                catch(const Exception& )
                {
                    xInput.clear();
                }
            }
        }

        if( xInput.is() )
        {
            InputSource source;
            source.aInputStream = xInput;
            source.sSystemId    = aFileName;

            // start parsing
            ::xmlscript::LibDescriptorArray aLibArray;

            Reference< XParser > xParser = xml::sax::Parser::create(mxContext);
            try
            {
                xParser->setDocumentHandler( ::xmlscript::importLibraryContainer( &aLibArray ) );
                xParser->parseStream( source );
            }
            catch ( const xml::sax::SAXException& )
            {
                TOOLS_WARN_EXCEPTION( "basic", "" );
                return;
            }
            catch ( const io::IOException& )
            {
                TOOLS_WARN_EXCEPTION( "basic", "" );
                return;
            }

            sal_Int32 nLibCount = aLibArray.mnLibCount;
            for( sal_Int32 i = 0 ; i < nLibCount ; i++ )
            {
                ::xmlscript::LibDescriptor& rLib = aLibArray.mpLibs[i];

                // Check storage URL
                OUString aStorageURL = rLib.aStorageURL;
                if( !bStorage && aStorageURL.isEmpty() && nPass == 0 )
                {
                    OUString aLibraryPath;
                    if( meInitMode == CONTAINER_INIT_FILE )
                    {
                        aLibraryPath = maLibraryPath;
                    }
                    else
                    {
                        aLibraryPath = maLibraryPath.getToken(1, ';');
                    }
                    INetURLObject aInetObj( aLibraryPath );

                    aInetObj.insertName( rLib.aName, true, INetURLObject::LAST_SEGMENT,
                                         INetURLObject::EncodeMechanism::All );
                    OUString aLibDirPath = aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
                    if( mxSFI->isFolder( aLibDirPath ) )
                    {
                        createVariableURL( rLib.aStorageURL, rLib.aName, maInfoFileName, true );
                        maModifiable.setModified(true, guard);
                    }
                    else if( rLib.bLink )
                    {
                        // Check "share" path
                        INetURLObject aShareInetObj( o3tl::getToken(maLibraryPath, 0, ';') );
                        aShareInetObj.insertName( rLib.aName, true, INetURLObject::LAST_SEGMENT,
                                                  INetURLObject::EncodeMechanism::All );
                        OUString aShareLibDirPath = aShareInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
                        if( mxSFI->isFolder( aShareLibDirPath ) )
                        {
                            createVariableURL( rLib.aStorageURL, rLib.aName, maInfoFileName, false );
                            maModifiable.setModified(true, guard);
                        }
                        else
                        {
                            // #i25537: Ignore lib if library folder does not really exist
                            continue;
                        }
                    }
                }

                OUString aLibName = rLib.aName;

                // If the same library name is used by the shared and the
                // user lib container index files the user file wins
                if( nPass == 1 && hasByName( aLibName ) )
                {
                    continue;
                }
                SfxLibrary* pImplLib;
                if( rLib.bLink )
                {
                    Reference< XNameAccess > xLib =
                        createLibraryLink_Impl(aLibName, rLib.aStorageURL, rLib.bReadOnly, guard);
                    pImplLib = static_cast< SfxLibrary* >( xLib.get() );
                }
                else
                {
                    Reference<XNameContainer> xLib = createLibrary_Impl(aLibName, guard);
                    pImplLib = static_cast< SfxLibrary* >( xLib.get() );
                    pImplLib->mbLoaded = false;
                    pImplLib->mbReadOnly = rLib.bReadOnly;
                    if( !bStorage )
                    {
                        checkStorageURL( rLib.aStorageURL, pImplLib->maLibInfoFileURL,
                                         pImplLib->maStorageURL, pImplLib->maUnexpandedStorageURL );
                    }
                }
                maModifiable.setModified(false, guard);

                // Read library info files
                if( !mbOldInfoFormat )
                {
                    uno::Reference< embed::XStorage > xLibraryStor;
                    if( !pImplLib->mbInitialised && bStorage )
                    {
                        try
                        {
                            xLibraryStor = xLibrariesStor->openStorageElement( rLib.aName,
                                                                                embed::ElementModes::READ );
                        }
                        catch(const uno::Exception& )
                        {
                            #if OSL_DEBUG_LEVEL > 0
                            TOOLS_WARN_EXCEPTION(
                                "basic",
                                "couldn't open sub storage for library \"" << rLib.aName << "\"");
                            #endif
                        }
                    }

                    // Link is already initialised in createLibraryLink_Impl()
                    if( !pImplLib->mbInitialised && (!bStorage || xLibraryStor.is()) )
                    {
                        bool bLoaded = implLoadLibraryIndexFile( pImplLib, rLib, xLibraryStor, OUString(), guard );
                        SAL_WARN_IF(
                            bLoaded && aLibName != rLib.aName, "basic",
                            ("Different library names in library container and"
                             " library info files!"));
                        if( GbMigrationSuppressErrors && !bLoaded )
                        {
                            removeLibrary( aLibName );
                        }
                    }
                }
                else if( !bStorage )
                {
                    // Write new index file immediately because otherwise
                    // the library elements will be lost when storing into
                    // the new info format
                    uno::Reference< embed::XStorage > xTmpStorage;
                    implStoreLibraryIndexFile( pImplLib, rLib, xTmpStorage );
                }

                implImportLibDescriptor(pImplLib, rLib, guard);

                if( nPass == 1 )
                {
                    pImplLib->mbSharedIndexFile = true;
                    pImplLib->mbReadOnly = true;
                }
            }

            // Keep flag for documents to force writing the new index files
            if( !bStorage )
            {
                mbOldInfoFormat = false;
            }
        }
    }

    // #110009: END Scope to force the StorageRefs to be destructed
    }

    if( !bStorage && meInitMode == DEFAULT )
    {
        try
        {
            implScanExtensions(guard);
        }
        catch(const uno::Exception& )
        {
            // TODO: error handling?
            SAL_WARN("basic", "Cannot access extensions!");
        }
    }

    // Preload?
    {
        for (auto& aName : maNameContainer.getElementNames())
        {
            SfxLibrary* pImplLib = getImplLib( aName );
            if( pImplLib->mbPreload )
            {
                loadLibrary_Impl(aName, guard);
            }
        }
    }

    if( meInitMode != DEFAULT )
        return;

    // tdf#121740 speed up loading documents with lots of embedded documents by avoid the UCB work of updating non-existent VBA libraries
    if (rInitialDocumentURL.isEmpty())
        return;

    INetURLObject aUserBasicInetObj( o3tl::getToken(maLibraryPath, 1, ';') );
    OUString aStandardStr(u"Standard"_ustr);

    INetURLObject aPrevUserBasicInetObj_1( aUserBasicInetObj );
    aPrevUserBasicInetObj_1.removeSegment();
    INetURLObject aPrevUserBasicInetObj_2 = aPrevUserBasicInetObj_1;
    aPrevUserBasicInetObj_1.Append( u"__basic_80" );
    aPrevUserBasicInetObj_2.Append( u"__basic_80_2" );

    // #i93163
    bool bCleanUp = false;
    try
    {
        INetURLObject aPrevUserBasicInetObj = aPrevUserBasicInetObj_1;
        OUString aPrevFolder = aPrevUserBasicInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
        if( mxSFI->isFolder( aPrevFolder ) )
        {
            // Check if Standard folder exists and is complete
            INetURLObject aUserBasicStandardInetObj( aUserBasicInetObj );
            aUserBasicStandardInetObj.insertName( aStandardStr, true, INetURLObject::LAST_SEGMENT,
                                                  INetURLObject::EncodeMechanism::All );
            INetURLObject aPrevUserBasicStandardInetObj( aPrevUserBasicInetObj );
            aPrevUserBasicStandardInetObj.insertName( aStandardStr, true, INetURLObject::LAST_SEGMENT,
                                                    INetURLObject::EncodeMechanism::All );
            OUString aPrevStandardFolder = aPrevUserBasicStandardInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
            if( mxSFI->isFolder( aPrevStandardFolder ) )
            {
                OUString aXlbExtension( u"xlb"_ustr );
                OUString aCheckFileName;

                // Check if script.xlb exists
                aCheckFileName = "script";
                checkAndCopyFileImpl( aUserBasicStandardInetObj,
                                      aPrevUserBasicStandardInetObj,
                                      aCheckFileName, aXlbExtension, mxSFI );

                // Check if dialog.xlb exists
                aCheckFileName = "dialog";
                checkAndCopyFileImpl( aUserBasicStandardInetObj,
                                      aPrevUserBasicStandardInetObj,
                                      aCheckFileName, aXlbExtension, mxSFI );

                // Check if module1.xba exists
                aCheckFileName = "Module1";
                checkAndCopyFileImpl( aUserBasicStandardInetObj,
                                      aPrevUserBasicStandardInetObj,
                                      aCheckFileName, u"xba", mxSFI );
            }
            else
            {
                OUString aStandardFolder = aUserBasicStandardInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
                mxSFI->copy( aStandardFolder, aPrevStandardFolder );
            }

            OUString aPrevCopyToFolder = aPrevUserBasicInetObj_2.GetMainURL( INetURLObject::DecodeMechanism::NONE );
            mxSFI->copy( aPrevFolder, aPrevCopyToFolder );
        }
        else
        {
            aPrevUserBasicInetObj = aPrevUserBasicInetObj_2;
            aPrevFolder = aPrevUserBasicInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
        }
        if( mxSFI->isFolder( aPrevFolder ) )
        {
            rtl::Reference<SfxLibraryContainer> pPrevCont = createInstanceImpl();

            // Rename previous basic folder to make storage URLs correct during initialisation
            OUString aFolderUserBasic = aUserBasicInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
            INetURLObject aUserBasicTmpInetObj( aUserBasicInetObj );
            aUserBasicTmpInetObj.removeSegment();
            aUserBasicTmpInetObj.Append( u"__basic_tmp" );
            OUString aFolderTmp = aUserBasicTmpInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );

            mxSFI->move( aFolderUserBasic, aFolderTmp );
            try
            {
                mxSFI->move( aPrevFolder, aFolderUserBasic );
            }
            catch(const Exception& )
            {
                // Move back user/basic folder
                try
                {
                       mxSFI->kill( aFolderUserBasic );
                }
                catch(const Exception& )
                {}
                mxSFI->move( aFolderTmp, aFolderUserBasic );
                throw;
            }

            INetURLObject aPrevUserBasicLibInfoInetObj( aUserBasicInetObj );
            aPrevUserBasicLibInfoInetObj.insertName( maInfoFileName, false, INetURLObject::LAST_SEGMENT,
                                                INetURLObject::EncodeMechanism::All );
            aPrevUserBasicLibInfoInetObj.setExtension( u"xlc");
            OUString aLibInfoFileName = aPrevUserBasicLibInfoInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
            Sequence<Any> aInitSeq( 1 );
            aInitSeq.getArray()[0] <<= aLibInfoFileName;
            GbMigrationSuppressErrors = true;
            pPrevCont->initialize( aInitSeq );
            GbMigrationSuppressErrors = false;

            // Rename folders back
            mxSFI->move( aFolderUserBasic, aPrevFolder );
            mxSFI->move( aFolderTmp, aFolderUserBasic );

            for (auto& aLibName : pPrevCont->getElementNames())
            {
                if( hasByName( aLibName ) )
                {
                    if( aLibName == aStandardStr )
                    {
                        SfxLibrary* pImplLib = getImplLib( aStandardStr );
                        OUString aStandardFolder = pImplLib->maStorageURL;
                        mxSFI->kill( aStandardFolder );
                    }
                    else
                    {
                        continue;
                    }
                }

                SfxLibrary* pImplLib = pPrevCont->getImplLib( aLibName );
                if( pImplLib->mbLink )
                {
                    OUString aStorageURL = pImplLib->maUnexpandedStorageURL;
                    bool bCreateLink = true;
                    if( aStorageURL.indexOf( "vnd.sun.star.expand:$UNO_USER_PACKAGES_CACHE"   ) != -1 ||
                        aStorageURL.indexOf( "vnd.sun.star.expand:$UNO_SHARED_PACKAGES_CACHE" ) != -1 ||
                        aStorageURL.indexOf( "vnd.sun.star.expand:$BUNDLED_EXTENSIONS" ) != -1 ||
                        aStorageURL.indexOf( "$(INST)"   ) != -1 )
                    {
                        bCreateLink = false;
                    }
                    if( bCreateLink )
                    {
                        createLibraryLink_Impl( aLibName, pImplLib->maStorageURL, pImplLib->mbReadOnly, guard );
                    }
                }
                else
                {
                    // Move folder if not already done
                    INetURLObject aUserBasicLibFolderInetObj( aUserBasicInetObj );
                    aUserBasicLibFolderInetObj.Append( aLibName );
                    OUString aLibFolder = aUserBasicLibFolderInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );

                    INetURLObject aPrevUserBasicLibFolderInetObj( aPrevUserBasicInetObj );
                    aPrevUserBasicLibFolderInetObj.Append( aLibName );
                    OUString aPrevLibFolder = aPrevUserBasicLibFolderInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );

                    if( mxSFI->isFolder( aPrevLibFolder ) && !mxSFI->isFolder( aLibFolder ) )
                    {
                        mxSFI->move( aPrevLibFolder, aLibFolder );
                    }

                    if( aLibName == aStandardStr )
                    {
                        maNameContainer.removeByName(aLibName, guard);
                    }

                    // Create library
                    Reference<XNameContainer> xLib = createLibrary_Impl(aLibName, guard);
                    SfxLibrary* pNewLib = static_cast< SfxLibrary* >( xLib.get() );
                    pNewLib->mbLoaded = false;
                    pNewLib->implSetModified(false, guard);
                    checkStorageURL( aLibFolder, pNewLib->maLibInfoFileURL,
                                     pNewLib->maStorageURL, pNewLib->maUnexpandedStorageURL );

                    uno::Reference< embed::XStorage > xDummyStor;
                    ::xmlscript::LibDescriptor aLibDesc;
                    implLoadLibraryIndexFile( pNewLib, aLibDesc, xDummyStor, pNewLib->maLibInfoFileURL, guard );
                    implImportLibDescriptor(pNewLib, aLibDesc, guard);
                }
            }
            mxSFI->kill( aPrevFolder );
        }
    }
    catch(const Exception&)
    {
        TOOLS_WARN_EXCEPTION("basic", "Upgrade of Basic installation failed somehow" );
        bCleanUp = true;
    }

    // #i93163
    if( !bCleanUp )
        return;

    INetURLObject aPrevUserBasicInetObj_Err(std::move(aUserBasicInetObj));
    aPrevUserBasicInetObj_Err.removeSegment();
    aPrevUserBasicInetObj_Err.Append( u"__basic_80_err" );
    OUString aPrevFolder_Err = aPrevUserBasicInetObj_Err.GetMainURL( INetURLObject::DecodeMechanism::NONE );

    bool bSaved = false;
    try
    {
        OUString aPrevFolder_1 = aPrevUserBasicInetObj_1.GetMainURL( INetURLObject::DecodeMechanism::NONE );
        if( mxSFI->isFolder( aPrevFolder_1 ) )
        {
            mxSFI->move( aPrevFolder_1, aPrevFolder_Err );
            bSaved = true;
        }
    }
    catch(const Exception& )
    {}
    try
    {
        OUString aPrevFolder_2 = aPrevUserBasicInetObj_2.GetMainURL( INetURLObject::DecodeMechanism::NONE );
        if( !bSaved && mxSFI->isFolder( aPrevFolder_2 ) )
        {
            mxSFI->move( aPrevFolder_2, aPrevFolder_Err );
        }
        else
        {
            mxSFI->kill( aPrevFolder_2 );
        }
    }
    catch(const Exception& )
    {}
}

void SfxLibraryContainer::implScanExtensions(std::unique_lock<std::mutex>& guard)
{
#if HAVE_FEATURE_EXTENSIONS
    ScriptExtensionIterator aScriptIt;

    bool bPureDialogLib = false;
    for (;;)
    {
        OUString aLibURL = aScriptIt.nextBasicOrDialogLibrary( bPureDialogLib );
        if (aLibURL.isEmpty())
            break;
        if( bPureDialogLib && maInfoFileName == "script" )
        {
            continue;
        }
        // Extract lib name
        sal_Int32 nLen = aLibURL.getLength();
        sal_Int32 indexLastSlash = aLibURL.lastIndexOf( '/' );
        sal_Int32 nReduceCopy = 0;
        if( indexLastSlash == nLen - 1 )
        {
            nReduceCopy = 1;
            indexLastSlash = aLibURL.lastIndexOf( '/', nLen - 1 );
        }

        OUString aLibName = aLibURL.copy( indexLastSlash + 1, nLen - indexLastSlash - nReduceCopy - 1 );

        // If a library of the same exists the existing library wins
        if( hasByName( aLibName ) )
        {
            continue;
        }
        // Add index file to URL
        OUString aIndexFileURL = aLibURL;
        if( nReduceCopy == 0 )
        {
            aIndexFileURL += "/";
        }
        aIndexFileURL += maInfoFileName + ".xlb";

        // Create link
        const bool bReadOnly = false;
        createLibraryLink_Impl(aLibName, aIndexFileURL, bReadOnly, guard);
    }
#else
    (void)guard;
#endif
}

// Handle maLibInfoFileURL and maStorageURL correctly
void SfxLibraryContainer::checkStorageURL( const OUString& aSourceURL,
                                           OUString& aLibInfoFileURL, OUString& aStorageURL,
                                           OUString& aUnexpandedStorageURL )
{
    OUString aExpandedSourceURL = expand_url( aSourceURL );
    if( aExpandedSourceURL != aSourceURL )
    {
        aUnexpandedStorageURL = aSourceURL;
    }
    else
    {
        // try to re-create the variable URL: helps moving the profile
        if (std::u16string_view aRest; aSourceURL.startsWith(expand_url(sUserBasicVariablePrefix), &aRest))
            aUnexpandedStorageURL = sUserBasicVariablePrefix + aRest;
        else if (aSourceURL.startsWith(expand_url(sInstBasicVariablePrefix), &aRest))
            aUnexpandedStorageURL = sInstBasicVariablePrefix + aRest;
        else
            aUnexpandedStorageURL.clear(); // This will use eventual value of aLibInfoFileURL
    }

    INetURLObject aInetObj( aExpandedSourceURL );
    OUString aExtension = aInetObj.getExtension();
    if( aExtension == "xlb" )
    {
        // URL to xlb file
        aLibInfoFileURL = aExpandedSourceURL;
        aInetObj.removeSegment();
        aStorageURL = aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
    }
    else
    {
        // URL to library folder
        aStorageURL = aExpandedSourceURL;
        aInetObj.insertName( maInfoFileName, false, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
        aInetObj.setExtension( u"xlb" );
        aLibInfoFileURL = aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
    }
}

SfxLibrary* SfxLibraryContainer::getImplLib( const OUString& rLibraryName )
{
    auto xNameAccess = maNameContainer.getByName(rLibraryName).query<XNameAccess>();
    return static_cast<SfxLibrary*>(xNameAccess.get());
}


// Storing with password encryption

// Empty implementation, avoids unnecessary implementation in dlgcont.cxx
bool SfxLibraryContainer::implStorePasswordLibrary( SfxLibrary*,
                                                    const OUString&,
                                                    const uno::Reference< embed::XStorage >&,
                                                    const uno::Reference< task::XInteractionHandler >&  )
{
    return false;
}

bool SfxLibraryContainer::implStorePasswordLibrary(
    SfxLibrary* /*pLib*/,
    const OUString& /*aName*/,
    const css::uno::Reference< css::embed::XStorage >& /*xStorage*/,
    const OUString& /*aTargetURL*/,
    const Reference< XSimpleFileAccess3 >& /*xToUseSFI*/,
    const uno::Reference< task::XInteractionHandler >&  )
{
    return false;
}

bool SfxLibraryContainer::implLoadPasswordLibrary(
    SfxLibrary* /*pLib*/,
    const OUString& /*Name*/,
    bool /*bVerifyPasswordOnly*/,
    std::unique_lock<std::mutex>& /*guard*/ )
{
    return true;
}

OUString SfxLibraryContainer::createAppLibraryFolder( SfxLibrary* pLib, std::u16string_view aName )
{
    OUString aLibDirPath = pLib->maStorageURL;
    if( aLibDirPath.isEmpty() )
    {
        INetURLObject aInetObj( o3tl::getToken(maLibraryPath, 1, ';') );
        aInetObj.insertName( aName, true, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
        checkStorageURL( aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ), pLib->maLibInfoFileURL,
                         pLib->maStorageURL, pLib->maUnexpandedStorageURL );
        aLibDirPath = pLib->maStorageURL;
    }

    if( !mxSFI->isFolder( aLibDirPath ) )
    {
        try
        {
            mxSFI->createFolder( aLibDirPath );
        }
        catch(const Exception& )
        {}
    }

    return aLibDirPath;
}

// Storing
void SfxLibraryContainer::implStoreLibrary( SfxLibrary* pLib,
                                            std::u16string_view aName,
                                            const uno::Reference< embed::XStorage >& xStorage )
{
    Reference< XSimpleFileAccess3 > xDummySFA;
    Reference< XInteractionHandler > xDummyHandler;
    implStoreLibrary( pLib, aName, xStorage, u"", xDummySFA, xDummyHandler );
}

// New variant for library export
void SfxLibraryContainer::implStoreLibrary( SfxLibrary* pLib,
                                            std::u16string_view aName,
                                            const uno::Reference< embed::XStorage >& xStorage,
                                            std::u16string_view aTargetURL,
                                            const Reference< XSimpleFileAccess3 >& rToUseSFI,
                                            const Reference< XInteractionHandler >& xHandler )
{
    bool bLink = pLib->mbLink;
    bool bStorage = xStorage.is() && !bLink;

    if( bStorage )
    {
        for (auto& aElementName : pLib->getElementNames())
        {
            OUString aStreamName = aElementName + ".xml";

            if( !isLibraryElementValid( pLib->getByName( aElementName ) ) )
            {
                SAL_WARN(
                    "basic",
                    "invalid library element \"" << aElementName << '"');
                continue;
            }
            try
            {
                uno::Reference< io::XStream > xElementStream = xStorage->openStreamElement(
                                                                    aStreamName,
                                                                    embed::ElementModes::READWRITE );
                //    throw uno::RuntimeException(); // TODO: method must either return the stream or throw an exception

                uno::Reference< beans::XPropertySet > xProps( xElementStream, uno::UNO_QUERY );
                SAL_WARN_IF(
                    !xProps.is(), "basic",
                    "The StorageStream must implement XPropertySet interface!");
                //if ( !xProps.is() ) //TODO

                if ( xProps.is() )
                {
                    xProps->setPropertyValue(u"MediaType"_ustr, uno::Any( u"text/xml"_ustr ) );

                    // #87671 Allow encryption
                    xProps->setPropertyValue(u"UseCommonStoragePasswordEncryption"_ustr, uno::Any( true ) );

                    Reference< XOutputStream > xOutput = xElementStream->getOutputStream();
                    Reference< XNameContainer > xLib( pLib );
                    writeLibraryElement( xLib, aElementName, xOutput );
                }
            }
            catch(const uno::Exception& )
            {
                SAL_WARN("basic", "Problem during storing of library!");
                // TODO: error handling?
            }
        }
        pLib->storeResourcesToStorage( xStorage );
    }
    else
    {
        // Export?
        bool bExport = !aTargetURL.empty();
        try
        {
            Reference< XSimpleFileAccess3 > xSFI = mxSFI;
            if( rToUseSFI.is() )
            {
                xSFI = rToUseSFI;
            }
            OUString aLibDirPath;
            if( bExport )
            {
                INetURLObject aInetObj( aTargetURL );
                aInetObj.insertName( aName, true, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
                aLibDirPath = aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );

                if( !xSFI->isFolder( aLibDirPath ) )
                {
                    xSFI->createFolder( aLibDirPath );
                }
                pLib->storeResourcesToURL( aLibDirPath, xHandler );
            }
            else
            {
                aLibDirPath = createAppLibraryFolder( pLib, aName );
                pLib->storeResources();
            }

            for (auto& aElementName : pLib->getElementNames())
            {
                INetURLObject aElementInetObj( aLibDirPath );
                aElementInetObj.insertName( aElementName, false,
                                            INetURLObject::LAST_SEGMENT,
                                            INetURLObject::EncodeMechanism::All );
                aElementInetObj.setExtension( maLibElementFileExtension );
                OUString aElementPath( aElementInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );

                if( !isLibraryElementValid( pLib->getByName( aElementName ) ) )
                {
                    SAL_WARN(
                        "basic",
                        "invalid library element \"" << aElementName << '"');
                    continue;
                }

                // TODO: Check modified
                try
                {
                    if( xSFI->exists( aElementPath ) )
                    {
                        xSFI->kill( aElementPath );
                    }
                    Reference< XOutputStream > xOutput = xSFI->openFileWrite( aElementPath );
                    Reference< XNameContainer > xLib( pLib );
                    writeLibraryElement( xLib, aElementName, xOutput );
                    xOutput->closeOutput();
                }
                catch(const Exception& )
                {
                    if( bExport )
                    {
                        throw;
                    }
                    SfxErrorContext aEc( ERRCTX_SFX_SAVEDOC, aElementPath );
                    ErrorHandler::HandleError( ERRCODE_IO_GENERAL );
                }
            }
        }
        catch(const Exception& )
        {
            if( bExport )
            {
                throw;
            }
        }
    }
}

void SfxLibraryContainer::implStoreLibraryIndexFile( SfxLibrary* pLib,
                                                     const ::xmlscript::LibDescriptor& rLib,
                                                     const uno::Reference< embed::XStorage >& xStorage )
{
    Reference< XSimpleFileAccess3 > xDummySFA;
    implStoreLibraryIndexFile( pLib, rLib, xStorage, u"", xDummySFA );
}

// New variant for library export
void SfxLibraryContainer::implStoreLibraryIndexFile( SfxLibrary* pLib,
                                                     const ::xmlscript::LibDescriptor& rLib,
                                                     const uno::Reference< embed::XStorage >& xStorage,
                                                     std::u16string_view aTargetURL,
                                                     const Reference< XSimpleFileAccess3 >& rToUseSFI )
{
    // Create sax writer
    Reference< XWriter > xWriter = xml::sax::Writer::create(mxContext);

    bool bLink = pLib->mbLink;
    bool bStorage = xStorage.is() && !bLink;

    // Write info file
    uno::Reference< io::XOutputStream > xOut;
    uno::Reference< io::XStream > xInfoStream;
    if( bStorage )
    {
        OUString aStreamName = maInfoFileName + "-lb.xml";

        try
        {
            xInfoStream = xStorage->openStreamElement( aStreamName, embed::ElementModes::READWRITE );
            SAL_WARN_IF(!xInfoStream.is(), "basic", "No stream!");
            uno::Reference< beans::XPropertySet > xProps( xInfoStream, uno::UNO_QUERY );
            //    throw uno::RuntimeException(); // TODO

            if ( xProps.is() )
            {
                xProps->setPropertyValue(u"MediaType"_ustr, uno::Any( u"text/xml"_ustr ) );

                // #87671 Allow encryption
                xProps->setPropertyValue(u"UseCommonStoragePasswordEncryption"_ustr, uno::Any( true ) );

                xOut = xInfoStream->getOutputStream();
            }
        }
        catch(const uno::Exception& )
        {
            SAL_WARN("basic", "Problem during storing of library index file!");
            // TODO: error handling?
        }
    }
    else
    {
        // Export?
        bool bExport = !aTargetURL.empty();
        Reference< XSimpleFileAccess3 > xSFI = mxSFI;
        if( rToUseSFI.is() )
        {
            xSFI = rToUseSFI;
        }
        OUString aLibInfoPath;
        if( bExport )
        {
            INetURLObject aInetObj( aTargetURL );
            aInetObj.insertName( rLib.aName, true, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
            OUString aLibDirPath = aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
            if( !xSFI->isFolder( aLibDirPath ) )
            {
                xSFI->createFolder( aLibDirPath );
            }
            aInetObj.insertName( maInfoFileName, false, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
            aInetObj.setExtension( u"xlb" );
            aLibInfoPath = aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
        }
        else
        {
            createAppLibraryFolder( pLib, rLib.aName );
            aLibInfoPath = pLib->maLibInfoFileURL;
        }

        try
        {
            if( xSFI->exists( aLibInfoPath ) )
            {
                xSFI->kill( aLibInfoPath );
            }
            xOut = xSFI->openFileWrite( aLibInfoPath );
        }
        catch(const Exception& )
        {
            if( bExport )
            {
                throw;
            }
            SfxErrorContext aEc( ERRCTX_SFX_SAVEDOC, aLibInfoPath );
            ErrorHandler::HandleError(  ERRCODE_IO_GENERAL );
        }
    }
    if( !xOut.is() )
    {
        SAL_WARN("basic", "couldn't open output stream");
        return;
    }
    xWriter->setOutputStream( xOut );
    xmlscript::exportLibrary( xWriter, rLib );
}


bool SfxLibraryContainer::implLoadLibraryIndexFile(  SfxLibrary* pLib,
                                                     ::xmlscript::LibDescriptor& rLib,
                                                     const uno::Reference< embed::XStorage >& xStorage,
                                                     const OUString& aIndexFileName,
                                                     std::unique_lock<std::mutex>& guard )
{
    Reference< XParser > xParser = xml::sax::Parser::create(mxContext);

    bool bStorage = false;
    if( pLib )
    {
        bool bLink = pLib->mbLink;
        bStorage = xStorage.is() && !bLink;
    }

    // Read info file
    uno::Reference< io::XInputStream > xInput;
    OUString aLibInfoPath;
    if( bStorage )
    {
        aLibInfoPath = maInfoFileName + "-lb.xml";

        try
        {
            uno::Reference< io::XStream > xInfoStream =
                xStorage->openStreamElement( aLibInfoPath, embed::ElementModes::READ );
            xInput = xInfoStream->getInputStream();
        }
        catch(const uno::Exception& )
        {}
    }
    else
    {
        // Create Input stream
        //String aLibInfoPath; // attention: THIS PROBLEM MUST BE REVIEWED BY SCRIPTING OWNER!!!

        if( pLib )
        {
            createAppLibraryFolder( pLib, rLib.aName );
            aLibInfoPath = pLib->maLibInfoFileURL;
        }
        else
        {
            aLibInfoPath = aIndexFileName;
        }
        try
        {
            xInput = mxSFI->openFileRead( aLibInfoPath );
        }
        catch(const Exception& )
        {
            xInput.clear();
            if( !GbMigrationSuppressErrors )
            {
                SfxErrorContext aEc( ERRCTX_SFX_LOADBASIC, aLibInfoPath );
                ErrorHandler::HandleError(  ERRCODE_IO_GENERAL );
            }
        }
    }
    if( !xInput.is() )
    {
        return false;
    }

    InputSource source;
    source.aInputStream = std::move(xInput);
    source.sSystemId    = aLibInfoPath;

    // start parsing
    try
    {
        xParser->setDocumentHandler( ::xmlscript::importLibrary( rLib ) );
        xParser->parseStream( source );
    }
    catch(const Exception& )
    {
        SAL_WARN("basic", "Parsing error");
        SfxErrorContext aEc( ERRCTX_SFX_LOADBASIC, aLibInfoPath );
        ErrorHandler::HandleError(  ERRCODE_IO_GENERAL );
        return false;
    }

    if( !pLib )
    {
        Reference<XNameContainer> xLib = createLibrary_Impl(rLib.aName, guard);
        pLib = static_cast< SfxLibrary* >( xLib.get() );
        pLib->mbLoaded = false;
        rLib.aStorageURL = aIndexFileName;
        checkStorageURL( rLib.aStorageURL, pLib->maLibInfoFileURL, pLib->maStorageURL,
                         pLib->maUnexpandedStorageURL );

        implImportLibDescriptor(pLib, rLib, guard);
    }

    return true;
}

void SfxLibraryContainer::implImportLibDescriptor( SfxLibrary* pLib,
                                                  ::xmlscript::LibDescriptor const& rLib,
                                                  std::unique_lock<std::mutex>& guard)
{
    if( pLib->mbInitialised )
        return;
    Any aDummyElement = createEmptyLibraryElement();
    for (auto& name : rLib.aElementNames)
    {
        pLib->maNameContainer.insertByName(name, aDummyElement, guard);
    }
    pLib->mbPasswordProtected = rLib.bPasswordProtected;
    pLib->mbReadOnly = rLib.bReadOnly;
    pLib->mbPreload  = rLib.bPreload;
    pLib->implSetModified(false, guard);
    pLib->mbInitialised = true;
}


// Methods of new XLibraryStorage interface?
void SfxLibraryContainer::storeLibraries_Impl( const uno::Reference< embed::XStorage >& i_rStorage,
                                               bool bComplete, std::unique_lock<std::mutex>& guard )
{
    const Sequence< OUString > aNames = maNameContainer.getElementNames();

    // Don't count libs from shared index file
    sal_Int32 nLibsToSave
        = std::count_if(aNames.begin(), aNames.end(),
                        [this](const OUString& name) {
                            SfxLibrary* pImplLib = getImplLib(name);
                            return !pImplLib->mbSharedIndexFile && !pImplLib->mbExtension;
                        });
    // Write to storage?
    bool bStorage = i_rStorage.is();
    uno::Reference< embed::XStorage > xSourceLibrariesStor;
    uno::Reference< embed::XStorage > xTargetLibrariesStor;
    OUString sTempTargetStorName;
    const bool bInplaceStorage = bStorage && ( i_rStorage == mxStorage );

    if( nLibsToSave == 0 )
    {
        if ( bInplaceStorage && mxStorage->hasByName(maLibrariesDir) )
        {
            mxStorage->removeElement(maLibrariesDir);
        }
        return;
    }

    if ( bStorage )
    {
        // Don't write if only empty standard lib exists
        if ( ( nLibsToSave == 1 ) && ( aNames[0] == "Standard" ) )
        {
            if (!getImplLib(aNames[0])->hasElements())
            {
                if ( bInplaceStorage && mxStorage->hasByName(maLibrariesDir) )
                {
                    mxStorage->removeElement(maLibrariesDir);
                }
                return;
            }
        }

        // create the empty target storage
        try
        {
            OUString sTargetLibrariesStoreName;
            if ( bInplaceStorage )
            {
                // create a temporary target storage
                const OUString aTempTargetNameBase = maLibrariesDir + "_temp_";
                sal_Int32 index = 0;
                do
                {
                    sTargetLibrariesStoreName = aTempTargetNameBase + OUString::number( index++ );
                    if ( !i_rStorage->hasByName( sTargetLibrariesStoreName ) )
                    {
                        break;
                    }
                }
                while ( true );
                sTempTargetStorName = sTargetLibrariesStoreName;
            }
            else
            {
                sTargetLibrariesStoreName = maLibrariesDir;
                if ( i_rStorage->hasByName( sTargetLibrariesStoreName ) )
                {
                    i_rStorage->removeElement( sTargetLibrariesStoreName );
                }
            }

            xTargetLibrariesStor.set( i_rStorage->openStorageElement( sTargetLibrariesStoreName, embed::ElementModes::READWRITE ), UNO_SET_THROW );
        }
        catch( const uno::Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("basic");
            return;
        }

        // open the source storage which might be used to copy yet-unmodified libraries
        try
        {
            if ( mxStorage->hasByName( maLibrariesDir ) || bInplaceStorage )
            {
                xSourceLibrariesStor = mxStorage->openStorageElement( maLibrariesDir,
                                                   bInplaceStorage ? embed::ElementModes::READWRITE : embed::ElementModes::READ );
            }
        }
        catch( const uno::Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("basic");
            return;
        }
    }

    int iArray = 0;
    ::xmlscript::LibDescriptor aLibDescriptorForExtensionLibs;
    ::xmlscript::LibDescriptorArray aLibArray( nLibsToSave );
    for (auto& name : aNames)
    {
        SfxLibrary* pImplLib = getImplLib(name);
        if( pImplLib->mbSharedIndexFile )
        {
            continue;
        }
        const bool bExtensionLib = pImplLib->mbExtension;
        ::xmlscript::LibDescriptor& rLib = bExtensionLib ?
              aLibDescriptorForExtensionLibs : aLibArray.mpLibs[iArray];
        if( !bExtensionLib )
        {
            iArray++;
        }
        rLib.aName = name;

        rLib.bLink = pImplLib->mbLink;
        if( !bStorage || pImplLib->mbLink )
        {
            rLib.aStorageURL = ( pImplLib->maUnexpandedStorageURL.getLength() ) ?
                pImplLib->maUnexpandedStorageURL : pImplLib->maLibInfoFileURL;
        }
        rLib.bReadOnly = pImplLib->mbReadOnly;
        rLib.bPreload = pImplLib->mbPreload;
        rLib.bPasswordProtected = pImplLib->mbPasswordProtected;
        rLib.aElementNames = pImplLib->getElementNames();

        if( pImplLib->implIsModified() || bComplete )
        {
// Testing pImplLib->implIsModified() is not reliable,
// IMHO the value of pImplLib->implIsModified() should
// reflect whether the library ( in-memory ) model
// is in sync with the library container's own storage. Currently
// whenever the library model is written to *any* storage
// pImplLib->implSetModified( sal_False ) is called
// The way the code works, especially the way that sfx uses
// temp storage when saving ( and later sets the root storage of the
// library container ) and similar madness in dbaccess means some surgery
// is required to make it possible to successfully use this optimisation
// It would be possible to do the implSetModified() call below only
// conditionally, but that would require an additional boolean to be
// passed in via the XStorageBasedDocument::storeLibrariesToStorage()...
// fdo#68983: If there's a password and the password is not known, only
// copying the storage works!
            // Can we simply copy the storage?
            bool isCopyStorage = !mbOldInfoFormat && !mbOasis2OOoFormat
                    && !pImplLib->isLoadedStorable()
                    && xSourceLibrariesStor.is() /* null for user profile */;
            if (isCopyStorage)
            {
                try
                {
                    (void)xSourceLibrariesStor->isStorageElement(rLib.aName);
                }
                catch (container::NoSuchElementException const&)
                {
                    isCopyStorage = false;
                }
            }
            if (isCopyStorage)
            {
                try
                {
                    xSourceLibrariesStor->copyElementTo( rLib.aName, xTargetLibrariesStor, rLib.aName );
                }
                catch( const uno::Exception& )
                {
                    DBG_UNHANDLED_EXCEPTION("basic");
                    // TODO: error handling?
                }
            }
            else
            {
                uno::Reference< embed::XStorage > xLibraryStor;
                if( bStorage )
                {
#if OSL_DEBUG_LEVEL > 0
                    try
                    {
#endif
                        xLibraryStor = xTargetLibrariesStor->openStorageElement(
                                                                        rLib.aName,
                                                                        embed::ElementModes::READWRITE );
#if OSL_DEBUG_LEVEL > 0
                    }
                    catch(const uno::Exception& )
                    {
                        TOOLS_WARN_EXCEPTION(
                            "basic",
                            "couldn't create sub storage for library \"" << rLib.aName << "\"");
                        throw;
                    }
#endif
                }

                // Maybe lib is not loaded?!
                if( bComplete )
                {
                    loadLibrary_Impl(rLib.aName, guard);
                }
                if( pImplLib->mbPasswordProtected )
                {
                    implStorePasswordLibrary( pImplLib, rLib.aName, xLibraryStor, uno::Reference< task::XInteractionHandler >() );
                    // TODO: Check return value
                }
                else
                {
                    implStoreLibrary( pImplLib, rLib.aName, xLibraryStor );
                }
                implStoreLibraryIndexFile( pImplLib, rLib, xLibraryStor );
                if( bStorage )
                {
                    try
                    {
                        uno::Reference< embed::XTransactedObject > xTransact( xLibraryStor, uno::UNO_QUERY_THROW );
                        xTransact->commit();
                    }
                    catch(const uno::Exception& )
                    {
                        DBG_UNHANDLED_EXCEPTION("basic");
                        // TODO: error handling
                        throw;
                    }
                }
            }
            maModifiable.setModified(true, guard);
            pImplLib->implSetModified(false, guard);
        }

        // For container info ReadOnly refers to mbReadOnlyLink
        rLib.bReadOnly = pImplLib->mbReadOnlyLink;
    }

    // if we did an in-place save into a storage (i.e. a save into the storage we were already based on),
    // then we need to clean up the temporary storage we used for this
    if ( bInplaceStorage && !sTempTargetStorName.isEmpty() )
    {
        SAL_WARN_IF(
            !xSourceLibrariesStor.is(), "basic",
            ("SfxLibrariesContainer::storeLibraries_impl: unexpected: we should"
             " have a source storage here!"));
        try
        {
            // for this, we first remove everything from the source storage, then copy the complete content
            // from the temporary target storage. From then on, what used to be the "source storage" becomes
            // the "target storage" for all subsequent operations.

            // (We cannot simply remove the storage, denoted by maLibrariesDir, from i_rStorage - there might be
            // open references to it.)

            if ( xSourceLibrariesStor.is() )
            {
                // remove
                const Sequence< OUString > aRemoveNames( xSourceLibrariesStor->getElementNames() );
                for ( auto const & removeName : aRemoveNames )
                {
                    xSourceLibrariesStor->removeElement( removeName );
                }

                // copy
                const Sequence< OUString > aCopyNames( xTargetLibrariesStor->getElementNames() );
                for ( auto const & copyName : aCopyNames )
                {
                    xTargetLibrariesStor->copyElementTo( copyName, xSourceLibrariesStor, copyName );
                }
            }

            // close and remove temp target
            xTargetLibrariesStor->dispose();
            i_rStorage->removeElement( sTempTargetStorName );
            xTargetLibrariesStor.clear();
            sTempTargetStorName.clear();

            // adjust target
            xTargetLibrariesStor = xSourceLibrariesStor;
            xSourceLibrariesStor.clear();
        }
        catch( const Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("basic");
            throw;
        }
    }

    if( !mbOldInfoFormat && !maModifiable.isModified() )
    {
        return;
    }
    maModifiable.setModified(false, guard);
    mbOldInfoFormat = false;

    // Write library container info
    // Create sax writer
    Reference< XWriter > xWriter = xml::sax::Writer::create(mxContext);

    // Write info file
    uno::Reference< io::XOutputStream > xOut;
    uno::Reference< io::XStream > xInfoStream;
    if( bStorage )
    {
        OUString aStreamName = maInfoFileName + "-lc.xml";

        try
        {
            xInfoStream = xTargetLibrariesStor->openStreamElement( aStreamName, embed::ElementModes::READWRITE );
            uno::Reference< beans::XPropertySet > xProps( xInfoStream, uno::UNO_QUERY_THROW );
            xProps->setPropertyValue(u"MediaType"_ustr, uno::Any( u"text/xml"_ustr ) );

            // #87671 Allow encryption
            xProps->setPropertyValue(u"UseCommonStoragePasswordEncryption"_ustr, uno::Any( true ) );

            xOut = xInfoStream->getOutputStream();
        }
        catch(const uno::Exception& )
        {
            ErrorHandler::HandleError(  ERRCODE_IO_GENERAL );
        }
    }
    else
    {
        // Create Output stream
        INetURLObject aLibInfoInetObj( o3tl::getToken(maLibraryPath, 1, ';') );
        aLibInfoInetObj.insertName( maInfoFileName, false, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
        aLibInfoInetObj.setExtension( u"xlc" );
        OUString aLibInfoPath( aLibInfoInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );

        try
        {
            if( mxSFI->exists( aLibInfoPath ) )
            {
                mxSFI->kill( aLibInfoPath );
            }
            xOut = mxSFI->openFileWrite( aLibInfoPath );
        }
        catch(const Exception& )
        {
            xOut.clear();
            SfxErrorContext aEc( ERRCTX_SFX_SAVEDOC, aLibInfoPath );
            ErrorHandler::HandleError(  ERRCODE_IO_GENERAL );
        }

    }
    if( !xOut.is() )
    {
        SAL_WARN("basic", "couldn't open output stream");
        return;
    }

    xWriter->setOutputStream( xOut );

    try
    {
        xmlscript::exportLibraryContainer( xWriter, &aLibArray );
        if ( bStorage )
        {
            uno::Reference< embed::XTransactedObject > xTransact( xTargetLibrariesStor, uno::UNO_QUERY_THROW );
            xTransact->commit();
        }
    }
    catch(const uno::Exception& )
    {
        SAL_WARN("basic", "Problem during storing of libraries!");
        ErrorHandler::HandleError(  ERRCODE_IO_GENERAL );
    }
}


// Methods XElementAccess
Type SAL_CALL SfxLibraryContainer::getElementType()
{
    LibraryContainerMethodGuard aGuard( *this );
    return maNameContainer.getElementType();
}

sal_Bool SfxLibraryContainer::hasElements()
{
    LibraryContainerMethodGuard aGuard( *this );
    return maNameContainer.hasElements();
}

// Methods XNameAccess
Any SfxLibraryContainer::getByName( const OUString& aName )
{
    LibraryContainerMethodGuard aGuard( *this );
    return maNameContainer.getByName(aName);
}

Sequence< OUString > SfxLibraryContainer::getElementNames()
{
    LibraryContainerMethodGuard aGuard( *this );
    return maNameContainer.getElementNames();
}

sal_Bool SfxLibraryContainer::hasByName( const OUString& aName )
{
    LibraryContainerMethodGuard aGuard( *this );
    return maNameContainer.hasByName( aName ) ;
}

// Methods XLibraryContainer
Reference< XNameContainer > SAL_CALL SfxLibraryContainer::createLibrary( const OUString& Name )
{
    LibraryContainerMethodGuard aGuard( *this );
    return createLibrary_Impl(Name, o3tl::temporary(std::unique_lock(m_aMutex)));
}

css::uno::Reference<css::container::XNameContainer>
SfxLibraryContainer::createLibrary_Impl(const OUString& Name, std::unique_lock<std::mutex>& guard)
{
    rtl::Reference<SfxLibrary> pNewLib = implCreateLibrary( Name );
    pNewLib->maLibElementFileExtension = maLibElementFileExtension;

    createVariableURL( pNewLib->maUnexpandedStorageURL, Name, maInfoFileName, true );
    // tdf#151741 - fill various storage URLs for the newly created library
    checkStorageURL(pNewLib->maUnexpandedStorageURL, pNewLib->maLibInfoFileURL,
                    pNewLib->maStorageURL, pNewLib->maUnexpandedStorageURL);

    Any aElement;
    aElement <<= Reference< XNameAccess >( pNewLib );
    maNameContainer.insertByName(Name, aElement, guard);
    maModifiable.setModified(true, guard);
    return pNewLib;
}

Reference< XNameAccess > SAL_CALL SfxLibraryContainer::createLibraryLink
    ( const OUString& Name, const OUString& StorageURL, sal_Bool ReadOnly )
{
    LibraryContainerMethodGuard aGuard( *this );
    return createLibraryLink_Impl(Name, StorageURL, ReadOnly, o3tl::temporary(std::unique_lock(m_aMutex)));
}

css::uno::Reference<css::container::XNameAccess>
SfxLibraryContainer::createLibraryLink_Impl(const OUString& Name, const OUString& StorageURL,
                                            sal_Bool ReadOnly, std::unique_lock<std::mutex>& guard)
{
    // TODO: Check other reasons to force ReadOnly status
    //if( !ReadOnly )
    //{
    //}

    OUString aLibInfoFileURL;
    OUString aLibDirURL;
    OUString aUnexpandedStorageURL;
    checkStorageURL( StorageURL, aLibInfoFileURL, aLibDirURL, aUnexpandedStorageURL );


    rtl::Reference<SfxLibrary> pNewLib = implCreateLibraryLink( Name, aLibInfoFileURL, aLibDirURL, ReadOnly );
    pNewLib->maLibElementFileExtension = maLibElementFileExtension;
    pNewLib->maUnexpandedStorageURL = aUnexpandedStorageURL;
    pNewLib->maOriginalStorageURL = StorageURL;

    uno::Reference< embed::XStorage > xDummyStor;
    ::xmlscript::LibDescriptor aLibDesc;
    implLoadLibraryIndexFile(pNewLib.get(), aLibDesc, xDummyStor, OUString(), guard);
    implImportLibDescriptor(pNewLib.get(), aLibDesc, guard);

    Any aElement;
    aElement <<= Reference< XNameAccess >( pNewLib );
    maNameContainer.insertByName(Name, aElement, guard);
    maModifiable.setModified(true, guard);

    if( StorageURL.indexOf( "vnd.sun.star.expand:$UNO_USER_PACKAGES_CACHE" ) != -1 )
    {
        pNewLib->mbExtension = true;
    }
    else if( StorageURL.indexOf( "vnd.sun.star.expand:$UNO_SHARED_PACKAGES_CACHE" ) != -1
           || StorageURL.indexOf( "vnd.sun.star.expand:$BUNDLED_EXTENSIONS" ) != -1 )
    {
        pNewLib->mbExtension = true;
        pNewLib->mbReadOnly = true;
    }

    return pNewLib;
}

void SAL_CALL SfxLibraryContainer::removeLibrary( const OUString& Name )
{
    LibraryContainerMethodGuard aGuard( *this );
    std::unique_lock guard(m_aMutex);
    // Get and hold library before removing
    rtl::Reference pImplLib(getImplLib(Name));
    if( pImplLib->mbReadOnly && !pImplLib->mbLink )
    {
        throw IllegalArgumentException(u"readonly && !link"_ustr, getXWeak(), 1);
    }
    // Remove from container
    maNameContainer.removeByName(Name, guard);
    maModifiable.setModified(true, guard);

    // Delete library files, but not for linked libraries
    if( pImplLib->mbLink )
        return;

    if( mxStorage.is() )
    {
        return;
    }
    if (pImplLib->hasElements())
    {
        for (auto& name : pImplLib->getElementNames())
            pImplLib->impl_removeWithoutChecks(name, guard);
    }

    // Delete index file
    createAppLibraryFolder(pImplLib.get(), Name);
    OUString aLibInfoPath = pImplLib->maLibInfoFileURL;
    try
    {
        if( mxSFI->exists( aLibInfoPath ) )
        {
            mxSFI->kill( aLibInfoPath );
        }
    }
    catch(const Exception& ) {}

    // Delete folder if empty
    INetURLObject aInetObj( o3tl::getToken(maLibraryPath, 1, ';') );
    aInetObj.insertName( Name, true, INetURLObject::LAST_SEGMENT,
                         INetURLObject::EncodeMechanism::All );
    OUString aLibDirPath = aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );

    try
    {
        if( mxSFI->isFolder( aLibDirPath ) )
        {
            Sequence< OUString > aContentSeq = mxSFI->getFolderContents( aLibDirPath, true );
            sal_Int32 nCount = aContentSeq.getLength();
            if( !nCount )
            {
                mxSFI->kill( aLibDirPath );
            }
        }
    }
    catch(const Exception& )
    {
    }
}

sal_Bool SAL_CALL SfxLibraryContainer::isLibraryLoaded( const OUString& Name )
{
    LibraryContainerMethodGuard aGuard( *this );
    SfxLibrary* pImplLib = getImplLib( Name );
    bool bRet = pImplLib->mbLoaded;
    return bRet;
}


void SAL_CALL SfxLibraryContainer::loadLibrary( const OUString& Name )
{
    LibraryContainerMethodGuard aGuard( *this );
    return loadLibrary_Impl(Name, o3tl::temporary(std::unique_lock(m_aMutex)));
}

void SfxLibraryContainer::loadLibrary_Impl(const OUString& Name,
                                           std::unique_lock<std::mutex>& guard)
{
    SfxLibrary* pImplLib = getImplLib(Name);

    bool bLoaded = pImplLib->mbLoaded;
    pImplLib->mbLoaded = true;
    if (bLoaded || !pImplLib->hasElements())
        return;

    if( pImplLib->mbPasswordProtected )
    {
        implLoadPasswordLibrary(pImplLib, Name, false, guard);
        return;
    }

    bool bLink = pImplLib->mbLink;
    bool bStorage = mxStorage.is() && !bLink;

    uno::Reference< embed::XStorage > xLibrariesStor;
    uno::Reference< embed::XStorage > xLibraryStor;
    if( bStorage )
    {
#if OSL_DEBUG_LEVEL > 0
        try
        {
#endif
            xLibrariesStor = mxStorage->openStorageElement( maLibrariesDir, embed::ElementModes::READ );
            SAL_WARN_IF(
                !xLibrariesStor.is(), "basic",
                ("The method must either throw exception or return a"
                 " storage!"));
            if ( !xLibrariesStor.is() )
            {
                throw uno::RuntimeException(u"null returned from openStorageElement"_ustr,getXWeak());
            }

            xLibraryStor = xLibrariesStor->openStorageElement( Name, embed::ElementModes::READ );
            SAL_WARN_IF(
                !xLibraryStor.is(), "basic",
                ("The method must either throw exception or return a"
                 " storage!"));
            if ( !xLibrariesStor.is() )
            {
                throw uno::RuntimeException(u"null returned from openStorageElement"_ustr,getXWeak());
            }
#if OSL_DEBUG_LEVEL > 0
        }
        catch(const uno::Exception& )
        {
            TOOLS_WARN_EXCEPTION(
                "basic",
                "couldn't open sub storage for library \"" << Name << "\"");
            throw;
        }
#endif
    }

    // tdf#167255 workaround: sort library elements to establish at least some predictable order.
    // FIXME: the order of modules must not affect their inner names visibility. Modules must load
    // their content first (and so the names of e.g. global constants / variables must be known),
    // and only then their elements' values must be resolved.
    auto elements = pImplLib->getElementNames();
    {
        auto range = asNonConstRange(elements);
        std::sort(range.begin(), range.end());
    }

    for (auto& aElementName : elements)
    {
        OUString aFile;
        uno::Reference< io::XInputStream > xInStream;

        if( bStorage )
        {
            uno::Reference< io::XStream > xElementStream;

            aFile = aElementName + ".xml";

            try
            {
                xElementStream = xLibraryStor->openStreamElement( aFile, embed::ElementModes::READ );
            }
            catch(const uno::Exception& )
            {}

            if( !xElementStream.is() )
            {
                // Check for EA2 document version with wrong extensions
                aFile = aElementName + "." + maLibElementFileExtension;
                try
                {
                    xElementStream = xLibraryStor->openStreamElement( aFile, embed::ElementModes::READ );
                }
                catch(const uno::Exception& )
                {}
            }

            if ( xElementStream.is() )
            {
                xInStream = xElementStream->getInputStream();
            }
            if ( !xInStream.is() )
            {
                SAL_WARN(
                    "basic",
                    "couldn't open library element stream - attempted to"
                        " open library \"" << Name << '"');
                throw RuntimeException(u"couldn't open library element stream"_ustr, *this);
            }
        }
        else
        {
            OUString aLibDirPath = pImplLib->maStorageURL;
            INetURLObject aElementInetObj( aLibDirPath );
            aElementInetObj.insertName( aElementName, false,
                                        INetURLObject::LAST_SEGMENT,
                                        INetURLObject::EncodeMechanism::All );
            aElementInetObj.setExtension( maLibElementFileExtension );
            aFile = aElementInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
        }

        Reference< XNameContainer > xLib( pImplLib );
        Any aAny = importLibraryElement( xLib, aElementName,
                                         aFile, xInStream );
        if( pImplLib->hasByName( aElementName ) )
        {
            if( aAny.hasValue() )
            {
                pImplLib->maNameContainer.replaceByName(aElementName, aAny, guard);
            }
        }
        else
        {
            pImplLib->maNameContainer.insertNoCheck(aElementName, aAny, guard);
        }
    }
    pImplLib->implSetModified(false, guard);
}

// Methods XLibraryContainer2
sal_Bool SAL_CALL SfxLibraryContainer::isLibraryLink( const OUString& Name )
{
    LibraryContainerMethodGuard aGuard( *this );
    SfxLibrary* pImplLib = getImplLib( Name );
    bool bRet = pImplLib->mbLink;
    return bRet;
}

OUString SAL_CALL SfxLibraryContainer::getLibraryLinkURL( const OUString& Name )
{
    LibraryContainerMethodGuard aGuard( *this );
    SfxLibrary* pImplLib = getImplLib( Name );
    bool bLink = pImplLib->mbLink;
    if( !bLink )
    {
        throw IllegalArgumentException(u"!link"_ustr, getXWeak(), 1);
    }
    OUString aRetStr = pImplLib->maLibInfoFileURL;
    return aRetStr;
}

sal_Bool SAL_CALL SfxLibraryContainer::isLibraryReadOnly( const OUString& Name )
{
    LibraryContainerMethodGuard aGuard( *this );
    SfxLibrary* pImplLib = getImplLib( Name );
    bool bRet = pImplLib->mbReadOnly || (pImplLib->mbLink && pImplLib->mbReadOnlyLink);
    return bRet;
}

void SAL_CALL SfxLibraryContainer::setLibraryReadOnly( const OUString& Name, sal_Bool bReadOnly )
{
    LibraryContainerMethodGuard aGuard( *this );
    std::unique_lock guard(m_aMutex);
    SfxLibrary* pImplLib = getImplLib( Name );
    if( pImplLib->mbLink )
    {
        if( pImplLib->mbReadOnlyLink != bool(bReadOnly) )
        {
            pImplLib->mbReadOnlyLink = bReadOnly;
            pImplLib->implSetModified(true, guard);
            maModifiable.setModified(true, guard);
        }
    }
    else
    {
        if( pImplLib->mbReadOnly != bool(bReadOnly) )
        {
            pImplLib->mbReadOnly = bReadOnly;
            pImplLib->implSetModified(true, guard);
        }
    }
}

void SAL_CALL SfxLibraryContainer::renameLibrary( const OUString& Name, const OUString& NewName )
{
    LibraryContainerMethodGuard aGuard( *this );
    std::unique_lock guard(m_aMutex);
    if( maNameContainer.hasByName( NewName ) )
    {
        throw ElementExistException();
    }
    // Get and hold library before removing
    rtl::Reference pImplLib(getImplLib(Name));

    // #i24094 Maybe lib is not loaded!
    if( pImplLib->mbPasswordProtected && !pImplLib->mbPasswordVerified )
    {
        return;     // Lib with unverified password cannot be renamed
    }
    loadLibrary_Impl(Name, guard);

    // Rename library folder, but not for linked libraries
    bool bMovedSuccessful = true;

    // Rename files
    bool bStorage = mxStorage.is();
    if( !bStorage && !pImplLib->mbLink )
    {
        bMovedSuccessful = false;

        OUString aLibDirPath = pImplLib->maStorageURL;
        // tdf#151741 - fill various storage URLs for the library
        // These URLs should not be empty for newly created libraries after
        // the change in SfxLibraryContainer::createLibrary_Impl.
        if (aLibDirPath.isEmpty())
        {
            checkStorageURL(pImplLib->maUnexpandedStorageURL, pImplLib->maLibInfoFileURL,
                            pImplLib->maStorageURL, pImplLib->maUnexpandedStorageURL);
        }

        INetURLObject aDestInetObj( o3tl::getToken(maLibraryPath, 1, ';'));
        aDestInetObj.insertName( NewName, true, INetURLObject::LAST_SEGMENT,
                                 INetURLObject::EncodeMechanism::All );
        OUString aDestDirPath = aDestInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );

        // Store new URL
        OUString aLibInfoFileURL = pImplLib->maLibInfoFileURL;
        checkStorageURL(aDestDirPath, pImplLib->maLibInfoFileURL, pImplLib->maStorageURL,
                         pImplLib->maUnexpandedStorageURL );

        try
        {
            if( mxSFI->isFolder( aLibDirPath ) )
            {
                if( !mxSFI->isFolder( aDestDirPath ) )
                {
                    mxSFI->createFolder( aDestDirPath );
                }
                // Move index file
                try
                {
                    if( mxSFI->exists( pImplLib->maLibInfoFileURL ) )
                    {
                        mxSFI->kill( pImplLib->maLibInfoFileURL );
                    }
                    mxSFI->move( aLibInfoFileURL, pImplLib->maLibInfoFileURL );
                }
                catch(const Exception& )
                {
                }

                for (auto& aElementName : pImplLib->getElementNames())
                {
                    INetURLObject aElementInetObj( aLibDirPath );
                    aElementInetObj.insertName( aElementName, false,
                        INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
                    aElementInetObj.setExtension( maLibElementFileExtension );
                    OUString aElementPath( aElementInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );

                    INetURLObject aElementDestInetObj( aDestDirPath );
                    aElementDestInetObj.insertName( aElementName, false,
                                                    INetURLObject::LAST_SEGMENT,
                                                    INetURLObject::EncodeMechanism::All );
                    aElementDestInetObj.setExtension( maLibElementFileExtension );
                    OUString aDestElementPath( aElementDestInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );

                    try
                    {
                        if( mxSFI->exists( aDestElementPath ) )
                        {
                            mxSFI->kill( aDestElementPath );
                        }
                        mxSFI->move( aElementPath, aDestElementPath );
                    }
                    catch(const Exception& )
                    {
                    }
                }
                pImplLib->storeResourcesAsURL( aDestDirPath, NewName );

                // Delete folder if empty
                Sequence< OUString > aContentSeq = mxSFI->getFolderContents( aLibDirPath, true );
                sal_Int32 nCount = aContentSeq.getLength();
                if( !nCount )
                {
                    mxSFI->kill( aLibDirPath );
                }

                bMovedSuccessful = true;
                pImplLib->implSetModified(true, guard);
            }
        }
        catch(const Exception& )
        {
        }
    }

    if( bStorage && !pImplLib->mbLink )
    {
        pImplLib->implSetModified(true, guard);
    }
    if( bMovedSuccessful )
    {
        // Remove the old library from the container and insert it back with the new name
        maNameContainer.removeByName(Name, guard);
        maNameContainer.insertByName(NewName, Any(Reference<XNameAccess>(pImplLib)), guard);
        maModifiable.setModified(true, guard);
    }
}


// Methods XInitialization
void SAL_CALL SfxLibraryContainer::initialize( const Sequence< Any >& _rArguments )
{
    LibraryContainerMethodGuard aGuard( *this );
    std::unique_lock guard(m_aMutex);
    sal_Int32 nArgCount = _rArguments.getLength();
    if ( nArgCount != 1 )
        throw IllegalArgumentException(u"too many args"_ustr, getXWeak(), -1);

    OUString sInitialDocumentURL;
    Reference< XStorageBasedDocument > xDocument;
    if ( _rArguments[0] >>= sInitialDocumentURL )
    {
        init(sInitialDocumentURL, nullptr, guard);
        return;
    }

    if ( _rArguments[0] >>= xDocument )
    {
        initializeFromDocument(xDocument, guard);
        return;
    }
    throw IllegalArgumentException(u"arg1 unknown type"_ustr, getXWeak(), 1);

}

void SfxLibraryContainer::initializeFromDocument( const Reference< XStorageBasedDocument >& _rxDocument,
                                                  std::unique_lock<std::mutex>& guard )
{
    // check whether this is a valid OfficeDocument, and obtain the document's root storage
    Reference< XStorage > xDocStorage;
    try
    {
        Reference< XServiceInfo > xSI( _rxDocument, UNO_QUERY_THROW );
        if ( xSI->supportsService(u"com.sun.star.document.OfficeDocument"_ustr))
        {
            xDocStorage.set( _rxDocument->getDocumentStorage(), UNO_SET_THROW );
        }
        Reference< XModel > xDocument( _rxDocument, UNO_QUERY_THROW );
        Reference< XComponent > xDocComponent( _rxDocument, UNO_QUERY_THROW );

        mxOwnerDocument = xDocument;
        startComponentListening( xDocComponent );
    }
    catch( const Exception& ) { }

    if ( !xDocStorage.is() )
    {
        throw IllegalArgumentException(u"no doc storage"_ustr, getXWeak(), 1);
    }
    init(OUString(), xDocStorage, guard);
}

// OEventListenerAdapter
void SfxLibraryContainer::_disposing( const EventObject& _rSource )
{
#if OSL_DEBUG_LEVEL > 0
    Reference< XModel > xDocument( mxOwnerDocument.get(), UNO_QUERY );
    SAL_WARN_IF(
        xDocument != _rSource.Source || !xDocument.is(), "basic",
        "SfxLibraryContainer::_disposing: where does this come from?");
#else
    (void)_rSource;
#endif
    dispose();
}

// OComponentHelper
void SfxLibraryContainer::disposing(std::unique_lock<std::mutex>& guard)
{
    Reference< XModel > xModel = mxOwnerDocument;
    EventObject aEvent( xModel );
    maVBAScriptListeners.disposeAndClear(guard, aEvent);
    stopAllComponentListening();
    mxOwnerDocument.clear();
}

// Methods XLibraryContainerPassword
sal_Bool SAL_CALL SfxLibraryContainer::isLibraryPasswordProtected( const OUString& )
{
    return false;
}

sal_Bool SAL_CALL SfxLibraryContainer::isLibraryPasswordVerified( const OUString& )
{
    throw IllegalArgumentException();
}

sal_Bool SAL_CALL SfxLibraryContainer::verifyLibraryPassword( const OUString&, const OUString& )
{
    throw IllegalArgumentException();
}

void SAL_CALL SfxLibraryContainer::changeLibraryPassword(const OUString&, const OUString&, const OUString& )
{
    throw IllegalArgumentException();
}

// Methods XContainer
void SAL_CALL SfxLibraryContainer::addContainerListener( const Reference< XContainerListener >& xListener )
{
    LibraryContainerMethodGuard aGuard( *this );
    std::unique_lock guard(m_aMutex);
    maNameContainer.setEventSource( getXWeak() );
    maNameContainer.addContainerListener(xListener, guard);
}

void SAL_CALL SfxLibraryContainer::removeContainerListener( const Reference< XContainerListener >& xListener )
{
    LibraryContainerMethodGuard aGuard( *this );
    std::unique_lock guard(m_aMutex);
    maNameContainer.removeContainerListener(xListener, guard);
}

// Methods XLibraryContainerExport
void SAL_CALL SfxLibraryContainer::exportLibrary( const OUString& Name, const OUString& URL,
    const Reference< XInteractionHandler >& Handler )
{
    LibraryContainerMethodGuard aGuard( *this );
    std::unique_lock guard(m_aMutex);
    SfxLibrary* pImplLib = getImplLib( Name );

    Reference< XSimpleFileAccess3 > xToUseSFI;
    if( Handler.is() )
    {
        xToUseSFI = ucb::SimpleFileAccess::create( mxContext );
        xToUseSFI->setInteractionHandler( Handler );
    }

    // Maybe lib is not loaded?!
    loadLibrary_Impl(Name, guard);

    uno::Reference< css::embed::XStorage > xDummyStor;
    if( pImplLib->mbPasswordProtected )
    {
        implStorePasswordLibrary( pImplLib, Name, xDummyStor, URL, xToUseSFI, Handler );
    }
    else
    {
        implStoreLibrary( pImplLib, Name, xDummyStor, URL, xToUseSFI, Handler );
    }
    ::xmlscript::LibDescriptor aLibDesc;
    aLibDesc.aName = Name;
    aLibDesc.bLink = false;             // Link status gets lost?
    aLibDesc.bReadOnly = pImplLib->mbReadOnly;
    aLibDesc.bPreload = false;          // Preload status gets lost?
    aLibDesc.bPasswordProtected = pImplLib->mbPasswordProtected;
    aLibDesc.aElementNames = pImplLib->getElementNames();

    implStoreLibraryIndexFile( pImplLib, aLibDesc, xDummyStor, URL, xToUseSFI );
}

OUString SfxLibraryContainer::expand_url( const OUString& url )
{
    if (url.startsWithIgnoreAsciiCase( "vnd.sun.star.expand:" ))
    {
        return comphelper::getExpandedUri(mxContext, url);
    }
    else if( mxStringSubstitution.is() )
    {
        OUString ret( mxStringSubstitution->substituteVariables( url, false ) );
        return ret;
    }
    else
    {
        return url;
    }
}

//XLibraryContainer3
OUString SAL_CALL SfxLibraryContainer::getOriginalLibraryLinkURL( const OUString& Name )
{
    LibraryContainerMethodGuard aGuard( *this );
    SfxLibrary* pImplLib = getImplLib( Name );
    bool bLink = pImplLib->mbLink;
    if( !bLink )
    {
        throw IllegalArgumentException(u"!link"_ustr, getXWeak(), 1);
    }
    OUString aRetStr = pImplLib->maOriginalStorageURL;
    return aRetStr;
}


// XVBACompatibility
sal_Bool SAL_CALL SfxLibraryContainer::getVBACompatibilityMode()
{
    return mbVBACompat;
}

void SAL_CALL SfxLibraryContainer::setVBACompatibilityMode( sal_Bool _vbacompatmodeon )
{
    /*  The member variable mbVBACompat must be set first, the following call
        to getBasicManager() may call getVBACompatibilityMode() which returns
        this value. */
    mbVBACompat = _vbacompatmodeon;
    BasicManager* pBasMgr = getBasicManager();
    if( !pBasMgr )
        return;

    // get the standard library
    OUString aLibName = pBasMgr->GetName();
    if ( aLibName.isEmpty())
    {
        aLibName = "Standard";
    }
    if( StarBASIC* pBasic = pBasMgr->GetLib( aLibName ) )
    {
        pBasic->SetVBAEnabled( _vbacompatmodeon );
    }
    /*  If in VBA compatibility mode, force creation of the VBA Globals
        object. Each application will create an instance of its own
        implementation and store it in its Basic manager. Implementations
        will do all necessary additional initialization, such as
        registering the global "This***Doc" UNO constant, starting the
        document events processor etc.
     */
    if( mbVBACompat ) try
    {
        Reference< XModel > xModel( mxOwnerDocument );   // weak-ref -> ref
        Reference< XMultiServiceFactory > xFactory( xModel, UNO_QUERY_THROW );
        xFactory->createInstance(u"ooo.vba.VBAGlobals"_ustr);
    }
    catch(const Exception& )
    {
    }
}

void SAL_CALL SfxLibraryContainer::setProjectName( const OUString& _projectname )
{
    msProjectName = _projectname;
    BasicManager* pBasMgr = getBasicManager();
    // Temporary HACK
    // Some parts of the VBA handling ( e.g. in core basic )
    // code expect the name of the VBA project to be set as the name of
    // the basic manager. Provide fail back here.
    if( pBasMgr )
    {
        pBasMgr->SetName( msProjectName );
    }
}

sal_Int32 SAL_CALL SfxLibraryContainer::getRunningVBAScripts()
{
    LibraryContainerMethodGuard aGuard( *this );
    return mnRunningVBAScripts;
}

void SAL_CALL SfxLibraryContainer::addVBAScriptListener( const Reference< vba::XVBAScriptListener >& rxListener )
{
    maVBAScriptListeners.addInterface(o3tl::temporary(std::unique_lock(m_aMutex)), rxListener);
}

void SAL_CALL SfxLibraryContainer::removeVBAScriptListener( const Reference< vba::XVBAScriptListener >& rxListener )
{
    maVBAScriptListeners.removeInterface(o3tl::temporary(std::unique_lock(m_aMutex)), rxListener);
}

void SAL_CALL SfxLibraryContainer::broadcastVBAScriptEvent( sal_Int32 nIdentifier, const OUString& rModuleName )
{
    // own lock for accessing the number of running scripts
    enterMethod();
    switch( nIdentifier )
    {
    case vba::VBAScriptEventId::SCRIPT_STARTED:
        ++mnRunningVBAScripts;
        break;
    case vba::VBAScriptEventId::SCRIPT_STOPPED:
        --mnRunningVBAScripts;
        break;
    }
    leaveMethod();

    Reference< XModel > xModel = mxOwnerDocument;  // weak-ref -> ref
    vba::VBAScriptEvent aEvent( Reference<XInterface>(xModel, UNO_QUERY), nIdentifier, rModuleName );
    maVBAScriptListeners.notifyEach(o3tl::temporary(std::unique_lock(m_aMutex)),
                                    &css::script::vba::XVBAScriptListener::notifyVBAScriptEvent,
                                    aEvent);
}

// Methods XPropertySet
css::uno::Reference<css::beans::XPropertySetInfo> SAL_CALL SfxLibraryContainer::getPropertySetInfo()
{
    return uno::Reference<beans::XPropertySetInfo>();
}

void SAL_CALL SfxLibraryContainer::setPropertyValue(const OUString& aPropertyName,
                                                    const uno::Any& aValue)
{
    if (aPropertyName != sVBATextEncodingPropName)
        throw UnknownPropertyException(aPropertyName, getXWeak());
    aValue >>= meVBATextEncoding;
}

css::uno::Any SAL_CALL SfxLibraryContainer::getPropertyValue(const OUString& aPropertyName)
{
    if (aPropertyName == sVBATextEncodingPropName)
        return uno::Any(meVBATextEncoding);
    throw UnknownPropertyException(aPropertyName, getXWeak());
}

void SAL_CALL SfxLibraryContainer::addPropertyChangeListener(
    const OUString& /* aPropertyName */, const Reference<XPropertyChangeListener>& /* xListener */)
{
    throw NoSupportException();
}

void SAL_CALL SfxLibraryContainer::removePropertyChangeListener(
    const OUString& /* aPropertyName */, const Reference<XPropertyChangeListener>& /* aListener */)
{
    throw NoSupportException();
}

void SAL_CALL SfxLibraryContainer::addVetoableChangeListener(
    const OUString& /* PropertyName */, const Reference<XVetoableChangeListener>& /* aListener */)
{
    throw NoSupportException();
}

void SAL_CALL SfxLibraryContainer::removeVetoableChangeListener(
    const OUString& /* PropertyName */, const Reference<XVetoableChangeListener>& /* aListener */)
{
    throw NoSupportException();
}

// Methods XServiceInfo
sal_Bool SAL_CALL SfxLibraryContainer::supportsService( const OUString& _rServiceName )
{
    return cppu::supportsService(this, _rServiceName);
}

// Implementation class SfxLibrary

// Ctor
SfxLibrary::SfxLibrary( ModifiableHelper& _rModifiable, const Type& aType,
    const Reference< XSimpleFileAccess3 >& xSFI )
        : mxSFI( xSFI )
        , mrModifiable( _rModifiable )
        , maNameContainer( aType, *this )
        , mbLoaded( true )
        , mbIsModified( true )
        , mbInitialised( false )
        , mbLink( false )
        , mbReadOnly( false )
        , mbReadOnlyLink( false )
        , mbPreload( false )
        , mbPasswordProtected( false )
        , mbPasswordVerified( false )
        , mbDoc50Password( false )
        , mbSharedIndexFile( false )
        , mbExtension( false )
{
}

SfxLibrary::SfxLibrary( ModifiableHelper& _rModifiable, const Type& aType,
    const Reference< XSimpleFileAccess3 >& xSFI,
    OUString aLibInfoFileURL, OUString aStorageURL, bool ReadOnly )
        : mxSFI( xSFI )
        , mrModifiable( _rModifiable )
        , maNameContainer( aType, *this )
        , mbLoaded( false )
        , mbIsModified( true )
        , mbInitialised( false )
        , mbLink( true )
        , mbReadOnly( false )
        , mbReadOnlyLink( ReadOnly )
        , mbPreload( false )
        , mbPasswordProtected( false )
        , mbPasswordVerified( false )
        , mbDoc50Password( false )
        , mbSharedIndexFile( false )
        , mbExtension( false )
        , maLibInfoFileURL(std::move( aLibInfoFileURL ))
        , maStorageURL(std::move( aStorageURL ))
{
}

bool SfxLibrary::isLoadedStorable()
{
    return mbLoaded && (!mbPasswordProtected || mbPasswordVerified);
}

void SfxLibrary::implSetModified(bool _bIsModified, std::unique_lock<std::mutex>& guard)
{
    if ( mbIsModified == _bIsModified )
    {
        return;
    }
    mbIsModified = _bIsModified;
    if ( mbIsModified )
    {
        mrModifiable.setModified(true, guard);
    }
}

// Methods XElementAccess
Type SfxLibrary::getElementType()
{
    return maNameContainer.getElementType();
}

sal_Bool SfxLibrary::hasElements()
{
    return maNameContainer.hasElements();
}

// Methods XNameAccess
Any SfxLibrary::getByName( const OUString& aName )
{
    impl_checkLoaded();

    return maNameContainer.getByName(aName);
}

Sequence< OUString > SfxLibrary::getElementNames()
{
    return maNameContainer.getElementNames();
}

sal_Bool SfxLibrary::hasByName( const OUString& aName )
{
    bool bRet = maNameContainer.hasByName( aName );
    return bRet;
}

void SfxLibrary::impl_checkReadOnly()
{
    if( mbReadOnly || (mbLink && mbReadOnlyLink) )
    {
        throw IllegalArgumentException(
            u"Library is readonly."_ustr,
            // TODO: resource
            *this, 0
        );
    }
}

void SfxLibrary::impl_checkLoaded()
{
    if ( !mbLoaded )
    {
        throw WrappedTargetException(
            OUString(),
            *this,
            Any( LibraryNotLoadedException(
                OUString(),
                *this
            ) )
        );
    }
}

// Methods XNameReplace
void SfxLibrary::replaceByName( const OUString& aName, const Any& aElement )
{
    impl_checkReadOnly();
    impl_checkLoaded();

    SAL_WARN_IF(
        !isLibraryElementValid(aElement), "basic",
        "SfxLibrary::replaceByName: replacing element is invalid!");

    std::unique_lock guard(m_aMutex);
    maNameContainer.replaceByName(aName, aElement, guard);
    implSetModified(true, guard);
}


// Methods XNameContainer
void SfxLibrary::insertByName( const OUString& aName, const Any& aElement )
{
    impl_checkReadOnly();
    impl_checkLoaded();

    SAL_WARN_IF(
        !isLibraryElementValid(aElement), "basic",
        "SfxLibrary::insertByName: to-be-inserted element is invalid!");

    std::unique_lock guard(m_aMutex);
    maNameContainer.insertByName(aName, aElement, guard);
    implSetModified(true, guard);
}

void SfxLibrary::impl_removeWithoutChecks(const OUString& _rElementName,
                                          std::unique_lock<std::mutex>& guard)
{
    maNameContainer.removeByName(_rElementName, guard);
    implSetModified(true, guard);

    // Remove element file
    if( maStorageURL.isEmpty() )
        return;

    INetURLObject aElementInetObj( maStorageURL );
    aElementInetObj.insertName( _rElementName, false,
                                INetURLObject::LAST_SEGMENT,
                                INetURLObject::EncodeMechanism::All );
    aElementInetObj.setExtension( maLibElementFileExtension );
    OUString aFile = aElementInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );

    try
    {
        if( mxSFI->exists( aFile ) )
        {
            mxSFI->kill( aFile );
        }
    }
    catch(const Exception& )
    {
        DBG_UNHANDLED_EXCEPTION("basic");
    }
}

void SfxLibrary::removeByName( const OUString& Name )
{
    impl_checkReadOnly();
    impl_checkLoaded();
    impl_removeWithoutChecks(Name, o3tl::temporary(std::unique_lock(m_aMutex)));
}

// Methods XContainer
void SAL_CALL SfxLibrary::addContainerListener( const Reference< XContainerListener >& xListener )
{
    maNameContainer.setEventSource( getXWeak() );
    maNameContainer.addContainerListener(xListener, o3tl::temporary(std::unique_lock(m_aMutex)));
}

void SAL_CALL SfxLibrary::removeContainerListener( const Reference< XContainerListener >& xListener )
{
    maNameContainer.removeContainerListener(xListener, o3tl::temporary(std::unique_lock(m_aMutex)));
}

// Methods XChangesNotifier
void SAL_CALL SfxLibrary::addChangesListener( const Reference< XChangesListener >& xListener )
{
    maNameContainer.setEventSource( getXWeak() );
    maNameContainer.addChangesListener(xListener, o3tl::temporary(std::unique_lock(m_aMutex)));
}

void SAL_CALL SfxLibrary::removeChangesListener( const Reference< XChangesListener >& xListener )
{
    maNameContainer.removeChangesListener(xListener, o3tl::temporary(std::unique_lock(m_aMutex)));
}


// Implementation class ScriptExtensionIterator

constexpr OUStringLiteral sBasicLibMediaType = u"application/vnd.sun.star.basic-library";
constexpr OUStringLiteral sDialogLibMediaType = u"application/vnd.sun.star.dialog-library";

ScriptExtensionIterator::ScriptExtensionIterator()
    : m_xContext( comphelper::getProcessComponentContext() )
    , m_eState( USER_EXTENSIONS )
    , m_bUserPackagesLoaded( false )
    , m_bSharedPackagesLoaded( false )
    , m_bBundledPackagesLoaded( false )
    , m_iUserPackage( 0 )
    , m_iSharedPackage( 0 )
       , m_iBundledPackage( 0 )
    , m_pScriptSubPackageIterator( nullptr )
{}

OUString ScriptExtensionIterator::nextBasicOrDialogLibrary( bool& rbPureDialogLib )
{
    OUString aRetLib;

    while( aRetLib.isEmpty() && m_eState != END_REACHED )
    {
        switch( m_eState )
        {
            case USER_EXTENSIONS:
            {
                Reference< deployment::XPackage > xScriptPackage =
                    implGetNextUserScriptPackage( rbPureDialogLib );
                if( !xScriptPackage.is() )
                {
                    break;
                }
                aRetLib = xScriptPackage->getURL();
                break;
            }

            case SHARED_EXTENSIONS:
            {
                Reference< deployment::XPackage > xScriptPackage =
                    implGetNextSharedScriptPackage( rbPureDialogLib );
                if( !xScriptPackage.is() )
                {
                    break;
                }
                aRetLib = xScriptPackage->getURL();
                break;
            }
            case BUNDLED_EXTENSIONS:
            {
                Reference< deployment::XPackage > xScriptPackage =
                    implGetNextBundledScriptPackage( rbPureDialogLib );
                if( !xScriptPackage.is() )
                {
                    break;
                }
                aRetLib = xScriptPackage->getURL();
                break;
            }
            case END_REACHED:
                SAL_WARN(
                    "basic",
                    ("ScriptExtensionIterator::nextBasicOrDialogLibrary():"
                     " Invalid case END_REACHED"));
                break;
        }
    }

    return aRetLib;
}

ScriptSubPackageIterator::ScriptSubPackageIterator( Reference< deployment::XPackage > const & xMainPackage )
    : m_xMainPackage( xMainPackage )
    , m_bIsValid( false )
    , m_bIsBundle( false )
    , m_nSubPkgCount( 0 )
    , m_iNextSubPkg( 0 )
{
    if( !m_xMainPackage.is() )
    {
        return;
    }
    // Check if parent package is registered
    beans::Optional< beans::Ambiguous<sal_Bool> > option( m_xMainPackage->isRegistered
        ( Reference<task::XAbortChannel>(), Reference<ucb::XCommandEnvironment>() ) );
    bool bRegistered = false;
    if( option.IsPresent )
    {
        beans::Ambiguous<sal_Bool> const & reg = option.Value;
        if( !reg.IsAmbiguous && reg.Value )
        {
            bRegistered = true;
        }
    }
    if( bRegistered )
    {
        m_bIsValid = true;
        if( m_xMainPackage->isBundle() )
        {
            m_bIsBundle = true;
            m_aSubPkgSeq = m_xMainPackage->getBundle( Reference<task::XAbortChannel>(),
                                                      Reference<ucb::XCommandEnvironment>() );
            m_nSubPkgCount = m_aSubPkgSeq.getLength();
        }
    }
}

Reference< deployment::XPackage > ScriptSubPackageIterator::getNextScriptSubPackage( bool& rbPureDialogLib )
{
    rbPureDialogLib = false;

    Reference< deployment::XPackage > xScriptPackage;
    if( !m_bIsValid )
    {
        return xScriptPackage;
    }
    if( m_bIsBundle )
    {
        sal_Int32 iPkg;
        for( iPkg = m_iNextSubPkg ; iPkg < m_nSubPkgCount ; ++iPkg )
        {
            const Reference<deployment::XPackage> xSubPkg = m_aSubPkgSeq[iPkg];
            xScriptPackage = implDetectScriptPackage( xSubPkg, rbPureDialogLib );
            if( xScriptPackage.is() )
            {
                break;
            }
        }
        m_iNextSubPkg = iPkg + 1;
    }
    else
    {
        xScriptPackage = implDetectScriptPackage( m_xMainPackage, rbPureDialogLib );
        m_bIsValid = false;     // No more script packages
    }

    return xScriptPackage;
}

Reference< deployment::XPackage > ScriptSubPackageIterator::implDetectScriptPackage ( const Reference< deployment::XPackage >& rPackage,
                                                                                      bool& rbPureDialogLib )
{
    Reference< deployment::XPackage > xScriptPackage;

    if( rPackage.is() )
    {
        const Reference< deployment::XPackageTypeInfo > xPackageTypeInfo = rPackage->getPackageType();
        OUString aMediaType = xPackageTypeInfo->getMediaType();
        if ( aMediaType == sBasicLibMediaType )
        {
            xScriptPackage = rPackage;
        }
        else if ( aMediaType == sDialogLibMediaType )
        {
            rbPureDialogLib = true;
            xScriptPackage = rPackage;
        }
    }

    return xScriptPackage;
}

Reference< deployment::XPackage > ScriptExtensionIterator::implGetNextUserScriptPackage( bool& rbPureDialogLib )
{
    Reference< deployment::XPackage > xScriptPackage;

    if( !m_bUserPackagesLoaded )
    {
        try
        {
            Reference< XExtensionManager > xManager = ExtensionManager::get( m_xContext );
            m_aUserPackagesSeq = xManager->getDeployedExtensions(u"user"_ustr,
                                                                 Reference< task::XAbortChannel >(),
                                                                 Reference< ucb::XCommandEnvironment >() );
        }
        catch(const css::uno::DeploymentException& )
        {
            // Special Office installations may not contain deployment code
            m_eState = END_REACHED;
            return xScriptPackage;
        }

        m_bUserPackagesLoaded = true;
    }

    if( m_iUserPackage == m_aUserPackagesSeq.getLength() )
    {
        m_eState = SHARED_EXTENSIONS;       // Later: SHARED_MODULE
    }
    else
    {
        if( m_pScriptSubPackageIterator == nullptr )
        {
            Reference<deployment::XPackage> xPackage = m_aUserPackagesSeq[m_iUserPackage];
            SAL_WARN_IF(
                !xPackage.is(), "basic",
                ("ScriptExtensionIterator::implGetNextUserScriptPackage():"
                 " Invalid package"));
            m_pScriptSubPackageIterator = new ScriptSubPackageIterator( xPackage );
        }

        xScriptPackage = m_pScriptSubPackageIterator->getNextScriptSubPackage( rbPureDialogLib );
        if( !xScriptPackage.is() )
        {
            delete m_pScriptSubPackageIterator;
            m_pScriptSubPackageIterator = nullptr;
            m_iUserPackage++;
        }
    }

    return xScriptPackage;
}

Reference< deployment::XPackage > ScriptExtensionIterator::implGetNextSharedScriptPackage( bool& rbPureDialogLib )
{
    Reference< deployment::XPackage > xScriptPackage;

    if( !m_bSharedPackagesLoaded )
    {
        try
        {
            Reference< XExtensionManager > xSharedManager = ExtensionManager::get( m_xContext );
            m_aSharedPackagesSeq = xSharedManager->getDeployedExtensions(u"shared"_ustr,
                                                                         Reference< task::XAbortChannel >(),
                                                                         Reference< ucb::XCommandEnvironment >() );
        }
        catch(const css::uno::DeploymentException& )
        {
            // Special Office installations may not contain deployment code
            return xScriptPackage;
        }

        m_bSharedPackagesLoaded = true;
    }

    if( m_iSharedPackage == m_aSharedPackagesSeq.getLength() )
    {
        m_eState = BUNDLED_EXTENSIONS;
    }
    else
    {
        if( m_pScriptSubPackageIterator == nullptr )
        {
            Reference<deployment::XPackage> xPackage = m_aSharedPackagesSeq[m_iSharedPackage];
            SAL_WARN_IF(
                !xPackage.is(), "basic",
                ("ScriptExtensionIterator::implGetNextSharedScriptPackage():"
                 " Invalid package"));
            m_pScriptSubPackageIterator = new ScriptSubPackageIterator( xPackage );
        }

        xScriptPackage = m_pScriptSubPackageIterator->getNextScriptSubPackage( rbPureDialogLib );
        if( !xScriptPackage.is() )
        {
            delete m_pScriptSubPackageIterator;
            m_pScriptSubPackageIterator = nullptr;
            m_iSharedPackage++;
        }
    }

    return xScriptPackage;
}

Reference< deployment::XPackage > ScriptExtensionIterator::implGetNextBundledScriptPackage( bool& rbPureDialogLib )
{
    Reference< deployment::XPackage > xScriptPackage;

    if( !m_bBundledPackagesLoaded )
    {
        try
        {
            Reference< XExtensionManager > xManager = ExtensionManager::get( m_xContext );
            m_aBundledPackagesSeq = xManager->getDeployedExtensions(u"bundled"_ustr,
                                                                    Reference< task::XAbortChannel >(),
                                                                    Reference< ucb::XCommandEnvironment >() );
        }
        catch(const css::uno::DeploymentException& )
        {
            // Special Office installations may not contain deployment code
            return xScriptPackage;
        }

        m_bBundledPackagesLoaded = true;
    }

    if( m_iBundledPackage == m_aBundledPackagesSeq.getLength() )
    {
        m_eState = END_REACHED;
    }
    else
    {
        if( m_pScriptSubPackageIterator == nullptr )
        {
            Reference<deployment::XPackage> xPackage = m_aBundledPackagesSeq[m_iBundledPackage];
            SAL_WARN_IF(
                !xPackage.is(), "basic",
                ("ScriptExtensionIterator::implGetNextBundledScriptPackage():"
                 " Invalid package"));
            m_pScriptSubPackageIterator = new ScriptSubPackageIterator( xPackage );
        }

        xScriptPackage = m_pScriptSubPackageIterator->getNextScriptSubPackage( rbPureDialogLib );
        if( !xScriptPackage.is() )
        {
            delete m_pScriptSubPackageIterator;
            m_pScriptSubPackageIterator = nullptr;
            m_iBundledPackage++;
        }
    }

    return xScriptPackage;
}

}   // namespace basic

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
