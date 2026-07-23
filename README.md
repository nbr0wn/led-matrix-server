# led-matrix-server

A multi-mode animation app for the rpi-rgb-led-matrix library with a small HTTP
control server (default port 8080): a browser UI and a JSON API to pick a mode,
cycle modes, and run Lua scripts. The driver library is cloned into
`rpi-rgb-led-matrix/` on the first build.

## Modes

Modes are numbered **dynamically** by their order in the app; those numbers are
only used by the JSON API (`GET /api` reports each mode's current number). The
web UI uses names. Adding or removing a mode never invalidates these tables.

| Mode           | Description                                          |
|----------------|-----------------------------------------------------|
| Plasma         | Animated multi-sine plasma / color field.           |
| Rainbow        | Diagonal scrolling rainbow.                          |
| Bouncing Balls | Colored balls bouncing around the canvas.           |
| Starfield      | Warp starfield: stars rush out from the center as brightening streaks. |
| Fire           | Doom-style rising fire effect.                       |
| Matrix Rain    | Green "digital rain" columns.                        |
| Game of Life   | Conway's Life, cells tinted by age; auto-reseeds.    |
| Julia Set      | Animated Julia-set fractal, morphing `c` parameter.  |
| Wireframe Cube | 1–5 rotating 3D cubes of random size, angle, speed.  |
| Tunnel         | Demoscene checkerboard tunnel flying inward.         |
| Metaballs      | Organic merging/splitting colored blobs.             |
| Water Ripples  | Height-field water sim with random raindrops.        |
| Fireworks      | Launching rockets that burst into fading sparks.     |
| Lissajous      | Glowing Lissajous curves that slowly morph.          |
| Vortex         | Hypnotic rotating multi-arm spiral.                  |
| Flow Field     | Particles streaming along a noise flow field.        |
| Boids          | Flocking swarm (separation/alignment/cohesion).      |
| Langton's Ant  | Cellular automaton building emergent "highways".     |
| Voronoi        | Animated Voronoi cells with glowing edges.           |
| Kaleidoscope   | 6-fold mirrored, folded plasma.                      |
| Color Wheel    | Rotating angular hue with pulsing radial rings.      |

### Simulations (evolve on their own using randomness)

| Mode               | Description                                                   |
|--------------------|--------------------------------------------------------------|
| Ant Colony         | Ants forage with evaporating pheromone trails and haul food to the queen; piles shrink and respawn. |
| Reaction-Diffusion | Gray-Scott Turing patterns; random seeds + drifting parameters. |
| Forest Fire        | Trees grow, lightning ignites, fire fronts sweep and regrow. |
| Wa-Tor             | Fish/shark predator-prey with oscillating population waves.  |
| Crystal (DLA)      | Diffusion-limited aggregation; walkers grow a fractal, then regrow. |
| City               | Top-down city: cars on a street grid, blocks grow/redevelop buildings, day/night lights, emergency vehicles. |
| Blackboard         | A slate pans left while chalk cartoons are drawn stroke by stroke and captioned; dust falls to the tray. |
| Mine               | Side-view dig under fog of war: miners ride the shaft lift to tunnel for gold/silver/gems and bank them in surface heaps; regenerates when worked out. |
| Orbital Colony     | A modular station spins and grows by docking modules; shuttles dock, satellites orbit, point-defense zaps asteroids. |
| Space Explorer     | AI ship roams a scrolling solar system, mines planets, fights enemies, warps onward. |
| Dungeon            | Autonomous NetHack-like hero on a scrolling 64×40 map with chunky 8×8 tiles. |
| Stock Ticker       | Intraday stock chart; symbol via the API (`{"mode":<n>,"arg":"TSLA"}`), refetched every 20s. |
| Jellyfin           | Plays the best title match from a Jellyfin server (`{"mode":<n>,"arg":"The Matrix"}`). |
| MAME               | Controller-driven arcade launcher (gamepad menu → runs MAME via Xvfb). |
| Lua Script         | Runs a Lua script POSTed to `/api/lua` (or pasted in the web UI). |

Plus any **fetched modules** (extra Lua modes downloaded at runtime, listed
after the built-ins) and special **mode 0**, which cycles through the *checked*
modes — `{"mode":0,"secs":10}` sets the seconds per mode.

## Control server

Everything is controlled over HTTP on port 8080 (`-w` to change).

**JSON API.** `GET /api` returns the current state and mode list; `POST /api`
switches or cycles:

```sh
curl http://<pi>:8080/api                                       # state + mode list
curl -X POST http://<pi>:8080/api -d '{"mode":5}'               # switch to mode 5
curl -X POST http://<pi>:8080/api -d '{"mode":34,"arg":"TSLA"}' # mode 34 with an arg
curl -X POST http://<pi>:8080/api -d '{"mode":0,"secs":10}'     # auto-cycle, 10s each
```

`mode` is 1-based (matching `n` from `GET /api`); `mode:0` starts the auto-cycle,
and any explicit pick stops cycling. The reply is the new state plus `"ok":true`,
or `{"ok":false,"error":"..."}`.

**Web UI.** `http://<pi>:8080/` shows a grid of mode buttons with per-mode cycle
checkboxes, a cycle-time box, argument textboxes for arg-taking modes, a fetch
dialog for Lua modules, and a Lua script editor. The checked set, cycle time and
per-mode args persist on the Pi.

**Basic auth / HTTPS (both optional, off by default).** Set
`WEB_AUTH=user:password` in the config for HTTP basic auth. For HTTPS, build with
`make TLS=1` (static OpenSSL, cached) and set `TLS_CERT`/`TLS_KEY` to a PEM
cert + key (a self-signed cert is fine on a LAN).

## Lua modes

The **Lua Script** mode and all fetched modules run user Lua on a sandboxed
embedded Lua 5.4 — base/table/string/math only (no file/OS/network), with an
instruction-count cap that kills infinite loops.

**How a script works.** The chunk runs once to define globals, then `setup(args)`
is called once and `loop(t, frame)` every frame (`t` = seconds elapsed,
`frame` = counter). Do one-time init in `setup`, per-frame drawing in `loop`.
Drawing is into a persistent RGB buffer — call `clear()` to wipe it each frame.
Coordinates are 0-based, colours 0–255, and all arguments must be integers (wrap
in `math.floor`). Drawing API: `width height clear color setpixel line rect
circle triangle filltriangle text scrollh scrollv`; `color(r,g,b)` sets a current
pen used whenever a later call omits its own `r,g,b`.

**Arguments.** A script that sets a top-level `arghint` string takes runtime
arguments; `setup(args)` then receives them as a table of `KEY=VALUE` pairs
(quote a value to include spaces; a numeric value arrives as a number). Such a
module gets an argument textbox in the web UI and can be driven over the API:
`{"mode":<n>,"arg":"count=16 speed=60"}`. The `arghint` value is the textbox
placeholder.

**Uploading.** POST a script as the raw body to `/api/lua` (or paste it into the
web UI). It is syntax-checked, saved on the Pi, reloaded on restart, and the
panel switches to it. Reply: `{"ok":true}` or `{"ok":false,"error":"..."}`.

## Fetched modules

Extra Lua modes downloaded at runtime instead of compiled in — they run on the
same sandboxed engine. In the web UI open the fetch dialog, add a catalog URL,
tick the modules you want and press Apply (unticking + Apply removes one).
Installs take effect without a restart and survive reboots.

A catalog is a JSON manifest listing modules by `id`, `name`, `script` (and
optional `desc`/`shot`), resolved relative to the manifest URL. The default
catalog is the separate repo
[`nbr0wn/led-matrix-server-lua`](https://github.com/nbr0wn/led-matrix-server-lua)
— Lua ports of most built-in modes. Installed files live under
`~/led-matrix-server/modules/`.

## Configuration (`led-matrix-server.conf`)

`run.sh` sources this file and builds the command line from separate variables.
Copy `led-matrix-server.conf.example` to `led-matrix-server.conf` (gitignored)
and edit:

```
LED_ROWS=64
LED_COLS=64
LED_PARALLEL=2
LED_CHAIN=3
LED_SLOWDOWN_GPIO=3
```

Optional: `WEB_PORT`, `WEB_AUTH`, `TLS_CERT`/`TLS_KEY`, `VIDEO_WIDTH`/
`VIDEO_HEIGHT` (source resolution for the video modes), and secrets such as
`JELLYFIN_HOST`/`JELLYFIN_API_KEY`.

## Build & install

```sh
./build-pi.sh          # cross-compile for 64-bit Pi OS (downloads the aarch64 toolchain on first use)
./deploy.sh            # copy binary + run.sh + config to the Pi
./install-service.sh   # optional: install + enable the systemd service
```

`deploy.sh` targets `root@172.16.16.168` with `~/.ssh/id_ed25519` by default;
override with `PI_HOST` / `PI_USER` / `PI_KEY` / `PI_DEST`. Cross builds link
statically (`-static`) so the binary runs regardless of the Pi's glibc; add
`make TLS=1` for HTTPS. Running `make` on the Pi itself gives a native build.

On the Pi:

```sh
~/led-matrix-server/run.sh              # or -m <mode> / -s <speed> / -f <fps>
systemctl restart led-matrix-server     # if installed as a service
journalctl -u led-matrix-server -f      # follow the log
```

## Files & adding a mode

| Path | Purpose |
|------|---------|
| `src/led-matrix-server.cc` | The app: render loop, HTTP server (web UI + JSON API), `main`. |
| `src/common/` | `mode.h` (base class + `REGISTER_MODE`), `gfx.cc`, `registry.cc`. |
| `cpp_modes/<name>/<name>.cc` | One C++ display mode per translation unit. |
| `cpp_modes/lua/lua-engine.h` | Sandboxed Lua engine (Lua Script mode + fetched modules). |
| `src/modules/` | Fetched-module catalog: fetch, parse, install/remove. |
| `led-matrix-server.conf`, `run.sh`, `build-pi.sh`, `deploy.sh` | Config + scripts. |

To add a C++ mode, create `cpp_modes/<name>/<name>.cc` with a `Mode` subclass and
end it with `REGISTER_MODE(<0-based order>, MyMode);`. The Makefile globs
`cpp_modes/*/*.cc`, so there is no central mode list to edit; the order number
fixes the menu position (append at the end to avoid renumbering the rest).
