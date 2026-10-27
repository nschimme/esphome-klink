# esphome-klink

**Local control for Kessil X-Series aquarium lights.** A cheap USB-C ESP32 board running ESPHome plugs into the
lamp's K-Link port and gives you a daily light program you edit on a curve, a real sun and moon for your location,
and Home Assistant control, or just its own web page. No cloud, no app. The same programs also run on ordinary
ESPHome lights, and several boards can share one chain. See all [features](#-features).

> ⚠️ **Use at your own risk.** This is an independent project, not affiliated with or supported by Kessil, and it talks to
> your lights in a way Kessil does not document. It can malfunction, damage the board or lamp, or void a warranty,
> and a wrong schedule can harm livestock. Read the [full disclaimer](#-disclaimer) before connecting anything.

## 🚀 Quick start

1. Get an ESP32-C6 board with a USB-C port and a USB-C cable. It plugs into a spare K-Link port on a lamp.
2. In the ESPHome dashboard, import
   `github://nschimme/esphome-klink/kessil-leader-esp32-c6.yaml@main`. Every leader file includes the Tuna Blue,
   Tuna Sun and Refugium default programs, so it works with any lamp mix.
3. Power it from the lamp, turn on **Send to lights**, and open `http://<device>/klink`.

The full walk-through, including wiring risks, is in [Getting started](docs/getting-started.md). **Read its first-run
section before connecting the board to a lamp.**

Try the web page without hardware: [live demo](https://nschimme.github.io/esphome-klink/).

## 🧭 What do you want to do?

| I want to… | Start with |
|---|---|
| Run a Kessil lamp | [Quick start](#-quick-start) |
| Build a refugium, sump, moonlight or planted-tank strip | [DIY lights](docs/diy-lights.md) |
| Put more than one board on a chain | [Leader and follower](docs/leader-follower.md) |
| See every Home Assistant entity | [Home Assistant](docs/home-assistant.md) |
| Understand a config option | [Configuration](docs/configuration.md) |

## ✨ Features

- 📈 **Daily program.** Up to 16 points of time, intensity, color and the extra LEDs (red and amber on a Tuna Sun,
  violet, red and green on a Tuna Blue), with the lamps moving in straight lines between them. Edit it by dragging
  points on a curve and see the change on the lamps as you go. The shipped programs are a 12-hour day with a
  two-hour plateau and a dark night.
- 🌅 **Sun and moon.** Switch a program to **Sky** and turn on the **Sun** and/or **Moon** layer. Sun follows the real
  sunrise and sunset at your location, with a warm glow at the edges and a ramp that is gentler than the real sun's
  (or a fixed day length you choose). Moon on a blue lamp lifts into the dark hours, brighter near a full moon; with
  Sun off it is a night-only light. Preview any day of the year before it happens.
- ⛈️ **Weather.** Optionally let clouds dim the sun, and a thunderstorm start the Storm effect.
- 🐟 **New tank?** **Acclimation** raises the whole program from a low share to full over days you choose.
- 🌗 **Smooth.** Transitions ease in and out, and **soft start** fades the lamps in whenever the controller starts.
- ✨ **Effects.** Storm, Cloud, Aurora and, on a Tuna Blue, Rainbow and the moon colors, as temporary overlays on the
  program, plus the standard ESPHome effects.
- 💡 **Any lamp setup.** Several groups on one board, a mode per group (Program, Manual, Off), and the same programs on
  ordinary dimmable, tunable-white, RGB and RGBW ESPHome lights, so a night light can follow the moon or a refugium
  strip can run a reverse photoperiod. See [DIY lights](docs/diy-lights.md).
- 🔗 **One leader, many followers.** Put a second esphome-klink board on the same chain as a `follower`; it follows
  the leader's group writes and answers the leader's scan for its own lamps (every `followers:` entry is a lamp by
  default, and `lamp: false` opts out). It never starts a frame, and it can drive its own native ESPHome lights with
  the class they actually support. See [leader and follower](docs/leader-follower.md).
- 🤝 **AP9X handover.** An AP9X can control its own K-Link chain. This controller announces itself with the same
  presence beacon Kessil's controllers use; an AP9X acting as the chain controller hands the chain over, and this
  controller then runs the program.
- 🏠 **Home Assistant optional.** The board serves its own web page at `/klink`. Home Assistant adds entities,
  automations, your home location and the weather.

## ✅ Supported lights

| Light | Config | Status |
|---|---|---|
| A360X Tuna Sun | any leader config | ✅ Tested |
| A360XE Tuna Sun | any leader config | ⚠️ Untested (expected to work) |
| A360X / A360XE Tuna Blue, A500X | any leader config | ⚠️ Untested |
| AP9X | any leader config | ⚠️ Untested |
| Refugium | any leader config | ⚠️ Untested |
| Ordinary ESPHome lights (DIY strips) | [DIY lights](docs/diy-lights.md) | ✅ Board LED tested · ⚠️ strips not run on a tank |
| Several boards on one chain | [Leader and follower](docs/leader-follower.md) | ⚠️ Untested on hardware |

❌ **Not supported:** the AP700 and older lights without a K-Link port.

| Board | Status |
|---|---|
| ESP32-C6 | ✅ Tested |
| ESP32-S3, -C3, -S2 | ⚠️ Builds, not run on a lamp |

**Notes**
- **Tuna Sun:** tested on two lamps in one group (intensity, color, red, amber, finding lamps, the daily program, effects,
  transitions).
- **Tuna Blue and A500X:** the channels (violet, red, green) follow Kessil's controller screen.
- **AP9X:** same type and channels as a Tuna Blue. If it is the chain controller, this board's presence beacon makes it
  hand over. Leave its own Wi-Fi control off.
- **Refugium:** the channels follow Kessil's product page.
- **DIY strips:** the planted-strip, refugium-follower and refugium-leader examples validate and build for the ESP32-C6.
- **Several boards:** the ownership logic and the follower's color mapping are host-tested; a two-board bench run is still
  needed.
- **Sun, moon, weather, acclimation, soft start:** computed and run on the device. The sun and moon are checked against an
  independent astronomy library; soft start and a full acclimation run have not been seen on lamps.
- **Kessil's Wi-Fi dongle or a Controller X** on the same chain at the same time fights this board for control, so unplug
  them.

**Home Assistant is optional.** It adds entities, automations, your home location and the weather. Without it the sun
and moon use your time zone's city as an approximate location, and there is no weather.

## 🙋 Help wanted

Most of the table above is untested because the author owns only a Tuna Sun. If you own another lamp, a short report
is a big help.

<details>
<summary>What to test and report</summary>

- **Tuna Blue owners:** in Home Assistant, enable the manual **violet**, **red** and **green** numbers for the group.
  Put the group in Manual, set one number to 50 % at a time, and note which color the lamp shows. Open an issue with
  what you saw.
- **A360XE, A500X, AP9X and Refugium owners:** run a scan, then open an issue with the type, model and firmware shown
  for each lamp in the Settings tab (for example `Type 2 · A360X Tuna Sun · firmware 1.3.0`). AP9X owners: say whether
  the AP9X was acting as its own chain controller, whether it handed over when this board started, and whether the
  violet, red and green channels match the lamp. Refugium owners: enable the manual **white** and **green** numbers,
  set one to 50 % at a time and note the color; then turn the **color** number from 0 to 240 and note where blue,
  violet, magenta and red appear.
- **ESP32-S3, -C3 and -S2 owners:** tell us whether the board finds your lamps and holds a program.
- **Two-board chains:** report whether the follower tracks the leader and whether the claim sequence behaves, and
  whether the leader's scan finds the follower's lamps and identify dips them.

</details>

## 📚 Documentation

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
| [DIY lights](docs/diy-lights.md) | Refugium, sump, moonlight and planted-tank strips; wiring, spectrum and safety |
| [Configuration](docs/configuration.md) | Substitutions, options, several groups, templates |
| [Leader and follower](docs/leader-follower.md) | Several esphome-klink devices on one chain, followers, claiming, AP9X handover |
| [Development](docs/development.md) | Layout, tests, the mock device, CI |

## ⚠️ Disclaimer

This is an independent, unofficial, community project. It is **not affiliated with, endorsed by, or supported by
Kessil**. "Kessil", "Tuna Sun", "Tuna Blue", "AP9X" and related names are trademarks of their respective owners and are
used here only to say which products this software is meant to work with.

**Use at your own risk.** This software talks to your lights over their control port in a way Kessil does not
document or support. It could malfunction, behave unexpectedly, or void your warranty. Plugging a microcontroller
board into a lamp's port can damage the board, the lamp, or both. Never leave lights running unattended on a
schedule you have not verified, and keep your livestock's needs in mind: a wrong schedule can harm fish, corals and
plants. You are responsible for anything you connect and run. The software is provided "as is", without warranty of
any kind (see [LICENSE](LICENSE)).
