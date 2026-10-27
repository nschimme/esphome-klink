#!/usr/bin/env python3
"""In-memory mock of the K-Link web API for developing the page without hardware.

Serves the assembled (not gzipped) page at http://127.0.0.1:8080/klink and implements every
route from components/klink/klink_web.cpp with fixed state. The page is re-assembled from
components/klink/web/ on every request, so edits show up on reload. Stdlib only:

    python3 tools/mock_server.py            # port 8080
    python3 tools/mock_server.py 9000       # another port
    python3 tools/mock_server.py --slow     # ~150 ms latency per request
    python3 tools/mock_server.py --no-location  # the Sky program asks for a location

Mock-only helpers (not part of the firmware API):
    POST /klink/_mock/writes?on=0|1         toggle "Send to lights" for the API
"""

import argparse
import json
import math
import re
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

sys.path.insert(0, str(ROOT / "components" / "klink"))
import klink_colors  # noqa: E402
import web_build  # noqa: E402

NUM_CHANNELS = 5
CHANNEL_MAX = [255, 255, 100, 100, 100]
# The Refugium's color (UI2) stops at 240.
CHANNEL_MAX_REFUGIUM = [255, 240, 100, 100, 100]
MIN_GID, MAX_GID = 1, 40
PROGRAM_ID_MIN, PROGRAM_ID_MAX = 101, 164
CUSTOM_GID_MIN, CUSTOM_GID_MAX = 3, 25
NUM_SLOTS = 8
PREVIEW_SECONDS = 60.0
SCAN_SECONDS = 2.5
NAME_MAX_LEN = 24

GET_ROUTES = ("/klink", "/klink/state", "/klink/snap", "/klink/generated")

COLOR_MODELS = ("tuna_sun", "tuna_blue", "refugium")

# Sky program defaults for bus groups. The generated days below are illustrative, not
# astronomy: they only need to move with the date and stay in a sensible range for the page.
SKY_DEFAULTS = {"layers": 3, "peak": 60, "hours": 0, "glow": 50, "ramp": 150, "amber": 0, "shift": 0, "moon": 5, "phase": 1}
LOCATION = {"lat": 41.88, "lon": -87.63, "source": "Home Assistant"}

# The effects each model offers (the light's own effect list, without "None").
EFFECTS = {
    "tuna_sun": ["Storm", "Cloud", "Aurora", "Pulse", "Flicker"],
    "tuna_blue": [
        "Storm",
        "Cloud",
        "Rainbow",
        "Aurora",
        "Moon Red",
        "Moon Blue",
        "Pulse",
        "Flicker",
    ],
    "refugium": ["Storm", "Cloud", "Aurora", "Pulse", "Flicker"],
    # A light program offers the effects every one of its lights can show: Cloud needs a color, Rainbow needs RGB.
    "light_brightness": ["Storm", "Aurora"],
    "light_ct": ["Storm", "Cloud", "Aurora"],
    "light_rgb": ["Storm", "Cloud", "Aurora", "Rainbow"],
    "light_rgbw": ["Storm", "Cloud", "Aurora", "Rainbow"],
}

EFFECT_SPEEDS = {"fast": 0, "moderate": 1, "slow": 2}

POST_ROUTES = (
    "/klink/mode",
    "/klink/identify",
    "/klink/point",
    "/klink/preview",
    "/klink/preview/done",
    "/klink/assign",
    "/klink/name",
    "/klink/groupname",
    "/klink/copy",
    "/klink/retry",
    "/klink/scan",
    "/klink/writes",
    "/klink/effect",
    "/klink/effect/stop",
    "/klink/output",
    "/klink/acclimation",
    "/klink/source",
    "/klink/sky",
    "/klink/source/copy",
)




def point(minute, channels, enabled=True):
    return {"minute": minute, "ch": list(channels), "enabled": enabled}


def copy_slots(slots):
    """A deep copy of slot dicts, so edits (which mutate in place) never touch the original."""
    return [point(p["minute"], p["ch"], p["enabled"]) for p in slots]


def has_sources_for(kind, model):
    """A bus group of the Tuna lamps has the Sky program; a Refugium and light programs do not."""
    return kind != "light" and model != "refugium"


def make_group(gid, name, model, labels, slots, kind="bus"):
    group = {
        "gid": gid,
        "kind": kind,
        "name": name,
        "model": model,
        "labels": list(labels),
        "max": list(CHANNEL_MAX_REFUGIUM if model == "refugium" else CHANNEL_MAX),
        "mode": "program",
        "before_preview": "program",
        "preview_until": 0.0,
        "selected": 0,
        "slots": slots,
        # The shipped program, captured before any edit, for the page's "Default" preset.
        "defaults": copy_slots(slots),
        # Effects: names this group offers, the one running (if any) and the runtime options. An
        # effect is a temporary overlay on the schedule; the mode never changes when one runs.
        "effects": list(EFFECTS.get(model, [])),
        "effect_name": None,
        "effect_end": 0.0,  # millis-like timestamp; 0.0 means "until stopped"
        "effect_speed": 0,  # 0 fast, 1 moderate, 2 slow
        "effect_minutes": 2.0,  # 0 = until stopped
        # Per-group output options; light programs drive ESPHome lights and have none.
        "output": (
            {"curve": 0, "seconds": 0.0, "soft_start": 30} if kind != "light" else None
        ),
        # Acclimation for every program: off means full light; turning it on starts at `start`.
        "acclimation": {"on": False, "start": 30, "days": 14, "share": 100},
    }
    if has_sources_for(kind, model):
        # Only a Tuna group has the Sky program.
        group["source"] = 0
        group["sky"] = dict(SKY_DEFAULTS)
    return group


