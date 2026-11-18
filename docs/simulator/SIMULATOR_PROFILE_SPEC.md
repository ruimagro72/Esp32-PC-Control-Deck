# Simulator Profile/Workflow Spec (GTK launcher + embedded canvas sim_host + esp-emu)

> Status 18 Nov 2026: reference binary is esp-emu 1.1.0; SD rows updated (`--sd`). Older references (pre-1.1.0) were kept only where still true; check the rest against `esp-emu --help` before use.

Estado: rascunho factual (T01, Wave 0). Baseado em código/docs do repo e notas esp-emulator/esp-part discutidas. Sem alterações de código.

## 1. Objetivos

- Unificar num **perfil** a configuração de simulação: alvo/board, firmware/imagem, hardware/periféricos, opções de emulação, cosim/parts, execução e artefactos.
- Fluxo único de trabalho: **perfil → firmware → config HW/perif/emu → validar → run/monitor → guardar/exportar**.
- Integrar GUI (launcher GTK) com **canvas SDL embutido** (host de cosim in-process) e subprocesso **esp-emu** externo.
- Reduzir divergência entre `start.sh emu`, `simulator/launcher/*`, `simulator/emu/*` e binários pré-construídos (ex. KryonOS v3).
- Preparar extensibilidade para cosim **in-process** (`--cosim-board`, WASM parts) e mapeamento de armazenamento (`--sd`, esp-part/alternativas) sem acoplar tudo a TCP.

## 2. Componentes e papéis

| Componente | Papel | Estado atual |
|---|---|---|
| `start.sh emu` | CLI orquestradora: `info/merge/build-host/run/bootlog/ui/console`; defaults es3c28p (`simulator/build/merged-emu.bin`, ELF emu), `--chip esp32s3 --psram-size 8M`, `--uart-tcp 127.0.0.1:5555`, cosim TCP em `ui`, áudio `--i2s-wav simulator/logs/i2s_out.wav` | Implementado; merge/run/ui/bootlog focados em es3c28p (KryonOS v3 requer passos manuais conforme prep) |
| `simulator/launcher/*` (`sim_launcher.c`, `config.c/h`, `proc.c/h`) | GUI GTK3 de controlo/config; invoca `./start.sh emu ui`; gere start/stop/restart por process group; persiste `launcher.conf`; escreve `.hw_board`/`.hw_rotation` | Implementado (processo externo host+emu) |
| `simulator/emu/lcd_touch_host.c` + `panel_config.h` | Host cosim SDL2 (TCP/Unix): claims SPI LCD + I2C touch, framebuffer ILI9341-like, modelo FT6336G mínimo, rato→touch, debug, rotação HW/estado | Implementado; resolução/pinos/touch maioritariamente compilados por perfil/build |
| `simulator/emu/sim_host.c/.h` | API embebível/refactor do host: config, connect/disconnect, net thread, attach SDL standalone/native (`SDL_CreateWindowFrom`), step/render, input/resize/keys, state | Scaffolding/API presente (base para integração canvas) |
| `esp-emu` (binário, v1.1.0 no repo) | Motor de emulação ESP32-S3 separado: firmware/ELF, PSRAM/flash, UART/USB-CDC, Wi-Fi/BLE, I2S WAV, cosim `--cosim-board` e `--cosim-tcp`, control/trace/GDB conforme suporte | Externo; **não modificado pelo repo** |
| `board-*.json` / parts | Dispositivos cosim in-process (regfile/WASM/parts) via `--cosim-board` | `board-es3c28p.json` (FT6336/ES8311 regfiles); parts WASM específicas (LCD/GT911/etc.) não incluídas |

## 3. Modelo de perfil (conceitos)

Perfil = unidade versionável/exportável que descreve **o que** simular e **como** executar. Subdivide-se em Target, Firmware/Artefactos e Runtime.

### 3.1 Target

