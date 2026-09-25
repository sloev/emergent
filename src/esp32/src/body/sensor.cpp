#include "body/sensor.h"

#include <Arduino.h>
#include <WiFi.h>

void Sensor::begin() {
    if (spec_.name == nullptr) return;  // unused slot

    if (spec_.kind == SensorKind::kDigitalIn) {
        pinMode(spec_.pin, INPUT);
    }
    // ADC pins need no pinMode() on ESP32. kWifiRssi needs no pin at all —
    // see read_wifi_rssi().

    has_reading_ = false;
    wifi_scan_pending_ = false;
}

float Sensor::read() {
    if (spec_.name == nullptr) return 0.0f;

    // kWifiRssi manages its own timing (a real scan cycle takes seconds
    // regardless of how often this is called) rather than the plain
    // cache-until-stale rule below.
    if (spec_.kind == SensorKind::kWifiRssi) {
        return read_wifi_rssi();
    }

    uint32_t now = micros();
    if (has_reading_ && spec_.update_rate_hz > 0.0f) {
        float period_us = 1'000'000.0f / spec_.update_rate_hz;
        if ((now - last_read_us_) < static_cast<uint32_t>(period_us)) {
            return last_value_;
        }
    }

    last_value_ = read_hardware();
    last_read_us_ = now;
    has_reading_ = true;
    return last_value_;
}

float Sensor::read_hardware() {
    switch (spec_.kind) {
        case SensorKind::kAdcNormalized:
            return analogRead(spec_.pin) / 4095.0f;
        case SensorKind::kDigitalIn:
            return digitalRead(spec_.pin) ? 1.0f : 0.0f;
        case SensorKind::kWifiRssi:
            return last_value_;  // never reached — read() routes this kind elsewhere
    }
    return 0.0f;
}

// WiFi.scanNetworks() blocks for seconds if called synchronously — long
// enough to stall the 20 Hz behavior tick and freeze the dashboard's web
// server mid-request. Kick off an async scan (scanNetworks(true, ...)) and
// harvest the result on a *later* call instead. Consequence, disclosed
// rather than hidden: this channel's value only actually changes every few
// seconds (one scan cycle), no matter how often read() is called.
float Sensor::read_wifi_rssi() {
    if (wifi_scan_pending_) {
        int16_t result = WiFi.scanComplete();
        if (result == WIFI_SCAN_RUNNING) {
            return last_value_;  // still waiting; nothing new yet
        }

        wifi_scan_pending_ = false;

        constexpr float kRssiFloorDbm = -90.0f;  // treat as "not present"
        constexpr float kRssiCeilDbm = -30.0f;   // treat as "as strong as it gets"
        float rssi_dbm = kRssiFloorDbm;

        if (result > 0 && spec_.target_ssid != nullptr) {
            for (int16_t i = 0; i < result; i++) {
                if (WiFi.SSID(i) == spec_.target_ssid) {
                    rssi_dbm = static_cast<float>(WiFi.RSSI(i));
                    break;
                }
            }
        }
        WiFi.scanDelete();

        float normalized = (rssi_dbm - kRssiFloorDbm) / (kRssiCeilDbm - kRssiFloorDbm);
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        last_value_ = normalized;
        has_reading_ = true;
        return last_value_;
    }

    // No scan in flight: start one, provided the minimum interval (repurposing
    // update_rate_hz as "don't scan more often than this") has elapsed.
    uint32_t now = micros();
    bool interval_elapsed =
        !has_reading_ || spec_.update_rate_hz <= 0.0f ||
        (now - last_scan_start_us_) >= static_cast<uint32_t>(1'000'000.0f / spec_.update_rate_hz);

    if (interval_elapsed) {
        WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
        wifi_scan_pending_ = true;
        last_scan_start_us_ = now;
    }
    return last_value_;
}
