"""Time zone -> representative city, the last-resort location for the sun and moon programs.

Standard library only (like klink_colors.py), so it can be tested without ESPHome. The data is the zone.tab
that ships with the tzdata package (ESPHome depends on it).
"""
import re


def iso6709(text):
    """'+4153-08737' or '+415310-0873710' -> (latitude, longitude) in degrees, or None."""
    m = re.fullmatch(r"([+-])(\d{2})(\d{2})(\d{2})?([+-])(\d{3})(\d{2})(\d{2})?", text)
    if not m:
        return None
    lat = int(m.group(2)) + int(m.group(3)) / 60.0 + int(m.group(4) or 0) / 3600.0
    lon = int(m.group(6)) + int(m.group(7)) / 60.0 + int(m.group(8) or 0) / 3600.0
    return (-lat if m.group(1) == "-" else lat, -lon if m.group(5) == "-" else lon)


def timezone_location(name):
    """The representative city's (latitude, longitude) for an IANA time zone name, or None.

    An approximate location is enough for the sun and moon programs; Home Assistant's zone.home or the
    latitude and longitude entities give exact ones.
    """
    try:
        from importlib import resources

        text = (resources.files("tzdata") / "zoneinfo" / "zone.tab").read_text(encoding="utf-8")
    except Exception:  # noqa: BLE001 - no tzdata or no file: no fallback location, nothing else breaks
        return None
    for line in text.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) >= 3 and parts[2] == name:
            return iso6709(parts[1])
    return None
