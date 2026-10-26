#!/usr/bin/env python3
"""Tests for components/klink/web_build.py (stdlib unittest).

Run: python3 tests/test_web_build.py
"""

import gzip
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "components" / "klink"))
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


if __name__ == "__main__":
    unittest.main()
