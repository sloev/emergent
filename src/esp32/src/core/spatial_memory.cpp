#include "core/spatial_memory.h"

#include <Arduino.h>
#include <cstring>

namespace {
// 2 bits/channel: which quartile of [0,1] the reading falls in. This bins
// absolute sensor level (not change, unlike contingency memory's deltas) —
// place identity is about ambient conditions, "where am I", not "what just
// moved".
uint32_t quantize_levels(Body& body) {
    uint32_t code = 0;
    size_t n = body.sensor_count() < 16 ? body.sensor_count() : 16;  // 2 bits x 16 = 32 bits
    for (size_t i = 0; i < n; i++) {
        float v = body.sensor_at(i).read();
        uint32_t level = static_cast<uint32_t>(v * 4.0f);
        if (level > 3) level = 3;
        code |= level << (2 * i);
    }
    return code;
}

uint32_t mix(uint32_t a) {
    uint32_t h = a;
    h += (h << 10);
    h ^= (h >> 6);
    h += (h << 3);
    h ^= (h >> 11);
    h += (h << 15);
    return h;
}
}  // namespace

uint32_t SpatialMemory::compute_signature(Body& body) { return quantize_levels(body); }

uint32_t SpatialMemory::decode_level(uint32_t signature, size_t channel_index) {
    if (channel_index >= 16) return 0;
    return (signature >> (2 * channel_index)) & 0x3u;
}

uint32_t SpatialMemory::encode_level(uint32_t signature, size_t channel_index, uint32_t level) {
    if (channel_index >= 16) return signature;
    uint32_t shift = 2 * channel_index;
    signature &= ~(0x3u << shift);
    return signature | ((level & 0x3u) << shift);
}

void SpatialMemory::clear() {
    memset(table_, 0, sizeof(table_));
    count_ = 0;
}

void SpatialMemory::restore_raw(uint32_t signature, uint32_t visit_count, uint16_t age,
                                 const float* drive_improvement) {
    uint32_t h = mix(signature);
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

void SpatialMemory::begin(Body& body) {
    memset(table_, 0, sizeof(table_));
    count_ = 0;
    has_prev_ = false;
    (void)body;
}

uint32_t SpatialMemory::insert_or_update(uint32_t signature, const float* drive_delta) {
    uint32_t h = mix(signature);
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
                c.drive_improvement[v] += kLearnRate * (drive_delta[v] - c.drive_improvement[v]);
            }
            return slot;
        }

        if (c.visit_count < weakest_visits) {
            weakest_visits = c.visit_count;
            weakest_slot = slot;
        }
    }

    // No match and no free slot within the probe window: evict the least
    // visited cell seen. Table stays bounded at kCapacity.
    SpatialCell& c = table_[weakest_slot];
    c.occupied = true;
    c.signature = signature;
    c.visit_count = 1;
    c.age = 0;
    for (size_t v = 0; v < kPhysVarCount; v++) c.drive_improvement[v] = drive_delta[v];
    return weakest_slot;
}

void SpatialMemory::update(Body& body, Physiology& phys, float dt_s) {
    if (dt_s <= 0.0f) return;

    uint32_t signature = compute_signature(body);

    float drive_now[kPhysVarCount];
    for (size_t v = 0; v < kPhysVarCount; v++) drive_now[v] = phys.drive(static_cast<PhysVar>(v));

    if (has_prev_) {
        for (size_t i = 0; i < kCapacity; i++) {
            if (table_[i].occupied) table_[i].age++;
        }

        float drive_delta[kPhysVarCount];
        for (size_t v = 0; v < kPhysVarCount; v++) drive_delta[v] = prev_drive_[v] - drive_now[v];

        insert_or_update(signature, drive_delta);
    }

    memcpy(prev_drive_, drive_now, sizeof(drive_now));
    has_prev_ = true;
}

bool SpatialMemory::best_cell(const Physiology& phys, uint32_t& out_signature, float& out_score) const {
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
