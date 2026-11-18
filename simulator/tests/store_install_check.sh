#!/usr/bin/env bash
# store_install_check.sh: boots KryonOS in esp-emu 1.1.0 on the open test AP, opens the
# App Store, taps Install on the first card (Hello World, Demos) and records what happens:
# console state and listings, LCD captures and the emulator log.
#
# usage: store_install_check.sh [--instance N] [--secs N] [--every N] [--out DIR]
#   --instance N    emulator instance; ports derive from it  (default $SIM_INSTANCE or 2)
#                   cosim 5591+10N (SDL host), UART 5555+10N (console)
#   --secs N        total wall time of the run in seconds    (default 840)
#   --every N       seconds between LCD captures             (default 60)
#   --out DIR       output folder          (default simulator/logs/store-install-<stamp>)
#
# Needs: simulator/esp-emu-1.1.0-*/esp-emu, simulator/emu/lcd_touch_host,
# simulator/build/merged-emu.bin, firmware/.pio/build/es3c28p_emu/firmware.elf,
# simulator/emu/storage/sd.img (copied to <out>/sd.img, the original is not touched),
# and ImageMagick `convert`.
# Output in DIR: console.txt (UART), sent.txt (commands with run time), emu.log,
# host-N.log, shot-NN.png (LCD), sd.img (throwaway card copy).
# The script starts every process it needs and kills each one by PID on exit.
# The LCD host runs with SDL_VIDEODRIVER=dummy, so no display is used.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/simulator"
INSTANCE="${SIM_INSTANCE:-2}"
# Ports of an emulator instance: one rule, same as sim_ports in start.sh.
ports() { COSIM_PORT=$((5591 + 10 * INSTANCE)); UART_PORT=$((5555 + 10 * INSTANCE)); }
SECS=840
EVERY=60
OUT=""

while [ $# -gt 0 ]; do
  case "$1" in
    --instance)   INSTANCE=$2; shift 2 ;;
    --secs)       SECS=$2; shift 2 ;;
    --every)      EVERY=$2; shift 2 ;;
    --out)        OUT=$2; shift 2 ;;
    -h|--help)    sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
case $INSTANCE in ''|*[!0-9]*) echo "instance must be a number: $INSTANCE" >&2; exit 2 ;; esac
ports

STAMP=$(date +%Y%m%d-%H%M%S)
OUT=${OUT:-$SIM/logs/store-install-$STAMP}
mkdir -p "$OUT" || exit 1

ESP=$(ls -d "$SIM"/esp-emu-1.1.0-*/esp-emu 2>/dev/null | head -n 1)
HOST=$SIM/emu/lcd_touch_host
MERGED=$SIM/build/merged-emu.bin
ELF=$ROOT/firmware/.pio/build/es3c28p_emu/firmware.elf
SD_SRC=$SIM/emu/storage/sd.img
SD=$OUT/sd.img
TZ_STR="<+00>0"
CONSOLE=$OUT/console.txt
SENT=$OUT/sent.txt

