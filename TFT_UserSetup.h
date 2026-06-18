// TFT_eSPI User_Setup for Waveshare ESP32-S3-LCD-1.28 (GC9A01, 240×240 round)
// Pin assignments verified against ref/esp32_s3_all_pins.csv
//
// HOW TO USE:
//   1. Locate your TFT_eSPI library folder:
//      Windows: Documents\Arduino\libraries\TFT_eSPI\
//   2. Rename the existing User_Setup.h to User_Setup.h.bak
//   3. Copy THIS file there, renamed as User_Setup.h

#define GC9A01_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// SPI pins (from esp32_s3_all_pins.csv)
#define TFT_DC    8   // LCD_DC
#define TFT_CS    9   // LCD_CS
#define TFT_SCLK  10  // LCD_CLK
#define TFT_MOSI  11  // LCD_MOSI
#define TFT_RST   12  // LCD_RST
// Backlight (GPIO40 / LCD_BL) is controlled in code via LCD_BL_PIN in config.h

// Fonts to include
#define LOAD_GLCD   // built-in 5×7 font (Font 1)
#define LOAD_FONT2  // small 8px font (Font 2)
#define LOAD_FONT4  // medium font (Font 4)

#define SPI_FREQUENCY  40000000
