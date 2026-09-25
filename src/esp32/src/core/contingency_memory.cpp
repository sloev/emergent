#include "core/contingency_memory.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>

#include "core/channel_code.h"
#include "core/decay_math.h"

namespace {
constexpr float kDeadzone = 0.02f;  // |Δ| below this counts as "no change"

float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 2 bits per channel: 00 none, 01 rose, 10 fell. 11 is unused/reserved.
uint32_t quantize_deltas(const float* now, const float* prev, size_t count) {
    uint32_t code = 0;
    size_t n = count < 16 ? count : 16;  // 2 bits x 16 channels = 32 bits
    for (size_t i = 0; i < n; i++) {
        float d = now[i] - prev[i];
        int8_t sym = 0;
        if (d > kDeadzone) {
            sym = 1;
        } else if (d < -kDeadzone) {
            sym = -1;
        }
        code = encode_channel_delta(code, i, sym);
    }
    return code;
}

uint8_t context_hash(Physiology& phys) {
    uint8_t h = 0;
    for (size_t i = 0; i < kPhysVarCount; i++) {
        if (phys.value(static_cast<PhysVar>(i)) >= 0.5f) h |= (1u << i);
    }
    return h;
}

// Simple integer mix (variant of a Jenkins one-at-a-time hash) to spread the
// combined key across the table without needing anything cryptographic.
uint32_t mix(uint32_t a, uint32_t b, uint8_t c) {
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
}  // namespace

int8_t ContingencyMemory::decode_channel(uint32_t code, size_t channel_index) {
    return decode_channel_delta(code, channel_index);
}

uint32_t ContingencyMemory::encode_channel(uint32_t code, size_t channel_index, int8_t delta) {
    return encode_channel_delta(code, channel_index, delta);
}

void ContingencyMemory::clear() {
    memset(table_, 0, sizeof(table_));
    count_ = 0;
}

void ContingencyMemory::restore_raw(uint32_t action_code, uint32_t sensor_code, uint8_t ctx_hash,
                                     float strength, float mean_drive_delta, uint16_t age) {
    if (action_code == 0 && sensor_code == 0) return;  // nothing survived remapping

    uint32_t h = mix(action_code, sensor_code, ctx_hash);
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
            // Two saved entries remapped onto the same code (channels that
            // didn't exist on this body were dropped from both) — keep
            // whichever was stronger rather than silently overwriting.
            if (strength > e.strength) e = ContingencyEntry{action_code, sensor_code, ctx_hash, strength,
                                                             mean_drive_delta, age, true};
            return;
        }
        if (e.strength < weakest_strength) {
            weakest_strength = e.strength;
            weakest_slot = slot;
        }
    }

    table_[weakest_slot] = ContingencyEntry{action_code, sensor_code, ctx_hash, strength, mean_drive_delta,
                                             age, true};
}

void ContingencyMemory::begin(Body& body) {
    memset(table_, 0, sizeof(table_));
    count_ = 0;
    has_prev_ = false;
    last_surprise_ = 1.0f;

    for (size_t i = 0; i < body.actuator_count() && i < Body::kMaxActuators; i++) {
        prev_actuator_[i] = body.actuator_at(i).value();
    }
    for (size_t i = 0; i < body.sensor_count() && i < Body::kMaxSensors; i++) {
        prev_sensor_[i] = body.sensor_at(i).last_value();
    }
}

uint32_t ContingencyMemory::insert_or_reinforce(uint32_t action_code, uint32_t sensor_code,
                                                 uint8_t ctx_hash, float drive_delta) {
    uint32_t h = mix(action_code, sensor_code, ctx_hash);
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
            e.strength = kLearnRate;
            e.mean_drive_delta = drive_delta;
            e.age = 0;
            count_++;
            return slot;
        }

        if (e.action_code == action_code && e.sensor_code == sensor_code && e.ctx_hash == ctx_hash) {
            e.strength += kLearnRate * (1.0f - e.strength);
            e.mean_drive_delta += kLearnRate * (drive_delta - e.mean_drive_delta);
            e.age = 0;
            return slot;
        }

        if (e.strength < weakest_strength) {
            weakest_strength = e.strength;
            weakest_slot = slot;
        }
    }

    // No exact match and no free slot within the probe window: evict the
    // weakest entry seen. Table stays bounded at kCapacity without ever
    // needing a separate "prune the whole table" pass.
    ContingencyEntry& e = table_[weakest_slot];
    e.occupied = true;
    e.action_code = action_code;
    e.sensor_code = sensor_code;
    e.ctx_hash = ctx_hash;
    e.strength = kLearnRate;
    e.mean_drive_delta = drive_delta;
    e.age = 0;
    return weakest_slot;
}

bool ContingencyMemory::predict_from_action(uint32_t action_code, uint32_t& out_sensor_code) const {
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

void ContingencyMemory::update(Body& body, Physiology& phys, float dt_s) {
    if (dt_s <= 0.0f) return;

    float now_actuator[Body::kMaxActuators];
    float now_sensor[Body::kMaxSensors];
    size_t na = body.actuator_count() < Body::kMaxActuators ? body.actuator_count() : Body::kMaxActuators;
    size_t ns = body.sensor_count() < Body::kMaxSensors ? body.sensor_count() : Body::kMaxSensors;

    for (size_t i = 0; i < na; i++) now_actuator[i] = body.actuator_at(i).value();
    for (size_t i = 0; i < ns; i++) now_sensor[i] = body.sensor_at(i).read();

    float total_drive = phys.total_drive();

    if (has_prev_) {
        // Decay every occupied entry — O(capacity), trivially cheap at this
        // table size even at a 20 Hz tick — and reclaim any that decayed
        // into denormal-noise territory so they stop occupying a slot and
        // skewing count(). age saturates instead of wrapping: a uint16_t
        // that just increments would silently reset to 0 (looking "fresh")
        // after ~55 minutes at this tick rate.
        for (size_t i = 0; i < kCapacity; i++) {
            if (!table_[i].occupied) continue;

            table_[i].strength = decay_strength(table_[i].strength, kDecayRate);
            table_[i].age = bump_age_saturating(table_[i].age);

            if (should_prune(table_[i].strength, kPruneThreshold)) {
                table_[i] = ContingencyEntry{};
                count_--;
            }
        }

        uint32_t action_code = quantize_deltas(now_actuator, prev_actuator_, na);
        uint32_t sensor_code = quantize_deltas(now_sensor, prev_sensor_, ns);
        uint8_t ctx = context_hash(phys);
        float drive_delta = prev_total_drive_ - total_drive;  // positive = things got better

        // Prediction check *before* this tick's observation gets folded
        // into memory below — otherwise every tick would trivially predict
        // itself perfectly. This is real prediction error: how much what
        // just happened differs from what memory already expected given
        // this tick's action, not a proxy like raw sensor activity.
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

bool ContingencyMemory::query_bias(uint32_t sensor_code, uint32_t& out_action_code,
                                    float& out_confidence) const {
    bool found = false;
    float best_score = -1e9f;

    for (size_t i = 0; i < kCapacity; i++) {
        const ContingencyEntry& e = table_[i];
        if (!e.occupied) continue;

        size_t dist = channel_code_distance(e.sensor_code, sensor_code);
        if (dist > kMaxQueryHammingDistance) continue;

        // Soft matching: tolerate a channel or two differing (kMaxQuery
        // HammingDistance), but discount confidence the further the match
        // is from exact — otherwise a handful of sensors makes the exact
        // signature space sparse enough that this almost never fires.
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

size_t ContingencyMemory::top_entries(ContingencyEntry* out, size_t max_out) const {
    // Scans every slot (must — the strongest entries could be anywhere) and
    // maintains a sorted top-`max_out` buffer by insertion. max_out is small
    // (dashboard display), so the shifting below is cheap.
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
