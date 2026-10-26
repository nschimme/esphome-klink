#pragma once
// Small HTTP helpers for the K-Link web API. No ESPHome dependencies (host tested).
#include <cstdint>
#include <cstdio>
#include <string>

namespace esphome {
namespace klink {

// Strict unsigned decimal parse: only '0'-'9', no sign, no surrounding whitespace, and the result
// must not exceed `max`. Empty input is rejected. Used for query parameters so a typo cannot be
// silently read as another value.
inline bool parse_uint(const std::string &text, uint32_t max, uint32_t &out) {
  if (text.empty())
    return false;
  uint32_t value = 0;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (c < '0' || c > '9')
      return false;
    uint32_t digit = static_cast<uint32_t>(c - '0');
    if (digit > max)
      return false;
    if (value > (max - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  out = value;
  return true;
}

// Escape a string for embedding inside a JSON double-quoted string. UTF-8 bytes pass through
// unchanged; quotes, backslashes and control characters are escaped.
inline std::string json_escape(const std::string &in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (size_t i = 0; i < in.size(); i++) {
    char c = in[i];
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x",
                        static_cast<unsigned int>(static_cast<unsigned char>(c)));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace klink
}  // namespace esphome
