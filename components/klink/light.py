import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, time
from esphome.components.light.effects import MONOCHROMATIC_EFFECTS, validate_effects
from esphome.const import (
    CONF_EFFECTS,
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_GAMMA_CORRECT,
    CONF_OUTPUT_ID,
    CONF_TIME_ID,
)

from . import CONF_GID, CONF_KLINK_ID, KLinkHub, ProgramHost, klink_ns
from .effects import EFFECTS, KLinkEffect  # noqa: F401  (the effects register when the component loads)

DEPENDENCIES = ["klink"]

CONF_SLOTS = "slots"
CONF_MANUAL_COLOR = "manual_color"
CONF_REFRESH_INTERVAL = "refresh_interval"
CONF_INVERT_COLOR = "invert_color"
CONF_MODEL = "model"

# ProgramHost is declared in __init__.py so the hub schema can define KLinkLightProgram without a
# circular import; it stays importable from here for the platform modules.
KLinkGroup = klink_ns.class_("KLinkGroup", ProgramHost, light.LightOutput, cg.Component)
GroupModel = klink_ns.enum("GroupModel", is_class=True)
MODELS = {
    "tuna_sun": GroupModel.TUNA_SUN,  # tested: UI1 intensity, UI2 color temperature, UI4 red, UI5 amber
    "tuna_blue": GroupModel.TUNA_BLUE,  # untested on a lamp: UI3 violet, UI4 red, UI5 green
    "refugium": GroupModel.REFUGIUM,  # untested on a lamp: UI2 blue to red (0-240), UI4 white, UI5 green
}


CONFIG_SCHEMA = (
    light.LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(KLinkGroup),
            cv.GenerateID(CONF_KLINK_ID): cv.use_id(KLinkHub),
            cv.Required(CONF_GID): cv.int_range(1, 40),
            cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            # Number of schedule points; each is a `text` entity (platform: klink) of this group.
            cv.Optional(CONF_SLOTS, default=16): cv.int_range(1, 16),
            # Manual commands send brightness as UI1 and the color temperature as UI2 (0-255). The
            # Kelvin range of UI2 depends on the model (the Tuna Sun spans 6,000 K to 9,000 K, set in
            # common/tuna-sun.yaml); these generic defaults are placeholders. UI2 = 0 is the cold end unless invert_color is set (on a Tuna Sun UI2 = 0 is warm, so set it).
            cv.Optional(CONF_COLD_WHITE_COLOR_TEMPERATURE, default="6500 K"): cv.color_temperature,
            cv.Optional(CONF_WARM_WHITE_COLOR_TEMPERATURE, default="2700 K"): cv.color_temperature,
            cv.Optional(CONF_INVERT_COLOR, default=False): cv.boolean,
            # What the lamps are. Only the Tuna Sun has a color temperature control; the other models get a
            # brightness-only light and use `number` entities (channel 2-5) for the rest.
            cv.Optional(CONF_MODEL, default="tuna_sun"): cv.one_of(*MODELS, lower=True),
            # UI2 used when a light command carries no color temperature (a plain "turn on").
            cv.Optional(CONF_MANUAL_COLOR, default=127): cv.uint8_t,
            # Light effects: Home Assistant's own (pulse, flicker, ...) plus the klink_* ones above.
            cv.Optional(CONF_EFFECTS): validate_effects(MONOCHROMATIC_EFFECTS + [e[0] for e in EFFECTS]),
            # Resend the group's level about once a second so a lamp that missed a frame catches up.
            cv.Optional(
                CONF_REFRESH_INTERVAL, default="1s"
            ): cv.positive_time_period_milliseconds,
            # Brightness maps straight onto UI1, and transitions would flood the bus.
            cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
            cv.Optional(
                CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    hub = await cg.get_variable(config[CONF_KLINK_ID])
    cg.add(var.set_hub(hub))
    cg.add(var.set_gid(config[CONF_GID]))
    time_ = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(time_))
    cg.add(var.set_num_slots(config[CONF_SLOTS]))
    cg.add(var.set_model(MODELS[config[CONF_MODEL]]))
    cg.add(var.set_manual_color(config[CONF_MANUAL_COLOR]))
    cg.add(var.set_color_range(config[CONF_COLD_WHITE_COLOR_TEMPERATURE], config[CONF_WARM_WHITE_COLOR_TEMPERATURE], config[CONF_INVERT_COLOR]))
    cg.add(var.set_refresh_interval(config[CONF_REFRESH_INTERVAL]))
