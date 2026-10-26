# esphome-klink

An [ESPHome](https://esphome.io) custom component that connects **Kessil X-Series aquarium lights** to
**Home Assistant**, using a cheap USB-C ESP32 board and a USB-C cable. It controls the lamps' intensity,
color, and extra channels, and runs an editable 8-point daily program. You can use it from Home
Assistant or from the esphome-klink web page the board serves itself, which also works offline.

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
| Group **Mode** (Program / Manual / Off), status text and live preview | Compiles in CI; **new and not yet tested on lamps**. |
| **esphome-klink web page** at `/klink` and the ESPHome device page | Compiles in CI; **new and not yet tested on a device or on lamps**. The page can also be run against a mock device, see [Contributing / development](#contributing--development). |
| **Kessil A360XE Tuna Blue** | **Untested.** Included with Kessil's layout for the A360XE: Tuna Blue plus red, green and purple (violet + indigo). The channel order matches Kessil's controller screen (VI, R, G), but the mapping to the lamp's hardware has not been confirmed on a lamp. |
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
`kessil-tuna-sun-esp32-s2.yaml`, and the matching `kessil-tuna-blue-*.yaml`. They include the default
network package, so you do not need to add Wi-Fi, API or OTA yourself: the device opens a setup access
point when it has no Wi-Fi credentials (see [Network](#network-wifi-api-and-ota)).

### 1. Adopt it from the ESPHome dashboard (easiest)

Each file contains a `dashboard_import:` block, so the dashboard can adopt it directly.

1. Flash the board once over USB with any ESPHome firmware (for example the ESPHome web installer at
   <https://web.esphome.io>, "Prepare for first use"), while it is connected **only to your computer**.
2. In the ESPHome dashboard, adopt the discovered device, or choose **New device -> import from URL** and
   paste:

   ```
   github://nschimme/esphome-klink/kessil-tuna-sun-esp32-c6.yaml@main
   ```

3. Finish the adoption and install. The dashboard creates a small config that pulls in this repo,
   including the default network package. If it does not add Wi-Fi credentials, connect to the
   device's setup access point after flashing and pick your network there.

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
```

The top-level file already brings the network package along. To run your own network, API or OTA
setup instead, leave out the top-level file and assemble the pieces yourself. This is useful to control
two groups (see [examples/two-groups.yaml](examples/two-groups.yaml)) from one board, to change a group ID,
or to add API encryption and an OTA password:

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

# common/network-wifi.yaml is not included here, so provide a network:
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

#### Several groups on one board

Each lamp model has a reusable template: `common/group-tuna-sun.yaml` and
`common/group-tuna-blue.yaml`. `common/tuna-sun.yaml` and `common/tuna-blue.yaml` are thin wrappers
around them that keep the group existing installations already use. To add another group, include
the template once per group with three package vars:

- `id` — the yaml/C++ id of the light, also used as every `group:` reference (for example `reef_5`);
- `name` — the display-name prefix of the entities (for example `Reef 5`);
- `gid` — the K-Link group id.

```yaml
packages:
  remote_package:
    url: https://github.com/nschimme/esphome-klink
    ref: main
    files:
      - boards/esp32-c6.yaml
      - common/base.yaml
      - common/tuna-sun.yaml
      - common/tuna-blue.yaml
      # A second Tuna Sun group, "Reef 5", in K-Link group 5:
      - path: common/group-tuna-sun.yaml
        vars: {id: reef_5, name: Reef 5, gid: "5"}
```

With a local checkout the same thing is a plain include:

```yaml
packages:
  reef_5: !include
    file: common/group-tuna-sun.yaml
    vars: {id: reef_5, name: Reef 5, gid: "5"}
```

Custom groups use K-Link group ids **3 to 25**; ids 1 and 2 are the defaults of the Tuna Blue and
Tuna Sun packages (change those with `tuna_blue_gid` / `tuna_sun_gid`). A group cannot mix lamp
types: a Tuna Sun group only contains Tuna Sun lamps, a Tuna Blue group only Tuna Blue lamps. See
[examples/two-groups.yaml](examples/two-groups.yaml) and
[examples/three-groups.yaml](examples/three-groups.yaml).

Keep `id` and `name` stable once a group is in use: the entity names and ids are built from them,
so renaming a group changes Home Assistant entities and loses the values stored for the old ones.

### 3. Clone the repo

```sh
git clone https://github.com/nschimme/esphome-klink
cd esphome-klink
esphome run kessil-tuna-sun-esp32-c6.yaml
```

No secrets file is needed: connect to the "<name> Setup" access point after flashing. The top-level
files fetch the component from this repo's `main` branch. To use the local copy of the component while
developing, change the `external_components:` source in `common/base.yaml` from `type: git` to
`type: local` with `path: components`.

## First run: be careful

1. **Flash while connected only to your computer.** Once the firmware runs, the board's USB pads carry the
   lamp bus, so the USB console is gone. Later updates go over WiFi (or hold the BOOT button while
   plugging in to flash by cable).
2. Unplug it from the computer and power it from the lamp. The hub listens first and transmits nothing
   while **Send to lights** is off, so nothing goes out on the bus until you say so.
3. If Kessil's WiFi dongle is on the chain, unplug it. Two controllers on one bus fight each other; the
   component listens and stops when it hears another controller, but do not plan on running both.
4. If you were prompted for Wi-Fi, connect to the **"<name> Setup"** access point and pick your network
   (see [Network](#network-wifi-api-and-ota)). Then open the device page.
5. Check the clock and **Bus state**: it should say the hub is listening. If **Bus state** reports a
   conflict, press **Retry** once the other controller is gone.
6. Turn on **Send to lights**. Frames are otherwise only logged (dry run). When the bus has been quiet
   and no lamps are known yet, the first scan starts by itself; **Known lamps** shows how many it found.
7. Open the esphome-klink page at `http://<device>/klink`. On the Dashboard, tap a group to flash its lamps, so
   you can see which lamps it controls. Use Settings to move a lamp to another group or rescan if
   needed.
8. On the Program tab, dial in the daily program. Edits preview on the lamps at once. Then set the
   group's **Mode** to **Program** and check the status text.

With `auto_writes: "true"` the component listens for 15 seconds and only takes over by itself after the
bus has been quiet; if another controller appears later, it stops and reports a conflict in
**Bus state**.

## Upgrading from 0.1.0

Version 0.2.0 reshapes the controls and the way the config is assembled. The Home Assistant
integration will need a little cleanup after you flash the new firmware.

**Renamed and removed entities.** Take a look at the device's **Entities** page in Home Assistant
after the upgrade:

- **Bus writes** is now **Send to lights**.
- The per-group schedule switch, the pause switch and the **Resume schedule** button are gone. The
  **Mode** select (**Program** / **Manual** / **Off**) and the **Status** text sensor replace them.
- **Commissioning mode**, **Find lamps** and **Identify next lamp** are no longer in the default
  config. Setup is automatic now, and **Retry** clears a bus conflict; the power-user actions
  `klink.scan`, `klink.identify`, `klink.stage_assignment` and `klink.confirm_assignment` still
  exist for scripting.

Home Assistant keeps the old entities as unavailable/orphaned entries and adds the new ones next to
them. Remove the old entries from the device's **Entities** page; do not delete the new **Mode** and
**Status** entities, they are the replacements.

**Send to lights is off after the upgrade** because its entity name changed and the new switch
starts in the off position. Frames are only logged (dry run) until you turn **Send to lights** on
again.

**Stored point values and modes are kept.** The per-group package keeps exactly the same entity
names and ids for the light, the eight point texts, the point editor and the manual channels, so
the values in flash (the daily program and the chosen mode) still belong to the same entities. Only
the entities listed above changed name.

**Connectivity moved to `common/network-wifi.yaml`.** The device now serves its own web pages and
the top-level files get Wi-Fi (with a setup access point), the API and OTA from
`common/network-wifi.yaml`. It is **unsecured by default**: no API encryption key and no OTA
password. If the device is on a network you do not trust, add your own key and password (see
[Network](#network-wifi-api-and-ota)).

## Network: Wi-Fi, API and OTA

`common/network-wifi.yaml` is the default connectivity package used by all eight top-level files. It
provides:

- Wi-Fi, with a **"<name> Setup"** access point and captive portal when no credentials are known;
- the Home Assistant API and OTA updates.

**It is unsecured by default: no API encryption key and no OTA password.** If the device is on a
network you do not trust, add them in your own config:

```yaml
api:
  encryption:
    key: !secret api_encryption_key

ota:
  - platform: esphome
    password: !secret ota_password
```

To use Ethernet, Thread or fixed Wi-Fi credentials instead, leave out `common/network-wifi.yaml` and add
your own network, `api:` and `ota:`. Whatever you use, the device needs a network: the clock is set over
SNTP and the program runs on local time.

## Web pages

The device serves two pages. Both work offline, straight from the board.

- **`http://<device>/klink`** is the esphome-klink page, with three tabs:
  - **Dashboard**: each group's mode and status; tap a group or a lamp to flash it so you can tell
    which is which.
  - **Program**: a visual day-curve editor. Drag points, watch the change preview on the lamps, use the
    "now" marker and scrub through the day, apply presets (Default, Daytime plateau, Always on and
    Moonlight), and copy a program to another group. **Done** ends the preview.
  - **Settings**: the lamp list with nicknames, moving a lamp to another group, rescan, **Send to
    lights**, **Retry**.
- **`http://<device>/`** is ESPHome's own device page (`web_server` version 3, `local: true`), with the
  entities grouped as **Controls**, **Program**, and **Bus and setup**.

Set `web: false` under `klink:` to leave out the esphome-klink page and its JSON API. Adding
`web_server: auth: {username: ..., password: ...}` in your own config protects both pages.

## What you get in Home Assistant

For each lamp group (Tuna Sun and Tuna Blue):

- a **light**: brightness is intensity; on the Tuna Sun the color temperature is the color control. The
  effects Pulse, Color sweep, Sunrise and Sunset are included;
- a **Mode** select: **Program** runs the daily program, **Manual** holds what you set, **Off** turns
  the lamps off. Changing the light or a manual channel number switches the group to Manual
  automatically; choosing Program hands control back. The mode is remembered across restarts. Like
  Kessil's WiFi dongle, a held value (Manual, or a point preview) is repeated about once a second, since
  it is not known whether the lamps keep a level without repeats;
- a **Status** text sensor saying what the group is doing right now, for example
  `Program · 14:02 · INT 42% CLR 60%`, `Manual`, `Off`, `Editing point 3 (previewing)` or
  `Effect: Sunrise`;
- the **8 point text entities** (`08:00 int=40 clr=20 r=10`) for copy and paste;
- a **point editor**: choose a point 1 to 8, then edit its time, whether it is on, and its channel
  numbers, like the controller's own Quick Set screen. Editing a point shows it on the lamps at once
  (live preview) and after about a minute without edits the group returns to its previous mode; changes
  are saved to flash a couple of seconds later;
- **manual channel numbers**: **red** and **amber** on the Tuna Sun; **color (raw)**, **violet**,
  **red** and **green** on the Tuna Blue;
- **Bus state**, **Known lamps**, **Bus timeouts** diagnostics, and a **Retry** button to clear a bus
  conflict.

Values are raw: INT and CLR run 0 to 255, red, amber, violet and green 0 to 100. For the Tuna Sun,
Kessil's A360X specification gives 6,000 K to 9,000 K plus red and amber; the spacing between the two
ends is assumed linear in mireds, not measured. On the Tuna Blue, the channel order follows Kessil's
controller screen (VI, R, G); how those channels map to the lamp itself is still untested.

The same controls and a visual program editor are on the [web pages](#web-pages).

## Configuration

Substitutions you can set in your own config:

| Substitution | Default | Meaning |
|---|---|---|
| `name`, `friendly_name` | per file | device name |
| `timezone` | `Etc/UTC` | the program runs on local time, so set yours |
| `tuna_sun_gid` | `"2"` | the K-Link group the Tuna Sun package controls |
| `tuna_blue_gid` | `"1"` | the K-Link group the Tuna Blue package controls |
| `auto_writes` | `"false"` | `"true"` = listen first and take over the bus when quiet |

The model packages `common/tuna-sun.yaml` and `common/tuna-blue.yaml` are thin wrappers around the
templates `common/group-tuna-sun.yaml` and `common/group-tuna-blue.yaml`. Include a template directly
with the package vars `id`, `name` and `gid` to add another group (see
[Several groups on one board](#several-groups-on-one-board)); custom groups use K-Link group ids
3-25 and a group cannot mix lamp types.

Options under `klink:`:

| Option | Default | Meaning |
|---|---|---|
| `web` | `true` | serve the esphome-klink page and its JSON API at `/klink`; `false` leaves them out |
| `auto_writes` | `false` | take over the bus by itself once it has been quiet (overrides the switch) |

Setup is automatic: **Send to lights** is off by default (a dry run that only logs frames), and once it
is on and the bus has been quiet, the first scan starts by itself when no lamps are known. **Find
lamps**, **Identify next lamp** and **Commissioning mode** are no longer in the default config. For
scripting, the power-user actions `klink.scan`, `klink.identify`, `klink.stage_assignment` and
`klink.confirm_assignment` still exist.

### Optional on-board RGB LED

`common/rgb-led.yaml` adds the board's single WS2812 as a plain **light**. It is not included by
default and it is **not driven by the program**; it is just an indicator you can turn on from Home
Assistant. The pin comes from `rgb_pin` in the board file: C3 GPIO8, C6 GPIO8, S2 GPIO18, S3 GPIO48
(those are the DevKit boards and are unverified; S3 v1.1 boards use 38). Override `rgb_pin` if your board
differs:

```yaml
packages:
  rgb: !include common/rgb-led.yaml
```

## Contributing / development

Host tests for the pure C++ logic need only a compiler:

```sh
for t in tests/test_klink_*.cpp; do g++ -std=c++11 -Wall -Wextra -I components/klink "$t" -o /tmp/t && /tmp/t; done
```

The nearest-colour search lives once in C++ (`klink_colors.h`) and once in Python
(`klink_colors.py`, the reference). Its tables and golden vectors are generated from the Python
source at test time, so nothing generated is committed:

```sh
python3 components/klink/klink_colors.py --emit-cpp > .tmp/klink_color_tables.h
python3 components/klink/klink_colors.py --emit-golden > .tmp/snap_golden.txt
g++ -std=c++11 -Wall -Wextra -I components/klink -I .tmp tests/test_color_snap.cpp -o .tmp/test_color_snap
.tmp/test_color_snap .tmp/snap_golden.txt
```

To work on the esphome-klink page without a device, `tools/mock_server.py` serves it with a fake
device. It re-assembles the page from `components/klink/web/` on every request, so a reload picks up
your edits:

```sh
python3 tools/mock_server.py            # http://127.0.0.1:8080/klink
python3 tools/mock_server.py 9000       # another port
```

The page sources are `components/klink/web/index.html`, `style.css` and `app.js`. Edit them
directly; a browser can also open that folder during development. Nothing is generated into the
tree: the component assembles the page (inline CSS and JS, generated colour block) and gzips it
during `esphome compile`. Check the size budget with:

```sh
python3 components/klink/web_build.py --check
```

CI (`.github/workflows/ci.yml`) runs the host tests and the page tests and size check, `esphome
config` on every top-level config and the example, and `esphome compile` for the boards it builds.

## License

[MIT](LICENSE).
