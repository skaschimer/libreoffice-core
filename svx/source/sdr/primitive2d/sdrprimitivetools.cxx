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

#include <sdr/primitive2d/sdrprimitivetools.hxx>
#include <tools/lazydelete.hxx>
#include <vcl/BitmapTools.hxx>
#include <mutex>


// helper methods

namespace drawinglayer::primitive2d
{
        Bitmap createDefaultCross_3x3(const basegfx::BColor& rBColor)
        {
            static tools::DeleteOnDeinit< Bitmap > aRetVal(tools::DeleteOnDeinitFlag::Empty);
            static basegfx::BColor aBColor;
            static std::mutex aMutex;

            std::scoped_lock aGuard(aMutex);

            if(!aRetVal.get() || rBColor != aBColor)
            {
                // copy values
                aBColor = rBColor;

                // create bitmap
                Color c(aBColor);
                sal_uInt8 r = c.GetRed();
                sal_uInt8 g = c.GetGreen();
                sal_uInt8 b = c.GetBlue();
                sal_uInt8 a = 255;
                const sal_uInt8 cross[] = {
                   0, 0, 0, a,   r, g, b, 0,   0, 0, 0, a,
                   r, g, b, 0,   r, g, b, 0,   r, g, b, 0,
                   0, 0, 0, a,   r, g, b, 0,   0, 0, 0, a
                };
                Bitmap aBitmap = vcl::bitmap::CreateFromData(cross, 3, 3, 12, /*nBitsPerPixel*/32);

                // create and exchange at aRetVal
                aRetVal.set(aBitmap);
            }

            return aRetVal.get() ? *aRetVal.get() : Bitmap();
        }
} // end of namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
