#pragma once
// The project version a follower reports in its `08` reply, parsed into the three bytes the frame
// carries. Pure logic, host tested.
#include <cstdint>

namespace kessil {
namespace klink {

// Parse a version such as "0.2.0", "v0.2.0", "1.2" or "1" into major, minor and patch. A non-numeric
// prefix (the "v" ESPHome's own example uses) is skipped, a non-numeric suffix is ignored, and each
// field is capped at 255. Missing fields are 0.
inline void parse_version_string(const char *text, uint8_t &major, uint8_t &minor, uint8_t &patch) {
  major = 0;
  minor = 0;
  patch = 0;
  if (text == nullptr)
    return;
  const char *s = text;
  while (*s != '\0' && (*s < '0' || *s > '9'))
    s++;
  uint8_t *fields[3] = {&major, &minor, &patch};
  for (int i = 0; i < 3 && *s != '\0'; i++) {
    uint32_t value = 0;
    while (*s >= '0' && *s <= '9') {
      if (value <= 1000)
        value = value * 10 + static_cast<uint32_t>(*s - '0');
      s++;
    }
    *fields[i] = static_cast<uint8_t>(value > 255 ? 255 : value);
    if (*s != '.')
      return;
    s++;
  }
}

}  // namespace klink
}  // namespace kessil
