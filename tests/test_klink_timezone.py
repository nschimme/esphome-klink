#!/usr/bin/env python3
"""Reference tests for components/klink/klink_timezone.py (stdlib unittest).

Run: python3 tests/test_klink_timezone.py
"""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "components" / "klink"))
import klink_timezone as tz  # noqa: E402


class Iso6709(unittest.TestCase):
    def test_minutes_and_seconds(self):
        self.assertEqual(tz.iso6709("+4153-08737"), (41 + 53 / 60.0, -(87 + 37 / 60.0)))
        lat, lon = tz.iso6709("-335200+1511200")
        self.assertAlmostEqual(lat, -33.8667, places=3)
        self.assertAlmostEqual(lon, 151.2, places=3)

    def test_garbage(self):
        self.assertIsNone(tz.iso6709("garbage"))


class TimezoneLocation(unittest.TestCase):
    def setUp(self):
        if tz.timezone_location("America/Chicago") is None:
            self.skipTest("the tzdata package is not installed")

    def test_known_zones(self):
        for name, lat, lon in (("America/Chicago", 41.85, -87.65), ("Europe/London", 51.51, -0.13),
                               ("Australia/Sydney", -33.87, 151.21)):
            found = tz.timezone_location(name)
            self.assertAlmostEqual(found[0], lat, delta=0.1, msg=name)
            self.assertAlmostEqual(found[1], lon, delta=0.1, msg=name)

    def test_unknown_zone(self):
        self.assertIsNone(tz.timezone_location("Not/AZone"))


if __name__ == "__main__":
    unittest.main()
