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


#include "moduldlg.hxx"
#include <basidesh.hxx>
#include <strings.hrc>
#include <bitmaps.hlst>
#include <iderdll.hxx>
#include "iderdll2.hxx"
#include <iderid.hxx>
#include <basobj.hxx>
#include <svx/passwd.hxx>
#include <ucbhelper/content.hxx>
#include <rtl/uri.hxx>
#include <sfx2/app.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/filedlghelper.hxx>
#include <sfx2/request.hxx>
#include <sfx2/sfxsids.hrc>
#include <sfx2/viewfrm.hxx>
#include <svl/stritem.hxx>
#include <tools/debug.hxx>
#include <tools/urlobj.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>

#include <com/sun/star/io/Pipe.hpp>
#include <com/sun/star/ui/dialogs/XFilePicker3.hpp>
#include <com/sun/star/ui/dialogs/XFolderPicker2.hpp>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>
#include <com/sun/star/script/DocumentScriptLibraryContainer.hpp>
#include <com/sun/star/script/DocumentDialogLibraryContainer.hpp>
#include <com/sun/star/script/XLibraryContainerPassword.hpp>
#include <com/sun/star/script/XLibraryContainerExport.hpp>
#include <com/sun/star/task/InteractionHandler.hpp>
#include <com/sun/star/ucb/SimpleFileAccess.hpp>
#include <com/sun/star/ucb/XCommandEnvironment.hpp>
#include <com/sun/star/ucb/NameClash.hpp>
#include <com/sun/star/packages/manifest/ManifestWriter.hpp>
#include <unotools/pathoptions.hxx>

#include <com/sun/star/util/VetoException.hpp>
#include <com/sun/star/script/ModuleSizeExceededRequest.hpp>

#include <comphelper/processfactory.hxx>
#include <comphelper/propertysequence.hxx>
#include <cppuhelper/implbase.hxx>
#include <o3tl/string_view.hxx>

#include <cassert>

namespace basctl
{

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::ucb;
using namespace ::com::sun::star::ui::dialogs;

namespace
{

class DummyInteractionHandler  : public ::cppu::WeakImplHelper< task::XInteractionHandler >
{
    Reference< task::XInteractionHandler2 > m_xHandler;
public:
    explicit DummyInteractionHandler(const Reference<task::XInteractionHandler2>& xHandler)
        : m_xHandler(xHandler)
    {
    }

    virtual void SAL_CALL handle( const Reference< task::XInteractionRequest >& rRequest ) override
    {
        if ( m_xHandler.is() )
        {
            script::ModuleSizeExceededRequest aModSizeException;
            if ( rRequest->getRequest() >>= aModSizeException )
                m_xHandler->handle( rRequest );
        }
    }
};

} // namespace

namespace
{
    int FindEntry(const weld::TreeView& rBox, std::u16string_view rName)
    {
        int nCount = rBox.n_children();
        for (int i = 0; i < nCount; ++i)
        {
            if (o3tl::equalsIgnoreAsciiCase(rName, rBox.get_text(i, 0)))
                return i;
        }
        return -1;
    }
}

// NewObjectDialog
IMPL_LINK_NOARG(NewObjectDialog, OkButtonHandler, weld::Button&, void)
{
    if (!m_bCheckName || IsValidSbxName(m_xEdit->get_text()))
        m_xDialog->response(RET_OK);
    else
    {
        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(m_xDialog.get(),
                                                       VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_BADSBXNAME)));
        xErrorBox->run();
        m_xEdit->grab_focus();
    }
}

NewObjectDialog::NewObjectDialog(weld::Window * pParent, ObjectMode eMode, bool bCheckName)
    : GenericDialogController(pParent, u"modules/BasicIDE/ui/newlibdialog.ui"_ustr, u"NewLibDialog"_ustr)
    , m_xEdit(m_xBuilder->weld_entry(u"entry"_ustr))
    , m_xOKButton(m_xBuilder->weld_button(u"ok"_ustr))
    , m_bCheckName(bCheckName)
{
    switch (eMode)
    {
        case ObjectMode::Library:
            m_xDialog->set_title(IDEResId(RID_STR_NEWLIB));
            break;
        case ObjectMode::Module:
            m_xDialog->set_title(IDEResId(RID_STR_NEWMOD));
            break;
        case ObjectMode::Dialog:
            m_xDialog->set_title(IDEResId(RID_STR_NEWDLG));
            break;
        default:
            assert(false);
    }
    m_xOKButton->connect_clicked(LINK(this, NewObjectDialog, OkButtonHandler));
}

// GotoLineDialog
GotoLineDialog::GotoLineDialog(weld::Window* pParent, sal_uInt32 nCurLine, sal_uInt32 nLineCount)
    : GenericDialogController(pParent, u"modules/BasicIDE/ui/gotolinedialog.ui"_ustr, u"GotoLineDialog"_ustr)
    , m_xSpinButton(m_xBuilder->weld_spin_button(u"spin"_ustr))
    , m_xLineCount(m_xBuilder->weld_label(u"line_count"_ustr))
    , m_xOKButton(m_xBuilder->weld_button(u"ok"_ustr))
    , m_nCurLine(nCurLine)
    , m_nLineCount(nLineCount)
{
    // Adjust line count label
    OUString sLabel = m_xLineCount->get_label();
    m_xLineCount->set_label(sLabel.replaceFirst("$1", OUString::number(m_nLineCount)));

    // Initialize the spin button
    m_xSpinButton->set_text(OUString::number(m_nCurLine));
    m_xSpinButton->set_range(1, m_nLineCount);
    m_xSpinButton->grab_focus();
    m_xSpinButton->select_region(0, -1);

    m_xOKButton->connect_clicked(LINK(this, GotoLineDialog, OkButtonHandler));
}

GotoLineDialog::~GotoLineDialog()
{
}

sal_Int32 GotoLineDialog::GetLineNumber() const
{
    return m_xSpinButton->get_value();
}

IMPL_LINK_NOARG(GotoLineDialog, OkButtonHandler, weld::Button&, void)
{
    // The number must be in the range between 1 and the number of lines in the module
    sal_Int32 nNumber = GetLineNumber();
    if (nNumber && nNumber >= 1 && nNumber <= static_cast<sal_Int32>(m_nLineCount))
    {
        m_xDialog->response(RET_OK);
    }
    else
    {
        m_xSpinButton->set_value(m_nCurLine);
        m_xSpinButton->select_region(0, -1);
    }
}

// ExportDialog
IMPL_LINK_NOARG(ExportDialog, OkButtonHandler, weld::Button&, void)
{
    m_bExportAsPackage = m_xExportAsPackageButton->get_active();
    m_xDialog->response(RET_OK);
}

ExportDialog::ExportDialog(weld::Window * pParent)
    : GenericDialogController(pParent, u"modules/BasicIDE/ui/exportdialog.ui"_ustr, u"ExportDialog"_ustr)
    , m_bExportAsPackage(false)
    , m_xExportAsPackageButton(m_xBuilder->weld_radio_button(u"extension"_ustr))
    , m_xOKButton(m_xBuilder->weld_button(u"ok"_ustr))
{
    m_xExportAsPackageButton->set_active(true);
    m_xOKButton->connect_clicked(LINK(this, ExportDialog, OkButtonHandler));
}

