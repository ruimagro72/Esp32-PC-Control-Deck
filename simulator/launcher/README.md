# Simulator launcher (GTK3)

Desktop window to configure and run the esp-emu simulation. The LCD canvas is embedded (cosim client compiled in).
Status 18 Nov 2026. Design and plan: [docs/simulator/launcher/](../../docs/simulator/launcher/).

## Build and run

Needs `libgtk-3-dev`, and SDL2 for the embedded canvas.

```bash
./start.sh emu build-launcher      # or ./simulator/launcher/build.sh
./start.sh launcher                # build if needed, then run
./simulator/launcher/sim_launcher --help
```

## Window

- Menus: Profile, Simulator, Card, View, Help.
- Canvas first. The config drawer and the bottom dock are hidden by default. While stopped, the canvas shows a summary card
  of the effective configuration.
- Simulator: Start, Stop, Restart. Runs `./start.sh emu ui` in its own process group and stops esp-emu, the host and the bridge.
- Card: replace the SD image, eject, build an image from a folder, extract an image, open the folder. Changing the card
  restarts the emulator (the emulator cannot swap cards during a run).
- View > Language: English source texts, Portuguese translation included. The choice is saved as `ui_lang` in `launcher.conf`.
- Board: `es3c28p` and the KryonOS v3 boards. The chosen id is saved in `simulator/emu/.hw_board`.
- Firmware sources the launcher offers:
  - local es3c28p emulator build: `simulator/build/merged-emu.bin` with ELF `firmware/.pio/build/es3c28p_emu/firmware.elf`;
  - KryonOS v3.0.0 per board: `simulator/emu/firmware/kryonos-v3/<board>/merged.bin` (no ELF, `--no-elf`);
  - es3c28p asset copy: `simulator/emu/firmware/es3c28p/merged.bin`;
  - a custom merged `.bin`.
- Cosim board JSON: passed as `--board FILE` (default `simulator/emu/board-es3c28p.json`).
- HW rotation 0-3: saved in `simulator/emu/.hw_rotation`, passed as `--hw-rot`.
- UART TCP, `--net user`, I2S WAV recording (`--no-audio` turns it off), extra arguments after `--`.

## Modules

- `sim_launcher.c`: GTK UI (menus, canvas, drawer, dock, summary card).
- `config.{c,h}`, `proc.{c,h}`: `launcher.conf` and process control.
- `profile.{c,h}`, `profile.schema.json`: profile JSON `simulator.profile/v1`. Format: [profiles README](../../docs/simulator/launcher/profiles/README.md).
- `firmware_mgr.{c,h}`: detect and check artefacts, presets, esptool merge command.
- `validate.{c,h}`: preflight and capability states (`supported-now`, `needs-runtime-host`, `blocked-by-emu`, `conditional`).
- `board_catalog.{c,h}`, `boards/*.json`: board metadata and presets. Format: [boards README](../../docs/simulator/launcher/boards/README.md).
- `embed/`, `parts_ui.{c,h}`, `theme.{c,h}`: embedded canvas, parts view, dark theme.
- `console_log.{c,h}`, `telemetry.{c,h}`, `storage_littlefs.{c,h}`, `audio_net.{c,h}`, `emu_caps_probe.{c,h}`, `i18n.{c,h}`.

## Translations

- Source texts are in English. Translations: `po/pt.po`, template `po/sim_launcher.pot`.
- Add a language: `msginit -i po/sim_launcher.pot -o po/xx.po -l xx`, translate, rebuild, then add the code to `k_ui_langs` in `sim_launcher.c`.
- Compiled catalogues go to `locale/` (git-ignored).

## Command-line equivalent

```bash
./start.sh emu help
./start.sh emu ui --firmware <merged.bin> [--elf <app.elf>|--no-elf] [--hw-rot 0-3] [--no-audio] \
                  [--board [FILE]] [--uart-tcp [HOST:PORT]] [--net MODE] [-- <extra esp-emu args>]
./start.sh emu run|bootlog [SECONDS] --firmware <merged.bin> [--no-elf]
```

Logs go to `simulator/logs/`.

## Compiled, not editable at runtime

Panel size, pins, touch address and mirroring are in `simulator/emu/panel_config.h` and the host code. Change them there and run
`./start.sh emu build-host`. The launcher does not apply them at runtime yet (see `docs/simulator/launcher/INTEGRATION_PLAN.md`).

## State and config files

- `simulator/launcher/launcher.conf` (launcher settings, including `ui_lang`)
- `simulator/emu/.hw_rotation`, `simulator/emu/.hw_board` (emulator state)
- `simulator/logs/` (boot.log, i2s_out.wav, shot.png)

## Limits

Board support and emulator parity: [docs/simulator/emu/HARDWARE-SUPPORT.md](../../docs/simulator/emu/HARDWARE-SUPPORT.md).
