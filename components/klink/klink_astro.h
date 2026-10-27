#pragma once
// Sun and moon positions for astronomical schedules. NOAA solar calculator and Meeus low-precision
// lunar theory; the sun is geometric (center, no refraction), the moon includes topocentric parallax.
// All angles in degrees, longitude positive east, times are minutes of the local day (0..1440).
#include <cmath>
#include <cstdint>

namespace kessil {
namespace klink {

static const double ASTRO_DEG = 3.14159265358979323846 / 180.0;
// Rise/set threshold for moon_elevation (topocentric center, refraction and semidiameter included).
static const double K_MOON_HORIZON = -0.566;

inline double astro_norm360(double d) {
  d = std::fmod(d, 360.0);
  return d < 0.0 ? d + 360.0 : d;
}
inline double sind(double d) { return std::sin(d * ASTRO_DEG); }
inline double cosd(double d) { return std::cos(d * ASTRO_DEG); }
inline double asind(double x) { return std::asin(x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x)) / ASTRO_DEG; }
inline double atan2d(double y, double x) { return std::atan2(y, x) / ASTRO_DEG; }

// Julian Day of a Gregorian calendar date at `ut_hours` UT.
inline double julian_day(int year, int month, int day, double ut_hours) {
  int y = year, m = month;
  if (m <= 2) {
    y--;
    m += 12;
  }
  int a = y / 100;
  int b = 2 - a + a / 4;
  return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + day + b - 1524.5 +
         ut_hours / 24.0;
}

// NOAA solar calculator: declination (deg) and equation of time (minutes) for a Julian Day.
// Geometric mean longitude/anomaly, equation of center, true longitude, apparent longitude
// (nutation + aberration), corrected obliquity.
inline void sun_decl_eqtime(double jd, double *decl, double *eqtime) {
  double T = (jd - 2451545.0) / 36525.0;
  double L0 = astro_norm360(280.46646 + T * (36000.76983 + 0.0003032 * T));
  double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
  double e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
  double C = sind(M) * (1.914602 - T * (0.004817 + 0.000014 * T)) +
             sind(2.0 * M) * (0.019993 - 0.000101 * T) + sind(3.0 * M) * 0.000289;
  double omega = 125.04 - 1934.136 * T;
  double lambda = L0 + C - 0.00569 - 0.00478 * sind(omega);
  double eps0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
  double eps = eps0 + 0.00256 * cosd(omega);
  *decl = asind(sind(eps) * sind(lambda));
  double y = (1.0 - cosd(eps)) / (1.0 + cosd(eps));  // tan^2(eps/2)
  double eq = y * sind(2.0 * L0) - 2.0 * e * sind(M) + 4.0 * e * y * sind(M) * cosd(2.0 * L0) -
              0.5 * y * y * sind(4.0 * L0) - 1.25 * e * e * sind(2.0 * M);
  *eqtime = 4.0 * eq / ASTRO_DEG;  // radians -> minutes (4 min per degree)
}

// Geometric elevation (no refraction) of the sun's center.
inline double sun_elevation(double jd, double lat, double lon) {
  double decl, eq;
  sun_decl_eqtime(jd, &decl, &eq);
  double jd0 = std::floor(jd + 0.5) - 0.5;  // previous 00:00 UT
  double ut_min = (jd - jd0) * 1440.0;
  double ha = (ut_min + eq + 4.0 * lon) / 4.0 - 180.0;  // true solar time -> hour angle
  return asind(sind(lat) * sind(decl) + cosd(lat) * cosd(decl) * cosd(ha));
}

// Local clock minute of solar noon from the equation of time.
inline double solar_noon_minute(int y, int m, int d, double /*lat*/, double lon, int tz_min) {
  double decl, eq;
  double jd = julian_day(y, m, d, 12.0) - tz_min / 1440.0;
  sun_decl_eqtime(jd, &decl, &eq);
  return 720.0 - 4.0 * lon - eq + tz_min;
}

// First crossing in the local day of `elev` through `angle` in the given direction, refined by
// bisection to `tol` minutes. Shared by sun_crossing and moon_crossing.
inline bool astro_crossing(double (*elev)(double, double, double), int y, int m, int d, double lat,
                           double lon, int tz_min, double angle, bool rising, double step,
                           double tol, double *minute) {
  double jd0 = julian_day(y, m, d, 0.0) - tz_min / 1440.0;
  int n = static_cast<int>(1440.0 / step + 0.5);
  double t = 0.0;
  double prev = elev(jd0, lat, lon);
  for (int i = 1; i <= n; i++) {
    double t1 = i * step;
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

// First time in the local day the sun's center crosses `angle` going up (rising) or down.
inline bool sun_crossing(int y, int m, int d, double lat, double lon, int tz_min, double angle,
                         bool rising, double *minute) {
  return astro_crossing(&sun_elevation, y, m, d, lat, lon, tz_min, angle, rising, 5.0, 0.1, minute);
}

// Illuminated fraction 0..1 from Meeus' phase angle (low precision, principal terms).
inline double moon_illumination(double jd) {
  double T = (jd - 2451545.0) / 36525.0;
  double D = astro_norm360(297.8501921 + 445267.1114034 * T - 0.0018819 * T * T +
                           T * T * T / 545868.0 - T * T * T * T / 113065000.0);
  double M = 357.5291092 + 35999.0502909 * T - 0.0001536 * T * T + T * T * T / 24490000.0;
  double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T * T + T * T * T / 69699.0 -
              T * T * T * T / 14712000.0;
  double i = 180.0 - D - 6.289 * sind(Mp) + 2.100 * sind(M) - 1.274 * sind(2.0 * D - Mp) -
             0.658 * sind(2.0 * D) - 0.214 * sind(2.0 * Mp) - 0.110 * sind(D);
  return 0.5 * (1.0 + cosd(i));
}

// Geocentric apparent right ascension and declination (Meeus ch. 47).
// Longitude terms kept (10 largest of table 47.A): 6288774 sin M', 1274027 sin(2D-M'),
// 658314 sin 2D, 213618 sin 2M', -185116 sin M, -114332 sin 2F, 58793 sin(2D-2M'),
// 57066 sin(2D-M-M'), 53322 sin(2D+M'), 45758 sin(2D-M), plus the additive 3958 sin A1,
// 1962 sin(L'-F), 318 sin A2.
// Latitude terms kept (6 largest of table 47.B): 5128122 sin F, 280602 sin(M'+F), 277693 sin(M'-F),
// 173237 sin(2D-F), 55413 sin(2D-M'+F), 46271 sin(2D-M'-F), plus the additive -2235 sin L',
// 382 sin A3, 175 sin(A1-F), 175 sin(A1+F), 127 sin(L'-M'), -115 sin(L'+M').
inline void moon_position(double jd, double *ra_deg, double *dec_deg) {
  double T = (jd - 2451545.0) / 36525.0;
  double t2 = T * T, t3 = t2 * T, t4 = t3 * T;
  double Lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * t2 + t3 / 538841.0 - t4 / 65194000.0;
  double D = 297.8501921 + 445267.1114034 * T - 0.0018819 * t2 + t3 / 545868.0 - t4 / 113065000.0;
  double M = 357.5291092 + 35999.0502909 * T - 0.0001536 * t2 + t3 / 24490000.0;
  double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * t2 + t3 / 69699.0 - t4 / 14712000.0;
  double F = 93.2720950 + 483202.0175233 * T - 0.0036539 * t2 - t3 / 3526000.0 + t4 / 863310000.0;
  double A1 = 119.75 + 131.849 * T;
  double A2 = 53.09 + 479264.290 * T;
  double A3 = 313.45 + 481266.484 * T;
  double E = 1.0 - 0.002516 * T - 0.0000074 * t2;

  double sl = 6288774.0 * sind(Mp) + 1274027.0 * sind(2.0 * D - Mp) + 658314.0 * sind(2.0 * D) +
              213618.0 * sind(2.0 * Mp) - 185116.0 * E * sind(M) - 114332.0 * sind(2.0 * F) +
              58793.0 * sind(2.0 * D - 2.0 * Mp) + 57066.0 * E * sind(2.0 * D - M - Mp) +
              53322.0 * sind(2.0 * D + Mp) + 45758.0 * E * sind(2.0 * D - M);
  sl += 3958.0 * sind(A1) + 1962.0 * sind(Lp - F) + 318.0 * sind(A2);
  double sb = 5128122.0 * sind(F) + 280602.0 * sind(Mp + F) + 277693.0 * sind(Mp - F) +
              173237.0 * sind(2.0 * D - F) + 55413.0 * sind(2.0 * D - Mp + F) +
              46271.0 * sind(2.0 * D - Mp - F);
  sb += -2235.0 * sind(Lp) + 382.0 * sind(A3) + 175.0 * sind(A1 - F) + 175.0 * sind(A1 + F) +
        127.0 * sind(Lp - Mp) - 115.0 * sind(Lp + Mp);

  double lambda = Lp + sl / 1000000.0;
  double beta = sb / 1000000.0;
  double omega = 125.04 - 1934.136 * T;
  lambda -= 0.00478 * sind(omega);  // nutation in longitude
  double eps = 23.4392911 - 0.0130042 * T + 0.00256 * cosd(omega);

  double x = cosd(beta) * cosd(lambda);
  double y = cosd(beta) * sind(lambda) * cosd(eps) - sind(beta) * sind(eps);
  double z = cosd(beta) * sind(lambda) * sind(eps) + sind(beta) * cosd(eps);
  *ra_deg = astro_norm360(atan2d(y, x));
  *dec_deg = asind(z);
}

// Topocentric altitude of the moon in degrees (geocentric position minus horizontal parallax).
inline double moon_elevation(double jd, double lat, double lon) {
  double ra, dec;
  moon_position(jd, &ra, &dec);
  double T = (jd - 2451545.0) / 36525.0;
  double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * T * T -
                T * T * T / 38710000.0;
  double ha = astro_norm360(gmst + lon - ra);
  double alt = asind(sind(lat) * sind(dec) + cosd(lat) * cosd(dec) * cosd(ha));
  return alt - 0.9507 * cosd(alt);
}

// First moon rise (rising) or set in the local day.
inline bool moon_crossing(int y, int m, int d, double lat, double lon, int tz_min, bool rising,
                          double *minute) {
  return astro_crossing(&moon_elevation, y, m, d, lat, lon, tz_min, K_MOON_HORIZON, rising, 10.0,
                        0.5, minute);
}

}  // namespace klink
}  // namespace kessil
