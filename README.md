# ESP32 PC Control Deck: simulator branch

An independent ESP32 emulator front-end. It runs any ESP32 firmware image on a PC: the `esp-emu` 1.1.0 binary (installed
by you, not included here), an SDL cosim host for the LCD and touch, and a GTK3 launcher.

Part of a project with four branches (`main`, `deck`, `server`, `simulator`) and the KryonOS fork (`es3c28p-support`). See the `main` branch for the overview.

Short version:

```bash
make -C simulator test              # unit tests
simulator/launcher/build.sh         # GTK3 launcher
simulator/launcher/sim_launcher
```

Parts, limits and what needs the owner's `start.sh`: [`simulator/README.md`](simulator/README.md).
Full documentation: [`docs/simulator/README.md`](docs/simulator/README.md).

Licence: free for the KryonOS team; other use by consultation. See [`SIMULATOR-LICENSE.md`](SIMULATOR-LICENSE.md).
