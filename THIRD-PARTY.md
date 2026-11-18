# Third-party components and licences (simulator branch)

The licence of the simulator code is stated in `SIMULATOR-LICENSE.md`. This table covers only the third-party components.
"To confirm" means the licence is not stated in the repository and has not been checked against the upstream source.

| Component | Where in this branch | Licence | Source and notes |
|---|---|---|---|
| GTK 3 | `simulator/launcher/` (build, system library through pkg-config) | To confirm (LGPL expected) | Not redistributed. |
| SDL 2 | `simulator/emu/sim_host.c`, `simulator/launcher/` (build, system library through pkg-config) | To confirm | Not redistributed. |
| esp-emu 1.1.0 (Espressif) | `simulator/esp-emu-1.1.0-x86_64-unknown-linux-gnu/` (not in this branch, git-ignored) | To confirm | Binary, not redistributed; installed locally by the user. |
| esptool (merge step) | Called by `start.sh emu merge`, which is not in this branch | To confirm | Not redistributed. |
| Bumble (BLE controller, Python) | `simulator/tests/ble_hci_check.sh` | To confirm | Used only by the local BLE check; not redistributed. |
| KryonOS v3.0.0 prebuilt images | Listed in `simulator/emu/firmware/manifest.json`; not in this branch | GPL-3.0 (upstream release) | Fetched from the upstream release, not redistributed here. |
| Flappy Bird sound effects (5 WAV, credited to the original game) | `simulator/SD_Card/system/SystemApps/categories/Games/flappy-bird/assets/` | To confirm | Not included; see `docs/simulator/TEST-MEDIA.md`. The app's About text credits .GEARS Studios / Dong Nguyen. |
| Menu track "The Mountain - Retro Game" (WAV) | `simulator/SD_Card/system/SystemApps/categories/Games/flappy-bird/assets/` | Not stated (app calls it royalty-free) | Not included; see `docs/simulator/TEST-MEDIA.md`. |
| freetouse.com MP3 files (5) | `simulator/SD_Card/UserData/Multimedia/Audio/MP3/` | freetouse.com terms, to confirm | Not included; see `docs/simulator/TEST-MEDIA.md`. |
| Tracker modules (5: .mod, .it) | `simulator/SD_Card/UserData/Multimedia/Audio/Modules/` | Not stated | Not included; see `docs/simulator/TEST-MEDIA.md`. |
