# Simulator

Runs an ESP32 firmware image on the PC with Espressif's `esp-emu` 1.1.0: CPU, flash, PSRAM, UART, Wi-Fi, I2S and an SD
image. `esp-emu` has no LCD and no touch chip, so a cosim client in this folder models them. The firmware under test is the
binary, not a mock.

Overview and licence: [`../README.md`](../README.md) and [`../SIMULATOR-LICENSE.md`](../SIMULATOR-LICENSE.md).
Full documentation: [`../docs/simulator/README.md`](../docs/simulator/README.md).

## Parts

| Path | What it is |
|---|---|
| `esp-emu-1.1.0-x86_64-unknown-linux-gnu/` | Location of the `esp-emu` 1.1.0 binary. Not in this branch and git-ignored: you install it locally. |
| `emu/sim_host.c`, `emu/sim_host.h`, `emu/lcd_touch_host.c`, `emu/panel_config.h` | Cosim client: LCD (SPI) and FT6336 touch (I2C 0x38), over `--cosim-tcp 127.0.0.1:5591`. Also compiled into the launcher. |
| `emu/boards/`, `emu/board-*.json` | Board definitions for the cosim client (ES3C28P, ESP32-S3 DevKitC-1, LilyGo T-Deck and T-HMI, M5Stack Cardputer). |
| `emu/parts/` | Parts definitions for the simulated board. |
| `emu/firmware/manifest.json` | Index of the firmware images the launcher offers. The images themselves are not in this branch (see below). |
| `emu/storage/` | Place of the SD image `emu/storage/sd.img` (FAT32, git-ignored). |
| `src/` | C++17 objects with unit tests: `Wire`, `LcdPanel`, `TouchChip`, `BoardProfile`, `File`. |
| `tests/` | Unit tests (`tests/unit/`), smoke tests in C, and shell checks (see below). |
| `launcher/` | GTK3 launcher: profiles, firmware merge, validation, console, log, card menu, embedded LCD canvas. |
| `Makefile` | Builds and runs the C++17 unit tests. |

## Build and test (verified on Linux, 18 Nov 2026)

```bash
make -C simulator test          # C++17 unit tests: 4 programs, all ok (needs g++)
simulator/launcher/build.sh     # GTK3 launcher (needs gcc, libgtk-3-dev, libsdl2-dev, pkg-config; msgfmt optional)
simulator/launcher/sim_launcher --help
```

`build.sh` writes `simulator/launcher/sim_launcher` and, when `msgfmt` is present, the translations in
`simulator/launcher/locale/`. The build prints six compiler warnings (`format-truncation`); it does not fail.

## Firmware images and the SD card

- The firmware images are not in this branch: `emu/firmware/manifest.json` lists them. The KryonOS v3.0.0 images come from
  the upstream release; the es3c28p image is a local build of the Deck firmware (from the `kryonos` and `deck` branches).
- The SD image is built from a folder of files. That folder (`SD_Card`) is generated from the KryonOS and Deck apps, so it
  is not in this branch. Without `emu/storage/sd.img` the launcher reports `SD card image not found` and the firmware runs
  without a card.

## What needs the owner's `start.sh`

The launcher's Start, Stop, Restart, card and build buttons call `./start.sh emu ...` and `./start.sh card ...`. The
owner's `start.sh` is not in this branch, so these buttons do not run from here as shipped. The `esp-emu` options that
`start.sh` passes are described in `docs/simulator/emu/HARDWARE-SUPPORT.md`. Several shell checks in `tests/` also call
`start.sh` (`run_extensive.sh`, `wifi_server_check.sh`, `ble_hci_check.sh`, `store_install_check.sh`) and fail here.

## Known limits

- LCD and touch are not modelled by `esp-emu`; the cosim client does it. Touch reports glass coordinates and the host
  converts the rotated point. The host holds a touch until the firmware has read it 3 times, because a slow emulator
  otherwise loses clicks. Check touch again in the emulator before relying on it.
- Audio: the `es3c28p_emu` build defines `ESP_EMU_SKIP_AUDIO` (audio init skipped, battery ADC stubbed).
- SD: the image cannot be removed or swapped during a run. Swap = restart.
- BLE: the HCI link to a BLE controller comes up; the scan result is not confirmed. `esp-emu` logs `(C3)` on an S3.
- Wi-Fi, I2S in real time, GDB and trace: see [`docs/simulator/emu/HARDWARE-SUPPORT.md`](../docs/simulator/emu/HARDWARE-SUPPORT.md).
- Only `esp-emu` 1.1.0 is the reference. Older notes in `docs/simulator/` describe earlier builds and are marked as such.
