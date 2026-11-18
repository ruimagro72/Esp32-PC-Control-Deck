#!/usr/bin/env bash
# ESP32 PC Control Deck - the single entry point.
#
#   ./start.sh build | flash | monitor | publish | put <app> | cmd "<cmd>" | bridge | monitor-vnc
#   ./start.sh server [run|build|selftest|console|list-ports|setup|stop|status|clean|openaction-install]
#   ./start.sh emu [info|merge|build-host|build-launcher|build-mockup|build|run|bootlog|ui|cosim|console|shot|launcher|mockup|test]
#   ./start.sh launcher [build|run|test|mockup-build|mockup-run]
#   ./start.sh card [stage|deploy <mount>|image|extract|ls]
#   ./start.sh test [all|server|native|emu|launcher|extensive|wifi|ble|store]
#   ./start.sh scan
#   ./start.sh help
#
# Firmware defaults: ENV=es3c28p, PORT=/dev/ttyACM0. The Java host is in server/.
# The defined SD card structure is simulator/SD_Card (single source, no card-staging).
# The simulator is simulator/ (an ESP32 emulator front-end, outside firmware/).
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
KEY="${OTA_KEY:-$ROOT/server/var/keys/ota_signing.pk8}"

die() { echo "Error: $*" >&2; exit 1; }

usage() {
    sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'
}

firmware_usage() {
    cat <<'EOF'
Firmware commands (KryonOS port, firmware/):
  build                         compile the board env (ENV=es3c28p)
  flash                         compile and upload (PORT=/dev/ttyACM0)
  monitor                       serial monitor
  publish                       build, sign and put the image in server/var/firmware
  put <app>                     copy an app to /sd/apps/<app> over the cable
  cmd "<console command>"       send a console command over the cable
  bridge                        VNC WebSocket bridge (5901 -> 5900)
  monitor-vnc [WxH]             virtual monitor for the Deck (krfb)
  stamp                         write firmware/src/Kernel/BuildId.h
EOF
}

# ----- firmware ------------------------------------------------------------

fw_stamp() {
    printf '#define BUILD_ID "%s"\n' "$(date -u +%Y%m%d-%H%M%S)" > "$FW/src/Kernel/BuildId.h"
}

fw_build() {
    fw_stamp
    ( cd "$FW" && pio run -e "$ENV" )
}

fw_flash() {
    fw_stamp
    ( cd "$FW" && pio run -e "$ENV" -t upload --upload-port "$PORT" )
}

fw_monitor() {
    ( cd "$FW" && pio device monitor -p "$PORT" -b 115200 )
}

fw_publish() {
    fw_stamp
    ( cd "$FW" && pio run -e "$ENV" ) >&2
    local jar
    jar="$(ls -t "$ROOT"/server/target/control-deck-host-*-all.jar 2>/dev/null | head -1)"
    [ -n "$jar" ] || die "Build the server first: ./start.sh server build"
    [ -f "$KEY" ] || die "No signing key at $KEY. Make it once: java -cp $jar dev.controldeck.host.firmware.FirmwareSigner keygen $KEY firmware/src/Kernel/OtaPublicKey.h (and flash that firmware before the first update)"
    mkdir -p "$ROOT/server/var/firmware"
    java -cp "$jar" dev.controldeck.host.firmware.FirmwareSigner sign "$KEY" \
        "$FW/.pio/build/$ENV/firmware.bin" "$ROOT/server/var/firmware/$ENV.bin"
    ls -l "$ROOT/server/var/firmware/$ENV.bin"
}

fw_put() {
    local app="${1:?app folder name}" src="$APPS/$app" args=()
    [ -d "$src" ] || src="$(ls -d "$APPSTORE"/categories/*/"$app" "$TEST_APPS/$app" 2>/dev/null | head -1)"
    [ -d "$src" ] || die "no app folder named $app (firmware/apps/, KryonOS-AppStore/categories/*/, test/)"
    for f in "$src"/*; do args+=("$f" "/sd/apps/$app/$(basename "$f")"); done
    exec java "$FW/tools/Put.java" "$PORT" "${args[@]}"
}

fw_cmd() {
    exec java "$FW/tools/Send.java" "$PORT" "${1:?command}"
}

fw_bridge() {
    exec "$FW/tools/vnc-bridge.sh"
}

fw_monitor_vnc() {
    exec "$FW/tools/deck-monitor.sh" "${1:-240x304}"
}

firmware_cmd() {
    local cmd="${1:-help}"
    [ $# -gt 0 ] && shift
    case "$cmd" in
        build|flash|monitor|publish|put|cmd|bridge|monitor-vnc|stamp)
            "fw_${cmd//-/_}" "$@" ;;
        help|-h|--help) firmware_usage ;;
        *) die "unknown firmware command '$cmd' (build, flash, monitor, publish, put, cmd, bridge, monitor-vnc, stamp)" ;;
    esac
}

# ----- server --------------------------------------------------------------

server_usage() {
    cat <<'EOF'
Server commands (Java host, server/):
  run [options]                 build if needed, then start the server (default)
  build                         compile and package with Maven
  selftest                      run the self-test battery
  console [--deck ID] [LINE]    the Deck's console through the running server
  list-ports                    list serial ports and the one "auto" would pick
  setup                         ask for the settings and truncate the history
  stop                          stop a server started by this script
  status                        say whether it runs
  clean                         remove the build output (mvn clean)
  openaction-install            install the OpenDeck plugin

run options (this run only):
  --no-serial                   do not talk to the board over USB
  --serial-port DEV             auto, /dev/ttyACM0, COM4 ...
  --baud N                      baud rate (default 115200)
  --bind ADDRESS                API listen address (default: saved api.bind, 127.0.0.1)
  --port N                      API port (default: saved api.port, 8080)

Environment: JAVA_OPTS, MVN_ARGS, CONTROL_DECK_VAR, CONTROL_DECK_DB,
  ESP32_SERIAL, ESP32_BAUD, CONTROL_DECK_DEBUG.
EOF
}

server_need_java() {
    command -v java >/dev/null 2>&1 || die "Java is not installed or not in PATH (the host needs Java 21 or newer)."
    local major
    major="$(java -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -1)"
    if [ -z "${major}" ] || [ "${major}" -lt 21 ]; then
        die "Java 21 or newer is required (found ${major:-unknown})."
    fi
}

server_jar_path() {
    ls -t "$ROOT"/server/target/control-deck-host-*-all.jar 2>/dev/null | head -1 || true
}

server_needs_build() {
    local jar
    jar="$(server_jar_path)"
    [ -z "${jar}" ] && return 0
    [ -n "$(find "$ROOT/server/src" "$ROOT/server/pom.xml" "$ROOT/server/plugins" -newer "${jar}" -type f -print -quit 2>/dev/null)" ]
}

server_build() {
    command -v mvn >/dev/null 2>&1 || die "Maven is not installed or not in PATH (Debian/Ubuntu: sudo apt install maven)."
    echo "Building the Java host (Maven)..."
    # shellcheck disable=SC2086
    ( cd "$ROOT/server" && mvn -q ${MVN_ARGS:-} package )
    [ -n "$(server_jar_path)" ] || die "the build finished but target/control-deck-host-*-all.jar was not produced."
    server_build_plugins
}

server_build_plugins() {
    local dir name tmp
    mkdir -p "$ROOT/server/target/plugins"
    for dir in "$ROOT"/server/plugins/*/; do
        [ -d "${dir}src" ] || continue
        name="$(basename "${dir}")"
        tmp="$ROOT/server/target/plugin-classes/${name}"
        rm -rf "${tmp}" && mkdir -p "${tmp}"
        # shellcheck disable=SC2046
        javac --release 21 -nowarn -cp "$(server_jar_path)" -d "${tmp}" $(find "${dir}src" -name "*.java") \
            || die "plugin ${name} did not compile."
        cp -r "${dir}src/META-INF" "${tmp}/" 2>/dev/null || true
        jar --create --file "$ROOT/server/target/plugins/${name}.jar" -C "${tmp}" . || die "plugin ${name}: jar failed."
        echo "Built plugin target/plugins/${name}.jar"
    done
}

server_ensure_built() {
    if server_needs_build; then
        server_build
    fi
}

server_running_pid() {
    [ -f "${PID_FILE}" ] || return 1
    local pid
    pid="$(cat "${PID_FILE}")"
    kill -0 "${pid}" 2>/dev/null || return 1
    echo "${pid}"
}

server_cmd() {
    local command_name="run"
    case "${1:-}" in
        ""|-*) ;;
        run|build|selftest|console|list-ports|setup|stop|status|clean|openaction-install) command_name="$1"; shift ;;
        help|-h|--help) server_usage; return 0 ;;
        *) die "unknown server command '$1' (run, build, selftest, console, list-ports, setup, stop, status, clean, openaction-install)" ;;
    esac
    for arg in "$@"; do
        case "$arg" in
            -h|--help) server_usage; return 0 ;;
        esac
    done

    case "${command_name}" in
        build)
            server_need_java
            server_build
            echo "Built $(server_jar_path)"
            ;;
        clean)
            command -v mvn >/dev/null 2>&1 || die "Maven is not installed or not in PATH."
            # shellcheck disable=SC2086
            ( cd "$ROOT/server" && mvn -q ${MVN_ARGS:-} clean )
            echo "Removed $ROOT/server/target"
            ;;
        openaction-install)
            server_need_java
            server_ensure_built
            local target="${XDG_CONFIG_HOME:-${HOME}/.config}/opendeck/plugins/dev.controldeck.deck.sdPlugin"
            mkdir -p "${target}"
            cp "$ROOT/server/openaction/dev.controldeck.deck.sdPlugin/"* "${target}/"
            printf '#!/bin/sh\nexport CONTROL_DECK_VAR="%s"\nexec java -jar "%s" "$@"\n' "${VAR_DIR}" "$(server_jar_path)" > "${target}/run.sh"
            chmod +x "${target}/run.sh"
            echo "Installed in ${target}; restart OpenDeck"
            ;;
        selftest)
            server_need_java
            server_ensure_built
            ( cd "$ROOT/server" && exec java ${JAVA_OPTS:-} -jar "$(server_jar_path)" --selftest )
            ;;
        console)
            server_need_java
            server_ensure_built
            ( cd "$ROOT/server" && exec java ${JAVA_OPTS:-} -jar "$(server_jar_path)" --console "$@" )
            ;;
        list-ports)
            server_need_java
            server_ensure_built
            ( cd "$ROOT/server" && exec java ${JAVA_OPTS:-} -jar "$(server_jar_path)" --list-ports )
            ;;
        setup)
            server_need_java
            server_ensure_built
            ( cd "$ROOT/server" && exec java ${JAVA_OPTS:-} -jar "$(server_jar_path)" --setup "$@" )
            ;;
        status)
            if pid="$(server_running_pid)"; then
                echo "The Java host is running (PID ${pid}). Transport state: GET /api/transports on its address."
            else
                echo "The Java host is not running."
            fi
            ;;
        stop)
            if ! pid="$(server_running_pid)"; then
                rm -f "${PID_FILE}"
                echo "The Java host is not running; nothing to stop."
                return 0
            fi
            kill -TERM "${pid}"
            for _ in $(seq 1 60); do
                kill -0 "${pid}" 2>/dev/null || break
                sleep 0.1
            done
            if kill -0 "${pid}" 2>/dev/null; then
                die "the host (PID ${pid}) did not stop in 6 s."
            fi
            rm -f "${PID_FILE}"
            echo "Stopped the Java host (PID ${pid})."
            ;;
        run)
            server_need_java
            if pid="$(server_running_pid)"; then
                die "the Java host is already running (PID ${pid}); stop it with: ./start.sh server stop"
            fi
            server_ensure_built
            mkdir -p "${VAR_DIR}/log"
            ( cd "$ROOT/server" && exec java ${JAVA_OPTS:-} -jar "$(server_jar_path)" "$@" ) > >(tee "${LOG_FILE}") 2>&1 &
            local host_pid=$!
            echo "${host_pid}" > "${PID_FILE}"
            trap 'kill -TERM "${host_pid}" 2>/dev/null || true' INT TERM
            local status=0
            wait "${host_pid}" || status=$?
            while kill -0 "${host_pid}" 2>/dev/null; do
                wait "${host_pid}" || status=$?
            done
            rm -f "${PID_FILE}"
            exit "${status}"
            ;;
    esac
}

# ----- simulator -----------------------------------------------------------

sim_ensure_card() {
    [ -d "$SIM_CARD" ] || die "simulator SD card not found: $SIM_CARD"
}

