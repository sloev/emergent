#pragma once
//
// Action generator (docs/synth-behavior.md §9): the piece that was missing
// from every release through v0.6.0. Everything before this only observed —
// physiology tracked internal state, contingency and spatial memory learned
// from whatever moved the actuators (dashboard sliders, the heartbeat
// blink). This is the first thing that writes to actuators on its own.
//
// Each tick, for every actuator channel that hasn't been manually driven
// recently (Actuator::manual_override_active), it proposes:
//
//   next = clamp(baseline + noise + contingency_bias + spatial_nudge)
//
// - baseline: the channel's current value (persistence — "keep doing what
//   you were doing" is the simplest stand-in for the article's
//   generate_baseline_action()).
// - noise: uniform random, scaled by Physiology::fuzz_scale() and the
//   channel's own range, per §6.2's calculate_fuzz_scale.
// - contingency_bias: decoded from ContingencyMemory::query_bias() against
//   the sensor-delta pattern just observed, scaled by its confidence.
// - spatial_nudge: NOT a direction — this hardware has no positioning
//   sensor, so SpatialMemory::best_cell() can only say *which* remembered
//   place looks promising, not *which way*. Documented honestly in
//   docs/roadmap.md: this uses it to modulate explore-vs-exploit instead of
//   steering — noise widens when a better place than "here" is known to
//   exist, narrows when the current place already looks like the best one
//   remembered.

#include "body/body.h"
#include "core/contingency_memory.h"
#include "core/physiology.h"
#include "core/spatial_memory.h"

class ActionGenerator {
public:
    void begin(Body& body);
    void tick(Body& body, Physiology& phys, ContingencyMemory& contingency, SpatialMemory& spatial);
};
