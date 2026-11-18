# Storage for esp-emu (SD image and LittleFS)

Status 18 Nov 2026, esp-emu 1.1.0.

## SD card (`--sd`)

- `esp-emu` 1.1.0 models the S3 SDMMC host and mounts an SDHC image. The es3c28p firmware mounts `/sd` from it.
- Image: `simulator/emu/storage/sd.img`, FAT32, 128 MB by default. Build it from the single source `simulator/SD_Card`:
  `./start.sh card image [--force] [DIR] [IMG] [MB]`. Also `card extract`, `card ls`, `card deploy <mount>` for a real card.
  The build uses mtools: no root and no mount.
- `start.sh emu run`, `ui` and `shot` pass `--sd` when the image exists (`run --no-sd` skips it). `bootlog` does not.
- Writes made by the firmware go into the image file. `simulator/emu/storage/*.img` is git-ignored (generated).
- A card cannot be removed or swapped during a run. To change the card, restart the emulator. The launcher's Card menu does this.
- esp-emu also has `ro` (read-only) and `cs=PIN` (SPI mode) and `cd=PIN` (card detect). `start.sh` does not pass them today.

## LittleFS (internal partition)

- The es3c28p merge preset puts LittleFS at 0xc90000 (`./start.sh emu merge --littlefs IMG`). Use it for small data.
- Without a valid image the firmware reports a mount error. This was not blocking in the last tests.

## Rules

- `simulator/SD_Card` is the content source. Do not keep a second copy of the card tree here.
- Do not commit generated images (`sd.img`, `sd-extract/`) unless they are needed for a test fixture.
