# Configuration

## Substitutions

Set these in your own config. The `tuna_sun_gid`, `tuna_blue_gid` and `refugium_gid` entries apply to
the optional full model packages; the default leader's GIDs are set in `common/leader-groups.yaml` (1, 2 and 26).

| Substitution | Default | Meaning |
|---|---|---|
| `name`, `friendly_name` | per file | device name |
| `timezone` | `Etc/UTC` | the program runs on local time, so set yours. It is also the last-resort location for the sun and moon. |
| `tuna_sun_gid` | `"2"` | the K-Link group the Tuna Sun package controls |
| `tuna_blue_gid` | `"1"` | the K-Link group the Tuna Blue package controls |
| `refugium_gid` | `"26"` | the K-Link group the Refugium package controls |
| `klink_version` | `"0.2.0"` | follower firmware version reported in the lamp reply |
| `auto_writes` | `"false"` | `"true"`: claim the chain by itself at boot |
| `role` | `"leader"` | `"leader"` drives the chain; `"follower"` follows it and answers the leader's scan for its lamps |
| `beacon_interval` | `"1s"` | how often a leader announces itself with the K-Link presence beacon |
| `claim_confirm` | `"2s"` | how long a leader announces before its first group write |
| `guard_quiet_time` | `"3s"` | no foreign group write for this long before a leader claims |
| `conflict_release` | `"10s"` | after a foreign write while controlling, wait this long before claiming again |
| `klink_ref` | `main` | the branch or tag of this repository the component is fetched from |

## Options under `klink:`

| Option | Default | Meaning |
|---|---|---|
| `web` | `true` | serve the esphome-klink page and its JSON API at `/klink`; `false` leaves them out |
| `role` | `leader` | `leader` announces, claims and drives the chain. `follower` listens to group writes and follows them; every `followers:` entry is a lamp unless it says `lamp: false`, and a follower only answers requests addressed to it. |
| `lamp_reply_delay` | `5ms` | follower only, 0-30 ms: how long a follower waits before each lamp reply. Rejected on a leader. |
| `time_id` | `local_time` | clock for programs created automatically for a GID with lamps but no YAML group |
| `auto_writes` | `false` | claim the chain by itself at boot (leaders only) |
| `writes_enabled` | `false` | start with sending on; the default is a dry run that only logs frames (leaders only) |
| `beacon_interval` | `1s` | how often a leader sends the command-34 presence beacon while claiming or controlling |
| `claim_confirm` | `2s` | how long a leader announces itself before its first group write |
| `guard_quiet_time` | `3s` | how long no foreign group write must be heard before a leader claims. A foreign beacon alone does not count: an AP9X may keep announcing while it yields. |
| `conflict_release` | `10s` | if another controller writes while this leader is controlling, writes pause and the leader tries again after this long without a foreign write |
| `monitor` | `false` | log every frame heard on the bus, with a millisecond stamp |
| `latitude`, `longitude` | unset | a default location for the sun and moon programs (give both) |
| `timezone` | unset | the time zone whose representative city is the last-resort location (the packages pass `${timezone}`) |
| `direct_drive` | per board | the two pads that carry the bus (`positive_pin`, `negative_pin`) |
| `on_lamp_found`, `on_scan_done` | unset | automations run when a scan finds a lamp or finishes |
| `programs` | unset | [light programs](light-programs.md) |
| `followers` | unset | native ESPHome lights that follow a K-Link group. Each entry is a Kessil lamp unless it says `lamp: false`; one entry is one lamp and one GID, however many lights it lists. See [leader and follower](leader-follower.md#lamp-mode). |

Setup is automatic: **Send to lights** is off by default, and once it is on the first scan starts by itself when no
lamps are known. For scripting, the actions `klink.scan`, `klink.identify`, `klink.stage_assignment` and
`klink.confirm_assignment` are available.

A leader announces itself with the K-Link presence beacon, so an AP9X that is acting as the chain controller hands
control over. The beacon commands no lamp. A Spectral Controller X or Wi-Fi dongle does not hand over; their group
writes still pause this leader. See [leader and follower](leader-follower.md) for multiple esphome-klink devices.

## Lamp groups and templates

The default leader includes `common/leader-groups.yaml`: three slim model programs at the default GIDs,
Tuna Blue (1), Tuna Sun (2) and Refugium (26). Each is a Home Assistant light with the built-in effects and a
mode select. It has no per-point, channel or datetime entities; edit the schedule, Sky, acclimation and light
behavior on the board's `/klink` page. A lamp in any other GID gets the same kind of program automatically
(web-only), using the model of the first lamp and that model's stock schedule. Its edits are stored per GID.

For the full Home Assistant point editor, include the reusable template for a group instead:

- `common/group-tuna-sun.yaml`, `common/group-tuna-blue.yaml` or `common/group-refugium.yaml` add the
  point text/number/datetime entities and the acclimation/effect controls to a YAML group;
- `common/tuna-sun.yaml`, `common/tuna-blue.yaml` and `common/refugium.yaml` are one-line packages
  around those templates, using the default GIDs.

A YAML group always wins over the automatic program for its GID. If you want the full editor on GID 1, 2
or 26, replace that entry in `common/leader-groups.yaml` rather than adding a second group with the same
GID. [`examples/minimal-leader.yaml`](../examples/minimal-leader.yaml) is the default leader; delete its
`groups:` line for a web-only leader with no Home Assistant group entities.

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
types: a Tuna Sun group contains only type-2 lamps and a Refugium group only type-3 lamps. The AP9X reports the same
type 1 as a Tuna Blue and uses the Tuna Blue package, so a type-1 group may contain Tuna Blue lamps, AP9X lamps, or
both. Move a lamp to a group in the web page's Settings tab. See
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
