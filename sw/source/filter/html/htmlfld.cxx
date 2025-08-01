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

#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <o3tl/string_view.hxx>
#include <osl/diagnose.h>
#include <docsh.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <svtools/htmltokn.h>
#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>
#include <unotools/useroptions.hxx>
#include <fmtfld.hxx>
#include <ndtxt.hxx>
#include <doc.hxx>
#include <fldbas.hxx>
#include <docufld.hxx>
#include <flddat.hxx>
#include "htmlfld.hxx"
#include "swhtml.hxx"

using namespace ::com::sun::star;

namespace {

struct HTMLNumFormatTableEntry
{
    std::string_view pName;
    NfIndexTableOffset eFormat;
};

}

HTMLOptionEnum<SwFieldIds> const aHTMLFieldTypeTable[] =
{
    { OOO_STRING_SW_HTML_FT_author,   SwFieldIds::Author      },
    { OOO_STRING_SW_HTML_FT_sender,   SwFieldIds::ExtUser     },
    { "DATE",                         SwFieldIds::Date        },
    { "TIME",                         SwFieldIds::Time        },
    { OOO_STRING_SW_HTML_FT_datetime, SwFieldIds::DateTime    },
    { OOO_STRING_SW_HTML_FT_page,     SwFieldIds::PageNumber  },
    { OOO_STRING_SW_HTML_FT_docinfo,  SwFieldIds::DocInfo     },
    { OOO_STRING_SW_HTML_FT_docstat,  SwFieldIds::DocStat     },
    { OOO_STRING_SW_HTML_FT_filename, SwFieldIds::Filename    },
    { nullptr,                        SwFieldIds(0)           }
};

HTMLNumFormatTableEntry const aHTMLDateFieldFormatTable[] =
{
    { "SSYS",       NF_DATE_SYSTEM_SHORT    },
    { "LSYS",       NF_DATE_SYSTEM_LONG     },
    { "DMY",        NF_DATE_SYS_DDMMYY,     },
    { "DMYY",       NF_DATE_SYS_DDMMYYYY,   },
    { "DMMY",       NF_DATE_SYS_DMMMYY,     },
    { "DMMYY",      NF_DATE_SYS_DMMMYYYY,   },
    { "DMMMY",      NF_DATE_DIN_DMMMMYYYY   },
    { "DMMMYY",         NF_DATE_DIN_DMMMMYYYY   },
    { "DDMMY",      NF_DATE_SYS_NNDMMMYY    },
    { "DDMMMY",         NF_DATE_SYS_NNDMMMMYYYY },
    { "DDMMMYY",    NF_DATE_SYS_NNDMMMMYYYY },
    { "DDDMMMY",    NF_DATE_SYS_NNNNDMMMMYYYY },
    { "DDDMMMYY",   NF_DATE_SYS_NNNNDMMMMYYYY },
    { "MY",             NF_DATE_SYS_MMYY        },
    { "MD",             NF_DATE_DIN_MMDD        },
    { "YMD",        NF_DATE_DIN_YYMMDD      },
    { "YYMD",       NF_DATE_DIN_YYYYMMDD    },
    { {},                    NF_NUMERIC_START }
};

HTMLNumFormatTableEntry const aHTMLTimeFieldFormatTable[] =
{
    { "SYS",     NF_TIME_HHMMSS },
    { "SSMM24",      NF_TIME_HHMM },
    { "SSMM12",      NF_TIME_HHMMAMPM },
    { {},            NF_NUMERIC_START }
};

