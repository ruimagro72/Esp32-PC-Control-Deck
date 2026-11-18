# Changelog

Newest first.

## Unreleased (17-18 Nov 2026)

- esp-emu 1.1.0 runs the real firmware image, with an SDL LCD and touch host and a GTK3 launcher.
- Touch press kept until the firmware reads it 3 times; LCD drawn at a whole-number scale.
- Launcher: dark theme, canvas first, English source texts with Portuguese translation, Eject restarts with `--no-sd`.
- C++17 objects with unit tests (`make -C simulator test`).
- Wi-Fi: the Deck flow was checked against the Java host over the emulated test access point.

## Known limitations

- BLE scan not confirmed in the simulator; `esp-emu` logs `(C3)` on an S3.
- Audio capture in the simulator reads half the input; suspected emulator side, hardware not checked.
- `esp-emu` 1.1.0 does not model the LCD; the picture comes from the SDL cosim host.
- The console stops answering after a JavaScript exception (seen in the emulator, not on hardware).
