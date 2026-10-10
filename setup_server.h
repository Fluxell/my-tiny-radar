#pragma once

// Starts the WiFi access point and HTTP server for initial device configuration.
// Reboots the device after a successful form submission.
// If timeoutMs > 0, returns early if no client connects within that window,
// allowing the caller to fall through and continue with saved settings.
void startSetupServer(unsigned long timeoutMs = 0);
