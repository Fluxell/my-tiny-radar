#pragma once

// ─── WiFi Access Point (setup mode) ───────────────────────────────────────────
#define AP_SSID         "TinyRadar"
#define AP_PASSWORD     ""              // empty string = open network
#define AP_IP           "192.168.4.1"

// ─── Display ──────────────────────────────────────────────────────────────────
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define LCD_BL_PIN      40             // backlight GPIO

// ─── Radar tile settings ──────────────────────────────────────────────────────
#define TILE_ZOOM       9              // zoom 9 ≈ 46-mile radius at 240 px
#define TILE_SIZE_PX    256            // RainViewer tile pixel dimension
#define TILE_GRID       2              // fetch a 2×2 grid
#define STITCH_SIZE     (TILE_SIZE_PX * TILE_GRID)   // 512

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
#define HTTP_TIMEOUT_MS 15000