HTMLOptionEnum<SvxNumType> const aHTMLPageNumFieldFormatTable[] =
{
    { OOO_STRING_SW_HTML_FF_uletter,     SVX_NUM_CHARS_UPPER_LETTER },
    { OOO_STRING_SW_HTML_FF_lletter,     SVX_NUM_CHARS_LOWER_LETTER },
    { OOO_STRING_SW_HTML_FF_uroman,      SVX_NUM_ROMAN_UPPER },
    { OOO_STRING_SW_HTML_FF_lroman,      SVX_NUM_ROMAN_LOWER },
    { OOO_STRING_SW_HTML_FF_arabic,      SVX_NUM_ARABIC },
    { OOO_STRING_SW_HTML_FF_none,        SVX_NUM_NUMBER_NONE },
    { OOO_STRING_SW_HTML_FF_char,        SVX_NUM_CHAR_SPECIAL },
    { OOO_STRING_SW_HTML_FF_page,        SVX_NUM_PAGEDESC },
    { OOO_STRING_SW_HTML_FF_ulettern,    SVX_NUM_CHARS_UPPER_LETTER_N },
    { OOO_STRING_SW_HTML_FF_llettern,    SVX_NUM_CHARS_LOWER_LETTER_N },
    { nullptr,                           SvxNumType(0) }
};

HTMLOptionEnum<SwExtUserSubType> const aHTMLExtUsrFieldSubTable[] =
{
    { OOO_STRING_SW_HTML_FS_company,     SwExtUserSubType::Company },
    { OOO_STRING_SW_HTML_FS_firstname,   SwExtUserSubType::Firstname },
    { OOO_STRING_SW_HTML_FS_name,        SwExtUserSubType::Name },
    { OOO_STRING_SW_HTML_FS_shortcut,    SwExtUserSubType::Shortcut },
    { OOO_STRING_SW_HTML_FS_street,      SwExtUserSubType::Street },
    { OOO_STRING_SW_HTML_FS_country,     SwExtUserSubType::Country },
    { OOO_STRING_SW_HTML_FS_zip,         SwExtUserSubType::Zip },
    { OOO_STRING_SW_HTML_FS_city,        SwExtUserSubType::City },
    { OOO_STRING_SW_HTML_FS_title,       SwExtUserSubType::Title },
    { OOO_STRING_SW_HTML_FS_position,    SwExtUserSubType::Position },
    { OOO_STRING_SW_HTML_FS_pphone,      SwExtUserSubType::PhonePrivate },
    { OOO_STRING_SW_HTML_FS_cphone,      SwExtUserSubType::PhoneCompany },
    { OOO_STRING_SW_HTML_FS_fax,         SwExtUserSubType::Fax },
    { OOO_STRING_SW_HTML_FS_email,       SwExtUserSubType::Email },
    { OOO_STRING_SW_HTML_FS_state,       SwExtUserSubType::State },
    { nullptr,                           SwExtUserSubType(0) }
};

HTMLOptionEnum<SwAuthorFormat> const aHTMLAuthorFieldFormatTable[] =
{
    { OOO_STRING_SW_HTML_FF_name,        SwAuthorFormat::Name },
    { OOO_STRING_SW_HTML_FF_shortcut,    SwAuthorFormat::Shortcut },
    { nullptr,                           SwAuthorFormat(0) }
};

HTMLOptionEnum<SwPageNumSubType> const aHTMLPageNumFieldSubTable[] =
{
    { OOO_STRING_SW_HTML_FS_random,      SwPageNumSubType::Random },
    { OOO_STRING_SW_HTML_FS_next,        SwPageNumSubType::Next },
    { OOO_STRING_SW_HTML_FS_prev,        SwPageNumSubType::Previous },
    { nullptr,                           SwPageNumSubType(0)  }
};

HTMLOptionEnum<SwDocInfoSubType> const aHTMLDocInfoFieldSubTable[] =
{
    { OOO_STRING_SW_HTML_FS_title,   SwDocInfoSubType::Title },
    { OOO_STRING_SW_HTML_FS_theme,   SwDocInfoSubType::Subject },
    { OOO_STRING_SW_HTML_FS_keys,    SwDocInfoSubType::Keys },
    { OOO_STRING_SW_HTML_FS_comment, SwDocInfoSubType::Comment },
    { "INFO1",                       SwDocInfoSubType::Info1 },
    { "INFO2",                       SwDocInfoSubType::Info2 },
    { "INFO3",                       SwDocInfoSubType::Info3 },
    { "INFO4",                       SwDocInfoSubType::Info4 },
    { OOO_STRING_SW_HTML_FS_custom,  SwDocInfoSubType::Custom },
    { OOO_STRING_SW_HTML_FS_create,  SwDocInfoSubType::Create },
    { OOO_STRING_SW_HTML_FS_change,  SwDocInfoSubType::Change },
    { nullptr,                       SwDocInfoSubType(0) }
};

