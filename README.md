# TinyWeatherRadar

A weather radar display for the [Waveshare ESP32-S3-LCD-1.28](https://www.waveshare.com/esp32-s3-lcd-1.28-b.htm) — a round 240×240 GC9A01 screen on an ESP32-S3. On first boot it hosts a WiFi setup page; after configuration it fetches live radar tiles from [RainViewer](https://www.rainviewer.com/) and updates on a schedule you choose.

---

## Hardware

- Waveshare ESP32-S3-LCD-1.28 (or -1.28-B)
- USB-C cable for flashing

No additional wiring required — the display is built into the board.

---

## Prerequisites

### Arduino IDE 2.x

Download from [arduino.cc/en/software](https://www.arduino.cc/en/software).

### ESP32 Arduino core

1. Open **File → Preferences** and add this URL to *Additional boards manager URLs*:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. Open **Tools → Board → Boards Manager**, search `esp32`, install **esp32 by Espressif Systems**.

### Libraries

Install all three via **Tools → Manage Libraries**:

| Library | Author |
|---|---|
| TFT_eSPI | Bodmer |
| PNGdec | Larry Bank |
| ArduinoJson | Benoît Blanchon (v6.x) |

---

## Configuration

### 1. TFT_eSPI display driver

TFT_eSPI needs to know which pins the display uses. Copy `TFT_UserSetup.h` from this repo into the TFT_eSPI library folder and rename it `User_Setup.h`, replacing the existing file.

The library folder is typically:
- **Windows:** `Documents\Arduino\libraries\TFT_eSPI\`
- **macOS/Linux:** `~/Arduino/libraries/TFT_eSPI/`

### 2. Compile-time options

Edit `config.h` before building if you need to change defaults:

| Constant | Default | Description |
|---|---|---|
| `AP_SSID` | `"TinyRadar"` | WiFi network name broadcast during setup |
| `AP_PASSWORD` | `""` | Setup AP password (empty = open) |
| `LCD_BL_PIN` | `40` | Backlight GPIO |
| `RV_TILE_COLOR` | `4` | Radar colour scheme (2=Blue, 4=TWC, 6=Meteored, 7=NEXRAD) |
| `REFRESH_DEFAULT` | `15` | Default refresh interval in minutes |

---

## Building & Flashing

1. Open `TinyWeatherRadar.ino` in Arduino IDE.
2. Select your board under **Tools → Board → esp32 → ESP32S3 Dev Module**.
3. Set the following under **Tools**:

   | Setting | Value |
   |---|---|
   | PSRAM | OPI PSRAM |
   | Flash Size | 8MB (match your board) |
   | Partition Scheme | Default 4MB with spiffs (or 8MB if available) |
   | Upload Speed | 921600 |

4. Select the correct COM port under **Tools → Port**.
5. Click **Upload** (→).

---

## Initial Setup

On first boot (or after a factory reset) the device has no WiFi or location configured. It will broadcast a WiFi access point:

> **Network:** `TinyRadar`  
> **Password:** *(none by default)*

1. Connect your phone or computer to `TinyRadar`.
2. Open a browser and go to **192.168.4.1**.
3. Fill in the setup form:
   - **Home WiFi** — the network the device will use to fetch radar data.
   - **Location** — enter a US ZIP code and tap *Look up* (uses mobile data / your computer's connection, not the device's), or type latitude/longitude directly.
   - **Refresh rate** — how often the radar image updates (5–60 minutes).
4. Tap **Save & Start Radar**.

The device restarts, connects to your home WiFi, downloads the latest radar image, and displays it. It then deep-sleeps and wakes automatically at the chosen interval.

> **Note:** The ZIP code lookup calls the [Zippopotam.us](https://api.zippopotam.us/) API from your browser. If your device has no internet while connected to the TinyRadar AP (e.g. a laptop with no other connection), enter coordinates manually instead. You can find your coordinates at [maps.google.com](https://maps.google.com) by right-clicking your location.

---

## Factory Reset

To clear saved settings and return to setup mode, erase the device's flash:

**Arduino IDE:** Tools → ESP32 Sketch Data Upload is not needed — use the **Erase Flash** option if available, or reflash with *Erase All Flash Before Sketch Upload* enabled under Tools.

Alternatively, add a call to `clearConfig()` in your sketch temporarily and reflash.

---

## License

See [LICENSE](LICENSE).
