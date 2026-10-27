# Light programs: the daily program on any ESPHome light

A **light program** runs the same daily program on ordinary ESPHome lights, with no K-Link bus at all. Everything a
lamp group has works the same: the point editor, **Mode** (Program, Manual, Off), the **Status** text, live preview,
the curve editor on the web page, sun and moon sources, acclimation and weather. It is not gated by **Send to
lights**, which only affects bus frames.

A good use is a night light that follows the moon, or a desk light that follows the sun. It is tested on the
on-board RGB LED of an ESP32-C6; other light types compile and validate but are untested.

Several lights can form one program. They all receive the same values, exactly like lamps in one group. A light
that cannot do one of the program's channels ignores that channel; the others still get it. Give each program a model
that matches its lights, and use one model per program.

## Models

| Model | Channels (UI1-UI5) | Editable point values |
|---|---|---|
| `brightness` | int | INT (0-255) |
| `ct` | int, ct | INT (0-255), CT (0-255) |
| `rgb` | int, r, g, b | INT (0-255), R / G / B (0-100) |
| `rgbw` | int, w, r, g, b | INT (0-255), W (0-255), R / G / B (0-100) |

On `ct`, CT 0 is the warm end and 255 the cold end. Colors are percentages. A point text is
`HH:MM int=… r=… g=… b=…` with any subset of the model's channels (the rest 0), or `off`.

The **Sun** source works on every model. The **Moon** source needs blue, so it is offered on `rgb` and `rgbw`.

## Manual detection

Changing a light from outside the program, in Home Assistant, another automation or its own switch, switches the
program to **Manual**: the program stops writing and the light keeps what you set. Choosing **Program** in the Mode
select hands control back and re-applies the program at the current time. **Off** turns the lights off.

## Effects and Find

The `klink_*` effects are light effects that work on any ESPHome light, not only K-Link groups: `klink_storm`,
`klink_cloud` and `klink_aurora` on every light except on/off-only (binary) lights, and `klink_rainbow` on RGB
lights. The two Moon effects (`klink_moon_red`, `klink_moon_blue`) are only on K-Link groups. Each takes `speed`
(fast, moderate or slow; default fast) and `duration` (default 2min; 0s runs until stopped).

UI1 (intensity) is the light's brightness. The color axis (UI2) is its color temperature when it has one; on an RGB
light without color temperature it is a warm-to-cool white tint (an approximation of 2700 K to 6500 K). A
brightness-only light has no color axis, so Cloud is not offered on it, because Cloud moves only the color axis.
Storm and Aurora also move intensity, so they work on every light. Rainbow sweeps red, green and blue at full
brightness.

| Light type | Effects offered | How UI2 shows |
|---|---|---|
| Brightness only | Storm, Aurora | No color axis; Cloud is not offered |
| Color temperature or cold/warm white | Storm, Cloud, Aurora | Its color temperature |
| RGB | Storm, Cloud, Aurora, Rainbow | A warm-to-cool white tint (an approximation of 2700 K to 6500 K) |
| RGBW | Treated like RGB for effects | As RGB |

To use them, list them on the light, for example:

```yaml
light:
  - platform: rgb
    ...
    effects:
      - klink_storm:
      - klink_cloud:
      - klink_aurora:
      - klink_rainbow:
```

For a color temperature or cold/warm white light list storm, cloud and aurora; for a brightness-only light storm and
aurora. Effect frames go to the light at 25 a second with no fade and are not published to Home Assistant one by
one (Home Assistant sees the effect running); they are not saved.

A light program shows, on the page's Effects row, the effects that every one of its lights can show, including Pulse,
Flicker and any other effect configured on the lights. Starting one starts it on all the lights of the program at
once, with the same random flashes for Storm. The program holds its schedule back while an effect runs; afterwards
the schedule shows again (Manual gets the lights back as they were before the effect, when the effect was started
from the page). The page's Speed and Time settings apply; they are not stored in flash for a program.

**Find these lights** on a program's Dashboard card makes the lights blink three times (dark, bright) and then
return to what they showed. It works while Send to lights is off too, since a light program never uses the bus.

## Templates

Include one template per program. Each file contains the whole program: the `klink: programs:` entry, the point
texts, the point editor, the mode select, the status sensor, the program source and the acclimation entities.

| Template | Model |
|---|---|
| `common/program-light-brightness.yaml` | `brightness` |
| `common/program-light-ct.yaml` | `ct` |
| `common/program-light-rgb.yaml` | `rgb` |
| `common/program-light-rgbw.yaml` | `rgbw` |

| Var | Meaning |
|---|---|
| `id` | the yaml/C++ id of the program, also used in every entity id |
| `name` | the display name prefix of every entity, for example `"Desk light"` |
| `lights` | a **YAML list** of ESPHome light ids, for example `[desk_light]`. Keep it unquoted: a quoted `"[desk_light]"` is a string, not a list. |
| `program_update_interval` | optional: how often an unchanged value is re-asserted (default `5s`) |

```yaml
packages:
  desk: !include
    file: common/program-light-brightness.yaml
    vars: {id: desk_program, name: "Desk light", lights: [desk_light]}
  ceiling: !include
    file: common/program-light-ct.yaml
    vars: {id: ceiling_program, name: "Ceiling", lights: [ceiling, ceiling_2]}
```

Two includes of the same template work as two programs, as long as `id` and `name` differ. Entity names and ids are
built from them, so renaming a program changes Home Assistant entity ids and loses stored values; keep them stable.

[`examples/light-program.yaml`](../examples/light-program.yaml) is a complete, validated config: an `rgb` program on
the board's own WS2812 (from `common/rgb-led.yaml`), with no lamp attached.

## The program entry

The templates generate one entry in `klink: programs:`. Writing it yourself:

```yaml
klink:
  programs:
    - id: desk_program          # yaml/C++ id, used as `group:` on the entities
      name: "Desk light"        # display name prefix
      model: brightness         # brightness | ct | rgb | rgbw
      lights: [desk_light]      # one or more ESPHome light ids, driven together
      time_id: local_time       # the clock from common/base.yaml
      # slots: 16               # program points, 1 to 16, default 16
      # update_interval: 5s     # re-assert an unchanged value this often; 0 disables it
      # transition: 5s          # fade between points; defaults to update_interval
```

Up to **64 programs**; they take the internal ids **101 to 164**, above the bus group ids. Program ids must be
unique.
