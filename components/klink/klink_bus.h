#pragma once
// Non-blocking K-Link bus: a FIFO of requests, one frame in flight, timeouts from an injected
// clock, a per-loop byte budget, and echo/garbage handling. Jobs (scan, identity read) are
// chained callbacks, so nothing ever sleeps. No ESPHome dependencies (host tested).
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "klink_frame.h"

namespace kessil {
namespace klink {

// Hardware abstraction supplied by the ESPHome component (or a fake in tests).
class Io {
 public:
  virtual ~Io() {}
  virtual uint32_t now_ms() = 0;
  virtual bool read_byte(uint8_t &out) = 0;  // false when nothing is waiting
  virtual void write(const std::vector<uint8_t> &frame) = 0;
};

class Bus {
 public:
  // ok=true: a complete valid reply arrived (empty for fire-and-forget). ok=false: timeout,
  // `reply` holds whatever partial bytes arrived (empty means silence).
  typedef std::function<void(bool ok, const std::vector<uint8_t> &reply)> Callback;

  // Called for every valid frame that is not the reply to the request in flight: traffic from
  // another controller (such as the WiFi dongle) or unsolicited fixture frames.
  typedef std::function<void(const std::vector<uint8_t> &frame)> FrameCallback;

  explicit Bus(Io &io) : io_(io) {}

  void set_frame_callback(FrameCallback cb) { frame_cb_ = cb; }
  const FrameParser &monitor() const { return monitor_; }

  void set_min_gap_ms(uint32_t gap) { min_gap_ms_ = gap; }
  void set_byte_budget(uint32_t bytes_per_loop) { byte_budget_ = bytes_per_loop; }

  // expect_cmd == 0: fire and forget. Otherwise wait for a valid frame with that command byte.
  void enqueue(const std::vector<uint8_t> &frame, uint8_t expect_cmd, uint32_t timeout_ms, Callback cb) {
    Request r;
    r.frame = frame;
    r.expect_cmd = expect_cmd;
    r.timeout_ms = timeout_ms;
    r.cb = cb;
    queue_.push_back(r);
  }

  size_t pending() const { return queue_.size() + (receiving_ ? 1 : 0); }
  bool busy() const { return pending() != 0; }
  uint32_t timeouts() const { return timeouts_; }
  uint32_t frames_sent() const { return frames_sent_; }

  // Call from the component's loop(). Does a bounded amount of work and never blocks.
  void loop() {
    uint32_t now = io_.now_ms();
    if (receiving_) {
      read_(now);
      return;
    }
    if (queue_.empty() || (have_activity_ && elapsed_(now, last_activity_) < min_gap_ms_)) {
      listen_();  // nothing to send yet: report whatever is on the bus
      return;
    }
    current_ = queue_.front();
    queue_.pop_front();
    uint8_t stale;
    for (uint32_t i = 0; i < byte_budget_ && io_.read_byte(stale); i++) {
    }  // drop stale bytes so replies line up with this request
    rx_.clear();
    garbage_.clear();
    io_.write(current_.frame);
    frames_sent_++;
    last_tx_ = current_.frame;
    last_tx_ms_ = now;
    echo_pending_ = true;
    last_activity_ = now;
    have_activity_ = true;
    if (current_.expect_cmd == 0) {
      Callback cb = current_.cb;
      if (cb)
        cb(true, std::vector<uint8_t>());
      return;
    }
    receiving_ = true;
    deadline_ = now + current_.timeout_ms;
  }

 private:
  struct Request {
    std::vector<uint8_t> frame;
    uint8_t expect_cmd;
    uint32_t timeout_ms;
    Callback cb;
  };

  static uint32_t elapsed_(uint32_t now, uint32_t since) { return now - since; }

  static bool valid_frame_(const std::vector<uint8_t> &r) {
    if (r.size() < 7 || r.front() != START || r.back() != STOP || r[1] != r.size() - 3)
      return false;
    uint8_t sum = 0;
    for (size_t i = 1; i + 1 < r.size(); i++)
      sum = static_cast<uint8_t>(sum + r[i]);
    return sum == 0;
  }

