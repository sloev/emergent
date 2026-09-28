#pragma once
//
// Body plans: what one simulated organism is made of. A plan lists its
// locomotion system, every sensor and actuator channel (with the physical
// part each one models), its battery, and its temperament (a Tuning, the
// same struct the firmware runs on).
//
// Organism 0 is the example build from docs/example-body.md. Organisms 1..N
// are generated from a seed out of real, cheap parts: stepper or DC wheels,
// a stepper with a steering servo, one or two vibration motors (bristlebot /
// Kilobot style), or two servo "legs" that only move the body when they
// sweep backward (anisotropic-friction crawlers). The engine is told none
// of this; it only ever sees channel kinds and ranges.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "board_config.h"
#include "core/tuning.h"

namespace sim {

enum class Loco : uint8_t {
    kDiffStepper,  // two stepper wheels: commanded speed is actual speed
    kDiffDc,       // two DC gearmotors: deadband, lag, speed noise
    kTricycle,     // one stepper drive wheel + a positional steering servo
    kVibro2,       // two vibration motors (Kilobot-like): slow, noisy, turns by imbalance
    kVibro1,       // one vibration motor (bristlebot): speed only, curves and wanders
    kCrawler,      // two servo legs: thrust only on the backward sweep, so it must oscillate
};

enum class Sense : uint8_t {
    kBattery,        // pack voltage through a divider (the energy sensor)
    kLight,          // LDR looking in one direction
    kAmbientLight,   // LDR looking up: brightness, no direction
    kMic,            // electret mic envelope
    kBump,           // microswitch covering an arc of the body
    kRangeIr,        // Sharp IR, 10-80 cm, one direction
    kRangeUs,        // ultrasonic, 2-400 cm, 30 degree cone
    kRssi,           // station beacon strength
    kChargeCurrent,  // INA219 on the charge input: current flowing in
    kAccel,          // accelerometer magnitude: vibration, jolts
    kProprio,        // position feedback of one of its own servos
};

enum class Role : uint8_t {
    kLocoLeft, kLocoRight,   // diff drives, vibro2 motors, crawler legs
    kDrive, kSteer,          // tricycle
    kVibro,                  // vibro1's only motor
    kHead,                   // servo panning head-mounted sensors
    kLed, kBuzzer, kFan, kHaptic,
};

struct SensorDef {
    Sense type;
    std::string name;
    std::string part;
    float angle = 0.0f;        // radians from the body's heading (directional senses)
    float arc = 0.0f;          // bump: half-width of the covered arc
    bool on_head = false;      // turns with the head servo
    float scan_period = 5.0f;  // rssi: seconds between readings
    int proprio_of = -1;       // proprio: actuator index
};

struct ActuatorDef {
    Role role;
    std::string name;
    std::string part;
    ActuatorKind kind;
    float min, max, rate, cost;
    float current_a;  // at full command (servos: while moving at full speed)
    float noise;      // how loud it is, 0..1 at full command
};

struct BodyPlan {
    int id = 0;
    std::string title;        // short: "#12 tricycle"
    std::string temperament;  // words
    Loco loco = Loco::kDiffStepper;
    float radius = 0.10f;
    float max_speed = 0.20f;   // m/s at full command (crawler: leg reach in m)
    float wheel_base = 0.15f;
    float curve = 0.0f;        // vibro1: built-in turning bias, rad/s at full
    int cells = 3;
    float capacity_ah = 1.0f;
    float base_current_a = 0.25f;
    std::vector<SensorDef> sensors;
    std::vector<ActuatorDef> actuators;
    Tuning tuning{};
    bool battery_linked = true;

