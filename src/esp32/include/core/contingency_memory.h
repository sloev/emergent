#pragma once
//
// Contingency memory: a bounded, decaying set of records of "when actuators
// moved like X, sensors moved like Y" — plus a running estimate of whether
// that pattern coincided with falling total drive pressure (things getting
// better). Every record is keyed purely by quantized deltas and a coarse
// physiology context; nothing here is told what a channel means. Consumed by
// ActionGenerator via query_bias() against the sensor-delta code this memory
// just observed (last_sensor_code()) — so the action generator always asks
// "what have I seen follow *this exact* pattern" rather than recomputing its
// own guess at the current delta. Also drives Physiology's curiosity signal
// via last_surprise() — real prediction error instead of raw sensor
// activity. Tuning constants (learn/decay rates, deadzone) live in tuning.h.
//
// begin()/update() are templated on the body type (duck-typed) so they can
// run against a plain, hardware-free test double under the native test
// environment as well as the real Body — see
// test/test_behavior_scenarios/fake_body.h.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "body/body.h"
#include "core/channel_code.h"
#include "core/decay_math.h"
#include "core/math_util.h"
#include "core/physiology.h"
#include "core/tuning.h"

struct ContingencyEntry {
    uint32_t action_code = 0;   // 2 bits/channel: 00 none, 01 rose, 10 fell
    uint32_t sensor_code = 0;   // same encoding, sensor channels
    uint8_t ctx_hash = 0;       // 1 bit per physiology var, thresholded at 0.5
    float strength = 0.0f;      // reliability; reinforced on repeat, decays otherwise
    float mean_drive_delta = 0.0f;  // EMA of (prev_total_drive - total_drive) when seen
    uint16_t age = 0;           // ticks since last reinforcement; saturates, never wraps
    bool occupied = false;
};

// 2 bits per channel: 00 none, 01 rose, 10 fell. 11 is unused/reserved.
inline uint32_t contingency_quantize_deltas(const float* now, const float* prev, size_t count) {
    uint32_t code = 0;
    size_t n = count < kChannelCodeMaxChannels ? count : kChannelCodeMaxChannels;
    for (size_t i = 0; i < n; i++) {
        float d = now[i] - prev[i];
        int8_t sym = 0;
        if (d > kTuning.contingency.deadzone) {
            sym = 1;
        } else if (d < -kTuning.contingency.deadzone) {
            sym = -1;
        }
        code = encode_channel_delta(code, i, sym);
    }
    return code;
}

inline uint8_t contingency_context_hash(Physiology& phys) {
    uint8_t h = 0;
    for (size_t i = 0; i < kPhysVarCount; i++) {
        if (phys.value(static_cast<PhysVar>(i)) >= 0.5f) h |= (1u << i);
    }
    return h;
}

