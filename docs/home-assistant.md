# Home Assistant entities

Every lamp group (one per Tuna Sun, Tuna Blue or Refugium template you include) shows up in Home Assistant as a device
section with the entities below, prefixed with the group's name (for example "Tuna Sun"). Entities marked
*off by default* exist but are disabled in Home Assistant until you enable them.

## The light

- **Brightness** is the lamp's intensity (UI1).
- **Tuna Sun:** color temperature is the color control (6,000 K to 9,000 K, assumed linear in mireds, not
  measured). The light also accepts an **RGB color**: it is snapped to the nearest color the lamp can make, using
  color temperature plus red and amber, and the snapped color is shown on the light.
- **Tuna Blue:** an RGB light whose color is snapped to the nearest color the lamp can make. The AP9X uses this
  model and channel layout.
- **Refugium:** an RGB light whose color is snapped to the nearest color the lamp can make.
- **Effects.** Storm, Cloud and Aurora on every model, plus Rainbow, Moon Red and Moon Blue on the Tuna Blue model,
  which drives UI3-UI5; a Tuna Sun does not have those channels. The standard ESPHome effects `pulse` and `flicker` are included
  too. A sunrise is a `light.turn_on` with a long `transition_length`.

An effect is a **temporary overlay** on the program. Starting one does not change the group's mode. While it runs,
the group writes only effect frames; when it finishes, is stopped or is replaced, the group goes straight back to
what it was doing (Program shows the schedule again, Off stays off, Manual returns to the held level). Cloud,
Aurora, Rainbow and the Moon effects fade in over your transition time; Storm's flashes stay hard. Standard ESPHome
effects reach the lamps at no more than 25 frames a second, and the built-in effects are written 25 times a second.

| Entity | Meaning |
|---|---|
| **effect speed** | Fast, Moderate or Slow, for the Storm, Cloud, Aurora, Rainbow and Moon effects. Remembered across restarts. |
| **effect duration** | 0 to 5 minutes in half-minute steps; `0` runs until stopped. Default 2 minutes. Remembered across restarts. |

## Mode and status

- **Mode** selects what the group does: **Program** runs the daily program, **Manual** holds what you set, **Off**
  turns the lamps off. Changing the light or a manual channel number switches the group to Manual; choosing
  Program hands control back. The mode is remembered across restarts. A held value (Manual or a point preview) is
  repeated about once a second on the bus.
- **Status** says what the group is doing right now, for example `Program · 14:02 · INT 42% · CLR 60%`,
  `Sun · 09:30 · INT 55% · CLR 24% · R 50% · AM 100% · clouds -12%`, `Program · 09:30 · INT 20% · CLR 8% · acclimating 40%`, `Manual`, `Off`,
  `Editing point 3 (previewing)`, `Effect: Pulse` or `Effect: Storm · 1:42 left`. When Sun or Moon cannot run it
  says why, for example `Program · Set a location` or `Program · waiting for clock`.

## Program

- **Program source** chooses where the schedule comes from: **My points**, **Sun** or **Moon**. A Tuna Sun and
  lights that cannot make blue offer only My points and Sun; a Tuna Blue also offers Moon. See
  [daily programs](daily-programs.md).
- **Point 1 to 16** are text entities, one per program point, in the form `HH:MM int=51 clr=12 r=10 am=30`, or
  `off`. Points 9 to 16 are *off by default*. Edits made in Home Assistant are saved to flash and take precedence
  over the shipped values.
- **Program point**, **point time**, **point enabled** and the **point INT / CLR / R / AM** numbers (on the Tuna
  Blue model: INT, CLR, violet, red, green) are the point editor: choose a point, then edit its time, whether it is on,
  and its channel values. Editing a point shows it on the lamps at once (live preview); after about a minute
  without edits the group returns to its previous mode. Changes are saved a couple of seconds later.

## Manual channels (*off by default*)

**Red** and **amber** on the Tuna Sun; **color (raw 0-255)**, **violet**, **red** and **green** on the Tuna Blue; **color (raw 0-240)**, **white** and **green** on the Refugium.
Setting one switches the group to Manual. Values are raw: INT and CLR run 0 to 255; red, amber, violet and green run
0 to 100. On the Tuna Blue the channel order follows Kessil's controller screen (violet, red, green); how
those channels map to the lamp is untested.

## Light behavior

| Entity | Meaning |
|---|---|
| **transition time** | 0 to 10 seconds; 0 changes instantly. The lamps ease in and out over this time. It is also the light's default transition length, so a command from Home Assistant or an automation without its own length uses it, and it replaces `default_transition_length` from YAML. Changes the group makes by itself fade the same way (a mode change, Off and back, resuming after a preview, an effect starting or ending). Your own live controls (the point editor, previews, manual channels) show at once, and the program's slow ramps are never delayed. |
| **brightness curve** (*off by default*) | **Linear** moves the lamp in equal steps, so Home Assistant's % equals the program's INT / 255. **Even steps (CIE 1931)** follows how bright the light looks, which makes the dim end easier to set precisely. |
| **soft start** | 0 to 120 seconds (default 30; 0 is off). Every time the controller starts, the lamps fade in from dark over this time instead of jumping to the current level. After a firmware update the lamps go dark for a moment and then rise. |

## Acclimation

| Entity | Meaning |
|---|---|
| **acclimation** | Switch. While on, the program's whole intensity is scaled up from the starting share to full. |
| **acclimation start** | 10 to 90 %, default 30. The share on the first day. |
| **acclimation days** | 1 to 90, default 14. Days until full light. |

Programs on ordinary lights have the acclimation entities too. See [daily programs](daily-programs.md#acclimation).

## Device-wide entities

| Entity | Meaning |
|---|---|
| **Send to lights** | Off is a dry run that only logs frames. On, a leader announces itself and claims the chain. It is a diagnostic entity: the setting is remembered, and after a restart an auto-writes leader claims by itself. A follower built from `common/follower-base.yaml` has no such switch, only **Bus state**. |
| **Retry** | Claims the chain again now after a conflict, without waiting for the release timer. Leaders only. |
| **Bus state** | What the chain guard is doing: `Leader: controlling`, `Leader: claiming…`, a conflict with a retry countdown, or `Follower: following the chain`. A diagnostic entity. |
| **Known lamps**, **Bus timeouts** | Diagnostics. |
| **Open esphome-klink** | A link to the device's [web page](web-pages.md). |
| **Latitude**, **Longitude** | Your location for the Sun and Moon programs. Empty until you enter a value; they override Home Assistant's home location. |

On a **follower**, a K-Link group light is read-only and shows the leader's value. Native ESPHome lights
bound with `klink: followers:` behave like ordinary lights, but their state is driven by the group
they follow. See [leader and follower](leader-follower.md).

With [`common/weather.yaml`](daily-programs.md#weather) included: **Follow weather**, **Max cloud dimming** and
**Storms from weather**.

The entities are grouped on the ESPHome device page as **Controls**, **Program**, **Light behavior**, **Sun and
moon** and **Bus and setup**.
