# Simulator UI Design — janela unificada launcher/simulador

> Status 18 Nov 2026: reference binary is esp-emu 1.1.0; the SD rows use `--sd`. Older mentions (pre-1.1.0) were kept only where the statement still holds; check the rest against `esp-emu --help` before use.

Estado: design factual (T11, docs). Sem alteração de código. Base: `docs/simulator/SIMULATOR_PROFILE_SPEC.md`, `docs/simulator/SIMULATOR_CAPABILITIES_MATRIX.md`, `docs/simulator/ESP_EMULATOR_INTEGRATION.md`, `simulator/launcher/profile.schema.json`, `simulator/launcher/README.md`, `docs/simulator/launcher/INTEGRATION_PLAN.md`, `start.sh emu help`, plano T01–T20.

## 1. Objetivo e princípios

- **Objetivo:** especificar UI/UX completa da janela unificada para validar interface antes da implementação total (T01–T20), cobrindo perfil, firmware, HW/perif/emu, storage/parts, rede/áudio, debug, execução e validação.
- **Janela única:** config/perfil + canvas `sim_host` embutido + logs/consola/estado numa aplicação GTK (evolução de `simulator/launcher/sim_launcher.c`).
- **Sem host SDL separado no fluxo normal:** `esp-emu` corre como subprocesso; cosim host/render/input integrados in-process via API `sim_host_*` (`simulator/emu/sim_host.{c,h}`) e canvas GTK (`GtkDrawingArea` + `SDL_CreateWindowFrom` / attach nativo). Host standalone opcional só durante migração (T12/T16), conforme `docs/simulator/launcher/INTEGRATION_PLAN.md`.
- **Perfil canónico:** `simulator.profile/v1` (`simulator/launcher/profile.schema.json`, `profile.{c,h}`) é a unidade versionável/exportável (target, firmware/artefactos, runtime, UI state, extensions). Importa/migra `launcher.conf`/`.hw_*` legados.
- **Validação antes de run:** preflight reporta `supported-now | needs-runtime-host | blocked-by-emu | conditional(version/tool)` com limites explícitos (SDMMC S3 emu 1.1.0, releases sem ELF, pinos/resolução compilados, etc.).
- **Reprodutibilidade:** UI mostra/computa comando efetivo (equivalente `start.sh emu ...`/args `esp-emu`) e rastreia artefactos/checksums/logs.
- **Progressive disclosure:** defaults seguros, opções avançadas recolhidas; tooltips/help inline para limites e dependências.

## 2. Layout proposto (wireframe)

```text
+--------------------------------------------------------------------------------------+
| Toolbar / Barra topo                                                                  |
| [Perfil v][Novo Abrir Guardar Guardar como Duplicar Importar/Exportar]               |
| [Start][Stop][Restart][Validar]  Estado: stopped/running/error  Caps: [badge resumo]   |
| [Copiar comando efetivo] [Abrir simulator/logs]                                      |
+-----------------------------------+--------------------------------------------------+
| Painel esquerdo: Config (scroll)  | Área central: Simulação                          |
| Tabs/Seções (accordion):          | +----------------------------------------------+ |
|  - Perfil / Metadados             | | Canvas display (sim_host embutido)           | |
|  - Board / HW                     | |  [framebuffer LCD, scale/fit, overlay debug] | |
|  - Firmware / Artefactos          | |  (rato/teclado -> touch/keys via sim_host)   | |
|  - Merge / Wizard                 | +----------------------------------------------+ |
|  - Storage / Parts                | [Scale][-][+] [HW rot 0-3] [Screenshot] [Info]   |
|  - Áudio                          | Status inline: board/fw/cosim/fps/resolução      |
|  - Rede / Rádio                   +--------------------------------------------------+
|  - Runtime emu                    | Painel inferior/lateral (tabs, redimensionável): |
|  - Debug / Observabilidade*       | [UART/Console] [Logs emu] [Logs host/cosim]      |
|  - Execução / Validação           | [Validação: erros/warnings/info + estado]        |
|  - UI state                       | [Capabilities] [Processos/ports]                 |
| [Aplicar ao perfil] [Reverter]    | filtros, busca, auto-scroll, export, limpar      |
+-----------------------------------+--------------------------------------------------+
| Barra estado: perfil path, firmware resolvido, cosim endpoint, UART TCP, uptime, PID  |
+--------------------------------------------------------------------------------------+

Dialogs/Wizards (modais ou janelas auxiliares):
- Wizard Merge/Flash (bootloader/partitions/app/fs, offsets, saída, dry-run/comando esptool)
- Importar perfil / Migrar launcher.conf
- Seletor de artefactos/presets (browse, drag-drop, SHA256, resolved info)
- Wiring/claims de virtual parts (.wasm) e estado/interação
- Detalhe de validação/capabilities (por item: estado, motivo, dependência, mitigação)
```

