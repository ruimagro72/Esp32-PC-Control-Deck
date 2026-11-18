# Changelog

Newest first.

## Unreleased (17-18 Nov 2026)

- Simulator added: esp-emu 1.1.0 runs the real firmware image, with an SDL LCD and touch host and a GTK launcher.
- KryonOS board support patches prepared for the LCDWiki ES3C28P 2.8 inch (not sent upstream; see `docs/upstream/`).
- Deck-server flow verified in the emulator over Wi-Fi (`simulator/tests/wifi_server_check.sh`).
- `./start.sh test` commands: `all`, `server`, `native`, `emu`, `launcher`, `extensive`.
- Docs reorganised: branches in `docs/BRANCHES.md`, simulator docs in `docs/simulator/`.
- Fixes: touch press kept until the firmware reads it 3 times; LCD drawn at a whole-number scale; launcher start-up crash; Eject restarts with `--no-sd`.

## Known limitations

- BLE scan not confirmed in the simulator: `blekbd on` showed nothing; esp-emu logs `(C3)` on an S3.
- Audio capture in the simulator reads half the input; suspected emulator side, hardware not checked.
- esp-emu 1.1.0 does not model the LCD; the picture comes from our SDL cosim host.
- Deck app and the new SD layout: the kernel sandbox blocks `/sd/system`, and firmware paths differ from the layout.
- Messages not requested by the Deck: `messages_request` (0x10) is not sent.
- Console stops answering after a JS exception (seen in the emulator, not on hardware).

## Tests (2026-11-18)

- Server self-tests: 101 passed, 0 failed.
- Native JavaScript apps: 26 passed, 0 failed (13 apps, board Duktape).
- Launcher suite: 20 passed, 1 failed (shellcheck not installed).
- Simulator unit tests: 4 ok.
- Wi-Fi flow: 124 `POST /api/deck` requests answered 200 by the Java host, about 4 min 20 s.
