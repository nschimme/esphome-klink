import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import uart
from esphome.const import CONF_ID, CONF_TRIGGER_ID

CODEOWNERS = []
DEPENDENCIES = ["uart"]
# The C++ classes for the platforms live in the hub, so their base components must be compiled in.
AUTO_LOAD = ["light", "text", "switch", "button", "sensor", "text_sensor", "number", "time", "select", "datetime"]

CONF_KLINK_ID = "klink_id"
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

klink_ns = cg.esphome_ns.namespace("klink")
KLinkHub = klink_ns.class_("KLinkHub", cg.Component, uart.UARTDevice)

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

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(KLinkHub),
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
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


def _final_validate(config):
    # RX is required. TX is optional: a receive-only monitor never transmits.
    return uart.final_validate_device_schema(
        "klink", baud_rate=None, require_rx=True, require_tx=False
    )(config)


FINAL_VALIDATE_SCHEMA = _final_validate


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
