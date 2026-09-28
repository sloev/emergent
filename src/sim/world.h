#pragma once
//
// A 2D room with one organism and one charging station. Physics and sensor
// models are simple but each is grounded in how the real part behaves
// (datasheet ranges, known noise sources), because the point is to see what
// the behavior engine does with *realistic* inputs, not idealised ones.
//
// Units: metres, seconds, radians, volts, amps, amp-hours.
//
// The organism never sees any of this: only normalized channel values via
// SimBody. World state (pose, state of charge) exists so the simulator can
// move the body and score it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "body_plan.h"

namespace sim {

constexpr float kPi = 3.14159265f;

struct WorldConfig {
    float room_w = 3.0f;
    float room_h = 2.5f;

    // Station: contacts on the right wall at station_y, behind a V funnel.
    float station_y = 1.25f;
    float funnel_depth = 0.30f;
    float funnel_throat = 0.03f;
    float dock_tolerance = 0.05f;   // contacts engage within this of the contact point
    float charge_current_a = 1.0f;  // CC until 80%, then tapering
    float station_led_intensity = 1.0f;

    // RF: log-distance path loss, the standard indoor model.
    float rssi_at_1m_dbm = -45.0f;
    float path_loss_exponent = 2.5f;
    float rssi_shadow_sigma_db = 4.0f;
    float rssi_fast_sigma_db = 2.0f;
    float rssi_period_override = 0.0f;  // >0: force every RSSI sensor to this scan period

    float window_intensity = 0.6f;

    uint32_t seed = 1;
};

struct Pose {
    float x, y, theta;
};

class World {
public:
    World(const WorldConfig& cfg, const BodyPlan& plan) : cfg_(cfg), plan_(plan), rng_(cfg.seed) {
        std::uniform_real_distribution<float> phase(0.0f, 2.0f * kPi);
        for (int i = 0; i < kShadowTerms; i++) {
            shadow_kx_[i] = std::normal_distribution<float>(0.0f, 3.0f)(rng_);
            shadow_ky_[i] = std::normal_distribution<float>(0.0f, 3.0f)(rng_);
            shadow_phase_[i] = phase(rng_);
        }
        prev_act_.assign(plan.actuators.size(), 0.0f);
        dc_speed_[0] = dc_speed_[1] = 0.0f;
        next_scan_.assign(plan.sensors.size(), 0.0f);
        last_rssi_.assign(plan.sensors.size(), -100.0f);
        reset_robot();
    }

    void reset_robot() {
        std::uniform_real_distribution<float> ux(0.3f, cfg_.room_w * 0.5f);
        std::uniform_real_distribution<float> uy(0.3f, cfg_.room_h - 0.3f);
        std::uniform_real_distribution<float> ut(-kPi, kPi);
        pose_ = {ux(rng_), uy(rng_), ut(rng_)};
        soc_ = 0.8f;
    }

