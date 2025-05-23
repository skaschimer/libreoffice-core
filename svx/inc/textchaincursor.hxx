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

#include <sal/types.h>

class SdrObjEditView;
class SdrTextObj;
class KeyEvent;
enum class CursorChainingEvent : sal_uInt8;
struct ESelection;


class TextChainCursorManager
{
public:
    TextChainCursorManager(SdrObjEditView *pEditView, const SdrTextObj *pTextObj);

    bool HandleKeyEvent( const KeyEvent& rKEvt );

    // Used by HandledKeyEvent and basic building block for handling cursor event
    void HandleCursorEvent(const CursorChainingEvent aCurEvt,
                           const ESelection& aNewSel);

    // To be used after chaining event to deal with some nuisances
    void  HandleCursorEventAfterChaining(const CursorChainingEvent aCurEvt,
                                         const ESelection& aNewSel);

private:
    SdrObjEditView *mpEditView;
    const SdrTextObj *mpTextObj;

    // flag for handling of CANC which is kind of an exceptional case
    bool mbHandlingDel;

    void impChangeEditingTextObj(SdrTextObj *pTargetTextObj, ESelection aNewSel);
    void impDetectEvent(const KeyEvent& rKEvt,
                        CursorChainingEvent& rOutCursorEvt,
                        ESelection& rOutSel,
                        bool& rOutHandled);
};


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

