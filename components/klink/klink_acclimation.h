#pragma once
// Acclimation ramp: a new tank or coral gets used to the light by raising the whole program's intensity from a
// starting percentage to 100 % over a number of days. Pure (host tested).
#include <cstdint>

namespace kessil {
namespace klink {

struct AcclimationPlan {
  bool on = false;
  uint8_t start_pct = 30;  // intensity share on the first day, 10-90
  uint8_t days = 14;       // days until 100 %, 1-90
  uint32_t since = 0;      // the clock (epoch seconds) when it began; 0 until the clock is known
};

// The share (0.1 to 1) of the program's intensity to use at `now` (epoch seconds). 1 when off, not yet started
// or finished. Rises smoothly, second by second, not in daily steps.
inline float acclimation_scale(const AcclimationPlan &plan, uint32_t now) {
  if (!plan.on || plan.since == 0 || now <= plan.since || plan.days == 0)
    return plan.on && plan.since != 0 && now <= plan.since ? plan.start_pct / 100.0f : 1.0f;
  const double length = static_cast<double>(plan.days) * 86400.0;
  const double elapsed = static_cast<double>(now - plan.since);
  if (elapsed >= length)
    return 1.0f;
  const double start = plan.start_pct / 100.0;
  return static_cast<float>(start + (1.0 - start) * (elapsed / length));
}

inline bool acclimation_finished(const AcclimationPlan &plan, uint32_t now) {
  return plan.on && plan.since != 0 && plan.days != 0 &&
         static_cast<double>(now - plan.since) >= static_cast<double>(plan.days) * 86400.0 && now > plan.since;
}

}  // namespace klink
}  // namespace kessil
