#pragma once
//
// Pure bit-packing math behind the 2-bits-per-channel codes ContingencyMemory
// and SpatialMemory use (up to 16 channels in a uint32_t). Zero dependency
// on Body/Actuator/Sensor/Arduino — factored out specifically so it's
// testable on the host (see test/native/test_channel_code.cpp) without
// dragging in the hardware layer.
//
// Two symbol alphabets share this packing:
//   - "delta" (ContingencyMemory): 0=no change, 1=rose, 2=fell.
//   - "level" (SpatialMemory): 0-3, which quartile of [0,1] a reading fell in.

#include <cstddef>
#include <cstdint>

constexpr size_t kChannelCodeMaxChannels = 16;

// --- delta symbols (ContingencyMemory) -------------------------------------

inline int8_t decode_channel_delta(uint32_t code, size_t channel_index) {
    if (channel_index >= kChannelCodeMaxChannels) return 0;
    uint32_t sym = (code >> (2 * channel_index)) & 0x3u;
    if (sym == 1) return 1;
    if (sym == 2) return -1;
    return 0;
}

inline uint32_t encode_channel_delta(uint32_t code, size_t channel_index, int8_t delta) {
    if (channel_index >= kChannelCodeMaxChannels) return code;
    uint32_t sym = delta > 0 ? 1u : (delta < 0 ? 2u : 0u);
    uint32_t shift = 2 * channel_index;
    code &= ~(0x3u << shift);
    return code | (sym << shift);
}

// --- level symbols (SpatialMemory) -----------------------------------------

inline uint32_t decode_channel_level(uint32_t code, size_t channel_index) {
    if (channel_index >= kChannelCodeMaxChannels) return 0;
    return (code >> (2 * channel_index)) & 0x3u;
}

inline uint32_t encode_channel_level(uint32_t code, size_t channel_index, uint32_t level) {
    if (channel_index >= kChannelCodeMaxChannels) return code;
    uint32_t shift = 2 * channel_index;
    code &= ~(0x3u << shift);
    return code | ((level & 0x3u) << shift);
}

// --- shared -----------------------------------------------------------------

// Number of 2-bit channel symbols that differ between two codes, across all
// 16 possible slots. Channels beyond a body's actual count are 0 in both
// codes by construction, so including them unconditionally is harmless.
inline size_t channel_code_distance(uint32_t a, uint32_t b) {
    size_t dist = 0;
    for (size_t i = 0; i < kChannelCodeMaxChannels; i++) {
        if (((a >> (2 * i)) & 0x3u) != ((b >> (2 * i)) & 0x3u)) dist++;
    }
    return dist;
}
