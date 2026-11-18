#!/usr/bin/env bash
set -euo pipefail

# Build simulator/launcher/sim_launcher (GTK3). Run from repo root or any directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT="$SCRIPT_DIR/sim_launcher"

cd "$REPO_ROOT"

sources=(
  "simulator/launcher/sim_launcher.c"
)

# Include other launcher modules (config/proc/profile/firmware_mgr/validate/board_catalog, etc.)
shopt -s nullglob
for src in "$SCRIPT_DIR"/*.c; do
  base="$(basename "$src")"
  [[ "$base" == "sim_launcher.c" ]] && continue
  sources+=("simulator/launcher/$base")
done
shopt -u nullglob

# Embedded sim host (cosim + SDL canvas) when available
if [[ -f "simulator/emu/sim_host.c" ]]; then
  sources+=("simulator/emu/sim_host.c")
fi

echo "Building $OUT"
gcc -O2 -Wall -Wextra -I"$REPO_ROOT/simulator/emu" -o "$OUT" "${sources[@]}" $(pkg-config --cflags --libs gtk+-3.0 gdk-x11-3.0 sdl2)
echo "Built: $OUT"

# Translations: po/<lang>.po -> locale/<lang>/LC_MESSAGES/sim_launcher.mo (generated, not tracked)
if command -v msgfmt >/dev/null 2>&1; then
  shopt -s nullglob
  for po in "$SCRIPT_DIR"/po/*.po; do
    lang="$(basename "$po" .po)"
    mkdir -p "$SCRIPT_DIR/locale/$lang/LC_MESSAGES"
    msgfmt -o "$SCRIPT_DIR/locale/$lang/LC_MESSAGES/sim_launcher.mo" "$po"
  done
  shopt -u nullglob
else
  echo "warning: msgfmt not found; translations (locale/) not built" >&2
fi
