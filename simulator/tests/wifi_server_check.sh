#!/bin/bash
# Wi-Fi + real server check: the Deck firmware in esp-emu joins an open test AP (--net user),
# is pointed at the Java host on the PC (gateway 192.168.4.1 = host loopback) and posts to /api/deck.
# The run keeps the emulator up for about 4 minutes after the join: the Deck app is started,
# the LCD is captured (through the cosim dump, headless), a console line and a message are
# queued on the server midway, the console is read back, and a second capture is taken.
# The server runs with a throwaway database (simulator/logs/wifi-check-var), never server/var.
# The throwaway token is passed in memory only and redacted from every log.
# Usage: SIM_INSTANCE=3 simulator/tests/wifi_server_check.sh   (default instance 3; UART 5585 = 5555+10N, cosim 5621 = 5591+10N)
set -u
umask 077   # files with the throwaway token are private to the user
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/simulator"
LOGS="$SIM/logs"
INSTANCE="${SIM_INSTANCE:-3}"
# Ports of an emulator instance: one rule, same as sim_ports in start.sh.
ports() { UART=$((5555 + 10 * INSTANCE)); COSIM=$((5591 + 10 * INSTANCE)); }
case $INSTANCE in ''|*[!0-9]*) echo "instance must be a number: $INSTANCE" >&2; exit 2 ;; esac
ports
HTTP=8089; SPORT=$((HTTP + 1))   # test-local, not per instance: the guest talks to HTTP (a socat proxy that logs the traffic), the server listens on SPORT
DECK_ID=240AC4000001             # the id the kernel sends in its hello frame (decoded from the proxy log)
DECK_APP=/sd/system/SystemApps/deck/main.js   # the Deck stats app on the card (system/SystemApps/deck; /local has no SystemApps)
RUN_END=300                      # seconds from the emulator start: about 4 minutes after the Wi-Fi join (~45 s)
JAR="$ROOT/server/target/control-deck-host-0.7.0-all.jar"
EMU="$SIM/esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu"
HOSTBIN="$SIM/emu/lcd_touch_host"
FW="$SIM/build/merged-emu.bin"
# stop whatever this run started and redact the throwaway token, on normal exit, error, Ctrl+C or kill
cleanup() {
    trap - EXIT
    for p in "${HPID:-}" "${CPID:-}" "${EPID:-}" "${SPID:-}" "${XPID:-}"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done
    sleep 1   # let the killed processes flush their logs before the redaction
    [ -n "${TOKEN:-}" ] && mdel -i "$LOGS/wifi-sd.img" ::/UserData/deck/deck.json 2>/dev/null   # the throwaway token leaves the copy
    if [ -n "${TOKEN:-}" ]; then
        sed -i "s/$TOKEN/<token>/g" "$LOGS"/wifi-console.txt "$LOGS"/wifi-emu.log "$LOGS"/wifi-server.log "$LOGS"/wifi-proxy.log "$LOGS"/wifi-api-*.json 2>/dev/null
    fi
    # throwaway artifacts, removed after the redaction: token file, card copy, server database
    rm -f "$LOGS/wifi-deck.json" "$LOGS/wifi-sd.img"
    [ -n "${VAR:-}" ] && rm -rf "$VAR"
}
trap cleanup EXIT
trap 'exit 130' INT TERM
for f in "$JAR" "$EMU" "$FW" "$HOSTBIN"; do [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }; done
command -v convert >/dev/null || { echo "missing: ImageMagick convert" >&2; exit 1; }
mkdir -p "$LOGS"
VAR="$LOGS/wifi-check-var"; rm -rf "$VAR"; mkdir -p "$VAR"
cp "$SIM/emu/storage/sd.img" "$LOGS/wifi-sd.img"          # throwaway copy of the card

start_server() {
    CONTROL_DECK_VAR="$VAR" CONTROL_DECK_WIFI=1 CONTROL_DECK_COUNT_LOOPBACK_PEERS=1 java -jar "$JAR" --bind 0.0.0.0 --port "$SPORT" >>"$LOGS/wifi-server.log" 2>&1 & SPID=$!
    for i in $(seq 1 30); do curl -s -o /dev/null "http://127.0.0.1:$SPORT/" && break; sleep 1; done
}
: >"$LOGS/wifi-server.log"
start_server
# the Wi-Fi transport keeps its own bind/port setting (saved in the database, applied after a restart)
curl -s -X PUT -H "Content-Type: application/json" -d "{\"enabled\":true,\"bind\":\"0.0.0.0\",\"port\":$SPORT}" "http://127.0.0.1:$SPORT/api/wifi" >/dev/null
kill "$SPID"; sleep 2
start_server
TOKEN=$(curl -s -X POST "http://127.0.0.1:$SPORT/api/wifi/token" | sed -n 's/.*"token" *: *"\([^"]*\)".*/\1/p')
[ -n "$TOKEN" ] || { echo "no token from the server (see $LOGS/wifi-server.log)" >&2; kill "$SPID"; exit 1; }

