#pragma once
// Mapping a daily program (five channel bytes) onto an ordinary ESPHome light:
// which capabilities the lights of a group share, the channel labels, the
// channel -> light values conversion and the echo/refresh rules that keep the
// program from fighting a user who changes the light by hand. Pure logic, no
// ESPHome dependencies (host tested).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "klink_schedule.h"

namespace kessil {
namespace klink {

// Capabilities a light entity can offer. CAP_BRIGHTNESS is assumed on every
// light, so it is not really optional; it is a bit so caps_common() can return
// a plain mask.
enum LightCap : uint8_t { CAP_BRIGHTNESS = 1, CAP_CT = 2, CAP_RGB = 4, CAP_WHITE = 8 };

// The light shape a group of lights is driven with. It is derived from the
// capabilities all of them share: an entity that cannot do everything is left
// out of the intersection, never driven with a value it cannot represent.
enum class LightModel : uint8_t { BRIGHTNESS, CT, RGB, RGBW };

static const uint8_t LIGHT_CAPS_ALL =
    static_cast<uint8_t>(CAP_BRIGHTNESS | CAP_CT | CAP_RGB | CAP_WHITE);  // 15

// Bitwise AND of every light's caps. The result always includes CAP_BRIGHTNESS
// (a light without brightness is not something we can drive), and undefined
// bits are dropped. No lights at all means brightness only.
inline uint8_t caps_common(const uint8_t *caps, size_t n) {
  if (caps == nullptr || n == 0)
    return CAP_BRIGHTNESS;
  uint8_t common = LIGHT_CAPS_ALL;
  for (size_t i = 0; i < n; i++)
    common &= caps[i];
  return static_cast<uint8_t>((common & LIGHT_CAPS_ALL) | CAP_BRIGHTNESS);
}

// Colour (RGB or RGBW) wins over colour temperature when a group offers both:
// a colour value cannot be expressed as a temperature, but a white channel can
// stand in for the temperature.
inline LightModel model_for_caps(uint8_t caps) {
  if ((caps & CAP_RGB) != 0)
    return (caps & CAP_WHITE) != 0 ? LightModel::RGBW : LightModel::RGB;
  if ((caps & CAP_CT) != 0)
    return LightModel::CT;
  return LightModel::BRIGHTNESS;
}

inline const char *light_model_name(LightModel model) {
  switch (model) {
    case LightModel::BRIGHTNESS:
      return "light_brightness";
    case LightModel::CT:
      return "light_ct";
    case LightModel::RGB:
      return "light_rgb";
    case LightModel::RGBW:
      return "light_rgbw";
  }
  return "";
}

// Channel labels per model, one per UI1-UI5 (same struct as the program slots):
// UI1 is always intensity, UI2 is temperature or white, UI3-UI5 are RGB.
inline const SlotLabels *light_labels(LightModel model) {
  static const SlotLabels kBrightness = {{"int", nullptr, nullptr, nullptr, nullptr}};
  static const SlotLabels kCt = {{"int", "ct", nullptr, nullptr, nullptr}};
  static const SlotLabels kRgb = {{"int", nullptr, "r", "g", "b"}};
  static const SlotLabels kRgbw = {{"int", "w", "r", "g", "b"}};
  switch (model) {
    case LightModel::BRIGHTNESS:
      return &kBrightness;
    case LightModel::CT:
      return &kCt;
    case LightModel::RGB:
      return &kRgb;
    case LightModel::RGBW:
      return &kRgbw;
  }
  return nullptr;
}

// What a light entity is asked to show. brightness/r/g/b/w are 0..1. mireds is
// in the light's own units and only meaningful for the CT model.
struct LightValues {
  bool on;
  float brightness;
  float mireds;
  float r;
  float g;
  float b;
  float w;
};

// The colour temperature range of the target light: cold end at min_mireds,
// warm end at max_mireds.
struct CtRange {
  float min_mireds;
  float max_mireds;
};

// Kessil's UI2 has 0 at the WARM end and 255 at the cold end, so the program
// drives the warm end first. Ranges handed to us come from user configuration,
// so an unusable one (reversed or non-positive) falls back to a typical
// warm-white tunable light instead of dividing by zero.
static const float DEFAULT_MIN_MIREDS = 153.0f;
static const float DEFAULT_MAX_MIREDS = 370.0f;

inline CtRange resolve_ct_range(CtRange range) {
  CtRange out;
  if (range.min_mireds > 0.0f && range.min_mireds < range.max_mireds) {
    out.min_mireds = range.min_mireds;
    out.max_mireds = range.max_mireds;
  } else {
    out.min_mireds = DEFAULT_MIN_MIREDS;
    out.max_mireds = DEFAULT_MAX_MIREDS;
  }
  return out;
}

inline float clamp01(float v) {
  if (!(v > 0.0f))  // also maps NaN to 0
    return 0.0f;
  return v > 1.0f ? 1.0f : v;
}

inline float fraction_from_mireds(float mireds, CtRange range) {
  CtRange r = resolve_ct_range(range);
  float span = r.max_mireds - r.min_mireds;  // > 0 after resolve_ct_range
  return clamp01((r.max_mireds - mireds) / span);
}

// Channels -> light values. Fields the model does not use stay 0.
inline LightValues to_light_values(LightModel model, const uint8_t ch[NUM_CHANNELS], CtRange range) {
  LightValues v = LightValues{};
  v.brightness = ch[0] / 255.0f;
  v.on = ch[0] > 0;
  CtRange r = resolve_ct_range(range);
  switch (model) {
    case LightModel::BRIGHTNESS:
      break;
    case LightModel::CT:
      v.mireds = r.max_mireds - (ch[1] / 255.0f) * (r.max_mireds - r.min_mireds);
      break;
    case LightModel::RGB:
      v.r = clamp01(ch[2] / 100.0f);
      v.g = clamp01(ch[3] / 100.0f);
      v.b = clamp01(ch[4] / 100.0f);
      break;
    case LightModel::RGBW:
      v.w = ch[1] / 255.0f;
      v.r = clamp01(ch[2] / 100.0f);
      v.g = clamp01(ch[3] / 100.0f);
      v.b = clamp01(ch[4] / 100.0f);
      break;
  }
  return v;
}

// How far an observed light may be from what the program asked for and still
// count as our own write coming back. ESPHome rounds and re-reports brightness
// and colour, so an exact compare would treat every update as a manual change.
struct MatchTolerance {
  float brightness;
  float mireds_fraction;
  float color;

