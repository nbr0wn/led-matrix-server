# led-matrix-server

A multi-mode animation app for the rpi-rgb-led-matrix library, with a small
HTTP control server (default port 8080): a browser web UI to pick a mode and
paste a Lua script, plus a JSON API (`GET`/`POST /api`) for scripting mode
switches. Rendering uses an off-screen `FrameCanvas` + `SwapOnVSync()` for
smooth double buffering. Modeled after the programs in
`rpi-rgb-led-matrix/examples-api-use/`. The driver library is cloned
automatically into the `rpi-rgb-led-matrix/` subdirectory on first build.

## Modes

Modes are numbered **dynamically** by their order in the app, and those numbers
are only used by the JSON API — `GET /api` reports the current number for each
mode. The web UI uses names, not numbers. So this list is unnumbered; adding or
removing a mode never invalidates the docs.

| Mode           | Description                                          |
|----------------|-----------------------------------------------------|
| Plasma         | Animated multi-sine plasma / color field.           |
| Rainbow        | Diagonal scrolling rainbow.                         |
| Bouncing Balls | Colored balls bouncing around the canvas.           |
| Starfield      | Warp starfield: stars rush out from the center as brightening streaks (matches the Lua-mode default). |
| Fire           | Doom-style rising fire effect.                      |
| Matrix Rain    | Green "digital rain" columns.                       |
| Game of Life   | Conway's Life, cells tinted by age; auto-reseeds.   |
| Julia Set      | Animated Julia-set fractal, morphing `c` parameter. |
| Wireframe Cube | 1–5 rotating 3D cubes of random size, angle, and speed. |
| Tunnel         | Demoscene checkerboard tunnel flying inward.        |
| Metaballs      | Organic merging/splitting colored blobs.            |
| Water Ripples  | Height-field water sim with random raindrops.       |
| Fireworks      | Launching rockets that burst into fading sparks.    |
| Lissajous      | Glowing Lissajous curves that slowly morph.         |
| Vortex         | Hypnotic rotating multi-arm spiral.                 |
| Flow Field     | Particles streaming along a noise flow field.       |
| Boids          | Flocking swarm (separation/alignment/cohesion).     |
| Langton's Ant  | Cellular automaton building emergent "highways".    |
| Voronoi        | Animated Voronoi cells with glowing edges.          |
| Kaleidoscope   | 6-fold mirrored, folded plasma.                     |
| Color Wheel    | Rotating angular hue with pulsing radial rings.     |

### Simulations (stochastic — evolve on their own using randomness)

| Mode              | Description                                                        |
|-------------------|-------------------------------------------------------------------|
| Ant Colony        | Ants forage with evaporating pheromone trails and haul food back to the queen; foragers that hunt too long give up on the trail and beeline to the nearest source, so none get stranded. She banks the harvest and turns the surplus into brood. Piles visibly shrink as they are carried off, then respawn elsewhere. |
| Reaction-Diffusion| Gray-Scott Turing patterns; random seeds + drifting parameters.   |
| Forest Fire       | Trees grow, lightning ignites, fire fronts sweep and regrow.      |
| Wa-Tor            | Fish/shark predator-prey with oscillating population waves.       |
| Crystal (DLA)     | Diffusion-limited aggregation; random walkers grow a fractal, then regrow. |
| City              | Top-down city: cars drive/turn on a street grid, blocks grow buildings (redevelop w/ cranes), parks, day/night lights windows + streetlights + headlights, emergency vehicles. |
| Blackboard        | A slate pans slowly left while an unseen hand keeps it full: line-art cartoons (27 kinds, two lanes + occasional full-height pieces) drawn stroke by stroke in wobbly, grainy chalk, captioned in a stroke-cut alphabet; chalk dust falls to the tray. |
| Mine              | Side-view dig: a pit-head winding wheel + shaft cart, miners tunnel to ore veins (coal/iron/gold/gems), bank it; lava seeps, water floods, rock caves in, day/night sky. |
| Orbital Colony    | A modular station spins & grows by docking modules (habitats, sun-tracking solar wings, radiators, greenhouses), shuttles dock, satellites orbit, point-defense zaps asteroids; rotating planet w/ terminator + city lights. |
| Space Explorer    | AI ship roams a scrolling solar system, mines planets, fights enemies, warps onward. |
| Dungeon           | Autonomous NetHack-like hero, chunky 8×8 tiles on a big 64×40 map, scrolling camera. |
| Stock Ticker      | Intraday stock chart; symbol set via the API (`{"mode":<n>,"arg":"TSLA"}`), refetched every 20s. |
| Jellyfin          | Searches a Jellyfin server for a title and plays the best match (`{"mode":<n>,"arg":"The Matrix"}`). |
| MAME              | Controller-driven arcade launcher (gamepad menu → runs MAME via Xvfb). |
| Lua Script        | Runs a Lua script POSTed to `/api/lua` (or pasted in the web UI); draws via `setup()`/`loop()`. |

