/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <test/bootstrapfixture.hxx>
#include <cppunit/TestAssert.h>

#include <tools/fontenum.hxx>
#include <unotools/fontcfg.hxx>

#include <font/PhysicalFontFamily.hxx>
#include <font/FontSelectPattern.hxx>
#include <vcl/font.hxx>

#include "fontmocks.hxx"

using namespace vcl::font;

class VclPhysicalFontFamilyTest : public test::BootstrapFixture
{
public:
    VclPhysicalFontFamilyTest()
        : BootstrapFixture(true, false)
    {
    }

    void testCreateFontFamily();
    void testAddFontFace_Default();
    void testAddOneFontFace();
    void testAddTwoFontFaces();
    void testFindBestFontFaceByStyleName();
    void testFindBestFontFaceByStyleNameOnSubstitute();
    void testFindBestFontFaceDefaultStyle();
    void testFindBestFontFaceWeightSubfamily();

    CPPUNIT_TEST_SUITE(VclPhysicalFontFamilyTest);
    CPPUNIT_TEST(testCreateFontFamily);
    CPPUNIT_TEST(testAddFontFace_Default);
    CPPUNIT_TEST(testAddOneFontFace);
    CPPUNIT_TEST(testAddTwoFontFaces);
    CPPUNIT_TEST(testFindBestFontFaceByStyleName);
    CPPUNIT_TEST(testFindBestFontFaceByStyleNameOnSubstitute);
    CPPUNIT_TEST(testFindBestFontFaceDefaultStyle);
    CPPUNIT_TEST(testFindBestFontFaceWeightSubfamily);
    CPPUNIT_TEST_SUITE_END();
};

void VclPhysicalFontFamilyTest::testCreateFontFamily()
{
    PhysicalFontFamily aFamily(u"Test font face"_ustr);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Family name", u""_ustr, aFamily.GetFamilyName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Search name", u"Test font face"_ustr, aFamily.GetSearchName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Min quality", -1, aFamily.GetMinQuality());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Type faces", FontTypeFaces::NONE, aFamily.GetTypeFaces());

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Match family name", u""_ustr, aFamily.GetMatchFamilyName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Match type", ImplFontAttrs::None, aFamily.GetMatchType());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Match weight", WEIGHT_DONTKNOW, aFamily.GetMatchWeight());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Match width", WIDTH_DONTKNOW, aFamily.GetMatchWidth());
}

void VclPhysicalFontFamilyTest::testAddFontFace_Default()
{
    PhysicalFontFamily aFamily(u"Test font face"_ustr);

    aFamily.AddFontFace(new TestFontFace(1));

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Family name", u""_ustr, aFamily.GetFamilyName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Search name", u"Test font face"_ustr, aFamily.GetSearchName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Min quality", 0, aFamily.GetMinQuality());
    FontTypeFaces eTypeFace
        = FontTypeFaces::Scalable | FontTypeFaces::NoneSymbol | FontTypeFaces::NoneItalic;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Type faces", eTypeFace, aFamily.GetTypeFaces());
}

void VclPhysicalFontFamilyTest::testAddOneFontFace()
{
    PhysicalFontFamily aFamily(u"Test font face"_ustr);

    FontAttributes aFontAttrs;
    aFontAttrs.SetFamilyName(u"Test font face"_ustr);
    aFontAttrs.SetFamilyType(FontFamily::FAMILY_ROMAN);
    aFontAttrs.SetPitch(FontPitch::PITCH_VARIABLE);
    aFontAttrs.SetItalic(FontItalic::ITALIC_NONE);
    aFontAttrs.SetQuality(10);
    aFontAttrs.SetWeight(FontWeight::WEIGHT_BOLD);
    aFontAttrs.SetWidthType(FontWidth::WIDTH_EXPANDED);

    aFamily.AddFontFace(new TestFontFace(aFontAttrs, 1));

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Family name", u"Test font face"_ustr, aFamily.GetFamilyName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Search name", u"Test font face"_ustr, aFamily.GetSearchName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Min quality", 10, aFamily.GetMinQuality());
    FontTypeFaces eTypeFace = FontTypeFaces::Scalable | FontTypeFaces::NoneSymbol
                              | FontTypeFaces::Bold | FontTypeFaces::NoneItalic;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Type faces", eTypeFace, aFamily.GetTypeFaces());
}

