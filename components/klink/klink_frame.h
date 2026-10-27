#pragma once
// K-Link frame building and reply parsing. No ESPHome dependencies, so it can be
// unit tested on a host (see ../../test_klink_frame.cpp).
//
// Frame: A9 | len | 00 00 | cmd | payload... | checksum | 5C
// len = payload size + 4; checksum = two's complement of the sum of len..payload.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace kessil {
namespace klink {

enum Command : uint8_t {
  CMD_FIRMWARE_VERSION = 0x08,
  CMD_MODEL = 0x20,
  CMD_STATUS = 0x35,
  CMD_FIND = 0x17,
  CMD_BRANCH = 0x18,
  CMD_MUTE = 0x19,
  CMD_UNMUTE = 0x1A,
  CMD_SET_GID = 0x1B,
  CMD_GET_GID_TYPE = 0x1C,
  CMD_SET_GROUP_CHANNELS = 0x1F,
  CMD_CONTROLLER_PROBE = 0x34,  // periodic "is another controller here?" probe
};

static const uint8_t START = 0xA9;
static const uint8_t STOP = 0x5C;
static const uint32_t ALL_KIDS = 0xFFFFFFFF;

inline std::vector<uint8_t> build(uint8_t command, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> f;
  f.push_back(START);
  f.push_back(static_cast<uint8_t>(payload.size() + 4));
  f.push_back(0x00);
  f.push_back(0x00);
  f.push_back(command);
  f.insert(f.end(), payload.begin(), payload.end());
  uint8_t sum = 0;
  for (size_t i = 1; i < f.size(); i++)
    sum = static_cast<uint8_t>(sum + f[i]);
  f.push_back(static_cast<uint8_t>(-sum));
  f.push_back(STOP);
  return f;
}

inline std::vector<uint8_t> be32(uint32_t v) {
  return {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
          static_cast<uint8_t>(v)};
}

inline std::vector<uint8_t> mute(uint32_t kid) { return build(CMD_MUTE, be32(kid)); }
inline std::vector<uint8_t> unmute(uint32_t kid) { return build(CMD_UNMUTE, be32(kid)); }
inline std::vector<uint8_t> identity(uint32_t kid) { return build(CMD_GET_GID_TYPE, be32(kid)); }
inline std::vector<uint8_t> find(uint32_t kid) { return build(CMD_FIND, be32(kid)); }
inline std::vector<uint8_t> branch(uint32_t lo, uint32_t hi_exclusive) {
  std::vector<uint8_t> p = be32(lo);
  std::vector<uint8_t> h = be32(hi_exclusive);
  p.insert(p.end(), h.begin(), h.end());
  return build(CMD_BRANCH, p);
}
inline std::vector<uint8_t> set_gid(uint32_t kid, uint8_t gid) {
  std::vector<uint8_t> p = be32(kid);
  p.push_back(gid);
  return build(CMD_SET_GID, p);
}
// Five raw channel bytes to every fixture in `gid`; the command has no readback.
inline std::vector<uint8_t> group_channels(uint8_t gid, const uint8_t ch[5]) {
  std::vector<uint8_t> p(ch, ch + 5);
  p.push_back(gid);
  p.push_back(0x00);
  return build(CMD_SET_GROUP_CHANNELS, p);
}

// Neither carries a KID: they are answered by the one unmuted lamp.
inline std::vector<uint8_t> model_query() { return build(CMD_MODEL, std::vector<uint8_t>()); }
inline std::vector<uint8_t> firmware_query() { return build(CMD_FIRMWARE_VERSION, std::vector<uint8_t>()); }

inline std::vector<uint8_t> controller_probe() { return build(CMD_CONTROLLER_PROBE, std::vector<uint8_t>()); }

// True when `r` is a complete frame for `command`: marker, length, command, checksum, stop.
// Bytes 2-3 are not constrained (they are not checked).
inline bool valid_reply(const std::vector<uint8_t> &r, uint8_t command) {
  if (r.size() < 7 || r.front() != START || r.back() != STOP || r[1] != r.size() - 3 || r[4] != command)
    return false;
  uint8_t sum = 0;
  for (size_t i = 1; i + 1 < r.size(); i++)
    sum = static_cast<uint8_t>(sum + r[i]);
  return sum == 0;
}

struct Identity {
  uint8_t gid;
  uint8_t type;  // reply byte 6 plus one
};

inline bool parse_identity(const std::vector<uint8_t> &r, Identity &out) {
  if (!valid_reply(r, CMD_GET_GID_TYPE) || r.size() < 9)
    return false;
  out.gid = r[5];
  out.type = static_cast<uint8_t>(r[6] + 1);
  return true;
}

// Model reply: the name is the bytes between the command and the checksum, with no terminator. An empty name is
// not a reply (a bare command frame has the same bytes as the request).
inline bool parse_model(const std::vector<uint8_t> &r, std::string &out) {
  if (!valid_reply(r, CMD_MODEL) || r.size() == 7)
    return false;
  size_t n = r.size() - 7;
  out.clear();
  for (size_t i = 0; i < n; i++) {
    uint8_t c = r[5 + i];
    if (c == 0)
      break;
    if (c < 0x20 || c > 0x7E)
      return false;
    out.push_back(static_cast<char>(c));
  }
  return true;
}

// Firmware reply: major, minor and patch; a lamp that sends only major and minor reports patch 0.
struct FirmwareVersion {
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
};

inline bool parse_firmware(const std::vector<uint8_t> &r, FirmwareVersion &out) {
  if (!valid_reply(r, CMD_FIRMWARE_VERSION) || (r.size() != 9 && r.size() != 10))
    return false;
  out.major = r[5];
  out.minor = r[6];
  out.patch = r.size() == 10 ? r[7] : 0;
  return true;
}

// One-line description of a valid frame, for logs: command name plus the fields we understand.
inline std::string describe_frame(const std::vector<uint8_t> &f) {
  if (f.size() < 7)
    return "short frame";
  char buf[96];
  uint8_t cmd = f[4];
  uint32_t kid = f.size() >= 9 ? (static_cast<uint32_t>(f[5]) << 24) | (static_cast<uint32_t>(f[6]) << 16) |
                                     (static_cast<uint32_t>(f[7]) << 8) | f[8]
                               : 0;
  switch (cmd) {
    case CMD_CONTROLLER_PROBE:
      return "controller probe (34)";
    case CMD_SET_GROUP_CHANNELS:
      if (f.size() == 14) {
        std::snprintf(buf, sizeof(buf), "group write GID %u: %u %u %u %u %u", f[10], f[5], f[6], f[7], f[8], f[9]);
        return buf;
      }
      return "group write (unexpected length)";
    case CMD_MUTE:
    case CMD_UNMUTE:
    case CMD_GET_GID_TYPE:
    case CMD_FIND:
      if (f.size() == 11) {
        const char *name = cmd == CMD_MUTE ? "mute" : cmd == CMD_UNMUTE ? "unmute" : cmd == CMD_FIND ? "find" : "identity query";
        if (kid == ALL_KIDS)
          std::snprintf(buf, sizeof(buf), "%s all lamps", name);
        else
          std::snprintf(buf, sizeof(buf), "%s KID %u", name, static_cast<unsigned>(kid));
        return buf;
      }
      if (cmd == CMD_GET_GID_TYPE && f.size() == 9) {
        std::snprintf(buf, sizeof(buf), "identity reply: GID %u, type %u", f[5], f[6] + 1);
        return buf;
      }
      if (cmd == CMD_MUTE && f.size() == 8)
        return "mute reply";
      break;
    case CMD_BRANCH:
      if (f.size() == 15)
        return "branch query";
      if (f.size() == 11) {
        std::snprintf(buf, sizeof(buf), "branch reply: KID %u", static_cast<unsigned>(kid));
        return buf;
      }
      return "branch reply";
    case CMD_FIRMWARE_VERSION:
      if (f.size() == 9) {
        std::snprintf(buf, sizeof(buf), "firmware version reply: %u.%u", f[5], f[6]);
        return buf;
      }
      if (f.size() == 10) {
        std::snprintf(buf, sizeof(buf), "firmware version reply: %u.%u.%u", f[5], f[6], f[7]);
        return buf;
      }
      return "firmware version request";
    case CMD_MODEL:
      if (f.size() > 8) {
        std::string name;
        for (size_t i = 5; i + 2 < f.size() && f[i] != 0; i++)
          name.push_back(static_cast<char>(f[i]));
        return "model reply: " + name;
      }
      return "model request";
    case CMD_STATUS:
      if (f.size() == 11) {
        std::snprintf(buf, sizeof(buf), "status request KID %u", static_cast<unsigned>(kid));
        return buf;
      }
      return "status reply";
    case CMD_SET_GID:
      return "set GID";
    default:
      break;
  }
  std::snprintf(buf, sizeof(buf), "command %02X", cmd);
  return buf;
}

// True for frames that only a controller sends (requests and writes), as opposed to a fixture's
// reply. Request and reply of the same command differ in length: for example an identity query is
// 11 bytes and its reply 9, a mute is 11 and its acknowledgement 8, a branch query is 15 and its
// reply 11. Unknown commands are not classified as controller frames.
inline bool looks_like_controller_frame(const std::vector<uint8_t> &f) {
  if (f.size() < 7)
    return false;
  switch (f[4]) {
    case CMD_CONTROLLER_PROBE:
      return f.size() == 7;
    case CMD_SET_GROUP_CHANNELS:
      return f.size() == 14;
    case CMD_SET_GID:
      return f.size() == 12;
    case CMD_FIND:
    case CMD_UNMUTE:
    case CMD_MUTE:
    case CMD_GET_GID_TYPE:
      return f.size() == 11;
    case CMD_BRANCH:
      return f.size() == 15;
    case CMD_FIRMWARE_VERSION:
    case CMD_MODEL:
      return f.size() == 7;  // no payload; the reply is longer
    case CMD_STATUS:
      return f.size() == 11;  // KID; the reply is 14 bytes
    default:
      return false;
  }
}

// Incremental frame extractor for a byte stream: resynchronises on the start marker, checks the
// length, terminator and checksum, and returns each valid frame once. Bytes that do not form a
// frame are counted and dropped, so line noise can never wedge it.
class FrameParser {
 public:
  // Add one byte, then try to extract a frame. Returns true and fills `out` for each complete
  // valid frame. After a true result, call poll() until it returns false: a resynchronisation can
  // uncover further complete frames that were already buffered.
  bool feed(uint8_t b, std::vector<uint8_t> &out) {
    buf_.push_back(b);
    return poll(out);
  }