Notas de layout: painéis esquerdo/inferior colapsáveis; canvas central prioritário; consola e validação sempre acessíveis. Secções marcadas `*` (debug avançado) ocultas por defeito/progressive disclosure.

## 3. Inventário completo de opções/controles previstos (T01–T20)

Estados: `supported-now` (fluxo atual repo+emu 1.1.0), `needs-runtime-host` (depende de host/part/refactor/adaptação), `blocked-by-emu` (limite esp-emu 1.1.0/S3), `conditional(version/tool)` (versão, ELF, toolchain, build flags, rede/HCI, validação).

### 3.1 Perfil
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| Novo / Abrir / Guardar / Guardar como / Duplicar | CRUD de ficheiro perfil JSON (`simulator.profile/v1`) | `needs-runtime-host` (T11/T17/T19; base `profile.c` existe) | `simulator/launcher/profile.{c,h}`; `launcher.conf` hoje parcial `supported-now` |
| Importar / Exportar JSON | Bundle/perfil portável com refs/checksums/comandos | `needs-runtime-host` | Rastreabilidade artefactos (T01/T17) |
| Migrar `launcher.conf` | Import legacy para perfil v1 | `needs-runtime-host` (função prevista em `profile`) | `simulator/launcher/launcher.conf`, `.hw_rotation`, `.hw_board` |
| Nome / descrição / tags / id | `metadata.{id,name,description,tags}` | `needs-runtime-host` | schema suportado |
| Perfil ativo / recentes | Seleção rápida e estado de sessão | `needs-runtime-host` | `ui_state`/persistência launcher |
| Comando efetivo copiável | Render de args finais (`start.sh emu`/`esp-emu`) | `needs-runtime-host` (T11/T16; hoje implícito) | `proc.c`, `firmware_mgr`, `emu_runtime`, `validate` |

