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

#include <vcl/filter/PngImageWriter.hxx>
#include <vcl/toolkit/treelistentry.hxx>
#include <vcl/toolkit/viewdataentry.hxx>
#include <iconview.hxx>
#include "iconviewimpl.hxx"
#include <vcl/uitest/uiobject.hxx>
#include <tools/json_writer.hxx>
#include <vcl/toolkit/svlbitm.hxx>
#include <tools/stream.hxx>
#include <vcl/cvtgrf.hxx>
#include <comphelper/base64.hxx>
#include <comphelper/propertyvalue.hxx>

namespace
{
const int separatorHeight = 10;
const int nSpacing = 5; // 5 pixels from top, from bottom, between icon and label
}

IconView::IconView(vcl::Window* pParent, WinBits nBits)
    : SvTreeListBox(pParent, nBits)
{
    m_nColumnCount = 1;
    mbCenterAndClipText = true;
    SetEntryWidth(100);

    pImpl.reset(new IconViewImpl(this, GetModel(), GetStyle()));
}

Size IconView::GetEntrySize(const SvTreeListEntry& entry) const
{
    if (entry.GetFlags() & SvTLEntryFlags::IS_SEPARATOR)
        return { GetEntryWidth() * GetColumnCount(), separatorHeight };
    return { GetEntryWidth(), GetEntryHeight() };
}

void IconView::UpdateEntrySize(const Image& pImage)
{
    int spacing = nSpacing * 2;
    SetEntryHeight(pImage.GetSizePixel().getHeight() + spacing);
    SetEntryWidth(pImage.GetSizePixel().getWidth() + spacing);
}

void IconView::CalcEntryHeight(SvTreeListEntry const* pEntry)
{
    int nHeight = nSpacing * 2;
    SvViewDataEntry* pViewData = GetViewDataEntry(pEntry);
    const size_t nCount = pEntry->ItemCount();
    bool bHasIcon = false;
    for (size_t nCur = 0; nCur < nCount; ++nCur)
    {
        nHeight += SvLBoxItem::GetHeight(pViewData, nCur);

        if (!bHasIcon && pEntry->GetItem(nCur).GetType() == SvLBoxItemType::ContextBmp)
            bHasIcon = true;
    }

    if (bHasIcon && nCount > 1)
        nHeight += nSpacing; // between icon and label

    if (nHeight > nEntryHeight)
    {
        nEntryHeight = nHeight;
        Control::SetFont(GetFont());
        pImpl->SetEntryHeight();
    }
}

void IconView::Resize()
{
    Size aBoxSize = Control::GetOutputSizePixel();

    if (!aBoxSize.Width())
        return;

    m_nColumnCount = nEntryWidth ? aBoxSize.Width() / nEntryWidth : 1;

    SvTreeListBox::Resize();
}

tools::Rectangle IconView::GetFocusRect(const SvTreeListEntry* pEntry, tools::Long)
{
    return { GetEntryPosition(pEntry), GetEntrySize(*pEntry) };
}

