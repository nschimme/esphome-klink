#pragma once
// A follower's lamps, answering the leader's discovery and query frames as if they were Kessil
// lamps. Everything they send is a reply to a request addressed to them; they never start a frame.
// Pure logic, no ESPHome dependencies (host tested).
//
// The replies are the hardware-observed ones (see ../../docs/leader-follower.md and the protocol
// notes): a lamp answers only while unmuted, and `08`/`20` carry no KID, so the leader unmutes
// exactly one lamp before asking them. A real fixture answers `08` with the 9-byte major.minor
// form; ours adds the patch byte.
#include <cstdint>
#include <string>
#include <vector>

#include "klink_frame.h"
#include "klink_inventory.h"

namespace kessil {
namespace klink {

// A `34` probe this recent makes a following `17` flash the lamp. Unmeasured on a real lamp; the
// leader sends the probe about 300 ms before the find.
static const uint32_t PROBE_WINDOW_MS = 1000;

// How long the local light dips when the leader identifies this lamp.
static const uint32_t FIND_DIP_MS = 500;

// The `35` status reply: seven payload bytes whose meaning is unknown; copied verbatim from the
// hardware observation.
static const uint8_t LAMP_STATUS[7] = {0x01, 0x01, 0x01, 0x02, 0x03, 0x05, 0x03};

// ---- Reply frames: shared by a single lamp and a set of them ---------------------------------

inline std::vector<uint8_t> lamp_branch_reply(uint32_t kid) { return build(CMD_BRANCH, be32(kid)); }
inline std::vector<uint8_t> lamp_mute_reply() { return build(CMD_MUTE, {0xFF}); }
inline std::vector<uint8_t> lamp_identity_reply(uint8_t gid, uint8_t type) {
  return build(CMD_GET_GID_TYPE, {gid, static_cast<uint8_t>(type - 1)});
}
inline std::vector<uint8_t> lamp_firmware_reply(uint8_t major, uint8_t minor, uint8_t patch) {
  return build(CMD_FIRMWARE_VERSION, {major, minor, patch});
}
inline std::vector<uint8_t> lamp_model_reply(const std::string &model) {
  std::vector<uint8_t> payload(model.begin(), model.end());
  payload.push_back(0);
  return build(CMD_MODEL, payload);
}
inline std::vector<uint8_t> lamp_status_reply() {
  return build(CMD_STATUS, std::vector<uint8_t>(LAMP_STATUS, LAMP_STATUS + sizeof(LAMP_STATUS)));
}

inline uint32_t lamp_frame_u32(const std::vector<uint8_t> &frame, size_t at) {
  return (static_cast<uint32_t>(frame[at]) << 24) | (static_cast<uint32_t>(frame[at + 1]) << 16) |
         (static_cast<uint32_t>(frame[at + 2]) << 8) | frame[at + 3];
}

// The KID in a request that carries one at offset 5 (branch queries carry two at 5 and 9).
inline uint32_t lamp_frame_kid(const std::vector<uint8_t> &frame) { return lamp_frame_u32(frame, 5); }

inline bool lamp_kid_matches(uint32_t lamp_kid, uint32_t target) {
  return target == ALL_KIDS || target == lamp_kid;
}

class LampResponder {
 public:
  void set_kid(uint32_t kid) { kid_ = kid; }
  uint32_t kid() const { return kid_; }
  void set_gid(uint8_t gid) { gid_ = gid; }
  uint8_t gid() const { return gid_; }
  // Lamp type 1-3 (1 Tuna Blue, 2 Tuna Sun, 3 Refugium). The `1C` reply carries type minus one.
  void set_type(uint8_t type) { type_ = type; }
  uint8_t type() const { return type_; }
  // The `20` reply: a NUL-terminated ASCII model string.
  void set_model(const std::string &model) { model_ = model; }
  const std::string &model() const { return model_; }
  // What the `08` reply reports: our project version, not a Kessil firmware version.
  void set_version(uint8_t major, uint8_t minor, uint8_t patch) {
    version_major_ = major;
    version_minor_ = minor;
    version_patch_ = patch;
  }
  // A muted lamp is silent to every request, including the broadcasts.
  void set_muted(bool muted) { muted_ = muted; }
  bool muted() const { return muted_; }

  // Handle one request. The reply is empty when the request is not ours or the lamp is muted.
  // `gid_changed` is set when a `1B` moved the lamp; `find_requested` when a `17` should flash the
  // local light. Both are cleared first, so a null pointer leaves that output unused.
  std::vector<uint8_t> handle(const std::vector<uint8_t> &frame, uint32_t now_ms, bool *gid_changed = nullptr,
                              bool *find_requested = nullptr) {
    if (gid_changed != nullptr)
      *gid_changed = false;
    if (find_requested != nullptr)
      *find_requested = false;
    if (frame.size() < 7 || frame[0] != START || frame.back() != STOP)
      return {};

    switch (frame[4]) {
      case CMD_BRANCH:
        // Range query: answer with our own KID when it falls in [lo, hi).
        if (frame.size() == 15 && !muted_ && kid_ >= lamp_frame_u32(frame, 5) &&
            kid_ < lamp_frame_u32(frame, 9))
          return lamp_branch_reply(kid_);
        return {};
      case CMD_MUTE: {
        if (frame.size() != 11 || !lamp_kid_matches(kid_, lamp_frame_kid(frame)))
          return {};
        muted_ = true;
        return lamp_mute_reply();
      }
      case CMD_UNMUTE:
        // No reply: this must work while muted, which is why it is not gated on muted_.
        if (frame.size() == 11 && lamp_kid_matches(kid_, lamp_frame_kid(frame)))
          muted_ = false;
        return {};
      case CMD_GET_GID_TYPE:
        if (frame.size() == 11 && !muted_ && lamp_frame_kid(frame) == kid_)
          return lamp_identity_reply(gid_, type_);
        return {};
      case CMD_SET_GID:
        // Not gated by mute: the leader unmutes first, and nothing observed says a real fixture
        // ignores `1B` while muted (unverified either way). The caller persists the GID.
        if (frame.size() == 12 && lamp_frame_kid(frame) == kid_ && gid_allowed_for_type(type_, frame[9])) {
          gid_ = frame[9];
          if (gid_changed != nullptr)
            *gid_changed = true;
        }
        return {};
      case CMD_FIND:
        if (frame.size() == 11 && lamp_frame_kid(frame) == kid_ && have_probe_ &&
            now_ms - last_probe_ms_ <= PROBE_WINDOW_MS && find_requested != nullptr)
          *find_requested = true;
        return {};
      case CMD_CONTROLLER_PROBE:
        // The find command only works right after a probe; remember when one was heard.
        if (frame.size() == 7) {
          have_probe_ = true;
          last_probe_ms_ = now_ms;
        }
        return {};
      case CMD_FIRMWARE_VERSION:
        if (frame.size() == 7 && !muted_)
          return lamp_firmware_reply(version_major_, version_minor_, version_patch_);
        return {};
      case CMD_MODEL:
        if (frame.size() == 7 && !muted_)
          return lamp_model_reply(model_);
        return {};
      case CMD_STATUS:
        if (frame.size() == 11 && !muted_ && lamp_frame_kid(frame) == kid_)
          return lamp_status_reply();
        return {};
      default:
        return {};
    }
  }

 private:
  // 0 means "not assigned"; the hub derives a stable one from the MAC and entry id, very unlikely
  // to collide (`kid:` pins one).
  uint32_t kid_{0};
  uint8_t gid_{0};
  uint8_t type_{0};
  std::string model_;
  uint8_t version_major_{0};
  uint8_t version_minor_{0};
  uint8_t version_patch_{0};
  bool muted_{false};  // unmuted at boot: a leader scan expects every lamp to answer
  bool have_probe_{false};
  uint32_t last_probe_ms_{0};
};

// What a set made of one request. The indexes point into the set; -1 means the request did not
// change that lamp.
struct LampResult {
  std::vector<uint8_t> reply;
  int gid_changed = -1;
  int find = -1;
};

// The lamps behind one UART. A branch query is answered by the lowest unmuted KID in range, one
// lamp only: several answers on one wire would garble. The leader mutes each clean answerer and
// asks the same range again, so a full scan still finds every lamp. The version and the probe time
// are shared, as they are on one device.
class LampSet {
 public:
  void add(const LampResponder &lamp) { lamps_.push_back(lamp); }
  void set_version(uint8_t major, uint8_t minor, uint8_t patch) {
    version_major_ = major;
    version_minor_ = minor;
    version_patch_ = patch;
  }
  size_t size() const { return lamps_.size(); }
  bool empty() const { return lamps_.empty(); }
  const std::vector<LampResponder> &lamps() const { return lamps_; }
  std::vector<LampResponder> &lamps() { return lamps_; }

  // Handle one request for every lamp in the set. Nothing is replied when no lamp answers.
  LampResult handle(const std::vector<uint8_t> &frame, uint32_t now_ms) {
    LampResult result;
    if (lamps_.empty() || frame.size() < 7 || frame[0] != START || frame.back() != STOP)
      return result;

    switch (frame[4]) {
      case CMD_BRANCH: {
        if (frame.size() != 15)
          return result;
        const uint32_t lo = lamp_frame_u32(frame, 5);
        const uint32_t hi = lamp_frame_u32(frame, 9);
        const int best = lowest_unmuted_in_(lo, hi);
        if (best >= 0)
          result.reply = lamp_branch_reply(lamps_[best].kid());
        return result;
      }
      case CMD_MUTE: {
        if (frame.size() != 11)
          return result;
        const uint32_t target = lamp_frame_kid(frame);
        bool matched = false;
        for (size_t i = 0; i < lamps_.size(); i++)
          if (lamp_kid_matches(lamps_[i].kid(), target)) {
            lamps_[i].set_muted(true);
            matched = true;
          }
        if (matched)
          result.reply = lamp_mute_reply();
        return result;
      }
      case CMD_UNMUTE:
        // No reply, and it must work while muted.
        if (frame.size() == 11) {
          const uint32_t target = lamp_frame_kid(frame);
          for (size_t i = 0; i < lamps_.size(); i++)
            if (lamp_kid_matches(lamps_[i].kid(), target))
              lamps_[i].set_muted(false);
        }
        return result;
      case CMD_GET_GID_TYPE:
        if (frame.size() == 11) {
          const int i = unmuted_index_(lamp_frame_kid(frame));
          if (i >= 0)
            result.reply = lamp_identity_reply(lamps_[i].gid(), lamps_[i].type());
        }
        return result;
      case CMD_SET_GID:
        // Not gated by mute: the leader unmutes first, and nothing observed says a real fixture
        // ignores `1B` while muted (unverified either way). The caller persists the GID.
        if (frame.size() == 12) {
          const int i = index_(lamp_frame_kid(frame));
          if (i >= 0 && gid_allowed_for_type(lamps_[i].type(), frame[9])) {
            lamps_[i].set_gid(frame[9]);
            result.gid_changed = i;
          }
        }
        return result;
      case CMD_FIND:
        if (frame.size() == 11 && have_probe_ && now_ms - last_probe_ms_ <= PROBE_WINDOW_MS)
          result.find = index_(lamp_frame_kid(frame));
        return result;
      case CMD_CONTROLLER_PROBE:
        // The find command only works right after a probe; remember when one was heard.
        if (frame.size() == 7) {
          have_probe_ = true;
          last_probe_ms_ = now_ms;
        }
        return result;
      case CMD_FIRMWARE_VERSION:
        if (frame.size() == 7 && lowest_unmuted_() >= 0)
          result.reply = lamp_firmware_reply(version_major_, version_minor_, version_patch_);
        return result;
      case CMD_MODEL: {
        if (frame.size() != 7)
          return result;
        // The leader unmutes exactly one lamp before asking; the lowest KID is that one.
        const int i = lowest_unmuted_();
        if (i >= 0)
          result.reply = lamp_model_reply(lamps_[i].model());
        return result;
      }
      case CMD_STATUS:
        if (frame.size() == 11) {
          const int i = unmuted_index_(lamp_frame_kid(frame));
          if (i >= 0)
            result.reply = lamp_status_reply();
        }
        return result;
      default:
        return result;
    }
  }

 private:
  int index_(uint32_t kid) const {
    for (size_t i = 0; i < lamps_.size(); i++)
      if (lamps_[i].kid() == kid)
        return static_cast<int>(i);
    return -1;
  }

  int unmuted_index_(uint32_t kid) const {
    for (size_t i = 0; i < lamps_.size(); i++)
      if (!lamps_[i].muted() && lamps_[i].kid() == kid)
        return static_cast<int>(i);
    return -1;
  }

  // The lowest-KID unmuted lamp in [lo, hi), or -1 when there is none.
  int lowest_unmuted_in_(uint32_t lo, uint32_t hi) const {
    int best = -1;
    for (size_t i = 0; i < lamps_.size(); i++)
      if (!lamps_[i].muted() && lamps_[i].kid() >= lo && lamps_[i].kid() < hi &&
          (best < 0 || lamps_[i].kid() < lamps_[best].kid()))
        best = static_cast<int>(i);
    return best;
  }

  // The lowest-KID unmuted lamp of the whole set, or -1 when every lamp is muted.
  int lowest_unmuted_() const {
    int best = -1;
    for (size_t i = 0; i < lamps_.size(); i++)
      if (!lamps_[i].muted() && (best < 0 || lamps_[i].kid() < lamps_[best].kid()))
        best = static_cast<int>(i);
    return best;
  }

  std::vector<LampResponder> lamps_;
  uint8_t version_major_{0};
  uint8_t version_minor_{0};
  uint8_t version_patch_{0};
  bool have_probe_{false};
  uint32_t last_probe_ms_{0};
};

}  // namespace klink
}  // namespace kessil
