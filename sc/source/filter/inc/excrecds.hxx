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

#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <tools/color.hxx>

#include "xerecord.hxx"
#include "xeroot.hxx"
#include "excdefs.hxx"

//------------------------------------------------------------------ Forwards -

class ScDBData;
struct ScQueryEntry;

//----------------------------------------------------------- class ExcRecord -

class ExcRecord : public XclExpRecord
{
public:
    virtual void            Save( XclExpStream& rStrm ) override;
    virtual void            SaveXml( XclExpXmlStream& rStrm ) override;

    virtual sal_uInt16          GetNum() const = 0;
    virtual std::size_t     GetLen() const = 0;

protected:
    virtual void            SaveCont( XclExpStream& rStrm );

private:
    /** Writes the body of the record. */
    virtual void            WriteBody( XclExpStream& rStrm ) override;
};

//--------------------------------------------------------- class ExcEmptyRec -

class ExcEmptyRec : public ExcRecord
{
private:
protected:
public:
    virtual void            Save( XclExpStream& rStrm ) override;
    virtual sal_uInt16          GetNum() const override;
    virtual std::size_t     GetLen() const override;
};

//--------------------------------------------------------- class ExcDummyRec -

class ExcDummyRec : public ExcRecord
{
protected:
public:
    virtual void            Save( XclExpStream& rStrm ) override;
    virtual sal_uInt16          GetNum() const override;
    virtual const sal_uInt8*        GetData() const = 0;    // byte data must contain header and body
};

//------------------------------------------------------- class ExcBoolRecord -
// stores bool as 16bit val ( 0x0000 | 0x0001 )

class ExcBoolRecord : public ExcRecord
{
private:
    virtual void            SaveCont( XclExpStream& rStrm ) override;

protected:
    bool                    bVal;

    ExcBoolRecord() : bVal( false ) {}

public:
    virtual std::size_t     GetLen() const override;
};

//--------------------------------------------------------- class ExcBof_Base -

class ExcBof_Base : public ExcRecord
{
private:
protected:
    sal_uInt16                  nDocType;
    sal_uInt16                  nVers;
    sal_uInt16                  nRupBuild;
    sal_uInt16                  nRupYear;
public:
                            ExcBof_Base();
};

//-------------------------------------------------------------- class ExcBof -
// Header Record for WORKSHEETS

class ExcBof final : public ExcBof_Base
{
private:
    virtual void            SaveCont( XclExpStream& rStrm ) override;
public:
                            ExcBof();

    virtual sal_uInt16          GetNum() const override;
    virtual std::size_t     GetLen() const override;
};

//------------------------------------------------------------- class ExcBofW -
// Header Record for WORKBOOKS

class ExcBofW final : public ExcBof_Base
{
private:
    virtual void            SaveCont( XclExpStream& rStrm ) override;
public:
                            ExcBofW();

    virtual sal_uInt16          GetNum() const override;
    virtual std::size_t     GetLen() const override;
};

//-------------------------------------------------------------- class ExcEof -

class ExcEof final : public ExcRecord
{
private:
public:
    virtual sal_uInt16          GetNum() const override;
    virtual std::size_t     GetLen() const override;
};

//--------------------------------------------------------- class ExcDummy_00 -
// INTERFACEHDR to FNGROUPCOUNT (see excrecds.cxx)

class ExcDummy_00 final : public ExcDummyRec
{
private:
    static const sal_uInt8      pMyData[];
    static const std::size_t   nMyLen;
public:
    virtual std::size_t        GetLen() const override;
    virtual const sal_uInt8*        GetData() const override;
};

// EXC_ID_WINDOWPROTECTION
class XclExpWindowProtection final : public XclExpBoolRecord
{
    public:
        XclExpWindowProtection(bool bValue);

    virtual void            SaveXml( XclExpXmlStream& rStrm ) override;
};

// EXC_ID_PROTECT  Document Protection
class XclExpProtection : public XclExpBoolRecord
{
    public:
        XclExpProtection(bool bValue);
};

class XclExpSheetProtection final : public XclExpProtection
{
    SCTAB                   mnTab;
    public:
        XclExpSheetProtection(bool bValue, SCTAB nTab);
    virtual void            SaveXml( XclExpXmlStream& rStrm ) override;
};

class XclExpPassHash final : public XclExpRecord
{
public:
    XclExpPassHash(const css::uno::Sequence<sal_Int8>& aHash);
    virtual ~XclExpPassHash() override;

private:
    virtual void    WriteBody(XclExpStream& rStrm) override;

private:
    sal_uInt16  mnHash;
};

//-------------------------------------------------------- class ExcDummy_04x -
// PASSWORD to BOOKBOOL (see excrecds.cxx), no 1904

class ExcDummy_040 final : public ExcDummyRec
{
private:
    static const sal_uInt8      pMyData[];
    static const std::size_t   nMyLen;
public:
    virtual std::size_t        GetLen() const override;
    virtual const sal_uInt8*        GetData() const override;
};

class ExcDummy_041 final : public ExcDummyRec
{
private:
    static const sal_uInt8     pMyData[];
    static const std::size_t   nMyLen;
public:
    virtual std::size_t        GetLen() const override;
    virtual const sal_uInt8*   GetData() const override;
};

//------------------------------------------------------------- class Exc1904 -

class Exc1904 final : public ExcBoolRecord
{
public:
                            Exc1904( const ScDocument& rDoc );
    virtual sal_uInt16      GetNum() const override;

    virtual void            SaveXml( XclExpXmlStream& rStrm ) override;
private:
    bool                    bDateCompatibility;
};

//------------------------------------------------------ class ExcBundlesheet -

class ExcBundlesheetBase : public ExcRecord
{
protected:
    sal_uInt64              m_nStrPos;
    sal_uInt64              m_nOwnPos;    // Position after # and Len
    sal_uInt16              nGrbit;
    SCTAB                   nTab;

                            ExcBundlesheetBase();

public:
                            ExcBundlesheetBase( const RootData& rRootData, SCTAB nTab );

    void                    SetStreamPos(sal_uInt64 const nStrPos) { m_nStrPos = nStrPos; }
    void                    UpdateStreamPos( XclExpStream& rStrm );

    virtual sal_uInt16          GetNum() const override;
};

class ExcBundlesheet final : public ExcBundlesheetBase
{
private:
    OString            aName;

    virtual void            SaveCont( XclExpStream& rStrm ) override;

public:
                            ExcBundlesheet( const RootData& rRootData, SCTAB nTab );
    virtual std::size_t     GetLen() const override;
};

//--------------------------------------------------------- class ExcDummy_02 -
// sheet dummies: CALCMODE to SETUP

class ExcDummy_02a final : public ExcDummyRec
{
private:
    static const sal_uInt8      pMyData[];
    static const std::size_t   nMyLen;
public:
    virtual std::size_t        GetLen() const override;
    virtual const sal_uInt8*        GetData() const override;
};

/** This record contains the Windows country IDs for the UI and document language. */
class XclExpCountry final : public XclExpRecord
{
public:
    explicit                    XclExpCountry( const XclExpRoot& rRoot );

private:
    sal_uInt16                  mnUICountry;        /// The UI country ID.
    sal_uInt16                  mnDocCountry;       /// The document country ID.

    /** Writes the body of the COUNTRY record. */
    virtual void                WriteBody( XclExpStream& rStrm ) override;
};

// XclExpWsbool ===============================================================

class XclExpWsbool final : public XclExpUInt16Record
{
public:
    explicit XclExpWsbool( bool bFitToPages );
};

/**
 * Save sheetPr element and its children for xlsx export.
 */
class XclExpXmlSheetPr final : public XclExpRecordBase
{
public:
    explicit XclExpXmlSheetPr(
        bool bFitToPages, SCTAB nScTab, const Color& rTabColor, bool bSummaryBelow, XclExpFilterManager* pManager );

    virtual void SaveXml( XclExpXmlStream& rStrm ) override;

private:
    SCTAB mnScTab;
    XclExpFilterManager* mpManager;
    bool mbFitToPage;
    Color maTabColor;
    bool mbSummaryBelow;
};

class XclExpFiltermode final : public XclExpEmptyRecord
{
public:
    explicit            XclExpFiltermode();
};

class XclExpAutofilterinfo final : public XclExpUInt16Record
{
public:
    explicit            XclExpAutofilterinfo( const ScAddress& rStartPos, SCCOL nScCol );

    const ScAddress& GetStartPos() const { return maStartPos; }
    SCCOL        GetColCount() const { return static_cast< SCCOL >( GetValue() ); }

private:
    ScAddress           maStartPos;
};

class ExcFilterCondition
{
private:
    sal_uInt8               nType;
    sal_uInt8               nOper;
    std::unique_ptr<XclExpString>
                            pText;

protected:
public:
                            ExcFilterCondition();
                            ~ExcFilterCondition();

