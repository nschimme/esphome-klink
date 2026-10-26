import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number

from . import klink_ns
from .light import ProgramHost

DEPENDENCIES = ["klink"]

CONF_GROUP = "group"
CONF_CHANNEL = "channel"
CONF_POINT_CHANNEL = "point_channel"

KLinkChannelNumber = klink_ns.class_("KLinkChannelNumber", number.Number, cg.Component)

# A control for one of a group's channels: UI3-UI5 in percent, and UI2 (0-255) on models that have no
# color temperature. On a Tuna Sun UI4 is red and UI5 is amber (UI3 is unused); other models use them for
# other colors. Giving a value takes manual control of the
# group, like the light, until "resume schedule".
def _one_of_channel(config):
    if (CONF_CHANNEL in config) == (CONF_POINT_CHANNEL in config):
        raise cv.Invalid("give exactly one of `channel` (manual level) or `point_channel` (point editor)")
    return config


CONFIG_SCHEMA = cv.All(
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


async def to_code(config):
    point = CONF_POINT_CHANNEL in config
    ui = config[CONF_POINT_CHANNEL] if point else config[CONF_CHANNEL]
    top = 255 if ui <= 2 and (point or ui == 2) else 100  # UI1 and UI2 are bytes, UI3-UI5 percent
    var = await number.new_number(config, min_value=0, max_value=top, step=1)
    await cg.register_component(var, config)
    cg.add(var.set_group(await cg.get_variable(config[CONF_GROUP])))
    if point:
        cg.add(var.set_point_field(ui - 1))
    else:
        cg.add(var.set_channel(ui))
