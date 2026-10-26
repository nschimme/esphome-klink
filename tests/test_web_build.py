#!/usr/bin/env python3
"""Tests for components/klink/web_build.py (stdlib unittest).

Run: python3 tests/test_web_build.py
"""

import gzip
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Import the mock first: it imports http.server (and stdlib datetime) before it puts
# components/klink on the path, where a datetime.py would otherwise shadow the stdlib module.
sys.path.insert(0, str(ROOT / "tools"))
import mock_server  # noqa: E402
import web_build  # noqa: E402


class AssembleTests(unittest.TestCase):
    def test_self_contained(self):
        page = web_build.assemble()
        for needle in ("<link", "src=", "/*KCOL*/", "COLORS:"):
            self.assertNotIn(needle, page, "assembled page still contains %r" % needle)
        self.assertIn("<style>", page)
        self.assertIn("<script>", page)
        self.assertIn("var KCOL=", page)

    def test_deterministic(self):
        self.assertEqual(web_build.assemble(), web_build.assemble())
        self.assertEqual(web_build.page_gzip(), web_build.page_gzip())

    def test_gzip_round_trip(self):
        page = web_build.assemble()
        self.assertEqual(gzip.decompress(web_build.page_gzip()).decode("utf-8"), page)

    def test_gzip_within_limit(self):
        self.assertLessEqual(len(web_build.page_gzip()), web_build.MAX_GZIP_BYTES)


class BadSourceTests(unittest.TestCase):
    def _use_sources(self, index, css, js):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = pathlib.Path(tmp.name)
        (root / "index.html").write_text(index, encoding="utf-8")
        (root / "style.css").write_text(css, encoding="utf-8")
        (root / "app.js").write_text(js, encoding="utf-8")
        old = web_build.WEB_DIR
        self.addCleanup(setattr, web_build, "WEB_DIR", old)
        web_build.WEB_DIR = root

    def test_missing_color_marker(self):
        self._use_sources(
            '<link rel="stylesheet" href="style.css">\n<script src="app.js"></script>\n',
            "body{}\n",
            "var x=1;\n",
        )
        with self.assertRaises(ValueError):
            web_build.assemble()

    def test_missing_style_link(self):
        self._use_sources('<script src="app.js"></script>\n', "body{}\n", "/*KCOL*/\n")
        with self.assertRaises(ValueError):
            web_build.assemble()

    def test_script_tag_in_js_rejected(self):
        self._use_sources(
            '<link rel="stylesheet" href="style.css">\n<script src="app.js"></script>\n',
            "body{}\n",
            "/*KCOL*/\nvar x='</script>';\n",
        )
        with self.assertRaises(ValueError):
            web_build.assemble()


class MockManyTests(unittest.TestCase):
    """The --many laboratory, checked through the mock's /klink/state handler."""

    def test_many_state(self):
        mock_server.use_many()
        state = mock_server.state_json()
        gids = sorted(group["gid"] for group in state["groups"])
        self.assertEqual(gids, list(range(1, 11)))
        self.assertEqual(len(state["lamps"]), 32)
        self.assertEqual(state["bus"]["known_lamps"], 32)
        known = set(gids)
        orphans = sorted({lamp["gid"] for lamp in state["lamps"] if lamp["gid"] not in known})
        self.assertEqual(orphans, [11, 12, 20])
        for group in state["groups"]:
            in_group = sorted(lamp["kid"] for lamp in state["lamps"] if lamp["gid"] == group["gid"])
            self.assertEqual(sorted(group["lamps"]), in_group)

    def test_defaults_match_initial_slots(self):
        mock_server.use_many()
        state = mock_server.state_json()
        for group in state["groups"]:
            self.assertEqual(group["defaults"], group["slots"])
        original = [dict(p, ch=list(p["ch"])) for p in state["groups"][0]["defaults"]]
        mock_server.handle_point({"gid": ["1"], "slot": ["1"], "minute": ["1"]})
        edited = mock_server.state_json()["groups"][0]
        self.assertNotEqual(edited["slots"], edited["defaults"])
        self.assertEqual(edited["defaults"], original, "an edit must not move the reported defaults")

    def test_many_assign_keeps_state_consistent(self):
        mock_server.use_many()
        mock_server.handle_assign({"kid": ["18000021"], "gid": ["3"]})
        state = mock_server.state_json()
        self.assertEqual(len(state["lamps"]), 32)
        moved = [lamp for lamp in state["lamps"] if lamp["kid"] == 18000021]
        self.assertEqual(moved[0]["gid"], 3)
        group3 = [group for group in state["groups"] if group["gid"] == 3][0]
        self.assertIn(18000021, group3["lamps"])
        self.assertEqual(state["bus"]["known_lamps"], 32)


class MockSnapTests(unittest.TestCase):
    """GET /klink/snap through the mock's handler, without sockets."""

    def setUp(self):
        self.saved = list(mock_server.GROUPS)
        mock_server.GROUPS[:] = [
            mock_server.make_group(
                2, "Tuna Sun", "tuna_sun", ["int", "clr", None, "r", "am"], []
            ),
            mock_server.make_group(
                1, "Tuna Blue", "tuna_blue", ["int", "clr", "vio", "r", "grn"], []
            ),
            mock_server.make_group(
                3, "Generic", "generic", ["ui1", "ui2", "ui3", "ui4", "ui5"], []
            ),
        ]

    def tearDown(self):
        mock_server.GROUPS[:] = self.saved

    def snap(self, **query):
        return mock_server.handle_snap({key: [value] for key, value in query.items()})

    def test_tuna_sun(self):
        payload = self.snap(gid="2", rgb="ff0000")
        expected = mock_server.klink_colors.nearest_i("tuna_sun", (255, 0, 0))
        self.assertEqual(payload["set"]["clr"], expected["clr"])
        self.assertEqual(list(payload["set"]), ["clr", "r", "am"])
        self.assertEqual([payload["set"]["r"], payload["set"]["am"]], expected["overlays"])
        self.assertEqual(payload["rgb"], "%02x%02x%02x" % expected["rgb"])
        self.assertEqual(payload["target"], "ff0000")
        self.assertEqual(payload["exact"], expected["dist"] == 0)

    def test_tuna_blue(self):
        payload = self.snap(gid="1", rgb="#00ff00")
        expected = mock_server.klink_colors.nearest_i("tuna_blue", (0, 255, 0))
        self.assertEqual(list(payload["set"]), ["clr", "vio", "r", "grn"])
        self.assertEqual(
            [payload["set"]["vio"], payload["set"]["r"], payload["set"]["grn"]],
            expected["overlays"],
        )
        self.assertEqual(payload["target"], "00ff00")

    def test_generic_rejected(self):
        with self.assertRaises(mock_server.HttpError) as raised:
            self.snap(gid="3", rgb="ffffff")
        self.assertEqual(raised.exception.code, 400)
        self.assertIn("no colour model", raised.exception.message)

    def test_bad_rgb_rejected(self):
        for value in ("", "fff", "gggggg", "#1234567", "##12345"):
            with self.assertRaises(mock_server.HttpError) as raised:
                self.snap(gid="2", rgb=value)
            self.assertEqual(raised.exception.code, 400)

    def test_unknown_group(self):
        with self.assertRaises(mock_server.HttpError) as raised:
            self.snap(gid="9", rgb="ffffff")
        self.assertEqual(raised.exception.code, 404)

    def test_snap_is_get_only(self):
        self.assertIn("/klink/snap", mock_server.GET_ROUTES)
        self.assertNotIn("/klink/snap", mock_server.POST_ROUTES)


if __name__ == "__main__":
    unittest.main()
