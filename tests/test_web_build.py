#!/usr/bin/env python3
"""Tests for components/klink/web_build.py (stdlib unittest).

Run: python3 tests/test_web_build.py
"""

import copy
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


class LightProgramTests(unittest.TestCase):
    """The mock's light programs: state shape, refused lamp routes and accepted program routes."""

    def setUp(self):
        self.saved = (copy.deepcopy(mock_server.GROUPS), copy.deepcopy(mock_server.LAMPS), dict(mock_server.BUS))

    def tearDown(self):
        mock_server.GROUPS[:] = self.saved[0]
        mock_server.LAMPS[:] = self.saved[1]
        mock_server.BUS.update(self.saved[2])

    def by_gid(self, gid):
        for group in mock_server.state_json()["groups"]:
            if group["gid"] == gid:
                return group
        return None

    def test_default_state_has_rgb_light(self):
        group = self.by_gid(101)
        self.assertIsNotNone(group, "the default mock state must include program 101")
        self.assertEqual(group["kind"], "light")
        self.assertEqual(group["name"], "Board LED")
        self.assertEqual(group["model"], "light_rgb")
        self.assertEqual(group["labels"], ["int", None, "r", "g", "b"])
        self.assertEqual(group["lamps"], [])
        self.assertEqual(group["defaults"], group["slots"])
        self.assertEqual(len(group["slots"]), 8)
        # A sensible RGB program: every enabled point has some colour while it is bright.
        for p in group["slots"]:
            if p["enabled"] and p["ch"][0] > 0:
                self.assertGreater(max(p["ch"][2], p["ch"][3], p["ch"][4]), 0)

    def test_no_ct_light_in_default_state(self):
        self.assertIsNone(self.by_gid(102))

    def test_refuses_lamp_routes_for_program_ids(self):
        with self.assertRaises(mock_server.HttpError) as raised:
            mock_server.handle_identify({"gid": ["101"]})
        self.assertEqual(raised.exception.code, 400)
        self.assertEqual(raised.exception.message, "not a lamp group")
        with self.assertRaises(mock_server.HttpError) as raised:
            mock_server.handle_assign({"kid": ["18000001"], "gid": ["101"]})
        self.assertEqual(raised.exception.code, 400)
        self.assertEqual(raised.exception.message, "not a lamp group")
        with self.assertRaises(mock_server.HttpError) as raised:
            mock_server.handle_snap({"gid": ["101"], "rgb": ["ffffff"]})
        self.assertEqual(raised.exception.code, 400)
        self.assertIn("no colour model", raised.exception.message)

    def test_accepts_program_routes(self):
        mock_server.handle_mode({"gid": ["101"], "mode": ["off"]})
        self.assertEqual(self.by_gid(101)["mode"], "off")
        mock_server.handle_mode({"gid": ["101"], "mode": ["program"]})
        mock_server.handle_point({"gid": ["101"], "slot": ["1"], "c3": ["10"], "c4": ["20"], "c5": ["30"]})
        self.assertEqual(self.by_gid(101)["slots"][0]["ch"][2:5], [10, 20, 30])
        mock_server.handle_preview({"gid": ["101"], "minute": ["600"]})
        mock_server.handle_preview_done({"gid": ["101"]})
        mock_server.handle_copy({"from": ["101"], "to": ["101"]})
        self.assertEqual(self.by_gid(101)["lamps"], [])

    def test_bad_program_id_rejected(self):
        for value in ("0", "41", "100", "165", "abc"):
            with self.assertRaises(mock_server.HttpError) as raised:
                mock_server.handle_mode({"gid": [value], "mode": ["off"]})
            self.assertEqual(raised.exception.code, 400)


