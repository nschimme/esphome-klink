#pragma once
// Group operating mode: the schedule (program), manual control, off, and the
// transient preview used while editing program points. No ESPHome dependencies
// (host tested).
#include <cstdint>

namespace kessil {
namespace klink {

enum class Mode : uint8_t {
  PROGRAM,
  MANUAL,
  OFF,
  PREVIEW,
};

struct ModeState {
  Mode mode = Mode::PROGRAM;
  Mode before_preview = Mode::PROGRAM;  // what PREVIEW restores to
  uint32_t preview_deadline_ms = 0;
};

// A user command (the light, a channel number, an effect frame) takes manual
// control. While previewing it keeps the preview running but makes MANUAL the
// mode restored afterwards.
inline void on_user_command(ModeState &s) {
  switch (s.mode) {
    case Mode::PROGRAM:
    case Mode::OFF:
      s.mode = Mode::MANUAL;
      break;
    case Mode::MANUAL:
      break;
    case Mode::PREVIEW:
      s.before_preview = Mode::MANUAL;
      break;
  }
}

// Request a user-selectable mode (PROGRAM, MANUAL or OFF). PREVIEW is not
// user-selectable and is ignored. A request made while previewing ends the
// preview and is remembered as the restored mode.
inline void set_mode(ModeState &s, Mode requested) {
  if (requested == Mode::PREVIEW)
    return;
  if (s.mode == Mode::PREVIEW) {
    s.before_preview = requested;
    s.mode = requested;
    return;
  }
  s.mode = requested;
}

inline void begin_preview(ModeState &s, uint32_t now_ms, uint32_t idle_ms) {
  if (s.mode == Mode::PREVIEW)
    return;
  s.before_preview = s.mode;
  s.mode = Mode::PREVIEW;
  s.preview_deadline_ms = now_ms + idle_ms;
}

inline void refresh_preview(ModeState &s, uint32_t now_ms, uint32_t idle_ms) {
  if (s.mode != Mode::PREVIEW)
    return;
  s.preview_deadline_ms = now_ms + idle_ms;
}

inline void end_preview(ModeState &s) {
  if (s.mode != Mode::PREVIEW)
    return;
  s.mode = s.before_preview;
}

// True once a running preview has reached its deadline. The signed comparison
// is safe across the uint32_t millisecond clock wrapping around.
inline bool preview_expired(const ModeState &s, uint32_t now_ms) {
  return s.mode == Mode::PREVIEW && static_cast<int32_t>(now_ms - s.preview_deadline_ms) >= 0;
}

inline const char *mode_name(Mode m) {
  switch (m) {
    case Mode::PROGRAM:
      return "Program";
    case Mode::MANUAL:
      return "Manual";
    case Mode::OFF:
      return "Off";
    case Mode::PREVIEW:
      return "Preview";
  }
  return "";
}

inline bool mode_name_equals(const char *name, const char *expected) {
  while (*expected != '\0') {
    char c = *name;
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + ('a' - 'A'));
    if (c != *expected)
      return false;
    name++;
    expected++;
  }
  return *name == '\0';
}

// Accepts only the user-selectable names, case-insensitively. PREVIEW is not
// accepted here: callers show it through mode_name(), not as an input.
inline bool mode_from_name(const char *name, Mode &out) {
  if (name == nullptr)
    return false;
  if (mode_name_equals(name, "program")) {
    out = Mode::PROGRAM;
    return true;
  }
  if (mode_name_equals(name, "manual")) {
    out = Mode::MANUAL;
    return true;
  }
  if (mode_name_equals(name, "off")) {
    out = Mode::OFF;
    return true;
  }
  return false;
}

}  // namespace klink
}  // namespace kessil
