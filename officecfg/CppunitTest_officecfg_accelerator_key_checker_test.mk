# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t; fill-column: 100 -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,officecfg_accelerator_key_checker_test))

$(eval $(call gb_CppunitTest_add_exception_objects,officecfg_accelerator_key_checker_test, \
	officecfg/qa/acceleratorkeychecker \
))

$(eval $(call gb_CppunitTest_use_libraries,officecfg_accelerator_key_checker_test, \
	cppu \
	cppuhelper \
	sal \
	subsequenttest \
	test \
	unotest \
	utl \
	vcl \
	tl \
))

$(eval $(call gb_CppunitTest_use_api,officecfg_accelerator_key_checker_test, \
	udkapi \
	offapi \
    oovbaapi \
))

$(eval $(call gb_CppunitTest_use_externals,officecfg_accelerator_key_checker_test, \
	boost_headers \
	libxml2 \
))

$(eval $(call gb_CppunitTest_use_ure,officecfg_accelerator_key_checker_test))
$(eval $(call gb_CppunitTest_use_vcl,officecfg_accelerator_key_checker_test,services))
$(eval $(call gb_CppunitTest_use_rdb,officecfg_accelerator_key_checker_test,services))
$(eval $(call gb_CppunitTest_use_configuration,officecfg_accelerator_key_checker_test))

# vim: set noet sw=4 ts=4:
