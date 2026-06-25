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
import com.sun.star.lib.uno.helper.ComponentBase;

import com.sun.star.script.browse.BrowseNodeTypes;
import com.sun.star.script.browse.XBrowseNode;
import com.sun.star.script.browse.XCreatableBrowseNode;
import com.sun.star.script.browse.XDeletableBrowseNode;
import com.sun.star.script.browse.XRenamableBrowseNode;
import com.sun.star.script.framework.container.Parcel;
import com.sun.star.script.framework.container.ParcelContainer;
import com.sun.star.script.framework.container.ScriptEntry;
import com.sun.star.script.framework.container.ScriptMetaData;
import com.sun.star.script.framework.log.LogUtils;
import com.sun.star.script.framework.provider.ScriptProvider;

import com.sun.star.ucb.XSimpleFileAccess;

import com.sun.star.uno.Any;
import com.sun.star.uno.AnyConverter;
import com.sun.star.uno.Type;
import com.sun.star.uno.UnoRuntime;
import com.sun.star.uno.XComponentContext;

import java.util.ArrayList;
import java.util.Collection;

import javax.swing.JOptionPane;

public class ParcelBrowseNode extends ComponentBase implements
    XBrowseNode, XCreatableBrowseNode, XDeletableBrowseNode, XRenamableBrowseNode {

    private final ScriptProvider provider;
    private Collection<XBrowseNode> browsenodes;
    private final ParcelContainer container;
    private Parcel parcel;
    private boolean deletable = true;
    private boolean creatable = false;
    private boolean renamable = true;

    public ParcelBrowseNode(ScriptProvider provider, ParcelContainer container,
                            String parcelName) throws
        com.sun.star.container.NoSuchElementException,
        com.sun.star.lang.WrappedTargetException {

        this.provider = provider;
        this.container = container;

        this.parcel = (Parcel)this.container.getByName(parcelName);

        if (provider.hasScriptEditor())
        {
            this.creatable = true;
        }

        String parcelDirUrl = parcel.getPathToParcel();
        XComponentContext xCtx = provider.getScriptingContext().getComponentContext();
        XMultiComponentFactory xFac = xCtx.getServiceManager();

        try {
            XSimpleFileAccess xSFA = UnoRuntime.queryInterface(XSimpleFileAccess.class,
                                      xFac.createInstanceWithContext(
                                          "com.sun.star.ucb.SimpleFileAccess",
                                          xCtx));
            if ( xSFA != null && ( xSFA.isReadOnly( parcelDirUrl ) ||
                 container.isUnoPkg() ) )
            {
                deletable = false;
                creatable = false;
                renamable = false;
            }
        } catch (com.sun.star.uno.Exception e) {
            // TODO propagate potential errors
            LogUtils.DEBUG("Caught exception creating ParcelBrowseNode " + e);
            LogUtils.DEBUG(LogUtils.getTrace(e));
        }

    }

    public String getName() {
        return parcel.getName();
    }

    public XBrowseNode[] getChildNodes() {
        try {

            if (hasChildNodes()) {
                String[] names = parcel.getElementNames();
                browsenodes = new ArrayList<XBrowseNode>(names.length);

                for (String name : names) {
                    browsenodes.add(new ScriptBrowseNode(provider, parcel, name));
                }
            } else {
                LogUtils.DEBUG("ParcelBrowseNode.getChildeNodes no children ");
                return new XBrowseNode[0];
            }
        } catch (Exception e) {
            LogUtils.DEBUG("Failed to getChildeNodes, exception: " + e);
            LogUtils.DEBUG(LogUtils.getTrace(e));
            return new XBrowseNode[0];
        }

        return browsenodes.toArray(new XBrowseNode[browsenodes.size()]);
    }

    public boolean hasChildNodes() {
        if (container != null && parcel != null && container.hasByName(getName())) {
            return parcel.hasElements();
        }

        return false;
    }

    public short getType() {
        return BrowseNodeTypes.CONTAINER;
    }

    @Override
    public String toString() {
        return getName();
    }

    @Override
    public boolean isCreatableNode() {
        return creatable;
    }

    @Override
    public boolean isDeletableNode() {
        return deletable;
    }

    @Override
    public boolean isRenamableNode() {
        return renamable;
    }

    @Override
    public ScriptBrowseNode createNode(String newName) {
        if (newName == null || newName.length() == 0)
            return null;

        try {
            String source = provider.getScriptEditor().getTemplate();

            String languageName =
                newName + "." + provider.getScriptEditor().getExtension();

            String language = container.getLanguage();

            ScriptEntry entry = new ScriptEntry(language, languageName);

            Parcel parcel = (Parcel)container.getByName(getName());
            ScriptMetaData data = new ScriptMetaData(parcel, entry, source);
            parcel.insertByName(languageName, data);

            ScriptBrowseNode sbn =
                new ScriptBrowseNode(provider, parcel, languageName);

            if (browsenodes == null) {
                LogUtils.DEBUG("browsenodes null!!");
                browsenodes = new ArrayList<XBrowseNode>(4);
            }

            browsenodes.add(sbn);
            return sbn;
        } catch (Exception e) {
            LogUtils.DEBUG("ParcelBrowseNode[create] failed with: " + e);
            LogUtils.DEBUG(LogUtils.getTrace(e));
        }
        return null;
    }

    @Override
    public boolean deleteNode() {
        try {
            if (container.deleteParcel(getName()))
                return true;
        } catch (Exception e) {
        }
        return false;
    }

    @Override
    public ParcelBrowseNode renameNode(String newName) {
        try {
            container.renameParcel(getName(), newName);
            Parcel p = (Parcel)container.getByName(newName);

            if (browsenodes == null) {
                getChildNodes();
            }

            if (browsenodes != null) {
                ScriptBrowseNode[] childNodes =
                    browsenodes.toArray(new ScriptBrowseNode[browsenodes.size()]);

                for (int index = 0; index < childNodes.length; index++) {
                    childNodes[ index ].updateURI(p);
                }
            }

            return this;
        } catch (Exception e) {
            return null;
        }
    }
}
