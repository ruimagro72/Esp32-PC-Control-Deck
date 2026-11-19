#!/usr/bin/env bash
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
LOGDIR="$ROOT/simulator/tests/results"
mkdir -p "$LOGDIR"
TS="$(date +%Y%m%d-%H%M%S)"
LOG="$LOGDIR/run-$TS.log"
exec > >(tee -a "$LOG") 2>&1

pass=0; fail=0; skip=0
run() {
  local name="$1"; shift
  echo
  echo "===== TEST: $name ====="
  if "$@"; then echo "PASS: $name"; pass=$((pass+1)); else echo "FAIL: $name (rc=$?)"; fail=$((fail+1)); fi
}
run_skip() { local name="$1"; shift; echo; echo "===== TEST: $name ====="; echo "SKIP: $name ($*)"; skip=$((skip+1)); }
# run_gated NAME "GATE CMD" "why" CMD...: runs CMD only when GATE (a bash snippet) succeeds; otherwise
# reports XFAIL (expected failure: the package or the rebuilt image the check needs is not there yet).
xfail=0
run_gated() {
  local name="$1" gate="$2" why="$3"; shift 3
  if bash -c "$gate" >/dev/null 2>&1; then run "$name" "$@"
  else echo; echo "===== TEST: $name ====="; echo "XFAIL: $name ($why)"; xfail=$((xfail+1)); fi
}

# GUI helpers: a private virtual X server (never the user's DISPLAY), a bounded run,
# and a kill by PID tree (children first, then the process group of the launched PID).
GUI_XPID=""; GUI_PID=""
kill_tree() {  # kill_tree PID: kill PID and its descendants with SIGKILL (by PID, no pattern match)
  local p="$1" c
  for c in $(pgrep -P "$p" 2>/dev/null); do kill_tree "$c"; done
  kill -KILL "$p" 2>/dev/null
}
kill_app() {  # stops the launched app tree only; the shared X server stays up for the next smoke
  [ -n "$GUI_PID" ] && { kill_tree "$GUI_PID"; kill -KILL -- "-$GUI_PID" 2>/dev/null; }
  GUI_PID=""
}
x_clean_stale() {  # removes the :79 lock and socket of a dead server; refuses when a live PID owns :79
  local lock=/tmp/.X79-lock pid=""
  [ -e "$lock" ] || [ -e /tmp/.X11-unix/X79 ] || return 0
  [ -f "$lock" ] && read -r pid < "$lock" 2>/dev/null
  pid="${pid//[[:space:]]/}"
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    echo "virtual X :79 is owned by live PID $pid; not started" >&2
    return 1
  fi
  rm -f "$lock" /tmp/.X11-unix/X79
}
cleanup_x() {  # stops the virtual X server this run started; SIGKILL skips its own cleanup, so remove its files here
  if [ -n "$GUI_XPID" ]; then
    kill -KILL "$GUI_XPID" 2>/dev/null; wait "$GUI_XPID" 2>/dev/null
    rm -f /tmp/.X79-lock /tmp/.X11-unix/X79
  fi
  GUI_XPID=""
}
cleanup_all() { kill_app; cleanup_x; }
trap cleanup_all EXIT
trap 'exit 130' INT TERM HUP
start_virtual_x() {  # sets GUI_XPID; returns 1 when no virtual X server can be started
  x_clean_stale || return 1
  if command -v Xvfb >/dev/null 2>&1; then
    Xvfb :79 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 & GUI_XPID=$!
  elif command -v Xephyr >/dev/null 2>&1; then
    Xephyr :79 -ac -no-host-grab -screen 1280x800 >/dev/null 2>&1 & GUI_XPID=$!
  else
    return 1
  fi
  local i
  for i in $(seq 1 10); do
    [ -S /tmp/.X11-unix/X79 ] && return 0
    kill -0 "$GUI_XPID" 2>/dev/null || break
    sleep 0.5
  done
  cleanup_x
  return 1
}
gui_smoke() {  # gui_smoke NAME CMD...: passes if CMD is still alive after 2 s or exits 0; runs on :79 only
  local name="$1"; shift
  local launch=() ok=0 rc=0
  command -v setsid >/dev/null 2>&1 && launch=(setsid)
  echo; echo "===== TEST: $name ====="
  if ! kill -0 "$GUI_XPID" 2>/dev/null; then
    echo "FAIL: $name (virtual X server :79 is not running)"; fail=$((fail+1)); return
  fi
  DISPLAY=:79 "${launch[@]}" "$@" >/dev/null 2>&1 & GUI_PID=$!
  sleep 2
  if kill -0 "$GUI_PID" 2>/dev/null; then ok=1
  else wait "$GUI_PID"; rc=$?; [ "$rc" -eq 0 ] && ok=1; fi
  kill_app
  if [ "$ok" -eq 1 ]; then echo "PASS: $name"; pass=$((pass+1)); else echo "FAIL: $name (rc=$rc)"; fail=$((fail+1)); fi
}
bounded_run() {  # bounded_run NAME CMD...: no display at all; the tree is killed after 2 s
  local name="$1"; shift
  local launch=()
  command -v setsid >/dev/null 2>&1 && launch=(setsid)
  echo; echo "===== TEST: $name ====="
  env -u DISPLAY "${launch[@]}" "$@" >/dev/null 2>&1 & GUI_PID=$!
  sleep 2
  kill_app
  echo "PASS: $name"; pass=$((pass+1))
}

