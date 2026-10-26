#include "klink_web.h"
#ifdef USE_KLINK_WEB

#include <cstring>

#include "esphome/core/log.h"

#include "klink_http.h"
#include "klink_light_map.h"

#if defined(USE_ESP32)
#ifndef HTTPD_400
#define HTTPD_400 "400 Bad Request"
#endif
#ifndef HTTPD_405
#define HTTPD_405 "405 Method Not Allowed"
#endif
#endif

namespace esphome {
namespace klink {

static const char *const TAG = "klink.web";

namespace kk = kessil::klink;

static bool klink_path_prefix(const char *path) {
  return std::strncmp(path, "/klink", 6) == 0 && (path[6] == '\0' || path[6] == '/');
}

static const char *mode_json_name(kk::Mode mode) {
  switch (mode) {
    case kk::Mode::PROGRAM:
      return "program";
    case kk::Mode::MANUAL:
      return "manual";
    case kk::Mode::OFF:
      return "off";
    case kk::Mode::PREVIEW:
      return "preview";
  }
  return "";
}

static bool is_post_route(const char *path) {
  return std::strcmp(path, "/klink/mode") == 0 || std::strcmp(path, "/klink/identify") == 0 ||
         std::strcmp(path, "/klink/point") == 0 || std::strcmp(path, "/klink/preview") == 0 ||
         std::strcmp(path, "/klink/preview/done") == 0 || std::strcmp(path, "/klink/assign") == 0 ||
         std::strcmp(path, "/klink/name") == 0 || std::strcmp(path, "/klink/copy") == 0 ||
         std::strcmp(path, "/klink/retry") == 0 || std::strcmp(path, "/klink/writes") == 0 || std::strcmp(path, "/klink/scan") == 0;
}

// GET-only routes. A POST to one of these answers 405, matching how a GET on a POST route does.
static bool is_get_route(const char *path) {
  return std::strcmp(path, "/klink") == 0 || std::strcmp(path, "/klink/state") == 0 ||
         std::strcmp(path, "/klink/snap") == 0;
}

// `request->arg()` returns std::string on the IDF server and an Arduino String on the Arduino one;
// normalise to std::string for the JSON building below.
static std::string arg_string(AsyncWebServerRequest *request, const char *name) {
#if defined(USE_ESP32)
  return request->arg(name);
#else
  const auto &value = request->arg(name);
  return std::string(value.c_str(), value.length());
#endif
}

static void append_quoted(std::string &out, const std::string &value) {
  out += '"';
  out += json_escape(value);
  out += '"';
}

static void send_json(AsyncWebServerRequest *request, int code, const std::string &body) {
#if defined(USE_ESP32)
  if (code == 400 || code == 405) {
    // The IDF wrapper only passes 200/404/409 through verbatim and maps every other status to 500,
    // so emit 400/405 through the raw API.
    httpd_resp_set_status(*request, code == 405 ? HTTPD_405 : HTTPD_400);
    httpd_resp_set_type(*request, "application/json");
    httpd_resp_set_hdr(*request, "Accept-Ranges", "none");
    httpd_resp_send(*request, body.c_str(), HTTPD_RESP_USE_STRLEN);
    return;
  }
  AsyncWebServerResponse *response = request->beginResponse(code, "application/json", body);
  response->addHeader("cache-control", "no-store");
  request->send(response);
#else
  // The Arduino server accepts arbitrary status codes directly and has no std::string overload.
  request->send(code, "application/json", body.c_str());
#endif
}

static void send_error(AsyncWebServerRequest *request, int code, const std::string &message) {
  std::string body;
  body.reserve(message.size() + 16);
  body += "{\"error\":\"";
  body += json_escape(message);
  body += "\"}";
  send_json(request, code, body);
}

void KLinkWeb::setup() {
  // Make sure the shared server exists even without the full web_server: component; init() is
  // ref-counted, so a web_server that also calls it only adds a reference.
  this->base_->init();
  this->base_->add_handler(this);
}

void KLinkWeb::dump_config() {
  ESP_LOGCONFIG(TAG, "K-Link web:");
  ESP_LOGCONFIG(TAG, "  Page and API: /klink");
}

bool KLinkWeb::canHandle(AsyncWebServerRequest *request) const {
#ifdef USE_ESP32
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(url_buf);
#else
  const auto &url = request->url();
#endif
  return klink_path_prefix(url.c_str());
}

void KLinkWeb::handleRequest(AsyncWebServerRequest *request) {
#ifdef USE_ESP32
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(url_buf);
#else
  const auto &url = request->url();
#endif
  const char *path = url.c_str();

  if (request->method() == HTTP_GET) {
    if (std::strcmp(path, "/klink") == 0) {
      this->handle_index(request);
      return;
    }
    if (std::strcmp(path, "/klink/state") == 0) {
      this->handle_state(request);
      return;
    }
    if (std::strcmp(path, "/klink/snap") == 0) {
      this->handle_snap(request);
      return;
    }
    if (is_post_route(path)) {
      send_error(request, 405, "method not allowed");
      return;
    }
    send_error(request, 404, "not found");
    return;
  }

  if (request->method() == HTTP_POST) {
    if (std::strcmp(path, "/klink/mode") == 0) {
      this->handle_mode(request);
      return;
    }
    if (std::strcmp(path, "/klink/identify") == 0) {
      this->handle_identify(request);
      return;
    }
    if (std::strcmp(path, "/klink/point") == 0) {
      this->handle_point(request);
      return;
    }
    if (std::strcmp(path, "/klink/preview") == 0) {
      this->handle_preview(request);
      return;
    }
    if (std::strcmp(path, "/klink/preview/done") == 0) {
      this->handle_preview_done(request);
      return;
    }
    if (std::strcmp(path, "/klink/assign") == 0) {
      this->handle_assign(request);
      return;
    }
    if (std::strcmp(path, "/klink/name") == 0) {
      this->handle_name(request);
      return;
    }
    if (std::strcmp(path, "/klink/copy") == 0) {
      this->handle_copy(request);
      return;
    }
    if (std::strcmp(path, "/klink/retry") == 0) {
      this->handle_retry(request);
      return;
    }
    if (std::strcmp(path, "/klink/writes") == 0) {
      this->handle_writes(request);
      return;
    }
    if (std::strcmp(path, "/klink/scan") == 0) {
      this->handle_scan(request);
      return;
    }
    if (is_get_route(path)) {
      send_error(request, 405, "method not allowed");
      return;
    }
    send_error(request, 404, "not found");
    return;
  }

  send_error(request, 405, "method not allowed");
}

void KLinkWeb::handle_index(AsyncWebServerRequest *request) {
#ifndef USE_ESP8266
  AsyncWebServerResponse *response = request->beginResponse(200, "text/html", KLINK_PAGE, KLINK_PAGE_LEN);
#else
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", KLINK_PAGE, KLINK_PAGE_LEN);
#endif
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
}

ProgramHost *KLinkWeb::find_group(uint32_t gid) {
  if (this->hub_ == nullptr)
    return nullptr;
  for (auto *group : this->hub_->programs())
    if (group->gid() == gid)
      return group;
  return nullptr;
}

std::string KLinkWeb::lamp_name(uint32_t kid) {
  const char *name = this->hub_ != nullptr ? this->hub_->names().get(kid) : nullptr;
  return name != nullptr ? name : "";
}

bool KLinkWeb::read_uint_arg(AsyncWebServerRequest *request, const char *name, uint32_t max, uint32_t &out) {
  if (!request->hasArg(name)) {
    send_error(request, 400, std::string("missing parameter: ") + name);
    return false;
  }
  if (!parse_uint(arg_string(request, name), max, out)) {
    send_error(request, 400, std::string("invalid parameter: ") + name);
    return false;
  }
  return true;
}

// A program route accepts both id kinds: a bus group (1-40) or a light program (101-164).
bool KLinkWeb::read_program_gid(AsyncWebServerRequest *request, const char *name, uint32_t &out) {
  if (!this->read_uint_arg(request, name, kk::PROGRAM_ID_MAX, out))
    return false;
  if (!kk::is_bus_gid(out) && !kk::is_program_id(out)) {
    send_error(request, 400, std::string(name) + " must be 1-40 or 101-164");
    return false;
  }
  return true;
}

// A route that needs real lamps on the bus: only bus group ids qualify, never a program id.
bool KLinkWeb::read_lamp_gid(AsyncWebServerRequest *request, const char *name, uint32_t &out) {
  if (!this->read_uint_arg(request, name, kk::PROGRAM_ID_MAX, out))
    return false;
  if (kk::is_program_id(out)) {
    send_error(request, 400, "not a lamp group");
    return false;
  }
  if (!kk::is_bus_gid(out)) {
    send_error(request, 400, std::string(name) + " must be 1-40");
    return false;
  }
  return true;
}

bool KLinkWeb::read_bool_arg(AsyncWebServerRequest *request, const char *name, bool &out) {
  if (!request->hasArg(name)) {
    send_error(request, 400, std::string("missing parameter: ") + name);
    return false;
  }
  const std::string value = arg_string(request, name);
  if (value == "1" || value == "true") {
    out = true;
    return true;
  }
  if (value == "0" || value == "false") {
    out = false;
    return true;
  }
  send_error(request, 400, std::string("invalid parameter: ") + name);
  return false;
}

void KLinkWeb::send_ok(AsyncWebServerRequest *request) { send_json(request, 200, "{\"ok\":true}"); }

// One slot in the state JSON: the same shape for "slots" and "defaults".
static void append_slot_json(std::string &out, const kk::SchedulePoint &p) {
  out += "{\"minute\":";
  out += std::to_string(p.minute);
  out += ",\"ch\":[";
  for (size_t c = 0; c < kk::NUM_CHANNELS; c++) {
    if (c)
      out += ',';
    out += std::to_string(p.channels[c]);
  }
  out += "],\"enabled\":";
  out += p.enabled ? "true" : "false";
  out += '}';
}

void KLinkWeb::handle_state(AsyncWebServerRequest *request) {
  std::string out;
  out.reserve(512);
  out += "{\"groups\":[";
  bool first_group = true;
  for (auto *group : this->hub_->programs()) {
    if (!first_group)
      out += ',';
    first_group = false;

    out += "{\"gid\":";
    out += std::to_string(group->gid());
    out += ",\"kind\":";
    append_quoted(out, group->kind());
    out += ",\"name\":";
    append_quoted(out, group->name());
    out += ",\"model\":";
    append_quoted(out, group->model_name());
    out += ",\"mode\":";
    append_quoted(out, mode_json_name(group->mode()));
    out += ",\"status\":";
    append_quoted(out, group->status_text());
    // Channel names and limits for the program editor; null marks a channel the model does not have.
    out += ",\"labels\":[";
    for (size_t c = 0; c < kk::NUM_CHANNELS; c++) {
      if (c)
        out += ',';
      const char *label = group->slot_labels()->name[c];
      if (label == nullptr)
        out += "null";
      else
        append_quoted(out, label);
    }
    out += "],\"max\":[";
    for (size_t c = 0; c < kk::NUM_CHANNELS; c++) {
      if (c)
        out += ',';
      out += std::to_string(kk::CHANNEL_MAX[c]);
    }
    out += ']';
    out += ",\"slots\":[";
    for (size_t i = 0; i < group->num_slots(); i++) {
      if (i)
        out += ',';
      append_slot_json(out, group->point(i));
    }
    // The shipped program, so the page can offer "Default" after any edit.
    out += "],\"defaults\":[";
    for (size_t i = 0; i < group->num_slots(); i++) {
      if (i)
        out += ',';
      kk::SchedulePoint p;
      group->default_point(i, p);
      append_slot_json(out, p);
    }
    out += "],\"lamps\":[";
    // A light program drives ESPHome lights, not lamps on the bus, so its lamp list is empty.
    if (!kk::is_program_id(group->gid())) {
      bool first_lamp = true;
      for (const auto &lamp : this->hub_->lamps()) {
        if (lamp.gid != group->gid())
          continue;
        if (!first_lamp)
          out += ',';
        first_lamp = false;
        out += std::to_string(lamp.kid);
      }
    }
    out += "],\"now_minute\":";
    out += std::to_string(group->now_minute());
    out += ",\"selected\":";
    out += std::to_string(group->selected_point());
    out += '}';
  }
  out += "],\"lamps\":[";
  bool first_lamp = true;
  for (const auto &lamp : this->hub_->lamps()) {
    if (!first_lamp)
      out += ',';
    first_lamp = false;
    out += "{\"kid\":";
    out += std::to_string(lamp.kid);
    out += ",\"type\":";
    out += std::to_string(lamp.type);
    out += ",\"gid\":";
    out += std::to_string(lamp.gid);
    out += ",\"name\":";
    append_quoted(out, lamp_name(lamp.kid));
    out += '}';
  }
  out += "],\"bus\":{\"state\":";
  append_quoted(out, this->hub_->state_text());
  out += ",\"writes\":";
  out += this->hub_->writes_enabled() ? "true" : "false";
  out += ",\"known_lamps\":";
  out += std::to_string(this->hub_->lamps().size());
  out += "}}";
  send_json(request, 200, out);
}

void KLinkWeb::handle_mode(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  if (!request->hasArg("mode")) {
    send_error(request, 400, "missing parameter: mode");
    return;
  }
  kk::Mode mode;
  if (!kk::mode_from_name(arg_string(request, "mode").c_str(), mode)) {
    send_error(request, 400, "invalid mode (program, manual or off)");
    return;
  }
  // Every change runs on the main loop (as ESPHome's own web handlers do with defer()): this handler
  // runs on the HTTP server's task, and state changes publish to the API and the event stream and
  // write bus frames, none of which is safe from another task.
  this->defer([group, mode]() { group->set_mode(mode); });
  this->send_ok(request);
}

void KLinkWeb::handle_identify(AsyncWebServerRequest *request) {
  const bool has_kid = request->hasArg("kid");
  const bool has_gid = request->hasArg("gid");
  if (has_kid == has_gid) {
    send_error(request, 400, "provide exactly one of kid or gid");
    return;
  }
  uint32_t value;
  if (has_kid) {
    if (!this->read_uint_arg(request, "kid", 0xFFFFFFFFu, value))
      return;
  } else {
    // Identifying needs real lamps: a program id is not a lamp group.
    if (!this->read_lamp_gid(request, "gid", value))
      return;
  }
  if (!this->hub_->writes_enabled()) {
    send_error(request, 409, "Send to lights is off");
    return;
  }
  KLinkHub *hub = this->hub_;
  if (has_kid) {
    this->defer([hub, value]() { hub->identify(value); });
  } else {
    bool any = false;
    for (const auto &lamp : hub->lamps())
      if (lamp.gid == value)
        any = true;
    if (!any) {
      send_error(request, 404, "no known lamps in that group");
      return;
    }
    const uint8_t gid = static_cast<uint8_t>(value);
    this->defer([hub, gid]() { hub->identify_gid(gid); });
  }
  this->send_ok(request);
}

void KLinkWeb::handle_point(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  uint32_t slot;
  if (!this->read_uint_arg(request, "slot", static_cast<uint32_t>(group->num_slots()), slot))
    return;
  if (slot < 1) {
    send_error(request, 400, "slot must be 1-based");
    return;
  }
  size_t index = slot - 1;

  kk::SchedulePoint p = group->point(index);
  uint32_t value;
  if (request->hasArg("minute")) {
    if (!parse_uint(arg_string(request, "minute"), 1439, value)) {
      send_error(request, 400, "invalid parameter: minute");
      return;
    }
    p.minute = static_cast<uint16_t>(value);
  }
  if (request->hasArg("int")) {
    if (!parse_uint(arg_string(request, "int"), kk::CHANNEL_MAX[0], value)) {
      send_error(request, 400, "invalid parameter: int");
      return;
    }
    p.channels[0] = static_cast<uint8_t>(value);
  }
  if (request->hasArg("clr")) {
    if (!parse_uint(arg_string(request, "clr"), kk::CHANNEL_MAX[1], value)) {
      send_error(request, 400, "invalid parameter: clr");
      return;
    }
    p.channels[1] = static_cast<uint8_t>(value);
  }
  if (request->hasArg("r")) {
    if (!parse_uint(arg_string(request, "r"), kk::CHANNEL_MAX[3], value)) {
      send_error(request, 400, "invalid parameter: r");
      return;
    }
    p.channels[3] = static_cast<uint8_t>(value);
  }
  if (request->hasArg("am")) {
    if (!parse_uint(arg_string(request, "am"), kk::CHANNEL_MAX[4], value)) {
      send_error(request, 400, "invalid parameter: am");
      return;
    }
    p.channels[4] = static_cast<uint8_t>(value);
  }
  // Index form for any model: c1..c5 are UI1..UI5 (the page uses the group's labels and limits).
  static const char *const CH_ARGS[kk::NUM_CHANNELS] = {"c1", "c2", "c3", "c4", "c5"};
  for (size_t c = 0; c < kk::NUM_CHANNELS; c++) {
    if (!request->hasArg(CH_ARGS[c]))
      continue;
    if (group->slot_labels()->name[c] == nullptr) {
      send_error(request, 400, std::string("this model has no channel ") + CH_ARGS[c]);
      return;
    }
    if (!parse_uint(arg_string(request, CH_ARGS[c]), kk::CHANNEL_MAX[c], value)) {
      send_error(request, 400, std::string("invalid parameter: ") + CH_ARGS[c]);
      return;
    }
    p.channels[c] = static_cast<uint8_t>(value);
  }
  if (request->hasArg("enabled")) {
    bool enabled;
    if (!this->read_bool_arg(request, "enabled", enabled))
      return;
    p.enabled = enabled;
  }
  this->defer([group, index, p]() { group->set_slot_from_values(index, p.minute, p.channels, p.enabled); });
  this->send_ok(request);
}

void KLinkWeb::handle_preview(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  bool has_slot = request->hasArg("slot");
  bool has_minute = request->hasArg("minute");
  if (has_slot == has_minute) {
    send_error(request, 400, "provide exactly one of slot or minute");
    return;
  }
  uint32_t value;
  if (has_slot) {
    if (!parse_uint(arg_string(request, "slot"), static_cast<uint32_t>(group->num_slots()), value) || value < 1) {
      send_error(request, 400, "invalid parameter: slot");
      return;
    }
    const size_t slot_index = value - 1;
    this->defer([group, slot_index]() { group->preview_point(slot_index); });
  } else {
    if (!parse_uint(arg_string(request, "minute"), 1439, value)) {
      send_error(request, 400, "invalid parameter: minute");
      return;
    }
    const uint16_t minute = static_cast<uint16_t>(value);
    this->defer([group, minute]() { group->preview_minute(minute); });
  }
  this->send_ok(request);
}

void KLinkWeb::handle_preview_done(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  this->defer([group]() { group->end_preview(); });
  this->send_ok(request);
}

void KLinkWeb::handle_assign(AsyncWebServerRequest *request) {
  uint32_t kid;
  if (!this->read_uint_arg(request, "kid", 0xFFFFFFFFu, kid))
    return;
  uint32_t gid;
  if (!this->read_lamp_gid(request, "gid", gid))
    return;
  std::string error;
  if (!this->hub_->check_assign(kid, static_cast<uint8_t>(gid), error)) {
    send_error(request, 409, error.empty() ? "assignment refused" : error);
    return;
  }
  KLinkHub *hub = this->hub_;
  const uint8_t target = static_cast<uint8_t>(gid);
  this->defer([hub, kid, target]() {
    std::string why;
    hub->assign(kid, target, why);
  });
  this->send_ok(request);
}

void KLinkWeb::handle_name(AsyncWebServerRequest *request) {
  uint32_t kid;
  if (!this->read_uint_arg(request, "kid", 0xFFFFFFFFu, kid))
    return;
  if (!request->hasArg("name")) {
    send_error(request, 400, "missing parameter: name");
    return;
  }
  std::string error;
  const std::string name = arg_string(request, "name");
  if (!this->hub_->check_name(kid, name, error)) {
    send_error(request, 400, error);
    return;
  }
  KLinkHub *hub = this->hub_;
  this->defer([hub, kid, name]() {
    std::string why;
    hub->set_name(kid, name, why);
  });
  this->send_ok(request);
}

void KLinkWeb::handle_copy(AsyncWebServerRequest *request) {
  uint32_t from;
  if (!this->read_program_gid(request, "from", from))
    return;
  uint32_t to;
  if (!this->read_program_gid(request, "to", to))
    return;
  ProgramHost *from_group = this->find_group(from);
  ProgramHost *to_group = this->find_group(to);
  if (from_group == nullptr || to_group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }
  if (to_group->model_name() != from_group->model_name() || to_group->num_slots() != from_group->num_slots()) {
    send_error(request, 409, "groups differ in model or slot count");
    return;
  }
  this->defer([to_group, from_group]() { to_group->copy_program_from(*from_group); });
  this->send_ok(request);
}

// Send to lights on or off, like the switch entity (which exists even without the web_server component).
void KLinkWeb::handle_writes(AsyncWebServerRequest *request) {
  bool on;
  if (!this->read_bool_arg(request, "on", on))
    return;
  KLinkHub *hub = this->hub_;
  this->defer([hub, on]() { hub->set_writes_from_ui(on); });
  this->send_ok(request);
}

void KLinkWeb::handle_retry(AsyncWebServerRequest *request) {
  KLinkHub *hub = this->hub_;
  this->defer([hub]() { hub->retry(); });
  this->send_ok(request);
}

void KLinkWeb::handle_scan(AsyncWebServerRequest *request) {
  KLinkHub *hub = this->hub_;
  this->defer([hub]() { hub->start_scan(); });
  this->send_ok(request);
}

static void append_hex_rgb(std::string &out, const uint8_t rgb[3]) {
  static const char HEX[] = "0123456789abcdef";
  for (size_t c = 0; c < 3; c++) {
    out += HEX[rgb[c] >> 4];
    out += HEX[rgb[c] & 0x0f];
  }
}

// GET /klink/snap?gid=&rgb=RRGGBB: the closest settings this lamp can make for the target colour.
// Read-only: the page applies the answer itself through /klink/point.
void KLinkWeb::handle_snap(AsyncWebServerRequest *request) {
  uint32_t gid;
  if (!this->read_program_gid(request, "gid", gid))
    return;
  ProgramHost *group = this->find_group(gid);
  if (group == nullptr) {
    send_error(request, 404, "unknown group");
    return;
  }

  const kk::ColorSpec *spec = nullptr;
  const std::string model = group->model_name();
  if (model == "tuna_sun")
    spec = &COLOR_SUN;
  else if (model == "tuna_blue")
    spec = &COLOR_BLUE;
  if (spec == nullptr) {
    send_error(request, 400, "no colour model for this group");
    return;
  }

  if (!request->hasArg("rgb")) {
    send_error(request, 400, "missing parameter: rgb");
    return;
  }
  uint8_t target[3];
  if (!parse_hex_rgb(arg_string(request, "rgb"), target)) {
    send_error(request, 400, "invalid parameter: rgb");
    return;
  }

  kk::SnapResult result = kk::nearest_i(*spec, target);

  const kk::SlotLabels *labels = group->slot_labels();
  std::string out;
  out.reserve(160);
  out += "{\"set\":{\"clr\":";
  out += std::to_string(result.clr);
  for (uint8_t o = 0; o < spec->n_overlays; o++) {
    uint8_t channel = spec->overlay_channel[o];
    const char *label = labels->name[channel];
    if (label == nullptr)
      continue;
    out += ',';
    append_quoted(out, label);
    out += ':';
    out += std::to_string(result.overlays[o]);
  }
  out += "},\"rgb\":\"";
  append_hex_rgb(out, result.rgb);
  out += "\",\"target\":\"";
  append_hex_rgb(out, target);
  out += "\",\"exact\":";
  out += result.dist == 0 ? "true" : "false";
  out += '}';
  send_json(request, 200, out);
}

}  // namespace klink
}  // namespace esphome

#endif  // USE_KLINK_WEB
