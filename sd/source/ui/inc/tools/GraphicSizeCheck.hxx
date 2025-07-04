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
#include <drawdoc.hxx>
#include <svx/GenericCheckDialog.hxx>
#include <svx/svdograf.hxx>

namespace sd
{
/**
 * Class responsible to check if a graphic object violates the size
 * constraints and store the results.
 */
class GraphicSizeViolation final
{
private:
    SdrGrafObj* m_pGraphicObject;

    sal_Int32 m_nLowDPILimit = 0;
    sal_Int32 m_nHighDPILimit = 0;

    sal_Int32 m_nDPIX = 0;
    sal_Int32 m_nDPIY = 0;

public:
    GraphicSizeViolation(sal_Int32 nDPI, SdrGrafObj* pGraphicObject);
    bool check();

    const OUString& getGraphicName();

    SdrGrafObj* getObject() const { return m_pGraphicObject; }

    bool isDPITooLow() { return m_nDPIX < m_nLowDPILimit || m_nDPIY < m_nLowDPILimit; }

    bool isDPITooHigh() { return m_nDPIX > m_nHighDPILimit || m_nDPIY > m_nHighDPILimit; }

    sal_Int32 getDPIX() { return m_nDPIX; }

    sal_Int32 getDPIY() { return m_nDPIY; }
};

/**
 * Run the graphic size checks for all the graphic objects in the DOM
 * and store a list of violations.
 */
class GraphicSizeCheck final
{
private:
    SdDrawDocument* m_pDocument;
    std::vector<std::unique_ptr<GraphicSizeViolation>> m_aGraphicSizeViolationList;

public:
    GraphicSizeCheck(SdDrawDocument* pDocument)
        : m_pDocument(pDocument)
    {
    }

    void check();

    std::vector<std::unique_ptr<GraphicSizeViolation>>& getViolationList()
    {
        return m_aGraphicSizeViolationList;
    }
};

/** The UI part of the GraphicSizeViolation used by GenericCheckDialog */
class GraphicSizeCheckGUIEntry final : public svx::CheckData
{
private:
    SdDrawDocument* m_pDocument;
    std::unique_ptr<GraphicSizeViolation> m_pViolation;

public:
    GraphicSizeCheckGUIEntry(SdDrawDocument* pDocument,
                             std::unique_ptr<GraphicSizeViolation>&& pViolation)
        : m_pDocument(pDocument)
        , m_pViolation(std::move(pViolation))
    {
    }

    OUString getText() override;

    bool canMarkObject() override { return true; }

    void markObject() override;

    bool hasProperties() override { return true; }

    void runProperties() override;
};

/**
 * The UI part presenting the graphic size check results, which is
 * used by GenericCheckDialog
 */
class GraphicSizeCheckGUIResult final : public svx::CheckDataCollection
{
public:
    GraphicSizeCheckGUIResult(SdDrawDocument* m_pDocument);

    OUString getTitle() override;
};

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
