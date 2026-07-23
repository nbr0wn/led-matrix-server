#!/usr/bin/env bash
# Launch one MAME game inside a virtual X screen sized to the LED panel, with
# the framebuffer written to /tmp/mmfb (the led-matrix-server MAME mode mmaps it).
# The mode fork/execs this and kills the whole process group to stop the game.
#   usage: mame-run.sh <romname> [width] [height]
ROM="$1"; W="${2:-192}"; H="${3:-128}"
FBDIR=/tmp/mmfb
# Clean up any previous session so :99 is free for a fresh Xvfb.
pkill -f "Xvfb :99" 2>/dev/null; rm -f /tmp/.X99-lock
rm -rf "$FBDIR"; mkdir -p "$FBDIR"

Xvfb :99 -screen 0 "${W}x${H}x24" -fbdir "$FBDIR" >/tmp/mm-xvfb.log 2>&1 &
XPID=$!
# clean up Xvfb whenever we leave (mame exits, or we're killed)
trap 'kill $XPID 2>/dev/null' EXIT
sleep 1.5

export DISPLAY=:99
# Run MAME in the foreground; when it exits (game quit) the EXIT trap stops
# Xvfb. If the parent kills our process group, MAME + Xvfb both get the signal.
/usr/games/mame "$ROM" \
  -rompath "$HOME/mame-roms" \
  -video soft -sound none -skip_gameinfo -nomouse -keepaspect \
  >/tmp/mm-mame.log 2>&1
