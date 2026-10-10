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
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"

// Single shared display instance — extern'd by setup_server.cpp and radar.cpp
TFT_eSPI tft;

AppConfig cfg;

static void applyZoomTap(int pin) {
    if (pin == BTN_LEFT_PIN) {
        cfg.tileZoom = max(cfg.tileZoom - 1, ZOOM_MIN);
        Serial.printf("[boot] LEFT tap — zoom out to %d\n", cfg.tileZoom);
    } else {
        cfg.tileZoom = min(cfg.tileZoom + 1, ZOOM_MAX);
        Serial.printf("[boot] RIGHT tap — zoom in to %d\n", cfg.tileZoom);
    }
    saveConfig(cfg);
}

void setup() {
    // Release GPIO holds from previous deep sleep (no-op on fresh power-on).
    // RST is deliberately left HELD on a timer wake: TFT_eSPI's init() always
    // toggles TFT_RST as a hardware reset, which wipes the GC9A01's GRAM (the
    // visible image) on every call. While the hold is active, the pin's real
    // level can't change no matter what init() writes to it, so the panel's
    // last frame survives straight through the next tft.init() call.
    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    bool resumingFromSleep = (wakeCause == ESP_SLEEP_WAKEUP_TIMER ||
                               wakeCause == ESP_SLEEP_WAKEUP_EXT1);
    gpio_hold_dis((gpio_num_t)LCD_BL_PIN);
    gpio_hold_dis((gpio_num_t)LCD_CS_PIN);
    if (!resumingFromSleep) {
        gpio_hold_dis((gpio_num_t)LCD_RST_PIN);
    }

    // Releasing the BL hold resets the pin's output register to its post-sleep
    // default until something drives it again — without this, the backlight
    // goes dark for the several seconds WiFi takes to connect, since the only
    // other digitalWrite(HIGH) for it happens deep inside fetchAndDisplayRadar().
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, HIGH);

    Serial.begin(115200);
    delay(300); // let serial settle

    Serial.println("\n[boot] TinyWeatherRadar starting");

    if (!loadConfig(cfg)) {
        Serial.println("[boot] no config found — entering setup mode");
        startSetupServer(); // blocks until form submitted + reboot
    }

    Serial.printf("[boot] config OK  ssid=%s  lat=%.4f  lon=%.4f  refresh=%dm  zoom=%d\n",
                  cfg.wifiSSID, cfg.latitude, cfg.longitude, cfg.refreshMinutes, cfg.tileZoom);

    // ─── Button handling ───────────────────────────────────────────────────
    // Buttons only act when they woke the device from deep sleep (EXT1) — there
    // is no idle loop to poll them otherwise. Short press = zoom; held for
    // BTN_HOLD_MS = reset (left) or setup mode (right).
    // Release RTC-domain hold from the last sleep so normal digital pinMode/
    // digitalRead behave correctly again (the RTC pull-up set below survives
    // deep sleep on its own but leaves the pad in "RTC function" mode).
    rtc_gpio_deinit((gpio_num_t)BTN_LEFT_PIN);
    rtc_gpio_deinit((gpio_num_t)BTN_RIGHT_PIN);
    pinMode(BTN_LEFT_PIN,  INPUT_PULLUP);
    pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);

    bool zoomTapped = false; // set below on a short tap — gates the WiFi-linger window

    if (wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t wakeStatus = esp_sleep_get_ext1_wakeup_status();
        bool leftBit  = wakeStatus & (1ULL << BTN_LEFT_PIN);
        bool rightBit = wakeStatus & (1ULL << BTN_RIGHT_PIN);

        delay(BTN_DEBOUNCE_MS); // let contact bounce settle before classifying hold/tap

        // The wake status bit already proves this pin was driven LOW to cause
        // the wake — re-checking digitalRead() here would silently drop any
        // press short enough to have released before BTN_DEBOUNCE_MS elapses.
        // If both fire at once, LEFT wins — its outcomes (zoom out / restart)
        // are lower-risk than RIGHT's (zoom in / blocking setup mode).
        int pin = leftBit ? BTN_LEFT_PIN : (rightBit ? BTN_RIGHT_PIN : -1);

        if (pin != -1) {
            unsigned long pressStart = millis();
            bool isHold = false;
            while (digitalRead(pin) == LOW) {
                if (millis() - pressStart >= BTN_HOLD_MS) { isHold = true; break; }
                delay(20);
            }

            if (pin == BTN_LEFT_PIN && isHold) {
                Serial.println("[boot] LEFT held — soft reset");
                ESP.restart();
            } else if (pin == BTN_RIGHT_PIN && isHold) {
                Serial.println("[boot] RIGHT held — entering setup mode");
                startSetupServer(); // blocks until form submit + reboot
            } else {
                applyZoomTap(pin);
                zoomTapped = true;
            }
        }
    }

    bool wifiOk = fetchAndDisplayRadar(cfg, zoomTapped);

    if (!wifiOk) {
        // WiFi failed — offer setup so the user can update credentials.
        // Times out after 60 s if no client connects, then falls through to deep sleep.
        Serial.println("[boot] WiFi failed — entering setup mode");
        startSetupServer(60000UL);
    }

    // A tap keeps WiFi connected and lingers for BTN_LINGER_MS, watching for
    // more taps, so rapid successive zoom changes don't each pay the full
    // WiFi-reconnect cost. Only reached on a tap (holds diverge above).
    if (zoomTapped && wifiOk) {
        Serial.println("[boot] linger window — watching for more taps");
        unsigned long idleDeadline = millis() + BTN_LINGER_MS;
        while (millis() < idleDeadline) {
            int livePin = -1;
            if (digitalRead(BTN_LEFT_PIN) == LOW) {
                livePin = BTN_LEFT_PIN;
            } else if (digitalRead(BTN_RIGHT_PIN) == LOW) {
                livePin = BTN_RIGHT_PIN;
            }

            if (livePin != -1) {
                delay(BTN_DEBOUNCE_MS);
                if (digitalRead(livePin) == LOW) {
                    applyZoomTap(livePin);
                    fetchAndDisplayRadar(cfg, true);
                    while (digitalRead(livePin) == LOW) delay(20); // wait for release
                    idleDeadline = millis() + BTN_LINGER_MS;
                }
            }
            delay(20);
        }
        Serial.println("[boot] linger window expired — disconnecting WiFi");
        disconnectWifi();
    }

    uint64_t sleepUs = (uint64_t)cfg.refreshMinutes * 60ULL * 1000000ULL;
    Serial.printf("[boot] sleeping %d minutes\n", cfg.refreshMinutes);
    gpio_hold_en((gpio_num_t)LCD_BL_PIN);   // keep backlight on during deep sleep
    gpio_hold_en((gpio_num_t)LCD_RST_PIN);  // prevent display reset
    gpio_hold_en((gpio_num_t)LCD_CS_PIN);   // keep CS inactive (high)
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_timer_wakeup(sleepUs);
    // Digital pinMode(INPUT_PULLUP) doesn't survive into the RTC power domain
    // that stays alive during deep sleep — without an RTC-level pull-up here,
    // these pins float while asleep and randomly read LOW, firing spurious
    // EXT1 wakeups long before the timer (or the button) actually does.
    rtc_gpio_pullup_en((gpio_num_t)BTN_LEFT_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_LEFT_PIN);
    rtc_gpio_pullup_en((gpio_num_t)BTN_RIGHT_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_RIGHT_PIN);
    esp_sleep_enable_ext1_wakeup((1ULL << BTN_LEFT_PIN) | (1ULL << BTN_RIGHT_PIN),
                                 ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

void loop() {
    // Never reached: setup mode loops internally; radar mode ends in deep sleep.
}
