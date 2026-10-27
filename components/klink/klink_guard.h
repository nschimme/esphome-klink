#pragma once
// Bus guard: stops this controller from transmitting while another controller is on the bus.
// A second master corrupts frames and can make fixtures misbehave, and Kessil's own
// controllers announce themselves once per second, so another controller is easy to hear.
// No ESPHome dependencies (host tested); time is injected as milliseconds.
#include <cstdint>
#include <string>

namespace kessil {
namespace klink {

class BusGuard {
 public:
  explicit BusGuard(uint32_t quiet_ms = 15000) : quiet_ms_(quiet_ms) {}

  void set_quiet_ms(uint32_t quiet_ms) { quiet_ms_ = quiet_ms; }
  // Call once at startup: the quiet period is counted from here.
  void start(uint32_t now) {
    start_ms_ = now;
    started_ = true;
  }

  // A controller-shaped frame was heard that this controller did not send.
  // Returns true if the caller must stop transmitting immediately (we were transmitting).
  bool on_foreign_frame(uint32_t now) {
    last_foreign_ms_ = now;
    ever_foreign_ = true;
    foreign_frames_++;
    if (transmitting_) {
      latched_ = true;
      transmitting_ = false;
      return true;
    }
    return false;
  }

  // Ask to start transmitting. Returns false, with a reason, when it is not safe.
  bool request_transmit(uint32_t now, std::string &reason) {
    if (!started_) {
      reason = "bus guard not started";
      return false;
    }
    uint32_t listened = now - start_ms_;
    if (listened < quiet_ms_) {
      reason = "still listening to the bus (" + std::to_string((quiet_ms_ - listened + 999) / 1000) + " s left)";
      return false;
    }
    if (ever_foreign_ && now - last_foreign_ms_ < quiet_ms_) {
      reason = "another controller was heard on the bus " + std::to_string((now - last_foreign_ms_) / 1000) +
               " s ago; unplug or power it off first";
      return false;
    }
    latched_ = false;
    transmitting_ = true;
    return true;
  }

  void stop_transmit() { transmitting_ = false; }
  // A conflict stays latched until the operator turns writes off and asks again.
  bool conflict_latched() const { return latched_; }
  bool transmitting() const { return transmitting_; }
  uint32_t foreign_frames() const { return foreign_frames_; }
  // True while another controller has been heard within the quiet period.
  bool other_controller_recent(uint32_t now) const { return ever_foreign_ && now - last_foreign_ms_ < quiet_ms_; }

 private:
  uint32_t quiet_ms_;
  uint32_t start_ms_ = 0;
  uint32_t last_foreign_ms_ = 0;
  uint32_t foreign_frames_ = 0;
  bool started_ = false;
  bool ever_foreign_ = false;
  bool transmitting_ = false;
  bool latched_ = false;
};

}  // namespace klink
}  // namespace kessil