### 3.2 Board / HW
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| Board preset (5 + custom) | `es3c28p`, `esp32-s3-devkitc-1-n16r8`, `lilygo-t-deck`, `lilygo-t-hmi`, `m5stack-cardputer`, custom | `supported-now` (seleção/id hoje) / `needs-runtime-host` (aplicar perfil completo) | `board_catalog.{c,h}`, `boards/*.json`, `simulator/launcher/README.md` |
| MCU / flash / PSRAM | `board_hardware.mcu.{chip,flash,psram}`; runtime `--chip/--psram-size` | `supported-now` (S3 16MB/8M es3c28p) / `conditional(version/tool)` outros alvos | `start.sh emu`, `esp-emu 1.1.0`; Cardputer PSRAM/expectativas a validar |
| Display W/H/orientação/rotação HW/scale | resolução, `orientation`, `hw_rot` 0-3, scale UI | `supported-now` (rot/scale/CLI parcial) / `needs-runtime-host` (W/H/painel runtime) | `sim_host`/`panel_config.h` compilado; launcher persiste W/H override sem aplicação total |
| Panel/controller/bus | modelo, bus SPI/paralelo, timing/CGRAM/offset/inversão | `needs-runtime-host` (maioria) | ILI9341/ST7789* por board; T-HMI paralelo gap |
| Touch controller/bus/pins/calib | FT6336G/GT911/XPT2046, I2C/SPI/bitbang, INT/RST/SDA/SCL, calibração | `supported-now` (FT6336G init/poll básico es3c28p) / `needs-runtime-host` (GT911/XPT2046/runtime) / `conditional` | `board-es3c28p.json` regfile, `lcd_touch_host.c`/`sim_host` |
| Pinos/buses | SPI/I2C/UART/GPIO mapping (`buses_pins`) | `needs-runtime-host` (runtime) / `conditional` | hoje em `panel_config.h`, perfis host, board JSON |
| Cosim mode/board_json/tcp/unix | `board_hardware.cosim.{mode,board_json,tcp,unix}` | `supported-now` (`board_json`, `tcp`) / `conditional(version/tool)` (`unix` bridge; TCP interno pre-1.1.0 mantém-se) | `--cosim-board/--cosim-tcp`, socat bridge, `start.sh emu ui` |
| Overrides (nota runtime-host) | `display.overrides`, pin/panel/touch overrides | `needs-runtime-host` | exigir tooltip: pinos/resolução/painel maioritariamente compilados; rebuild/refactor necessário |

### 3.3 Firmware
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| Seleção fonte | local build, preset KryonOS, asset, custom | `supported-now` (UI atual subset) / `needs-runtime-host` (gestão completa) | `firmware_mgr.{c,h}`, presets por board |
| Formato | merged/split/ELF/LittleFS/UF2 | `supported-now` (merged/ELF/split->merge) / `conditional(version/tool)` (LittleFS fluxo, UF2 conversão) | `start.sh emu merge`, esptool, esp-emu merged/HEX conforme versão |
| Paths browse/drag-drop | `firmware.artifacts.*` | `needs-runtime-host` (UX); paths suportados via CLI/config | diálogos GTK/validação paths |
| Offsets/flash mode/freq/size | split/merge params (`firmware.flash.*`) | `supported-now` (defaults merge es3c28p) / `needs-runtime-host` (UI completa) | esptool merge-bin, layouts partição |
| ELF use/detect/`--no-elf` | `artifacts.elf`, `use_elf`, auto-detect | `supported-now` (`--elf/--no-elf`) / `conditional` (releases sem ELF) | BLE hooks/símbolos dependem de ELF |
| Resolved/SHA256 | metadados resolvidos, checksums por artefacto | `needs-runtime-host` | `firmware.resolved`, import releases (KryonOS v3 SHA256) |
| Presets por board | defaults de paths/env/offsets por alvo | `needs-runtime-host` | `board_catalog`, `KRYONOS-V3.0.0-PREP.md` refs |

### 3.4 Merge / wizard
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| Selecionar bootloader/partitions/app/fs | inputs split + LittleFS/data | `supported-now` (merge CLI/env es3c28p) / `needs-runtime-host` (wizard genérico) | `start.sh emu merge`, `firmware_mgr` |
| Offsets | `0x0/0x8000/0x10000/0xc90000` default S3, editáveis | `supported-now` defaults / `conditional` por layout | tabela partições do firmware |
| Saída em `simulator/build/` | path destino merged | `supported-now` | `EMU_MERGED` default |
| Gerar/comando esptool | preview/dry-run/copy | `needs-runtime-host` | reprodutibilidade; esptool/esptool.py/penv |
| Validar merge | existência, offsets, tamanho, FS opcional | `needs-runtime-host` | `validate.{c,h}` |

