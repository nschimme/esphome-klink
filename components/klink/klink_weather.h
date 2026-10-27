#pragma once
// How cloudy a Home Assistant weather condition is, and whether it is a thunderstorm. Pure (host tested).
#include <cstring>
#include <string>

namespace kessil {
namespace klink {

// Cloud cover 0 (clear) to 1 (dark overcast) for the condition names a Home Assistant weather entity reports
// as its state. An unknown, unavailable or empty condition counts as clear: the weather must never darken the
// lamps on a guess.
inline float weather_cloud_factor(const std::string &condition) {
  struct Entry {
    const char *name;
    float factor;
  };
  static const Entry TABLE[] = {
      {"sunny", 0.0f},         {"clear-night", 0.0f},     {"exceptional", 0.0f},      {"windy", 0.1f},
      {"partlycloudy", 0.3f},  {"windy-variant", 0.3f},   {"cloudy", 0.6f},           {"snowy", 0.7f},
      {"fog", 0.7f},           {"hail", 0.75f},           {"rainy", 0.75f},           {"snowy-rainy", 0.8f},
      {"pouring", 0.85f},      {"lightning", 0.85f},      {"lightning-rainy", 0.95f},
  };
  for (const Entry &e : TABLE)
    if (condition == e.name)
      return e.factor;
  return 0.0f;
}

inline bool weather_is_thunder(const std::string &condition) {
  return condition == "lightning" || condition == "lightning-rainy";
}

// The next value of a slowly moving factor: at most `max_step` per call toward the target, so clouds fade in and
// out over minutes and never flicker.
inline float weather_ease(float current, float target, float max_step) {
  if (target > current)
    return current + max_step < target ? current + max_step : target;
  return current - max_step > target ? current - max_step : target;
}

}  // namespace klink
}  // namespace kessil