HTMLOptionEnum<SwDocInfoSubType> const aHTMLDocInfoFieldFormatTable[] =
{
    { OOO_STRING_SW_HTML_FF_author,  SwDocInfoSubType::SubAuthor },
    { OOO_STRING_SW_HTML_FF_time,    SwDocInfoSubType::SubTime },
    { OOO_STRING_SW_HTML_FF_date,    SwDocInfoSubType::SubDate },
    { nullptr,                       SwDocInfoSubType(0) }
};

HTMLOptionEnum<SwDocStatSubType> const aHTMLDocStatFieldSubTable[] =
{
    { OOO_STRING_SW_HTML_FS_page,    SwDocStatSubType::Page },
    { OOO_STRING_SW_HTML_FS_para,    SwDocStatSubType::Paragraph },
    { OOO_STRING_SW_HTML_FS_word,    SwDocStatSubType::Word },
    { OOO_STRING_SW_HTML_FS_char,    SwDocStatSubType::Character },
    { OOO_STRING_SW_HTML_FS_tbl,     SwDocStatSubType::Table },
    { OOO_STRING_SW_HTML_FS_grf,     SwDocStatSubType::Graphic },
    { OOO_STRING_SW_HTML_FS_ole,     SwDocStatSubType::OLE },
    { nullptr,                       SwDocStatSubType(0) }
};

HTMLOptionEnum<SwFileNameFormat> const aHTMLFileNameFieldFormatTable[] =
{
    { OOO_STRING_SW_HTML_FF_name,       SwFileNameFormat::Name },
    { OOO_STRING_SW_HTML_FF_pathname,   SwFileNameFormat::PathName },
    { OOO_STRING_SW_HTML_FF_path,       SwFileNameFormat::Path },
    { OOO_STRING_SW_HTML_FF_name_noext, SwFileNameFormat::NameNoExt },
    { nullptr,                          SwFileNameFormat(0) }
};

SvxNumType SwHTMLParser::GetNumType( std::u16string_view rStr, SvxNumType nDfltType )
{
    const HTMLOptionEnum<SvxNumType> *pOptEnums = aHTMLPageNumFieldFormatTable;
    while( pOptEnums->pName )
    {
        if( o3tl::equalsIgnoreAsciiCase( rStr, pOptEnums->pName ) )
            return pOptEnums->nValue;
        pOptEnums++;
    }
    return nDfltType;
}

void SwHTMLParser::NewField()
{
    bool bKnownType = false, bFixed = false,
         bHasNumFormat = false, bHasNumValue = false;
    SwFieldIds nType = SwFieldIds::Database;
    OUString aValue, aNumFormat, aNumValue, aName;
    const HTMLOption *pSubOption=nullptr, *pFormatOption=nullptr;

    const HTMLOptions& rHTMLOptions = GetOptions();
    size_t i;

    for ( i = rHTMLOptions.size(); i; )
    {
        const HTMLOption& rOption = rHTMLOptions[--i];
        switch( rOption.GetToken() )
        {
        case HtmlOptionId::TYPE:
            bKnownType = rOption.GetEnum( nType, aHTMLFieldTypeTable );
            break;
        case HtmlOptionId::SUBTYPE:
            pSubOption = &rOption;
            break;
        case HtmlOptionId::FORMAT:
            pFormatOption = &rOption;
            break;
        case HtmlOptionId::NAME:
            aName = rOption.GetString();
            break;
        case HtmlOptionId::VALUE:
            aValue = rOption.GetString();
            break;
        case HtmlOptionId::SDNUM:
            aNumFormat = rOption.GetString();
            bHasNumFormat = true;
            break;
        case HtmlOptionId::SDVAL:
            aNumValue = rOption.GetString();
            bHasNumValue = true;
            break;
        case HtmlOptionId::SDFIXED:
            bFixed = true;
            break;
        default: break;
        }
    }

    if( !bKnownType )
        return;

    // Author and sender are only inserted as a variable field if the document
    // was last changed by ourself or nobody changed it and it was created
    // by ourself. Otherwise it will be a fixed field.
    if( !bFixed &&
        (SwFieldIds::ExtUser == nType ||
         SwFieldIds::Author == nType) )
    {
        SvtUserOptions aOpt;
        const OUString aUser = aOpt.GetFullName();
        SwDocShell *pDocShell(m_xDoc->GetDocShell());
        OSL_ENSURE(pDocShell, "no SwDocShell");
        if (pDocShell) {
            uno::Reference<document::XDocumentPropertiesSupplier> xDPS(
                pDocShell->GetModel(), uno::UNO_QUERY_THROW);
            uno::Reference<document::XDocumentProperties> xDocProps(
                xDPS->getDocumentProperties());
            OSL_ENSURE(xDocProps.is(), "Doc has no DocumentProperties");
            const OUString aChanged = xDocProps->getModifiedBy();
            const OUString aCreated = xDocProps->getAuthor();
            if( aUser.isEmpty() ||
                (!aChanged.isEmpty() ? aUser != aChanged : aUser != aCreated) )
                bFixed = true;
        }
    }

    SwFieldIds nWhich = nType;
    if( SwFieldIds::Date==nType || SwFieldIds::Time==nType )
        nWhich = SwFieldIds::DateTime;

    SwFieldType* pType = m_xDoc->getIDocumentFieldsAccess().GetSysFieldType( nWhich );
    std::unique_ptr<SwField> xNewField;
    bool bInsOnEndTag = false;

    switch( nType )
    {
    case SwFieldIds::ExtUser:
        if( pSubOption )
        {
            SwExtUserSubType nSub;
            SwAuthorFormat nFormat = SwAuthorFormat::Name;
            if( bFixed )
            {
                nFormat |= SwAuthorFormat::Fixed;
                bInsOnEndTag = true;
            }
            if( pSubOption->GetEnum( nSub, aHTMLExtUsrFieldSubTable ) )
                xNewField.reset(new SwExtUserField(static_cast<SwExtUserFieldType*>(pType), nSub, nFormat));
        }
        break;

    case SwFieldIds::Author:
        {
            SwAuthorFormat nFormat = SwAuthorFormat::Name;
            if( pFormatOption )
                pFormatOption->GetEnum( nFormat, aHTMLAuthorFieldFormatTable );
            if( bFixed )
            {
                nFormat |= SwAuthorFormat::Fixed;
                bInsOnEndTag = true;
            }

            xNewField.reset(new SwAuthorField(static_cast<SwAuthorFieldType*>(pType), nFormat));
        }
        break;

    case SwFieldIds::Date:
    case SwFieldIds::Time:
        {
            sal_uInt32 nNumFormat = 0;
            DateTime aDateTime( DateTime::SYSTEM );
            sal_Int64 nTime = aDateTime.GetTime();
            sal_Int32 nDate = aDateTime.GetDate();
            SwDateTimeSubType nSub = SwDateTimeSubType::None;
            bool bValidFormat = false;
            HTMLNumFormatTableEntry const * pFormatTable;

            if( SwFieldIds::Date==nType )
            {
                nSub = SwDateTimeSubType::Date;
                pFormatTable = aHTMLDateFieldFormatTable;
                if( !aValue.isEmpty() )
                    nDate = aValue.toInt32();
            }
            else
            {
                nSub = SwDateTimeSubType::Time;
                pFormatTable = aHTMLTimeFieldFormatTable;
                if( !aValue.isEmpty() )
                    nTime = static_cast<sal_uLong>(aValue.toInt32()); // Is this OK? 32-bit encoded time?
            }
            if( !aValue.isEmpty() )
                nSub |= SwDateTimeSubType::Fixed;

            SvNumberFormatter *pFormatter = m_xDoc->GetNumberFormatter();
            if( pFormatOption )
            {
                const OUString& rFormat = pFormatOption->GetString();
                for( int k = 0; !pFormatTable[k].pName.empty(); ++k )
                {
                    if( rFormat.equalsIgnoreAsciiCaseAscii( pFormatTable[k].pName ) )
                    {
                        nNumFormat = pFormatter->GetFormatIndex(
                                        pFormatTable[k].eFormat, LANGUAGE_SYSTEM);
                        bValidFormat = true;
                        break;
                    }
                }
            }
            if( !bValidFormat )
                nNumFormat = pFormatter->GetFormatIndex( pFormatTable[i].eFormat,
                                                      LANGUAGE_SYSTEM);

            xNewField.reset(new SwDateTimeField(static_cast<SwDateTimeFieldType *>(pType), nSub, nNumFormat));

            if (nSub & SwDateTimeSubType::Fixed)
                static_cast<SwDateTimeField *>(xNewField.get())->SetDateTime(DateTime(Date(nDate), tools::Time::fromEncodedTime(nTime)));
        }
        break;

    case SwFieldIds::DateTime:
        if( bHasNumFormat )
        {
            SwDateTimeSubType nSub = SwDateTimeSubType::None;

            SvNumberFormatter *pFormatter = m_xDoc->GetNumberFormatter();
            sal_uInt32 nNumFormat;
            LanguageType eLang;
            double dValue = GetTableDataOptionsValNum(
                                nNumFormat, eLang, aNumValue, aNumFormat,
                                *m_xDoc->GetNumberFormatter() );
            SvNumFormatType nFormatType = pFormatter->GetType( nNumFormat );
            switch( nFormatType )
            {
            case SvNumFormatType::DATE: nSub = SwDateTimeSubType::Date; break;
            case SvNumFormatType::TIME: nSub = SwDateTimeSubType::Time; break;
            default: break;
            }

            if( nSub != SwDateTimeSubType::None )
            {
                if( bHasNumValue )
                    nSub |= SwDateTimeSubType::Fixed;

                xNewField.reset(new SwDateTimeField(static_cast<SwDateTimeFieldType *>(pType), nSub, nNumFormat));
                if (bHasNumValue)
                    static_cast<SwDateTimeField*>(xNewField.get())->SetValue(dValue);
            }
        }
        break;

    case SwFieldIds::PageNumber:
        if( pSubOption )
        {
            SwPageNumSubType nSub;
            if( pSubOption->GetEnum( nSub, aHTMLPageNumFieldSubTable ) )
            {
                SvxNumType nFormat = SVX_NUM_PAGEDESC;
                if( pFormatOption )
                    pFormatOption->GetEnum( nFormat, aHTMLPageNumFieldFormatTable );

                short nOff = 0;

                if( nFormat!=SVX_NUM_CHAR_SPECIAL && !aValue.isEmpty() )
                    nOff = static_cast<short>(aValue.toInt32());
                else if( nSub == SwPageNumSubType::Next  )
                    nOff = 1;
                else if( nSub == SwPageNumSubType::Previous  )
                    nOff = -1;

                if( nFormat==SVX_NUM_CHAR_SPECIAL &&
                    nSub == SwPageNumSubType::Random )
                    nFormat = SVX_NUM_PAGEDESC;

                xNewField.reset(new SwPageNumberField(static_cast<SwPageNumberFieldType*>(pType), nSub, nFormat, nOff));
                if (nFormat == SVX_NUM_CHAR_SPECIAL)
                    static_cast<SwPageNumberField*>(xNewField.get())->SetUserString(aValue);
            }
        }
        break;

    case SwFieldIds::DocInfo:
        if( pSubOption )
        {
            SwDocInfoSubType nSub;
            if( pSubOption->GetEnum( nSub, aHTMLDocInfoFieldSubTable ) )
            {
                SwDocInfoSubType nExtSub = SwDocInfoSubType::Title;
                if( SwDocInfoSubType::Create == nSub ||
                    SwDocInfoSubType::Change == nSub )
                {
                    nExtSub = SwDocInfoSubType::SubAuthor;
                    if( pFormatOption )
                        pFormatOption->GetEnum( nExtSub, aHTMLDocInfoFieldFormatTable );
                    nSub |= nExtSub;
                }

                sal_uInt32 nNumFormat = 0;
                double dValue = 0;
                if( bHasNumFormat && (SwDocInfoSubType::SubDate==nExtSub || SwDocInfoSubType::SubTime==nExtSub) )
                {
                    LanguageType eLang;
                    dValue = GetTableDataOptionsValNum(
                                    nNumFormat, eLang, aNumValue, aNumFormat,
                                    *m_xDoc->GetNumberFormatter() );
                    bFixed &= bHasNumValue;
                }
                else
                    bHasNumValue = false;

                if( nSub >= SwDocInfoSubType::Info1 && nSub <= SwDocInfoSubType::Info4 && aName.isEmpty() )
                {
                    // backward compatibility for OOo 2:
                    // map to names stored in AddMetaUserDefined
                    aName = m_InfoNames[static_cast<sal_uInt16>(nSub) - static_cast<sal_uInt16>(SwDocInfoSubType::Info1)];
                    nSub = SwDocInfoSubType::Custom;
                }

                if( bFixed )
                {
                    nSub |= SwDocInfoSubType::SubFixed;
                    bInsOnEndTag = true;
                }

                xNewField.reset(new SwDocInfoField(static_cast<SwDocInfoFieldType *>(pType), nSub, aName, nNumFormat));
                if (bHasNumValue)
                    static_cast<SwDocInfoField*>(xNewField.get())->SetValue(dValue);
            }
        }
        break;

    case SwFieldIds::DocStat:
        if( pSubOption )
        {
            SwDocStatSubType nSub;
            if( pSubOption->GetEnum( nSub, aHTMLDocStatFieldSubTable ) )
            {
                SvxNumType nFormat = SVX_NUM_ARABIC;
                if( pFormatOption )
                    pFormatOption->GetEnum( nFormat, aHTMLPageNumFieldFormatTable );
                xNewField.reset(new SwDocStatField(static_cast<SwDocStatFieldType*>(pType), nSub, nFormat));
                m_bUpdateDocStat |= (SwDocStatSubType::Page != nSub);
            }
        }
        break;

    case SwFieldIds::Filename:
        {
            SwFileNameFormat nFormat = SwFileNameFormat::Name;
            if( pFormatOption )
                pFormatOption->GetEnum( nFormat, aHTMLFileNameFieldFormatTable );
            if( bFixed )
            {
                nFormat |= SwFileNameFormat::Fixed;
                bInsOnEndTag = true;
            }

            xNewField.reset(new SwFileNameField(static_cast<SwFileNameFieldType*>(pType), nFormat));
        }
        break;
    default:
        ;
    }

    if (!xNewField)
        return;

    if (bInsOnEndTag)
    {
        m_xField = std::move(xNewField);
    }
    else
    {
        m_xDoc->getIDocumentContentOperations().InsertPoolItem(*m_pPam, SwFormatField(*xNewField));
        xNewField.reset();
    }
    m_bInField = true;
}

