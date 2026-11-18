# Simulator Capabilities Matrix (board × periférico × capacidade)

> Status 18 Nov 2026: reference binary is esp-emu 1.1.0. The SD rows and the LCD row are updated. Other cells were written for pre-1.1.0; where they are still right for 1.1.0, they stay. Cell texts are mostly Portuguese.

Estado: factual/prático (T01, Wave 0). Fontes: `docs/simulator/emu/HARDWARE-SUPPORT.md`, `docs/simulator/emu/KRYONOS-V3.0.0-PREP.md`, `docs/simulator/emu/NOTES.md`, `docs/simulator/emu/EMULATOR-NOTES.md`, `simulator/launcher/README.md`, `start.sh`, `simulator/emu/{script.sh,lcd_touch_host.c,sim_host.h,board-es3c28p.json,panel_config.h}` e referências esp-emulator/esp-part discutidas. Sem alterações de código.

## Legenda de estados

| Estado | Significado |
|---|---|
| `supported-now` | Utilizável no fluxo atual (repo + esp-emu 1.1.0), com configuração indicada |
| `needs-runtime-host` | Capacidade depende de host/part/board JSON/adaptação no nosso stack (não pronta ou parcial) |
| `blocked-by-emu` | Limitação conhecida do esp-emu pre-1.1.0/S3 (upstream/modelo ausente) |
| `conditional(version/tool)` | Depende de versão do emu, ELF, toolchain, build flags, rede/HCI, ou não validado end-to-end |

## Boards alvo

- `es3c28p` (KryonDeck alvo local emu)
- `esp32-s3-devkitc-1-n16r8`
- `lilygo-t-deck`
- `lilygo-t-hmi`
- `m5stack-cardputer`

## 1. Matriz — base compute/runtime

| Área / capacidade | es3c28p | esp32-s3-devkitc-1-n16r8 | lilygo-t-deck | lilygo-t-hmi | m5stack-cardputer | Notas |
|---|---|---|---|---|---|---|
| CPU / flash / PSRAM (ESP32-S3, 16MB típico, `--psram-size 8M`) | `supported-now` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Cardputer: flash/PSRAM segundo board def PlatformIO (`m5stack-stamps3`), validar sizing/expectativas; targets KryonOS v3: tentativa headless com PSRAM 8M |
| Boot serial/headless (build local es3c28p_emu) | `supported-now` | — | — | — | — | `ESP_EMU_SKIP_AUDIO` contorna áudio/SD/ADC; kernel atinge init em testes |
| Boot serial/headless (release KryonOS v3, sem guardas emu) | — | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Pode WDT/hang em I2S/SD/ADC/periféricos; validar por `bootlog`; sem ELF |
| Consola UART / `--uart-tcp` / `console` | `supported-now` | `supported-now` | `supported-now` | `supported-now` | `supported-now` | Requer expor `--uart-tcp` e cliente socat/nc; conteúdo depende do backend de consola do firmware |
| USB Serial JTAG / USB CDC | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `--console auto|usb-cdc`; es3c28p usa CDC on boot (`Serial`=USB CDC). Validar por alvo/firmware |
| Wi-Fi (STA/AP, `--net user`, `--wifi-*`, `--hostfwd`) | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Emulado; paridade STA/scan/serviços não totalmente validada |
| BLE (`--ble-hci`, com `--elf`) | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Hooks/interceções com ELF; releases sem ELF reduzem diagnóstico; HCI externo pode usar sockets |
| Debug/trace/GDB/control (`--gdb/--gdb-halt`, `--trace`, `--control-tcp`; pause/step/reset/regs/mem/perf) | `supported-now` (flags/config, pre-1.1.0) / `conditional(version/tool,needs-runtime-host)` | `supported-now` (flags/config, pre-1.1.0) / `conditional(version/tool,needs-runtime-host)` | `supported-now` (flags/config, pre-1.1.0) / `conditional(version/tool,needs-runtime-host)` | `supported-now` (flags/config, pre-1.1.0) / `conditional(version/tool,needs-runtime-host)` | `supported-now` (flags/config, pre-1.1.0) / `conditional(version/tool,needs-runtime-host)` | Flags presentes no esp-emu pre-1.1.0 local (pass-through possível); dados runtime/automação UI/step/regs/mem/perf dependem de suporte efetivo, ELF, parsing de trace/control e integração host/launcher |

## 2. Matriz — display, touch e input

