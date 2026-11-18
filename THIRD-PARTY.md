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
| Flappy Bird sound effects (credited to the original game) | `simulator/SD_Card/system/SystemApps/categories/Games/flappy-bird/` | To confirm | The app's own About text credits .GEARS Studios / Dong Nguyen. Terms of use not stated. Consider removing before a public export. |
| Menu track "The Mountain - Retro Game" | Named in the flappy-bird app (`main.js`); audio file not checked | To confirm | The app calls it royalty-free. Source and terms not in the repository. |
| freetouse.com MP3 files (7) | `simulator/SD_Card/UserData/Multimedia/Audio/MP3/` | To confirm | Terms not in the repository. Exclude from any public export unless the terms allow redistribution. |
