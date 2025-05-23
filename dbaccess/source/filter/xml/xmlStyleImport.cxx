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

#include "xmlStyleImport.hxx"

#include <xmloff/maptype.hxx>
#include <xmloff/xmlimppr.hxx>
#include <xmloff/families.hxx>
#include <xmloff/xmlnumfi.hxx>
#include <xmloff/xmltoken.hxx>
#include <osl/diagnose.h>
#include "xmlfilter.hxx"
#include "xmlHelper.hxx"

namespace dbaxml
{

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::xml::sax;
using namespace ::com::sun::star::style;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::container;
using namespace xmloff::token;


OTableStyleContext::OTableStyleContext( ODBFilter& rImport,
        OTableStylesContext& rStyles, XmlStyleFamily nFamily )
    :XMLPropStyleContext( rImport, rStyles, nFamily, false )
    ,pStyles(&rStyles)
    ,m_nNumberFormat(-1)
{

}

OTableStyleContext::~OTableStyleContext()
{

}

void OTableStyleContext::FillPropertySet(
            const Reference< XPropertySet > & rPropSet )
{
    if ( !IsDefaultStyle() )
    {
        if ( GetFamily() == XmlStyleFamily::TABLE_TABLE )
        {
            if ( !sPageStyle.isEmpty() )
            {
                AddProperty(CTF_DB_MASTERPAGENAME, Any(sPageStyle));
            }
        }
        else if ( GetFamily() == XmlStyleFamily::TABLE_COLUMN )
        {
            if ((m_nNumberFormat == -1) && !m_sDataStyleName.isEmpty())
            {
                SvXMLNumFormatContext* pStyle = const_cast<SvXMLNumFormatContext*>(dynamic_cast< const SvXMLNumFormatContext* >(pStyles->FindStyleChildContext(
                    XmlStyleFamily::DATA_STYLE, m_sDataStyleName, true)));
                if ( !pStyle )
                {
                    OTableStylesContext* pMyStyles = dynamic_cast<OTableStylesContext* >(GetOwnImport().GetAutoStyles());
                    if ( pMyStyles )
                        pStyle = const_cast<SvXMLNumFormatContext*>(dynamic_cast< const SvXMLNumFormatContext* >(pMyStyles->
                            FindStyleChildContext(XmlStyleFamily::DATA_STYLE, m_sDataStyleName, true)));
                    else {
                        OSL_FAIL("not possible to get style");
                    }
                }
                if ( pStyle )
                {
                    uno::Any aNumberFormat;
                    m_nNumberFormat = pStyle->GetKey();
                    aNumberFormat <<= m_nNumberFormat;
                    AddProperty(CTF_DB_NUMBERFORMAT, aNumberFormat);
                }
            }
        }
    }
    XMLPropStyleContext::FillPropertySet(rPropSet);
}

void OTableStyleContext::SetDefaults()
{
}

void OTableStyleContext::AddProperty(const sal_Int16 nContextID, const uno::Any& rValue)
{
    sal_Int32 nIndex(pStyles->GetIndex(nContextID));
    OSL_ENSURE(nIndex != -1, "Property not found in Map");
    XMLPropertyState aPropState(nIndex, rValue);
    GetProperties().push_back(aPropState); // has to be inserted in a sort order later
}

void OTableStyleContext::SetAttribute( sal_Int32 nElement,
                                        const OUString& rValue )
{
    switch(nElement & TOKEN_MASK)
    {
        case XML_DATA_STYLE_NAME:
            m_sDataStyleName = rValue;
            break;
        case XML_MASTER_PAGE_NAME:
            sPageStyle = rValue;
            break;
        default:
            XMLPropStyleContext::SetAttribute( nElement, rValue );
    }
}

ODBFilter& OTableStyleContext::GetOwnImport()
{
    return static_cast<ODBFilter&>(GetImport());
}


OTableStylesContext::OTableStylesContext( SvXMLImport& rImport,
                                          bool bTempAutoStyles )
    : SvXMLStylesContext( rImport )
    , m_nNumberFormatIndex(-1)
    , m_nMasterPageNameIndex(-1)
    , bAutoStyles(bTempAutoStyles)
{

}

OTableStylesContext::~OTableStylesContext()
{

}

void OTableStylesContext::endFastElement(sal_Int32 )
{
    if (bAutoStyles)
        GetImport().GetTextImport()->SetAutoStyles( this );
    else
        GetImport().GetStyles()->CopyStylesToDoc(true);
}

SvXMLImportPropertyMapper*
    OTableStylesContext::GetImportPropertyMapper(
                    XmlStyleFamily nFamily ) const
{
    SvXMLImportPropertyMapper* pMapper = SvXMLStylesContext::GetImportPropertyMapper(nFamily);

    if (!pMapper)
    {
        switch( nFamily )
        {
            case XmlStyleFamily::TABLE_TABLE:
            {
                if ( !m_xTableImpPropMapper )
                    m_xTableImpPropMapper = std::make_unique<SvXMLImportPropertyMapper>( const_cast<OTableStylesContext*>(this)->GetOwnImport().GetTableStylesPropertySetMapper(), const_cast<SvXMLImport&>(GetImport()) );
                pMapper = m_xTableImpPropMapper.get();
            }
             break;
            case XmlStyleFamily::TABLE_COLUMN:
            {
                if ( !m_xColumnImpPropMapper )
                    m_xColumnImpPropMapper = std::make_unique<SvXMLImportPropertyMapper>( const_cast<OTableStylesContext*>(this)->GetOwnImport().GetColumnStylesPropertySetMapper(), const_cast<SvXMLImport&>(GetImport()) );
                pMapper = m_xColumnImpPropMapper.get();
            }
             break;
            case XmlStyleFamily::TABLE_CELL:
            {
                if ( !m_xCellImpPropMapper )
                    m_xCellImpPropMapper = std::make_unique<SvXMLImportPropertyMapper>( const_cast<OTableStylesContext*>(this)->GetOwnImport().GetCellStylesPropertySetMapper(), const_cast<SvXMLImport&>(GetImport()) );
                pMapper = m_xCellImpPropMapper.get();
            }
             break;
            default: break;
        }
    }

    return pMapper;
}

SvXMLStyleContext *OTableStylesContext::CreateStyleStyleChildContext(
        XmlStyleFamily nFamily, sal_Int32 nElement,
        const Reference< xml::sax::XFastAttributeList > & xAttrList )
{
    SvXMLStyleContext *pStyle = SvXMLStylesContext::CreateStyleStyleChildContext( nFamily, nElement,
                                                            xAttrList );
    if (pStyle)
        return pStyle;

    switch( nFamily )
    {
    case XmlStyleFamily::TABLE_TABLE:
    case XmlStyleFamily::TABLE_COLUMN:
    case XmlStyleFamily::TABLE_CELL:
        return new OTableStyleContext( GetOwnImport(), *this, nFamily );
    default: break;
    }

    return nullptr;
}

OUString OTableStylesContext::GetServiceName( XmlStyleFamily nFamily ) const
{
    OUString sServiceName = SvXMLStylesContext::GetServiceName(nFamily);
    if (sServiceName.isEmpty())
    {
        switch( nFamily )
        {
        case XmlStyleFamily::TABLE_TABLE:
            sServiceName = XML_STYLE_FAMILY_TABLE_TABLE_STYLES_NAME;
            break;
        case XmlStyleFamily::TABLE_COLUMN:
            sServiceName = XML_STYLE_FAMILY_TABLE_COLUMN_STYLES_NAME;
            break;
        case XmlStyleFamily::TABLE_CELL:
            sServiceName = XML_STYLE_FAMILY_TABLE_CELL_STYLES_NAME;
            break;
        default: break;
        }
    }
    return sServiceName;
}

sal_Int32 OTableStylesContext::GetIndex(const sal_Int16 nContextID)
{
    if ( nContextID == CTF_DB_NUMBERFORMAT )
    {
        if (m_nNumberFormatIndex == -1)
            m_nNumberFormatIndex =
                GetImportPropertyMapper(XmlStyleFamily::TABLE_COLUMN)->getPropertySetMapper()->FindEntryIndex(nContextID);
        return m_nNumberFormatIndex;
    }
    else if ( nContextID == CTF_DB_MASTERPAGENAME )
    {
        if (m_nMasterPageNameIndex == -1)
            m_nMasterPageNameIndex =
                GetImportPropertyMapper(XmlStyleFamily::TABLE_TABLE)->getPropertySetMapper()->FindEntryIndex(nContextID);
        return m_nMasterPageNameIndex;
    }
    else
        return -1;
}

ODBFilter& OTableStylesContext::GetOwnImport()
{
    return static_cast<ODBFilter&>(GetImport());
}

} // dbaxml

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
