// Emergent simulator: runs the unmodified behavior engine (physiology,
// contingency memory, spatial memory, learned reflexes, action generator)
// in a 2D room with a charging station, in one of many bodies, and scores
// what it does.
//
//   ./emergent-sim --runs 8 --lives 6 --hours 3              # organism 0, every condition
//   ./emergent-sim --organism 12 --json --runs 4 --lives 4   # one zoo organism, machine-readable
//   ./emergent-sim --organism 12 --trace out.json --seed 3   # 1 Hz trace of every channel
//   ./emergent-sim --organism 12 --describe                  # what organism 12 is made of
//
// Conditions are ablations of the same organism plus a null model with no
// engine at all, so every number has something to be compared to.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "body_plan.h"
#include "core/action_generator.h"
#include "core/contingency_memory.h"
#include "core/physiology.h"
#include "core/spatial_memory.h"
#include "life/organism.h"
#include "sim_body.h"
#include "world.h"

namespace {

using namespace sim;

enum class Condition {
    kFull, kHandWired, kHandWiredPlastic, kNoLearning, kNoContingency, kNoSpatial, kNoBatteryLink, kNoiseOnly, kRandomWalk
};

struct ConditionInfo {
    Condition c;
    const char* name;
    const char* what;
    bool example_only;
};

const ConditionInfo kConditions[] = {
    {Condition::kFull, "full", "unmodified engine", false},
    {Condition::kHandWired, "hand_wired", "reflexes fixed by hand to phototaxis, not learned (upper bound)", true},
    {Condition::kHandWiredPlastic, "hand_wired_plastic", "starts hand-wired, then learns (does learning keep it?)", true},
    {Condition::kNoLearning, "no_reflexes", "learned sensorimotor reflexes off", false},
    {Condition::kNoContingency, "no_contingency", "contingency bias never fires", false},
    {Condition::kNoSpatial, "no_spatial", "spatial nudge never fires", false},
    {Condition::kNoBatteryLink, "no_battery_link", "battery not wired to h_energy", false},
    {Condition::kNoiseOnly, "noise_only", "no memory, no reflexes: drives + noise", false},
    {Condition::kRandomWalk, "random_walk", "null model: wanders with its own locomotion, no engine", false},
};

struct Metrics {
    float lifespan_h = 0;
    bool died = false;
    int dock_events = 0;
    int hungry_dockings = 0;
    int patch_switches = 0;  // meals taken at a different station than the last one
    float docked_frac = 0;
    float full_docked_frac = 0;  // of the time docked, how much at >95% charge (charging forever)
    int places = 0;              // life engine: places it has remembered
    float charge_ah = 0;
    float coverage = 0;
    float mean_speed = 0;
    float dist_when_hungry = 0;
    float dist_when_sated = 0;
    float behavior_entropy = 0;
    float var_mean[kPhysVarCount] = {};
    float drive_on[kPhysVarCount] = {};
    float mean_fuzz = 0;
};

constexpr float kDt = 0.05f;

life::Organism::Traits g_traits;  // --off; set before any thread starts
constexpr int kCellsX = 30, kCellsY = 25;  // 10 cm grid

// What evolution works on: the constitution, the brain's wiring (its seed),
// and the innate readout weights an individual is born with. Learning then
// starts from these instead of from zero. No human writes the weights;
// selection finds them.
struct EvoGenome {
    life::Genome g;
    std::vector<float> innate;  // n_out x feature_count, row-major; empty = born blank
    size_t features = 0;        // feature_count() the innate weights were evolved for
};

bool save_genome(const char* path, const EvoGenome& e, int organism, size_t n_out, size_t nf) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    const life::Genome& g = e.g;
    fprintf(f, "organism %d\nseed %u\nhunger_setpoint %g\nboredom_rate %g\npain_fade_s %g\nexplore %g\n"
               "explore_tau_s %g\nlearn_rate %g\nvalue_rate %g\nmodel_rate %g\nhorizon_s %g\ntrace_s %g\n"
               "brain_gain %g\nenergy_buffer_s %g\nsatiety_setpoint %g\nplace_radius %g\nn_out %zu\nfeatures %zu\ninnate",
            organism, g.seed, g.hunger_setpoint, g.boredom_rate, g.pain_fade_s, g.explore, g.explore_tau_s, g.learn_rate,
            g.value_rate, g.model_rate, g.horizon_s, g.trace_s, g.brain_gain, g.energy_buffer_s, g.satiety_setpoint,
            g.place_radius, n_out, nf);
    for (float w : e.innate) fprintf(f, " %.5g", w);
    fprintf(f, "\n");
    fclose(f);
    return true;
}

bool load_genome(const char* path, EvoGenome& e) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char key[64];
    size_t n_out = 0, nf = 0;
    life::Genome& g = e.g;
    while (fscanf(f, "%63s", key) == 1) {
        std::string k = key;
        if (k == "innate") {
            e.features = nf;
            e.innate.assign(n_out * nf, 0.0f);
            for (auto& w : e.innate)
                if (fscanf(f, "%f", &w) != 1) break;
            break;
        }
        double v = 0;
        if (fscanf(f, "%lf", &v) != 1) break;
        if (k == "seed") g.seed = static_cast<uint32_t>(v);
        else if (k == "hunger_setpoint") g.hunger_setpoint = static_cast<float>(v);
        else if (k == "boredom_rate") g.boredom_rate = static_cast<float>(v);
        else if (k == "pain_fade_s") g.pain_fade_s = static_cast<float>(v);
        else if (k == "explore") g.explore = static_cast<float>(v);
        else if (k == "explore_tau_s") g.explore_tau_s = static_cast<float>(v);
        else if (k == "learn_rate") g.learn_rate = static_cast<float>(v);
        else if (k == "value_rate") g.value_rate = static_cast<float>(v);
        else if (k == "model_rate") g.model_rate = static_cast<float>(v);
        else if (k == "horizon_s") g.horizon_s = static_cast<float>(v);
        else if (k == "trace_s") g.trace_s = static_cast<float>(v);
        else if (k == "brain_gain") g.brain_gain = static_cast<float>(v);
        else if (k == "energy_buffer_s") g.energy_buffer_s = static_cast<float>(v);
        else if (k == "satiety_setpoint") g.satiety_setpoint = static_cast<float>(v);
        else if (k == "place_radius") g.place_radius = static_cast<float>(v);
        else if (k == "n_out") n_out = static_cast<size_t>(v);
        else if (k == "features") nf = static_cast<size_t>(v);
    }
    fclose(f);
    return true;
}

