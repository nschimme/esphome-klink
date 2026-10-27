// Host test: g++ -std=c++11 -Wall -Wextra -I components/klink test_klink_schedule.cpp && ./a.out
#include <cstdio>
#include "klink_schedule.h"

using namespace kessil::klink;

static int failures = 0;
static void check(const char *name, bool ok) {
  if (!ok) { std::printf("FAIL %s\n", name); failures++; }
}
static SchedulePoint pt(int h, int m, int a, int b, int c, int d, int e, bool on = true) {
  SchedulePoint p;
  p.minute = static_cast<uint16_t>(h * 60 + m);
  p.channels[0] = a; p.channels[1] = b; p.channels[2] = c; p.channels[3] = d; p.channels[4] = e;
  p.enabled = on;
  return p;
}
static bool is(const uint8_t *o, int a, int b, int c, int d, int e) {
  return o[0] == a && o[1] == b && o[2] == c && o[3] == d && o[4] == e;
}
static uint32_t at(int h, int m = 0, int s = 0) { return h * 3600 + m * 60 + s; }

int main() {
  uint8_t o[5];
  std::vector<SchedulePoint> none;
  check("no points", !schedule_compute(none, 0, o));
  check("all disabled", !schedule_compute({pt(8, 0, 1, 1, 1, 1, 1, false)}, 0, o));

  check("single point holds", schedule_compute({pt(8, 0, 10, 20, 30, 40, 50)}, at(3), o) && is(o, 10, 20, 30, 40, 50));

  std::vector<SchedulePoint> day = {pt(8, 0, 0, 0, 0, 0, 0), pt(12, 0, 200, 100, 100, 50, 10)};
  check("at first point", schedule_compute(day, at(8), o) && is(o, 0, 0, 0, 0, 0));
  check("midpoint", schedule_compute(day, at(10), o) && is(o, 100, 50, 50, 25, 5));
  check("at second point", schedule_compute(day, at(12), o) && is(o, 200, 100, 100, 50, 10));
  // After 12:00 it ramps back toward the 08:00 point of tomorrow (20 h span); at 22:00 that is 10/20.
  check("wraps to next day", schedule_compute(day, at(22), o) && is(o, 100, 50, 50, 25, 5));
  // Before the first point it continues the previous day's ramp: 02:00 is 14 h into the 20 h span.
  check("before first point", schedule_compute(day, at(2), o) && is(o, 60, 30, 30, 15, 3));

  std::vector<SchedulePoint> night = {pt(22, 0, 0, 0, 0, 0, 0), pt(6, 0, 100, 100, 100, 100, 100)};
  check("ramp across midnight", schedule_compute(night, at(2), o) && is(o, 50, 50, 50, 50, 50));
  check("seconds interpolate", schedule_compute({pt(0, 0, 0, 0, 0, 0, 0), pt(0, 2, 120, 0, 0, 0, 0)}, 30, o) && o[0] == 30);

  std::vector<SchedulePoint> unsorted = {pt(12, 0, 200, 0, 0, 0, 0), pt(8, 0, 0, 0, 0, 0, 0)};
  check("order of slots does not matter", schedule_compute(unsorted, at(10), o) && o[0] == 100);
  std::vector<SchedulePoint> skip = {pt(8, 0, 0, 0, 0, 0, 0), pt(10, 0, 255, 255, 100, 100, 100, false), pt(12, 0, 200, 0, 0, 0, 0)};
  check("disabled slot ignored", schedule_compute(skip, at(10), o) && o[0] == 100);

  check("duplicate minute does not divide by zero", schedule_compute({pt(8, 0, 5, 5, 5, 5, 5), pt(8, 0, 9, 9, 9, 9, 9)}, at(9), o));
  check("clamp UI3-5 to 100", clamp_channel(2, 250) == 100 && clamp_channel(4, 101) == 100 && clamp_channel(0, 250) == 250);
  check("clamp negative", clamp_channel(0, -5) == 0);
  check("second of day wraps", schedule_compute(day, SECONDS_PER_DAY + at(10), o) && o[0] == 100);

  // Floor division between the points 09:00 (51, 12) and 11:00 (102, 51).
  std::vector<SchedulePoint> captured = {pt(9, 0, 51, 12, 0, 0, 0), pt(11, 0, 102, 51, 0, 0, 0)};
  check("floor division 10:40:39", schedule_compute(captured, at(10, 40, 39), o) && o[0] == 93 && o[1] == 44);
  check("floor division 10:41:14", schedule_compute(captured, at(10, 41, 14), o) && o[0] == 94 && o[1] == 44);
  // Decreasing segment: floor of the true value (never above it).
  std::vector<SchedulePoint> down = {pt(0, 0, 10, 0, 0, 0, 0), pt(0, 3, 0, 0, 0, 0, 0)};
  check("decreasing segment floors", schedule_compute(down, 60, o) && o[0] == 6);

  uint8_t x[5] = {104, 52, 0, 0, 0}, y[5] = {104, 52, 0, 0, 0}, z[5] = {105, 52, 0, 0, 0}, w[5] = {106, 52, 0, 0, 0};
  check("channels_close equal", channels_close(x, y, 0));
  check("channels_close within one", channels_close(x, z, 1) && !channels_close(x, z, 0));
  check("channels_close too far", !channels_close(x, w, 1));

  // slot text
  SchedulePoint sp;
  check("parse basic", parse_slot("08:00 2 0 0 0 0", sp) && sp.enabled && sp.minute == 480 && sp.channels[0] == 2);
  check("parse commas and short hour", parse_slot("9:05,51,12,0,0,0", sp) && sp.minute == 545 && sp.channels[1] == 12);
  check("parse seconds ignored", parse_slot("13:00:30 153 89 0 0 0", sp) && sp.minute == 780);
  check("parse off", parse_slot("off", sp) && !sp.enabled && parse_slot("", sp) && !sp.enabled && parse_slot("  OFF ", sp) && !sp.enabled);
  check("reject too few values", !parse_slot("08:00 1 2 3", sp) && !sp.enabled);
  check("reject too many values", !parse_slot("08:00 1 2 3 4 5 6", sp));
  check("reject bad time", !parse_slot("24:00 1 2 3 4 5", sp) && !parse_slot("08:60 1 2 3 4 5", sp) && !parse_slot("0800 1 2 3 4 5", sp) && !parse_slot("ab:cd 1 2 3 4 5", sp));
  check("reject UI3 over 100", !parse_slot("08:00 0 0 101 0 0", sp));
  check("accept UI1 255", parse_slot("08:00 255 255 100 100 100", sp) && sp.channels[0] == 255);
  check("reject UI1 256", !parse_slot("08:00 256 0 0 0 0", sp));
  check("reject negative and junk", !parse_slot("08:00 -1 0 0 0 0", sp) && !parse_slot("08:00 1x 0 0 0 0", sp));
  check("reject off with trailing junk", !parse_slot("off 5", sp));
  check("format", format_slot(pt(8, 0, 2, 0, 0, 0, 0)) == "08:00 2 0 0 0 0" && format_slot(pt(8, 0, 2, 0, 0, 0, 0, false)) == "off");
  check("format round trip", parse_slot(format_slot(pt(19, 5, 51, 25, 0, 0, 0)), sp) && sp.minute == 1145 && sp.channels[1] == 25);
  check("labeled sun", parse_slot("13:00 int=153 clr=89 r=25 am=10", sp, &LABELS_TUNA_SUN) && sp.enabled && sp.minute == 780 &&
        sp.channels[0] == 153 && sp.channels[1] == 89 && sp.channels[2] == 0 && sp.channels[3] == 25 && sp.channels[4] == 10);
  check("labeled partial and case", parse_slot("21:00 R=4", sp, &LABELS_TUNA_SUN) && sp.channels[3] == 4 && sp.channels[0] == 0);
  check("labeled rejects unused UI3 on sun", !parse_slot("21:00 vio=4", sp, &LABELS_TUNA_SUN));
  check("labeled blue", parse_slot("21:00 vio=4 grn=5", sp, &LABELS_TUNA_BLUE) && sp.channels[2] == 4 && sp.channels[4] == 5);
  check("labeled rejects duplicate, range, junk", !parse_slot("21:00 r=1 r=2", sp, &LABELS_TUNA_SUN) &&
        !parse_slot("21:00 r=101", sp, &LABELS_TUNA_SUN) && !parse_slot("21:00 r=x", sp, &LABELS_TUNA_SUN) &&
        !parse_slot("21:00 r=1", sp));
  check("legacy still parses with labels", parse_slot("08:00 2 0 0 0 0", sp, &LABELS_TUNA_SUN) && sp.channels[0] == 2);
  check("format labeled", format_slot(pt(13, 0, 153, 89, 0, 25, 10), &LABELS_TUNA_SUN) == "13:00 int=153 clr=89 r=25 am=10");
  check("format labeled round trip", parse_slot(format_slot(pt(13, 0, 153, 89, 0, 25, 10), &LABELS_TUNA_SUN), sp, &LABELS_TUNA_SUN) &&
        sp.channels[3] == 25 && sp.channels[4] == 10);
  check("format blue fits", format_slot(pt(13, 0, 255, 255, 100, 100, 100), &LABELS_TUNA_BLUE).size() < 64);

  check("refugium labels", LABELS_REFUGIUM.name[2] == nullptr && parse_slot("09:00 int=102 clr=240 w=10 grn=5", sp, &LABELS_REFUGIUM, CHANNEL_MAX_REFUGIUM) &&
        sp.channels[1] == 240 && sp.channels[3] == 10 && sp.channels[4] == 5);
  check("refugium clr limit is 240", !parse_slot("09:00 int=102 clr=241", sp, &LABELS_REFUGIUM, CHANNEL_MAX_REFUGIUM) &&
        !parse_slot("09:00 1 241 0 0 0", sp, &LABELS_REFUGIUM, CHANNEL_MAX_REFUGIUM) && parse_slot("09:00 1 241 0 0 0", sp));
  check("refugium has no channel 3 label", !parse_slot("09:00 vio=5", sp, &LABELS_REFUGIUM, CHANNEL_MAX_REFUGIUM));

  std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures != 0;
}
