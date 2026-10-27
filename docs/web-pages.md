# Web pages and JSON API

The device serves two pages straight from the board; both work offline.

## The esphome-klink page: `http://<device>/klink`

Three tabs.

**Dashboard**
- Each group's mode and status. Tap a group, or one of its lamps, to flash it so you can tell which is which.
- An **Effects** row per K-Link group: run Storm, Cloud, Aurora and the other effects at Fast, Moderate or Slow for a
  few minutes or until stopped. An effect is a temporary overlay: the program carries on where it left off.
- A line showing the acclimation share while acclimation is on.

**Program**
- A **source switch** (My points, Sun, Moon) for groups that support it. With My points, the curve is a visual
  day editor: drag points, watch the change preview on the lamps, use the "now" marker and scrub through the day.
  Color programs get a hue/saturation picker for a point's color; color-temperature programs a warm-to-cool strip.
- With Sun or Moon, the curve is today's generated day, read-only. Date shortcuts (today, the equinoxes and
  solstices, a date picker) preview other days. A summary line gives sunrise, noon and sunset, or moonrise, moonset
  and how much of the moon is lit. Sun and moon settings sit under the curve, **Make an editable copy** copies the
  day into My points, and a location line shows where the sky is computed for.
- Presets: **Default**, **Daytime plateau** and **Always on**. Undo, redo and revert, and a button to copy a
  program to another group. **Done** ends a preview.

**Settings**
- The lamp list with nicknames, moving a lamp to another group, and rescan; **Send to lights** and **Retry**.
- A **Light behavior** card per K-Link group (brightness curve, transition time, soft start), an **Acclimation** card
  per program, and a **Location** card.

Light programs (ids 101 to 164) appear in the Program tab too, with channels that follow the light's type, and no
lamp chips or identify buttons since they have no lamps on the bus.

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
| `GET /klink/state` | | Everything the page shows: groups (mode, status, points, defaults, lamps, effects, output options, acclimation, source, sun/moon settings and today's generated day), the lamp list, the location (rounded to 0.01 degrees) and the bus state. |
| `GET /klink/generated` | `gid`, `source=sun\|moon`, `date=YYYY-MM-DD` | The points and an event summary for any day. Answers `{"pending":true}` while it computes; ask again after a moment. 409 when no location is set. |
| `GET /klink/snap` | `gid`, `rgb=RRGGBB` | The closest settings the lamp can make for a color. Read-only. |
| `POST /klink/mode` | `gid`, `mode=program\|manual\|off` | Sets the mode. |
| `POST /klink/point` | `gid`, `slot` (1-based), any of `minute`, `int`, `clr`, `r`, `am`, `c1`-`c5`, `enabled` | Edits a point. `c1`-`c5` address UI1-UI5 for any model. |
| `POST /klink/preview` | `gid`, `slot` or `minute` | Shows a point, or a time of day, on the lamps; returns to the previous mode after a minute. |
| `POST /klink/preview/done` | `gid` | Ends a preview. |
| `POST /klink/copy` | `from`, `to` | Copies a program to another group of the same model and size. |
| `POST /klink/source` | `gid`, `source=points\|sun\|moon` | Chooses the program source. |
| `POST /klink/sky` | `gid`, any of `peak`, `hours`, `ramp`, `glow`, `amber`, `shift`, `moon`, `phase` | Sun and moon settings. |
| `POST /klink/source/copy` | `gid` | Copies today's generated day into My points. |
| `POST /klink/output` | `gid`, any of `curve=0\|1`, `seconds=0..10`, `soft_start=0..120` | Brightness curve, transition time and soft start. |
| `POST /klink/acclimation` | `gid`, any of `on=0\|1`, `start=10..90`, `days=1..90` | The acclimation ramp. |
| `POST /klink/effect` | `gid`, `name`, `speed`, `minutes` | Starts an effect. |
| `POST /klink/effect/stop` | `gid` | Stops the running effect. |
| `POST /klink/identify` | `kid` or `gid` | Flashes one lamp, or every lamp of a group. |
| `POST /klink/assign` | `kid`, `gid` | Moves a lamp to another group. |
| `POST /klink/name` | `kid`, `name` | Gives a lamp a nickname. |
| `POST /klink/scan` | | Scans the bus for lamps. |
| `POST /klink/writes` | `on=0\|1` | Send to lights. |
| `POST /klink/retry` | | Clears a bus conflict. |
