/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#pragma once

#include <memory>

#include <com/sun/star/util/DateTime.hpp>

#include <vcl/dllapi.h>
#include <basegfx/vector/b2dsize.hxx>
#include <basegfx/range/b2drectangle.hxx>
#include <basegfx/point/b2dpoint.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <rtl/ustring.hxx>
#include <tools/color.hxx>
#include <tools/gen.hxx>
#include <vcl/checksum.hxx>
#include <vcl/Scanline.hxx>
#include <vcl/pdf/PDFAnnotationSubType.hxx>
#include <vcl/pdf/PDFPageObjectType.hxx>
#include <vcl/pdf/PDFSegmentType.hxx>
#include <vcl/pdf/PDFBitmapType.hxx>
#include <vcl/pdf/PDFObjectType.hxx>
#include <vcl/pdf/PDFTextRenderMode.hxx>
#include <vcl/pdf/PDFFillMode.hxx>
#include <vcl/pdf/PDFFindFlags.hxx>
#include <vcl/pdf/PDFErrorType.hxx>
#include <vcl/pdf/PDFFormFieldType.hxx>
#include <vcl/pdf/PDFAnnotAActionType.hxx>

class SvMemoryStream;
class Bitmap;

namespace vcl::pdf
{
inline constexpr OString constDictionaryKeyTitle = "T"_ostr;
inline constexpr OString constDictionaryKeyContents = "Contents"_ostr;
inline constexpr OString constDictionaryKeyPopup = "Popup"_ostr;
inline constexpr OString constDictionaryKeyModificationDate = "M"_ostr;
inline constexpr OString constDictionaryKeyInteriorColor = "IC"_ostr;
inline constexpr OString constDictionaryKey_DefaultStyle = "DS"_ostr;
inline constexpr OString constDictionaryKey_RichContent = "RC"_ostr;

class PDFiumBitmap;
class PDFiumDocument;
class PDFiumPageObject;

class VCL_DLLPUBLIC PDFium
{
public:
    virtual ~PDFium() = default;

    virtual const OUString& getLastError() const = 0;

    virtual std::unique_ptr<PDFiumDocument> openDocument(const void* pData, int nSize,
                                                         const OString& rPassword)
        = 0;
    virtual PDFErrorType getLastErrorCode() = 0;
    /// createBitmap can reduce requested size to possible value
    virtual std::unique_ptr<PDFiumBitmap> createBitmap(int& nWidth, int& nHeight, int nAlpha) = 0;
};

class PDFiumPage;

class VCL_DLLPUBLIC PDFiumBitmap
{
public:
    virtual ~PDFiumBitmap() = default;
    virtual void fillRect(int left, int top, int width, int height, sal_uInt32 nColor) = 0;
    virtual void renderPageBitmap(PDFiumDocument* pDoc, PDFiumPage* pPage, int nStartX, int nStartY,
                                  int nSizeX, int nSizeY)
        = 0;
    virtual ConstScanline getBuffer() = 0;
    virtual int getStride() = 0;
    virtual int getWidth() = 0;
    virtual int getHeight() = 0;
    virtual PDFBitmapType getFormat() = 0;
    /// Convert the bitmap buffer to a Bitmap
    virtual Bitmap createBitmapFromBuffer() = 0;
};

class VCL_DLLPUBLIC PDFiumAnnotation
{
public:
    virtual ~PDFiumAnnotation() = default;
    virtual PDFAnnotationSubType getSubType() = 0;
    virtual basegfx::B2DRectangle getRectangle() = 0;
    virtual bool hasKey(OString const& rKey) = 0;
    virtual PDFObjectType getValueType(OString const& rKey) = 0;
    virtual OUString getString(OString const& rKey) = 0;
    virtual std::unique_ptr<PDFiumAnnotation> getLinked(OString const& rKey) = 0;
    virtual int getObjectCount() = 0;
    virtual std::unique_ptr<PDFiumPageObject> getObject(int nIndex) = 0;
    virtual std::vector<std::vector<basegfx::B2DPoint>> getInkStrokes() = 0;
    virtual std::vector<basegfx::B2DPoint> getVertices() = 0;
    virtual Color getColor() = 0;
    virtual Color getInteriorColor() = 0;
    virtual float getBorderWidth() = 0;
    virtual basegfx::B2DSize getBorderCornerRadius() = 0;
    virtual size_t getAttachmentPointsCount() = 0;
    virtual std::vector<basegfx::B2DPoint> getAttachmentPoints(size_t nIndex) = 0;
    virtual std::vector<basegfx::B2DPoint> getLineGeometry() = 0;
    virtual PDFFormFieldType getFormFieldType(PDFiumDocument* pDoc) = 0;
    virtual float getFontSize(PDFiumDocument* pDoc) = 0;
    virtual Color getFontColor(PDFiumDocument* pDoc) = 0;
    virtual OUString getFormFieldAlternateName(PDFiumDocument* pDoc) = 0;
    virtual int getFormFieldFlags(PDFiumDocument* pDoc) = 0;
    virtual OUString getFormAdditionalActionJavaScript(PDFiumDocument* pDoc,
                                                       PDFAnnotAActionType eEvent)
        = 0;
    virtual OUString getFormFieldValue(PDFiumDocument* pDoc) = 0;
    virtual int getOptionCount(PDFiumDocument* pDoc) = 0;
};

class PDFiumTextPage;

class VCL_DLLPUBLIC PDFiumPathSegment
{
public:
    virtual ~PDFiumPathSegment() = default;
    virtual basegfx::B2DPoint getPoint() const = 0;
    virtual bool isClosed() const = 0;
    virtual PDFSegmentType getType() const = 0;
};

class VCL_DLLPUBLIC PDFiumPageObject
{
public:
    virtual ~PDFiumPageObject() = default;

