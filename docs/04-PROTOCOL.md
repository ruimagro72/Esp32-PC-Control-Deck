# Protocol 4

---

# Protocol 4: the link between the Deck and the server

Binary frames, the only protocol between a Deck and the server, on every transport. This
file holds the frame layout, the message types, what each message means, and how each
transport (USB, BLE, Wi-Fi, UDP discovery) carries them. Protocol 3 (JSON lines `DD:{...}`)
is gone; its document is in the git history (`docs/04-PROTOCOL.md` before 4 Nov 2026).

**Decided 4 Nov 2026:** JSON is gone from the link between the Deck and the
server, on every transport (USB, BLE, Wi-Fi, the UDP discovery). No bridge, no negotiation,
no compatibility with protocol 3: a Deck and a server talk protocol 4 or not at all, so both
boards are flashed with the server that knows it. Every message has a fixed binary layout,
like the header of a TCP or UDP packet. The firmware has no JSON library.

Not part of this: the server's local REST API (`03-SERVER.md`: administration, `curl`, the
OpenDeck plugin), which is not the Deck's link.

## The frame

Every message is one frame. Decoded, a frame is:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `0x40`: protocol 4 in the high nibble, flags in the low nibble (none yet) |
| 1 | 1 | `0x00`: reserved, always 0 |
| 2 | 1 | type (the tables below) |
| 3 | 1 | channel (0 = control; streams later: audio) |
| 4 | 2 | sequence, per sender (loss detection) |
| 6 | 2 | payload length (at most 2048) |
| 8 | n | payload |
| 8+n | 2 | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) of bytes 0..8+n-1 |