# ----- SD card -------------------------------------------------------------

# Secret and per-device files (keys, tokens, Wi-Fi and VNC config, owner's deck config): scan flags
# them and deploy never copies them, so the card keeps its own copies.
is_secret_file() {
    case "$1" in
        *.pk8|*/deck.json|*/vnc.json|*/.env|*wifi_credentials*) return 0 ;;
    esac
    return 1
}

# Copies the card tree without overwriting or deleting anything on the card except the files it
# carries; per-device files on the card are never touched.
card_deploy() {
    local dst="${1:?mount point of the card}" f rel skipped=""
    [ -d "$SIM_CARD" ] || die "simulator SD card not found: $SIM_CARD"
    [ -d "$dst" ] || die "card mount point not found: $dst"
    while IFS= read -r -d '' f; do
        mkdir -p -- "$dst/${f#"$SIM_CARD/"}"
    done < <(find "$SIM_CARD" -mindepth 1 -type d -print0)
    while IFS= read -r -d '' f; do
        rel="${f#"$SIM_CARD/"}"
        if is_secret_file "$f"; then
            skipped="${skipped:+$skipped, }$rel"
            continue
        fi
        cp --no-preserve=mode -- "$f" "$dst/$rel" || die "copy failed: $rel"
    done < <(find "$SIM_CARD" -type f -print0)
    sync
    [ -z "$skipped" ] || echo "skipped per-device files (not copied): $skipped"
    echo "deployed $SIM_CARD to $dst"
}

# Card images are FAT32 files built and unpacked with mtools (no root, no mount, works on Linux
# and Windows): the user can edit the folder, rebuild the image, or unpack an image to inspect it.
SIM_CARD_IMG="$SIM/emu/storage/sd.img"

card_need_mtools() {
    local t
    for t in mformat mcopy mdir; do
        command -v "$t" >/dev/null 2>&1 || die "card: '$t' not found (install mtools)"
    done
}

# card_image [--force] [DIR] [IMG] [SIZE_MB]: FAT32 image of DIR (default simulator/SD_Card).
card_image() {
    local force=0
    if [ "${1:-}" = "--force" ]; then force=1; shift; fi
    local dir="${1:-$SIM_CARD}" img="${2:-$SIM_CARD_IMG}" mb="${3:-128}"
    card_need_mtools
    [ -d "$dir" ] || die "card image: folder not found: $dir"
    case "$mb" in ''|*[!0-9]*) die "card image: SIZE_MB must be a number" ;; esac
    [ "$mb" -ge 64 ] || die "card image: FAT32 needs at least 64 MB (got $mb)"
    if [ -e "$img" ] && [ "$force" -ne 1 ]; then
        die "card image: $img exists (the card keeps what the firmware wrote). Back it up and pass --force to replace it."
    fi
    mkdir -p "$(dirname "$img")"
    rm -f "$img"
    truncate -s "${mb}M" "$img"
    mformat -i "$img" -F :: || die "card image: mformat failed"
    ( cd "$dir" && mcopy -s -m -Q -i "$img" ./* :: ) || die "card image: copying $dir failed"
    echo "card image: $img (${mb} MB FAT32) from $dir: $(mdir -s -i "$img" :: 2>/dev/null | tail -2 | head -1 | sed 's/^ *//')"
}

# card_extract [--force] [IMG] [DIR]: unpack an image into a folder (default: emu/storage/sd-extract).
card_extract() {
    local force=0
    if [ "${1:-}" = "--force" ]; then force=1; shift; fi
    local img="${1:-$SIM_CARD_IMG}" dir="${2:-$SIM/emu/storage/sd-extract}"
    card_need_mtools
    [ -f "$img" ] || die "card extract: image not found: $img"
    if [ -d "$dir" ] && [ -n "$(ls -A "$dir" 2>/dev/null)" ] && [ "$force" -ne 1 ]; then
        die "card extract: $dir is not empty (pass --force to unpack over it)"
    fi
    mkdir -p "$dir"
    mcopy -s -m -n -Q -i "$img" :: "$dir" || die "card extract: copying out of $img failed"
    echo "card extract: $img -> $dir: $(find "$dir" -type f | wc -l) files"
}

# card_ls [IMG] [PATH]: list a folder of the image without unpacking it.
card_ls() {
    local img="${1:-$SIM_CARD_IMG}" path="${2:-/}"
    card_need_mtools
    [ -f "$img" ] || die "card ls: image not found: $img"
    mdir -i "$img" "::$path"
}

card_cmd() {
    local cmd="${1:-help}"
    [ $# -gt 0 ] && shift
    case "$cmd" in
        stage)
            [ -d "$SIM_CARD" ] || die "simulator SD card not found: $SIM_CARD"
            echo "simulator/SD_Card is the defined card structure (single source). No card-staging is generated."
            echo "  $SIM_CARD: $(find "$SIM_CARD" -type f | wc -l) files, $(du -sh "$SIM_CARD" | cut -f1)"
            ;;
        deploy) card_deploy "$@" ;;
        image) card_image "$@" ;;
        extract) card_extract "$@" ;;
        ls) card_ls "$@" ;;
        help|-h|--help)
            cat <<'EOF'
usage: ./start.sh card [command]
  stage             show info for the defined SD card (simulator/SD_Card); no staging generated
  deploy <mount>    copy simulator/SD_Card to the mounted card
  image [--force] [DIR] [IMG] [MB]   build a FAT32 image from DIR (default simulator/SD_Card -> emu/storage/sd.img, 128 MB)
  extract [--force] [IMG] [DIR]    unpack an image to a folder (default emu/storage/sd-extract)
  ls [IMG] [PATH]   list a folder inside the image
EOF
            ;;
        *) die "unknown card command '$cmd' (stage, deploy, image, extract, ls)" ;;
    esac
}

# ----- tests and scan ------------------------------------------------------

