import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_TYPE
from esphome.core import CORE

from . import CONF_KLINK_ID, KLinkHub, klink_ns
from .light import ProgramHost

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"
CONF_CHANNEL = "channel"
CONF_POINT_CHANNEL = "point_channel"

KLinkChannelNumber = klink_ns.class_("KLinkChannelNumber", number.Number, cg.Component)
KLinkTankNumber = klink_ns.class_("KLinkTankNumber", number.Number, cg.Component)
TankNumberKind = klink_ns.enum("TankNumberKind", is_class=True)
KLinkCloudDimmingNumber = klink_ns.class_("KLinkCloudDimmingNumber", number.Number, cg.Component)
KLinkLocationNumber = klink_ns.class_("KLinkLocationNumber", number.Number, cg.Component)
KLinkEffectDurationNumber = klink_ns.class_(
    "KLinkEffectDurationNumber", number.Number, cg.Component
)
KLinkFadeTimeNumber = klink_ns.class_(
    "KLinkFadeTimeNumber", number.Number, cg.Component
)

# A control for one of a group's channels: UI3-UI5 in percent, and UI2 (0-255) on models that have no
# color temperature. On a Tuna Sun UI4 is red and UI5 is amber (UI3 is unused); other models use them for
# other colors. Giving a value takes manual control of the
# group, like the light, until "resume schedule".
def _one_of_channel(config):
    if (CONF_CHANNEL in config) == (CONF_POINT_CHANNEL in config):
        raise cv.Invalid("give exactly one of `channel` (manual level) or `point_channel` (point editor)")
    return config


