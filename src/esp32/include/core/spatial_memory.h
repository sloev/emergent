#pragma once
//
// Spatial memory.
//
// This hardware has no positioning sensor — no encoders, no IMU, no GPS.
// Rather than fabricate open-loop odometry from commanded motor duty cycle
// (which would drift into nonsense within seconds, since duty cycle isn't
// measured displacement), a place cell here is identified by a coarse
// signature of the current sensor readings — same ambient light/RF/whatever
// reads the same, treated as the same place.
//
// Without real coordinates there's no direction to steer in: best_cell()
// returns *which remembered place looks most promising*, not a heading.
// ActionGenerator uses it to modulate how much exploration noise to inject
// (wider when a better-scoring place than "here" is known to exist,
// narrower when "here" already looks best) — an explore/exploit signal, not
// motor bias. Turning this into an actual heading needs a real odometry
// source, which this hardware doesn't have.
//
// Entirely header-only: nothing here touches hardware (compute_signature()
// is templated on the body type — duck-typed — precisely so it also runs
// against the plain, hardware-free test double under the native test
// environment; see test/test_behavior_scenarios/fake_body.h).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "body/body.h"
#include "core/channel_code.h"
#include "core/decay_math.h"
#include "core/physiology.h"
#include "core/tuning.h"

struct SpatialCell {
    uint32_t signature = 0;   // coarse quantized sensor snapshot identifying this "place"
    uint32_t visit_count = 0;
    uint16_t age = 0;         // ticks since last visited
    float drive_improvement[kPhysVarCount] = {};  // EMA of (prev_drive - drive) while here, per variable
    bool occupied = false;
};

inline uint32_t spatial_mix(uint32_t a) {
    uint32_t h = a;
    h += (h << 10);
    h ^= (h >> 6);
    h += (h << 3);
    h ^= (h >> 11);
    h += (h << 15);
    return h;
}

class SpatialMemory {
public:
    static constexpr size_t kCapacity = 32;
    static constexpr size_t kMaxProbe = 8;

    void begin(Body& body) {
        memset(table_, 0, sizeof(table_));
        count_ = 0;
        has_prev_ = false;
        (void)body;
    }

    // Advance one behavior tick: computes this tick's place signature,
    // ages every cell, and reinforces/inserts the record for the place
    // we're in now with how drives changed since last tick.
    void update(Body& body, Physiology& phys, float dt_s) {
        if (dt_s <= 0.0f) return;

        uint32_t signature = compute_signature(body);

        float drive_now[kPhysVarCount];
        for (size_t v = 0; v < kPhysVarCount; v++) drive_now[v] = phys.drive(static_cast<PhysVar>(v));

        if (has_prev_) {
            // Saturate rather than let a uint16_t silently wrap to 0
            // (looking "just visited") after ~55 minutes at this tick rate.
            for (size_t i = 0; i < kCapacity; i++) {
                if (table_[i].occupied) table_[i].age = bump_age_saturating(table_[i].age);
            }

            float drive_delta[kPhysVarCount];
            for (size_t v = 0; v < kPhysVarCount; v++) drive_delta[v] = prev_drive_[v] - drive_now[v];

            insert_or_update(signature, drive_delta);
        }

        memcpy(prev_drive_, drive_now, sizeof(drive_now));
        has_prev_ = true;
    }

    size_t count() const { return count_; }
    static constexpr size_t capacity() { return kCapacity; }

    // Coarse sensor-based place identity: 2 bits/channel, which quartile of
    // [0,1] each sensor's reading falls in. This bins absolute sensor level
    // (not change, unlike contingency memory's deltas) — place identity is
    // about ambient conditions, "where am I", not "what just moved".
    template <typename BodyT>
    static uint32_t compute_signature(BodyT& body) {
        uint32_t code = 0;
        size_t n = body.sensor_count() < kChannelCodeMaxChannels ? body.sensor_count() : kChannelCodeMaxChannels;
        for (size_t i = 0; i < n; i++) {
            float v = body.sensor_at(i).read();
            uint32_t level = static_cast<uint32_t>(v * 4.0f);
            if (level > 3) level = 3;
            code = encode_channel_level(code, i, level);
        }
        return code;
    }

