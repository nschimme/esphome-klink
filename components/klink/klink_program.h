#pragma once
// The interface a program host exposes to the program entities (slot text, point editor, mode
// select, ...) and to the web API. Today the only implementation is KLinkGroup, which drives a
// K-Link bus group; programs that drive ordinary ESPHome lights implement the same interface.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "klink_acclimation.h"
#include "klink_mode.h"
#include "klink_schedule.h"

namespace esphome {
namespace klink {

class KLinkSlotText;

// How Home Assistant's brightness maps to the lamp's UI1: a straight line (Kessil: % = INT/255) or
// the CIE 1931 lightness curve. The light's own gamma (ESPHome default 2.8) is not used.
enum class BrightnessCurve : uint8_t { LINEAR = 0, CIE = 1 };

// Per-group output options, persisted. All default to the Kessil behavior: nothing eased or smoothed.
struct OutputSettings {
  uint8_t curve{0};       // BrightnessCurve: 0 linear, 1 CIE 1931
  uint16_t fade_ds{0};    // fade time in tenths of a second (0-100), 0 = jumps are immediate
  uint8_t soft_start{30};  // seconds to fade in from dark after every start (0-120), 0 = no soft start
};

// Where a program's values come from: the user's own points, or a day generated from the sun or the moon
// at the device's location.
enum class ProgramSource : uint8_t { POINTS = 0, SUN = 1, MOON = 2 };

// The device-wide location (latitude and longitude in degrees, east positive). One per device so a Sun
// program and a Moon program on different lights agree.
class LocationProvider {
 public:
  virtual ~LocationProvider() = default;
  virtual bool location(double &latitude, double &longitude) const = 0;
  // "entered here", "Home Assistant" or "YAML"; empty when there is no location.
  virtual const char *location_source() const = 0;
  // The fraction (0 to 1) to take off the sun's intensity right now because of the weather, already scaled
  // by the user's maximum; 0 when the weather is not followed.
  virtual float weather_dimming() const { return 0.0f; }
};

// What the user can tune about the generated sun and moon days. Percent values are of full intensity.
struct SkySettings {
  uint8_t peak{60};   // sun: brightest intensity at noon, percent (60 = INT 153, the stock preset's peak)
  uint8_t hours{0};   // sun: 0 follows the real sun; 4-16 fixes the day length to that many hours
  uint8_t glow{100};  // sun: sunrise and sunset glow (red/amber on a Tuna Sun, violet on a Tuna Blue), percent
  uint8_t amber{0};   // sun, Tuna Sun: amber night glow, percent (0-10)
  uint8_t ramp{15};   // sun: minutes from sunrise to near-peak, in tens (15 = 150 minutes); 0 follows the real sun
  int8_t shift{0};    // sun and moon: minutes added to every point (-120 to 120)
  uint8_t moon{5};    // moon: brightest moonlight, percent of full intensity
  uint8_t phase{1};   // moon: scale with the moon's phase (1) or always full (0)
};

class ProgramHost {
 public:
  virtual ~ProgramHost() = default;

  // "bus" for a K-Link group; other kinds are up to later implementations.
  virtual const char *kind() const = 0;
  virtual uint8_t gid() const = 0;
  virtual std::string name() const = 0;
  virtual std::string model_name() const = 0;
  virtual const kessil::klink::SlotLabels *slot_labels() const = 0;
  virtual size_t num_slots() const = 0;
  virtual const kessil::klink::SchedulePoint &point(size_t index) const = 0;
  // The shipped ("factory") point for a slot, independent of edits stored since. False when there is
  // no slot text for `index` or its initial value does not parse; `out` is then a disabled point.
  virtual bool default_point(size_t index, kessil::klink::SchedulePoint &out) const = 0;
  virtual size_t selected_point() const = 0;
  virtual void select_point(size_t index) = 0;
  virtual void edit_point_time(uint8_t hour, uint8_t minute) = 0;
  virtual void edit_point_channel(uint8_t index, uint8_t value) = 0;
  virtual void edit_point_enabled(bool enabled) = 0;
  virtual bool set_slot_from_values(size_t index, uint16_t minute,
                                    const uint8_t channels[kessil::klink::NUM_CHANNELS], bool enabled) = 0;
  virtual void set_slot(size_t index, const kessil::klink::SchedulePoint &point) = 0;
  virtual bool copy_program_from(const ProgramHost &other) = 0;
  virtual void register_slot_text(size_t index, KLinkSlotText *text) = 0;
  virtual void add_on_editor_callback(std::function<void()> &&cb) = 0;
  virtual kessil::klink::Mode mode() const = 0;
  virtual void set_mode(kessil::klink::Mode mode) = 0;
  virtual void add_on_mode_callback(std::function<void()> &&cb) = 0;
  virtual std::string status_text() = 0;
  virtual void preview_point(size_t slot) = 0;
  virtual void preview_minute(uint16_t minute_of_day) = 0;
  virtual void end_preview() = 0;
  virtual int now_minute() const = 0;
  virtual void set_extra(uint8_t channel, uint8_t value) = 0;
  virtual uint8_t extra(uint8_t channel) const = 0;
  virtual void set_extra_silent(uint8_t channel, uint8_t value) = 0;
  // Told when the manual channel values changed by themselves (a snapped color from Home Assistant).
  virtual void add_on_extras_callback(std::function<void()> &&cb) { (void) cb; }

