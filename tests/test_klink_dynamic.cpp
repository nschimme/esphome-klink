// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_dynamic.cpp -o /tmp/tdyn && /tmp/tdyn
#include <cmath>
#include <cstdio>
#include <vector>
#include "klink_astro.h"
#include "klink_dynamic.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static const SchedulePoint *find_point(const std::vector<SchedulePoint> &pts, int minute) {
  for (size_t i = 0; i < pts.size(); i++)
    if (pts[i].minute == minute)
      return &pts[i];
  return nullptr;
}

static bool points_equal(const std::vector<SchedulePoint> &a, const std::vector<SchedulePoint> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].minute != b[i].minute || a[i].enabled != b[i].enabled)
      return false;
    for (size_t c = 0; c < NUM_CHANNELS; c++)
      if (a[i].channels[c] != b[i].channels[c])
        return false;
  }
  return true;
}

static int event_minute(int y, int m, int d, double lat, double lon, int tz, double angle,
                        bool rising) {
  double minute = 0.0;
  if (!sun_crossing(y, m, d, lat, lon, tz, angle, rising, &minute))
    return -1;
  return static_cast<int>(std::floor(minute + 0.5));
}

static int round_minute(double v) { return static_cast<int>(std::floor(v + 0.5)); }

struct Site {
  const char *name;
  double lat, lon;
};
static const Site SITES[] = {
    {"Chicago", 41.8781, -87.6298},
    {"London", 51.5074, -0.1278},
    {"Sydney", -33.8688, 151.2093},
    {"Tromso", 69.6492, 18.9553},
};

struct DateRef {
  int y, m, d;
  const char *label;
};
static const DateRef DATES[] = {
    {2026, 3, 20, "03-20"},
    {2026, 6, 21, "06-21"},
    {2026, 9, 23, "09-23"},
    {2026, 12, 21, "12-21"},
};

// tz offsets per site (row) and date (column), as in test_klink_astro.cpp; 09-23 reuses the 03-20
// offsets of the same site.
static const int TZ[4][4] = {
    {-300, -300, -300, -360},  // Chicago
    {0, 60, 60, 0},            // London
    {660, 600, 600, 660},      // Sydney
    {60, 120, 120, 60},        // Tromso
};

static SunDay make_sun_day(const Site &s, const DateRef &d, int tz) {
  SunDay day;
  day.astro_dawn = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, -18.0, true);
  day.nautical_dawn = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, -12.0, true);
  day.civil_dawn = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, -6.0, true);
  day.sunrise = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, -0.833, true);
  day.golden_am = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, 6.0, true);
  day.noon = round_minute(solar_noon_minute(d.y, d.m, d.d, s.lat, s.lon, tz));
  day.golden_pm = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, 6.0, false);
  day.sunset = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, -0.833, false);
  day.civil_dusk = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, -6.0, false);
  day.nautical_dusk = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, -12.0, false);
  day.astro_dusk = event_minute(d.y, d.m, d.d, s.lat, s.lon, tz, -18.0, false);
  // Polar day: no horizon crossing and the sun is still above it at local noon.
  if (day.sunrise < 0 && day.sunset < 0 && day.noon >= 0) {
    double jd = julian_day(d.y, d.m, d.d, 0.0) + (day.noon - tz) / 1440.0;
    day.always_up = sun_elevation(jd, s.lat, s.lon) > -0.833;
  }
  return day;
}

// Everything the property test asks of any generated point list.
static void check_shape(const std::vector<SchedulePoint> &pts, const SunParams &p, const char *name) {
  char buf[192];
  bool sorted = true, minutes = true, chans = true, enabled = true;
  for (size_t i = 0; i < pts.size(); i++) {
    if (i > 0 && pts[i].minute <= pts[i - 1].minute)
      sorted = false;
    if (pts[i].minute > 1439)
      minutes = false;
    if (!pts[i].enabled)
      enabled = false;
    for (size_t c = 0; c < NUM_CHANNELS; c++)
      if (pts[i].channels[c] > CHANNEL_MAX[c])
        chans = false;
    if (static_cast<int>(pts[i].channels[0]) > p.peak_int ||
        static_cast<int>(pts[i].channels[0]) < p.floor_int)
      chans = false;
  }
  std::snprintf(buf, sizeof(buf), "%s sorted strictly ascending", name);
  check(buf, sorted);
  std::snprintf(buf, sizeof(buf), "%s 1..16 points", name);
  check(buf, !pts.empty() && pts.size() <= 16);
  std::snprintf(buf, sizeof(buf), "%s minutes in 0..1439", name);
  check(buf, minutes);
  std::snprintf(buf, sizeof(buf), "%s channels in range", name);
  check(buf, chans);
  std::snprintf(buf, sizeof(buf), "%s all points enabled", name);
  check(buf, enabled);
  std::snprintf(buf, sizeof(buf), "%s schedule_compute over 24h", name);
  bool computed = true;
  uint8_t out[NUM_CHANNELS];
  for (uint32_t sec = 0; sec < SECONDS_PER_DAY; sec += 420)
    if (!schedule_compute(pts, sec, out))
      computed = false;
  check(buf, computed);
}

