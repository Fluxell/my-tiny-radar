// TFT_eSPI User_Setup for Waveshare ESP32-S3-LCD-1.28 (GC9A01, 240×240 round)
//
// HOW TO USE:
//   1. Locate your TFT_eSPI library folder (typically in Arduino/libraries/TFT_eSPI/)
//   2. Rename the existing User_Setup.h to User_Setup.h.bak
//   3. Copy THIS file there, renamed as User_Setup.h
//
// VERIFY PINS against your board's schematic — the values below match the
// most common Waveshare ESP32-S3-LCD-1.28 wiring. The "B" variant may differ.

#define GC9A01_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// SPI pins
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_CS    10
#define TFT_DC     8
#define TFT_RST    9
// Backlight is controlled in code (LCD_BL_PIN in config.h), not by TFT_eSPI

// Fonts to include
#define LOAD_GLCD   // built-in 5×7 font (Font 1)
#define LOAD_FONT2  // small 8px font (Font 2)
#define LOAD_FONT4  // medium font (Font 4)

// SPI bus frequency — 40 MHz is safe; try 80 MHz if you want faster redraws
#define SPI_FREQUENCY  40000000

// Touch screen — not used on this board
// #define TOUCH_CS  XX
