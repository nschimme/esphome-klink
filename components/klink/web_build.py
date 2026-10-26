#!/usr/bin/env python3
"""Assemble and gzip the esphome-klink web page (stdlib only).

The page sources live in components/klink/web/: index.html, style.css and app.js. They are kept
apart so the folder can be opened in a browser during development. At compile time ESPHome calls
page_gzip(), which inlines the CSS and JS into one page and gzips it; nothing generated is
committed.

    python3 components/klink/web_build.py            # print source and gzip sizes
    python3 components/klink/web_build.py --check    # same, fail if the gzip is too large
    python3 components/klink/web_build.py --write-html out.html
"""

import argparse
import gzip
import re
import sys
from pathlib import Path

try:
    from . import klink_colors
except ImportError:  # running as a script, or loaded as a top-level module
    import klink_colors

WEB_DIR = Path(__file__).parent / "web"

MAX_GZIP_BYTES = 24000

STYLE_LINK = '<link rel="stylesheet" href="style.css">'
SCRIPT_TAG = '<script src="app.js"></script>'
COLOR_MARKER = "/*KCOL*/"

# Applied to every source: trailing whitespace goes, and blank lines at the very start/end.
_WS = re.compile(r"[ \t]+$", re.MULTILINE)


def _clean(text):
    """Strip trailing whitespace and blank lines at the start/end; keep everything else."""
    text = _WS.sub("", text)
    lines = text.splitlines()
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(lines) + "\n"


def colors_js():
    """The generated colour block, kept in sync with klink_colors."""

    def quote(values):
        return "[" + ",".join("'%s'" % value for value in values) + "]"

    overlays = ",".join(
        "%s:'%s'" % (label, value) for label, value in klink_colors.OVERLAYS.items()
    )
    return "var KCOL={sun:%s,blue:%s,ov:{%s}};" % (
        quote(klink_colors.SUN_STOPS),
        quote(klink_colors.BLUE_STOPS),
        overlays,
    )


def _read(name):
    path = WEB_DIR / name
    if not path.is_file():
        raise ValueError("missing web source: %s" % path)
    return path.read_text(encoding="utf-8")


def assemble():
    """Return the single-file page: CSS and JS inlined, colour block filled in."""
    html = _clean(_read("index.html"))
    css = _clean(_read("style.css"))
    js = _clean(_read("app.js"))

    if "</script" in js.lower():
        raise ValueError("app.js must not contain </script")
    if COLOR_MARKER not in js:
        raise ValueError("app.js is missing the %s marker" % COLOR_MARKER)
    js = js.replace(COLOR_MARKER, colors_js())

    if STYLE_LINK not in html:
        raise ValueError("index.html is missing %s" % STYLE_LINK)
    if SCRIPT_TAG not in html:
        raise ValueError("index.html is missing %s" % SCRIPT_TAG)

    html = html.replace(STYLE_LINK, "<style>\n%s</style>" % css, 1)
    html = html.replace(SCRIPT_TAG, "<script>\n%s</script>" % js, 1)
    return html


def page_gzip():
    """The page as gzip bytes, byte-for-byte reproducible."""
    return gzip.compress(assemble().encode("utf-8"), 9, mtime=0)


def _sizes():
    source = assemble().encode("utf-8")
    payload = gzip.compress(source, 9, mtime=0)
    return source, payload


def main(argv=None):
    parser = argparse.ArgumentParser(description="Assemble and gzip the esphome-klink page.")
    parser.add_argument(
        "--check",
        action="store_true",
        help="exit non-zero if the gzipped page is larger than MAX_GZIP_BYTES",
    )
    parser.add_argument("--write-html", metavar="PATH", help="write the assembled page to PATH")
    args = parser.parse_args(argv)

    try:
        source, payload = _sizes()
    except (OSError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1

    print("web page: %d bytes source, %d bytes gzipped (limit %d)" % (len(source), len(payload), MAX_GZIP_BYTES))
    if args.write_html:
        Path(args.write_html).write_text(assemble(), encoding="utf-8")
        print("wrote %s" % args.write_html)
    if args.check and len(payload) > MAX_GZIP_BYTES:
        print(
            "error: gzipped page is %d bytes (limit %d)" % (len(payload), MAX_GZIP_BYTES),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
