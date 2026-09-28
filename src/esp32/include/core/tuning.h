#pragma once
//
// Every tunable rate/gain/threshold in the behavior engine, in one place, so
// changing "how curious" or "how fast energy drains" doesn't require hunting
// through five files. Together these are the organism's constitution: its
// comfort bands and temperament. g_tuning is mutable so a constitution can
// be set at runtime (the simulator gives each organism its own; later the
// dashboard's Config tab) without recompiling. Structural constants (table sizes, bit widths, probe
// bounds) stay next to the code they size instead — those aren't things
// you'd tune, only things you'd resize if you changed the underlying data
// layout.
//
// All rates are "per second" so behavior is independent of tick rate.

struct Tuning {
    // Comfortable band per physiology variable, in PhysVar order: energy,
    // fatigue, safety, arousal, curiosity, boredom. Outside its band a
    // variable becomes a drive. Curiosity's band is unused (its drive is its
    // value). Energy's lower edge is where hunger starts: 0.65 is about
    // half charge on a LiPo read across its full voltage span.
    struct {
        float lo[6] = {0.65f, 0.0f, 0.7f, 0.0f, 0.0f, 0.0f};
        float hi[6] = {1.0f, 0.6f, 1.0f, 0.7f, 1.0f, 0.4f};
    } bands;

    // Exploration width (Physiology::fuzz_scale) from drives: curiosity,
    // boredom and hunger widen it (foraging), threat narrows it (freezing).
    struct {
        float curiosity = 0.3f;
        float boredom = 0.4f;
        float hunger = 0.3f;
        float threat = 0.3f;
        float min = 0.05f;
        float max = 0.8f;
    } exploration;

    struct {
        float adaptation_tau_s = 10.0f;     // how fast a channel's noise floor is learned
        float salience_k = 3.0f;            // change must exceed k x a channel's typical jitter
        float min_salient_change = 0.005f;  // absolute floor, for channels that are perfectly quiet
        float habituation_step = 0.2f;      // startle lost per salient event on a channel
        float habituation_tau_s = 60.0f;    // startle recovers over this
    } senses;

    struct {
        float idle_regen_rate = 0.01f;  // no energy sensor only: slow trickle, "idle metabolism"
        float drain_rate = 0.08f;       // no energy sensor only: scaled by summed actuator cost
        float battery_gain = 0.4f;      // pull rate toward energy_sensor reading
    } energy;

    struct {
        float gain = 0.012f;          // scaled by summed actuator cost; settles ~0.3 at cost 1, ~0.55 at cost 3
        float recovery_rate = 0.028f;
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
        float gain = 0.05f;              // rise toward 1 while nothing out of the ordinary happens
        float reset_gain = 0.5f;         // reset by change relative to the usual
        float habituation_tau_s = 120.0f;  // what "usual" means
    } boredom;

    struct {
        float noise_gain = 0.5f;         // fraction of channel span, at fuzz=1
        float noise_tau_s = 1.0f;        // exploration noise correlation time: smooth wandering, not jitter
        float contingency_gain = 0.3f;   // fraction of span, at confidence=1
        float spatial_gain = 0.5f;       // scales best_cell() score into an explore/exploit nudge
        float spatial_nudge_max = 0.3f;  // clamp so it can widen/narrow noise, never dominate it
        float rest_pull_gain = 0.3f;     // how much of the proposed action is shrunk toward 0, at max pressure
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
        float learn_rate = 0.002f;          // per tick, per unit of normalized advantage and trace
        float trace_tau_s = 3.0f;           // how far back a drive drop reaches to credit actions
        float input_mean_tau_s = 300.0f;    // input centring: "brighter than usual", not "bright"
        float reward_baseline_tau_s = 30.0f;
        float reward_sd_floor = 1e-4f;      // don't amplify pure float noise into "reward"
        float advantage_clip = 5.0f;
        float weight_max = 1.5f;            // a reflex can bias an actuator, never pin it beyond exploration's reach
        float weight_decay_per_s = 2e-4f;   // unrewarded habits fade over ~1.5 h
    } policy;

    struct {
        float thermal_limit = 20.0f;          // cost-seconds before a channel is force-cut
        float thermal_cooldown_rate = 2.0f;   // cost-seconds recovered per second
        float critical_battery_level = 0.05f;  // raw energy_sensor level that force-zeros everything
    } safety_monitor;
};

inline Tuning g_tuning{};
