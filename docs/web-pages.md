# Web pages and JSON API

The device serves two pages straight from the board; both work offline.

## The esphome-klink page: `http://<device>/klink`

Three tabs.

**Dashboard**
- Each group's mode and status. Tap a group, or one of its lamps, to flash it so you can tell which is which.
- A light program's card has **Find these lights**: the lights of the program blink three times (dark, bright) and
  then return to what they showed. It works while Send to lights is off, since a light program never uses the bus.
- A **Follower** card for each `klink: followers:` entry that is not already in a group's lamp list. **Find these
  lights** flashes the bound ESPHome lights directly; it never sends a bus frame, so it works on a follower too.
- An **Effects** row per K-Link group and per light program: run Storm, Cloud, Aurora and the other effects at Fast,
  Moderate or Slow for a few minutes or until stopped. An effect is a temporary overlay: the program carries on
  where it left off. On a light program the row offers the effects that every one of its lights can show, and
  starting one starts it on all of them at once.
- A line showing the acclimation share while acclimation is on.

**Program**
- A **source switch** (My points, Sky) for groups that support it. Sky has **Sun** and **Moon** toggles; a lamp that
  cannot make blue offers only the Sun toggle. With My points, the curve is a visual day editor: drag points, watch
  the change preview on the lamps, use the "now" marker and scrub through the day. Color programs get a
  hue/saturation picker for a point's color; color-temperature programs a warm-to-cool strip.
- With Sky, the curve is today's generated day, read-only. Date shortcuts (today, the equinoxes and solstices, a
  date picker) preview other days. When both layers are on the summary line gives both the sun's events and the
  moon's rise, set and how much of it is lit. Sun and moon settings sit beside the curve (under it on a narrow
  screen), with undo, redo and revert for the settings, **Make an editable copy** copies the day into My points, and
  a location line shows where the sky is computed for. Changing a setting redraws today's and the previewed day.
- Presets: **Default**, **Daytime plateau** and **Always on**. Undo, redo and revert, and a button to copy a
  program to another group. **Done** ends a preview.

**Settings**
- The lamp list with nicknames, the type of each lamp and (after a scan) the model and firmware it reports,
  moving a lamp to another group, and **Scan for lamps**. A follower has no bus lamp list and hides this card.
- **Group settings:** one group selector drives a **Light behavior** card and an **Acclimation** card for the chosen
  group, instead of one card per group. A follower keeps only the brightness curve; transition and soft start belong
  to the chain leader, and its read-only bus groups do not show Acclimation. A GID with lamps but no group package
  gets an automatic program from its lamp model, so it appears here; the hint is a fallback while that program is
  being created.
- **Device:** location and group names in one collapsed card, plus **Bus**: Send to lights, Retry and **Scan for lamps**.
  A follower hides the Bus card; the header pill shows its bus state and the lamp card shows the lamp count.

Light programs (ids 101 to 164) appear in the Program tab too, with channels that follow the light's type. They have
no lamp chips, but **Find these lights** works on them and their card carries the same Effects row.

## ESPHome's device page: `http://<device>/`

ESPHome's own page (`web_server` version 3, `local: true`) lists every entity, grouped as **Controls**, **Program**,
**Light behavior**, **Sun and moon** and **Bus and setup**.

Set `web: false` under `klink:` to leave out the esphome-klink page and its JSON API. Adding
`web_server: auth: {username: ..., password: ...}` in your own config protects both pages.

## JSON API

The page uses this API, and you can too. Group ids are `gid`: 1 to 40 for K-Link groups, 101 to 164 for light
programs. Every `POST` answers `{"ok":true}` or an error status with a message (400 bad parameter, 404 unknown
group, 409 refused). State changes run on the device's main loop.

| Request | Parameters | What it does |
|---|---|---|
| `GET /klink/state` | | Everything the page shows: groups (mode, status, points, defaults, lamps, effects, output options, acclimation, source, Sky layers/settings and today's generated day), the lamp list, the location (rounded to 0.01 degrees) and the bus state. |
| `GET /klink/generated` | `gid`, `source=sky`, `date=YYYY-MM-DD` | The points and an event summary for any day. The summary has the sun's events and, when the Moon layer is on, a `moon` object. Answers `{"pending":true}` while it computes; ask again after a moment. 409 when no location is set. |
| `GET /klink/snap` | `gid`, `rgb=RRGGBB` | The closest settings the lamp can make for a color. Read-only. |
| `POST /klink/mode` | `gid`, `mode=program\|manual\|off` | Sets the mode. |
| `POST /klink/point` | `gid`, `slot` (1-based), any of `minute`, `int`, `clr`, `r`, `am`, `c1`-`c5`, `enabled` | Edits a point. `c1`-`c5` address UI1-UI5 for any model. |
| `POST /klink/preview` | `gid`, `slot` or `minute` | Shows a point, or a time of day, on the lamps; returns to the previous mode after a minute. |
| `POST /klink/preview/done` | `gid` | Ends a preview. |
| `POST /klink/copy` | `from`, `to` | Copies a program to another group of the same model and size. |
| `POST /klink/source` | `gid`, `source=points\|sky` | Chooses My points or the Sky program. |
| `POST /klink/sky` | `gid`, any of `layers`, `peak`, `hours`, `ramp`, `glow`, `amber`, `shift`, `moon`, `phase` | Sky layers (`3` both, `1` Sun only, `2` Moon only) and the Sun and Moon settings. |
| `POST /klink/source/copy` | `gid` | Copies today's generated day into My points. |
| `POST /klink/output` | `gid`, any of `curve=0\|1`, `seconds=0..10`, `soft_start=0..120` | Brightness curve, transition time and soft start. |
| `POST /klink/acclimation` | `gid`, any of `on=0\|1`, `start=10..90`, `days=1..90` | The acclimation ramp. |
| `POST /klink/effect` | `gid`, `name`, `speed`, `minutes` | Starts an effect. |
| `POST /klink/effect/stop` | `gid` | Stops the running effect. |
| `POST /klink/identify` | `kid`, `gid` (a bus group, or a light program 101-164) or `follower` (a `followers:` entry id) | Flashes one lamp, every lamp of a group, the lights of a program, or the lights of one follower. On a follower, `kid` and `gid` flash the board's own matching lights instead of requiring bus writes. |
| `POST /klink/assign` | `kid`, `gid` | Moves a lamp to another group. |
| `POST /klink/name` | `kid`, `name` | Gives a lamp a nickname. |
| `POST /klink/groupname` | `gid`, `name` | Names a group on the page (24 bytes at most; an empty name restores the configured name). Home Assistant keeps the name from the YAML. |
| `POST /klink/scan` | | Scans the bus for lamps. 409 when the leader is not controlling it (for example Send to lights is off); a follower answers 400. |
| `POST /klink/writes` | `on=0\|1` | Send to lights. |
| `POST /klink/retry` | | Claims the chain again after a conflict. |
