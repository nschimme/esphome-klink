import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE

from . import CONF_KLINK_ID, KLinkHub, klink_ns

DEPENDENCIES = ["klink"]

KLinkButton = klink_ns.class_("KLinkButton", button.Button, cg.Component)
ButtonKind = klink_ns.enum("ButtonKind", is_class=True)
KINDS = {
    "scan": ButtonKind.SCAN,
    "identify_next": ButtonKind.IDENTIFY_NEXT,
    "retry": ButtonKind.RETRY,
}

CONFIG_SCHEMA = (
    button.button_schema(KLinkButton, entity_category="config")
    .extend(
        {
            cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub),
            cv.Required(CONF_TYPE): cv.one_of(*KINDS, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    cg.add(var.set_kind(KINDS[config[CONF_TYPE]]))
    cg.add(var.set_hub(await cg.get_variable(config[CONF_KLINK_ID])))