    // Advance physics by dt under the actuator values the body just wrote.
    void step(const std::vector<float>& a, float dt) {
        t_ += dt;
        bool powered = soc_ > 0.0f;
        std::normal_distribution<float> n01(0.0f, 1.0f);
        auto val = [&](Role r) {
            int i = plan_.actuator_index(r);
            return (i >= 0 && powered) ? a[static_cast<size_t>(i)] : 0.0f;
        };
        auto delta = [&](Role r) {
            int i = plan_.actuator_index(r);
            return (i >= 0 && powered) ? a[static_cast<size_t>(i)] - prev_act_[static_cast<size_t>(i)] : 0.0f;
        };

        float v = 0.0f, w = 0.0f;
        vibration_ = 0.0f;
        switch (plan_.loco) {
            case Loco::kDiffStepper: {
                float vl = val(Role::kLocoLeft) * plan_.max_speed, vr = val(Role::kLocoRight) * plan_.max_speed;
                v = 0.5f * (vl + vr);
                w = (vr - vl) / plan_.wheel_base;
                break;
            }
            case Loco::kDiffDc: {
                // Gearmotors: no motion below ~15% duty, ~0.3 s to spin up,
                // and two "identical" motors never quite match.
                auto target = [&](float u) {
                    float m = std::fabs(u);
                    return m < 0.15f ? 0.0f : std::copysign((m - 0.15f) / 0.85f, u) * plan_.max_speed;
                };
                float k = dt / 0.3f;
                dc_speed_[0] += (target(val(Role::kLocoLeft)) * 0.95f - dc_speed_[0]) * k;
                dc_speed_[1] += (target(val(Role::kLocoRight)) * 1.05f - dc_speed_[1]) * k;
                v = 0.5f * (dc_speed_[0] + dc_speed_[1]) * (1.0f + 0.05f * n01(rng_));
                w = (dc_speed_[1] - dc_speed_[0]) / plan_.wheel_base;
                break;
            }
            case Loco::kTricycle: {
                v = val(Role::kDrive) * plan_.max_speed;
                float steer = val(Role::kSteer) * (kPi / 3.0f);
                w = v * std::tan(steer) / plan_.wheel_base;
                break;
            }
            case Loco::kVibro2: {
                float l = std::max(0.0f, val(Role::kLocoLeft)), r = std::max(0.0f, val(Role::kLocoRight));
                v = plan_.max_speed * 0.5f * (l + r) * (1.0f + 0.3f * n01(rng_));
                w = 0.8f * (l - r) + 0.6f * (l + r) * n01(rng_);
                vibration_ = 0.5f * (l + r);
                break;
            }
            case Loco::kVibro1: {
                float m = std::max(0.0f, val(Role::kVibro));
                v = plan_.max_speed * m * (1.0f + 0.3f * n01(rng_));
                w = plan_.curve * m + 0.8f * m * n01(rng_);
                vibration_ = m;
                break;
            }
            case Loco::kCrawler: {
                // A leg only pushes the body when it sweeps backward (the
                // foot grips); sweeping forward it drags with a little slip.
                auto push = [&](float d) {
                    float ang = d * (kPi / 4.0f);
                    return ang < 0 ? -ang * plan_.max_speed / dt : -0.15f * ang * plan_.max_speed / dt;
                };
                float vl = push(delta(Role::kLocoLeft)), vr = push(delta(Role::kLocoRight));
                v = 0.5f * (vl + vr);
                w = (vr - vl) / plan_.wheel_base;
                break;
            }
        }

        pose_.theta += w * dt;
        pose_.theta = std::atan2(std::sin(pose_.theta), std::cos(pose_.theta));
        float nx = pose_.x + v * std::cos(pose_.theta) * dt;
        float ny = pose_.y + v * std::sin(pose_.theta) * dt;
        collide(nx, ny);
        speed_ = std::fabs(v);

        int head = plan_.actuator_index(Role::kHead);
        head_angle_ = head >= 0 ? a[static_cast<size_t>(head)] * (kPi / 2.0f) : 0.0f;

        // Current: base draw plus every actuator. Servos draw while moving.
        float draw = plan_.base_current_a;
        noise_ = 0.0f;
        if (powered) {
            for (size_t i = 0; i < plan_.actuators.size(); i++) {
                const ActuatorDef& d = plan_.actuators[i];
                float mag = d.kind == ActuatorKind::kServo ? std::min(1.0f, std::fabs(a[i] - prev_act_[i]) / (dt * 2.0f))
                                                           : std::fabs(a[i]);
                draw += d.current_a * mag;
                noise_ += d.noise * mag;
                if (d.role == Role::kHaptic) vibration_ += a[i];
            }
        }
        for (size_t i = 0; i < a.size(); i++) prev_act_[i] = a[i];
        draw_ = draw;

        docked_ = std::fabs(dock_x() - pose_.x) < cfg_.dock_tolerance && std::fabs(pose_.y - cfg_.station_y) < cfg_.dock_tolerance;
        charge_ = 0.0f;
        if (docked_) {
            float taper = soc_ < 0.8f ? 1.0f : std::max(0.0f, (1.0f - soc_) / 0.2f);
            charge_ = cfg_.charge_current_a * taper;
        }
        charging_ = charge_ > 0.01f;
        soc_ += (charge_ - draw) * dt / (plan_.capacity_ah * 3600.0f);
        soc_ = std::clamp(soc_, 0.0f, 1.0f);

        jolt_ = (bumped_ && !was_bumped_) ? 1.0f : 0.0f;
        was_bumped_ = bumped_;
    }