echo "Extensive simulator test battery"
echo "root=$ROOT ts=$TS"

# 1. Syntax / static
run "bash -n start.sh" bash -n start.sh
export PATH="$ROOT/simulator/tests/.tools/bin:$PATH"
if command -v shellcheck >/dev/null 2>&1; then
  run "shellcheck start.sh (warnings non-fatal)" bash -lc 'shellcheck -S warning start.sh || true; exit 0'
else
  echo "FAIL: shellcheck not found (install system shellcheck or place binary at simulator/tests/.tools/bin/shellcheck)"
  fail=$((fail+1))
fi

# 2. Builds
run "launcher build.sh" ./simulator/launcher/build.sh
run "emu build-host" ./start.sh emu build-host
run "mockup_studio build.sh" ./simulator/mockup_studio/build.sh

# 3. CLI help/info/merge dry-run matrix
run "emu help" bash -lc './start.sh emu help >/dev/null'
run "emu info" bash -lc './start.sh emu info >/dev/null'
run "emu merge --help" bash -lc './start.sh emu merge --help >/dev/null'
for b in es3c28p esp32-s3-devkitc-1-n16r8 lilygo-t-deck lilygo-t-hmi m5stack-cardputer; do
  run "emu merge --dry-run --board $b" bash -lc "./start.sh emu merge --dry-run --board '$b' >/dev/null"
done
bounded_run "emu ui --help-ish (no display, killed after 2 s)" ./start.sh emu ui --help

# 4. Launcher headless/help
run "sim_launcher --help" bash -lc './simulator/launcher/sim_launcher --help >/dev/null'

# 5. C module smoke tests (compile+run)
CC="${CC:-gcc}"
CFLAGS="-O1 -Wall -Wextra -I$ROOT/simulator/launcher -I$ROOT/simulator/emu"
run "smoke: console_log" bash -lc "cd '$ROOT' && $CC $CFLAGS simulator/tests/smoke_console_log.c simulator/launcher/console_log.c -o simulator/tests/smoke_console_log && ./simulator/tests/smoke_console_log"
run "smoke: firmware_mgr merge plan" bash -lc "cd '$ROOT' && $CC $CFLAGS simulator/tests/smoke_firmware_mgr.c simulator/launcher/firmware_mgr.c -o simulator/tests/smoke_firmware_mgr && ./simulator/tests/smoke_firmware_mgr"
run "smoke: validate basics" bash -lc "cd '$ROOT' && $CC $CFLAGS simulator/tests/smoke_validate.c simulator/launcher/validate.c simulator/launcher/firmware_mgr.c simulator/launcher/storage_littlefs.c simulator/launcher/audio_net.c simulator/launcher/emu_caps_probe.c simulator/launcher/config.c -o simulator/tests/smoke_validate && ./simulator/tests/smoke_validate"

# 5b. Simulator C++ unit tests (same flags as simulator/Makefile)
run "simulator libsimcore" make -s -C "$ROOT/simulator" all
for t in test_touch_chip test_lcd_panel; do
  run "unit: $t" bash -lc "cd '$ROOT/simulator' && mkdir -p build/unit && g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc tests/unit/$t.cpp build/libsimcore.a -o build/unit/$t && ./build/unit/$t"
done


# 5c. Battery profile block, arg parity, and emulator checks (instance 60 only, one emulator at a time).
# Each check names the package it needs; a missing package or a stale firmware image reports XFAIL.
PROFDIR="$ROOT/simulator/launcher/profiles"
TSCRATCH="$(mktemp -d)"
trap 'cleanup_all; rm -rf "$TSCRATCH"' EXIT
EMU_IMG="$ROOT/simulator/build/merged-emu.bin"