Plus any **fetched modules** — extra Lua modes downloaded from a catalog URL at
runtime, listed after the built-ins. See "Fetched modules" below.

Plus special **mode 0** — cycles through the **checked** modes (chosen in the web
UI, default all); seconds per mode via `{"mode":0,"secs":10}`. See "Auto-cycle" below.

**Space Explorer** is an autonomous solar-system sim on a large
560×400 world with a camera that scrolls to follow the ship (windowed like the
dungeon). The AI ship visits **planets** to restock, fights enemy ships, and
jumps through a **warp gate** to the next, harder system. Key mechanics:

- **Two planet types** — **green** planets give **fuel**, **amber** planets give
  **bullets** (ammo). When the ship runs low on either it breaks off to find the
  matching planet (or the gate). Each planet holds a **finite reservoir** shown
  by a **progress bar above it** that drains as the ship mines it; once empty the
  planet dims and the ship must look elsewhere. Planets are drawn as shaded,
  textured, banded spheres (some with Saturn-style rings) for a bit more life.
- **Forward-only gun** — the ship fires straight ahead, so it must *rotate to
  aim* at an enemy before it can shoot. It only engages when it **has bullets**;
  when empty it **flees** and heads to an ammo planet instead of uselessly
  circling (which was getting it killed).
- **Planet guardians** — frontier planets are defended by enemy ships (red
  pulsing ring = locked) that must be destroyed before the ship can mine them.
  The two starter planets stay open as a safe resupply.
- **Hidden warp gate** — the gate isn't shown or targeted until the ship's
  sensors discover it; the ship *roams the map searching* (while refuelling and
  fighting) and only makes for the gate once found.

HUD: green fuel bar + red health bar across the top, yellow ammo pips
(bottom-left), cyan system pips (bottom-right). Destroyed → it explodes and
respawns a fresh run.

**Dungeon** is an autonomous roguelike with chunky 8×8 tiles
on a 64×40-tile map; a 24×16-tile viewport scrolls to keep the hero centered, so
it explores a dungeon bigger than the display. Detailed native 8×8 sprites: the
hero is a little **man**, potions are **flasks**, the stairs are **steps**, and
there are **14 distinct monster shapes** (spider, bat, snake, ghost, slime,
skeleton, dragon, eyeball, rat, bird, golem, scorpion, wraith, mushroom). Floors
are always black; walls get a **speckle/dot/brick/cave texture** and a distinct
dim base color per level. Pickups: green **health cross** (heals 50%), orange
**strength sword** (+attack), pink **heart** (+max HP), blue **shield** (+armor).
Powerups persist so the hero grows stronger, but each floor is deadlier, so runs
still end. If its path is blocked it fights toward the nearest monster (or takes
a free step) rather than freezing.

**Stock Ticker** plots an intraday chart (green up / red down) with a
dashed previous-close baseline and a header line (symbol, price, % change). A
background thread refetches every 20s via `curl` (Yahoo Finance chart API);
`range=1d` returns the latest session, so **after hours / weekends it shows the
previous day's data** (with a 5-day fallback if the day is empty). Set the
symbol as an argument: `{"mode":<n>,"arg":"TSLA"}` (n from `GET /api`). Requires `fonts/6x10.bdf` (deployed by
`deploy.sh`) for the text and internet access on the Pi.

**Jellyfin** searches a Jellyfin media server for a title and plays
the best match on the panel. Give the title as the `arg`, e.g.
`{"mode":<n>,"arg":"The Matrix"}` (n from `GET /api`; no arg plays the first item
on the server). The title may be **quoted** — `<n> "Blade Runner 2049"` — so
titles containing/ending in numbers work. Up to **two trailing times set the
start and end offsets** — each written as plain seconds, `m:ss` or `h:mm:ss`.
`arg="The Matrix 120"` starts two minutes in; `arg='"Blade Runner 2049" 1:30 2:00'`
**loops just that 30-second segment**. With no end offset the movie plays to its
end and then **restarts at the start offset**. Changing the offsets while a video
is playing takes effect immediately. While ffmpeg is loading, the panel shows the
**movie title and start time** (`time: hh:mm:ss`, or `loop: hh:mm:ss-hh:mm:ss`).

