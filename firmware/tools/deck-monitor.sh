#!/bin/sh
# A virtual monitor the size of the Deck, shared over VNC (KDE, krfb-virtualmonitor).
# Drag onto it what the Deck should show. Close the normal krfb first: both use port 5900.
#   deck-monitor.sh [WxH] [password] [port]
#   portrait 2.8" (KryonOS today): 240x304   landscape: 320x224   (screen minus the 16 px bar)
RES=${1:-240x304}
PASS=${2:-1234}
PORT=${3:-5900}
echo "Virtual monitor 'Deck' $RES on port $PORT (Ctrl+C removes it)"
exec krfb-virtualmonitor --resolution "$RES" --name Deck --password "$PASS" --port "$PORT"
