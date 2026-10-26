import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_TYPE, STATE_CLASS_MEASUREMENT

from . import CONF_KLINK_ID, KLinkHub, klink_ns

DEPENDENCIES = ["klink"]

KLinkSensor = klink_ns.class_("KLinkSensor", sensor.Sensor, cg.PollingComponent)
SensorKind = klink_ns.enum("SensorKind", is_class=True)
KINDS = {
    "bus_timeouts": SensorKind.BUS_TIMEOUTS,
    "lamps": SensorKind.LAMPS,
    "frames": SensorKind.FRAMES,
    "bad_frames": SensorKind.BAD_FRAMES,
    "shadow_matches": SensorKind.SHADOW_MATCHES,
    "shadow_mismatches": SensorKind.SHADOW_MISMATCHES,
    "foreign_frames": SensorKind.FOREIGN_FRAMES,
    "other_controller": SensorKind.OTHER_CONTROLLER,
    "conflict_latched": SensorKind.CONFLICT_LATCHED,
}

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        KLinkSensor,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category="diagnostic",
    )
    .extend(
        {
            cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub),
            cv.Required(CONF_TYPE): cv.one_of(*KINDS, lower=True),
        }
    )
    .extend(cv.polling_component_schema("30s"))
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    cg.add(var.set_kind(KINDS[config[CONF_TYPE]]))
    cg.add(var.set_hub(await cg.get_variable(config[CONF_KLINK_ID])))
