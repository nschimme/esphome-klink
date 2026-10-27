# Daily programs

A group runs a daily program: a short list of **points**, each with a time of day and values for the lamp's
channels. The lamps move in straight lines between points, wrapping past midnight. Where the points come from is
the group's **program source**: your own points, a computed sun day, or a computed moon night. On top of that you
can scale the intensity for a new tank (acclimation) and let the weather dim the sun.

## My points

A point is `HH:MM` plus values, written as `key=value` pairs. Anything you leave out is 0.

| Model | Keys | Ranges |
|---|---|---|
| Tuna Sun | `int` `clr` `r` `am` | int and clr 0-255; red and amber 0-100 |
| Tuna Blue | `int` `clr` `vio` `r` `grn` | int and clr 0-255; violet, red and green 0-100 |
| Refugium | `int` `clr` `w` `grn` | int 0-255; clr 0-240 (blue to red); white and green 0-100 |

Examples: `09:00 int=51 clr=12 r=10 am=30` (Tuna Sun), `13:00 int=153 clr=178 vio=60 r=5 grn=5` (Tuna Blue or
AP9X), `09:00 int=102 clr=150 w=10` (Refugium). The plain five-number form `HH:MM 51 12 0 10 30` also works. `off` (or an
empty text) leaves a slot unused. Values out of range are rejected, not clamped. With one point enabled the lamp
holds it all day; with none the group has nothing to run.

**Percent or raw?** The page and the status line show intensity and color in percent. The point texts and the JSON API
use raw values: INT and CLR run to 255, so percent times 2.55 gives the raw value (60 % is 153). Red, amber, violet
and green are already 0 to 100.

`CLR` is the color axis. On the Tuna Sun, 0 is the warm end (6,000 K) and 255 the cool end (9,000 K).

The Tuna templates have **16 point slots**. You edit points in the web page's curve editor, in Home Assistant
(the point texts and the point editor entities), or by pasting texts.

### Shipped programs

Both start from a 12-hour day with a two-hour plateau at peak and a dark night. The Tuna Sun adds red and amber:
10 % red and 30 % amber at 09:00 and 19:00, 5 % and 10 % at 11:00 and 17:00, 8 % and 15 % at 13:00 and 15:00. The
Tuna Blue follows violet with intensity (60 % at the peak) with 3 % red and green at 11:00 and 17:00 and 5 % at
13:00 and 15:00. Reef keepers commonly run violet near the peak intensity and a few percent of red and green;
raise violet slowly in a new tank. Kessil describes red and amber as for the look of the tank, not for growth.
The Tuna Blue's extra channels (violet, red, green) are untested on a real lamp, so check them before you rely
on them.

| Time | Tuna Sun | Tuna Blue |
|---|---|---|
| 08:00 | `int=2 clr=0` | `int=2 clr=0 vio=1` |
| 09:00 | `int=51 clr=12 r=10 am=30` | `int=51 clr=25 vio=20` |
| 11:00 | `int=102 clr=51 r=5 am=10` | `int=102 clr=102 vio=40 r=3 grn=3` |
| 13:00 | `int=153 clr=89 r=8 am=15` | `int=153 clr=178 vio=60 r=5 grn=5` |
| 15:00 | `int=153 clr=89 r=8 am=15` | `int=153 clr=178 vio=60 r=5 grn=5` |
| 17:00 | `int=102 clr=63 r=5 am=10` | `int=102 clr=127 vio=40 r=3 grn=3` |
| 19:00 | `int=51 clr=25 r=10 am=30` | `int=51 clr=51 vio=20` |
| 20:00 | `int=0 clr=0` | `int=0 clr=0` |

The page's **Default** button restores these.

## Starting points

Suggestions to adjust, not rules. The shipped programs already follow the pattern most keepers describe: about 12 hours
with a two-hour ramp at each end and a plateau at peak.

- **Planted tank, Tuna Sun.** Use the **Sun** source if you like a day that follows the season, or **Fixed** day
  length at the hours your plants need, with a ramp of about two hours. Kessil describes red and amber as for the look
  of the tank, so keep them modest if you want, or raise them for color.
- **Reef tank, Tuna Blue.** Keepers commonly run violet near the peak intensity and a few percent of red and green,
  with a peak of six to eight hours, and raise violet slowly in a new tank. Switch on **Acclimation** for new corals.
- **A night light.** On an RGB light program, use the **Moon** source. On a Tuna Sun, use a small **Night glow** (amber)
  in the Sun settings.

## Sun and moon

Choose **Sun** or **Moon** as the program source (the Program tab's switch, or the **Program source** select in
Home Assistant). The curve is computed for today at your location, redrawn at the start of each day (the moon's
night at local noon), and never saved: only the source and its settings are. Choose **Make an editable copy** on the
Program tab to copy today's curve into My points, simplified to fit the slots.