ExportDialog::~ExportDialog()
{
}

// LibPage
LibPage::LibPage(weld::Container* pParent, OrganizeDialog* pDialog)
    : OrganizePage(pParent, u"modules/BasicIDE/ui/libpage.ui"_ustr, u"LibPage"_ustr, pDialog)
    , m_xBasicsBox(m_xBuilder->weld_combo_box(u"location"_ustr))
    , m_xLibBox(m_xBuilder->weld_tree_view(u"library"_ustr))
    , m_xEditButton(m_xBuilder->weld_button(u"edit"_ustr))
    , m_xPasswordButton(m_xBuilder->weld_button(u"password"_ustr))
    , m_xNewLibButton(m_xBuilder->weld_button(u"new"_ustr))
    , m_xInsertLibButton(m_xBuilder->weld_button(u"import"_ustr))
    , m_xExportButton(m_xBuilder->weld_button(u"export"_ustr))
    , m_xDelButton(m_xBuilder->weld_button(u"delete"_ustr))
    , m_aCurDocument(ScriptDocument::getApplicationScriptDocument())
    , m_eCurLocation(LIBRARY_LOCATION_UNKNOWN)
{
    Size aSize(m_xLibBox->get_approximate_digit_width() * 40,
               m_xLibBox->get_height_rows(10));
    m_xLibBox->set_size_request(aSize.Width(), aSize.Height());

    // tdf#93476 The libraries should be listed alphabetically
    m_xLibBox->make_sorted();

    m_xEditButton->connect_clicked( LINK( this, LibPage, ButtonHdl ) );
    m_xNewLibButton->connect_clicked( LINK( this, LibPage, ButtonHdl ) );
    m_xPasswordButton->connect_clicked( LINK( this, LibPage, ButtonHdl ) );
    m_xExportButton->connect_clicked( LINK( this, LibPage, ButtonHdl ) );
    m_xInsertLibButton->connect_clicked( LINK( this, LibPage, ButtonHdl ) );
    m_xDelButton->connect_clicked( LINK( this, LibPage, ButtonHdl ) );
    m_xLibBox->connect_selection_changed(LINK(this, LibPage, TreeListHighlightHdl));

    m_xBasicsBox->connect_changed( LINK( this, LibPage, BasicSelectHdl ) );

    m_xLibBox->connect_editing(LINK(this, LibPage, EditingEntryHdl),
                               LINK(this, LibPage, EditedEntryHdl));

    FillListBox();
    m_xBasicsBox->set_active(0);
    SetCurLib();

    CheckButtons();
}

IMPL_LINK(LibPage, EditingEntryHdl, const weld::TreeIter&, rIter, bool)
{
    // check, if Standard library
    OUString aLibName = m_xLibBox->get_text(rIter, 0);

    if ( aLibName.equalsIgnoreAsciiCase( "Standard" ) )
    {
        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(m_pDialog->getDialog(),
                                                       VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_CANNOTCHANGENAMESTDLIB)));
        xErrorBox->run();
        return false;
    }

    // check, if library is readonly
    Reference< script::XLibraryContainer2 > xModLibContainer( m_aCurDocument.getLibraryContainer( E_SCRIPTS ) );
    Reference< script::XLibraryContainer2 > xDlgLibContainer( m_aCurDocument.getLibraryContainer( E_DIALOGS ) );
    if ( ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) && xModLibContainer->isLibraryReadOnly( aLibName ) && !xModLibContainer->isLibraryLink( aLibName ) ) ||
         ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) && xDlgLibContainer->isLibraryReadOnly( aLibName ) && !xDlgLibContainer->isLibraryLink( aLibName ) ) )
    {
        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(m_pDialog->getDialog(),
                                                       VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_LIBISREADONLY)));
        xErrorBox->run();
        return false;
    }

    // i24094: Password verification necessary for renaming
    if ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) && !xModLibContainer->isLibraryLoaded( aLibName ) )
    {
        bool bOK = true;
        // check password
        Reference< script::XLibraryContainerPassword > xPasswd( xModLibContainer, UNO_QUERY );
        if ( xPasswd.is() && xPasswd->isLibraryPasswordProtected( aLibName ) && !xPasswd->isLibraryPasswordVerified( aLibName ) )
        {
            OUString aPassword;
            bOK = QueryPassword(m_pDialog->getDialog(), xModLibContainer, aLibName, aPassword);
        }
        if ( !bOK )
            return false;
    }

    // TODO: check if library is reference/link

    return true;
}

IMPL_LINK(LibPage, EditedEntryHdl, const IterString&, rIterString, bool)
{
    const weld::TreeIter& rIter = rIterString.first;
    OUString sNewName = rIterString.second;

    bool bValid = sNewName.getLength() <= 30 && IsValidSbxName(sNewName);
    OUString aOldName(m_xLibBox->get_text(rIter, 0));

    if (bValid && aOldName != sNewName)
    {
        try
        {
            Reference< script::XLibraryContainer2 > xModLibContainer( m_aCurDocument.getLibraryContainer( E_SCRIPTS ) );
            if ( xModLibContainer.is() )
                xModLibContainer->renameLibrary( aOldName, sNewName );

            Reference< script::XLibraryContainer2 > xDlgLibContainer( m_aCurDocument.getLibraryContainer( E_DIALOGS ) );
            if ( xDlgLibContainer.is() )
                xDlgLibContainer->renameLibrary( aOldName, sNewName );

            MarkDocumentModified( m_aCurDocument );
            if (SfxBindings* pBindings = GetBindingsPtr())
            {
                pBindings->Invalidate( SID_BASICIDE_LIBSELECTOR );
                pBindings->Update( SID_BASICIDE_LIBSELECTOR );
            }
        }
        catch (const container::ElementExistException& )
        {
            std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(m_pDialog->getDialog(),
                                                           VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_SBXNAMEALLREADYUSED)));
            xErrorBox->run();
            return false;
        }
        catch (const container::NoSuchElementException& )
        {
            DBG_UNHANDLED_EXCEPTION("basctl.basicide");
            return false;
        }
    }

    if ( !bValid )
    {
        OUString sWarning(sNewName.getLength() > 30 ? IDEResId(RID_STR_LIBNAMETOLONG) : IDEResId(RID_STR_BADSBXNAME));
        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(m_pDialog->getDialog(),
                                                       VclMessageType::Warning, VclButtonsType::Ok, sWarning));
        xErrorBox->run();

    }

    return bValid;
}

LibPage::~LibPage()
{
    if (m_xBasicsBox)
    {
        const sal_Int32 nCount = m_xBasicsBox->get_count();
        for (sal_Int32 i = 0; i < nCount; ++i)
        {
            DocumentEntry* pEntry = weld::fromId<DocumentEntry*>(m_xBasicsBox->get_id(i));
            delete pEntry;
        }
    }
}

