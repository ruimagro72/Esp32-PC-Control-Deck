# Virtual parts (esp-part, cosim)

Status 18 Nov 2026. Area for virtual parts (WASM devices) and their wiring. No `.wasm` part is shipped. The
working device models are the cosim client (LCD, touch) and the in-process `regfile` devices in
`simulator/emu/board-es3c28p.json`. Wiring example: `simulator/emu/parts/wiring.example.json` (illustrative).

## Model (upstream esp-part / esp-emulator)

- **Part**: a WebAssembly module that models a component attached to chip pins or buses. Loaded by the emulator. It talks
  through the esp-part ABI (`part_*`) and protobuf messages (`esp_cosim.proto`).
- **SDK**: Rust crate `esp-part`. Implement `Device`, export with `esp_part::export_part!(Device)`. Message types come from
  `esp_cosim.proto` at build time (`ESP_COSIM_PROTO_DIR` when built outside the upstream repo).
- **Integration modes**:
  - `--cosim-board FILE`: in-process devices, parts and wiring. Preferred; no socket for devices.
  - `--cosim-tcp HOST:PORT`: external consumer or host. The only external transport in 1.1.0. Used for the LCD and touch.

## Device ABI (summary)

| Concept | Purpose |
|---|---|
| `NAME`, `configure(config)` | Identity; receives pin, address and strap config |
| `claims()` | Declares driven and watched pins, I2C addresses, SPI CS. The emulator rejects conflicting claims |
| `on_pins(levels, now)` | Reacts to pin level or timing changes |
| `poll(now)` | Emits outputs or state (one message per poll) |
| `state()` | Named values for UI or debug. Not pixels |
| `on_input(name)` | UI actions such as `press` or `set:*` |
| `ON_BUS` / `on_bus(request)` | I2C and SPI transactions; reply with the same `correlation_id` |
| `ON_MESSAGE` / `on_uart`, `on_pwm` | UART and PWM messages |
| `en()` / `drive_en()` / `on_en()` | EN or reset pin, when wired and supported |

Build and test (upstream pattern): `cargo build --release --target wasm32-unknown-unknown`; logic tests with `cargo test`
on the host. The wasm toolchain is not set up in this repo: conditional.

## Wiring and claims

- Claims reserve GPIO (drive or watch), I2C address, SPI CS and bus, UART, PWM and ADC, so firmware and parts do not conflict.
- Wiring connects MCU pins and buses to device pins (board JSON or part config, depending on the format of the esp-emu version).
- Local example: `simulator/emu/board-es3c28p.json` (FT6336 touch 0x38, ES8311 codec 0x18, both as `regfile`).

## Bus and peripheral examples

| Domain | Examples | Notes |
|---|---|---|
| GPIO, simple sensors | button, LED, relay | Quick state without hardware |
| I2C | SHT31, EEPROM, expanders, touch and codec regfiles | Regfiles are enough for init; full parts for fidelity |
| SPI | TMP123, 93C46 EEPROM, displays | SPI display parts are not included here (conditional) |
| UART | serial sensors, GPS-like devices | Needs UART claims in the ABI version used |
| PWM, timers | servo (SG90), rotary encoder (KY040) | Timing fidelity varies |
| ADC | potentiometer, NTC, battery sense | Needs analog support in the model |
| I2S, audio | WAV files, streamed audio | `--i2s-wav*` works locally; real-time PCM conditional |

## SD and storage

- SD is not a plain bus part. esp-emu 1.1.0 models it with `--sd IMAGE` (SDHC image on the S3 SDMMC host). The launcher and
  `start.sh` use it. See `emu/storage/README.md`.
- LittleFS partition images (`./start.sh emu merge --littlefs`) are the other storage route.

## Dependency on the esp-emu version

| Capability | Status (1.1.0) | Condition |
|---|---|---|
| `--cosim-board` in-process devices | used | Device types must exist in the binary |
| `--cosim-tcp` external host | used for LCD and touch | Protocol and ABI must match the binary |
| WASM SPI display or touch part | not included, not validated | Toolchain, ABI and emulator support needed |
| `--control-tcp` part or region commands, `--log-file` | varies by release | Check `esp-emu --help` |
| Real-time I2S via cosim consumer | tools exist upstream | Conditional on firmware and tools |

Always check the binary you run: `simulator/esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu --help`.

## Suggested structure (not created)

```text
simulator/emu/parts/
  wiring.example.json            # illustrative wiring and claims
  <board-or-target>/             # optional grouping
    wiring.json
    devices.json
  wasm/
    <part-name>.wasm             # esp-part module (cdylib, wasm32-unknown-unknown)
    <part-name>.README.md        # source, ABI/proto version, claims, build command
    <part-name>.license.txt      # attribution and licence, if redistributed
```

Guidelines:
- Keep the source and build steps next to each `.wasm`. Do not copy upstream source into this repo without a licence check (esp-emulator is Apache-2.0, this project is GPL-3.0).
- Record the `esp_cosim.proto` and ABI revision and the esp-emu version.
- Prefer in-process `--cosim-board` parts. Use the TCP bridge only as a fallback.
- Claims are authoritative: avoid overlapping I2C addresses, SPI CS and driven pins.
