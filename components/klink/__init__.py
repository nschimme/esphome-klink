import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light, time, uart, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_TIME_ID,
    CONF_TRIGGER_ID,
    CONF_UPDATE_INTERVAL,
)

try:
    from . import klink_colors, web_build
except ImportError:  # loaded as a top-level module (tests, scripts)
    import klink_colors
    import web_build

CODEOWNERS = []
DEPENDENCIES = ["uart"]


def AUTO_LOAD(config):
    # The C++ classes for the platforms live in the hub, so their base components must be compiled in.
    loads = ["light", "text", "switch", "button", "sensor", "text_sensor", "number", "time", "select", "datetime"]
    # The web page and JSON API share the web_server_base HTTP server; only pull it in when asked.
    if config[CONF_WEB]:
        loads.append("web_server_base")
    return loads


CONF_KLINK_ID = "klink_id"
CONF_KLINK_WEB_ID = "klink_web_id"
CONF_WRITES_ENABLED = "writes_enabled"
CONF_MONITOR = "monitor"
CONF_AUTO_WRITES = "auto_writes"
CONF_GUARD_QUIET_TIME = "guard_quiet_time"
CONF_DIRECT_DRIVE = "direct_drive"
CONF_POSITIVE_PIN = "positive_pin"
CONF_NEGATIVE_PIN = "negative_pin"
CONF_ON_LAMP_FOUND = "on_lamp_found"
CONF_ON_SCAN_DONE = "on_scan_done"
CONF_KID = "kid"
CONF_GID = "gid"
CONF_WEB = "web"
CONF_PROGRAMS = "programs"
CONF_LIGHTS = "lights"
CONF_MODEL = "model"
CONF_SLOTS = "slots"
CONF_TRANSITION = "transition"

klink_ns = cg.esphome_ns.namespace("klink")
KLinkHub = klink_ns.class_("KLinkHub", cg.Component, uart.UARTDevice)
KLinkWeb = klink_ns.class_("KLinkWeb", cg.Component)

# The interface every program host implements (KLinkGroup on the bus, KLinkLightProgram on ordinary
# lights). Declared here so both light.py and the hub schema can name it without a circular import.
ProgramHost = klink_ns.class_("ProgramHost")
KLinkLightProgram = klink_ns.class_("KLinkLightProgram", ProgramHost, cg.Component)
LightModel = klink_ns.enum("LightModel", is_class=True)

# The entity shape is fixed at config time, so the model is explicit rather than derived from the
# lights (a light may later lack one of the channels; that channel is then ignored for it).
PROGRAM_MODELS = {
    "brightness": LightModel.BRIGHTNESS,
    "ct": LightModel.CT,
    "rgb": LightModel.RGB,
    "rgbw": LightModel.RGBW,
}
MAX_PROGRAMS = 64  # program ids 101..164; see klink_light_map.h

PROGRAM_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(KLinkLightProgram),
            cv.Required(CONF_NAME): cv.string,
            # One or more existing ESPHome lights, driven together with the same values.
            cv.Required(CONF_LIGHTS): cv.All(
                cv.ensure_list(cv.use_id(light.LightState)), cv.Length(min=1)
            ),
            cv.Required(CONF_MODEL): cv.one_of(*PROGRAM_MODELS, lower=True),
            cv.Optional(CONF_SLOTS, default=8): cv.int_range(1, 16),
            # How often an unchanged program value is re-asserted (0 disables the refresh).
            cv.Optional(CONF_UPDATE_INTERVAL, default="5s"): cv.positive_time_period_milliseconds,
            # Defaults to the update interval (in to_code); previews use a short transition anyway.
            cv.Optional(CONF_TRANSITION): cv.positive_time_period_milliseconds,
            cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

LampFoundTrigger = klink_ns.class_(
    "LampFoundTrigger", automation.Trigger.template(cg.uint32, cg.uint8, cg.uint8)
)
ScanDoneTrigger = klink_ns.class_(
    "ScanDoneTrigger", automation.Trigger.template(cg.bool_, cg.uint32)
)
ScanAction = klink_ns.class_("ScanAction", automation.Action)
IdentifyAction = klink_ns.class_("IdentifyAction", automation.Action)
StageAssignmentAction = klink_ns.class_("StageAssignmentAction", automation.Action)
ConfirmAssignmentAction = klink_ns.class_("ConfirmAssignmentAction", automation.Action)


