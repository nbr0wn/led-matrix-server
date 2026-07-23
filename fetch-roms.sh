#!/usr/bin/env bash
# Fetch MAME ROMs onto this Pi, into MAME's rompath (~/mame-roms).
#
# This list is now trimmed to only the games that DON'T yet verify against the
# installed MAME (0.276) -- i.e. the ones still needing a matching-version ROM.
# Update just these on the ROM host, then re-run this; the ~20 games that
# already work are left untouched.
#
# RUN ON THE PI:  ~/led-matrix-server/fetch-roms.sh   (prompts once for the password)
#
# To re-scan good/bad after fetching:
#   cd ~/mame-roms; for z in *.zip; do g=${z%.zip}; \
#     /usr/games/mame -rompath ~/mame-roms -verifyroms "$g" 2>&1 \
#     | grep -qiE "is good|is best available" && echo "GOOD $g" || echo "bad  $g"; done
set -uo pipefail

# ============================ CONFIGURE THESE ============================
SRC_USER="nbrown"                             # your username on the ROM host
SRC_DIR="/mnt/emulation/MAME_STUFF/roms"      # directory holding the .zip files
# ========================================================================
SRC_HOST="172.16.16.16"
DEST="$HOME/mame-roms"

# Only the still-broken games (version-mismatched with MAME 0.276).
ROMS=(
  galaga     # Galaga (V)
  digdug     # Dig Dug (V)
  pengo      # Pengo (V)
  mario      # Mario Bros. (H)
  qbert      # Q*bert (H)
  bublbobl   # Bubble Bobble (H)
  gng        # Ghosts'n Goblins (H)
  blktiger   # Black Tiger (H)
  elevator   # Elevator Action (V)
)

mkdir -p "$DEST"
# Stream the listed files through ONE ssh+tar connection; --ignore-failed-read
# silently skips any that aren't present on the host.
files=""
for r in "${ROMS[@]}"; do files="$files $r.zip"; done

echo ">> Fetching ${#ROMS[@]} (still-broken) ROMs from $SRC_USER@$SRC_HOST:$SRC_DIR -> $DEST"
echo "   (enter the $SRC_HOST password once)"
ssh "$SRC_USER@$SRC_HOST" "cd \"$SRC_DIR\" && tar cf - --ignore-failed-read$files 2>/dev/null" \
  | tar xf - -C "$DEST"

echo
echo ">> Re-verifying the fetched titles against MAME 0.276:"
for r in "${ROMS[@]}"; do
  if /usr/games/mame -rompath "$DEST" -verifyroms "$r" 2>&1 | grep -qiE "is good|is best available"; then
    echo "   GOOD  $r"
  else
    echo "   bad   $r"
  fi
done
