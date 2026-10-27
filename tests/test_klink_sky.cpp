// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_sky.cpp -o /tmp/tsky && /tmp/tsky
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "klink_dynamic.h"
#include "klink_schedule.h"
#include "klink_sky.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// The sun reference rows from test_klink_astro.cpp.
struct SunRef {
  const char *site; int y, m, d, tz; double lat, lon;
  double rise, set;
  double c6r, c6s, c12r, c12s, c18r, c18s;
  double noon;
};

static const SunRef SUN[] = {
    {"Chicago", 2026, 3, 20, -300, 41.8781, -87.6298, 414, 1143, 386, 1171, 353, 1203, 320, 1237, 778},
    {"Chicago", 2026, 6, 21, -300, 41.8781, -87.6298, 316, 1229, 281, 1264, 237, 1308, 183, 1361, 772},
    {"Chicago", 2026, 12, 21, -360, 41.8781, -87.6298, 435, 983, 403, 1014, 368, 1049, 334, 1083, 709},
    {"London", 2026, 3, 20, 0, 51.5074, -0.1278, 363, 1093, 330, 1127, 291, 1166, 250, 1207, 728},
    {"London", 2026, 6, 21, 60, 51.5074, -0.1278, 283, 1282, 235, 1329, 161, 1404, -1, -1, 782},
    {"London", 2026, 12, 21, 0, 51.5074, -0.1278, 484, 953, 443, 994, 400, 1037, 359, 1078, 719},
    {"Sydney", 2026, 3, 20, 660, -33.8688, 151.2093, 418, 1147, 393, 1172, 364, 1201, 334, 1230, 783},
    {"Sydney", 2026, 6, 21, 600, -33.8688, 151.2093, 420, 1014, 392, 1042, 361, 1073, 331, 1103, 717},
    {"Sydney", 2026, 12, 21, 660, -33.8688, 151.2093, 341, 1205, 311, 1235, 276, 1270, 236, 1310, 773},
    {"Tromso", 2026, 6, 21, 120, 69.6492, 18.9553, -1, -1, -1, -1, -1, -1, -1, -1, 766},
    {"Tromso", 2026, 12, 21, 60, 69.6492, 18.9553, -1, -1, 571, 833, 467, 938, 388, 1016, 702},
    {"Tromso", 2026, 3, 20, 60, 69.6492, 18.9553, 344, 1082, 284, 1142, 208, 1219, 106, 1325, 712},
};

static void check_event(const char *label, int got, double ref) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "sun_day %s", label);
  if (ref < 0.0)
    check(buf, got == -1);
  else
    check(buf, got >= 0 && near(got, ref, 3.0));
  if (ref >= 0.0 && got >= 0 && !near(got, ref, 3.0))
    std::printf("  %s: got %d want %.0f (diff %+d)\n", buf, got, ref, got - static_cast<int>(ref));
}

// The PyEphem moon table: clipped rise/set in local-day minutes, illumination at the middle of the
// clipped window, and the raw window crossings for reference (minutes after midnight of day d).
struct MoonRow {
  const char *site; int y, m, d, tz; double lat, lon;
  int clipped_rise, clipped_set; double illum;
  int raw_rise, raw_set, civil_dusk, civil_dawn;
};

