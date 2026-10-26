#pragma once
// Lamp nicknames: a tiny fixed-capacity table serialized as a versioned, checksummed blob for
// flash. No ESPHome dependencies (host tested).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "klink_inventory.h"

namespace kessil {
namespace klink {

static const size_t MAX_NAMES = 32;     // at most one nickname per known lamp
static const size_t NAME_MAX_LEN = 24;  // UTF-8 bytes, not including the terminator
static const uint8_t NAME_BLOB_VERSION = 1;

// Blob: version, count, count x (kid BE32, name length byte, name bytes), CRC-16 BE. This is the
// largest blob the format can produce; persistence buffers can size themselves from it.
static const size_t NAME_BLOB_MAX = 2 + MAX_NAMES * (4 + 1 + NAME_MAX_LEN) + 2;

class NameTable {
 public:
  // Returns nullptr when `kid` has no name; otherwise a NUL-terminated name.
  const char *get(uint32_t kid) const {
    for (size_t i = 0; i < names_.size(); i++)
      if (names_[i].kid == kid)
        return names_[i].name.c_str();
    return nullptr;
  }

  bool contains(uint32_t kid) const { return get(kid) != nullptr; }
  size_t size() const { return names_.size(); }

  // An empty name removes the entry. Names are trimmed to at most NAME_MAX_LEN bytes and must be
  // valid UTF-8. Returns false (and leaves the table unchanged) when the name is invalid or there
  // is no room for a new entry.
  bool set(uint32_t kid, const std::string &name) {
    if (name.size() > NAME_MAX_LEN)
      return false;
    if (!utf8_valid(name))
      return false;
    for (size_t i = 0; i < names_.size(); i++) {
      if (names_[i].kid == kid) {
        if (name.empty()) {
          names_.erase(names_.begin() + i);
        } else {
          names_[i].name = name;
        }
        return true;
      }
    }
    if (name.empty())
      return true;  // removing a name that isn't there is fine
    if (names_.size() >= MAX_NAMES)
      return false;
    names_.push_back(Entry{kid, name});
    return true;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    b.push_back(NAME_BLOB_VERSION);
    b.push_back(static_cast<uint8_t>(names_.size()));
    for (size_t i = 0; i < names_.size(); i++) {
      const Entry &e = names_[i];
      b.push_back(static_cast<uint8_t>(e.kid >> 24));
      b.push_back(static_cast<uint8_t>(e.kid >> 16));
      b.push_back(static_cast<uint8_t>(e.kid >> 8));
      b.push_back(static_cast<uint8_t>(e.kid));
      b.push_back(static_cast<uint8_t>(e.name.size()));
      for (size_t j = 0; j < e.name.size(); j++)
        b.push_back(static_cast<uint8_t>(e.name[j]));
    }
    uint16_t crc = Inventory::crc16(b);
    b.push_back(static_cast<uint8_t>(crc >> 8));
    b.push_back(static_cast<uint8_t>(crc));
    return b;
  }

  // Replace the contents from a blob. On any problem (wrong version, bad length, bad checksum,
  // invalid name) the table is left unchanged and false is returned.
  bool deserialize(const std::vector<uint8_t> &b) {
    if (b.size() < 4 || b[0] != NAME_BLOB_VERSION || b[1] > MAX_NAMES)
      return false;
    size_t pos = 2;
    std::vector<Entry> parsed;
    for (size_t i = 0; i < b[1]; i++) {
      if (pos + 5 > b.size())
        return false;
      uint32_t kid = (static_cast<uint32_t>(b[pos]) << 24) | (static_cast<uint32_t>(b[pos + 1]) << 16) |
                     (static_cast<uint32_t>(b[pos + 2]) << 8) | b[pos + 3];
      size_t len = b[pos + 4];
      pos += 5;
      if (len > NAME_MAX_LEN || pos + len > b.size())
        return false;
      std::string name(reinterpret_cast<const char *>(&b[pos]), len);
      pos += len;
      if (!utf8_valid(name))
        return false;
      parsed.push_back(Entry{kid, name});
    }
    if (pos + 2 != b.size())
      return false;
    std::vector<uint8_t> body(b.begin(), b.end() - 2);
    if (Inventory::crc16(body) != ((static_cast<uint16_t>(b[b.size() - 2]) << 8) | b[b.size() - 1]))
      return false;
    names_ = parsed;
    return true;
  }

  // Minimal UTF-8 well-formedness check: rejects overlong encodings, surrogates and out-of-range
  // code points.
  static bool utf8_valid(const std::string &s) {
    size_t i = 0;
    while (i < s.size()) {
      uint8_t c = static_cast<uint8_t>(s[i]);
      if (c < 0x80) {
        i++;
        continue;
      }
      size_t n;
      uint32_t cp;
      if ((c & 0xE0) == 0xC0) {
        n = 2;
        cp = c & 0x1F;
      } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        cp = c & 0x0F;
      } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        cp = c & 0x07;
      } else {
        return false;
      }
      if (i + n > s.size())
        return false;
      for (size_t j = 1; j < n; j++) {
        uint8_t cc = static_cast<uint8_t>(s[i + j]);
        if ((cc & 0xC0) != 0x80)
          return false;
        cp = (cp << 6) | (cc & 0x3F);
      }
      if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000))
        return false;
      if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return false;
      i += n;
    }
    return true;
  }

 private:
  struct Entry {
    uint32_t kid;
    std::string name;
  };
  std::vector<Entry> names_;
};

}  // namespace klink
}  // namespace kessil
