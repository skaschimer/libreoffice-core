/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <memory>

#include <sfx2/AccessibilityIssue.hxx>
#include <sfx2/sidebar/ControllerItem.hxx>
#include <sfx2/sidebar/PanelLayout.hxx>
#include <svl/poolitem.hxx>
#include <tools/link.hxx>
#include <vcl/weld.hxx>

#include <com/sun/star/ui/XSidebar.hpp>

#include <doc.hxx>

namespace sw::sidebar
{
class AccessibilityCheckEntry final
{
private:
    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Container> m_xContainer;
    std::unique_ptr<weld::Label> m_xLabel;
    std::unique_ptr<weld::LinkButton> m_xGotoButton;
    std::unique_ptr<weld::Button> m_xFixButton;

    std::shared_ptr<sfx::AccessibilityIssue> m_pAccessibilityIssue;

public:
    AccessibilityCheckEntry(weld::Container* pParent,
                            std::shared_ptr<sfx::AccessibilityIssue> const& pAccessibilityIssue);

    weld::Widget* get_widget() const { return m_xContainer.get(); }

    DECL_LINK(GotoButtonClicked, weld::LinkButton&, bool);
    DECL_LINK(FixButtonClicked, weld::Button&, void);
};

enum class AccessibilityCheckGroups : size_t
{
    Document = 0,
    Styles = 1,
    Linked = 2,
    NoAlt = 3,
    Table = 4,
    Formatting = 5,
    DirectFormatting = 6,
    Hyperlink = 7,
    Fakes = 8,
    Numbering = 9,
    Other = 10,
    LAST = Other
};

class AccessibilityCheckLevel
{
private:
    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Box> m_xContainer; ///< this is required for gtk3 even if unused
    css::uno::Reference<css::ui::XSidebar> m_xSidebar;
    std::array<std::vector<std::unique_ptr<AccessibilityCheckEntry>>, 11> m_aEntries;
    std::array<std::unique_ptr<weld::Expander>, 11> m_xExpanders;
    std::array<std::unique_ptr<weld::Box>, 11> m_xBoxes;

    DECL_LINK(ExpandHdl, weld::Expander&, void);

public:
    AccessibilityCheckLevel(weld::Box* pParent, css::uno::Reference<css::ui::XSidebar> xSidebar);

    void removeAllEntries();

    void addEntryForGroup(AccessibilityCheckGroups eGroup, std::vector<sal_Int32>& rIndices,
                          std::shared_ptr<sfx::AccessibilityIssue> const& pIssue);

    void show(size_t nGroupIndex);
    void hide(size_t nGroupIndex);
};

class A11yCheckIssuesPanel : public PanelLayout,
                             public ::sfx2::sidebar::ControllerItem::ItemUpdateReceiverInterface
{
public:
    static std::unique_ptr<PanelLayout> Create(weld::Widget* pParent, SfxBindings* pBindings,
                                               css::uno::Reference<css::ui::XSidebar> xSidebar);

    virtual void NotifyItemUpdate(const sal_uInt16 nSId, const SfxItemState eState,
                                  const SfxPoolItem* pState) override;

    virtual void GetControlState(const sal_uInt16 /*nSId*/,
                                 boost::property_tree::ptree& /*rState*/) override{};

    A11yCheckIssuesPanel(weld::Widget* pParent, SfxBindings* pBindings,
                         css::uno::Reference<css::ui::XSidebar> xSidebar);
    void ImplDestroy();
    virtual ~A11yCheckIssuesPanel() override;

private:
    std::unique_ptr<weld::Button> m_xOptionsButton;
    std::array<std::unique_ptr<weld::Expander>, 2> m_xLevelExpanders;
    std::array<std::unique_ptr<weld::Box>, 2> mxAccessibilityBox;
    std::array<std::unique_ptr<AccessibilityCheckLevel>, 2> m_aLevelEntries;
    std::unique_ptr<weld::Box> mxUpdateBox;
    std::unique_ptr<weld::LinkButton> mxUpdateLinkButton;
    std::unique_ptr<weld::Widget> m_xListSep;

    sfx::AccessibilityIssueCollection m_aIssueCollection;
    void removeAllEntries();
    void populateIssues();

    DECL_LINK(OptionsButtonClicked, weld::Button&, void);
    DECL_LINK(ExpandHdl, weld::Expander&, void);
    DECL_LINK(UpdateLinkButtonClicked, weld::LinkButton&, bool);
    DECL_LINK(PopulateIssuesHdl, void*, void);

    void addEntryForGroup(AccessibilityCheckGroups eGroup,
                          std::vector<std::vector<sal_Int32>>& rIndices,
                          std::shared_ptr<sfx::AccessibilityIssue> const& pIssue);

    SfxBindings* GetBindings() { return mpBindings; }

    SfxBindings* mpBindings;
    SwDoc* mpDoc;
    css::uno::Reference<css::ui::XSidebar> mxSidebar;
    ::sfx2::sidebar::ControllerItem maA11yCheckController;
    sal_Int32 mnIssueCount;
    bool mbAutomaticCheckEnabled;
};

} //end of namespace sw::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
