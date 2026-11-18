# Simulator Launcher Profiles

Profiles define launcher and emulator configuration in JSON (`schema_version: simulator.profile/v1`).

## Files
- `simulator/launcher/profile.schema.json`: JSON Schema (draft 2020-12) for validation and editor tooling.
- `simulator/launcher/profiles/example-es3c28p.json`: reference profile for the ES3C28P board.

## Usage
- Use `simulator/launcher/profiles/example-es3c28p.json` as a template; prefer repo-relative paths.
- Validate with any draft 2020-12 JSON Schema validator (e.g. `ajv`, `python-jsonschema` with 2020-12 support).
- Keep `$schema` in each profile file pointing to `../profile.schema.json` (relative to the profile) for local validation.

## Migration from `launcher.conf`
Map legacy keys to profile fields:
- `board` -> `metadata.board_preset`, `board_hardware.board_id`
- `firmware` -> `firmware.selection`
- `merged_image_path` -> `firmware.artifacts.merged`
- `use_elf`, `elf_path` -> `firmware.artifacts.use_elf`, `firmware.artifacts.elf`
- `scale`, `hw_rotation` -> `ui_state.scale`, `board_hardware.display.overrides.hw_rotation` (and/or `display.orientation` if applicable)
- `uart_target` -> `emu_runtime.instance` (ports are no longer stored: cosim 5591+10N, UART 5555+10N, GDB 3333+10N; old `uart_tcp` keys are ignored)
- `extra_args` -> `emu_runtime.extra_args`
- `net_user`, `audio` -> `network_radio.net_user`, `audio.enabled`
- `custom_firmware_path` -> `firmware.artifacts.merged` (or appropriate artifact) with `firmware.selection: "custom"`

Add metadata/timestamps and any hardware/runtime details not present in `launcher.conf` as needed.
