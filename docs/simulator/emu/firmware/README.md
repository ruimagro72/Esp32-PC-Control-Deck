# Firmware images for esp-emu

Prebuilt images under `simulator/emu/firmware/`. No recompile needed to run them. Reference binary: esp-emu 1.1.0.
Index of the assets: `simulator/emu/firmware/manifest.json`.

## Layout

- `es3c28p/`: local es3c28p emulator build (this repo).
- `kryonos-v3/<board>/`: KryonOS v3.0.0 release assets and a merged image per board.

## es3c28p (local `es3c28p_emu` build)

- Files in `es3c28p/`: `bootloader.bin`, `partitions.bin`, `firmware.bin`, `merged.bin`.
- `merged.bin` is byte-identical to `simulator/build/merged-emu.bin` (2864640 bytes, sha256 `fe93f20b...e94e89d8`).
  The older source path `simulator/emu/merged-emu.bin` no longer exists.
- `firmware.elf` is not copied here (git-ignored). Use `firmware/.pio/build/es3c28p_emu/firmware.elf`; `start.sh` uses it by default.
- `simulator/build/merged.bin` is a different image (3041392 bytes). Do not mix them up.

## KryonOS v3.0.0 (downloaded, SHA256-checked, merged)

- Source: https://github.com/Haris16-code/KryonOS/releases/tag/v3.0.0
- Chip `esp32s3`, flash 16 MB, `qio`, `80m`. Offsets: bootloader `0x0`, partitions `0x8000`, app `0x10000`.
- Merge per board: `./start.sh emu merge` (preset per board; see `./start.sh emu merge --help`).
- Verify: `sha256sum -c checksums.sha256` in each board folder.
- Boards: `esp32-s3-devkitc-1-n16r8`, `lilygo-t-deck`, `lilygo-t-hmi`, `m5stack-cardputer`. No ELF in the release: less symbol and hook detail.
- Asset URLs and SHA256 values per file: `../KRYONOS-V3.0.0-PREP.md`, section 1.

## Limits

Boot caveats for stock images without emulator guards (I2S, SD_MMC, ADC) and the SD and LCD status: see
`../HARDWARE-SUPPORT.md`.
