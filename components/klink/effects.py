"""The klink_* light effects, registered when the klink component loads.

They run on K-Link groups (group writes) and on ordinary ESPHome lights (light calls, shown through what the
light can do), so the registration lives here and not in the klink light platform: a light program's lights
are configured with the stock `rgb`, `monochromatic` or `cwww` platforms.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.light.effects import (
    register_monochromatic_effect,
    register_rgb_effect,
    register_effect,
)
from esphome.components.light.types import LightEffect
from esphome.const import CONF_DURATION, CONF_NAME, CONF_SPEED

from . import klink_ns

KLinkEffect = klink_ns.class_("KLinkEffect", LightEffect)
FxKind = klink_ns.enum("FxKind", is_class=True)

SPEEDS = {"fast": 0, "moderate": 1, "slow": 2}

# (YAML key, default name, kind, registrar). The built-in effects take a speed (fast/moderate/slow) and a
# duration. Standard ESPHome effects (pulse, flicker, ...) are listed in YAML as usual.
#  - Storm, Cloud and Aurora move intensity and the color axis: every light except on/off-only ones.
#  - Rainbow needs red, green and blue: RGB lights.
#  - The Moon effects hold K-Link channels (UI3, UI4) that mean nothing on another kind of light: K-Link groups only.
EFFECTS = [
    ("klink_storm", "Storm", FxKind.STORM, register_monochromatic_effect),
    ("klink_cloud", "Cloud", FxKind.CLOUD, register_monochromatic_effect),
    ("klink_aurora", "Aurora", FxKind.AURORA, register_monochromatic_effect),
    ("klink_rainbow", "Rainbow", FxKind.RAINBOW, register_rgb_effect),
    ("klink_moon_red", "Moon Red", FxKind.MOON_RED, register_effect),  # drives UI3
    ("klink_moon_blue", "Moon Blue", FxKind.MOON_BLUE, register_effect),  # drives UI4
]


def _register_effect(key, default_name, kind, registrar):
    schema = {
        cv.Optional(CONF_SPEED, default="fast"): cv.one_of(*SPEEDS, lower=True),
        # How long it runs (0.5 to 5 minutes); 0s runs until stopped.
        cv.Optional(CONF_DURATION, default="2min"): cv.positive_time_period_milliseconds,
    }

    @registrar(key, KLinkEffect, default_name, schema)
    async def effect_to_code(config, effect_id):
        var = cg.new_Pvariable(effect_id, config[CONF_NAME])
        cg.add(var.set_kind(kind))
        cg.add(var.set_duration(config[CONF_DURATION]))
        cg.add(var.set_speed(SPEEDS[config[CONF_SPEED]]))
        return var

    return effect_to_code


for _key, _name, _kind, _registrar in EFFECTS:
    _register_effect(_key, _name, _kind, _registrar)
