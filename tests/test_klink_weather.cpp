// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_weather.cpp -o <out> && <out>
#include <cmath>
#include <cstdio>
#include "klink_weather.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

int main() {
  check("sunny is clear", weather_cloud_factor("sunny") == 0.0f);
  check("partly cloudy", std::fabs(weather_cloud_factor("partlycloudy") - 0.3f) < 1e-6f);
  check("cloudy", std::fabs(weather_cloud_factor("cloudy") - 0.6f) < 1e-6f);
  check("rain is darker than cloud", weather_cloud_factor("rainy") > weather_cloud_factor("cloudy"));
  check("pouring darker than rain", weather_cloud_factor("pouring") > weather_cloud_factor("rainy"));
  check("storm is the darkest", weather_cloud_factor("lightning-rainy") > weather_cloud_factor("lightning"));
  check("unknown is clear", weather_cloud_factor("unavailable") == 0.0f && weather_cloud_factor("") == 0.0f &&
                                weather_cloud_factor("unknown") == 0.0f && weather_cloud_factor("bogus") == 0.0f);
  check("all factors within 0..1", [] {
    const char *names[] = {"sunny", "clear-night", "cloudy", "fog", "hail", "lightning", "lightning-rainy",
                           "partlycloudy", "pouring", "rainy", "snowy", "snowy-rainy", "windy", "windy-variant",
                           "exceptional"};
    for (const char *n : names) {
      float f = weather_cloud_factor(n);
      if (f < 0.0f || f > 1.0f) return false;
    }
    return true;
  }());
  check("thunder", weather_is_thunder("lightning") && weather_is_thunder("lightning-rainy") &&
                       !weather_is_thunder("rainy") && !weather_is_thunder("pouring"));

  // Easing: never jumps, lands exactly, works both ways.
  float v = 0.0f;
  int steps = 0;
  while (v != 0.6f && steps < 10000) { v = weather_ease(v, 0.6f, 0.002f); steps++; }
  check("ease up lands exactly after about 300 steps", v == 0.6f && steps >= 299 && steps <= 302);
  float down = weather_ease(0.6f, 0.0f, 0.002f);
  check("ease down moves one step", std::fabs(down - 0.598f) < 1e-6f);
  check("ease holds at target", weather_ease(0.3f, 0.3f, 0.002f) == 0.3f);

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
