#pragma once
// Dynamic schedules: a day of sun or moon events turned into schedule points, so a light follows the
// real sky instead of a fixed table. Pure math on top of klink_schedule.h; no ESPHome dependencies
// (host tested). Angles are the caller's: astro -18, nautical -12, civil -6, horizon -0.833,
// golden +6 degrees; no event on that day is -1.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "klink_schedule.h"

namespace kessil {
namespace klink {

// Which channels this fixture has: a generic light only knows intensity and color, Tuna Sun adds
// red and amber, Tuna Blue adds violet.
enum DynModel {
  DYN_GENERIC = 0,
  DYN_SUN = 1,
  DYN_BLUE = 2,
  // Ordinary lights driven by a light program (channel layouts from klink_light_map.h): brightness only,
  // intensity plus red/green/blue (UI3-UI5, percent), and intensity plus white (UI2) plus red/green/blue.
  // A tunable-white light uses DYN_GENERIC: UI2 is its color temperature, warm at 0.
  DYN_LIGHT_BRIGHTNESS = 3,
  DYN_LIGHT_RGB = 4,
  DYN_LIGHT_RGBW = 5,
};

// One local day of sun events, in minutes; -1 where the event does not happen that day.
struct SunDay {
  int astro_dawn = -1, nautical_dawn = -1, civil_dawn = -1, sunrise = -1, golden_am = -1, noon = -1,
      golden_pm = -1, sunset = -1, civil_dusk = -1, nautical_dusk = -1, astro_dusk = -1;
  bool always_up = false;  // polar day: the sun never drops below the sunrise elevation
};

struct SunParams {
  int peak_int = 130;
  int floor_int = 0;
  int clr_warm = 0;
  int clr_cool = 120;
  int glow = 100;      // percent, full scale of the red/amber/violet glow channels
  int dip = 0;         // percent taken off the noon intensity, for a midday siesta
  int shift_min = 0;   // minutes added to every point (e.g. the user's local offset)
  int night_amber = 0;  // percent, DYN_SUN only: amber floor at the dark end of the night
  // Minutes from sunrise to near-peak intensity (and the same before sunset): a tank wants a gentler morning
  // than the real sun gives. 0 keeps the real sun, which reaches 85 % within about 40 minutes.
  int ramp_min = 0;
};

struct MoonNight {
  int rise = -1;            // minutes of the local day, -1 = no rise/set that day
  int set = -1;
  double illumination = 0.0;  // 0..1
};

struct MoonParams {
  int max_int = 12;
  bool follow_phase = true;
  int clr = 255;
  int violet = 60;     // percent at full intensity, DYN_BLUE only
  int shift_min = 0;
  int amber = 0;       // percent at full intensity, DYN_SUN only
};

// Round half up, also for the negative values a silly parameter could produce.
inline int dyn_round(double x) {
  return x >= 0.0 ? static_cast<int>(x + 0.5) : -static_cast<int>(-x + 0.5);
}

// Wrap any minute into 0..1439.
inline uint16_t dyn_wrap_minute(int minute) {
  int m = minute % 1440;
  if (m < 0)
    m += 1440;
  return static_cast<uint16_t>(m);
}

// A warm-to-cool white, tinted toward deep orange by the glow, as red/green/blue percentages for lights
// that mix their color. The moon is a cool blue.
inline void dyn_rgb(int clr, double glow_fraction, bool moon, int out[3]) {
  static const double WARM[3] = {100, 62, 24}, COOL[3] = {78, 90, 100}, GLOW[3] = {100, 38, 6}, MOON[3] = {45, 62, 100};
  const double c = clr < 0 ? 0.0 : (clr > 255 ? 1.0 : clr / 255.0);
  for (int k = 0; k < 3; k++) {
    double v = moon ? MOON[k] : WARM[k] + (COOL[k] - WARM[k]) * c;
    if (!moon)
      v += (GLOW[k] - v) * (glow_fraction < 0 ? 0 : (glow_fraction > 1 ? 1 : glow_fraction));
    out[k] = dyn_round(v);
  }
}

// One sun point: intensity and color interpolate between the floor/peak and warm/cool spans, the
// glow channels follow g (0..1) times the glow percent. `f` is the point's intensity fraction; on a
// Tuna Sun it lifts amber at the dark end of the night (night_amber, full at f = 0 and gone by 0.15).
inline SchedulePoint dyn_sun_point(int minute, int inten, int clr, double g, double f,
                                   const SunParams &p, DynModel model) {
  SchedulePoint pt;
  pt.minute = dyn_wrap_minute(minute);
  for (size_t c = 0; c < NUM_CHANNELS; c++)
    pt.channels[c] = 0;
  pt.channels[0] = clamp_channel(0, inten);
  pt.channels[1] = clamp_channel(1, clr);
  double glow = g * p.glow;
  if (model == DYN_LIGHT_BRIGHTNESS) {
    pt.channels[1] = 0;
  } else if (model == DYN_LIGHT_RGB || model == DYN_LIGHT_RGBW) {
    int rgb[3];
    dyn_rgb(clr, glow / 100.0, false, rgb);
    pt.channels[1] = model == DYN_LIGHT_RGBW ? clamp_channel(1, clr) : 0;  // white level follows the cool end
    for (int k = 0; k < 3; k++)
      pt.channels[2 + k] = clamp_channel(2 + k, rgb[k]);
  } else if (model == DYN_SUN) {
    pt.channels[3] = clamp_channel(3, dyn_round(glow * 0.5));
    int glow_amber = dyn_round(glow);
    int night = dyn_round(p.night_amber * std::max(0.0, 1.0 - f / 0.15));
    pt.channels[4] = clamp_channel(4, night > glow_amber ? night : glow_amber);
  } else if (model == DYN_BLUE) {
    pt.channels[2] = clamp_channel(2, dyn_round(glow));
  }
  pt.enabled = true;
  return pt;
}

// Sort ascending and separate points that landed on the same minute by moving the later one a minute
// on; one that would pass 1439 is dropped. Never more than 16 points.
inline void dyn_finish(std::vector<SchedulePoint> &points) {
  std::stable_sort(points.begin(), points.end(),
                   [](const SchedulePoint &a, const SchedulePoint &b) { return a.minute < b.minute; });
  std::vector<SchedulePoint> kept;
  kept.reserve(points.size());
  for (size_t i = 0; i < points.size(); i++) {
    SchedulePoint pt = points[i];
    if (!kept.empty() && pt.minute <= kept.back().minute) {
      int m = static_cast<int>(kept.back().minute) + 1;
      if (m > 1439)
        continue;
      pt.minute = static_cast<uint16_t>(m);
    }
    kept.push_back(pt);
  }
  if (kept.size() > 16)
    kept.resize(16);
  points.swap(kept);
}

// The day's sun events as schedule points. Morning and evening mirror each other; noon is the peak,
// scaled down by the dip. Polar day gives one noon point that holds all day; a day without any event
// gives one point at the floor. Missing single events (e.g. no astronomical twilight in a northern
// summer) are simply left out, and on a day the sun never rises (polar night) there is no noon peak:
// the twilight level is the brightest the sky gets.
inline std::vector<SchedulePoint> sun_points(const SunDay &real_day, const SunParams &p, DynModel model) {
  SunDay day = real_day;
  // A gentler ramp: the "golden hour" point (85 % of the peak) moves to ramp_min after sunrise and before
  // sunset, never closer than half an hour to solar noon.
  if (p.ramp_min > 0 && day.noon >= 0) {
    if (day.sunrise >= 0) {
      int g = day.sunrise + p.ramp_min;
      if (g > day.noon - 30)
        g = day.noon - 30;
      if (g > day.sunrise)
        day.golden_am = g;
    }
    if (day.sunset >= 0) {
      int g = day.sunset - p.ramp_min;
      if (g < day.noon + 30)
        g = day.noon + 30;
      if (g < day.sunset)
        day.golden_pm = g;
    }
  }
  std::vector<SchedulePoint> out;
  const int span = p.peak_int - p.floor_int;
  const double dip_scale = 1.0 - p.dip / 100.0;

  if (day.always_up) {
    out.push_back(dyn_sun_point(720 + p.shift_min, dyn_round(p.floor_int + dip_scale * span),
                                dyn_round(p.clr_cool), 0.0, dip_scale, p, model));
    return out;
  }

  struct DynEvent {
    int minute;
    double f;  // fraction of floor..peak
    double c;  // fraction of warm..cool
    double g;  // fraction of the glow percent
  };
  // Noon is only a peak on a day the sun actually rises: in polar night the twilight level is the
  // brightest the sky gets, so there is nothing to climb to. (Polar day returned above.)
  const int noon_minute = day.sunrise >= 0 ? day.noon : -1;
  const DynEvent events[] = {
      {day.astro_dawn, 0.00, 0.0, 0.00},      {day.nautical_dawn, 0.04, 0.0, 0.10},
      {day.civil_dawn, 0.15, 0.0, 0.60},      {day.sunrise, 0.35, 0.2, 1.00},
      {day.golden_am, 0.85, 0.8, 0.00},       {noon_minute, dip_scale, 1.0, 0.00},
      {day.golden_pm, 0.85, 0.8, 0.00},       {day.sunset, 0.35, 0.2, 1.00},
      {day.civil_dusk, 0.15, 0.0, 0.60},      {day.nautical_dusk, 0.04, 0.0, 0.10},
      {day.astro_dusk, 0.00, 0.0, 0.00},
  };
  const size_t n = sizeof(events) / sizeof(events[0]);
  for (size_t i = 0; i < n; i++) {
    if (events[i].minute < 0)
      continue;
    int inten = dyn_round(p.floor_int + events[i].f * span);
    int clr = dyn_round(p.clr_warm + events[i].c * (p.clr_cool - p.clr_warm));
    out.push_back(dyn_sun_point(events[i].minute + p.shift_min, inten, clr, events[i].g,
                                events[i].f, p, model));
  }
  if (out.empty())  // polar night: hold the floor color, no glow
    out.push_back(dyn_sun_point(p.shift_min, p.floor_int, p.clr_warm, 0.0, 0.0, p, model));
  dyn_finish(out);
  return out;
}

// One moon point: intensity 0 unless it is the peak, color always the same, violet (Tuna Blue) and
// amber (Tuna Sun) scaled by the same intensity fraction.
inline SchedulePoint dyn_moon_point(int minute, int inten, int violet, int amber, const MoonParams &p,
                                    DynModel model) {
  SchedulePoint pt;
  pt.minute = dyn_wrap_minute(minute);
  for (size_t c = 0; c < NUM_CHANNELS; c++)
    pt.channels[c] = 0;
  pt.channels[0] = clamp_channel(0, inten);
  pt.channels[1] = clamp_channel(1, p.clr);
  if (model == DYN_BLUE) {
    pt.channels[2] = clamp_channel(2, violet);
  } else if (model == DYN_LIGHT_BRIGHTNESS) {
    pt.channels[1] = 0;
  } else if (model == DYN_LIGHT_RGB || model == DYN_LIGHT_RGBW) {
    int rgb[3];
    dyn_rgb(p.clr, 0.0, true, rgb);
    pt.channels[1] = 0;  // no white at night
    for (int k = 0; k < 3; k++)
      pt.channels[2 + k] = clamp_channel(2 + k, rgb[k]);
  }
  else if (model == DYN_SUN)
    pt.channels[4] = clamp_channel(4, amber);
  pt.enabled = true;
  return pt;
}

// Rise, peak and set of the moon. The peak sits at the midpoint of the interval, counting a set
// before the rise as after midnight. A day without a usable interval (moon up or down all day) holds
// intensity 0.
inline std::vector<SchedulePoint> moon_points(const MoonNight &n, const MoonParams &p, DynModel model) {
  std::vector<SchedulePoint> out;
  if (n.rise < 0 || n.set < 0) {
    out.push_back(dyn_moon_point(p.shift_min, 0, 0, 0, p, model));
    return out;
  }
  double frac = p.follow_phase ? n.illumination : 1.0;
  int peak_int = p.max_int > 0 ? dyn_round(p.max_int * frac) : 0;
  double violet_frac = p.max_int > 0 ? static_cast<double>(peak_int) / p.max_int : 0.0;
  int violet = dyn_round(p.violet * violet_frac);
  int amber = dyn_round(p.amber * violet_frac);
  int end = n.set < n.rise ? n.set + 1440 : n.set;
  int peak = (n.rise + (end - n.rise) / 2) % 1440;

  out.push_back(dyn_moon_point(n.rise + p.shift_min, 0, 0, 0, p, model));
  out.push_back(dyn_moon_point(peak + p.shift_min, peak_int, violet, amber, p, model));
  out.push_back(dyn_moon_point(n.set + p.shift_min, 0, 0, 0, p, model));
  dyn_finish(out);
  return out;
}

}  // namespace klink
}  // namespace kessil