// One organism across one or more lives. A life ends when the battery is
// flat away from the dock. The next life is what an experimenter would do
// with the real robot: recharge it, put it down somewhere random, power it
// on. Memories and reflexes persist (the firmware saves life-state);
// physiology restarts from its boot values.
struct Runner {
    Runner(Condition c, const BodyPlan& p, const WorldConfig& w, float h, int n, bool life_engine)
        : cond(c), plan(p), wcfg(w), hours(h), lives(n), use_life(life_engine), world(wcfg, plan), body(plan),
          rng(w.seed + 99) {}

    Condition cond;
    BodyPlan plan;
    WorldConfig wcfg;
    float hours;
    int lives;
    bool use_life;
    World world;
    SimBody body;
    Physiology phys;
    ContingencyMemory cm, cm_empty;
    SpatialMemory spatial, spatial_empty;
    ActionGenerator gen;
    life::Organism org;
    float in_buf[life::Organism::kMaxIn] = {};
    float out_buf[life::Organism::kMaxOut] = {};
    std::mt19937 rng;
    const EvoGenome* evo = nullptr;  // life engine: born from this genome instead of defaults
    FILE* trace = nullptr;
    bool trace_first = true;
    long trace_every = 20;  // ticks between trace rows

    std::vector<Metrics> run_all() {
        // g_tuning is the classic engine's global constitution; the life
        // engine carries everything in its Genome, so evaluations can run on
        // several threads at once without touching shared state.
        if (!use_life) g_tuning = plan.tuning;
        cm.begin(body);
        cm_empty.begin(body);
        gen.set_seed(wcfg.seed * 2654435761u + 1);
        gen.begin_learning(body);
        gen.set_learning(cond != Condition::kNoLearning && cond != Condition::kNoiseOnly);
        if (cond == Condition::kHandWired || cond == Condition::kHandWiredPlastic) hand_wire();
        if (cond == Condition::kHandWired) gen.set_plasticity(false);
        if (use_life) begin_life_engine();
        std::vector<Metrics> out;
        for (int life = 0; life < lives; life++) {
            if (life > 0) world.reset_robot();
            out.push_back(run_life(life));
        }
        return out;
    }

    // The new engine: one Organism, told only which input is its energy and
    // which inputs hurt (bumpers).
    void begin_life_engine() {
        life::Genome g;
        g.seed = wcfg.seed * 2654435761u + static_cast<uint32_t>(plan.id) * 97u + 1u;
        g.hunger_setpoint = plan.tuning.bands.lo[0];
        uint32_t pain = 0;
        bool bip[life::Organism::kMaxOut] = {};
        for (size_t i = 0; i < plan.sensors.size() && i < 32; i++)
            if (plan.sensors[i].type == Sense::kBump) pain |= 1u << i;
        for (size_t j = 0; j < plan.actuators.size() && j < life::Organism::kMaxOut; j++) bip[j] = plan.actuators[j].min < 0;
        int energy = plan.battery_linked ? body.energy_index() : -1;
        if (evo) g = evo->g;
        org.begin(g, body.sensor_count(), body.actuator_count(), energy, pain, bip);
        org.set_traits(g_traits);
        if (evo && !evo->innate.empty() && evo->features && evo->features != org.feature_count()) {
            fprintf(stderr, "genome was evolved for %zu features, this organism has %zu: re-evolve it\n", evo->features,
                    org.feature_count());
            exit(1);
        }
        if (evo && !evo->innate.empty() && cond != Condition::kNoiseOnly) {
            size_t nf = org.feature_count();
            for (size_t j = 0; j < org.output_count(); j++)
                for (size_t k = 0; k < nf && j * nf + k < evo->innate.size(); k++) org.set_innate(j, k, evo->innate[j * nf + k]);
        }
        org.set_plasticity(cond != Condition::kNoLearning && cond != Condition::kNoiseOnly);
    }

    // Braitenberg 2b in the hunger context of the example body: each head
    // light excites the opposite wheel; the station's clicks brake.
    void hand_wire() {
        SensorimotorPolicy& p = gen.policy();
        size_t e = 1 + static_cast<size_t>(PhysVar::kEnergy);
        size_t bias = p.input_count();
        auto s = [&](const char* n) {
            for (size_t i = 0; i < plan.sensors.size(); i++)
                if (plan.sensors[i].name == n) return i;
            return static_cast<size_t>(0);
        };
        size_t L = static_cast<size_t>(plan.actuator_index(Role::kLocoLeft));
        size_t R = static_cast<size_t>(plan.actuator_index(Role::kLocoRight));
        p.set_weight(e, s("light_left"), R, 1.5f);
        p.set_weight(e, s("light_right"), L, 1.5f);
        p.set_weight(e, s("light_left"), L, -0.8f);
        p.set_weight(e, s("light_right"), R, -0.8f);
        p.set_weight(e, bias, L, 0.2f);
        p.set_weight(e, bias, R, 0.2f);
        p.set_weight(e, s("mic"), L, -1.5f);
        p.set_weight(e, s("mic"), R, -1.5f);
    }

