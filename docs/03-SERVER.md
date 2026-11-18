# Server: API, telemetry, GPU, plugins

---

# Server API and macros

The server (`server`) exposes telemetry, history, inventory, transport state and macro operations over HTTP, and runs the USB serial bridge. This is the local REST API (administration, `curl`, the OpenDeck plugin); the Deck's own link is binary protocol 4 ([04-PROTOCOL.md](./04-PROTOCOL.md)). Installing and starting: `server/README.md`.

## Start

```bash
./start.sh server
```

| Command or option | Effect |
| --- | --- |
| `run` (default) | Build when needed, start the API and the USB serial bridge |
| `--no-serial` | No USB serial bridge for this run (the API still runs) |
| `--serial-port DEV`, `--baud N` | Serial port (`auto`, `/dev/ttyACM0`, `COM4` ...) and baud rate for this run |
| `--bind ADDRESS`, `--port N` | API listen address and port for this run |
| `setup` | Ask for the settings (API address and port, Wi-Fi switch and token, serial port and baud, history length) and save them |
| `list-ports` | List serial devices and the one `auto` picks |
| `status`, `stop` | Whether the host runs; stop it |
| `selftest` | The host's own tests |
| `build`, `clean` | Maven build; remove the build output |

Options apply to one run and are never saved. Defaults: USB serial on, Wi-Fi off, API on `127.0.0.1:8080`. What the host prints is also written to `server/var/log/java-host.log`.

Requirements: JDK 21 or newer and Maven. Linux is the tested host; the telemetry readers use `/proc` and `/sys`.

## Access rules

