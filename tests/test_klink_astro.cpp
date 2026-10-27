// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_astro.cpp -o /tmp/ta && /tmp/ta
#include <cmath>
#include <cstdio>
#include "klink_astro.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

struct SunRef {
  const char *site; int y, m, d, tz; double lat, lon;
  double rise, set;          // -0.833 deg, refracted horizon
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

// Check one sun event: ref < 0 means "must not happen"; otherwise within 3 min.
static void check_sun_event(const char *name, const SunRef &r, double angle, bool rising, double ref) {
  double minute = -999.0;
  bool got = sun_crossing(r.y, r.m, r.d, r.lat, r.lon, r.tz, angle, rising, &minute);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d %+g %s", name, r.y, r.m, r.d, angle,
                rising ? "rise" : "set");
  if (ref < 0.0)
    check(buf, !got);
  else
    check(buf, got && near(minute, ref, 3.0));
  if (got && ref >= 0.0 && !near(minute, ref, 3.0))
    std::printf("  %s: got %.1f want %.0f (diff %+.1f)\n", buf, minute, ref, minute - ref);
}

struct MoonRef {
  const char *site; int y, m, d, tz; double lat, lon;
  double rise, set, illum;
};

static const MoonRef MOON[] = {
    {"Chicago", 2026, 10, 26, -300, 41.8781, -87.6298, 1071, 477, 0.994},
    {"Chicago", 2026, 11, 5, -300, 41.8781, -87.6298, 220, 942, 0.129},
    {"Chicago", 2026, 11, 12, -300, 41.8781, -87.6298, 666, 1180, 0.107},
    {"London", 2026, 10, 26, 0, 51.5074, -0.1278, 981, 436, 0.997},
    {"London", 2026, 11, 5, 0, 51.5074, -0.1278, 153, 883, 0.144},
    {"London", 2026, 11, 12, 0, 51.5074, -0.1278, 659, 1061, 0.096},
    {"Sydney", 2026, 10, 26, 660, -33.8688, 151.2093, 1172, 332, 0.998},
    {"Sydney", 2026, 11, 5, 660, -33.8688, 151.2093, 201, 933, 0.180},
    {"Sydney", 2026, 11, 12, 660, -33.8688, 151.2093, 436, 1351, 0.072},
};

int main() {
  check("julian_day J2000", julian_day(2000, 1, 1, 12.0) == 2451545.0);

  // Solar noon on the equator at the March equinox: elevation ~ 90 - |declination|.
  {
    double jd = julian_day(2026, 3, 20, 0.0);
    double decl, eq;
    sun_decl_eqtime(jd + 0.5, &decl, &eq);
    double noon = solar_noon_minute(2026, 3, 20, 0.0, 0.0, 0);
    double elev = sun_elevation(jd + noon / 1440.0, 0.0, 0.0);
    check("equinox noon elevation", near(elev, 90.0 - std::fabs(decl), 2.0));
    std::printf("  equinox noon: elev %.3f decl %.3f noon %.2f\n", elev, decl, noon);
  }

  for (size_t i = 0; i < sizeof(SUN) / sizeof(SUN[0]); i++) {
    const SunRef &r = SUN[i];
    check_sun_event(r.site, r, -0.833, true, r.rise);
    check_sun_event(r.site, r, -0.833, false, r.set);
    check_sun_event(r.site, r, -6.0, true, r.c6r);
    check_sun_event(r.site, r, -6.0, false, r.c6s);
    check_sun_event(r.site, r, -12.0, true, r.c12r);
    check_sun_event(r.site, r, -12.0, false, r.c12s);
    check_sun_event(r.site, r, -18.0, true, r.c18r);
    check_sun_event(r.site, r, -18.0, false, r.c18s);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d solar noon", r.site, r.y, r.m, r.d);
    double noon = solar_noon_minute(r.y, r.m, r.d, r.lat, r.lon, r.tz);
    check(buf, near(noon, r.noon, 2.0));
    if (!near(noon, r.noon, 2.0))
      std::printf("  %s: got %.2f want %.0f (diff %+.1f)\n", buf, noon, r.noon, noon - r.noon);
  }

  // Chicago midsummer: the elevation at the reported sunrise really is the -0.833 crossing.
  {
    double minute = 0.0;
    bool got = sun_crossing(2026, 6, 21, 41.8781, -87.6298, -300, -0.833, true, &minute);
    double jd = julian_day(2026, 6, 21, 0.0) + 300.0 / 1440.0 + minute / 1440.0;
    check("crossing elevation matches angle", got && near(sun_elevation(jd, 41.8781, -87.6298), -0.833, 0.02));
  }

  for (size_t i = 0; i < sizeof(MOON) / sizeof(MOON[0]); i++) {
    const MoonRef &r = MOON[i];
    double rise = -999.0, set = -999.0;
    bool gr = moon_crossing(r.y, r.m, r.d, r.lat, r.lon, r.tz, true, &rise);
    bool gs = moon_crossing(r.y, r.m, r.d, r.lat, r.lon, r.tz, false, &set);
    double noon_jd = julian_day(r.y, r.m, r.d, 0.0) + (720.0 - r.tz) / 1440.0;
    double illum = moon_illumination(noon_jd);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d moonrise", r.site, r.y, r.m, r.d);
    check(buf, gr && near(rise, r.rise, 20.0));
    if (gr && !near(rise, r.rise, 20.0))
      std::printf("  %s: got %.1f want %.0f (diff %+.1f)\n", buf, rise, r.rise, rise - r.rise);

    std::snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d moonset", r.site, r.y, r.m, r.d);
    check(buf, gs && near(set, r.set, 20.0));
    if (gs && !near(set, r.set, 20.0))
      std::printf("  %s: got %.1f want %.0f (diff %+.1f)\n", buf, set, r.set, set - r.set);

    std::snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d illumination", r.site, r.y, r.m, r.d);
    check(buf, near(illum, r.illum, 0.04));
    if (!near(illum, r.illum, 0.04))
      std::printf("  %s: got %.3f want %.3f (diff %+.3f)\n", buf, illum, r.illum, illum - r.illum);
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
