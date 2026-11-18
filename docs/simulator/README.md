# Simulator documentation

How to run the simulator: `simulator/README.md`. The launcher README stays next to its code: [launcher](../../simulator/launcher/README.md).

## Design and reference

- [SIMULATOR_CAPABILITIES_MATRIX.md](./SIMULATOR_CAPABILITIES_MATRIX.md): board by peripheral by capability status matrix.
- [SIMULATOR_PROFILE_SPEC.md](./SIMULATOR_PROFILE_SPEC.md): simulator profile format and the start.sh workflow it drives.
- [SIMULATOR_UI_DESIGN.md](./SIMULATOR_UI_DESIGN.md): launcher UI design (factual, tied to the matrix).

## emu/ (esp-emu, the board and its firmware)

- [emu/HARDWARE-SUPPORT.md](./emu/HARDWARE-SUPPORT.md): support per peripheral for the es3c28p board, with commands and limits. Current reference.
- [emu/firmware/README.md](./emu/firmware/README.md): the firmware images under `simulator/emu/firmware/`.
- [emu/storage/README.md](./emu/storage/README.md): storage strategy: SD image (`--sd`), LittleFS partition.
- [emu/NOTES.md](./emu/NOTES.md): historical lab notes (16 Nov 2026, esp-emu 0.51.0 and an earlier build). Superseded.
- [emu/EMULATOR-NOTES.md](./emu/EMULATOR-NOTES.md): historical analysis and decisions (earlier esp-emu build, Portuguese). Superseded where it conflicts with HARDWARE-SUPPORT.md.

## parts/

- [parts/README.md](./parts/README.md): virtual parts (esp-part WASM devices) and the cosim wiring model. No `.wasm` parts shipped.

## launcher/

- [LAUNCHER-HELP.md](./LAUNCHER-HELP.md): user help for the GTK launcher: window layout, Start/Stop/card, profiles, network, validation messages, shortcuts, known limits.
- [LAUNCHER-TOOLTIPS.md](./LAUNCHER-TOOLTIPS.md): proposed tooltip text for every launcher control and candidate Help menu entries. Not wired yet.
- [launcher/INTEGRATION_PLAN.md](./launcher/INTEGRATION_PLAN.md): plan to merge the launcher and the SDL host into one window. Partly done (embedded LCD canvas).
- [launcher/boards/README.md](./launcher/boards/README.md): board catalogue JSON format and support states.
- [launcher/profiles/README.md](./launcher/profiles/README.md): profile JSON (`simulator.profile/v1`) usage and the migration from `launcher.conf`.