void IconView::PaintEntry(SvTreeListEntry& rEntry, tools::Long nX, tools::Long nY,
                          vcl::RenderContext& rRenderContext)
{
    pImpl->UpdateContextBmpWidthMax(&rEntry);

    const Size entrySize = GetEntrySize(rEntry);
    short nTempEntryHeight = entrySize.Height();
    short nTempEntryWidth = entrySize.Width();

    Point aEntryPos(nX, nY);

    rRenderContext.Push(vcl::PushFlags::FILLCOLOR | vcl::PushFlags::LINECOLOR
                        | vcl::PushFlags::FONT);
    const Color aBackupColor = rRenderContext.GetFillColor();

    const StyleSettings& rSettings = rRenderContext.GetSettings().GetStyleSettings();

    const Size aOutputSize = GetOutputSizePixel();
    if (aOutputSize.getHeight() < nTempEntryHeight)
        nTempEntryHeight = aOutputSize.getHeight();

    const SvViewDataEntry* pViewDataEntry = GetViewDataEntry(&rEntry);

    if (pViewDataEntry->IsHighlighted())
    {
        vcl::Font aHighlightFont(rRenderContext.GetFont());
        const Color aHighlightTextColor(rSettings.GetHighlightTextColor());
        aHighlightFont.SetColor(aHighlightTextColor);

        // set font color to highlight
        rRenderContext.SetTextColor(aHighlightTextColor);
        rRenderContext.SetFont(aHighlightFont);
    }

    bool bFillColorSet = false;
    // draw background
    if (!(nTreeFlags & SvTreeFlags::USESEL))
    {
        // set background pattern/color
        Wallpaper aWallpaper = rRenderContext.GetBackground();

        if (pViewDataEntry->IsHighlighted())
        {
            Color aNewWallColor = rSettings.GetHighlightColor();
            // if the face color is bright then the deactivate color is also bright
            // -> so you can't see any deactivate selection
            const WinBits nWindowStyle = GetStyle();
            const bool bHideSelection = (nWindowStyle & WB_HIDESELECTION) != 0 && !HasFocus();
            if (bHideSelection && !rSettings.GetFaceColor().IsBright()
                && aWallpaper.GetColor().IsBright() != rSettings.GetDeactiveColor().IsBright())
            {
                aNewWallColor = rSettings.GetDeactiveColor();
            }
            aWallpaper.SetColor(aNewWallColor);
        }

        Color aBackgroundColor = aWallpaper.GetColor();
        if (aBackgroundColor != COL_TRANSPARENT)
        {
            rRenderContext.SetFillColor(aBackgroundColor);
            bFillColorSet = true;
            // this case may occur for smaller horizontal resizes
            if (nTempEntryWidth > 1)
                rRenderContext.DrawRect({ aEntryPos, Size(nTempEntryWidth, nTempEntryHeight) });
        }
    }

    const size_t nItemCount = rEntry.ItemCount();
    size_t nIconItem = nItemCount;

    int nLabelHeight = 0;
    std::vector<size_t> aTextItems;

    for (size_t nCurItem = 0; nCurItem < nItemCount; ++nCurItem)
    {
        SvLBoxItem& rItem = rEntry.GetItem(nCurItem);
        SvLBoxItemType nItemType = rItem.GetType();

        if (nItemType == SvLBoxItemType::ContextBmp)
        {
            nIconItem = nCurItem;
            continue;
        }

        aTextItems.push_back(nCurItem);
        auto nItemHeight = SvLBoxItem::GetHeight(pViewDataEntry, nCurItem);
        nLabelHeight += nItemHeight;
    }

    int nLabelYPos = nY + nTempEntryHeight - nLabelHeight - nSpacing; // padding from bottom
    for (auto nCurItem : aTextItems)
    {
        aEntryPos.setY(nLabelYPos);

        auto nItemHeight = SvLBoxItem::GetHeight(pViewDataEntry, nCurItem);
        nLabelYPos += nItemHeight;

        rEntry.GetItem(nCurItem).Paint(aEntryPos, *this, rRenderContext, pViewDataEntry, rEntry);
    }

    if (bFillColorSet)
        rRenderContext.SetFillColor(aBackupColor);

    // draw icon
    if (nIconItem < nItemCount)
    {
        SvLBoxItem& rItem = rEntry.GetItem(nIconItem);
        auto nItemWidth = rItem.GetWidth(this, pViewDataEntry, nIconItem);
        auto nItemHeight = SvLBoxItem::GetHeight(pViewDataEntry, nIconItem);

        aEntryPos.setY(nY);

        // center horizontally
        aEntryPos.AdjustX((nTempEntryWidth - nItemWidth) / 2);
        // center vertically
        int nImageAreaHeight = nTempEntryHeight - nSpacing * 2; // spacings from top, from bottom
        if (nLabelHeight > 0)
        {
            nImageAreaHeight -= nLabelHeight + nSpacing; // spacing between icon and label
        }
        aEntryPos.AdjustY((nImageAreaHeight - nItemHeight) / 2 + nSpacing);

        rItem.Paint(aEntryPos, *this, rRenderContext, pViewDataEntry, rEntry);
    }

    rRenderContext.Pop();
}