void LibPage::CheckButtons()
{
    std::unique_ptr<weld::TreeIter> xCur(m_xLibBox->make_iterator());
    if (!m_xLibBox->get_cursor(xCur.get()))
        return;

    OUString aLibName = m_xLibBox->get_text(*xCur, 0);
    Reference< script::XLibraryContainer2 > xModLibContainer( m_aCurDocument.getLibraryContainer( E_SCRIPTS ) );
    Reference< script::XLibraryContainer2 > xDlgLibContainer( m_aCurDocument.getLibraryContainer( E_DIALOGS ) );

    if ( m_eCurLocation == LIBRARY_LOCATION_SHARE )
    {
        m_xPasswordButton->set_sensitive(false);
        m_xNewLibButton->set_sensitive(false);
        m_xInsertLibButton->set_sensitive(false);
        m_xDelButton->set_sensitive(false);
    }
    else if ( aLibName.equalsIgnoreAsciiCase( "Standard" ) )
    {
        m_xPasswordButton->set_sensitive(false);
        m_xNewLibButton->set_sensitive(true);
        m_xInsertLibButton->set_sensitive(true);
        m_xExportButton->set_sensitive(false);
        m_xDelButton->set_sensitive(false);
    }
    else if ( ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) && xModLibContainer->isLibraryReadOnly( aLibName ) ) ||
              ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) && xDlgLibContainer->isLibraryReadOnly( aLibName ) ) )
    {
        m_xPasswordButton->set_sensitive(false);
        m_xNewLibButton->set_sensitive(true);
        m_xInsertLibButton->set_sensitive(true);
        if ( ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) && xModLibContainer->isLibraryReadOnly( aLibName ) && !xModLibContainer->isLibraryLink( aLibName ) ) ||
             ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) && xDlgLibContainer->isLibraryReadOnly( aLibName ) && !xDlgLibContainer->isLibraryLink( aLibName ) ) )
            m_xDelButton->set_sensitive(false);
        else
            m_xDelButton->set_sensitive(true);
    }
    else
    {
        if ( xModLibContainer.is() && !xModLibContainer->hasByName( aLibName ) )
            m_xPasswordButton->set_sensitive(false);
        else
            m_xPasswordButton->set_sensitive(true);

        m_xNewLibButton->set_sensitive(true);
        m_xInsertLibButton->set_sensitive(true);
        m_xExportButton->set_sensitive(true);
        m_xDelButton->set_sensitive(true);
    }
}

void LibPage::ActivatePage()
{
    SetCurLib();
}

IMPL_LINK_NOARG(LibPage, TreeListHighlightHdl, weld::TreeView&, void)
{
    CheckButtons();
}

IMPL_LINK_NOARG( LibPage, BasicSelectHdl, weld::ComboBox&, void )
{
    SetCurLib();
    CheckButtons();
}

IMPL_LINK( LibPage, ButtonHdl, weld::Button&, rButton, void )
{
    if (&rButton == m_xEditButton.get())
    {
        SfxAllItemSet aArgs( SfxGetpApp()->GetPool() );
        SfxRequest aRequest( SID_BASICIDE_APPEAR, SfxCallMode::SYNCHRON, aArgs );
        SfxGetpApp()->ExecuteSlot( aRequest );

        SfxUnoAnyItem aDocItem( SID_BASICIDE_ARG_DOCUMENT_MODEL, Any( m_aCurDocument.getDocumentOrNull() ) );

        std::unique_ptr<weld::TreeIter> xCurEntry(m_xLibBox->make_iterator());
        if (!m_xLibBox->get_cursor(xCurEntry.get()))
            return;
        OUString aLibName(m_xLibBox->get_text(*xCurEntry, 0));
        SfxStringItem aLibNameItem( SID_BASICIDE_ARG_LIBNAME, aLibName );
        if (SfxDispatcher* pDispatcher = GetDispatcher())
            pDispatcher->ExecuteList( SID_BASICIDE_LIBSELECTED,
                SfxCallMode::ASYNCHRON, { &aDocItem, &aLibNameItem });
        EndTabDialog();
        return;
    }
    else if (&rButton == m_xNewLibButton.get())
        NewLib();
    else if (&rButton == m_xInsertLibButton.get())
        InsertLib();
    else if (&rButton == m_xExportButton.get())
    {
        std::unique_ptr<weld::TreeIter> xCurEntry(m_xLibBox->make_iterator());
        if (!m_xLibBox->get_cursor(xCurEntry.get()))
            return;
        OUString aLibName(m_xLibBox->get_text(*xCurEntry, 0));
        Export(m_aCurDocument, aLibName, m_pDialog->getDialog());
    }
    else if (&rButton == m_xDelButton.get())
        DeleteCurrent();
    else if (&rButton == m_xPasswordButton.get())
    {
        std::unique_ptr<weld::TreeIter> xCurEntry(m_xLibBox->make_iterator());
        if (!m_xLibBox->get_cursor(xCurEntry.get()))
            return;
        OUString aLibName(m_xLibBox->get_text(*xCurEntry, 0));

        // load module library (if not loaded)
        Reference< script::XLibraryContainer > xModLibContainer = m_aCurDocument.getLibraryContainer( E_SCRIPTS );
        if ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) && !xModLibContainer->isLibraryLoaded( aLibName ) )
        {
            Shell* pShell = GetShell();
            if (pShell)
                pShell->GetViewFrame().GetWindow().EnterWait();
            xModLibContainer->loadLibrary( aLibName );
            if (pShell)
                pShell->GetViewFrame().GetWindow().LeaveWait();
        }

        // load dialog library (if not loaded)
        Reference< script::XLibraryContainer > xDlgLibContainer = m_aCurDocument.getLibraryContainer( E_DIALOGS );
        if ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) && !xDlgLibContainer->isLibraryLoaded( aLibName ) )
        {
            Shell* pShell = GetShell();
            if (pShell)
                pShell->GetViewFrame().GetWindow().EnterWait();
            xDlgLibContainer->loadLibrary( aLibName );
            if (pShell)
                pShell->GetViewFrame().GetWindow().LeaveWait();
        }

        // check, if library is password protected
        if ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) )
        {
            Reference< script::XLibraryContainerPassword > xPasswd( xModLibContainer, UNO_QUERY );
            if ( xPasswd.is() )
            {
                bool const bProtected = xPasswd->isLibraryPasswordProtected( aLibName );

                // change password dialog
                SvxPasswordDialog aDlg(m_pDialog->getDialog(), !bProtected);
                aDlg.SetCheckPasswordHdl(LINK(this, LibPage, CheckPasswordHdl));

                if (aDlg.run() == RET_OK)
                {
                    bool const bNewProtected = xPasswd->isLibraryPasswordProtected( aLibName );

                    if ( bNewProtected != bProtected )
                    {
                        int nPos = m_xLibBox->get_iter_index_in_parent(*xCurEntry);
                        m_xLibBox->remove(*xCurEntry);
                        ImpInsertLibEntry(aLibName, nPos);
                        m_xLibBox->set_cursor(nPos);
                    }

                    MarkDocumentModified( m_aCurDocument );
                }
            }
        }
    }
    CheckButtons();
}