### 3.5 Storage / Parts
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| LittleFS image | `storage.littlefs.{enabled,image_path}`; `--littlefs` merge | `conditional(version/tool)` (depende de imagem/layout/flow) | partição FS (ex. `0xc90000`), mount pode falhar sem bloquear; `SD_Card` ~37MB > spiffs pequena |
| SD | `storage.sd_card.*` | `supported-now` (es3c28p, 1.1.0 `--sd`) | `start.sh run/ui/shot` pass `--sd simulator/emu/storage/sd.img`; swap during a run not modelled |
| Virtual parts `.wasm` + wiring/claims/estado/interação | carregar parts em `--cosim-board`, mapear pinos/buses, claims GPIO/I2C/SPI/UART/PWM/ADC/EN, UI de estado/input | `needs-runtime-host` / `conditional(version/tool)` | `esp-part` ABI/`esp_cosim.proto`, toolchain/parts não incluídas; preferir in-process sem TCP |

### 3.6 Áudio
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| Enable | `audio.enabled`, `--no-audio` | `supported-now` (capture I2S out default em UI) | `start.sh emu ui/bootlog/run` |
| I2S WAV out/in paths | `--i2s-wav`, `--i2s-wav-in` | `supported-now` (out) / `conditional(version/tool)` (in/instância/tempo-real) | `simulator/logs/i2s_out.wav` default |
| Realtime flag | preferência `audio.realtime` / PCM tempo-real | `conditional(version/tool)` | cosim externo/ferramentas/binário/firmware |

### 3.7 Rede / Rádio
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| `--net user` | `network_radio.net_user` | `supported-now` (flag/pass-through) | default emu; paridade funcional a validar |
| Hostfwd | regras `hostfwd` | `conditional(version/tool/host)` | `--hostfwd`, permissões/backend |
| Wi-Fi SSID/pass/auth | `network_radio.wifi.*`, `--wifi-*` | `conditional(version/tool)` | STA/AP/emulação dependente de firmware/runtime |
| BLE HCI/condicional | `network_radio.ble.*`, `--ble-hci` | `conditional(version/tool)` | geralmente requer `--elf`; HCI externo/adaptador; hooks observados mas fluxo completo não confirmado |

### 3.8 Runtime emu
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| esp-emu path | `emu_runtime.esp_emu_path` | `supported-now` (binário 1.1.0 no repo) | validar `--version/--help`; motor externo |
| Chip/PSRAM/timeout | `chip`, `psram`, `--timeout` | `supported-now` (S3/8M/timeout bootlog) / `conditional` | perfis por board |
| `--uart-tcp` / console mode | endpoint, `console_mode`, `--console auto|uart0|usb-cdc` | `supported-now` (UART TCP) / `conditional` (USB-CDC/JTAG por fw) | `start.sh emu console` (socat/nc), `boot.log` |
| Extra args | `emu_runtime.extra_args` / pass-through `--` | `supported-now` | cuidado com flags desconhecidas |
| Backend `ui/cosim/--no-host` | modo de arranque (UI embutida, cosim-only, headless) | `supported-now` (CLI `ui/cosim/run/bootlog`) / `needs-runtime-host` (orquestração embutida) | `start.sh emu ui --no-host`, `proc.c` process group, exit code 42 (reboot hw_rot/board) |

### 3.9 Debug / Observabilidade (condicional/lab-style)
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| Run/pause/step/reset | control plane | `conditional(version/tool)` | esp-emu/lab/control API; não integrado no launcher atual |
| GDB | `--gdb`, `--gdb-halt` | `conditional(version/tool)` | ELF recomendado; suporte binário |
| Trace | `--trace` | `conditional(version/tool)` | ficheiro trace; análise externa |
| Perf | MIPS/ciclos/tempo | `conditional(version/tool)` | requer telemetria/API suportada |
| GPIO | watch/drive/visualização | `conditional(version/tool)` / `needs-runtime-host` (UI) | cosim/parts/control |
| Registos / memória | inspeção | `conditional(version/tool)` | GDB/control/trace |
| Screenshot | captura canvas host | `supported-now` (SDL host) / `needs-runtime-host` (UI embutida) | UART TCP não suporta `shot` binário; usar screenshot SDL |
| Info board/firmware | paths, versões, artefactos | `supported-now` (`emu info`) / `needs-runtime-host` (painel) | `start.sh emu info` |