| Área / capacidade | es3c28p | esp32-s3-devkitc-1-n16r8 | lilygo-t-deck | lilygo-t-hmi | m5stack-cardputer | Notas |
|---|---|---|---|---|---|---|
| Display SPI (ILI9341/ST7789*) live via host cosim externo | `supported-now` (es3c28p, cosim client) | `needs-runtime-host` | `needs-runtime-host` | — | `needs-runtime-host` | es3c28p: host SDL existe (ILI9341-like) mas visual live depende de `--cosim-tcp` e routing SPI (lacuna observada em pre-1.1.0 sem dados SPI em alguns testes); outros: pinos/res/CGRAM/offset/rotação diferem do perfil compilado |
| Display 8-bit paralelo (ST7789, T-HMI) | — | — | — | `needs-runtime-host` | — | Sem transport/consumer paralelo no stack atual; maior gap |
| Touch I2C FT6336G (init/poll básico) | `supported-now` | `needs-runtime-host` | — | — | — | es3c28p: `board-es3c28p.json` regfile (in-process) + modelo mínimo no host; interação completa depende de modelo/part |
| Touch I2C GT911 (0x5D/0x14) | — | — | `needs-runtime-host` | — | — | Protocolo/registos diferentes de FT6336; regfile/host atual insuficiente |
| Touch resistivo XPT2046 (SPI/bitbang) | — | `needs-runtime-host` | — | `needs-runtime-host` | — | Modelo não implementado no host atual |
| Teclado matriz / coprocessor I2C / trackball | — | — | `needs-runtime-host` | — | `needs-runtime-host` | T-Deck: keyboard I2C 0x55, trackball GPIO; Cardputer: matriz 74HC138+GPIO (sem touch) |
| Resolução/pinos/touch/orientação runtime no host SDL | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | Hoje maioritariamente em `panel_config.h`/compilado; launcher persiste W/H override mas runtime host não aplicado totalmente; rebuild necessário para mudar perfil/pinos |

## 3. Matriz — buses, GPIO e periféricos

| Área / capacidade | es3c28p | esp32-s3-devkitc-1-n16r8 | lilygo-t-deck | lilygo-t-hmi | m5stack-cardputer | Notas |
|---|---|---|---|---|---|---|
| GPIO / matriz IO / watch-drive (cosim) | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `--cosim-board`/parts/consumers conforme disponibilidade; WS2812 timing específico não incluído |
| I2C controlador + devices regfile (in-process) | `supported-now` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | es3c28p: FT6336+ES8311 em I2C0/1; outros alvos requerem stubs/parts e endereços corretos |
| SPI controlador / GP-SPI | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Cosim SPI via parts/consumers; display/touch SPI dependem de implementação específica |
| UART (periférico/fw) | `supported-now` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Modelado no emu; casos de uso (GPS, etc.) dependem de device/bridge |
| PWM / LEDC / RMT / PCNT / MCPWM / timers | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Modelados em geral; dispositivos específicos (WS2812) requerem part dedicada |
| ADC / bateria | `needs-runtime-host` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | es3c28p: contornado em `ESP_EMU_SKIP_AUDIO` (valores seguros) para evitar WDT no arranque; sem modelo real |

## 4. Matriz — áudio e armazenamento

| Área / capacidade | es3c28p | esp32-s3-devkitc-1-n16r8 | lilygo-t-deck | lilygo-t-hmi | m5stack-cardputer | Notas |
|---|---|---|---|---|---|---|
| Áudio I2S (WAV out/in por ficheiro, `--i2s-wav/--i2s-wav-in/--i2s-instance`) | `supported-now` (flag/config) | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Flags presentes no esp-emu pre-1.1.0; captura/entrada por ficheiro utilizável quando configurada; firmware/rotas/init e paridade tempo-real condicionais |
| Áudio I2S tempo-real PCM / codec completo | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | I2C codec via regfile; PCM tempo-real via cosim externo/ferramentas; boot stock pode bloquear em I2S (ver guardas) |
| LittleFS / spiffs interno (imagem em merge) | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Merge pode incluir FS (`0xc90000` / layout da partição); sem imagem válida pode falhar mount (não sempre bloqueante) |
| SDMMC / SDSPI (SD card on the emu S3) | `supported-now` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | esp-emu 1.1.0 `--sd` (SDHC image, FAT32). es3c28p tested: the firmware mounts /sd. Swap during a run not modelled. Other boards untested. |
| `--sd` / image from `simulator/SD_Card` | `supported-now` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `./start.sh card image` builds `simulator/emu/storage/sd.img` (128 MB default). |
| esp-part / alternativas de armazenamento (WASM part, backing image custom) | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | Extensibilidade futura; requer part/tooling/suporte e integração de wiring |

