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
 *   Flash Size: 4MB
 *
 * TFT_eSPI setup:
 *   Copy TFT_UserSetup.h from this project into the TFT_eSPI library folder,
 *   renaming it User_Setup.h (replace the existing file).
 */

#include "config.h"
#include "storage.h"
#include "setup_server.h"
#include "radar.h"
#include "imu.h"
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include "driver/gpio.h"

// Single shared display instance — extern'd by setup_server.cpp and radar.cpp
TFT_eSPI tft;

AppConfig cfg;

void setup() {
    // Release GPIO holds from previous deep sleep (no-op on fresh power-on)
    gpio_hold_dis((gpio_num_t)LCD_BL_PIN);
    gpio_hold_dis((gpio_num_t)LCD_RST_PIN);
    gpio_hold_dis((gpio_num_t)LCD_CS_PIN);

    Serial.begin(115200);
    delay(300); // let serial settle

    Serial.println("\n[boot] TinyWeatherRadar starting");

    if (!loadConfig(cfg)) {
        Serial.println("[boot] no config found — entering setup mode");
        startSetupServer(); // blocks until form submitted + reboot
    }

    if (checkSetupGesture()) {
        Serial.println("[boot] face-down gesture — entering setup mode");
        startSetupServer(); // blocks until form submitted + reboot
    }

    Serial.printf("[boot] config OK  ssid=%s  lat=%.4f  lon=%.4f  refresh=%dm\n",
                  cfg.wifiSSID, cfg.latitude, cfg.longitude, cfg.refreshMinutes);

    bool wifiOk = fetchAndDisplayRadar(cfg);

    if (!wifiOk) {
        // WiFi failed — fall back to setup mode so the user can update credentials
        Serial.println("[boot] WiFi failed — entering setup mode");
        startSetupServer();  // blocks until form submitted + reboot
    }

    uint64_t sleepUs = (uint64_t)cfg.refreshMinutes * 60ULL * 1000000ULL;
    Serial.printf("[boot] sleeping %d minutes\n", cfg.refreshMinutes);
    gpio_hold_en((gpio_num_t)LCD_BL_PIN);   // keep backlight on during deep sleep
    gpio_hold_en((gpio_num_t)LCD_RST_PIN);  // prevent display reset
    gpio_hold_en((gpio_num_t)LCD_CS_PIN);   // keep CS inactive (high)
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_timer_wakeup(sleepUs);
    esp_deep_sleep_start();
}

void loop() {
    // Never reached: setup mode loops internally; radar mode ends in deep sleep.
}
