# ESP32 PC Control Deck: the Deck layer (deck branch)

The Deck is the application that runs on the board inside KryonOS: the Deck screen (PC stats and macros), the Deck link
(USB, Bluetooth LE or Wi-Fi), the Deck shell, the Bluetooth page and the VNC client. It is not a program of its own: it
never runs on its own. It needs two other branches:

- `kryonos`: the KryonOS 3.0.0 firmware with the board support and the Deck's kernel parts (`DeckLink`, `DeckOta`).
- `server`: the Java host on the PC, protocol 4 (`docs/04-PROTOCOL.md`).

Extract the three branches into one folder with `main` (see the `main` branch README).

## What is here

| Path | What it is |
|---|---|
| `firmware/apps/deck/` | Deck client: stats and macros from the server (`decknet.js` is loaded from the app folder) |
| `firmware/apps/decklink/` | Deck link page: Bluetooth LE service to the server, passkey, link state |
| `firmware/apps/deckshell/` | entry in the Home that switches the main screen to the Deck shell |
| `firmware/apps/shell/` | the Deck shell (`shell.js`) and `autoexec.js` |
| `firmware/apps/bluetooth/` | Bluetooth page: status, advertising, scan, BLE keyboard |
| `firmware/apps/vnc/` | VNC client (virtual monitor mode, `vnc.example.json`) |
| `firmware/apps/lib/decknet.js` | the protocol 4 in JavaScript, for other apps (`eval(FS.readTextFile("/sd/lib/decknet.js"))`) |
| `firmware/tools/Send.java`, `deck-monitor.sh`, `vnc-bridge.sh` | PC-side tools (a sender, a virtual monitor for VNC, a VNC bridge) |
| `docs/upstream/D-deck-layer/` | the Deck layer as patch files against KryonOS (for rebasing, not for upstream) |
| `docs/firmware/OTA.md`, `docs/firmware/UI_FROM_SERVER.md` | signed OTA from the server; the UI sent by the server (proposal) |
| `docs/04-PROTOCOL.md`, `docs/05-SECURITY.md` | the protocol and the security model (shared with `server`) |

`firmware/apps/deck/decknet.js` and `firmware/apps/lib/decknet.js` are the same file. The Deck app reads its copy from its
own folder: KryonOS asks for storage permission outside the app folder.

## Putting the apps on the card

- Development over the serial console, from the main folder: `./start.sh put <app>` (sends one app folder to the board).
- The SD card layout (`simulator/SD_Card`) is in the `simulator` branch. `./start.sh card deploy <mount>` copies it to a card.
  Known open point: the firmware reads some paths that differ from that layout (see Status).
- Per-device files (`deck.json`, `vnc.json`, `wifi_credentials.enc`) are never copied by the deploy: write them on the card.
  The example is `firmware/apps/vnc/vnc.example.json`. Write real secrets only on the card, never in a file of this repository.

## VNC (moved here from the kryonos branch)

The app is a JavaScript client. App, then WebSocket, then the server's Java bridge (or `tools/vnc-bridge.sh`), then a VNC
server on the PC (for example krfb). `tools/deck-monitor.sh` makes a virtual monitor the Deck's size (240x304 portrait).
Figures from the project notes (`docs/02` and `docs/06` in the kryonos branch): raw 16-bit about 3 fps (network-bound, about 400 KB/s); hextile 0.7 fps (JavaScript-bound).
Conclusion: JavaScript can steer; decoding must be native C.

## Status (18 Nov 2026)

- The Deck works on the hardware. The SD layout needs the KryonOS default.
- Bluetooth LE: the Deck link is built, not tested end to end on the board.
- The home screen app list (`[ APPS ]`) can be empty in some runs (open).
- The kernel sandbox blocks `/sd/system`, and some firmware paths differ from the SD layout (open).
- Full list of open points: the owner's roadmap (not in this branch).

## Licence and use

The Deck layer is the owner's own code. It has no restrictions: it is free to use, modify and sell, commercial or not,
provided as is, without warranty. No formal licence file is included. The KryonOS parts it runs on are under GPL-3.0
(`kryonos` branch).
