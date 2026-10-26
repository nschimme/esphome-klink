#!/usr/bin/env python3
"""Colour model for the esphome-klink page (stdlib only).

The values here are our own approximations of the light a lamp makes. They are rough on purpose:
BLUE_STOPS in particular is a plain blue ramp that still needs calibration against real lamps.
Nothing is taken from Kessil's website, images or code.

Used by components/klink/web_build.py to generate the KCOL block in the page and by
tests/test_klink_colors.py as the reference implementation.

Run tests with: python3 tests/test_klink_colors.py
"""

import math
import sys

# 17 evenly spaced stops over CLR 0..255 (CLR 0,16,...,240,255). CLR 0 is the warm end (6,000 K),
# CLR 255 the cool end (9,000 K). Between the ends we interpolate linearly in mireds.
SUN_TEMPS = (6000.0, 9000.0)
SUN_STOPS = []

# A rough blue ramp, deep blue to light blue, interpolated linearly in RGB. Uncalibrated guess.
BLUE_STOPS = []

# Channel overlays applied on top of the CLR base tint, in the group's label order.
# The light_* labels (g, b, ct, w) are not part of any Kessil colour model; the page uses them
# for its own slider tracks and legend lines, and grn/g may share the same green.
OVERLAYS = {
    "r": "#ff2a1a",
    "am": "#ffa000",
    "vio": "#7a3cff",
    "grn": "#22d36b",
    "g": "#22d36b",
    "b": "#2a5cff",
    "ct": "#ffb46b",
    "w": "#e4ebf1",
}

# CLR used when a group has no CLR channel (generic model, not rendered): the middle of the axis.
MID_CLR = 128


def _hex_to_rgb(value):
    value = value.lstrip("#")
    return [int(value[i:i + 2], 16) for i in (0, 2, 4)]


def _rgb_to_hex(rgb):
    return "#" + "".join("%02x" % max(0, min(255, int(round(v)))) for v in rgb)


def _kelvin_rgb(kelvin):
    """Tanner Helland's widely known blackbody approximation, clamped to 0..255."""
    t = kelvin / 100.0
    if t <= 66:
        red = 255.0
        green = 99.4708025861 * math.log(t) - 161.1195681661
    else:
        red = 329.698727446 * ((t - 60) ** -0.1332047592)
        green = 288.1221695283 * ((t - 60) ** -0.0755148492)
    if t >= 66:
        blue = 255.0
    elif t <= 19:
        blue = 0.0
    else:
        blue = 138.5177312231 * math.log(t - 10) - 305.0447927307
    return [max(0.0, min(255.0, red)), max(0.0, min(255.0, green)), max(0.0, min(255.0, blue))]


def _mired_kelvin(clr):
    warm, cool = 1e6 / SUN_TEMPS[0], 1e6 / SUN_TEMPS[1]
    return 1e6 / (warm + (cool - warm) * clr / 255.0)


def _stop_clr(index, count=17):
    return 255.0 if index == count - 1 else 255.0 * index / (count - 1)


def _build():
    for i in range(17):
        SUN_STOPS.append(_rgb_to_hex(_kelvin_rgb(_mired_kelvin(_stop_clr(i)))))
    deep, light = _hex_to_rgb("#1d4ed8"), _hex_to_rgb("#5ec8ff")
    for i in range(17):
        t = _stop_clr(i) / 255.0
        BLUE_STOPS.append(_rgb_to_hex([a + (b - a) * t for a, b in zip(deep, light)]))


_build()


def _stops_for(model):
    if model == "tuna_sun":
        return SUN_STOPS
    if model == "tuna_blue":
        return BLUE_STOPS
    raise ValueError("no colour axis for model %r" % (model,))


def _stop_rgb(stops, clr):
    """Linear interpolation between the 17 stops, all in float RGB."""
    clr = MID_CLR if clr is None else max(0.0, min(255.0, float(clr)))
    n = len(stops) - 1
    f = clr / 255.0 * n
    i = int(f)
    t = f - i
    a, b = _hex_to_rgb(stops[i]), _hex_to_rgb(stops[min(n, i + 1)])
    return [x + (y - x) * t for x, y in zip(a, b)]


def swatch(model, clr, overlays=None):
    """Approximate lamp colour as #rrggbb.

    overlays maps a channel label (r, am, vio, grn) to its percent (0..100); entries are applied in
    insertion order, matching the order of the group's labels in the web page.
    """
    rgb = _stop_rgb(_stops_for(model), clr)
    for label, value in (overlays or {}).items():
        overlay = _hex_to_rgb(OVERLAYS[label])
        fraction = 0.6 * max(0.0, min(100.0, float(value))) / 100.0
        rgb = [base + (top - base) * fraction for base, top in zip(rgb, overlay)]
    return _rgb_to_hex(rgb)


