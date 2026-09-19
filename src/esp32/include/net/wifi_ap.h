#pragma once

#include "board_config.h"

// Onboard WiFi hotspot. Credentials default to the board profile but can be
// overridden at runtime from the dashboard's config page — the override is
// persisted to NVS and survives reflashing (it only resets on erase_flash).
namespace wifi_ap {

void begin(const BoardConfig& board);

const char* ssid();

// Persists new credentials; takes effect on next boot (caller restarts).
void save_credentials(const char* ssid, const char* password);

}  // namespace wifi_ap
