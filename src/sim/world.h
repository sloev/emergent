#pragma once
//
// A 2D room with one robot and one charging station, modelled on the
// example body in docs/example-body.md. Physics and sensor models are
// deliberately simple but each one is grounded in how the real part behaves
// (datasheet ranges, known noise sources), because the point of the
// simulator is to find out what the behavior engine does with *realistic*
// inputs, not idealised ones.
//
// Units: metres, seconds, radians, volts, amps, amp-hours.
//
// The organism never sees any of this — only normalized channel values
// through SimBody (sim_body.h). World state (position, heading, state of
// charge) exists only so the simulator can move the body and score it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace sim {

constexpr float kPi = 3.14159265f;

struct WorldConfig {
    // Room: a plain rectangle, origin bottom-left.
    float room_w = 3.0f;
    float room_h = 2.5f;

    // Chassis: two NEMA17 steppers on 65 mm wheels, ~150 mm track.
    // 1 rev/s at full command -> ~0.2 m/s.
    float robot_radius = 0.10f;
    float wheel_base = 0.15f;
    float max_wheel_speed = 0.20f;

    // Battery: 3S LiPo. Open-circuit voltage 9.0 V (empty) .. 12.6 V (full).
    float capacity_ah = 1.0f;
    float base_current_a = 0.25f;        // ESP32 with WiFi on + regulators + sensors
    float stepper_current_a = 0.7f;      // per stepper at full speed (driver disabled at rest)
    float buzzer_current_a = 0.03f;
    float led_current_a = 0.05f;
    float servo_current_a = 0.15f;       // while moving

    // Station: dock contact point against the right wall, mid-height.
    float station_x = 2.9f;
    float station_y = 1.25f;
    float dock_radius = 0.06f;           // contacts engage with the centre within this of the contact point
    float funnel_depth = 0.30f;          // guide walls reach this far out from the contacts
    float funnel_throat = 0.03f;         // half-width left at the contacts
    float charge_current_a = 1.0f;       // 1C charger, CC until 80% then tapering
    float station_led_intensity = 1.0f;

    // RF: log-distance path loss, the standard indoor model.
    float rssi_at_1m_dbm = -45.0f;
    float path_loss_exponent = 2.5f;
    float rssi_shadow_sigma_db = 4.0f;   // spatially correlated shadowing
    float rssi_fast_sigma_db = 2.0f;     // per-read fading
    float rssi_scan_period_s = 5.0f;     // firmware scans every 5 s today

    // Ambient light: a window along the top wall.
    float window_intensity = 0.6f;

    uint32_t seed = 1;
};

struct Pose {
    float x, y, theta;
};

// Motor/actuator commands the body writes each tick, in channel units.
struct Commands {
    float wheel_left = 0;    // -1..1
    float wheel_right = 0;   // -1..1
    float head_pan = 0;      // -1..1 -> -90..+90 degrees
    float led = 0;           // 0..1
    float buzzer = 0;        // 0..1
};

// Raw physical readings, before normalization into channel values.
struct Readings {
    float battery_v = 0;
    float mic_level = 0;      // 0..1 envelope
    float light_left = 0;     // arbitrary irradiance units
    float light_right = 0;
    bool bump = false;
    float ir_distance_m = 0;  // to the nearest wall straight ahead
    float rssi_dbm = -100;
};

class World {
public:
    explicit World(const WorldConfig& cfg) : cfg_(cfg), rng_(cfg.seed) {
        std::uniform_real_distribution<float> phase(0.0f, 2.0f * kPi);
        for (int i = 0; i < kShadowTerms; i++) {
            shadow_kx_[i] = std::normal_distribution<float>(0.0f, 3.0f)(rng_);
            shadow_ky_[i] = std::normal_distribution<float>(0.0f, 3.0f)(rng_);
            shadow_phase_[i] = phase(rng_);
        }
        reset_robot();
    }

    void reset_robot() {
        std::uniform_real_distribution<float> ux(0.3f, cfg_.room_w * 0.5f);
        std::uniform_real_distribution<float> uy(0.3f, cfg_.room_h - 0.3f);
        std::uniform_real_distribution<float> ut(-kPi, kPi);
        pose_ = {ux(rng_), uy(rng_), ut(rng_)};
        soc_ = 0.8f;
    }