| Class | Who | Endpoints |
| --- | --- | --- |
| public | anyone | `GET /` (health summary) |
| peer | the local computer, or a LAN peer with the Bearer token | telemetry, history, inventory, status, `/api/deck` (a Deck's binary frames), macro events, the sanitized macro list |
| local | the local computer only, even with a valid token | configuration, macro definitions, token management |

- A request from loopback needs no token. Any other address needs `Authorization: Bearer <token>`.
- Remote peers are refused outright (403) unless the Wi-Fi transport is enabled **and** a token exists. Binding the API to a LAN address does not by itself make it reachable.
- Only the SHA-256 of the token is stored (`transports.wifi.token_hash`). The token is shown once, when it is created.
- More than 10 failed attempts per minute from one address are answered with 429.
- Unauthenticated remote clients get 401/403 for every path, so routes cannot be enumerated.
- `X-Forwarded-For` and similar headers are not trusted.
- Macro definitions are executable configuration, so they cannot be read in full or written from the LAN, even with a token. A remote `GET /api/macros` returns only `id`, `label`, `action` and `hidden`.
- Requests made by web pages are refused (403): the `Host` header must be an IP address or `localhost`, and a request whose `Origin` / `Sec-Fetch-Site` shows another site is refused. A request with a body must send `Content-Type: application/json` (415 otherwise).

The host's HTTP server has no TLS. Use Wi-Fi only on a network you trust, and keep the firewall closed to other networks.

## Endpoints

| Method | Path | Class | Purpose |
| --- | --- | --- | --- |
| `GET` | `/` | public | Health summary: API and protocol version, capabilities |
| `GET` | `/api/stats` | peer | Current telemetry (the protocol `stats` frame) |
| `GET` | `/api/gpu` | peer | The GPU subset of the telemetry |
| `GET` | `/api/metrics` | peer | Metric registry with `available`, default metric per component, windows, retention |
| `GET` | `/api/history?metric=<id>&minutes=<n>&points=<n>` | peer | History of one metric |
| `GET` | `/api/history?component=<name>&minutes=<n>` | peer | History of every metric of a component |
| `GET` | `/api/inventory`, `/api/inventory/{component}` | peer | Component characteristics |
| `POST` | `/api/messages` | local | Leave a message for the Decks: `{"text": "...", "to": "<Deck id>" or "all", "from": "Alex", "kind": "text"}` (`"voice"` shows its length until the intercom carries audio). The Decks get it within 15 s; the gear shows how many are unread |
| `GET` | `/api/messages` | local | The messages kept (the newest 100), with the Decks that read each |
| `DELETE` | `/api/messages/{id}` | local | Delete one |
| `PUT` | `/api/language`, `/api/decks/{id}/language` | local | The language of the texts of all Decks, or of one: `{"language": "pt"}` (a table `texts/<lang>.json` must exist, else English) |
| `GET` | `/api/firmware/{board}` | peer | The board's firmware image (`application/octet-stream`), from `var/firmware/{board}.bin`; 404 when there is none. See `firmware_offer` in `04-PROTOCOL.md` |
| `GET` | `/api/transports` | peer | State of USB, Wi-Fi, BLE and Bluetooth Classic |
| `GET` | `/api/connection/status` | peer | Which transport is active, and health |
| `GET` | `/api/wifi/status` | peer | Wi-Fi transport state |
| `PUT` | `/api/wifi` | local | `enabled`, `bind`, `port` (restart to apply bind/port) |
| `POST`, `DELETE` | `/api/wifi/token` | local | Create/rotate or remove the peer token |
| `POST` | `/api/deck` | peer | A Deck over Wi-Fi: its protocol 4 frames in the body (`application/octet-stream`), the server's frames in the answer (`04-PROTOCOL.md`) |
| `POST` | `/api/console` | local | A line for a Deck's console: `{"line": "info", "deck": "<Deck id>" (optional: any Deck), "wait_ms": 3000}` (0 to 10000); the answer `{"id", "deck", "done", "text"}` (`02-FIRMWARE-KRYONOS.md`) |
| `GET` | `/api/console/{id}?wait_ms=<n>` | local | The rest of an answer that was not done |
| `GET` | `/api/console/log?after=<n>&deck=<id>` | local | The Decks' live log lines after number `n` (`{"last", "lines": [{"seq", "deck", "time", "text"}]}`; the newest 500 kept) |
| `POST` | `/api/event` | peer | Macro event `{"id": 3}` |
| `POST` | `/api/event/batch` | peer | `{"events": [1, 2]}`, `{"events": [{"id": 1}]}` or a bare list; at most 6 |
| `GET` | `/api/event/{id}/config` | peer | Sanitized macro summary |
| `GET` | `/api/macros` | peer | Macro list (sanitized for remote peers) |
| `POST` | `/api/macros` | local | Add a macro |
| `GET`, `PUT`, `DELETE` | `/api/macro/{id}` | local | Read, update, remove a macro |
| `POST` | `/api/macros/reorder` | local | `[3, 1, 2]` or `{"ids": [...]}`; every id exactly once |

Settings are changed with `./start.sh server setup` (and the Wi-Fi routes above). The PHP host's `/api/config/*` routes are not part of the Java host; configuration over HTTP returns with the browser administration (the roadmap is not in this branch).

Errors are JSON `{"success": false, "error": "..."}` with 400 (invalid input), 401, 403, 404, 405, 409, 413, 415, 429 or 500.

## Examples

```bash
curl http://127.0.0.1:8080/api/stats
curl 'http://127.0.0.1:8080/api/history?metric=cpu_usage&minutes=10&points=60'
curl http://127.0.0.1:8080/api/inventory/gpu
curl http://127.0.0.1:8080/api/transports

# create a Wi-Fi token on the host (shown once); start.sh setup does the same
curl -X POST http://127.0.0.1:8080/api/wifi/token

```

A Deck over Wi-Fi posts binary protocol 4 frames to `/api/deck` (`04-PROTOCOL.md`); there
is no JSON form of it to try with `curl`.

History response:

```json
{"success": true, "data": {"metric": "net_down", "label": "Download", "unit": "Mbit/s", "component": "network",
  "minutes": 10, "points": 5, "step_seconds": 120, "start": 1761552600, "end": 1761553200,
  "samples_in_window": 298, "latest": 23.4, "values": [0.1, 2.5, null, 12.0, 23.4]}}
```

`null` in `values` is a gap (no measurement) and must not be drawn as zero.

## Telemetry values

`/api/stats` returns the frame under `data`, plus `success` and `timestamp`. A value is measured or `null`; nothing is estimated. See [03-SERVER.md](./03-SERVER.md).

Rates (CPU, network, disk, GPU power) are differences between two readings; the previous reading is kept in `server/var/` so a restart does not lose it. Requests within one second of the previous sample reuse it, so polling clients cannot distort the rates. A sample is also written to the history.

## Wi-Fi setup

Steps: see `server/README.md` (`--setup`). `GET /api/transports` shows `problems` for a misconfiguration (for example Wi-Fi on while the API listens on a loopback address).

## Configuration

The settings live in the SQLite database `server/var/control-deck.db` (mode 600, ignored by Git), table `config`, with one section per concern:

```json
{
  "config_version": 2,
  "api":        {"bind": "127.0.0.1", "port": 8080},
  "telemetry":  {"interval_seconds": 2.0, "disk_path": "auto", "history_minutes": 30, "gpu_vram_total_gb": null},
  "transports": {
    "serial": {"enabled": true, "port": "auto", "baud": 115200},
    "wifi":   {"enabled": false, "token_hash": null},
    "ble":    {"enabled": false}
  },
  "macros":     []
}
```

The first run writes the defaults. `disk_path` selects a filesystem (`auto`, `/`, `/mnt/data`), not a device name. The macros of the old PHP host's `config.json` are copied once with `./start.sh server --import-macros FILE`.

## Runtime files

`server/var/` (ignored by Git) holds `control-deck.db` (settings, macros, token hash and the telemetry history in table `samples`), `sample_state.json`, `latest.json`, `transports.json`, `auth_failures.json`, `java-host.pid` and `log/java-host.log`. Delete only while the host is stopped.

## Serial behavior

Only one process can own the board's serial port. The bridge sends frames in small paced writes. It does not flash or modify firmware. Only protocol 4 firmware is understood ([04-PROTOCOL.md](./04-PROTOCOL.md)).

## Tests

```bash
./start.sh server selftest     # unit, integration, serial bridge and macro tests (no hardware)
```

## Macros

The ESP32 sends one numeric macro ID to the host bridge. The bridge then runs the matching action from the host's local configuration, the database `server/var/control-deck.db` (edit the macros with the host-only `/api/macros` and `/api/macro/{id}` routes; macros of the old PHP host are copied once with `./start.sh server --import-macros FILE`).

Commands, paths and URLs are not stored in the ESP32 firmware.

### Default layout

| ID | Label | Action |
| ---: | --- | --- |
| 1 | ChatGPT | Open ChatGPT in the default browser |
| 2 | Ollama | Check the local API and start Ollama when needed |
| 3 | Voice Server | Start a local voice-server script after you set its path |
| 4 | Terminal | Open a terminal |
| 5 | Task Manager | Open the system monitor |
| 6 | Lock PC | Lock the current session |

### Supported actions

#### Open a URL

```json
{
  "id": 1,
  "label": "ChatGPT",
  "action": "url",
  "value": "https://chatgpt.com/"
}
```

#### Start a command

A command can be a string or an argument array. Arrays avoid quoting problems and are recommended for Windows scripts.

```json
{
  "id": 3,
  "label": "Voice Server",
  "action": "command",
  "new_console": true,
  "value": {
    "windows": ["cmd.exe", "/c", "C:\\Path\\To\\START_VOICE_SERVER.cmd"]
  },
  "working_directory": {
    "windows": "C:\\Path\\To"
  }
}
```

Options:

- `hidden`: start without a visible console on Windows
- `new_console`: start in a separate terminal window (Windows, and Linux when a terminal emulator and a graphical session exist)
- `working_directory`: process working directory

Commands are started **detached and never waited for**, so a long-running command (a server) cannot block the API or the display. They run as argument arrays, never through a shell string. If the executable is not found, the event reports `Command not found: <name>` instead of failing silently.

#### Start a service only when it is unavailable

The `service` action checks `health_url` before starting the process.

```json
{
  "id": 2,
  "label": "Ollama",
  "action": "service",
  "health_url": "http://127.0.0.1:11434/api/tags",
  "hidden": true,
  "value": {
    "windows": ["ollama", "serve"],
    "linux": ["ollama", "serve"],
    "macos": ["ollama", "serve"]
  }
}
```

If the endpoint answers (HTTP 200-399), the host reports that the service is already running and does nothing. Otherwise it starts the command (detached) and reports `started`.

#### Send a hotkey (not supported yet)

```json
{
  "id": 4,
  "label": "Mute",
  "action": "hotkey",
  "value": "volume_mute"
}
```

Examples:

- `ctrl+shift+esc`
- `win+r`
- `alt+f2`
- `volume_mute`
- `volume_up`
- `media_play_pause`

#### Type text

```json
{
  "id": 5,
  "label": "Signature",
  "action": "text",
  "value": "Hello from ESP32 PC Control Deck"
}
```

### Platform-specific values

`value` and `working_directory` can contain per-platform entries:

```json
"value": {
  "windows": ["wt.exe"],
  "linux": ["x-terminal-emulator"],
  "macos": ["open", "-a", "Terminal"]
}
```

The public example keeps the Windows commands unchanged and provides separate Linux and macOS commands. Linux desktop environments use different terminal and system-monitor applications, so edit only the `linux` value when the default command is not installed.

**Support status of the host (Linux):** `url`, `command` and `service` work. `hotkey` and `text` need keyboard injection, which the host does not implement yet: pressing such a macro reports `Action 'hotkey' is not supported by the Java host (it requires keyboard injection)` (`macro_result` with `ok: false`) instead of silently doing nothing. `url` accepts only `http(s)` URLs. `url` and `new_console` need a graphical session (`DISPLAY` or `WAYLAND_DISPLAY`) in the environment that started the host. Windows and macOS command launching is best effort and not validated.

### Security boundary

The display cannot submit an arbitrary command string. It can only submit an ID from 1 to 6. The server maps that ID to an action already in its own settings database.

Treat the macros in the host's database as executable local configuration and add only actions you trust.


---

# Telemetry

What the server measures, where each value comes from, and how the metrics reach the Deck.

Policy: **a value is measured or it is `null`** (NaN in protocol 4). The server does not
estimate, guess or substitute. A missing value is shown as unavailable on the display and as a
gap in history graphs. Availability depends on hardware, kernel support and permissions.

## Metrics

`GET /api/metrics` returns this table with an `available` flag. The ids are the ones
`history_request` uses.

| Metric | Stats field | Unit | Component |
| --- | --- | --- | --- |
| `cpu_usage` | `cpu` | % | cpu |
| `cpu_temp` | `cpu_temp` | °C | cpu |
| `system_temp` | `system_temp` | °C | cpu |
| `ram_usage` | `ram` | % | ram |
| `gpu_usage` | `gpu` | % | gpu |
| `vram_usage` | `vram` | % | gpu |
| `gpu_temp` | `gpu_temp` | °C | gpu |
| `gpu_clock` | `gpu_clock_mhz` | MHz | gpu |
| `gpu_power` | `gpu_power_watts` | W | gpu |
| `net_down` | `net_down_mbps` | Mbit/s | network |
| `net_up` | `net_up_mbps` | Mbit/s | network |
| `disk_used` | `disk_used` | % | disk |
| `disk_read` | `disk_read_mbs` | MB/s | disk |
| `disk_write` | `disk_write_mbs` | MB/s | disk |
| `disk_temp` | `disk_temp` | °C | disk |

Default metric when a component's History button is pressed: `cpu_usage`, `ram_usage`,
`gpu_usage`, `net_down`, `disk_read`.

## Rates and idle values

Disk and network rates are differences between two readings of operating-system counters. A
rate of zero is correct when there was no traffic or disk I/O in the interval; small rates may
round to `0.0` on the screen (the JSON of `/api/stats` keeps 6 decimals). After a long pause
the server takes a short 150 ms baseline sample first, so rates are available at once. A
counter that goes backwards (interface reset) gives `null`, not a negative rate.

Disk capacity and I/O follow `telemetry.disk_path`: `auto` (the default) or a mounted
directory whose filesystem is monitored, such as `/` or `/mnt/data`. It is a filesystem
path, not a device name. `start.sh setup` does not ask for it yet.

## Temperatures (Linux)

| Field | Source |
| --- | --- |
| `cpu_temp` | hwmon `coretemp` / `k10temp` / `zenpower`: the Package, Tctl or Tdie sensor first, then another sensor of the same chip; otherwise a thermal zone of type `x86_pkg_temp`, `cpu-thermal`, `cpu_thermal` or `soc_thermal` |
| `system_temp` | hwmon `acpitz`, or a thermal zone of type `acpitz` / `pch_*` |
| `gpu_temp` | the GPU's own hwmon (below) |
| `disk_temp` | hwmon of the monitored disk: `drivetemp` (SATA) or the NVMe controller; `null` if the disk exposes none |

Thermal zones are matched by their `type`, never by position. Values are in degrees Celsius
(the kernel reports millidegrees).

## GPU (Linux)

The collector picks one DRM card (`/sys/class/drm/card*`), preferring one that exposes VRAM
counters, then an Intel Arc A770, then any other, and reads what its driver exposes:

| Field | Source | Null when |
| --- | --- | --- |
| `gpu` (utilization) | `device/gpu_busy_percent` (amdgpu); otherwise the DRM clients' fdinfo: the busiest engine class (`drm-engine-*` busy time on i915, `drm-cycles-*` on xe, per engine of the class) | no busy counter and no fdinfo |
| `vram_used_gb` | `device/mem_info_vram_used`, or the sum of `drm-total-lmem` / `drm-resident-local0` (i915) / `drm-resident-vram0` (xe) in the DRM clients | not exposed |
| `vram_total_gb` | `device/mem_info_vram_total`, or `gt/gt0/lmem_size_bytes`; otherwise `telemetry.gpu_vram_total_gb` if set (the default: unset) | not exposed and not configured |
| `vram` (percent) | used / total | either is null |
| `gpu_temp` | the GPU's own `device/hwmon/hwmon*/temp1_input`, or `temp2_input` (xe: package) | no sensor (i915 on an Arc has none) |
| `gpu_clock_mhz` | `gt_act_freq_mhz`, `gt/gt0/rps_act_freq_mhz` or `device/tile0/gt0/freq0/act_freq` (xe); 0 while the GT is in RC6 | not exposed |
| `gpu_power_watts` | the GPU's own hwmon `power1_input`, or the delta of its `energy1_input` (`energy2_input` on xe) | no counter |

The fdinfo readings need no privilege, so they count the programs of the user the server
runs as.

The GPU plugin (`GpuSource`) adds to the catalog, for the same card, the busy share of each
engine class (`gpu0.render`, `gpu0.video`, `gpu0.video_enh`, `gpu0.copy`, `gpu0.compute`),
the requested clock (`gpu0.clock_req`), the time in RC6 (`gpu0.rc6`, the mean of the GTs), the
fan (`gpu0.fan`), the VRAM in use (`gpu0.vram_used`) and the interrupts per second
(`gpu0.irq`, from `/proc/interrupts`). Sources, privileges, the i915 and xe differences, and what
`intel_gpu_top` reads that an ordinary user cannot: this document, the GPU section.

What the server deliberately does **not** do (earlier versions did): no placeholder VRAM
usage; no assumed 16 GB total for an Arc A770 (set `telemetry.gpu_vram_total_gb`; the
inventory then says `vram_total_source: "config"`); no CPU RAPL energy presented as GPU
power; no clock-based power estimate; no `rc6_residency_ms` presented as utilization; no
`sudo`, no permission changes.

**Intel Arc.** The A770 (PCI id `0x56a0`) is preferred when several cards exist.
Clock, power and fan come through sysfs and hwmon; the global busy counter needs the driver's
perf PMU, which an ordinary user cannot open (`CAP_PERFMON`). If the kernel exposes
`energy1_input` only to root, power stays `null`. i915 gives no temperature on the A770; the
VRAM size is only in the DRM query ioctl (this document, the GPU section, options B and C).

Checking what the driver exposes:

```bash
ls /sys/class/drm/card*/device/ | head -50
cat /sys/class/drm/card*/device/vendor
ls /sys/class/drm/card*/device/hwmon/hwmon*/
curl http://127.0.0.1:8080/api/gpu
```

## Component inventory

Static characteristics, separate from live telemetry (`GET /api/inventory/{component}`, the
`inventory` frame). A field the server cannot read is `null`; the Deck shows it as
unavailable and never infers it.

| Component | Fields |
| --- | --- |
| `cpu` | `model`, `physical_cores`, `logical_threads`, `max_mhz` |
| `ram` | `total_gb`, `type` (always `null`: needs root), `speed_mhz` (always `null`) |
| `gpu` | `name` (from `lspci` when installed), `vendor`, `pci_id`, `driver`, `vram_total_gb`, `vram_total_source` (`driver`, `config` or `null`) |
| `network` | `interfaces[]`: `name`, `type` (`ethernet`/`wireless`), `state`, `speed_mbps`, `default_route` |
| `disk` | `monitored_path`, `disks[]`: `name`, `model`, `size_gb`, `type` (`nvme`/`ssd`/`hdd`), `mount_points`, `monitored` |

## Other platforms

The collectors read `/proc` and `/sys`. On macOS and Windows every metric is `null` (the
clock still works). Earlier macOS and Windows branches that did not give correct values were
removed rather than kept as untested code.

## Metrics catalog (design of 4 Nov 2026, core built)

**Requirement:** the metrics are plugins, outside the firmware. A PC may have no GPU,
two GPUs, two CPUs, three network cards; the same firmware runs on Decks on different
machines, so it must not carry code, text or layout for parts a machine does not have.

Built on 4 Nov 2026: the server's telemetry plugins (`TelemetrySource`: `SystemSource`, the
Linux reader; `NetworkSource`, every network card; `GpuSource`, the GPU's engines, requested
clock, RC6, fan and VRAM in use), the `catalog` frame, values by index in
`stats`, `GET /api/catalog`, `app/deck_catalog.h`, and the LVGL Monitor built from it.
Still to do (secondary): several GPUs, history by catalog id, per-metric
refresh, Detail/History pages from the catalog, CPU per package or core, then the fixed 19
values leave `stats`.

