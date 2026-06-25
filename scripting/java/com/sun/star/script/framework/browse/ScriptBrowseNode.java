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
package com.sun.star.script.framework.browse;

import com.sun.star.beans.XIntrospectionAccess;

import com.sun.star.lang.XMultiComponentFactory;

import com.sun.star.lib.uno.helper.PropertySet;

import com.sun.star.script.browse.BrowseNodeTypes;
import com.sun.star.script.browse.XBrowseNode;
import com.sun.star.script.browse.XDeletableBrowseNode;
import com.sun.star.script.browse.XEditableBrowseNode;
import com.sun.star.script.browse.XRenamableBrowseNode;
import com.sun.star.script.framework.container.Parcel;
import com.sun.star.script.framework.container.ScriptEntry;
import com.sun.star.script.framework.container.ScriptMetaData;
import com.sun.star.script.framework.log.LogUtils;
import com.sun.star.script.framework.provider.ScriptProvider;
import com.sun.star.script.provider.XScriptContext;

import com.sun.star.ucb.XSimpleFileAccess;

import com.sun.star.uno.Any;
import com.sun.star.uno.AnyConverter;
import com.sun.star.uno.DeploymentException;
import com.sun.star.uno.Type;
import com.sun.star.uno.UnoRuntime;
import com.sun.star.uno.XComponentContext;

public class ScriptBrowseNode extends PropertySet implements
    XBrowseNode, XDeletableBrowseNode, XEditableBrowseNode, XRenamableBrowseNode {

    private final ScriptProvider provider;

    private Parcel parent;
    private String name;

    // these are properties, accessed by reflection
    public String uri;
    public String description;

    private boolean editable = false;
    private boolean deletable = false;
    private boolean renamable = false;

    public ScriptBrowseNode(ScriptProvider provider, Parcel parent, String name) {

        this.provider = provider;
        this.name = name;
        this.parent = parent;
        ScriptMetaData data = null;
        XComponentContext xCtx = provider.getScriptingContext().getComponentContext();
        XMultiComponentFactory xFac = xCtx.getServiceManager();

        try {
            data = parent.getByName( name );
            XSimpleFileAccess xSFA = UnoRuntime.queryInterface(
                                         XSimpleFileAccess.class,
                                         xFac.createInstanceWithContext(
                                             "com.sun.star.ucb.SimpleFileAccess",
                                             xCtx));

            uri = data.getShortFormScriptURL();
            description = data.getDescription();
            if (provider.hasScriptEditor()) {
                this.editable = true;

                try {
                    if (!parent.isUnoPkg()
                        && !xSFA.isReadOnly(parent.getPathToParcel())) {

                        this.deletable = true;
                        this.renamable = true;

                    }
                }
                // TODO propagate errors
                catch (Exception e) {
                    LogUtils.DEBUG("Caught exception in creation of ScriptBrowseNode");
                    LogUtils.DEBUG(LogUtils.getTrace(e));
                }

            }

        }
        // TODO fix exception types to be caught here, should we rethrow?
        catch (Exception e) {

            LogUtils.DEBUG("** caught exception getting script data for " + name +
                           " ->" + e.toString());

        }

        registerProperty("URI", new Type(String.class), (short)0, "uri");
        registerProperty("Description", new Type(String.class), (short)0,
                         "description");
    }

    public String getName() {
        return name;
    }

    public XBrowseNode[] getChildNodes() {
        return new XBrowseNode[0];
    }

    public boolean hasChildNodes() {
        return false;
    }

    public short getType() {
        return BrowseNodeTypes.SCRIPT;
    }

    @Override
    public String toString() {
        return getName();
    }

    public void updateURI(Parcel p) {
        parent = p;

        try {
            ScriptMetaData data = parent.getByName(name);
            uri = data.getShortFormScriptURL();
        }
        // TODO fix exception types to be caught here, should we rethrow?
        catch (Exception e) {
            LogUtils.DEBUG("** caught exception getting script data for " + name +
                           " ->" + e.toString());
        }
    }

    @Override
    public boolean isDeletableNode() {
        return deletable;
    }

    @Override
    public boolean isEditableNode() {
        return editable;
    }

    @Override
    public boolean isRenamableNode() {
        return renamable;
    }

    @Override
    public boolean editNode() {
        if (!editable) {
            throw new DeploymentException("Called editable on non-editable node");
        }

        XScriptContext ctxt = provider.getScriptingContext();
        ScriptMetaData data = null;

        try {
            data = parent.getByName(name);
        } catch (Exception e) {
            return false;
        }

        provider.getScriptEditor().edit(ctxt, data);

        return true;
    }

    @Override
    public boolean deleteNode() {
        if (!deletable) {
            throw new DeploymentException("Called delete on non-deletable node");
        }

        try {
            parent.removeByName(name);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    @Override
    public ScriptBrowseNode renameNode(String newName) {
        if (!renamable) {
            throw new DeploymentException("Called rename on non-renamable node");
        }

        try {
            ScriptMetaData oldData = parent.getByName(name);
            oldData.loadSource();
            String oldSource = oldData.getSource();

            LogUtils.DEBUG("Create renamed script");

            String languageName =
                newName + "." + provider.getScriptEditor().getExtension();

            String language = provider.getName();

            ScriptEntry entry = new ScriptEntry(language, languageName);

            ScriptMetaData data =
                new ScriptMetaData(parent, entry, oldSource);

            parent.insertByName(languageName, data);

            LogUtils.DEBUG("Now remove old script");
            parent.removeByName(name);

            uri = data.getShortFormScriptURL();
            name = languageName;
            return this;
        } catch (Exception e) {
            return null;
        }
    }
}