def _add_web_objects(config):
    if not config[CONF_WEB]:
        return config
    # Only ask for these IDs when the web API is on, so a build without it needs no web server at
    # all. Both are auto-IDs: the K-Link web handler is declared here, and the web server base
    # instance is resolved to the single one web_server_base declares.
    config = dict(config)
    config[CONF_KLINK_WEB_ID] = cv.declare_id(KLinkWeb)(None)
    config[CONF_WEB_SERVER_BASE_ID] = cv.use_id(web_server_base.WebServerBase)(None)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(KLinkHub),
            # Serve the esphome-klink page and JSON API at /klink on the shared HTTP server.
            cv.Optional(CONF_WEB, default=True): cv.boolean,
            # Dry run by default: frames are logged, nothing is transmitted.
            cv.Optional(CONF_WRITES_ENABLED, default=False): cv.boolean,
            # Listen for and log every frame on the bus (other controllers' traffic, lamp replies).
            cv.Optional(CONF_MONITOR, default=False): cv.boolean,
            # Take over the bus by itself once it has been quiet (no switch to flip). While another
            # controller is heard, only shadow it. Overrides writes_enabled.
            cv.Optional(CONF_AUTO_WRITES, default=False): cv.boolean,
            # Writes wait until the bus has been free of other controllers this long, and stop at
            # once if another controller is heard.
            cv.Optional(
                CONF_GUARD_QUIET_TIME, default="15s"
            ): cv.positive_time_period_milliseconds,
            # No transceiver: drive the bus straight from two ESP32 pads (D+ and D-, complementary).
            # Plain pin numbers: the uart already owns these pads. The pads can be asked for more current than a GPIO allows if the lamps terminate the
            # pair; see the README. The uart tx_pin must be the negative pin.
            cv.Optional(CONF_DIRECT_DRIVE): cv.Schema(
                {
                    cv.Required(CONF_POSITIVE_PIN): cv.int_range(0, 48),
                    cv.Required(CONF_NEGATIVE_PIN): cv.int_range(0, 48),
                }
            ),
            cv.Optional(CONF_ON_LAMP_FOUND): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(LampFoundTrigger)}
            ),
            cv.Optional(CONF_ON_SCAN_DONE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ScanDoneTrigger)}
            ),
            # Programs that drive ordinary ESPHome lights directly (no K-Link bus). Entities attach
            # with `group: <program id>`, just like a bus group.
            # No default: a hub without programs keeps an unchanged config dump.
            cv.Optional(CONF_PROGRAMS): cv.All(
                cv.ensure_list(PROGRAM_SCHEMA), cv.Length(max=MAX_PROGRAMS)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    _add_web_objects,
)


def _final_validate(config):
    # RX is required. TX is optional: a receive-only monitor never transmits.
    return uart.final_validate_device_schema(
        "klink", baud_rate=None, require_rx=True, require_tx=False
    )(config)


FINAL_VALIDATE_SCHEMA = _final_validate