    virtual PDFPageObjectType getType() = 0;
    virtual OUString getText(std::unique_ptr<PDFiumTextPage> const& pTextPage) = 0;

    virtual int getFormObjectCount() = 0;
    virtual std::unique_ptr<PDFiumPageObject> getFormObject(int nIndex) = 0;

    virtual basegfx::B2DHomMatrix getMatrix() = 0;
    virtual basegfx::B2DRectangle getBounds() = 0;
    virtual double getFontSize() = 0;
    virtual OUString getFontName() = 0;
    virtual PDFTextRenderMode getTextRenderMode() = 0;
    virtual Color getFillColor() = 0;
    virtual Color getStrokeColor() = 0;
    virtual double getStrokeWidth() = 0;
    // Path
    virtual int getPathSegmentCount() = 0;
    virtual std::unique_ptr<PDFiumPathSegment> getPathSegment(int index) = 0;
    virtual Size getImageSize(PDFiumPage& rPage) = 0;
    virtual std::unique_ptr<PDFiumBitmap> getImageBitmap() = 0;
    virtual bool getDrawMode(PDFFillMode& eFillMode, bool& bStroke) = 0;
};

class VCL_DLLPUBLIC PDFiumSearchHandle
{
public:
    virtual ~PDFiumSearchHandle() = default;

    virtual bool findNext() = 0;
    virtual bool findPrev() = 0;
    virtual int getSearchResultIndex() = 0;
    virtual int getSearchCount() = 0;
};

class VCL_DLLPUBLIC PDFiumTextPage
{
public:
    virtual ~PDFiumTextPage() = default;

    virtual int countChars() = 0;
    virtual unsigned int getUnicode(int index) = 0;
    virtual std::unique_ptr<PDFiumSearchHandle>
    findStart(const OUString& rFindWhat, PDFFindFlags nFlags, sal_Int32 nStartIndex) = 0;

    /// Returned rect is no longer upside down and is in mm100.
    virtual basegfx::B2DRectangle getCharBox(int nIndex, double fPageHeight) = 0;
};

class VCL_DLLPUBLIC PDFiumStructureElement
{
public:
    virtual ~PDFiumStructureElement() = default;