| Campo | Descrição | Exemplos / origem |
|---|---|---|
| `id` | Identificador estável da board/alvo | `es3c28p`, `esp32-s3-devkitc-1-n16r8`, `lilygo-t-deck`, `lilygo-t-hmi`, `m5stack-cardputer` |
| `chip` | SoC e modo flash/PSRAM | `esp32s3`, 16MB qio, PSRAM 8MB qio_opi/OPI (Cardputer: validar PSRAM vs board def) |
| `display` | Resolução, controlador, bus, pinos, rotação/inversão/CGRAM/offset | ES3C28P: 240x320 ILI9341 SPI; T-Deck: 320x240 ST7789; T-HMI: 240x320 ST7789 paralelo; Cardputer: 240x135 ST7789V2 SPI |
| `touch/input` | Controlador, bus/endereço, pinos/INT/RST, matriz teclado/trackball | FT6336G I2C 0x38; GT911 0x5D/0x14; XPT2046 SPI/bitbang; Cardputer matriz 74HC138+GPIO |
| `storage` | SD (SDMMC/SDSPI), partições FS (LittleFS/spiffs), imagem externa | SDMMC 4/1-bit, SPI CS; partição `spiffs/littlefs@0xc90000` (layouts variam) |
| `audio` | Codec I2C, I2S pinos/instância, mic/speaker | ES8311 0x18; `--i2s-wav/--i2s-wav-in/--i2s-instance` |
| `peripherals` | Wi-Fi/BLE/USB, bateria ADC, power/PMU, sensores/LoRa/GPS conforme board | Caps por board (ver matriz) |
| `cosim_map` | Claims/pins/devices esperados pelo host/board JSON/parts | `panel_config.h`, `board-*.json`, claims em `lcd_touch_host.c` |

### 3.2 Firmware e artefactos

| Campo | Descrição | Notas |
|---|---|---|
| `source` | `local-build` (PlatformIO env) ou `prebuilt-release` (split/merged) | es3c28p: `es3c28p`/`es3c28p_emu`; KryonOS v3: release assets |
| `env` | Ambiente PlatformIO (quando build local) | `es3c28p`, `es3c28p_emu` (`ESP_EMU_SKIP_AUDIO`) |
| `images` | Bootloader/partitions/app e offsets; merged | Padrão `0x0 / 0x8000 / 0x10000` (v3); merge es3c28p inclui FS em `0xc90000` quando presente |
| `elf` | Caminho ELF (símbolos/interceções/BLE hooks) | Opcional; releases v3 sem ELF (`--no-elf`) |
| `fs_image` | LittleFS/spiffs/data | `littlefs.bin`/`--littlefs`; SD_Card (~37MB) > spiffs 3.36MB (não cabe direto) |
| `checksums` | SHA256 por artefacto | Obrigatório para releases importados (tabela em prep KryonOS v3) |
| `build_flags` | Flags de emulação/guardas | `ESP_EMU_SKIP_AUDIO` salta áudio/SD_MMC/ADC no es3c28p_emu |

### 3.3 Runtime / execução

| Campo | Descrição | Mapeamento atual |
|---|---|---|
| `psram/flash/chip` | Args esp-emu | `--chip esp32s3 --psram-size 8M` |
| `console` | Modo consola | `--console auto|uart0|usb-cdc`, `--uart-tcp HOST:PORT` (default 127.0.0.1:5555) |
| `network` | Rede emulada | `--net user`, `--hostfwd`, `--wifi-ssid/password/auth` |
| `ble` | HCI/BLE | `--ble-hci tcp:localhost:9544` (com `--elf`) ou adaptador |
| `audio` | Captura/injeção I2S | `--i2s-wav`, `--i2s-wav-in`, `--i2s-instance` |
| `cosim_board` | JSON in-process | `--cosim-board <path>` (preferido, sem sockets) |
| `cosim_external` | Transporte externo (1.1.0) | `--cosim-tcp HOST:PORT`; ponte `--cosim-unix` via socat (host Unix, TCP interno mantém-se) |
| `control/debug` | Pausa/step/reset, trace, GDB, control | Suportado pelo ecossistema esp-emu/lab quando exposto; perfil deve registar endpoint/opções usadas |
| `host_view` | Escala, rotação HW, debug overlay, persistência estado | `--hw-rot`, `.hw_rotation/.hw_board`, escala launcher/SDL |
| `extra_args` | Pass-through para esp-emu | `-- <args>` / campo extra |

## 4. Fluxo end-to-end

`perfil → firmware → config HW/perif/emu → validar → run/monitor → guardar/exportar`