    int actuator_index(Role r) const {
        for (size_t i = 0; i < actuators.size(); i++)
            if (actuators[i].role == r) return static_cast<int>(i);
        return -1;
    }
};

inline const char* loco_name(Loco l) {
    switch (l) {
        case Loco::kDiffStepper: return "stepper wheels";
        case Loco::kDiffDc: return "DC wheels";
        case Loco::kTricycle: return "tricycle (stepper + steering servo)";
        case Loco::kVibro2: return "two vibration motors";
        case Loco::kVibro1: return "one vibration motor";
        case Loco::kCrawler: return "servo crawler";
    }
    return "?";
}

inline const char* loco_short(Loco l) {
    switch (l) {
        case Loco::kDiffStepper: return "stepper-wheels";
        case Loco::kDiffDc: return "dc-wheels";
        case Loco::kTricycle: return "tricycle";
        case Loco::kVibro2: return "vibro-2";
        case Loco::kVibro1: return "vibro-1";
        case Loco::kCrawler: return "crawler";
    }
    return "?";
}

inline const char* sense_name(Sense s) {
    switch (s) {
        case Sense::kBattery: return "battery";
        case Sense::kLight: return "light";
        case Sense::kAmbientLight: return "ambient-light";
        case Sense::kMic: return "mic";
        case Sense::kBump: return "bump";
        case Sense::kRangeIr: return "ir-range";
        case Sense::kRangeUs: return "ultrasonic";
        case Sense::kRssi: return "rssi";
        case Sense::kChargeCurrent: return "charge-current";
        case Sense::kAccel: return "accel";
        case Sense::kProprio: return "proprio";
    }
    return "?";
}

inline const char* role_name(Role r) {
    switch (r) {
        case Role::kLocoLeft: return "loco-left";
        case Role::kLocoRight: return "loco-right";
        case Role::kDrive: return "drive";
        case Role::kSteer: return "steer";
        case Role::kVibro: return "vibro";
        case Role::kHead: return "head";
        case Role::kLed: return "led";
        case Role::kBuzzer: return "buzzer";
        case Role::kFan: return "fan";
        case Role::kHaptic: return "haptic";
    }
    return "?";
}

// --- the example build (docs/example-body.md) ------------------------------

inline BodyPlan example_plan() {
    BodyPlan p;
    p.id = 0;
    p.title = "#0 example build";
    p.temperament = "default constitution";
    p.loco = Loco::kDiffStepper;
    p.radius = 0.10f;
    p.max_speed = 0.20f;
    p.wheel_base = 0.15f;
    p.cells = 3;
    p.capacity_ah = 1.0f;
    p.base_current_a = 0.25f;
    p.actuators = {
        {Role::kLocoLeft, "stepper_left", "NEMA17 + A4988", ActuatorKind::kPwmBidirectional, -1, 1, 2, 1.0f, 0.7f, 0.35f},
        {Role::kLocoRight, "stepper_right", "NEMA17 + A4988", ActuatorKind::kPwmBidirectional, -1, 1, 2, 1.0f, 0.7f, 0.35f},
        {Role::kHead, "head_pan", "SG90 servo", ActuatorKind::kServo, -1, 1, 2, 0.2f, 0.15f, 0.05f},
        {Role::kLed, "led", "5 mm LED", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.1f, 0.05f, 0.0f},
        {Role::kBuzzer, "buzzer", "piezo", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.1f, 0.03f, 0.8f},
    };
    SensorDef s;
    p.sensors.push_back({Sense::kBattery, "battery_voltage", "100k/22k divider"});
    p.sensors.push_back({Sense::kMic, "mic", "MAX4466"});
    s = {Sense::kLight, "light_left", "GL5528 LDR"};
    s.angle = 0.6f;
    s.on_head = true;
    p.sensors.push_back(s);
    s = {Sense::kLight, "light_right", "GL5528 LDR"};
    s.angle = -0.6f;
    s.on_head = true;
    p.sensors.push_back(s);
    s = {Sense::kBump, "bump", "microswitch, front"};
    s.arc = 1.2f;
    p.sensors.push_back(s);
    p.sensors.push_back({Sense::kRangeIr, "ir_distance", "Sharp GP2Y0A21"});
    s = {Sense::kRssi, "rf_rssi_station", "ESP32 Wi-Fi scan"};
    s.scan_period = 5.0f;
    p.sensors.push_back(s);
    return p;
}

// --- generated organisms -----------------------------------------------------

namespace detail {
struct Rng {
    std::mt19937 g;
    explicit Rng(uint32_t seed) : g(seed) {}
    float uni(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g); }
    bool chance(float p) { return uni(0, 1) < p; }
    int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(g); }
};

inline const char* side(float angle) {
    if (angle > 1.2f) return "left";
    if (angle > 0.3f) return "front-left";
    if (angle > -0.3f) return "front";
    if (angle > -1.2f) return "front-right";
    return "right";
}
}  // namespace detail

