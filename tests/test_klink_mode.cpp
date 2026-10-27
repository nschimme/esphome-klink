// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink tests/test_klink_mode.cpp -o /tmp/t_mode && /tmp/t_mode
#include <cstdio>
#include <cstring>
#include "klink_mode.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}

int main() {
  ModeState s;
  check("defaults", s.mode == Mode::PROGRAM && s.before_preview == Mode::PROGRAM && s.preview_deadline_ms == 0);

  s = ModeState{};
  on_user_command(s);
  check("program -> manual", s.mode == Mode::MANUAL && s.before_preview == Mode::PROGRAM);

  s = ModeState{};
  s.mode = Mode::OFF;
  on_user_command(s);
  check("off -> manual", s.mode == Mode::MANUAL);

  s = ModeState{};
  s.mode = Mode::MANUAL;
  s.before_preview = Mode::PROGRAM;
  on_user_command(s);
  check("manual stays", s.mode == Mode::MANUAL && s.before_preview == Mode::PROGRAM);

  s = ModeState{};
  s.mode = Mode::PREVIEW;
  s.before_preview = Mode::PROGRAM;
  on_user_command(s);
  check("preview user command restores manual", s.mode == Mode::PREVIEW && s.before_preview == Mode::MANUAL);

  s = ModeState{};
  set_mode(s, Mode::OFF);
  check("set off", s.mode == Mode::OFF);
  set_mode(s, Mode::MANUAL);
  check("set manual", s.mode == Mode::MANUAL);
  set_mode(s, Mode::PROGRAM);
  check("set program", s.mode == Mode::PROGRAM);

  s = ModeState{};
  set_mode(s, Mode::PREVIEW);
  check("set preview ignored", s.mode == Mode::PROGRAM && s.before_preview == Mode::PROGRAM);

  s = ModeState{};
  begin_preview(s, 1000, 5000);
  check("begin preview", s.mode == Mode::PREVIEW && s.before_preview == Mode::PROGRAM && s.preview_deadline_ms == 6000);

  s = ModeState{};
  begin_preview(s, 1000, 5000);
  set_mode(s, Mode::OFF);
  check("set during preview ends it", s.mode == Mode::OFF && s.before_preview == Mode::OFF);

  s = ModeState{};
  begin_preview(s, 1000, 5000);
  set_mode(s, Mode::MANUAL);
  check("set manual during preview", s.mode == Mode::MANUAL && s.before_preview == Mode::MANUAL);

  s = ModeState{};
  begin_preview(s, 1000, 5000);
  set_mode(s, Mode::PROGRAM);
  check("set program during preview", s.mode == Mode::PROGRAM && s.before_preview == Mode::PROGRAM);

  s = ModeState{};
  begin_preview(s, 1000, 5000);
  set_mode(s, Mode::PREVIEW);
  check("set preview during preview ignored", s.mode == Mode::PREVIEW && s.before_preview == Mode::PROGRAM);

  s = ModeState{};
  begin_preview(s, 1000, 5000);
  end_preview(s);
  check("preview restores program", s.mode == Mode::PROGRAM && s.before_preview == Mode::PROGRAM);

  s = ModeState{};
  s.mode = Mode::MANUAL;
  begin_preview(s, 1000, 5000);
  end_preview(s);
  check("preview restores manual", s.mode == Mode::MANUAL && s.before_preview == Mode::MANUAL);

  s = ModeState{};
  s.mode = Mode::OFF;
  begin_preview(s, 1000, 5000);
  end_preview(s);
  check("preview restores off", s.mode == Mode::OFF && s.before_preview == Mode::OFF);

  s = ModeState{};
  end_preview(s);
  check("end preview outside preview no-op", s.mode == Mode::PROGRAM);

  s = ModeState{};
  begin_preview(s, 1000, 5000);
  begin_preview(s, 2000, 7000);
  check("begin preview idempotent", s.mode == Mode::PREVIEW && s.preview_deadline_ms == 6000 && s.before_preview == Mode::PROGRAM);

  s = ModeState{};
  begin_preview(s, 1000, 5000);
  refresh_preview(s, 2000, 3000);
  check("refresh preview", s.preview_deadline_ms == 5000);
  end_preview(s);
  uint32_t old_deadline = s.preview_deadline_ms;
  refresh_preview(s, 3000, 3000);
  check("refresh only in preview", s.mode == Mode::PROGRAM && s.preview_deadline_ms == old_deadline);

  s = ModeState{};
  check("not expired when not preview", !preview_expired(s, 9999));
  begin_preview(s, 1000, 100);
  check("not expired before deadline", !preview_expired(s, 1099));
  check("expired at deadline", preview_expired(s, 1100));
  check("expired after deadline", preview_expired(s, 1200));

  s = ModeState{};
  begin_preview(s, 0xFFFFFFF0u, 100);
  check("wrap deadline", s.preview_deadline_ms == 0x54);
  check("wrap not expired before", !preview_expired(s, 0xFFFFFFF0u));
  check("wrap not expired just before", !preview_expired(s, 0x53));
  check("wrap expired at deadline", preview_expired(s, 0x54));

  check("mode names", std::strcmp(mode_name(Mode::PROGRAM), "Program") == 0 &&
                          std::strcmp(mode_name(Mode::MANUAL), "Manual") == 0 &&
                          std::strcmp(mode_name(Mode::OFF), "Off") == 0 &&
                          std::strcmp(mode_name(Mode::PREVIEW), "Preview") == 0);

  Mode out = Mode::PREVIEW;
  check("from name program", mode_from_name("program", out) && out == Mode::PROGRAM);
  check("from name PROGRAM", mode_from_name("PROGRAM", out) && out == Mode::PROGRAM);
  check("from name mixed case", mode_from_name("pRoGrAm", out) && out == Mode::PROGRAM);
  check("from name manual", mode_from_name("Manual", out) && out == Mode::MANUAL);
  check("from name off", mode_from_name("OFF", out) && out == Mode::OFF);

  out = Mode::PROGRAM;
  check("from name preview rejected", !mode_from_name("preview", out) && out == Mode::PROGRAM);
  check("from name empty rejected", !mode_from_name("", out) && out == Mode::PROGRAM);
  check("from name nullptr rejected", !mode_from_name(nullptr, out) && out == Mode::PROGRAM);
  check("from name trailing rejected", !mode_from_name("program ", out) && out == Mode::PROGRAM);
  check("from name junk rejected", !mode_from_name("auto", out) && out == Mode::PROGRAM);

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