| Fase | Entradas | Ações | Saídas / artefactos |
|---|---|---|---|
| Perfil | board id, template, perfil existente | Selecionar target; definir defaults (display/touch/pins/PSRAM/console/cosim) | `profile` (ficheiro de config; hoje parcial em `launcher.conf` + `.hw_*`) |
| Firmware | env local ou URLs/assets | Build (`pio run -e <env>`) ou download; verificar SHA256; `merge` (esptool/esptool.py) | `.bin/.elf`, `merged*.bin`, checksums |
| Config HW/perif/emu | mapa de pinos, board JSON, opções rede/áudio/uart | Configurar cosim in-process (`--cosim-board`) e/ou externo; overrides suportados | args efetivos, paths JSON/imagens |
| Validar | perfil resolvido | Pré-checks: binários/paths existem, ELF quando requerido, portas livres, limites conhecidos (SDMMC, LCD part, release sem ELF/guardas) | relatório de validação (erros/warnings: `supported-now/needs-runtime-host/blocked-by-emu/conditional`) |
| Run/Monitor | perfil validado | `run` (serial), `bootlog [s]`, `ui` (host+emu), `console` (socat/nc); futuro: pause/step/reset/GDB/trace via endpoints suportados | logs (`simulator/logs/boot.log`, `i2s_out.wav`, bridge logs), screenshots, estado runtime |
| Guardar/Exportar | perfil + resultados | Persistir perfil/config; exportar bundle (perfil, refs de artefactos/checksums, comandos reproduzíveis) | `launcher.conf` / perfil exportado; opcional pacote de evidências |

## 5. Integração alvo: launcher GTK + canvas sim_host embutido + esp-emu subprocesso

Base: `docs/simulator/launcher/INTEGRATION_PLAN.md`.

- **Processo único GUI:** `sim_launcher` (ou `sim_app`) contém config + viewer.
- **emu:** subprocesso (separado) lançado com args do perfil; `proc.c` gere process group/stop.
- **sim_host embutido:** usa API `sim_host_*` (`sim_host.h/.c`) para connect/net/render/input/state.
- **Canvas:** `GtkDrawingArea` realized → `GDK_WINDOW_XID` → `SDL_CreateWindowFrom`; render via `g_timeout`/idle; eventos GDK traduzidos para `sim_host_handle_mouse/resize/key`.
- **Fluxo normal:** sem `lcd_touch_host` como janela/processo separado; standalone host mantido opcional durante migração.
- **Riscos/notas:** integração SDL+GTK sensível (X11 foco/resize/coords); resolução/pinos runtime exigem refactor (hoje `panel_config.h`/compile-time em muitos casos).

## 6. Integração com `start.sh emu`, `simulator/launcher/*`, `simulator/emu/sim_host`

| Área | Contrato / comportamento esperado |
|---|---|
| `start.sh emu info` | Paths, existência de artefactos, versões esp-emu/esptool |
| `start.sh emu merge [--env|--out|--littlefs]` | Produz merged (es3c28p default paths) |
| `start.sh emu build-host` | Compila `lcd_touch_host` (+ `sim_host.c`) com SDL2 |
| `start.sh emu run/bootlog [s]` | Execução headless/serial com `--firmware/--elf/--no-elf/--board/--uart-tcp` |
| `start.sh emu ui` | esp-emu `--cosim-tcp` + host SDL; reinicia em exit code 42 (mudança `hw_rot/board_state`); grava I2S WAV por default |
| `start.sh emu console` | Liga a `--uart-tcp` via socat/nc |
| Launcher | Campos: board, firmware source/path, cosim board JSON, LCD W/H override (persistido), HW rot, scale, UART TCP, `--net user`, I2S WAV, extra args; aplica via CLI/start.sh e ficheiros de estado |
| sim_host API | `sim_host_config_init/create/destroy`, `connect/start_net`, `video_init_standalone|video_attach_native`, `step/poll_sdl_events/render`, `handle_*`, `load/save_state`, `exit_code` (42=reboot) |

## 7. esp-emulator pre-1.1.0 / lab (capacidades relevantes ao perfil)