// Simple integer mix (variant of a Jenkins one-at-a-time hash) to spread the
// combined key across the table without needing anything cryptographic.
inline uint32_t contingency_mix(uint32_t a, uint32_t b, uint8_t c) {
    uint32_t h = a;
    h += (h << 10);
    h ^= (h >> 6);
    h += b;
    h += (h << 10);
    h ^= (h >> 6);
    h += c;
    h += (h << 10);
    h ^= (h >> 6);
    h += (h << 3);
    h ^= (h >> 11);
    h += (h << 15);
    return h;
}

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

    template <typename BodyT>
    void begin(BodyT& body) {
        memset(table_, 0, sizeof(table_));
        count_ = 0;
        has_prev_ = false;
        last_surprise_ = 1.0f;

        for (size_t i = 0; i < body.actuator_count() && i < BodyT::kMaxActuators; i++) {
            prev_actuator_[i] = body.actuator_at(i).value();
        }
        for (size_t i = 0; i < body.sensor_count() && i < BodyT::kMaxSensors; i++) {
            prev_sensor_[i] = body.sensor_at(i).last_value();
        }
    }

    // Advance one behavior tick: quantizes this tick's actuator/sensor
    // deltas and physiology context, decays every entry (pruning any that
    // decayed below the prune threshold), computes this tick's surprise
    // (last_surprise()) against what was already known, then reinforces or
    // inserts the record for what just happened.
    template <typename BodyT>
    void update(BodyT& body, Physiology& phys, float dt_s) {
        if (dt_s <= 0.0f) return;

        float now_actuator[BodyT::kMaxActuators];
        float now_sensor[BodyT::kMaxSensors];
        size_t na = body.actuator_count() < BodyT::kMaxActuators ? body.actuator_count() : BodyT::kMaxActuators;
        size_t ns = body.sensor_count() < BodyT::kMaxSensors ? body.sensor_count() : BodyT::kMaxSensors;

        for (size_t i = 0; i < na; i++) now_actuator[i] = body.actuator_at(i).value();
        for (size_t i = 0; i < ns; i++) now_sensor[i] = body.sensor_at(i).read();

        float total_drive = phys.total_drive();

        if (has_prev_) {
            // Decay every occupied entry — O(capacity), trivially cheap at
            // this table size even at a 20 Hz tick — and reclaim any that
            // decayed into denormal-noise territory so they stop occupying
            // a slot and skewing count(). age saturates instead of
            // wrapping: a uint16_t that just increments would silently
            // reset to 0 (looking "fresh") after ~55 minutes at this tick
            // rate.
            for (size_t i = 0; i < kCapacity; i++) {
                if (!table_[i].occupied) continue;

                table_[i].strength = decay_strength(table_[i].strength, kTuning.contingency.decay_rate);
                table_[i].age = bump_age_saturating(table_[i].age);

                if (should_prune(table_[i].strength, kTuning.contingency.prune_threshold)) {
                    table_[i] = ContingencyEntry{};
                    count_--;
                }
            }

            uint32_t action_code = contingency_quantize_deltas(now_actuator, prev_actuator_, na);
            uint32_t sensor_code = contingency_quantize_deltas(now_sensor, prev_sensor_, ns);
            uint8_t ctx = contingency_context_hash(phys);
            float drive_delta = prev_total_drive_ - total_drive;  // positive = things got better

            // Prediction check *before* this tick's observation gets folded
            // into memory below — otherwise every tick would trivially
            // predict itself perfectly. This is real prediction error: how
            // much what just happened differs from what memory already
            // expected given this tick's action, not a proxy like raw
            // sensor activity.
            uint32_t predicted_sensor_code = 0;
            bool have_prediction = predict_from_action(action_code, predicted_sensor_code);
            if (have_prediction) {
                size_t dist = channel_code_distance(predicted_sensor_code, sensor_code);
                last_surprise_ = ns > 0 ? clampf(static_cast<float>(dist) / static_cast<float>(ns), 0.0f, 1.0f) : 0.0f;
            } else {
                last_surprise_ = 1.0f;  // never seen this action before: maximally novel
            }

            last_sensor_code_ = sensor_code;
            insert_or_reinforce(action_code, sensor_code, ctx, drive_delta);
        }

        memcpy(prev_actuator_, now_actuator, sizeof(float) * na);
        memcpy(prev_sensor_, now_sensor, sizeof(float) * ns);
        prev_total_drive_ = total_drive;
        has_prev_ = true;
    }

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
    bool query_bias(uint32_t sensor_code, uint32_t& out_action_code, float& out_confidence) const {
        bool found = false;
        float best_score = -1e9f;

        for (size_t i = 0; i < kCapacity; i++) {
            const ContingencyEntry& e = table_[i];
            if (!e.occupied) continue;

            size_t dist = channel_code_distance(e.sensor_code, sensor_code);
            if (dist > kMaxQueryHammingDistance) continue;

            // Soft matching: tolerate a channel or two differing
            // (kMaxQueryHammingDistance), but discount confidence the
            // further the match is from exact — otherwise a handful of
            // sensors makes the exact signature space sparse enough that
            // this almost never fires.
            float discount = 1.0f / static_cast<float>(1 + dist);
            float score = e.mean_drive_delta * e.strength * discount;
            if (score > best_score) {
                best_score = score;
                out_action_code = e.action_code;
                out_confidence = e.strength * discount;
                found = true;
            }
        }
        return found;
    }

    // Copies up to max_out occupied entries, strongest first, into `out`.
    // For diagnostics/dashboard use only — not on any hot path.
    size_t top_entries(ContingencyEntry* out, size_t max_out) const {
        // Scans every slot (must — the strongest entries could be anywhere)
        // and maintains a sorted top-`max_out` buffer by insertion. max_out
        // is small (dashboard display), so the shifting below is cheap.
        size_t written = 0;
        for (size_t i = 0; i < kCapacity; i++) {
            const ContingencyEntry& e = table_[i];
            if (!e.occupied) continue;
            if (written == max_out && e.strength <= out[written - 1].strength) continue;

            size_t pos = written < max_out ? written : max_out - 1;
            while (pos > 0 && out[pos - 1].strength < e.strength) {
                out[pos] = out[pos - 1];
                pos--;
            }
            out[pos] = e;
            if (written < max_out) written++;
        }
        return written;
    }

    // Encoding helpers, exposed so the dashboard can decode/encode codes
    // without duplicating the bit layout — decode for display, encode for
    // life-state restore, where entries are serialized by channel *name*
    // (portable across boards) and have to be re-packed into this body's
    // bit positions on load.
    static int8_t decode_channel(uint32_t code, size_t channel_index) {
        return decode_channel_delta(code, channel_index);
    }
    static uint32_t encode_channel(uint32_t code, size_t channel_index, int8_t delta) {
        return encode_channel_delta(code, channel_index, delta);
    }

    // Empties the table. Life-state restore starts from a clean slate
    // rather than merging with whatever the board already learned this
    // session.
    void clear() {
        memset(table_, 0, sizeof(table_));
        count_ = 0;
    }

    // Inserts a fully-formed entry (probe-and-evict, same placement policy
    // as the tick-time path) without going through the EMA blending
    // insert_or_reinforce() does — restore wants to set these fields
    // exactly as saved, not treat them as a fresh single observation.
    void restore_raw(uint32_t action_code, uint32_t sensor_code, uint8_t ctx_hash, float strength,
                      float mean_drive_delta, uint16_t age) {
        if (action_code == 0 && sensor_code == 0) return;  // nothing survived remapping

        uint32_t h = contingency_mix(action_code, sensor_code, ctx_hash);
        size_t start = h % kCapacity;

        size_t weakest_slot = start;
        float weakest_strength = 2.0f;

        for (size_t probe = 0; probe < kMaxProbe; probe++) {
            size_t slot = (start + probe) % kCapacity;
            ContingencyEntry& e = table_[slot];

            if (!e.occupied) {
                e = ContingencyEntry{action_code, sensor_code, ctx_hash, strength, mean_drive_delta, age, true};
                count_++;
                return;
            }
            if (e.action_code == action_code && e.sensor_code == sensor_code && e.ctx_hash == ctx_hash) {
                // Two saved entries remapped onto the same code (channels
                // that didn't exist on this body were dropped from both) —
                // keep whichever was stronger rather than silently
                // overwriting.
                if (strength > e.strength) {
                    e = ContingencyEntry{action_code, sensor_code, ctx_hash, strength, mean_drive_delta, age, true};
                }
                return;
            }
            if (e.strength < weakest_strength) {
                weakest_strength = e.strength;
                weakest_slot = slot;
            }
        }

        table_[weakest_slot] =
            ContingencyEntry{action_code, sensor_code, ctx_hash, strength, mean_drive_delta, age, true};
    }

