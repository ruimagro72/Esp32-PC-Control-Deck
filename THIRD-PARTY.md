# Third-party components and licences

The project's own code (firmware layer, server) has no restrictions. The simulator is free for the KryonOS team; other use by consultation. This table covers only the third-party components and their licences. The root [`LICENSE`](LICENSE) file is the GPL-3.0 text; it is not a statement of the project's own licence.

"To confirm" means the licence is not stated in the repository and has not been checked against the upstream source.

| Component | Where in this repository | Licence | Source and notes |
|---|---|---|---|
| KryonOS (base of the firmware) | KryonOS fork, branch `es3c28p-support` ([ruimagro72/KryonOS](https://github.com/ruimagro72/KryonOS)); in the assembled tree, `firmware/` except the parts below | GPL-3.0 (upstream base; the Deck's own layer: owner's code, no restrictions) | [Haris16-code/KryonOS](https://github.com/Haris16-code/KryonOS), release v3.0.0. Our changes are commits on the fork's branch and patch files in `docs/upstream/`. The upstream README and `LICENSE` are in the fork. |
| Duktape 2.7.0 (JavaScript engine) | `firmware/src/Runtime/duktape.c`, `duktape.h` | MIT | Header: Copyright 2013-present Duktape authors. Git commit 03d4d72 (from the header). |
| ESP8266Audio `AudioGeneratorMOD` decoder, via Win-95-Tracker-Cyd | `firmware/src/Hal/Audio/MODPlayer.h`, `MODPlayer.cpp` | GPL (version to confirm) | Header: "GPL licensed", by Earle F. Philhower III. Taken from [ivans805/Win-95-Tracker-Cyd](https://github.com/ivans805/Win-95-Tracker-Cyd), whose own licence is to confirm. `ModPlayer.cpp/.h` (task and API) are ours. |
| minimp3 | `firmware/src/Hal/Audio/minimp3.h` | CC0 1.0 (public domain dedication) | Header; [lieff/minimp3](https://github.com/lieff/minimp3). |
| TFT_Touch, adapted to FT6336G | `firmware/lib/TFT_Touch/` | To confirm | Adapted from the LCDWiki ES3C28P / ESP32-32E demo package (`Install libraries/TFT_Touch`). `library.properties` names Bodmer as original author. The folder has no licence file. |
| LCDWiki ES3C28P demo code (base for the board port) | `docs/upstream/A-board-es3c28p/` (patch 0001 includes the vendor touch library) | To confirm | Board vendor package; no licence text in the repository. |
| `decknet.js` (Deck protocol 4 in JavaScript) | `firmware/apps/deck/decknet.js` | Owner's own code, no restrictions | Project code. The header has no licence line. |
| Simulator launcher and SD-card tools | `simulator/` | Simulator: "Free for the KryonOS team. Other use by consultation. Contact via the repository." | Project code. Built against GTK3 and SDL2 through pkg-config (system libraries, not redistributed; their licences to confirm). |
| Server BLE bridge | `server/ble-bridge/` | Owner's own code, no restrictions | Project code. Links dynamically to libsystemd (sd-bus), LGPL-2.1, from the system (not redistributed). |
| KryonOS v3.0.0 prebuilt images | `simulator/emu/firmware/kryonos-v3/` | GPL-3.0 (upstream release) | Release assets of KryonOS v3.0.0, see `simulator/emu/firmware/manifest.json`. Release binaries are not built from source here. |
| `simulator/emu/firmware/es3c28p/` | `simulator/emu/firmware/es3c28p/` | Owner's own code, no restrictions | Local build of this repository's own firmware (`es3c28p_emu`). Contains KryonOS code (GPL-3.0, see the KryonOS row). |
| esp-emu 1.1.0 (Espressif emulator) | `simulator/esp-emu-1.1.0-x86_64-unknown-linux-gnu/` (not tracked in git) | To confirm | Binary, not redistributed; the user installs it locally. See `simulator/README.md`. |
| Bumble (BLE controller, Python, `pip install bumble`) | `simulator/tests/ble_hci_check.sh` | To confirm | Used only by the local BLE test, not redistributed. |
| PlatformIO libraries (`TFT_eSPI`, `TJpg_Decoder`, `PNGdec`, `TinyGPSPlus`, `ArduinoJson`, `ESPAsyncWebServer`, `WebSockets`) | `firmware/platformio.ini` (`lib_deps`) | To confirm | Fetched at build time, not in the repository. |

Libraries linked on the PC side (`server/`) are listed in `server/pom.xml`; their licences are not checked here (to confirm).