# ---------------------------------------------------------------------------
# Integer reference model.
#
# The firmware needs one nearest-colour search and the page's mock server, tests and golden vectors
# need the same answer, so the arithmetic below is integer only and is duplicated line by line in
# components/klink/klink_colors.h. The generated tables (`--emit-cpp`) and golden vectors
# (`--emit-golden`) come from this file; nothing generated is committed.

# Overlay channels per model, in the group's label order: (channel index, label).
CHANNELS = {
    "tuna_sun": [(3, "r"), (4, "am")],
    "tuna_blue": [(2, "vio"), (3, "r"), (4, "grn")],
}

# Highest number of swatch+dist evaluations nearest_i() may make for any target.
MAX_EVALS = 700

_CLR_STEPS = (16, 8, 4, 2, 1)
_OV_STEPS = (20, 10, 5, 2, 1)


def int_stops(model):
    """The 17 hex stops of `model` as (r, g, b) integer tuples."""
    return [tuple(_hex_to_rgb(value)) for value in _stops_for(model)]


def _overlay_rgb(model):
    return [_hex_to_rgb(OVERLAYS[label]) for _, label in CHANNELS[model]]


def swatch_i(model, clr, overlays=None):
    """Integer twin of swatch(): returns an (r, g, b) tuple instead of a hex string.

    `clr` is 0..255 and `overlays` a list of 0..100 values in the model's overlay order. There is
    no clamping: out-of-range inputs are caller errors (nearest_i keeps them in range).
    """
    stops = int_stops(model)
    values = list(overlays or [])
    num = int(clr) * 16
    i = num // 255
    if i >= 16:
        i = 15
    frac = num - i * 255
    out = [0, 0, 0]
    for c in range(3):
        a, b = stops[i][c], stops[i + 1][c]
        out[c] = (a * (255 - frac) + b * frac + 127) // 255
    for index, rgb in enumerate(_overlay_rgb(model)):
        value = int(values[index]) if index < len(values) else 0
        k = 60 * value
        for c in range(3):
            out[c] = (out[c] * (10000 - k) + rgb[c] * k + 5000) // 10000
    return tuple(out)


def dist_i(a, b):
    """Redmean squared distance between two (r, g, b) colours, integer only."""
    rm = (int(a[0]) + int(b[0])) // 2
    dr = int(a[0]) - int(b[0])
    dg = int(a[1]) - int(b[1])
    db = int(a[2]) - int(b[2])
    return ((512 + rm) * dr * dr >> 8) + 4 * dg * dg + ((767 - rm) * db * db >> 8)


def nearest_i(model, target):
    """Find the CLR and overlay settings whose swatch_i() is closest to `target`.

    Returns {"clr", "overlays", "rgb", "dist", "evals"}. Coarse scan over CLR with overlays 0,
    then a coordinate descent from large to small steps; the first strictly improving candidate
    wins (+ before -). Must stay identical to nearest_i() in klink_colors.h.
    """
    n_ov = len(CHANNELS[model])
    target = (int(target[0]), int(target[1]), int(target[2]))
    evals = 0

    def evaluate(clr, overlays):
        nonlocal evals
        rgb = swatch_i(model, clr, overlays)
        evals += 1
        return rgb, dist_i(rgb, target)

    # Stage 1: coarse CLR scan (0, 8, ..., 248, 255), overlays 0.
    best_dist = None
    best_clr = 0
    best_ov = [0] * n_ov
    best_rgb = (0, 0, 0)
    for clr in list(range(0, 249, 8)) + [255]:
        rgb, distance = evaluate(clr, [0] * n_ov)
        if best_dist is None or distance < best_dist:
            best_dist, best_clr, best_rgb = distance, clr, rgb

    # Stage 2: coordinate descent over [clr, ov0, ov1, ...].
    values = [best_clr] + [0] * n_ov
    for s in range(5):
        for _pass in range(8):
            accepted = False
            for vi in range(1 + n_ov):
                step = _CLR_STEPS[s] if vi == 0 else _OV_STEPS[s]
                high = 255 if vi == 0 else 100
                current = values[vi]
                for sign in (1, -1):
                    candidate = current + sign * step
                    if candidate < 0:
                        candidate = 0
                    elif candidate > high:
                        candidate = high
                    if candidate == current:
                        continue
                    trial = list(values)
                    trial[vi] = candidate
                    rgb, distance = evaluate(trial[0], trial[1:])
                    if distance < best_dist:
                        values[vi] = candidate
                        best_dist = distance
                        best_clr = values[0]
                        best_ov = list(values[1:])
                        best_rgb = rgb
                        accepted = True
                        break
            if not accepted:
                break

    return {"clr": best_clr, "overlays": best_ov, "rgb": best_rgb, "dist": best_dist, "evals": evals}


