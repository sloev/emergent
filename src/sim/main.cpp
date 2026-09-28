// Emergent simulator: runs the unmodified behavior engine (physiology,
// contingency memory, spatial memory, action generator) in a 2D room with a
// charging station, and scores what it does.
//
//   ./emergent-sim --runs 20 --hours 6            # compare all conditions
//   ./emergent-sim --condition full --seed 3 --hours 2 --trace out.csv
//
// Conditions are ablations of the same organism, plus a null model that
// uses no engine at all, so every number has something to be compared to.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/action_generator.h"
#include "core/contingency_memory.h"
#include "core/physiology.h"
#include "core/spatial_memory.h"
#include "sim_body.h"
#include "world.h"

namespace {

using namespace sim;

enum class Condition { kFull, kHandWired, kHandWiredPlastic, kNoLearning, kNoContingency, kNoSpatial, kNoBatteryLink, kNoiseOnly, kRandomWalk };

struct ConditionInfo {
    Condition c;
    const char* name;
    const char* what;
};

const ConditionInfo kConditions[] = {
    {Condition::kFull, "full", "unmodified engine"},
    {Condition::kHandWired, "hand_wired", "reflexes fixed by hand to phototaxis, not learned (upper bound)"},
    {Condition::kHandWiredPlastic, "hand_wired_plastic", "starts hand-wired, then learns (does learning keep it?)"},
    {Condition::kNoLearning, "no_reflexes", "learned sensorimotor reflexes off"},
    {Condition::kNoContingency, "no_contingency", "contingency bias never fires"},
    {Condition::kNoSpatial, "no_spatial", "spatial nudge never fires"},
    {Condition::kNoBatteryLink, "no_battery_link", "battery not wired to h_energy"},
    {Condition::kNoiseOnly, "noise_only", "no memory, no reflexes: drives + noise"},
    {Condition::kRandomWalk, "random_walk", "null model: run-and-tumble, no engine"},
};

struct Metrics {
    float lifespan_h = 0;
    bool died = false;
    int dock_events = 0;
    int hungry_dockings = 0;      // dockings that began while h_energy drive > 0
    float hungry_charge_ah = 0;   // charge taken on while hungry
    float docked_frac = 0;
    float charge_ah = 0;
    float coverage = 0;
    float mean_speed = 0;
    float dist_when_hungry = 0;   // mean distance to station while h_energy drive > 0
    float dist_when_sated = 0;    // ... while it's 0
    float behavior_entropy = 0;   // bits, mean over 60 s windows
    float behavior_entropy_sd = 0;
    float var_mean[kPhysVarCount] = {};
    float drive_on[kPhysVarCount] = {};  // fraction of time each drive is active
    float mean_fuzz = 0;
    int contingency_entries = 0;
    int places = 0;
};

constexpr float kDt = 0.05f;           // 20 Hz, same as firmware
constexpr int kCoverageCells = 30;     // 10 cm grid over 3 m

// One organism across one or more lives. A life ends when the battery is
// flat away from the dock. The next life is what an experimenter would do
// with the real robot: recharge it, put it down somewhere random, power it
// on. Memories persist (the firmware saves them as life-state); physiology
// restarts from its boot values.
struct Runner {
    Runner(Condition c, const WorldConfig& w, float h, int n, FILE* t)
        : cond(c), wcfg(w), hours(h), lives(n), trace(t) {}

    Condition cond;
    WorldConfig wcfg;
    float hours;          // cap per life
    int lives = 1;
    FILE* trace = nullptr;

    World world{wcfg};
    SimBody body{cond != Condition::kNoBatteryLink};
    Physiology phys;
    ContingencyMemory cm, cm_empty;
    SpatialMemory spatial, spatial_empty;
    ActionGenerator gen;
    std::mt19937 rng{wcfg.seed + 99};
    int trace_every = 20;  // 1 Hz

    std::vector<Metrics> run_all() {
        cm.begin(body);
        cm_empty.begin(body);
        gen.set_seed(wcfg.seed * 2654435761u + 1);
        gen.begin_learning(body);
        gen.set_learning(cond != Condition::kNoLearning && cond != Condition::kNoiseOnly);
        if (cond == Condition::kHandWired || cond == Condition::kHandWiredPlastic) hand_wire();
        if (cond == Condition::kHandWired) gen.set_plasticity(false);
        std::vector<Metrics> out;
        for (int life = 0; life < lives; life++) {
            if (life > 0) world.reset_robot();
            out.push_back(run_life(life));
        }
        return out;
    }