# Deploy the repo Deck app over the stale copy in the throwaway card (overwrite: mcopy -o, same tool as start.sh card).
# Its data dir is paths.data of app.json = /sd/UserData/deck: deck.json there holds the test URL and the throwaway token.
# A placeholder token would not work: main.js needs a token and calls DeckLink.wifiSetup with it (it would replace the NVS one).
# The deck.json file is removed from the throwaway copy in cleanup, so the token is not left in the card.
DECK_SRC="$ROOT/firmware/apps/deck"
( cd "$DECK_SRC" && mcopy -o -m -Q -i "$LOGS/wifi-sd.img" ./* ::/system/SystemApps/deck/ ) || { echo "deck deploy failed" >&2; exit 1; }
for f in main.js decknet.js app.json; do
    mtype -i "$LOGS/wifi-sd.img" "::/system/SystemApps/deck/$f" | cmp -s - "$DECK_SRC/$f" || { echo "deck deploy: $f differs on the card" >&2; exit 1; }
done
printf '{"server":"http://192.168.4.1:%s","token":"%s","id":"%s"}\n' "$HTTP" "$TOKEN" "$DECK_ID" >"$LOGS/wifi-deck.json"
chmod 600 "$LOGS/wifi-deck.json"
mcopy -o -Q -i "$LOGS/wifi-sd.img" "$LOGS/wifi-deck.json" ::/UserData/deck/deck.json || { echo "deck.json copy failed" >&2; exit 1; }
rm -f "$LOGS/wifi-deck.json"

socat -v "TCP-LISTEN:$HTTP,reuseaddr,fork" "TCP:127.0.0.1:$SPORT" 2>"$LOGS/wifi-proxy.log" & XPID=$!
"$EMU" --chip esp32s3 --psram-size 8M --firmware "$FW" --sd "$LOGS/wifi-sd.img" \
    --uart-tcp "127.0.0.1:$UART" --cosim-tcp "127.0.0.1:$COSIM" --net user --wifi-ssid testnet --wifi-password "" --wifi-auth open \
    --timeout "$((RUN_END + 120))s" >"$LOGS/wifi-emu.log" 2>&1 & EPID=$!
START=$(date +%s)
# at SECS: wait until SECS seconds after the emulator start
at() { local d=$((START + $1 - $(date +%s))); [ "$d" -gt 0 ] && sleep "$d"; return 0; }
send() { printf '%s\r\n' "$1" >&3; }
# shot NAME: a wake tap (the display sleeps after 60 s idle; the tap only wakes it and is consumed),
# then one LCD capture from the cosim dump (headless: SDL_VIDEODRIVER=dummy).
shot() {
    local base="$LOGS/$1"
    send "tap 120 200"; sleep 2
    SDL_VIDEODRIVER=dummy timeout -k 5 60s "$HOSTBIN" --port "$COSIM" --dump-fb "$base.ppm" --dump-after 4 >"$base.host.log" 2>&1 & HPID=$!
    wait "$HPID"; HPID=""
    if [ -f "$base.ppm" ] && convert "$base.ppm" "$base.png"; then rm -f "$base.ppm"; echo "capture: $base.png"
    else echo "capture failed: $base.host.log" >&2; fi
}
at 28
exec 3<>"/dev/tcp/127.0.0.1/$UART"
cat <&3 >"$LOGS/wifi-console.txt" & CPID=$!
send "wifi testnet|"
at 40
send "js DeckLink.wifiSetup(\"http://192.168.4.1:$HTTP\",\"$TOKEN\")"
at 50
send "state"
send "ls /sd/system/SystemApps/deck" # confirms the Deck app path before the run
at 55
send "run $DECK_APP"
at 80
send "state"
shot wifi-shot-01-stats               # first LCD capture: stats from the server, or offline/connecting
at 150
send "state"
curl -s -m 10 "http://127.0.0.1:$SPORT/api/connection/status" >"$LOGS/wifi-api-connection-before.json"
at 165                                # midway: queue a console line and a message on the server (curl, loopback)
curl -s -m 20 -X POST -H "Content-Type: application/json" \
    -d "{\"line\":\"state\",\"deck\":\"$DECK_ID\",\"wait_ms\":8000}" "http://127.0.0.1:$SPORT/api/console" >"$LOGS/wifi-api-console.json"
curl -s -m 10 -X POST -H "Content-Type: application/json" \
    -d "{\"text\":\"wifi check message\",\"to\":\"$DECK_ID\"}" "http://127.0.0.1:$SPORT/api/messages" >"$LOGS/wifi-api-messages.json"
curl -s -m 10 "http://127.0.0.1:$SPORT/api/messages" >"$LOGS/wifi-api-inbox.json"
send "state"                          # read the Deck console afterwards
send "ls /local"
at 190
shot wifi-shot-02-after-queue         # second LCD capture after the queue
at 280
send "state"
curl -s -m 10 "http://127.0.0.1:$SPORT/api/connection/status" >"$LOGS/wifi-api-connection-after.json"
at "$RUN_END"
send "state"
sleep 3
cleanup   # kills the processes and redacts the throwaway token from the logs
echo "decks seen by the server: $LOGS/wifi-api-connection-after.json"; echo "console: $LOGS/wifi-console.txt"; echo "server log: $LOGS/wifi-server.log"; echo "proxy log (the proof): $LOGS/wifi-proxy.log"
echo "console answer from the server: $LOGS/wifi-api-console.json"; echo "message queued: $LOGS/wifi-api-messages.json"; echo "LCD captures: $LOGS/wifi-shot-01-stats.png $LOGS/wifi-shot-02-after-queue.png"
echo "POST /api/deck in the proxy log: $(grep -c 'POST /api/deck' "$LOGS/wifi-proxy.log"), answered 200: $(grep -c 'HTTP/1.1 200' "$LOGS/wifi-proxy.log")"