IMPL_LINK( LibPage, CheckPasswordHdl, SvxPasswordDialog *, pDlg, bool )
{
    bool bRet = false;

    std::unique_ptr<weld::TreeIter> xCurEntry(m_xLibBox->make_iterator());
    if (!m_xLibBox->get_cursor(xCurEntry.get()))
        return bRet;

    OUString aLibName(m_xLibBox->get_text(*xCurEntry, 0));
    Reference< script::XLibraryContainerPassword > xPasswd( m_aCurDocument.getLibraryContainer( E_SCRIPTS ), UNO_QUERY );

    if ( xPasswd.is() )
    {
        try
        {
            OUString aOldPassword( pDlg->GetOldPassword() );
            OUString aNewPassword( pDlg->GetNewPassword() );
            xPasswd->changeLibraryPassword( aLibName, aOldPassword, aNewPassword );
            bRet = true;
        }
        catch (...)
        {
        }
    }

    return bRet;
}

void LibPage::NewLib()
{
    createLibImpl(m_pDialog->getDialog(), m_aCurDocument, m_xLibBox.get(), nullptr);
}

void LibPage::InsertLib()
{
    auto remove_entry = [this](OUString& rLibName) { // remove listbox entry
        int nEntry = FindEntry(*m_xLibBox, rLibName);
        if (nEntry != -1)
            m_xLibBox->remove(nEntry);
    };

    auto insert_entry = [this](OUString& rLibName) { // insert listbox entry
        m_xLibBox->make_unsorted();
        ImpInsertLibEntry(rLibName, m_xLibBox->n_children());
        m_xLibBox->make_sorted();
        m_xLibBox->set_cursor(m_xLibBox->find_text(rLibName));
    };

    ImportLib(m_aCurDocument, m_pDialog->getDialog(), remove_entry, insert_entry, {});
}

