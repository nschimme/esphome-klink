#pragma once
// Known lamps (KID -> GID/type), assignment safety rules, and a versioned, checksummed blob for
// flash. No ESPHome dependencies (host tested).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kessil {
namespace klink {

static const size_t MAX_LAMPS = 32;  // K-Link chains are documented up to 32 lights
static const uint8_t MIN_GID = 1;
static const uint8_t MAX_GID = 40;
static const uint8_t CUSTOM_GID_MIN = 3;  // 1, 2 and 26 are the per-type defaults
static const uint8_t CUSTOM_GID_MAX = 25;
static const uint8_t BLOB_VERSION = 1;

struct LampRecord {
  uint32_t kid;
  uint8_t gid;
  uint8_t type;  // 1, 2 or 3
};

// Default group for a lamp type (type 1 -> 1, type 2 -> 2, type 3 -> 26); 0 for an unknown type.
inline uint8_t default_gid(uint8_t type) { return type == 1 ? 1 : type == 2 ? 2 : type == 3 ? 26 : 0; }
inline bool is_default_gid(uint8_t gid) { return gid == 1 || gid == 2 || gid == 26; }

class Inventory {
 public:
  const std::vector<LampRecord> &lamps() const { return lamps_; }

  const LampRecord *find(uint32_t kid) const {
    for (size_t i = 0; i < lamps_.size(); i++)
      if (lamps_[i].kid == kid)
        return &lamps_[i];
    return nullptr;
  }

  // Insert or update. False when the table is full or the type is not 1-3.
  bool upsert(uint32_t kid, uint8_t gid, uint8_t type) {
    if (type < 1 || type > 3)
      return false;
    for (size_t i = 0; i < lamps_.size(); i++)
      if (lamps_[i].kid == kid) {
        lamps_[i].gid = gid;
        lamps_[i].type = type;
        return true;
      }
    if (lamps_.size() >= MAX_LAMPS)
      return false;
    lamps_.push_back(LampRecord{kid, gid, type});
    return true;
  }

  bool remove(uint32_t kid) {
    for (size_t i = 0; i < lamps_.size(); i++)
      if (lamps_[i].kid == kid) {
        lamps_.erase(lamps_.begin() + i);
        return true;
      }
    return false;
  }

  // Why `kid` must not be moved to `gid`, or an empty string when the move is allowed.
  std::string check_assignment(uint32_t kid, uint8_t gid) const {
    const LampRecord *lamp = find(kid);
    if (lamp == nullptr)
      return "unknown KID: scan first";
    if (gid < MIN_GID || gid > MAX_GID)
      return "GID must be 1-40";
    if (is_default_gid(gid) && gid != default_gid(lamp->type))
      return "that is the default group of a different lamp type";
    if (!is_default_gid(gid) && (gid < CUSTOM_GID_MIN || gid > CUSTOM_GID_MAX))
      return "custom groups use GIDs 3-25";
    for (size_t i = 0; i < lamps_.size(); i++)
      if (lamps_[i].gid == gid && lamps_[i].kid != kid && lamps_[i].type != lamp->type)
        return "group already holds a different lamp type";
    return "";
  }

  // Blob: version, count, count x (kid BE32, gid, type), CRC-16/CCITT-FALSE over the preceding bytes.
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> b;
    b.push_back(BLOB_VERSION);
    b.push_back(static_cast<uint8_t>(lamps_.size()));
    for (size_t i = 0; i < lamps_.size(); i++) {
      b.push_back(static_cast<uint8_t>(lamps_[i].kid >> 24));
      b.push_back(static_cast<uint8_t>(lamps_[i].kid >> 16));
      b.push_back(static_cast<uint8_t>(lamps_[i].kid >> 8));
      b.push_back(static_cast<uint8_t>(lamps_[i].kid));
      b.push_back(lamps_[i].gid);
      b.push_back(lamps_[i].type);
    }
    uint16_t crc = crc16(b);
    b.push_back(static_cast<uint8_t>(crc >> 8));
    b.push_back(static_cast<uint8_t>(crc));
    return b;
  }

  // Replace the contents from a blob. Leaves the inventory unchanged and returns false on any
  // problem (wrong version, bad length, bad checksum, invalid record).
  bool deserialize(const std::vector<uint8_t> &b) {
    if (b.size() < 4 || b[0] != BLOB_VERSION || b[1] > MAX_LAMPS || b.size() != 4u + 6u * b[1])
      return false;
    std::vector<uint8_t> body(b.begin(), b.end() - 2);
    if (crc16(body) != ((static_cast<uint16_t>(b[b.size() - 2]) << 8) | b[b.size() - 1]))
      return false;
    std::vector<LampRecord> parsed;
    for (size_t i = 0; i < b[1]; i++) {
      const uint8_t *p = &b[2 + 6 * i];
      LampRecord r = {(static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                          (static_cast<uint32_t>(p[2]) << 8) | p[3],
                      p[4], p[5]};
      if (r.type < 1 || r.type > 3)
        return false;
      parsed.push_back(r);
    }
    lamps_ = parsed;
    return true;
  }

  static uint16_t crc16(const std::vector<uint8_t> &data) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < data.size(); i++) {
      crc ^= static_cast<uint16_t>(data[i]) << 8;
      for (int bit = 0; bit < 8; bit++)
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
  }

 private:
  std::vector<LampRecord> lamps_;
};

}  // namespace klink
}  // namespace kessil
