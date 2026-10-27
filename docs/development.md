# Development

## Layout

| Path | What it is |
|---|---|
| `components/klink/` | the ESPHome component: Python schema and codegen, the C++ glue, and the page sources in `web/` |
| `common/`, `boards/`, `kessil-*.yaml` | the packages, templates and top-level device configs |
| `examples/` | complete, validated example configs |
| `tests/` | host tests (C++ and Python) and the page tests (Node) |
| `tools/` | `mock_server.py` (the page against a fake device) and `ci_prepare.py` |
| `docs/` | these pages |

The component is split into **pure logic** with no ESPHome dependency, which the host tests compile directly, and
the **ESPHome glue** that wires it to the bus, the lights and the entities.

- Pure logic: `klink_frame.h`, `klink_bus.h`, `klink_guard.h` (the K-Link protocol and the chain-ownership
  state machine),
  `klink_schedule.h` (points and interpolation), `klink_mode.h`, `klink_output.h` (fades), `klink_colors.h`,
  `klink_light_map.h`, `klink_effects.h`, `klink_astro.h` (sun and moon positions), `klink_dynamic.h` (sun and moon
  days as points), `klink_sky.h`, `klink_weather.h`, `klink_acclimation.h`, `klink_inventory.h`, `klink_names.h`,
  `klink_lamp_responder.h` (a follower's lamp replies, shared by every lamp behind one UART), `klink_http.h`.
- Glue: `klink.h` / `klink.cpp` (the hub, the bus group, the native-light follower and the entities),
  `klink_program_base.h` / `.cpp` (everything a program does: points, mode, preview, sources, acclimation, status),
  `klink_light_program.h` / `.cpp`, `klink_web.cpp` (the page and the JSON API).

Rules that keep the device stable:

- Handlers in `klink_web.cpp` run on the web server's task. They only read, validate and `defer` the change to the
  main loop. Heavy work (a generated day for the date preview) also runs on the main loop, and the page asks again
  until the answer is ready.
- Do not change the protocol layer (`klink_frame.h`, `klink_bus.h`, `klink_guard.h`) without checking against a real
  lamp. `test_klink_guard.cpp` covers the ownership state machine; `test_klink_light_map.cpp` covers the
  channel-to-appearance mapping used by followers.
- Nothing generated is committed: the page is assembled and gzipped during `esphome compile`, and the color tables
  and golden vectors are generated from `klink_colors.py` at test time.

## Tests

C++ logic (needs only a compiler):

```sh
mkdir -p build
for t in tests/test_klink_*.cpp; do g++ -std=c++11 -Wall -Wextra -I components/klink "$t" -o build/t && build/t; done
```

The nearest-color search lives once in C++ (`klink_colors.h`) and once in Python (`klink_colors.py`, the reference):

```sh
python3 components/klink/klink_colors.py --emit-cpp > build/klink_color_tables.h
python3 components/klink/klink_colors.py --emit-golden > build/snap_golden.txt
g++ -std=c++11 -Wall -Wextra -I components/klink -I build tests/test_color_snap.cpp -o build/test_color_snap
build/test_color_snap build/snap_golden.txt
```

Python and page tests:

```sh
python3 tests/test_klink_colors.py
python3 tests/test_klink_timezone.py   # needs the tzdata package for the zone lookups
python3 tests/test_web_build.py
node tests/web/run.js                  # the page against a stub DOM and fetch, no browser
python3 components/klink/web_build.py --check
```

`esphome config` validates every top-level config and the examples; `esphome compile` builds them.

## The page

The sources are `components/klink/web/index.html`, `style.css` and `app.js`. Edit them directly. The component inlines
the CSS and JS, adds the generated color block and gzips the result during `esphome compile`. The gzipped page has a
budget of 40,000 bytes (`MAX_GZIP_BYTES` in `web_build.py`), checked by `web_build.py --check` and by the build.

To work on the page without a device, `tools/mock_server.py` serves it with a fake device and re-assembles the page on
every request, so a reload picks up your edits:

```sh
python3 tools/mock_server.py            # http://127.0.0.1:8080/klink
python3 tools/mock_server.py 9000       # another port
python3 tools/mock_server.py --no-location   # no location: the Sky "Set a location" state
```

The mock's generated Sky days are illustrative shapes, not real astronomy.

### Hosted demo

`tools/build_demo.py` turns the same mock into a static page for GitHub Pages: it runs
`tools/mock_server.py` in the browser through Pyodide, so the page's own code is unchanged. The demo's banner says
that nothing is connected, and the sun and moon days are illustrative. Try it locally with:

```sh
python3 tools/build_demo.py _site && python3 -m http.server -d _site
```

`.github/workflows/pages.yml` publishes `_site/` on every push to `main` and on manual dispatch. Nothing generated is
committed.

## CI

`.github/workflows/ci.yml` installs ESPHome 2026.10.0 and runs the host tests, the page tests and size check, the Python tests, `esphome config`
on every top-level config and example, and `esphome compile` for the boards it builds.
