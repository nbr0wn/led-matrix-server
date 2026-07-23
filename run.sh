#!/usr/bin/env bash
# Run led-matrix-server with the panel flags and port from led-matrix-server.conf.
#
# Driving the GPIO requires root, so this re-execs under sudo if needed.
# Extra args (e.g. -m 3 -f 30) are passed through and override the config.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# led-matrix-server.conf holds all local settings and secrets and is NOT committed (see
# .gitignore). Copy led-matrix-server.conf.example to led-matrix-server.conf and fill it in.
if [[ ! -f "$DIR/led-matrix-server.conf" ]]; then
  echo "error: $DIR/led-matrix-server.conf not found." >&2
  echo "       cp led-matrix-server.conf.example led-matrix-server.conf   and edit it." >&2
  exit 1
fi
# shellcheck disable=SC1091
source "$DIR/led-matrix-server.conf"
# The app reads the Jellyfin server + key from the environment (nothing secret
# is baked into the binary), so forward them from the config.
export JELLYFIN_HOST="${JELLYFIN_HOST:-}" JELLYFIN_API_KEY="${JELLYFIN_API_KEY:-}"

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  exec sudo "$DIR/run.sh" "$@"
fi

cd "$DIR"   # so the stock ticker finds fonts/ via a relative path

# Build the --led-* flags from the separate config variables.
LED_ARGS="--led-rows=${LED_ROWS:-64} --led-cols=${LED_COLS:-64}"
LED_ARGS="$LED_ARGS --led-parallel=${LED_PARALLEL:-2} --led-chain=${LED_CHAIN:-3}"
LED_ARGS="$LED_ARGS --led-slowdown-gpio=${LED_SLOWDOWN_GPIO:-3}"

# Optional HTTP basic auth: set WEB_AUTH=user:password in led-matrix-server.conf to enable.
# Passed via the environment (not -A) so the password doesn't show up in `ps`.
[[ -n "${WEB_AUTH:-}" ]] && export MATRIX_WEB_AUTH="$WEB_AUTH"

# Optional HTTPS: set TLS_CERT and TLS_KEY (PEM paths) in led-matrix-server.conf. Needs a
# TLS-enabled binary (built with `make TLS=1`). Unset = plain HTTP.
TLS_ARGS=()
[[ -n "${TLS_CERT:-}" ]] && TLS_ARGS=(-c "$TLS_CERT" -k "$TLS_KEY")

# shellcheck disable=SC2086
exec "$DIR/led-matrix-server" $LED_ARGS \
  -w "${WEB_PORT:-8080}" "${TLS_ARGS[@]}" "$@"
