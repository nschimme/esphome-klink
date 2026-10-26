#!/usr/bin/env python3
"""Build components/klink/klink_web_page.h from components/klink/web/index.html.

Stdlib only. The page is minified conservatively, gzipped, and emitted as a C++ byte array in the
same format the firmware expects. Run from anywhere:

    python3 tools/build_web.py

tools/klink_colors.py supplies the KCOL colour block, which is written back into the source between
the /*COLORS:BEGIN*/ and /*COLORS:END*/ markers so the committed page stays in sync. The gzipped
page must fit in 16000 bytes; the script prints the size and fails otherwise.
"""

import gzip
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "components" / "klink" / "web" / "index.html"
TARGET = ROOT / "components" / "klink" / "klink_web_page.h"
MAX_BYTES = 16000

sys.path.insert(0, str(ROOT / "tools"))
import klink_colors  # noqa: E402

COLORS_BEGIN = "/*COLORS:BEGIN*/"
COLORS_END = "/*COLORS:END*/"

# Start/end of the regions whose contents must be left alone when stripping HTML comments. HTML
# comments cannot appear inside <script> or <style>, and their contents may contain string literals
# that look like comments.
OPEN_TAG = re.compile(r"<(script|style)\b[^>]*>", re.IGNORECASE)
CLOSE_TAG = re.compile(r"</(script|style)\s*>", re.IGNORECASE)


def strip_html_comments(text):
    out = []
    i = 0
    n = len(text)
    inside = None
    while i < n:
        if inside is None:
            if text.startswith("<!--", i):
                end = text.find("-->", i + 4)
                i = n if end < 0 else end + 3
                continue
            match = OPEN_TAG.match(text, i)
            if match:
                inside = match.group(1).lower()
                out.append(match.group(0))
                i = match.end()
                continue
            out.append(text[i])
            i += 1
        else:
            match = CLOSE_TAG.match(text, i)
            if match and match.group(1).lower() == inside:
                inside = None
                out.append(match.group(0))
                i = match.end()
                continue
            out.append(text[i])
            i += 1
    return "".join(out)


WORD = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.$#-")


def compact(text, keep_calc=False):
    """Drop runs of whitespace where they are not needed, leaving strings alone.

    Only whitespace between two word characters is kept (plus a space before a token that starts
    with a backslash, so escapes stay separated). With keep_calc, whitespace around + and - is also
    kept, because CSS calc() requires it.
    """
    out = []
    quote = None
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if quote:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c in "'\"`":
            quote = c
            out.append(c)
            i += 1
            continue
        if c == "/" and not keep_calc and i + 1 < n and text[i + 1] in "/*":
            if text[i + 1] == "*":
                end = text.find("*/", i + 2)
                i = n if end < 0 else end + 2
            else:
                end = text.find("\n", i + 2)
                i = n if end < 0 else end + 1
            continue
        if c.isspace():
            j = i
            while j < n and text[j].isspace():
                j += 1
            prev = out[-1] if out else ""
            nxt = text[j] if j < n else ""
            keep = prev in WORD and (nxt in WORD or nxt == "\\")
            if keep_calc and (prev in "+-" or nxt in "+-"):
                keep = True
            if prev and nxt and keep:
                out.append(" ")
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def minify(text):
    text = strip_html_comments(text)
    lines = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            if lines and lines[-1]:
                lines.append("")
            continue
        lines.append(line)
    while lines and not lines[-1]:
        lines.pop()
    text = "\n".join(lines) + "\n"
    for tag, keep_calc in (("style", True), ("script", False)):
        pattern = re.compile(r"(<%s>)(.*?)(</%s>)" % (tag, tag), re.DOTALL)
        text = pattern.sub(lambda m: m.group(1) + compact(m.group(2), keep_calc) + m.group(3), text)
    return text


def colors_js():
    """The generated KCOL block, kept in sync with tools/klink_colors.py."""
    def quote(values):
        return "[" + ",".join("'%s'" % value for value in values) + "]"

    overlays = ",".join("%s:'%s'" % (label, value) for label, value in klink_colors.OVERLAYS.items())
    return "var KCOL={sun:%s,blue:%s,ov:{%s}};" % (
        quote(klink_colors.SUN_STOPS),
        quote(klink_colors.BLUE_STOPS),
        overlays,
    )


def inject_colors(text):
    """Replace the text between the COLORS markers and return the new page source."""
    start = text.find(COLORS_BEGIN)
    if start < 0:
        print("error: %s is missing from %s" % (COLORS_BEGIN, SOURCE), file=sys.stderr)
        return None
    end = text.find(COLORS_END, start)
    if end < 0:
        print("error: %s is missing from %s" % (COLORS_END, SOURCE), file=sys.stderr)
        return None
    head = start + len(COLORS_BEGIN)
    return text[:head] + "\n" + colors_js() + "\n" + text[end:]


def format_header(payload):
    rows = []
    for offset in range(0, len(payload), 16):
        chunk = payload[offset:offset + 16]
        rows.append("    " + " ".join("0x%02x," % byte for byte in chunk))
    return (
        "#pragma once\n"
        "// Generated by tools/build_web.py from components/klink/web/index.html. Do not edit by hand.\n"
        "\n"
        '#include "esphome/core/defines.h"\n'
        "#ifdef USE_KLINK_WEB\n"
        "\n"
        '#include "esphome/core/hal.h"\n'
        "\n"
        "namespace esphome {\n"
        "namespace klink {\n"
        "\n"
        "constexpr uint8_t KLINK_PAGE[] PROGMEM = {\n"
        + "\n".join(rows)
        + "\n};\n"
        "constexpr size_t KLINK_PAGE_LEN = sizeof(KLINK_PAGE);\n"
        "\n"
        "}  // namespace klink\n"
        "}  // namespace esphome\n"
        "\n"
        "#endif  // USE_KLINK_WEB\n"
    )


def main():
    if not SOURCE.is_file():
        print("missing source: %s" % SOURCE, file=sys.stderr)
        return 1
    raw = SOURCE.read_text(encoding="utf-8")
    updated = inject_colors(raw)
    if updated is None:
        return 1
    if updated != raw:
        # Keep the committed source and the raw page the mock server serves in sync.
        SOURCE.write_text(updated, encoding="utf-8")
        raw = updated
    page = minify(raw).encode("utf-8")
    payload = gzip.compress(page, 9, mtime=0)
    TARGET.write_text(format_header(payload), encoding="utf-8")
    print("web page: %d bytes source, %d bytes gzipped -> %s" % (len(page), len(payload), TARGET.relative_to(ROOT)))
    if len(payload) > MAX_BYTES:
        print("error: gzipped page is %d bytes (limit %d)" % (len(payload), MAX_BYTES), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