| Capacidade | Uso no perfil | Nota |
|---|---|---|
| CPU/mem/flash/PSRAM S3 | `--chip`, `--psram-size`, imagem merged | Validado no repo (8M) |
| UART / USB CDC | Consola/logs, `--uart-tcp` | `--console auto|uart0|usb-cdc` |
| Wi-Fi (STA/AP) | `--net user/tap`, `--wifi-*`, `--hostfwd` | Paridade funcional dependente de firmware/runtime |
| BLE | `--ble-hci` com `--elf` | Hooks com ELF |
| I2S WAV | `--i2s-wav/--i2s-wav-in` | Ficheiros; PCM tempo-real via cosim externo conforme ferramentas |
| Cosim in-process | `--cosim-board` (parts/devices/regfile/WASM) | **Sem TCP**; preferido |
| Cosim externo | `--cosim-tcp` | Needed for the host SDL/LCD live path (LCD and touch) on 1.1.0; no other transport |
| Drop/load bin, run/pause/step/reset | Control plane / debug session | Quando exposto por CLI/control API/lab; perfil deve persistir sessão/endpoint |
| Consola, GPIO watch/drive, regs/mem, perf | Observabilidade/debug | Via esp-emu/lab/tools suportadas; nem tudo integrado no launcher |
| Trace / GDB | `--trace`, GDB stubs/opções suportadas | Condicional a suporte/ELF |

Limits observed on 1.1.0: no WASM LCD part (the LCD comes from the cosim client over TCP); SD works through `--sd`; the WASM toolchain (wasm std) may be missing in this environment.

## 8. esp-part (WASM parts) como extensibilidade cosim

- **Formato:** parts WASM carregáveis em `--cosim-board` (ABI esp-part), dispositivos in-process.
- **Claims/interfaces:** GPIO, I2C, SPI, UART, PWM, ADC, EN/reset/níveis conforme ABI/part.
- **Wiring:** mapeamento pino↔sinal/device no board JSON/manifest do part; permite substituir regfiles/consumidores externos.
- **Aplicação prevista:** part ILI9341+FT6336 (display/touch) para eliminar dependência de `--cosim-tcp` no visual; modelos GT911/XPT2046, teclado matriz, etc.
- **Estado no repo:** não incluídas; dependem de toolchain e disponibilidade das parts.

## 9. Limites e condicionantes (factuais)

| Limite | Impacto | Mitigação/nota |
|---|---|---|
| Motor esp-emu externo | Não controlado pelo repo; capacidades variam por versão | Pin and test with the 1.1.0 binary in the repo; registar versão no perfil |
| SDMMC/SDSPI on the S3 | pre-1.1.0 had no `--sd`; 1.1.0 has it | Use `--sd` on 1.1.0 (`./start.sh card image`); `ESP_EMU_SKIP_AUDIO` no longer skips SD |
| Pinos/resolução host parcialmente compilados | Runtime config incompleta (`panel_config.h`, perfis em host) | Editar+rebuild host; refactor para config runtime (sim_host/panel dinâmico) |
| Releases sem ELF | Menos símbolos/interceções/BLE hooks/debug | `--no-elf`; obter ELF de build local do tag quando necessário |
| LCD/touch é diferencial nosso | Paridade depende de host/claims/parts, não só do core emu | Manter host/board JSON/parts por board; validar SPI/I2C routing |
| cosim TCP internal (1.1.0) | Ponte Unix não elimina TCP interno | Migrar para parts in-process quando viável |
| `simulator/SD_Card` (~37 MB) vs the 3.36 MB FS partition | Does not fit the FS partition | Use the `--sd` image (`./start.sh card image`, 128 MB default) |
| Firmware release sem guardas emu | Pode WDT/hang (I2S/SD/ADC/periféricos) | Validar por bootlog; builds emu com `ESP_EMU_SKIP_AUDIO` no es3c28p |

## 10. Critérios de aceitação do perfil (práticos)

- Perfil identifica univocamente target + firmware/artefactos + runtime + cosim.
- `validar` reporta limites (`blocked-by-emu`, `needs-runtime-host`, etc.) antes de correr.
- Comandos gerados equivalentes/reprodutíveis via `start.sh emu` ou args esp-emu explícitos.
- Artefactos e checksums rastreáveis; logs/screenshots associados à execução.
- Sem apagar/alterar ficheiros não relacionados; integração preserva fluxo es3c28p existente.
