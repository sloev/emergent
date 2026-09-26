#include "core/safety_monitor.h"

#include <Arduino.h>
#include <cstring>

#include "core/tuning.h"

void SafetyMonitor::begin(Body& body) {
    memset(thermal_accum_, 0, sizeof(thermal_accum_));
    memset(thermal_tripped_, 0, sizeof(thermal_tripped_));
    battery_critical_ = false;

    const char* energy_sensor = body.board().energy_sensor;
    energy_sensor_index_ = -1;
    if (energy_sensor != nullptr) {
        for (size_t i = 0; i < body.sensor_count(); i++) {
            if (strcmp(body.sensor_at(i).name(), energy_sensor) == 0) {
                energy_sensor_index_ = static_cast<int>(i);
                break;
            }
        }
    }
}

void SafetyMonitor::enforce(Body& body, float dt_s) {
    if (dt_s <= 0.0f) return;

    battery_critical_ = false;
    if (energy_sensor_index_ >= 0) {
        float level = body.sensor_at(static_cast<size_t>(energy_sensor_index_)).last_value();
        battery_critical_ = level < kTuning.safety_monitor.critical_battery_level;
    }

    size_t n = body.actuator_count() < Body::kMaxActuators ? body.actuator_count() : Body::kMaxActuators;
    for (size_t i = 0; i < n; i++) {
        Actuator& a = body.actuator_at(i);

        float accum = thermal_accum_[i] + a.cost() * dt_s - kTuning.safety_monitor.thermal_cooldown_rate * dt_s;
        if (accum < 0.0f) accum = 0.0f;
        thermal_accum_[i] = accum;

        thermal_tripped_[i] = accum > kTuning.safety_monitor.thermal_limit;

        if (battery_critical_ || thermal_tripped_[i]) {
            a.write(0.0f);
        }
    }
}