Numbers are little endian. A frame whose version, length or CRC is wrong is dropped and
counted (Connectivity's "damaged").

**On the serial line** (USB, and BLE, which carries the same bytes): each frame is COBS-encoded
and ended by one `0x00`. COBS leaves no `0x00` inside, so a lost byte costs one frame. The
debug build also writes text lines starting with `#` on the same line; a reader tells them
apart by the first byte after the previous end: `#` starts a text line (up to `\n`), `0x00`
is skipped, anything else starts a frame. A `0x00` inside a text line ends it as damaged
(it was the tail of a broken frame).

**Over Wi-Fi**: `POST /api/deck` with the Deck's token (`Authorization: Bearer`), the body
`application/octet-stream`: the Deck's frames one after another, each COBS-encoded and
ended by `0x00`, as on the serial line. The answer is the server's frames, the same way.
A text client (the Deck's JavaScript, `decknet.js`) sends the same bytes base64-encoded as
`text/plain`; the server answers in base64 text too (`RequestGuard.java`, `Router.java`).

**Discovery** (UDP broadcast, port 8080): one frame, not COBS-encoded (a datagram has its own
end): `discover` from the Deck, `host_here` from each server.

## Field types

| Name | Bytes |
| --- | --- |
| `u8`, `u16`, `u32` | 1, 2, 4, little endian |
| `f32` | IEEE-754 single, little endian; **NaN = not measured** (protocol 3's `null`) |
| `str` | `u8` length, then that many bytes of UTF-8 (at most 255) |
| `str16` | `u16` length, then the bytes (long texts: the help) |
| `[n] x` | `n` items of `x`, after a `u8` count unless said otherwise |

Implemented by `app/deck_frame.h` (`deck::frame`, `deck::wire`) and `Frame4` / `Wire` on
the server, checked against the same vectors on both sides.

## Deck to server (types 0x01-0x3F)

| Type | Name | Payload |
| --- | --- | --- |
| 0x01 | `hello` | `u8` protocol (4), `str` Deck id (12 hex), `str` firmware version, `str` board id |
| 0x02 | `pong` | (none): the answer to `ping` |
| 0x03 | `board` | the Deck's description, below |
| 0x04 | `stats_request` | `str` Deck id (Wi-Fi: the server answers with `stats` and the key images queued for it) |
| 0x05 | `config_request` | (none) |
| 0x06 | `macro` | `u8` key (1-6), `str` Deck id |
| 0x07 | `history_request` | `str` metric id, `u8` minutes, `u16` points |
| 0x08 | `inventory_request` | `u8` component (0 cpu, 1 ram, 2 gpu, 3 network, 4 disk) |
| 0x09 | `transport` | `u8` the link the Deck moved to (0 USB, 1 Wi-Fi, 2 BLE) |
| 0x0A | `wifi_token_request` | `str` Deck id (USB only) |
| 0x0B | `firmware_check` | `str` board id, `str` firmware version |
| 0x0C | `catalog_request` | `u16` from |
| 0x0D | `texts_request` | `u16` from, `str` Deck id |
| 0x0E | `rebooting` | (none) |
| 0x0F | `discover` | (none; UDP) |
| 0x10 | `messages_request` | `str` Deck id, `u32` newest message id the Deck has |
| 0x11 | `message_read` | `str` Deck id, `u32` message id |
| 0x12 | `transfer_request` | `u8` kind, `str` Deck id, `str` what (kind 1: the board id) — the Deck asks the server to send it an object (below) |
| 0x13 | `transfer_ack` | `u16` transfer id, `u32` the next offset the Deck wants (all before it arrived and was kept) |
| 0x14 | `transfer_end` | `u16` transfer id, `u8` status (0 kept and checked, 1 check failed, 2 no room, 3 cancelled) |
| 0x15 | `upload_begin` | `u16` upload id (the Deck's), `u8` kind (2: a voice message), `u32` size, `str` Deck id, `str` to (`""` the people at the PC, a Deck id, `all`) |
| 0x16 | `upload_data` | `u16` upload id, `str` Deck id, `u32` offset, the bytes to the end of the payload (1024 at most) |
| 0x17 | `console_output` | `str` Deck id, `u16` request id (0: a live log line), `u8` flags (bit 0: the last part of the answer), `str16` text: the console's answer (`02-FIRMWARE-KRYONOS.md`) |
| 0x3F | `error` | `u8` code, `str` detail |

`hello`'s firmware version is the image's own string: on USB the kernel sends `DeckOta::version()`
(from `DECKFW:` in the image, `kryon-0.8-<build>`). The Deck app's Wi-Fi hello sends its own constant
`0.8.0` (`deck/main.js`). The `0.7.0` in `server/pom.xml` is the Java host's version: a different thing.

`board`: `str` Deck id, `str` board name, `str` firmware, `str` chip, `u8` chip revision,
`u8` cores, `u16` MHz, `u32` flash bytes, `u32` flash the image was built for, `u32` PSRAM
bytes, `u8` flags (bit 0 PSRAM in the package, 1 RGB LED, 2 Wi-Fi, 3 BLE, 4 Bluetooth
Classic, 5 battery input), `u16` screen width, `u16` screen height, `str` display, `str`
touch, `str` SD, `str` audio, `u16` battery mV (0 none), `u32` heap free, `u32` heap minimum,
`u32` heap total, `u16` NVS used, `u16` NVS free, `u16` NVS total, then the keypad (for
OpenDeck): `u8` rows, `u8` columns, `u16` key width, `u16` key height in pixels.

## Server to Deck (types 0x40-0x7F)

| Type | Name | Payload |
| --- | --- | --- |
| 0x40 | `host_hello` | `u8` protocol (4), `u16` stats interval in ms, `[n] u8` history windows (minutes), `u32` server time (Unix seconds) |
| 0x41 | `ping` | (none) |
| 0x42 | `stats` | below |
| 0x43 | `config` | `[n]` macros: `u8` key, `str` label |
| 0x44 | `inventory` | `u8` component, `u8` flags (bit 0: lines left out), `[n] str` lines, formatted by the server |
| 0x45 | `history` | `str` metric id, `str` unit, `u8` minutes, `u16` step in seconds, `u32` end (Unix seconds), `u16` n, `n` `f32` values (NaN: no measurement) |
| 0x46 | `key_image` | `u8` key position, `u8` flags (bit 0: clear the key), `u16` first row, then RGB565 pixels (2 bytes each, rows of the key's width) to the end |
| 0x47 | `key_more` | (none): more key images wait; the Deck asks again at once |
| 0x48 | `wifi_token` | `u8` available; if 1: `str` token, `u16` port, `[n] str` addresses; if 0: `str` reason |
| 0x49 | `recalibrate` | (none) |
| 0x4A | `firmware_offer` | `u8` available; if 1: `str` version, `u32` size, `str` MD5 (32 hex), `str` path |
| 0x4B | `catalog` | `u32` version, `u16` from, `u16` total, `[n]` entries: `str` id, `str` group, `str` name, `str` short name, `str` unit, `u8` kind (0 percent, 1 temperature, 2 rate, 3 frequency, 4 power, 5 size), `f32` max (NaN: follows the values), `u16` refresh seconds |
| 0x4C | `texts` | `u32` version, `u32` ids check, `str` language, `u16` from, `u16` total, `[n] str16` texts |
| 0x4D | `host_here` | `u16` port, `str` name (UDP) |
| 0x4E | `messages` | `[n]` messages: `u32` id, `u32` time (Unix seconds), `u8` kind (0 text, 1 voice), `u8` flags (bit 0 read), `str` from, `str16` text (voice: its length in seconds, as text, until audio travels) |
| 0x4F | `transfer_begin` | `u16` transfer id, `u8` kind, `u32` size, `str` MD5 (32 hex) |
| 0x50 | `transfer_data` | `u16` transfer id, `u32` offset, the bytes to the end of the payload (at most 1024) |
| 0x51 | `upload_ack` | `u16` upload id, `u32` the next offset the server wants (the answer to `upload_begin` and to each `upload_data`); when it equals the size the upload is done |
| 0x52 | `console_input` | `u16` request id (1-65535), `str` the line typed on the Deck's console (`02-FIRMWARE-KRYONOS.md`) |
| 0x7F | `error` | `u8` code, `str` detail |

`stats`: `u32` local time in seconds since midnight, `u8` day, `u8` month, then 19 `f32` in
this order (the old pages' fixed values, until they are built from the catalog): CPU %, CPU
°C, system °C, RAM %, RAM used GB, RAM total GB, GPU %, VRAM %, VRAM used GB, VRAM total GB,
GPU °C, GPU MHz, GPU W, disk used %, disk read MB/s, disk write MB/s, disk °C, network down
Mbit/s, network up Mbit/s; then `u32` catalog version and `[n] f32` the catalog's values in
its order (`03-SERVER.md`, "Metrics catalog").

Error codes: 1 unknown type, 2 invalid request, 3 internal error, 4 not allowed.

## Transfers: objects larger than a frame

One mechanism for every object that does not fit a frame, in both directions: the firmware
image for an update over USB or BLE (now), voice messages and their recordings, key images
and icons (later). Kinds: 1 firmware image, 2 voice message (16 kHz 16-bit mono PCM until a
codec is chosen), 3 image, 4 the Deck's pack (below).

**Server to Deck** (`transfer_request`: kind 1 with the board id, the image the server would
offer in `firmware_offer`; kind 2 with a voice message's id, its audio):

1. The server answers `transfer_begin` (a transfer id of its own, the size, the MD5) and the
   first blocks: `transfer_data` at offsets 0, 1024, ... up to the window (4 blocks).
2. The Deck keeps each block that comes at the offset it expects and answers `transfer_ack`
   with the next offset it wants; each ack brings the blocks after it, up to the window. A
   lost or damaged block is asked again by the ack, which still names it.
3. When the last block is kept, the Deck says `transfer_end` (0, or why not). A firmware
   image is checked against its MD5 and the Deck restarts into it; a voice message is played.

The window keeps the line busy without flooding it: on a serial line the acks pace the server;
over Wi-Fi (HTTP, request and answer) each ack carries the next blocks in its answer, though
the firmware image over Wi-Fi uses `GET /api/firmware/<board>` instead (faster). A server
that has nothing to send answers `error` (2, invalid request).

**Deck to server** (a voice message recorded on the Deck, kind 2):

1. The Deck sends `upload_begin` (its own upload id, the size, whom it is for); the server
   answers `upload_ack` with offset 0.
2. The Deck sends `upload_data` blocks of 1024 bytes, at most 4 ahead of the last ack. The
   server keeps a block only at the offset it expects and acks each with the next offset
   it wants: an ack that names the same offset again means a block was lost, and the Deck
   sends from there (once per offset). No ack for 1 s: the Deck sends again from the last
   ack; ten times in a row: it gives up.
3. The ack that equals the size ends it: the server keeps the message (`var/messages.json`,
   the audio in `var/voice/<id>.pcm`), for the PC when `to` is `""` (`GET /api/messages`,
   `GET /api/messages/<id>/audio` as WAV). The frames' CRC guards each block; there is no MD5.

Voice messages for the Decks are left on the PC with `POST /api/messages {"kind": "voice",
"file": "<a WAV on this PC: 16 kHz, 16-bit, mono>", "to": ..., "from": ...}`.

**The pack (kind 4): the screens and theme on the Deck's microSD card.** Once per connection
(3 s after the server's `host_hello`, and again when a card is put in) with its files checked,
then every minute with its version only (so a pack assigned on the server reaches a connected
Deck within a minute), a Deck with a card sends `transfer_request`
kind 4 with `what` = `"<board> <version it has>"`; the version is `""` when the card has no
pack, or when its files do not match the manifest (missing, damaged, a new or empty card).
The server builds the pack assigned to that Deck id (`server/var/packs/decks.properties`,
`<deck id>=<pack name>`, otherwise `default`; files in `var/packs/<name>/`, a board's own
files in `var/packs/<name>/@<board>/`; without `var/packs/default`, the repository's
`firmware/sd-card/deck`). Same version: `error` 2, nothing to send. Otherwise the transfer
above; the pack is text headers with each file's bytes after its header:

    DECKPACK 1
    version <12 hex: SHA-256 of the files>
    file <path under /deck/> <size>
    <size bytes>
    ...
    end

The Deck writes it to `/deck/pack.tmp`, unpacks it into `/deck/new/`, moves each file over its
place in `/deck/`, then writes `/deck/manifest` (each file's path, size and CRC-32) and
`/deck/version`. Paths: lower-case letters, digits, `.` `_` `-` `/`, no `..`, not `version`,
`manifest` or `new/`; at most 256 KB a file, 512 KB a pack. A Deck without a card, or whose
card is damaged, says so with `error` 3 ("no microSD card: the built-in pages", "microSD card
damaged (<file>): sent again", "microSD card new or empty: installing").

The packs over HTTP (local only): `GET /api/packs` lists the packs with their version per
board and the Decks given one; `PUT /api/decks/<id>/pack {"pack": "night"}` gives a Deck a
pack (`null` or `"default"`: the default again).

The serial line from the Deck carries frames and text lines (the debug log, the Arduino
core's `[  1046][E][...]` lines): a frame always starts with the byte 0x02, anything else
starts a text line. After a `pong` with no `hello` the server keeps pinging until the hello.

## Order of a connection

USB and BLE: the server sends `ping` until the Deck answers (`pong` or `hello`); on `hello`
it sends `host_hello`, `config`, the five `inventory`, `wifi_token` (USB), then `stats` every
interval. The Deck asks `texts_request` and `catalog_request` when it has not got them.
Wi-Fi: the Deck sends `hello` and `stats_request` in its requests; the answers carry the rest.

## What the messages mean

Kept from protocol 3: the layout changed, the meaning did not.

- **A value is measured or it is NaN** (`null` in the REST API). The server never estimates
  or substitutes another sensor; the Deck shows NaN as unavailable and as a gap in graphs,
  never as zero.
- **`stats`**: pushed every interval of `host_hello` (USB, BLE) or the answer to
  `stats_request` (Wi-Fi). The local time lets the Deck run its clock with `millis()`; the day
  and month are shown when the clock is tapped. Rates are differences between two counter
  readings; after a pause the server takes a 150 ms baseline first, and a counter that goes
  backwards gives NaN.
- **`config`**: the six key labels (the firmware wraps long ones). Only ids and labels travel;
  commands, paths and URLs stay on the server.
- **`macro`**: key 1 to 6. The server maps it to the action in its own settings
  (`03-SERVER.md`, "Macros").
- **`hello`**: the Deck id is the chip's MAC as 12 hex digits, the same after a power loss
  or a new firmware. The token is a credential and changes; the id does not.
- **`history_request`**: a metric id (`03-SERVER.md`), minutes exactly 5, 10, 15 or 20,
  points 10 to 120 (default 60). The answer's values are equal time buckets ending at `end`,
  oldest first; a bucket is the average of the samples in it, NaN when it has none (a gap).
  The server keeps `telemetry.history_minutes` (default 30, at least 20) in its database.
  **The Deck keeps no history**: not in flash, no sample log in RAM. A graph sends one request
  per line shown, holds only those series, and asks again every few seconds while it is open.
- **`inventory`**: static characteristics of a component (fields in `03-SERVER.md`),
  formatted as lines by the server. Over USB and BLE the server sends the five right after
  `host_hello`; over Wi-Fi the Deck asks once per component a few seconds after `host_hello`,
  never when a page opens. The Deck keeps them in RAM only.
- **`wifi_token`**: only on the cable, in the handshake or for `wifi_token_request` (sent by
  a Deck on Wi-Fi with no server, every 5 s, every 30 s once answered). It gives that Deck its
  **own** token (a new one at each USB connection; the server keeps only its SHA-256), the API
  port and the server's LAN addresses. The Deck keeps the address in its own subnet (else the
  first), the port and the token in flash, so after a power loss it reaches the server over
  Wi-Fi without the cable. The Deck accepts it only from the cable.
- **`firmware_check` / `firmware_offer`**: asked from Settings > Firmware. The server reads
  `var/firmware/<board>.bin`, its version from the text after `DECKFW:` inside the image;
  "not available" when there is none or it is the version running. The Deck fetches `path`
  over Wi-Fi with its token (`GET /api/firmware/{board}`) and checks size and MD5 while it
  writes the other app slot.
- **`texts_request` / `texts`**: the texts the Deck shows once it has a server
  (the planned UI from the server, `07-FEATURES-AND-ROADMAP.md`), asked at each connection and again
  while the table is incomplete. The texts come in the order of `app/text_ids.h`; the ids
  check is the CRC-32 of the ids' names, compared with the Deck's own; the language is the
  Deck's (its own setting on the server, or the one for all).
- **`catalog_request` / `catalog`**: the metrics catalog in pages, asked when the version in
  `stats` differs from the one the Deck has.
- **`discover` / `host_here`**: only on a private network; the address is where the answer
  came from. It gives no access: the token still comes over the cable.
- **`recalibrate`**: deletes the saved touch calibration and restarts the Deck.
- **`console_input` / `console_output`**: the Deck's console, a backoffice channel beside the
  normal messages (`02-FIRMWARE-KRYONOS.md`). The Deck takes `console_input` on the link in use and, while
  another link is in use, also on its USB cable; it answers on the link the line came from.
  Over Wi-Fi the line travels in the answer to the Deck's next request. A browser never
  talks to the Deck: the server's own API carries the lines.
- An error never echoes the offending input.

## BLE: a serial line over the air

Firmware 0.5.6 and later, Linux server only. Only **Bluetooth Low Energy** exists on the
ES3C28P (the ESP32-S3 has no Bluetooth Classic radio).

- The **Deck is the peripheral** (GATT server). It advertises only while BLE is its transport
  (USE BLE on the Bluetooth page), with the BLE library of the Arduino core 2.0.14
  (Bluedroid): no library added.
- The **server is the central**, through a small separate program, `server/ble-bridge/` (C, the
  BlueZ D-Bus API through sd-bus; Linux). It connects to every paired Deck and gives each one
  a pseudo terminal, which the Java server opens like a USB cable. No Bluetooth code in the
  JVM; Windows and macOS get their own small bridge later.

| Item | UUID | Properties | Direction |
| --- | --- | --- | --- |
| Control Deck service | `c6d2fba6-17ec-4591-a399-4982dacbaf6d` | | |
| `deck_in` | `200a64c8-1a2b-4927-9c15-3783198616b4` | Write, Write Without Response | server to Deck |
| `deck_out` | `d444ff04-43f4-4f30-8fc8-4b38a8466870` | Notify | Deck to server |

The Deck advertises the service UUID (scan response: its name, `Control Deck XXXX`, the last
four hex digits of its id). Both characteristics carry a **byte stream**: exactly the bytes
of the serial line, split wherever the ATT payload ends (MTU − 3 bytes, 20 at the default
MTU, up to 244 after negotiation). ATT keeps the order, so the receiver just appends. The
server gives a BLE Deck the same session as a USB one. The Deck sends `transport` on its USB
line when it switches to BLE, as it does for Wi-Fi.

Security:

- LE Secure Connections with bonding and MITM protection. The Deck is **display only**: it
  shows a 6-digit pairing code, typed once on the PC (the system's Bluetooth settings, or
  `bluetoothctl`). Both characteristics, and the notification switch, need an encrypted,
  authenticated link: an unpaired central can see the Deck but not talk to it.
- The bridge only connects to Decks paired with the PC. No token: the bond is the access.
  The Deck keeps a label and two settings (preferred, disabled) per PC address
  (`app/deck_ble_peers.h`, NVS key `blepeers`); FORGET THIS PC removes that bond, DISABLE
  keeps it but drops the PC each time it connects.
- Bond keys stay in the Bluetooth stacks' own storage (NVS on the Deck, BlueZ on the PC) and
  never appear in logs.

Files on the PC (Linux): `$XDG_RUNTIME_DIR/control-deck-ble/<name>`, one per Deck that has a
pseudo terminal, with `<pid> <pty path> <description>`. Written by `ble-bridge` (and by the
PC build's simulated BLE, `DECK_NATIVE_BLE=sim`); the Java server opens each one whose process
is alive with a serial session of its own, reported as the `ble` transport.

## State of each transport

`GET /api/transports` reports `usb`, `wifi`, `ble` and `bluetooth_classic`, each with
`implemented`, `enabled`, `state` (`disabled`, `stopped`, `connecting`, `connected`, `stale`,
`error`, `listening`, `misconfigured`, `waiting`, `unsupported`) and the last error. `ble`
lists each BLE Deck under `decks`; it is `connected` when one is. A Wi-Fi Deck seen in the last
15 seconds makes `wifi` `connected`. Bluetooth Classic is `unsupported` on the ESP32-S3.


