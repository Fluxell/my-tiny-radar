#pragma once
#include <Arduino.h>

// Returns true if a setup-trigger gesture is detected (device held face-down).
// Call once on boot before WiFi, after loadConfig().
bool checkSetupGesture();
