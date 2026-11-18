# Branches

The repository is organised in four independent branches. Each one has its own purpose, its own release rhythm and its own README. Work on one branch does not need the others to build or run, except where noted below.

| Branch | Content | Runs on |
| --- | --- | --- |
| `kryonos` | KryonOS 3.0 with support for the LCDWiki ES3C28P 2.8 inch board | ESP32-S3 board |
| `deck` | The Deck application: `firmware/apps/deck`, `decknet`, the shell and the Deck apps | Inside KryonOS on the board |
| `server` | The Java host on the PC: REST API, protocol 4, macros, telemetry, firmware store | PC (JDK 21+) |
| `simulator` | ESP32 emulator front-end: esp-emu binary, SDL cosim host, GTK launcher | PC (Linux) |

## Branch details

### `kryonos`

- KryonOS 3.0 with the support for the LCDWiki ES3C28P 2.8 inch board (ESP32-S3).
- Patch groups, as in `docs/upstream/`: **B** (fixes valid on every board), **A** (board and ES8311 audio), **C** (console and JavaScript API).
- Kept here for now: the kernel-side DeckLink support that the Deck needs (optional for other users).
- Status of the upstream work: the patches are prepared, not yet sent. The branch evolves until the KryonOS team decides whether to support the board.
- The branch will reopen for two things: the 3.5 inch LCDWiki E32R35T (classic ESP32, no PSRAM), and corrections to the 2.8 inch support.
- Details: `docs/06-UPSTREAM-KRYONOS.md` and `docs/upstream/README.md` (in the `kryonos` branch).

### `deck`

- The Deck application. It runs on KryonOS and needs a Deck server to be useful. It never runs on its own.
- Contents: the Deck apps (`deck`, `decklink`, `bluetooth`, `deckshell`), the shell in `firmware/apps/shell/`, and the Deck's own card files.
- It follows KryonOS releases: each KryonOS release is taken as the base, and the Deck layer is rebased on it.

### `server`

- The Java host that runs on the PC: REST API, protocol 4 over USB, Bluetooth LE or Wi-Fi, macros, telemetry with plugins, and the firmware store with signing.
- The protocol is the contract with the Deck app. A change to the protocol is a change in both branches.
- Self-tests: `./start.sh server selftest`.

### `simulator`

- An independent ESP32 emulator front-end: the `esp-emu` 1.1.0 binary, the SDL cosim host (LCD and touch) and the GTK launcher.
- It runs any ESP32 firmware image, KryonOS or not, of any version.
- Free for the KryonOS team. Other use by consultation.
- Limits: `simulator/README.md` and `docs/simulator/SIMULATOR_CAPABILITIES_MATRIX.md` (in the `simulator` branch).

## How the pieces fit

- The firmware image is built from `kryonos` plus the files of `deck`. The composition is described in each branch README.
- `./start.sh build` compiles the firmware tree in `firmware/` for the board (`ENV=es3c28p`).
- The Deck app talks to the `server` branch over USB, Bluetooth LE or Wi-Fi, using protocol 4.
- The `simulator` branch runs the firmware image on the PC, so the board, the Deck app and the server can be tested together without hardware.

## Status

- `kryonos`: 2.8 inch support working, tested on hardware on 13 Nov 2026 and in the emulator on 18 Nov 2026.
- `deck`: working on hardware. The SD layout needs the KryonOS default (see the roadmap in the `deck` branch).
- `server`: stable, protocol 4.
- `simulator`: usable, with the limits listed in `simulator/README.md` (in the `simulator` branch).