void SwHTMLParser::EndField()
{
    if (m_xField)
    {
        switch (m_xField->Which())
        {
        case SwFieldIds::DocInfo:
            OSL_ENSURE( static_cast<SwDocInfoField*>(m_xField.get())->IsFixed(),
                    "Field DocInfo should not have been saved" );
            static_cast<SwDocInfoField*>(m_xField.get())->SetExpansion( m_aContents );
            break;

        case SwFieldIds::ExtUser:
            OSL_ENSURE( static_cast<SwExtUserField*>(m_xField.get())->IsFixed(),
                    "Field ExtUser should not have been saved" );
            static_cast<SwExtUserField*>(m_xField.get())->SetExpansion( m_aContents );
            break;

        case SwFieldIds::Author:
            OSL_ENSURE( static_cast<SwAuthorField*>(m_xField.get())->IsFixed(),
                    "Field Author should not have been saved" );
            static_cast<SwAuthorField*>(m_xField.get())->SetExpansion( m_aContents );
            break;

        case SwFieldIds::Filename:
            OSL_ENSURE( static_cast<SwFileNameField*>(m_xField.get())->IsFixed(),
                    "Field FileName should not have been saved" );
            static_cast<SwFileNameField*>(m_xField.get())->SetExpansion( m_aContents );
            break;
        default: break;
        }

        m_xDoc->getIDocumentContentOperations().InsertPoolItem( *m_pPam, SwFormatField(*m_xField) );
        m_xField.reset();
    }

    m_bInField = false;
    m_aContents.clear();
}

