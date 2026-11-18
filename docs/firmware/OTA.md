# Firmware over the air from the Deck's own server

One cabled flash installs this; after it the Deck updates itself over any supported link (USB, BLE or Wi-Fi: frames go through DeckLink). No internet update server: the Deck's own server.

## How an update goes

1. `./start.sh publish` builds the firmware (stamped with the build time), signs it
   and puts it in `server/var/firmware/<board>.bin`. Signing: ECDSA P-256 with SHA-256, the private
   key in `server/var/keys/ota_signing.pk8` (mode 600, never in git, not for the card or the Deck).
   Make the key once: `java -cp server/target/control-deck-host-*-all.jar
   dev.controldeck.host.firmware.FirmwareSigner keygen server/var/keys/ota_signing.pk8
   firmware/src/Kernel/OtaPublicKey.h`, then flash a firmware that holds that public key.
2. On the Deck (console, also remote through the server shell, or `DeckLink.otaCheck()` in JS):
   `ota check` asks the server (`firmware_check`); the server offers a signed image whose version
   differs from the running one (`firmware_offer`). `ota status` shows the offer.
3. `ota apply` starts the download (`transfer_request` kind 1; 1 KB blocks, window of 4, each block
   acknowledged). The image is written to the idle app slot as it arrives; the last 80 bytes (the
   signature trailer) are held back. The running slot is never touched.
4. At the end the Deck checks the signature against the public key built into its own firmware
   (`OtaPublicKey.h`). Only if it is valid does it commit the slot (`Update.end`), tell the server
   (`transfer_end` 0) and restart. A bad or missing signature aborts and nothing changes.

## Signed image format

`image bytes` + 80-byte trailer: `DECKSIG1` (8), signature length (1), DER signature (at most 72),
zero padding. The signature covers the image bytes only. The server's FirmwareStore offers only
images with a well-formed trailer; the Deck is the real verifier.

## Security properties (see docs/05-SECURITY.md, H3 and M2)

- No JavaScript call and no file on the card can write the flash; the only path is these frames,
  from a greeted server, with an image signed by the key that stays on the server.
- A man in the middle (even on plain-HTTP Wi-Fi) cannot make the Deck flash his own image:
  the signature will not verify. This does not replace TLS for the token (H1).
- Refused when the battery is under 30 % and no charger is connected.
- Known limits: a replay of an OLD signed image is accepted if the server offers it (the server is
  trusted; version ordering is not checked on the Deck); there is no automatic rollback: the
  Arduino-ESP32 bootloader does not roll back a slot that boots but misbehaves, so a broken
  (but correctly signed) image needs the cable. Test an image on the board before publishing it.

## Status

Built and the server side tested on the PC (signed image published, offered through
/api/deck). The Deck side (kernel/DeckOta.cpp) is built but NOT flashed: the first install is the
last cabled flash. Then test: `ota check`, `ota apply`, watch `ota status`, the Deck restarts into the
new stamp (`info`/hello shows the version).

## Test on the board (13 Nov 2026, evening) PASSED

After the cabled flash (build 20261113-185924): over Wi-Fi, with no serial cable on the server, `ota check`
returned the offer kryon-0.8-20261113-185512 (2 912 288 bytes); `ota apply` downloaded it (about 1 minute),
the signature verified, the Deck restarted by itself, and `ota check` then answered "up to date". During
the download the console does not answer (the single Wi-Fi link is busy with blocks). The cable stayed
connected as a safety net. Next: the same over BLE (after re-pairing with `DeckLink.bleForget()`), and a
failure test (a tampered image must be refused and leave the running firmware untouched).
