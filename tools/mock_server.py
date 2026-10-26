#!/usr/bin/env python3
"""In-memory mock of the K-Link web API for developing the page without hardware.

Serves the assembled (not gzipped) page at http://127.0.0.1:8080/klink and implements every
route from components/klink/klink_web.cpp with fixed state. The page is re-assembled from
components/klink/web/ on every request, so edits show up on reload. Stdlib only:

    python3 tools/mock_server.py            # port 8080
    python3 tools/mock_server.py 9000       # another port
    python3 tools/mock_server.py --slow     # ~150 ms latency per request
    python3 tools/mock_server.py --many     # 10 groups, 32 lamps, 3 groups without a program

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
import web_build  # noqa: E402

NUM_CHANNELS = 5
CHANNEL_MAX = [255, 255, 100, 100, 100]
MIN_GID, MAX_GID = 1, 40
CUSTOM_GID_MIN, CUSTOM_GID_MAX = 3, 25
NUM_SLOTS = 8
PREVIEW_SECONDS = 60.0
SCAN_SECONDS = 2.5
NAME_MAX_LEN = 24

POST_ROUTES = (
    "/klink/mode",
    "/klink/identify",
    "/klink/point",
    "/klink/preview",
    "/klink/preview/done",
    "/klink/assign",
    "/klink/name",
    "/klink/copy",
    "/klink/retry",
    "/klink/scan",
    "/klink/writes",
)


def point(minute, channels, enabled=True):
    return {"minute": minute, "ch": list(channels), "enabled": enabled}


def make_group(gid, name, model, labels, slots):
    return {
        "gid": gid,
        "name": name,
        "model": model,
        "labels": list(labels),
        "max": list(CHANNEL_MAX),
        "mode": "program",
        "before_preview": "program",
        "preview_until": 0.0,
        "selected": 0,
        "slots": slots,
    }


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
]

LAMPS = [
    {"kid": 18000001, "type": 2, "gid": 2, "name": ""},
    {"kid": 18000002, "type": 1, "gid": 1, "name": "Left"},
    {"kid": 18000003, "type": 1, "gid": 1, "name": "Right"},
]

BUS = {"writes": True, "conflict": False, "scanning_until": 0.0, "scan_note": ""}


# ---- --many laboratory ------------------------------------------------------------------------


def many_slots():
    """Eight slots, six enabled, valid for either model (c2 is ignored on Tuna Sun)."""
    return [
        point(8 * 60, [30, 60, 0, 0, 0]),
        point(10 * 60 + 30, [150, 120, 0, 5, 15]),
        point(13 * 60, [210, 170, 0, 20, 35]),
        point(16 * 60, [170, 140, 0, 10, 25]),
        point(19 * 60, [70, 70, 0, 0, 10]),
        point(21 * 60 + 30, [8, 12, 0, 0, 0]),
        point(0, [0, 0, 0, 0, 0], False),
        point(0, [0, 0, 0, 0, 0], False),
    ]


MANY_GROUPS = (
    (1, "Tuna Blue", "tuna_blue"),
    (2, "Tuna Sun", "tuna_sun"),
    (3, "Reef 3", "tuna_sun"),
    (4, "Reef 4", "tuna_blue"),
    (5, "Reef 5", "tuna_sun"),
    (6, "Reef 6", "tuna_blue"),
    (7, "Reef 7", "tuna_sun"),
    (8, "Reef 8", "tuna_blue"),
    (9, "Reef 9", "tuna_sun"),
    (10, "Reef 10", "tuna_blue"),
)

LABELS = {
    "tuna_sun": ["int", "clr", None, "r", "am"],
    "tuna_blue": ["int", "clr", "vio", "r", "grn"],
}


def many_groups():
    return [
        make_group(gid, name, model, LABELS[model], many_slots())
        for gid, name, model in MANY_GROUPS
    ]


def many_lamps():
    """32 lamps: two in each configured group, four in each unconfigured group 11, 12 and 20."""
    lamps = []
    kid = 18000001
    placement = [(gid, 2) for gid, _, _ in MANY_GROUPS] + [(11, 4), (12, 4), (20, 4)]
    named = {18000002: "Left", 18000003: "Right", 18000008: "Reef left", 18000009: "Reef right"}
    types = {1: 1, 2: 2}
    for gid, count in placement:
        if gid not in types:
            types[gid] = 1 if gid % 2 else 2
        for _ in range(count):
            lamps.append(
                {
                    "kid": kid,
                    "type": types[gid],
                    "gid": gid,
                    "name": named.get(kid, ""),
                }
            )
            kid += 1
    return lamps


def use_many():
    """Replace the default two groups with the large laboratory used by --many."""
    GROUPS[:] = many_groups()
    LAMPS[:] = many_lamps()
    BUS["writes"] = True
    BUS["conflict"] = False
    BUS["scan_note"] = ""

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


def status_text(group):
    mode = group["mode"]
    if mode == "program":
        minute = now_minute()
        channels = compute_minute(group["slots"], minute)
        if channels is None:
            return "Program · no points enabled"
        return "Program · %02d:%02d · INT %d%% CLR %d%%" % (
            minute // 60,
            minute % 60,
            percent(channels[0]),
            percent(channels[1]),
        )
    if mode == "manual":
        return "Manual"
    if mode == "off":
        return "Off"
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


def begin_preview(group):
    if group["mode"] != "preview":
        group["before_preview"] = group["mode"]
        group["mode"] = "preview"
    group["preview_until"] = time.time() + PREVIEW_SECONDS


# ---- state JSON -------------------------------------------------------------------------------


def state_json():
    expire_state()
    minute = now_minute()
    groups = []
    for group in GROUPS:
        lamps = [lamp["kid"] for lamp in LAMPS if lamp["gid"] == group["gid"]]
        groups.append(
            {
                "gid": group["gid"],
                "name": group["name"],
                "model": group["model"],
                "mode": group["mode"],
                "status": status_text(group),
                "labels": group["labels"],
                "max": group["max"],
                "slots": group["slots"],
                "lamps": lamps,
                "now_minute": minute,
                "selected": group["selected"],
            }
        )
    lamps = [
        {"kid": lamp["kid"], "type": lamp["type"], "gid": lamp["gid"], "name": lamp["name"]} for lamp in LAMPS
    ]
    return {
        "groups": groups,
        "lamps": lamps,
        "bus": {"state": bus_state_text(), "writes": BUS["writes"], "known_lamps": len(LAMPS)},
    }


# ---- route handlers ---------------------------------------------------------------------------


def handle_mode(query):
    gid = require_uint(query, "gid", MAX_GID)
    if gid < MIN_GID:
        raise HttpError(400, "gid must be 1-40")
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


def handle_identify(query):
    has_kid = "kid" in query
    has_gid = "gid" in query
    if has_kid == has_gid:
        raise HttpError(400, "provide exactly one of kid or gid")
    value = require_uint(query, "kid" if has_kid else "gid", 0xFFFFFFFF if has_kid else MAX_GID)
    if not BUS["writes"]:
        raise HttpError(409, "Send to lights is off")
    if has_gid:
        if not any(lamp["gid"] == value for lamp in LAMPS):
            raise HttpError(404, "no known lamps in that group")


def handle_point(query):
    gid = require_uint(query, "gid", MAX_GID)
    if gid < MIN_GID:
        raise HttpError(400, "gid must be 1-40")
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
            p["ch"][channel] = require_uint(query, name, CHANNEL_MAX[channel])
    for c in range(NUM_CHANNELS):
        name = "c%d" % (c + 1)
        if name not in query:
            continue
        if group["labels"][c] is None:
            raise HttpError(400, "this model has no channel " + name)
        p["ch"][c] = require_uint(query, name, CHANNEL_MAX[c])
    if "enabled" in query:
        p["enabled"] = require_bool(query, "enabled")
    group["slots"][index] = p
    # The firmware previews the point it just wrote (preview_point(index)).
    if p["enabled"]:
        begin_preview(group)


def handle_preview(query):
    gid = require_uint(query, "gid", MAX_GID)
    if gid < MIN_GID:
        raise HttpError(400, "gid must be 1-40")
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
        begin_preview(group)
    else:
        minute = parse_uint(query["minute"][0], 1439)
        if minute is None:
            raise HttpError(400, "invalid parameter: minute")
        if compute_minute(group["slots"], minute) is None:
            return  # nothing enabled
        begin_preview(group)


def handle_preview_done(query):
    gid = require_uint(query, "gid", MAX_GID)
    if gid < MIN_GID:
        raise HttpError(400, "gid must be 1-40")
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
    gid = require_uint(query, "gid", MAX_GID)
    if gid < MIN_GID:
        raise HttpError(400, "gid must be 1-40")
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


def handle_copy(query):
    source_gid = require_uint(query, "from", MAX_GID)
    if source_gid < MIN_GID:
        raise HttpError(400, "from must be 1-40")
    target_gid = require_uint(query, "to", MAX_GID)
    if target_gid < MIN_GID:
        raise HttpError(400, "to must be 1-40")
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
    "/klink/identify": handle_identify,
    "/klink/point": handle_point,
    "/klink/preview": handle_preview,
    "/klink/preview/done": handle_preview_done,
    "/klink/assign": handle_assign,
    "/klink/name": handle_name,
    "/klink/copy": handle_copy,
    "/klink/retry": handle_retry,
    "/klink/scan": handle_scan,
}


# ---- HTTP plumbing ----------------------------------------------------------------------------


class Handler(BaseHTTPRequestHandler):
    server_version = "KLinkMock/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[mock] %s\n" % (fmt % args))

    def send_json(self, code, payload):
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, code, message):
        self.send_json(code, {"error": message})

    def send_text(self, code, body, content_type="text/html; charset=utf-8"):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def split_path(self):
        parsed = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        return parsed.path, query

    def throttle(self):
        if SLOW_SECONDS:
            time.sleep(SLOW_SECONDS)

    def do_GET(self):
        self.throttle()
        path, query = self.split_path()
        with LOCK:
            try:
                if path == "/klink":
                    self.serve_index()
                    return
                if path == "/klink/state":
                    self.send_json(200, state_json())
                    return
                if path in POST_ROUTES:
                    self.send_error_json(405, "method not allowed")
                    return
                if path == "/":
                    self.send_text(
                        200,
                        "<!doctype html><meta charset=\"utf-8\"><title>K-Link mock</title>"
                        "<h1>K-Link device page (mock)</h1><p><a href=\"/klink\">Open the K-Link page</a></p>",
                    )
                    return
                self.send_error_json(404, "not found")
            except Exception as exc:  # keep the mock responsive while developing
                self.send_error_json(500, "mock error: %s" % exc)

    def serve_index(self):
        # Re-assemble on every request so a reload picks up edits to components/klink/web/.
        try:
            body = web_build.assemble().encode("utf-8")
        except (OSError, ValueError) as exc:
            self.send_text(500, "web page build failed: %s" % exc, "text/plain; charset=utf-8")
            return
        self.send_text(200, body)

    def do_POST(self):
        self.throttle()
        path, query = self.split_path()
        length = int(self.headers.get("Content-Length") or 0)
        if length > 0:
            self.rfile.read(length)
        with LOCK:
            try:
                if path in ("/switch/send_to_lights/turn_on", "/switch/send_to_lights/turn_off"):
                    BUS["writes"] = path.endswith("turn_on")
                    BUS["scan_note"] = ""
                    self.send_text(200, "", "text/plain; charset=utf-8")
                    return
                if path == "/klink/_mock/writes":
                    handle_mock_writes(query)
                    self.send_json(200, {"ok": True})
                    return
                handler = ROUTES.get(path)
                if handler is None:
                    self.send_error_json(404, "not found")
                    return
                handler(query)
                self.send_json(200, {"ok": True})
            except HttpError as err:
                self.send_error_json(err.code, err.message)
            except Exception as exc:  # keep the mock responsive while developing
                self.send_error_json(500, "mock error: %s" % exc)


class Server(ThreadingHTTPServer):
    daemon_threads = True


def main(argv=None):
    global SLOW_SECONDS
    parser = argparse.ArgumentParser(description="Mock of the K-Link web API.")
    parser.add_argument("port", nargs="?", type=int, default=8080, help="TCP port (default 8080)")
    parser.add_argument("--host", default="127.0.0.1", help="bind address (default 127.0.0.1)")
    parser.add_argument("--slow", action="store_true", help="add ~150 ms of latency to every request")
    parser.add_argument(
        "--many",
        action="store_true",
        help="10 configured groups, 32 lamps and 3 groups this controller has no program for",
    )
    args = parser.parse_args(argv)
    if args.many:
        use_many()
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
