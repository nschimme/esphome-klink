#!/usr/bin/env python3
"""Keep the C++ stock schedules and the full YAML templates from drifting apart.

The default leader and every automatic program use components/klink/klink_model_defaults.h.
The optional full group templates use their text entities' initial_value strings. They must
describe the same 16 points for each model.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "components/klink/klink_model_defaults.h"
TEMPLATES = {
    "TUNA_BLUE": ROOT / "common/group-tuna-blue.yaml",
    "TUNA_SUN": ROOT / "common/group-tuna-sun.yaml",
    "REFUGIUM": ROOT / "common/group-refugium.yaml",
}
LABELS = {
    "TUNA_BLUE": {"int": 0, "clr": 1, "vio": 2, "r": 3, "grn": 4},
    "TUNA_SUN": {"int": 0, "clr": 1, "r": 3, "am": 4},
    "REFUGIUM": {"int": 0, "clr": 1, "w": 3, "grn": 4},
}

CPP_POINT = re.compile(
    r"\{(\d+)( \* 60)?, \{(\d+), (\d+), (\d+), (\d+), (\d+)\}, (true|false)\}"
)
CPP_ARRAY = re.compile(r"static const SchedulePoint (\w+)\[STOCK_POINTS\] = \{(.*?)\n  \};", re.S)
YAML_SLOT = re.compile(r'slot: (\d+).*?initial_value:\s*"([^"]*)"')


def cpp_points(source: str) -> dict[str, list[tuple[int, tuple[int, ...], bool]]]:
    result: dict[str, list[tuple[int, tuple[int, ...], bool]]] = {}
    for name, body in CPP_ARRAY.findall(source):
        points = []
        for minute, star, *channels, enabled in CPP_POINT.findall(body):
            points.append((int(minute) * 60 if star else int(minute),
                           tuple(int(c) for c in channels), enabled == "true"))
        result[name] = points
    return result


def yaml_points(model: str, text: str) -> list[tuple[int, tuple[int, ...], bool]]:
    slots: dict[int, tuple[int, tuple[int, ...], bool]] = {}
    for index, value in YAML_SLOT.findall(text):
        slots[int(index)] = parse_initial(int(index), value.strip(), LABELS[model])
    expected = list(range(1, 17))
    if sorted(slots) != expected:
        raise AssertionError(f"{TEMPLATES[model].name}: slots {sorted(slots)} != 1..16")
    return [slots[i] for i in expected]


def parse_initial(index: int, value: str, labels: dict[str, int]) -> tuple[int, tuple[int, ...], bool]:
    if value == "off":
        return 0, (0, 0, 0, 0, 0), False
    match = re.match(r"(\d{1,2}):(\d{2})(?:\s+(.*))?$", value)
    if not match:
        raise AssertionError(f"slot {index}: cannot parse {value!r}")
    hour, minute = int(match.group(1)), int(match.group(2))
    if hour > 23 or minute > 59:
        raise AssertionError(f"slot {index}: bad time {value!r}")
    channels = [0] * 5
    for token in (match.group(3) or "").split():
        key, _, raw = token.partition("=")
        if key not in labels:
            raise AssertionError(f"slot {index}: unknown channel {key!r}")
        channels[labels[key]] = int(raw)
    return hour * 60 + minute, tuple(channels), True


def main() -> int:
    failures = 0
    cpp = cpp_points(HEADER.read_text())
    for model, path in TEMPLATES.items():
        try:
            expected = yaml_points(model, path.read_text())
        except AssertionError as exc:
            print(f"FAIL stock defaults: {exc}")
            failures += 1
            continue
        actual = cpp.get(model)
        if actual != expected:
            print(f"FAIL stock defaults: {model} differs between {HEADER.name} and {path.name}")
            for i, (a, b) in enumerate(zip(actual or [], expected)):
                if a != b:
                    print(f"  slot {i + 1}: C++ {a} != YAML {b}")
            if actual is not None and len(actual) != len(expected):
                print(f"  count: C++ {len(actual)} != YAML {len(expected)}")
            failures += 1
    if failures:
        return 1
    print("stock defaults match the YAML templates")
    return 0


if __name__ == "__main__":
    sys.exit(main())
