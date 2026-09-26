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
// at the current delta. Also drives Physiology's curiosity signal via
// last_surprise() — real prediction error instead of raw sensor activity.

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
    uint16_t age = 0;           // ticks since last reinforcement; saturates, never wraps
    bool occupied = false;
};

class ContingencyMemory {
public:
    static constexpr size_t kCapacity = 256;  // power of 2
    static constexpr size_t kMaxProbe = 8;    // bound worst-case insert work

    // query_bias() tolerates entries whose sensor_code differs from the
    // query by up to this many channel symbols (out of up to 16), so recall
    // isn't limited to bit-exact matches — with few sensors the signature
    // space is small enough that exact match is plausible, but it thins out
    // fast as more sensors are added. Each differing symbol also discounts
    // the match's score (see query_bias()).
    static constexpr size_t kMaxQueryHammingDistance = 1;

    void begin(Body& body);

    // Advance one behavior tick: quantizes this tick's actuator/sensor
    // deltas and physiology context, decays every entry (pruning any that
    // decayed below kPruneThreshold), computes this tick's surprise
    // (last_surprise()) against what was already known, then reinforces or
    // inserts the record for what just happened.
    void update(Body& body, Physiology& phys, float dt_s);

    size_t count() const { return count_; }
    static constexpr size_t capacity() { return kCapacity; }

    // The sensor-delta code computed on the most recent update() — what to
    // pass to query_bias() to ask "what usually happens after what I'm
    // seeing right now".
    uint32_t last_sensor_code() const { return last_sensor_code_; }

    // How much this tick's actual sensor_code differed from what memory
    // would have predicted for this tick's action_code, in [0,1] (0 =
    // exactly as expected, 1 = every channel differed or this action has
    // never been observed before). Real prediction error, computed *before*
    // this tick's observation is folded into memory — feeds
    // Physiology::update()'s curiosity term.
    float last_surprise() const { return last_surprise_; }

    // Best-effort recall: does memory contain a pattern whose sensor_code
    // is within kMaxQueryHammingDistance of `sensor_code` and that
    // historically preceded falling drive pressure? If so, fills
    // out_action_code (decode with decode_channel()) and out_confidence
    // (discounted by match distance), and returns true.
    bool query_bias(uint32_t sensor_code, uint32_t& out_action_code, float& out_confidence) const;

    // Copies up to max_out occupied entries, strongest first, into `out`.
    // For diagnostics/dashboard use only — not on any hot path.
    size_t top_entries(ContingencyEntry* out, size_t max_out) const;

    // Encoding helpers, exposed so the dashboard can decode/encode codes
    // without duplicating the bit layout — decode for display, encode for
    // life-state restore (docs/synth-behavior.md §13), where entries are
    // serialized by channel *name* (portable across boards) and have to be
    // re-packed into this body's bit positions on load.
    static int8_t decode_channel(uint32_t code, size_t channel_index);
    static uint32_t encode_channel(uint32_t code, size_t channel_index, int8_t delta);

    // Empties the table. Life-state restore starts from a clean slate
    // rather than merging with whatever the board already learned this
    // session.
    void clear();

    // Inserts a fully-formed entry (probe-and-evict, same placement policy
    // as the tick-time path) without going through the EMA blending
    // insert_or_reinforce() does — restore wants to set these fields
    // exactly as saved, not treat them as a fresh single observation.
    void restore_raw(uint32_t action_code, uint32_t sensor_code, uint8_t ctx_hash, float strength,
                      float mean_drive_delta, uint16_t age);

private:
    uint32_t insert_or_reinforce(uint32_t action_code, uint32_t sensor_code, uint8_t ctx_hash,
                                  float drive_delta);
    bool predict_from_action(uint32_t action_code, uint32_t& out_sensor_code) const;

    ContingencyEntry table_[kCapacity];
    size_t count_ = 0;

    float prev_actuator_[Body::kMaxActuators] = {};
    float prev_sensor_[Body::kMaxSensors] = {};
    bool has_prev_ = false;
    float prev_total_drive_ = 0.0f;
    uint32_t last_sensor_code_ = 0;
    float last_surprise_ = 1.0f;  // everything is novel before any experience exists
};