    // Null model: move with this body's own locomotion, no engine at all.
    float rw_timer = 0, rw_a = 0, rw_b = 0, rw_phase = 0;
    void random_walk_step() {
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        rw_timer -= kDt;
        rw_phase += kDt * 2.0f * kPi;
        if (rw_timer <= 0 || world.bumped()) {
            rw_timer = 1.0f + 3.0f * std::fabs(u(rng));
            rw_a = u(rng);
            rw_b = u(rng);
        }
        auto set = [&](Role r, float v) {
            int i = plan.actuator_index(r);
            if (i >= 0) body.actuator_at(static_cast<size_t>(i)).write(v);
        };
        switch (plan.loco) {
            case Loco::kDiffStepper:
            case Loco::kDiffDc:
            case Loco::kWheelServo:
                set(Role::kLocoLeft, 0.5f - 0.4f * rw_a);
                set(Role::kLocoRight, 0.5f + 0.4f * rw_a);
                break;
            case Loco::kTricycle:
                set(Role::kDrive, 0.5f);
                set(Role::kSteer, rw_a);
                break;
            case Loco::kVibro2:
                set(Role::kLocoLeft, 0.6f + 0.4f * rw_a);
                set(Role::kLocoRight, 0.6f + 0.4f * rw_b);
                break;
            case Loco::kVibro1:
                set(Role::kVibro, 0.7f);
                break;
            case Loco::kCrawler:
                set(Role::kLocoLeft, 0.8f * std::sin(rw_phase));
                set(Role::kLocoRight, 0.8f * std::sin(rw_phase + 0.6f * rw_a));
                break;
        }
    }

    Metrics run_life(int life) {
        phys.begin(body);
        if (use_life) org.reset_state();
        Metrics m;
        bool visited[kCellsX][kCellsY] = {};
        double speed_sum = 0, hungry_d = 0, sated_d = 0;
        long hungry_n = 0, sated_n = 0, docked_ticks = 0, full_docked_ticks = 0;
        bool was_charging = false;
        int last_patch = -1;
        float prev_soc = world.soc();
        int window_hist[9] = {};
        int window_n = 0;
        double ent_sum = 0;
        int ent_n = 0;
        int energy = body.energy_index();
        int la = plan.actuator_index(Role::kLocoLeft) >= 0 ? plan.actuator_index(Role::kLocoLeft)
                 : plan.actuator_index(Role::kDrive) >= 0 ? plan.actuator_index(Role::kDrive)
                                                           : plan.actuator_index(Role::kVibro);
        int ra = plan.actuator_index(Role::kLocoRight) >= 0 ? plan.actuator_index(Role::kLocoRight)
                                                             : plan.actuator_index(Role::kSteer);

        long total_ticks = static_cast<long>(hours * 3600.0f / kDt);
        long tick = 0;
        uint32_t now_ms = 0;

        for (; tick < total_ticks; tick++) {
            now_ms += 50;
            for (size_t i = 0; i < body.sensor_count(); i++) body.sensor_at(i).set(world.sense(i));

            if (cond == Condition::kRandomWalk) {
                random_walk_step();
                phys.update(body, kDt, 0.0f);
            } else if (use_life) {
                for (size_t i = 0; i < body.sensor_count(); i++) in_buf[i] = body.sensor_at(i).last_value();
                org.tick(in_buf, out_buf, kDt);
                for (size_t j = 0; j < body.actuator_count(); j++) body.actuator_at(j).write(out_buf[j]);
                phys.update(body, kDt, 0.0f);  // bookkeeping only: nothing reads it back
            } else {
                ContingencyMemory& c_act =
                    (cond == Condition::kNoContingency || cond == Condition::kNoiseOnly) ? cm_empty : cm;
                SpatialMemory& s_act =
                    (cond == Condition::kNoSpatial || cond == Condition::kNoiseOnly) ? spatial_empty : spatial;
                phys.update(body, kDt, cm.last_surprise());
                cm.update(body, phys, kDt);
                spatial.update(body, phys, kDt);
                gen.tick(body, phys, c_act, s_act, now_ms);
            }

            // Firmware SafetyMonitor's battery floor.
            if (energy >= 0 && body.sensor_at(static_cast<size_t>(energy)).last_value() < 0.05f) {
                for (size_t i = 0; i < body.actuator_count(); i++) body.actuator_at(i).force(0.0f);
            }

            world.step(body.values(), kDt);

            // --- metrics ---------------------------------------------------
            const Pose& p = world.pose();
            int cx = std::min(kCellsX - 1, std::max(0, static_cast<int>(p.x / 0.1f)));
            int cy = std::min(kCellsY - 1, std::max(0, static_cast<int>(p.y / 0.1f)));
            visited[cx][cy] = true;
            speed_sum += world.speed();
            if (world.docked()) {
                docked_ticks++;
                if (world.soc() > 0.95f) full_docked_ticks++;
            }
            bool hungry = use_life ? org.hunger() > 0.0f : phys.drive(PhysVar::kEnergy) > 0.0f;
            if (world.charging() && !was_charging) {
                m.dock_events++;
                if (hungry) m.hungry_dockings++;
                if (last_patch >= 0 && world.docked_at() != last_patch) m.patch_switches++;
                last_patch = world.docked_at();
            }
            was_charging = world.charging();
            if (world.soc() > prev_soc) m.charge_ah += (world.soc() - prev_soc) * plan.capacity_ah;
            prev_soc = world.soc();
            float d = world.distance_to_station();
            if (hungry) {
                hungry_d += d;
                hungry_n++;
            } else {
                sated_d += d;
                sated_n++;
            }
            for (size_t v = 0; v < kPhysVarCount; v++) {
                m.var_mean[v] += phys.value(static_cast<PhysVar>(v));
                if (phys.drive(static_cast<PhysVar>(v)) > 0.0f) m.drive_on[v] += 1;
            }
            m.mean_fuzz += phys.fuzz_scale();

            // Behavior entropy over its two main locomotion channels, 60 s windows.
            auto sgn = [&](int idx) {
                if (idx < 0) return 1;
                float v = body.actuator_at(static_cast<size_t>(idx)).value();
                return v > 0.1f ? 2 : (v < -0.1f ? 0 : 1);
            };
            window_hist[sgn(la) * 3 + sgn(ra)]++;
            if (++window_n == static_cast<int>(60.0f / kDt)) {
                float h = 0;
                for (int k = 0; k < 9; k++) {
                    if (!window_hist[k]) continue;
                    float q = static_cast<float>(window_hist[k]) / window_n;
                    h -= q * std::log2(q);
                }
                ent_sum += h;
                ent_n++;
                memset(window_hist, 0, sizeof(window_hist));
                window_n = 0;
            }

            if (trace && tick % trace_every == 0) write_trace_row(life);

            if (world.soc() <= 0.0f && !world.docked()) {
                m.died = true;
                tick++;
                break;
            }
        }

        float ticks = static_cast<float>(tick);
        m.lifespan_h = ticks * kDt / 3600.0f;
        m.docked_frac = docked_ticks / ticks;
        m.full_docked_frac = docked_ticks ? static_cast<float>(full_docked_ticks) / docked_ticks : NAN;
        m.places = use_life ? static_cast<int>(org.place_count()) : 0;
        int cells = 0;
        for (auto& row : visited)
            for (bool v : row) cells += v;
        m.coverage = static_cast<float>(cells) / (kCellsX * kCellsY);
        m.mean_speed = static_cast<float>(speed_sum / ticks);
        m.dist_when_hungry = hungry_n ? static_cast<float>(hungry_d / hungry_n) : NAN;
        m.dist_when_sated = sated_n ? static_cast<float>(sated_d / sated_n) : NAN;
        m.behavior_entropy = ent_n ? static_cast<float>(ent_sum / ent_n) : NAN;
        for (size_t v = 0; v < kPhysVarCount; v++) {
            m.var_mean[v] /= ticks;
            m.drive_on[v] /= ticks;
        }
        m.mean_fuzz /= ticks;
        return m;
    }

