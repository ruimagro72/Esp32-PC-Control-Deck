# Emulador ESP32-PC-Control-Deck (esp-emulator) — Notas e análise

> **Historical analysis (16 Nov 2026), superseded.** Written in Portuguese. It describes an earlier esp-emu build and older paths (`simulator/esp-emu/`). Current state (18 Nov 2026): reference binary esp-emu 1.1.0; SD through `--sd` (`./start.sh card image`); LCD and touch through the cosim client. See `HARDWARE-SUPPORT.md`. Kept as a record of the work.

Documento de referência para o trabalho de emulação/simulação do projeto (ES3C28P / KryonOS / Deck). Criado para consolidar decisões, testes, documentação relevante e próximos passos.

> Contexto: objetivo é simular/emular hardware e executar o **mesmo firmware/KryonOS** real (UI gerada pelo código, não pelo host), com SD mapeada idealmente a `simulator/SD_Card`. Trabalho concentrado em `simulator/esp-emu/`.

## 1) Decisão de abordagem

- **Escolhida:** `espressif/esp-emulator` (prioritária) por suportar ESP32-S3, consumir imagem flash *merged* e ter menor complexidade de integração inicial face ao fluxo QEMU/ESP-IDF para este build Arduino/PlatformIO.
- **Excluída da análise:** recompilação nativa com HAL/stubs (estilo `Launcher/native`) — não considerada o caminho correto neste momento.
- **Comparação resumida:**
  - **esp-emulator:** corre binário real (Duktape/KryonOS reais), ESP32-S3 explícito, bom para PoC; lacunas prováveis em periféricos específicos da placa (display SPI, touch, SD, áudio, etc.) a validar/colmatar.
  - **QEMU (Espressif):** também corre firmware real, bom debug, mas encaixe ESP32-S3/periféricos e integração com artefactos PlatformIO menos direta (mais complexidade).
  - **Simulador Node/V8:** corre apps JS (≠ firmware/Duktape), baixa fidelidade e risco de UI inventada — não cumpre o objetivo de paridade.

## 2) Alvo e artefactos

- **Firmware:** `env:es3c28p` (ESP32-S3, 16 MB flash QIO, PSRAM OPI/qio_opi, ILI9341 240x320, touch FT6336G I2C, SD_MMC 4-bit, áudio I2S/ES8311, Wi-Fi, BLE, USB CDC).
- **Build:** `./start.sh build` ou `pio run -e es3c28p` em `firmware/`.
- **Artefactos:** `firmware/.pio/build/es3c28p/` (`bootloader.bin`, `partitions.bin`, `firmware.bin`, `firmware.elf`, `littlefs.bin` quando gerado).
- **Partições (detetadas):** nvs@0x9000, otadata@0xe000, app0@0x10000, app1@0x650000, spiffs/littlefs@0xc90000, coredump@0xff0000.
- **Imagem merged (exemplo):**
  ```bash
  esptool.py --chip esp32s3 merge_bin -o simulator/esp-emu/merged.bin \
    --flash_mode qio --flash_freq 80m --flash_size 16MB \
    0x0 firmware/.pio/build/es3c28p/bootloader.bin \
    0x8000 firmware/.pio/build/es3c28p/partitions.bin \
    0x10000 firmware/.pio/build/es3c28p/firmware.bin
  ```
  (Incluir partição de dados/LittleFS em 0xc90000 se necessário.)

## 3) Ferramentas e versões em uso

- **`esp-emu` v0.51.0** (sistema) e **`esp-emu` pre-1.1.0** em `simulator/esp-emu/esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu` (recomendado; adiciona *virtual parts*/co-sim).
- **PlatformIO/esptool** disponíveis no ambiente.
- Execução típica: `--chip esp32s3 --psram-size 8M --elf <firmware.elf> --firmware <merged.bin>`.

## 4) Resultados de testes (resumo)

### v0.51.0 e pre-1.1.0 (binário real)
- **Série/UART:** funcional (logs ROM/firmware visíveis).
- **Rede (emulador):** `--net user` ativo (fallback; SoftAP e backend prontos). Wi-Fi do emulador configurável (`--wifi-ssid/password/auth`).
- **BLE (emulador):** com `--elf`, interceções de símbolos BLE detetadas (9 funções no teste) — integração de baixo nível presente.
- **Boot *stock* (es3c28p):** pânico por *interrupt wdt timeout* em `AudioEngine::init()` → `i2s_driver_install/i2s_start`.
- **Boot com contornos de emulação (build com `-DESP_EMU_SKIP_AUDIO`):** saltar áudio I2S, saltar init SD (SD_MMC) e *stub* de leituras ADC de bateria permitiu progredir até **`HarixKernel initialized successfully`** e execução contínua sem pânico imediato (sem UI visível; LittleFS falhou na imagem sem dados válidos na partição).

### Contornos aplicados ao firmware para testes (guardados por `ESP_EMU_SKIP_AUDIO`)
- `firmware/src/main.cpp`: não inicializar/reproduzir áudio no arranque.
- `firmware/src/FileSystem/FileSystem.cpp`: não inicializar SD em ES3C28P (evita bloqueio SD_MMC sem hardware).
- `firmware/src/Hal/Boards/es3c28p/BoardConfig.cpp`: valores seguros para bateria/ADC (evita bloqueio).

> Estes contornos são apenas para viabilizar testes no emulador; builds normais não devem depender deles sem a flag.

## 5) Estado por componente (o que funciona / limitações)

| Componente | Estado | Notas |
|---|---|---|
| Série/UART | **Funcional** | Consola utilizável (`--console`, `--uart-tcp`). |
| Wi-Fi/Rede (emulador) | **Disponível, paridade a validar** | `--net user/tap/vmnet`, SoftAP, hostfwd, mDNS; validar fluxos reais do firmware (WebManager/DeckLink) em runtime estável. |
| BLE (emulador) | **Integração presente, funcionalidade a validar** | `--ble-hci tcp` (Bumble) ou `hci0` com `--elf`; falta validar casos de uso do projeto. |
| LCD ILI9341 (SPI) + touch | **Sem saída direta out-of-the-box** | Requer *virtual part*/bridge (cosim SPI+GPIO+I2C). Host inicial criado; afinação pendente. |
| Áudio (I2S) | **Problemático no boot *stock*; bridges disponíveis** | `--i2s-wav`/`--i2s-wav-in` (ficheiros) e, via cosim TCP, possibilidade de PCM em tempo real (consumidor TCP pode reclamar pads I2S). |
| SD (cartão ES3C28P, SD_MMC) | **Sem mapeamento direto** | Lacuna: expor `simulator/SD_Card` como armazenamento utilizável pelo firmware. |
| LED (WS2812 GPIO 42) / GPIO/LEDC/RMT | **Modelados em geral; WS2812 específico a implementar** | LED simples/PWM viável via *part* (ex. sg90 como referência PWM); WS2812 requer modelo dedicado. |

## 6) Documentação relevante analisada (`esp-emulator`)

- **`docs/guides/BLE.md`:** `--ble-hci tcp:localhost:9544` com Bumble (sem hardware) ou `hci0`; `--elf` obrigatório; `tools/bumble_test.py`, `tools/vhci_bridge.py`. Bom para validar BLE do firmware.
- **`docs/guides/NETWORKING.md`:** `--net user` (DHCP/DNS/TCP/UDP/mDNS/IPv6/ICMP *spoof*, `hostfwd`, `mdns-nat`), Wi-Fi SoftAP (WPA2/WPA3/Enterprise), firmware em STA/AP, TAP/vmnet para L2 real. Essencial para testar serviços de rede.
- **`docs/guides/HOST-INTERFACES.md`:** co-simulação `--cosim-tcp`/`--cosim-board` (GPIO/I2C/SPI, proto `esp_cosim.proto`), ferramentas (`cosim-lcd-view.py`, `cosim-tcp-i2c.py`, `cosim-tcp-i2s.py`), `--control-tcp`, UART, GDB, `--trace`. Nota: **consumidores TCP podem reclamar pads I2S** para áudio em tempo real; *WASM parts* não (ainda). Base para UI/touch/áudio.
- **`docs/guides/HOSTED.md` / `MATTER.md`:** cenários específicos (P4↔C6 SDIO, Matter) — não prioritários para ES3C28P/KryonOS atual.
- **`examples/parts/sg90`:** *part* PWM (LEDC/MCPWM) — referência para modelar saídas PWM/indicadores (LED PWM); WS2812 exigirá abordagem própria.
- **`test_apps`:** exemplos de firmware+cosim (gpio_button, i2c_sensor, spi_tmp123/spi_qspi, pwm_servo, wpa3_sta, etc.) úteis como padrões.
- **Referência externa:** `lcgamboa/qemu` (PICSimLab, Wi-Fi/ESP-NOW, biblioteca) — apenas referência.

## 7) Artefactos criados em `simulator/esp-emu/`

