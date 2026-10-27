# Getting started

This page takes you from a board in a box to lamps running a daily program.

> **At a glance**
> - One ESP32 board and one USB-C cable per K-Link port.
> - Use the **native USB** port, not the UART/COM bridge port.
> - Measure D+ to D- before the first connection.
> - Keep **Send to lights** off until the first scan is reviewed.

## Words used in these docs

| Word | Meaning |
|---|---|
| **Group** | One or more lamps of the same model that always get the same values. A board can control several groups. |
| **Point** | A time of day with the values the lamp should have then. The lamp moves in straight lines between points. |
| **Source** | Where a group's points come from: your own (**My points**), the **Sun** or the **Moon**. |
| **INT** | Intensity: how bright the lamp is. |
| **CLR** | Color: on a Tuna Sun, 0 is the warm end and 255 the cool end. |
| **Send to lights** | The switch that lets the controller write to the lamps. Off is a dry run that only logs what it would send, which makes the first connection safe. |

## What you need

- A **Kessil X-Series light** with a spare K-Link port (the lamps have two). See [Supported models](#supported-models)
  for the models, their lamp types and what is tested. Not supported: the AP700 and older lights with no K-Link port;
  this controller talks to the X-Series lamps and the AP9X directly and does not work through those other products.
- An **ESP32 board with a native USB-C port** and a plain USB-C-to-USB-C cable. The board plugs into the lamp's
  K-Link port. There is no transceiver and no breakout: the chip's own USB data pads (D+ and D-) carry the bus.
- Wi-Fi, so the controller can set its clock over SNTP and talk to Home Assistant. The program runs on local time.
- **ESPHome 2026.10**, the version the project is built and tested with.

| Board family | Config suffix | D- (UART TX) | D+ (UART RX) | Tested |
|---|---|---|---|---|
| ESP32-C6 | `esp32-c6` | GPIO12 | GPIO13 | yes |
| ESP32-S3 | `esp32-s3` | GPIO19 | GPIO20 | no |
| ESP32-C3 | `esp32-c3` | GPIO18 | GPIO19 | no |
| ESP32-S2 | `esp32-s2` | GPIO19 | GPIO20 | no |

An ESP32-C6 "mini" or SuperMini style board is a good choice. Boards without a native USB-C connection to the
chip's USB data pads, and other variants (ESP32-H2, ESP32-P4), are not supported.

### Boards with two USB ports

On an ESP32-C6-DevKitC-1, the two USB-C ports are **not** two copies of the same UART:

| Port label | Chip connection | K-Link? | Use for |
|---|---|---|---|
| **USB** | Native USB Serial/JTAG, GPIO12/13 | **yes** | K-Link, flashing, console |
| **UART** / **COM** | USB-to-UART bridge → UART0 | no | Flashing, console, power |

K-Link is a shared half-duplex bus, not USB. One native pair is a tap on that bus. Lamps have two K-Link ports, so
the usual daisy chain is lamp-to-lamp with the board on a spare port; the board does not need two ports for that. To
put the board physically in the middle, use a passive two-connector breakout on the **same native D+/D- pair** (and
ground), not the second USB port. Do not parallel VBUS from two ports: power the board from one and pass only the
data pair and ground.

**Risk, accepted knowingly.** If the lamps terminate the pair, the pads are asked for more current than a normal
GPIO is rated for, and the board can be damaged. Before the first connection, measure the resistance across D+
and D- with the lamp unpowered. Above about 10 kOhm the load is light. The lamps themselves are not expected to
be at risk, but nothing here guarantees that.

## Supported models

K-Link lamps report one of three lamp types: 1 Tuna Blue, 2 Tuna Sun or 3 Refugium. The AP9X reports the Tuna Blue
type. Kessil says K-Link links A360X, A360XE and A500X units; the AP9X also has K-Link ports and can act as the
chain controller.

| Model | Lamp type | Status |
|---|---|---|
| A360X Tuna Sun | 2 Tuna Sun | Tested on hardware. |
| A360XE Tuna Sun | 2 Tuna Sun | Expected to work as a Tuna Sun; untested. |
| A360X/A360XE Tuna Blue | 1 Tuna Blue | The channels follow Kessil's controller screen; untested. |
| A500X Tuna Blue (185 W) | 1 Tuna Blue | Expected to work as a Tuna Blue: it has the same two K-Link ports and the same violet, red and green channels, which a controller on the bus can reach. Untested. |
| Refugium | 3 Refugium | Supported with `kessil-refugium-<board>.yaml`: UI2 color 0-240 (blue to red), UI4 white, UI5 green. Untested. |
| AP9X | 1 Tuna Blue | Use `kessil-tuna-blue-<board>.yaml`: the same model, same five channels (intensity, color, violet, red, green), and it can share a Tuna Blue group. If the AP9X is acting as its own chain controller, this controller's presence beacon makes it hand over. Untested. |
| A160WE and A80 | None (0-10 V) | Analog lights with no K-Link support known; not controllable over K-Link. A light program on a PWM-to-0-10 V converter module driven by the ESPHome board is the route (see [light programs](light-programs.md) and [DIY lights](diy-lights.md)). |
| AP700 | None | Not supported; its control path is different. |

The AP9X can also act as the K-Link controller for a chain. Use the Tuna Blue package (or assign it to an
existing Tuna Blue group) and leave its own Wi-Fi control off. A leader board announces itself with
the same K-Link presence beacon Kessil's controllers use; an AP9X that is acting as the chain controller hands the chain over
within a few seconds. A Spectral Controller X or Wi-Fi dongle does not hand over, so their group writes still pause
this board. See [leader and follower](leader-follower.md) for putting a second esphome-klink board on the chain.

## Install

Pick the file for your board and lamp: `kessil-tuna-sun-<board>.yaml`, `kessil-tuna-blue-<board>.yaml` (also
for an AP9X) or `kessil-refugium-<board>.yaml` for a Refugium, where
`<board>` is `esp32-c6`, `esp32-s3`, `esp32-c3` or `esp32-s2`. Each file includes the default network package, so
you do not add Wi-Fi, API or OTA yourself: the device opens a setup access point when it has no credentials.

### Option 1: adopt it from the ESPHome dashboard

1. Flash the board once over USB with any ESPHome firmware (for example the web installer at
   <https://web.esphome.io>, "Prepare for first use"), while it is connected **only to your computer**.
2. In the ESPHome dashboard, adopt the discovered device, or choose **New device -> import from URL** and paste:

   ```
   github://nschimme/esphome-klink/kessil-tuna-sun-esp32-c6.yaml@main
   ```

3. Finish the adoption and install. If no Wi-Fi credentials are added, connect to the device's setup access point
   after flashing and pick your network there.

### Option 2: a remote package in your own config

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

To run your own network, API or OTA setup instead, leave out the top-level file and assemble the pieces:

```yaml
substitutions:
  name: kessil-tank
  timezone: America/Chicago

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

For a **Tuna Blue** or an **AP9X**, use `kessil-tuna-blue-esp32-c6.yaml` (option 1 and the first block of
option 2) or `common/tuna-blue.yaml` (the second block) in place of the Tuna Sun file. The AP9X reports type 1 and
uses the Tuna Blue model. Each package is its own K-Link group: Tuna Blue defaults to group 1, Tuna Sun to group 2.
Set `tuna_blue_gid` to another free type-1 group id such as 3 if you include a second one. Because the AP9X is type
1, it can also join a Tuna Blue group directly; assign it there if you want it to follow the Tuna Blue program. For
more groups, see [configuration](configuration.md#several-groups-on-one-board).

### Option 3: clone the repo

```sh
git clone https://github.com/nschimme/esphome-klink
cd esphome-klink
esphome run kessil-tuna-sun-esp32-c6.yaml
```

No secrets file is needed: connect to the "<name> Setup" access point after flashing. The top-level files fetch
the component from this repo's `main` branch. To use the local copy of the component while developing, change the
`external_components:` source in `common/base.yaml` from `type: git` to `type: local` with `path: components`, or
set the `klink_ref` substitution to a branch name.

## First run

1. **Flash while connected only to your computer.** Once the firmware runs, the board's USB pads carry the lamp
   bus, so the USB console is gone. Later updates go over Wi-Fi (or hold the BOOT button while plugging in to
   flash by cable).
2. Unplug it from the computer and power it from the lamp. The controller listens first and transmits nothing
   while **Send to lights** is off (a dry run that only logs frames).
3. If Kessil's Wi-Fi dongle is on the chain, unplug it. Two controllers on one bus fight each other. This
   board announces itself as a leader, waits for the other controller's group writes to stop, and pauses if one
   appears later, but do not plan on running both.
4. If you were prompted for Wi-Fi, connect to the **"<name> Setup"** access point and pick your network, then open
   the device page.
5. Check **Bus state**: with writes off it says `Writes off (dry run)`. A follower says `Follower: following the chain`.
6. Turn on **Send to lights**. The board announces itself, claims the chain, and starts the first scan by itself
   when no lamps are known; **Known lamps** shows how many it found.
7. Open the esphome-klink page at `http://<device>/klink`. On the Dashboard, tap a group to flash its lamps and see
   which ones it controls. Use Settings to move a lamp to another group or rescan.
8. On the Program tab, dial in the daily program (edits preview on the lamps at once). Then set the group's
   **Mode** to **Program** and check the status text.

With `auto_writes: "true"` the leader claims the chain by itself after a short announcement and a few seconds
without another controller's group writes. If another controller writes later, it pauses and tries again after the
conflict release time.

## Network and security

`common/network-wifi.yaml` is the default connectivity package. It provides Wi-Fi with a "<name> Setup" access
point and captive portal when no credentials are known, the Home Assistant API, and OTA updates.

**It is unsecured by default: no API encryption key and no OTA password.** If the device is on a network you do
not trust, add them in your own config:

```yaml
api:
  encryption:
    key: !secret api_encryption_key

ota:
  - platform: esphome
    password: !secret ota_password

web_server:
  auth:
    username: !secret web_user
    password: !secret web_password
```

The OTA password does not protect ESPHome's web update page (`/update`); `web_server: auth` protects it and both
web pages. To use Ethernet, Thread or fixed Wi-Fi credentials, leave out `common/network-wifi.yaml` and add your
own network, `api:` and `ota:`. Whatever you use, the device needs a network for its clock.

## Questions

**Do I need Home Assistant?** No. The web page on the board does everything on its own. Home Assistant adds the
entities, automations, your home location and the weather.

**Can I keep Kessil's Wi-Fi dongle or Controller X connected?** Not at the same time. Two controllers on one bus fight
each other. This controller pauses when it hears one write, and claims again after the other is quiet.

**Can I put more than one esphome-klink board on the chain?** Yes: one `role: leader` and any number of
`role: follower` boards (`common/follower-base.yaml`). A follower follows the leader and can drive its own native ESPHome
lights; every `followers:` entry is a lamp by default, so it also answers the leader's scan unless it says `lamp: false`.
See [leader and follower](leader-follower.md).

**What if Wi-Fi drops?** A running board keeps its clock and keeps running the program. After a restart it needs the
network once to set the clock.

**What if the board loses power?** Whether the lamps hold their level without a controller is not documented. When
the board starts again it resumes the program, fading the lamps in if soft start is on.

## Troubleshooting

| Symptom | What to check |
|---|---|
| Nothing reaches the lamps | **Send to lights** is off, or **Bus state** reports a conflict. Turn it on, or press **Retry** after unplugging the other controller. |
| **Known lamps** stays 0 | The scan starts once **Send to lights** is on and the bus has been quiet. Check the cable and that the lamp is powered, then use Settings -> rescan. |
| The program does not follow the time | The clock is not set yet (the status line says "waiting for clock"), or `timezone` is wrong. |
| Sun and Moon say "Set a location" | No location is known; see [daily programs](daily-programs.md#location). |
| Home Assistant shows no entities | The device and Home Assistant must be on the same network, with the ESPHome integration added. |
