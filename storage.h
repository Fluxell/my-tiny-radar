#pragma once
#include <Arduino.h>

struct AppConfig {
    char    wifiSSID[64];
    char    wifiPass[64];
    float   latitude;
    float   longitude;
    int     refreshMinutes;
    char    tzPosix[64];       // POSIX TZ string, e.g. "CST6CDT,M3.2.0,M11.1.0"
};

// Returns true if valid config was found in NVS
bool loadConfig(AppConfig& cfg);
void saveConfig(const AppConfig& cfg);
void clearConfig();