1. **The server finds what the machine has**, through telemetry plugins: one per source
   (Linux `/proc` and `/sys`, Intel, AMD or NVIDIA GPU, LibreHardwareMonitor on Windows,
   ...), each giving **as many instances as exist**: `cpu0`, `gpu1`, `net:eth0`,
   `disk:nvme0n1`, ... The user can hide, rename or reorder them in the server's settings.
2. **The server sends the Deck a catalog** when they connect and when it changes: for each
   metric an index, an id, the group (component instance), a long and a short name, the unit,
   the kind (percent, temperature, rate, frequency, power, size), its range when fixed, and
   its refresh interval.
3. **The firmware knows kinds, not hardware.** It draws "a value of this kind" (a bar, a
   number, a chart line) and groups by component instance. Nothing in the firmware names a
   CPU, a GPU or a network card.
4. **Values travel by catalog index**; `history_request` will ask by index.
5. **Per-metric refresh**: each metric at its own interval (minimum 1 s, chosen on the Deck,
   kept per Deck); the Deck joins the metrics that are due into one request and the server
   serves only what is asked.

Plugin contract (decided 2 Nov 2026): each plugin says which components and metrics it reads
and returns readings in a standard shape. Java plugins by default; a plugin that needs
another language runs as a long-lived separate program (never one process per sample);
native code inside the JVM (FFM) only if a frequent reading ever justifies it.


---

# GPU telemetry on Intel (i915 and xe)

Can the server show what `intel_gpu_top` shows for the Intel Arc A770 (and other Intel GPUs on
the i915 and xe drivers), without privileges, and without copying code? Analysis of 4 Nov 2026
on this PC (Arc A770, PCI `0x56a0`, i915, Debian kernel 6.12.115), and what the server reads
since then. The metric list and the other components are in `03-SERVER.md`.

## Verdict

Most of it, yes; the rest needs one decision by the user.

- **Now, with no privilege** (built, `GpuSource`): busy per engine class (render, video,
  video enhance, copy, compute), actual and requested clock, power, RC6 (idle) share, fan,
  VRAM used, interrupts per second, and the GPU temperature on xe. All from sysfs, hwmon,
  `/proc/interrupts` and the DRM clients' fdinfo.
- **Against igt-gpu-tools v2.6** (the current upstream, reviewed after the first analysis, see
  below): the server already shows more than `intel_gpu_top` v2.6 on this card without
  privileges. That tool **refuses to start** without `CAP_PERFMON` (it has no fdinfo-only
  mode), **refuses xe** cards (it points to `gputop`), and shows **no power for a discrete
  card** (only the CPU's RAPL `energy-gpu` / `energy-pkg`, and only for an integrated GPU).
  Neither it nor `gputop` reads a temperature or the VRAM size. Our fdinfo arithmetic (time /
  cycles, engine capacity) matches `gputop`'s.
- **The catch**: `intel_gpu_top`'s global figures come from the driver's **perf PMU**, which an
  ordinary user cannot open (`CAP_PERFMON` is needed; Debian sets `perf_event_paranoid` to 3).
  What an ordinary user can read instead, the fdinfo, counts **only that user's own
  programs**. On a desktop PC where the user runs everything (here `kwin_wayland`,
  `plasmashell`, the browser) the two are close; GPU work by other users or system services
  is missed. And a local copy of intel-gpu-tools is not the `intel_gpu_top` that
  reads the PMU at all (below).
- **Still missing**: the VRAM size (the query ioctl gives it to any user, but Java cannot
  call an ioctl under the project's rules: a small helper, design below), free VRAM and the
  global (all-users) busy figures (`CAP_PERFMON`: a helper the user grants it to, design
  below), and the GPU temperature on i915 (not exposed by the driver at all on an Arc).

## igt-gpu-tools v2.6: what the modern tools read

Source: igt-gpu-tools, the official freedesktop.org repository, tag `v2.6`
(commit `916c79c`, Sep 2026). Files read: `tools/intel_gpu_top.c`, `tools/gputop.c`,
`tools/gputop/xe_gputop.c`, `lib/igt_drm_fdinfo.c`, `lib/igt_drm_clients.c` (all MIT: the
permission notice or `SPDX-License-Identifier: MIT`). Every finding in this section is from
v2.6.

**`intel_gpu_top` (i915 only).**

- Busy per engine, frequencies, RC6 and interrupts: the i915 perf PMU only. It finds the PMU
  (`i915_<pci>` for a discrete card, `i915` for the integrated one) and opens one group of
  counters: `<engine>-busy/-wait/-sema` per physical engine, requested and actual frequency
  and RC6 residency per GT (`__I915_PMU_*_FREQUENCY(gt)`, `__I915_PMU_RC6_RESIDENCY(gt)`; the
  RC6 shown is the **mean of the GTs**), and `interrupts`.
- If `pmu_init` fails it prints the `CAP_PERFMON` message and **exits**: there is no
  unprivileged mode. The `-p` option means "physical engines instead of classes", not
  "without the PMU".
- xe: "Xe devices are not supported by intel_gpu_top. Please use 'gputop' tool instead."
- Power: RAPL through perf (`/sys/devices/power`, `energy-gpu` and `energy-pkg`), opened only
  when the card is **not** discrete. An Arc gets no power figure (and RAPL also needs perf
  rights).
- Memory: only the clients' fdinfo (`system0`, `local0`: total, shared, resident, purgeable,
  active), per client or summed per PID. It does **not** remove shared buffers counted by
  several clients; it shows `shared` beside `resident` for the user to judge.
- Clients: when the render node has fdinfo, the same per-client table as `gputop` (below),
  next to the PMU figures. The per-client percentage is busy time over the period, capped at
  100 x capacity.
- No temperature, no fan, no VRAM size or free VRAM, no hwmon or sysfs reading at all.

**`gputop` (any DRM driver, i915 and xe).** Reads the DRM clients' fdinfo; no privilege, own
processes only. Per client and engine class: engine time (`drm-engine-<class>` ns) as
`delta_ns / period / capacity`, or, when a client gives both `drm-cycles-*` and
`drm-total-cycles-*`, `delta_cycles / delta_total_cycles / capacity`, capped at 100 %.
Capacity defaults to 1 when `drm-engine-capacity-*` is absent. Memory: `drm-total-*` or
`drm-resident-*` summed over regions. On xe it can also read the xe PMU
(`engine-active-ticks`, `engine-total-ticks`, percent = active / total), which needs perf
rights like the i915 PMU.

**`lib/igt_drm_clients.c`.** Finds DRM files by `fstat` (character device, major 226), counts
a client once per (DRM minor, `drm-client-id`) (skips duplicate descriptors), filters by
`drm-pdev`, and keeps per-client last values to take deltas. A client that goes backwards is
not counted for that period.

**Against our code.**

| Point | igt v2.6 | Server | Result |
| --- | --- | --- | --- |
| Engine time (i915) | delta ns / period / capacity | the same, summed over own clients, capped at 100 | same |
| Cycles (xe) | delta cycles / delta total cycles / capacity, when both keys are present | the same | same |
| Capacity | default 1 | default 1 | same |
| Duplicate descriptors | skipped by (minor, client id) | skipped by client id (the kernel's id is unique across devices) and `drm-pdev` | same |
| New client | its first sample shows nothing until it has two | not counted until the next reading | same |
| Shared buffers | not removed | not removed | same limitation, documented |
| RC6 | mean of every GT (PMU) | was gt0 only; **now the mean of every `gt*/rc6_residency_ms`** (xe: `tile*/gt*/gtidle`) | fixed |
| Interrupts | PMU `interrupts` | **now `/proc/interrupts`**, the card's lines, no privilege | added |
| Power on a discrete card | none | hwmon `energy1_input` | we show more |
| Temperature, fan, VRAM size | none | hwmon (xe temperature, fan); VRAM size: option B | we show more |

## The local copy of intel-gpu-tools is the old tool

the local intel-gpu-tools copy (commit `fb7bc2e8`) is **intel-gpu-tools 1.3** (`configure.ac`,
autotools, about 2013), a personal fork, kept untouched. Its `tools/intel_gpu_top.c` (714 lines) maps the GPU's registers
through libpciaccess (`intel_get_mmio`, `pci_device_map_range`) and samples `INSTDONE` and the
ring HEAD/TAIL registers in a loop: it needs root and knows only the GPUs of that time. It does
not know the Arc (DG2), the xe driver, the PMU or fdinfo, so it cannot answer the question.

The installed `intel_gpu_top` (Debian package `intel-gpu-tools 2.0-1`) is the modern one, and
the PMU is what it needs. Run here as the ordinary user (read-only, nothing changed):

```
$ intel_gpu_top -J -s 1000 -o -
Failed to initialize PMU! (Permission denied)
When running as a normal user CAP_PERFMON is required to access performance monitoring.
```

The first analysis (same day) described the modern tool from the kernel's interfaces only; the
v2.6 source, reviewed afterwards (section above), confirmed it and added the RC6 mean over GTs
and the interrupts.

## Sources, privileges, and what this PC gives

| Value | Source | i915 | xe | Privilege | Read here (ordinary user) |
| --- | --- | --- | --- | --- | --- |
| Engine busy, all processes | perf PMU `i915_<pci>` (`rcs0-busy`, `vcs0-busy`, ...) / `xe_<pci>` (engine active ticks) | yes | yes (newer kernels) | `CAP_PERFMON` (`perf_event_paranoid` is 3) | no: `Permission denied` |
| Engine busy, own processes | `/proc/<pid>/fdinfo/<fd>` of `/dev/dri/*`: `drm-engine-<class>: <ns> ns` (i915), `drm-cycles-<class>` + `drm-total-cycles-<class>` (xe), `drm-engine-capacity-<class>` | yes | yes | none (own processes only) | yes: render 0.7-1.5 %, video, video enhance, copy, compute 0 % |
| Actual clock | `card0/gt_act_freq_mhz`, `gt/gt0/rps_act_freq_mhz` / `device/tile0/gt0/freq0/act_freq`; PMU `actual-frequency` (average) | yes | yes | none (PMU: `CAP_PERFMON`) | 600 MHz, **0 while the GT is in RC6** |
| Requested clock | `card0/gt_cur_freq_mhz`, `gt/gt0/rps_cur_freq_mhz` / `tile0/gt0/freq0/cur_freq`; PMU `requested-frequency` | yes | yes | none | 600 MHz |
| RC6 / idle | `gt/gt*/rc6_residency_ms` (ms, counter; the mean of the GTs) / `tile*/gt*/gtidle/idle_residency_ms`; PMU `rc6-residency` per GT | yes | yes | none | 20-74 % of the time |
| Interrupts | `/proc/interrupts`, the lines naming the card's PCI address and driver, summed over CPUs; PMU `interrupts` | yes | yes | none (PMU: `CAP_PERFMON`) | 463-531 /s |
| Power | hwmon of the card: `energy1_input` (µJ, counter; xe also `energy2_input`, package). `intel_gpu_top` v2.6: CPU RAPL via perf, integrated GPUs only | yes | yes | none here (the file is `0444`) | 44.6-48.6 W (the card idles high) |
| Power limit | hwmon `power1_max` | yes | yes | none to read | 190 W |
| Fan | hwmon `fan1_input` (RPM) | yes (6.12) | newer kernels | none | 0 RPM (fans stopped at idle; one reading of 16) |
| Temperature | hwmon `temp*_input` | **no** (i915 has no temperature on DG2) | `temp2_input` package, `temp3_input` VRAM | none | not exposed |
| VRAM used, own processes | fdinfo `drm-resident-local0` (i915), `drm-resident-vram0` (xe) | yes | yes | none (own processes) | 1.16-1.20 GB |
| VRAM size | `DRM_IOCTL_I915_QUERY` / `DRM_I915_QUERY_MEMORY_REGIONS` `probed_size` on `/dev/dri/renderD128`; xe: `DRM_IOCTL_XE_DEVICE_QUERY` (`MEM_REGIONS`) | yes | yes | none (render group) | **17 079 205 888 bytes = 15.9 GiB** (read-only probe in C, see below) |
| VRAM free, all processes | the same query's `unallocated_size` | yes | yes | `CAP_PERFMON` (without it the kernel reports the size) | no: `unallocated == probed` |
| VRAM size in sysfs | none in either driver on kernel 6.12 (checked in the modules' strings) | no | no | | |

Other read-only facts from this PC: `/proc/sys/kernel/perf_event_paranoid` is `3`; the process
has no capabilities (`CapEff: 0`); `/sys/bus/event_source/devices/i915_0000_03_00.0` exists
(type 14) and lists the events above; `/dev/dri/renderD128` is `crw-rw---- root render` and the
user is in `render` and `video`; the i915 module parameter `lmem_size` is readable only by root
and is a limit in MiB (0 = no limit), not the VRAM size; BAR 2 is 16 GiB (Resizable BAR), the
CPU's window on the VRAM, also not its size; `dmesg` is closed to the user.

The VRAM-size probe was 30 lines of C written here from the kernel's uapi header
(`/usr/include/drm/i915_drm.h`, MIT), compiled in the session's scratch folder and run once as
the user; it is not part of the project. Its output:

```
class=0 instance=0 probed=33427415040 unallocated=33427415040 ...   (system memory)
class=1 instance=0 probed=17079205888 unallocated=17079205888 ...   (device memory, VRAM)
```

## The catch, in full

1. **The PMU is privileged.** Every global figure of `intel_gpu_top` (busy per engine for all
   processes, averaged frequencies, RC6, interrupts) comes from `perf_event_open` on the
   driver's PMU, a system-wide event: it needs `CAP_PERFMON` (or `CAP_SYS_ADMIN`, or
   `perf_event_paranoid` <= 0; Debian's 3 blocks unprivileged perf entirely).
2. **fdinfo is per process.** It needs no privilege, but `/proc/<pid>/fd` of another user's
   process cannot be read: the busy share and VRAM in use are those of the user the server
   runs as. A VRAM buffer shared between two programs (the compositor and a window) is counted
   in both, so "VRAM used" can be higher than the real figure. igt v2.6 does not remove them
   either: fdinfo gives `drm-shared-*` per client but not which buffers, so no tool can
   subtract them exactly.
3. **i915 and xe differ.** i915 writes busy time in ns; xe writes busy and total GPU cycles.
   Engine classes are `render, copy, video, video-enhance, compute` in i915 and
   `rcs, bcs, vcs, vecs, ccs` in xe. The clocks and the idle counter are in the card's folder
   (i915) or under `device/tile0/gt0/` (xe). xe has temperatures, i915 on an Arc has none. A
   GPU with two GTs (Meteor Lake: render and media) has `gt0` and `gt1`; the clocks are read
   from `gt0`, RC6 is the mean of all GTs (as `intel_gpu_top` v2.6). `intel_gpu_top` v2.6
   itself refuses xe cards; `gputop` reads them.
4. **The VRAM size is behind an ioctl.** It needs no privilege, but Java has no ioctl, and the
   project's rules keep native code out of the JVM (FFM would also need JDK 22; the project
   builds for 21). Free VRAM needs `CAP_PERFMON` on top.
5. **No GPU temperature on i915 Arc**, from any interface an ordinary user has (`intel_gpu_top`
   does not show one either). Only the xe driver gives it.
6. **The actual clock is 0 while the GT sleeps (RC6)**; that is a measurement, not a fault.

## What the server reads now (no privileges)

`HostSensors` picks the card (as before) and reads, besides what it read already:

- the driver's name (the `device/driver` link);
- requested clock, the RC6 / idle counter (mean of the GTs) and the fan, from the paths in
  the table;
- the card's interrupt count from `/proc/interrupts` (only lines with both its PCI address and
  its driver's name: another card on the same driver is not mixed in);
- xe's actual clock, temperature (`temp2_input` after `temp1_input`) and package energy
  (`energy2_input` after `energy1_input`);
- `DrmClients` gives each engine class in percent of its capacity (the busy time of every own
  client since the previous reading, divided by the time and by `drm-engine-capacity`; for xe
  the busy cycles over the total cycles), and the GPU use as the busiest class. A client that
  appears between two readings counts from the next one.

The i915 `lmem_size` module parameter is no longer read as a VRAM size (it is a limit in MiB;
a server run as root would have shown 0 GB).

The telemetry plugin `GpuSource` puts them in the catalog, group `GPU`, beside the system
plugin's `gpu0.use`, `gpu0.vram`, `gpu0.temp`, `gpu0.clock`, `gpu0.power`:

| Id | Name | Unit | Kind | Refresh |
| --- | --- | --- | --- | --- |
| `gpu0.render` | Render engine | % | percent | 1 s |
| `gpu0.video` | Video engine | % | percent | 1 s |
| `gpu0.video_enh` | Video enhance | % | percent | 1 s |
| `gpu0.copy` | Copy engine | % | percent | 1 s |
| `gpu0.compute` | Compute engine | % | percent | 1 s |
| `gpu0.clock_req` | GPU clock requested | MHz | frequency | 2 s |
| `gpu0.rc6` | GPU idle (RC6) | % | percent | 2 s |
| `gpu0.fan` | GPU fan | RPM | frequency (a rotation; protocol 4 has no other kind) | 5 s |
| `gpu0.vram_used` | VRAM used | GB | size | 2 s |
| `gpu0.irq` | GPU interrupts | /s | rate | 2 s |

A value the driver does not give never enters the catalog (an AMD card has no RC6 or engine
entries). On this PC the catalog has 28 entries (the Deck keeps 40). Values read from the
running server here (ports 18080 and 18095, temporary `var/`): use 0.7-1.5 %, render
0.7-1.5 %, the other engines 0 %, clock 0-600 MHz, requested 600 MHz, RC6 20-74 %, fan 0 RPM,
VRAM used 0.94-1.20 GB, interrupts 463-531 /s, power 44.6-48.6 W, temperature `null`, VRAM `%`
`null` (size unknown unless `telemetry.gpu_vram_total_gb` is set; the real default is unset).

Self-tests (`GpuTests`, fake sysfs and `/proc` trees in a temporary folder): the i915 Arc case
(engines with capacity, the same client through two descriptors counted once, clocks, RC6
counter as the mean of two GTs, interrupts of this card only, fan, no temperature), a client
that opens the GPU between two readings, the xe case (cycles, tile0 paths, package temperature
and energy, `drm-resident-vram0`), and the plugin's entries, RC6 share and interrupt rate,
including a counter that went back.

## Options for the rest

### A. Only unprivileged sources (done)

What is above. Nothing to install. Limits: own processes only, no VRAM size (set it in the
settings), no i915 temperature.

### B. `gpu-info`: a helper for the VRAM size, no privilege (proposed)

A C program of some 100 lines in `server/gpu-info/`, built like `server/ble-bridge` (one file,
a Makefile, `-Wall -Wextra`), written from the kernel's uapi headers (`i915_drm.h`,
`xe_drm.h`, both MIT). The server runs it **once at start** per Intel card
(`gpu-info /dev/dri/renderD128`), reads one line of JSON
(`{"driver":"i915","vram_total_bytes":17079205888}`), and uses it as the VRAM size
(`vram_total_source: "driver"`); a missing or failing helper leaves the size unknown, as now. It
opens the render node read-write (the DRM API needs it; the ioctls only read), needs no
capability, and it is not a long-lived process. This closes the VRAM percentage on i915 and xe
without configuration.

### C. `gpu-pmu`: a helper with `CAP_PERFMON`, set by the user (design only)

For the global figures (all processes) and free VRAM. A long-lived C program, like
`ble-bridge`:

- opens the driver's PMU (`/sys/bus/event_source/devices/i915_<pci>/type` and `events/*`,
  `perf_event_open` with `pid = -1`, the PMU's `cpumask` CPU, one group of the busy, frequency
  and RC6 counters), reads the group once a second and writes one line of JSON on stdout
  (`{"t":..., "busy":{"rcs0":ns,...}, "act_mhz_cycles":..., "rc6_ns":...,
  "vram_free_bytes":...}`); the server turns two lines into percentages as it does with
  every counter;
- free VRAM from the same query ioctl (`unallocated_size`, now honest under `CAP_PERFMON`);
- the server starts it if it exists and reads it as another telemetry plugin; when it runs,
  its figures replace the fdinfo ones (a `source` field in `/api/gpu` says which);
- the **user** grants the capability, once, documented in `01-ARCHITECTURE-AND-SETUP.md`:
  `sudo setcap cap_perfmon=ep server/gpu-pmu/build/gpu-pmu` (again after each rebuild). The
  server never asks for it, never runs `sudo`, never changes `perf_event_paranoid`.

Cost of C: `CAP_PERFMON` lets the program read every perf counter of the machine (CPU
counters of all processes too), so the program must stay small, take no input but its
command line, and do nothing but read; a fault in it cannot reach the server (separate
process). The alternative, `perf_event_paranoid` <= 0 for the whole system, opens perf to
every program and is not recommended.

A variant without new C code: the user grants `CAP_PERFMON` to the installed
`intel_gpu_top` and the server reads its JSON output (`intel_gpu_top -J -s 1000 -d
pci:slot=0000:03:00.0`) as a long-lived child process. It gives busy per engine for all
processes, frequencies, RC6 and interrupts (v2.6), but no power on a discrete card, nothing
for xe, and a capability on a system binary that every user can run; option C keeps the
capability on a program of ours that does only this.

