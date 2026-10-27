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
KLinkEffectSpeedSelect = klink_ns.class_("KLinkEffectSpeedSelect", select.Select, cg.Component)
KLinkSourceSelect = klink_ns.class_("KLinkSourceSelect", select.Select, cg.Component)
KLinkCurveSelect = klink_ns.class_("KLinkCurveSelect", select.Select, cg.Component)

# Option order matches the kessil::klink::Mode enum values: PROGRAM, MANUAL, OFF.
MODE_OPTIONS = ["Program", "Manual", "Off"]
# Order matches the speed values KLinkEffect uses: 0 fast, 1 moderate, 2 slow.
EFFECT_SPEED_OPTIONS = ["Fast", "Moderate", "Slow"]
# Order matches BrightnessCurve: LIGHT, LINEAR, CIE.
BRIGHTNESS_CURVE_OPTIONS = ["Linear", "Even steps (CIE 1931)"]

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

# Speed for effects: Fast, Moderate or Slow.
EFFECT_SPEED_SCHEMA = (
    select.select_schema(
        KLinkEffectSpeedSelect, entity_category="config", icon="mdi:speedometer"
    )
    .extend({cv.Required(CONF_GROUP): cv.use_id(ProgramHost)})
    .extend(cv.COMPONENT_SCHEMA)
)

# How Home Assistant's brightness maps to the lamp: a straight line or CIE 1931.
BRIGHTNESS_CURVE_SCHEMA = (
    select.select_schema(
        KLinkCurveSelect, entity_category="config", icon="mdi:chart-bell-curve"
    )
    .extend({cv.Required(CONF_GROUP): cv.use_id(ProgramHost)})
    .extend(cv.COMPONENT_SCHEMA)
)

# Where a bus group's schedule comes from: its own points, or the Sky program (sun, moon, or both).
SOURCE_SCHEMA = (
    select.select_schema(KLinkSourceSelect, icon="mdi:weather-sunny")
    .extend({cv.Required(CONF_GROUP): cv.use_id(ProgramHost)})
    .extend(cv.COMPONENT_SCHEMA)
)

CONFIG_SCHEMA = cv.typed_schema(
    {
        "point": POINT_SCHEMA,
        "mode": MODE_SCHEMA,
        "effect_speed": EFFECT_SPEED_SCHEMA,
        "brightness_curve": BRIGHTNESS_CURVE_SCHEMA,
        "source": SOURCE_SCHEMA,
    },
    default_type="point",
)


async def to_code(config):
    group = await cg.get_variable(config[CONF_GROUP])
    if config[CONF_TYPE] == "mode":
        var = await select.new_select(config, options=MODE_OPTIONS)
        await cg.register_component(var, config)
        cg.add(var.set_group(group))
        return
    if config[CONF_TYPE] == "effect_speed":
        var = await select.new_select(config, options=EFFECT_SPEED_OPTIONS)
        await cg.register_component(var, config)
        cg.add(var.set_group(group))
        return
    if config[CONF_TYPE] == "source":
        # Home Assistant only picks the program; the Sun and Moon layers are set on the device page.
        var = await select.new_select(config, options=["My points", "Sky"])
        await cg.register_component(var, config)
        cg.add(var.set_group(group))
        return
    if config[CONF_TYPE] == "brightness_curve":
        var = await select.new_select(config, options=BRIGHTNESS_CURVE_OPTIONS)
        await cg.register_component(var, config)
        cg.add(var.set_group(group))
        return
    # The slot count is a plain config value on the host; read it back from its config. A bus group
    # is a `light:` entry with matching output_id; a KLinkLightProgram is a `klink: programs:` entry
    # with a matching id.
    slots = 16
    for light_conf in CORE.config.get("light", []):
        if light_conf.get("output_id") == config[CONF_GROUP]:
            slots = light_conf.get("slots", 16)
    for program_conf in (CORE.config.get("klink") or {}).get("programs", []) or []:
        if program_conf.get("id") == config[CONF_GROUP]:
            slots = program_conf.get("slots", 16)
    options = [str(i) for i in range(1, slots + 1)]
    var = await select.new_select(config, options=options)
    await cg.register_component(var, config)
    cg.add(var.set_group(group))
