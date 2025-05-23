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

#include <filter/msfilter/svdfppt.hxx>
#include <diadef.h>
#include <sot/storage.hxx>
#include <svx/svdtypes.hxx>
#include <memory>

class SdDrawDocument;
class SfxMedium;

/*************************************************************************
|*
|* local import
|*
\************************************************************************/

class SdPage;
class SdAnimationInfo;
class Ppt97Animation;

typedef std::shared_ptr< Ppt97Animation > Ppt97AnimationPtr;
typedef ::std::map < SdrObject*, Ppt97AnimationPtr > tAnimationMap;

class ImplSdPPTImport : public SdrPowerPointImport
{
    rtl::Reference<SotStorageStream> mxPicturesStream;
    SfxMedium&      mrMed;
    SotStorage&      mrStorage;
    DffRecordHeader maDocHd;
    std::vector<OUString> maSlideNameList;
    bool            mbDocumentFound;
    sal_uInt32      mnFilterOptions;
    SdDrawDocument& mrDoc;
    PresChange      mePresChange;
    SdrLayerID      mnBackgroundObjectsLayerID;

    tAnimationMap   maAnimations;
    void            SetHeaderFooterPageSettings( SdPage* pPage, const PptSlidePersistEntry* pMasterPersist );
    void            ImportPageEffect( SdPage* pPage, const bool bNewAnimationsUsed );

    void            FillSdAnimationInfo(SdAnimationInfo* pInfo, const PptInteractiveInfoAtom& rIAtom, const OUString& rMacroName);

    virtual         rtl::Reference<SdrObject> ProcessObj( SvStream& rSt, DffObjData& rData, SvxMSDffClientData& rClientData, ::tools::Rectangle& rTextRect, SdrObject* pObj ) override;
    virtual         SdrObject* ApplyTextObj( PPTTextObj* pTextObj, SdrTextObj* pText, SdPageCapsule pPage,
                                            SfxStyleSheet*, SfxStyleSheet** ) const override;

public:

    OUString        ReadSound( sal_uInt32 nSoundRef ) const;
    OUString        ReadMedia( sal_uInt32 nMediaRef ) const;

    ImplSdPPTImport( SdDrawDocument& rDoc, SotStorage& rStorage, SfxMedium& rMed, PowerPointImportParam& );
    virtual ~ImplSdPPTImport() override;

    bool         Import();
    virtual bool ReadFormControl(  rtl::Reference<SotStorage>& rSrc1, css::uno::Reference< css::form::XFormComponent > & rFormComp ) const override;
};

class SdPPTImport
{
    PowerPointImportParam maParam;
    std::unique_ptr<ImplSdPPTImport> pFilter;

public:

    SdPPTImport( SdDrawDocument& rDoc, SvStream& rDocStream, SotStorage& rStorage, SfxMedium& rMed );
    ~SdPPTImport();

    bool Import();
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
