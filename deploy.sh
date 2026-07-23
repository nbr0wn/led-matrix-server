#!/usr/bin/env bash
# Deploy the cross-compiled led-matrix-server app to the Raspberry Pi over SSH.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- Configurable target (override via environment) -------------------
PI_USER="${PI_USER:-root}"
PI_HOST="${PI_HOST:-172.16.16.168}"
PI_KEY="${PI_KEY:-$HOME/.ssh/id_ed25519}"
PI_DEST="${PI_DEST:-led-matrix-server}"     # relative to the Pi user's home
# ----------------------------------------------------------------------

SSH_OPTS=(-i "$PI_KEY" -o ConnectTimeout=10)

if [[ ! -x "$DIR/led-matrix-server" ]]; then
  echo "error: $DIR/led-matrix-server not built yet. Run ./build-pi.sh first." >&2
  exit 1
fi

echo ">> Creating ~/$PI_DEST (and fonts/) on $PI_USER@$PI_HOST ..."
ssh "${SSH_OPTS[@]}" "$PI_USER@$PI_HOST" "mkdir -p ~/$PI_DEST/fonts"

echo ">> Copying files ..."
# The binary may be running (systemd service) -> overwriting it in place fails
# with "text file busy". Upload to a temp name and atomically mv it over: the
# running process keeps the old inode, the new file takes the name.
scp "${SSH_OPTS[@]}" "$DIR/led-matrix-server" "$PI_USER@$PI_HOST:~/$PI_DEST/led-matrix-server.new"
ssh "${SSH_OPTS[@]}" "$PI_USER@$PI_HOST" \
  "chmod +x ~/$PI_DEST/led-matrix-server.new && mv -f ~/$PI_DEST/led-matrix-server.new ~/$PI_DEST/led-matrix-server"

scp "${SSH_OPTS[@]}" \
  "$DIR/run.sh" \
  "$DIR/led-matrix-server.conf" \
  "$DIR/mame-run.sh" \
  "$DIR/fetch-roms.sh" \
  "$PI_USER@$PI_HOST:~/$PI_DEST/"

# panel-cal binary (built alongside led-matrix-server); temp+mv like the main binary
# since it's also a running-capable executable.
if [[ -x "$DIR/panel-cal" ]]; then
  scp "${SSH_OPTS[@]}" "$DIR/panel-cal" "$PI_USER@$PI_HOST:~/$PI_DEST/panel-cal.new"
  ssh "${SSH_OPTS[@]}" "$PI_USER@$PI_HOST" \
    "chmod +x ~/$PI_DEST/panel-cal.new && mv -f ~/$PI_DEST/panel-cal.new ~/$PI_DEST/panel-cal"
fi
ssh "${SSH_OPTS[@]}" "$PI_USER@$PI_HOST" "chmod +x ~/$PI_DEST/mame-run.sh ~/$PI_DEST/fetch-roms.sh"

# The Lua modules are no longer part of this repo: they live in the separate
# nbr0wn/led-matrix-server-lua repo and the app fetches them from GitHub raw at
# runtime. Clean up any local copy an older deploy left on the Pi.
ssh "${SSH_OPTS[@]}" "$PI_USER@$PI_HOST" "rm -rf ~/$PI_DEST/lua ~/$PI_DEST/catalog" || true

# Fonts used by the stock ticker mode.
scp "${SSH_OPTS[@]}" \
  "$DIR/rpi-rgb-led-matrix/fonts/6x10.bdf" \
  "$DIR/rpi-rgb-led-matrix/fonts/5x7.bdf" \
  "$PI_USER@$PI_HOST:~/$PI_DEST/fonts/"

ssh "${SSH_OPTS[@]}" "$PI_USER@$PI_HOST" "chmod +x ~/$PI_DEST/run.sh"
echo ">> (If running as a service, restart it: systemctl restart led-matrix-server)"

echo ">> Deployed to $PI_USER@$PI_HOST:~/$PI_DEST"
echo ">> Run it on the Pi with:  ~/$PI_DEST/run.sh"
