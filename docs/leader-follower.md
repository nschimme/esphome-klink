# Leader and follower on one K-Link chain

K-Link is a shared bus. Use **one leader and any number of followers**: the same rule as one Kessil
controller per chain. A follower follows the leader's group writes, and every `klink: followers:`
entry is a lamp unless it says `lamp: false`, so by default a follower also answers the leader's scan
for its own lamps.

```text
Leader (ESP32) ── K-Link pair ──┬── Kessil lamp
                                ├── Kessil lamp
                                └── Follower (ESP32; follows writes, answers the leader's scan for its lamps)
```

| Role | Transmits | Does |
|---|---|---|
| `leader` | yes | announces with command 34, claims the chain, scans, assigns, identifies, writes groups. Its own `followers:` entries are lamps in its own inventory, with no bus traffic. |
| `follower` | only replies to requests addressed to it | listens to command `1F` group writes and follows them; answers the leader's scan for every entry that is a lamp. With `lamp: false` on every entry, or no entries, it never transmits. |

> **Untested on hardware.** The claiming logic, the follower's color mapping and the lamp replies are
> host-tested and the examples are config-checked in CI; a two-board bench run with a real lamp is
> still needed. Not verified on hardware: the reply delay, the 10-byte `08` reply against a
> Dongle/AP9X (real fixtures send 9 bytes), the Refugium model string, a lamp unmuted at boot, and a
> multi-lamp scan sequence with a real lamp in the same KID range.