private:
    uint32_t insert_or_reinforce(uint32_t action_code, uint32_t sensor_code, uint8_t ctx_hash, float drive_delta) {
        uint32_t h = contingency_mix(action_code, sensor_code, ctx_hash);
        size_t start = h % kCapacity;

        size_t weakest_slot = start;
        float weakest_strength = 2.0f;  // strength is always <= 1, so this always loses

        for (size_t probe = 0; probe < kMaxProbe; probe++) {
            size_t slot = (start + probe) % kCapacity;
            ContingencyEntry& e = table_[slot];

            if (!e.occupied) {
                e.occupied = true;
                e.action_code = action_code;
                e.sensor_code = sensor_code;
                e.ctx_hash = ctx_hash;
                e.strength = kTuning.contingency.learn_rate;
                e.mean_drive_delta = drive_delta;
                e.age = 0;
                count_++;
                return slot;
            }

            if (e.action_code == action_code && e.sensor_code == sensor_code && e.ctx_hash == ctx_hash) {
                e.strength += kTuning.contingency.learn_rate * (1.0f - e.strength);
                e.mean_drive_delta += kTuning.contingency.learn_rate * (drive_delta - e.mean_drive_delta);
                e.age = 0;
                return slot;
            }

            if (e.strength < weakest_strength) {
                weakest_strength = e.strength;
                weakest_slot = slot;
            }
        }

        // No exact match and no free slot within the probe window: evict
        // the weakest entry seen. Table stays bounded at kCapacity without
        // ever needing a separate "prune the whole table" pass.
        ContingencyEntry& e = table_[weakest_slot];
        e.occupied = true;
        e.action_code = action_code;
        e.sensor_code = sensor_code;
        e.ctx_hash = ctx_hash;
        e.strength = kTuning.contingency.learn_rate;
        e.mean_drive_delta = drive_delta;
        e.age = 0;
        return weakest_slot;
    }

    bool predict_from_action(uint32_t action_code, uint32_t& out_sensor_code) const {
        bool found = false;
        size_t best_distance = kMaxQueryHammingDistance + 1;  // sentinel: worse than any acceptable match
        float best_strength = -1.0f;

        for (size_t i = 0; i < kCapacity; i++) {
            const ContingencyEntry& e = table_[i];
            if (!e.occupied) continue;

            size_t dist = channel_code_distance(e.action_code, action_code);
            if (dist > kMaxQueryHammingDistance) continue;

            if (dist < best_distance || (dist == best_distance && e.strength > best_strength)) {
                best_distance = dist;
                best_strength = e.strength;
                out_sensor_code = e.sensor_code;
                found = true;
            }
        }
        return found;
    }

    ContingencyEntry table_[kCapacity];
    size_t count_ = 0;

    float prev_actuator_[Body::kMaxActuators] = {};
    float prev_sensor_[Body::kMaxSensors] = {};
    bool has_prev_ = false;
    float prev_total_drive_ = 0.0f;
    uint32_t last_sensor_code_ = 0;
    float last_surprise_ = 1.0f;  // everything is novel before any experience exists
};
