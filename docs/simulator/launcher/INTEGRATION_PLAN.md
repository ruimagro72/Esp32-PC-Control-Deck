# Launcher and SDL host integration: plan and status

Goal: one desktop app (GTK3) with the config and start panel and an embedded SDL canvas for the emulated display and touch,
without a separate host window in the normal flow.

## Status (18 Nov 2026)

- Done: the cosim client (`simulator/emu/sim_host.c`, with `lcd_touch_host.c` as the standalone host) is compiled into the launcher,
  and the launcher embeds the LCD canvas.
- Open: `./start.sh emu ui` still opens the separate SDL host window unless `--no-host` is given. Use it when the
  launcher should not open a second window.
- Open: panel size, pins, touch address and mirroring are still compiled in `simulator/emu/panel_config.h`.
- Open: a single binary for the launcher and the host, and removal of duplicate process handling in `start.sh`.

## Steps

1. Split the cosim client from the host window into a reusable core (`sim_host.c/.h`): init and config, connect and disconnect,
   event processing, render, input injection, shutdown. Status: core is shared; the host window still has its own main loop.
2. Embed SDL2 in GTK3: `GtkDrawingArea` -> X11 window id -> `SDL_CreateWindowFrom`, render on a timer, map GDK events to
   touch. Status: canvas embedded; keep an eye on focus, resize and input coordinates (platform-sensitive).
3. Integrate with `config` and `proc`: Start launches esp-emu with the cosim endpoint; the viewer attaches; Stop detaches
   and stops the processes. Status: Start, Stop and Restart call `start.sh emu ui`. Re-test before relying on it.
4. Put all editable parameters in one place (firmware, board, cosim JSON, pins, resolution, audio, rotation, scale, UART,
   network, extra arguments) and apply them at runtime where possible. Status: most are editable; pins and resolution are not.
5. Remove duplicate process and window code, and update the build and start scripts and docs.

## Notes and risks

- SDL and GTK in one process: platform-sensitive (X11 focus, resizing, input coordinates).
- esp-emu stays a separate process.
- Runtime resolution and pins need the core refactor (step 4); today they come from the profile or compile-time defaults.
