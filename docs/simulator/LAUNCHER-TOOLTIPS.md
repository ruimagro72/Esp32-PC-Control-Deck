# Launcher tooltips (proposal)

Tooltip text for every control of the GTK launcher. Labels are the English source strings (`_("...")` in `simulator/launcher/sim_launcher.c`). Line numbers refer to that file; the UI is built in `main()` (line 2418) and the menu items in the same function. Line numbers were re-checked for `main()` and the Browse rows only; other numbers may have drifted.

Status meaning:
- **not wired yet**: no tooltip in the code. A later edit adds `gtk_widget_set_tooltip_text()` (or the tooltip argument of `menu_add()`) and the matching `msgid`/`msgstr` in `po/sim_launcher.pot` and `po/pt.po`.
- **already wired**: the code already sets this tooltip. Keep it, do not add a second one.

Proposed tooltips are at most 90 characters. Rows marked **already wired** show the existing text, which can be longer. Status "wired" = tooltip is in the code and in po/sim_launcher.pot and po/pt.po.

Labels with a glyph (▶, ■, ↻, ▤, ✕) are shown exactly as the code has them.

Counts (122 rows): 98 wired, 24 already wired, 0 not wired. The 10 Browse buttons are wired through the `tip` argument of `with_browse()`. The Badge row quotes the code text "2 s after the host connects", which is not the exact rule: RUNNING needs the host attached and connected and more than 2 s since the emulator started.

## Toolbar and status

| Control | Where | Proposed tooltip | Status |
|---------|-------|------------------|--------|
| ▶ Start | line 2391, tooltip 2393 | Simulator → Start (Ctrl+R) | already wired |
| ■ Stop | line 2391, tooltip 2393 | Simulator → Stop (Ctrl+.) | already wired |
| ↻ Restart | line 2391, tooltip 2393 | Simulator → Restart (Ctrl+Shift+R) | already wired |
| Profile name (toolbar) | `lbl_profile`, line 1284 (set in `update_status`) | Active profile file (full path) | already wired |
| Badge STOPPED / STARTING… / RUNNING | `lbl_badge`, line 2403 | Emulator state. RUNNING appears 2 s after the host connects | wired |
| Run timer 00:00:00 | `lbl_timer`, line 2402 | Time since the emulator started | wired |
| Board and resolution (toolbar) | `lbl_board`, line 2399 | Board preset and LCD size in use | wired |
| Status line | `status_label`, line 2407 (tooltip set in `update_status`, line 1304) | Full status text (already set at run time) | already wired |
| ▤ Console / Logs | `btn_dock`, line 2853, tooltip 2854 | Show/hide the bottom panel (validation, command, telemetry, logs) | already wired |
| Capability badges (status bar) | `caps_label`, line 2410 | Capability summary (text is also set in `update_panels`, line 925) | already wired |

## Nav rail and drawer

| Control | Where | Proposed tooltip | Status |
|---------|-------|------------------|--------|
| Profile (rail) | `rail_btn[0]`, line 2421 (rail loop) | Profile and firmware settings | wired |
| Runtime (rail) | `rail_btn[1]`, same loop | Runtime, audio, network, LittleFS and SD settings | wired |
| Merge / Run (rail) | `rail_btn[2]`, same loop | Run mode notes. The merge wizard is not built yet | wired |
| Storage (rail) | `rail_btn[3]`, same loop | Parts (WASM) list. LittleFS and SD are on the Runtime page | wired |
| ✕ (drawer close) | `btn_drawer_close`, line 2441 | Close panel | already wired |

## Profile / Firmware page

| Control | Where | Proposed tooltip | Status |
|---------|-------|------------------|--------|
| Active profile (JSON) | `entry_profile_path` (read-only, shows the profile name), `profile_field_set()` | Full path of the profile file that Save writes. Browse changes it | wired |
| Browse (profile) | line 2447 | Choose a profile file | wired |
| Board preset | `combo_board`, line 2471 | Board from boards/*.json. Sets cosim board and display size defaults | wired |
| Target chip (esp-emu) | `combo_chip`, line 2472 | Chip for esp-emu. The es3c28p board is an ESP32-S3 | wired |
| Firmware selection | `combo_firmware`, line 2473 | Firmware image to run: local build, KryonOS v3, asset or custom path | wired |
| Merged firmware | `entry_merged`, line 2474 | Merged flash image (.bin) passed to esp-emu | wired |
| Browse (merged) | line 2474 | Choose the merged firmware image | wired |
| ELF | `entry_elf`, line 2475 | ELF with symbols. Used only when Use ELF is checked | wired |
| Browse (ELF) | line 2475 | Choose the ELF file | wired |
| Use ELF (--elf) | `check_use_elf`, line 2459, tooltip 2779 | Release without ELF: use --no-elf; reduces symbols/BLE intercepts/hooks/debug (conditional). | already wired |
| Custom firmware path (legacy) | `entry_custom`, line 2477 | Old custom firmware path. Used only with Custom firmware selected | wired |
| Cosim board JSON | `entry_cosim_board`, line 2478, tooltip 2787 | Cosim: --cosim-board in-process preferred; TCP/unix/bridge conditional on version/host. | already wired |
| Browse (cosim board) | line 2478 | Choose the cosim board JSON | wired |
| LCD W (0=default) | `spin_lcd_w`, line 2479, tooltip 2785 | Pins/resolution/panel largely compiled (panel_config.h/lcd_touch_host.c). Partial runtime override. | already wired |
| LCD H (0=default) | `spin_lcd_h`, line 2480, tooltip 2786 | Pins/resolution/panel largely compiled (panel_config.h/lcd_touch_host.c). Partial runtime override. | already wired |
| Scale | `spin_scale`, line 2481 | Whole-number zoom of the LCD on the canvas (1 to 4) | wired |
| HW rot | `spin_rot`, line 2482 | Hardware rotation of the LCD and touch, 0 to 3 | wired |

## Runtime / Audio / Network page

| Control | Where | Proposed tooltip | Status |
|---------|-------|------------------|--------|
| Run mode | `combo_run_mode`, line 2530 | UI with embedded host (cosim), or backend only (--no-host) | wired |
| Cosim via Unix socket bridge (socat, conditional) | `check_cosim_unix`, line 2492 | Route cosim through a Unix socket with socat. Needs socat (conditional) | wired |
| Cosim Unix path | `entry_cosim_unix`, line 2532 | Unix socket path for the socat cosim bridge | wired |
| UART TCP / target | `entry_uart`, line 2533 | Host:port of the UART console, for example 127.0.0.1:5555 | wired |
| Console mode (tcp/stdio/pty/none) | `entry_console_mode`, line 2534 | Where the firmware console goes: tcp, stdio, pty or none | wired |
| esp-emu path (basic) | `entry_emu_path`, line 2535 | Path to the esp-emu binary | wired |
| esp-emu caps path (probe) | `entry_emu_caps`, line 2536 | esp-emu binary used by the capability probe | wired |
| Browse (esp-emu caps) | line 2536 | Choose the esp-emu binary for the probe | wired |
| Refresh caps | `btn_refresh_caps`, line 2528 | Run esp-emu --help and --version again and update the capability states | wired |
| Audio enabled | `check_audio`, line 2496 | Enable I2S audio through esp-emu. Output is a WAV file | wired |
| Realtime audio (conditional) | `check_audio_realtime`, line 2496, tooltip 2782 | Realtime audio: conditional(version/tool). Prefer I2S WAV from file when applicable. | already wired |
| Audio out path | `entry_audio_out`, line 2551 | WAV file that receives the I2S output | wired |
| Browse (audio out) | line 2551 | Choose the output WAV file | wired |
| Audio in path | `entry_audio_in`, line 2552 | WAV file used as I2S input (optional) | wired |
| Browse (audio in) | line 2552 | Choose the input WAV file | wired |
| I2S instance | `spin_audio_instance`, line 2553 | I2S instance number for esp-emu. -1 keeps the default | wired |
| Audio backend hint | `entry_audio_backend`, line 2554 | Audio backend hint kept in the profile (default wav-file) | wired |
| net enabled (user mode legacy) | `check_net_user`, line 2498 | Legacy switch for user-mode networking (--net user). Prefer Network enabled | wired |
| Network enabled | `check_net_enabled`, line 2515 | Enable networking with --net. Mode is set in Net mode | wired |
| Net mode | `entry_net_mode`, line 2556 | Mode for --net. user is the built-in user-mode networking | wired |
| Hostfwd (; sep) | `entry_hostfwd`, line 2557 | Host port forwards separated by ;. Format proto:hostaddr:hostport-guestaddr:guestport | wired |
| Wi-Fi (conditional) | `check_wifi`, line 2498, tooltip 2780 | Wi-Fi: conditional(version/tool/fw). Visible in the UI; the full flow depends on support/args/firmware. | already wired |
| Wi-Fi SSID | `entry_wifi_ssid`, line 2560 | Network name of the emulated Wi-Fi AP. Test profiles use testnet | wired |
| Wi-Fi password | `entry_wifi_password`, line 2561 | WPA passphrase, 8 to 63 characters. Ignored for open | wired |
| Wi-Fi auth | `combo_wifi_auth`, line 2562 | Security of the emulated AP: open, wpa2-psk, wpa3-sae or wpa2-wpa3 | wired |
| BLE (conditional) | `check_ble`, line 2498, tooltip 2781 | BLE: conditional(version/tool). Usually needs external ELF/HCI; not guaranteed. | already wired |
| BLE HCI | `entry_ble_hci`, line 2563 | HCI transport for BLE, for example tcp:localhost:9544. Needs an HCI server there | wired |
| GDB stub (--gdb, conditional tool) | `check_gdb_enabled`, line 2538 | Start a GDB stub on the GDB port (--gdb). Conditional on esp-emu support | wired |
| Halt on start (--gdb-halt) | `check_gdb_halt`, line 2540 | Wait for GDB before the firmware starts (--gdb-halt) | wired |
| GDB port | `spin_gdb_port`, line 2542 | TCP port of the GDB stub | wired |
| Trace (--trace, conditional tool) | `check_trace_enabled`, line 2544 | Write an esp-emu trace file (--trace). Conditional on esp-emu support | wired |
| Trace path | `entry_trace_path`, line 2547 | Output file for the trace | wired |
| Browse (trace path) | line 2626 | Choose the trace output file | wired |
| Extra args | `entry_extra`, line 2548, tooltip 2788 | Extra args passed to esp-emu (unknown flags may fail). | already wired |
| LittleFS enabled (conditional) | `check_littlefs_enabled`, line 2499, tooltip 2783 | LittleFS conditional (image/layout). SD: Card menu (esp-emu 1.1.0 --sd). | already wired |
| LittleFS image | `entry_littlefs`, line 2565, tooltip 2784 | Optional LittleFS image. SD: Card menu (esp-emu 1.1.0 --sd). | already wired |
| LittleFS label | `entry_littlefs_label`, line 2566 | Partition label of the LittleFS image (default littlefs) | wired |
| LittleFS mount | `entry_littlefs_mount`, line 2567 | Mount path of the LittleFS image in the firmware | wired |
| LittleFS offset | `entry_littlefs_offset`, line 2568 | Flash offset of the LittleFS partition. Must match the partition table | wired |
| LittleFS size | `entry_littlefs_size`, line 2569 | Size of the LittleFS partition. Must match the partition table | wired |
| SD (legacy: use the Card menu) | `check_sd_enabled`, line 2505 | Legacy SD switch. Use Card menu to eject or replace the image | wired |
| SD image | `entry_sd_image`, line 2572 | Card image file (.img). Set it from Card menu > Replace image | wired |
| SD card folder | Runtime page, SD section (line not yet known) | Folder used as the content of the SD card. At Start the card image is rebuilt from this folder. | wired |
| SD mode | `combo_sd_mode`, line 2571 | SD bus mode: sdmmc (ESP32-S3), sdspi or none | wired |
| SD slot | `entry_sd_slot`, line 2573 | SD slot index (legacy field kept in the profile) | wired |
| SD CS pin | `entry_sd_cs`, line 2574 | Chip-select GPIO for SPI SD (legacy field kept in the profile) | wired |
| Browse (LittleFS image) | line 2565 | Choose the LittleFS image file | wired |
| Browse (SD card folder) | Runtime page, SD section (line not yet known) | Choose the folder to use as the SD card. | wired |

## Merge / Run and Storage / Parts page

| Control | Where | Proposed tooltip | Status |
|---------|-------|------------------|--------|
| Open merge wizard | `t13btn`, line 2581 | Merge wizard is not built yet. Use ./start.sh emu merge | wired |
| Parts dir (.wasm) | `entry_parts_dir`, line 2591 | Folder with .wasm parts. Scan reads this folder | wired |
| Scan | `btn_parts_scan`, line 2596 | Look for .wasm parts in the parts folder | wired |
| Check conflicts | `btn_parts_validate`, line 2597 | Check the parts list for claim and wiring conflicts | wired |
| Instance ID | `entry_parts_instance_id`, line 2602 | Id of this part instance in the parts list | wired |
| Part name | `entry_parts_part_name`, line 2603 | Name of the part to add | wired |
| Add/Config | `btn_parts_add`, line 2606 | Add the part instance to the list, or update its settings | wired |
| Remove | `btn_parts_remove`, line 2607 | Remove the selected part instance from the list | wired |

## Bottom dock: tool tabs and log bar

| Control | Where | Proposed tooltip | Status |
|---------|-------|------------------|--------|
| Open (Code/Build/Run) | `btn_wf_open`, line 2657 | Open a file in the workflow editor | wired |
| Save (Code/Build/Run) | `btn_wf_save`, line 2658 | Save the workflow file. This does not save the profile | wired |
| Build emulator (start.sh) | Simulator menu item (`on_workflow_build`) | Run the build command set in Code/Build/Run > Build cmd | wired |
| Text filter | `entry_log_filter`, line 2733 | Show only log lines that contain this text | wired |
| Level filter | `combo_log_level`, line 2733 | Show only lines of this level. All levels means no filter | wired |
| Origin filter | `combo_log_origin`, line 2741 | Show only lines from this source (esp-emu, host, launcher, validation) | wired |
| Follow | `check_log_follow`, line 2748 | Keep the log scrolled to the newest line | wired |
| Clear | `btn_log_clear`, line 2749 | Clear the visible log tab | wired |
| Export... | `btn_log_export`, line 2751 | Save the visible log lines to a text file | wired |

## Menu bar

| Menu item | Where | Proposed tooltip | Status |
|-----------|-------|------------------|--------|
| Profile > New | line 2793 | Create a profile with default values. Nothing is saved until Save | wired |
| Profile > Open… | line 2794 | Load a profile JSON file and make it the active profile | wired |
| Profile > Save | line 2795 | Write the active profile file | wired |
| Profile > Save as… | line 2796 | Write the profile to a new file and make it the active profile | wired |
| Profile > Duplicate | line 2797 | Copy the active profile to a new file and make the copy active | wired |
| Profile > Import conf | line 2798 | Import a legacy launcher.conf into the editor | wired |
| Profile > Export | line 2799 | Write a copy of the profile to a file. The active profile does not change | wired |
| Profile > Quit | line 2801 | Close the launcher. Stops the emulator and saves the profile (Ctrl+Q) | wired |
| Simulator > Start | line 2804 | Start the emulator with the active profile (Ctrl+R) | wired |
| Simulator > Stop | line 2805 | Stop the emulator and the host (Ctrl+.) | wired |
| Simulator > Restart | line 2806 | Stop and start the emulator again (Ctrl+Shift+R) | wired |
| Simulator > Validate | line 2811 | Run validation again and show the results in the Validation tab | wired |
| Simulator > Copy command | line 2812 | Copy the effective esp-emu command line to the clipboard | wired |
| Replace image… (restarts the emulator) | line 2945 | esp-emu 1.1.0 cannot swap the card while running: if the emulator is running, it is restarted in a controlled way. | already wired |
| Card > Eject (restarts the emulator) | line 2816 | Starts without the card (--no-sd). esp-emu 1.1.0 does not remove the card while running: controlled restart. | already wired |
| Card > Build image from folder… | line 2819 | ./start.sh card image --force <folder> <image>: REPLACES the image (what the firmware wrote to the card is lost). | already wired |
| Card > Extract image to folder… | line 2821 | ./start.sh card extract --force <image> <folder> | already wired |
| Card > Open card folder | line 2822 | xdg-open simulator/SD_Card | already wired |
| View > Configuration panel | line 2824 | Show or hide the drawer with the configuration pages | wired |
| View > Bottom panel | line 2825 | Show or hide the bottom dock | wired |
| View > Status bar | line 2826 | Show or hide the status bar | wired |
| View > Logs | line 2827 | Show or hide the log tabs of the bottom dock | wired |
| View > GPIO/Pins window | line 2828 | Shows the bottom panel on the GPIO/Pins tab | already wired |
| View > Language | line 2830 | Interface language. The launcher must be restarted to apply it | wired |
| Help > About | line 2850 | About this launcher | wired |

Note: the Language dialog already says the launcher must be restarted (`on_ui_lang_selected`, line 2096). The tooltip repeats that for the menu.

## Help menu entries

The Help menu has About, Help contents and Keyboard shortcuts (tooltips wired). Open items:

| Entry | Action | Notes |
|-------|--------|-------|
| Help contents | Opens `docs/simulator/LAUNCHER-HELP.md` in the launcher's own viewer window (`help_ui.c`) | Done, not xdg-open. Looks for the file from the working directory, then from the binary's folder |
| Keyboard shortcuts | Dialog with the shortcut table built into `help_ui.c` (not read from LAUNCHER-HELP.md) | Lists F1, Ctrl+R, Ctrl+., Ctrl+Shift+R, Ctrl+Q |
| About | Already present (`on_help_about`, line 2082). Text: "ESP32 Simulator (Unified)" and a one-line description | Keep. Could add the esp-emu version from the probe |

Tooltip for each: Help contents "Open the launcher help file", Keyboard shortcuts "List the keyboard shortcuts" (both 90 characters or less).
