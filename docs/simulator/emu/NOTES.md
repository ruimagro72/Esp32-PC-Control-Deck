# ESP-Emu (espressif/esp-emulator) validation - ES3C28P

> **Historical lab notes (16 Nov 2026), superseded.** They describe esp-emu 0.51.0 and pre-1.1.0 and paths that no longer exist (`simulator/esp-emu/`, `simulator/emu/merged-emu.bin`). Current facts: reference binary esp-emu 1.1.0; SD works through `--sd` (`start.sh card image`); LCD and touch come from the cosim client. See `HARDWARE-SUPPORT.md` and `../../../simulator/README.md`.

## Artifacts
- `script.sh`: merge/run/bootlog helper (work in this folder).
- `merged.bin`: bootloader@0x0 + partitions@0x8000 + app@0x10000 (16MB, qio/80m).
- `boot*.log`: boot/UART captures from tests.

## Commands used
- Merge: `esptool.py --chip esp32s3 merge_bin -o simulator/esp-emu/merged.bin --flash_mode qio --flash_freq 80m --flash_size 16MB 0x0 firmware/.pio/build/es3c28p/bootloader.bin 0x8000 firmware/.pio/build/es3c28p/partitions.bin 0x10000 firmware/.pio/build/es3c28p/firmware.bin`
- Run: `esp-emu --chip esp32s3 --psram-size 8M --elf firmware/.pio/build/es3c28p/firmware.elf --firmware simulator/esp-emu/merged.bin`

## Findings (firmware es3c28p, esp-emu v0.51.0)
- **Serial/UART:** works (ROM + firmware logs visible). `--console`, `--uart-tcp`, `--uart1-tcp` available.
- **Boot (stock build):** hangs/panics with `Interrupt wdt timeout on CPU1` during `AudioEngine::init()` -> I2S driver install/start (`AudioEngine.cpp` / `i2s_driver_install`).
- **Boot (with emu workarounds, `-DESP_EMU_SKIP_AUDIO`):**
  - Skipping audio I2S init allows boot to continue.
  - SD init (SD_MMC for ES3C28P) can block/hang without hardware response; skipping it allows further progress.
  - ADC battery reads (`analogReadMilliVolts` / `adc_hal_convert`) caused wdt timeout in top-bar draw; returning safe stub values in emu mode allowed further progress.
  - With these workarounds + `--psram-size 8M`, firmware reaches `HarixKernel initialized successfully` and continues running without immediate panic (no visible serial after, likely in UI/main loop).
- **LittleFS/data partition:** internal LittleFS mount failed with merged app-only image (no valid FS image at 0xc90000 in that test). A LittleFS image can be merged if available; SD remains the separate external storage target.
- **Wi-Fi/Network:** esp-emu provides `--net user|tap|vmnet` and Wi-Fi SoftAP options (`--wifi-ssid/password/auth/...`). In tests it fell back to user-net (tap0 unavailable). Full firmware Wi-Fi/runtime behavior not fully exercised due to peripheral/display/input gaps and early boot blockers; capability exists but **parity with device Wi-Fi must be validated in a stable runtime scenario**.
- **BT/BLE:** esp-emu reports BLE symbol interception hooks when `--elf` is provided and supports `--ble-hci`. No full BLE workflow validated (firmware/runtime not fully interactive). **Not confirmed functional for this firmware/use case.**
- **SD:** ES3C28P uses SD_MMC; no esp-emu option found to attach/expose external SD card. Mapping `simulator/SD_Card` as the board SD card is **not directly possible with current observed emu interfaces** (needs alternative strategy).
- **LCD/touch/input:** ILI9341 over SPI/TFT_eSPI + board touch/input have no observed esp-emu display/touch output path. **No visible/interactive UI available out-of-the-box**.

## Temporary firmware-side emu guards added for testing
- `firmware/src/main.cpp`: skip audio init/play when `ESP_EMU_SKIP_AUDIO` defined.
- `firmware/src/FileSystem/FileSystem.cpp`: skip board SD init for CYD/T_DECK/ES3C28P when `ESP_EMU_SKIP_AUDIO` defined.
- `firmware/src/Hal/Boards/es3c28p/BoardConfig.cpp`: stub battery voltage/percent/charger when `ESP_EMU_SKIP_AUDIO` defined (avoid ADC hang).
These are compile-time guarded; normal builds unchanged unless flag is set.

## Cosim progress (earlier build, concrete start)
- `esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu` tested: boots same as 0.51.0 with emu workarounds (serial, WiFi SoftAP/net, BLE hooks, reaches `HarixKernel initialized successfully`).
- `lcd_touch_host.c` (SDL2, C, no Python): native cosim-tcp client claiming SPI CS=10 (LCD) + DC GPIO 46 and I2C addr 0x38 (FT6336G touch, SDA 16/SCL 15, INT 17, RST 18). Renders ILI9341 SPI transfers in a window; mouse mapped to touch registers.
- `script.sh ui`: launches the earlier esp-emu build with `--cosim-tcp` and the LCD+touch host; records I2S out to `i2s_out.wav` via `--i2s-wav` (Espressif-provided) unless `--no-audio`.
- Notes: SPI bus/controller resolution depends on firmware GPIO matrix routing; if display stays blank, cosim claims/routing and DC pin handling may need adjustment. Touch protocol is a minimal FT6x06 register model (may need tuning to match driver reads).