### 3.10 Execução / Validação
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| Start/Stop/Restart | ciclo de vida (emu subprocesso + host embutido) | `supported-now` (process group externo) / `needs-runtime-host` (attach/detach embutido) | `proc.c`, `sim_host` connect/disconnect |
| Preflight | tools, paths, ELF, portas, ranges, compatibilidade | `needs-runtime-host` | `validate.{c,h}`, `firmware_mgr` |
| Lista erros/warnings/info com estados | itens `supported-now|needs-runtime-host|blocked-by-emu|conditional(version/tool)` | `needs-runtime-host` | matriz/capabilities por board+perfil; mensagens acionáveis |

### 3.11 UI state
| Opção/controlo | Descrição / campos | Estado alvo | Dependências / notas |
|---|---|---|---|
| Painéis visíveis/layout | `ui_state.panels`, docks colapsáveis | `needs-runtime-host` | GTK persistência |
| Filtros/export logs | UART/emu/host/cosim/validação | `needs-runtime-host` | `simulator/logs/*` |
| Abrir `simulator/logs` | atalho diretório/logs correntes | `supported-now` (manual) / `needs-runtime-host` (botão) | `boot.log`, `i2s_out.wav`, `cosim-bridge.log` |
| Window/scale/last_run | geometria, escala, último resultado | `supported-now` parcial / `needs-runtime-host` | `ui_state.{window,scale,last_run}` |

## 4. Mapeamento: grupos → estado/dependências → ficheiros/módulos

| Grupo | Estado atual (hoje) | Estado planeado / bloqueado | Ficheiros/módulos principais |
|---|---|---|---|
| Perfil | `launcher.conf` + `.hw_board/.hw_rotation` (`supported-now` parcial) | perfil `simulator.profile/v1` completo, import/export/migração (`needs-runtime-host`) | `simulator/launcher/profile.{c,h}`, `profile.schema.json`, `config.{c,h}`, `simulator/launcher/README.md` |
| Board/HW | presets/id, rot, alguns campos CLI; pinos/resolução compilados | aplicação runtime por board, overrides validados; paralelo/GT911/XPT2046/teclado `needs-runtime-host`; limites por emu `conditional` | `board_catalog.{c,h}`, `boards/*.json`, `simulator/emu/panel_config.h`, `simulator/emu/lcd_touch_host.c`, `simulator/emu/sim_host.{c,h}`, `simulator/emu/board-es3c28p.json`, `simulator/emu/HARDWARE-SUPPORT.md` |
| Firmware | seleção/paths/`--elf/--no-elf` subset, merge es3c28p | deteção/presets/SHA256/resolved/split-merge genérico (`needs-runtime-host`); UF2/LittleFS flows `conditional` | `firmware_mgr.{c,h}`, `start.sh` (`emu merge/info`), `simulator/build/*`, `simulator/emu/firmware/*`, `KRYONOS-V3.0.0-PREP.md` |
| Merge/wizard | CLI merge com defaults | wizard UI com preview/validação (`needs-runtime-host`) | `firmware_mgr.{c,h}`, `validate.{c,h}`, `start.sh emu merge` |
| Storage/Parts | LittleFS em merge quando existe; SD via `--sd` image | LittleFS UX `conditional`; SD `supported-now` (1.1.0); parts/wiring `needs-runtime-host/conditional` | `profile.schema.json` (`storage`), `start.sh`, `esp-part` refs, `simulator/ESP_EMULATOR_INTEGRATION.md` |
| Áudio | I2S WAV out integrado; `--no-audio` | in/realtime/instância `conditional`; UI routing `needs-runtime-host` | `start.sh`, `simulator/logs/i2s_out.wav`, `emu_runtime`/`audio` no perfil |
| Rede/Rádio | `--net user`/extra args básicos | wifi/hostfwd/BLE/config UI `conditional`; validação conectividade | `network_radio` (schema), `start.sh`, esp-emu flags |
| Runtime emu | esp-emu 1.1.0, `run/bootlog/ui/console`, process group | orquestração embutida, modos unificados, sessão/debug endpoints `needs-runtime-host/conditional` | `simulator/launcher/proc.{c,h}`, `start.sh emu`, `simulator/esp-emu-1.1.0-*/esp-emu` |
| Debug/Observabilidade | info/logs/bootlog/screenshot SDL; GDB/trace flags disponíveis no binário | painéis lab-style (pause/step/perf/GPIO/regs/mem) `conditional/needs-runtime-host` | `esp-emu --help`, `start.sh`, `sim_host` state/screenshot, web/lab refs |
| Execução/Validação | start/stop/restart launcher; validação ad-hoc | preflight/capabilities engine com estados por item (`needs-runtime-host`) | `validate.{c,h}`, `proc.{c,h}`, `sim_host.{c,h}`, `simulator/SIMULATOR_CAPABILITIES_MATRIX.md` |
| UI state | scale/rot/board/conf parcial | layout/painéis/filtros/export/last_run persistidos (`needs-runtime-host`) | `config.{c,h}`, `profile` `ui_state`, `sim_launcher.c` |
| Integração janela única | launcher externo + host SDL separado (fluxo atual) | canvas embutido + sim_host in-process (`needs-runtime-host`, T12/T16) | `docs/simulator/launcher/INTEGRATION_PLAN.md`, `sim_launcher.c`, `sim_host.{c,h}`, GTK/SDL2 |