    void write_trace_row(int life) {
        const Pose& p = world.pose();
        fprintf(trace, "%s[%d,%.0f,%.3f,%.3f,%.3f,%.3f,%d,%.3f", trace_first ? "" : ",\n", life, world.time(), p.x, p.y,
                p.theta, world.soc(), world.docked() ? 1 : 0, world.head_angle());
        trace_first = false;
        for (size_t k = 0; k < world.station_count(); k++) fprintf(trace, ",%.4f", world.station_buffer(k));
        if (use_life) {
            fprintf(trace, ",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f", org.hunger(), org.pain(), org.boredom(),
                    org.dopamine(), org.adrenaline(), org.cortisol(), org.serotonin(), org.value(), org.exploration());
        } else {
            for (size_t v = 0; v < kPhysVarCount; v++) fprintf(trace, ",%.3f", phys.value(static_cast<PhysVar>(v)));
            fprintf(trace, ",%.3f", phys.fuzz_scale());
        }
        for (size_t i = 0; i < body.sensor_count(); i++) fprintf(trace, ",%.3f", body.sensor_at(i).last_value());
        for (size_t i = 0; i < body.actuator_count(); i++) fprintf(trace, ",%.3f", body.actuator_at(i).value());
        fprintf(trace, "]");
    }
};

// --- JSON helpers ----------------------------------------------------------

std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

std::string jnum(double v) {
    if (std::isnan(v)) return "null";
    char b[32];
    snprintf(b, sizeof(b), "%.4g", v);
    return b;
}

std::string plan_json(const BodyPlan& p) {
    std::string s = "{\"id\":" + std::to_string(p.id) + ",\"title\":" + jstr(p.title) +
                    ",\"loco\":" + jstr(loco_short(p.loco)) + ",\"loco_desc\":" + jstr(loco_name(p.loco)) +
                    ",\"temperament\":" + jstr(p.temperament) + ",\"radius\":" + jnum(p.radius) +
                    ",\"max_speed\":" + jnum(p.max_speed) + ",\"cells\":" + std::to_string(p.cells) +
                    ",\"capacity_ah\":" + jnum(p.capacity_ah) + ",\"base_current_a\":" + jnum(p.base_current_a) +
                    ",\"sensors\":[";
    for (size_t i = 0; i < p.sensors.size(); i++) {
        const SensorDef& d = p.sensors[i];
        s += std::string(i ? "," : "") + "{\"name\":" + jstr(d.name) + ",\"type\":" + jstr(sense_name(d.type)) +
             ",\"part\":" + jstr(d.part) + ",\"angle\":" + jnum(d.angle) + ",\"on_head\":" + (d.on_head ? "true" : "false") +
             (d.type == Sense::kRssi ? ",\"scan_period\":" + jnum(d.scan_period) : std::string()) + "}";
    }
    s += "],\"actuators\":[";
    for (size_t i = 0; i < p.actuators.size(); i++) {
        const ActuatorDef& d = p.actuators[i];
        s += std::string(i ? "," : "") + "{\"name\":" + jstr(d.name) + ",\"role\":" + jstr(role_name(d.role)) +
             ",\"part\":" + jstr(d.part) + ",\"min\":" + jnum(d.min) + ",\"max\":" + jnum(d.max) + "}";
    }
    const Tuning& t = p.tuning;
    s += "],\"tuning\":{\"hunger_below\":" + jnum(t.bands.lo[0]) + ",\"boredom_above\":" + jnum(t.bands.hi[5]) +
         ",\"boredom_gain\":" + jnum(t.boredom.gain) + ",\"startle_gain\":" + jnum(t.safety.drop_gain) +
         ",\"curiosity_weight\":" + jnum(t.exploration.curiosity) + ",\"hunger_weight\":" + jnum(t.exploration.hunger) +
         ",\"learn_rate\":" + jnum(t.policy.learn_rate) + ",\"trace_tau_s\":" + jnum(t.policy.trace_tau_s) +
         ",\"noise_tau_s\":" + jnum(t.action.noise_tau_s) + ",\"noise_gain\":" + jnum(t.action.noise_gain) + "}}";
    return s;
}

