# Launcher help

The GTK launcher is the desk tool of the simulator. Source: `simulator/launcher/` (`sim_launcher.c`). Build: `./start.sh launcher build` (or `./start.sh emu build-launcher`). Run: `./start.sh launcher` (or `./start.sh emu launcher`).

Short reference for each field: [LAUNCHER-TOOLTIPS.md](./LAUNCHER-TOOLTIPS.md). Launcher code notes: [simulator/launcher/README.md](../../simulator/launcher/README.md).

## What the launcher is

The launcher edits a **profile** (the emulated board: chip, display, firmware, card, audio, network), starts **esp-emu** with that profile, shows the emulated LCD on a canvas, sends your mouse as touch input, and shows the logs. It does not change the firmware. The Code/Build/Run tab can run a build command, but only when you press its button.

## Window layout

- **Menu bar**: Profile, Simulator, Card, View, Help.
- **Toolbar**: Start, Stop, Restart; active profile name; board and resolution; run badge (STOPPED, STARTING, RUNNING); run timer.
- **Nav rail** (left, 4 items). Each item opens the drawer on one page:
  - Profile: firmware. Drawer title "PROFILE / FIRMWARE".
  - Runtime: runtime, audio, network, LittleFS, SD. Drawer title "RUNTIME / AUDIO / NETWORK".
  - Merge / Run: run mode notes. The merge wizard is not built yet; use `./start.sh emu merge`.
  - Storage: parts list (WASM). Drawer title "STORAGE / PARTS". LittleFS and SD fields are on the Runtime page, not here.
- **Drawer**: the fields of the selected page. Close it with its close button or View > Configuration panel.
- **Canvas** (centre): the emulated LCD, drawn by the embedded SDL host. While the emulator is stopped, the canvas shows a **summary card** with the effective configuration of the active profile (12 rows: Board, Chip, Display, Firmware, ELF, Cosim board, SD card, Network/Wi-Fi/BLE, Audio, Console/UART, esp-emu, Command).
- **Bottom dock** (hidden at first start; View > Bottom panel, or the status bar button "Console / Logs"):
  - Tool tabs: Validation/Capabilities, Effective command, Processes/Telemetry, Debug/GDB/Trace, GPIO/Pins (state), Code/Build/Run.
  - Log tabs: esp-emu log, host/cosim log, Info. The log row has a text filter, level and origin filters, Follow, Clear and Export.
- **Status bar**: one status line (profile, run state, esp-emu version, card state) and capability badges. The status bar button toggles the dock.

The window title ends with " *" when the profile has unsaved changes. View menu states (panels shown or hidden, language) are kept in `simulator/launcher/launcher.conf`.

## Start, Stop, Restart, card

- **Start** (toolbar, Simulator menu, Ctrl+R): runs pre-start validation first. If an error blocks the start, a dialog "Cannot start the emulator" shows the first error and the suggested fix. Otherwise it starts esp-emu with the effective command and attaches the canvas.
- **Badge**: STOPPED, then STARTING while the emulator and host connect, then RUNNING. RUNNING shows when the host is attached and connected and more than 2 seconds have passed since the emulator started. This is a timer, not a firmware signal. In backend-only run mode it shows RUNNING at once.
- **Stop** (Ctrl+.): stops the emulator and the host.
- **Restart** (Ctrl+Shift+R): stop, then start.
- **Closing the window** (or Profile > Quit, Ctrl+Q) stops the emulator and saves the window layout and the launcher settings (`launcher.conf`). It does not write the profile file. If the profile has unsaved changes (`*` in the title), a dialog asks: Save (writes the active profile, then quits), Discard (quits without writing it) or Cancel (keeps the launcher open; Escape also cancels). The profile file is written only by Save, Save as, Duplicate and Export.
- **Card menu**:
  - Replace image... (restarts the emulator): choose a `.img` file. If the emulator runs, it restarts to apply the card.
  - Eject (restarts the emulator): turns the SD card off. The next start uses `--no-sd`. There is no confirmation dialog.
  - Build image from folder...: runs `./start.sh card image --force <folder> <image>`. It replaces the image, so what the firmware wrote to the card is lost. It asks Yes/No first. It refuses to run while the emulator is running: stop the emulator first.
  - Extract image to folder...: runs `./start.sh card extract --force <image> <folder>`.
  - Open card folder: opens `simulator/SD_Card`.

The card is an SD image file. esp-emu 1.1.0 supports it with `--sd` (default image `simulator/emu/storage/sd.img`). Limit: the card cannot be swapped while the emulator runs, so the launcher restarts the emulator to apply a change.

## Profiles

A profile is a JSON file in the format `simulator.profile/v1` (schema: `simulator/launcher/profile.schema.json`). It describes the whole emulated board. Top-level keys: `schema_version`, `metadata`, `board_hardware`, `firmware`, `storage`, `audio`, `network_radio`, `emu_runtime`, `ui_state`, `extensions`. Profile files are in `simulator/launcher/profiles/`.

