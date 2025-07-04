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

#include <framework/ConfigurationChangeRequest.hxx>
#include <com/sun/star/container/XNamed.hpp>
#include <comphelper/compbase.hxx>

namespace sd::framework {

typedef cppu::ImplInheritanceHelper <
      sd::framework::ConfigurationChangeRequest,
      css::container::XNamed
    > UpdateRequestInterfaceBase;

/** This update request is used to request configuration updates
    asynchronous when no other requests are being processed.  When there are
    other requests then we can simply wait until the last one is executed:
    the configuration is updated when the request queue becomes empty.  This
    is use by this implementation as well.  The execute() method does not
    really do anything.  This request just triggers the update of the
    configuration when it is removed as last request from the queue.
*/
class UpdateRequest final
    : public UpdateRequestInterfaceBase
{
public:
    UpdateRequest() noexcept;
    virtual ~UpdateRequest() noexcept override;

    // XConfigurationChangeOperation

    virtual void execute (
        const rtl::Reference<sd::framework::Configuration>& rxConfiguration) override;

    // XNamed

    /** Return a human readable string representation.  This is used for
        debugging purposes.
    */
    virtual OUString SAL_CALL getName() override;

    /** This call is ignored because the XNamed interface is (mis)used to
        give access to a human readable name for debugging purposes.
    */
    virtual void SAL_CALL setName (const OUString& rName) override;
};

} // end of namespace sd::framework

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