    // Advance physics by dt under the given commands.
    void step(const Commands& c, float dt) {
        t_ += dt;
        bool powered = soc_ > 0.0f;

        float vl = powered ? c.wheel_left * cfg_.max_wheel_speed : 0.0f;
        float vr = powered ? c.wheel_right * cfg_.max_wheel_speed : 0.0f;

        // Differential-drive kinematics. Steppers don't slip under normal
        // load, so commanded speed is actual speed — until the robot is
        // pushing against a wall, where it stalls (handled by the clamp).
        float v = 0.5f * (vl + vr);
        float w = (vr - vl) / cfg_.wheel_base;
        pose_.theta += w * dt;
        pose_.theta = std::atan2(std::sin(pose_.theta), std::cos(pose_.theta));
        float nx = pose_.x + v * std::cos(pose_.theta) * dt;
        float ny = pose_.y + v * std::sin(pose_.theta) * dt;

        float r = cfg_.robot_radius;
        bump_ = false;

        // Station funnel: two 45-degree guide walls in front of the dock.
        // A robot that enters the mouth while moving toward the wall is
        // squeezed onto the contacts; that's what mechanical self-aligning
        // docks are for. Touching a guide wall reads as a bump.
        float into = cfg_.station_x - nx;  // distance in front of the contacts
        if (into < cfg_.funnel_depth && into > -r) {
            float half = std::max(0.0f, into) + cfg_.funnel_throat;
            float off = ny - cfg_.station_y;
            if (std::fabs(off) < cfg_.funnel_depth + cfg_.funnel_throat && std::fabs(off) > half) {
                ny = cfg_.station_y + (off > 0 ? half : -half);
                bump_ = true;
            }
        }
        if (nx < r) { nx = r; bump_ = true; }
        if (nx > cfg_.room_w - r) { nx = cfg_.room_w - r; bump_ = true; }
        if (ny < r) { ny = r; bump_ = true; }
        if (ny > cfg_.room_h - r) { ny = cfg_.room_h - r; bump_ = true; }
        pose_.x = nx;
        pose_.y = ny;

        head_angle_ = c.head_pan * (kPi / 2.0f);
        speed_ = std::fabs(v);
        wheel_activity_ = 0.5f * (std::fabs(c.wheel_left) + std::fabs(c.wheel_right));

        // Battery: current draw from everything running, minus charging.
        float draw = cfg_.base_current_a;
        if (powered) {
            draw += cfg_.stepper_current_a * (std::fabs(c.wheel_left) + std::fabs(c.wheel_right));
            draw += cfg_.buzzer_current_a * c.buzzer + cfg_.led_current_a * c.led;
            draw += cfg_.servo_current_a * std::fabs(c.head_pan - prev_head_) / std::max(dt, 1e-3f) * 0.1f;
        }
        prev_head_ = c.head_pan;

        docked_ = distance_to_station() < cfg_.dock_radius;
        float charge = 0.0f;
        if (docked_) {
            // CC-CV: full current to 80%, then tapering linearly to 0 at 100%.
            float taper = soc_ < 0.8f ? 1.0f : std::max(0.0f, (1.0f - soc_) / 0.2f);
            charge = cfg_.charge_current_a * taper;
        }
        charging_ = charge > 0.01f;
        soc_ += (charge - draw) * dt / (cfg_.capacity_ah * 3600.0f);
        soc_ = std::clamp(soc_, 0.0f, 1.0f);

        buzzer_ = powered ? c.buzzer : 0.0f;
        led_ = powered ? c.led : 0.0f;
    }

    Readings read() {
        Readings r;
        std::normal_distribution<float> n01(0.0f, 1.0f);

        // LiPo discharge curve, roughly: steep at both ends, flat middle.
        // Under load the voltage sags by I*R_internal (~0.15 ohm for a 3S pack).
        float cell = 3.0f + 1.2f * soc_curve(soc_);
        float sag = 0.15f * (cfg_.stepper_current_a * wheel_activity_ * 2.0f);
        r.battery_v = 3.0f * cell - sag + (charging_ ? 0.3f : 0.0f) + 0.02f * n01(rng_);

        // Light: each LDR looks 35 degrees off the head's axis with a
        // cosine-ish acceptance; the station's LED and the window are the
        // sources, falling off with distance squared.
        float heading = pose_.theta + head_angle_;
        r.light_left = light_at(heading + 0.6f) + 0.01f * n01(rng_);
        r.light_right = light_at(heading - 0.6f) + 0.01f * n01(rng_);

        r.bump = bump_;
        r.ir_distance_m = ray_to_wall(pose_.x, pose_.y, pose_.theta);

        // Mic: its own steppers whine, its own buzzer is loud, the station
        // clicks while charging, and the room is never silent.
        float mic = 0.05f + 0.35f * wheel_activity_ + 0.8f * buzzer_ + (charging_ ? 0.25f : 0.0f);
        r.mic_level = std::clamp(mic + 0.03f * n01(rng_), 0.0f, 1.0f);

        // RSSI only updates when a scan completes.
        if (t_ >= next_scan_t_) {
            next_scan_t_ = t_ + cfg_.rssi_scan_period_s;
            float d = std::max(0.05f, distance_to_station());
            float rssi = cfg_.rssi_at_1m_dbm - 10.0f * cfg_.path_loss_exponent * std::log10(d);
            rssi += shadowing(pose_.x, pose_.y) + cfg_.rssi_fast_sigma_db * n01(rng_);
            last_rssi_ = rssi;
        }
        r.rssi_dbm = last_rssi_;
        return r;
    }

