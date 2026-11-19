#!/usr/bin/env bash
# ESP32 PC Control Deck - the single entry point.
#
#   ./start.sh                     interactive menu (areas, then numbered options)
#   ./start.sh help                full list of commands (generated from scripts/commands.sh)
#   ./start.sh AREA N [args]       run option N of AREA, e.g. ./start.sh server 2
#   ./start.sh AREA CMD [args]     run a command, e.g. ./start.sh server run --no-serial
#
# Areas: firmware, server, emu, launcher, card, test, scan, export.
# Each area lives in scripts/<area>.sh; scripts/commands.sh holds the command table, help and menu.
# Firmware defaults: ENV=es3c28p, PORT=/dev/ttyACM0. The Java host is in server/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$ROOT/firmware"
APPS="$FW/apps"
APPSTORE="$APPS/KryonOS-AppStore"
TEST_APPS="$APPS/test"
SHELL_APP="$APPS/shell"
RES="$ROOT/Resources"
SIM="$ROOT/simulator"
SIM_CARD="$SIM/SD_Card"
SERVER="$ROOT/server"
VAR_DIR="${CONTROL_DECK_VAR:-$ROOT/server/var}"
PID_FILE="$VAR_DIR/java-host.pid"
LOG_FILE="$VAR_DIR/log/java-host.log"
ENV="${ENV:-es3c28p}"
PORT="${PORT:-/dev/ttyACM0}"

for part in common firmware server card test scan export emu launcher commands; do
    # shellcheck source=/dev/null
    . "$ROOT/scripts/$part.sh"
done

main "$@"