Recommendation: B is cheap and closes the VRAM size for everyone; C only if the
own-processes figures prove too far from the real ones (to check with `sudo intel_gpu_top`
beside the Deck, by the user).

## Licence

Checked file by file in the local intel-gpu-tools copy (4 Nov 2026):

| File | Notice |
| --- | --- |
| `tools/intel_gpu_top.c` | Copyright 2007, 2011 Intel Corporation, MIT (X11) permission notice |
| `lib/intel_gpu_tools.h`, `lib/instdone.c`, `lib/instdone.h`, `lib/intel_mmio.c`, `lib/intel_pci.c`, `lib/intel_reg_map.c`, `lib/intel_chipset.h`, `lib/intel_drm.c` | Intel (and Oracle in `intel_drm.c`), MIT permission notice |
| `lib/intel_reg.h`, `lib/i915_reg.h` | Precision Insight / Tungsten Graphics, MIT-style permission notice |
| `COPYING` | collects those permissive notices |
| `piglit/` | a separate test suite with GPL-2, GPL-3 and LGPL-2 files: not read |

Checked in igt-gpu-tools v2.6 (commit `916c79c`), the files read:

| File | Notice |
| --- | --- |
| `tools/intel_gpu_top.c` | Copyright 2007-2023 Intel Corporation, MIT permission notice |
| `tools/gputop.c`, `tools/gputop/xe_gputop.c`, `tools/gputop/xe_gputop.h`, `tools/gputop/utils.c`, `tools/gputop/utils.h` | `SPDX-License-Identifier: MIT`, Intel 2023 / 2025 |
| `lib/igt_drm_fdinfo.c`, `lib/igt_drm_clients.c` | MIT (checked by the coordinator; the second SPDX MIT) |

