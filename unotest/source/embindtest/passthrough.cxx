/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <cppuhelper/implbase.hxx>
#include <cppuhelper/supportsservice.hxx>

#include <com/sun/star/lang/XServiceInfo.hpp>
#include <org/libreoffice/embindtest/XPassthrough.hpp>

namespace com::sun::star::uno
{
class XComponentContext;
}

namespace
{
class Passthrough : public cppu::WeakImplHelper<org::libreoffice::embindtest::XPassthrough>
{
public:
    css::uno::Reference<css::uno::XInterface>
        SAL_CALL passthrough(const css::uno::Reference<css::uno::XInterface>& xInterface) override
    {
        return xInterface;
    }
};
}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
org_libreoffice_comp_embindtest_Passthrough_get_implementation(
    css::uno::XComponentContext*, css::uno::Sequence<css::uno::Any> const&)
{
    return cppu::acquire(new Passthrough);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
