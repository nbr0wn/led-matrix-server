# Project Status — led-matrix-server (led-matrix-server)

Updated: 2026-07-20

## What this is
Multi-mode LED matrix animation app (hub75 panels) with a TCP control server,
built on the hzeller rpi-rgb-led-matrix driver library. Native (Pi) and
cross (x86_64 → aarch64, `./build-pi.sh`) builds; deploys to a Pi via
`deploy.sh`.

## Recent changes
- **Vendored the hub75 driver as a subdirectory** (was: project lived inside
  the library checkout and referenced `../`):
  - `Makefile` now auto-clones https://github.com/hzeller/rpi-rgb-led-matrix.git
    into `rpi-rgb-led-matrix/` on first build (`$(RGB_LIBDIR)/Makefile` is the
    clone marker) and builds `rpi-rgb-led-matrix/lib/librgbmatrix.a` there.
  - App objects have an order-only prerequisite on the clone so headers exist
    before compiling; `distclean` skips the lib clean if the dir is absent.
  - `deploy.sh` font paths updated to `rpi-rgb-led-matrix/fonts/`.
  - README references to `../` updated; `rpi-rgb-led-matrix/` gitignored.
- Verified: clean `make` from scratch clones + builds lib + links
  `led-matrix-server` and `panel-cal` successfully; fonts present for deploy.

- **Self-contained cross toolchain** (was: required apt-installed
  gcc-aarch64-linux-gnu):
  - `make cross` downloads ARM's prebuilt aarch64-none-linux-gnu toolchain
    (14.2.rel1, ~160 MB tarball / ~813 MB extracted) into `toolchain/` on
    first use, then re-invokes make with an absolute CROSS= prefix.
  - `build-pi.sh` now just runs `make cross`; no compiler packages needed.
  - `toolchain/` gitignored; never removed by clean/distclean (rm -rf to redo).
  - Verified: `make cross` from scratch downloads + builds; `file` confirms
    both binaries are statically linked ELF aarch64.

- **Source tree restructure**: `led-matrix-server.cc` and the vendored Lua moved
  to `src/` (`src/lua/`); `panel-cal.cc` moved to `calibration/`. Makefile
  paths (`SRC_DIR`, `CAL_DIR`, `LUA_DIR`) and README updated. Objects still
  build to the repo root / `src/lua/`; binaries land in the repo root as
  before, so `deploy.sh` is unaffected. Verified with a clean native build.

## Open items
- Nothing pending.
- Note: static-glibc link warnings for getpwnam/getgrnam (NSS) during cross
  link are expected and pre-existing; the Pi has glibc shared libs at runtime.