LIGHT_RGB_SLOTS = [
    point(8 * 60, [20, 0, 100, 55, 20]),
    point(10 * 60 + 30, [140, 0, 100, 85, 60]),
    point(13 * 60, [220, 0, 100, 95, 80]),
    point(16 * 60, [180, 0, 90, 70, 55]),
    point(19 * 60, [80, 0, 100, 50, 25]),
    point(21 * 60 + 30, [10, 0, 100, 30, 10]),
    point(0, [0, 0, 0, 0, 0], False),
    point(0, [0, 0, 0, 0, 0], False),
]


def light_groups():
    """The light programs every mock run starts with."""
    return [
        make_group(
            101,
            "Board LED",
            "light_rgb",
            ["int", None, "r", "g", "b"],
            copy_slots(LIGHT_RGB_SLOTS),
            kind="light",
        ),
    ]


GROUPS = [
    make_group(
        2,
        "Tuna Sun",
        "tuna_sun",
        ["int", "clr", None, "r", "am"],
        [
            point(8 * 60, [30, 60, 0, 0, 0]),
            point(10 * 60 + 30, [150, 120, 0, 5, 15]),
            point(13 * 60, [210, 170, 0, 20, 35]),
            point(16 * 60, [170, 140, 0, 10, 25]),
            point(19 * 60, [70, 70, 0, 0, 10]),
            point(21 * 60 + 30, [8, 12, 0, 0, 0]),
            point(0, [0, 0, 0, 0, 0], False),
            point(0, [0, 0, 0, 0, 0], False),
        ],
    ),
    make_group(
        1,
        "Tuna Blue",
        "tuna_blue",
        ["int", "clr", "vio", "r", "grn"],
        [
            point(9 * 60, [40, 80, 20, 5, 10]),
            point(11 * 60, [180, 200, 50, 15, 25]),
            point(14 * 60, [220, 230, 70, 25, 40]),
            point(17 * 60, [190, 210, 60, 20, 30]),
            point(20 * 60, [90, 120, 30, 5, 10]),
            point(22 * 60, [10, 20, 5, 0, 0]),
            point(0, [0, 0, 0, 0, 0], False),
            point(0, [0, 0, 0, 0, 0], False),
        ],
    ),
    make_group(
        26,
        "Refugium",
        "refugium",
        ["int", "clr", None, "w", "grn"],
        [
            point(8 * 60, [0, 0, 0, 0, 0]),
            point(9 * 60, [100, 150, 0, 10, 0]),
            point(17 * 60, [100, 150, 0, 10, 0]),
            point(18 * 60, [0, 0, 0, 0, 0]),
            point(0, [0, 0, 0, 0, 0], False),
            point(0, [0, 0, 0, 0, 0], False),
            point(0, [0, 0, 0, 0, 0], False),
            point(0, [0, 0, 0, 0, 0], False),
        ],
    ),
] + light_groups()

MODEL_NAMES = {1: "A360X Tuna Blue", 2: "A360X Tuna Sun", 3: "A360X Refugium"}


def lamp_record(lamp):
    return {
        "kid": lamp["kid"],
        "type": lamp["type"],
        "gid": lamp["gid"],
        "default_gid": {1: 1, 2: 2, 3: 26}.get(lamp["type"], 0),
        "name": lamp["name"],
        "model": MODEL_NAMES.get(lamp["type"], ""),
        "firmware": "1.3.0",
    }


LAMPS = [
    {"kid": 18000001, "type": 2, "gid": 2, "name": ""},
    {"kid": 18000002, "type": 1, "gid": 1, "name": "Left"},
    {"kid": 18000003, "type": 1, "gid": 1, "name": "Right"},
    {"kid": 18000004, "type": 3, "gid": 26, "name": ""},
]

# Native ESPHome lights bound with `klink: followers:`. Empty in the leader demo: a leader and a
# follower are separate boards. Set this list (and `ROLE`) to preview the follower page.
FOLLOWERS = []
ROLE = "leader"

BUS = {"writes": True, "conflict": False, "scanning_until": 0.0, "scan_note": ""}

LOCK = threading.Lock()
SLOW_SECONDS = 0.0


# ---- validation helpers -----------------------------------------------------------------------


class HttpError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


def parse_uint(text, maximum):
    if not text or re.fullmatch(r"[0-9]+", text) is None:
        return None
    value = int(text)
    return value if value <= maximum else None


def require_uint(query, name, maximum):
    if name not in query:
        raise HttpError(400, "missing parameter: " + name)
    value = parse_uint(query[name][0], maximum)
    if value is None:
        raise HttpError(400, "invalid parameter: " + name)
    return value


def require_bool(query, name):
    if name not in query:
        raise HttpError(400, "missing parameter: " + name)
    value = query[name][0]
    if value in ("1", "true"):
        return True
    if value in ("0", "false"):
        return False
    raise HttpError(400, "invalid parameter: " + name)


def find_group(gid):
    for group in GROUPS:
        if group["gid"] == gid:
            return group
    return None


def find_lamp(kid):
    for lamp in LAMPS:
        if lamp["kid"] == kid:
            return lamp
    return None


def require_group(gid):
    group = find_group(gid)
    if group is None:
        raise HttpError(404, "unknown group")
    return group


def is_program_id(value):
    return PROGRAM_ID_MIN <= value <= PROGRAM_ID_MAX


def is_bus_gid(value):
    return MIN_GID <= value <= MAX_GID


def parse_program_gid(text):
    """A program route takes either a bus group (1-40) or a light program (101-164)."""
    value = parse_uint(text, PROGRAM_ID_MAX)
    if value is None or not (is_bus_gid(value) or is_program_id(value)):
        return None
    return value