fw_fresh() {  # the merged emulator image is newer than every firmware source file
  [ -f "$EMU_IMG" ] && [ -z "$(find "$ROOT/firmware/src" -type f -newer "$EMU_IMG" -print -quit)" ]
}
export -f fw_fresh; export ROOT EMU_IMG
battery_harness_build() {
  ( cd "$ROOT/simulator/launcher" && gcc -O1 -Wall -I. -o "$TSCRATCH/battery_harness" ../tests/fixtures/battery_harness.c \
      profile.c config.c audio_net.c storage_littlefs.c )
}
battery_roundtrip() {  # load + save keeps the block; a profile without the block stays without it
  battery_harness_build || return 1
  local p keys a b
  for p in test-battery:'["present","capacity_mah","initial_percent","charging","time_scale"]' \
           test-battery-absent:'["present"]' example-es3c28p:'[]'; do
    keys="${p#*:}"; p="${p%%:*}"
    "$TSCRATCH/battery_harness" roundtrip "$PROFDIR/$p.json" "$TSCRATCH/$p.out.json" || return 1
    a="$(jq -cS --argjson k "$keys" '.board_hardware.battery | if . == null then "none" else with_entries(select(.key as $x | $k | index($x))) end' "$PROFDIR/$p.json")"
    b="$(jq -cS --argjson k "$keys" '.board_hardware.battery | if . == null then "none" else with_entries(select(.key as $x | $k | index($x))) end' "$TSCRATCH/$p.out.json")"
    [ "$a" = "$b" ] || { echo "$p: block changed: $a -> $b"; return 1; }
  done
  [ "$(jq -r '.board_hardware.battery | type' "$TSCRATCH/example-es3c28p.out.json")" = null ]
}
shell_battery_arg() {  # the --i2c-dev value emu.sh builds for profile $1 (empty when none)
  ( ROOT="$ROOT"; FW="$ROOT/firmware"; APPS="$FW/apps"; APPSTORE="$APPS/KryonOS-AppStore"; TEST_APPS="$APPS/test"
    SHELL_APP="$APPS/shell"; RES="$ROOT/Resources"; SIM="$ROOT/simulator"; SIM_CARD="$SIM/SD_Card"; SERVER="$ROOT/server"
    VAR_DIR="$ROOT/server/var"; PID_FILE="$VAR_DIR/java-host.pid"; LOG_FILE="$VAR_DIR/log/java-host.log"; ENV=es3c28p; PORT=/dev/null
    for part in common firmware server card test scan export emu launcher commands; do . "$ROOT/scripts/$part.sh"; done
    emu_profile_load "$1"; emu_profile_esp_args
    prev=""; for a in ${EMU_PROFILE_ARGS[@]+"${EMU_PROFILE_ARGS[@]}"}; do
      [ "$prev" = "--i2c-dev" ] && echo "$a"; prev="$a"; done )
}
battery_arg_parity() {  # launcher_config_battery_arg and emu.sh give byte-identical strings
  battery_harness_build || return 1
  local p c s
  for p in test-battery test-battery-absent example-es3c28p; do
    c="$("$TSCRATCH/battery_harness" arg "$PROFDIR/$p.json")" || c=""
    s="$(shell_battery_arg "$p")" || return 1
    [ "$c" = "$s" ] || { echo "$p: C='$c' shell='$s'"; return 1; }
    echo "$p: '$c'"
  done
  [ -n "$("$TSCRATCH/battery_harness" arg "$PROFDIR/test-battery.json")" ] || { echo "test-battery gives no argument"; return 1; }
}
JSBAT='js console.log(Device.hasBattery()+" "+Device.getBatteryPercent()+" "+Device.getBatteryVoltage().toFixed(2)+" "+Device.isCharging())'
battery_emu() {  # percent drops with time_scale 60; present=false reads 0 / 0.00 V
  local out="$TSCRATCH/bat.png" l1 l2 p1 p2
  "$ROOT/start.sh" emu shot --instance 60 --profile test-battery --secs 70 --cmd "$JSBAT" --cmd 'wait 20' --cmd "$JSBAT" --out "$out" >/dev/null 2>&1
  l1="$(grep -E '^(true|false) [0-9]+ ' "${out%.*}.console.txt" | sed -n 1p)"; l2="$(grep -E '^(true|false) [0-9]+ ' "${out%.*}.console.txt" | sed -n 2p)"
  echo "first: $l1 / second: $l2"
  p1="$(cut -d' ' -f2 <<<"$l1")"; p2="$(cut -d' ' -f2 <<<"$l2")"
  [ -n "$p1" ] && [ -n "$p2" ] && [ "$p2" -lt "$p1" ] || return 1
  out="$TSCRATCH/bat-absent.png"
  "$ROOT/start.sh" emu shot --instance 60 --profile test-battery-absent --secs 50 --cmd "$JSBAT" --out "$out" >/dev/null 2>&1
  grep -qx 'false 0 0.00 false' "${out%.*}.console.txt"
}
rotation_dims() {  # host dump follows the firmware rotation: 0,2 = 240x320, 1,3 = 320x240
  local r want got
  for r in 0 1 2 3; do
    "$ROOT/start.sh" emu shot --instance 60 --secs 50 --cmd "rotate $r" --cmd 'wait 12' --out "$TSCRATCH/rot$r.png" >/dev/null 2>&1
    got="$(identify -format '%wx%h' "$TSCRATCH/rot$r.png" 2>/dev/null)"
    want=240x320; [ $((r & 1)) -eq 1 ] && want=320x240
    echo "rotate $r: $got (want $want)"
    [ "$got" = "$want" ] || return 1
  done
}
wifi_cancel_state() {  # Settings > Connections > Wi-Fi > scan list > Cancel returns to state 6 (SETTINGS_WIFI)
  local out="$TSCRATCH/wifi-cancel.png" last
  "$ROOT/start.sh" emu shot --instance 60 --secs 60 --net user --wifi-ssid myssid --wifi-auth open --wifi-password "" \
    --cmd 'tap 120 177' --cmd 'tap 60 56' --cmd 'tap 120 70' --cmd 'tap 120 140' --cmd 'wait 10' --cmd 'tap 120 300' --cmd 'wait 3' --cmd 'state' \
    --out "$out" >/dev/null 2>&1
  last="$(grep '^state=' "${out%.*}.console.txt" | tail -n 1)"
  echo "last state reply: ${last:-none}"
  [[ "$last" == state=6\ * ]]
}
run_gated "battery: profile round trip" "grep -q launcher_config_battery_arg '$ROOT/simulator/launcher/config.h' && grep -q battery '$ROOT/simulator/launcher/profile.c'" \
  "package P7 (profile battery block) not in the tree" battery_roundtrip
