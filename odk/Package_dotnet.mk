# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t; fill-column: 100 -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Package_Package,odk_dotnet,$(INSTROOT)))

$(eval $(call gb_Package_set_outdir,odk_dotnet,$(INSTDIR)))

$(eval $(call gb_Package_add_files,odk_dotnet,$(SDKDIRNAME)/dotnet,\
	$(LIBO_SHARE_DOTNET_FOLDER)/net_basetypes.dll \
	$(LIBO_SHARE_DOTNET_FOLDER)/net_bridge.dll \
	$(LIBO_SHARE_DOTNET_FOLDER)/net_bridge.runtimeconfig.json \
	$(LIBO_SHARE_DOTNET_FOLDER)/net_oootypes.dll \
	$(LIBO_SHARE_DOTNET_FOLDER)/net_uretypes.dll \
))

$(eval $(call gb_Package_add_files,odk_dotnet,$(SDKDIRNAME)/dotnet,\
	$(SDKDIRNAME)/dotnet/LibreOffice.Bindings.0.1.0.nupkg \
	$(SDKDIRNAME)/dotnet/nuget.config \
))

# vim: set noet sw=4 ts=4:
