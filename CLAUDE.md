# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Arduino sketch for an ESP32-S3 + Waveshare 1.28" round GC9A01 display (240×240) that fetches RainViewer radar tiles over WiFi and renders them centered on a configured location.

## Build & Flash

This is an Arduino IDE project — there is no `make` or `cmake`. Compile and upload through **Arduino IDE 2.x** with the ESP32 Arduino core installed.

Board settings (Tools menu):
- **Board:** ESP32S3 Dev Module (Espressif core) or Waveshare ESP32-S3-LCD-1.85
- **Flash Size:** 4MB  ← actual chip size (0x400000); larger settings fail to boot
- **PSRAM:** Disabled  ← board has no accessible PSRAM; streaming renderer used instead
- **Partition Scheme:** Huge APP (3MB No OTA/1MB SPIFFS)  ← most app space; no FATFS needed

Required libraries (Library Manager):
- `TFT_eSPI` by Bodmer
- `PNGdec` by Larry Bank
- `ArduinoJson` v6 (if v7 is installed, change `DynamicJsonDocument` → `JsonDocument` in `radar.cpp`)

Before compiling, copy `TFT_UserSetup.h` → `Arduino/libraries/TFT_eSPI/User_Setup.h` (replaces the library's existing file).

## Architecture

The sketch has two runtime modes selected at boot based on whether NVS holds a saved config:

**Setup mode** (`setup_server.cpp`) — ESP32 acts as a WiFi AP (`TinyRadar`). Serves a single-page HTML form at `192.168.4.1` that collects home WiFi credentials, lat/lon (with client-side ZIP→coords lookup via zippopotam.us), and refresh rate. On submit, saves to NVS and reboots.

**Radar mode** (`radar.cpp`) — connects to home WiFi as a station, fetches the RainViewer API for the latest radar timestamp, downloads a 2×2 grid of 256×256 PNG tiles at zoom 9 (~46-mile radius), decodes and stitches them into a 512×512 RGB565 buffer in PSRAM, crops a 240×240 region centred on the stored lat/lon, pushes to the GC9A01 via TFT_eSPI, then enters deep sleep for the configured interval (5–60 min).

### Key files

| File | Role |
|---|---|
| `config.h` | All compile-time constants (AP SSID, pin numbers, RainViewer color scheme, zoom level) |
| `storage.h/cpp` | NVS read/write via `Preferences.h` — single `AppConfig` struct |
| `setup_server.h/cpp` | AP + `WebServer` blocking loop; HTML/JS served from `PROGMEM` |
| `radar.h/cpp` | RainViewer API fetch, tile math, PNG decode, display push |
| `TFT_UserSetup.h` | TFT_eSPI pin config for this board — must be copied to library folder |

### Memory layout

The 512×512 stitch buffer (512 KB) and 80 KB tile download buffer both use `ps_malloc()` (PSRAM). The 240×240 crop buffer (~112 KB) also uses `ps_malloc` with a row-by-row fallback. All three are allocated once in `fetchAndDisplayRadar()` and never freed between wake cycles (device deep-sleeps and reboots each cycle).

### Tile coordinate math

`lon2tileX` / `lat2tileY` use standard Web Mercator (OSM/Google) tile formulas. The 2×2 grid origin is shifted one tile left or up when the target location falls in the left/upper half of its home tile, keeping the location near the centre of the stitched image.

### Display pin

Backlight is controlled directly via `LCD_BL_PIN` (currently GPIO 40) — not managed by TFT_eSPI.