void VclPhysicalFontFamilyTest::testAddTwoFontFaces()
{
    PhysicalFontFamily aFamily(u"Test font face"_ustr);

    FontAttributes aFontAttrs;
    aFontAttrs.SetFamilyName(u"Test font face"_ustr);
    aFontAttrs.SetFamilyType(FontFamily::FAMILY_ROMAN);
    aFontAttrs.SetPitch(FontPitch::PITCH_VARIABLE);
    aFontAttrs.SetItalic(FontItalic::ITALIC_NONE);
    aFontAttrs.SetQuality(10);
    aFontAttrs.SetWeight(FontWeight::WEIGHT_THIN);
    aFontAttrs.SetWidthType(FontWidth::WIDTH_EXPANDED);

    aFamily.AddFontFace(new TestFontFace(aFontAttrs, 1));

    aFontAttrs.SetFamilyName(u"Test font face"_ustr);
    aFontAttrs.SetFamilyType(FontFamily::FAMILY_ROMAN);
    aFontAttrs.SetPitch(FontPitch::PITCH_VARIABLE);
    aFontAttrs.SetItalic(FontItalic::ITALIC_NORMAL);
    aFontAttrs.SetQuality(5);
    aFontAttrs.SetWeight(FontWeight::WEIGHT_BOLD);
    aFontAttrs.SetWidthType(FontWidth::WIDTH_CONDENSED);

    aFamily.AddFontFace(new TestFontFace(aFontAttrs, 2));

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Family name", u"Test font face"_ustr, aFamily.GetFamilyName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Search name", u"Test font face"_ustr, aFamily.GetSearchName());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Min quality", 5, aFamily.GetMinQuality());
    FontTypeFaces eTypeFace = FontTypeFaces::Scalable | FontTypeFaces::NoneSymbol
                              | FontTypeFaces::Light | FontTypeFaces::Bold
                              | FontTypeFaces::NoneItalic | FontTypeFaces::Italic;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Type faces", eTypeFace, aFamily.GetTypeFaces());
}

void VclPhysicalFontFamilyTest::testFindBestFontFaceByStyleName()
{
    // An explicit style name must pick the matching face even though all faces
    // share the family name.
    PhysicalFontFamily aFamily(u"Test"_ustr);

    FontAttributes aRegular;
    aRegular.SetFamilyName(u"Test"_ustr);
    aRegular.SetStyleName(u"Regular"_ustr);
    aRegular.SetWeight(WEIGHT_NORMAL);
    aRegular.SetWidthType(WIDTH_NORMAL);
    aFamily.AddFontFace(new TestFontFace(aRegular, 1));

    FontAttributes aCondensed;
    aCondensed.SetFamilyName(u"Test"_ustr);
    aCondensed.SetStyleName(u"Condensed"_ustr);
    aCondensed.SetWeight(WEIGHT_NORMAL);
    aCondensed.SetWidthType(WIDTH_CONDENSED);
    TestFontFace* pCondensedFace = new TestFontFace(aCondensed, 2);
    aFamily.AddFontFace(pCondensedFace);

    vcl::Font aFont(u"Test"_ustr, Size(0, 12));
    FontSelectPattern aFSP(aFont, u"Test"_ustr, Size(0, 12), 12.0f);
    aFSP.SetStyleName(u"Condensed"_ustr);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("explicit style name should select the matching face",
                                 static_cast<PhysicalFontFace*>(pCondensedFace),
                                 aFamily.FindBestFontFace(aFSP));
}

