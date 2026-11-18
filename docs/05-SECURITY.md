# Security

---

# Security

## Local-first design

ESP32 PC Control Deck is intentionally local-first:

- USB serial is the default transport and needs no network,
- Wi-Fi is optional, off by default, and the API listens on `127.0.0.1` unless you change it,
- BLE is optional; only PCs paired with the Deck (a code shown on its screen) can talk to it, and the bond is the access (`04-PROTOCOL.md`, "BLE"),
- no cloud service is required by the host,
- the Deck stores its Wi-Fi passwords and the host token in its flash (NVS), which is not encrypted unless flash encryption is enabled (see `docs/02-FIRMWARE-KRYONOS.md` in the `kryonos` branch).

## Action execution

The ESP32 sends only a numeric macro ID from 1 to 6. It does not send a command path or arbitrary shell text.

The host bridge maps the ID to an action in the host's local settings (`server/var/control-deck.db`). Those settings can open URLs, execute commands and start services (typing text and hotkeys are planned).

Treat these settings as executable local configuration:

- add only commands you trust,
- verify paths before enabling autostart,
- do not commit private local paths or secrets unless intentionally public,
- do not use configuration files from unknown sources,
- keep `server/var/` out of Git (it is in `.gitignore`); the host creates its files there with mode 600.

## Network access (Wi-Fi transport)

The HTTP API has three access classes, **public** (`GET /` only), **peer** (the local computer, or a LAN address with `Authorization: Bearer <token>`) and **local** (loopback only, even with a token, because macros are executable configuration). Which route is in which class: [03-SERVER.md](./03-SERVER.md#access-rules).

Rules that matter:

- Binding the API to a LAN address does not make it reachable by itself. Remote peers are refused until the Wi-Fi transport is enabled **and** a token exists.
- Only the SHA-256 of the token is stored. The token is shown once when it is created (`./start.sh server setup` or `POST /api/wifi/token` from the host computer). Rotating it invalidates the old one.
- Repeated failed attempts from one address are throttled (HTTP 429). Unauthenticated remote clients cannot enumerate routes.
- A LAN peer can read telemetry, history and inventory and can press macro ids. It cannot read or change macro commands or any configuration.
- Request bodies are limited to 16 KB; metric and component names are validated against fixed lists; error messages do not echo input.
- The host does not write requests to its log, so tokens and passwords never reach it.
- Requests made by web pages are refused: the `Host` header must be an IP address or `localhost` (no DNS rebinding), a request whose `Origin` / `Sec-Fetch-Site` shows another site is refused, and a request body must be declared as `application/json`.
- The host's HTTP server has no TLS: the token and the telemetry cross the network in clear text. Use it only on a network you trust, and restrict the port with a firewall. A TLS reverse proxy is the right answer for anything more.
- Wi-Fi passwords stay on the Deck, never in this API. The host stores and serves none.

## Local HTTP checks

The host makes read-only HTTP requests only where the settings say so:

- a macro's `health_url` (for example Ollama's `http://127.0.0.1:11434/api/tags`), checked when the macro is pressed,
- the settings also keep a LibreHardwareMonitor address (`http://127.0.0.1:8085/data.json`) for a future Windows telemetry source; the Linux host does not use it.

Do not change these to untrusted remote endpoints without understanding the privacy and network implications.

## Reporting a vulnerability

Use GitHub private vulnerability reporting for security issues. Avoid publishing exploit details in a public issue before a fix is available.

## A firewall on the host's computer