def require_program_gid(query, name):
    if name not in query:
        raise HttpError(400, "missing parameter: " + name)
    value = parse_program_gid(query[name][0])
    if value is None:
        raise HttpError(400, name + " must be 1-40 or 101-164")
    return value


def require_lamp_gid(query, name):
    """Only real lamps on the bus live in a bus group; a light program never does."""
    if name not in query:
        raise HttpError(400, "missing parameter: " + name)
    value = parse_uint(query[name][0], PROGRAM_ID_MAX)
    if value is None:
        raise HttpError(400, "invalid parameter: " + name)
    if is_program_id(value):
        raise HttpError(400, "not a lamp group")
    if not is_bus_gid(value):
        raise HttpError(400, name + " must be 1-40")
    return value


# ---- schedule and status text -----------------------------------------------------------------


def compute_minute(slots, minute_of_day):
    """Same interpolation as kk::schedule_compute (floor division, wraps past midnight)."""
    on = [p for p in slots if p["enabled"] and p["minute"] < 1440]
    if not on:
        return None
    on.sort(key=lambda p: p["minute"])
    second_of_day = minute_of_day * 60
    prev = len(on) - 1
    for i, p in enumerate(on):
        if p["minute"] * 60 <= second_of_day:
            prev = i
    nxt = (prev + 1) % len(on)
    t0 = on[prev]["minute"] * 60
    t1 = on[nxt]["minute"] * 60
    span = (t1 - t0 + 86400) % 86400
    elapsed = (second_of_day - t0 + 86400) % 86400
    values = []
    for c in range(NUM_CHANNELS):
        a, b = on[prev]["ch"][c], on[nxt]["ch"][c]
        value = a
        if span:
            delta = (b - a) * elapsed
            step = delta // span if delta >= 0 else -((-delta + span - 1) // span)
            value = a + step
        values.append(max(0, min(CHANNEL_MAX[c], value)))
    return values


def percent(value):
    return int(math.floor(value * 100.0 / 255.0 + 0.5))


def now_minute():
    local = time.localtime()
    return local.tm_hour * 60 + local.tm_min