static const MoonRow MOON[] = {
    {"Chicago", 2026, 11, 5, -360, 41.8781, -87.6298, 228, 360, 0.082, 1668, 2344, 1029, 1800},
    {"Chicago", 2026, 11, 12, -360, 41.8781, -87.6298, 1022, 1120, 0.110, 606, 1120, 1022, 1808},  // moon up at noon and at dusk, sets 18:40
    {"Chicago", 2026, 11, 19, -360, 41.8781, -87.6298, 1017, 124, 0.757, 811, 1564, 1017, 1816},
    {"Chicago", 2026, 11, 26, -360, 41.8781, -87.6298, 1098, 384, 0.898, 1098, 2064, 1013, 1824},
    {"London", 2026, 11, 5, 0, 51.5074, -0.1278, 230, 387, 0.096, 1670, 2335, 1022, 1827},
    {"London", 2026, 11, 8, 0, 51.5074, -0.1278, -1, -1, 0.020, 0, 0, 0, 0},  // new moon: down for the whole dark period
    {"London", 2026, 11, 12, 0, 51.5074, -0.1278, 1012, 1061, 0.096, 659, 1061, 1012, 1838},  // same: up at noon, sets 17:41
    {"London", 2026, 11, 19, 0, 51.5074, -0.1278, 1004, 119, 0.734, 818, 1559, 1004, 1849},
    {"London", 2026, 11, 26, 0, 51.5074, -0.1278, 1036, 419, 0.916, 1036, 2110, 997, 1859},
    {"Sydney", 2026, 11, 5, 660, -33.8688, 151.2093, 228, 324, 0.128, 1668, 2434, 1193, 1764},
    {"Sydney", 2026, 11, 19, 660, -33.8688, 151.2093, 1208, 150, 0.698, 836, 1590, 1208, 1753},
};

static int circular_distance(int a, int b) {
  int d = std::abs(a - b) % 1440;
  return d > 720 ? 1440 - d : d;
}

static bool has_minute(const std::vector<SchedulePoint> &pts, int minute) {
  for (size_t i = 0; i < pts.size(); i++)
    if (pts[i].minute == minute)
      return true;
  return false;
}

static bool sorted_strict(const std::vector<SchedulePoint> &pts) {
  for (size_t i = 1; i < pts.size(); i++)
    if (pts[i].minute <= pts[i - 1].minute)
      return false;
  return true;
}

static int max_deviation(const std::vector<SchedulePoint> &full,
                         const std::vector<SchedulePoint> &less) {
  int worst = 0;
  for (uint32_t sec = 0; sec < SECONDS_PER_DAY; sec += 300) {
    uint8_t a[NUM_CHANNELS], b[NUM_CHANNELS];
    schedule_compute(full, sec, a);
    schedule_compute(less, sec, b);
    for (size_t c = 0; c < NUM_CHANNELS; c++) {
      int d = static_cast<int>(a[c]) - static_cast<int>(b[c]);
      if (d < 0)
        d = -d;
      if (d > worst)
        worst = d;
    }
  }
  return worst;
}

static void check_shapes(const std::vector<SchedulePoint> &pts, const char *label) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s sorted strictly ascending", label);
  check(buf, sorted_strict(pts));
  std::snprintf(buf, sizeof(buf), "%s 1..16 points", label);
  check(buf, !pts.empty() && pts.size() <= 16);
  bool in_range = true, enabled = true;
  uint8_t out[NUM_CHANNELS];
  bool computed = true;
  for (size_t i = 0; i < pts.size(); i++) {
    if (pts[i].minute > 1439)
      in_range = false;
    if (!pts[i].enabled)
      enabled = false;
    for (size_t c = 0; c < NUM_CHANNELS; c++)
      if (pts[i].channels[c] > CHANNEL_MAX[c])
        in_range = false;
  }
  for (uint32_t sec = 0; sec < SECONDS_PER_DAY; sec += 420)
    if (!schedule_compute(pts, sec, out))
      computed = false;
  std::snprintf(buf, sizeof(buf), "%s minutes and channels in range", label);
  check(buf, in_range);
  std::snprintf(buf, sizeof(buf), "%s all points enabled", label);
  check(buf, enabled);
  std::snprintf(buf, sizeof(buf), "%s schedule_compute over 24h", label);
  check(buf, computed);
}

static const int MONTH_DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static void next_day(int &y, int &m, int &d) {
  int days = MONTH_DAYS[m - 1] + (m == 2 && is_leap(y) ? 1 : 0);
  if (++d > days) {
    d = 1;
    if (++m > 12) {
      m = 1;
      y++;
    }
  }
}

static bool same_sun_day(const SunDay &a, const SunDay &b) {
  return a.astro_dawn == b.astro_dawn && a.nautical_dawn == b.nautical_dawn &&
         a.civil_dawn == b.civil_dawn && a.sunrise == b.sunrise && a.golden_am == b.golden_am &&
         a.noon == b.noon && a.golden_pm == b.golden_pm && a.sunset == b.sunset &&
         a.civil_dusk == b.civil_dusk && a.nautical_dusk == b.nautical_dusk &&
         a.astro_dusk == b.astro_dusk && a.always_up == b.always_up;
}