int main() {
  const SunParams def;
  check("defaults", def.peak_int == 130 && def.floor_int == 0 && def.clr_warm == 0 &&
                        def.clr_cool == 120 && def.glow == 100 && def.dip == 0 &&
                        def.shift_min == 0 && def.night_amber == 0);
  check("moon defaults", MoonParams().amber == 0);

  // Property test over real astronomical data: every site, date and model.
  for (size_t si = 0; si < sizeof(SITES) / sizeof(SITES[0]); si++) {
    for (size_t di = 0; di < sizeof(DATES) / sizeof(DATES[0]); di++) {
      const Site &site = SITES[si];
      const DateRef &date = DATES[di];
      SunDay day = make_sun_day(site, date, TZ[si][di]);
      for (int model = DYN_GENERIC; model <= DYN_BLUE; model++) {
        std::vector<SchedulePoint> pts =
            sun_points(day, def, static_cast<DynModel>(model));
        char name[192];
        std::snprintf(name, sizeof(name), "sun %s %s model %d", site.name, date.label, model);
        check_shape(pts, def, name);
      }
    }
  }

  // Chicago midsummer: every event happens, so all eleven points are there.
  {
    SunDay day = make_sun_day(SITES[0], DATES[1], TZ[0][1]);
    std::vector<SchedulePoint> pts = sun_points(day, def, DYN_GENERIC);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Chicago 06-21 has 11 points (got %u)",
                  static_cast<unsigned>(pts.size()));
    check(buf, pts.size() == 11);
  }

  // London midsummer: the sun never reaches -18, so no astronomical events and nine points.
  {
    SunDay day = make_sun_day(SITES[1], DATES[1], TZ[1][1]);
    check("London 06-21 has no astro dawn", day.astro_dawn == -1);
    check("London 06-21 has no astro dusk", day.astro_dusk == -1);
    std::vector<SchedulePoint> pts = sun_points(day, def, DYN_GENERIC);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "London 06-21 has 9 points (got %u)",
                  static_cast<unsigned>(pts.size()));
    check(buf, pts.size() == 9);
  }

  // Tromso midsummer: polar day, one point holding the peak at noon.
  {
    SunDay day = make_sun_day(SITES[3], DATES[1], TZ[3][1]);
    check("Tromso 06-21 always_up", day.always_up);
    std::vector<SchedulePoint> pts = sun_points(day, def, DYN_SUN);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Tromso 06-21 one point (got %u)",
                  static_cast<unsigned>(pts.size()));
    check(buf, pts.size() == 1);
    check("Tromso 06-21 point at 12:00", !pts.empty() && pts[0].minute == 720);
    check("Tromso 06-21 INT is peak",
          !pts.empty() && pts[0].channels[0] == static_cast<uint8_t>(def.peak_int));
    check("Tromso 06-21 CLR is the Tuna Sun cool end", !pts.empty() && pts[0].channels[1] == 89);
  }

  // Tromso midwinter: twilight only, so the top of the day is the civil level.
  {
    SunDay day = make_sun_day(SITES[3], DATES[3], TZ[3][3]);
    check("Tromso 12-21 no sunrise", day.sunrise == -1);
    check("Tromso 12-21 no golden morning", day.golden_am == -1);
    check("Tromso 12-21 no golden evening", day.golden_pm == -1);
    check("Tromso 12-21 no sunset", day.sunset == -1);
    std::vector<SchedulePoint> pts = sun_points(day, def, DYN_SUN);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Tromso 12-21 has 6 twilight points (got %u)",
                  static_cast<unsigned>(pts.size()));
    check(buf, pts.size() == 6);
    int max_int = 0;
    for (size_t i = 0; i < pts.size(); i++)
      if (pts[i].channels[0] > max_int)
        max_int = pts[i].channels[0];
    std::snprintf(buf, sizeof(buf), "Tromso 12-21 INT <= civil level (got %d)", max_int);
    check(buf, near(max_int, 0.15 * def.peak_int, 1.0));
  }

  // Shape: Chicago equinox, the reference day for the curve's values.
  SunDay chi = make_sun_day(SITES[0], DATES[0], TZ[0][0]);
  std::vector<SchedulePoint> gen = sun_points(chi, def, DYN_GENERIC);
  {
    const SchedulePoint *noon = find_point(gen, chi.noon);
    const SchedulePoint *rise = find_point(gen, chi.sunrise);
    const SchedulePoint *astro = find_point(gen, chi.astro_dawn);
    check("shape noon point exists", noon != nullptr);
    check("shape noon INT == peak", noon != nullptr && noon->channels[0] == 130);
    check("shape noon CLR == cool", noon != nullptr && noon->channels[1] == def.clr_cool);
    check("shape sunrise point exists", rise != nullptr);
    check("shape sunrise INT == round(130*0.35) +-1",
          rise != nullptr && rise->channels[0] >= 45 && rise->channels[0] <= 47);
    check("shape astro dawn INT == floor",
          astro != nullptr && astro->channels[0] == static_cast<uint8_t>(def.floor_int));
    check("shape astro dawn CLR == warm", astro != nullptr && astro->channels[1] == def.clr_warm);

    std::vector<SchedulePoint> sun = sun_points(chi, def, DYN_SUN);
    const SchedulePoint *rise_s = find_point(sun, chi.sunrise);
    const SchedulePoint *noon_s = find_point(sun, chi.noon);
    check("shape sunrise UI5 == edge amber", rise_s != nullptr && rise_s->channels[4] == 60);
    check("shape sunrise UI4 == edge red", rise_s != nullptr && rise_s->channels[3] == 20);
    check("shape noon UI5 == midday amber", noon_s != nullptr && noon_s->channels[4] == 15);
    check("shape noon UI4 == midday red", noon_s != nullptr && noon_s->channels[3] == 8);
    check("shape sun noon CLR == 89", noon_s != nullptr && noon_s->channels[1] == 89);
    check("shape generic UI3-UI5 == 0",
          noon != nullptr && noon->channels[2] == 0 && noon->channels[3] == 0 &&
              noon->channels[4] == 0);

    std::vector<SchedulePoint> blue = sun_points(chi, def, DYN_BLUE);
    const SchedulePoint *rise_b = find_point(blue, chi.sunrise);
    const SchedulePoint *noon_b = find_point(blue, chi.noon);
    check("shape blue sunrise UI3 == intensity + edge violet", rise_b != nullptr && rise_b->channels[2] == 31);
    check("shape blue noon UI3 == peak violet", noon_b != nullptr && noon_b->channels[2] == 60);
    check("shape blue UI4/UI5 trace the peak",
          noon_b != nullptr && noon_b->channels[3] == 5 && noon_b->channels[4] == 5);
    check("shape blue noon CLR == 178", noon_b != nullptr && noon_b->channels[1] == 178);
  }

  // night_amber: a Tuna Sun amber floor that fades out by the civil level. Glow off, so only the
  // floor shows. Default 0 changes nothing.
  {
    SunParams base;
    SunParams zero;
    zero.night_amber = 0;
    check("night_amber 0 changes nothing",
          points_equal(sun_points(chi, base, DYN_SUN), sun_points(chi, zero, DYN_SUN)));

    SunParams p;
    p.glow = 0;
    p.night_amber = 5;
    std::vector<SchedulePoint> pts = sun_points(chi, p, DYN_SUN);
    const SchedulePoint *astro = find_point(pts, chi.astro_dawn);
    const SchedulePoint *nautical = find_point(pts, chi.nautical_dawn);
    const SchedulePoint *civil = find_point(pts, chi.civil_dawn);
    const SchedulePoint *rise = find_point(pts, chi.sunrise);
    check("night_amber 5 astro UI5 == 5", astro != nullptr && astro->channels[4] == 5);
    check("night_amber 5 nautical UI5 == 4", nautical != nullptr && nautical->channels[4] == 4);
    check("night_amber 5 civil UI5 == 2", civil != nullptr && civil->channels[4] == 2);
    check("night_amber 5 sunrise UI5 == 5", rise != nullptr && rise->channels[4] == 5);
    const SchedulePoint *astro_pm = find_point(pts, chi.astro_dusk);
    check("night_amber 5 astro dusk UI5 == 5", astro_pm != nullptr && astro_pm->channels[4] == 5);

    // The glow still wins where it is brighter than the floor.
    SunParams glow;
    glow.night_amber = 5;
    std::vector<SchedulePoint> g = sun_points(chi, glow, DYN_SUN);
    const SchedulePoint *rise_g = find_point(g, chi.sunrise);
    check("night_amber 5 keeps edge glow amber", rise_g != nullptr && rise_g->channels[4] == 60);

    // On a Tuna Blue the same floor lifts violet instead.
    SunParams blue;
    blue.glow = 0;
    blue.night_amber = 5;
    std::vector<SchedulePoint> blue_pts = sun_points(chi, blue, DYN_BLUE);
    const SchedulePoint *blue_astro = find_point(blue_pts, chi.astro_dawn);
    const SchedulePoint *blue_nautical = find_point(blue_pts, chi.nautical_dawn);
    const SchedulePoint *blue_civil = find_point(blue_pts, chi.civil_dawn);
    const SchedulePoint *blue_rise = find_point(blue_pts, chi.sunrise);
    check("blue night_amber 5 astro UI3 == 5", blue_astro != nullptr && blue_astro->channels[2] == 5);
    check("blue night_amber 5 nautical UI3 == 4", blue_nautical != nullptr && blue_nautical->channels[2] == 4);
    check("blue night_amber 5 civil UI3 == 9", blue_civil != nullptr && blue_civil->channels[2] == 9);
    check("blue night_amber 5 sunrise UI3 == 21", blue_rise != nullptr && blue_rise->channels[2] == 21);
    SunParams blue_glow;
    blue_glow.night_amber = 5;
    std::vector<SchedulePoint> blue_g = sun_points(chi, blue_glow, DYN_BLUE);
    const SchedulePoint *blue_rise_g = find_point(blue_g, chi.sunrise);
    check("blue night_amber 5 keeps edge glow violet", blue_rise_g != nullptr && blue_rise_g->channels[2] == 31);
  }

  // dip: midday siesta at 20 percent.
  {
    SunParams p;
    p.dip = 20;
    std::vector<SchedulePoint> pts = sun_points(chi, p, DYN_GENERIC);
    const SchedulePoint *noon = find_point(pts, chi.noon);
    check("dip 20 lowers noon INT to 104", noon != nullptr && noon->channels[0] == 104);
  }

  // shift 30: the noon point moves by exactly 30 minutes.
  {
    SunParams p;
    p.shift_min = 30;
    std::vector<SchedulePoint> pts = sun_points(chi, p, DYN_GENERIC);
    const SchedulePoint *noon = find_point(pts, (chi.noon + 30) % 1440);
    check("shift 30 moves noon point", noon != nullptr && noon->channels[0] == 130);
    check("shift 30 keeps noon CLR", noon != nullptr && noon->channels[1] == 120);
    check_shape(pts, p, "shift 30");
  }

  // shift -700: wraps past midnight and stays sorted and in range.
  {
    SunParams p;
    p.shift_min = -700;
    std::vector<SchedulePoint> pts = sun_points(chi, p, DYN_GENERIC);
    int want = ((chi.noon - 700) % 1440 + 1440) % 1440;
    const SchedulePoint *noon = find_point(pts, want);
    check("shift -700 wraps noon point", noon != nullptr && noon->channels[0] == 130);
    check_shape(pts, p, "shift -700");
  }

  // Moon: London 2026-11-05 with the reference numbers.
  MoonNight london;
  london.rise = 153;
  london.set = 883;
  london.illumination = 0.144;
  MoonParams mp;
  {
    std::vector<SchedulePoint> m = moon_points(london, mp, DYN_GENERIC);
    check("moon three points", m.size() == 3);
    const SchedulePoint *rise = find_point(m, 153);
    const SchedulePoint *peak = find_point(m, 518);
    const SchedulePoint *set = find_point(m, 883);
    check("moon rise INT 0", rise != nullptr && rise->channels[0] == 0);
    check("moon peak minute 518", peak != nullptr);
    check("moon peak INT round(12*0.144)", peak != nullptr && peak->channels[0] == 2);
    check("moon set INT 0", set != nullptr && set->channels[0] == 0);
    check("moon CLR 255 everywhere",
          rise != nullptr && rise->channels[1] == 255 && peak != nullptr &&
              peak->channels[1] == 255 && set != nullptr && set->channels[1] == 255);
    check("moon UI3-UI5 0 for generic",
          peak != nullptr && peak->channels[2] == 0 && peak->channels[3] == 0 &&
              peak->channels[4] == 0);

    MoonParams nofollow;
    nofollow.follow_phase = false;
    std::vector<SchedulePoint> m2 = moon_points(london, nofollow, DYN_GENERIC);
    const SchedulePoint *peak2 = find_point(m2, 518);
    check("moon peak INT full without phase", peak2 != nullptr && peak2->channels[0] == 12);
  }

  // Moon amber: Tuna Sun only, at the peak, scaled by the same intensity fraction as violet.
  {
    MoonParams dflt;
    std::vector<SchedulePoint> md = moon_points(london, dflt, DYN_SUN);
    bool zero = true;
    for (size_t i = 0; i < md.size(); i++)
      if (md[i].channels[4] != 0)
        zero = false;
    check("moon amber default 0 keeps UI5 0", zero);

    MoonParams p;
    p.amber = 50;
    std::vector<SchedulePoint> m = moon_points(london, p, DYN_SUN);
    const SchedulePoint *rise = find_point(m, 153);
    const SchedulePoint *peak = find_point(m, 518);
    const SchedulePoint *set = find_point(m, 883);
    check("moon amber peak UI5 == round(50 * 2/12)",
          peak != nullptr && peak->channels[4] == 8);
    check("moon amber rise UI5 == 0", rise != nullptr && rise->channels[4] == 0);
    check("moon amber set UI5 == 0", set != nullptr && set->channels[4] == 0);
    check("moon amber leaves UI3 0", peak != nullptr && peak->channels[2] == 0);

    MoonParams blue;
    blue.amber = 50;
    std::vector<SchedulePoint> mb = moon_points(london, blue, DYN_BLUE);
    const SchedulePoint *peak_b = find_point(mb, 518);
    check("moon amber ignored by blue", peak_b != nullptr && peak_b->channels[4] == 0 &&
                                            peak_b->channels[2] == 10);
  }

  // Sun + Moon: one curve with the sun by day and the moon lifted into the dark hours.
  {
    MoonNight night;
    night.rise = 1150;   // just after sunset on the reference day
    night.set = 153;     // before sunrise the next morning
    night.illumination = 0.5;
    std::vector<SchedulePoint> merged = sun_moon_points(chi, night, def, mp, DYN_BLUE);
    const SchedulePoint *noon = find_point(merged, chi.noon);
    const SchedulePoint *moon_peak = find_point(merged, 1371);
    check("sun+moon keeps the sun's noon", noon != nullptr && noon->channels[0] == 130);
    check("sun+moon has the moon peak", moon_peak != nullptr && moon_peak->channels[0] == 6);
    check("sun+moon moon peak is violet", moon_peak != nullptr && moon_peak->channels[2] == 30);
    check("sun+moon fits the slots", merged.size() <= 16);
    bool sorted = true;
    for (size_t i = 1; i < merged.size(); i++)
      if (merged[i].minute <= merged[i - 1].minute)
        sorted = false;
    check("sun+moon points sorted and distinct", sorted);
    uint8_t night_out[NUM_CHANNELS] = {0, 0, 0, 0, 0};
    uint8_t sun_out[NUM_CHANNELS] = {0, 0, 0, 0, 0};
    std::vector<SchedulePoint> sun_only = sun_points(chi, def, DYN_BLUE);
    check("moon lights part of the night", schedule_compute(merged, 1380 * 60, night_out) && night_out[0] > 0);
    check("sun alone is dark there", schedule_compute(sun_only, 1380 * 60, sun_out) && sun_out[0] == 0);
  }

  // Moon crossing midnight: set 300 comes before rise 1100, so the peak lands at 1420.
  MoonNight cross;
  cross.rise = 1100;
  cross.set = 300;
  cross.illumination = 1.0;
  {
    std::vector<SchedulePoint> m = moon_points(cross, mp, DYN_BLUE);
    check("moon midnight three points", m.size() == 3);
    check("moon midnight sorted 300,1100,1420",
          m.size() == 3 && m[0].minute == 300 && m[1].minute == 1100 && m[2].minute == 1420);
    uint8_t out[NUM_CHANNELS];
    check("moon midnight peak value", schedule_compute(m, 1420 * 60, out) && out[0] == 12);
    check("moon midnight set zero", schedule_compute(m, 300 * 60, out) && out[0] == 0);
    check("moon midnight between zero", schedule_compute(m, 700 * 60, out) && out[0] == 0);
    check("moon midnight CLR", out[1] == 255);
    const SchedulePoint *peak = find_point(m, 1420);
    check("moon blue UI3 == violet at peak", peak != nullptr && peak->channels[2] == 60);
    check("moon blue UI3 == 0 at rise", m.size() == 3 && m[0].channels[2] == 0);
    check("moon blue UI4/UI5 == 0",
          peak != nullptr && peak->channels[3] == 0 && peak->channels[4] == 0);
  }

  // Moon without a rise or set that day.
  {
    MoonNight none;
    none.rise = -1;
    none.set = 400;
    none.illumination = 0.5;
    std::vector<SchedulePoint> m = moon_points(none, mp, DYN_BLUE);
    check("moon unavailable one point", m.size() == 1);
    check("moon unavailable at 00:00", !m.empty() && m[0].minute == 0);
    check("moon unavailable INT 0", !m.empty() && m[0].channels[0] == 0);
    check("moon unavailable UI3 0", !m.empty() && m[0].channels[2] == 0);
  }

  // Two events on the same minute: the later one moves a minute on, and is dropped past 1439.
  {
    SunParams p;
    SunDay collide;
    collide.civil_dawn = 300;
    collide.sunrise = 300;
    std::vector<SchedulePoint> moved = sun_points(collide, p, DYN_GENERIC);
    check("collision moves point one minute later",
          moved.size() == 2 && moved[0].minute == 300 && moved[1].minute == 301);

    SunDay edge;
    edge.civil_dawn = 1439;
    edge.sunrise = 1439;
    std::vector<SchedulePoint> dropped = sun_points(edge, p, DYN_GENERIC);
    check("collision past 1439 is dropped", dropped.size() == 1 && dropped[0].minute == 1439);
  }

  // No event at all and no polar day: one point holding the floor color.
  {
    SunParams p;
    SunDay empty;
    std::vector<SchedulePoint> pts = sun_points(empty, p, DYN_SUN);
    check("empty day one point", pts.size() == 1);
    check("empty day at 00:00", !pts.empty() && pts[0].minute == 0);
    check("empty day INT == floor", !pts.empty() && pts[0].channels[0] == 0);
    check("empty day CLR == warm", !pts.empty() && pts[0].channels[1] == 0);
    check("empty day no glow",
          !pts.empty() && pts[0].channels[2] == 0 && pts[0].channels[3] == 0 &&
              pts[0].channels[4] == 0);
  }

  // Polar day with a midday dip: still one point, now below the peak.
  {
    SunParams p;
    p.dip = 20;
    SunDay up;
    up.always_up = true;
    std::vector<SchedulePoint> pts = sun_points(up, p, DYN_GENERIC);
    check("always_up with dip one point", pts.size() == 1);
    check("always_up with dip INT 104", !pts.empty() && pts[0].channels[0] == 104);
  }

  // Moon shift: the three points move together.
  {
    MoonParams p;
    p.shift_min = 30;
    std::vector<SchedulePoint> m = moon_points(london, p, DYN_GENERIC);
    check("moon shift moves rise", find_point(m, 183) != nullptr);
    check("moon shift moves peak", find_point(m, 548) != nullptr);
    check("moon shift moves set", find_point(m, 913) != nullptr);
  }

  // Determinism: the same inputs give the same vectors.
  {
    std::vector<SchedulePoint> a = sun_points(chi, def, DYN_SUN);
    std::vector<SchedulePoint> b = sun_points(chi, def, DYN_SUN);
    check("sun points deterministic", points_equal(a, b));
    std::vector<SchedulePoint> c = moon_points(london, mp, DYN_BLUE);
    std::vector<SchedulePoint> d = moon_points(london, mp, DYN_BLUE);
    check("moon points deterministic", points_equal(c, d));
  }

  // Ordinary lights: brightness only, RGB (UI3-UI5 percent) and RGBW.
  {
    SunDay day;
    day.astro_dawn = 300; day.nautical_dawn = 340; day.civil_dawn = 380; day.sunrise = 420; day.golden_am = 480;
    day.noon = 720; day.golden_pm = 960; day.sunset = 1020; day.civil_dusk = 1060; day.nautical_dusk = 1100;
    day.astro_dusk = 1140;
    SunParams sp;
    std::vector<SchedulePoint> b = sun_points(day, sp, DYN_LIGHT_BRIGHTNESS);
    bool only_int = true;
    for (size_t i = 0; i < b.size(); i++)
      for (size_t c = 1; c < NUM_CHANNELS; c++)
        if (b[i].channels[c] != 0) only_int = false;
    check("brightness light: intensity only", only_int && b.size() == 11);
    std::vector<SchedulePoint> rgb = sun_points(day, sp, DYN_LIGHT_RGB), full = sun_points(day, sp, DYN_GENERIC);
    // Noon is the coolest, bluest white; the sunrise glow is the reddest and has less blue.
    size_t noon = 5, rise = 3;
    check("rgb light: noon is whiter than the sunrise", rgb[noon].channels[4] > rgb[rise].channels[4] + 30 &&
                                                          rgb[noon].channels[3] > rgb[rise].channels[3] &&
                                                          rgb[noon].channels[1] == 0);
    check("rgb light: sunrise glows orange", rgb[rise].channels[2] == 100 && rgb[rise].channels[4] < 20);
    check("rgb light: intensity equals the generic day", rgb[noon].channels[0] == full[noon].channels[0]);
    std::vector<SchedulePoint> w = sun_points(day, sp, DYN_LIGHT_RGBW);
    check("rgbw light: white level follows the cool end", w[noon].channels[1] == sp.clr_cool && w[0].channels[1] == 0);
    MoonNight n;
    n.rise = 1100; n.set = 300; n.illumination = 1.0;
    MoonParams mp;
    mp.max_int = 12;
    std::vector<SchedulePoint> m = moon_points(n, mp, DYN_LIGHT_RGB);
    size_t peak = 0;
    for (size_t i = 0; i < m.size(); i++)
      if (m[i].channels[0] > m[peak].channels[0]) peak = i;
    check("rgb light moon: a cool blue", m[peak].channels[4] == 100 && m[peak].channels[2] < m[peak].channels[4] &&
                                          m[peak].channels[1] == 0);
    check("brightness light moon: intensity only", moon_points(n, mp, DYN_LIGHT_BRIGHTNESS)[peak].channels[2] == 0);
  }

  // A gentler ramp moves the near-peak point away from sunrise, and leaves the real sun alone at 0.
  {
    SunDay day;
    day.astro_dawn = 300; day.nautical_dawn = 340; day.civil_dawn = 380; day.sunrise = 420; day.golden_am = 450;
    day.noon = 780; day.golden_pm = 1110; day.sunset = 1140; day.civil_dusk = 1180; day.nautical_dusk = 1220;
    day.astro_dusk = 1260;
    SunParams real;
    SunParams ramp;
    ramp.ramp_min = 150;
    std::vector<SchedulePoint> a = sun_points(day, real, DYN_GENERIC), b = sun_points(day, ramp, DYN_GENERIC);
    check("ramp 0 keeps the real golden hour", a[4].minute == 450 && a[6].minute == 1110);
    check("ramp moves the morning point to sunrise + 150", b[4].minute == 570 && b[4].channels[0] == a[4].channels[0]);
    check("ramp moves the evening point to sunset - 150", b[6].minute == 990);
    check("ramp keeps noon and the twilight events", b[5].minute == 780 && b[3].minute == 420);
    SunParams huge;
    huge.ramp_min = 600;  // never closer than half an hour to noon
    std::vector<SchedulePoint> c = sun_points(day, huge, DYN_GENERIC);
    check("ramp stops half an hour before noon", c[4].minute == 750 && c[6].minute == 810);
  }

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
