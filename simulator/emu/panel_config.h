#ifndef PANEL_CONFIG_H
#define PANEL_CONFIG_H

/*
 * Default panel/peripheral config for cosim host (es3c28p-compatible).
 * Compile-time defaults below; sim_host now applies LCD W/H, scale, rotation,
 * mirror and touch mapping/calibration at runtime via SimHostConfig/API/CLI/env.
 * Bus pins, GPIO claim pins, touch I2C addr/controller and protocol behavior
 * remain compile-time/conditional here (see sim_host.c notes).
 */

// --- Dimensões do painel (nativas, em píxeis) ---
#ifndef PANEL_WIDTH
#define PANEL_WIDTH 240
#endif
#ifndef PANEL_HEIGHT
#define PANEL_HEIGHT 320
#endif

// --- Escala/orientação da apresentação ---
#ifndef PANEL_WINDOW_SCALE
#define PANEL_WINDOW_SCALE 2
#endif
// Espelho da imagem apresentada (ajuste de orientação do painel)
#ifndef PANEL_MIRROR_X
#define PANEL_MIRROR_X 1
#endif
#ifndef PANEL_MIRROR_Y
#define PANEL_MIRROR_Y 0
#endif

// Transformação das coordenadas de toque reportadas ao firmware (espaço raw do painel)
// Ajustar se o toque seleciona o alvo errado (ex.: cima/baixo ou esquerda/direita trocados)
#ifndef PANEL_TOUCH_INVERT_X
#define PANEL_TOUCH_INVERT_X 0
#endif
#ifndef PANEL_TOUCH_INVERT_Y
#define PANEL_TOUCH_INVERT_Y 0
#endif
#ifndef PANEL_TOUCH_SWAP_XY
#define PANEL_TOUCH_SWAP_XY 0
#endif

// --- LCD (SPI) ---
#ifndef PANEL_LCD_CS
#define PANEL_LCD_CS   10
#endif
#ifndef PANEL_LCD_DC
#define PANEL_LCD_DC   46
#endif
#ifndef PANEL_LCD_BL
#define PANEL_LCD_BL   45
#endif
#ifndef PANEL_LCD_MOSI
#define PANEL_LCD_MOSI 11
#endif
#ifndef PANEL_LCD_MISO
#define PANEL_LCD_MISO 13
#endif
#ifndef PANEL_LCD_SCLK
#define PANEL_LCD_SCLK 12
#endif

// --- Touch (I2C, ex. FT6336G) ---
#ifndef PANEL_TOUCH_ADDR
#define PANEL_TOUCH_ADDR 0x38
#endif
#ifndef PANEL_TOUCH_SCL
#define PANEL_TOUCH_SCL  15
#endif
#ifndef PANEL_TOUCH_SDA
#define PANEL_TOUCH_SDA  16
#endif
#ifndef PANEL_TOUCH_INT
#define PANEL_TOUCH_INT  17
#endif
#ifndef PANEL_TOUCH_RST
#define PANEL_TOUCH_RST  18
#endif

// --- Cosim / identificação ---
#ifndef PANEL_COSIM_NAME
#define PANEL_COSIM_NAME "es3c28p-lcd-touch"
#endif
#ifndef PANEL_COSIM_PORT
#define PANEL_COSIM_PORT "5591"
#endif

#endif /* PANEL_CONFIG_H */
