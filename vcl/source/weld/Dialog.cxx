/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <vcl/abstdlg.hxx>
#include <vcl/weld/Dialog.hxx>

namespace weld
{
void Dialog::executeScreenshotAnnotationDialog()
{
    // open screenshot annotation dialog
    VclAbstractDialogFactory* pFact = VclAbstractDialogFactory::Create();
    ScopedVclPtr<AbstractScreenshotAnnotationDlg> pDialog
        = pFact->CreateScreenshotAnnotationDlg(*this);
    assert(pDialog);
    pDialog->Execute();
}

void Dialog::set_default_response(int nResponse)
{
    std::unique_ptr<weld::Button> pButton = weld_button_for_response(nResponse);
    change_default_button(nullptr, pButton.get());
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