  bool poll(std::vector<uint8_t> &out) {
    for (;;) {
      size_t skip = 0;
      while (skip < buf_.size() && buf_[skip] != START)
        skip++;
      if (skip) {
        dropped_ += skip;
        buf_.erase(buf_.begin(), buf_.begin() + skip);
      }
      if (buf_.size() < 2)
        return false;
      size_t total = static_cast<size_t>(buf_[1]) + 3;
      if (total < 7 || total > MAX_FRAME) {  // implausible length: this marker was not a frame start
        drop_first_();
        continue;
      }
      if (buf_.size() < total)
        return false;
      uint8_t sum = 0;
      for (size_t i = 1; i + 1 < total; i++)
        sum = static_cast<uint8_t>(sum + buf_[i]);
      if (buf_[total - 1] == STOP && sum == 0) {
        out.assign(buf_.begin(), buf_.begin() + total);
        buf_.erase(buf_.begin(), buf_.begin() + total);
        frames_++;
        return true;
      }
      bad_++;
      drop_first_();
    }
  }

  uint32_t frames() const { return frames_; }
  uint32_t bad_frames() const { return bad_; }
  uint32_t dropped_bytes() const { return dropped_; }

  static const size_t MAX_FRAME = 64;

 private:
  void drop_first_() {
    buf_.erase(buf_.begin());
    dropped_++;
  }

  std::vector<uint8_t> buf_;
  uint32_t frames_ = 0, bad_ = 0, dropped_ = 0;
};

}  // namespace klink
}  // namespace kessil