FactoryFunction IconView::GetUITestFactory() const { return IconViewUIObject::create; }

static OString extractPngString(const SvLBoxContextBmp* pBmpItem)
{
    Bitmap aImage = pBmpItem->GetBitmap1().GetBitmap();
    SvMemoryStream aOStm(65535, 65535);
    // Use fastest compression "1"
    css::uno::Sequence<css::beans::PropertyValue> aFilterData{
        comphelper::makePropertyValue(u"Compression"_ustr, sal_Int32(1)),
    };
    vcl::PngImageWriter aPNGWriter(aOStm);
    aPNGWriter.setParameters(aFilterData);
    if (aPNGWriter.write(aImage))
    {
        css::uno::Sequence<sal_Int8> aSeq(static_cast<sal_Int8 const*>(aOStm.GetData()),
                                          aOStm.Tell());
        OStringBuffer aBuffer("data:image/png;base64,");
        ::comphelper::Base64::encode(aBuffer, aSeq);
        return aBuffer.makeStringAndClear();
    }

    return ""_ostr;
}

OUString IconView::renderEntry(int pos, int /*dpix*/, int /*dpiy*/) const
{
    // TODO: support various DPI
    SvTreeListEntry* pEntry = GetEntry(pos);
    if (!pEntry)
        return "";

    OUString sResult;
    const bool bHandled
        = maDumpImageHdl.IsSet() && maDumpImageHdl.Call(encoded_image_query(sResult, pEntry));

    if (!bHandled)
    {
        if (const SvLBoxItem* pIt = pEntry->GetFirstItem(SvLBoxItemType::ContextBmp))
        {
            const SvLBoxContextBmp* pBmpItem = static_cast<const SvLBoxContextBmp*>(pIt);
            if (pBmpItem)
                return OStringToOUString(extractPngString(pBmpItem), RTL_TEXTENCODING_ASCII_US);
        }
    }

    return sResult;
}

void IconView::DumpEntryAndSiblings(tools::JsonWriter& rJsonWriter, SvTreeListEntry* pEntry)
{
    while (pEntry)
    {
        auto aNode = rJsonWriter.startStruct();

        // simple listbox value
        const SvLBoxItem* pIt = pEntry->GetFirstItem(SvLBoxItemType::String);
        if (pIt)
            rJsonWriter.put("text", static_cast<const SvLBoxString*>(pIt)->GetText());

        pIt = pEntry->GetFirstItem(SvLBoxItemType::ContextBmp);
        if (pIt)
        {
            const SvLBoxContextBmp* pBmpItem = static_cast<const SvLBoxContextBmp*>(pIt);
            if (pBmpItem)
                rJsonWriter.put("ondemand", true);
        }

        if (const OUString tooltip = GetEntryTooltip(pEntry); !tooltip.isEmpty())
            rJsonWriter.put("tooltip", tooltip);

        if (IsSelected(pEntry))
            rJsonWriter.put("selected", true);

        if (pEntry->GetFlags() & SvTLEntryFlags::IS_SEPARATOR)
            rJsonWriter.put("separator", true);

        rJsonWriter.put("row", GetModel()->GetAbsPos(pEntry));

        pEntry = pEntry->NextSibling();
    }
}

void IconView::DumpAsPropertyTree(tools::JsonWriter& rJsonWriter)
{
    SvTreeListBox::DumpAsPropertyTree(rJsonWriter);
    rJsonWriter.put("type", "iconview");
    rJsonWriter.put("singleclickactivate", GetActivateOnSingleClick());
    rJsonWriter.put("textWithIconEnabled", IsTextColumnEnabled());
    auto aNode = rJsonWriter.startArray("entries");
    DumpEntryAndSiblings(rJsonWriter, First());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
