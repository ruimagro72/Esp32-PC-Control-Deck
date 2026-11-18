# ESP32 PC Control Deck

A desk device with a 2.8 inch touch screen, built on an ESP32-S3 board (LCDWiki ES3C28P). It shows the state of a PC and
can control it. The project has three parts, plus a simulator that runs the firmware on a PC without hardware.

This branch (`main`) holds the overview, the branch plan, the changelog, the third-party notices, the GPL-3.0 text and
`start.sh`, the single entry point. The code is in the four branches below.

## Branches

| Branch | What it is |
| --- | --- |
| `kryonos` | KryonOS 3.0.0 with support for the LCDWiki ES3C28P 2.8 inch board. KryonOS is a JavaScript OS for ESP32. GPL-3.0. |
| `deck` | The Deck layer for KryonOS: the Deck apps, the shell, the Deck link, the Bluetooth page, the VNC client and the PC tools. |
| `server` | The Java host that runs on the PC: REST API, protocol 4, macros, telemetry and the firmware store. |
| `simulator` | An independent ESP32 emulator front-end: runs an ESP32 firmware image on a PC (esp-emu, SDL host, GTK launcher). |

How they fit: the firmware image is KryonOS (`kryonos`) plus the Deck layer (`deck`). The Deck talks to the server
(`server`) with protocol 4. The simulator (`simulator`) runs the firmware image on a PC, so the board, the Deck and the
server can be tested together. Plan and details: [`docs/BRANCHES.md`](docs/BRANCHES.md).

## Getting the code in one tree

The branches keep their own paths (`firmware/`, `server/`, `simulator/`, `docs/`), so one working tree is made by
extracting the branches into the same folder. From a clone of this repository:

```bash
DIR="$HOME/esp32-deck"                      # any empty folder
mkdir -p "$DIR"
for b in kryonos deck server simulator main; do
  git archive "$b" | tar -x -C "$DIR"       # main last: its README and notices are the ones kept
done
cd "$DIR" && ./start.sh help
```

Root files that several branches have (`README.md`, `CHANGELOG.md`, `THIRD-PARTY.md`, `LICENSE`, `.gitignore`) are
replaced by the last extraction, so use the branch READMEs for the details of each part.

Checked on the assembled tree: `./start.sh help`, `./start.sh test help`, `./start.sh test server` (101 passed). `./start.sh test native` needs `simulator/SD_Card`, which is not in these branches. `./start.sh scan` needs the tree to be a git repository (`git init` in the folder).

## Status

- `kryonos`: 2.8 inch support works on hardware (13 Nov 2026) and in the emulator (18 Nov 2026).
- `deck`: works on hardware. The SD layout needs the KryonOS default.
- `server`: stable with protocol 4. The self-tests were run on 18 Nov 2026 (101 passed).
- `simulator`: usable, with limits (no LCD model in esp-emu; BLE scan not confirmed).

## Honest limits

- The project is developed on a single desk. Hardware is one board (ES3C28P); other boards are not supported in these branches.
- Several items are open, for example an app list on the home screen that is empty in some runs, and firmware paths that do
  not match the SD layout. The known problems are in each branch's documentation.
- Protocol 4 is the contract between `deck` and `server`: a change to it is a change in both branches.

## Licence

- `kryonos` (KryonOS and its derived parts): GPL-3.0. The text is in [`LICENSE`](LICENSE) on this branch and in `firmware/LICENSE` of `kryonos`.
- The owner's own code (`server`, `deck`, and everything of the owner's in `main`): **no restrictions**. It is free to use,
  modify and sell, commercial or not, provided as is, without warranty. No formal licence file is needed for it.
- `simulator`: free for the KryonOS team. Other use by consultation. See `SIMULATOR-LICENSE.md` on the `simulator` branch.
- Third-party components and their licences: [`THIRD-PARTY.md`](THIRD-PARTY.md).
