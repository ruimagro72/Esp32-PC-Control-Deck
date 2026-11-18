# D. Deck layer (NOT for upstream)

Our own layer for the ESP32 PC Control Deck, kept as patches so it can be rebased onto new
KryonOS releases. Do not send these to the KryonOS project.

| Patch | What |
|---|---|
| 0001 | `src/Kernel/DeckLink.*`: the Deck protocol 4 on the USB port, shared with the console (COBS frames start with 0x02, text lines go to the console); ping -> hello, `console_input` runs a console line, other frames queued for the Deck app. Replaces the console line reader of C-0001 |
| 0002 | `/sd/autoexec.js` at start, `/sd/shell.js` as the main screen instead of KryonOS's launcher (falls back to the launcher after 3 quick failures), `System.setShell(path)` |
| 0003 | Deck apps (`apps/`), `shell/`, the App Store index copy (`KryonOS-AppStore/`), PC tools `Send.java`, `deck-monitor.sh`, `vnc-bridge.sh` |

Applies on top of B, A and C. Secrets are not in the patches: `apps/*/deck.json` and
`vnc.json` are git-ignored.
