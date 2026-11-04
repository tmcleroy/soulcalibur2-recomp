# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic inventory checks; no game files or playthrough are required."""
import struct
import unittest
from build_graphics_inventory import scan_models, recipe, RECIPE_SIZE

class InventoryTests(unittest.TestCase):
    def model(self):
        d=bytearray(0xa0);d[:7]=b'VMX.'+bytes((4,16,13))
        struct.pack_into('<H',d,20,1);struct.pack_into('<I',d,28,0x50)
        return d
    def test_material_bounds(self):
        with self.assertRaises(ValueError):scan_models(self.model()[:-1],'fixture')
    def test_material_selection(self):
        d=self.model();r=scan_models(d,'fixture')
        self.assertEqual((r['models'],r['materials']),(1,1));self.assertFalse(r['unsupported'])
        struct.pack_into('<3I',d,0x54,0x50,1,1)
        self.assertEqual(len(scan_models(d,'fixture')['unsupported']),1)
    def test_recipe_layout(self):
        p=bytes(856);r=recipe(0x152,p)
        self.assertEqual(len(r),RECIPE_SIZE);self.assertEqual(r[16],0)
        with self.assertRaises(ValueError):recipe(0x142,p,program=b'bad')

if __name__ == '__main__':
    unittest.main()
