#pragma once
//
// Pure decay/aging math factored out of ContingencyMemory/SpatialMemory —
// zero dependency on Body/Arduino, testable on the host (see
// test/native/test_decay_math.cpp).

#include <cstdint>

// Saturates instead of wrapping. A uint16_t that just does age++ silently
// resets to 0 (looking "just visited") after 65536 ticks — ~55 minutes at
// this project's 20 Hz behavior tick.
inline uint16_t bump_age_saturating(uint16_t age) {
    return age < UINT16_MAX ? static_cast<uint16_t>(age + 1) : age;
}

inline float decay_strength(float strength, float decay_rate) {
    return strength * (1.0f - decay_rate);
}

// True once a decayed entry is weak enough to reclaim rather than leave
// occupying a slot as denormal noise forever.
inline bool should_prune(float strength, float prune_threshold) {
    return strength < prune_threshold;
}
