// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_model_defaults.cpp && ./a.out
// The stock schedule a group uses when it has no YAML point entities.
#include <cstdio>

#include "klink_model_defaults.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    failures++;
  }
}

int main() {
  const SchedulePoint *blue = stock_defaults(1);
  const SchedulePoint *sun = stock_defaults(2);
  const SchedulePoint *ref = stock_defaults(3);
  check("blue 08:00 int", blue[0].minute == 8 * 60 && blue[0].channels[0] == 2 && blue[0].enabled);
  check("blue 13:00 peak", blue[3].minute == 13 * 60 && blue[3].channels[0] == 153 && blue[3].channels[1] == 178);
  check("blue 20:00 dark", blue[7].minute == 20 * 60 && blue[7].channels[0] == 0 && blue[7].enabled);
  check("blue spare off", !blue[8].enabled && blue[8].channels[0] == 0);
  check("sun 09:00 amber", sun[1].minute == 9 * 60 && sun[1].channels[4] == 30);
  check("sun 13:00 warm", sun[3].minute == 13 * 60 && sun[3].channels[1] == 89);
  check("refugium 08:00 dark", ref[0].minute == 8 * 60 && ref[0].channels[0] == 0);
  check("refugium 09:00 light", ref[1].minute == 9 * 60 && ref[1].channels[1] == 150);
  check("refugium 17:00 light", ref[2].minute == 17 * 60 && ref[2].channels[1] == 150);
  check("refugium 18:00 dark", ref[3].minute == 18 * 60 && ref[3].channels[0] == 0);
  check("refugium spare off", !ref[4].enabled);
  check("spare count", STOCK_POINTS == 16);

  SchedulePoint p;
  check("point 1", stock_default_point(2, 0, p) && p.minute == 8 * 60 && p.channels[0] == 2);
  check("point 16", stock_default_point(2, 15, p) && !p.enabled);
  check("past the end", !stock_default_point(2, 16, p));
  return failures == 0 ? 0 : 1;
}
