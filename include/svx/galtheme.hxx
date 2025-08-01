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

#ifndef INCLUDED_SVX_GALTHEME_HXX
#define INCLUDED_SVX_GALTHEME_HXX

#include <svx/svxdllapi.h>
#include <svx/galleryobjectcollection.hxx>

#include <tools/debug.hxx>
#include <tools/urlobj.hxx>
#include <tools/datetime.hxx>
#include <vcl/bitmapex.hxx>
#include <svl/SfxBroadcaster.hxx>
#include <svx/galmisc.hxx>
#include <memory>

class GalleryFileStorage;
class GalleryThemeEntry;
class SgaObject;
class FmFormModel;
class Gallery;
namespace unogallery
{
    class GalleryItem;
    class GalleryTheme;
}
namespace weld
{
    class ComboBox;
    class Widget;
}

class SVXCORE_DLLPUBLIC GalleryTheme final : public SfxBroadcaster
{
    friend class Gallery;
    friend class GalleryThemeCacheEntry;
    friend class ::unogallery::GalleryTheme;
    friend class ::unogallery::GalleryItem;

private:

    std::unique_ptr<GalleryFileStorage>     mpGalleryStorageEngine;
    GalleryObjectCollection     maGalleryObjectCollection;
    Gallery*                    mpParent;
    GalleryThemeEntry*          mpThm;
    sal_uInt32                  mnThemeLockCount;
    sal_uInt32                  mnBroadcasterLockCount;
    sal_uInt32                  mnDragPos;
    bool                        mbDragging;
    bool                        mbAbortActualize;

    const std::unique_ptr<GalleryFileStorage>& getGalleryStorageEngine() const { return mpGalleryStorageEngine; }

    SAL_DLLPRIVATE void         ImplSetModified( bool bModified );
    SAL_DLLPRIVATE void         ImplBroadcast(sal_uInt32 nUpdatePos);

    GalleryTheme(GalleryTheme const &) = delete;
    void operator =(GalleryTheme const &) = delete;

public:
    SAL_DLLPRIVATE              GalleryTheme(Gallery* pGallery, GalleryThemeEntry* pThemeEntry);

    SAL_DLLPRIVATE              virtual ~GalleryTheme() override;

    SAL_DLLPRIVATE sal_uInt32   GetObjectCount() const { return maGalleryObjectCollection.size(); }

    std::unique_ptr<SgaObject>  AcquireObject(sal_uInt32 nPos);

    bool                        InsertObject(const SgaObject& rObj, sal_uInt32 nPos = SAL_MAX_UINT32);
    void                        RemoveObject(sal_uInt32 nPos);
    bool                        ChangeObjectPos(sal_uInt32 nOldPos, sal_uInt32 nNewPos);

    const OUString&             GetName() const;

    // used for building gallery themes during compilation:
    void                        SetDestDir(const OUString& rDestDir, bool bRelative);

    sal_uInt32                  GetId() const;
    SAL_DLLPRIVATE void         SetId( sal_uInt32 nNewId, bool bResetThemeName );

    SAL_DLLPRIVATE void         SetDragging( bool bSet ) { mbDragging = bSet; }
    SAL_DLLPRIVATE bool         IsDragging() const { return mbDragging; }

    SAL_DLLPRIVATE void         LockTheme() { ++mnThemeLockCount; }
    SAL_DLLPRIVATE bool         UnlockTheme();

    SAL_DLLPRIVATE void         LockBroadcaster() { mnBroadcasterLockCount++; }
    void                        UnlockBroadcaster();
    SAL_DLLPRIVATE bool         IsBroadcasterLocked() const { return mnBroadcasterLockCount > 0; }

    SAL_DLLPRIVATE void         SetDragPos(sal_uInt32 nPos) { mnDragPos = nPos; }
    SAL_DLLPRIVATE sal_uInt32   GetDragPos() const { return mnDragPos; }

    bool                        IsReadOnly() const;
    bool                        IsDefault() const;

    void                        Actualize( const Link<const INetURLObject&, void>& rActualizeLink, GalleryProgress* pProgress = nullptr );
    SAL_DLLPRIVATE void         AbortActualize() { mbAbortActualize = true; }

    SAL_DLLPRIVATE Gallery*     GetParent() const { return mpParent; }

public:

    SAL_DLLPRIVATE SgaObjKind GetObjectKind(sal_uInt32 nPos) const
    {
        if (nPos < GetObjectCount())
            return maGalleryObjectCollection.getForPosition( nPos )->eObjKind;
        return SgaObjKind::NONE;
    }

    SAL_DLLPRIVATE const INetURLObject& GetObjectURL(sal_uInt32 nPos) const
    {
        DBG_ASSERT(nPos < GetObjectCount(), "Position out of range");
        return maGalleryObjectCollection.getURLForPosition(nPos);
    }

    SAL_DLLPRIVATE bool         GetThumb(sal_uInt32 nPos, Bitmap& rBmp);

    bool                        GetGraphic(sal_uInt32 nPos, Graphic& rGraphic);
    bool                        InsertGraphic(const Graphic& rGraphic, sal_uInt32 nInsertPos);

    bool                        GetModel(sal_uInt32 nPos, SdrModel& rModel);
    bool                        InsertModel(const FmFormModel& rModel, sal_uInt32 nInsertPos);

    SAL_DLLPRIVATE bool         GetModelStream(sal_uInt32 nPos, SvStream & rModelStream);
    SAL_DLLPRIVATE bool         InsertModelStream(SvStream& rModelStream, sal_uInt32 nInsertPos);

    SAL_DLLPRIVATE bool         GetURL(sal_uInt32 nPos, INetURLObject& rURL) const;
    bool                        InsertURL(const INetURLObject& rURL, sal_uInt32 nInsertPos = SAL_MAX_UINT32);
    SAL_DLLPRIVATE bool         InsertFileOrDirURL(const INetURLObject& rFileOrDirURL, sal_uInt32 nInsertPos);

    SAL_DLLPRIVATE bool         InsertTransferable(const css::uno::Reference< css::datatransfer::XTransferable >& rxTransferable, sal_uInt32 nInsertPos);

    SAL_DLLPRIVATE void         CopyToClipboard(const weld::Widget& rWidget, sal_uInt32 nPos);

    DateTime getModificationDate() const;

    const INetURLObject& getThemeURL() const;

public:

    SAL_DLLPRIVATE SvStream&    ReadData( SvStream& rIn );
    static void                 InsertAllThemes(weld::ComboBox& rListBox);

    // for buffering PreviewBitmaps and strings for object and path
    SAL_DLLPRIVATE void GetPreviewBitmapAndStrings(sal_uInt32 nPos, Bitmap& rBitmap, Size& rSize, OUString& rTitle, OUString& rPath);
    SAL_DLLPRIVATE void SetPreviewBitmapAndStrings(sal_uInt32 nPos, const Bitmap& rBitmap, const Size& rSize, const OUString& rTitle, const OUString& rPath);
};

#endif // INCLUDED_SVX_GALTHEME_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