    // One normalized (0..1) reading for sensor channel i.
    float sense(size_t i) {
        const SensorDef& s = plan_.sensors[i];
        std::normal_distribution<float> n01(0.0f, 1.0f);
        switch (s.type) {
            case Sense::kBattery: {
                // LiPo: steep at both ends, flat middle; sag under load
                // (~0.05 ohm per cell); charger holds it ~0.1 V/cell higher.
                float cell = 3.0f + 1.2f * soc_curve(soc_);
                float v = plan_.cells * cell - draw_ * 0.05f * plan_.cells + (charging_ ? 0.1f * plan_.cells : 0.0f);
                v += 0.02f * n01(rng_);
                return clamp01((v - 3.0f * plan_.cells) / (1.2f * plan_.cells));
            }
            case Sense::kLight: {
                float look = pose_.theta + s.angle + (s.on_head ? head_angle_ : 0.0f);
                float l = light_at(look, true);
                return clamp01(l / (l + 0.5f) + 0.01f * n01(rng_));
            }
            case Sense::kAmbientLight: {
                float l = light_at(0.0f, false);
                return clamp01(l / (l + 0.5f) + 0.01f * n01(rng_));
            }
            case Sense::kMic: {
                float m = 0.05f + noise_ + (charging_ ? 0.25f : 0.0f);
                return clamp01(m + 0.03f * n01(rng_));
            }
            case Sense::kBump: {
                for (float c : contacts_) {
                    float rel = std::atan2(std::sin(c - pose_.theta - s.angle), std::cos(c - pose_.theta - s.angle));
                    if (std::fabs(rel) < s.arc) return 1.0f;
                }
                return 0.0f;
            }
            case Sense::kRangeIr: {
                float d = std::clamp(ray(pose_.theta + s.angle), 0.1f, 0.8f);
                return clamp01((1.0f / d - 1.25f) / (10.0f - 1.25f) + 0.01f * n01(rng_));
            }
            case Sense::kRangeUs: {
                float th = pose_.theta + s.angle;
                float d = std::min({ray(th), ray(th + 0.26f), ray(th - 0.26f)});
                return clamp01(1.0f - std::clamp(d, 0.02f, 2.0f) / 2.0f + 0.005f * n01(rng_));
            }
            case Sense::kRssi: {
                float period = cfg_.rssi_period_override > 0 ? cfg_.rssi_period_override : s.scan_period;
                if (t_ >= next_scan_[i]) {
                    next_scan_[i] = t_ + period;
                    float d = std::max(0.05f, distance_to_station());
                    float rs = cfg_.rssi_at_1m_dbm - 10.0f * cfg_.path_loss_exponent * std::log10(d);
                    last_rssi_[i] = rs + shadowing(pose_.x, pose_.y) + cfg_.rssi_fast_sigma_db * n01(rng_);
                }
                return clamp01((last_rssi_[i] + 90.0f) / 60.0f);
            }
            case Sense::kChargeCurrent:
                return clamp01(charge_ / cfg_.charge_current_a + 0.01f * n01(rng_));
            case Sense::kAccel:
                return clamp01(0.4f * vibration_ + jolt_ + 0.02f * n01(rng_));
            case Sense::kProprio: {
                const ActuatorDef& d = plan_.actuators[static_cast<size_t>(s.proprio_of)];
                float v = prev_act_[static_cast<size_t>(s.proprio_of)];
                return clamp01((v - d.min) / (d.max - d.min) + 0.01f * n01(rng_));
            }
        }
        return 0.0f;
    }

    const Pose& pose() const { return pose_; }
    float soc() const { return soc_; }
    bool docked() const { return docked_; }
    bool charging() const { return charging_; }
    bool bumped() const { return bumped_; }
    float speed() const { return speed_; }
    float time() const { return t_; }
    float head_angle() const { return head_angle_; }
    const WorldConfig& config() const { return cfg_; }
    float dock_x() const { return cfg_.room_w - plan_.radius; }
    float distance_to_station() const { return std::hypot(pose_.x - dock_x(), pose_.y - cfg_.station_y); }

private:
    static float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

