/*
 * TinyWeatherRadar
 * ESP32-S3 + Waveshare 1.28" round GC9A01 display
 *
 * Boot flow:
 *   1. No NVS config → AP "TinyRadar" + web server at 192.168.4.1
 *   2. Config saved  → connect home WiFi → fetch RainViewer tile → display → deep sleep
 *
 * Required libraries (Arduino Library Manager):
 *   - TFT_eSPI       by Bodmer
 *   - PNGdec         by Larry Bank
 *   - ArduinoJson    v6 (or v7 — change DynamicJsonDocument → JsonDocument)
 *
 * Board settings (Arduino IDE → Tools):
 *   Board:      ESP32S3 Dev Module   (or Waveshare ESP32-S3 if listed)
 *   PSRAM:      OPI PSRAM
 *   Flash Size: 8MB (or whatever your board has)
 *
 * TFT_eSPI setup:
 *   Copy TFT_UserSetup.h from this project into the TFT_eSPI library folder,
 *   renaming it User_Setup.h (replace the existing file).
 */

#include "config.h"
#include "storage.h"
#include "setup_server.h"
#include "radar.h"
#include <esp_sleep.h>

AppConfig cfg;

void setup() {
    Serial.begin(115200);
    delay(300); // let serial settle

    Serial.println("\n[boot] TinyWeatherRadar starting");

    if (!loadConfig(cfg)) {
        Serial.println("[boot] no config found — entering setup mode");
        startSetupServer(); // blocks until form submitted + reboot
    }

    Serial.printf("[boot] config OK  ssid=%s  lat=%.4f  lon=%.4f  refresh=%dm\n",
                  cfg.wifiSSID, cfg.latitude, cfg.longitude, cfg.refreshMinutes);

    fetchAndDisplayRadar(cfg);

    uint64_t sleepUs = (uint64_t)cfg.refreshMinutes * 60ULL * 1000000ULL;
    Serial.printf("[boot] sleeping %d minutes\n", cfg.refreshMinutes);
    esp_sleep_enable_timer_wakeup(sleepUs);
    esp_deep_sleep_start();
}

void loop() {
    // Never reached: setup mode loops internally; radar mode ends in deep sleep.
}
