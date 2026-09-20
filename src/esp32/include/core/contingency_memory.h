#pragma once
//
// Contingency memory (docs/synth-behavior.md §7).
//
// A bounded, decaying set of records of "when actuators moved like X,
// sensors moved like Y" — plus, as an extension the article's pseudocode
// leaves implicit, a running estimate of whether that pattern coincided with
// falling total drive pressure (things getting better). Every record is
// keyed purely by quantized deltas and a coarse physiology context; nothing
// here is told what a channel means. Consumed by ActionGenerator (v0.7.0)
// via query_bias() against the sensor-delta code this memory just observed
// (last_sensor_code()) — so the action generator always asks "what have I
// seen follow *this exact* pattern" rather than recomputing its own guess
// at the current delta.

#include <cstddef>
#include <cstdint>

#include "body/body.h"
#include "core/physiology.h"

struct ContingencyEntry {
    uint32_t action_code = 0;   // 2 bits/channel: 00 none, 01 rose, 10 fell
    uint32_t sensor_code = 0;   // same encoding, sensor channels
    uint8_t ctx_hash = 0;       // 1 bit per physiology var, thresholded at 0.5
    float strength = 0.0f;      // reliability; reinforced on repeat, decays otherwise
    float mean_drive_delta = 0.0f;  // EMA of (prev_total_drive - total_drive) when seen
    uint16_t age = 0;           // ticks since last reinforcement
    bool occupied = false;
};

class ContingencyMemory {
public:
    static constexpr size_t kCapacity = 256;  // power of 2
    static constexpr float kLearnRate = 0.1f;
    static constexpr float kDecayRate = 0.005f;
    static constexpr size_t kMaxProbe = 8;  // bound worst-case insert work

    void begin(Body& body);

    // Advance one behavior tick: quantizes this tick's actuator/sensor
    // deltas and physiology context, decays every entry, then reinforces or
    // inserts the record for what just happened.
    void update(Body& body, Physiology& phys, float dt_s);

    size_t count() const { return count_; }
    static constexpr size_t capacity() { return kCapacity; }

    // The sensor-delta code computed on the most recent update() — what to
    // pass to query_bias() to ask "what usually happens after what I'm
    // seeing right now".
    uint32_t last_sensor_code() const { return last_sensor_code_; }

    // Best-effort recall: does memory contain a pattern whose sensor_code
    // matches `sensor_code` and that historically preceded falling drive
    // pressure? If so, fills out_action_code (decode with decode_channel())
    // and out_confidence, and returns true. Exact-match only for now — no
    // action generator exists yet to consume near-matches, so there's no
    // behavior riding on this beyond what's tested directly.
    bool query_bias(uint32_t sensor_code, uint32_t& out_action_code, float& out_confidence) const;

    // Copies up to max_out occupied entries, strongest first, into `out`.
    // For diagnostics/dashboard use only — not on any hot path.
    size_t top_entries(ContingencyEntry* out, size_t max_out) const;

    // Encoding helpers, exposed so the dashboard can decode codes for
    // display without duplicating the bit layout.
    static int8_t decode_channel(uint32_t code, size_t channel_index);

private:
    uint32_t insert_or_reinforce(uint32_t action_code, uint32_t sensor_code, uint8_t ctx_hash,
                                  float drive_delta);

    ContingencyEntry table_[kCapacity];
    size_t count_ = 0;

    float prev_actuator_[Body::kMaxActuators] = {};
    float prev_sensor_[Body::kMaxSensors] = {};
    bool has_prev_ = false;
    float prev_total_drive_ = 0.0f;
    uint32_t last_sensor_code_ = 0;
};
