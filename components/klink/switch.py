import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE

from . import CONF_KLINK_ID, KLinkHub, klink_ns
from .light import ProgramHost

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"

KLinkSwitch = klink_ns.class_("KLinkSwitch", switch.Switch, cg.Component)
SwitchKind = klink_ns.enum("SwitchKind", is_class=True)
KINDS = {
    "writes": SwitchKind.WRITES,
    "commissioning": SwitchKind.COMMISSIONING,
    "point_enabled": SwitchKind.POINT_ENABLED,  # the point editor's on/off for the selected point
    "acclimation": SwitchKind.ACCLIMATION,  # the program's acclimation ramp, on or off
    "follow_weather": SwitchKind.FOLLOW_WEATHER,  # clouds dim the sun program
    "storms_from_weather": SwitchKind.STORMS_FROM_WEATHER,  # a thunderstorm starts the Storm effect
}
GROUP_KINDS = ("point_enabled", "acclimation")


def _check(config):
    has_group = CONF_GROUP in config
    if config[CONF_TYPE] in GROUP_KINDS and not has_group:
        raise cv.Invalid(f"type '{config[CONF_TYPE]}' needs a `group`")
    if config[CONF_TYPE] not in GROUP_KINDS and has_group:
        raise cv.Invalid(f"type '{config[CONF_TYPE]}' is hub-wide and takes no `group`")
    return config


CONFIG_SCHEMA = cv.All(
    switch.switch_schema(KLinkSwitch, entity_category="config")
    .extend(
        {
            cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub),
            cv.Required(CONF_TYPE): cv.one_of(*KINDS, lower=True),
            cv.Optional(CONF_GROUP): cv.use_id(ProgramHost),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _check,
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    cg.add(var.set_kind(KINDS[config[CONF_TYPE]]))
    cg.add(var.set_hub(await cg.get_variable(config[CONF_KLINK_ID])))
    if CONF_GROUP in config:
        cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
