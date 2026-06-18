#pragma once
#include <Arduino.h>

struct AppConfig {
    char  wifiSSID[64];
    char  wifiPass[64];
    float latitude;
    float longitude;
    int   refreshMinutes;
};

// Returns true if valid config was found in NVS
bool loadConfig(AppConfig& cfg);
void saveConfig(const AppConfig& cfg);
void clearConfig();