    // Braitenberg 2b in the hunger context: each light sensor excites the
    // opposite wheel, so the body turns toward and drives at the brightest
    // light; a very bright reading (at the dock) brakes. Learning stays on
    // so it can still adapt, but it starts from a working wiring.
    void hand_wire() {
        SensorimotorPolicy& p = gen.policy();
        size_t e = 1 + static_cast<size_t>(PhysVar::kEnergy);
        size_t bias = p.input_count();
        p.set_weight(e, kLightL, kWheelR, 1.5f);
        p.set_weight(e, kLightR, kWheelL, 1.5f);
        p.set_weight(e, kLightL, kWheelL, -0.8f);
        p.set_weight(e, kLightR, kWheelR, -0.8f);
        p.set_weight(e, bias, kWheelL, 0.2f);
        p.set_weight(e, bias, kWheelR, 0.2f);
        p.set_weight(e, kMic, kWheelL, -1.5f);   // station clicks while charging: stop
        p.set_weight(e, kMic, kWheelR, -1.5f);
    }

    Metrics run_life(int life) {
        phys.begin(body);
        // Null-model state.
        float rw_timer = 0, rw_l = 0, rw_r = 0;

        Metrics m;
        bool visited[kCoverageCells][kCoverageCells] = {};
        double speed_sum = 0, hungry_d = 0, sated_d = 0;
        long hungry_n = 0, sated_n = 0, docked_ticks = 0;
        bool was_charging = false;
        float prev_soc = world.soc();
        int window_hist[9] = {};
        int window_n = 0;
        std::vector<float> window_entropies;

        long total_ticks = static_cast<long>(hours * 3600.0f / kDt);
        long tick = 0;
        uint32_t now_ms = 0;

        for (; tick < total_ticks; tick++) {
            now_ms += 50;
            body.sense(world.read());

            if (cond == Condition::kRandomWalk) {
                // Classic run-and-tumble at a fixed cruise: the "no engine" baseline.
                rw_timer -= kDt;
                if (rw_timer <= 0 || world.bumped()) {
                    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
                    rw_timer = 1.0f + 3.0f * std::fabs(u(rng));
                    float turn = u(rng) * 0.5f;
                    rw_l = 0.4f - turn;
                    rw_r = 0.4f + turn;
                }
                body.actuator_at(kWheelL).write(rw_l);
                body.actuator_at(kWheelR).write(rw_r);
                // Keep physiology running so energy metrics are comparable.
                phys.update(body, kDt, 0.0f);
            } else {
                ContingencyMemory& cm_for_action =
                    (cond == Condition::kNoContingency || cond == Condition::kNoiseOnly) ? cm_empty : cm;
                SpatialMemory& sp_for_action =
                    (cond == Condition::kNoSpatial || cond == Condition::kNoiseOnly) ? spatial_empty : spatial;

                phys.update(body, kDt, cm.last_surprise());
                cm.update(body, phys, kDt);
                spatial.update(body, phys, kDt);
                gen.tick(body, phys, cm_for_action, sp_for_action, now_ms);
            }

            // Firmware SafetyMonitor's battery floor: below 5% of the
            // energy channel, every actuator is forced to 0.
            if (body.sensor_at(kBattery).last_value() < 0.05f) {
                for (size_t i = 0; i < body.actuator_count(); i++) body.actuator_at(i).force(0.0f);
            }

            world.step(body.commands(), kDt);

            // --- metrics ---------------------------------------------------
            const Pose& p = world.pose();
            int cx = std::min(kCoverageCells - 1, std::max(0, static_cast<int>(p.x / 0.1f)));
            int cy = std::min(kCoverageCells - 1, std::max(0, static_cast<int>(p.y / 0.1f)));
            visited[cx][cy] = true;
            speed_sum += world.speed();
            if (world.docked()) docked_ticks++;
            bool hungry = phys.drive(PhysVar::kEnergy) > 0.0f;
            if (world.charging() && !was_charging) {
                m.dock_events++;
                if (hungry) m.hungry_dockings++;
            }
            was_charging = world.charging();
            if (world.soc() > prev_soc) {
                m.charge_ah += (world.soc() - prev_soc) * wcfg.capacity_ah;
                if (hungry) m.hungry_charge_ah += (world.soc() - prev_soc) * wcfg.capacity_ah;
            }
            prev_soc = world.soc();

            for (size_t v = 0; v < kPhysVarCount; v++) {
                m.var_mean[v] += phys.value(static_cast<PhysVar>(v));
                if (phys.drive(static_cast<PhysVar>(v)) > 0.0f) m.drive_on[v] += 1;
            }
            m.mean_fuzz += phys.fuzz_scale();

            float d = world.distance_to_station();
            if (phys.drive(PhysVar::kEnergy) > 0.0f) {
                hungry_d += d;
                hungry_n++;
            } else {
                sated_d += d;
                sated_n++;
            }

            auto sgn = [](float v) { return v > 0.1f ? 2 : (v < -0.1f ? 0 : 1); };
            window_hist[sgn(body.actuator_at(kWheelL).value()) * 3 + sgn(body.actuator_at(kWheelR).value())]++;
            if (++window_n == static_cast<int>(60.0f / kDt)) {
                float h = 0;
                for (int k = 0; k < 9; k++) {
                    if (window_hist[k] == 0) continue;
                    float q = static_cast<float>(window_hist[k]) / window_n;
                    h -= q * std::log2(q);
                }
                window_entropies.push_back(h);
                memset(window_hist, 0, sizeof(window_hist));
                window_n = 0;
            }

            if (trace && tick % trace_every == 0) {
                fprintf(trace, "%d,%.1f,%.3f,%.3f,%.3f,%.3f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.3f,%.3f,%.3f,%.3f\n",
                        life, world.time(), p.x, p.y, p.theta, world.soc(), world.docked() ? 1 : 0,
                        phys.value(PhysVar::kEnergy), phys.value(PhysVar::kFatigue), phys.value(PhysVar::kSafety),
                        phys.value(PhysVar::kArousal), phys.value(PhysVar::kCuriosity), phys.value(PhysVar::kBoredom),
                        phys.fuzz_scale(), body.actuator_at(kWheelL).value(), body.actuator_at(kWheelR).value(),
                        body.actuator_at(kHead).value(), body.actuator_at(kLed).value(),
                        body.actuator_at(kBuzzer).value(), phys.total_drive(), gen.policy().last_advantage(),
                        phys.drive(PhysVar::kEnergy), phys.drive(PhysVar::kSafety), phys.drive(PhysVar::kBoredom));
            }

            if (world.soc() <= 0.0f && !world.docked()) {
                m.died = true;
                tick++;
                break;
            }
        }

        float ticks = static_cast<float>(tick);
        m.lifespan_h = ticks * kDt / 3600.0f;
        m.docked_frac = docked_ticks / ticks;
        for (size_t v = 0; v < kPhysVarCount; v++) {
            m.var_mean[v] /= ticks;
            m.drive_on[v] /= ticks;
        }
        m.mean_fuzz /= ticks;
        int cells = 0;
        for (auto& row : visited)
            for (bool v : row) cells += v;
        m.coverage = static_cast<float>(cells) / (kCoverageCells * 25);  // room is 30 x 25 cells
        m.mean_speed = static_cast<float>(speed_sum / ticks);
        m.dist_when_hungry = hungry_n ? static_cast<float>(hungry_d / hungry_n) : NAN;
        m.dist_when_sated = sated_n ? static_cast<float>(sated_d / sated_n) : NAN;
        if (!window_entropies.empty()) {
            double s = 0, s2 = 0;
            for (float e : window_entropies) {
                s += e;
                s2 += e * e;
            }
            double n = window_entropies.size();
            m.behavior_entropy = static_cast<float>(s / n);
            m.behavior_entropy_sd = static_cast<float>(std::sqrt(std::max(0.0, s2 / n - (s / n) * (s / n))));
        }
        m.contingency_entries = static_cast<int>(cm.count());
        m.places = static_cast<int>(spatial.count());
        return m;
    }
};

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

// The learned wiring, per motivational context: which sensor drives which
// actuator. Only weights that grew past a threshold are shown.
void print_reflexes(Runner& r) {
    const SensorimotorPolicy& p = r.gen.policy();
    printf("\nlearned reflexes (|w| > 0.3), context: sensor -> actuator\n");
    for (size_t k = 0; k < SensorimotorPolicy::kContexts; k++) {
        const char* ctx = k == 0 ? "always" : Physiology::var_name(static_cast<PhysVar>(k - 1));
        for (size_t i = 0; i <= p.input_count(); i++) {
            const char* in = i < p.input_count() ? kSimSensors[i].name : "(bias)";
            for (size_t j = 0; j < p.output_count(); j++) {
                float w = p.weight(k, i, j);
                if (std::fabs(w) > 0.3f) printf("  %-12s %-16s -> %-14s %+.2f\n", ctx, in, kSimActuators[j].name, w);
            }
        }
    }
}

void usage() {
    fprintf(stderr,
            "usage: emergent-sim [--condition NAME|all] [--runs N] [--hours H] [--seed S]\n"
            "                    [--lives N] [--rssi-period S] [--capacity AH] [--trace FILE]\n"
            "conditions:\n");
    for (auto& c : kConditions) fprintf(stderr, "  %-16s %s\n", c.name, c.what);
}

}  // namespace