The Sun source works on every program. The Moon source needs blue light, so it is offered on a Tuna Blue and on
RGB or RGBW light programs, and not on a Tuna Sun, tunable-white or brightness-only lights. Neither the Sun nor the
Moon source is offered on a Refugium.

### Sun

The curve follows the real sun: a dim glow from astronomical dawn, red and amber (violet on a Tuna Blue) at
sunrise, a climb to full intensity at solar noon, and the same in reverse in the evening, with a dark night. It is
up to eleven points. Events that do not happen (the sun does not rise, or does not get dark) are left out.

| Setting | Meaning |
|---|---|
| Peak brightness | Intensity at noon, in percent of full. Default 60 %, which is INT 153, the same peak as the shipped points. |
| Day length | **Real sun**, or **Fixed** at 4 to 16 hours centered on solar noon (a constant photoperiod all year). |
| Morning and evening ramp | Minutes from sunrise to near-peak intensity, and the same before sunset. Default 150 minutes; 0 follows the real sun, which climbs in under an hour. |
| Sunrise and sunset glow | Strength of the red/amber (violet on a Tuna Blue) glow, in percent. Default 100. |
| Night glow | Tuna Sun only, 0 to 10 %: a dim amber light through the night. Default 0. Amber is a very dim light, far dimmer than red, so it suits a night glow. |
| Time shift | Minutes added to every point, -120 to 120. |

On ordinary lights the sun is a warm-to-cool white (color temperature, white or RGB) with an orange glow at sunrise
and sunset.

### Moon

The curve lights only the dark hours, from moonrise to moonset clipped to civil darkness, peaking in the middle.
If the moon is not up while it is dark, the lamp stays off. Rise and set times are estimates, within about 15
minutes.

| Setting | Meaning |
|---|---|
| Maximum moonlight | Brightest moonlight, in percent of full intensity. Default 5 %. |
| Follow the moon's phase | On: brightness scales with how much of the moon is lit. Off: always full. |

On a Tuna Blue the moon uses intensity plus violet; on RGB lights it is a cool blue. Run it on a night light, for
example a light program on a blue or RGB light.

### Location

There is one location per device, so a Sun and a Moon program agree. It is the first of these that exists:

1. the **Latitude** and **Longitude** entities, if you set them;
2. Home Assistant's home location (`zone.home`), read automatically;
3. the last location Home Assistant gave, kept across restarts;
4. `latitude` and `longitude` in the `klink:` block;
5. the representative city of your `timezone` (approximate, but enough for the sun to be about right).

The web page shows which one is in use, rounded to 0.01 degrees. If none exists, the Sun and Moon sources stay off,
the group keeps running My points, and the status says "Set a location".

### Previewing other days

On the Program tab, date shortcuts (today, the equinoxes and the solstices, and a date picker) draw another day's
curve so you can see how the year changes it. A preview is read-only and never changes what the lamps do.

## Weather

Include [`common/weather.yaml`](../common/weather.yaml) and name your Home Assistant weather entity:

```yaml
packages:
  weather: !include
    file: common/weather.yaml
    vars: {weather_entity: weather.home}
```

Only the weather **condition** is used, so any weather integration works. A condition that is unknown or unavailable
counts as clear. Clouds take a share off the Sun source's intensity:

| Condition | Cloud share |
|---|---|
| sunny, clear-night, exceptional | 0 |
| windy | 0.1 |
| partlycloudy, windy-variant | 0.3 |
| cloudy | 0.6 |
| fog, snowy | 0.7 |
| rainy, hail | 0.75 |
| snowy-rainy | 0.8 |
| pouring, lightning | 0.85 |
| lightning-rainy | 0.95 |

| Entity | Meaning |
|---|---|
| **Follow weather** | Off by default. The Sun source's intensity is reduced by the cloud share times **Max cloud dimming**, easing in and out over minutes so it never flickers. The status shows `clouds -N%`. |
| **Max cloud dimming** | 0 to 90 %, default 50. The most that is taken off at a cloud share of 1. |
| **Storms from weather** | Off by default. When a thunderstorm (`lightning`, `lightning-rainy`) begins, the Storm effect starts on K-Link groups in Program mode that have it, never on light programs. At most once every 30 minutes, and never from the first value after a restart. |

For your own rules, call `light.turn_on` with `effect: Storm` from a Home Assistant automation.

## Acclimation

Acclimation helps a new tank or new corals get used to the light. While it is on, the program's whole intensity is
scaled from the **acclimation start** share (default 30 %) up to full over **acclimation days** (default 14). It
rises smoothly, second by second, starts counting when you switch it on, and switches itself off when the days are
up. The status line shows the share right now. It works with every source and on light programs. Changing the
numbers while it runs does not restart the days.

## Transitions and startup

The **transition time** makes changes ease in and out; **soft start** fades the lamps in from dark each time the
controller starts. Both are described with the other [Home Assistant entities](home-assistant.md#light-behavior).
