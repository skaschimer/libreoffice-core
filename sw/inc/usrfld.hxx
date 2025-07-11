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

#include "swdllapi.h"
#include "fldbas.hxx"

class SwCalc;
class SwDoc;

/**
 * The shared part of a user field.
 *
 * Tracks the value, but conversion between the float and string representation
 * always happens with the system locale.
 */
class SW_DLLPUBLIC SwUserFieldType final : public SwValueFieldType
{
    bool    m_bValidValue : 1;
    bool    m_bDeleted : 1;
    /// Float value type.
    double  m_nValue;
    UIName  m_aName;
    /// String value type.
    OUString  m_aContent;
    /// Language used by m_aContents
    OUString m_aContentLang;
    SwUserType  m_nType;

public:
    SwUserFieldType( SwDoc* pDocPtr, const UIName& );

    virtual UIName          GetName() const override;
    virtual std::unique_ptr<SwFieldType> Copy() const override;

    OUString                Expand(sal_uInt32 nFormat, SwUserType nSubType, LanguageType nLng);

    OUString                GetContent( sal_uInt32 nFormat = 0 ) const;
           void             SetContent( const OUString& rStr, sal_uInt32 nFormat = 0 );

    OUString                GetInputOrDateTime( sal_uInt32 nFormat ) const;

    inline bool             IsValid() const;

           double           GetValue(SwCalc& rCalc);    // Recalculate member nValue.
    inline double           GetValue() const;
    inline void             SetValue(const double nVal);

    inline SwUserType       GetType() const;
    inline void             SetType(SwUserType);

    bool                    IsDeleted() const       { return m_bDeleted; }
    void                    SetDeleted( bool b )    { m_bDeleted = b; }

    virtual void QueryValue( css::uno::Any& rVal, sal_uInt16 nMId ) const override;
    virtual void PutValue( const css::uno::Any& rVal, sal_uInt16 nMId ) override;
    virtual void UpdateFields() override;
    void EnsureValid();
    void dumpAsXml(xmlTextWriterPtr pWriter) const override;

private:
    virtual void SwClientNotify(const SwModify&, const SfxHint&) override;
};

inline bool SwUserFieldType::IsValid() const
    { return m_bValidValue; }

inline double SwUserFieldType::GetValue() const
    { return m_nValue; }

inline void SwUserFieldType::SetValue(const double nVal)
    { m_nValue = nVal; }

inline SwUserType SwUserFieldType::GetType() const
    { return m_nType; }

inline void SwUserFieldType::SetType(SwUserType nSub)
{
    m_nType = nSub;
    EnableFormat(!(nSub & SwUserType::String));
}

/**
 * The non-shared part of a user field.
 *
 * Tracks the number format and the language, conversion between the float and
 * string representation is independent from the system locale.
 */
class SW_DLLPUBLIC SwUserField final : public SwValueField
{
    SwUserType  m_nSubType;

    virtual OUString    ExpandImpl(SwRootFrame const* pLayout) const override;
    virtual std::unique_ptr<SwField> Copy() const override;

public:
    SwUserField(SwUserFieldType*, SwUserType nSub, sal_uInt32 nFormat);

    SwUserType              GetSubType() const;
    void                    SetSubType(SwUserType nSub);

    virtual double          GetValue() const override;
    virtual void            SetValue( const double& rVal ) override;

    virtual OUString        GetFieldName() const override;

    // Name cannot be changed.
    virtual OUString        GetPar1() const override;

    // Content.
    virtual OUString        GetPar2() const override;
    virtual void            SetPar2(const OUString& rStr) override;
    virtual bool            QueryValue( css::uno::Any& rVal, sal_uInt16 nWhichId ) const override;
    virtual bool            PutValue( const css::uno::Any& rVal, sal_uInt16 nWhichId ) override;
    void dumpAsXml(xmlTextWriterPtr pWriter) const override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
