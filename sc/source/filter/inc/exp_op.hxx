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

#include <tools/stream.hxx>
#include <memory>
#include "xeroot.hxx"

class ExcDocument;

class ExportTyp
{
protected:
                        ~ExportTyp() {}

    SvStream&           aOut;
public:
                        ExportTyp( SvStream& aStream ) : aOut( aStream ) {}

    virtual ErrCode     Write() = 0;
};

class ExportBiff5 : public ExportTyp, protected XclExpRoot
{
private:
    std::unique_ptr<ExcDocument>
                        pExcDoc;

protected:
    RootData*           pExcRoot;

public:
                        ExportBiff5( XclExpRootData& rExpData, SvStream& rStrm );
    virtual             ~ExportBiff5() override;
    ErrCode             Write() override;
};

class ExportBiff8 final : public ExportBiff5
{
public:
                        ExportBiff8( XclExpRootData& rExpData, SvStream& rStrm );
    virtual             ~ExportBiff8() override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
