# esphome-klink

An [ESPHome](https://esphome.io) custom component that connects **Kessil X-Series aquarium lights** to
**Home Assistant**, using a cheap USB-C ESP32 board and a USB-C cable. It controls the lamps' intensity,
color, and extra channels, and runs an editable 8-point daily program, all from Home Assistant.

> ## Disclaimer
>
> This is an independent, unofficial, community project. It is **not affiliated with, endorsed by, or
> supported by Kessil**. "Kessil", "Tuna Sun", "Tuna Blue" and related names are trademarks of their
> respective owners and are used here only to say which products this software is meant to work with.
>
> **Use at your own risk.** This software talks to your lights over their control port in a way Kessil
> does not document or support. It could malfunction, behave unexpectedly, or void your warranty. Plugging a
> microcontroller board into a lamp's port can damage the board, the lamp, or both. Never leave lights
> running unattended on a schedule you have not verified, and keep your livestock's needs in mind: a
> wrong schedule can harm fish, corals and plants. You are responsible for anything you connect and run.
> The software is provided "as is", without warranty of any kind (see [LICENSE](LICENSE)).

## What works, and what has been tested

| Item | Status |
|---|---|
| **Kessil A360X Tuna Sun** | **Tested.** Two fixtures in one group, controlled from an ESP32-C6: intensity, color, red, amber, finding lamps, the daily program. |
| Program point editor, effects (Pulse, Color sweep, Sunrise, Sunset) | Built and flashed; **not yet verified end to end** against a lamp. |
| **Kessil A360X Tuna Blue** | **Untested.** Included with a violet / red / green channel layout that has not been confirmed on a lamp. |
| ESP32-C6 (USB-C) | **Tested.** |
| ESP32-S3, ESP32-C3, ESP32-S2 (USB-C) | **Compile only.** The configs build, but nobody has run them on a lamp. Reports welcome. |

### Not supported yet

- **Kessil Refugium** and any other X-Series model not listed above. You can try `model: generic`, which
  exposes the five channels as raw values, but it is untested and the channel meanings are unknown.
- **Other Kessil lights and controllers**, for example the AP9X, the AP700 and the Controller X. This
  project talks to X-Series fixtures directly; it does not work through those products.
- **Kessil's WiFi dongle running at the same time.** Two controllers on one bus fight each other. The
  component listens first and never transmits while it hears another controller, but you should unplug
  the dongle for normal use.
- **Lights with no K-Link port** (for example older non-X models).
- Other boards: anything without a native USB-C connection to the USB data pads, or an ESP32 variant not
  listed in the table below (ESP32-H2 and ESP32-P4, for example).

## Hardware

You need an ESP32 board with a **native USB-C port**, and a plain USB-C-to-USB-C cable. The board plugs
into a spare K-Link port on a lamp (the lamps have two). There is no transceiver and no breakout: the
chip's own USB data pads (D+ and D-) carry the bus.

| Board family | Config | D- (UART TX) | D+ (UART RX) | Tested |
|---|---|---|---|---|
| ESP32-C6 | `esp32-c6` | GPIO12 | GPIO13 | yes |
| ESP32-S3 | `esp32-s3` | GPIO19 | GPIO20 | no |
| ESP32-C3 | `esp32-c3` | GPIO18 | GPIO19 | no |
| ESP32-S2 | `esp32-s2` | GPIO19 | GPIO20 | no |

An ESP32-C6 "mini" or SuperMini style board is a good choice.

**Risk, accepted knowingly.** If the lamps terminate the pair, the pads are asked for more current than a
normal GPIO is rated for, and the board can be damaged. Before the first connection, measure the
resistance across D+ and D- with the lamp unpowered. Above about 10 kOhm the load is light. The lamps
themselves are not expected to be at risk, but nothing here guarantees that.

## Quick start

All three ways use the same files. Pick the file for your board:
`kessil-tuna-sun-esp32-c6.yaml`, `kessil-tuna-sun-esp32-s3.yaml`, `kessil-tuna-sun-esp32-c3.yaml`,
`kessil-tuna-sun-esp32-s2.yaml`, and the matching `kessil-tuna-blue-*.yaml`.

### 1. Adopt it from the ESPHome dashboard (easiest)

Each file contains a `dashboard_import:` block, so the dashboard can adopt it directly.

1. Flash the board once over USB with any ESPHome firmware (for example the ESPHome web installer at
   <https://web.esphome.io>, "Prepare for first use"), while it is connected **only to your computer**.
2. In the ESPHome dashboard, adopt the discovered device, or choose **New device -> import from URL** and
   paste:

   ```
   github://nschimme/esphome-klink/kessil-tuna-sun-esp32-c6.yaml@main
   ```

3. Finish the adoption and install. The dashboard creates a small config that pulls in this repo and adds
   your WiFi, API key and OTA password.

### 2. Use it as a remote package in your own config

```yaml
substitutions:
  name: kessil-tuna-sun
  timezone: America/Chicago        # the program runs on local time: set yours

packages:
  remote_package:
    url: https://github.com/nschimme/esphome-klink
    ref: main                      # pin a tag or commit once you have one that works
    files: [kessil-tuna-sun-esp32-c6.yaml]
    refresh: 1d

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

api:
  encryption:
    key: !secret api_encryption_key

ota:
  - platform: esphome
    password: !secret ota_password
```

You can also assemble the pieces yourself instead of using a top-level file. This is useful to control
two groups (see [examples/two-groups.yaml](examples/two-groups.yaml)) from one board, or to change a group ID:

```yaml
substitutions:
  name: kessil-tank
  tuna_sun_gid: "2"

esphome:
  name: ${name}

packages:
  remote_package:
    url: https://github.com/nschimme/esphome-klink
    ref: main
    files:
      - boards/esp32-c6.yaml
      - common/base.yaml
      - common/tuna-sun.yaml
```

### 3. Clone the repo

```sh
git clone https://github.com/nschimme/esphome-klink
cd esphome-klink
# create secrets.yaml with wifi_ssid, wifi_password, api_encryption_key, ota_password
esphome run kessil-tuna-sun-esp32-c6.yaml
```

The top-level files fetch the component from this repo's `main` branch. To use the local copy of the
component while developing, add this to your own config:

```yaml
external_components:
  - source:
      type: local
      path: components
```

## First run: be careful

1. **Flash while connected only to your computer.** Once the firmware runs, the board's USB pads carry the
   lamp bus, so the USB console is gone. Later updates go over WiFi (or hold the BOOT button while
   plugging in to flash by cable).
2. Unplug it from the computer and power it from the lamp.
3. **"Bus writes" is OFF by default.** In that state the component logs the frames it would send and
   transmits nothing. Check the logs, then turn the switch on.
4. If Kessil's WiFi dongle is on the chain, unplug it before enabling writes. With
   `auto_writes: "true"` the component listens for 15 seconds and only takes over by itself after the bus has
   been quiet; if another controller appears later, it stops and reports a conflict in **Bus state**.
5. Press **Find lamps** (with **Commissioning mode** on) to list the lamps and their groups. Set `tuna_sun_gid` or
   `tuna_blue_gid` to the group your lamps are in.

## What you get in Home Assistant

For each lamp group:

- a **light** (brightness is intensity; on the Tuna Sun, color temperature is the color control) with
  the effects Pulse, Color sweep, Sunrise and Sunset;
- a **mode** select: **Program** runs the daily program; **Manual** holds what you set and any light or
  number change switches to it automatically; **Off** turns the lamps off;
- a **status** text sensor saying what the group is doing right now (the running program, Manual, Off,
  a preview, or the active effect);
- **red** and **amber** (Tuna Sun) or **violet / red / green** (Tuna Blue) manual controls;
- a **program editor**: choose a point 1 to 8, then edit its time and its INT, CLR, R and AM values, like
  the controller's own Quick Set screen. Editing a point turns it on, shows it on the lamps at once for
  about a minute, and then the group returns to its previous mode; changes are saved to flash a couple of
  seconds later;
- raw **point text entities** (`08:00 int=40 clr=20 r=10`) for copy and paste;
- **Bus state**, **Known lamps** and **Bus timeouts** diagnostics.

Values are raw: INT and CLR run 0 to 255, red, amber, violet and green 0 to 100.

## Configuration

Substitutions you can set in your own config:

| Substitution | Default | Meaning |
|---|---|---|
| `name`, `friendly_name` | per file | device name |
| `timezone` | `Etc/UTC` | the program runs on local time, so set yours |
| `tuna_sun_gid` | `"2"` | the K-Link group the Tuna Sun package controls |
| `tuna_blue_gid` | `"1"` | the K-Link group the Tuna Blue package controls |
| `auto_writes` | `"false"` | `"true"` = listen first and take over the bus when quiet |

## Development

Host tests for the protocol code need only a C++ compiler:

```sh
for t in tests/test_klink_*.cpp; do g++ -std=c++11 -Wall -Wextra -I components/klink "$t" -o /tmp/t && /tmp/t; done
```

## License

[MIT](LICENSE).
