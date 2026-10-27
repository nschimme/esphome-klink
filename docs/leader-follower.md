# Leader and follower on one K-Link chain

K-Link is a shared bus. Use **one leader and any number of silent followers**—the same rule as one
Kessil controller per chain.

```text
Leader (ESP32) ── K-Link pair ──┬── Kessil lamp
                                ├── Kessil lamp
                                └── Follower (ESP32, RX only)
```

| Role | Transmits | Does |
|---|---|---|
| `leader` | yes | announces with command 34, claims the chain, scans, assigns, identifies, writes groups |
| `follower` | **no** | listens to command `1F` group writes and follows them |

> **Untested on hardware.** The claiming logic and the follower's color mapping are host-tested and the
> examples are config-checked in CI; a two-board bench run is still needed.

**Jump to:** [Quickstart](#quickstart) · [Claiming](#how-a-leader-claims) · [Follower outputs](#follower-outputs) · [Troubleshooting](#troubleshooting) · [Limits](#limits) · [Diagnostics](#diagnostics)

A follower is not a quiet leader: it never puts a frame on the bus. It has no **Send to lights**
switch or **Retry** button, and its `/klink` page greys out the leader-only parts (Find lamps, flashing
and moving lamps, Send to lights); do those on the leader.

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
   light(s) to drive.
5. **Wire the follower's K-Link pins** to the same pair as the lamps (receive only). **Bus state**
   reads `Follower: following the chain`, and the light changes when the leader writes the group.

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
| **esphome leader** | yes | yes | Owns the chain |
| **esphome follower** | no | no | Listens only |
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
      # transition: 0s
```

The follower derives the light class from the lights (RGB/RGBW, CT, or brightness) and maps the
source model's color curve and overlays to it. A group with no lamps works as a **bus channel**: the
leader still writes `1F`, and every follower on that GID shows it.

See [DIY lights](diy-lights.md) for refugium and planted-strip builds.

## Troubleshooting

| Symptom | Check |
|---|---|
| Follower light never changes | The follower's `gid` is a group the leader actually writes. The leader's **Bus state** must read `Leader: controlling`, not `claiming`. |
| **Bus state** says `Follower: following the chain` but nothing arrives | Wiring: the follower's D+/D- must reach the same pair as the leader. Set `klink: monitor: true` and look for `bus @…` lines. |
| Colors are wrong | `model` must be the leader group's model, not the strip's. |
| Leader stays in `claiming` | Another controller keeps writing; unplug it (see Limits). An AP9X beacon alone does not block a claim. |
| Leader keeps dropping to `conflict` | A second leader or a Kessil controller is writing. Look for it with `monitor: true`. |

## Limits

- **No automatic election.** The beacon has no identity. Two leaders can claim at once; a foreign
  write pauses both. Roles are explicit.
- **One chain per leader.** Two electrically separate chains need two leaders.
- **Followers cannot scan or assign.** Do discovery and group assignment on the leader.
- **No takeover.** A follower holds its last value if the leader disappears.
- **Controller X / Wi-Fi dongle are not peers.** Keep them off a chain driven by this board.

## Diagnostics

| Entity | Reads |
|---|---|
| **Bus state** | `Leader: controlling`, `Leader: claiming…`, a conflict countdown, or `Follower: following the chain` |
| **Send to lights** | Leader only: your intent. It stays on while a claim is pending or paused; **Bus state** says which. A board built from `common/follower-base.yaml` has no such switch. |
| **Retry** | Leader only: claims again after a conflict |
| **Monitor** | `klink: monitor: true` logs every frame a follower hears |