  static inline MatchTolerance default_tolerance() { return {0.02f, 0.03f, 0.02f}; }
};

// True when `observed` is our `expected` state echoed back. Both off matches
// regardless of the other fields (a light that is off remembers its last
// brightness, which we do not care about). Fields the model does not use are
// ignored.
inline bool matches_expected(LightModel model, const LightValues &expected, const LightValues &observed,
                             CtRange range, MatchTolerance tol) {
  if (expected.on != observed.on)
    return false;
  if (!expected.on && !observed.on)
    return true;
  if (std::fabs(expected.brightness - observed.brightness) > tol.brightness)
    return false;
  switch (model) {
    case LightModel::BRIGHTNESS:
      return true;
    case LightModel::CT: {
      float a = fraction_from_mireds(expected.mireds, range);
      float b = fraction_from_mireds(observed.mireds, range);
      return std::fabs(a - b) <= tol.mireds_fraction;
    }
    case LightModel::RGB:
      return std::fabs(expected.r - observed.r) <= tol.color &&
             std::fabs(expected.g - observed.g) <= tol.color &&
             std::fabs(expected.b - observed.b) <= tol.color;
    case LightModel::RGBW:
      return std::fabs(expected.r - observed.r) <= tol.color &&
             std::fabs(expected.g - observed.g) <= tol.color &&
             std::fabs(expected.b - observed.b) <= tol.color &&
             std::fabs(expected.w - observed.w) <= tol.color;
  }
  return false;
}

inline bool channels_differ(const uint8_t *a, const uint8_t *b) {
  for (size_t c = 0; c < NUM_CHANNELS; c++)
    if (a[c] != b[c])
      return true;
  return false;
}

// Decide whether to write to the light again. A periodic refresh re-asserts our
// value so a dropped BLE command is repaired; refresh_ms == 0 disables it, so
// an unchanged program costs nothing. Passing `now` equal to `last` (an
// unchanged schedule) with refresh on still refreshes, by design.
inline bool needs_light_update(bool have_applied, const uint8_t *last, const uint8_t *now,
                               uint32_t ms_since_last, uint32_t refresh_ms) {
  if (!have_applied)
    return true;
  if (channels_differ(last, now))
    return true;
  return refresh_ms != 0 && ms_since_last >= refresh_ms;
}

// Program ids live above the bus group ids so both can share one id space:
// 1..40 are bus groups (MIN_GID/MAX_GID in klink_inventory.h; not included here
// to keep this header free of that dependency), 101..164 are our programs.
static const uint8_t PROGRAM_ID_MIN = 101;
static const uint8_t PROGRAM_ID_MAX = 164;
static const uint8_t BUS_GID_MIN = 1;
static const uint8_t BUS_GID_MAX = 40;
static const size_t NUM_PROGRAM_IDS = 64;

inline bool is_program_id(uint32_t id) { return id >= PROGRAM_ID_MIN && id <= PROGRAM_ID_MAX; }

inline bool is_bus_gid(uint32_t id) { return id >= BUS_GID_MIN && id <= BUS_GID_MAX; }

inline bool program_index_valid(size_t index) { return index < NUM_PROGRAM_IDS; }

// Index -> program id. Out-of-range indexes clamp to the last id so callers that
// forget to check still get a valid id rather than wrapping; check
// program_index_valid() first to reject them.
inline uint8_t program_id_for_index(size_t index) {
  if (index >= NUM_PROGRAM_IDS)
    return PROGRAM_ID_MAX;
  return static_cast<uint8_t>(PROGRAM_ID_MIN + index);
}

}  // namespace klink
}  // namespace kessil
