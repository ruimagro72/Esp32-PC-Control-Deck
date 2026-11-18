#!/bin/bash
# BLE HCI check: a persistent Bumble virtual controller (tcp 9544) with a fake HID keyboard,
# plus esp-emu running the Deck firmware with --ble-hci.
# Needs (set in the environment, no default path):
#   BUMBLE_VENV    a Python venv with Bumble installed (python3 -m venv DIR && DIR/bin/pip install bumble)
#   BUMBLE_HELPER  the HCI helper script hid_advertiser.py: a local, optional file that is NOT
#                  tracked by git (it is not in this repository, so this check cannot run from a clean clone)
# Steps: open the UART socket first, wake the console (\r\n), wait for "help", then
#        "blekbd", "blekbd on", wait for the scan, "blekbd" again. Evidence: console replies,
#        HCI scan commands seen by the Bumble controller, emulator log.
# Usage: SIM_INSTANCE=4 simulator/tests/ble_hci_check.sh   (default instance 4; UART 5595 = 5555+10N)
# Output: simulator/logs/ble-console.txt, ble-emu.log, ble-bumble.log
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/simulator"
LOGS="$SIM/logs"
INSTANCE="${SIM_INSTANCE:-4}"
# Ports of an emulator instance: one rule, same as sim_ports in start.sh.
ports() { UART=$((5555 + 10 * INSTANCE)); }
case $INSTANCE in ''|*[!0-9]*) echo "instance must be a number: $INSTANCE" >&2; exit 2 ;; esac
ports
BPORT=9544   # Bumble virtual controller: external, fixed, not per instance
if [ -z "${BUMBLE_VENV:-}" ] || [ -z "${BUMBLE_HELPER:-}" ]; then
    echo "set BUMBLE_VENV (Bumble venv directory) and BUMBLE_HELPER (path to hid_advertiser.py)" >&2
    echo "example: BUMBLE_VENV=\$HOME/bumble-venv BUMBLE_HELPER=\$HOME/ble-helpers/hid_advertiser.py $0" >&2
    exit 2
fi
VENV_PY="$BUMBLE_VENV/bin/python"
EMU="$SIM/esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu"
FW="$SIM/build/merged-emu.bin"
ELF="$ROOT/firmware/.pio/build/es3c28p_emu/firmware.elf"
CONSOLE="$LOGS/ble-console.txt"
EMU_LOG="$LOGS/ble-emu.log"
BUMBLE_LOG="$LOGS/ble-bumble.log"
EPID=""; BPID=""; CPID=""

cleanup() {
    [ -n "$CPID" ] && kill "$CPID" 2>/dev/null
    [ -n "$EPID" ] && kill "$EPID" 2>/dev/null
    [ -n "$BPID" ] && kill "$BPID" 2>/dev/null
    exec 3<&- 3>&- 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT INT TERM

# Wait until file contains pattern: wait_for <file> <pattern> <seconds>
wait_for() {
    local end=$((SECONDS + $3))
    while [ "$SECONDS" -lt "$end" ]; do
        grep -q -- "$2" "$1" 2>/dev/null && return 0
        sleep 1
    done
    return 1
}

# Connect the UART socket and keep it open on fd 3: open_uart <seconds>
open_uart() {
    local end=$((SECONDS + $1))
    while [ "$SECONDS" -lt "$end" ]; do
        if exec 3<>"/dev/tcp/127.0.0.1/$UART" 2>/dev/null; then return 0; fi
        sleep 1
    done
    return 1
}

for f in "$VENV_PY" "$BUMBLE_HELPER" "$EMU" "$FW" "$ELF"; do
    [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
if (exec 3<>"/dev/tcp/127.0.0.1/$BPORT") 2>/dev/null; then
    echo "port $BPORT already in use by another process: not touching it, aborting" >&2
    exit 2
fi
if (exec 3<>"/dev/tcp/127.0.0.1/$UART") 2>/dev/null; then
    echo "UART port $UART already in use: run another instance (SIM_INSTANCE)" >&2
    exit 2
fi
mkdir -p "$LOGS"
cp "$SIM/emu/storage/sd.img" "$LOGS/ble-sd.img"   # throwaway copy, the real card stays untouched
: >"$CONSOLE"

"$VENV_PY" "$BUMBLE_HELPER" "$BPORT" >"$BUMBLE_LOG" 2>&1 & BPID=$!
if ! wait_for "$BUMBLE_LOG" "emu\] HCI tcp-server listening" 20; then
    echo "bumble helper did not start, see $BUMBLE_LOG" >&2; exit 1
fi
"$EMU" --chip esp32s3 --psram-size 8M --firmware "$FW" --sd "$LOGS/ble-sd.img" --elf "$ELF" \
    --uart-tcp "127.0.0.1:$UART" --ble-hci "tcp:localhost:$BPORT" --timeout 600s >"$EMU_LOG" 2>&1 & EPID=$!

if ! open_uart 120; then
    echo "UART $UART did not open, see $EMU_LOG" >&2; exit 1
fi
# Socket open before anything is sent: reader started first, so no reply is lost
cat <&3 >>"$CONSOLE" & CPID=$!

printf '\r\n' >&3; sleep 2
printf 'help\r\n' >&3
if wait_for "$CONSOLE" "KryonOS Serial Commands" 240; then
    echo "console alive: help answered"
else
    echo "console silent: no help reply in 240 s"
fi

printf 'blekbd\r\n' >&3
if wait_for "$CONSOLE" "blekbd: " 60; then echo "blekbd reply: yes"; else echo "blekbd reply: none in 60 s"; fi

printf 'blekbd on\r\n' >&3
if wait_for "$CONSOLE" "blekbd: scanning" 60; then
    echo "blekbd on reply: scanning started"
else
    echo "blekbd on reply: no 'scanning' line in 60 s (see console file)"
fi

printf 'info\r\n' >&3   # liveness probe: does the console still answer after "blekbd on"?
if wait_for "$CONSOLE" "Free Heap" 60; then echo "liveness after blekbd on: console answers"; else echo "liveness after blekbd on: console silent"; fi

sleep 90
printf 'blekbd\r\n' >&3
wait_for "$CONSOLE" "blekbd: " 60 >/dev/null || true
sleep 2

echo "--- console replies ($CONSOLE)"
grep -a "blekbd\|KryonOS Serial\|unknown command" "$CONSOLE" | tail -20
echo "--- HCI scan commands seen by Bumble ($BUMBLE_LOG)"
grep -a "\[hci\]\|\[kbd\]\|\[emu\]" "$BUMBLE_LOG" | head -40
echo "--- emulator log, BLE lines ($EMU_LOG)"
grep -a -i "ble\|hci" "$EMU_LOG" | head -40
echo "console replies: $CONSOLE"
echo "emulator log:    $EMU_LOG"
echo "bumble log:      $BUMBLE_LOG"
