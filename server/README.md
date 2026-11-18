# Java host (server)

The host runs on the PC and talks to the Deck (the ESP32 device). It provides a REST API, protocol 4 over USB, Bluetooth LE
(through a small C bridge) or Wi-Fi, macros, telemetry with plugins and the firmware store.

Version 0.7.0 (`server/pom.xml`). Protocol 4. Requires JDK 21 or newer and Maven 3.9 or newer (built with JDK 25 and
Maven 3.9.9 for the check below).

## Build

1. Package the host:

```bash
cd server
mvn -DskipTests package
```

This writes `target/control-deck-host-0.7.0-all.jar` (a single runnable jar).

2. Build the GPU plugins. The host loads them from `target/plugins/` (beside the jar); the self-tests need them:

```bash
mkdir -p target/plugins target/plugin-classes
for dir in plugins/*/; do
  [ -d "${dir}src" ] || continue
  name=$(basename "$dir")
  tmp="target/plugin-classes/$name"
  rm -rf "$tmp" && mkdir -p "$tmp"
  javac --release 21 -nowarn -cp target/control-deck-host-0.7.0-all.jar -d "$tmp" $(find "${dir}src" -name "*.java")
  cp -r "${dir}src/META-INF" "$tmp/"
  jar --create --file "target/plugins/$name.jar" -C "$tmp" .
done
```

This writes `target/plugins/gpu-amd.jar` and `target/plugins/gpu-intel.jar`. The `openaction` folder has no `src`, so the loop skips it.

Optional, the BLE bridge (Linux, needs the `libsystemd` development files and `pkg-config`):

```bash
make -C server/ble-bridge          # writes server/ble-bridge/build/ble-bridge
```

## Self-test

3. Run the self-tests. They run without hardware and end with a `Passed N, failed M, skipped K` line:

```bash
java -jar target/control-deck-host-*-all.jar --selftest
```

Expected last line:

```
Passed 101, failed 0, skipped 0
```

## Run

```bash
java -jar target/control-deck-host-*-all.jar
```

Defaults: API on `127.0.0.1:8080`, Wi-Fi off, USB serial port `auto`, BLE bridge on. The first run creates the database
`var/control-deck.db` with the default settings, so no configuration file is needed.

| Option | What it does |
|---|---|
| `--setup` | Ask for the settings and store them in the database (truncates the telemetry history) |
| `--bind ADDRESS`, `--port N` | Listen address and port for this run |
| `--no-serial`, `--no-ble` | Turn the USB or BLE transport off for this run |
| `--serial-port DEV`, `--baud N` | Serial port (`auto` by default) and baud rate (115200 by default) |
| `--list-ports` | List the serial ports and mark the ones that look like a board |
| `--import-macros FILE` | Copy the macros of a PHP host `config.json` into the database, then exit |
| `--console [--deck ID] [LINE]` | Show the Deck's console through the running host |
| `--selftest`, `--version`, `--help` | Self-tests, version, help |

Environment: `ESP32_SERIAL`, `ESP32_BAUD`, `CONTROL_DECK_DB` (database path), `CONTROL_DECK_VAR` (runtime directory, default
`./var`), `CONTROL_DECK_HOST_DIR`, and the per-run switches `CONTROL_DECK_WIFI`, `CONTROL_DECK_SERIAL`, `CONTROL_DECK_BLE`
(`1` or `0`).

On Linux the user needs access to the serial port (group `dialout` on most distributions). Only one program should read the
port at a time.

## Documentation

- [`docs/03-SERVER.md`](../docs/03-SERVER.md): REST API, settings, telemetry, plugins, macros.
- [`docs/04-PROTOCOL.md`](../docs/04-PROTOCOL.md): protocol 4, the contract with the Deck app.
- [`docs/05-SECURITY.md`](../docs/05-SECURITY.md): who may call the API, tokens, Wi-Fi, firmware signing.
- [`ble-bridge/README.md`](ble-bridge/README.md): the BLE bridge.

## Layout

```
server/
  pom.xml          Maven build (Java 21, shaded jar)
  src/main/java/   host: protocol, transports, HTTP API, macros, telemetry, firmware store, self-tests
  plugins/         telemetry plugins (gpu-amd, gpu-intel, openaction)
  ble-bridge/      C bridge to BlueZ over sd-bus (one file, Makefile)
```

## Not in this branch

- No `LICENSE` file: the licence of the server code is to be defined by the owner (see the `main` branch).
- No `start.sh`: the commands above are the complete build and run path.
