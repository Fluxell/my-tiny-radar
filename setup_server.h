#pragma once

// Starts the WiFi access point and HTTP server for initial device configuration.
// Blocks indefinitely; reboots the device after a successful form submission.
void startSetupServer();
