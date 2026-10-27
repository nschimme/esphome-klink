# DIY reef and planted lights

Any ordinary ESPHome light can run the same schedule as a Kessil group: a refugium strip, sump light,
moonlight, nano/quarantine light, or planted-tank strip.

> **At a glance**
> - **Standalone strip** → local light program. No K-Link chain.
> - **Strip on a K-Link chain** → follower. The leader runs the schedule.
> - **Refugium** → reverse photoperiod on a lamp-free channel (GID 3).
> - **Reef display** → this is control, not a spectrum upgrade; keep a reef fixture for SPS.

**Jump to:** [Reef](#reef) · [Planted](#planted) · [Standalone strip](#standalone-strip) · [Refugium strip](#refugium-strip-on-the-chain) · [Wiring](#wiring) · [Spectrum and limits](#spectrum-and-limits)

```text
Standalone:  ESP32 ──data──▶ strip        (no K-Link)

Follower:    leader ──K-Link──┬── Kessil lamps
                              └── follower ESP32 ──data──▶ strip
```

| I want… | Build | Schedule runs on |
|---|---|---|
| A planted or sump strip with its own timing | Local light program | The strip board |
| A refugium strip on the display's chain | Follower + `common/refugium-channel.yaml` on the leader | The chain leader |
| A strip that follows the display lights | Follower bound to the display group | The chain leader |
| A moonlight with its own timing | Local light program, low blue points | The strip board |

## Reef

| Use | Build | Notes |
|---|---|---|
| Refugium / macroalgae | Follower on GID 3, or standalone program | Reverse photoperiod (on 20:00, off 08:00); warm white/red |
| Sump / equipment | Standalone program | White or tunable white; on only when working |
| Moonlight | Standalone program | A few percent of blue; do not follow the display group |
| Nano / frag / quarantine | Standalone program | Check spectrum and intensity for that tank |
| Supplemental strip | Follower on the display group | Adds fill/actinic; does not make an RGB strip a reef fixture |

## Planted

| Use | Build | Notes |
|---|---|---|
| Main strip | Standalone `rgbw` program | White for viewing, red/blue for growth; sunrise/sunset ramp |
| Siesta | Standalone program | Two on/off blocks in the points |
| Moonlight / viewing | Second program or second strip | Keep the main photoperiod dark at night |

An RGB/RGBW strip can grow low- and medium-demand plants, but it is not a horticultural LED just
because it has red and blue dice. If growth is the goal, use a strip with a suitable white/red
spectrum.

Refugium timing (example):

```text
            08:00        20:00        08:00
display     ██████████████░░░░░░░░░░░░
refugium    ░░░░░░░░░░░░░░████████████
```

## Standalone strip

Complete example: [`examples/diy-planted-strip.yaml`](../examples/diy-planted-strip.yaml).

```yaml
packages:
  base: !include ../common/base.yaml
  program: !include
    file: ../common/program-light-rgbw.yaml
    vars: {id: planted_program, name: "Planted strip", lights: [planted_strip]}

light:
  - platform: esp32_rmt_led_strip
    id: planted_strip
    name: "Planted strip"
    pin: GPIO4
    num_leds: 60
    chipset: SK6812
    rgb_order: GRB
    restore_mode: ALWAYS_OFF
```

Use the template that matches the strip: `program-light-rgbw.yaml`, `-rgb.yaml`, `-ct.yaml`, or
`-brightness.yaml`. It works with **Send to lights** off because it never uses the bus.

## Refugium strip on the chain

**Leader:** include the lamp-free channel ([`examples/refugium-leader.yaml`](../examples/refugium-leader.yaml)):

```yaml
packages:
  refugium: !include common/refugium-channel.yaml
```

This creates GID 3 with a reverse photoperiod and no Kessil lamps. The leader still writes its five
channels on the bus.

**Follower:** bind the strip to GID 3 ([`examples/diy-refugium-strip.yaml`](../examples/diy-refugium-strip.yaml)):

```yaml
packages:
  base: !include ../common/follower-base.yaml   # instead of common/base.yaml; sets role: follower

klink:
  followers:
    - id: refugium_follow
      gid: 3
      model: tuna_sun     # warm white plus red/amber for macroalgae
      lights: [refugium_strip]
      transition: 1s
```

`model` is the **leader's group**, not the strip: `tuna_sun`, `tuna_blue` (AP9X), or `refugium`. The
follower derives RGB, color temperature, or brightness from the strip itself. To follow the display
instead, use its group id; for an independent schedule, use the channel above or a standalone program.

Every `followers:` entry is a lamp by default, so this strip also answers the leader's scan and the
leader lists it as a lamp of the entry's model; it can then be assigned a GID like any lamp. Add
`lamp: false` to the entry if the strip should only follow the channel: it then never answers a scan
and never transmits at all.

## Wiring

```text
5 V PSU ──┬──────────────── strip 5 V
          ├── 1000 µF
          └── GND ──┬────── strip GND
                    └────── ESP32 GND

ESP32 GPIO4 ──330 Ω──▶ 3.3→5 V level shifter ──▶ strip DIN
```

- [ ] ESP32-C6/S3; examples use GPIO4 for data (C6 K-Link pins are GPIO12/13).
- [ ] 5 V strip and a 5 V supply sized for it: about 60 mA per LED at full white.
- [ ] Common ground; 3.3 V→5 V level shifter; 330–470 Ω data resistor; 1000 µF at the strip.
- [ ] Power injection for long strips; do not power a large strip through the board.
- [ ] Fuse or current-limited supply; IP-rated strip/cable; drip loops; GFCI/RCD; ventilated enclosure.
- [ ] Unplug before wiring. Keep salt creep and splashes out of the electronics.

Analog RGB/RGBW: use MOSFETs or a dedicated driver on a separate supply, common ground, driven by
ESPHome `pwm` outputs.

## Spectrum and limits

| Use | RGB strip enough? | Better choice |
|---|---|---|
| Refugium / macroalgae | Usually | Warm white/red refugium or horticultural strip |
| Planted, low/medium demand | Often | White + red/blue planted LED |
| Planted, demanding carpet/red | No | Purpose-built planted LED |
| SPS reef display | No | Reef fixture with blue/violet/UV and full spectrum |
| Sump, moonlight, quarantine | Yes | Any suitable strip |

- **Follower color is an approximation** of the source Kessil model on different LEDs; it is not a
  colorimeter match.
- Follower limits: it does not scan, assign or take over; it holds its last value if the leader
  disappears. A follower lamp is scanned, assigned and identified by the leader (see
  [leader and follower](leader-follower.md#lamp-mode)); a `lamp: false` entry is not, and never
  transmits.
- Examples pass `esphome config` and build; they have not been run on a tank yet.
- Roles and claiming: [leader and follower](leader-follower.md).
