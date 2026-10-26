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

// Parse exactly six hex digits, either case, with an optional single leading '#'.
// Used for the colour-wheel target (`rgb=RRGGBB`); anything else is rejected.
inline bool parse_hex_rgb(const std::string &text, uint8_t out[3]) {
  size_t start = 0;
  if (!text.empty() && text[0] == '#')
    start = 1;
  if (text.size() - start != 6)
    return false;
  for (size_t i = 0; i < 3; i++) {
    uint8_t value = 0;
    for (size_t j = 0; j < 2; j++) {
      char c = text[start + i * 2 + j];
      uint8_t digit;
      if (c >= '0' && c <= '9')
        digit = static_cast<uint8_t>(c - '0');
      else if (c >= 'a' && c <= 'f')
        digit = static_cast<uint8_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        digit = static_cast<uint8_t>(c - 'A' + 10);
      else
        return false;
      value = static_cast<uint8_t>(value * 16 + digit);
    }
    out[i] = value;
  }
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