  // A bus that reads its own transmissions back (direct drive, or a transceiver that leaves its
  // receiver on) hands us our own frame. Swallow exactly one copy, shortly after sending, so it is
  // never mistaken for another controller.
  void deliver_(const std::vector<uint8_t> &frame) {
    if (echo_pending_ && frame == last_tx_ && elapsed_(io_.now_ms(), last_tx_ms_) <= ECHO_WINDOW_MS) {
      echo_pending_ = false;
      return;
    }
    if (frame_cb_)
      frame_cb_(frame);
  }

  // Idle receive: run incoming bytes through the frame parser and report each valid frame.
  void listen_() {
    if (!frame_cb_)
      return;  // nobody is listening; leave stale bytes to be dropped when the next request starts
    uint8_t b;
    std::vector<uint8_t> frame;
    for (uint32_t i = 0; i < byte_budget_ && io_.read_byte(b); i++) {
      bool got = monitor_.feed(b, frame);
      while (got) {
        deliver_(frame);
        got = monitor_.poll(frame);
      }
    }
  }

  void finish_(bool ok) {
    receiving_ = false;
    Callback cb = current_.cb;
    std::vector<uint8_t> reply;
    reply.swap(rx_);
    // A timeout after an invalid frame is not silence: hand the invalid bytes to the caller so it can
    // tell a collision from an empty range.
    if (!ok && reply.empty())
      reply.swap(garbage_);
    garbage_.clear();
    last_activity_ = io_.now_ms();
    if (!ok)
      timeouts_++;
    if (cb)
      cb(ok, reply);
  }

  void read_(uint32_t now) {
    uint8_t b;
    for (uint32_t i = 0; i < byte_budget_ && io_.read_byte(b); i++) {
      if (rx_.empty() && b != START) {
        // Resync on the frame marker. The skipped byte is still activity on the line: overlapping
        // replies often arrive as bytes that never form a frame, which is not the same as silence.
        if (garbage_.size() < 32)
          garbage_.push_back(b);
        continue;
      }
      rx_.push_back(b);
      last_activity_ = now;
      // Half-duplex transceivers echo our own frame back: drop it and keep listening.
      if (rx_.size() == current_.frame.size() && rx_ == current_.frame) {
        echo_pending_ = false;
        rx_.clear();
        continue;
      }
      if (rx_.size() >= 2 && rx_.size() == static_cast<size_t>(rx_[1]) + 3) {
        if (valid_reply(rx_, current_.expect_cmd)) {
          finish_(true);
          return;
        }
        if (valid_frame_(rx_)) {
          if (frame_cb_)
            deliver_(rx_);  // valid, but not our reply: let the monitor see it
        } else {
          garbage_ = rx_;  // a complete frame that fails its checksum: overlapping replies look like this
        }
        rx_.clear();
      }
    }
    if (static_cast<int32_t>(now - deadline_) >= 0)
      finish_(false);
  }

  Io &io_;
  std::deque<Request> queue_;
  Request current_;
  std::vector<uint8_t> rx_;
  std::vector<uint8_t> garbage_;  // last complete frame that failed validation while waiting
  bool receiving_ = false;
  bool have_activity_ = false;
  uint32_t deadline_ = 0;
  uint32_t last_activity_ = 0;
  static const uint32_t ECHO_WINDOW_MS = 100;
  std::vector<uint8_t> last_tx_;
  uint32_t last_tx_ms_ = 0;
  bool echo_pending_ = false;
  uint32_t min_gap_ms_ = 20;
  uint32_t byte_budget_ = 64;
  uint32_t timeouts_ = 0;
  uint32_t frames_sent_ = 0;
  FrameCallback frame_cb_;
  FrameParser monitor_;
};

// Mute reply seen on hardware: A9 05 00 00 19 FF E3 5C. Unmute (1A) is not acknowledged.
static const uint32_t REPLY_TIMEOUT_MS = 150;

// Unmute `kid`, read GID/type (1C), then mute it again whatever happened. `done` gets the result.
typedef std::function<void(bool ok, Identity id)> IdentityCallback;
inline void read_identity_async(Bus &bus, uint32_t kid, IdentityCallback done) {
  bus.enqueue(unmute(kid), 0, 0, nullptr);
  bus.enqueue(identity(kid), CMD_GET_GID_TYPE, REPLY_TIMEOUT_MS, [&bus, kid, done](bool ok, const std::vector<uint8_t> &r) {
    Identity id = {0, 0};
    bool parsed = ok && parse_identity(r, id);
    bus.enqueue(mute(kid), CMD_MUTE, REPLY_TIMEOUT_MS, [done, parsed, id](bool, const std::vector<uint8_t> &) {
      if (done)
        done(parsed, id);
    });
  });
}

// Binary-search discovery with command 18: an empty range stays silent, exactly one responder
// answers with its KID (and is then muted so the range can be asked again), several collide into
// garbage and are split. Found lamps are left MUTED; read their identity with an unmute first.
class Scanner {
 public:
  typedef std::function<void(bool complete, const std::vector<uint32_t> &kids)> Done;