No GPL file and nothing in `piglit/` was opened.

All of these are MIT-compatible, so reuse would be allowed with the notice kept, but
**nothing was copied or translated**: the old tool reads registers that do not apply to the
Arc, v2.6 was used only to check our method against it (the table "Against our code"), and the
server is written from the kernel's documented interfaces
(`Documentation/gpu/drm-usage-stats.rst`, the i915 and xe sysfs attributes, the hwmon sysfs
ABI, the uapi headers `i915_drm.h` and `xe_drm.h`, MIT).

## VRAM size from the PCI BAR (13 Nov 2026)

i915 and xe do not put the VRAM size in sysfs. Fallback added in HostSensors.vramFromBar: the
card's largest prefetchable 64-bit memory BAR from sysfs `resource` (no root). With Resizable BAR
on, an Intel dGPU maps all its memory there: the Arc A770 shows BAR2 = 16.00 GiB, so VRAM total
16 GB and VRAM % now reach the Deck. Under 2 GiB (no Resizable BAR: 256 MiB) it is not used and
the configured `gpu_vram_total_gb` stays. GPU temperature: i915 on DG2 exposes none in hwmon
(only energy, power limit, fan); it needs the xe driver. perf_event_paranoid is 3 on this PC, so
intel_gpu_top's PMU counters need root; the server keeps using the DRM clients' fdinfo for busy %.

