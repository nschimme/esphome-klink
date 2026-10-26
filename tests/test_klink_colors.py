#!/usr/bin/env python3
"""Reference tests for tools/klink_colors.py (stdlib unittest).

Run: python3 tests/test_klink_colors.py
"""

import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import klink_colors as kc  # noqa: E402

HEX = re.compile(r"^#[0-9a-f]{6}$")


def channels(value):
    return [int(value[i:i + 2], 16) for i in (1, 3, 5)]


class StopTests(unittest.TestCase):
    def test_sun_endpoints(self):
        warm, cool = channels(kc.SUN_STOPS[0]), channels(kc.SUN_STOPS[-1])
        self.assertGreaterEqual(warm[0], cool[0], "CLR 0 must be warmer (more red)")
        self.assertLessEqual(warm[2], cool[2], "CLR 0 must be warmer (less blue)")

    def test_sun_endpoint_temperatures(self):
        for clr, kelvin in ((0, 6000.0), (255, 9000.0)):
            got = kc._mired_kelvin(clr)
            self.assertAlmostEqual(got, kelvin, places=6)

    def test_seventeen_stops(self):
        self.assertEqual(len(kc.SUN_STOPS), 17)
        self.assertEqual(len(kc.BLUE_STOPS), 17)

    def test_hex_format(self):
        for value in kc.SUN_STOPS + kc.BLUE_STOPS + list(kc.OVERLAYS.values()):
            self.assertRegex(value, HEX)

    def test_blue_ramp_direction(self):
        deep, light = channels(kc.BLUE_STOPS[0]), channels(kc.BLUE_STOPS[-1])
        self.assertGreaterEqual(light[2], deep[2])
        self.assertGreaterEqual(light[0], deep[0])


class SwatchTests(unittest.TestCase):
    def test_zero_overlay_keeps_base(self):
        base = kc.swatch("tuna_sun", 120)
        self.assertEqual(kc.swatch("tuna_sun", 120, {"r": 0, "am": 0}), base)

    def test_full_overlay_moves_sixty_percent(self):
        base = channels(kc.swatch("tuna_sun", 0))
        overlay = channels(kc.OVERLAYS["r"])
        got = channels(kc.swatch("tuna_sun", 0, {"r": 100}))
        for i in range(3):
            expected = base[i] + (overlay[i] - base[i]) * 0.6
            self.assertLessEqual(abs(got[i] - expected), 1)

    def test_swatch_bounds(self):
        for model in ("tuna_sun", "tuna_blue"):
            for clr in (0, 1, 64, 128, 200, 255):
                for overlays in ({}, {"r": 100}, {"vio": 50, "grn": 100}, {"am": 100, "r": 100}):
                    got = channels(kc.swatch(model, clr, overlays))
                    for value in got:
                        self.assertGreaterEqual(value, 0)
                        self.assertLessEqual(value, 255)

    def test_generic_rejected(self):
        with self.assertRaises(ValueError):
            kc.swatch("generic", 0)


if __name__ == "__main__":
    unittest.main()