int main(int argc, char** argv) {
    std::string condition = "all";
    int runs = 10;
    float hours = 4.0f;
    uint32_t seed = 1;
    int lives = 1;
    const char* trace_path = nullptr;
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
        else if (!strcmp(argv[i], "--runs")) runs = atoi(next());
        else if (!strcmp(argv[i], "--hours")) hours = static_cast<float>(atof(next()));
        else if (!strcmp(argv[i], "--seed")) seed = static_cast<uint32_t>(atoi(next()));
        else if (!strcmp(argv[i], "--rssi-period")) wcfg.rssi_scan_period_s = static_cast<float>(atof(next()));
        else if (!strcmp(argv[i], "--capacity")) wcfg.capacity_ah = static_cast<float>(atof(next()));
        else if (!strcmp(argv[i], "--lives")) lives = atoi(next());
        else if (!strcmp(argv[i], "--trace")) trace_path = next();
        else {
            usage();
            return 2;
        }
    }

    if (trace_path) {
        Condition c = Condition::kFull;
        for (auto& ci : kConditions)
            if (condition == ci.name) c = ci.c;
        FILE* f = fopen(trace_path, "w");
        if (!f) {
            perror(trace_path);
            return 1;
        }
        fprintf(f, "life,t,x,y,theta,soc,docked,h_energy,h_fatigue,h_safety,h_arousal,h_curiosity,h_boredom,fuzz,"
                   "stepper_left,stepper_right,head_pan,led,buzzer,total_drive,advantage,d_energy,d_safety,d_boredom\n");
        wcfg.seed = seed;
        auto r = std::make_unique<Runner>(c, wcfg, hours, lives, f);
        std::vector<Metrics> ms = r->run_all();
        fclose(f);
        for (size_t i = 0; i < ms.size(); i++) {
            const Metrics& m = ms[i];
            printf("life %zu: lived %.2f h%s, %d dockings (%d hungry), charged %.3f Ah, %.1f%% docked, "
                   "coverage %.0f%%\n",
                   i + 1, m.lifespan_h, m.died ? " (died)" : "", m.dock_events, m.hungry_dockings, m.charge_ah,
                   100 * m.docked_frac, 100 * m.coverage);
            printf("        ");
            for (size_t v = 0; v < kPhysVarCount; v++)
                printf("%s %.2f (on %.0f%%)  ", Physiology::var_name(static_cast<PhysVar>(v)), m.var_mean[v],
                       100 * m.drive_on[v]);
            printf("fuzz %.2f\n", m.mean_fuzz);
        }
        print_reflexes(*r);
        return 0;
    }

    printf("%d organisms x %d lives (max %.1f h each) per condition, battery %.2f Ah, RSSI scan every %.1f s\n\n",
           runs, lives, hours, wcfg.capacity_ah, wcfg.rssi_scan_period_s);
    printf("%-18s %11s %6s %9s %8s %9s %8s %15s %13s\n", "condition", "lifespan_h", "died", "dockings", "docked%",
           "coverage", "speed", "dist hungry/ok", "entropy(sd)");

    std::vector<std::pair<const char*, std::vector<Summary>>> curves;
    for (auto& ci : kConditions) {
        if (condition != "all" && condition != ci.name) continue;
        Summary life, died, docks, docked, cov, spd, dh, ds, ent, entsd;
        std::vector<Summary> curve(lives);
        for (int k = 0; k < runs; k++) {
            wcfg.seed = seed + k;
            auto r = std::make_unique<Runner>(ci.c, wcfg, hours, lives, nullptr);
            std::vector<Metrics> ms = r->run_all();
            for (size_t li = 0; li < ms.size(); li++) {
                const Metrics& m = ms[li];
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
                entsd.add(m.behavior_entropy_sd);
            }
        }
        printf("%-18s %5.2f±%-4.2f %5.0f%% %4.1f±%-3.1f %7.1f%% %8.0f%% %8.3f %6.2f / %-6.2f %5.2f(%.2f)\n", ci.name,
               life.mean(), life.sd(), 100 * died.mean(), docks.mean(), docks.sd(), docked.mean(), cov.mean(),
               spd.mean(), dh.mean(), ds.mean(), ent.mean(), entsd.mean());
        fflush(stdout);
        curves.emplace_back(ci.name, curve);
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