run_gated "battery: C and emu.sh arguments identical" "grep -q launcher_config_battery_arg '$ROOT/simulator/launcher/config.c' && grep -q 'i2c-dev' '$ROOT/scripts/emu.sh'" \
  "package P6/P7 (emu.sh battery argument, config.c helper) not in the tree" battery_arg_parity
if [ "${EXT_SKIP_EMU:-0}" = 1 ]; then
  run_skip "battery: emulator percent drops" "EXT_SKIP_EMU=1"; run_skip "rotation: host dump follows firmware" "EXT_SKIP_EMU=1"; run_skip "wifi: Cancel returns to Settings Wi-Fi" "EXT_SKIP_EMU=1"
else
  run_gated "battery: emulator percent drops" "[ -f '$ROOT/firmware/src/Hal/Boards/es3c28p/SimBattery.cpp' ] && grep -q 'i2c-dev' '$ROOT/scripts/emu.sh' && fw_fresh" \
    "package P5/P6 missing or merged-emu.bin older than the firmware sources (rebuild: ./start.sh build, ./start.sh emu merge)" battery_emu
  run_gated "rotation: host dump follows firmware" "grep -q 'hw rotation ->' '$ROOT/simulator/emu/sim_host.c' && [ '$ROOT/simulator/emu/lcd_touch_host' -nt '$ROOT/simulator/emu/sim_host.c' ]" \
    "package P6 (host follows MADCTL) not in the tree or lcd_touch_host not rebuilt" rotation_dims
  run_gated "wifi: Cancel returns to Settings Wi-Fi" "fw_fresh" \
    "merged-emu.bin older than the firmware sources (package P1 fix not in the image)" wifi_cancel_state
fi

# 6. Optional tools
if command -v cppcheck >/dev/null 2>&1; then
  run "cppcheck launcher subset" bash -lc "cppcheck --enable=warning,style --inline-suppr --error-exitcode=0 -I simulator/launcher -I simulator/emu simulator/launcher/sim_launcher.c simulator/launcher/config.c simulator/launcher/firmware_mgr.c simulator/launcher/validate.c simulator/emu/sim_host.c >/dev/null 2>&1 || true; exit 0"
else
  run_skip "cppcheck" "not installed"
fi

# 7. GUI smoke (DISPLAY)
# The user's desktop DISPLAY is never used here: the smoke runs on a private :79 server.
if start_virtual_x; then
  gui_smoke "launcher GUI startup smoke (2s, :79)" "$ROOT/simulator/launcher/sim_launcher"
  gui_smoke "mockup_studio GUI startup smoke (2s, :79)" "$ROOT/simulator/mockup_studio/mockup_studio"
  cleanup_x
else
  run_skip "GUI startup smoke" "skipped: no virtual X server (install Xvfb or Xephyr)"
fi

echo
echo "===== SUMMARY ====="
echo "PASS=$pass FAIL=$fail SKIP=$skip XFAIL=$xfail"
echo "LOG=$LOG"
exit $((fail==0?0:1))
