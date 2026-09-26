#pragma once
//
// Every tunable rate/gain/threshold in the behavior engine, in one place, so
// changing "how curious" or "how fast energy drains" doesn't require hunting
// through five files. Structural constants (table sizes, bit widths, probe
// bounds) stay next to the code they size instead — those aren't things
// you'd tune, only things you'd resize if you changed the underlying data
// layout.
//
// All rates are "per second" so behavior is independent of tick rate.

struct Tuning {
    struct {
        float idle_regen_rate = 0.01f;  // slow trickle, "idle metabolism"
        float drain_rate = 0.08f;       // scaled by summed actuator cost
        float battery_gain = 0.4f;      // pull rate toward energy_sensor reading
    } energy;

    struct {
        float gain = 0.12f;  // scaled by summed actuator cost
        float recovery_rate = 0.03f;
    } fatigue;

    struct {
        float drop_gain = 1.2f;  // scaled by single-tick sensor spike
        float recovery_rate = 0.05f;
    } safety;

    struct {
        float gain = 1.5f;
        float decay = 0.3f;
    } arousal;

    struct {
        float gain = 1.0f;
        float decay = 0.25f;
    } curiosity;

    struct {
        float gain = 0.15f;
        float reset_gain = 2.0f;
    } boredom;

    struct {
        float noise_gain = 0.5f;         // fraction of channel span, at fuzz=1
        float contingency_gain = 0.3f;   // fraction of span, at confidence=1
        float spatial_gain = 0.5f;       // scales best_cell() score into an explore/exploit nudge
        float spatial_nudge_max = 0.3f;  // clamp so it can widen/narrow noise, never dominate it
        float rest_pull_gain = 0.3f;     // fraction of the way toward 0 per tick, at max pressure
    } action;

    struct {
        float learn_rate = 0.1f;
        float decay_rate = 0.005f;
        float prune_threshold = 0.02f;  // below this, a decayed entry is reclaimed
        float deadzone = 0.02f;         // |delta| below this counts as "no change"
    } contingency;

    struct {
        float learn_rate = 0.15f;
    } spatial;

    struct {
        float thermal_limit = 20.0f;          // cost-seconds before a channel is force-cut
        float thermal_cooldown_rate = 2.0f;   // cost-seconds recovered per second
        float critical_battery_level = 0.05f;  // raw energy_sensor level that force-zeros everything
    } safety_monitor;
};

constexpr Tuning kTuning{};