void ImportLib(const ScriptDocument& rDocument, weld::Dialog* pDialog,
               const std::function<void(OUString& rLibName)>& func_remove_entry,
               const std::function<void(OUString& rLibName)>& func_insert_entry,
               const std::function<void()>& func_insert_entries)
{
    basctl::EnsureIde();

    const Reference< uno::XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );
    // file open dialog
    sfx2::FileDialogHelper aDlg(ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE, FileDialogFlags::NONE, pDialog);
    aDlg.SetContext(sfx2::FileDialogHelper::BasicInsertLib);
    const Reference <XFilePicker3>& xFP = aDlg.GetFilePicker();

    xFP->setTitle(IDEResId(RID_STR_APPENDLIBS));

    // filter
    OUString aTitle(IDEResId(RID_STR_BASIC));
    xFP->appendFilter( aTitle, u"*.sbl;*.xlc;*.xlb"        // library files
              ";*.sdw;*.sxw;*.odt"       // text
              ";*.vor;*.stw;*.ott"       // text template
              ";*.sgl;*.sxg;*.odm"       // master document
              ";*.oth"                   // html document template
              ";*.sdc;*.sxc;*.ods"       // spreadsheet
              ";*.stc;*.ots"             // spreadsheet template
              ";*.sda;*.sxd;*.odg"       // drawing
              ";*.std;*.otg"             // drawing template
              ";*.sdd;*.sxi;*.odp"       // presentation
              ";*.sti;*.otp"             // presentation template
              ";*.sxm;*.odf"_ustr );          // formula

    OUString aLastFilter(GetExtraData()->GetAddLibFilter());
    if ( !aLastFilter.isEmpty() )
        xFP->setCurrentFilter( aLastFilter );
    else
        xFP->setCurrentFilter( IDEResId(RID_STR_BASIC) );

    if ( xFP->execute() != RET_OK )
            return;

    GetExtraData()->SetAddLibPath( xFP->getDisplayDirectory() );
    GetExtraData()->SetAddLibFilter( xFP->getCurrentFilter() );

    // library containers for import
    Reference< script::XLibraryContainer2 > xModLibContImport;
    Reference< script::XLibraryContainer2 > xDlgLibContImport;

    // file URLs
    Sequence< OUString > aFiles = xFP->getSelectedFiles();
    INetURLObject aURLObj( aFiles[0] );
    auto xModURLObj = std::make_shared<INetURLObject>(aURLObj);
    auto xDlgURLObj = std::make_shared<INetURLObject>(aURLObj);

    OUString aBase = aURLObj.getBase();
    OUString aModBase( u"script"_ustr );
    OUString aDlgBase( u"dialog"_ustr );

    if ( aBase == aModBase || aBase == aDlgBase )
    {
        xModURLObj->setBase( aModBase );
        xDlgURLObj->setBase( aDlgBase );
    }

    Reference< XSimpleFileAccess3 > xSFA( SimpleFileAccess::create(comphelper::getProcessComponentContext()) );

    OUString aModURL( xModURLObj->GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
    if ( xSFA->exists( aModURL ) )
    {
        xModLibContImport = script::DocumentScriptLibraryContainer::createWithURL(xContext, aModURL);
    }

    OUString aDlgURL( xDlgURLObj->GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
    if ( xSFA->exists( aDlgURL ) )
    {
        xDlgLibContImport = script::DocumentDialogLibraryContainer::createWithURL(xContext, aDlgURL);
    }

    if ( !xModLibContImport.is() && !xDlgLibContImport.is() )
        return;

    std::shared_ptr<LibDialog> xLibDlg;

    Sequence< OUString > aLibNames = GetMergedLibraryNames( xModLibContImport, xDlgLibContImport );
    if (aLibNames.hasElements())
    {
        // library import dialog
        xLibDlg = std::make_shared<LibDialog>(pDialog);
        xLibDlg->SetStorageName(aURLObj.getName());
        weld::TreeView& rView = xLibDlg->GetLibBox();
        rView.make_unsorted();
        rView.freeze();

        for (auto& aLibName : aLibNames)
        {
            // libbox entries
            if ( !( ( xModLibContImport.is() && xModLibContImport->hasByName( aLibName ) && xModLibContImport->isLibraryLink( aLibName ) ) ||
                    ( xDlgLibContImport.is() && xDlgLibContImport->hasByName( aLibName ) && xDlgLibContImport->isLibraryLink( aLibName ) ) ) )
            {
                rView.append();
                const int nRow = rView.n_children() - 1;
                rView.set_toggle(nRow, TRISTATE_TRUE);
                rView.set_text(nRow, aLibName, 0);
            }
        }

        rView.thaw();
        rView.make_sorted();

        if (rView.n_children())
            rView.set_cursor(0);
    }

    if (!xLibDlg)
    {
        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(pDialog,
                                                       VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_NOLIBINSTORAGE)));
        xErrorBox->run();
        return;
    }

    OUString aExtension( aURLObj.getExtension() );
    OUString aLibExtension( u"xlb"_ustr );
    OUString aContExtension( u"xlc"_ustr );

    // disable reference checkbox for documents and sbls
    if ( aExtension != aLibExtension && aExtension != aContExtension )
        xLibDlg->EnableReference(false);

    weld::DialogController::runAsync(
        xLibDlg,
        [aContExtension, xDlgURLObj = std::move(xDlgURLObj), aExtension, aLibExtension,
         xModURLObj = std::move(xModURLObj), xLibDlg, xDlgLibContImport, xModLibContImport,
         rDocument, pDialog, func_remove_entry, func_insert_entry,
         func_insert_entries](sal_Int32 nResult)
        {
            if (!nResult )
                return;

            bool bChanges = false;
            bool bRemove = false;
            bool bReplace = xLibDlg->IsReplace();
            bool bReference = xLibDlg->IsReference();
            weld::TreeView& rView = xLibDlg->GetLibBox();
            for (int nLib = 0, nChildren = rView.n_children(); nLib < nChildren; ++nLib)
            {
                if (rView.get_toggle(nLib) == TRISTATE_TRUE)
                {
                    OUString aLibName(rView.get_text(nLib));
                    Reference<script::XLibraryContainer2> xModLibContainer(
                        rDocument.getLibraryContainer(E_SCRIPTS));
                    Reference<script::XLibraryContainer2> xDlgLibContainer(
                        rDocument.getLibraryContainer(E_DIALOGS));

                    // check, if the library is already existing
                    if ( ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) ) ||
                         ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) ) )
                    {
                        if ( bReplace )
                        {
                            // check, if the library is the Standard library
                            if ( aLibName == "Standard" )
                            {
                                std::unique_ptr<weld::MessageDialog> xErrorBox(
                                    Application::CreateMessageDialog(
                                        pDialog, VclMessageType::Warning, VclButtonsType::Ok,
                                        IDEResId(RID_STR_REPLACESTDLIB)));
                                xErrorBox->run();
                                continue;
                            }

                            // check, if the library is readonly and not a link
                            if ( ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) && xModLibContainer->isLibraryReadOnly( aLibName ) && !xModLibContainer->isLibraryLink( aLibName ) ) ||
                                 ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) && xDlgLibContainer->isLibraryReadOnly( aLibName ) && !xDlgLibContainer->isLibraryLink( aLibName ) ) )
                            {
                                OUString aErrStr( IDEResId(RID_STR_REPLACELIB) );
                                aErrStr = aErrStr.replaceAll("XX", aLibName) + "\n" + IDEResId(RID_STR_LIBISREADONLY);
                                std::unique_ptr<weld::MessageDialog> xErrorBox(
                                    Application::CreateMessageDialog(pDialog,
                                                                     VclMessageType::Warning,
                                                                     VclButtonsType::Ok, aErrStr));
                                xErrorBox->run();
                                continue;
                            }

                            // remove existing libraries
                            bRemove = true;
                        }
                        else
                        {
                            OUString aErrStr;
                            if ( bReference )
                                aErrStr = IDEResId(RID_STR_REFNOTPOSSIBLE);
                            else
                                aErrStr = IDEResId(RID_STR_IMPORTNOTPOSSIBLE);
                            aErrStr = aErrStr.replaceAll("XX", aLibName) + "\n" +IDEResId(RID_STR_SBXNAMEALLREADYUSED);
                            std::unique_ptr<weld::MessageDialog> xErrorBox(
                                Application::CreateMessageDialog(pDialog, VclMessageType::Warning,
                                                                 VclButtonsType::Ok, aErrStr));
                            xErrorBox->run();
                            continue;
                        }
                    }

                    // check, if the library is password protected
                    bool bOK = false;
                    OUString aPassword;
                    if ( xModLibContImport.is() && xModLibContImport->hasByName( aLibName ) )
                    {
                        Reference< script::XLibraryContainerPassword > xPasswd( xModLibContImport, UNO_QUERY );
                        if ( xPasswd.is() && xPasswd->isLibraryPasswordProtected( aLibName ) && !xPasswd->isLibraryPasswordVerified( aLibName ) && !bReference )
                        {
                            bOK = QueryPassword(pDialog, xModLibContImport, aLibName, aPassword,
                                                true, true);

                            if ( !bOK )
                            {
                                OUString aErrStr( IDEResId(RID_STR_NOIMPORT) );
                                aErrStr = aErrStr.replaceAll("XX", aLibName);
                                std::unique_ptr<weld::MessageDialog> xErrorBox(
                                    Application::CreateMessageDialog(pDialog,
                                                                     VclMessageType::Warning,
                                                                     VclButtonsType::Ok, aErrStr));
                                xErrorBox->run();
                                continue;
                            }
                        }
                    }

                    // remove existing libraries
                    if ( bRemove )
                    {
                        if (func_remove_entry)
                            func_remove_entry(aLibName); // LibPage::InsertLib

                        // remove module library
                        if ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) )
                            xModLibContainer->removeLibrary( aLibName );

                        // remove dialog library
                        if ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) )
                            xDlgLibContainer->removeLibrary( aLibName );
                    }

                    // copy module library
                    if ( xModLibContImport.is() && xModLibContImport->hasByName( aLibName ) && xModLibContainer.is() && !xModLibContainer->hasByName( aLibName ) )
                    {
                        Reference< container::XNameContainer > xModLib;
                        if ( bReference )
                        {
                            // storage URL
                            INetURLObject aModStorageURLObj(*xModURLObj);
                            if ( aExtension == aContExtension )
                            {
                                sal_Int32 nCount = aModStorageURLObj.getSegmentCount();
                                aModStorageURLObj.insertName( aLibName, false, nCount-1 );
                                aModStorageURLObj.setExtension( aLibExtension );
                                aModStorageURLObj.setFinalSlash();
                            }
                            OUString aModStorageURL( aModStorageURLObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );

                            // create library link
                            xModLib.set( xModLibContainer->createLibraryLink( aLibName, aModStorageURL, true ), UNO_QUERY);
                        }
                        else
                        {
                            // create library
                            xModLib = xModLibContainer->createLibrary( aLibName );
                            if ( xModLib.is() )
                            {
                                // get import library
                                Reference< container::XNameContainer > xModLibImport;
                                Any aElement = xModLibContImport->getByName( aLibName );
                                aElement >>= xModLibImport;

                                if ( xModLibImport.is() )
                                {
                                    // load library
                                    if ( !xModLibContImport->isLibraryLoaded( aLibName ) )
                                        xModLibContImport->loadLibrary( aLibName );

                                    // copy all modules
                                    for (auto& aModName : xModLibImport->getElementNames())
                                    {
                                        Any aElement_ = xModLibImport->getByName( aModName );
                                        xModLib->insertByName( aModName, aElement_ );
                                    }

                                    // set password
                                    if ( bOK )
                                    {
                                        Reference< script::XLibraryContainerPassword > xPasswd( xModLibContainer, UNO_QUERY );
                                        if ( xPasswd.is() )
                                        {
                                            try
                                            {
                                                xPasswd->changeLibraryPassword( aLibName, OUString(), aPassword );
                                            }
                                            catch (...)
                                            {
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // copy dialog library
                    if ( xDlgLibContImport.is() && xDlgLibContImport->hasByName( aLibName ) && xDlgLibContainer.is() && !xDlgLibContainer->hasByName( aLibName ) )
                    {
                        Reference< container::XNameContainer > xDlgLib;
                        if ( bReference )
                        {
                            // storage URL
                            INetURLObject aDlgStorageURLObj( *xDlgURLObj );
                            if ( aExtension == aContExtension )
                            {
                                sal_Int32 nCount = aDlgStorageURLObj.getSegmentCount();
                                aDlgStorageURLObj.insertName( aLibName, false, nCount - 1 );
                                aDlgStorageURLObj.setExtension( aLibExtension );
                                aDlgStorageURLObj.setFinalSlash();
                            }
                            OUString aDlgStorageURL( aDlgStorageURLObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );

                            // create library link
                            xDlgLib.set( xDlgLibContainer->createLibraryLink( aLibName, aDlgStorageURL, true ), UNO_QUERY);
                        }
                        else
                        {
                            // create library
                            xDlgLib = xDlgLibContainer->createLibrary( aLibName );
                            if ( xDlgLib.is() )
                            {
                                // get import library
                                Reference< container::XNameContainer > xDlgLibImport;
                                Any aElement = xDlgLibContImport->getByName( aLibName );
                                aElement >>= xDlgLibImport;

                                if ( xDlgLibImport.is() )
                                {
                                    // load library
                                    if ( !xDlgLibContImport->isLibraryLoaded( aLibName ) )
                                        xDlgLibContImport->loadLibrary( aLibName );

                                    // copy all dialogs
                                    for (auto& aDlgName : xDlgLibImport->getElementNames())
                                    {
                                        Any aElement_ = xDlgLibImport->getByName( aDlgName );
                                        xDlgLib->insertByName( aDlgName, aElement_ );
                                    }
                                }
                            }
                        }
                    }
                    if (func_insert_entry)
                        func_insert_entry(aLibName); // LibPage::InsertLib
                    bChanges = true;
                }
            }

            if (bChanges)
            {
                if (func_insert_entries)
                    func_insert_entries(); // MacroManager
                MarkDocumentModified(rDocument);
            }
        });
}

void Export(const ScriptDocument& rDocument, const OUString& aLibName, weld::Dialog* pDialog)
{
    // Password verification
    Reference<script::XLibraryContainer2> xModLibContainer(rDocument.getLibraryContainer(E_SCRIPTS));

    if ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) && !xModLibContainer->isLibraryLoaded( aLibName ) )
    {
        bool bOK = true;

        // check password
        Reference< script::XLibraryContainerPassword > xPasswd( xModLibContainer, UNO_QUERY );
        if ( xPasswd.is() && xPasswd->isLibraryPasswordProtected( aLibName ) && !xPasswd->isLibraryPasswordVerified( aLibName ) )
        {
            OUString aPassword;
            bOK = QueryPassword(pDialog, xModLibContainer, aLibName, aPassword);
        }
        if ( !bOK )
            return;
    }

    std::unique_ptr<ExportDialog> xNewDlg(new ExportDialog(pDialog));
    if (xNewDlg->run() != RET_OK)
        return;

    try
    {
        bool bExportAsPackage = xNewDlg->isExportAsPackage();
        //tdf#112063 ensure closing xNewDlg is not selected as
        //parent of file dialog from ExportAs...
        xNewDlg.reset();
        if (bExportAsPackage)
            ExportAsPackage(rDocument, aLibName, pDialog);
        else
            ExportAsBasic(rDocument, aLibName, pDialog);
    }
    catch(const util::VetoException& ) // user canceled operation
    {
    }
}