void SwHTMLParser::InsertFieldText()
{
    if (m_xField)
    {
        // append the current text part to the text
        m_aContents += aToken;
    }
}

void SwHTMLParser::InsertCommentText( std::string_view pTag )
{
    bool bEmpty = m_aContents.isEmpty();
    if( !bEmpty )
        m_aContents += "\n";

    m_aContents += aToken;
    if( bEmpty && !pTag.empty() )
    {
        m_aContents = OUString::Concat("HTML: <") + OUString::createFromAscii(pTag) + ">" + m_aContents;
    }
}

void SwHTMLParser::InsertComment( const OUString& rComment, std::string_view pTag )
{
    OUString aComment( rComment );
    if( !pTag.empty() )
    {
        aComment += "</" +
            OUString::createFromAscii(pTag) +
            ">";
    }

    // MIB 24.06.97: If a PostIt should be insert after a space, we
    // will insert before the space. Then there are less problems
    // during formatting. (bug #40483#)
    const sal_Int32 nPos = m_pPam->GetPoint()->GetContentIndex();
    SwTextNode *pTextNd = m_pPam->GetPointNode().GetTextNode();
    bool bMoveFwd = false;
    if (nPos>0 && pTextNd && (' ' == pTextNd->GetText()[nPos-1]))
    {
        bMoveFwd = true;

        SwNodeOffset nNodeIdx = m_pPam->GetPoint()->GetNodeIndex();
        const sal_Int32 nIdx = m_pPam->GetPoint()->GetContentIndex();
        for( auto i = m_aSetAttrTab.size(); i > 0; )
        {
            HTMLAttr *pAttr = m_aSetAttrTab[--i];
            if( pAttr->GetStartParagraphIdx() != nNodeIdx ||
                pAttr->GetStartContent() != nIdx )
                break;

            if( RES_TXTATR_FIELD == pAttr->m_pItem->Which() &&
                SwFieldIds::Script == static_cast<const SwFormatField *>(pAttr->m_pItem.get())->GetField()->GetTyp()->Which() )
            {
                bMoveFwd = false;
                break;
            }
        }

        if( bMoveFwd )
            m_pPam->Move( fnMoveBackward );
    }

    SwPostItField aPostItField(
                    static_cast<SwPostItFieldType*>(m_xDoc->getIDocumentFieldsAccess().GetSysFieldType( SwFieldIds::Postit )),
                    OUString(), aComment, OUString(), SwMarkName(), DateTime(DateTime::SYSTEM));
    InsertAttr( SwFormatField( aPostItField ), false );

    if( bMoveFwd )
        m_pPam->Move( fnMoveForward );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
