# Hardware support in esp-emu for the es3c28p board

Status 18 Nov 2026. Reference binary: `esp-emu` 1.1.0 (`simulator/esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu`).
Statuses: **usable** = tested here; **partial** = works with limits; **not modelled** = no emulator support; **unverified** = flag exists, behaviour not tested end to end.

## Principle

- Peripherals that `esp-emu` does not model are answered by a device model. Prefer `--cosim-board` (in-process devices
  such as `regfile`, no socket). `--cosim-tcp HOST:PORT` is the only external transport in 1.1.0; there is no Unix socket
  or pipe option (checked on 1.1.0).
- The LCD and touch use the cosim client (`emu/lcd_touch_host.c`, `emu/sim_host.c`) over `--cosim-tcp 127.0.0.1:5591`.
  Its default is `PANEL_COSIM_PORT` in `emu/panel_config.h`.

## Builds and artefacts

| Build | PlatformIO env | Use |
|---|---|---|
| Emulator | `es3c28p_emu` | Defines `ESP_EMU_SKIP_AUDIO`: audio init skipped, battery ADC returns safe values. |
| Hardware | `es3c28p` | No emulator guards. |

```bash
cd firmware && pio run -e es3c28p_emu     # firmware.elf and firmware.bin
./start.sh emu merge --emu                # merged image (default: simulator/build/merged-emu.bin)
```

Merged image for the es3c28p preset: bootloader at 0x0, partitions at 0x8000, app at 0x10000, LittleFS at 0xc90000,
16 MB. Copy kept at `simulator/emu/firmware/es3c28p/merged.bin`.

## Support map

| Hardware (es3c28p) | Status | How it runs | Notes |
|---|---|---|---|
| CPU, PSRAM | usable | `--chip esp32s3 --psram-size 8M` | Set by `start.sh`. |
| Flash image | usable | `--firmware merged.bin --elf firmware.elf` | `--no-elf` for release images without ELF. |
| UART console | usable | `./start.sh emu console` (UART TCP, default `127.0.0.1:5555`) | `ls`, `cat`, `df`, `time` commands. |
| USB CDC (`Serial`) | unverified | `--console auto` or `usb-cdc` | The firmware is built with `ARDUINO_USB_CDC_ON_BOOT=1`; check `esp-emu --help`. |
| GPIO | partial | `--cosim-board` parts | No WS2812 (GPIO 42) model: needs a part with edge timing. |
| I2C touch and codec | partial | `--cosim-board simulator/emu/board-es3c28p.json` | `regfile` devices: touch FT6336 at 0x38 on I2C0 and I2C1, codec ES8311 at 0x18 on I2C0 and I2C1. |
| LCD ILI9341 over SPI | usable | cosim client over `--cosim-tcp` | Pins: CS 10, DC 46, SCLK 12, MOSI 11, MISO 13, BL 45. Panel 240x320, mirror X on. Read replies are zero-filled (up to 4096 bytes). |
| Touch FT6336 (I2C 0x38) | usable | cosim client (host answers the bus) | Pins: SDA 16, SCL 15, INT 17, RST 18. See the claim note below. |
| I2S audio | partial | `--i2s-wav FILE` (out), `--i2s-wav-in FILE` (in) | `ui` records `simulator/logs/i2s_out.wav` unless `--no-audio`. Real-time PCM not validated. |
| Wi-Fi 2.4 GHz | unverified | `--net user`, `--wifi-ssid/--wifi-password/--wifi-auth`, `--hostfwd` | Guest network is emulated. A scan returns the single AP configured by `--wifi-ssid`/`--wifi-auth` (profile SSID and auth); esp-emu cannot switch that AP off (a profile with `wifi.enabled=false` still shows the default `myssid`). Internet connectivity may not match a real network. |
| Bluetooth LE | unverified | `--elf firmware.elf --ble-hci tcp:HOST:PORT` | BLE hooks are reported with an ELF; a full BLE workflow is not confirmed. |
| SD card (SDMMC) | usable | `--sd simulator/emu/storage/sd.img` | SDHC image, FAT32, built by `./start.sh card image` from `simulator/SD_Card`. The firmware mounts `/sd`. Writes go into the image. Options `ro`, `cs=PIN` (SPI mode), `cd=PIN` exist in esp-emu; `start.sh` does not pass them. Removing or swapping the card during a run is not modelled: restart. |
| LittleFS (internal) | partial | `./start.sh emu merge --littlefs IMG` | Missing or invalid image: mount error, not always blocking. |
| Battery | model (placeholders) | profile `board_hardware.battery` -> `--i2c-dev 0:0x5B,...` register file | The firmware (build with `ESP_EMU_SKIP_AUDIO` only) reads the register file and models charge, voltage and estimates. Capacity, charge current and consumption figures are placeholders, not ES3C28P measurements. Without the block the old stub values apply (4.0 V, 75 %). Register map in `docs/simulator/LAUNCHER-HELP.md`. |
| Debug: GDB, trace, control | unverified | `--gdb PORT`, `--gdb-halt`, `--trace FILE`, `--control-tcp HOST:PORT` | Flags exist in `esp-emu --help`. Useful output needs the ELF. |

## Claim conflict to check

`board-es3c28p.json` gives the FT6336 device address 0x38 on both I2C buses. The cosim client also answers 0x38 on the
touch pins. esp-emu rejects overlapping claims. Do not combine `--board` with the cosim touch path until this is checked
on 1.1.0 (not tested).

## Commands

```bash
./start.sh emu bootlog 12 --board         # boot with board regfiles, 12 s
./start.sh emu run --board -- --console auto
./start.sh emu ui --board                 # board regfiles + cosim client for the LCD
./start.sh emu shot --secs 25 --cmd "ls /sd"   # headless: console replies and LCD capture
```

