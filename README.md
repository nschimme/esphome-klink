# esphome-klink

Local control of **Kessil X-Series aquarium lights** (A360X and A360XE Tuna Sun and Tuna Blue) from Home Assistant, or
from a web page the controller serves itself, using a cheap USB-C ESP32 board running ESPHome plugged into the lamp's
K-Link port. No cloud, no app. You get a daily light program you can edit on a
visual day curve, a sun that rises and sets at your location, a moon for the night, gentle ramps, an acclimation mode
for new tanks, and the lamps' storm, cloud and aurora effects. The same programs can also run on ordinary ESPHome
lights.

> ## Disclaimer
>
> This is an independent, unofficial, community project. It is **not affiliated with, endorsed by, or supported by
> Kessil**. "Kessil", "Tuna Sun", "Tuna Blue" and related names are trademarks of their respective owners and are used
> here only to say which products this software is meant to work with.
>
> **Use at your own risk.** This software talks to your lights over their control port in a way Kessil does not
> document or support. It could malfunction, behave unexpectedly, or void your warranty. Plugging a microcontroller
> board into a lamp's port can damage the board, the lamp, or both. Never leave lights running unattended on a
> schedule you have not verified, and keep your livestock's needs in mind: a wrong schedule can harm fish, corals and
> plants. You are responsible for anything you connect and run. The software is provided "as is", without warranty of
> any kind (see [LICENSE](LICENSE)).

## What it does

- **Daily program.** Up to 16 points of time, intensity, color and the extra LEDs (red and amber on a Tuna Sun,
  violet, red and green on a Tuna Blue), with the lamps moving in straight lines between them. Edit it by dragging
  points on a curve and see the change on the lamps as you go. The shipped programs are a 12-hour day with a
  two-hour plateau and a dark night.
- **Sun and moon.** Switch a program to **Sun** and it follows the real sunrise and sunset at your location, with a
  warm glow at the edges and a ramp that is gentler than the real sun's (or a fixed day length you choose). Switch it
  to **Moon** on a blue or RGB light and it lights the dark hours, brighter near a full moon. Preview any day of the
  year before it happens.
- **Weather.** Optionally let clouds dim the sun, and a thunderstorm start the Storm effect.
- **New tank?** **Acclimation** raises the whole program from a low share to full over days you choose.
- **Smooth.** Transitions ease in and out, and **soft start** fades the lamps in whenever the controller starts.
- **Effects.** Storm, Cloud, Aurora and, on a Tuna Blue, Rainbow and the moon colors, as temporary overlays on the
  program, plus the standard ESPHome effects.
- **Any lamp setup.** Several groups on one board, a mode per group (Program, Manual, Off), and the same programs on
  ordinary dimmable, tunable-white, RGB and RGBW ESPHome lights, so a night light can follow the moon.

## What is tested

| Item | Status |
|---|---|
| **Kessil A360X Tuna Sun**, two lamps in one group, from an ESP32-C6 | **Tested:** intensity, color, red, amber, finding lamps, the daily program, effects and transitions. |
| Sun, moon, weather, acclimation, soft start | Computed days are checked against an independent astronomy library (sun within about a minute, moon within about 15 minutes) and run on the device; soft start and a full acclimation run have not been observed on lamps. |
| Light programs on ordinary lights | **Tested** on the board's own RGB LED; other light types compile but are untested. |
| **Kessil A360XE Tuna Blue** | **Untested.** The channels follow Kessil's controller screen (violet, red, green), but the mapping to the lamp has not been confirmed. |
| ESP32-C6 | **Tested.** ESP32-S3, -C3 and -S2 build but have not been run on a lamp. |

Not supported:
- **The AP9X, the AP700 and older lights.** This project talks directly to X-Series fixtures through their K-Link
  port. Lights with no K-Link port (older non-X models) cannot be controlled, and the AP9X and AP700 are different
  products that it does not work through.
- The Kessil Refugium and other X-Series models (`model: generic` exposes the raw channels, untested).
- Kessil's Wi-Fi dongle or Controller X on the same bus at the same time: two controllers fight each other, so
  unplug them.

## Is this for you?

- You have an X-Series **Tuna Sun** (tested) or **Tuna Blue** (untested), on a freshwater planted or a reef tank.
  The AP9X, the AP700 and older lights without a K-Link port are not supported.
- You can flash an ESP32 with ESPHome, or are happy to follow a short guide to do it.
- **Home Assistant is optional.** The web page on the board works on its own. Home Assistant adds entities and
  automations, your home location for the sun and moon, and the weather; without it the sun and moon use your time
  zone's city as an approximate location, and there is no weather.
- You accept the hardware risk of plugging a bare board into a lamp's port (see Getting started).
- You will not run Kessil's Wi-Fi dongle or Controller X on the same lamps at the same time: two controllers on one
  bus fight each other, and this one stops when it hears the other.

## Quick start

1. Get an ESP32-C6 board with a USB-C port and a USB-C cable. It plugs into a spare K-Link port on a lamp.
2. In the ESPHome dashboard, import
   `github://nschimme/esphome-klink/kessil-tuna-sun-esp32-c6.yaml@main` (use `kessil-tuna-blue-...` for a Tuna Blue).
3. Power it from the lamp, turn on **Send to lights**, and open `http://<device>/klink`.

The full walk-through, including wiring risks, is in [Getting started](docs/getting-started.md). **Read the first-run
section before connecting the board to a lamp.**

## Help wanted

The Tuna Blue and the other boards are untested because the author has none of them. If you do:

- **Tuna Blue owners:** in Home Assistant, enable the manual **violet**, **red** and **green** numbers for the group.
  Put the group in Manual, set one number to 50 % at a time, and note which color the lamp shows. Then open an issue
  with what you saw. That confirms (or corrects) the channel mapping the Tuna Blue support rests on.
- **ESP32-S3, -C3 and -S2 owners:** tell us whether the board finds your lamps and holds a program.

## Documentation

Set up and use:

| Page | What is in it |
|---|---|
| [Getting started](docs/getting-started.md) | Hardware, install, first run, network and security, troubleshooting, FAQ |
| [Daily programs](docs/daily-programs.md) | Points, shipped programs, starting points for planted and reef tanks, sun and moon, location, weather, acclimation |
| [Home Assistant](docs/home-assistant.md) | Every entity, modes, effects, transitions, soft start |

Customize and integrate:

| Page | What is in it |
|---|---|
| [Web pages and JSON API](docs/web-pages.md) | The `/klink` page and the API behind it |
| [Light programs](docs/light-programs.md) | The daily program on any ESPHome light |
| [Configuration](docs/configuration.md) | Substitutions, options, several groups, templates |
| [Development](docs/development.md) | Layout, tests, the mock device, CI |

## License

[MIT](LICENSE).