    static float soc_curve(float soc) {
        if (soc > 0.9f) return 0.85f + (soc - 0.9f) * 1.5f;
        if (soc > 0.1f) return 0.45f + (soc - 0.1f) * 0.5f;
        return soc * 4.5f;
    }

    // Walls clamp the body and press whatever bumper faces them. The funnel
    // squeezes a body that enters its mouth onto the contacts.
    void collide(float nx, float ny) {
        float r = plan_.radius;
        contacts_.clear();
        if (nx < r) { nx = r; contacts_.push_back(kPi); }
        if (nx > cfg_.room_w - r) { nx = cfg_.room_w - r; contacts_.push_back(0.0f); }
        if (ny < r) { ny = r; contacts_.push_back(-kPi / 2); }
        if (ny > cfg_.room_h - r) { ny = cfg_.room_h - r; contacts_.push_back(kPi / 2); }
        float into = dock_x() - nx;
        if (into < cfg_.funnel_depth && into > -r) {
            float half = std::max(0.0f, into) + cfg_.funnel_throat;
            float off = ny - cfg_.station_y;
            if (std::fabs(off) < cfg_.funnel_depth + cfg_.funnel_throat && std::fabs(off) > half) {
                ny = cfg_.station_y + (off > 0 ? half : -half);
                contacts_.push_back(off > 0 ? kPi / 4 : -kPi / 4);
            }
        }
        pose_.x = nx;
        pose_.y = ny;
        bumped_ = !contacts_.empty();
    }

    float light_at(float look, bool directional) const {
        float total = 0.0f;
        float dx = cfg_.room_w - pose_.x, dy = cfg_.station_y - pose_.y;
        float d2 = dx * dx + dy * dy + 0.02f;
        float c = directional ? std::cos(std::atan2(dy, dx) - look) : 0.6f;
        if (c > 0) total += cfg_.station_led_intensity * c * 0.15f / d2;
        float wy = cfg_.room_h + 0.5f - pose_.y;
        float wc = directional ? std::cos(kPi / 2 - look) : 0.6f;
        if (wc > 0) total += cfg_.window_intensity * wc * 0.5f / (wy * wy + 0.25f);
        return total;
    }

    float ray(float th) const {
        float dx = std::cos(th), dy = std::sin(th);
        float t = 10.0f, x = pose_.x, y = pose_.y;
        if (dx > 1e-4f) t = std::min(t, (cfg_.room_w - x) / dx);
        if (dx < -1e-4f) t = std::min(t, -x / dx);
        if (dy > 1e-4f) t = std::min(t, (cfg_.room_h - y) / dy);
        if (dy < -1e-4f) t = std::min(t, -y / dy);
        return std::max(0.0f, t - plan_.radius);
    }

    // Spatially correlated shadow fading: a fixed sum of random plane waves,
    // so a spot reads consistently off the ideal curve (the property Wi-Fi
    // fingerprinting relies on).
    float shadowing(float x, float y) const {
        float s = 0.0f;
        for (int i = 0; i < kShadowTerms; i++) s += std::sin(shadow_kx_[i] * x + shadow_ky_[i] * y + shadow_phase_[i]);
        return cfg_.rssi_shadow_sigma_db * s * std::sqrt(2.0f / kShadowTerms);
    }

    static constexpr int kShadowTerms = 12;

    WorldConfig cfg_;
    const BodyPlan& plan_;
    std::mt19937 rng_;
    Pose pose_{};
    float soc_ = 0.8f;
    float t_ = 0.0f;
    float head_angle_ = 0.0f;
    float speed_ = 0.0f;
    float draw_ = 0.0f;
    float charge_ = 0.0f;
    float noise_ = 0.0f;
    float vibration_ = 0.0f;
    float jolt_ = 0.0f;
    float dc_speed_[2];
    bool bumped_ = false, was_bumped_ = false;
    bool docked_ = false;
    bool charging_ = false;
    std::vector<float> contacts_;
    std::vector<float> prev_act_;
    std::vector<float> next_scan_;
    std::vector<float> last_rssi_;
    float shadow_kx_[kShadowTerms], shadow_ky_[kShadowTerms], shadow_phase_[kShadowTerms];
};

}  // namespace sim
