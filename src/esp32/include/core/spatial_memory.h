#pragma once
//
// Spatial memory (docs/synth-behavior.md §8).
//
// This hardware has no positioning sensor — no encoders, no IMU, no GPS —
// and the article's own pseudocode treats "position" as a given without
// saying where it comes from. Rather than fabricate open-loop odometry from
// commanded motor duty cycle (which would drift into nonsense within
// seconds, since duty cycle isn't measured displacement), this uses the
// article's explicitly offered alternative: "a coarse 2-D grid OR a set of
// place cells" (§8.1). A place cell here is identified by a coarse signature
// of the current sensor readings — same ambient light/RF/whatever reads the
// same, treated as the same place. That's the same gradient-following idea
// §10 describes for light and RF, just made the identity of a "location"
// instead of a single channel's bias.
//
// Consequence: without real coordinates there's no direction to steer in,
// so best_cell() returns *which remembered place looks most promising*, not
// a heading. Turning that into actual motor bias needs either a real
// odometry source or a policy that free-explores toward matching sensory
// conditions — deferred to v0.7.0's core loop, honestly, rather than faked
// here.

#include <cstddef>
#include <cstdint>

#include "body/body.h"
#include "core/physiology.h"

struct SpatialCell {
    uint32_t signature = 0;   // coarse quantized sensor snapshot identifying this "place"
    uint32_t visit_count = 0;
    uint16_t age = 0;         // ticks since last visited
    float drive_improvement[kPhysVarCount] = {};  // EMA of (prev_drive - drive) while here, per variable
    bool occupied = false;
};

class SpatialMemory {
public:
    static constexpr size_t kCapacity = 32;
    static constexpr size_t kMaxProbe = 8;

    void begin(Body& body);

    // Advance one behavior tick: computes this tick's place signature,
    // ages every cell, and reinforces/inserts the record for the place
    // we're in now with how drives changed since last tick.
    void update(Body& body, Physiology& phys, float dt_s);

    size_t count() const { return count_; }
    static constexpr size_t capacity() { return kCapacity; }

    // Coarse sensor-based place identity. Exposed for dashboard/tests.
    static uint32_t compute_signature(Body& body);

    // The occupied cell with the highest current-drive-weighted expected
    // improvement (docs/synth-behavior.md §8.2 best_cell_for), i.e. "which
    // known place has historically helped most with what's pressing right
    // now". Returns false if memory is empty.
    bool best_cell(const Physiology& phys, uint32_t& out_signature, float& out_score) const;

    // Raw table access for the dashboard's fixed-grid view — every slot,
    // occupied or not, so the grid layout is stable across calls.
    const SpatialCell& cell_at(size_t i) const { return table_[i]; }

    // Signature encoding helpers (2 bits/channel, quantized into 4 levels),
    // exposed for life-state serialize/restore (docs/synth-behavior.md §13)
    // where signatures are saved by sensor *name* and re-packed into this
    // body's bit positions on load.
    static uint32_t decode_level(uint32_t signature, size_t channel_index);
    static uint32_t encode_level(uint32_t signature, size_t channel_index, uint32_t level);

    void clear();
    void restore_raw(uint32_t signature, uint32_t visit_count, uint16_t age,
                      const float* drive_improvement);

private:
    uint32_t insert_or_update(uint32_t signature, const float* drive_delta);

    SpatialCell table_[kCapacity];
    size_t count_ = 0;
    bool has_prev_ = false;
    float prev_drive_[kPhysVarCount] = {};
};