Profile menu:

- New: loads default values into the editor. Nothing is written until you Save. Save then writes to `simulator/launcher/profiles/new-profile.json` unless you use Save as.
- Open...: loads a profile JSON and makes it the active profile.
- Save: writes the active profile file.
- Save as...: writes to a new file and makes it the active profile.
- Duplicate: writes the active profile to a new file and makes the copy active. Its id and name come from the new file name (as in Save as...). If the write fails, the active profile stays the original.
- Import conf: reads a legacy `launcher.conf` into the editor.
- Export: writes a copy to a file. The active profile does not change.
- Quit: same as closing the window.

The active profile path is in the drawer field "Active profile (JSON)". If that path is empty, the launcher uses `simulator/launcher/profiles/active.json`. If no profile file can be read, the launcher imports `launcher.conf` or uses built-in defaults and logs which one it used. There is no Ctrl+S shortcut.

### Shipped profiles

- `example-es3c28p`: starting point. Board es3c28p with the local merged image and ELF. SD off, networking off, Wi-Fi off, BLE off.
- `test-no-card`: SD off. User-mode networking on. Open Wi-Fi AP "testnet". BLE off. Checks a run without a card.
- `test-offline`: SD on (default card image). Networking, Wi-Fi and BLE off. Checks the card with no network.
- `test-wifi-open`: SD on. User-mode networking. Open Wi-Fi AP "testnet". BLE off. Wi-Fi join test.
- `test-ble-hci`: SD on. Networking and Wi-Fi off. BLE on over `tcp:localhost:9544`. Needs an HCI server on that port.

The `test-*` profiles use test values only.

### Start from the command line

```
./start.sh emu run --profile test-wifi-open
./start.sh emu ui --profile test-ble-hci
./start.sh emu shot --profile test-no-card --secs 20 --out shot.png
```

- `--profile NAME` reads `simulator/launcher/profiles/NAME.json`. A value with `/` or `.json` is a path.
- Command-line flags win over the profile.
- The profile options need `jq` in `PATH`.
- `storage.sd_enabled: false` means no `--sd`. `storage.sd_image_path` selects another card image.
- `network_radio.net_mode` gives `--net`. `network_radio.wifi` gives `--wifi-ssid` and `--wifi-auth`. `network_radio.ble` gives `--ble-hci`.

### Wi-Fi passphrase

- A profile never contains the Wi-Fi passphrase. `launcher.conf` never stores it either: an old `wifi_password` line is ignored when the file is read.
- The launcher keeps the passphrase in memory for the session only. At start it is seeded from the environment variable `SIM_WIFI_PASSWORD` if that variable is set. Example: `export SIM_WIFI_PASSWORD='...'` then `./start.sh launcher`.
- The entry field "Wi-Fi password" (Runtime page) is masked. There is no show/hide toggle.
- Effective command tab, Simulator > Copy command, and the start log show the passphrase as `'***'`. An empty open-network passphrase shows as `''`. Copy command therefore copies the masked form, which does not run with the real passphrase.
- The command that runs keeps the real passphrase: the launcher passes it to esp-emu as `--wifi-password`. On a shared machine, local users can read it with `ps` while esp-emu runs.
- For `./start.sh emu run --profile ...` with a non-open auth, start.sh reads the passphrase only from `SIM_WIFI_PASSWORD` and never prints it.

### Command quoting and extra args

- The command shown in the Effective command tab and copied by Simulator > Copy command quotes each value for the shell, for example `--wifi-auth 'open'`. Flag names are not quoted.
- The Runtime page field "Extra args" is added to the command as typed, without quoting. The launcher runs the command with `/bin/sh -c`, so anything in extra args runs in the shell. Load only profiles you trust: an imported profile's `emu_runtime.extra_args` runs the same way.

## Network

- **Network / user mode**: `--net user` (esp-emu user-mode networking). Enable it with the "Network enabled" checkbox on the Runtime page. The page also has a legacy checkbox "net enabled (user mode legacy)".
- **Host forwards**: field "Hostfwd (; sep)". Rules are separated by `;` and have the form `proto:hostaddr:hostport-guestaddr:guestport`.
- **Wi-Fi**: fields "Wi-Fi SSID", "Wi-Fi password", "Wi-Fi auth". The auth values are:
  - `open`: no passphrase. The launcher sends an empty `--wifi-password`.
  - `wpa2-psk`, `wpa3-sae`, `wpa2-wpa3`: need a passphrase of 8 to 63 characters.
  - `unknown` (GUI list only): sends no `--wifi-auth`. start.sh rejects it in a profile when Wi-Fi is enabled.
