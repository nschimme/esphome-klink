import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.core import CORE

from . import klink_ns
from .light import KLinkGroup

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"

KLinkPointSelect = klink_ns.class_("KLinkPointSelect", select.Select, cg.Component)

# Which program point the point editor entities of a group (time, enabled, the channel numbers) show and
# edit, like the dots at the bottom of Kessil's own Quick Set screen. Options are "1" to the group's `slots`.
CONFIG_SCHEMA = (
    select.select_schema(KLinkPointSelect, entity_category="config", icon="mdi:chart-timeline-variant")
    .extend({cv.Required(CONF_GROUP): cv.use_id(KLinkGroup)})
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    group = await cg.get_variable(config[CONF_GROUP])
    # The slot count is a plain config value on the light; read it back from its config.
    slots = 8
    for light_conf in CORE.config.get("light", []):
        if light_conf.get("output_id") == config[CONF_GROUP]:
            slots = light_conf.get("slots", 8)
    options = [str(i) for i in range(1, slots + 1)]
    var = await select.new_select(config, options=options)
    await cg.register_component(var, config)
    cg.add(var.set_group(group))
