// /sd/system/autoexec.js - runs once at start, before the main screen (like autoexec.bat / rc).
// The kernel loads this path only (firmware/src/main.cpp, after the 2026-11-19 change). Needs a firmware flash to take effect.
// Edit and copy it to the card's system/ folder.

// ---- Environment variables (kernel ENV), defined at boot and available to every app ----
// Set here (autoexec.js runs once at start, before the main screen); read in apps with ENV.get("key").
// Control Deck server: empty on purpose (the Deck's own IP comes from DHCP, so no literal here).
// The Deck reads "server" from deck.json first; this is only a fallback. Discovery (UDP) is not wired on the Deck side yet.
ENV.set("deckServer", "");
ENV.set("deckToken", "");                          // the server sends the token; keep empty until known

// ---- Main screen (the shell). Pick one: ----
System.setShell("");                   // KryonOS's own launcher (Home): the same screen at boot and after leaving an app
// System.setShell("/sd/system/SystemApps/shell.js"); // the Deck shell (a second app list; kept until it has macros and server status of its own)
// System.setShell("/sd/UserApps/vnc/main.js");        // start straight into an app, e.g. the VNC client

// ---- Start-up actions (examples, uncomment to use) ----
// console.log("autoexec: RAM " + System.getInfo().freeRAM);
// Later: services (messages, battery, updates, server link) are started from here.

// Deck Link over BLE at power on (kernel service, passkey on; the PC pairs once):
// DeckLink.bleStart(true);