test_native() {
    local tndir="$FW/test-native"
    local b="$tndir/build"
    local hostsd="$b/hostsd"
    local hostlocal="$b/hostlocal"
    mkdir -p "$b"
    node "$tndir/gen-api.js" >/dev/null
    cat "$tndir/api.generated.js" "$tndir/prelude.js" > "$b/prelude.all.js"
    [ -x "$b/host" ] && [ "$b/host" -nt "$tndir/host.c" ] || gcc -O1 -w -I"$FW/src/Runtime" -o "$b/host" "$tndir/host.c" "$FW/src/Runtime/duktape.c" -lm || exit 2
    sim_ensure_card
    mkdir -p "$hostlocal"
    if [ $# -eq 0 ]; then
        set -- "$FW"/apps/*/main.js "$FW/apps/KryonOS-AppStore/categories/Hardware/led_controller/main.js" \
               "$FW/apps/test/com.kryonos.musicplayer/main.js" "$FW/apps/test/com.kryonos.piano/main.js" "$FW/apps/shell/shell.js"
    fi
    local pass=0 fail=0
    for app in "$@"; do
        local name d host_appdir out rc script
        name="$(basename "$(dirname "$app")")/$(basename "$app")"
        d="$(basename "$(dirname "$app")")"; case "$d" in led_controller) d=ledcontroller;; com.kryonos.musicplayer) d=musicplayer;; com.kryonos.piano) d=piano;; esac
        host_appdir=""
        if [ -d "$SIM_CARD/UserApps/$d" ]; then host_appdir="/sd/UserApps/$d";
        elif [ -d "$SIM_CARD/system/SystemApps/$d" ]; then host_appdir="/sd/system/SystemApps/$d";
        elif [ -f "$SIM_CARD/system/SystemApps/$d" ]; then host_appdir="/sd/system/SystemApps";
        elif [ -d "$SIM_CARD/apps/$d" ]; then host_appdir="/sd/apps/$d";
        else host_appdir="/sd/UserApps/$d"; fi
        for script in tap hold-corner; do
            rm -rf "$hostsd" && cp -r "$SIM_CARD" "$hostsd"
            out="$(HOST_APPDIR="$host_appdir" HOST_SD="$hostsd" HOST_LOCAL="$hostlocal" timeout 20 "$b/host" "$b/prelude.all.js" "$app" 300 "$script" 2>&1)"; rc=$?
            if [ $rc -eq 0 ]; then pass=$((pass+1)); echo "PASS $name [$script]"; else fail=$((fail+1)); echo "FAIL $name [$script]"; echo "$out" | sed 's/^/    /' | head -8; fi
        done
    done
    echo "native app test: $pass passed, $fail failed"; [ $fail -eq 0 ]
}

test_cmd() {
    local what="${1:-all}"
    [ $# -gt 0 ] && shift || true
    case "$what" in
        all)
            echo "== server selftest =="
            ( server_cmd selftest )
            echo "== native app test =="
            test_native "$@"
            echo "== simulator/launcher extensive =="
            ( emu_cmd_test_extensive "$@" )
            ;;
        server) server_cmd selftest "$@" ;;
        native) test_native "$@" ;;
        emu|launcher|extensive|simulator-extensive) emu_cmd_test_extensive "$@" ;;
        wifi) bash "$SIM/tests/wifi_server_check.sh" "$@" ;;
        ble) bash "$SIM/tests/ble_hci_check.sh" "$@" ;;
        store) bash "$SIM/tests/store_install_check.sh" "$@" ;;
        help|-h|--help) echo "usage: ./start.sh test [all|server|native|emu|launcher|extensive|wifi|ble|store]" ;;
        *) die "unknown test '$what' (all, server, native, emu, launcher, extensive, wifi, ble, store; see: ./start.sh test help)" ;;
    esac
}

project_files() {
    find "$ROOT" \
        \( -path "$ROOT/.git" -o -path "$ROOT/.claude" -o -path "$ROOT/.env" -o -path "$ROOT/Resources" -o -path "$ROOT/firmware/.pio" \
           -o -path "$ROOT/server/target" -o -path "$ROOT/server/var" \
           -o -path "$ROOT/simulator/build" -o -path "$ROOT/simulator/logs" -o -name node_modules -type d \) -prune -o \
        -type f -print0
}

scan() {
    local f rel hits
    local -a secret=() ignored=() content=()
    # The scan's own patterns: lines tagged scan-self are not content hits.
    local pat_key='BEGIN (RSA |EC )?PRIVATE KEY'   # scan-self
    local pat_bearer='Bearer [A-Za-z0-9_-]{30,}'   # scan-self
    local pat_home='/home/[a-z]+/'                 # scan-self

    # Names: every file on disk. A match git ignores is the owner's local file: reported, not a failure.
    while IFS= read -r -d '' f; do
        is_secret_file "$f" || continue
        rel="${f#"$ROOT/"}"
        if git -C "$ROOT" check-ignore -q -- "$rel"; then
            ignored+=("$rel")
        else
            secret+=("$rel")
        fi
    done < <(project_files)

    # Content: tracked files plus untracked files git does not ignore. Only file names are printed.
    while IFS= read -r -d '' rel; do
        [ -f "$ROOT/$rel" ] || continue
        hits="$(grep -IEn -e "$pat_key" -e "$pat_bearer" -e "$pat_home" -- "$ROOT/$rel" 2>/dev/null | grep -v 'scan-self' || true)"
        if [ -n "$hits" ]; then
            content+=("$rel")
        fi
    done < <(git -C "$ROOT" ls-files -z --cached --others --exclude-standard)

    for rel in "${ignored[@]}"; do echo "ignored local file (not a failure): $rel"; done
    for rel in "${secret[@]}"; do echo "SECRET FILE: $rel"; done
    for rel in "${content[@]}"; do echo "CONTENT PATTERN: $rel"; done

    if [ "${#secret[@]}" -gt 0 ]; then
        echo "secret files found: ${#secret[@]} (names above)"
    fi
    if [ "${#content[@]}" -gt 0 ]; then
        echo "content patterns found: ${#content[@]} file(s) (names above; matched text not printed)"
    fi
    if [ $(( ${#secret[@]} + ${#content[@]} )) -eq 0 ]; then
        echo "clean: no secret files and no content patterns in tracked or untracked files"
        return 0
    fi
    return 1
}



# ----- esp-emu (ESP32 emulator) -------------------------------------------

EMU_DIR="$SIM/emu"
EMU_FW_BUILD_HW="$FW/.pio/build/es3c28p"
EMU_FW_BUILD_EMU="$FW/.pio/build/es3c28p_emu"
EMU_FW_BUILD="$EMU_FW_BUILD_EMU"
EMU_ESP="$SIM/esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu"
EMU_HOST="$EMU_DIR/lcd_touch_host"
EMU_MERGED="$SIM/build/merged-emu.bin"
EMU_ELF="$EMU_FW_BUILD/firmware.elf"
EMU_LOG_DIR="$SIM/logs"
EMU_BOARD_DEFAULT="$EMU_DIR/board-es3c28p.json"
EMU_LAUNCHER="$SIM/launcher/sim_launcher"
EMU_MOCKUP_STUDIO="$SIM/mockup_studio/mockup_studio"
EMU_TEST_SCRIPT="$SIM/tests/run_extensive.sh"

# sim_ports N: the only place that knows the emulator ports. Instance N (0-99) adds 10*N to the base
# ports: cosim 5591+10N, uart 5555+10N, control 5556+10N, gdb 3333+10N (N=0 gives the base ports).
# Sets SIM_PORT_COSIM, SIM_PORT_UART, SIM_PORT_CONTROL, SIM_PORT_GDB.
sim_ports() {
    local n="${1:-0}"
    [[ "$n" =~ ^[0-9]{1,2}$ ]] || die "emu: instance must be a number 0-99 (got '$n')"
    n=$((10#$n))
    SIM_PORT_COSIM=$((5591 + 10 * n))
    SIM_PORT_UART=$((5555 + 10 * n))
    SIM_PORT_CONTROL=$((5556 + 10 * n))
    SIM_PORT_GDB=$((3333 + 10 * n))
}

# emu_resolve_instance DEFAULT ARGS...: prints the instance of this run. Order: --instance N in ARGS,
# then SIM_INSTANCE, then emu_runtime.instance of --profile (when the profile sets it), then DEFAULT.
emu_resolve_instance() {
    local default="$1" cli="" prof="" a
    shift
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --instance) cli="${2:?--instance N}"; shift 2 ;;
            --profile) prof="${2:?--profile NAME|FILE}"; shift 2 ;;
            --) break ;;
            *) shift ;;
        esac
    done
    if [[ -z "$cli" && -n "${SIM_INSTANCE:-}" ]]; then cli="$SIM_INSTANCE"; fi
    if [[ -z "$cli" && -n "$prof" ]]; then emu_profile_load "$prof"; cli="$EMU_PROF_INSTANCE"; fi
    a="${cli:-$default}"
    [[ "$a" =~ ^[0-9]{1,2}$ ]] || die "emu: instance must be a number 0-99 (got '$a')"
    printf '%s' "$((10#$a))"
}

emu_usage() {
    cat <<'EOF'
ESP32 emulator commands (esp-emu 1.1.0, simulator/).

Usage:
  ./start.sh emu help|--help
  ./start.sh emu info
  ./start.sh emu merge [--board ID] [--bootloader F] [--partitions F] [--app F]
                       [--littlefs F] [--flash-mode M] [--flash-freq F] [--flash-size S]
                       [--out FILE] [--env E|--emu|--hw] [--dry-run] [--esptool PATH]
                       [--boot-offset N] [--part-offset N] [--app-offset N] [--fs-offset N]
  ./start.sh emu merge --help
  ./start.sh emu build-host
  ./start.sh emu build-launcher
  ./start.sh emu build-mockup
  ./start.sh emu build [all]
  ./start.sh emu run [--instance N] [--firmware|--merged FILE] [--elf FILE|--no-elf] [--board [FILE]]
                     [--profile NAME|FILE] [--no-sd]
                     [--uart-tcp [HOST:PORT]] [--cosim-log FILE] [--gdb [PORT]] [--gdb-halt]
                     [--trace FILE] [--control-tcp [HOST:PORT]] [-- extra esp-emu args]
  ./start.sh emu bootlog [SECONDS] [--instance N] [--firmware|--merged FILE] [--elf FILE|--no-elf]
                         [--board [FILE]] [--uart-tcp [HOST:PORT]] [--cosim-log FILE]
                         [--gdb [PORT]] [--gdb-halt] [--trace FILE] [--control-tcp [HOST:PORT]]
                         [-- extra esp-emu args]
  ./start.sh emu ui [--instance N] [--firmware|--merged FILE] [--elf FILE|--no-elf] [--hw-rot 0-3]
                    [--board [FILE]] [--port P] [--uart-tcp [HOST:PORT]]
                    [--profile NAME|FILE] [--no-sd]
                    [--cosim-unix PATH] [--cosim-log FILE] [--no-audio]
                    [--i2s-wav FILE] [--i2s-wav-in FILE] [--console MODE]
                    [--net MODE] [--gdb [PORT]] [--gdb-halt] [--trace FILE]
                    [--control-tcp [HOST:PORT]] [--no-host] [--debug|--debug-touch|-v]
                    [-- extra esp-emu args]
  ./start.sh emu shot [--instance N] [--secs N] [--out FILE.png] [--cmd TEXT]... [--sd IMG]
                      [--profile NAME|FILE] [--port P] [--uart-port P] [--net MODE]
                      [--wifi-ssid S] [--wifi-password P] [--wifi-auth A]
  ./start.sh emu launcher [-- args]
  ./start.sh emu mockup [run|build]
  ./start.sh emu console [--instance N] [--port P | --target HOST:PORT | HOST:PORT]
  ./start.sh emu test

Subcommands:
  info                    Show tool paths, file presence, esp-emu/esptool versions.
  merge                   Build merged flash image with esptool merge-bin (ESP32-S3).
  build-host              Build SDL2 cosim LCD+touch host (gcc + pkg-config + sdl2).
  build-launcher          Build unified GTK simulator launcher (simulator/launcher/sim_launcher).
  build-mockup            Build mockup studio UI (simulator/mockup_studio/mockup_studio).
  build [all]             Build host + launcher + mockup (default: all).
  run                     Run firmware in esp-emu (serial/cosim headless from this script).
  bootlog                 Run with timeout and save UART output to simulator/logs/boot.log.
  ui                      Start esp-emu (cosim-tcp) + SDL host UI; host reboot loop keeps hw-rot.
  launcher                Build (if needed) and run unified GTK simulator launcher.
  mockup                  Build/run mockup studio UI (mockup run|build).
  cosim                   Alias for ui --no-host.
  console                 Attach to UART TCP console (socat preferred, nc fallback).
  shot [--instance N] [--secs N] [--out F.png] [--cmd "ls /sd"]... [--profile NAME|FILE] [--port P --uart-port P]
                          Headless test run: boots with the SD image, sends console commands, saves the console replies and a capture of the emulated LCD.
  test                    Run extensive simulator/launcher battery (simulator/tests/run_extensive.sh).

Common flags (where applicable):
  --firmware, --merged F  Firmware image (default: simulator/build/merged-emu.bin).
  --elf F                 ELF for symbols/backtraces/hooks/GDB/trace (default: es3c28p_emu firmware.elf).
  --no-elf                Do not pass --elf (use for release bins without matching ELF).
  --gdb [PORT]            Enable esp-emu GDB stub on PORT (conditional(tool/runtime); pair with matching gdb/ELF).
  --gdb-halt              Halt on startup for GDB attach (use with --gdb).
  --trace FILE            Write esp-emu execution trace to FILE (conditional(tool/runtime); typically needs --elf).
  --control-tcp [H:P]     Expose esp-emu control TCP endpoint (conditional(version/tool)).
  --board [FILE]          esp-emu --cosim-board JSON (default file when omitted: board-es3c28p.json).
  --instance N            Emulator instance 0-99 (env SIM_INSTANCE; profile emu_runtime.instance; default 0, shot 9).
                          Ports = base + 10*N: cosim 5591, uart 5555, control 5556, gdb 3333 (N=0).
  --port P                Cosim TCP port for ui and shot (default: 5591 + 10*N, localhost).
  --uart-tcp [HOST:PORT]  Expose UART over TCP (default when omitted: 127.0.0.1:uart port of the instance, 5555 + 10*N).
  --gdb [PORT]            GDB stub port (default when omitted: 3333 + 10*N).
  --control-tcp [HOST:PORT]  Control endpoint (default when omitted: 127.0.0.1:5556 + 10*N).
  --cosim-unix PATH       ui: bridge cosim TCP to UNIX socket via socat for host --unix.
  --cosim-log FILE        Pass esp-emu --cosim-log FILE.
  --hw-rot 0-3            ui: initial LCD/touch hardware rotation (persisted in emulator state dir).
  --no-audio              ui: disable default --i2s-wav capture.
  --i2s-wav FILE          ui/run/bootlog extra-friendly: forwarded to esp-emu (ui also uses as capture path).
  --i2s-wav-in FILE       Forwarded to esp-emu (audio input WAV, if supported by binary/firmware).
  --console MODE          Forwarded to esp-emu (e.g. auto|uart|usb-cdc, binary-dependent).
  --net MODE              Forwarded to esp-emu (e.g. user; tap/vmnet host/version dependent).
  --profile NAME|FILE     run/ui/shot: read simulator/launcher/profiles/NAME.json (or a path with / or .json).
                          storage.sd_enabled false = no --sd; storage.sd_image_path = other card image;
                          network_radio.net_mode -> --net; network_radio.wifi (enabled, ssid, auth) -> --wifi-ssid/
                          --wifi-auth (open adds --wifi-password ""); network_radio.ble (enabled, hci) -> --ble-hci;
                          audio.out -> --i2s-wav, audio.in -> --i2s-wav-in (repo-relative paths; null = unset);
                          emu_runtime.instance -> instance (see --instance).
                          Needs jq. Command-line flags win over the profile. Wi-Fi passphrase (non-open auth)
                          comes only from the SIM_WIFI_PASSWORD environment variable, never from the profile.
  --no-host               ui: start esp-emu/cosim backend only, do not launch SDL host.
  --debug, --debug-touch, -v
                          ui: passed to LCD+touch host.
  --                      ui: pass remaining args to esp-emu.

Firmware formats and merge:
  - Preferred emu input: merged flash image (--firmware). Default target: merged-emu.bin.
  - merge presets (see: ./start.sh emu merge --help):
      es3c28p                   firmware/.pio/build/es3c28p_emu (fallback: asset dir),
                                merged-emu.bin, 16MB, offsets 0x0/0x8000/0x10000/0xc90000.
      esp32-s3-devkitc-1-n16r8  kryonos-v3 split assets -> merged-<board>.bin, 16MB.
      lilygo-t-deck             idem.
      lilygo-t-hmi              idem.
      m5stack-cardputer         kryonos-v3 split assets -> merged-<board>.bin, 8MB, no FS offset.
  - Override fields: --bootloader/--partitions/--app/--littlefs, --boot-offset/--part-offset/
    --app-offset/--fs-offset, --flash-mode/--flash-freq/--flash-size, --out, --esptool.
  - esptool >=5 uses merge-bin; older uses merge_bin (auto-detected). --dry-run prints the
    validated argv command without writing files.
  - Split release assets can be merged with esptool; pass resulting merged bin via --firmware and usually --no-elf.
  - Build firmware first if needed, e.g.: (cd firmware && pio run -e es3c28p_emu)

Capability notes (esp-emu 1.1.0, ESP32-S3 / es3c28p focus):
  - SDMMC/SDSPI card: esp-emu 1.1.0 provides --sd for the S3 SDMMC host; ./start.sh card image builds the image. Use LittleFS/data image when possible.
  - es3c28p FS region 0xc90000..0xff0000 is 3.375MB; simulator/SD_Card (~37MB) does not fit.
  - Stock releases may boot/hang without emu guards; es3c28p_emu build skips problematic audio/SD/ADC paths.
  - Release firmware often has no matching ELF: use --no-elf (reduced symbols/debug hooks).
  - Magic checks are warnings only (image 0xE9, partition table 0xAA50).
  - LCD/touch UI relies on cosim host path (board JSON + SDL host / cosim transport), not full native panel emulation.
  - Wi-Fi/BLE/USB backend behavior is binary, host, and firmware dependent; validate with emu info/bootlog.
  - Unknown/extra flags can be forwarded after -- (ui) or as trailing args (run/bootlog) when safe for esp-emu.

State/logs: simulator/build, simulator/logs (boot.log, i2s_out.wav, cosim-bridge.log).
EOF
}

emu_find_esptool_soft() {
    if [[ -n "${ESPTOOL:-}" && -x "$ESPTOOL" ]]; then printf '%s\n' "$ESPTOOL"; return 0; fi
    local penv="$HOME/.platformio/penv/bin/esptool"
    [[ -x "$penv" ]] && { printf '%s\n' "$penv"; return 0; }
    local c
    for c in "$HOME/.local/bin/esptool" "$HOME/.local/bin/esptool.py" \
             /usr/local/bin/esptool /usr/bin/esptool /usr/local/bin/esptool.py /usr/bin/esptool.py; do
        [[ -x "$c" ]] && { printf '%s\n' "$c"; return 0; }
    done
    if command -v esptool >/dev/null 2>&1; then command -v esptool; return 0; fi
    if command -v esptool.py >/dev/null 2>&1; then command -v esptool.py; return 0; fi
    return 1
}

emu_require_esptool() {
    local tool
    if tool="$(emu_find_esptool_soft)"; then printf '%s\n' "$tool"; return 0; fi
    die "emu: esptool not found. Install 'esptool'/'esptool.py' or PlatformIO penv (~/.platformio/penv/bin/esptool). Needed for: ./start.sh emu merge"
}

emu_require_esp() {
    [ -x "$EMU_ESP" ] || die "emu: esp-emu binary not found or not executable: $EMU_ESP. Install/place esp-emu 1.1.0 x86_64 Linux build there, or run ./start.sh emu info."
}

emu_require_file() {
    local path="$1" label="$2" hint="${3:-}"
    if [ ! -f "$path" ]; then
        if [ -n "$hint" ]; then die "emu: $label not found: $path ($hint)"; else die "emu: $label not found: $path"; fi
    fi
    [ -r "$path" ] || die "emu: $label not readable: $path"
}

emu_merge_usage() {
    cat <<'EOF'
./start.sh emu merge [options]

Build a merged ESP32-S3 flash image with esptool (merge-bin on esptool >= 5,
merge_bin on older; auto-detected). No shell is used to build the command.

Board presets:
  --board ID               es3c28p (default), esp32-s3-devkitc-1-n16r8, lilygo-t-deck,
                           lilygo-t-hmi, m5stack-cardputer.

  es3c28p                  firmware/.pio/build/es3c28p_emu (fallback:
                           simulator/emu/firmware/es3c28p) -> simulator/build/merged-emu.bin
                           16MB qio/80m, offsets 0x0/0x8000/0x10000/0xc90000.
  esp32-s3-devkitc-1-n16r8 simulator/emu/firmware/kryonos-v3/<board> ->
  lilygo-t-deck            simulator/build/merged-<board>.bin, 16MB qio/80m,
  lilygo-t-hmi             offsets 0x0/0x8000/0x10000 (release FS not shipped).
  m5stack-cardputer        same dir, 8MB output; FS offset unknown (set --fs-offset).

Inputs / layout (all optional; preset defaults shown):
  --bootloader FILE        bootloader image (default <src>/bootloader.bin) @ --boot-offset
  --partitions FILE        partition table (default <src>/partitions.bin) @ --part-offset
  --app, --firmware FILE   app image (default <src>/firmware.bin) @ --app-offset
  --littlefs FILE          LittleFS/spiffs/data image @ --fs-offset (optional)
  --merged FILE            Already-merged image: no-op or copy to --out
  --boot-offset N          default 0x0
  --part-offset N          default 0x8000
  --app-offset N           default 0x10000
  --fs-offset N            preset 0xc90000 (16MB boards), 0x0/unknown for cardputer

Flash / tool / output:
  --flash-mode M           qio (default)
  --flash-freq F           80m (default)
  --flash-size S           16MB (default; m5stack-cardputer 8MB)
  --out, -o FILE           output image (preset default: simulator/build/merged-*.bin)
  --esptool PATH           explicit esptool executable (default: autodetect)
  --dry-run                show validation + argv command; write nothing
  --env E                  explicit PlatformIO env build dir (compat; overrides preset src)
  --emu, --hw              --env es3c28p_emu / --env es3c28p (compat)
  --help, -h               this help

Validation:
  - inputs must exist, be readable and non-empty
  - offsets must be ordered and segments must not overlap; end must fit flash size
  - output directory must exist or be creatable
  - signature checks are warnings (image 0xE9, partition table 0xAA50)
  - no LittleFS image -> warning (internal FS mount may fail; not blocking)
  - esptool missing -> capability conditional(version/tool); merge needs the tool

Limits (esp-emu 1.1.0, ESP32-S3):
  - SDMMC/SDSPI: esp-emu 1.1.0 provides --sd for the S3 SDMMC host (./start.sh card image builds the image). Preferred data path: merged LittleFS image.
  - es3c28p FS region 0xc90000..0xff0000 = 3.375MB; simulator/SD_Card (~37MB) does not fit.
  - KryonOS v3 release assets have no ELF: run with --no-elf (reduced symbols/backtraces).
EOF
}

emu_merge_preset() {
    case "$1" in
        es3c28p)
            if [[ -d "$FW/.pio/build/es3c28p_emu" ]]; then
                printf '%s|%s|%s|%s\n' "$FW/.pio/build/es3c28p_emu" "$SIM/build/merged-emu.bin" "16MB" "0xc90000"
            else
                printf '%s|%s|%s|%s\n' "$SIM/emu/firmware/es3c28p" "$SIM/build/merged-emu.bin" "16MB" "0xc90000"
            fi
            ;;
        esp32-s3-devkitc-1-n16r8|lilygo-t-deck|lilygo-t-hmi)
            printf '%s|%s|%s|%s\n' "$SIM/emu/firmware/kryonos-v3/$1" "$SIM/build/merged-$1.bin" "16MB" "0xc90000"
            ;;
        m5stack-cardputer)
            printf '%s|%s|%s|%s\n' "$SIM/emu/firmware/kryonos-v3/$1" "$SIM/build/merged-$1.bin" "8MB" "0x0"
            ;;
        *) return 1 ;;
    esac
}

emu_merge_offset() {
    local label="$1" v="$2"
    [[ "$v" =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]] || die "emu merge: invalid $label offset: '$v' (use 0xHEX or decimal)"
    printf '%u' "$((v))"
}

emu_merge_flash_bytes() {
    local s="$1" n u
    if [[ "$s" =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]]; then printf '%u' "$((s))"; return 0; fi
    if [[ "$s" =~ ^([0-9]+)([kKmMgG])([iI]?)([bB]?)$ ]]; then
        n="${BASH_REMATCH[1]}"; u="${BASH_REMATCH[2]}"
        case "${u,,}" in
            k) printf '%u' "$((n * 1024))" ;;
            m) printf '%u' "$((n * 1024 * 1024))" ;;
            g) printf '%u' "$((n * 1024 * 1024 * 1024))" ;;
        esac
        return 0
    fi
    return 1
}

# Prints a human problem and returns 1 on mismatch; returns 0 when plausible.
emu_merge_magic_problem() {
    local path="$1" kind="$2" got
    case "$kind" in
        image)
            got="$(od -An -tx1 -N1 -- "$path" 2>/dev/null | tr -d ' \n')"
            [[ "$got" == "e9" ]] && return 0
            printf 'expected image magic 0xE9, got 0x%s' "${got:-??}"
            ;;
        partitions)
            got="$(od -An -tx1 -N2 -- "$path" 2>/dev/null | tr -d ' \n')"
            [[ "$got" == "aa50" ]] && return 0
            printf 'expected partition magic 0xAA50, got 0x%s' "${got:-????}"
            ;;
        fs)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
    return 1
}

emu_merge_outdir_problem() {
    local out="$1" dir probe parent
    dir="$(dirname -- "$out")"
    probe="$dir"
    while [[ ! -d "$probe" ]]; do
        parent="$(dirname -- "$probe")"
        [[ "$parent" == "$probe" ]] && break
        probe="$parent"
    done
    if [[ ! -d "$probe" ]]; then printf 'output directory unavailable: %s' "$dir"; return 1; fi
    if [[ ! -w "$probe" || ! -x "$probe" ]]; then printf 'output directory not writable: %s' "$probe"; return 1; fi
    return 0
}

emu_cmd_merge() {
    local board="" env="" out="" littlefs="" merged_input=""
    local bootloader="" partitions="" app=""
    local explicit_boot=0 explicit_part=0 explicit_app=0
    local boot_off="" part_off="" app_off="" fs_off=""
    local flash_mode="qio" flash_freq="80m" flash_size=""
    local dry_run=0 esptool_override=""
    local preset_line="" preset_src="" preset_out="" preset_flash="" preset_fs=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help|-h) emu_merge_usage; return 0 ;;
            --board) board="${2:-}"; [[ -n "$board" ]] || die "emu merge: --board requires a board id"; shift 2 ;;
            --board=*) board="${1#*=}"; [[ -n "$board" ]] || die "emu merge: --board requires a board id"; shift ;;
            --env) env="${2:-}"; [[ -n "$env" ]] || die "emu merge: --env requires a name"; shift 2 ;;
            --hw) env="es3c28p"; shift ;;
            --emu) env="es3c28p_emu"; shift ;;
            --out|-o) out="${2:-}"; [[ -n "$out" ]] || die "emu merge: --out requires a path"; shift 2 ;;
            --littlefs) littlefs="${2:-}"; [[ -n "$littlefs" ]] || die "emu merge: --littlefs requires a path"; shift 2 ;;
            --merged) merged_input="${2:-}"; [[ -n "$merged_input" ]] || die "emu merge: --merged requires a path"; shift 2 ;;
            --bootloader) bootloader="${2:-}"; explicit_boot=1; [[ -n "$bootloader" ]] || die "emu merge: --bootloader requires a path"; shift 2 ;;
            --partitions) partitions="${2:-}"; explicit_part=1; [[ -n "$partitions" ]] || die "emu merge: --partitions requires a path"; shift 2 ;;
            --app|--firmware) app="${2:-}"; explicit_app=1; [[ -n "$app" ]] || die "emu merge: --app/--firmware requires a path"; shift 2 ;;
            --boot-offset) boot_off="${2:-}"; shift 2 ;;
            --part-offset) part_off="${2:-}"; shift 2 ;;
            --app-offset) app_off="${2:-}"; shift 2 ;;
            --fs-offset) fs_off="${2:-}"; shift 2 ;;
            --flash-mode) flash_mode="${2:-}"; [[ -n "$flash_mode" ]] || die "emu merge: --flash-mode requires a value"; shift 2 ;;
            --flash-freq) flash_freq="${2:-}"; [[ -n "$flash_freq" ]] || die "emu merge: --flash-freq requires a value"; shift 2 ;;
            --flash-size) flash_size="${2:-}"; [[ -n "$flash_size" ]] || die "emu merge: --flash-size requires a value"; shift 2 ;;
            --esptool) esptool_override="${2:-}"; [[ -n "$esptool_override" ]] || die "emu merge: --esptool requires a path"; shift 2 ;;
            --dry-run) dry_run=1; shift ;;
            *) die "emu merge: unknown arg: $1 (see: ./start.sh emu merge --help)" ;;
        esac
    done

    [[ -n "$board" ]] || board="es3c28p"
    preset_line="$(emu_merge_preset "$board")" || die "emu merge: unknown board '$board' (valid: es3c28p, esp32-s3-devkitc-1-n16r8, lilygo-t-deck, lilygo-t-hmi, m5stack-cardputer)"
    IFS='|' read -r preset_src preset_out preset_flash preset_fs <<<"$preset_line"

    local src_dir="$preset_src"
    if [[ -n "$env" ]]; then
        src_dir="$FW/.pio/build/$env"
        [[ -d "$src_dir" ]] || die "emu merge: build dir not found: $src_dir. Build firmware first: (cd firmware && pio run -e $env)"
        if [[ -z "$out" ]]; then
            if [[ "$env" == "es3c28p_emu" ]]; then out="$SIM/build/merged-emu.bin"; else out="$SIM/build/merged.bin"; fi
        fi
    fi
    [[ -n "$flash_size" ]] || flash_size="$preset_flash"
    [[ -n "$out" ]] || out="$preset_out"

    [[ -n "$bootloader" ]] || bootloader="$src_dir/bootloader.bin"
    [[ -n "$partitions" ]] || partitions="$src_dir/partitions.bin"
    [[ -n "$app" ]] || app="$src_dir/firmware.bin"
    if [[ -z "$littlefs" ]]; then
        if [[ -f "$src_dir/littlefs.bin" ]]; then littlefs="$src_dir/littlefs.bin"
        elif [[ -f "$src_dir/spiffs.bin" ]]; then littlefs="$src_dir/spiffs.bin"; fi
    fi

    # Already-merged input: no-op (or copy) instead of trying to merge a merged bin.
    local merged_candidate=""
    if [[ -n "$merged_input" ]]; then
        merged_candidate="$merged_input"
    elif [[ "$explicit_app" -eq 1 && "$explicit_boot" -eq 0 && "$explicit_part" -eq 0 && "$(basename -- "$app")" == *merged* ]]; then
        merged_candidate="$app"
    fi
    if [[ -n "$merged_candidate" ]]; then
        emu_require_file "$merged_candidate" "merged firmware image"
        local mproblem
        if ! mproblem="$(emu_merge_magic_problem "$merged_candidate" image)"; then
            echo "WARN: emu merge: $merged_candidate: $mproblem" >&2
        fi
        if [[ "$out" == "$merged_candidate" ]]; then
            echo "Already merged (no split inputs): $merged_candidate"
            return 0
        fi
        if [[ "$dry_run" -eq 1 ]]; then
            printf 'DRY-RUN: cp %q %q\n' "$merged_candidate" "$out"
            echo "No files written."
            return 0
        fi
        emu_merge_outdir_problem "$out" >/dev/null || die "emu merge: $(emu_merge_outdir_problem "$out")"
        mkdir -p -- "$(dirname -- "$out")"
        cp -- "$merged_candidate" "$out"
        echo "Merged image already provided; copied to: $out"
        return 0
    fi

    # Inputs.
    local hint="build firmware first: (cd firmware && pio run -e ${env:-es3c28p_emu})"
    emu_require_file "$bootloader" "bootloader image" "$hint"
    emu_require_file "$partitions" "partitions image" "$hint"
    emu_require_file "$app" "app firmware image" "$hint"
    if [[ -n "$littlefs" ]]; then
        emu_require_file "$littlefs" "littlefs/data image" "pass a valid image path to --littlefs"
    fi

    # Offsets.
    [[ -n "$boot_off" ]] || boot_off="0x0"
    [[ -n "$part_off" ]] || part_off="0x8000"
    [[ -n "$app_off" ]] || app_off="0x10000"
    local boot_off_d part_off_d app_off_d fs_off_d=0
    boot_off_d="$(emu_merge_offset bootloader "$boot_off")"
    part_off_d="$(emu_merge_offset partitions "$part_off")"
    app_off_d="$(emu_merge_offset app "$app_off")"
    if [[ -n "$littlefs" ]]; then
        [[ -n "$fs_off" ]] || die "emu merge: --littlefs requires --fs-offset for board '$board' (layout offset unknown)"
        fs_off_d="$(emu_merge_offset fs "$fs_off")"
        if [[ "$fs_off_d" == "0" ]]; then die "emu merge: --fs-offset must be non-zero for an included data image"; fi
    fi

    # Sizes and ordering.
    local boot_sz part_sz app_sz fs_sz=0
    boot_sz="$(stat -c %s -- "$bootloader")"
    part_sz="$(stat -c %s -- "$partitions")"
    app_sz="$(stat -c %s -- "$app")"
    [[ "$boot_sz" -gt 0 ]] || die "emu merge: bootloader image is empty: $bootloader"
    [[ "$part_sz" -gt 0 ]] || die "emu merge: partitions image is empty: $partitions"
    [[ "$app_sz" -gt 0 ]] || die "emu merge: app image is empty: $app"
    if [[ -n "$littlefs" ]]; then
        fs_sz="$(stat -c %s -- "$littlefs")"
        [[ "$fs_sz" -gt 0 ]] || die "emu merge: littlefs/data image is empty: $littlefs"
    fi

    local roles=(bootloader partitions app littlefs)
    local paths=("$bootloader" "$partitions" "$app" "${littlefs:-}")
    local offs=("$boot_off_d" "$part_off_d" "$app_off_d" "$fs_off_d")
    local sizes=("$boot_sz" "$part_sz" "$app_sz" "$fs_sz")
    local prev_off=0 prev_end=0 have_prev=0 i name path off sz
    for i in "${!roles[@]}"; do
        name="${roles[$i]}"; path="${paths[$i]}"; off="${offs[$i]}"; sz="${sizes[$i]}"
        [[ -n "$path" && "$sz" -gt 0 ]] || continue
        if [[ "$have_prev" -eq 1 ]]; then
            [[ "$off" -gt "$prev_off" ]] || die "emu merge: $name offset ($off) must be greater than previous offset ($prev_off)"
            [[ "$off" -ge "$prev_end" ]] || die "emu merge: $name offset ($off) overlaps previous segment (previous end $prev_end)"
        fi
        prev_off="$off"
        prev_end="$((off + sz))"
        have_prev=1
    done

    local flash_bytes
    if flash_bytes="$(emu_merge_flash_bytes "$flash_size")"; then
        [[ "$prev_end" -le "$flash_bytes" ]] || die "emu merge: last segment ends at $prev_end, beyond flash size $flash_bytes ($flash_size)"
    else
        echo "WARN: emu merge: unrecognised flash size '$flash_size'; flash bound check skipped" >&2
    fi
    if [[ -n "$littlefs" && "$fs_off_d" -eq $((0xc90000)) ]]; then
        local fs_limit=$((0x360000))
        [[ "$fs_sz" -le "$fs_limit" ]] || die "emu merge: littlefs image is $fs_sz bytes; FS region 0xc90000..0xff0000 holds at most $fs_limit bytes (SD_Card ~37MB does not fit)"
    fi

    # Signature hints (warnings only).
    local problem
    if ! problem="$(emu_merge_magic_problem "$bootloader" image)"; then echo "WARN: emu merge: bootloader: $problem ($bootloader)" >&2; fi
    if ! problem="$(emu_merge_magic_problem "$partitions" partitions)"; then echo "WARN: emu merge: partitions: $problem ($partitions)" >&2; fi
    if ! problem="$(emu_merge_magic_problem "$app" image)"; then echo "WARN: emu merge: app: $problem ($app)" >&2; fi
    if [[ -n "$littlefs" ]]; then
        if ! problem="$(emu_merge_magic_problem "$littlefs" fs)"; then echo "WARN: emu merge: littlefs: $problem ($littlefs)" >&2; fi
        echo "Including data image: $littlefs @ $fs_off"
    else
        echo "WARN: emu merge: no LittleFS/data image included; internal FS mount may fail (often non-blocking)."
        echo "      SDMMC/SDSPI is not in the merged image; esp-emu 1.1.0 provides --sd for it (./start.sh card image builds the image)." >&2
    fi

    # Output directory.
    local out_problem=""
    if ! out_problem="$(emu_merge_outdir_problem "$out")"; then
        if [[ "$dry_run" -eq 1 ]]; then echo "WARN: emu merge: $out_problem" >&2
        else die "emu merge: $out_problem"; fi
    fi

    # Tool + subcommand.
    local esptool_cmd="" capability="conditional(version/tool)" have_tool=0
    if [[ -n "$esptool_override" ]]; then
        esptool_cmd="$esptool_override"
        if [[ -x "$esptool_cmd" ]] || command -v "$esptool_cmd" >/dev/null 2>&1; then have_tool=1; fi
        if [[ "$have_tool" -eq 0 ]]; then
            if [[ "$dry_run" -eq 1 ]]; then echo "WARN: emu merge: --esptool '$esptool_cmd' is not executable; dry-run keeps it as-is" >&2
            else die "emu merge: --esptool is not executable: $esptool_cmd"; fi
        fi
    elif esptool_cmd="$(emu_find_esptool_soft)"; then
        have_tool=1
    else
        esptool_cmd="esptool"
        if [[ "$dry_run" -eq 1 ]]; then
            echo "WARN: emu merge: esptool not found; capability conditional(version/tool); dry-run uses 'esptool'" >&2
        else
            die "emu: esptool not found. Install 'esptool'/'esptool.py' or PlatformIO penv (~/.platformio/penv/bin/esptool). Needed for: ./start.sh emu merge"
        fi
    fi
    local subcmd="merge-bin"
    if [[ "$have_tool" -eq 1 ]]; then
        capability="supported-now"
        local ver_line="" ver_major=""
        ver_line="$("$esptool_cmd" version 2>/dev/null | head -1 || true)"
        ver_major="$(printf '%s' "$ver_line" | grep -oE '[0-9]+' | head -1 || true)"
        if [[ -n "$ver_major" && "$ver_major" -lt 5 ]]; then subcmd="merge_bin"; fi
    fi

    # Summary.
    echo "Merge plan: board=$board chip=esp32s3 flash=$flash_mode/$flash_freq/$flash_size"
    echo "Input dir: $src_dir"
    echo "Output:    $out"
    printf '  %-11s offset 0x%06x size %8s  %s\n' bootloader "$boot_off_d" "$boot_sz" "$bootloader"
    printf '  %-11s offset 0x%06x size %8s  %s\n' partitions "$part_off_d" "$part_sz" "$partitions"
    printf '  %-11s offset 0x%06x size %8s  %s\n' app "$app_off_d" "$app_sz" "$app"
    if [[ -n "$littlefs" ]]; then
        printf '  %-11s offset 0x%06x size %8s  %s\n' littlefs "$fs_off_d" "$fs_sz" "$littlefs"
    fi
    echo "esptool:   $esptool_cmd ($subcmd, $capability)"

    local opt_mode="--flash-mode" opt_freq="--flash-freq" opt_size="--flash-size"
    if [[ "$subcmd" == "merge_bin" ]]; then
        opt_mode="--flash_mode"; opt_freq="--flash_freq"; opt_size="--flash_size"
    fi
    local args=(--chip esp32s3 "$subcmd" -o "$out"
        "$opt_mode" "$flash_mode" "$opt_freq" "$flash_freq" "$opt_size" "$flash_size"
        "$boot_off" "$bootloader" "$part_off" "$partitions" "$app_off" "$app")
    if [[ -n "$littlefs" ]]; then args+=("$fs_off" "$littlefs"); fi

    if [[ "$dry_run" -eq 1 ]]; then
        printf 'DRY-RUN: '
        printf '%q ' "$esptool_cmd" "${args[@]}"
        printf '\nNo files written.\n'
        return 0
    fi

    mkdir -p -- "$(dirname -- "$out")"
    "$esptool_cmd" "${args[@]}"
    echo "Merged image: $out (board=$board${env:+, env=$env})"
}

emu_cmd_build_host() {
    command -v gcc >/dev/null 2>&1 || die "emu build-host: gcc not found. Install a C compiler (e.g. build-essential)."
    command -v pkg-config >/dev/null 2>&1 || die "emu build-host: pkg-config not found. Install pkg-config."
    emu_require_file "$EMU_DIR/lcd_touch_host.c" "LCD/touch host source"
    emu_require_file "$EMU_DIR/sim_host.c" "cosim host source"
    if ! pkg-config --exists sdl2 2>/dev/null; then
        die "emu build-host: SDL2 development files not found (pkg-config sdl2 failed). Install libsdl2-dev/sdl2-devel."
    fi
    gcc -O2 -Wall -Wextra -I"$EMU_DIR" -o "$EMU_HOST" "$EMU_DIR/lcd_touch_host.c" "$EMU_DIR/sim_host.c" $(pkg-config --cflags --libs sdl2)
    echo "Built: $EMU_HOST"
}

# emu_posix_tz: the PC's current UTC offset as a POSIX TZ without DST rules (+01:00 -> <+01>-1, -03:00 -> <-03>3).
emu_posix_tz() {
    local z sign hh mm name off
    z="$(date +%z)"
    sign="${z:0:1}"; hh=$((10#${z:1:2})); mm=$((10#${z:3:2}))
    name="${sign}$(printf '%02d' "$hh")"
    if (( mm )); then name="${name}$(printf '%02d' "$mm")"; off="$hh:$(printf '%02d' "$mm")"; else off="$hh"; fi
    if (( hh == 0 && mm == 0 )); then off=0; elif [[ "$sign" == "+" ]]; then off="-$off"; fi
    printf '<%s>%s' "$name" "$off"
}

# emu_uart_port ARGS...: the port of the last "--uart-tcp HOST:PORT" in ARGS (empty when none).
emu_uart_port() {
    local prev="" a port=""
    for a in "$@"; do
        if [[ "$prev" == "--uart-tcp" && "$a" == *:* ]]; then port="${a##*:}"; fi
        prev="$a"
    done
    printf '%s' "$port"
}

# emu_sync_time UART_PORT: set the emulated clock to this PC's time, in the background.
# The emulator has no NTP, so the firmware would show its build time. The console reads UART0
# only once its main loop runs, so "time set" is sent at +8, +16 and +26 s after the port opens.
emu_sync_time() {
    local port="${1:-}" i up=0
    [[ "$port" =~ ^[0-9]+$ ]] || return 0
    (
        for ((i = 0; i < 60; i++)); do
            if ( exec 3<>"/dev/tcp/127.0.0.1/$port" ) 2>/dev/null; then up=1; break; fi
            sleep 1
        done
        [[ $up -eq 1 ]] || exit 0
        for wait in 8 8 10; do
            sleep "$wait"
            ( exec 3<>"/dev/tcp/127.0.0.1/$port" && printf 'time set %s %s\r\n' "$(date +%s)" "$(emu_posix_tz)" >&3 && sleep 1 ) 2>/dev/null || true
        done
        # The emulated chip runs slower than real time (about 8 %), so its clock falls behind: keep
        # setting it every 20 s for as long as the emulator's UART port answers.
        while sleep 20; do
            ( exec 3<>"/dev/tcp/127.0.0.1/$port" && printf 'time set %s %s\r\n' "$(date +%s)" "$(emu_posix_tz)" >&3 && sleep 1 ) 2>/dev/null || break
        done
    ) >/dev/null 2>&1 &
    return 0
}

# Profiles (emu run/shot/ui --profile NAME|FILE). NAME = simulator/launcher/profiles/NAME.json; a
# value with / or .json is a path. Reads storage (SD) and network_radio (net, Wi-Fi, BLE) keys with jq.
# Explicit command-line options win over the profile. The Wi-Fi passphrase is read only from
# SIM_WIFI_PASSWORD, never from the profile, and is never printed.
EMU_PROFILE_DIR="$SIM/launcher/profiles"

emu_argv_has() {
    local flag="$1" a
    shift
    for a in "$@"; do [[ "$a" == "$flag" ]] && return 0; done
    return 1
}

# emu_profile_load SPEC: sets EMU_PROF_NO_SD (0/1), EMU_PROF_SD_IMG ("" = default card image),
# EMU_PROF_NET, EMU_PROF_WIFI_SSID, EMU_PROF_WIFI_AUTH, EMU_PROF_BLE_HCI, EMU_PROF_AUDIO_OUT,
# EMU_PROF_AUDIO_IN (repo-relative paths made absolute; "" = unset), EMU_PROF_INSTANCE ("" = not set).
emu_profile_load() {
    local spec="$1" file schema sd_enabled sd_path sd_abs net wifi_on ssid auth ble_on hci aout ain inst
    command -v jq >/dev/null 2>&1 || die "emu --profile needs jq (not found in PATH; install jq)"
    if [[ "$spec" == */* || "$spec" == *.json ]]; then file="$spec"; else file="$EMU_PROFILE_DIR/$spec.json"; fi
    if [[ "$file" != /* && ! -f "$file" && -f "$ROOT/$file" ]]; then file="$ROOT/$file"; fi
    [ -f "$file" ] || die "emu profile not found: $file (names live in simulator/launcher/profiles/)"
    jq -e 'type == "object"' "$file" >/dev/null 2>&1 || die "emu profile is not a JSON object: $file"
    schema="$(jq -r '.schema_version // "simulator.profile/v1"' "$file")"
    [[ "$schema" == "simulator.profile/v1" ]] \
        || die "emu profile $file: schema_version is '$schema', expected simulator.profile/v1"
    sd_enabled="$(jq -r 'if .storage.sd_enabled == false then "0" else "1" end' "$file")"
    sd_path="$(jq -r '.storage.sd_image_path // ""' "$file")"
    net="$(jq -r '.network_radio.net_mode // ""' "$file")"
    wifi_on="$(jq -r '.network_radio.wifi.enabled // false' "$file")"
    ssid="$(jq -r '.network_radio.wifi.ssid // ""' "$file")"
    auth="$(jq -r '.network_radio.wifi.auth // ""' "$file")"
    ble_on="$(jq -r '.network_radio.ble.enabled // false' "$file")"
    hci="$(jq -r '.network_radio.ble.hci // ""' "$file")"
    aout="$(jq -r '.audio.out // ""' "$file")"
    ain="$(jq -r '.audio.in // ""' "$file")"
    inst="$(jq -r '.emu_runtime.instance // ""' "$file")"

    EMU_PROF_INSTANCE="$inst"
    EMU_PROF_AUDIO_OUT=""; EMU_PROF_AUDIO_IN=""
    if [[ -n "$aout" ]]; then [[ "$aout" == /* ]] || aout="$ROOT/$aout"; EMU_PROF_AUDIO_OUT="$(readlink -m "$aout")"; fi
    if [[ -n "$ain" ]]; then
        [[ "$ain" == /* ]] || ain="$ROOT/$ain"
        ain="$(readlink -m "$ain")"
        [ -f "$ain" ] || die "emu profile $file: audio.in not found: $ain"
        EMU_PROF_AUDIO_IN="$ain"
    fi

    EMU_PROF_NO_SD=0; [[ "$sd_enabled" == "0" ]] && EMU_PROF_NO_SD=1
    EMU_PROF_SD_IMG=""
    if [[ $EMU_PROF_NO_SD -eq 0 && -n "$sd_path" ]]; then
        [[ "$sd_path" == /* ]] || sd_path="$ROOT/$sd_path"
        sd_abs="$(readlink -m "$sd_path")"
        if [[ "$sd_abs" != "$(readlink -m "$SIM_CARD_IMG")" ]]; then
            [ -f "$sd_abs" ] || die "emu profile $file: sd_image_path not found: $sd_abs (./start.sh card image)"
            EMU_PROF_SD_IMG="$sd_abs"
        fi
    fi

    EMU_PROF_NET="$net"
    EMU_PROF_WIFI_SSID=""; EMU_PROF_WIFI_AUTH=""
    if [[ "$wifi_on" == "true" ]]; then
        [[ -n "$ssid" ]] || die "emu profile $file: network_radio.wifi.ssid is empty"
        case "$auth" in
            open|wpa2-psk|wpa3-sae|wpa2-wpa3) ;;
            *) die "emu profile $file: network_radio.wifi.auth must be open|wpa2-psk|wpa3-sae|wpa2-wpa3 (got '$auth')" ;;
        esac
        if [[ "$auth" != open && -z "${SIM_WIFI_PASSWORD:-}" ]]; then
            die "emu profile $file uses Wi-Fi auth '$auth': export SIM_WIFI_PASSWORD (the passphrase is never read from the profile)"
        fi
        EMU_PROF_WIFI_SSID="$ssid"; EMU_PROF_WIFI_AUTH="$auth"
    fi

    EMU_PROF_BLE_HCI=""
    if [[ "$ble_on" == "true" ]]; then
        [[ -n "$hci" ]] || die "emu profile $file: network_radio.ble.hci is empty"
        EMU_PROF_BLE_HCI="$hci"
    fi
}

# emu_profile_esp_args RAW_ARGS...: fills EMU_PROFILE_ARGS with the esp-emu flags of the loaded
# profile. A flag already present in RAW_ARGS (the command line as typed) is not added.
emu_profile_esp_args() {
    EMU_PROFILE_ARGS=()
    if [[ -n "$EMU_PROF_NET" ]] && ! emu_argv_has --net "$@"; then EMU_PROFILE_ARGS+=(--net "$EMU_PROF_NET"); fi
    if [[ -n "$EMU_PROF_WIFI_SSID" ]]; then
        if ! emu_argv_has --wifi-ssid "$@"; then EMU_PROFILE_ARGS+=(--wifi-ssid "$EMU_PROF_WIFI_SSID"); fi
        if ! emu_argv_has --wifi-auth "$@"; then EMU_PROFILE_ARGS+=(--wifi-auth "$EMU_PROF_WIFI_AUTH"); fi
        if ! emu_argv_has --wifi-password "$@"; then
            if [[ "$EMU_PROF_WIFI_AUTH" == open ]]; then EMU_PROFILE_ARGS+=(--wifi-password "")
            else EMU_PROFILE_ARGS+=(--wifi-password "$SIM_WIFI_PASSWORD"); fi
        fi
    fi
    if [[ -n "$EMU_PROF_BLE_HCI" ]] && ! emu_argv_has --ble-hci "$@"; then EMU_PROFILE_ARGS+=(--ble-hci "$EMU_PROF_BLE_HCI"); fi
    if [[ -n "$EMU_PROF_AUDIO_OUT" ]] && ! emu_argv_has --i2s-wav "$@" && ! emu_argv_has --no-audio "$@"; then
        mkdir -p "$(dirname "$EMU_PROF_AUDIO_OUT")"
        EMU_PROFILE_ARGS+=(--i2s-wav "$EMU_PROF_AUDIO_OUT")
    fi
    if [[ -n "$EMU_PROF_AUDIO_IN" ]] && ! emu_argv_has --i2s-wav-in "$@"; then EMU_PROFILE_ARGS+=(--i2s-wav-in "$EMU_PROF_AUDIO_IN"); fi
}

emu_cmd_run() {
    emu_require_esp
    local inst
    inst="$(emu_resolve_instance 0 "$@")"; sim_ports "$inst"
    local extra=() board="" firmware="$EMU_MERGED" elf="$EMU_ELF" no_elf=0 no_sd=0 time_sync=1 profile="" card="$SIM_CARD_IMG" raw=("$@")
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --instance) shift 2 ;;
            --uart-tcp)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=(--uart-tcp "$2"); shift 2; else extra+=(--uart-tcp "127.0.0.1:$SIM_PORT_UART"); shift; fi ;;
            --board)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then board="$2"; shift 2; else board="$EMU_BOARD_DEFAULT"; shift; fi ;;
            --firmware|--merged) firmware="${2:-}"; shift 2 ;;
            --elf) elf="${2:-}"; shift 2 ;;
            --no-elf) no_elf=1; shift ;;
            --profile) profile="${2:?--profile NAME|FILE}"; shift 2 ;;
            --no-sd) no_sd=1; shift ;;
            --no-time-sync) time_sync=0; shift ;;
            --) shift; extra+=("$@"); break ;;
            --cosim-log) extra+=(--cosim-log "${2:-}"); shift 2 ;;
            --gdb)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=(--gdb "$2"); shift 2; else extra+=(--gdb "$SIM_PORT_GDB"); shift; fi ;;
            --gdb-halt) extra+=(--gdb-halt); shift ;;
            --trace) extra+=(--trace "${2:-}"); shift 2 ;;
            --control-tcp)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=(--control-tcp "$2"); shift 2; else extra+=(--control-tcp "127.0.0.1:$SIM_PORT_CONTROL"); shift; fi ;;
            --i2s-wav|--i2s-wav-in|--console|--net)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=("$1" "$2"); shift 2; else extra+=("$1"); shift; fi ;;
            *) extra+=("$1"); shift ;;
        esac
    done
    if [[ -n "$profile" ]]; then
        emu_profile_load "$profile"
        if [[ $EMU_PROF_NO_SD -eq 1 ]]; then no_sd=1; fi
        if [[ -n "$EMU_PROF_SD_IMG" ]]; then card="$EMU_PROF_SD_IMG"; fi
        emu_profile_esp_args "${raw[@]}"
        extra+=(${EMU_PROFILE_ARGS[@]+"${EMU_PROFILE_ARGS[@]}"})
    fi
    if [[ "$firmware" == "$EMU_MERGED" && ! -f "$firmware" ]]; then emu_cmd_merge; fi
    emu_require_file "$firmware" "firmware image" "pass --firmware/--merged, build es3c28p_emu, or run ./start.sh emu merge"
    if [[ -n "$board" ]]; then emu_require_file "$board" "cosim board file"; extra+=(--cosim-board "$board"); fi
    local emu_args=(--chip esp32s3 --psram-size 8M --firmware "$firmware")
    if [[ $no_sd -eq 0 && -f "$card" ]]; then emu_args+=(--sd "$card"); fi
    if [[ $no_elf -eq 0 && -n "$elf" && -f "$elf" ]]; then emu_args+=(--elf "$elf"); elif [[ $no_elf -eq 0 && -n "$elf" ]]; then echo "emu run: ELF not found, continuing without --elf: $elf" >&2; fi
    if [[ $time_sync -eq 1 ]]; then emu_sync_time "$(emu_uart_port "${extra[@]}")"; fi
    exec "$EMU_ESP" "${emu_args[@]}" "${extra[@]}"
}

emu_cmd_bootlog() {
    emu_require_esp
    local inst
    inst="$(emu_resolve_instance 0 "$@")"; sim_ports "$inst"
    local secs="8" extra=() board="" firmware="$EMU_MERGED" elf="$EMU_ELF" no_elf=0
    if [[ $# -gt 0 && "$1" =~ ^[0-9]+$ ]]; then secs="$1"; shift; fi
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --instance) shift 2 ;;
            --uart-tcp)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=(--uart-tcp "$2"); shift 2; else extra+=(--uart-tcp "127.0.0.1:$SIM_PORT_UART"); shift; fi ;;
            --board)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then board="$2"; shift 2; else board="$EMU_BOARD_DEFAULT"; shift; fi ;;
            --firmware|--merged) firmware="${2:-}"; shift 2 ;;
            --elf) elf="${2:-}"; shift 2 ;;
            --no-elf) no_elf=1; shift ;;
            --cosim-log) extra+=(--cosim-log "${2:-}"); shift 2 ;;
            --gdb)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=(--gdb "$2"); shift 2; else extra+=(--gdb "$SIM_PORT_GDB"); shift; fi ;;
            --gdb-halt) extra+=(--gdb-halt); shift ;;
            --trace) extra+=(--trace "${2:-}"); shift 2 ;;
            --control-tcp)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=(--control-tcp "$2"); shift 2; else extra+=(--control-tcp "127.0.0.1:$SIM_PORT_CONTROL"); shift; fi ;;
            --i2s-wav|--i2s-wav-in|--console|--net)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=("$1" "$2"); shift 2; else extra+=("$1"); shift; fi ;;
            *) extra+=("$1"); shift ;;
        esac
    done
    if [[ "$firmware" == "$EMU_MERGED" && ! -f "$firmware" ]]; then emu_cmd_merge; fi
    emu_require_file "$firmware" "firmware image" "pass --firmware/--merged, build es3c28p_emu, or run ./start.sh emu merge"
    if [[ -n "$board" ]]; then emu_require_file "$board" "cosim board file"; extra+=(--cosim-board "$board"); fi
    mkdir -p "$EMU_LOG_DIR"
    pkill -f "$EMU_ESP" 2>/dev/null || true
    sleep 0.2
    local emu_args=(--chip esp32s3 --psram-size 8M --firmware "$firmware" --timeout "${secs}s")
    if [[ $no_elf -eq 0 && -n "$elf" && -f "$elf" ]]; then emu_args+=(--elf "$elf"); elif [[ $no_elf -eq 0 && -n "$elf" ]]; then echo "emu bootlog: ELF not found, continuing without --elf: $elf" >&2; fi
    timeout -k 2 "${secs}s" "$EMU_ESP" "${emu_args[@]}" "${extra[@]}" > "$EMU_LOG_DIR/boot.log" 2>&1 || true
    echo "Saved: $EMU_LOG_DIR/boot.log"
    tail -n 40 "$EMU_LOG_DIR/boot.log" || true
}

# emu shot: headless run for tests. Boots the firmware with the SD image, sends console commands
# (--cmd, repeatable) over the UART, saves the console replies and a capture of the emulated LCD.
#   ./start.sh emu shot [--secs N] [--out FILE.png] [--cmd "ls /sd"]... [--sd IMG] [--profile NAME|FILE] [--port P] [--uart-port P]
#   network: [--net user] [--wifi-ssid S] [--wifi-password P ("" = open)] [--wifi-auth open|wpa2-psk|wpa3-sae|wpa2-wpa3]
# Each run needs its own ports when several run at once (agents in parallel).
emu_cmd_shot() {
    # Default instance 9 (runs next to a ui on instance 0); with --port and no --instance it is 0.
    local inst shot_default=9
    emu_argv_has --port "$@" && shot_default=0
    inst="$(emu_resolve_instance "$shot_default" "$@")"; sim_ports "$inst"
    local secs=25 out="$EMU_LOG_DIR/shot.png" port="$SIM_PORT_COSIM" uart="$SIM_PORT_UART" sd="$SIM_CARD_IMG" cmds=() time_sync=1 net_args=() profile="" raw=("$@")
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --instance) shift 2 ;;
            --secs) secs="${2:?--secs N}"; shift 2 ;;
            --out) out="${2:?--out FILE}"; shift 2 ;;
            --cmd) cmds+=("${2:?--cmd TEXT}"); shift 2 ;;
            --sd) sd="${2:?--sd IMG}"; shift 2 ;;
            --profile) profile="${2:?--profile NAME|FILE}"; shift 2 ;;
            --port) port="${2:?--port P}"; shift 2 ;;
            --uart-port) uart="${2:?--uart-port P}"; shift 2 ;;
            --no-time-sync) time_sync=0; shift ;;
            --net|--wifi-ssid|--wifi-password|--wifi-auth) net_args+=("$1" "${2-}"); shift 2 ;;
            *) die "emu shot: unknown option '$1'" ;;
        esac
    done
    if [[ -n "$profile" ]]; then
        emu_profile_load "$profile"
        if [[ $EMU_PROF_NO_SD -eq 1 ]] && ! emu_argv_has --sd "${raw[@]}"; then sd=""; fi
        if [[ -n "$EMU_PROF_SD_IMG" ]] && ! emu_argv_has --sd "${raw[@]}"; then sd="$EMU_PROF_SD_IMG"; fi
        emu_profile_esp_args "${raw[@]}"
        net_args+=(${EMU_PROFILE_ARGS[@]+"${EMU_PROFILE_ARGS[@]}"})
    fi
    [ -x "$EMU_ESP" ] || die "esp-emu not found at $EMU_ESP"
    [ -x "$EMU_HOST" ] || emu_cmd_build_host
    [ -f "$EMU_MERGED" ] || emu_cmd_merge
    if [[ -n "$sd" ]]; then [ -f "$sd" ] || die "emu shot: SD image not found: $sd (./start.sh card image)"; fi
    command -v convert >/dev/null 2>&1 || die "emu shot: ImageMagick 'convert' not found"
    mkdir -p "$EMU_LOG_DIR" "$(dirname "$out")"
    local log="$EMU_LOG_DIR/shot-$port.log" replies="${out%.*}.console.txt" emu_pid
    local emu_args=(--chip esp32s3 --psram-size 8M --firmware "$EMU_MERGED"
                    --cosim-tcp "127.0.0.1:$port" --uart-tcp "127.0.0.1:$uart" --timeout "$((secs + 20))s")
    if [[ -n "$sd" ]]; then emu_args+=(--sd "$sd"); fi
    emu_args+=("${net_args[@]}")
    [ -f "$EMU_ELF" ] && emu_args+=(--elf "$EMU_ELF")
    if [[ $time_sync -eq 1 ]]; then emu_sync_time "$uart"; fi
    "$EMU_ESP" "${emu_args[@]}" >"$log" 2>&1 &
    emu_pid=$!
    sleep 2
    # console commands go in once the firmware is up (UART0 mirror), replies are saved
    ( sleep $((secs - 8)); exec 3<>"/dev/tcp/127.0.0.1/$uart" || exit 0
      for c in "${cmds[@]}"; do printf '%s\r\n' "$c" >&3; sleep 3; done
      timeout 3 cat <&3 >"$replies" ) 2>/dev/null &
    local conn=$!
    SDL_VIDEODRIVER=dummy timeout "$((secs + 15))s" "$EMU_HOST" --port "$port" --dump-fb "$out.ppm" --dump-after "$secs" >"$EMU_LOG_DIR/shot-host-$port.log" 2>&1 || true
    wait "$conn" 2>/dev/null || true
    kill "$emu_pid" 2>/dev/null || true; wait "$emu_pid" 2>/dev/null || true
    if [ -f "$out.ppm" ]; then convert "$out.ppm" "$out" && rm -f "$out.ppm"; fi
    echo "shot: $out"; [ ${#cmds[@]} -gt 0 ] && echo "console replies: $replies"; echo "emulator log: $log"
}

emu_cmd_console() {
    local inst
    inst="$(emu_resolve_instance 0 "$@")"; sim_ports "$inst"
    local target="127.0.0.1:$SIM_PORT_UART"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --instance) shift 2 ;;
            --port) target="127.0.0.1:${2:-$SIM_PORT_UART}"; shift 2 ;;
            --target) target="${2:-127.0.0.1:$SIM_PORT_UART}"; shift 2 ;;
            -h|--help) echo "Usage: ./start.sh emu console [--instance N] [--port P | --target HOST:PORT | HOST:PORT]"; return 0 ;;
            *) target="$1"; shift ;;
        esac
    done
    local host="${target%:*}" port="${target##*:}"
    echo "emu console: connecting to ${host}:${port} (start emulator with --uart-tcp ${host}:${port})" >&2
    if command -v socat >/dev/null 2>&1; then exec socat -,rawer "TCP:${host}:${port}";
    elif command -v nc >/dev/null 2>&1; then exec nc "$host" "$port";
    else die "emu console: no TCP client found. Install socat (preferred) or nc/netcat."; fi
}

emu_cmd_ui() {
    [ -x "$EMU_ESP" ] || die "esp-emu 1.1.0 not found at $EMU_ESP"
    local inst
    inst="$(emu_resolve_instance 0 "$@")"; sim_ports "$inst"
    local port="$SIM_PORT_COSIM" audio=1 no_sd=0 cosim_unix="" board="" uart_tcp="" hw_rot="" bridge_pid="" no_host=0 time_sync=1
    local firmware="$EMU_MERGED" elf="$EMU_ELF" no_elf=0 profile="" card="$SIM_CARD_IMG" raw=("$@")
    local extra=() host_extra=()
    local rot_state="$EMU_DIR/.hw_rotation" board_state="$EMU_DIR/.hw_board"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --instance) shift 2 ;;
            --no-audio) audio=0; shift ;;
            --no-sd) no_sd=1; shift ;;
            --profile) profile="${2:?--profile NAME|FILE}"; shift 2 ;;
            --no-host) no_host=1; shift ;;
            --no-time-sync) time_sync=0; shift ;;
            --port) port="${2:-$SIM_PORT_COSIM}"; shift 2 ;;
            --hw-rot) hw_rot="${2:-0}"; shift 2 ;;
            --board)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then board="$2"; shift 2; else board="$EMU_BOARD_DEFAULT"; shift; fi ;;
            --firmware|--merged) firmware="${2:-}"; shift 2 ;;
            --elf) elf="${2:-}"; shift 2 ;;
            --no-elf) no_elf=1; shift ;;
            --cosim-log) extra+=(--cosim-log "${2:-}"); shift 2 ;;
            --gdb)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=(--gdb "$2"); shift 2; else extra+=(--gdb "$SIM_PORT_GDB"); shift; fi ;;
            --gdb-halt) extra+=(--gdb-halt); shift ;;
            --trace) extra+=(--trace "${2:-}"); shift 2 ;;
            --control-tcp)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then extra+=(--control-tcp "$2"); shift 2; else extra+=(--control-tcp "127.0.0.1:$SIM_PORT_CONTROL"); shift; fi ;;
            --debug|--debug-touch|-v) host_extra+=("$1"); shift ;;
            --cosim-unix) cosim_unix="${2:-}"; [[ -n "$cosim_unix" ]] || die "--cosim-unix requires a path"; shift 2 ;;
            --uart-tcp)
                if [[ -n "${2:-}" && "${2:-}" != --* ]]; then uart_tcp="$2"; extra+=(--uart-tcp "$2"); shift 2; else uart_tcp="127.0.0.1:$SIM_PORT_UART"; extra+=(--uart-tcp "127.0.0.1:$SIM_PORT_UART"); shift; fi ;;
            --) shift; extra+=("$@"); break ;;
            *) extra+=("$1"); shift ;;
        esac
    done
    if [[ -n "$profile" ]]; then
        emu_profile_load "$profile"
        if [[ $EMU_PROF_NO_SD -eq 1 ]]; then no_sd=1; fi
        if [[ -n "$EMU_PROF_SD_IMG" ]]; then card="$EMU_PROF_SD_IMG"; fi
        emu_profile_esp_args "${raw[@]}"
        extra+=(${EMU_PROFILE_ARGS[@]+"${EMU_PROFILE_ARGS[@]}"})
    fi
    if [[ $no_host -eq 0 ]]; then [ -x "$EMU_HOST" ] || emu_cmd_build_host; fi
    if [[ "$firmware" == "$EMU_MERGED" && ! -f "$firmware" ]]; then emu_cmd_merge; fi
    [ -f "$firmware" ] || die "firmware image not found: $firmware"
    if [[ -z "$uart_tcp" ]]; then
        for ((i=0; i<${#extra[@]}; i++)); do
            if [[ "${extra[$i]}" == "--uart-tcp" && $((i+1)) -lt ${#extra[@]} ]]; then uart_tcp="${extra[$((i+1))]}"; break; fi
        done
    fi
    if [[ -n "$uart_tcp" ]]; then host_extra+=(--uart "$uart_tcp"); fi
    if [[ $time_sync -eq 1 ]]; then emu_sync_time "$(emu_uart_port "${extra[@]}")"; fi
    if [[ -z "$hw_rot" ]]; then hw_rot="0"; fi
    echo "$hw_rot" > "$rot_state" 2>/dev/null || true
    if [[ ! -f "$board_state" ]]; then echo "es3c28p" > "$board_state" 2>/dev/null || true; fi

    local emu_args=(--chip esp32s3 --psram-size 8M --firmware "$firmware" --cosim-tcp "127.0.0.1:${port}")
    if [[ $no_sd -eq 0 && -f "$card" ]]; then emu_args+=(--sd "$card"); fi
    if [[ $no_elf -eq 0 && -n "$elf" && -f "$elf" ]]; then emu_args+=(--elf "$elf"); fi
    if [[ -n "$board" ]]; then [ -f "$board" ] || die "cosim board not found: $board"; emu_args+=(--cosim-board "$board"); echo "Cosim board (in-process): $board"; fi
    mkdir -p "$EMU_LOG_DIR"
    # The default capture yields to an --i2s-wav given on the command line or by the profile (audio.out).
    if [[ $audio -eq 1 ]] && ! emu_argv_has --i2s-wav ${extra[@]+"${extra[@]}"}; then emu_args+=(--i2s-wav "$EMU_LOG_DIR/i2s_out.wav"); echo "Audio (I2S out): $EMU_LOG_DIR/i2s_out.wav"; fi

    if [[ $no_host -eq 1 ]]; then
        pkill -f "$EMU_ESP.*--cosim-tcp.*127.0.0.1:${port}" 2>/dev/null || true
        if command -v fuser >/dev/null 2>&1; then fuser -k "${port}/tcp" >/dev/null 2>&1 || true; fi
        if [[ -n "$cosim_unix" ]]; then rm -f "$cosim_unix"; pkill -f "socat.*UNIX-LISTEN:${cosim_unix}.*TCP:127.0.0.1:${port}" 2>/dev/null || true; fi
        sleep 0.2
        echo "Starting esp-emu 1.1.0 backend-only (--no-host, cosim-tcp 127.0.0.1:${port}) ..."
        "$EMU_ESP" "${emu_args[@]}" "${extra[@]}" &
        local emu_pid=$!
        if [[ -n "$cosim_unix" ]]; then
            command -v socat >/dev/null 2>&1 || die "missing command: socat"
            socat "UNIX-LISTEN:${cosim_unix},fork,reuseaddr,unlink-early" "TCP:127.0.0.1:${port}" >"$EMU_LOG_DIR/cosim-bridge.log" 2>&1 &
            bridge_pid=$!
            echo "Cosim Unix socket bridge: $cosim_unix -> tcp:127.0.0.1:${port} (conditional; esp-emu 1.1.0 usa TCP interno)"
        fi
        cleanup_nohost() {
            if [[ -n "${bridge_pid:-}" ]]; then kill "$bridge_pid" 2>/dev/null || true; fi
            if [[ -n "${emu_pid:-}" ]]; then kill "$emu_pid" 2>/dev/null || true; wait "$emu_pid" 2>/dev/null || true; fi
            if [[ -n "$cosim_unix" ]]; then rm -f "$cosim_unix"; fi
        }
        trap cleanup_nohost EXIT INT TERM
        echo "Backend running. Connect host/client to cosim tcp://127.0.0.1:${port}${cosim_unix:+ or unix:$cosim_unix}. Ctrl-C to stop."
        wait "$emu_pid" || true
        trap - EXIT INT TERM
        cleanup_nohost
        return 0
    fi

    while true; do
        pkill -f "$EMU_HOST" 2>/dev/null || true
        pkill -f "$EMU_ESP.*--cosim-tcp.*127.0.0.1:${port}" 2>/dev/null || true
        if command -v fuser >/dev/null 2>&1; then fuser -k "${port}/tcp" >/dev/null 2>&1 || true; fi
        if [[ -n "$cosim_unix" ]]; then rm -f "$cosim_unix"; pkill -f "socat.*UNIX-LISTEN:${cosim_unix}.*TCP:127.0.0.1:${port}" 2>/dev/null || true; fi
        sleep 0.3

        echo "Starting esp-emu 1.1.0 (hw_rot=$hw_rot) ..."
        "$EMU_ESP" "${emu_args[@]}" "${extra[@]}" &
        local emu_pid=$! bridge_pid=""
        cleanup_iter() {
            if [[ -n "${bridge_pid:-}" ]]; then kill "$bridge_pid" 2>/dev/null || true; fi
            if [[ -n "${emu_pid:-}" ]]; then kill "$emu_pid" 2>/dev/null || true; wait "$emu_pid" 2>/dev/null || true; fi
            pkill -f "$EMU_HOST" 2>/dev/null || true
            if [[ -n "$cosim_unix" ]]; then rm -f "$cosim_unix"; fi
        }
        trap cleanup_iter EXIT INT TERM

        local host_connect_args=(--port "$port")
        if [[ -n "$cosim_unix" ]]; then
            command -v socat >/dev/null 2>&1 || die "missing command: socat"
            socat "UNIX-LISTEN:${cosim_unix},fork,reuseaddr,unlink-early" "TCP:127.0.0.1:${port}" >"$EMU_LOG_DIR/cosim-bridge.log" 2>&1 &
            bridge_pid=$!; sleep 0.2
            host_connect_args=(--unix "$cosim_unix")
            echo "Cosim Unix socket bridge: $cosim_unix -> tcp:127.0.0.1:${port} (conditional; esp-emu 1.1.0 usa TCP interno)"
        fi

        echo "Starting LCD+touch host UI..."
        local host_rc=0
        "$EMU_HOST" "${host_connect_args[@]}" "${host_extra[@]}" --hw-rot "$hw_rot" --rot-state "$rot_state" --board-state "$board_state" || host_rc=$?
        trap - EXIT INT TERM
        cleanup_iter

        if [[ "$host_rc" -eq 42 && -f "$rot_state" ]]; then
            hw_rot="$(tr -dc '0-3' < "$rot_state" | head -c1 || echo "$hw_rot")"
            echo "Simulator requested reboot (hw_rot=$hw_rot, board=$(cat "$board_state" 2>/dev/null || echo '?'))..."
            continue
        fi
        break
    done
}


emu_cmd_build_launcher() {
    [ -x "$SIM/launcher/build.sh" ] || die "launcher build script not found: $SIM/launcher/build.sh"
    "$SIM/launcher/build.sh"
}

emu_cmd_build_mockup() {
    [ -x "$SIM/mockup_studio/build.sh" ] || die "mockup build script not found: $SIM/mockup_studio/build.sh"
    "$SIM/mockup_studio/build.sh"
}

emu_cmd_build_all() {
    local what="${1:-all}"
    case "$what" in
        all|"")
            emu_cmd_build_host
            emu_cmd_build_launcher
            emu_cmd_build_mockup
            ;;
        host) emu_cmd_build_host ;;
        launcher) emu_cmd_build_launcher ;;
        mockup) emu_cmd_build_mockup ;;
        *) die "emu build: unknown target '$what' (all, host, launcher, mockup)" ;;
    esac
}

emu_cmd_launcher() {
    [ -x "$EMU_LAUNCHER" ] || emu_cmd_build_launcher
    exec "$EMU_LAUNCHER" "$@"
}

emu_cmd_mockup() {
    local sub="${1:-run}"
    [ $# -gt 0 ] && shift || true
    case "$sub" in
        build) emu_cmd_build_mockup ;;
        run|"")
            [ -x "$EMU_MOCKUP_STUDIO" ] || emu_cmd_build_mockup
            exec "$EMU_MOCKUP_STUDIO" "$@"
            ;;
        *) die "emu mockup: unknown subcommand '$sub' (run, build)" ;;
    esac
}

emu_cmd_test_extensive() {
    [ -x "$EMU_TEST_SCRIPT" ] || die "extensive simulator test script not found: $EMU_TEST_SCRIPT"
    exec "$EMU_TEST_SCRIPT" "$@"
}

emu_cmd_info() {
    echo "EMU_DIR=$EMU_DIR"
    echo "EMU_ESP=$EMU_ESP"
    echo "EMU_HOST=$EMU_HOST"
    echo "EMU_MERGED=$EMU_MERGED (exists: $(test -f "$EMU_MERGED" && echo yes || echo no))"
    echo "EMU_LOG_DIR=$EMU_LOG_DIR"
    echo "ELF_EMU=$EMU_FW_BUILD_EMU/firmware.elf (exists: $(test -f "$EMU_FW_BUILD_EMU/firmware.elf" && echo yes || echo no))"
    echo "ELF_HW=$EMU_FW_BUILD_HW/firmware.elf (exists: $(test -f "$EMU_FW_BUILD_HW/firmware.elf" && echo yes || echo no))"
    echo "BOARD_DEFAULT=$EMU_BOARD_DEFAULT (exists: $(test -f "$EMU_BOARD_DEFAULT" && echo yes || echo no))"
    "$EMU_ESP" --version 2>&1 | head -2 || echo "esp-emu 1.1.0: missing"
    if command -v esptool >/dev/null 2>&1; then esptool version 2>&1 | head -2 || true; elif command -v esptool.py >/dev/null 2>&1; then esptool.py version 2>&1 | head -2 || true; elif [[ -x "$HOME/.platformio/penv/bin/esptool" ]]; then "$HOME/.platformio/penv/bin/esptool" version 2>&1 | head -2 || true; fi
}

emu_cmd() {
    local cmd="${1:-help}"
    [ $# -gt 0 ] && shift
    case "$cmd" in
        merge) emu_cmd_merge "$@" ;;
        build-host) emu_cmd_build_host "$@" ;;
        build-launcher) emu_cmd_build_launcher "$@" ;;
        build-mockup) emu_cmd_build_mockup "$@" ;;
        build) emu_cmd_build_all "$@" ;;
        run) emu_cmd_run "$@" ;;
        bootlog) emu_cmd_bootlog "$@" ;;
        ui) emu_cmd_ui "$@" ;;
        launcher) emu_cmd_launcher "$@" ;;
        mockup) emu_cmd_mockup "$@" ;;
        test) emu_cmd_test_extensive "$@" ;;
        cosim) emu_cmd_ui --no-host "$@" ;;
        console) emu_cmd_console "$@" ;;
        shot) emu_cmd_shot "$@" ;;
        info) emu_cmd_info "$@" ;;
        help|-h|--help) emu_usage ;;
        *) die "emu: unknown command '$cmd'. Use: info, merge, build-host, build-launcher, build-mockup, build, run, bootlog, ui, launcher, mockup, cosim, console, test, help" ;;
    esac
}

# ----- launcher (GTK unified simulator UI) -----------------------------------

launcher_cmd() {
    local cmd="${1:-run}"
    [ $# -gt 0 ] && shift || true
    case "$cmd" in
        run|"") emu_cmd_launcher "$@" ;;
        build) emu_cmd_build_launcher "$@" ;;
        test|extensive) emu_cmd_test_extensive "$@" ;;
        mockup-build) emu_cmd_build_mockup "$@" ;;
        mockup-run) emu_cmd_mockup run "$@" ;;
        mockup) emu_cmd_mockup "$@" ;;
        help|-h|--help)
            cat <<'EOF'
usage: ./start.sh launcher [command]
  run [-- args]       build if needed and run unified GTK simulator launcher (default)
  build               build unified GTK simulator launcher
  test|extensive      run extensive simulator/launcher test battery
  mockup [run|build]  build/run mockup studio UI
  mockup-build        build mockup studio UI
  mockup-run          run mockup studio UI
EOF
            ;;
        *) die "unknown launcher command '$cmd' (run, build, test, mockup, mockup-build, mockup-run)" ;;
    esac
}

# ----- dispatch ------------------------------------------------------------

case "${1:-help}" in
    build|flash|monitor|publish|put|cmd|bridge|monitor-vnc|stamp)
        firmware_cmd "$@" ;;
    firmware)
        shift
        firmware_cmd "$@" ;;
    server)
        shift
        server_cmd "$@" ;;
    emu|emulator)
        shift
        emu_cmd "$@" ;;
    launcher)
        shift
        launcher_cmd "$@" ;;
    card)
        shift
        card_cmd "$@" ;;
    test)
        shift
        test_cmd "$@" ;;
    scan)
        shift
        scan "$@" ;;
    help|-h|--help)
        usage ;;
    *)
        die "unknown command '$1' (build, flash, monitor, publish, put, cmd, bridge, monitor-vnc, server, emu, launcher, card, test, scan, help)" ;;
esac