## 5. Matriz — cosim e formatos de firmware

| Área / capacidade | es3c28p | esp32-s3-devkitc-1-n16r8 | lilygo-t-deck | lilygo-t-hmi | m5stack-cardputer | Notas |
|---|---|---|---|---|---|---|
| Cosim in-process `--cosim-board` (sem TCP na camada devices) | `supported-now` (flag/config) | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | Flag presente no esp-emu pre-1.1.0; `board-es3c28p.json` existe (regfiles I2C). Outros boards/parts WASM específicos requerem JSON, ABI/toolchain e wiring. |
| Cosim externo `--cosim-tcp`/`--cosim-log` (1.1.0) | `supported-now` (flag/transporte) / `needs-runtime-host` (fidelidade) | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Flags presentes no esp-emu pre-1.1.0; usado por host SDL/UI. Execução/visualização completa (ex. SPI LCD, WASM parts) depende de consumer/part/modelo. |
| Ponte Unix host↔emu (`--unix` no host, socat UNIX↔TCP) | `conditional(version/tool,socat)` | `conditional(version/tool,socat)` | `conditional(version/tool,socat)` | `conditional(version/tool,socat)` | `conditional(version/tool,socat)` | `sim_host`/CLI/launcher suportam Unix socket no lado do host; **esp-emu pre-1.1.0 não tem `--cosim-unix` (TCP interno)**, ponte socat necessária |
| esp-part WASM (GPIO/I2C/SPI/UART/PWM/ADC/EN, wiring) | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | `needs-runtime-host` | Parts não incluídas; toolchain/ABI e claims por device necessários (ex. LCD+touch) |
| Firmware merged (bootloader+partitions+app[+fs]) | `supported-now` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | es3c28p: `start.sh emu merge/ui`; v3: merge de split assets documentado |
| Firmware split (bootloader/partitions/app com offsets) | `conditional(version/tool)` | `supported-now` | `supported-now` | `supported-now` | `supported-now` | Assets KryonOS v3.0.0 publicados com SHA256; requer merge para esp-emu |
| ELF (símbolos, BLE hooks, debug) | `supported-now` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Builds locais fornecem ELF; releases v3 **sem ELF** |
| UF2 | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | `conditional(version/tool)` | Não é formato primário no fluxo esp-emu deste repo; converter para bin/merged quando aplicável |

## 6. Limites transversais (resumo)

- **Motor externo:** esp-emu 1.1.0 (binário no repo) governa capacidades core/cosim; upgrades alteram matriz. Flags de debug/trace/cosim/uart/i2s podem estar disponíveis para pass-through mesmo quando runtime/telemetria/paridade permanece `conditional`/`needs-runtime-host`.
- **S3 SDMMC/SDSPI:** `--sd` in 1.1.0 (es3c28p tested). In pre-1.1.0 it was `blocked-by-emu`.
- **Host SDL:** diferencial do projeto; pinos/resolução/mapeamentos ainda parcialmente compilados (`panel_config.h`), lacunas de routing/modelo (SPI LCD, GT911, XPT2046, paralelo, teclado).
- **Releases sem ELF/guardas:** menor observabilidade e risco de boot hang; validar por `bootlog`.
- **Backends externos:** QEMU (IDF), Wokwi, upstream esp-emulator e outras opções exigem capability detection e validação por versão/tool/perfil (ver `docs/reports/external-support-research.md`).
- **Artefactos/logs:** `simulator/logs/`, `simulator/build/`, `simulator/screenshots/` (existentes no workspace) suportam evidência; UART TCP não suporta `shot` binário (usar screenshot SDL).

## 7. Referências rápidas

- `docs/simulator/emu/HARDWARE-SUPPORT.md` — mapa por hardware es3c28p, board JSON, comandos e limites.
- `docs/simulator/emu/KRYONOS-V3.0.0-PREP.md` — assets/offsets/SHA256 v3, specs por board, viabilidade e passos.
- `simulator/launcher/README.md` / `INTEGRATION_PLAN.md` — parâmetros editáveis vs compilados, integração alvo.
- `docs/simulator/emu/NOTES.md`, `docs/simulator/emu/EMULATOR-NOTES.md` — achados de boot/cosim/SPI/UI.
- `start.sh emu help` — CLI de merge/run/bootlog/ui/console.