**Jump to:** [Quickstart](#quickstart) · [Claiming](#how-a-leader-claims) · [Follower outputs](#follower-outputs) · [Lamp mode](#lamp-mode) · [Troubleshooting](#troubleshooting) · [Limits](#limits) · [Diagnostics](#diagnostics)

A follower is not a quiet leader: it never puts a beacon, a request or a group write on the bus. It
only answers requests addressed to it. With `lamp: false` on every `followers:` entry (or no entries
at all) it transmits nothing at all. It has no **Send to lights** switch or **Retry** button, and its
`/klink` page greys out the leader-only parts (Find lamps, flashing and moving lamps, Send to lights);
do those on the leader.

## Quickstart

Two boards on one chain: the leader drives the Kessil lamps, the follower drives a strip.

1. **Leader.** Flash a normal package for your lamps (for example `common/tuna-sun.yaml` on top of
   `common/base.yaml`), turn on **Send to lights** and wait for **Bus state** to read `Leader: controlling`.
2. **Pick the group the follower will copy.** Either a lamp group the leader already has (its GID is
   on the leader's page), or a group with no lamps that exists only to carry a schedule:
   include `common/refugium-channel.yaml` (GID 3).
3. **Follower.** Start from [`examples/follower.yaml`](../examples/follower.yaml) or
   [`examples/diy-refugium-strip.yaml`](../examples/diy-refugium-strip.yaml). It uses
   `common/follower-base.yaml` instead of `common/base.yaml`, which sets `role: follower` for you.
4. **Bind the light.** In `klink: followers:` give the group's `gid`, the group's `model` and the
   light(s) to drive. The entry is a lamp by default; add `lamp: false` to keep it a plain follower
   that only listens.
5. **Wire the follower's K-Link pins** to the same pair as the lamps. It answers the leader's scan for
   its lamps and follows the leader's group writes. **Bus state** reads
   `Follower: following the chain`, and the light changes when the leader writes the group.

Terms used below: a *group write* is the K-Link command `1F` that sets a group's five channels; the
*beacon* is the payload-less command `34` a controller sends about once a second to say it is there;
a *foreign* write is one sent by a different controller than this board.

```yaml
substitutions:
  role: leader       # the default; common/follower-base.yaml fixes it to follower
```

## How a leader claims

| State | Enters when | Leaves when |
|---|---|---|
| `IDLE` | Writes off, or follower | Writes requested |
| `CLAIMING` | Writes requested | 2 s of 34 + 3 s with no foreign `1F` → `CONTROLLING` |
| `CONTROLLING` | Claim confirmed | A foreign `1F` → `CONFLICT` |
| `CONFLICT` | Another controller writes | 10 s with no foreign `1F` → `CLAIMING` |

1. **Announce** — send command 34 every `beacon_interval` (1 s).
2. **Confirm** — wait `claim_confirm` (2 s) before the first group write.
3. **Wait for writes** — no foreign group write for `guard_quiet_time` (3 s). A foreign beacon alone
   does not count: an AP9X may announce while yielding.
4. **Control** — write groups and keep the beacon going.
5. **Yield on a real write** — another controller's group write pauses us; retry after
   `conflict_release` (10 s) without another foreign write, with a small random spread.

| Device | Sends 34 | Sends 1F | Result |
|---|---|---|---|
| **esphome leader** | yes | yes | Owns the chain; its own lamps stay off the bus |
| **esphome follower** | no | no | Follows group writes and replies to requests addressed to it; with `lamp: false` it sends nothing |
| **AP9X** | yes | yes | Yields to our 34; may keep announcing |
| **Controller X / Wi-Fi dongle** | yes | yes | Does not yield; keep off the chain |

Example conflict timing:

| Time | Event | State |
|---|---|---|
| 0 s | A foreign `1F` arrives | `CONFLICT`, writes paused |
| 10 s | No foreign `1F` since 0 s | `CLAIMING` |
| 12 s | Claim confirmed | `CONTROLLING` |

## Follower outputs

### A read-only K-Link group light

```yaml
light:
  - platform: klink
    klink_id: kl
    name: "Tuna Sun (follow)"
    gid: 2
    time_id: local_time
    model: tuna_sun
    slots: 1
```

Fixtures on the chain are already driven by the leader. The follower light shows the leader's value and
answers a local command with it. If you include a full group template on a follower, its Program,
Mode, point and effect entities are inert; only the light follows.

### Native ESPHome lights

```yaml
packages:
  base: !include ../common/follower-base.yaml   # sets role: follower

klink:
  followers:
    - id: desk_follow
      gid: 3
      model: tuna_blue     # the LEADER group's model: what its channels 2-5 mean
      lights: [desk_light]
      lamp: false          # a plain follower of GID 3; leave it out to answer the leader's scan
      # transition: 0s
```

The follower derives the light class from the lights (RGB/RGBW, CT, or brightness) and maps the
source model's color curve and overlays to it. A group with no lamps works as a **bus channel**: the
leader still writes `1F`, and every follower on that GID shows it.

See [DIY lights](diy-lights.md) for refugium and planted-strip builds.

## Lamp mode

Every `klink: followers:` entry is a Kessil lamp, on either role, unless the entry says `lamp: false`.
One entry = one KID = one GID = one row in the leader's lamp list, however many `lights:` it lists.
Group the lights that belong together in one entry to save lamp slots: a chain is documented up to 32
lamps, bus lamps are capped at 32 and a leader's own lamps separately at 16, so local lamps never use
bus slots. Only entries with different GIDs need their own entry.

| Entry | Meaning |
|---|---|
| absent | a lamp with defaults: the usual case |
| `lamp:` (bare) or `lamp: {}` | the same defaults, written out |
| `lamp: {kid: 18000021}` | a lamp with a pinned KID |
| `lamp: false` | a plain follower of that group; it never answers the scan |

The KID otherwise comes from the chip MAC and the entry id: stable per board and per entry,
practically unique but not guaranteed. Set `kid:` if a scan ever logs an "unresolvable collision on a
single KID". A KID is `0` (derive it) or 18000000 to 36000000 inclusive, the range a leader scans;
any other value would never be found. Two entries with the same explicit `kid` are a config error.

The entry's `gid` is the initial GID and must be valid for the model's lamp type: the Tuna Blue
default 1, Tuna Sun 2, Refugium 26, or any type in the custom groups 3-25. An invalid one is a config
error; `lamp: false` is the way to follow an arbitrary group. A GID the leader assigns is stored and
wins over `gid:` after a reboot; an invalid or type-incompatible stored or received GID is ignored.

Hub option `lamp_reply_delay` (0-30 ms, default 5 ms) sets how long a follower waits before each
reply. It is rejected on a leader. A real lamp's delay is unmeasured.

The `08` firmware reply reports the klink project version from the config's `esphome: project:
version` (`0.0.0` when unset), as a 10-byte frame, not a Kessil firmware version. The `20` model reply
is `A360X Tuna Blue`, `A360X Tuna Sun` or `A360X Refugium` (the Refugium string is a guess).

```yaml
packages:
  base: !include ../common/follower-base.yaml   # sets role: follower

klink:
  lamp_reply_delay: 5ms   # follower only, 0-30 ms, default 5 ms
  followers:
    - id: led_follow
      gid: 2               # initial GID; a GID the leader assigns is stored and wins after a reboot
      model: tuna_sun      # decides the lamp type (1C) and the model string (20)
      lights: [board_led]  # every light here is part of the same lamp: one KID, one row
      # lamp:              # optional: a bare block is the default
      #   kid: 18000021    # optional; 0 (the default) derives a stable KID from the chip MAC
      # lamp: false        # a plain follower: follows GID 2 and never answers a scan
```

### On a follower

The device answers the leader's scan over the wire for all its lamps from one responder, so the
leader finds, lists, assigns and identifies them. It never sends a beacon, a group write or any
request, only replies to requests addressed to it, so the leader's conflict logic is unaffected. With
`lamp: false` on every entry (or no `followers:` entries) a follower never transmits at all.

The lamp starts unmuted, and a range query (`18`) is answered by the lowest unmuted KID in range; the
leader mutes that lamp and asks again, so a full scan still finds every lamp. Identity (`1C`) and
status (`35`) are answered only for its own KID, and the two requests without a KID (project version
`08` and model `20`) whenever one of its lamps is unmuted. A mute (`19`) matching its KID or
`FFFFFFFF` silences it and is acknowledged; an unmute (`1A`) works while muted and is not
acknowledged. Set GID (`1B`) stores the new group, which survives a reboot, and the entry's lights
follow that group.

Identify on a follower lamp needs a `34` probe heard just before the `17` find. The follower then dips
the light itself for about half a second and restores it, so no reply frame is involved. A bare `17`
without a recent probe does nothing, as on a real lamp. Like any lamp it holds its last value while
the leader's sends are off or while the leader is claiming or in conflict.

The lights in one entry share the entry's `model` as their source; the class they are driven with is
the intersection of what they support (see `KLinkLightFollower::setup()`). Brightness is always part
of it, and when every light offers both RGB and color temperature, RGB wins. Every light then gets
on/off and brightness, plus color temperature or RGB only when the shared class has it: one light
without a channel takes that channel away from the whole entry, and with no color left the entry is
brightness-only. A light with a local effect running is left alone.

### On a leader

The same entries are lamps in the leader's OWN inventory, with no bus traffic: the leader's UART
hears its own requests echoed, so a leader never answers itself. This is the same idea as an AP9X,
the closest real-world model: a controller with its own lamp that appears as a type-1 lamp. See
[`examples/leader-lamp.yaml`](../examples/leader-lamp.yaml).

- It appears in the lamp list and the group chips; the page shows it like any lamp, and its
  "firmware" is the klink project version.
- Assignment on the web Settings tab follows the usual rules: a type-compatible group, with custom
  ids 3-25 or its model's default.
- Identify dips the light for about half a second, with no bus frames.
- The lights follow their group because the leader's own writes are applied locally.
- The assigned GID is stored and wins over `gid:` after a reboot. The record is local to this
  device, so a rescan cannot leave a stale bus entry behind.

Like any lamp on the chain, the leader's own lamps hold their last value while **Send to lights** is
off or while the leader is claiming or in conflict.

`lamp_reply_delay` is rejected on a leader: it only changes how a follower answers on the wire.

## Troubleshooting

| Symptom | Check |
|---|---|
| Follower light never changes | The follower's `gid` is a group the leader actually writes. The leader's **Bus state** must read `Leader: controlling`, not `claiming`. |
| **Bus state** says `Follower: following the chain` but nothing arrives | Wiring: the follower's D+/D- must reach the same pair as the leader. Set `klink: monitor: true` and look for `bus @…` lines. |
| Colors are wrong | `model` must be the leader group's model, not the strip's. |
| A follower lamp is missing from the scan | The entry must be a lamp (`lamp:` absent or a block, not `lamp: false`). Check that its `gid` is valid for its model. |
| The scan logs an "unresolvable collision on a single KID" | Two lamps derive or pin the same KID. Set a distinct `kid:` on one entry. |
| Leader stays in `claiming` | Another controller keeps writing; unplug it (see Limits). An AP9X beacon alone does not block a claim. |
| Leader keeps dropping to `conflict` | A second leader or a Kessil controller is writing. Look for it with `monitor: true`. |

## Limits

- **No automatic election.** The beacon has no identity. Two leaders can claim at once; a foreign
  write pauses both. Roles are explicit.
- **One chain per leader.** Two electrically separate chains need two leaders.
- **Followers cannot scan or assign.** They answer the leader's scan and honor an assignment for their
  lamps, but do discovery and group assignment on the leader. A follower with `lamp: false` on every
  entry is not scanned, assigned or identified at all.
- **Lamp slots.** A chain is documented up to 32 lamps and bus lamps are capped at 32; a leader's own
  lamps have their own cap of 16, so they never use bus slots. One `followers:` entry is one lamp
  however many lights it lists.
- **No takeover.** A follower holds its last value if the leader disappears.
- **Controller X / Wi-Fi dongle are not peers.** Keep them off a chain driven by this board.

## Diagnostics

| Entity | Reads |
|---|---|
| **Bus state** | `Leader: controlling`, `Leader: claiming…`, a conflict countdown, or `Follower: following the chain` |
| **Send to lights** | Leader only: your intent. It stays on while a claim is pending or paused; **Bus state** says which. A board built from `common/follower-base.yaml` has no such switch. |
| **Retry** | Leader only: claims again after a conflict |
| **Monitor** | `klink: monitor: true` logs every frame a follower hears, including the requests a leader sends to its lamps |
| **Known lamps** | Leader only: the lamps the last scan found, including a follower's lamps that answered it |
