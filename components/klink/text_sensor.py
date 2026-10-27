import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_TYPE

from . import CONF_KLINK_ID, KLinkHub, klink_ns
from .light import ProgramHost

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"

KLinkStateSensor = klink_ns.class_("KLinkStateSensor", text_sensor.TextSensor, cg.PollingComponent)
KLinkGroupStatus = klink_ns.class_("KLinkGroupStatus", text_sensor.TextSensor, cg.PollingComponent)

# One line describing what the hub is doing with the chain: controlling, claiming, paused by a
# foreign write, writes off, or following as a follower.
HUB_SCHEMA = (
    text_sensor.text_sensor_schema(KLinkStateSensor)
    .extend({cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub)})
    .extend(cv.polling_component_schema("2s"))
)

# One line describing what a group is doing right now: the running program, Manual, Off, a preview,
# or the active effect.
STATUS_SCHEMA = (
    text_sensor.text_sensor_schema(KLinkGroupStatus)
    .extend({cv.Required(CONF_GROUP): cv.use_id(ProgramHost)})
    .extend(cv.polling_component_schema("1s"))
)

CONFIG_SCHEMA = cv.typed_schema(
    {"hub": HUB_SCHEMA, "status": STATUS_SCHEMA},
    default_type="hub",
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    if config[CONF_TYPE] == "status":
        cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
    else:
        cg.add(var.set_hub(await cg.get_variable(config[CONF_KLINK_ID])))