void VclPhysicalFontFamilyTest::testFindBestFontFaceByStyleNameOnSubstitute()
{
    // If requested family is missing, an explicit style name must still select
    // the face with matching style.
    PhysicalFontFamily aFamily(u"Test"_ustr);

    FontAttributes aRegular;
    aRegular.SetFamilyName(u"Test"_ustr);
    aRegular.SetStyleName(u"Regular"_ustr);
    aRegular.SetWeight(WEIGHT_NORMAL);
    aRegular.SetWidthType(WIDTH_NORMAL);
    aFamily.AddFontFace(new TestFontFace(aRegular, 1));

    FontAttributes aCondensed;
    aCondensed.SetFamilyName(u"Test"_ustr);
    aCondensed.SetStyleName(u"Condensed"_ustr);
    aCondensed.SetWeight(WEIGHT_NORMAL);
    aCondensed.SetWidthType(WIDTH_CONDENSED);
    TestFontFace* pCondensedFace = new TestFontFace(aCondensed, 2);
    aFamily.AddFontFace(pCondensedFace);

    vcl::Font aFont(u"Substitute"_ustr, Size(0, 12));
    FontSelectPattern aFSP(aFont, u"Substitute"_ustr, Size(0, 12), 12.0f);
    aFSP.SetStyleName(u"Condensed"_ustr);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("style name should select the face even when the family differs",
                                 static_cast<PhysicalFontFace*>(pCondensedFace),
                                 aFamily.FindBestFontFace(aFSP));
}

void VclPhysicalFontFamilyTest::testFindBestFontFaceDefaultStyle()
{
    // With no style requested, the default (RIBBI) style must win over an
    // extended subfamily with the same weight/italic/width, so a bare family
    // request picks "Regular" rather than "Small Caps".
    PhysicalFontFamily aFamily(u"Test"_ustr);

    FontAttributes aRegular;
    aRegular.SetFamilyName(u"Test"_ustr);
    aRegular.SetStyleName(u"Regular"_ustr);
    aRegular.SetWeight(WEIGHT_NORMAL);
    aRegular.SetWidthType(WIDTH_NORMAL);
    TestFontFace* pRegularFace = new TestFontFace(aRegular, 1);
    aFamily.AddFontFace(pRegularFace);

    FontAttributes aSmallCaps;
    aSmallCaps.SetFamilyName(u"Test"_ustr);
    aSmallCaps.SetStyleName(u"Small Caps"_ustr);
    aSmallCaps.SetWeight(WEIGHT_NORMAL);
    aSmallCaps.SetWidthType(WIDTH_NORMAL);
    aFamily.AddFontFace(new TestFontFace(aSmallCaps, 2));

    vcl::Font aFont(u"Test"_ustr, Size(0, 12));
    FontSelectPattern aFSP(aFont, u"Test"_ustr, Size(0, 12), 12.0f);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("a bare family request should select the default face",
                                 static_cast<PhysicalFontFace*>(pRegularFace),
                                 aFamily.FindBestFontFace(aFSP));
}

void VclPhysicalFontFamilyTest::testFindBestFontFaceWeightSubfamily()
{
    // An extended subfamily that is itself a weight ("Light") is the
    // authoritative style: requesting it must select the Light face even
    // though the requested weight is NORMAL.
    PhysicalFontFamily aFamily(u"Test"_ustr);

    FontAttributes aRegular;
    aRegular.SetFamilyName(u"Test"_ustr);
    aRegular.SetStyleName(u"Regular"_ustr);
    aRegular.SetWeight(WEIGHT_NORMAL);
    aRegular.SetWidthType(WIDTH_NORMAL);
    aFamily.AddFontFace(new TestFontFace(aRegular, 1));

    FontAttributes aLight;
    aLight.SetFamilyName(u"Test"_ustr);
    aLight.SetStyleName(u"Light"_ustr);
    aLight.SetWeight(WEIGHT_LIGHT);
    aLight.SetWidthType(WIDTH_NORMAL);
    TestFontFace* pLightFace = new TestFontFace(aLight, 2);
    aFamily.AddFontFace(pLightFace);

    vcl::Font aFont(u"Test"_ustr, Size(0, 12));
    aFont.SetWeight(WEIGHT_NORMAL);
    FontSelectPattern aFSP(aFont, u"Test"_ustr, Size(0, 12), 12.0f);
    aFSP.SetStyleName(u"Light"_ustr);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("an extended subfamily must select its face over the regular one",
                                 static_cast<PhysicalFontFace*>(pLightFace),
                                 aFamily.FindBestFontFace(aFSP));
}

CPPUNIT_TEST_SUITE_REGISTRATION(VclPhysicalFontFamilyTest);

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