CHANNEL_SCHEMA = cv.All(
    number.number_schema(KLinkChannelNumber, unit_of_measurement="%", icon="mdi:palette")
    .extend(
        {
            cv.Required(CONF_GROUP): cv.use_id(ProgramHost),
            # 2 is only meaningful for models without a color temperature (0-255); 3-5 are 0-100%
            cv.Optional(CONF_CHANNEL): cv.int_range(2, 5),
            # Point editor: instead of a manual level, edit this channel (1-5 = UI1-UI5) of the program
            # point chosen with the group's point select.
            cv.Optional(CONF_POINT_CHANNEL): cv.int_range(1, 5),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _one_of_channel,
)

# How long effects run, in minutes: 0 to 5 in half-minute steps,
# 0 meaning "until stopped".
EFFECT_DURATION_TYPE = "effect_duration"

EFFECT_DURATION_SCHEMA = (
    number.number_schema(
        KLinkEffectDurationNumber,
        unit_of_measurement="min",
        icon="mdi:timer-outline",
        entity_category="config",
    )
    .extend(
        {
            cv.Required(CONF_TYPE): cv.one_of(EFFECT_DURATION_TYPE, lower=True),
            cv.Required(CONF_GROUP): cv.use_id(ProgramHost),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


# How long jumps take to fade, in seconds: 0 to 10 in tenths, 0 meaning immediate.
FADE_TIME_TYPE = "fade_time"

FADE_TIME_SCHEMA = (
    number.number_schema(
        KLinkFadeTimeNumber,
        unit_of_measurement="s",
        icon="mdi:timer-sand",
        entity_category="config",
    )
    .extend(
        {
            cv.Required(CONF_TYPE): cv.one_of(FADE_TIME_TYPE, lower=True),
            cv.Required(CONF_GROUP): cv.use_id(ProgramHost),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

TYPED_KINDS = (EFFECT_DURATION_TYPE, FADE_TIME_TYPE)


# The device-wide location for the sun and moon programs: a value entered here wins over the one imported
# from Home Assistant (zone.home) and the YAML default.
LOCATION_TYPES = {"latitude": (0, -90, 90), "longitude": (1, -180, 180)}

LOCATION_SCHEMA = (
    number.number_schema(
        KLinkLocationNumber,
        unit_of_measurement="°",
        icon="mdi:map-marker",
        entity_category="config",
    )
    .extend(
        {
            cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub),
            cv.Required(CONF_TYPE): cv.one_of(*LOCATION_TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


# Acclimation ramp (start share, days) and the soft start after a power loss (seconds), per program.
TANK_TYPES = {
    "acclimation_start": (TankNumberKind.ACC_START, 10, 90, 5, "%", "mdi:sprout"),
    "acclimation_days": (TankNumberKind.ACC_DAYS, 1, 90, 1, "d", "mdi:calendar-range"),
    "soft_start": (TankNumberKind.SOFT_START, 0, 120, 5, "s", "mdi:lightbulb-on-outline"),
}

TANK_SCHEMA = (
    number.number_schema(KLinkTankNumber, entity_category="config")
    .extend(
        {
            cv.Required(CONF_GROUP): cv.use_id(ProgramHost),
            cv.Required(CONF_TYPE): cv.one_of(*TANK_TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

# The most the weather may dim the sun program when it is fully overcast.
CLOUD_DIMMING_TYPE = "cloud_dimming"

CLOUD_DIMMING_SCHEMA = (
    number.number_schema(
        KLinkCloudDimmingNumber,
        unit_of_measurement="%",
        icon="mdi:weather-cloudy",
        entity_category="config",
    )
    .extend(
        {
            cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub),
            cv.Required(CONF_TYPE): cv.one_of(CLOUD_DIMMING_TYPE, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


def _config_schema(config):
    if config.get(CONF_TYPE) in TANK_TYPES:
        return TANK_SCHEMA(config)
    if config.get(CONF_TYPE) == CLOUD_DIMMING_TYPE:
        return CLOUD_DIMMING_SCHEMA(config)
    if config.get(CONF_TYPE) in LOCATION_TYPES:
        return LOCATION_SCHEMA(config)
    # Channel numbers keep their old config shape (no `type:` key); only the effect duration and fade
    # time numbers are typed. Adding a default type through cv.typed_schema would add `type: channel`
    # to every existing channel entity in `esphome config`.
    if config.get(CONF_TYPE) == EFFECT_DURATION_TYPE:
        return EFFECT_DURATION_SCHEMA(config)
    if config.get(CONF_TYPE) == FADE_TIME_TYPE:
        return FADE_TIME_SCHEMA(config)
    if CONF_TYPE in config:
        raise cv.Invalid(f"type must be one of {', '.join(TYPED_KINDS)}, latitude, longitude")
    return CHANNEL_SCHEMA(config)


CONFIG_SCHEMA = cv.All(_config_schema)


def _group_model(group_id):
    """The `model` of the klink light with this id, or None (a light program has none)."""
    for light_conf in CORE.config.get("light", []):
        if light_conf.get("output_id") == group_id:
            return light_conf.get("model")
    return None


async def to_code(config):
    if config.get(CONF_TYPE) in TANK_TYPES:
        kind, low, high, step, unit, icon = TANK_TYPES[config[CONF_TYPE]]
        var = await number.new_number(config, min_value=low, max_value=high, step=step)
        await cg.register_component(var, config)
        cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
        cg.add(var.set_kind(kind))
        return
    if config.get(CONF_TYPE) == CLOUD_DIMMING_TYPE:
        var = await number.new_number(config, min_value=0, max_value=90, step=5)
        await cg.register_component(var, config)
        cg.add(var.set_hub(await cg.get_variable(config[CONF_KLINK_ID])))
        return
    if config.get(CONF_TYPE) in LOCATION_TYPES:
        index, low, high = LOCATION_TYPES[config[CONF_TYPE]]
        var = await number.new_number(config, min_value=low, max_value=high, step=0.01)
        await cg.register_component(var, config)
        cg.add(var.set_hub(await cg.get_variable(config[CONF_KLINK_ID])))
        cg.add(var.set_index(index))
        return
    if config.get(CONF_TYPE) == EFFECT_DURATION_TYPE:
        var = await number.new_number(config, min_value=0, max_value=5, step=0.5)
        await cg.register_component(var, config)
        cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
        return
    if config.get(CONF_TYPE) == FADE_TIME_TYPE:
        var = await number.new_number(config, min_value=0, max_value=10, step=0.1)
        await cg.register_component(var, config)
        cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
        return
    point = CONF_POINT_CHANNEL in config
    ui = config[CONF_POINT_CHANNEL] if point else config[CONF_CHANNEL]
    top = 255 if ui <= 2 and (point or ui == 2) else 100  # UI1 and UI2 are bytes, UI3-UI5 percent
    if ui == 2 and _group_model(config[CONF_GROUP]) == "refugium":
        top = 240  # the lamp refuses a CLR above 240
    var = await number.new_number(config, min_value=0, max_value=top, step=1)
    await cg.register_component(var, config)
    cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
    if point:
        cg.add(var.set_point_field(ui - 1))
    else:
        cg.add(var.set_channel(ui))