    // The occupied cell with the highest current-drive-weighted expected
    // improvement, i.e. "which known place has historically helped most
    // with what's pressing right now". Returns false if memory is empty.
    bool best_cell(const Physiology& phys, uint32_t& out_signature, float& out_score) const {
        bool found = false;
        float best = -1e9f;

        for (size_t i = 0; i < kCapacity; i++) {
            const SpatialCell& c = table_[i];
            if (!c.occupied) continue;

            float score = 0.0f;
            for (size_t v = 0; v < kPhysVarCount; v++) {
                score += phys.drive(static_cast<PhysVar>(v)) * c.drive_improvement[v];
            }

            if (score > best) {
                best = score;
                out_signature = c.signature;
                out_score = score;
                found = true;
            }
        }
        return found;
    }

    // Raw table access for the dashboard's fixed-grid view — every slot,
    // occupied or not, so the grid layout is stable across calls.
    const SpatialCell& cell_at(size_t i) const { return table_[i]; }

    // Signature encoding helpers (2 bits/channel, quantized into 4 levels),
    // exposed for life-state serialize/restore, where signatures are saved
    // by sensor *name* and re-packed into this body's bit positions on load.
    static uint32_t decode_level(uint32_t signature, size_t channel_index) {
        return decode_channel_level(signature, channel_index);
    }
    static uint32_t encode_level(uint32_t signature, size_t channel_index, uint32_t level) {
        return encode_channel_level(signature, channel_index, level);
    }

    void clear() {
        memset(table_, 0, sizeof(table_));
        count_ = 0;
    }

    void restore_raw(uint32_t signature, uint32_t visit_count, uint16_t age, const float* drive_improvement) {
        uint32_t h = spatial_mix(signature);
        size_t start = h % kCapacity;

        size_t weakest_slot = start;
        uint32_t weakest_visits = UINT32_MAX;

        for (size_t probe = 0; probe < kMaxProbe; probe++) {
            size_t slot = (start + probe) % kCapacity;
            SpatialCell& c = table_[slot];

            if (!c.occupied) {
                c.occupied = true;
                c.signature = signature;
                c.visit_count = visit_count;
                c.age = age;
                memcpy(c.drive_improvement, drive_improvement, sizeof(c.drive_improvement));
                count_++;
                return;
            }
            if (c.signature == signature) {
                // Two saved cells collapsed onto the same remapped signature
                // (channels missing on this body were dropped from both) —
                // keep whichever was visited more.
                if (visit_count > c.visit_count) {
                    c.visit_count = visit_count;
                    c.age = age;
                    memcpy(c.drive_improvement, drive_improvement, sizeof(c.drive_improvement));
                }
                return;
            }
            if (c.visit_count < weakest_visits) {
                weakest_visits = c.visit_count;
                weakest_slot = slot;
            }
        }

        SpatialCell& c = table_[weakest_slot];
        c.occupied = true;
        c.signature = signature;
        c.visit_count = visit_count;
        c.age = age;
        memcpy(c.drive_improvement, drive_improvement, sizeof(c.drive_improvement));
    }

private:
    uint32_t insert_or_update(uint32_t signature, const float* drive_delta) {
        uint32_t h = spatial_mix(signature);
        size_t start = h % kCapacity;

        size_t weakest_slot = start;
        uint32_t weakest_visits = UINT32_MAX;

        for (size_t probe = 0; probe < kMaxProbe; probe++) {
            size_t slot = (start + probe) % kCapacity;
            SpatialCell& c = table_[slot];

            if (!c.occupied) {
                c.occupied = true;
                c.signature = signature;
                c.visit_count = 1;
                c.age = 0;
                for (size_t v = 0; v < kPhysVarCount; v++) c.drive_improvement[v] = drive_delta[v];
                count_++;
                return slot;
            }

            if (c.signature == signature) {
                c.visit_count++;
                c.age = 0;
                for (size_t v = 0; v < kPhysVarCount; v++) {
                    c.drive_improvement[v] += kTuning.spatial.learn_rate * (drive_delta[v] - c.drive_improvement[v]);
                }
                return slot;
            }

            if (c.visit_count < weakest_visits) {
                weakest_visits = c.visit_count;
                weakest_slot = slot;
            }
        }

        // No match and no free slot within the probe window: evict the
        // least visited cell seen. Table stays bounded at kCapacity.
        SpatialCell& c = table_[weakest_slot];
        c.occupied = true;
        c.signature = signature;
        c.visit_count = 1;
        c.age = 0;
        for (size_t v = 0; v < kPhysVarCount; v++) c.drive_improvement[v] = drive_delta[v];
        return weakest_slot;
    }

    SpatialCell table_[kCapacity];
    size_t count_ = 0;
    bool has_prev_ = false;
    float prev_drive_[kPhysVarCount] = {};
};