void implExportLib(const ScriptDocument& rScriptDocument, const OUString& aLibName,
                   const OUString& aTargetURL, const Reference<task::XInteractionHandler>& Handler)
{
    Reference<script::XLibraryContainerExport> xModLibContainerExport(
        rScriptDocument.getLibraryContainer(E_SCRIPTS), UNO_QUERY);
    Reference<script::XLibraryContainerExport> xDlgLibContainerExport(
        rScriptDocument.getLibraryContainer(E_DIALOGS), UNO_QUERY);
    if ( xModLibContainerExport.is() )
        xModLibContainerExport->exportLibrary(aLibName, aTargetURL, Handler);

    if (!xDlgLibContainerExport.is())
        return;
    Reference<container::XNameAccess> xNameAcc(xDlgLibContainerExport, UNO_QUERY);
    if (!xNameAcc.is())
        return;
    if (!xNameAcc->hasByName(aLibName))
        return;
    xDlgLibContainerExport->exportLibrary(aLibName, aTargetURL, Handler);
}

// Implementation XCommandEnvironment

namespace {

class OLibCommandEnvironment : public cppu::WeakImplHelper< XCommandEnvironment >
{
    Reference< task::XInteractionHandler > mxInteraction;

public:
    explicit OLibCommandEnvironment(const Reference<task::XInteractionHandler>& xInteraction)
        : mxInteraction( xInteraction )
    {}

    // Methods
    virtual Reference< task::XInteractionHandler > SAL_CALL getInteractionHandler() override;
    virtual Reference< XProgressHandler > SAL_CALL getProgressHandler() override;
};

}

Reference< task::XInteractionHandler > OLibCommandEnvironment::getInteractionHandler()
{
    return mxInteraction;
}

Reference< XProgressHandler > OLibCommandEnvironment::getProgressHandler()
{
    Reference< XProgressHandler > xRet;
    return xRet;
}