static bool same_moon_night(const MoonNight &a, const MoonNight &b) {
  return a.rise == b.rise && a.set == b.set && a.illumination == b.illumination;
}

int main() {
  // sun_day against the klink_astro reference rows.
  for (size_t i = 0; i < sizeof(SUN) / sizeof(SUN[0]); i++) {
    const SunRef &r = SUN[i];
    SunDay day = sun_day(r.y, r.m, r.d, r.lat, r.lon, r.tz);
    char label[160];
    std::snprintf(label, sizeof(label), "%s %04d-%02d-%02d", r.site, r.y, r.m, r.d);
    char name[192];

    std::snprintf(name, sizeof(name), "%s astro dawn", label);
    check_event(name, day.astro_dawn, r.c18r);
    std::snprintf(name, sizeof(name), "%s nautical dawn", label);
    check_event(name, day.nautical_dawn, r.c12r);
    std::snprintf(name, sizeof(name), "%s civil dawn", label);
    check_event(name, day.civil_dawn, r.c6r);
    std::snprintf(name, sizeof(name), "%s sunrise", label);
    check_event(name, day.sunrise, r.rise);
    std::snprintf(name, sizeof(name), "%s sunset", label);
    check_event(name, day.sunset, r.set);
    std::snprintf(name, sizeof(name), "%s civil dusk", label);
    check_event(name, day.civil_dusk, r.c6s);
    std::snprintf(name, sizeof(name), "%s nautical dusk", label);
    check_event(name, day.nautical_dusk, r.c12s);
    std::snprintf(name, sizeof(name), "%s astro dusk", label);
    check_event(name, day.astro_dusk, r.c18s);
    std::snprintf(name, sizeof(name), "%s noon", label);
    check_event(name, day.noon, r.noon);

    // The golden events are not in the table; compare them with a direct crossing at +6.
    double m6 = 0.0;
    if (sun_crossing(r.y, r.m, r.d, r.lat, r.lon, r.tz, 6.0, true, &m6))
      check_event("golden am", day.golden_am, std::floor(m6 + 0.5));
    else
      check("golden am absent", day.golden_am == -1);
    if (sun_crossing(r.y, r.m, r.d, r.lat, r.lon, r.tz, 6.0, false, &m6))
      check_event("golden pm", day.golden_pm, std::floor(m6 + 0.5));
    else
      check("golden pm absent", day.golden_pm == -1);

    bool no_horizon = day.sunrise == -1 && day.sunset == -1;
    std::snprintf(name, sizeof(name), "%s always_up only without a horizon event", label);
    check(name, day.always_up == (no_horizon && day.always_up));
  }

  // Polar day and polar night flags from the table.
  {
    SunDay up = sun_day(2026, 6, 21, 69.6492, 18.9553, 120);
    check("Tromso 06-21 always_up", up.always_up);
    SunDay down = sun_day(2026, 12, 21, 69.6492, 18.9553, 60);
    check("Tromso 12-21 no sunrise", down.sunrise == -1);
    check("Tromso 12-21 not always_up", !down.always_up);
    check("Tromso 12-21 noon filled", down.noon >= 0);
  }

  // moon_night against the PyEphem table.
  for (size_t i = 0; i < sizeof(MOON) / sizeof(MOON[0]); i++) {
    const MoonRow &r = MOON[i];
    MoonNight n = moon_night(r.y, r.m, r.d, r.lat, r.lon, r.tz);
    char label[160], name[192];
    std::snprintf(label, sizeof(label), "moon_night %s %04d-%02d-%02d", r.site, r.y, r.m, r.d);

    if (r.clipped_rise < 0) {
      std::snprintf(name, sizeof(name), "%s rise == -1", label);
      check(name, n.rise == -1);
    } else {
      std::snprintf(name, sizeof(name), "%s rise", label);
      check(name, n.rise >= 0 && circular_distance(n.rise, r.clipped_rise) <= 20);
      std::snprintf(name, sizeof(name), "%s set", label);
      check(name, n.set >= 0 && circular_distance(n.set, r.clipped_set) <= 20);
    }
    std::snprintf(name, sizeof(name), "%s illumination", label);
    check(name, near(n.illumination, r.illum, 0.05));
    if (!near(n.illumination, r.illum, 0.05))
      std::printf("  %s: got %.3f want %.3f\n", name, n.illumination, r.illum);
    if (n.rise >= 0 && circular_distance(n.rise, r.clipped_rise) > 20)
      std::printf("  %s rise: got %d want %d\n", label, n.rise, r.clipped_rise);
    if (n.set >= 0 && circular_distance(n.set, r.clipped_set) > 20)
      std::printf("  %s set: got %d want %d\n", label, n.set, r.clipped_set);
  }

  // simplify_points on a full Chicago midsummer day.
  SunDay chi = sun_day(2026, 6, 21, 41.8781, -87.6298, -300);
  SunParams def;
  std::vector<SchedulePoint> full = sun_points(chi, def, DYN_SUN);
  {
    std::printf("  full Chicago 06-21: %u points\n", static_cast<unsigned>(full.size()));
    check("simplify source has 11 points", full.size() == 11);
    int peak_minute = -1, peak_int = -1;
    for (size_t i = 0; i < full.size(); i++)
      if (full[i].channels[0] > peak_int) {
        peak_int = full[i].channels[0];
        peak_minute = full[i].minute;
      }

    std::vector<SchedulePoint> s8 = simplify_points(full, 8);
    check("simplify to 8 has 8 points", s8.size() == 8);
    check("simplify to 8 sorted", sorted_strict(s8));
    check("simplify to 8 keeps the noon peak", has_minute(s8, peak_minute));
    int dev8 = max_deviation(full, s8);
    std::printf("  simplify 11 -> 8: %u points, max deviation %d raw units\n",
                static_cast<unsigned>(s8.size()), dev8);
    check("simplify to 8 deviates <= 25", dev8 <= 25);

    std::vector<SchedulePoint> s6 = simplify_points(full, 6);
    check("simplify to 6 has 6 points", s6.size() == 6);
    check("simplify to 6 sorted", sorted_strict(s6));
    check("simplify to 6 keeps the noon peak", has_minute(s6, peak_minute));
    int dev6 = max_deviation(full, s6);
    std::printf("  simplify 11 -> 6: %u points, max deviation %d raw units\n",
                static_cast<unsigned>(s6.size()), dev6);

    std::vector<SchedulePoint> noop = simplify_points(full, full.size());
    check("simplify at size is a no-op", noop.size() == full.size() && sorted_strict(noop));

    std::vector<SchedulePoint> one(1);
    one[0] = full[full.size() / 2];
    std::vector<SchedulePoint> one_kept = simplify_points(one, 0);
    check("simplify keeps one point", one_kept.size() == 1 && one_kept[0].minute == one[0].minute);
  }

  // fixed_photoperiod: a normal day scales to exactly ten hours around noon.
  {
    SunDay fixed = fixed_photoperiod(chi, 10);
    check("fixed 10h sunrise == noon - 300", fixed.sunrise == chi.noon - 300);
    check("fixed 10h sunset == noon + 300", fixed.sunset == chi.noon + 300);
    check("fixed 10h noon unchanged", fixed.noon == chi.noon);
    double scale = 300.0 / ((chi.sunset - chi.sunrise) / 2.0);
    int want_astro = dyn_round(chi.noon + (chi.astro_dawn - chi.noon) * scale);
    check("fixed 10h astro dawn scaled", fixed.astro_dawn == want_astro);
    int want_dusk = dyn_round(chi.noon + (chi.astro_dusk - chi.noon) * scale);
    check("fixed 10h astro dusk scaled", fixed.astro_dusk == want_dusk);
    check("fixed 10h keeps all events", fixed.astro_dawn >= 0 && fixed.nautical_dawn >= 0 &&
                                            fixed.civil_dawn >= 0 && fixed.golden_am >= 0 &&
                                            fixed.golden_pm >= 0 && fixed.civil_dusk >= 0 &&
                                            fixed.nautical_dusk >= 0 && fixed.astro_dusk >= 0);
  }

  // fixed_photoperiod on a polar day: a synthetic twilight ladder, still eleven sun points.
  {
    SunDay tromso = sun_day(2026, 6, 21, 69.6492, 18.9553, 120);
    check("Tromso source always_up", tromso.always_up);
    SunDay fixed = fixed_photoperiod(tromso, 12);
    check("fixed polar 12h sunrise == noon - 360", fixed.sunrise == tromso.noon - 360);
    check("fixed polar 12h sunset == noon + 360", fixed.sunset == tromso.noon + 360);
    check("fixed polar 12h astro dawn", fixed.astro_dawn == fixed.sunrise - 135);
    check("fixed polar 12h nautical dawn", fixed.nautical_dawn == fixed.sunrise - 90);
    check("fixed polar 12h civil dawn", fixed.civil_dawn == fixed.sunrise - 45);
    check("fixed polar 12h golden am", fixed.golden_am == fixed.sunrise + 90);
    check("fixed polar 12h golden pm", fixed.golden_pm == fixed.sunset - 90);
    check("fixed polar 12h civil dusk", fixed.civil_dusk == fixed.sunset + 45);
    check("fixed polar 12h nautical dusk", fixed.nautical_dusk == fixed.sunset + 90);
    check("fixed polar 12h astro dusk", fixed.astro_dusk == fixed.sunset + 135);
    check("fixed polar 12h not always_up", !fixed.always_up);
    check("fixed polar 12h noon unchanged", fixed.noon == tromso.noon);
    std::vector<SchedulePoint> pts = sun_points(fixed, def, DYN_SUN);
    check("fixed polar 12h sun_points has 11 points", pts.size() == 11);
    check_shapes(pts, "fixed polar 12h");
  }

  // Determinism.
  {
    SunDay a = sun_day(2026, 11, 19, 41.8781, -87.6298, -360);
    SunDay b = sun_day(2026, 11, 19, 41.8781, -87.6298, -360);
    check("sun_day deterministic", same_sun_day(a, b));
    MoonNight c = moon_night(2026, 11, 19, 41.8781, -87.6298, -360);
    MoonNight d = moon_night(2026, 11, 19, 41.8781, -87.6298, -360);
    check("moon_night deterministic", same_moon_night(c, d));
  }

  // Never throws (never crashes or produces impossible shapes) at an extreme latitude.
  {
    int y = 2026, m = 1, d = 1;
    MoonParams mp;
    for (int i = 0; i < 89; i++) {
      SunDay day = sun_day(y, m, d, 89.0, 0.0, 0);
      check("lat 89 noon filled", day.noon >= 0);
      if (day.sunrise < 0 || day.sunset < 0)
        check("lat 89 polar day only without horizon events",
              !day.always_up || (day.sunrise == -1 && day.sunset == -1));
      std::vector<SchedulePoint> pts = sun_points(day, def, DYN_SUN);
      char label[96];
      std::snprintf(label, sizeof(label), "lat 89 day %d sun", i);
      check_shapes(pts, label);
      std::vector<SchedulePoint> mpts = moon_points(moon_night(y, m, d, 89.0, 0.0, 0), mp, DYN_BLUE);
      std::snprintf(label, sizeof(label), "lat 89 day %d moon", i);
      check_shapes(mpts, label);

      MoonNight n = moon_night(y, m, d, 89.0, 0.0, 0);
      bool interval_ok = (n.rise == -1 && n.set == -1) ||
                         (n.rise >= 0 && n.rise <= 1439 && n.set >= 0 && n.set <= 1439);
      std::snprintf(label, sizeof(label), "lat 89 day %d moon range", i);
      check(label, interval_ok);
      std::snprintf(label, sizeof(label), "lat 89 day %d moon illumination", i);
      check(label, n.illumination >= 0.0 && n.illumination <= 1.0);
      next_day(y, m, d);
    }
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
