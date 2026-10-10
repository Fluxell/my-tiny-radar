#pragma once

// ─── WiFi Access Point (setup mode) ───────────────────────────────────────────
#define AP_SSID         "TinyRadar"
#define AP_PASSWORD     ""              // empty string = open network
#define AP_IP           "192.168.4.1"

// ─── Display ──────────────────────────────────────────────────────────────────
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define LCD_BL_PIN      40             // backlight GPIO
#define LCD_RST_PIN     12             // display reset — must be held HIGH during deep sleep
#define LCD_CS_PIN      9              // SPI chip-select — must be held HIGH during deep sleep

// ─── Buttons ──────────────────────────────────────────────────────────────────
#define BTN_LEFT_PIN    4              // short=zoom out, hold=soft reset
#define BTN_RIGHT_PIN   5              // short=zoom in,  hold=enter setup mode
                                       // NOTE: GPIO5 is silkscreened TP_INT (touch panel
                                       // interrupt) but no touch code exists in this project.
#define BTN_HOLD_MS     2000           // ms held to trigger the "long press" action
#define BTN_DEBOUNCE_MS 40             // settle time before trusting a wake-triggered read
#define BTN_LINGER_MS   30000          // stay awake + WiFi connected this long after a tap,
                                       // watching for more taps, before actually sleeping

// ─── Radar tile settings ──────────────────────────────────────────────────────
#define TILE_ZOOM       7              // default zoom — used only as the NVS fallback default
#define ZOOM_MIN        4              // widest useful view
#define ZOOM_MAX        7              // hard cap — closest allowed zoom level
#define TILE_SIZE_PX    256            // RainViewer tile pixel dimension
#define TILE_GRID       2              // fetch a 2×2 grid
#define STITCH_SIZE     (TILE_SIZE_PX * TILE_GRID)   // 512

// ─── Stadia Maps base layer ───────────────────────────────────────────────────
#define STADIA_API_KEY  "c2184315-b476-4bbb-b5bc-01a423863456"              // get a free key at client.stadiamaps.com
#define STADIA_STYLE    "stamen_toner_lite"

// ─── RainViewer ───────────────────────────────────────────────────────────────
#define RV_API_URL      "https://api.rainviewer.com/public/weather-maps.json"
#define RV_TILE_COLOR   4              // 2=Universal Blue, 4=TWC, 6=Meteored, 7=NEXRAD
#define RV_TILE_OPTIONS "1_1"          // smooth=1, show-snow=1

// ─── Refresh bounds (minutes) ─────────────────────────────────────────────────
#define REFRESH_MIN     5
#define REFRESH_MAX     60
#define REFRESH_DEFAULT 15

// ─── NVS namespace ────────────────────────────────────────────────────────────
#define NVS_NAMESPACE   "tinyradar"

// ─── HTTP timeouts (ms) ───────────────────────────────────────────────────────
#define HTTP_TIMEOUT_MS 30000
