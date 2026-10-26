import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import CONF_KLINK_ID, KLinkHub, klink_ns

DEPENDENCIES = ["klink"]

KLinkStateSensor = klink_ns.class_("KLinkStateSensor", text_sensor.TextSensor, cg.PollingComponent)

# One line describing what the hub is doing with the bus: controlling, shadowing another controller,
# waiting for a quiet bus, writes off, or a latched conflict.
CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(KLinkStateSensor)
    .extend({cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub)})
    .extend(cv.polling_component_schema("2s"))
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    cg.add(var.set_hub(await cg.get_variable(config[CONF_KLINK_ID])))