    const Pose& pose() const { return pose_; }
    float soc() const { return soc_; }
    bool docked() const { return docked_; }
    bool charging() const { return charging_; }
    bool bumped() const { return bump_; }
    float speed() const { return speed_; }
    float time() const { return t_; }
    const WorldConfig& config() const { return cfg_; }

    float distance_to_station() const {
        return std::hypot(pose_.x - cfg_.station_x, pose_.y - cfg_.station_y);
    }

private:
    static float soc_curve(float soc) {
        // Maps state of charge to 0..1 of the per-cell voltage span with the
        // typical LiPo shape: quick drop off the top, long plateau, cliff at the end.
        if (soc > 0.9f) return 0.85f + (soc - 0.9f) * 1.5f;
        if (soc > 0.1f) return 0.45f + (soc - 0.1f) * 0.5f;
        return soc * 4.5f;
    }

    float light_at(float look) const {
        float total = 0.0f;
        // Station LED.
        float dx = cfg_.station_x - pose_.x, dy = cfg_.station_y - pose_.y;
        float d2 = dx * dx + dy * dy + 0.02f;
        float ang = std::atan2(dy, dx) - look;
        float c = std::cos(ang);
        if (c > 0) total += cfg_.station_led_intensity * c * 0.15f / d2;
        // Window: a broad source above the top wall.
        float wy = cfg_.room_h + 0.5f - pose_.y;
        float wang = std::atan2(wy, 0.0f) - look;
        float wc = std::cos(wang);
        if (wc > 0) total += cfg_.window_intensity * wc * 0.5f / (wy * wy + 0.25f);
        return total;
    }

    float ray_to_wall(float x, float y, float th) const {
        float dx = std::cos(th), dy = std::sin(th);
        float t = 10.0f;
        if (dx > 1e-4f) t = std::min(t, (cfg_.room_w - x) / dx);
        if (dx < -1e-4f) t = std::min(t, -x / dx);
        if (dy > 1e-4f) t = std::min(t, (cfg_.room_h - y) / dy);
        if (dy < -1e-4f) t = std::min(t, -y / dy);
        return std::max(0.0f, t - cfg_.robot_radius);
    }

    // Spatially correlated shadow fading: a fixed sum of random plane waves,
    // so the same spot reads consistently off the ideal curve (walls,
    // furniture, bodies) — the property Wi-Fi fingerprinting relies on.
    float shadowing(float x, float y) const {
        float s = 0.0f;
        for (int i = 0; i < kShadowTerms; i++) {
            s += std::sin(shadow_kx_[i] * x + shadow_ky_[i] * y + shadow_phase_[i]);
        }
        return cfg_.rssi_shadow_sigma_db * s * std::sqrt(2.0f / kShadowTerms);
    }

    static constexpr int kShadowTerms = 12;

    WorldConfig cfg_;
    std::mt19937 rng_;
    Pose pose_{};
    float soc_ = 0.8f;
    float t_ = 0.0f;
    float head_angle_ = 0.0f;
    float prev_head_ = 0.0f;
    float speed_ = 0.0f;
    float wheel_activity_ = 0.0f;
    float buzzer_ = 0.0f;
    float led_ = 0.0f;
    bool bump_ = false;
    bool docked_ = false;
    bool charging_ = false;
    float next_scan_t_ = 0.0f;
    float last_rssi_ = -100.0f;
    float shadow_kx_[kShadowTerms], shadow_ky_[kShadowTerms], shadow_phase_[kShadowTerms];
};

}  // namespace sim
