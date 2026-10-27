#!/usr/bin/env python3
"""Build the static GitHub Pages demo of the K-Link page (stdlib only).

The demo runs tools/mock_server.py in the browser via Pyodide, so the page's own code is
unchanged: a small shell answers window.fetch with mock_server.dispatch(). This script writes:

    index.html   the demo shell (tools/demo/index.html)
    bundle.json  the files the mock needs under Pyodide, by virtual path
    .nojekyll    keep GitHub Pages from filtering the output

    python3 tools/build_demo.py                 # write _site/
    python3 tools/build_demo.py out_dir
    python3 -m http.server -d _site             # try it locally

Nothing generated is committed.
"""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEMO_HTML = ROOT / "tools" / "demo" / "index.html"
WEB_DIR = "components/klink/web"

# The virtual paths keep the repository layout: mock_server computes ROOT as the parent of its own
# folder and adds components/klink to sys.path, so the imports work in Pyodide's file system too.
BUNDLE_FILES = (
    "tools/mock_server.py",
    "components/klink/web_build.py",
    # web_build imports klink_colors; neither imports anything else from the component.
    "components/klink/klink_colors.py",
)


def bundle():
    """Return {virtual path: file text} for everything dispatch() needs under Pyodide."""
    files = {}
    for name in BUNDLE_FILES:
        files[name] = (ROOT / name).read_text(encoding="utf-8")
    for path in sorted((ROOT / WEB_DIR).iterdir()):
        if path.is_file():
            files[WEB_DIR + "/" + path.name] = path.read_text(encoding="utf-8")
    return files


def build(out_dir):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    (out / "index.html").write_text(DEMO_HTML.read_text(encoding="utf-8"), encoding="utf-8")
    (out / "bundle.json").write_text(
        json.dumps(bundle(), separators=(",", ":"), ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (out / ".nojekyll").write_text("", encoding="utf-8")
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build the static K-Link demo for GitHub Pages.")
    parser.add_argument("out_dir", nargs="?", default="_site", help="output directory (default _site)")
    args = parser.parse_args(argv)
    try:
        out = build(args.out_dir)
    except OSError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    print("demo: wrote %s (index.html, bundle.json, .nojekyll)" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
