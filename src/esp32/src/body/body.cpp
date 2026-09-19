#include "body/body.h"

#include <cstring>

void Body::begin() {
    actuator_count_ = board_.actuator_count < kMaxActuators ? board_.actuator_count : kMaxActuators;
    for (size_t i = 0; i < actuator_count_; i++) {
        actuators_[i] = Actuator(board_.actuators[i]);
        actuators_[i].begin();
    }

    sensor_count_ = board_.sensor_count < kMaxSensors ? board_.sensor_count : kMaxSensors;
    for (size_t i = 0; i < sensor_count_; i++) {
        sensors_[i] = Sensor(board_.sensors[i]);
        sensors_[i].begin();
    }
}

Actuator* Body::actuator(const char* name) {
    for (size_t i = 0; i < actuator_count_; i++) {
        if (strcmp(actuators_[i].name(), name) == 0) return &actuators_[i];
    }
    return nullptr;
}

Sensor* Body::sensor(const char* name) {
    for (size_t i = 0; i < sensor_count_; i++) {
        if (strcmp(sensors_[i].name(), name) == 0) return &sensors_[i];
    }
    return nullptr;
}