  // Effects as a temporary overlay on the schedule. Hosts without effects keep these defaults.
  // The light's effect names, without "None".
  virtual std::vector<std::string> effect_names() const { return {}; }
  virtual bool start_effect(const std::string &name) { return false; }
  virtual void stop_effect() {}
  // Seconds left, 0 when no effect is running, -1 when it runs until stopped.
  virtual int32_t effect_remaining_seconds() const { return 0; }
  // Name of the running effect, empty when none is running.
  virtual std::string effect_label() const { return ""; }
  virtual uint8_t effect_speed() const { return 0; }  // 0 fast, 1 moderate, 2 slow
  virtual uint32_t effect_duration_ms() const { return 120000; }
  virtual void set_effect_options(uint8_t speed, uint32_t duration_ms) {}

  // Sun and moon programs: hosts without them keep these defaults.
  virtual void set_location_provider(const LocationProvider *provider) { (void) provider; }
  virtual bool has_sources() const { return false; }
  // The moon needs blue light: only a Tuna Blue or a light that mixes color can show it. The Sun source works
  // on every program (intensity, color temperature, glow where the light has them).
  virtual bool can_moon() const { return false; }
  virtual ProgramSource source() const { return ProgramSource::POINTS; }
  virtual void set_source(ProgramSource source) { (void) source; }
  virtual void add_on_source_callback(std::function<void()> &&cb) { (void) cb; }

  // Acclimation ramp: scales the program's intensity up over days. Every program has it.
  virtual kessil::klink::AcclimationPlan acclimation() const { return kessil::klink::AcclimationPlan(); }
  virtual void set_acclimation(const kessil::klink::AcclimationPlan &plan) { (void) plan; }
  virtual float acclimation_share() const { return 1.0f; }
  virtual void add_on_acclimation_callback(std::function<void()> &&cb) { (void) cb; }

  // Sun and moon days: tuning, any date's day for the page's preview, and today's day for the lamps.
  virtual SkySettings sky_settings() const { return SkySettings(); }
  virtual void set_sky_settings(const SkySettings &settings) { (void) settings; }
  // The day (points and a JSON summary object) for a local date, whatever the active source: false when there
  // is no location or no such source. Read-only; the page uses it for the preview chips.
  virtual bool generate_day(ProgramSource source, int year, int month, int day,
                            std::vector<kessil::klink::SchedulePoint> &points, std::string &summary) const {
    (void) source; (void) year; (void) month; (void) day; (void) points; (void) summary;
    return false;
  }
  virtual const std::vector<kessil::klink::SchedulePoint> &generated_points() const {
    static const std::vector<kessil::klink::SchedulePoint> none;
    return none;
  }
  virtual std::string source_note() const { return std::string(); }
  // Today's generated day becomes the user's own points (simplified to fit); the source returns to My points.
  virtual bool copy_generated_to_points() { return false; }

  // Output options (brightness curve, transition time and style): only the bus groups have them.
  virtual bool has_output_options() const { return false; }
  virtual OutputSettings output_settings() const { return OutputSettings(); }
  virtual void set_output_settings(const OutputSettings &settings) { (void) settings; }
  virtual void add_on_output_settings_callback(std::function<void()> &&cb) { (void) cb; }
};

}  // namespace klink
}  // namespace esphome
