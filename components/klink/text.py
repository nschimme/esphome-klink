import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text
from esphome.const import CONF_INITIAL_VALUE

from . import klink_ns
from .light import ProgramHost

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"
CONF_SLOT = "slot"

KLinkSlotText = klink_ns.class_("KLinkSlotText", text.Text, cg.Component)

CONFIG_SCHEMA = (
    text.text_schema(KLinkSlotText, entity_category="config", mode="TEXT")
    .extend(
        {
            # `output_id` of the klink light this slot belongs to.
            cv.Required(CONF_GROUP): cv.use_id(ProgramHost),
            cv.Required(CONF_SLOT): cv.int_range(1, 16),
            cv.Optional(CONF_INITIAL_VALUE, default="off"): cv.string,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await text.new_text(config, min_length=0, max_length=63)
    await cg.register_component(var, config)
    group = await cg.get_variable(config[CONF_GROUP])
    cg.add(var.set_group(group))
    cg.add(var.set_index(config[CONF_SLOT] - 1))
    cg.add(var.set_initial_value(config[CONF_INITIAL_VALUE]))
