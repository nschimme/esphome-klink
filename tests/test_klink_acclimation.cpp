// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_acclimation.cpp -o <out> && <out>
#include <cmath>
#include <cstdio>
#include "klink_acclimation.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
  AcclimationPlan p;
  check("off is full", acclimation_scale(p, 5000000) == 1.0f);
  p.on = true; p.start_pct = 30; p.days = 10; p.since = 1000000;
  check("at the start: the starting share", near(acclimation_scale(p, 1000000), 0.3f));
  check("before the start: the starting share", near(acclimation_scale(p, 999000), 0.3f));
  check("halfway", near(acclimation_scale(p, 1000000 + 5 * 86400), 0.65f));
  check("a quarter of a day in is smooth, not a daily step", acclimation_scale(p, 1000000 + 21600) > 0.3f);
  check("monotonic over the ramp", [&] {
    float last = 0.0f;
    for (uint32_t t = 1000000; t <= 1000000 + 10 * 86400; t += 3600) {
      float s = acclimation_scale(p, t);
      if (s < last) return false;
      last = s;
    }
    return true;
  }());
  check("at the end: full", acclimation_scale(p, 1000000 + 10 * 86400) == 1.0f);
  check("after the end: full", acclimation_scale(p, 1000000 + 30 * 86400) == 1.0f);
  check("not finished before the end", !acclimation_finished(p, 1000000 + 9 * 86400));
  check("finished at the end", acclimation_finished(p, 1000000 + 10 * 86400));
  AcclimationPlan waiting = p;
  waiting.since = 0;
  check("clock unknown: no effect yet", acclimation_scale(waiting, 5000000) == 1.0f && !acclimation_finished(waiting, 5000000));
  AcclimationPlan off = p;
  off.on = false;
  check("off is never finished", !acclimation_finished(off, 1000000 + 30 * 86400));
  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