- **Verified**: the firmware joins the open test AP (`testnet`) and gets 192.168.4.2. It posts to the real Java host and gets HTTP 200 (18 Nov 2026, `simulator/tests/wifi_server_check.sh`). Only the open AP was verified end to end.
- **BLE over HCI**: checkbox "BLE" and field "BLE HCI". Format `tcp:host:port`, sent as `--ble-hci`. The HCI link to a Bumble controller was verified (`simulator/tests/ble_hci_check.sh`). The BLE scan (`blekbd on`) is not confirmed.

## Validation messages

Validation runs on Simulator > Validate and before each Start. Results are listed in the Validation/Capabilities tab with a severity:

- **ERROR** (blocks Start). Examples:
  - `AUDIO_NET_INVALID`: audio or network values are invalid. Fix the I2S paths, net mode, hostfwd or Wi-Fi/BLE values.
  - `AUDIO_WIFI_PASS_LEN`: Wi-Fi passphrase must be 8 to 63 characters. Use one, or set the network to open.
  - `CAP_SD_BLOCKED_BY_EMU`: SD is requested, a successful probe of the configured esp-emu ran, and `--sd` is not in its `--help` output (esp-emu pre-1.1.0). An unprobed or failed probe does not raise it. Fix: use esp-emu 1.1.0 or newer, or set the caps path to that binary.
  - `FILE_MISSING_FIRMWARE`: the merged firmware file is missing. Set a valid path or build the image.
  - `CONFLICT_CUSTOM_FW_EMPTY`: custom firmware is selected but no path is set.
  - `PORT_INVALID_UART`: UART TCP must be `host:port` with a port from 1 to 65535.
  - `RANGE_HW_ROTATION`, `RANGE_SCALE`, `RANGE_LCD_DIM`: rotation 0 to 3, scale 1 to 4, LCD size 0 or more (0 = default).
  - `SLFS_ERROR`: LittleFS image or partition problem.
- **WARN** (Start still runs). Examples:
  - `CONFLICT_ELF_ENABLED_NO_PATH`: use_elf is on but no ELF path.
  - `CONFLICT_LCD_PARTIAL_OVERRIDE`: only one of LCD width and height is set.
  - `CAP_RELEASE_NO_ELF`: running without ELF gives less symbol and debug data.
  - `CAP_LCD_RUNTIME_HOST`: LCD size override needs host support.
  - `AUDIO_WIFI_OPEN_PASS`: the open network ignores the passphrase.
  - `AUDIO_*_FLAG_MISSING`, `AUDIO_HOSTFWD_UNSUPPORTED`: the esp-emu binary does not list the flag, so the argument is left out.
  - `AUDIO_IN_MISSING`: the I2S input WAV file is missing.
  - `TOOL_MISSING_*`: a tool is not found (for example esp-emu, esptool, socat or nc, SDL2).
- **INFO**: the state only (`CAP_*`, `TOOL_FOUND`, `FILE_OK`, `RANGE_OK`, `PORT_OK`).

Refresh caps (Runtime page) runs `esp-emu --help` and `--version` again. Capability states come from that probe. Until a probe succeeds, the states are unknown.

## Help menu

- Help contents (F1): opens the help file in a viewer window. The viewer renders headings, bullets and code only; tables show as raw `|` lines. The file is `docs/simulator/LAUNCHER-HELP.md`, found from the working directory first, then from the binary's folder.
- Keyboard shortcuts: lists F1, Ctrl+R, Ctrl+., Ctrl+Shift+R and Ctrl+Q. It does not list the Alt menu mnemonics.
- About: the program name and a one-line description.

## Keyboard shortcuts

- F1: Help contents.
- Ctrl+R: Start.
- Ctrl+.: Stop.
- Ctrl+Shift+R: Restart.
- Ctrl+Q: Quit.
- Alt+P, Alt+S, Alt+C, Alt+V, Alt+H: open the Profile, Simulator, Card, View or Help menu (GTK mnemonics from the menu labels).

There is no shortcut for Save.

## Known limits

- **esp-emu 1.1.0 has no built-in LCD model.** The LCD is drawn by the SDL host (`simulator/emu/sim_host.c`) from the emulator's cosim data. A request to Espressif is drafted (not in this branch). It is not posted.
- **SD**: `--sd` works (SDMMC image mount verified). The card cannot be swapped while the emulator runs; the launcher restarts it.
- **BLE scan**: not confirmed (`blekbd on` gave no verified result). The HCI link works.
- **Wi-Fi**: only the open AP is verified end to end.
- **Wi-Fi passphrase**: held in memory and shown in the process list while esp-emu runs (see Wi-Fi passphrase above).
- **LCD size and pins** are compiled into the host. The runtime override is partial.
- **Emulated time** runs slower than real time (about 8 % in tests). `start.sh` sets the clock from the PC over UART (`time set`, every 20 s in the test flow).
- **Parts (WASM)**: discovery only. There is no runtime cosim for parts.
- **Merge wizard**: not built. Use `./start.sh emu merge`.
- **Telemetry and GDB/trace** depend on the host, the emulator and its flags.
