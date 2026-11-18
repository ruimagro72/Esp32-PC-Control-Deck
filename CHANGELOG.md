# Changelog

Newest first.

## 0.7.0 (Nov 2026)

- Version 0.7.0, protocol 4, Java 21 build (`server/pom.xml`).
- Self-tests: `--selftest` runs without hardware.
- BLE bridge (`server/ble-bridge`) builds with `make`; the host starts it unless `--no-ble`.
- Deck flow checked over Wi-Fi against the simulator: 124 `POST /api/deck` requests answered 200 (18 Nov 2026).

## Known limits

- Three GPU telemetry self-tests failed in the 18 Nov 2026 run with JDK 25 (`Passed 98, failed 3`).