class MockEffectTests(unittest.TestCase):
    """POST /klink/effect and /klink/effect/stop: an overlay that never changes the mode."""

    SUN = ["Storm", "Cloud", "Aurora", "Pulse", "Color sweep", "Sunrise", "Sunset"]
    BLUE = [
        "Storm",
        "Cloud",
        "Rainbow",
        "Aurora",
        "Moon Red",
        "Moon Blue",
        "Pulse",
        "Color sweep",
        "Sunrise",
        "Sunset",
    ]

    def setUp(self):
        self.saved = mock_server.GROUPS[:]
        mock_server.GROUPS[:] = [
            mock_server.make_group(2, "Tuna Sun", "tuna_sun", ["int", "clr", None, "r", "am"], []),
            mock_server.make_group(1, "Tuna Blue", "tuna_blue", ["int", "clr", "vio", "r", "grn"], []),
            mock_server.make_group(101, "Board LED", "light_rgb", ["int", None, "r", "g", "b"], [], kind="light"),
        ]

    def tearDown(self):
        mock_server.GROUPS[:] = self.saved

    def group(self, gid):
        return [group for group in mock_server.state_json()["groups"] if group["gid"] == gid][0]

    def start(self, **query):
        mock_server.handle_effect({key: [value] for key, value in query.items()})

    def test_effect_names_per_model(self):
        self.assertEqual(self.group(2)["effects"], self.SUN)
        self.assertEqual(self.group(1)["effects"], self.BLUE)
        self.assertEqual(self.group(101)["effects"], [])

    def test_idle_defaults(self):
        for gid in (2, 1, 101):
            group = self.group(gid)
            self.assertIsNone(group["effect"])
            self.assertEqual(group["effect_speed"], 0)
            self.assertEqual(group["effect_minutes"], 2.0)

    def test_start_keeps_mode_and_sets_state(self):
        self.start(gid="2", name="Storm", speed="slow", minutes="1")
        group = self.group(2)
        self.assertEqual(group["mode"], "program")
        self.assertEqual(group["effect"]["name"], "Storm")
        self.assertGreaterEqual(group["effect"]["remaining"], 55)
        self.assertLessEqual(group["effect"]["remaining"], 60)
        self.assertEqual(group["effect_speed"], 2)
        self.assertEqual(group["effect_minutes"], 1.0)
        self.assertTrue(group["status"].startswith("Effect: Storm"))
        self.assertIn("left", group["status"])

    def test_until_stopped(self):
        self.start(gid="2", name="Aurora", minutes="0")
        group = self.group(2)
        self.assertEqual(group["effect"]["remaining"], -1)
        self.assertEqual(group["status"], "Effect: Aurora")

    def test_options_default_to_the_group_settings(self):
        self.start(gid="2", name="Storm")
        group = self.group(2)
        self.assertEqual(group["effect"], {"name": "Storm", "remaining": 120})
        self.assertEqual([group["effect_speed"], group["effect_minutes"]], [0, 2.0])

    def test_stop(self):
        self.start(gid="1", name="Rainbow", minutes="5")
        self.assertIsNotNone(self.group(1)["effect"])
        mock_server.handle_effect_stop({"gid": ["1"]})
        group = self.group(1)
        self.assertIsNone(group["effect"])
        self.assertEqual(group["mode"], "program")
        self.assertNotIn("Effect:", group["status"])

    def test_effect_expires_by_itself(self):
        self.start(gid="2", name="Storm", minutes="0.5")
        mock_server.GROUPS[0]["effect_end"] = mock_server.time.time() - 0.01
        group = self.group(2)
        self.assertIsNone(group["effect"])
        self.assertEqual(group["mode"], "program")

    def test_unknown_group_and_effect(self):
        with self.assertRaises(mock_server.HttpError) as raised:
            self.start(gid="9", name="Storm")
        self.assertEqual((raised.exception.code, raised.exception.message), (404, "unknown group"))
        with self.assertRaises(mock_server.HttpError) as raised:
            self.start(gid="2", name="Disco")
        self.assertEqual((raised.exception.code, raised.exception.message), (404, "unknown effect"))
        # A light program has effects on the light, but none are offered here.
        with self.assertRaises(mock_server.HttpError) as raised:
            self.start(gid="101", name="Storm")
        self.assertEqual(raised.exception.code, 404)

    def test_bad_parameters(self):
        with self.assertRaises(mock_server.HttpError) as raised:
            self.start(gid="2")
        self.assertEqual((raised.exception.code, raised.exception.message), (400, "missing parameter: name"))
        for speed in ("warp", "", "Fast!"):
            with self.assertRaises(mock_server.HttpError) as raised:
                self.start(gid="2", name="Storm", speed=speed)
            self.assertEqual((raised.exception.code, raised.exception.message), (400, "invalid parameter: speed"))
        for minutes in ("6", "-1", "1.25", "abc", ""):
            with self.assertRaises(mock_server.HttpError) as raised:
                self.start(gid="2", name="Storm", minutes=minutes)
            self.assertEqual((raised.exception.code, raised.exception.message), (400, "invalid parameter: minutes"))
        for minutes in ("0", "0.5", "5"):
            mock_server.handle_effect({"gid": ["2"], "name": ["Storm"], "minutes": [minutes]})
            self.assertIsNotNone(self.group(2)["effect"])

    def test_effect_routes_are_post_only(self):
        for path in ("/klink/effect", "/klink/effect/stop"):
            self.assertIn(path, mock_server.POST_ROUTES)
            self.assertIn(path, mock_server.ROUTES)
            self.assertNotIn(path, mock_server.GET_ROUTES)


class MockManyTests(unittest.TestCase):
    """The --many laboratory, checked through the mock's /klink/state handler."""

    def test_many_state(self):
        mock_server.use_many()
        state = mock_server.state_json()
        gids = sorted(group["gid"] for group in state["groups"])
        self.assertEqual(gids, list(range(1, 11)) + [101, 102])
        self.assertEqual(len(state["lamps"]), 32)
        self.assertEqual(state["bus"]["known_lamps"], 32)
        known = set(gids)
        orphans = sorted({lamp["gid"] for lamp in state["lamps"] if lamp["gid"] not in known})
        self.assertEqual(orphans, [11, 12, 20])
        for group in state["groups"]:
            in_group = sorted(lamp["kid"] for lamp in state["lamps"] if lamp["gid"] == group["gid"])
            self.assertEqual(sorted(group["lamps"]), in_group)

    def test_many_has_ct_light(self):
        mock_server.use_many()
        state = mock_server.state_json()
        ct = [group for group in state["groups"] if group["gid"] == 102][0]
        self.assertEqual(ct["kind"], "light")
        self.assertEqual(ct["model"], "light_ct")
        self.assertEqual(ct["labels"], ["int", "ct", None, None, None])
        self.assertEqual(ct["lamps"], [])
        self.assertEqual(ct["defaults"], ct["slots"])
        self.assertEqual(len(ct["slots"]), 8)

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