    virtual OUString getAltText() = 0;
    virtual OUString getActualText() = 0;
    virtual OUString getID() = 0;
    virtual OUString getLang() = 0;
    virtual OUString getTitle() = 0;
    virtual OUString getType() = 0;
    virtual OUString getObjectType() = 0;

    virtual int getNumberOfChildren() = 0;
    virtual int getChildMarkedContentID(int nIndex) = 0;
    virtual std::unique_ptr<PDFiumStructureElement> getChild(int nIndex) = 0;
    virtual std::unique_ptr<PDFiumStructureElement> getParent() = 0;
};

class VCL_DLLPUBLIC PDFiumStructureTree
{
public:
    virtual ~PDFiumStructureTree() = default;

    virtual int getNumberOfChildren() = 0;
    virtual std::unique_ptr<PDFiumStructureElement> getChild(int nIndex) = 0;
};

class VCL_DLLPUBLIC PDFiumPage
{
public:
    virtual ~PDFiumPage() = default;

    virtual int getObjectCount() = 0;
    virtual std::unique_ptr<PDFiumPageObject> getObject(int nIndex) = 0;

    virtual int getAnnotationCount() = 0;
    virtual int getAnnotationIndex(std::unique_ptr<PDFiumAnnotation> const& rAnnotation) = 0;

    virtual std::unique_ptr<PDFiumAnnotation> getAnnotation(int nIndex) = 0;

    virtual std::unique_ptr<PDFiumTextPage> getTextPage() = 0;
    virtual std::unique_ptr<PDFiumStructureTree> getStructureTree() = 0;

    /// Get bitmap checksum of the page, without annotations/commenting.
    virtual BitmapChecksum getChecksum(int nMDPPerm) = 0;

    virtual double getWidth() = 0;
    virtual double getHeight() = 0;

    virtual bool hasTransparency() = 0;

    virtual bool hasLinks() = 0;

    virtual void onAfterLoadPage(PDFiumDocument* pDoc) = 0;
};

/// Represents one digital signature, as exposed by PDFium.
class VCL_DLLPUBLIC PDFiumSignature
{
public:
    virtual ~PDFiumSignature() = default;

    virtual std::vector<int> getByteRange() = 0;
    virtual int getDocMDPPermission() = 0;
    virtual std::vector<unsigned char> getContents() = 0;
    virtual OString getSubFilter() = 0;
    virtual OUString getReason() = 0;
    virtual css::util::DateTime getTime() = 0;
};

class VCL_DLLPUBLIC PDFiumAttachment
{
public:
    virtual ~PDFiumAttachment() = default;

    virtual OUString getName() = 0;
    virtual bool getFile(std::vector<unsigned char>& rOutBuffer) = 0;
};

class VCL_DLLPUBLIC PDFiumDocument
{
public:
    virtual ~PDFiumDocument() = default;

    // Page size in points
    virtual basegfx::B2DSize getPageSize(int nIndex) = 0;
    virtual int getPageCount() = 0;
    virtual int getSignatureCount() = 0;
    virtual int getFileVersion() = 0;
    virtual int getAttachmentCount() = 0;
    virtual bool saveWithVersion(SvMemoryStream& rStream, int nFileVersion, bool bRemoveSecurity)
        = 0;

    virtual std::unique_ptr<PDFiumPage> openPage(int nIndex) = 0;
    virtual std::unique_ptr<PDFiumSignature> getSignature(int nIndex) = 0;
    virtual std::unique_ptr<PDFiumAttachment> getAttachment(int nIndex) = 0;

    virtual std::vector<unsigned int> getTrailerEnds() = 0;
    virtual OUString getBookmarks() = 0;
};

struct VCL_DLLPUBLIC PDFiumLibrary final
{
    static std::shared_ptr<PDFium>& get();
};

// Tools

VCL_DLLPUBLIC OUString convertPdfDateToISO8601(std::u16string_view rInput);

} // namespace vcl::pdf

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
