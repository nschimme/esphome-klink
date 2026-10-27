#pragma once
// Assemble a whole day/night from the astronomy: a SunDay for one local date, the dark part of the
// moon's night, point simplification and a fixed photoperiod. Pure math on top of klink_astro.h and
// klink_dynamic.h; no ESPHome dependencies (host tested). Minutes are minutes of the local day
// unless stated otherwise, and -1 means the event does not happen.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "klink_astro.h"
#include "klink_dynamic.h"

namespace kessil {
namespace klink {

// First crossing in an arbitrary [start, end] minute window measured from local midnight of the
// day whose Julian Day is jd0, refined by bisection to tol minutes. Used by moon_night, where the
// window spans two calendar days and astro_crossing's single-day scan does not fit.
inline bool sky_window_crossing(double (*elev)(double, double, double), double jd0, double start,
                                double end, double lat, double lon, double angle, bool rising,
                                double step, double tol, double *minute) {
  if (end <= start)
    return false;
  double t = start;
  double prev = elev(jd0 + start / 1440.0, lat, lon);
  while (t < end) {
    double t1 = t + step;
    if (t1 > end)
      t1 = end;
    double cur = elev(jd0 + t1 / 1440.0, lat, lon);
    bool crossed = rising ? (prev < angle && cur >= angle) : (prev >= angle && cur < angle);
    if (crossed) {
      double lo = t, hi = t1;
      while (hi - lo > tol) {
        double mid = 0.5 * (lo + hi);
        double em = elev(jd0 + mid / 1440.0, lat, lon);
        if ((em >= angle) == rising)
          hi = mid;
        else
          lo = mid;
      }
      *minute = 0.5 * (lo + hi);
      return true;
    }
    prev = cur;
    t = t1;
  }
  return false;
}

// Sun elevation along one local day, cheap enough for a small chip: declination and the equation of time
// are computed at the two local midnights and interpolated (they change slowly, so the error is a few
// seconds of time), leaving three trig calls per sample.
struct SkyDayElevation {
  double jd_mid, dec0, eq0, dec1, eq1, lat_sin, lat_cos, lon;
  SkyDayElevation(int y, int m, int d, double lat, double lon_deg, int tz_min) {
    jd_mid = julian_day(y, m, d, 0.0) - tz_min / 1440.0;
    sun_decl_eqtime(jd_mid, &dec0, &eq0);
    sun_decl_eqtime(jd_mid + 1.0, &dec1, &eq1);
    lat_sin = sind(lat);
    lat_cos = cosd(lat);
    lon = lon_deg;
  }
  double at(double minute) const {
    const double f = minute / 1440.0;
    const double dec = dec0 + (dec1 - dec0) * f;
    const double eq = eq0 + (eq1 - eq0) * f;
    const double jd = jd_mid + f;
    const double ut_min = (jd - (std::floor(jd + 0.5) - 0.5)) * 1440.0;
    const double ha = (ut_min + eq + 4.0 * lon) / 4.0 - 180.0;
    return asind(lat_sin * sind(dec) + lat_cos * cosd(dec) * cosd(ha));
  }
};

// One local day of sun events, as SunDay expects them: dawn events rising, dusk events falling, noon
// always filled. One pass over the day at 10 minute steps finds every crossing, refined by bisection to
// half a minute: smooth transitions matter more than the exact minute. A crossing rounded past midnight is pinned to 23:59 so the events stay wrap-free.
inline SunDay sun_day(int y, int m, int d, double lat, double lon, int tz_min) {
  SunDay day;
  const SkyDayElevation sky(y, m, d, lat, lon, tz_min);
  static const int STEPS = 144;  // 10 minute steps over the day
  double e[STEPS + 1];
  for (int i = 0; i <= STEPS; i++)
    e[i] = sky.at(i * 10.0);
  auto event = [&](double angle, bool rising) -> int {
    for (int i = 0; i < STEPS; i++) {
      const bool crossed = rising ? (e[i] < angle && e[i + 1] >= angle) : (e[i] >= angle && e[i + 1] < angle);
      if (!crossed)
        continue;
      double lo = i * 10.0, hi = lo + 10.0;
      while (hi - lo > 0.5) {
        const double mid = 0.5 * (lo + hi);
        if ((sky.at(mid) >= angle) == rising)
          hi = mid;
        else
          lo = mid;
      }
      int r = dyn_round(0.5 * (lo + hi));
      return r > 1439 ? 1439 : r;
    }
    return -1;
  };
  day.astro_dawn = event(-18.0, true);
  day.nautical_dawn = event(-12.0, true);
  day.civil_dawn = event(-6.0, true);
  day.sunrise = event(-0.833, true);
  day.golden_am = event(6.0, true);
  day.noon = dyn_round(solar_noon_minute(y, m, d, lat, lon, tz_min));
  day.golden_pm = event(6.0, false);
  day.sunset = event(-0.833, false);
  day.civil_dusk = event(-6.0, false);
  day.nautical_dusk = event(-12.0, false);
  day.astro_dusk = event(-18.0, false);
  if (day.sunrise < 0 && day.sunset < 0)
    day.always_up = sky.at(day.noon) > -0.833;
  return day;
}

// The dark part of the night that starts on local day d: the window from local noon of d to local
// noon of d + 1, clipped to civil darkness. The moon interval runs from its rise (or the start of
// the window when it is already up) to its next set (or the window end), clipped to dusk..dawn; the
// first moon-up interval that survives the clipping wins. Rise and set wrap at midnight, so a set after
// midnight comes out smaller than the rise, which is what moon_points expects. No such interval: -1.
inline MoonNight moon_night(int y, int m, int d, double lat, double lon, int tz_min) {
  MoonNight n;
  const double jd0 = julian_day(y, m, d, 0.0) - tz_min / 1440.0;
  const double win_start = 720.0;   // local noon of day d
  const double win_end = 2160.0;    // local noon of day d + 1
  const double mid = 1440.0;        // window middle, for the illumination of an empty night

  // Civil darkness within the window. A sun that never reaches -6 leaves the whole window dark.
  double civil_dusk = win_start, civil_dawn = win_end;
  double dusk = 0.0, dawn = 0.0;
  if (sky_window_crossing(&sun_elevation, jd0, win_start, win_end, lat, lon, -6.0, false, 30.0, 1.0,
                          &dusk) &&
      sky_window_crossing(&sun_elevation, jd0, dusk, win_end, lat, lon, -6.0, true, 30.0, 1.0,
                          &dawn)) {
    civil_dusk = dusk;
    civil_dawn = dawn;
  }

  // Walk the moon's up-intervals through the window (it may already be up at the start, or set and rise
  // again) and take the first one that overlaps civil darkness. The moon up at noon but down by dusk
  // is skipped; one that is still up at dusk starts at dusk.
  double rise = 0.0, set = 0.0;
  bool found = false;
  bool up = moon_elevation(jd0 + win_start / 1440.0, lat, lon) >= K_MOON_HORIZON;
  double cursor = win_start, iv_start = win_start;
  while (!found && cursor < win_end) {
    double edge = 0.0;
    if (up) {
      bool falls = sky_window_crossing(&moon_elevation, jd0, cursor, win_end, lat, lon, K_MOON_HORIZON,
                                       false, 30.0, 1.0, &edge);
      double iv_end = falls ? edge : win_end;
      double lo = iv_start > civil_dusk ? iv_start : civil_dusk;
      double hi = iv_end < civil_dawn ? iv_end : civil_dawn;
      if (hi > lo) {
        rise = lo;
        set = hi;
        found = true;
      }
      if (!falls)
        break;
      cursor = edge + 0.5;
      up = false;
    } else {
      if (!sky_window_crossing(&moon_elevation, jd0, cursor, win_end, lat, lon, K_MOON_HORIZON, true,
                               30.0, 1.0, &edge))
        break;
      iv_start = edge;
      cursor = edge + 0.5;
      up = true;
    }
  }
  if (!found) {
    n.rise = -1;
    n.set = -1;
    n.illumination = moon_illumination(jd0 + mid / 1440.0);
    return n;
  }
  n.rise = static_cast<int>(dyn_wrap_minute(dyn_round(rise)));
  n.set = static_cast<int>(dyn_wrap_minute(dyn_round(set)));
  n.illumination = moon_illumination(jd0 + 0.5 * (rise + set) / 1440.0);
  return n;
}

// Change to the daily curve when points[i] is removed: sample every 5 minutes between the two
// neighbors (the points form a circle, so the first point's neighbors are the last and the second),
// sum the absolute per-channel differences of schedule_compute at minute*60, and total over the
// samples. The integrated error is what "changes the curve least" means here; the largest single
// sample would drop the noon peak before its much wider, lower shoulders.
inline long long sky_removal_cost(const std::vector<SchedulePoint> &points, size_t i) {
  const size_t count = points.size();
  std::vector<SchedulePoint> reduced;
  reduced.reserve(count - 1);
  for (size_t k = 0; k < count; k++)
    if (k != i)
      reduced.push_back(points[k]);

  const size_t prev = (i + count - 1) % count;
  const size_t next = (i + 1) % count;
  const int prev_minute = points[prev].minute;
  int span = (static_cast<int>(points[next].minute) - prev_minute + 1440) % 1440;
  if (span == 0)
    span = 1440;  // two points: the removal turns the whole circle into one held value

  long long total = 0;
  for (int off = 0; off <= span; off += 5) {
    uint32_t sec = static_cast<uint32_t>((prev_minute + off) % 1440) * 60u;
    uint8_t full[NUM_CHANNELS] = {0, 0, 0, 0, 0};
    uint8_t less[NUM_CHANNELS] = {0, 0, 0, 0, 0};
    schedule_compute(points, sec, full);
    schedule_compute(reduced, sec, less);
    for (size_t c = 0; c < NUM_CHANNELS; c++) {
      int diff = static_cast<int>(full[c]) - static_cast<int>(less[c]);
      total += diff < 0 ? -diff : diff;
    }
  }
  return total;
}

// Drop points until at most max_points remain, always the one whose removal changes the daily curve
// least (ties: the earliest). Never fewer than one point. Returns the sorted result.
inline std::vector<SchedulePoint> simplify_points(std::vector<SchedulePoint> points,
                                                  size_t max_points) {
  if (max_points < 1)
    max_points = 1;
  std::stable_sort(points.begin(), points.end(),
                   [](const SchedulePoint &a, const SchedulePoint &b) { return a.minute < b.minute; });
  while (points.size() > max_points && points.size() > 1) {
    size_t best = 0;
    long long best_cost = sky_removal_cost(points, 0);
    for (size_t i = 1; i < points.size(); i++) {
      long long cost = sky_removal_cost(points, i);
      if (cost < best_cost) {
        best_cost = cost;
        best = i;
      }
    }
    points.erase(points.begin() + static_cast<std::ptrdiff_t>(best));
  }
  return points;
}

// The same day with a sunrise-to-sunset span of exactly `hours` (clamped to 1..23), centered on
// noon. A normal day scales every event's offset from noon to the new half span; a polar day or
// night gets a synthetic twilight ladder around the new sunrise/sunset. Events pushed outside the
// day become -1 rather than wrapping.
inline SunDay fixed_photoperiod(const SunDay &day, int hours) {
  if (hours < 1)
    hours = 1;
  if (hours > 23)
    hours = 23;
  const int half = hours * 30;

  SunDay out = day;
  out.sunrise = day.noon - half;
  out.sunset = day.noon + half;
  if (day.sunrise >= 0 && day.sunset >= 0) {
    double orig_half = (day.sunset - day.sunrise) / 2.0;
    double scale = orig_half != 0.0 ? half / orig_half : 1.0;
    int *scaled[] = {&out.astro_dawn, &out.nautical_dawn, &out.civil_dawn, &out.golden_am,
                     &out.golden_pm, &out.civil_dusk, &out.nautical_dusk, &out.astro_dusk};
    for (size_t i = 0; i < sizeof(scaled) / sizeof(scaled[0]); i++)
      if (*scaled[i] >= 0)
        *scaled[i] = dyn_round(day.noon + (*scaled[i] - day.noon) * scale);
  } else {
    out.astro_dawn = out.sunrise - 135;
    out.nautical_dawn = out.sunrise - 90;
    out.civil_dawn = out.sunrise - 45;
    out.golden_am = out.sunrise + 90;
    out.golden_pm = out.sunset - 90;
    out.civil_dusk = out.sunset + 45;
    out.nautical_dusk = out.sunset + 90;
    out.astro_dusk = out.sunset + 135;
    out.always_up = false;
  }
  out.noon = day.noon;

  int *all[] = {&out.astro_dawn, &out.nautical_dawn, &out.civil_dawn, &out.sunrise, &out.golden_am,
                &out.golden_pm, &out.sunset, &out.civil_dusk, &out.nautical_dusk, &out.astro_dusk};
  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
    if (*all[i] < 0 || *all[i] > 1439)
      *all[i] = -1;
  return out;
}

}  // namespace klink
}  // namespace kessil
