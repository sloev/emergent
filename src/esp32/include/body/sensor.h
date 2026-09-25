#pragma once

#include "body/sensor_spec.h"

// Runtime handle for one sensor channel: reads and normalizes the hardware
// described by a SensorSpec, caching between reads per update_rate_hz.
class Sensor {
public:
    Sensor() = default;
    explicit Sensor(const SensorSpec& spec) : spec_(spec) {}

    void begin();

    // Returns a normalized reading. If update_rate_hz > 0 and called again
    // before the next scheduled sample, returns the cached value instead of
    // touching hardware. kWifiRssi is the exception — see read_wifi_rssi().
    float read();

    float last_value() const { return last_value_; }
    const char* name() const { return spec_.name; }
    const SensorSpec& spec() const { return spec_; }

private:
    float read_hardware();
    float read_wifi_rssi();

    SensorSpec spec_{};
    float last_value_ = 0.0f;
    uint32_t last_read_us_ = 0;
    bool has_reading_ = false;

    // kWifiRssi only.
    uint32_t last_scan_start_us_ = 0;
    bool wifi_scan_pending_ = false;
};