`--board` without a file uses `simulator/emu/board-es3c28p.json`. `shot` writes `simulator/logs/shot.png` by default.
UART TCP does not carry binary screenshots: use `shot` or the SDL window for visual evidence.

## Wi-Fi and BLE checks (18 Nov 2026, esp-emu 1.1.0)

- Wi-Fi verified with `--net user --wifi-ssid testnet --wifi-password "" --wifi-auth open`: firmware console `wifi testnet|` gives connected, IP 192.168.4.2, gateway 192.168.4.1 reaches the host loopback.
- `./start.sh emu shot` accepts `--net`, `--wifi-ssid`, `--wifi-password`, `--wifi-auth`.
- Launcher auth values sent to esp-emu: `open`, `wpa2-psk`, `wpa2-wpa3`, `wpa3-sae`.
- BLE: esp-emu connects to a Bumble virtual controller (`simulator/tests/ble_hci_check.sh`; needs a Python venv with Bumble in `BUMBLE_VENV` and the optional local helper `hid_advertiser.py` in `BUMBLE_HELPER`, not tracked by git). The HCI link to the Bumble controller is up; whether `blekbd on` scans is not confirmed yet (console capture was empty).
- Wi-Fi end to end verified with the real Java host: `simulator/tests/wifi_server_check.sh` (throwaway DB under `simulator/logs`, socat proxy logs the traffic). The firmware joins the open test AP, `DeckLink.wifiSetup("http://192.168.4.1:8089", token)` over the console, the server answers `POST /api/deck` with 200 and a protocol frame, and the firmware keeps polling every 2 emulated seconds.
- The server needs `PUT /api/wifi {"enabled":true,"bind":"0.0.0.0","port":N}` and a restart, since the Wi-Fi transport keeps its own bind (the `--bind` flag is not enough).
- Peers behind the emulator NAT look like loopback, so the server shows no `last_peer_seen`.

## Audio capture (input WAV format)

Status (18 Nov 2026): the input is accepted, the recording is NOT correct yet.

- Input WAV must be 16001 Hz, 2 channels, 16 bit (PCM). The legacy I2S driver of the firmware rounds the requested 16000 Hz to 16001 Hz. `--i2s-wav-in` refuses a file whose rate differs: boot log line `--i2s-wav-in: '...' is (16000, 2, 16) ... but the firmware receives (16001, 2, 16); not fed`. The launcher and `start.sh` do not convert files; make the file with the right format.
- Make a test tone (1 kHz, 3 s): `ffmpeg -f lavfi -i "sine=frequency=1000:sample_rate=16001:duration=3" -ac 2 -ar 16001 -c:a pcm_s16le simulator/logs/audio/rx_in_16001.wav`. Check the header: bytes 24-27 `81 3E 00 00` (16001), bytes 28-31 `04 FA 00 00` (64004).
- Run on a throwaway SD copy, with the console on the default UART port 5555: `./start.sh emu bootlog 300 --uart-tcp --i2s-wav <abs>/simulator/logs/audio/i2s_out_16001.wav --i2s-wav-in <abs>/simulator/logs/audio/rx_in_16001.wav --sd <copy.img>`. `bootlog` overwrites `simulator/logs/boot.log`.
- Drive the console after boot (the firmware runs without a client, but its output needs one): `(sleep 20; printf 'js System.speaker.record("/sd/rec_test.wav",3)\r\n'; sleep 150) | socat - TCP:127.0.0.1:5555`. Reply `JS OK`.
- Extract the result with `mcopy -i <copy.img> ::/rec_test.wav <out.wav>`. The record is mono 16000 Hz 16 bit (96044 bytes).
- Measured (run of 18 Nov 2026): log has no `not fed` line; the receiver is (16001, 2, 16); the source reports `is spent; silence follows`. The recording is not all 0xFFFF, but the tone is wrong: about 2000 Hz (not 1 kHz) for the first 1.5 s (24000 samples), then all zero. Upward zero crossings: 500 per 0.25 s block over 1.5 s, then 0. The firmware consumed the 3 s input in 1.5 s of recorded samples, so each recorded sample takes two input frames. Cause not yet found; the firmware side is not changed.
- Playback side: `i2s_out_16001.wav` (44101 Hz, 2 ch, 16 bit, first format kept) is all zero in this run (no playback was requested). `simulator/logs/audio/i2s_out.wav` from the earlier run has a signal (about 332 Hz by zero crossings at 44101 Hz, source unknown); no +1 Hz pattern was checked there.
- Not tried: 16000 Hz mono input. The log names the receiver as stereo, so it was not run.

## Limits

- LCD: no ILI9341 part (`.wasm`) exists; the screen depends on the cosim client. Moving it into `--cosim-board` needs a part and the wasm toolchain, which is not set up here.
- SD: swap during a run not modelled (restart instead). `card image` makes a 128 MB image by default; pass a size in MB to change it (`./start.sh card image [--force] [DIR] [IMG] [MB]`).
- Audio: real-time PCM through cosim is not validated; use the WAV files.
- Host SPI reads return zeros. Add real register answers only if the firmware needs them.
- Wi-Fi: end-to-end connectivity depends on the emulated network; `--net user` is NAT inside esp-emu.
- WebManager (server side, firmware): mutating requests are accepted when `Origin` and `Referer` are absent (non-browser clients). Session cookies are `HttpOnly; Path=/; SameSite=Lax` without `Secure` over HTTP. Review before exposing the port.
- Other boards (KryonOS v3 images): see `KRYONOS-V3.0.0-PREP.md`. No board-specific LCD or touch model exists for them here.
