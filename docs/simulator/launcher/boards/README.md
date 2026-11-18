# Board catalogue (launcher)

Editable board and profile metadata for `simulator/launcher`. Sources: `SIMULATOR_CAPABILITIES_MATRIX.md`,
`SIMULATOR_PROFILE_SPEC.md`, `../../emu/HARDWARE-SUPPORT.md`, `../../emu/KRYONOS-V3.0.0-PREP.md`.

- One JSON per board in `simulator/launcher/boards/<id>.json`. Known fields are factual. Uncertain or runtime-dependent items carry a
  `note` or `support_*` flag, or the value `"unknown"`.
- C API: `simulator/launcher/board_catalog.h` and `.c` (list, get, load JSON overrides, firmware, cosim and LCD helpers).
  The defaults compiled in match these JSON files. JSON files override editable metadata without a rebuild.
- Support states: `supported-now`, `needs-runtime-host`, `blocked-by-emu`, `conditional(version/tool)`.