void ExportAsPackage(const ScriptDocument& rScriptDocument, const OUString& aLibName,
                     weld::Dialog* pDialog)
{
    EnsureIde();
    // file open dialog
    sfx2::FileDialogHelper aDlg(ui::dialogs::TemplateDescription::FILESAVE_SIMPLE,
                                FileDialogFlags::NONE, pDialog);
    aDlg.SetContext(sfx2::FileDialogHelper::BasicExportPackage);
    const Reference <XFilePicker3>& xFP = aDlg.GetFilePicker();

    const Reference< uno::XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );
    Reference< task::XInteractionHandler2 > xHandler( task::InteractionHandler::createWithParent(xContext, nullptr) );
    Reference< XSimpleFileAccess3 > xSFA = SimpleFileAccess::create(xContext);

    xFP->setTitle(IDEResId(RID_STR_EXPORTPACKAGE));

    // filter
    OUString aTitle(IDEResId(RID_STR_PACKAGE_BUNDLE));
    xFP->appendFilter( aTitle, u"*.oxt"_ustr ); // library files

    xFP->setCurrentFilter( aTitle );

    if ( xFP->execute() != RET_OK )
        return;

    GetExtraData()->SetAddLibPath(xFP->getDisplayDirectory());

    Sequence< OUString > aFiles = xFP->getSelectedFiles();
    INetURLObject aURL( aFiles[0] );
    if( aURL.getExtension().isEmpty() )
        aURL.setExtension( u"oxt" );

    OUString aPackageURL( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );

    OUString aTmpPath = SvtPathOptions().GetTempPath();
    INetURLObject aInetObj( aTmpPath );
    aInetObj.insertName( aLibName, true, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
    OUString aSourcePath = aInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
    if( xSFA->exists( aSourcePath ) )
        xSFA->kill( aSourcePath );
    Reference< task::XInteractionHandler > xDummyHandler( new DummyInteractionHandler( xHandler ) );
    implExportLib(rScriptDocument, aLibName, aTmpPath, xDummyHandler);

    Reference< XCommandEnvironment > xCmdEnv = new OLibCommandEnvironment(xHandler);

    ::ucbhelper::Content sourceContent( aSourcePath, xCmdEnv, comphelper::getProcessComponentContext() );

    OUString destFolder = "vnd.sun.star.zip://" +
                          ::rtl::Uri::encode( aPackageURL,
                                              rtl_UriCharClassRegName,
                                              rtl_UriEncodeIgnoreEscapes,
                                              RTL_TEXTENCODING_UTF8 ) +
                          "/";

    if( xSFA->exists( aPackageURL ) )
        xSFA->kill( aPackageURL );

    ::ucbhelper::Content destFolderContent( destFolder, xCmdEnv, comphelper::getProcessComponentContext() );
    destFolderContent.transferContent(
        sourceContent, ::ucbhelper::InsertOperation::Copy,
        OUString(), NameClash::OVERWRITE );

    INetURLObject aMetaInfInetObj( aTmpPath );
    aMetaInfInetObj.insertName( u"META-INF",
        true, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );
    OUString aMetaInfFolder = aMetaInfInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE );
    if( xSFA->exists( aMetaInfFolder ) )
        xSFA->kill( aMetaInfFolder );
    xSFA->createFolder( aMetaInfFolder );

    std::vector< Sequence<beans::PropertyValue> > manifest;

    OUString fullPath = aLibName
                      + "/" ;
    auto attribs(::comphelper::InitPropertySequence({
        { "FullPath", Any(fullPath) },
        { "MediaType", Any(u"application/vnd.sun.star.basic-library"_ustr) }
    }));
    manifest.push_back( attribs );

    // write into pipe:
    Reference<packages::manifest::XManifestWriter> xManifestWriter = packages::manifest::ManifestWriter::create( xContext );
    Reference<io::XOutputStream> xPipe( io::Pipe::create( xContext ), UNO_QUERY_THROW );
    xManifestWriter->writeManifestSequence(
        xPipe, Sequence< Sequence<beans::PropertyValue> >(
            manifest.data(), manifest.size() ) );

    aMetaInfInetObj.insertName( u"manifest.xml",
        true, INetURLObject::LAST_SEGMENT, INetURLObject::EncodeMechanism::All );

    // write buffered pipe data to content:
    ::ucbhelper::Content manifestContent( aMetaInfInetObj.GetMainURL( INetURLObject::DecodeMechanism::NONE ), xCmdEnv, comphelper::getProcessComponentContext() );
    manifestContent.writeStream( Reference<io::XInputStream>( xPipe, UNO_QUERY_THROW ), true );

    ::ucbhelper::Content MetaInfContent( aMetaInfFolder, xCmdEnv, comphelper::getProcessComponentContext() );
    destFolderContent.transferContent(
        MetaInfContent, ::ucbhelper::InsertOperation::Copy,
        OUString(), NameClash::OVERWRITE );

    if( xSFA->exists( aSourcePath ) )
        xSFA->kill( aSourcePath );
    if( xSFA->exists( aMetaInfFolder ) )
        xSFA->kill( aMetaInfFolder );
}

void ExportAsBasic(const ScriptDocument& rScriptDocument, const OUString& aLibName,
                   weld::Dialog* pDialog)
{
    EnsureIde();
    // Folder picker
    const Reference< uno::XComponentContext >& xContext( ::comphelper::getProcessComponentContext() );
    Reference<XFolderPicker2> xFolderPicker = sfx2::createFolderPicker(xContext, pDialog);
    Reference< task::XInteractionHandler2 > xHandler( task::InteractionHandler::createWithParent(xContext, nullptr) );

    xFolderPicker->setTitle(IDEResId(RID_STR_EXPORTBASIC));

    // set display directory and filter
    OUString aPath =GetExtraData()->GetAddLibPath();
    if( aPath.isEmpty() )
        aPath = SvtPathOptions().GetWorkPath();

    // INetURLObject aURL(m_sSavePath, INetProtocol::File);
    xFolderPicker->setDisplayDirectory( aPath );
    short nRet = xFolderPicker->execute();
    if( nRet == RET_OK )
    {
        OUString aTargetURL = xFolderPicker->getDirectory();
        GetExtraData()->SetAddLibPath(aTargetURL);

        Reference< task::XInteractionHandler > xDummyHandler( new DummyInteractionHandler( xHandler ) );
        implExportLib(rScriptDocument, aLibName, aTargetURL, xDummyHandler);
    }
}

void LibPage::DeleteCurrent()
{
    std::unique_ptr<weld::TreeIter> xCurEntry(m_xLibBox->make_iterator());
    if (!m_xLibBox->get_cursor(xCurEntry.get()))
        return;
    OUString aLibName(m_xLibBox->get_text(*xCurEntry, 0));

    // check, if library is link
    bool bIsLibraryLink = false;
    Reference< script::XLibraryContainer2 > xModLibContainer( m_aCurDocument.getLibraryContainer( E_SCRIPTS ) );
    Reference< script::XLibraryContainer2 > xDlgLibContainer( m_aCurDocument.getLibraryContainer( E_DIALOGS ) );
    if ( ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) && xModLibContainer->isLibraryLink( aLibName ) ) ||
         ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) && xDlgLibContainer->isLibraryLink( aLibName ) ) )
    {
        bIsLibraryLink = true;
    }

    if (!QueryDelLib(aLibName, bIsLibraryLink, m_pDialog->getDialog()))
        return;

    // inform BasicIDE
    SfxUnoAnyItem aDocItem( SID_BASICIDE_ARG_DOCUMENT_MODEL, Any( m_aCurDocument.getDocumentOrNull() ) );
    SfxStringItem aLibNameItem( SID_BASICIDE_ARG_LIBNAME, aLibName );
    if (SfxDispatcher* pDispatcher = GetDispatcher())
        pDispatcher->ExecuteList(SID_BASICIDE_LIBREMOVED,
                  SfxCallMode::SYNCHRON, { &aDocItem, &aLibNameItem });

    // remove library from module and dialog library containers
    if ( xModLibContainer.is() && xModLibContainer->hasByName( aLibName ) )
        xModLibContainer->removeLibrary( aLibName );
    if ( xDlgLibContainer.is() && xDlgLibContainer->hasByName( aLibName ) )
        xDlgLibContainer->removeLibrary( aLibName );

    m_xLibBox->remove(*xCurEntry);
    MarkDocumentModified( m_aCurDocument );
}

void LibPage::EndTabDialog()
{
    m_pDialog->response(RET_OK);
}

void LibPage::FillListBox()
{
    InsertListBoxEntry( ScriptDocument::getApplicationScriptDocument(), LIBRARY_LOCATION_USER );
    InsertListBoxEntry( ScriptDocument::getApplicationScriptDocument(), LIBRARY_LOCATION_SHARE );

    ScriptDocuments aDocuments( ScriptDocument::getAllScriptDocuments( ScriptDocument::DocumentsSorted ) );
    for (auto const& doc : aDocuments)
    {
        InsertListBoxEntry( doc, LIBRARY_LOCATION_DOCUMENT );
    }
}

void LibPage::InsertListBoxEntry( const ScriptDocument& rDocument, LibraryLocation eLocation )
{
    OUString aEntryText(rDocument.getTitle(eLocation));
    OUString sId(weld::toId(new DocumentEntry(rDocument, eLocation)));
    m_xBasicsBox->append(sId,  aEntryText);
}