The wrap is **seamless and frame-exact**. A transcode job is a one-way pipe — it
cannot be rewound and reused — so looping needs a fresh one, and that takes a few
seconds to start. The first pass banks the opening `kCacheSecs` (8 s, ~20 MB of
decoded frames); every later pass replays those from RAM while the new transcode
spins up behind them.

The restart asks for the **same** start offset each time, not the join point, and
then **throws away exactly as many frames as the buffer holds**. Frame *k* of the
new stream is therefore frame *k* of the buffer, and the two line up frame for
frame. Asking the server to seek straight to the join point cannot be exact — it
snaps to a keyframe and lands a few frames off, which shows as a jump. To let the
discard finish inside the buffer's 8 s the stream must run faster than realtime,
so there is no `-re`: playback is paced on a monotonic clock in `WaitFrame` and
the pipe (enlarged to 4 MB, since a 64 KB default cannot hold even one frame)
back-pressures ffmpeg whenever the panel is the slower end. A segment shorter
than the buffer is held whole and looped from RAM with no server traffic at all.

A background thread searches the REST API with `curl` via the relevance-ranked
`/Search/Hints` endpoint (preferring an exact title match), then asks Jellyfin to
**transcode server-side** to ~256×144 h264 (`stream.ts`, with `startTimeTicks`
for the start offset), so the Pi only decodes a tiny stream instead of the
possibly-4K original. Each playback gets a fresh `PlaySessionId` and forces a
real transcode (`EnableAutoStreamCopy=false`), and the mode issues `DELETE
/Videos/ActiveEncodings` **before every stream, after every stream, and when the
panel switches away** — once by `playSessionId` and once by `deviceId`, since the
server matches on whichever it is given, then waits ~0.5 s for the kill to land.
While a transcode job for an item is alive the server replays that job's output
from the top, so re-asking for the *same* movie at a new offset played from the
beginning; killing the job (which also deletes its output) is what makes a
changed start time take effect, and it stops a stray server-side ffmpeg
outliving the mode. `ffmpeg` scales the stream to **fill**
the panel (cover + center-crop, so there are no letterbox bars; `-re` real-time,
`-t` for the end offset) and the mode blits the frames. Looping is done by
re-opening the stream, since `-stream_loop` cannot rewind an HTTP input. It
pauses ffmpeg when the mode isn't on screen and resumes when re-selected.
Requires `ffmpeg` on the Pi and network access to the server; host/key are
constants in `JellyfinMode` (currently `172.16.16.17:8096`).

**Lua Script** runs a user-uploaded Lua script that drives the display
through primitive functions. POST the script as the raw body to `/api/lua`:

```sh
curl --data-binary @myscript.lua http://<pi>:8080/api/lua
```

The reply is `{"ok":true}` (the script is now running) or
`{"ok":false,"error":"..."}` on a syntax or first-frame runtime error. A
successful upload switches the panel to the Lua mode so you see it immediately.
The last accepted script is saved to `~/led-matrix-server/lua_script.lua` and
reloaded on restart. You can also paste it into the textbox in the web UI (see
below), which persists it the same way.

The script runs into a persistent RGB buffer. The chunk runs once to define
globals; then **`setup()` is called once** and **`loop(t, frame)` is called
every frame** (t = seconds elapsed, frame = frame counter). Globals set in
`setup()` persist into `loop()` by normal Lua scoping — do your one-time
initialization in `setup()` and your per-frame drawing in `loop()`. It's
sandboxed (base/table/string/math only — no file/OS access) and an
instruction-count cap kills infinite loops. API (colors 0–255, coords 0-based):

```
w = width()   h = height()
clear([r,g,b])                        -- fill buffer (default black)
color(r,g,b)                          -- set the pen (see note below)
setpixel(x,y[,r,g,b])
line(x0,y0,x1,y1[,r,g,b])
rect(x,y,w,h[,r,g,b])                 -- filled
circle(x,y,rad[,r,g,b])               -- filled disc
triangle(x0,y0,x1,y1,x2,y2[,r,g,b])   -- outline
filltriangle(x0,y0,x1,y1,x2,y2[,r,g,b]) -- filled
text(x,y,str[,r,g,b])                 -- 6x10 font, y = top
scrollh(n)   scrollv(n)               -- wrap-scroll the whole buffer
```

