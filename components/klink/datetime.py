import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import datetime

from . import klink_ns
from .light import KLinkGroup

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"

KLinkPointTime = klink_ns.class_("KLinkPointTime", datetime.TimeEntity, cg.Component)

# The time of day of the program point chosen with the group's point select.
CONFIG_SCHEMA = (
    datetime.time_schema(KLinkPointTime)
    .extend({cv.Required(CONF_GROUP): cv.use_id(KLinkGroup)})
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await datetime.new_datetime(config)
    await cg.register_component(var, config)
    cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