std::string metrics_json(const Metrics& m) {
    return "{\"lifespan_h\":" + jnum(m.lifespan_h) + ",\"died\":" + (m.died ? "true" : "false") +
           ",\"dockings\":" + std::to_string(m.dock_events) + ",\"hungry_dockings\":" + std::to_string(m.hungry_dockings) +
           ",\"docked\":" + jnum(m.docked_frac) + ",\"charge_ah\":" + jnum(m.charge_ah) + ",\"coverage\":" + jnum(m.coverage) +
           ",\"speed\":" + jnum(m.mean_speed) + ",\"dist_hungry\":" + jnum(m.dist_when_hungry) +
           ",\"dist_sated\":" + jnum(m.dist_when_sated) + ",\"entropy\":" + jnum(m.behavior_entropy) +
           ",\"patch_switches\":" + std::to_string(m.patch_switches) + ",\"full_docked\":" + jnum(m.full_docked_frac) +
           ",\"places\":" + std::to_string(m.places) + "}";
}

// The strongest learned connections: which sense drives which output, in
// which motivational context.
std::string life_reflexes_json(Runner& r, size_t n) {
    struct W {
        float w;
        size_t j, k;
    };
    std::vector<W> all;
    const life::Organism& o = r.org;
    for (size_t j = 0; j < o.output_count(); j++)
        for (size_t k = 0; k < o.feature_count(); k++) all.push_back({o.weight(j, k), j, k});
    std::sort(all.begin(), all.end(), [](const W& a, const W& b) { return std::fabs(a.w) > std::fabs(b.w); });
    std::string s = "[";
    size_t ni = o.input_count();
    for (size_t q = 0; q < std::min(n, all.size()); q++) {
        const W& w = all[q];
        std::string from = w.k < ni ? r.plan.sensors[w.k].name
                           : w.k < ni + life::Organism::kInner ? life::Organism::inner_name(w.k - ni)
                           : w.k < ni + life::Organism::kInner + life::Organism::kNeurons
                               ? "neuron " + std::to_string(w.k - ni - life::Organism::kInner)
                           : w.k < ni + life::Organism::kInner + life::Organism::kNeurons + life::Organism::kPlaces
                               ? "place " + std::to_string(w.k - ni - life::Organism::kInner - life::Organism::kNeurons)
                               : std::string("(bias)");
        s += std::string(q ? "," : "") + "{\"context\":\"-\",\"from\":" + jstr(from) +
             ",\"to\":" + jstr(r.plan.actuators[w.j].name) + ",\"w\":" + jnum(w.w) + "}";
    }
    return s + "]";
}

std::string reflexes_json(Runner& r, size_t n) {
    if (r.use_life) return life_reflexes_json(r, n);
    struct W {
        float w;
        size_t k, i, j;
    };
    std::vector<W> all;
    const SensorimotorPolicy& p = r.gen.policy();
    for (size_t k = 0; k < SensorimotorPolicy::kContexts; k++)
        for (size_t i = 0; i <= p.input_count(); i++)
            for (size_t j = 0; j < p.output_count(); j++) all.push_back({p.weight(k, i, j), k, i, j});
    std::sort(all.begin(), all.end(), [](const W& a, const W& b) { return std::fabs(a.w) > std::fabs(b.w); });
    std::string s = "[";
    for (size_t q = 0; q < std::min(n, all.size()); q++) {
        const W& w = all[q];
        const char* ctx = w.k == 0 ? "always" : Physiology::var_name(static_cast<PhysVar>(w.k - 1));
        std::string in = w.i < p.input_count() ? r.plan.sensors[w.i].name : "(bias)";
        s += std::string(q ? "," : "") + "{\"context\":" + jstr(ctx) + ",\"from\":" + jstr(in) +
             ",\"to\":" + jstr(r.plan.actuators[w.j].name) + ",\"w\":" + jnum(w.w) + "}";
    }
    return s + "]";
}

struct Summary {
    double sum = 0, sum2 = 0;
    int n = 0;
    void add(double v) {
        if (std::isnan(v)) return;
        sum += v;
        sum2 += v * v;
        n++;
    }
    double mean() const { return n ? sum / n : NAN; }
    double sd() const { return n > 1 ? std::sqrt(std::max(0.0, (sum2 - sum * sum / n) / (n - 1))) : 0.0; }
};

// --- evolution ---------------------------------------------------------------

struct Evolver {
    BodyPlan plan;
    WorldConfig wcfg;
    float hours;
    int births;  // independent newborns per evaluation (different rooms/starts)
    int lives;   // consecutive lives per newborn: learning must stay good, not just start good
    size_t n_out = 0, nf = 0;
    std::mt19937 rng{12345};

    float normal(float sd) { return std::normal_distribution<float>(0.0f, sd)(rng); }
    float uni() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    EvoGenome random_genome() {
        EvoGenome e;
        e.g.seed = static_cast<uint32_t>(rng()) | 1u;
        e.innate.assign(n_out * nf, 0.0f);
        for (auto& w : e.innate) w = normal(0.1f);
        return e;
    }

    void mutate(EvoGenome& e) {
        for (auto& w : e.innate)
            if (uni() < 0.1f) w = std::max(-2.0f, std::min(2.0f, w + normal(0.2f)));
        auto scale = [&](float& v, float lo, float hi) {
            if (uni() < 0.3f) v = std::max(lo, std::min(hi, v * std::exp(normal(0.15f))));
        };
        life::Genome& g = e.g;
        scale(g.hunger_setpoint, 0.4f, 0.9f);
        scale(g.boredom_rate, 0.005f, 0.3f);
        scale(g.explore, 0.05f, 1.5f);
        scale(g.explore_tau_s, 0.2f, 5.0f);
        scale(g.learn_rate, 0.001f, 1.0f);
        scale(g.horizon_s, 2.0f, 200.0f);
        scale(g.trace_s, 0.3f, 20.0f);
        scale(g.brain_gain, 0.5f, 1.8f);
        scale(g.satiety_setpoint, 0.7f, 1.0f);
        scale(g.place_radius, 0.03f, 0.4f);
        if (uni() < 0.05f) g.seed = static_cast<uint32_t>(rng()) | 1u;  // rarely: a new brain
    }

    // Fitness: mean self-sufficiency (lifespan / idle endurance) of newborns.
    float evaluate(const EvoGenome& e, uint32_t gen_seed) {
        float idle = 0.8f * plan.capacity_ah / plan.base_current_a;
        float sum = 0.0f;
        for (int b = 0; b < births; b++) {
            WorldConfig w = wcfg;
            w.seed = gen_seed * 31u + static_cast<uint32_t>(b) * 7u + 1u;
            Runner r(Condition::kFull, plan, w, hours, lives, true);
            r.evo = &e;
            std::vector<Metrics> ms = r.run_all();
            for (const Metrics& m : ms) sum += m.lifespan_h / idle;
        }
        return sum / (births * lives);
    }

    void run(int generations, int pop, const char* out_path) {
        {
            Runner probe(Condition::kFull, plan, wcfg, hours, 1, true);
            probe.begin_life_engine();
            n_out = probe.org.output_count();
            nf = probe.org.feature_count();
        }
        std::vector<EvoGenome> P;
        for (int i = 0; i < pop; i++) P.push_back(random_genome());
        std::vector<float> fit(pop, 0.0f);
        unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        printf("evolving %s: %d generations x %d genomes, %d newborns x %d lives each, lives capped at %.1f h, "
               "%d station(s), %u threads\n",
               plan.title.c_str(), generations, pop, births, lives, hours, wcfg.stations, threads);
        printf("%4s %8s %8s %8s  %s\n", "gen", "best", "mean", "worst", "best genome");
        for (int gen = 0; gen < generations; gen++) {
            std::atomic<int> next{0};
            uint32_t gs = static_cast<uint32_t>(gen) + 1u;
            std::vector<std::thread> pool;
            for (unsigned t = 0; t < threads; t++)
                pool.emplace_back([&] {
                    for (int i; (i = next++) < pop;) fit[i] = evaluate(P[i], gs);
                });
            for (auto& th : pool) th.join();

            std::vector<int> order(pop);
            for (int i = 0; i < pop; i++) order[i] = i;
            std::sort(order.begin(), order.end(), [&](int a, int b) { return fit[a] > fit[b]; });
            double mean = 0;
            for (float f : fit) mean += f;
            mean /= pop;
            const life::Genome& bg = P[order[0]].g;
            printf("%4d %8.2f %8.2f %8.2f  explore %.2f learn %.3f hunger<%.2f bored %.3f horizon %.0fs trace %.1fs gain %.2f\n",
                   gen + 1, fit[order[0]], mean, fit[order[pop - 1]], bg.explore, bg.learn_rate, bg.hunger_setpoint,
                   bg.boredom_rate, bg.horizon_s, bg.trace_s, bg.brain_gain);
            fflush(stdout);
            if (out_path) save_genome(out_path, P[order[0]], plan.id, n_out, nf);

            // Next generation: keep the best quarter, fill the rest with
            // mutated children of tournament winners.
            std::vector<EvoGenome> next_gen;
            int elite = std::max(1, pop / 4);
            for (int i = 0; i < elite; i++) next_gen.push_back(P[order[i]]);
            while (static_cast<int>(next_gen.size()) < pop) {
                int a = static_cast<int>(uni() * pop), b = static_cast<int>(uni() * pop), c = static_cast<int>(uni() * pop);
                int w = fit[a] > fit[b] ? (fit[a] > fit[c] ? a : c) : (fit[b] > fit[c] ? b : c);
                EvoGenome child = P[w];
                mutate(child);
                next_gen.push_back(child);
            }
            P.swap(next_gen);
        }
    }
};

void usage() {
    fprintf(stderr,
            "usage: emergent-sim [--organism K] [--condition NAME|all] [--runs N] [--lives N] [--hours H]\n"
            "                    [--seed S] [--rssi-period S] [--json] [--describe] [--trace FILE] [--trace-every S]\n"
            "                    [--engine life|classic]   (life = include/life/organism.h, the default)\n"
            "                    [--evolve G --pop P --births B --out genome.txt]   evolve innate wiring + constitution\n"
            "                    [--genome genome.txt]     run a saved (evolved) genome\n"
            "                    [--stations 1|2] [--station-buffer AH]   (0 = unlimited mains station)\n"
            "                    [--off satiety,progress,places]   switch life-engine mechanisms off\n"
            "--hours 0 (default) caps each life at 2.5x the body's idle endurance\n"
            "organism 0 is the example build; 1.. are generated bodies (see body_plan.h)\n"
            "conditions:\n");
    for (auto& c : kConditions) fprintf(stderr, "  %-20s %s%s\n", c.name, c.what, c.example_only ? " [organism 0]" : "");
}

}  // namespace

int main(int argc, char** argv) {
    std::string condition = "all";
    int runs = 10, lives = 1, organism = 0;
    float hours = 0.0f;
    uint32_t seed = 1;
    bool json = false, describe = false, life_engine = true;
    const char* trace_path = nullptr;
    const char* genome_path = nullptr;
    const char* out_path = nullptr;
    int evolve = 0, pop = 24, births = 2, evo_lives = 2;
    float trace_interval_s = 1.0f;
    WorldConfig wcfg;

    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage();
                exit(2);
            }
            return argv[++i];
        };
        if (!strcmp(argv[i], "--condition")) condition = next();
        else if (!strcmp(argv[i], "--organism")) organism = atoi(next());
        else if (!strcmp(argv[i], "--runs")) runs = atoi(next());
        else if (!strcmp(argv[i], "--lives")) lives = atoi(next());
        else if (!strcmp(argv[i], "--hours")) hours = static_cast<float>(atof(next()));
        else if (!strcmp(argv[i], "--seed")) seed = static_cast<uint32_t>(atoi(next()));
        else if (!strcmp(argv[i], "--rssi-period")) wcfg.rssi_period_override = static_cast<float>(atof(next()));
        else if (!strcmp(argv[i], "--json")) json = true;
        else if (!strcmp(argv[i], "--engine")) {
            const char* e = next();
            if (!strcmp(e, "life")) life_engine = true;
            else if (!strcmp(e, "classic")) life_engine = false;
            else {
                usage();
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--describe")) describe = true;
        else if (!strcmp(argv[i], "--trace")) trace_path = next();
        else if (!strcmp(argv[i], "--trace-every")) trace_interval_s = static_cast<float>(atof(next()));
        else if (!strcmp(argv[i], "--genome")) genome_path = next();
        else if (!strcmp(argv[i], "--evolve")) evolve = atoi(next());
        else if (!strcmp(argv[i], "--pop")) pop = atoi(next());
        else if (!strcmp(argv[i], "--births")) births = atoi(next());
        else if (!strcmp(argv[i], "--out")) out_path = next();
        else if (!strcmp(argv[i], "--off")) {
            const char* v = next();
            g_traits.satiety = !strstr(v, "satiety");
            g_traits.progress = !strstr(v, "progress");
            g_traits.places = !strstr(v, "places");
        }
        else if (!strcmp(argv[i], "--stations")) wcfg.stations = atoi(next());
        else if (!strcmp(argv[i], "--station-buffer")) wcfg.station_buffer_ah = static_cast<float>(atof(next()));
        else if (!strcmp(argv[i], "--evo-lives")) evo_lives = atoi(next());
        else {
            usage();
            return 2;
        }
    }

    BodyPlan plan = plan_for(organism);
    // --hours 0 (the default): cap each life at 2.5x this body's idle
    // endurance, the time it would last sitting perfectly still. Anything
    // past 1x idle endurance can only come from charging.
    float idle_h = 0.8f * plan.capacity_ah / plan.base_current_a;
    if (hours <= 0.0f) hours = std::min(10.0f, std::max(1.0f, 2.5f * idle_h));
    if (describe) {
        printf("%s\n", plan_json(plan).c_str());
        return 0;
    }

    if (evolve > 0) {
        Evolver ev{plan, wcfg, hours, births, evo_lives};
        ev.run(evolve, pop, out_path);
        return 0;
    }

    EvoGenome loaded;
    if (genome_path && !load_genome(genome_path, loaded)) {
        perror(genome_path);
        return 1;
    }
    const EvoGenome* evo = genome_path ? &loaded : nullptr;

    if (trace_path) {
        Condition c = Condition::kFull;
        for (auto& ci : kConditions)
            if (condition == ci.name) c = ci.c;
        FILE* f = fopen(trace_path, "w");
        if (!f) {
            perror(trace_path);
            return 1;
        }
        wcfg.seed = seed;
        if (c == Condition::kNoBatteryLink) plan.battery_linked = false;
        auto r = std::make_unique<Runner>(c, plan, wcfg, hours, lives, life_engine);
        r->evo = evo;
        fprintf(f,
                "{\"meta\":{\"organism\":%s,\"condition\":%s,\"seed\":%u,\"room\":[%g,%g],\"station_y\":%g,"
                "\"engine\":\"%s\",\"funnel_depth\":%g,\"interval_s\":%g,\"cap_h\":%g,\"idle_h\":%g,\"stations\":%d,\"station_buffer_ah\":%g,\"columns\":[\"life\",\"t\",\"x\",\"y\",\"theta\",\"soc\",\"docked\",\"head\"",
                plan_json(plan).c_str(), jstr(condition == "all" ? "full" : condition).c_str(), seed, wcfg.room_w,
                wcfg.room_h, wcfg.station_y, life_engine ? "life" : "classic", wcfg.funnel_depth, trace_interval_s, hours, idle_h,
                wcfg.stations, wcfg.station_buffer_ah);
        for (int k = 0; k < wcfg.stations; k++) fprintf(f, ",\"station%d\"", k);
        if (life_engine) {
            fprintf(f, ",\"hunger\",\"pain\",\"boredom\",\"dopamine\",\"adrenaline\",\"cortisol\",\"serotonin\",\"value\",\"explore\"");
        } else {
            for (size_t v = 0; v < kPhysVarCount; v++)
                fprintf(f, ",%s", jstr(Physiology::var_name(static_cast<PhysVar>(v))).c_str());
            fprintf(f, ",\"fuzz\"");
        }
        for (auto& s : plan.sensors) fprintf(f, ",%s", jstr("in:" + s.name).c_str());
        for (auto& a : plan.actuators) fprintf(f, ",%s", jstr("out:" + a.name).c_str());
        fprintf(f, "]},\"rows\":[\n");
        r->trace = f;
        r->trace_every = std::max(1L, static_cast<long>(trace_interval_s / kDt + 0.5f));
        std::vector<Metrics> ms = r->run_all();
        fprintf(f, "],\"lives\":[");
        for (size_t i = 0; i < ms.size(); i++) fprintf(f, "%s%s", i ? "," : "", metrics_json(ms[i]).c_str());
        fprintf(f, "],\"reflexes\":%s}\n", reflexes_json(*r, 12).c_str());
        fclose(f);
        for (size_t i = 0; i < ms.size(); i++) {
            const Metrics& m = ms[i];
            printf("life %zu: lived %.2f h%s, %d dockings (%d hungry), charged %.3f Ah, %.1f%% docked, coverage %.0f%%\n",
                   i + 1, m.lifespan_h, m.died ? " (died)" : "", m.dock_events, m.hungry_dockings, m.charge_ah,
                   100 * m.docked_frac, 100 * m.coverage);
        }
        return 0;
    }

    if (json) {
        printf("{\"organism\":%s,\"runs\":%d,\"lives\":%d,\"hours\":%g,\"idle_endurance_h\":%g,\"conditions\":{",
               plan_json(plan).c_str(), runs, lives, hours, idle_h);
    } else {
        printf("organism %s (%s; %s)\n", plan.title.c_str(), loco_name(plan.loco), plan.temperament.c_str());
        printf("%d runs x %d lives (max %.1f h each; idle endurance %.2f h) per condition\n\n", runs, lives, hours, idle_h);
        printf("%-18s %11s %6s %9s %8s %6s %9s %8s %15s %8s %8s %6s\n", "condition", "lifespan_h", "died", "dockings",
               "docked%", "full%", "coverage", "speed", "dist hungry/ok", "entropy", "switches", "places");
    }

    std::vector<std::pair<const char*, std::vector<Summary>>> curves;
    bool first_cond = true;
    for (auto& ci : kConditions) {
        if (condition != "all" && condition != ci.name) continue;
        if (ci.example_only && (organism != 0 || life_engine)) continue;
        if (life_engine && (ci.c == Condition::kNoContingency || ci.c == Condition::kNoSpatial)) continue;
        BodyPlan p = plan;
        if (ci.c == Condition::kNoBatteryLink) p.battery_linked = false;
        Summary life, died, docks, docked, fulld, cov, spd, dh, ds, ent, sw, pl;
        std::vector<Summary> curve(lives);
        std::string lives_json, reflexes;
        for (int k = 0; k < runs; k++) {
            wcfg.seed = seed + k;
            auto r = std::make_unique<Runner>(ci.c, p, wcfg, hours, lives, life_engine);
            r->evo = evo;
            std::vector<Metrics> ms = r->run_all();
            if (k == 0 && ci.c == Condition::kFull) reflexes = reflexes_json(*r, 8);
            lives_json += std::string(k ? "," : "") + "[";
            for (size_t li = 0; li < ms.size(); li++) {
                const Metrics& m = ms[li];
                lives_json += std::string(li ? "," : "") + metrics_json(m);
                curve[li].add(m.lifespan_h);
                life.add(m.lifespan_h);
                died.add(m.died ? 1 : 0);
                docks.add(m.dock_events);
                docked.add(100 * m.docked_frac);
                cov.add(100 * m.coverage);
                spd.add(m.mean_speed);
                dh.add(m.dist_when_hungry);
                ds.add(m.dist_when_sated);
                ent.add(m.behavior_entropy);
                sw.add(m.patch_switches);
                fulld.add(100 * m.full_docked_frac);
                pl.add(m.places);
            }
            lives_json += "]";
        }
        if (json) {
            printf("%s%s:{\"runs\":[%s]%s}", first_cond ? "" : ",", jstr(ci.name).c_str(), lives_json.c_str(),
                   reflexes.empty() ? "" : (",\"reflexes\":" + reflexes).c_str());
            first_cond = false;
        } else {
            printf("%-18s %5.2f±%-4.2f %5.0f%% %4.1f±%-3.1f %7.1f%% %5.0f%% %8.0f%% %8.3f %6.2f / %-6.2f %8.2f %8.1f %6.1f\n",
                   ci.name, life.mean(), life.sd(), 100 * died.mean(), docks.mean(), docks.sd(), docked.mean(), fulld.mean(),
                   cov.mean(), spd.mean(), dh.mean(), ds.mean(), ent.mean(), sw.mean(), pl.mean());
            fflush(stdout);
        }
        curves.emplace_back(ci.name, curve);
    }

    if (json) {
        printf("}}\n");
        return 0;
    }
    if (lives > 1) {
        printf("\nlifespan (h) by life number — does it get better at staying alive?\n%-18s", "condition");
        for (int li = 0; li < lives; li++) printf(" %6d", li + 1);
        printf("\n");
        for (auto& [name, curve] : curves) {
            printf("%-18s", name);
            for (auto& sm : curve) printf(" %6.2f", sm.mean());
            printf("\n");
        }
    }
    return 0;
}
