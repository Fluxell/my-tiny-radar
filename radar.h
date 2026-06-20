#pragma once
#include "storage.h"

// Connects to WiFi, fetches the latest RainViewer radar tile(s),
// stitches + crops them, and pushes the result to the GC9A01 display.
// Returns false if WiFi failed to connect (caller should enter setup mode).
// Disconnects WiFi when done.
bool fetchAndDisplayRadar(const AppConfig& cfg);