## 5. Notas de UX

- **Defaults seguros:** perfil es3c28p, `--chip esp32s3 --psram-size 8M`, `--uart-tcp 127.0.0.1:5555`, áudio out para `simulator/logs/i2s_out.wav`, `--net user` opcional/desligado conforme preset, sem partes/SD experimentais ativados.
- **Tooltips/help inline obrigatórios (limites):**
  - SDMMC/SDSPI: `--sd` in esp-emu 1.1.0 (es3c28p tested). It was blocked in pre-1.1.0.
  - **Release sem ELF:** usar `--no-elf`; menor símbolos/interceções BLE/hooks/debug.
  - **Pinos/resolução/runtime:** muitos parâmetros de painel/touch/bus ainda compilados (`panel_config.h`, `lcd_touch_host.c`); W/H override pode não aplicar totalmente em runtime → indicar necessidade de rebuild/refactor.
  - **Cosim:** `--cosim-board` in-process preferido; `--cosim-tcp` necessário para host externo em pre-1.1.0; ponte Unix não elimina TCP interno.
  - **Boot firmware stock:** pode WDT/hang sem guardas emu (`ESP_EMU_SKIP_AUDIO` em `es3c28p_emu`); validar por bootlog.
- **Progressive disclosure:** mostrar essenciais (perfil, firmware, board, start/stop, canvas, consola, validação); esconder avançado (GDB/trace/perf/regs/mem, wiring parts, offsets manuais, extra args, cosim transporte detalhado) em "Avançado/Lab".
- **Feedback de estado:** badges por controlo/grupo com estado de capacidade; erros bloqueantes impedem Start e explicam mitigação; warnings permitem continuar com confirmação.
- **Comando efetivo sempre visível/copiável:** reflete perfil resolvido + overrides, para reproduzir via `start.sh emu` ou `esp-emu`.
- **Recuperação/restart:** expor exit code 42 (mudança `hw_rot/board_state`) e ações de restart controlado; preservar logs e estado.
- **Acessibilidade operacional:** filtros/busca/export em logs, indicação clara de processo/portas em uso, e abertura rápida de `simulator/logs`/artefactos.
