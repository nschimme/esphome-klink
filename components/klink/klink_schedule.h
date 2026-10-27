#pragma once
// Daily schedule: a handful of (time, five channel bytes) points, linearly
// interpolated with wrap past midnight. No ESPHome dependencies (host tested).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace kessil {
namespace klink {

static const size_t NUM_CHANNELS = 5;
static const uint32_t SECONDS_PER_DAY = 86400;
// UI1-UI2 are full-range bytes; UI3-UI5 stop at 100.
static const uint8_t CHANNEL_MAX[NUM_CHANNELS] = {255, 255, 100, 100, 100};

struct SchedulePoint {
  uint16_t minute;  // minute of day, 0-1439
  uint8_t channels[NUM_CHANNELS];
  bool enabled;
};

inline uint8_t clamp_channel(size_t index, int value) {
  if (value < 0)
    return 0;
  return static_cast<uint8_t>(value > CHANNEL_MAX[index] ? CHANNEL_MAX[index] : value);
}

// The two points a time of day lies between, and how far along the way it is. False when no point
// is enabled. One enabled point holds its value all day (span 0). Otherwise the value moves linearly
// from the latest point at or before now to the next one, wrapping across midnight.
struct ScheduleSegment {
  SchedulePoint from;
  SchedulePoint to;
  uint32_t span;     // seconds between the two points
  uint32_t elapsed;  // seconds since `from`
};

inline bool schedule_locate(const std::vector<SchedulePoint> &points, uint32_t second_of_day,
                            ScheduleSegment &seg) {
  std::vector<SchedulePoint> on;
  for (size_t i = 0; i < points.size(); i++)
    if (points[i].enabled && points[i].minute < 1440)
      on.push_back(points[i]);
  if (on.empty())
    return false;
  std::stable_sort(on.begin(), on.end(),
                   [](const SchedulePoint &a, const SchedulePoint &b) { return a.minute < b.minute; });
  second_of_day %= SECONDS_PER_DAY;

  size_t prev = on.size() - 1;  // before the first point of the day: the last one, from yesterday
  for (size_t i = 0; i < on.size(); i++)
    if (static_cast<uint32_t>(on[i].minute) * 60 <= second_of_day)
      prev = i;
  size_t next = (prev + 1) % on.size();

  uint32_t t0 = static_cast<uint32_t>(on[prev].minute) * 60;
  uint32_t t1 = static_cast<uint32_t>(on[next].minute) * 60;
  seg.from = on[prev];
  seg.to = on[next];
  seg.span = (t1 + SECONDS_PER_DAY - t0) % SECONDS_PER_DAY;  // equal minutes or a single point: 0
  seg.elapsed = (second_of_day + SECONDS_PER_DAY - t0) % SECONDS_PER_DAY;
  return true;
}

// Compute the channels for `second_of_day` (0-86399). Returns false when no point is enabled.
inline bool schedule_compute(const std::vector<SchedulePoint> &points, uint32_t second_of_day,
                             uint8_t out[NUM_CHANNELS]) {
  ScheduleSegment seg;
  if (!schedule_locate(points, second_of_day, seg))
    return false;
  for (size_t c = 0; c < NUM_CHANNELS; c++) {
    int a = seg.from.channels[c], b = seg.to.channels[c];
    int v = a;
    if (seg.span != 0) {
      // Floor division (not rounding), so a ramp from 51 to 102 reads 93 where the exact value is 93.78.
      int64_t delta = static_cast<int64_t>(b - a) * seg.elapsed;
      int64_t step = delta >= 0 ? delta / seg.span : -((-delta + seg.span - 1) / seg.span);
      v = a + static_cast<int>(step);
    }
    out[c] = clamp_channel(c, v);
  }
  return true;
}

// True when every channel of `a` is within `tolerance` of `b`. Used to compare our schedule with
// another controller's writes: both sides tick at different moments, so a value can differ by 1.
inline bool channels_close(const uint8_t a[NUM_CHANNELS], const uint8_t b[NUM_CHANNELS], int tolerance) {
  for (size_t c = 0; c < NUM_CHANNELS; c++) {
    int d = static_cast<int>(a[c]) - static_cast<int>(b[c]);
    if (d > tolerance || d < -tolerance)
      return false;
  }
  return true;
}

// Channel names for the labeled slot form, one per UI1-UI5; nullptr marks a channel the model has
// no control for (it is left at 0 and not shown). Matches Kessil's own program screen.
struct SlotLabels {
  const char *name[NUM_CHANNELS];
};
static const SlotLabels LABELS_TUNA_SUN = {{"int", "clr", nullptr, "r", "am"}};
static const SlotLabels LABELS_TUNA_BLUE = {{"int", "clr", "vio", "r", "grn"}};
static const SlotLabels LABELS_GENERIC = {{"ui1", "ui2", "ui3", "ui4", "ui5"}};

// Slot text, as edited in Home Assistant: "HH:MM u1 u2 u3 u4 u5" (spaces or commas), or "off"/"" for
// an unused slot. Strict: out-of-range values are rejected, never clamped, so a typo cannot
// silently change a light level. Seconds in the time ("HH:MM:SS") are accepted and ignored.
inline bool parse_slot(const std::string &text, SchedulePoint &out, const SlotLabels *labels = nullptr) {
  std::vector<std::string> tok;
  std::string cur;
  for (size_t i = 0; i <= text.size(); i++) {
    char c = i < text.size() ? text[i] : ' ';
    if (c == ' ' || c == ',' || c == '\t') {
      if (!cur.empty())
        tok.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.minute = 0;
  for (size_t c = 0; c < NUM_CHANNELS; c++)
    out.channels[c] = 0;
  out.enabled = false;
  if (tok.empty() || tok[0] == "off" || tok[0] == "OFF" || tok[0] == "Off")
    return tok.size() <= 1;
  // Labeled form, e.g. "13:00 int=153 clr=89 r=25": any subset of the model's channels, the rest 0.
  bool labeled = tok.size() > 1 && tok[1].find('=') != std::string::npos;
  if (!labeled && tok.size() != 1 + NUM_CHANNELS)
    return false;
  if (labeled && labels == nullptr)
    return false;

  int part[3] = {0, 0, 0};
  size_t parts = 0;
  std::string num;
  for (size_t i = 0; i <= tok[0].size(); i++) {
    char c = i < tok[0].size() ? tok[0][i] : ':';
    if (c == ':') {
      if (num.empty() || num.size() > 2 || parts >= 3)
        return false;
      part[parts++] = (num.size() == 2) ? (num[0] - '0') * 10 + (num[1] - '0') : num[0] - '0';
      num.clear();
    } else if (c >= '0' && c <= '9') {
      num.push_back(c);
    } else {
      return false;
    }
  }
  if (parts < 2 || part[0] > 23 || part[1] > 59 || part[2] > 59)
    return false;
  out.minute = static_cast<uint16_t>(part[0] * 60 + part[1]);

  if (labeled) {
    bool seen[NUM_CHANNELS] = {false, false, false, false, false};
    for (size_t i = 1; i < tok.size(); i++) {
      size_t eq = tok[i].find('=');
      if (eq == std::string::npos)
        return false;
      std::string key = tok[i].substr(0, eq), val = tok[i].substr(eq + 1);
      for (size_t k = 0; k < key.size(); k++)
        key[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(key[k])));
      size_t c = NUM_CHANNELS;
      for (size_t k = 0; k < NUM_CHANNELS; k++)
        if (labels->name[k] != nullptr && key == labels->name[k])
          c = k;
      if (c == NUM_CHANNELS || seen[c] || val.empty() || val.size() > 3)
        return false;
      int v = 0;
      for (size_t k = 0; k < val.size(); k++) {
        if (val[k] < '0' || val[k] > '9')
          return false;
        v = v * 10 + (val[k] - '0');
      }
      if (v > CHANNEL_MAX[c])
        return false;
      seen[c] = true;
      out.channels[c] = static_cast<uint8_t>(v);
    }
    out.enabled = true;
    return true;
  }

  for (size_t c = 0; c < NUM_CHANNELS; c++) {
    const std::string &t = tok[1 + c];
    if (t.empty() || t.size() > 3)
      return false;
    int v = 0;
    for (size_t i = 0; i < t.size(); i++) {
      if (t[i] < '0' || t[i] > '9')
        return false;
      v = v * 10 + (t[i] - '0');
    }
    if (v > CHANNEL_MAX[c])
      return false;
    out.channels[c] = static_cast<uint8_t>(v);
  }
  out.enabled = true;
  return true;
}

inline std::string format_slot(const SchedulePoint &p, const SlotLabels *labels = nullptr) {
  if (!p.enabled)
    return "off";
  if (labels != nullptr) {
    char lab[64];
    int n = std::snprintf(lab, sizeof(lab), "%02u:%02u", p.minute / 60u, p.minute % 60u);
    for (size_t c = 0; c < NUM_CHANNELS && n > 0 && n < static_cast<int>(sizeof(lab)); c++)
      if (labels->name[c] != nullptr)
        n += std::snprintf(lab + n, sizeof(lab) - n, " %s=%u", labels->name[c], p.channels[c]);
    return lab;
  }
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%02u:%02u %u %u %u %u %u", p.minute / 60u, p.minute % 60u, p.channels[0],
                p.channels[1], p.channels[2], p.channels[3], p.channels[4]);
  return buf;
}

}  // namespace klink
}  // namespace kessil