def _fmt_color(rgb):
    return "{%d, %d, %d}" % (rgb[0], rgb[1], rgb[2])


def emit_cpp(firmware=False):
    """Text of the generated colour tables (nothing generated is committed).

    With firmware=False this is the standalone `klink_color_tables.h` used by the host tests:
    a guarded header that includes klink_colors.h and wraps the tables in namespace kessil::klink.

    With firmware=True it returns only the definitions (no `#pragma once`, no `#include`) inside
    `namespace esphome { namespace klink {`, ready to be emitted as a global after esphome.h:
    klink_web.h declares COLOR_SUN/COLOR_BLUE `extern` already, so these definitions get external
    linkage.
    """
    if firmware:
        lines = ["namespace esphome { namespace klink {", ""]
    else:
        lines = [
            "#pragma once",
            "// Generated by components/klink/klink_colors.py --emit-cpp. Do not edit.",
            '#include "klink_colors.h"',
            "",
            "namespace kessil {",
            "namespace klink {",
            "",
        ]
    for model, macro in (("tuna_sun", "SUN"), ("tuna_blue", "BLUE")):
        channels = CHANNELS[model]
        lines.append("static const uint8_t COLOR_%s_STOPS[17][3] = {" % macro)
        for rgb in int_stops(model):
            lines.append("    %s," % _fmt_color(rgb))
        lines.append("};")
        lines.append("")
        lines.append(
            "static const uint8_t COLOR_%s_OVERLAY_CHANNEL[%d] = {%s};"
            % (macro, len(channels), ", ".join(str(ch) for ch, _ in channels))
        )
        lines.append("static const uint8_t COLOR_%s_OVERLAY_RGB[%d][3] = {" % (macro, len(channels)))
        for rgb in _overlay_rgb(model):
            lines.append("    %s," % _fmt_color(rgb))
        lines.append("};")
        lines.append("")
        if firmware:
            lines.append(
                "const kessil::klink::ColorSpec COLOR_%s = {COLOR_%s_STOPS, %d, "
                "COLOR_%s_OVERLAY_CHANNEL, COLOR_%s_OVERLAY_RGB};"
                % (macro, macro, len(channels), macro, macro)
            )
        else:
            lines.append(
                "static const ColorSpec COLOR_%s = {COLOR_%s_STOPS, %d, COLOR_%s_OVERLAY_CHANNEL, "
                "COLOR_%s_OVERLAY_RGB};" % (macro, macro, len(channels), macro, macro)
            )
        lines.append("")
    if firmware:
        lines.append("}}  // namespace esphome::klink")
    else:
        lines.append("}  // namespace klink")
        lines.append("}  // namespace kessil")
    return "\n".join(lines) + "\n"


_PRIMARY_TARGETS = [
    (0, 0, 0),
    (255, 255, 255),
    (255, 0, 0),
    (0, 255, 0),
    (0, 0, 255),
    (255, 255, 0),
    (0, 255, 255),
    (255, 0, 255),
    (128, 128, 128),
    (255, 128, 0),
    (128, 0, 255),
    (0x10, 0x20, 0x30),
]


def _golden_targets(model):
    targets = list(int_stops(model)) + list(_PRIMARY_TARGETS)
    x = 42
    for _ in range(40):
        rgb = []
        for _ in range(3):
            x = (x * 1103515245 + 12345) & 0x7FFFFFFF
            rgb.append((x >> 16) & 255)
        targets.append(tuple(rgb))
    return targets


def emit_golden():
    """Golden vectors: one line per model and target, recomputed by the C++ test."""
    lines = []
    for model in ("tuna_sun", "tuna_blue"):
        for target in _golden_targets(model):
            result = nearest_i(model, target)
            overlays = list(result["overlays"]) + [-1] * (3 - len(result["overlays"]))
            rgb = result["rgb"]
            lines.append(
                "%s %d %d %d %d %d %d %d %d %d %d %d %d"
                % (
                    model,
                    target[0],
                    target[1],
                    target[2],
                    result["clr"],
                    overlays[0],
                    overlays[1],
                    overlays[2],
                    rgb[0],
                    rgb[1],
                    rgb[2],
                    result["dist"],
                    result["evals"],
                )
            )
    return "\n".join(lines) + "\n"


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    if args == ["--emit-cpp"]:
        sys.stdout.write(emit_cpp())
        return 0
    if args == ["--emit-golden"]:
        sys.stdout.write(emit_golden())
        return 0
    sys.stdout.write("usage: klink_colors.py --emit-cpp | --emit-golden\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
