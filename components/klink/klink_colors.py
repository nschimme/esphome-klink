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

# 17 evenly spaced stops over CLR 0..255 (CLR 0,16,...,240,255). CLR 0 is the warm end (6,000 K),
# CLR 255 the cool end (9,000 K). Between the ends we interpolate linearly in mireds.
SUN_TEMPS = (6000.0, 9000.0)
SUN_STOPS = []

# A rough blue ramp, deep blue to light blue, interpolated linearly in RGB. Uncalibrated guess.
BLUE_STOPS = []

# Channel overlays applied on top of the CLR base tint, in the group's label order.
OVERLAYS = {"r": "#ff2a1a", "am": "#ffa000", "vio": "#7a3cff", "grn": "#22d36b"}

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