A Deck is another device on the network, so a firewall that drops what comes in (for example
OpenSnitch's System firewall with `policy drop` on input) must let it through: **TCP 8080** for the
API and **UDP 8080** for a Deck looking for the host, from the local network only. A Deck on the same
computer needs neither. Answering `discover` tells the local network the computer's name and that the
host runs there; it gives no token.


---

# Security review (13 Nov 2026)

The Deck controls the PC (triggers configured macros, and a VNC view with keyboard and mouse of
a virtual monitor). So the Deck, and the link to the server, are a path into the PC and its
network. This review ranks the ways that path could be abused, worst first, from reading the
code. Nothing here is a known break-in; these are the holes to close "while the cucumber is
small". Re-do it whenever the link or the PC-side capabilities change.

Threat model: an attacker on the same LAN as the PC (home Wi-Fi, or a company network where the
PC and the Deck sit), without physical access to the PC or the paired Deck. USB (physical) and a
BLE link paired with the passkey are treated as trusted; Wi-Fi is not.

## HIGH

### H1. Wi-Fi transport is plain HTTP; the token and everything else travel in clear
`POST /api/deck` is plain HTTP with `Authorization: Bearer <token>` (server
http/Router.java, firmware Kernel/DeckLink.cpp wifiTask). A passive listener on the LAN captures
the token, then can act as the Deck: trigger **every configured macro** (open a terminal, lock
the PC, run a configured command) and read the stats. The VNC password (H2) also travels this
way. On a trusted home LAN the exposure is small; on a shared or company network it is a real
back door to the PC.
- Fix: TLS on the Wi-Fi transport (the server serves HTTPS; the ESP32 has mbedTLS, already used
  by the OTA/AI code). Until then: per-Deck tokens (already supported), and document that the
  Wi-Fi link must be on a trusted network. Bearer-over-HTTP is acceptable only on loopback.

### H2. The virtual-monitor VNC listens on every interface and its password crosses the clear link
`VirtualMonitor` runs krfb (5910) and the WS bridge (5911) bound to `0.0.0.0`; the random 8-char
password is sent to the Deck over the plain Wi-Fi link (H1). A LAN attacker who reads the
password connects to `ws://PC:5911` and gets the PC's screen, keyboard and mouse: full control.
- Fix: bind the WS bridge to the Deck's address (or localhost) rather than `0.0.0.0`; require the
  same Bearer token on the WebSocket upgrade; send the VNC password only over an encrypted link
  (H1); stop the monitor as soon as the Deck leaves (the DELETE path exists, make it automatic).

### H3. The native OTA does not validate the server certificate (MITM = malicious firmware)
`TLSHelper::configureTLS` calls `client.setInsecure()`, and the OTA uses it for both the manifest
fetch and the firmware download (OTAManager.cpp:89, 230). The TLS connection to GitHub is not
authenticated: anyone who can intercept it (DNS spoofing, a rogue access point, a compromised
router) can serve a matching manifest and arbitrary firmware, which the device flashes. This is
remote code execution on the Deck. It is also the only theoretical way to "redirect" the stock
OTA to another server, which is exactly why it must be closed, not used.
- Fix: validate the certificate (pin the CA or the server cert) for the firmware download, and/or
  require a signed firmware image checked on-device before `Update.begin` (M2). The
  OTA-from-our-server work should authenticate the server and verify a signature, over the
  authenticated DeckLink, never `setInsecure()`.

## MEDIUM

### M1. A card app runs with network and persistence powers, and app packs are not signed
An app on the SD card can `System.setShell(...)` (persist as the Deck's main screen),
`Network.post` / `Network.downloadFile` / open a `WebSocket` or an `HttpServer`. A malicious app
dropped on the card (or delivered by a future App Store that is not authenticated to our server)
could phone home or open a port. The per-app storage sandbox limits files, which helps.
- Fix: sign app packs and verify on install; confirm `System.setShell` with the user; keep the
  storage-permission model; authenticate App Store delivery to our own server.

### M2. OTA must verify a signature, not only a hash (when added)
There is no JS-to-flash path today (good: a card app cannot reflash the device, see "Confirmed
good"). When OTA-from-our-server is added, a hash alone is not enough: a MITM on the update
channel could serve a matching hash. The image must be signed and the signature checked on the
device before `Update.begin`, and the download must be over the authenticated (ideally TLS) link.

### M3. Token has no expiry or per-Deck binding beyond the hash list
Tokens are bearer secrets stored as SHA-256; rotating invalidates all. There is no expiry and no
tie to a Deck's identity (MAC) at use time. Enough for home use; for more, bind a token to a Deck
id and add expiry/rotation.

## LOW / to watch

- The auth throttle is per-address and file-based (10 failures / 60 s): a distributed source, or
  a lost failures file, weakens it. Fine for a LAN.
- `writeReg`/`analogWrite`/`digitalWrite` from JS drive the board's own pins (hardware, not the
  PC): a bad app can misuse the board, not the network.

## Confirmed good (keep these)

- **Macros are key-only:** the Deck sends an id 1..N, the server runs the macro the owner
  configured; the Deck cannot supply a command (MessageHandler.macroResult).
- **Console is one-way:** `console_input` flows server to Deck; a peer cannot inject console
  commands through `/api/deck` (it only accepts the Deck's own message types).
- **No JS-to-flash path, and no local-file flash path:** no OTA/Update/flash binding is exposed
  to JavaScript; the native OTA streams only from a URL (the author's GitHub manifest); there is
  no boot-time "flash /sd/firmware.bin" path (verified 13 Nov: no esp_ota_write outside the HTTP
  path, no SD firmware read). So a script cannot download firmware and reflash the device, and
  pulling the card cannot plant firmware. This is a deliberate wall: the ONLY way to change the
  code is a cabled flash (or the authenticated, signed OTA to be built in M2). Keep it that way;
  do NOT add a raw flashing binding to JS or an unsigned local-file update path.
- **Tokens:** stored as SHA-256, compared with `MessageDigest.isEqual` (constant time), every
  hash checked with no early exit (PeerAuth).
- **Loopback is the local user; remote peers need a token; the Wi-Fi transport can be disabled.**
- **BLE link:** MITM pairing with a passkey shown on the Deck, and the link is encrypted (after
  the 13 Nov cipher fix). The strongest link.
- **Filesystem:** per-app sandbox, on-demand storage permission, `/system` folders blocked.

## Suggested order to fix

1. H2 quick part: bind the VNC bridge to the Deck, require the token on the WS upgrade, auto-stop.
2. H1: TLS on the Wi-Fi transport (bigger; needs the server cert and the ESP32 client).
3. M2 when OTA-from-server is built; M1 when the App Store is built.


