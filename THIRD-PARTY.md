# Third-party components (deck branch)

| Component | Where | Licence | Notes |
|---|---|---|---|
| KryonOS 3.0.0 (runtime the apps run on) | not in this branch; see the KryonOS fork (`es3c28p-support`) | GPL-3.0 | The Deck's apps use KryonOS APIs; no KryonOS code is copied here. |
| `decknet.js` (protocol 4 in JavaScript) | `firmware/apps/lib/decknet.js`, `firmware/apps/deck/decknet.js` | Owner's own code, no restrictions | No third-party code. |
| `krfb-virtualmonitor` (KDE, used by `deck-monitor.sh`) | `firmware/tools/deck-monitor.sh` | To confirm | A system tool on the PC, not redistributed. |
