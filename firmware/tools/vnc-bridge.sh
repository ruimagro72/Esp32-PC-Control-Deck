#!/bin/sh
# WebSocket bridge for the VNC app: the Deck connects to ws://<this PC>:5901 and the bridge
# forwards to the VNC server (krfb on 5900). Frames are cut to 8 KB: the ESP32 WebSocket library
# drops anything over 15 KB. Plain Java (the server's WsTcpBridge), no Python.
#   vnc-bridge.sh [listen_port] [vnc_host:port]
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JAR=$(ls "$ROOT"/server/target/control-deck-host-*-all.jar 2>/dev/null | head -1)
[ -n "$JAR" ] || { echo "Build the server first: ./start.sh server build" >&2; exit 1; }
exec java -cp "$JAR" dev.controldeck.host.display.WsTcpBridge "${1:-5901}" "${2:-127.0.0.1:5900}"