  explicit Scanner(Bus &bus) : bus_(bus) {}

  bool running() const { return running_; }
  uint32_t queries() const { return queries_; }
  uint32_t silent_ranges() const { return silent_; }

  void start(uint32_t lo, uint32_t hi_exclusive, Done done) {
    stack_.clear();
    found_.clear();
    queries_ = silent_ = 0;
    repeats_ = 0;
    done_ = done;
    running_ = true;
    stack_.push_back(Range{lo, hi_exclusive});
    next_();
  }

 private:
  struct Range {
    uint32_t lo, hi;
  };

  void finish_(bool complete) {
    running_ = false;
    Done d = done_;
    if (d)
      d(complete, found_);
  }

  void next_() {
    if (stack_.empty()) {
      finish_(true);
      return;
    }
    Range r = stack_.back();
    stack_.pop_back();
    query_(r, 0);
  }

  // Several lamps in one range either collide into an invalid frame or one of them wins and answers
  // cleanly (the others stay hidden behind it). Either way the range is not finished until it falls
  // silent: a lamp that answers is muted and the same range is asked again, so every lamp in the range is found.
  void query_(Range r, int attempt) {
    queries_++;
    bus_.enqueue(branch(r.lo, r.hi), CMD_BRANCH, REPLY_TIMEOUT_MS, [this, r, attempt](bool ok, const std::vector<uint8_t> &reply) {
      if (ok && reply.size() == 11) {
        uint32_t kid = (static_cast<uint32_t>(reply[5]) << 24) | (static_cast<uint32_t>(reply[6]) << 16) |
                       (static_cast<uint32_t>(reply[7]) << 8) | reply[8];
        if (kid >= r.lo && kid < r.hi) {
          if (already_found_(kid)) {
            // Still answering after being muted: the mute did not take. Retry it once, then give up
            // rather than loop forever.
            if (++repeats_ > 2) {
              finish_(false);
              return;
            }
          } else {
            found_.push_back(kid);
            repeats_ = 0;
          }
          bus_.enqueue(mute(kid), CMD_MUTE, REPLY_TIMEOUT_MS, [this, r](bool, const std::vector<uint8_t> &) { query_(r, 0); });
          return;
        }
      } else if (!ok && reply.empty()) {  // silence: nothing left in this range, retry once like the Python tool
        if (attempt < 1) {
          query_(r, attempt + 1);
          return;
        }
        silent_++;
        next_();
        return;
      }
      // Garbage, a short frame, an invalid frame, or a KID outside the range: several lamps answered. Split.
      if (r.hi - r.lo <= 1) {
        finish_(false);
        return;
      }
      uint32_t mid = r.lo + (r.hi - r.lo) / 2;
      stack_.push_back(Range{mid, r.hi});
      stack_.push_back(Range{r.lo, mid});
      next_();
    });
  }

  bool already_found_(uint32_t kid) const {
    for (size_t i = 0; i < found_.size(); i++)
      if (found_[i] == kid)
        return true;
    return false;
  }

  Bus &bus_;
  std::vector<Range> stack_;
  std::vector<uint32_t> found_;
  Done done_;
  bool running_ = false;
  int repeats_ = 0;
  uint32_t queries_ = 0;
  uint32_t silent_ = 0;
};

}  // namespace klink
}  // namespace kessil