    bool             IsEmpty() const     { return (nType == EXC_AFTYPE_NOTUSED); }
    std::size_t             GetTextBytes() const;

    void                    SetCondition( sal_uInt8 nTp, sal_uInt8 nOp, const OUString* pT );

    void                    Save( XclExpStream& rStrm );
    void                    SaveXml( XclExpXmlStream& rStrm );
    void                    SaveText( XclExpStream& rStrm );
};

class XclExpAutofilter final : public XclExpRecord, protected XclExpRoot
{
private:
    enum FilterType
    {
        Empty,
        FilterCondition,
        MultiValue,
        BlankValue,
        ColorValue
    };
    FilterType              meType;
    sal_uInt16              nCol;
    bool                    bIsButtonHidden;
    sal_uInt16              nFlags;
    bool                    bHasBlankValue;
    ExcFilterCondition      aCond[ 2 ];
    std::vector<OUString> maMultiValues; // CT_Filter values
    std::vector<OUString> maDateValues; // CT_DateGroupItem values
    std::vector<std::pair<::Color, bool>> maColorValues; // first->Color, second->bIsBackgroundColor (vs. TextColor)

    bool                    AddCondition( ScQueryConnect eConn, sal_uInt8 nType,
                                sal_uInt8 nOp, const OUString* pText, bool bSimple = false );

    virtual void            WriteBody( XclExpStream& rStrm ) override;

public:
                            XclExpAutofilter( const XclExpRoot& rRoot, sal_uInt16 nC, bool bIsEmpty = false );

    sal_uInt16       GetCol() const          { return nCol; }
    bool             HasTop10() const        { return ::get_flag( nFlags, EXC_AFFLAG_TOP10 ); }

    bool                    HasCondition() const;
    bool                    AddEntry( const ScQueryEntry& rEntry );
    void                    AddMultiValueEntry( const ScQueryEntry& rEntry );
    void                    SetButtonHidden(bool bValue) { bIsButtonHidden = bValue; }
    void AddColorEntry( const ScQueryEntry& rEntry );

    virtual void            SaveXml( XclExpXmlStream& rStrm ) override;
};

class ExcAutoFilterRecs final : public XclExpRecordBase, protected XclExpRoot
{
public:
    /** @param  pDefinedData
                If nullptr, obtain anonymous ScDBData from sheet nTab.
                Else, use defined database range; used with XclExpTables.
     */
    explicit            ExcAutoFilterRecs( const XclExpRoot& rRoot, SCTAB nTab, const ScDBData* pDefinedData );
    virtual             ~ExcAutoFilterRecs() override;

    void                AddObjRecs();

    virtual void        Save( XclExpStream& rStrm ) override;
    virtual void        SaveXml( XclExpXmlStream& rStrm ) override;

    bool                HasFilterMode() const;

private:
    XclExpAutofilter*   GetByCol( SCCOL nCol ); // always 0-based
    bool                IsFiltered( SCCOL nCol );

private:
    typedef XclExpRecordList< XclExpAutofilter >    XclExpAutofilterList;
    typedef XclExpAutofilterList::RecordRefType     XclExpAutofilterRef;

    XclExpAutofilterList maFilterList;
    rtl::Reference<XclExpFiltermode> m_pFilterMode;
    rtl::Reference<XclExpAutofilterinfo> m_pFilterInfo;
    ScRange                 maRef;
    bool mbAutoFilter;

    ScRange maSortRef; // sort area without headers
    std::vector< std::tuple<ScRange, OUString, bool> > maSortCustomList; // sorted column with list of sorted items
};

/** Sheet filter manager. Contains auto filters or advanced filters from all sheets. */
class XclExpFilterManager final : protected XclExpRoot
{
public:
    explicit            XclExpFilterManager( const XclExpRoot& rRoot );

    /** Creates the filter records for the specified sheet.
        @descr  Creates and inserts related built-in NAME records. Therefore this
            function is called from the name buffer itself. */
    void                InitTabFilter( SCTAB nScTab );

    /** Returns a record object containing all filter records for the specified sheet. */
    XclExpRecordRef     CreateRecord( SCTAB nScTab );

    /** Returns whether or not FilterMode is present */
    bool                HasFilterMode( SCTAB nScTab );

private:
    using               XclExpRoot::CreateRecord;

    typedef rtl::Reference< ExcAutoFilterRecs >  XclExpTabFilterRef;
    typedef ::std::map< SCTAB, XclExpTabFilterRef > XclExpTabFilterMap;

    XclExpTabFilterMap  maFilterMap;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
