import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE

from . import CONF_KLINK_ID, KLinkHub, klink_ns
from .light import KLinkGroup

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"

KLinkButton = klink_ns.class_("KLinkButton", button.Button, cg.Component)
ButtonKind = klink_ns.enum("ButtonKind", is_class=True)
KINDS = {
    "scan": ButtonKind.SCAN,
    "identify_next": ButtonKind.IDENTIFY_NEXT,
    "resume_schedule": ButtonKind.RESUME_SCHEDULE,
}


def _check(config):
    if config[CONF_TYPE] == "resume_schedule" and CONF_GROUP not in config:
        raise cv.Invalid("type 'resume_schedule' needs a `group`")
    return config


CONFIG_SCHEMA = cv.All(
    button.button_schema(KLinkButton, entity_category="config")
    .extend(
        {
            cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub),
            cv.Required(CONF_TYPE): cv.one_of(*KINDS, lower=True),
            cv.Optional(CONF_GROUP): cv.use_id(KLinkGroup),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _check,
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    cg.add(var.set_kind(KINDS[config[CONF_TYPE]]))
    cg.add(var.set_hub(await cg.get_variable(config[CONF_KLINK_ID])))
    if CONF_GROUP in config:
        cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
