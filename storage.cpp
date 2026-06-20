#include "storage.h"
#include "config.h"
#include <Preferences.h>

bool loadConfig(AppConfig& cfg) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);

    bool hasConfig = prefs.isKey("ssid");
    if (hasConfig) {
        prefs.getString("ssid",    cfg.wifiSSID,  sizeof(cfg.wifiSSID));
        prefs.getString("pass",    cfg.wifiPass,  sizeof(cfg.wifiPass));
        cfg.latitude       = prefs.getFloat("lat",     0.0f);
        cfg.longitude      = prefs.getFloat("lon",     0.0f);
        cfg.refreshMinutes = prefs.getInt("refresh", REFRESH_DEFAULT);
        prefs.getString("tzposix", cfg.tzPosix, sizeof(cfg.tzPosix));
    }

    prefs.end();
    return hasConfig;
}

void saveConfig(const AppConfig& cfg) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("ssid",    cfg.wifiSSID);
    prefs.putString("pass",    cfg.wifiPass);
    prefs.putFloat("lat",      cfg.latitude);
    prefs.putFloat("lon",      cfg.longitude);
    prefs.putInt("refresh",    cfg.refreshMinutes);
    prefs.putString("tzposix", cfg.tzPosix);
    prefs.end();
}

void clearConfig() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}