void LibPage::SetCurLib()
{
    DocumentEntry* pEntry = weld::fromId<DocumentEntry*>(m_xBasicsBox->get_active_id());
    if (!pEntry)
        return;

    const ScriptDocument& aDocument( pEntry->GetDocument() );
    DBG_ASSERT( aDocument.isAlive(), "LibPage::SetCurLib: no document, or document is dead!" );
    if ( !aDocument.isAlive() )
        return;
    LibraryLocation eLocation = pEntry->GetLocation();
    if ( aDocument == m_aCurDocument && eLocation == m_eCurLocation )
        return;

    m_aCurDocument = aDocument;
    m_eCurLocation = eLocation;
    m_xLibBox->clear();

    // get a sorted list of library names
    int nEntry = 0;
    for (auto& aLibName : aDocument.getLibraryNames())
    {
        if (eLocation == aDocument.getLibraryLocation(aLibName))
            ImpInsertLibEntry(aLibName, nEntry++);
    }

    int nEntry_ = FindEntry(*m_xLibBox, u"Standard");
    if (nEntry_ == -1 && m_xLibBox->n_children())
        nEntry_ = 0;
    m_xLibBox->set_cursor(nEntry_);

    m_xLibBox->columns_autosize();
}

void LibPage::ImpInsertLibEntry( const OUString& rLibName, int nPos )
{
    // check, if library is password protected
    bool bProtected = false;
    Reference< script::XLibraryContainer2 > xModLibContainer( m_aCurDocument.getLibraryContainer( E_SCRIPTS ) );
    if ( xModLibContainer.is() && xModLibContainer->hasByName( rLibName ) )
    {
        Reference< script::XLibraryContainerPassword > xPasswd( xModLibContainer, UNO_QUERY );
        if ( xPasswd.is() )
        {
            bProtected = xPasswd->isLibraryPasswordProtected( rLibName );
        }
    }

    m_xLibBox->insert_text(nPos, rLibName);

    if (bProtected)
        m_xLibBox->set_image(nPos, RID_BMP_LOCKED);

    // check, if library is link
    if ( xModLibContainer.is() && xModLibContainer->hasByName( rLibName ) && xModLibContainer->isLibraryLink( rLibName ) )
    {
        OUString aLinkURL = xModLibContainer->getLibraryLinkURL( rLibName );
        m_xLibBox->set_text(nPos, aLinkURL, 1);
    }
}

// Helper function
void createLibImpl(weld::Window* pWin, const ScriptDocument& rDocument,
                   weld::TreeView* pLibBox, SbTreeListBox* pBasicBox)
{
    OSL_ENSURE( rDocument.isAlive(), "createLibImpl: invalid document!" );
    if ( !rDocument.isAlive() )
        return;

    // create library name
    OUString aLibName;
    bool bValid = false;
    sal_Int32 i = 1;
    while ( !bValid )
    {
        aLibName = "Library" + OUString::number( i );
        if ( !rDocument.hasLibrary( E_SCRIPTS, aLibName ) && !rDocument.hasLibrary( E_DIALOGS, aLibName ) )
            bValid = true;
        i++;
    }

    NewObjectDialog aNewDlg(pWin, ObjectMode::Library);
    aNewDlg.SetObjectName(aLibName);

    if (!aNewDlg.run())
        return;

    if (!aNewDlg.GetObjectName().isEmpty())
        aLibName = aNewDlg.GetObjectName();

    if ( aLibName.getLength() > 30 )
    {
        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(pWin,
                                                       VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_LIBNAMETOLONG)));
        xErrorBox->run();
    }
    else if ( !IsValidSbxName( aLibName ) )
    {
        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(pWin,
                                                       VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_BADSBXNAME)));
        xErrorBox->run();
    }
    else if ( rDocument.hasLibrary( E_SCRIPTS, aLibName ) || rDocument.hasLibrary( E_DIALOGS, aLibName ) )
    {
        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(pWin,
                                                       VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_SBXNAMEALLREADYUSED2)));
        xErrorBox->run();
    }
    else
    {
        try
        {
            // create module and dialog library
            rDocument.getOrCreateLibrary( E_SCRIPTS, aLibName );
            rDocument.getOrCreateLibrary( E_DIALOGS, aLibName );

            if( pLibBox )
            {
                pLibBox->append_text(aLibName);
                pLibBox->set_cursor(pLibBox->find_text(aLibName));
            }

            // create a module
            OUString aModName = rDocument.createObjectName( E_SCRIPTS, aLibName );
            OUString sModuleCode;
            if ( !rDocument.createModule( aLibName, aModName, true, sModuleCode ) )
                throw Exception("could not create module " + aModName, nullptr);

            // tdf#151741 - store all libraries to the file system, otherwise they
            // cannot be renamed/moved since the SfxLibraryContainer::renameLibrary
            // moves the folders/files on the file system
            Reference<script::XLibraryContainer2> xModLibContainer(
                rDocument.getLibraryContainer(E_SCRIPTS));
            Reference<script::XLibraryContainer2> xDlgLibContainer(
                rDocument.getLibraryContainer(E_DIALOGS));
            Reference<script::XPersistentLibraryContainer> xModPersLibContainer(xModLibContainer,
                                                                                UNO_QUERY);
            if (xModPersLibContainer.is())
                xModPersLibContainer->storeLibraries();
            Reference<script::XPersistentLibraryContainer> xDlgPersLibContainer(xDlgLibContainer,
                                                                                UNO_QUERY);
            if (xDlgPersLibContainer.is())
                xDlgPersLibContainer->storeLibraries();

            SbxItem aSbxItem( SID_BASICIDE_ARG_SBX, rDocument, aLibName, aModName, SBX_TYPE_MODULE );
            if (SfxDispatcher* pDispatcher = GetDispatcher())
                pDispatcher->ExecuteList(SID_BASICIDE_SBXINSERTED,
                                      SfxCallMode::SYNCHRON, { &aSbxItem });

            if( pBasicBox )
            {
                std::unique_ptr<weld::TreeIter> xIter(pBasicBox->make_iterator(nullptr));
                bool bValidIter = pBasicBox->get_cursor(xIter.get());
                std::unique_ptr<weld::TreeIter> xRootEntry(pBasicBox->make_iterator(xIter.get()));
                while (bValidIter)
                {
                    pBasicBox->copy_iterator(*xIter, *xRootEntry);
                    bValidIter = pBasicBox->iter_parent(*xIter);
                }

                BrowseMode nMode = pBasicBox->GetMode();
                bool bDlgMode = ( nMode & BrowseMode::Dialogs ) && !( nMode & BrowseMode::Modules );
                const auto sId = bDlgMode ? RID_BMP_DLGLIB : RID_BMP_MODLIB;
                pBasicBox->AddEntry(aLibName, sId, xRootEntry.get(), false, std::make_unique<Entry>(OBJ_TYPE_LIBRARY));
                pBasicBox->AddEntry(aModName, RID_BMP_MODULE, xRootEntry.get(), false, std::make_unique<Entry>(OBJ_TYPE_MODULE));
                pBasicBox->set_cursor(*xRootEntry);
                pBasicBox->select(*xRootEntry);
            }
        }
        catch (const uno::Exception& )
        {
            DBG_UNHANDLED_EXCEPTION("basctl.basicide");
        }
    }
}

} // namespace basctl

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
