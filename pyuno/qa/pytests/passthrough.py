# -*- tab-width: 4; indent-tabs-mode: nil; py-indent-offset: 4 -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

import unittest
import unohelper
import org.libreoffice.unotest
from org.libreoffice.embindtest import thePassthrough

class MyUnoObject(unohelper.Base):
    pass

class PassthroughTest(unittest.TestCase):
    def test_passthrough(self):
        ctx = org.libreoffice.unotest.pyuno.getComponentContext()

        # Instantiate a Python object that exposes an UNO interface
        my_uno_object = MyUnoObject()

        # If we pass it through an UNO interface and back we should get exactly the same python
        # object back.
        returned_uno_object = thePassthrough.get(ctx).passthrough(my_uno_object)

        self.assertEqual(id(my_uno_object), id(returned_uno_object))


# vim: set shiftwidth=4 softtabstop=4 expandtab:
