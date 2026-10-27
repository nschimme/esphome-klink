#pragma once
// Chain ownership for a K-Link chain with more than one esphome-klink device.
//
// K-Link itself has no leader election. Kessil's controllers (and an AP9X) announce themselves with
// the payload-less command-34 frame about once a second. An AP9X that hears another controller's
// beacon yields and resumes when the beacon has been gone for a few seconds. Group channel writes
// (command 1F) are the only proof that a controller is actually driving the lamps; fixtures never
// answer a beacon.
//
// This guard follows that model:
//   * a follower never transmits at all;
//   * a leader announces itself at the beacon interval while it is claiming or controlling;
//   * it starts writing only after it has announced itself for claim_confirm and no foreign group
//     write or controller request has been heard for foreign_holdoff;
//   * a foreign group write while controlling is a real conflict and stops the writes; the guard
//     pauses, then claims again after the other controller has been quiet for conflict_release.
//
// A foreign beacon is deliberately not treated as ownership: an AP9X may keep announcing while it
// is yielding, and two esphome leaders cannot be told apart from the beacon alone. Roles are
// explicit (leader or follower); this class does not elect a leader.
//
// No ESPHome dependencies (host tested); time is injected as milliseconds.
#include <cstdint>
#include <string>

namespace kessil {
namespace klink {

enum class ChainRole : uint8_t { LEADER, FOLLOWER };
enum class ChainState : uint8_t { IDLE, CLAIMING, CONTROLLING, CONFLICT };

// How long a foreign group write keeps a leader from claiming. Kessil's controllers write about
// once a second; three seconds is three missed writes.
static const uint32_t DEFAULT_FOREIGN_HOLDOFF_MS = 3000;

class ChainGuard {
 public:
  ChainGuard(uint32_t beacon_ms = 1000, uint32_t claim_confirm_ms = 2000, uint32_t foreign_holdoff_ms = 3000,
             uint32_t conflict_release_ms = 10000)
      : beacon_ms_(beacon_ms),
        claim_confirm_ms_(claim_confirm_ms),
        foreign_holdoff_ms_(foreign_holdoff_ms),
        conflict_release_ms_(conflict_release_ms) {}

  void set_role(ChainRole role) {
    role_ = role;
    if (role_ == ChainRole::FOLLOWER)
      state_ = ChainState::IDLE;
  }
  ChainRole role() const { return role_; }
  bool is_leader() const { return role_ == ChainRole::LEADER; }
  bool is_follower() const { return role_ == ChainRole::FOLLOWER; }

  void set_beacon_ms(uint32_t ms) { beacon_ms_ = ms; }
  void set_claim_confirm_ms(uint32_t ms) { claim_confirm_ms_ = ms; }
  void set_foreign_holdoff_ms(uint32_t ms) { foreign_holdoff_ms_ = ms; }
  uint32_t foreign_holdoff_ms() const { return foreign_holdoff_ms_; }
  void set_conflict_release_ms(uint32_t ms) { conflict_release_ms_ = ms; }
  // Extra wait before claiming, spread across devices so two equal leaders do not keep colliding.
  void set_jitter_ms(uint32_t ms) { jitter_ms_ = ms; }

  // Call once at startup. The guard is idle until writes are requested.
  void start(uint32_t now) {
    started_ = true;
    state_ = ChainState::IDLE;
    state_since_ = claim_start_ = now;
    have_beacon_ = false;
    have_foreign_ = false;
    have_beacon_heard_ = false;
  }

  void set_want_writes(bool on) {
    want_ = on;
    if (!on)
      state_ = ChainState::IDLE;
  }
  bool want_writes() const { return want_; }
  void stop() {
    want_ = false;
    state_ = ChainState::IDLE;
  }

  ChainState state() const { return state_; }
  uint32_t state_since() const { return state_since_; }
  bool controlling() const { return state_ == ChainState::CONTROLLING; }
  bool conflict() const { return state_ == ChainState::CONFLICT; }
  // True while this device should put command-34 frames on the bus: a leader that wants the chain.
  bool beaconing() const {
    return started_ && role_ == ChainRole::LEADER && want_ &&
           (state_ == ChainState::CLAIMING || state_ == ChainState::CONTROLLING);
  }

  struct Update {
    bool send_beacon;
    bool controlling;
    bool state_changed;
  };

  // Advance the state machine and report whether a beacon is due. The caller enqueues the beacon;
  // this call records it so the next one is due after beacon_ms.
  Update update(uint32_t now) {
    Update out{false, false, false};
    if (!started_)
      return out;
    if (role_ == ChainRole::FOLLOWER || !want_) {
      if (state_ != ChainState::IDLE) {
        enter_(ChainState::IDLE, now);
        out.state_changed = true;
      }
      out.controlling = false;
      return out;
    }

    if (state_ == ChainState::IDLE) {
      enter_(ChainState::CLAIMING, now);
      have_beacon_ = false;
      out.state_changed = true;
    }

    if (state_ == ChainState::CLAIMING) {
      // A foreign beacon does not count here: an AP9X may keep announcing while it yields. Only a
      // real controller write holds the claim back.
      if (!foreign_recent_(now, foreign_holdoff_ms_) &&
          elapsed_(now, claim_start_) >= claim_confirm_ms_ + jitter_ms_) {
        enter_(ChainState::CONTROLLING, now);
        out.state_changed = true;
      }
    } else if (state_ == ChainState::CONFLICT) {
      if (!foreign_recent_(now, conflict_release_ms_ + jitter_ms_) &&
          elapsed_(now, state_since_) >= conflict_release_ms_ + jitter_ms_) {
        enter_(ChainState::CLAIMING, now);
        have_beacon_ = false;
        out.state_changed = true;
      }
    }

    if (state_ == ChainState::CLAIMING || state_ == ChainState::CONTROLLING) {
      if (!have_beacon_ || elapsed_(now, last_beacon_ms_) >= beacon_ms_) {
        last_beacon_ms_ = now;
        have_beacon_ = true;
        beacons_sent_++;
        out.send_beacon = true;
      }
    }
    out.controlling = state_ == ChainState::CONTROLLING;
    return out;
  }

  // A foreign command-34 frame was heard. It is counted, but it does not change ownership: the
  // receiver may be an AP9X that is yielding, and the frame carries no identity.
  void on_beacon(uint32_t now) {
    beacons_heard_++;
    last_beacon_heard_ms_ = now;
    have_beacon_heard_ = true;
  }

  // A foreign command-1F group write, or another controller request, was heard. This is the real
  // sign that somebody is driving the chain. If this device was controlling, stop and pause.
  void on_foreign_write(uint32_t now) {
    if (!started_)
      return;
    foreign_writes_++;
    last_foreign_ms_ = now;
    have_foreign_ = true;
    if (state_ == ChainState::CONTROLLING)
      enter_(ChainState::CONFLICT, now);
    else if (state_ == ChainState::CONFLICT)
      state_since_ = now;  // keep the retry countdown tied to the last foreign write
  }

  // Take control again now: drop a conflict and start claiming. The claim confirmation and the
  // foreign-write holdoff still apply.
  void retry(uint32_t now) {
    if (!started_ || role_ != ChainRole::LEADER)
      return;
    want_ = true;
    state_ = ChainState::CLAIMING;
    state_since_ = claim_start_ = now;
    have_beacon_ = false;
  }

  bool foreign_recent(uint32_t now, uint32_t ms) const { return foreign_recent_(now, ms); }
  bool beacon_heard_recent(uint32_t now, uint32_t ms) const {
    return have_beacon_heard_ && elapsed_(now, last_beacon_heard_ms_) < ms;
  }

  uint32_t beacons_heard() const { return beacons_heard_; }
  uint32_t beacons_sent() const { return beacons_sent_; }
  uint32_t foreign_writes() const { return foreign_writes_; }
  uint32_t conflicts() const { return conflicts_; }

  const char *state_name() const {
    switch (state_) {
      case ChainState::IDLE:
        return role_ == ChainRole::FOLLOWER ? "follower" : "idle";
      case ChainState::CLAIMING:
        return "claiming";
      case ChainState::CONTROLLING:
        return "controlling";
      case ChainState::CONFLICT:
        return "conflict";
    }
    return "";
  }

  // One line for the bus-state entity, given the current time.
  std::string reason(uint32_t now) const {
    if (role_ == ChainRole::FOLLOWER)
      return "follower: listening only";
    if (!want_)
      return "writes off";
    switch (state_) {
      case ChainState::IDLE:
        return "writes off";
      case ChainState::CLAIMING:
        if (foreign_recent_(now, foreign_holdoff_ms_))
          return "claiming: waiting for the other controller";
        return "claiming: announcing";
      case ChainState::CONTROLLING:
        return "controlling";
      case ChainState::CONFLICT: {
        uint32_t wait = conflict_release_ms_ + jitter_ms_;
        uint32_t gone = elapsed_(now, state_since_);
        uint32_t left = gone >= wait ? 0 : (wait - gone + 999) / 1000;
        return "conflict: another controller wrote; retrying in " + std::to_string(left) + " s";
      }
    }
    return "";
  }

 private:
  static uint32_t elapsed_(uint32_t now, uint32_t since) { return now - since; }

  bool foreign_recent_(uint32_t now, uint32_t ms) const {
    return have_foreign_ && elapsed_(now, last_foreign_ms_) < ms;
  }

  void enter_(ChainState state, uint32_t now) {
    if (state_ == state)
      return;
    state_ = state;
    state_since_ = now;
    if (state == ChainState::CLAIMING)
      claim_start_ = now;
    if (state == ChainState::CONFLICT)
      conflicts_++;
  }

  uint32_t beacon_ms_;
  uint32_t claim_confirm_ms_;
  uint32_t foreign_holdoff_ms_;
  uint32_t conflict_release_ms_;
  uint32_t jitter_ms_{0};

  ChainRole role_{ChainRole::LEADER};
  ChainState state_{ChainState::IDLE};
  uint32_t state_since_{0};
  uint32_t claim_start_{0};
  uint32_t last_beacon_ms_{0};
  uint32_t last_foreign_ms_{0};
  uint32_t last_beacon_heard_ms_{0};
  bool started_{false};
  bool want_{false};
  bool have_beacon_{false};
  bool have_foreign_{false};
  bool have_beacon_heard_{false};
  uint32_t beacons_sent_{0};
  uint32_t beacons_heard_{0};
  uint32_t foreign_writes_{0};
  uint32_t conflicts_{0};
};

}  // namespace klink
}  // namespace kessil
