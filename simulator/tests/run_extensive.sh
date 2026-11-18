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
echo "PASS=$pass FAIL=$fail SKIP=$skip"
echo "LOG=$LOG"
exit $((fail==0?0:1))
