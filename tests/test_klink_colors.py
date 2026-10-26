#!/usr/bin/env python3
"""Reference tests for components/klink/klink_colors.py (stdlib unittest).

Run: python3 tests/test_klink_colors.py
"""

import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "components" / "klink"))
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


class IntegerModelTests(unittest.TestCase):
    def test_int_stops_round_trip(self):
        for model, stops in (("tuna_sun", kc.SUN_STOPS), ("tuna_blue", kc.BLUE_STOPS)):
            self.assertEqual(kc.int_stops(model), [tuple(channels(v)) for v in stops])

    def test_swatch_i_endpoints(self):
        for model in ("tuna_sun", "tuna_blue"):
            stops = kc.int_stops(model)
            self.assertEqual(kc.swatch_i(model, 0, []), stops[0])
            self.assertEqual(kc.swatch_i(model, 255, []), stops[-1])

    def test_overlay_full_moves_sixty_percent(self):
        for model in ("tuna_sun", "tuna_blue"):
            n = len(kc.CHANNELS[model])
            for clr in (0, 64, 128, 200, 255):
                base = kc.swatch_i(model, clr, [0] * n)
                for index, rgb in enumerate(kc._overlay_rgb(model)):
                    overlays = [0] * n
                    overlays[index] = 100
                    got = kc.swatch_i(model, clr, overlays)
                    for c in range(3):
                        expected = base[c] + (rgb[c] - base[c]) * 0.6
                        self.assertLessEqual(abs(got[c] - expected), 1)

    def test_nearest_i_exact_on_own_stops(self):
        for model in ("tuna_sun", "tuna_blue"):
            n = len(kc.CHANNELS[model])
            for stop in kc.int_stops(model):
                result = kc.nearest_i(model, stop)
                self.assertEqual(result["dist"], 0)
                self.assertEqual(result["overlays"], [0] * n)
                self.assertEqual(kc.swatch_i(model, result["clr"], result["overlays"]), result["rgb"])

    def test_nearest_i_bounds_and_determinism(self):
        for model in ("tuna_sun", "tuna_blue"):
            for target in ((0, 0, 0), (255, 255, 255), (12, 34, 56), (255, 128, 0)):
                first = kc.nearest_i(model, target)
                second = kc.nearest_i(model, target)
                self.assertEqual(first, second)
                self.assertLessEqual(first["evals"], kc.MAX_EVALS)
                self.assertEqual(kc.dist_i(first["rgb"], target), first["dist"])
                self.assertEqual(kc.swatch_i(model, first["clr"], first["overlays"]), first["rgb"])

    def test_float_and_integer_swatch_agree(self):
        for model in ("tuna_sun", "tuna_blue"):
            labels = [label for _, label in kc.CHANNELS[model]]
            for clr in range(0, 256, 17):
                for level in (0, 50, 100):
                    configs = [{label: level for label in labels}]
                    for label in labels:
                        configs.append({label: level})
                    for config in configs:
                        want = channels(kc.swatch(model, clr, config))
                        got = list(kc.swatch_i(model, clr, [config.get(label, 0) for label in labels]))
                        for c in range(3):
                            self.assertLessEqual(abs(want[c] - got[c]), 1)


class EmitTests(unittest.TestCase):
    def test_emit_cpp_has_specs(self):
        text = kc.emit_cpp()
        self.assertIn("COLOR_SUN", text)
        self.assertIn("COLOR_BLUE", text)
        self.assertIn("ColorSpec", text)

    def test_emit_cpp_firmware_definitions_only(self):
        text = kc.emit_cpp(firmware=True)
        self.assertNotIn("#include", text)
        self.assertNotIn("#pragma", text)
        self.assertIn("namespace esphome { namespace klink {", text)
        self.assertIn("const kessil::klink::ColorSpec COLOR_SUN =", text)
        self.assertIn("const kessil::klink::ColorSpec COLOR_BLUE =", text)
        # The firmware specs are non-static so klink_web.cpp can link them.
        self.assertNotIn("static const ColorSpec COLOR_SUN", text)
        self.assertIn("static const uint8_t COLOR_SUN_STOPS[17][3]", text)
        self.assertIn("static const uint8_t COLOR_SUN_OVERLAY_CHANNEL[2]", text)
        self.assertIn("static const uint8_t COLOR_BLUE_OVERLAY_CHANNEL[3]", text)

    def test_emit_cpp_default_still_standalone(self):
        text = kc.emit_cpp()
        self.assertIn("#pragma once", text)
        self.assertIn('#include "klink_colors.h"', text)
        self.assertIn("static const ColorSpec COLOR_SUN", text)
        self.assertNotIn("namespace esphome", text)

    def test_emit_golden_shape(self):
        lines = kc.emit_golden().splitlines()
        self.assertEqual(len(lines), 2 * (17 + 12 + 40))
        for line in lines:
            self.assertEqual(len(line.split()), 13)


if __name__ == "__main__":
    unittest.main()