### GPU temperature on the Arc A770 (13 Nov 2026)

- i915 gained hwmon `temp1_input` (package temperature) for DG1/DG2 around kernel 6.12/6.13
  (commit 0f47fed5, sysfs-driver-intel-i915-hwmon). Debian's 6.12.116 lacks it: the card's hwmon
  has only energy, power limit and fan. xe has `temp2_input` (package) and `temp3_input` (VRAM)
  but needs i915/xe force_probe swapped on the kernel command line.
- The server already reads hwmon `temp1_input` (i915) and `temp2_input` (xe)
  (HostSensors.java ~160): no code change; the value appears with a kernel that publishes it.
- Recommended: the trixie-backports kernel (7.2.12 on 13 Nov), keeping i915. Installed by the
  user; the 6.12 kernel stays in GRUB as a fallback.


---

# Server add-ons (plugins)

Hardware that differs from PC to PC (GPU vendors first) is read by **plugins**, not by the host.
A PC installs only the plugins of its own hardware; with none, the host still reports the common
values. Decided on 13 Nov 2026.

## How it works

- Interface (SPI), public, in the host: `dev.controldeck.host.telemetry.spi`
  - `GpuPlugin`: `supports(driver)`, `read(card, values)`, optional `preference(card)`.
  - `GpuContext`: read-only view of one card (paths under /sys; a fixture tree in the tests).
  - `GpuValues`: what the plugin fills (busy %, VRAM used/total, clock, requested clock, idle ms).
- Loader: `telemetry/Plugins.java` — `ServiceLoader` over the jars in `plugins/` next to the host
  jar (`server/target/plugins/`), or `-Dcontroldeck.plugins=<folder>`. A broken jar is skipped.
- The host keeps what every Linux GPU driver offers: the card choice (a 2 GiB+ PCI window,
  known vendors, plus the plugins' preference), hwmon (temperature, power, energy, fan) and the
  DRM clients' fdinfo (busy per engine class, local memory). Plugins run first; the common
  sources fill what they leave empty; then the configured VRAM size.

## Plugins in the repository

| Plugin | Drivers | Reads |
|---|---|---|
| `server/plugins/gpu-intel` | i915, xe | local memory per client, VRAM size (lmem_size_bytes or the PCI BAR with Resizable BAR), clocks (gt/rps/tile), RC6/idle |
| `server/plugins/gpu-amd` | amdgpu | gpu_busy_percent, mem_info_vram_used/total; prefers the discrete card |

To add one (NVIDIA next, for example): a folder `server/plugins/<name>/src/` with the classes
and `META-INF/services/dev.controldeck.host.telemetry.spi.GpuPlugin` naming them.

## Build

`./start.sh server build`: Maven builds the host, then `build_plugins` (inside `start.sh`)
compiles each plugin with `javac --release 21` against the host jar and packs it with `jar`
(JDK only) into `server/target/plugins/<name>.jar`. The self-test uses the same folder, so the
GPU tests need the plugins built.

## Checked 13 Nov 2026 (Arc A770, i915)

With the plugins: VRAM total 16 GB, VRAM %, clock. Without them: only VRAM used (fdinfo) and
power (hwmon). Self-test 101/101.


