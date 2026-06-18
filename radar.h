#pragma once
#include "storage.h"

// Connects to WiFi, fetches the latest RainViewer radar tile(s),
// stitches + crops them, and pushes the result to the GC9A01 display.
// Disconnects WiFi when done.
void fetchAndDisplayRadar(const AppConfig& cfg);
