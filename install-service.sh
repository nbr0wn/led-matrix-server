#!/usr/bin/env bash
# Install and enable the led-matrix-server systemd service on the Raspberry Pi.
# Assumes the app is already deployed (run ./deploy.sh first).
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PI_USER="${PI_USER:-root}"
PI_HOST="${PI_HOST:-172.16.16.168}"
PI_KEY="${PI_KEY:-$HOME/.ssh/id_ed25519}"
SSH_OPTS=(-i "$PI_KEY" -o ConnectTimeout=10)

echo ">> Copying led-matrix-server.service to the Pi ..."
scp "${SSH_OPTS[@]}" "$DIR/led-matrix-server.service" \
  "$PI_USER@$PI_HOST:/etc/systemd/system/led-matrix-server.service"

echo ">> Enabling and (re)starting the service ..."
ssh "${SSH_OPTS[@]}" "$PI_USER@$PI_HOST" \
  "systemctl daemon-reload && systemctl enable --now led-matrix-server.service && \
   sleep 2 && systemctl --no-pager --lines=8 status led-matrix-server.service || true"

echo ">> Done. Manage it with:"
echo "     systemctl {status,restart,stop} led-matrix-server"
echo "     journalctl -u led-matrix-server -f"