- `esp-emu-1.1.0-x86_64-unknown-linux-gnu/esp-emu` — binário pre-1.1.0.
- `script.sh` — `info|merge|build-host|run|bootlog|ui|console` (usa pre-1.1.0 e `esptool` com flags não-deprecadas; `ui` lança cosim-tcp + host UI e captura I2S com `--i2s-wav` por defeito; `bootlog` usa `timeout -k`).
  - **Série (UART):** `run|ui|bootlog` aceitam `--uart-tcp [HOST:PORT]` (por omissão `127.0.0.1:5555`) para expor a consola série via TCP; `./script.sh console [--target HOST:PORT]` liga-se com `socat`/`nc`. Nota: no alvo es3c28p (`ARDUINO_USB_CDC_ON_BOOT=1`) a consola de comandos do firmware pode estar em USB CDC; pode ser necessário ajustar `--console` no `esp-emu` conforme o backend ativo.
  - **Wi-Fi/Rede:** encaminhamento de flags do `esp-emu` (ex.: `--net user`, `--hostfwd tcp::8080-:80`, `--wifi-*`) via `--`/args; `--net user` é o modo típico em loopback para validar serviços (WebManager/DeckLink) com *port forwarding*.
  - **Cosim por socket Unix (ponte):** `./script.sh ui --cosim-unix <path>` cria uma ponte `socat` (`UNIX-LISTEN:<path>` → `TCP:127.0.0.1:<port>`) e liga o host com `--unix <path>`. O `esp-emu` pre-1.1.0 continua a usar cosim TCP internamente; o host (`lcd_touch_host`, com `--unix`) e o fluxo local passam a operar sobre socket Unix.
- `panel_config.h` — configuração única do painel/periféricos do host (resolução, pinos, toque, espelho, mapeamento X/Y/swap, escala, nome/porta cosim), sobreponível por `-D`.
- `lcd_touch_host.c` / `lcd_touch_host` — cliente cosim nativo (SDL2, C): LCD ILI9341 via SPI (CS/DC/BL/SCLK/MOSI/MISO) + touch FT6336G via I2C com rato→touch, INT, suporte TCP/Unix (`--unix`), e modo debug (`--debug`).
- `merged.bin` — imagem merged para testes.
- `NOTES.md` — notas de validação iniciais.
- Logs `boot*.log` — evidências de arranque.

## 8) Lacunas conhecidas / riscos

- **UI/touch (em progresso):** protocolo cosim host→emulador corrigido (mensagens *bare*); `RegisterAck` recebido e **I2C 0x38 (touch, addr 0x38) detetado e respondido** pelo host via cosim-tcp (272 pedidos em ~16s, sem `no answer`). Níveis GPIO (DC/BL/INT/RST) chegam como `HW/GPIO/*/LEVEL`. **SPI do LCD não chega ao host:** ao anexar o consumidor o emulador regista `[INFO cosim] SPI transfers are answered by parts now` e não foram observados `BusRequest` SPI (0) — sem dados SPI não há render ILI9341, pelo que a janela SDL permanece preta. Causa provável: claim/routing SPI na inscrição cosim ou encaminhamento SPI para consumidores TCP; requer investigação (campos/proto do claim SPI, pinos/bus/host, e comportamento do `esp-emu` pre-1.1.0).
- **Áudio:** boot *stock* bloqueia em I2S; `--i2s-wav*` e I2S via cosim disponíveis, mas integração em tempo real/playback/captura no host por concluir.
- **Wi-Fi/Rede:** `--net user` e *port forwarding* (`--hostfwd`) integrados no fluxo do script; **validação funcional** ponta-a-ponta do firmware (WebManager/DeckLink/serviços) ainda pendente.
- **Série/consola:** exposição via `--uart-tcp` e `console` disponíveis no script; a consola de comandos do firmware pode depender do backend USB CDC no es3c28p (ajuste de `--console` a validar).
- **Cosim:** transporte Unix no lado host/ponte implementado; `esp-emu` pre-1.1.0 não suporta cosim Unix nativo (continua TCP internamente).
- **SD (`simulator/SD_Card`):** sem mecanismo direto confirmado no emulador; essencial definir estratégia.
- **Periféricos específicos:** I2S/ADC/teclado/trackball/USB/timings podem exigir contornos ou suporte adicional.
- **Multi-chip:** possível a prazo conforme suporte do emulador, mas não endereçado.

## 9) Próximos passos sugeridos

1. **Afinar UI+touch:** correr `./script.sh ui`, verificar registo/ACK cosim e chegada de transações SPI/I2C, ajustar `lcd_touch_host.c` (bus/CS, fases SPI, DC/pin_levels, registos/leituras FT6336G) até obter imagem e interação.
2. **Áudio no PC:** consolidar captura (`--i2s-wav`) e avaliar *bridge* I2S em tempo real via cosim-tcp (modelo `cosim-tcp-i2s.py`) para saída/entrada.
3. **Validar Wi-Fi:** runtime estável + `--net user`/`hostfwd` para exercitar WebManager/DeckLink/serviços.
4. **Validar BLE:** `--ble-hci tcp` + Bumble (e depois hci0 se necessário) com cenários do firmware.
5. **SD:** investigar/implementar estratégia para `simulator/SD_Card` (imagem, partição de dados ou mecanismo suportado).
6. **LED/indicadores:** *part* simples (GPIO/LEDC) e, se necessário, modelo WS2812.
7. **Integrar em `start.sh`:** quando estável, expor comandos de emulação no entry point único.

---
*Última atualização deste documento: gerado durante sessão de análise/PoC; rever após afinações.*
