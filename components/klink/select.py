import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_TYPE
from esphome.core import CORE

from . import klink_ns
from .light import ProgramHost

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"

KLinkPointSelect = klink_ns.class_("KLinkPointSelect", select.Select, cg.Component)
KLinkModeSelect = klink_ns.class_("KLinkModeSelect", select.Select, cg.Component)

# Option order matches the kessil::klink::Mode enum values: PROGRAM, MANUAL, OFF.
MODE_OPTIONS = ["Program", "Manual", "Off"]

# Which program point the point editor entities of a group (time, enabled, the channel numbers) show and
# edit, like the dots at the bottom of Kessil's own Quick Set screen. Options are "1" to the group's `slots`.
POINT_SCHEMA = (
    select.select_schema(KLinkPointSelect, entity_category="config", icon="mdi:chart-timeline-variant")
    .extend({cv.Required(CONF_GROUP): cv.use_id(ProgramHost)})
    .extend(cv.COMPONENT_SCHEMA)
)

# The group's operating mode: Program (runs the daily program), Manual (holds what you set) or Off.
MODE_SCHEMA = (
    select.select_schema(KLinkModeSelect, icon="mdi:state-machine")
    .extend({cv.Required(CONF_GROUP): cv.use_id(ProgramHost)})
    .extend(cv.COMPONENT_SCHEMA)
)

CONFIG_SCHEMA = cv.typed_schema(
    {"point": POINT_SCHEMA, "mode": MODE_SCHEMA},
    default_type="point",
)


async def to_code(config):
    group = await cg.get_variable(config[CONF_GROUP])
    if config[CONF_TYPE] == "mode":
        var = await select.new_select(config, options=MODE_OPTIONS)
        await cg.register_component(var, config)
        cg.add(var.set_group(group))
        return
    # The slot count is a plain config value on the light; read it back from its config.
    slots = 8
    for light_conf in CORE.config.get("light", []):
        if light_conf.get("output_id") == config[CONF_GROUP]:
            slots = light_conf.get("slots", 8)
    options = [str(i) for i in range(1, slots + 1)]
    var = await select.new_select(config, options=options)
    await cg.register_component(var, config)
    cg.add(var.set_group(group))