def format_remaining(seconds):
    if seconds < 0:
        seconds = 0
    return "%d:%02d" % (seconds // 60, seconds % 60)


def effect_remaining(group):
    """Seconds left, -1 while it runs until stopped, None when no effect runs.

    Rounds up like the firmware: the page shows how long is left, not how much has elapsed.
    """
    if not group.get("effect_name"):
        return None
    end = group.get("effect_end") or 0.0
    if end <= 0.0:
        return -1
    left = end - time.time()
    if left <= 0.0:
        return 0
    return int(math.ceil(left))


def status_text(group):
    mode = group["mode"]
    remaining = effect_remaining(group)
    if remaining is not None:
        if remaining <= 0:
            return "Effect: " + group["effect_name"]  # about to end
        return "Effect: " + group["effect_name"] + " · " + format_remaining(remaining) + " left"
    if mode == "program":
        minute = now_minute()
        channels = compute_minute(group["slots"], minute)
        if channels is None:
            return "Program · no points enabled"
        parts = ""
        for index, label in enumerate(group["labels"]):
            if label is not None:
                value = round(channels[index] * 100 / group["max"][index])
                parts += " · %s %d%%" % (label.upper(), value)
        return "Program · %02d:%02d%s" % (minute // 60, minute % 60, parts)
    if mode == "manual":
        return "Manual"
    if mode == "off":
        return "Off"
    if group.get("preview_minute") is not None:
        return "Previewing %02d:%02d" % (group["preview_minute"] // 60, group["preview_minute"] % 60)
    return "Editing point %d (previewing)" % (group["selected"] + 1)


def scanning():
    return BUS["scanning_until"] > time.time()


def bus_state_text():
    if BUS["conflict"]:
        text = "Conflict: another controller wrote while this one was controlling. Press Retry to try again"
    elif scanning():
        text = "Scanning for lamps"
    elif BUS["writes"]:
        text = "Controlling"
    else:
        text = "Writes off (dry run)"
    if BUS["scan_note"]:
        text += " (" + BUS["scan_note"] + ")"
    return text


def expire_state():
    now = time.time()
    if BUS["scanning_until"] and BUS["scanning_until"] <= now:
        BUS["scanning_until"] = 0.0
    for group in GROUPS:
        if group["mode"] == "preview" and group["preview_until"] <= now:
            group["mode"] = group["before_preview"]
            group["preview_until"] = 0.0
        # An effect that reaches its time ends by itself; whatever the mode was resumes unchanged.
        if group.get("effect_name") and effect_remaining(group) == 0:
            group["effect_name"] = None
            group["effect_end"] = 0.0


def begin_preview(group):
    if group["mode"] != "preview":
        group["before_preview"] = group["mode"]
        group["mode"] = "preview"
    group["preview_until"] = time.time() + PREVIEW_SECONDS


# ---- illustrative sun and moon days -----------------------------------------------------------
# These days are not real astronomy. The sun's sunrise and sunset move with the day of the year
# around a fixed 12:40 solar noon, and the moon gets a short evening-to-morning window, so the page
# has a plausible curve and summary for every date. They are only for developing the page.


def is_leap(year):
    return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)


def day_of_year(year, month, day):
    lengths = [31, 29 if is_leap(year) else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    return sum(lengths[: month - 1]) + day


def today_iso():
    now = time.localtime()
    return "%04d-%02d-%02d" % (now.tm_year, now.tm_mon, now.tm_mday)


def parse_date(text):
    match = re.fullmatch(r"(\d{4})-(\d{2})-(\d{2})", text or "")
    if match is None:
        return None
    year, month, day = (int(part) for part in match.groups())
    if not 2000 <= year <= 2100 or not 1 <= month <= 12 or not 1 <= day <= 31:
        return None
    return year, month, day


def _sky_value(group, name, default, low, high):
    value = group.get("sky", {}).get(name, default)
    try:
        value = int(value)
    except (TypeError, ValueError):
        value = default
    return max(low, min(high, value))


def sun_generated(group, date_text):
    year, month, day = parse_date(date_text)
    doy = day_of_year(year, month, day)
    noon = 12 * 60 + 40
    hours = _sky_value(group, "hours", 0, 0, 16)
    peak = _sky_value(group, "peak", 60, 0, 100)
    glow = _sky_value(group, "glow", 50, 0, 100)
    ramp = _sky_value(group, "ramp", 150, 0, 240)
    shift = _sky_value(group, "shift", 0, -120, 120)
    amber = _sky_value(group, "amber", 0, 0, 10)
    if hours:
        length = hours * 60
    else:
        length = int(round(720 + 180 * math.sin(2 * math.pi * (doy - 80) / 365.0)))
    sunrise = max(0, min(1439, noon - length // 2))
    sunset = max(0, min(1439, noon + length // 2))
    # The shoulder where the ramp gets near the peak: the firmware moves it ramp minutes from sunrise
    # and sunset, never closer than half an hour to noon. 0 follows the real sun's short climb.
    reach = ramp if ramp else 40
    shoulder_am = min(noon - 30, sunrise + reach)
    if shoulder_am <= sunrise:
        shoulder_am = sunrise
    shoulder_pm = max(noon + 30, sunset - reach)
    if shoulder_pm >= sunset:
        shoulder_pm = sunset
    minutes = [
        sunrise - 100, sunrise - 60, sunrise - 30, sunrise, shoulder_am,
        noon, shoulder_pm, sunset, sunset + 30, sunset + 60, sunset + 100,
    ]
    levels = [0.0, 0.04, 0.15, 0.35, 0.85, 1.0, 0.85, 0.35, 0.15, 0.04, 0.0]
    temps = [0.0, 0.0, 0.0, 0.2, 0.8, 1.0, 0.8, 0.2, 0.0, 0.0, 0.0]
    glows = [0.0, 0.10, 0.60, 1.0, 0.0, 0.0, 0.0, 1.0, 0.60, 0.10, 0.0]
    blue = group.get("model") == "tuna_blue"
    night = int(round(amber * 2.55))
    points = []
    # Mirror the shipped My points curve: violet and the red/green traces follow intensity, while
    # red and amber are stronger at the edges. The default glow 50 gives 10 % red and 30 % amber.
    clr_cool = 178 if blue else 89
    for minute, level, temp, glow_at in zip(minutes, levels, temps, glows):
        intensity = int(round(level * peak * 2.55))
        color = int(round(temp * clr_cool))
        night_at = night if not level else max(0, int(round(night * (1.0 - level))))
        if blue:
            violet = int(round(60 * level + glow_at * glow * 0.1))
            trace = int(round(5 * level))
            channels = [intensity, color, max(violet, night_at), trace, trace]
        else:
            red = max(int(round(8 * level)), int(round(glow_at * glow * 0.2)))
            amber = max(int(round(15 * level)), int(round(glow_at * glow * 0.6)))
            channels = [intensity, color, 0, red, max(amber, night_at)]
        points.append(point((minute + shift) % 1440, channels))
    summary = {
        "sunrise": sunrise,
        "noon": noon,
        "sunset": sunset,
        "length": sunset - sunrise,
        "always_up": False,
    }
    return {"points": points, "summary": summary}


def moon_generated(group, date_text):
    year, month, day = parse_date(date_text)
    doy = day_of_year(year, month, day)
    shift = _sky_value(group, "shift", 0, -120, 120)
    # An evening-to-early-morning window, so the illustrative moon is up when it is dark.
    rise = 1080 + (doy * 7) % 240
    moon_set = rise + 300
    peak = rise + (moon_set - rise) // 2
    illumination = round((1 - math.cos(2 * math.pi * doy / 29.5)) / 2, 2)
    sky = group.get("sky", {})
    top = 255.0 * sky.get("moon", 0) / 100.0 * (illumination if sky.get("phase", 1) else 1.0)
    points = [
        point((rise + shift) % 1440, [0, 255, 0, 0, 0]),
        point((peak + shift) % 1440, [int(round(top)), 255, 0, 0, 0]),
        point((moon_set + shift) % 1440, [0, 255, 0, 0, 0]),
    ]
    summary = {"rise": rise % 1440, "set": moon_set % 1440, "illumination": illumination}
    return {"points": points, "summary": summary}


def generated_day(group, source, date_text):
    if source == 1:
        sky = group.get("sky", {})
        layers = sky.get("layers", 3) & (3 if can_moon(group) else 1)
        if layers == 3:
            day = sun_generated(group, date_text)
            # The illustrative moon window may fall in daylight, so keep only the points outside the
            # sun's day.
            moon = moon_generated(group, date_text)
            sunrise = day["summary"]["sunrise"]
            sunset = day["summary"]["sunset"]
            dark = [p for p in moon["points"] if p["minute"] <= sunrise or p["minute"] >= sunset]
            if dark:
                day["points"] = sorted(day["points"] + dark, key=lambda p: p["minute"])
                day["summary"]["moon"] = moon["summary"]
            else:
                day["summary"]["moon"] = {"rise": -1, "set": -1, "illumination": moon["summary"]["illumination"]}
            return day
        if layers == 2:
            return moon_generated(group, date_text)
        if layers == 0:
            return {"points": [], "summary": {}}
        return sun_generated(group, date_text)
    return {"points": [], "summary": {}}


def source_note(group):
    if group.get("source", 0) == 0:
        return ""
    return "" if LOCATION is not None else "Set a location"


# ---- state JSON -------------------------------------------------------------------------------


def state_json():
    expire_state()
    minute = now_minute()
    groups = []
    for group in GROUPS:
        lamps = [lamp["kid"] for lamp in LAMPS if lamp["gid"] == group["gid"]]
        remaining = effect_remaining(group)
        effect = None
        if remaining is not None:
            effect = {"name": group["effect_name"], "remaining": remaining}
        entry = {
            "gid": group["gid"],
            "kind": group["kind"],
            "name": group.get("nickname") or group["name"],
            "default_name": group["name"],
            "model": group["model"],
            "mode": group["mode"],
            "status": status_text(group),
            "effects": group["effects"],
            "effect": effect,
            "effect_speed": group["effect_speed"],
            "effect_minutes": group["effect_minutes"],
            "labels": group["labels"],
            "max": group["max"],
            "slots": group["slots"],
            "defaults": group["defaults"],
            "lamps": lamps,
            "now_minute": minute,
            "selected": group["selected"],
            "acclimation": dict(group["acclimation"]),
        }
        if group.get("output") is not None:
            entry["output"] = group["output"]
        if "source" in group:
            entry["source"] = group["source"]
            entry["can_moon"] = can_moon(group)
            entry["source_note"] = source_note(group)
            entry["sky"] = dict(group["sky"])
            if group["source"] != 0 and LOCATION is not None:
                entry["generated"] = generated_day(group, group["source"], today_iso())["points"]
        groups.append(entry)
    lamps = [lamp_record(lamp) for lamp in LAMPS]
    return {
        "groups": groups,
        "lamps": lamps,
        "followers": [dict(l) for l in FOLLOWERS],
        "location": None if LOCATION is None else dict(LOCATION),
        "bus": {
            "state": bus_state_text(),
            "role": ROLE,
            "writes": BUS["writes"],
            "want_writes": BUS["writes"],
            "known_lamps": len(LAMPS),
        },
    }


# ---- route handlers ---------------------------------------------------------------------------


def handle_mode(query):
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    if "mode" not in query:
        raise HttpError(400, "missing parameter: mode")
    mode = query["mode"][0].lower()
    if mode not in ("program", "manual", "off"):
        raise HttpError(400, "invalid mode (program, manual or off)")
    if group["mode"] == "preview":
        group["before_preview"] = mode
        group["mode"] = mode
    else:
        group["mode"] = mode
    group["preview_until"] = 0.0


def handle_effect(query):
    """Start an effect as a temporary overlay; the group's mode does not change."""
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    if "name" not in query:
        raise HttpError(400, "missing parameter: name")
    name = query["name"][0]
    if name not in group["effects"]:
        raise HttpError(404, "unknown effect")
    speed = group["effect_speed"]
    if "speed" in query:
        speed = EFFECT_SPEEDS.get(query["speed"][0])
        if speed is None:
            raise HttpError(400, "invalid parameter: speed")
    minutes = group["effect_minutes"]
    if "minutes" in query:
        minutes = parse_effect_minutes(query["minutes"][0])
        if minutes is None:
            raise HttpError(400, "invalid parameter: minutes")
    # Options persist with the group state, like the firmware's preference-backed settings.
    group["effect_speed"] = speed
    group["effect_minutes"] = minutes
    group["effect_name"] = name
    group["effect_end"] = 0.0 if minutes == 0 else time.time() + minutes * 60.0


def handle_effect_stop(query):
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    group["effect_name"] = None
    group["effect_end"] = 0.0


def handle_output(query):
    """Any subset of a bus group's brightness curve, transition time and soft start."""
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    if group.get("output") is None:
        raise HttpError(400, "this program has no output options")
    settings = dict(group["output"])
    if "curve" in query:
        settings["curve"] = require_uint(query, "curve", 1)
    if "seconds" in query:
        seconds = parse_output_seconds(query["seconds"][0])
        if seconds is None:
            raise HttpError(400, "invalid parameter: seconds")
        settings["seconds"] = seconds
    if "soft_start" in query:
        settings["soft_start"] = require_uint(query, "soft_start", 120)
    group["output"] = settings


def parse_output_seconds(text):
    """0..10 seconds, tenths allowed; 0 means instant. None when invalid."""
    if not text:
        return None
    try:
        value = float(text)
    except ValueError:
        return None
    if not 0.0 <= value <= 10.0:
        return None
    return round(value, 1)  # the firmware stores tenths of a second


def handle_acclimation(query):
    """Any subset of on, start (10-90) and days (1-90); turning on starts counting today."""
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    state = group["acclimation"]
    on = state["on"]
    if "on" in query:
        on = require_bool(query, "on")
    start = state["start"]
    if "start" in query:
        start = require_uint(query, "start", 90)
        if start < 10:
            raise HttpError(400, "invalid parameter: start")
    days = state["days"]
    if "days" in query:
        days = require_uint(query, "days", 90)
        if days < 1:
            raise HttpError(400, "invalid parameter: days")
    state["on"] = on
    state["start"] = start
    state["days"] = days
    # The mock keeps it simple: while on, the whole program runs at the starting share.
    state["share"] = start if on else 100


# ---- Sky program: sun and moon layers ----------------------------------------------------------

SOURCE_NAMES = {"points": 0, "sky": 1}
GEN_SEEN = set()
GEN_RESULTS = {}


def require_sky_group(query):
    """The group named by gid, when it has the Sky program."""
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    if "source" not in group:
        raise HttpError(400, "this program has no Sky source")
    return group


def parse_source(text):
    value = SOURCE_NAMES.get(text)
    if value is None:
        raise HttpError(400, "invalid parameter: source")
    return value


def can_moon(group):
    """The moon needs blue light: a Tuna Blue or a light that mixes color."""
    return group["model"] in ("tuna_blue", "light_rgb", "light_rgbw")


def handle_source(query):
    group = require_sky_group(query)
    if "source" not in query:
        raise HttpError(400, "invalid parameter: source")
    group["source"] = parse_source(query["source"][0])


def handle_sky(query):
    """Any subset of the Sky settings, validated like the firmware."""
    group = require_sky_group(query)
    settings = dict(group["sky"])
    if "layers" in query:
        layers = require_uint(query, "layers", 3)
        if layers == 0:
            raise HttpError(400, "invalid parameter: layers")
        if layers == 2 and not can_moon(group):
            raise HttpError(400, "the moon needs a light that makes blue")
        settings["layers"] = layers
    if "peak" in query:
        settings["peak"] = require_uint(query, "peak", 100)
    if "hours" in query:
        hours = require_uint(query, "hours", 16)
        if hours != 0 and hours < 4:
            raise HttpError(400, "invalid parameter: hours")
        settings["hours"] = hours
    if "glow" in query:
        settings["glow"] = require_uint(query, "glow", 100)
    if "ramp" in query:
        # The device keeps the ramp in whole tens of minutes, so it rounds a posted value down.
        settings["ramp"] = require_uint(query, "ramp", 240) // 10 * 10
    if "amber" in query:
        settings["amber"] = require_uint(query, "amber", 10)
    if "moon" in query:
        settings["moon"] = require_uint(query, "moon", 100)
    if "phase" in query:
        settings["phase"] = require_uint(query, "phase", 1)
    if "shift" in query:
        text = query["shift"][0]
        if re.fullmatch(r"[+-]?\d+", text) is None:
            raise HttpError(400, "invalid parameter: shift")
        shift = int(text)
        if shift < -120 or shift > 120:
            raise HttpError(400, "invalid parameter: shift")
        settings["shift"] = shift
    group["sky"] = settings


def fit_points(points, count):
    """Downsample a generated day to the slots there are, keeping the first and last points.

    The firmware keeps the slot count and fills any slots the day does not reach with off points.
    """
    if len(points) > count:
        packed = []
        for i in range(count):
            index = round(i * (len(points) - 1) / (count - 1)) if count > 1 else 0
            packed.append(copy_slots([points[index]])[0])
    else:
        packed = copy_slots(points)
    while len(packed) < count:
        packed.append({"minute": 0, "ch": [0, 0, 0, 0, 0], "enabled": False})
    return packed


def handle_source_copy(query):
    group = require_sky_group(query)
    if group["source"] == 0 or LOCATION is None:
        raise HttpError(409, "no generated day to copy")
    day = generated_day(group, group["source"], today_iso())
    group["slots"] = fit_points(day["points"], len(group["slots"]))
    group["source"] = 0


def handle_generated(query):
    """GET /klink/generated: pending on the first ask for a day, the day on the second."""
    group = require_sky_group(query)
    if "source" not in query:
        raise HttpError(400, "invalid parameter: source")
    source = parse_source(query["source"][0])
    if source == 0:
        raise HttpError(400, "invalid parameter: source")
    date_text = query.get("date", [""])[0]
    if parse_date(date_text) is None:
        raise HttpError(400, "invalid parameter: date")
    if LOCATION is None:
        raise HttpError(409, "no location set")
    settings = group.get("sky", {})
    sky_key = tuple(settings.get(n) for n in ("layers", "peak", "hours", "glow", "amber", "ramp", "shift", "moon", "phase"))
    key = (group["gid"], source, date_text, sky_key)
    if key not in GEN_SEEN:
        GEN_SEEN.add(key)
        return {"pending": True}
    if key not in GEN_RESULTS:
        GEN_RESULTS[key] = generated_day(group, source, date_text)
    return GEN_RESULTS[key]


def parse_effect_minutes(text):
    """0..5 in half-minute steps; 0 means "until stopped". None when invalid."""
    if not text:
        return None
    try:
        value = float(text)
    except ValueError:
        return None
    if not 0.0 <= value <= 5.0:
        return None
    halves = value * 2.0
    if abs(halves - round(halves)) > 1e-6:
        return None
    return value


def handle_identify(query):
    has_kid = "kid" in query
    has_gid = "gid" in query
    has_follower = "follower" in query
    if int(has_kid) + int(has_gid) + int(has_follower) != 1:
        raise HttpError(400, "provide exactly one of kid, gid or follower")
    if has_follower:
        key = query["follower"][0]
        if not any(lamp["id"] == key for lamp in FOLLOWERS):
            raise HttpError(404, "unknown follower")
        return
    if has_gid:
        value = require_uint(query, "gid", PROGRAM_ID_MAX)
        if is_program_id(value):
            # A light program flashes its own lights, also while Send to lights is off (it never touches the bus).
            require_group(value)
            return
        value = require_lamp_gid(query, "gid")
    else:
        value = require_uint(query, "kid", 0xFFFFFFFF)
    if not BUS["writes"]:
        raise HttpError(409, "Send to lights is off")
    if has_gid:
        if not any(lamp["gid"] == value for lamp in LAMPS):
            raise HttpError(404, "no known lamps in that group")


def handle_point(query):
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    slot = require_uint(query, "slot", NUM_SLOTS)
    if slot < 1:
        raise HttpError(400, "slot must be 1-based")
    index = slot - 1
    current = group["slots"][index]
    p = {
        "minute": current["minute"],
        "ch": list(current["ch"]),
        "enabled": current["enabled"],
    }
    if "minute" in query:
        p["minute"] = require_uint(query, "minute", 1439)
    for name, channel in (("int", 0), ("clr", 1), ("r", 3), ("am", 4)):
        if name in query:
            p["ch"][channel] = require_uint(query, name, group["max"][channel])
    for c in range(NUM_CHANNELS):
        name = "c%d" % (c + 1)
        if name not in query:
            continue
        if group["labels"][c] is None:
            raise HttpError(400, "this model has no channel " + name)
        p["ch"][c] = require_uint(query, name, group["max"][c])
    if "enabled" in query:
        p["enabled"] = require_bool(query, "enabled")
    group["slots"][index] = p
    # The firmware previews the point it just wrote (preview_point(index)).
    if p["enabled"]:
        begin_preview(group)


def parse_hex_rgb(text):
    """Exactly six hex digits, either case, optional single leading '#' (matches the firmware)."""
    if text.startswith("#"):
        text = text[1:]
    if len(text) != 6 or re.fullmatch(r"[0-9a-fA-F]{6}", text) is None:
        return None
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def handle_snap(query):
    """Closest settings this lamp can make for a target color; read-only."""
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    model = group["model"]
    if model not in COLOR_MODELS:
        raise HttpError(400, "no color model for this group")
    if "rgb" not in query:
        raise HttpError(400, "missing parameter: rgb")
    target = parse_hex_rgb(query["rgb"][0])
    if target is None:
        raise HttpError(400, "invalid parameter: rgb")
    result = klink_colors.nearest_i(model, target)
    settings = {"clr": result["clr"]}
    for (channel, label), value in zip(klink_colors.CHANNELS[model], result["overlays"]):
        if group["labels"][channel] == label:
            settings[label] = value
    return {
        "set": settings,
        "rgb": "%02x%02x%02x" % result["rgb"],
        "target": "%02x%02x%02x" % target,
        "exact": result["dist"] == 0,
    }


def handle_preview(query):
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    has_slot = "slot" in query
    has_minute = "minute" in query
    if has_slot == has_minute:
        raise HttpError(400, "provide exactly one of slot or minute")
    if has_slot:
        slot = parse_uint(query["slot"][0], NUM_SLOTS)
        if slot is None or slot < 1:
            raise HttpError(400, "invalid parameter: slot")
        index = slot - 1
        if not group["slots"][index]["enabled"]:
            return
        # The firmware only sends the point; selecting it is the editor's job. The mock tracks it
        # so the status line names the point a developer just asked to preview.
        group["selected"] = index
        group["preview_minute"] = None
        begin_preview(group)
    else:
        minute = parse_uint(query["minute"][0], 1439)
        if minute is None:
            raise HttpError(400, "invalid parameter: minute")
        if compute_minute(group["slots"], minute) is None:
            return  # nothing enabled
        group["preview_minute"] = minute
        begin_preview(group)


def handle_preview_done(query):
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    if group["mode"] == "preview":
        group["mode"] = group["before_preview"]
        group["preview_until"] = 0.0


def assignment_error(lamp, gid):
    if gid < MIN_GID or gid > MAX_GID:
        return "GID must be 1-40"
    default_gid = {1: 1, 2: 2, 3: 26}.get(lamp["type"], 0)
    if gid in (1, 2, 26) and gid != default_gid:
        return "that is the default group of a different lamp type"
    if gid not in (1, 2, 26) and (gid < CUSTOM_GID_MIN or gid > CUSTOM_GID_MAX):
        return "custom groups use GIDs 3-25"
    for other in LAMPS:
        if other["gid"] == gid and other["kid"] != lamp["kid"] and other["type"] != lamp["type"]:
            return "group already holds a different lamp type"
    return ""


def handle_assign(query):
    kid = require_uint(query, "kid", 0xFFFFFFFF)
    gid = require_lamp_gid(query, "gid")
    if not BUS["writes"]:
        raise HttpError(409, "Send to lights is off")
    lamp = find_lamp(kid)
    if lamp is None:
        raise HttpError(409, "unknown KID: scan first")
    error = assignment_error(lamp, gid)
    if error:
        raise HttpError(409, error)
    lamp["gid"] = gid


def handle_name(query):
    kid = require_uint(query, "kid", 0xFFFFFFFF)
    if "name" not in query:
        raise HttpError(400, "missing parameter: name")
    name = query["name"][0]
    try:
        encoded = name.encode("utf-8")
    except UnicodeEncodeError:
        raise HttpError(400, "name is not valid UTF-8")
    if len(encoded) > NAME_MAX_LEN:
        raise HttpError(400, "name is too long (24 bytes max)")
    lamp = find_lamp(kid)
    if lamp is not None:
        lamp["name"] = name


def handle_group_name(query):
    gid = require_program_gid(query, "gid")
    group = require_group(gid)
    if "name" not in query:
        raise HttpError(400, "missing parameter: name")
    name = query["name"][0]
    if len(name.encode("utf-8")) > NAME_MAX_LEN:
        raise HttpError(400, "name is too long (24 bytes max)")
    group["nickname"] = name


def handle_copy(query):
    source_gid = require_program_gid(query, "from")
    target_gid = require_program_gid(query, "to")
    source = require_group(source_gid)
    target = require_group(target_gid)
    if source["model"] != target["model"] or len(source["slots"]) != len(target["slots"]):
        raise HttpError(409, "groups differ in model or slot count")
    target["slots"] = [{"minute": p["minute"], "ch": list(p["ch"]), "enabled": p["enabled"]} for p in source["slots"]]


def handle_retry(query):
    BUS["conflict"] = False
    BUS["scan_note"] = ""


def handle_scan(query):
    BUS["scan_note"] = ""
    if not BUS["writes"]:
        BUS["scan_note"] = "Scan needs Send to lights on"
        return
    BUS["scanning_until"] = time.time() + SCAN_SECONDS


def handle_mock_writes(query):
    value = query.get("on", ["1"])[0]
    if value not in ("0", "1"):
        raise HttpError(400, "invalid parameter: on")
    BUS["writes"] = value == "1"
    BUS["scan_note"] = ""


def handle_writes(query):
    value = query.get("on", [None])[0]
    if value not in ("0", "1", "true", "false"):
        raise HttpError(400, "invalid parameter: on")
    BUS["writes"] = value in ("1", "true")
    BUS["scan_note"] = ""


ROUTES = {
    "/klink/writes": handle_writes,
    "/klink/mode": handle_mode,
    "/klink/effect": handle_effect,
    "/klink/effect/stop": handle_effect_stop,
    "/klink/output": handle_output,
    "/klink/acclimation": handle_acclimation,
    "/klink/source": handle_source,
    "/klink/sky": handle_sky,
    "/klink/source/copy": handle_source_copy,
    "/klink/identify": handle_identify,
    "/klink/point": handle_point,
    "/klink/preview": handle_preview,
    "/klink/preview/done": handle_preview_done,
    "/klink/assign": handle_assign,
    "/klink/name": handle_name,
    "/klink/groupname": handle_group_name,
    "/klink/copy": handle_copy,
    "/klink/retry": handle_retry,
    "/klink/scan": handle_scan,
}


# ---- Request dispatch -------------------------------------------------------------------------

JSON_TYPE = "application/json"
HTML_TYPE = "text/html; charset=utf-8"
TEXT_TYPE = "text/plain; charset=utf-8"


def _json_result(code, payload):
    return code, JSON_TYPE, json.dumps(payload, separators=(",", ":"), ensure_ascii=False)


def _error_result(code, message):
    return _json_result(code, {"error": message})


def dispatch(method, target, body=None):
    """Answer one request without a socket, so the same mock can run under Pyodide.

    `target` is the path plus optional query string, exactly as it appears on the request line.
    Returns (status, content_type, body) with `body` as text. `body` (the request body) is
    accepted for symmetry; every route takes its parameters from the query string.
    """
    parsed = urllib.parse.urlsplit(target)
    path = parsed.path
    query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
    with LOCK:
        try:
            if method == "GET":
                if path == "/klink":
                    # Re-assemble on every request so a reload picks up edits to components/klink/web/.
                    try:
                        return 200, HTML_TYPE, web_build.assemble()
                    except (OSError, ValueError) as exc:
                        return 500, TEXT_TYPE, "web page build failed: %s" % exc
                if path == "/klink/state":
                    return _json_result(200, state_json())
                if path == "/klink/snap":
                    return _json_result(200, handle_snap(query))
                if path == "/klink/generated":
                    return _json_result(200, handle_generated(query))
                if path in POST_ROUTES:
                    return _error_result(405, "method not allowed")
                if path == "/":
                    return (
                        200,
                        HTML_TYPE,
                        "<!doctype html><meta charset=\"utf-8\"><title>K-Link mock</title>"
                        "<h1>K-Link device page (mock)</h1><p><a href=\"/klink\">Open the K-Link page</a></p>",
                    )
                return _error_result(404, "not found")
            if method == "POST":
                if path in ("/switch/send_to_lights/turn_on", "/switch/send_to_lights/turn_off"):
                    BUS["writes"] = path.endswith("turn_on")
                    BUS["scan_note"] = ""
                    return 200, TEXT_TYPE, ""
                if path == "/klink/_mock/writes":
                    handle_mock_writes(query)
                    return _json_result(200, {"ok": True})
                handler = ROUTES.get(path)
                if handler is None:
                    if path in GET_ROUTES:
                        return _error_result(405, "method not allowed")
                    return _error_result(404, "not found")
                handler(query)
                return _json_result(200, {"ok": True})
            return _error_result(405, "method not allowed")
        except HttpError as err:
            return _error_result(err.code, err.message)
        except Exception as exc:  # keep the mock responsive while developing
            return _error_result(500, "mock error: %s" % exc)


# ---- HTTP plumbing ----------------------------------------------------------------------------


class Handler(BaseHTTPRequestHandler):
    server_version = "KLinkMock/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[mock] %s\n" % (fmt % args))

    def send_result(self, result):
        code, content_type, body = result
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def throttle(self):
        if SLOW_SECONDS:
            time.sleep(SLOW_SECONDS)

    def do_GET(self):
        self.throttle()
        self.send_result(dispatch("GET", self.path))

    def do_POST(self):
        self.throttle()
        length = int(self.headers.get("Content-Length") or 0)
        if length > 0:
            self.rfile.read(length)  # drain the body; the routes read the query string
        self.send_result(dispatch("POST", self.path))


class Server(ThreadingHTTPServer):
    daemon_threads = True


def main(argv=None):
    global SLOW_SECONDS, LOCATION
    parser = argparse.ArgumentParser(description="Mock of the K-Link web API.")
    parser.add_argument("port", nargs="?", type=int, default=8080, help="TCP port (default 8080)")
    parser.add_argument("--host", default="127.0.0.1", help="bind address (default 127.0.0.1)")
    parser.add_argument("--slow", action="store_true", help="add ~150 ms of latency to every request")
    parser.add_argument(
        "--no-location",
        action="store_true",
        help="report no location, so the Sky program asks for one",
    )
    args = parser.parse_args(argv)
    if args.no_location:
        LOCATION = None
    SLOW_SECONDS = 0.15 if args.slow else 0.0
    server = Server((args.host, args.port), Handler)
    print("K-Link mock: http://%s:%d/klink  (Ctrl+C to stop)" % (args.host, args.port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
