#!/bin/sh
# A virtual monitor the size of the Deck, shared over VNC (KDE, krfb-virtualmonitor).
# Drag onto it what the Deck should show. Close the normal krfb first: both use port 5900.
#   deck-monitor.sh [WxH] [password] [port]
#   portrait 2.8" (KryonOS today): 240x304   landscape: 320x224   (screen minus the 16 px bar)
# The password is not in this file. Order: 2nd argument, env DECK_VNC_PASSWORD, then the first
# line of ${XDG_CONFIG_HOME:-$HOME/.config}/control-deck/vnc-password (same password as vnc.json).
RES=${1:-240x304}
PASS=${2:-${DECK_VNC_PASSWORD:-}}
PORT=${3:-5900}
PFILE="${XDG_CONFIG_HOME:-$HOME/.config}/control-deck/vnc-password"
if [ -z "$PASS" ] && [ -r "$PFILE" ]; then
    PASS=$(head -n 1 "$PFILE")
fi
[ -n "$PASS" ] || { echo "No VNC password: give it as 2nd argument, set DECK_VNC_PASSWORD, or write $PFILE" >&2; exit 1; }
echo "Virtual monitor 'Deck' $RES on port $PORT (Ctrl+C removes it)"
exec krfb-virtualmonitor --resolution "$RES" --name Deck --password "$PASS" --port "$PORT"