`color(r,g,b)` sets a **current pen colour** that every later drawing call uses
when its own `r,g,b` are omitted, until the next `color()` — so you can set a
colour once and draw many shapes in it. Passing `r,g,b` to a call still overrides
the pen for that call. The pen starts white and resets to white on each reload.

**Script arguments.** A script that sets a top-level `arghint` string takes
runtime arguments; `setup(args)` then receives them as a table. Arguments are
`KEY=VALUE` pairs (quote a value to include spaces); a value that parses as a
number arrives as a number, otherwise a string. A fetched module that declares
`arghint` gets an **argument textbox** on the main page next to its name, and can
be driven over the API — `POST /api {"mode":<n>,"arg":"count=16 speed=60"}`.

```lua
arghint = "count=8 speed=30"          -- shown as the textbox placeholder
function setup(args)
  count = math.floor(tonumber(args.count) or 8)
  speed = tonumber(args.speed) or 30
end
```

Example — a warp starfield on black (also the built-in default, and what the
web UI's **Insert example** button pastes):

```lua
local N = 150
local SPEED = 3.0

local function newstar(far)
  return { x = (math.random()-0.5)*W*2, y = (math.random()-0.5)*H*2,
           z = far and math.random()*W or W,
           r = 160+math.random(0,95), g = 160+math.random(0,95), b = 200+math.random(0,55) }
end

function setup()
  W, H = width(), height()
  cx, cy = W/2, H/2
  stars = {}
  for i = 1, N do stars[i] = newstar(true) end
end

function loop(t, frame)
  clear(0, 0, 0)
  for i = 1, N do
    local s = stars[i]
    local pz = s.z
    s.z = s.z - SPEED
    if s.z <= 1 then s = newstar(false); stars[i] = s; pz = s.z + SPEED end
    local k, pk = 100/s.z, 100/pz
    local f = math.max(0, 1 - s.z/W)
    line(math.floor(cx+s.x*pk), math.floor(cy+s.y*pk),
         math.floor(cx+s.x*k),  math.floor(cy+s.y*k),
         math.floor(s.r*f), math.floor(s.g*f), math.floor(s.b*f))
  end
end
```

If no script has been uploaded yet, this default runs so the mode is never
blank. Uses an embedded Lua 5.4 (vendored under `src/lua/`, compiled into the
binary).

## Control server

Everything is controlled over HTTP on port 8080 (change with `-w`): a browser
web UI, and a JSON API for scripting. (Earlier versions had an ASCII telnet
server on port 4242; it has been removed in favour of the JSON API below.)

### JSON API

`GET /api` returns the current state and the full mode list:

```sh
curl http://<pi>:8080/api
# {"current":5,"current_name":"Fire","cycling":false,"cycle_secs":15,
#  "builtin_count":36,
#  "modes":[{"n":1,"name":"Plasma","builtin":true,"checked":true,
#            "takes_arg":false}, ...]}
```

`POST /api` switches or cycles. `mode` is 1-based (matching the `n` values from
`GET /api`); `mode: 0` starts the auto-cycle:

```sh
curl -X POST http://<pi>:8080/api -d '{"mode":5}'            # switch to mode 5
curl -X POST http://<pi>:8080/api -d '{"mode":34,"arg":"TSLA"}'  # ticker -> TSLA
curl -X POST http://<pi>:8080/api -d '{"mode":0,"secs":10}'  # cycle, 10s each
```

The reply is the new state (the same shape as `GET /api`) with an added
`"ok":true` and a human-readable `"message"`; a bad request returns
`{"ok":false,"error":"..."}`. Any explicit mode pick stops cycling. While
cycling, arg-taking modes use their saved argument, else a default (the stock
ticker defaults to **SPY**).

### Web server

The same interface is available from a browser at `http://<pi>:8080/` (change
the port with `-w`). It shows a grid of mode buttons — clicking one switches to
it. Each mode has a **checkbox** selecting whether it's in the cycle set;
**Cycle checked** cycles those at the interval in the **Cycle time (s)** box
(with a **Check / uncheck all** toggle). Modes that take an argument (Stock
Ticker, Jellyfin) appear in a separate **Modes with arguments** section, each
with its own textbox — the value is saved as you type and used whenever that
mode is selected or cycled, so a checked arg-mode cycles with your symbol/title.
The checked set + cycle time persist to `~/led-matrix-server/cycle.conf`, and the
per-mode args to `~/led-matrix-server/args.conf`. Below that is a Lua **textbox** pre-filled with the
current script; **Apply & Save** syntax-checks it, persists it to
`~/led-matrix-server/lua_script.lua`, shows any error, and jumps to the Lua mode. It's
plain HTTP (`GET /`, `POST /mode`, `POST /checks`, `POST /cyclesecs`,
`POST /args`, `POST /lua`) with no external dependencies (unless built with
`TLS=1`, which links OpenSSL — see HTTPS below), so the binary stays a single
static executable.

### Basic auth (optional)

The whole web server — the UI, the JSON API, and the Lua upload — can be put
behind HTTP basic auth. It's **off by default**. Set a user:password in
`led-matrix-server.conf` and restart:

```
WEB_AUTH=neil:s3cret
```

(or pass `-A user:password` directly). Browsers then prompt for the credentials;
scripts pass them with `curl -u user:password ...`. Requests without valid
credentials get `401 Unauthorized`. Note this is unencrypted HTTP on the LAN —
the auth stops casual access, but the password crosses the wire in base64; don't
reuse a real password, and don't expose the port to the internet. The port is
also visible to local users via `ps` when set through `-A` (the `WEB_AUTH` env
route avoids that). Put it behind HTTPS (below) if the base64 password on the
wire bothers you.

### HTTPS (optional)

The web server can serve HTTPS instead of HTTP. It's **off by default** (plain
HTTP), and requires a **TLS-enabled binary** — TLS support is opt-in at build
time so the normal build stays dependency-free:

```sh
make TLS=1        # cross-builds a static OpenSSL once (cached in openssl/)
```

Then point the server at a PEM cert + key (set both in `led-matrix-server.conf`, or pass
`-c cert.pem -k key.pem`):

```
TLS_CERT=/root/led-matrix-server/cert.pem
TLS_KEY=/root/led-matrix-server/key.pem
```

A self-signed cert is fine for a LAN panel:

```sh
openssl req -x509 -newkey rsa:2048 -nodes -days 825 \
  -keyout key.pem -out cert.pem -subj "/CN=pimatrix.local"
```

The startup log then reads `https://` and the API is reached with
`curl -k https://<pi>:8080/api` (`-k` accepts the self-signed cert). A
non-TLS binary given `-c/-k` refuses to start and tells you to rebuild with
`make TLS=1`. Switching a checkout between TLS and non-TLS needs a `make clean`
first (make doesn't notice the changed compile flags on its own).

## Files

| File            | Purpose                                                     |
|-----------------|-------------------------------------------------------------|
| `src/led-matrix-server.cc` | The app: render loop, HTTP server (web UI + JSON API), `main`. |
| `src/common/mode.h`   | What a mode compiles against: `Mode`/`TrailMode`, shared helpers, `REGISTER_MODE`. |
| `src/common/gfx.cc`   | `HueToRGB` / `DrawLine` (the helpers too big to inline).   |
| `src/common/registry.cc` | Collects the self-registered modes and hands `main` the ordered list. |
| `cpp_modes/<name>/<name>.cc` | One C++ display mode, one translation unit, one `.o`.  |
| `cpp_modes/lua/lua-engine.h` | The sandboxed Lua display engine, shared by the Lua Script mode and fetched modules. |
| `src/modules/`  | Fetched Lua modules: catalog fetch/parse, install/remove, mode wrapper. |
| `Makefile`      | Native or cross build.                                       |
| `led-matrix-server.conf`   | `--led-*` panel flags and the control-server `PORT`.        |
| `run.sh`        | Runs the binary with the config flags (re-execs under sudo).|
| `build-pi.sh`   | Cross-compiles for 64-bit Raspberry Pi OS (aarch64).        |
| `deploy.sh`     | Copies binary + wrapper + config to the Pi over SSH.        |

### Adding a mode

Create `cpp_modes/<name>/<name>.cc`:

```cpp
#include "../../common/mode.h"

class MyMode : public Mode {
public:
  const char *name() const override { return "My Mode"; }
  void Draw(Canvas *c, float t, float dt) override { /* ... */ }
};

REGISTER_MODE(36, MyMode);   // 0-based position in the menu
```

The Makefile globs `cpp_modes/*/*.cc`, so there is no build file and no central
mode list to edit. The number in `REGISTER_MODE` is what fixes the menu order:
static initializers run in an unspecified order across translation units, so
the registry sorts by it rather than trusting the link order. Inserting a mode
in the middle renumbers the ones after it, which shifts the API/web numbers
users type — appending at the end avoids that.

## Fetched modules

Extra Lua modes can be downloaded at runtime instead of compiled in. They run on
the same sandboxed engine as the **Lua Script** mode, appear after the built-ins
under **Fetched modules** (numbered 37, 38, ... in `GET /api`), and survive restarts.

In the web UI press **Modules…** to open the fetch dialog: paste a catalog URL,
press **Add catalog**, then tick the modules you want and press **Apply**. The
checkboxes always show what is currently installed, so unticking one and
applying removes it. Several catalog URLs can be registered at once.

A catalog is a JSON manifest at a URL:

```json
{
  "name": "Nick's modules",
  "modules": [
    { "id": "warp", "name": "Warp Tunnel", "desc": "Starfield in a tunnel",
      "script": "warp/warp.lua", "shot": "warp/warp.png" }
  ]
}
```

`script` and `shot` are resolved relative to the manifest URL (absolute URLs and
`/`-rooted paths work too). `id` is what a module is keyed on, so keep it stable;
`desc` and `shot` are optional. Any static file server will do. The bundled
catalog gives **each module its own directory** &mdash; `lua/<id>/<id>.lua`
and `lua/<id>/<id>.png` &mdash; but a manifest can lay its files out however
it likes, since the paths are just relative URLs.

Notes:

- **Screenshots are never downloaded to the Pi.** The tiles point the browser at
  the catalog host directly, so the images cost the Pi nothing &mdash; but they
  only show up if the browser can reach that host.
- Scripts are **syntax-checked before install**; a module that doesn't parse is
  refused and the dialog says why. A module that fails at *runtime* still
  installs, and its Lua error is shown under **Fetched modules**.
- Installing or removing takes effect **without a restart**: the render thread
  swaps the mode list between frames.
- A newly installed module joins the auto-cycle set automatically.
- **Forgetting a catalog uninstalls its modules**, so nothing is left in the mode
  list with no tile to remove it from.
- Stored under `~/led-matrix-server/modules/`: `catalogs.conf` (the URLs),
  `installed.conf` (the selection), `cache/` (manifests), `scripts/` (the `.lua`).

### The Lua module catalog

The Lua modules live in a **separate repo**,
[`nbr0wn/led-matrix-server-lua`](https://github.com/nbr0wn/led-matrix-server-lua):
a Lua port of **every built-in mode that can be expressed in the sandbox**
&mdash; 32 of the 36 &mdash; each in its own `<name>/` directory with a
`<name>.lua` and a `<name>.png` screenshot, plus a `catalog.json` manifest at
the repo root. The app fetches it from GitHub raw at a fixed, built-in URL
(`kCatalogUrl` in `src/modules/modules.cc`):

```
https://raw.githubusercontent.com/nbr0wn/led-matrix-server-lua/main/catalog.json
```

Open **Fetch LUA modules from github** in the web UI and all 32 appear as tiles;
tick the ones you want and press Apply. The C modes stay exactly where they are;
the Lua copies land after them, so you can put the same effect side by side and
compare.

Four modes have no Lua equivalent, because the sandbox has no `io`, no `os` and
no network by design: **Stock Ticker** (fetches quotes), **Jellyfin** (drives
ffmpeg), **MAME** (spawns a process and reads a framebuffer) and **Lua Script**
itself.

To regenerate after editing a script (in the `led-matrix-server-lua` repo, where
each mode's `.lua` lives in `<name>/<name>.lua`):

```sh
python3 build.py /path/to/luarun
```

`build.py` runs each script, writes its screenshot to `<name>/<name>.png`,
and emits the manifest with the per-directory paths. It writes its **actual
output** as the screenshot,
so a tile can never drift from what the module really draws. `luarun` is a small
host-side harness that links the vendored Lua and mirrors the panel's sandbox
and drawing API; a script that runs clean under it runs clean on the panel.

Porting notes, if you write your own:

- The drawing API takes **integers** &mdash; `luaL_checkinteger` rejects a float
  with a fractional part, so wrap coordinates and colours in `math.floor`.
- Per-pixel effects at 256&times;128 are too slow one pixel at a time. Compute on
  a half-res grid and fill 2&times;2 blocks with `rect()`: same look, a quarter of
  the calls. Anything trig-heavy that doesn't depend on `t` belongs in `setup()`.
- The frame buffer **persists between frames** unless you call `clear()`. Growth
  effects (Langton's Ant, DLA) exploit that and only draw what changed.
- Costs on the Pi range from negligible (Bouncing Balls, City) to roughly a third
  of a frame at 60fps for the heaviest (Voronoi, Metaballs). All 32 keep up well
  enough to look right; the busiest few simply render fewer frames per second
  than their C counterparts.

## Configuration (`led-matrix-server.conf`)

`run.sh` sources this and builds the command line from separate variables:

```
LED_ROWS=64
LED_COLS=64
LED_PARALLEL=2
LED_CHAIN=3
LED_SLOWDOWN_GPIO=3
VIDEO_WIDTH=256
VIDEO_HEIGHT=144
```

Panel setup: 6× 64×64 panels, 3 wide (chain) × 2 tall (parallel) = 192×128
canvas. `PORT` is the control-server TCP port. `VIDEO_WIDTH`/`VIDEO_HEIGHT` is
the source resolution for the video modes — Jellyfin transcodes to it
server-side (and MAME will render its virtual display at it) — passed to the
binary as `-W`/`-H`. The binary still accepts raw `--led-*` flags too.

## Cross-compile (on an x86_64 host)

```sh
./build-pi.sh        # == make cross
```

No cross packages needed: `make cross` downloads ARM's prebuilt
aarch64 toolchain (~160 MB) into the `toolchain/` subdirectory on first
use and builds with it. A system toolchain still works via
`make CROSS=aarch64-linux-gnu-`.

This rebuilds `rpi-rgb-led-matrix/lib/librgbmatrix.a` with the cross toolchain and links the
`led-matrix-server` binary. The library archive is architecture-specific — run
`make distclean` (or just `./build-pi.sh`, which cleans first) when switching
between native and cross builds.

The cross build links **statically** (`-static`), so the binary carries its
own libc/libm/libstdc++ and runs regardless of the Pi's glibc version. (A
dynamically-linked cross build would fail on the Pi whenever the build host's
glibc is newer than the Pi's — e.g. newly versioned `atan2f@GLIBC_2.43`.)
Because static NSS lookups are unreliable, the app defaults to **not** dropping
root privileges; pass `--led-drop-privs` to re-enable if your setup supports it.
Native builds (run `make` on the Pi itself) link dynamically as usual.

## Deploy to the Pi

```sh
./deploy.sh
```

Target defaults to `root@172.16.16.168` using `~/.ssh/id_ed25519`; override
with `PI_USER` / `PI_HOST` / `PI_KEY` / `PI_DEST` environment variables.

## Run (on the Pi)

```sh
~/led-matrix-server/run.sh              # uses led-matrix-server.conf (flags + port)
~/led-matrix-server/run.sh -m 5         # start in mode N (1-based menu number)
~/led-matrix-server/run.sh -s 2.0       # 2x animation speed
~/led-matrix-server/run.sh -f 30        # cap at 30 fps
```

## Switch modes over the network

From any machine that can reach the Pi, POST JSON to the API (`n` values come
from `GET /api`):

```sh
curl -X POST http://172.16.16.168:8080/api -d '{"mode":5}'           # switch
curl -X POST http://172.16.16.168:8080/api -d '{"mode":34,"arg":"TSLA"}'  # w/ arg
curl -X POST http://172.16.16.168:8080/api -d '{"mode":0,"secs":10}' # cycle
```

Or just open `http://172.16.16.168:8080/` in a browser. Press CTRL-C on the Pi
to stop the app cleanly (or `systemctl stop` if running as a service).

## Run as a service (systemd)

Install and enable the service (starts on boot, restarts on failure):

```sh
./deploy.sh            # first, so the binary + fonts are on the Pi
./install-service.sh   # copies led-matrix-server.service, enables and starts it
```

Manage it on the Pi:

```sh
systemctl status led-matrix-server
systemctl restart led-matrix-server
systemctl stop led-matrix-server
journalctl -u led-matrix-server -f
```

The unit runs `run.sh` as root from `/root/led-matrix-server` after the network is
online (so the stock ticker can fetch quotes).
