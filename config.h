#pragma once

// Display pins
#define PIN_SCLK   D8
#define PIN_MOSI   D10
#define PIN_DC     D3
#define PIN_CS     D2
#define PIN_RST    D4
#define PIN_BL     D7

// Touch pins
#define PIN_TP_SDA D6
#define PIN_TP_SCL D5
#define PIN_TP_RST D0
#define PIN_TP_INT D1

// Display
#define DISPLAY_CX 120
#define DISPLAY_CY 120

// Colours (RGB565)
#define COL_BG       0x0000
#define COL_WHITE    0xFFFF
#define COL_GREEN    0x07E0
#define COL_RED      0xF800
#define COL_ORANGE   0xFD20
#define COL_GREY     0x7BEF
#define COL_DARKGREY 0x39E7
#define COL_RING     0x07E0

// Timing
#define RING_WARN_SECS 20

// NTP
#define NTP_SERVER      "pool.ntp.org"
#define NTP_OFFSET_SEC  -25200
#define NTP_INTERVAL_MS 10000
