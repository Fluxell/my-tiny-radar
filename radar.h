#pragma once
#include "storage.h"

// Connects to WiFi, fetches the latest RainViewer radar tile(s),
// stitches + crops them, and pushes the result to the GC9A01 display.
// Returns false if WiFi failed to connect (caller should enter setup mode).
// Disconnects WiFi when done, unless keepWifiConnected is true (used for the
// button-tap linger window, so repeated taps re-render without a full
// reconnect each time) — caller is then responsible for calling disconnectWifi().
bool fetchAndDisplayRadar(const AppConfig& cfg, bool keepWifiConnected = false);

// Disconnects and powers down WiFi. Only needed after a call to
// fetchAndDisplayRadar() made with keepWifiConnected = true.
void disconnectWifi();
