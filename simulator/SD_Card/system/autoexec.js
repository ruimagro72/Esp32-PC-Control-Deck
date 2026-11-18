// /sd/autoexec.js - runs once at start, before the main screen (like autoexec.bat / rc).
// Edit and copy it back to the card; no firmware update needed.

// ---- Main screen (the shell). Pick one: ----
System.setShell("");                   // KryonOS's own launcher (Home): the same screen at boot and after leaving an app
// System.setShell("/sd/shell.js");       // the Deck shell (a second app list; kept until it has macros and server status of its own)
// System.setShell("/sd/apps/vnc/main.js"); // start straight into an app, e.g. the VNC client

// ---- Start-up actions (examples, uncomment to use) ----
// console.log("autoexec: RAM " + System.getInfo().freeRAM);
// Later: services (messages, battery, updates, server link) are started from here.

// Deck Link over BLE at power on (kernel service, passkey on; the PC pairs once):
// DeckLink.bleStart(true);
