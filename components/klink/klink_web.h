#pragma once
#include "esphome/core/defines.h"
#ifdef USE_KLINK_WEB

#include <string>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include "klink.h"
#include "klink_colors.h"

namespace esphome {
namespace klink {

// The gzipped page, generated at compile time by components/klink/web_build.py and emitted as a
// global in main.cpp.
extern const uint8_t KLINK_PAGE[] PROGMEM;
extern const size_t KLINK_PAGE_LEN;

// Colour models, generated at compile time by components/klink/klink_colors.py and emitted as
// globals in main.cpp. Declared here first so the generated const definitions get external
// linkage (the same pattern as KLINK_PAGE).
extern const kessil::klink::ColorSpec COLOR_SUN;
extern const kessil::klink::ColorSpec COLOR_BLUE;

// The K-Link web handler: the page at /klink and the JSON API under /klink. Registered with the
// shared web server (web_server_base) so it works with or without the full `web_server:` component.
class KLinkWeb : public AsyncWebHandler, public Component {
 public:
  explicit KLinkWeb(web_server_base::WebServerBase *base) : base_(base) {}
  void set_hub(KLinkHub *hub) { this->hub_ = hub; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI; }

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  void handle_index(AsyncWebServerRequest *request);
  void handle_state(AsyncWebServerRequest *request);
  void handle_mode(AsyncWebServerRequest *request);
  void handle_identify(AsyncWebServerRequest *request);
  void handle_point(AsyncWebServerRequest *request);
  void handle_preview(AsyncWebServerRequest *request);
  void handle_preview_done(AsyncWebServerRequest *request);
  void handle_assign(AsyncWebServerRequest *request);
  void handle_name(AsyncWebServerRequest *request);
  void handle_copy(AsyncWebServerRequest *request);
  void handle_retry(AsyncWebServerRequest *request);
  void handle_writes(AsyncWebServerRequest *request);
  void handle_scan(AsyncWebServerRequest *request);
  void handle_snap(AsyncWebServerRequest *request);

  ProgramHost *find_group(uint32_t gid);
  std::string lamp_name(uint32_t kid);
  bool read_uint_arg(AsyncWebServerRequest *request, const char *name, uint32_t max, uint32_t &out);
  bool read_bool_arg(AsyncWebServerRequest *request, const char *name, bool &out);
  void send_ok(AsyncWebServerRequest *request);

  web_server_base::WebServerBase *base_;
  KLinkHub *hub_{nullptr};
};

}  // namespace klink
}  // namespace esphome

#endif  // USE_KLINK_WEB