for f in "$ESP" "$HOST" "$MERGED" "$ELF" "$SD_SRC"; do
  [ -n "$f" ] && [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
command -v convert >/dev/null || { echo "missing: ImageMagick convert" >&2; exit 1; }

PIDS=()
cleanup() {
  local p
  for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null; done
  for p in "${PIDS[@]:-}"; do wait "$p" 2>/dev/null; done
  exec 3>&- 2>/dev/null
}
trap cleanup EXIT
trap 'exit 130' INT TERM

cp "$SD_SRC" "$SD" || exit 1

"$ESP" --chip esp32s3 --psram-size 8M --firmware "$MERGED" --sd "$SD" \
  --cosim-tcp "127.0.0.1:$COSIM_PORT" --uart-tcp "127.0.0.1:$UART_PORT" --timeout "$((SECS + 60))s" \
  --net user --wifi-ssid testnet --wifi-password "" --wifi-auth open --elf "$ELF" \
  >"$OUT/emu.log" 2>&1 &
EMU_PID=$!
PIDS+=("$EMU_PID")

# Wait for the UART port (probe in a subshell so a refused connection cannot end this shell).
up=0
for _ in $(seq 90); do
  if (exec 3<>"/dev/tcp/127.0.0.1/$UART_PORT") 2>/dev/null; then up=1; break; fi
  sleep 1
done
[ "$up" = 1 ] || { echo "UART port $UART_PORT not listening" >&2; exit 1; }

exec 3<>"/dev/tcp/127.0.0.1/$UART_PORT"
cat <&3 >"$CONSOLE" &
CAT_PID=$!
PIDS+=("$CAT_PID")

START=$(date +%s)
END=$((START + SECS))
LAST_SYNC=0

elapsed() { echo $(( $(date +%s) - START )); }

send() {
  printf '%s\r\n' "$1" >&3 2>/dev/null
  echo "sent@$(elapsed)s: $1" >>"$SENT"
}

# The emulated chip runs slower than real time: keep its clock at the PC time every 20 s.
sync_time() {
  local now
  now=$(date +%s)
  if (( now - LAST_SYNC >= 20 )); then
    printf 'time set %s %s\r\n' "$now" "$TZ_STR" >&3 2>/dev/null
    LAST_SYNC=$now
  fi
}

wait_until() {
  while [ "$(elapsed)" -lt "$1" ]; do sync_time; sleep 1; done
}

# wait_state PATTERN TIMEOUT: poll `state` until PATTERN shows in console output sent after the call.
wait_state() {
  local base deadline next
  base=$(wc -l <"$CONSOLE")
  deadline=$(( $(date +%s) + $2 ))
  next=0
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if tail -n +"$((base + 1))" "$CONSOLE" | grep -q "$1"; then return 0; fi
    if [ "$(date +%s)" -ge "$next" ]; then send state; next=$(( $(date +%s) + 20 )); fi
    sync_time
    sleep 1
  done
  return 1
}

# Scheduler: runs in the background, writes commands to the console and keeps the clock set.
(
  wait_until 20;  send "wifi testnet"
  wait_until 40;  send "tap 120 87"
  wait_until 90;  send "tap 120 117"
  wait_until 120; send "tap 120 117"
  if wait_state "name=APP_STORE" 300; then echo "store: APP_STORE reached" >>"$SENT"
  else echo "store: APP_STORE not reached in 300 s" >>"$SENT"; fi
  wait_until "$(( $(elapsed) + 60 ))"   # let the index load from raw.githubusercontent.com
  send state
  send "tap 60 300"                     # card Install: opens the Installer dialog (state 3)
  echo "install: tapped card Install at $(elapsed)s" >>"$SENT"
  wait_until "$(( $(elapsed) + 60 ))"   # download; the display sleeps after 60 s idle
  send "tap 60 247"                     # wake tap: only wakes the screen, the tap is consumed
  echo "install: wake tap at $(elapsed)s" >>"$SENT"
  wait_until "$(( $(elapsed) + 3 ))"
  send "tap 60 247"                     # real Install in the Installer dialog (x 27..115, y 232..262)
  echo "install: Install in dialog at $(elapsed)s" >>"$SENT"
  send "ls /local/system"               # config_install_sd.txt there would switch the destination to SD
  send "ls /local"
  while [ "$(elapsed)" -lt "$((SECS - 150))" ]; do
    send state
    send "ls /local/apps"
    send "ls /local/apps/com.kryonos.helloworld"
    send "ls /sd/UserApps"
    send df
    wait_until "$(( $(elapsed) + 60 ))"
  done
  send state
  send exit
  echo "home: exit sent at $(elapsed)s" >>"$SENT"
  wait_until "$(( $(elapsed) + 20 ))"
  send state
  send "ls /local/apps"
  send "ls /sd/UserApps"
  # Home: SEL at the footer (120 300) leaves the screen the Installer is on, if the firmware maps it so.
  send "tap 120 300"
  echo "home: tap 120 300 (SEL) at $(elapsed)s" >>"$SENT"
  wait_until "$(( $(elapsed) + 15 ))"
  send state
  # Scroll the home list down with DN (footer 195 300) until the APPS header shows.
  for _ in 1 2 3 4 5 6; do
    send "tap 195 300"
    wait_until "$(( $(elapsed) + 3 ))"
  done
  send state
  echo "home: DN x6 done at $(elapsed)s" >>"$SENT"
  while true; do sync_time; sleep 1; done
) &
SCHED_PID=$!
PIDS+=("$SCHED_PID")

# LCD captures: the host dumps once after --dump-after seconds and exits, so each capture is one host run.
n=0
while [ "$(date +%s)" -lt "$END" ]; do
  n=$((n + 1))
  shot=$(printf '%s/shot-%02d' "$OUT" "$n")
  SDL_VIDEODRIVER=dummy timeout -k 5 "$((EVERY + 120))s" "$HOST" --port "$COSIM_PORT" \
    --dump-fb "$shot.ppm" --dump-after "$EVERY" >"$OUT/host-$n.log" 2>&1 &
  HOST_PID=$!
  PIDS+=("$HOST_PID")
  wait "$HOST_PID"
  if [ -f "$shot.ppm" ]; then convert "$shot.ppm" "$shot.png" && rm -f "$shot.ppm"; fi
  echo "capture $n at $(elapsed)s: $(basename "$shot.png")" >>"$SENT"
done

kill "$SCHED_PID" "$CAT_PID" "$EMU_PID" 2>/dev/null
wait "$EMU_PID" 2>/dev/null

echo "done: $OUT"
echo "--- state and install lines (console.txt) ---"
grep -E 'state=|INSTALL|install|rror|ERR|fail|UserApps|apps' "$CONSOLE" | tail -n 60
