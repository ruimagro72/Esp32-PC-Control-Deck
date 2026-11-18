# ble-bridge

Connects the PC to every Control Deck that is paired with it over Bluetooth LE, and gives
each Deck a pseudo terminal (pty). The Control Deck host (Java) opens that pty like a USB
serial cable, so a BLE Deck gets the same session as a USB one. The host has no Bluetooth
code of its own; it starts this program when it is built.

The bytes on the pty are exactly the bytes of the USB serial line (`DD:<json>\n` lines). Over
the air they are split wherever the ATT payload ends. The GATT service and the security rules
are in [docs/04-PROTOCOL.md](../../docs/04-PROTOCOL.md).

Linux only, through BlueZ's D-Bus API (sd-bus from libsystemd). Windows and macOS will get
their own bridge later.

## Build

Needs a C compiler, `make`, `pkg-config` and the libsystemd headers (Debian/Ubuntu:
`libsystemd-dev`).

```
make
```

builds `build/ble-bridge`. `make clean` removes it.

## Run

```
./build/ble-bridge
```

The Java host starts it by itself when `build/ble-bridge` exists. It runs until SIGINT or
SIGTERM, then closes every link, disconnects the Decks it connected and removes its files.
Each event is one line on stdout (the host adds the time and a `[ble]` prefix), for example:

```
Control Deck 1A2B (AA:BB:CC:DD:EE:FF): paired Deck found
Control Deck 1A2B (AA:BB:CC:DD:EE:FF): connected, pty /dev/pts/7, mtu 244 (acquired)
Control Deck 1A2B (AA:BB:CC:DD:EE:FF): the host opened /dev/pts/7
```

`mtu` is the largest write to the Deck (ATT MTU minus 3). `acquired` means both
characteristics are sockets (`AcquireWrite`, `AcquireNotify`); otherwise the line says which
fallback is used (`WriteValue`, `StartNotify`).

A Deck only advertises while BLE is its transport, so failing to connect is normal: the bridge
tries again every 5 s and says each distinct problem once. If the system bus or BlueZ is not
there, it says so once and tries again every 10 s.

## Files

One file per linked Deck in `$XDG_RUNTIME_DIR/control-deck-ble/` (`/tmp/control-deck-ble/`
without `XDG_RUNTIME_DIR`), named after the Deck's alias with spaces and slashes turned into
`_`. It holds one line:

```
<pid> <pty path> <alias> (BLE <address>)
```

The PC build's simulated BLE writes the same format. The file is removed when the link ends.
If the name is taken by another live process, the address is added to the name
(`Control_Deck_1A2B_AABBCCDDEEFF`).

## Pairing

The bridge never pairs: it only uses devices that BlueZ lists as paired and that offer the
Control Deck service. Pair each Deck once, with USE BLE on its Bluetooth page:

- KDE: System Settings, Bluetooth, add a device, pick `Control Deck XXXX`, type the 6-digit
  code shown on the Deck. Other desktops have the same in their Bluetooth settings.
- Or `bluetoothctl`: `scan le`, wait for `Control Deck XXXX`, `pair <address>`, type the code
  shown on the Deck.

FORGET on the Deck's Bluetooth page removes its bonds; remove the device on the PC too.

## Licence

MIT, like the rest of the project. It links dynamically to libsystemd (sd-bus), which is
LGPL-2.1.
