# Configuration

## Substitutions

Set these in your own config.

| Substitution | Default | Meaning |
|---|---|---|
| `name`, `friendly_name` | per file | device name |
| `timezone` | `Etc/UTC` | the program runs on local time, so set yours. It is also the last-resort location for the sun and moon. |
| `tuna_sun_gid` | `"2"` | the K-Link group the Tuna Sun package controls |
| `tuna_blue_gid` | `"1"` | the K-Link group the Tuna Blue package controls |
| `refugium_gid` | `"26"` | the K-Link group the Refugium package controls |
| `auto_writes` | `"false"` | `"true"`: listen first and take over the bus when it is quiet |
| `klink_ref` | `main` | the branch or tag of this repository the component is fetched from |

## Options under `klink:`

| Option | Default | Meaning |
|---|---|---|
| `web` | `true` | serve the esphome-klink page and its JSON API at `/klink`; `false` leaves them out |
| `auto_writes` | `false` | take over the bus by itself once it has been quiet (overrides the **Send to lights** switch) |
| `writes_enabled` | `false` | start with sending on; the default is a dry run that only logs frames |
| `guard_quiet_time` | `15s` | how long the bus must be free of other controllers before writes start, and stay stopped after one is heard |
| `monitor` | `false` | log every frame heard on the bus, with a millisecond stamp |
| `latitude`, `longitude` | unset | a default location for the sun and moon programs (give both) |
| `timezone` | unset | the time zone whose representative city is the last-resort location (the packages pass `${timezone}`) |
| `direct_drive` | per board | the two pads that carry the bus (`positive_pin`, `negative_pin`) |
| `on_lamp_found`, `on_scan_done` | unset | automations run when a scan finds a lamp or finishes |
| `programs` | unset | [light programs](light-programs.md) |

Setup is automatic: **Send to lights** is off by default, and once it is on and the bus has been quiet, the first scan
starts by itself when no lamps are known. For scripting, the actions `klink.scan`, `klink.identify`,
`klink.stage_assignment` and `klink.confirm_assignment` are available.

## Lamp groups and templates

`common/tuna-sun.yaml` and `common/tuna-blue.yaml` are the model packages: one group each, using the default group
ids. They are thin wrappers around the reusable templates `common/group-tuna-sun.yaml` and
`common/group-tuna-blue.yaml`, which you can include directly to add more groups.

### Several groups on one board

Include the template once per group, with three package vars:

- `id`: the yaml/C++ id of the light, also used as every `group:` reference (for example `reef_5`);
- `name`: the display-name prefix of the entities (for example `Reef 5`);
- `gid`: the K-Link group id.

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

Custom groups use K-Link group ids **3 to 25**; ids 1, 2 and 26 are the defaults of the Tuna Blue, Tuna Sun and
Refugium packages (change those with `tuna_blue_gid`, `tuna_sun_gid` and `refugium_gid`). A group cannot mix lamp
types: a Tuna Sun group contains only Tuna Sun lamps, a Tuna Blue group only Tuna Blue lamps, and a Refugium group
only Refugium lamps. Move a lamp to a group in the web page's Settings tab. See
[`examples/two-groups.yaml`](../examples/two-groups.yaml) and
[`examples/three-groups.yaml`](../examples/three-groups.yaml).

Keep `id` and `name` stable once a group is in use: entity names and ids are built from them, so renaming a group
changes the Home Assistant entities and loses the values stored for the old ones.

## Weather

[`common/weather.yaml`](../common/weather.yaml) reads a Home Assistant weather entity; see
[daily programs](daily-programs.md#weather).

## On-board RGB LED

`common/rgb-led.yaml` adds the board's single WS2812 as a plain **light**. It is not included by default and the
program does not drive it unless you add a [light program](light-programs.md) for it. The pin comes from `rgb_pin` in
the board file: C3 GPIO8, C6 GPIO8, S2 GPIO18, S3 GPIO48 (the DevKit boards; unverified except the C6, and S3 v1.1
boards use 38). Override `rgb_pin` if your board differs:

```yaml
packages:
  rgb: !include common/rgb-led.yaml
```