inline BodyPlan random_plan(int id, uint32_t seed) {
    detail::Rng r(seed * 7919u + 17u);
    BodyPlan p;
    p.id = id;

    // Locomotion.
    float w = r.uni(0, 1);
    p.loco = w < 0.22f ? Loco::kDiffStepper
             : w < 0.37f ? Loco::kDiffDc
             : w < 0.52f ? Loco::kTricycle
             : w < 0.72f ? Loco::kVibro2
             : w < 0.82f ? Loco::kVibro1
                         : Loco::kCrawler;

    auto act = [&](Role role, const std::string& name, const std::string& part, ActuatorKind kind, float mn,
                   float mx, float rate, float cost, float amps, float noise) {
        p.actuators.push_back({role, name, part, kind, mn, mx, rate, cost, amps, noise});
    };

    switch (p.loco) {
        case Loco::kDiffStepper: {
            p.radius = r.uni(0.07f, 0.12f);
            p.max_speed = r.uni(0.10f, 0.25f);
            p.wheel_base = p.radius * 1.5f;
            float a = r.uni(0.4f, 0.9f);
            act(Role::kLocoLeft, "stepper_left", "NEMA17 stepper", ActuatorKind::kPwmBidirectional, -1, 1, 2, 1, a, 0.35f);
            act(Role::kLocoRight, "stepper_right", "NEMA17 stepper", ActuatorKind::kPwmBidirectional, -1, 1, 2, 1, a, 0.35f);
            break;
        }
        case Loco::kDiffDc: {
            p.radius = r.uni(0.07f, 0.12f);
            p.max_speed = r.uni(0.20f, 0.40f);
            p.wheel_base = p.radius * 1.5f;
            float a = r.uni(0.3f, 0.6f);
            act(Role::kLocoLeft, "motor_left", "N20 gearmotor + DRV8833", ActuatorKind::kPwmBidirectional, -1, 1, 4, 1, a, 0.25f);
            act(Role::kLocoRight, "motor_right", "N20 gearmotor + DRV8833", ActuatorKind::kPwmBidirectional, -1, 1, 4, 1, a, 0.25f);
            break;
        }
        case Loco::kTricycle: {
            p.radius = r.uni(0.08f, 0.12f);
            p.max_speed = r.uni(0.15f, 0.30f);
            p.wheel_base = p.radius * 1.6f;
            act(Role::kDrive, "drive_stepper", "NEMA17 stepper", ActuatorKind::kPwmBidirectional, -1, 1, 2, 1, r.uni(0.4f, 0.8f), 0.35f);
            act(Role::kSteer, "steer_servo", "MG90S servo (positional)", ActuatorKind::kServo, -1, 1, 3, 0.2f, 0.2f, 0.05f);
            break;
        }
        case Loco::kVibro2: {
            p.radius = r.uni(0.03f, 0.05f);
            p.max_speed = r.uni(0.01f, 0.03f);
            float a = r.uni(0.06f, 0.10f);
            act(Role::kLocoLeft, "vibe_left", "coin vibration motor", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.3f, a, 0.3f);
            act(Role::kLocoRight, "vibe_right", "coin vibration motor", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.3f, a, 0.3f);
            break;
        }
        case Loco::kVibro1: {
            p.radius = r.uni(0.025f, 0.04f);
            p.max_speed = r.uni(0.02f, 0.05f);
            p.curve = r.uni(-0.6f, 0.6f);
            act(Role::kVibro, "vibe", "pager vibration motor", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.3f, r.uni(0.06f, 0.10f), 0.3f);
            break;
        }
        case Loco::kCrawler: {
            p.radius = r.uni(0.06f, 0.10f);
            p.max_speed = r.uni(0.02f, 0.05f);  // leg reach
            p.wheel_base = p.radius * 1.4f;
            act(Role::kLocoLeft, "leg_left", "SG90 servo leg", ActuatorKind::kServo, -1, 1, 6, 0.3f, 0.2f, 0.1f);
            act(Role::kLocoRight, "leg_right", "SG90 servo leg", ActuatorKind::kServo, -1, 1, 6, 0.3f, 0.2f, 0.1f);
            break;
        }
    }

    bool small = p.loco == Loco::kVibro1 || p.loco == Loco::kVibro2;
    bool head = !small && r.chance(0.35f);
    if (head) act(Role::kHead, "head_pan", "SG90 servo", ActuatorKind::kServo, -1, 1, 2, 0.2f, 0.15f, 0.05f);
    if (r.chance(0.6f)) act(Role::kLed, "led", "LED", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.1f, 0.03f, 0.0f);
    if (r.chance(0.4f)) act(Role::kBuzzer, "buzzer", "piezo", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.1f, 0.03f, 0.8f);
    if (!small && r.chance(0.15f)) act(Role::kFan, "fan", "30 mm fan", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.4f, 0.15f, 0.4f);
    if (!small && r.chance(0.15f)) act(Role::kHaptic, "haptic", "vibration motor", ActuatorKind::kPwmUnipolar, 0, 1, 0, 0.2f, 0.08f, 0.2f);

    // Battery: 1-3 LiPo cells. Tiny bodies carry tiny packs.
    p.cells = small ? 1 : 1 + r.pick(3);
    p.capacity_ah = small ? r.uni(0.15f, 0.4f) : r.uni(0.5f, 2.0f);
    p.base_current_a = small ? r.uni(0.08f, 0.12f) : r.uni(0.15f, 0.25f);

    // Senses. The battery is always there: it's what the organism lives on.
    p.sensors.push_back({Sense::kBattery, "battery", "voltage divider"});
    auto add = [&](SensorDef s) {
        if (p.sensors.size() < 12) p.sensors.push_back(s);
    };
    const float light_angles[] = {0.0f, 0.5f, -0.5f, 0.9f, -0.9f, 1.6f, -1.6f, 3.14f};
    int n_light = r.pick(4);  // 0..3 directional
    for (int i = 0; i < n_light; i++) {
        SensorDef s{Sense::kLight, "", "LDR"};
        s.angle = light_angles[r.pick(8)];
        s.on_head = head && r.chance(0.7f);
        s.name = std::string("light_") + detail::side(s.angle) + (s.on_head ? "_head" : "") + "_" + std::to_string(i);
        add(s);
    }
    if (r.chance(0.3f)) add({Sense::kAmbientLight, "light_up", "LDR facing up"});
    if (r.chance(0.6f)) add({Sense::kMic, "mic", "MAX4466 envelope"});
    int n_bump = r.pick(3);
    for (int i = 0; i < n_bump; i++) {
        SensorDef s{Sense::kBump, i == 0 ? "bump_front" : "bump_rear", "microswitch"};
        s.angle = i == 0 ? 0.0f : 3.14f;
        s.arc = r.uni(0.6f, 1.4f);
        add(s);
    }
    int n_range = small ? 0 : r.pick(3);
    for (int i = 0; i < n_range; i++) {
        bool us = r.chance(0.4f);
        SensorDef s{us ? Sense::kRangeUs : Sense::kRangeIr, "", us ? "HC-SR04" : "Sharp GP2Y0A21"};
        s.angle = i == 0 ? 0.0f : (r.chance(0.5f) ? 0.8f : -0.8f);
        s.name = std::string(us ? "sonar_" : "ir_") + detail::side(s.angle);
        add(s);
    }
    if (r.chance(0.6f)) {
        SensorDef s{Sense::kRssi, "rssi_station", "Wi-Fi scan"};
        if (r.chance(0.35f)) {
            s.scan_period = 0.2f;
            s.part = "ESP-NOW beacon";
        }
        add(s);
    }
    if (r.chance(0.35f)) add({Sense::kChargeCurrent, "charge_in", "INA219 on charge input"});
    if (r.chance(0.4f)) add({Sense::kAccel, "accel", "MPU6050 magnitude"});
    for (size_t j = 0; j < p.actuators.size(); j++) {
        if (p.actuators[j].kind == ActuatorKind::kServo && r.chance(0.5f)) {
            SensorDef s{Sense::kProprio, "feel_" + p.actuators[j].name, "servo feedback pot"};
            s.proprio_of = static_cast<int>(j);
            add(s);
        }
    }

    // Temperament: the same knobs the firmware exposes, drawn around the
    // defaults. Each draw gets a word so a reader can tell organisms apart.
    Tuning& t = p.tuning;
    t.bands.lo[0] = r.uni(0.5f, 0.8f);
    t.bands.hi[5] = r.uni(0.25f, 0.6f);
    t.boredom.gain = r.uni(0.02f, 0.10f);
    t.safety.drop_gain = r.uni(0.6f, 2.0f);
    t.exploration.curiosity = r.uni(0.1f, 0.5f);
    t.exploration.hunger = r.uni(0.0f, 0.5f);
    t.policy.learn_rate *= r.uni(0.5f, 2.0f);
    t.policy.trace_tau_s = r.uni(1.5f, 6.0f);
    t.action.noise_tau_s = r.uni(0.5f, 2.0f);
    t.action.noise_gain = r.uni(0.3f, 0.7f);

    std::vector<std::string> words;
    words.push_back(t.bands.lo[0] > 0.7f ? "hungers early" : t.bands.lo[0] < 0.58f ? "hungers late" : "");
    words.push_back(t.boredom.gain > 0.075f ? "restless" : t.boredom.gain < 0.035f ? "placid" : "");
    words.push_back(t.safety.drop_gain > 1.6f ? "timid" : t.safety.drop_gain < 0.9f ? "bold" : "");
    words.push_back(t.exploration.curiosity > 0.4f ? "curious" : "");
    words.push_back(t.exploration.hunger > 0.4f ? "forages hard when hungry"
                    : t.exploration.hunger < 0.1f ? "doesn't forage harder when hungry" : "");
    words.push_back(t.policy.learn_rate > 0.003f ? "quick learner" : t.policy.learn_rate < 0.0014f ? "slow learner" : "");
    words.push_back(t.policy.trace_tau_s > 4.5f ? "long memory span" : t.policy.trace_tau_s < 2.2f ? "short memory span" : "");
    std::string temper;
    for (auto& wd : words) {
        if (wd.empty()) continue;
        if (!temper.empty()) temper += ", ";
        temper += wd;
    }
    p.temperament = temper.empty() ? "middling in everything" : temper;
    p.title = "#" + std::to_string(id) + " " + loco_short(p.loco);
    return p;
}

inline BodyPlan plan_for(int id) { return id == 0 ? example_plan() : random_plan(id, static_cast<uint32_t>(id)); }

}  // namespace sim