def _add_web_page():
    """Embed the gzipped page as a global byte array in the generated main.cpp.

    The globals are emitted at file scope after esphome.h, so the statement is self-contained
    (its own namespace and no dependence on a using-directive). PROGMEM comes from
    esphome/core/hal.h, which esphome.h includes for every build. The `const` definition gets
    external linkage because klink_web.h (included by esphome.h) declares the symbols `extern`
    earlier in the same translation unit.
    """
    payload = web_build.page_gzip()
    if len(payload) > web_build.MAX_GZIP_BYTES:
        raise cv.Invalid(
            "esphome-klink page is %d bytes gzipped (limit %d); trim components/klink/web/"
            % (len(payload), web_build.MAX_GZIP_BYTES)
        )
    rows = []
    for offset in range(0, len(payload), 16):
        rows.append(
            "  " + " ".join("0x%02x," % byte for byte in payload[offset:offset + 16])
        )
    text = (
        "namespace esphome { namespace klink {\n"
        "const uint8_t KLINK_PAGE[] PROGMEM = {\n"
        + "\n".join(rows)
        + "\n};\n"
        "const size_t KLINK_PAGE_LEN = sizeof(KLINK_PAGE);\n"
        "}}  // namespace esphome::klink\n"
    )
    cg.add_global(cg.RawStatement(text))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_writes_enabled(config[CONF_WRITES_ENABLED]))
    cg.add(var.set_monitor(config[CONF_MONITOR]))
    cg.add(var.set_auto_writes(config[CONF_AUTO_WRITES]))
    cg.add(var.set_guard_quiet_ms(config[CONF_GUARD_QUIET_TIME]))
    if CONF_DIRECT_DRIVE in config:
        dd = config[CONF_DIRECT_DRIVE]
        cg.add(var.set_direct_drive(dd[CONF_POSITIVE_PIN], dd[CONF_NEGATIVE_PIN]))

    # Programs use no bus but still register with the hub (for the web API and the shared entities).
    for index, prog in enumerate(config.get(CONF_PROGRAMS, [])):
        pvar = cg.new_Pvariable(prog[CONF_ID])
        cg.add(pvar.set_name(prog[CONF_NAME]))
        cg.add(pvar.set_program_id(101 + index))
        cg.add(pvar.set_yaml_id(str(prog[CONF_ID])))
        cg.add(pvar.set_hub(var))
        cg.add(pvar.set_model(PROGRAM_MODELS[prog[CONF_MODEL]]))
        cg.add(pvar.set_num_slots(prog[CONF_SLOTS]))
        cg.add(pvar.set_update_ms(prog[CONF_UPDATE_INTERVAL]))
        cg.add(
            pvar.set_transition_ms(
                prog.get(CONF_TRANSITION, prog[CONF_UPDATE_INTERVAL])
            )
        )
        cg.add(pvar.set_time(await cg.get_variable(prog[CONF_TIME_ID])))
        for light_id in prog[CONF_LIGHTS]:
            cg.add(pvar.add_light(await cg.get_variable(light_id)))
        # `update_interval` here is the program's refresh interval, not a PollingComponent cadence:
        # keep it away from register_component, which would emit the PollingComponent-only
        # set_update_interval().
        component_conf = dict(prog)
        component_conf.pop(CONF_UPDATE_INTERVAL, None)
        await cg.register_component(pvar, component_conf)

    if config[CONF_WEB]:
        cg.add_define("USE_KLINK_WEB")
        _add_web_page()
        # The colour tables for /klink/snap. klink_web.h declares COLOR_SUN/COLOR_BLUE extern
        # before this global, so the const definitions below get external linkage.
        cg.add_global(cg.RawStatement(klink_colors.emit_cpp(firmware=True)))
        base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
        web = cg.new_Pvariable(config[CONF_KLINK_WEB_ID], base)
        await cg.register_component(web, config)
        cg.add(web.set_hub(var))

    for conf in config.get(CONF_ON_LAMP_FOUND, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.uint32, "kid"), (cg.uint8, "gid"), (cg.uint8, "type")], conf
        )
    for conf in config.get(CONF_ON_SCAN_DONE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.bool_, "complete"), (cg.uint32, "found")], conf
        )


HUB_ACTION = cv.Schema({cv.GenerateID(): cv.use_id(KLinkHub)})
IDENTIFY_SCHEMA = HUB_ACTION.extend({cv.Required(CONF_KID): cv.templatable(cv.uint32_t)})
ASSIGN_SCHEMA = HUB_ACTION.extend(
    {
        cv.Required(CONF_KID): cv.templatable(cv.uint32_t),
        cv.Required(CONF_GID): cv.templatable(cv.int_range(1, 40)),
    }
)


@automation.register_action("klink.scan", ScanAction, HUB_ACTION, synchronous=True)
@automation.register_action(
    "klink.confirm_assignment", ConfirmAssignmentAction, HUB_ACTION, synchronous=True
)
async def hub_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "klink.identify", IdentifyAction, IDENTIFY_SCHEMA, synchronous=True
)
async def identify_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    cg.add(var.set_kid(await cg.templatable(config[CONF_KID], args, cg.uint32)))
    return var


@automation.register_action(
    "klink.stage_assignment", StageAssignmentAction, ASSIGN_SCHEMA, synchronous=True
)
async def stage_assignment_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    cg.add(var.set_kid(await cg.templatable(config[CONF_KID], args, cg.uint32)))
    cg.add(var.set_gid(await cg.templatable(config[CONF_GID], args, cg.uint8)))
    return var
